#include "PdfRaster.h"

namespace matriz::provadata {

namespace {

// Texto PDF: ASCII puro entre parênteses; qualquer outra coisa vai como UTF-16BE em hexa.
juce::String textoPdf(const juce::String& s) {
    bool ascii = true;
    for (auto p = s.getCharPointer(); !p.isEmpty();) if (p.getAndAdvance() > 126) { ascii = false; break; }
    if (ascii) return "(" + s.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)") + ")";
    juce::String hex = "<FEFF";
    for (auto p = s.getCharPointer(); !p.isEmpty();) {
        const juce::juce_wchar c = p.getAndAdvance();
        if (c >= 0x10000) {
            const auto v = c - 0x10000;
            hex += juce::String::toHexString(static_cast<int>(0xD800 + (v >> 10))).paddedLeft('0', 4).toUpperCase();
            hex += juce::String::toHexString(static_cast<int>(0xDC00 + (v & 0x3FF))).paddedLeft('0', 4).toUpperCase();
        } else {
            hex += juce::String::toHexString(static_cast<int>(c)).paddedLeft('0', 4).toUpperCase();
        }
    }
    return hex + ">";
}

void escrever(juce::MemoryOutputStream& o, const juce::String& s) { o.write(s.toRawUTF8(), s.getNumBytesAsUTF8()); }

} // namespace

bool escreverPdfDePaginas(const std::vector<juce::Image>& paginas, const juce::File& saida, const juce::String& titulo, const juce::String& autor,
                          float qualidadeJpeg) {
    if (paginas.empty()) return false;
    constexpr double larguraPt = 595.28, alturaPt = 841.89;
    const int n = static_cast<int>(paginas.size());
    juce::MemoryOutputStream o;
    std::vector<size_t> deslocamentos;  // índice = número do objeto - 1
    const auto iniciarObjeto = [&](int numero) {
        if (static_cast<int>(deslocamentos.size()) < numero) deslocamentos.resize(static_cast<size_t>(numero), 0);
        deslocamentos[static_cast<size_t>(numero - 1)] = static_cast<size_t>(o.getDataSize());
        escrever(o, juce::String(numero) + " 0 obj\n");
    };

    escrever(o, "%PDF-1.4\n");
    const char bin[] = {'%', static_cast<char>(0xE2), static_cast<char>(0xE3), static_cast<char>(0xCF), static_cast<char>(0xD3), '\n'};
    o.write(bin, sizeof(bin));

    // 1 = catálogo, 2 = páginas, 3 = info; página i: 4+3i (página), 5+3i (imagem), 6+3i (conteúdo)
    iniciarObjeto(1);
    escrever(o, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    iniciarObjeto(2);
    juce::String kids;
    for (int i = 0; i < n; ++i) kids += juce::String(4 + 3 * i) + " 0 R ";
    escrever(o, "<< /Type /Pages /Kids [" + kids + "] /Count " + juce::String(n) + " >>\nendobj\n");
    iniciarObjeto(3);
    const auto agora = juce::Time::getCurrentTime();
    escrever(o, "<< /Title " + textoPdf(titulo) + " /Author " + textoPdf(autor) + " /Producer (BKR Matriz Mini) /CreationDate (D:" +
                    agora.formatted("%Y%m%d%H%M%S") + ") >>\nendobj\n");

    for (int i = 0; i < n; ++i) {
        const int pg = 4 + 3 * i, im = 5 + 3 * i, ct = 6 + 3 * i;
        iniciarObjeto(pg);
        escrever(o, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + juce::String(larguraPt, 2) + " " + juce::String(alturaPt, 2) +
                        "] /Resources << /XObject << /Im0 " + juce::String(im) + " 0 R >> >> /Contents " + juce::String(ct) + " 0 R >>\nendobj\n");

        juce::Image rgb = paginas[static_cast<size_t>(i)];
        if (rgb.getFormat() != juce::Image::RGB) {
            juce::Image copia(juce::Image::RGB, rgb.getWidth(), rgb.getHeight(), true);
            juce::Graphics g(copia);
            g.fillAll(juce::Colours::white);
            g.drawImageAt(rgb, 0, 0);
            rgb = copia;
        }
        juce::MemoryOutputStream jpg;
        juce::JPEGImageFormat fmt;
        fmt.setQuality(qualidadeJpeg);
        if (!fmt.writeImageToStream(rgb, jpg)) return false;
        iniciarObjeto(im);
        escrever(o, "<< /Type /XObject /Subtype /Image /Width " + juce::String(rgb.getWidth()) + " /Height " + juce::String(rgb.getHeight()) +
                        " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " + juce::String(static_cast<juce::int64>(jpg.getDataSize())) +
                        " >>\nstream\n");
        o.write(jpg.getData(), jpg.getDataSize());
        escrever(o, "\nendstream\nendobj\n");

        const juce::String conteudo = "q " + juce::String(larguraPt, 2) + " 0 0 " + juce::String(alturaPt, 2) + " 0 0 cm /Im0 Do Q\n";
        iniciarObjeto(ct);
        escrever(o, "<< /Length " + juce::String(conteudo.getNumBytesAsUTF8()) + " >>\nstream\n" + conteudo + "endstream\nendobj\n");
    }

    const int total = 3 + 3 * n;
    const auto posXref = o.getDataSize();
    escrever(o, "xref\n0 " + juce::String(total + 1) + "\n0000000000 65535 f \n");
    for (int i = 1; i <= total; ++i) escrever(o, juce::String(static_cast<juce::int64>(deslocamentos[static_cast<size_t>(i - 1)])).paddedLeft('0', 10) + " 00000 n \n");
    escrever(o, "trailer\n<< /Size " + juce::String(total + 1) + " /Root 1 0 R /Info 3 0 R >>\nstartxref\n" + juce::String(static_cast<juce::int64>(posXref)) + "\n%%EOF\n");

    saida.getParentDirectory().createDirectory();
    return saida.replaceWithData(o.getData(), o.getDataSize());
}

} // namespace matriz::provadata
