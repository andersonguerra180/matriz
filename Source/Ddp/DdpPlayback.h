#pragma once
#include <JuceHeader.h>
#include <atomic>

#include "DdpModel.h"

namespace bkr
{

// Toca a imagem de áudio de um DDP como se fosse um CD (44,1 kHz, 16 bits estéreo, PCM cru).
// Adaptado do BKR DDP Check (sem medidores nem modo de transição).
class DdpPlayback : public juce::AudioIODeviceCallback
{
public:
    DdpPlayback();
    ~DdpPlayback() override;

    bool loadDisc (const DdpDisc& disc);
    void play();
    void pause();
    void stop();
    bool isPlaying() const { return playing.load(); }

    void setPositionSample (int64_t samplePos);
    int64_t getPositionSample() const { return currentSamplePosition.load(); }

    void playTrack (int trackIndex);
    void previousTrack();
    void nextTrack();
    int getCurrentTrackIndex() const;

    void setBigEndian (bool isBe) { isBigEndianAudio.store (isBe); }
    bool isBigEndian() const { return isBigEndianAudio.load(); }
    bool audioDeviceOk() const { return deviceOk; }

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

private:
    juce::AudioDeviceManager deviceManager;
    bool deviceOk { false };

    DdpDisc disc;
    std::unique_ptr<juce::FileInputStream> audioStream;
    juce::CriticalSection streamLock;
    std::vector<uint8_t> rawBuffer;

    std::atomic<bool> playing { false };
    std::atomic<int64_t> currentSamplePosition { 0 };
    std::atomic<bool> isBigEndianAudio { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DdpPlayback)
};

} // namespace bkr
