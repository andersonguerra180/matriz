#include "MainComponent.h"

#include "../I18n/Strings.h"
#include "FichaPanelComponent.h"
#include "MosaicoComponent.h"
#include "Tokens.h"

namespace matriz::ui {

MainComponent::MainComponent() { reconstruirTelaInicial(); }

MainComponent::~MainComponent() = default;

void MainComponent::reconstruirTelaInicial() {
    mosaicoViewport_.reset();
    mosaico_.reset();
    visualizadorPlaceholder_.reset();
    fichaPanel_.reset();

    telaInicialTitulo_ = std::make_unique<juce::Label>();
    telaInicialTitulo_->setText(matriz::i18n::t("tela_inicial.titulo"), juce::dontSendNotification);
    telaInicialTitulo_->setJustificationType(juce::Justification::centred);
    telaInicialTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteTitulo, juce::Font::bold)));
    telaInicialTitulo_->setColour(juce::Label::textColourId, tema().textoPrimario);
    addAndMakeVisible(*telaInicialTitulo_);

    telaInicialSubtitulo_ = std::make_unique<juce::Label>();
    telaInicialSubtitulo_->setText(matriz::i18n::t("tela_inicial.subtitulo"), juce::dontSendNotification);
    telaInicialSubtitulo_->setJustificationType(juce::Justification::centred);
    telaInicialSubtitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    telaInicialSubtitulo_->setColour(juce::Label::textColourId, tema().textoSecundario);
    addAndMakeVisible(*telaInicialSubtitulo_);

    telaInicialBotaoNovo_ = std::make_unique<juce::TextButton>(matriz::i18n::t("tela_inicial.botao_novo"));
    telaInicialBotaoNovo_->onClick = [this] { if (aoPedirNovoProjeto) aoPedirNovoProjeto(); };
    addAndMakeVisible(*telaInicialBotaoNovo_);

    telaInicialBotaoAbrir_ = std::make_unique<juce::TextButton>(matriz::i18n::t("tela_inicial.botao_abrir"));
    telaInicialBotaoAbrir_->onClick = [this] { if (aoPedirAbrirProjeto) aoPedirAbrirProjeto(); };
    addAndMakeVisible(*telaInicialBotaoAbrir_);

    resized();
    repaint();
}

void MainComponent::reconstruirLayoutProjeto() {
    telaInicialTitulo_.reset();
    telaInicialSubtitulo_.reset();
    telaInicialBotaoNovo_.reset();
    telaInicialBotaoAbrir_.reset();

    mosaico_ = std::make_unique<MosaicoComponent>(*projetoAberto_);
    mosaico_->aoSelecionar = [this](const std::string& itemId) { selecionarItem(itemId); };

    mosaicoViewport_ = std::make_unique<juce::Viewport>();
    mosaicoViewport_->setViewedComponent(mosaico_.get(), false);
    addAndMakeVisible(*mosaicoViewport_);

    // Visualizador (vídeo/imagem/vazio) + transporte + tira de diagnóstico
    // são B.1.4/B.1.5 — fronteira de etapa. Por enquanto, área reservada e
    // vazia no layout (§11.1 já define o espaço; o conteúdo vem depois).
    visualizadorPlaceholder_ = std::make_unique<juce::Component>();
    addAndMakeVisible(*visualizadorPlaceholder_);

    fichaPanel_ = std::make_unique<FichaPanelComponent>(*projetoAberto_);
    addAndMakeVisible(*fichaPanel_);

    mosaico_->recarregar();

    resized();
    repaint();
}

void MainComponent::abrirProjeto(std::unique_ptr<matriz::model::Project> projeto) {
    projetoAberto_ = std::make_unique<ProjetoAberto>(std::move(projeto));
    reconstruirLayoutProjeto();
}

void MainComponent::fecharProjeto() {
    projetoAberto_.reset();
    reconstruirTelaInicial();
}

void MainComponent::selecionarItem(const std::string& itemId) {
    if (fichaPanel_) fichaPanel_->mostrarItem(itemId);
}

void MainComponent::paint(juce::Graphics& g) { g.fillAll(tema().fundo); }

void MainComponent::resized() {
    auto area = getLocalBounds();

    if (!temProjetoAberto()) {
        auto centro = area.withSizeKeepingCentre(360, 180);
        telaInicialTitulo_->setBounds(centro.removeFromTop(32));
        centro.removeFromTop(tema().espacoMedio);
        telaInicialSubtitulo_->setBounds(centro.removeFromTop(40));
        centro.removeFromTop(tema().espacoGrande);
        auto botoes = centro.removeFromTop(32);
        telaInicialBotaoNovo_->setBounds(botoes.removeFromLeft(170));
        botoes.removeFromLeft(tema().espacoMedio);
        telaInicialBotaoAbrir_->setBounds(botoes.removeFromLeft(170));
        return;
    }

    constexpr int kLarguraMosaico = 320;
    constexpr int kLarguraFicha = 340;

    mosaicoViewport_->setBounds(area.removeFromLeft(kLarguraMosaico));
    fichaPanel_->setBounds(area.removeFromRight(kLarguraFicha));
    visualizadorPlaceholder_->setBounds(area);

    if (mosaico_) {
        mosaico_->setSize(mosaicoViewport_->getWidth() - mosaicoViewport_->getScrollBarThickness(), mosaico_->getHeight());
        mosaico_->resized();
    }
}

} // namespace matriz::ui
