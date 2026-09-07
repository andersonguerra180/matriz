#pragma once

#include <JuceHeader.h>
#include <functional>

namespace matriz::ui {

class AboutDialog : public juce::Component {
public:
    AboutDialog();
    ~AboutDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void mouseDown(const juce::MouseEvent& e) override;

    std::function<void()> aoFechar;

    static void exibirModal();

private:
    juce::Image splashImage_;
    std::unique_ptr<juce::TextButton> btnClose_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutDialog)
};

} // namespace matriz::ui
