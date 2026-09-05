#include "Tokens.h"
#include "../App/Preferencias.h"

namespace matriz::ui {

namespace {
    enum class TemaAtivo { Dark, Light };
    TemaAtivo temaAtivo_ = TemaAtivo::Dark;
    float escalaFonte_ = 1.0f;
    bool inicializado_ = false;

    void inicializarSeNecessario() {
        if (inicializado_) return;
        inicializado_ = true;
        auto pref = matriz::app::lerTema();
        temaAtivo_ = (pref == "light") ? TemaAtivo::Light : TemaAtivo::Dark;
        escalaFonte_ = matriz::app::lerEscalaFonte();
    }
}

const Tema& tema() {
    inicializarSeNecessario();
    return temaAtivo_ == TemaAtivo::Light ? temaBkrLight() : temaBkrDark();
}

void recarregarTema() {
    inicializado_ = false;
    inicializarSeNecessario();
    if (auto* lf = dynamic_cast<juce::LookAndFeel_V4*>(&juce::LookAndFeel::getDefaultLookAndFeel())) {
        configurarLookAndFeel(*lf);
    }
}

void aplicarTemaGlobal(juce::Component* raiz) {
    recarregarTema();
    if (raiz != nullptr) {
        raiz->sendLookAndFeelChange();
        raiz->repaint();
    }
    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i) {
        if (auto* c = juce::Desktop::getInstance().getComponent(i)) {
            c->sendLookAndFeelChange();
            c->repaint();
        }
    }
}

void configurarLookAndFeel(juce::LookAndFeel_V4& lf) {
    const auto& tk = tema();
    juce::LookAndFeel_V4::ColourScheme esquema{
        tk.fundo,            // windowBackground
        tk.painel,           // widgetBackground
        tk.painel,           // menuBackground
        tk.borda,            // outline
        tk.textoPrimario,    // defaultText
        tk.acento,           // defaultFill
        tk.textoSobreAcento, // highlightedText
        tk.acentoHover,      // highlightColour
        tk.textoPrimario     // menuText
    };
    lf.setColourScheme(esquema);

    // ListBox & Tables
    lf.setColour(juce::ListBox::backgroundColourId, tk.painel);
    lf.setColour(juce::ListBox::outlineColourId, tk.borda);
    lf.setColour(juce::ListBox::textColourId, tk.textoPrimario);

    lf.setColour(juce::TableHeaderComponent::backgroundColourId, tk.painelAlt);
    lf.setColour(juce::TableHeaderComponent::textColourId, tk.textoPrimario);
    lf.setColour(juce::TableHeaderComponent::outlineColourId, tk.borda);
    lf.setColour(juce::TableHeaderComponent::highlightColourId, tk.acento);

    // ComboBox
    lf.setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
    lf.setColour(juce::ComboBox::textColourId, tk.textoPrimario);
    lf.setColour(juce::ComboBox::outlineColourId, tk.borda);
    lf.setColour(juce::ComboBox::arrowColourId, tk.textoPrimario);
    lf.setColour(juce::ComboBox::buttonColourId, tk.painelAlt);
    lf.setColour(juce::ComboBox::focusedOutlineColourId, tk.acento);

    // ToggleButton
    lf.setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    lf.setColour(juce::ToggleButton::tickColourId, tk.acento);
    lf.setColour(juce::ToggleButton::tickDisabledColourId, tk.textoTerciario);

    // TextEditor
    lf.setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
    lf.setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    lf.setColour(juce::TextEditor::outlineColourId, tk.borda);
    lf.setColour(juce::TextEditor::focusedOutlineColourId, tk.acento);
    lf.setColour(juce::TextEditor::highlightColourId, tk.acento.withAlpha(0.35f));
    lf.setColour(juce::TextEditor::highlightedTextColourId, tk.textoPrimario);
    lf.setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);

    // PopupMenu
    lf.setColour(juce::PopupMenu::backgroundColourId, tk.painelAlt);
    lf.setColour(juce::PopupMenu::textColourId, tk.textoPrimario);
    lf.setColour(juce::PopupMenu::headerTextColourId, tk.textoSecundario);
    lf.setColour(juce::PopupMenu::highlightedBackgroundColourId, tk.acento);
    lf.setColour(juce::PopupMenu::highlightedTextColourId, tk.textoSobreAcento);

    // ScrollBar
    lf.setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    lf.setColour(juce::ScrollBar::thumbColourId, tk.borda.brighter(0.15f));
    lf.setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    // Dialog / AlertWindow
    lf.setColour(juce::AlertWindow::backgroundColourId, tk.painel);
    lf.setColour(juce::AlertWindow::textColourId, tk.textoPrimario);
    lf.setColour(juce::AlertWindow::outlineColourId, tk.borda);
}

float escalaFonte() {
    inicializarSeNecessario();
    return escalaFonte_;
}

} // namespace matriz::ui
