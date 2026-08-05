#include "Consolidacao.h"

#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "Mascara.h"
#include "MetadadoEmbutido.h"

#include <algorithm>
#include <map>
#include <set>

namespace matriz::consolidacao {

using matriz::db::Value;

namespace {

// Cadeia de nomes da raiz até `pastaId` (inclusive) — pra tanto montar o
// caminho de pastas de saída quanto achar a máscara herdada.
std::vector<std::pair<std::string, juce::String>> cadeiaAncestral(matriz::db::Database& registro,
                                                                     const std::string& pastaId) {
    std::vector<std::pair<std::string, juce::String>> cadeia; // {id, nome}, folha primeiro
    std::string atual = pastaId;
    std::set<std::string> visitados; // guarda contra ciclo malformado — nunca trava
    while (!atual.empty() && !visitados.count(atual)) {
        visitados.insert(atual);
        auto stmt = registro.prepare("SELECT nome, pasta_pai_id FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, Value::of(atual));
        if (!stmt.step()) break;
        cadeia.push_back({atual, juce::String(stmt.columnText(0))});
        atual = stmt.columnIsNull(1) ? std::string() : stmt.columnText(1);
    }
    std::reverse(cadeia.begin(), cadeia.end()); // raiz primeiro
    return cadeia;
}

// Máscara efetiva de uma pasta: a dela mesma se tiver, senão sobe a cadeia
// até achar uma, senão a do projeto (§11.6 — "herdada da pai").
juce::String mascaraEfetiva(matriz::db::Database& registro, const std::vector<std::pair<std::string, juce::String>>& cadeia) {
    for (auto it = cadeia.rbegin(); it != cadeia.rend(); ++it) {
        auto stmt = registro.prepare("SELECT mascara_nomenclatura FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, Value::of(it->first));
        if (stmt.step() && !stmt.columnIsNull(0)) {
            juce::String m = stmt.columnText(0);
            if (m.isNotEmpty()) return m;
        }
    }
    auto stmtProjeto = registro.prepare("SELECT mascara_nomenclatura FROM projeto LIMIT 1");
    if (stmtProjeto.step()) return juce::String(stmtProjeto.columnText(0));
    return "{codigo}-{seq:03}-{titulo}";
}

std::map<std::string, std::string> camposFichaDoItem(matriz::db::Database& registro, const std::string& itemId) {
    std::map<std::string, std::string> out;
    auto stmt = registro.prepare(
        "SELECT campo_id, valor FROM item_campo WHERE item_id = ? AND nivel = 'raiz' AND nivel_indice = 0");
    stmt.bind(1, Value::of(itemId));
    while (stmt.step())
        if (!stmt.columnIsNull(1)) out[stmt.columnText(0)] = stmt.columnText(1);
    return out;
}

// Nome de pasta seguro nos dois sistemas de arquivos: sem separador, sem
// caractere reservado do Windows, sem ponto/espaço no fim (o Explorer
// recusa). Nunca devolve vazio — pasta sem nome não é pasta.
juce::String segmentoSeguro(const juce::String& bruto, const juce::String& fallback) {
    juce::String s = bruto.trim();
    for (auto c : juce::String("/\\:*?\"<>|")) s = s.replaceCharacter(c, '-');
    s = s.trimCharactersAtEnd(". ");
    return s.isEmpty() ? fallback : s;
}

} // namespace

std::string nivelHierarquiaToString(NivelHierarquia n) {
    switch (n) {
        case NivelHierarquia::Projeto: return "projeto";
        case NivelHierarquia::Ano: return "ano";
        case NivelHierarquia::TipoMidia: return "tipo_midia";
        case NivelHierarquia::TipoArquivo: return "tipo_arquivo";
        case NivelHierarquia::Origem: return "origem";
        case NivelHierarquia::Artista: return "artista";
        case NivelHierarquia::PastaManual: return "pasta_manual";
    }
    return "projeto";
}

NivelHierarquia nivelHierarquiaFromString(const std::string& s) {
    if (s == "projeto") return NivelHierarquia::Projeto;
    if (s == "ano") return NivelHierarquia::Ano;
    if (s == "tipo_midia") return NivelHierarquia::TipoMidia;
    if (s == "tipo_arquivo") return NivelHierarquia::TipoArquivo;
    if (s == "origem") return NivelHierarquia::Origem;
    if (s == "artista") return NivelHierarquia::Artista;
    if (s == "pasta_manual") return NivelHierarquia::PastaManual;
    throw std::runtime_error("nível de hierarquia de backup desconhecido: \"" + s + "\"");
}

HierarquiaBackup hierarquiaPadrao() {
    return {NivelHierarquia::Projeto, NivelHierarquia::Ano, NivelHierarquia::TipoMidia, NivelHierarquia::TipoArquivo};
}

std::string hierarquiaParaCsv(const HierarquiaBackup& h) {
    std::string out;
    for (auto n : h) {
        if (!out.empty()) out += ",";
        out += nivelHierarquiaToString(n);
    }
    return out;
}

HierarquiaBackup hierarquiaDeCsv(const std::string& csv) {
    HierarquiaBackup out;
    juce::StringArray partes;
    partes.addTokens(juce::String(csv), ",", "");
    partes.removeEmptyStrings();
    for (auto& p : partes) {
        try {
            out.push_back(nivelHierarquiaFromString(p.trim().toStdString()));
        } catch (const std::exception&) {
            return hierarquiaPadrao(); // valor corrompido não impede de abrir o projeto
        }
    }
    return out.empty() ? hierarquiaPadrao() : out;
}

HierarquiaBackup hierarquiaDoProjeto(matriz::db::Database& registro) {
    auto stmt = registro.prepare("SELECT hierarquia_backup FROM projeto LIMIT 1");
    if (!stmt.step() || stmt.columnIsNull(0)) return hierarquiaPadrao();
    return hierarquiaDeCsv(stmt.columnText(0));
}

void gravarHierarquiaDoProjeto(matriz::db::Database& registro, const HierarquiaBackup& hierarquia) {
    registro.run("UPDATE projeto SET hierarquia_backup = ?, atualizado_em = ?",
                  {Value::of(hierarquiaParaCsv(hierarquia)), Value::of(matriz::model::agoraIso8601())});
}

PlanoConsolidacao planejarConsolidacao(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                        const juce::File& destino, const HierarquiaBackup& hierarquiaPedida,
                                        const RotuloTipoMidia& rotuloTipoMidia) {
    PlanoConsolidacao plano;
    HierarquiaBackup hierarquia = hierarquiaPedida.empty() ? hierarquiaDoProjeto(registro) : hierarquiaPedida;
    auto rotuloTipo = rotuloTipoMidia ? rotuloTipoMidia
                                       : RotuloTipoMidia([](const std::string& t) { return juce::String(t); });

    // Um contador de sequência POR PASTA (não global) — "{seq:03}" dentro
    // de uma pasta numera os itens daquela pasta, na ordem em que a
    // consulta devolve (por código de acervo, estável e previsível).
    std::map<std::string, int> seqPorPasta;

    auto stmt = registro.prepare(
        "SELECT aip.item_id, aip.pasta_id, i.codigo_acervo, i.titulo, i.tipo_midia, "
        "a.id, a.caminho_relativo, a.checksum_sha256 "
        "FROM acervo_item_pasta aip "
        "JOIN item i ON i.id = aip.item_id "
        "JOIN arquivo a ON a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = i.id "
        "                          ORDER BY eh_master DESC, id LIMIT 1) "
        "ORDER BY i.codigo_acervo");

    std::map<std::string, std::vector<size_t>> indicesPorDestino; // caminho final -> índices em plano.itens, pra achar conflito

    while (stmt.step()) {
        ItemPlanejado ip;
        ip.itemId = stmt.columnText(0);
        ip.pastaId = stmt.columnText(1);
        ip.codigoAcervo = stmt.columnText(2);
        std::string titulo = stmt.columnText(3);
        std::string tipoMidia = stmt.columnText(4);
        ip.arquivoId = stmt.columnText(5);
        juce::String caminhoRelativoOrigem = stmt.columnText(6);
        std::string checksumAtual = stmt.columnText(7);

        juce::File arquivoNoProjeto = pastaProjeto.getChildFile(caminhoRelativoOrigem);
        ip.nomeOriginal = arquivoNoProjeto.getFileName();
        ip.tamanhoBytes = arquivoNoProjeto.existsAsFile() ? arquivoNoProjeto.getSize() : 0;

        auto cadeia = cadeiaAncestral(registro, ip.pastaId);
        juce::String mascara = mascaraEfetiva(registro, cadeia);

        matriz::consolidacao::ContextoMascara ctx;
        ctx.codigoAcervo = ip.codigoAcervo;
        ctx.titulo = titulo;
        ctx.tipoMidia = tipoMidia;
        ctx.nomeOriginalSemExtensao = arquivoNoProjeto.getFileNameWithoutExtension().toStdString();
        {
            auto stmtProjeto = registro.prepare("SELECT nome FROM projeto LIMIT 1");
            ctx.nomeAcervo = stmtProjeto.step() ? stmtProjeto.columnText(0) : pastaProjeto.getFileNameWithoutExtension().toStdString();
        }
        ctx.nomePasta = cadeia.empty() ? std::string() : cadeia.back().second.toStdString();
        ctx.seq = ++seqPorPasta[ip.pastaId];
        ctx.camposFicha = camposFichaDoItem(registro, ip.itemId);

        std::string nomeBase = resolverMascara(mascara, ctx);
        juce::String extensao = arquivoNoProjeto.getFileExtension(); // já inclui o "."

        // Caminho de pastas montado nível a nível conforme a hierarquia
        // escolhida (item 5.2). Campo ausente vira "sem <campo>" — nunca
        // some, nunca trava.
        juce::StringArray segmentosPasta;
        for (auto nivel : hierarquia) {
            switch (nivel) {
                case NivelHierarquia::Projeto:
                    segmentosPasta.add(segmentoSeguro(ctx.nomeAcervo, "Project"));
                    break;
                case NivelHierarquia::Ano: {
                    auto it = ctx.camposFicha.find("ano");
                    juce::String ano = it == ctx.camposFicha.end() ? juce::String() : juce::String(it->second).trim();
                    segmentosPasta.add(segmentoSeguro(ano, "No year"));
                    break;
                }
                case NivelHierarquia::TipoMidia:
                    segmentosPasta.add(segmentoSeguro(tipoMidia.empty() ? juce::String() : rotuloTipo(tipoMidia),
                                                       "Unclassified"));
                    break;
                case NivelHierarquia::TipoArquivo: {
                    juce::String ext = extensao.trimCharactersAtStart(".").toUpperCase();
                    segmentosPasta.add(segmentoSeguro(ext, "No extension"));
                    break;
                }
                case NivelHierarquia::Origem: {
                    auto it = ctx.camposFicha.find("origem");
                    juce::String origem = it == ctx.camposFicha.end() ? juce::String() : juce::String(it->second).trim();
                    segmentosPasta.add(segmentoSeguro(origem, "No origin"));
                    break;
                }
                case NivelHierarquia::Artista: {
                    auto it = ctx.camposFicha.find("artista_principal");
                    juce::String artista = it == ctx.camposFicha.end() ? juce::String() : juce::String(it->second).trim();
                    segmentosPasta.add(segmentoSeguro(artista, "No artist"));
                    break;
                }
                case NivelHierarquia::PastaManual:
                    // A subárvore que o operador montou à mão na árvore
                    // BACKUP, com todos os seus níveis (não só a folha).
                    for (auto& [id, nome] : cadeia) segmentosPasta.add(segmentoSeguro(nome, "Folder"));
                    break;
            }
        }
        juce::String caminhoRelDestino =
            (segmentosPasta.isEmpty() ? juce::String() : segmentosPasta.joinIntoString("/") + "/") + juce::String(nomeBase) +
            extensao;
        ip.caminhoRelativoDestino = caminhoRelDestino;

        // Incremental: já existe um registro pra esta combinação com o
        // MESMO checksum do arquivo hoje? Então não precisa copiar de novo.
        auto stmtJa = registro.prepare(
            "SELECT checksum_sha256 FROM consolidacao_registro WHERE item_id = ? AND pasta_id = ? AND arquivo_id = ?");
        stmtJa.bind(1, Value::of(ip.itemId));
        stmtJa.bind(2, Value::of(ip.pastaId));
        stmtJa.bind(3, Value::of(ip.arquivoId));
        if (stmtJa.step() && stmtJa.columnText(0) == checksumAtual) ip.jaConsolidado = true;

        indicesPorDestino[caminhoRelDestino.toStdString()].push_back(plano.itens.size());
        plano.itens.push_back(std::move(ip));
    }

    for (auto& [caminho, indices] : indicesPorDestino) {
        if (indices.size() <= 1) continue;
        plano.nomesEmConflito.push_back(juce::String(caminho));
        for (auto idx : indices) plano.itens[idx].emConflito = true;
    }

    auto stmtNaoOrg = registro.prepare(
        "SELECT COUNT(*) FROM item WHERE id NOT IN (SELECT item_id FROM acervo_item_pasta)");
    if (stmtNaoOrg.step()) plano.itensNaoOrganizados = static_cast<int>(stmtNaoOrg.columnInt(0));

    for (auto& ip : plano.itens)
        if (!ip.jaConsolidado && !ip.emConflito) plano.espacoNecessarioBytes += ip.tamanhoBytes;

    plano.espacoDisponivelBytes = destino.getBytesFreeOnVolume();

    return plano;
}

ResultadoConsolidacao executarConsolidacao(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                            const juce::File& destino, const PlanoConsolidacao& plano,
                                            const AoProgredir& aoProgredir) {
    ResultadoConsolidacao resultado;
    resultado.totalPlanejado = static_cast<int>(plano.itens.size());
    std::string agora = matriz::model::agoraIso8601();

    int processados = 0;
    for (auto& ip : plano.itens) {
        // Checagem ANTES de copiar, nunca depois: um arquivo de 40 GB
        // copiado até o fim depois do clique não é "cancelamento imediato".
        if (aoProgredir && !aoProgredir(processados, resultado.totalPlanejado)) {
            resultado.cancelado = true;
            break;
        }
        ++processados;

        if (ip.emConflito) continue; // bloqueado — plano.podeConsolidar() já devia ter impedido chegar aqui
        if (ip.jaConsolidado) {
            ++resultado.pulados;
            continue;
        }

        try {
            auto stmt = registro.prepare("SELECT caminho_relativo FROM arquivo WHERE id = ?");
            stmt.bind(1, Value::of(ip.arquivoId));
            if (!stmt.step()) throw std::runtime_error("arquivo não encontrado no registro");
            juce::File origem = pastaProjeto.getChildFile(juce::String(stmt.columnText(0)));
            if (!origem.existsAsFile()) throw std::runtime_error("master ausente em disco: " + origem.getFullPathName().toStdString());

            juce::File destinoArquivo = destino.getChildFile(ip.caminhoRelativoDestino);
            destinoArquivo.getParentDirectory().createDirectory();
            destinoArquivo.deleteFile(); // reconsolidação: substitui a cópia anterior, nunca acumula lixo

            if (!origem.copyFileTo(destinoArquivo))
                throw std::runtime_error("falha ao copiar pra " + destinoArquivo.getFullPathName().toStdString());

            // Verificação (§11.7): existe, tamanho bate, checksum bate. Não
            // há metadado embutido nesta etapa — nada a "ler de volta".
            if (!destinoArquivo.existsAsFile()) throw std::runtime_error("cópia não existe depois de copiar");
            if (destinoArquivo.getSize() != origem.getSize())
                throw std::runtime_error("tamanho da cópia não bate com o original");

            matriz::ingest::Checksums checksumOrigem = matriz::ingest::calcularChecksums(origem);
            matriz::ingest::Checksums checksumCopia = matriz::ingest::calcularChecksums(destinoArquivo);
            if (checksumCopia.sha256 != checksumOrigem.sha256)
                throw std::runtime_error("checksum da cópia não bate com o original");

            registro.run(
                "INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                "checksum_sha256, consolidado_em) VALUES (?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(item_id, pasta_id, arquivo_id) DO UPDATE SET "
                "caminho_relativo_destino = excluded.caminho_relativo_destino, "
                "checksum_sha256 = excluded.checksum_sha256, consolidado_em = excluded.consolidado_em",
                {Value::of(matriz::model::novoUuid()), Value::of(ip.itemId), Value::of(ip.pastaId), Value::of(ip.arquivoId),
                 Value::of(ip.caminhoRelativoDestino.toStdString()), Value::of(checksumCopia.sha256), Value::of(agora)});

            // Capa junto da cópia (item 9). O spec pede EMBUTIR quando o
            // formato suporta (ID3/FLAC/MP4) — isso não existe, pelo mesmo
            // motivo declarado no cabeçalho deste arquivo (não há escritor
            // de metadado por formato no projeto). O que dá pra garantir
            // hoje é a outra metade do spec: "copiada junto quando não".
            // Vai ao lado do arquivo, com o mesmo nome base, pra qualquer
            // navegador de arquivos mostrar os dois juntos.
            {
                auto stmtCapa = registro.prepare(
                    "SELECT caminho_relativo FROM arquivo WHERE item_id = ? AND papel = 'capa_frente' LIMIT 1");
                stmtCapa.bind(1, Value::of(ip.itemId));
                if (stmtCapa.step()) {
                    juce::File capa = pastaProjeto.getChildFile(juce::String(stmtCapa.columnText(0)));
                    if (capa.existsAsFile()) {
                        juce::File destinoCapa = destinoArquivo.getParentDirectory().getChildFile(
                            destinoArquivo.getFileNameWithoutExtension() + capa.getFileExtension());
                        destinoCapa.deleteFile();
                        // Falha ao copiar a capa não derruba o item: o master
                        // já foi copiado e verificado, e é ele que importa.
                        if (!capa.copyFileTo(destinoCapa))
                            resultado.falhas.push_back(ip.codigoAcervo + ": capa não pôde ser copiada junto");
                    }
                }
            }

            ++resultado.consolidados;
        } catch (const std::exception& e) {
            resultado.falhas.push_back(ip.codigoAcervo + ": " + e.what());
        }
    }

    // Marcadores viram metadado embutido NA CÓPIA (item 8.3), depois de tudo
    // já copiado e verificado — o original nunca é tocado. Roda por último de
    // propósito: se falhar, a cópia verificada continua válida, e o marcador
    // continua na ficha e no relatório.
    resultado.arquivosComMarcadorEmbutido = embutirMarcadoresNoBackup(registro, destino);

    return resultado;
}

} // namespace matriz::consolidacao
