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

namespace matriz::preservation {

using matriz::db::Value;
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

// ---------------------------------------------------------------------------
// Dublin Core CSV
// ---------------------------------------------------------------------------

juce::String exportarCsvDublinCore(db::Database& db,
                                    const std::vector<std::string>& itemIds)
{
    juce::String csv;
    csv += "dc.identifier,dc.title,dc.format,dc.extent,dc.source,dc.date,"
           "dc.rights\n";

    for (const auto& id : itemIds) {
        try {
            auto stmt = db.prepare(
                "SELECT IFNULL(i.persistent_id,''), i.titulo, IFNULL(i.tipo_midia,''), "
                "       IFNULL(a.tamanho_bytes,0), IFNULL(a.checksum_sha256,''), i.criado_em "
                "FROM item i "
                "LEFT JOIN arquivo a ON a.item_id = i.id AND a.eh_master = 1 "
                "WHERE i.id = ? LIMIT 1");
            stmt.bind(1, Value::of(id));
            if (!stmt.step()) continue;

            auto esc = [](const juce::String& s) -> juce::String {
                if (s.containsAnyOf(",\"\n\r"))
                    return "\"" + s.replace("\"", "\"\"") + "\"";
                return s;
            };

            std::string rightsLabel = "UNKNOWN";
            try {
                auto rStmt = db.prepare(
                    "SELECT IFNULL(rights_status,'UNKNOWN') FROM preservation_right "
                    "WHERE item_id = ? LIMIT 1");
                rStmt.bind(1, Value::of(id));
                if (rStmt.step()) rightsLabel = rStmt.columnText(0);
            } catch (...) {}

            csv += esc(juce::String(stmt.columnText(0))) + ","
                 + esc(juce::String(stmt.columnText(1))) + ","
                 + esc(juce::String(stmt.columnText(2))) + ","
                 + juce::String(stmt.columnInt(3))        + ","
                 + esc(juce::String(stmt.columnText(4))) + ","
                 + esc(juce::String(stmt.columnText(5))) + ","
                 + esc(juce::String(rightsLabel))          + "\n";
        } catch (...) {}
    }
    return csv;
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
