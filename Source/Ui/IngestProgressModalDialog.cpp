#include "IngestProgressModalDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

IngestProgressModalDialog::IngestProgressModalDialog(int totalFiles, std::function<void()> onCancel)
    : totalFiles_(juce::jmax(1, totalFiles)),
      onCancel_(std::move(onCancel)),
      progressBar_(progressFraction_),
      startTime_(std::chrono::steady_clock::now()) {
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    lblHeader_.setText(isPt ? juce::String::fromUTF8("INGERINDO ARQUIVOS") : "INGESTING FILES", juce::dontSendNotification);
    lblHeader_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    lblHeader_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblHeader_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblHeader_);

    lblFileName_.setText(isPt ? juce::String::fromUTF8("Preparando arquivos...") : "Preparing files...", juce::dontSendNotification);
    lblFileName_.setFont(juce::Font(juce::FontOptions(13.0f)));
    lblFileName_.setColour(juce::Label::textColourId, tk.textoSecundario);
    lblFileName_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblFileName_);

    progressBar_.setColour(juce::ProgressBar::backgroundColourId, tk.fundo);
    progressBar_.setColour(juce::ProgressBar::foregroundColourId, tk.acento);
    addAndMakeVisible(progressBar_);

    lblEta_.setText(isPt ? (juce::String::fromUTF8("Processando ") + juce::String(totalFiles_) + juce::String::fromUTF8(" arquivos — calculando tempo estimado..."))
                         : ("Processing " + juce::String(totalFiles_) + " files — calculating estimated time..."), juce::dontSendNotification);
    lblEta_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    lblEta_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblEta_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblEta_);

    btnCancel_.setButtonText(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
    btnCancel_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffdc2626)); // Red
    btnCancel_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnCancel_.onClick = [this] {
        if (!isCancelling_.exchange(true)) {
            setCancelling();
            if (onCancel_) onCancel_();
        }
    };
    addAndMakeVisible(btnCancel_);

    setSize(560, 260);
    setWantsKeyboardFocus(true);
    startTimer(100); // 10 Hz refresh
}

IngestProgressModalDialog::~IngestProgressModalDialog() {
    stopTimer();
}

void IngestProgressModalDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);
    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 2);
}

void IngestProgressModalDialog::resized() {
    auto r = getLocalBounds().reduced(24);

    lblHeader_.setBounds(r.removeFromTop(28));
    r.removeFromTop(8);

    lblFileName_.setBounds(r.removeFromTop(20));
    r.removeFromTop(16);

    progressBar_.setBounds(r.removeFromTop(26));
    r.removeFromTop(12);

    lblEta_.setBounds(r.removeFromTop(24));
    r.removeFromTop(20);

    auto bottom = r.removeFromBottom(36);
    btnCancel_.setBounds(bottom.withSizeKeepingCentre(160, 36));
}

void IngestProgressModalDialog::recordFileProcessed(double durationSeconds) {
    std::lock_guard<std::mutex> lock(timeHistoryLock_);
    recentDurations_.push_back(durationSeconds);
    if (recentDurations_.size() > kMaxMovingAverageWindow) {
        recentDurations_.pop_front();
    }
}

void IngestProgressModalDialog::updateProgress(int completedCount, const juce::String& currentFileName) {
    completedFiles_.store(completedCount);
    if (currentFileName.isNotEmpty()) {
        lblFileName_.setText(currentFileName, juce::dontSendNotification);
    }
}

void IngestProgressModalDialog::setCancelling() {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    isCancelling_.store(true);
    lblHeader_.setText(isPt ? juce::String::fromUTF8("CANCELANDO INGESTÃO...") : "CANCELLING INGESTION...", juce::dontSendNotification);
    lblHeader_.setColour(juce::Label::textColourId, juce::Colour(0xfff97316));
    lblEta_.setText(isPt ? juce::String::fromUTF8("Descartando proxies parciais e revertendo alterações...")
                         : "Discarding partial proxies and rolling back changes...", juce::dontSendNotification);
    btnCancel_.setEnabled(false);
}

juce::String IngestProgressModalDialog::formatRemainingTime(double secondsRemaining) {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    if (secondsRemaining <= 1.0) {
        return isPt ? juce::String::fromUTF8("quase concluído") : "almost finished";
    }
    if (secondsRemaining < 60.0) {
        int secs = static_cast<int>(std::round(secondsRemaining));
        return isPt ? (juce::String::fromUTF8("estimado ") + juce::String(secs) + juce::String::fromUTF8(" segundos restantes"))
                    : ("estimated " + juce::String(secs) + " seconds remaining");
    }
    int mins = static_cast<int>(std::round(secondsRemaining / 60.0));
    if (mins == 1) {
        return isPt ? juce::String::fromUTF8("estimado 1 minuto restante") : "estimated 1 minute remaining";
    }
    return isPt ? (juce::String::fromUTF8("estimado ") + juce::String(mins) + juce::String::fromUTF8(" minutos restantes"))
                : ("estimated " + juce::String(mins) + " minutes remaining");
}

void IngestProgressModalDialog::closeDialog() {
    stopTimer();
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(1);
        dw->setVisible(false);
        juce::Component::SafePointer<juce::DialogWindow> safeDw(dw);
        juce::MessageManager::callAsync([safeDw] {
            if (safeDw != nullptr) {
                safeDw->removeFromDesktop();
                delete safeDw.getComponent();
            }
        });
    } else {
        setVisible(false);
    }
}

bool IngestProgressModalDialog::keyPressed(const juce::KeyPress& key) {
    if (isComplete_) {
        closeDialog();
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        if (!isCancelling_.exchange(true)) {
            setCancelling();
            if (onCancel_) onCancel_();
        }
        return true;
    }
    return false;
}

void IngestProgressModalDialog::timerCallback() {
    if (isCancelling_.load()) {
        return;
    }

    int completed = completedFiles_.load();
    progressFraction_ = juce::jlimit(0.0, 1.0, static_cast<double>(completed) / static_cast<double>(totalFiles_));
    progressBar_.repaint();

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    int remaining = juce::jmax(0, totalFiles_ - completed);
    if (remaining == 0) {
        if (!isComplete_) {
            isComplete_ = true;
            lblHeader_.setText(isPt ? juce::String::fromUTF8("INGESTÃO CONCLUÍDA") : "INGESTION COMPLETE", juce::dontSendNotification);
            lblHeader_.setColour(juce::Label::textColourId, tema().acento);
            lblEta_.setText(isPt ? (juce::String::fromUTF8("Todos os ") + juce::String(totalFiles_) + juce::String::fromUTF8(" arquivos processados com sucesso!"))
                                 : ("All " + juce::String(totalFiles_) + " files processed successfully!"), juce::dontSendNotification);
            btnCancel_.setButtonText(isPt ? juce::String::fromUTF8("CONCLUIR") : "DONE");
            btnCancel_.setColour(juce::TextButton::buttonColourId, tema().acento);
            btnCancel_.onClick = [this] { closeDialog(); };

            juce::Component::SafePointer<IngestProgressModalDialog> safeThis(this);
            juce::Timer::callAfterDelay(900, [safeThis] {
                if (safeThis != nullptr && safeThis->isComplete_) {
                    safeThis->closeDialog();
                }
            });
        }
        return;
    }

    double avgSecsPerFile = 0.0;
    {
        std::lock_guard<std::mutex> lock(timeHistoryLock_);
        if (!recentDurations_.empty()) {
            double sum = 0.0;
            for (double d : recentDurations_) sum += d;
            avgSecsPerFile = sum / static_cast<double>(recentDurations_.size());
        }
    }

    // Fallback if not enough samples: use wall-clock elapsed time
    if (avgSecsPerFile <= 0.0001 && completed > 0) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime_).count();
        avgSecsPerFile = elapsed / static_cast<double>(completed);
    }

    if (avgSecsPerFile > 0.0001) {
        double secondsRemaining = avgSecsPerFile * remaining;
        juce::String timeStr = formatRemainingTime(secondsRemaining);
        if (isPt) {
            lblEta_.setText(juce::String::fromUTF8("Processando ") + juce::String(completed) + " de " + juce::String(totalFiles_) +
                            juce::String::fromUTF8(" arquivos — ") + timeStr, juce::dontSendNotification);
        } else {
            lblEta_.setText("Processing " + juce::String(completed) + " of " + juce::String(totalFiles_) +
                            " files — " + timeStr, juce::dontSendNotification);
        }
    } else {
        lblEta_.setText(isPt ? (juce::String::fromUTF8("Processando ") + juce::String(completed) + " de " + juce::String(totalFiles_) +
                                juce::String::fromUTF8(" arquivos — calculando tempo estimado..."))
                             : ("Processing " + juce::String(completed) + " of " + juce::String(totalFiles_) +
                                " files — calculating estimated time..."), juce::dontSendNotification);
    }
}

IngestProgressModalDialog* IngestProgressModalDialog::showModal(int totalFiles, std::function<void()> onCancel) {
    auto* dialog = new IngestProgressModalDialog(totalFiles, std::move(onCancel));
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = isPt ? juce::String::fromUTF8("Ingerir Arquivos") : "Ingest Files";
    options.dialogBackgroundColour = tema().painel;
    options.escapeKeyTriggersCloseButton = false; // Must click Cancel button to abort atomically
    options.useNativeTitleBar = false;
    options.resizable = false;

    // Center on screen
    if (auto* tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        auto area = tela->userBounds;
        dialog->setCentrePosition(area.getCentreX(), area.getCentreY());
    }

    options.launchAsync();
    return dialog;
}

} // namespace matriz::ui
