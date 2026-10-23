#pragma once

#include <JuceHeader.h>

#include <utility>
#include <vector>

#include "MatrizLookAndFeel.h"
#include "Tokens.h"

// Barras laterais das abas INTAKE, STRUCTURE e METADATA no desenho dos mapas "BARRA LATERAL NO TAMANHO REAL · 240 × 805"
// (out/2026): coluna de 240 px, cards claros de cantos arredondados com título espaçado em caixa alta (e, à direita do
// título, uma contagem ou um controle), chips de 26 px com ponto de cor, nome e contagem à direita, pílulas secundárias
// e a pílula grafite da ação principal. Texto nunca abaixo de 12 px. Só geometria e pintura: cada aba posiciona os
// próprios controles em corpo().
namespace matriz::ui::barra {

inline constexpr int kLargura = 240;
inline constexpr int kMargem = 6;       // borda da coluna -> card
inline constexpr int kPad = 10;         // dentro do card, nas laterais
inline constexpr int kPadV = 8;         // dentro do card, em cima e embaixo
inline constexpr int kPadChip = 8;      // texto do chip até a borda
inline constexpr int kTituloH = 18;
inline constexpr int kEntreCards = 8;
inline constexpr int kChipH = 26;
inline constexpr int kGap = 5;          // entre chips/linhas
inline constexpr float kRaioCard = 12.0f;
inline constexpr float kRaioChip = 7.0f;

inline juce::Font fonteTitulo() {
    return juce::Font(juce::FontOptions(12.0f, juce::Font::bold)).withExtraKerningFactor(0.14f);
}
inline juce::Font fonteChip() { return juce::Font(juce::FontOptions(13.0f)); }
inline juce::Font fonteChipNegrito() { return juce::Font(juce::FontOptions(13.0f, juce::Font::bold)); }
inline juce::Font fonteContagem() { return juce::Font(juce::FontOptions(12.5f)); }
inline juce::Font fontePilula() { return juce::Font(juce::FontOptions(13.0f, juce::Font::bold)); }

// "Audio (43)" -> {"Audio", "43"}; sem "(n)" no fim, a contagem vem vazia.
inline std::pair<juce::String, juce::String> separarContagem(const juce::String& texto) {
    const auto t = texto.trim();
    if (t.endsWithChar(')')) {
        const int abre = t.lastIndexOf(" (");
        if (abre > 0) {
            const auto dentro = t.substring(abre + 2, t.length() - 1);
            if (dentro.containsOnly("0123456789")) return {t.substring(0, abre), dentro};
        }
    }
    return {t, {}};
}

// Largura que um chip precisa para mostrar nome + contagem sem cortar (quem monta a grade decide meia linha ou linha
// inteira com isto).
inline int larguraNecessariaDoChip(const juce::String& texto, bool comPonto) {
    const auto [nome, n] = separarContagem(texto);
    int w = kPadChip + (comPonto ? 14 : 0) + juce::GlyphArrangement::getStringWidthInt(fonteChip(), nome) + kPadChip;
    if (n.isNotEmpty()) w += 8 + juce::GlyphArrangement::getStringWidthInt(fonteContagem(), n);
    return w;
}

// Trilho do seletor lista/grade (os dois ViewModeIconButton com setSegmentado(true) ficam por cima).
inline void pintarTrilhoSegmentado(juce::Graphics& g, juce::Rectangle<int> r) {
    const auto& tk = tema();
    g.setColour(tk.botaoSecundario);
    g.fillRoundedRectangle(r.toFloat(), r.getHeight() * 0.5f);
}

class Layout {
public:
    // `areaColuna`: os 240 px inteiros da esquerda (altura total do componente).
    void comecar(juce::Rectangle<int> areaColuna) {
        coluna_ = areaColuna;
        corpo_ = areaColuna.reduced(kMargem, 8);
        cards_.clear();
        titulos_.clear();
        direitas_.clear();
    }

    juce::Rectangle<int>& corpo() { return corpo_; }

    // Abre um card: reserva a linha do título e estreita corpo() para dentro do padding. Devolve a linha do título
    // (útil para pôr um controle à direita dele). Título vazio = card sem título (ex.: a linha da busca não é card).
    juce::Rectangle<int> iniciarCard(const juce::String& titulo) {
        cardTopo_ = corpo_.getY();
        corpo_.removeFromTop(kPadV);
        corpo_ = corpo_.withTrimmedLeft(kPad).withTrimmedRight(kPad);
        juce::Rectangle<int> linha;
        if (titulo.isNotEmpty()) {
            linha = corpo_.removeFromTop(kTituloH);
            corpo_.removeFromTop(6);
        }
        titulos_.push_back({titulo, linha});
        direitas_.push_back({});
        return linha;
    }

    // Card de uma linha só com o título à esquerda e os controles ao lado (VIEW). Devolve o que sobra da linha.
    juce::Rectangle<int> iniciarCardEmLinha(const juce::String& titulo, int altura) {
        cardTopo_ = corpo_.getY();
        corpo_.removeFromTop(kPadV);
        corpo_ = corpo_.withTrimmedLeft(kPad).withTrimmedRight(kPad);
        auto linha = corpo_.removeFromTop(altura);
        const int w = juce::GlyphArrangement::getStringWidthInt(fonteTitulo(), titulo) + 4;
        titulos_.push_back({titulo, linha.removeFromLeft(w)});
        direitas_.push_back({});
        linha.removeFromLeft(10);
        return linha;
    }

    // Texto cinza à direita do título do card aberto ("1 active", "0 selected", "376 items").
    void textoDireita(const juce::String& texto) {
        if (!direitas_.empty()) direitas_.back() = texto;
    }

    void finalizarCard() {
        corpo_.removeFromTop(kPadV);
        corpo_ = corpo_.withTrimmedLeft(-kPad).withTrimmedRight(-kPad);
        cards_.push_back({corpo_.getX(), cardTopo_, corpo_.getWidth(), corpo_.getY() - cardTopo_});
        corpo_.removeFromTop(kEntreCards);
    }

    // Último card estica até `fundo` (ex.: FOLDER EXPLORER ocupa a altura que sobra).
    void esticarUltimoCardAte(int fundo) {
        if (!cards_.empty()) cards_.back().setBottom(fundo);
    }

    juce::Rectangle<int> ultimoCard() const { return cards_.empty() ? juce::Rectangle<int>() : cards_.back(); }

    void paint(juce::Graphics& g) const {
        const auto& tk = tema();
        g.setColour(tk.fundo);
        g.fillRect(coluna_);
        g.setColour(tk.borda);
        g.fillRect(coluna_.getRight() - 1, coluna_.getY(), 1, coluna_.getHeight());
        for (const auto& c : cards_) {
            g.setColour(tk.painel);
            g.fillRoundedRectangle(c.toFloat(), kRaioCard);
            g.setColour(tk.borda.withAlpha(0.55f));
            g.drawRoundedRectangle(c.toFloat().reduced(0.5f), kRaioCard, 1.0f);
        }
        for (size_t i = 0; i < titulos_.size(); ++i) {
            const auto& [titulo, r] = titulos_[i];
            if (titulo.isEmpty()) continue;
            g.setColour(tk.textoSecundario);
            g.setFont(fonteTitulo());
            g.drawText(titulo, r, juce::Justification::centredLeft, false);
            if (i < direitas_.size() && direitas_[i].isNotEmpty()) {
                g.setColour(tk.textoTerciario);
                g.setFont(fonteContagem());
                g.drawText(direitas_[i], r, juce::Justification::centredRight, true);
            }
        }
    }

private:
    juce::Rectangle<int> coluna_;
    juce::Rectangle<int> corpo_;
    int cardTopo_ = 0;
    std::vector<juce::Rectangle<int>> cards_;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> titulos_;
    std::vector<juce::String> direitas_;
};

// Chip de filtro: ponto de cor (propriedade "cor", ARGB), nome à esquerda e contagem à direita (o "(n)" do fim do
// texto do botão). Ativo (propriedade "ativo") = grafite; contagem 0 / sem contagem com "apagarSemContagem" = cinza,
// ainda clicável. Propriedade "negrito" = nome em negrito (anos).
struct ChipLf : public MatrizLookAndFeel {
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool sobre, bool apertado) override {
        const auto& tk = tema();
        const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const bool ativo = b.getProperties()["ativo"];
        juce::Colour fundo = ativo ? tk.acento : tk.painelAlt;
        if (apertado) fundo = fundo.darker(0.08f);
        else if (sobre) fundo = ativo ? fundo.brighter(0.1f) : fundo.darker(0.03f);
        g.setColour(fundo);
        g.fillRoundedRectangle(r, kRaioChip);
        if (!ativo) {
            g.setColour(tk.borda.withAlpha(0.6f));
            g.drawRoundedRectangle(r, kRaioChip, 1.0f);
        }
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        const auto& tk = tema();
        const bool ativo = b.getProperties()["ativo"];
        const auto [nome, n] = separarContagem(b.getButtonText());
        const bool apagado = !ativo && b.getProperties()["apagarSemContagem"] && (n.isEmpty() || n == "0");
        auto r = b.getLocalBounds().reduced(kPadChip, 0);
        if (b.getProperties().contains("cor")) {
            const auto cor = juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(b.getProperties()["cor"])));
            g.setColour(apagado ? cor.withAlpha(0.45f) : cor);
            g.fillEllipse(static_cast<float>(r.getX()), r.getCentreY() - 4.0f, 8.0f, 8.0f);
            r.removeFromLeft(14);
        }
        if (n.isNotEmpty()) {
            g.setFont(fonteContagem());
            g.setColour(ativo ? tk.textoSobreAcento.withAlpha(0.8f) : tk.textoTerciario);
            const int w = juce::GlyphArrangement::getStringWidthInt(fonteContagem(), n) + 2;
            g.drawText(n, r.removeFromRight(w), juce::Justification::centredRight, false);
            r.removeFromRight(6);
        }
        g.setFont(b.getProperties()["negrito"] ? fonteChipNegrito() : fonteChip());
        g.setColour(ativo ? tk.textoSobreAcento : (apagado ? tk.textoDesativado : tk.textoPrimario));
        g.drawText(nome, r, juce::Justification::centredLeft, true);
    }
};

// Pílula secundária (Select all, Clear, + New folder, Rename...): fundo bege, texto em negrito, centralizado.
struct PilulaLf : public MatrizLookAndFeel {
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool sobre, bool apertado) override {
        const auto& tk = tema();
        const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const bool ativo = b.getProperties()["ativo"] || b.getToggleState();
        juce::Colour fundo = ativo ? tk.acento : tk.botaoSecundario;
        if (!b.isEnabled()) fundo = tk.painel;
        else if (apertado) fundo = fundo.darker(0.08f);
        else if (sobre) fundo = fundo.darker(0.04f);
        g.setColour(fundo);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(tk.borda.withAlpha(b.isEnabled() ? 0.5f : 0.7f));
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        const auto& tk = tema();
        const bool ativo = b.getProperties()["ativo"] || b.getToggleState();
        const auto fonte = b.getProperties()["leve"] ? fonteChip() : fontePilula();
        const auto cor = !b.isEnabled() ? tk.textoDesativado : (ativo ? tk.textoSobreAcento : tk.textoPrimario);
        g.setFont(fonte);
        g.setColour(cor);
        if (b.getProperties()["lupa"]) {  // "Find": lupa antes do texto, os dois centralizados
            const float wTexto = juce::GlyphArrangement::getStringWidth(fonte, b.getButtonText());
            const auto r = b.getLocalBounds().toFloat();
            float x = r.getCentreX() - (12.0f + 6.0f + wTexto) * 0.5f;
            const float cy = r.getCentreY();
            g.drawEllipse(x, cy - 5.5f, 9.0f, 9.0f, 1.5f);
            g.drawLine(x + 7.5f, cy + 2.0f, x + 11.0f, cy + 5.5f, 1.6f);
            g.drawText(b.getButtonText(), juce::Rectangle<float>(x + 18.0f, r.getY(), wTexto + 2.0f, r.getHeight()),
                       juce::Justification::centredLeft, false);
            return;
        }
        g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(8, 0), juce::Justification::centred, 1, 0.85f);
    }
};

// Botões de um grupo segmentado sobre o trilho bege (– | Fit | + do STRUCTURE): sem fundo próprio; com a propriedade
// "destaque", uma pílula clara por trás do texto (o Fit do mapa).
struct SegmentoLf : public MatrizLookAndFeel {
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool sobre, bool apertado) override {
        const auto& tk = tema();
        const auto r = b.getLocalBounds().toFloat().reduced(3.0f);
        if (b.getProperties()["destaque"]) {
            g.setColour(apertado ? tk.campoFundo.darker(0.06f) : tk.campoFundo);
            g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        } else if (sobre || apertado) {
            g.setColour(tk.painel.withAlpha(apertado ? 0.9f : 0.6f));
            g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        }
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        const auto& tk = tema();
        g.setFont(b.getProperties()["destaque"] ? fontePilula() : juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.setColour(b.isEnabled() ? tk.textoPrimario : tk.textoDesativado);
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }
};

// Pílula grafite da ação principal. Com a propriedade "rotulo", desenha "rotulo · n" (n = o "(n)" do texto do botão,
// que fica como está para quem já o lê); sem ela, o próprio texto.
struct PrimariaLf : public MatrizLookAndFeel {
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool sobre, bool apertado) override {
        const auto& tk = tema();
        const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        juce::Colour fundo = b.isEnabled() ? tk.acento : tk.acento.withAlpha(0.45f);
        if (apertado) fundo = fundo.darker(0.15f);
        else if (sobre) fundo = tk.acentoHover;
        g.setColour(fundo);
        const float raio = b.getProperties().contains("raio") ? static_cast<float>(b.getProperties()["raio"]) : r.getHeight() * 0.5f;
        g.fillRoundedRectangle(r, raio);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        const auto& tk = tema();
        juce::String texto = b.getButtonText();
        if (b.getProperties().contains("rotulo")) {
            const auto n = separarContagem(texto).second;
            texto = b.getProperties()["rotulo"].toString() + (n.isNotEmpty() ? juce::String::fromUTF8(" \xc2\xb7 ") + n : juce::String());
        }
        g.setFont(fontePilula());
        g.setColour(tk.textoSobreAcento);
        g.drawFittedText(texto, b.getLocalBounds().reduced(8, 0), juce::Justification::centred, 1, 0.85f);
    }
};

// Botão compacto "Filter options": ícone de funil + o número de opções ligadas (propriedade "n"; 0 = só o ícone).
struct FiltroOpcoesLf : public MatrizLookAndFeel {
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool sobre, bool apertado) override {
        const auto& tk = tema();
        const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const int n = b.getProperties()["n"];
        // Sem opção ligada: bege; com 1 ou mais: grafite (mapa).
        juce::Colour fundo = n > 0 ? tk.acento : tk.botaoSecundario;
        if (apertado) fundo = fundo.darker(0.1f);
        else if (sobre) fundo = n > 0 ? tk.acentoHover : fundo.darker(0.04f);
        g.setColour(fundo);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        if (n == 0) {
            g.setColour(tk.borda.withAlpha(0.6f));
            g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
        }
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        const auto& tk = tema();
        const int n = b.getProperties()["n"];
        const juce::String num = n > 0 ? juce::String(n) : juce::String();
        const int wNum = num.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt(fontePilula(), num) + 5;
        const float wIcone = 13.0f;
        auto r = b.getLocalBounds().toFloat();
        float x = r.getCentreX() - (wIcone + wNum) * 0.5f;
        const float cy = r.getCentreY();
        g.setColour(n > 0 ? tk.textoSobreAcento : tk.textoPrimario);
        g.fillRoundedRectangle(x, cy - 4.5f, 13.0f, 1.6f, 0.8f);
        g.fillRoundedRectangle(x + 2.5f, cy - 0.8f, 8.0f, 1.6f, 0.8f);
        g.fillRoundedRectangle(x + 4.5f, cy + 2.9f, 4.0f, 1.6f, 0.8f);
        if (wNum > 0) {
            g.setFont(fontePilula());
            g.drawText(num, juce::Rectangle<float>(x + wIcone + 5.0f, r.getY(), static_cast<float>(wNum), r.getHeight()),
                       juce::Justification::centredLeft, false);
        }
    }
};

// Ponto de cor de cada tipo de mídia — o mesmo nas barras do METADATA e do INTAKE.
inline juce::Colour corDoTipo(const juce::String& chave) {
    if (chave == "audio") return juce::Colour(0xff2a9d8f);
    if (chave == "video") return juce::Colour(0xff9d4edd);
    if (chave == "images" || chave == "image") return juce::Colour(0xfff4a261);
    if (chave == "documents" || chave == "doc") return juce::Colour(0xff2b9348);
    if (chave == "sessions") return juce::Colour(0xff2b9348);
    if (chave == "phone_dump") return juce::Colour(0xff0ea5c6);
    if (chave == "projects") return juce::Colour(0xffb8860b);
    return juce::Colour(0xff8a8f98);  // others, AI generated, merge conflicts
}

inline void definirCor(juce::Component& c, juce::Colour cor) {
    c.getProperties().set("cor", static_cast<juce::int64>(cor.getARGB()));
}

inline void definirAtivo(juce::Component& c, bool ativo) {
    if (static_cast<bool>(c.getProperties()["ativo"]) == ativo) return;
    c.getProperties().set("ativo", ativo);
    c.repaint();
}

} // namespace matriz::ui::barra
