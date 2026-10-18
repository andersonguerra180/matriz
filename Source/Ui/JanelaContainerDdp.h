#pragma once

#include <JuceHeader.h>

#include <memory>
#include <string>
#include <vector>

#include "JanelaPlayerDdp.h"
#include "PainelMetadadosArquivo.h"

namespace matriz::ui {

class ProjetoAberto;

// Janela do container DDP: lista dos arquivos do container à esquerda, o player de CD no meio e, à direita, os
// metadados do arquivo selecionado (mesmo painel do INTAKE). Abre igual no INTAKE, no METADATA e no folder map.
class JanelaContainerDdp : public juce::Component, private juce::ListBoxModel {
public:
    JanelaContainerDdp(ProjetoAberto& projeto, const std::string& nestId, const juce::File& pastaDdp);
    ~JanelaContainerDdp() override;

    // Abre a janela; devolve false (com `erro`) se o DDP não puder ser lido.
    static bool abrir(ProjetoAberto& projeto, const std::string& nestId, const juce::File& pastaDdp, juce::String& erro);

    bool carregado() const { return player_ != nullptr && player_->carregado(); }
    const juce::String& erro() const { return erro_; }
    int numeroDeArquivosParaTeste() const { return static_cast<int>(arquivos_.size()); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    int getNumRows() override { return static_cast<int>(arquivos_.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selecionada) override;
    void selectedRowsChanged(int ultimaLinha) override;

    struct Arquivo {
        std::string itemId;
        juce::String nome;
    };

    ProjetoAberto& projeto_;
    std::vector<Arquivo> arquivos_;
    juce::ListBox lista_;
    std::unique_ptr<JanelaPlayerDdp> player_;
    std::unique_ptr<PainelMetadadosArquivo> metadados_;
    juce::String erro_;
    juce::Rectangle<int> areaLista_, areaPlayer_, areaMetadados_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JanelaContainerDdp)
};

}  // namespace matriz::ui
