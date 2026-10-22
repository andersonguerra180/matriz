#include "Tablet.h"

#include <cmath>

namespace matriz::ui {

juce::Font fonteUi(float tamanhoToken, int estilo) {
    return juce::Font(juce::FontOptions(tamanhoToken * escalaFonte(), estilo));
}

void desenharCartao(juce::Graphics& g, juce::Rectangle<float> area, float raio, juce::Colour preenchimento,
                    bool comSombra) {
    const auto& tk = tema();
    if (comSombra) {
        // Sombra barata: um retângulo arredondado deslocado (nada de blur em software a cada repaint).
        g.setColour(tk.sombra);
        g.fillRoundedRectangle(area.translated(0.0f, 3.5f), raio);
    }
    g.setColour(preenchimento);
    g.fillRoundedRectangle(area, raio);
    g.setColour(tk.linhaSuave);
    g.drawRoundedRectangle(area.reduced(0.5f), raio, 1.0f);
}

juce::Path caminhoCartaoComSeta(juce::Rectangle<float> a, float r, float h, bool seta) {
    juce::Path p;
    const float x = a.getX(), y = a.getY(), R = a.getRight(), B = a.getBottom();
    p.startNewSubPath(x + r, y);
    p.lineTo(R - r, y);
    p.quadraticTo(R, y, R, y + r);
    p.lineTo(R, B - r);
    p.quadraticTo(R, B, R - r, B);
    if (seta) {
        const float cx = a.getCentreX();
        const float aw = juce::jmin(26.0f, a.getWidth() / 6.0f);
        p.lineTo(cx + aw, B);
        p.lineTo(cx, B + h);
        p.lineTo(cx - aw, B);
    }
    p.lineTo(x + r, B);
    p.quadraticTo(x, B, x, B - r);
    p.lineTo(x, y + r);
    p.quadraticTo(x, y, x + r, y);
    p.closeSubPath();
    return p;
}

void desenharCartaoComSeta(juce::Graphics& g, juce::Rectangle<float> area, float raio, float alturaSeta, juce::Colour fill, bool comSeta) {
    const auto& tk = tema();
    const auto caminho = caminhoCartaoComSeta(area, raio, alturaSeta, comSeta);
    g.setColour(tk.sombra);
    g.fillPath(caminho, juce::AffineTransform::translation(0.0f, 3.5f));
    g.setColour(fill);
    g.fillPath(caminho);
    g.setColour(tk.linhaSuave);
    g.strokePath(caminho, juce::PathStrokeType(1.0f));
}

void desenharCirculoNumero(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& texto) {
    const auto& tk = tema();
    g.setColour(tk.acento);
    g.fillEllipse(area);
    g.setColour(tk.textoSobreAcento);
    g.setFont(fonteUi(tk.tamanhoFontePequena + 1.0f, juce::Font::bold));
    g.drawText(texto, area.toNearestInt(), juce::Justification::centred, false);
}

void desenharRotuloSecao(juce::Graphics& g, const juce::String& texto, juce::Rectangle<int> area, juce::Colour cor,
                         juce::Justification just) {
    g.setColour(cor);
    g.setFont(fonteUi(tema().fonteRotuloSecao, juce::Font::bold).withExtraKerningFactor(0.06f));
    // toUpperCase do JUCE não converte letras acentuadas (ú → Ú) no locale C do macOS: converte à mão (Latin-1).
    juce::String maiusculas;
    for (auto c : texto.toUpperCase()) maiusculas += (c >= 0xE0 && c <= 0xFE && c != 0xF7) ? static_cast<juce::juce_wchar>(c - 0x20) : c;
    g.drawText(maiusculas, area, just, true);
}

void desenharIcone(juce::Graphics& g, IconeUi icone, juce::Rectangle<float> a, juce::Colour cor) {
    g.setColour(cor);
    const float w = a.getWidth(), h = a.getHeight();
    const float x = a.getX(), y = a.getY();
    const juce::PathStrokeType traco(juce::jmax(1.5f, w * 0.07f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    switch (icone) {
        case IconeUi::Home:
            p.startNewSubPath(x + w * 0.10f, y + h * 0.50f);
            p.lineTo(x + w * 0.50f, y + h * 0.12f);
            p.lineTo(x + w * 0.90f, y + h * 0.50f);
            p.startNewSubPath(x + w * 0.22f, y + h * 0.44f);
            p.lineTo(x + w * 0.22f, y + h * 0.88f);
            p.lineTo(x + w * 0.78f, y + h * 0.88f);
            p.lineTo(x + w * 0.78f, y + h * 0.44f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Backup:  // disco com seta para baixo
            p.addRoundedRectangle(x + w * 0.12f, y + h * 0.58f, w * 0.76f, h * 0.30f, w * 0.08f);
            g.strokePath(p, traco);
            p.clear();
            p.startNewSubPath(x + w * 0.50f, y + h * 0.10f);
            p.lineTo(x + w * 0.50f, y + h * 0.46f);
            p.startNewSubPath(x + w * 0.34f, y + h * 0.32f);
            p.lineTo(x + w * 0.50f, y + h * 0.48f);
            p.lineTo(x + w * 0.66f, y + h * 0.32f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Explorer:  // pasta
            p.startNewSubPath(x + w * 0.10f, y + h * 0.30f);
            p.lineTo(x + w * 0.10f, y + h * 0.82f);
            p.lineTo(x + w * 0.90f, y + h * 0.82f);
            p.lineTo(x + w * 0.90f, y + h * 0.36f);
            p.lineTo(x + w * 0.46f, y + h * 0.36f);
            p.lineTo(x + w * 0.38f, y + h * 0.22f);
            p.lineTo(x + w * 0.10f, y + h * 0.22f);
            p.closeSubPath();
            g.strokePath(p, traco);
            break;
        case IconeUi::Stats:  // barras
            for (int i = 0; i < 3; ++i) {
                const float alturaBarra = h * (0.30f + 0.22f * static_cast<float>(i));
                g.fillRoundedRectangle(x + w * (0.14f + 0.28f * static_cast<float>(i)), y + h * 0.88f - alturaBarra,
                                       w * 0.18f, alturaBarra, w * 0.05f);
            }
            break;
        case IconeUi::Arquivo:
            p.startNewSubPath(x + w * 0.22f, y + h * 0.10f);
            p.lineTo(x + w * 0.62f, y + h * 0.10f);
            p.lineTo(x + w * 0.80f, y + h * 0.30f);
            p.lineTo(x + w * 0.80f, y + h * 0.90f);
            p.lineTo(x + w * 0.22f, y + h * 0.90f);
            p.closeSubPath();
            p.startNewSubPath(x + w * 0.62f, y + h * 0.10f);
            p.lineTo(x + w * 0.62f, y + h * 0.30f);
            p.lineTo(x + w * 0.80f, y + h * 0.30f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Pasta:
            desenharIcone(g, IconeUi::Explorer, a, cor);
            break;
        case IconeUi::Ferramentas:  // chave inglesa: cabo diagonal e boca aberta
            p.startNewSubPath(x + w * 0.20f, y + h * 0.82f);
            p.lineTo(x + w * 0.56f, y + h * 0.46f);
            g.strokePath(p, juce::PathStrokeType(traco.getStrokeThickness() * 1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            p.clear();
            p.addCentredArc(x + w * 0.68f, y + h * 0.32f, w * 0.19f, h * 0.19f, 0.0f, juce::MathConstants<float>::pi * 0.95f,
                            juce::MathConstants<float>::pi * 2.55f, true);
            g.strokePath(p, traco);
            break;
        case IconeUi::Celular:  // smartphone: corpo, alto-falante, botão e ondas de envio
            p.addRoundedRectangle(x + w * 0.24f, y + h * 0.04f, w * 0.52f, h * 0.92f, w * 0.10f);
            g.strokePath(p, traco);
            g.fillRoundedRectangle(x + w * 0.42f, y + h * 0.12f, w * 0.16f, h * 0.035f, h * 0.02f);
            g.fillEllipse(x + w * 0.455f, y + h * 0.82f, w * 0.09f, w * 0.09f);
            p.clear();
            p.startNewSubPath(x + w * 0.50f, y + h * 0.66f);
            p.lineTo(x + w * 0.50f, y + h * 0.28f);
            p.startNewSubPath(x + w * 0.38f, y + h * 0.40f);
            p.lineTo(x + w * 0.50f, y + h * 0.27f);
            p.lineTo(x + w * 0.62f, y + h * 0.40f);
            g.strokePath(p, traco);
            break;
        case IconeUi::PainelEsquerdo:
        case IconeUi::PainelDireito: {
            const bool esq = icone == IconeUi::PainelEsquerdo;
            p.addRoundedRectangle(x + w * 0.10f, y + h * 0.18f, w * 0.80f, h * 0.64f, w * 0.10f);
            g.strokePath(p, traco);
            const float faixaX = esq ? x + w * 0.38f : x + w * 0.62f;
            g.fillRect(juce::Rectangle<float>(esq ? x + w * 0.10f : faixaX, y + h * 0.18f, esq ? faixaX - (x + w * 0.10f) : (x + w * 0.90f) - faixaX, h * 0.64f).reduced(w * 0.04f));
            break;
        }
        case IconeUi::Play:
            p.addTriangle(x + w * 0.24f, y + h * 0.14f, x + w * 0.24f, y + h * 0.86f, x + w * 0.84f, y + h * 0.50f);
            g.fillPath(p.createPathWithRoundedCorners(w * 0.06f));
            break;
        case IconeUi::Pause:
            g.fillRoundedRectangle(x + w * 0.20f, y + h * 0.14f, w * 0.20f, h * 0.72f, w * 0.05f);
            g.fillRoundedRectangle(x + w * 0.60f, y + h * 0.14f, w * 0.20f, h * 0.72f, w * 0.05f);
            break;
        case IconeUi::Stop:
            g.fillRoundedRectangle(x + w * 0.20f, y + h * 0.20f, w * 0.60f, h * 0.60f, w * 0.08f);
            break;
        case IconeUi::Fechar:
            p.startNewSubPath(x + w * 0.22f, y + h * 0.22f);
            p.lineTo(x + w * 0.78f, y + h * 0.78f);
            p.startNewSubPath(x + w * 0.78f, y + h * 0.22f);
            p.lineTo(x + w * 0.22f, y + h * 0.78f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Cadeado:
            p.addRoundedRectangle(x + w * 0.20f, y + h * 0.44f, w * 0.60f, h * 0.42f, w * 0.08f);
            g.strokePath(p, traco);
            p.clear();
            p.startNewSubPath(x + w * 0.32f, y + h * 0.44f);
            p.lineTo(x + w * 0.32f, y + h * 0.32f);
            p.addCentredArc(x + w * 0.50f, y + h * 0.32f, w * 0.18f, h * 0.20f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, false);
            p.lineTo(x + w * 0.68f, y + h * 0.44f);
            g.strokePath(p, traco);
            break;
        case IconeUi::PastaMais:  // pasta com um + dentro (importar)
            p.startNewSubPath(x + w * 0.06f, y + h * 0.26f);
            p.lineTo(x + w * 0.06f, y + h * 0.82f);
            p.quadraticTo(x + w * 0.06f, y + h * 0.90f, x + w * 0.14f, y + h * 0.90f);
            p.lineTo(x + w * 0.86f, y + h * 0.90f);
            p.quadraticTo(x + w * 0.94f, y + h * 0.90f, x + w * 0.94f, y + h * 0.82f);
            p.lineTo(x + w * 0.94f, y + h * 0.38f);
            p.quadraticTo(x + w * 0.94f, y + h * 0.30f, x + w * 0.86f, y + h * 0.30f);
            p.lineTo(x + w * 0.52f, y + h * 0.30f);
            p.lineTo(x + w * 0.40f, y + h * 0.14f);
            p.lineTo(x + w * 0.14f, y + h * 0.14f);
            p.quadraticTo(x + w * 0.06f, y + h * 0.14f, x + w * 0.06f, y + h * 0.22f);
            p.closeSubPath();
            g.strokePath(p, traco);
            p.clear();
            p.startNewSubPath(x + w * 0.50f, y + h * 0.46f);
            p.lineTo(x + w * 0.50f, y + h * 0.74f);
            p.startNewSubPath(x + w * 0.36f, y + h * 0.60f);
            p.lineTo(x + w * 0.64f, y + h * 0.60f);
            g.strokePath(p, traco);
            break;
        case IconeUi::PhoneGallery:
            p.addRoundedRectangle(x + w * 0.22f, y + h * 0.08f, w * 0.56f, h * 0.84f, w * 0.09f);
            g.strokePath(p, traco);
            p.clear();
            p.addRoundedRectangle(x + w * 0.32f, y + h * 0.22f, w * 0.36f, h * 0.38f, w * 0.04f);
            g.strokePath(p, traco);
            p.clear();
            p.startNewSubPath(x + w * 0.35f, y + h * 0.54f);
            p.lineTo(x + w * 0.44f, y + h * 0.38f);
            p.lineTo(x + w * 0.52f, y + h * 0.48f);
            p.lineTo(x + w * 0.60f, y + h * 0.34f);
            p.lineTo(x + w * 0.65f, y + h * 0.54f);
            g.strokePath(p, traco);
            g.fillEllipse(x + w * 0.46f, y + h * 0.78f, w * 0.08f, w * 0.08f);
            break;
        case IconeUi::WhatsappChat:
            p.startNewSubPath(x + w * 0.20f, y + h * 0.45f);
            p.addCentredArc(x + w * 0.50f, y + h * 0.44f, w * 0.35f, h * 0.32f, 0.0f, juce::MathConstants<float>::pi * 0.25f, juce::MathConstants<float>::pi * 1.95f, true);
            p.lineTo(x + w * 0.20f, y + h * 0.85f);
            p.closeSubPath();
            g.strokePath(p, traco);
            p.clear();
            p.startNewSubPath(x + w * 0.36f, y + h * 0.36f);
            p.lineTo(x + w * 0.44f, y + h * 0.36f);
            p.startNewSubPath(x + w * 0.36f, y + h * 0.48f);
            p.lineTo(x + w * 0.64f, y + h * 0.48f);
            g.strokePath(p, traco);
            break;
        case IconeUi::SocialMedia:
            g.fillEllipse(x + w * 0.22f, y + h * 0.44f, w * 0.18f, h * 0.18f);
            g.fillEllipse(x + w * 0.62f, y + h * 0.20f, w * 0.18f, h * 0.18f);
            g.fillEllipse(x + w * 0.62f, y + h * 0.68f, w * 0.18f, h * 0.18f);
            p.startNewSubPath(x + w * 0.36f, y + h * 0.50f);
            p.lineTo(x + w * 0.64f, y + h * 0.30f);
            p.startNewSubPath(x + w * 0.36f, y + h * 0.54f);
            p.lineTo(x + w * 0.64f, y + h * 0.74f);
            g.strokePath(p, traco);
            break;
        case IconeUi::StorageUnit:
            p.addRoundedRectangle(x + w * 0.15f, y + h * 0.22f, w * 0.70f, h * 0.56f, w * 0.08f);
            g.strokePath(p, traco);
            g.fillEllipse(x + w * 0.72f, y + h * 0.46f, w * 0.08f, h * 0.08f);
            p.clear();
            p.startNewSubPath(x + w * 0.25f, y + h * 0.50f);
            p.lineTo(x + w * 0.60f, y + h * 0.50f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Documents:
            p.startNewSubPath(x + w * 0.22f, y + h * 0.12f);
            p.lineTo(x + w * 0.58f, y + h * 0.12f);
            p.lineTo(x + w * 0.78f, y + h * 0.32f);
            p.lineTo(x + w * 0.78f, y + h * 0.88f);
            p.lineTo(x + w * 0.22f, y + h * 0.88f);
            p.closeSubPath();
            p.startNewSubPath(x + w * 0.58f, y + h * 0.12f);
            p.lineTo(x + w * 0.58f, y + h * 0.32f);
            p.lineTo(x + w * 0.78f, y + h * 0.32f);
            p.startNewSubPath(x + w * 0.32f, y + h * 0.46f);
            p.lineTo(x + w * 0.68f, y + h * 0.46f);
            p.startNewSubPath(x + w * 0.32f, y + h * 0.60f);
            p.lineTo(x + w * 0.68f, y + h * 0.60f);
            p.startNewSubPath(x + w * 0.32f, y + h * 0.74f);
            p.lineTo(x + w * 0.52f, y + h * 0.74f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Photography:
            p.startNewSubPath(x + w * 0.14f, y + h * 0.32f);
            p.lineTo(x + w * 0.32f, y + h * 0.32f);
            p.lineTo(x + w * 0.40f, y + h * 0.20f);
            p.lineTo(x + w * 0.60f, y + h * 0.20f);
            p.lineTo(x + w * 0.68f, y + h * 0.32f);
            p.lineTo(x + w * 0.86f, y + h * 0.32f);
            p.lineTo(x + w * 0.86f, y + h * 0.82f);
            p.lineTo(x + w * 0.14f, y + h * 0.82f);
            p.closeSubPath();
            g.strokePath(p, traco);
            g.drawEllipse(x + w * 0.34f, y + h * 0.38f, w * 0.32f, h * 0.32f, traco.getStrokeThickness());
            g.fillEllipse(x + w * 0.44f, y + h * 0.48f, w * 0.12f, h * 0.12f);
            break;
        case IconeUi::Sound:
            g.fillEllipse(x + w * 0.20f, y + h * 0.60f, w * 0.22f, h * 0.22f);
            g.fillEllipse(x + w * 0.58f, y + h * 0.48f, w * 0.22f, h * 0.22f);
            p.startNewSubPath(x + w * 0.38f, y + h * 0.68f);
            p.lineTo(x + w * 0.38f, y + h * 0.20f);
            p.lineTo(x + w * 0.76f, y + h * 0.12f);
            p.lineTo(x + w * 0.76f, y + h * 0.56f);
            g.strokePath(p, juce::PathStrokeType(traco.getStrokeThickness() * 1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        case IconeUi::Videos:
            p.addRoundedRectangle(x + w * 0.14f, y + h * 0.22f, w * 0.72f, h * 0.62f, w * 0.08f);
            g.strokePath(p, traco);
            p.clear();
            p.startNewSubPath(x + w * 0.14f, y + h * 0.42f);
            p.lineTo(x + w * 0.86f, y + h * 0.42f);
            p.startNewSubPath(x + w * 0.30f, y + h * 0.22f);
            p.lineTo(x + w * 0.42f, y + h * 0.42f);
            p.startNewSubPath(x + w * 0.54f, y + h * 0.22f);
            p.lineTo(x + w * 0.66f, y + h * 0.42f);
            p.startNewSubPath(x + w * 0.44f, y + h * 0.52f);
            p.lineTo(x + w * 0.44f, y + h * 0.74f);
            p.lineTo(x + w * 0.62f, y + h * 0.63f);
            p.closeSubPath();
            g.strokePath(p, traco);
            break;
        case IconeUi::YouTube:
            p.addRoundedRectangle(x + w * 0.10f, y + h * 0.24f, w * 0.80f, h * 0.52f, w * 0.14f);
            g.strokePath(p, traco);
            p.clear();
            p.addTriangle(x + w * 0.43f, y + h * 0.39f, x + w * 0.43f, y + h * 0.61f, x + w * 0.63f, y + h * 0.50f);
            g.fillPath(p.createPathWithRoundedCorners(w * 0.03f));
            break;
        case IconeUi::Other:
            p.startNewSubPath(x + w * 0.50f, y + h * 0.14f);
            p.lineTo(x + w * 0.84f, y + h * 0.32f);
            p.lineTo(x + w * 0.84f, y + h * 0.68f);
            p.lineTo(x + w * 0.50f, y + h * 0.86f);
            p.lineTo(x + w * 0.16f, y + h * 0.68f);
            p.lineTo(x + w * 0.16f, y + h * 0.32f);
            p.closeSubPath();
            p.startNewSubPath(x + w * 0.50f, y + h * 0.14f);
            p.lineTo(x + w * 0.50f, y + h * 0.50f);
            p.lineTo(x + w * 0.84f, y + h * 0.32f);
            p.startNewSubPath(x + w * 0.50f, y + h * 0.50f);
            p.lineTo(x + w * 0.16f, y + h * 0.32f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Web:  // globo: contorno, meridiano e equador
            p.addEllipse(x + w * 0.12f, y + h * 0.12f, w * 0.76f, h * 0.76f);
            g.strokePath(p, traco);
            p.clear();
            p.addEllipse(x + w * 0.34f, y + h * 0.12f, w * 0.32f, h * 0.76f);
            p.startNewSubPath(x + w * 0.12f, y + h * 0.50f);
            p.lineTo(x + w * 0.88f, y + h * 0.50f);
            p.startNewSubPath(x + w * 0.19f, y + h * 0.30f);
            p.lineTo(x + w * 0.81f, y + h * 0.30f);
            p.startNewSubPath(x + w * 0.19f, y + h * 0.70f);
            p.lineTo(x + w * 0.81f, y + h * 0.70f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Multimedia:  // tela com triângulo de play e pé
            p.addRoundedRectangle(x + w * 0.10f, y + h * 0.16f, w * 0.80f, h * 0.54f, w * 0.06f);
            g.strokePath(p, traco);
            p.clear();
            p.addTriangle(x + w * 0.43f, y + h * 0.28f, x + w * 0.43f, y + h * 0.58f, x + w * 0.65f, y + h * 0.43f);
            g.fillPath(p.createPathWithRoundedCorners(w * 0.03f));
            p.clear();
            p.startNewSubPath(x + w * 0.50f, y + h * 0.70f);
            p.lineTo(x + w * 0.50f, y + h * 0.84f);
            p.startNewSubPath(x + w * 0.30f, y + h * 0.84f);
            p.lineTo(x + w * 0.70f, y + h * 0.84f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Disco:  // CD/DVD: aro, furo central e reflexo
            p.addEllipse(x + w * 0.10f, y + h * 0.10f, w * 0.80f, h * 0.80f);
            g.strokePath(p, traco);
            p.clear();
            p.addEllipse(x + w * 0.42f, y + h * 0.42f, w * 0.16f, h * 0.16f);
            g.strokePath(p, traco);
            p.clear();
            p.addCentredArc(x + w * 0.50f, y + h * 0.50f, w * 0.30f, h * 0.30f, 0.0f,
                            juce::MathConstants<float>::pi * 1.15f, juce::MathConstants<float>::pi * 1.55f, true);
            g.strokePath(p, traco);
            break;
        case IconeUi::CdRom:  // disco com a etiqueta ROM
            p.addEllipse(x + w * 0.06f, y + h * 0.06f, w * 0.72f, h * 0.72f);
            g.strokePath(p, traco);
            p.clear();
            p.addEllipse(x + w * 0.34f, y + h * 0.34f, w * 0.16f, h * 0.16f);
            g.strokePath(p, traco);
            p.clear();
            p.addRoundedRectangle(x + w * 0.46f, y + h * 0.62f, w * 0.48f, h * 0.28f, w * 0.05f);
            g.fillPath(p);
            g.setColour(juce::Colours::white);
            g.setFont(fonteUi(juce::jmax(7.0f, w * 0.17f), juce::Font::bold));
            g.drawText("ROM", juce::Rectangle<float>(x + w * 0.46f, y + h * 0.62f, w * 0.48f, h * 0.28f), juce::Justification::centred, false);
            g.setColour(cor);
            break;
        case IconeUi::Cassete:  // fita cassete: corpo, dois carretéis e janela
            p.addRoundedRectangle(x + w * 0.08f, y + h * 0.22f, w * 0.84f, h * 0.56f, w * 0.06f);
            g.strokePath(p, traco);
            p.clear();
            p.addEllipse(x + w * 0.22f, y + h * 0.36f, w * 0.20f, h * 0.20f);
            p.addEllipse(x + w * 0.58f, y + h * 0.36f, w * 0.20f, h * 0.20f);
            g.strokePath(p, traco);
            p.clear();
            p.startNewSubPath(x + w * 0.28f, y + h * 0.78f);
            p.lineTo(x + w * 0.36f, y + h * 0.64f);
            p.lineTo(x + w * 0.64f, y + h * 0.64f);
            p.lineTo(x + w * 0.72f, y + h * 0.78f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Link:  // dois elos de corrente
            p.addRoundedRectangle(x + w * 0.10f, y + h * 0.36f, w * 0.44f, h * 0.28f, h * 0.14f);
            p.addRoundedRectangle(x + w * 0.46f, y + h * 0.36f, w * 0.44f, h * 0.28f, h * 0.14f);
            g.strokePath(p, traco);
            break;
        case IconeUi::Logo:  // quadrado com 4 blocos
            for (int i = 0; i < 4; ++i) {
                const float bx = x + w * (0.18f + 0.34f * static_cast<float>(i % 2));
                const float by = y + h * (0.18f + 0.34f * static_cast<float>(i / 2));
                g.fillRoundedRectangle(bx, by, w * 0.28f, h * 0.28f, w * 0.06f);
            }
            break;
    }
}

// ── BotaoIcone ───────────────────────────────────────────────────────
BotaoIcone::BotaoIcone(IconeUi icone, const juce::String& tooltip) : juce::Button(tooltip), icone_(icone) {
    setTooltip(tooltip);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void BotaoIcone::paintButton(juce::Graphics& g, bool destacado, bool pressionado) {
    const auto& tk = tema();
    const bool ligado = getToggleState();
    auto area = getLocalBounds().toFloat().reduced(0.5f);
    const float raio = area.getHeight() * 0.5f;
    g.setColour(ligado ? tk.acento : (pressionado ? tk.painelAlt.darker(0.08f) : (destacado ? tk.painelAlt.brighter(0.04f) : tk.painelAlt)));
    g.fillRoundedRectangle(area, raio);
    if (!ligado) {
        g.setColour(tk.linhaSuave);
        g.drawRoundedRectangle(area, raio, 1.0f);
    }
    desenharIcone(g, icone_, area.reduced(area.getHeight() * 0.24f), ligado ? tk.textoSobreAcento : tk.textoPrimario);
}

// ── BotaoIconeTexto ───────────────────────────────────────────────────
BotaoIconeTexto::BotaoIconeTexto(IconeUi icone, const juce::String& texto) : juce::Button(texto), icone_(icone) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

int BotaoIconeTexto::larguraIdeal() const {
    const auto fonte = fonteUi(tema().tamanhoFonteCorpo, juce::Font::bold);
    const int texto = getButtonText().isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt(fonte, getButtonText()) + 8;
    return 40 + texto;
}

void BotaoIconeTexto::paintButton(juce::Graphics& g, bool destacado, bool pressionado) {
    const auto& tk = tema();
    auto area = getLocalBounds().toFloat().reduced(0.5f);
    const bool ativo = isEnabled();
    g.setColour(pressionado ? tk.painelAlt.darker(0.08f) : (destacado ? tk.painelAlt.brighter(0.04f) : tk.painelAlt));
    g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
    g.setColour(tk.linhaSuave);
    g.drawRoundedRectangle(area, area.getHeight() * 0.5f, 1.0f);
    const auto cor = ativo ? tk.textoPrimario : tk.textoTerciario;
    const float lado = juce::jmin(18.0f, area.getHeight() * 0.5f);
    const bool soIcone = getButtonText().isEmpty();
    const float xIcone = soIcone ? area.getCentreX() - lado * 0.5f : area.getX() + 14.0f;
    desenharIcone(g, icone_, { xIcone, area.getCentreY() - lado * 0.5f, lado, lado }, cor);
    if (!soIcone) {
        g.setColour(cor);
        g.setFont(fonteUi(tk.tamanhoFonteCorpo, juce::Font::bold));
        g.drawText(getButtonText(), area.withTrimmedLeft(14.0f + lado + 8.0f).toNearestInt(), juce::Justification::centredLeft, true);
    }
}

// ── BotaoPilula ───────────────────────────────────────────────────────
BotaoPilula::BotaoPilula(const juce::String& texto, Estilo estilo) : juce::Button(texto), estilo_(estilo) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void BotaoPilula::setEstilo(Estilo e) {
    estilo_ = e;
    repaint();
}

int BotaoPilula::alturaPadrao(bool destaqueTopo) const {
    return destaqueTopo ? tema().alturaBotaoPrimario : tema().alturaBotao;
}

int BotaoPilula::larguraIdeal() const {
    const auto fonte = fonteUi(tema().tamanhoFonteCorpo, juce::Font::bold);
    return juce::GlyphArrangement::getStringWidthInt(fonte, getButtonText()) + 40;
}

void BotaoPilula::paintButton(juce::Graphics& g, bool destacado, bool pressionado) {
    const auto& tk = tema();
    auto area = getLocalBounds().toFloat().reduced(0.5f);
    const float raio = (estilo_ == Estilo::Redondo) ? juce::jmin(area.getWidth(), area.getHeight()) * 0.5f
                                                    : area.getHeight() * 0.5f;

    juce::Colour fundo, texto, linha = tk.linhaSuave;
    switch (estilo_) {
        case Estilo::Primario:
            fundo = pressionado ? tk.acentoHover.darker(0.1f) : (destacado ? tk.acentoHover : tk.acento);
            texto = tk.textoSobreAcento;
            linha = fundo;
            break;
        case Estilo::Perigo:
            fundo = tk.perigoSuave;
            texto = tk.perigo;
            linha = tk.perigo.withAlpha(0.5f);
            break;
        case Estilo::Fantasma:
            fundo = destacado ? tk.painelAlt.withAlpha(0.6f) : juce::Colours::transparentBlack;
            texto = tk.textoSecundario;
            break;
        case Estilo::Redondo:
        case Estilo::Secundario:
        default:
            fundo = pressionado ? tk.painelAlt.darker(0.08f) : (destacado ? tk.painelAlt.brighter(0.04f) : tk.painelAlt);
            texto = tk.textoPrimario;
            break;
    }
    if (usarCorSecao_ && estilo_ == Estilo::Secundario && getToggleState()) {
        fundo = corSecao_;
        texto = tk.textoSobreAcento;
    }
    if (!isEnabled()) {
        fundo = fundo.withMultipliedAlpha(0.45f);
        texto = texto.withMultipliedAlpha(0.45f);
        linha = linha.withMultipliedAlpha(0.45f);
    }

    g.setColour(fundo);
    g.fillRoundedRectangle(area, raio);
    if (estilo_ != Estilo::Primario) {
        g.setColour(linha);
        g.drawRoundedRectangle(area, raio, 1.0f);
    }
    g.setColour(texto);
    g.setFont(fonteUi(tk.tamanhoFonteCorpo, juce::Font::bold));
    g.drawText(getButtonText(), getLocalBounds().reduced(8, 0), juce::Justification::centred, true);
}

// ── ChipFiltro ───────────────────────────────────────────────────────
ChipFiltro::ChipFiltro(const juce::String& rotulo, int contador)
    : juce::Button(rotulo), rotulo_(rotulo), contador_(contador) {
    setClickingTogglesState(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::String ChipFiltro::textoCompleto() const {
    return contador_ >= 0 ? rotulo_ + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(contador_) : rotulo_;
}

int ChipFiltro::larguraIdeal() const {
    const auto fonte = fonteUi(tema().tamanhoFontePequena + 1.0f, juce::Font::bold);
    return juce::roundToInt(static_cast<float>(juce::GlyphArrangement::getStringWidthInt(fonte, textoCompleto())) * 1.08f) + 36 + (usarMarca_ ? 14 : 0);
}

void ChipFiltro::paintButton(juce::Graphics& g, bool destacado, bool) {
    const auto& tk = tema();
    auto area = getLocalBounds().toFloat().reduced(0.5f);
    const float raio = area.getHeight() * 0.5f;
    const bool ativo = getToggleState();

    juce::Colour fundo = ativo ? tk.acento : (destacado ? tk.painelAlt.brighter(0.05f) : tk.painelAlt);
    juce::Colour texto = ativo ? tk.textoSobreAcento : tk.textoPrimario;
    g.setColour(fundo);
    g.fillRoundedRectangle(area, raio);
    if (!ativo) {
        g.setColour(tk.linhaSuave);
        g.drawRoundedRectangle(area, raio, 1.0f);
    }
    auto texto_area = getLocalBounds().reduced(10, 0);
    if (usarMarca_) {
        g.setColour(corMarca_);
        const float d = 8.0f;
        g.fillEllipse(static_cast<float>(texto_area.getX()), area.getCentreY() - d * 0.5f, d, d);
        texto_area.removeFromLeft(14);
    }
    g.setColour(texto);
    g.setFont(fonteUi(tk.tamanhoFontePequena + 1.0f, juce::Font::bold));
    g.drawText(textoCompleto(), texto_area, juce::Justification::centredLeft, true);
}

// ── ChaveSegmentada ───────────────────────────────────────────────────
class ChaveSegmentada::Segmento : public juce::Button {
public:
    explicit Segmento(const juce::String& t) : juce::Button(t) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paintButton(juce::Graphics& g, bool destacado, bool) override {
        const auto& tk = tema();
        auto area = getLocalBounds().toFloat().reduced(3.0f);
        const bool ativo = getToggleState();
        if (ativo) {
            g.setColour(tk.acento);
            g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
        } else if (destacado) {
            g.setColour(tk.painelAlt.brighter(0.05f));
            g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
        }
        g.setColour(ativo ? tk.textoSobreAcento : tk.textoSecundario);
        g.setFont(fonteUi(tk.tamanhoFontePequena, juce::Font::bold).withExtraKerningFactor(0.08f));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, true);
    }
};

ChaveSegmentada::ChaveSegmentada(const juce::String& textoA, const juce::String& textoB) {
    a_ = std::make_unique<Segmento>(textoA);
    b_ = std::make_unique<Segmento>(textoB);
    a_->setToggleState(true, juce::dontSendNotification);
    a_->onClick = [this] { selecionar(0, true); };
    b_->onClick = [this] { selecionar(1, true); };
    addAndMakeVisible(*a_);
    addAndMakeVisible(*b_);
}

ChaveSegmentada::~ChaveSegmentada() = default;

juce::Button& ChaveSegmentada::botaoA() { return *a_; }
juce::Button& ChaveSegmentada::botaoB() { return *b_; }

void ChaveSegmentada::definirIds(const juce::String& idA, const juce::String& idB) {
    a_->setComponentID(idA);
    b_->setComponentID(idB);
}

void ChaveSegmentada::selecionar(int indice, bool notificar) {
    indice_ = indice == 1 ? 1 : 0;
    a_->setToggleState(indice_ == 0, juce::dontSendNotification);
    b_->setToggleState(indice_ == 1, juce::dontSendNotification);
    repaint();
    if (notificar && aoMudar) aoMudar(indice_);
}

void ChaveSegmentada::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto area = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(tk.painelAlt);
    g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
    g.setColour(tk.linhaSuave);
    g.drawRoundedRectangle(area, area.getHeight() * 0.5f, 1.0f);
}

void ChaveSegmentada::resized() {
    auto area = getLocalBounds();
    a_->setBounds(area.removeFromLeft(area.getWidth() / 2));
    b_->setBounds(area);
}

// ── CartaoComponent ───────────────────────────────────────────────────
juce::Rectangle<int> CartaoComponent::areaConteudo(int margem) const {
    auto area = getLocalBounds().reduced(margem);
    if (titulo_.isNotEmpty()) area.removeFromTop(24);
    return area;
}

void CartaoComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    const float raio = raio_ > 0.0f ? raio_ : tk.raioCartao;
    desenharCartao(g, getLocalBounds().toFloat().reduced(2.0f), raio, tk.cartao, sombra_);
    if (usarMarca_) {
        g.setColour(marca_);
        g.fillRoundedRectangle(16.0f, 16.0f, 10.0f, 10.0f, 3.0f);
    }
    if (titulo_.isNotEmpty())
        desenharRotuloSecao(g, titulo_, getLocalBounds().reduced(usarMarca_ ? 34 : 16, 12).removeFromTop(20), tk.textoTerciario);
}

// ── BarraTituloTela ───────────────────────────────────────────────────
void BarraTituloTela::definirTitulo(const juce::String& titulo, const juce::String& subtitulo) {
    titulo_ = titulo;
    subtitulo_ = subtitulo;
    repaint();
}

void BarraTituloTela::definirAcoes(std::vector<juce::Component*> acoes) {
    for (auto* c : acoes_) if (c != nullptr) removeChildComponent(c);
    acoes_ = std::move(acoes);
    for (auto* c : acoes_) if (c != nullptr) addAndMakeVisible(c);
    resized();
}

void BarraTituloTela::definirExtrasSubtitulo(std::vector<juce::Component*> extras) {
    for (auto* c : extras_) if (c != nullptr) removeChildComponent(c);
    extras_ = std::move(extras);
    for (auto* c : extras_) if (c != nullptr) addAndMakeVisible(c);
    resized();
}

void BarraTituloTela::definirSetores(std::vector<Setor> setores) {
    for (auto& s : setores_) for (auto* c : s.comps) if (c != nullptr) removeChildComponent(c);
    setores_ = std::move(setores);
    for (auto& s : setores_) for (auto* c : s.comps) if (c != nullptr) addAndMakeVisible(c);
    resized();
}

void BarraTituloTela::paint(juce::Graphics& g) {
    const auto& tk = tema();
    if (!setores_.empty()) {
        g.setColour(tk.linhaSuave);
        for (int x : divisoresX_)
            g.drawLine(static_cast<float>(x), static_cast<float>(getHeight()) * 0.5f - 26.0f, static_cast<float>(x),
                       static_cast<float>(getHeight()) * 0.5f + 26.0f, 1.0f);
        if (mostrarRotulosSetores_)
            for (size_t i = 0; i < setores_.size() && i < rotulosSetores_.size(); ++i)
                desenharRotuloSecao(g, setores_[i].rotulo, rotulosSetores_[i], tk.textoTerciario, juce::Justification::centredLeft);
    }
    auto area = areaTexto_.isEmpty() ? getLocalBounds().reduced(24, 0) : areaTexto_;
    const int blocoAltura = 36 + 20;
    area = area.withSizeKeepingCentre(area.getWidth(), blocoAltura);
    g.setColour(tk.textoPrimario);
    g.setFont(fonteUi(tk.fonteTituloTela, juce::Font::bold));
    g.drawText(titulo_, area.removeFromTop(36), juce::Justification::centredLeft, true);
    if (extras_.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(fonteUi(tk.fonteSubtituloTela));
        g.drawText(subtitulo_, area, juce::Justification::centredLeft, true);
    }
}

void BarraTituloTela::resized() {
    auto area = getLocalBounds().reduced(24, 0);
    // Ações alinhadas à direita, na ordem recebida.
    int x = area.getRight();
    // Setores (da direita para a esquerda): [rótulo em cima] [controles], com divisor fino entre eles.
    divisoresX_.clear();
    rotulosSetores_.assign(setores_.size(), {});
    if (!setores_.empty()) {
        constexpr int kGap = 10, kGutter = 14, kRotuloAlt = 14, kRotuloGap = 4;
        int maxAlt = 0;
        for (auto& s : setores_) for (auto* c : s.comps) if (c != nullptr && c->isVisible()) maxAlt = std::max(maxAlt, c->getHeight() > 0 ? c->getHeight() : tema().alturaBotao);
        const int bloco = kRotuloAlt + kRotuloGap + maxAlt;
        const int topo = (getHeight() - bloco) / 2;
        for (int i = static_cast<int>(setores_.size()) - 1; i >= 0; --i) {
            auto& s = setores_[static_cast<size_t>(i)];
            int larg = 0;
            for (auto* c : s.comps) if (c != nullptr && c->isVisible()) larg += (c->getWidth() > 0 ? c->getWidth() : 120) + kGap;
            if (larg > 0) larg -= kGap;
            int cx = x - larg;
            const int xIni = cx;
            for (auto* c : s.comps) {
                if (c == nullptr || !c->isVisible()) continue;
                const int w = c->getWidth() > 0 ? c->getWidth() : 120;
                const int h = c->getHeight() > 0 ? c->getHeight() : tema().alturaBotao;
                c->setBounds(cx, topo + kRotuloAlt + kRotuloGap + (maxAlt - h) / 2, w, h);
                cx += w + kGap;
            }
            rotulosSetores_[static_cast<size_t>(i)] = { xIni, topo, std::max(larg, 80), kRotuloAlt };
            x = xIni;
            if (i > 0) {
                x -= kGutter;
                divisoresX_.push_back(x);
                x -= kGutter;
            }
        }
        x -= kGutter * 2;  // respiro entre o título/BACKUP e o primeiro setor
        mostrarRotulosSetores_ = getWidth() >= 1180;
    }
    for (auto it = acoes_.rbegin(); it != acoes_.rend(); ++it) {
        auto* c = *it;
        if (c == nullptr || !c->isVisible()) continue;
        int larg = c->getWidth() > 0 ? c->getWidth() : 120;
        int alt = c->getHeight() > 0 ? c->getHeight() : tema().alturaBotao;
        c->setBounds(x - larg, (getHeight() - alt) / 2, larg, alt);
        x -= larg + 10;
    }
    areaTexto_ = area.withRight(x);

    // Extras na linha do subtítulo
    const int blocoAltura = 36 + 20;
    const auto bloco = areaTexto_.withSizeKeepingCentre(areaTexto_.getWidth(), blocoAltura);
    int ex = bloco.getX();
    for (auto* c : extras_) {
        if (c == nullptr || !c->isVisible()) continue;
        const int larg = c->getWidth() > 0 ? c->getWidth() : 120;
        const int alt = c->getHeight() > 0 ? c->getHeight() : 30;
        c->setBounds(ex, bloco.getY() + 36 + (20 - alt) / 2, larg, alt);
        ex += larg + 10;
    }
}

// ── PilulaInfo ───────────────────────────────────────────────────────
int PilulaInfo::larguraIdeal() const {
    return juce::GlyphArrangement::getStringWidthInt(fonteUi(tema().tamanhoFonteCorpo, juce::Font::bold), texto_) + 36;
}

void PilulaInfo::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto area = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(cor_.isTransparent() ? tk.painelAlt : cor_.withAlpha(0.18f));
    g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
    g.setColour(cor_.isTransparent() ? tk.linhaSuave : cor_);
    g.drawRoundedRectangle(area, area.getHeight() * 0.5f, 1.0f);
    g.setColour(tk.textoPrimario);
    g.setFont(fonteUi(tk.tamanhoFonteCorpo, juce::Font::bold));
    g.drawText(texto_, getLocalBounds().reduced(14, 0), juce::Justification::centred, true);
}

void PilulaInfo::mouseUp(const juce::MouseEvent& e) {
    if (aoClicar && getLocalBounds().contains(e.getPosition())) aoClicar();
}

// ── Utilitários de árvore de componentes ──────────────────────────────────
juce::Component* encontrarRaizApp(juce::Component* inicio) {
    juce::Component* c = inicio;
    juce::Component* ultimo = inicio;
    while (c != nullptr) {
        if (c->getComponentID() == "app.root") return c;
        ultimo = c;
        c = c->getParentComponent();
    }
    return ultimo;
}

juce::Component* encontrarPorId(juce::Component* raiz, const juce::String& id) {
    if (raiz == nullptr) return nullptr;
    if (raiz->getComponentID() == id) return raiz;
    for (auto* filho : raiz->getChildren())
        if (auto* r = encontrarPorId(filho, id)) return r;
    return nullptr;
}

void coletarIds(juce::Component* raiz, std::vector<juce::Component*>& out) {
    if (raiz == nullptr) return;
    if (raiz->getComponentID().isNotEmpty()) out.push_back(raiz);
    for (auto* filho : raiz->getChildren()) coletarIds(filho, out);
}

// ── AbasPasta ────────────────────────────────────────────────────────────────────────────────

class AbasPasta::BotaoAba : public juce::Button {
public:
    BotaoAba(const juce::String& rotulo, juce::Colour cor) : juce::Button(rotulo), rotulo_(rotulo), cor_(cor) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setClickingTogglesState(false);
    }
    bool ativa = false;
    int larguraIdeal() const {
        return juce::roundToInt(juce::GlyphArrangement::getStringWidth(fonteUi(tema().fonteCorpoGrande - 1.0f, juce::Font::bold), rotulo_.toUpperCase())) + 56;
    }
    juce::Colour cor() const { return cor_; }

    static juce::Colour corFaixa() {
        const auto& tk = tema();
        return tk.fundo.interpolatedWith(juce::Colours::black, 0.10f);
    }

    void paintButton(juce::Graphics& g, bool sobre, bool) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat();
        const float raio = 13.0f;
        juce::Path p;
        // aba = retângulo com os cantos de cima arredondados e base reta (a pasta)
        p.addRoundedRectangle(r.getX(), r.getY() + 4.0f, r.getWidth(), r.getHeight() - 4.0f + raio, raio, raio, true, true, false, false);
        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(getLocalBounds());
            if (ativa) {
                g.setColour(tk.sombra.withAlpha(0.35f));
                g.fillPath(p, juce::AffineTransform::translation(0.0f, -1.5f));
                g.setColour(tk.fundo);
                g.fillPath(p);
                g.setColour(tk.linhaSuave);
                g.strokePath(p, juce::PathStrokeType(1.0f));
                g.setColour(cor_);
                g.fillRoundedRectangle(r.getX() + 18.0f, r.getY() + 4.0f, r.getWidth() - 36.0f, 3.5f, 1.75f);
            } else {
                g.setColour(corFaixa().interpolatedWith(tk.painelAlt, sobre ? 0.75f : 0.45f));
                g.fillPath(p);
                g.setColour(tk.linhaSuave);
                g.strokePath(p, juce::PathStrokeType(1.0f));
            }
        }
        g.setColour(ativa ? tk.textoPrimario : tk.textoSecundario);
        g.setFont(fonteUi(tk.fonteCorpoGrande - 1.0f, juce::Font::bold).withExtraKerningFactor(0.06f));
        g.drawText(rotulo_.toUpperCase(), getLocalBounds().withTrimmedTop(6), juce::Justification::centred, true);
    }

private:
    juce::String rotulo_;
    juce::Colour cor_;
};

AbasPasta::AbasPasta() { setComponentID("tabs.bar"); }
AbasPasta::~AbasPasta() = default;

void AbasPasta::definirAbas(std::vector<Aba> abas) {
    botoes_.clear();
    removeAllChildren();
    int i = 0;
    for (const auto& a : abas) {
        auto b = std::make_unique<BotaoAba>(a.rotulo, a.cor);
        b->setComponentID(a.id);
        const int indice = i++;
        b->onClick = [this, indice] { selecionar(indice, true); };
        addAndMakeVisible(*b);
        botoes_.push_back(std::move(b));
    }
    selecionada_ = juce::jlimit(0, juce::jmax(0, static_cast<int>(botoes_.size()) - 1), selecionada_);
    for (size_t k = 0; k < botoes_.size(); ++k) botoes_[k]->ativa = static_cast<int>(k) == selecionada_;
    resized();
    repaint();
}

void AbasPasta::selecionar(int indice, bool notificar) {
    if (indice < 0 || indice >= static_cast<int>(botoes_.size())) return;
    selecionada_ = indice;
    for (size_t k = 0; k < botoes_.size(); ++k) { botoes_[k]->ativa = static_cast<int>(k) == indice; botoes_[k]->repaint(); }
    botoes_[static_cast<size_t>(indice)]->toFront(false);
    repaint();
    if (notificar && aoMudar) aoMudar(indice);
}

juce::Button* AbasPasta::botao(int indice) const {
    return indice >= 0 && indice < static_cast<int>(botoes_.size()) ? botoes_[static_cast<size_t>(indice)].get() : nullptr;
}

void AbasPasta::resized() {
    int x = 24;
    for (auto& b : botoes_) {
        const int w = juce::jmax(150, b->larguraIdeal());
        b->setBounds(x, 6, w, getHeight() - 6);
        x += w - 2;  // as abas se sobrepõem um pouco, como pastas num arquivo
    }
    // a ativa vai por cima das outras
    for (auto& b : botoes_) if (b->ativa) b->toFront(false);
}

void AbasPasta::paint(juce::Graphics& g) {
    g.fillAll(BotaoAba::corFaixa());
}

void AbasPasta::paintOverChildren(juce::Graphics& g) {
    // linha de base da faixa; a aba ativa a "abre" para fundir com a tela
    const auto& tk = tema();
    g.setColour(tk.linhaSuave);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    if (auto* b = botao(selecionada_)) {
        g.setColour(tk.fundo);
        g.fillRect(b->getX() + 1, getHeight() - 1, b->getWidth() - 2, 1);
    }
}

} // namespace matriz::ui
