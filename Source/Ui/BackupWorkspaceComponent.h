#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <set>

#include "ProjetoAberto.h"
#include "../Consolidacao/Consolidacao.h"
#include "../Consolidacao/BackupScanEngine.h"
#include "HierarquiaEditorComponent.h"
#include "../App/Cancelamento.h"
#include "OverlayComponent.h"
#include "BackupScanProgressDialog.h"
#include "EventBus.h"

namespace matriz::ui {

class BackupWorkspaceComponent : public juce::Component,
                                 public EventBusListener,
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
    std::function<void()> aoPedirIrParaDuplicatas;
    std::function<void(const std::set<std::string>&)> aoAbrirNoGrid;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void recarregar();
    void aoItemAlterado(const EventoItemAlterado& e) override;

private:
    class PreviaLista;

    // ListBoxModel methods for Vaults list
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int rowNumber, const juce::MouseEvent&) override;
    juce::String getTooltipForRow(int rowNumber) override;

    void atualizarResumo();
    void iniciarBackup();
    void dispararScanDestino(bool forcado = false);
    void executarBackupAcao(bool forcarOverride);
    void iniciarSyncComOutroDestino();
    void carregarDestinoAtivoInicial();

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
    struct OpcaoContent {
        std::string chave;
        juce::String rotulo;
        int contagem = 0;
    };
    std::vector<OpcaoContent> opcoesContent_;
    int selectedContentIdx_ = 0;
    void carregarOpcoesContent();
    std::vector<ProjetoAberto::ColecaoDisponivel> colecoes_;
    int selectedCollectionIdx_ = 0;

    // Target Selection
    struct DestinoBackupItem {
        std::string id;
        juce::String rotulo;
        juce::String caminho;
        std::string papel;
        bool online = false;
        bool ativo = false;
    };
    std::vector<DestinoBackupItem> destinosBackup_;
    int selectedDestinoIdx_ = -1;
    juce::File customDestFolder_;
    juce::File resolvedDestFolder_;

    void carregarDestinosBackup();
    void adicionarOuAtivarDestino(const juce::File& pasta, const juce::String& rotuloSugerido);
    void criarNovoClone(const juce::File& folder);
    void desvincularDestino(const DestinoBackupItem& dest);

    // Scan & Integrity State
    matriz::consolidacao::ResultadoScanBackup scanResult_;
    bool scanRealizado_ = false;

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

    void registrarDestinoBackup(const juce::File& destFolder, const juce::String& rotuloSugerido,
                                int copiado, int pulados, int falhas, bool cancelado);

    // === CONFIG CONTAINER & VIEWPORT ===
    class ConfigContainerComponent;
    std::unique_ptr<juce::Viewport> configViewport_;
    std::unique_ptr<ConfigContainerComponent> configContainer_;
    juce::Rectangle<int> cartaoPrevia_;

    struct CatalogBackupItem {
        juce::String name;
        juce::String path;
        juce::int64 sizeBytes = 0;
        uint64_t totalAssets = 0;
        uint64_t backedUpAssets = 0;
        uint64_t missingAssets = 0;
        uint64_t needsAttention = 0;
        juce::String status = "READY";
    };

    class CatalogBackupContainerComponent;
    std::unique_ptr<juce::Viewport> catalogBackupViewport_;
    std::unique_ptr<CatalogBackupContainerComponent> catalogBackupContainer_;
    std::vector<CatalogBackupItem> catalogBackupItems_;
    CatalogBackupItem catalogBackupTotal_;

    void carregarColecoesBackupCatalogo();

    void abrirJanelaSelecionarArquivos();
    juce::String rotuloOpcaoSelecionados() const;

    // === SOURCE section ===
    std::unique_ptr<juce::Label> labelSource_;
    std::unique_ptr<juce::ComboBox> comboSource_;
    std::unique_ptr<juce::TextButton> btnEditarSelecao_;
    std::unique_ptr<juce::ComboBox> comboColecoes_;

    // === DESTINATION section ===
    std::unique_ptr<juce::Label> labelDest_;
    std::unique_ptr<juce::ListBox> listVaults_;
    std::unique_ptr<juce::TextButton> btnBrowseVault_;
    std::unique_ptr<juce::Button> btnGoogleDriveDest_; // Export to Google Drive (local mount)
    std::unique_ptr<juce::Label> labelDestInfo_;
    bool googleDriveComoDestino_ = false;
    juce::File pastaGoogleDrive_;  // resolved GD mount, if selected

    // Helper: detect Google Drive Desktop mount (macOS)
    static juce::File detectarPastaGoogleDrive();

    // === ORGANIZATION section ===
    std::unique_ptr<juce::Label> labelOrg_;
    std::unique_ptr<juce::ComboBox> comboOrg_;
    std::unique_ptr<juce::TextButton> btnEditarHierarquia_;
    matriz::consolidacao::HierarquiaBackup hierarquiaCustom_;
    std::unique_ptr<juce::ToggleButton> togglePreservarEstrutura_;
    std::unique_ptr<juce::ToggleButton> toggleUsarEstruturaMapa_;
    std::unique_ptr<juce::Label> labelPrefixo_;
    std::unique_ptr<juce::ComboBox> comboModoPrefixo_;
    std::unique_ptr<juce::TextEditor> editPrefixo_;
    matriz::consolidacao::ModoPrefixoArquivo modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Nenhum;
    juce::String prefixoAuto_ = "BKR";
    juce::String prefixoCustomizado_ = "BKR";

    // === OPTIONS section ===
    std::unique_ptr<juce::Label> labelOpcoes_;
    std::unique_ptr<juce::ToggleButton> toggleGerarCatalogo_;
    std::unique_ptr<juce::ToggleButton> toggleEmbutirMetadados_;
    std::unique_ptr<juce::ToggleButton> toggleVerificarChecksum_;
    std::unique_ptr<juce::ToggleButton> toggleAutoResolverConflitos_;
    std::unique_ptr<juce::ToggleButton> toggleForcarRebackup_;

    // === PREVIEW section ===
    class LegendaStatusComponent;
    std::unique_ptr<juce::Label> labelResumo_;
    std::unique_ptr<LegendaStatusComponent> barraLegenda_;
    std::unique_ptr<juce::Viewport> listPreviaViewport_;
    std::unique_ptr<PreviaLista> listPrevia_;

    // === PROGRESS ===
    std::unique_ptr<juce::ProgressBar> barraProgresso_;
    std::unique_ptr<juce::Label> labelProgressoStatus_;

    // === BUTTONS ===
    std::unique_ptr<juce::TextButton> btnStartBackup_;
    std::unique_ptr<juce::TextButton> btnSyncDestino_;
    std::unique_ptr<juce::TextButton> btnPublishHtml_;
    std::unique_ptr<juce::TextButton> btnExportZip_;
    std::unique_ptr<juce::TextButton> btnLimparZip_;
    std::unique_ptr<juce::TextButton> btnSendToPrint_;
    std::unique_ptr<juce::TextButton> btnLimparPrint_;
    std::unique_ptr<juce::TextButton> btnCancelarExecucao_;
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
    void publicarHtml();
    void atualizarBotoesListas();

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

    void mostrarPopupConflitoPreservacao();
    PainelOverlay overlay_;
};

} // namespace matriz::ui
