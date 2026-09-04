#pragma once

#include <JuceHeader.h>
#include <functional>
#include <string>
#include <vector>

#include "../Ui/ProjetoAberto.h"
#include "../Consolidacao/Consolidacao.h"

namespace matriz::catalogo {

struct SkippedAssetInfo {
    std::string itemId;
    juce::String title;
    juce::String collectionName;
    juce::String reason;
};

struct ResultadoExportSite {
    int totalCollections = 0;
    int totalAssets = 0;
    int thumbnailsGenerated = 0;
    int previewsGenerated = 0;
    std::vector<SkippedAssetInfo> skippedOfflineAssets;
    std::vector<juce::String> errors;
    bool cancelled = false;
    juce::File outputDirectory;
};

struct ParamsExportSite {
    int previewDurationSec = 15;
    int thumbnailMaxPx = 1200;
    int paginationLimit = 250;
    juce::File backgroundImageFile; // Optional: custom background (.jpg/.jpeg/.png)
    juce::File logoFile;            // Optional: custom logo (.jpg/.jpeg/.png)
};

// Generates a self-contained, responsive static HTML browser
// containing all linked Collections in a Catalog project.
//
// Guaranteed rules:
//  - Only active and valid in Catalog projects.
//  - Never copies original / master files into the export.
//  - Reuses existing ProjetoAberto reading layer (no duplicate queries).
//  - Gracefully skips offline master files without interrupting export.
//  - Caches previews by validating target file against source master mtime.
//  - Outputs HTML/CSS/JS with zero runtime server or CDN dependencies.
ResultadoExportSite exportarHtmlBrowser(ui::ProjetoAberto& projeto,
                                       const juce::File& destino,
                                       const ParamsExportSite& params = {},
                                       const matriz::consolidacao::AoProgredir& aoProgredir = {});

// Deterministic URL-friendly slug generator for collection names
juce::String gerarSlugColecao(const juce::String& nomeOriginal);

} // namespace matriz::catalogo
