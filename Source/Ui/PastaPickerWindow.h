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

    // Outros usos da mesma janela (ex.: Include in Container): textos próprios; vazio = o texto de pasta.
    // `soFolhas`: nós com filhos são só agrupadores (abrem/fecham, não são destino).
    struct Textos {
        juce::String buscar, botao, semResultados;
        bool soFolhas = false;
    };

    // `raiz` é copiada — o chamador não precisa manter a árvore viva.
    static void abrir(const juce::String& titulo, const ProjetoAberto::NoArvore& raiz, AoEscolher aoEscolher);
    static void abrir(const juce::String& titulo, const ProjetoAberto::NoArvore& raiz, AoEscolher aoEscolher, const Textos& textos);

    ~PastaPickerWindow() override;
    void closeButtonPressed() override;

private:
    class Conteudo;
    PastaPickerWindow(const juce::String& titulo, const ProjetoAberto::NoArvore& raiz, AoEscolher aoEscolher, const Textos& textos);
    void fecharAssincrono();

    JUCE_DECLARE_WEAK_REFERENCEABLE(PastaPickerWindow)
};

}  // namespace matriz::ui
