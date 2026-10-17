#include "BridgeXmp.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>

#include "../Analytics/AssetGeolocation.h"
#include "../Model/NomesCanonicos.h"
#include "../Model/Project.h"

namespace matriz::consolidacao::bridge {

using matriz::db::Value;

namespace {

// ===================================================================== utilidades de bytes

juce::uint32 lerU32(const juce::uint8* p, bool bigEndian) {
    return bigEndian ? (juce::uint32(p[0]) << 24) | (juce::uint32(p[1]) << 16) | (juce::uint32(p[2]) << 8) | p[3]
                     : (juce::uint32(p[3]) << 24) | (juce::uint32(p[2]) << 16) | (juce::uint32(p[1]) << 8) | p[0];
}
juce::uint32 lerU16(const juce::uint8* p, bool bigEndian) {
    return bigEndian ? (juce::uint32(p[0]) << 8) | p[1] : (juce::uint32(p[1]) << 8) | p[0];
}
void escreverU32(juce::uint8* p, juce::uint32 v, bool bigEndian) {
    if (bigEndian) { p[0] = juce::uint8(v >> 24); p[1] = juce::uint8(v >> 16); p[2] = juce::uint8(v >> 8); p[3] = juce::uint8(v); }
    else           { p[3] = juce::uint8(v >> 24); p[2] = juce::uint8(v >> 16); p[1] = juce::uint8(v >> 8); p[0] = juce::uint8(v); }
}
void escreverU16(juce::uint8* p, juce::uint32 v, bool bigEndian) {
    if (bigEndian) { p[0] = juce::uint8(v >> 8); p[1] = juce::uint8(v); }
    else           { p[1] = juce::uint8(v >> 8); p[0] = juce::uint8(v); }
}

bool lerEm(juce::FileInputStream& in, juce::int64 pos, void* dst, int n) {
    in.setPosition(pos);
    return in.read(dst, n) == n;
}

bool substituirPorTemporario(const juce::File& destino, const juce::File& temporario) {
    destino.deleteFile();
    if (!temporario.moveFileTo(destino)) {
        temporario.deleteFile();
        return false;
    }
    return true;
}

juce::uint32 crc32Png(const juce::uint8* dados, size_t n, juce::uint32 crc = 0xFFFFFFFFu) {
    static std::array<juce::uint32, 256> tabela = [] {
        std::array<juce::uint32, 256> t{};
        for (juce::uint32 i = 0; i < 256; ++i) {
            juce::uint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    for (size_t i = 0; i < n; ++i) crc = tabela[(crc ^ dados[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

constexpr char kIdentXmpJpeg[] = "http://ns.adobe.com/xap/1.0/";  // + NUL = 29 bytes
constexpr juce::uint8 kUuidXmp[16] = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
                                       0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};

// ===================================================================== embutir: JPEG / PNG

bool embutirJpeg(const juce::File& f, const std::string& pacote) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) return false;
    const auto* d = static_cast<const juce::uint8*>(mb.getData());
    const size_t n = mb.getSize();
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    const size_t carga = 29 + pacote.size();
    if (carga + 2 > 65535) return false;  // XMP estendido não é suportado: vira sidecar

    juce::MemoryOutputStream out;
    out.write(d, 2);
    size_t pos = 2;
    bool inserido = false;
    auto escreverXmp = [&] {
        out.writeByte(char(0xFF));
        out.writeByte(char(0xE1));
        out.writeByte(char((carga + 2) >> 8));
        out.writeByte(char((carga + 2) & 0xFF));
        out.write(kIdentXmpJpeg, 29);
        out.write(pacote.data(), pacote.size());
        inserido = true;
    };
    while (pos + 4 <= n) {
        if (d[pos] != 0xFF) return false;
        const juce::uint8 m = d[pos + 1];
        if (m == 0xFF || m == 0x00 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) return false;
        if (m == 0xD9 || m == 0xDA) break;  // EOI / SOS: o resto vai cru
        const size_t len = (size_t(d[pos + 2]) << 8) | d[pos + 3];
        if (len < 2 || pos + 2 + len > n) return false;
        const bool ehXmp = m == 0xE1 && len >= 2 + 29 && std::memcmp(d + pos + 4, kIdentXmpJpeg, 29) == 0;
        if (ehXmp) { pos += 2 + len; continue; }  // o XMP antigo sai
        const bool ehExif = m == 0xE1 && len >= 2 + 6 && std::memcmp(d + pos + 4, "Exif\0\0", 6) == 0;
        if (!(m == 0xE0 || ehExif) && !inserido) escreverXmp();  // depois de JFIF/Exif, antes do resto
        out.write(d + pos, 2 + len);
        pos += 2 + len;
    }
    if (!inserido) escreverXmp();
    out.write(d + pos, n - pos);
    const juce::File tmp = f.getSiblingFile(f.getFileName() + ".xmp.tmp");
    if (!tmp.replaceWithData(out.getData(), out.getDataSize())) return false;
    return substituirPorTemporario(f, tmp);
}

bool embutirPng(const juce::File& f, const std::string& pacote) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) return false;
    const auto* d = static_cast<const juce::uint8*>(mb.getData());
    const size_t n = mb.getSize();
    static const juce::uint8 assinatura[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n < 8 + 12 || std::memcmp(d, assinatura, 8) != 0) return false;

    // iTXt: keyword\0 + flag + metodo + idioma\0 + traduzido\0 + texto
    std::string dados = "XML:com.adobe.xmp";
    dados.push_back('\0'); dados.push_back('\0'); dados.push_back('\0');
    dados.push_back('\0'); dados.push_back('\0');
    dados += pacote;

    juce::MemoryOutputStream out;
    out.write(d, 8);
    size_t pos = 8;
    bool inserido = false;
    while (pos + 12 <= n) {
        const juce::uint32 len = lerU32(d + pos, true);
        if (pos + 12 + size_t(len) > n) return false;
        const char* tipo = reinterpret_cast<const char*>(d + pos + 4);
        const bool ehXmpAntigo = std::memcmp(tipo, "iTXt", 4) == 0 && len >= 18 &&
                                 std::memcmp(d + pos + 8, "XML:com.adobe.xmp", 17) == 0;
        if (!ehXmpAntigo) out.write(d + pos, 12 + size_t(len));
        pos += 12 + size_t(len);
        if (!inserido) {  // logo depois do IHDR (primeiro chunk)
            juce::uint8 cab[8];
            escreverU32(cab, juce::uint32(dados.size()), true);
            std::memcpy(cab + 4, "iTXt", 4);
            out.write(cab, 8);
            out.write(dados.data(), dados.size());
            juce::uint32 crc = crc32Png(reinterpret_cast<const juce::uint8*>("iTXt"), 4);
            crc = crc32Png(reinterpret_cast<const juce::uint8*>(dados.data()), dados.size(), crc) ^ 0xFFFFFFFFu;
            juce::uint8 c4[4];
            escreverU32(c4, crc, true);
            out.write(c4, 4);
            inserido = true;
        }
    }
    if (!inserido || pos != n) return false;
    const juce::File tmp = f.getSiblingFile(f.getFileName() + ".xmp.tmp");
    if (!tmp.replaceWithData(out.getData(), out.getDataSize())) return false;
    return substituirPorTemporario(f, tmp);
}

// ===================================================================== embutir: TIFF (no próprio arquivo)

bool embutirTiff(const juce::File& f, const std::string& pacote) {
    const juce::int64 tamanho = f.getSize();
    if (tamanho < 16 || tamanho > 0xF0000000LL - juce::int64(pacote.size()) - 4096) return false;  // BigTIFF/gigante: sidecar
    std::vector<std::array<juce::uint8, 12>> entradas;
    juce::uint8 proximo[4];
    bool be = false;
    {
        juce::FileInputStream in(f);
        if (!in.openedOk()) return false;
        juce::uint8 h[8];
        if (!lerEm(in, 0, h, 8)) return false;
        if (h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0) be = false;
        else if (h[0] == 'M' && h[1] == 'M' && h[2] == 0 && h[3] == 42) be = true;
        else return false;
        const juce::uint32 ifd = lerU32(h + 4, be);
        juce::uint8 c2[2];
        if (ifd < 8 || juce::int64(ifd) + 2 > tamanho || !lerEm(in, ifd, c2, 2)) return false;
        const juce::uint32 qtd = lerU16(c2, be);
        if (juce::int64(ifd) + 2 + juce::int64(qtd) * 12 + 4 > tamanho) return false;
        for (juce::uint32 i = 0; i < qtd; ++i) {
            std::array<juce::uint8, 12> e{};
            if (!lerEm(in, ifd + 2 + juce::int64(i) * 12, e.data(), 12)) return false;
            if (lerU16(e.data(), be) != 700) entradas.push_back(e);
        }
        if (!lerEm(in, ifd + 2 + juce::int64(qtd) * 12, proximo, 4)) return false;
    }
    juce::FileOutputStream out(f);
    if (!out.openedOk()) return false;
    out.setPosition(tamanho);
    auto desfazer = [&] { out.setPosition(tamanho); out.truncate(); out.flush(); return false; };

    juce::int64 pos = tamanho;
    if (pos & 1) { out.writeByte(0); ++pos; }
    const juce::int64 offXmp = pos;
    if (!out.write(pacote.data(), pacote.size())) return desfazer();
    pos += juce::int64(pacote.size());
    if (pos & 1) { out.writeByte(0); ++pos; }
    const juce::int64 offIfd = pos;

    std::array<juce::uint8, 12> nova{};
    escreverU16(nova.data(), 700, be);
    escreverU16(nova.data() + 2, 1, be);  // BYTE
    escreverU32(nova.data() + 4, juce::uint32(pacote.size()), be);
    escreverU32(nova.data() + 8, juce::uint32(offXmp), be);
    entradas.push_back(nova);
    std::stable_sort(entradas.begin(), entradas.end(), [be](const auto& a, const auto& b) {
        return lerU16(a.data(), be) < lerU16(b.data(), be);
    });
    juce::uint8 c2[2];
    escreverU16(c2, juce::uint32(entradas.size()), be);
    out.write(c2, 2);
    for (const auto& e : entradas) out.write(e.data(), 12);
    out.write(proximo, 4);

    juce::uint8 novoOffset[4];
    escreverU32(novoOffset, juce::uint32(offIfd), be);
    out.flush();
    if (out.getStatus().failed()) return desfazer();
    out.setPosition(4);
    out.write(novoOffset, 4);  // por último: só agora o cabeçalho passa a apontar pro IFD novo
    out.flush();
    return !out.getStatus().failed();
}

// ===================================================================== embutir: PSD

bool embutirPsd(const juce::File& f, const std::string& pacote) {
    const juce::File tmp = f.getSiblingFile(f.getFileName() + ".xmp.tmp");
    juce::int64 esperado = 0;
    {
        juce::FileInputStream in(f);
        if (!in.openedOk()) return false;
        const juce::int64 tamanho = in.getTotalLength();
        juce::uint8 cab[30];
        if (tamanho < 34 || !lerEm(in, 0, cab, 30)) return false;
        if (std::memcmp(cab, "8BPS", 4) != 0 || lerU16(cab + 4, true) != 1) return false;  // PSB (versão 2): sidecar
        const juce::uint32 lenCor = lerU32(cab + 26, true);
        const juce::int64 inicioRes = 26 + 4 + juce::int64(lenCor);
        juce::uint8 l4[4];
        if (inicioRes + 4 > tamanho || !lerEm(in, inicioRes, l4, 4)) return false;
        const juce::uint32 lenRes = lerU32(l4, true);
        const juce::int64 fimRes = inicioRes + 4 + juce::int64(lenRes);
        if (fimRes > tamanho) return false;

        juce::MemoryBlock recursos;
        recursos.setSize(lenRes);
        if (lenRes > 0 && !lerEm(in, inicioRes + 4, recursos.getData(), int(lenRes))) return false;
        const auto* r = static_cast<const juce::uint8*>(recursos.getData());
        juce::MemoryOutputStream novos;
        size_t p = 0;
        while (p + 12 <= lenRes) {
            if (std::memcmp(r + p, "8BIM", 4) != 0) return false;
            const juce::uint32 id = lerU16(r + p + 4, true);
            const size_t nomeLen = r[p + 6];
            size_t q = p + 7 + nomeLen;
            if ((1 + nomeLen) & 1) ++q;  // nome pascal alinhado a par
            if (q + 4 > lenRes) return false;
            const juce::uint32 tam = lerU32(r + q, true);
            size_t fim = q + 4 + tam;
            if (tam & 1) ++fim;
            if (fim > lenRes) return false;
            if (id != 1060) novos.write(r + p, fim - p);
            p = fim;
        }
        // 1060 = XMP. Nome pascal vazio (2 bytes), tamanho, dados, pad.
        novos.write("8BIM", 4);
        juce::uint8 b2[2]; escreverU16(b2, 1060, true); novos.write(b2, 2);
        novos.writeByte(0); novos.writeByte(0);
        juce::uint8 b4[4]; escreverU32(b4, juce::uint32(pacote.size()), true); novos.write(b4, 4);
        novos.write(pacote.data(), pacote.size());
        if (pacote.size() & 1) novos.writeByte(0);

        juce::FileOutputStream out(tmp);
        if (!out.openedOk()) return false;
        out.setPosition(0);
        out.truncate();
        in.setPosition(0);
        juce::MemoryBlock bloco;
        bloco.setSize(size_t(inicioRes));
        if (in.read(bloco.getData(), int(inicioRes)) != int(inicioRes)) { tmp.deleteFile(); return false; }
        out.write(bloco.getData(), bloco.getSize());
        escreverU32(b4, juce::uint32(novos.getDataSize()), true);
        out.write(b4, 4);
        out.write(novos.getData(), novos.getDataSize());
        in.setPosition(fimRes);
        juce::HeapBlock<char> buf(1 << 20);
        for (;;) {
            const int lido = in.read(buf, 1 << 20);
            if (lido <= 0) break;
            out.write(buf, size_t(lido));
        }
        out.flush();
        if (out.getStatus().failed()) { tmp.deleteFile(); return false; }
        esperado = tamanho - juce::int64(lenRes) + juce::int64(novos.getDataSize());
    }
    if (tmp.getSize() != esperado) { tmp.deleteFile(); return false; }
    return substituirPorTemporario(f, tmp);
}

// ===================================================================== embutir: MP4/MOV e WAV (acrescenta no fim)

bool embutirIso(const juce::File& f, const std::string& pacote) {
    const juce::int64 tamanho = f.getSize();
    if (tamanho < 16 || tamanho > 0xF0000000LL) return false;
    std::vector<juce::int64> uuidsXmp;
    {
        juce::FileInputStream in(f);
        if (!in.openedOk()) return false;
        juce::uint8 cab[24];
        if (!lerEm(in, 0, cab, 8) || std::memcmp(cab + 4, "ftyp", 4) != 0) return false;
        juce::int64 pos = 0;
        while (pos < tamanho) {
            if (pos + 8 > tamanho || !lerEm(in, pos, cab, 8)) return false;
            juce::uint64 tam = lerU32(cab, true);
            int cabecalho = 8;
            if (tam == 1) {
                if (!lerEm(in, pos + 8, cab + 8, 8)) return false;
                tam = (juce::uint64(lerU32(cab + 8, true)) << 32) | lerU32(cab + 12, true);
                cabecalho = 16;
            } else if (tam == 0) {
                tam = juce::uint64(tamanho - pos);
            }
            if (tam < juce::uint64(cabecalho) || pos + juce::int64(tam) > tamanho) return false;
            if (std::memcmp(cab + 4, "uuid", 4) == 0) {
                juce::uint8 u[16];
                if (tam >= juce::uint64(cabecalho + 16) && lerEm(in, pos + cabecalho, u, 16) && std::memcmp(u, kUuidXmp, 16) == 0)
                    uuidsXmp.push_back(pos);
            }
            pos += juce::int64(tam);
        }
        if (pos != tamanho) return false;
    }
    juce::FileOutputStream out(f);
    if (!out.openedOk()) return false;
    for (auto p : uuidsXmp) { out.setPosition(p + 4); out.write("free", 4); }  // XMP antigo vira 'free'
    out.setPosition(tamanho);
    auto desfazer = [&] { out.setPosition(tamanho); out.truncate(); out.flush(); return false; };
    juce::uint8 b4[4];
    escreverU32(b4, juce::uint32(8 + 16 + pacote.size()), true);
    out.write(b4, 4);
    out.write("uuid", 4);
    out.write(kUuidXmp, 16);
    out.write(pacote.data(), pacote.size());
    out.flush();
    return out.getStatus().failed() ? desfazer() : true;
}

bool embutirWav(const juce::File& f, const std::string& pacote) {
    const juce::int64 tamanho = f.getSize();
    if (tamanho < 20 || tamanho + juce::int64(pacote.size()) + 16 > 0xFFFFFFF0LL) return false;
    std::vector<juce::int64> antigos;
    {
        juce::FileInputStream in(f);
        if (!in.openedOk()) return false;
        juce::uint8 h[12];
        if (!lerEm(in, 0, h, 12) || std::memcmp(h, "RIFF", 4) != 0 || std::memcmp(h + 8, "WAVE", 4) != 0) return false;
        juce::int64 pos = 12;
        while (pos + 8 <= tamanho) {
            juce::uint8 c[8];
            if (!lerEm(in, pos, c, 8)) return false;
            juce::int64 fim = pos + 8 + juce::int64(lerU32(c + 4, false));
            if (fim & 1) ++fim;
            if (fim > tamanho) return false;
            if (std::memcmp(c, "_PMX", 4) == 0) antigos.push_back(pos);
            pos = fim;
        }
        if (pos != tamanho) return false;  // sobra no fim: não mexe
    }
    juce::FileOutputStream out(f);
    if (!out.openedOk()) return false;
    for (auto p : antigos) { out.setPosition(p); out.write("JUNK", 4); }
    out.setPosition(tamanho);
    auto desfazer = [&] { out.setPosition(tamanho); out.truncate(); out.flush(); return false; };
    out.write("_PMX", 4);
    juce::uint8 b4[4];
    escreverU32(b4, juce::uint32(pacote.size()), false);
    out.write(b4, 4);
    out.write(pacote.data(), pacote.size());
    juce::int64 novoTamanho = tamanho + 8 + juce::int64(pacote.size());
    if (pacote.size() & 1) { out.writeByte(0); ++novoTamanho; }
    out.flush();
    if (out.getStatus().failed()) return desfazer();
    escreverU32(b4, juce::uint32(novoTamanho - 8), false);
    out.setPosition(4);
    out.write(b4, 4);
    out.flush();
    return !out.getStatus().failed();
}

// ===================================================================== localizar o XMP (leitura)

std::string textoEntre(const std::string& s, const char* ini, const char* fim) {
    const auto a = s.find(ini);
    if (a == std::string::npos) return {};
    const auto b = s.find(fim, a);
    if (b == std::string::npos) return {};
    return s.substr(a, b - a + std::strlen(fim));
}

std::string recortarPacote(const std::string& bruto) {
    auto t = textoEntre(bruto, "<x:xmpmeta", "</x:xmpmeta>");
    if (t.empty()) t = textoEntre(bruto, "<rdf:RDF", "</rdf:RDF>");
    return t;
}

std::string lerTrecho(juce::FileInputStream& in, juce::int64 pos, juce::int64 n) {
    std::string s(size_t(std::max<juce::int64>(0, n)), '\0');
    in.setPosition(pos);
    const int lido = in.read(s.data(), int(s.size()));
    s.resize(size_t(std::max(0, lido)));
    return s;
}

// Acha o pacote seguindo a estrutura do formato; vazio se o formato não tem XMP ou não é reconhecido.
std::string xmpPorEstrutura(const juce::File& f, bool& reconhecido) {
    reconhecido = true;
    juce::FileInputStream in(f);
    if (!in.openedOk()) { reconhecido = false; return {}; }
    const juce::int64 tamanho = in.getTotalLength();
    juce::uint8 h[16] = {};
    in.setPosition(0);
    const int lidoH = in.read(h, 16);
    if (lidoH < 8) { reconhecido = false; return {}; }

    if (h[0] == 0xFF && h[1] == 0xD8) {  // JPEG
        juce::int64 pos = 2;
        while (pos + 4 <= tamanho) {
            juce::uint8 m[4];
            if (!lerEm(in, pos, m, 4) || m[0] != 0xFF) break;
            if (m[1] == 0xDA || m[1] == 0xD9) break;
            const juce::int64 len = (juce::int64(m[2]) << 8) | m[3];
            if (len < 2) break;
            if (m[1] == 0xE1 && len >= 2 + 29) {
                char id[29];
                if (lerEm(in, pos + 4, id, 29) && std::memcmp(id, kIdentXmpJpeg, 29) == 0)
                    return lerTrecho(in, pos + 4 + 29, len - 2 - 29);
            }
            pos += 2 + len;
        }
        return {};
    }
    if (std::memcmp(h, "\x89PNG", 4) == 0) {  // PNG
        juce::int64 pos = 8;
        while (pos + 12 <= tamanho) {
            juce::uint8 c[8];
            if (!lerEm(in, pos, c, 8)) break;
            const juce::int64 len = lerU32(c, true);
            if (std::memcmp(c + 4, "iTXt", 4) == 0 && len >= 22) {
                char kw[17];
                if (lerEm(in, pos + 8, kw, 17) && std::memcmp(kw, "XML:com.adobe.xmp", 17) == 0) {
                    const std::string dados = lerTrecho(in, pos + 8, len);
                    // keyword\0 flag metodo idioma\0 traduzido\0 texto (sem compressão)
                    size_t i = 18;
                    if (dados.size() > i + 2 && dados[i] == 0 && dados[i + 1] == 0) {
                        i += 2;
                        while (i < dados.size() && dados[i] != 0) ++i;
                        ++i;
                        while (i < dados.size() && dados[i] != 0) ++i;
                        ++i;
                        if (i < dados.size()) return dados.substr(i);
                    }
                }
            }
            pos += 12 + len;
        }
        return {};
    }
    if (std::memcmp(h, "8BPS", 4) == 0) {  // PSD
        juce::uint8 b4[4];
        juce::uint8 c4[4];
        if (!lerEm(in, 26, c4, 4)) return {};
        const juce::int64 inicioRes = 26 + 4 + juce::int64(lerU32(c4, true));
        if (!lerEm(in, inicioRes, b4, 4)) return {};
        const juce::int64 fim = inicioRes + 4 + juce::int64(lerU32(b4, true));
        juce::int64 p = inicioRes + 4;
        while (p + 12 <= fim && p + 12 <= tamanho) {
            juce::uint8 r[7];
            if (!lerEm(in, p, r, 7) || std::memcmp(r, "8BIM", 4) != 0) break;
            const juce::uint32 id = lerU16(r + 4, true);
            juce::int64 q = p + 7 + r[6];
            if ((1 + r[6]) & 1) ++q;
            juce::uint8 t4[4];
            if (!lerEm(in, q, t4, 4)) break;
            const juce::int64 tam = lerU32(t4, true);
            if (id == 1060) return lerTrecho(in, q + 4, tam);
            p = q + 4 + tam + (tam & 1);
        }
        return {};
    }
    if (std::memcmp(h + 4, "ftyp", 4) == 0) {  // MP4 / MOV
        juce::int64 pos = 0;
        while (pos + 8 <= tamanho) {
            juce::uint8 c[16];
            if (!lerEm(in, pos, c, 8)) break;
            juce::uint64 tam = lerU32(c, true);
            int cabecalho = 8;
            if (tam == 1) {
                if (!lerEm(in, pos + 8, c + 8, 8)) break;
                tam = (juce::uint64(lerU32(c + 8, true)) << 32) | lerU32(c + 12, true);
                cabecalho = 16;
            } else if (tam == 0) {
                tam = juce::uint64(tamanho - pos);
            }
            if (tam < juce::uint64(cabecalho)) break;
            if (std::memcmp(c + 4, "uuid", 4) == 0 && tam >= juce::uint64(cabecalho + 16)) {
                juce::uint8 u[16];
                if (lerEm(in, pos + cabecalho, u, 16) && std::memcmp(u, kUuidXmp, 16) == 0)
                    return lerTrecho(in, pos + cabecalho + 16, juce::int64(tam) - cabecalho - 16);
            }
            pos += juce::int64(tam);
        }
        return {};
    }
    if (std::memcmp(h, "RIFF", 4) == 0 && std::memcmp(h + 8, "WAVE", 4) == 0) {  // WAV
        juce::int64 pos = 12;
        while (pos + 8 <= tamanho) {
            juce::uint8 c[8];
            if (!lerEm(in, pos, c, 8)) break;
            const juce::int64 tam = lerU32(c + 4, false);
            if (std::memcmp(c, "_PMX", 4) == 0) return lerTrecho(in, pos + 8, tam);
            pos += 8 + tam + (tam & 1);
        }
        return {};
    }
    const bool le = h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0;
    const bool be = h[0] == 'M' && h[1] == 'M' && h[2] == 0 && h[3] == 42;
    if (le || be) {  // TIFF (inclui DNG/CR2/NEF...): tag 700 do IFD0
        const juce::uint32 ifd = lerU32(h + 4, be);
        juce::uint8 c2[2];
        if (!lerEm(in, ifd, c2, 2)) return {};
        const juce::uint32 qtd = lerU16(c2, be);
        for (juce::uint32 i = 0; i < qtd && i < 4096; ++i) {
            juce::uint8 e[12];
            if (!lerEm(in, juce::int64(ifd) + 2 + juce::int64(i) * 12, e, 12)) break;
            if (lerU16(e, be) == 700) {
                const juce::uint32 cont = lerU32(e + 4, be);
                const juce::uint32 off = lerU32(e + 8, be);
                if (cont > 0 && juce::int64(off) + cont <= tamanho) return lerTrecho(in, off, cont);
            }
        }
        return {};
    }
    reconhecido = false;
    return {};
}

// Formatos sem estrutura tratada (PDF, AI, ...): varre o começo e o fim do arquivo.
std::string xmpPorVarredura(const juce::File& f) {
    juce::FileInputStream in(f);
    if (!in.openedOk()) return {};
    const juce::int64 tamanho = in.getTotalLength();
    constexpr juce::int64 janela = 4 * 1024 * 1024;
    std::string s = lerTrecho(in, 0, std::min(tamanho, janela));
    auto p = recortarPacote(s);
    if (!p.empty() || tamanho <= janela) return p;
    return recortarPacote(lerTrecho(in, tamanho - janela, janela));
}

// ===================================================================== interpretar o XML

std::vector<std::string> valoresDaPropriedade(const juce::XmlElement& desc, const juce::String& nome, bool preferirDefault) {
    std::vector<std::string> out;
    if (desc.hasAttribute(nome)) {
        out.push_back(desc.getStringAttribute(nome).toStdString());
        return out;
    }
    if (auto* el = desc.getChildByName(nome)) {
        auto* colecao = el->getFirstChildElement();  // rdf:Alt / rdf:Bag / rdf:Seq
        if (colecao != nullptr && colecao->getTagName().startsWith("rdf:")) {
            std::string padrao;
            for (auto* li : colecao->getChildIterator()) {
                const auto t = li->getAllSubText().trim().toStdString();
                if (t.empty()) continue;
                if (preferirDefault && li->getStringAttribute("xml:lang") == "x-default" && padrao.empty()) padrao = t;
                out.push_back(t);
            }
            if (preferirDefault && !padrao.empty()) return {padrao};
        } else {
            const auto t = el->getAllSubText().trim().toStdString();
            if (!t.empty()) out.push_back(t);
        }
    }
    return out;
}

std::string primeiro(const std::vector<std::string>& v) { return v.empty() ? std::string() : v.front(); }

std::string escXml(const std::string& v) {
    return juce::String::fromUTF8(v.c_str()).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        .replace("\"", "&quot;").toStdString();
}

bool mesmaChave(const std::string& a, const std::string& b) {
    return matriz::model::nomes::chave(a) == matriz::model::nomes::chave(b);
}

}  // namespace

// ===================================================================== SAÍDA

DadosBridge coletarDadosBridge(matriz::db::Database& registro, const std::string& itemId) {
    DadosBridge d;
    {
        auto st = registro.prepare("SELECT COALESCE(NULLIF(TRIM(dc_title), ''), titulo, ''), COALESCE(dc_description, ''), "
                                   "COALESCE(ano, '') FROM item WHERE id = ?");
        st.bind(1, Value::of(itemId));
        if (st.step()) {
            d.titulo = st.columnText(0);
            d.descricao = st.columnText(1);
            const juce::String ano = juce::String::fromUTF8(st.columnText(2).c_str()).trim();
            if (ano.length() >= 4 && ano.substring(0, 4).containsOnly("0123456789")) d.dataCriacao = ano.substring(0, 4).toStdString();
        }
    }
    std::vector<std::string> tags;
    {
        auto st = registro.prepare("SELECT tag FROM item_tag WHERE item_id = ? ORDER BY tag");
        st.bind(1, Value::of(itemId));
        while (st.step()) tags.push_back(st.columnText(0));
    }
    // PEOPLE mora em item_tag (o seletor põe o nome como tag); collection_person é a lista de nomes.
    std::vector<std::string> nomesPessoas;
    try {
        auto st = registro.prepare("SELECT nome FROM collection_person");
        while (st.step()) nomesPessoas.push_back(st.columnText(0));
    } catch (...) {}
    for (const auto& t : tags) {
        d.palavrasChave.push_back(t);
        for (const auto& p : nomesPessoas)
            if (mesmaChave(p, t)) { d.pessoas.push_back(t); break; }
    }
    try {
        if (auto g = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(registro, itemId)) {
            auto pega = [](const std::optional<std::string>& o) { return o ? *o : std::string(); };
            d.local = !pega(g->locality).empty() ? pega(g->locality)
                      : !pega(g->neighborhood).empty() ? pega(g->neighborhood) : pega(g->district);
            d.cidade = pega(g->city);
            d.estado = pega(g->stateProvince);
            d.pais = pega(g->country);
        }
    } catch (...) {}
    return d;
}

std::string montarPacoteXmp(const DadosBridge& d) {
    auto alt = [&](const char* prop, const std::string& v) {
        return v.empty() ? juce::String()
                         : juce::String("   <") + prop + "><rdf:Alt><rdf:li xml:lang=\"x-default\">" +
                               juce::String::fromUTF8(escXml(v).c_str()) + "</rdf:li></rdf:Alt></" + prop + ">\n";
    };
    auto bag = [&](const char* prop, const std::vector<std::string>& vs) {
        if (vs.empty()) return juce::String();
        juce::String out = juce::String("   <") + prop + "><rdf:Bag>";
        for (const auto& v : vs) out << "<rdf:li>" << juce::String::fromUTF8(escXml(v).c_str()) << "</rdf:li>";
        return out + "</rdf:Bag></" + prop + ">\n";
    };
    auto simples = [&](const char* prop, const std::string& v) {
        return v.empty() ? juce::String()
                         : juce::String("   <") + prop + ">" + juce::String::fromUTF8(escXml(v).c_str()) + "</" + prop + ">\n";
    };
    std::vector<std::string> hier;
    for (const auto& p : d.pessoas) hier.push_back("Pessoas|" + p);
    juce::String x;
    x << "<?xpacket begin=\"" << juce::String::fromUTF8("\xef\xbb\xbf") << "\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
      << "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"BKR Matriz\">\n"
      << " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
      << "  <rdf:Description rdf:about=\"\"\n"
      << "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
      << "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
      << "    xmlns:lr=\"http://ns.adobe.com/lightroom/1.0/\"\n"
      << "    xmlns:photoshop=\"http://ns.adobe.com/photoshop/1.0/\"\n"
      << "    xmlns:Iptc4xmpCore=\"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/\"\n"
      << "    xmp:CreatorTool=\"BKR Matriz\">\n"
      << alt("dc:title", d.titulo) << alt("dc:description", d.descricao)
      << bag("dc:subject", d.palavrasChave) << bag("lr:hierarchicalSubject", hier)
      << simples("photoshop:DateCreated", d.dataCriacao) << simples("Iptc4xmpCore:Location", d.local)
      << simples("photoshop:City", d.cidade) << simples("photoshop:State", d.estado)
      << simples("photoshop:Country", d.pais)
      << "  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>\n";
    return x.toStdString();
}

bool formatoAceitaXmpEmbutido(const juce::File& arquivo) {
    return arquivo.hasFileExtension("jpg;jpeg;png;tif;tiff;psd;mp4;mov;m4v;wav");
}

bool embutirXmpNoArquivo(const juce::File& arquivo, const std::string& pacote) {
    if (!arquivo.existsAsFile() || pacote.empty()) return false;
    try {
        if (arquivo.hasFileExtension("jpg;jpeg")) return embutirJpeg(arquivo, pacote);
        if (arquivo.hasFileExtension("png")) return embutirPng(arquivo, pacote);
        if (arquivo.hasFileExtension("tif;tiff")) return embutirTiff(arquivo, pacote);
        if (arquivo.hasFileExtension("psd")) return embutirPsd(arquivo, pacote);
        if (arquivo.hasFileExtension("mp4;mov;m4v")) return embutirIso(arquivo, pacote);
        if (arquivo.hasFileExtension("wav")) return embutirWav(arquivo, pacote);
    } catch (...) {}
    return false;
}

bool gravarSidecarAdobe(const juce::File& arquivo, const std::string& pacote) {
    const juce::File sidecar = arquivo.withFileExtension("xmp");
    if (sidecar.existsAsFile()) return false;
    const juce::File tmp = sidecar.getSiblingFile(sidecar.getFileName() + ".tmp");
    if (!tmp.replaceWithData(pacote.data(), pacote.size())) return false;
    if (!tmp.moveFileTo(sidecar)) {
        tmp.deleteFile();
        return false;
    }
    return true;
}

ResultadoBridgeArquivo gravarXmpBridgeNaCopia(matriz::db::Database& registro, const std::string& itemId,
                                              const juce::File& copia, std::string& aviso) {
    const auto dados = coletarDadosBridge(registro, itemId);
    if (dados.vazio()) return ResultadoBridgeArquivo::SemMetadado;
    const std::string pacote = montarPacoteXmp(dados);
    if (formatoAceitaXmpEmbutido(copia)) {
        if (embutirXmpNoArquivo(copia, pacote)) return ResultadoBridgeArquivo::Embutido;
        aviso = copia.getFileName().toStdString() + ": XMP could not be embedded, sidecar written instead";
    }
    if (gravarSidecarAdobe(copia, pacote)) return ResultadoBridgeArquivo::Sidecar;
    if (copia.withFileExtension("xmp").existsAsFile()) {
        aviso = copia.getFileName().toStdString() + ": sidecar name " +
                copia.withFileExtension("xmp").getFileName().toStdString() +
                " already exists in the destination folder; sidecar skipped (not overwritten)";
        return ResultadoBridgeArquivo::ColisaoSidecar;
    }
    aviso = copia.getFileName().toStdString() + ": XMP sidecar could not be written";
    return ResultadoBridgeArquivo::Falha;
}

// ===================================================================== ENTRADA

DadosBridge interpretarPacoteXmp(const std::string& pacote) {
    DadosBridge d;
    const auto recorte = recortarPacote(pacote);
    if (recorte.empty()) return d;
    std::unique_ptr<juce::XmlElement> raiz(juce::XmlDocument::parse(juce::String::fromUTF8(recorte.c_str())));
    if (!raiz) return d;
    const juce::XmlElement* rdf = raiz->getTagName() == "rdf:RDF" ? raiz.get() : raiz->getChildByName("rdf:RDF");
    if (!rdf) return d;
    std::vector<std::string> hier;
    for (auto* desc : rdf->getChildWithTagNameIterator("rdf:Description")) {
        if (d.titulo.empty()) d.titulo = primeiro(valoresDaPropriedade(*desc, "dc:title", true));
        if (d.descricao.empty()) d.descricao = primeiro(valoresDaPropriedade(*desc, "dc:description", true));
        for (auto& k : valoresDaPropriedade(*desc, "dc:subject", false)) d.palavrasChave.push_back(k);
        for (auto& k : valoresDaPropriedade(*desc, "lr:hierarchicalSubject", false)) hier.push_back(k);
        if (d.dataCriacao.empty()) d.dataCriacao = primeiro(valoresDaPropriedade(*desc, "photoshop:DateCreated", false));
        if (d.local.empty()) d.local = primeiro(valoresDaPropriedade(*desc, "Iptc4xmpCore:Location", false));
        if (d.cidade.empty()) d.cidade = primeiro(valoresDaPropriedade(*desc, "photoshop:City", false));
        if (d.estado.empty()) d.estado = primeiro(valoresDaPropriedade(*desc, "photoshop:State", false));
        if (d.pais.empty()) d.pais = primeiro(valoresDaPropriedade(*desc, "photoshop:Country", false));
    }
    for (const auto& h : hier) {
        const juce::String s = juce::String::fromUTF8(h.c_str());
        if (!s.startsWithIgnoreCase("Pessoas|")) continue;
        const juce::String nome = s.fromLastOccurrenceOf("|", false, false).trim();
        if (nome.isNotEmpty()) d.pessoas.push_back(nome.toStdString());
    }
    return d;
}

std::optional<DadosBridge> lerXmpBridge(const juce::File& arquivo) {
    if (!arquivo.existsAsFile()) return std::nullopt;
    // Sidecar: IMG_001.xmp (Adobe) e IMG_001.CR2.xmp (BKR Matriz) — manda sobre o embutido.
    for (const juce::File& sc : {arquivo.withFileExtension("xmp"), juce::File(arquivo.getFullPathName() + ".xmp")}) {
        if (sc != arquivo && sc.existsAsFile() && sc.getSize() < 8 * 1024 * 1024) {
            const auto d = interpretarPacoteXmp(sc.loadFileAsString().toStdString());
            if (!d.vazio()) return d;
        }
    }
    if (arquivo.hasFileExtension("xmp")) return std::nullopt;
    bool reconhecido = false;
    std::string pacote = xmpPorEstrutura(arquivo, reconhecido);
    if (pacote.empty() && !reconhecido) pacote = xmpPorVarredura(arquivo);
    if (pacote.empty()) return std::nullopt;
    auto d = interpretarPacoteXmp(pacote);
    if (d.vazio()) return std::nullopt;
    return d;
}

void aplicarDadosBridgeAoItem(matriz::db::Database& registro, const std::string& itemId, const DadosBridge& d) {
    if (d.vazio()) return;
    namespace nomes = matriz::model::nomes;
    auto vocab = nomes::Vocabulario::carregar(registro, nomes::Vocabulario::Tipo::Tags);
    const std::string agora = matriz::model::agoraIso8601();

    if (!d.titulo.empty())
        registro.run("UPDATE item SET titulo = ?, dc_title = ? WHERE id = ?", {Value::of(d.titulo), Value::of(d.titulo), Value::of(itemId)});
    if (!d.descricao.empty())
        registro.run("UPDATE item SET dc_description = ? WHERE id = ?", {Value::of(d.descricao), Value::of(itemId)});
    {
        const juce::String ano = juce::String::fromUTF8(d.dataCriacao.c_str()).trim();
        if (ano.length() >= 4 && ano.substring(0, 4).containsOnly("0123456789"))
            registro.run("UPDATE item SET ano = ? WHERE id = ?", {Value::of(ano.substring(0, 4).toStdString()), Value::of(itemId)});
    }

    // Tags e pessoas são ADICIONADAS; mesmo nome sem diferenciar maiúsculas = mesma entrada (vocabulário do projeto).
    auto adicionarTag = [&](const std::string& nome) {
        const std::string canon = vocab.canonico(nome);
        if (canon.empty()) return;
        registro.run("INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                     {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(canon)});
    };
    std::set<std::string> chavesPessoas;
    for (const auto& p : d.pessoas) chavesPessoas.insert(nomes::chave(p));
    for (const auto& k : d.palavrasChave)
        if (!chavesPessoas.count(nomes::chave(k))) adicionarTag(k);
    if (!d.pessoas.empty()) {
        registro.exec("CREATE TABLE IF NOT EXISTS collection_person (id TEXT PRIMARY KEY, nome TEXT NOT NULL UNIQUE, criado_em TEXT NOT NULL)");
        std::set<std::string> existentes;
        auto st = registro.prepare("SELECT nome FROM collection_person");
        while (st.step()) existentes.insert(nomes::chave(st.columnText(0)));
        for (const auto& p : d.pessoas) {
            const std::string canon = vocab.canonico(p);
            if (canon.empty()) continue;
            if (!existentes.count(nomes::chave(canon))) {
                registro.run("INSERT OR IGNORE INTO collection_person (id, nome, criado_em) VALUES (?, ?, ?)",
                             {Value::of(matriz::model::novoUuid()), Value::of(canon), Value::of(agora)});
                existentes.insert(nomes::chave(canon));
            }
            adicionarTag(p);  // o seletor PEOPLE também põe o nome como tag
        }
    }

    if (!d.local.empty() || !d.cidade.empty() || !d.estado.empty() || !d.pais.empty()) {
        matriz::analytics::AssetGeolocation geo;
        if (auto atual = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(registro, itemId)) geo = *atual;
        geo.assetId = itemId;
        if (!d.local.empty() && !geo.locality) geo.locality = d.local;
        if (!d.cidade.empty() && !geo.city) geo.city = d.cidade;
        if (!d.estado.empty() && !geo.stateProvince) geo.stateProvince = d.estado;
        if (!d.pais.empty() && !geo.country) geo.country = d.pais;
        if (geo.source == matriz::analytics::GeoSource::None) geo.source = matriz::analytics::GeoSource::Imported;
        matriz::analytics::AssetGeolocationRepository::salvar(registro, geo);
    }
}

}  // namespace matriz::consolidacao::bridge
