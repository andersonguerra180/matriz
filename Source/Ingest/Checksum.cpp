#include "Checksum.h"

namespace matriz::ingest {

Checksums calcularChecksums(const juce::File& arquivo) {
    Checksums c;
    c.md5 = juce::MD5(arquivo).toHexString().toLowerCase().toStdString();
    c.sha256 = juce::SHA256(arquivo).toHexString().toLowerCase().toStdString();
    return c;
}

} // namespace matriz::ingest
