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
// (+ EXTRACT da pasta e MOVE TO FOLDER). Com `nestId` é a janela de um CONTAINER: só os arquivos dele (pastaId é ignorado). A ficha
// da direita é SOMENTE LEITURA. Não apaga nada em disco: "extrair" tira o arquivo da pasta (vai para NO FOLDER).
// Com pastaId == kPastaSemPasta é a pasta virtual `_no folder`: os itens sem pasta no mapa selecionado do Folder Map (os mesmos
// que o card NO FOLDER conta), carregados fora da message thread; `selecionarTudo` abre com todos eles selecionados.
class FloatingPreviewWindow;

class JanelaPastaFlutuante : public juce::DocumentWindow {
public:
    static constexpr const char* kPastaSemPasta = "@no_folder";
    static constexpr const char* kNomeSemPasta = "_no folder";

    JanelaPastaFlutuante(ProjetoAberto& projeto, const std::string& pastaId, const juce::String& nomePasta,
                         std::function<void()> aoFechar, std::function<void()> aoMudouDados, const std::string& nestId = {},
                         bool selecionarTudo = false);
    ~JanelaPastaFlutuante() override;

    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress& key) override;
    // Self-test (--selftest-explorer-visual): entra na pasta `id` como o duplo clique faria.
    void irParaPastaParaTeste(const std::string& id, const juce::String& nome);
    void mostrarArquivoParaTeste(const std::string& itemId);
    // Duplo clique no arquivo: o preview aberto (nullptr se não abriu).
    FloatingPreviewWindow* abrirPreviewParaTeste(const std::string& itemId);
    void mostrarAbaFiltrosParaTeste();  // container: lateral na aba Filters

private:
    class Conteudo;
    Conteudo* conteudo_ = nullptr;
    std::function<void()> aoFechar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JanelaPastaFlutuante)
};

}  // namespace matriz::ui
