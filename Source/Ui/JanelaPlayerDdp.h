#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

#include "../Ddp/DdpModel.h"
#include "../Ddp/DdpPlayback.h"

namespace matriz::ui {

// Player de DDP: toca a imagem de um master de CD como um CD e mostra os dados do disco e das faixas
// (baseado no BKR DDP Check, no layout da moldura do Matriz).
class JanelaPlayerDdp : public juce::Component, private juce::Timer, private juce::TableListBoxModel {
public:
    explicit JanelaPlayerDdp(const juce::File& pastaDdp);
    ~JanelaPlayerDdp() override;

    // Abre a janela; devolve false (com `erro`) se a pasta não for um DDP legível.
    static bool abrir(const juce::File& pastaDdp, juce::String& erro);
    // Auto-teste: imprime disco/faixas lidos e grava um PNG da janela.
    static bool autoTeste(const juce::File& pastaDdp, const juce::File& png);
    // Procura entre os arquivos de origem a pasta que contém o DDPID.
    static juce::File acharPastaDdp(const std::vector<juce::File>& arquivos);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    int getNumRows() override { return static_cast<int>(disco_.tracks.size()); }
    void paintRowBackground(juce::Graphics&, int row, int w, int h, bool selecionada) override;
    void paintCell(juce::Graphics&, int row, int col, int w, int h, bool selecionada) override;
    void cellClicked(int row, int col, const juce::MouseEvent& e) override;
    void atualizarBotoes();
    void fechar();

    juce::File pasta_;
    bkr::DdpDisc disco_;
    bool carregado_ = false;
    juce::String erro_;
    bool checksumsProntos_ = false;
    std::unique_ptr<bkr::DdpPlayback> player_;

    juce::TableListBox tabela_;
    juce::TextButton btnAnterior_, btnPlay_, btnParar_, btnProximo_, btnInverter_;
    juce::Slider posicao_;
    bool arrastando_ = false;
    juce::Rectangle<int> areaInfo_, areaDisplay_;
    int faixaAtual_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JanelaPlayerDdp)
};

}  // namespace matriz::ui
