#pragma once

#include <JuceHeader.h>

#include <functional>

#include "Tokens.h"

namespace matriz::ui {

// Faixa fina no topo da janela: texto de aviso + um botão opcional. Usada pelo
// MAIN EDIT MODE ("EDITANDO O MAIN") e pelo clone somente leitura.
class FaixaAvisoComponent : public juce::Component {
public:
    static constexpr int kAltura = 28;

    FaixaAvisoComponent() {
        botao_.onClick = [this] { if (aoBotao_) aoBotao_(); };
        addChildComponent(botao_);
    }

    void definir(const juce::String& texto, juce::Colour cor, const juce::String& textoBotao, std::function<void()> aoBotao) {
        texto_ = texto;
        cor_ = cor;
        aoBotao_ = std::move(aoBotao);
        botao_.setButtonText(textoBotao);
        botao_.setVisible(textoBotao.isNotEmpty());
        botao_.setColour(juce::TextButton::buttonColourId, cor.darker(0.35f));
        botao_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(cor_);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        auto r = getLocalBounds().reduced(10, 0);
        if (botao_.isVisible()) r.removeFromRight(botao_.getWidth() + 8);
        g.drawText(texto_, r, juce::Justification::centredLeft, true);
    }

    void resized() override {
        botao_.setBounds(getLocalBounds().removeFromRight(190).reduced(4, 3));
    }

private:
    juce::String texto_;
    juce::Colour cor_ = juce::Colour(0xffb91c1c);
    juce::TextButton botao_;
    std::function<void()> aoBotao_;
};

} // namespace matriz::ui
