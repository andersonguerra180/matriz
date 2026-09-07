#pragma once

#include <JuceHeader.h>
#include "Tokens.h"

namespace matriz::ui {

class ViewModeIconButton : public juce::TextButton {
public:
    enum class IconType {
        Grid,
        List
    };

    explicit ViewModeIconButton(IconType type) : type_(type) {}

    void setAtivo(bool ativo) {
        if (ativo_ != ativo) {
            ativo_ = ativo;
            repaint();
        }
    }

    bool isAtivo() const { return ativo_; }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        const auto& tk = tema();
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        float corner = tk.raioPequeno;

        // Background fill
        if (ativo_) {
            g.setColour(tk.acento);
            g.fillRoundedRectangle(bounds, corner);
        } else if (shouldDrawButtonAsDown) {
            g.setColour(tk.painelAlt.withAlpha(0.7f));
            g.fillRoundedRectangle(bounds, corner);
        } else if (shouldDrawButtonAsHighlighted) {
            g.setColour(tk.painelAlt.withAlpha(0.4f));
            g.fillRoundedRectangle(bounds, corner);
        } else {
            g.setColour(tk.painelAlt.withAlpha(0.2f));
            g.fillRoundedRectangle(bounds, corner);
        }

        // Outline border
        g.setColour(ativo_ ? tk.acento.darker(0.1f) : tk.borda.withAlpha(0.6f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);

        // Icon color
        juce::Colour iconCol = ativo_ ? tk.textoSobreAcento 
                             : (shouldDrawButtonAsHighlighted ? tk.textoPrimario : tk.textoSecundario);
        g.setColour(iconCol);

        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();

        if (type_ == IconType::Grid) {
            // macOS Finder style 2x2 grid icon (4 rounded squares)
            float sqSize = 5.0f;
            float gap = 2.0f;
            float sqCorner = 1.0f;
            
            float x1 = cx - sqSize - (gap * 0.5f);
            float x2 = cx + (gap * 0.5f);
            float y1 = cy - sqSize - (gap * 0.5f);
            float y2 = cy + (gap * 0.5f);

            g.fillRoundedRectangle(x1, y1, sqSize, sqSize, sqCorner);
            g.fillRoundedRectangle(x2, y1, sqSize, sqSize, sqCorner);
            g.fillRoundedRectangle(x1, y2, sqSize, sqSize, sqCorner);
            g.fillRoundedRectangle(x2, y2, sqSize, sqSize, sqCorner);
        } else {
            // macOS Finder style list icon (3 horizontal rows)
            float lineW = 12.0f;
            float lineH = 1.8f;
            float r = 0.9f;
            float x0 = cx - (lineW * 0.5f);
            
            g.fillRoundedRectangle(x0, cy - 4.5f, lineW, lineH, r);
            g.fillRoundedRectangle(x0, cy - 0.9f, lineW, lineH, r);
            g.fillRoundedRectangle(x0, cy + 2.7f, lineW, lineH, r);
        }
    }

private:
    IconType type_;
    bool ativo_ = false;
};

} // namespace matriz::ui
