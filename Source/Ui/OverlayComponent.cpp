#include "OverlayComponent.h"

#include <map>

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

juce::String PainelOverlay::CampoWidget::valor() const {
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

int PainelOverlay::alturaDoCartao() const {
    const auto& tk = tema();
    int altura = kMargem;
    altura += static_cast<int>(tk.tamanhoFonteTitulo) + tk.espacoMedio;  // título
    if (config_.mensagem.isNotEmpty()) {
        // Estimativa de quebra de linha: largura útil dividida por uma
        // largura média de caractere. Não precisa ser exata — só não pode
        // cortar texto, então arredonda pra cima.
        int larguraUtil = kLarguraCartao - 2 * kMargem;
        int charsPorLinha = juce::jmax(1, larguraUtil / 7);
        int linhas = 1 + config_.mensagem.length() / charsPorLinha;
        altura += linhas * (static_cast<int>(tk.tamanhoFonteCorpo) + 6) + tk.espacoMedio;
    }
    if (!config_.opcoes.empty()) altura += kAlturaLinha + tk.espacoMedio;
    if (config_.comCampoTexto) altura += kAlturaLinha + tk.espacoMedio;
    for (size_t i = 0; i < config_.campos.size(); ++i)
        altura += 16 + kAlturaLinha + tk.espacoPequeno;
    altura += kAlturaLinha + kMargem;
    return altura;
}

juce::Rectangle<int> PainelOverlay::areaDoCartao() const {
    int altura = juce::jmin(alturaDoCartao(), juce::jmax(120, getHeight() - 2 * kMargem));
    int largura = juce::jmin(kLarguraCartao, juce::jmax(240, getWidth() - 2 * kMargem));
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

    for (auto& cf : config_.campos) {
        auto rotulo = std::make_unique<juce::Label>(cf.id, cf.rotulo);
        rotulo->setFont(juce::Font(tema().tamanhoFonteCorpo - 1.0f));
        rotulo->setColour(juce::Label::textColourId, tema().textoSecundario);
        addAndMakeVisible(*rotulo);
        rotulosCampos_.push_back(std::move(rotulo));

        CampoWidget cw;
        cw.id = cf.id;
        if (!cf.opcoes.empty()) {
            cw.combo = std::make_unique<juce::ComboBox>();
            cw.combo->addItem("(none)", 1);
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
            addAndMakeVisible(*cw.combo);
        } else {
            cw.editor = std::make_unique<juce::TextEditor>();
            cw.editor->setText(cf.valorInicial, juce::dontSendNotification);
            cw.editor->setTextToShowWhenEmpty(cf.dica, tema().textoTerciario);
            addAndMakeVisible(*cw.editor);
        }
        camposExtras_.push_back(std::move(cw));
    }

    for (const auto& b : config_.botoes) {
        auto botao = std::make_unique<juce::TextButton>(b.rotulo);
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
    botoes_.clear();
    combo_.reset();
    campo_.reset();
    camposExtras_.clear();
    rotulosCampos_.clear();

    if (callback) callback(r);
}

void PainelOverlay::fechar() {
    if (!aberto_) return;
    aoConcluir_ = nullptr;
    aberto_ = false;
    setVisible(false);
    botoes_.clear();
    combo_.reset();
    campo_.reset();
    camposExtras_.clear();
    rotulosCampos_.clear();
}

void PainelOverlay::resized() {
    if (!aberto_) return;
    const auto& tk = tema();
    auto cartao = areaDoCartao().reduced(kMargem);

    cartao.removeFromTop(static_cast<int>(tk.tamanhoFonteTitulo) + tk.espacoMedio);
    if (config_.mensagem.isNotEmpty()) {
        int larguraUtil = cartao.getWidth();
        int charsPorLinha = juce::jmax(1, larguraUtil / 7);
        int linhas = 1 + config_.mensagem.length() / charsPorLinha;
        cartao.removeFromTop(linhas * (static_cast<int>(tk.tamanhoFonteCorpo) + 6) + tk.espacoMedio);
    }

    if (combo_) {
        combo_->setBounds(cartao.removeFromTop(kAlturaLinha));
        cartao.removeFromTop(tk.espacoMedio);
    }
    if (campo_) {
        campo_->setBounds(cartao.removeFromTop(kAlturaLinha));
        cartao.removeFromTop(tk.espacoMedio);
    }

    for (size_t i = 0; i < camposExtras_.size(); ++i) {
        if (i < rotulosCampos_.size())
            rotulosCampos_[i]->setBounds(cartao.removeFromTop(16));
        auto area = cartao.removeFromTop(kAlturaLinha);
        if (camposExtras_[i].editor)
            camposExtras_[i].editor->setBounds(area);
        else if (camposExtras_[i].combo)
            camposExtras_[i].combo->setBounds(area);
        cartao.removeFromTop(tk.espacoPequeno);
    }

    auto rodape = cartao.removeFromBottom(kAlturaLinha);
    // Da direita pra esquerda: o botão padrão fica na ponta direita, que é
    // onde a mão já está depois de ler o texto.
    for (auto it = botoes_.rbegin(); it != botoes_.rend(); ++it) {
        (*it)->setBounds(rodape.removeFromRight(120));
        rodape.removeFromRight(tk.espacoPequeno);
    }
}

void PainelOverlay::paint(juce::Graphics& g) {
    if (!aberto_) return;
    const auto& tk = tema();

    // Scrim: escurece o que está atrás sem esconder, pra o operador não
    // perder o contexto do que estava fazendo.
    g.fillAll(juce::Colours::black.withAlpha(0.55f));

    auto cartao = areaDoCartao();
    g.setColour(tk.painel);
    g.fillRoundedRectangle(cartao.toFloat(), tk.raioMedio);
    g.setColour(tk.borda);
    g.drawRoundedRectangle(cartao.toFloat(), tk.raioMedio, 1.0f);

    auto miolo = cartao.reduced(kMargem);
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(tk.tamanhoFonteTitulo, juce::Font::bold));
    g.drawText(config_.titulo, miolo.removeFromTop(static_cast<int>(tk.tamanhoFonteTitulo)),
               juce::Justification::centredLeft, true);
    miolo.removeFromTop(tk.espacoMedio);

    if (config_.mensagem.isNotEmpty()) {
        g.setColour(tk.textoSecundario);
        g.setFont(tk.tamanhoFonteCorpo);
        int charsPorLinha = juce::jmax(1, miolo.getWidth() / 7);
        int linhas = 1 + config_.mensagem.length() / charsPorLinha;
        auto areaTexto = miolo.removeFromTop(linhas * (static_cast<int>(tk.tamanhoFonteCorpo) + 6));
        g.drawFittedText(config_.mensagem, areaTexto, juce::Justification::topLeft, linhas);
    }
}

void PainelOverlay::mouseDown(const juce::MouseEvent&) {
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
    return true;  // overlay aberto engole o resto do teclado
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
