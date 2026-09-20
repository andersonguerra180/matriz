#pragma once

#include <JuceHeader.h>
#include "../Db/Database.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
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
    juce::Colour customColor;
    bool hasCustomColor = false;

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
    void lookAndFeelChanged() override;

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
    class NavTreemapIconButton : public juce::Button {
    public:
        enum class TipoIcone { Home, Subir };
        explicit NavTreemapIconButton(TipoIcone tipo)
            : juce::Button("nav_btn"), tipo_(tipo) {}

        void paintButton(juce::Graphics& g, bool isHover, bool isDown) override {
            const auto& tk = tema();
            auto bounds = getLocalBounds().toFloat().reduced(0.5f);
            float corner = tk.raioPequeno;

            if (isDown) {
                g.setColour(tk.painelAlt.withAlpha(0.8f));
            } else if (isHover) {
                g.setColour(tk.painelAlt.withAlpha(0.5f));
            } else {
                g.setColour(tk.painelAlt.withAlpha(0.2f));
            }
            g.fillRoundedRectangle(bounds, corner);

            g.setColour(isEnabled() ? tk.borda.withAlpha(0.6f) : tk.borda.withAlpha(0.2f));
            g.drawRoundedRectangle(bounds, corner, 1.0f);

            juce::Colour iconCol = !isEnabled() ? tk.textoTerciario.withAlpha(0.4f)
                                 : (isDown || isHover ? tk.textoPrimario : tk.textoSecundario);
            g.setColour(iconCol);

            float cx = bounds.getCentreX();
            float cy = bounds.getCentreY();

            if (tipo_ == TipoIcone::Home) {
                juce::Path roof;
                roof.startNewSubPath(cx - 6.5f, cy + 0.5f);
                roof.lineTo(cx, cy - 5.5f);
                roof.lineTo(cx + 6.5f, cy + 0.5f);
                g.strokePath(roof, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                juce::Path body;
                body.startNewSubPath(cx - 4.5f, cy);
                body.lineTo(cx - 4.5f, cy + 5.5f);
                body.lineTo(cx - 1.2f, cy + 5.5f);
                body.lineTo(cx - 1.2f, cy + 2.0f);
                body.lineTo(cx + 1.2f, cy + 2.0f);
                body.lineTo(cx + 1.2f, cy + 5.5f);
                body.lineTo(cx + 4.5f, cy + 5.5f);
                body.lineTo(cx + 4.5f, cy);
                g.strokePath(body, juce::PathStrokeType(1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
            } else {
                juce::Path arrow;
                arrow.startNewSubPath(cx - 5.0f, cy - 1.0f);
                arrow.lineTo(cx, cy - 6.0f);
                arrow.lineTo(cx + 5.0f, cy - 1.0f);
                arrow.startNewSubPath(cx, cy - 5.5f);
                arrow.lineTo(cx, cy + 5.5f);
                g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

    private:
        TipoIcone tipo_;
    };

    NavTreemapIconButton btnUp_{NavTreemapIconButton::TipoIcone::Subir};
    NavTreemapIconButton btnRoot_{NavTreemapIconButton::TipoIcone::Home};

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
