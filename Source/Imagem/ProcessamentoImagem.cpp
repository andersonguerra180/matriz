#include "ProcessamentoImagem.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

namespace matriz::imagem {

namespace {

inline uint16_t lerU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint16_t lerU16LE(const uint8_t* p) {
    return static_cast<uint16_t>((p[1] << 8) | p[0]);
}

inline uint32_t lerU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

inline void gravarU16BE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

inline void gravarU32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

// Cálculo CRC32 para chunks PNG
uint32_t calcularCrc32Png(const uint8_t* dados, size_t tamanho) {
    static uint32_t tabela[256];
    static bool tabelaInicializada = false;
    if (!tabelaInicializada) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
            }
            tabela[i] = c;
        }
        tabelaInicializada = true;
    }

    uint32_t c = 0xFFFFFFFFL;
    for (size_t i = 0; i < tamanho; ++i) {
        c = tabela[(c ^ dados[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFL;
}

} // namespace

// ==============================================================================
// 2. Parser APP1/EXIF para Orientação
// ==============================================================================

int orientacaoExif(const uint8_t* dados, size_t tamanho) {
    if (dados == nullptr || tamanho < 14) return 1;

    // Deve começar com SOI (0xFF, 0xD8)
    if (dados[0] != 0xFF || dados[1] != 0xD8) return 1;

    size_t pos = 2;
    while (pos + 4 < tamanho) {
        if (dados[pos] != 0xFF) break;
        uint8_t marker = dados[pos + 1];
        if (marker == 0xDA || marker == 0xD9) break; // SOS ou EOI

        uint16_t len = lerU16BE(&dados[pos + 2]);
        if (pos + 2 + len > tamanho) break;

        // APP1 Marker: 0xFF, 0xE1
        if (marker == 0xE1 && len >= 14) {
            const uint8_t* app1 = &dados[pos + 4];
            size_t app1Len = len - 2;

            if (std::memcmp(app1, "Exif\0\0", 6) == 0 && app1Len >= 14) {
                const uint8_t* tiff = app1 + 6;
                size_t tiffLen = app1Len - 6;

                bool isLE = (tiff[0] == 'I' && tiff[1] == 'I');
                bool isBE = (tiff[0] == 'M' && tiff[1] == 'M');
                if (!isLE && !isBE) return 1;

                auto ler16 = [isLE](const uint8_t* p) -> uint16_t {
                    return isLE ? lerU16LE(p) : lerU16BE(p);
                };
                auto ler32 = [isLE](const uint8_t* p) -> uint32_t {
                    if (isLE) {
                        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
                    }
                    return lerU32BE(p);
                };

                uint16_t magic = ler16(tiff + 2);
                if (magic != 42) return 1;

                uint32_t offsetIfd0 = ler32(tiff + 4);
                if (offsetIfd0 + 2 > tiffLen) return 1;

                uint16_t numEntries = ler16(tiff + offsetIfd0);
                size_t entryPos = offsetIfd0 + 2;

                for (uint16_t i = 0; i < numEntries; ++i) {
                    if (entryPos + 12 > tiffLen) break;
                    uint16_t tag = ler16(tiff + entryPos);
                    if (tag == 0x0112) { // Orientation
                        uint16_t val = ler16(tiff + entryPos + 8);
                        if (val >= 1 && val <= 8) return static_cast<int>(val);
                        return 1;
                    }
                    entryPos += 12;
                }
            }
        }

        pos += 2 + len;
    }

    return 1;
}

// ==============================================================================
// 3. Aplicação Pura de Orientação em Pixels (8 Casos EXIF)
// ==============================================================================

ImagemBuffer aplicarOrientacao(const ImagemBuffer& src, int orientacao) {
    if (!src.valido() || orientacao <= 1 || orientacao > 8) {
        return src;
    }

    const int W = src.largura;
    const int H = src.altura;

    // Para orientações 5, 6, 7, 8: largura e altura se invertem
    bool trocaDimensoes = (orientacao >= 5 && orientacao <= 8);
    const int destW = trocaDimensoes ? H : W;
    const int destH = trocaDimensoes ? W : H;

    ImagemBuffer dst(destW, destH);

    for (int dy = 0; dy < destH; ++dy) {
        for (int dx = 0; dx < destW; ++dx) {
            int sx = 0;
            int sy = 0;

            switch (orientacao) {
                case 1: // Normal
                    sx = dx;
                    sy = dy;
                    break;
                case 2: // Flip Horizontal
                    sx = W - 1 - dx;
                    sy = dy;
                    break;
                case 3: // 180°
                    sx = W - 1 - dx;
                    sy = H - 1 - dy;
                    break;
                case 4: // Flip Vertical
                    sx = dx;
                    sy = H - 1 - dy;
                    break;
                case 5: // Transpose
                    sx = dy;
                    sy = dx;
                    break;
                case 6: // 90° CW
                    sx = dy;
                    sy = H - 1 - dx;
                    break;
                case 7: // Transverse
                    sx = W - 1 - dy;
                    sy = H - 1 - dx;
                    break;
                case 8: // 270° CW (90° CCW)
                    sx = W - 1 - dy;
                    sy = dx;
                    break;
                default:
                    sx = dx;
                    sy = dy;
                    break;
            }

            sx = std::clamp(sx, 0, W - 1);
            sy = std::clamp(sy, 0, H - 1);

            const uint8_t* pSrc = src.pixel(sx, sy);
            uint8_t* pDst = dst.pixel(dx, dy);
            pDst[0] = pSrc[0];
            pDst[1] = pSrc[1];
            pDst[2] = pSrc[2];
            pDst[3] = pSrc[3];
        }
    }

    return dst;
}

// ==============================================================================
// 4. Redimensionamento de Alta Qualidade (Média por Área)
// ==============================================================================

ImagemBuffer redimensionar(const ImagemBuffer& src, int novoW, int novoH) {
    if (!src.valido() || novoW <= 0 || novoH <= 0) return ImagemBuffer();
    if (src.largura == novoW && src.altura == novoH) return src;

    ImagemBuffer dst(novoW, novoH);

    const double xRatio = static_cast<double>(src.largura) / static_cast<double>(novoW);
    const double yRatio = static_cast<double>(src.altura) / static_cast<double>(novoH);

    for (int dy = 0; dy < novoH; ++dy) {
        const double sy0 = dy * yRatio;
        const double sy1 = (dy + 1) * yRatio;
        const int iy0 = static_cast<int>(sy0);
        const int iy1 = std::min(static_cast<int>(std::ceil(sy1)), src.altura);

        for (int dx = 0; dx < novoW; ++dx) {
            const double sx0 = dx * xRatio;
            const double sx1 = (dx + 1) * xRatio;
            const int ix0 = static_cast<int>(sx0);
            const int ix1 = std::min(static_cast<int>(std::ceil(sx1)), src.largura);

            double accR = 0.0;
            double accG = 0.0;
            double accB = 0.0;
            double accA = 0.0;
            double totalPeso = 0.0;

            for (int iy = iy0; iy < iy1; ++iy) {
                const double pesoY = std::max(0.0, std::min(sy1, static_cast<double>(iy + 1)) - std::max(sy0, static_cast<double>(iy)));
                if (pesoY <= 0.0) continue;

                for (int ix = ix0; ix < ix1; ++ix) {
                    const double pesoX = std::max(0.0, std::min(sx1, static_cast<double>(ix + 1)) - std::max(sx0, static_cast<double>(ix)));
                    const double peso = pesoX * pesoY;
                    if (peso <= 0.0) continue;

                    const uint8_t* p = src.pixel(ix, iy);
                    accR += p[0] * peso;
                    accG += p[1] * peso;
                    accB += p[2] * peso;
                    accA += p[3] * peso;
                    totalPeso += peso;
                }
            }

            uint8_t* pDst = dst.pixel(dx, dy);
            if (totalPeso > 0.0) {
                pDst[0] = static_cast<uint8_t>(std::clamp(std::round(accR / totalPeso), 0.0, 255.0));
                pDst[1] = static_cast<uint8_t>(std::clamp(std::round(accG / totalPeso), 0.0, 255.0));
                pDst[2] = static_cast<uint8_t>(std::clamp(std::round(accB / totalPeso), 0.0, 255.0));
                pDst[3] = static_cast<uint8_t>(std::clamp(std::round(accA / totalPeso), 0.0, 255.0));
            } else {
                pDst[0] = 255; pDst[1] = 255; pDst[2] = 255; pDst[3] = 255;
            }
        }
    }

    return dst;
}

// ==============================================================================
// 5. Enquadramento no Papel (Preencher ou Encaixar com Proporção Travada)
// ==============================================================================

ImagemBuffer enquadrar(const ImagemBuffer& src, int papelW, int papelH,
                       ModoEnquadramento modo, float offsetX, float offsetY) {
    if (!src.valido() || papelW <= 0 || papelH <= 0) return ImagemBuffer();

    offsetX = std::clamp(offsetX, -1.0f, 1.0f);
    offsetY = std::clamp(offsetY, -1.0f, 1.0f);

    if (modo == ModoEnquadramento::Preencher) {
        // Escala para cobrir todo o papel (maior escala)
        double scale = std::max(static_cast<double>(papelW) / src.largura,
                                static_cast<double>(papelH) / src.altura);
        int sw = std::max(papelW, static_cast<int>(std::round(src.largura * scale)));
        int sh = std::max(papelH, static_cast<int>(std::round(src.altura * scale)));

        ImagemBuffer redim = redimensionar(src, sw, sh);

        int excessoX = sw - papelW;
        int excessoY = sh - papelH;

        // Deslocamento: 0.0 = centro, -1.0 = topo/esquerda, 1.0 = base/direita
        int cropX = static_cast<int>(std::round((excessoX / 2.0) + (offsetX * (excessoX / 2.0))));
        int cropY = static_cast<int>(std::round((excessoY / 2.0) + (offsetY * (excessoY / 2.0))));
        cropX = std::clamp(cropX, 0, excessoX);
        cropY = std::clamp(cropY, 0, excessoY);

        ImagemBuffer dst(papelW, papelH);
        for (int y = 0; y < papelH; ++y) {
            const uint8_t* pSrc = redim.pixel(cropX, cropY + y);
            uint8_t* pDst = dst.pixel(0, y);
            std::memcpy(pDst, pSrc, static_cast<size_t>(papelW * 4));
        }
        return dst;
    }

    // Modo Encaixar: Foto inteira com margem branca
    double scale = std::min(static_cast<double>(papelW) / src.largura,
                            static_cast<double>(papelH) / src.altura);
    int sw = std::max(1, static_cast<int>(std::round(src.largura * scale)));
    int sh = std::max(1, static_cast<int>(std::round(src.altura * scale)));

    ImagemBuffer redim = redimensionar(src, sw, sh);

    // Fundo branco
    ImagemBuffer dst(papelW, papelH, 255, 255, 255, 255);

    int posX = (papelW - sw) / 2;
    int posY = (papelH - sh) / 2;

    for (int y = 0; y < sh; ++y) {
        const uint8_t* pSrc = redim.pixel(0, y);
        uint8_t* pDst = dst.pixel(posX, posY + y);
        std::memcpy(pDst, pSrc, static_cast<size_t>(sw * 4));
    }

    return dst;
}

// ==============================================================================
// 6. Ajustes via LUT por Canal (Brilho, Contraste, Saturação, Levels, Gamma)
// ==============================================================================

void aplicarAjustes(ImagemBuffer& img,
                    float brilho,
                    float contraste,
                    float saturacao,
                    int levelMin,
                    int levelMax,
                    float gamma) {
    if (!img.valido()) return;

    brilho = std::clamp(brilho, -1.0f, 1.0f);
    contraste = std::clamp(contraste, -1.0f, 1.0f);
    saturacao = std::clamp(saturacao, 0.0f, 2.0f);
    levelMin = std::clamp(levelMin, 0, 254);
    levelMax = std::clamp(levelMax, levelMin + 1, 255);
    gamma = std::clamp(gamma, 0.2f, 3.0f);

    bool precisaAjusteLuma = (brilho != 0.0f || contraste != 0.0f ||
                             levelMin != 0 || levelMax != 255 || gamma != 1.0f);
    bool precisaAjusteSat = (saturacao != 1.0f);

    if (!precisaAjusteLuma && !precisaAjusteSat) return;

    // Pré-computa tabela LUT de 256 valores para luminância
    uint8_t lut[256];
    const float rangeLevel = static_cast<float>(levelMax - levelMin);
    const float invGamma = 1.0f / gamma;
    const float contrastFactor = (contraste >= 0.0f) ? (1.0f + contraste * 2.0f) : (1.0f + contraste);

    for (int i = 0; i < 256; ++i) {
        float v = static_cast<float>(i);
        // Levels
        v = std::clamp((v - levelMin) / rangeLevel, 0.0f, 1.0f);
        // Gamma
        if (gamma != 1.0f) v = std::pow(v, invGamma);
        // Contraste
        if (contraste != 0.0f) v = (v - 0.5f) * contrastFactor + 0.5f;
        // Brilho
        if (brilho != 0.0f) v += brilho;

        lut[i] = static_cast<uint8_t>(std::clamp(std::round(v * 255.0f), 0.0f, 255.0f));
    }

    const size_t totalPixels = static_cast<size_t>(img.largura * img.altura);
    uint8_t* p = img.pixels.data();

    for (size_t i = 0; i < totalPixels; ++i, p += 4) {
        uint8_t r = precisaAjusteLuma ? lut[p[0]] : p[0];
        uint8_t g = precisaAjusteLuma ? lut[p[1]] : p[1];
        uint8_t b = precisaAjusteLuma ? lut[p[2]] : p[2];

        if (precisaAjusteSat) {
            float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            r = static_cast<uint8_t>(std::clamp(std::round(luma + (r - luma) * saturacao), 0.0f, 255.0f));
            g = static_cast<uint8_t>(std::clamp(std::round(luma + (g - luma) * saturacao), 0.0f, 255.0f));
            b = static_cast<uint8_t>(std::clamp(std::round(luma + (b - luma) * saturacao), 0.0f, 255.0f));
        }

        p[0] = r;
        p[1] = g;
        p[2] = b;
    }
}

// ==============================================================================
// 7. Nitidez Leve (Unsharp Mask)
// ==============================================================================

void nitidez(ImagemBuffer& img, float forca) {
    if (!img.valido() || forca <= 0.0f) return;
    forca = std::clamp(forca, 0.0f, 2.0f);

    const int W = img.largura;
    const int H = img.altura;
    std::vector<uint8_t> borrada = img.pixels;

    // Blur 3x3 separável simples
    std::vector<uint8_t> tempH = img.pixels;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int x0 = std::max(0, x - 1);
            int x1 = x;
            int x2 = std::min(W - 1, x + 1);

            const uint8_t* p0 = img.pixel(x0, y);
            const uint8_t* p1 = img.pixel(x1, y);
            const uint8_t* p2 = img.pixel(x2, y);

            uint8_t* pOut = &tempH[static_cast<size_t>((y * W + x) * 4)];
            for (int c = 0; c < 3; ++c) {
                pOut[c] = static_cast<uint8_t>((p0[c] + p1[c] * 2 + p2[c] + 2) >> 2);
            }
        }
    }

    for (int y = 0; y < H; ++y) {
        int y0 = std::max(0, y - 1);
        int y1 = y;
        int y2 = std::min(H - 1, y + 1);

        for (int x = 0; x < W; ++x) {
            const uint8_t* p0 = &tempH[static_cast<size_t>((y0 * W + x) * 4)];
            const uint8_t* p1 = &tempH[static_cast<size_t>((y1 * W + x) * 4)];
            const uint8_t* p2 = &tempH[static_cast<size_t>((y2 * W + x) * 4)];

            uint8_t* pOut = &borrada[static_cast<size_t>((y * W + x) * 4)];
            for (int c = 0; c < 3; ++c) {
                pOut[c] = static_cast<uint8_t>((p0[c] + p1[c] * 2 + p2[c] + 2) >> 2);
            }
        }
    }

    // Unsharp mask: orig + forca * (orig - borrada)
    const size_t totalPixels = static_cast<size_t>(W * H);
    uint8_t* pOrig = img.pixels.data();
    const uint8_t* pBlur = borrada.data();

    for (size_t i = 0; i < totalPixels; ++i, pOrig += 4, pBlur += 4) {
        for (int c = 0; c < 3; ++c) {
            float diff = static_cast<float>(pOrig[c]) - static_cast<float>(pBlur[c]);
            float res = static_cast<float>(pOrig[c]) + forca * diff;
            pOrig[c] = static_cast<uint8_t>(std::clamp(std::round(res), 0.0f, 255.0f));
        }
    }
}

// ==============================================================================
// 1. Leitura de Imagem (Apenas JPEG e PNG)
// ==============================================================================

ResultadoLeitura lerImagem(const juce::File& arquivo) {
    ResultadoLeitura out;

    if (!arquivo.existsAsFile()) {
        out.erro = "Arquivo inexistente ou inacessível: " + arquivo.getFullPathName();
        return out;
    }

    auto ext = arquivo.getFileExtension().toLowerCase();
    bool isJpg = (ext == ".jpg" || ext == ".jpeg");
    bool isPng = (ext == ".png");

    if (!isJpg && !isPng) {
        out.erro = "Formato não suportado (" + ext + "). Apenas arquivos JPG e PNG são aceitos.";
        return out;
    }

    juce::MemoryBlock dados;
    if (!arquivo.loadFileAsData(dados) || dados.getSize() < 8) {
        out.erro = "Arquivo vazio ou ilegível.";
        return out;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(dados.getData());
    const size_t tamanho = dados.getSize();

    // Verificação de assinatura/magic bytes
    if (isJpg) {
        if (bytes[0] != 0xFF || bytes[1] != 0xD8 || bytes[2] != 0xFF) {
            out.erro = "Assinatura JPEG inválida ou corrompida.";
            return out;
        }

        // Checar se é JPEG CMYK (4 componentes de cor)
        size_t p = 2;
        while (p + 8 < tamanho) {
            if (bytes[p] != 0xFF) break;
            uint8_t m = bytes[p + 1];
            if (m == 0xDA || m == 0xD9) break; // SOS / EOI
            uint16_t len = lerU16BE(&bytes[p + 2]);
            if (p + 2 + len > tamanho) break;

            // SOF0 (0xC0), SOF1 (0xC1), SOF2 (0xC2)
            if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB) || (m >= 0xCD && m <= 0xCF)) {
                if (len >= 8) {
                    uint8_t numComponentes = bytes[p + 9];
                    if (numComponentes == 4) {
                        out.erro = "Imagem em formato JPEG CMYK não suportada para impressão (converta para RGB).";
                        return out;
                    }
                }
                break;
            }
            p += 2 + len;
        }

        out.orientacaoExif = orientacaoExif(bytes, tamanho);
    } else if (isPng) {
        static const uint8_t pngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        if (std::memcmp(bytes, pngSig, 8) != 0) {
            out.erro = "Assinatura PNG inválida ou corrompida.";
            return out;
        }
        out.orientacaoExif = 1; // PNG não tem EXIF padronizado
    }

    // Lê DPI diretamente dos cabeçalhos se houver
    lerDpiDosBytes(arquivo, out.dpiX, out.dpiY);

    // Decodifica imagem via JUCE
    juce::Image img = juce::ImageFileFormat::loadFrom(arquivo);
    if (!img.isValid()) {
        out.erro = "Falha ao decodificar imagem.";
        return out;
    }

    const int W = img.getWidth();
    const int H = img.getHeight();
    ImagemBuffer buf(W, H);

    // Achata transparência em fundo branco sólido
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            juce::Colour c = img.getPixelAt(x, y);
            float a = c.getFloatAlpha();
            uint8_t r = static_cast<uint8_t>(std::round(c.getRed() * a + 255.0f * (1.0f - a)));
            uint8_t g = static_cast<uint8_t>(std::round(c.getGreen() * a + 255.0f * (1.0f - a)));
            uint8_t b = static_cast<uint8_t>(std::round(c.getBlue() * a + 255.0f * (1.0f - a)));
            buf.definirPixel(x, y, r, g, b, 255);
        }
    }

    out.buffer = std::move(buf);
    out.sucesso = true;
    return out;
}

// ==============================================================================
// 8. Gravação com Injeção de DPI e sRGB em Nível de Bytes
// ==============================================================================

bool gravar(const ImagemBuffer& img,
            const juce::File& destino,
            FormatoSaida formato,
            int qualidade,
            double dpi) {
    if (!img.valido()) return false;
    dpi = std::max(1.0, dpi);

    // Converte ImagemBuffer para juce::Image (RGB)
    juce::Image jImg(juce::Image::RGB, img.largura, img.altura, false);
    for (int y = 0; y < img.altura; ++y) {
        for (int x = 0; x < img.largura; ++x) {
            const uint8_t* p = img.pixel(x, y);
            jImg.setPixelAt(x, y, juce::Colour(p[0], p[1], p[2]));
        }
    }

    juce::MemoryOutputStream rawStream;

    if (formato == FormatoSaida::Jpeg) {
        qualidade = std::clamp(qualidade, 1, 100);
        float qFloat = qualidade / 100.0f;
        juce::JPEGImageFormat jpegFormat;
        jpegFormat.setQuality(qFloat);
        if (!jpegFormat.writeImageToStream(jImg, rawStream)) {
            return false;
        }

        const uint8_t* srcBytes = static_cast<const uint8_t*>(rawStream.getData());
        const size_t srcSize = rawStream.getDataSize();
        if (srcSize < 4 || srcBytes[0] != 0xFF || srcBytes[1] != 0xD8) return false;

        juce::MemoryBlock finalBytes;
        uint16_t dpiU16 = static_cast<uint16_t>(std::clamp(std::round(dpi), 1.0, 65535.0));

        // Checar se já tem JFIF APP0 logo após SOI
        if (srcSize > 18 && srcBytes[2] == 0xFF && srcBytes[3] == 0xE0 &&
            std::memcmp(&srcBytes[6], "JFIF\0", 5) == 0) {
            // Atualiza campos de densidade no JFIF existente
            finalBytes.append(srcBytes, srcSize);
            uint8_t* p = static_cast<uint8_t*>(finalBytes.getData()) + 2; // Aponta para o início do APP0 (0xFF, 0xE0)
            p[11] = 1; // Unidades: 1 = dots per inch (DPI)
            gravarU16BE(&p[12], dpiU16); // Xdensity
            gravarU16BE(&p[14], dpiU16); // Ydensity
        } else {
            // Insere cabeçalho JFIF padrão de 18 bytes após SOI (0xFF, 0xD8)
            uint8_t jfifHeader[18] = {
                0xFF, 0xE0,       // APP0 marker
                0x00, 0x10,       // Comprimento = 16 bytes
                'J', 'F', 'I', 'F', 0x00, // Identificador "JFIF\0"
                0x01, 0x01,       // Versão 1.01
                0x01,             // Unidades = 1 (DPI)
                static_cast<uint8_t>((dpiU16 >> 8) & 0xFF), static_cast<uint8_t>(dpiU16 & 0xFF), // Xdensity
                static_cast<uint8_t>((dpiU16 >> 8) & 0xFF), static_cast<uint8_t>(dpiU16 & 0xFF), // Ydensity
                0x00, 0x00        // Miniatura 0x0
            };

            finalBytes.append(srcBytes, 2); // SOI
            finalBytes.append(jfifHeader, 18);
            finalBytes.append(srcBytes + 2, srcSize - 2);
        }

        if (destino.exists()) destino.deleteFile();
        return destino.replaceWithData(finalBytes.getData(), finalBytes.getSize());
    }

    // Formato PNG
    juce::PNGImageFormat pngFormat;
    if (!pngFormat.writeImageToStream(jImg, rawStream)) {
        return false;
    }

    const uint8_t* srcBytes = static_cast<const uint8_t*>(rawStream.getData());
    const size_t srcSize = rawStream.getDataSize();
    static const uint8_t pngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (srcSize < 8 || std::memcmp(srcBytes, pngSig, 8) != 0) return false;

    // Constrói chunk pHYs: 9 bytes de dados
    // Pixels por metro = round(dpi / 0.0254)
    uint32_t ppm = static_cast<uint32_t>(std::round(dpi / 0.0254));

    uint8_t physChunk[21];
    gravarU32BE(&physChunk[0], 9); // Comprimento dos dados
    physChunk[4] = 'p'; physChunk[5] = 'H'; physChunk[6] = 'Y'; physChunk[7] = 's';
    gravarU32BE(&physChunk[8], ppm); // PPM X
    gravarU32BE(&physChunk[12], ppm); // PPM Y
    physChunk[16] = 1; // Unidade = metro
    uint32_t physCrc = calcularCrc32Png(&physChunk[4], 13);
    gravarU32BE(&physChunk[17], physCrc);

    // Constrói chunk sRGB: 1 byte de dados (0 = Perceptual)
    uint8_t srgbChunk[13];
    gravarU32BE(&srgbChunk[0], 1);
    srgbChunk[4] = 's'; srgbChunk[5] = 'R'; srgbChunk[6] = 'G'; srgbChunk[7] = 'B';
    srgbChunk[8] = 0; // Rendering intent: Perceptual
    uint32_t srgbCrc = calcularCrc32Png(&srgbChunk[4], 5);
    gravarU32BE(&srgbChunk[9], srgbCrc);

    // Localiza o fim do chunk IHDR (sempre o primeiro chunk após a assinatura de 8 bytes)
    // IHDR: 4 bytes len + 4 bytes "IHDR" + 13 bytes data + 4 bytes CRC = 25 bytes
    size_t posAposIhdr = 8 + 25;
    if (posAposIhdr > srcSize) return false;

    juce::MemoryBlock finalBytes;
    finalBytes.append(srcBytes, posAposIhdr);
    finalBytes.append(srgbChunk, sizeof(srgbChunk));
    finalBytes.append(physChunk, sizeof(physChunk));
    finalBytes.append(srcBytes + posAposIhdr, srcSize - posAposIhdr);

    if (destino.exists()) destino.deleteFile();
    return destino.replaceWithData(finalBytes.getData(), finalBytes.getSize());
}

// ==============================================================================
// Leitor Independente de DPI para Testes e Validação
// ==============================================================================

bool lerDpiDosBytes(const juce::File& arquivo, double& outDpiX, double& outDpiY) {
    outDpiX = 72.0;
    outDpiY = 72.0;

    if (!arquivo.existsAsFile()) return false;

    juce::MemoryBlock dados;
    if (!arquivo.loadFileAsData(dados) || dados.getSize() < 16) return false;

    const uint8_t* b = static_cast<const uint8_t*>(dados.getData());
    const size_t sz = dados.getSize();

    // JPEG JFIF
    if (b[0] == 0xFF && b[1] == 0xD8) {
        size_t p = 2;
        while (p + 4 < sz) {
            if (b[p] != 0xFF) break;
            uint8_t m = b[p + 1];
            if (m == 0xDA || m == 0xD9) break;
            uint16_t len = lerU16BE(&b[p + 2]);
            if (p + 2 + len > sz) break;

            if (m == 0xE0 && len >= 16 && std::memcmp(&b[p + 4], "JFIF\0", 5) == 0) {
                uint8_t unit = b[p + 11];
                uint16_t xDens = lerU16BE(&b[p + 12]);
                uint16_t yDens = lerU16BE(&b[p + 14]);

                if (unit == 1 && xDens > 0 && yDens > 0) { // DPI
                    outDpiX = xDens;
                    outDpiY = yDens;
                    return true;
                } else if (unit == 2 && xDens > 0 && yDens > 0) { // DPCM
                    outDpiX = xDens * 2.54;
                    outDpiY = yDens * 2.54;
                    return true;
                }
            }
            p += 2 + len;
        }
    }

    // PNG pHYs
    static const uint8_t pngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (sz >= 8 && std::memcmp(b, pngSig, 8) == 0) {
        size_t p = 8;
        while (p + 12 < sz) {
            uint32_t len = lerU32BE(&b[p]);
            if (p + 12 + len > sz) break;

            if (std::memcmp(&b[p + 4], "pHYs", 4) == 0 && len >= 9) {
                uint32_t ppmX = lerU32BE(&b[p + 8]);
                uint32_t ppmY = lerU32BE(&b[p + 12]);
                uint8_t unit = b[p + 16];

                if (unit == 1 && ppmX > 0 && ppmY > 0) { // Metros
                    outDpiX = std::round(ppmX * 0.0254);
                    outDpiY = std::round(ppmY * 0.0254);
                    return true;
                }
            }
            p += 12 + len;
        }
    }

    return false;
}

} // namespace matriz::imagem
