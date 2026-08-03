#include "MainWindow.h"

#include "../App/Preferencias.h"
#include "../I18n/Strings.h"
#include "ConfiguracoesProjetoDialogo.h"
#include "NovoProjetoDialogo.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {
enum MenuIndices { kMenuArquivo = 0, kMenuProjeto = 1, kMenuPreferencias = 2, kMenuAjuda = 3 };
enum ComandoMenu {
    kCmdNovoProjeto = 1,
    kCmdAbrirProjeto,
    kCmdFecharProjeto,
    kCmdSair,
    kCmdConfiguracoes,
    kCmdIngerirArquivos,
    kCmdIdiomaIngles,
    kCmdIdiomaPortugues
};
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
    return {matriz::i18n::t("menu.arquivo"), matriz::i18n::t("menu.projeto"),
            matriz::i18n::t("menu.preferencias"), matriz::i18n::t("menu.ajuda")};
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    if (topLevelMenuIndex == kMenuArquivo) {
        // Um lote de ingest em background segura uma referência ao registro
        // do projeto atual — trocar/fechar o projeto nesse meio tempo
        // deixaria o job com uma referência pendurada (§ver MainComponent::
        // fecharProjeto). Trocar de projeto é só uma reabertura disfarçada
        // de "novo"/"abrir", então as três ficam desabilitadas juntas.
        bool podeTrocarProjeto = !conteudo_->ingestEmAndamento();
        menu.addItem(kCmdNovoProjeto, matriz::i18n::t("menu.arquivo_novo_projeto"), podeTrocarProjeto);
        menu.addItem(kCmdAbrirProjeto, matriz::i18n::t("menu.arquivo_abrir_projeto"), podeTrocarProjeto);
        menu.addItem(kCmdFecharProjeto, matriz::i18n::t("menu.arquivo_fechar_projeto"),
                     conteudo_->temProjetoAberto() && podeTrocarProjeto);
        menu.addSeparator();
        menu.addItem(kCmdIngerirArquivos, matriz::i18n::t("menu.arquivo_ingerir_arquivos"), conteudo_->temProjetoAberto());
        menu.addSeparator();
        menu.addItem(kCmdSair, matriz::i18n::t("menu.arquivo_sair"));
    } else if (topLevelMenuIndex == kMenuProjeto) {
        menu.addItem(kCmdConfiguracoes, matriz::i18n::t("menu.projeto_configuracoes"), conteudo_->temProjetoAberto());
    } else if (topLevelMenuIndex == kMenuPreferencias) {
        // Troca aplicada na hora (Parte 2): reconstrói toda a UI com o
        // idioma novo, sem reiniciar o aplicativo.
        juce::String localeAtual = matriz::app::lerLocale();
        menu.addItem(kCmdIdiomaIngles, matriz::i18n::t("menu.preferencias_idioma_ingles"), true, localeAtual == "en");
        menu.addItem(kCmdIdiomaPortugues, matriz::i18n::t("menu.preferencias_idioma_portugues"), true,
                     localeAtual == "pt_BR");
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
        case kCmdIdiomaIngles: trocarIdioma("en"); break;
        case kCmdIdiomaPortugues: trocarIdioma("pt_BR"); break;
        default: break;
    }
}

void MainWindow::trocarIdioma(const juce::String& locale) {
    if (locale == matriz::app::lerLocale()) return;
    if (conteudo_->ingestEmAndamento()) return; // menu já trava as outras trocas de estado nesse caso

    matriz::app::gravarLocale(locale);
    matriz::i18n::carregar(locale);

    // Reconstrói o MainComponent inteiro: é o único jeito de garantir que
    // toda string em tela (não só o menu, que já retraduz sozinho porque
    // consulta t() de novo a cada vez que abre) reflete o idioma novo sem
    // precisar de um retranslateUi() em cada Component existente.
    auto projetoAberto = conteudo_->destacarProjeto();
    conteudo_ = std::make_unique<MainComponent>();
    conteudo_->aoPedirNovoProjeto = [this] { pedirNovoProjeto(); };
    conteudo_->aoPedirAbrirProjeto = [this] { pedirAbrirProjeto(); };
    setContentNonOwned(conteudo_.get(), true);
    if (projetoAberto) conteudo_->abrirProjeto(std::move(projetoAberto));

    menuItemsChanged();
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
