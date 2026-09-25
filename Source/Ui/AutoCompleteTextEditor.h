#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

// Fase 4 (Autocomplete por projeto): TextEditor com popup de sugestões
// vindas do histórico do projeto para aquele campo. É um juce::TextEditor
// de verdade (não um wrapper) — quem já monta um LinhaUnificada/LinhaLote
// com onFocusLost/onReturnKey/onTextChange continua fazendo exatamente
// isso, sem mudar nada além de trocar make_unique<juce::TextEditor> por
// make_unique<AutoCompleteTextEditor>(provedor). Selecionar uma sugestão do
// popup dispara esses MESMOS callbacks (onTextChange, depois onReturnKey ou
// onFocusLost) — o mesmo caminho de salvamento da Fase 0, nunca um
// caminho paralelo. dynamic_cast<juce::TextEditor*> (usado pelo flush da
// Fase 0) também continua funcionando, porque a herança é real.

namespace matriz::ui {

class AutoCompleteTextEditor : public juce::TextEditor, private juce::TextEditor::Listener {
public:
    // provedorValores é chamado sob demanda (ao focar/digitar) — nunca
    // guardamos uma cópia velha da lista de sugestões.
    explicit AutoCompleteTextEditor(std::function<std::vector<juce::String>()> provedorValores);
    ~AutoCompleteTextEditor() override;

    void focusGained(juce::Component::FocusChangeType cause) override;

private:
    class PopupRow;
    class Popup;

    void textEditorTextChanged(juce::TextEditor&) override;
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override;
    void textEditorFocusLost(juce::TextEditor&) override;

    void atualizarPopup();
    void esconderPopup();
    void selecionarValor(const juce::String& valor);

    std::function<std::vector<juce::String>()> provedorValores_;
    std::unique_ptr<Popup> popup_;
};

} // namespace matriz::ui
