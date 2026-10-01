#include "ProjetoAberto.h"
#include "TraducaoContent.h"
#include "../Diag/Watchdog.h"

#include "../Ingest/Miniaturas.h"
#include "../Ingest/ProcessoExterno.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/CacheArquivo.h"
#include "../Vault/Reconciliacao.h"
#include "../Model/NotasEstruturadas.h"
#include "../Vault/Resolucao.h"
#include "../Consolidacao/Consolidacao.h"
#include "../Model/ProjectLog.h"
#include "../Model/NomesCanonicos.h"
#include "../Consolidacao/PacoteCollection.h"

#include "../I18n/Strings.h"

#include <algorithm>
#include <unordered_map>
#include <thread>
#include "EventBus.h"
#include "ProgressoGlobal.h"
#include "../Ficha/OrigemPadrao.h"

namespace matriz::ui {

namespace {

// Nó intermediário de construção da árvore (Origem ou Acervo) — heap-
// alocado via unique_ptr de propósito: o endereço de cada nó precisa ficar
// estável enquanto a árvore cresce, porque guardamos ponteiros pra ele em
// mapas de busca (por nome na Origem, por id no Acervo) durante a
// construção. Um std::vector<NoArvore> comum invalidaria esses ponteiros a
// cada realocação — bug real que foi pensado e evitado aqui, não só teórico
// (a árvore Origem pode ter centenas de milhares de segmentos de caminho).
struct NoBuilder {
    juce::String nome;
    std::string id;
    std::string pastaPaiId;
    int posicaoX = 0;
    int posicaoY = 0;
    double escalaNo = 1.0;
    bool ativo = true;
    juce::String corCustomizadaHex;
    juce::String regraOrganizacao;
    std::vector<std::unique_ptr<NoBuilder>> filhos;
    std::map<juce::String, NoBuilder*> indiceFilhosPorNome; // dono é `filhos`; só busca
    std::set<std::string> itemIdsDiretos;
};

NoBuilder* obterOuCriarFilhoPorNome(NoBuilder& pai, const juce::String& nome) {
    auto it = pai.indiceFilhosPorNome.find(nome);
    if (it != pai.indiceFilhosPorNome.end()) return it->second;
    auto novo = std::make_unique<NoBuilder>();
    novo->nome = nome;
    NoBuilder* ptr = novo.get();
    pai.indiceFilhosPorNome[nome] = ptr;
    pai.filhos.push_back(std::move(novo));
    return ptr;
}

juce::String csvDeConjunto(const std::set<juce::String>& s) {
    juce::StringArray arr;
    for (auto& v : s) arr.add(v);
    return arr.joinIntoString(",");
}

std::set<juce::String> conjuntoDeCsv(const juce::String& csv) {
    std::set<juce::String> out;
    for (auto& v : juce::StringArray::fromTokens(csv, ",", ""))
        if (v.isNotEmpty()) out.insert(v);
    return out;
}

ProjetoAberto::NoArvore materializar(const NoBuilder& b, bool ordenarAlfabetico) {
    ProjetoAberto::NoArvore n;
    n.id = b.id;
    n.nome = b.nome;
    n.pastaPaiId = b.pastaPaiId;
    n.posicaoX = b.posicaoX;
    n.posicaoY = b.posicaoY;
    n.escalaNo = b.escalaNo;
    n.ativo = b.ativo;
    n.corCustomizadaHex = b.corCustomizadaHex;
    n.regraOrganizacao = b.regraOrganizacao;
    n.itemIds = b.itemIdsDiretos;
    n.itemIdsDiretos = b.itemIdsDiretos;

    std::vector<const NoBuilder*> ordemFilhos;
    ordemFilhos.reserve(b.filhos.size());
    for (auto& f : b.filhos) ordemFilhos.push_back(f.get());
    if (ordenarAlfabetico)
        std::sort(ordemFilhos.begin(), ordemFilhos.end(),
                  [](const NoBuilder* a, const NoBuilder* c) { return a->nome.compareIgnoreCase(c->nome) < 0; });

    n.filhos.reserve(ordemFilhos.size());
    for (auto* filho : ordemFilhos) {
        ProjetoAberto::NoArvore noFilho = materializar(*filho, ordenarAlfabetico);
        for (auto& id : noFilho.itemIds) n.itemIds.insert(id);
        n.filhos.push_back(std::move(noFilho));
    }
    return n;
}

// Fase 1 — árvore do mapa ORIGINAL: mesma lógica de arvoreOrigem(true)
// (segmentos de caminho de arquivo.caminho_absoluto_origem, prefixo comum
// descartado), mas com um id ESTÁVEL por nó (derivado do caminho
// acumulado) — arvoreOrigem() deixa `id` vazio de propósito pros seus
// outros usos (árvore Origem/Explorer), e ArvoreBackupComponent precisa de
// um id não-vazio pra renderizar, selecionar e abrir "Show in Grid" num
// nó. Puramente computado — nunca lê nem grava acervo_pasta.
ProjetoAberto::NoArvore construirArvoreOriginalVirtual(matriz::db::Database& db) {
    struct Par { std::string itemId; juce::StringArray segmentos; };
    std::vector<Par> pares;
    auto stmt = db.prepare(
        "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a WHERE a.caminho_absoluto_origem IS NOT NULL "
        "AND a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = a.item_id ORDER BY eh_master DESC, id LIMIT 1)");
    while (stmt.step()) {
        Par p;
        p.itemId = stmt.columnText(0);
        juce::File pasta = juce::File(juce::String(stmt.columnText(1))).getParentDirectory();
        p.segmentos.addTokens(pasta.getFullPathName(), juce::File::getSeparatorString(), "");
        p.segmentos.removeEmptyStrings();
        pares.push_back(std::move(p));
    }

    int prefixoComum = 0;
    if (!pares.empty()) {
        prefixoComum = pares[0].segmentos.size();
        for (size_t i = 1; i < pares.size(); ++i) {
            int n = juce::jmin(prefixoComum, pares[i].segmentos.size());
            int match = 0;
            while (match < n && pares[0].segmentos[match] == pares[i].segmentos[match]) ++match;
            prefixoComum = match;
        }
        if (prefixoComum > 0) prefixoComum -= 1;
    }

    NoBuilder raiz;
    for (auto& p : pares) {
        NoBuilder* atual = &raiz;
        juce::String caminhoAcumulado;
        for (int s = prefixoComum; s < p.segmentos.size(); ++s) {
            caminhoAcumulado += (caminhoAcumulado.isEmpty() ? "" : "/") + p.segmentos[s];
            NoBuilder* filho = obterOuCriarFilhoPorNome(*atual, p.segmentos[s]);
            if (filho->id.empty()) {
                filho->id = "original:" + caminhoAcumulado.toStdString();
                // Sem pastaPaiId o canvas não desenha a linha pai->filho.
                filho->pastaPaiId = atual->id;
            }
            atual = filho;
        }
        atual->itemIdsDiretos.insert(p.itemId);
    }
    return materializar(raiz, true);
}

} // namespace

ProjetoAberto::ProjetoAberto(std::unique_ptr<matriz::model::Project> projeto) : projeto_(std::move(projeto)) {
    juce::File registroFile = projeto_->pasta().getChildFile("registro.sqlite");
    juce::File pastaProjeto = projeto_->pasta();
    // Fase 5: destino com papel CLONE abre somente leitura.
    somenteLeitura_ = projeto_->papel() == "CLONE" && projeto_->modo() != matriz::model::Modo::Catalogo;

    const bool somenteLeituraNaAbertura = somenteLeitura_;
    std::thread([registroFile, pastaProjeto, somenteLeituraNaAbertura]() {
        if (somenteLeituraNaAbertura) return;  // manutenção de tamanho_bytes escreve no banco: não num clone
        try {
            matriz::db::Database db(registroFile.getFullPathName().toStdString());

            struct FileToUpdate {
                std::string id;
                std::string localizacaoVault;
                std::string relativo;
                std::string origem;
            };
            std::vector<FileToUpdate> pendentes;

            {
                auto stmt = db.prepare(std::string("SELECT a.id, ") + matriz::vault::colunasDeResolucao() +
                                       " FROM arquivo a " + matriz::vault::joinDeResolucao() +
                                       " WHERE a.tamanho_bytes IS NULL");
                while (stmt.step()) {
                    pendentes.push_back({stmt.columnText(0), stmt.columnText(1), stmt.columnText(2),
                                          stmt.columnText(3)});
                }
            }

            if (!pendentes.empty()) {
                // stat() de cada arquivo ANTES da transação: com ela aberta,
                // esta conexão segura o lock de escrita do arquivo e a
                // conexão principal toma SQLITE_BUSY durante todo o I/O.
                std::vector<std::pair<std::string, juce::int64>> tamanhos;
                tamanhos.reserve(pendentes.size());
                for (const auto& item : pendentes) {
                    auto f = matriz::vault::resolverCaminho(pastaProjeto, item.localizacaoVault, item.relativo,
                                                             item.origem);
                    tamanhos.emplace_back(item.id, f ? f->getSize() : 0);
                }
                db.run("BEGIN TRANSACTION", {});
                try {
                    for (const auto& [id, sz] : tamanhos)
                        db.run("UPDATE arquivo SET tamanho_bytes = ? WHERE id = ?",
                               {matriz::db::Value::of(static_cast<long long>(sz)), matriz::db::Value::of(id)});
                    db.run("COMMIT TRANSACTION", {});
                } catch (...) {
                    try { db.run("ROLLBACK", {}); } catch (...) {}
                }
            }
        } catch (...) {
            // Ignore/log errors safely
        }
    }).detach();
}

void ProjetoAberto::avisarSomenteLeitura() {
    // Alguns mutadores rodam em thread de fundo (ex.: salvarMetadadoEmLote): a janela
    // sempre nasce na message thread.
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        juce::MessageManager::callAsync([] { avisarSomenteLeitura(); });
        return;
    }
    static juce::int64 ultimoAviso = 0;
    const auto agora = juce::Time::currentTimeMillis();
    if (agora - ultimoAviso < 4000) return;  // um aviso, não um por chamada
    ultimoAviso = agora;
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::InfoIcon)
                                     .withTitle(matriz::i18n::t("clone_ro.titulo"))
                                     .withMessage(matriz::i18n::t("clone_ro.bloqueado"))
                                     .withButton(matriz::i18n::t("dialogo.ok")),
                                 juce::ModalCallbackFunction::create([](int) {}));
}

bool ProjetoAberto::reavaliarSomenteLeitura() {
    if (!projeto_) return false;
    projeto_->recarregarPapel();
    const bool novo = projeto_->papel() == "CLONE" && projeto_->modo() != matriz::model::Modo::Catalogo;
    if (novo == somenteLeitura_) return false;
    somenteLeitura_ = novo;
    if (!novo) {
        try {
            matriz::model::ProjectLog(projeto_->pasta()).appendEntry("Clone promoted to MAIN", {"Editing is unlocked for this project."});
        } catch (...) {}
    }
    return true;
}

juce::int64 ProjetoAberto::tamanhoTotalDosMasters() const {
    if (!projeto_) return 0;
    auto stmt = leitura().prepare(
        "SELECT SUM(tamanho_bytes) FROM ("
        "  SELECT COALESCE(tamanho_bytes, 0) as tamanho_bytes, ROW_NUMBER() OVER (PARTITION BY item_id ORDER BY eh_master DESC, id) as rn "
        "  FROM arquivo"
        ") WHERE rn = 1");
    if (stmt.step()) {
        return static_cast<juce::int64>(stmt.columnInt(0));
    }
    return 0;
}

namespace {
// Status offline: MAIN/CLONE primeiro (etapa 2 do modelo SOURCE/MAIN/CLONE) —
// com o backup presente o item NÃO está offline, mesmo com o SOURCE guardado.
// `resolvedor` é criado sob demanda pelo chamador (carrega o
// consolidacao_registro inteiro uma vez).
bool arquivoMasterExiste(const matriz::vault::ResolvedorEmLote& resolvedor,
                         const std::map<std::string, std::string>& relinks,
                         const std::string& masterArqId, const std::string& vaultLoc,
                         const std::string& camRel, const std::string& camAbs) {
    if (masterArqId.empty()) return false;
    auto it = relinks.find(masterArqId);
    if (it != relinks.end() && !it->second.empty()) return juce::File(it->second).existsAsFile();
    auto res = resolvedor.resolver(masterArqId, vaultLoc, camRel, camAbs);
    return res.has_value() && res->existsAsFile();
}
} // namespace

std::vector<ItemResumo> ProjetoAberto::listarItensDeProjeto(matriz::db::Database& registro,
                                                            matriz::db::Database& indice,
                                                            const juce::File& pastaProjeto,
                                                            const std::map<std::string, std::string>& inMemoryRelinks,
                                                            const std::set<std::string>* itensOffline) {
    std::vector<ItemResumo> out;
    std::unique_ptr<matriz::vault::ResolvedorEmLote> resolvedor;  // só se algum item precisar checar disco

    // Uma consulta com JOIN em vez de N+1 (arquivo e vault resolvidos num único join
    // para a master, características técnicas e miniatura/tags preparadas uma vez).
    auto stmt = registro.prepare(
        "SELECT i.id, i.codigo_acervo, i.titulo, i.tipo_midia, i.estado, i.atualizado_em, "
        "EXISTS(SELECT 1 FROM consolidacao_registro cr WHERE cr.item_id = i.id), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'artista_principal'), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'titulo'), "
        "a.caminho_relativo, "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'origem'), "
        "i.ano, "
        "a.caminho_absoluto_origem, "
        "(SELECT ap.nome FROM acervo_item_pasta aip JOIN acervo_pasta ap ON ap.id = aip.pasta_id WHERE aip.item_id = i.id LIMIT 1), "
        "a.tamanho_bytes, "
        "i.content_type, i.collection_type, i.criado_em, "
        "a.id, "
        "COALESCE(v.localizacao, ''), "
        "i.isrc, "
        "0, "
        "COALESCE(i.metadados_editados, 0) != 0, "
        "COALESCE((WITH RECURSIVE cadeia(pasta_id, pasta_pai_id, ativo, prof) AS ("
        "  SELECT ap0.id, ap0.pasta_pai_id, ap0.ativo, 0 FROM acervo_pasta ap0 "
        "  WHERE ap0.id = (SELECT aip0.pasta_id FROM acervo_item_pasta aip0 WHERE aip0.item_id = i.id LIMIT 1) "
        "  UNION ALL "
        "  SELECT ap1.id, ap1.pasta_pai_id, ap1.ativo, c.prof + 1 FROM acervo_pasta ap1 "
        "  JOIN cadeia c ON ap1.id = c.pasta_pai_id WHERE c.prof < 64"
        ") SELECT MIN(ativo) FROM cadeia), 1), "
        "COALESCE(i.marcado_revisado, 0) != 0, "
        "CASE WHEN json_valid(a.caracteristicas_tecnicas_json) THEN CAST(json_extract(a.caracteristicas_tecnicas_json, '$.duracaoSegundos') AS REAL) ELSE NULL END, "
        "CASE WHEN json_valid(a.caracteristicas_tecnicas_json) THEN json_extract(a.caracteristicas_tecnicas_json, '$.exifDataOriginal') ELSE NULL END, "
        "i.dc_subject, "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'dc_created'), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'data_criacao') "
        "FROM item i "
        "LEFT JOIN arquivo a ON a.item_id = i.id AND a.id = ("
        "  SELECT a2.id FROM arquivo a2 WHERE a2.item_id = i.id ORDER BY a2.eh_master DESC, a2.id LIMIT 1"
        ") "
        "LEFT JOIN vault v ON v.id = a.vault_id "
        "WHERE COALESCE(i.em_quarentena, 0) = 0 ORDER BY i.codigo_acervo");

    const auto nests = mapaDeNests(registro);  // uma consulta: item -> nest
    auto minStmt = indice.prepare(
        "SELECT caminho_relativo FROM miniatura WHERE item_id = ? AND tipo = 'miniatura' ORDER BY gerado_em DESC LIMIT 1");
    auto tagStmt = registro.prepare(
        "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id IN ('tags', 'genero', 'estilo', 'palavras_chave') AND valor IS NOT NULL AND valor != ''");

    while (stmt.step()) {
        ItemResumo r;
        r.id = stmt.columnText(0);
        r.codigoAcervo = stmt.columnText(1);
        r.titulo = stmt.columnText(2);
        r.tipoMidia = stmt.columnText(3);
        r.estado = stmt.columnText(4);
        r.atualizadoEm = stmt.columnText(5);
        r.sincronizado = stmt.columnInt(6) != 0;
        if (!stmt.columnIsNull(7)) r.artistaLancamento = stmt.columnText(7);
        if (!stmt.columnIsNull(8)) r.tituloLancamento = stmt.columnText(8);
        if (!stmt.columnIsNull(9)) {
            juce::String caminho9 = stmt.columnText(9);
            int dotPos = caminho9.lastIndexOfChar('.');
            if (dotPos >= 0)
                r.extensaoArquivo = caminho9.substring(dotPos + 1).toLowerCase().toStdString();
        }
        if (!stmt.columnIsNull(10)) r.origem = stmt.columnText(10);
        if (!stmt.columnIsNull(11)) {
            // EVENT DATE (item.ano, o mesmo que a ficha mostra) em qualquer formato ("2019",
            // "2019-05-04", "04/05/2019"): vale o ano dele. Vazio ou "0" = sem ano (Unknown).
            r.ano = extrairAnoDeData(juce::String::fromUTF8(stmt.columnText(11).c_str()));
        }
        if (!stmt.columnIsNull(15)) r.contentType = stmt.columnText(15);
        if (!stmt.columnIsNull(16)) r.collectionType = stmt.columnText(16);
        r.criadoEm = stmt.columnText(17);
        if (auto nit = nests.find(r.id); nit != nests.end()) {
            r.nestId = nit->second.nestId;
            r.nestCapaId = nit->second.capaId;
            r.nestTotal = nit->second.total;
            r.nestCapa = (r.id == nit->second.capaId);
        }

        std::string masterArqId = stmt.columnIsNull(18) ? "" : stmt.columnText(18);
        std::string vaultLoc = stmt.columnIsNull(19) ? "" : stmt.columnText(19);
        std::string camRel = stmt.columnIsNull(9) ? "" : stmt.columnText(9);
        std::string camAbs = stmt.columnIsNull(12) ? "" : stmt.columnText(12);

        r.masterArquivoId = masterArqId;
        r.caminhoRelativoArquivo = camRel;
        r.caminhoAbsolutoOrigem = camAbs;
        if (!stmt.columnIsNull(20)) r.isrc = stmt.columnText(20);
        if (!stmt.columnIsNull(21)) r.marcadoPublicacao = stmt.columnInt(21) != 0;
        if (!stmt.columnIsNull(22)) r.metadadosEditados = stmt.columnInt(22) != 0;
        r.pastaAtiva = stmt.columnInt(23) != 0;
        r.marcadoRevisado = stmt.columnInt(24) != 0;
        if (!stmt.columnIsNull(27)) {
            std::string subj = stmt.columnText(27);
            if (!subj.empty()) r.subject = subj;
        }
        // Data do metadado (mesma regra do Intake, ver listarItensEmQuarentena):
        // sem isto a coluna DATE CREATED do Grid caía em criado_em (data do ingest).
        if (!stmt.columnIsNull(28)) r.dataCriacao = stmt.columnText(28);
        else if (!stmt.columnIsNull(29)) r.dataCriacao = stmt.columnText(29);

        // Status offline: o cache (preenchido em background na abertura) evita
        // stat por item; só os que estão no cache são re-verificados em disco
        // (relink pode tê-los trazido de volta). Sem cache: verifica todos.
        if (itensOffline == nullptr || itensOffline->count(r.id) > 0)
        {
            if (!resolvedor) resolvedor = std::make_unique<matriz::vault::ResolvedorEmLote>(registro, pastaProjeto);
            r.offline = !arquivoMasterExiste(*resolvedor, inMemoryRelinks, masterArqId, vaultLoc, camRel, camAbs);
        }

        // Características técnicas via json_extract em SQL (sem juce::JSON::parse em C++)
        if (!stmt.columnIsNull(25)) {
            r.duracaoSegundos = stmt.columnReal(25);
        }
        // Sem EVENT DATE = Unknown: nem EXIF, nem dc_created, nem a data do arquivo no disco entram
        // aqui (o ingest já copia o que achou para item.ano; o que sobra vazio é Unknown de verdade).

        // Check thumbnail from indice database (statement preparado uma vez)
        try {
            minStmt.reset();
            minStmt.bind(1, matriz::db::Value::of(r.id));
            if (minStmt.step() && !minStmt.columnIsNull(0)) {
                r.miniaturaCaminhoRelativo = minStmt.columnText(0);
            }
        } catch (...) {}

        // Extract tags / genres from item_campo (statement preparado uma vez)
        try {
            tagStmt.reset();
            tagStmt.bind(1, matriz::db::Value::of(r.id));
            while (tagStmt.step()) {
                juce::String tagVal = tagStmt.columnText(0);
                juce::StringArray parts;
                parts.addTokens(tagVal, ",;", "\"");
                for (auto& p : parts) {
                    juce::String trimmed = p.trim();
                    if (trimmed.isNotEmpty()) r.tags.push_back(trimmed.toStdString());
                }
            }
        } catch (...) {}

        if (!r.titulo.empty()) {
            r.nomeOriginalArquivo = r.titulo;
        } else if (!stmt.columnIsNull(9)) {
            juce::String caminho9 = stmt.columnText(9);
            int slashPos = std::max(caminho9.lastIndexOfChar('/'), caminho9.lastIndexOfChar('\\'));
            r.nomeOriginalArquivo = (slashPos >= 0 ? caminho9.substring(slashPos + 1) : caminho9).toStdString();
        } else if (!stmt.columnIsNull(12)) {
            juce::String caminho12 = stmt.columnText(12);
            int slashPos = std::max(caminho12.lastIndexOfChar('/'), caminho12.lastIndexOfChar('\\'));
            r.nomeOriginalArquivo = (slashPos >= 0 ? caminho12.substring(slashPos + 1) : caminho12).toStdString();
        }

        if (!stmt.columnIsNull(13)) r.pastaNome = stmt.columnText(13);
        r.tamanhoBytes = stmt.columnIsNull(14) ? 0 : static_cast<juce::int64>(stmt.columnInt(14));

        out.push_back(std::move(r));
    }
    return out;
}

int ProjetoAberto::contarItens() const {
    if (!projeto_) return 0;
    // Mesmo filtro de listarItensDeProjeto() (fora da quarentena).
    auto st = leitura().prepare("SELECT COUNT(*) FROM item WHERE COALESCE(em_quarentena, 0) = 0");
    return st.step() ? static_cast<int>(st.columnInt(0)) : 0;
}

std::vector<ItemResumo> ProjetoAberto::listarItens() const {
    if (!projeto_) return {};
    // Cópia das marcações/relinks em memória sob marcacoesMutex_ (comentário
    // no membro, ProjetoAberto.h) -- este método roda em background
    // (MosaicoComponent::recarregar() via poolSnapshot_/thread
    // "MatrizSnapshot") enquanto a message thread pode estar escrevendo
    // nesses mesmos sets/map ao mesmo tempo (H/K/P/W, relink).
    std::map<std::string, std::string> relinkCopia;
    std::set<std::string> htmlCopia, zipCopia, printCopia, watermarkCopia, offlineCopia;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        relinkCopia = inMemoryRelinkedPaths_;
        htmlCopia = marcadosHtml_;
        zipCopia = marcadosZip_;
        printCopia = marcadosPrint_;
        watermarkCopia = marcadosWatermark_;
        offlineCopia = itensOfflineCache_;
    }
    auto items = listarItensDeProjeto(leitura(), leituraIndice(), projeto_->pasta(), relinkCopia, &offlineCopia);
    for (auto& item : items) {
        item.marcadoPublicacao = htmlCopia.count(item.id) > 0;
        item.marcadoZip = zipCopia.count(item.id) > 0;
        item.marcadoPrint = printCopia.count(item.id) > 0;
        item.marcadoWatermark = watermarkCopia.count(item.id) > 0;
    }
    return items;
}

ProjetoAberto::ResultadoSanitizacao ProjetoAberto::sanitizarDuplicata(matriz::db::Database& registro,
                                                                      const std::string& manterId,
                                                                      const std::string& descartarId,
                                                                      const std::set<std::string>& usarDescartado) {
    using matriz::db::Value;
    ResultadoSanitizacao r;
    if (manterId.empty() || descartarId.empty() || manterId == descartarId) return r;
    r.idMantido = manterId;
    const std::string agora = matriz::model::agoraIso8601();

    // Colunas de metadado do próprio `item` que entram na soma.
    static const char* const kColunas[] = {
        "tipo_midia", "ano", "content_type", "source_media", "collection_type", "isrc",
        "dc_title", "dc_creator", "dc_subject", "dc_description", "dc_publisher", "dc_contributor",
        "dc_created", "dc_issued", "dc_type", "dc_format", "dc_identifier", "dc_source",
        "dc_language", "dc_relation", "dc_coverage", "dc_rights"};
    std::string cols = "codigo_acervo, titulo, COALESCE(notas_livres, '')";
    for (auto* c : kColunas) cols += std::string(", COALESCE(") + c + ", '')";
    auto lerItem = [&](const std::string& id, std::vector<std::string>& out) {
        auto st = registro.prepare("SELECT " + cols + " FROM item WHERE id = ?");
        st.bind(1, Value::of(id));
        if (!st.step()) return false;
        const int n = 3 + static_cast<int>(std::size(kColunas));
        for (int i = 0; i < n; ++i) out.push_back(st.columnText(i));
        return true;
    };
    std::vector<std::string> k, d;
    if (!lerItem(manterId, k) || !lerItem(descartarId, d)) return r;
    r.codigoMantido = k[0];
    r.codigoDescartado = d[0];

    // Onde o arquivo do descartado está (SOURCE) — fica gravado no mantido.
    std::string arquivoDescartado, localDescartado;
    {
        auto st = registro.prepare(std::string("SELECT a.id, ") + matriz::vault::colunasDeResolucao() +
                                   " FROM arquivo a " + matriz::vault::joinDeResolucao() +
                                   " WHERE a.item_id = ? ORDER BY a.eh_master DESC, a.id LIMIT 1");
        st.bind(1, Value::of(descartarId));
        if (st.step()) {
            arquivoDescartado = st.columnText(0);
            const std::string loc = st.columnText(1), rel = st.columnText(2), abs = st.columnText(3);
            localDescartado = !abs.empty() ? abs
                            : (!loc.empty() ? juce::File(juce::String(loc)).getChildFile(juce::String(rel))
                                                  .getFullPathName().toStdString()
                                            : rel);
        }
    }
    {
        auto st = registro.prepare("SELECT 1 FROM consolidacao_registro WHERE item_id = ? LIMIT 1");
        st.bind(1, Value::of(descartarId));
        r.descartadoJaNoMain = st.step();
    }

    // 1-4. Fase 4 (MergeFichas): listas somam (tags/pessoas, subjects,
    // marcadores, assuntos); valor único: vazio preenche, datas compatíveis
    // ficam na mais precisa, diferente de verdade vale o do mantido (ou o
    // escolhido na tela) e o perdedor vai pro item_historico.
    const auto juncao = matriz::model::merge::juntarFichas(
        registro, manterId, descartarId, usarDescartado, matriz::model::merge::origemDoDescartado(registro, descartarId));
    r.camposSomados = juncao.camposSomados;
    r.conflitos = static_cast<int>(juncao.conflitos.size());

    // 5. Notas — sempre append, nunca sobrescreve.
    auto acrescentar = [](const std::string& atual, const juce::String& bloco) {
        juce::String s = juce::String::fromUTF8(atual.c_str()).trimEnd();
        return (s.isEmpty() ? bloco : s + "\n\n" + bloco).toStdString();
    };
    juce::String blocoMantido = "[DUPLICATE MERGED] " + juce::String(agora) + " — duplicate " +
                                juce::String::fromUTF8(r.codigoDescartado.c_str()) + " (\"" +
                                juce::String::fromUTF8(d[1].c_str()) + "\") discarded; its file stays in SOURCE at " +
                                juce::String::fromUTF8(localDescartado.c_str()) + " and is not copied to backup.";
    if (r.conflitos > 0)
        blocoMantido << "\n" << juce::String(r.conflitos)
                     << " merge conflict(s): this item's values stayed; the duplicate's are in the item history "
                        "(filter \"Merge conflicts\").";
    if (!d[2].empty())
        blocoMantido << "\nNotes from the duplicate:\n" << juce::String::fromUTF8(d[2].c_str());
    registro.run("UPDATE item SET notas_livres = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                 {Value::of(acrescentar(k[2], blocoMantido)), Value::of(agora), Value::of(manterId)});

    juce::String blocoDescartado = "Validated as duplicate of " + juce::String::fromUTF8(r.codigoMantido.c_str()) +
                                   " — discarded side: file stays in SOURCE, excluded from backup; metadata merged "
                                   "into the kept item. [USER_VERIFIED_DUPLICATE]";
    registro.run("UPDATE item SET estado = 'duplicata', notas_livres = ?, atualizado_em = ? WHERE id = ?",
                 {Value::of(acrescentar(d[2], blocoDescartado)), Value::of(agora), Value::of(descartarId)});

    // 6. Log (PREMIS no banco; `linhasLog` pro log.md do projeto).
    const std::string detalhe = "Duplicate resolved: kept " + r.codigoMantido + ", discarded " + r.codigoDescartado +
                                " (file stays in SOURCE: " + localDescartado + ")";
    try {
    matriz::preservation::registrarEvento(registro, manterId, {}, matriz::preservation::EventType::Validation,
                                          detalhe, matriz::preservation::Outcome::Success,
                                          "Merged " + std::to_string(r.camposSomados) + " field(s)", "bkr-agent-sistema");
    matriz::preservation::registrarEvento(registro, descartarId, arquivoDescartado,
                                          matriz::preservation::EventType::Validation, detalhe,
                                          matriz::preservation::Outcome::Success,
                                          "Excluded from backup; nothing deleted", "bkr-agent-sistema");
    } catch (...) {}  // evento é registro auxiliar: não derruba a resolução

    r.linhasLog.add("Kept: " + juce::String::fromUTF8(r.codigoMantido.c_str()) + " (\"" +
                    juce::String::fromUTF8(k[1].c_str()) + "\")");
    r.linhasLog.add("Discarded: " + juce::String::fromUTF8(r.codigoDescartado.c_str()) + " (\"" +
                    juce::String::fromUTF8(d[1].c_str()) + "\") — stays in SOURCE, excluded from backup");
    r.linhasLog.add("Discarded file location: " + juce::String::fromUTF8(localDescartado.c_str()));
    r.linhasLog.add("Fields merged into kept item: " + juce::String(r.camposSomados) +
                    ", merge conflicts (kept in the item history): " + juce::String(r.conflitos));
    if (r.descartadoJaNoMain)
        r.linhasLog.add("The discarded item already had a copy in MAIN — left untouched (nothing is deleted).");
    return r;
}

ProjetoAberto::ResultadoIntakePacote ProjetoAberto::aplicarPacoteIngerido(
    const matriz::consolidacao::pacote::Pacote& pacote, const std::vector<std::string>& itensIngeridos) {
    namespace pk = matriz::consolidacao::pacote;
    namespace nomes = matriz::model::nomes;
    using matriz::db::Value;
    ResultadoIntakePacote r;
    if (!projeto_) return r;
    if (somenteLeitura_) { r.erro = "read-only project"; return r; }
    auto& db = projeto_->registro();
    const std::string agora = matriz::model::agoraIso8601();
    const std::string projetoId = projeto_->projetoId();
    const std::set<std::string> doPacote(itensIngeridos.begin(), itensIngeridos.end());
    r.entraram = static_cast<int>(itensIngeridos.size());

    std::multimap<std::string, size_t> porSha;
    for (size_t i = 0; i < pacote.arquivos.size(); ++i) porSha.emplace(pacote.arquivos[i].sha256, i);
    std::vector<bool> usado(pacote.arquivos.size(), false);
    const juce::String sep = juce::String::charToString(0x1f);  // nomes de pasta podem ter '/'

    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    try {
        auto vocabTags = nomes::Vocabulario::carregar(db, nomes::Vocabulario::Tipo::Tags);
        auto vocabSubjects = nomes::Vocabulario::carregar(db, nomes::Vocabulario::Tipo::Subjects);
        db.exec("BEGIN IMMEDIATE");

        // 1. Folder map novo com o nome do pacote.
        {
            std::set<juce::String> existentes;
            auto st = db.prepare("SELECT nome FROM folder_map WHERE projeto_id = ?");
            st.bind(1, Value::of(projetoId));
            while (st.step()) existentes.insert(juce::String::fromUTF8(st.columnText(0).c_str()).trim());
            const juce::String base = pacote.nome().trim().isEmpty() ? juce::String("Package") : pacote.nome().trim();
            r.folderMap = base;
            for (int n = 2; existentes.count(r.folderMap); ++n) r.folderMap = base + " (" + juce::String(n) + ")";
        }
        const std::string mapaId = matriz::model::novoUuid();
        {
            int ordem = 0;
            auto st = db.prepare("SELECT COALESCE(MAX(ordem), -1) + 1 FROM folder_map WHERE projeto_id = ?");
            st.bind(1, Value::of(projetoId));
            if (st.step()) ordem = static_cast<int>(st.columnInt(0));
            db.run("INSERT INTO folder_map (id, projeto_id, nome, ordem, criado_em, atualizado_em) VALUES (?, ?, ?, ?, ?, ?)",
                   {Value::of(mapaId), Value::of(projetoId), Value::of(r.folderMap.toStdString()), Value::of(ordem),
                    Value::of(agora), Value::of(agora)});
        }
        std::map<juce::String, std::string> pastaPorCaminho;
        std::function<void(const juce::var&, const std::string&, const juce::String&)> criarPastas =
            [&](const juce::var& lista, const std::string& paiId, const juce::String& prefixo) {
                auto* arr = lista.getArray();
                if (!arr) return;
                int ordem = 0;
                for (const auto& no : *arr) {
                    const juce::String nome = no.getProperty("nome", {}).toString().trim();
                    if (nome.isEmpty()) continue;
                    const juce::String chave = prefixo.isEmpty() ? nome : prefixo + sep + nome;
                    std::string id;
                    if (auto it = pastaPorCaminho.find(chave); it != pastaPorCaminho.end()) {
                        id = it->second;  // irmãs com o mesmo nome viram uma pasta só
                    } else {
                        id = matriz::model::novoUuid();
                        db.run("INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mapa_id, posicao_x, "
                               "posicao_y, ativo, criado_em, atualizado_em) VALUES (?, ?, ?, ?, ?, ?, 0, 0, 1, ?, ?)",
                               {Value::of(id), Value::of(projetoId), paiId.empty() ? Value::null() : Value::of(paiId),
                                Value::of(nome.toStdString()), Value::of(ordem++), Value::of(mapaId), Value::of(agora),
                                Value::of(agora)});
                        pastaPorCaminho[chave] = id;
                    }
                    criarPastas(no.getProperty("pastas", {}), id, chave);
                }
            };
        criarPastas(pacote.pastas, {}, {});

        // 2. Cada item novo: casa pelo SHA-256, grava a ficha, põe na pasta.
        const std::string autor = "Package: " + pacote.nome().toStdString();
        for (const auto& itemId : itensIngeridos) {
            std::string sha, abs;
            {
                auto st = db.prepare("SELECT COALESCE(checksum_sha256, ''), COALESCE(caminho_absoluto_origem, '') "
                                     "FROM arquivo WHERE item_id = ? ORDER BY eh_master DESC LIMIT 1");
                st.bind(1, Value::of(itemId));
                if (st.step()) {
                    sha = juce::String(st.columnText(0)).toLowerCase().toStdString();
                    abs = st.columnText(1);
                }
            }
            // De onde veio (Fase 4: origem do valor perdedor num merge de duplicata).
            db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                   "VALUES (?, ?, 'raiz', 0, 'pacote_origem', ?, 'leitura_tecnica', ?) "
                   "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor",
                   {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(pacote.nome().toStdString()),
                    Value::of(agora)});
            const juce::File arq(juce::String::fromUTF8(abs.c_str()));
            const std::string rel = arq.getRelativePathFrom(pacote.media()).replaceCharacter('\\', '/').toStdString();
            if (!sha.empty()) {
                auto st = db.prepare("SELECT item_id FROM arquivo WHERE checksum_sha256 = ? AND item_id <> ?");
                st.bind(1, Value::of(sha));
                st.bind(2, Value::of(itemId));
                bool existia = false;
                while (!existia && st.step()) existia = doPacote.count(st.columnText(0)) == 0;
                if (existia) ++r.jaExistiam;
            }
            std::optional<size_t> escolhido;
            auto faixa = porSha.equal_range(sha);
            for (auto it = faixa.first; it != faixa.second; ++it) {
                if (usado[it->second]) continue;
                if (!escolhido || pacote.arquivos[it->second].caminho == rel) escolhido = it->second;
                if (pacote.arquivos[it->second].caminho == rel) break;
            }
            if (sha.empty() || !escolhido) {
                ++r.semCorrespondencia;
                r.semCorrespondenciaNomes.add(arq.getFileName());
                continue;
            }
            usado[*escolhido] = true;
            const auto& reg = pacote.arquivos[*escolhido];
            if (!reg.dados.vazio()) {
                pk::gravarDadosFicha(db, itemId, reg.dados, vocabTags, vocabSubjects, autor);
                ++r.comDados;
            }
            if (!reg.pasta.empty()) {
                juce::StringArray partes;
                for (const auto& n : reg.pasta) partes.add(n);
                if (auto it = pastaPorCaminho.find(partes.joinIntoString(sep)); it != pastaPorCaminho.end())
                    inserirItemPastaInterno(itemId, it->second, agora);
            }
        }
        for (size_t i = 0; i < pacote.arquivos.size(); ++i)
            if (!usado[i]) {
                ++r.registrosSemArquivo;
                r.registrosSemArquivoCaminhos.add(juce::String::fromUTF8(pacote.arquivos[i].caminho.c_str()));
            }
        db.exec("COMMIT");
    } catch (const std::exception& e) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        r.erro = e.what();
        return r;
    }
    r.ok = true;
    try {
        juce::StringArray linhas;
        linhas.add("Package: " + pacote.pasta.getFullPathName() + " (from \"" + pacote.colecao + "\", folder map \"" +
                   pacote.folderMap + "\", exported " + pacote.exportadoEm + ")");
        linhas.add("Files in: " + juce::String(r.entraram) + ", with catalog data: " + juce::String(r.comDados) +
                   ", without a match: " + juce::String(r.semCorrespondencia) + ", already in this collection: " +
                   juce::String(r.jaExistiam) + ", catalog records without a file: " + juce::String(r.registrosSemArquivo));
        linhas.add("Folder map created: " + r.folderMap);
        for (int i = 0; i < r.semCorrespondenciaNomes.size() && i < 50; ++i)
            linhas.add("No catalog match: " + r.semCorrespondenciaNomes[i]);
        for (int i = 0; i < r.registrosSemArquivoCaminhos.size() && i < 50; ++i)
            linhas.add("Catalog record without file: " + r.registrosSemArquivoCaminhos[i]);
        matriz::model::ProjectLog(projeto_->pasta()).appendEntry("Collection Package Ingested", linhas);
    } catch (...) {}
    return r;
}

void ProjetoAberto::resolverDuplicatasEmSegundoPlano(std::vector<std::string> itensAfetados, CorpoResolucao corpo,
                                                     std::function<void(bool, std::vector<ResultadoSanitizacao>)> aoConcluir,
                                                     const std::string& descricaoUndo) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || !corpo) return;
    std::weak_ptr<bool> vivo = vivo_;
    poolMerge_.addJob([this, vivo, itensAfetados, corpo, aoConcluir, descricaoUndo] {
        bool ok = false;
        std::vector<ResultadoSanitizacao> resultados;
        std::shared_ptr<matriz::model::merge::Retrato> retrato;
        {
            std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
            auto& db = projeto_->registro();
            try {
                db.exec("BEGIN IMMEDIATE");
                retrato = matriz::model::merge::retratar(db, itensAfetados);
                resultados = corpo(db);
                db.exec("COMMIT");
                ok = true;
            } catch (const std::exception& e) {
                try { db.exec("ROLLBACK"); } catch (...) {}
                juce::Logger::writeToLog("[merge] resolucao desfeita: " + juce::String(e.what()));
            }
        }
        juce::MessageManager::callAsync([this, vivo, ok, resultados, retrato, aoConcluir, descricaoUndo] {
            if (!vivo.lock()) return;
            if (ok && retrato)
                registrarUndo(descricaoUndo, [this, retrato] { restaurarRetratoMerge(*retrato); });
            if (aoConcluir) aoConcluir(ok, resultados);
        });
    });
}

void ProjetoAberto::restaurarRetratoMerge(const matriz::model::merge::Retrato& retrato) {
    if (!projeto_) return;
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    auto& db = projeto_->registro();
    try {
        db.exec("BEGIN IMMEDIATE");
        matriz::model::merge::restaurar(db, retrato);
        db.exec("COMMIT");
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        return;
    }
    EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");
}

void ProjetoAberto::simularJuncaoEmSegundoPlano(const std::string& manterId, const std::string& descartarId,
                                                std::function<void(std::vector<matriz::model::merge::Conflito>)> aoConcluir) {
    if (!projeto_) return;
    std::weak_ptr<bool> vivo = vivo_;
    poolMerge_.addJob([this, vivo, manterId, descartarId, aoConcluir] {
        std::vector<matriz::model::merge::Conflito> conflitos;
        try {
            conflitos = matriz::model::merge::juntarFichas(projeto_->registro(), manterId, descartarId, {}, {}, /*simular*/ true)
                            .conflitos;
        } catch (...) {}
        juce::MessageManager::callAsync([vivo, conflitos, aoConcluir] {
            if (vivo.lock() && aoConcluir) aoConcluir(conflitos);
        });
    });
}

bool ProjetoAberto::temConflitoMergePendente(const std::string& itemId) const {
    if (!projeto_) return false;
    try {
        return !matriz::model::merge::conflitosPendentes(leitura(), itemId).empty();
    } catch (...) {
        return false;
    }
}

std::vector<matriz::model::merge::ConflitoPendente> ProjetoAberto::conflitosMergePendentes(const std::string& itemId) const {
    if (!projeto_) return {};
    try {
        return matriz::model::merge::conflitosPendentes(leitura(), itemId);
    } catch (...) {
        return {};
    }
}

void ProjetoAberto::revisarConflitosMerge(const std::string& itemId, std::set<std::string> trocarHistoricoIds) {
    resolverDuplicatasEmSegundoPlano(
        {itemId},
        [itemId, trocarHistoricoIds](matriz::db::Database& db) {
            matriz::model::merge::revisarConflitos(db, itemId, trocarHistoricoIds);
            return std::vector<ResultadoSanitizacao>{};
        },
        [itemId](bool ok, std::vector<ResultadoSanitizacao>) {
            if (ok) EventBus::obterInstancia().dispararItemAlterado(itemId, "metadado");
        },
        "Review Merge Conflicts");
}

std::vector<ItemResumo> ProjetoAberto::listarItensDaColecao(const juce::File& pastaColecao) const {
    juce::File resolvedDir = matriz::model::Project::resolverPastaProjeto(pastaColecao);
    juce::File regFile = resolvedDir.getChildFile("registro.sqlite");
    juce::File indFile = resolvedDir.getChildFile("indice.sqlite");
    if (!regFile.existsAsFile()) return {};

    try {
        matriz::db::Database regDb(regFile.getFullPathName().toStdString());
        std::vector<ItemResumo> items;
        // Outra coleção: o cache offline é do projeto aberto, não serve aqui.
        if (indFile.existsAsFile()) {
            matriz::db::Database indDb(indFile.getFullPathName().toStdString());
            items = listarItensDeProjeto(regDb, indDb, resolvedDir);
        } else {
            // Temporary in-memory dummy db if indice.sqlite is missing
            matriz::db::Database dummyInd(":memory:");
            items = listarItensDeProjeto(regDb, dummyInd, resolvedDir);
        }
        {
            std::lock_guard<std::mutex> lock(marcacoesMutex_);
            for (auto& item : items) {
                item.marcadoPublicacao = marcadosHtml_.count(item.id) > 0;
                item.marcadoZip = marcadosZip_.count(item.id) > 0;
                item.marcadoPrint = marcadosPrint_.count(item.id) > 0;
                item.marcadoWatermark = marcadosWatermark_.count(item.id) > 0;
            }
        }
        return items;
    } catch (...) {
        return {};
    }
}

std::vector<ItemResumo> ProjetoAberto::listarItensEmQuarentena() const {
    std::vector<ItemResumo> out;
    if (!projeto_) return out;
    std::unique_ptr<matriz::vault::ResolvedorEmLote> resolvedor;  // só se algum item precisar checar disco

    // Mesma cópia sob lock de listarItens() -- este método também roda em
    // background (IntakeWorkspaceComponent) enquanto a message thread pode
    // escrever nesses sets/map ao mesmo tempo.
    std::map<std::string, std::string> relinkCopia;
    std::set<std::string> htmlCopia, zipCopia, printCopia, watermarkCopia, offlineCopia;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        relinkCopia = inMemoryRelinkedPaths_;
        htmlCopia = marcadosHtml_;
        zipCopia = marcadosZip_;
        printCopia = marcadosPrint_;
        watermarkCopia = marcadosWatermark_;
        offlineCopia = itensOfflineCache_;
    }

    // Retry loop in case SQLite is momentarily busy during ingest transactions
    for (int tentativa = 0; tentativa < 3; ++tentativa) {
        try {
            out.clear();
            auto stmt = leitura().prepare(
                "SELECT i.id, i.codigo_acervo, i.titulo, i.tipo_midia, i.estado, i.atualizado_em, "
                "EXISTS(SELECT 1 FROM consolidacao_registro cr WHERE cr.item_id = i.id), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
                " AND c.campo_id = 'artista_principal'), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
                " AND c.campo_id = 'titulo'), "
                "(SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
                " AND c.campo_id = 'origem'), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
                " AND c.campo_id = 'ano'), "
                "(SELECT a.caminho_absoluto_origem FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "(SELECT ap.nome FROM acervo_item_pasta aip JOIN acervo_pasta ap ON ap.id = aip.pasta_id WHERE aip.item_id = i.id LIMIT 1), "
                "(SELECT a.tamanho_bytes FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "i.content_type, i.collection_type, i.criado_em, "
                "(SELECT a.id FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "(SELECT COALESCE(v.localizacao, '') FROM arquivo a LEFT JOIN vault v ON v.id = a.vault_id WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "0, "
                "COALESCE(i.metadados_editados, 0) != 0, "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'source_media'), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'dc_created'), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'data_criacao'), "
                "(SELECT a.caracteristicas_tecnicas_json FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1) "
                "FROM item i WHERE i.em_quarentena = 1 ORDER BY i.criado_em DESC, i.id");
            while (stmt.step()) {
                ItemResumo r;
                r.id = stmt.columnText(0);
                r.codigoAcervo = stmt.columnText(1);
                r.titulo = stmt.columnText(2);
                r.tipoMidia = stmt.columnText(3);
                r.estado = stmt.columnText(4);
                r.atualizadoEm = stmt.columnText(5);
                r.sincronizado = stmt.columnInt(6) != 0;
                if (!stmt.columnIsNull(7)) r.artistaLancamento = stmt.columnText(7);
                if (!stmt.columnIsNull(8)) r.tituloLancamento = stmt.columnText(8);
                if (!stmt.columnIsNull(9)) {
                    juce::String caminho9 = stmt.columnText(9);
                    int dotPos = caminho9.lastIndexOfChar('.');
                    if (dotPos >= 0)
                        r.extensaoArquivo = caminho9.substring(dotPos + 1).toLowerCase().toStdString();
                }
                if (!stmt.columnIsNull(10)) r.origem = stmt.columnText(10);
                if (!stmt.columnIsNull(11)) {
                    juce::String anoTexto = stmt.columnText(11);
                    if (anoTexto.containsOnly("0123456789") && anoTexto.isNotEmpty()) r.ano = anoTexto.getIntValue();
                }
                if (!stmt.columnIsNull(15)) r.contentType = stmt.columnText(15);
                if (!stmt.columnIsNull(16)) r.collectionType = stmt.columnText(16);
                r.criadoEm = stmt.columnText(17);
                r.marcadoPublicacao = htmlCopia.count(r.id) > 0;
                r.marcadoZip = zipCopia.count(r.id) > 0;
                r.marcadoPrint = printCopia.count(r.id) > 0;
                r.marcadoWatermark = watermarkCopia.count(r.id) > 0;
                if (!stmt.columnIsNull(21)) r.metadadosEditados = stmt.columnInt(21) != 0;

                std::string masterArqId = stmt.columnIsNull(18) ? "" : stmt.columnText(18);
                std::string vaultLoc = stmt.columnIsNull(19) ? "" : stmt.columnText(19);
                std::string camRel = stmt.columnIsNull(9) ? "" : stmt.columnText(9);
                std::string camAbs = stmt.columnIsNull(12) ? "" : stmt.columnText(12);

                r.masterArquivoId = masterArqId;
                r.caminhoRelativoArquivo = camRel;
                r.caminhoAbsolutoOrigem = camAbs;

                // Mesmo critério de listarItensDeProjeto: re-verifica só os do cache.
                if (offlineCopia.count(r.id) > 0)
                {
                    if (!resolvedor)
                        resolvedor = std::make_unique<matriz::vault::ResolvedorEmLote>(leitura(), projeto_->pasta());
                    r.offline = !arquivoMasterExiste(*resolvedor, relinkCopia, masterArqId, vaultLoc, camRel, camAbs);
                }

                if (!r.titulo.empty()) {
                    r.nomeOriginalArquivo = r.titulo;
                } else if (!stmt.columnIsNull(9)) {
                    juce::String caminho9 = stmt.columnText(9);
                    int slashPos = std::max(caminho9.lastIndexOfChar('/'), caminho9.lastIndexOfChar('\\'));
                    r.nomeOriginalArquivo = (slashPos >= 0 ? caminho9.substring(slashPos + 1) : caminho9).toStdString();
                } else if (!stmt.columnIsNull(12)) {
                    juce::String caminho12 = stmt.columnText(12);
                    int slashPos = std::max(caminho12.lastIndexOfChar('/'), caminho12.lastIndexOfChar('\\'));
                    r.nomeOriginalArquivo = (slashPos >= 0 ? caminho12.substring(slashPos + 1) : caminho12).toStdString();
                }

                if (!stmt.columnIsNull(13)) r.pastaNome = stmt.columnText(13);
                r.tamanhoBytes = stmt.columnIsNull(14) ? 0 : static_cast<juce::int64>(stmt.columnInt(14));

                if (!stmt.columnIsNull(22)) r.sourceMedia = stmt.columnText(22);
                if (!stmt.columnIsNull(23)) r.dataCriacao = stmt.columnText(23);
                else if (!stmt.columnIsNull(24)) r.dataCriacao = stmt.columnText(24);

                if (!stmt.columnIsNull(25)) {
                    auto jsonStr = stmt.columnText(25);
                    auto varObj = juce::JSON::parse(jsonStr);
                    if (varObj.isObject()) {
                        if (varObj.hasProperty("duracaoSegundos")) {
                            r.duracaoSegundos = static_cast<double>(varObj["duracaoSegundos"]);
                        }
                        if (!r.ano.has_value() && varObj.hasProperty("exifDataOriginal")) {
                            juce::String exifDt = varObj["exifDataOriginal"].toString();
                            for (int i = 0; i + 3 < exifDt.length(); ++i) {
                                if (std::isdigit(exifDt[i]) && std::isdigit(exifDt[i+1]) &&
                                    std::isdigit(exifDt[i+2]) && std::isdigit(exifDt[i+3])) {
                                    int yVal = exifDt.substring(i, i + 4).getIntValue();
                                    if (yVal > 1800 && yVal <= juce::Time::getCurrentTime().getYear() + 1) { r.ano = yVal; break; }
                                }
                            }
                        }
                    }
                }

                out.push_back(std::move(r));
            }
            break; // Success!
        } catch (...) {
            if (tentativa < 2) {
                juce::Thread::sleep(20);
            }
        }
    }
    return out;
}

void ProjetoAberto::registrarUndoEnvioAoGrid(const std::vector<std::string>& itemIds) {
    if (desfazendo_ || !projeto_) return;
    std::vector<std::pair<std::string, std::string>> antes;  // id -> lote_grid_id anterior
    for (const auto& id : itemIds) {
        auto st = projeto_->registro().prepare("SELECT COALESCE(lote_grid_id, '') FROM item WHERE id = ? AND em_quarentena = 1");
        st.bind(1, matriz::db::Value::of(id));
        if (st.step()) antes.push_back({id, st.columnText(0)});
    }
    if (antes.empty()) return;
    registrarUndo(antes.size() == 1 ? "Send to Grid" : "Send " + std::to_string(antes.size()) + " Items to Grid",
                  [this, antes]() {
        auto& d = projeto_->registro();
        d.run("BEGIN TRANSACTION", {});
        try {
            for (const auto& [id, lote] : antes)
                d.run("UPDATE item SET em_quarentena = 1, lote_grid_id = ? WHERE id = ?",
                      {lote.empty() ? matriz::db::Value::null() : matriz::db::Value::of(lote), matriz::db::Value::of(id)});
            d.run("COMMIT", {});
        } catch (...) {
            try { d.run("ROLLBACK", {}); } catch (...) {}
            return;
        }
        ultimosItensIngeridosValido_ = false;
        for (const auto& [id, lote] : antes) EventBus::obterInstancia().dispararItemAlterado(id, "quarentena");
    });
}

std::vector<std::string> ProjetoAberto::semMarcadosR(const std::vector<std::string>& itemIds) const {
    const auto marcados = idsMarcadosR();
    if (marcados.empty()) return itemIds;
    std::vector<std::string> out;
    out.reserve(itemIds.size());
    for (const auto& id : itemIds)
        if (marcados.count(id) == 0) out.push_back(id);
    return out;
}

void ProjetoAberto::confirmarItemGrid(const std::string& itemId) {
    if (!projeto_ || itemId.empty()) return;
    if (idsMarcadosR().count(itemId) > 0) return;  // marcado com R: não entra no grid de jeito nenhum
    registrarUndoEnvioAoGrid({itemId});
    std::string agora = matriz::model::agoraIso8601();
    projeto_->registro().run(
        "UPDATE item SET em_quarentena = 0, atualizado_em = ? WHERE id = ?",
        {matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
    EventBus::obterInstancia().dispararItemAlterado(itemId, "quarentena");
}

void ProjetoAberto::confirmarLoteGrid(const std::vector<std::string>& todosOsIds) {
    if (!projeto_ || todosOsIds.empty()) return;
    const auto itemIds = semMarcadosR(todosOsIds);  // marcado com R: não entra no grid de jeito nenhum
    if (itemIds.empty()) return;
    registrarUndoEnvioAoGrid(itemIds);
    std::string agora = matriz::model::agoraIso8601();
    // Id da leva: ms desde a época (zero-padded, ordena como texto) + uuid
    // pra desempatar duas promoções no mesmo ms.
    std::string loteGridId = juce::String(juce::Time::currentTimeMillis()).paddedLeft('0', 16).toStdString() +
                             "-" + matriz::model::novoUuid();
    projeto_->registro().run("BEGIN TRANSACTION", {});
    try {
        for (const auto& id : itemIds) {
            projeto_->registro().run(
                "UPDATE item SET em_quarentena = 0, atualizado_em = ?, lote_grid_id = ? WHERE id = ?",
                {matriz::db::Value::of(agora), matriz::db::Value::of(loteGridId), matriz::db::Value::of(id)});
        }
        projeto_->registro().run("COMMIT", {});
    } catch (...) {
        projeto_->registro().run("ROLLBACK", {});
        throw;
    }
    // Item "RECENTLY INGESTED": o gatilho é a promoção INTAKE -> GRID (este
    // método), não o ingest bruto em si — um conjunto só vira "recém
    // ingerido" quando o operador aprova e manda pra grade; o próximo lote
    // aprovado substitui este — relido do banco (lote_grid_id mais recente).
    // Antes dos eventos: quem os recebe já enxerga a leva nova.
    ultimosItensIngeridosValido_ = false;
    for (const auto& id : itemIds) {
        EventBus::obterInstancia().dispararItemAlterado(id, "quarentena");
    }
}

std::vector<ProjetoAberto::ItemDetalhe> ProjetoAberto::obterDetalhesItens(const std::set<std::string>& itemIds) const {
    std::vector<ItemDetalhe> out;
    if (!projeto_ || itemIds.empty()) return out;
    out.reserve(itemIds.size());

    auto stmt = leitura().prepare(
        "SELECT i.id, i.titulo, "
        "(SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
        "(SELECT a.tamanho_bytes FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1) "
        "FROM item i WHERE i.id = ?");

    for (const auto& id : itemIds) {
        stmt.reset();
        stmt.bind(1, matriz::db::Value::of(id));
        if (!stmt.step()) continue;

        ItemDetalhe d;
        d.id = stmt.columnText(0);
        auto titulo = stmt.columnText(1);
        auto caminho = stmt.columnIsNull(2) ? std::string{} : stmt.columnText(2);

        if (!titulo.empty()) {
            d.nome = titulo;
        } else if (!caminho.empty()) {
            juce::String c(caminho);
            int slashPos = std::max(c.lastIndexOfChar('/'), c.lastIndexOfChar('\\'));
            d.nome = (slashPos >= 0 ? c.substring(slashPos + 1) : c).toStdString();
        }

        if (!caminho.empty()) {
            juce::String c(caminho);
            int dotPos = c.lastIndexOfChar('.');
            if (dotPos >= 0)
                d.extensao = c.substring(dotPos + 1).toLowerCase().toStdString();
        }

        d.tamanhoBytes = stmt.columnIsNull(3) ? 0 : static_cast<juce::int64>(stmt.columnInt(3));
        out.push_back(std::move(d));
    }
    return out;
}

const matriz::ficha::FichaDefinition& ProjetoAberto::definicaoPara(const std::string& tipoMidia) {
    auto it = definicoesCache_.find(tipoMidia);
    if (it != definicoesCache_.end()) return it->second;

    juce::File caminho = juce::File(MATRIZ_FICHAS_DIR).getChildFile(tipoMidia + ".yaml");
    if (!caminho.existsAsFile())
        throw ProjetoAbertoError("nenhuma definição de ficha encontrada para o tipo \"" + tipoMidia + "\": " +
                                  caminho.getFullPathName().toStdString());

    auto [inserido, ok] = definicoesCache_.emplace(tipoMidia, matriz::ficha::loadFromFile(caminho.getFullPathName().toStdString()));
    return inserido->second;
}

std::optional<std::string> ProjetoAberto::valorCampo(const std::string& itemId, const std::string& nivel,
                                                       int nivelIndice, const std::string& campoId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = leitura().prepare(
        "SELECT valor FROM item_campo WHERE item_id = ? AND nivel = ? AND nivel_indice = ? AND campo_id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    stmt.bind(2, matriz::db::Value::of(nivel));
    stmt.bind(3, matriz::db::Value::of(nivelIndice));
    stmt.bind(4, matriz::db::Value::of(campoId));
    if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
    return std::nullopt;
}

std::vector<std::string> ProjetoAberto::papeisArquivoPresentes(const std::string& itemId) const {
    std::vector<std::string> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare("SELECT DISTINCT papel FROM arquivo WHERE item_id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

int ProjetoAberto::definirCapa(const std::vector<std::string>& itemIds, const juce::File& imagem) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return 0; }
    if (!projeto_ || !imagem.existsAsFile()) return 0;

    int aplicadas = 0;
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (auto& itemId : itemIds) {
            // Uma capa por item: trocar substitui a anterior em vez de
            // empilhar capas que ninguém mais consegue distinguir. Não chama
            // removerCapa() aqui — abriria uma segunda transação dentro
            // desta (SQLite não suporta BEGIN aninhado); mesma lógica dela,
            // sem BEGIN/COMMIT próprio (item 6, fix de performance).
            {
                auto stmt = db.prepare("SELECT id FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'");
                stmt.bind(1, matriz::db::Value::of(itemId));
                std::vector<std::string> capasAntigas;
                while (stmt.step()) capasAntigas.push_back(stmt.columnText(0));
                for (auto& arquivoIdAntigo : capasAntigas)
                    projeto_->indice().run("DELETE FROM miniatura WHERE arquivo_id = ?",
                                            {matriz::db::Value::of(arquivoIdAntigo)});
                db.run("DELETE FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'",
                       {matriz::db::Value::of(itemId)});
            }

            std::string arquivoId = matriz::model::novoUuid();
            juce::File destino = projeto_->pasta().getChildFile("arquivos").getChildFile(arquivoId).getChildFile(
                imagem.getFileName());
            destino.getParentDirectory().createDirectory();
            if (!imagem.copyFileTo(destino)) continue; // falha num item não derruba os outros

            std::string agora = matriz::model::agoraIso8601();
            db.run(
                "INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, "
                "criado_em, atualizado_em) VALUES (?, ?, ?, ?, 'capa_frente', 0, ?, ?, ?)",
                {matriz::db::Value::of(arquivoId), matriz::db::Value::of(itemId),
                 matriz::db::Value::of(destino.getRelativePathFrom(projeto_->pasta()).toStdString()),
                 matriz::db::Value::of(imagem.getFullPathName().toStdString()),
                 matriz::db::Value::of(static_cast<long long>(destino.getSize())),
                 matriz::db::Value::of(agora),
                 matriz::db::Value::of(agora)});

            // Grava a miniatura por cima: caminhoMiniaturaPrincipal() resolve por
            // gerado_em DESC, então a linha nova passa a valer sem precisar
            // apagar a gerada — e apagar a capa depois faz a antiga voltar sozinha.
            matriz::ingest::gerarEGravarMiniaturaPrincipal(projeto_->indice(), projeto_->pasta(), itemId, arquivoId,
                                                            destino, matriz::ingest::CategoriaMidia::Imagem,
                                                            std::nullopt);
            ++aplicadas;
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
    return aplicadas;
}

void ProjetoAberto::removerCapa(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (auto& itemId : itemIds) {
            // Primeiro as miniaturas geradas A PARTIR da capa (no índice, que é
            // outro banco — não há CASCADE entre os dois).
            auto stmt = db.prepare(
                "SELECT id FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'");
            stmt.bind(1, matriz::db::Value::of(itemId));
            std::vector<std::string> capas;
            while (stmt.step()) capas.push_back(stmt.columnText(0));

            for (auto& arquivoId : capas)
                projeto_->indice().run("DELETE FROM miniatura WHERE arquivo_id = ?",
                                        {matriz::db::Value::of(arquivoId)});

            db.run("DELETE FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'",
                   {matriz::db::Value::of(itemId)});
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

bool ProjetoAberto::temCapa(const std::string& itemId) const {
    if (!projeto_) return false;
    auto stmt = leitura().prepare(
        "SELECT 1 FROM arquivo WHERE item_id = ? AND papel = 'capa_frente' LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    return stmt.step();
}

bool ProjetoAberto::algumTemCapa(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return false;
    constexpr size_t kBloco = 400;  // abaixo do teto de variáveis do SQLite
    for (size_t ini = 0; ini < itemIds.size(); ini += kBloco) {
        const size_t fim = std::min(itemIds.size(), ini + kBloco);
        std::string sql = "SELECT 1 FROM arquivo WHERE papel = 'capa_frente' AND item_id IN (?";
        for (size_t k = ini + 1; k < fim; ++k) sql += ",?";
        sql += ") LIMIT 1";
        auto stmt = leitura().prepare(sql);
        for (size_t k = ini; k < fim; ++k) stmt.bind(static_cast<int>(k - ini) + 1, matriz::db::Value::of(itemIds[k]));
        if (stmt.step()) return true;
    }
    return false;
}

std::vector<std::string> ProjetoAberto::valoresUsadosNoCampo(const std::string& campoId) const {
    std::vector<std::string> out;
    if (!projeto_) return out;
    // Restrito aos itens DESTE projeto: o registro é por projeto, mas a
    // junção deixa isso explícito e resiste a um banco que um dia guarde
    // mais de um. Valor vazio não é vocabulário — é campo não preenchido.
    auto stmt = leitura().prepare(
        "SELECT DISTINCT ic.valor FROM item_campo ic JOIN item i ON i.id = ic.item_id "
        "WHERE i.projeto_id = ? AND ic.campo_id = ? AND ic.valor IS NOT NULL AND TRIM(ic.valor) <> '' "
        "ORDER BY ic.valor COLLATE NOCASE");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(campoId));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

std::optional<std::string> ProjetoAberto::lerMetadado(const std::string& itemId, const std::string& coluna) const {
    if (!projeto_) return std::nullopt;
    static const std::set<std::string> kColunasPermitidas = {
        "ano", "caminho_catalogo", "content_type", "source_media", "collection_type", "isrc", "notas_livres", "titulo",
        "dc_title", "dc_creator", "dc_subject", "dc_description", "dc_publisher", "dc_contributor",
        "dc_created", "dc_issued", "dc_type", "dc_format", "dc_identifier", "dc_source",
        "dc_language", "dc_relation", "dc_coverage", "dc_rights",
        // "As 3 datas do sistema" — DATE CREATED é um campo nativo do BKR
        // (nunca Dublin Core): já existia como "data_criacao" em item_campo
        // (preenchido no ingest, ver IngestArquivo.cpp), só faltava estar
        // liberado aqui pra virar campo editável de verdade na ficha.
        "data_criacao",
        // DATE ISSUED (item 11): quando o asset foi ingerido no BKR Matriz —
        // já existe como item.criado_em (setado uma vez, na criação da
        // linha), só somente-leitura aqui, nunca gravado via salvarMetadado.
        "criado_em"
    };
    if (kColunasPermitidas.find(coluna) == kColunasPermitidas.end()) return std::nullopt;

    try {
        auto stmt = leitura().prepare("SELECT " + coluna + " FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step() && !stmt.columnIsNull(0)) {
            std::string val = stmt.columnText(0);
            if (!val.empty()) return val;
        }
    } catch (...) {}

    try {
        auto stmt = leitura().prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        stmt.bind(2, matriz::db::Value::of(coluna));
        if (stmt.step() && !stmt.columnIsNull(0)) {
            std::string val = stmt.columnText(0);
            if (!val.empty()) return val;
        }
    } catch (...) {}

    return std::nullopt;
}

void ProjetoAberto::registrarUndo(const std::string& descricao, std::function<void()> acaoReversa) {
    if (desfazendo_ || !acaoReversa) return;
    if (grupoAberto_) {
        grupoAberto_->acoesReversas.push_back(std::move(acaoReversa));
    } else {
        pilhaUndo_.push_back(UndoEntry{descricao, {std::move(acaoReversa)}});
        while (static_cast<int>(pilhaUndo_.size()) > kMaxUndo)
            pilhaUndo_.erase(pilhaUndo_.begin());
        if (aoMudarUndo) aoMudarUndo();
    }
}

void ProjetoAberto::iniciarGrupoUndo(const std::string& descricao) {
    if (desfazendo_) return;
    if (grupoAberto_) finalizarGrupoUndo();
    grupoAberto_ = UndoEntry{descricao, {}};
}

void ProjetoAberto::finalizarGrupoUndo() {
    if (!grupoAberto_ || grupoAberto_->acoesReversas.empty()) {
        grupoAberto_.reset();
        return;
    }
    pilhaUndo_.push_back(std::move(*grupoAberto_));
    grupoAberto_.reset();
    while (static_cast<int>(pilhaUndo_.size()) > kMaxUndo)
        pilhaUndo_.erase(pilhaUndo_.begin());
    if (aoMudarUndo) aoMudarUndo();
}

bool ProjetoAberto::desfazer() {
    if (pilhaUndo_.empty()) return false;
    auto entry = std::move(pilhaUndo_.back());
    pilhaUndo_.pop_back();
    desfazendo_ = true;
    for (auto it = entry.acoesReversas.rbegin(); it != entry.acoesReversas.rend(); ++it) {
        if (*it) (*it)();
    }
    desfazendo_ = false;
    if (aoMudarUndo) aoMudarUndo();
    return true;
}

std::string ProjetoAberto::descricaoUndoAtual() const {
    if (pilhaUndo_.empty()) return {};
    return pilhaUndo_.back().descricao;
}

void ProjetoAberto::salvarMetadado(const std::string& itemId, const std::string& coluna, const std::string& valorDigitado) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    // Nomes case-insensitive: "show" digitado vira o "Show" que o projeto já usa.
    const std::string valor = coluna == "dc_subject"
                                  ? matriz::model::nomes::subjectsCanonicos(projeto_->registro(), valorDigitado)
                                  : valorDigitado;

    if (!desfazendo_) {
        auto old = lerMetadado(itemId, coluna);
        std::string oldVal = old.value_or("");
        registrarUndo("Edit " + coluna, [this, itemId, coluna, oldVal]() {
            salvarMetadado(itemId, coluna, oldVal);
        });
    }

    // Try updating column on item table
    try {
        projeto_->registro().run(
            "UPDATE item SET " + coluna + " = ?, atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
            {matriz::db::Value::of(valor),
             matriz::db::Value::of(matriz::model::agoraIso8601()),
             matriz::db::Value::of(itemId)});
    } catch (...) {
        try {
            projeto_->registro().run(
                "UPDATE item SET atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                {matriz::db::Value::of(matriz::model::agoraIso8601()),
                 matriz::db::Value::of(itemId)});
        } catch (...) {}
    }

    // Sync to item_campo
    std::string agora = matriz::model::agoraIso8601();
    try {
        projeto_->registro().run(
            "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
            "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
            {matriz::db::Value::of(matriz::model::novoUuid()),
             matriz::db::Value::of(itemId),
             matriz::db::Value::of(coluna),
             matriz::db::Value::of(valor),
             matriz::db::Value::of(agora)});
    } catch (...) {}

    // A coluna "ano" (EVENT DATE na ficha) alimenta os filtros de data da aba
    // METADATA; ela ganha um tipo próprio de evento para que a lista possa
    // se reavaliar na hora, em vez de só na próxima vez que a aba recarrega.
    EventBus::obterInstancia().dispararItemAlterado(itemId, coluna == "ano" ? "metadado_data" : "metadado");
}

void ProjetoAberto::salvarMetadadoEmLote(const std::vector<std::string>& itemIds,
                                          const std::vector<std::pair<std::string, std::string>>& camposEValoresDigitados,
                                          const std::set<std::pair<std::string, std::string>>& pular) {
    MATRIZ_TRACE("ProjetoAberto::salvarMetadadoEmLote");
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty() || camposEValoresDigitados.empty()) return;
    // Nomes case-insensitive: SUBJECT entra na grafia que o projeto já usa.
    auto camposEValores = camposEValoresDigitados;
    for (auto& [coluna, valor] : camposEValores)
        if (coluna == "dc_subject") valor = matriz::model::nomes::subjectsCanonicos(projeto_->registro(), valor);

    // Uma entrada de Undo só pro lote inteiro (não uma por item x campo):
    // captura o valor antigo de cada combinação antes de escrever. Desfazer
    // restaura item por item via salvarMetadado() normal — ação rara e
    // pequena o bastante pra não precisar ser transacional.
    if (!desfazendo_) {
        struct ValorAntigo { std::string itemId, coluna, valor; };
        auto anteriores = std::make_shared<std::vector<ValorAntigo>>();
        anteriores->reserve(itemIds.size() * camposEValores.size());
        for (const auto& itemId : itemIds) {
            for (const auto& campoValor : camposEValores) {
                if (pular.count({itemId, campoValor.first})) continue;
                anteriores->push_back({itemId, campoValor.first,
                                        lerMetadado(itemId, campoValor.first).value_or("")});
            }
        }
        auto desfazerLote = [this, anteriores]() {
            for (const auto& v : *anteriores) {
                salvarMetadado(v.itemId, v.coluna, v.valor);
            }
        };
        std::string descricao = "Edit " + camposEValores.front().first + " (lote)";
        // Chamado de background pelo Intake: a pilha de Undo e aoMudarUndo
        // são da message thread — registra lá. vivo_ expira se o projeto
        // fechar antes do callAsync rodar.
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
            registrarUndo(descricao, std::move(desfazerLote));
        } else {
            std::weak_ptr<bool> vivo = vivo_;
            juce::MessageManager::callAsync([this, vivo, descricao, desfazerLote]() mutable {
                if (vivo.lock()) registrarUndo(descricao, std::move(desfazerLote));
            });
        }
    }

    auto& db = projeto_->registro();
    std::string agora = matriz::model::agoraIso8601();

    std::unique_lock<std::recursive_mutex> writeLock(projeto_->writeMutex());
    try {
        db.exec("BEGIN IMMEDIATE");
        for (const auto& itemId : itemIds) {
            for (const auto& campoValor : camposEValores) {
                const std::string& coluna = campoValor.first;
                const std::string& valor = campoValor.second;
                if (pular.count({itemId, coluna})) continue;
                try {
                    db.run(
                        "UPDATE item SET " + coluna + " = ?, atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                        {matriz::db::Value::of(valor), matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
                } catch (...) {
                    try {
                        db.run("UPDATE item SET atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                               {matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
                    } catch (...) {}
                }
                try {
                    db.run(
                        "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                        "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
                        "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
                        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                         matriz::db::Value::of(coluna), matriz::db::Value::of(valor), matriz::db::Value::of(agora)});
                } catch (...) {}
            }
        }
        db.exec("COMMIT");
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        return;
    }

    // EventBus/juce::ListenerList não é thread-safe pra iterar/chamar de uma
    // thread que não seja a message thread -- este método pode ser chamado
    // de background (ver IntakeWorkspaceComponent::aplicarXAosSelecionados),
    // então o disparo do evento amplo sempre volta pra message thread via
    // callAsync, nunca direto.
    bool ehData = camposEValores.front().first == "ano";
    juce::MessageManager::callAsync([ehData] {
        EventBus::obterInstancia().dispararItemAlterado("", ehData ? "metadado_data" : "metadado");
    });
}

bool ProjetoAberto::preencherAnoPadraoSeVazio(const std::string& itemId, const std::string& ano) {
    if (!projeto_ || itemId.empty() || ano.size() != 4) return false;
    for (char c : ano) if (c < '0' || c > '9') return false;

    try {
        // Mesmo UPDATE condicional que IngestArquivo usa: quem já tem ano
        // (inclusive um digitado pelo usuário) nunca é sobrescrito. Nada de
        // metadados_editados, atualizado_em, Undo ou EventBus aqui — abrir
        // uma ficha não pode marcar o item como editado nem empilhar Undo.
        auto& db = projeto_->registro();
        auto antes = db.prepare("SELECT COUNT(*) FROM item WHERE id = ? AND (ano IS NULL OR ano = '')");
        antes.bind(1, matriz::db::Value::of(itemId));
        bool estavaVazio = antes.step() && antes.columnInt(0) > 0;
        if (!estavaVazio) return false;

        db.run("UPDATE item SET ano = ? WHERE id = ? AND (ano IS NULL OR ano = '')",
               {matriz::db::Value::of(ano), matriz::db::Value::of(itemId)});
        return true;
    } catch (...) {
        return false;
    }
}

void ProjetoAberto::redefinirMetadadosItens(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    auto& db = projeto_->registro();
    std::string agora = matriz::model::agoraIso8601();

    for (const auto& itemId : itemIds) {
        if (itemId.empty()) continue;

        try {
            // 1. Clear non-native custom field entries (keep only reading/ingest technical fields)
            db.run("DELETE FROM item_campo WHERE item_id = ? AND fonte != 'leitura_tecnica'",
                   {matriz::db::Value::of(itemId)});

            // 2. Clear user tags
            db.run("DELETE FROM item_tag WHERE item_id = ?",
                   {matriz::db::Value::of(itemId)});

            // 3. Clear search entries for tags
            db.run("DELETE FROM busca_fts WHERE item_id = ? AND conteudo LIKE '#%'",
                   {matriz::db::Value::of(itemId)});

            // 4. Find if there are native readings for ano / date or source_media
            std::optional<std::string> nativeAno;
            std::optional<std::string> nativeSourceMedia;

            auto stmtAno = db.prepare(
                "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id IN ('dc_created', 'data_criacao', 'ano') AND fonte = 'leitura_tecnica' LIMIT 1");
            stmtAno.bind(1, matriz::db::Value::of(itemId));
            if (stmtAno.step() && !stmtAno.columnIsNull(0)) {
                juce::String dt(stmtAno.columnText(0));
                if (dt.length() >= 4) {
                    nativeAno = dt.substring(0, 4).toStdString();
                }
            }

            auto stmtSm = db.prepare(
                "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = 'source_media' AND fonte = 'leitura_tecnica' LIMIT 1");
            stmtSm.bind(1, matriz::db::Value::of(itemId));
            if (stmtSm.step() && !stmtSm.columnIsNull(0)) {
                nativeSourceMedia = stmtSm.columnText(0);
            }

            // 5. Reset item columns to original/null and mark metadados_editados = 0
            db.run("UPDATE item SET metadados_editados = 0, notas_livres = NULL, isrc = NULL, content_type = NULL, collection_type = NULL, ano = ?, source_media = ?, atualizado_em = ? WHERE id = ?",
                   {nativeAno ? matriz::db::Value::of(*nativeAno) : matriz::db::Value::null(),
                    nativeSourceMedia ? matriz::db::Value::of(*nativeSourceMedia) : matriz::db::Value::null(),
                    matriz::db::Value::of(agora),
                    matriz::db::Value::of(itemId)});

            EventBus::obterInstancia().dispararItemAlterado(itemId, "metadado");
        } catch (...) {}
    }
}

std::vector<std::string> ProjetoAberto::lerTags(const std::string& itemId) const {
    std::vector<std::string> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT tag FROM item_tag WHERE item_id = ? ORDER BY tag COLLATE NOCASE");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

void ProjetoAberto::definirTags(const std::string& itemId, const std::vector<std::string>& tagsDigitadas) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    if (!desfazendo_) {
        auto oldTags = lerTags(itemId);
        registrarUndo("Set Tags", [this, itemId, oldTags]() {
            definirTags(itemId, oldTags);
        });
    }
    auto& db = projeto_->registro();
    // Nomes case-insensitive: cada tag na grafia que o projeto já usa, sem
    // repetir ("Show" e "show" no mesmo item viram uma só).
    std::vector<std::string> tags;
    {
        std::set<std::string> chaves;
        for (const auto& t : tagsDigitadas) {
            std::string canon = matriz::model::nomes::tagCanonica(db, t);
            if (!canon.empty() && chaves.insert(matriz::model::nomes::chave(canon)).second) tags.push_back(canon);
        }
    }
    db.run("BEGIN TRANSACTION", {});
    try {
        db.run("DELETE FROM item_tag WHERE item_id = ?", {matriz::db::Value::of(itemId)});
        for (const auto& tag : tags) {
            if (tag.empty()) continue;
            db.run(
                "INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                {matriz::db::Value::of(matriz::model::novoUuid()),
                 matriz::db::Value::of(itemId),
                 matriz::db::Value::of(tag)});
        }
        // Index tags in FTS (both with and without '#') — erro aqui não
        // derruba a transação inteira, só a indexação de busca fica velha.
        try {
            db.run("DELETE FROM busca_fts WHERE item_id = ? AND (conteudo LIKE '#%' OR conteudo IN (SELECT tag FROM item_tag WHERE item_id = ?))",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(itemId)});
            for (const auto& tag : tags) {
                if (tag.empty()) continue;
                db.run(
                    "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
                    {matriz::db::Value::of(itemId), matriz::db::Value::of(tag)});
                db.run(
                    "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
                    {matriz::db::Value::of(itemId), matriz::db::Value::of("#" + tag)});
            }
        } catch (...) {}
        try {
            db.run(
                "UPDATE item SET metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                {matriz::db::Value::of(matriz::model::agoraIso8601()),
                 matriz::db::Value::of(itemId)});
        } catch (...) {}
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
    EventBus::obterInstancia().dispararItemAlterado(itemId, "tags");
}

void ProjetoAberto::adicionarTag(const std::string& itemId, const std::string& tag) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || tag.empty()) return;
    juce::String clean = juce::String(tag).trimCharactersAtStart("#").trim();
    if (clean.isEmpty()) return;
    // Nomes case-insensitive: grafia que o projeto já usa.
    std::string cleanStr = matriz::model::nomes::tagCanonica(projeto_->registro(), clean.toStdString());
    bool jaTinha = false;
    {
        auto st = projeto_->registro().prepare("SELECT 1 FROM item_tag WHERE item_id = ? AND tag = ?");
        st.bind(1, matriz::db::Value::of(itemId));
        st.bind(2, matriz::db::Value::of(cleanStr));
        jaTinha = st.step();
    }
    if (jaTinha) return;  // "show" num item que já tem "Show": nada muda (e o Undo não apagaria a dele)
    if (!desfazendo_) {
        registrarUndo("Add Tag", [this, itemId, cleanStr]() {
            removerTag(itemId, cleanStr);
        });
    }
    projeto_->registro().run(
        "INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()),
         matriz::db::Value::of(itemId),
         matriz::db::Value::of(cleanStr)});
    try {
        projeto_->registro().run(
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
            {matriz::db::Value::of(itemId), matriz::db::Value::of(cleanStr)});
        projeto_->registro().run(
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
            {matriz::db::Value::of(itemId), matriz::db::Value::of("#" + cleanStr)});
    } catch (...) {}
    try {
        projeto_->registro().run(
            "UPDATE item SET metadados_editados = 1, atualizado_em = ? WHERE id = ?",
            {matriz::db::Value::of(matriz::model::agoraIso8601()),
             matriz::db::Value::of(itemId)});
    } catch (...) {}
    EventBus::obterInstancia().dispararItemAlterado(itemId, "tags");
}

void ProjetoAberto::removerTag(const std::string& itemId, const std::string& tag) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || tag.empty()) return;
    juce::String clean = juce::String(tag).trimCharactersAtStart("#").trim();
    std::string cleanStr = clean.toStdString();
    if (!desfazendo_) {
        registrarUndo("Remove Tag", [this, itemId, cleanStr]() {
            adicionarTag(itemId, cleanStr);
        });
    }
    // Nomes case-insensitive: "SHOW" remove o "Show" do item.
    {
        const std::string k = matriz::model::nomes::chave(cleanStr);
        std::vector<std::string> doItem;
        auto st = projeto_->registro().prepare("SELECT tag FROM item_tag WHERE item_id = ?");
        st.bind(1, matriz::db::Value::of(itemId));
        while (st.step()) doItem.push_back(st.columnText(0));
        for (const auto& t : doItem)
            if (t != tag && t != cleanStr && matriz::model::nomes::chave(t) == k)
                projeto_->registro().run("DELETE FROM item_tag WHERE item_id = ? AND tag = ?",
                                          {matriz::db::Value::of(itemId), matriz::db::Value::of(t)});
    }
    projeto_->registro().run("DELETE FROM item_tag WHERE item_id = ? AND (tag = ? OR tag = ?)",
                              {matriz::db::Value::of(itemId), matriz::db::Value::of(tag), matriz::db::Value::of(cleanStr)});
    try {
        projeto_->registro().run("DELETE FROM busca_fts WHERE item_id = ? AND (conteudo = ? OR conteudo = ? OR conteudo = ?)",
                                  {matriz::db::Value::of(itemId), matriz::db::Value::of(tag), matriz::db::Value::of(cleanStr), matriz::db::Value::of("#" + cleanStr)});
    } catch (...) {}
    EventBus::obterInstancia().dispararItemAlterado(itemId, "tags");
}

std::vector<std::string> ProjetoAberto::listarPessoas() const {
    std::vector<std::string> pessoas;
    if (!projeto_) return pessoas;
    try {
        projeto_->registro().execScript(
            "CREATE TABLE IF NOT EXISTS collection_person ("
            "  id TEXT PRIMARY KEY,"
            "  nome TEXT NOT NULL UNIQUE,"
            "  criado_em TEXT NOT NULL"
            ");"
        );
        auto stmt = projeto_->registro().prepare("SELECT nome FROM collection_person ORDER BY nome COLLATE NOCASE ASC");
        while (stmt.step()) {
            pessoas.push_back(stmt.columnText(0));
        }
    } catch (...) {}
    return pessoas;
}

bool ProjetoAberto::adicionarPessoa(const std::string& nome) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || nome.empty()) return false;
    juce::String clean = juce::String(nome).trim();
    if (clean.isEmpty()) return false;
    try {
        projeto_->registro().execScript(
            "CREATE TABLE IF NOT EXISTS collection_person ("
            "  id TEXT PRIMARY KEY,"
            "  nome TEXT NOT NULL UNIQUE,"
            "  criado_em TEXT NOT NULL"
            ");"
        );
        auto stmt = projeto_->registro().prepare(
            "INSERT OR IGNORE INTO collection_person (id, nome, criado_em) VALUES (?, ?, ?)"
        );
        stmt.bind(1, matriz::db::Value::of(matriz::model::novoUuid()));
        // Nomes case-insensitive: "joão" não cria uma 2ª entrada ao lado de "João".
        stmt.bind(2, matriz::db::Value::of(matriz::model::nomes::tagCanonica(projeto_->registro(), clean.toStdString())));
        stmt.bind(3, matriz::db::Value::of(matriz::model::agoraIso8601()));
        stmt.step();
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjetoAberto::removerPessoa(const std::string& nome) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || nome.empty()) return false;
    try {
        auto stmt = projeto_->registro().prepare("DELETE FROM collection_person WHERE nome = ?");
        stmt.bind(1, matriz::db::Value::of(nome));
        stmt.step();
        return true;
    } catch (...) {
        return false;
    }
}

const std::vector<std::string>& ProjetoAberto::ultimosItensIngeridos() const {
    if (ultimosItensIngeridosValido_ || !projeto_) return ultimosItensIngeridos_;
    ultimosItensIngeridos_.clear();
    try {
        auto stmt = leitura().prepare(
            "SELECT id FROM item WHERE lote_grid_id = "
            "(SELECT MAX(lote_grid_id) FROM item WHERE lote_grid_id IS NOT NULL) "
            "AND COALESCE(em_quarentena, 0) = 0");
        while (stmt.step()) ultimosItensIngeridos_.push_back(stmt.columnText(0));
        ultimosItensIngeridosValido_ = true;
    } catch (...) {}
    return ultimosItensIngeridos_;
}

bool ProjetoAberto::recarregarOuSubstituirArquivo(const std::string& itemId, const juce::File& novoCaminho,
                                                   juce::String& erro) {
    if (!projeto_) { erro = "Nenhum projeto aberto."; return false; }
    if (somenteLeitura_) { erro = matriz::i18n::t("clone_ro.bloqueado").toStdString(); return false; }
    if (!novoCaminho.existsAsFile()) {
        erro = "Arquivo nao encontrado: " + novoCaminho.getFullPathName();
        return false;
    }

    auto& db = projeto_->registro();

    std::string masterId, masterPapel;
    {
        auto stmt = db.prepare("SELECT id, papel FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (!stmt.step()) { erro = "Este item nao tem um arquivo master."; return false; }
        masterId = stmt.columnText(0);
        masterPapel = stmt.columnText(1);
    }

    try {
        auto analise = matriz::ingest::analisarArquivo(novoCaminho);
        auto categoria = matriz::ingest::categoriaPorExtensao(novoCaminho);

        matriz::ingest::AnaliseCache cache;
        if (!analise.ehPlaceholderNuvem)
            cache = matriz::ingest::calcularCache(novoCaminho, categoria, projeto_->pasta(),
                                                   analise.leitura.duracaoSegundos);

        db.run("BEGIN TRANSACTION", {});
        std::string novoArquivoId;
        try {
            // Uma derivada por item (mesma regra que definirCapa já usa pra
            // capa): a anterior sai antes da nova entrar, senão cada
            // reload/replace empilha uma derivada nova sem limite. O master
            // (masterId) nunca é tocado aqui.
            std::vector<std::string> derivadasAntigas;
            auto stmtAntigas = db.prepare(
                "SELECT id FROM arquivo WHERE item_id = ? AND derivada_de_arquivo_id IS NOT NULL");
            stmtAntigas.bind(1, matriz::db::Value::of(itemId));
            while (stmtAntigas.step()) derivadasAntigas.push_back(stmtAntigas.columnText(0));
            for (auto& idAntigo : derivadasAntigas)
                projeto_->indice().run("DELETE FROM miniatura WHERE arquivo_id = ?",
                                        {matriz::db::Value::of(idAntigo)});
            db.run("DELETE FROM arquivo WHERE item_id = ? AND derivada_de_arquivo_id IS NOT NULL",
                   {matriz::db::Value::of(itemId)});

            auto resultado = matriz::ingest::gravarArquivoAnalisado(db, itemId, analise, masterPapel,
                                                                      /*ehMaster=*/false, masterId);
            novoArquivoId = resultado.arquivoId;
            matriz::ingest::gravarCache(db, novoArquivoId, cache);

            db.run("COMMIT", {});
        } catch (...) {
            db.run("ROLLBACK", {});
            throw;
        }

        if (!novoArquivoId.empty()) {
            // Fora da transação (miniatura mora no índice, banco separado) —
            // mesmo padrão de gerarEGravarMiniaturaPrincipal no ingest normal.
            matriz::ingest::gerarEGravarMiniaturaPrincipal(projeto_->indice(), projeto_->pasta(), itemId,
                                                             novoArquivoId, novoCaminho, categoria,
                                                             analise.leitura.duracaoSegundos);
        }

        return true;
    } catch (const std::exception& e) {
        erro = juce::String(e.what());
        return false;
    }
}

std::optional<juce::String> ProjetoAberto::caminhoMiniaturaPrincipal(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = leituraIndice().prepare(
        "SELECT caminho_relativo FROM miniatura WHERE item_id = ? AND tipo = 'miniatura' ORDER BY gerado_em DESC LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return std::nullopt;
    juce::String relativo = stmt.columnText(0);
    return projeto_->pasta().getChildFile(relativo).getFullPathName();
}

void ProjetoAberto::gerarMiniaturasFaltantes() {
    if (!projeto_) { DBG("gerarMiniaturasFaltantes: projeto_ é nulo"); return; }
    auto& reg = projeto_->registro();
    auto& idx = projeto_->indice();
    auto pasta = projeto_->pasta();

    std::set<std::string> comMiniatura;
    {
        auto stmt = idx.prepare("SELECT DISTINCT item_id FROM miniatura WHERE tipo = 'miniatura'");
        while (stmt.step()) comMiniatura.insert(stmt.columnText(0));
    }
    DBG("gerarMiniaturasFaltantes: " + juce::String((int)comMiniatura.size()) + " itens já têm miniatura");

    auto stmt = reg.prepare(
        std::string("SELECT a.item_id, a.id, a.caracteristicas_tecnicas_json, ") +
        matriz::vault::colunasDeResolucao() +
        " FROM arquivo a " + matriz::vault::joinDeResolucao() +
        " ORDER BY a.eh_master DESC, a.id");

    // Progresso visível (a geração rodava muda em background): total aproximado =
    // itens com arquivo que ainda não têm miniatura. Sem nada faltando, nenhuma tarefa.
    int totalFaltando = 0;
    {
        auto st = reg.prepare("SELECT COUNT(DISTINCT item_id) FROM arquivo");
        if (st.step()) totalFaltando = juce::jmax(0, static_cast<int>(st.columnInt(0)) - static_cast<int>(comMiniatura.size()));
    }
    const juce::String kIdTarefa = "catalog_thumbs";
    if (totalFaltando > 0)
        ProgressoGlobal::obterInstancia().iniciarTarefa(kIdTarefa, "Generating thumbnails", totalFaltando, nullptr,
                                                       "Checking files on disk...", false, true);
    juce::uint32 ultimoAviso = 0;

    std::set<std::string> jaProcessados;
    int gerados = 0, pulados = 0;
    while (stmt.step()) {
        std::string itemId = stmt.columnText(0);
        if (comMiniatura.count(itemId) || jaProcessados.count(itemId)) continue;
        jaProcessados.insert(itemId);

        std::string arquivoId = stmt.columnText(1);
        std::string jsonTecnico = stmt.columnText(2);
        juce::File arq = matriz::vault::caminhoEsperado(pasta, stmt.columnText(3),
                                                         stmt.columnText(4), stmt.columnText(5));
        if (totalFaltando > 0) {
            const juce::uint32 agora = juce::Time::getMillisecondCounter();
            if (agora - ultimoAviso >= 250) {
                ultimoAviso = agora;
                ProgressoGlobal::obterInstancia().atualizarProgresso(
                    kIdTarefa, juce::jmin(static_cast<int>(jaProcessados.size()), totalFaltando),
                    "Thumbnails " + juce::String(static_cast<int>(jaProcessados.size())) + " of " +
                        juce::String(totalFaltando) + "  |  " + arq.getFileName());
            }
        }
        if (!arq.existsAsFile()) { ++pulados; continue; }

        auto cat = matriz::ingest::categoriaPorExtensao(arq);
        if (cat != matriz::ingest::CategoriaMidia::Video &&
            cat != matriz::ingest::CategoriaMidia::Audio &&
            cat != matriz::ingest::CategoriaMidia::Imagem)
            continue;

        std::optional<double> duracao;
        juce::var raiz = juce::JSON::parse(jsonTecnico);
        if (raiz.isObject() && raiz.hasProperty("duracaoSegundos"))
            duracao = static_cast<double>(raiz["duracaoSegundos"]);

        if (!duracao.has_value() && (cat == matriz::ingest::CategoriaMidia::Video ||
                                     cat == matriz::ingest::CategoriaMidia::Audio)) {
            try {
                juce::StringArray args;
                args.add("-v"); args.add("quiet");
                args.add("-show_entries"); args.add("format=duration");
                args.add("-of"); args.add("default=noprint_wrappers=1:nokey=1");
                args.add(arq.getFullPathName());
                auto saida = matriz::ingest::capturarSaidaTexto("ffprobe", args, 10000);
                double d = juce::String(saida).trim().getDoubleValue();
                if (d > 0.0) duracao = d;
            } catch (...) {}
        }

        DBG("gerarMiniatura: " + arq.getFileName() + " cat=" + juce::String((int)cat)
            + " dur=" + juce::String(duracao.value_or(-1.0)));

        try {
            matriz::ingest::gerarEGravarMiniaturaPrincipal(idx, pasta, itemId, arquivoId, arq, cat, duracao);
            ++gerados;
        } catch (const std::exception& e) {
            DBG("gerarMiniatura ERRO: " + juce::String(e.what()));
        }
    }
    if (totalFaltando > 0)
        ProgressoGlobal::obterInstancia().concluirTarefa(
            kIdTarefa, "Catalog ready  |  " + juce::String(gerados) + " thumbnails created" +
                           (pulados > 0 ? "  |  " + juce::String(pulados) + " files offline" : juce::String()));
    DBG("gerarMiniaturasFaltantes: gerados=" + juce::String(gerados) + " pulados=" + juce::String(pulados)
        + " processados=" + juce::String((int)jaProcessados.size()));
}

std::optional<ProjetoAberto::ArquivoInfo> ProjetoAberto::arquivoPrincipal(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = leitura().prepare(
        std::string("SELECT a.id, a.papel, a.eh_master, a.caracteristicas_tecnicas_json, ") +
        matriz::vault::colunasDeResolucao() + " FROM arquivo a " + matriz::vault::joinDeResolucao() +
        // Item D.9/10 (Reload File/Replace File): uma derivada de "reload/
        // replace" (derivada_de_arquivo_id preenchido) vale mais que o
        // master enquanto existir — é o conteúdo atual do item; o master
        // propriamente dito nunca é tocado (fica preservado no histórico).
        " WHERE a.item_id = ? "
        "ORDER BY (CASE WHEN a.derivada_de_arquivo_id IS NOT NULL THEN 1 ELSE 0 END) DESC, "
        "a.eh_master DESC, a.id LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return std::nullopt;

    ArquivoInfo info;
    info.id = stmt.columnText(0);
    info.papel = stmt.columnText(1);
    info.ehMaster = stmt.columnInt(2) != 0;
    info.caracteristicasTecnicasJson = stmt.columnText(3);
    // Caminho ESPERADO, não resolvido: com o Vault offline (I3) a ficha
    // continua abrindo e o painel precisa poder dizer onde o arquivo mora.
    // Quem vai realmente ler os bytes checa existsAsFile().
    // Etapa 2: MAIN/CLONE primeiro — preview, player, miniatura e forma de
    // onda abrem a cópia do backup quando o SOURCE está guardado.
    info.caminhoAbsoluto = matriz::vault::caminhoEsperadoArquivo(leitura(), info.id, projeto_->pasta())
                               .getFullPathName();
    return info;
}

std::optional<ProjetoAberto::SugestaoCampo> ProjetoAberto::sugestaoPendente(const std::string& itemId,
                                                                             const std::string& nivel, int nivelIndice,
                                                                             const std::string& campoId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = leituraIndice().prepare(
        "SELECT id, valor, confianca, modelo, modelo_versao FROM sugestao_campo "
        "WHERE item_id = ? AND nivel = ? AND nivel_indice = ? AND campo_id = ? AND confirmado = 0 "
        "ORDER BY processado_em DESC LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    stmt.bind(2, matriz::db::Value::of(nivel));
    stmt.bind(3, matriz::db::Value::of(nivelIndice));
    stmt.bind(4, matriz::db::Value::of(campoId));
    if (!stmt.step()) return std::nullopt;

    SugestaoCampo s;
    s.id = stmt.columnText(0);
    s.valor = stmt.columnText(1);
    if (!stmt.columnIsNull(2)) s.confianca = stmt.columnReal(2);
    s.modelo = stmt.columnText(3);
    s.modeloVersao = stmt.columnText(4);
    return s;
}

void ProjetoAberto::confirmarSugestao(const SugestaoCampo& sugestao, const std::string& itemId,
                                       const std::string& nivel, int nivelIndice, const std::string& campoId,
                                       const std::string& autor) {
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();

    projeto_->registro().run(
        "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, 'humano', ?) "
        "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) "
        "DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(nivel),
         matriz::db::Value::of(nivelIndice), matriz::db::Value::of(campoId), matriz::db::Value::of(sugestao.valor),
         matriz::db::Value::of(agora)});

    projeto_->registro().run(
        "INSERT INTO item_historico (id, item_id, tipo_evento, campo_id, valor_novo, modelo_origem, "
        "modelo_origem_versao, confianca_origem, autor, criado_em) "
        "VALUES (?, ?, 'confirmacao_sugestao_ia', ?, ?, ?, ?, ?, ?, ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(campoId),
         matriz::db::Value::of(sugestao.valor), matriz::db::Value::of(sugestao.modelo),
         matriz::db::Value::of(sugestao.modeloVersao),
         sugestao.confianca ? matriz::db::Value::of(*sugestao.confianca) : matriz::db::Value::null(),
         matriz::db::Value::of(autor), matriz::db::Value::of(agora)});

    projeto_->indice().run("UPDATE sugestao_campo SET confirmado = 1, confirmado_por = ?, confirmado_em = ? WHERE id = ?",
                            {matriz::db::Value::of(autor), matriz::db::Value::of(agora), matriz::db::Value::of(sugestao.id)});
}

std::vector<ProjetoAberto::ItemObservacao> ProjetoAberto::observacoesDoItem(const std::string& itemId) const {
    std::vector<ItemObservacao> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT id, texto, autor, criado_em, minutagem_ms FROM item_observacao WHERE item_id = ? ORDER BY criado_em");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) {
        ItemObservacao o;
        o.id = stmt.columnText(0);
        o.texto = stmt.columnText(1);
        o.autor = stmt.columnText(2);
        o.criadoEm = stmt.columnText(3);
        if (!stmt.columnIsNull(4)) o.minutagemMs = static_cast<int64_t>(stmt.columnInt(4));
        out.push_back(std::move(o));
    }
    return out;
}

std::string ProjetoAberto::adicionarObservacao(const std::string& itemId, const std::string& texto,
                                                std::optional<int64_t> minutagemMs, const std::string& autor) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_) return {};
    std::string id = matriz::model::novoUuid();
    projeto_->registro().run(
        "INSERT INTO item_observacao (id, item_id, texto, autor, criado_em, minutagem_ms) VALUES (?, ?, ?, ?, ?, ?)",
        {matriz::db::Value::of(id), matriz::db::Value::of(itemId), matriz::db::Value::of(texto),
         matriz::db::Value::of(autor), matriz::db::Value::of(matriz::model::agoraIso8601()),
         minutagemMs ? matriz::db::Value::of(static_cast<long long>(*minutagemMs)) : matriz::db::Value::null()});
    return id;
}

void ProjetoAberto::atualizarObservacao(const std::string& observacaoId, const std::string& texto,
                                         std::optional<int64_t> minutagemMs) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE item_observacao SET texto = ?, minutagem_ms = ? WHERE id = ?",
        {matriz::db::Value::of(texto),
         minutagemMs ? matriz::db::Value::of(static_cast<long long>(*minutagemMs)) : matriz::db::Value::null(),
         matriz::db::Value::of(observacaoId)});
}

void ProjetoAberto::removerObservacao(const std::string& observacaoId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    projeto_->registro().run("DELETE FROM item_observacao WHERE id = ?", {matriz::db::Value::of(observacaoId)});
}

ProjetoAberto::NoArvore ProjetoAberto::arvoreOrigem(bool incluirTodos) const {
    if (!projeto_) return {};

    NoBuilder raiz;

    std::string sql =
        "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a WHERE a.caminho_absoluto_origem IS NOT NULL "
        "AND a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = a.item_id ORDER BY eh_master DESC, id LIMIT 1)";
    if (!incluirTodos)
        sql += " AND a.item_id NOT IN (SELECT item_id FROM acervo_item_pasta)";
    auto stmt = leitura().prepare(sql);

    struct Par { std::string itemId; juce::StringArray segmentos; };
    std::vector<Par> pares;

    while (stmt.step()) {
        Par p;
        p.itemId = stmt.columnText(0);
        juce::File pasta = juce::File(juce::String(stmt.columnText(1))).getParentDirectory();
        p.segmentos.addTokens(pasta.getFullPathName(), juce::File::getSeparatorString(), "");
        p.segmentos.removeEmptyStrings();
        pares.push_back(std::move(p));
    }

    // Collapse common prefix: only show from the loaded folder downward,
    // never volumes/HDs/ancestor directories.
    int prefixoComum = 0;
    if (!pares.empty()) {
        prefixoComum = pares[0].segmentos.size();
        for (size_t i = 1; i < pares.size(); ++i) {
            int n = juce::jmin(prefixoComum, pares[i].segmentos.size());
            int match = 0;
            while (match < n && pares[0].segmentos[match] == pares[i].segmentos[match])
                ++match;
            prefixoComum = match;
        }
        if (prefixoComum > 0)
            prefixoComum = prefixoComum - 1;
    }

    for (auto& p : pares) {
        NoBuilder* atual = &raiz;
        for (int s = prefixoComum; s < p.segmentos.size(); ++s)
            atual = obterOuCriarFilhoPorNome(*atual, p.segmentos[s]);
        atual->itemIdsDiretos.insert(p.itemId);
    }

    return materializar(raiz, true);
}

ProjetoAberto::NoArvore ProjetoAberto::podarArvore(const NoArvore& raiz, const std::set<std::string>& idsPermitidos) {
    NoArvore podado;
    podado.id = raiz.id;
    podado.nome = raiz.nome;
    podado.pastaPaiId = raiz.pastaPaiId;
    podado.posicaoX = raiz.posicaoX;
    podado.posicaoY = raiz.posicaoY;
    podado.ativo = raiz.ativo;

    for (const auto& id : raiz.itemIds)
        if (idsPermitidos.count(id)) podado.itemIds.insert(id);
    for (const auto& id : raiz.itemIdsDiretos)
        if (idsPermitidos.count(id)) podado.itemIdsDiretos.insert(id);

    for (const auto& filho : raiz.filhos) {
        auto filhoPodado = podarArvore(filho, idsPermitidos);
        if (!filhoPodado.itemIds.empty() || !filhoPodado.filhos.empty())
            podado.filhos.push_back(std::move(filhoPodado));
    }

    return podado;
}

const std::string ProjetoAberto::kMapaOriginal = "__ORIGINAL__";

std::string ProjetoAberto::mapaAtivoPadrao() const {
    if (!projeto_) return {};
    if (!mapaAtivoSelecionado_.empty() && mapaAtivoSelecionado_ != kMapaOriginal) {
        auto stmtSel = leitura().prepare("SELECT 1 FROM folder_map WHERE id = ? AND projeto_id = ?");
        stmtSel.bind(1, matriz::db::Value::of(mapaAtivoSelecionado_));
        stmtSel.bind(2, matriz::db::Value::of(projeto_->projetoId()));
        if (stmtSel.step()) return mapaAtivoSelecionado_;
    }
    auto stmt = leitura().prepare(
        "SELECT id FROM folder_map WHERE projeto_id = ? ORDER BY ordem, criado_em LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    return stmt.step() ? stmt.columnText(0) : std::string();
}

void ProjetoAberto::definirMapaAtivo(const std::string& mapaId) {
    mapaAtivoSelecionado_ = mapaId;
    if (!projeto_ || mapaId.empty()) return;
    projeto_->pasta().getChildFile("mapa_ativo.txt").replaceWithText(juce::String(mapaId));
}

std::string ProjetoAberto::mapaInicialDoFolderMap() const {
    if (!projeto_) return kMapaOriginal;
    const auto arq = projeto_->pasta().getChildFile("mapa_ativo.txt");
    if (!arq.existsAsFile()) return kMapaOriginal;
    const std::string lembrado = arq.loadFileAsString().trim().toStdString();
    if (lembrado.empty() || lembrado == kMapaOriginal) return kMapaOriginal;
    auto stmt = leitura().prepare("SELECT 1 FROM folder_map WHERE id = ? AND projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(lembrado));
    stmt.bind(2, matriz::db::Value::of(projeto_->projetoId()));
    return stmt.step() ? lembrado : kMapaOriginal;
}

std::string ProjetoAberto::mapaSelecionadoNoFolderMap() const {
    return mapaAtivoSelecionado_.empty() ? mapaInicialDoFolderMap() : mapaAtivoSelecionado_;
}

std::vector<ProjetoAberto::FolderMapInfo> ProjetoAberto::listarFolderMaps() const {
    std::vector<FolderMapInfo> out;
    FolderMapInfo original;
    original.id = kMapaOriginal;
    original.nome = matriz::i18n::t("mapa.original");
    original.original = true;
    out.push_back(original);
    if (!projeto_) return out;

    auto stmt = leitura().prepare(
        "SELECT id, nome FROM folder_map WHERE projeto_id = ? ORDER BY ordem, criado_em");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        FolderMapInfo info;
        info.id = stmt.columnText(0);
        info.nome = juce::String(stmt.columnText(1));
        out.push_back(info);
    }
    return out;
}

std::string ProjetoAberto::criarFolderMap(const juce::String& nome, const std::optional<std::string>& origemMapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_ || nome.trim().isEmpty()) return {};

    std::string mapaId = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();

    int ordem = 0;
    {
        auto stmt = db.prepare("SELECT COALESCE(MAX(ordem), -1) + 1 FROM folder_map WHERE projeto_id = ?");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        if (stmt.step()) ordem = stmt.columnInt(0);
    }

    db.run("INSERT INTO folder_map (id, projeto_id, nome, ordem, criado_em, atualizado_em) VALUES (?, ?, ?, ?, ?, ?)",
           {matriz::db::Value::of(mapaId), matriz::db::Value::of(projeto_->projetoId()),
            matriz::db::Value::of(nome.trim().toStdString()), matriz::db::Value::of(ordem),
            matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

    if (!origemMapaId) return mapaId; // em branco

    // Cópia do ORIGINAL ou de outro mapa: um RETRATO do momento, não uma
    // referência viva — recria pastas + item->pasta com ids novos, nunca
    // reaproveita os do mapa de origem (evita colisão e mantém os mapas
    // totalmente independentes daqui em diante).
    NoArvore origem = arvoreAcervo(*origemMapaId);
    for (auto& raizFilho : origem.filhos) {
        // arvoreAcervo() sempre acrescenta um nó sintético "não organizados"
        // como filho da raiz, com id vazio (nenhuma pasta real tem id
        // vazio) — nunca copiado pra dentro do mapa novo.
        if (raizFilho.id.empty()) continue;
        replicarSubarvoreNoAcervo(raizFilho, "", true, mapaId);
    }
    return mapaId;
}

bool ProjetoAberto::renomearFolderMap(const std::string& mapaId, const juce::String& novoNome) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || mapaId == kMapaOriginal || novoNome.trim().isEmpty()) return false;
    projeto_->registro().run("UPDATE folder_map SET nome = ?, atualizado_em = ? WHERE id = ? AND projeto_id = ?",
                              {matriz::db::Value::of(novoNome.trim().toStdString()),
                               matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(mapaId),
                               matriz::db::Value::of(projeto_->projetoId())});
    return true;
}

bool ProjetoAberto::apagarFolderMap(const std::string& mapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || mapaId == kMapaOriginal) return false;
    // Fase 2: o mapa do MAIN (backup_config_main.mapa_id) não pode ser apagado.
    if (mapaId == mapaDoMainId()) return false;
    projeto_->registro().run("DELETE FROM folder_map WHERE id = ? AND projeto_id = ?",
                              {matriz::db::Value::of(mapaId), matriz::db::Value::of(projeto_->projetoId())});
    return true;
}

std::set<std::string> ProjetoAberto::itensSemPasta(const std::string& mapaId) const {
    std::set<std::string> out;
    if (!projeto_ || mapaId == kMapaOriginal) return out;
    auto stmt = leitura().prepare(
        "SELECT id FROM item WHERE projeto_id = ? AND id NOT IN "
        "(SELECT aip.item_id FROM acervo_item_pasta aip WHERE aip.mapa_id = ?)");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(mapaId));
    while (stmt.step()) out.insert(stmt.columnText(0));
    return out;
}

int ProjetoAberto::contarItensSemPasta(const std::string& mapaId) const {
    if (!projeto_ || mapaId == kMapaOriginal) return 0;
    auto stmt = leitura().prepare(
        "SELECT COUNT(*) FROM item WHERE projeto_id = ? AND id NOT IN "
        "(SELECT aip.item_id FROM acervo_item_pasta aip WHERE aip.mapa_id = ?)");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(mapaId));
    return stmt.step() ? stmt.columnInt(0) : 0;
}

std::string ProjetoAberto::mapaIdDaPasta(const std::string& pastaId) const {
    if (!projeto_ || pastaId.empty()) return {};
    auto stmt = leitura().prepare("SELECT mapa_id FROM acervo_pasta WHERE id = ?");
    stmt.bind(1, matriz::db::Value::of(pastaId));
    if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
    return {};
}

void ProjetoAberto::inserirItemPastaInterno(const std::string& itemId, const std::string& pastaId,
                                             const std::string& agora) {
    if (!projeto_) return;
    std::string mapaId = mapaIdDaPasta(pastaId);
    projeto_->registro().run(
        "INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, mapa_id, criado_em) VALUES (?, ?, ?, ?, ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
         matriz::db::Value::of(pastaId),
         mapaId.empty() ? matriz::db::Value::null() : matriz::db::Value::of(mapaId),
         matriz::db::Value::of(agora)});
}

ProjetoAberto::NoArvore ProjetoAberto::arvoreAcervo(const std::string& mapaId) const {
    if (!projeto_) return {};
    if (mapaId == kMapaOriginal) return construirArvoreOriginalVirtual(leitura());

    struct Registro {
        std::string id;
        std::optional<std::string> paiId;
    };
    std::vector<Registro> registros;
    std::unordered_map<std::string, std::unique_ptr<NoBuilder>> porId;
    std::unordered_map<std::string, NoBuilder*> ptrPorId;

    auto stmt = leitura().prepare(
        "SELECT id, pasta_pai_id, nome, posicao_x, posicao_y, ativo, cor_customizada, regra_organizacao, escala_no FROM acervo_pasta "
        "WHERE projeto_id = ? AND mapa_id = ? ORDER BY ordem, criado_em");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(mapaId));
    while (stmt.step()) {
        Registro r;
        r.id = stmt.columnText(0);
        if (!stmt.columnIsNull(1)) r.paiId = stmt.columnText(1);
        registros.push_back(r);

        auto no = std::make_unique<NoBuilder>();
        no->id = r.id;
        if (r.paiId) no->pastaPaiId = *r.paiId;
        no->nome = stmt.columnText(2);
        no->posicaoX = stmt.columnInt(3);
        no->posicaoY = stmt.columnInt(4);
        no->ativo = (stmt.columnInt(5) != 0);
        if (!stmt.columnIsNull(6)) no->corCustomizadaHex = juce::String(stmt.columnText(6));
        if (!stmt.columnIsNull(7)) no->regraOrganizacao = juce::String(stmt.columnText(7));
        if (!stmt.columnIsNull(8)) no->escalaNo = stmt.columnReal(8);
        ptrPorId[r.id] = no.get();
        porId[r.id] = std::move(no);
    }

    NoBuilder raiz;
    for (auto& r : registros) {
        NoBuilder* alvo = (r.paiId && ptrPorId.count(*r.paiId)) ? ptrPorId[*r.paiId] : &raiz;
        alvo->filhos.push_back(std::move(porId[r.id]));
    }

    auto stmtItens = leitura().prepare(
        "SELECT pasta_id, item_id FROM acervo_item_pasta WHERE mapa_id = ?");
    stmtItens.bind(1, matriz::db::Value::of(mapaId));
    while (stmtItens.step()) {
        auto it = ptrPorId.find(stmtItens.columnText(0));
        if (it != ptrPorId.end()) it->second->itemIdsDiretos.insert(stmtItens.columnText(1));
    }

    NoArvore raizFinal = materializar(raiz, false);
    raizFinal.id.clear();
    raizFinal.nome = juce::String();

    NoArvore naoOrganizados;
    naoOrganizados.nome = matriz::i18n::t("arvore.nao_organizados");
    naoOrganizados.itemIds = itensSemPasta(mapaId);
    naoOrganizados.itemIdsDiretos = naoOrganizados.itemIds;
    raizFinal.filhos.push_back(std::move(naoOrganizados));

    return raizFinal;
}

std::string ProjetoAberto::criarPastaAcervo(const std::string& nome, const std::optional<std::string>& pastaPaiId,
                                             const std::string& mapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return {};
    std::string id = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();

    // pastaPaiId, se dado, precisa pertencer ao MESMO mapa — senão a nova
    // pasta viraria uma ponte entre dois mapas que deveriam ser
    // independentes. Defensivo: promove a raiz do mapa em vez de falhar.
    std::optional<std::string> pastaPaiIdSaneado = pastaPaiId;
    if (pastaPaiIdSaneado && mapaIdDaPasta(*pastaPaiIdSaneado) != mapaId) pastaPaiIdSaneado = std::nullopt;
    const auto& pastaPaiIdEfetivo = pastaPaiIdSaneado;

    int ordem = 0;
    {
        auto stmt = projeto_->registro().prepare(
            pastaPaiIdEfetivo ? "SELECT COALESCE(MAX(ordem), -1) + 1 FROM acervo_pasta WHERE pasta_pai_id = ?"
                       : "SELECT COALESCE(MAX(ordem), -1) + 1 FROM acervo_pasta WHERE pasta_pai_id IS NULL AND projeto_id = ? AND mapa_id = ?");
        stmt.bind(1, pastaPaiIdEfetivo ? matriz::db::Value::of(*pastaPaiIdEfetivo) : matriz::db::Value::of(projeto_->projetoId()));
        if (!pastaPaiIdEfetivo) stmt.bind(2, matriz::db::Value::of(mapaId));
        if (stmt.step()) ordem = stmt.columnInt(0);
    }

    projeto_->registro().run(
        "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mapa_id, posicao_x, posicao_y, ativo, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, 0, 0, 1, ?, ?)",
        {matriz::db::Value::of(id), matriz::db::Value::of(projeto_->projetoId()),
         pastaPaiIdEfetivo ? matriz::db::Value::of(*pastaPaiIdEfetivo) : matriz::db::Value::null(), matriz::db::Value::of(nome),
         matriz::db::Value::of(ordem), matriz::db::Value::of(mapaId), matriz::db::Value::of(agora),
         matriz::db::Value::of(agora)});

    // MAIN EDIT MODE: pasta nova no mapa do MAIN nasce junto no disco, com desfazer.
    if (editandoMain_ && !mapaDoMainId().empty() && mapaId == mapaDoMainId()) {
        auto r = matriz::mainedit::criarPastaFisica(contextoDoMain(), id);
        if (!r.ok) avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str()));
        tocarAtividadeMain();
        if (r.ok && !desfazendo_) {
            registrarUndo("Create Folder", [this, id]() {
                const auto rel = matriz::consolidacao::caminhoFisicoDaPasta(projeto_->registro(), id);
                if (!apagarPastaAcervo(id)) return;
                const juce::File dir = contextoDoMain().media().getChildFile(rel);
                if (rel.isNotEmpty() && dir.isDirectory() && dir.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0)
                    dir.deleteFile();
            });
        }
    }
    return id;
}

bool ProjetoAberto::mainExiste() const {
    if (!projeto_) return false;
    try {
        auto st = leitura().prepare("SELECT 1 FROM consolidacao_registro LIMIT 1");
        return st.step();
    } catch (...) {
        return false;
    }
}

std::string ProjetoAberto::mapaDoMainId() const {
    if (!projeto_) return {};
    try {
        auto st = leitura().prepare("SELECT COALESCE(backup_config_main, '') FROM projeto LIMIT 1");
        if (!st.step()) return {};
        juce::var cfg = juce::JSON::parse(juce::String::fromUTF8(st.columnText(0).c_str()));
        if (!cfg.isObject()) return {};
        return cfg.getProperty("mapa_id", "").toString().toStdString();
    } catch (...) {
        return {};
    }
}

juce::String ProjetoAberto::nomeDoMapaDoMain() const {
    const auto id = mapaDoMainId();
    if (id.empty() || !projeto_) return {};
    try {
        auto st = leitura().prepare("SELECT nome FROM folder_map WHERE id = ?");
        st.bind(1, matriz::db::Value::of(id));
        return st.step() ? juce::String::fromUTF8(st.columnText(0).c_str()) : juce::String();
    } catch (...) {
        return {};
    }
}

bool ProjetoAberto::pastaTemArquivosNoMain(const std::string& pastaId) const {
    if (!projeto_) return false;
    try {
        // Fase 2: as travas valem só pro mapa do MAIN; os outros mapas do
        // usuário ficam sempre livres. Sem mapa_id gravado, MAIN por regra
        // ou estrutura original não usa mapa nenhum. Só projeto sem config
        // (MAIN legado) segue a regra antiga, sem distinguir mapa.
        const auto mapaMain = mapaDoMainId();
        bool temConfig = false;
        {
            auto sc = leitura().prepare("SELECT COALESCE(backup_config_main, '') FROM projeto LIMIT 1");
            if (sc.step()) temConfig = juce::JSON::parse(juce::String::fromUTF8(sc.columnText(0).c_str())).isObject();
        }
        if (temConfig && mapaIdDaPasta(pastaId) != mapaMain) return false;
        // A pasta e todas as subpastas: algum item dali (hoje ou quando foi
        // copiado) já tem cópia registrada no MAIN.
        auto st = leitura().prepare(
            "WITH RECURSIVE sub(id) AS (SELECT ? UNION ALL "
            "  SELECT p.id FROM acervo_pasta p JOIN sub ON p.pasta_pai_id = sub.id) "
            "SELECT 1 FROM consolidacao_registro cr WHERE cr.pasta_id IN (SELECT id FROM sub) "
            "   OR cr.item_id IN (SELECT aip.item_id FROM acervo_item_pasta aip WHERE aip.pasta_id IN (SELECT id FROM sub)) "
            "LIMIT 1");
        st.bind(1, matriz::db::Value::of(pastaId));
        return st.step();
    } catch (...) {
        return false;
    }
}

// ── Auto-organização por pasta (Folder Map) ─────────────────────────────

namespace {

// Ano de um texto de data: primeira sequência de 4 dígitos entre 1800 e 2099.
juce::String anoDeTextoDeData(const juce::String& texto) {
    for (int i = 0; i + 3 < texto.length(); ++i) {
        if (juce::CharacterFunctions::isDigit(texto[i]) && juce::CharacterFunctions::isDigit(texto[i + 1]) &&
            juce::CharacterFunctions::isDigit(texto[i + 2]) && juce::CharacterFunctions::isDigit(texto[i + 3])) {
            const int ano = texto.substring(i, i + 4).getIntValue();
            if (ano >= 1800 && ano <= 2099) return juce::String(ano);
        }
    }
    return {};
}

// Mesma limpeza de nome de pasta do backup (Consolidacao.cpp: segmentoSeguro).
juce::String segmentoDePastaAuto(const juce::String& bruto, const juce::String& vazio) {
    juce::String s = bruto.trim();
    for (auto c : juce::String("/\\:*?\"<>|")) s = s.replaceCharacter(c, '-');
    s = s.trimCharactersAtEnd(". ");
    return s.isEmpty() ? vazio : s;
}

bool nivelPermitidoNaAutoOrganizacao(matriz::consolidacao::NivelHierarquia n) {
    using N = matriz::consolidacao::NivelHierarquia;
    return n == N::Ano || n == N::TipoMidia || n == N::TipoArquivo || n == N::Origem || n == N::Artista ||
           n == N::ContentType || n == N::Subject;
}

// Só as chaves de fato usadas por desfazer/refazer de uma organização.
struct LinhaPasta {
    std::string id, projetoId, paiId, nome, mapaId, cor, regra, criadoEm, atualizadoEm;
    bool paiNulo = true, corNula = true, regraNula = true;
    long long ordem = 0, x = 0, y = 0, ativo = 1;
};

} // namespace

std::optional<int> ProjetoAberto::extrairAnoDeData(const juce::String& texto) {
    const auto ano = anoDeTextoDeData(texto);
    if (ano.isEmpty()) return std::nullopt;
    return ano.getIntValue();
}

std::optional<int> ProjetoAberto::anoDoEventDate(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    try {
        auto st = leitura().prepare(
            "SELECT ano FROM item WHERE id = ?");
        st.bind(1, matriz::db::Value::of(itemId));
        if (st.step() && !st.columnIsNull(0)) return extrairAnoDeData(juce::String::fromUTF8(st.columnText(0).c_str()));
    } catch (...) {}
    return std::nullopt;
}

std::map<std::string, std::vector<juce::String>> ProjetoAberto::segmentosDeOrganizacao(
    const std::set<std::string>& itemIds, const matriz::consolidacao::HierarquiaBackup& niveis) const {
    using N = matriz::consolidacao::NivelHierarquia;
    std::map<std::string, std::vector<juce::String>> out;
    if (!projeto_ || itemIds.empty() || niveis.empty()) return out;

    const std::vector<std::string> ids(itemIds.begin(), itemIds.end());
    constexpr size_t kLote = 500;
    for (size_t inicio = 0; inicio < ids.size(); inicio += kLote) {
        const size_t fim = std::min(ids.size(), inicio + kLote);
        std::string marcas;
        for (size_t i = inicio; i < fim; ++i) marcas += (i > inicio ? ",?" : "?");
        auto stmt = leitura().prepare(
            "SELECT i.id, i.tipo_midia, i.dc_creator, i.collection_type, i.dc_subject, i.source_media, "
            "i.ano, "
            "a.caminho_relativo "
            "FROM item i "
            "LEFT JOIN arquivo a ON a.item_id = i.id AND a.id = ("
            "  SELECT a2.id FROM arquivo a2 WHERE a2.item_id = i.id ORDER BY a2.eh_master DESC, a2.id LIMIT 1) "
            "WHERE i.id IN (" + marcas + ")");
        for (size_t i = inicio; i < fim; ++i) stmt.bind(static_cast<int>(i - inicio) + 1, matriz::db::Value::of(ids[i]));

        auto texto = [&](int col) { return stmt.columnIsNull(col) ? juce::String() : juce::String::fromUTF8(stmt.columnText(col).c_str()); };
        while (stmt.step()) {
            std::vector<juce::String> segs;
            segs.reserve(niveis.size());
            for (auto nivel : niveis) {
                switch (nivel) {
                    case N::Ano: {
                        // Só o EVENT DATE (item.ano, o que a ficha mostra): vazio ou "0" = "No year"
                        // (mesma regra do Unknown do METADATA).
                        segs.push_back(segmentoDePastaAuto(anoDeTextoDeData(texto(6)), "No year"));
                        break;
                    }
                    case N::TipoMidia: segs.push_back(segmentoDePastaAuto(texto(1), "Unclassified")); break;
                    case N::TipoArquivo: {
                        const auto caminho = texto(7);
                        const int ponto = caminho.lastIndexOfChar('.');
                        const int barra = juce::jmax(caminho.lastIndexOfChar('/'), caminho.lastIndexOfChar('\\'));
                        const juce::String ext = ponto > barra ? caminho.substring(ponto + 1).toUpperCase() : juce::String();
                        segs.push_back(segmentoDePastaAuto(ext, "No extension"));
                        break;
                    }
                    case N::Origem: {
                        // source_media é um JSON com a chave "medium"; valor legado não-JSON vale direto.
                        const auto bruto = texto(5);
                        juce::String medium;
                        if (bruto.isNotEmpty()) {
                            juce::var parsed = juce::JSON::parse(bruto);
                            if (auto* obj = parsed.getDynamicObject()) {
                                if (obj->hasProperty("medium")) medium = obj->getProperty("medium").toString();
                            } else {
                                medium = bruto;
                            }
                        }
                        segs.push_back(segmentoDePastaAuto(medium, "No source medium"));
                        break;
                    }
                    case N::Artista: segs.push_back(segmentoDePastaAuto(texto(2), "No creator")); break;
                    case N::ContentType: segs.push_back(segmentoDePastaAuto(texto(3), "No content")); break;
                    case N::Subject: segs.push_back(segmentoDePastaAuto(texto(4), "No subject")); break;
                    default: break;
                }
            }
            out[stmt.columnText(0)] = std::move(segs);
        }
    }
    return out;
}

ProjetoAberto::ResultadoAutoOrg ProjetoAberto::aplicarAutoOrganizacao(const std::string& pastaId, const std::string& regraCsv,
                                                                      bool automatico) {
    ResultadoAutoOrg res;
    if (somenteLeitura_) { res.status = StatusAutoOrg::SomenteLeitura; return res; }
    if (!projeto_ || pastaId.empty()) { res.status = StatusAutoOrg::PastaInvalida; return res; }
    const std::string mapaId = mapaIdDaPasta(pastaId);
    if (mapaId.empty()) { res.status = StatusAutoOrg::PastaInvalida; return res; }
    if (mapaId == kMapaOriginal) { res.status = StatusAutoOrg::MapaOriginal; return res; }

    matriz::consolidacao::HierarquiaBackup niveis;
    for (auto n : matriz::consolidacao::hierarquiaDeCsv(regraCsv))
        if (nivelPermitidoNaAutoOrganizacao(n)) niveis.push_back(n);
    if (niveis.empty() || regraCsv.empty()) { res.status = StatusAutoOrg::SemNiveis; return res; }
    const std::string csv = matriz::consolidacao::hierarquiaParaCsv(niveis);

    auto& db = projeto_->registro();

    // 1. A pasta e as subpastas AUTO dela (à mão ficam de fora), com os itens de cada uma.
    std::vector<LinhaPasta> linhas;
    {
        auto st = db.prepare(
            "SELECT id, projeto_id, pasta_pai_id, nome, ordem, mapa_id, posicao_x, posicao_y, ativo, cor_customizada, "
            "regra_organizacao, criado_em, atualizado_em FROM acervo_pasta WHERE projeto_id = ? AND mapa_id = ? "
            "ORDER BY ordem, criado_em");
        st.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        st.bind(2, matriz::db::Value::of(mapaId));
        while (st.step()) {
            LinhaPasta l;
            l.id = st.columnText(0);
            l.projetoId = st.columnText(1);
            l.paiNulo = st.columnIsNull(2);
            if (!l.paiNulo) l.paiId = st.columnText(2);
            l.nome = st.columnText(3);
            l.ordem = st.columnInt(4);
            l.mapaId = st.columnText(5);
            l.x = st.columnInt(6);
            l.y = st.columnInt(7);
            l.ativo = st.columnInt(8);
            l.corNula = st.columnIsNull(9);
            if (!l.corNula) l.cor = st.columnText(9);
            l.regraNula = st.columnIsNull(10) || st.columnText(10).empty();
            if (!l.regraNula) l.regra = st.columnText(10);
            l.criadoEm = st.columnText(11);
            l.atualizadoEm = st.columnText(12);
            linhas.push_back(std::move(l));
        }
    }
    std::map<std::string, size_t> indice;
    for (size_t i = 0; i < linhas.size(); ++i) indice[linhas[i].id] = i;
    auto alvoIt = indice.find(pastaId);
    if (alvoIt == indice.end()) { res.status = StatusAutoOrg::PastaInvalida; return res; }
    if (regraEhAuto(juce::String(linhas[alvoIt->second].regra))) { res.status = StatusAutoOrg::SubpastaAuto; return res; }
    if (pastaTemArquivosNoMain(pastaId)) { res.status = StatusAutoOrg::TemArquivosNoMain; return res; }
    const std::string regraAnterior = linhas[alvoIt->second].regra;
    const bool regraAnteriorNula = linhas[alvoIt->second].regraNula;

    std::map<std::string, std::vector<std::string>> filhosPorPai;
    for (const auto& l : linhas)
        if (!l.paiNulo) filhosPorPai[l.paiId].push_back(l.id);
    // Subpastas AUTO alcançáveis a partir da pasta por pastas AUTO (uma pasta manual barra o caminho).
    std::vector<std::string> autoDescendentes;
    {
        std::vector<std::string> pilha{pastaId};
        while (!pilha.empty()) {
            const auto atual = pilha.back();
            pilha.pop_back();
            for (const auto& f : filhosPorPai[atual]) {
                if (regraEhAuto(juce::String(linhas[indice[f]].regra))) {
                    autoDescendentes.push_back(f);
                    pilha.push_back(f);
                }
            }
        }
    }
    std::set<std::string> pastasDeOrigem(autoDescendentes.begin(), autoDescendentes.end());
    pastasDeOrigem.insert(pastaId);

    std::map<std::string, std::vector<std::string>> pastasAtuaisDoItem;  // item -> pastas DESTE mapa
    std::set<std::string> itensAOrganizar;
    {
        auto st = db.prepare("SELECT item_id, pasta_id FROM acervo_item_pasta WHERE mapa_id = ?");
        st.bind(1, matriz::db::Value::of(mapaId));
        std::vector<std::pair<std::string, std::string>> todas;
        while (st.step()) todas.emplace_back(st.columnText(0), st.columnText(1));
        for (const auto& [item, pasta] : todas)
            if (pastasDeOrigem.count(pasta)) itensAOrganizar.insert(item);
        for (const auto& [item, pasta] : todas)
            if (itensAOrganizar.count(item)) pastasAtuaisDoItem[item].push_back(pasta);
    }

    // 2. Segmentos de cada item, numa consulta por lote.
    const auto segmentos = segmentosDeOrganizacao(itensAOrganizar, niveis);

    // 3. Caminhos distintos, em ordem alfabética, pra as pastas nascerem já ordenadas.
    auto chave = [](const std::vector<juce::String>& segs) {
        std::vector<std::string> k;
        for (const auto& s : segs) k.push_back(s.toLowerCase().toStdString());
        return k;
    };
    std::map<std::vector<std::string>, std::vector<juce::String>> caminhos;
    for (const auto& [item, segs] : segmentos) caminhos.emplace(chave(segs), segs);

    // Desfazer: uma organização = um grupo. A passada automática não registra nada (senão o
    // Cmd+Z seria desfeito de novo no próximo recarregamento do mapa).
    const bool registrarDesfazer = !automatico && !desfazendo_;
    const bool eraDesfazendo = desfazendo_;
    if (automatico) desfazendo_ = true;  // criarPastaAcervo/apagar não registram nada nesta passada
    struct RestauraFlag {
        bool& flag; bool valor;
        ~RestauraFlag() { flag = valor; }
    } restaura{desfazendo_, eraDesfazendo};
    if (registrarDesfazer) iniciarGrupoUndo("Auto-organize folder");

    std::unique_lock<std::recursive_mutex> trava(writeMutex());
    bool emTransacao = false;
    try { db.exec("BEGIN IMMEDIATE"); emTransacao = true; } catch (...) {}
    const std::string agora = matriz::model::agoraIso8601();
    std::vector<std::string> criadas;
    std::vector<LinhaPasta> apagadas;
    std::vector<std::pair<std::string, std::string>> associacoesAntes;  // (item, pasta) dos itens movidos
    std::map<std::string, std::string> destinoDoItem;
    bool falhou = false;
    try {
        // Regra da pasta.
        db.run("UPDATE acervo_pasta SET regra_organizacao = ?, atualizado_em = ? WHERE id = ?",
               {matriz::db::Value::of(csv), matriz::db::Value::of(agora), matriz::db::Value::of(pastaId)});

        // Subpastas: reaproveita as AUTO pelo nome, cria as que faltam.
        std::map<std::string, std::map<std::string, std::string>> autoFilhoPorNome;  // pai -> nome minúsculo -> id
        for (const auto& id : autoDescendentes) {
            const auto& l = linhas[indice[id]];
            autoFilhoPorNome[l.paiId][juce::String(l.nome).toLowerCase().toStdString()] = id;
        }
        std::map<std::vector<std::string>, std::string> pastaDoCaminho;
        for (const auto& [k, segs] : caminhos) {
            std::string atual = pastaId;
            for (const auto& seg : segs) {
                const auto nomeMin = seg.toLowerCase().toStdString();
                auto& mapaFilhos = autoFilhoPorNome[atual];
                auto it = mapaFilhos.find(nomeMin);
                if (it == mapaFilhos.end()) {
                    const std::string novo = criarPastaAcervo(seg.toStdString(), atual, mapaId);
                    if (novo.empty()) throw std::runtime_error("criarPastaAcervo falhou");
                    db.run("UPDATE acervo_pasta SET regra_organizacao = '@auto' WHERE id = ?", {matriz::db::Value::of(novo)});
                    criadas.push_back(novo);
                    it = mapaFilhos.emplace(nomeMin, novo).first;
                }
                atual = it->second;
            }
            pastaDoCaminho[k] = atual;
        }

        // Itens: cada um vai pra pasta do caminho dele (mesma transação, sem N transações).
        for (const auto& [item, segs] : segmentos) {
            const std::string& destino = pastaDoCaminho[chave(segs)];
            const auto& atuais = pastasAtuaisDoItem[item];
            if (atuais.size() == 1 && atuais.front() == destino) continue;
            for (const auto& p : atuais) associacoesAntes.emplace_back(item, p);
            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(item), matriz::db::Value::of(mapaId)});
            db.run("INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, mapa_id, criado_em) VALUES (?, ?, ?, ?, ?)",
                   {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item),
                    matriz::db::Value::of(destino), matriz::db::Value::of(mapaId), matriz::db::Value::of(agora)});
            destinoDoItem[item] = destino;
            ++res.itensMovidos;
        }

        // Subpastas AUTO que ficaram vazias (sem itens e sem filhas), da mais funda pra mais rasa.
        // Nunca apaga pasta manual, nem AUTO que abriga pasta manual.
        std::set<std::string> comItens;
        {
            auto st = db.prepare("SELECT DISTINCT pasta_id FROM acervo_item_pasta WHERE mapa_id = ?");
            st.bind(1, matriz::db::Value::of(mapaId));
            while (st.step()) comItens.insert(st.columnText(0));
        }
        std::map<std::string, int> filhasRestantes;
        for (const auto& l : linhas)
            if (!l.paiNulo) ++filhasRestantes[l.paiId];
        const std::set<std::string> criadasSet(criadas.begin(), criadas.end());
        for (const auto& [pai, filhos] : autoFilhoPorNome)
            for (const auto& [nome, id] : filhos)
                if (criadasSet.count(id)) ++filhasRestantes[pai];
        std::vector<std::string> candidatas = autoDescendentes;
        // Da mais funda pra mais rasa: profundidade pelo número de ancestrais AUTO.
        auto profundidade = [&](const std::string& id) {
            int p = 0;
            std::string cur = id;
            while (cur != pastaId && indice.count(cur) && !linhas[indice[cur]].paiNulo) { cur = linhas[indice[cur]].paiId; ++p; }
            return p;
        };
        std::sort(candidatas.begin(), candidatas.end(),
                  [&](const std::string& a, const std::string& b) { return profundidade(a) > profundidade(b); });
        for (const auto& id : candidatas) {
            if (comItens.count(id) || filhasRestantes[id] > 0) continue;
            LinhaPasta copia = linhas[indice[id]];
            db.run("DELETE FROM acervo_pasta WHERE id = ?", {matriz::db::Value::of(id)});
            apagadas.push_back(copia);
            --filhasRestantes[copia.paiId];
            ++res.pastasApagadas;
        }
        res.pastasCriadas = static_cast<int>(criadas.size());
    } catch (...) {
        falhou = true;
    }
    if (emTransacao) {
        try { db.exec(falhou ? "ROLLBACK" : "COMMIT"); } catch (...) { try { db.exec("ROLLBACK"); } catch (...) {} falhou = true; }
    }
    if (falhou) {
        if (registrarDesfazer) grupoAberto_.reset();
        res = ResultadoAutoOrg{};
        res.status = StatusAutoOrg::PastaInvalida;
        return res;
    }

    // Reversões, na ordem em que foram registradas (o desfazer as executa de trás pra frente):
    // regra -> pastas criadas -> itens movidos -> pastas apagadas.
    // Reorganizar sem nada a mudar não deixa entrada de desfazer.
    const bool mudouAlgo = !criadas.empty() || !destinoDoItem.empty() || !apagadas.empty() || regraAnteriorNula || regraAnterior != csv;
    if (registrarDesfazer && !mudouAlgo) grupoAberto_.reset();
    if (registrarDesfazer && mudouAlgo) {
        registrarUndo("Auto-organize folder", [this, pastaId, regraAnterior, regraAnteriorNula]() {
            projeto_->registro().run("UPDATE acervo_pasta SET regra_organizacao = ? WHERE id = ?",
                                     {regraAnteriorNula ? matriz::db::Value::null() : matriz::db::Value::of(regraAnterior),
                                      matriz::db::Value::of(pastaId)});
        });
        if (!criadas.empty())
            registrarUndo("Auto-organize folder", [this, criadas]() {
                for (auto it = criadas.rbegin(); it != criadas.rend(); ++it)
                    projeto_->registro().run("DELETE FROM acervo_pasta WHERE id = ?", {matriz::db::Value::of(*it)});
            });
        if (!associacoesAntes.empty() || !destinoDoItem.empty()) {
            std::set<std::string> movidos;
            for (const auto& [item, d] : destinoDoItem) movidos.insert(item);
            registrarUndo("Auto-organize folder", [this, mapaId, associacoesAntes, movidos]() {
                auto& reg = projeto_->registro();
                const std::string quando = matriz::model::agoraIso8601();
                reg.run("BEGIN TRANSACTION", {});
                try {
                    for (const auto& item : movidos)
                        reg.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                                {matriz::db::Value::of(item), matriz::db::Value::of(mapaId)});
                    for (const auto& [item, pasta] : associacoesAntes)
                        reg.run("INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, mapa_id, criado_em) VALUES (?, ?, ?, ?, ?)",
                                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item),
                                 matriz::db::Value::of(pasta), matriz::db::Value::of(mapaId), matriz::db::Value::of(quando)});
                    reg.run("COMMIT", {});
                } catch (...) {
                    reg.run("ROLLBACK", {});
                }
            });
        }
        if (!apagadas.empty())
            registrarUndo("Auto-organize folder", [this, apagadas]() {
                // Pais antes dos filhos (foram apagadas da mais funda pra mais rasa).
                for (auto it = apagadas.rbegin(); it != apagadas.rend(); ++it) {
                    projeto_->registro().run(
                        "INSERT OR IGNORE INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mapa_id, posicao_x, posicao_y, "
                        "ativo, cor_customizada, regra_organizacao, criado_em, atualizado_em) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        {matriz::db::Value::of(it->id), matriz::db::Value::of(it->projetoId),
                         it->paiNulo ? matriz::db::Value::null() : matriz::db::Value::of(it->paiId),
                         matriz::db::Value::of(it->nome), matriz::db::Value::of(it->ordem), matriz::db::Value::of(it->mapaId),
                         matriz::db::Value::of(it->x), matriz::db::Value::of(it->y), matriz::db::Value::of(it->ativo),
                         it->corNula ? matriz::db::Value::null() : matriz::db::Value::of(it->cor),
                         it->regraNula ? matriz::db::Value::null() : matriz::db::Value::of(it->regra),
                         matriz::db::Value::of(it->criadoEm), matriz::db::Value::of(it->atualizadoEm)});
                }
            });
        finalizarGrupoUndo();
    }
    return res;
}

void ProjetoAberto::desligarAutoOrganizacao(const std::string& pastaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || pastaId.empty()) return;
    const std::string mapaId = mapaIdDaPasta(pastaId);
    if (mapaId.empty() || mapaId == kMapaOriginal) return;
    auto& db = projeto_->registro();

    // A pasta e as subpastas AUTO dela ficam como estão: só perdem a regra e a marcação.
    std::map<std::string, std::string> regraAntes;  // id -> regra (só as que tinham)
    {
        auto st = db.prepare(
            "WITH RECURSIVE sub(id) AS (SELECT ? UNION ALL SELECT p.id FROM acervo_pasta p JOIN sub ON p.pasta_pai_id = sub.id "
            "WHERE p.regra_organizacao = '@auto') "
            "SELECT p.id, COALESCE(p.regra_organizacao, '') FROM acervo_pasta p WHERE p.id IN (SELECT id FROM sub)");
        st.bind(1, matriz::db::Value::of(pastaId));
        while (st.step())
            if (!st.columnText(1).empty()) regraAntes[st.columnText(0)] = st.columnText(1);
    }
    if (regraAntes.empty()) return;
    const std::string agora = matriz::model::agoraIso8601();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& [id, regra] : regraAntes)
            db.run("UPDATE acervo_pasta SET regra_organizacao = NULL, atualizado_em = ? WHERE id = ?",
                   {matriz::db::Value::of(agora), matriz::db::Value::of(id)});
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        return;
    }
    registrarUndo("Turn off auto-organize", [this, regraAntes]() {
        for (const auto& [id, regra] : regraAntes)
            projeto_->registro().run("UPDATE acervo_pasta SET regra_organizacao = ? WHERE id = ?",
                                     {matriz::db::Value::of(regra), matriz::db::Value::of(id)});
    });
}

int ProjetoAberto::organizarItensSoltosDasPastasComRegra(const std::string& mapaId) {
    if (somenteLeitura_ || !projeto_ || mapaId.empty() || mapaId == kMapaOriginal || organizandoAuto_) return 0;
    // Checagem barata: uma consulta só, e só pastas com regra E com itens diretos.
    std::vector<std::pair<std::string, std::string>> alvos;
    try {
        auto st = projeto_->registro().prepare(
            "SELECT p.id, p.regra_organizacao FROM acervo_pasta p WHERE p.projeto_id = ? AND p.mapa_id = ? "
            "AND p.regra_organizacao IS NOT NULL AND p.regra_organizacao <> '' AND p.regra_organizacao <> '@auto' "
            "AND EXISTS (SELECT 1 FROM acervo_item_pasta aip WHERE aip.pasta_id = p.id)");
        st.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        st.bind(2, matriz::db::Value::of(mapaId));
        while (st.step()) alvos.emplace_back(st.columnText(0), st.columnText(1));
    } catch (...) { return 0; }
    if (alvos.empty()) return 0;

    organizandoAuto_ = true;  // guarda contra recursão
    int organizadas = 0;
    for (const auto& [id, regra] : alvos) {
        const auto r = aplicarAutoOrganizacao(id, regra, true);
        if (r.status == StatusAutoOrg::Ok && (r.itensMovidos > 0 || r.pastasApagadas > 0)) ++organizadas;
    }
    organizandoAuto_ = false;
    return organizadas;
}

void ProjetoAberto::avisarMapaTravado(const juce::String& mensagem) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(matriz::i18n::t("mapa_main.titulo"))
                                     .withMessage(mensagem)
                                     .withButton(matriz::i18n::t("dialogo.ok")),
                                 juce::ModalCallbackFunction::create([](int) {}));
}

bool ProjetoAberto::renomearPastaAcervo(const std::string& pastaId, const std::string& novoNome) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_) return false;
    // MAIN EDIT MODE: renomear pasta do mapa do MAIN renomeia a pasta no disco e
    // reaponta os caminhos registrados, juntos (motor MainEdit).
    if (editandoMain_ && !mapaDoMainId().empty() && mapaIdDaPasta(pastaId) == mapaDoMainId()) {
        std::string nomeAntes;
        {
            auto st = projeto_->registro().prepare("SELECT nome FROM acervo_pasta WHERE id = ?");
            st.bind(1, matriz::db::Value::of(pastaId));
            if (st.step()) nomeAntes = st.columnText(0);
        }
        const bool registrarUndoDesta = !desfazendo_;
        auto aoTerminar = [this, pastaId, nomeAntes, registrarUndoDesta](const matriz::mainedit::Resultado& r) {
            if (!r.ok) avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str()));
            else if (registrarUndoDesta)
                registrarUndo("Rename Folder", [this, pastaId, nomeAntes]() { renomearPastaAcervo(pastaId, nomeAntes); });
            if (aoTerminarEdicaoPastaMain) aoTerminarEdicaoPastaMain();
        };
        if (contarArquivosDaPastaNoMain(pastaId) > kLimiteArquivosPastaSincrona) {
            // Muitos arquivos: background com progresso global (a UI não trava). Mesma transação única no banco.
            auto ctx = contextoDoMain();
            const auto novo = juce::String::fromUTF8(novoNome.c_str());
            executarEdicaoMain("Renaming folder in MAIN", [ctx, pastaId, novo] {
                return matriz::mainedit::renomearPasta(ctx, pastaId, novo);
            }, aoTerminar);
            return true;
        }
        auto r = matriz::mainedit::renomearPasta(contextoDoMain(), pastaId, juce::String::fromUTF8(novoNome.c_str()));
        tocarAtividadeMain();
        if (!r.ok) { avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str())); return false; }
        if (registrarUndoDesta)
            registrarUndo("Rename Folder", [this, pastaId, nomeAntes]() { renomearPastaAcervo(pastaId, nomeAntes); });
        return true;
    }
    if (pastaTemArquivosNoMain(pastaId)) {
        avisarMapaTravado(matriz::i18n::t("mapa_main.pasta_no_main"));
        return false;
    }
    if (!desfazendo_) {
        auto stmt = projeto_->registro().prepare("SELECT nome FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(pastaId));
        std::string oldNome = stmt.step() ? stmt.columnText(0) : "";
        registrarUndo("Rename Folder", [this, pastaId, oldNome]() {
            renomearPastaAcervo(pastaId, oldNome);
        });
    }
    projeto_->registro().run("UPDATE acervo_pasta SET nome = ?, atualizado_em = ? WHERE id = ?",
                              {matriz::db::Value::of(novoNome), matriz::db::Value::of(matriz::model::agoraIso8601()),
                               matriz::db::Value::of(pastaId)});
    return true;
}

bool ProjetoAberto::apagarPastaAcervo(const std::string& pastaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_) return false;
    if (pastaTemArquivosNoMain(pastaId)) {
        avisarMapaTravado(matriz::i18n::t("mapa_main.pasta_no_main"));
        return false;
    }
    projeto_->registro().run("DELETE FROM acervo_pasta WHERE id = ?", {matriz::db::Value::of(pastaId)});
    return true;
}

bool ProjetoAberto::moverPastaAcervo(const std::string& pastaId, const std::optional<std::string>& novaPastaPaiId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_) return false;
    if (editandoMain_ && !mapaDoMainId().empty() && mapaIdDaPasta(pastaId) == mapaDoMainId()) {
        std::optional<std::string> paiAntes;
        {
            auto st = projeto_->registro().prepare("SELECT pasta_pai_id FROM acervo_pasta WHERE id = ?");
            st.bind(1, matriz::db::Value::of(pastaId));
            if (st.step() && !st.columnIsNull(0)) paiAntes = st.columnText(0);
        }
        // Mesmo pai = nada a mover (arrastar sem soltar em outra pasta).
        if (paiAntes.value_or("") == novaPastaPaiId.value_or("")) return true;
        const bool registrarUndoDesta = !desfazendo_;
        const std::string novoPai = novaPastaPaiId.value_or("");
        auto aoTerminar = [this, pastaId, paiAntes, registrarUndoDesta](const matriz::mainedit::Resultado& r) {
            if (!r.ok) avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str()));
            else if (registrarUndoDesta)
                registrarUndo("Move Folder", [this, pastaId, paiAntes]() { moverPastaAcervo(pastaId, paiAntes); });
            if (aoTerminarEdicaoPastaMain) aoTerminarEdicaoPastaMain();
        };
        if (contarArquivosDaPastaNoMain(pastaId) > kLimiteArquivosPastaSincrona) {
            auto ctx = contextoDoMain();
            executarEdicaoMain("Moving folder in MAIN", [ctx, pastaId, novoPai] {
                return matriz::mainedit::moverPasta(ctx, pastaId, novoPai);
            }, aoTerminar);
            return true;
        }
        auto r = matriz::mainedit::moverPasta(contextoDoMain(), pastaId, novoPai);
        tocarAtividadeMain();
        if (!r.ok) { avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str())); return false; }
        if (registrarUndoDesta)
            registrarUndo("Move Folder", [this, pastaId, paiAntes]() { moverPastaAcervo(pastaId, paiAntes); });
        return true;
    }
    if (pastaTemArquivosNoMain(pastaId)) {
        avisarMapaTravado(matriz::i18n::t("mapa_main.pasta_no_main"));
        return false;
    }
    // Vale pra QUALQUER move de hierarquia (desconectar, reconectar via
    // socket-drag, futura reorganização) — não só o desconectar do item 7.
    // Mesmo padrão de renomearPastaAcervo: guardado por desfazendo_, então
    // desfazer() nunca registra a si mesmo de novo ao reverter.
    if (!desfazendo_) {
        std::optional<std::string> oldPaiId;
        auto stmt = projeto_->registro().prepare("SELECT pasta_pai_id FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(pastaId));
        if (stmt.step() && !stmt.columnIsNull(0)) oldPaiId = stmt.columnText(0);
        registrarUndo("Move Folder", [this, pastaId, oldPaiId]() {
            moverPastaAcervo(pastaId, oldPaiId);
        });
    }
    projeto_->registro().run(
        "UPDATE acervo_pasta SET pasta_pai_id = ?, atualizado_em = ? WHERE id = ?",
        {novaPastaPaiId ? matriz::db::Value::of(*novaPastaPaiId) : matriz::db::Value::null(),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
    return true;
}

void ProjetoAberto::atualizarEscalasPastasAcervo(const std::vector<std::pair<std::string, double>>& escalasPorPasta) {
    if (somenteLeitura_ || !projeto_ || escalasPorPasta.empty()) return;
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& [id, escala] : escalasPorPasta)
            db.run("UPDATE acervo_pasta SET escala_no = ? WHERE id = ?",
                   {escala == 1.0 ? matriz::db::Value::null() : matriz::db::Value::of(escala), matriz::db::Value::of(id)});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
}

double ProjetoAberto::fatorCardsDoMapa(const std::string& mapaId) const {
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return 0.0;
    try {
        auto st = leitura().prepare("SELECT fator_cards FROM folder_map WHERE id = ?");
        st.bind(1, matriz::db::Value::of(mapaId));
        if (st.step() && !st.columnIsNull(0)) return st.columnReal(0);
    } catch (...) {}
    return 0.0;
}

void ProjetoAberto::definirFatorCardsDoMapa(const std::string& mapaId, double fator) {
    if (somenteLeitura_ || !projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return;
    projeto_->registro().run("UPDATE folder_map SET fator_cards = ? WHERE id = ?",
                             {matriz::db::Value::of(fator), matriz::db::Value::of(mapaId)});
}

void ProjetoAberto::atualizarPosicaoPastaAcervo(const std::string& pastaId, int x, int y) {
    if (somenteLeitura_) return;
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE acervo_pasta SET posicao_x = ?, posicao_y = ?, atualizado_em = ? WHERE id = ?",
        {matriz::db::Value::of(x), matriz::db::Value::of(y),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::alternarAtivoPastaAcervo(const std::string& pastaId, bool ativo) {
    if (somenteLeitura_) return;
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE acervo_pasta SET ativo = ?, atualizado_em = ? WHERE id = ?",
        {matriz::db::Value::of(ativo ? 1 : 0),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::definirCorPastaAcervo(const std::string& pastaId, const juce::String& corArgbHex) {
    if (somenteLeitura_) return;
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE acervo_pasta SET cor_customizada = ?, atualizado_em = ? WHERE id = ?",
        {corArgbHex.isEmpty() ? matriz::db::Value::null() : matriz::db::Value::of(corArgbHex.toStdString()),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
}

juce::String ProjetoAberto::lerCorPastaAcervo(const std::string& pastaId) const {
    if (!projeto_) return {};
    try {
        auto stmt = leitura().prepare("SELECT cor_customizada FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(pastaId));
        if (stmt.step() && !stmt.columnIsNull(0)) return juce::String(stmt.columnText(0));
    } catch (...) {}
    return {};
}

std::vector<juce::String> ProjetoAberto::historicoCoresPasta() const {
    std::vector<juce::String> resultado;
    if (!projeto_) return resultado;
    try {
        auto stmt = leitura().prepare("SELECT historico_cores_pasta FROM projeto LIMIT 1");
        if (stmt.step() && !stmt.columnIsNull(0)) {
            juce::var arr = juce::JSON::parse(juce::String(stmt.columnText(0)));
            if (auto* a = arr.getArray()) {
                for (auto& v : *a) {
                    juce::String hex = v.toString();
                    if (hex.isNotEmpty()) resultado.push_back(hex);
                }
            }
        }
    } catch (...) {}
    return resultado;
}

void ProjetoAberto::definirHistoricoCoresPasta(const std::vector<juce::String>& coresHex) {
    if (somenteLeitura_) return;
    if (!projeto_) return;
    juce::Array<juce::var> arr;
    for (const auto& hex : coresHex) arr.add(hex);
    juce::String json = juce::JSON::toString(juce::var(arr), true);
    try {
        projeto_->registro().run("UPDATE projeto SET historico_cores_pasta = ?",
                                  {matriz::db::Value::of(json.toStdString())});
    } catch (...) {}
}

void ProjetoAberto::adicionarItensAPasta(const std::vector<std::string>& idsPedidos, const std::string& pastaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    const auto itemIds = expandirMembrosDeNest(idsPedidos);  // mover um nest move todos os arquivos dele juntos
    std::string mapaId = mapaIdDaPasta(pastaId);
    if (mapaId.empty()) return;
    if (!desfazendo_) {
        std::map<std::string, std::vector<std::string>> antigasPastas;
        for (const auto& id : itemIds) {
            // Só as pastas DESTE mapa — mover num mapa nunca deve mexer na
            // posição do item em outro mapa (Fase 1).
            auto stmtOld = projeto_->registro().prepare(
                "SELECT pasta_id FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?");
            stmtOld.bind(1, matriz::db::Value::of(id));
            stmtOld.bind(2, matriz::db::Value::of(mapaId));
            std::vector<std::string> pastas;
            while (stmtOld.step()) pastas.push_back(stmtOld.columnText(0));
            antigasPastas[id] = std::move(pastas);
        }
        registrarUndo("Move Items to Folder", [this, antigasPastas, mapaId]() {
            std::string agora = matriz::model::agoraIso8601();
            for (const auto& [id, pastas] : antigasPastas) {
                projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                                         {matriz::db::Value::of(id), matriz::db::Value::of(mapaId)});
                for (const auto& oldPasta : pastas) inserirItemPastaInterno(id, oldPasta, agora);
            }
        });
    }
    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (auto& itemId : itemIds) {
            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
            inserirItemPastaInterno(itemId, pastaId, agora);
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

void ProjetoAberto::adicionarItemAPastaSemRemoverOutras(const std::string& itemId, const std::string& pastaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemId.empty() || pastaId.empty()) return;
    inserirItemPastaInterno(itemId, pastaId, matriz::model::agoraIso8601());
}

std::optional<std::string> ProjetoAberto::localizarItemPorCodigo(const std::string& codigoAcervo) const {
    if (!projeto_ || codigoAcervo.empty()) return std::nullopt;
    auto stmt = leitura().prepare(
        "SELECT id FROM item WHERE projeto_id = ? AND codigo_acervo = ? LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(codigoAcervo));
    if (stmt.step()) return stmt.columnText(0);
    return std::nullopt;
}

std::string ProjetoAberto::agruparItensEmNovaPasta(const std::vector<std::string>& idsPedidos,
                                                    const std::string& mapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return {};
    const auto itemIds = expandirMembrosDeNest(idsPedidos);

    std::string newFolderId = criarPastaAcervo("New Folder", std::nullopt, mapaId);

    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& itemId : itemIds) {
            // Só tira o item das pastas DESTE mapa — outros mapas não são
            // afetados (Fase 1: mapas totalmente independentes).
            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
            inserirItemPastaInterno(itemId, newFolderId, agora);
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }

    return newFolderId;
}

void ProjetoAberto::removerItensDoBackup(const std::vector<std::string>& idsPedidos, const std::string& mapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return;
    const auto itemIds = expandirMembrosDeNest(idsPedidos);  // tirar um nest da lista tira todos os arquivos dele
    if (!desfazendo_) {
        std::vector<std::pair<std::string, std::string>> anteriores;
        for (const auto& itemId : itemIds) {
            auto stmt = projeto_->registro().prepare(
                "SELECT pasta_id FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?");
            stmt.bind(1, matriz::db::Value::of(itemId));
            stmt.bind(2, matriz::db::Value::of(mapaId));
            if (stmt.step()) {
                anteriores.push_back({itemId, stmt.columnText(0)});
            }
        }
        registrarUndo("Exclude from Backup", [this, anteriores]() {
            restaurarItensParaBackup(anteriores);
        });
    }
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (auto& itemId : itemIds)
            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

void ProjetoAberto::restaurarItensParaBackup(const std::vector<std::pair<std::string, std::string>>& itensPastas) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& [itemId, pastaId] : itensPastas)
            if (!pastaId.empty()) inserirItemPastaInterno(itemId, pastaId, agora);
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

namespace {
// Undo do REJECT (Intake): antes de apagar, as linhas do item e de tudo que
// referencia item ou arquivo (FK — lido do próprio esquema) vão pra tabelas
// TEMP da conexão, marcadas com um token; o undo as reinsere. TEMP = some ao
// fechar o projeto (a pilha de undo também não sobrevive).
struct TabelaLigada {
    std::string tabela, coluna, alvo;  // alvo: "item" ou "arquivo"
};
std::vector<TabelaLigada> tabelasLigadasAoItem(matriz::db::Database& db) {
    std::vector<TabelaLigada> out;
    auto st = db.prepare("SELECT m.name, f.\"from\", f.\"table\" FROM sqlite_master m, pragma_foreign_key_list(m.name) f "
                         "WHERE m.type = 'table' AND f.\"table\" IN ('item', 'arquivo')");
    while (st.step()) out.push_back({st.columnText(0), st.columnText(1), st.columnText(2)});
    return out;
}
std::string colunasDe(matriz::db::Database& db, const std::string& tabela) {
    std::string cols;
    auto st = db.prepare("SELECT name FROM pragma_table_info(?)");
    st.bind(1, matriz::db::Value::of(tabela));
    while (st.step()) cols += (cols.empty() ? "\"" : ", \"") + st.columnText(0) + "\"";
    return cols;
}
void guardarParaUndo(matriz::db::Database& db, const std::string& token, const std::string& tabela,
                     const std::string& onde) {
    db.exec("CREATE TEMP TABLE IF NOT EXISTS \"undo__" + tabela + "\" AS SELECT '' AS undo_token, * FROM \"" + tabela +
            "\" WHERE 0");
    db.run("INSERT INTO temp.\"undo__" + tabela + "\" SELECT ?, * FROM \"" + tabela + "\" WHERE " + onde,
           {matriz::db::Value::of(token), matriz::db::Value::of(token)});
}
} // namespace

void ProjetoAberto::removerItensDoProjeto(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    // Uma transação só pro lote inteiro (era um DELETE autocommit por item —
    // cada um pagando seu próprio overhead de WAL + cascade de FK sozinho,
    // igual o removerItensDoBackup já fazia certo logo acima).
    auto& db = projeto_->registro();
    const std::string token = matriz::model::novoUuid();
    std::vector<std::string> tabelasGuardadas;
    db.run("BEGIN TRANSACTION", {});
    try {
        if (!desfazendo_) {
            db.exec("CREATE TEMP TABLE IF NOT EXISTS undo_ids (token TEXT, id TEXT)");
            for (auto& itemId : itemIds)
                db.run("INSERT INTO temp.undo_ids (token, id) VALUES (?, ?)",
                       {matriz::db::Value::of(token), matriz::db::Value::of(itemId)});
            const std::string idsItem = "(SELECT id FROM temp.undo_ids WHERE token = ?)";
            const std::string idsArquivo = "(SELECT id FROM arquivo WHERE item_id IN " + idsItem + ")";
            guardarParaUndo(db, token, "item", "id IN " + idsItem);
            tabelasGuardadas.push_back("item");
            guardarParaUndo(db, token, "arquivo", "item_id IN " + idsItem);
            tabelasGuardadas.push_back("arquivo");
            for (const auto& t : tabelasLigadasAoItem(db)) {
                if (t.tabela == "arquivo") continue;
                guardarParaUndo(db, token, t.tabela,
                                "\"" + t.coluna + "\" IN " + (t.alvo == "item" ? idsItem : idsArquivo));
                tabelasGuardadas.push_back(t.tabela);
            }
        }
        for (auto& itemId : itemIds)
            db.run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(itemId)});
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
    if (desfazendo_ || tabelasGuardadas.empty()) return;
    // Ordem: item, arquivo, depois as filhas (FK). OR IGNORE: tabela alcançada
    // por duas colunas (ex. item_relacao a/b) guardou a linha duas vezes.
    std::sort(tabelasGuardadas.begin() + 2, tabelasGuardadas.end());
    tabelasGuardadas.erase(std::unique(tabelasGuardadas.begin() + 2, tabelasGuardadas.end()), tabelasGuardadas.end());
    const std::vector<std::string> ids = itemIds;
    registrarUndo(itemIds.size() == 1 ? "Reject Item" : "Reject " + std::to_string(itemIds.size()) + " Items",
                  [this, token, tabelasGuardadas, ids]() {
        auto& d = projeto_->registro();
        d.run("BEGIN TRANSACTION", {});
        try {
            for (const auto& t : tabelasGuardadas) {
                const std::string cols = colunasDe(d, t);
                d.run("INSERT OR IGNORE INTO \"" + t + "\" (" + cols + ") SELECT " + cols + " FROM temp.\"undo__" + t +
                          "\" WHERE undo_token = ?",
                      {matriz::db::Value::of(token)});
            }
            d.run("COMMIT", {});
        } catch (...) {
            try { d.run("ROLLBACK", {}); } catch (...) {}
            return;
        }
        for (const auto& id : ids) EventBus::obterInstancia().dispararItemAlterado(id, "quarentena");
    });
}

// ── NEST ─────────────────────────────────────────────────────────────────

namespace {
struct EstadoNest {
    std::string id, projetoId, capa, criadoEm;
    std::vector<std::string> membros;
};

// Os nests a que os itens pertencem, com todos os membros — o retrato que o Undo restaura.
std::vector<EstadoNest> capturarNests(matriz::db::Database& db, const std::set<std::string>& itemIds) {
    std::set<std::string> idsDeNest;
    {
        auto st = db.prepare("SELECT nest_id FROM nest_item WHERE item_id = ?");
        for (const auto& id : itemIds) {
            st.reset();
            st.bind(1, matriz::db::Value::of(id));
            while (st.step()) idsDeNest.insert(st.columnText(0));
        }
    }
    std::vector<EstadoNest> out;
    for (const auto& nid : idsDeNest) {
        EstadoNest e;
        e.id = nid;
        auto st = db.prepare("SELECT projeto_id, COALESCE(capa_item_id, ''), criado_em FROM nest WHERE id = ?");
        st.bind(1, matriz::db::Value::of(nid));
        if (!st.step()) continue;
        e.projetoId = st.columnText(0);
        e.capa = st.columnText(1);
        e.criadoEm = st.columnText(2);
        auto sm = db.prepare("SELECT item_id FROM nest_item WHERE nest_id = ?");
        sm.bind(1, matriz::db::Value::of(nid));
        while (sm.step()) e.membros.push_back(sm.columnText(0));
        out.push_back(std::move(e));
    }
    return out;
}

void removerNestsDosItens(matriz::db::Database& db, const std::set<std::string>& itemIds) {
    for (const auto& id : itemIds)
        db.run("DELETE FROM nest WHERE id IN (SELECT nest_id FROM nest_item WHERE item_id = ?)", {matriz::db::Value::of(id)});
}

void restaurarNests(matriz::db::Database& db, const std::vector<EstadoNest>& estados) {
    for (const auto& e : estados) {
        db.run("INSERT OR REPLACE INTO nest (id, projeto_id, capa_item_id, criado_em) VALUES (?, ?, ?, ?)",
               {matriz::db::Value::of(e.id), matriz::db::Value::of(e.projetoId),
                e.capa.empty() ? matriz::db::Value::null() : matriz::db::Value::of(e.capa), matriz::db::Value::of(e.criadoEm)});
        for (const auto& m : e.membros)
            db.run("INSERT OR REPLACE INTO nest_item (item_id, nest_id, adicionado_em) VALUES (?, ?, ?)",
                   {matriz::db::Value::of(m), matriz::db::Value::of(e.id), matriz::db::Value::of(e.criadoEm)});
    }
}
} // namespace

std::map<std::string, ProjetoAberto::NestInfo> ProjetoAberto::mapaDeNests(matriz::db::Database& registro) {
    std::map<std::string, NestInfo> out;
    try {
        std::map<std::string, std::vector<std::string>> membros;  // nest -> itens
        std::map<std::string, std::string> capaDe;
        auto st = registro.prepare(
            "SELECT ni.item_id, ni.nest_id, "
            "COALESCE(n.capa_item_id, (SELECT x.item_id FROM nest_item x JOIN item i2 ON i2.id = x.item_id "
            "                          WHERE x.nest_id = ni.nest_id ORDER BY i2.criado_em, i2.codigo_acervo LIMIT 1)) "
            "FROM nest_item ni JOIN nest n ON n.id = ni.nest_id");
        while (st.step()) {
            membros[st.columnText(1)].push_back(st.columnText(0));
            capaDe[st.columnText(1)] = st.columnIsNull(2) ? std::string() : st.columnText(2);
        }
        for (const auto& [nid, itens] : membros) {
            if (itens.size() < 2) continue;  // nest de um arquivo só não existe
            NestInfo info;
            info.nestId = nid;
            info.capaId = capaDe[nid];
            info.total = static_cast<int>(itens.size());
            for (const auto& id : itens) out[id] = info;
        }
    } catch (...) {}
    return out;
}

std::string ProjetoAberto::criarNest(const std::vector<std::string>& itemIdsPedidos) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_ || itemIdsPedidos.size() < 2) return {};
    auto& db = projeto_->registro();

    std::set<std::string> conjunto(itemIdsPedidos.begin(), itemIdsPedidos.end());
    const auto antes = capturarNests(db, conjunto);
    for (const auto& e : antes) conjunto.insert(e.membros.begin(), e.membros.end());  // nest sobre nest: junta tudo
    if (conjunto.size() < 2) return {};

    std::string nestId = matriz::model::novoUuid();
    std::string capa, criadoEm = matriz::model::agoraIso8601();
    if (!antes.empty()) {  // reaproveita o nest mais antigo (e a capa dele, se continuar entre os arquivos)
        const auto maisAntigo = std::min_element(antes.begin(), antes.end(),
                                                 [](const EstadoNest& a, const EstadoNest& b) { return a.criadoEm < b.criadoEm; });
        nestId = maisAntigo->id;
        criadoEm = maisAntigo->criadoEm;
        capa = maisAntigo->capa;
    }
    if (capa.empty() || conjunto.count(capa) == 0)
        capa = ordenarPorDataDeCriacao(std::vector<std::string>(conjunto.begin(), conjunto.end())).front();  // 1º por data

    db.run("BEGIN TRANSACTION", {});
    try {
        removerNestsDosItens(db, conjunto);
        EstadoNest novo;
        novo.id = nestId;
        novo.projetoId = projeto_->projetoId();
        novo.capa = capa;
        novo.criadoEm = criadoEm;
        novo.membros.assign(conjunto.begin(), conjunto.end());
        restaurarNests(db, {novo});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    if (!desfazendo_) {
        registrarUndo("Nest " + std::to_string(conjunto.size()) + " Files", [this, antes, conjunto]() {
            auto& d = projeto_->registro();
            d.run("BEGIN TRANSACTION", {});
            try {
                removerNestsDosItens(d, conjunto);
                restaurarNests(d, antes);
                d.run("COMMIT", {});
            } catch (...) { try { d.run("ROLLBACK", {}); } catch (...) {} return; }
            EventBus::obterInstancia().dispararItemAlterado("", "nest");
        });
    }
    EventBus::obterInstancia().dispararItemAlterado("", "nest");
    return nestId;
}

void ProjetoAberto::desfazerNest(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    auto& db = projeto_->registro();
    const std::set<std::string> pedidos(itemIds.begin(), itemIds.end());
    const auto antes = capturarNests(db, pedidos);
    if (antes.empty()) return;
    std::set<std::string> todos = pedidos;
    for (const auto& e : antes) todos.insert(e.membros.begin(), e.membros.end());

    db.run("BEGIN TRANSACTION", {});
    try {
        removerNestsDosItens(db, todos);
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    if (!desfazendo_) {
        registrarUndo("Un-nest", [this, antes]() {
            auto& d = projeto_->registro();
            d.run("BEGIN TRANSACTION", {});
            try {
                restaurarNests(d, antes);
                d.run("COMMIT", {});
            } catch (...) { try { d.run("ROLLBACK", {}); } catch (...) {} return; }
            EventBus::obterInstancia().dispararItemAlterado("", "nest");
        });
    }
    EventBus::obterInstancia().dispararItemAlterado("", "nest");
}

void ProjetoAberto::definirCapaDoNest(const std::string& nestId, const std::string& itemId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || nestId.empty() || itemId.empty()) return;
    auto& db = projeto_->registro();
    std::string capaAntiga;
    {
        auto st = db.prepare("SELECT COALESCE(n.capa_item_id, '') FROM nest n "
                             "WHERE n.id = ? AND EXISTS (SELECT 1 FROM nest_item WHERE nest_id = n.id AND item_id = ?)");
        st.bind(1, matriz::db::Value::of(nestId));
        st.bind(2, matriz::db::Value::of(itemId));
        if (!st.step()) return;  // o arquivo não é deste nest
        capaAntiga = st.columnText(0);
    }
    if (capaAntiga == itemId) return;
    db.run("UPDATE nest SET capa_item_id = ? WHERE id = ?", {matriz::db::Value::of(itemId), matriz::db::Value::of(nestId)});
    if (!capaAntiga.empty()) transferirMarcacoes(capaAntiga, itemId);  // o EXPORT segue o que está marcado
    if (!desfazendo_) {
        registrarUndo("Change Nest Cover", [this, nestId, capaAntiga, itemId]() {
            projeto_->registro().run("UPDATE nest SET capa_item_id = ? WHERE id = ?",
                                     {capaAntiga.empty() ? matriz::db::Value::null() : matriz::db::Value::of(capaAntiga),
                                      matriz::db::Value::of(nestId)});
            if (!capaAntiga.empty()) transferirMarcacoes(itemId, capaAntiga);
            EventBus::obterInstancia().dispararItemAlterado("", "nest");
        });
    }
    EventBus::obterInstancia().dispararItemAlterado("", "nest");
}

std::vector<std::string> ProjetoAberto::ordenarPorDataDeCriacao(const std::vector<std::string>& ids) const {
    if (!projeto_ || ids.size() < 2) return ids;
    std::vector<std::pair<std::pair<std::string, std::string>, std::string>> chaves;  // ((data, código), id)
    auto st = leitura().prepare(
        "SELECT COALESCE((SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'data_criacao'), "
        "                (SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'dc_created'), "
        "                i.criado_em), i.codigo_acervo FROM item i WHERE i.id = ?");
    for (const auto& id : ids) {
        st.reset();
        st.bind(1, matriz::db::Value::of(id));
        std::string data, codigo;
        if (st.step()) {
            data = st.columnIsNull(0) ? std::string() : st.columnText(0);
            codigo = st.columnIsNull(1) ? std::string() : st.columnText(1);
        }
        chaves.push_back({{data, codigo}, id});
    }
    std::sort(chaves.begin(), chaves.end());
    std::vector<std::string> out;
    out.reserve(chaves.size());
    for (auto& c : chaves) out.push_back(c.second);
    return out;
}

std::vector<std::string> ProjetoAberto::membrosDoNest(const std::string& nestId) const {
    std::vector<std::string> ids;
    if (!projeto_ || nestId.empty()) return ids;
    auto st = leitura().prepare("SELECT item_id FROM nest_item WHERE nest_id = ?");
    st.bind(1, matriz::db::Value::of(nestId));
    while (st.step()) ids.push_back(st.columnText(0));
    return ordenarPorDataDeCriacao(ids);
}

std::optional<ProjetoAberto::NestInfo> ProjetoAberto::nestDoItem(const std::string& itemId) const {
    if (!projeto_ || itemId.empty()) return std::nullopt;
    auto st = leitura().prepare("SELECT nest_id FROM nest_item WHERE item_id = ?");
    st.bind(1, matriz::db::Value::of(itemId));
    if (!st.step()) return std::nullopt;
    const std::string nid = st.columnText(0);
    const auto mapa = mapaDeNests(leitura());
    auto it = mapa.find(itemId);
    if (it == mapa.end() || it->second.nestId != nid) return std::nullopt;
    return it->second;
}

std::vector<std::string> ProjetoAberto::semMembrosNaoCapaDeNest(const std::vector<std::string>& itemIds) const {
    if (!projeto_ || itemIds.empty()) return itemIds;
    const auto nests = mapaDeNests(leitura());
    if (nests.empty()) return itemIds;
    std::vector<std::string> out;
    out.reserve(itemIds.size());
    for (const auto& id : itemIds) {
        auto it = nests.find(id);
        if (it == nests.end() || it->second.capaId == id) out.push_back(id);
    }
    return out;
}

std::optional<std::vector<std::string>> ProjetoAberto::idsDoCatalogoSemNaoCapas() const {
    if (!projeto_) return std::nullopt;
    const auto nests = mapaDeNests(leitura());
    if (nests.empty()) return std::nullopt;
    std::vector<std::string> out;
    auto st = leitura().prepare("SELECT id FROM item WHERE COALESCE(em_quarentena, 0) = 0");
    while (st.step()) {
        const std::string id = st.columnText(0);
        auto it = nests.find(id);
        if (it == nests.end() || it->second.capaId == id) out.push_back(id);
    }
    return out;
}

std::vector<std::string> ProjetoAberto::expandirMembrosDeNest(const std::vector<std::string>& itemIds) const {
    if (!projeto_ || itemIds.empty()) return itemIds;
    std::vector<std::string> out = itemIds;
    std::set<std::string> ja(itemIds.begin(), itemIds.end());
    std::set<std::string> nestsVistos;
    auto st = leitura().prepare("SELECT nest_id FROM nest_item WHERE item_id = ?");
    auto sm = leitura().prepare("SELECT item_id FROM nest_item WHERE nest_id = ?");
    for (const auto& id : itemIds) {
        st.reset();
        st.bind(1, matriz::db::Value::of(id));
        if (!st.step()) continue;
        const std::string nid = st.columnText(0);
        if (!nestsVistos.insert(nid).second) continue;
        sm.reset();
        sm.bind(1, matriz::db::Value::of(nid));
        while (sm.step()) {
            const std::string m = sm.columnText(0);
            if (ja.insert(m).second) out.push_back(m);
        }
    }
    return out;
}

std::set<std::string> ProjetoAberto::idsMarcadosR() const {
    std::set<std::string> out;
    if (!projeto_) return out;
    try {
        auto st = leitura().prepare(
            "SELECT m.item_id FROM intake_marca_r m JOIN item i ON i.id = m.item_id WHERE COALESCE(i.em_quarentena, 0) = 1");
        while (st.step()) out.insert(st.columnText(0));
    } catch (...) {}
    return out;
}

void ProjetoAberto::alternarMarcaR(const std::vector<std::string>& itemIds) {
    if (itemIds.empty() || !projeto_) return;
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    auto& db = projeto_->registro();
    const auto marcados = idsMarcadosR();
    bool todosMarcados = true;
    for (const auto& id : itemIds)
        if (marcados.count(id) == 0) { todosMarcados = false; break; }
    const std::string agora = matriz::model::agoraIso8601();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& id : itemIds) {
            if (todosMarcados)
                db.run("DELETE FROM intake_marca_r WHERE item_id = ?", {matriz::db::Value::of(id)});
            else
                db.run("INSERT OR IGNORE INTO intake_marca_r (item_id, origem, marcado_em) VALUES (?, 'usuario', ?)",
                       {matriz::db::Value::of(id), matriz::db::Value::of(agora)});
        }
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
}

int ProjetoAberto::rejeitarMarcadosR() {
    if (!projeto_) return 0;
    if (somenteLeitura_) { avisarSomenteLeitura(); return 0; }
    const auto marcados = idsMarcadosR();
    if (marcados.empty()) return 0;
    const std::vector<std::string> ids(marcados.begin(), marcados.end());
    auto& db = projeto_->registro();

    // SHA-256 (e nome) de cada um, lidos ANTES de o item sair — depois não há mais de onde ler.
    std::vector<std::pair<std::string, std::string>> hashes;
    {
        auto st = db.prepare("SELECT checksum_sha256, caminho_relativo FROM arquivo "
                             "WHERE item_id = ? AND checksum_sha256 IS NOT NULL AND checksum_sha256 != ''");
        for (const auto& id : ids) {
            st.reset();
            st.bind(1, matriz::db::Value::of(id));
            while (st.step())
                hashes.push_back({st.columnText(0), juce::File(juce::String(st.columnText(1))).getFileName().toStdString()});
        }
    }

    // Um Cmd+Z desfaz tudo junto: os itens voltam e os hashes novos saem da lista.
    iniciarGrupoUndo(ids.size() == 1 ? "Reject Marked File" : "Reject " + std::to_string(ids.size()) + " Marked Files");
    try {
        removerItensDoProjeto(ids);
    } catch (...) {
        finalizarGrupoUndo();
        throw;
    }

    std::vector<std::string> novos;
    const std::string agora = matriz::model::agoraIso8601();
    db.run("BEGIN TRANSACTION", {});
    try {
        auto existe = db.prepare("SELECT 1 FROM intake_rejeitados WHERE sha256 = ?");
        for (const auto& [sha, nome] : hashes) {
            existe.reset();
            existe.bind(1, matriz::db::Value::of(sha));
            if (existe.step()) continue;
            db.run("INSERT INTO intake_rejeitados (sha256, nome_original, rejeitado_em) VALUES (?, ?, ?)",
                   {matriz::db::Value::of(sha), matriz::db::Value::of(nome), matriz::db::Value::of(agora)});
            novos.push_back(sha);
        }
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        novos.clear();
    }
    if (!novos.empty()) {
        registrarUndo("Reject Marked Files", [this, novos]() {
            auto& d = projeto_->registro();
            d.run("BEGIN TRANSACTION", {});
            try {
                for (const auto& sha : novos)
                    d.run("DELETE FROM intake_rejeitados WHERE sha256 = ?", {matriz::db::Value::of(sha)});
                d.run("COMMIT", {});
            } catch (...) { try { d.run("ROLLBACK", {}); } catch (...) {} }
        });
    }
    finalizarGrupoUndo();
    return static_cast<int>(ids.size());
}

void ProjetoAberto::renomearItens(const std::vector<std::string>& itemIds, const std::string& novoTitulo) {
    std::vector<std::pair<std::string, std::string>> itemETitulo;
    itemETitulo.reserve(itemIds.size());
    for (const auto& id : itemIds) itemETitulo.emplace_back(id, novoTitulo);
    renomearItensComTitulos(itemETitulo);
}

void ProjetoAberto::renomearItensComTitulos(const std::vector<std::pair<std::string, std::string>>& itemETitulo) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemETitulo.empty()) return;
    if (!desfazendo_) {
        std::vector<std::pair<std::string, std::string>> antigosTitulos;
        for (const auto& par : itemETitulo) {
            const auto& id = par.first;
            auto stmt = projeto_->registro().prepare("SELECT titulo FROM item WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(id));
            if (stmt.step()) antigosTitulos.push_back({id, stmt.columnText(0)});
        }
        registrarUndo("Rename Items", [this, antigosTitulos]() {
            renomearItensComTitulos(antigosTitulos);
        });
    }
    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& [itemId, novoTitulo] : itemETitulo) {
            juce::String tituloNovoTrim = juce::String(novoTitulo).trim();
            auto stmtAntigo = db.prepare("SELECT titulo, notas_livres FROM item WHERE id = ?");
            stmtAntigo.bind(1, matriz::db::Value::of(itemId));
            std::string tituloAntigo;
            std::string notasAtuais;
            if (stmtAntigo.step()) {
                tituloAntigo = stmtAntigo.columnText(0);
                if (!stmtAntigo.columnIsNull(1)) notasAtuais = stmtAntigo.columnText(1);
            }

            db.run("UPDATE item SET titulo = ?, atualizado_em = ? WHERE id = ?",
                   {matriz::db::Value::of(novoTitulo), matriz::db::Value::of(agora),
                    matriz::db::Value::of(itemId)});

            bool tituloRealmenteMudou = tituloNovoTrim != juce::String(tituloAntigo).trim();

            if (!desfazendo_ && tituloRealmenteMudou && !tituloAntigo.empty()) {
                std::string linhaNota = "Previous Name: " + tituloAntigo;
                std::string notasNovas = notasAtuais.empty() ? linhaNota : (notasAtuais + "\n" + linhaNota);
                db.run("UPDATE item SET notas_livres = ? WHERE id = ?",
                       {matriz::db::Value::of(notasNovas), matriz::db::Value::of(itemId)});
            }

            // Modelo SOURCE/MAIN (etapa 5): o nome físico de um arquivo já no
            // MAIN nunca muda — renomear o item muda só o catálogo (e o nome
            // "Previous Name" nas notas). Antes, sincronizarNomeDeBackupAposRenomear
            // renomeava a cópia no backup a cada edição de título.
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }

    // UM evento de lote, depois do COMMIT (antes: um por item, com a transação ainda aberta).
    std::vector<std::string> ids;
    ids.reserve(itemETitulo.size());
    for (const auto& par : itemETitulo) ids.push_back(par.first);
    EventBus::obterInstancia().dispararItensAlterados(ids, "titulo");
}

void ProjetoAberto::alternarMarcadoRevisado(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    bool todosMarcados = true;
    for (const auto& id : itemIds) {
        auto stmt = projeto_->registro().prepare("SELECT marcado_revisado FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(id));
        if (!stmt.step() || stmt.columnIsNull(0) || stmt.columnInt(0) == 0) {
            todosMarcados = false;
            break;
        }
    }
    int novoValor = todosMarcados ? 0 : 1;
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& id : itemIds) {
            db.run("UPDATE item SET marcado_revisado = ? WHERE id = ?",
                   {matriz::db::Value::of(novoValor), matriz::db::Value::of(id)});
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
    for (const auto& id : itemIds) {
        EventBus::obterInstancia().dispararItemAlterado(id, "marcado_revisado");
    }
}

bool ProjetoAberto::itemMarcadoRevisado(const std::string& itemId) const {
    if (!projeto_ || itemId.empty()) return false;
    try {
        auto stmt = leitura().prepare("SELECT COALESCE(marcado_revisado, 0) != 0 FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) return stmt.columnInt(0) != 0;
    } catch (...) {}
    return false;
}

void ProjetoAberto::limparMarcadoRevisadoDe(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& id : itemIds)
            db.run("UPDATE item SET marcado_revisado = 0 WHERE id = ? AND marcado_revisado != 0", {matriz::db::Value::of(id)});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    for (const auto& id : itemIds) EventBus::obterInstancia().dispararItemAlterado(id, "marcado_revisado");
}

void ProjetoAberto::limparTodosMarcadosRevisado() {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    projeto_->registro().run("UPDATE item SET marcado_revisado = 0 WHERE marcado_revisado != 0", {});
    EventBus::obterInstancia().dispararItemAlterado("", "marcado_revisado");
}

std::vector<ProjetoAberto::AlvoArquivo> ProjetoAberto::alvosArquivoPrincipal(const std::vector<std::string>& itemIds) const {
    std::vector<AlvoArquivo> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        std::string("SELECT a.id, ") + matriz::vault::colunasDeResolucao() + " FROM arquivo a " +
        matriz::vault::joinDeResolucao() + " WHERE a.item_id = ? ORDER BY a.eh_master DESC, a.id LIMIT 1");
    for (const auto& itemId : itemIds) {
        stmt.reset();
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (!stmt.step()) continue;
        out.push_back({itemId, stmt.columnText(0), stmt.columnText(1), stmt.columnText(2), stmt.columnText(3)});
    }
    return out;
}

std::shared_ptr<matriz::vault::ResolvedorEmLote> ProjetoAberto::criarResolvedorEmLote() const {
    if (!projeto_) return nullptr;
    return std::make_shared<matriz::vault::ResolvedorEmLote>(projeto_->registro(), projeto_->pasta());
}

int ProjetoAberto::gravarOutraMetadataEmLote(const std::map<std::string, std::string>& textoPorItem) {
    if (!projeto_ || textoPorItem.empty()) return 0;
    auto& db = projeto_->registro();
    const std::string agora = matriz::model::agoraIso8601();
    std::unique_lock<std::recursive_mutex> writeLock(projeto_->writeMutex());
    int gravados = 0;
    try {
        db.exec("BEGIN IMMEDIATE");
        auto ler = db.prepare("SELECT notas_livres FROM item WHERE id = ?");
        for (const auto& [itemId, texto] : textoPorItem) {
            ler.reset();
            ler.bind(1, matriz::db::Value::of(itemId));
            std::string atual;
            if (ler.step() && !ler.columnIsNull(0)) atual = ler.columnText(0);
            // A automática vai sempre primeiro (como no ingest); as seções do
            // usuário ficam como estavam.
            std::vector<matriz::model::SecaoNota> secoes = {{matriz::model::kOutraMetadataTitulo, texto, true}};
            for (auto& sec : matriz::model::parseNotasEstruturadas(atual))
                if (!sec.automatica) secoes.push_back(std::move(sec));
            db.run("UPDATE item SET notas_livres = ?, atualizado_em = ? WHERE id = ?",
                   {matriz::db::Value::of(matriz::model::serializarNotasEstruturadas(secoes)),
                    matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
            ++gravados;
        }
        db.exec("COMMIT");
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        return 0;
    }
    return gravados;
}

std::optional<juce::String> ProjetoAberto::caminhoDeOrigem(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;

    // 1. Try resolving via arquivo table with vault resolution
    try {
        auto stmt = projeto_->registro().prepare(
            std::string("SELECT ") + matriz::vault::colunasDeResolucao() +
            " FROM arquivo a " + matriz::vault::joinDeResolucao() +
            " WHERE a.item_id = ? ORDER BY a.eh_master DESC, a.id LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) {
            std::string locVault = stmt.columnText(0);
            std::string camRel = stmt.columnText(1);
            std::string camAbs = stmt.columnText(2);

            auto f = matriz::vault::resolverCaminho(projeto_->pasta(), locVault, camRel, camAbs);
            if (f && f->existsAsFile()) {
                return f->getFullPathName();
            }
            auto fEsp = matriz::vault::caminhoEsperado(projeto_->pasta(), locVault, camRel, camAbs);
            if (fEsp != juce::File() && fEsp.getFullPathName().isNotEmpty()) {
                return fEsp.getFullPathName();
            }
            if (!camAbs.empty()) {
                return juce::String(camAbs);
            }
        }
    } catch (...) {}

    // 2. Try simple caminho_absoluto_origem query directly
    try {
        auto stmt2 = projeto_->registro().prepare(
            "SELECT caminho_absoluto_origem FROM arquivo WHERE item_id = ? AND caminho_absoluto_origem IS NOT NULL AND caminho_absoluto_origem != '' "
            "ORDER BY eh_master DESC, id LIMIT 1");
        stmt2.bind(1, matriz::db::Value::of(itemId));
        if (stmt2.step()) {
            juce::String c = stmt2.columnText(0);
            if (c.isNotEmpty()) return c;
        }
    } catch (...) {}

    // 3. Try localizacao_conhecida
    try {
        auto stmtLoc = projeto_->registro().prepare(
            "SELECT lc.caminho_absoluto FROM localizacao_conhecida lc "
            "JOIN arquivo a ON a.id = lc.arquivo_id WHERE a.item_id = ? "
            "ORDER BY lc.criado_em DESC LIMIT 1");
        stmtLoc.bind(1, matriz::db::Value::of(itemId));
        if (stmtLoc.step()) {
            juce::String c = stmtLoc.columnText(0);
            if (c.isNotEmpty()) return c;
        }
    } catch (...) {}

    // 4. Try caminho_catalogo in item table
    try {
        auto stmtItem = projeto_->registro().prepare("SELECT caminho_catalogo FROM item WHERE id = ?");
        stmtItem.bind(1, matriz::db::Value::of(itemId));
        if (stmtItem.step()) {
            juce::String camCat = stmtItem.columnText(0);
            if (camCat.isNotEmpty()) return camCat;
        }
    } catch (...) {}

    // 5. Try item_campo metadata
    try {
        auto stmtCampo = projeto_->registro().prepare(
            "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id IN ('caminho', 'caminho_absoluto', 'caminho_origem', 'path') LIMIT 1");
        stmtCampo.bind(1, matriz::db::Value::of(itemId));
        if (stmtCampo.step()) {
            juce::String val = stmtCampo.columnText(0);
            if (val.isNotEmpty()) return val;
        }
    } catch (...) {}

    // 6. Try relative path resolution directly against disk / volumes
    try {
        auto stmtRel = projeto_->registro().prepare(
            "SELECT caminho_relativo FROM arquivo WHERE item_id = ? AND caminho_relativo IS NOT NULL AND caminho_relativo != '' "
            "ORDER BY eh_master DESC, id LIMIT 1");
        stmtRel.bind(1, matriz::db::Value::of(itemId));
        if (stmtRel.step()) {
            juce::String rel = stmtRel.columnText(0);
            if (rel.isNotEmpty()) {
                if (juce::File::isAbsolutePath(rel)) {
                    juce::File f(rel);
                    if (f.existsAsFile() || f.isDirectory()) return f.getFullPathName();
                }
                juce::File inProj = projeto_->pasta().getChildFile(rel);
                if (inProj.existsAsFile() || inProj.isDirectory()) return inProj.getFullPathName();

                juce::File inVol = juce::File("/Volumes").getChildFile(rel);
                if (inVol.existsAsFile() || inVol.isDirectory()) return inVol.getFullPathName();
                
                return rel;
            }
        }
    } catch (...) {}

    return std::nullopt;
}

std::set<std::string> ProjetoAberto::itensComMesmoConteudo(const std::string& itemId) const {
    std::set<std::string> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT DISTINCT a2.item_id FROM arquivo a1 "
        "JOIN arquivo a2 ON a2.checksum_sha256 = a1.checksum_sha256 AND a2.tamanho_bytes = a1.tamanho_bytes "
        "WHERE a1.item_id = ? AND a1.checksum_sha256 IS NOT NULL AND a1.checksum_sha256 <> '' "
        "AND a1.tamanho_bytes > 0 AND a2.tamanho_bytes > 0");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.insert(stmt.columnText(0));

    // Só o próprio item = não há duplicata; devolver {ele} faria a grade
    // filtrar pra um item só e parecer que "achou" alguma coisa.
    if (out.size() <= 1) out.clear();
    return out;
}

std::vector<ProjetoAberto::ParDuplicatas> ProjetoAberto::listarGruposDuplicados() const {
    std::vector<ParDuplicatas> out;
    if (!projeto_) return out;

    struct DbInfo {
        std::string itemId;
        std::string codigoAcervo;
        std::string titulo;
        std::string tipoMidia;
        std::string estado;
        juce::String caminhoRelativo;
        juce::String caminhoOrigem;
        juce::int64 tamanhoBytes = 0;
        std::string checksumSha256;
    };

    std::vector<DbInfo> arquivos;

    auto stmt = leitura().prepare(
        "SELECT a.item_id, i.codigo_acervo, i.titulo, i.tipo_midia, i.estado, "
        "a.caminho_relativo, a.caminho_absoluto_origem, a.tamanho_bytes, a.checksum_sha256 "
        "FROM arquivo a "
        "JOIN item i ON i.id = a.item_id "
        "WHERE a.eh_master = 1 AND i.estado <> 'duplicata'");

    while (stmt.step()) {
        DbInfo inf;
        inf.itemId = stmt.columnText(0);
        inf.codigoAcervo = stmt.columnIsNull(1) ? "" : stmt.columnText(1);
        inf.titulo = stmt.columnText(2);
        inf.tipoMidia = stmt.columnIsNull(3) ? "" : stmt.columnText(3);
        inf.estado = stmt.columnText(4);
        inf.caminhoRelativo = stmt.columnText(5);
        inf.caminhoOrigem = stmt.columnIsNull(6) ? "" : stmt.columnText(6);
        inf.tamanhoBytes = stmt.columnIsNull(7) ? 0 : static_cast<juce::int64>(stmt.columnInt(7));
        inf.checksumSha256 = stmt.columnIsNull(8) ? "" : stmt.columnText(8);

        if (inf.tamanhoBytes > 0 && !inf.checksumSha256.empty()) {
            arquivos.push_back(inf);
        }
    }

    auto extrairNome = [](const juce::String& rel, const juce::String& orig) -> juce::String {
        juce::String caminho = orig.isNotEmpty() ? orig : rel;
        int slashPos = std::max(caminho.lastIndexOfChar('/'), caminho.lastIndexOfChar('\\'));
        return slashPos >= 0 ? caminho.substring(slashPos + 1) : caminho;
    };

    std::map<std::tuple<juce::String, juce::int64, std::string>, std::vector<DbInfo>> grupos;
    for (const auto& a : arquivos) {
        juce::String fname = extrairNome(a.caminhoRelativo, a.caminhoOrigem).toLowerCase();
        auto key = std::make_tuple(fname, a.tamanhoBytes, a.checksumSha256);
        grupos[key].push_back(a);
    }

    for (const auto& [key, itensGrupo] : grupos) {
        std::set<std::string> itemIds;
        for (const auto& item : itensGrupo) {
            itemIds.insert(item.itemId);
        }

        if (itemIds.size() > 1) {
            ParDuplicatas grupo;
            const auto& firstItem = itensGrupo.front();
            grupo.filename = extrairNome(firstItem.caminhoRelativo, firstItem.caminhoOrigem);
            grupo.tamanhoBytes = std::get<1>(key);
            grupo.checksumSha256 = std::get<2>(key);

            for (const auto& dbItem : itensGrupo) {
                ParDuplicatas::ItemInfo info;
                info.id = dbItem.itemId;
                info.codigoAcervo = dbItem.codigoAcervo;
                info.titulo = dbItem.titulo;
                info.tipoMidia = dbItem.tipoMidia;
                info.estado = dbItem.estado;
                info.caminhoRelativo = dbItem.caminhoRelativo;
                info.caminhoOrigem = dbItem.caminhoOrigem;
                info.tamanhoBytes = dbItem.tamanhoBytes;
                grupo.itens.push_back(info);
            }
            out.push_back(std::move(grupo));
        }
    }

    return out;
}

void ProjetoAberto::atualizarEstadoItem(const std::string& itemId, const std::string& novoEstado) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();
    projeto_->registro().run("UPDATE item SET estado = ?, atualizado_em = ? WHERE id = ?",
                             {matriz::db::Value::of(novoEstado), matriz::db::Value::of(agora),
                              matriz::db::Value::of(itemId)});
}

int ProjetoAberto::replicarSubarvoreNoAcervo(const NoArvore& origem, const std::string& pastaPaiId,
                                              bool manterEstrutura, const std::string& mapaId) {
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return 0;
    if (!manterEstrutura) {
        std::vector<std::string> ids(origem.itemIds.begin(), origem.itemIds.end());
        if (ids.empty()) return 0;
        std::string destino = pastaPaiId;
        if (destino.empty())
            destino = criarPastaAcervo(origem.nome.toStdString(), std::nullopt, mapaId);
        adicionarItensAPasta(ids, destino);
        return static_cast<int>(ids.size());
    }

    // Manter estrutura (padrão). Uma transação só: replicar um catálogo com
    // milhares de pastas em auto-commit levaria um fsync por INSERT, e uma
    // falha no meio deixaria meia hierarquia montada.
    projeto_->registro().run("BEGIN", {});
    int vinculados = 0;
    try {
        // Pilha explícita em vez de recursão: uma hierarquia de disco pode
        // ser bem funda, e estourar a pilha nativa no meio de uma transação
        // seria o pior lugar possível pra descobrir isso.
        struct Pendente {
            const NoArvore* no;
            std::string pastaPaiId;
        };
        std::vector<Pendente> pilha{{&origem, pastaPaiId}};

        while (!pilha.empty()) {
            Pendente atual = pilha.back();
            pilha.pop_back();

            // A própria pasta arrastada também é recriada no destino — é o
            // que faz "arrastar a pasta X" produzir X lá dentro, e não só o
            // conteúdo dela derramado na pasta de destino.
            std::optional<std::string> pai =
                atual.pastaPaiId.empty() ? std::nullopt : std::optional(atual.pastaPaiId);
            std::string novaPastaId = criarPastaAcervo(atual.no->nome.toStdString(), pai, mapaId);

            if (!atual.no->itemIdsDiretos.empty()) {
                std::string agoraItem = matriz::model::agoraIso8601();
                for (const auto& itemId : atual.no->itemIdsDiretos) {
                    // Só tira o item das pastas DESTE mapa (Fase 1).
                    projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                                             {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
                    inserirItemPastaInterno(itemId, novaPastaId, agoraItem);
                }
                vinculados += static_cast<int>(atual.no->itemIdsDiretos.size());
            }

            for (auto& filho : atual.no->filhos) pilha.push_back({&filho, novaPastaId});
        }
        projeto_->registro().run("COMMIT", {});
    } catch (...) {
        projeto_->registro().run("ROLLBACK", {});
        throw;
    }
    return vinculados;
}

void ProjetoAberto::removerItemDaPasta(const std::string& itemId, const std::string& pastaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND pasta_id = ?",
                              {matriz::db::Value::of(itemId), matriz::db::Value::of(pastaId)});
}

std::set<std::string> ProjetoAberto::buscarItens(const juce::String& texto, EscopoBusca escopo) const {
    std::set<std::string> out;
    if (!projeto_) return out;
    juce::String termo = texto.trim();
    if (termo.isEmpty()) return out;
    std::string projetoId = projeto_->projetoId();
    if (escopo != EscopoBusca::Todos) return buscarItensNoEscopo(termo, escopo);

    juce::StringArray tokens;
    tokens.addTokens(termo, " \t\r\n,", "\"");

    if (tokens.isEmpty()) return out;

    bool primeiroToken = true;

    for (auto token : tokens) {
        token = token.trim().unquoted();
        if (token.isEmpty()) continue;

        std::set<std::string> matchesParaToken;

        // 1. FTS5 search for this token
        juce::String cleanToken = token.trimCharactersAtStart("#").replace("\"", "\"\"");
        if (cleanToken.isNotEmpty()) {
            juce::String ftsQuery = "\"" + cleanToken + "\"*";
            try {
                auto stmt = leitura().prepare(
                    "SELECT DISTINCT b.item_id FROM busca_fts b "
                    "JOIN item i ON i.id = b.item_id "
                    "WHERE i.projeto_id = ? AND busca_fts MATCH ?");
                stmt.bind(1, matriz::db::Value::of(projetoId));
                stmt.bind(2, matriz::db::Value::of(ftsQuery.toStdString()));
                while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
            } catch (...) {}
        }

        // 2. Direct SQL search across all metadata tables and columns (case-insensitive LIKE)
        std::string pattern = "%" + token.replace("%", "\\%").replace("_", "\\_").toStdString() + "%";
        std::string cleanPattern = "%" + token.trimCharactersAtStart("#").replace("%", "\\%").replace("_", "\\_").toStdString() + "%";

        // a) item table columns
        try {
            auto stmt = leitura().prepare(
                "SELECT id FROM item "
                "WHERE projeto_id = ? AND ("
                "   titulo LIKE ? ESCAPE '\\' OR "
                "   codigo_acervo LIKE ? ESCAPE '\\' OR "
                "   persistent_id LIKE ? ESCAPE '\\' OR "
                "   notas_livres LIKE ? ESCAPE '\\' OR "
                "   content_type LIKE ? ESCAPE '\\' OR "
                "   collection_type LIKE ? ESCAPE '\\' OR "
                "   isrc LIKE ? ESCAPE '\\' OR "
                "   tipo_midia LIKE ? ESCAPE '\\' OR "
                "   estado LIKE ? ESCAPE '\\'"
                ")");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            for (int k = 2; k <= 10; ++k) stmt.bind(k, matriz::db::Value::of(pattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // b) item_campo (all metadata fields: artist, creator, description, year, custom YAML fields, etc.)
        try {
            auto stmt = leitura().prepare(
                "SELECT c.item_id FROM item_campo c "
                "JOIN item i ON i.id = c.item_id "
                "WHERE i.projeto_id = ? AND c.valor LIKE ? ESCAPE '\\'");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(cleanPattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // c) item_tag (tags)
        try {
            auto stmt = leitura().prepare(
                "SELECT t.item_id FROM item_tag t "
                "JOIN item i ON i.id = t.item_id "
                "WHERE i.projeto_id = ? AND t.tag LIKE ? ESCAPE '\\'");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(cleanPattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // d) item_observacao (notes)
        try {
            auto stmt = leitura().prepare(
                "SELECT o.item_id FROM item_observacao o "
                "JOIN item i ON i.id = o.item_id "
                "WHERE i.projeto_id = ? AND o.texto LIKE ? ESCAPE '\\'");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(pattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // e) arquivo (filename, relative path, origin absolute path)
        try {
            auto stmt = leitura().prepare(
                "SELECT a.item_id FROM arquivo a "
                "JOIN item i ON i.id = a.item_id "
                "WHERE i.projeto_id = ? AND ("
                "   a.caminho_relativo LIKE ? ESCAPE '\\' OR "
                "   a.caminho_absoluto_origem LIKE ? ESCAPE '\\'"
                ")");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(pattern));
            stmt.bind(3, matriz::db::Value::of(pattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // f) item_assunto / assunto
        try {
            auto stmt = leitura().prepare(
                "SELECT ia.item_id FROM item_assunto ia "
                "JOIN assunto s ON s.id = ia.assunto_id "
                "JOIN item i ON i.id = ia.item_id "
                "WHERE i.projeto_id = ? AND s.termo LIKE ? ESCAPE '\\'");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(pattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // Intersect with overall result (AND logic across multiple words)
        if (primeiroToken) {
            out = std::move(matchesParaToken);
            primeiroToken = false;
        } else {
            std::set<std::string> inter;
            for (const auto& id : out) {
                if (matchesParaToken.count(id)) inter.insert(id);
            }
            out = std::move(inter);
        }

        if (out.empty()) break;
    }

    return out;
}

std::set<std::string> ProjetoAberto::buscarItensNoEscopo(const juce::String& termo, EscopoBusca escopo) const {
    // Um conjunto por palavra, intersectados (E lógico, como a busca geral);
    // cada palavra procurada só nos campos do escopo.
    std::set<std::string> out;
    juce::StringArray tokens;
    tokens.addTokens(termo, " \t\r\n,", "\"");
    tokens.removeEmptyStrings();
    const std::string pid = projeto_->projetoId();
    bool primeiro = true;
    for (auto token : tokens) {
        token = token.trim().unquoted().trimCharactersAtStart("#");
        if (token.isEmpty()) continue;
        const std::string like = "%" + token.replace("%", "\\%").replace("_", "\\_").toStdString() + "%";
        std::vector<std::pair<std::string, std::vector<std::string>>> consultas;  // SQL (1º ? = projeto), padrões
        const std::string doItem = "SELECT i.id FROM item i WHERE i.projeto_id = ? AND (";
        switch (escopo) {
            case EscopoBusca::NomeArquivo:
                consultas.push_back({doItem + "i.titulo LIKE ? ESCAPE '\\' OR i.codigo_acervo LIKE ? ESCAPE '\\')", {like, like}});
                consultas.push_back({"SELECT a.item_id FROM arquivo a JOIN item i ON i.id = a.item_id WHERE i.projeto_id = ? AND "
                                     "(a.caminho_relativo LIKE ? ESCAPE '\\' OR a.caminho_absoluto_origem LIKE ? ESCAPE '\\')", {like, like}});
                break;
            case EscopoBusca::Criador:
                consultas.push_back({doItem + "i.dc_creator LIKE ? ESCAPE '\\')", {like}});
                consultas.push_back({"SELECT c.item_id FROM item_campo c JOIN item i ON i.id = c.item_id WHERE i.projeto_id = ? AND "
                                     "c.campo_id IN ('dc_creator', 'artista_principal', 'creator', 'autor') AND c.valor LIKE ? ESCAPE '\\'", {like}});
                break;
            case EscopoBusca::Assunto:
                consultas.push_back({doItem + "i.dc_subject LIKE ? ESCAPE '\\')", {like}});
                consultas.push_back({"SELECT ia.item_id FROM item_assunto ia JOIN assunto s ON s.id = ia.assunto_id JOIN item i ON i.id = ia.item_id "
                                     "WHERE i.projeto_id = ? AND s.termo LIKE ? ESCAPE '\\'", {like}});
                break;
            case EscopoBusca::Conteudo: {
                // Aceita o nome em português (o banco guarda em inglês).
                const std::string likeEn = "%" + traduzirContent(token, false).toStdString() + "%";
                consultas.push_back({doItem + "i.collection_type LIKE ? ESCAPE '\\' OR i.content_type LIKE ? ESCAPE '\\' "
                                              "OR i.collection_type LIKE ? OR i.content_type LIKE ?)", {like, like, likeEn, likeEn}});
                break;
            }
            case EscopoBusca::PessoasTags:
                consultas.push_back({"SELECT t.item_id FROM item_tag t JOIN item i ON i.id = t.item_id WHERE i.projeto_id = ? AND "
                                     "t.tag LIKE ? ESCAPE '\\'", {like}});
                consultas.push_back({doItem + "i.dc_contributor LIKE ? ESCAPE '\\')", {like}});
                consultas.push_back({"SELECT c.item_id FROM item_campo c JOIN item i ON i.id = c.item_id WHERE i.projeto_id = ? AND "
                                     "(c.campo_id LIKE '%pesso%' OR c.campo_id LIKE '%people%' OR c.campo_id LIKE '%contributor%') "
                                     "AND c.valor LIKE ? ESCAPE '\\'", {like}});
                break;
            case EscopoBusca::Extensao: {
                const std::string ext = "%." + token.trimCharactersAtStart(".").toStdString();
                consultas.push_back({"SELECT a.item_id FROM arquivo a JOIN item i ON i.id = a.item_id WHERE i.projeto_id = ? AND "
                                     "a.caminho_relativo LIKE ?", {ext}});
                break;
            }
            case EscopoBusca::Notas:
                consultas.push_back({doItem + "i.notas_livres LIKE ? ESCAPE '\\')", {like}});
                consultas.push_back({"SELECT o.item_id FROM item_observacao o JOIN item i ON i.id = o.item_id WHERE i.projeto_id = ? AND "
                                     "o.texto LIKE ? ESCAPE '\\'", {like}});
                break;
            case EscopoBusca::Geo: {
                // Termos do endereço ou coordenadas ("-16.44" acha latitude/longitude começando assim).
                const std::string coord = token.toStdString() + "%";
                consultas.push_back({"SELECT g.asset_id FROM asset_geolocation g JOIN item i ON i.id = g.asset_id WHERE i.projeto_id = ? AND ("
                                     "g.city LIKE ?1x OR g.state_province LIKE ?1x OR g.country LIKE ?1x OR g.formatted_address LIKE ?1x "
                                     "OR g.street LIKE ?1x OR g.neighborhood LIKE ?1x OR g.locality LIKE ?1x OR g.municipality LIKE ?1x "
                                     "OR CAST(g.latitude AS TEXT) LIKE ?2x OR CAST(g.longitude AS TEXT) LIKE ?2x)", {like, coord}});
                break;
            }
            case EscopoBusca::Todos:
                break;
        }
        std::set<std::string> achados;
        for (auto& [sqlBruto, padroes] : consultas) {
            try {
                // "?1x"/"?2x": o mesmo padrão repetido — expande pra ?s posicionais.
                std::string sql;
                std::vector<std::string> binds;
                for (size_t k = 0; k < sqlBruto.size(); ++k) {
                    if (sqlBruto[k] == '?' && k + 2 < sqlBruto.size() && sqlBruto[k + 2] == 'x') {
                        binds.push_back(padroes[static_cast<size_t>(sqlBruto[k + 1] - '1')]);
                        sql += '?';
                        k += 2;
                    } else {
                        sql += sqlBruto[k];
                    }
                }
                const bool expandiu = !binds.empty();
                if (!expandiu) binds = padroes;
                auto st = leitura().prepare(sql);
                st.bind(1, matriz::db::Value::of(pid));
                for (size_t k = 0; k < binds.size(); ++k) st.bind(static_cast<int>(k) + 2, matriz::db::Value::of(binds[k]));
                while (st.step()) achados.insert(st.columnText(0));
            } catch (...) {}
        }
        if (primeiro) {
            out = std::move(achados);
            primeiro = false;
        } else {
            std::set<std::string> inter;
            for (const auto& id : out) if (achados.count(id)) inter.insert(id);
            out = std::move(inter);
        }
        if (out.empty()) break;
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorTipoMidia() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare("SELECT tipo_midia, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY tipo_midia");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorEstado() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare("SELECT estado, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY estado");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorExtensao() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT (SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1) "
        "FROM item i WHERE i.projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        if (stmt.columnIsNull(0)) continue;
        juce::String caminho = stmt.columnText(0);
        int dotPos = caminho.lastIndexOfChar('.');
        juce::String ext = dotPos >= 0 ? caminho.substring(dotPos + 1).toLowerCase() : juce::String();
        if (ext.isEmpty()) continue;
        out[ext.toStdString()]++;
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorOrigem() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT (SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'origem') "
        "FROM item i WHERE i.projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnIsNull(0) ? std::string() : stmt.columnText(0)]++;
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorContentType() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT content_type FROM item WHERE projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        std::string v = stmt.columnIsNull(0) ? std::string() : stmt.columnText(0);
        if (!v.empty()) out[v]++;
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorCollectionType() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT collection_type FROM item WHERE projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        std::string v = stmt.columnIsNull(0) ? std::string() : stmt.columnText(0);
        if (!v.empty()) out[v]++;
    }
    return out;
}

std::vector<ProjetoAberto::ColecaoDisponivel> ProjetoAberto::listarColecoesDisponiveis() const {
    std::vector<ColecaoDisponivel> out;
    if (!projeto_) return out;

    std::map<std::string, int> contagens;
    int semColecao = 0;

    auto stmt = leitura().prepare(
        "SELECT collection_type, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY collection_type");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        std::string v = stmt.columnIsNull(0) ? std::string() : stmt.columnText(0);
        int count = stmt.columnInt(1);
        if (v.empty()) {
            semColecao += count;
        } else {
            contagens[v] += count;
        }
    }

    for (const auto& [nome, cnt] : contagens) {
        out.push_back({nome, juce::String(nome), cnt});
    }
    if (semColecao > 0) {
        out.push_back({"Unknown", "Unknown", semColecao});
    }
    return out;
}

std::set<std::string> ProjetoAberto::itensDaColecao(const std::string& chave) const {
    std::set<std::string> out;
    if (!projeto_) return out;

    if (chave == "Unknown" || chave.empty()) {
        auto stmt = leitura().prepare(
            "SELECT id FROM item WHERE projeto_id = ? AND (collection_type IS NULL OR collection_type = '')");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            out.insert(stmt.columnText(0));
        }
    } else {
        auto stmt = leitura().prepare(
            "SELECT id FROM item WHERE projeto_id = ? AND collection_type = ?");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        stmt.bind(2, matriz::db::Value::of(chave));
        while (stmt.step()) {
            out.insert(stmt.columnText(0));
        }
    }
    return out;
}

std::vector<ProjetoAberto::ColecaoLink> ProjetoAberto::listarColecoesLinkadas() const {
    std::vector<ColecaoLink> out;
    if (!projeto_) return out;

    try {
        auto stmt = leitura().prepare(
            "SELECT id, caminho_projeto, nome, IFNULL(grupo, ''), criado_em FROM catalog_colecao_link ORDER BY nome ASC");
        while (stmt.step()) {
            ColecaoLink link;
            link.id = stmt.columnText(0);
            link.caminhoProjeto = juce::String::fromUTF8(stmt.columnText(1).c_str());
            link.nome = juce::String::fromUTF8(stmt.columnText(2).c_str());
            link.grupo = juce::String::fromUTF8(stmt.columnText(3).c_str());
            link.criadoEm = juce::String::fromUTF8(stmt.columnText(4).c_str());

            juce::File pasta(link.caminhoProjeto);
            juce::File dbFile = matriz::model::Project::resolverPastaProjeto(pasta).getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                link.valido = true;
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    auto countStmt = colDb.prepare("SELECT COUNT(*) FROM item");
                    if (countStmt.step()) link.totalAssets = static_cast<uint64_t>(countStmt.columnInt(0));

                    auto sizeStmt = colDb.prepare("SELECT IFNULL(SUM(tamanho_bytes), 0) FROM arquivo");
                    if (sizeStmt.step()) link.totalBytes = static_cast<juce::int64>(sizeStmt.columnInt(0));
                } catch (...) {}
            } else {
                link.valido = false;
            }

            out.push_back(link);
        }
    } catch (...) {}
    return out;
}

bool ProjetoAberto::linkarColecao(const juce::File& pastaProjeto, const juce::String& grupo) {
    if (!projeto_ || !pastaProjeto.isDirectory()) return false;
    juce::File resolvedPasta = matriz::model::Project::resolverPastaProjeto(pastaProjeto);
    juce::File dbFile = resolvedPasta.getChildFile("registro.sqlite");
    if (!dbFile.existsAsFile()) return false;

    juce::String nome = pastaProjeto.getFileName();
    try {
        matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
        auto nameStmt = colDb.prepare("SELECT nome FROM projeto LIMIT 1");
        if (nameStmt.step() && !nameStmt.columnIsNull(0)) {
            juce::String dbNome = juce::String::fromUTF8(nameStmt.columnText(0).c_str());
            if (dbNome.isNotEmpty()) nome = dbNome;
        }
    } catch (...) {}

    std::string id = matriz::model::novoUuid();
    std::string caminho = pastaProjeto.getFullPathName().toStdString();
    std::string nomeStr = nome.toStdString();
    std::string grupoStr = grupo.toStdString();
    std::string agora = juce::Time::getCurrentTime().formatted("%Y-%m-%dT%H:%M:%SZ").toStdString();

    try {
        projeto_->registro().run(
            "INSERT INTO catalog_colecao_link (id, caminho_projeto, nome, grupo, criado_em) "
            "VALUES (?, ?, ?, ?, ?) "
            "ON CONFLICT(caminho_projeto) DO UPDATE SET nome = excluded.nome, grupo = excluded.grupo",
            {matriz::db::Value::of(id),
             matriz::db::Value::of(caminho),
             matriz::db::Value::of(nomeStr),
             matriz::db::Value::of(grupoStr),
             matriz::db::Value::of(agora)});
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjetoAberto::desvincularColecao(const std::string& linkId) {
    if (!projeto_ || linkId.empty()) return false;
    try {
        projeto_->registro().run("DELETE FROM catalog_colecao_link WHERE id = ?", {matriz::db::Value::of(linkId)});
        dirty_ = true;
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjetoAberto::relocarColecaoLink(const std::string& linkId, const juce::File& novaPastaProjeto) {
    if (!projeto_ || linkId.empty() || !novaPastaProjeto.exists()) return false;
    juce::File dbFile = matriz::model::Project::resolverPastaProjeto(novaPastaProjeto).getChildFile("registro.sqlite");
    if (!dbFile.existsAsFile()) return false;

    try {
        std::string caminho = novaPastaProjeto.getFullPathName().toStdString();
        projeto_->registro().run(
            "UPDATE catalog_colecao_link SET caminho_projeto = ? WHERE id = ?",
            {matriz::db::Value::of(caminho), matriz::db::Value::of(linkId)});
        dirty_ = true;
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjetoAberto::atualizarGrupoColecao(const std::string& linkId, const juce::String& novoGrupo) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || linkId.empty()) return false;
    try {
        projeto_->registro().run(
            "UPDATE catalog_colecao_link SET grupo = ? WHERE id = ?",
            {matriz::db::Value::of(novoGrupo.toStdString()), matriz::db::Value::of(linkId)});
        return true;
    } catch (...) {
        return false;
    }
}

std::set<std::string> ProjetoAberto::itensPorFaixaAno(int anoDe, int anoAte) const {
    std::set<std::string> out;
    if (!projeto_) return out;

    // 1. Check user-filled creation fields in item_campo (ano, dc_created, data_criacao)
    try {
        auto stmt = leitura().prepare(
            "SELECT c.item_id, c.valor FROM item_campo c JOIN item i ON i.id = c.item_id "
            "WHERE i.projeto_id = ? AND c.campo_id IN ('ano', 'dc_created', 'data_criacao')");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string val = stmt.columnText(1);
            if (!val.empty()) {
                for (size_t i = 0; i + 3 < val.size(); ++i) {
                    if (std::isdigit(val[i]) && std::isdigit(val[i+1]) && std::isdigit(val[i+2]) && std::isdigit(val[i+3])) {
                        int yr = std::stoi(val.substr(i, 4));
                        if (yr >= anoDe && yr <= anoAte) {
                            out.insert(itemId);
                            break;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    // 2. Direct column ano in item table
    try {
        auto stmt = leitura().prepare(
            "SELECT id FROM item WHERE projeto_id = ? AND ano IS NOT NULL AND ano >= ? AND ano <= ?");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        stmt.bind(2, matriz::db::Value::of(anoDe));
        stmt.bind(3, matriz::db::Value::of(anoAte));
        while (stmt.step()) out.insert(stmt.columnText(0));
    } catch (...) {}

    // 3. EXIF creation date in arquivo.caracteristicas_tecnicas_json
    try {
        auto stmt = leitura().prepare(
            "SELECT a.item_id, a.caracteristicas_tecnicas_json FROM arquivo a JOIN item i ON i.id = a.item_id "
            "WHERE i.projeto_id = ? AND a.eh_master = 1 AND a.caracteristicas_tecnicas_json LIKE '%exifDataOriginal%'");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string jsonStr = stmt.columnText(1);
            auto varObj = juce::JSON::parse(jsonStr);
            if (varObj.isObject() && varObj.hasProperty("exifDataOriginal")) {
                juce::String exifDt = varObj["exifDataOriginal"].toString();
                for (int i = 0; i + 3 < exifDt.length(); ++i) {
                    if (std::isdigit(exifDt[i]) && std::isdigit(exifDt[i+1]) &&
                        std::isdigit(exifDt[i+2]) && std::isdigit(exifDt[i+3])) {
                        int yr = exifDt.substring(i, i + 4).getIntValue();
                        if (yr >= anoDe && yr <= anoAte) {
                            out.insert(itemId);
                            break;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    // 4. Physical file creation/modification date
    try {
        auto stmt = leitura().prepare(
            "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a JOIN item i ON i.id = a.item_id "
            "WHERE i.projeto_id = ? AND a.eh_master = 1 AND a.caminho_absoluto_origem IS NOT NULL AND a.caminho_absoluto_origem != ''");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string absPath = stmt.columnText(1);
            juce::File f(absPath);
            if (f.existsAsFile()) {
                int yr = f.getCreationTime().getYear();
                if (yr <= 1970 || yr > 2025) yr = f.getLastModificationTime().getYear();
                if (yr >= anoDe && yr <= anoAte) {
                    out.insert(itemId);
                }
            }
        }
    } catch (...) {}

    return out;
}

std::vector<ProjetoAberto::ColecaoInteligente> ProjetoAberto::listarColecoes() const {
    std::vector<ColecaoInteligente> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT id, nome, busca_texto, filtros_tipo_midia, filtros_estado, filtros_extensao, filtros_origem, "
        "ano_de, ano_ate, filtros_content_type, filtros_collection_type FROM colecao_inteligente "
        "WHERE projeto_id = ? ORDER BY ordem, criado_em");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        ColecaoInteligente c;
        c.id = stmt.columnText(0);
        c.nome = stmt.columnText(1);
        c.buscaTexto = stmt.columnText(2);
        c.filtrosTipoMidia = conjuntoDeCsv(stmt.columnText(3));
        c.filtrosEstado = conjuntoDeCsv(stmt.columnText(4));
        c.filtrosExtensao = conjuntoDeCsv(stmt.columnText(5));
        c.filtrosOrigem = conjuntoDeCsv(stmt.columnText(6));
        if (!stmt.columnIsNull(7)) c.anoDe = static_cast<int>(stmt.columnInt(7));
        if (!stmt.columnIsNull(8)) c.anoAte = static_cast<int>(stmt.columnInt(8));
        c.filtrosContentType = conjuntoDeCsv(stmt.columnText(9));
        c.filtrosCollectionType = conjuntoDeCsv(stmt.columnText(10));
        out.push_back(std::move(c));
    }
    return out;
}

std::string ProjetoAberto::salvarColecao(const ColecaoInteligente& colecao) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_) return {};
    std::string id = colecao.id.empty() ? matriz::model::novoUuid() : colecao.id;
    std::string agora = matriz::model::agoraIso8601();
    projeto_->registro().run(
        "INSERT INTO colecao_inteligente (id, projeto_id, nome, busca_texto, filtros_tipo_midia, filtros_estado, "
        "filtros_extensao, filtros_origem, filtros_content_type, filtros_collection_type, ano_de, ano_ate, ordem, criado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?) "
        "ON CONFLICT(id) DO UPDATE SET nome = excluded.nome, busca_texto = excluded.busca_texto, "
        "filtros_tipo_midia = excluded.filtros_tipo_midia, filtros_estado = excluded.filtros_estado, "
        "filtros_extensao = excluded.filtros_extensao, filtros_origem = excluded.filtros_origem, "
        "filtros_content_type = excluded.filtros_content_type, filtros_collection_type = excluded.filtros_collection_type, "
        "ano_de = excluded.ano_de, ano_ate = excluded.ano_ate",
        {matriz::db::Value::of(id), matriz::db::Value::of(projeto_->projetoId()),
         matriz::db::Value::of(colecao.nome.toStdString()), matriz::db::Value::of(colecao.buscaTexto.toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosTipoMidia).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosEstado).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosExtensao).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosOrigem).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosContentType).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosCollectionType).toStdString()),
         colecao.anoDe ? matriz::db::Value::of(*colecao.anoDe) : matriz::db::Value::null(),
         colecao.anoAte ? matriz::db::Value::of(*colecao.anoAte) : matriz::db::Value::null(),
         matriz::db::Value::of(agora)});
    return id;
}

void ProjetoAberto::apagarColecao(const std::string& id) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    projeto_->registro().run("DELETE FROM colecao_inteligente WHERE id = ?", {matriz::db::Value::of(id)});
}

bool ProjetoAberto::obterItemInfo(const std::string& itemId, std::string& titulo, std::string& tipoMidia, std::string& codigoAcervo) const {
    if (!projeto_) return false;
    auto stmt = leitura().prepare("SELECT titulo, tipo_midia, codigo_acervo FROM item WHERE id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return false;
    titulo = stmt.columnText(0);
    tipoMidia = stmt.columnIsNull(1) ? "" : stmt.columnText(1);
    codigoAcervo = stmt.columnText(2);
    return true;
}

std::optional<ItemResumo> ProjetoAberto::obterItemResumo(const std::string& itemId) const {
    if (!projeto_ || itemId.empty()) return std::nullopt;
    std::string tit, tipo, cod;
    if (!obterItemInfo(itemId, tit, tipo, cod)) return std::nullopt;

    ItemResumo r;
    r.id = itemId;
    r.titulo = tit;
    r.tipoMidia = tipo;
    r.codigoAcervo = cod;

    auto arq = arquivoPrincipal(itemId);
    if (arq) {
        juce::File f(arq->caminhoAbsoluto);
        r.nomeOriginalArquivo = f.getFileName().toStdString();
        r.extensaoArquivo = f.getFileExtension().toStdString();
        r.caminhoAbsolutoOrigem = arq->caminhoAbsoluto.toStdString();
        r.tamanhoBytes = f.existsAsFile() ? f.getSize() : 0;
    }

    try {
        auto stmt = leitura().prepare(
            "SELECT COALESCE(metadados_editados, 0) != 0 FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) r.metadadosEditados = stmt.columnInt(0) != 0;
    } catch (...) {}

    r.tags = lerTags(itemId);

    // Sem isto marcadoRevisado vinha sempre false — e quem copiava o resumo
    // (MosaicoComponent::atualizarItemEmMemoria) apagava a borda do E.
    r.marcadoRevisado = itemMarcadoRevisado(itemId);
    r.marcadoPublicacao = contemMarcacao(TipoMarcacao::Html, itemId);
    r.marcadoZip = contemMarcacao(TipoMarcacao::Zip, itemId);
    r.marcadoPrint = contemMarcacao(TipoMarcacao::Print, itemId);
    r.marcadoWatermark = contemMarcacao(TipoMarcacao::Watermark, itemId);

    return r;
}

std::set<int> ProjetoAberto::indicesExistentes(const std::string& itemId, const std::string& nivel) const {
    std::set<int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT DISTINCT nivel_indice FROM item_campo WHERE item_id = ? AND nivel = ? ORDER BY nivel_indice");
    stmt.bind(1, matriz::db::Value::of(itemId));
    stmt.bind(2, matriz::db::Value::of(nivel));
    while (stmt.step()) out.insert(static_cast<int>(stmt.columnInt(0)));
    return out;
}

void ProjetoAberto::gravarTipoMidia(const std::string& itemId, const std::string& tipoMidia, const std::string& agora,
                                    bool promoverEstado) {
    auto& registro = projeto_->registro();
    if (promoverEstado) {
        // Classificar É o que move o item de 'novo' pra 'catalogado' (§4): o
        // ingest só o trouxe pra dentro; a decisão de que tipo de mídia é isto
        // é humana. Estados posteriores (revisado/aprovado/publicado) não são
        // sobrescritos — reclassificar um item já aprovado não o rebaixa.
        registro.run(
            "UPDATE item SET tipo_midia = ?, atualizado_em = ?, "
            "estado = CASE WHEN estado IN ('novo', 'capturado', 'nao_digitalizado') THEN 'catalogado' ELSE estado END "
            "WHERE id = ?",
            {matriz::db::Value::of(tipoMidia), matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
    } else {
        registro.run("UPDATE item SET tipo_midia = ?, atualizado_em = ? WHERE id = ?",
                     {matriz::db::Value::of(tipoMidia), matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
    }

    auto origem = matriz::ficha::origemPadraoParaTipo(tipoMidia);
    if (origem) {
        registro.run(
            "INSERT OR IGNORE INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'raiz', 0, 'origem', ?, 'leitura_tecnica', ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(*origem), matriz::db::Value::of(agora)});
    }
}

void ProjetoAberto::atualizarTipoMidia(const std::string& itemId, const std::string& tipoMidia) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    if (!desfazendo_) {
        auto stmt = projeto_->registro().prepare("SELECT tipo_midia FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        std::string oldTipo = stmt.step() ? stmt.columnText(0) : "";
        registrarUndo("Change Media Type", [this, itemId, oldTipo]() {
            atualizarTipoMidia(itemId, oldTipo);
        });
    }
    gravarTipoMidia(itemId, tipoMidia, matriz::model::agoraIso8601(), /*promoverEstado*/ true);

    EventBus::obterInstancia().dispararItemAlterado(itemId, "classificacao");
}

void ProjetoAberto::restaurarTiposMidia(const std::map<std::string, std::string>& tiposPorItem) {
    if (!projeto_) return;
    // Mesma escrita de atualizarTipoMidia() (o desfazer sempre foi por ela), mas
    // numa transação só e com um evento de lote.
    const std::string agora = matriz::model::agoraIso8601();
    auto& registro = projeto_->registro();
    registro.run("BEGIN", {});
    try {
        for (const auto& [id, tipo] : tiposPorItem) gravarTipoMidia(id, tipo, agora, /*promoverEstado*/ true);
        registro.run("COMMIT", {});
    } catch (...) {
        registro.run("ROLLBACK", {});
        throw;
    }
    std::vector<std::string> ids;
    ids.reserve(tiposPorItem.size());
    for (const auto& par : tiposPorItem) ids.push_back(par.first);
    EventBus::obterInstancia().dispararItensAlterados(ids, "classificacao");
}

void ProjetoAberto::aplicarTipoMidiaEmLote(const std::vector<std::string>& itemIds, const std::string& tipoMidia,
                                           bool promoverEstado) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    if (!desfazendo_) {
        std::map<std::string, std::string> antigosTipos;
        for (const auto& id : itemIds) {
            auto stmt = projeto_->registro().prepare("SELECT tipo_midia FROM item WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(id));
            if (stmt.step()) antigosTipos[id] = stmt.columnText(0);
        }
        registrarUndo("Batch Change Media Type", [this, antigosTipos]() {
            restaurarTiposMidia(antigosTipos);
        });
    }
    std::string agora = matriz::model::agoraIso8601();
    auto& registro = projeto_->registro();
    registro.run("BEGIN", {});
    try {
        for (auto& id : itemIds) gravarTipoMidia(id, tipoMidia, agora, promoverEstado);
        registro.run("COMMIT", {});
    } catch (...) {
        registro.run("ROLLBACK", {});
        throw;
    }

    EventBus::obterInstancia().dispararItensAlterados(itemIds, "classificacao");
}

void ProjetoAberto::obterTiposMidiaDosItens(const std::vector<std::string>& itemIds, std::set<std::string>& tiposPresentes, bool& algumNulo) const {
    algumNulo = false;
    if (!projeto_) return;
    for (auto& id : itemIds) {
        auto stmt = leitura().prepare("SELECT tipo_midia FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(id));
        if (!stmt.step()) continue;
        if (stmt.columnIsNull(0)) algumNulo = true;
        else tiposPresentes.insert(stmt.columnText(0));
    }
}

} // namespace matriz::ui

namespace matriz::ui {

std::vector<ProjetoAberto::VaultResumo> ProjetoAberto::listarVaults() const {
    std::vector<VaultResumo> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT v.id, v.nome, v.localizacao, v.status, "
        "(SELECT COUNT(DISTINCT a.item_id) FROM arquivo a WHERE a.vault_id = v.id) "
        "FROM vault v "
        "WHERE v.projeto_id = ? "
        "  AND v.localizacao NOT LIKE '/System/Volumes/%' "
        "  AND v.localizacao NOT IN ('/dev', '/net', '/home') "
        "  AND v.nome NOT IN ('dev', 'Preboot', 'Update', 'VM', 'xarts', 'Hardware') "
        "ORDER BY v.nome");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        VaultResumo v;
        v.id = stmt.columnText(0);
        v.nome = juce::String(stmt.columnText(1));
        v.localizacao = juce::String(stmt.columnText(2));
        v.online = stmt.columnText(3) == "online";
        v.totalItens = static_cast<int>(stmt.columnInt(4));
        out.push_back(std::move(v));
    }
    return out;
}

ProjetoAberto::SituacaoMain ProjetoAberto::normalizarPapelMain() {
    SituacaoMain sit;
    if (!projeto_) return sit;
    auto& db = projeto_->registro();
    struct Linha { std::string id, destinationId, papel; juce::String rotulo, caminho; };
    std::vector<Linha> linhas;
    try {
        auto st = db.prepare("SELECT id, COALESCE(destination_id, id), COALESCE(papel, 'CLONE'), rotulo, destino_path "
                             "FROM backup_destino WHERE ativo = 1 ORDER BY criado_em ASC");
        while (st.step())
            linhas.push_back({st.columnText(0), st.columnText(1), st.columnText(2),
                              juce::String::fromUTF8(st.columnText(3).c_str()),
                              juce::String::fromUTF8(st.columnText(4).c_str())});
    } catch (...) { return sit; }
    if (linhas.empty()) return sit;  // projeto sem backup: continua como hoje

    std::vector<const Linha*> mains;
    for (auto& l : linhas) if (l.papel == "ORIGINAL") mains.push_back(&l);
    if (mains.size() == 1) { sit.tipo = SituacaoMain::Tipo::Ok; return sit; }

    if (mains.size() > 1) {
        const std::string raizId = projeto_->destinationId();
        for (auto* m : mains) {
            if (!raizId.empty() && m->destinationId == raizId) {
                definirMain(m->id);
                sit.tipo = SituacaoMain::Tipo::Ok;
                return sit;
            }
        }
    }
    sit.tipo = SituacaoMain::Tipo::Perguntar;
    for (auto& l : linhas) sit.opcoes.push_back({l.id, l.rotulo + juce::String::fromUTF8(" \xe2\x80\x94 ") + l.caminho});
    return sit;
}

void ProjetoAberto::definirMain(const std::string& backupDestinoId) {
    if (!projeto_ || backupDestinoId.empty()) return;
    auto& db = projeto_->registro();
    juce::String rotulo;
    try {
        db.run("BEGIN IMMEDIATE", {});
        db.run("UPDATE backup_destino SET papel = CASE WHEN id = ? THEN 'ORIGINAL' ELSE 'CLONE' END WHERE ativo = 1",
               {matriz::db::Value::of(backupDestinoId)});
        auto st = db.prepare("SELECT rotulo FROM backup_destino WHERE id = ?");
        st.bind(1, matriz::db::Value::of(backupDestinoId));
        if (st.step()) rotulo = juce::String::fromUTF8(st.columnText(0).c_str());
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        return;
    }
    try {
        matriz::model::ProjectLog(projeto_->pasta()).appendEntry(
            "MAIN assigned", {"Version: " + rotulo, "Other registered versions are CLONE (labels only, nothing copied)"});
    } catch (...) {}
}

int ProjetoAberto::arquivosQueDependemDoSource() {
    if (!projeto_) return 0;
    auto& db = projeto_->registro();
    try {
        std::string mainId = projeto_->destinationId();
        auto sm = db.prepare("SELECT COALESCE(destination_id, id) FROM backup_destino WHERE ativo = 1 AND papel = 'ORIGINAL' LIMIT 1");
        if (sm.step()) mainId = sm.columnText(0);
        auto st = db.prepare(
            "SELECT COUNT(*) FROM arquivo a JOIN item i ON i.id = a.item_id "
            "WHERE COALESCE(i.em_quarentena, 0) = 0 AND a.eh_master = 1 AND NOT EXISTS ("
            "  SELECT 1 FROM consolidacao_registro c WHERE c.arquivo_id = a.id "
            "  AND (COALESCE(c.destino_id, '') = '' OR c.destino_id = ?))");
        st.bind(1, matriz::db::Value::of(mainId));
        if (st.step()) return static_cast<int>(st.columnInt(0));
    } catch (...) {}
    return 0;
}

std::vector<ProjetoAberto::VersaoResumo> ProjetoAberto::listarVersoes() {
    std::vector<VersaoResumo> out;
    if (!projeto_) return out;
    sincronizarBackupDestinoDeHistorico();
    auto& db = projeto_->registro();
    const int64_t revisaoProjeto = projeto_->revisao();
    std::string mainDestinationId;

    // MAIN e CLONEs.
    try {
        auto st = db.prepare(
            "SELECT id, COALESCE(destination_id, id), COALESCE(papel, 'CLONE'), rotulo, destino_path, "
            "COALESCE(ultima_revisao_conhecida, 0), COALESCE(ultimo_visto_em, '') FROM backup_destino WHERE ativo = 1 "
            "ORDER BY criado_em ASC");
        auto raizes = matriz::vault::destinosDeBackup(db, projeto_->pasta());
        while (st.step()) {
            VersaoResumo v;
            v.id = st.columnText(0);
            const std::string destId = st.columnText(1);
            v.papel = st.columnText(2) == "ORIGINAL" ? VersaoResumo::Papel::Main : VersaoResumo::Papel::Clone;
            v.rotulo = juce::String::fromUTF8(st.columnText(3).c_str());
            v.caminho = juce::String::fromUTF8(st.columnText(4).c_str());
            // Raiz atual pelo destination_id (a pasta aberta pode ser este destino
            // montado com outro nome).
            juce::File raiz(v.caminho);
            for (auto& d : raizes) if (d.destinationId == destId) raiz = d.raiz;
            v.caminho = raiz.getFullPathName();
            v.online = raiz.isDirectory();
            if (v.papel == VersaoResumo::Papel::Main) {
                mainDestinationId = destId;
            } else {
                v.desatualizado = st.columnInt(5) < revisaoProjeto;
                v.ultimaData = juce::String::fromUTF8(st.columnText(6).c_str());
                v.origem = "MAIN";
            }
            // Registros deste destino (por id ou caminho); os legados (sem destino)
            // contam pro MAIN.
            auto stats = db.prepare(
                "SELECT COUNT(DISTINCT item_id), MAX(consolidado_em) FROM consolidacao_registro "
                "WHERE (destino_id != '' AND destino_id = ?) OR destino_path = ? OR destino_path = ? "
                "   OR (? = 1 AND COALESCE(destino_id, '') = '' AND COALESCE(destino_path, '') = '')");
            stats.bind(1, matriz::db::Value::of(destId));
            stats.bind(2, matriz::db::Value::of(st.columnText(4)));
            stats.bind(3, matriz::db::Value::of(raiz.getChildFile("Media").getFullPathName().toStdString()));
            stats.bind(4, matriz::db::Value::of(v.papel == VersaoResumo::Papel::Main ? 1 : 0));
            if (stats.step()) {
                v.totalItens = static_cast<int>(stats.columnInt(0));
                if (v.papel == VersaoResumo::Papel::Main && !stats.columnIsNull(1))
                    v.ultimaData = juce::String::fromUTF8(stats.columnText(1).c_str());
            }
            out.push_back(std::move(v));
        }
    } catch (...) {}

    // SOURCEs: volumes (tabela vault) de onde vieram arquivos. Uma pasta de
    // origem que é um destino de backup não é SOURCE.
    try {
        auto st = db.prepare(
            "SELECT v.id, v.nome, v.localizacao, COUNT(a.id), MAX(a.criado_em), "
            "SUM(CASE WHEN NOT EXISTS (SELECT 1 FROM consolidacao_registro c WHERE c.arquivo_id = a.id "
            "      AND (COALESCE(c.destino_id, '') = '' OR c.destino_id = ?)) THEN 1 ELSE 0 END), "
            "COUNT(DISTINCT substr(a.criado_em, 1, 10)), "
            "EXISTS (SELECT 1 FROM consolidacao_registro c JOIN arquivo a2 ON a2.id = c.arquivo_id WHERE a2.vault_id = v.id) "
            "FROM vault v JOIN arquivo a ON a.vault_id = v.id GROUP BY v.id ORDER BY MIN(a.criado_em)");
        st.bind(1, matriz::db::Value::of(mainDestinationId));
        const auto codigos = matriz::consolidacao::codigosDeSource(db);
        while (st.step()) {
            VersaoResumo v;
            v.papel = VersaoResumo::Papel::Source;
            v.id = st.columnText(0);
            v.rotulo = juce::String::fromUTF8(st.columnText(1).c_str());
            v.caminho = juce::String::fromUTF8(st.columnText(2).c_str());
            bool ehDestino = false;
            for (auto& d : out)
                if (d.papel != VersaoResumo::Papel::Source &&
                    (juce::File(v.caminho) == juce::File(d.caminho) || juce::File(v.caminho).isAChildOf(juce::File(d.caminho))))
                    ehDestino = true;
            if (ehDestino) continue;
            v.online = juce::File(v.caminho).isDirectory();
            v.totalItens = static_cast<int>(st.columnInt(3));
            v.ultimaData = juce::String::fromUTF8(st.columnText(4).c_str());
            v.dependentes = static_cast<int>(st.columnInt(5));
            v.ingestoes = static_cast<int>(st.columnInt(6));
            v.codigoEditavel = st.columnInt(7) == 0;
            if (auto it = codigos.find(v.id); it != codigos.end()) v.codigo = juce::String(it->second);
            out.push_back(std::move(v));
        }
    } catch (...) {}

    // Clones brutos de SOURCE (etapa 7): também são CLONE, com a origem.
    try {
        db.exec("CREATE TABLE IF NOT EXISTS source_clone ("
                "  id TEXT PRIMARY KEY, vault_id TEXT NOT NULL, origem_path TEXT NOT NULL, destino_path TEXT NOT NULL,"
                "  rotulo TEXT NOT NULL DEFAULT '', criado_em TEXT NOT NULL, ultima_sync_em TEXT, arquivos INTEGER NOT NULL DEFAULT 0)");
        const auto codigos = matriz::consolidacao::codigosDeSource(db);
        auto st = db.prepare("SELECT c.id, c.rotulo, c.destino_path, COALESCE(c.ultima_sync_em, ''), c.arquivos, c.vault_id, "
                             "COALESCE(v.nome, '') FROM source_clone c LEFT JOIN vault v ON v.id = c.vault_id "
                             "ORDER BY c.criado_em");
        while (st.step()) {
            VersaoResumo v;
            v.papel = VersaoResumo::Papel::Clone;
            v.cloneDeSource = true;
            v.id = st.columnText(0);
            v.rotulo = juce::String::fromUTF8(st.columnText(1).c_str());
            v.caminho = juce::String::fromUTF8(st.columnText(2).c_str());
            v.online = juce::File(v.caminho).isDirectory();
            v.ultimaData = juce::String::fromUTF8(st.columnText(3).c_str());
            v.totalItens = static_cast<int>(st.columnInt(4));
            auto it = codigos.find(st.columnText(5));
            v.origem = (it != codigos.end() ? juce::String(it->second) + " " : juce::String()) +
                       juce::String::fromUTF8(st.columnText(6).c_str());
            out.push_back(std::move(v));
        }
    } catch (...) {}

    // MAIN sempre no topo; depois CLONEs; depois SOURCEs.
    std::stable_sort(out.begin(), out.end(), [](const VersaoResumo& a, const VersaoResumo& b) {
        return static_cast<int>(a.papel) < static_cast<int>(b.papel);
    });
    return out;
}

void ProjetoAberto::sincronizarBackupDestinoDeHistorico() {
    if (!projeto_) return;
    auto& db = projeto_->registro();
    // Roda a cada visita à aba BACKUP: só grava (e só marca o projeto como
    // sujo — Database::run() sempre marca) quando há algo a mudar de fato.
    auto destinoJaRegistrado = [&db](const std::string& caminho) {
        auto st = db.prepare("SELECT 1 FROM backup_destino WHERE destino_path = ? LIMIT 1");
        st.bind(1, matriz::db::Value::of(caminho));
        return st.step();
    };
    try {
        // Se houver registros legados com destino_path vazio e existir a pasta padrão <projeto>/Backup com arquivos
        juce::File pastaBackupPadrao = projeto_->pasta().getChildFile("Backup");
        if (pastaBackupPadrao.isDirectory()) {
            std::string pathPadrao = pastaBackupPadrao.getFullPathName().toStdString();
            bool temLegado = false;
            {
                auto stmtLegado = db.prepare(
                    "SELECT 1 FROM consolidacao_registro WHERE destino_path = '' OR destino_path IS NULL LIMIT 1");
                temLegado = stmtLegado.step();
            }
            if (temLegado)
                db.run("UPDATE consolidacao_registro SET destino_path = ? WHERE destino_path = '' OR destino_path IS NULL",
                       {matriz::db::Value::of(pathPadrao)});

            auto stmtCount = db.prepare("SELECT COUNT(*) FROM consolidacao_registro WHERE destino_path = ?");
            stmtCount.bind(1, matriz::db::Value::of(pathPadrao));
            if (stmtCount.step() && stmtCount.columnInt(0) > 0 && !destinoJaRegistrado(pathPadrao)) {
                std::string agora = matriz::model::agoraIso8601();
                db.run("INSERT OR IGNORE INTO backup_destino (id, destino_path, rotulo, ativo, criado_em) VALUES (?, ?, ?, 1, ?)",
                       {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(pathPadrao),
                        matriz::db::Value::of("Backup"), matriz::db::Value::of(agora)});
            }
        }

        // Verificar se há vaults com histórico em consolidacao_registro
        auto stmtVaults = db.prepare("SELECT nome, localizacao FROM vault WHERE localizacao IS NOT NULL AND localizacao != ''");
        while (stmtVaults.step()) {
            std::string vNome = stmtVaults.columnText(0);
            std::string vLoc = stmtVaults.columnText(1);
            juce::File fVault(vLoc);
            if (fVault.isDirectory()) {
                auto sCheck = db.prepare("SELECT COUNT(*) FROM consolidacao_registro WHERE destino_path = ?");
                sCheck.bind(1, matriz::db::Value::of(vLoc));
                if (sCheck.step() && sCheck.columnInt(0) > 0 && !destinoJaRegistrado(vLoc)) {
                    std::string agora = matriz::model::agoraIso8601();
                    db.run("INSERT OR IGNORE INTO backup_destino (id, destino_path, rotulo, ativo, criado_em) VALUES (?, ?, ?, 1, ?)",
                           {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(vLoc),
                            matriz::db::Value::of(vNome), matriz::db::Value::of(agora)});
                }
            }
        }

        // Buscar outros destinos existentes no histórico de consolidação
        auto stmtAuto = db.prepare(
            "SELECT DISTINCT destino_path FROM consolidacao_registro "
            "WHERE destino_path != '' AND destino_path NOT IN (SELECT destino_path FROM backup_destino)");
        std::vector<std::string> novosDestinos;
        while (stmtAuto.step()) {
            novosDestinos.push_back(stmtAuto.columnText(0));
        }

        for (const auto& dp : novosDestinos) {
            juce::File f(dp);
            if (f.getFileName().equalsIgnoreCase("Media") || f.getFileName().equalsIgnoreCase("Project")) {
                continue; // Não registrar subpastas Media/Project como destino
            }
            std::string rotulo = f.getFileName().toStdString();
            if (rotulo.empty()) rotulo = "Backup Destination";
            std::string agora = matriz::model::agoraIso8601();
            db.run("INSERT OR IGNORE INTO backup_destino (id, destino_path, rotulo, ativo, criado_em) VALUES (?, ?, ?, 1, ?)",
                   {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(dp),
                    matriz::db::Value::of(rotulo), matriz::db::Value::of(agora)});
        }
    } catch (...) {}
}

std::vector<ProjetoAberto::ColecaoEmbutida> ProjetoAberto::listarColecoesEmbutidas() const {
    if (!projeto_) return {};

    static const std::vector<std::pair<const char*, const char*>> kEmbutidas = {
        {"clipping", "colecoes.clipping"},       {"ausentes", "colecoes.ausentes"},
        {"nao_baixados", "colecoes.nao_baixados"}, {"incompletos", "colecoes.incompletos"},
        {"vulneraveis", "colecoes.vulneraveis"}, {"revisao", "colecoes.revisao"}};

    std::map<std::string, int> contagens;
    try {
        auto stmt = leitura().prepare(
            "SELECT colecao, COUNT(DISTINCT item_id) FROM colecao_embutida GROUP BY colecao");
        while (stmt.step()) contagens[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    } catch (const std::exception&) {
        // Projeto antigo reaberto antes das views existirem: a seção some,
        // o resto do painel continua.
        return {};
    }

    std::vector<ColecaoEmbutida> out;
    for (const auto& [chave, chaveI18n] : kEmbutidas) {
        ColecaoEmbutida c;
        c.chave = chave;
        c.rotulo = matriz::i18n::t(chaveI18n);
        c.contagem = contagens.count(chave) ? contagens[chave] : 0;
        out.push_back(std::move(c));
    }
    return out;
}

std::set<std::string> ProjetoAberto::itensDaColecaoEmbutida(const std::string& chave) const {
    std::set<std::string> out;
    if (!projeto_) return out;
    // Fase 4: não é view do schema (bancos antigos não a teriam) — vem direto
    // do histórico de merge.
    if (chave == "merge_conflitos") return matriz::model::merge::itensComConflitoPendente(leitura());
    try {
        auto stmt = leitura().prepare("SELECT DISTINCT item_id FROM colecao_embutida WHERE colecao = ?");
        stmt.bind(1, matriz::db::Value::of(chave));
        while (stmt.step()) out.insert(stmt.columnText(0));
    } catch (const std::exception&) {
    }
    return out;
}

std::vector<std::string> ProjetoAberto::reavaliarVaults() {
    if (!projeto_) return {};
    try {
        bool mudou = false;
        auto reconectados = matriz::vault::reavaliarVaults(projeto_->registro(), &mudou);
        if (mudou) vaultsMudaram_.store(true);
        return reconectados;
    } catch (const std::exception&) {
        return {};
    }
}

// ---------------------------------------------------------------------------
// Camada de Preservação Digital (OAIS / PREMIS / FAIR)
// ---------------------------------------------------------------------------

preservation::PreservationStatus ProjetoAberto::obterPreservationStatus(const std::string& itemId) const {
    if (!projeto_) return {};
    try { return preservation::obterStatus(projeto_->registro(), itemId); }
    catch (...) { return {}; }
}

std::vector<preservation::EventoPreservacao> ProjetoAberto::listarEventosPreservacao(const std::string& itemId) const {
    if (!projeto_) return {};
    try { return preservation::listarEventos(projeto_->registro(), itemId); }
    catch (...) { return {}; }
}

std::optional<preservation::DireitosPreservacao> ProjetoAberto::obterDireitos(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    try { return preservation::obterDireitos(projeto_->registro(), itemId); }
    catch (...) { return std::nullopt; }
}

void ProjetoAberto::salvarDireitos(const std::string& itemId, const preservation::DireitosPreservacao& d) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    try {
        preservation::salvarDireitos(projeto_->registro(), itemId, d, "bkr-agent-sistema");
    } catch (...) {}
}

void ProjetoAberto::verificarFixityAsync(const std::string& arquivoId,
                                         const std::string& caminhoAbsoluto,
                                         std::function<void(preservation::ResultadoFixity)> callback) {
    if (!projeto_ || !callback) return;

    // Captura o caminho do arquivo do projeto para abrir uma segunda conexão
    // na thread de background (a conexão principal pertence à message thread).
    juce::File registroFile = projeto_->pasta().getChildFile("registro.sqlite");

    std::thread([registroFile, arquivoId, caminhoAbsoluto, callback = std::move(callback)]() mutable {
        preservation::ResultadoFixity resultado;
        try {
            db::Database db(registroFile.getFullPathName().toStdString());
            resultado = preservation::verificarFixity(db, arquivoId, caminhoAbsoluto, "bkr-agent-sistema");
        } catch (const std::exception& e) {
            resultado.success  = false;
            resultado.mensagem = std::string("Erro ao verificar: ") + e.what();
        }
        // Devolve resultado na message thread
        juce::MessageManager::callAsync([resultado, callback = std::move(callback)]() mutable {
            callback(resultado);
        });
    }).detach();
}

juce::String ProjetoAberto::exportarPreservacaoJson(const std::string& itemId) const {
    if (!projeto_) return "{}";
    try { return preservation::exportarJson(projeto_->registro(), itemId); }
    catch (...) { return "{}"; }
}

juce::String ProjetoAberto::exportarPreservacaoCsv(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto colecoes = listarColecoesLinkadas();
        juce::String csv;
        bool headerWritten = false;

        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = colDir.getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    std::vector<std::string> ids;
                    auto stmt = colDb.prepare("SELECT id FROM item ORDER BY codigo_acervo ASC, id ASC");
                    while (stmt.step()) ids.push_back(stmt.columnText(0));

                    juce::String part = preservation::exportarCsv(colDb, ids);
                    if (!headerWritten) {
                        csv += part;
                        headerWritten = true;
                    } else {
                        int newlinePos = part.indexOfChar('\n');
                        if (newlinePos >= 0) csv += part.substring(newlinePos + 1);
                    }
                } catch (...) {}
            }
        }
        if (csv.isEmpty()) {
            return preservation::exportarCsv(projeto_->registro(), itemIds);
        }
        return csv;
    }
    try { return preservation::exportarCsv(projeto_->registro(), itemIds); }
    catch (...) { return {}; }
}

juce::String ProjetoAberto::exportarFullCsv(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto colecoes = listarColecoesLinkadas();
        juce::String csv;
        bool headerWritten = false;

        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = colDir.getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    std::vector<std::string> ids;
                    auto stmt = colDb.prepare("SELECT id FROM item ORDER BY codigo_acervo ASC, id ASC");
                    while (stmt.step()) ids.push_back(stmt.columnText(0));

                    juce::String part = preservation::exportarFullCsv(colDb, ids);
                    if (!headerWritten) {
                        csv += part;
                        headerWritten = true;
                    } else {
                        int newlinePos = part.indexOfChar('\n');
                        if (newlinePos >= 0) csv += part.substring(newlinePos + 1);
                    }
                } catch (...) {}
            }
        }
        if (csv.isEmpty()) {
            return preservation::exportarFullCsv(projeto_->registro(), itemIds);
        }
        return csv;
    }
    try { return preservation::exportarFullCsv(projeto_->registro(), itemIds); }
    catch (...) { return {}; }
}

bool ProjetoAberto::exportarFullCsvPacote(const std::vector<std::string>& itemIds, const juce::File& destLocation, juce::String& errorOut) const {
    if (!projeto_) {
        errorOut = "No active project open";
        return false;
    }
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        try {
            juce::File pkgDir = (destLocation.isDirectory() && destLocation.getFileName() == "BKR_Full_Export")
                                    ? destLocation
                                    : destLocation.getChildFile("BKR_Full_Export");
            if (!pkgDir.exists()) pkgDir.createDirectory();
            juce::File csvFile = pkgDir.getChildFile("BKR_FULL.csv");
            juce::String csvContent = exportarFullCsv(itemIds);
            csvFile.replaceWithText(csvContent, false, false, "\n");

            juce::File schemaFile = pkgDir.getChildFile("BKR_FULL.schema.json");
            schemaFile.replaceWithText(preservation::gerarFullCsvSchemaJson(), false, false, "\n");

            juce::MemoryBlock block;
            csvFile.loadFileAsData(block);
            juce::SHA256 sha(block.getData(), block.getSize());
            juce::String csvSha256 = sha.toHexString();

            juce::StringArray lines;
            lines.addLines(csvContent);
            int assetCount = 0;
            for (int i = 1; i < lines.size(); ++i) {
                if (lines[i].trim().isNotEmpty()) assetCount++;
            }

            juce::File manifestFile = pkgDir.getChildFile("manifest.json");
            juce::String manifestContent = preservation::gerarFullCsvManifestJson(assetCount, csvSha256);
            manifestFile.replaceWithText(manifestContent, false, false, "\n");

            auto valRes = preservation::validarFullCsvFile(csvFile, assetCount);
            if (!valRes.valid) {
                errorOut = valRes.error;
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            errorOut = e.what();
            return false;
        } catch (...) {
            errorOut = "Unknown error during export";
            return false;
        }
    }
    try { return preservation::exportarFullCsvPacote(projeto_->registro(), itemIds, destLocation, errorOut); }
    catch (const std::exception& e) { errorOut = e.what(); return false; }
    catch (...) { errorOut = "Unknown error during export"; return false; }
}

juce::String ProjetoAberto::exportarXlsXml(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    std::string projName = projeto_->nome();
    std::string catCode = "BKR-MATRIZ-01";

    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto escHtml = [](const std::string& str) -> juce::String {
            juce::String s(str);
            return s.replace("&", "&amp;")
                    .replace("<", "&lt;")
                    .replace(">", "&gt;")
                    .replace("\"", "&quot;")
                    .replace("'", "&apos;");
        };

        auto colecoes = listarColecoesLinkadas();
        std::string dateNow = matriz::model::agoraIso8601();

        juce::int64 totalBytes = 0;
        int totalAssets = 0;

        struct ItemRow {
            std::string title, creator, subject, description, publisher, contributor;
            std::string created, issued, type, format, identifier, source, language;
            std::string relation, coverage, rights;
        };
        std::vector<ItemRow> allRows;

        auto processDb = [&](matriz::db::Database& db, const std::vector<std::string>& ids) {
            std::vector<std::string> itemIdsList = ids;
            if (itemIdsList.empty()) {
                try {
                    auto stmt = db.prepare("SELECT id FROM item ORDER BY codigo_acervo ASC, id ASC");
                    while (stmt.step()) itemIdsList.push_back(stmt.columnText(0));
                } catch (...) {}
            }

            for (const auto& id : itemIdsList) {
                try {
                    auto stmt = db.prepare("SELECT IFNULL(tamanho_bytes,0) FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
                    stmt.bind(1, matriz::db::Value::of(id));
                    if (stmt.step()) totalBytes += stmt.columnInt(0);
                } catch (...) {}

                auto lerCampo = [&](const std::string& itemId, const std::string& campo) -> std::string {
                    try {
                        auto stmt = db.prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
                        stmt.bind(1, matriz::db::Value::of(itemId));
                        stmt.bind(2, matriz::db::Value::of(campo));
                        if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
                    } catch (...) {}
                    return "";
                };

                ItemRow row;
                row.title = lerCampo(id, "dc_title");
                row.creator = lerCampo(id, "dc_creator");
                row.subject = lerCampo(id, "dc_subject");
                row.description = lerCampo(id, "dc_description");
                row.publisher = lerCampo(id, "dc_publisher");
                row.contributor = lerCampo(id, "dc_contributor");
                row.created = lerCampo(id, "dc_created");
                row.issued = lerCampo(id, "dc_issued");
                row.type = lerCampo(id, "dc_type");
                row.format = lerCampo(id, "dc_format");
                row.identifier = lerCampo(id, "dc_identifier");
                row.source = lerCampo(id, "dc_source");
                row.language = lerCampo(id, "dc_language");
                row.relation = lerCampo(id, "dc_relation");
                row.coverage = lerCampo(id, "dc_coverage");
                row.rights = lerCampo(id, "dc_rights");

                try {
                    auto stmt = db.prepare(
                        "SELECT IFNULL(persistent_id,''), IFNULL(titulo,''), IFNULL(tipo_midia,''), criado_em FROM item WHERE id = ? LIMIT 1");
                    stmt.bind(1, matriz::db::Value::of(id));
                    if (stmt.step()) {
                        if (row.identifier.empty()) row.identifier = stmt.columnText(0);
                        if (row.title.empty()) row.title = stmt.columnText(1);
                        if (row.format.empty()) row.format = stmt.columnText(2);
                        if (row.created.empty()) row.created = stmt.columnText(3);
                    }
                } catch (...) {}

                allRows.push_back(row);
                totalAssets++;
            }
        };

        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = matriz::model::Project::resolverPastaProjeto(colDir).getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    processDb(colDb, {});
                } catch (...) {}
            }
        }

        // If no linked collections or if root db has items as well
        if (allRows.empty()) {
            processDb(projeto_->registro(), itemIds);
        }

        juce::String html;
        html += "<html xmlns:o=\"urn:schemas-microsoft-com:office:office\"\n"
                "xmlns:x=\"urn:schemas-microsoft-com:office:excel\"\n"
                "xmlns=\"http://www.w3.org/TR/REC-html40\">\n"
                "<head>\n"
                "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
                "<meta name=\"ProgId\" content=\"Excel.Sheet\">\n"
                "<meta name=\"Generator\" content=\"BKR Matriz Archival Engine\">\n"
                "<!--[if gte mso 9]><xml>\n"
                " <x:ExcelWorkbook>\n"
                "  <x:ExcelWorksheets>\n"
                "   <x:ExcelWorksheet>\n"
                "    <x:Name>Dublin Core Catalog</x:Name>\n"
                "    <x:WorksheetOptions>\n"
                "     <x:DisplayGridlines/>\n"
                "    </x:WorksheetOptions>\n"
                "   </x:ExcelWorksheet>\n"
                "  </x:ExcelWorksheets>\n"
                " </x:ExcelWorkbook>\n"
                "</xml><![endif]-->\n"
                "<style>\n"
                "  body { font-family: 'Segoe UI', Arial, sans-serif; background-color: #F8FAFC; margin: 0; padding: 20px; }\n"
                "  .hdr-table { width: 100%; border-collapse: collapse; margin-bottom: 20px; font-family: 'Segoe UI', Arial, sans-serif; }\n"
                "  .hdr-main { background-color: #0F172A; color: #38BDF8; font-size: 16px; font-weight: bold; padding: 14px; border: 1px solid #1E293B; text-transform: uppercase; letter-spacing: 1px; }\n"
                "  .hdr-sub { background-color: #1E293B; color: #F8FAFC; font-size: 12px; padding: 10px 14px; border: 1px solid #334155; }\n"
                "  .meta-grid { width: 100%; border-collapse: collapse; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; margin-top: 10px; }\n"
                "  .meta-grid th { background-color: #1E293B; color: #38BDF8; font-weight: bold; text-align: left; padding: 10px 12px; border: 1px solid #334155; font-size: 11px; text-transform: uppercase; letter-spacing: 0.5px; }\n"
                "  .meta-grid td { padding: 9px 12px; border: 1px solid #CBD5E1; color: #0F172A; font-size: 12px; background-color: #FFFFFF; }\n"
                "  .meta-grid tr:nth-child(even) td { background-color: #F8FAFC; }\n"
                "</style>\n"
                "</head>\n"
                "<body>\n";

        html += "<table class=\"hdr-table\">\n";
        html += "  <tr>\n";
        html += "    <td colspan=\"16\" class=\"hdr-main\">BKR MATRIZ — PROJECT / COLLECTION: " + escHtml(projName) + " &nbsp;|&nbsp; CATALOG: " + escHtml(catCode) + "</td>\n";
        html += "  </tr>\n";
        html += "  <tr>\n";
        html += "    <td colspan=\"8\" class=\"hdr-sub\"><b>CATALOGING / BACKUP DATE:</b> " + escHtml(dateNow) + "</td>\n";
        html += "    <td colspan=\"8\" class=\"hdr-sub\"><b>TOTAL ASSETS:</b> " + juce::String(totalAssets) + " &nbsp;|&nbsp; <b>TOTAL SIZE:</b> " + juce::File::descriptionOfSizeInBytes(totalBytes) + "</td>\n";
        html += "  </tr>\n";
        html += "</table>\n";

        html += "<table class=\"meta-grid\">\n";
        html += "  <thead>\n";
        html += "    <tr>\n";
        html += "      <th>TITLE (dc.title)</th>\n";
        html += "      <th>CREATOR (dc.creator)</th>\n";
        html += "      <th>SUBJECT (dc.subject)</th>\n";
        html += "      <th>DESCRIPTION (dc.description)</th>\n";
        html += "      <th>PUBLISHER (dc.publisher)</th>\n";
        html += "      <th>CONTRIBUTOR (dc.contributor)</th>\n";
        html += "      <th>DATE CREATED (dc.created)</th>\n";
        html += "      <th>DATE ISSUED (dc.issued)</th>\n";
        html += "      <th>TYPE (dc.type)</th>\n";
        html += "      <th>FORMAT (dc.format)</th>\n";
        html += "      <th>IDENTIFIER (dc.identifier)</th>\n";
        html += "      <th>SOURCE (dc.source)</th>\n";
        html += "      <th>LANGUAGE (dc.language)</th>\n";
        html += "      <th>RELATION (dc.relation)</th>\n";
        html += "      <th>COVERAGE (dc.coverage)</th>\n";
        html += "      <th>RIGHTS (dc.rights)</th>\n";
        html += "    </tr>\n";
        html += "  </thead>\n";
        html += "  <tbody>\n";

        for (const auto& row : allRows) {
            html += "    <tr>\n";
            html += "      <td>" + escHtml(row.title) + "</td>\n";
            html += "      <td>" + escHtml(row.creator) + "</td>\n";
            html += "      <td>" + escHtml(row.subject) + "</td>\n";
            html += "      <td>" + escHtml(row.description) + "</td>\n";
            html += "      <td>" + escHtml(row.publisher) + "</td>\n";
            html += "      <td>" + escHtml(row.contributor) + "</td>\n";
            html += "      <td>" + escHtml(row.created) + "</td>\n";
            html += "      <td>" + escHtml(row.issued) + "</td>\n";
            html += "      <td>" + escHtml(row.type) + "</td>\n";
            html += "      <td>" + escHtml(row.format) + "</td>\n";
            html += "      <td>" + escHtml(row.identifier) + "</td>\n";
            html += "      <td>" + escHtml(row.source) + "</td>\n";
            html += "      <td>" + escHtml(row.language) + "</td>\n";
            html += "      <td>" + escHtml(row.relation) + "</td>\n";
            html += "      <td>" + escHtml(row.coverage) + "</td>\n";
            html += "      <td>" + escHtml(row.rights) + "</td>\n";
            html += "    </tr>\n";
        }

        html += "  </tbody>\n";
        html += "</table>\n";
        html += "</body>\n</html>";
        return html;
    }

    try { return preservation::exportarXlsXml(projeto_->registro(), itemIds, projName, catCode); }
    catch (...) { return {}; }
}

juce::String ProjetoAberto::exportarDublinCoreCsv(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto colecoes = listarColecoesLinkadas();
        juce::String csv;
        bool headerWritten = false;

        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = matriz::model::Project::resolverPastaProjeto(colDir).getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    std::vector<std::string> ids;
                    auto stmt = colDb.prepare("SELECT id FROM item ORDER BY codigo_acervo ASC, id ASC");
                    while (stmt.step()) ids.push_back(stmt.columnText(0));

                    juce::String part = preservation::exportarCsvDublinCore(colDb, ids);
                    if (!headerWritten) {
                        csv += part;
                        headerWritten = true;
                    } else {
                        int newlinePos = part.indexOfChar('\n');
                        if (newlinePos >= 0) csv += part.substring(newlinePos + 1);
                    }
                } catch (...) {}
            }
        }
        if (csv.isEmpty()) {
            return preservation::exportarCsvDublinCore(projeto_->registro(), itemIds);
        }
        return csv;
    }
    try { return preservation::exportarCsvDublinCore(projeto_->registro(), itemIds); }
    catch (...) { return {}; }
}

juce::String ProjetoAberto::exportarFixityManifest(const std::vector<std::string>& itemIds,
                                                     const std::string& algoritmo) const {
    if (!projeto_) return {};
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto colecoes = listarColecoesLinkadas();
        juce::String manifest;
        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = matriz::model::Project::resolverPastaProjeto(colDir).getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    std::vector<std::string> ids;
                    auto stmt = colDb.prepare("SELECT id FROM item");
                    while (stmt.step()) ids.push_back(stmt.columnText(0));
                    manifest += preservation::exportarFixityManifest(colDb, ids, algoritmo);
                } catch (...) {}
            }
        }
        if (manifest.isEmpty()) {
            return preservation::exportarFixityManifest(projeto_->registro(), itemIds, algoritmo);
        }
        return manifest;
    }
    try { return preservation::exportarFixityManifest(projeto_->registro(), itemIds, algoritmo); }
    catch (...) { return {}; }
}

void ProjetoAberto::aplicarRelinkEmMemoria(const std::string& arquivoId, const std::string& newPath) {
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        inMemoryRelinkedPaths_[arquivoId] = newPath;
    }
    dirty_ = true;
}

void ProjetoAberto::aplicarBatchRelinkEmMemoria(const std::map<std::string, std::string>& newPaths) {
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        for (const auto& [arqId, p] : newPaths) {
            inMemoryRelinkedPaths_[arqId] = p;
        }
    }
    dirty_ = true;
}

void ProjetoAberto::salvar() {
    if (!projeto_) return;
    try {
        std::map<std::string, std::string> paraGravar;
        {
            std::lock_guard<std::mutex> lock(marcacoesMutex_);
            paraGravar = inMemoryRelinkedPaths_;
        }

        auto& db = projeto_->registro();
        for (const auto& [arqId, newPath] : paraGravar) {
            auto stmt = db.prepare("UPDATE arquivo SET caminho_absoluto_origem = ?, atualizado_em = ? WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(newPath));
            stmt.bind(2, matriz::db::Value::of(matriz::model::agoraIso8601()));
            stmt.bind(3, matriz::db::Value::of(arqId));
            stmt.step();

            auto stmtLoc = db.prepare("INSERT OR IGNORE INTO localizacao_conhecida (id, arquivo_id, caminho_absoluto, criado_em) VALUES (?, ?, ?, ?)");
            stmtLoc.bind(1, matriz::db::Value::of(matriz::model::novoUuid()));
            stmtLoc.bind(2, matriz::db::Value::of(arqId));
            stmtLoc.bind(3, matriz::db::Value::of(newPath));
            stmtLoc.bind(4, matriz::db::Value::of(matriz::model::agoraIso8601()));
            stmtLoc.step();
        }

        db.run("PRAGMA wal_checkpoint(TRUNCATE)", {});
        projeto_->indice().run("PRAGMA wal_checkpoint(TRUNCATE)", {});

        {
            std::lock_guard<std::mutex> lock(marcacoesMutex_);
            inMemoryRelinkedPaths_.clear();
        }
        dirty_ = false;
    } catch (...) {}
}

void ProjetoAberto::descartarAlteracoesEmMemoria() {
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        inMemoryRelinkedPaths_.clear();
    }
    dirty_ = false;
}

std::optional<juce::File> ProjetoAberto::resolverArquivoComMemoria(const std::string& arquivoId) const {
    std::string pathEmMemoria;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        auto it = inMemoryRelinkedPaths_.find(arquivoId);
        if (it != inMemoryRelinkedPaths_.end()) pathEmMemoria = it->second;
    }
    if (!pathEmMemoria.empty()) {
        juce::File f(pathEmMemoria);
        if (f.existsAsFile()) return f;
    }
    if (!projeto_) return std::nullopt;
    return matriz::vault::resolverArquivo(projeto_->registro(), arquivoId, projeto_->pasta());
}

std::set<std::string>& ProjetoAberto::obterConjuntoMarcacao(TipoMarcacao tipo) {
    switch (tipo) {
        case TipoMarcacao::Html: return marcadosHtml_;
        case TipoMarcacao::Zip: return marcadosZip_;
        case TipoMarcacao::Print: return marcadosPrint_;
        case TipoMarcacao::Watermark: return marcadosWatermark_;
    }
    return marcadosHtml_;
}

const std::set<std::string>& ProjetoAberto::obterConjuntoMarcacao(TipoMarcacao tipo) const {
    switch (tipo) {
        case TipoMarcacao::Html: return marcadosHtml_;
        case TipoMarcacao::Zip: return marcadosZip_;
        case TipoMarcacao::Print: return marcadosPrint_;
        case TipoMarcacao::Watermark: return marcadosWatermark_;
    }
    return marcadosHtml_;
}

// obterConjuntoMarcacao() não tranca sozinha -- quem chama precisa segurar
// marcacoesMutex_ (ver comentário no membro, ProjetoAberto.h). É privada e
// só usada pelas funções abaixo, cada uma já tranca a sua parte.
void ProjetoAberto::alternarMarcacao(TipoMarcacao tipo, const std::vector<std::string>& itemIds) {
    if (itemIds.empty()) return;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        auto& s = obterConjuntoMarcacao(tipo);
        bool todosMarcados = true;
        for (const auto& id : itemIds) {
            if (s.find(id) == s.end()) {
                todosMarcados = false;
                break;
            }
        }
        if (todosMarcados) {
            for (const auto& id : itemIds) {
                s.erase(id);
            }
        } else {
            for (const auto& id : itemIds) {
                s.insert(id);
            }
        }
    }
    EventBus::obterInstancia().dispararItensAlterados(itemIds, "marcacao");
}

void ProjetoAberto::definirMarcacao(TipoMarcacao tipo, const std::vector<std::string>& itemIds, bool marcado) {
    if (itemIds.empty()) return;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        auto& s = obterConjuntoMarcacao(tipo);
        for (const auto& id : itemIds) {
            if (marcado) s.insert(id);
            else s.erase(id);
        }
    }
    EventBus::obterInstancia().dispararItensAlterados(itemIds, "marcacao");
}

bool ProjetoAberto::contemMarcacao(TipoMarcacao tipo, const std::string& itemId) const {
    if (itemId.empty()) return false;
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    const auto& s = obterConjuntoMarcacao(tipo);
    return s.find(itemId) != s.end();
}

size_t ProjetoAberto::contarMarcacoes(TipoMarcacao tipo) const {
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    return obterConjuntoMarcacao(tipo).size();
}

void ProjetoAberto::limparMarcacoes(TipoMarcacao tipo) {
    std::vector<std::string> afetados;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        auto& s = obterConjuntoMarcacao(tipo);
        if (s.empty()) return;
        afetados.assign(s.begin(), s.end());
        s.clear();
    }
    EventBus::obterInstancia().dispararItensAlterados(afetados, "marcacao");
}

void ProjetoAberto::limparTodasMarcacoes() {
    // Os 4 conjuntos de uma vez e UM evento de lote com a união dos afetados.
    std::set<std::string> afetados;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        for (auto tipo : { TipoMarcacao::Html, TipoMarcacao::Zip, TipoMarcacao::Print, TipoMarcacao::Watermark }) {
            auto& s = obterConjuntoMarcacao(tipo);
            afetados.insert(s.begin(), s.end());
            s.clear();
        }
    }
    EventBus::obterInstancia().dispararItensAlterados(std::vector<std::string>(afetados.begin(), afetados.end()), "marcacao");
}

std::vector<std::string> ProjetoAberto::idsMarcados(TipoMarcacao tipo) const {
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    const auto& s = obterConjuntoMarcacao(tipo);
    return std::vector<std::string>(s.begin(), s.end());
}

void ProjetoAberto::transferirMarcacoes(const std::string& oldItemId, const std::string& newItemId) {
    if (oldItemId.empty() || newItemId.empty() || oldItemId == newItemId) return;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        for (auto tipo : { TipoMarcacao::Html, TipoMarcacao::Zip, TipoMarcacao::Print, TipoMarcacao::Watermark }) {
            auto& s = obterConjuntoMarcacao(tipo);
            if (s.erase(oldItemId) > 0) {
                s.insert(newItemId);
            }
        }
    }
    EventBus::obterInstancia().dispararItemAlterado(newItemId, "marcacao");
}

void ProjetoAberto::salvarConfiguracaoWatermark(const ConfiguracaoWatermark& cfg) {
    if (!projeto_) return;
    juce::File f = projeto_->pasta().getChildFile("watermark.json");
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("caminhoLogo", cfg.caminhoLogo);
    obj->setProperty("opacidade", static_cast<double>(cfg.opacidade));
    obj->setProperty("escala", static_cast<double>(cfg.escala));
    obj->setProperty("margem", static_cast<double>(cfg.margem));
    obj->setProperty("posicaoIdH", cfg.posicaoIdH);
    obj->setProperty("customPosX_H", static_cast<double>(cfg.customPosX_H));
    obj->setProperty("customPosY_H", static_cast<double>(cfg.customPosY_H));
    obj->setProperty("posicaoIdV", cfg.posicaoIdV);
    obj->setProperty("customPosX_V", static_cast<double>(cfg.customPosX_V));
    obj->setProperty("customPosY_V", static_cast<double>(cfg.customPosY_V));

    juce::var v(obj.release());
    f.replaceWithText(juce::JSON::toString(v, true));
}

ConfiguracaoWatermark ProjetoAberto::obterConfiguracaoWatermark() const {
    if (!projeto_) return ConfiguracaoWatermark();
    return carregarConfiguracaoWatermarkDePasta(projeto_->pasta());
}

ConfiguracaoWatermark ProjetoAberto::carregarConfiguracaoWatermarkDePasta(const juce::File& pastaProjeto) {
    ConfiguracaoWatermark cfg;
    juce::File f = pastaProjeto.getChildFile("watermark.json");
    if (!f.existsAsFile()) return cfg;

    auto parsed = juce::JSON::parse(f);
    if (!parsed.isObject()) return cfg;

    auto* obj = parsed.getDynamicObject();
    if (!obj) return cfg;

    cfg.caminhoLogo = obj->getProperty("caminhoLogo").toString();
    if (obj->hasProperty("opacidade")) cfg.opacidade = static_cast<float>(static_cast<double>(obj->getProperty("opacidade")));
    if (obj->hasProperty("escala")) cfg.escala = static_cast<float>(static_cast<double>(obj->getProperty("escala")));
    if (obj->hasProperty("margem")) cfg.margem = static_cast<float>(static_cast<double>(obj->getProperty("margem")));
    if (obj->hasProperty("posicaoIdH")) cfg.posicaoIdH = static_cast<int>(obj->getProperty("posicaoIdH"));
    if (obj->hasProperty("customPosX_H")) cfg.customPosX_H = static_cast<float>(static_cast<double>(obj->getProperty("customPosX_H")));
    if (obj->hasProperty("customPosY_H")) cfg.customPosY_H = static_cast<float>(static_cast<double>(obj->getProperty("customPosY_H")));
    if (obj->hasProperty("posicaoIdV")) cfg.posicaoIdV = static_cast<int>(obj->getProperty("posicaoIdV"));
    if (obj->hasProperty("customPosX_V")) cfg.customPosX_V = static_cast<float>(static_cast<double>(obj->getProperty("customPosX_V")));
    if (obj->hasProperty("customPosY_V")) cfg.customPosY_V = static_cast<float>(static_cast<double>(obj->getProperty("customPosY_V")));

    return cfg;
}

// ============================================================================
// MAIN EDIT MODE (Fase 4)
// ============================================================================

bool ProjetoAberto::podeEditarMain() const {
    return projeto_ && projeto_->papel() == "ORIGINAL" && projeto_->modo() != matriz::model::Modo::Catalogo && mainExiste();
}

bool ProjetoAberto::nomeConfereComProjeto(const juce::String& digitado) const {
    if (!projeto_) return false;
    // Nome ATUAL (File > Rename Project pode ter mudado): compara com o que está no banco agora.
    const auto atual = juce::String::fromUTF8(projeto_->nome().c_str()).trim();
    return atual.isNotEmpty() && digitado.trim() == atual;
}

bool ProjetoAberto::entrarModoEdicaoMain(const juce::String& nomeDigitado) {
    if (editandoMain_) return true;
    if (!podeEditarMain() || !nomeConfereComProjeto(nomeDigitado)) return false;
    editandoMain_ = true;
    tocarAtividadeMain();
    try { matriz::model::ProjectLog(projeto_->pasta()).appendEntry("MAIN EDIT MODE: entered", {"The MAIN is now editable through Matriz."}); } catch (...) {}
    if (aoMudarModoEdicaoMain) aoMudarModoEdicaoMain();
    return true;
}

void ProjetoAberto::sairModoEdicaoMain(const juce::String& motivo) {
    if (!editandoMain_) return;
    editandoMain_ = false;
    try { matriz::model::ProjectLog(projeto_->pasta()).appendEntry("MAIN EDIT MODE: left", {"Reason: " + motivo}); } catch (...) {}
    if (aoMudarModoEdicaoMain) aoMudarModoEdicaoMain();
}

void ProjetoAberto::tocarAtividadeMain() { ultimaAtividadeMainMs_ = juce::Time::currentTimeMillis(); }

bool ProjetoAberto::verificarTimeoutEdicaoMain() {
    if (!editandoMain_ || operacaoMainEmCurso_.load()) return false;
    if (juce::Time::currentTimeMillis() - ultimaAtividadeMainMs_ < kTimeoutEdicaoMainMs) return false;
    sairModoEdicaoMain("no operations for 15 minutes");
    return true;
}

int ProjetoAberto::contarArquivosDaPastaNoMain(const std::string& pastaId) const {
    if (!projeto_) return 0;
    try {
        const auto pref = matriz::consolidacao::caminhoFisicoDaPasta(leitura(), pastaId);
        if (pref.isEmpty()) return 0;
        const std::string prefixo = (pref + "/").toStdString();
        auto st = leitura().prepare(
            "SELECT COUNT(*) FROM consolidacao_registro WHERE substr(caminho_relativo_destino, 1, ?) = ?");
        st.bind(1, matriz::db::Value::of(static_cast<long long>(pref.length() + 1)));
        st.bind(2, matriz::db::Value::of(prefixo));
        return st.step() ? static_cast<int>(st.columnInt(0)) : 0;
    } catch (...) {
        return 0;
    }
}

void ProjetoAberto::mapaMoverItemDoMain(const std::string& itemId, const std::string& pastaDe, const std::string& pastaPara) {
    if (!projeto_ || pastaPara.empty()) return;
    if (!pastaDe.empty())
        projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND pasta_id = ?",
                                  {matriz::db::Value::of(itemId), matriz::db::Value::of(pastaDe)});
    inserirItemPastaInterno(itemId, pastaPara, matriz::model::agoraIso8601());
}

matriz::mainedit::ContextoMain ProjetoAberto::contextoDoMain() const {
    matriz::mainedit::ContextoMain c;
    c.registro = &projeto_->registro();
    c.raiz = matriz::model::normalizarParaRaizDestino(projeto_->raiz());
    c.pastaProjeto = projeto_->pasta();
    c.destinoId = projeto_->destinationId();
    c.mainUsaMapa = mainUsaMapa();
    auto* self = const_cast<ProjetoAberto*>(this);
    c.mapaMoverItem = [self](const std::string& item, const std::string& de, const std::string& para) {
        self->mapaMoverItemDoMain(item, de, para);
    };
    return c;
}

void ProjetoAberto::executarEdicaoMain(const juce::String& titulo, std::function<matriz::mainedit::Resultado()> trabalho,
                                       AoConcluirEdicaoMain aoConcluir) {
    auto falhar = [&](const std::string& msg) {
        matriz::mainedit::Resultado r;
        r.erro = msg;
        if (aoConcluir) aoConcluir(r);
    };
    if (!editandoMain_) { falhar("MAIN EDIT MODE is not active"); return; }
    bool esperado = false;
    if (!operacaoMainEmCurso_.compare_exchange_strong(esperado, true)) { falhar("another MAIN operation is still running"); return; }
    tocarAtividadeMain();
    ProgressoGlobal::obterInstancia().iniciarTarefa("main_edit", titulo, 0, nullptr, titulo + "...");
    std::weak_ptr<bool> vivo = vivo_;
    poolMainEdit_.addJob([this, vivo, trabalho = std::move(trabalho), aoConcluir = std::move(aoConcluir), titulo]() {
        matriz::mainedit::Resultado r;
        try {
            r = trabalho();
        } catch (const std::exception& e) {
            r.ok = false;
            r.erro = e.what();
        }
        juce::MessageManager::callAsync([this, vivo, r, aoConcluir, titulo]() {
            auto vivoAgora = vivo.lock();
            if (!vivoAgora) return;  // o projeto foi fechado no meio
            operacaoMainEmCurso_.store(false);
            tocarAtividadeMain();
            ProgressoGlobal::obterInstancia().concluirTarefa(
                "main_edit", r.ok ? titulo + ": done" : titulo + ": failed - " + juce::String::fromUTF8(r.erro.c_str()));
            if (aoConcluir) aoConcluir(r);
        });
    });
}

void ProjetoAberto::editarMainRenomearArquivo(const std::string& registroId, const juce::String& novoNome,
                                              AoConcluirEdicaoMain aoConcluir, bool registrarUndoDesta) {
    auto ctx = contextoDoMain();
    executarEdicaoMain("Renaming file in MAIN", [ctx, registroId, novoNome] {
        return matriz::mainedit::renomearArquivo(ctx, registroId, novoNome);
    }, [this, registroId, aoConcluir, registrarUndoDesta](const matriz::mainedit::Resultado& r) {
        if (r.ok && registrarUndoDesta) {
            const auto nomeAntes = juce::File(r.deRel).getFileName();
            registrarUndo("Rename file in MAIN", [this, registroId, nomeAntes]() {
                editarMainRenomearArquivo(registroId, nomeAntes, nullptr, false);
            });
        }
        if (aoConcluir) aoConcluir(r);
    });
}

void ProjetoAberto::editarMainMoverArquivo(const std::string& registroId, const std::string& novaPastaId,
                                           AoConcluirEdicaoMain aoConcluir, bool registrarUndoDesta) {
    auto ctx = contextoDoMain();
    std::string pastaAntes;
    {
        auto st = projeto_->registro().prepare("SELECT pasta_id FROM consolidacao_registro WHERE id = ?");
        st.bind(1, matriz::db::Value::of(registroId));
        if (st.step()) pastaAntes = st.columnText(0);
    }
    executarEdicaoMain("Moving file in MAIN", [ctx, registroId, novaPastaId] {
        return matriz::mainedit::moverArquivo(ctx, registroId, novaPastaId);
    }, [this, registroId, aoConcluir, registrarUndoDesta, pastaAntes](const matriz::mainedit::Resultado& r) {
        if (r.ok && registrarUndoDesta && !pastaAntes.empty()) {
            registrarUndo("Move file in MAIN", [this, registroId, pastaAntes]() {
                editarMainMoverArquivo(registroId, pastaAntes, nullptr, false);
            });
        }
        if (aoConcluir) aoConcluir(r);
    });
}

void ProjetoAberto::editarMainSubstituirArquivo(const std::string& registroId, const juce::File& novaVersao,
                                                AoConcluirEdicaoMain aoConcluir) {
    auto ctx = contextoDoMain();
    executarEdicaoMain("Replacing file in MAIN", [ctx, registroId, novaVersao] {
        return matriz::mainedit::substituirArquivo(ctx, registroId, novaVersao);
    }, std::move(aoConcluir));
}

void ProjetoAberto::editarMainDeletarArquivo(const std::string& registroId, AoConcluirEdicaoMain aoConcluir) {
    auto ctx = contextoDoMain();
    executarEdicaoMain("Moving file to quarantine", [ctx, registroId] {
        return matriz::mainedit::deletarArquivo(ctx, registroId);
    }, std::move(aoConcluir));
}

void ProjetoAberto::editarMainRestaurar(const std::string& quarentenaId, const juce::String& destinoAlternativoRel,
                                        AoConcluirEdicaoMain aoConcluir) {
    auto ctx = contextoDoMain();
    executarEdicaoMain("Restoring from quarantine", [ctx, quarentenaId, destinoAlternativoRel] {
        return matriz::mainedit::restaurar(ctx, quarentenaId, destinoAlternativoRel);
    }, std::move(aoConcluir));
}

void ProjetoAberto::editarMainEsvaziarQuarentena(std::function<void(const matriz::mainedit::ResultadoEsvaziar&)> aoConcluir) {
    auto ctx = contextoDoMain();
    auto resultado = std::make_shared<matriz::mainedit::ResultadoEsvaziar>();
    executarEdicaoMain("Emptying quarantine", [ctx, resultado] {
        *resultado = matriz::mainedit::esvaziarQuarentena(ctx, [](int feito, int total) {
            juce::MessageManager::callAsync([feito, total] {
                ProgressoGlobal::obterInstancia().atualizarFracao("main_edit", static_cast<double>(feito) / std::max(1, total),
                                                                   juce::String(feito) + " / " + juce::String(total));
            });
            return true;
        });
        matriz::mainedit::Resultado r;
        r.ok = resultado->falhas.empty() && !resultado->cancelado;
        if (!r.ok && !resultado->falhas.empty()) r.erro = resultado->falhas.front();
        return r;
    }, [resultado, aoConcluir](const matriz::mainedit::Resultado&) {
        if (aoConcluir) aoConcluir(*resultado);
    });
}

void ProjetoAberto::converterMainParaFolderMap(
    std::function<void(const juce::String& erro, const juce::String& nomeDoMapa)> aoConcluir) {
    if (!editandoMain_ || !projeto_) { if (aoConcluir) aoConcluir("MAIN EDIT MODE is not active", {}); return; }
    if (mainUsaMapa()) { if (aoConcluir) aoConcluir("The MAIN already uses a folder map", {}); return; }
    auto nomeDoMapa = std::make_shared<juce::String>();
    executarEdicaoMain("Converting MAIN to folder map", [this, nomeDoMapa]() {
        matriz::mainedit::Resultado r;
        auto& db = projeto_->registro();
        struct Linha { std::string id, item; juce::String caminho; };
        std::vector<Linha> linhas;
        {
            auto st = db.prepare("SELECT id, item_id, caminho_relativo_destino FROM consolidacao_registro");
            while (st.step()) linhas.push_back({st.columnText(0), st.columnText(1), juce::String::fromUTF8(st.columnText(2).c_str())});
        }
        if (linhas.empty()) { r.erro = "the MAIN has no registered files"; return r; }

        // Pastas reais: todo prefixo de diretório dos caminhos registrados.
        std::set<juce::String> dirs;
        for (auto& l : linhas) {
            juce::StringArray partes;
            partes.addTokens(l.caminho, "/", "");
            juce::String acumulado;
            for (int i = 0; i < partes.size() - 1; ++i) {
                acumulado += (acumulado.isEmpty() ? "" : "/") + partes[i];
                if (partes[i].isNotEmpty()) dirs.insert(acumulado);
            }
        }
        std::vector<juce::String> ordenadas(dirs.begin(), dirs.end());
        std::sort(ordenadas.begin(), ordenadas.end(), [](const juce::String& a, const juce::String& b) {
            const int da = a.retainCharacters("/").length(), dbb = b.retainCharacters("/").length();
            return da != dbb ? da < dbb : a < b;
        });

        const juce::String nome = "MAIN structure";
        std::string mapaId = criarFolderMap(nome, std::nullopt);
        if (mapaId.empty()) { r.erro = "could not create the folder map"; return r; }
        *nomeDoMapa = nome;

        matriz::db::Database::Trava trava(db);
        db.exec("BEGIN IMMEDIATE");
        try {
            std::map<juce::String, std::string> idPorDir;
            for (auto& dir : ordenadas) {
                const juce::String pai = dir.contains("/") ? dir.upToLastOccurrenceOf("/", false, false) : juce::String();
                const juce::String seg = dir.contains("/") ? dir.fromLastOccurrenceOf("/", false, false) : dir;
                std::optional<std::string> paiId;
                if (pai.isNotEmpty()) paiId = idPorDir[pai];
                idPorDir[dir] = criarPastaAcervo(seg.toStdString(), paiId, mapaId);
            }
            const auto agora = matriz::model::agoraIso8601();
            for (auto& l : linhas) {
                if (!l.caminho.contains("/")) continue;  // raiz de Media/: fica em NO FOLDER
                const auto pastaId = idPorDir[l.caminho.upToLastOccurrenceOf("/", false, false)];
                if (pastaId.empty()) continue;
                inserirItemPastaInterno(l.item, pastaId, agora);
                db.run("UPDATE consolidacao_registro SET pasta_id = ? WHERE id = ?",
                       {matriz::db::Value::of(pastaId), matriz::db::Value::of(l.id)});
            }
            // A partir daqui o MAIN segue as regras "com folder map".
            juce::var cfg;
            {
                auto st = db.prepare("SELECT COALESCE(backup_config_main, '') FROM projeto LIMIT 1");
                if (st.step()) cfg = juce::JSON::parse(juce::String::fromUTF8(st.columnText(0).c_str()));
            }
            if (!cfg.isObject()) cfg = juce::var(new juce::DynamicObject());
            auto* o = cfg.getDynamicObject();
            o->setProperty("preservar", false);
            o->setProperty("mapa", true);
            o->setProperty("org", 1);
            o->setProperty("por_source", false);
            o->setProperty("mapa_id", juce::String(mapaId));
            db.run("UPDATE projeto SET backup_config_main = ?", {matriz::db::Value::of(juce::JSON::toString(cfg, true).toStdString())});
            db.exec("COMMIT");
        } catch (...) {
            try { db.exec("ROLLBACK"); } catch (...) {}
            throw;
        }
        try {
            matriz::model::ProjectLog(projeto_->pasta()).appendEntry(
                "MAIN EDIT: MAIN converted to folder map",
                {"New folder map: " + nome, "Folders created: " + juce::String(static_cast<int>(ordenadas.size())),
                 "Automatic organization by year/type/source no longer applies; new items go to _SEM_PASTA until organized."});
        } catch (...) {}
        r.ok = true;
        return r;
    }, [nomeDoMapa, aoConcluir](const matriz::mainedit::Resultado& r) {
        if (aoConcluir) aoConcluir(r.ok ? juce::String() : juce::String::fromUTF8(r.erro.c_str()), *nomeDoMapa);
    });
}

void ProjetoAberto::recuperarOperacoesDoMain() {
    if (!projeto_ || projeto_->papel() != "ORIGINAL" || projeto_->modo() == matriz::model::Modo::Catalogo) return;
    auto ctx = contextoDoMain();
    std::weak_ptr<bool> vivo = vivo_;
    poolMainEdit_.addJob([ctx, vivo]() {
        auto res = matriz::mainedit::recuperarJournal(ctx);
        if (res.conflitos.empty()) return;
        juce::MessageManager::callAsync([vivo, res]() {
            if (!vivo.lock()) return;
            juce::String msg = matriz::i18n::t("main_edit.recuperacao_conflito");
            for (size_t i = 0; i < res.conflitos.size() && i < 5; ++i) msg << "\n\n" << juce::String::fromUTF8(res.conflitos[i].c_str());
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                                             .withTitle(matriz::i18n::t("main_edit.titulo"))
                                             .withMessage(msg)
                                             .withButton(matriz::i18n::t("dialogo.ok")),
                                         juce::ModalCallbackFunction::create([](int) {}));
        });
    });
}

} // namespace matriz::ui
