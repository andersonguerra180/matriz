#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

#include "Tokens.h"

// Widgets do redesenho "tablet": botões pílula, chips com contador, chave
// segmentada, cartões com sombra e barra de título de tela. Tudo desenha a
// partir de matriz::ui::tema() a cada paint(), então trocar Claro/Escuro ao
// vivo nunca deixa uma cor antiga guardada.

namespace matriz::ui {

// Fonte do tema já multiplicada por escalaFonte().
juce::Font fonteUi(float tamanhoToken, int estilo = juce::Font::plain);

// Cartão: sombra suave (0 1px 2px + 0 8px 24px) + preenchimento + linha suave.
void desenharCartao(juce::Graphics& g, juce::Rectangle<float> area, float raio, juce::Colour preenchimento,
                    bool comSombra = true);

// Cartão cujo contorno forma uma seta para baixo (fluxo entre cartões): um único juce::Path com o corpo
// arredondado e a ponta. `area` é só o corpo; a ponta ocupa `alturaSeta` logo abaixo dele.
juce::Path caminhoCartaoComSeta(juce::Rectangle<float> area, float raio, float alturaSeta, bool comSeta);
void desenharCartaoComSeta(juce::Graphics& g, juce::Rectangle<float> area, float raio, float alturaSeta, juce::Colour preenchimento,
                           bool comSeta);
// Círculo escuro com número/letra clara (cabeçalho dos cartões em sequência).
void desenharCirculoNumero(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& texto);

// Rótulo de seção: CAIXA ALTA, 11 bold, com espaçamento de letras.
void desenharRotuloSecao(juce::Graphics& g, const juce::String& texto, juce::Rectangle<int> area,
                         juce::Colour cor, juce::Justification just = juce::Justification::centredLeft);

// Ícones vetoriais simples usados na navegação, no Home e nos nós de arquivo.
enum class IconeUi { Home, Backup, Explorer, Stats, Arquivo, Pasta, Logo, PainelEsquerdo, PainelDireito, Play, Pause, Stop, Fechar, Cadeado, Celular, Ferramentas,
    PhoneGallery, WhatsappChat, SocialMedia, StorageUnit, Documents, Photography, Sound, Videos, YouTube, Other, PastaMais,
    Web, Multimedia, Disco, CdRom, Cassete, Link };
void desenharIcone(juce::Graphics& g, IconeUi icone, juce::Rectangle<float> area, juce::Colour cor);

// ── Botão quadrado só com ícone (liga/desliga) ────────────────────────────
class BotaoIcone : public juce::Button {
public:
    BotaoIcone(IconeUi icone, const juce::String& tooltip);
    void paintButton(juce::Graphics& g, bool destacado, bool pressionado) override;
private:
    IconeUi icone_;
};

// ── Botão de transporte: ícone desenhado + texto opcional ────────────────
class BotaoIconeTexto : public juce::Button {
public:
    BotaoIconeTexto(IconeUi icone, const juce::String& texto);
    int larguraIdeal() const;
    void definirIcone(IconeUi i) { if (icone_ != i) { icone_ = i; repaint(); } }
    void paintButton(juce::Graphics& g, bool destacado, bool pressionado) override;
private:
    IconeUi icone_;
};

// ── Botão pílula ───────────────────────────────────────────────────────
class BotaoPilula : public juce::Button {
public:
    enum class Estilo { Primario, Secundario, Perigo, Fantasma, Redondo };

    BotaoPilula(const juce::String& texto, Estilo estilo = Estilo::Secundario);

    void setEstilo(Estilo e);
    Estilo estilo() const { return estilo_; }
    // Altura recomendada pelo tema (44; 48 no primário do topo).
    int alturaPadrao(bool destaqueTopo = false) const;
    int larguraIdeal() const;
    void definirCorSecao(juce::Colour c, bool usar) { corSecao_ = c; usarCorSecao_ = usar; repaint(); }

    void paintButton(juce::Graphics& g, bool destacado, bool pressionado) override;

private:
    Estilo estilo_;
    juce::Colour corSecao_;
    bool usarCorSecao_ = false;
};

// ── Chip de filtro (altura 30, raio total, contador) ──────────────────────
class ChipFiltro : public juce::Button {
public:
    ChipFiltro(const juce::String& rotulo, int contador = -1);

    void definirRotulo(const juce::String& rotulo) { rotulo_ = rotulo; repaint(); }
    void definirContador(int c) { contador_ = c; repaint(); }
    void definirAtivo(bool a) { setToggleState(a, juce::dontSendNotification); repaint(); }
    bool ativo() const { return getToggleState(); }
    void definirCorMarca(juce::Colour c, bool usar) { corMarca_ = c; usarMarca_ = usar; repaint(); }
    juce::String textoCompleto() const;
    int larguraIdeal() const;

    void paintButton(juce::Graphics& g, bool destacado, bool pressionado) override;

private:
    juce::String rotulo_;
    int contador_;
    juce::Colour corMarca_;
    bool usarMarca_ = false;
};

// ── Chave segmentada GRID/LIST ────────────────────────────────────────
class ChaveSegmentada : public juce::Component {
public:
    ChaveSegmentada(const juce::String& textoA, const juce::String& textoB);
    ~ChaveSegmentada() override;

    void definirIds(const juce::String& idA, const juce::String& idB);
    void selecionar(int indice, bool notificar);
    int selecionado() const { return indice_; }
    juce::Button& botaoA();
    juce::Button& botaoB();

    std::function<void(int)> aoMudar;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Segmento;
    std::unique_ptr<Segmento> a_, b_;
    int indice_ = 0;
};

// ── Cartão (contêiner) ────────────────────────────────────────────────
class CartaoComponent : public juce::Component {
public:
    explicit CartaoComponent(float raio = -1.0f) : raio_(raio) {}
    void definirTitulo(const juce::String& t) { titulo_ = t; repaint(); }
    void definirCorMarca(juce::Colour c, bool usar) { marca_ = c; usarMarca_ = usar; repaint(); }
    void definirSombra(bool s) { sombra_ = s; repaint(); }
    juce::Rectangle<int> areaConteudo(int margem = 16) const;
    void paint(juce::Graphics& g) override;

private:
    float raio_;
    juce::String titulo_;
    juce::Colour marca_;
    bool usarMarca_ = false;
    bool sombra_ = true;
};

// ── Barra de título de tela (altura 80) ───────────────────────────────
class BarraTituloTela : public juce::Component {
public:
    BarraTituloTela() = default;

    void definirTitulo(const juce::String& titulo, const juce::String& subtitulo);
    // Ações da direita, da mais à esquerda para a mais à direita.
    void definirAcoes(std::vector<juce::Component*> acoes);
    // Componentes na segunda linha (no lugar do subtítulo), da esquerda para a direita — ex.: pílula do backup.
    void definirExtrasSubtitulo(std::vector<juce::Component*> extras);
    // Setores à direita do título (da esquerda para a direita): cada um com um rótulo pequeno em cima e um divisor
    // fino entre eles. Em barra estreita os rótulos somem; os divisores e a ordem continuam.
    struct Setor {
        juce::String rotulo;
        std::vector<juce::Component*> comps;
    };
    void definirSetores(std::vector<Setor> setores);
    bool rotulosDeSetoresVisiveis() const { return mostrarRotulosSetores_; }
    const juce::String& titulo() const { return titulo_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String titulo_, subtitulo_;
    std::vector<juce::Component*> acoes_;
    std::vector<juce::Component*> extras_;
    std::vector<Setor> setores_;
    std::vector<juce::Rectangle<int>> rotulosSetores_;
    std::vector<int> divisoresX_;
    bool mostrarRotulosSetores_ = true;
    juce::Rectangle<int> areaTexto_;
};

// Pílula informativa ("BACKUP: nome ▾"); clicável quando aoClicar é definido.
class PilulaInfo : public juce::Component {
public:
    PilulaInfo() = default;
    void definirTexto(const juce::String& t) { texto_ = t; repaint(); }
    void definirCor(juce::Colour c) { cor_ = c; repaint(); }
    const juce::String& texto() const { return texto_; }
    int larguraIdeal() const;
    std::function<void()> aoClicar;

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    juce::String texto_;
    juce::Colour cor_;
};

// ── Abas em forma de pasta (tablet): faixa no topo da tela; a aba ativa se funde com o fundo da tela ──────────
class AbasPasta : public juce::Component {
public:
    struct Aba {
        juce::String rotulo;
        juce::String id;       // componentID do botão da aba (ex.: "tab.main", "tab.stats")
        juce::Colour cor;      // cor de seção (filete no topo da aba ativa)
    };
    AbasPasta();
    ~AbasPasta() override;

    void definirAbas(std::vector<Aba> abas);
    void selecionar(int indice, bool notificar = false);
    int selecionada() const { return selecionada_; }
    juce::Button* botao(int indice) const;
    std::function<void(int)> aoMudar;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    static constexpr int kAltura = 48;

private:
    class BotaoAba;
    std::vector<std::unique_ptr<BotaoAba>> botoes_;
    int selecionada_ = 0;
};

// Procura (a partir de `inicio`, subindo) o componente raiz da aplicação,
// identificado por componentID == "app.root"; se não houver, o último pai.
juce::Component* encontrarRaizApp(juce::Component* inicio);

// Localiza um componente por componentID dentro de `raiz` (profundidade total).
juce::Component* encontrarPorId(juce::Component* raiz, const juce::String& id);
void coletarIds(juce::Component* raiz, std::vector<juce::Component*>& out);

} // namespace matriz::ui
