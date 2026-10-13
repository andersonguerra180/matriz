#pragma once

#include <JuceHeader.h>

#include "../I18n/Strings.h"
#include "Tablet.h"
#include "Tokens.h"

// Sistema de janelas do BKR Matriz Mini: UMA moldura para todas as janelas (ver docs/SISTEMA_DE_JANELAS.md).
// Papel creme, canto 18, sombra suave; ícone da seção (40 px, canto 12) é o único lugar com cor; trilha + título em
// caixa alta; X redondo à direita; corpo com margem 24 e blocos em "poço"; rodapé em faixa bege com a ação principal
// (pílula grafite) sempre à direita. Vermelho só para ação que apaga.
namespace matriz::ui::moldura {

enum class Secao { Auto, Backup, Explorador, Ferramentas, Neutro };

// Seção da tela em que o usuário está (o MainComponent atualiza ao trocar de tela); janelas sem seção própria usam esta.
inline Secao& secaoAtual() { static Secao s = Secao::Backup; return s; }

struct Paleta {
    juce::Colour fundo, janela, poco, linha, grafite, tinta, mudo, vermelho, branco, campo;
};

inline Paleta paleta() {
    const auto& tk = tema();
    if (tk.fundo.getBrightness() > 0.5f) {
        return { juce::Colour(0xffece7e0), juce::Colour(0xfff6f2eb), juce::Colour(0xffede8e0), juce::Colour(0xffd8d1c6),
                 juce::Colour(0xff4a4440), juce::Colour(0xff1b1917), juce::Colour(0xff6f6a62), juce::Colour(0xffb8221a),
                 juce::Colours::white, juce::Colour(0xfffaf8f4) };
    }
    return { tk.fundo, tk.cartao, tk.painelAlt, tk.linhaSuave, tk.acento, tk.textoPrimario, tk.textoSecundario, tk.perigo,
             tk.textoSobreAcento, tk.campoFundo };
}

inline Secao resolver(Secao s) { return s == Secao::Auto ? secaoAtual() : s; }

// Cor do ícone da seção e do azulejo atrás dele.
inline juce::Colour corIcone(Secao s) {
    switch (resolver(s)) {
        case Secao::Explorador: return juce::Colour(0xff2e8b57);
        case Secao::Ferramentas: return juce::Colour(0xffc77a12);
        case Secao::Neutro: return paleta().grafite;
        default: return juce::Colour(0xff2b6cb0);
    }
}
inline juce::Colour corAzulejo(Secao s) { return corIcone(s).withAlpha(0.16f); }

inline juce::String nomeDaSecao(Secao s) {
    switch (resolver(s)) {
        case Secao::Explorador: return matriz::i18n::t("moldura.secao.explorador");
        case Secao::Ferramentas: return matriz::i18n::t("moldura.secao.ferramentas");
        case Secao::Neutro: return {};
        default: return matriz::i18n::t("moldura.secao.backup");
    }
}

// Letras acentuadas em caixa alta (o toUpperCase do JUCE não converte ú → Ú no locale C do macOS).
inline juce::String caixaAlta(const juce::String& texto) {
    juce::String r;
    for (auto c : texto.toUpperCase()) r += (c >= 0xE0 && c <= 0xFE && c != 0xF7) ? static_cast<juce::juce_wchar>(c - 0x20) : c;
    return r;
}

struct Geometria {
    juce::Rectangle<int> janela, cabecalho, corpo, rodape, botaoFechar;
};

constexpr int kAlturaCabecalho = 80;
constexpr int kAlturaRodape = 76;
constexpr int kMargemCorpo = 24;
constexpr float kRaio = 18.0f;

inline Geometria geometria(juce::Rectangle<int> janela, bool comRodape) {
    Geometria g;
    g.janela = janela;
    auto r = janela;
    g.cabecalho = r.removeFromTop(kAlturaCabecalho);
    if (comRodape) g.rodape = r.removeFromBottom(kAlturaRodape);
    g.corpo = r;
    g.botaoFechar = juce::Rectangle<int>(36, 36).withCentre({ g.cabecalho.getRight() - 24 - 18, g.cabecalho.getCentreY() });
    return g;
}

// Pinta sombra, papel, cabeçalho (ícone da seção, trilha, título, contador, X, linha de progresso) e a faixa do rodapé.
// `progresso` < 0 = sem linha de progresso; `contador` ex.: "01 / 05".
inline void desenhar(juce::Graphics& g, const Geometria& geo, Secao secao, const juce::String& trilha, const juce::String& titulo,
                     bool comRodape, float progresso = -1.0f, const juce::String& contador = {}, bool comFechar = true) {
    const auto p = paleta();
    const auto janela = geo.janela.toFloat();
    // sombra suave (camadas, sem blur)
    for (int i = 5; i >= 1; --i) {
        g.setColour(juce::Colours::black.withAlpha(0.035f));
        g.fillRoundedRectangle(janela.expanded(static_cast<float>(i) * 3.0f).translated(0.0f, static_cast<float>(i) * 2.0f), kRaio + static_cast<float>(i) * 3.0f);
    }
    g.setColour(p.janela);
    g.fillRoundedRectangle(janela, kRaio);
    if (comRodape) {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle(janela, kRaio);
        g.reduceClipRegion(clip);
        g.setColour(p.poco);
        g.fillRect(geo.rodape);
        g.setColour(p.linha);
        g.fillRect(geo.rodape.withHeight(1));
        g.restoreState();
    }
    g.setColour(p.linha);
    g.drawRoundedRectangle(janela.reduced(0.5f), kRaio, 1.0f);

    // cabeçalho
    const auto cab = geo.cabecalho;
    const juce::Rectangle<float> azulejo(static_cast<float>(cab.getX() + kMargemCorpo), static_cast<float>(cab.getCentreY() - 20), 40.0f, 40.0f);
    g.setColour(corAzulejo(secao));
    g.fillRoundedRectangle(azulejo, 12.0f);
    const IconeUi icone = resolver(secao) == Secao::Explorador ? IconeUi::Pasta
                          : resolver(secao) == Secao::Ferramentas ? IconeUi::Ferramentas : IconeUi::Backup;
    desenharIcone(g, icone, azulejo.reduced(10.0f), corIcone(secao));

    auto texto = cab.withTrimmedLeft(kMargemCorpo + 40 + 16).withTrimmedRight(kMargemCorpo + (comFechar ? 52 : 0) + (contador.isNotEmpty() ? 90 : 0));
    g.setColour(p.mudo);
    g.setFont(fonteUi(10.5f, juce::Font::bold).withExtraKerningFactor(0.2f));
    const auto nome = nomeDaSecao(secao);
    const auto linhaTrilha = trilha.isNotEmpty() ? (nome.isNotEmpty() ? nome + " \xc2\xb7 " + trilha : trilha) : nome;
    g.drawText(caixaAlta(juce::String(linhaTrilha)), juce::Rectangle<int>(texto.getX(), cab.getCentreY() - 25, texto.getWidth(), 16),
               juce::Justification::centredLeft, true);
    g.setColour(p.tinta);
    g.setFont(fonteUi(17.0f, juce::Font::bold).withExtraKerningFactor(0.08f));
    g.drawText(caixaAlta(titulo), juce::Rectangle<int>(texto.getX(), cab.getCentreY() - 8, texto.getWidth(), 26), juce::Justification::centredLeft, true);

    if (contador.isNotEmpty()) {
        g.setColour(p.tinta);
        g.setFont(fonteUi(13.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
        g.drawText(contador, juce::Rectangle<int>(geo.botaoFechar.getX() - 100, cab.getY(), 90, cab.getHeight()), juce::Justification::centredRight, false);
    }
    if (comFechar) {
        const auto x = geo.botaoFechar.toFloat();
        g.setColour(p.janela);
        g.fillEllipse(x);
        g.setColour(p.linha);
        g.drawEllipse(x.reduced(0.5f), 1.0f);
        const auto c = x.getCentre();
        g.setColour(p.tinta);
        g.drawLine(c.x - 6, c.y - 6, c.x + 6, c.y + 6, 1.8f);
        g.drawLine(c.x - 6, c.y + 6, c.x + 6, c.y - 6, 1.8f);
    }
    // linha sob o cabeçalho (+ progresso)
    g.setColour(p.linha);
    g.fillRect(juce::Rectangle<int>(cab.getX(), cab.getBottom() - 1, cab.getWidth(), 1));
    if (progresso >= 0.0f) {
        g.setColour(p.grafite);
        g.fillRect(juce::Rectangle<int>(cab.getX() + 1, cab.getBottom() - 3, static_cast<int>((cab.getWidth() - 2) * juce::jlimit(0.0f, 1.0f, progresso)), 3));
    }
}

// Janelas nativas (DialogWindow) sem barra de título: o cartão ocupa a janela com uma margem para a sombra, sobre o fundo.
constexpr int kMargemJanelaNativa = 18;
inline juce::Rectangle<int> areaDoCartaoNativo(juce::Rectangle<int> total) { return total.reduced(kMargemJanelaNativa); }
inline int tamanhoComMargem(int tamanho) { return tamanho + 2 * kMargemJanelaNativa; }

// Abre `conteudo` numa janela própria no estilo da moldura (sem barra de título; o cabeçalho é da moldura).
inline juce::DialogWindow* abrirJanela(juce::Component* conteudoNovo, const juce::String& titulo, bool modal = false) {
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = titulo;
    opt.content.setOwned(conteudoNovo);
    opt.dialogBackgroundColour = paleta().fundo;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = false;
    opt.resizable = false;
    auto* dw = opt.create();
    dw->setTitleBarHeight(0);
    dw->setContentComponentSize(conteudoNovo->getWidth(), conteudoNovo->getHeight());
    dw->centreWithSize(dw->getWidth(), dw->getHeight());
    dw->setVisible(true);
    if (modal) dw->enterModalState(true, nullptr, true);
    return dw;
}

// Botões da moldura: principal (grafite, ou vermelho se apaga) e secundário (poço com contorno).
inline void estilizarBotao(juce::TextButton& b, bool principal, bool apaga = false) {
    const auto p = paleta();
    b.setColour(juce::TextButton::buttonColourId, principal ? (apaga ? p.vermelho : p.grafite) : p.poco);
    b.setColour(juce::TextButton::buttonOnColourId, principal ? (apaga ? p.vermelho : p.grafite) : p.poco);
    b.setColour(juce::TextButton::textColourOffId, principal ? p.branco : p.tinta);
    b.setColour(juce::TextButton::textColourOnId, principal ? p.branco : p.tinta);
}

}  // namespace matriz::ui::moldura
