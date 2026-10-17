#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>

#include "ProjetoAberto.h"

namespace matriz::ui {

// Janela flutuante (não modal) para escolher uma pasta de destino — usada por
// "Move to Folder" / "Send to Folder" no lugar do PopupMenu plano: árvore
// colapsável/expansível com scroll e caixa de busca. Duplo clique, Enter ou o
// botão confirmam; Esc ou fechar cancelam. Só existe uma por vez.
class PastaPickerWindow : public juce::DocumentWindow {
public:
    using AoEscolher = std::function<void(const std::string& pastaId)>;

    // `raiz` é copiada — o chamador não precisa manter a árvore viva.
    static void abrir(const juce::String& titulo, const ProjetoAberto::NoArvore& raiz, AoEscolher aoEscolher);

    ~PastaPickerWindow() override;
    void closeButtonPressed() override;

private:
    class Conteudo;
    PastaPickerWindow(const juce::String& titulo, const ProjetoAberto::NoArvore& raiz, AoEscolher aoEscolher);
    void fecharAssincrono();

    JUCE_DECLARE_WEAK_REFERENCEABLE(PastaPickerWindow)
};

}  // namespace matriz::ui
