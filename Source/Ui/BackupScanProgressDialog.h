#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include "../Consolidacao/BackupScanEngine.h"
#include "../Db/Database.h"

namespace matriz::ui {

class BackupScanProgressDialog : public juce::Component, private juce::Timer {
public:
    BackupScanProgressDialog(
        matriz::db::Database& db,
        const juce::File& pastaProjeto,
        const juce::File& destinoAtivo,
        matriz::consolidacao::PlanoConsolidacao plano,
        std::function<void(matriz::consolidacao::ResultadoScanBackup)> onCompleto);
    ~BackupScanProgressDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    void closeDialog();

    static void showModal(
        matriz::db::Database& db,
        const juce::File& pastaProjeto,
        const juce::File& destinoAtivo,
        const matriz::consolidacao::PlanoConsolidacao& plano,
        std::function<void(matriz::consolidacao::ResultadoScanBackup)> onCompleto);

private:
    void timerCallback() override;
    void iniciarVarredura();
    void cancelarVarredura();

    matriz::db::Database& db_;
    juce::File pastaProjeto_;
    juce::File destinoAtivo_;
    matriz::consolidacao::PlanoConsolidacao plano_;
    std::function<void(matriz::consolidacao::ResultadoScanBackup)> onCompleto_;

    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> isFinished_{false};

    juce::CriticalSection resultsLock_;
    matriz::consolidacao::ResultadoScanBackup resultado_;
    juce::String currentStageText_;
    double progressFraction_ = 0.0;

    juce::Label lblHeader_;
    juce::Label lblStage_;
    juce::ProgressBar progressBar_;
    juce::TextButton btnCancel_;

    std::unique_ptr<juce::Thread> workerThread_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackupScanProgressDialog)
};

} // namespace matriz::ui
