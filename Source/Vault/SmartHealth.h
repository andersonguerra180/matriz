#pragma once

#include <JuceHeader.h>
#include <string>

namespace matriz::db {
class Database;
}

namespace matriz::vault {

enum class HealthState {
    Healthy,
    Warning,
    Failing,
    Unknown,
    Unavailable
};

struct SmartHealthReport {
    HealthState state = HealthState::Unavailable;
    juce::String stateLabel = "UNAVAILABLE";
    juce::Colour stateColour = juce::Colour(0xff71717a);
    juce::String smartStatus = "-";
    int temperatureC = -1;
    juce::int64 powerOnHours = -1;
    juce::int64 reallocatedSectors = -1;
    juce::int64 pendingSectors = -1;
    juce::int64 uncorrectableSectors = -1;
    juce::String lastScanTime = "-";
    juce::String unavailableMessage;
    std::string rawJson;
};

// Evaluates SMART health from a smartctl JSON output string
SmartHealthReport parseSmartctlJson(const juce::String& jsonText);

// Avalia o JSON que a consulta do Windows monta a partir de Get-PhysicalDisk / Get-StorageReliabilityCounter:
// {"Health":"Healthy|Warning|Unhealthy","Temp":35,"Hours":1200,"ReadErr":0,"WriteErr":0,...}. Fica fora do
// #if do Windows para o self-test exercitar as regras em qualquer sistema. Sem "Health" = indisponível (comum em HD
// externo atrás de ponte USB, que o Windows não consegue consultar).
SmartHealthReport avaliarSaudeWindowsJson(const juce::String& jsonText);

// Windows: consulta a saúde do disco que contém `mountPoint` (ex.: E:\) via PowerShell, sem janela de console e sem
// precisar de administrador. BLOQUEIA (processo externo, até ~15 s): chamar de thread de fundo. Resultado em cache
// por 10 min por letra. Só é definida no Windows.
SmartHealthReport obterSaudeSmartWindows(const juce::File& mountPoint);

// Native in-process macOS DiskArbitration / IOKit SMART health query (no external tools required)
SmartHealthReport obterSaudeSmartNativoMac(const juce::File& path, const std::string& bsdNode);

// Dispatches smartctl command if present, with seamless native IOKit/DiskArbitration fallback
SmartHealthReport consultarSaudeSmart(const std::string& bsdDeviceNode, const juce::File& mountPoint);

// Loads the latest persistent SMART reading from the database without querying hardware
SmartHealthReport obterUltimoLog(matriz::db::Database& db, const std::string& vaultId);

// Loads the latest persistent SMART reading from the database, or runs an on-demand scan if none exists
SmartHealthReport obterUltimoLogOuConsultar(matriz::db::Database& db, const std::string& vaultId,
                                           const std::string& bsdDeviceNode, const juce::File& mountPoint);

// Persists a SMART health reading into the database
void gravarLogSmart(matriz::db::Database& db, const std::string& vaultId, const SmartHealthReport& report);

} // namespace matriz::vault
