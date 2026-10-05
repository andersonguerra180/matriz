#include "TarefaGlobalModalDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

TarefaGlobalModalDialog::TarefaGlobalModalDialog() {
    const auto& tk = tema();

    lblTitulo_ = std::make_unique<juce::Label>();
    lblTitulo_->setFont(juce::Font(juce::FontOptions(19.0f, juce::Font::bold)));
    lblTitulo_->setJustificationType(juce::Justification::centred);
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    lblDetalhe_ = std::make_unique<juce::Label>();
    lblDetalhe_->setFont(juce::Font(juce::FontOptions(14.0f)));
    lblDetalhe_->setJustificationType(juce::Justification::centred);
    lblDetalhe_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblDetalhe_);

    lblPorcentagem_ = std::make_unique<juce::Label>();
    lblPorcentagem_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    lblPorcentagem_->setJustificationType(juce::Justification::centred);
    lblPorcentagem_->setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(*lblPorcentagem_);

    btnCancelar_ = std::make_unique<juce::TextButton>(
        matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("CANCELAR") : juce::String("CANCEL"));
    btnCancelar_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xffef4444));
    btnCancelar_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnCancelar_->onClick = [this] {
        if (estado_.ativo && estado_.id.isNotEmpty())
            ProgressoGlobal::obterInstancia().cancelarTarefa(estado_.id);
    };
    btnCancelar_->setVisible(false);
    addAndMakeVisible(*btnCancelar_);

    setSize(480, 216);
}

void TarefaGlobalModalDialog::definirEstado(const EstadoProgresso& estado) {
    estado_ = estado;
    atualizarVisual();
}

void TarefaGlobalModalDialog::atualizarVisual() {
    lblTitulo_->setText(estado_.titulo, juce::dontSendNotification);

    juce::String detalhe = estado_.detalhe;
    if (detalhe.isEmpty() && estado_.totalItens > 0) {
        detalhe = juce::String(estado_.itensConcluidos) + " / " + juce::String(estado_.totalItens);
    }
    lblDetalhe_->setText(detalhe, juce::dontSendNotification);

    if (estado_.fracao >= 0.0) {
        int pct = static_cast<int>(std::round(juce::jlimit(0.0, 1.0, estado_.fracao) * 100.0));
        lblPorcentagem_->setText(juce::String(pct) + "%", juce::dontSendNotification);
    } else {
        lblPorcentagem_->setText("", juce::dontSendNotification);
    }

    btnCancelar_->setVisible(estado_.cancelavel);
    resized();
    repaint();
}

void TarefaGlobalModalDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    const auto barArea = areaBarra().toFloat();
    g.setColour(tk.painelAlt);
    g.fillRoundedRectangle(barArea, 6.0f);
    g.setColour(tk.borda);
    g.drawRoundedRectangle(barArea, 6.0f, 1.0f);

    if (estado_.fracao >= 0.0) {
        float fillW = barArea.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, estado_.fracao));
        if (fillW > 1.0f) {
            g.setColour(tk.acento);
            g.fillRoundedRectangle(barArea.withWidth(fillW), 6.0f);
        }
    } else {
        // Indeterminado: preenche parcial e translúcido — ainda deixa claro
        // que algo está rodando, sem fingir saber quanto falta.
        g.setColour(tk.acento.withAlpha(0.45f));
        g.fillRoundedRectangle(barArea.withWidth(barArea.getWidth() * 0.35f), 6.0f);
    }
}

void TarefaGlobalModalDialog::resized() {
    if (!lblTitulo_ || !lblDetalhe_ || !lblPorcentagem_ || !btnCancelar_)
        return;

    // Tudo na mesma coluna central, margens iguais (32 px), cada bloco com a sua faixa: título, detalhe, barra, porcentagem e
    // botão nunca se sobrepõem (a barra de paint() e os rótulos usam as mesmas coordenadas).
    const int x = 32, w = getWidth() - 64;
    lblTitulo_->setBounds(x, 22, w, 28);
    lblDetalhe_->setBounds(x, 56, w, 22);
    lblPorcentagem_->setBounds(x, areaBarra().getBottom() + 6, w, 20);
    if (btnCancelar_->isVisible()) {
        const int btnW = 120, btnH = 34;
        btnCancelar_->setBounds((getWidth() - btnW) / 2, getHeight() - btnH - 20, btnW, btnH);
    }
}

juce::Rectangle<int> TarefaGlobalModalDialog::areaBarra() const {
    return {32, 96, getWidth() - 64, 22};
}

// ---------------------------------------------------------------------------

TarefaGlobalModalWatcher::TarefaGlobalModalWatcher() {
    ProgressoGlobal::obterInstancia().adicionarListener(this);
}

TarefaGlobalModalWatcher::~TarefaGlobalModalWatcher() {
    ProgressoGlobal::obterInstancia().removerListener(this);
    stopTimer();
    fecharModal();
}

void TarefaGlobalModalWatcher::aoProgressoAtualizado(const EstadoProgresso& estado) {
    if (!estado.ativo) {
        if (idPendente_ == estado.id) { idPendente_.clear(); stopTimer(); }
        if (idMostrando_.isNotEmpty() && idMostrando_ == estado.id) fecharModal();
        return;
    }

    if (estado.temModalProprio) return; // já tem modal dedicado — não duplica
    if (estado.somenteBarra) return;     // carregamento rotineiro: só a barra inferior

    if (idMostrando_.isNotEmpty() && idMostrando_ == estado.id) {
        atualizarModal(estado);
        return;
    }

    if (idPendente_ != estado.id) {
        idPendente_ = estado.id;
        // Limiar curto de propósito (ver comentário no .h): o objetivo é
        // nunca deixar o usuário se perguntando se travou, então o modal
        // some antes de qualquer travamento real virar dúvida — mas
        // operações rápidas (a maioria dos recarregar() rotineiros) nunca
        // chegam a piscar ele na tela.
        startTimer(400);
    }
}

void TarefaGlobalModalWatcher::timerCallback() {
    stopTimer();
    if (idPendente_.isEmpty()) return;

    auto estadoAtual = ProgressoGlobal::obterInstancia().obterEstado();
    if (estadoAtual.ativo && estadoAtual.id == idPendente_ && !estadoAtual.temModalProprio && !estadoAtual.somenteBarra) {
        abrirModal(estadoAtual);
    }
    idPendente_.clear();
}

void TarefaGlobalModalWatcher::abrirModal(const EstadoProgresso& estado) {
    if (janela_) return;

    auto* conteudo = new TarefaGlobalModalDialog();
    conteudo->definirEstado(estado);
    conteudo_ = conteudo;
    idMostrando_ = estado.id;

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(conteudo);
    options.dialogTitle = estado.titulo.isNotEmpty() ? estado.titulo : juce::String("Working...");
    options.dialogBackgroundColour = tema().painel;
    options.escapeKeyTriggersCloseButton = false;
    options.useNativeTitleBar = false;
    options.resizable = false;

    auto* dw = options.launchAsync();
    janela_ = dw;
    if (dw) {
        if (auto* tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
            auto area = tela->userBounds;
            dw->setCentrePosition(area.getCentreX(), area.getCentreY());
        }
    }
}

void TarefaGlobalModalWatcher::atualizarModal(const EstadoProgresso& estado) {
    if (conteudo_) conteudo_->definirEstado(estado);
    if (auto* dw = janela_.getComponent()) {
        if (estado.titulo.isNotEmpty()) dw->setName(estado.titulo);
    }
}

void TarefaGlobalModalWatcher::fecharModal() {
    idMostrando_.clear();
    conteudo_ = nullptr;
    if (!janela_) return;

    auto* dw = janela_.getComponent();
    janela_ = nullptr;
    if (!dw) return;

    dw->exitModalState(0);
    dw->setVisible(false);
    juce::Component::SafePointer<juce::DialogWindow> safeDw(dw);
    juce::MessageManager::callAsync([safeDw] {
        if (safeDw != nullptr) {
            safeDw->removeFromDesktop();
            delete safeDw.getComponent();
        }
    });
}

} // namespace matriz::ui
