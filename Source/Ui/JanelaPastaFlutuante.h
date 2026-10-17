#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <set>
#include <string>

#include "ProjetoAberto.h"

namespace matriz::ui {

// Janela flutuante de uma pasta do Folder Map (duplo clique no card da pasta, em Structure): grade de miniaturas com todos os
// arquivos da pasta e das subpastas, coluna de filtros de navegação à esquerda e o botão direito padrão do app
// (+ EXTRACT da pasta e MOVE TO FOLDER). Não apaga nada em disco: "extrair" tira o arquivo da pasta (vai para NO FOLDER).
class JanelaPastaFlutuante : public juce::DocumentWindow {
public:
    JanelaPastaFlutuante(ProjetoAberto& projeto, const std::string& pastaId, const juce::String& nomePasta,
                         std::function<void()> aoFechar, std::function<void()> aoMudouDados);
    ~JanelaPastaFlutuante() override;

    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class Conteudo;
    Conteudo* conteudo_ = nullptr;
    std::function<void()> aoFechar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JanelaPastaFlutuante)
};

}  // namespace matriz::ui
