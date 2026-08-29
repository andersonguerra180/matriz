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
    kCmdNovoCatalogo,
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
    kCmdUndo,
    kCmdRecenteBase = 2000
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
    setVisible(false);
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
    return {"File", "Edit", "Project", "Preferences", "Help"};
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    if (topLevelMenuIndex == kMenuArquivo) {
        bool podeTrocarProjeto = !conteudo_->ingestEmAndamento();
        bool temProjeto = conteudo_->temProjetoAberto() || conteudo_->temCatalogoAberto();
        bool isCatalog = (conteudo_->projetoAberto() && conteudo_->projetoAberto()->projeto().modo() == matriz::model::Modo::Catalogo);

        // New Submenu
        juce::PopupMenu newMenu;
        newMenu.addItem(kCmdNovoProjeto, "Collection (.mtz)...", podeTrocarProjeto);
        newMenu.addItem(kCmdNovoCatalogo, "Catalog (.bkm)...", podeTrocarProjeto);
        menu.addSubMenu("New", newMenu, podeTrocarProjeto);

        // Open Submenu
        juce::PopupMenu openMenu;
        openMenu.addItem(kCmdAbrirProjeto, "Collection (.mtz)...", podeTrocarProjeto);
        openMenu.addItem(kCmdAbrirCatalogo, "Catalog (.bkm)...", podeTrocarProjeto);
        menu.addSubMenu("Open", openMenu, podeTrocarProjeto);

        // Recent Files Submenu
        juce::PopupMenu recentMenu;
        auto recentes = matriz::app::lerRecentes();
        if (!recentes.empty()) {
            int rIdx = 0;
            for (const auto& r : recentes) {
                juce::String label = r.nome.isEmpty() ? r.pasta : r.nome;
                if (!r.pasta.isEmpty() && r.pasta != label)
                    label += " (" + r.pasta + ")";
                recentMenu.addItem(kCmdRecenteBase + rIdx, label);
                rIdx++;
            }
        } else {
            recentMenu.addItem(1, "No Recent Files", false);
        }
        menu.addSubMenu("Open Recent Files", recentMenu, podeTrocarProjeto);

        menu.addSeparator();
        juce::String saveText = "Save (Cmd+S)";
        juce::String saveAsText = "Save As... (Cmd+Shift+S)";
        juce::String closeText = "Close";
        juce::String infoText = "Project Info...";

        if (temProjeto) {
            if (isCatalog) {
                saveText = "Save Catalog (Cmd+S)";
                saveAsText = "Save Catalog As... (Cmd+Shift+S)";
                closeText = "Close Catalog";
                infoText = "Catalog Info...";
            } else {
                saveText = "Save Collection (Cmd+S)";
                saveAsText = "Save Collection As... (Cmd+Shift+S)";
                closeText = "Close Collection";
                infoText = "Collection Info...";
            }
        }

        menu.addItem(kCmdSalvarProjeto, saveText, temProjeto);
        menu.addItem(kCmdSalvarProjetoComo, saveAsText, temProjeto);
        menu.addItem(kCmdFecharProjeto, closeText, temProjeto && podeTrocarProjeto);
        menu.addItem(kCmdInfoProjeto, infoText, temProjeto);
        menu.addSeparator();
        menu.addItem(kCmdIngerirArquivos, "Add Files...", conteudo_->temProjetoAberto() && !isCatalog);
        menu.addSeparator();
        menu.addItem(kCmdSair, "Quit");
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
        case kCmdNovoProjeto: pedirNovoProjeto(matriz::model::Modo::Preservacao); break;
        case kCmdNovoCatalogo: pedirNovoCatalogo(); break;
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
        default: {
            if (menuItemID >= kCmdRecenteBase && menuItemID < kCmdRecenteBase + 100) {
                auto recentes = matriz::app::lerRecentes();
                int idx = menuItemID - kCmdRecenteBase;
                if (idx >= 0 && idx < static_cast<int>(recentes.size())) {
                    juce::File pastaRecente(recentes[idx].pasta);
                    if (pastaRecente.isDirectory() || pastaRecente.existsAsFile()) {
                        abrirPasta(pastaRecente);
                    } else {
                        juce::AlertWindow::showAsync(
                            juce::MessageBoxOptions()
                                .withIconType(juce::MessageBoxIconType::InfoIcon)
                                .withTitle("Project Not Found")
                                .withMessage("Recent project location not found:\n" + recentes[idx].pasta)
                                .withButton("OK"),
                            nullptr);
                    }
                }
            }
            break;
        }
    }
}

void MainWindow::conectarConteudo() {
    conteudo_->aoPedirNovoProjeto = [this](matriz::model::Modo modo) { pedirNovoProjeto(modo); };
    conteudo_->aoPedirAbrirProjeto = [this] { pedirAbrirProjeto(); };
    conteudo_->aoAbrirRecente = [this](juce::File pasta) { abrirPasta(pasta); };
    conteudo_->aoPedirIngerirArquivos = [this] { pedirIngerirArquivos(); };
    conteudo_->aoSalvarComo = [this] { pedirSalvarProjetoComo(); };
    conteudo_->aoPedirBackup = [this] { pedirConsolidar(); };
    conteudo_->aoMudarEstadoProjeto = [this] {
        menuItemsChanged();
        if (conteudo_->temProjetoAberto()) {
            auto pNome = juce::String::fromUTF8(conteudo_->projetoAberto()->projeto().nome().c_str());
            setName(pNome.isEmpty() ? "BKR Matriz" : (pNome + " — BKR Matriz"));
        } else {
            setName("BKR Matriz");
        }
    };
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

void MainWindow::pedirNovoCatalogo() {
    pedirNovoProjeto(matriz::model::Modo::Catalogo);
}

void MainWindow::pedirAbrirProjeto() {
    auto chooser = std::make_shared<juce::FileChooser>("Open Collection (.mtz)");
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
                              abrirPasta(file);
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
        PainelPreferencias(std::shared_ptr<juce::DialogWindow> win, MainWindow* mainWin)
            : janela_(std::move(win)), mainWin_(mainWin) {
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

            toggleTooltips_ = std::make_unique<juce::ToggleButton>("Show tooltips (hints on hover)");
            toggleTooltips_->setToggleState(matriz::app::lerTooltipsHabilitados(), juce::dontSendNotification);
            toggleTooltips_->setColour(juce::ToggleButton::textColourId, tk.textoSecundario);
            addAndMakeVisible(*toggleTooltips_);

            btnSave_ = std::make_unique<juce::TextButton>("SAVE & APPLY");
            btnSave_->setColour(juce::TextButton::buttonColourId, tk.acento);
            btnSave_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
            btnSave_->onClick = [this] {
                matriz::app::gravarTema(comboTema_->getSelectedId() == 2 ? "light" : "dark");
                matriz::app::gravarTooltipsHabilitados(toggleTooltips_->getToggleState());
                matriz::ui::recarregarTema();
                if (mainWin_ && mainWin_->conteudo_) {
                    mainWin_->conteudo_->atualizarTooltips();
                }
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

            setSize(400, 240);
        }

        void resized() override {
            auto area = getLocalBounds().reduced(16);
            lblTitulo_->setBounds(area.removeFromTop(30));
            area.removeFromTop(8);

            lblTema_->setBounds(area.removeFromTop(20));
            comboTema_->setBounds(area.removeFromTop(28));
            area.removeFromTop(12);

            toggleTooltips_->setBounds(area.removeFromTop(28));
            
            auto bottomRow = area.removeFromBottom(36);
            btnClose_->setBounds(bottomRow.removeFromRight(100));
            bottomRow.removeFromRight(8);
            btnSave_->setBounds(bottomRow.removeFromRight(130));
        }

    private:
        std::shared_ptr<juce::DialogWindow> janela_;
        MainWindow* mainWin_;
        std::unique_ptr<juce::Label> lblTitulo_;
        std::unique_ptr<juce::Label> lblTema_;
        std::unique_ptr<juce::ComboBox> comboTema_;
        std::unique_ptr<juce::ToggleButton> toggleTooltips_;
        std::unique_ptr<juce::TextButton> btnSave_;
        std::unique_ptr<juce::TextButton> btnClose_;
    };

    auto painel = std::make_unique<PainelPreferencias>(janela, this);
    janela->setContentOwned(painel.release(), true);
    janela->setResizable(false, false);
    janela->centreWithSize(400, 280);
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
