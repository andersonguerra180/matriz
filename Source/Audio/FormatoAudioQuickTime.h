#pragma once

#include <JuceHeader.h>

namespace matriz::audio {

// Áudio de .mov/.qt (vídeo QuickTime). O CoreAudioFormat do JUCE abre o
// arquivo por callbacks (AudioFileOpenWithCallbacks), e o CoreAudio não
// aceita QuickTime por esse caminho (-4, unimpErr) — só pela URL. Sem isto
// o preview de um .mov ficava sem forma de onda e sem som. Só funciona com
// FileInputStream (arquivo em disco): é o que a timeline e o AudioThumbnail
// (FileInputSource) entregam.
class FormatoAudioQuickTime : public juce::AudioFormat {
public:
    FormatoAudioQuickTime();

    juce::Array<int> getPossibleSampleRates() override { return {}; }
    juce::Array<int> getPossibleBitDepths() override { return {}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }

    juce::AudioFormatReader* createReaderFor(juce::InputStream* stream, bool deleteStreamIfOpeningFails) override;

    std::unique_ptr<juce::AudioFormatWriter> createWriterFor(std::unique_ptr<juce::OutputStream>&,
                                                             const juce::AudioFormatWriterOptions&) override {
        return nullptr;
    }
};

// registerBasicFormats() + FormatoAudioQuickTime.
void registrarFormatosDeAudio(juce::AudioFormatManager& gerenciador);

} // namespace matriz::audio
