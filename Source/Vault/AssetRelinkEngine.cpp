#include "AssetRelinkEngine.h"
#include "Resolucao.h"
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

        juce::int64 recordedSize = static_cast<juce::int64>(stmt.columnInt(1));
        std::string recordedSha = stmt.columnText(2);
        std::string camRel = stmt.columnText(3);
        std::string camAbs = stmt.columnText(4);

        juce::String expectedFname = juce::File(camAbs.empty() ? camRel : camAbs).getFileName();
        juce::String actualFname = novoArquivo.getFileName();

        // 1. Filename & extension check (warning/validation)
        juce::String expExt = juce::File(expectedFname).getFileExtension().toLowerCase();
        juce::String actExt = novoArquivo.getFileExtension().toLowerCase();
        if (expExt != actExt && expExt.isNotEmpty()) {
            res.errorMessage = "File extension mismatch (expected " + expExt + ", got " + actExt + ").";
            return res;
        }

        // 2. Size check if recorded
        if (recordedSize > 0 && novoArquivo.getSize() != recordedSize) {
            res.errorMessage = "File size mismatch (expected " + juce::File::descriptionOfSizeInBytes(recordedSize) +
                               ", selected file is " + juce::File::descriptionOfSizeInBytes(novoArquivo.getSize()) + ").";
            return res;
        }

        // 3. SHA-256 checksum check if recorded
        if (!recordedSha.empty()) {
            juce::String actualSha = calcularSha256(novoArquivo);
            if (!actualSha.equalsIgnoreCase(juce::String(recordedSha))) {
                res.errorMessage = "Checksum mismatch: the content of the selected file does not match the original asset hash.";
                return res;
            }
        }

        res.isValid = true;
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

} // namespace matriz::vault
