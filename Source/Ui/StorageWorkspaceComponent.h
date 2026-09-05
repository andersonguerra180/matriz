#pragma once

#include <JuceHeader.h>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include "ProjetoAberto.h"
#include "../Vault/DeviceUsageLog.h"
#include "../Vault/SmartHealth.h"

namespace matriz::ui {

class StorageWorkspaceComponent : public juce::Component,
                                  public juce::TableListBoxModel,
                                  private juce::Timer {
public:
    explicit StorageWorkspaceComponent(ProjetoAberto& projeto);
    ~StorageWorkspaceComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void recarregar();

private:
    void timerCallback() override;

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

        // Live space metrics
        juce::int64 espacoTotalBytes = 0;
        juce::int64 espacoLivreBytes = 0;
        juce::int64 espacoUsadoBytes = 0;
        double pctUsado = 0.0;
        double pctLivre = 0.0;
        bool metricasEspacoDisponiveis = false;

        // Integrated SMART Health
        matriz::vault::SmartHealthReport smartReport;

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

    class LogCalendarComponent;
    class ColumnCardsContainer;

    // TableListBoxModel overrides for History table
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent& mouseEvent) override;

    void carregarDados();
    void selecionarDevice(const std::string& vaultId, bool isSourceSelection);
    void atualizarSaudeSmartDoDevice(const std::string& vaultId, bool forcarNovaConsulta = true);
    void selecionarDataCalendario(const juce::String& yyyyMmDd);
    void atualizarListaLogsFiltrada();
    void abrirPastaLogs();
    void abrirRelatorioTxt(const std::string& caminho);

    ProjetoAberto& projeto_;
    bool isCatalog_ = false;

    std::vector<StorageDevice> sourceDevices_;
    std::vector<StorageDevice> backupDevices_;

    std::string selectedVaultId_;
    bool selectedIsSource_ = true;
    std::vector<matriz::vault::DeviceUsageEntry> allDeviceUsageLogs_;
    std::vector<matriz::vault::DeviceUsageEntry> displayedUsageLogs_;
    std::map<juce::String, int> datesWithLogs_;
    juce::String selectedDate_; // "YYYY-MM-DD" or empty for all

    juce::String lastStorageError_;
    juce::String lastStorageErrorDetails_;

    // UI Header
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<juce::Label> lblSubtitle_;
    std::unique_ptr<juce::TextButton> btnRefresh_;

    // Two Columns (Top Area - 80% screen)
    std::unique_ptr<juce::Label> lblSourceColumnTitle_;
    std::unique_ptr<juce::Viewport> sourceCardsViewport_;
    std::unique_ptr<ColumnCardsContainer> sourceCardsContainer_;

    std::unique_ptr<juce::Label> lblBackupColumnTitle_;
    std::unique_ptr<juce::Viewport> backupCardsViewport_;
    std::unique_ptr<ColumnCardsContainer> backupCardsContainer_;

    // Bottom Area (20% screen) - Interactive Log Calendar & Sessions Dock
    std::unique_ptr<juce::Component> logDockContainer_;
    std::unique_ptr<LogCalendarComponent> logCalendarComp_;

    std::unique_ptr<juce::Label> lblDayLogsTitle_;
    std::unique_ptr<juce::TextButton> btnShowAllLogs_;
    std::unique_ptr<juce::TextButton> btnOpenLogFolder_;
    std::unique_ptr<juce::TableListBox> tableHistory_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StorageWorkspaceComponent)
};

} // namespace matriz::ui
