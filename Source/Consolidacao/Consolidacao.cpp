#include "Consolidacao.h"

#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "../Preservation/Preservation.h"
#include "../Vault/Resolucao.h"
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
                                        const RotuloTipoMidia& rotuloTipoMidia,
                                        const juce::String& prefixoCustomizado) {
    PlanoConsolidacao plano;
    HierarquiaBackup hierarquia = hierarquiaPedida.empty() ? hierarquiaDoProjeto(registro) : hierarquiaPedida;
    auto rotuloTipo = rotuloTipoMidia ? rotuloTipoMidia
                                       : RotuloTipoMidia([](const std::string& t) { return juce::String(t); });

    std::string prefixoEfetivo;
    if (prefixoCustomizado.trim().isNotEmpty()) {
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

    // Um contador de sequência POR PASTA (não global) — "{seq:03}" dentro
    // de uma pasta numera os itens daquela pasta, na ordem em que a
    // consulta devolve (por código de acervo, estável e previsível).
    std::map<std::string, int> seqPorPasta;

    auto stmt = registro.prepare(
        "SELECT i.id, COALESCE(aip.pasta_id, ''), i.codigo_acervo, i.titulo, i.tipo_midia, "
        "a.id, a.caminho_relativo, a.checksum_sha256, a.tamanho_bytes "
        "FROM item i "
        "LEFT JOIN acervo_item_pasta aip ON aip.item_id = i.id "
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

        auto resolvido = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto);
        juce::File arquivoNoProjeto = resolvido ? *resolvido : pastaProjeto.getChildFile(caminhoRelativoOrigem);
        ip.nomeOriginal = arquivoNoProjeto.getFileName();

        juce::int64 dbSize = stmt.columnIsNull(8) ? 0 : static_cast<juce::int64>(stmt.columnInt(8));
        if (dbSize > 0) {
            ip.tamanhoBytes = dbSize;
        } else {
            ip.tamanhoBytes = arquivoNoProjeto.existsAsFile() ? arquivoNoProjeto.getSize() : 0;
        }

        auto cadeia = cadeiaAncestral(registro, ip.pastaId);
        juce::String mascara = prefixoCustomizado.trim().isNotEmpty()
            ? juce::String("{prefix}_{name}_{number}_{year}")
            : mascaraEfetiva(registro, cadeia);

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
        ctx.seq = ++seqPorPasta[ip.pastaId];
        ctx.camposFicha = camposFichaDoItem(registro, ip.itemId);
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
            if (!temAnoNaFicha && (mascara.contains("{year}") || mascara.contains("{ano}"))) {
                std::string anoInferido = resolverAnoEfetivo(registro, ip.itemId, ip.arquivoId, arquivoNoProjeto);
                if (!anoInferido.empty()) {
                    ctx.camposFicha["ano"] = anoInferido;
                }
            }
        }

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
        juce::String nomeArquivoFinal = resolverNomeFinalBackup(arquivoNoProjeto, nomeBase, usaEstruturaOriginal);
        juce::String caminhoRelDestino =
            (segmentosPasta.isEmpty() ? juce::String() : segmentosPasta.joinIntoString("/") + "/") + nomeArquivoFinal;
        ip.caminhoRelativoDestino = caminhoRelDestino;

        // Incremental: já existe um registro pra esta combinação com o
        // MESMO checksum do arquivo hoje? Então não precisa copiar de novo.
        auto stmtJa = registro.prepare(
            "SELECT checksum_sha256 FROM consolidacao_registro WHERE item_id = ? AND pasta_id = ? AND arquivo_id = ?");
        stmtJa.bind(1, Value::of(ip.itemId));
        stmtJa.bind(2, Value::of(ip.pastaId));
        stmtJa.bind(3, Value::of(ip.arquivoId));
        if (stmtJa.step()) ip.jaConsolidado = true;

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
            auto resolvido = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto);
            if (!resolvido)
                throw std::runtime_error("master ausente em disco (Vault offline ou arquivo movido/removido): " +
                                          ip.codigoAcervo);
            juce::File origem = *resolvido;

            juce::File destinoArquivo = destino.getChildFile(ip.caminhoRelativoDestino);
            destinoArquivo.getParentDirectory().createDirectory();
            destinoArquivo.deleteFile(); // reconsolidação: substitui a cópia anterior, nunca acumula lixo

            if (!origem.copyFileTo(destinoArquivo))
                throw std::runtime_error("falha ao copiar pra " + destinoArquivo.getFullPathName().toStdString());

            if (!destinoArquivo.existsAsFile()) throw std::runtime_error("cópia não existe depois de copiar");
            if (destinoArquivo.getSize() != origem.getSize())
                throw std::runtime_error("tamanho da cópia não bate com o original");

            // Embed WAV markers into backup copy if applicable (idempotent)
            if (destinoArquivo.hasFileExtension("wav")) {
                auto marcadores = marcadoresDoItem(registro, ip.itemId);
                if (!marcadores.empty()) {
                    embutirMarcadoresEmWav(destinoArquivo, marcadores);
                    ++resultado.arquivosComMarcadorEmbutido;
                }
            }

            // Embed supported metadata into backup copy if applicable
            auto meta = coletarMetadadosDoItem(registro, ip.itemId);
            embutirMetadadosNoArquivo(destinoArquivo, meta);

            // Compute FINAL SHA256 of delivered backup bytes AFTER all modifications
            matriz::ingest::Checksums checksumCopia = matriz::ingest::calcularChecksums(destinoArquivo);

            try {
                registro.run(
                    "INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                    "checksum_sha256, consolidado_em) VALUES (?, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(item_id, pasta_id, arquivo_id) DO UPDATE SET "
                    "caminho_relativo_destino = excluded.caminho_relativo_destino, "
                    "checksum_sha256 = excluded.checksum_sha256, consolidado_em = excluded.consolidado_em",
                    {Value::of(matriz::model::novoUuid()), Value::of(ip.itemId), Value::of(ip.pastaId), Value::of(ip.arquivoId),
                     Value::of(ip.caminhoRelativoDestino.toStdString()), Value::of(checksumCopia.sha256), Value::of(agora)});
            } catch (...) {
                try {
                    registro.exec("PRAGMA foreign_keys = OFF");
                    registro.run(
                        "INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                        "checksum_sha256, consolidado_em) VALUES (?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(item_id, pasta_id, arquivo_id) DO UPDATE SET "
                        "caminho_relativo_destino = excluded.caminho_relativo_destino, "
                        "checksum_sha256 = excluded.checksum_sha256, consolidado_em = excluded.consolidado_em",
                        {Value::of(matriz::model::novoUuid()), Value::of(ip.itemId), Value::of(ip.pastaId), Value::of(ip.arquivoId),
                         Value::of(ip.caminhoRelativoDestino.toStdString()), Value::of(checksumCopia.sha256), Value::of(agora)});
                    registro.exec("PRAGMA foreign_keys = ON");
                } catch (...) {
                    registro.exec("PRAGMA foreign_keys = ON");
                    throw;
                }
            }

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

} // namespace matriz::consolidacao
