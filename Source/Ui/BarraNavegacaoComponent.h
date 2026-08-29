#pragma once

#include <JuceHeader.h>
#include <functional>
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
        Backup
    };

    BarraNavegacaoComponent();
    ~BarraNavegacaoComponent() override;

    void setProjectInfo(const juce::String& projectName, bool isCatalog);
    void setHasParentCatalog(bool hasParent);

    void setSelectedTab(Tab tab);
    Tab getSelectedTab() const { return selectedTab_; }

    std::function<void(Tab)> aoMudarTab;
    std::function<void()> aoClicarFechar;

    juce::String getTooltip() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    void reconstruirTabs();

    struct ItemTab {
        Tab tab;
        juce::String label;
        juce::Rectangle<int> bounds;
        bool hover = false;
    };

    std::vector<ItemTab> tabs_;
    Tab selectedTab_ = Tab::Grid;

    juce::String brandText_{"COLLECTION"};
    bool isCatalog_ = false;
    bool hasParentCatalog_ = false;

    std::unique_ptr<juce::TextButton> botaoFechar_;
};

} // namespace matriz::ui
