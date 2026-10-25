#pragma once

#include <JuceHeader.h>

#include "Moldura.h"

namespace matriz::ui {

// Janela independente da verificação de duplicatas (botão "Find duplicates" do card FILTERS, no METADATA). Só hospeda o
// DuplicatesWorkspaceComponent, que continua sendo do MainComponent (não é dono): fechar só esconde, e a varredura e os
// resultados ficam como estavam até a próxima abertura. O MainComponent destrói a janela antes do workspace.
class JanelaDuplicatas : public juce::DocumentWindow {
public:
    explicit JanelaDuplicatas(juce::Component& conteudo, const juce::String& titulo)
        : juce::DocumentWindow(titulo, moldura::paleta().fundo, juce::DocumentWindow::closeButton) {
        // Moldura do sistema de janelas: sem barra nativa; cabeçalho + X redondo desenhados pela JanelaMoldura.
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setBackgroundColour(moldura::paleta().fundo);
        setResizable(true, true);
        setResizeLimits(900 + 2 * moldura::kMargemJanelaNativa, 560 + moldura::kAlturaCabecalho + 2 * moldura::kMargemJanelaNativa,
                        4000, 3000);
        setContentOwned(new moldura::JanelaMoldura(&conteudo, moldura::Secao::Explorador, "Metadata", titulo, true, false), false);
        centreWithSize(1280 + 2 * moldura::kMargemJanelaNativa, 800 + moldura::kAlturaCabecalho + 2 * moldura::kMargemJanelaNativa);
    }
    ~JanelaDuplicatas() override { clearContentComponent(); }

    void mostrar() {
        setVisible(true);
        toFront(true);
    }
    void closeButtonPressed() override { setVisible(false); }
    // Voltar para a janela (ex.: depois de mudar a seleção no grid): quem hospeda atualiza o que depende do grid.
    std::function<void()> aoAtivar;
    void activeWindowStatusChanged() override {
        DocumentWindow::activeWindowStatusChanged();
        if (isActiveWindow() && aoAtivar) aoAtivar();
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) { closeButtonPressed(); return true; }
        return DocumentWindow::keyPressed(key);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JanelaDuplicatas)
};

}  // namespace matriz::ui
