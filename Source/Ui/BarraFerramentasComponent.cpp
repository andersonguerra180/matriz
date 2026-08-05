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
    campoBusca_->onTextChange = [this] { if (aoBuscar) aoBuscar(campoBusca_->getText()); };
    addAndMakeVisible(*campoBusca_);

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

void BarraFerramentasComponent::definirDetalhesAbertos(bool abertos) {
    detalhesAbertos_ = abertos;
    if (!botaoDetalhes_) return;
    const auto& tk = tema();
    botaoDetalhes_->setColour(juce::TextButton::buttonColourId, abertos ? tk.acento : tk.painelAlt);
    botaoDetalhes_->setColour(juce::TextButton::textColourOffId, abertos ? tk.textoSobreAcento : tk.textoPrimario);
    botaoDetalhes_->repaint();
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
    auto area = getLocalBounds().reduced(tk.espacoMedio, 8);

    botaoAdicionar_->setBounds(area.removeFromLeft(170));
    area.removeFromLeft(tk.espacoMedio);
    botaoNavegar_->setBounds(area.removeFromLeft(150));
    area.removeFromLeft(tk.espacoGrande);

    // Da direita pra esquerda: as ações de destino do fluxo (backup,
    // detalhes) e o controle de tamanho ficam ancorados na borda direita,
    // pra não dançarem quando a janela é redimensionada.
    botaoBackup_->setBounds(area.removeFromRight(130));
    area.removeFromRight(tk.espacoMedio);
    botaoDetalhes_->setBounds(area.removeFromRight(96));
    area.removeFromRight(tk.espacoGrande);

    constexpr int kLarguraTamanho = 34;
    botaoTamanhoG_->setBounds(area.removeFromRight(kLarguraTamanho));
    botaoTamanhoM_->setBounds(area.removeFromRight(kLarguraTamanho));
    botaoTamanhoP_->setBounds(area.removeFromRight(kLarguraTamanho));
    area.removeFromRight(tk.espacoGrande);

    // O que sobra é da busca + contagem. A busca encolhe com a janela; a
    // contagem tem largura fixa e some junto com o texto quando vazia.
    auto areaContagem = area.removeFromRight(juce::jmin(170, area.getWidth() / 3));
    labelContagem_->setBounds(areaContagem.withTrimmedLeft(tk.espacoMedio));
    campoBusca_->setBounds(area.removeFromLeft(juce::jmax(0, area.getWidth())));
}

} // namespace matriz::ui
