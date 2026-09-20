#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>
#include "ProjetoAberto.h"
#include "../Catalogo/CatalogSiteExport.h"

namespace matriz::ui {

class PublishHtmlDialog : public juce::Component {
public:
    explicit PublishHtmlDialog(ProjetoAberto& projeto);
    ~PublishHtmlDialog() override;

    static void exibirModal(ProjetoAberto& projeto);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void()> aoFechar;

private:
    void closeDialog();
    void escolherImagemFundo();
    void escolherImagemLogo();
    void iniciarPublicacao();

    ProjetoAberto& projeto_;

    juce::File arquivoFundo_;
    juce::File arquivoLogo_;

    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblSubtitulo_;

    // Escopo
    std::unique_ptr<juce::GroupComponent> grpEscopo_;
    std::unique_ptr<juce::Label> lblDicaPublicacao_;
    std::unique_ptr<juce::ToggleButton> rbApenasMarcadosH_;
    std::unique_ptr<juce::Component> badgeH_;
    std::unique_ptr<juce::Label> lblSufixoH_;
    std::unique_ptr<juce::ToggleButton> rbTodosAssets_;

    // Estrutura
    std::unique_ptr<juce::GroupComponent> grpEstrutura_;
    std::unique_ptr<juce::Label> lblEstruturaInfo_;
    std::unique_ptr<juce::ComboBox> comboEstrutura_;

    // Branding
    std::unique_ptr<juce::GroupComponent> grpBranding_;
    std::unique_ptr<juce::TextButton> btnEscolherFundo_;
    std::unique_ptr<juce::Label> lblCaminhoFundo_;
    std::unique_ptr<juce::TextButton> btnLimparFundo_;

    std::unique_ptr<juce::TextButton> btnEscolherLogo_;
    std::unique_ptr<juce::Label> lblCaminhoLogo_;
    std::unique_ptr<juce::TextButton> btnLimparLogo_;

    // Texto Customizado
    std::unique_ptr<juce::GroupComponent> grpTextoCustom_;
    std::unique_ptr<juce::TextEditor> editorTextoCustom_;

    // Botoes Acao
    std::unique_ptr<juce::TextButton> btnCancelar_;
    std::unique_ptr<juce::TextButton> btnPublicar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PublishHtmlDialog)
};

} // namespace matriz::ui
