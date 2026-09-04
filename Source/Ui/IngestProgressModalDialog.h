#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <deque>
#include <chrono>

namespace matriz::ui {

class IngestProgressModalDialog : public juce::Component, private juce::Timer {
public:
    IngestProgressModalDialog(int totalFiles, std::function<void()> onCancel);
    ~IngestProgressModalDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Called as each file completes to update dynamic moving average ETA
    void recordFileProcessed(double durationSeconds);

    // Updates processed count and checks completion
    void updateProgress(int completedCount, const juce::String& currentFileName = {});

    // Sets cancelling state
    void setCancelling();

    // Closes and dismisses dialog cleanly
    void closeDialog();
    bool isComplete() const { return isComplete_; }

    bool keyPressed(const juce::KeyPress& key) override;

    static IngestProgressModalDialog* showModal(int totalFiles, std::function<void()> onCancel);

private:
    int totalFiles_ = 0;
    std::atomic<int> completedFiles_{0};
    std::atomic<bool> isCancelling_{false};
    bool isComplete_ = false;
    std::function<void()> onCancel_;

    // Moving average ETA calculation
    std::mutex timeHistoryLock_;
    std::deque<double> recentDurations_; // durations in seconds
    static constexpr size_t kMaxMovingAverageWindow = 25;
    std::chrono::steady_clock::time_point startTime_;

    juce::Label lblHeader_;
    juce::Label lblFileName_;
    juce::Label lblEta_;
    juce::ProgressBar progressBar_;
    double progressFraction_ = 0.0;
    juce::TextButton btnCancel_;

    void timerCallback() override;
    juce::String formatRemainingTime(double secondsRemaining);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IngestProgressModalDialog)
};

} // namespace matriz::ui
