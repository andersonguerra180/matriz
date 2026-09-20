#pragma once

#include <JuceHeader.h>
#include <functional>

namespace matriz::ui {

class TrialNagDialog : public juce::Component, private juce::Timer {
public:
    TrialNagDialog();
    ~TrialNagDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void()> aoFechar;

    static void exibirSeNecessario(juce::Component* parent = nullptr);
    static void exibirModal(juce::Component* parent = nullptr);
    static bool isTrial();

private:
    void timerCallback() override;

    int segundosRestantes_{5};
    std::unique_ptr<juce::TextButton> btnContinuar_;
    std::unique_ptr<juce::TextButton> btnComprar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrialNagDialog)
};

} // namespace matriz::ui
