#include "ProjectLoadingModalDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Catalogo/CatalogoProxies.h"
#include <future>
#include <thread>

namespace matriz::ui {

namespace {

// Item novo (hoje): um clique que reabre a mesma pasta (ou abre outra
// enquanto a primeira ainda está tentando) não cancelava a tentativa
// anterior — só o diálogo dela desistia de esperar. A leitura de
// Project::abrir() ficava rodando sozinha em segundo plano, competindo por
// CPU/IO com a tentativa nova. Resultado observado: abrir "Recovery"
// estourou o prazo, o operador tentou "Antigas e Variadas" em seguida — que
// também estourou, não porque tivesse algo de errado nele, mas porque
// "Recovery" ainda estava rodando por baixo, brigando pelos mesmos
// recursos. Este contador só deixa UMA tentativa de verdade em voo por vez;
// um segundo clique enquanto isso é recusado com uma mensagem, em vez de
// empilhar mais uma tentativa concorrente.
std::atomic<bool>& carregamentoEmVoo() {
    static std::atomic<bool> flag{false};
    return flag;
}

} // namespace

ProjectLoadingModalDialog::ProjectLoadingModalDialog(const juce::File& pasta)
    : pasta_(pasta), progressBar_(currentProgress_) {
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

    setSize(450, 140);
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

    if (text.isNotEmpty() && lblStatus_.getText() != text) {
        lblStatus_.setText(text, juce::dontSendNotification);
    }

    if (std::abs(target - currentProgress_) > 0.005) {
        currentProgress_ += (target - currentProgress_) * 0.30;
    } else {
        currentProgress_ = target;
    }
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

void ProjectLoadingModalDialog::launch(const juce::File& pasta,
                                      juce::Component* parentComp,
                                      OnCompleteCallback onComplete) {
    bool esperado = false;
    if (!carregamentoEmVoo().compare_exchange_strong(esperado, true)) {
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        if (onComplete) {
            onComplete(nullptr, "", 0, false,
                (isPt ? juce::String::fromUTF8("Já existe uma abertura de projeto em andamento — aguarde ela terminar (ou dar erro) antes de tentar outra.")
                      : juce::String("A project is already being opened — wait for it to finish (or fail) before trying another."))
                    .toStdString());
        }
        return;
    }

    auto* dialog = new ProjectLoadingModalDialog(pasta);
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
    juce::Thread::launch([safeDialog, pasta, onComplete = std::move(onComplete)]() mutable {
        std::unique_ptr<matriz::model::Project> projeto;
        std::string erroMsg;
        std::string rotuloMaisRecente;
        int64_t revisaoMaisRecente = 0;
        bool isCatalog = false;

        // Step 1: Open database & records
        //
        // Item novo (hoje): mesma classe de bug já corrigida no ingest —
        // Project::abrir() é síncrono e bloqueante (SQLite abre e recupera
        // WAL/journal, lê e escreve várias linhas), sem prazo nenhum. Se o
        // projeto ficou com o banco num estado sujo por causa de um
        // travamento anterior (ou o storage em si continuar lento/
        // indisponível — a mesma causa do travamento original), abrir()
        // podia travar pra sempre e este diálogo ficava preso em 25% sem
        // nenhum jeito de fechar, sem cancelar, sem nada — exatamente o
        // relatado. Roda numa thread solta com prazo: se estourar, some com
        // um erro em vez de travar o diálogo pra sempre.
        if (safeDialog) safeDialog->setStatus(matriz::i18n::t("loading.step_db"), 0.25);
        {
            auto promessaAbrir = std::make_shared<std::promise<std::unique_ptr<matriz::model::Project>>>();
            auto futuroAbrir = promessaAbrir->get_future();
            std::thread([promessaAbrir, pasta]() {
                try {
                    promessaAbrir->set_value(matriz::model::Project::abrir(pasta));
                } catch (...) {
                    try { promessaAbrir->set_exception(std::current_exception()); } catch (...) {}
                }
                // Só libera aqui — quando Project::abrir() REALMENTE
                // termina — não lá embaixo quando o wait_for desiste. É o
                // que impede um segundo clique de empilhar outra tentativa
                // por cima desta enquanto ela ainda está rodando de verdade.
                carregamentoEmVoo().store(false);
            }).detach();

            constexpr int kPrazoAbrirSegundos = 45;
            if (futuroAbrir.wait_for(std::chrono::seconds(kPrazoAbrirSegundos)) == std::future_status::ready) {
                try {
                    projeto = futuroAbrir.get();
                } catch (const std::exception& e) {
                    erroMsg = e.what();
                }
            } else {
                erroMsg = "Timed out opening project after " + std::to_string(kPrazoAbrirSegundos) +
                           "s (storage may be slow, disconnected, or unresponsive): " +
                           pasta.getFullPathName().toStdString();
            }
        }

        if (projeto != nullptr) {
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
        } else {
            if (matriz::catalogo::ehPastaDeCatalogo(pasta)) {
                isCatalog = true;
            }
        }

        // Step 4: Finalize workspace setup on Message Thread
        if (safeDialog) safeDialog->setStatus(matriz::i18n::t("loading.step_workspace"), 0.95);

        juce::Thread::sleep(120);

        juce::MessageManager::callAsync([safeDialog,
                                         proj = std::move(projeto),
                                         rotulo = std::move(rotuloMaisRecente),
                                         rev = revisaoMaisRecente,
                                         cat = isCatalog,
                                         err = std::move(erroMsg),
                                         cb = std::move(onComplete)]() mutable {
            if (safeDialog) {
                safeDialog->setStatus(matriz::i18n::t("loading.step_ready"), 1.0);
                safeDialog->closeDialog();
            }
            if (cb) {
                cb(std::move(proj), std::move(rotulo), rev, cat, std::move(err));
            }
        });
    });
}

} // namespace matriz::ui
