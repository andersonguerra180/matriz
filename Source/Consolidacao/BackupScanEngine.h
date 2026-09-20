#pragma once

// ==============================================================================
// BackupScanEngine
//
// NOTA ARQUITETURAL IMPORTANTE:
// Este componente e estritamente responsavel pela varredura de integridade e
// calculo de hash SHA-256 no DESTINO DE BACKUP ATIVO (classificando arquivos em
// 🔴 Vermelho / 🟢 Verde / ⚪ Orfao).
//
// NAO CONFUNDIR NEM UNIFICAR com "RescanEngine / Rescan Backup Sources", que
// opera na ORIGEM dos arquivos para reingestao e deteccao de novos assets de entrada.
// Qualquer eventual unificacao requer uma decisao de arquitetura separada.
// ==============================================================================

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../Db/Database.h"
#include "Consolidacao.h"

namespace matriz::consolidacao {

enum class StatusArquivoBackup {
    Verde,    // 🟢 Arquivo do project grid tem correspondente com hash identico no destino ativo
    Vermelho, // 🔴 Arquivo do project grid nao tem correspondente com hash igual no destino ativo (novo ou modificado)
    Orfao     // ⚪ Arquivo existe fisicamente no destino ativo mas nao esta no project grid
};

struct ItemStatusBackup {
    std::string itemId;
    std::string arquivoId;
    std::string codigoAcervo;
    std::string titulo;
    juce::String nomeArquivo;
    juce::String caminhoRelativoDestino;
    juce::int64 tamanhoBytes = 0;
    std::string sha256Projeto;
    std::string sha256Destino;
    StatusArquivoBackup status = StatusArquivoBackup::Vermelho;
};

struct ArquivoOrfao {
    juce::String caminhoRelativo;
    juce::File arquivoFisico;
    juce::int64 tamanhoBytes = 0;
    std::string sha256;
};

struct ResultadoScanBackup {
    std::vector<ItemStatusBackup> itensGrid;
    std::vector<ArquivoOrfao> orfaos;
    int totalVerdes = 0;
    int totalVermelhos = 0;
    int totalOrfaos = 0;
    bool cancelado = false;
};

using AoProgredirScan = std::function<bool(const juce::String& faseDescricao, int feito, int total)>;

class BackupScanEngine {
public:
    static ResultadoScanBackup executarScanDestino(
        matriz::db::Database& registro,
        const juce::File& pastaProjeto,
        const juce::File& destinoAtivo,
        const PlanoConsolidacao& plano,
        const AoProgredirScan& aoProgredir = nullptr,
        std::atomic<bool>* cancelamento = nullptr);
};

} // namespace matriz::consolidacao
