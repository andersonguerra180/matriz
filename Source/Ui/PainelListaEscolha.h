#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

#include "BarraLateral.h"
#include "PainelOpcoesFiltro.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

// Lista longa de escolha no desenho do FILTER OPTIONS, no lugar do menu nativo do dropdown (que fecha ao soltar o
// botão do mouse e escolhe o que estiver embaixo dele — impreciso numa lista grande). O painel só fecha clicando fora
// ou com Esc; busca no topo quando a lista é grande; rola dentro do cartão quando não cabe.
//   Multipla: um interruptor por linha, cada troca já aplica (filtros). "Clear all" no título.
//   Unica:    clicar destaca, APPLY grava (duplo clique / Enter também), CLOSE sai sem gravar (metadados).
class PainelListaEscolha : public juce::Component, private juce::KeyListener {
public:
    enum class Modo { Multipla, Unica };

    struct Opcao {
        juce::String valor;    // o que volta no callback (token bruto)
        juce::String rotulo;   // o que aparece
        int contagem = -1;     // -1 = sem número
        bool ligado = false;   // Multipla: interruptor; Unica: o valor atual
    };

    struct Config {
        juce::String titulo, subtitulo;
        Modo modo = Modo::Multipla;
        std::vector<Opcao> opcoes;
        std::function<void(const juce::String& valor, bool ligado)> aoAlternar;  // Multipla
        std::function<void()> aoLimparTudo;                                       // Multipla
        std::function<void(const juce::String& valor)> aoAplicar;                 // Unica
    };

    explicit PainelListaEscolha(Config c, int largura) : cfg_(std::move(c)) {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        if (cfg_.modo == Modo::Unica)
            for (size_t i = 0; i < cfg_.opcoes.size(); ++i)
                if (cfg_.opcoes[i].ligado) destacado_ = static_cast<int>(i);

        if (temBusca()) {
            busca_.setTextToShowWhenEmpty(i18n::t("painel_lista.buscar"), tema().textoTerciario);
            busca_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            busca_.setColour(juce::TextEditor::textColourId, juce::Colours::black);
            busca_.setColour(juce::TextEditor::outlineColourId, tema().borda);
            busca_.setColour(juce::TextEditor::focusedOutlineColourId, tema().acento);
            busca_.setFont(juce::Font(juce::FontOptions(13.0f)));
            busca_.setIndents(8, 6);
            busca_.onTextChange = [this] { refiltrar(); };
            busca_.onEscapeKey = [this] { fechar(); };
            busca_.onReturnKey = [this] { confirmarCursor(); };
            busca_.addKeyListener(this);
            addAndMakeVisible(busca_);
        }

        lista_.dono = this;
        viewport_.setViewedComponent(&lista_, false);
        viewport_.setScrollBarsShown(true, false);
        viewport_.setScrollBarThickness(8);
        addAndMakeVisible(viewport_);

        if (cfg_.modo == Modo::Unica) {
            btnAplicar_.setButtonText(i18n::t("painel_lista.aplicar"));
            btnFechar_.setButtonText(i18n::t("painel_lista.fechar"));
            btnAplicar_.onClick = [this] { aplicarDestacado(); };
            btnFechar_.onClick = [this] { fechar(); };
            addAndMakeVisible(btnAplicar_);
            addAndMakeVisible(btnFechar_);
        }

        refiltrar();
        const int linhas = juce::jlimit(1, kMaxLinhasVisiveis, static_cast<int>(cfg_.opcoes.size()));
        setSize(juce::jmax(largura, 280), alturaFixa() + linhas * kLinha + kPadBaixo);
    }

    ~PainelListaEscolha() override { busca_.removeKeyListener(this); }

    // Abre preso à âncora (abaixo dela, sem seta), numa CaixaFlutuante (clique fora fecha e segue adiante; clicar de
    // novo na âncora fecha — aí devolve nullptr). O SafePointer serve para atualizar as opções com ele aberto.
    static juce::Component::SafePointer<PainelListaEscolha> abrir(juce::Component& ancora, Config c) {
        auto* painel = CaixaFlutuante::abrir(ancora, std::make_unique<PainelListaEscolha>(std::move(c), ancora.getWidth()),
                                             PainelOpcoesFiltro::lfCaixa());
        juce::Component::SafePointer<PainelListaEscolha> seguro(dynamic_cast<PainelListaEscolha*>(painel));
        juce::MessageManager::callAsync([seguro] {
            if (seguro == nullptr) return;
            if (seguro->temBusca()) seguro->busca_.grabKeyboardFocus();
            else seguro->grabKeyboardFocus();
        });
        return seguro;
    }

    // Opções novas com o painel aberto: mantém o texto da busca, a rolagem e (Unica) o destaque pelo valor.
    void atualizarOpcoes(std::vector<Opcao> opcoes, const juce::String& subtitulo) {
        juce::String valorDestacado;
        if (destacado_ >= 0 && destacado_ < static_cast<int>(cfg_.opcoes.size()))
            valorDestacado = cfg_.opcoes[static_cast<size_t>(destacado_)].valor;
        cfg_.opcoes = std::move(opcoes);
        cfg_.subtitulo = subtitulo;
        destacado_ = -1;
        for (size_t i = 0; i < cfg_.opcoes.size(); ++i)
            if (cfg_.opcoes[i].valor == valorDestacado) destacado_ = static_cast<int>(i);
        const auto rolagem = viewport_.getViewPosition();
        refiltrar();
        viewport_.setViewPosition(rolagem);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        auto cab = juce::Rectangle<int>(kPadX, 0, getWidth() - 2 * kPadX, kCabecalho);
        if (cfg_.modo == Modo::Multipla) {
            const auto txt = i18n::t("filtro_opcoes.limpar_tudo");
            limparTudo_ = cab.removeFromRight(juce::GlyphArrangement::getStringWidthInt(fonteLimpar(), txt) + 2);
            g.setFont(fonteLimpar());
            g.setColour(algumaLigada() ? tk.textoPrimario : tk.textoDesativado);
            g.drawText(txt, limparTudo_, juce::Justification::centredRight, false);
            g.fillRect(limparTudo_.getX(), limparTudo_.getCentreY() + 8, limparTudo_.getWidth(), 1);  // sublinhado
            cab.removeFromRight(8);
        }
        g.setColour(tk.textoPrimario);
        g.setFont(barra::fonteTitulo());
        g.drawText(cfg_.titulo, cab, juce::Justification::centredLeft, true);
        int y = kCabecalho;
        if (cfg_.subtitulo.isNotEmpty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(cfg_.subtitulo, kPadX, y - 6, getWidth() - 2 * kPadX, kSubtitulo, juce::Justification::centredLeft, true);
            y += kSubtitulo;
        }
        g.setColour(tk.borda.withAlpha(0.6f));
        g.fillRect(kPadX, y - 1, getWidth() - 2 * kPadX, 1);
    }

    void resized() override {
        int y = kCabecalho + (cfg_.subtitulo.isNotEmpty() ? kSubtitulo : 0) + 6;
        if (temBusca()) {
            busca_.setBounds(kPadX, y, getWidth() - 2 * kPadX, kBusca);
            y += kBusca + 6;
        }
        const int rodape = cfg_.modo == Modo::Unica ? kRodape : 0;
        viewport_.setBounds(kPadX - 6, y, getWidth() - 2 * kPadX + 12, getHeight() - y - rodape - kPadBaixo);
        lista_.setSize(viewport_.getMaximumVisibleWidth(), juce::jmax(kLinha, static_cast<int>(visiveis_.size()) * kLinha));
        if (cfg_.modo == Modo::Unica) {
            auto r = juce::Rectangle<int>(kPadX, getHeight() - kRodape - kPadBaixo + 8, getWidth() - 2 * kPadX, kRodape - 12);
            btnAplicar_.setBounds(r.removeFromRight(96));
            r.removeFromRight(8);
            btnFechar_.setBounds(r.removeFromRight(96));
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (cfg_.modo == Modo::Multipla && limparTudo_.expanded(4).contains(e.getPosition())) limparTudo();
    }

    bool keyPressed(const juce::KeyPress& k) override { return tratarTecla(k); }

private:
    static constexpr int kPadX = 16, kCabecalho = 40, kSubtitulo = 18, kBusca = 30, kLinha = 34, kRodape = 48,
                         kPadBaixo = 10, kMaxLinhasVisiveis = 12;

    // A lista em si, dentro do Viewport (rola quando não cabe).
    struct Lista : public juce::Component {
        PainelListaEscolha* dono = nullptr;
        int sobre = -1;  // linha sob o mouse

        void paint(juce::Graphics& g) override { dono->pintarLinhas(g); }
        void mouseMove(const juce::MouseEvent& e) override { definirSobre(e.y / kLinha); }
        void mouseExit(const juce::MouseEvent&) override { definirSobre(-1); }
        void mouseUp(const juce::MouseEvent& e) override {
            if (e.mouseWasDraggedSinceMouseDown()) return;  // soltar depois de arrastar não escolhe nada
            dono->clicarLinha(e.y / kLinha, false);
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override { dono->clicarLinha(e.y / kLinha, true); }
        void definirSobre(int i) {
            if (i == sobre) return;
            sobre = i;
            repaint();
        }
    };

    static juce::Font fonteLimpar() { return juce::Font(juce::FontOptions(12.5f, juce::Font::bold)); }

    bool temBusca() const { return cfg_.opcoes.size() > 8; }
    int alturaFixa() const {
        return kCabecalho + (cfg_.subtitulo.isNotEmpty() ? kSubtitulo : 0) + 6 + (temBusca() ? kBusca + 6 : 0) +
               (cfg_.modo == Modo::Unica ? kRodape : 0);
    }
    bool algumaLigada() const {
        for (const auto& o : cfg_.opcoes) if (o.ligado) return true;
        return false;
    }

    void refiltrar() {
        const auto termo = busca_.getText().trim();
        visiveis_.clear();
        for (size_t i = 0; i < cfg_.opcoes.size(); ++i)
            if (termo.isEmpty() || cfg_.opcoes[i].rotulo.containsIgnoreCase(termo)) visiveis_.push_back(static_cast<int>(i));
        cursor_ = visiveis_.empty() ? -1 : juce::jlimit(0, static_cast<int>(visiveis_.size()) - 1, juce::jmax(0, cursor_));
        if (termo.isEmpty() && cfg_.modo == Modo::Unica && destacado_ >= 0)
            for (size_t v = 0; v < visiveis_.size(); ++v)
                if (visiveis_[v] == destacado_) cursor_ = static_cast<int>(v);
        resized();
        lista_.repaint();
    }

    void pintarLinhas(juce::Graphics& g) {
        const auto& tk = tema();
        const int w = lista_.getWidth();
        if (visiveis_.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(13.0f)));
            g.drawText(i18n::t("painel_lista.nenhum_resultado"), 6, 0, w - 12, kLinha, juce::Justification::centredLeft, false);
            return;
        }
        for (size_t v = 0; v < visiveis_.size(); ++v) {
            const auto& o = cfg_.opcoes[static_cast<size_t>(visiveis_[v])];
            auto linha = juce::Rectangle<int>(0, static_cast<int>(v) * kLinha, w, kLinha);
            const bool destacada = cfg_.modo == Modo::Unica && visiveis_[v] == destacado_;
            if (destacada || static_cast<int>(v) == lista_.sobre || static_cast<int>(v) == cursor_) {
                g.setColour(destacada ? tk.acento.withAlpha(0.18f) : tk.painelAlt);
                g.fillRoundedRectangle(linha.reduced(2, 2).toFloat(), 7.0f);
            }
            auto r = linha.reduced(8, 0);
            if (cfg_.modo == Modo::Multipla) {
                auto chave = r.removeFromRight(36).withSizeKeepingCentre(36, 20);
                PainelOpcoesFiltro::desenharChave(g, chave.toFloat(), o.ligado);
                r.removeFromRight(10);
            } else if (destacada) {
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
                g.drawText(juce::String::fromUTF8("\xe2\x9c\x93"), r.removeFromRight(18), juce::Justification::centred, false);
                r.removeFromRight(6);
            }
            if (o.contagem >= 0) {
                const auto n = juce::String(o.contagem);
                g.setColour(tk.textoTerciario);
                g.setFont(barra::fonteContagem());
                g.drawText(n, r.removeFromRight(juce::GlyphArrangement::getStringWidthInt(barra::fonteContagem(), n) + 4),
                           juce::Justification::centredRight, false);
                r.removeFromRight(8);
            }
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(13.5f, (o.ligado || destacada) ? juce::Font::bold : juce::Font::plain)));
            g.drawText(o.rotulo, r, juce::Justification::centredLeft, true);
        }
    }

    void clicarLinha(int v, bool duplo) {
        if (v < 0 || v >= static_cast<int>(visiveis_.size())) return;
        cursor_ = v;
        const int i = visiveis_[static_cast<size_t>(v)];
        auto& o = cfg_.opcoes[static_cast<size_t>(i)];
        if (cfg_.modo == Modo::Multipla) {
            if (duplo) return;  // o duplo clique já passou por dois mouseUp: não desfaz a troca
            o.ligado = !o.ligado;
            const auto valor = o.valor;
            const bool ligado = o.ligado;
            repaint();
            if (cfg_.aoAlternar) cfg_.aoAlternar(valor, ligado);  // pode chamar atualizarOpcoes: nada de `o` depois
            return;
        }
        destacado_ = i;
        lista_.repaint();
        if (duplo) aplicarDestacado();
    }

    void limparTudo() {
        if (!algumaLigada()) return;
        for (auto& o : cfg_.opcoes) o.ligado = false;
        repaint();
        lista_.repaint();
        if (cfg_.aoLimparTudo) cfg_.aoLimparTudo();
    }

    void confirmarCursor() {
        if (cursor_ < 0 || cursor_ >= static_cast<int>(visiveis_.size())) return;
        if (cfg_.modo == Modo::Multipla) {
            clicarLinha(cursor_, false);
            return;
        }
        destacado_ = visiveis_[static_cast<size_t>(cursor_)];
        aplicarDestacado();
    }

    void aplicarDestacado() {
        if (destacado_ < 0 || destacado_ >= static_cast<int>(cfg_.opcoes.size())) return;
        const auto valor = cfg_.opcoes[static_cast<size_t>(destacado_)].valor;
        auto aplicar = cfg_.aoAplicar;
        fechar();  // assíncrono: o painel ainda existe até o fim desta chamada
        if (aplicar) aplicar(valor);
    }

    void fechar() {
        if (auto* caixa = findParentComponentOfClass<juce::CallOutBox>()) caixa->dismiss();
    }

    void moverCursor(int delta) {
        if (visiveis_.empty()) return;
        cursor_ = juce::jlimit(0, static_cast<int>(visiveis_.size()) - 1, cursor_ + delta);
        if (cfg_.modo == Modo::Unica) destacado_ = visiveis_[static_cast<size_t>(cursor_)];
        const int topo = cursor_ * kLinha;
        auto pos = viewport_.getViewPosition();
        if (topo < pos.y) viewport_.setViewPosition(pos.x, topo);
        else if (topo + kLinha > pos.y + viewport_.getMaximumVisibleHeight())
            viewport_.setViewPosition(pos.x, topo + kLinha - viewport_.getMaximumVisibleHeight());
        lista_.repaint();
    }

    bool tratarTecla(const juce::KeyPress& k) {
        if (k == juce::KeyPress::upKey) { moverCursor(-1); return true; }
        if (k == juce::KeyPress::downKey) { moverCursor(1); return true; }
        if (k == juce::KeyPress::returnKey) { confirmarCursor(); return true; }
        if (k == juce::KeyPress::escapeKey) { fechar(); return true; }
        if (k == juce::KeyPress::spaceKey && !busca_.hasKeyboardFocus(false)) {
            if (cursor_ >= 0) clicarLinha(cursor_, false);
            return true;
        }
        return false;
    }

    // Setas/Enter digitando na busca: andam/escolhem na lista em vez de mexer no texto.
    bool keyPressed(const juce::KeyPress& k, juce::Component*) override {
        if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey) return tratarTecla(k);
        return false;
    }

    Config cfg_;
    juce::TextEditor busca_;
    juce::Viewport viewport_;
    Lista lista_;
    juce::TextButton btnAplicar_, btnFechar_;
    std::vector<int> visiveis_;  // índices em cfg_.opcoes que passam pela busca
    int cursor_ = 0;             // posição em visiveis_ (teclado)
    int destacado_ = -1;         // Unica: índice em cfg_.opcoes
    juce::Rectangle<int> limparTudo_;
};

// Dropdown que, em vez do menu nativo, abre um PainelListaEscolha (aoAbrir). Mesmo desenho e tamanho do
// juce::ComboBox — o texto mostrado é posto por quem usa (setText), sem itens no combo.
class ComboDePainel : public juce::ComboBox {
public:
    std::function<void()> aoAbrir;
    void showPopup() override {
        hidePopup();  // o ComboBox marcou o menu como ativo ao clicar; sem isto o próximo clique não abriria nada
        if (aoAbrir) aoAbrir();
    }
};

} // namespace matriz::ui
