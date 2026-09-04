#include "AssetRelinkEngine.h"
#include "Resolucao.h"
#include "../Preservation/Preservation.h"
#include "../Model/ProjectLog.h"
#include "../Model/Project.h"
#include <algorithm>

namespace matriz::vault {

namespace {

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
                auto resOpt = resolverCaminho(pastaProjeto, locVault, camRel, camAbs);
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
                                                bool aceitarSubstituicaoComNovoId,
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
            matriz::preservation::registrarEvento(db, oldItemId, masterArqId,
                                                  "RELINKED",
                                                  "Asset relocated with matching checksum",
                                                  matriz::preservation::Outcome::Success,
                                                  newAbsPath, "Operator");

            // Project Log
            matriz::model::ProjectLog pLog(pastaProjeto);
            juce::StringArray details;
            details.add("Asset ID: " + juce::String(oldItemId));
            details.add("Previous Path: " + juce::String(camAbsOld));
            details.add("New Path: " + juce::String(newAbsPath));
            details.add("Checksum: " + juce::String(val.actualSha) + " (MATCH)");
            pLog.appendEntry("Asset Relinked (Relocated)", details);

            outNovoItemId = oldItemId;
            return true;
        } catch (const std::exception& e) {
            outError = "Database error during relink: " + juce::String(e.what());
            return false;
        }
    }

    if (val.isDifferentContent) {
        if (!aceitarSubstituicaoComNovoId) {
            outError = "File differs from original. Confirmation required to replace asset and assign new ID.";
            return false;
        }

        // Content mismatch replacement -> GENERATES NEW ASSET ID and records succession
        try {
            std::string newItemId = matriz::model::novoUuid();
            std::string newArqId = matriz::model::novoUuid();

            // 1. Fetch old item info
            auto sItem = db.prepare("SELECT projeto_id, tipo_midia, codigo_acervo, estado, notas_livres, em_quarentena, marcado_publicacao FROM item WHERE id = ?");
            sItem.bind(1, matriz::db::Value::of(oldItemId));
            if (!sItem.step()) {
                outError = "Original item record missing.";
                return false;
            }

            std::string projId = sItem.columnText(0);
            std::string tipoMidia = sItem.columnText(1);
            std::string codAcervo = sItem.columnText(2);
            std::string estado = sItem.columnText(3);
            std::string notas = sItem.columnIsNull(4) ? "" : sItem.columnText(4);
            int quarentena = sItem.columnInt(5);
            int marcadoPub = sItem.columnInt(6);

            // Archive old item's codigo_acervo to avoid unique constraint conflict
            std::string archivedCod = codAcervo + "_archived_" + std::to_string(juce::Time::getCurrentTime().toMilliseconds());
            auto sArch = db.prepare("UPDATE item SET codigo_acervo = ?, estado = 'arquivado', atualizado_em = ? WHERE id = ?");
            sArch.bind(1, matriz::db::Value::of(archivedCod));
            sArch.bind(2, matriz::db::Value::of(agoraIso.toStdString()));
            sArch.bind(3, matriz::db::Value::of(oldItemId));
            sArch.step();

            // 2. Insert new item with active codigo_acervo
            auto sInsItem = db.prepare(
                "INSERT INTO item (id, projeto_id, tipo_midia, codigo_acervo, estado, notas_livres, em_quarentena, marcado_publicacao, criado_em, atualizado_em) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            sInsItem.bind(1, matriz::db::Value::of(newItemId));
            sInsItem.bind(2, matriz::db::Value::of(projId));
            sInsItem.bind(3, matriz::db::Value::of(tipoMidia));
            sInsItem.bind(4, matriz::db::Value::of(codAcervo));
            sInsItem.bind(5, matriz::db::Value::of(estado));
            sInsItem.bind(6, matriz::db::Value::of(notas));
            sInsItem.bind(7, matriz::db::Value::of(static_cast<int64_t>(quarentena)));
            sInsItem.bind(8, matriz::db::Value::of(static_cast<int64_t>(marcadoPub)));
            sInsItem.bind(9, matriz::db::Value::of(agoraIso.toStdString()));
            sInsItem.bind(10, matriz::db::Value::of(agoraIso.toStdString()));
            sInsItem.step();

            // 3. Clone descriptive metadata fields (item_campo)
            auto sCloneCampos = db.prepare(
                "INSERT INTO item_campo (id, item_id, campo_id, valor, indice_repeticao, criado_em, atualizado_em) "
                "SELECT hex(randomblob(16)), ?, campo_id, valor, indice_repeticao, ?, ? FROM item_campo WHERE item_id = ?");
            sCloneCampos.bind(1, matriz::db::Value::of(newItemId));
            sCloneCampos.bind(2, matriz::db::Value::of(agoraIso.toStdString()));
            sCloneCampos.bind(3, matriz::db::Value::of(agoraIso.toStdString()));
            sCloneCampos.bind(4, matriz::db::Value::of(oldItemId));
            sCloneCampos.step();

            // 4. Insert new master file record
            auto sInsArq = db.prepare(
                "INSERT INTO arquivo (id, item_id, caminho_absoluto_origem, tamanho_bytes, checksum_sha256, eh_master, criado_em, atualizado_em) "
                "VALUES (?, ?, ?, ?, ?, 1, ?, ?)");
            sInsArq.bind(1, matriz::db::Value::of(newArqId));
            sInsArq.bind(2, matriz::db::Value::of(newItemId));
            sInsArq.bind(3, matriz::db::Value::of(newAbsPath));
            sInsArq.bind(4, matriz::db::Value::of(static_cast<int64_t>(val.actualSize)));
            sInsArq.bind(5, matriz::db::Value::of(val.actualSha));
            sInsArq.bind(6, matriz::db::Value::of(agoraIso.toStdString()));
            sInsArq.bind(7, matriz::db::Value::of(agoraIso.toStdString()));
            sInsArq.step();

            // 5. PREMIS ledger linking succession
            matriz::preservation::registrarEvento(db, oldItemId, masterArqId,
                                                  "RELINKED",
                                                  "Superseded by replacement asset ID " + newItemId,
                                                  "SUPERSEDED", newAbsPath, "Operator");

            matriz::preservation::registrarEvento(db, newItemId, newArqId,
                                                  "RELINKED",
                                                  "Created via manual relink replacement superseding old Asset ID " + oldItemId,
                                                  matriz::preservation::Outcome::Success,
                                                  newAbsPath, "Operator");

            // 6. Project Log
            matriz::model::ProjectLog pLog(pastaProjeto);
            juce::StringArray details;
            details.add("Previous Asset ID: " + juce::String(oldItemId));
            details.add("New Asset ID: " + juce::String(newItemId));
            details.add("Previous Path: " + juce::String(camAbsOld));
            details.add("New Path: " + juce::String(newAbsPath));
            details.add("New Checksum: " + juce::String(val.actualSha) + " (CONTENT REPLACED)");
            pLog.appendEntry("Asset Replaced (New Asset ID)", details);

            outNovoItemId = newItemId;
            return true;
        } catch (const std::exception& e) {
            outError = "Database error during replacement: " + juce::String(e.what());
            return false;
        }
    }

    outError = "Unknown validation disposition.";
    return false;
}

} // namespace matriz::vault
