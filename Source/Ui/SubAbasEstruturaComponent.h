#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "Tokens.h"

namespace matriz::ui {

// Faixa de sub-abas (STRUCTURE › FOLDER MAP / SPACE MAP). Mesmo visual das abas
// principais de BarraNavegacaoComponent::paint (aba de pasta, cantos superiores
// raio 5, ativa em amarelo claro a 30% com contorno preto, inativa em painelAlt
// a 15%, hover, cursor de mão, largura medida pelo texto), lado a lado à
// esquerda sobre uma linha de base. Cada aba leva um ponto de cor de identidade.
class SubAbasEstruturaComponent : public juce::Component {
public:
    struct Aba {
        juce::String label;
        juce::Colour corPonto;
    };

    static constexpr int kAltura = 40;

    void definirAbas(std::vector<Aba> abas) {
        abas_ = std::move(abas);
        hover_ = -1;
        resized();
        repaint();
    }

    // Só marca a aba ativa; não dispara aoTrocar (quem chama já sabe).
    void selecionar(int indice) {
        if (selecionada_ == indice) return;
        selecionada_ = indice;
        repaint();
    }
    int selecionada() const { return selecionada_; }

    std::function<void(int)> aoTrocar;

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);
        g.setColour(tk.borda);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);

        static const juce::Colour corAmareloClaro(0xfffde047);
        for (size_t i = 0; i < abas_.size(); ++i) {
            const bool ativa = static_cast<int>(i) == selecionada_;
            const bool hover = static_cast<int>(i) == hover_;
            const auto tb = bounds_[i].toFloat();
            const float r = 5.0f;
            const float bx = tb.getX(), by = tb.getY(), bw = tb.getWidth(), bh = tb.getHeight();

            juce::Path caminho;
            caminho.startNewSubPath(bx, by + bh);
            caminho.lineTo(bx, by + r);
            caminho.addArc(bx, by, r * 2.0f, r * 2.0f, juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f, false);
            caminho.lineTo(bx + bw - r, by);
            caminho.addArc(bx + bw - r * 2.0f, by, r * 2.0f, r * 2.0f, juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi, false);
            caminho.lineTo(bx + bw, by + bh);
            juce::Path preenchimento(caminho);
            preenchimento.closeSubPath();

            if (ativa) {
                g.setColour(corAmareloClaro.withAlpha(0.30f));
                g.fillPath(preenchimento);
                g.setColour(juce::Colours::black);
                g.strokePath(caminho, juce::PathStrokeType(1.5f));
                g.setColour(tk.acento);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
            } else if (hover) {
                g.setColour(tk.painelAlt.withAlpha(0.40f));
                g.fillPath(preenchimento);
                g.setColour(tk.borda.withAlpha(0.60f));
                g.strokePath(caminho, juce::PathStrokeType(1.0f));
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            } else {
                g.setColour(tk.painelAlt.withAlpha(0.15f));
                g.fillPath(preenchimento);
                g.setColour(tk.borda.withAlpha(0.40f));
                g.strokePath(caminho, juce::PathStrokeType(0.8f));
                g.setColour(tk.textoSecundario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            }

            // Ponto de identidade + texto (o conjunto fica centralizado na aba).
            const float d = 8.0f;
            auto area = bounds_[i].toFloat();
            const float wTexto = static_cast<float>(larguraTexto(abas_[i].label, ativa));
            const float wConjunto = d + kGapPonto + wTexto;
            const float x0 = area.getCentreX() - wConjunto * 0.5f;
            g.setColour(abas_[i].corPonto.withAlpha(ativa || hover ? 1.0f : 0.75f));
            g.fillEllipse(x0, area.getCentreY() - d * 0.5f, d, d);
            g.setColour(ativa ? tk.acento : (hover ? tk.textoPrimario : tk.textoSecundario));
            g.drawText(abas_[i].label, juce::Rectangle<float>(x0 + d + kGapPonto, area.getY(), wTexto + 2.0f, area.getHeight()),
                       juce::Justification::centredLeft, false);
        }
    }

    void resized() override {
        bounds_.assign(abas_.size(), {});
        const int tabY = 5;
        const int tabH = getHeight() - 6;  // apoiada na linha de base, como as abas principais
        int x = 16;
        for (size_t i = 0; i < abas_.size(); ++i) {
            // Mesma medida das principais (texto + 26, mínimo 88), mais o ponto.
            const int w = std::max(88, larguraTexto(abas_[i].label, true) + 26 + static_cast<int>(8 + kGapPonto));
            bounds_[i] = juce::Rectangle<int>(x, tabY, w, tabH);
            x += w + 4;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        for (size_t i = 0; i < bounds_.size(); ++i) {
            if (bounds_[i].contains(e.getPosition())) {
                if (static_cast<int>(i) != selecionada_) {
                    selecionada_ = static_cast<int>(i);
                    repaint();
                    if (aoTrocar) aoTrocar(static_cast<int>(i));
                }
                return;
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        int novo = -1;
        for (size_t i = 0; i < bounds_.size(); ++i)
            if (bounds_[i].contains(e.getPosition())) novo = static_cast<int>(i);
        setMouseCursor(novo >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (novo != hover_) {
            hover_ = novo;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        if (hover_ != -1) {
            hover_ = -1;
            repaint();
        }
    }

private:
    static constexpr float kGapPonto = 6.0f;

    static int larguraTexto(const juce::String& texto, bool negrito) {
        const auto& tk = tema();
        juce::Font f(juce::FontOptions(tk.tamanhoFonteCorpo, negrito ? juce::Font::bold : juce::Font::plain));
        return juce::GlyphArrangement::getStringWidthInt(f, texto);
    }

    std::vector<Aba> abas_;
    std::vector<juce::Rectangle<int>> bounds_;
    int selecionada_ = 0;
    int hover_ = -1;
};

} // namespace matriz::ui
