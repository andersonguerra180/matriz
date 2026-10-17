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
        destaques_.clear();
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
        destaques_.push_back(juce::Colours::transparentBlack);
        corpo_.removeFromTop(kEntreCards);
    }

    // Card que pede ação do usuário (estilo alertas do INTAKE): fundo e borda grossa na cor dada, título na mesma cor.
    // Chamar logo depois de finalizarCard(); quem para de chamar (nada pendente) volta ao visual normal.
    void destacarUltimoCard(juce::Colour cor) {
        if (!destaques_.empty()) destaques_.back() = cor;
    }

    void paint(juce::Graphics& g) const {
        const auto& tk = tema();
        g.setColour(tk.fundo);
        g.fillRect(coluna_);
        g.setColour(tk.borda);
        g.fillRect(coluna_.getRight() - 1, coluna_.getY(), 1, coluna_.getHeight());

        for (size_t i = 0; i < cards_.size(); ++i) {
            const auto& c = cards_[i];
            const bool destaque = i < destaques_.size() && destaques_[i].getAlpha() > 0;
            if (destaque) {
                g.setColour(tk.painelAlt.withAlpha(0.25f));
                g.fillRoundedRectangle(c.toFloat(), tk.raioPequeno);
                g.setColour(destaques_[i].withAlpha(0.20f));
                g.fillRoundedRectangle(c.toFloat(), tk.raioPequeno);
                g.setColour(destaques_[i]);
                g.drawRoundedRectangle(c.toFloat().reduced(1.0f), tk.raioPequeno, 2.5f);
            } else {
                g.setColour(tk.painelAlt.withAlpha(0.25f));
                g.fillRoundedRectangle(c.toFloat(), tk.raioPequeno);
                g.setColour(tk.borda.withAlpha(0.7f));
                g.drawRoundedRectangle(c.toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);
            }
        }
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        for (size_t i = 0; i < titulos_.size(); ++i) {
            const bool destaque = i < destaques_.size() && destaques_[i].getAlpha() > 0;
            g.setColour(destaque ? destaques_[i] : tk.textoPrimario);
            g.drawText(titulos_[i].first, titulos_[i].second, juce::Justification::centredLeft);
        }
    }

private:
    juce::Rectangle<int> coluna_;
    juce::Rectangle<int> corpo_;
    int cardTopo_ = 0;
    std::vector<juce::Rectangle<int>> cards_;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> titulos_;
    std::vector<juce::Colour> destaques_;  // um por card; transparente = visual normal
};

} // namespace matriz::ui
