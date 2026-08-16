#include "Pcm.h"

#include <algorithm>
#include <vector>

namespace matriz::ingest {

std::vector<float> decodificarExcertoPcmMono(const juce::File& audio, const juce::File& /*dirTemporario*/,
                                              int sampleRate, double offsetSegundos, double duracaoSegundos) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audio));
    if (reader == nullptr)
        return {};

    double fileSampleRate = reader->sampleRate;
    juce::int64 startSample = static_cast<juce::int64>(offsetSegundos * fileSampleRate);
    juce::int64 numSamples = static_cast<juce::int64>(duracaoSegundos * fileSampleRate);

    if (startSample >= reader->lengthInSamples)
        return {};
    if (startSample + numSamples > reader->lengthInSamples)
        numSamples = reader->lengthInSamples - startSample;

    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), static_cast<int>(numSamples));
    reader->read(&buffer, 0, static_cast<int>(numSamples), startSample, true, true);

    std::vector<float> monoAmostras(static_cast<size_t>(numSamples));
    int numChannels = reader->numChannels;
    if (numChannels > 0) {
        for (juce::int64 i = 0; i < numSamples; ++i) {
            float soma = 0.0f;
            for (int c = 0; c < numChannels; ++c) {
                soma += buffer.getSample(c, static_cast<int>(i));
            }
            monoAmostras[static_cast<size_t>(i)] = soma / static_cast<float>(numChannels);
        }
    }

    if (fileSampleRate != sampleRate && numSamples > 0) {
        double ratio = fileSampleRate / static_cast<double>(sampleRate);
        juce::int64 targetSamples = static_cast<juce::int64>(numSamples / ratio);
        std::vector<float> resampled(static_cast<size_t>(targetSamples));
        for (juce::int64 i = 0; i < targetSamples; ++i) {
            double pos = i * ratio;
            juce::int64 idx1 = static_cast<juce::int64>(pos);
            juce::int64 idx2 = std::min(idx1 + 1, numSamples - 1);
            float frac = static_cast<float>(pos - idx1);
            resampled[static_cast<size_t>(i)] = (1.0f - frac) * monoAmostras[static_cast<size_t>(idx1)] + frac * monoAmostras[static_cast<size_t>(idx2)];
        }
        return resampled;
    }

    return monoAmostras;
}

} // namespace matriz::ingest
