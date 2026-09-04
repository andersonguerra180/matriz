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

// Native in-process macOS DiskArbitration / IOKit SMART health query (no external tools required)
SmartHealthReport obterSaudeSmartNativoMac(const juce::File& path, const std::string& bsdNode);

// Dispatches smartctl command if present, with seamless native IOKit/DiskArbitration fallback
SmartHealthReport consultarSaudeSmart(const std::string& bsdDeviceNode, const juce::File& mountPoint);

// Loads the latest persistent SMART reading from the database, or runs an on-demand scan if none exists
SmartHealthReport obterUltimoLogOuConsultar(matriz::db::Database& db, const std::string& vaultId,
                                           const std::string& bsdDeviceNode, const juce::File& mountPoint);

// Persists a SMART health reading into the database
void gravarLogSmart(matriz::db::Database& db, const std::string& vaultId, const SmartHealthReport& report);

} // namespace matriz::vault
