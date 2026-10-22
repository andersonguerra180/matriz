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
    juce::Colour janelaDestaque;  // card que precisa se destacar dos demais (ex.: GEO LOCATION nos questionários)
};

inline Paleta paleta() {
    const auto& tk = tema();
    if (tk.fundo.getBrightness() > 0.5f) {
        return { juce::Colour(0xffece7e0), juce::Colour(0xfff6f2eb), juce::Colour(0xffede8e0), juce::Colour(0xffd8d1c6),
                 juce::Colour(0xff4a4440), juce::Colour(0xff1b1917), juce::Colour(0xff6f6a62), juce::Colour(0xffb8221a),
                 juce::Colours::white, juce::Colour(0xfffaf8f4), juce::Colour(0xffeee7d8) };
    }
    return { tk.fundo, tk.cartao, tk.painelAlt, tk.linhaSuave, tk.acento, tk.textoPrimario, tk.textoSecundario, tk.perigo,
             tk.textoSobreAcento, tk.campoFundo, tk.cartao.interpolatedWith(tk.acento, 0.12f) };
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
// `etiqueta` (opcional): tag grande e colorida no cabeçalho, à esquerda do X — ex.: o tipo do PROJECT ("VINYL TRANSCRIPTION").
inline void desenhar(juce::Graphics& g, const Geometria& geo, Secao secao, const juce::String& trilha, const juce::String& titulo,
                     bool comRodape, float progresso = -1.0f, const juce::String& contador = {}, bool comFechar = true,
                     const juce::String& etiqueta = {}) {
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

    int larguraEtiqueta = 0;
    if (etiqueta.isNotEmpty()) {
        const auto fonteEtiqueta = fonteUi(14.0f, juce::Font::bold).withExtraKerningFactor(0.12f);
        larguraEtiqueta = juce::GlyphArrangement::getStringWidthInt(fonteEtiqueta, caixaAlta(etiqueta)) + 36;
        const int xDireita = geo.botaoFechar.getX() - (contador.isNotEmpty() ? 110 : 16);
        const juce::Rectangle<float> pilula(static_cast<float>(xDireita - larguraEtiqueta), static_cast<float>(cab.getCentreY() - 16), static_cast<float>(larguraEtiqueta), 32.0f);
        g.setColour(juce::Colour(0xffb8860b));
        g.fillRoundedRectangle(pilula, 16.0f);
        g.setColour(juce::Colours::white);
        g.setFont(fonteEtiqueta);
        g.drawText(caixaAlta(etiqueta), pilula.toNearestInt(), juce::Justification::centred, false);
        larguraEtiqueta += 16;
    }
    auto texto = cab.withTrimmedLeft(kMargemCorpo + 40 + 16).withTrimmedRight(kMargemCorpo + (comFechar ? 52 : 0) + (contador.isNotEmpty() ? 90 : 0) + larguraEtiqueta);
    g.setColour(p.mudo);
    g.setFont(fonteUi(tema().tamanhoFonteMicro, juce::Font::bold).withExtraKerningFactor(0.06f));
    const auto nome = nomeDaSecao(secao);
    const auto linhaTrilha = trilha.isNotEmpty() ? (nome.isNotEmpty() ? nome + " \xc2\xb7 " + trilha : trilha) : nome;
    g.drawText(caixaAlta(juce::String(linhaTrilha)), juce::Rectangle<int>(texto.getX(), cab.getCentreY() - 25, texto.getWidth(), 16),
               juce::Justification::centredLeft, true);
    g.setColour(p.tinta);
    g.setFont(juce::Font(juce::FontOptions("Jost", 17.0f, juce::Font::bold)).withExtraKerningFactor(0.06f));  // Jost: só títulos de janela
    g.drawText(caixaAlta(titulo), juce::Rectangle<int>(texto.getX(), cab.getCentreY() - 8, texto.getWidth(), 26), juce::Justification::centredLeft, true);

    if (contador.isNotEmpty()) {
        g.setColour(p.tinta);
        g.setFont(fonteUi(13.0f, juce::Font::bold).withExtraKerningFactor(0.06f));
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

// Envolve o conteúdo de um diálogo existente na moldura (cabeçalho de 80 px com ícone da seção, trilha, título em caixa
// alta e X redondo; cartão creme canto 18 com sombra). O conteúdo mantém o próprio tamanho e layout: só ganha o
// cabeçalho acima. Usar com abrirSemBarra() — ou com LaunchOptions que já não usem barra nativa.
class JanelaMoldura : public juce::Component {
public:
    // `possuir` = false quando o dono do conteúdo é outro (a moldura só o hospeda).
    JanelaMoldura(juce::Component* conteudo, Secao secao, const juce::String& trilha, const juce::String& titulo,
                  bool comFechar = true, bool possuir = true)
        : conteudo_(conteudo), secao_(secao), trilha_(trilha), titulo_(titulo), comFechar_(comFechar) {
        if (possuir) dono_.reset(conteudo);
        addAndMakeVisible(*conteudo_);
        setSize(tamanhoComMargem(conteudo_->getWidth()), tamanhoComMargem(kAlturaCabecalho + conteudo_->getHeight()));
    }
    void definirTitulo(const juce::String& titulo) { titulo_ = titulo; repaint(); }
    // Tag do tipo do PROJECT no cabeçalho ("" = sem tag).
    void definirEtiqueta(const juce::String& etiqueta) { etiqueta_ = etiqueta; repaint(); }

    void paint(juce::Graphics& g) override {
        desenhar(g, geometria(areaDoCartaoNativo(getLocalBounds()), false), secao_, trilha_, titulo_, false, -1.0f, {}, comFechar_, etiqueta_);
    }
    void paintOverChildren(juce::Graphics& g) override {
        // O conteúdo pinta fundo retangular: repinta os cantos fora do cartão arredondado com o fundo da janela
        // e refaz o contorno do cartão.
        const auto cartao = areaDoCartaoNativo(getLocalBounds()).toFloat();
        juce::Path fora;
        fora.addRectangle(cartao);
        fora.addRoundedRectangle(cartao, kRaio);
        fora.setUsingNonZeroWinding(false);
        g.setColour(paleta().fundo);
        g.fillPath(fora);
        g.setColour(paleta().linha);
        g.drawRoundedRectangle(cartao.reduced(0.5f), kRaio, 1.0f);
    }
    void resized() override { conteudo_->setBounds(geometria(areaDoCartaoNativo(getLocalBounds()), false).corpo); }

    void mouseDown(const juce::MouseEvent& e) override {
        const auto geo = geometria(areaDoCartaoNativo(getLocalBounds()), false);
        arrastando_ = false;
        if (comFechar_ && geo.botaoFechar.contains(e.getPosition())) { fechar(); return; }
        if (geo.cabecalho.contains(e.getPosition())) {
            if (auto* topo = getTopLevelComponent()) { arrastando_ = true; dragger_.startDraggingComponent(topo, e.getEventRelativeTo(topo)); }
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!arrastando_) return;
        if (auto* topo = getTopLevelComponent()) dragger_.dragComponent(topo, e.getEventRelativeTo(topo), nullptr);
    }
    void mouseMove(const juce::MouseEvent& e) override {
        const auto geo = geometria(areaDoCartaoNativo(getLocalBounds()), false);
        setMouseCursor(comFechar_ && geo.botaoFechar.contains(e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

private:
    void fechar() {
        if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>()) dw->closeButtonPressed();
    }
    std::unique_ptr<juce::Component> dono_;
    juce::Component* conteudo_ = nullptr;
    Secao secao_;
    juce::String trilha_, titulo_, etiqueta_;
    bool comFechar_ = true;
    bool arrastando_ = false;
    juce::ComponentDragger dragger_;
};

// Abre uma janela frameless no estilo da moldura a partir de LaunchOptions já preenchidas (o conteúdo já está em
// `opt.content`, com o tamanho do cartão + margem de sombra). Mesmo comportamento do launchAsync() (modal, apaga ao fechar),
// mas sem a barra de título do JUCE e com o fundo da aplicação ao redor do cartão.
inline juce::DialogWindow* abrirSemBarra(juce::DialogWindow::LaunchOptions& opt) {
    const int w = opt.content->getWidth(), h = opt.content->getHeight();
    opt.useNativeTitleBar = false;
    opt.dialogBackgroundColour = paleta().fundo;
    auto* dw = opt.create();
    dw->setTitleBarHeight(0);
    dw->setContentComponentSize(w, h);
    dw->centreWithSize(dw->getWidth(), dw->getHeight());
    dw->setVisible(true);
    dw->enterModalState(true, nullptr, true);
    return dw;
}

// Envolve o conteúdo de `opt` na moldura e abre (substitui `opt.launchAsync()` nos diálogos existentes). O título do
// cabeçalho é o `dialogTitle`; a seção/trilha são opcionais.
inline juce::DialogWindow* lancar(juce::DialogWindow::LaunchOptions& opt, Secao secao = Secao::Auto, const juce::String& trilha = {}) {
    auto* conteudo = opt.content.release();
    opt.content.setOwned(new JanelaMoldura(conteudo, secao, trilha, opt.dialogTitle));
    return abrirSemBarra(opt);
}

// Botões da moldura: principal (grafite, ou vermelho se apaga) e secundário (poço com contorno).
inline void estilizarBotao(juce::TextButton& b, bool principal, bool apaga = false) {
    const auto p = paleta();
    const auto secundario = tema().botaoSecundario;  // claro: #E4DED4
    b.setColour(juce::TextButton::buttonColourId, principal ? (apaga ? p.vermelho : p.grafite) : secundario);
    b.setColour(juce::TextButton::buttonOnColourId, principal ? (apaga ? p.vermelho : p.grafite) : secundario);
    b.setColour(juce::TextButton::textColourOffId, principal ? p.branco : p.tinta);
    b.setColour(juce::TextButton::textColourOnId, principal ? p.branco : p.tinta);
}

}  // namespace matriz::ui::moldura
