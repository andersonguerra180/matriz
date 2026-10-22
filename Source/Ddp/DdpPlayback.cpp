#include "DdpPlayback.h"
#include <algorithm>

namespace bkr
{

DdpPlayback::DdpPlayback()
{
    const auto erro = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (erro.isEmpty())
    {
        // O CD é 44,1 kHz: pede essa taxa ao dispositivo para o áudio não sair acelerado.
        auto setup = deviceManager.getAudioDeviceSetup();
        if (setup.sampleRate != 44100.0)
        {
            setup.sampleRate = 44100.0;
            deviceManager.setAudioDeviceSetup (setup, true);
        }
        deviceOk = true;
    }
    rawBuffer.resize (16384 * 4);
    deviceManager.addAudioCallback (this);
}

DdpPlayback::~DdpPlayback()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

bool DdpPlayback::loadDisc (const DdpDisc& newDisc)
{
    playing.store (false);
    const juce::ScopedLock sl (streamLock);
    disc = newDisc;
    isBigEndianAudio.store (disc.isBigEndian);
    currentSamplePosition.store (0);
    audioStream.reset();
    if (! disc.audioFile.existsAsFile()) return false;
    audioStream = std::make_unique<juce::FileInputStream> (disc.audioFile);
    if (! audioStream->openedOk()) { audioStream.reset(); return false; }
    return true;
}

void DdpPlayback::play()
{
    if (disc.isValid && audioStream != nullptr)
    {
        if (currentSamplePosition.load() >= disc.totalSamples) currentSamplePosition.store (0);
        playing.store (true);
    }
}

void DdpPlayback::pause() { playing.store (false); }

void DdpPlayback::stop()
{
    playing.store (false);
    currentSamplePosition.store (0);
}

void DdpPlayback::setPositionSample (int64_t samplePos)
{
    currentSamplePosition.store (juce::jlimit<int64_t> (0, disc.totalSamples, samplePos));
}

void DdpPlayback::playTrack (int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= (int) disc.tracks.size()) return;
    setPositionSample (disc.tracks[(size_t) trackIndex].index1Sample);
    play();
}

void DdpPlayback::previousTrack()
{
    const int idx = getCurrentTrackIndex();
    const int64_t pos = currentSamplePosition.load();
    if (idx > 0 && pos - disc.tracks[(size_t) idx].index1Sample <= 2 * 44100) playTrack (idx - 1);
    else playTrack (idx);
}

void DdpPlayback::nextTrack()
{
    const int idx = getCurrentTrackIndex();
    if (idx + 1 < (int) disc.tracks.size()) playTrack (idx + 1);
}

int DdpPlayback::getCurrentTrackIndex() const
{
    const int64_t pos = currentSamplePosition.load();
    for (int i = (int) disc.tracks.size() - 1; i >= 0; --i)
        if (pos >= disc.tracks[(size_t) i].index0Sample) return i;
    return 0;
}

void DdpPlayback::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    if (numOutputChannels == 0 || numSamples == 0) return;
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], numSamples);

    if (! playing.load() || ! disc.isValid) return;

    const juce::ScopedTryLock sl (streamLock);
    if (! sl.isLocked() || audioStream == nullptr) return;

    int64_t pos = currentSamplePosition.load();
    const bool isBe = isBigEndianAudio.load();
    int rendered = 0;

    while (rendered < numSamples && pos < disc.totalSamples)
    {
        int chunk = (int) std::min<int64_t> ({ (int64_t) (numSamples - rendered), disc.totalSamples - pos, (int64_t) (rawBuffer.size() / 4) });
        audioStream->setPosition (pos * 4);
        const int read = audioStream->read (rawBuffer.data(), chunk * 4) / 4;
        if (read <= 0) { playing.store (false); break; }
        for (int s = 0; s < read; ++s)
        {
            const uint8_t* p = &rawBuffer[(size_t) s * 4];
            const int16_t l = isBe ? (int16_t) ((p[0] << 8) | p[1]) : (int16_t) (p[0] | (p[1] << 8));
            const int16_t r = isBe ? (int16_t) ((p[2] << 8) | p[3]) : (int16_t) (p[2] | (p[3] << 8));
            if (out[0] != nullptr) out[0][rendered + s] = (float) l / 32768.0f;
            if (numOutputChannels > 1 && out[1] != nullptr) out[1][rendered + s] = (float) r / 32768.0f;
        }
        rendered += read;
        pos += read;
    }
    currentSamplePosition.store (pos);
    if (pos >= disc.totalSamples) playing.store (false);
}

} // namespace bkr
