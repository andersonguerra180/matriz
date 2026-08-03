#pragma once

#include <JuceHeader.h>

#include "MainComponent.h"

namespace matriz::ui {

class MainWindow : public juce::DocumentWindow, public juce::MenuBarModel {
public:
    MainWindow(const juce::String& nome);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // juce::MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    // Chamado pelos cartões da tela inicial — o modo já foi escolhido lá.
    void pedirNovoProjeto(matriz::model::Modo modo);
    // Chamado pelo menu Arquivo > Novo projeto, que pode ser acionado com um
    // projeto já aberto (não existe tela de cartões nesse momento) — pergunta
    // o modo com um diálogo mínimo antes de seguir pro fluxo de sempre.
    void pedirNovoProjetoViaMenu();
    void pedirAbrirProjeto();
    void abrirPasta(const juce::File& pasta);
    void pedirConfiguracoesProjeto();
    void pedirIngerirArquivos();
    void trocarIdioma(const juce::String& locale);
    void conectarConteudo();

    std::unique_ptr<MainComponent> conteudo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace matriz::ui
