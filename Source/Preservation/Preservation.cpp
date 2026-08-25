#include "Preservation.h"

#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>

#ifdef __APPLE__
#  include <CommonCrypto/CommonDigest.h>
#  include <fstream>
#  include <iomanip>
#endif

#include "../Model/Project.h"   // novoUuid(), agoraIso8601()
#include "../Ingest/LeituraTecnica.h" // categoriaPorExtensao

namespace matriz::preservation {

using matriz::db::Value;
using matriz::db::Database;
using matriz::model::novoUuid;
using matriz::model::agoraIso8601;

// ---------------------------------------------------------------------------
// Helpers internos
// ---------------------------------------------------------------------------

namespace {

// Recalcula SHA-256 de um arquivo em disco. Somente leitura.
// Devolve string hex em minúsculas, ou vazio se o arquivo não puder ser lido.
std::string calcularSha256(const std::string& caminho) {
#ifdef __APPLE__
    std::ifstream file(caminho, std::ios::binary);
    if (!file.is_open()) return {};

    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);

    constexpr size_t kBuf = 65536;
    char buf[kBuf];
    while (file.read(buf, kBuf) || file.gcount() > 0)
        CC_SHA256_Update(&ctx, buf, static_cast<CC_LONG>(file.gcount()));

    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i)
        ss << std::setw(2) << static_cast<int>(digest[i]);
    return ss.str();
#else
    juce::File f(juce::String::fromUTF8(caminho.c_str()));
    if (!f.existsAsFile()) return {};
    return juce::SHA256(f).toHexString().toLowerCase().toStdString();
#endif
}

// Calcula o overall: pior dos status individuais
// CRITICAL > WARNING > UNKNOWN > OK
std::string calcularOverall(const PreservationStatus& s) {
    const auto& v = {s.identityStatus, s.fixityStatus, s.formatStatus,
                     s.backupStatus, s.rightsStatus, s.provenanceStatus};
    if (std::any_of(v.begin(), v.end(), [](const std::string& x){ return x == "CRITICAL"; }))
        return "CRITICAL";
    if (std::any_of(v.begin(), v.end(), [](const std::string& x){ return x == "WARNING"; }))
        return "WARNING";
    if (std::any_of(v.begin(), v.end(), [](const std::string& x){ return x == "UNKNOWN"; }))
        return "UNKNOWN";
    return "OK";
}

} // namespace

// ---------------------------------------------------------------------------
// Agentes
// ---------------------------------------------------------------------------

std::string obterOuCriarAgente(db::Database& db,
                                const std::string& nome,
                                const std::string& tipo,
                                const std::string& versao)
{
    // Tenta encontrar pelo nome+versão (o índice único garante unicidade)
    {
        auto stmt = db.prepare(
            "SELECT id FROM preservation_agent "
            "WHERE nome = ? AND IFNULL(versao,'') = ? LIMIT 1");
        stmt.bind(1, Value::of(nome));
        stmt.bind(2, Value::of(versao));
        if (stmt.step()) return stmt.columnText(0);
    }

    std::string id  = novoUuid();
    std::string now = agoraIso8601();
    try {
        db.run(
            "INSERT OR IGNORE INTO preservation_agent "
            "(id, nome, tipo, versao, criado_em) VALUES (?,?,?,?,?)",
            { Value::of(id), Value::of(nome), Value::of(tipo),
              versao.empty() ? Value::null() : Value::of(versao),
              Value::of(now) });
    } catch (...) {}

    // Pode ter havido race no INSERT OR IGNORE — reler para garantir
    auto stmt2 = db.prepare(
        "SELECT id FROM preservation_agent "
        "WHERE nome = ? AND IFNULL(versao,'') = ? LIMIT 1");
    stmt2.bind(1, Value::of(nome));
    stmt2.bind(2, Value::of(versao));
    if (stmt2.step()) return stmt2.columnText(0);
    return id;
}

std::string agenteBkr(db::Database& db, const std::string& versao)
{
    return obterOuCriarAgente(db, "BKR Matriz", "software", versao);
}

// ---------------------------------------------------------------------------
// Persistent Identifier
// ---------------------------------------------------------------------------

std::string garantirPersistentId(db::Database& db, const std::string& itemId)
{
    // Já existe? Devolve sem alterar.
    {
        auto stmt = db.prepare(
            "SELECT persistent_id FROM item WHERE id = ? LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            std::string pid = stmt.columnText(0);
            if (!pid.empty()) return pid;
        }
    }

    // Deriva de maneira determinística do UUID para ser reproduzível
    // em caso de recriação: primeiros 8 hex chars do UUID sem hífens.
    std::string hexPart = itemId;
    // Remove hífens
    hexPart.erase(std::remove(hexPart.begin(), hexPart.end(), '-'), hexPart.end());
    // Pega os 8 primeiros e converte para maiúsculas
    if (hexPart.size() > 8) hexPart = hexPart.substr(0, 8);
    for (auto& c : hexPart) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    std::string pid = "BKR:ASSET:" + hexPart;
    try {
        db.run("UPDATE item SET persistent_id = ? WHERE id = ?",
               { Value::of(pid), Value::of(itemId) });
    } catch (...) {}
    return pid;
}

// ---------------------------------------------------------------------------
// Eventos PREMIS
// ---------------------------------------------------------------------------

std::string registrarEvento(db::Database& db,
                             const std::string& itemId,
                             const std::string& arquivoId,
                             const std::string& eventType,
                             const std::string& eventDetail,
                             const std::string& outcome,
                             const std::string& outcomeDetail,
                             const std::string& agentId)
{
    std::string id  = novoUuid();
    std::string now = agoraIso8601();
    try {
        db.run(
            "INSERT INTO preservation_event "
            "(id, item_id, arquivo_id, event_type, event_date_time, "
            " event_detail, event_outcome, event_outcome_detail, agent_id, criado_em) "
            "VALUES (?,?,?,?,?,?,?,?,?,?)",
            { Value::of(id),
              Value::of(itemId),
              arquivoId.empty() ? Value::null() : Value::of(arquivoId),
              Value::of(eventType),
              Value::of(now),
              eventDetail.empty() ? Value::null() : Value::of(eventDetail),
              Value::of(outcome),
              outcomeDetail.empty() ? Value::null() : Value::of(outcomeDetail),
              agentId.empty() ? Value::null() : Value::of(agentId),
              Value::of(now) });
    } catch (...) {}
    return id;
}

// ---------------------------------------------------------------------------
// Fixity
// ---------------------------------------------------------------------------

ResultadoFixity verificarFixity(db::Database& db,
                                 const std::string& arquivoId,
                                 const std::string& caminhoAbsoluto,
                                 const std::string& agentId)
{
    ResultadoFixity r;

    // Lê itemId e hash esperado do banco
    std::string itemId, sha256Esperado;
    {
        auto stmt = db.prepare(
            "SELECT item_id, IFNULL(checksum_sha256,'') FROM arquivo WHERE id = ? LIMIT 1");
        stmt.bind(1, Value::of(arquivoId));
        if (!stmt.step()) {
            r.mensagem = "arquivo nao encontrado no banco: " + arquivoId;
            return r;
        }
        itemId       = stmt.columnText(0);
        sha256Esperado = stmt.columnText(1);
    }

    if (sha256Esperado.empty()) {
        r.mensagem = "sem checksum registrado — fixity nao pode ser verificada";
        registrarEvento(db, itemId, arquivoId,
                        EventType::FixityCheck,
                        "No baseline checksum to compare against",
                        Outcome::Warning, "FIXITY_CALCULATED event may be missing",
                        agentId);
        return r;
    }

    // Recalcular SHA-256 — somente leitura
    std::string sha256Calculado = calcularSha256(caminhoAbsoluto);
    if (sha256Calculado.empty()) {
        r.mensagem = "nao foi possivel ler o arquivo: " + caminhoAbsoluto;
        registrarEvento(db, itemId, arquivoId,
                        EventType::FixityCheck,
                        "File could not be read: " + caminhoAbsoluto,
                        Outcome::Failure, "File may be missing or inaccessible",
                        agentId);
        return r;
    }

    r.sha256Calculado = sha256Calculado;
    r.sha256Esperado  = sha256Esperado;
    r.success         = (sha256Calculado == sha256Esperado);

    std::string now = agoraIso8601();

    // Atualiza data de verificação (independente do resultado)
    try {
        db.run("UPDATE arquivo SET checksum_verificado_em = ? WHERE id = ?",
               { Value::of(now), Value::of(arquivoId) });
    } catch (...) {}

    if (r.success) {
        r.mensagem = "SHA-256 matches: " + sha256Calculado;
        registrarEvento(db, itemId, arquivoId,
                        EventType::FixityCheck,
                        "SHA-256 verified: " + sha256Calculado,
                        Outcome::Success, {},
                        agentId);
    } else {
        r.mensagem = "INTEGRITY FAILURE — expected: " + sha256Esperado +
                     " | calculated: " + sha256Calculado;
        // Marca arquivo como corrompido
        try {
            db.run("UPDATE arquivo SET estado_presenca = 'corrompido' WHERE id = ?",
                   { Value::of(arquivoId) });
        } catch (...) {}
        registrarEvento(db, itemId, arquivoId,
                        EventType::FixityCheck,
                        "SHA-256 mismatch: expected " + sha256Esperado +
                        " | got " + sha256Calculado,
                        Outcome::Failure,
                        "File may be corrupt or have been modified outside BKR",
                        agentId);
    }
    return r;
}

// ---------------------------------------------------------------------------
// Preservation Status
// ---------------------------------------------------------------------------

PreservationStatus obterStatus(db::Database& db, const std::string& itemId)
{
    PreservationStatus s;
    s.identityStatus    = "UNKNOWN";
    s.fixityStatus      = "UNKNOWN";
    s.formatStatus      = "UNKNOWN";
    s.backupStatus      = "WARNING";
    s.rightsStatus      = "UNKNOWN";
    s.provenanceStatus  = "UNKNOWN";
    s.overall           = "UNKNOWN";

    try {
        auto stmt = db.prepare(
            "SELECT identity_status, fixity_status, format_status, "
            "       backup_status, rights_status, provenance_status "
            "FROM asset_preservation_status WHERE item_id = ? LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            s.identityStatus   = stmt.columnText(0);
            s.fixityStatus     = stmt.columnText(1);
            s.formatStatus     = stmt.columnText(2);
            s.backupStatus     = stmt.columnText(3);
            s.rightsStatus     = stmt.columnText(4);
            s.provenanceStatus = stmt.columnText(5);
            s.overall          = calcularOverall(s);
        }
    } catch (...) {}
    return s;
}

// ---------------------------------------------------------------------------
// Event History
// ---------------------------------------------------------------------------

std::vector<EventoPreservacao> listarEventos(db::Database& db,
                                              const std::string& itemId)
{
    std::vector<EventoPreservacao> eventos;
    try {
        auto stmt = db.prepare(
            "SELECT pe.id, pe.event_type, pe.event_date_time, "
            "       IFNULL(pe.event_detail,''), pe.event_outcome, "
            "       IFNULL(pe.event_outcome_detail,''), "
            "       IFNULL(pa.nome,''), IFNULL(pa.versao,'') "
            "FROM preservation_event pe "
            "LEFT JOIN preservation_agent pa ON pa.id = pe.agent_id "
            "WHERE pe.item_id = ? "
            "ORDER BY pe.event_date_time DESC, pe.criado_em DESC");
        stmt.bind(1, Value::of(itemId));
        while (stmt.step()) {
            EventoPreservacao e;
            e.id            = stmt.columnText(0);
            e.eventType     = stmt.columnText(1);
            e.eventDateTime = stmt.columnText(2);
            e.eventDetail   = stmt.columnText(3);
            e.outcome       = stmt.columnText(4);
            e.outcomeDetail = stmt.columnText(5);
            e.agentNome     = stmt.columnText(6);
            e.agentVersao   = stmt.columnText(7);
            eventos.push_back(std::move(e));
        }
    } catch (...) {}
    return eventos;
}

// ---------------------------------------------------------------------------
// Direitos
// ---------------------------------------------------------------------------

std::optional<DireitosPreservacao> obterDireitos(db::Database& db,
                                                  const std::string& itemId)
{
    try {
        auto stmt = db.prepare(
            "SELECT id, rights_status, IFNULL(rights_holder,''), "
            "       IFNULL(license,''), IFNULL(usage_notes,''), IFNULL(restrictions,'') "
            "FROM preservation_right WHERE item_id = ? LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            DireitosPreservacao d;
            d.id           = stmt.columnText(0);
            d.rightsStatus = stmt.columnText(1);
            d.rightsHolder = stmt.columnText(2);
            d.license      = stmt.columnText(3);
            d.usageNotes   = stmt.columnText(4);
            d.restrictions = stmt.columnText(5);
            return d;
        }
    } catch (...) {}
    return std::nullopt;
}

void salvarDireitos(db::Database& db,
                     const std::string& itemId,
                     const DireitosPreservacao& direitos,
                     const std::string& agentId)
{
    // Lê o estado anterior para registrar evento de mudança
    auto anterior = obterDireitos(db, itemId);
    std::string now = agoraIso8601();

    try {
        if (anterior) {
            // Atualiza linha existente
            db.run(
                "UPDATE preservation_right SET "
                "  rights_status = ?, rights_holder = ?, license = ?, "
                "  usage_notes = ?, restrictions = ?, atualizado_em = ? "
                "WHERE item_id = ?",
                { Value::of(direitos.rightsStatus),
                  direitos.rightsHolder.empty() ? Value::null() : Value::of(direitos.rightsHolder),
                  direitos.license.empty()      ? Value::null() : Value::of(direitos.license),
                  direitos.usageNotes.empty()   ? Value::null() : Value::of(direitos.usageNotes),
                  direitos.restrictions.empty() ? Value::null() : Value::of(direitos.restrictions),
                  Value::of(now),
                  Value::of(itemId) });

            // Evento de atualização se algo mudou
            std::string detail = "rights_status: " + anterior->rightsStatus +
                                 " -> " + direitos.rightsStatus;
            registrarEvento(db, itemId, {},
                            EventType::MetadataUpdated,
                            detail, Outcome::Success, {}, agentId);
        } else {
            // Insere nova linha
            db.run(
                "INSERT INTO preservation_right "
                "(id, item_id, rights_status, rights_holder, license, "
                " usage_notes, restrictions, criado_em, atualizado_em) "
                "VALUES (?,?,?,?,?,?,?,?,?)",
                { Value::of(novoUuid()),
                  Value::of(itemId),
                  Value::of(direitos.rightsStatus),
                  direitos.rightsHolder.empty() ? Value::null() : Value::of(direitos.rightsHolder),
                  direitos.license.empty()      ? Value::null() : Value::of(direitos.license),
                  direitos.usageNotes.empty()   ? Value::null() : Value::of(direitos.usageNotes),
                  direitos.restrictions.empty() ? Value::null() : Value::of(direitos.restrictions),
                  Value::of(now), Value::of(now) });

            registrarEvento(db, itemId, {},
                            EventType::MetadataUpdated,
                            "Rights record created: " + direitos.rightsStatus,
                            Outcome::Success, {}, agentId);
        }
    } catch (...) {}
}

// ---------------------------------------------------------------------------
// Export JSON / CSV
// ---------------------------------------------------------------------------

juce::String exportarJson(db::Database& db, const std::string& itemId)
{
    auto root = std::make_unique<juce::DynamicObject>();

    // Identifiers
    try {
        auto stmt = db.prepare(
            "SELECT titulo, tipo_midia, IFNULL(persistent_id,''), criado_em, atualizado_em "
            "FROM item WHERE id = ? LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            root->setProperty("identifier", juce::String(stmt.columnText(2)));
            root->setProperty("uuid",       juce::String(itemId));
            root->setProperty("name",       juce::String(stmt.columnText(0)));
            root->setProperty("format",     juce::String(stmt.columnText(1)));
            root->setProperty("created",    juce::String(stmt.columnText(3)));
            root->setProperty("modified",   juce::String(stmt.columnText(4)));
        }
    } catch (...) {}

    // Fixity (arquivo master)
    try {
        auto stmt = db.prepare(
            "SELECT IFNULL(checksum_sha256,''), tamanho_bytes, "
            "       IFNULL(checksum_gerado_em,''), IFNULL(checksum_verificado_em,'') "
            "FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            auto checksumObj = std::make_unique<juce::DynamicObject>();
            checksumObj->setProperty("algorithm", "SHA-256");
            checksumObj->setProperty("value",     juce::String(stmt.columnText(0)));
            checksumObj->setProperty("calculated_at", juce::String(stmt.columnText(2)));
            checksumObj->setProperty("verified_at",   juce::String(stmt.columnText(3)));
            root->setProperty("checksum",   juce::var(checksumObj.release()));
            root->setProperty("size_bytes", static_cast<juce::int64>(stmt.columnInt(1)));
        }
    } catch (...) {}

    // Provenance
    try {
        auto stmt = db.prepare(
            "SELECT IFNULL(caminho_absoluto_origem,''), criado_em "
            "FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            auto provObj = std::make_unique<juce::DynamicObject>();
            provObj->setProperty("original_path", juce::String(stmt.columnText(0)));
            provObj->setProperty("ingested_at",   juce::String(stmt.columnText(1)));
            root->setProperty("provenance", juce::var(provObj.release()));
        }
    } catch (...) {}

    // Rights
    auto direitos = obterDireitos(db, itemId);
    if (direitos) {
        auto rObj = std::make_unique<juce::DynamicObject>();
        rObj->setProperty("status",       juce::String(direitos->rightsStatus));
        rObj->setProperty("holder",       juce::String(direitos->rightsHolder));
        rObj->setProperty("license",      juce::String(direitos->license));
        rObj->setProperty("usage_notes",  juce::String(direitos->usageNotes));
        rObj->setProperty("restrictions", juce::String(direitos->restrictions));
        root->setProperty("rights", juce::var(rObj.release()));
    } else {
        auto rObj = std::make_unique<juce::DynamicObject>();
        rObj->setProperty("status", "UNKNOWN");
        root->setProperty("rights", juce::var(rObj.release()));
    }

    // Technical metadata
    try {
        auto stmt = db.prepare(
            "SELECT caracteristicas_tecnicas_json "
            "FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (stmt.step()) {
            juce::var tech = juce::JSON::parse(juce::String(stmt.columnText(0)));
            root->setProperty("technical", tech.isVoid() ? juce::var(new juce::DynamicObject()) : tech);
        }
    } catch (...) {}

    // Events
    auto eventos = listarEventos(db, itemId);
    juce::Array<juce::var> evArray;
    for (const auto& ev : eventos) {
        auto eObj = std::make_unique<juce::DynamicObject>();
        eObj->setProperty("type",           juce::String(ev.eventType));
        eObj->setProperty("date_time",      juce::String(ev.eventDateTime));
        eObj->setProperty("detail",         juce::String(ev.eventDetail));
        eObj->setProperty("outcome",        juce::String(ev.outcome));
        eObj->setProperty("outcome_detail", juce::String(ev.outcomeDetail));
        juce::String agentStr = juce::String(ev.agentNome);
        if (!ev.agentVersao.empty()) agentStr += " " + juce::String(ev.agentVersao);
        eObj->setProperty("agent", agentStr);
        evArray.add(juce::var(eObj.release()));
    }
    root->setProperty("events", juce::var(evArray));

    return juce::JSON::toString(juce::var(root.release()), true);
}

juce::String exportarCsv(db::Database& db, const std::vector<std::string>& itemIds)
{
    juce::String csv;
    csv += "persistent_id,uuid,name,format,size_bytes,sha256,fixity_status,"
           "backup_status,rights_status,created_at\n";

    for (const auto& id : itemIds) {
        try {
            auto stmt = db.prepare(
                "SELECT IFNULL(i.persistent_id,''), i.id, i.titulo, IFNULL(i.tipo_midia,''), "
                "       IFNULL(a.tamanho_bytes,0), IFNULL(a.checksum_sha256,''), i.criado_em "
                "FROM item i "
                "LEFT JOIN arquivo a ON a.item_id = i.id AND a.eh_master = 1 "
                "WHERE i.id = ? LIMIT 1");
            stmt.bind(1, Value::of(id));
            if (!stmt.step()) continue;

            auto status = obterStatus(db, id);

            auto esc = [](const juce::String& s) -> juce::String {
                if (s.containsAnyOf(",\"\n\r"))
                    return "\"" + s.replace("\"", "\"\"") + "\"";
                return s;
            };

            csv += esc(juce::String(stmt.columnText(0))) + ","
                 + esc(juce::String(stmt.columnText(1))) + ","
                 + esc(juce::String(stmt.columnText(2))) + ","
                 + esc(juce::String(stmt.columnText(3))) + ","
                 + juce::String(stmt.columnInt(4))        + ","
                 + esc(juce::String(stmt.columnText(5))) + ","
                 + esc(juce::String(status.fixityStatus))  + ","
                 + esc(juce::String(status.backupStatus))  + ","
                 + esc(juce::String(status.rightsStatus))  + ","
                 + esc(juce::String(stmt.columnText(6)))  + "\n";

        } catch (...) {}
    }
    return csv;
}

namespace {

juce::String gerarFullCsvSchemaJson() {
    return R"schema({
  "schema": "BKR_FULL_CSV",
  "version": "1.0",
  "encoding": "UTF-8",
  "delimiter": ",",
  "field_count": 49,
  "fields": [
    {"name": "asset_id", "type": "UUID", "required": true, "description": "Unique UUID of the asset", "namespace": "Identity"},
    {"name": "persistent_id", "type": "TEXT", "required": false, "description": "Persistent ID when available", "namespace": "Identity"},
    {"name": "catalog_code", "type": "TEXT", "required": false, "description": "BKR catalog code", "namespace": "Identity"},
    {"name": "file_title", "type": "TEXT", "required": true, "description": "Filename without extension", "namespace": "File"},
    {"name": "file_media_type", "type": "TEXT", "required": true, "description": "Media category (Audio, Video, Image, Document, Text)", "namespace": "File"},
    {"name": "file_path", "type": "TEXT", "required": false, "description": "Absolute file path", "namespace": "File"},
    {"name": "file_size_bytes", "type": "INTEGER", "required": false, "description": "File size in bytes", "namespace": "File"},
    {"name": "file_sha256", "type": "TEXT", "required": false, "description": "SHA-256 checksum string", "namespace": "File"},
    {"name": "file_created_at", "type": "DATETIME", "required": false, "description": "File creation date ISO 8601", "namespace": "File"},
    {"name": "file_modified_at", "type": "DATETIME", "required": false, "description": "File modification date ISO 8601", "namespace": "File"},
    {"name": "technical_year", "type": "INTEGER", "required": false, "description": "Technical creation year", "namespace": "Technical"},
    {"name": "technical_duration_seconds", "type": "REAL", "required": false, "description": "Duration in seconds (decimal)", "namespace": "Technical"},
    {"name": "technical_format", "type": "TEXT", "required": false, "description": "Technical container/format", "namespace": "Technical"},
    {"name": "technical_codec", "type": "TEXT", "required": false, "description": "Technical codec name", "namespace": "Technical"},
    {"name": "technical_sample_rate", "type": "INTEGER", "required": false, "description": "Sample rate in Hz", "namespace": "Technical"},
    {"name": "technical_bit_depth", "type": "INTEGER", "required": false, "description": "Bit depth", "namespace": "Technical"},
    {"name": "catalog_content_type", "type": "TEXT", "required": false, "description": "Catalog content type", "namespace": "Catalog"},
    {"name": "catalog_source_media", "type": "TEXT", "required": false, "description": "Catalog source media", "namespace": "Catalog"},
    {"name": "catalog_collection_type", "type": "TEXT", "required": false, "description": "Catalog collection type", "namespace": "Catalog"},
    {"name": "catalog_isrc", "type": "TEXT", "required": false, "description": "Catalog ISRC code", "namespace": "Catalog"},
    {"name": "catalog_notes", "type": "TEXT", "required": false, "description": "Free notes text", "namespace": "Catalog"},
    {"name": "catalog_tags", "type": "JSON", "required": false, "description": "Tags JSON array string", "namespace": "Catalog"},
    {"name": "geo_latitude", "type": "REAL", "required": false, "description": "Geolocation latitude decimal", "namespace": "Geolocation"},
    {"name": "geo_longitude", "type": "REAL", "required": false, "description": "Geolocation longitude decimal", "namespace": "Geolocation"},
    {"name": "geo_altitude", "type": "REAL", "required": false, "description": "Geolocation altitude decimal", "namespace": "Geolocation"},
    {"name": "geo_formatted_address", "type": "TEXT", "required": false, "description": "Formatted address", "namespace": "Geolocation"},
    {"name": "geo_city", "type": "TEXT", "required": false, "description": "City name", "namespace": "Geolocation"},
    {"name": "geo_state_province", "type": "TEXT", "required": false, "description": "State or province", "namespace": "Geolocation"},
    {"name": "geo_country", "type": "TEXT", "required": false, "description": "Country name", "namespace": "Geolocation"},
    {"name": "geo_country_code", "type": "TEXT", "required": false, "description": "Country ISO code", "namespace": "Geolocation"},
    {"name": "geo_postal_code", "type": "TEXT", "required": false, "description": "Postal code", "namespace": "Geolocation"},
    {"name": "geo_source", "type": "TEXT", "required": false, "description": "Geolocation source", "namespace": "Geolocation"},
    {"name": "geo_accuracy_meters", "type": "REAL", "required": false, "description": "Accuracy in meters decimal", "namespace": "Geolocation"},
    {"name": "dc_title", "type": "TEXT", "required": false, "description": "Dublin Core Title", "namespace": "DublinCore"},
    {"name": "dc_creator", "type": "TEXT", "required": false, "description": "Dublin Core Creator", "namespace": "DublinCore"},
    {"name": "dc_subject", "type": "TEXT", "required": false, "description": "Dublin Core Subject", "namespace": "DublinCore"},
    {"name": "dc_description", "type": "TEXT", "required": false, "description": "Dublin Core Description", "namespace": "DublinCore"},
    {"name": "dc_publisher", "type": "TEXT", "required": false, "description": "Dublin Core Publisher", "namespace": "DublinCore"},
    {"name": "dc_contributor", "type": "TEXT", "required": false, "description": "Dublin Core Contributor", "namespace": "DublinCore"},
    {"name": "dc_created", "type": "TEXT", "required": false, "description": "Dublin Core Date Created (YYYY-MM-DD)", "namespace": "DublinCore"},
    {"name": "dc_issued", "type": "TEXT", "required": false, "description": "Dublin Core Date Issued (YYYY-MM-DD)", "namespace": "DublinCore"},
    {"name": "dc_type", "type": "TEXT", "required": false, "description": "Dublin Core Type", "namespace": "DublinCore"},
    {"name": "dc_format", "type": "TEXT", "required": false, "description": "Dublin Core Format MIME", "namespace": "DublinCore"},
    {"name": "dc_identifier", "type": "TEXT", "required": false, "description": "Dublin Core Identifier", "namespace": "DublinCore"},
    {"name": "dc_source", "type": "TEXT", "required": false, "description": "Dublin Core Source", "namespace": "DublinCore"},
    {"name": "dc_language", "type": "TEXT", "required": false, "description": "Dublin Core Language", "namespace": "DublinCore"},
    {"name": "dc_relation", "type": "TEXT", "required": false, "description": "Dublin Core Relation", "namespace": "DublinCore"},
    {"name": "dc_coverage", "type": "TEXT", "required": false, "description": "Dublin Core Coverage", "namespace": "DublinCore"},
    {"name": "dc_rights", "type": "TEXT", "required": false, "description": "Dublin Core Rights", "namespace": "DublinCore"}
  ]
})schema";
}

juce::String gerarFullCsvManifestJson(int assetCount, const juce::String& csvSha256) {
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("schema", "BKR_FULL_CSV");
    obj->setProperty("version", "1.0");
    obj->setProperty("exported_at", juce::String(matriz::model::agoraIso8601()));
    obj->setProperty("asset_count", assetCount);
    obj->setProperty("encoding", "UTF-8");
    obj->setProperty("delimiter", ",");
    obj->setProperty("csv_sha256", csvSha256);
    return juce::JSON::toString(juce::var(obj.release()), true);
}

struct FullCsvValidationResult {
    bool valid = true;
    std::string error;
    int assetCount = 0;
};

FullCsvValidationResult validarFullCsvFile(const juce::File& csvFile, int expectedAssetCount) {
    FullCsvValidationResult res;
    if (!csvFile.existsAsFile()) {
        res.valid = false;
        res.error = "BKR_FULL.csv file does not exist";
        return res;
    }

    juce::String content = csvFile.loadFileAsString();
    juce::StringArray lines;
    lines.addLines(content);

    if (lines.size() < 1) {
        res.valid = false;
        res.error = "CSV file is empty";
        return res;
    }

    auto parseCsvLine = [](const juce::String& line) -> std::vector<juce::String> {
        std::vector<juce::String> tokens;
        juce::String cur;
        bool inQuotes = false;
        for (int i = 0; i < line.length(); ++i) {
            juce::juce_wchar c = line[i];
            if (c == '"') {
                if (inQuotes && i + 1 < line.length() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    inQuotes = !inQuotes;
                }
            } else if (c == ',' && !inQuotes) {
                tokens.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        tokens.push_back(cur);
        return tokens;
    };

    auto headerTokens = parseCsvLine(lines[0]);
    if (headerTokens.size() != 49) {
        res.valid = false;
        res.error = "Header column count mismatch: expected 49, got " + std::to_string(headerTokens.size());
        return res;
    }

    std::set<std::string> seenAssetIds;
    int recordCount = 0;

    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].trim().isEmpty()) continue;
        auto tokens = parseCsvLine(lines[i]);
        if (tokens.size() != 49) {
            res.valid = false;
            res.error = "Row " + std::to_string(i + 1) + " column count mismatch: expected 49, got " + std::to_string(tokens.size());
            return res;
        }

        std::string assetId = tokens[0].trim().toStdString();
        if (assetId.empty()) {
            res.valid = false;
            res.error = "Row " + std::to_string(i + 1) + " has empty asset_id";
            return res;
        }

        if (seenAssetIds.count(assetId)) {
            res.valid = false;
            res.error = "Duplicate asset_id found: " + assetId;
            return res;
        }
        seenAssetIds.insert(assetId);

        // Verify file_size_bytes is integer if present
        std::string sizeStr = tokens[6].trim().toStdString();
        if (!sizeStr.empty()) {
            for (char c : sizeStr) {
                if (!std::isdigit(c)) {
                    res.valid = false;
                    res.error = "Row " + std::to_string(i + 1) + " file_size_bytes is not integer: " + sizeStr;
                    return res;
                }
            }
        }

        // Verify catalog_tags is valid JSON array if present
        std::string tagsStr = tokens[21].trim().toStdString();
        if (!tagsStr.empty()) {
            auto jsonVar = juce::JSON::parse(tagsStr);
            if (!jsonVar.isArray()) {
                res.valid = false;
                res.error = "Row " + std::to_string(i + 1) + " catalog_tags is not valid JSON array: " + tagsStr;
                return res;
            }
        }

        recordCount++;
    }

    if (recordCount != expectedAssetCount) {
        res.valid = false;
        res.error = "Asset count mismatch: expected " + std::to_string(expectedAssetCount) + ", got " + std::to_string(recordCount);
        return res;
    }

    res.assetCount = recordCount;
    return res;
}

} // namespace

juce::String exportarFullCsv(db::Database& db, const std::vector<std::string>& itemIds)
{
    juce::String csv;
    // 49 columns in exact official schema order (Section 2)
    csv += "asset_id,persistent_id,catalog_code,"
           "file_title,file_media_type,file_path,file_size_bytes,file_sha256,file_created_at,file_modified_at,"
           "technical_year,technical_duration_seconds,technical_format,technical_codec,technical_sample_rate,technical_bit_depth,"
           "catalog_content_type,catalog_source_media,catalog_collection_type,catalog_isrc,catalog_notes,catalog_tags,"
           "geo_latitude,geo_longitude,geo_altitude,geo_formatted_address,geo_city,geo_state_province,geo_country,geo_country_code,geo_postal_code,geo_source,geo_accuracy_meters,"
           "dc_title,dc_creator,dc_subject,dc_description,dc_publisher,dc_contributor,dc_created,dc_issued,dc_type,dc_format,dc_identifier,dc_source,dc_language,dc_relation,dc_coverage,dc_rights\n";

    auto esc = [](const juce::String& s) -> juce::String {
        if (s.containsAnyOf(",\"\n\r"))
            return "\"" + s.replace("\"", "\"\"") + "\"";
        return s;
    };

    auto lerCampo = [&](const std::string& itemId, const std::string& campo) -> std::string {
        try {
            auto stmt = db.prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
            stmt.bind(1, Value::of(itemId));
            stmt.bind(2, Value::of(campo));
            if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
        } catch (...) {}
        return "";
    };

    for (const auto& id : itemIds) {
        try {
            std::string pid = "", codigo = "", titulo = "", tipoMidiaRaw = "", criadoEm = "", atualizadoEm = "";
            std::string caminhoAbs = "", caminhoRel = "", sha256 = "";
            juce::int64 tamanhoBytes = -1;

            try {
                auto stmt = db.prepare(
                    "SELECT IFNULL(i.persistent_id,''), IFNULL(i.codigo_acervo,''), i.titulo, IFNULL(i.tipo_midia,''), "
                    "       i.criado_em, i.atualizado_em, IFNULL(a.caminho_absoluto_origem,''), IFNULL(a.caminho_relativo,''), IFNULL(a.tamanho_bytes,-1), IFNULL(a.checksum_sha256,'') "
                    "FROM item i "
                    "LEFT JOIN arquivo a ON a.item_id = i.id AND a.eh_master = 1 "
                    "WHERE i.id = ? LIMIT 1");
                stmt.bind(1, Value::of(id));
                if (stmt.step()) {
                    pid = stmt.columnText(0);
                    codigo = stmt.columnText(1);
                    titulo = stmt.columnText(2);
                    tipoMidiaRaw = stmt.columnText(3);
                    criadoEm = stmt.columnText(4);
                    atualizadoEm = stmt.columnText(5);
                    caminhoAbs = stmt.columnText(6);
                    caminhoRel = stmt.columnText(7);
                    tamanhoBytes = stmt.columnInt(8);
                    sha256 = stmt.columnText(9);
                }
            } catch (...) {}

            std::string filePath = caminhoAbs.empty() ? caminhoRel : caminhoAbs;
            juce::File fileObj(filePath);

            // file_title: remove only extension
            std::string fileTitle;
            if (!filePath.empty() && fileObj.getFileNameWithoutExtension().isNotEmpty()) {
                fileTitle = fileObj.getFileNameWithoutExtension().toStdString();
            } else {
                fileTitle = titulo;
                if (fileTitle.rfind('.') != std::string::npos) {
                    fileTitle = fileTitle.substr(0, fileTitle.rfind('.'));
                }
            }

            // Extension & Technical format
            juce::String ext = fileObj.getFileExtension().toLowerCase().replace(".", "");
            if (ext.isEmpty() && !caminhoRel.empty()) {
                int dot = caminhoRel.rfind('.');
                if (dot != std::string::npos) ext = juce::String(caminhoRel.substr(dot + 1)).toLowerCase();
            }
            std::string techFormat = ext.toUpperCase().toStdString();

            // file_media_type (Audio, Video, Image, Document, Text)
            std::string fileMediaType;
            auto cat = matriz::ingest::categoriaPorExtensao(ext);
            if (cat == matriz::ingest::CategoriaMidia::Audio || tipoMidiaRaw == "digital_audio" || tipoMidiaRaw == "audio" || tipoMidiaRaw == "sessao") {
                fileMediaType = "Audio";
            } else if (cat == matriz::ingest::CategoriaMidia::Video || tipoMidiaRaw == "digital_video" || tipoMidiaRaw == "video") {
                fileMediaType = "Video";
            } else if (cat == matriz::ingest::CategoriaMidia::Imagem || tipoMidiaRaw == "foto" || tipoMidiaRaw == "images") {
                fileMediaType = "Image";
            } else if (cat == matriz::ingest::CategoriaMidia::Texto) {
                fileMediaType = "Text";
            } else {
                fileMediaType = "Document";
            }

            // file_size_bytes
            std::string fileSizeStr;
            if (tamanhoBytes >= 0) fileSizeStr = std::to_string(tamanhoBytes);
            else if (fileObj.existsAsFile()) fileSizeStr = std::to_string(fileObj.getSize());

            // file_created_at & file_modified_at (ISO 8601)
            std::string fileCreatedAt = criadoEm;
            std::string fileModifiedAt = atualizadoEm;
            if (fileObj.existsAsFile()) {
                fileCreatedAt = fileObj.getCreationTime().formatted("%Y-%m-%dT%H:%M:%S-03:00").toStdString();
                fileModifiedAt = fileObj.getLastModificationTime().formatted("%Y-%m-%dT%H:%M:%S-03:00").toStdString();
            }

            // Technical metadata from caracteristicas_tecnicas_json
            std::string techCodec = "", techDuration = "", techSampleRate = "", techBitDepth = "";
            try {
                auto exifStmt = db.prepare("SELECT caracteristicas_tecnicas_json FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
                exifStmt.bind(1, Value::of(id));
                if (exifStmt.step() && !exifStmt.columnIsNull(0)) {
                    auto jsonStr = exifStmt.columnText(0);
                    auto varObj = juce::JSON::parse(jsonStr);
                    if (varObj.isObject()) {
                        if (varObj.hasProperty("codec")) techCodec = varObj["codec"].toString().toStdString();
                        if (varObj.hasProperty("duracaoSegundos")) {
                            double dur = static_cast<double>(varObj["duracaoSegundos"]);
                            if (dur > 0.0) techDuration = juce::String(dur, 2).toStdString();
                        }
                        if (varObj.hasProperty("sampleRate")) {
                            int sr = static_cast<int>(varObj["sampleRate"]);
                            if (sr > 0) techSampleRate = std::to_string(sr);
                        }
                        if (varObj.hasProperty("bitDepth")) {
                            int bd = static_cast<int>(varObj["bitDepth"]);
                            if (bd > 0) techBitDepth = std::to_string(bd);
                        }
                    }
                }
            } catch (...) {}

            // technical_year
            std::string techYear = lerCampo(id, "ano");
            if (techYear.empty()) techYear = lerCampo(id, "dc_created");
            if (techYear.length() >= 4) techYear = techYear.substr(0, 4);

            // Catalog fields
            std::string contentType = lerCampo(id, "content_type");
            std::string sourceMedia = lerCampo(id, "source_media");
            std::string collectionType = lerCampo(id, "collection_type");
            std::string isrc = lerCampo(id, "isrc");
            std::string notas = lerCampo(id, "notas_livres");

            // catalog_tags as valid JSON array string
            std::string tagsJson = "";
            try {
                juce::Array<juce::var> tagArray;
                auto tStmt = db.prepare("SELECT tag FROM item_tag WHERE item_id = ? ORDER BY tag");
                tStmt.bind(1, Value::of(id));
                while (tStmt.step()) {
                    tagArray.add(juce::String(tStmt.columnText(0)));
                }
                if (tagArray.size() > 0) {
                    tagsJson = juce::JSON::toString(juce::var(tagArray), false).toStdString();
                }
            } catch (...) {}

            // Geolocation fields
            std::string lat = "", lng = "", alt = "", fmtAddr = "", city = "", state = "", country = "", ccode = "", pcode = "", geoSrc = "", geoAcc = "";
            try {
                auto gStmt = db.prepare(
                    "SELECT latitude, longitude, altitude, formatted_address, city, state_province, country, country_code, postal_code, source, accuracy_meters "
                    "FROM asset_geolocation WHERE asset_id = ? LIMIT 1");
                gStmt.bind(1, Value::of(id));
                if (gStmt.step()) {
                    if (!gStmt.columnIsNull(0)) lat = juce::String(gStmt.columnReal(0)).toStdString();
                    if (!gStmt.columnIsNull(1)) lng = juce::String(gStmt.columnReal(1)).toStdString();
                    if (!gStmt.columnIsNull(2)) alt = juce::String(gStmt.columnReal(2)).toStdString();
                    if (!gStmt.columnIsNull(3)) fmtAddr = gStmt.columnText(3);
                    if (!gStmt.columnIsNull(4)) city = gStmt.columnText(4);
                    if (!gStmt.columnIsNull(5)) state = gStmt.columnText(5);
                    if (!gStmt.columnIsNull(6)) country = gStmt.columnText(6);
                    if (!gStmt.columnIsNull(7)) ccode = gStmt.columnText(7);
                    if (!gStmt.columnIsNull(8)) pcode = gStmt.columnText(8);
                    if (!gStmt.columnIsNull(9)) geoSrc = gStmt.columnText(9);
                    if (!gStmt.columnIsNull(10)) geoAcc = juce::String(gStmt.columnReal(10)).toStdString();
                }
            } catch (...) {}

            // Dublin Core fields
            std::string dcTitle = lerCampo(id, "dc_title");
            std::string dcCreator = lerCampo(id, "dc_creator");
            std::string dcSubject = lerCampo(id, "dc_subject");
            std::string dcDescription = lerCampo(id, "dc_description");
            std::string dcPublisher = lerCampo(id, "dc_publisher");
            std::string dcContributor = lerCampo(id, "dc_contributor");
            std::string dcCreated = lerCampo(id, "dc_created");
            std::string dcIssued = lerCampo(id, "dc_issued");
            std::string dcType = lerCampo(id, "dc_type");
            std::string dcFormat = lerCampo(id, "dc_format");
            std::string dcIdentifier = lerCampo(id, "dc_identifier");
            std::string dcSource = lerCampo(id, "dc_source");
            std::string dcLanguage = lerCampo(id, "dc_language");
            std::string dcRelation = lerCampo(id, "dc_relation");
            std::string dcCoverage = lerCampo(id, "dc_coverage");
            std::string dcRights = lerCampo(id, "dc_rights");

            // Build 49 columns row
            csv += esc(id) + "," + esc(pid) + "," + esc(codigo) + ","
                 + esc(fileTitle) + "," + esc(fileMediaType) + "," + esc(filePath) + "," + esc(fileSizeStr) + "," + esc(sha256) + "," + esc(fileCreatedAt) + "," + esc(fileModifiedAt) + ","
                 + esc(techYear) + "," + esc(techDuration) + "," + esc(techFormat) + "," + esc(techCodec) + "," + esc(techSampleRate) + "," + esc(techBitDepth) + ","
                 + esc(contentType) + "," + esc(sourceMedia) + "," + esc(collectionType) + "," + esc(isrc) + "," + esc(notas) + "," + esc(tagsJson) + ","
                 + esc(lat) + "," + esc(lng) + "," + esc(alt) + "," + esc(fmtAddr) + "," + esc(city) + "," + esc(state) + "," + esc(country) + "," + esc(ccode) + "," + esc(pcode) + "," + esc(geoSrc) + "," + esc(geoAcc) + ","
                 + esc(dcTitle) + "," + esc(dcCreator) + "," + esc(dcSubject) + "," + esc(dcDescription) + "," + esc(dcPublisher) + "," + esc(dcContributor) + ","
                 + esc(dcCreated) + "," + esc(dcIssued) + "," + esc(dcType) + "," + esc(dcFormat) + "," + esc(dcIdentifier) + "," + esc(dcSource) + ","
                 + esc(dcLanguage) + "," + esc(dcRelation) + "," + esc(dcCoverage) + "," + esc(dcRights) + "\n";
        } catch (...) {}
    }
    return csv;
}

bool exportarFullCsvPacote(db::Database& db, const std::vector<std::string>& itemIds, const juce::File& destLocation, juce::String& errorOut)
{
    try {
        juce::File pkgDir = destLocation.isDirectory() ? destLocation : destLocation.getParentDirectory().getChildFile("BKR_Full_Export");
        if (!pkgDir.exists()) pkgDir.createDirectory();

        juce::File csvFile = pkgDir.getChildFile("BKR_FULL.csv");
        juce::File schemaFile = pkgDir.getChildFile("BKR_FULL.schema.json");
        juce::File manifestFile = pkgDir.getChildFile("manifest.json");

        juce::String csvContent = exportarFullCsv(db, itemIds);
        csvFile.replaceWithText(csvContent, false, false, "\n");

        schemaFile.replaceWithText(gerarFullCsvSchemaJson(), false, false, "\n");

        juce::MemoryBlock block;
        csvFile.loadFileAsData(block);
        juce::SHA256 sha(block.getData(), block.getSize());
        juce::String csvSha256 = sha.toHexString();

        juce::String manifestContent = gerarFullCsvManifestJson(static_cast<int>(itemIds.size()), csvSha256);
        manifestFile.replaceWithText(manifestContent, false, false, "\n");

        // Validate package
        auto valRes = validarFullCsvFile(csvFile, static_cast<int>(itemIds.size()));
        if (!valRes.valid) {
            errorOut = valRes.error;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    } catch (...) {
        errorOut = "Unknown error during BKR Full CSV package export";
        return false;
    }
}

// ---------------------------------------------------------------------------
// Dublin Core CSV
// ---------------------------------------------------------------------------

juce::String exportarCsvDublinCore(db::Database& db,
                                    const std::vector<std::string>& itemIds)
{
    juce::String csv;
    csv += "title,creator,subject,description,publisher,contributor,created,issued,type,format,identifier,source,language,relation,coverage,rights\n";

    auto esc = [](const juce::String& s) -> juce::String {
        if (s.containsAnyOf(",\"\n\r"))
            return "\"" + s.replace("\"", "\"\"") + "\"";
        return s;
    };

    auto lerCampo = [&](const std::string& itemId, const std::string& campo) -> std::string {
        try {
            auto stmt = db.prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
            stmt.bind(1, Value::of(itemId));
            stmt.bind(2, Value::of(campo));
            if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
        } catch (...) {}
        return "";
    };

    for (const auto& id : itemIds) {
        try {
            std::string title = lerCampo(id, "dc_title");
            std::string creator = lerCampo(id, "dc_creator");
            std::string subject = lerCampo(id, "dc_subject");
            std::string description = lerCampo(id, "dc_description");
            std::string publisher = lerCampo(id, "dc_publisher");
            std::string contributor = lerCampo(id, "dc_contributor");
            std::string created = lerCampo(id, "dc_created");
            std::string issued = lerCampo(id, "dc_issued");
            std::string type = lerCampo(id, "dc_type");
            std::string format = lerCampo(id, "dc_format");
            std::string identifier = lerCampo(id, "dc_identifier");
            std::string source = lerCampo(id, "dc_source");
            std::string language = lerCampo(id, "dc_language");
            std::string relation = lerCampo(id, "dc_relation");
            std::string coverage = lerCampo(id, "dc_coverage");
            std::string rights = lerCampo(id, "dc_rights");

            try {
                auto stmt = db.prepare(
                    "SELECT IFNULL(persistent_id,''), IFNULL(titulo,''), IFNULL(tipo_midia,''), criado_em FROM item WHERE id = ? LIMIT 1");
                stmt.bind(1, Value::of(id));
                if (stmt.step()) {
                    if (identifier.empty()) identifier = stmt.columnText(0);
                    if (title.empty()) title = stmt.columnText(1);
                    if (format.empty()) format = stmt.columnText(2);
                    if (created.empty()) created = stmt.columnText(3);
                }
            } catch (...) {}

            csv += esc(title) + "," + esc(creator) + "," + esc(subject) + "," + esc(description) + ","
                 + esc(publisher) + "," + esc(contributor) + "," + esc(created) + "," + esc(issued) + ","
                 + esc(type) + "," + esc(format) + "," + esc(identifier) + "," + esc(source) + ","
                 + esc(language) + "," + esc(relation) + "," + esc(coverage) + "," + esc(rights) + "\n";
        } catch (...) {}
    }
    return csv;
}

juce::String exportarXlsXml(db::Database& db,
                            const std::vector<std::string>& itemIds,
                            const std::string& projectName,
                            const std::string& catalogCode)
{
    auto escHtml = [](const std::string& str) -> juce::String {
        juce::String s(str);
        return s.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
                .replace("\"", "&quot;")
                .replace("'", "&apos;");
    };

    juce::String html;
    html += "<html xmlns:o=\"urn:schemas-microsoft-com:office:office\"\n"
            "xmlns:x=\"urn:schemas-microsoft-com:office:excel\"\n"
            "xmlns=\"http://www.w3.org/TR/REC-html40\">\n"
            "<head>\n"
            "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
            "<meta name=\"ProgId\" content=\"Excel.Sheet\">\n"
            "<meta name=\"Generator\" content=\"BKR Matriz Archival Engine\">\n"
            "<!--[if gte mso 9]><xml>\n"
            " <x:ExcelWorkbook>\n"
            "  <x:ExcelWorksheets>\n"
            "   <x:ExcelWorksheet>\n"
            "    <x:Name>Dublin Core Catalog</x:Name>\n"
            "    <x:WorksheetOptions>\n"
            "     <x:DisplayGridlines/>\n"
            "    </x:WorksheetOptions>\n"
            "   </x:ExcelWorksheet>\n"
            "  </x:ExcelWorksheets>\n"
            " </x:ExcelWorkbook>\n"
            "</xml><![endif]-->\n"
            "<style>\n"
            "  body { font-family: 'Segoe UI', Arial, sans-serif; background-color: #F8FAFC; margin: 0; padding: 20px; }\n"
            "  .hdr-table { width: 100%; border-collapse: collapse; margin-bottom: 20px; font-family: 'Segoe UI', Arial, sans-serif; }\n"
            "  .hdr-main { background-color: #0F172A; color: #38BDF8; font-size: 16px; font-weight: bold; padding: 14px; border: 1px solid #1E293B; text-transform: uppercase; letter-spacing: 1px; }\n"
            "  .hdr-sub { background-color: #1E293B; color: #F8FAFC; font-size: 12px; padding: 10px 14px; border: 1px solid #334155; }\n"
            "  .meta-grid { width: 100%; border-collapse: collapse; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; margin-top: 10px; }\n"
            "  .meta-grid th { background-color: #1E293B; color: #38BDF8; font-weight: bold; text-align: left; padding: 10px 12px; border: 1px solid #334155; font-size: 11px; text-transform: uppercase; letter-spacing: 0.5px; }\n"
            "  .meta-grid td { padding: 9px 12px; border: 1px solid #CBD5E1; color: #0F172A; font-size: 12px; background-color: #FFFFFF; }\n"
            "  .meta-grid tr:nth-child(even) td { background-color: #F8FAFC; }\n"
            "</style>\n"
            "</head>\n"
            "<body>\n";

    std::string dateNow = matriz::model::agoraIso8601();
    juce::int64 totalBytes = 0;
    for (const auto& id : itemIds) {
        try {
            auto stmt = db.prepare("SELECT IFNULL(tamanho_bytes,0) FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
            stmt.bind(1, Value::of(id));
            if (stmt.step()) totalBytes += stmt.columnInt(0);
        } catch (...) {}
    }

    html += "<table class=\"hdr-table\">\n";
    html += "  <tr>\n";
    html += "    <td colspan=\"16\" class=\"hdr-main\">BKR MATRIZ — PROJECT / COLLECTION: " + escHtml(projectName) + " &nbsp;|&nbsp; CATALOG: " + escHtml(catalogCode.empty() ? "BKR-MATRIZ-01" : catalogCode) + "</td>\n";
    html += "  </tr>\n";
    html += "  <tr>\n";
    html += "    <td colspan=\"8\" class=\"hdr-sub\"><b>CATALOGING / BACKUP DATE:</b> " + escHtml(dateNow) + "</td>\n";
    html += "    <td colspan=\"8\" class=\"hdr-sub\"><b>TOTAL ASSETS:</b> " + juce::String(itemIds.size()) + " &nbsp;|&nbsp; <b>TOTAL SIZE:</b> " + juce::File::descriptionOfSizeInBytes(totalBytes) + "</td>\n";
    html += "  </tr>\n";
    html += "</table>\n";

    html += "<table class=\"meta-grid\">\n";
    html += "  <thead>\n";
    html += "    <tr>\n";
    html += "      <th>TITLE (dc.title)</th>\n";
    html += "      <th>CREATOR (dc.creator)</th>\n";
    html += "      <th>SUBJECT (dc.subject)</th>\n";
    html += "      <th>DESCRIPTION (dc.description)</th>\n";
    html += "      <th>PUBLISHER (dc.publisher)</th>\n";
    html += "      <th>CONTRIBUTOR (dc.contributor)</th>\n";
    html += "      <th>DATE CREATED (dc.created)</th>\n";
    html += "      <th>DATE ISSUED (dc.issued)</th>\n";
    html += "      <th>TYPE (dc.type)</th>\n";
    html += "      <th>FORMAT (dc.format)</th>\n";
    html += "      <th>IDENTIFIER (dc.identifier)</th>\n";
    html += "      <th>SOURCE (dc.source)</th>\n";
    html += "      <th>LANGUAGE (dc.language)</th>\n";
    html += "      <th>RELATION (dc.relation)</th>\n";
    html += "      <th>COVERAGE (dc.coverage)</th>\n";
    html += "      <th>RIGHTS (dc.rights)</th>\n";
    html += "    </tr>\n";
    html += "  </thead>\n";
    html += "  <tbody>\n";

    auto lerCampo = [&](const std::string& itemId, const std::string& campo) -> std::string {
        try {
            auto stmt = db.prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
            stmt.bind(1, Value::of(itemId));
            stmt.bind(2, Value::of(campo));
            if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
        } catch (...) {}
        return "";
    };

    for (const auto& id : itemIds) {
        std::string title = lerCampo(id, "dc_title");
        std::string creator = lerCampo(id, "dc_creator");
        std::string subject = lerCampo(id, "dc_subject");
        std::string description = lerCampo(id, "dc_description");
        std::string publisher = lerCampo(id, "dc_publisher");
        std::string contributor = lerCampo(id, "dc_contributor");
        std::string created = lerCampo(id, "dc_created");
        std::string issued = lerCampo(id, "dc_issued");
        std::string type = lerCampo(id, "dc_type");
        std::string format = lerCampo(id, "dc_format");
        std::string identifier = lerCampo(id, "dc_identifier");
        std::string source = lerCampo(id, "dc_source");
        std::string language = lerCampo(id, "dc_language");
        std::string relation = lerCampo(id, "dc_relation");
        std::string coverage = lerCampo(id, "dc_coverage");
        std::string rights = lerCampo(id, "dc_rights");

        try {
            auto stmt = db.prepare(
                "SELECT IFNULL(persistent_id,''), IFNULL(titulo,''), IFNULL(tipo_midia,''), criado_em FROM item WHERE id = ? LIMIT 1");
            stmt.bind(1, Value::of(id));
            if (stmt.step()) {
                if (identifier.empty()) identifier = stmt.columnText(0);
                if (title.empty()) title = stmt.columnText(1);
                if (format.empty()) format = stmt.columnText(2);
                if (created.empty()) created = stmt.columnText(3);
            }
        } catch (...) {}

        html += "    <tr>\n";
        html += "      <td>" + escHtml(title) + "</td>\n";
        html += "      <td>" + escHtml(creator) + "</td>\n";
        html += "      <td>" + escHtml(subject) + "</td>\n";
        html += "      <td>" + escHtml(description) + "</td>\n";
        html += "      <td>" + escHtml(publisher) + "</td>\n";
        html += "      <td>" + escHtml(contributor) + "</td>\n";
        html += "      <td>" + escHtml(created) + "</td>\n";
        html += "      <td>" + escHtml(issued) + "</td>\n";
        html += "      <td>" + escHtml(type) + "</td>\n";
        html += "      <td>" + escHtml(format) + "</td>\n";
        html += "      <td>" + escHtml(identifier) + "</td>\n";
        html += "      <td>" + escHtml(source) + "</td>\n";
        html += "      <td>" + escHtml(language) + "</td>\n";
        html += "      <td>" + escHtml(relation) + "</td>\n";
        html += "      <td>" + escHtml(coverage) + "</td>\n";
        html += "      <td>" + escHtml(rights) + "</td>\n";
        html += "    </tr>\n";
    }

    html += "  </tbody>\n";
    html += "</table>\n";
    html += "</body>\n</html>";

    return html;
}

// ---------------------------------------------------------------------------
// Fixity Manifest (sha256sum / md5sum format)
// ---------------------------------------------------------------------------

juce::String exportarFixityManifest(db::Database& db,
                                     const std::vector<std::string>& itemIds,
                                     const std::string& algoritmo)
{
    bool usarSha = (algoritmo != "md5");
    juce::String manifest;

    for (const auto& id : itemIds) {
        try {
            auto stmt = db.prepare(
                "SELECT IFNULL(checksum_sha256,''), IFNULL(checksum_md5,''), "
                "       IFNULL(caminho_relativo,'') "
                "FROM arquivo WHERE item_id = ? AND eh_master = 1");
            stmt.bind(1, Value::of(id));
            while (stmt.step()) {
                juce::String hash = usarSha
                    ? juce::String(stmt.columnText(0))
                    : juce::String(stmt.columnText(1));
                juce::String path = juce::String(stmt.columnText(2));
                if (hash.isNotEmpty() && path.isNotEmpty())
                    manifest += hash + "  " + path + "\n";
            }
        } catch (...) {}
    }
    return manifest;
}

// ---------------------------------------------------------------------------
// Format Risk Classification
// ---------------------------------------------------------------------------

std::string classificarRiscoFormato(const std::string& extensao)
{
    static const std::unordered_map<std::string, std::string> tabela = {
        // Preservacao segura — abertos, lossless, padrao
        {"wav", "OK"}, {"bwf", "OK"}, {"wave", "OK"},
        {"aiff", "OK"}, {"aif", "OK"}, {"flac", "OK"},
        {"tiff", "OK"}, {"tif", "OK"}, {"png", "OK"},
        {"pdf", "OK"}, {"txt", "OK"}, {"csv", "OK"},
        {"xml", "OK"}, {"json", "OK"}, {"svg", "OK"},
        {"dng", "OK"}, {"bmp", "OK"},
        // Acesso — comprimidos mas amplamente suportados
        {"mp3", "OK"}, {"mp4", "OK"}, {"m4a", "OK"},
        {"jpg", "OK"}, {"jpeg", "OK"}, {"ogg", "OK"},
        {"webm", "OK"}, {"mkv", "OK"}, {"mov", "OK"},
        // Em risco — proprietarios ou obsoletos
        {"wma", "AT_RISK"}, {"wmv", "AT_RISK"},
        {"ra", "AT_RISK"}, {"ram", "AT_RISK"}, {"rm", "AT_RISK"},
        {"m4p", "AT_RISK"},
        {"doc", "AT_RISK"}, {"xls", "AT_RISK"}, {"ppt", "AT_RISK"},
        {"mpc", "AT_RISK"}, {"vqf", "AT_RISK"}, {"ape", "AT_RISK"},
        {"au", "AT_RISK"}, {"shn", "AT_RISK"},
        {"psd", "AT_RISK"}, {"ai", "AT_RISK"}, {"cdr", "AT_RISK"},
        {"3gp", "AT_RISK"}, {"asf", "AT_RISK"}, {"swf", "AT_RISK"},
    };

    std::string ext = extensao;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);

    auto it = tabela.find(ext);
    return (it != tabela.end()) ? it->second : "UNKNOWN";
}

// ---------------------------------------------------------------------------
// Compliance: Regra 3-2-1
// ---------------------------------------------------------------------------

ComplianceRegra321 avaliarRegra321(db::Database& db)
{
    ComplianceRegra321 r;

    try {
        auto stmt = db.prepare(
            "SELECT COUNT(DISTINCT v.id), "
            "       COUNT(DISTINCT v.tipo), "
            "       SUM(CASE WHEN v.tipo IN ('nuvem_sync','rede','lto') THEN 1 ELSE 0 END) "
            "FROM vault v");
        if (stmt.step()) {
            r.totalCopias         = stmt.columnInt(0) + 1; // +1 = disco principal
            r.tiposMidiaDistintos = stmt.columnInt(1) + 1; // +1 = disco principal (local)
            r.temOffsite          = stmt.columnInt(2) > 0;
        }
    } catch (...) {
        r.totalCopias = 1;
        r.tiposMidiaDistintos = 1;
    }

    // Contar destinos configurados tambem
    try {
        auto stmt = db.prepare(
            "SELECT COUNT(DISTINCT tipo), "
            "       SUM(CASE WHEN tipo IN ('s3','google_drive','nas_smb') THEN 1 ELSE 0 END) "
            "FROM destino");
        if (stmt.step()) {
            int tiposDestino = stmt.columnInt(0);
            if (tiposDestino > 0) {
                r.tiposMidiaDistintos = std::max(r.tiposMidiaDistintos,
                                                  tiposDestino + 1);
            }
            if (stmt.columnInt(1) > 0) r.temOffsite = true;
        }
    } catch (...) {}

    bool copias3   = r.totalCopias >= 3;
    bool midias2   = r.tiposMidiaDistintos >= 2;
    bool offsite1  = r.temOffsite;

    if (copias3 && midias2 && offsite1) {
        r.status = "OK";
    } else {
        std::string detail;
        if (!copias3) detail += std::to_string(r.totalCopias) + "/3 copies; ";
        if (!midias2) detail += std::to_string(r.tiposMidiaDistintos) + "/2 media types; ";
        if (!offsite1) detail += "no off-site; ";
        if (!detail.empty()) detail.erase(detail.size() - 2);
        r.status = detail;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Compliance: Vault Refresh Alert
// ---------------------------------------------------------------------------

std::vector<AlertaRefreshVault> detectarVaultsParaRefresh(db::Database& db,
                                                           int limiteAnos)
{
    std::vector<AlertaRefreshVault> alertas;
    try {
        auto stmt = db.prepare(
            "SELECT nome, tipo, "
            "  CAST((julianday('now') - julianday(IFNULL(verificado_em, criado_em))) / 365 AS INTEGER) "
            "FROM vault "
            "WHERE CAST((julianday('now') - julianday(IFNULL(verificado_em, criado_em))) / 365 AS INTEGER) >= ?");
        stmt.bind(1, Value::of(limiteAnos));
        while (stmt.step()) {
            AlertaRefreshVault a;
            a.vaultNome         = stmt.columnText(0);
            a.tipo              = stmt.columnText(1);
            a.anosDesdeUltimoUso = stmt.columnInt(2);
            alertas.push_back(std::move(a));
        }
    } catch (...) {}
    return alertas;
}

// ---------------------------------------------------------------------------
// Compliance: Format Risk Count
// ---------------------------------------------------------------------------

int contarFormatosEmRisco(db::Database& db)
{
    int count = 0;
    try {
        // caminho_relativo holds the file path; extract extension after last '.'
        auto stmt = db.prepare(
            "SELECT DISTINCT i.id, a.caminho_relativo "
            "FROM arquivo a JOIN item i ON i.id = a.item_id "
            "WHERE a.eh_master = 1 AND a.caminho_relativo LIKE '%.%'");
        while (stmt.step()) {
            std::string path = stmt.columnText(1);
            auto dotPos = path.rfind('.');
            if (dotPos != std::string::npos) {
                std::string ext = path.substr(dotPos + 1);
                if (classificarRiscoFormato(ext) == "AT_RISK") ++count;
            }
        }
    } catch (...) {}
    return count;
}

} // namespace matriz::preservation
