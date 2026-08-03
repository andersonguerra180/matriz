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
    void pedirNovoProjeto();
    void pedirAbrirProjeto();
    void pedirConfiguracoesProjeto();
    void pedirIngerirArquivos();
    void trocarIdioma(const juce::String& locale);

    std::unique_ptr<MainComponent> conteudo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace matriz::ui
