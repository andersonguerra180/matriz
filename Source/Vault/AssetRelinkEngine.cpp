#include "AssetRelinkEngine.h"
#include "Resolucao.h"
#include "../Preservation/Preservation.h"
#include "../Model/ProjectLog.h"
#include "../Model/Project.h"
#include "../Model/NomesCanonicos.h"
#include <algorithm>
#include <set>

namespace matriz::vault {

namespace {

// Relink com conteúdo igual (SHA-256 já conferido): troca o caminho do master no lugar, mantendo o Asset ID; PREMIS e
// Project Log. Sem transação própria — o lote junta vários numa só. Com logAdiado, a entrada do Project Log vai para
// a lista em vez de reescrever log.md agora (o lote grava todas de uma vez, depois do COMMIT).
bool relinkarConteudoIdentico(matriz::db::Database& db, const juce::File& pastaProjeto, const std::string& itemId,
                              const std::string& masterArqId, const std::string& camAbsOld, const std::string& newAbsPath,
                              const std::string& sha, juce::String& outError,
                              std::vector<matriz::model::ProjectLog::Entry>* logAdiado = nullptr) {
    const juce::String agoraIso = juce::Time::getCurrentTime().formatted("%Y-%m-%dT%H:%M:%SZ");
    try {
        auto stmtUp = db.prepare("UPDATE arquivo SET caminho_absoluto_origem = ?, atualizado_em = ? WHERE id = ?");
        stmtUp.bind(1, matriz::db::Value::of(newAbsPath));
        stmtUp.bind(2, matriz::db::Value::of(agoraIso.toStdString()));
        stmtUp.bind(3, matriz::db::Value::of(masterArqId));
        stmtUp.step();

        auto stmtLoc = db.prepare("INSERT OR IGNORE INTO localizacao_conhecida (id, arquivo_id, caminho_absoluto, criado_em) VALUES (?, ?, ?, ?)");
        stmtLoc.bind(1, matriz::db::Value::of(matriz::model::novoUuid()));
        stmtLoc.bind(2, matriz::db::Value::of(masterArqId));
        stmtLoc.bind(3, matriz::db::Value::of(newAbsPath));
        stmtLoc.bind(4, matriz::db::Value::of(agoraIso.toStdString()));
        stmtLoc.step();

        // PREMIS event
        matriz::preservation::registrarEvento(db, itemId, masterArqId,
                                              "RELINKED",
                                              "Asset relocated with matching checksum",
                                              matriz::preservation::Outcome::Success,
                                              newAbsPath,
                                              "");  // sem agente: "Operator" não existe em preservation_agent e a FK descartava o evento

        // Project Log
        juce::StringArray details;
        details.add("Asset ID: " + juce::String(itemId));
        details.add("Previous Path: " + juce::String(camAbsOld));
        details.add("New Path: " + juce::String(newAbsPath));
        details.add("Checksum: " + juce::String(sha) + " (MATCH)");
        if (logAdiado != nullptr)
            logAdiado->push_back({"Asset Relinked (Relocated)", details});
        else
            matriz::model::ProjectLog(pastaProjeto).appendEntry("Asset Relinked (Relocated)", details);
        return true;
    } catch (const std::exception& e) {
        outError = "Database error during relink: " + juce::String(e.what());
        return false;
    }
}

// "Relinkar mesmo assim": o arquivo achado tem outro conteúdo (algum programa regravou tags/metadados, recompressão...).
// O MESMO item passa a apontar para ele; tamanho e SHA-256 novos ficam no master e o SHA anterior vai para o PREMIS e o
// Project Log. O gatilho arquivo_master_travado só deixa mudar checksum de master com a marca de reconciliacao_em_curso,
// que entra e sai aqui dentro. Chamar dentro de uma transação (a marca nunca chega ao COMMIT).
bool relinkarConteudoDiferente(matriz::db::Database& db, const juce::File& pastaProjeto, const std::string& itemId,
                               const std::string& masterArqId, const std::string& camAbsOld, const std::string& newAbsPath,
                               const std::string& shaAntigo, const std::string& shaNovo, juce::int64 tamanhoNovo,
                               juce::String& outError, std::vector<matriz::model::ProjectLog::Entry>* logAdiado = nullptr) {
    const std::string agoraIso = juce::Time::getCurrentTime().formatted("%Y-%m-%dT%H:%M:%SZ").toStdString();
    try {
        db.run("INSERT OR IGNORE INTO reconciliacao_em_curso (marca) VALUES (1)", {});
        db.run("UPDATE arquivo SET caminho_absoluto_origem = ?, tamanho_bytes = ?, checksum_sha256 = ?, checksum_md5 = NULL, "
               "checksum_gerado_em = ?, atualizado_em = ? WHERE id = ?",
               {matriz::db::Value::of(newAbsPath), matriz::db::Value::of(static_cast<int64_t>(tamanhoNovo)),
                matriz::db::Value::of(shaNovo), matriz::db::Value::of(agoraIso), matriz::db::Value::of(agoraIso),
                matriz::db::Value::of(masterArqId)});
        db.run("DELETE FROM reconciliacao_em_curso", {});
        db.run("INSERT OR IGNORE INTO localizacao_conhecida (id, arquivo_id, caminho_absoluto, criado_em) VALUES (?, ?, ?, ?)",
               {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(masterArqId),
                matriz::db::Value::of(newAbsPath), matriz::db::Value::of(agoraIso)});

        matriz::preservation::registrarEvento(db, itemId, masterArqId, "RELINKED",
                                              "Asset relinked to a file with different content, accepted by the operator. "
                                              "Previous SHA-256: " + (shaAntigo.empty() ? std::string("(none)") : shaAntigo) +
                                                  "; new SHA-256: " + shaNovo,
                                              matriz::preservation::Outcome::Warning, newAbsPath,
                                              "");  // sem agente: "Operator" não existe em preservation_agent e a FK descartava o evento

        juce::StringArray details;
        details.add("Asset ID: " + juce::String(itemId));
        details.add("Previous Path: " + juce::String(camAbsOld));
        details.add("New Path: " + juce::String(newAbsPath));
        details.add("Previous Checksum: " + juce::String(shaAntigo.empty() ? std::string("(none)") : shaAntigo));
        details.add("New Checksum: " + juce::String(shaNovo) + " (CONTENT CHANGED, ACCEPTED BY OPERATOR)");
        if (logAdiado != nullptr)
            logAdiado->push_back({"Asset Relinked (Content Changed)", details});
        else
            matriz::model::ProjectLog(pastaProjeto).appendEntry("Asset Relinked (Content Changed)", details);
        return true;
    } catch (const std::exception& e) {
        try { db.run("DELETE FROM reconciliacao_em_curso", {}); } catch (...) {}
        outError = "Database error during relink: " + juce::String(e.what());
        return false;
    }
}

juce::String normalizarSeparadores(const juce::String& p) {
    return p.replaceCharacter('\\', '/');
}

juce::StringArray dividirCaminho(const juce::String& p) {
    juce::String norm = normalizarSeparadores(p);
    juce::StringArray parts;
    parts.addTokens(norm, "/", "\"");
    parts.removeEmptyStrings();
    return parts;
}

} // namespace

ResultadoRelinkLote AssetRelinkEngine::relinkarEmLotePorPasta(matriz::db::Database& db,
                                                              std::recursive_mutex& escrita,
                                                              const juce::File& pastaProjeto,
                                                              const std::vector<std::string>& itemIds,
                                                              const juce::File& pasta,
                                                              std::function<bool()> cancelado,
                                                              std::function<void(int, int)> progresso) {
    ResultadoRelinkLote res;
    auto parar = [&] { return cancelado && cancelado(); };
    struct Pedido {
        std::string itemId, arquivoId, camAbsOld, sha;
        juce::int64 tamanho = 0;
        std::string chaveNome;
    };
    std::vector<Pedido> pedidos;
    std::set<std::string> nomesProcurados;
    try {
        std::unique_lock<std::recursive_mutex> leitura(escrita);  // só as consultas (curtas); a varredura e os hashes vêm sem lock
        for (const auto& id : itemIds) {
            auto st = db.prepare(
                "SELECT a.id, a.caminho_relativo, COALESCE(a.caminho_absoluto_origem, ''), COALESCE(a.checksum_sha256, ''), "
                "COALESCE(a.tamanho_bytes, 0), COALESCE(v.localizacao, '') "
                "FROM arquivo a LEFT JOIN vault v ON v.id = a.vault_id WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
            st.bind(1, matriz::db::Value::of(id));
            if (!st.step()) { res.naoAchados.push_back(id); continue; }
            Pedido p;
            p.itemId = id;
            p.arquivoId = st.columnText(0);
            const std::string camRel = st.columnText(1);
            p.camAbsOld = st.columnText(2);
            p.sha = st.columnText(3);
            p.tamanho = static_cast<juce::int64>(st.columnInt(4));
            const juce::File esperado = caminhoEsperado(pastaProjeto, st.columnText(5), camRel, p.camAbsOld);
            if (esperado != juce::File() && esperado.existsAsFile()) { res.jaOnline.push_back(id); continue; }
            const juce::String nome = esperado != juce::File()
                                          ? esperado.getFileName()
                                          : juce::String::fromUTF8((p.camAbsOld.empty() ? camRel : p.camAbsOld).c_str())
                                                .replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false);
            p.chaveNome = matriz::model::nomes::chave(nome.toStdString());
            if (p.chaveNome.empty()) { res.naoAchados.push_back(id); continue; }
            nomesProcurados.insert(p.chaveNome);
            pedidos.push_back(std::move(p));
        }
    } catch (const std::exception& e) {
        res.erro = e.what();
        return res;
    }
    if (pedidos.empty()) return res;

    // Uma varredura da pasta escolhida (e subpastas), guardando só os nomes procurados.
    std::map<std::string, std::vector<juce::File>> porNome;
    for (const auto& entrada : juce::RangedDirectoryIterator(pasta, true, "*", juce::File::findFiles)) {
        if (parar()) { res.cancelado = true; return res; }
        const auto chaveNome = matriz::model::nomes::chave(entrada.getFile().getFileName().toStdString());
        if (nomesProcurados.count(chaveNome) != 0) porNome[chaveNome].push_back(entrada.getFile());
    }

    // Conferência (tamanho, depois SHA-256) sem o lock de escrita: hash de vídeo leva tempo.
    struct Achado { const Pedido* pedido; juce::File arquivo; };
    std::vector<Achado> achados;
    std::set<std::string> arquivosUsados;  // dois itens nunca apontam para o mesmo arquivo
    const int total = static_cast<int>(pedidos.size());
    int feitos = 0;
    for (const auto& p : pedidos) {
        if (parar()) { res.cancelado = true; return res; }
        const auto it = porNome.find(p.chaveNome);
        bool achou = false, algumCandidato = false;
        if (it != porNome.end()) {
            for (const auto& f : it->second) {
                if (arquivosUsados.count(f.getFullPathName().toStdString()) != 0) continue;
                algumCandidato = true;
                if (p.tamanho > 0 && f.getSize() != p.tamanho) continue;
                if (p.sha.empty() || !calcularSha256(f).equalsIgnoreCase(juce::String(p.sha))) continue;
                achados.push_back({&p, f});
                arquivosUsados.insert(f.getFullPathName().toStdString());
                achou = true;
                break;
            }
        }
        if (!achou) (algumCandidato ? res.conteudoDiferente : res.naoAchados).push_back(p.itemId);
        if (!achou && algumCandidato) {  // um só arquivo com o nome (ainda livre): pode ser aceito mesmo assim
            std::vector<juce::File> livres;
            for (const auto& f : it->second)
                if (arquivosUsados.count(f.getFullPathName().toStdString()) == 0) livres.push_back(f);
            if (livres.size() == 1) res.candidatoDiferente[p.itemId] = livres.front().getFullPathName().toStdString();
        }
        if (progresso) progresso(++feitos, total);
    }
    if (achados.empty()) return res;

    std::vector<matriz::model::ProjectLog::Entry> entradasLog;
    std::unique_lock<std::recursive_mutex> lock(escrita);
    try {
        db.exec("BEGIN IMMEDIATE");
        for (const auto& a : achados) {
            juce::String erro;
            if (!relinkarConteudoIdentico(db, pastaProjeto, a.pedido->itemId, a.pedido->arquivoId, a.pedido->camAbsOld,
                                          a.arquivo.getFullPathName().toStdString(), a.pedido->sha, erro, &entradasLog))
                throw std::runtime_error(erro.toStdString());
            res.relinkados.push_back(a.pedido->itemId);
        }
        db.exec("COMMIT");
    } catch (const std::exception& e) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        res.relinkados.clear();
        res.erro = e.what();
        entradasLog.clear();
    }
    lock.unlock();
    matriz::model::ProjectLog(pastaProjeto).appendEntries(entradasLog);
    return res;
}

ResultadoRelinkLote AssetRelinkEngine::relinkarAceitandoDiferenca(matriz::db::Database& db,
                                                                  std::recursive_mutex& escrita,
                                                                  const juce::File& pastaProjeto,
                                                                  const std::map<std::string, std::string>& itemParaArquivo,
                                                                  std::function<bool()> cancelado,
                                                                  std::function<void(int, int)> progresso) {
    ResultadoRelinkLote res;
    auto parar = [&] { return cancelado && cancelado(); };
    struct Aceito {
        std::string itemId, arquivoId, camAbsOld, shaAntigo, caminhoNovo, shaNovo;
        juce::int64 tamanhoNovo = 0;
    };
    std::vector<Aceito> aceitos;
    std::set<std::string> arquivosUsados;  // dois itens nunca apontam para o mesmo arquivo
    const int total = static_cast<int>(itemParaArquivo.size());
    int feitos = 0;
    for (const auto& [itemId, caminho] : itemParaArquivo) {
        if (parar()) { res.cancelado = true; return res; }
        const juce::File f(juce::String::fromUTF8(caminho.c_str()));
        if (!f.existsAsFile() || !arquivosUsados.insert(caminho).second) { res.naoAchados.push_back(itemId); continue; }
        Aceito a;
        a.itemId = itemId;
        a.caminhoNovo = caminho;
        try {
            std::unique_lock<std::recursive_mutex> leitura(escrita);
            auto st = db.prepare("SELECT id, COALESCE(caminho_absoluto_origem, caminho_relativo), COALESCE(checksum_sha256, '') "
                                 "FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
            st.bind(1, matriz::db::Value::of(itemId));
            if (!st.step()) { res.naoAchados.push_back(itemId); continue; }
            a.arquivoId = st.columnText(0);
            a.camAbsOld = st.columnText(1);
            a.shaAntigo = st.columnText(2);
        } catch (const std::exception& e) {
            res.erro = e.what();
            return res;
        }
        a.tamanhoNovo = f.getSize();
        a.shaNovo = calcularSha256(f).toStdString();  // sem o lock: hash de vídeo leva tempo
        aceitos.push_back(std::move(a));
        if (progresso) progresso(++feitos, total);
    }
    if (aceitos.empty()) return res;

    std::vector<matriz::model::ProjectLog::Entry> entradasLog;
    std::unique_lock<std::recursive_mutex> lock(escrita);
    try {
        db.exec("BEGIN IMMEDIATE");
        for (const auto& a : aceitos) {
            juce::String erro;
            if (!relinkarConteudoDiferente(db, pastaProjeto, a.itemId, a.arquivoId, a.camAbsOld, a.caminhoNovo, a.shaAntigo,
                                           a.shaNovo, a.tamanhoNovo, erro, &entradasLog))
                throw std::runtime_error(erro.toStdString());
            res.relinkados.push_back(a.itemId);
        }
        db.exec("COMMIT");
    } catch (const std::exception& e) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        res.relinkados.clear();
        res.erro = e.what();
        entradasLog.clear();
    }
    lock.unlock();
    matriz::model::ProjectLog(pastaProjeto).appendEntries(entradasLog);
    return res;
}

juce::String AssetRelinkEngine::calcularSha256(const juce::File& file) {
    if (!file.existsAsFile()) return {};
    return juce::SHA256(file).toHexString().toLowerCase();
}

AssetPresenceReport AssetRelinkEngine::verificarPresencaAssets(matriz::db::Database& db,
                                                              const juce::File& pastaProjeto,
                                                              const std::map<std::string, std::string>& inMemoryOverrides) {
    AssetPresenceReport report;

    try {
        auto stmt = db.prepare(
            "SELECT a.id, a.item_id, i.titulo, COALESCE(v.localizacao, ''), a.caminho_relativo, COALESCE(a.caminho_absoluto_origem, '') "
            "FROM arquivo a "
            "JOIN item i ON i.id = a.item_id "
            "LEFT JOIN vault v ON v.id = a.vault_id "
            "WHERE a.eh_master = 1");

        // MAIN/CLONE primeiro: com o backup presente o asset está online.
        ResolvedorEmLote resolvedor(db, pastaProjeto);
        while (stmt.step()) {
            std::string arquivoId = stmt.columnText(0);
            std::string itemId = stmt.columnText(1);
            std::string titulo = stmt.columnText(2);
            std::string locVault = stmt.columnText(3);
            std::string camRel = stmt.columnText(4);
            std::string camAbs = stmt.columnText(5);

            report.totalAssets++;

            bool exists = false;
            auto it = inMemoryOverrides.find(arquivoId);
            if (it != inMemoryOverrides.end() && !it->second.empty()) {
                juce::File overrideFile(it->second);
                exists = overrideFile.existsAsFile();
            } else {
                auto resOpt = resolvedor.resolver(arquivoId, locVault, camRel, camAbs);
                exists = resOpt.has_value() && resOpt->existsAsFile();
            }

            if (exists) {
                report.onlineAssets++;
            } else {
                report.offlineAssets++;
                report.missingItemIds.push_back(itemId);

                juce::File expected = caminhoEsperado(pastaProjeto, locVault, camRel, camAbs);
                juce::String expStr = expected != juce::File() ? expected.getFullPathName() : (camAbs.empty() ? camRel : camAbs);
                report.missingPaths.push_back(expStr.toStdString());

                if (report.sampleMissingExpectedPath.isEmpty()) {
                    report.sampleMissingExpectedPath = expStr;
                    report.sampleMissingItemId = itemId;
                    report.sampleMissingArquivoId = arquivoId;
                    report.sampleMissingTitle = titulo;
                }
            }
        }
    } catch (...) {}

    return report;
}

bool AssetRelinkEngine::inferirNovaRaiz(const juce::String& oldPath,
                                       const juce::String& newPath,
                                       juce::String& outOldRoot,
                                       juce::String& outNewRoot) {
    if (oldPath.isEmpty() || newPath.isEmpty()) return false;

    auto oldParts = dividirCaminho(oldPath);
    auto newParts = dividirCaminho(newPath);

    if (oldParts.isEmpty() || newParts.isEmpty()) return false;

    // Compare segments backwards from the end
    int matchingCount = 0;
    int oldIdx = oldParts.size() - 1;
    int newIdx = newParts.size() - 1;

    while (oldIdx >= 0 && newIdx >= 0) {
        if (oldParts[oldIdx].equalsIgnoreCase(newParts[newIdx])) {
            matchingCount++;
            oldIdx--;
            newIdx--;
        } else {
            break;
        }
    }

    if (matchingCount == 0) {
        // Fallback: at least match filename without extension or exact extension if possible
        if (juce::File(oldPath).getFileName().equalsIgnoreCase(juce::File(newPath).getFileName())) {
            matchingCount = 1;
            oldIdx = oldParts.size() - 2;
            newIdx = newParts.size() - 2;
        } else {
            return false;
        }
    }

    // Rebuild old root and new root
    juce::String normOld = normalizarSeparadores(oldPath);
    juce::String normNew = normalizarSeparadores(newPath);

    bool oldIsWindows = normOld.contains(":/");
    bool newIsWindows = normNew.contains(":/");

    juce::String oldRootStr = "";
    if (normOld.startsWithChar('/')) oldRootStr = "/";
    for (int i = 0; i <= oldIdx; ++i) {
        if (i > 0) oldRootStr += "/";
        oldRootStr += oldParts[i];
    }

    juce::String newRootStr = "";
    if (normNew.startsWithChar('/')) newRootStr = "/";
    for (int i = 0; i <= newIdx; ++i) {
        if (i > 0) newRootStr += "/";
        newRootStr += newParts[i];
    }

    outOldRoot = oldRootStr;
    outNewRoot = newRootStr;

    return true;
}

RelocationResult AssetRelinkEngine::relocarColecaoEmMemoria(matriz::db::Database& db,
                                                           const juce::File& pastaProjeto,
                                                           const juce::String& oldSamplePath,
                                                           const juce::File& newSampleFile,
                                                           std::map<std::string, std::string>& outInMemoryRelocatedPaths) {
    RelocationResult res;
    if (!newSampleFile.existsAsFile()) return res;

    juce::String oldRoot, newRoot;
    if (!inferirNovaRaiz(oldSamplePath, newSampleFile.getFullPathName(), oldRoot, newRoot)) {
        return res;
    }

    res.inferredOldRoot = oldRoot;
    res.inferredNewRoot = newRoot;

    juce::File newRootDir(newRoot);
    juce::String normOldRoot = normalizarSeparadores(oldRoot).toLowerCase();
    if (!normOldRoot.endsWithChar('/')) normOldRoot += "/";

    try {
        auto stmt = db.prepare(
            "SELECT a.id, a.item_id, COALESCE(v.localizacao, ''), a.caminho_relativo, COALESCE(a.caminho_absoluto_origem, '') "
            "FROM arquivo a "
            "LEFT JOIN vault v ON v.id = a.vault_id "
            "WHERE a.eh_master = 1");

        while (stmt.step()) {
            std::string arquivoId = stmt.columnText(0);
            std::string itemId = stmt.columnText(1);
            std::string locVault = stmt.columnText(2);
            std::string camRel = stmt.columnText(3);
            std::string camAbs = stmt.columnText(4);

            juce::File targetCandidate;

            // Strategy 1: If camAbs starts with oldRoot, replace with newRoot
            juce::String normCamAbs = normalizarSeparadores(camAbs);
            if (normCamAbs.toLowerCase().startsWith(normOldRoot)) {
                juce::String sub = normCamAbs.substring(normOldRoot.length());
                targetCandidate = newRootDir.getChildFile(sub);
            }

            // Strategy 2: If target not found or camAbs empty, try newRoot + camRel
            if (!targetCandidate.existsAsFile() && !camRel.empty()) {
                targetCandidate = newRootDir.getChildFile(juce::String(camRel));
            }

            // Strategy 3: Try matching filename inside newRootDir recursively if not found
            if (!targetCandidate.existsAsFile()) {
                juce::String fname = juce::File(camAbs.empty() ? camRel : camAbs).getFileName();
                if (fname.isNotEmpty()) {
                    auto matchFile = newRootDir.getChildFile(fname);
                    if (matchFile.existsAsFile()) {
                        targetCandidate = matchFile;
                    }
                }
            }

            if (targetCandidate.existsAsFile()) {
                outInMemoryRelocatedPaths[arquivoId] = targetCandidate.getFullPathName().toStdString();
                res.resolvedCount++;
            } else {
                res.notFoundCount++;
                res.offlineCount++;
            }
        }
    } catch (...) {}

    return res;
}

ValidationResult AssetRelinkEngine::validarAsset(matriz::db::Database& db,
                                                 const std::string& itemId,
                                                 const juce::File& novoArquivo) {
    ValidationResult res;
    if (!novoArquivo.existsAsFile()) {
        res.errorMessage = "The selected file does not exist on disk.";
        return res;
    }

    try {
        auto stmt = db.prepare(
            "SELECT a.id, a.tamanho_bytes, a.checksum_sha256, a.caminho_relativo, COALESCE(a.caminho_absoluto_origem, '') "
            "FROM arquivo a "
            "WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));

        if (!stmt.step()) {
            res.errorMessage = "Asset record not found in project database.";
            return res;
        }

        res.expectedSize = static_cast<juce::int64>(stmt.columnInt(1));
        res.expectedSha = stmt.columnText(2);
        std::string camRel = stmt.columnText(3);
        std::string camAbs = stmt.columnText(4);

        res.actualSize = novoArquivo.getSize();
        res.actualSha = calcularSha256(novoArquivo).toStdString();

        bool sizeMatches = (res.expectedSize <= 0 || res.actualSize == res.expectedSize);
        bool shaMatches = (!res.expectedSha.empty() && juce::String(res.actualSha).equalsIgnoreCase(juce::String(res.expectedSha)));

        if (shaMatches && sizeMatches) {
            res.isIdenticalContent = true;
            res.isValid = true;
        } else {
            // Content differs -> smart replacement candidate
            res.isDifferentContent = true;
            res.isValid = true;
            res.warningMessage = "Selected file content differs from original (checksum mismatch).";
        }
    } catch (const std::exception& e) {
        res.errorMessage = "Validation error: " + juce::String(e.what());
    }

    return res;
}

bool AssetRelinkEngine::relocarAssetIndividualEmMemoria(matriz::db::Database& db,
                                                       const std::string& itemId,
                                                       const juce::File& novoArquivo,
                                                       std::map<std::string, std::string>& outInMemoryRelocatedPaths,
                                                       juce::String& outError) {
    auto val = validarAsset(db, itemId, novoArquivo);
    if (!val.isValid) {
        outError = val.errorMessage;
        return false;
    }

    try {
        auto stmt = db.prepare("SELECT a.id FROM arquivo a WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) {
            std::string arquivoId = stmt.columnText(0);
            outInMemoryRelocatedPaths[arquivoId] = novoArquivo.getFullPathName().toStdString();
            return true;
        }
    } catch (...) {}

    outError = "Could not find master record for item.";
    return false;
}

bool AssetRelinkEngine::executarRelinkIndividual(matriz::db::Database& db,
                                                const juce::File& pastaProjeto,
                                                const std::string& oldItemId,
                                                const juce::File& novoArquivo,
                                                bool aceitarConteudoDiferente,
                                                std::string& outNovoItemId,
                                                juce::String& outError) {
    auto val = validarAsset(db, oldItemId, novoArquivo);
    if (!val.isValid) {
        outError = val.errorMessage;
        return false;
    }

    std::string masterArqId;
    std::string camAbsOld;
    try {
        auto sArq = db.prepare("SELECT id, COALESCE(caminho_absoluto_origem, caminho_relativo) FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
        sArq.bind(1, matriz::db::Value::of(oldItemId));
        if (sArq.step()) {
            masterArqId = sArq.columnText(0);
            camAbsOld = sArq.columnText(1);
        }
    } catch (...) {}

    if (masterArqId.empty()) {
        outError = "Master file record not found for item.";
        return false;
    }

    juce::String agoraIso = juce::Time::getCurrentTime().formatted("%Y-%m-%dT%H:%M:%SZ");
    std::string newAbsPath = novoArquivo.getFullPathName().toStdString();

    if (val.isIdenticalContent) {
        // Relocation of identical asset -> PRESERVES existing Asset ID
        if (!relinkarConteudoIdentico(db, pastaProjeto, oldItemId, masterArqId, camAbsOld, newAbsPath, val.actualSha, outError))
            return false;
        outNovoItemId = oldItemId;
        return true;
    }

    if (val.isDifferentContent) {
        if (!aceitarConteudoDiferente) {
            outError = "File differs from original. Confirmation required to relink anyway.";
            return false;
        }
        // Mesmo item, outro conteúdo: antes criava um item novo (copiando quase nada da ficha) e, falhando no meio,
        // deixava o original renomeado "_archived_" — sem transação e sem mensagem. Agora é o mesmo Asset ID.
        try {
            db.exec("BEGIN IMMEDIATE");
            if (!relinkarConteudoDiferente(db, pastaProjeto, oldItemId, masterArqId, camAbsOld, newAbsPath, val.expectedSha,
                                           val.actualSha, val.actualSize, outError)) {
                db.exec("ROLLBACK");
                return false;
            }
            db.exec("COMMIT");
        } catch (const std::exception& e) {
            try { db.exec("ROLLBACK"); } catch (...) {}
            outError = "Database error during relink: " + juce::String(e.what());
            return false;
        }
        outNovoItemId = oldItemId;
        return true;
    }

    outError = "Unknown validation disposition.";
    return false;
}

} // namespace matriz::vault
