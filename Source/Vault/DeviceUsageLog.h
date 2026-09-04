#pragma once

#include <JuceHeader.h>
#include <string>
#include <vector>
#include "SmartHealth.h"

namespace matriz::db {
class Database;
}

namespace matriz::vault {

struct DeviceUsageEntry {
    std::string id;
    std::string vaultId;
    std::string dataDia;            // YYYY-MM-DD
    std::string criadoEm;           // YYYY-MM-DD HH:MM:SS
    std::string acao;               // "INGEST", "BACKUP", "ONLINE_SCAN", "VERIFY"
    std::string operador;           // System user login
    std::string computador;         // Workstation hostname
    std::string sistemaOperacional; // OS name and version
    std::string saudeEstado;        // "HEALTHY", "WARNING", "FAILING", "UNAVAILABLE"
    std::string smartStatus;        // "PASSED", "NOT SUPPORTED", etc.
    int totalArquivos = 0;
    juce::int64 totalBytes = 0;
    juce::StringArray arquivos;     // Processed files
    std::string detalhes;
    std::string relatorioMdCaminho;
};

// Initializes the SQLite table for device usage logs
void inicializarTabelaUsoDispositivo(matriz::db::Database& db);

// Records a device usage session (Ingest, Backup, or Online Scan), persists to SQLite,
// and generates/increments the daily Markdown report in <projectFolder>/log/disk/<diskName>/<diskName>_<YYYY-MM-DD>.md
// and in <backupFolder>/log/disk/<diskName>/ if available.
void registrarUsoDoDispositivo(matriz::db::Database& db,
                              const juce::File& pastaProjeto,
                              const std::string& vaultId,
                              const std::string& acao,
                              int totalArquivos,
                              juce::int64 totalBytes,
                              const juce::StringArray& listaArquivos,
                              const std::string& detalhes = "",
                              const SmartHealthReport* healthOverride = nullptr);

// Retrieves chronological usage timeline records for a specific vault
std::vector<DeviceUsageEntry> listarHistoricoUsoDoDispositivo(matriz::db::Database& db, const std::string& vaultId);

// Resolves the daily markdown report path for a vault on a given date
juce::File obterCaminhoRelatorioMarkdownDispositivo(const juce::File& pastaProjeto,
                                                  const std::string& nomeVault,
                                                  const std::string& dataDia);

} // namespace matriz::vault
