#include "CatalogoProxies.h"

#include "../Model/Project.h"
#include "../Vault/Resolucao.h"

namespace matriz::catalogo {

using matriz::db::Value;

namespace {

const char* kArquivoBanco = "catalogo.db";
const char* kPastaMiniaturas = "miniaturas";

// Schema do catálogo. Auto-contido de propósito: nenhuma referência ao
// registro nem ao índice do projeto que o gerou, e nenhum caminho pra dentro
// dele. É isso que faz o catálogo abrir numa máquina que nunca viu o projeto.
const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS catalogo_info (
    chave   TEXT PRIMARY KEY,
    valor   TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS proxy_item (
    item_id                 TEXT PRIMARY KEY,
    codigo_acervo           TEXT NOT NULL,
    titulo                  TEXT NOT NULL,
    tipo_midia              TEXT,
    -- Onde o original estava quando foi indexado. É o que permite dizer
    -- "está no HD Samsung T7 / 2003 / Berlim" com a fonte desconectada.
    caminho_origem          TEXT,
    volume_origem           TEXT,
    -- Onde a cópia está DENTRO deste backup, relativo à raiz dele.
    caminho_no_backup       TEXT,
    checksum_sha256         TEXT,
    tamanho_bytes           INTEGER NOT NULL DEFAULT 0,
    -- Relativo à pasta do catálogo, nunca absoluto: caminho absoluto
    -- quebraria assim que o disco fosse montado noutro ponto.
    miniatura_relativa      TEXT,
    tem_forma_onda          INTEGER NOT NULL DEFAULT 0,
    metadado_tecnico_json   TEXT,
    ficha_json              TEXT
);
)SQL";

// "/Volumes/HD Samsung T7/2003/Berlim/x.wav" -> "HD Samsung T7"
//
// Resolvido no momento da GERAÇÃO, quando o volume ainda está montado — é
// por isso que o nome fica gravado no catálogo em vez de ser recalculado na
// leitura: quando o operador for consultar, o disco provavelmente não está
// mais lá, e é justamente aí que o nome dele importa.
juce::String volumeDoCaminho(const juce::String& caminhoAbsoluto) {
    if (caminhoAbsoluto.isEmpty()) return {};

    juce::StringArray partes;
    partes.addTokens(caminhoAbsoluto, juce::File::getSeparatorString(), "");
    partes.removeEmptyStrings();

#if JUCE_MAC
    // Disco externo no macOS monta em /Volumes/<nome> — o nome está ali,
    // literal, e não depende do arquivo ainda existir.
    if (partes.size() >= 2 && partes[0] == "Volumes") return partes[1];
#endif

    // Disco interno (ou qualquer caminho sem prefixo de montagem): pergunta
    // ao sistema o rótulo do volume. O primeiro segmento do caminho seria
    // "var", "Users", "home" — nome de pasta do sistema, não de disco, e
    // dizer ao operador "está no volume var" não ajuda ninguém.
    juce::String rotulo = juce::File(caminhoAbsoluto).getVolumeLabel();
    if (rotulo.isNotEmpty()) return rotulo;

    return {};
}

// Ficha do item como JSON simples (campo -> valor), pra o catálogo carregar
// o que o operador preencheu sem depender das definições YAML.
juce::String fichaComoJson(matriz::db::Database& registro, const std::string& itemId) {
    auto obj = std::make_unique<juce::DynamicObject>();
    auto stmt = registro.prepare(
        "SELECT campo_id, valor FROM item_campo WHERE item_id = ? AND nivel = 'raiz' AND nivel_indice = 0");
    stmt.bind(1, Value::of(itemId));
    while (stmt.step()) {
        if (stmt.columnIsNull(1)) continue;
        obj->setProperty(juce::String(stmt.columnText(0)), juce::String(stmt.columnText(1)));
    }
    return juce::JSON::toString(juce::var(obj.release()), true);
}

struct ProxyDeItem {
    std::string itemId, codigo, titulo, tipoMidia, checksum;
    juce::String caminhoOrigem, caminhoNoBackup;
    juce::int64 tamanhoBytes = 0;
    juce::File miniaturaNoProjeto; // vazia se não houver
    juce::String metadadoJson;
    bool temFormaOnda = false;
};

// Junta, num lugar só, tudo o que o catálogo precisa saber de cada item.
// Consulta o registro e o índice; nenhuma escrita.
std::vector<ProxyDeItem> levantarProxies(matriz::db::Database& registro, matriz::db::Database& indice,
                                          const juce::File& pastaProjeto) {
    std::vector<ProxyDeItem> out;

    auto stmt = registro.prepare(
        std::string("SELECT i.id, i.codigo_acervo, i.titulo, i.tipo_midia, "
                    "a.caminho_absoluto_origem, a.caminho_relativo, a.checksum_sha256, a.id, "
                    "a.caracteristicas_tecnicas_json, COALESCE(v.localizacao, '') "
                    "FROM item i LEFT JOIN arquivo a ON a.id = "
                    "  (SELECT id FROM arquivo a2 WHERE a2.item_id = i.id ORDER BY a2.eh_master DESC, a2.id LIMIT 1) ") +
        matriz::vault::joinDeResolucao() +
        // codigo_acervo pode ser NULL (§5: só é gasto quando o ingest do
        // arquivo dá certo, e uma duplicata reconhecida nunca gasta um). Sem
        // o teste de NULL primeiro, o SQLite ordena esses itens ANTES de
        // todos os outros e o catálogo abre encabeçado por linhas que não têm
        // arquivo nenhum pra descrever.
        " ORDER BY (i.codigo_acervo IS NULL), i.codigo_acervo");

    while (stmt.step()) {
        ProxyDeItem p;
        p.itemId = stmt.columnText(0);
        p.codigo = stmt.columnText(1);
        p.titulo = stmt.columnText(2);
        if (!stmt.columnIsNull(3)) p.tipoMidia = stmt.columnText(3);
        if (!stmt.columnIsNull(4)) p.caminhoOrigem = juce::String(stmt.columnText(4));
        if (!stmt.columnIsNull(6)) p.checksum = stmt.columnText(6);
        if (!stmt.columnIsNull(8)) p.metadadoJson = juce::String(stmt.columnText(8));

        if (!stmt.columnIsNull(5)) {
            // I5: o master mora no Vault; `pastaProjeto` só entra como
            // fallback de projeto legado.
            auto master = matriz::vault::resolverCaminho(pastaProjeto, stmt.columnText(9), stmt.columnText(5),
                                                          stmt.columnIsNull(4) ? std::string() : stmt.columnText(4));
            if (master) p.tamanhoBytes = master->getSize();
        }

        std::string arquivoId = stmt.columnIsNull(7) ? std::string() : stmt.columnText(7);

        // Onde a cópia ficou dentro do backup — vem do registro da
        // consolidação, que é quem sabe a máscara e a hierarquia aplicadas.
        // Vazio quando o item ainda não foi gravado em backup nenhum: o
        // catálogo continua descrevendo o item (com a origem), só não tem
        // cópia local pra abrir.
        if (!arquivoId.empty()) {
            auto stmtCons = registro.prepare(
                "SELECT caminho_relativo_destino FROM consolidacao_registro WHERE arquivo_id = ? "
                "ORDER BY consolidado_em DESC LIMIT 1");
            stmtCons.bind(1, Value::of(arquivoId));
            if (stmtCons.step()) p.caminhoNoBackup = juce::String(stmtCons.columnText(0));
        }

        // Miniatura mais recente do item (a capa personalizada, quando
        // existe, é justamente a mais recente — ver ProjetoAberto::definirCapa).
        auto stmtMin = indice.prepare(
            "SELECT caminho_relativo FROM miniatura WHERE item_id = ? AND tipo = 'miniatura' "
            "ORDER BY gerado_em DESC LIMIT 1");
        stmtMin.bind(1, Value::of(p.itemId));
        if (stmtMin.step()) {
            juce::File m = pastaProjeto.getChildFile(juce::String(stmtMin.columnText(0)));
            if (m.existsAsFile()) p.miniaturaNoProjeto = m;
        }

        if (!arquivoId.empty()) {
            auto stmtOnda = indice.prepare("SELECT 1 FROM forma_onda WHERE arquivo_id = ? LIMIT 1");
            stmtOnda.bind(1, Value::of(arquivoId));
            p.temFormaOnda = stmtOnda.step();
        }

        out.push_back(std::move(p));
    }
    return out;
}

} // namespace

juce::String descreverLocalizacao(const juce::String& caminhoAbsoluto) {
    if (caminhoAbsoluto.isEmpty()) return {};

    juce::StringArray partes;
    partes.addTokens(caminhoAbsoluto, juce::File::getSeparatorString(), "");
    partes.removeEmptyStrings();
#if JUCE_MAC
    if (partes.size() >= 2 && partes[0] == "Volumes") partes.remove(0);
#endif

    // Sem o nome do arquivo: a pergunta que isto responde é "onde procurar",
    // e o nome já está na frente do operador na própria linha do catálogo.
    if (partes.size() > 1) partes.remove(partes.size() - 1);
    if (partes.isEmpty()) return {};

    // Caminho fundo vira ilegível ("var / folders / fn / 6db7m45 / T / ...").
    // O que o operador precisa é o disco e as últimas pastas — o miolo não
    // ajuda a achar nada e só afoga a informação útil.
    constexpr int kMaxSegmentos = 4;
    if (partes.size() <= kMaxSegmentos) return partes.joinIntoString(" / ");

    juce::StringArray resumo;
    resumo.add(partes[0]); // o disco/raiz
    resumo.add(juce::String(juce::CharPointer_UTF8("\xe2\x80\xa6"))); // reticências
    for (int i = partes.size() - (kMaxSegmentos - 2); i < partes.size(); ++i) resumo.add(partes[i]);
    return resumo.joinIntoString(" / ");
}

EstimativaCatalogo estimar(matriz::db::Database& registro, matriz::db::Database& indice,
                            const juce::File& pastaProjeto) {
    EstimativaCatalogo e;
    for (auto& p : levantarProxies(registro, indice, pastaProjeto)) {
        ++e.itens;
        if (p.miniaturaNoProjeto.existsAsFile()) e.tamanhoBytes += p.miniaturaNoProjeto.getSize();
    }
    return e;
}

ResultadoGeracao gerar(matriz::db::Database& registro, matriz::db::Database& indice,
                        const juce::File& pastaProjeto, const juce::File& destinoBackup,
                        const matriz::consolidacao::AoProgredir& aoProgredir) {
    ResultadoGeracao resultado;

    juce::File pastaCatalogo = destinoBackup.getChildFile(kNomePastaCatalogo);
    juce::File pastaMiniaturas = pastaCatalogo.getChildFile(kPastaMiniaturas);

    // Regenerar apaga o anterior por inteiro — o catálogo é derivado, nunca
    // fonte de verdade, então reconstruir do zero é sempre correto e evita
    // deixar proxy de item que já saiu do projeto.
    pastaCatalogo.deleteRecursively();
    pastaMiniaturas.createDirectory();

    auto proxies = levantarProxies(registro, indice, pastaProjeto);
    resultado.totalPlanejado = static_cast<int>(proxies.size());

    matriz::db::Database banco(pastaCatalogo.getChildFile(kArquivoBanco).getFullPathName().toStdString());
    banco.execScript(kSchema);
    banco.run("INSERT OR REPLACE INTO catalogo_info (chave, valor) VALUES ('gerado_em', ?)",
              {Value::of(matriz::model::agoraIso8601())});
    banco.run("INSERT OR REPLACE INTO catalogo_info (chave, valor) VALUES ('versao', '1')", {});

    banco.exec("BEGIN");
    int processados = 0;
    try {
        for (auto& p : proxies) {
            if (aoProgredir && !aoProgredir(processados, resultado.totalPlanejado)) {
                resultado.cancelado = true;
                break;
            }
            ++processados;

            juce::String miniaturaRelativa;
            if (p.miniaturaNoProjeto.existsAsFile()) {
                juce::File destinoMin =
                    pastaMiniaturas.getChildFile(juce::String(p.itemId) + p.miniaturaNoProjeto.getFileExtension());
                if (p.miniaturaNoProjeto.copyFileTo(destinoMin))
                    miniaturaRelativa = juce::String(kPastaMiniaturas) + "/" + destinoMin.getFileName();
                else
                    resultado.falhas.push_back(p.codigo + ": não foi possível copiar a miniatura");
            }

            banco.run(
                "INSERT OR REPLACE INTO proxy_item (item_id, codigo_acervo, titulo, tipo_midia, caminho_origem, "
                "volume_origem, caminho_no_backup, checksum_sha256, tamanho_bytes, miniatura_relativa, "
                "tem_forma_onda, metadado_tecnico_json, ficha_json) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                {Value::of(p.itemId), Value::of(p.codigo), Value::of(p.titulo), Value::of(p.tipoMidia),
                 Value::of(p.caminhoOrigem.toStdString()),
                 Value::of(volumeDoCaminho(p.caminhoOrigem).toStdString()),
                 Value::of(p.caminhoNoBackup.toStdString()), Value::of(p.checksum),
                 Value::of(static_cast<long long>(p.tamanhoBytes)), Value::of(miniaturaRelativa.toStdString()),
                 Value::of(p.temFormaOnda), Value::of(p.metadadoJson.toStdString()),
                 Value::of(fichaComoJson(registro, p.itemId).toStdString())});
            ++resultado.gravados;
        }
        banco.exec("COMMIT");
    } catch (...) {
        banco.exec("ROLLBACK");
        throw;
    }

    return resultado;
}

juce::File resolverPastaCatalogo(const juce::File& pasta) {
    if (pasta.getChildFile(kArquivoBanco).existsAsFile()) return pasta;
    juce::File dentro = pasta.getChildFile(kNomePastaCatalogo);
    if (dentro.getChildFile(kArquivoBanco).existsAsFile()) return dentro;
    return {};
}

bool ehPastaDeCatalogo(const juce::File& pasta) { return resolverPastaCatalogo(pasta) != juce::File(); }

std::vector<EntradaCatalogo> abrir(const juce::File& pasta) {
    std::vector<EntradaCatalogo> out;
    juce::File pastaCatalogo = resolverPastaCatalogo(pasta);
    if (pastaCatalogo == juce::File()) return out;

    matriz::db::Database banco(pastaCatalogo.getChildFile(kArquivoBanco).getFullPathName().toStdString());
    auto stmt = banco.prepare(
        "SELECT item_id, codigo_acervo, titulo, tipo_midia, caminho_origem, volume_origem, caminho_no_backup, "
        "checksum_sha256, tamanho_bytes, miniatura_relativa, tem_forma_onda, metadado_tecnico_json, ficha_json "
        // Mesma regra da geração: item sem código de acervo (§5 — duplicata
        // reconhecida, ou ingest que falhou) vai pro fim, não encabeça a
        // lista com uma linha sem arquivo pra descrever.
        "FROM proxy_item ORDER BY (codigo_acervo IS NULL OR codigo_acervo = ''), codigo_acervo");
    while (stmt.step()) {
        EntradaCatalogo e;
        e.itemId = stmt.columnText(0);
        e.codigoAcervo = stmt.columnText(1);
        e.titulo = stmt.columnText(2);
        e.tipoMidia = stmt.columnText(3);
        e.caminhoOrigem = juce::String(stmt.columnText(4));
        e.volumeOrigem = juce::String(stmt.columnText(5));
        e.caminhoNoBackup = juce::String(stmt.columnText(6));
        e.checksumSha256 = stmt.columnText(7);
        e.tamanhoBytes = stmt.columnInt(8);
        e.miniaturaRelativa = juce::String(stmt.columnText(9));
        e.temFormaOnda = stmt.columnInt(10) != 0;
        e.metadadoTecnicoJson = juce::String(stmt.columnText(11));
        e.fichaJson = juce::String(stmt.columnText(12));
        out.push_back(std::move(e));
    }
    return out;
}

Localizacao localizar(const EntradaCatalogo& entrada, const juce::File& pastaCatalogo) {
    Localizacao loc;

    // 1. A cópia dentro do próprio backup. Se o catálogo abriu, o backup
    //    quase sempre está montado junto — é a fonte mais próxima.
    if (entrada.caminhoNoBackup.isNotEmpty()) {
        juce::File raizBackup = pastaCatalogo.getParentDirectory();
        juce::File noBackup = raizBackup.getChildFile(entrada.caminhoNoBackup);
        if (noBackup.existsAsFile()) {
            loc.fonteConectada = true;
            loc.arquivoReal = noBackup;
            loc.descricaoHumana = descreverLocalizacao(noBackup.getFullPathName());
            return loc;
        }
    }

    // 2. O caminho de origem, se aquele volume estiver montado agora.
    if (entrada.caminhoOrigem.isNotEmpty()) {
        juce::File origem(entrada.caminhoOrigem);
        if (origem.existsAsFile()) {
            loc.fonteConectada = true;
            loc.arquivoReal = origem;
            loc.descricaoHumana = descreverLocalizacao(entrada.caminhoOrigem);
            return loc;
        }
    }

    // 3. Nada montado: diz onde procurar.
    loc.fonteConectada = false;
    loc.descricaoHumana = descreverLocalizacao(entrada.caminhoOrigem);
    return loc;
}

} // namespace matriz::catalogo
