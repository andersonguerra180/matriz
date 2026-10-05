#include "ProjectLoadingModalDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Catalogo/CatalogoProxies.h"
#include "../Diag/Watchdog.h"
#include <chrono>

namespace matriz::ui {

namespace {

// Só UMA abertura de verdade em voo por vez (um segundo clique competiria por CPU/IO com a
// primeira). A trava é liberada em TODOS os caminhos de saída de Project::abrir() (sucesso,
// erro, exceção, cancelamento) — ver o guard `Liberar` em launch().
std::atomic<bool>& carregamentoEmVoo() {
    static std::atomic<bool> flag{false};
    return flag;
}

// Abertura atual/mais recente (acessada só pela message thread).
std::shared_ptr<EstadoAberturaProjeto>& estadoAtual() {
    static std::shared_ptr<EstadoAberturaProjeto> e;
    return e;
}

juce::String formatarDuracao(juce::int64 segundos) {
    const auto m = segundos / 60;
    const auto s = segundos % 60;
    return juce::String(m) + ":" + juce::String(s).paddedLeft('0', 2);
}

} // namespace

struct EstadoAberturaProjeto {
    juce::File pasta;
    juce::String nome;
    std::chrono::steady_clock::time_point inicio = std::chrono::steady_clock::now();
    std::shared_ptr<matriz::model::CancelamentoAbertura> cancel = std::make_shared<matriz::model::CancelamentoAbertura>();
    std::atomic<bool> terminou{false};    // Project::abrir() retornou (trava liberada)
    std::atomic<bool> abandonada{false};  // diálogo desistiu de esperar o cancelamento: descartar o resultado
    juce::int64 segundosDecorridos() const {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - inicio).count();
    }
};

ProjectLoadingModalDialog::ProjectLoadingModalDialog(const juce::File& pasta,
                                                     std::shared_ptr<EstadoAberturaProjeto> estado,
                                                     bool aguardandoApenas)
    : pasta_(pasta), progressBar_(currentProgress_), estado_(std::move(estado)), aguardandoApenas_(aguardandoApenas) {
    const auto& tk = tema();

    bool isCatalog = matriz::catalogo::ehPastaDeCatalogo(pasta);
    bool isCollection = !isCatalog && (pasta.getParentDirectory().getFileName() == "collections" ||
                                       pasta.getParentDirectory().getFileName() == "Collections");

    juce::String titulo = isCatalog ? i18n::t("loading.title_catalog")
                                    : (isCollection ? i18n::t("loading.title_collection")
                                                    : i18n::t("loading.title_project"));

    lblHeader_.setText(titulo, juce::dontSendNotification);
    lblHeader_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    lblHeader_.setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(lblHeader_);

    juce::String sub = pasta.getFileName();
    if (sub.isEmpty()) sub = pasta.getFullPathName();
    lblSubHeader_.setText(sub, juce::dontSendNotification);
    lblSubHeader_.setFont(juce::Font(juce::FontOptions(12.5f)));
    lblSubHeader_.setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(lblSubHeader_);

    progressBar_.setColour(juce::ProgressBar::foregroundColourId, tk.acento);
    progressBar_.setColour(juce::ProgressBar::backgroundColourId, tk.painelAlt);
    addAndMakeVisible(progressBar_);

    lblStatus_.setText(i18n::t("loading.step_db"), juce::dontSendNotification);
    lblStatus_.setFont(juce::Font(juce::FontOptions(11.5f)));
    lblStatus_.setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(lblStatus_);

    const bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    btnContinuar_.setButtonText(isPt ? "CONTINUAR ESPERANDO" : "KEEP WAITING");
    btnCancelar_.setButtonText(isPt ? "CANCELAR" : "CANCEL");
    for (auto* b : {&btnContinuar_, &btnCancelar_}) {
        b->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        b->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        addChildComponent(*b);
    }
    btnContinuar_.onClick = [this]() {
        continuouEsperando_ = true;
        btnContinuar_.setVisible(false);
        btnCancelar_.setVisible(false);
    };
    btnCancelar_.onClick = [this]() { cliqueCancelar(); };

    setSize(450, 152);
    startTimer(25);
}

ProjectLoadingModalDialog::~ProjectLoadingModalDialog() {
    stopTimer();
}

void ProjectLoadingModalDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto bounds = getLocalBounds().toFloat();

    g.setColour(tk.painel);
    g.fillRoundedRectangle(bounds, 8.0f);

    g.setColour(tk.borda);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.2f);
}

void ProjectLoadingModalDialog::resized() {
    auto area = getLocalBounds().reduced(20, 16);

    lblHeader_.setBounds(area.removeFromTop(22));
    lblSubHeader_.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);

    progressBar_.setBounds(area.removeFromTop(12));
    area.removeFromTop(8);

    lblStatus_.setBounds(area.removeFromTop(20));
    area.removeFromTop(8);
    auto botoes = area.removeFromTop(24);
    btnCancelar_.setBounds(botoes.removeFromRight(110));
    botoes.removeFromRight(8);
    btnContinuar_.setBounds(botoes.removeFromRight(170));
}

void ProjectLoadingModalDialog::setStatus(const juce::String& text, double progress) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    currentStatusText_ = text;
    targetProgress_ = juce::jlimit(0.0, 1.0, progress);
}

void ProjectLoadingModalDialog::timerCallback() {
    std::unique_lock<std::mutex> lock(stateMutex_);
    auto text = currentStatusText_;
    double target = targetProgress_;
    lock.unlock();

    if (!avisoMostrado_ && !cancelando_ && !aguardandoApenas_ && text.isNotEmpty() && lblStatus_.getText() != text) {
        lblStatus_.setText(text, juce::dontSendNotification);
    }

    if (estado_ && !cancelando_) {
        if (aguardandoApenas_) {
            if (estado_->terminou.load()) { closeDialog(); return; }
            lblStatus_.setText(textoAindaAbrindo("Ainda finalizando", "Still finishing"), juce::dontSendNotification);
        } else if (!estado_->terminou.load() && estado_->segundosDecorridos() >= 45) {
            if (!avisoMostrado_) {
                avisoMostrado_ = true;
                btnContinuar_.setVisible(true);
                btnCancelar_.setVisible(true);
            }
            lblStatus_.setText(textoAindaAbrindo("Ainda abrindo", "Still opening"), juce::dontSendNotification);
        }
    } else if (estado_ && cancelando_) {
        const bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        lblStatus_.setText(isPt ? "Cancelando..." : "Cancelling...", juce::dontSendNotification);
        // Se o cancelamento demorar (disco preso), solta o diálogo; o resultado será descartado.
        if (!estado_->terminou.load() && juce::Time::currentTimeMillis() - cancelouEmMs_ > 10000) {
            estado_->abandonada.store(true);
            closeDialog();
            return;
        }
    }

    if (std::abs(target - currentProgress_) > 0.005) {
        currentProgress_ += (target - currentProgress_) * 0.30;
    } else {
        currentProgress_ = target;
    }
}

juce::String ProjectLoadingModalDialog::textoAindaAbrindo(const char* verboPt, const char* verboEn) const {
    const bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    const auto tempo = formatarDuracao(estado_ ? estado_->segundosDecorridos() : 0);
    return (isPt ? juce::String(verboPt) : juce::String(verboEn)) + " " + (estado_ ? estado_->nome : juce::String()) +
           juce::String::fromUTF8(" \xe2\x80\x94 ") + tempo;
}

void ProjectLoadingModalDialog::cliqueCancelar() {
    if (!estado_ || cancelando_) return;
    cancelando_ = true;
    cancelouEmMs_ = juce::Time::currentTimeMillis();
    btnContinuar_.setVisible(false);
    btnCancelar_.setVisible(false);
    estado_->cancel->cancelar();
}

void ProjectLoadingModalDialog::closeDialog() {
    if (isClosed_) return;
    isClosed_ = true;
    stopTimer();

    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
        dw->setVisible(false);
    }
}

void ProjectLoadingModalDialog::mostrarEsperaDaAnterior(juce::Component* parentComp) {
    auto estado = estadoAtual();
    if (!estado || estado->terminou.load()) return;

    auto* dialog = new ProjectLoadingModalDialog(estado->pasta, estado, true);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "";
    options.dialogBackgroundColour = juce::Colours::transparentBlack;
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = false;
    if (parentComp != nullptr && parentComp->isShowing()) {
        auto c = parentComp->getScreenBounds().getCentre();
        dialog->setCentrePosition(c.x, c.y);
    }
    options.launchAsync();
}

void ProjectLoadingModalDialog::launch(const juce::File& pasta,
                                      juce::Component* parentComp,
                                      OnCompleteCallback onComplete) {
    bool esperado = false;
    if (!carregamentoEmVoo().compare_exchange_strong(esperado, true)) {
        const bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        auto anterior = estadoAtual();
        const juce::String nome = anterior ? anterior->nome : juce::String("?");
        const auto tempo = formatarDuracao(anterior ? anterior->segundosDecorridos() : 0);
        const juce::String msg = isPt
            ? (juce::String::fromUTF8("A cole\xc3\xa7\xc3\xa3o \"") + nome + juce::String::fromUTF8("\" ainda est\xc3\xa1 abrindo (h\xc3\xa1 ") + tempo +
               juce::String::fromUTF8("). Aguarde ela terminar antes de abrir outra."))
            : ("\"" + nome + "\" is still opening (" + tempo + " so far). Wait for it to finish before opening another.");
        juce::Component::SafePointer<juce::Component> safeParent(parentComp);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(isPt ? "Abertura em andamento" : "Opening in progress")
                .withMessage(msg)
                .withButton(isPt ? "AGUARDAR" : "WAIT")
                .withButton("OK"),
            [safeParent](int resultado) {
                if (resultado == 1) mostrarEsperaDaAnterior(safeParent.getComponent());
            });
        return;
    }

    auto estado = std::make_shared<EstadoAberturaProjeto>();
    estado->pasta = pasta;
    estado->nome = pasta.getFileName().isNotEmpty() ? pasta.getFileName() : pasta.getFullPathName();
    estadoAtual() = estado;

    auto* dialog = new ProjectLoadingModalDialog(pasta, estado);
    juce::Component::SafePointer<ProjectLoadingModalDialog> safeDialog(dialog);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "";
    options.dialogBackgroundColour = juce::Colours::transparentBlack;
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = false;

    if (parentComp != nullptr && parentComp->isShowing()) {
        auto screenCenter = parentComp->getScreenBounds().getCentre();
        dialog->setCentrePosition(screenCenter.x, screenCenter.y);
    } else if (auto* tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        auto area = tela->userBounds;
        dialog->setCentrePosition(area.getCentreX(), area.getCentreY());
    }

    options.launchAsync();

    // Spawn background worker thread
    juce::Thread::launch([safeDialog, estado, pasta, onComplete = std::move(onComplete)]() mutable {
        std::unique_ptr<matriz::model::Project> projeto;
        std::string erroMsg;
        std::string rotuloMaisRecente;
        int64_t revisaoMaisRecente = 0;
        bool isCatalog = false;
        bool cancelado = false;
        const auto inicioDialogo = std::chrono::steady_clock::now();

        // Step 1: Open database & records. Sem prazo que gere erro: aos 45 s o diálogo oferece
        // CONTINUAR ESPERANDO / CANCELAR; cancelar interrompe Project::abrir() de verdade.
        if (safeDialog) safeDialog->setStatus(matriz::i18n::t("loading.step_db"), 0.25);
        {
            // A trava é liberada em TODOS os caminhos (sucesso, erro, exceção, cancelamento).
            struct Liberar {
                std::shared_ptr<EstadoAberturaProjeto> e;
                ~Liberar() { e->terminou.store(true); carregamentoEmVoo().store(false); }
            } liberar{estado};
            try {
                projeto = matriz::model::Project::abrir(pasta, estado->cancel.get());
            } catch (const matriz::model::AberturaCancelada&) {
                cancelado = true;
            } catch (const std::exception& e) {
                erroMsg = e.what();
            } catch (...) {
                erroMsg = "unknown error opening project";
            }
        }

        if (projeto != nullptr && !estado->cancel->cancelado()) {
            // Step 2: Destination checks
            if (safeDialog) safeDialog->setStatus(matriz::i18n::t("loading.step_destinations"), 0.55);
            try {
                auto stmt = projeto->registro().prepare("SELECT destino_path, rotulo, destination_id FROM backup_destino WHERE ativo = 1");
                while (stmt.step()) {
                    std::string dId = stmt.columnText(2);
                    if (dId == projeto->destinationId()) continue;
                    juce::File dPasta(stmt.columnText(0));
                    if (dPasta.isDirectory()) {
                        auto dOpt = matriz::model::DestinationInfo::lerDeArquivo(dPasta.getChildFile("destination.json"));
                        if (dOpt && dOpt->projetoId == projeto->projetoId() && dOpt->revisao > projeto->revisao()) {
                            if (dOpt->revisao > revisaoMaisRecente) {
                                revisaoMaisRecente = dOpt->revisao;
                                rotuloMaisRecente = dOpt->rotulo.empty() ? dPasta.getFileName().toStdString() : dOpt->rotulo;
                            }
                        }
                    }
                }
            } catch (...) {}

            // Step 3: Warm up index / media catalog
            if (safeDialog) safeDialog->setStatus(matriz::i18n::t("loading.step_indices"), 0.85);
            try {
                projeto->registro().prepare("SELECT COUNT(*) FROM item").step();
            } catch (...) {}
        } else if (projeto == nullptr && !cancelado) {
            if (matriz::catalogo::ehPastaDeCatalogo(pasta)) {
                isCatalog = true;
            }
        }

        // Cancelado (ou diálogo abandonado): fecha o projeto resultante (destrutor fecha os bancos) e descarta.
        if (estado->cancel->cancelado() || estado->abandonada.load()) {
            cancelado = true;
            projeto.reset();
        }

        matriz::diag::logarWatchdog("[Abrir] dialogo " + estado->nome + " total=" +
            juce::String(static_cast<juce::int64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - inicioDialogo).count())) + " ms" +
            (cancelado ? " (cancelado)" : ""));

        // Step 4: Finalize workspace setup on Message Thread
        if (safeDialog) safeDialog->setStatus(matriz::i18n::t("loading.step_workspace"), 0.95);

        juce::Thread::sleep(120);

        juce::MessageManager::callAsync([safeDialog,
                                         proj = std::move(projeto),
                                         rotulo = std::move(rotuloMaisRecente),
                                         rev = revisaoMaisRecente,
                                         cat = isCatalog,
                                         err = std::move(erroMsg),
                                         cancelado,
                                         cb = std::move(onComplete)]() mutable {
            if (safeDialog) {
                safeDialog->setStatus(matriz::i18n::t("loading.step_ready"), 1.0);
                safeDialog->closeDialog();
            }
            if (cancelado) return;  // abertura cancelada/abandonada: não abre sozinho
            if (proj) proj->iniciarAutocorrecoesEmBackground();
            if (cb) {
                cb(std::move(proj), std::move(rotulo), rev, cat, std::move(err));
            }
        });
    });
}

} // namespace matriz::ui
