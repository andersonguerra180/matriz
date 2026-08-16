#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace matriz::ui {

class BarraNavegacaoComponent : public juce::Component {
public:
    enum class Tab {
        Home,
        Grid,
        Analytics,
        Tree,
        Backup
    };

    BarraNavegacaoComponent();
    ~BarraNavegacaoComponent() override;

    void setSelectedTab(Tab tab);
    Tab getSelectedTab() const { return selectedTab_; }

    std::function<void(Tab)> aoMudarTab;
    std::function<void()> aoClicarFechar;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    struct ItemTab {
        Tab tab;
        juce::String label;
        juce::Rectangle<int> bounds;
        bool hover = false;
    };

    std::vector<ItemTab> tabs_;
    Tab selectedTab_ = Tab::Home;

    std::unique_ptr<juce::TextButton> botaoFechar_;
};

} // namespace matriz::ui
