#include "OverlayComponent.h"

#include <algorithm>
#include <map>

#include "../I18n/Strings.h"
#include "Tablet.h"
#include "LogosTipoArquivo.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {
// Interruptor liga/desliga (pílula) com a logo de uma marca ao lado.
class ChaveComLogo : public juce::Button {
public:
    explicit ChaveComLogo(const juce::String& logo) : juce::Button("chave"), logo_(logo.isNotEmpty() ? carregarLogoDeTipo(logo) : juce::Image()) {
        setClickingTogglesState(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void paintButton(juce::Graphics& g, bool, bool) override {
        const auto& tk = tema();
        auto b = getLocalBounds().toFloat();
        auto pilula = b.removeFromLeft(46.0f).withSizeKeepingCentre(46.0f, 24.0f);
        const bool on = getToggleState();
        g.setColour(on ? tk.acento : tk.textoTerciario.withAlpha(0.45f));
        g.fillRoundedRectangle(pilula, 12.0f);
        g.setColour(juce::Colours::white);
        const float d = 18.0f;
        g.fillEllipse(on ? pilula.getRight() - d - 3.0f : pilula.getX() + 3.0f, pilula.getCentreY() - d * 0.5f, d, d);
        b.removeFromLeft(12.0f);
        if (logo_.isValid()) {
            auto area = b.removeFromLeft(b.getHeight()).reduced(2.0f);
            g.drawImage(logo_, area, juce::RectanglePlacement::centred);
        }
    }
private:
    juce::Image logo_;
};
// Contêiner dos campos: desenha o cartão de cada grupo (ex.: GEO LOCATION) atrás dos campos.
class ContainerCampos : public juce::Component {
public:
    struct Cartao { juce::Rectangle<int> area; juce::String titulo; };
    std::vector<Cartao> cartoes;
    void paint(juce::Graphics& g) override {
        const auto p = moldura::paleta();
        for (const auto& c : cartoes) {
            g.setColour(p.janela);
            g.fillRoundedRectangle(c.area.toFloat(), 12.0f);
            g.setColour(p.linha);
            g.drawRoundedRectangle(c.area.toFloat().reduced(0.5f), 12.0f, 1.0f);
            g.setColour(p.mudo);
            g.setFont(fonteUi(tema().tamanhoFontePequena, juce::Font::bold).withExtraKerningFactor(0.10f));
            g.drawText(c.titulo.toUpperCase(), c.area.withTrimmedLeft(14).withHeight(34), juce::Justification::centredLeft, true);
        }
    }
};
}  // namespace

juce::String PainelOverlay::CampoWidget::valor() const {
    if (chave) return chave->getToggleState() ? juce::String("1") : juce::String();
    if (combo) {
        int sel = combo->getSelectedId();
        if (sel <= 1) return {};
        return combo->getText();
    }
    if (editor) return editor->getText();
    return {};
}

PainelOverlay::PainelOverlay() {
    setWantsKeyboardFocus(true);
    setVisible(false);
    setInterceptsMouseClicks(true, true);
}

// Todos os filhos são unique_ptr membros: destruir o overlay destrói os
// botões/combo/campo junto, e nenhum deles sobrevive ao pai (o padrão que o
// AlertWindow justamente NÃO tinha, já que não assumia posse do custom
// component).
PainelOverlay::~PainelOverlay() = default;

static int calcularAlturaTexto(const juce::String& texto, const juce::Font& fonte, int larguraMax) {
    if (texto.isEmpty() || larguraMax <= 20) return 0;
    juce::StringArray linhas;
    linhas.addLines(texto);
    int totalLinhas = 0;
    for (const auto& l : linhas) {
        if (l.trim().isEmpty()) {
            totalLinhas += 1;
            continue;
        }
        int w = juce::GlyphArrangement::getStringWidthInt(fonte, l);
        int quebras = std::max(1, (w + larguraMax - 1) / larguraMax);
        totalLinhas += quebras;
    }
    int alturaLinha = static_cast<int>(fonte.getHeight()) + 6;
    return totalLinhas * alturaLinha;
}

bool PainelOverlay::campoVisivel(size_t indice) const {
    if (indice >= config_.campos.size()) return true;
    const auto& cf = config_.campos[indice];
    if (cf.visivelQuandoId.isEmpty()) return true;
    for (const auto& cw : camposExtras_)
        if (cw.id == cf.visivelQuandoId) return cw.valor() == cf.visivelQuandoValor;
    return true;
}

struct PainelOverlay::PlanoCampos {
    int altura = 0;
    struct Pos { size_t i = 0; juce::Rectangle<int> rotulo, campo; };
    std::vector<Pos> campos;
    struct Cartao { juce::Rectangle<int> area; juce::String titulo; juce::Rectangle<int> botao; };
    std::vector<Cartao> cartoes;
};

// Posição de cada campo: soltos ocupam a largura toda; campos de um mesmo grupo ficam num cartão, dois por linha.
PainelOverlay::PlanoCampos PainelOverlay::planejarCampos(int largura) const {
    PlanoCampos plano;
    const int gap = tema().espacoMedio;
    int y = 0;
    size_t i = 0;
    while (i < config_.campos.size()) {
        if (!campoVisivel(i)) { ++i; continue; }
        const auto& grupo = config_.campos[i].grupo;
        if (grupo.isEmpty()) {
            const int altura = kAlturaLinha + (juce::jmax(1, config_.campos[i].linhas) - 1) * 20;
            plano.campos.push_back({ i, { 0, y, largura, 18 }, { 0, y + 18, largura, altura } });
            y += 18 + altura + gap;
            ++i;
            continue;
        }
        constexpr int pad = 14, titulo = 34;
        const int topo = y;
        y += titulo;
        const int colW = (largura - 2 * pad - 10) / 2;
        int coluna = 0;
        while (i < config_.campos.size() && (config_.campos[i].grupo == grupo || !campoVisivel(i))) {
            if (campoVisivel(i)) {
                const int x = pad + coluna * (colW + 10);
                plano.campos.push_back({ i, { x, y, colW, 18 }, { x, y + 18, colW, kAlturaLinha } });
                if (++coluna == 2) { coluna = 0; y += 18 + kAlturaLinha + 8; }
            }
            ++i;
        }
        if (coluna != 0) y += 18 + kAlturaLinha + 8;
        y += 6;
        plano.cartoes.push_back({ { 0, topo, largura, y - topo }, grupo, { largura - pad - 150, topo + 5, 150, 26 } });
        y += gap;
    }
    plano.altura = y;
    return plano;
}

int PainelOverlay::alturaCampos() const { return planejarCampos(400).altura; }

// Um dropdown ou interruptor mudou: campos condicionais aparecem/somem e o cartão se ajusta.
void PainelOverlay::aoMudarCampo() {
    for (size_t i = 0; i < config_.campos.size() && i < camposExtras_.size(); ++i) {
        const auto& cf = config_.campos[i];
        if (cf.desabilitaCampoId.isNotEmpty() && camposExtras_[i].chave) {
            const bool ligado = camposExtras_[i].chave->getToggleState();
            for (auto& alvo : camposExtras_)
                if (alvo.id == cf.desabilitaCampoId && alvo.editor) alvo.editor->setEnabled(!ligado);
        }
    }
    resized();
    repaint();
}

int PainelOverlay::alturaDoCartao() const {
    const auto& tk = tema();
    int altura = moldura::kAlturaCabecalho + (temRodape() ? moldura::kAlturaRodape : 0) + 2 * moldura::kMargemCorpo;
    if (config_.mensagem.isNotEmpty()) {
        const int larguraUtil = larguraDoCartao() - 2 * moldura::kMargemCorpo;
        altura += calcularAlturaTexto(config_.mensagem, fonteUi(14.0f), larguraUtil) + tk.espacoGrande;
    }
    if (!config_.opcoes.empty()) altura += kAlturaLinha + tk.espacoMedio;
    if (config_.comCampoTexto) altura += kAlturaLinha + tk.espacoMedio;
    altura += alturaCampos();
    if (config_.conteudo) altura += config_.alturaConteudo + tk.espacoMedio;
    return altura;
}

juce::Rectangle<int> PainelOverlay::areaDoCartao() const {
    int altura = juce::jmin(alturaDoCartao(), juce::jmax(120, getHeight() - 2 * kMargem));
    int largura = juce::jmin(larguraDoCartao(), juce::jmax(240, getWidth() - 2 * kMargem));
    return juce::Rectangle<int>(largura, altura).withCentre(getLocalBounds().getCentre());
}

void PainelOverlay::mostrar(Config config, std::function<void(Resultado)> aoConcluir) {
    // Um overlay por vez: o que estiver aberto é cancelado antes, pra nunca
    // haver dois callbacks pendentes disputando o mesmo estado.
    if (aberto_) fechar();

    config_ = std::move(config);
    aoConcluir_ = std::move(aoConcluir);

    botoes_.clear();
    combo_.reset();
    campo_.reset();
    camposExtras_.clear();
    rotulosCampos_.clear();

    if (!config_.opcoes.empty()) {
        combo_ = std::make_unique<juce::ComboBox>();
        std::map<int, juce::String> cabMap;
        for (auto& [idx, txt] : config_.cabecalhos) cabMap[idx] = txt;
        for (int i = 0; i < static_cast<int>(config_.opcoes.size()); ++i) {
            auto it = cabMap.find(i);
            if (it != cabMap.end()) combo_->addSectionHeading(it->second);
            combo_->addItem(config_.opcoes[static_cast<size_t>(i)], i + 1);
        }
        combo_->setSelectedId(juce::jlimit(0, static_cast<int>(config_.opcoes.size()) - 1,
                                            config_.opcaoSelecionada) + 1,
                              juce::dontSendNotification);
        addAndMakeVisible(*combo_);
    }

    if (config_.comCampoTexto) {
        campo_ = std::make_unique<juce::TextEditor>();
        campo_->setText(config_.textoInicial, juce::dontSendNotification);
        campo_->setTextToShowWhenEmpty(config_.dicaCampoTexto, tema().textoTerciario);
        addAndMakeVisible(*campo_);
    }

    if (!config_.campos.empty()) {
        containerCampos_ = std::make_unique<ContainerCampos>();
        viewportCampos_ = std::make_unique<juce::Viewport>();
        viewportCampos_->setViewedComponent(containerCampos_.get(), false);
        viewportCampos_->setScrollBarsShown(true, false);
        addAndMakeVisible(*viewportCampos_);
    }

    juce::Component::SafePointer<PainelOverlay> seguroCampo(this);
    for (auto& cf : config_.campos) {
        auto rotulo = std::make_unique<juce::Label>(cf.id, cf.rotulo);
        rotulo->setFont(juce::Font(tema().tamanhoFonteCorpo - 1.0f));
        rotulo->setColour(juce::Label::textColourId, tema().textoSecundario);
        if (containerCampos_) containerCampos_->addAndMakeVisible(*rotulo);
        else addAndMakeVisible(*rotulo);
        rotulosCampos_.push_back(std::move(rotulo));

        CampoWidget cw;
        cw.id = cf.id;
        if (cf.interruptor) {
            cw.chave = std::make_unique<ChaveComLogo>(cf.logoInterruptor);
            cw.chave->setToggleState(cf.valorInicial == "1", juce::dontSendNotification);
            cw.chave->onClick = [seguroCampo] { if (seguroCampo != nullptr) seguroCampo->aoMudarCampo(); };
            if (containerCampos_) containerCampos_->addAndMakeVisible(*cw.chave);
            else addAndMakeVisible(*cw.chave);
        } else if (!cf.opcoes.empty()) {
            cw.combo = std::make_unique<juce::ComboBox>();
            cw.combo->addItem(matriz::i18n::t("light.ui.imp.none"), 1);
            for (int i = 0; i < static_cast<int>(cf.opcoes.size()); ++i)
                cw.combo->addItem(cf.opcoes[static_cast<size_t>(i)], i + 2);
            if (cf.valorInicial.isNotEmpty()) {
                for (int i = 0; i < static_cast<int>(cf.opcoes.size()); ++i) {
                    if (cf.opcoes[static_cast<size_t>(i)] == cf.valorInicial) {
                        cw.combo->setSelectedId(i + 2, juce::dontSendNotification);
                        break;
                    }
                }
            } else {
                cw.combo->setSelectedId(1, juce::dontSendNotification);
            }
            cw.combo->onChange = [seguroCampo] { if (seguroCampo != nullptr) seguroCampo->aoMudarCampo(); };
            if (containerCampos_) containerCampos_->addAndMakeVisible(*cw.combo);
            else addAndMakeVisible(*cw.combo);
        } else {
            cw.editor = std::make_unique<juce::TextEditor>();
            if (cf.linhas > 1) {
                cw.editor->setMultiLine(true, true);
                cw.editor->setReturnKeyStartsNewLine(true);
            }
            cw.editor->setText(cf.valorInicial, juce::dontSendNotification);
            cw.editor->setTextToShowWhenEmpty(cf.dica, tema().textoTerciario);
            if (containerCampos_) containerCampos_->addAndMakeVisible(*cw.editor);
            else addAndMakeVisible(*cw.editor);
        }
        camposExtras_.push_back(std::move(cw));
    }

    botoesGrupo_.clear();
    for (const auto& bg : config_.botoesGrupo) {
        auto b = std::make_unique<juce::TextButton>(bg.rotulo);
        moldura::estilizarBotao(*b, false);
        b->setComponentID("grupo." + bg.grupo);
        auto aoClicar = bg.aoClicar;
        juce::Component::SafePointer<PainelOverlay> dono(this);
        b->onClick = [dono, aoClicar] { if (dono != nullptr && aoClicar) aoClicar(*dono); };
        if (containerCampos_) containerCampos_->addAndMakeVisible(*b);
        botoesGrupo_.push_back(std::move(b));
    }

    for (size_t i = 0; i < config_.campos.size() && i < camposExtras_.size(); ++i) {
        const auto& cf = config_.campos[i];
        if (cf.desabilitaCampoId.isNotEmpty() && camposExtras_[i].chave && camposExtras_[i].chave->getToggleState())
            for (auto& alvo : camposExtras_)
                if (alvo.id == cf.desabilitaCampoId && alvo.editor) alvo.editor->setEnabled(false);
    }

    for (const auto& b : config_.botoes) {
        auto botao = std::make_unique<juce::TextButton>(b.rotulo);
        moldura::estilizarBotao(*botao, b.ehPadrao, b.ehPadrao && config_.tom == Tom::Perigo);
        int id = b.id;
        // SafePointer: se o overlay morrer entre o clique e o despacho, o
        // callback não toca em memória liberada.
        juce::Component::SafePointer<PainelOverlay> safeThis(this);
        botao->onClick = [safeThis, id]() {
            if (safeThis) safeThis->concluir(id);
        };
        addAndMakeVisible(*botao);
        botoes_.push_back(std::move(botao));
    }

    if (config_.conteudo) addAndMakeVisible(*config_.conteudo);

    aberto_ = true;
    setVisible(true);
    toFront(true);
    resized();
    grabKeyboardFocus();
    if (campo_) campo_->grabKeyboardFocus();
    else if (!camposExtras_.empty() && camposExtras_.front().editor) camposExtras_.front().editor->grabKeyboardFocus();
    repaint();
}

void PainelOverlay::concluir(int botaoId) {
    if (!aberto_) return;

    Resultado r;
    r.botaoId = botaoId;
    r.opcaoSelecionada = combo_ ? combo_->getSelectedId() - 1 : -1;
    r.texto = campo_ ? campo_->getText() : juce::String();
    for (auto& cw : camposExtras_)
        r.valores[cw.id] = cw.valor();

    auto callback = std::move(aoConcluir_);
    aoConcluir_ = nullptr;
    aberto_ = false;
    setVisible(false);
    soltarConteudo();
    botoes_.clear();
    combo_.reset();
    campo_.reset();
    camposExtras_.clear();
    rotulosCampos_.clear();
    botoesGrupo_.clear();
    viewportCampos_.reset();
    containerCampos_.reset();

    if (callback) callback(r);
}

void PainelOverlay::fechar() {
    if (!aberto_) return;
    aoConcluir_ = nullptr;
    aberto_ = false;
    setVisible(false);
    soltarConteudo();
    botoes_.clear();
    combo_.reset();
    campo_.reset();
    camposExtras_.clear();
    rotulosCampos_.clear();
    botoesGrupo_.clear();
    viewportCampos_.reset();
    containerCampos_.reset();
}

void PainelOverlay::soltarConteudo() {
    if (!config_.conteudo) return;
    removeChildComponent(config_.conteudo.get());
    // Pode estar sendo chamado de dentro de um clique do próprio conteúdo: só destrói depois.
    auto guardar = std::move(config_.conteudo);
    config_.conteudo.reset();
    config_.aoTecla = nullptr;
    juce::MessageManager::callAsync([guardar] {});
}

void PainelOverlay::resized() {
    if (!aberto_) return;
    const auto& tk = tema();
    const auto geo = moldura::geometria(areaDoCartao(), temRodape());
    auto corpo = geo.corpo.reduced(moldura::kMargemCorpo);

    if (config_.mensagem.isNotEmpty()) {
        const int textoH = calcularAlturaTexto(config_.mensagem, fonteUi(14.0f), corpo.getWidth());
        corpo.removeFromTop(textoH + tk.espacoGrande);
    }
    if (config_.conteudo) {
        auto areaConteudo = corpo;
        areaConteudo.setHeight(juce::jmax(config_.alturaConteudo, corpo.getHeight()));
        config_.conteudo->setBounds(areaConteudo);
    }
    if (combo_) {
        combo_->setBounds(corpo.removeFromTop(kAlturaLinha));
        corpo.removeFromTop(tk.espacoMedio);
    }
    if (campo_) {
        campo_->setBounds(corpo.removeFromTop(kAlturaLinha));
        corpo.removeFromTop(tk.espacoMedio);
    }
    const auto ajustarWidgets = [&](size_t i, juce::Rectangle<int> rotulo, juce::Rectangle<int> area, bool visivel) {
        if (i < rotulosCampos_.size()) { rotulosCampos_[i]->setVisible(visivel); rotulosCampos_[i]->setBounds(rotulo); }
        if (camposExtras_[i].editor) { camposExtras_[i].editor->setVisible(visivel); camposExtras_[i].editor->setBounds(area); }
        else if (camposExtras_[i].combo) { camposExtras_[i].combo->setVisible(visivel); camposExtras_[i].combo->setBounds(area); }
        else if (camposExtras_[i].chave) { camposExtras_[i].chave->setVisible(visivel); camposExtras_[i].chave->setBounds(area.withWidth(visivel ? 220 : 0)); }
    };
    if (viewportCampos_ && containerCampos_) {
        const int alturaTotal = alturaCampos();
        const bool scrollVisivel = alturaTotal > corpo.getHeight();
        const int larguraUtil = corpo.getWidth() - (scrollVisivel ? 14 : 0);

        const auto plano = planejarCampos(larguraUtil);
        containerCampos_->setSize(larguraUtil, plano.altura);
        for (size_t i = 0; i < camposExtras_.size(); ++i) ajustarWidgets(i, {}, {}, false);
        for (const auto& pc : plano.campos) ajustarWidgets(pc.i, pc.rotulo, pc.campo, true);
        auto* cont = static_cast<ContainerCampos*>(containerCampos_.get());
        cont->cartoes.clear();
        for (auto& b : botoesGrupo_) b->setVisible(false);
        for (const auto& c : plano.cartoes) {
            cont->cartoes.push_back({ c.area, c.titulo });
            for (auto& b : botoesGrupo_)
                if (b->getComponentID() == "grupo." + c.titulo) { b->setBounds(c.botao); b->setVisible(true); }
        }
        cont->repaint();
        viewportCampos_->setBounds(corpo);
    }

    // Rodapé: a ação principal na ponta direita, as outras à esquerda dela.
    auto rodape = geo.rodape.reduced(moldura::kMargemCorpo, 0).withSizeKeepingCentre(geo.rodape.getWidth() - 2 * moldura::kMargemCorpo, 44);
    for (auto it = botoes_.rbegin(); it != botoes_.rend(); ++it) {
        const int strW = juce::GlyphArrangement::getStringWidthInt(fonteUi(tk.tamanhoFonteCorpo, juce::Font::bold), (*it)->getButtonText());
        (*it)->setBounds(rodape.removeFromRight(juce::jmax(120, strW + 48)));
        rodape.removeFromRight(10);
    }
}

void PainelOverlay::paint(juce::Graphics& g) {
    if (!aberto_) return;
    const auto& tk = tema();
    // Scrim: escurece o que está atrás sem esconder, pra o operador não perder o contexto do que estava fazendo.
    g.fillAll(tk.scrim);

    const auto geo = moldura::geometria(areaDoCartao(), temRodape());
    moldura::desenhar(g, geo, config_.secao, config_.trilha, config_.titulo, temRodape(), config_.progresso, config_.contador);

    auto corpo = geo.corpo.reduced(moldura::kMargemCorpo);
    if (config_.mensagem.isNotEmpty()) {
        g.setColour(moldura::paleta().tinta);
        const auto fonte = fonteUi(14.0f);
        g.setFont(fonte);
        const int textoH = calcularAlturaTexto(config_.mensagem, fonte, corpo.getWidth());
        g.drawFittedText(config_.mensagem, corpo.removeFromTop(textoH), juce::Justification::topLeft, 20);
    }
    if (config_.dicaRodape.isNotEmpty() && temRodape()) {
        g.setColour(moldura::paleta().mudo);
        g.setFont(fonteUi(tk.tamanhoFontePequena));
        g.drawText(config_.dicaRodape, geo.rodape.withTrimmedLeft(moldura::kMargemCorpo).withTrimmedRight(moldura::kMargemCorpo + 280),
                   juce::Justification::centredLeft, true);
    }
}

void PainelOverlay::mouseDown(const juce::MouseEvent& e) {
    if (aberto_ && moldura::geometria(areaDoCartao(), temRodape()).botaoFechar.contains(e.getPosition())) {
        keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));  // o X faz o mesmo que Esc
        return;
    }
    // Come o clique: enquanto o overlay está aberto, nada atrás dele responde.
    // Clicar no scrim NÃO fecha — fechar sem escolher precisa ser deliberado
    // (Escape ou o botão de cancelar), senão um clique errado descarta o que
    // o operador ia responder.
}

bool PainelOverlay::keyPressed(const juce::KeyPress& tecla) {
    if (!aberto_) return false;

    for (size_t i = 0; i < config_.botoes.size(); ++i) {
        const auto& b = config_.botoes[i];
        if (b.ehPadrao && tecla == juce::KeyPress::returnKey) {
            concluir(b.id);
            return true;
        }
        if (b.ehCancelar && tecla == juce::KeyPress::escapeKey) {
            concluir(b.id);
            return true;
        }
    }
    if (config_.aoTecla && config_.aoTecla(tecla)) return true;
    return true;  // overlay aberto engole o resto do teclado
}

void PainelOverlay::definirValorCampo(const juce::String& id, const juce::String& valor) {
    for (auto& cw : camposExtras_)
        if (cw.id == id && cw.editor) cw.editor->setText(valor, juce::dontSendNotification);
}

void PainelOverlay::acionarBotaoParaTeste(int indice) {
    if (indice < 0 || indice >= static_cast<int>(config_.botoes.size())) return;
    concluir(config_.botoes[static_cast<size_t>(indice)].id);
}

void PainelOverlay::selecionarOpcaoParaTeste(int indice) {
    if (combo_) combo_->setSelectedId(indice + 1, juce::dontSendNotification);
}

void PainelOverlay::definirTextoParaTeste(const juce::String& texto) {
    if (campo_) campo_->setText(texto, juce::dontSendNotification);
}

} // namespace matriz::ui
