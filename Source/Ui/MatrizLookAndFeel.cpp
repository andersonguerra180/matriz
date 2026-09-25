#include "MatrizLookAndFeel.h"
#include "Tokens.h"

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

} // namespace matriz::ui
