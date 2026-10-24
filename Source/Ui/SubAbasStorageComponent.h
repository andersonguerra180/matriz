#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "Tokens.h"

namespace matriz::ui {

// Sub-abas da aba STORAGE (STORAGE / SPACE MAP), no desenho do anexo: abas de pasta com ícone e texto espaçado em caixa
// alta, alinhadas à esquerda; a ativa tem a cor do conteúdo e emenda no painel de borda arredondada logo abaixo, a inativa
// fica mais escura, um pouco mais baixa e separada pela linha do painel. O componente ocupa a área inteira (abas + painel)
// e fica ATRÁS do workspace, que o MainComponent põe em areaConteudo().
class SubAbasStorageComponent : public juce::Component {
public:
    enum class Icone { Disco, Mapa };
    struct Aba {
        juce::String label;
        Icone icone;
    };

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

    // Cor que o workspace pinta de fundo: a aba ativa e o miolo do painel usam a mesma, para emendar sem costura.
    void definirCorConteudo(juce::Colour c) {
        if (corConteudo_ == c) return;
        corConteudo_ = c;
        repaint();
    }

    // Onde o workspace da sub-aba ativa vai (dentro do painel, sem tocar a borda arredondada).
    juce::Rectangle<int> areaConteudo() const { return painel_.reduced(kInsetConteudo); }

    std::function<void(int)> aoTrocar;

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.fundo);
        if (painel_.isEmpty()) return;

        const auto p = painel_.toFloat();
        const bool ativaNaBorda = selecionada_ >= 0 && selecionada_ < static_cast<int>(bounds_.size()) &&
                                  bounds_[static_cast<size_t>(selecionada_)].getX() == painel_.getX();
        // Painel: canto superior esquerdo reto quando a aba ativa começa nele (a borda da aba desce direto).
        juce::Path caixa;
        caixa.addRoundedRectangle(p.getX(), p.getY(), p.getWidth(), p.getHeight(), kRaioPainel, kRaioPainel, !ativaNaBorda, true, true, true);
        g.setColour(corConteudo_);
        g.fillPath(caixa);
        g.setColour(tk.borda);
        g.strokePath(caixa, juce::PathStrokeType(1.0f));

        for (size_t i = 0; i < abas_.size(); ++i) {
            const bool ativa = static_cast<int>(i) == selecionada_;
            const bool hover = static_cast<int>(i) == hover_;
            auto t = bounds_[i].toFloat();
            if (!ativa) t = t.withTrimmedTop(kDesnivelInativa);

            juce::Path contorno;  // topo arredondado e laterais; a base fica aberta
            contorno.startNewSubPath(t.getX(), t.getBottom());
            contorno.lineTo(t.getX(), t.getY() + kRaioAba);
            contorno.quadraticTo(t.getX(), t.getY(), t.getX() + kRaioAba, t.getY());
            contorno.lineTo(t.getRight() - kRaioAba, t.getY());
            contorno.quadraticTo(t.getRight(), t.getY(), t.getRight(), t.getY() + kRaioAba);
            contorno.lineTo(t.getRight(), t.getBottom());
            juce::Path preenchimento(contorno);
            preenchimento.closeSubPath();

            juce::Colour fundo = ativa ? corConteudo_ : tk.botaoSecundario;
            if (!ativa && hover) fundo = fundo.brighter(0.04f).interpolatedWith(corConteudo_, 0.35f);
            g.setColour(fundo);
            g.fillPath(preenchimento);
            g.setColour(tk.borda);
            g.strokePath(contorno, juce::PathStrokeType(1.0f));
            if (ativa) {  // apaga a linha do painel debaixo da aba ativa: aba e painel viram uma peça só
                g.setColour(corConteudo_);
                g.fillRect(t.getX() + 0.5f, t.getBottom() - 1.0f, t.getWidth() - 1.0f, 2.5f);
            }

            const auto cor = ativa ? tk.textoPrimario : (hover ? tk.textoPrimario : tk.textoSecundario);
            const auto fonte = fonteAba();
            const float wTexto = juce::GlyphArrangement::getStringWidth(fonte, abas_[i].label);
            const float wConjunto = kIcone + kGapIcone + wTexto;
            const float x0 = t.getCentreX() - wConjunto * 0.5f;
            const float cy = t.getCentreY() + (ativa ? 0.0f : -0.5f);
            g.setColour(cor);
            desenharIcone(g, abas_[i].icone, x0, cy);
            g.setFont(fonte);
            g.drawText(abas_[i].label, juce::Rectangle<float>(x0 + kIcone + kGapIcone, t.getY(), wTexto + 4.0f, t.getHeight()),
                       juce::Justification::centredLeft, false);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(kMargemX, 0);
        area.removeFromTop(kTopo);
        auto linhaAbas = area.removeFromTop(kAlturaAbas);
        area.removeFromBottom(kMargemBaixo);
        painel_ = area;

        bounds_.assign(abas_.size(), {});
        int x = linhaAbas.getX();
        for (size_t i = 0; i < abas_.size(); ++i) {
            const int w = juce::GlyphArrangement::getStringWidthInt(fonteAba(), abas_[i].label) +
                          static_cast<int>(kIcone + kGapIcone) + 2 * kPadAba;
            bounds_[i] = juce::Rectangle<int>(x, linhaAbas.getY(), w, linhaAbas.getHeight());
            x += w + 4;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const int i = abaEm(e.getPosition());
        if (i >= 0 && i != selecionada_) {
            selecionada_ = i;
            repaint();
            if (aoTrocar) aoTrocar(i);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int novo = abaEm(e.getPosition());
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
    static constexpr int kMargemX = 12;
    static constexpr int kTopo = 14;
    static constexpr int kAlturaAbas = 40;
    static constexpr int kMargemBaixo = 12;
    static constexpr int kPadAba = 22;
    static constexpr int kInsetConteudo = 3;  // raio 10: o canto do retângulo do conteúdo fica dentro do arco
    static constexpr float kRaioPainel = 10.0f;
    static constexpr float kRaioAba = 8.0f;
    static constexpr float kDesnivelInativa = 4.0f;
    static constexpr float kIcone = 14.0f;
    static constexpr float kGapIcone = 9.0f;

    static juce::Font fonteAba() { return juce::Font(juce::FontOptions(13.0f, juce::Font::bold)).withExtraKerningFactor(0.12f); }

    static void desenharIcone(juce::Graphics& g, Icone icone, float x, float cy) {
        if (icone == Icone::Disco) {  // drive: retângulo arredondado com o "led" à esquerda
            g.drawRoundedRectangle(x + 0.5f, cy - 4.5f, 13.0f, 9.0f, 2.5f, 1.4f);
            g.fillRoundedRectangle(x + 3.0f, cy - 1.0f, 4.0f, 2.0f, 1.0f);
        } else {  // mapa de espaço: quadrado com uma coluna à esquerda e o lado direito dividido
            g.drawRoundedRectangle(x + 0.5f, cy - 6.0f, 12.0f, 12.0f, 2.0f, 1.4f);
            g.drawLine(x + 5.5f, cy - 6.0f, x + 5.5f, cy + 6.0f, 1.3f);
            g.drawLine(x + 5.5f, cy, x + 12.5f, cy, 1.3f);
        }
    }

    int abaEm(juce::Point<int> pos) const {
        for (size_t i = 0; i < bounds_.size(); ++i)
            if (bounds_[i].contains(pos)) return static_cast<int>(i);
        return -1;
    }

    std::vector<Aba> abas_;
    std::vector<juce::Rectangle<int>> bounds_;
    juce::Rectangle<int> painel_;
    juce::Colour corConteudo_ = tema().fundo;
    int selecionada_ = 0;
    int hover_ = -1;
};

}  // namespace matriz::ui
