// AudioToolbox ANTES do JUCE: com o `using namespace juce` do JuceHeader,
// Point/AudioBuffer dos headers do macOS ficariam ambíguos.
#if defined(__APPLE__)
 #include <AudioToolbox/AudioToolbox.h>
#endif

#include "FormatoAudioQuickTime.h"

#include <vector>

namespace matriz::audio {

#if JUCE_MAC
namespace {

// Decodifica pelo ExtAudioFile aberto por URL, entregando float intercalado
// e desintercalando pros canais do JUCE.
class LeitorAudioQuickTime : public juce::AudioFormatReader {
public:
    LeitorAudioQuickTime(juce::InputStream* stream, ExtAudioFileRef arquivo, const AudioStreamBasicDescription& formato,
                         juce::int64 quadros)
        : juce::AudioFormatReader(stream, "QuickTime audio"), arquivo_(arquivo) {
        sampleRate = formato.mSampleRate;
        numChannels = formato.mChannelsPerFrame;
        bitsPerSample = 32;
        usesFloatingPointData = true;
        lengthInSamples = quadros;
    }

    ~LeitorAudioQuickTime() override { ExtAudioFileDispose(arquivo_); }

    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
                     juce::int64 startSampleInFile, int numSamples) override {
        clearSamplesBeyondAvailableLength(destChannels, numDestChannels, startOffsetInDestBuffer, startSampleInFile,
                                          numSamples, lengthInSamples);
        if (numSamples <= 0) return true;

        if (startSampleInFile != posicao_) {
            if (ExtAudioFileSeek(arquivo_, startSampleInFile) != noErr) return false;
            posicao_ = startSampleInFile;
        }

        const int canais = static_cast<int>(numChannels);
        intercalado_.resize(static_cast<size_t>(numSamples * canais));
        int lidos = 0;
        while (lidos < numSamples) {
            AudioBufferList lista;
            lista.mNumberBuffers = 1;
            lista.mBuffers[0].mNumberChannels = static_cast<UInt32>(canais);
            lista.mBuffers[0].mDataByteSize = static_cast<UInt32>((numSamples - lidos) * canais * sizeof(float));
            lista.mBuffers[0].mData = intercalado_.data() + lidos * canais;
            UInt32 quadros = static_cast<UInt32>(numSamples - lidos);
            if (ExtAudioFileRead(arquivo_, &quadros, &lista) != noErr) return false;
            if (quadros == 0) break;
            lidos += static_cast<int>(quadros);
        }
        posicao_ += lidos;

        for (int c = 0; c < numDestChannels; ++c) {
            auto* destino = reinterpret_cast<float*>(destChannels[c]);
            if (destino == nullptr) continue;
            destino += startOffsetInDestBuffer;
            if (c >= canais) {
                juce::FloatVectorOperations::clear(destino, numSamples);
                continue;
            }
            for (int i = 0; i < lidos; ++i) destino[i] = intercalado_[static_cast<size_t>(i * canais + c)];
            if (lidos < numSamples) juce::FloatVectorOperations::clear(destino + lidos, numSamples - lidos);
        }
        return true;
    }

private:
    ExtAudioFileRef arquivo_;
    juce::int64 posicao_ = 0;
    std::vector<float> intercalado_;
};

} // namespace
#endif

FormatoAudioQuickTime::FormatoAudioQuickTime()
    : juce::AudioFormat("QuickTime audio", juce::StringArray(".mov", ".qt")) {}

juce::AudioFormatReader* FormatoAudioQuickTime::createReaderFor(juce::InputStream* stream,
                                                                bool deleteStreamIfOpeningFails) {
#if JUCE_MAC
    if (auto* arquivoStream = dynamic_cast<juce::FileInputStream*>(stream)) {
        const auto caminho = arquivoStream->getFile().getFullPathName();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            nullptr, reinterpret_cast<const UInt8*>(caminho.toRawUTF8()),
            static_cast<CFIndex>(caminho.getNumBytesAsUTF8()), false);
        ExtAudioFileRef arquivo = nullptr;
        const OSStatus erro = url ? ExtAudioFileOpenURL(url, &arquivo) : -1;
        if (url) CFRelease(url);

        if (erro == noErr && arquivo != nullptr) {
            AudioStreamBasicDescription formatoArquivo{};
            UInt32 tamanho = sizeof(formatoArquivo);
            SInt64 quadros = 0;
            UInt32 tamanhoQuadros = sizeof(quadros);
            bool ok = ExtAudioFileGetProperty(arquivo, kExtAudioFileProperty_FileDataFormat, &tamanho, &formatoArquivo) == noErr
                      && formatoArquivo.mChannelsPerFrame > 0 && formatoArquivo.mSampleRate > 0
                      && ExtAudioFileGetProperty(arquivo, kExtAudioFileProperty_FileLengthFrames, &tamanhoQuadros, &quadros) == noErr
                      && quadros > 0;
            if (ok) {
                AudioStreamBasicDescription cliente{};
                cliente.mSampleRate = formatoArquivo.mSampleRate;
                cliente.mFormatID = kAudioFormatLinearPCM;
                cliente.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
                cliente.mChannelsPerFrame = formatoArquivo.mChannelsPerFrame;
                cliente.mBitsPerChannel = 32;
                cliente.mFramesPerPacket = 1;
                cliente.mBytesPerFrame = 4 * cliente.mChannelsPerFrame;
                cliente.mBytesPerPacket = cliente.mBytesPerFrame;
                ok = ExtAudioFileSetProperty(arquivo, kExtAudioFileProperty_ClientDataFormat, sizeof(cliente), &cliente) == noErr;
            }
            if (ok) return new LeitorAudioQuickTime(stream, arquivo, formatoArquivo, quadros);
            ExtAudioFileDispose(arquivo);
        }
    }
#endif
    if (deleteStreamIfOpeningFails) delete stream;
    return nullptr;
}

void registrarFormatosDeAudio(juce::AudioFormatManager& gerenciador) {
    gerenciador.registerBasicFormats();
    gerenciador.registerFormat(new FormatoAudioQuickTime(), false);
}

} // namespace matriz::audio
