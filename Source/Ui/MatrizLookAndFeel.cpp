#include "MatrizLookAndFeel.h"
#include "Tokens.h"
#include <AssetsBinaryData.h>
#include "Tablet.h"
#include "Moldura.h"

namespace matriz::ui {

// Item 6 (lista nova de hoje): referência que o usuário deu foi um fader —
// haste fina e sempre visível, com um "cap" redondo bem mais largo que ela,
// fácil de localizar e agarrar sem precisar mirar. A área de drag continua
// sendo a mesma proporcional que o JUCE já calcula internamente
// (thumbStartPosition/thumbSize) — só a aparência muda.
void MatrizLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                       bool isMouseOver, bool isMouseDown) {
    if (thumbSize <= 0) return;

    const auto& tk = tema();
    const int comprimentoTrilha = isScrollbarVertical ? height : width;
    const int espessuraTotal = isScrollbarVertical ? width : height;

    // Haste fina, sempre visível, centralizada na espessura total —
    // referência visual (o "trilho" do fader).
    g.setColour(tk.borda.withAlpha(0.6f));
    if (isScrollbarVertical) {
        float cx = x + espessuraTotal / 2.0f;
        g.fillRoundedRectangle(cx - 1.5f, static_cast<float>(y), 3.0f, static_cast<float>(height), 1.5f);
    } else {
        float cy = y + espessuraTotal / 2.0f;
        g.fillRoundedRectangle(static_cast<float>(x), cy - 1.5f, static_cast<float>(width), 3.0f, 1.5f);
    }

    // Knob redondo — o "cap" do fader, com diâmetro igual à espessura
    // reservada pro scrollbar (bem mais largo que a haste fina de 3px
    // acima), centrado na posição proporcional do thumb.
    const int diametro = juce::jmax(14, espessuraTotal - 2);
    const int centro = thumbStartPosition + thumbSize / 2;

    auto corBase = findColour(juce::ScrollBar::thumbColourId);
    auto cor = isMouseDown ? corBase.brighter(0.3f) : (isMouseOver ? corBase.brighter(0.15f) : corBase);

    juce::Rectangle<float> knob;
    if (isScrollbarVertical) {
        float cx = x + espessuraTotal / 2.0f;
        float minCy = y + diametro / 2.0f;
        float maxCy = y + height - diametro / 2.0f;
        float cy = juce::jlimit(juce::jmin(minCy, maxCy), juce::jmax(minCy, maxCy),
                                 static_cast<float>(y + centro));
        knob = { cx - diametro / 2.0f, cy - diametro / 2.0f, static_cast<float>(diametro), static_cast<float>(diametro) };
    } else {
        float cy = y + espessuraTotal / 2.0f;
        float minCx = x + diametro / 2.0f;
        float maxCx = x + width - diametro / 2.0f;
        float cx = juce::jlimit(juce::jmin(minCx, maxCx), juce::jmax(minCx, maxCx),
                                 static_cast<float>(x + centro));
        knob = { cx - diametro / 2.0f, cy - diametro / 2.0f, static_cast<float>(diametro), static_cast<float>(diametro) };
    }

    g.setColour(cor);
    g.fillEllipse(knob);
    g.setColour(cor.darker(0.35f));
    g.drawEllipse(knob.reduced(0.5f), 1.0f);
}

// Botão no estilo Mini (tema claro): principal = pílula grafite; secundário = pílula #E4DED4 com borda; desativado =
// transparente com borda; vermelho só para quem apaga. Botões coloridos (verde/laranja/azul/roxo...) viram pílula
// neutra e a cor sobra apenas num quadradinho à esquerda do texto.
static bool botaoColorido(const juce::Colour& c) {
    return c.getSaturation() > 0.28f && c.getBrightness() > 0.25f && c.getAlpha() > 100;
}
static bool botaoVermelho(const juce::Colour& c) {
    const float h = c.getHue();
    return c.getSaturation() > 0.5f && (h < 0.04f || h > 0.96f);
}

void MatrizLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                              bool destacado, bool pressionado) {
    const auto& tk = tema();
    const bool claro = tk.fundo.getBrightness() > 0.5f;
    auto area = b.getLocalBounds().toFloat().reduced(0.5f);
    // Alvos altos viram pílula; botões miúdos (toolbars legadas) ficam com raio de campo.
    const bool redondo = b.getWidth() == b.getHeight();  // "?" e afins: círculo
    const float raio = (b.getHeight() >= 30 || redondo) ? area.getHeight() * 0.5f : juce::jmin(tk.raioMedio, area.getHeight() * 0.5f);
    const bool ativo = b.isEnabled();

    juce::Colour fill = bg;
    juce::Colour ponto;
    bool comPonto = false;
    if (claro && ativo && botaoColorido(bg) && !botaoVermelho(bg)) {
        // colorido -> neutro com ponto; o "ligado" (toggle) vira grafite
        ponto = bg;
        comPonto = dynamic_cast<juce::TextButton*>(&b) != nullptr && !b.getToggleState();
        fill = b.getToggleState() ? tk.acento : tk.botaoSecundario;
    }
    if (claro && !ativo) {
        g.setColour(tk.linhaSuave);
        g.drawRoundedRectangle(area, raio, 1.0f);
        return;
    }
    auto cor = fill.withMultipliedAlpha(ativo ? 1.0f : 0.45f);
    if (pressionado) cor = cor.darker(0.12f);
    else if (destacado) cor = cor.brighter(0.06f);
    g.setColour(cor);
    g.fillRoundedRectangle(area, raio);
    g.setColour(tk.linhaSuave.withMultipliedAlpha(ativo ? 1.0f : 0.45f));
    g.drawRoundedRectangle(area, raio, 1.0f);
    if (comPonto && area.getHeight() >= 22.0f) {
        const float d = 8.0f;
        g.setColour(ponto);
        g.fillRoundedRectangle(area.getX() + 12.0f, area.getCentreY() - d * 0.5f, d, d, 2.0f);
    }
}

void MatrizLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    const auto& tk = tema();
    const bool claro = tk.fundo.getBrightness() > 0.5f;
    g.setFont(getTextButtonFont(b, b.getHeight()));
    auto cor = b.findColour(b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (!b.isEnabled()) cor = claro ? tk.textoDesativado : cor.withMultipliedAlpha(0.5f);
    else if (claro) {
        // texto claro sobre fundo que (após a neutralização) ficou claro precisa virar tinta
        const auto bg = b.findColour(b.getToggleState() ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
        if (botaoColorido(bg) && !botaoVermelho(bg)) cor = b.getToggleState() ? tk.textoSobreAcento : tk.textoPrimario;
    }
    g.setColour(cor);
    const int recuo = juce::jmin(b.getHeight() / 2, 14);
    g.drawFittedText(b.getButtonText(), recuo, 0, b.getWidth() - 2 * recuo, b.getHeight(), juce::Justification::centred, 2);
}

juce::Font MatrizLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    const auto& tk = tema();
    const float tam = buttonHeight >= 36 ? tk.tamanhoFonteCorpo + 1.0f : tk.tamanhoFontePequena + 0.5f;
    return juce::Font(juce::FontOptions(tam * escalaFonte(), juce::Font::bold));
}

void MatrizLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& te) {
    const auto& tk = tema();
    g.setColour(te.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                           te.isMultiLine() ? tk.raioMedio : tk.raioCampo);
}

void MatrizLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& te) {
    const auto& tk = tema();
    if (!te.isEnabled()) return;
    const bool foco = te.hasKeyboardFocus(true) && !te.isReadOnly();
    g.setColour(te.findColour(foco ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f,
                           te.isMultiLine() ? tk.raioMedio : tk.raioCampo, foco ? 1.6f : 1.0f);
}

void MatrizLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                      juce::ComboBox& box) {
    const auto& tk = tema();
    auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(area, tk.raioCampo);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(area, tk.raioCampo, 1.0f);
    juce::Path seta;
    const float cx = static_cast<float>(width) - 18.0f, cy = static_cast<float>(height) * 0.5f;
    seta.startNewSubPath(cx - 4.0f, cy - 2.0f);
    seta.lineTo(cx, cy + 2.5f);
    seta.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.3f));
    g.strokePath(seta, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MatrizLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                          float, float, juce::Slider::SliderStyle style, juce::Slider& s) {
    if (style != juce::Slider::LinearHorizontal) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0, 0, style, s);
        return;
    }
    const auto& tk = tema();
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float espessura = 6.0f;
    juce::Rectangle<float> trilha(static_cast<float>(x), cy - espessura * 0.5f, static_cast<float>(width), espessura);
    g.setColour(tk.painelAlt);
    g.fillRoundedRectangle(trilha, espessura * 0.5f);
    g.setColour(tk.linhaSuave);
    g.drawRoundedRectangle(trilha, espessura * 0.5f, 1.0f);
    g.setColour(s.findColour(juce::Slider::trackColourId));
    g.fillRoundedRectangle(trilha.withRight(sliderPos), espessura * 0.5f);
    const float d = 18.0f;
    g.setColour(tk.cartao);
    g.fillEllipse(sliderPos - d * 0.5f, cy - d * 0.5f, d, d);
    g.setColour(s.findColour(juce::Slider::thumbColourId));
    g.drawEllipse(sliderPos - d * 0.5f + 0.75f, cy - d * 0.5f + 0.75f, d - 1.5f, d - 1.5f, 2.0f);
}

// Fontes embutidas (OFL), iguais em macOS, Windows e Linux:
//  - IBM Plex Sans: padrão de TODA a interface (Regular / Medium / SemiBold);
//  - Jost: só títulos de tela e de janela (nome "Jost");
//  - IBM Plex Mono: caminhos, hashes, timecodes (nome "IBM Plex Mono" ou a fonte monoespaçada padrão).
static juce::Typeface::Ptr typefaceEmbutida(const void* dados, size_t tamanho) {
    return juce::Typeface::createSystemTypefaceFor(dados, tamanho);
}

juce::Typeface::Ptr MatrizLookAndFeel::getTypefaceForFont(const juce::Font& font) {
    const auto nome = font.getTypefaceName();
    const auto estilo = font.getTypefaceStyle();
    const bool negrito = font.isBold() || estilo.containsIgnoreCase("Bold") || estilo.containsIgnoreCase("Semi");
    const bool medio = estilo.containsIgnoreCase("Medium");

    if (nome == "Jost") {
        static juce::Typeface::Ptr regular = typefaceEmbutida(AssetsBinaryData::JostRegular_ttf, AssetsBinaryData::JostRegular_ttfSize);
        static juce::Typeface::Ptr bold = typefaceEmbutida(AssetsBinaryData::JostBold_ttf, AssetsBinaryData::JostBold_ttfSize);
        if (auto tf = (negrito ? bold : regular)) return tf;
    } else if (nome == "IBM Plex Mono" || nome == juce::Font::getDefaultMonospacedFontName()) {
        static juce::Typeface::Ptr mono = typefaceEmbutida(AssetsBinaryData::IBMPlexMonoRegular_ttf, AssetsBinaryData::IBMPlexMonoRegular_ttfSize);
        if (mono != nullptr) return mono;
    } else if (nome == juce::Font::getDefaultSansSerifFontName() || nome == "<Sans-Serif>" || nome == "IBM Plex Sans" ||
               nome == "Inter" || nome.isEmpty()) {
        static juce::Typeface::Ptr regular = typefaceEmbutida(AssetsBinaryData::IBMPlexSansRegular_ttf, AssetsBinaryData::IBMPlexSansRegular_ttfSize);
        static juce::Typeface::Ptr medium = typefaceEmbutida(AssetsBinaryData::IBMPlexSansMedium_ttf, AssetsBinaryData::IBMPlexSansMedium_ttfSize);
        static juce::Typeface::Ptr semi = typefaceEmbutida(AssetsBinaryData::IBMPlexSansSemiBold_ttf, AssetsBinaryData::IBMPlexSansSemiBold_ttfSize);
        auto tf = negrito ? semi : (medio ? medium : regular);
        if (tf != nullptr) return tf;
    }
    return LookAndFeel_V4::getTypefaceForFont(font);  // fallback: fonte do sistema
}

juce::Font MatrizLookAndFeel::getAlertWindowTitleFont() { return fonteUi(tema().tamanhoFonteTitulo, juce::Font::bold); }
juce::Font MatrizLookAndFeel::getAlertWindowMessageFont() { return fonteUi(tema().tamanhoFonteCorpo); }
juce::Font MatrizLookAndFeel::getAlertWindowFont() { return fonteUi(tema().tamanhoFonteCorpo, juce::Font::bold); }

void MatrizLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>&, juce::TextLayout& layout) {
    // Caixa de aviso do sistema no estilo da moldura (Moldura.h): papel creme, sem faixa colorida, texto em tinta.
    const auto p = moldura::paleta();
    g.fillAll(p.janela);
    g.setColour(p.linha);
    g.drawRect(alert.getLocalBounds(), 1);
    g.setColour(p.tinta);
    layout.draw(g, juce::Rectangle<float>(28.0f, 24.0f, static_cast<float>(alert.getWidth()) - 56.0f, static_cast<float>(alert.getHeight()) - 48.0f));
}

} // namespace matriz::ui
