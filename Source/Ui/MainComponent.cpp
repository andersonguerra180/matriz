#include "MainComponent.h"

#include "../I18n/Strings.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/LeituraTecnica.h"
#include "FichaPanelComponent.h"
#include "MosaicoComponent.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

// Tipo de mídia padrão por categoria de arquivo — o operador troca depois
// direto na ficha se o ingest de pasta (§7.3, com inferência real) ainda não
// existir pra esse fluxo. Papel do arquivo segue o mesmo raciocínio: só o
// necessário pra o item nascer catalogável, não uma tentativa de adivinhar
// o release/faixa/capa que só a inferência de estrutura resolve de verdade.
struct MapeamentoIngest {
    std::string tipoMidia;
    std::string papel;
    bool ehMaster;
};

std::optional<MapeamentoIngest> mapearCategoria(matriz::ingest::CategoriaMidia categoria) {
    switch (categoria) {
        case matriz::ingest::CategoriaMidia::Audio: return MapeamentoIngest{"fita_rolo", "preservation_master", true};
        case matriz::ingest::CategoriaMidia::Video: return MapeamentoIngest{"video", "preservation_master", true};
        case matriz::ingest::CategoriaMidia::Imagem: return MapeamentoIngest{"foto", "foto_suporte", false};
        case matriz::ingest::CategoriaMidia::Documento: return MapeamentoIngest{"documento", "documento", false};
        case matriz::ingest::CategoriaMidia::Desconhecida: return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

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
    mosaico_->aoArquivosSoltos = [this](const juce::Array<juce::File>& arquivos) { ingerirArquivos(arquivos); };

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

void MainComponent::ingerirArquivos(const juce::Array<juce::File>& arquivos) {
    if (!projetoAberto_) return;
    auto& registro = projetoAberto_->projeto().registro();

    auto stmtProjeto = registro.prepare("SELECT id, prefixo_nomenclatura FROM projeto LIMIT 1");
    if (!stmtProjeto.step()) return;
    std::string projetoId = stmtProjeto.columnText(0);
    juce::String prefixo = stmtProjeto.columnText(1);

    auto stmtContagem = registro.prepare("SELECT COUNT(*) FROM item");
    stmtContagem.step();
    int proximoNumero = static_cast<int>(stmtContagem.columnInt(0)) + 1;

    int ingeridos = 0;
    juce::StringArray erros;

    for (auto& arquivo : arquivos) {
        if (arquivo.isDirectory()) continue; // ingest de pasta (§7.3, inferência de estrutura) fica pra depois

        auto categoria = matriz::ingest::categoriaPorExtensao(arquivo);
        auto mapeamento = mapearCategoria(categoria);
        if (!mapeamento) {
            erros.add(arquivo.getFileName() + ": " + matriz::i18n::t("ingest.erro_extensao_desconhecida"));
            continue;
        }

        try {
            std::string itemId = matriz::model::novoUuid();
            std::string agora = matriz::model::agoraIso8601();
            juce::String codigo = prefixo + "-" + juce::String(proximoNumero).paddedLeft('0', 4);
            ++proximoNumero;

            registro.run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
                "VALUES (?, ?, ?, ?, ?, 'capturado', ?, ?)",
                {matriz::db::Value::of(itemId), matriz::db::Value::of(projetoId), matriz::db::Value::of(codigo.toStdString()),
                 matriz::db::Value::of(arquivo.getFileNameWithoutExtension().toStdString()),
                 matriz::db::Value::of(mapeamento->tipoMidia), matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

            matriz::ingest::ingerirArquivo(registro, projetoAberto_->projeto().pasta(), itemId, arquivo, mapeamento->papel,
                                            mapeamento->ehMaster);
            ++ingeridos;
        } catch (const std::exception& e) {
            erros.add(arquivo.getFileName() + ": " + juce::String(e.what()));
        }
    }

    if (mosaico_) mosaico_->recarregar();

    juce::String resumo = matriz::i18n::t("ingest.resumo").replace("{n}", juce::String(ingeridos));
    if (!erros.isEmpty())
        resumo += "\n\n" + matriz::i18n::t("ingest.resumo_erros") + "\n" + erros.joinIntoString("\n");

    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                      .withIconType(erros.isEmpty() ? juce::MessageBoxIconType::InfoIcon
                                                                     : juce::MessageBoxIconType::WarningIcon)
                                      .withTitle(matriz::i18n::t("ingest.titulo"))
                                      .withMessage(resumo)
                                      .withButton(matriz::i18n::t("comum.ok")),
                                  static_cast<juce::ModalComponentManager::Callback*>(nullptr));
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
