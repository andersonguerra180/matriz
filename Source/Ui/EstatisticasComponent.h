#pragma once

#include <JuceHeader.h>
#include "ProjetoAberto.h"
#include "AnalyticsTreemapComponent.h"
#include <set>
#include <memory>

namespace matriz::ui {

class EstatisticasComponent : public juce::Component {
public:
    explicit EstatisticasComponent(ProjetoAberto& projeto);
    ~EstatisticasComponent() override;

    void recarregar();
    void setSelectedAssets(const std::set<std::string>& assetIds);

    std::function<void(const std::string& itemId)> aoSelecionarItem;
    std::function<void(const std::set<std::string>& assetIds)> aoAbrirNoGrid;
    std::function<void(const std::set<std::string>& assetIds)> aoClicarNeedsAttention;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    ProjetoAberto& projeto_;
    AnalyticsTreemapComponent treemapComponent_;

    // Top Summary Indicators
    struct SummaryKpi {
        uint64_t totalAssets = 0;
        juce::int64 totalBytes = 0;
        std::string primaryFormatName = "Audio";
        uint64_t primaryFormatCount = 0;
        uint64_t needsAttentionCount = 0;
        double backupHealthPercentage = 0.0;
        uint64_t vulnerableAssetsCount = 0;
    } summaryKpi_;

    struct CollectionKpi {
        juce::String name;
        juce::String path;
        uint64_t totalAssets = 0;
        juce::int64 totalBytes = 0;
        std::string primaryFormatName = "None";
        uint64_t primaryFormatCount = 0;
        uint64_t needsAttentionCount = 0;
        double backupHealthPercentage = 100.0;
        uint64_t vulnerableAssetsCount = 0;
        bool valido = true;
    };

    std::vector<CollectionKpi> colecoesKpi_;
    CollectionKpi catalogTotalKpi_;

    class CatalogAnalyticsContent;
    std::unique_ptr<juce::Viewport> catalogAnalyticsViewport_;
    std::unique_ptr<CatalogAnalyticsContent> catalogAnalyticsContent_;

    std::set<std::string> needsAttentionIds_;
    juce::Rectangle<int> needsAttentionCardBounds_;

    void carregarMetricasDoBanco(matriz::db::Database& db);
    void carregarMetricasCatalogo();
    void desenharTopKpiCards(juce::Graphics& g, const juce::Rectangle<int>& area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EstatisticasComponent)
};

} // namespace matriz::ui
