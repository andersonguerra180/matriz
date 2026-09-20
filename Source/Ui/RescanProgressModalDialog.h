#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include "../Vault/RescanEngine.h"

namespace matriz::ui {

class RescanProgressModalDialog : public juce::Component, private juce::Timer {
public:
    RescanProgressModalDialog(
        std::vector<matriz::vault::RescanPair> pares,
        matriz::db::Database& db,
        std::function<void(bool sucesso, std::vector<matriz::vault::RescanFileResult> resultados)> onCompleto);
    ~RescanProgressModalDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void closeDialog();

    static RescanProgressModalDialog* showModal(
        std::vector<matriz::vault::RescanPair> pares,
        matriz::db::Database& db,
        std::function<void(bool sucesso, std::vector<matriz::vault::RescanFileResult> resultados)> onCompleto);

private:
    void timerCallback() override;
    void iniciarVarredura();
    void cancelarVarredura();
    juce::String formatRemainingTime(double secondsRemaining);

    std::vector<matriz::vault::RescanPair> pares_;
    matriz::db::Database& db_;
    std::function<void(bool, std::vector<matriz::vault::RescanFileResult>)> onCompleto_;

    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> isFinished_{false};
    std::atomic<bool> scanSuccess_{false};
    std::atomic<int> processedCount_{0};
    std::atomic<int> totalFiles_{0};

    juce::CriticalSection resultsLock_;
    std::vector<matriz::vault::RescanFileResult> resultados_;
    juce::String currentFileName_;

    juce::CriticalSection timeHistoryLock_;
    std::deque<double> recentDurations_;
    std::chrono::steady_clock::time_point startTime_;

    juce::Label lblHeader_;
    juce::Label lblFileName_;
    juce::Label lblEta_;
    juce::ProgressBar progressBar_;
    double progressFraction_ = 0.0;
    juce::TextButton btnCancel_;

    std::unique_ptr<juce::Thread> workerThread_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RescanProgressModalDialog)
};

} // namespace matriz::ui
