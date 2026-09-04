#include "BarraFerramentasComponent.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

BarraFerramentasComponent::BarraFerramentasComponent() {
    // "Adicionar arquivos" é a única ação primária da barra (cor de acento,
    // preenchida). Tudo o mais é secundário — se dois botões disputam a
    // atenção, nenhum dos dois é o próximo passo óbvio.
    botaoAdicionar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("barra.adicionar"));
    botaoAdicionar_->setTooltip(matriz::i18n::t("barra.adicionar_dica"));
    botaoAdicionar_->onClick = [this] { if (aoAdicionarArquivos) aoAdicionarArquivos(); };
    aplicarEstiloBotao(*botaoAdicionar_, true);
    addAndMakeVisible(*botaoAdicionar_);

    // Segunda porta de entrada (item 3): navegador estilo Finder dentro do
    // app, pra quem prefere percorrer as pastas aqui em vez de abrir o
    // Finder de verdade e arrastar. Arrastar continua funcionando igual.
    botaoNavegar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("navegador.abrir"));
    botaoNavegar_->onClick = [this] { if (aoAbrirNavegador) aoAbrirNavegador(); };
    aplicarEstiloBotao(*botaoNavegar_, false);
    addAndMakeVisible(*botaoNavegar_);

    campoBusca_ = std::make_unique<juce::TextEditor>();
    campoBusca_->setTextToShowWhenEmpty(matriz::i18n::t("barra.buscar"), tema().textoTerciario);
    campoBusca_->setColour(juce::TextEditor::backgroundColourId, tema().fundo);
    campoBusca_->setColour(juce::TextEditor::outlineColourId, tema().borda);
    campoBusca_->setColour(juce::TextEditor::focusedOutlineColourId, tema().bordaFoco);
    campoBusca_->setColour(juce::TextEditor::textColourId, tema().textoPrimario);
    campoBusca_->onTextChange = [this] {
        if (btnLimparBusca_) btnLimparBusca_->setVisible(campoBusca_->getText().isNotEmpty());
        if (aoBuscar) aoBuscar(campoBusca_->getText());
    };
    addAndMakeVisible(*campoBusca_);

    btnLimparBusca_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xc3\x97"));
    btnLimparBusca_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    btnLimparBusca_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnLimparBusca_->setVisible(false);
    btnLimparBusca_->onClick = [this] {
        campoBusca_->setText("", true);
        btnLimparBusca_->setVisible(false);
    };
    addAndMakeVisible(*btnLimparBusca_);

    labelContagem_ = std::make_unique<juce::Label>();
    labelContagem_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    labelContagem_->setColour(juce::Label::textColourId, tema().textoTerciario);
    labelContagem_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*labelContagem_);

    struct { std::unique_ptr<juce::TextButton>* destino; const char* chave; int indice; } tamanhos[] = {
        {&botaoTamanhoP_, "grade.tamanho_pequeno", 0},
        {&botaoTamanhoM_, "grade.tamanho_medio", 1},
        {&botaoTamanhoG_, "grade.tamanho_grande", 2},
    };
    for (auto& t : tamanhos) {
        *t.destino = std::make_unique<juce::TextButton>(matriz::i18n::t(t.chave));
        (*t.destino)->setTooltip(matriz::i18n::t("barra.tamanho_dica"));
        int indice = t.indice;
        (*t.destino)->onClick = [this, indice] {
            marcarTamanhoAtivo(indice);
            if (aoMudarTamanho) aoMudarTamanho(indice);
        };
        aplicarEstiloBotao(**t.destino, false);
        addAndMakeVisible(**t.destino);
    }
    marcarTamanhoAtivo(tamanhoAtivo_);

    botaoModoVisao_ = std::make_unique<juce::TextButton>(matriz::i18n::t("grade.modo_lista"));
    botaoModoVisao_->setTooltip(matriz::i18n::t("barra.modo_visao_dica"));
    botaoModoVisao_->onClick = [this] {
        modoLista_ = !modoLista_;
        const auto& tk = tema();
        botaoModoVisao_->setButtonText(matriz::i18n::t(modoLista_ ? "grade.modo_grade" : "grade.modo_lista"));
        botaoModoVisao_->setColour(juce::TextButton::buttonColourId, modoLista_ ? tk.acento : tk.painelAlt);
        botaoModoVisao_->setColour(juce::TextButton::textColourOffId, modoLista_ ? tk.textoSobreAcento : tk.textoPrimario);
        if (aoAlternarModoVisao) aoAlternarModoVisao(modoLista_);
    };
    aplicarEstiloBotao(*botaoModoVisao_, false);
    addAndMakeVisible(*botaoModoVisao_);

    botaoDetalhes_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.alternar"));
    botaoDetalhes_->setTooltip(matriz::i18n::t("barra.detalhes_dica"));
    botaoDetalhes_->onClick = [this] { if (aoAlternarDetalhes) aoAlternarDetalhes(); };
    aplicarEstiloBotao(*botaoDetalhes_, false);
    addAndMakeVisible(*botaoDetalhes_);

    botaoBackup_ = std::make_unique<juce::TextButton>(matriz::i18n::t("barra.backup"));
    botaoBackup_->setTooltip(matriz::i18n::t("barra.backup_dica"));
    botaoBackup_->onClick = [this] { if (aoFazerBackup) aoFazerBackup(); };
    aplicarEstiloBotao(*botaoBackup_, false);
    addAndMakeVisible(*botaoBackup_);

    botaoEstruturaOrigem_ = std::make_unique<juce::TextButton>("SRC TREE");
    botaoEstruturaOrigem_->setTooltip("Show/hide Source Directory Structure");
    botaoEstruturaOrigem_->onClick = [this] {
        mostrarEstruturaOrigem_ = !mostrarEstruturaOrigem_;
        const auto& tk = tema();
        botaoEstruturaOrigem_->setColour(juce::TextButton::buttonColourId, mostrarEstruturaOrigem_ ? tk.acento : tk.painelAlt);
        botaoEstruturaOrigem_->setColour(juce::TextButton::textColourOffId, mostrarEstruturaOrigem_ ? tk.textoSobreAcento : tk.textoPrimario);
        if (aoAlternarEstruturaOrigem) aoAlternarEstruturaOrigem(mostrarEstruturaOrigem_);
    };
    aplicarEstiloBotao(*botaoEstruturaOrigem_, false);
    // addChildComponent, não addAndMakeVisible: o segundo força visible=true e
    // desfaz o estado inicial escondido (só ADVANCED revela estes dois).
    addChildComponent(*botaoEstruturaOrigem_);

    botaoEstruturaBackup_ = std::make_unique<juce::TextButton>("DEST TREE");
    botaoEstruturaBackup_->setTooltip("Show/hide Backup Destination Structure");
    botaoEstruturaBackup_->onClick = [this] {
        mostrarEstruturaBackup_ = !mostrarEstruturaBackup_;
        const auto& tk = tema();
        botaoEstruturaBackup_->setColour(juce::TextButton::buttonColourId, mostrarEstruturaBackup_ ? tk.acento : tk.painelAlt);
        botaoEstruturaBackup_->setColour(juce::TextButton::textColourOffId, mostrarEstruturaBackup_ ? tk.textoSobreAcento : tk.textoPrimario);
        if (aoAlternarEstruturaBackup) aoAlternarEstruturaBackup(mostrarEstruturaBackup_);
    };
    aplicarEstiloBotao(*botaoEstruturaBackup_, false);
    addChildComponent(*botaoEstruturaBackup_);

    botaoAdvanced_ = std::make_unique<juce::TextButton>("ADVANCED");
    botaoAdvanced_->setTooltip("Show/hide advanced directory trees");
    botaoAdvanced_->onClick = [this] {
        mostrarAdvanced_ = !mostrarAdvanced_;
        const auto& tk = tema();
        botaoAdvanced_->setColour(juce::TextButton::buttonColourId, mostrarAdvanced_ ? tk.acento : tk.painelAlt);
        botaoAdvanced_->setColour(juce::TextButton::textColourOffId, mostrarAdvanced_ ? tk.textoSobreAcento : tk.textoPrimario);
        
        botaoEstruturaOrigem_->setVisible(mostrarAdvanced_);
        botaoEstruturaBackup_->setVisible(mostrarAdvanced_);
        resized();
    };
    aplicarEstiloBotao(*botaoAdvanced_, false);
    addAndMakeVisible(*botaoAdvanced_);

    // Horizontal filters
    btnAllAssets_ = std::make_unique<juce::TextButton>("ALL ASSETS");
    btnAudio_ = std::make_unique<juce::TextButton>("AUDIO");
    btnVideo_ = std::make_unique<juce::TextButton>("VIDEO");
    btnImage_ = std::make_unique<juce::TextButton>("IMAGE");
    btnDocument_ = std::make_unique<juce::TextButton>("DOCUMENT");

    std::vector<std::pair<juce::TextButton*, std::string>> horizontalFilters = {
        { btnAllAssets_.get(), "all" },
        { btnAudio_.get(), "audio" },
        { btnVideo_.get(), "video" },
        { btnImage_.get(), "image" },
        { btnDocument_.get(), "document" }
    };

    for (const auto& hf : horizontalFilters) {
        aplicarEstiloBotao(*hf.first, false);
        std::string chave = hf.second;
        hf.first->onClick = [this, chave] {
            filtroHorizontalAtivo_ = chave;
            atualizarBotoesFiltroHorizontal();
            if (aoMudarFiltroHorizontal) aoMudarFiltroHorizontal(chave);
        };
        addAndMakeVisible(*hf.first);
    }
    atualizarBotoesFiltroHorizontal();

    // Status filter buttons (ALL, ONLINE, OFFLINE)
    lblStatusFilter_ = std::make_unique<juce::Label>();
    lblStatusFilter_->setText("STATUS:", juce::dontSendNotification);
    lblStatusFilter_->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    lblStatusFilter_->setColour(juce::Label::textColourId, tema().textoTerciario);
    lblStatusFilter_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lblStatusFilter_);

    btnStatusAll_ = std::make_unique<juce::TextButton>("ALL");
    btnStatusOnline_ = std::make_unique<juce::TextButton>("ONLINE");
    btnStatusOffline_ = std::make_unique<juce::TextButton>("OFFLINE");

    std::vector<std::pair<juce::TextButton*, std::string>> statusFilters = {
        { btnStatusAll_.get(), "all" },
        { btnStatusOnline_.get(), "online" },
        { btnStatusOffline_.get(), "offline" }
    };

    for (const auto& sf : statusFilters) {
        aplicarEstiloBotao(*sf.first, false);
        std::string s = sf.second;
        sf.first->onClick = [this, s] {
            filtroStatusAtivo_ = s;
            atualizarBotoesFiltroStatus();
            if (aoMudarFiltroStatus) aoMudarFiltroStatus(s);
        };
        addAndMakeVisible(*sf.first);
    }
    atualizarBotoesFiltroStatus();

    // Mark edited items toggle
    btnDestacarEditados_ = std::make_unique<juce::TextButton>("MARK EDITED: ON");
    btnDestacarEditados_->setTooltip("Toggle zebra highlight for edited items (ON/OFF)");
    btnDestacarEditados_->onClick = [this] {
        destacarEditados_ = !destacarEditados_;
        definirDestacarEditados(destacarEditados_);
        if (aoAlternarDestacarEditados) aoAlternarDestacarEditados(destacarEditados_);
    };
    aplicarEstiloBotao(*btnDestacarEditados_, false);
    addAndMakeVisible(*btnDestacarEditados_);
    definirDestacarEditados(destacarEditados_);

    definirContagem(0, 0, false);
}

void BarraFerramentasComponent::aplicarEstiloBotao(juce::TextButton& botao, bool primario) const {
    const auto& tk = tema();
    botao.setColour(juce::TextButton::buttonColourId, primario ? tk.acento : tk.painelAlt);
    botao.setColour(juce::TextButton::buttonOnColourId, tk.acento);
    botao.setColour(juce::TextButton::textColourOffId, primario ? tk.textoSobreAcento : tk.textoPrimario);
    botao.setColour(juce::TextButton::textColourOnId, tk.textoSobreAcento);
}

void BarraFerramentasComponent::marcarTamanhoAtivo(int indice) {
    tamanhoAtivo_ = indice;
    const auto& tk = tema();
    juce::TextButton* botoes[] = {botaoTamanhoP_.get(), botaoTamanhoM_.get(), botaoTamanhoG_.get()};
    for (int i = 0; i < 3; ++i) {
        if (!botoes[i]) continue;
        bool ativo = i == indice;
        botoes[i]->setColour(juce::TextButton::buttonColourId, ativo ? tk.acento : tk.painelAlt);
        botoes[i]->setColour(juce::TextButton::textColourOffId, ativo ? tk.textoSobreAcento : tk.textoSecundario);
    }
}

void BarraFerramentasComponent::definirFiltroHorizontalAtivo(const std::string& chave) {
    filtroHorizontalAtivo_ = chave;
    atualizarBotoesFiltroHorizontal();
}

void BarraFerramentasComponent::definirFiltroStatusAtivo(const std::string& status) {
    filtroStatusAtivo_ = status;
    atualizarBotoesFiltroStatus();
}

void BarraFerramentasComponent::atualizarBotoesFiltroHorizontal() {
    const auto& tk = tema();
    std::vector<std::pair<juce::TextButton*, std::string>> horizontalFilters = {
        { btnAllAssets_.get(), "all" },
        { btnAudio_.get(), "audio" },
        { btnVideo_.get(), "video" },
        { btnImage_.get(), "image" },
        { btnDocument_.get(), "document" }
    };
    for (const auto& hf : horizontalFilters) {
        if (!hf.first) continue;
        bool ativo = (hf.second == filtroHorizontalAtivo_);
        hf.first->setColour(juce::TextButton::buttonColourId, ativo ? tk.acento : tk.painelAlt);
        hf.first->setColour(juce::TextButton::textColourOffId, ativo ? tk.textoSobreAcento : tk.textoPrimario);
    }
}

void BarraFerramentasComponent::atualizarBotoesFiltroStatus() {
    const auto& tk = tema();
    std::vector<std::pair<juce::TextButton*, std::string>> statusFilters = {
        { btnStatusAll_.get(), "all" },
        { btnStatusOnline_.get(), "online" },
        { btnStatusOffline_.get(), "offline" }
    };
    for (const auto& sf : statusFilters) {
        if (!sf.first) continue;
        bool ativo = (sf.second == filtroStatusAtivo_);
        if (sf.second == "offline" && ativo) {
            sf.first->setColour(juce::TextButton::buttonColourId, juce::Colour(0xfff97316)); // bright orange
            sf.first->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        } else {
            sf.first->setColour(juce::TextButton::buttonColourId, ativo ? tk.acento : tk.painelAlt);
            sf.first->setColour(juce::TextButton::textColourOffId, ativo ? tk.textoSobreAcento : tk.textoPrimario);
        }
    }
}

void BarraFerramentasComponent::definirDetalhesAbertos(bool abertos) {
    detalhesAbertos_ = abertos;
    if (!botaoDetalhes_) return;
    const auto& tk = tema();
    botaoDetalhes_->setColour(juce::TextButton::buttonColourId, abertos ? tk.acento : tk.painelAlt);
    botaoDetalhes_->setColour(juce::TextButton::textColourOffId, abertos ? tk.textoSobreAcento : tk.textoPrimario);
    botaoDetalhes_->repaint();
}

void BarraFerramentasComponent::definirDestacarEditados(bool ativo) {
    destacarEditados_ = ativo;
    if (!btnDestacarEditados_) return;
    const auto& tk = tema();
    btnDestacarEditados_->setButtonText(ativo ? "MARK EDITED: ON" : "MARK EDITED: OFF");
    btnDestacarEditados_->setColour(juce::TextButton::buttonColourId, ativo ? tk.acento : tk.painelAlt);
    btnDestacarEditados_->setColour(juce::TextButton::textColourOffId, ativo ? tk.textoSobreAcento : tk.textoSecundario);
    btnDestacarEditados_->repaint();
}

void BarraFerramentasComponent::definirContagem(int visiveis, int total, bool filtroAtivo) {
    if (!labelContagem_) return;
    // Sem nenhum arquivo ainda a contagem seria ruído ("0 de 0 arquivos")
    // logo abaixo da faixa que já está explicando como adicionar.
    if (total <= 0) {
        labelContagem_->setText({}, juce::dontSendNotification);
        return;
    }
    juce::String chave = filtroAtivo ? "barra.contagem_filtrada" : "barra.contagem";
    labelContagem_->setText(matriz::i18n::t(chave)
                                .replace("{visiveis}", juce::String(visiveis))
                                .replace("{total}", juce::String(total)),
                            juce::dontSendNotification);
}

void BarraFerramentasComponent::definirTextoBuscaSemNotificar(const juce::String& texto) {
    if (campoBusca_) campoBusca_->setText(texto, juce::dontSendNotification);
}

void BarraFerramentasComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);
    g.setColour(tk.borda);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void BarraFerramentasComponent::resized() {
    const auto& tk = tema();
    
    // Divide the bounds into top and bottom rows
    auto areaTop = getLocalBounds().removeFromTop(44).reduced(tk.espacoMedio, 8);
    auto areaBottom = getLocalBounds().removeFromBottom(44).reduced(tk.espacoMedio, 8);

    // --- TOP ROW ---
    botaoAdicionar_->setBounds(areaTop.removeFromLeft(140));
    areaTop.removeFromLeft(tk.espacoMedio);
    botaoBackup_->setBounds(areaTop.removeFromLeft(100));
    areaTop.removeFromLeft(tk.espacoGrande);

    // Da direita para a esquerda: View Mode, Sizes, Metadata Details, and Advanced Options
    botaoAdvanced_->setBounds(areaTop.removeFromRight(88));
    areaTop.removeFromRight(tk.espacoMedio);
    
    if (mostrarAdvanced_) {
        botaoEstruturaBackup_->setBounds(areaTop.removeFromRight(90));
        areaTop.removeFromRight(tk.espacoMedio);
        botaoEstruturaOrigem_->setBounds(areaTop.removeFromRight(90));
        areaTop.removeFromRight(tk.espacoMedio);
    }
    
    botaoDetalhes_->setBounds(areaTop.removeFromRight(84));
    areaTop.removeFromRight(tk.espacoGrande);

    constexpr int kLarguraTamanho = 34;
    botaoTamanhoG_->setBounds(areaTop.removeFromRight(kLarguraTamanho));
    botaoTamanhoM_->setBounds(areaTop.removeFromRight(kLarguraTamanho));
    botaoTamanhoP_->setBounds(areaTop.removeFromRight(kLarguraTamanho));
    areaTop.removeFromRight(tk.espacoPequeno);
    botaoModoVisao_->setBounds(areaTop.removeFromRight(44));
    areaTop.removeFromRight(tk.espacoGrande);

    // The remaining top row space is for Search and Count
    auto areaContagem = areaTop.removeFromRight(juce::jmin(150, areaTop.getWidth() / 3));
    labelContagem_->setBounds(areaContagem.withTrimmedLeft(tk.espacoMedio));
    auto buscaBox = areaTop.removeFromLeft(juce::jmax(0, areaTop.getWidth()));
    if (btnLimparBusca_) {
        auto clearBox = buscaBox.removeFromRight(24);
        btnLimparBusca_->setBounds(clearBox);
    }
    campoBusca_->setBounds(buscaBox);

    // --- BOTTOM ROW ---
    // Lay out the 5 horizontal filter buttons on the left
    int filterW = 110;
    int gap = tk.espacoMedio;
    int startX = areaBottom.getX();
    
    btnAllAssets_->setBounds(startX, areaBottom.getY(), filterW, areaBottom.getHeight()); startX += filterW + gap;
    btnAudio_->setBounds(startX, areaBottom.getY(), filterW, areaBottom.getHeight()); startX += filterW + gap;
    btnVideo_->setBounds(startX, areaBottom.getY(), filterW, areaBottom.getHeight()); startX += filterW + gap;
    btnImage_->setBounds(startX, areaBottom.getY(), filterW, areaBottom.getHeight()); startX += filterW + gap;
    btnDocument_->setBounds(startX, areaBottom.getY(), filterW, areaBottom.getHeight()); startX += filterW + gap;
    if (btnDestacarEditados_) {
        btnDestacarEditados_->setBounds(startX + tk.espacoPequeno, areaBottom.getY(), 135, areaBottom.getHeight());
    }

    // Lay out Status filter buttons on the right side
    auto statusRight = areaBottom;
    int statusBtnW = 74;
    btnStatusOffline_->setBounds(statusRight.removeFromRight(statusBtnW));
    statusRight.removeFromRight(tk.espacoPequeno);
    btnStatusOnline_->setBounds(statusRight.removeFromRight(statusBtnW));
    statusRight.removeFromRight(tk.espacoPequeno);
    btnStatusAll_->setBounds(statusRight.removeFromRight(56));
    statusRight.removeFromRight(tk.espacoPequeno);
    lblStatusFilter_->setBounds(statusRight.removeFromRight(65));
}

} // namespace matriz::ui
