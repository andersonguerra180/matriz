#pragma once

#include <JuceHeader.h>
#include <string>
#include <vector>
#include <memory>
#include "ProjetoAberto.h"
#include "../Vault/DeviceUsageLog.h"

namespace matriz::ui {

class StorageWorkspaceComponent : public juce::Component,
                                  public juce::TableListBoxModel,
                                  private juce::Timer {
public:
    explicit StorageWorkspaceComponent(ProjetoAberto& projeto);
    ~StorageWorkspaceComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void recarregar();

private:
    void timerCallback() override;

    struct PropRow {
        juce::String label;
        juce::String value;
    };

    struct StorageDevice {
        std::string id;
        std::string projetoId;
        juce::String nome;
        std::string tipo;
        juce::String localizacao;
        std::string uuidVolume;
        std::string vendor;
        std::string modelo;
        std::string numeroSerie;
        juce::int64 capacidadeBytes = 0;
        bool removivel = false;
        std::string sistemaArquivos;
        std::string categoriaDispositivo;
        bool categoriaManual = false;
        std::string status;
        std::string criadoEm;
        std::string vistoEm;

        bool online = false;
        bool isSource = false;
        bool isBackup = false;

        // Ingest stats (Source)
        int totalArquivos = 0;
        juce::int64 totalBytes = 0;
        std::string ultimoIngest;

        // Backup stats (Backup)
        int totalBackups = 0;
        int totalItensCopiados = 0;
        int totalItensFalha = 0;
        std::string ultimoBackup;
    };

private:
    // TableListBoxModel overrides for History table
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent& mouseEvent) override;

    void carregarDados();
    void selecionarDevice(const std::string& vaultId, bool isSourceSelection);
    void salvarNomeVault();
    void salvarCategoriaVault(const std::string& novaCategoria);
    void abrirPastaLogs();
    void abrirRelatorioMd(const std::string& caminho);

    ProjetoAberto& projeto_;
    bool isCatalog_ = false;

    std::vector<StorageDevice> sourceDevices_;
    std::vector<StorageDevice> backupDevices_;

    std::string selectedVaultId_;
    bool selectedIsSource_ = true;
    std::vector<matriz::vault::DeviceUsageEntry> selectedUsageLogs_;

    juce::String lastStorageError_;
    juce::String lastStorageErrorDetails_;

    // UI Header
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<juce::Label> lblSubtitle_;
    std::unique_ptr<juce::TextButton> btnRefresh_;

    // Two Columns (Top Area)
    class ColumnCardsContainer;
    std::unique_ptr<juce::Label> lblSourceColumnTitle_;
    std::unique_ptr<juce::Viewport> sourceCardsViewport_;
    std::unique_ptr<ColumnCardsContainer> sourceCardsContainer_;

    std::unique_ptr<juce::Label> lblBackupColumnTitle_;
    std::unique_ptr<juce::Viewport> backupCardsViewport_;
    std::unique_ptr<ColumnCardsContainer> backupCardsContainer_;

    // Inspector & History Panel (Bottom Area)
    std::unique_ptr<juce::Component> inspectorContainer_;
    std::unique_ptr<juce::Label> lblInspectorTitle_;
    std::unique_ptr<juce::Label> lblNickName_;
    std::unique_ptr<juce::TextEditor> txtNickName_;
    std::unique_ptr<juce::TextButton> btnSaveNickName_;
    std::unique_ptr<juce::Label> lblCategory_;
    std::unique_ptr<juce::ComboBox> comboCategory_;

    // Hardware specifications component
    class HardwarePropsComponent;
    std::unique_ptr<juce::Label> lblHardwareTitle_;
    std::unique_ptr<HardwarePropsComponent> hardwarePropsComp_;

    // Drive Health component
    class DriveHealthComponent;
    std::unique_ptr<juce::Label> lblHealthTitle_;
    std::unique_ptr<juce::TextButton> btnRefreshHealth_;
    std::unique_ptr<DriveHealthComponent> driveHealthComp_;

    void atualizarSaudeSmart(bool forcarNovaConsulta = false);

    // History Log Table
    std::unique_ptr<juce::Label> lblHistoryTitle_;
    std::unique_ptr<juce::TextButton> btnOpenLogFolder_;
    std::unique_ptr<juce::TableListBox> tableHistory_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StorageWorkspaceComponent)
};

} // namespace matriz::ui
