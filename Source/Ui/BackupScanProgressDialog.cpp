#include "BackupScanProgressDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

namespace {

class BackupScanWorkerThread : public juce::Thread {
public:
    BackupScanWorkerThread(
        matriz::db::Database& db,
        const juce::File& pastaProjeto,
        const juce::File& destinoAtivo,
        const matriz::consolidacao::PlanoConsolidacao& plano,
        std::atomic<bool>& cancelRequested,
        std::function<void(const juce::String&, double)> onProgressUpdate,
        std::function<void(matriz::consolidacao::ResultadoScanBackup)> onDone)
        : juce::Thread("BackupScanWorker"),
          db_(db),
          pastaProjeto_(pastaProjeto),
          destinoAtivo_(destinoAtivo),
          plano_(plano),
          cancelRequested_(cancelRequested),
          onProgressUpdate_(std::move(onProgressUpdate)),
          onDone_(std::move(onDone)) {}

    void run() override {
        auto res = matriz::consolidacao::BackupScanEngine::executarScanDestino(
            db_, pastaProjeto_, destinoAtivo_, plano_,
            [this](const juce::String& faseDescricao, int feito, int total) -> bool {
                if (cancelRequested_.load()) return false;
                double frac = total > 0 ? static_cast<double>(feito) / static_cast<double>(total) : 0.0;
                if (onProgressUpdate_) onProgressUpdate_(faseDescricao, frac);
                return !cancelRequested_.load();
            },
            &cancelRequested_);

        if (onDone_) {
            onDone_(std::move(res));
        }
    }

private:
    matriz::db::Database& db_;
    juce::File pastaProjeto_;
    juce::File destinoAtivo_;
    matriz::consolidacao::PlanoConsolidacao plano_;
    std::atomic<bool>& cancelRequested_;
    std::function<void(const juce::String&, double)> onProgressUpdate_;
    std::function<void(matriz::consolidacao::ResultadoScanBackup)> onDone_;
};

} // namespace

BackupScanProgressDialog::BackupScanProgressDialog(
    matriz::db::Database& db,
    const juce::File& pastaProjeto,
    const juce::File& destinoAtivo,
    matriz::consolidacao::PlanoConsolidacao plano,
    std::function<void(matriz::consolidacao::ResultadoScanBackup)> onCompleto)
    : db_(db),
      pastaProjeto_(pastaProjeto),
      destinoAtivo_(destinoAtivo),
      plano_(std::move(plano)),
      onCompleto_(std::move(onCompleto)),
      progressBar_(progressFraction_)
{
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    lblHeader_.setText(isPt ? juce::String::fromUTF8("Verificação de Integridade do Backup") : "Backup Destination Integrity Scan", juce::dontSendNotification);
    lblHeader_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    lblHeader_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblHeader_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblHeader_);

    currentStageText_ = isPt ? juce::String::fromUTF8("Iniciando varredura...") : "Starting scan...";
    lblStage_.setText(currentStageText_, juce::dontSendNotification);
    lblStage_.setFont(juce::Font(juce::FontOptions(12.5f)));
    lblStage_.setColour(juce::Label::textColourId, tk.textoSecundario);
    lblStage_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblStage_);

    progressBar_.setColour(juce::ProgressBar::foregroundColourId, tk.acento);
    progressBar_.setColour(juce::ProgressBar::backgroundColourId, tk.painelAlt);
    addAndMakeVisible(progressBar_);

    btnCancel_.setButtonText(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
    btnCancel_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancel_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnCancel_.onClick = [this] { cancelarVarredura(); };
    addAndMakeVisible(btnCancel_);

    setSize(480, 180);
    setWantsKeyboardFocus(true);

    iniciarVarredura();
    startTimerHz(20);
}

BackupScanProgressDialog::~BackupScanProgressDialog() {
    stopTimer();
    cancelRequested_.store(true);
    if (workerThread_ && workerThread_->isThreadRunning()) {
        workerThread_->stopThread(2000);
    }
}

void BackupScanProgressDialog::lookAndFeelChanged() {
    const auto& tk = tema();
    lblHeader_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblStage_.setColour(juce::Label::textColourId, tk.textoSecundario);
    progressBar_.setColour(juce::ProgressBar::foregroundColourId, tk.acento);
    progressBar_.setColour(juce::ProgressBar::backgroundColourId, tk.painelAlt);
    btnCancel_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancel_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    repaint();
}

void BackupScanProgressDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);
    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);
}

void BackupScanProgressDialog::resized() {
    auto r = getLocalBounds().reduced(20, 16);
    lblHeader_.setBounds(r.removeFromTop(24));
    r.removeFromTop(8);
    lblStage_.setBounds(r.removeFromTop(20));
    r.removeFromTop(12);
    progressBar_.setBounds(r.removeFromTop(24));
    r.removeFromTop(16);
    auto btnArea = r.removeFromBottom(28);
    btnCancel_.setBounds(btnArea.withSizeKeepingCentre(120, 28));
}

bool BackupScanProgressDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        cancelarVarredura();
        return true;
    }
    return false;
}

void BackupScanProgressDialog::iniciarVarredura() {
    cancelRequested_.store(false);
    isFinished_.store(false);

    workerThread_ = std::make_unique<BackupScanWorkerThread>(
        db_, pastaProjeto_, destinoAtivo_, plano_, cancelRequested_,
        [this](const juce::String& stage, double fraction) {
            juce::ScopedLock sl(resultsLock_);
            currentStageText_ = stage;
            progressFraction_ = fraction;
        },
        [this](matriz::consolidacao::ResultadoScanBackup res) {
            juce::ScopedLock sl(resultsLock_);
            resultado_ = std::move(res);
            isFinished_.store(true);
        });

    workerThread_->startThread();
}

void BackupScanProgressDialog::cancelarVarredura() {
    cancelRequested_.store(true);
    btnCancel_.setEnabled(false);
    btnCancel_.setButtonText(matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Cancelando...") : "Cancelling...");
    stopTimer();
    if (workerThread_ && workerThread_->isThreadRunning()) {
        workerThread_->stopThread(500);
    }
    closeDialog();
}

void BackupScanProgressDialog::timerCallback() {
    if (cancelRequested_.load()) {
        stopTimer();
        closeDialog();
        return;
    }

    {
        juce::ScopedLock sl(resultsLock_);
        lblStage_.setText(currentStageText_, juce::dontSendNotification);
    }

    if (isFinished_.load()) {
        stopTimer();
        matriz::consolidacao::ResultadoScanBackup finalRes;
        {
            juce::ScopedLock sl(resultsLock_);
            finalRes = std::move(resultado_);
        }
        auto callback = onCompleto_;
        closeDialog();
        if (callback && !cancelRequested_.load() && !finalRes.cancelado) {
            callback(std::move(finalRes));
        }
    }
}

void BackupScanProgressDialog::closeDialog() {
    stopTimer();
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
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

void BackupScanProgressDialog::showModal(
    matriz::db::Database& db,
    const juce::File& pastaProjeto,
    const juce::File& destinoAtivo,
    const matriz::consolidacao::PlanoConsolidacao& plano,
    std::function<void(matriz::consolidacao::ResultadoScanBackup)> onCompleto)
{
    auto comp = std::make_unique<BackupScanProgressDialog>(
        db, pastaProjeto, destinoAtivo, plano, std::move(onCompleto));

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = matriz::i18n::localeAtivo().startsWith("pt")
        ? juce::String::fromUTF8("Verificação de Backup") : "Backup Verification";
    opt.content.set(comp.release(), true);
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

} // namespace matriz::ui
