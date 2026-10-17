#pragma once
#include <JuceHeader.h>
#include <vector>

namespace bkr
{

struct DdpTrack
{
    int trackNumber { 1 };
    juce::String title;
    juce::String performer;
    juce::String songwriter;
    juce::String composer;
    juce::String isrc;

    // CD Frames (75 frames/sec)
    int64_t index0Frame { 0 };
    int64_t index1Frame { 0 };
    int64_t endFrame { 0 };

    // Sample positions (44100 Hz, 588 samples/frame)
    int64_t index0Sample { 0 };
    int64_t index1Sample { 0 };
    int64_t endSample { 0 };

    int64_t pregapFrames() const { return index1Frame - index0Frame; }
    int64_t trackFrames()  const { return endFrame - index1Frame; }
    int64_t totalFrames()  const { return endFrame - index0Frame; }

    double pregapSeconds() const { return (double) pregapFrames() / 75.0; }
    double trackSeconds()  const { return (double) trackFrames() / 75.0; }
    double totalSeconds()  const { return (double) totalFrames() / 75.0; }

    bool hasPregapAudio { false };

    // Analysis results
    bool isAnalyzed { false };
    double integratedLufs { -100.0 };
    double maxTruePeakDb { -100.0 };
    double maxSamplePeakDb { -100.0 };
    int consecutiveClips { 0 };
};

struct DdpChecksum
{
    juce::String fileName;
    juce::String expectedMd5;
    juce::String calculatedMd5;
    bool matched { false };
    bool fileExists { false };
};

struct DdpDisc
{
    juce::File folder;
    juce::File audioFile;
    juce::File pqFile;
    juce::File cdtextFile;
    juce::File idFile;
    juce::File mapFile;

    juce::String formatVersion { "DDP 2.00" };
    juce::String albumTitle;
    juce::String discArtist;
    juce::String upcEan;

    int64_t totalFrames { 0 };
    int64_t totalSamples { 0 };
    double totalDurationSeconds { 0.0 };

    bool isBigEndian { false };
    bool autoDetectedBigEndian { false };
    bool manualEndianOverride { false };

    std::vector<DdpTrack> tracks;
    std::vector<DdpChecksum> checksums;
    std::vector<juce::String> warnings;

    bool isValid { false };
    bool hasHiddenTrack { false };

    // Disc-wide analysis results
    bool isAnalyzed { false };
    double integratedLufs { -100.0 };
    double maxTruePeakDb { -100.0 };
    double maxSamplePeakDb { -100.0 };
    int totalClips { 0 };

    void reset()
    {
        folder = juce::File();
        audioFile = juce::File();
        pqFile = juce::File();
        cdtextFile = juce::File();
        idFile = juce::File();
        mapFile = juce::File();
        formatVersion = "DDP 2.00";
        albumTitle.clear();
        discArtist.clear();
        upcEan.clear();
        totalFrames = 0;
        totalSamples = 0;
        totalDurationSeconds = 0.0;
        isBigEndian = false;
        autoDetectedBigEndian = false;
        manualEndianOverride = false;
        tracks.clear();
        checksums.clear();
        warnings.clear();
        isValid = false;
        hasHiddenTrack = false;
        isAnalyzed = false;
        integratedLufs = -100.0;
        maxTruePeakDb = -100.0;
        maxSamplePeakDb = -100.0;
        totalClips = 0;
    }
};

class DdpParser
{
public:
    static bool parseFolder (const juce::File& folder, DdpDisc& outDisc, juce::String& outError);
    static bool verifyChecksums (DdpDisc& disc);
    static bool detectEndianness (const juce::File& audioFile, bool& outIsBigEndian);
    static void checkPregapAudio (DdpDisc& disc);
    static juce::String formatCdTime (int64_t frames);
    static juce::String formatTimeMs (double seconds);

private:
    static bool parseDdpId (const juce::File& file, DdpDisc& disc);
    static bool parseDdpMs (const juce::File& file, DdpDisc& disc, std::map<juce::String, juce::File>& mappedFiles);
    static bool parsePqDescr (const juce::File& file, DdpDisc& disc);
    static bool parseCdText (const juce::File& file, DdpDisc& disc);
    static bool parseMd5Files (const juce::File& folder, DdpDisc& disc);
};

} // namespace bkr
