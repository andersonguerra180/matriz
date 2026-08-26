#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <set>

#include "ProjetoAberto.h"
#include "../Consolidacao/Consolidacao.h"
#include "HierarquiaEditorComponent.h"
#include "../App/Cancelamento.h"

namespace matriz::ui {

class BackupWorkspaceComponent : public juce::Component,
                                 private juce::ListBoxModel {
public:
    enum class Estado {
        Config,
        Running,
        Done
    };

    BackupWorkspaceComponent(ProjetoAberto& projeto, const std::set<std::string>& selectedItemIds);
    ~BackupWorkspaceComponent() override;

    std::function<void()> aoConcluir;
    std::function<void()> aoVoltarHome;
    std::function<void(const juce::File&)> aoAbrirCatalogo;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class PreviaLista;

    // ListBoxModel methods for Vaults list
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int rowNumber, const juce::MouseEvent&) override;

    void atualizarResumo();
    void iniciarBackup();

    ProjetoAberto& projeto_;
    std::set<std::string> selectedItemIds_;

    Estado estado_ = Estado::Config;

    // Selection criteria
    enum class WhatOption {
        Everything,
        Intake,
        SelectedAssets,
        NeedsBackup,
        Collection
    };
    WhatOption whatOption_ = WhatOption::Everything;
    std::vector<ProjetoAberto::ColecaoEmbutida> colecoes_;
    int selectedCollectionIdx_ = 0;

    // Target Selection
    std::vector<ProjetoAberto::VaultResumo> vaults_;
    int selectedVaultIdx_ = -1;
    juce::File customDestFolder_;
    juce::File resolvedDestFolder_;

    // Plan & Execution
    matriz::consolidacao::PlanoConsolidacao plano_;
    matriz::app::CancelamentoPtr cancelamento_ = std::make_shared<matriz::app::Cancelamento>();
    bool executando_ = false;
    double progressoValor_ = 0.0;

    // Done stats
    int copiadoCount_ = 0;
    int verificadoCount_ = 0;
    int falhasCount_ = 0;
    std::vector<juce::String> falhasLista_;

    // UI elements — ALL visible at once in Config state
    std::unique_ptr<juce::Label> labelTitulo_;

    // === SOURCE section ===
    std::unique_ptr<juce::Label> labelSource_;
    std::unique_ptr<juce::ComboBox> comboSource_;
    std::unique_ptr<juce::ComboBox> comboColecoes_;

    // === DESTINATION section ===
    std::unique_ptr<juce::Label> labelDest_;
    std::unique_ptr<juce::ListBox> listVaults_;
    std::unique_ptr<juce::TextButton> btnBrowseVault_;
    std::unique_ptr<juce::Label> labelDestInfo_;

    // === ORGANIZATION section ===
    std::unique_ptr<juce::Label> labelOrg_;
    std::unique_ptr<juce::ComboBox> comboOrg_;
    std::unique_ptr<juce::TextButton> btnEditarHierarquia_;
    matriz::consolidacao::HierarquiaBackup hierarquiaCustom_;
    std::unique_ptr<juce::ToggleButton> togglePreservarEstrutura_;

    // === OPTIONS section ===
    std::unique_ptr<juce::Label> labelOpcoes_;
    std::unique_ptr<juce::ToggleButton> toggleGerarCatalogo_;
    std::unique_ptr<juce::ToggleButton> toggleEmbutirMetadados_;
    std::unique_ptr<juce::ToggleButton> toggleVerificarChecksum_;

    // === PREVIEW section ===
    std::unique_ptr<juce::Label> labelResumo_;
    std::unique_ptr<juce::Viewport> listPreviaViewport_;
    std::unique_ptr<PreviaLista> listPrevia_;

    // === PROGRESS ===
    std::unique_ptr<juce::ProgressBar> barraProgresso_;
    std::unique_ptr<juce::Label> labelProgressoStatus_;

    // === BUTTONS ===
    std::unique_ptr<juce::TextButton> btnStartBackup_;
    std::unique_ptr<juce::TextButton> btnCancel_;
    std::unique_ptr<juce::TextButton> btnDone_;
    std::unique_ptr<juce::TextButton> btnOpenCatalog_;
    std::unique_ptr<juce::TextButton> btnExportXls_;
    std::unique_ptr<juce::TextButton> btnExportCsv_;
    std::unique_ptr<juce::TextButton> btnExportDublinCore_;
    std::unique_ptr<juce::TextButton> btnExportChecksums_;
    std::unique_ptr<juce::TextButton> btnExportJanela_;

    void mostrarJanelaExportar();
    void exportarXls();
    void exportarCsv();
    void exportarDublinCore();
    void exportarChecksums();

    // Auto-export to a specific folder (no FileChooser dialog)
    void exportarCsvPara(const juce::File& destFolder);
    void exportarXlsPara(const juce::File& destFolder);
    void exportarDublinCorePara(const juce::File& destFolder);
    void exportarChecksumsPara(const juce::File& destFolder);
    juce::String gerarManifestChecksumsBackup(const std::function<void(int, int)>& onProgress = nullptr);

    // Helpers
    std::set<std::string> obterItensSelecionadosPeloCriterio();
    void aplicarEstiloBotao(juce::TextButton& botao, bool primario);

    // Config e progresso são telas diferentes no mesmo Component. Sem esconder
    // uma ao mostrar a outra, os controles antigos continuam com os bounds da
    // passada anterior e a barra de progresso é desenhada por cima deles.
    void mostrarControlesConfig(bool mostrar);

    // Retângulos calculados em resized() e pintados em paint(): os cartões que
    // agrupam cada seção. Guardados para as duas funções não recalcularem
    // geometria em duplicata e sairem de sincronia.
    std::vector<juce::Rectangle<int>> cartoes_;
    juce::Rectangle<int> cartaoCentral_;
    juce::Rectangle<int> faixaCabecalho_;
    juce::Rectangle<int> faixaRodape_;
};

} // namespace matriz::ui
