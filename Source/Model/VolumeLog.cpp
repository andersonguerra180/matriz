#include "VolumeLog.h"

#include <mutex>

#include "../Vault/SmartHealth.h"
#include "../Vault/Volume.h"

namespace matriz::model::volumelog {

namespace {

std::mutex& mutexDoLog() {
    static std::mutex m;
    return m;
}

juce::String nomeDaUnidade(const juce::File& raiz) {
    juce::String nome = raiz.getFileName();
    if (nome.isEmpty() || raiz.getFullPathName() == "/") nome = raiz.getVolumeLabel();
    if (raiz.getFullPathName().endsWith("Mobile Documents")) nome = "iCloud Drive";
    return nome.isNotEmpty() ? nome : raiz.getFullPathName();
}

juce::String agoraTexto() { return juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S"); }

juce::String tipoDaUnidade(const juce::File& raiz, bool nuvem) {
    if (nuvem) return "Cloud";
    if (!raiz.exists()) return "Unknown (offline)";
    if (raiz.isOnCDRomDrive()) return "Optical (CD/DVD)";
    if (raiz.isOnRemovableDrive()) return "USB / removable (pen drive, SD card, external disk)";
    if (raiz.isOnHardDisk()) return "Disk (internal or external hard drive / SSD)";
    return "Unknown";
}

void gravarLinhaSemTrava(const juce::File& arquivo, const juce::File& raiz, const juce::String& linha) {
    arquivo.getParentDirectory().createDirectory();
    if (!arquivo.existsAsFile()) {
        arquivo.appendText("# Volume log - " + nomeDaUnidade(raiz) + "\n# " + raiz.getFullPathName() +
                           "\n# Format: date time | role | action | detail\n",
                           false, false, "\n");
    }
    arquivo.appendText(linha + "\n", false, false, "\n");
}

juce::String unicaLinha(juce::String t) { return t.replace("\r", " ").replace("\n", " ; "); }

} // namespace

juce::File raizDoVolume(const juce::File& caminho, bool* nuvem) {
    if (nuvem) *nuvem = false;
    const juce::String texto = caminho.getFullPathName();
    juce::StringArray seg;
    for (const auto& p : juce::StringArray::fromTokens(texto, "/", "")) if (p.isNotEmpty()) seg.add(p);
    if (seg.size() >= 2 && seg[0] == "Volumes") return juce::File("/Volumes/" + seg[1]);
    if (seg.size() >= 5 && seg[0] == "Users" && seg[2] == "Library" && seg[3] == "CloudStorage") {
        if (nuvem) *nuvem = true;
        return juce::File("/Users/" + seg[1] + "/Library/CloudStorage/" + seg[4]);
    }
    if (seg.size() >= 4 && seg[0] == "Users" && seg[2] == "Library" && seg[3] == "Mobile Documents") {
        if (nuvem) *nuvem = true;
        return juce::File("/Users/" + seg[1] + "/Library/Mobile Documents");
    }
    return caminho.getFileName().isEmpty() ? caminho : juce::File("/");
}

juce::File pastaDeLogs(const juce::File& pastaProjeto) { return pastaProjeto.getChildFile("logs"); }

juce::File arquivoDeLog(const juce::File& pastaProjeto, const juce::File& raizVolume) {
    return pastaDeLogs(pastaProjeto).getChildFile("volumes").getChildFile(
        juce::File::createLegalFileName(nomeDaUnidade(raizVolume)) + ".log");
}

void registrar(const juce::File& pastaProjeto, const juce::File& caminhoNoVolume, const juce::String& papel,
               const juce::String& acao, const juce::String& detalhe) {
    try {
        if (!pastaProjeto.isDirectory()) return;
        const juce::File raiz = raizDoVolume(caminhoNoVolume);
        std::lock_guard<std::mutex> trava(mutexDoLog());
        gravarLinhaSemTrava(arquivoDeLog(pastaProjeto, raiz), raiz,
                            agoraTexto() + " | " + papel + " | " + acao + " | " + unicaLinha(detalhe));
    } catch (...) {
    }
}

void observar(const juce::File& pastaProjeto, const juce::File& raizVolume, const juce::String& papel) {
    try {
        if (!pastaProjeto.isDirectory()) return;
        const bool online = raizVolume.exists();
        const juce::int64 livre = online ? raizVolume.getBytesFreeOnVolume() : 0;
        const juce::int64 total = online ? raizVolume.getVolumeTotalSize() : 0;
        const juce::String estado = online ? "ONLINE" : "OFFLINE";
        const juce::String detalhe = online ? "total " + juce::File::descriptionOfSizeInBytes(total) + ", free " +
                                                  juce::File::descriptionOfSizeInBytes(livre)
                                            : "volume not mounted / not reachable";
        std::lock_guard<std::mutex> trava(mutexDoLog());
        const juce::File arq = arquivoDeLog(pastaProjeto, raizVolume);
        // Última foto: compara estado e espaço livre (em GB inteiros) e a idade.
        if (arq.existsAsFile()) {
            const auto linhas = juce::StringArray::fromLines(arq.loadFileAsString());
            for (int i = linhas.size() - 1; i >= 0; --i) {
                if (!linhas[i].contains("| STATUS |")) continue;
                const auto partes = juce::StringArray::fromTokens(linhas[i], "|", "");
                if (partes.size() >= 4) {
                    const auto quando = juce::Time::fromISO8601(partes[0].trim().replace(" ", "T"));
                    const bool mesmoEstado = partes[3].trim().startsWith(estado);
                    const bool mesmoLivre = !online || partes[3].contains("free " + juce::File::descriptionOfSizeInBytes(livre));
                    if (mesmoEstado && mesmoLivre && (juce::Time::getCurrentTime() - quando).inHours() < 24.0) return;
                }
                break;
            }
        }
        gravarLinhaSemTrava(arq, raizVolume, agoraTexto() + " | " + papel + " | STATUS | " + estado + " - " + detalhe);
    } catch (...) {
    }
}

juce::String relatorioDaUnidade(const juce::File& pastaProjeto, const juce::File& raizVolume) {
    juce::String r;
    bool nuvem = false;
    raizDoVolume(raizVolume, &nuvem);
    const bool online = raizVolume.exists();

    r << "VOLUME: " << nomeDaUnidade(raizVolume) << "\n";
    r << "Path: " << raizVolume.getFullPathName() << "\n\n";

    r << "== CHARACTERISTICS ==\n";
    r << "Status: " << (online ? "ONLINE" : "OFFLINE") << "\n";
    r << "Type: " << tipoDaUnidade(raizVolume, nuvem) << "\n";
    std::string bsd;
    if (online) {
        r << "Total size: " << juce::File::descriptionOfSizeInBytes(raizVolume.getVolumeTotalSize()) << "\n";
        r << "Available: " << juce::File::descriptionOfSizeInBytes(raizVolume.getBytesFreeOnVolume()) << "\n";
        try {
            const auto info = matriz::vault::descreverVolume(raizVolume);
            const auto& h = info.hardware;
            if (!h.fileSystem.empty()) r << "File system: " << juce::String(h.fileSystem) << "\n";
            if (!h.vendor.empty()) r << "Vendor: " << juce::String(h.vendor) << "\n";
            if (!h.model.empty()) r << "Model: " << juce::String(h.model) << "\n";
            if (!h.serialNumber.empty()) r << "Serial number: " << juce::String(h.serialNumber) << "\n";
            if (!h.volumeUuid.empty()) r << "Volume UUID: " << juce::String(h.volumeUuid) << "\n";
            if (!h.bsdDeviceNode.empty()) r << "Device: " << juce::String(h.bsdDeviceNode) << "\n";
            if (h.isValid) r << "Internal: " << (h.isInternal ? "yes" : "no") << " | Removable: " << (h.isRemovable ? "yes" : "no") << "\n";
            bsd = h.bsdDeviceNode;
        } catch (...) {
        }
    } else {
        r << "(volume not connected: live details unavailable; the history below was recorded while it was in use)\n";
    }

    r << "\n== HEALTH ==\n";
    if (online && !nuvem) {
        try {
            const auto saude = matriz::vault::consultarSaudeSmart(bsd, raizVolume);
            r << "State: " << saude.stateLabel << "\n";
            r << "SMART status: " << saude.smartStatus << "\n";
            if (saude.temperatureC >= 0) r << "Temperature: " << saude.temperatureC << " C\n";
            if (saude.powerOnHours >= 0) r << "Power-on hours: " << saude.powerOnHours << "\n";
            if (saude.reallocatedSectors >= 0) r << "Reallocated sectors: " << saude.reallocatedSectors << "\n";
            if (saude.pendingSectors >= 0) r << "Pending sectors: " << saude.pendingSectors << "\n";
            if (saude.uncorrectableSectors >= 0) r << "Uncorrectable sectors: " << saude.uncorrectableSectors << "\n";
            if (saude.unavailableMessage.isNotEmpty()) r << saude.unavailableMessage << "\n";
        } catch (...) {
            r << "Health information unavailable for this volume.\n";
        }
    } else {
        r << "Health information unavailable (" << (nuvem ? "cloud storage" : "volume offline") << ").\n";
    }

    r << "\n== HISTORY (newest first) ==\n";
    const juce::File arq = arquivoDeLog(pastaProjeto, raizVolume);
    if (arq.existsAsFile()) {
        std::lock_guard<std::mutex> trava(mutexDoLog());
        auto linhas = juce::StringArray::fromLines(arq.loadFileAsString());
        int n = 0;
        for (int i = linhas.size() - 1; i >= 0; --i) {
            if (linhas[i].isEmpty() || linhas[i].startsWithChar('#')) continue;
            r << linhas[i] << "\n";
            ++n;
        }
        if (n == 0) r << "(no events recorded yet)\n";
        r << "\nLog file: " << arq.getFullPathName() << "\n";
    } else {
        r << "(no events recorded yet)\n";
    }
    return r;
}

} // namespace matriz::model::volumelog
