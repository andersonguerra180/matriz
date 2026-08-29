#pragma once

#include <JuceHeader.h>
#include "ProjetoAberto.h"
#include <functional>
#include <vector>

namespace matriz::ui {

class CatalogHubComponent : public juce::Component, public juce::TableListBoxModel {
public:
    explicit CatalogHubComponent(ProjetoAberto& projeto);
    ~CatalogHubComponent() override;

    void recarregar();

    void paint(juce::Graphics& g) override;
    void resized() override;

    // TableListBoxModel implementation
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;

    std::function<void(const juce::File& pastaColecao)> aoAbrirColecao;
    std::function<void()> aoBackupConsolidado;

private:
    enum ColumnIds {
        kColStatus = 1,
        kColNome = 2,
        kColGrupo = 3,
        kColAssets = 4,
        kColTamanho = 5,
        kColCaminho = 6
    };

    ProjetoAberto& projeto_;
    std::vector<ProjetoAberto::ColecaoLink> colecoes_;

    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblSubtitulo_;
    std::unique_ptr<juce::Label> lblResumo_;

    std::unique_ptr<juce::TextButton> btnImportar_;
    std::unique_ptr<juce::TextButton> btnAbrir_;
    std::unique_ptr<juce::TextButton> btnRelocar_;
    std::unique_ptr<juce::TextButton> btnDesvincular_;
    std::unique_ptr<juce::TextButton> btnBackup_;

    std::unique_ptr<juce::TableListBox> tabela_;

    void importarColecaoDialogo();
    void abrirSelecionada();
    void relocarSelecionada();
    void desvincularSelecionada();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CatalogHubComponent)
};

} // namespace matriz::ui
