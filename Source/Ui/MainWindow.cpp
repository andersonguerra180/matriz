#include "MainWindow.h"

#include "../I18n/Strings.h"
#include "ConfiguracoesProjetoDialogo.h"
#include "NovoProjetoDialogo.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {
enum MenuIndices { kMenuArquivo = 0, kMenuProjeto = 1, kMenuAjuda = 2 };
enum ComandoMenu { kCmdNovoProjeto = 1, kCmdAbrirProjeto, kCmdFecharProjeto, kCmdSair, kCmdConfiguracoes, kCmdIngerirArquivos };
} // namespace

MainWindow::MainWindow(const juce::String& nome)
    : DocumentWindow(nome, tema().fundo, DocumentWindow::allButtons) {
    setUsingNativeTitleBar(true);

    conteudo_ = std::make_unique<MainComponent>();
    conteudo_->aoPedirNovoProjeto = [this] { pedirNovoProjeto(); };
    conteudo_->aoPedirAbrirProjeto = [this] { pedirAbrirProjeto(); };
    setContentNonOwned(conteudo_.get(), true);

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
#else
    setMenuBar(this);
#endif

    centreWithSize(1200, 800);
    setResizable(true, false);
    setVisible(true);
}

MainWindow::~MainWindow() {
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#else
    setMenuBar(nullptr);
#endif
    conteudo_.reset();
}

void MainWindow::closeButtonPressed() { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

juce::StringArray MainWindow::getMenuBarNames() {
    return {matriz::i18n::t("menu.arquivo"), matriz::i18n::t("menu.projeto"), matriz::i18n::t("menu.ajuda")};
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    if (topLevelMenuIndex == kMenuArquivo) {
        menu.addItem(kCmdNovoProjeto, matriz::i18n::t("menu.arquivo_novo_projeto"));
        menu.addItem(kCmdAbrirProjeto, matriz::i18n::t("menu.arquivo_abrir_projeto"));
        menu.addItem(kCmdFecharProjeto, matriz::i18n::t("menu.arquivo_fechar_projeto"), conteudo_->temProjetoAberto());
        menu.addSeparator();
        menu.addItem(kCmdIngerirArquivos, matriz::i18n::t("menu.arquivo_ingerir_arquivos"), conteudo_->temProjetoAberto());
        menu.addSeparator();
        menu.addItem(kCmdSair, matriz::i18n::t("menu.arquivo_sair"));
    } else if (topLevelMenuIndex == kMenuProjeto) {
        menu.addItem(kCmdConfiguracoes, matriz::i18n::t("menu.projeto_configuracoes"), conteudo_->temProjetoAberto());
    }
    return menu;
}

void MainWindow::menuItemSelected(int menuItemID, int) {
    switch (menuItemID) {
        case kCmdNovoProjeto: pedirNovoProjeto(); break;
        case kCmdAbrirProjeto: pedirAbrirProjeto(); break;
        case kCmdFecharProjeto: conteudo_->fecharProjeto(); break;
        case kCmdSair: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case kCmdConfiguracoes: pedirConfiguracoesProjeto(); break;
        case kCmdIngerirArquivos: pedirIngerirArquivos(); break;
        default: break;
    }
}

void MainWindow::pedirNovoProjeto() {
    mostrarDialogoNovoProjeto([this](std::optional<NovoProjetoResultado> resultado) {
        if (!resultado) return;
        try {
            auto projeto = matriz::model::Project::criar(resultado->pasta, resultado->params);
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

void MainWindow::pedirAbrirProjeto() {
    auto chooser = std::make_shared<juce::FileChooser>(matriz::i18n::t("dialogo_abrir_projeto.titulo"));
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser](const juce::FileChooser& fc) {
                              juce::File pasta = fc.getResult();
                              if (pasta == juce::File()) return;
                              try {
                                  auto projeto = matriz::model::Project::abrir(pasta);
                                  conteudo_->abrirProjeto(std::move(projeto));
                              } catch (const std::exception& e) {
                                  juce::AlertWindow::showAsync(
                                      juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::WarningIcon)
                                          .withTitle(matriz::i18n::t("dialogo_abrir_projeto.erro_titulo"))
                                          .withMessage(juce::String(e.what()))
                                          .withButton(matriz::i18n::t("comum.ok")),
                                      static_cast<juce::ModalComponentManager::Callback*>(nullptr));
                              }
                          });
}

void MainWindow::pedirConfiguracoesProjeto() {
    if (!conteudo_->temProjetoAberto()) return;
    mostrarDialogoConfiguracoesProjeto(*conteudo_->projetoAberto(), [] {});
}

void MainWindow::pedirIngerirArquivos() {
    if (!conteudo_->temProjetoAberto()) return;
    auto chooser = std::make_shared<juce::FileChooser>(matriz::i18n::t("menu.arquivo_ingerir_arquivos"));
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, chooser](const juce::FileChooser& fc) {
                              auto resultados = fc.getResults();
                              if (resultados.isEmpty()) return;
                              conteudo_->ingerirArquivos(resultados);
                          });
}

} // namespace matriz::ui
