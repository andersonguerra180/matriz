#include "Pcm.h"

#include "ProcessoExterno.h"

#include <cstring>

namespace matriz::ingest {

std::vector<float> decodificarExcertoPcmMono(const juce::File& audio, const juce::File& dirTemporario,
                                              int sampleRate, double offsetSegundos, double duracaoSegundos) {
    dirTemporario.createDirectory();
    juce::File tmp = dirTemporario.getChildFile("matriz_excerto_" + juce::Uuid().toDashedString() + ".f32le");

    juce::StringArray args{"-y", "-hide_banner", "-loglevel", "error"};
    if (offsetSegundos > 0.0) {
        args.add("-ss");
        args.add(juce::String(offsetSegundos));
    }
    args.add("-i");
    args.add(audio.getFullPathName());
    args.add("-t");
    args.add(juce::String(duracaoSegundos));
    args.add("-f");
    args.add("f32le");
    args.add("-ac");
    args.add("1");
    args.add("-ar");
    args.add(juce::String(sampleRate));
    args.add(tmp.getFullPathName());

    rodarEsperandoSucesso("ffmpeg", args);

    std::vector<float> amostras;
    if (tmp.existsAsFile()) {
        juce::MemoryBlock bloco;
        tmp.loadFileAsData(bloco);
        size_t n = bloco.getSize() / sizeof(float);
        amostras.resize(n);
        std::memcpy(amostras.data(), bloco.getData(), n * sizeof(float));
        tmp.deleteFile();
    }
    return amostras;
}

} // namespace matriz::ingest
