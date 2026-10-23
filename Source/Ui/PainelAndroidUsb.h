#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "../Celular/RecebimentoCelular.h"

// PHONE DUMP > ANDROID USB: o celular plugado por cabo, no mesmo formato da janela de pasta do Folder Map
// (JanelaPastaFlutuante): cabeçalho com VOLTAR, nome e busca; coluna esquerda com os filtros e a árvore das pastas do
// celular (triângulos, como no Finder); mosaico no centro; ficha de metadados (com GEO) fixa à direita.
//   I            marca/desmarca a seleção do mosaico, ou a pasta selecionada na árvore (pasta = tudo dentro dela)
//   espaço / duplo clique   preview numa janela própria (← → passam)
//   IMPORT       copia o que está marcado para uma pasta de sessão em "Received from phone" e devolve essa pasta para
//                quem abriu (o Intake a ingere como a galeria do QR code: container gallery, Phone Dump).

namespace matriz::ui {

class JanelaAndroidUsb : public juce::DocumentWindow {
public:
    // `existentes` (opcional) roda numa thread de fundo: (nome, tamanho) do que o projeto já tem.
    // `aoImportar(pasta, n)` e `aoFechar()` chegam na message thread, uma vez; quem abriu destrói a janela depois.
    JanelaAndroidUsb(std::function<matriz::celular::ConjuntoExistentes()> existentes,
                     std::function<void(juce::File pasta, int recebidos)> aoImportar, std::function<void()> aoFechar);
    ~JanelaAndroidUsb() override;

    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress& key) override;

    class Conteudo;

private:
    Conteudo* conteudo_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JanelaAndroidUsb)
};

} // namespace matriz::ui
