#pragma once

#include <JuceHeader.h>

#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

// Usada no INTAKE e no mosaico do `_no folder` (os dois têm R + REJECT MARKED).
// Legenda de atalhos do card ACTIONS, no estilo da de METADATA: "Shortcuts  [R] Reject (?)".
class LegendaAtalhosIntake : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit LegendaAtalhosIntake(const juce::String& dica = i18n::t("intake.legenda_r_dica")) { setTooltip(dica); }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat();
        const float cy = r.getCentreY();
        juce::Font fontInst(juce::FontOptions(tk.tamanhoFonteCorpo - 1.0f, juce::Font::bold));
        juce::Font fontLabel(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold));
        float x = 2.0f;

        const auto instrucao = i18n::t("intake.legenda_atalhos");
        g.setFont(fontInst);
        g.setColour(tk.textoSecundario);
        const float instW = juce::GlyphArrangement::getStringWidth(fontInst, instrucao);
        g.drawText(instrucao, juce::roundToInt(x), 0, juce::roundToInt(std::ceil(instW)), getHeight(), juce::Justification::centredLeft, false);
        x += instW + 10.0f;

        juce::Rectangle<float> badge(x, cy - 10.0f, 20.0f, 20.0f);
        g.setColour(tk.perigo);
        g.fillRoundedRectangle(badge, 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        g.drawText("R", badge, juce::Justification::centred, false);
        x += 26.0f;

        const auto rotulo = i18n::t("intake.legenda_r");
        g.setFont(fontLabel);
        g.setColour(tk.textoPrimario);
        const float labelW = juce::GlyphArrangement::getStringWidth(fontLabel, rotulo);
        g.drawText(rotulo, juce::roundToInt(x), 0, juce::roundToInt(std::ceil(labelW)), getHeight(), juce::Justification::centredLeft, false);
        x += labelW + 10.0f;

        juce::Rectangle<float> ajuda(x, cy - 8.0f, 16.0f, 16.0f);
        g.setColour(tk.textoSecundario.withAlpha(0.6f));
        g.drawEllipse(ajuda.reduced(1.0f), 1.0f);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        g.drawText("?", ajuda, juce::Justification::centred, false);
    }
};

} // namespace matriz::ui
