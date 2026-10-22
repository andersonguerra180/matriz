#include "DdpModel.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bkr
{

static uint32_t readBigEndian32 (const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t readLittleEndian32 (const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int64_t msfToFrames (int mins, int secs, int frms)
{
    return (int64_t)mins * 60 * 75 + (int64_t)secs * 75 + (int64_t)frms;
}

juce::String DdpParser::formatCdTime (int64_t frames)
{
    if (frames < 0) frames = 0;
    const int64_t totalSecs = frames / 75;
    const int64_t mins = totalSecs / 60;
    const int64_t secs = totalSecs % 60;
    const int64_t remFrames = frames % 75;
    return juce::String::formatted ("%02d:%02d:%02d", (int)mins, (int)secs, (int)remFrames);
}

juce::String DdpParser::formatTimeMs (double seconds)
{
    if (seconds < 0.0) seconds = 0.0;
    const int mins = (int)(seconds / 60.0);
    const double secs = seconds - (mins * 60.0);
    return juce::String::formatted ("%02d:%05.2f", mins, secs);
}

bool DdpParser::parseFolder (const juce::File& folder, DdpDisc& outDisc, juce::String& outError)
{
    outDisc.reset();
    outDisc.folder = folder;

    if (! folder.isDirectory())
    {
        outError = "O caminho especificado nao e uma pasta valida.";
        return false;
    }

    std::map<juce::String, juce::File> mappedFiles;

    // 1. Locate and parse DDPID
    juce::Array<juce::File> files = folder.findChildFiles (juce::File::findFiles, false);
    for (auto& f : files)
    {
        if (f.getFileName().equalsIgnoreCase ("DDPID"))
            outDisc.idFile = f;
        else if (f.getFileName().equalsIgnoreCase ("DDPMS"))
            outDisc.mapFile = f;
        else if (f.getFileName().equalsIgnoreCase ("PQDESCR") || f.getFileName().equalsIgnoreCase ("PQDESCR.DAT"))
            outDisc.pqFile = f;
        else if (f.getFileName().equalsIgnoreCase ("CDTEXT.BIN") || f.getFileName().equalsIgnoreCase ("CDTEXT.DAT"))
            outDisc.cdtextFile = f;
        else if (f.getFileName().equalsIgnoreCase ("IMAGE.DAT") || f.getFileName().equalsIgnoreCase ("IMAGE.BIN")
                 || f.getFileName().equalsIgnoreCase ("MAIN.DAT") || f.getFileName().equalsIgnoreCase ("AUDIO.DAT"))
            outDisc.audioFile = f;
    }

    if (outDisc.idFile.existsAsFile())
        parseDdpId (outDisc.idFile, outDisc);
    else
        outDisc.warnings.push_back ("Aviso: Arquivo DDPID nao encontrado na pasta.");

    // 2. Parse DDPMS if present
    if (outDisc.mapFile.existsAsFile())
        parseDdpMs (outDisc.mapFile, outDisc, mappedFiles);

    // Fallback search for audio file if not identified yet
    if (! outDisc.audioFile.existsAsFile())
    {
        for (auto& f : files)
        {
            if (f.getFileExtension().equalsIgnoreCase (".dat") || f.getFileExtension().equalsIgnoreCase (".bin"))
            {
                if (! f.getFileName().equalsIgnoreCase ("CDTEXT.BIN") && ! f.getFileName().equalsIgnoreCase ("PQDESCR.DAT"))
                {
                    outDisc.audioFile = f;
                    break;
                }
            }
        }
    }

    if (! outDisc.audioFile.existsAsFile())
    {
        outError = "Nao foi possivel localizar o arquivo de audio da imagem DDP (IMAGE.DAT / MAIN.DAT / *.dat).";
        return false;
    }

    // 3. Fallback search for PQDESCR if not found
    if (! outDisc.pqFile.existsAsFile())
    {
        for (auto& f : files)
        {
            if (f.getFileName().containsIgnoreCase ("PQ"))
            {
                outDisc.pqFile = f;
                break;
            }
        }
    }

    if (! outDisc.pqFile.existsAsFile())
    {
        outError = "Arquivo de subcodigos e faixas (PQDESCR) nao foi encontrado no DDP.";
        return false;
    }

    // 4. Fallback search for CDTEXT.BIN
    if (! outDisc.cdtextFile.existsAsFile())
    {
        for (auto& f : files)
        {
            if (f.getFileName().containsIgnoreCase ("CDTEXT"))
            {
                outDisc.cdtextFile = f;
                break;
            }
        }
    }

    // Parse PQDESCR (tracks, pregaps, ISRCs, UPC)
    if (! parsePqDescr (outDisc.pqFile, outDisc))
    {
        outError = "Falha ao interpretar o arquivo PQDESCR.";
        return false;
    }

    if (outDisc.tracks.empty())
    {
        outError = "Nenhuma faixa de audio foi encontrada no descritor PQDESCR.";
        return false;
    }

    // Calculate total audio file samples/frames
    const int64_t audioBytes = outDisc.audioFile.getSize();
    outDisc.totalSamples = audioBytes / 4; // 16-bit stereo (4 bytes per sample)
    outDisc.totalFrames = outDisc.totalSamples / 588;
    outDisc.totalDurationSeconds = (double) outDisc.totalSamples / 44100.0;

    // Detect Endianness
    detectEndianness (outDisc.audioFile, outDisc.isBigEndian);
    outDisc.autoDetectedBigEndian = outDisc.isBigEndian;

    // Parse CD-Text if available
    if (outDisc.cdtextFile.existsAsFile())
        parseCdText (outDisc.cdtextFile, outDisc);
    else
        outDisc.warnings.push_back ("Disco sem arquivo CD-Text (CDTEXT.BIN). Usando nomes padrao.");

    // Fill default track titles if missing
    for (size_t i = 0; i < outDisc.tracks.size(); ++i)
    {
        auto& t = outDisc.tracks[i];
        if (t.title.trim().isEmpty())
            t.title = juce::String::formatted ("Faixa %02d", t.trackNumber);
        if (t.performer.trim().isEmpty() && outDisc.discArtist.isNotEmpty())
            t.performer = outDisc.discArtist;
    }

    // Verify Checksums (MD5)
    parseMd5Files (folder, outDisc);

    // Check Pregap Audio & Hidden Track
    checkPregapAudio (outDisc);

    outDisc.isValid = true;
    return true;
}

bool DdpParser::parseDdpId (const juce::File& file, DdpDisc& disc)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() == 0)
        return false;

    juce::String content = mb.toString().trim();
    if (content.startsWithIgnoreCase ("DDP"))
    {
        // DDPID é um registro de largura fixa: "DDP 2.00" + campos seguintes; só a versão interessa.
        disc.formatVersion = content.substring (0, 8).trim();
    }
    return true;
}

bool DdpParser::parseDdpMs (const juce::File& file, DdpDisc& disc, std::map<juce::String, juce::File>& mappedFiles)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() == 0)
        return false;

    // DDPMS typically contains 128-byte records
    const size_t numRecords = mb.getSize() / 128;
    const uint8_t* ptr = static_cast<const uint8_t*> (mb.getData());

    for (size_t r = 0; r < numRecords; ++r)
    {
        const uint8_t* rec = ptr + (r * 128);
        juce::String line = juce::String::fromUTF8 ((const char*)rec, 128).trim();
        juce::StringArray tokens;
        tokens.addTokens (line, " \t\r\n", "\"");
        tokens.removeEmptyStrings();

        if (tokens.size() > 0)
        {
            juce::String fname = tokens[0].trim();
            juce::File child = disc.folder.getChildFile (fname);
            if (child.existsAsFile())
            {
                mappedFiles[fname.toUpperCase()] = child;
                if (fname.containsIgnoreCase ("IMAGE") || fname.containsIgnoreCase ("MAIN") || fname.containsIgnoreCase ("AUDIO"))
                    disc.audioFile = child;
                else if (fname.containsIgnoreCase ("PQ"))
                    disc.pqFile = child;
                else if (fname.containsIgnoreCase ("CDTEXT"))
                    disc.cdtextFile = child;
            }
        }
    }
    return true;
}

struct RawPqRecord
{
    int track { 0 };
    int index { 0 };
    int64_t frame { 0 };
    juce::String isrc;
    juce::String upc;
    bool isLeadOut { false };
};

bool DdpParser::parsePqDescr (const juce::File& file, DdpDisc& disc)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() == 0)
        return false;

    disc.tracks.clear();
    std::vector<RawPqRecord> rawRecords;

    const size_t totalBytes = mb.getSize();
    const uint8_t* ptr = static_cast<const uint8_t*> (mb.getData());

    // Formato DDP 2.00 em texto: registros de 64 bytes "VVVS" + faixa(2) + índice(2) + 2 espaços + MMSSFF(6) + controle(2)
    // + 2 espaços + ISRC(12); faixa "AA" = lead-out; o registro 00 traz o UPC/EAN a partir do byte 32.
    const bool vvvs = totalBytes >= 64 && std::memcmp (ptr, "VVVS", 4) == 0;
    if (vvvs)
    {
        auto num = [] (const uint8_t* p, int n) { int v = 0; for (int i = 0; i < n; ++i) { if (p[i] < '0' || p[i] > '9') return -1; v = v * 10 + (p[i] - '0'); } return v; };
        for (size_t r = 0; r + 64 <= totalBytes; r += 64)
        {
            const uint8_t* rec = ptr + r;
            if (std::memcmp (rec, "VVVS", 4) != 0) continue;
            const bool leadOut = rec[4] == 'A' && rec[5] == 'A';
            const int trk = leadOut ? 0xAA : num (rec + 4, 2);
            const int idx = num (rec + 6, 2);
            if (trk < 0 || idx < 0) continue;
            const int mm = num (rec + 10, 2), ss = num (rec + 12, 2), ff = num (rec + 14, 2);
            const int64_t frame = (mm < 0 || ss < 0 || ff < 0) ? 0 : msfToFrames (mm, ss, ff);
            if (trk == 0)
            {
                juce::String upc = juce::String::fromUTF8 ((const char*) (rec + 32), 13).trim();
                juce::String digits;
                for (int i = 0; i < upc.length(); ++i) if (juce::CharacterFunctions::isDigit (upc[i])) digits += upc[i];
                if (digits.isNotEmpty()) disc.upcEan = digits;
                continue;
            }
            RawPqRecord raw;
            raw.track = trk;
            raw.index = idx;
            raw.frame = frame;
            raw.isLeadOut = leadOut;
            if (! leadOut)
            {
                juce::String isrc = juce::String::fromUTF8 ((const char*) (rec + 20), 12).trim();
                if (isrc.length() == 12) raw.isrc = isrc;
            }
            rawRecords.push_back (raw);
        }
    }

    // Check if it's binary 128-byte format
    if (! vvvs && totalBytes >= 128 && (totalBytes % 128 == 0 || totalBytes >= 512))
    {
        const size_t numRecords = totalBytes / 128;
        for (size_t r = 0; r < numRecords; ++r)
        {
            const uint8_t* rec = ptr + (r * 128);
            const uint8_t trk = rec[2];
            const uint8_t idx = rec[3];

            // Lead-in (Track 00)
            if (trk == 0x00 && r == 0)
            {
                // Catalog number (UPC/EAN)
                juce::String upcStr = juce::String::fromUTF8 ((const char*) (rec + 4), 13).trim();
                // Filter out non-numeric characters
                juce::String filteredUpc;
                for (int i = 0; i < upcStr.length(); ++i)
                {
                    if (juce::CharacterFunctions::isDigit (upcStr[i]))
                        filteredUpc += upcStr[i];
                }
                if (filteredUpc.isNotEmpty())
                    disc.upcEan = filteredUpc;
                continue;
            }

            // Lead-out (Track 0xAA or 170)
            if (trk == 0xAA || trk == 170 || trk == 0xA0)
            {
                uint32_t frameVal = readBigEndian32 (rec + 4);
                if (frameVal == 0 || frameVal > 80 * 60 * 75)
                {
                    // Try MSF
                    int mins = rec[8];
                    int secs = rec[9];
                    int frms = rec[10];
                    frameVal = (uint32_t) msfToFrames (mins, secs, frms);
                }
                RawPqRecord raw;
                raw.track = 0xAA;
                raw.index = idx;
                raw.frame = frameVal;
                raw.isLeadOut = true;
                rawRecords.push_back (raw);
                continue;
            }

            // Regular tracks (Track 1..99)
            if (trk >= 1 && trk <= 99)
            {
                uint32_t frameVal = readBigEndian32 (rec + 4);
                // Also check MSF in bytes 8..10 if frameVal is suspiciously 0 for late tracks
                if (frameVal == 0 && (rec[8] != 0 || rec[9] != 0 || rec[10] != 0))
                {
                    frameVal = (uint32_t) msfToFrames (rec[8], rec[9], rec[10]);
                }

                RawPqRecord raw;
                raw.track = trk;
                raw.index = idx;
                raw.frame = frameVal;

                // ISRC at bytes 16..27 (12 ASCII chars)
                juce::String isrcStr = juce::String::fromUTF8 ((const char*) (rec + 16), 12).trim();
                if (isrcStr.length() == 12)
                    raw.isrc = isrcStr;

                rawRecords.push_back (raw);
            }
        }
    }

    // If binary parsing didn't yield records, try line-based ASCII parsing
    if (rawRecords.empty() && ! vvvs)
    {
        juce::String content = mb.toString();
        juce::StringArray lines;
        lines.addLines (content);

        for (auto& line : lines)
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWith ("#") || line.startsWith (";"))
                continue;

            if (line.containsIgnoreCase ("UPC") || line.containsIgnoreCase ("CATALOG"))
            {
                juce::String upc = line.fromFirstOccurrenceOf (" ", false, false).trim().unquoted();
                disc.upcEan = upc;
                continue;
            }

            // Track/Index lines e.g.: TRK 01 IDX 01 00:02:00
            // or INDEX 01 00:00:00
            if (line.containsIgnoreCase ("TRK") || line.containsIgnoreCase ("TRACK") || line.containsIgnoreCase ("INDEX"))
            {
                // Parse tokens
                juce::StringArray tks;
                tks.addTokens (line, " \t", "\"");
                int trkNum = 1;
                int idxNum = 1;
                int64_t frameVal = 0;
                juce::String isrc;

                for (int i = 0; i < tks.size(); ++i)
                {
                    if (tks[i].equalsIgnoreCase ("TRK") || tks[i].equalsIgnoreCase ("TRACK"))
                    {
                        if (i + 1 < tks.size()) trkNum = tks[i + 1].getIntValue();
                    }
                    else if (tks[i].equalsIgnoreCase ("IDX") || tks[i].equalsIgnoreCase ("INDEX"))
                    {
                        if (i + 1 < tks.size()) idxNum = tks[i + 1].getIntValue();
                    }
                    else if (tks[i].equalsIgnoreCase ("ISRC"))
                    {
                        if (i + 1 < tks.size()) isrc = tks[i + 1];
                    }
                    else if (tks[i].contains (":"))
                    {
                        // Time string mm:ss:ff or mm:ss:mmm
                        auto parts = juce::StringArray::fromTokens (tks[i], ":", "");
                        if (parts.size() >= 3)
                        {
                            int m = parts[0].getIntValue();
                            int s = parts[1].getIntValue();
                            int f = parts[2].getIntValue();
                            frameVal = msfToFrames (m, s, f);
                        }
                    }
                }

                RawPqRecord raw;
                raw.track = trkNum;
                raw.index = idxNum;
                raw.frame = frameVal;
                raw.isrc = isrc;
                rawRecords.push_back (raw);
            }
        }
    }

    if (rawRecords.empty())
        return false;

    // Organize raw records into DdpTracks
    // Group by track number
    std::map<int, DdpTrack> trackMap;
    int64_t leadOutFrame = 0;

    for (auto& r : rawRecords)
    {
        if (r.isLeadOut || r.track == 0xAA)
        {
            leadOutFrame = r.frame;
            continue;
        }

        auto& trk = trackMap[r.track];
        trk.trackNumber = r.track;
        if (r.isrc.isNotEmpty()) trk.isrc = r.isrc;

        if (r.index == 0)
        {
            trk.index0Frame = r.frame;
            trk.index0Sample = r.frame * 588;
        }
        else if (r.index == 1)
        {
            trk.index1Frame = r.frame;
            trk.index1Sample = r.frame * 588;
            if (trk.index0Frame == 0 && r.track == 1)
            {
                // Track 1 pregap usually starts at 0 if not specified
                trk.index0Sample = trk.index0Frame * 588;
            }
            if (r.isrc.isNotEmpty())
                trk.isrc = r.isrc;
        }
    }

    // Convert map to sorted vector
    for (auto& pair : trackMap)
        disc.tracks.push_back (pair.second);

    std::sort (disc.tracks.begin(), disc.tracks.end(), [](const DdpTrack& a, const DdpTrack& b) {
        return a.trackNumber < b.trackNumber;
    });

    // Sem índice 0 explícito, o início da faixa (índice 0) é o índice 1 — antes de calcular o fim da faixa anterior.
    for (auto& tr : disc.tracks)
        if (tr.index0Frame == 0 && tr.trackNumber > 1)
            tr.index0Frame = tr.index1Frame;

    // Calculate end frame for each track
    for (size_t i = 0; i < disc.tracks.size(); ++i)
    {
        auto& current = disc.tracks[i];

        // If index0 was not explicitly given, it equals index1 (no pregap) or prev track end
        if (current.index0Frame == 0 && current.trackNumber > 1)
            current.index0Frame = current.index1Frame;

        if (i + 1 < disc.tracks.size())
        {
            // End frame of track i is index0 of track i+1
            current.endFrame = disc.tracks[i + 1].index0Frame;
        }
        else
        {
            // Last track ends at lead-out frame or total audio frames
            current.endFrame = leadOutFrame > 0 ? leadOutFrame : (disc.audioFile.getSize() / 4 / 588);
        }

        current.index0Sample = current.index0Frame * 588;
        current.index1Sample = current.index1Frame * 588;
        current.endSample = current.endFrame * 588;
    }

    return ! disc.tracks.empty();
}

bool DdpParser::parseCdText (const juce::File& file, DdpDisc& disc)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 18)
        return false;

    const uint8_t* data = static_cast<const uint8_t*> (mb.getData());
    size_t size = mb.getSize();

    // Check for optional 4-byte header (e.g. CDTEXT length prefix)
    size_t offset = 0;
    if ((size % 18) == 4)
        offset = 4;

    std::map<int, juce::String> titleMap;
    std::map<int, juce::String> artistMap;
    std::map<int, juce::String> songwriterMap;
    std::map<int, juce::String> composerMap;
    std::map<int, juce::String> isrcMap;

    // Helper accumulator for multi-pack text
    int currentPackType = -1;
    int currentTrackIndex = -1;
    juce::MemoryOutputStream textStream;

    auto flushText = [&]() {
        if (currentPackType >= 0 && currentTrackIndex >= 0)
        {
            // CD-Text costuma ser ISO 8859-1 (ç, ã, é...); se os bytes forem UTF-8 válido, vale UTF-8.
            const auto* bytes = (const char*) textStream.getData();
            const int n = (int) textStream.getDataSize();
            juce::String str;
            if (juce::CharPointer_UTF8::isValidString (bytes, n))
                str = juce::String::fromUTF8 (bytes, n);
            else
                for (int i = 0; i < n; ++i) str += (juce::juce_wchar) (uint8_t) bytes[i];
            str = str.trim();
            if (currentPackType == 0x80)
            {
                if (currentTrackIndex == 0) disc.albumTitle = str;
                else titleMap[currentTrackIndex] = str;
            }
            else if (currentPackType == 0x81)
            {
                if (currentTrackIndex == 0) disc.discArtist = str;
                else artistMap[currentTrackIndex] = str;
            }
            else if (currentPackType == 0x82)
            {
                songwriterMap[currentTrackIndex] = str;
            }
            else if (currentPackType == 0x83)
            {
                composerMap[currentTrackIndex] = str;
            }
            else if (currentPackType == 0x8E)
            {
                if (currentTrackIndex == 0 && disc.upcEan.isEmpty()) disc.upcEan = str;
                else isrcMap[currentTrackIndex] = str;
            }
        }
        textStream.reset();
    };

    while (offset + 18 <= size)
    {
        const uint8_t* pack = data + offset;
        const uint8_t type = pack[0];
        const uint8_t track = pack[1];
        // const uint8_t seq = pack[2];
        // const uint8_t charPos = pack[3];
        const uint8_t* payload = pack + 4; // 12 bytes

        if (type == 0x80 || type == 0x81 || type == 0x82 || type == 0x83 || type == 0x8E)
        {
            if (type != currentPackType)
            {
                flushText();
                currentPackType = type;
                currentTrackIndex = track;
            }

            for (int b = 0; b < 12; ++b)
            {
                uint8_t c = payload[b];
                if (c == 0x00)
                {
                    flushText();
                    currentTrackIndex++;
                }
                else
                {
                    textStream.writeByte ((char) c);
                }
            }
        }

        offset += 18;
    }
    flushText();

    // Assign parsed CD-Text to tracks
    for (auto& t : disc.tracks)
    {
        if (titleMap.find (t.trackNumber) != titleMap.end() && titleMap[t.trackNumber].isNotEmpty())
            t.title = titleMap[t.trackNumber];
        if (artistMap.find (t.trackNumber) != artistMap.end() && artistMap[t.trackNumber].isNotEmpty())
            t.performer = artistMap[t.trackNumber];
        if (songwriterMap.find (t.trackNumber) != songwriterMap.end())
            t.songwriter = songwriterMap[t.trackNumber];
        if (composerMap.find (t.trackNumber) != composerMap.end())
            t.composer = composerMap[t.trackNumber];
        if (isrcMap.find (t.trackNumber) != isrcMap.end() && isrcMap[t.trackNumber].isNotEmpty())
            t.isrc = isrcMap[t.trackNumber];
    }

    if (disc.albumTitle.isEmpty())
        disc.albumTitle = disc.folder.getFileName();

    return true;
}

bool DdpParser::detectEndianness (const juce::File& audioFile, bool& outIsBigEndian)
{
    outIsBigEndian = false;
    juce::FileInputStream stream (audioFile);
    if (! stream.openedOk())
        return false;

    const int64_t totalBytes = audioFile.getSize();
    if (totalBytes < 4096)
        return false;

    // Sample from 10% to 50% into the file to avoid lead-in absolute silence
    int64_t startPos = totalBytes / 4;
    startPos -= (startPos % 4); // align to 4-byte sample frame
    stream.setPosition (startPos);

    const int numSamples = 8192;
    std::vector<uint8_t> buffer (numSamples * 4);
    int bytesRead = stream.read (buffer.data(), (int) buffer.size());
    int samplesRead = bytesRead / 4;

    if (samplesRead < 1024)
        return false;

    double sumDeltaLe = 0.0;
    double sumDeltaBe = 0.0;

    int16_t prevLeL = (int16_t) (buffer[0] | (buffer[1] << 8));
    int16_t prevBeL = (int16_t) ((buffer[0] << 8) | buffer[1]);

    for (int i = 1; i < samplesRead; ++i)
    {
        int byteIdx = i * 4;
        uint8_t b0 = buffer[byteIdx];
        uint8_t b1 = buffer[byteIdx + 1];

        int16_t sLeL = (int16_t) (b0 | (b1 << 8));
        int16_t sBeL = (int16_t) ((b0 << 8) | b1);

        sumDeltaLe += std::abs ((double) sLeL - (double) prevLeL);
        sumDeltaBe += std::abs ((double) sBeL - (double) prevBeL);

        prevLeL = sLeL;
        prevBeL = sBeL;
    }

    double avgDeltaLe = sumDeltaLe / (samplesRead - 1);
    double avgDeltaBe = sumDeltaBe / (samplesRead - 1);

    // If Big Endian delta is noticeably smaller than Little Endian delta, it's Big Endian
    if (avgDeltaBe < avgDeltaLe * 0.6)
    {
        outIsBigEndian = true;
    }
    else
    {
        outIsBigEndian = false;
    }

    return true;
}

bool DdpParser::parseMd5Files (const juce::File& folder, DdpDisc& disc)
{
    disc.checksums.clear();
    juce::Array<juce::File> md5Files;

    for (auto& f : folder.findChildFiles (juce::File::findFiles, false))
    {
        if (f.getFileExtension().equalsIgnoreCase (".md5") || f.getFileName().containsIgnoreCase ("CHECKSUM")
            || f.getFileName().equalsIgnoreCase ("MD5SUMS"))
        {
            md5Files.add (f);
        }
    }

    if (md5Files.isEmpty())
        return false;

    for (auto& md5File : md5Files)
    {
        juce::StringArray lines;
        lines.addLines (md5File.loadFileAsString());

        for (auto& line : lines)
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWith ("#") || line.startsWith (";"))
                continue;

            juce::String hash;
            juce::String fname;

            // BSD format: MD5 (filename) = hash
            if (line.startsWithIgnoreCase ("MD5 ("))
            {
                fname = line.fromFirstOccurrenceOf ("MD5 (", false, false).upToFirstOccurrenceOf (")", false, false);
                hash = line.fromLastOccurrenceOf ("=", false, false).trim();
            }
            else
            {
                // Standard: <hash>  <filename> or <hash> *<filename>
                int spaceIdx = line.indexOfAnyOf (" \t");
                if (spaceIdx > 0)
                {
                    hash = line.substring (0, spaceIdx).trim();
                    fname = line.substring (spaceIdx).trim();
                    if (fname.startsWith ("*"))
                        fname = fname.substring (1).trim();
                }
            }

            if (hash.length() == 32 && fname.isNotEmpty())
            {
                DdpChecksum cs;
                cs.fileName = fname;
                cs.expectedMd5 = hash.toLowerCase();
                juce::File targetFile = folder.getChildFile (fname);
                cs.fileExists = targetFile.existsAsFile();
                disc.checksums.push_back (cs);
            }
        }
    }

    return verifyChecksums (disc);
}

bool DdpParser::verifyChecksums (DdpDisc& disc)
{
    bool allMatched = true;

    for (auto& cs : disc.checksums)
    {
        juce::File f = disc.folder.getChildFile (cs.fileName);
        if (! f.existsAsFile())
        {
            cs.fileExists = false;
            cs.matched = false;
            allMatched = false;
            continue;
        }

        cs.fileExists = true;
        juce::FileInputStream stream (f);
        if (stream.openedOk())
        {
            juce::MD5 md5 (stream);
            cs.calculatedMd5 = md5.toHexString().toLowerCase();
            cs.matched = (cs.calculatedMd5 == cs.expectedMd5);
            if (! cs.matched)
                allMatched = false;
        }
        else
        {
            cs.matched = false;
            allMatched = false;
        }
    }

    return allMatched;
}

void DdpParser::checkPregapAudio (DdpDisc& disc)
{
    if (! disc.audioFile.existsAsFile())
        return;

    juce::FileInputStream stream (disc.audioFile);
    if (! stream.openedOk())
        return;

    for (auto& t : disc.tracks)
    {
        if (t.pregapFrames() > 0)
        {
            int64_t startByte = t.index0Sample * 4;
            int64_t numBytes = t.pregapFrames() * 588 * 4;
            stream.setPosition (startByte);

            std::vector<uint8_t> buf (juce::jmin ((int64_t) 65536, numBytes));
            int readBytes = stream.read (buf.data(), (int) buf.size());
            int samples = readBytes / 4;

            double maxAmp = 0.0;
            for (int i = 0; i < samples; ++i)
            {
                int16_t s = disc.isBigEndian
                    ? (int16_t) ((buf[i * 4] << 8) | buf[i * 4 + 1])
                    : (int16_t) (buf[i * 4] | (buf[i * 4 + 1] << 8));
                maxAmp = std::max (maxAmp, std::abs ((double) s / 32768.0));
            }

            // If amplitude is above -60 dBFS (approx 0.001)
            t.hasPregapAudio = (maxAmp > 0.001);

            if (t.trackNumber == 1 && t.index1Frame > 0 && t.hasPregapAudio)
            {
                disc.hasHiddenTrack = true;
            }
        }
    }
}

} // namespace bkr
