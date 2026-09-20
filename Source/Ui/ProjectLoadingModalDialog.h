#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include "../Model/Project.h"

namespace matriz::ui {

class ProjectLoadingModalDialog : public juce::Component,
                                  private juce::Timer {
public:
    explicit ProjectLoadingModalDialog(const juce::File& pasta);
    ~ProjectLoadingModalDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setStatus(const juce::String& text, double progress);
    void closeDialog();

    using OnCompleteCallback = std::function<void(
        std::unique_ptr<matriz::model::Project> proj,
        std::string rotuloMaisRecente,
        int64_t revisaoMaisRecente,
        bool isCatalog,
        std::string erroMsg)>;

    static void launch(const juce::File& pasta,
                       juce::Component* parentComp,
                       OnCompleteCallback onComplete);

private:
    void timerCallback() override;

    juce::File pasta_;
    juce::Label lblHeader_;
    juce::Label lblSubHeader_;
    juce::ProgressBar progressBar_;
    juce::Label lblStatus_;

    double targetProgress_ = 0.05;
    double currentProgress_ = 0.05;
    juce::String currentStatusText_;
    std::mutex stateMutex_;
    bool isClosed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectLoadingModalDialog)
};

} // namespace matriz::ui
