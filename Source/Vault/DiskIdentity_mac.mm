#import <Foundation/Foundation.h>
#include <sys/mount.h>
#include <sys/param.h>

#if defined(__APPLE__)
#include <DiskArbitration/DiskArbitration.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOBlockStorageDriver.h>

#ifndef kIOMainPortDefault
#define kIOMainPortDefault kIOMasterPortDefault
#endif
#endif

#include "DiskIdentity.h"
#include "SmartHealth.h"
#include "Volume.h"

namespace matriz::vault {

namespace {

static juce::String extrairDiscoFisicoBase(const juce::String& bsdNode) {
    if (bsdNode.isEmpty()) return {};
    juce::String bsd = bsdNode.trim();
    if (bsd.startsWith("/dev/")) bsd = bsd.substring(5);

    int sIdx = bsd.lastIndexOf("s");
    if (sIdx > 4 && bsd.substring(sIdx + 1).containsOnly("0123456789")) {
        bsd = bsd.substring(0, sIdx);
    }

    if (!bsd.startsWith("/dev/")) {
        return "/dev/" + bsd;
    }
    return bsd;
}

#if defined(__APPLE__)
juce::String cfStringToJuce(CFStringRef cfStr) {
    if (!cfStr) return {};
    return juce::String::fromCFString(cfStr).trim();
}

juce::String extrairPropriedadeString(io_registry_entry_t entry, CFStringRef chave) {
    if (!entry || !chave) return {};
    CFTypeRef prop = IORegistryEntryCreateCFProperty(entry, chave, kCFAllocatorDefault, 0);
    if (!prop) return {};

    juce::String res;
    if (CFGetTypeID(prop) == CFStringGetTypeID()) {
        res = cfStringToJuce((CFStringRef)prop);
    }
    CFRelease(prop);
    return res;
}

void inspecionarHardwareViaIOKit(const juce::String& bsdName,
                                 juce::String& outSerial,
                                 juce::String& outVendor,
                                 juce::String& outModel) {
    if (bsdName.isEmpty()) return;

    // Remove "/dev/" se presente
    juce::String bsd = bsdName;
    if (bsd.startsWith("/dev/")) bsd = bsd.substring(5);

    mach_port_t mainPort = 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    mainPort = kIOMasterPortDefault;
#pragma clang diagnostic pop

    io_service_t service = IOServiceGetMatchingService(mainPort,
                                                       IOBSDNameMatching(mainPort, 0, bsd.toRawUTF8()));

    // Se falhar, tenta sem a partição (ex: disk2s1 -> disk2)
    if (service == IO_OBJECT_NULL && bsd.contains("s")) {
        int sIndex = bsd.lastIndexOf("s");
        if (sIndex > 4) {
            juce::String bsdBase = bsd.substring(0, sIndex);
            service = IOServiceGetMatchingService(mainPort,
                                                  IOBSDNameMatching(mainPort, 0, bsdBase.toRawUTF8()));
        }
    }

    if (service == IO_OBJECT_NULL) return;

    io_registry_entry_t current = service;
    int maxDepth = 10;

    while (current != IO_OBJECT_NULL && maxDepth-- > 0) {
        // 1. Tenta ler Serial Number
        if (outSerial.isEmpty()) {
            outSerial = extrairPropriedadeString(current, CFSTR("Serial Number"));
            if (outSerial.isEmpty()) {
                outSerial = extrairPropriedadeString(current, CFSTR("Product Serial Number"));
            }
            if (outSerial.isEmpty()) {
                outSerial = extrairPropriedadeString(current, CFSTR("USB Serial Number"));
            }
            if (outSerial.isEmpty()) {
                outSerial = extrairPropriedadeString(current, CFSTR("IOPlatformSerialNumber"));
            }
        }

        // 2. Tenta ler Device Characteristics
        CFTypeRef devChars = IORegistryEntryCreateCFProperty(current,
                                                             CFSTR("Device Characteristics"),
                                                             kCFAllocatorDefault, 0);
        if (devChars != nullptr) {
            if (CFGetTypeID(devChars) == CFDictionaryGetTypeID()) {
                NSDictionary* dict = (__bridge NSDictionary*)devChars;
                if (outSerial.isEmpty() && dict[@"Serial Number"]) {
                    outSerial = juce::String::fromCFString((__bridge CFStringRef)dict[@"Serial Number"]).trim();
                }
                if (outVendor.isEmpty() && dict[@"Vendor Identification"]) {
                    outVendor = juce::String::fromCFString((__bridge CFStringRef)dict[@"Vendor Identification"]).trim();
                }
                if (outModel.isEmpty() && dict[@"Product Identification"]) {
                    outModel = juce::String::fromCFString((__bridge CFStringRef)dict[@"Product Identification"]).trim();
                }
            }
            CFRelease(devChars);
        }

        // 3. Tenta propriedades diretas no nó
        if (outVendor.isEmpty()) {
            outVendor = extrairPropriedadeString(current, CFSTR("Vendor Identification"));
            if (outVendor.isEmpty()) {
                outVendor = extrairPropriedadeString(current, CFSTR("Device Vendor"));
            }
        }
        if (outModel.isEmpty()) {
            outModel = extrairPropriedadeString(current, CFSTR("Product Identification"));
            if (outModel.isEmpty()) {
                outModel = extrairPropriedadeString(current, CFSTR("Device Model"));
            }
            if (outModel.isEmpty()) {
                outModel = extrairPropriedadeString(current, CFSTR("Product Name"));
            }
            if (outModel.isEmpty()) {
                outModel = extrairPropriedadeString(current, CFSTR("Model"));
            }
        }

        if (outSerial.isNotEmpty() && outVendor.isNotEmpty() && outModel.isNotEmpty()) {
            break;
        }

        io_registry_entry_t parent = IO_OBJECT_NULL;
        kern_return_t kr = IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent);
        if (current != service) {
            IOObjectRelease(current);
        }
        current = (kr == KERN_SUCCESS) ? parent : IO_OBJECT_NULL;
    }

    if (current != IO_OBJECT_NULL && current != service) {
        IOObjectRelease(current);
    }
    IOObjectRelease(service);
}
#endif

} // namespace

VolumeHardwareIdentity obterIdentidadeHardwareVolume(const juce::File& path) {
    VolumeHardwareIdentity id;
    juce::File alvo = path;
    if (alvo.getFullPathName().isEmpty()) return id;

#if defined(__APPLE__)
    struct statfs sfs;
    if (::statfs(alvo.getFullPathName().toRawUTF8(), &sfs) != 0) {
        return id;
    }

    id.mountPoint = sfs.f_mntonname;
    id.bsdDeviceNode = sfs.f_mntfromname;
    id.fileSystem = sfs.f_fstypename;
    id.totalCapacityBytes = static_cast<juce::int64>(sfs.f_blocks) * static_cast<juce::int64>(sfs.f_bsize);
    id.freeBytes = static_cast<juce::int64>(sfs.f_bavail) * static_cast<juce::int64>(sfs.f_bsize);
    id.isValid = true;

    // DiskArbitration details
    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    if (session != nullptr) {
        if (DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, sfs.f_mntfromname)) {
            if (CFDictionaryRef desc = DADiskCopyDescription(disk)) {
                // Volume UUID
                auto uuid = static_cast<CFUUIDRef>(CFDictionaryGetValue(desc, kDADiskDescriptionVolumeUUIDKey));
                if (uuid != nullptr) {
                    if (CFStringRef uuidStr = CFUUIDCreateString(kCFAllocatorDefault, uuid)) {
                        id.volumeUuid = juce::String::fromCFString(uuidStr).toStdString();
                        CFRelease(uuidStr);
                    }
                }

                // Volume Label / Name
                auto volName = static_cast<CFStringRef>(CFDictionaryGetValue(desc, kDADiskDescriptionVolumeNameKey));
                if (volName != nullptr) {
                    id.volumeLabel = cfStringToJuce(volName).toStdString();
                }

                // Fallback Model / Vendor from DA if available
                auto model = static_cast<CFStringRef>(CFDictionaryGetValue(desc, kDADiskDescriptionDeviceModelKey));
                if (model != nullptr && id.model.empty()) {
                    id.model = cfStringToJuce(model).toStdString();
                }

                auto vendor = static_cast<CFStringRef>(CFDictionaryGetValue(desc, kDADiskDescriptionDeviceVendorKey));
                if (vendor != nullptr && id.vendor.empty()) {
                    id.vendor = cfStringToJuce(vendor).toStdString();
                }

                // Removable / Internal
                auto internalVal = static_cast<CFBooleanRef>(CFDictionaryGetValue(desc, kDADiskDescriptionDeviceInternalKey));
                if (internalVal != nullptr) {
                    id.isInternal = CFBooleanGetValue(internalVal);
                }

                auto removableVal = static_cast<CFBooleanRef>(CFDictionaryGetValue(desc, kDADiskDescriptionMediaRemovableKey));
                if (removableVal != nullptr) {
                    id.isRemovable = CFBooleanGetValue(removableVal);
                }

                CFRelease(desc);
            }
            CFRelease(disk);
        }
        CFRelease(session);
    }

    if (id.volumeLabel.empty()) {
        juce::String fallbackName = juce::File(id.mountPoint).getFileName();
        id.volumeLabel = fallbackName.isEmpty() ? "System Drive" : fallbackName.toStdString();
    }

    // IOKit Hardware inspection for physical serial, vendor, and model
    juce::String serial, vendor, model;
    inspecionarHardwareViaIOKit(juce::String(id.bsdDeviceNode), serial, vendor, model);

    if (serial.isNotEmpty()) id.serialNumber = serial.toStdString();
    if (vendor.isNotEmpty() && id.vendor.empty()) id.vendor = vendor.toStdString();
    if (model.isNotEmpty() && id.model.empty()) id.model = model.toStdString();

    // If hardware serial is empty, fallback to volumeUuid as stable identifier
    if (id.serialNumber.empty() && !id.volumeUuid.empty()) {
        id.serialNumber = id.volumeUuid;
    }
#endif

    return id;
}

std::vector<VolumeHardwareIdentity> listarVolumesMontados() {
    std::vector<VolumeHardwareIdentity> volumes;
#if defined(__APPLE__)
    int numMounts = getfsstat(NULL, 0, MNT_NOWAIT);
    if (numMounts <= 0) return volumes;

    std::vector<struct statfs> fsList(numMounts);
    numMounts = getfsstat(fsList.data(), (int)(sizeof(struct statfs) * numMounts), MNT_NOWAIT);
    for (int i = 0; i < numMounts; ++i) {
        juce::String mountPoint = juce::String::fromUTF8(fsList[i].f_mntonname);
        // Ignore internal OS partitions (/System/Volumes/..., root /, /dev, etc.)
        if (mountPoint.isEmpty() || mountPoint == "/" || mountPoint.startsWith("/System/Volumes/")
            || mountPoint == "/dev" || mountPoint == "/net" || mountPoint == "/home"
            || mountPoint == "/Volumes/Macintosh HD") {
            continue;
        }

        // Only include user-accessible / external / removable storage in /Volumes/
        if (mountPoint.startsWith("/Volumes/")) {
            auto id = obterIdentidadeHardwareVolume(juce::File(mountPoint));
            if (id.isValid) {
                volumes.push_back(std::move(id));
            }
        }
    }
#endif
    return volumes;
}

VolumeHardwareIdentity encontrarVolumePorSerialOuUuid(const std::string& serialOrUuid) {
    if (serialOrUuid.empty()) return {};

    auto montados = listarVolumesMontados();
    for (const auto& v : montados) {
        if (!v.serialNumber.empty() && v.serialNumber == serialOrUuid) {
            return v;
        }
        if (!v.volumeUuid.empty() && v.volumeUuid == serialOrUuid) {
            return v;
        }
    }
    return {};
}

SmartHealthReport obterSaudeSmartNativoMac(const juce::File& path, const std::string& bsdNode) {
    SmartHealthReport rep;
    rep.lastScanTime = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M");

    juce::String bsd = extrairDiscoFisicoBase(juce::String(bsdNode));
    if (bsd.isEmpty() && path.exists()) {
        auto id = obterIdentidadeHardwareVolume(path);
        bsd = extrairDiscoFisicoBase(juce::String(id.bsdDeviceNode));
    }
    if (bsd.isEmpty() && bsdNode.rfind("/dev/", 0) != std::string::npos) {
        bsd = juce::String(bsdNode);
    }
    if (bsd.isEmpty()) {
        auto id = encontrarVolumePorSerialOuUuid(bsdNode);
        if (id.isValid) {
            bsd = extrairDiscoFisicoBase(juce::String(id.bsdDeviceNode));
        }
    }

    if (bsd.isEmpty()) {
        rep.state = HealthState::Unavailable;
        rep.stateLabel = "UNAVAILABLE";
        rep.stateColour = juce::Colour(0xff71717a);
        rep.unavailableMessage = "SMART data unavailable: physical disk node not found.";
        return rep;
    }

#if defined(__APPLE__)
    juce::String smartDaStatus;

    // 1. Direct macOS DiskArbitration query
    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    if (session != nullptr) {
        if (DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, bsd.toRawUTF8())) {
            if (CFDictionaryRef desc = DADiskCopyDescription(disk)) {
                auto smartVal = static_cast<CFStringRef>(CFDictionaryGetValue(desc, CFSTR("DAMediaSMARTStatus")));
                if (!smartVal) smartVal = static_cast<CFStringRef>(CFDictionaryGetValue(desc, CFSTR("MediaSMARTStatus")));
                if (!smartVal) smartVal = static_cast<CFStringRef>(CFDictionaryGetValue(desc, CFSTR("SMARTStatus")));
                if (smartVal != nullptr) {
                    smartDaStatus = cfStringToJuce(smartVal);
                }
                CFRelease(desc);
            }
            CFRelease(disk);
        }
        CFRelease(session);
    }

    // 2. Direct IOKit Registry query for telemetry (temperature, hours, sector counts)
    juce::String bsdShort = bsd;
    if (bsdShort.startsWith("/dev/")) bsdShort = bsdShort.substring(5);

    mach_port_t mainPort = 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    mainPort = kIOMasterPortDefault;
#pragma clang diagnostic pop

    io_service_t service = IOServiceGetMatchingService(mainPort,
                                                       IOBSDNameMatching(mainPort, 0, bsdShort.toRawUTF8()));
    if (service == IO_OBJECT_NULL && bsdShort.contains("s")) {
        int sIdx = bsdShort.lastIndexOf("s");
        if (sIdx > 4) {
            juce::String bsdBase = bsdShort.substring(0, sIdx);
            service = IOServiceGetMatchingService(mainPort,
                                                  IOBSDNameMatching(mainPort, 0, bsdBase.toRawUTF8()));
        }
    }

    if (service != IO_OBJECT_NULL) {
        io_registry_entry_t current = service;
        int maxDepth = 12;
        while (current != IO_OBJECT_NULL && maxDepth-- > 0) {
            if (smartDaStatus.isEmpty()) {
                smartDaStatus = extrairPropriedadeString(current, CFSTR("SMART Status"));
                if (smartDaStatus.isEmpty()) {
                    smartDaStatus = extrairPropriedadeString(current, CFSTR("SMARTStatus"));
                }
            }

            // Temperature
            if (rep.temperatureC < 0) {
                CFTypeRef tVal = IORegistryEntryCreateCFProperty(current, CFSTR("Temperature"), kCFAllocatorDefault, 0);
                if (!tVal) tVal = IORegistryEntryCreateCFProperty(current, CFSTR("Current Temperature"), kCFAllocatorDefault, 0);
                if (!tVal) tVal = IORegistryEntryCreateCFProperty(current, CFSTR("SMART Temperature"), kCFAllocatorDefault, 0);
                if (tVal != nullptr) {
                    if (CFGetTypeID(tVal) == CFNumberGetTypeID()) {
                        int t = 0;
                        CFNumberGetValue((CFNumberRef)tVal, kCFNumberIntType, &t);
                        if (t > 0 && t < 120) rep.temperatureC = t;
                    }
                    CFRelease(tVal);
                }
            }

            // Power-on hours
            if (rep.powerOnHours < 0) {
                CFTypeRef hVal = IORegistryEntryCreateCFProperty(current, CFSTR("Hours of Operation"), kCFAllocatorDefault, 0);
                if (!hVal) hVal = IORegistryEntryCreateCFProperty(current, CFSTR("Power-On Hours"), kCFAllocatorDefault, 0);
                if (!hVal) hVal = IORegistryEntryCreateCFProperty(current, CFSTR("PowerOnHours"), kCFAllocatorDefault, 0);
                if (hVal != nullptr) {
                    if (CFGetTypeID(hVal) == CFNumberGetTypeID()) {
                        long long h = 0;
                        CFNumberGetValue((CFNumberRef)hVal, kCFNumberLongLongType, &h);
                        if (h >= 0) rep.powerOnHours = static_cast<juce::int64>(h);
                    }
                    CFRelease(hVal);
                }
            }

            // Reallocated / Pending / Uncorrectable
            if (rep.reallocatedSectors < 0) {
                CFTypeRef rVal = IORegistryEntryCreateCFProperty(current, CFSTR("Reallocated Sector Count"), kCFAllocatorDefault, 0);
                if (!rVal) rVal = IORegistryEntryCreateCFProperty(current, CFSTR("ReallocatedSectors"), kCFAllocatorDefault, 0);
                if (rVal != nullptr) {
                    if (CFGetTypeID(rVal) == CFNumberGetTypeID()) {
                        long long r = 0;
                        CFNumberGetValue((CFNumberRef)rVal, kCFNumberLongLongType, &r);
                        if (r >= 0) rep.reallocatedSectors = static_cast<juce::int64>(r);
                    }
                    CFRelease(rVal);
                }
            }

            io_registry_entry_t parent = IO_OBJECT_NULL;
            kern_return_t kr = IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent);
            if (current != service) {
                IOObjectRelease(current);
            }
            current = (kr == KERN_SUCCESS) ? parent : IO_OBJECT_NULL;
        }

        if (current != IO_OBJECT_NULL && current != service) {
            IOObjectRelease(current);
        }
        IOObjectRelease(service);
    }

    // 3. Fallback to built-in macOS `diskutil info` (available on 100% of Macs natively)
    if (smartDaStatus.isEmpty()) {
        juce::ChildProcess proc;
        juce::StringArray args;
        args.add("/usr/sbin/diskutil");
        args.add("info");
        args.add("-plist");
        args.add(bsd);

        if (proc.start(args, juce::ChildProcess::wantStdOut)) {
            juce::String out = proc.readAllProcessOutput();
            proc.waitForProcessToFinish(2000);
            if (out.contains("<key>SMARTStatus</key>")) {
                int start = out.indexOf("<key>SMARTStatus</key>");
                int valStart = out.indexOf(start, "<string>");
                int valEnd = out.indexOf(valStart, "</string>");
                if (valStart >= 0 && valEnd > valStart) {
                    smartDaStatus = out.substring(valStart + 8, valEnd).trim();
                }
            }
        }
    }

    // 4. Map findings into SmartHealthReport
    if (smartDaStatus.equalsIgnoreCase("Verified") || smartDaStatus.equalsIgnoreCase("Passed")) {
        rep.smartStatus = "PASSED";
        rep.state = HealthState::Healthy;
        rep.stateLabel = "HEALTHY";
        rep.stateColour = juce::Colour(0xff22c55e);

        if (rep.reallocatedSectors < 0) rep.reallocatedSectors = 0;
        if (rep.pendingSectors < 0) rep.pendingSectors = 0;
        if (rep.uncorrectableSectors < 0) rep.uncorrectableSectors = 0;

        // If warning condition
        if (rep.reallocatedSectors > 0 || rep.pendingSectors > 0 || rep.uncorrectableSectors > 0 || rep.temperatureC > 60) {
            rep.state = HealthState::Warning;
            rep.stateLabel = "WARNING";
            rep.stateColour = juce::Colour(0xffeab308);
        }
    } else if (smartDaStatus.equalsIgnoreCase("Failing") || smartDaStatus.equalsIgnoreCase("Failed")) {
        rep.smartStatus = "FAILING";
        rep.state = HealthState::Failing;
        rep.stateLabel = "FAILING";
        rep.stateColour = juce::Colour(0xffef4444);
    } else if (smartDaStatus.equalsIgnoreCase("Not Supported") || smartDaStatus.isEmpty()) {
        // If volume is mounted and active on disk, report healthy online status with SMART Not Supported
        bool isOnlineVolume = path.exists() || (bsd.isNotEmpty() && bsd != "/dev/");
        if (isOnlineVolume) {
            rep.state = HealthState::Healthy;
            rep.stateLabel = "HEALTHY";
            rep.stateColour = juce::Colour(0xff22c55e);
            rep.smartStatus = "NOT SUPPORTED";
            if (rep.reallocatedSectors < 0) rep.reallocatedSectors = 0;
            if (rep.pendingSectors < 0) rep.pendingSectors = 0;
            if (rep.uncorrectableSectors < 0) rep.uncorrectableSectors = 0;
        } else {
            rep.state = HealthState::Unavailable;
            rep.stateLabel = "UNAVAILABLE";
            rep.stateColour = juce::Colour(0xff71717a);
            rep.smartStatus = "NOT SUPPORTED";
            rep.unavailableMessage = "Storage device is not mounted or offline.";
        }
    } else {
        rep.state = HealthState::Healthy;
        rep.stateLabel = "HEALTHY";
        rep.stateColour = juce::Colour(0xff22c55e);
        rep.smartStatus = smartDaStatus;
    }
#endif

    return rep;
}

} // namespace matriz::vault
