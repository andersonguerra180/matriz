#pragma once

#include <JuceHeader.h>
#include "ProjetoAberto.h"
#include <string>
#include <vector>

namespace matriz::ui {

struct FormatCategoryStat {
    std::string categoryName; // "3D & Other", "Audio", "Photo / Image", "Video"
    uint64_t assetCount = 0;
    juce::int64 sizeBytes = 0;
    juce::Colour color;
};

struct FileExtensionStat {
    std::string extension; // ".JPG", ".WAV", ".PNG", ".MP4", etc.
    uint64_t count = 0;
    juce::int64 sizeBytes = 0;
};

struct CollectionStatsData {
    uint64_t totalAssets = 0;
    juce::int64 totalSizeBytes = 0;
    std::string primaryFormatName = "3D & Other";
    uint64_t primaryFormatCount = 0;
    double backupHealthPercentage = 0.0;
    uint64_t vulnerableAssetsCount = 0;

    std::vector<FormatCategoryStat> categories;
    std::vector<FileExtensionStat> topExtensions;

    std::vector<std::pair<std::string, uint64_t>> decadeDistribution;

    uint64_t backedUpVaultsCount = 0;
    uint64_t singleCopyVulnerableCount = 0;
    uint64_t offlineUnverifiedCount = 0;
};

class CollectionStatsDashboardComponent : public juce::Component {
public:
    CollectionStatsDashboardComponent();
    ~CollectionStatsDashboardComponent() override = default;

    void updateData(const CollectionStatsData& data);
    void recarregarDoBanco(matriz::db::Database& db);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    CollectionStatsData data_;
};

} // namespace matriz::ui
