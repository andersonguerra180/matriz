#pragma once

#include <JuceHeader.h>
#include <functional>
#include <utility>
#include <vector>

namespace matriz::ui {

class BarraNavegacaoComponent : public juce::Component, public juce::TooltipClient {
public:
    enum class Tab {
        Catalog,
        Intake,
        Grid,
        Duplicates,
        Analytics,
        Tree,
        Backup,
        Storage
    };

    BarraNavegacaoComponent();
    ~BarraNavegacaoComponent() override;

    void setProjectInfo(const juce::String& projectName, bool isCatalog);

    void setSelectedTab(Tab tab);
    Tab getSelectedTab() const { return selectedTab_; }

    // Componentes que a aba ativa empresta para esta barra: o grupo da
    // esquerda encosta na margem esquerda, o da direita na margem direita, e
    // o cluster de tabs continua centralizado no que sobra (mesmo cálculo de
    // sempre). Cada par é {componente, largura}. A barra só posiciona — a
    // posse continua com quem criou o botão.
    using ComponenteExtra = std::pair<juce::Component*, int>;
    void setComponentesExtras(const std::vector<ComponenteExtra>& esquerda,
                              const std::vector<ComponenteExtra>& direita);

    std::function<void(Tab)> aoMudarTab;

    juce::String getTooltip() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    void reconstruirTabs();
    juce::String obterTextoAjuda(Tab tab) const;

    struct ItemTab {
        Tab tab;
        juce::String label;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> sepBounds;
        bool hover = false;
    };

    std::vector<ItemTab> tabs_;
    Tab selectedTab_ = Tab::Grid;

    bool isCatalog_ = false;

    std::unique_ptr<juce::TextButton> botaoAjuda_;

    struct ExtraSlot {
        juce::Component::SafePointer<juce::Component> comp;
        int largura = 0;
    };
    std::vector<ExtraSlot> extrasEsquerda_;
    std::vector<ExtraSlot> extrasDireita_;
    int larguraExtras(const std::vector<ExtraSlot>& slots) const;
};

} // namespace matriz::ui
