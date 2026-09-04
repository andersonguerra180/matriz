#pragma once

#include <JuceHeader.h>
#include <string>
#include <vector>

namespace matriz::vault {

struct VolumeHardwareIdentity {
    std::string mountPoint;          // e.g. "/Volumes/BUNKER 4TB" or "/"
    std::string volumeUuid;          // Volume UUID (stable volume GUID)
    std::string volumeLabel;         // Human-readable volume label
    std::string bsdDeviceNode;       // e.g. "/dev/disk2s1" or "disk2s1"
    std::string fileSystem;          // e.g. "apfs", "exfat", "hfs"
    std::string vendor;              // e.g. "Apple", "SanDisk", "Western Digital"
    std::string model;               // e.g. "Extreme SSD", "APPLE SSD AP0512M"
    std::string serialNumber;        // Physical drive hardware serial number
    juce::int64 totalCapacityBytes = 0;
    juce::int64 freeBytes = 0;
    bool isRemovable = false;
    bool isInternal = false;
    bool isValid = false;
};

// Returns complete hardware identity for the volume containing `path`.
VolumeHardwareIdentity obterIdentidadeHardwareVolume(const juce::File& path);

// Lists all currently mounted volumes with their hardware identities.
std::vector<VolumeHardwareIdentity> listarVolumesMontados();

// Finds a currently mounted volume matching the given serial number or volume UUID.
VolumeHardwareIdentity encontrarVolumePorSerialOuUuid(const std::string& serialOrUuid);

} // namespace matriz::vault
