#include "BarraGuiaComponent.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

BarraGuiaComponent::BarraGuiaComponent() {
    botaoAlternar_ = std::make_unique<juce::TextButton>();
    botaoAlternar_->onClick = [this] { definirColapsada(!colapsada_); };
    botaoAlternar_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    botaoAlternar_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    addAndMakeVisible(*botaoAlternar_);
    atualizarTextos();
}

void BarraGuiaComponent::definirEtapa(Etapa etapa, int quantidade) {
    if (etapa_ == etapa && quantidade_ == quantidade) return;
    etapa_ = etapa;
    quantidade_ = quantidade;
    atualizarTextos();
    repaint();
}

void BarraGuiaComponent::definirColapsada(bool colapsada) {
    if (colapsada_ == colapsada) return;
    colapsada_ = colapsada;
    atualizarTextos();
    if (aoMudarAltura) aoMudarAltura();
    resized();
    repaint();
}

void BarraGuiaComponent::atualizarTextos() {
    switch (etapa_) {
        case Etapa::Classificar:
            titulo_ = matriz::i18n::t("guia.classificar_titulo").replace("{n}", juce::String(quantidade_));
            corpo_ = matriz::i18n::t("guia.classificar_corpo");
            break;
        case Etapa::Organizar:
            titulo_ = matriz::i18n::t("guia.organizar_titulo");
            corpo_ = matriz::i18n::t("guia.organizar_corpo");
            break;
        case Etapa::Backup:
            titulo_ = matriz::i18n::t("guia.backup_titulo");
            corpo_ = matriz::i18n::t("guia.backup_corpo");
            break;
        case Etapa::Vazio:
        default:
            titulo_ = matriz::i18n::t("guia.vazio_titulo");
            corpo_ = matriz::i18n::t("guia.vazio_corpo");
            break;
    }
    if (botaoAlternar_)
        botaoAlternar_->setButtonText(matriz::i18n::t(colapsada_ ? "guia.mostrar" : "guia.ocultar"));
}

void BarraGuiaComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();

    // Fundo tingido de acento em vez de painel neutro: a faixa precisa se
    // distinguir da cromia do resto da janela pra ser lida como "o app
    // está falando comigo", não como mais um painel de dados.
    g.setColour(tk.acento.withAlpha(0.10f));
    g.fillRect(getLocalBounds());
    g.setColour(tk.acento);
    g.fillRect(0, 0, 3, getHeight()); // faixa vertical de acento na borda esquerda
    g.setColour(tk.borda);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    auto miolo = getLocalBounds().withTrimmedLeft(tk.espacoGrande).reduced(tk.espacoMedio, 0);
    miolo.removeFromRight(110); // espaço do botão Entendi/Dica

    if (colapsada_) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText(titulo_, miolo, juce::Justification::centredLeft, true);
        return;
    }

    miolo.reduce(0, 8);
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    g.drawText(titulo_, miolo.removeFromTop(20), juce::Justification::centredLeft, true);

    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    g.drawFittedText(corpo_, miolo, juce::Justification::topLeft, 2);
}

void BarraGuiaComponent::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds().reduced(tk.espacoMedio, colapsada_ ? 3 : 8);
    botaoAlternar_->setBounds(area.removeFromRight(96).removeFromTop(colapsada_ ? 20 : 24));
}

} // namespace matriz::ui
