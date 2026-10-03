#include "Consolidacao.h"
#include <unordered_map>
#include "../Diag/Watchdog.h"

#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "../Model/ProjectLog.h"
#include "../Preservation/Preservation.h"
#include "../Vault/Resolucao.h"
#include "Mascara.h"
#include "MetadadoEmbutido.h"
// matriz_ingest_selftest (console app) não linka juce_gui_basics — Ui/BatchWatermarkDialog.h
// declara um juce::Component e não compila nesse alvo. A aplicação de watermark no backup
// só existe no app gráfico; no self-test o bloco abaixo simplesmente não entra.
#if JUCE_MODULE_AVAILABLE_juce_gui_basics
#include "../Ui/ProjetoAberto.h"
#include "../Ui/BatchWatermarkDialog.h"
#endif

#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

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

// Verdadeiro se a pasta (ou qualquer ancestral até a raiz) estiver marcada
// como DESATIVADA — desativar uma pasta na TREEMAP tem que dar bypass em
// tudo dentro dela, então a checagem sobe a cadeia inteira, não só o nível
// direto.
bool pastaOuAncestralDesativada(matriz::db::Database& registro, const std::string& pastaId) {
    if (pastaId.empty()) return false;
    std::string atual = pastaId;
    std::set<std::string> visitados;
    while (!atual.empty() && !visitados.count(atual)) {
        visitados.insert(atual);
        auto stmt = registro.prepare("SELECT ativo, pasta_pai_id FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, Value::of(atual));
        if (!stmt.step()) break;
        if (!stmt.columnIsNull(0) && stmt.columnInt(0) == 0) return true;
        atual = stmt.columnIsNull(1) ? std::string() : stmt.columnText(1);
    }
    return false;
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
    if (stmtProjeto.step() && !stmtProjeto.columnIsNull(0)) {
        juce::String m = stmtProjeto.columnText(0);
        if (m.isNotEmpty()) return m;
    }
    return "{codigo}-{seq:03}-{titulo}";
}

std::map<std::string, std::string> camposFichaDoItem(matriz::db::Database& registro, const std::string& itemId) {
    std::map<std::string, std::string> out;
    auto stmt = registro.prepare(
        "SELECT campo_id, valor FROM item_campo WHERE item_id = ? AND nivel = 'raiz' AND nivel_indice = 0");
    stmt.bind(1, Value::of(itemId));
    while (stmt.step())
        if (!stmt.columnIsNull(1)) out[stmt.columnText(0)] = stmt.columnText(1);
    // EVENT DATE = coluna item.ano (é o que a ficha mostra e o ingest preenche; item_campo só tem o
    // "ano" de quem foi editado à mão). Vazio ou "0" = sem EVENT DATE -> nível YEAR cai em "No year".
    auto stAno = registro.prepare("SELECT ano FROM item WHERE id = ?");
    stAno.bind(1, Value::of(itemId));
    if (stAno.step()) {
        const std::string ano = stAno.columnIsNull(0) ? std::string() : stAno.columnText(0);
        if (ano.empty() || ano == "0") out.erase("ano");
        else out["ano"] = ano;
    }
    return out;
}

std::string extrairAnoDeTexto(const juce::String& val) {
    for (int i = 0; i + 3 < val.length(); ++i) {
        if (std::isdigit(val[i]) && std::isdigit(val[i+1]) &&
            std::isdigit(val[i+2]) && std::isdigit(val[i+3])) {
            int yr = val.substring(i, i + 4).getIntValue();
            if (yr >= 1800 && yr <= 2099) return std::to_string(yr);
        }
    }
    return {};
}

std::string resolverAnoEfetivo(matriz::db::Database& registro, const std::string& itemId,
                               const std::string& arquivoId, const juce::File& arquivoNoProjeto) {
    // 1. item_campo (manual override "ano")
    try {
        auto stmt = registro.prepare(
            "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = 'ano' AND valor IS NOT NULL AND valor != '' LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            std::string yr = extrairAnoDeTexto(stmt.columnText(0));
            if (!yr.empty()) return yr;
        }
    } catch (...) {}

    // 2. item.ano column in item table
    try {
        auto stmt = registro.prepare("SELECT ano FROM item WHERE id = ? AND ano IS NOT NULL AND ano > 0 LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            int yr = stmt.columnInt(0);
            if (yr >= 1800 && yr <= 2099) return std::to_string(yr);
        }
    } catch (...) {}

    // 3. other date fields in item_campo (dc_created, data_criacao, data)
    try {
        auto stmt = registro.prepare(
            "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id IN ('dc_created', 'data_criacao', 'data') AND valor IS NOT NULL AND valor != '' LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            std::string yr = extrairAnoDeTexto(stmt.columnText(0));
            if (!yr.empty()) return yr;
        }
    } catch (...) {}

    // 4. EXIF / technical characteristics in arquivo table
    try {
        auto stmt = registro.prepare(
            "SELECT caracteristicas_tecnicas_json, caminho_absoluto_origem FROM arquivo WHERE id = ? OR (item_id = ? AND eh_master = 1) ORDER BY eh_master DESC LIMIT 1");
        stmt.bind(1, Value::of(arquivoId));
        stmt.bind(2, Value::of(itemId));
        if (stmt.step()) {
            if (!stmt.columnIsNull(0)) {
                auto varObj = juce::JSON::parse(stmt.columnText(0));
                if (varObj.isObject()) {
                    if (varObj.hasProperty("exifDataOriginal")) {
                        std::string yr = extrairAnoDeTexto(varObj["exifDataOriginal"].toString());
                        if (!yr.empty()) return yr;
                    }
                    if (varObj.hasProperty("creation_time")) {
                        std::string yr = extrairAnoDeTexto(varObj["creation_time"].toString());
                        if (!yr.empty()) return yr;
                    }
                    if (varObj.hasProperty("date")) {
                        std::string yr = extrairAnoDeTexto(varObj["date"].toString());
                        if (!yr.empty()) return yr;
                    }
                    if (varObj.hasProperty("year")) {
                        std::string yr = extrairAnoDeTexto(varObj["year"].toString());
                        if (!yr.empty()) return yr;
                    }
                }
            }
            if (!stmt.columnIsNull(1)) {
                juce::File absOrig(stmt.columnText(1));
                if (absOrig.existsAsFile()) {
                    int yr = absOrig.getCreationTime().getYear();
                    if (yr <= 1970 || yr > 2099) yr = absOrig.getLastModificationTime().getYear();
                    if (yr >= 1800 && yr <= 2099) return std::to_string(yr);
                }
            }
        }
    } catch (...) {}

    // 5. Physical file in project
    if (arquivoNoProjeto.existsAsFile()) {
        int yr = arquivoNoProjeto.getCreationTime().getYear();
        if (yr <= 1970 || yr > 2099) yr = arquivoNoProjeto.getLastModificationTime().getYear();
        if (yr >= 1800 && yr <= 2099) return std::to_string(yr);
    }

    return {};
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

juce::String segmentoDePastaSeguro(const juce::String& nome) { return segmentoSeguro(nome, "Folder"); }

void anotarMovePendentePraClones(matriz::db::Database& registro, const char* tipo, const std::string& de,
                                 const std::string& para, const std::string& sha) {
    if (de == para) return;
    std::vector<std::string> clones;
    try {
        auto st = registro.prepare("SELECT id FROM backup_destino WHERE ativo = 1 AND papel <> 'ORIGINAL'");
        while (st.step()) clones.push_back(st.columnText(0));
    } catch (...) { return; }
    for (const auto& clone : clones) {
        long long idExistente = 0;
        std::string deOriginal;
        {
            auto st = registro.prepare(
                "SELECT id, de FROM clone_move_pendente WHERE clone_id = ? AND tipo = ? AND para = ? ORDER BY id DESC LIMIT 1");
            st.bind(1, Value::of(clone));
            st.bind(2, Value::of(std::string(tipo)));
            st.bind(3, Value::of(de));
            if (st.step()) { idExistente = st.columnInt(0); deOriginal = st.columnText(1); }
        }
        if (idExistente != 0) {
            if (deOriginal == para)
                registro.run("DELETE FROM clone_move_pendente WHERE id = ?", {Value::of(idExistente)});
            else
                registro.run("UPDATE clone_move_pendente SET para = ? WHERE id = ?",
                                  {Value::of(para), Value::of(idExistente)});
        } else {
            registro.run(
                "INSERT INTO clone_move_pendente (clone_id, tipo, de, para, sha256, criado_em) VALUES (?, ?, ?, ?, ?, ?)",
                {Value::of(clone), Value::of(std::string(tipo)), Value::of(de), Value::of(para), Value::of(sha),
                 Value::of(matriz::model::agoraIso8601())});
        }
    }
}



juce::String caminhoFisicoDaPasta(matriz::db::Database& registro, const std::string& pastaId) {
    if (pastaId.empty()) return {};
    juce::StringArray segs;
    for (auto& [id, nome] : cadeiaAncestral(registro, pastaId)) segs.add(segmentoSeguro(nome, "Folder"));
    return segs.joinIntoString("/");
}

juce::String resolverNomeFinalBackup(const juce::File& arquivoOrigem, const std::string& nomeBaseMascara, bool usaEstruturaOriginal) {
    if (usaEstruturaOriginal)
        return arquivoOrigem.getFileName();

    juce::String nomeBase = juce::String(nomeBaseMascara).trim();
    juce::String extOriginal = arquivoOrigem.getFileExtension();

    if (extOriginal.isEmpty())
        return nomeBase;

    if (nomeBase.endsWithIgnoreCase(extOriginal))
        return nomeBase;

    return nomeBase + extOriginal;
}

std::string nivelHierarquiaToString(NivelHierarquia n) {
    switch (n) {
        case NivelHierarquia::Projeto: return "projeto";
        case NivelHierarquia::Ano: return "ano";
        case NivelHierarquia::TipoMidia: return "tipo_midia";
        case NivelHierarquia::TipoArquivo: return "tipo_arquivo";
        case NivelHierarquia::Origem: return "origem";
        case NivelHierarquia::Artista: return "artista";
        case NivelHierarquia::ContentType: return "content_type";
        case NivelHierarquia::Subject: return "subject";
        case NivelHierarquia::PastaManual: return "pasta_manual";
        case NivelHierarquia::EstruturaOriginal: return "estrutura_original";
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
    if (s == "content_type") return NivelHierarquia::ContentType;
    if (s == "subject") return NivelHierarquia::Subject;
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

// Identifica o destino em consolidacao_registro.destino_path: caminho
// absoluto normalizado da pasta de mídia passada a planejar/executar.
std::string chaveDestino(const juce::File& destino) {
    return destino.getFullPathName().trimCharactersAtEnd("/").toStdString();
}

ResultadoManifestoChecksums gerarManifestChecksums(matriz::db::Database& registro, const juce::File& destinoMedia,
                                                   const std::vector<ItemPlanejado>& itens,
                                                   const std::function<bool(int, int)>& aoProgredir) {
    ResultadoManifestoChecksums r;
    struct HashRegistro { int prioridade; std::string sha; };
    std::unordered_map<std::string, HashRegistro> doRegistro;
    if (destinoMedia.isDirectory()) {
        try {
            const std::string chave = chaveDestino(destinoMedia);
            const std::string destinoId = matriz::vault::destinationIdDaRaiz(destinoMedia.getParentDirectory());
            auto stmt = registro.prepare(
                "SELECT arquivo_id, caminho_relativo_destino, checksum_sha256, COALESCE(destino_path, ''), "
                "COALESCE(destino_id, '') FROM consolidacao_registro WHERE checksum_sha256 != '' "
                "AND (destino_path = ? OR destino_path = '' OR destino_path IS NULL "
                "     OR (? != '' AND COALESCE(destino_id, '') = ?)) ORDER BY consolidado_em");
            stmt.bind(1, Value::of(chave));
            stmt.bind(2, Value::of(destinoId));
            stmt.bind(3, Value::of(destinoId));
            while (stmt.step()) {
                const int prioridade = stmt.columnText(3) == chave ? 0
                                       : (!destinoId.empty() && stmt.columnText(4) == destinoId ? 1 : 2);
                auto& slot = doRegistro[stmt.columnText(0) + "\n" + stmt.columnText(1)];
                if (slot.sha.empty() || prioridade <= slot.prioridade) slot = {prioridade, stmt.columnText(2)};
            }
        } catch (...) {}
    }

    const int total = static_cast<int>(itens.size());
    int feito = 0;
    std::string semRegistro;
    for (const auto& item : itens) {
        if (aoProgredir && !aoProgredir(feito, total)) {
            r.cancelado = true;
            return r;
        }
        juce::String relPath = item.caminhoRelativoDestino;
        if (relPath.isEmpty()) relPath = item.nomeOriginal;
        // WINPORT: CaminhosBanco — relPath vem do banco/plano e é comparado como string com
        // consolidacao_registro.caminho_relativo_destino; no merge, normalizar os dois lados (doBanco/paraBanco).
        auto it = doRegistro.find(item.arquivoId + "\n" + relPath.toStdString());
        if (it != doRegistro.end()) {
            r.texto += it->second.sha + "  " + relPath.toStdString() + "\n";
            ++r.comHash;
        } else {
            semRegistro += "# not consolidated: " + relPath.toStdString() + "\n";
            ++r.semRegistro;
        }
        ++feito;
    }
    r.texto += semRegistro;
    if (aoProgredir) aoProgredir(total, total);
    return r;
}

PlanoConsolidacao planejarConsolidacao(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                        const juce::File& destino, const HierarquiaBackup& hierarquiaPedida,
                                        const RotuloTipoMidia& rotuloTipoMidia,
                                        ModoPrefixoArquivo modoPrefixo,
                                        const juce::String& prefixoCustomizado,
                                        bool autoResolverConflitos,
                                        bool forcarRebackup,
                                        bool organizarPorSource,
                                        bool paraExport,
                                        const std::string& mapaId) {
    MATRIZ_TRACE("consolidacao::planejarConsolidacao");
    PlanoConsolidacao plano;
    std::map<std::string, std::string> codigoPorVault;
    if (organizarPorSource) codigoPorVault = codigosDeSource(registro);
    // `destino` é <raiz do destino>/Media; o destination.json fica na raiz.
    const std::string destinoId = matriz::vault::destinationIdDaRaiz(destino.getParentDirectory());
    HierarquiaBackup hierarquia = hierarquiaPedida.empty() ? hierarquiaDoProjeto(registro) : hierarquiaPedida;
    auto rotuloTipo = rotuloTipoMidia ? rotuloTipoMidia
                                       : RotuloTipoMidia([](const std::string& t) { return juce::String(t); });

    std::string prefixoEfetivo;
    if (modoPrefixo == ModoPrefixoArquivo::Custom && prefixoCustomizado.trim().isNotEmpty()) {
        prefixoEfetivo = prefixoCustomizado.trim().toStdString();
    } else {
        auto stmtPref = registro.prepare("SELECT prefixo_nomenclatura FROM projeto LIMIT 1");
        if (stmtPref.step() && !stmtPref.columnIsNull(0)) {
            prefixoEfetivo = stmtPref.columnText(0);
        }
    }
    if (prefixoEfetivo.empty()) prefixoEfetivo = "BKR";

    bool usaEstruturaOriginal = std::find(hierarquia.begin(), hierarquia.end(),
                                          NivelHierarquia::EstruturaOriginal) != hierarquia.end();
    bool usaPastaManual = std::find(hierarquia.begin(), hierarquia.end(),
                                    NivelHierarquia::PastaManual) != hierarquia.end();

    // Fase 2: com folder map escolhido, PastaManual lê só as pastas DESSE mapa
    // (o item pode estar em pastas de vários mapas). Item sem pasta nesse mapa:
    // backup -> _SEM_PASTA na raiz do MAIN; export -> excluído e contado.
    const bool filtrarMapa = !mapaId.empty();
    const bool mapaEmUso = filtrarMapa && usaPastaManual && !usaEstruturaOriginal;
    const bool semPastaFisica = mapaEmUso && !paraExport;
    std::set<std::string> itensComMovimento;

    // Contador de sequência por pasta de destino (garante numeração sequencial por pasta final)
    std::map<std::string, int> seqPorDestino;

    // item (Duplicates): 'duplicata' já era documentado no schema como
    // "nunca copiado de novo" (conteúdo reconhecido, não reimportado), mas
    // esta consulta nunca filtrava por estado — um item validado como
    // duplicata pelo workspace de Duplicates continuava entrando em todo
    // Make Backup normalmente. Exclui aqui, sem apagar nada do catálogo
    // nem do disco: só tira da PRÓXIMA leva de backup.
    auto stmt = registro.prepare(
        std::string("SELECT i.id, COALESCE(aip.pasta_id, ''), i.codigo_acervo, i.titulo, i.tipo_midia, "
        "a.id, a.caminho_relativo, a.checksum_sha256, a.tamanho_bytes, "
        "i.dc_creator, i.collection_type, i.dc_subject, i.source_media, COALESCE(a.vault_id, '') "
        "FROM item i "
        "LEFT JOIN acervo_item_pasta aip ON aip.item_id = i.id ") +
        (filtrarMapa ? "AND aip.mapa_id = ? " : "") +
        "JOIN arquivo a ON a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = i.id "
        "                          ORDER BY eh_master DESC, id LIMIT 1) "
        "WHERE i.estado != 'duplicata' "
        "ORDER BY i.codigo_acervo");
    if (filtrarMapa) stmt.bind(1, Value::of(mapaId));

    // Fase 4: arquivo que o usuário deletou no MAIN EDIT MODE (foi pra
    // quarentena) não volta no backup seguinte — só se for restaurado.
    std::set<std::string> arquivosDeletadosDoMain;
    if (!paraExport) {
        try {
            auto stq = registro.prepare("SELECT arquivo_id FROM quarentena_item WHERE motivo = 'deletado' AND restaurado_em IS NULL");
            while (stq.step()) arquivosDeletadosDoMain.insert(stq.columnText(0));
        } catch (...) {}
    }

    std::map<std::string, std::vector<size_t>> indicesPorDestino; // caminho final -> índices em plano.itens, pra achar conflito
    std::unordered_set<std::string> itensProcessados;

    while (stmt.step()) {
        std::string itemId = stmt.columnText(0);
        if (!usaPastaManual || usaEstruturaOriginal) {
            if (!itensProcessados.insert(itemId).second)
                continue; // Na estrutura original ou por tipo/ano, cada item do projeto é copiado uma única vez
        }

        std::string pastaIdCandidata = stmt.columnText(1);
        if (pastaOuAncestralDesativada(registro, pastaIdCandidata)) continue; // pasta desabilitada na TREEMAP — bypass total
        if (mapaEmUso && paraExport && pastaIdCandidata.empty()) {
            plano.semPastaExcluidos.insert(itemId);  // export por mapa: sem pasta = fora
            continue;
        }

        ItemPlanejado ip;
        ip.itemId = std::move(itemId);
        ip.pastaId = std::move(pastaIdCandidata);
        ip.codigoAcervo = stmt.columnText(2);
        std::string titulo = stmt.columnText(3);
        std::string tipoMidia = stmt.columnText(4);
        ip.arquivoId = stmt.columnText(5);
        if (arquivosDeletadosDoMain.count(ip.arquivoId)) continue;
        juce::String caminhoRelativoOrigem = stmt.columnText(6);
        std::string checksumAtual = stmt.columnText(7);
        std::string dcCreatorAtual = stmt.columnIsNull(9) ? std::string() : stmt.columnText(9);
        std::string collectionTypeAtual = stmt.columnIsNull(10) ? std::string() : stmt.columnText(10);
        std::string dcSubjectAtual = stmt.columnIsNull(11) ? std::string() : stmt.columnText(11);
        std::string sourceMediaAtual = stmt.columnIsNull(12) ? std::string() : stmt.columnText(12);
        juce::String codigoSource;
        if (organizarPorSource) {
            auto itCod = codigoPorVault.find(stmt.columnText(13));
            if (itCod != codigoPorVault.end()) codigoSource = juce::String(itCod->second);
        }

        // Origem, não MAIN: o nome planejado parte do nome ORIGINAL, não do
        // nome (com máscara) que a cópia ganhou no backup.
        auto resolvido = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto,
                                                        matriz::vault::Preferencia::Origem);
        juce::File arquivoNoProjeto = resolvido ? *resolvido : pastaProjeto.getChildFile(caminhoRelativoOrigem);
        ip.nomeOriginal = arquivoNoProjeto.getFileName();

        juce::int64 dbSize = stmt.columnIsNull(8) ? 0 : static_cast<juce::int64>(stmt.columnInt(8));
        if (dbSize > 0) {
            ip.tamanhoBytes = dbSize;
        } else {
            ip.tamanhoBytes = arquivoNoProjeto.existsAsFile() ? arquivoNoProjeto.getSize() : 0;
        }

        auto cadeia = cadeiaAncestral(registro, ip.pastaId);

        matriz::consolidacao::ContextoMascara ctx;
        ctx.prefixo = prefixoEfetivo;
        ctx.codigoAcervo = ip.codigoAcervo;
        ctx.titulo = titulo;
        ctx.tipoMidia = tipoMidia;
        ctx.nomeOriginalSemExtensao = arquivoNoProjeto.getFileNameWithoutExtension().toStdString();
        {
            auto stmtProjeto = registro.prepare("SELECT nome FROM projeto LIMIT 1");
            ctx.nomeAcervo = stmtProjeto.step() ? stmtProjeto.columnText(0) : pastaProjeto.getFileNameWithoutExtension().toStdString();
        }
        ctx.nomePasta = cadeia.empty() ? std::string() : cadeia.back().second.toStdString();
        ctx.camposFicha = camposFichaDoItem(registro, ip.itemId);
        // dc_creator e collection_type são colunas de item, não item_campo
        // (salvarMetadado grava direto na coluna) — camposFichaDoItem só
        // veria um valor velho da leitura técnica do ingest. Sobrescreve
        // com o valor atual da coluna pra CREATOR/CONTENT (itens 1 e 2 da
        // correção de UI) refletirem a última edição da ficha.
        if (!dcCreatorAtual.empty()) ctx.camposFicha["dc_creator"] = dcCreatorAtual;
        if (!collectionTypeAtual.empty()) ctx.camposFicha["collection_type"] = collectionTypeAtual;
        if (!dcSubjectAtual.empty()) ctx.camposFicha["dc_subject"] = dcSubjectAtual;
        if (!sourceMediaAtual.empty()) ctx.camposFicha["source_media"] = sourceMediaAtual;
        bool temAnoNaFicha = false;
        {
            auto itAno = ctx.camposFicha.find("ano");
            if (itAno != ctx.camposFicha.end() && !itAno->second.empty()) {
                std::string anoTratado = extrairAnoDeTexto(itAno->second);
                if (!anoTratado.empty()) {
                    ctx.camposFicha["ano"] = anoTratado;
                    temAnoNaFicha = true;
                }
            }
            if (!temAnoNaFicha) {
                std::string anoInferido = resolverAnoEfetivo(registro, ip.itemId, ip.arquivoId, arquivoNoProjeto);
                if (!anoInferido.empty()) {
                    ctx.camposFicha["ano"] = anoInferido;
                }
            }
        }

        juce::String extensao = arquivoNoProjeto.getFileExtension(); // já inclui o "."

        // Caminho de pastas montado nível a nível conforme a hierarquia escolhida
        juce::StringArray segmentosPasta;
        for (auto nivel : hierarquia) {
            switch (nivel) {
                case NivelHierarquia::Projeto:
                    segmentosPasta.add(segmentoSeguro(ctx.nomeAcervo, "Project"));
                    break;
                case NivelHierarquia::Ano: {
                    juce::String ano = temAnoNaFicha ? juce::String(ctx.camposFicha["ano"]).trim() : juce::String();
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
                    // Renomeado pra SOURCE MEDIUM na UI (item 1 da correção
                    // "BACKUP e INTAKE"): organiza pelo mesmo source_media
                    // (ORIGINAL SOURCE MEDIUM da ficha / "Source media..." do
                    // INTAKE), não mais pelo flag simples Digital/Analógico.
                    // source_media grava um JSON (OriginalSourceMediumInfo::
                    // serialize, em Ui/OriginalSourceMedium.cpp — não incluído
                    // aqui de propósito, esse .cpp não linka no alvo de
                    // selftest console); só a chave "medium" interessa pro
                    // nome da pasta, o resto (device, speed, etc.) fica na ficha.
                    auto it = ctx.camposFicha.find("source_media");
                    juce::String medium;
                    if (it != ctx.camposFicha.end() && !it->second.empty()) {
                        juce::var parsed = juce::JSON::parse(juce::String(it->second));
                        if (auto* obj = parsed.getDynamicObject()) {
                            if (obj->hasProperty("medium")) medium = obj->getProperty("medium").toString();
                        } else {
                            medium = juce::String(it->second); // valor legado, não-JSON
                        }
                    }
                    segmentosPasta.add(segmentoSeguro(medium.trim(), "No source medium"));
                    break;
                }
                case NivelHierarquia::Artista: {
                    // Renomeado pra CREATOR na UI (item 2 da correção de UI):
                    // organiza pelo mesmo dc_creator da ficha, não mais por
                    // artista_principal.
                    auto it = ctx.camposFicha.find("dc_creator");
                    juce::String criador = it == ctx.camposFicha.end() ? juce::String() : juce::String(it->second).trim();
                    segmentosPasta.add(segmentoSeguro(criador, "No creator"));
                    break;
                }
                case NivelHierarquia::ContentType: {
                    auto it = ctx.camposFicha.find("collection_type");
                    juce::String content = it == ctx.camposFicha.end() ? juce::String() : juce::String(it->second).trim();
                    segmentosPasta.add(segmentoSeguro(content, "No content"));
                    break;
                }
                case NivelHierarquia::Subject: {
                    // Substitui MANUAL FOLDERS no editor visual (item da 4ª
                    // correção de UI): organiza pelo mesmo dc_subject da
                    // ficha (ASSET & USER / DUBLIN CORE, já sincronizados).
                    auto it = ctx.camposFicha.find("dc_subject");
                    juce::String assunto = it == ctx.camposFicha.end() ? juce::String() : juce::String(it->second).trim();
                    segmentosPasta.add(segmentoSeguro(assunto, "No subject"));
                    break;
                }
                case NivelHierarquia::PastaManual:
                    // A subárvore que o operador montou à mão na árvore BACKUP
                    for (auto& [id, nome] : cadeia) segmentosPasta.add(segmentoSeguro(nome, "Folder"));
                    if (cadeia.empty() && semPastaFisica) segmentosPasta.add(kPastaSemPasta);
                    break;
                case NivelHierarquia::EstruturaOriginal: {
                    juce::File fRel(caminhoRelativoOrigem);
                    juce::File dirPai = fRel.getParentDirectory();
                    if (dirPai.getFullPathName() != "." && dirPai.getFullPathName().isNotEmpty()) {
                        juce::StringArray segs;
                        segs.addTokens(dirPai.getFullPathName(), "/\\", "");
                        for (auto& s : segs)
                            if (s.trim().isNotEmpty()) segmentosPasta.add(segmentoSeguro(s, "Folder"));
                    }
                    break;
                }
            }
        }

        // Etapa 5: na estrutura original cada SOURCE tem a sua pasta raiz.
        if (usaEstruturaOriginal && codigoSource.isNotEmpty()) segmentosPasta.insert(0, codigoSource);

        juce::String pastaDestinoStr = segmentosPasta.joinIntoString("/");
        ctx.seq = ++seqPorDestino[pastaDestinoStr.toStdString()];

        juce::String nomeArquivoFinal;
        bool manteveNomeOriginal = usaEstruturaOriginal;
        if (modoPrefixo == ModoPrefixoArquivo::Nenhum) {
            // "NO PREFIX" não quer dizer "nome físico original": o nome do
            // arquivo copiado é comandado pelo título da ficha de metadados
            // (renomear na ficha renomeia no backup). Só quando o item ainda
            // não tem título é que o nome físico de origem prevalece — e a
            // hierarquia "estrutura original" continua preservando o master,
            // via resolverNomeFinalBackup.
            juce::String tituloBase = juce::String(resolverMascara("{titulo}", ctx)).trim();
            if (tituloBase.isEmpty()) manteveNomeOriginal = true;
            nomeArquivoFinal = tituloBase.isEmpty()
                ? arquivoNoProjeto.getFileName()
                : resolverNomeFinalBackup(arquivoNoProjeto, tituloBase.toStdString(), usaEstruturaOriginal);
        } else {
            juce::String mascara = (modoPrefixo == ModoPrefixoArquivo::Custom && prefixoCustomizado.trim().isNotEmpty())
                ? juce::String("{prefix}_{name}_{number}_{year}")
                : (modoPrefixo == ModoPrefixoArquivo::Auto ? juce::String("{prefix}_{name}_{number}_{year}") : mascaraEfetiva(registro, cadeia));

            std::string nomeBase = resolverMascara(mascara, ctx);
            nomeArquivoFinal = resolverNomeFinalBackup(arquivoNoProjeto, nomeBase, usaEstruturaOriginal);
        }
        // Etapa 5: nome original + sufixo do SOURCE (DSC_0001_S01.JPG) — dois
        // cartões com o mesmo nome de arquivo não colidem no MAIN.
        if (manteveNomeOriginal && codigoSource.isNotEmpty()) {
            juce::File fNome(nomeArquivoFinal);
            nomeArquivoFinal = fNome.getFileNameWithoutExtension() + "_" + codigoSource + fNome.getFileExtension();
        }
                juce::String caminhoRelDestino =
            (segmentosPasta.isEmpty() ? juce::String() : pastaDestinoStr + "/") + nomeArquivoFinal;

        // Auto-resolver conflito de nomes se ativado
        if (autoResolverConflitos && indicesPorDestino.find(caminhoRelDestino.toStdString()) != indicesPorDestino.end()) {
            juce::File fNome(nomeArquivoFinal);
            juce::String baseSemExt = fNome.getFileNameWithoutExtension();
            juce::String ext = fNome.getFileExtension();
            int tentativa = 1;
            juce::String novoCaminhoRelDestino;
            do {
                juce::String novoNome = baseSemExt + "_" + juce::String(tentativa++) + ext;
                novoCaminhoRelDestino = (segmentosPasta.isEmpty() ? juce::String() : pastaDestinoStr + "/") + novoNome;
            } while (indicesPorDestino.find(novoCaminhoRelDestino.toStdString()) != indicesPorDestino.end());

            caminhoRelDestino = novoCaminhoRelDestino;
            plano.conflitosAutoResolvidos++;
        }

        ip.caminhoRelativoDestino = caminhoRelDestino;

        // O destino só cresce (modelo SOURCE/MAIN/CLONE, etapa 4): um arquivo que
        // já tem cópia registrada NESTE destino fica onde está — mesmo que a
        // máscara, a hierarquia ou a pasta do MAPA produzissem outro caminho hoje.
        // Busca pelo arquivo (não pela pasta): mover o item de pasta no MAPA não
        // pode gerar uma segunda cópia. Registros deste destino por id/caminho,
        // ou legados (sem destino gravado). EXPORT não herda nada disso.
        if (!paraExport) {
            auto stmtJa = registro.prepare(
                "SELECT caminho_relativo_destino FROM consolidacao_registro WHERE arquivo_id = ? "
                "AND (destino_path = ? OR destino_path = '' OR destino_path IS NULL "
                "     OR (? != '' AND COALESCE(destino_id, '') = ?)) "
                "ORDER BY (pasta_id = ?) DESC, (item_id = ?) DESC LIMIT 1");
            stmtJa.bind(1, Value::of(ip.arquivoId));
            stmtJa.bind(2, Value::of(chaveDestino(destino)));
            stmtJa.bind(3, Value::of(destinoId));
            stmtJa.bind(4, Value::of(destinoId));
            stmtJa.bind(5, Value::of(ip.pastaId));
            stmtJa.bind(6, Value::of(ip.itemId));
            if (stmtJa.step() && !stmtJa.columnIsNull(0) && !stmtJa.columnText(0).empty()) {
                const juce::String registrado = juce::String::fromUTF8(stmtJa.columnText(0).c_str());
                // Fase 2: estava em _SEM_PASTA e agora tem pasta no mapa do MAIN ->
                // oferece MOVER (nunca recopiar). Só o primeiro registro do item
                // leva o move; se o item entrou em mais de uma pasta, os demais
                // ficam como já consolidados (uma única cópia física no MAIN).
                if (semPastaFisica && !ip.pastaId.empty() &&
                    registrado.startsWith(juce::String(kPastaSemPasta) + "/")) {
                    if (itensComMovimento.insert(ip.itemId).second) {
                        ip.moverDe = registrado;
                        plano.movimentosSemPasta.push_back(ip);  // ip.caminhoRelativoDestino é o NOVO caminho
                    }
                    ip.jaConsolidado = true;
                } else {
                    caminhoRelDestino = registrado;
                    ip.caminhoRelativoDestino = caminhoRelDestino;
                    // Sumiu do disco: recopia NO MESMO caminho. Forçado: idem.
                    ip.jaConsolidado = !forcarRebackup && destino.getChildFile(caminhoRelDestino).existsAsFile();
                }
            } else {
                ip.jaConsolidado = false;
            }
        } else {
            ip.jaConsolidado = false;
        }

        indicesPorDestino[caminhoRelDestino.toStdString()].push_back(plano.itens.size());
        plano.itens.push_back(std::move(ip));
    }

    for (auto& [caminho, indices] : indicesPorDestino) {
        if (indices.size() <= 1) continue;
        std::unordered_set<std::string> idsDistintos;
        for (auto idx : indices) idsDistintos.insert(plano.itens[idx].itemId);
        if (idsDistintos.size() <= 1) continue;

        plano.nomesEmConflito.push_back(juce::String(caminho));
        for (auto idx : indices) plano.itens[idx].emConflito = true;
    }

    auto stmtNaoOrg = registro.prepare(
        filtrarMapa ? "SELECT COUNT(*) FROM item WHERE id NOT IN (SELECT item_id FROM acervo_item_pasta WHERE mapa_id = ?)"
                    : "SELECT COUNT(*) FROM item WHERE id NOT IN (SELECT item_id FROM acervo_item_pasta)");
    if (filtrarMapa) stmtNaoOrg.bind(1, Value::of(mapaId));
    if (stmtNaoOrg.step()) plano.itensNaoOrganizados = static_cast<int>(stmtNaoOrg.columnInt(0));

    for (auto& ip : plano.itens)
        if (!ip.jaConsolidado && !ip.emConflito) plano.espacoNecessarioBytes += ip.tamanhoBytes;

    plano.espacoDisponivelBytes = destino.getBytesFreeOnVolume();

    return plano;
}

bool codigoDeSourceValido(const juce::String& codigo) {
    if (codigo.isEmpty() || codigo.length() > 12) return false;
    for (auto c : codigo)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

std::map<std::string, std::string> codigosDeSource(matriz::db::Database& registro) {
    std::map<std::string, std::string> out;
    std::set<std::string> usados;  // comparação sem caixa
    try {
        auto st = registro.prepare("SELECT id, codigo FROM vault WHERE COALESCE(codigo, '') <> ''");
        while (st.step()) {
            out[st.columnText(0)] = st.columnText(1);
            usados.insert(juce::String(st.columnText(1)).toUpperCase().toStdString());
        }
        auto sv = registro.prepare("SELECT a.vault_id FROM arquivo a WHERE COALESCE(a.vault_id, '') <> '' "
                                   "GROUP BY a.vault_id ORDER BY MIN(a.criado_em), a.vault_id");
        int n = 1;
        while (sv.step()) {
            const std::string id = sv.columnText(0);
            if (out.count(id)) continue;
            std::string cod;
            do {
                cod = juce::String::formatted("S%02d", n++).toStdString();
            } while (usados.count(cod));
            usados.insert(cod);
            out[id] = cod;
        }
    } catch (...) {}
    return out;
}

ResultadoMovimentos executarMovimentosSemPasta(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                                const juce::File& destino,
                                                const std::vector<ItemPlanejado>& movimentos,
                                                const AoProgredir& aoProgredir) {
    ResultadoMovimentos resultado;
    const int total = static_cast<int>(movimentos.size());
    int feito = 0;
    for (const auto& mv : movimentos) {
        if (aoProgredir && !aoProgredir(feito, total)) {
            resultado.cancelado = true;
            break;
        }
        ++feito;
        try {
            if (mv.moverDe.isEmpty()) continue;
            juce::File origem = destino.getChildFile(mv.moverDe);
            juce::File alvo = destino.getChildFile(mv.caminhoRelativoDestino);
            const bool origemExiste = origem.existsAsFile();
            const bool alvoExiste = alvo.existsAsFile();
            if (!origemExiste && !alvoExiste)
                throw std::runtime_error("file not found in the MAIN (_SEM_PASTA)");
            if (origemExiste && alvoExiste)
                throw std::runtime_error("target already exists, not overwritten: " + mv.caminhoRelativoDestino.toStdString());
            if (origemExiste) {
                alvo.getParentDirectory().createDirectory();
                if (!origem.moveFileTo(alvo) || !alvo.existsAsFile())
                    throw std::runtime_error("move failed: " + mv.caminhoRelativoDestino.toStdString());
                // Capa que viajou junto da cópia (mesmo nome-base, extensão da capa).
                try {
                    auto stCapa = registro.prepare(
                        "SELECT caminho_relativo FROM arquivo WHERE item_id = ? AND papel = 'capa_frente' LIMIT 1");
                    stCapa.bind(1, Value::of(mv.itemId));
                    if (stCapa.step()) {
                        const juce::String ext = juce::File(juce::String::fromUTF8(stCapa.columnText(0).c_str())).getFileExtension();
                        juce::File capaOrigem = origem.getParentDirectory().getChildFile(origem.getFileNameWithoutExtension() + ext);
                        juce::File capaAlvo = alvo.getParentDirectory().getChildFile(alvo.getFileNameWithoutExtension() + ext);
                        if (capaOrigem.existsAsFile() && !capaAlvo.existsAsFile()) capaOrigem.moveFileTo(capaAlvo);
                    }
                } catch (...) {}
            }
            // (origem ausente + alvo presente = move já feito, registro atrasado: só atualiza o banco.)
            std::string shaMovido;
            try {
                auto stSha = registro.prepare("SELECT checksum_sha256 FROM consolidacao_registro WHERE item_id = ? AND arquivo_id = ? "
                                              "AND caminho_relativo_destino = ? LIMIT 1");
                stSha.bind(1, Value::of(mv.itemId));
                stSha.bind(2, Value::of(mv.arquivoId));
                stSha.bind(3, Value::of(mv.moverDe.toStdString()));
                if (stSha.step()) shaMovido = stSha.columnText(0);
            } catch (...) {}
            try {
                registro.run("UPDATE consolidacao_registro SET pasta_id = ?, caminho_relativo_destino = ? "
                             "WHERE item_id = ? AND arquivo_id = ? AND caminho_relativo_destino = ?",
                             {Value::of(mv.pastaId), Value::of(mv.caminhoRelativoDestino.toStdString()),
                              Value::of(mv.itemId), Value::of(mv.arquivoId), Value::of(mv.moverDe.toStdString())});
            } catch (...) {
                // Já existe registro (item, pasta nova, arquivo, destino): fica só ele.
                registro.run("DELETE FROM consolidacao_registro WHERE item_id = ? AND arquivo_id = ? "
                             "AND caminho_relativo_destino = ?",
                             {Value::of(mv.itemId), Value::of(mv.arquivoId), Value::of(mv.moverDe.toStdString())});
            }
            try {
                anotarMovePendentePraClones(registro, "arquivo", mv.moverDe.toStdString(),
                                            mv.caminhoRelativoDestino.toStdString(), shaMovido);
            } catch (...) {}
            matriz::model::ProjectLog pLog(pastaProjeto);
            pLog.appendEntry("Media Moved Out Of _SEM_PASTA",
                             {"Item: " + juce::String::fromUTF8(mv.codigoAcervo.c_str()), "From: " + mv.moverDe,
                              "To: " + mv.caminhoRelativoDestino});
            ++resultado.movidos;
        } catch (const std::exception& e) {
            resultado.falhas.push_back(mv.codigoAcervo + ": " + e.what());
        }
    }
    return resultado;
}

ResultadoExport executarExport(matriz::db::Database& registro, const juce::File& pastaProjeto,
                               const juce::File& destinoExport, const PlanoConsolidacao& plano,
                               const AoProgredir& aoProgredir, const std::set<std::string>& itensComMarcaDagua,
                               bool embutir) {
    ResultadoExport resultado;
#if !JUCE_MODULE_AVAILABLE_juce_gui_basics
    (void)itensComMarcaDagua;
#endif
    const int total = static_cast<int>(plano.itens.size());
    int processados = 0;
    for (const auto& ip : plano.itens) {
        if (aoProgredir && !aoProgredir(processados, total)) {
            resultado.cancelado = true;
            break;
        }
        ++processados;
        if (ip.emConflito) {
            resultado.falhas.push_back(ip.codigoAcervo + ": name conflict at destination");
            continue;
        }
        try {
            // A origem é sempre o MAIN (ou um CLONE, espelho dele): se a
            // leitura cai no mesmo arquivo que a ORIGEM daria, não há cópia
            // no backup — o SOURCE nunca é fonte de EXPORT.
            auto noBackup = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto);
            auto naOrigem = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto,
                                                           matriz::vault::Preferencia::Origem);
            if (!noBackup || (naOrigem && *naOrigem == *noBackup)) {
                ++resultado.foraDoMain;
                continue;
            }
            juce::File destinoArquivo = destinoExport.getChildFile(ip.caminhoRelativoDestino);
            if (destinoArquivo.existsAsFile())
                throw std::runtime_error("already exists in the export folder: " +
                                         destinoArquivo.getFullPathName().toStdString());
            destinoArquivo.getParentDirectory().createDirectory();
            if (!noBackup->copyFileTo(destinoArquivo) || destinoArquivo.getSize() != noBackup->getSize())
                throw std::runtime_error("copy failed: " + destinoArquivo.getFullPathName().toStdString());
#if JUCE_MODULE_AVAILABLE_juce_gui_basics
            if (itensComMarcaDagua.count(ip.itemId) > 0) {
                auto cfgWm = ui::ProjetoAberto::carregarConfiguracaoWatermarkDePasta(pastaProjeto);
                if (cfgWm.valida() &&
                    ui::BatchWatermarkDialog::aplicarMarcaDaguaEmArquivo(destinoArquivo, destinoArquivo, cfgWm))
                    ++resultado.comMarcaDagua;
            }
#endif
            if (embutir) {
                if (destinoArquivo.hasFileExtension("wav")) {
                    auto marcadores = marcadoresDoItem(registro, ip.itemId);
                    if (!marcadores.empty()) embutirMarcadoresEmWav(destinoArquivo, marcadores);
                }
                // Embed onde o formato aceita; sidecar XMP onde não aceita (etapa 8).
                if (embutirMetadadosNoArquivo(destinoArquivo, coletarMetadadosDoItem(registro, ip.itemId)) ==
                    StatusEmbedding::Unsupported)
                    escreverSidecarAvulso(registro, ip.itemId, destinoArquivo);
            }
            ++resultado.copiados;
        } catch (const std::exception& e) {
            resultado.falhas.push_back(ip.codigoAcervo + ": " + e.what());
        }
    }
    try {
        matriz::model::ProjectLog(pastaProjeto).appendEntry("Export Created", {
            "Destination: " + destinoExport.getFullPathName(),
            "Files: " + juce::String(resultado.copiados) + " (not yet in MAIN, skipped: " +
                juce::String(resultado.foraDoMain) + ", failures: " + juce::String((int) resultado.falhas.size()) + ")",
            juce::String("Embedded metadata: ") + (embutir ? "yes" : "no") +
                ", watermarked: " + juce::String(resultado.comMarcaDagua)});
    } catch (...) {}
    return resultado;
}

ResultadoConsolidacao executarConsolidacao(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                            const juce::File& destino, const PlanoConsolidacao& plano,
                                            const AoProgredir& aoProgredir,
                                            const std::set<std::string>& itensMarcadosWatermark,
                                            bool embutirNaCopia) {
    ResultadoConsolidacao resultado;
    resultado.totalPlanejado = static_cast<int>(plano.itens.size());
    std::string agora = matriz::model::agoraIso8601();
    // `destino` é <raiz do destino>/Media; o destination.json fica na raiz.
    const std::string destinoId = matriz::vault::destinationIdDaRaiz(destino.getParentDirectory());
    // Código do SOURCE fica fixo quando o primeiro arquivo dele entra no MAIN.
    const auto codigosAtuais = codigosDeSource(registro);
#if !JUCE_MODULE_AVAILABLE_juce_gui_basics
    (void)itensMarcadosWatermark;
#endif

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
            auto resolvido = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto);
            if (!resolvido)
                throw std::runtime_error("master ausente em disco (Vault offline ou arquivo movido/removido): " +
                                          ip.codigoAcervo);
            juce::File origem = *resolvido;

            juce::File destinoArquivo = destino.getChildFile(ip.caminhoRelativoDestino);
            // A leitura resolve MAIN primeiro: numa reconsolidação forçada no
            // próprio MAIN a "origem" é o arquivo de destino — copiar por cima
            // de si mesmo truncaria o arquivo. Já está lá: conta como pulado.
            if (origem == destinoArquivo) {
                ++resultado.pulados;
                continue;
            }
            destinoArquivo.getParentDirectory().createDirectory();

            // Se o caminho mudou mas o arquivo antigo já existia no destino com o mesmo tamanho,
            // podemos apenas mover/renomear em vez de recopiar:
            bool movidoLocalmente = false;
            try {
                auto stmtAntigo = registro.prepare(
                    "SELECT caminho_relativo_destino, checksum_sha256 FROM consolidacao_registro WHERE item_id = ? AND pasta_id = ? AND arquivo_id = ? LIMIT 1");
                stmtAntigo.bind(1, Value::of(ip.itemId));
                stmtAntigo.bind(2, Value::of(ip.pastaId));
                stmtAntigo.bind(3, Value::of(ip.arquivoId));
                if (stmtAntigo.step()) {
                    std::string antRel = stmtAntigo.columnText(0);
                    if (!antRel.empty() && antRel != ip.caminhoRelativoDestino.toStdString()) {
                        juce::File arqAntigo = destino.getChildFile(antRel);
                        if (arqAntigo.existsAsFile() && arqAntigo.getSize() == origem.getSize()) {
                            destinoArquivo.deleteFile();
                            if (arqAntigo.moveFileTo(destinoArquivo)) {
                                movidoLocalmente = true;
                                matriz::model::ProjectLog pLog(pastaProjeto);
                                pLog.appendEntry("Media Relocated", {"From: " + juce::String::fromUTF8(antRel.c_str()), "To: " + ip.caminhoRelativoDestino});
                            }
                        }
                    }
                }
            } catch (...) {}

            if (!movidoLocalmente) {
                destinoArquivo.deleteFile(); // reconsolidação: substitui a cópia anterior, nunca acumula lixo

                if (!origem.copyFileTo(destinoArquivo))
                    throw std::runtime_error("falha ao copiar pra " + destinoArquivo.getFullPathName().toStdString());
            }

            if (!destinoArquivo.existsAsFile()) throw std::runtime_error("cópia não existe depois de copiar");
            if (destinoArquivo.getSize() != origem.getSize())
                throw std::runtime_error("tamanho da cópia não bate com o original");

            // Apply watermark if item is marked for watermark and configured
#if JUCE_MODULE_AVAILABLE_juce_gui_basics
            if (itensMarcadosWatermark.count(ip.itemId) > 0) {
                auto cfgWm = ui::ProjetoAberto::carregarConfiguracaoWatermarkDePasta(pastaProjeto);
                if (cfgWm.valida()) {
                    ui::BatchWatermarkDialog::aplicarMarcaDaguaEmArquivo(destinoArquivo, destinoArquivo, cfgWm);
                }
            }
#endif

            // Metadados e marcadores dentro da cópia: só no primeiro backup
            // (antes de o MAIN existir). Em "Adicionar ao MAIN" a cópia fica
            // byte a byte igual ao original — edições vivem no banco/sidecar.
            if (embutirNaCopia) {
                if (destinoArquivo.hasFileExtension("wav")) {
                    auto marcadores = marcadoresDoItem(registro, ip.itemId);
                    if (!marcadores.empty()) {
                        embutirMarcadoresEmWav(destinoArquivo, marcadores);
                        ++resultado.arquivosComMarcadorEmbutido;
                    }
                }
                auto meta = coletarMetadadosDoItem(registro, ip.itemId);
                embutirMetadadosNoArquivo(destinoArquivo, meta);
            }

            // Compute FINAL SHA256 of delivered backup bytes AFTER all modifications
            matriz::ingest::Checksums checksumCopia = matriz::ingest::calcularChecksums(destinoArquivo);

            std::string destPathStr = chaveDestino(destino);
            try {
                registro.run(
                    "INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                    "checksum_sha256, consolidado_em, destino_path) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(item_id, pasta_id, arquivo_id, destino_path) DO UPDATE SET "
                    "caminho_relativo_destino = excluded.caminho_relativo_destino, "
                    "checksum_sha256 = excluded.checksum_sha256, consolidado_em = excluded.consolidado_em",
                    {Value::of(matriz::model::novoUuid()), Value::of(ip.itemId), Value::of(ip.pastaId), Value::of(ip.arquivoId),
                     Value::of(ip.caminhoRelativoDestino.toStdString()), Value::of(checksumCopia.sha256), Value::of(agora),
                     Value::of(destPathStr)});
            } catch (...) {
                try {
                    // Fallback para esquema legado sem destino_path no UNIQUE
                    registro.run(
                        "INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                        "checksum_sha256, consolidado_em, destino_path) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(item_id, pasta_id, arquivo_id) DO UPDATE SET "
                        "caminho_relativo_destino = excluded.caminho_relativo_destino, "
                        "checksum_sha256 = excluded.checksum_sha256, consolidado_em = excluded.consolidado_em, "
                        "destino_path = excluded.destino_path",
                        {Value::of(matriz::model::novoUuid()), Value::of(ip.itemId), Value::of(ip.pastaId), Value::of(ip.arquivoId),
                         Value::of(ip.caminhoRelativoDestino.toStdString()), Value::of(checksumCopia.sha256), Value::of(agora),
                         Value::of(destPathStr)});
                } catch (...) {
                    // Último recurso: sem coluna destino_path
                    registro.run(
                        "INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                        "checksum_sha256, consolidado_em) VALUES (?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(item_id, pasta_id, arquivo_id) DO UPDATE SET "
                        "caminho_relativo_destino = excluded.caminho_relativo_destino, "
                        "checksum_sha256 = excluded.checksum_sha256, consolidado_em = excluded.consolidado_em",
                        {Value::of(matriz::model::novoUuid()), Value::of(ip.itemId), Value::of(ip.pastaId), Value::of(ip.arquivoId),
                         Value::of(ip.caminhoRelativoDestino.toStdString()), Value::of(checksumCopia.sha256), Value::of(agora)});
                }
            }
            // Identidade do destino pelo destination_id (o caminho muda quando
            // o disco monta com outro nome) — é o que o resolvedor usa.
            if (!destinoId.empty()) {
                try {
                    registro.run("UPDATE consolidacao_registro SET destino_id = ? WHERE item_id = ? AND pasta_id = ? "
                                 "AND arquivo_id = ? AND COALESCE(destino_path, '') = ?",
                                 {Value::of(destinoId), Value::of(ip.itemId), Value::of(ip.pastaId),
                                  Value::of(ip.arquivoId), Value::of(destPathStr)});
                } catch (...) {}
            }

            // Primeiro arquivo deste SOURCE no MAIN: o código (S01…) fica fixo.
            try {
                auto sv = registro.prepare("SELECT COALESCE(vault_id, '') FROM arquivo WHERE id = ?");
                sv.bind(1, Value::of(ip.arquivoId));
                if (sv.step()) {
                    auto itCod = codigosAtuais.find(sv.columnText(0));
                    if (itCod != codigosAtuais.end())
                        registro.run("UPDATE vault SET codigo = ? WHERE id = ? AND COALESCE(codigo, '') = ''",
                                     {Value::of(itCod->second), Value::of(itCod->first)});
                }
            } catch (...) {}

            // -------------------------------------------------------------------
            // Preservation events — PREMIS BACKUP_CREATED + BACKUP_VERIFIED.
            // BACKUP_VERIFIED:SUCCESS é o único critério para backup_status=OK.
            // Erros silenciados: falha de preservação não aborta o backup.
            // -------------------------------------------------------------------
            try {
                // Lê SHA-256 original do banco para comparação
                std::string sha256Original;
                {
                    auto stmtSha = registro.prepare(
                        "SELECT IFNULL(checksum_sha256,'') FROM arquivo WHERE id = ? LIMIT 1");
                    stmtSha.bind(1, Value::of(ip.arquivoId));
                    if (stmtSha.step()) sha256Original = stmtSha.columnText(0);
                }

                std::string destStr = destinoArquivo.getFullPathName().toStdString();

                // BACKUP_CREATED — sempre, independente da comparação
                preservation::registrarEvento(
                    registro, ip.itemId, ip.arquivoId,
                    preservation::EventType::BackupCreated,
                    "Destination: " + destStr,
                    preservation::Outcome::Success, {},
                    "bkr-agent-sistema");

                // BACKUP_VERIFIED — compara hash da cópia com hash original
                bool hashBate = !sha256Original.empty() && (checksumCopia.sha256 == sha256Original);
                if (hashBate) {
                    preservation::registrarEvento(
                        registro, ip.itemId, ip.arquivoId,
                        preservation::EventType::BackupVerified,
                        "Destination: " + destStr +
                        " | SHA-256: " + checksumCopia.sha256,
                        preservation::Outcome::Success, {},
                        "bkr-agent-sistema");
                } else {
                    std::string detail = sha256Original.empty()
                        ? "No baseline SHA-256 to compare against"
                        : "Expected: " + sha256Original + " | Got: " + checksumCopia.sha256;
                    preservation::registrarEvento(
                        registro, ip.itemId, ip.arquivoId,
                        preservation::EventType::BackupVerified,
                        "Destination: " + destStr,
                        preservation::Outcome::Failure,
                        detail,
                        "bkr-agent-sistema");
                }
            } catch (...) {}

            // Capa junto da cópia (item 9).
            {
                auto stmtCapa = registro.prepare(
                    "SELECT id FROM arquivo WHERE item_id = ? AND papel = 'capa_frente' LIMIT 1");
                stmtCapa.bind(1, Value::of(ip.itemId));
                if (stmtCapa.step()) {
                    auto capaResolvida = matriz::vault::resolverArquivo(registro, stmtCapa.columnText(0), pastaProjeto);
                    if (capaResolvida && capaResolvida->existsAsFile()) {
                        juce::File capa = *capaResolvida;
                        juce::File destinoCapa = destinoArquivo.getParentDirectory().getChildFile(
                            destinoArquivo.getFileNameWithoutExtension() + capa.getFileExtension());
                        destinoCapa.deleteFile();
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

    return resultado;
}

void sincronizarNomeDeBackupAposRenomear(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                          const std::string& itemId, const std::string& tituloAntigo,
                                          const std::string& tituloNovo) {
    if (itemId.empty() || tituloAntigo == tituloNovo) return;

    // Hierarquia "estrutura original" preserva o nome do arquivo master — o
    // título nunca fez parte desse nome, então não há o que sincronizar.
    HierarquiaBackup hierarquia = hierarquiaDoProjeto(registro);
    if (std::find(hierarquia.begin(), hierarquia.end(), NivelHierarquia::EstruturaOriginal) != hierarquia.end())
        return;

    // Destinos de backup conhecidos, ativos e montados agora mesmo.
    std::vector<juce::File> mediaOnline;
    {
        auto stmt = registro.prepare(
            "SELECT destino_path FROM backup_destino WHERE ativo = 1 AND destino_path IS NOT NULL AND destino_path != ''");
        while (stmt.step()) {
            juce::File media = juce::File(juce::String(stmt.columnText(0))).getChildFile("Media");
            if (media.isDirectory()) mediaOnline.push_back(media);
        }
    }
    if (mediaOnline.empty()) return; // nada montado agora — sem fila; alinha no próximo backup manual

    std::string codigoAcervo, tipoMidia;
    {
        auto stmt = registro.prepare("SELECT codigo_acervo, tipo_midia FROM item WHERE id = ?");
        stmt.bind(1, Value::of(itemId));
        if (!stmt.step()) return;
        codigoAcervo = stmt.columnText(0);
        tipoMidia = stmt.columnText(1);
    }

    std::string prefixoEfetivo;
    {
        auto stmt = registro.prepare("SELECT prefixo_nomenclatura FROM projeto LIMIT 1");
        if (stmt.step() && !stmt.columnIsNull(0)) prefixoEfetivo = stmt.columnText(0);
    }
    if (prefixoEfetivo.empty()) prefixoEfetivo = "BKR";

    std::string nomeAcervo;
    {
        auto stmt = registro.prepare("SELECT nome FROM projeto LIMIT 1");
        nomeAcervo = stmt.step() ? stmt.columnText(0) : pastaProjeto.getFileNameWithoutExtension().toStdString();
    }

    auto camposFichaBase = camposFichaDoItem(registro, itemId);

    // Uma linha por (pasta, arquivo) em que o item já foi consolidado alguma
    // vez — pode haver mais de uma se o item está em várias pastas do mapa
    // BACKUP.
    struct LinhaRegistro { std::string pastaId, arquivoId, caminhoRelativo; };
    std::vector<LinhaRegistro> linhas;
    {
        auto stmt = registro.prepare(
            "SELECT pasta_id, arquivo_id, caminho_relativo_destino FROM consolidacao_registro WHERE item_id = ?");
        stmt.bind(1, Value::of(itemId));
        while (stmt.step()) {
            if (stmt.columnIsNull(2)) continue;
            std::string rel = stmt.columnText(2);
            if (rel.empty()) continue;
            linhas.push_back({stmt.columnText(0), stmt.columnText(1), rel});
        }
    }

    matriz::model::ProjectLog log(pastaProjeto);

    for (auto& lr : linhas) {
        auto cadeia = cadeiaAncestral(registro, lr.pastaId);
        juce::String mascara = mascaraEfetiva(registro, cadeia);

        auto resolvido = matriz::vault::resolverArquivo(registro, lr.arquivoId, pastaProjeto,
                                                        matriz::vault::Preferencia::Origem);
        juce::File arquivoNoProjeto = resolvido ? *resolvido
                                                 : pastaProjeto.getChildFile(juce::String(lr.caminhoRelativo));

        ContextoMascara ctx;
        ctx.prefixo = prefixoEfetivo;
        ctx.codigoAcervo = codigoAcervo;
        ctx.tipoMidia = tipoMidia;
        ctx.nomeOriginalSemExtensao = arquivoNoProjeto.getFileNameWithoutExtension().toStdString();
        ctx.nomeAcervo = nomeAcervo;
        ctx.nomePasta = cadeia.empty() ? std::string() : cadeia.back().second.toStdString();
        ctx.camposFicha = camposFichaBase;
        {
            auto itAno = ctx.camposFicha.find("ano");
            bool temAno = itAno != ctx.camposFicha.end() && !itAno->second.empty();
            if (temAno) {
                std::string anoTratado = extrairAnoDeTexto(itAno->second);
                if (!anoTratado.empty()) ctx.camposFicha["ano"] = anoTratado;
                else temAno = false;
            }
            if (!temAno) {
                std::string anoInferido = resolverAnoEfetivo(registro, itemId, lr.arquivoId, arquivoNoProjeto);
                if (!anoInferido.empty()) ctx.camposFicha["ano"] = anoInferido;
            }
        }

        juce::File relFile(juce::String(lr.caminhoRelativo));
        juce::String extensao = relFile.getFileExtension(); // já inclui o "."
        juce::String nomeAntigoEsperado = relFile.getFileName();
        juce::String pastaPrefixo = relFile.getParentDirectory().getFullPathName();
        if (pastaPrefixo == ".") pastaPrefixo = {};

        juce::String nomeNovo;

        // Caminho do modo "NO PREFIX" (o default da aba BACKUP): lá o nome
        // gravado é o próprio título da ficha, sem máscara — a varredura de
        // {seq} abaixo nunca reproduziria esse nome, e o rename era
        // descartado com "naming mask no longer reproduces...".
        ctx.titulo = tituloAntigo;
        juce::String tituloAntigoSanitizado = juce::String(resolverMascara("{titulo}", ctx)).trim();
        bool nomeVeioDoTitulo = tituloAntigoSanitizado.isNotEmpty()
                                 && relFile.getFileNameWithoutExtension() == tituloAntigoSanitizado;

        if (nomeVeioDoTitulo) {
            ctx.titulo = tituloNovo;
            juce::String tituloNovoSanitizado = juce::String(resolverMascara("{titulo}", ctx)).trim();
            if (tituloNovoSanitizado.isEmpty()) continue; // título apagado — mantém o nome já consolidado
            nomeNovo = tituloNovoSanitizado + extensao;
        } else {
            // Redescobre o {seq} usado originalmente reproduzindo o nome antigo —
            // seq nunca foi persistido à parte, só embutido no nome já gravado.
            int seqEncontrado = -1;
            ctx.titulo = tituloAntigo;
            for (int seq = 1; seq <= 9999; ++seq) {
                ctx.seq = seq;
                juce::String candidato = juce::String(resolverMascara(mascara, ctx)) + extensao;
                if (candidato == nomeAntigoEsperado) { seqEncontrado = seq; break; }
            }
            if (seqEncontrado < 0) {
                log.appendEntry("Backup Rename Skipped", {
                    "Item: " + juce::String(codigoAcervo),
                    juce::String::fromUTF8("Reason: current naming mask no longer reproduces the existing backup "
                                            "file name — rename skipped, run Backup again to resync.")});
                continue;
            }

            ctx.titulo = tituloNovo;
            ctx.seq = seqEncontrado;
            nomeNovo = juce::String(resolverMascara(mascara, ctx)) + extensao;
        }
        if (nomeNovo == nomeAntigoEsperado) continue; // máscara não usa {titulo} (ou sanitização deu igual) — nada a fazer

        juce::String novoRelativo = pastaPrefixo.isEmpty() ? nomeNovo : (pastaPrefixo + "/" + nomeNovo);

        for (auto& media : mediaOnline) {
            juce::File origemArq = media.getChildFile(juce::String(lr.caminhoRelativo));
            if (!origemArq.existsAsFile()) continue; // não consolidado nesse destino específico

            juce::File destinoArq = media.getChildFile(novoRelativo);
            if (destinoArq.exists()) {
                log.appendEntry("Backup Rename Skipped", {
                    "Item: " + juce::String(codigoAcervo),
                    juce::String::fromUTF8("Reason: a file already exists at the new planned name — not overwritten."),
                    "Existing: " + destinoArq.getFullPathName(),
                    "Would-be new name: " + novoRelativo});
                continue;
            }

            destinoArq.getParentDirectory().createDirectory();
            if (origemArq.moveFileTo(destinoArq)) {
                registro.run(
                    "UPDATE consolidacao_registro SET caminho_relativo_destino = ? WHERE item_id = ? AND pasta_id = ? AND arquivo_id = ?",
                    {Value::of(novoRelativo.toStdString()), Value::of(itemId), Value::of(lr.pastaId), Value::of(lr.arquivoId)});
                log.appendEntry("Backup Media Renamed", {
                    "From: " + juce::String(lr.caminhoRelativo),
                    "To: " + novoRelativo});
            }
        }
    }
}

} // namespace matriz::consolidacao
