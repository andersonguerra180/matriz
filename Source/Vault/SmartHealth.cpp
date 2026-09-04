#include "SmartHealth.h"
#include "../Db/Database.h"
#include "../Model/Project.h"
#include "DiskIdentity.h"

namespace matriz::vault {

namespace {

juce::String formatScanTime(const juce::Time& t) {
    return t.formatted("%Y-%m-%d %H:%M");
}

juce::File encontrarSmartctl() {
    const char* paths[] = {
        "/opt/homebrew/bin/smartctl",
        "/usr/local/bin/smartctl",
        "/usr/local/sbin/smartctl",
        "/usr/bin/smartctl",
        "/usr/sbin/smartctl"
    };
    for (const char* p : paths) {
        juce::File f(p);
        if (f.existsAsFile()) return f;
    }
    return {};
}

juce::String extrairDiscoFisicoBase(const juce::String& bsdNode) {
    if (bsdNode.isEmpty()) return {};
    juce::String bsd = bsdNode.trim();
    if (bsd.startsWith("/dev/")) bsd = bsd.substring(5);

    // Se tiver partição (ex: disk2s1 -> disk2), isola o disco base
    int sIdx = bsd.lastIndexOf("s");
    if (sIdx > 4 && bsd.substring(sIdx + 1).containsOnly("0123456789")) {
        bsd = bsd.substring(0, sIdx);
    }

    if (!bsd.startsWith("/dev/")) {
        return "/dev/" + bsd;
    }
    return bsd;
}

} // namespace

SmartHealthReport parseSmartctlJson(const juce::String& jsonText) {
    SmartHealthReport rep;
    rep.rawJson = jsonText.toStdString();
    rep.lastScanTime = formatScanTime(juce::Time::getCurrentTime());

    auto varJson = juce::JSON::parse(jsonText);
    if (!varJson.isObject()) {
        rep.state = HealthState::Unavailable;
        rep.stateLabel = "UNAVAILABLE";
        rep.stateColour = juce::Colour(0xff71717a);
        rep.unavailableMessage = "SMART data unavailable through the current storage interface.";
        return rep;
    }

    auto* obj = varJson.getDynamicObject();
    if (!obj) {
        rep.state = HealthState::Unavailable;
        rep.stateLabel = "UNAVAILABLE";
        rep.stateColour = juce::Colour(0xff71717a);
        rep.unavailableMessage = "SMART data unavailable through the current storage interface.";
        return rep;
    }

    // Check smartctl exit status bitmask
    int exitStatus = 0;
    if (auto* sctl = obj->getProperty("smartctl").getDynamicObject()) {
        if (sctl->hasProperty("exit_status")) {
            exitStatus = static_cast<int>(sctl->getProperty("exit_status"));
        }
    }

    // Bit 1: Device open failed / unsupported interface / no permissions
    // Bit 2: SMART command failed
    if ((exitStatus & 2) != 0 || (exitStatus & 1) != 0) {
        rep.state = HealthState::Unavailable;
        rep.stateLabel = "UNAVAILABLE";
        rep.stateColour = juce::Colour(0xff71717a);
        rep.unavailableMessage = "SMART data unavailable through the current storage interface.";
        return rep;
    }

    // SMART Status
    bool hasSmartStatus = false;
    bool smartPassed = false;
    if (auto* st = obj->getProperty("smart_status").getDynamicObject()) {
        if (st->hasProperty("passed")) {
            hasSmartStatus = true;
            smartPassed = static_cast<bool>(st->getProperty("passed"));
            rep.smartStatus = smartPassed ? "PASSED" : "FAILED";
        }
    }

    // Temperature
    if (auto* temp = obj->getProperty("temperature").getDynamicObject()) {
        if (temp->hasProperty("current")) {
            rep.temperatureC = static_cast<int>(temp->getProperty("current"));
        }
    }

    // Power-on hours (ATA format)
    if (auto* poh = obj->getProperty("power_on_time").getDynamicObject()) {
        if (poh->hasProperty("hours")) {
            rep.powerOnHours = static_cast<juce::int64>(poh->getProperty("hours"));
        }
    }

    // ATA Attributes Table
    if (auto* ata = obj->getProperty("ata_smart_attributes").getDynamicObject()) {
        if (auto* tbl = ata->getProperty("table").getArray()) {
            for (auto& item : *tbl) {
                if (auto* attr = item.getDynamicObject()) {
                    int id = static_cast<int>(attr->getProperty("id"));
                    juce::int64 rawVal = 0;
                    if (auto* rawObj = attr->getProperty("raw").getDynamicObject()) {
                        rawVal = static_cast<juce::int64>(rawObj->getProperty("value"));
                    }

                    if (id == 5) rep.reallocatedSectors = rawVal;
                    else if (id == 197) rep.pendingSectors = rawVal;
                    else if (id == 198) rep.uncorrectableSectors = rawVal;
                    else if (id == 194 && rep.temperatureC < 0) rep.temperatureC = static_cast<int>(rawVal);
                    else if (id == 9 && rep.powerOnHours < 0) rep.powerOnHours = rawVal;
                }
            }
        }
    }

    // NVMe Health Information Log
    int nvmeCriticalWarning = 0;
    if (auto* nvme = obj->getProperty("nvme_smart_health_information_log").getDynamicObject()) {
        if (nvme->hasProperty("critical_warning")) {
            nvmeCriticalWarning = static_cast<int>(nvme->getProperty("critical_warning"));
        }
        if (nvme->hasProperty("temperature") && rep.temperatureC < 0) {
            rep.temperatureC = static_cast<int>(nvme->getProperty("temperature"));
        }
        if (nvme->hasProperty("power_on_hours") && rep.powerOnHours < 0) {
            rep.powerOnHours = static_cast<juce::int64>(nvme->getProperty("power_on_hours"));
        }
        if (nvme->hasProperty("media_and_data_integrity_errors")) {
            juce::int64 nvmeErrors = static_cast<juce::int64>(nvme->getProperty("media_and_data_integrity_errors"));
            if (rep.uncorrectableSectors < 0) rep.uncorrectableSectors = nvmeErrors;
        }
    }

    // Evaluation of State
    if (!hasSmartStatus && rep.temperatureC < 0 && rep.powerOnHours < 0) {
        rep.state = HealthState::Unknown;
        rep.stateLabel = "UNKNOWN";
        rep.stateColour = juce::Colour(0xff71717a);
        return rep;
    }

    // Check Failing
    if ((hasSmartStatus && !smartPassed) || (exitStatus & 8) != 0 || nvmeCriticalWarning > 0) {
        rep.state = HealthState::Failing;
        rep.stateLabel = "FAILING";
        rep.stateColour = juce::Colour(0xffef4444);
        return rep;
    }

    // Check Warning
    bool warningCondition = (rep.reallocatedSectors > 0) || (rep.pendingSectors > 0) ||
                           (rep.uncorrectableSectors > 0) || (rep.temperatureC > 60);

    if (warningCondition) {
        rep.state = HealthState::Warning;
        rep.stateLabel = "WARNING";
        rep.stateColour = juce::Colour(0xffeab308);
        return rep;
    }

    if (hasSmartStatus && smartPassed) {
        rep.state = HealthState::Healthy;
        rep.stateLabel = "HEALTHY";
        rep.stateColour = juce::Colour(0xff22c55e);
        return rep;
    }

    rep.state = HealthState::Unknown;
    rep.stateLabel = "UNKNOWN";
    rep.stateColour = juce::Colour(0xff71717a);
    return rep;
}

SmartHealthReport consultarSaudeSmart(const std::string& bsdDeviceNode, const juce::File& mountPoint) {
    juce::File smartctl = encontrarSmartctl();
    if (!smartctl.existsAsFile()) {
        SmartHealthReport rep;
        rep.state = HealthState::Unavailable;
        rep.stateLabel = "UNAVAILABLE";
        rep.stateColour = juce::Colour(0xff71717a);
        rep.unavailableMessage = "SMART data unavailable through the current storage interface.";
        return rep;
    }

    juce::String bsd = extrairDiscoFisicoBase(juce::String(bsdDeviceNode));
    if (bsd.isEmpty() && mountPoint.exists()) {
        auto id = obterIdentidadeHardwareVolume(mountPoint);
        bsd = extrairDiscoFisicoBase(juce::String(id.bsdDeviceNode));
    }

    if (bsd.isEmpty()) {
        SmartHealthReport rep;
        rep.state = HealthState::Unavailable;
        rep.stateLabel = "UNAVAILABLE";
        rep.stateColour = juce::Colour(0xff71717a);
        rep.unavailableMessage = "SMART data unavailable through the current storage interface.";
        return rep;
    }

    juce::StringArray args;
    args.add(smartctl.getFullPathName());
    args.add("-j");
    args.add("-a");
    args.add(bsd);

    juce::ChildProcess proc;
    if (!proc.start(args, juce::ChildProcess::wantStdOut)) {
        SmartHealthReport rep;
        rep.state = HealthState::Unavailable;
        rep.stateLabel = "UNAVAILABLE";
        rep.stateColour = juce::Colour(0xff71717a);
        rep.unavailableMessage = "SMART data unavailable through the current storage interface.";
        return rep;
    }

    juce::String output = proc.readAllProcessOutput();
    proc.waitForProcessToFinish(3000);

    return parseSmartctlJson(output);
}

void gravarLogSmart(matriz::db::Database& db, const std::string& vaultId, const SmartHealthReport& report) {
    if (vaultId.empty()) return;

    try {
        db.run(
            "CREATE TABLE IF NOT EXISTS vault_smart_log ("
            "  id                     TEXT PRIMARY KEY,"
            "  vault_id               TEXT NOT NULL REFERENCES vault(id) ON DELETE CASCADE,"
            "  estado                 TEXT NOT NULL,"
            "  smart_status           TEXT,"
            "  temperatura_c          INTEGER,"
            "  horas_ligado           INTEGER,"
            "  reallocated_sectors    INTEGER,"
            "  pending_sectors        INTEGER,"
            "  uncorrectable_sectors  INTEGER,"
            "  raw_json               TEXT,"
            "  criado_em              TEXT NOT NULL"
            ");", {});
    } catch (...) {}

    std::string logId = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();

    try {
        auto stmt = db.prepare(
            "INSERT INTO vault_smart_log (id, vault_id, estado, smart_status, temperatura_c, horas_ligado, "
            "reallocated_sectors, pending_sectors, uncorrectable_sectors, raw_json, criado_em) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
        stmt.bind(1, matriz::db::Value::of(logId));
        stmt.bind(2, matriz::db::Value::of(vaultId));
        stmt.bind(3, matriz::db::Value::of(report.stateLabel.toStdString()));
        stmt.bind(4, matriz::db::Value::of(report.smartStatus.toStdString()));
        stmt.bind(5, matriz::db::Value::of(report.temperatureC));
        stmt.bind(6, matriz::db::Value::of(report.powerOnHours));
        stmt.bind(7, matriz::db::Value::of(report.reallocatedSectors));
        stmt.bind(8, matriz::db::Value::of(report.pendingSectors));
        stmt.bind(9, matriz::db::Value::of(report.uncorrectableSectors));
        stmt.bind(10, matriz::db::Value::of(report.rawJson));
        stmt.bind(11, matriz::db::Value::of(agora));
        stmt.step();
    } catch (...) {}
}

SmartHealthReport obterUltimoLogOuConsultar(matriz::db::Database& db, const std::string& vaultId,
                                           const std::string& bsdDeviceNode, const juce::File& mountPoint) {
    if (vaultId.empty()) {
        return consultarSaudeSmart(bsdDeviceNode, mountPoint);
    }

    try {
        auto stmt = db.prepare(
            "SELECT estado, COALESCE(smart_status, '-'), COALESCE(temperatura_c, -1), "
            "       COALESCE(horas_ligado, -1), COALESCE(reallocated_sectors, -1), "
            "       COALESCE(pending_sectors, -1), COALESCE(uncorrectable_sectors, -1), "
            "       COALESCE(criado_em, ''), COALESCE(raw_json, '') "
            "FROM vault_smart_log "
            "WHERE vault_id = ? "
            "ORDER BY criado_em DESC "
            "LIMIT 1;");
        stmt.bind(1, matriz::db::Value::of(vaultId));

        if (stmt.step()) {
            SmartHealthReport rep;
            rep.stateLabel = stmt.columnText(0);
            if (rep.stateLabel == "HEALTHY") {
                rep.state = HealthState::Healthy;
                rep.stateColour = juce::Colour(0xff22c55e);
            } else if (rep.stateLabel == "WARNING") {
                rep.state = HealthState::Warning;
                rep.stateColour = juce::Colour(0xffeab308);
            } else if (rep.stateLabel == "FAILING") {
                rep.state = HealthState::Failing;
                rep.stateColour = juce::Colour(0xffef4444);
            } else if (rep.stateLabel == "UNKNOWN") {
                rep.state = HealthState::Unknown;
                rep.stateColour = juce::Colour(0xff71717a);
            } else {
                rep.state = HealthState::Unavailable;
                rep.stateColour = juce::Colour(0xff71717a);
                rep.unavailableMessage = "SMART data unavailable through the current storage interface.";
            }

            rep.smartStatus = stmt.columnText(1);
            rep.temperatureC = static_cast<int>(stmt.columnInt(2));
            rep.powerOnHours = stmt.columnInt(3);
            rep.reallocatedSectors = stmt.columnInt(4);
            rep.pendingSectors = stmt.columnInt(5);
            rep.uncorrectableSectors = stmt.columnInt(6);
            juce::String dt = stmt.columnText(7);
            rep.lastScanTime = dt.isNotEmpty() ? dt.substring(0, 16).replace("T", " ") : "-";
            rep.rawJson = stmt.columnText(8);
            return rep;
        }
    } catch (...) {}

    // No previous log found: run live check and record
    auto rep = consultarSaudeSmart(bsdDeviceNode, mountPoint);
    gravarLogSmart(db, vaultId, rep);
    return rep;
}

} // namespace matriz::vault
