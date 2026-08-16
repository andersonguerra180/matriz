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

namespace matriz::vault {

namespace {

bool statfsDe(const juce::File& caminho, struct statfs& out) {
    if (caminho.getFullPathName().isEmpty()) return false;
    return ::statfs(caminho.getFullPathName().toRawUTF8(), &out) == 0;
}

} // namespace

juce::File pontoDeMontagem(const juce::File& caminho) {
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
    if (nome.isEmpty()) nome = "System Drive";  // "/" não tem nome de arquivo
    info.nome = nome.toStdString();
    return info;
}

} // namespace matriz::vault
