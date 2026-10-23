#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

#include "BarraLateral.h"
#include "MatrizLookAndFeel.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

// FILTER OPTIONS da barra do METADATA no desenho do mapa: cartão claro com seções (HIDE / SHOW ONLY / SELECTION), uma
// linha por opção com título, descrição e interruptor à direita, e "Clear all" no título. Fica aberto enquanto o
// usuário liga/desliga (cada troca já aplica); fecha clicando fora. Abre como CallOutBox sem a seta.
class PainelOpcoesFiltro : public juce::Component {
public:
    struct Opcao {
        int id = 0;
        juce::String secao, titulo, descricao;
        bool ligado = false;
    };

    PainelOpcoesFiltro(std::vector<Opcao> opcoes, std::function<void(int, bool)> aoAlternar)
        : opcoes_(std::move(opcoes)), aoAlternar_(std::move(aoAlternar)) {
        setSize(300, alturaTotal());
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    // Abre preso ao botão (abaixo dele, sem seta). O painel é dono de si via CallOutBox.
    static void abrir(juce::Component& botao, std::vector<Opcao> opcoes, std::function<void(int, bool)> aoAlternar) {
        auto painel = std::make_unique<PainelOpcoesFiltro>(std::move(opcoes), std::move(aoAlternar));
        auto& caixa = juce::CallOutBox::launchAsynchronously(std::move(painel), botao.getScreenBounds(), nullptr);
        caixa.setArrowSize(0.0f);
        caixa.setLookAndFeel(&lfCaixa());
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        auto r = getLocalBounds();
        auto cab = r.removeFromTop(kCabecalho).reduced(kPadX, 0);
        g.setColour(tk.textoPrimario);
        g.setFont(barra::fonteTitulo());
        g.drawText(i18n::t("filtro_opcoes.titulo"), cab, juce::Justification::centredLeft, false);
        limparTudo_ = cab.removeFromRight(juce::GlyphArrangement::getStringWidthInt(fonteLimpar(), i18n::t("filtro_opcoes.limpar_tudo")) + 2);
        g.setFont(fonteLimpar());
        g.setColour(algumaLigada() ? tk.textoPrimario : tk.textoDesativado);
        g.drawText(i18n::t("filtro_opcoes.limpar_tudo"), limparTudo_, juce::Justification::centredRight, false);
        g.fillRect(limparTudo_.getX(), limparTudo_.getCentreY() + 8, limparTudo_.getWidth(), 1);  // sublinhado
        g.setColour(tk.borda.withAlpha(0.6f));
        g.fillRect(kPadX, kCabecalho - 1, getWidth() - 2 * kPadX, 1);

        int y = kCabecalho + 4;
        juce::String secaoAtual;
        for (size_t i = 0; i < opcoes_.size(); ++i) {
            const auto& o = opcoes_[i];
            if (o.secao != secaoAtual) {
                secaoAtual = o.secao;
                g.setColour(tk.textoTerciario);
                g.setFont(barra::fonteTitulo());
                g.drawText(o.secao, kPadX, y, getWidth() - 2 * kPadX, kSecao, juce::Justification::centredLeft, false);
                y += kSecao;
            } else {
                g.setColour(tk.borda.withAlpha(0.4f));
                g.fillRect(kPadX, y, getWidth() - 2 * kPadX, 1);
            }
            auto linha = juce::Rectangle<int>(kPadX, y, getWidth() - 2 * kPadX, kLinha);
            auto chave = linha.removeFromRight(36).withSizeKeepingCentre(36, 20);
            desenharChave(g, chave.toFloat(), o.ligado);
            linha.removeFromRight(10);
            auto textos = linha.withSizeKeepingCentre(linha.getWidth(), 34);
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
            g.drawText(o.titulo, textos.removeFromTop(18), juce::Justification::centredLeft, true);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(o.descricao, textos, juce::Justification::centredLeft, true);
            y += kLinha;
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        const auto p = e.getPosition();
        if (limparTudo_.expanded(4).contains(p)) {
            for (auto& o : opcoes_)
                if (o.ligado) {
                    o.ligado = false;
                    if (aoAlternar_) aoAlternar_(o.id, false);
                }
            repaint();
            return;
        }
        const int i = indiceNaPosicao(p.y);
        if (i < 0) return;
        auto& o = opcoes_[static_cast<size_t>(i)];
        o.ligado = !o.ligado;
        if (aoAlternar_) aoAlternar_(o.id, o.ligado);
        repaint();
    }

private:
    static constexpr int kPadX = 16, kCabecalho = 44, kSecao = 26, kLinha = 48, kPadBaixo = 10;

    static juce::Font fonteLimpar() { return juce::Font(juce::FontOptions(12.5f, juce::Font::bold)); }

    bool algumaLigada() const {
        for (const auto& o : opcoes_) if (o.ligado) return true;
        return false;
    }

    int alturaTotal() const {
        int h = kCabecalho + 4 + kPadBaixo;
        juce::String s;
        for (const auto& o : opcoes_) {
            if (o.secao != s) { s = o.secao; h += kSecao; }
            h += kLinha;
        }
        return h;
    }

    int indiceNaPosicao(int py) const {
        int y = kCabecalho + 4;
        juce::String s;
        for (size_t i = 0; i < opcoes_.size(); ++i) {
            if (opcoes_[i].secao != s) { s = opcoes_[i].secao; y += kSecao; }
            if (py >= y && py < y + kLinha) return static_cast<int>(i);
            y += kLinha;
        }
        return -1;
    }

    static void desenharChave(juce::Graphics& g, juce::Rectangle<float> r, bool ligado) {
        const auto& tk = tema();
        g.setColour(ligado ? tk.acento : tk.botaoSecundario);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        if (!ligado) {
            g.setColour(tk.borda.withAlpha(0.6f));
            g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
        }
        const float d = r.getHeight() - 4.0f;
        const float x = ligado ? r.getRight() - d - 2.0f : r.getX() + 2.0f;
        g.setColour(juce::Colours::white);
        g.fillEllipse(x, r.getY() + 2.0f, d, d);
    }

    // Fundo do CallOutBox: o mesmo cartão claro das barras, cantos de 14 px, borda fina e sombra leve; sem seta.
    struct LfCaixa : public MatrizLookAndFeel {
        void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path&, juce::Image&) override {
            const auto& tk = tema();
            auto r = box.getLocalBounds().toFloat().reduced(6.0f);
            juce::DropShadow(juce::Colours::black.withAlpha(0.14f), 12, {0, 3}).drawForRectangle(g, r.toNearestInt());
            g.setColour(tk.painel);
            g.fillRoundedRectangle(r, 14.0f);
            g.setColour(tk.borda.withAlpha(0.7f));
            g.drawRoundedRectangle(r.reduced(0.5f), 14.0f, 1.0f);
        }
        int getCallOutBoxBorderSize(const juce::CallOutBox&) override { return 6; }
        float getCallOutBoxCornerSize(const juce::CallOutBox&) override { return 14.0f; }
    };
    static LfCaixa& lfCaixa() {
        static LfCaixa lf;
        return lf;
    }

    std::vector<Opcao> opcoes_;
    std::function<void(int, bool)> aoAlternar_;
    juce::Rectangle<int> limparTudo_;
};

} // namespace matriz::ui
