#include "SmartHealth.h"

#include <map>
#include <mutex>
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

SmartHealthReport avaliarSaudeWindowsJson(const juce::String& jsonText) {
    SmartHealthReport rep;
    rep.rawJson = jsonText.toStdString();
    rep.lastScanTime = formatScanTime(juce::Time::getCurrentTime());
    auto indisponivel = [&rep](const juce::String& msg) {
        rep.state = HealthState::Unavailable;
        rep.stateLabel = "UNAVAILABLE";
        rep.stateColour = juce::Colour(0xff71717a);
        rep.unavailableMessage = msg;
        return rep;
    };

    const juce::var raiz = juce::JSON::parse(jsonText);  // guarda o var: o DynamicObject morre junto com ele
    auto* obj = raiz.getDynamicObject();
    if (obj == nullptr) return indisponivel("Windows did not return drive health information.");
    const juce::String saude = obj->getProperty("Health").toString().trim();
    if (saude.isEmpty())
        return indisponivel("Windows does not report health for this drive (common for external USB enclosures).");

    auto numero = [obj](const char* nome) -> juce::int64 {
        const juce::var v = obj->getProperty(nome);
        return (v.isVoid() || v.isUndefined()) ? -1 : static_cast<juce::int64>(v);  // null do JSON = sem dado
    };
    const juce::int64 temp = numero("Temp");
    const juce::int64 horas = numero("Hours");
    const juce::int64 errosLeitura = numero("ReadErr");
    const juce::int64 errosEscrita = numero("WriteErr");
    if (temp > 0 && temp < 150) rep.temperatureC = static_cast<int>(temp);  // 0 = o disco não informa
    if (horas >= 0) rep.powerOnHours = horas;
    if (errosLeitura >= 0 || errosEscrita >= 0)
        rep.uncorrectableSectors = juce::jmax<juce::int64>(errosLeitura, 0) + juce::jmax<juce::int64>(errosEscrita, 0);

    const bool alerta = rep.uncorrectableSectors > 0 || rep.temperatureC > 60;
    if (saude.equalsIgnoreCase("Healthy")) {
        rep.smartStatus = "PASSED";
        rep.state = alerta ? HealthState::Warning : HealthState::Healthy;
    } else if (saude.equalsIgnoreCase("Warning")) {
        rep.smartStatus = "WARNING";
        rep.state = HealthState::Warning;
    } else if (saude.equalsIgnoreCase("Unhealthy")) {
        rep.smartStatus = "FAILED";
        rep.state = HealthState::Failing;
    } else {
        rep.state = HealthState::Unknown;
    }
    switch (rep.state) {
        case HealthState::Healthy: rep.stateLabel = "HEALTHY"; rep.stateColour = juce::Colour(0xff22c55e); break;
        case HealthState::Warning: rep.stateLabel = "WARNING"; rep.stateColour = juce::Colour(0xffeab308); break;
        case HealthState::Failing: rep.stateLabel = "FAILING"; rep.stateColour = juce::Colour(0xffef4444); break;
        default: rep.stateLabel = "UNKNOWN"; rep.stateColour = juce::Colour(0xff71717a); break;
    }
    return rep;
}

#if defined(_WIN32)
SmartHealthReport obterSaudeSmartWindows(const juce::File& mountPoint) {
    const juce::String caminho = mountPoint.getFullPathName();
    const juce::juce_wchar letra = caminho.isNotEmpty() ? caminho[0] : 0;
    if (!((letra >= 'A' && letra <= 'Z') || (letra >= 'a' && letra <= 'z')) || caminho[1] != ':')
        return avaliarSaudeWindowsJson("{}");  // sem letra de unidade (ex.: caminho de rede): nada a consultar

    // Cache de 10 min por letra: a tela de Storage consulta cada dispositivo e o PowerShell leva alguns segundos.
    static std::mutex mtxCache;
    static std::map<juce::juce_wchar, std::pair<juce::uint32, SmartHealthReport>> cache;
    const juce::juce_wchar chave = juce::CharacterFunctions::toUpperCase(letra);
    {
        std::lock_guard<std::mutex> lk(mtxCache);
        auto it = cache.find(chave);
        if (it != cache.end() && juce::Time::getMillisecondCounter() - it->second.first < 10u * 60u * 1000u)
            return it->second.second;
    }

    // A letra já foi validada (um caractere A-Z): é a única parte variável do script.
    const juce::String script =
        "$ErrorActionPreference = 'Stop'\n"
        "try {\n"
        "  $d = @(Get-Partition -DriveLetter '" + juce::String::charToString(chave) + "' | Get-Disk | Get-PhysicalDisk)[0]\n"
        "  $r = $null\n"
        "  try { $r = $d | Get-StorageReliabilityCounter } catch {}\n"
        "  [pscustomobject]@{ Health = [string]$d.HealthStatus; Media = [string]$d.MediaType; Bus = [string]$d.BusType;\n"
        "    Temp = $r.Temperature; Hours = $r.PowerOnHours; ReadErr = $r.ReadErrorsUncorrected;\n"
        "    WriteErr = $r.WriteErrorsUncorrected } | ConvertTo-Json -Compress\n"
        "} catch { '{}' }\n";
    // -EncodedCommand (UTF-16LE em base64): nenhuma aspa na linha de comando para quebrar.
    const wchar_t* w = script.toWideCharPointer();
    const juce::String b64 = juce::Base64::toBase64(w, wcslen(w) * sizeof(wchar_t));

    juce::String saida;
    {
        juce::ChildProcess proc;  // o JUCE usa CREATE_NO_WINDOW: sem janela de console
        if (proc.start(juce::StringArray{"powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                         "-EncodedCommand", b64},
                       juce::ChildProcess::wantStdOut)) {
            if (proc.waitForProcessToFinish(15000))
                saida = proc.readAllProcessOutput().trim();
            else
                proc.kill();  // travou: sem dado, e o resultado fica "indisponível"
        }
    }
    auto rep = avaliarSaudeWindowsJson(saida);
    {
        std::lock_guard<std::mutex> lk(mtxCache);
        cache[chave] = {juce::Time::getMillisecondCounter(), rep};
    }
    return rep;
}
#endif

SmartHealthReport consultarSaudeSmart(const std::string& bsdDeviceNode, const juce::File& mountPoint) {
    juce::File smartctl = encontrarSmartctl();
    if (smartctl.existsAsFile()) {
        juce::String bsd = extrairDiscoFisicoBase(juce::String(bsdDeviceNode));
        if (bsd.isEmpty() && mountPoint.exists()) {
            auto id = obterIdentidadeHardwareVolume(mountPoint);
            bsd = extrairDiscoFisicoBase(juce::String(id.bsdDeviceNode));
        }

        if (bsd.isNotEmpty()) {
            juce::StringArray args;
            args.add(smartctl.getFullPathName());
            args.add("-j");
            args.add("-a");
            args.add(bsd);

            juce::ChildProcess proc;
            if (proc.start(args, juce::ChildProcess::wantStdOut)) {
                juce::String output = proc.readAllProcessOutput();
                proc.waitForProcessToFinish(3000);
                auto rep = parseSmartctlJson(output);
                if (rep.state != HealthState::Unavailable) {
                    return rep;
                }
            }
        }
    }

#if defined(__APPLE__)
    return obterSaudeSmartNativoMac(mountPoint, bsdDeviceNode);
#elif defined(_WIN32)
    return obterSaudeSmartWindows(mountPoint);
#else
    SmartHealthReport rep;
    rep.state = HealthState::Unavailable;
    rep.stateLabel = "UNAVAILABLE";
    rep.stateColour = juce::Colour(0xff71717a);
    rep.unavailableMessage = "SMART telemetry is not supported on this storage interface.";
    return rep;
#endif
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

SmartHealthReport obterUltimoLog(matriz::db::Database& db, const std::string& vaultId) {
    if (vaultId.empty()) return SmartHealthReport{};

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

    return SmartHealthReport{};
}

SmartHealthReport obterUltimoLogOuConsultar(matriz::db::Database& db, const std::string& vaultId,
                                           const std::string& bsdDeviceNode, const juce::File& mountPoint) {
    if (vaultId.empty()) {
        return consultarSaudeSmart(bsdDeviceNode, mountPoint);
    }

    auto existing = obterUltimoLog(db, vaultId);
    if (existing.state != HealthState::Unavailable || existing.smartStatus != "-") {
        return existing;
    }

    // No previous log found: run live check and record
    auto rep = consultarSaudeSmart(bsdDeviceNode, mountPoint);
    gravarLogSmart(db, vaultId, rep);
    return rep;
}

} // namespace matriz::vault
