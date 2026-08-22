#pragma once

#include <JuceHeader.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../Db/Database.h"

// ---------------------------------------------------------------------------
// Camada de Preservação Digital — OAIS / PREMIS / FAIR
//
// Este módulo implementa os mecanismos concretos de preservação sobre o banco
// SQLite existente (registro.sqlite). Não altera nenhum arquivo em disco —
// toda operação sobre arquivos é somente leitura.
//
// Conceitos mapeados:
//   SIP  — entrada/ingest (IngestArquivo.cpp chama garantirPersistentId +
//           registrarEvento INGEST)
//   AIP  — objeto preservado: item + arquivo + preservation_event +
//           preservation_right + fixity (tudo no mesmo banco)
//   DIP  — exportação: exportarJson / exportarCsv
//
// Regras:
//   • NUNCA modifica conteúdo de arquivo em disco.
//   • preservation_event é append-only: nunca apagado ou alterado.
//   • persistent_id coexiste com item.id (UUID técnico); não o substitui.
// ---------------------------------------------------------------------------

namespace matriz::preservation {

// ---------------------------------------------------------------------------
// Agentes
// ---------------------------------------------------------------------------

// Obtém o id do agente pelo nome+versão, criando-o se não existir.
// Idempotente: chamar duas vezes com os mesmos argumentos devolve o mesmo id.
std::string obterOuCriarAgente(db::Database& db,
                                const std::string& nome,
                                const std::string& tipo,   // "software"|"hardware"|"pessoa"|"organizacao"
                                const std::string& versao = "");

// ---------------------------------------------------------------------------
// Persistent Identifier
// ---------------------------------------------------------------------------

// Garante que item.persistent_id está preenchido.
// Formato: "BKR:ASSET:<8 hex chars maiúsculos do UUID>".
// Idempotente: se já existir, devolve o valor atual SEM alterar.
// O item.id (UUID técnico interno) NÃO é substituído.
std::string garantirPersistentId(db::Database& db, const std::string& itemId);

// ---------------------------------------------------------------------------
// Eventos PREMIS
// ---------------------------------------------------------------------------

// Tipos de evento mínimos exigidos pela especificação.
// Usar estas constantes para manter consistência nos registros.
namespace EventType {
    inline constexpr const char* Ingest              = "INGEST";
    inline constexpr const char* FormatIdentified    = "FORMAT_IDENTIFIED";
    inline constexpr const char* MetadataExtracted   = "METADATA_EXTRACTED";
    inline constexpr const char* FixityCalculated    = "FIXITY_CALCULATED";
    inline constexpr const char* FixityCheck         = "FIXITY_CHECK";
    inline constexpr const char* BackupCreated       = "BACKUP_CREATED";
    inline constexpr const char* BackupVerified      = "BACKUP_VERIFIED";
    inline constexpr const char* MetadataUpdated     = "METADATA_UPDATED";
    inline constexpr const char* FileRenamed         = "FILE_RENAMED";
    inline constexpr const char* FileMoved           = "FILE_MOVED";
    inline constexpr const char* RelationshipCreated = "RELATIONSHIP_CREATED";
    inline constexpr const char* ExportCreated       = "EXPORT_CREATED";
    inline constexpr const char* Validation          = "VALIDATION";
    inline constexpr const char* Migration           = "MIGRATION";
} // namespace EventType

namespace Outcome {
    inline constexpr const char* Success = "SUCCESS";
    inline constexpr const char* Failure = "FAILURE";
    inline constexpr const char* Warning = "WARNING";
} // namespace Outcome

// Registra um evento PREMIS. Sempre append — nunca altera eventos existentes.
// arquivoId pode ser vazio quando o evento diz respeito ao item como um todo.
// agentId pode ser vazio quando o agente não é conhecido/aplicável.
// Devolve o id do evento criado.
std::string registrarEvento(db::Database& db,
                             const std::string& itemId,
                             const std::string& arquivoId,
                             const std::string& eventType,
                             const std::string& eventDetail,
                             const std::string& outcome,
                             const std::string& outcomeDetail,
                             const std::string& agentId);

// ---------------------------------------------------------------------------
// Fixity
// ---------------------------------------------------------------------------

struct ResultadoFixity {
    bool        success          = false;
    std::string sha256Calculado;
    std::string sha256Esperado;
    std::string mensagem;
};

// Verifica a integridade de um arquivo:
//   1. Recalcula SHA-256 do arquivo em disco (somente leitura — NUNCA altera)
//   2. Compara com arquivo.checksum_sha256
//   3. Atualiza arquivo.checksum_verificado_em
//   4. Em falha: atualiza arquivo.estado_presenca = 'corrompido'
//   5. Registra evento FIXITY_CHECK com SUCCESS ou FAILURE
// agentId = id do agente que disparou a verificação (ex: BKR Matriz).
ResultadoFixity verificarFixity(db::Database& db,
                                 const std::string& arquivoId,
                                 const std::string& caminhoAbsoluto,
                                 const std::string& agentId);

// ---------------------------------------------------------------------------
// Preservation Status
// ---------------------------------------------------------------------------

struct PreservationStatus {
    std::string identityStatus;    // "OK" | "UNKNOWN"
    std::string fixityStatus;      // "OK" | "WARNING" | "CRITICAL" | "UNKNOWN"
    std::string formatStatus;      // "OK" | "UNKNOWN"
    std::string backupStatus;      // "OK" | "WARNING"
    std::string rightsStatus;      // "OK" | "WARNING" | "UNKNOWN"
    std::string provenanceStatus;  // "OK" | "UNKNOWN"
    std::string overall;           // pior dos anteriores: CRITICAL > WARNING > UNKNOWN > OK
};

// Lê preservation status da view asset_preservation_status.
// Se o item não existir, devolve tudo "UNKNOWN".
PreservationStatus obterStatus(db::Database& db, const std::string& itemId);

// ---------------------------------------------------------------------------
// Event History (para UI)
// ---------------------------------------------------------------------------

struct EventoPreservacao {
    std::string id;
    std::string eventType;
    std::string eventDateTime;
    std::string eventDetail;
    std::string outcome;
    std::string outcomeDetail;
    std::string agentNome;
    std::string agentVersao;
};

// Lista eventos de um item em ordem cronológica inversa (mais recente primeiro).
std::vector<EventoPreservacao> listarEventos(db::Database& db,
                                              const std::string& itemId);

// ---------------------------------------------------------------------------
// Direitos
// ---------------------------------------------------------------------------

struct DireitosPreservacao {
    std::string id;
    std::string rightsStatus;  // "PUBLIC_DOMAIN"|"COPYRIGHT"|"LICENSED"|"UNKNOWN"|"RESTRICTED"
    std::string rightsHolder;
    std::string license;
    std::string usageNotes;
    std::string restrictions;
};

// Lê direitos do item. nullopt se nenhuma linha de direitos existe ainda.
std::optional<DireitosPreservacao> obterDireitos(db::Database& db,
                                                  const std::string& itemId);

// Grava direitos (INSERT OR REPLACE — upsert por item_id).
// Registra evento METADATA_UPDATED se já existia uma linha anterior.
void salvarDireitos(db::Database& db,
                     const std::string& itemId,
                     const DireitosPreservacao& direitos,
                     const std::string& agentId = "");

// ---------------------------------------------------------------------------
// Export (FAIR / DIP)
// ---------------------------------------------------------------------------

// Exporta os metadados completos de um item como JSON (AIP → DIP).
// Formato:
// {
//   "identifier": "BKR:ASSET:8F72A91C",
//   "uuid": "...",
//   "name": "...",
//   "format": "...",
//   "size": 0,
//   "created": "...",
//   "checksum": { "algorithm": "SHA-256", "value": "..." },
//   "provenance": { "origin": "...", "ingested_by": "..." },
//   "rights": { ... },
//   "technical": { ... },
//   "events": [ ... ]
// }
juce::String exportarJson(db::Database& db, const std::string& itemId);

// Exporta vários items como CSV (uma linha de cabeçalho + uma por item).
juce::String exportarCsv(db::Database& db,
                          const std::vector<std::string>& itemIds);

// Exporta CSV com cabeçalhos Dublin Core (dc.identifier, dc.title, ...).
juce::String exportarCsvDublinCore(db::Database& db,
                                    const std::vector<std::string>& itemIds);

// Exporta manifesto de fixity em formato sha256sum/md5sum (.txt legível).
// Cada linha: "hash  caminho_relativo\n" — interoperável com sha256sum -c.
juce::String exportarFixityManifest(db::Database& db,
                                     const std::vector<std::string>& itemIds,
                                     const std::string& algoritmo = "sha256");

// ---------------------------------------------------------------------------
// Format Risk Classification
// ---------------------------------------------------------------------------

// Classifica risco de obsolescência de um formato por extensão.
// "OK" = aberto/lossless (wav, bwf, tiff, pdf, flac, png, txt, csv, xml)
// "AT_RISK" = proprietário/obsoleto (wma, m4p, ra, rm, doc, xls)
// "UNKNOWN" = extensão não reconhecida
std::string classificarRiscoFormato(const std::string& extensao);

// ---------------------------------------------------------------------------
// Compliance Metrics (project-level)
// ---------------------------------------------------------------------------

struct ComplianceRegra321 {
    int totalCopias       = 0;
    int tiposMidiaDistintos = 0;
    bool temOffsite       = false;
    std::string status;  // "OK" | "WARNING" | detalhes
};

ComplianceRegra321 avaliarRegra321(db::Database& db);

struct AlertaRefreshVault {
    std::string vaultNome;
    std::string tipo;
    int anosDesdeUltimoUso = 0;
};

std::vector<AlertaRefreshVault> detectarVaultsParaRefresh(db::Database& db,
                                                           int limiteAnos = 5);

int contarFormatosEmRisco(db::Database& db);

// ---------------------------------------------------------------------------
// Agente BKR (singleton por sessão)
// ---------------------------------------------------------------------------

// Retorna o id do agente "BKR Matriz" (criando-o no banco se necessário).
// Versão é passada pelo caller (normalmente ProjectVersion::string()).
std::string agenteBkr(db::Database& db, const std::string& versao = "");

} // namespace matriz::preservation
