#include "DiskIdentity.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <winioctl.h>
#include <ntddstor.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace matriz::vault {

namespace {

std::string trimString(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string getVolumeGuid(const std::wstring& rootPath) {
    WCHAR volumeName[MAX_PATH] = {0};
    if (GetVolumeNameForVolumeMountPointW(rootPath.c_str(), volumeName, MAX_PATH)) {
        // volumeName is "\\?\Volume{guid}\"
        std::wstring ws(volumeName);
        auto start = ws.find(L'{');
        auto end = ws.find(L'}');
        if (start != std::wstring::npos && end != std::wstring::npos) {
            std::wstring guid = ws.substr(start + 1, end - start - 1);
            return juce::String(guid.c_str()).toStdString();
        }
        return juce::String(volumeName).toStdString();
    }
    return "";
}

void getStorageProperty(const std::wstring& volumeRoot, VolumeHardwareIdentity& ident) {
    // Tenta abrir o volume para consultar IOCTL_STORAGE_QUERY_PROPERTY
    std::wstring volPath = L"\\\\.\\" + volumeRoot;
    if (volPath.back() == L'\\') volPath.pop_back();

    HANDLE hDevice = CreateFileW(volPath.c_str(), 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
    if (hDevice == INVALID_HANDLE_VALUE) return;

    STORAGE_PROPERTY_QUERY query = {};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    BYTE buffer[1024] = {0};
    DWORD bytesReturned = 0;
    if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
                        &query, sizeof(query),
                        buffer, sizeof(buffer),
                        &bytesReturned, nullptr)) {
        STORAGE_DEVICE_DESCRIPTOR* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer);
        if (desc->VendorIdOffset != 0 && desc->VendorIdOffset < bytesReturned) {
            ident.vendor = trimString(reinterpret_cast<char*>(buffer + desc->VendorIdOffset));
        }
        if (desc->ProductIdOffset != 0 && desc->ProductIdOffset < bytesReturned) {
            ident.model = trimString(reinterpret_cast<char*>(buffer + desc->ProductIdOffset));
        }
        if (desc->SerialNumberOffset != 0 && desc->SerialNumberOffset < bytesReturned) {
            ident.serialNumber = trimString(reinterpret_cast<char*>(buffer + desc->SerialNumberOffset));
        }
        ident.isRemovable = desc->RemovableMedia;
        ident.isInternal = (desc->BusType == BusTypeNvme || desc->BusType == BusTypeSata || desc->BusType == BusTypeAta);
    }
    CloseHandle(hDevice);
}

} // namespace

VolumeHardwareIdentity obterIdentidadeHardwareVolume(const juce::File& path) {
    VolumeHardwareIdentity ident;
    if (path == juce::File()) return ident;

    std::wstring fullPath = path.getFullPathName().toWideCharPointer();
    WCHAR volumePath[MAX_PATH] = {0};
    if (!GetVolumePathNameW(fullPath.c_str(), volumePath, MAX_PATH)) {
        return ident;
    }

    ident.mountPoint = juce::String(volumePath).toStdString();

    WCHAR volumeLabelBuf[MAX_PATH] = {0};
    WCHAR fileSystemBuf[MAX_PATH] = {0};
    DWORD serialNumber = 0;
    DWORD maxComponentLen = 0;
    DWORD flags = 0;

    if (GetVolumeInformationW(volumePath, volumeLabelBuf, MAX_PATH,
                              &serialNumber, &maxComponentLen, &flags,
                              fileSystemBuf, MAX_PATH)) {
        ident.volumeLabel = juce::String(volumeLabelBuf).toStdString();
        ident.fileSystem = juce::String(fileSystemBuf).toLowerCase().toStdString();
        
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << serialNumber;
        ident.volumeUuid = getVolumeGuid(volumePath);
        if (ident.volumeUuid.empty()) {
            ident.volumeUuid = ss.str();
        }
    }

    ULARGE_INTEGER freeBytesAvail = {}, totalBytes = {}, totalFree = {};
    if (GetDiskFreeSpaceExW(volumePath, &freeBytesAvail, &totalBytes, &totalFree)) {
        ident.totalCapacityBytes = static_cast<juce::int64>(totalBytes.QuadPart);
        ident.freeBytes = static_cast<juce::int64>(freeBytesAvail.QuadPart);
    }

    UINT driveType = GetDriveTypeW(volumePath);
    if (driveType == DRIVE_REMOVABLE || driveType == DRIVE_CDROM) {
        ident.isRemovable = true;
        ident.isInternal = false;
    } else if (driveType == DRIVE_FIXED) {
        ident.isInternal = true;
        ident.isRemovable = false;
    }

    ident.bsdDeviceNode = ident.mountPoint;
    getStorageProperty(volumePath, ident);

    ident.isValid = true;
    return ident;
}

std::vector<VolumeHardwareIdentity> listarVolumesMontados() {
    std::vector<VolumeHardwareIdentity> lista;
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1 << i)) {
            char driveLetter = 'A' + i;
            std::string root = std::string(1, driveLetter) + ":\\";
            VolumeHardwareIdentity id = obterIdentidadeHardwareVolume(juce::File(root));
            if (id.isValid) {
                lista.push_back(id);
            }
        }
    }
    return lista;
}

VolumeHardwareIdentity encontrarVolumePorSerialOuUuid(const std::string& serialOrUuid) {
    if (serialOrUuid.empty()) return {};
    auto volumes = listarVolumesMontados();
    for (const auto& v : volumes) {
        if (juce::String(v.volumeUuid).equalsIgnoreCase(juce::String(serialOrUuid)) ||
            juce::String(v.serialNumber).equalsIgnoreCase(juce::String(serialOrUuid))) {
            return v;
        }
    }
    return {};
}

} // namespace matriz::vault
#endif
