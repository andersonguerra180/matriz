// AudioToolbox ANTES do JUCE: com o `using namespace juce` do JuceHeader,
// Point/AudioBuffer dos headers do macOS ficariam ambíguos.
#if defined(__APPLE__)
 #include <AudioToolbox/AudioToolbox.h>
#elif defined(_WIN32) || defined(_WIN64)
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #pragma comment(lib, "mfplat.lib")
 #pragma comment(lib, "mfreadwrite.lib")
 #pragma comment(lib, "mfuuid.lib")
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
#elif JUCE_WINDOWS
namespace {

class LeitorAudioMF : public juce::AudioFormatReader {
public:
    LeitorAudioMF(juce::InputStream* stream, IMFSourceReader* reader, double sRate, unsigned int channels, juce::int64 totalSamples)
        : juce::AudioFormatReader(stream, "MediaFoundation audio"), reader_(reader) {
        sampleRate = sRate;
        numChannels = channels;
        bitsPerSample = 32;
        usesFloatingPointData = true;
        lengthInSamples = totalSamples;
        if (reader_) reader_->AddRef();
    }

    ~LeitorAudioMF() override {
        if (reader_) reader_->Release();
    }

    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
                     juce::int64 startSampleInFile, int numSamples) override {
        clearSamplesBeyondAvailableLength(destChannels, numDestChannels, startOffsetInDestBuffer, startSampleInFile,
                                          numSamples, lengthInSamples);
        if (numSamples <= 0 || !reader_) return true;

        if (startSampleInFile != posicao_) {
            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_I8;
            var.hVal.QuadPart = static_cast<LONGLONG>((startSampleInFile * 10000000.0) / sampleRate);
            reader_->SetCurrentPosition(GUID_NULL, var);
            PropVariantClear(&var);
            posicao_ = startSampleInFile;
        }

        const int canais = static_cast<int>(numChannels);
        intercalado_.resize(static_cast<size_t>(numSamples * canais));
        int lidos = 0;

        while (lidos < numSamples) {
            DWORD flags = 0;
            LONGLONG llTimeStamp = 0;
            IMFSample* pSample = nullptr;

            HRESULT hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &llTimeStamp, &pSample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
            if (!pSample) continue;

            IMFMediaBuffer* pBuffer = nullptr;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);
            if (SUCCEEDED(hr) && pBuffer) {
                BYTE* pData = nullptr;
                DWORD cbCurrentLen = 0;
                hr = pBuffer->Lock(&pData, nullptr, &cbCurrentLen);
                if (SUCCEEDED(hr) && pData) {
                    int framesInBuffer = static_cast<int>(cbCurrentLen / (canais * sizeof(float)));
                    int toCopy = std::min(framesInBuffer, numSamples - lidos);
                    if (toCopy > 0) {
                        std::memcpy(intercalado_.data() + (lidos * canais), pData, toCopy * canais * sizeof(float));
                        lidos += toCopy;
                    }
                    pBuffer->Unlock();
                }
                pBuffer->Release();
            }
            pSample->Release();
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
    IMFSourceReader* reader_ = nullptr;
    juce::int64 posicao_ = 0;
    std::vector<float> intercalado_;
};

} // namespace
#endif

FormatoAudioQuickTime::FormatoAudioQuickTime()
    : juce::AudioFormat("QuickTime audio", juce::StringArray(".mov", ".qt", ".mp4", ".m4a")) {}

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
#elif JUCE_WINDOWS
    if (auto* arquivoStream = dynamic_cast<juce::FileInputStream*>(stream)) {
        MFStartup(MF_VERSION);
        IMFSourceReader* pReader = nullptr;
        std::wstring wpath = arquivoStream->getFile().getFullPathName().toWideCharPointer();

        HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &pReader);
        if (SUCCEEDED(hr) && pReader) {
            IMFMediaType* pPartialType = nullptr;
            MFCreateMediaType(&pPartialType);
            pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            pPartialType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);

            hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPartialType);
            pPartialType->Release();

            if (SUCCEEDED(hr)) {
                IMFMediaType* pUncompressedAudioType = nullptr;
                hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pUncompressedAudioType);
                if (SUCCEEDED(hr) && pUncompressedAudioType) {
                    UINT32 sRate = 44100;
                    UINT32 channels = 2;
                    pUncompressedAudioType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sRate);
                    pUncompressedAudioType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
                    pUncompressedAudioType->Release();

                    PROPVARIANT var;
                    PropVariantInit(&var);
                    juce::int64 totalSamples = 0;
                    if (SUCCEEDED(pReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var))) {
                        LONGLONG duration100ns = var.hVal.QuadPart;
                        totalSamples = static_cast<juce::int64>((duration100ns * sRate) / 10000000LL);
                        PropVariantClear(&var);
                    }

                    return new LeitorAudioMF(stream, pReader, sRate, channels, totalSamples);
                }
            }
            pReader->Release();
        }
    }
#endif
    if (deleteStreamIfOpeningFails) delete stream;
    return nullptr;
}

void registrarFormatosDeAudio(juce::AudioFormatManager& gerenciador) {
    gerenciador.registerBasicFormats();
    gerenciador.registerFormat(new FormatoAudioQuickTime(), false);
#if JUCE_WINDOWS
    gerenciador.registerFormat(new juce::WindowsMediaFormat(), false);
#endif
}

} // namespace matriz::audio
