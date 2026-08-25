#pragma once

#include <JuceHeader.h>
#include "ProjetoAberto.h"
#include <set>
#include <memory>

namespace matriz::ui {

class EstatisticasComponent : public juce::Component {
public:
    explicit EstatisticasComponent(ProjetoAberto& projeto);
    ~EstatisticasComponent() override = default;

    void recarregar();
    void setSelectedAssets(const std::set<std::string>& assetIds);

    std::function<void(const std::string& itemId)> aoSelecionarItem;
    std::function<void(const std::set<std::string>& assetIds)> aoAbrirNoGrid;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    ProjetoAberto& projeto_;

    // Top Summary Indicators
    struct SummaryKpi {
        uint64_t totalAssets = 0;
        juce::int64 totalBytes = 0;
        std::string primaryFormatName = "Audio";
        uint64_t primaryFormatCount = 0;
        double backupHealthPercentage = 0.0;
        uint64_t vulnerableAssetsCount = 0;
    } summaryKpi_;

    void carregarMetricasDoBanco(matriz::db::Database& db);
    void desenharTopKpiCards(juce::Graphics& g, const juce::Rectangle<int>& area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EstatisticasComponent)
};

} // namespace matriz::ui
