#include "Checksum.h"

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace matriz::ingest {

Checksums calcularChecksums(const juce::File& arquivo) {
    Checksums c;
    std::ifstream file(arquivo.getFullPathName().toStdString(), std::ios::binary);
    if (!file.is_open()) {
        return c;
    }

    CC_MD5_CTX md5Ctx;
    CC_SHA256_CTX sha256Ctx;
    CC_MD5_Init(&md5Ctx);
    CC_SHA256_Init(&sha256Ctx);

    constexpr size_t bufferSize = 65536;
    char buffer[bufferSize];

    while (file.read(buffer, bufferSize) || file.gcount() > 0) {
        std::streamsize bytesRead = file.gcount();
        CC_MD5_Update(&md5Ctx, buffer, static_cast<CC_LONG>(bytesRead));
        CC_SHA256_Update(&sha256Ctx, buffer, static_cast<CC_LONG>(bytesRead));
    }

    unsigned char md5Digest[CC_MD5_DIGEST_LENGTH];
    unsigned char sha256Digest[CC_SHA256_DIGEST_LENGTH];
    CC_MD5_Final(md5Digest, &md5Ctx);
    CC_SHA256_Final(sha256Digest, &sha256Ctx);

    std::stringstream md5Ss;
    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; ++i) {
        md5Ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(md5Digest[i]);
    }
    c.md5 = md5Ss.str();

    std::stringstream sha256Ss;
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i) {
        sha256Ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(sha256Digest[i]);
    }
    c.sha256 = sha256Ss.str();

    return c;
}

} // namespace matriz::ingest

#else

namespace matriz::ingest {

Checksums calcularChecksums(const juce::File& arquivo) {
    Checksums c;
    c.md5 = juce::MD5(arquivo).toHexString().toLowerCase().toStdString();
    c.sha256 = juce::SHA256(arquivo).toHexString().toLowerCase().toStdString();
    return c;
}

} // namespace matriz::ingest
#endif

