#include "RescanProgressModalDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

class RescanWorkerThread : public juce::Thread {
public:
    RescanWorkerThread(
        std::vector<matriz::vault::RescanPair> pares,
        matriz::db::Database& db,
        std::atomic<bool>& cancelRequested,
        std::atomic<int>& processedCount,
        std::atomic<int>& totalFiles,
        std::function<void(const juce::String&)> onFileUpdated,
        std::function<void(bool, std::vector<matriz::vault::RescanFileResult>)> onDone)
        : juce::Thread("RescanWorker"),
          pares_(std::move(pares)),
          db_(db),
          cancelRequested_(cancelRequested),
          processedCount_(processedCount),
          totalFiles_(totalFiles),
          onFileUpdated_(std::move(onFileUpdated)),
          onDone_(std::move(onDone)) {}

    void run() override {
        std::vector<matriz::vault::RescanFileResult> resultados;
        bool ok = matriz::vault::RescanEngine::executarVarredura(
            pares_, db_, cancelRequested_,
            [this](int processed, int total, const juce::String& currentFile) {
                processedCount_.store(processed);
                totalFiles_.store(total);
                if (onFileUpdated_) onFileUpdated_(currentFile);
            },
            resultados);

        if (onDone_) {
            onDone_(ok && !cancelRequested_.load(), std::move(resultados));
        }
    }

private:
    std::vector<matriz::vault::RescanPair> pares_;
    matriz::db::Database& db_;
    std::atomic<bool>& cancelRequested_;
    std::atomic<int>& processedCount_;
    std::atomic<int>& totalFiles_;
    std::function<void(const juce::String&)> onFileUpdated_;
    std::function<void(bool, std::vector<matriz::vault::RescanFileResult>)> onDone_;
};

RescanProgressModalDialog::RescanProgressModalDialog(
    std::vector<matriz::vault::RescanPair> pares,
    matriz::db::Database& db,
    std::function<void(bool, std::vector<matriz::vault::RescanFileResult>)> onCompleto)
    : pares_(std::move(pares)),
      db_(db),
      onCompleto_(std::move(onCompleto)),
      progressBar_(progressFraction_) {

    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    lblHeader_.setText(isPt ? juce::String::fromUTF8("Rescan de Fontes de Backup") : "Rescan Backup Sources", juce::dontSendNotification);
    lblHeader_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    lblHeader_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblHeader_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblHeader_);

    lblFileName_.setText(isPt ? juce::String::fromUTF8("Calculando arquivos...") : "Calculating files to scan...", juce::dontSendNotification);
    lblFileName_.setFont(juce::Font(juce::FontOptions(12.5f)));
    lblFileName_.setColour(juce::Label::textColourId, tk.textoSecundario);
    lblFileName_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblFileName_);

    progressBar_.setColour(juce::ProgressBar::foregroundColourId, tk.acento);
    progressBar_.setColour(juce::ProgressBar::backgroundColourId, tk.painelAlt);
    addAndMakeVisible(progressBar_);

    lblEta_.setText(isPt ? juce::String::fromUTF8("Iniciando varredura...") : "Starting scan...", juce::dontSendNotification);
    lblEta_.setFont(juce::Font(juce::FontOptions(11.5f)));
    lblEta_.setColour(juce::Label::textColourId, tk.textoTerciario);
    lblEta_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblEta_);

    btnCancel_.setButtonText(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
    btnCancel_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancel_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnCancel_.onClick = [this] { cancelarVarredura(); };
    addAndMakeVisible(btnCancel_);

    startTime_ = std::chrono::steady_clock::now();
    setSize(460, 200);

    iniciarVarredura();
    startTimerHz(15);
}

RescanProgressModalDialog::~RescanProgressModalDialog() {
    stopTimer();
    cancelRequested_.store(true);
    if (workerThread_) {
        workerThread_->stopThread(3000);
        workerThread_.reset();
    }
}

void RescanProgressModalDialog::iniciarVarredura() {
    juce::Component::SafePointer<RescanProgressModalDialog> safeThis(this);

    workerThread_ = std::make_unique<RescanWorkerThread>(
        pares_, db_, cancelRequested_, processedCount_, totalFiles_,
        [safeThis](const juce::String& fname) {
            if (safeThis) {
                const juce::ScopedLock sl(safeThis->resultsLock_);
                safeThis->currentFileName_ = fname;
            }
        },
        [safeThis](bool ok, std::vector<matriz::vault::RescanFileResult> res) {
            if (safeThis) {
                {
                    const juce::ScopedLock sl(safeThis->resultsLock_);
                    safeThis->scanSuccess_.store(ok);
                    safeThis->resultados_ = std::move(res);
                    safeThis->isFinished_.store(true);
                }
            }
        });

    workerThread_->startThread();
}

void RescanProgressModalDialog::cancelarVarredura() {
    cancelRequested_.store(true);
    btnCancel_.setEnabled(false);
    btnCancel_.setButtonText("Cancelling...");
    lblFileName_.setText("Aborting rescan...", juce::dontSendNotification);

    juce::Component::SafePointer<RescanProgressModalDialog> safeThis(this);
    juce::Timer::callAfterDelay(100, [safeThis] {
        if (!safeThis) return;
        safeThis->closeDialog();
        if (safeThis->onCompleto_) {
            safeThis->onCompleto_(false, {});
        }
    });
}

void RescanProgressModalDialog::closeDialog() {
    stopTimer();
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
    }
}

juce::String RescanProgressModalDialog::formatRemainingTime(double secondsRemaining) {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    if (secondsRemaining <= 0.0) return isPt ? juce::String::fromUTF8("poucos segundos restantes") : "few seconds remaining";
    int totalSecs = static_cast<int>(secondsRemaining);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    if (mins > 0) {
        return isPt ? juce::String::formatted("aprox. %d min %02d seg restantes", mins, secs)
                    : juce::String::formatted("approx. %d min %02d sec remaining", mins, secs);
    }
    return isPt ? juce::String::formatted("aprox. %d seg restantes", secs)
                : juce::String::formatted("approx. %d sec remaining", secs);
}

void RescanProgressModalDialog::timerCallback() {
    if (cancelRequested_.load()) return;

    if (isFinished_.load()) {
        stopTimer();
        bool ok = scanSuccess_.load();
        std::vector<matriz::vault::RescanFileResult> res;
        {
            const juce::ScopedLock sl(resultsLock_);
            res = std::move(resultados_);
        }
        closeDialog();
        if (onCompleto_) {
            onCompleto_(ok, std::move(res));
        }
        return;
    }

    int processed = processedCount_.load();
    int total = totalFiles_.load();
    juce::String currentFile;
    {
        const juce::ScopedLock sl(resultsLock_);
        currentFile = currentFileName_;
    }

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    if (total > 0) {
        progressFraction_ = juce::jlimit(0.0, 1.0, static_cast<double>(processed) / static_cast<double>(total));
        lblFileName_.setText(currentFile.isNotEmpty() ? currentFile : (isPt ? juce::String::fromUTF8("Escaneando...") : "Scanning..."), juce::dontSendNotification);

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime_).count();
        if (processed > 0) {
            double avgSecs = elapsed / static_cast<double>(processed);
            double remaining = avgSecs * static_cast<double>(total - processed);
            if (isPt) {
                lblEta_.setText(juce::String::fromUTF8("Escaneando ") + juce::String(processed) + " de " + juce::String(total) +
                                juce::String::fromUTF8(" arquivos — ") + formatRemainingTime(remaining), juce::dontSendNotification);
            } else {
                lblEta_.setText("Scanning " + juce::String(processed) + " of " + juce::String(total) +
                                " files — " + formatRemainingTime(remaining), juce::dontSendNotification);
            }
        } else {
            lblEta_.setText(isPt ? (juce::String::fromUTF8("Escaneando ") + juce::String(processed) + " de " + juce::String(total) + " arquivos...")
                                 : ("Scanning " + juce::String(processed) + " of " + juce::String(total) + " files..."), juce::dontSendNotification);
        }
    } else {
        progressFraction_ = -1.0; // Indeterminate
        lblEta_.setText(isPt ? juce::String::fromUTF8("Escaneando diretórios...") : "Scanning directories...", juce::dontSendNotification);
    }

    repaint();
}

void RescanProgressModalDialog::lookAndFeelChanged() {
    const auto& tk = tema();
    lblHeader_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblFileName_.setColour(juce::Label::textColourId, tk.textoSecundario);
    lblEta_.setColour(juce::Label::textColourId, tk.textoTerciario);
    progressBar_.setColour(juce::ProgressBar::foregroundColourId, tk.acento);
    progressBar_.setColour(juce::ProgressBar::backgroundColourId, tk.painelAlt);
    btnCancel_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancel_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    repaint();
}

void RescanProgressModalDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);
}

void RescanProgressModalDialog::resized() {
    auto area = getLocalBounds().reduced(24, 18);

    lblHeader_.setBounds(area.removeFromTop(24));
    area.removeFromTop(10);

    lblFileName_.setBounds(area.removeFromTop(20));
    area.removeFromTop(8);

    progressBar_.setBounds(area.removeFromTop(14));
    area.removeFromTop(8);

    lblEta_.setBounds(area.removeFromTop(18));
    area.removeFromTop(12);

    btnCancel_.setBounds(area.removeFromBottom(28).withSizeKeepingCentre(120, 28));
}

RescanProgressModalDialog* RescanProgressModalDialog::showModal(
    std::vector<matriz::vault::RescanPair> pares,
    matriz::db::Database& db,
    std::function<void(bool, std::vector<matriz::vault::RescanFileResult>)> onCompleto) {

    auto* dialog = new RescanProgressModalDialog(std::move(pares), db, std::move(onCompleto));
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = isPt ? juce::String::fromUTF8("Rescan de Fontes de Backup") : "Rescan Backup Sources";
    options.dialogBackgroundColour = tema().painel;
    options.escapeKeyTriggersCloseButton = false;
    options.useNativeTitleBar = false;
    options.resizable = false;

    if (auto* tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        auto area = tela->userBounds;
        dialog->setCentrePosition(area.getCentreX(), area.getCentreY());
    }

    options.launchAsync();
    return dialog;
}

} // namespace matriz::ui
