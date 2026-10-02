#include "Checksum.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>

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

#elif defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace matriz::ingest {

namespace {

std::string toHex(const unsigned char* data, size_t length) {
    std::stringstream ss;
    for (size_t i = 0; i < length; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return ss.str();
}

} // namespace

Checksums calcularChecksums(const juce::File& arquivo) {
    Checksums c;
    
    // Suporte a caminhos longos no Windows e caracteres Unicode/UTF-8
    std::wstring wpath = arquivo.getFullPathName().toWideCharPointer();
    std::ifstream file(wpath.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return c;
    }

    BCRYPT_ALG_HANDLE hSha256Alg = nullptr;
    BCRYPT_ALG_HANDLE hMd5Alg = nullptr;
    BCRYPT_HASH_HANDLE hSha256Hash = nullptr;
    BCRYPT_HASH_HANDLE hMd5Hash = nullptr;

    if (BCryptOpenAlgorithmProvider(&hSha256Alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return c;
    }
    if (BCryptOpenAlgorithmProvider(&hMd5Alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0) {
        BCryptCloseAlgorithmProvider(hSha256Alg, 0);
        return c;
    }

    if (BCryptCreateHash(hSha256Alg, &hSha256Hash, nullptr, 0, nullptr, 0, 0) != 0 ||
        BCryptCreateHash(hMd5Alg, &hMd5Hash, nullptr, 0, nullptr, 0, 0) != 0) {
        if (hSha256Hash) BCryptDestroyHash(hSha256Hash);
        if (hMd5Hash) BCryptDestroyHash(hMd5Hash);
        BCryptCloseAlgorithmProvider(hSha256Alg, 0);
        BCryptCloseAlgorithmProvider(hMd5Alg, 0);
        return c;
    }

    constexpr size_t bufferSize = 65536;
    char buffer[bufferSize];

    while (file.read(buffer, bufferSize) || file.gcount() > 0) {
        ULONG bytesRead = static_cast<ULONG>(file.gcount());
        BCryptHashData(hSha256Hash, reinterpret_cast<PUCHAR>(buffer), bytesRead, 0);
        BCryptHashData(hMd5Hash, reinterpret_cast<PUCHAR>(buffer), bytesRead, 0);
    }

    unsigned char sha256Digest[32] = {0};
    unsigned char md5Digest[16] = {0};

    BCryptFinishHash(hSha256Hash, sha256Digest, sizeof(sha256Digest), 0);
    BCryptFinishHash(hMd5Hash, md5Digest, sizeof(md5Digest), 0);

    BCryptDestroyHash(hSha256Hash);
    BCryptDestroyHash(hMd5Hash);
    BCryptCloseAlgorithmProvider(hSha256Alg, 0);
    BCryptCloseAlgorithmProvider(hMd5Alg, 0);

    c.sha256 = toHex(sha256Digest, sizeof(sha256Digest));
    c.md5 = toHex(md5Digest, sizeof(md5Digest));

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
