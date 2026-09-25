#include "IngestProgressModalDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

IngestProgressModalDialog::IngestProgressModalDialog(int totalFiles,
                                                     std::function<void(bool manterArquivos)> onCancelChoice,
                                                     bool startInScanMode)
    : mode_(startInScanMode ? Mode::Scanning : Mode::Ingesting),
      totalFiles_(juce::jmax(1, totalFiles)),
      onCancelChoice_(std::move(onCancelChoice)),
      progressBar_(progressFraction_),
      startTime_(std::chrono::steady_clock::now()) {
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    if (mode_ == Mode::Scanning) {
        lblHeader_.setText(isPt ? juce::String::fromUTF8("VERIFICANDO INTAKE...") : "CHECKING INTAKE...", juce::dontSendNotification);
        progressFraction_ = -1.0; // Indeterminate bouncing progress
    } else {
        lblHeader_.setText(isPt ? juce::String::fromUTF8("INGERINDO ARQUIVOS") : "INGESTING FILES", juce::dontSendNotification);
    }
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

    if (mode_ == Mode::Scanning) {
        lblEta_.setText(isPt ? juce::String::fromUTF8("Buscando arquivos e verificando itens já no Intake...")
                             : "Scanning files and checking items already in Intake...", juce::dontSendNotification);
    } else {
        lblEta_.setText(isPt ? (juce::String::fromUTF8("Processando ") + juce::String(totalFiles_) + juce::String::fromUTF8(" arquivos — calculando tempo estimado..."))
                             : ("Processing " + juce::String(totalFiles_) + " files — calculating estimated time..."), juce::dontSendNotification);
    }
    lblEta_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    lblEta_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblEta_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblEta_);

    btnCancel_.setButtonText(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
    btnCancel_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffdc2626)); // Red
    btnCancel_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnCancel_.onClick = [this] {
        if (mode_ == Mode::Scanning) {
            if (!isCancelling_.exchange(true)) {
                setCancelling(false);
                if (onCancelChoice_) onCancelChoice_(false);
                closeDialog();
            }
            return;
        }

        if (mode_ == Mode::Finished || isComplete_) {
            closeDialog();
            return;
        }

        if (completedFiles_.load() > 0) {
            showCancelConfirmation();
        } else {
            if (!isCancelling_.exchange(true)) {
                setCancelling(false);
                if (onCancelChoice_) onCancelChoice_(false);
            }
        }
    };
    addAndMakeVisible(btnCancel_);

    // Item novo (hoje): "pular só este arquivo" — visível durante o
    // Ingesting normal, ao lado do Cancel. Some fora desse modo (não faz
    // sentido durante Scanning/Finalizing/confirmação de cancelamento).
    btnSkip_.setButtonText(isPt ? juce::String::fromUTF8("PULAR ESTE ARQUIVO") : "SKIP THIS FILE");
    btnSkip_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnSkip_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnSkip_.setTooltip(isPt ? juce::String::fromUTF8("Desiste do(s) arquivo(s) em andamento agora — o lote continua com o resto. O nome vai pro log do projeto.")
                              : "Gives up on whichever file(s) are in flight right now — the batch continues with the rest. The name goes to the project log.");
    btnSkip_.onClick = [this] {
        if (onSkipCurrent_) onSkipCurrent_();
    };
    addChildComponent(btnSkip_);

    // Confirmation buttons
    btnKeep_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff16a34a)); // Green
    btnKeep_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnKeep_.onClick = [this] {
        confirmingCancel_ = false;
        if (!isCancelling_.exchange(true)) {
            setCancelling(true);
            if (onCancelChoice_) onCancelChoice_(true);
        }
    };
    addChildComponent(btnKeep_);

    btnDiscard_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffdc2626)); // Red
    btnDiscard_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnDiscard_.onClick = [this] {
        confirmingCancel_ = false;
        if (!isCancelling_.exchange(true)) {
            setCancelling(false);
            if (onCancelChoice_) onCancelChoice_(false);
        }
    };
    addChildComponent(btnDiscard_);

    btnResume_.setColour(juce::TextButton::buttonColourId, tk.painel);
    btnResume_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnResume_.onClick = [this] {
        hideCancelConfirmation();
    };
    addChildComponent(btnResume_);

    setSize(600, 270);
    setWantsKeyboardFocus(true);
    startTimer(100); // 10 Hz refresh
}

IngestProgressModalDialog::IngestProgressModalDialog(int totalFiles, std::function<void()> onCancel)
    : IngestProgressModalDialog(totalFiles, [onCancel = std::move(onCancel)](bool) {
          if (onCancel) onCancel();
      }, false) {}

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

    lblEta_.setBounds(r.removeFromTop(28));
    r.removeFromTop(16);

    auto bottom = r.removeFromBottom(36);
    if (confirmingCancel_) {
        int gap = 10;
        int btnW = (bottom.getWidth() - 2 * gap) / 3;
        btnKeep_.setBounds(bottom.removeFromLeft(btnW));
        bottom.removeFromLeft(gap);
        btnDiscard_.setBounds(bottom.removeFromLeft(btnW));
        bottom.removeFromLeft(gap);
        btnResume_.setBounds(bottom);
    } else if (btnSkip_.isVisible()) {
        int gap = 10;
        int totalW = 160 + gap + 200;
        auto area = bottom.withSizeKeepingCentre(totalW, 36);
        btnCancel_.setBounds(area.removeFromLeft(160));
        area.removeFromLeft(gap);
        btnSkip_.setBounds(area);
    } else {
        btnCancel_.setBounds(bottom.withSizeKeepingCentre(160, 36));
    }
}

void IngestProgressModalDialog::updateScanProgress(int totalScanned, int skippedCount, int newCount,
                                                   const juce::String& currentItemName) {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    if (currentItemName.isNotEmpty()) {
        lblFileName_.setText(currentItemName, juce::dontSendNotification);
    }

    if (skippedCount > 0) {
        lblEta_.setText(isPt ? (juce::String::fromUTF8("Varredura: ") + juce::String(totalScanned) +
                                juce::String::fromUTF8(" arquivos (") + juce::String(skippedCount) +
                                juce::String::fromUTF8(" já no Intake — ignorados, ") +
                                juce::String(newCount) + juce::String::fromUTF8(" novos)"))
                             : ("Scanning: " + juce::String(totalScanned) + " files (" +
                                juce::String(skippedCount) + " in Intake — skipped, " +
                                juce::String(newCount) + " new)"),
                        juce::dontSendNotification);
    } else {
        lblEta_.setText(isPt ? (juce::String::fromUTF8("Varredura: ") + juce::String(totalScanned) +
                                juce::String::fromUTF8(" arquivos encontrados (") +
                                juce::String(newCount) + juce::String::fromUTF8(" novos)"))
                             : ("Scanning: " + juce::String(totalScanned) + " files found (" +
                                juce::String(newCount) + " new)"),
                        juce::dontSendNotification);
    }
}

void IngestProgressModalDialog::startIngestMode(int totalFilesToIngest, int skippedCount) {
    mode_ = Mode::Ingesting;
    totalFiles_ = juce::jmax(1, totalFilesToIngest);
    totalSkipped_ = skippedCount;
    completedFiles_.store(0);
    progressFraction_ = 0.0;
    startTime_ = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(timeHistoryLock_);
        recentDurations_.clear();
    }

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    const auto& tk = tema();
    lblHeader_.setText(isPt ? juce::String::fromUTF8("INGERINDO ARQUIVOS") : "INGESTING FILES", juce::dontSendNotification);
    lblHeader_.setColour(juce::Label::textColourId, tk.textoPrimario);

    if (totalSkipped_ > 0) {
        lblFileName_.setText(isPt ? (juce::String::fromUTF8("Iniciando cópia (") + juce::String(totalSkipped_) + juce::String::fromUTF8(" arquivos existentes ignorados)..."))
                                  : ("Starting ingest (" + juce::String(totalSkipped_) + " existing files skipped)..."),
                             juce::dontSendNotification);
    } else {
        lblFileName_.setText(isPt ? juce::String::fromUTF8("Preparando arquivos...") : "Preparing files...", juce::dontSendNotification);
    }

    lblEta_.setText(isPt ? (juce::String::fromUTF8("Processando ") + juce::String(totalFiles_) + juce::String::fromUTF8(" arquivos — calculando tempo estimado..."))
                         : ("Processing " + juce::String(totalFiles_) + " files — calculating estimated time..."), juce::dontSendNotification);

    btnCancel_.setButtonText(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
    btnCancel_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffdc2626));
    btnCancel_.setEnabled(true);
    btnCancel_.setVisible(true);

    btnSkip_.setEnabled(true);
    btnSkip_.setVisible(onSkipCurrent_ != nullptr);

    resized();
}

void IngestProgressModalDialog::finishWithNotice(const juce::String& title, const juce::String& message) {
    mode_ = Mode::Finished;
    isComplete_ = true;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    lblHeader_.setText(title, juce::dontSendNotification);
    lblHeader_.setColour(juce::Label::textColourId, tema().acento);
    lblFileName_.setText("", juce::dontSendNotification);
    lblEta_.setText(message, juce::dontSendNotification);
    progressFraction_ = 1.0;
    progressBar_.repaint();

    btnSkip_.setVisible(false);
    btnCancel_.setButtonText(isPt ? juce::String::fromUTF8("CONCLUIR") : "DONE");
    btnCancel_.setColour(juce::TextButton::buttonColourId, tema().acento);
    btnCancel_.onClick = [this] { closeDialog(); };
    btnCancel_.setEnabled(true);
    btnCancel_.setVisible(true);

    if (btnKeep_.isVisible()) btnKeep_.setVisible(false);
    if (btnDiscard_.isVisible()) btnDiscard_.setVisible(false);
    if (btnResume_.isVisible()) btnResume_.setVisible(false);

    resized();

    juce::Component::SafePointer<IngestProgressModalDialog> safeThis(this);
    juce::Timer::callAfterDelay(1500, [safeThis] {
        if (safeThis != nullptr && safeThis->isComplete_) {
            safeThis->closeDialog();
        }
    });
}

void IngestProgressModalDialog::showCancelConfirmation() {
    confirmingCancel_ = true;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    int done = completedFiles_.load();

    lblHeader_.setText(isPt ? juce::String::fromUTF8("CANCELAR INGESTÃO?") : "CANCEL INGESTION?", juce::dontSendNotification);
    lblHeader_.setColour(juce::Label::textColourId, juce::Colour(0xfff97316));

    lblEta_.setText(isPt ? (juce::String(done) + juce::String::fromUTF8(" arquivo(s) já foram carregados para o Intake.\nO que você deseja fazer?"))
                         : (juce::String(done) + " file(s) have already been imported into Intake.\nWhat would you like to do?"),
                    juce::dontSendNotification);

    btnKeep_.setButtonText(isPt ? (juce::String::fromUTF8("MANTER (") + juce::String(done) + ")")
                                : ("KEEP (" + juce::String(done) + ")"));
    btnDiscard_.setButtonText(isPt ? juce::String::fromUTF8("DESCARTAR TUDO") : "DISCARD ALL");
    btnResume_.setButtonText(isPt ? juce::String::fromUTF8("CONTINUAR") : "CONTINUE");

    btnCancel_.setVisible(false);
    btnSkip_.setVisible(false);
    btnKeep_.setVisible(true);
    btnDiscard_.setVisible(true);
    btnResume_.setVisible(true);

    resized();
}

void IngestProgressModalDialog::hideCancelConfirmation() {
    confirmingCancel_ = false;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    btnKeep_.setVisible(false);
    btnDiscard_.setVisible(false);
    btnResume_.setVisible(false);
    btnCancel_.setVisible(true);
    btnSkip_.setVisible(onSkipCurrent_ != nullptr);

    lblHeader_.setText(isPt ? juce::String::fromUTF8("INGERINDO ARQUIVOS") : "INGESTING FILES", juce::dontSendNotification);
    lblHeader_.setColour(juce::Label::textColourId, tema().textoPrimario);

    resized();
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

void IngestProgressModalDialog::setCancelling(bool mantendoArquivos) {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    isCancelling_.store(true);
    lblHeader_.setText(isPt ? juce::String::fromUTF8("CANCELANDO INGESTÃO...") : "CANCELLING INGESTION...", juce::dontSendNotification);
    lblHeader_.setColour(juce::Label::textColourId, juce::Colour(0xfff97316));

    if (mantendoArquivos) {
        lblEta_.setText(isPt ? juce::String::fromUTF8("Interrompendo... Preservando arquivos importados no Intake...")
                             : "Stopping... Preserving imported files in Intake...", juce::dontSendNotification);
    } else {
        lblEta_.setText(isPt ? juce::String::fromUTF8("Descartando arquivos parciais e revertendo alterações...")
                             : "Discarding partial files and rolling back changes...", juce::dontSendNotification);
    }

    btnCancel_.setEnabled(false);
    btnSkip_.setEnabled(false);
    btnKeep_.setEnabled(false);
    btnDiscard_.setEnabled(false);
    btnResume_.setEnabled(false);
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

void IngestProgressModalDialog::beginFinalizing(int totalPassos) {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    mode_ = Mode::Finalizing;
    // isComplete_ volta a false de propósito: o lote NÃO está completo
    // enquanto a finalização não terminar, e é esse flag que libera o
    // fechamento por tecla e pelo timer.
    isComplete_ = false;
    totalPassosFinalizacao_ = juce::jmax(1, totalPassos);

    lblHeader_.setText(isPt ? juce::String::fromUTF8("FINALIZANDO LOTE...") : "FINISHING UP...",
                       juce::dontSendNotification);
    lblHeader_.setColour(juce::Label::textColourId, tema().textoPrimario);
    lblFileName_.setText(isPt ? juce::String::fromUTF8("Arquivos processados — fechando o lote...")
                              : "Files processed — closing the batch...", juce::dontSendNotification);
    lblEta_.setText(isPt ? juce::String::fromUTF8("Não feche o aplicativo.") : "Do not close the app.",
                    juce::dontSendNotification);

    progressFraction_ = 0.0;
    progressBar_.repaint();

    // Daí pra frente não há mais o que cancelar: os arquivos já estão no
    // Intake e o que resta é só consolidar índices e histórico.
    btnCancel_.setEnabled(false);
    btnSkip_.setVisible(false);
    btnKeep_.setVisible(false);
    btnDiscard_.setVisible(false);
    btnResume_.setVisible(false);
    resized();
}

void IngestProgressModalDialog::setFinalizingStep(int indice, const juce::String& descricao) {
    if (mode_ != Mode::Finalizing) return;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    lblFileName_.setText(descricao, juce::dontSendNotification);
    lblEta_.setText((isPt ? juce::String::fromUTF8("Etapa ") : juce::String("Step "))
                        + juce::String(juce::jlimit(1, totalPassosFinalizacao_, indice + 1))
                        + (isPt ? juce::String::fromUTF8(" de ") : juce::String(" of "))
                        + juce::String(totalPassosFinalizacao_),
                    juce::dontSendNotification);

    progressFraction_ = juce::jlimit(0.0, 1.0,
        static_cast<double>(indice) / static_cast<double>(totalPassosFinalizacao_));
    progressBar_.repaint();
    repaint();
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
    if (mode_ == Mode::Finalizing) {
        // Nada a cancelar nem a fechar aqui: os arquivos já entraram e o que
        // resta é consolidar índices. Engole a tecla (inclusive ESC) pra o
        // modal não sair da frente no meio do trabalho pesado.
        juce::ignoreUnused(key);
        return true;
    }
    if (isComplete_) {
        closeDialog();
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        if (confirmingCancel_) {
            hideCancelConfirmation();
            return true;
        }
        if (mode_ == Mode::Scanning) {
            if (!isCancelling_.exchange(true)) {
                setCancelling(false);
                if (onCancelChoice_) onCancelChoice_(false);
                closeDialog();
            }
            return true;
        }
        if (completedFiles_.load() > 0) {
            showCancelConfirmation();
        } else {
            if (!isCancelling_.exchange(true)) {
                setCancelling(false);
                if (onCancelChoice_) onCancelChoice_(false);
            }
        }
        return true;
    }
    return false;
}

void IngestProgressModalDialog::timerCallback() {
    if (isCancelling_.load() || confirmingCancel_) {
        return;
    }

    if (mode_ == Mode::Scanning) {
        progressBar_.repaint();
        return;
    }

    if (mode_ == Mode::Finished) {
        return;
    }

    if (mode_ == Mode::Finalizing) {
        // Quem manda na barra agora é setFinalizingStep(); aqui só mantém o
        // desenho vivo.
        progressBar_.repaint();
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

            juce::String msg = isPt ? (juce::String::fromUTF8("Todos os ") + juce::String(totalFiles_) + juce::String::fromUTF8(" arquivos novos processados com sucesso!"))
                                    : ("All " + juce::String(totalFiles_) + " new files processed successfully!");
            if (totalSkipped_ > 0) {
                msg += isPt ? (juce::String::fromUTF8(" (") + juce::String(totalSkipped_) + juce::String::fromUTF8(" já existentes no Intake foram ignorados)"))
                            : (" (" + juce::String(totalSkipped_) + " already in Intake were skipped)");
            }
            lblEta_.setText(msg, juce::dontSendNotification);

            btnCancel_.setButtonText(isPt ? juce::String::fromUTF8("CONCLUIR") : "DONE");
            btnCancel_.setColour(juce::TextButton::buttonColourId, tema().acento);
            btnCancel_.onClick = [this] { closeDialog(); };

            juce::Component::SafePointer<IngestProgressModalDialog> safeThis(this);
            juce::Timer::callAfterDelay(900, [safeThis] {
                // Se nesse meio tempo o lote entrou na fase de finalização,
                // este fechamento automático NÃO vale mais: era ele que
                // tirava o modal da frente enquanto a message thread ainda
                // tinha vários segundos de trabalho pesado pela frente.
                if (safeThis != nullptr && safeThis->isComplete_
                    && safeThis->mode_ != Mode::Finalizing) {
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

IngestProgressModalDialog* IngestProgressModalDialog::showModal(int totalFiles,
                                                                std::function<void(bool manterArquivos)> onCancelChoice) {
    auto* dialog = new IngestProgressModalDialog(totalFiles, std::move(onCancelChoice), false);
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = isPt ? juce::String::fromUTF8("Ingerir Arquivos") : "Ingest Files";
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

IngestProgressModalDialog* IngestProgressModalDialog::showModal(int totalFiles, std::function<void()> onCancel) {
    return showModal(totalFiles, [onCancel = std::move(onCancel)](bool) {
        if (onCancel) onCancel();
    });
}

IngestProgressModalDialog* IngestProgressModalDialog::showScanModal(std::function<void()> onCancelScan) {
    auto* dialog = new IngestProgressModalDialog(1, [onCancelScan = std::move(onCancelScan)](bool) {
        if (onCancelScan) onCancelScan();
    }, true);

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = isPt ? juce::String::fromUTF8("Verificando Arquivos") : "Checking Files";
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
