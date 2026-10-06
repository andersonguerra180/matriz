#include "LimpezaImagem.h"

#include <exiv2/exiv2.hpp>

#include <array>
#include <cstring>

namespace matriz::consolidacao::limpeza {

namespace {

using Bytes = std::vector<std::uint8_t>;

// EXIF mínimo: cabeçalho TIFF big-endian + 1 entrada (Orientation, SHORT). 36 bytes de TIFF + "Exif\0\0".
Bytes exifSoOrientacao(int orientacao) {
    Bytes t = {'M', 'M', 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08,  // II/MM, 42, offset do IFD0 = 8
               0x00, 0x01,                                      // 1 entrada
               0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01,  // tag 0x0112, SHORT, count 1
               0x00, static_cast<std::uint8_t>(orientacao), 0x00, 0x00,  // valor (alinhado à esquerda no campo de 4 bytes)
               0x00, 0x00, 0x00, 0x00};                         // próximo IFD = nenhum
    // O valor SHORT em big-endian ocupa os 2 primeiros bytes do campo: 0x00 0xNN.
    return t;
}

std::uint32_t crc32Png(const std::uint8_t* d, std::size_t n) {
    static const auto tabela = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = tabela[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void u32be(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

}  // namespace

int lerOrientacao(const Bytes& bytes) {
    if (bytes.size() < 16) return 0;
    try {
        auto image = Exiv2::ImageFactory::open(bytes.data(), static_cast<long>(bytes.size()));
        if (!image) return 0;
        image->readMetadata();
        auto& exif = image->exifData();
        auto it = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
        if (it == exif.end()) return 0;
        const long v = it->toInt64();
        return (v >= 1 && v <= 8) ? static_cast<int>(v) : 0;
    } catch (...) {
        return 0;
    }
}

bool limparJpeg(const Bytes& in, int orientacao, Bytes& out) {
    out.clear();
    const std::size_t n = in.size();
    if (n < 4 || in[0] != 0xFF || in[1] != 0xD8) return false;

    Bytes r = {0xFF, 0xD8};
    r.reserve(n);
    std::size_t pos = 2;
    bool exifInserido = orientacao < 1 || orientacao > 8;  // nada a inserir
    bool viuSos = false;

    auto inserirExif = [&] {
        if (exifInserido) return;
        exifInserido = true;
        const Bytes tiff = exifSoOrientacao(orientacao);
        const std::size_t len = 2 + 6 + tiff.size();
        r.push_back(0xFF);
        r.push_back(0xE1);
        r.push_back(static_cast<std::uint8_t>(len >> 8));
        r.push_back(static_cast<std::uint8_t>(len));
        for (char c : {'E', 'x', 'i', 'f', '\0', '\0'}) r.push_back(static_cast<std::uint8_t>(c));
        r.insert(r.end(), tiff.begin(), tiff.end());
    };

    while (pos + 1 < n) {
        if (in[pos] != 0xFF) return false;  // estrutura inválida
        while (pos < n && in[pos] == 0xFF) ++pos;  // preenchimento 0xFF
        if (pos >= n) return false;
        const std::uint8_t marcador = in[pos++];

        if (marcador == 0xD9) {  // EOI antes do SOS: imagem sem dados
            return false;
        }
        if (marcador == 0x01 || (marcador >= 0xD0 && marcador <= 0xD7)) {  // sem comprimento
            r.push_back(0xFF);
            r.push_back(marcador);
            continue;
        }
        if (pos + 2 > n) return false;
        const std::size_t len = (static_cast<std::size_t>(in[pos]) << 8) | in[pos + 1];
        if (len < 2 || pos + len > n) return false;
        const std::uint8_t* payload = in.data() + pos + 2;
        const std::size_t tamPayload = len - 2;

        if (marcador == 0xDA) {  // SOS: o resto é dado de imagem
            inserirExif();
            viuSos = true;
            r.push_back(0xFF);
            r.push_back(marcador);
            r.insert(r.end(), in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + len));
            pos += len;
            // Copia o dado entrecortado até o EOI da imagem principal; descarta tudo que vier depois (miniaturas,
            // MPF, dados de "motion photo", XMP anexado).
            std::size_t i = pos;
            while (i + 1 < n) {
                if (in[i] == 0xFF && in[i + 1] == 0xD9) { i += 2; break; }
                ++i;
            }
            if (i + 1 >= n && !(n >= 2 && in[n - 2] == 0xFF && in[n - 1] == 0xD9)) return false;  // sem EOI
            r.insert(r.end(), in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(i));
            pos = i;
            break;
        }

        bool manter = true;
        if (marcador == 0xE1) manter = false;                               // APP1: EXIF/XMP/ExtendedXMP
        else if (marcador >= 0xE3 && marcador <= 0xED) manter = false;      // APP3..APP13 (IPTC/Photoshop, Ducky, ...)
        else if (marcador == 0xEF) manter = false;                          // APP15
        else if (marcador == 0xFE) manter = false;                          // COM
        else if (marcador == 0xE2) {                                        // APP2: só o perfil ICC fica (FlashPix/MPF saem)
            static const char icc[] = "ICC_PROFILE";
            manter = tamPayload >= 12 && std::memcmp(payload, icc, 11) == 0;
        }
        // APP0 (JFIF), APP14 (Adobe) e todos os marcadores de decodificação ficam.

        if (manter) {
            if (marcador != 0xE0) inserirExif();  // o EXIF mínimo vem logo depois do JFIF (APP0), se houver
            r.push_back(0xFF);
            r.push_back(marcador);
            r.insert(r.end(), in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + len));
        }
        pos += len;
    }
    if (!viuSos) return false;
    out = std::move(r);
    return true;
}

bool limparPng(const Bytes& in, int orientacao, Bytes& out) {
    out.clear();
    static const std::uint8_t assinatura[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (in.size() < 8 + 12 || std::memcmp(in.data(), assinatura, 8) != 0) return false;

    Bytes r(in.begin(), in.begin() + 8);
    r.reserve(in.size());
    std::size_t pos = 8;
    bool exifInserido = orientacao < 1 || orientacao > 8;
    bool viuIend = false;

    auto inserirExif = [&] {
        if (exifInserido) return;
        exifInserido = true;
        const Bytes tiff = exifSoOrientacao(orientacao);
        u32be(r, static_cast<std::uint32_t>(tiff.size()));
        const std::size_t ini = r.size();
        for (char c : {'e', 'X', 'I', 'f'}) r.push_back(static_cast<std::uint8_t>(c));
        r.insert(r.end(), tiff.begin(), tiff.end());
        u32be(r, crc32Png(r.data() + ini, r.size() - ini));
    };

    while (pos + 12 <= in.size()) {
        const std::uint32_t len = (static_cast<std::uint32_t>(in[pos]) << 24) | (static_cast<std::uint32_t>(in[pos + 1]) << 16) |
                                  (static_cast<std::uint32_t>(in[pos + 2]) << 8) | in[pos + 3];
        if (pos + 12 + static_cast<std::size_t>(len) > in.size()) return false;
        char tipo[5] = {static_cast<char>(in[pos + 4]), static_cast<char>(in[pos + 5]), static_cast<char>(in[pos + 6]),
                        static_cast<char>(in[pos + 7]), 0};
        const std::size_t total = 12 + static_cast<std::size_t>(len);

        auto eh = [&](const char* t) { return std::memcmp(tipo, t, 4) == 0; };
        // Ficam: estrutura da imagem e cor (inclui ICC). Saem: texto (tEXt/zTXt/iTXt = XMP e comentários), eXIf, tIME e
        // qualquer chunk desconhecido/privado.
        const bool manter = eh("IHDR") || eh("PLTE") || eh("IDAT") || eh("IEND") || eh("tRNS") || eh("cHRM") || eh("gAMA") ||
                            eh("iCCP") || eh("sBIT") || eh("sRGB") || eh("bKGD") || eh("hIST") || eh("pHYs") || eh("sPLT") ||
                            eh("acTL") || eh("fcTL") || eh("fdAT") || eh("cICP") || eh("mDCV") || eh("cLLI");
        if (eh("IDAT")) inserirExif();  // eXIf precisa vir antes do primeiro IDAT
        if (manter) r.insert(r.end(), in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + total));
        pos += total;
        if (eh("IEND")) { viuIend = true; break; }  // o que vier depois do IEND é descartado
    }
    if (!viuIend) return false;
    out = std::move(r);
    return true;
}

} // namespace matriz::consolidacao::limpeza
