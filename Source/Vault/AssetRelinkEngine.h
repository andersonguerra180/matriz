#pragma once

#include <JuceHeader.h>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include "../Db/Database.h"

namespace matriz::vault {

struct AssetPresenceReport {
    uint64_t totalAssets = 0;
    uint64_t onlineAssets = 0;
    uint64_t offlineAssets = 0;
    std::vector<std::string> missingItemIds;
    std::vector<std::string> missingPaths;
    juce::String sampleMissingExpectedPath;
    std::string sampleMissingItemId;
    std::string sampleMissingArquivoId;
    std::string sampleMissingTitle;
};

struct RelocationResult {
    int resolvedCount = 0;
    int notFoundCount = 0;
    int offlineCount = 0;
    juce::String inferredOldRoot;
    juce::String inferredNewRoot;
};

struct ValidationResult {
    bool isValid = false;
    juce::String errorMessage;
};

class AssetRelinkEngine {
public:
    // Scans all master assets in database and verifies if their physical files exist on disk,
    // honoring any in-memory relocation overrides currently in effect.
    static AssetPresenceReport verificarPresencaAssets(matriz::db::Database& db,
                                                       const juce::File& pastaProjeto,
                                                       const std::map<std::string, std::string>& inMemoryOverrides = {});

    // Robustly infers old root vs new root by matching path components from the tail backwards.
    // Handles drive letter changes, mount point changes, and relocated root folders.
    static bool inferirNovaRaiz(const juce::String& oldPath,
                                const juce::String& newPath,
                                juce::String& outOldRoot,
                                juce::String& outNewRoot);

    // Relocates all candidate assets in memory based on the reference file chosen by the user.
    // Populates outInMemoryRelocatedPaths with (arquivoId -> newPath).
    // Does NOT perform disk save to .mtz / .bkm.
    static RelocationResult relocarColecaoEmMemoria(matriz::db::Database& db,
                                                    const juce::File& pastaProjeto,
                                                    const juce::String& oldSamplePath,
                                                    const juce::File& newSampleFile,
                                                    std::map<std::string, std::string>& outInMemoryRelocatedPaths);

    // Validates if a user-selected file matches an asset's expected identity (filename, size, SHA-256).
    static ValidationResult validarAsset(matriz::db::Database& db,
                                         const std::string& itemId,
                                         const juce::File& novoArquivo);

    // Relocates a single asset in memory after validation.
    // Populates outInMemoryRelocatedPaths for that specific asset only.
    static bool relocarAssetIndividualEmMemoria(matriz::db::Database& db,
                                               const std::string& itemId,
                                               const juce::File& novoArquivo,
                                               std::map<std::string, std::string>& outInMemoryRelocatedPaths,
                                               juce::String& outError);

    // Helper to compute SHA-256 for a file
    static juce::String calcularSha256(const juce::File& file);
};

} // namespace matriz::vault
