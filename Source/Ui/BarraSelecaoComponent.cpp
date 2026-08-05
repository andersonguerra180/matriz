#include "BarraSelecaoComponent.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

BarraSelecaoComponent::BarraSelecaoComponent() {
    const auto& tk = tema();

    // "Editar em lote" é a ação primária: é ela que justifica a barra
    // existir. As outras duas são utilitárias e ficam com o estilo neutro.
    botaoEditarLote_ = std::make_unique<juce::TextButton>(matriz::i18n::t("selecao.editar_lote"));
    botaoEditarLote_->setColour(juce::TextButton::buttonColourId, tk.textoSobreAcento);
    botaoEditarLote_->setColour(juce::TextButton::textColourOffId, tk.acento);
    botaoEditarLote_->onClick = [this] { if (aoEditarEmLote) aoEditarEmLote(); };
    addAndMakeVisible(*botaoEditarLote_);

    botaoSelecionarTodos_ = std::make_unique<juce::TextButton>(matriz::i18n::t("selecao.selecionar_todos"));
    botaoSelecionarTodos_->setColour(juce::TextButton::buttonColourId, tk.acento.brighter(0.25f));
    botaoSelecionarTodos_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    botaoSelecionarTodos_->onClick = [this] { if (aoSelecionarTodos) aoSelecionarTodos(); };
    addAndMakeVisible(*botaoSelecionarTodos_);

    botaoLimpar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("selecao.limpar"));
    botaoLimpar_->setColour(juce::TextButton::buttonColourId, tk.acento.brighter(0.25f));
    botaoLimpar_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    botaoLimpar_->onClick = [this] { if (aoLimparSelecao) aoLimparSelecao(); };
    addAndMakeVisible(*botaoLimpar_);

    // Legenda inicial coerente com quantidade_ = 0. definirQuantidade()
    // ignora chamadas que não mudam o valor, então sem isto o texto ficaria
    // vazio até a primeira seleção real — invisível em uso normal (a barra
    // nasce escondida), mas o renderizador de snapshot do self-test desenha
    // os filhos mesmo escondidos, e saía uma barra azul sem legenda nenhuma.
    texto_ = matriz::i18n::t("selecao.contagem").replace("{n}", "0");
    setVisible(false);
}

void BarraSelecaoComponent::definirQuantidade(int quantidade) {
    if (quantidade_ == quantidade) return;
    quantidade_ = quantidade;
    // Também escreve o texto pra quantidade 0 (barra escondida): o
    // renderizador de snapshot do self-test de UI desenha os filhos
    // independentemente de visibilidade, e uma barra azul sem legenda
    // nenhuma no snapshot parece defeito.
    texto_ = quantidade == 1 ? matriz::i18n::t("selecao.contagem_um")
                             : matriz::i18n::t("selecao.contagem").replace("{n}", juce::String(quantidade));
    setVisible(quantidade > 0);
    repaint();
}

void BarraSelecaoComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto area = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(tk.acento);
    g.fillRoundedRectangle(area, tk.raioMedio);

    auto miolo = getLocalBounds().reduced(tk.espacoGrande, 0);
    miolo.removeFromRight(340); // espaço dos três botões
    g.setColour(tk.textoSobreAcento);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    g.drawText(texto_, miolo, juce::Justification::centredLeft, true);
}

void BarraSelecaoComponent::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds().reduced(tk.espacoMedio, 7);
    botaoLimpar_->setBounds(area.removeFromRight(120));
    area.removeFromRight(tk.espacoPequeno);
    botaoSelecionarTodos_->setBounds(area.removeFromRight(120));
    area.removeFromRight(tk.espacoPequeno);
    botaoEditarLote_->setBounds(area.removeFromRight(120));
}

} // namespace matriz::ui
