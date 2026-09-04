// Os headers de sistema vêm ANTES de Volume.h de propósito: JuceHeader.h faz
// `using namespace juce`, e MacTypes.h (puxado por DiskArbitration ->
// CoreFoundation) declara um `Point` que fica ambíguo com juce::Point se
// entrar depois.
#include <limits.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/param.h>

#if defined(__APPLE__)
#include <DiskArbitration/DiskArbitration.h>
#endif

#include "Volume.h"
#include "../Db/Database.h"
#include "../Model/Project.h"

namespace matriz::vault {

namespace {

bool statfsDe(const juce::File& caminho, struct statfs& out) {
    if (caminho.getFullPathName().isEmpty()) return false;
    return ::statfs(caminho.getFullPathName().toRawUTF8(), &out) == 0;
}

} // namespace

juce::File pontoDeMontagem(const juce::File& caminho) {
    juce::String pathStr = caminho.getFullPathName();
    if (pathStr.startsWith("/Volumes/")) {
        juce::String volName = pathStr.substring(9).upToFirstOccurrenceOf("/", false, false);
        if (volName.isNotEmpty()) {
            return juce::File("/Volumes/" + volName);
        }
    }

    struct statfs sfs;
    if (!statfsDe(caminho, sfs)) return juce::File("/");
    return juce::File(juce::String::fromUTF8(sfs.f_mntonname));
}

std::string uuidDoVolume(const juce::File& caminho) {
#if JUCE_MAC
    struct statfs sfs;
    if (!statfsDe(caminho, sfs)) return {};

    DASessionRef sessao = DASessionCreate(kCFAllocatorDefault);
    if (sessao == nullptr) return {};

    std::string resultado;
    if (DADiskRef disco = DADiskCreateFromBSDName(kCFAllocatorDefault, sessao, sfs.f_mntfromname)) {
        if (CFDictionaryRef descricao = DADiskCopyDescription(disco)) {
            auto uuid = static_cast<CFUUIDRef>(
                CFDictionaryGetValue(descricao, kDADiskDescriptionVolumeUUIDKey));
            if (uuid != nullptr) {
                if (CFStringRef texto = CFUUIDCreateString(kCFAllocatorDefault, uuid)) {
                    resultado = juce::String::fromCFString(texto).toStdString();
                    CFRelease(texto);
                }
            }
            CFRelease(descricao);
        }
        CFRelease(disco);
    }
    CFRelease(sessao);
    return resultado;
#else
    juce::ignoreUnused(caminho);
    return {};
#endif
}

juce::File raizDoVault(const juce::File& caminho) {
    juce::String pathStr = caminho.getFullPathName();
    if (pathStr.startsWith("/Volumes/")) {
        juce::String volName = pathStr.substring(9).upToFirstOccurrenceOf("/", false, false);
        if (volName.isNotEmpty()) {
            return juce::File("/Volumes/" + volName);
        }
    }

    juce::File montagem = pontoDeMontagem(caminho);
    juce::String caminhoMontagem = montagem.getFullPathName();

    // Firmlink do macOS: o volume de dados monta em /System/Volumes/Data mas
    // o conteúdo é acessado por "/". A raiz útil é "/".
    if (caminhoMontagem == "/System/Volumes/Data") return juce::File("/");

    // Qualquer outro caso em que o caminho não fica sob o ponto de montagem
    // (montagem por link, caminho já canônico por outra via): "/" é o único
    // prefixo garantido.
    char resolvido[PATH_MAX];
    if (::realpath(caminho.getFullPathName().toRawUTF8(), resolvido) != nullptr) {
        juce::String canonico = juce::String::fromUTF8(resolvido);
        juce::String prefixo = caminhoMontagem.endsWithChar('/') ? caminhoMontagem : caminhoMontagem + "/";
        if (!canonico.startsWith(prefixo) && canonico != caminhoMontagem) return juce::File("/");
    }

    return montagem;
}

std::string caminhoRelativoAoVolume(const juce::File& arquivo) {
    // Canoniza os dois lados ANTES de comparar: statfs resolve links
    // simbólicos (devolve o ponto de montagem real), e o caminho do arquivo
    // pode ter chegado pela forma com link. Sem isso, a subtração de prefixo
    // falha e caímos no caminho absoluto sem necessidade.
    juce::File raiz = raizDoVault(arquivo);
    juce::String caminhoRaiz = raiz.getFullPathName();

    juce::String caminhoArquivo = arquivo.getFullPathName();
    char resolvido[PATH_MAX];
    if (::realpath(arquivo.getFullPathName().toRawUTF8(), resolvido) != nullptr)
        caminhoArquivo = juce::String::fromUTF8(resolvido);

    if (!caminhoRaiz.endsWithChar('/')) caminhoRaiz << '/';
    if (caminhoArquivo.startsWith(caminhoRaiz))
        return caminhoArquivo.substring(caminhoRaiz.length()).toStdString();

    // Fora da raiz: devolve o absoluto em vez de inventar um relativo que
    // sobe de nível.
    return caminhoArquivo.toStdString();
}

InfoVolume descreverVolume(const juce::File& caminho) {
    InfoVolume info;
    info.pontoMontagem = raizDoVault(caminho);
    info.uuid = uuidDoVolume(caminho);

    juce::String nome = info.pontoMontagem.getFileName();
    if (nome.isEmpty() || info.pontoMontagem.getFullPathName() == "/") {
        nome = "Macintosh HD";
    }
    info.nome = nome.toStdString();
    info.hardware = obterIdentidadeHardwareVolume(caminho);
    if (!info.hardware.volumeUuid.empty() && info.uuid.empty()) {
        info.uuid = info.hardware.volumeUuid;
    }
    if (info.hardware.volumeLabel.empty()) {
        info.hardware.volumeLabel = info.nome;
    }
    return info;
}

std::string inferirCategoriaDispositivo(const InfoVolume& volume) {
    const auto& hw = volume.hardware;
    juce::String fs = juce::String(hw.fileSystem).toLowerCase();

    // 1. Mídia óptica
    if (fs == "cddafs" || fs == "udf") {
        return "cd";
    }

    // Rede / SMB / NFS / AFP
    if (fs.startsWith("smb") || fs.startsWith("nfs") || fs.startsWith("afp")) {
        return "rede";
    }

    // 2. Disco interno
    if (hw.isInternal || volume.pontoMontagem.getFullPathName() == "/") {
        return "hd_interno";
    }

    // 3 & 4. Removível: Pen drive (< 256 GB) vs HD Externo (>= 256 GB)
    if (hw.isRemovable) {
        constexpr juce::int64 limitePenDriveBytes = 256LL * 1024 * 1024 * 1024;
        if (hw.totalCapacityBytes > 0 && hw.totalCapacityBytes < limitePenDriveBytes) {
            return "pen_drive";
        }
        return "hd_externo";
    }

    // Fallback se for /Volumes/ mas não marcado como removível pelo SO
    juce::String mountStr = volume.pontoMontagem.getFullPathName();
    if (mountStr.startsWith("/Volumes/") && mountStr != "/Volumes/Macintosh HD") {
        constexpr juce::int64 limitePenDriveBytes = 256LL * 1024 * 1024 * 1024;
        if (hw.totalCapacityBytes > 0 && hw.totalCapacityBytes < limitePenDriveBytes) {
            return "pen_drive";
        }
        return "hd_externo";
    }

    return "hd_interno";
}

std::string obterOuCriarVaultParaDestino(matriz::db::Database& registro,
                                        const juce::File& caminhoDestino,
                                        const std::string& projetoId) {
    using matriz::db::Value;
    auto volume = descreverVolume(caminhoDestino);
    std::string caminhoMontagem = volume.pontoMontagem.getFullPathName().toStdString();

    // Filter out OS pseudo-partitions and special virtual filesystems
    if (juce::String(caminhoMontagem).startsWith("/System/Volumes/")
        || caminhoMontagem == "/dev" || caminhoMontagem == "/net" || caminhoMontagem == "/home") {
        return {};
    }

    auto stmt = registro.prepare(
        "SELECT id, categoria_manual FROM vault WHERE (uuid_volume <> '' AND uuid_volume = ?) OR localizacao = ? LIMIT 1");
    stmt.bind(1, Value::of(volume.uuid));
    stmt.bind(2, Value::of(caminhoMontagem));

    std::string agora = matriz::model::agoraIso8601();
    bool isOnline = volume.pontoMontagem.isDirectory();
    std::string statusStr = isOnline ? "online" : "offline";

    if (stmt.step()) {
        std::string vaultId = stmt.columnText(0);
        int categoriaManual = stmt.columnInt(1);

        if (categoriaManual == 1) {
            // Respect user's manual category override — update only hardware details
            registro.run(
                "UPDATE vault SET vendor = ?, modelo = ?, numero_serie = ?, capacidade_bytes = ?, "
                "removivel = ?, sistema_arquivos = ?, status = ?, visto_em = ? WHERE id = ?",
                {Value::of(volume.hardware.vendor),
                 Value::of(volume.hardware.model),
                 Value::of(volume.hardware.serialNumber),
                 Value::of(volume.hardware.totalCapacityBytes),
                 Value::of(volume.hardware.isRemovable ? 1 : 0),
                 Value::of(volume.hardware.fileSystem),
                 Value::of(statusStr),
                 Value::of(agora),
                 Value::of(vaultId)});
        } else {
            // Re-infer category
            std::string categoriaInferida = inferirCategoriaDispositivo(volume);
            registro.run(
                "UPDATE vault SET vendor = ?, modelo = ?, numero_serie = ?, capacidade_bytes = ?, "
                "removivel = ?, sistema_arquivos = ?, categoria_dispositivo = ?, status = ?, visto_em = ? WHERE id = ?",
                {Value::of(volume.hardware.vendor),
                 Value::of(volume.hardware.model),
                 Value::of(volume.hardware.serialNumber),
                 Value::of(volume.hardware.totalCapacityBytes),
                 Value::of(volume.hardware.isRemovable ? 1 : 0),
                 Value::of(volume.hardware.fileSystem),
                 Value::of(categoriaInferida),
                 Value::of(statusStr),
                 Value::of(agora),
                 Value::of(vaultId)});
        }
        return vaultId;
    }

    std::string vaultId = matriz::model::novoUuid();
    std::string categoriaInferida = inferirCategoriaDispositivo(volume);
    std::string nomeAmigavel = volume.nome.empty() ? (caminhoMontagem == "/" ? "Macintosh HD" : "Storage Device") : volume.nome;

    registro.run(
        "INSERT INTO vault (id, projeto_id, nome, tipo, uuid_volume, raiz_relativa, localizacao, status, "
        "vendor, modelo, numero_serie, capacidade_bytes, removivel, sistema_arquivos, categoria_dispositivo, categoria_manual, visto_em, criado_em) "
        "VALUES (?, ?, ?, 'local', ?, '', ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?)",
        {Value::of(vaultId),
         Value::of(projetoId),
         Value::of(nomeAmigavel),
         Value::of(volume.uuid),
         Value::of(caminhoMontagem),
         Value::of(statusStr),
         Value::of(volume.hardware.vendor),
         Value::of(volume.hardware.model),
         Value::of(volume.hardware.serialNumber),
         Value::of(volume.hardware.totalCapacityBytes),
         Value::of(volume.hardware.isRemovable ? 1 : 0),
         Value::of(volume.hardware.fileSystem),
         Value::of(categoriaInferida),
         Value::of(agora),
         Value::of(agora)});

    return vaultId;
}

void sincronizarDrivesDoProjeto(matriz::db::Database& registro, const std::string& projetoId) {
    using matriz::db::Value;

    // Clean up OS pseudo-partitions that might have been recorded
    try {
        registro.run("DELETE FROM vault WHERE localizacao LIKE '/System/Volumes/%' OR localizacao IN ('/dev', '/net', '/home') OR nome IN ('dev', 'Preboot', 'Update', 'VM', 'xarts', 'Hardware');", {});
    } catch (...) {}

    std::string pid = projetoId;
    if (pid.empty()) {
        auto stmt = registro.prepare("SELECT id FROM projeto LIMIT 1");
        if (stmt.step()) pid = stmt.columnText(0);
    }

    // 1. Auto-discover and register all mounted storage devices in /Volumes/
    auto mountedVolumes = listarVolumesMontados();
    for (const auto& vol : mountedVolumes) {
        if (!vol.mountPoint.empty() && juce::String(vol.mountPoint).startsWith("/Volumes/")) {
            obterOuCriarVaultParaDestino(registro, juce::File(vol.mountPoint), pid);
        }
    }

    // 2. Scan all project files to ensure their physical source vaults are registered and linked
    try {
        struct ArqRef {
            std::string id;
            std::string absOrigem;
            std::string rel;
            std::string vaultId;
        };
        std::vector<ArqRef> arquivos;
        auto stmtArq = registro.prepare(
            "SELECT a.id, COALESCE(a.caminho_absoluto_origem, ''), COALESCE(a.caminho_relativo, ''), COALESCE(a.vault_id, '') "
            "FROM arquivo a "
            "WHERE a.vault_id IS NULL OR a.vault_id = '' OR NOT EXISTS (SELECT 1 FROM vault v WHERE v.id = a.vault_id);");

        while (stmtArq.step()) {
            arquivos.push_back({stmtArq.columnText(0), stmtArq.columnText(1), stmtArq.columnText(2), stmtArq.columnText(3)});
        }

        for (const auto& arq : arquivos) {
            juce::File f;
            if (!arq.absOrigem.empty()) {
                f = juce::File(arq.absOrigem);
            }
            if (!f.exists() && !arq.rel.empty()) {
                for (const auto& vol : mountedVolumes) {
                    juce::File cand = juce::File(vol.mountPoint).getChildFile(arq.rel);
                    if (cand.exists()) {
                        f = cand;
                        break;
                    }
                }
            }
            if (!arq.absOrigem.empty()) {
                juce::File target(arq.absOrigem);
                std::string vId = obterOuCriarVaultParaDestino(registro, target, pid);
                if (!vId.empty()) {
                    registro.run("UPDATE arquivo SET vault_id = ? WHERE id = ?", {Value::of(vId), Value::of(arq.id)});
                }
            } else if (f.exists()) {
                std::string vId = obterOuCriarVaultParaDestino(registro, f, pid);
                if (!vId.empty()) {
                    registro.run("UPDATE arquivo SET vault_id = ? WHERE id = ?", {Value::of(vId), Value::of(arq.id)});
                }
            }
        }
    } catch (...) {}
}

} // namespace matriz::vault
