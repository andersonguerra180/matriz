#include "MainWindow.h"

#include "../Catalogo/CatalogoProxies.h"

#include "../App/Preferencias.h"
#include "../Diag/NSExceptionGuard.h"
#include "../I18n/Strings.h"
#include "AboutDialog.h"
#include "ConfiguracoesProjetoDialogo.h"
#include "ConsolidacaoDialogo.h"
#include "ProjectLogViewerDialog.h"
#include "../Model/ProjectLog.h"
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
    kCmdAbout,
    kCmdUndo,
    kCmdProjectLog,
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

void MainWindow::lookAndFeelChanged() {
    setBackgroundColour(tema().fundo);
    if (conteudo_) {
        conteudo_->sendLookAndFeelChange();
        conteudo_->repaint();
    }
    repaint();
}

juce::StringArray MainWindow::getMenuBarNames() {
    juce::String locale = matriz::app::lerLocale();
    bool isPt = locale.equalsIgnoreCase("pt_BR") || locale.equalsIgnoreCase("pt-BR") || locale.equalsIgnoreCase("pt");
    if (isPt) {
        return {juce::String::fromUTF8("Arquivo"), juce::String::fromUTF8("Editar"),
                juce::String::fromUTF8("Projeto"), juce::String::fromUTF8("Preferências"),
                juce::String::fromUTF8("Ajuda")};
    }
    return {"File", "Edit", "Project", "Preferences", "Help"};
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    juce::String locale = matriz::app::lerLocale();
    bool isPt = locale.equalsIgnoreCase("pt_BR") || locale.equalsIgnoreCase("pt-BR") || locale.equalsIgnoreCase("pt");

    if (topLevelMenuIndex == kMenuArquivo) {
        bool podeTrocarProjeto = !conteudo_->ingestEmAndamento();
        bool temProjeto = conteudo_->temProjetoAberto() || conteudo_->temCatalogoAberto();
        bool isCatalog = (conteudo_->projetoAberto() && conteudo_->projetoAberto()->projeto().modo() == matriz::model::Modo::Catalogo);

        // New Submenu
        juce::PopupMenu newMenu;
        newMenu.addItem(kCmdNovoProjeto, isPt ? juce::String::fromUTF8("Coleção (.mtz)...") : "Collection (.mtz)...", podeTrocarProjeto);
        newMenu.addItem(kCmdNovoCatalogo, isPt ? juce::String::fromUTF8("Catálogo (.bkm)...") : "Catalog (.bkm)...", podeTrocarProjeto);
        menu.addSubMenu(isPt ? juce::String::fromUTF8("Novo") : "New", newMenu, podeTrocarProjeto);

        // Open Submenu
        juce::PopupMenu openMenu;
        openMenu.addItem(kCmdAbrirProjeto, isPt ? juce::String::fromUTF8("Coleção (.mtz)...") : "Collection (.mtz)...", podeTrocarProjeto);
        openMenu.addItem(kCmdAbrirCatalogo, isPt ? juce::String::fromUTF8("Catálogo (.bkm)...") : "Catalog (.bkm)...", podeTrocarProjeto);
        menu.addSubMenu(isPt ? juce::String::fromUTF8("Abrir") : "Open", openMenu, podeTrocarProjeto);

        // Recent Files Submenu
        juce::PopupMenu recentMenu;
        auto recentes = matriz::app::lerRecentes();
        if (!recentes.empty()) {
            int rIdx = 0;
            for (const auto& r : recentes) {
                juce::String nome = r.nome.isEmpty() ? juce::File(r.pasta).getFileName() : r.nome;
                bool isCat = (r.modo.equalsIgnoreCase("catalogo") || r.modo.equalsIgnoreCase("catalog"));
                juce::String tag = isPt ? (isCat ? juce::String::fromUTF8("[CATÁLOGO]") : juce::String::fromUTF8("[COLEÇÃO]"))
                                        : (isCat ? "[CATALOG]" : "[COLLECTION]");
                juce::String label = tag + "  " + nome;
                if (!r.pasta.isEmpty())
                    label += juce::String::fromUTF8(" — ") + r.pasta;
                recentMenu.addItem(kCmdRecenteBase + rIdx, label);
                rIdx++;
            }
        } else {
            recentMenu.addItem(1, isPt ? juce::String::fromUTF8("Nenhum Arquivo Recente") : "No Recent Files", false);
        }
        menu.addSubMenu(isPt ? juce::String::fromUTF8("Abrir Arquivos Recentes") : "Open Recent Files", recentMenu, podeTrocarProjeto);

        menu.addSeparator();
        juce::String saveText = "Save (Cmd+S)";
        juce::String saveAsText = "Save As... (Cmd+Shift+S)";
        juce::String closeText = "Close";
        juce::String infoText = "Project Info...";

        if (temProjeto) {
            if (isCatalog) {
                saveText = isPt ? juce::String::fromUTF8("Salvar Catálogo (Cmd+S)") : "Save Catalog (Cmd+S)";
                saveAsText = isPt ? juce::String::fromUTF8("Salvar Catálogo Como... (Cmd+Shift+S)") : "Save Catalog As... (Cmd+Shift+S)";
                closeText = isPt ? juce::String::fromUTF8("Fechar Catálogo") : "Close Catalog";
                infoText = isPt ? juce::String::fromUTF8("Info do Catálogo...") : "Catalog Info...";
            } else {
                saveText = isPt ? juce::String::fromUTF8("Salvar Coleção (Cmd+S)") : "Save Collection (Cmd+S)";
                saveAsText = isPt ? juce::String::fromUTF8("Salvar Coleção Como... (Cmd+Shift+S)") : "Save Collection As... (Cmd+Shift+S)";
                closeText = isPt ? juce::String::fromUTF8("Fechar Coleção") : "Close Collection";
                infoText = isPt ? juce::String::fromUTF8("Info da Coleção...") : "Collection Info...";
            }
        } else if (isPt) {
            saveText = juce::String::fromUTF8("Salvar (Cmd+S)");
            saveAsText = juce::String::fromUTF8("Salvar Como... (Cmd+Shift+S)");
            closeText = juce::String::fromUTF8("Fechar");
            infoText = juce::String::fromUTF8("Info do Projeto...");
        }

        menu.addItem(kCmdSalvarProjeto, saveText, temProjeto);
        menu.addItem(kCmdSalvarProjetoComo, saveAsText, temProjeto);
        menu.addItem(kCmdFecharProjeto, closeText, temProjeto && podeTrocarProjeto);
        menu.addSeparator();
        menu.addItem(kCmdIngerirArquivos, isPt ? juce::String::fromUTF8("Adicionar Arquivos...") : "Add Files...", conteudo_->temProjetoAberto() && !isCatalog);
        menu.addSeparator();
        menu.addItem(kCmdSair, isPt ? juce::String::fromUTF8("Encerrar") : "Quit");
    } else if (topLevelMenuIndex == kMenuEditar) {
        bool podeUndo = conteudo_->temProjetoAberto() && conteudo_->podeDesfazer();
        menu.addItem(kCmdUndo, isPt ? juce::String::fromUTF8("Desfazer (Cmd+Z)") : "Undo (Cmd+Z)", podeUndo, false, nullptr);
        menu.addSeparator();
        menu.addItem(kCmdRenomearItem, isPt ? juce::String::fromUTF8("Renomear Item(ns)... (R)") : "Rename Item(s)... (R)", conteudo_->temProjetoAberto());
        menu.addItem(kCmdRemoverDoBackup, isPt ? juce::String::fromUTF8("Remover Selecionado do Backup (C)") : "Remove Selected from Backup (C)", conteudo_->temProjetoAberto());
    } else if (topLevelMenuIndex == kMenuProjeto) {
        menu.addItem(kCmdConsolidar, isPt ? juce::String::fromUTF8("Consolidar / Relocar Arquivos...") : "Consolidate / Relocate Files...", conteudo_->temProjetoAberto());
        menu.addSeparator();
        menu.addItem(kCmdProjectLog, isPt ? juce::String::fromUTF8("Registro de Alterações do Projeto (log.md)...") : "Project Log (log.md)...", conteudo_->temProjetoAberto());
    } else if (topLevelMenuIndex == kMenuPreferencias) {
        menu.addItem(kCmdPreferenciasGerais, isPt ? juce::String::fromUTF8("Preferências / Tema / Chave de IA...") : "Preferences / Theme / AI Key...");
        menu.addItem(kCmdAudioDevice, isPt ? juce::String::fromUTF8("Dispositivo de Áudio...") : "Audio Device...");
    } else if (topLevelMenuIndex == kMenuAjuda) {
        menu.addItem(kCmdAbout, isPt ? juce::String::fromUTF8("Sobre o BKR Matriz...") : "About BKR Matriz...");
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
        case kCmdAbout: mostrarAboutDialogo(); break;
        case kCmdProjectLog:
            if (conteudo_->temProjetoAberto()) {
                matriz::model::ProjectLog pLog(conteudo_->pastaProjeto());
                matriz::ui::ProjectLogViewerDialog::showModal(std::move(pLog));
            }
            break;
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
    conteudo_->aoTrocarIdioma = [this](const juce::String& locale) {
        trocarIdioma(locale);
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
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    auto janela = std::make_shared<juce::DialogWindow>(
        isPt ? juce::String::fromUTF8("Preferências e Tema") : "Preferences & Theme",
        tema().painel, true);

    struct PainelPreferencias : public juce::Component {
        PainelPreferencias(std::shared_ptr<juce::DialogWindow> win, MainWindow* mainWin)
            : janela_(std::move(win)), mainWin_(mainWin) {
            const auto& tk = tema();
            bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

            lblTitulo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Preferências do Aplicativo") : "Application Preferences");
            lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
            lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*lblTitulo_);

            lblIdioma_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Idioma / Language:") : "Language / Idioma:");
            lblIdioma_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
            lblIdioma_->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lblIdioma_);

            comboIdioma_ = std::make_unique<juce::ComboBox>();
            comboIdioma_->addItem("English (EN-US)", 1);
            comboIdioma_->addItem(juce::String::fromUTF8("Português do Brasil (PT-BR)"), 2);
            comboIdioma_->setSelectedId(isPt ? 2 : 1, juce::dontSendNotification);
            addAndMakeVisible(*comboIdioma_);

            lblTema_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Aparência / Tema da UI:") : "Appearance / UI Theme:");
            lblTema_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
            lblTema_->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lblTema_);

            comboTema_ = std::make_unique<juce::ComboBox>();
            comboTema_->addItem(isPt ? juce::String::fromUTF8("Escuro (BKR Dark)") : "Dark (BKR Dark)", 1);
            comboTema_->addItem(isPt ? juce::String::fromUTF8("Claro (BKR Light)") : "Light (BKR Light)", 2);
            juce::String temaAtual = matriz::app::lerTema();
            comboTema_->setSelectedId(temaAtual == "light" ? 2 : 1, juce::dontSendNotification);
            addAndMakeVisible(*comboTema_);

            toggleTooltips_ = std::make_unique<juce::ToggleButton>(isPt ? juce::String::fromUTF8("Exibir dicas ao passar o mouse (tooltips)") : "Show tooltips (hints on hover)");
            toggleTooltips_->setToggleState(matriz::app::lerTooltipsHabilitados(), juce::dontSendNotification);
            toggleTooltips_->setColour(juce::ToggleButton::textColourId, tk.textoSecundario);
            addAndMakeVisible(*toggleTooltips_);

            btnSave_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("SALVAR E APLICAR") : "SAVE & APPLY");
            btnSave_->setColour(juce::TextButton::buttonColourId, tk.acento);
            btnSave_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
            btnSave_->onClick = [this] {
                auto novoLocale = comboIdioma_->getSelectedId() == 2 ? "pt_BR" : "en";
                bool idiomaMudou = (novoLocale != matriz::i18n::localeAtivo());
                matriz::app::gravarLocale(novoLocale);
                matriz::app::gravarTema(comboTema_->getSelectedId() == 2 ? "light" : "dark");
                matriz::app::gravarTooltipsHabilitados(toggleTooltips_->getToggleState());

                if (idiomaMudou && mainWin_) {
                    mainWin_->trocarIdioma(novoLocale);
                }

                matriz::ui::aplicarTemaGlobal(mainWin_);
                if (mainWin_ && mainWin_->conteudo_) {
                    mainWin_->conteudo_->atualizarTooltips();
                }
                if (janela_) janela_->exitModalState(0);
            };
            addAndMakeVisible(*btnSave_);

            btnClose_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("FECHAR") : "CLOSE");
            btnClose_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
            btnClose_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
            btnClose_->onClick = [this] {
                if (janela_) janela_->exitModalState(0);
            };
            addAndMakeVisible(*btnClose_);

            setSize(420, 310);
        }

        void resized() override {
            auto area = getLocalBounds().reduced(16);
            lblTitulo_->setBounds(area.removeFromTop(30));
            area.removeFromTop(8);

            lblIdioma_->setBounds(area.removeFromTop(20));
            comboIdioma_->setBounds(area.removeFromTop(28));
            area.removeFromTop(10);

            lblTema_->setBounds(area.removeFromTop(20));
            comboTema_->setBounds(area.removeFromTop(28));
            area.removeFromTop(12);

            toggleTooltips_->setBounds(area.removeFromTop(28));
            
            auto bottomRow = area.removeFromBottom(36);
            btnClose_->setBounds(bottomRow.removeFromRight(100));
            bottomRow.removeFromRight(8);
            btnSave_->setBounds(bottomRow.removeFromRight(140));
        }

    private:
        std::shared_ptr<juce::DialogWindow> janela_;
        MainWindow* mainWin_;
        std::unique_ptr<juce::Label> lblTitulo_;
        std::unique_ptr<juce::Label> lblIdioma_;
        std::unique_ptr<juce::ComboBox> comboIdioma_;
        std::unique_ptr<juce::Label> lblTema_;
        std::unique_ptr<juce::ComboBox> comboTema_;
        std::unique_ptr<juce::ToggleButton> toggleTooltips_;
        std::unique_ptr<juce::TextButton> btnSave_;
        std::unique_ptr<juce::TextButton> btnClose_;
    };

    auto painel = std::make_unique<PainelPreferencias>(janela, this);
    janela->setContentOwned(painel.release(), true);
    janela->setResizable(false, false);
    janela->centreWithSize(420, 350);
    janela->setVisible(true);
    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela](int) {
        janela->setVisible(false);
    }));
}

void MainWindow::trocarIdioma(const juce::String& locale) {
    matriz::app::gravarLocale(locale);
    matriz::i18n::carregar(locale);

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
    juce::MenuBarModel::setMacMainMenu(this);
#endif
    menuItemsChanged();

    if (conteudo_) {
        conteudo_->sendLookAndFeelChange();
        conteudo_->repaint();
    }
    repaint();
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

void MainWindow::mostrarAboutDialogo() {
    AboutDialog::exibirModal();
}

} // namespace matriz::ui
