#pragma once

#include <JuceHeader.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include "../Db/Database.h"

namespace matriz::ui {
class ProjetoAberto;
}

namespace matriz::vault {

struct RescanPair {
    juce::String sourceDriveName;
    juce::String backupDriveName;
    std::vector<juce::File> sourceRootPaths;
};

struct RescanFileResult {
    enum class Status {
        Inalterado,
        Modificado,
        Novo
    };

    Status status = Status::Inalterado;
    juce::File file;
    std::string caminhoAbsoluto;
    std::string sha256Calculado;
    std::string sha256Registrado;
    std::string itemIdOriginal;
    std::string tituloBase;
    std::string novoTitulo;
    juce::int64 tamanhoBytes = 0;
    std::string extensao;
    std::string tipoMidia;
};

class RescanEngine {
public:
    // Discovers configured Source Drive ↔ Backup Drive pairs for the open collection
    static std::vector<RescanPair> obterParesDeBackup(ui::ProjetoAberto& projeto);

    // Determines next versioned title: "Bambelô" -> "Bambelô (v2)", "Bambelô (v2)" -> "Bambelô (v3)"
    static std::string calcularProximoTituloComVersao(matriz::db::Database& db, const std::string& tituloBase);

    // Counts total files to scan across the selected pairs
    static int contarTotalArquivos(const std::vector<RescanPair>& pares);

    // Recursively scans a list of files/directories without shortcuts, computing SHA-256 for each
    static bool executarVarredura(
        const std::vector<RescanPair>& pares,
        matriz::db::Database& db,
        std::atomic<bool>& cancelRequested,
        std::function<void(int processedFiles, int totalFiles, const juce::String& currentFileName)> onProgress,
        std::vector<RescanFileResult>& outResultados);
};

} // namespace matriz::vault
