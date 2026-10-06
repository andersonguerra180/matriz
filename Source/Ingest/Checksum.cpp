#include "Checksum.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

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

// ---------------------------------------------------------------------------
// SHA-256 incremental: CommonCrypto no macOS (aceleração de hardware); nas
// demais plataformas, implementação portátil (FIPS 180-4).
// ---------------------------------------------------------------------------
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>

namespace matriz::ingest {

struct Sha256Parcial::Impl { CC_SHA256_CTX ctx; };

Sha256Parcial::Sha256Parcial() : impl_(new Impl) { CC_SHA256_Init(&impl_->ctx); }
Sha256Parcial::~Sha256Parcial() = default;
void Sha256Parcial::atualizar(const void* dados, size_t n) {
    const auto* p = static_cast<const unsigned char*>(dados);
    while (n > 0) {  // CC_LONG é 32 bits
        const size_t parte = n > 0x40000000u ? 0x40000000u : n;
        CC_SHA256_Update(&impl_->ctx, p, static_cast<CC_LONG>(parte));
        p += parte;
        n -= parte;
    }
}
std::string Sha256Parcial::finalizar() {
    unsigned char d[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(d, &impl_->ctx);
    std::stringstream ss;
    for (unsigned char b : d) ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return ss.str();
}

} // namespace matriz::ingest

#else

namespace matriz::ingest {

struct Sha256Parcial::Impl {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buf[64];
    size_t nbuf = 0;
    uint64_t total = 0;

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void bloco(const unsigned char* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
};

Sha256Parcial::Sha256Parcial() : impl_(new Impl) {}
Sha256Parcial::~Sha256Parcial() = default;
void Sha256Parcial::atualizar(const void* dados, size_t n) {
    const auto* p = static_cast<const unsigned char*>(dados);
    impl_->total += n;
    while (n > 0) {
        const size_t parte = std::min(n, sizeof(impl_->buf) - impl_->nbuf);
        std::memcpy(impl_->buf + impl_->nbuf, p, parte);
        impl_->nbuf += parte;
        p += parte;
        n -= parte;
        if (impl_->nbuf == 64) { impl_->bloco(impl_->buf); impl_->nbuf = 0; }
    }
}
std::string Sha256Parcial::finalizar() {
    const uint64_t bits = impl_->total * 8;
    const unsigned char um = 0x80, zero = 0;
    atualizar(&um, 1);
    while (impl_->nbuf != 56) atualizar(&zero, 1);
    unsigned char len[8];
    for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
    atualizar(len, 8);
    std::stringstream ss;
    for (uint32_t v : impl_->h) ss << std::hex << std::setw(8) << std::setfill('0') << v;
    return ss.str();
}

} // namespace matriz::ingest

#endif

namespace matriz::ingest {

std::string sha256DoArquivo(const juce::File& arquivo, const std::function<bool()>& continuar, bool* cancelado) {
    if (cancelado) *cancelado = false;
    juce::FileInputStream in(arquivo);
    if (!in.openedOk()) return {};
    Sha256Parcial h;
    std::vector<char> buf(1 << 20);
    for (;;) {
        if (continuar && !continuar()) {
            if (cancelado) *cancelado = true;
            return {};
        }
        const int lidos = in.read(buf.data(), static_cast<int>(buf.size()));
        if (lidos < 0) return {};
        if (lidos == 0) break;
        h.atualizar(buf.data(), static_cast<size_t>(lidos));
    }
    return h.finalizar();
}

} // namespace matriz::ingest
