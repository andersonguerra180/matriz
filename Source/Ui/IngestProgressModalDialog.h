#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <deque>
#include <chrono>
#include <functional>

namespace matriz::ui {

class IngestProgressModalDialog : public juce::Component, private juce::Timer {
public:
    enum class Mode {
        Scanning,
        Ingesting,
        // Fase de fechamento do lote (árvore, grade, intake, filtros, log,
        // vault). Os arquivos já foram processados, mas o trabalho NÃO
        // acabou — antes isto rodava com o modal já fechado e a janela
        // travava em silêncio. Nesta fase a barra passa a medir as etapas
        // de finalização e o diálogo nunca se fecha sozinho.
        Finalizing,
        Finished
    };

    IngestProgressModalDialog(int totalFiles, std::function<void(bool manterArquivos)> onCancelChoice, bool startInScanMode = false);
    IngestProgressModalDialog(int totalFiles, std::function<void()> onCancel);
    ~IngestProgressModalDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Phase 1: Scanning & Duplicate Checking
    void updateScanProgress(int totalScanned, int skippedCount, int newCount, const juce::String& currentItemName);
    void startIngestMode(int totalFilesToIngest, int totalSkipped);
    void finishWithNotice(const juce::String& title, const juce::String& message);

    // Phase 2: Ingesting
    // Called as each file completes to update dynamic moving average ETA
    void recordFileProcessed(double durationSeconds);

    // Updates processed count and checks completion
    void updateProgress(int completedCount, const juce::String& currentFileName = {});

    // Mais arquivos entraram no MESMO lote em andamento (ex.: um segundo
    // drag-and-drop antes do primeiro terminar de copiar) — ao contrário de
    // startIngestMode(), NÃO reseta completedFiles_/progressFraction_; só
    // corrige o total mostrado ("X of TOTAL") pra bater com o que o modal
    // vai de fato processar. Atribui (não jmax): o chamador passa
    // ingestsTotalLote_, a fonte única — jmax deixava um total de lote
    // anterior (ex.: 586) num modal reaproveitado por um lote de 560.
    void ajustarTotalArquivos(int novoTotal) { totalFiles_ = juce::jmax(1, novoTotal); }

    // Sets cancelling state
    void setCancelling(bool mantendoArquivos = false);

    // Item novo (hoje): deixa o operador desistir só do arquivo atual sem
    // cancelar o lote inteiro — visível durante o modo Ingesting. O
    // callback decide o que "atual" significa (ver MainComponent), esta
    // classe só repassa o clique.
    void setOnSkipCurrent(std::function<void()> onSkipCurrent) { onSkipCurrent_ = std::move(onSkipCurrent); }

    // Fase 3: finalização do lote. beginFinalizing() troca o modo, desliga
    // o auto-fechamento e reinicia a barra para medir `totalPassos` etapas;
    // setFinalizingStep() reporta a etapa atual (base 0) e o seu rótulo.
    void beginFinalizing(int totalPassos);
    void setFinalizingStep(int indice, const juce::String& descricao);
    bool isFinalizing() const { return mode_ == Mode::Finalizing; }

    // Closes and dismisses dialog cleanly
    void closeDialog();
    bool isComplete() const { return isComplete_; }

    bool keyPressed(const juce::KeyPress& key) override;

    static IngestProgressModalDialog* showModal(int totalFiles, std::function<void(bool manterArquivos)> onCancelChoice);
    static IngestProgressModalDialog* showModal(int totalFiles, std::function<void()> onCancel);
    static IngestProgressModalDialog* showScanModal(std::function<void()> onCancelScan);

private:
    Mode mode_ = Mode::Ingesting;
    int totalFiles_ = 0;
    int totalSkipped_ = 0;
    int totalPassosFinalizacao_ = 0;
    std::atomic<int> completedFiles_{0};
    std::atomic<bool> isCancelling_{false};
    bool isComplete_ = false;
    bool confirmingCancel_ = false;
    std::function<void(bool manterArquivos)> onCancelChoice_;
    std::function<void()> onSkipCurrent_;

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
    juce::TextButton btnSkip_;

    // Confirmation buttons when cancelling with already imported files
    juce::TextButton btnKeep_;
    juce::TextButton btnDiscard_;
    juce::TextButton btnResume_;

    void timerCallback() override;
    juce::String formatRemainingTime(double secondsRemaining);
    void showCancelConfirmation();
    void hideCancelConfirmation();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IngestProgressModalDialog)
};

} // namespace matriz::ui
