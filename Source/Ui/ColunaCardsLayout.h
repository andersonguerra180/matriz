#pragma once

#include <JuceHeader.h>

#include <utility>
#include <vector>

#include "Tokens.h"

namespace matriz::ui {

// Coluna esquerda em cards, no mesmo padrão do INTAKE (IntakeWorkspaceComponent::
// paint/resized): coluna de 230px com fundo tk.fundo e divisor de 1px, cards com
// painelAlt a 25%, borda a 70% e raioPequeno, título de seção em 10pt negrito.
// Só guarda a geometria: quem usa posiciona os controles em corpo().
class ColunaCardsLayout {
public:
    static constexpr int kLargura = 230;
    static constexpr int kCardPad = 6;
    static constexpr int kHeaderH = 18;
    static constexpr int kEntreCards = 8;

    // `areaColuna`: os 230px inteiros da esquerda (altura total do componente).
    void comecar(juce::Rectangle<int> areaColuna) {
        coluna_ = areaColuna;
        corpo_ = areaColuna.reduced(10, 8);
        cards_.clear();
        titulos_.clear();
    }

    juce::Rectangle<int>& corpo() { return corpo_; }
    juce::Rectangle<int> ultimoCard() const { return cards_.empty() ? juce::Rectangle<int>() : cards_.back(); }

    void iniciarCard(const juce::String& titulo) {
        corpo_.removeFromTop(kCardPad);
        cardTopo_ = corpo_.getY();
        titulos_.push_back({titulo, corpo_.removeFromTop(kHeaderH)});
        corpo_.removeFromTop(4);
    }

    void finalizarCard() {
        corpo_.removeFromTop(kCardPad);
        cards_.push_back({corpo_.getX() - 4, cardTopo_ - kCardPad, corpo_.getWidth() + 8, corpo_.getY() - cardTopo_ + kCardPad});
        corpo_.removeFromTop(kEntreCards);
    }

    void paint(juce::Graphics& g) const {
        const auto& tk = tema();
        g.setColour(tk.fundo);
        g.fillRect(coluna_);
        g.setColour(tk.borda);
        g.fillRect(coluna_.getRight() - 1, coluna_.getY(), 1, coluna_.getHeight());

        for (const auto& c : cards_) {
            g.setColour(tk.painelAlt.withAlpha(0.25f));
            g.fillRoundedRectangle(c.toFloat(), tk.raioPequeno);
            g.setColour(tk.borda.withAlpha(0.7f));
            g.drawRoundedRectangle(c.toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);
        }
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.setColour(tk.textoPrimario);
        for (const auto& [titulo, b] : titulos_) g.drawText(titulo, b, juce::Justification::centredLeft);
    }

private:
    juce::Rectangle<int> coluna_;
    juce::Rectangle<int> corpo_;
    int cardTopo_ = 0;
    std::vector<juce::Rectangle<int>> cards_;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> titulos_;
};

} // namespace matriz::ui
