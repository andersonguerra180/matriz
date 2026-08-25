#include "MainWindow.h"

#include "../Catalogo/CatalogoProxies.h"

#include "../App/Preferencias.h"
#include "../Diag/NSExceptionGuard.h"
#include "../I18n/Strings.h"
#include "ConfiguracoesProjetoDialogo.h"
#include "ConsolidacaoDialogo.h"
#include "NovoProjetoDialogo.h"
#include "Tokens.h"
#include "ModalMitigacao.h"

namespace matriz::ui {

namespace {
enum MenuIndices { kMenuArquivo = 0, kMenuEditar = 1, kMenuProjeto = 2, kMenuPreferencias = 3, kMenuAjuda = 4 };
enum ComandoMenu {
    kCmdNovoProjeto = 1,
    kCmdAbrirProjeto,
    kCmdAbrirCatalogo,
    kCmdFecharProjeto,
    kCmdSalvarProjeto,
    kCmdSalvarProjetoComo,
    kCmdInfoProjeto,
    kCmdSair,
    kCmdRenomearItem,
    kCmdRemoverDoBackup,
    kCmdConfiguracoes,
    kCmdIngerirArquivos,
    kCmdConsolidar,
    kCmdPreferenciasGerais,
    kCmdAudioDevice,
    kCmdUndo
};
} // namespace

MainWindow::MainWindow(const juce::String& nome)
    : DocumentWindow(nome, tema().fundo, DocumentWindow::allButtons) {
    setUsingNativeTitleBar(true);

    conteudo_ = std::make_unique<MainComponent>();
    conectarConteudo();
    setContentNonOwned(conteudo_.get(), true);

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
#else
    setMenuBar(this);
#endif

    setResizable(true, false);
    if (auto* tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        setBounds(tela->userBounds.toNearestInt());
    else
        centreWithSize(1200, 800);
    setVisible(true);
}

MainWindow::~MainWindow() {
    matriz::diag::breadcrumb("MainWindow::dtor");
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#else
    setMenuBar(nullptr);
#endif
    conteudo_.reset();
}

void MainWindow::closeButtonPressed() { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

juce::StringArray MainWindow::getMenuBarNames() {
    return {matriz::i18n::t("menu.arquivo"), "Edit", matriz::i18n::t("menu.projeto"),
            matriz::i18n::t("menu.preferencias"), matriz::i18n::t("menu.ajuda")};
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    if (topLevelMenuIndex == kMenuArquivo) {
        bool podeTrocarProjeto = !conteudo_->ingestEmAndamento();
        bool temProjeto = conteudo_->temProjetoAberto() || conteudo_->temCatalogoAberto();
        menu.addItem(kCmdNovoProjeto, "New Project (.mtz)...", podeTrocarProjeto);
        menu.addItem(kCmdAbrirProjeto, "Open Project (.mtz)...", podeTrocarProjeto);
        menu.addItem(kCmdAbrirCatalogo, "Open Catalog (.bkm)...", podeTrocarProjeto);
        menu.addSeparator();
        bool temProjetoMtz = conteudo_->temProjetoAberto();
        menu.addItem(kCmdSalvarProjeto, "Save Project (Cmd+S)", temProjetoMtz);
        menu.addItem(kCmdSalvarProjetoComo, "Save Project As... (Cmd+Shift+S)", temProjetoMtz);
        menu.addItem(kCmdFecharProjeto, "Close Project", temProjeto && podeTrocarProjeto);
        menu.addItem(kCmdInfoProjeto, "Project Info...", temProjetoMtz);
        menu.addSeparator();
        menu.addItem(kCmdIngerirArquivos, matriz::i18n::t("menu.arquivo_ingerir_arquivos"), conteudo_->temProjetoAberto());
        menu.addSeparator();
        menu.addItem(kCmdSair, matriz::i18n::t("menu.arquivo_sair"));
    } else if (topLevelMenuIndex == kMenuEditar) {
        bool podeUndo = conteudo_->temProjetoAberto() && conteudo_->podeDesfazer();
        menu.addItem(kCmdUndo, "Undo (Cmd+Z)", podeUndo, false, nullptr);
        menu.addSeparator();
        menu.addItem(kCmdRenomearItem, "Rename Item(s)... (R)", conteudo_->temProjetoAberto());
        menu.addItem(kCmdRemoverDoBackup, "Remove Selected from Backup (C)", conteudo_->temProjetoAberto());
    } else if (topLevelMenuIndex == kMenuProjeto) {
        menu.addItem(kCmdConfiguracoes, matriz::i18n::t("menu.projeto_configuracoes"), conteudo_->temProjetoAberto());
        menu.addItem(kCmdConsolidar, matriz::i18n::t("consolidacao.titulo"), conteudo_->temProjetoAberto());
    } else if (topLevelMenuIndex == kMenuPreferencias) {
        menu.addItem(kCmdPreferenciasGerais, "Preferences / Theme / AI Key...");
        menu.addItem(kCmdAudioDevice, "Audio Device...");
    }
    return menu;
}

void MainWindow::menuItemSelected(int menuItemID, int) {
    switch (menuItemID) {
        case kCmdNovoProjeto: pedirNovoProjetoViaMenu(); break;
        case kCmdAbrirProjeto: pedirAbrirProjeto(); break;
        case kCmdAbrirCatalogo: pedirAbrirCatalogo(); break;
        case kCmdSalvarProjeto: conteudo_->salvarProjeto(); break;
        case kCmdSalvarProjetoComo: pedirSalvarProjetoComo(); break;
        case kCmdFecharProjeto: conteudo_->fecharProjeto(); break;
        case kCmdInfoProjeto: pedirConfiguracoesProjeto(); break;
        case kCmdSair: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case kCmdUndo: conteudo_->executarUndo(); break;
        case kCmdRenomearItem: conteudo_->renomearItemSelecionado(); break;
        case kCmdRemoverDoBackup: conteudo_->removerItemSelecionadoDoBackup(); break;
        case kCmdConfiguracoes: pedirConfiguracoesProjeto(); break;
        case kCmdIngerirArquivos: pedirIngerirArquivos(); break;
        case kCmdConsolidar: pedirConsolidar(); break;
        case kCmdPreferenciasGerais: mostrarPreferenciasDialogo(); break;
        case kCmdAudioDevice: mostrarAudioDeviceDialogo(); break;
        default: break;
    }
}

void MainWindow::conectarConteudo() {
    conteudo_->aoPedirNovoProjeto = [this](matriz::model::Modo modo) { pedirNovoProjeto(modo); };
    conteudo_->aoPedirAbrirProjeto = [this] { pedirAbrirProjeto(); };
    conteudo_->aoAbrirRecente = [this](juce::File pasta) { abrirPasta(pasta); };
    conteudo_->aoPedirIngerirArquivos = [this] { pedirIngerirArquivos(); };
    conteudo_->aoSalvarComo = [this] { pedirSalvarProjetoComo(); };
    conteudo_->aoPedirBackup = [this] { pedirConsolidar(); };
}

void MainWindow::pedirNovoProjetoViaMenu() {
    auto janela = std::make_shared<juce::AlertWindow>(matriz::i18n::t("menu.arquivo_novo_projeto"), juce::String(),
                                                        juce::MessageBoxIconType::QuestionIcon);
    janela->addButton(matriz::i18n::t("tela_inicial.cartao_archive_titulo"), 1);
    janela->addButton(matriz::i18n::t("tela_inicial.cartao_catalog_titulo"), 2);
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainWindow> safeThis(this);
    janela->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, janela](int resultado) {
                                 janela->setVisible(false);
                                 janela->removeFromDesktop();
                                 if (!safeThis) return;

                                 if (resultado == 1) safeThis->pedirNovoProjeto(matriz::model::Modo::Preservacao);
                                 else if (resultado == 2) safeThis->pedirNovoProjeto(matriz::model::Modo::Catalogo);
                             }));
}

void MainWindow::pedirNovoProjeto(matriz::model::Modo modo) {
    mostrarDialogoNovoProjeto(modo, [this](std::optional<NovoProjetoResultado> resultado) {
        if (!resultado) return;
        try {
            auto projeto = matriz::model::Project::criar(resultado->pasta, resultado->params);
            matriz::app::registrarRecente(projeto->pasta().getFullPathName(), projeto->nome(),
                                            matriz::model::modoToString(projeto->modo()));
            conteudo_->abrirProjeto(std::move(projeto));
        } catch (const std::exception& e) {
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                              .withIconType(juce::MessageBoxIconType::WarningIcon)
                                              .withTitle(matriz::i18n::t("dialogo_novo_projeto.erro_titulo"))
                                              .withMessage(juce::String(e.what()))
                                              .withButton(matriz::i18n::t("comum.ok")),
                                          static_cast<juce::ModalComponentManager::Callback*>(nullptr));
        }
    });
}

void MainWindow::abrirPasta(const juce::File& pasta) {
    // Pasta de backup com catálogo dentro abre em modo consulta (item 11),
    // não como projeto — é o caso de "recebi um HD de backup e quero ver o
    // que tem nele", onde não existe projeto nenhum pra abrir.
    if (matriz::catalogo::ehPastaDeCatalogo(pasta)) {
        if (conteudo_->abrirCatalogo(pasta)) return;
    }

    try {
        auto projeto = matriz::model::Project::abrir(pasta);
        matriz::app::registrarRecente(projeto->pasta().getFullPathName(), projeto->nome(),
                                        matriz::model::modoToString(projeto->modo()));
        conteudo_->abrirProjeto(std::move(projeto));
    } catch (const std::exception& e) {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::WarningIcon)
                                          .withTitle(matriz::i18n::t("dialogo_abrir_projeto.erro_titulo"))
                                          .withMessage(juce::String(e.what()))
                                          .withButton(matriz::i18n::t("comum.ok")),
                                      static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

void MainWindow::pedirAbrirProjeto() {
    auto chooser = std::make_shared<juce::FileChooser>(matriz::i18n::t("dialogo_abrir_projeto.titulo"));
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser](const juce::FileChooser& fc) {
                              juce::File pasta = fc.getResult();
                              if (pasta == juce::File()) return;
                              abrirPasta(pasta);
                          });
}

void MainWindow::pedirAbrirCatalogo() {
    auto chooser = std::make_shared<juce::FileChooser>("Open Catalog (.bkm)");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser](const juce::FileChooser& fc) {
                              juce::File file = fc.getResult();
                              if (file == juce::File()) return;
                              conteudo_->abrirCatalogo(file);
                          });
}

void MainWindow::pedirConfiguracoesProjeto() {
    if (!conteudo_->temProjetoAberto()) return;
    mostrarDialogoConfiguracoesProjeto(*conteudo_->projetoAberto(), [] {});
}

void MainWindow::pedirConsolidar() {
    if (!conteudo_->temProjetoAberto()) return;
    mostrarDialogoConsolidacao(*conteudo_->projetoAberto(), [] {});
}

void MainWindow::mostrarAudioDeviceDialogo() {
    auto deviceManager = std::make_shared<juce::AudioDeviceManager>();
    deviceManager->initialiseWithDefaultDevices(0, 2);

    auto janela = std::make_shared<juce::DialogWindow>("Audio Device", tema().painel, true);

    struct PainelAudioDevice : public juce::Component {
        PainelAudioDevice(juce::AudioDeviceManager& dm, std::shared_ptr<juce::DialogWindow> win)
            : janela_(std::move(win)) {
            selector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
                dm, 0, 0, 0, 2, false, false, true, false);
            addAndMakeVisible(*selector_);

            const auto& tk = tema();
            btnApply_ = std::make_unique<juce::TextButton>("APPLY");
            btnApply_->setColour(juce::TextButton::buttonColourId, tk.acento);
            btnApply_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
            btnApply_->onClick = [this] {
                if (aoAplicar) aoAplicar();
            };
            addAndMakeVisible(*btnApply_);

            btnClose_ = std::make_unique<juce::TextButton>("CLOSE");
            btnClose_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
            btnClose_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
            btnClose_->onClick = [this] {
                if (janela_) janela_->exitModalState(0);
            };
            addAndMakeVisible(*btnClose_);

            setSize(500, 450);
        }

        void resized() override {
            auto area = getLocalBounds();
            auto bottomRow = area.removeFromBottom(44).reduced(8, 6);
            btnClose_->setBounds(bottomRow.removeFromRight(100));
            bottomRow.removeFromRight(8);
            btnApply_->setBounds(bottomRow.removeFromRight(100));
            selector_->setBounds(area);
        }

        std::function<void()> aoAplicar;

    private:
        std::unique_ptr<juce::AudioDeviceSelectorComponent> selector_;
        std::unique_ptr<juce::TextButton> btnApply_;
        std::unique_ptr<juce::TextButton> btnClose_;
        std::shared_ptr<juce::DialogWindow> janela_;
    };

    auto painel = std::make_unique<PainelAudioDevice>(*deviceManager, janela);

    struct Estado {
        std::shared_ptr<juce::DialogWindow> janela;
        std::shared_ptr<juce::AudioDeviceManager> deviceManager;
    };
    auto estado = std::make_shared<Estado>();
    estado->janela = janela;
    estado->deviceManager = deviceManager;

    janela->setContentOwned(painel.release(), true);
    janela->setResizable(true, false);
    janela->centreWithSize(500, 450);
    janela->setVisible(true);
    janela->enterModalState(true, juce::ModalCallbackFunction::create([estado](int) {
        estado->janela->setVisible(false);
    }));
}

void MainWindow::mostrarPreferenciasDialogo() {
    auto janela = std::make_shared<juce::DialogWindow>("Preferences & Theme", tema().painel, true);

    struct PainelPreferencias : public juce::Component {
        PainelPreferencias(std::shared_ptr<juce::DialogWindow> win) : janela_(std::move(win)) {
            const auto& tk = tema();

            lblTitulo_ = std::make_unique<juce::Label>("", "Application Preferences");
            lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
            lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*lblTitulo_);

            lblTema_ = std::make_unique<juce::Label>("", "Appearance / UI Theme:");
            lblTema_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
            lblTema_->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lblTema_);

            comboTema_ = std::make_unique<juce::ComboBox>();
            comboTema_->addItem("Dark (BKR Dark)", 1);
            comboTema_->addItem("Light (BKR Light)", 2);
            juce::String temaAtual = matriz::app::lerTema();
            comboTema_->setSelectedId(temaAtual == "light" ? 2 : 1, juce::dontSendNotification);
            addAndMakeVisible(*comboTema_);

            lblGemini_ = std::make_unique<juce::Label>("", "Google Gemini API Key (AI Scan):");
            lblGemini_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
            lblGemini_->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lblGemini_);

            txtGeminiKey_ = std::make_unique<juce::TextEditor>();
            txtGeminiKey_->setText(matriz::app::lerGeminiApiKey(), false);
            txtGeminiKey_->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
            txtGeminiKey_->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
            txtGeminiKey_->setColour(juce::TextEditor::outlineColourId, tk.borda);
            txtGeminiKey_->setTextToShowWhenEmpty("Enter Gemini API key...", tk.textoTerciario);
            addAndMakeVisible(*txtGeminiKey_);

            lblGeminiInfo_ = std::make_unique<juce::Label>("", "Required for AI SCAN contextual analysis. Stored locally in preferences.");
            lblGeminiInfo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            lblGeminiInfo_->setColour(juce::Label::textColourId, tk.textoTerciario);
            addAndMakeVisible(*lblGeminiInfo_);

            lblRecent_ = std::make_unique<juce::Label>("", "RECENTLY INGESTED:");
            lblRecent_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
            lblRecent_->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lblRecent_);

            comboRecentMode_ = std::make_unique<juce::ComboBox>();
            comboRecentMode_->addItem("TIME (auto-expire after hours)", 1);
            comboRecentMode_->addItem("CLEAR LIST on app start", 2);
            auto curMode = matriz::app::lerRecentlyIngestedMode();
            comboRecentMode_->setSelectedId(curMode == matriz::app::RecentlyIngestedMode::ClearOnStart ? 2 : 1, juce::dontSendNotification);
            comboRecentMode_->onChange = [this] {
                bool isTime = comboRecentMode_->getSelectedId() == 1;
                spinRecentHoras_->setEnabled(isTime);
                lblRecentHoras_->setAlpha(isTime ? 1.0f : 0.4f);
            };
            addAndMakeVisible(*comboRecentMode_);

            lblRecentHoras_ = std::make_unique<juce::Label>("", "Hours:");
            lblRecentHoras_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            lblRecentHoras_->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lblRecentHoras_);

            spinRecentHoras_ = std::make_unique<juce::Slider>(juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft);
            spinRecentHoras_->setRange(1, 72, 1);
            spinRecentHoras_->setValue(matriz::app::lerRecentlyIngestedHoras(), juce::dontSendNotification);
            spinRecentHoras_->setColour(juce::Slider::textBoxTextColourId, tk.textoPrimario);
            spinRecentHoras_->setColour(juce::Slider::textBoxBackgroundColourId, tk.painelAlt);
            spinRecentHoras_->setColour(juce::Slider::textBoxOutlineColourId, tk.borda);
            bool isTimeCur = curMode != matriz::app::RecentlyIngestedMode::ClearOnStart;
            spinRecentHoras_->setEnabled(isTimeCur);
            if (!isTimeCur) lblRecentHoras_->setAlpha(0.4f);
            addAndMakeVisible(*spinRecentHoras_);

            btnSave_ = std::make_unique<juce::TextButton>("SAVE & APPLY");
            btnSave_->setColour(juce::TextButton::buttonColourId, tk.acento);
            btnSave_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
            btnSave_->onClick = [this] {
                matriz::app::gravarGeminiApiKey(txtGeminiKey_->getText().trim());
                matriz::app::gravarTema(comboTema_->getSelectedId() == 2 ? "light" : "dark");
                auto modo = comboRecentMode_->getSelectedId() == 2
                    ? matriz::app::RecentlyIngestedMode::ClearOnStart
                    : matriz::app::RecentlyIngestedMode::Time;
                matriz::app::gravarRecentlyIngestedMode(modo);
                matriz::app::gravarRecentlyIngestedHoras(static_cast<int>(spinRecentHoras_->getValue()));
                matriz::ui::recarregarTema();
                if (janela_) janela_->exitModalState(0);
            };
            addAndMakeVisible(*btnSave_);

            btnClose_ = std::make_unique<juce::TextButton>("CLOSE");
            btnClose_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
            btnClose_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
            btnClose_->onClick = [this] {
                if (janela_) janela_->exitModalState(0);
            };
            addAndMakeVisible(*btnClose_);

            setSize(480, 320);
        }

        void resized() override {
            auto area = getLocalBounds().reduced(16);
            lblTitulo_->setBounds(area.removeFromTop(30));
            area.removeFromTop(12);

            lblTema_->setBounds(area.removeFromTop(22));
            comboTema_->setBounds(area.removeFromTop(28));
            area.removeFromTop(12);

            lblGemini_->setBounds(area.removeFromTop(22));
            txtGeminiKey_->setBounds(area.removeFromTop(28));
            lblGeminiInfo_->setBounds(area.removeFromTop(20));
            area.removeFromTop(12);

            lblRecent_->setBounds(area.removeFromTop(22));
            comboRecentMode_->setBounds(area.removeFromTop(28));
            area.removeFromTop(4);
            auto horasRow = area.removeFromTop(28);
            lblRecentHoras_->setBounds(horasRow.removeFromLeft(50));
            spinRecentHoras_->setBounds(horasRow.removeFromLeft(120));
            area.removeFromTop(12);

            auto bottomRow = area.removeFromBottom(36);
            btnClose_->setBounds(bottomRow.removeFromRight(100));
            bottomRow.removeFromRight(8);
            btnSave_->setBounds(bottomRow.removeFromRight(130));
        }

    private:
        std::shared_ptr<juce::DialogWindow> janela_;
        std::unique_ptr<juce::Label> lblTitulo_;
        std::unique_ptr<juce::Label> lblTema_;
        std::unique_ptr<juce::ComboBox> comboTema_;
        std::unique_ptr<juce::Label> lblGemini_;
        std::unique_ptr<juce::TextEditor> txtGeminiKey_;
        std::unique_ptr<juce::Label> lblGeminiInfo_;
        std::unique_ptr<juce::Label> lblRecent_;
        std::unique_ptr<juce::ComboBox> comboRecentMode_;
        std::unique_ptr<juce::Label> lblRecentHoras_;
        std::unique_ptr<juce::Slider> spinRecentHoras_;
        std::unique_ptr<juce::TextButton> btnSave_;
        std::unique_ptr<juce::TextButton> btnClose_;
    };

    auto painel = std::make_unique<PainelPreferencias>(janela);
    janela->setContentOwned(painel.release(), true);
    janela->setResizable(false, false);
    janela->centreWithSize(480, 430);
    janela->setVisible(true);
    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela](int) {
        janela->setVisible(false);
    }));
}

void MainWindow::pedirSalvarProjetoComo() {
    if (!conteudo_->temProjetoAberto()) return;
    conteudo_->salvarProjeto();
    juce::File pastaOriginal = conteudo_->pastaProjeto();
    auto chooser = std::make_shared<juce::FileChooser>("Save Project As...", pastaOriginal.getParentDirectory());
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser, pastaOriginal](const juce::FileChooser& fc) {
                              auto resultado = fc.getResult();
                              if (resultado == juce::File()) return;
                              juce::File destino = resultado.getFileNameWithoutExtension().isEmpty()
                                  ? resultado : resultado.withFileExtension("mtz");
                              if (destino.exists()) destino.deleteRecursively();
                              if (pastaOriginal.copyDirectoryTo(destino)) {
                                  abrirPasta(destino);
                                  setName(getName() + "  [Saved As: " + destino.getFileName() + "]");
                                  juce::Component::SafePointer<MainWindow> safeWin(this);
                                  auto origName = getName().upToFirstOccurrenceOf("  [Saved", false, false);
                                  juce::Timer::callAfterDelay(2000, [safeWin, origName] {
                                      if (safeWin) safeWin->setName(origName);
                                  });
                              }
                          });
}

void MainWindow::pedirIngerirArquivos() {
    if (!conteudo_->temProjetoAberto()) return;
    auto chooser = std::make_shared<juce::FileChooser>(matriz::i18n::t("menu.arquivo_ingerir_arquivos"));
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectDirectories |
                              juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, chooser](const juce::FileChooser& fc) {
                              auto resultados = fc.getResults();
                              if (resultados.isEmpty()) return;
                              conteudo_->ingerirArquivos(resultados);
                          });
}

} // namespace matriz::ui
