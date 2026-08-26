#pragma once

#include <JuceHeader.h>
#include "../Db/Database.h"
#include "ProjetoAberto.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <set>
#include <cstdint>

namespace matriz::ui {

struct AnalyticsTreemapNode {
    std::string id;
    std::string assetId;
    std::string name;
    std::string path;
    uint64_t directSize = 0;
    uint64_t aggregateSize = 0;
    std::string mediaType;
    std::string extension;
    bool isDirectory = false;
    bool isLeaf = true;

    AnalyticsTreemapNode* parent = nullptr;
    std::vector<std::unique_ptr<AnalyticsTreemapNode>> children;
    juce::Rectangle<float> bounds;
};

class AnalyticsTreemapComponent : public juce::Component {
public:
    AnalyticsTreemapComponent();
    ~AnalyticsTreemapComponent() override = default;

    void recarregarDoBanco(matriz::db::Database& db);
    void resetarNavegacao();

    std::function<void(const std::string& assetId)> aoSelecionarItem;
    std::function<void(const std::set<std::string>& assetIds)> aoAbrirNoGrid;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    std::unique_ptr<AnalyticsTreemapNode> rootNode_;
    AnalyticsTreemapNode* noAtual_ = nullptr;
    AnalyticsTreemapNode* noHover_ = nullptr;
    AnalyticsTreemapNode* noSelecionado_ = nullptr;

    uint64_t totalAssetsNoCatalogo_ = 0;
    uint64_t totalTamanhoNoCatalogo_ = 0;

    juce::TextButton btnUp_{"UP"};
    juce::TextButton btnRoot_{"GLOBAL ROOT"};

    struct BreadcrumbSegment {
        std::string label;
        AnalyticsTreemapNode* node = nullptr;
        juce::Rectangle<int> bounds;
    };
    std::vector<BreadcrumbSegment> breadcrumbs_;

    void construirArvoreDoBanco(matriz::db::Database& db);
    void recalcularLayout();
    void layoutNode(AnalyticsTreemapNode* parentNode, const juce::Rectangle<float>& area);
    void squarify(AnalyticsTreemapNode* parentNode, const juce::Rectangle<float>& area);

    void desenharTopBar(juce::Graphics& g, juce::Rectangle<int>& area);
    void desenharBreadcrumbs(juce::Graphics& g, juce::Rectangle<int>& area);
    void desenharTreemap(juce::Graphics& g, const juce::Rectangle<int>& area);
    void desenharNo(juce::Graphics& g, AnalyticsTreemapNode* node, int depth, const juce::Rectangle<float>& clipArea);
    void desenharTooltip(juce::Graphics& g);

    AnalyticsTreemapNode* encontrarNoEm(AnalyticsTreemapNode* startNode, juce::Point<float> pt);
    juce::Colour obterCorCategoria(const std::string& category) const;
    juce::String formatarTamanho(uint64_t bytes) const;
    std::string inferirCategoria(const std::string& mediaType, const std::string& ext) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyticsTreemapComponent)
};

} // namespace matriz::ui
