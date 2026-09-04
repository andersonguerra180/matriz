#include "MainComponent.h"

#include "../App/Cancelamento.h"
#include "../App/Preferencias.h"
#include "../App/Logger.h"
#include "../Model/ProjectLog.h"
#include "../Diag/Watchdog.h"
#include "../Diag/NSExceptionGuard.h"
#include "../I18n/Strings.h"
#include <mutex>

#include "../Ficha/OrigemPadrao.h"
#include "../Ingest/CacheArquivo.h"
#include "../Vault/Reconciliacao.h"
#include "../Vault/Volume.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Ingest/Miniaturas.h"
#include "AcoesItem.h"
#include "ArvoreComponent.h"
#include "BarraFerramentasComponent.h"
#include "CatalogoComponent.h"
#include "CatalogHubComponent.h"
#include "IntakeWorkspaceComponent.h"
#include "FichaPanelComponent.h"
#include "FiltrosComponent.h"
#include "MosaicoComponent.h"
#include "PainelInconsistenciasComponent.h"
#include "SelecionarTipoMidiaDialogo.h"
#include "NavegadorArquivosDialogo.h"
#include "PreviewComponent.h"
#include "BarraProgressoGlobalComponent.h"
#include "ProgressoGlobal.h"
#include "InitialRelinkDialog.h"
#include "OfflineAssetRelinkDialog.h"
#include "IngestProgressModalDialog.h"
#include "../Vault/AssetRelinkEngine.h"
#include "../Vault/Resolucao.h"
#include "Tokens.h"

namespace matriz::ui {


// Faixa de ações no topo do painel direito (item 5 das correções de
// operação: "resolver tudo no mesmo lugar"). Com o arquivo selecionado, dá
// pra categorizar, renomear, mandar pra uma pasta da BACKUP, tirar do backup
// e abrir a prévia sem sair dali — e tudo vale igual pra seleção múltipla.
//
// Cada botão chama a MESMA função de matriz::ui::acoes que o menu de botão
// direito usa: as duas portas não podem divergir de comportamento.
class BarraAcoesFicha : public juce::Component {
public:
    BarraAcoesFicha() {
        auto criar = [this](std::unique_ptr<juce::TextButton>& destino, const char* chave,
                             std::function<void()>& acao) {
            destino = std::make_unique<juce::TextButton>(matriz::i18n::t(chave));
            destino->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
            destino->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
            destino->onClick = [&acao] { if (acao) acao(); };
            addAndMakeVisible(*destino);
        };
        criar(botaoCategorizar_, "acoes.categorizar", aoCategorizar);
        criar(botaoRenomear_, "acoes.renomear", aoRenomear);
        criar(botaoPreview_, "acoes.preview", aoAbrirPreview);
        criar(botaoEnviar_, "acoes.enviar_para_pasta", aoEnviarParaPasta);
        criar(botaoRemover_, "acoes.remover_do_backup", aoRemoverDoBackup);

        // Remover é destrutivo pro PLANO (nunca pro disco) — cor de perigo
        // pra não ser clicado no automático junto dos outros.
        botaoRemover_->setColour(juce::TextButton::textColourOffId, tema().perigo);
    }

    std::function<void()> aoCategorizar, aoRenomear, aoAbrirPreview, aoEnviarParaPasta, aoRemoverDoBackup;

    // Sem nada selecionado não há sobre o que agir; os botões ficam
    // desabilitados em vez de sumirem, pra o painel não mudar de altura.
    void definirQuantidadeSelecionada(int quantidade) {
        bool algum = quantidade > 0;
        bool umSo = quantidade == 1;
        botaoCategorizar_->setEnabled(algum);
        botaoRenomear_->setEnabled(algum);
        botaoEnviar_->setEnabled(algum);
        botaoRemover_->setEnabled(algum);
        botaoPreview_->setEnabled(umSo); // prévia é de UM arquivo
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(tema().painel);
        g.setColour(tema().borda);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(tema().espacoMedio, 6);
        constexpr int kAlturaBotao = 24;

        auto linha1 = area.removeFromTop(kAlturaBotao);
        int terco = linha1.getWidth() / 3;
        botaoCategorizar_->setBounds(linha1.removeFromLeft(terco).reduced(2, 0));
        botaoRenomear_->setBounds(linha1.removeFromLeft(terco).reduced(2, 0));
        botaoPreview_->setBounds(linha1.reduced(2, 0));

        area.removeFromTop(4);
        auto linha2 = area.removeFromTop(kAlturaBotao);
        botaoEnviar_->setBounds(linha2.removeFromLeft(linha2.getWidth() / 2).reduced(2, 0));
        botaoRemover_->setBounds(linha2.reduced(2, 0));
    }

    static constexpr int kAltura = 66;

private:
    std::unique_ptr<juce::TextButton> botaoCategorizar_, botaoRenomear_, botaoPreview_, botaoEnviar_, botaoRemover_;
};

class DivisorArrastavel : public juce::Component {
public:
    DivisorArrastavel() { setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); }

    std::function<void(int deltaX)> aoArrastar;

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        g.setColour(emHover_ ? tema().bordaFoco : tema().borda);
        g.fillRect(area.withWidth(1).withX(area.getCentreX()));
    }

    void mouseEnter(const juce::MouseEvent&) override { emHover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { emHover_ = false; repaint(); }
    void mouseDown(const juce::MouseEvent&) override { posInicioX_ = getPosition().x; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (aoArrastar) aoArrastar(e.getDistanceFromDragStartX());
    }

    int posInicioX_ = 0;

private:
    bool emHover_ = false;
};

class DivisorArrastavelV : public juce::Component {
public:
    DivisorArrastavelV() { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }

    std::function<void(int deltaY)> aoArrastar;

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        g.setColour(emHover_ ? tema().bordaFoco : tema().borda);
        g.fillRect(area.withHeight(1).withY(area.getCentreY()));
    }

    void mouseEnter(const juce::MouseEvent&) override { emHover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { emHover_ = false; repaint(); }
    void mouseDown(const juce::MouseEvent&) override { posInicioY_ = getPosition().y; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (aoArrastar) aoArrastar(e.getDistanceFromDragStartY());
    }

    int posInicioY_ = 0;

private:
    bool emHover_ = false;
};

// Transport bar — Pro Tools-style: position counter + transport buttons.
// Purely visual for now; wired to AudioWorkspace when audio is playing.
class TransportComponent : public juce::Component {
public:
    TransportComponent() {
        setInterceptsMouseClicks(true, true);

        auto makeBotao = [this](std::unique_ptr<juce::TextButton>& b, const juce::String& label) {
            b = std::make_unique<juce::TextButton>(label);
            b->setColour(juce::TextButton::buttonColourId, tema().painel);
            b->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
            addAndMakeVisible(*b);
        };

        makeBotao(botaoInicio_,    juce::String::fromUTF8("\xe2\x8f\xae"));
        makeBotao(botaoRebobinar_, juce::String::fromUTF8("\xe2\x8f\xaa"));
        makeBotao(botaoAvancar_,   juce::String::fromUTF8("\xe2\x8f\xa9"));
        makeBotao(botaoFim_,       juce::String::fromUTF8("\xe2\x8f\xad"));
        makeBotao(botaoParar_,     juce::String::fromUTF8("\xe2\x96\xa0"));
        makeBotao(botaoPlay_,      juce::String::fromUTF8("\xe2\x96\xb6"));
        makeBotao(botaoGravar_,    juce::String::fromUTF8("\xe2\x97\x89"));

        botaoGravar_->setColour(juce::TextButton::textColourOffId, tema().perigo);
        botaoPlay_->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff4adf4a));

        botaoPlay_->onClick = [this] { tocando_ = !tocando_; repaint(); if (aoPlay) aoPlay(tocando_); };
        botaoParar_->onClick = [this] { tocando_ = false; repaint(); if (aoParar) aoParar(); };
    }

    std::function<void(bool)> aoPlay;
    std::function<void()> aoParar;

    void definirPosicao(double segundos) {
        posicaoSegundos_ = segundos;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(tema().painel);
        g.setColour(tema().borda);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);

        auto counter = getLocalBounds().withWidth(180).withX(8).reduced(0, 4);
        g.setColour(juce::Colour(0xff111114));
        g.fillRoundedRectangle(counter.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xff22dd44));
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));

        int mins = static_cast<int>(posicaoSegundos_) / 60;
        int secs = static_cast<int>(posicaoSegundos_) % 60;
        int frames = static_cast<int>((posicaoSegundos_ - std::floor(posicaoSegundos_)) * 30.0);
        g.drawText(juce::String::formatted("%02d : %02d : %02d", mins, secs, frames),
                   counter, juce::Justification::centred, true);
    }

    void resized() override {
        constexpr int kBotaoW = 32;
        constexpr int kGap = 2;
        int x = 196;
        int y = (getHeight() - 24) / 2;
        auto colocar = [&](juce::TextButton* b) {
            b->setBounds(x, y, kBotaoW, 24);
            x += kBotaoW + kGap;
        };
        colocar(botaoInicio_.get());
        colocar(botaoRebobinar_.get());
        colocar(botaoAvancar_.get());
        colocar(botaoFim_.get());
        x += 6;
        colocar(botaoParar_.get());
        colocar(botaoPlay_.get());
        colocar(botaoGravar_.get());
    }

    static constexpr int kAltura = 36;

private:
    std::unique_ptr<juce::TextButton> botaoInicio_, botaoRebobinar_, botaoAvancar_, botaoFim_;
    std::unique_ptr<juce::TextButton> botaoParar_, botaoPlay_, botaoGravar_;
    double posicaoSegundos_ = 0.0;
    bool tocando_ = false;
};

// Cartão grande e clicável de escolha de modo (§1.1) — a primeira decisão
// do operador, nunca uma opção escondida atrás de "Novo projeto". Cada
// cartão já entrega o texto certo pro público daquele modo (§1.2/§1.3).
class CartaoModo : public juce::Component {
public:
    CartaoModo(juce::String titulo, juce::String descricao, juce::String publico)
        : titulo_(std::move(titulo)), descricao_(std::move(descricao)), publico_(std::move(publico)) {
        setInterceptsMouseClicks(true, false);
    }

    std::function<void()> aoClicar;

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds().toFloat();
        g.setColour(emHover_ ? tema().painelAlt : tema().painel);
        g.fillRoundedRectangle(area, tema().raioMedio);
        g.setColour(emHover_ ? tema().bordaFoco : tema().borda);
        g.drawRoundedRectangle(area.reduced(1.0f), tema().raioMedio, emHover_ ? 2.0f : 1.0f);

        auto miolo = getLocalBounds().reduced(static_cast<int>(tema().espacoGrande));
        g.setColour(tema().textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteTitulo, juce::Font::bold)));
        g.drawText(titulo_, miolo.removeFromTop(32), juce::Justification::centredLeft);

        miolo.removeFromTop(tema().espacoGrande);
        g.setColour(tema().textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
        g.drawFittedText(descricao_, miolo.removeFromTop(40), juce::Justification::topLeft, 2);

        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        g.drawText(publico_, miolo.removeFromBottom(20), juce::Justification::bottomLeft);
    }

    void mouseEnter(const juce::MouseEvent&) override { emHover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { emHover_ = false; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (getLocalBounds().contains(e.getPosition()) && aoClicar) aoClicar();
    }

private:
    juce::String titulo_, descricao_, publico_;
    bool emHover_ = false;
};

// Uma linha clicável na lista de "Recentes" — nome + selo de modo.
class LinhaProjetoRecente : public juce::Component {
public:
    LinhaProjetoRecente(juce::String nome, juce::String seloModo) : nome_(std::move(nome)), selo_(std::move(seloModo)) {
        setInterceptsMouseClicks(true, false);
    }

    std::function<void()> aoClicar;

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        if (emHover_) {
            g.setColour(tema().painelAlt);
            g.fillRoundedRectangle(area.toFloat(), tema().raioPequeno);
        }
        auto miolo = area.reduced(tema().espacoMedio, 0);

        auto areaSelo = miolo.removeFromRight(100).reduced(2, 4);
        bool isCatalog = (selo_.equalsIgnoreCase("catalogo") || selo_.equalsIgnoreCase("catalog"));
        juce::Colour corBadge = isCatalog ? juce::Colour(0xff9d4edd) : tema().acento;
        g.setColour(corBadge.withAlpha(0.18f));
        g.fillRoundedRectangle(areaSelo.toFloat(), 4.0f);
        g.setColour(corBadge.withAlpha(0.5f));
        g.drawRoundedRectangle(areaSelo.toFloat(), 4.0f, 1.0f);

        g.setColour(corBadge);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
        g.drawText(isCatalog ? "CATALOG" : "COLLECTION", areaSelo, juce::Justification::centred);

        g.setColour(tema().textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
        g.drawText(nome_, miolo, juce::Justification::centredLeft);
    }

    void mouseEnter(const juce::MouseEvent&) override { emHover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { emHover_ = false; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (getLocalBounds().contains(e.getPosition()) && aoClicar) aoClicar();
    }

private:
    juce::String nome_, selo_;
    bool emHover_ = false;
};

// Fora do namespace anônimo porque aparece na assinatura de
// finalizarUnidadeDeLote(), declarada no header.
struct EstadoLote {
    juce::CriticalSection lock;
    std::vector<juce::String> erros;
    int sucessos = 0;
    int duplicatas = 0; // dentro de sucessos — conteúdo já conhecido, reconhecido em vez de reimportado (item 9)

    // Itens que a fase 1 já inseriu no banco mas cujo arquivo nunca chegou a
    // ser processado, porque o operador cancelou. Precisam ser APAGADOS no
    // fim: sem isso, cancelar deixaria itens-fantasma na grade — com código
    // de acervo e tudo — que parecem material de verdade mas não têm arquivo
    // nenhum atrás. Não é "descartar o que foi feito" (item 10 proíbe isso);
    // é limpar o que nunca chegou a ser feito.
    std::vector<std::string> naoProcessados;
    std::vector<std::string> todosItemIds;
    bool cancelado = false;
};

namespace {

struct PapelInfo {
    std::string papel;
    bool ehMaster;
};

// Papel do arquivo (§5.4) por categoria — não existe mais tipo de mídia
// escolhido no momento do ingest (Reorientação completa §7.1: conteúdo
// aparece antes de qualquer classificação; a ficha, e portanto o tipo de
// mídia, vem depois, catalogada por cima do que já está na grade). Papel
// fica genérico até a catalogação; migrar pra "master"/"capa_frente"/etc.
// específicos de um tipo é trabalho futuro (§7.4 "aplicar tipo de mídia à
// seleção").
PapelInfo papelPorCategoria(matriz::ingest::CategoriaMidia categoria) {
    if (categoria == matriz::ingest::CategoriaMidia::Audio || categoria == matriz::ingest::CategoriaMidia::Video)
        return {"preservation_master", true};
    if (categoria == matriz::ingest::CategoriaMidia::Imagem) return {"foto_suporte", true};
    return {"documento", true};
}

} // namespace

MainComponent::MainComponent() {
    atualizarTooltips();
    addChildComponent(overlay_);
    reconstruirTelaInicial();
}

void MainComponent::atualizarTooltips() {
    if (matriz::app::lerTooltipsHabilitados()) {
        if (!tooltips_) {
            tooltips_ = std::make_unique<juce::TooltipWindow>(this, 600);
        }
    } else {
        tooltips_ = nullptr;
    }
}

MainComponent::~MainComponent() {
    stopTimer();
    if (cancelamentoLote_) cancelamentoLote_->pedir();
    ingestPool_.removeAllJobs(true, 5000);
    poolVaults_.removeAllJobs(true, 2000);
}

void MainComponent::reconstruirTelaInicial() {
    mosaicoViewport_.reset();
    mosaico_.reset();
    preview_.reset();
    escuta_.reset();
    itemEmPreview_.clear();
    fichaPanel_.reset();
    barraAcoesFicha_.reset();
    barraFerramentas_.reset();
    transport_.reset();
    fichaAberta_ = false;
    arvoreOrigemViewport_.reset();
    arvoreOrigem_.reset();
    arvoreAcervoViewport_.reset();
    arvoreAcervo_.reset();
    labelSource_.reset();
    botaoSourceLista_.reset();
    botaoSourceIcones_.reset();
    labelBackupTree_.reset();
    botaoBackupLista_.reset();
    botaoBackupIcones_.reset();
    botaoNovaPastaAcervo_.reset();
    divisor1_.reset();
    divisor2_.reset();
    divisorFicha_.reset();
    filtrosViewport_.reset();
    filtros_.reset();

    barraNavegacao_.reset();
    catalogWorkspace_.reset();
    analyticsWorkspace_.reset();
    treeWorkspace_.reset();
    backupWorkspace_.reset();
    storageWorkspace_.reset();
    preservationWorkspace_.reset();

    // Pergunta direta em vez de instrução abstrata: a tela inicial abria com
    // "Escolha como você quer trabalhar", que não diz nada a quem nunca viu
    // o app. Agora pergunta o que a pessoa tem em mãos.
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

    telaInicialCartaoArchive_ =
        std::make_unique<CartaoModo>(matriz::i18n::t("tela_inicial.cartao_archive_titulo"),
                                      matriz::i18n::t("tela_inicial.cartao_archive_descricao"),
                                      matriz::i18n::t("tela_inicial.cartao_archive_publico"));
    telaInicialCartaoArchive_->aoClicar = [this] {
        if (aoPedirNovoProjeto) aoPedirNovoProjeto(matriz::model::Modo::Preservacao);
    };
    addAndMakeVisible(*telaInicialCartaoArchive_);

    telaInicialCartaoCatalog_ =
        std::make_unique<CartaoModo>(matriz::i18n::t("tela_inicial.cartao_catalog_titulo"),
                                      matriz::i18n::t("tela_inicial.cartao_catalog_descricao"),
                                      matriz::i18n::t("tela_inicial.cartao_catalog_publico"));
    telaInicialCartaoCatalog_->aoClicar = [this] {
        if (aoPedirNovoProjeto) aoPedirNovoProjeto(matriz::model::Modo::Catalogo);
    };
    addAndMakeVisible(*telaInicialCartaoCatalog_);

    telaInicialBotaoAbrir_ = std::make_unique<juce::TextButton>(matriz::i18n::t("tela_inicial.botao_abrir"));
    telaInicialBotaoAbrir_->onClick = [this] { if (aoPedirAbrirProjeto) aoPedirAbrirProjeto(); };
    addAndMakeVisible(*telaInicialBotaoAbrir_);

    telaInicialLinhasRecentes_.clear();
    auto recentes = matriz::app::lerRecentes();
    if (!recentes.empty()) {
        telaInicialRecentesTitulo_ = std::make_unique<juce::Label>();
        telaInicialRecentesTitulo_->setText(matriz::i18n::t("tela_inicial.recentes_titulo"), juce::dontSendNotification);
        telaInicialRecentesTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
        telaInicialRecentesTitulo_->setColour(juce::Label::textColourId, tema().textoTerciario);
        addAndMakeVisible(*telaInicialRecentesTitulo_);

        constexpr size_t kMaxRecentesExibidos = 5;
        if (recentes.size() > kMaxRecentesExibidos) recentes.resize(kMaxRecentesExibidos);
        for (auto& r : recentes) {
            bool isCatalog = (r.modo.equalsIgnoreCase("catalogo") || r.modo.equalsIgnoreCase("catalog"));
            juce::String selo = isCatalog ? "CATALOG" : "COLLECTION";
            auto linha = std::make_unique<LinhaProjetoRecente>(r.nome, selo);
            juce::File pasta(r.pasta);
            linha->aoClicar = [this, pasta] { if (aoAbrirRecente) aoAbrirRecente(pasta); };
            addAndMakeVisible(*linha);
            telaInicialLinhasRecentes_.push_back(std::move(linha));
        }
    } else {
        telaInicialRecentesTitulo_.reset();
    }

    resized();
    repaint();
}

void MainComponent::reconstruirLayoutProjeto() {
    telaAtiva_ = TelaAtiva::Catalog;
    homePanel_.reset();
    ingestWizard_.reset();

    telaInicialTitulo_.reset();
    telaInicialSubtitulo_.reset();
    telaInicialCartaoArchive_.reset();
    telaInicialCartaoCatalog_.reset();
    telaInicialBotaoAbrir_.reset();
    telaInicialRecentesTitulo_.reset();
    telaInicialLinhasRecentes_.clear();

    // A grade é o protagonista (Reorientação completa §3.1) — ocupa o
    // essencial da janela; o preview (quando aberto) troca de lugar com
    // ela no MESMO espaço, nunca some atrás de outra coisa (abrirPreview/
    // fecharPreview alternam a visibilidade dos dois, resized() dá bounds
    // idênticos aos dois).
    mosaico_ = std::make_unique<MosaicoComponent>(*projetoAberto_);
    mosaico_->aoSelecionar = [this](const std::string& itemId) { selecionarItem(itemId); };
    mosaico_->aoAbrirPreview = [this](const std::string& itemId) { abrirPreview(itemId); };
    mosaico_->aoAbrirRelinkOffline = [this](const std::string& itemId) { abrirDialogoRelinkOffline(itemId); };
    mosaico_->aoClicarEstadoVazio = [this] { if (aoPedirIngerirArquivos) aoPedirIngerirArquivos(); };
    mosaico_->aoMudarSelecao = [this] {
        projetoAberto_->definirItensSelecionadosNoGrid(mosaico_->itensSelecionados());
        atualizarPainelDeApoio();
    };
    mosaico_->aoMudarConteudoVisivel = [this] {
        atualizarPainelDeApoio();
        atualizarBarraMetricas();
    };
    mosaico_->aoPedirMenuContexto = [this](std::vector<std::string> itemIds) { abrirMenuContextoItens(std::move(itemIds)); };
    mosaico_->aoNavegarParaSubpasta = [this](const SubpastaInfo& sub) {
        if (painelAtivo_ == PainelAtivo::Source && arvoreOrigem_)
            arvoreOrigem_->selecionarNoPorItemIds(sub.itemIds);
        else if (painelAtivo_ == PainelAtivo::BackupTree && arvoreAcervo_)
            arvoreAcervo_->selecionarNoPorItemIds(sub.itemIds);
    };
    // Atalhos 1-9 (critério 14). A lista de tipos vem de fichas/*.yaml, na
    // MESMA ordem do seletor — o operador aprende a posição uma vez e ela
    // vale nos dois lugares.
    mosaico_->aoCategorizarPorAtalho = [this](int indice, std::vector<std::string> itemIds) {
        if (!projetoAberto_) return;
        auto tipos = listarTiposMidiaDisponiveis(*projetoAberto_);
        if (indice < 0 || indice >= static_cast<int>(tipos.size())) return;

        const std::string& tipo = tipos[static_cast<size_t>(indice)].id;
        for (const auto& itemId : itemIds) projetoAberto_->atualizarTipoMidia(itemId, tipo);

        if (mosaico_) mosaico_->recarregar();
        atualizarPainelDeApoio();
        if (filtros_) filtros_->recarregar();
    };

    mosaicoViewport_ = std::make_unique<juce::Viewport>();
    mosaicoViewport_->setViewedComponent(mosaico_.get(), false);
    addAndMakeVisible(*mosaicoViewport_);

    // Dois painéis lado a lado: SOURCE (Origem) e BACKUP TREE (Acervo).
    // Sempre visíveis — sem abas, sem trocar. Cada painel tem cabeçalho
    // próprio. Clicar num painel o ativa (highlight + filtros atuam nele).
    arvoreOrigem_ = std::make_unique<ArvoreComponent>(*projetoAberto_);
    arvoreOrigem_->definirAba(ArvoreComponent::Aba::Origem);
    arvoreOrigem_->aoSelecionarNo = [this](std::optional<std::set<std::string>> itemIds) {
        ativarPainel(PainelAtivo::Source);
        if (mosaico_) mosaico_->definirFiltroItens(std::move(itemIds));
    };
    arvoreOrigem_->aoMostrarConteudoNaGrade = [this](const std::set<std::string>& itemIds) {
        mostrarGrid();
        if (catalogWorkspace_) catalogWorkspace_->definirSelecaoItens(itemIds);
        else if (mosaico_) {
            mosaico_->definirFiltroItens(itemIds);
            mosaico_->definirSelecao(itemIds);
        }
    };
    arvoreOrigem_->aoMudarSubpastas = [this](std::vector<SubpastaInfo> subs) {
        if (mosaico_) mosaico_->definirSubpastas(std::move(subs));
    };
    arvoreOrigemViewport_ = std::make_unique<juce::Viewport>();
    arvoreOrigemViewport_->setViewedComponent(arvoreOrigem_.get(), false);
    addAndMakeVisible(*arvoreOrigemViewport_);

    arvoreAcervo_ = std::make_unique<ArvoreComponent>(*projetoAberto_);
    arvoreAcervo_->definirAba(ArvoreComponent::Aba::Acervo);
    arvoreAcervo_->definirModoVisao(ArvoreComponent::ModoVisao::Icones);
    arvoreAcervo_->aoSelecionarNo = [this](std::optional<std::set<std::string>> itemIds) {
        ativarPainel(PainelAtivo::BackupTree);
        if (mosaico_) mosaico_->definirFiltroItens(std::move(itemIds));
    };
    arvoreAcervo_->aoMostrarConteudoNaGrade = [this](const std::set<std::string>& itemIds) {
        mostrarGrid();
        if (catalogWorkspace_) catalogWorkspace_->definirSelecaoItens(itemIds);
        else if (mosaico_) {
            mosaico_->definirFiltroItens(itemIds);
            mosaico_->definirSelecao(itemIds);
        }
    };
    arvoreAcervo_->aoMudarSubpastas = [this](std::vector<SubpastaInfo> subs) {
        if (mosaico_) mosaico_->definirSubpastas(std::move(subs));
    };
    arvoreAcervoViewport_ = std::make_unique<juce::Viewport>();
    arvoreAcervoViewport_->setViewedComponent(arvoreAcervo_.get(), false);
    addAndMakeVisible(*arvoreAcervoViewport_);

    labelSource_ = std::make_unique<juce::Label>("", matriz::i18n::t("arvore.aba_origem"));
    labelSource_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    labelSource_->setColour(juce::Label::textColourId, tema().textoPrimario);
    labelSource_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*labelSource_);

    // View mode toggles for SOURCE panel (list/icons)
    botaoSourceLista_ = std::make_unique<juce::TextButton>(matriz::i18n::t("navegador.visao_lista"));
    botaoSourceLista_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    botaoSourceLista_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    botaoSourceLista_->onClick = [this] {
        if (arvoreOrigem_) arvoreOrigem_->definirModoVisao(ArvoreComponent::ModoVisao::Lista);
    };
    addAndMakeVisible(*botaoSourceLista_);

    botaoSourceIcones_ = std::make_unique<juce::TextButton>(matriz::i18n::t("navegador.visao_icones"));
    botaoSourceIcones_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    botaoSourceIcones_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    botaoSourceIcones_->onClick = [this] {
        if (arvoreOrigem_) arvoreOrigem_->definirModoVisao(ArvoreComponent::ModoVisao::Icones);
    };
    addAndMakeVisible(*botaoSourceIcones_);

    labelBackupTree_ = std::make_unique<juce::Label>("", matriz::i18n::t("arvore.aba_acervo"));
    labelBackupTree_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    labelBackupTree_->setColour(juce::Label::textColourId, tema().textoPrimario);
    labelBackupTree_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*labelBackupTree_);

    // View mode toggles for BACKUP TREE panel
    botaoBackupLista_ = std::make_unique<juce::TextButton>(matriz::i18n::t("navegador.visao_lista"));
    botaoBackupLista_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    botaoBackupLista_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    botaoBackupLista_->onClick = [this] {
        if (arvoreAcervo_) arvoreAcervo_->definirModoVisao(ArvoreComponent::ModoVisao::Lista);
    };
    addAndMakeVisible(*botaoBackupLista_);

    botaoBackupIcones_ = std::make_unique<juce::TextButton>(matriz::i18n::t("navegador.visao_icones"));
    botaoBackupIcones_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    botaoBackupIcones_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    botaoBackupIcones_->onClick = [this] {
        if (arvoreAcervo_) arvoreAcervo_->definirModoVisao(ArvoreComponent::ModoVisao::Icones);
    };
    addAndMakeVisible(*botaoBackupIcones_);

    botaoNovaPastaAcervo_ = std::make_unique<juce::TextButton>("+");
    botaoNovaPastaAcervo_->onClick = [this] { if (arvoreAcervo_) arvoreAcervo_->criarPastaDeTopoNivel(); };
    addAndMakeVisible(*botaoNovaPastaAcervo_);

    {
        auto d1 = std::make_unique<DivisorArrastavel>();
        d1->aoArrastar = [this](int deltaX) {
            auto* div = static_cast<DivisorArrastavel*>(divisor1_.get());
            int larguraUtil = getWidth();
            int novaPos = div->posInicioX_ + deltaX;
            propPainel1_ = juce::jlimit(0.10f, 0.40f, static_cast<float>(novaPos) / larguraUtil);
            resized();
        };
        divisor1_ = std::move(d1);
        addAndMakeVisible(*divisor1_);
    }
    {
        auto d2 = std::make_unique<DivisorArrastavel>();
        d2->aoArrastar = [this](int deltaX) {
            auto* div = static_cast<DivisorArrastavel*>(divisor2_.get());
            int larguraUtil = getWidth();
            int novaPos = div->posInicioX_ + deltaX;
            propPainel2_ = juce::jlimit(0.10f, 0.40f, 1.0f - static_cast<float>(novaPos) / larguraUtil);
            resized();
        };
        divisor2_ = std::move(d2);
        addAndMakeVisible(*divisor2_);
    }
    {
        auto dv = std::make_unique<DivisorArrastavelV>();
        dv->aoArrastar = [this](int deltaY) {
            auto* div = static_cast<DivisorArrastavelV*>(divisorFicha_.get());
            int alturaPainel = arvoreAcervoViewport_ ? arvoreAcervoViewport_->getHeight() +
                (fichaPanel_ ? fichaPanel_->getHeight() : 0) + BarraAcoesFicha::kAltura + 6 : getHeight();
            int novaPos = div->posInicioY_ + deltaY;
            int base = arvoreAcervoViewport_ ? arvoreAcervoViewport_->getY() : 0;
            propFichaBackup_ = juce::jlimit(0.15f, 0.70f, 1.0f - static_cast<float>(novaPos - base) / alturaPainel);
            resized();
        };
        divisorFicha_ = std::move(dv);
        addAndMakeVisible(*divisorFicha_);
    }

    arvoreOrigem_->aoMudarOrganizacao = [this] {
        if (arvoreAcervo_) arvoreAcervo_->recarregarSincrono();
        if (painelAtivo_ == PainelAtivo::Source && arvoreOrigem_ && mosaico_)
            mosaico_->definirFiltroItens(arvoreOrigem_->todosOsItensVisiveis());
        if (mosaico_) mosaico_->recarregar();
        if (filtros_) filtros_->recarregar();
        atualizarPainelDeApoio();
        atualizarEtapaDoFluxo();
    };
    arvoreAcervo_->aoMudarOrganizacao = [this] {
        if (arvoreOrigem_) arvoreOrigem_->recarregarSincrono();
        if (painelAtivo_ == PainelAtivo::Source && arvoreOrigem_ && mosaico_)
            mosaico_->definirFiltroItens(arvoreOrigem_->todosOsItensVisiveis());
        if (mosaico_) mosaico_->recarregar();
        if (filtros_) filtros_->recarregar();
        atualizarPainelDeApoio();
        atualizarEtapaDoFluxo();
    };

    painelAtivo_ = PainelAtivo::Source;

    // Filtros (chips) + busca + coleções inteligentes (item 7, Acréscimos
    // §10) — embaixo da árvore na mesma coluna (§9.1). Também precisa do
    // mosaico já existir (opera direto sobre ele, mesmo padrão de
    // ArvoreComponent sobre ProjetoAberto).
    filtros_ = std::make_unique<FiltrosComponent>(*projetoAberto_, *mosaico_);
    // Aviso de "backup planejado" na mesma faixa discreta do ingest e da
    // reconciliação — nunca um modal (§8/§9: nada que exija resposta).
    filtros_->aoAvisar = [this](const juce::String& mensagem) {
        textoProgressoIngest_ = mensagem;
    };
    filtrosViewport_ = std::make_unique<juce::Viewport>();
    filtrosViewport_->setViewedComponent(filtros_.get(), false);
    addAndMakeVisible(*filtrosViewport_);


    // Ficha (metadata) always visible below the grid — clicking a file
    // immediately shows its details for editing.
    fichaAberta_ = true;
    fichaPanel_ = std::make_unique<FichaPanelComponent>(*projetoAberto_);
    addAndMakeVisible(*fichaPanel_);

    barraAcoesFicha_ = std::make_unique<BarraAcoesFicha>();
    addAndMakeVisible(*barraAcoesFicha_);
    {
        // Mesmos ganchos do menu de contexto — as duas portas compartilham
        // não só as funções de ação, mas também o que acontece depois delas.
        auto idsSelecionados = [this] {
            if (!mosaico_) return std::vector<std::string>{};
            return std::vector<std::string>(mosaico_->itensSelecionados().begin(),
                                             mosaico_->itensSelecionados().end());
        };
        barraAcoesFicha_->aoCategorizar = [this, idsSelecionados] {
            matriz::ui::acoes::mudarTipo(*projetoAberto_, idsSelecionados(), ganchosDeAcao());
        };
        barraAcoesFicha_->aoRenomear = [this, idsSelecionados] {
            matriz::ui::acoes::renomear(*projetoAberto_, idsSelecionados(), ganchosDeAcao());
        };
        barraAcoesFicha_->aoEnviarParaPasta = [this, idsSelecionados] {
            matriz::ui::acoes::enviarParaPasta(*projetoAberto_, idsSelecionados(), ganchosDeAcao(),
                                                barraAcoesFicha_.get());
        };
        barraAcoesFicha_->aoRemoverDoBackup = [this, idsSelecionados] {
            matriz::ui::acoes::removerDoBackup(*projetoAberto_, idsSelecionados(), ganchosDeAcao());
        };
        barraAcoesFicha_->aoAbrirPreview = [this] {
            if (mosaico_ && !mosaico_->itemSelecionado().empty()) abrirPreview(mosaico_->itemSelecionado());
        };
    }
    // Edição em lote pode mudar tipo_midia e campos que afetam agrupamento,
    // contagem de chips e o painel de inconsistências em outro lugar.
    fichaPanel_->aoAplicarEmLote = [this] {
        if (mosaico_) mosaico_->recarregar();
        if (arvoreOrigem_) arvoreOrigem_->recarregar();
        if (arvoreAcervo_) arvoreAcervo_->recarregar();
        if (filtros_) filtros_->recarregar();
        atualizarPainelDeApoio();
    };
    fichaPanel_->aoAplicarSucesso = [this](const std::string& itemId) {
        if (mosaico_) mosaico_->atualizarItemEmMemoria(itemId);
    };
    fichaPanel_->aoMudar = [this] {
        if (mosaico_) mosaico_->recarregar();
        if (arvoreOrigem_) arvoreOrigem_->recarregar();
        if (arvoreAcervo_) arvoreAcervo_->recarregar();
        if (filtros_) filtros_->recarregar();
        atualizarPainelDeApoio();
    };

    // --- Barras de apoio ---
    //
    // Antes disto, as ações do fluxo principal (adicionar arquivos, gravar
    // backup) só existiam no menu do sistema, e a edição em lote só dentro
    // da ficha fechada. Quem abria o app pela primeira vez via uma grade
    // vazia, um botão "Ficha" e mais nada — daí "não se explica".
    barraFerramentas_ = std::make_unique<BarraFerramentasComponent>();
    barraFerramentas_->aoAdicionarArquivos = [this] { if (aoPedirIngerirArquivos) aoPedirIngerirArquivos(); };
    barraFerramentas_->aoAbrirNavegador = [this] { abrirNavegadorArquivos(); };
    barraFerramentas_->aoAlternarDetalhes = [this] { alternarFicha(); };
    barraFerramentas_->aoFazerBackup = [this] { if (aoPedirBackup) aoPedirBackup(); };
    barraFerramentas_->aoBuscar = [this](const juce::String& texto) {
        if (!mosaico_) return;
        mosaico_->definirBusca(texto);
        if (filtros_) filtros_->recarregar(); // a linha "Limpar filtros" aparece/some com a busca
    };
    barraFerramentas_->aoMudarTamanho = [this](int indice) {
        if (!mosaico_) return;
        mosaico_->definirTamanhoCelula(indice == 0   ? MosaicoComponent::TamanhoCelula::Pequeno
                                        : indice == 2 ? MosaicoComponent::TamanhoCelula::Grande
                                                      : MosaicoComponent::TamanhoCelula::Medio);
    };
    barraFerramentas_->aoAlternarModoVisao = [this](bool lista) {
        if (!mosaico_) return;
        mosaico_->definirModoVisao(lista ? MosaicoComponent::ModoVisao::Lista
                                          : MosaicoComponent::ModoVisao::Grade);
    };
    barraFerramentas_->aoAlternarDestacarEditados = [this](bool ativo) {
        if (mosaico_) mosaico_->definirDestacarEditados(ativo);
    };
    barraFerramentas_->aoMudarFiltroHorizontal = [this](const std::string& filtro) {
        if (!mosaico_ || !projetoAberto_) return;
        
        if (filtro == "all") {
            mosaico_->definirFiltroItens(std::nullopt);
            mosaico_->recarregar();
            return;
        }
        
        std::vector<std::string> tipos;
        if (filtro == "audio") {
            tipos = {"fita_rolo", "cassete", "vinil", "dat", "minidisc", "cd", "digital_audio", "field_recording", "sound_effects"};
        } else if (filtro == "video") {
            tipos = {"filme", "video", "dvd", "vhs", "umatic", "betacam", "betamax", "digital_video"};
        } else if (filtro == "image") {
            tipos = {"foto", "cover_art", "negativo", "slide"};
        } else if (filtro == "document") {
            tipos = {"documento", "sample", "3d_file", "release"};
        }
        
        std::set<std::string> ids;
        juce::String sql = "SELECT id FROM item";
        if (!tipos.empty()) {
            sql += " WHERE tipo_midia IN (";
            for (size_t i = 0; i < tipos.size(); ++i) {
                sql += "'" + juce::String(tipos[i]) + "'";
                if (i + 1 < tipos.size()) sql += ",";
            }
            sql += ")";
        }
        
        try {
            auto stmt = projetoAberto_->projeto().registro().prepare(sql.toStdString());
            while (stmt.step()) {
                ids.insert(stmt.columnText(0));
            }
        } catch (const std::exception& e) {
            juce::Logger::writeToLog("SQL error in horizontal filter: " + juce::String(e.what()));
        }
        
        mosaico_->definirFiltroItens(ids);
        mosaico_->recarregar();
    };
    barraFerramentas_->aoMudarFiltroStatus = [this](const std::string& status) {
        if (!mosaico_) return;
        if (status == "online") {
            mosaico_->alternarFiltroDisponibilidade(MosaicoComponent::FiltroDisponibilidade::Online);
        } else if (status == "offline") {
            mosaico_->alternarFiltroDisponibilidade(MosaicoComponent::FiltroDisponibilidade::Offline);
        } else {
            mosaico_->alternarFiltroDisponibilidade(MosaicoComponent::FiltroDisponibilidade::All);
        }
    };
    addAndMakeVisible(*barraFerramentas_);

    transport_ = std::make_unique<TransportComponent>();
    addAndMakeVisible(*transport_);

    setWantsKeyboardFocus(true);

    // A busca agora mora na barra de ferramentas, no topo — deixar uma
    // segunda caixa de busca na coluna esquerda faria o operador não saber
    // qual das duas manda. FiltrosComponent avisa por aqui quando precisa
    // que o campo acompanhe (limpar filtros, aplicar coleção salva).
    filtros_->aoSincronizarBusca = [this](const juce::String& texto) {
        if (barraFerramentas_) barraFerramentas_->definirTextoBuscaSemNotificar(texto);
    };

    mosaico_->recarregar();
    atualizarPainelDeApoio();
    atualizarEtapaDoFluxo();

    resized();
    repaint();
}

matriz::ui::acoes::Ganchos MainComponent::ganchosDeAcao() {
    matriz::ui::acoes::Ganchos ganchos;
    // Qualquer ação que mexe no banco pode mudar contagem, agrupamento,
    // árvore e o estágio do fluxo — recarrega tudo por um caminho só.
    ganchos.aoMudarDados = [this] {
        if (mosaico_) mosaico_->recarregar();
        if (arvoreOrigem_) arvoreOrigem_->recarregar();
        if (arvoreAcervo_) arvoreAcervo_->recarregar();
        if (filtros_) filtros_->recarregar();
        atualizarPainelDeApoio();
    };
    ganchos.aoAbrirDetalhes = [this] {
        if (!fichaAberta_) alternarFicha();
        if (mosaico_) selecionarItem(mosaico_->itemSelecionado());
    };
    ganchos.aoFiltrarItens = [this](std::set<std::string> ids) {
        if (mosaico_) mosaico_->definirFiltroItens(std::move(ids));
    };
    return ganchos;
}

void MainComponent::abrirMenuContextoItens(std::vector<std::string> itemIds) {
    if (!projetoAberto_ || itemIds.empty()) return;

    auto ganchos = ganchosDeAcao();
    auto menu = matriz::ui::acoes::construirMenu(*projetoAberto_, itemIds);
    ProjetoAberto* projeto = projetoAberto_.get();
    menu.showMenuAsync(juce::PopupMenu::Options(), [projeto, itemIds, ganchos](int resultado) {
        matriz::ui::acoes::executar(resultado, *projeto, itemIds, ganchos);
    });
}

void MainComponent::atualizarPainelDeApoio() {
    MATRIZ_TRACE("MainComponent::atualizarPainelDeApoio");
    if (!projetoAberto_ || !mosaico_) return;

    int total = mosaico_->totalItensCarregados();
    int visiveis = mosaico_->totalItensVisiveis();

    if (barraFerramentas_) {
        barraFerramentas_->definirContagem(visiveis, total, visiveis != total);
        barraFerramentas_->definirDetalhesAbertos(fichaAberta_);
    }

    if (barraAcoesFicha_)
        barraAcoesFicha_->definirQuantidadeSelecionada(static_cast<int>(mosaico_->itensSelecionados().size()));

}

void MainComponent::atualizarEtapaDoFluxo() {}

void MainComponent::ativarPainel(PainelAtivo painel) {
    if (painelAtivo_ == painel) return;
    painelAtivo_ = painel;
    repaint();
}

void MainComponent::alternarFicha() {
    fichaAberta_ = !fichaAberta_;
    if (fichaPanel_) fichaPanel_->setVisible(fichaAberta_);
    if (barraAcoesFicha_) barraAcoesFicha_->setVisible(fichaAberta_);
    if (divisorFicha_) divisorFicha_->setVisible(fichaAberta_);
    if (barraFerramentas_) barraFerramentas_->definirDetalhesAbertos(fichaAberta_);
    resized();
}

void MainComponent::abrirPreview(const std::string& itemId) {
    if (!projetoAberto_) return;

    // Áudio abre na Estação de Escuta (§7), não no preview genérico: pra
    // material sonoro, "abrir" significa ouvir, com transporte, marcadores e
    // medidores — não olhar uma miniatura.
    if (auto info = projetoAberto_->arquivoPrincipal(itemId)) {
        if (matriz::ingest::categoriaPorExtensao(juce::File(info->caminhoAbsoluto)) ==
            matriz::ingest::CategoriaMidia::Audio) {
            abrirEscuta(itemId);
            return;
        }
    }

    if (!preview_) {
        preview_ = std::make_unique<PreviewComponent>(*projetoAberto_);
        preview_->aoFechar = [this] { fecharPreview(); };
        preview_->aoNavegar = [this](int direcao) {
            if (!mosaico_ || !preview_) return;
            auto proximo = mosaico_->itemAdjacente(preview_->itemAtual(), direcao);
            if (proximo) abrirPreview(*proximo);
        };
        preview_->aoPedirRelinkManual = [this](const std::string& id) {
            abrirDialogoRelinkOffline(id);
        };
        addAndMakeVisible(*preview_);
    }

    itemEmPreview_ = itemId;
    preview_->mostrarItem(itemId);
    mosaico_->selecionarItem(itemId);
    selecionarItem(itemId);

    if (transport_) {
        auto* vp = preview_->videoPlayerParaTeste();
        transport_->aoPlay = [vp](bool tocando) {
            if (!vp) return;
            if (tocando) vp->tocar(); else vp->pausar();
        };
        transport_->aoParar = [vp]() {
            if (vp) vp->parar();
        };
        if (vp) {
            vp->aoPosicaoMudar = [this](double s) {
                if (transport_) transport_->definirPosicao(s);
            };
        }
    }

    mosaicoViewport_->setVisible(false);
    preview_->setVisible(true);
    resized();
    repaint();
}

// Estação de escuta (§7). Criada na primeira vez que um áudio é aberto —
// abrir placa de som e alocar buffer de reprodução num projeto que só tem
// fotos seria custo puro.
void MainComponent::abrirEscuta(const std::string& itemId) {
    matriz::diag::breadcrumb("abrirEscuta");
    if (!projetoAberto_) return;

    if (!escuta_) {
        escuta_ = std::make_unique<AudioWorkspace>(*projetoAberto_);
        escuta_->aoFechar = [this] { fecharEscuta(); };
        escuta_->aoMudarMarcadores = [this] {
            if (fichaPanel_ && !itemEmEscuta_.empty())
                fichaPanel_->mostrarItem(itemEmEscuta_);
        };
        escuta_->aoNavegar = [this](int direcao) {
            if (!mosaico_ || itemEmEscuta_.empty()) return;
            auto proximo = mosaico_->itemAdjacente(itemEmEscuta_, direcao);
            if (proximo) abrirPreview(*proximo);
        };
        addAndMakeVisible(*escuta_);
    }

    auto it = projetoAberto_->obterItemResumo(itemId);
    if (!it) return;

    // O arquivo é OPCIONAL (I3): com o Vault desconectado o workspace abre
    // do mesmo jeito, com forma de onda e métricas vindas do cache, e só o
    // transporte fica desabilitado.
    std::optional<juce::File> arquivo;
    if (auto info = projetoAberto_->arquivoPrincipal(itemId)) {
        juce::File f(info->caminhoAbsoluto);
        if (f.existsAsFile()) arquivo = f;
    }

    itemEmEscuta_ = itemId;
    escuta_->carregarAsset(*it, arquivo, nullptr);
    selecionarItem(itemId);

    mosaicoViewport_->setVisible(false);
    if (preview_) preview_->setVisible(false);
    escuta_->setVisible(true);
    escuta_->grabKeyboardFocus();  // JKL/espaço só valem com o foco aqui
    resized();
    repaint();
}

void MainComponent::fecharEscuta() {
    matriz::diag::breadcrumb("fecharEscuta");
    if (!escuta_) return;
    escuta_->descarregar();
    escuta_->setVisible(false);
    mosaicoViewport_->setVisible(true);
    itemEmEscuta_.clear();
    resized();
    repaint();
}

void MainComponent::fecharPreview() {
    if (!preview_) return;
    preview_->setVisible(false);
    mosaicoViewport_->setVisible(true);
    itemEmPreview_.clear();
    resized();
    repaint();
}

bool MainComponent::abrirCatalogo(const juce::File& pasta) {
    auto novo = std::make_unique<CatalogoComponent>();
    if (!novo->abrir(pasta)) return false;

    projetoAberto_.reset();
    reconstruirTelaInicial(); // limpa tudo o que era de projeto
    telaInicialTitulo_.reset();
    telaInicialSubtitulo_.reset();
    telaInicialCartaoArchive_.reset();
    telaInicialCartaoCatalog_.reset();
    telaInicialBotaoAbrir_.reset();
    telaInicialRecentesTitulo_.reset();
    telaInicialLinhasRecentes_.clear();

    catalogo_ = std::move(novo);
    reconstruirLayoutCatalogo(pasta);
    if (aoMudarEstadoProjeto) aoMudarEstadoProjeto();
    return true;
}

void MainComponent::reconstruirLayoutCatalogo(const juce::File& pasta) {
    catalogoViewport_ = std::make_unique<juce::Viewport>();
    catalogoViewport_->setViewedComponent(catalogo_.get(), false);
    addAndMakeVisible(*catalogoViewport_);

    catalogoTitulo_ = std::make_unique<juce::Label>();
    catalogoTitulo_->setText(matriz::i18n::t("catalogo.titulo") + "  |  " + pasta.getFileName(),
                              juce::dontSendNotification);
    catalogoTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    catalogoTitulo_->setColour(juce::Label::textColourId, tema().textoPrimario);
    addAndMakeVisible(*catalogoTitulo_);

    catalogoBusca_ = std::make_unique<juce::TextEditor>();
    catalogoBusca_->setTextToShowWhenEmpty(matriz::i18n::t("barra.buscar"), tema().textoTerciario);
    catalogoBusca_->onTextChange = [this] {
        if (catalogo_) catalogo_->definirBusca(catalogoBusca_->getText());
    };
    addAndMakeVisible(*catalogoBusca_);

    catalogoFechar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("catalogo.fechar"));
    catalogoFechar_->onClick = [this] { fecharProjeto(); };
    addAndMakeVisible(*catalogoFechar_);

    resized();
    repaint();
}

void MainComponent::abrirProjeto(std::unique_ptr<matriz::model::Project> projeto) {
    stopTimer();
    poolVaults_.removeAllJobs(true, 2000);
    reconstruirTelaInicial();

    projetoAberto_ = std::make_unique<ProjetoAberto>(std::move(projeto));

    bool isCatalog = (projetoAberto_->projeto().modo() == matriz::model::Modo::Catalogo);
    juce::String nomeProj = juce::String::fromUTF8(projetoAberto_->projeto().nome().c_str());

    // Create navigation bar
    barraNavegacao_ = std::make_unique<BarraNavegacaoComponent>();
    barraNavegacao_->setProjectInfo(nomeProj, isCatalog);
    barraNavegacao_->setHasParentCatalog(!isCatalog && catalogoPai_.exists());
    barraNavegacao_->aoMudarTab = [this](BarraNavegacaoComponent::Tab tab) {
        if (tab == BarraNavegacaoComponent::Tab::Catalog) mostrarCatalogHub();
        else if (tab == BarraNavegacaoComponent::Tab::Intake) mostrarIntake();
        else if (tab == BarraNavegacaoComponent::Tab::Grid) mostrarGrid();
        else if (tab == BarraNavegacaoComponent::Tab::Duplicates) mostrarDuplicates();
        else if (tab == BarraNavegacaoComponent::Tab::Analytics) mostrarAnalytics();
        else if (tab == BarraNavegacaoComponent::Tab::Tree) mostrarTree();
        else if (tab == BarraNavegacaoComponent::Tab::Backup) mostrarBackup();
        else if (tab == BarraNavegacaoComponent::Tab::Storage) mostrarStorage();
    };
    barraNavegacao_->aoClicarFechar = [this] {
        if (catalogoPai_.exists()) retornarAoCatalogo();
        else fecharProjeto();
    };
    addAndMakeVisible(*barraNavegacao_);

    barraProgressoGlobal_ = std::make_unique<BarraProgressoGlobalComponent>();
    addAndMakeVisible(*barraProgressoGlobal_);

    if (isCatalog) {
        mostrarCatalogHub();
    } else {
        if (projetoAberto_ && projetoAberto_->contarItens() > 0) {
            mostrarGrid();
        } else {
            mostrarIntake();
        }
    }

    verificarVaultsConectados();
    verificarPresencaInicialAssets();
    if (aoMudarEstadoProjeto) aoMudarEstadoProjeto();
    startTimer(5000);
}

void MainComponent::abrirColecaoDoCatalogo(const juce::File& pastaColecao) {
    if (!projetoAberto_) return;
    catalogoPai_ = pastaProjeto();

    ProgressoGlobal::obterInstancia().iniciarTarefa("collection_open", "Opening Collection", 100, nullptr, pastaColecao.getFileName());

    auto projColecao = matriz::model::Project::abrir(pastaColecao);
    if (!projColecao) {
        ProgressoGlobal::obterInstancia().concluirTarefa("collection_open", "Failed to open collection");
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Failed to Open Collection")
                .withMessage("Could not open collection at:\n" + pastaColecao.getFullPathName())
                .withButton("OK"),
            nullptr);
        return;
    }
    abrirProjeto(std::move(projColecao));
    ProgressoGlobal::obterInstancia().concluirTarefa("collection_open", "Collection loaded: " + pastaColecao.getFileName());
}

void MainComponent::retornarAoCatalogo() {
    if (!catalogoPai_.exists()) {
        fecharProjeto();
        return;
    }
    juce::File destino = catalogoPai_;
    catalogoPai_ = juce::File();

    auto catalogoProj = matriz::model::Project::abrir(destino);
    if (catalogoProj) {
        abrirProjeto(std::move(catalogoProj));
    } else {
        fecharProjeto();
    }
}

void MainComponent::mostrarCatalogHub() {
    if (!projetoAberto_) return;
    telaAtiva_ = TelaAtiva::Catalog;

    ProgressoGlobal::obterInstancia().iniciarTarefa("hub_open", "Opening Collections", 100, nullptr, "Loading collections hub...");

    if (barraNavegacao_) {
        barraNavegacao_->setSelectedTab(BarraNavegacaoComponent::Tab::Catalog);
        barraNavegacao_->setVisible(true);
    }

    if (homePanel_) homePanel_->setVisible(false);
    if (intakeWorkspace_) intakeWorkspace_->setVisible(false);
    if (catalogWorkspace_) catalogWorkspace_->setVisible(false);
    if (duplicatesWorkspace_) duplicatesWorkspace_->setVisible(false);
    if (analyticsWorkspace_) analyticsWorkspace_->setVisible(false);
    if (treeWorkspace_) treeWorkspace_->setVisible(false);
    if (backupWorkspace_) backupWorkspace_->setVisible(false);
    if (storageWorkspace_) storageWorkspace_->setVisible(false);

    if (!catalogHubWorkspace_) {
        catalogHubWorkspace_ = std::make_unique<CatalogHubComponent>(*projetoAberto_);
        juce::Component::SafePointer<MainComponent> safeThis(this);
        catalogHubWorkspace_->aoAbrirColecao = [safeThis](const juce::File& pastaColecao) {
            if (safeThis) safeThis->abrirColecaoDoCatalogo(pastaColecao);
        };
        catalogHubWorkspace_->aoBackupConsolidado = [safeThis] {
            if (safeThis) safeThis->mostrarBackup();
        };
        addAndMakeVisible(*catalogHubWorkspace_);
    } else {
        catalogHubWorkspace_->setVisible(true);
        catalogHubWorkspace_->recarregar();
    }

    ProgressoGlobal::obterInstancia().concluirTarefa("hub_open", "Collections Hub ready");
    resized();
    repaint();
}

void MainComponent::mostrarHome() {
    mostrarGrid();
}

void MainComponent::mostrarIntake() {
    if (!projetoAberto_) return;
    telaAtiva_ = TelaAtiva::Catalog;

    if (barraNavegacao_) {
        barraNavegacao_->setSelectedTab(BarraNavegacaoComponent::Tab::Intake);
        barraNavegacao_->setVisible(true);
    }

    if (homePanel_) homePanel_->setVisible(false);
    if (catalogHubWorkspace_) catalogHubWorkspace_->setVisible(false);
    if (catalogWorkspace_) catalogWorkspace_->setVisible(false);
    if (duplicatesWorkspace_) duplicatesWorkspace_->setVisible(false);
    if (analyticsWorkspace_) analyticsWorkspace_->setVisible(false);
    if (treeWorkspace_) treeWorkspace_->setVisible(false);
    if (backupWorkspace_) backupWorkspace_->setVisible(false);
    if (storageWorkspace_) storageWorkspace_->setVisible(false);

    if (!intakeWorkspace_) {
        intakeWorkspace_ = std::make_unique<IntakeWorkspaceComponent>(*projetoAberto_);
        intakeWorkspace_->aoPedirIngerirArquivos = [this] { if (aoPedirIngerirArquivos) aoPedirIngerirArquivos(); };
        intakeWorkspace_->aoIngerirArquivosDireto = [this](const juce::Array<juce::File>& arqs) {
            ingerirArquivos(arqs);
        };
        intakeWorkspace_->aoConfirmarParaGrid = [this] {
            if (catalogWorkspace_) catalogWorkspace_->recarregar();
        };
        addAndMakeVisible(*intakeWorkspace_);
    } else {
        intakeWorkspace_->setVisible(true);
        intakeWorkspace_->recarregar();
    }

    resized();
    repaint();
}

void MainComponent::mostrarCatalog() {
    mostrarGrid();
}

void MainComponent::mostrarGrid() {
    if (!projetoAberto_) return;
    telaAtiva_ = TelaAtiva::Catalog;

    ProgressoGlobal::obterInstancia().iniciarTarefa("catalog_view", "Opening Catalog", 100, nullptr, "Loading catalog view...");

    if (barraNavegacao_) {
        barraNavegacao_->setSelectedTab(BarraNavegacaoComponent::Tab::Grid);
        barraNavegacao_->setVisible(true);
    }

    if (homePanel_) homePanel_->setVisible(false);
    if (catalogHubWorkspace_) catalogHubWorkspace_->setVisible(false);
    if (intakeWorkspace_) intakeWorkspace_->setVisible(false);
    if (duplicatesWorkspace_) duplicatesWorkspace_->setVisible(false);
    if (analyticsWorkspace_) analyticsWorkspace_->setVisible(false);
    if (treeWorkspace_) treeWorkspace_->setVisible(false);
    if (backupWorkspace_) backupWorkspace_->setVisible(false);
    if (storageWorkspace_) storageWorkspace_->setVisible(false);

    if (!catalogWorkspace_) {
        catalogWorkspace_ = std::make_unique<CatalogWorkspaceComponent>(*projetoAberto_);
        catalogWorkspace_->aoAgruparEIrParaTree = [this](std::string folderId) {
            mostrarTree();
            if (treeWorkspace_) {
                treeWorkspace_->selecionarERenomearPasta(folderId);
            }
        };
        addAndMakeVisible(*catalogWorkspace_);
    } else {
        catalogWorkspace_->setVisible(true);
        catalogWorkspace_->recarregar();
    }

    ProgressoGlobal::obterInstancia().concluirTarefa("catalog_view", "Catalog ready");

    resized();
    repaint();
}

void MainComponent::mostrarDuplicates() {
    if (!projetoAberto_) return;
    telaAtiva_ = TelaAtiva::Catalog;

    if (barraNavegacao_) {
        barraNavegacao_->setSelectedTab(BarraNavegacaoComponent::Tab::Duplicates);
        barraNavegacao_->setVisible(true);
    }

    if (homePanel_) homePanel_->setVisible(false);
    if (catalogHubWorkspace_) catalogHubWorkspace_->setVisible(false);
    if (intakeWorkspace_) intakeWorkspace_->setVisible(false);
    if (catalogWorkspace_) catalogWorkspace_->setVisible(false);
    if (analyticsWorkspace_) analyticsWorkspace_->setVisible(false);
    if (treeWorkspace_) treeWorkspace_->setVisible(false);
    if (backupWorkspace_) backupWorkspace_->setVisible(false);
    if (storageWorkspace_) storageWorkspace_->setVisible(false);

    if (!duplicatesWorkspace_) {
        duplicatesWorkspace_ = std::make_unique<DuplicatesWorkspaceComponent>(*projetoAberto_);
        addAndMakeVisible(*duplicatesWorkspace_);
    } else {
        duplicatesWorkspace_->setVisible(true);
        duplicatesWorkspace_->recarregar();
    }

    resized();
    repaint();
}

void MainComponent::mostrarAnalytics() {
    if (!projetoAberto_) return;
    telaAtiva_ = TelaAtiva::Preservation;

    if (barraNavegacao_) {
        barraNavegacao_->setSelectedTab(BarraNavegacaoComponent::Tab::Analytics);
        barraNavegacao_->setVisible(true);
    }

    if (homePanel_) homePanel_->setVisible(false);
    if (catalogHubWorkspace_) catalogHubWorkspace_->setVisible(false);
    if (intakeWorkspace_) intakeWorkspace_->setVisible(false);
    if (catalogWorkspace_) catalogWorkspace_->setVisible(false);
    if (duplicatesWorkspace_) duplicatesWorkspace_->setVisible(false);
    if (treeWorkspace_) treeWorkspace_->setVisible(false);
    if (backupWorkspace_) backupWorkspace_->setVisible(false);
    if (storageWorkspace_) storageWorkspace_->setVisible(false);

    if (!analyticsWorkspace_) {
        analyticsWorkspace_ = std::make_unique<EstatisticasComponent>(*projetoAberto_);
        analyticsWorkspace_->aoAbrirNoGrid = [this](const std::set<std::string>& ids) {
            mostrarGrid();
            if (catalogWorkspace_) {
                catalogWorkspace_->filtrarPorIds(ids);
            }
        };
        analyticsWorkspace_->aoClicarNeedsAttention = [this](const std::set<std::string>& ids) {
            mostrarGrid();
            if (catalogWorkspace_) {
                catalogWorkspace_->filtrarPorIds(ids);
            }
        };
        addAndMakeVisible(*analyticsWorkspace_);
    } else {
        analyticsWorkspace_->setVisible(true);
        analyticsWorkspace_->recarregar();
    }

    resized();
    repaint();
}

void MainComponent::mostrarTree() {
    if (!projetoAberto_) return;
    telaAtiva_ = TelaAtiva::Catalog;

    if (barraNavegacao_) {
        barraNavegacao_->setSelectedTab(BarraNavegacaoComponent::Tab::Tree);
        barraNavegacao_->setVisible(true);
    }

    if (homePanel_) homePanel_->setVisible(false);
    if (catalogHubWorkspace_) catalogHubWorkspace_->setVisible(false);
    if (intakeWorkspace_) intakeWorkspace_->setVisible(false);
    if (catalogWorkspace_) catalogWorkspace_->setVisible(false);
    if (duplicatesWorkspace_) duplicatesWorkspace_->setVisible(false);
    if (analyticsWorkspace_) analyticsWorkspace_->setVisible(false);
    if (backupWorkspace_) backupWorkspace_->setVisible(false);
    if (storageWorkspace_) storageWorkspace_->setVisible(false);

    if (!treeWorkspace_) {
        treeWorkspace_ = std::make_unique<ArvoreBackupComponent>(*projetoAberto_);
        treeWorkspace_->aoMostrarConteudoNaGrade = [this](const std::set<std::string>& itemIds) {
            mostrarGrid();
            if (catalogWorkspace_) {
                catalogWorkspace_->definirSelecaoItens(itemIds);
            } else if (mosaico_) {
                mosaico_->definirFiltroItens(itemIds);
                mosaico_->definirSelecao(itemIds);
            }
        };
        addAndMakeVisible(*treeWorkspace_);
    } else {
        treeWorkspace_->setVisible(true);
        treeWorkspace_->recarregar();
    }

    resized();
    repaint();
}

void MainComponent::mostrarIngestWizard() {
    if (!projetoAberto_) return;

    auto chooser = std::make_shared<juce::FileChooser>(
        "Select files or folders to ingest",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*");

    juce::Component::SafePointer<MainComponent> safeThis(this);
    chooser->launchAsync(
        juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories |
            juce::FileBrowserComponent::canSelectMultipleItems,
        [safeThis, chooser](const juce::FileChooser& fc) {
            if (!safeThis) return;
            auto results = fc.getResults();
            if (results.isEmpty()) return;

            juce::Array<juce::File> arquivos;
            for (auto& f : results) arquivos.add(f);
            safeThis->ingerirArquivos(arquivos);
            safeThis->mostrarIntake();
        });
}

void MainComponent::mostrarBackup() {
    if (!projetoAberto_) return;
    telaAtiva_ = TelaAtiva::Backup;

    if (barraNavegacao_) {
        barraNavegacao_->setSelectedTab(BarraNavegacaoComponent::Tab::Backup);
        barraNavegacao_->setVisible(true);
    }

    if (homePanel_) homePanel_->setVisible(false);
    if (catalogHubWorkspace_) catalogHubWorkspace_->setVisible(false);
    if (intakeWorkspace_) intakeWorkspace_->setVisible(false);
    if (catalogWorkspace_) catalogWorkspace_->setVisible(false);
    if (duplicatesWorkspace_) duplicatesWorkspace_->setVisible(false);
    if (analyticsWorkspace_) analyticsWorkspace_->setVisible(false);
    if (treeWorkspace_) treeWorkspace_->setVisible(false);
    if (storageWorkspace_) storageWorkspace_->setVisible(false);

    std::set<std::string> selected;
    if (catalogWorkspace_) selected = catalogWorkspace_->itensSelecionados();
    else if (mosaico_) selected = mosaico_->itensSelecionados();

    backupWorkspace_ = std::make_unique<BackupWorkspaceComponent>(*projetoAberto_, selected);
    juce::Component::SafePointer<MainComponent> safeThis(this);
    backupWorkspace_->aoConcluir = [safeThis] {
        juce::MessageManager::callAsync([safeThis] { if (safeThis) safeThis->mostrarGrid(); });
    };
    backupWorkspace_->aoVoltarHome = [safeThis] {
        juce::MessageManager::callAsync([safeThis] { if (safeThis) safeThis->mostrarGrid(); });
    };
    backupWorkspace_->aoAbrirCatalogo = [safeThis](const juce::File& pastaBackup) {
        juce::MessageManager::callAsync([safeThis, pastaBackup] { if (safeThis) safeThis->abrirCatalogo(pastaBackup); });
    };
    addAndMakeVisible(*backupWorkspace_);

    resized();
    repaint();
}

void MainComponent::mostrarStorage() {
    if (!projetoAberto_) return;
    telaAtiva_ = TelaAtiva::Backup;

    if (barraNavegacao_) {
        barraNavegacao_->setSelectedTab(BarraNavegacaoComponent::Tab::Storage);
        barraNavegacao_->setVisible(true);
    }

    if (homePanel_) homePanel_->setVisible(false);
    if (catalogHubWorkspace_) catalogHubWorkspace_->setVisible(false);
    if (intakeWorkspace_) intakeWorkspace_->setVisible(false);
    if (catalogWorkspace_) catalogWorkspace_->setVisible(false);
    if (duplicatesWorkspace_) duplicatesWorkspace_->setVisible(false);
    if (analyticsWorkspace_) analyticsWorkspace_->setVisible(false);
    if (treeWorkspace_) treeWorkspace_->setVisible(false);
    if (backupWorkspace_) backupWorkspace_->setVisible(false);

    if (!storageWorkspace_) {
        storageWorkspace_ = std::make_unique<StorageWorkspaceComponent>(*projetoAberto_);
        addAndMakeVisible(*storageWorkspace_);
    } else {
        storageWorkspace_->setVisible(true);
        storageWorkspace_->recarregar();
    }

    resized();
    repaint();
}

void MainComponent::mostrarPreservation() {
    if (!projetoAberto_) return;
    telaAtiva_ = TelaAtiva::Preservation;

    homePanel_.reset();
    ingestWizard_.reset();
    backupWorkspace_.reset();
    if (catalogWorkspace_) catalogWorkspace_->setVisible(false);

    // Hide project layout
    if (barraFerramentas_) barraFerramentas_->setVisible(false);
    if (transport_) transport_->setVisible(false);
    if (mosaicoViewport_) mosaicoViewport_->setVisible(false);
    if (arvoreOrigemViewport_) arvoreOrigemViewport_->setVisible(false);
    if (arvoreAcervoViewport_) arvoreAcervoViewport_->setVisible(false);
    if (divisor1_) divisor1_->setVisible(false);
    if (divisor2_) divisor2_->setVisible(false);
    if (divisorFicha_) divisorFicha_->setVisible(false);
    if (labelSource_) labelSource_->setVisible(false);
    if (botaoSourceLista_) botaoSourceLista_->setVisible(false);
    if (botaoSourceIcones_) botaoSourceIcones_->setVisible(false);
    if (labelBackupTree_) labelBackupTree_->setVisible(false);
    if (botaoBackupLista_) botaoBackupLista_->setVisible(false);
    if (botaoBackupIcones_) botaoBackupIcones_->setVisible(false);
    if (botaoNovaPastaAcervo_) botaoNovaPastaAcervo_->setVisible(false);
    if (filtrosViewport_) filtrosViewport_->setVisible(false);
    if (fichaPanel_) fichaPanel_->setVisible(false);
    if (barraAcoesFicha_) barraAcoesFicha_->setVisible(false);
    if (preview_) preview_->setVisible(false);
    if (escuta_) escuta_->setVisible(false);

    if (barraNavegacao_) {
        barraNavegacao_->setSelectedTab(BarraNavegacaoComponent::Tab::Analytics);
        barraNavegacao_->setVisible(true);
    }

    preservationWorkspace_ = std::make_unique<PreservationWorkspaceComponent>(*projetoAberto_);
    preservationWorkspace_->aoClicarMetrica = [this](const std::string& chave) {
        mostrarCatalog();
        if (!catalogWorkspace_ || !projetoAberto_) return;

        if (chave == "total") {
            catalogWorkspace_->filtrarPorChave("all");
        } else if (chave == "verified" || chave == "corrompido" || chave == "falha_integridade") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                std::string sql = (chave == "verified")
                    ? "SELECT DISTINCT item_id FROM arquivo WHERE checksum_verificado_em IS NOT NULL AND estado_presenca = 'presente'"
                    : "SELECT DISTINCT item_id FROM arquivo WHERE estado_presenca = 'corrompido'";
                auto stmt = db.prepare(sql);
                while (stmt.step()) ids.insert(stmt.columnText(0));
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "persistent_id") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                auto stmt = db.prepare("SELECT id FROM item WHERE persistent_id IS NOT NULL");
                while (stmt.step()) ids.insert(stmt.columnText(0));
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "fixity_sha256") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                auto stmt = db.prepare("SELECT DISTINCT item_id FROM arquivo WHERE checksum_sha256 IS NOT NULL");
                while (stmt.step()) ids.insert(stmt.columnText(0));
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "sem_fixity") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                auto stmt = db.prepare(
                    "SELECT id FROM item WHERE NOT EXISTS "
                    "(SELECT 1 FROM arquivo WHERE arquivo.item_id = item.id AND checksum_sha256 IS NOT NULL)");
                while (stmt.step()) ids.insert(stmt.columnText(0));
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "fixity_verificada") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                auto stmt = db.prepare(
                    "SELECT DISTINCT item_id FROM preservation_event "
                    "WHERE event_type = 'FIXITY_CHECK' AND event_outcome = 'SUCCESS'");
                while (stmt.step()) ids.insert(stmt.columnText(0));
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "formato_identificado") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                auto stmt = db.prepare(
                    "SELECT DISTINCT item_id FROM arquivo "
                    "WHERE caracteristicas_tecnicas_json IS NOT NULL AND caracteristicas_tecnicas_json <> '{}'");
                while (stmt.step()) ids.insert(stmt.columnText(0));
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "backup_verificado") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                auto stmt = db.prepare(
                    "SELECT DISTINCT item_id FROM preservation_event "
                    "WHERE event_type = 'BACKUP_VERIFIED' AND event_outcome = 'SUCCESS'");
                while (stmt.step()) ids.insert(stmt.columnText(0));
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "sem_backup") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                auto stmt = db.prepare(
                    "SELECT id FROM item WHERE NOT EXISTS "
                    "(SELECT 1 FROM preservation_event pe "
                    " WHERE pe.item_id = item.id AND pe.event_type = 'BACKUP_VERIFIED' AND pe.event_outcome = 'SUCCESS')");
                while (stmt.step()) ids.insert(stmt.columnText(0));
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "direitos_desconhecidos") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                auto stmt = db.prepare(
                    "SELECT id FROM item WHERE NOT EXISTS "
                    "(SELECT 1 FROM preservation_right pr WHERE pr.item_id = item.id AND pr.rights_status <> 'UNKNOWN')");
                while (stmt.step()) ids.insert(stmt.columnText(0));
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "formato_risco") {
            std::set<std::string> ids;
            try {
                auto& db = projetoAberto_->projeto().registro();
                auto stmt = db.prepare("SELECT DISTINCT item_id FROM arquivo WHERE caminho_relativo IS NOT NULL");
                while (stmt.step()) {
                    auto itemId = stmt.columnText(0);
                    auto stmtArq = db.prepare("SELECT caminho_relativo FROM arquivo WHERE item_id = ?");
                    stmtArq.bind(1, matriz::db::Value::of(itemId));
                    while (stmtArq.step()) {
                        auto path = stmtArq.columnText(0);
                        auto dot = path.rfind('.');
                        if (dot != std::string::npos) {
                            auto ext = path.substr(dot + 1);
                            if (matriz::preservation::classificarRiscoFormato(ext) == "AT_RISK") {
                                ids.insert(itemId);
                                break;
                            }
                        }
                    }
                }
            } catch (...) {}
            catalogWorkspace_->filtrarPorIds(std::move(ids));
        } else if (chave == "eventos") {
            catalogWorkspace_->filtrarPorChave("all");
        } else if (chave == "regra_321" || chave == "vault_refresh" || chave == "compliance_score") {
            catalogWorkspace_->filtrarPorChave("all");
        } else {
            catalogWorkspace_->filtrarPorChave(chave);
        }
    };
    addAndMakeVisible(*preservationWorkspace_);

    resized();
    repaint();
}

void MainComponent::timerCallback() {
    // Um timer só, dois trabalhos com ritmos muito diferentes:
    //
    //  - acompanhar o lote de ingest: 10 Hz, pra a grade parecer viva;
    //  - reavaliar Vaults montados: a cada 5 s, porque conectar um disco não
    //    é evento que precise de resposta em décimos de segundo.
    if (loteEmCurso_) {
        finalizarUnidadeDeLote(estadoLoteAtual_, nullptr);
        if (pendentes_->load() <= 0) {
            loteEmCurso_ = false;
            estadoLoteAtual_.reset();
        }
    }

    if (++ticksDoTimer_ >= 50) {
        ticksDoTimer_ = 0;
        verificarVaultsConectados();
    }

    // Sem lote em curso, o timer volta ao ritmo lento — 10 Hz com a janela
    // parada seria acordar o processo 10 vezes por segundo à toa.
    if (!loteEmCurso_ && getTimerInterval() != 5000) startTimer(5000);
}

void MainComponent::verificarVaultsConectados() {
    MATRIZ_TRACE("MainComponent::verificarVaultsConectados");
    if (!projetoAberto_ || reconciliacaoEmAndamento_) return;

    // Durante um lote de ingest não vale a pena: a pool está saturada (a
    // varredura entraria na fila atrás de milhares de arquivos) e o quadro
    // de Vaults muda a cada arquivo. Reconectar um disco espera o lote.
    if (ingestEmAndamento()) return;

    // reavaliarVaults() parece barato — "só compara caminho e UUID" — mas o
    // UUID vem do DiskArbitration, que é IPC síncrono com o daemon do
    // sistema. Sob carga isso bloqueia por segundos, e era o pico de 17 s
    // medido na message thread. Vai pra background como todo o resto.
    reconciliacaoEmAndamento_ = true;
    juce::Component::SafePointer<MainComponent> safeThis(this);
    ProjetoAberto* projeto = projetoAberto_.get();

    poolVaults_.addJob([safeThis, projeto]() {
        std::vector<std::string> reconectados;
        try {
            reconectados = projeto->reavaliarVaults();
        } catch (const std::exception&) {
        }

        juce::MessageManager::callAsync([safeThis, reconectados]() {
            if (!safeThis) return;
            safeThis.getComponent()->aoTerminarReavaliacaoDeVaults(reconectados);
        });
    });
}

void MainComponent::atualizarCacheDeTamanhoTotal() {
    if (!projetoAberto_ || calculandoTamanhoTotal_) return;
    calculandoTamanhoTotal_ = true;

    juce::Component::SafePointer<MainComponent> safeThis(this);
    ProjetoAberto* projeto = projetoAberto_.get();

    poolVaults_.addJob([safeThis, projeto]() {
        juce::int64 total = 0;
        try {
            total = projeto->tamanhoTotalDosMasters();
        } catch (const std::exception&) {
        }

        juce::MessageManager::callAsync([safeThis, total]() {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            self->calculandoTamanhoTotal_ = false;
            if (self->tamanhoTotalEmCache_ == total) return;
            self->tamanhoTotalEmCache_ = total;
            (void)self;
        });
    });
}

void MainComponent::aoTerminarReavaliacaoDeVaults(const std::vector<std::string>& reconectados) {
    if (!projetoAberto_) {
        reconciliacaoEmAndamento_ = false;
        return;
    }

    if (filtros_) filtros_->recarregar();  // bolinha verde/cinza muda na hora
    if (reconectados.empty()) {
        reconciliacaoEmAndamento_ = false;
        return;
    }

    textoProgressoIngest_ = matriz::i18n::t("vault.reconciliando");
    ProgressoGlobal::obterInstancia().iniciarTarefa("vault_reconcile", "Reconciling Vaults", 0, nullptr, "Scanning connected volumes...");

    matriz::db::Database* registro = &projetoAberto_->projeto().registro();
    juce::Component::SafePointer<MainComponent> safeThis(this);

    poolVaults_.addJob([registro, reconectados, safeThis]() mutable {
        matriz::vault::ResumoReconciliacao total;
        for (const auto& vaultId : reconectados) {
            auto r = matriz::vault::varreduraRapida(*registro, vaultId);
            total.novos += r.novos;
            total.movidos += r.movidos;
            total.ausentes += r.ausentes;
            total.alterados += r.alterados;
        }

        juce::MessageManager::callAsync([safeThis, total]() {
            if (!safeThis) return;
            safeThis->concluirReconciliacao(total);
        });
    });
}

void MainComponent::concluirReconciliacao(const matriz::vault::ResumoReconciliacao& resumo) {
    reconciliacaoEmAndamento_ = false;
    if (!projetoAberto_) return;

    // Aviso discreto na faixa, nunca um modal (§8): reconectar um disco não
    // é um evento que exige resposta do operador.
    textoProgressoIngest_ = resumo.comoTexto();
    ProgressoGlobal::obterInstancia().concluirTarefa("vault_reconcile", resumo.comoTexto());

    if (mosaico_) mosaico_->recarregar();
    if (filtros_) filtros_->recarregar();
    atualizarPainelDeApoio();
}

void MainComponent::salvarProjeto() {
    if (!projetoAberto_) return;
    try {
        projetoAberto_->salvar();
        if (auto* win = findParentComponentOfClass<juce::DocumentWindow>()) {
            auto titulo = win->getName();
            win->setName(titulo + "  [Saved]");
            juce::Component::SafePointer<juce::DocumentWindow> safeWin(win);
            juce::Timer::callAfterDelay(1500, [safeWin, titulo] {
                if (safeWin) safeWin->setName(titulo);
            });
        }
    } catch (...) {}
}

juce::File MainComponent::pastaProjeto() const {
    if (!projetoAberto_) return {};
    return projetoAberto_->projeto().pasta();
}

void MainComponent::fecharProjeto() {
    if (catalogo_) {
        catalogo_.reset();
        catalogoViewport_.reset();
        catalogoTitulo_.reset();
        catalogoBusca_.reset();
        catalogoFechar_.reset();
        reconstruirTelaInicial();
        if (aoMudarEstadoProjeto) aoMudarEstadoProjeto();
        return;
    }

    if (ingestEmAndamento()) return;

    if (projetoAberto_ && projetoAberto_->isDirty()) {
        int res = juce::AlertWindow::showYesNoCancelBox(
            juce::AlertWindow::QuestionIcon,
            "Unsaved Changes",
            "This project has unsaved changes (such as relinked asset paths).\n\nDo you want to save your changes before closing?",
            "Save", "Don't Save", "Cancel");
        if (res == 1) { // Save
            salvarProjeto();
        } else if (res == 2) { // Don't Save
            projetoAberto_->descartarAlteracoesEmMemoria();
        } else { // Cancel
            return;
        }
    }

    stopTimer();
    poolVaults_.removeAllJobs(true, 2000);
    catalogoPai_ = juce::File();
    telaAtiva_ = TelaAtiva::Inicial;
    reconstruirTelaInicial();
    projetoAberto_.reset();
    if (aoMudarEstadoProjeto) aoMudarEstadoProjeto();
}

void MainComponent::verificarPresencaInicialAssets() {
    if (!projetoAberto_) return;
    juce::Component::SafePointer<MainComponent> safeThis(this);
    ProjetoAberto* proj = projetoAberto_.get();
    juce::File pastaProj = pastaProjeto();

    poolVaults_.addJob([safeThis, proj, pastaProj]() {
        matriz::vault::AssetPresenceReport report;
        try {
            report = matriz::vault::AssetRelinkEngine::verificarPresencaAssets(
                proj->projeto().registro(), pastaProj, proj->inMemoryRelinkedPaths());
        } catch (...) {}

        juce::MessageManager::callAsync([safeThis, report]() {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            if (report.totalAssets > 0 && report.onlineAssets == 0 && report.offlineAssets > 0) {
                self->mostrarDialogoRelinkInicial(report);
            }
        });
    });
}

void MainComponent::mostrarDialogoRelinkInicial(const matriz::vault::AssetPresenceReport& report) {
    if (!projetoAberto_) return;
    juce::Component::SafePointer<MainComponent> safeThis(this);

    InitialRelinkDialog::showModal(
        report.sampleMissingExpectedPath,
        juce::String(report.sampleMissingTitle),
        [safeThis, report](const juce::File& fileSelected) {
            if (!safeThis || !safeThis->projetoAberto_) return;
            std::map<std::string, std::string> inMemRelocated;
            auto res = matriz::vault::AssetRelinkEngine::relocarColecaoEmMemoria(
                safeThis->projetoAberto_->projeto().registro(),
                safeThis->pastaProjeto(),
                report.sampleMissingExpectedPath,
                fileSelected,
                inMemRelocated);

            safeThis->projetoAberto_->aplicarBatchRelinkEmMemoria(inMemRelocated);

            if (safeThis->mosaico_) safeThis->mosaico_->recarregar();
            if (safeThis->catalogWorkspace_) safeThis->catalogWorkspace_->recarregar();

            juce::String msg = "ASSET RELOCATION\n\nResolved: " + juce::String(res.resolvedCount) +
                               "\nNot Found: " + juce::String(res.notFoundCount) +
                               "\nOffline: " + juce::String(res.offlineCount) +
                               "\n\nNote: Changes are currently held in memory. Use File -> Save (Cmd+S) to persist.";

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Relocation Summary",
                msg,
                "OK");
        },
        [] {});
}

void MainComponent::abrirDialogoRelinkOffline(const std::string& itemId) {
    if (!projetoAberto_) return;
    std::string titulo, tipoMidia, codigoAcervo;
    projetoAberto_->obterItemInfo(itemId, titulo, tipoMidia, codigoAcervo);

    juce::String expectedPath;
    juce::String storageName = "Local Storage";

    try {
        auto stmt = projetoAberto_->projeto().registro().prepare(
            "SELECT a.caminho_relativo, COALESCE(a.caminho_absoluto_origem, ''), COALESCE(v.nome, 'Local Storage'), COALESCE(v.localizacao, '') "
            "FROM arquivo a "
            "LEFT JOIN vault v ON v.id = a.vault_id "
            "WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) {
            std::string camRel = stmt.columnText(0);
            std::string camAbs = stmt.columnText(1);
            storageName = stmt.columnText(2);
            std::string locVault = stmt.columnText(3);
            auto expFile = matriz::vault::caminhoEsperado(projetoAberto_->projeto().pasta(), locVault, camRel, camAbs);
            expectedPath = expFile != juce::File() ? expFile.getFullPathName() : (camAbs.empty() ? camRel : camAbs);
        }
    } catch (...) {}

    juce::Component::SafePointer<MainComponent> safeThis(this);
    OfflineAssetRelinkDialog::showModal(
        projetoAberto_->projeto().registro(),
        itemId,
        juce::String(titulo),
        expectedPath,
        storageName,
        [safeThis, itemId](const juce::File& fileSelected) {
            if (!safeThis || !safeThis->projetoAberto_) return;
            std::string novoItemId;
            juce::String err;
            bool ok = matriz::vault::AssetRelinkEngine::executarRelinkIndividual(
                safeThis->projetoAberto_->projeto().registro(),
                safeThis->projetoAberto_->projeto().pasta(),
                itemId,
                fileSelected,
                true,
                novoItemId,
                err);
            if (ok) {
                safeThis->projetoAberto_->salvar();
                if (safeThis->mosaico_) safeThis->mosaico_->recarregar();
                if (safeThis->catalogWorkspace_) safeThis->catalogWorkspace_->recarregar();
                if (safeThis->fichaPanel_) safeThis->fichaPanel_->mostrarItem(novoItemId);
            }
        });
}

bool MainComponent::temPainelInconsistencias() const {
    return catalogWorkspace_ != nullptr && catalogWorkspace_->isVisible();
}

int MainComponent::totalInconsistencias() const {
    return (catalogWorkspace_ != nullptr && catalogWorkspace_->isVisible()) ? 1 : 0;
}

juce::String MainComponent::textoProgressoIngestParaTeste() const {
    return textoProgressoIngest_;
}

std::unique_ptr<matriz::model::Project> MainComponent::destacarProjeto() {
    if (!projetoAberto_) return nullptr;
    jassert(!ingestEmAndamento());
    return projetoAberto_->destacarProjeto();
}

void MainComponent::selecionarItem(const std::string& itemId) {
    MATRIZ_TRACE("MainComponent::selecionarItem");
    if (!fichaPanel_) return;

    // Clique único abre o painel direito, sem duplo clique e sem menu — o
    // painel era colapsado por padrão e só abria por um botão que não dizia
    // o que tinha dentro, então na prática ninguém via a ficha.
    if (!fichaAberta_ && !itemId.empty()) alternarFicha();
    // Seleção múltipla (§12.2, item 8): 2+ itens marcados na grade entra em
    // modo lote na ficha, não só o último clicado (itemId, a âncora).
    if (mosaico_ && mosaico_->itensSelecionados().size() > 1) {
        std::vector<std::string> ids(mosaico_->itensSelecionados().begin(), mosaico_->itensSelecionados().end());
        fichaPanel_->mostrarSelecao(ids);
    } else {
        fichaPanel_->mostrarItem(itemId);
    }

}

void MainComponent::atualizarBarraMetricas() {}

void MainComponent::ingerirArquivos(const juce::Array<juce::File>& arquivosOuPastas) {
    if (!projetoAberto_) return;

    auto arquivos = expandirArquivos(arquivosOuPastas);
    if (arquivos.empty()) return;

    processarLoteEmBackground(std::move(arquivos), "", "");
}

// Navegador estilo Finder embutido (item 3) — segunda porta de entrada,
// coexistindo com arrastar direto do Finder. Ao clicar ADD TO BACKUP, a
// janela fecha sozinha (item 3.1) e a seleção entra pelo MESMO caminho de
// ingest do arrastar, então tudo o que já valia (subárvore inteira, dedup
// por checksum, resumo de fim de lote) vale igual aqui.
void MainComponent::abrirNavegadorArquivos() {
    if (!projetoAberto_) return;

    janelaNavegador_ = mostrarNavegadorArquivos(
        ultimaLocalizacaoNavegador_,
        [this](ResultadoNavegador r) {
            juce::Array<juce::File> selecionados;
            for (auto& f : r.selecionados) selecionados.add(f);
            // manterEstrutura=false achata: entra só o que é arquivo, sem
            // trazer as pastas como nível. expandirArquivos já é recursivo,
            // então a diferença está em preservar ou não a subárvore no
            // Acervo — a mesma escolha que o drop da árvore oferece.
            if (r.manterEstrutura) {
                ingerirArquivos(selecionados);
            } else {
                juce::Array<juce::File> soArquivos;
                for (auto& f : selecionados) {
                    if (f.isDirectory())
                        for (const auto& dentro :
                             f.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, true))
                            soArquivos.add(dentro);
                    else
                        soArquivos.add(f);
                }
                ingerirArquivos(soArquivos);
            }
        },
        [this](juce::File pasta) { ultimaLocalizacaoNavegador_ = pasta; });
}

void MainComponent::filesDropped(const juce::StringArray& files, int x, int y) {
    matriz::diag::breadcrumb("filesDropped");
    arrastandoArquivo_ = false;
    if (mosaico_) mosaico_->definirArrastandoArquivo(false);
    repaint();
    if (!temProjetoAberto()) return;

    if (ingestWizard_ && ingestWizard_->isInterestedInFileDrag(files)) {
        ingestWizard_->filesDropped(files, x, y);
        return;
    }

    juce::Array<juce::File> arquivos;
    for (auto& caminho : files) arquivos.add(juce::File(caminho));
    ingerirArquivos(arquivos);
}

void MainComponent::fileDragEnter(const juce::StringArray&, int, int) {
    matriz::diag::breadcrumb("fileDragEnter");
    arrastandoArquivo_ = true;
    if (mosaico_) mosaico_->definirArrastandoArquivo(true);
    repaint();
}

void MainComponent::fileDragExit(const juce::StringArray&) {
    matriz::diag::breadcrumb("fileDragExit");
    arrastandoArquivo_ = false;
    if (mosaico_) mosaico_->definirArrastandoArquivo(false);
    repaint();
}

std::vector<juce::File> MainComponent::expandirArquivos(const juce::Array<juce::File>& arquivosOuPastas) const {
    std::vector<juce::File> arquivos;
    for (auto& entrada : arquivosOuPastas) {
        if (entrada.isDirectory()) {
            for (auto& sub : juce::RangedDirectoryIterator(entrada, true, "*", juce::File::findFiles)) {
                juce::File f = sub.getFile();
                juce::String name = f.getFileName();
                juce::String ext = f.getFileExtension().trimCharactersAtStart(".").toLowerCase();

                if (name.startsWith(".") || f.getSize() == 0) continue;
                if (ext == "sfk" || ext == "reapeaks" || ext == "asd") continue;

                arquivos.push_back(f);
            }
        } else {
            juce::String name = entrada.getFileName();
            juce::String ext = entrada.getFileExtension().trimCharactersAtStart(".").toLowerCase();

            if (name.startsWith(".") || entrada.getSize() == 0) continue;
            if (ext == "sfk" || ext == "reapeaks" || ext == "asd") continue;

            arquivos.push_back(entrada);
        }
    }
    return arquivos;
}

void MainComponent::processarLoteEmBackground(std::vector<juce::File> arquivos,
                                                const std::string& sourceMedia,
                                                const std::string& collection) {
    if (!projetoAberto_) return;

    // Ponteiro, não referência: a referência local morreria ao sair desta
    // função, mas os jobs do ThreadPool rodam depois disso — precisam de
    // algo que sobreviva. O objeto em si só é seguro enquanto o projeto
    // continuar aberto, por isso o menu (e fecharProjeto()) ficam travados
    // com ingestEmAndamento() até o último job terminar.
    matriz::db::Database* registro = &projetoAberto_->projeto().registro();
    matriz::db::Database* indice = &projetoAberto_->projeto().indice();
    juce::File pastaProjeto = projetoAberto_->projeto().pasta();

    auto stmtProjeto = registro->prepare("SELECT id, prefixo_nomenclatura FROM projeto LIMIT 1");
    if (!stmtProjeto.step()) return;
    std::string projetoId = stmtProjeto.columnText(0);
    juce::String prefixo = stmtProjeto.columnText(1);

    std::vector<std::string> itemIds;
    itemIds.reserve(arquivos.size());
    registro->run("BEGIN", {});
    try {
        for (auto& arquivo : arquivos) {
            std::string itemId = matriz::model::novoUuid();
            std::string agora = matriz::model::agoraIso8601();

            auto cat = matriz::ingest::categoriaPorExtensao(arquivo);
            std::string tipoMidia;
            switch (cat) {
                case matriz::ingest::CategoriaMidia::Audio:      tipoMidia = "digital_audio"; break;
                case matriz::ingest::CategoriaMidia::Video:      tipoMidia = "digital_video"; break;
                case matriz::ingest::CategoriaMidia::Imagem:     tipoMidia = "foto"; break;
                case matriz::ingest::CategoriaMidia::Documento:  tipoMidia = "documento"; break;
                case matriz::ingest::CategoriaMidia::Texto:      tipoMidia = "documento"; break;
                case matriz::ingest::CategoriaMidia::Sessao:     tipoMidia = "sessao"; break;
                default: break;
            }

            std::string estado = tipoMidia.empty() ? "capturado" : "catalogado";
            auto tipoVal = tipoMidia.empty() ? matriz::db::Value::null() : matriz::db::Value::of(tipoMidia);

            registro->run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, em_quarentena, criado_em, atualizado_em) "
                "VALUES (?, ?, NULL, ?, ?, ?, 1, ?, ?)",
                {matriz::db::Value::of(itemId), matriz::db::Value::of(projetoId),
                 matriz::db::Value::of(arquivo.getFileNameWithoutExtension().toStdString()),
                 tipoVal, matriz::db::Value::of(estado),
                 matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

            if (!tipoMidia.empty()) {
                auto origem = matriz::ficha::origemPadraoParaTipo(tipoMidia);
                if (origem) {
                    registro->run(
                        "INSERT OR IGNORE INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                        "VALUES (?, ?, 'raiz', 0, 'origem', ?, 'leitura_tecnica', ?)",
                        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                         matriz::db::Value::of(*origem), matriz::db::Value::of(agora)});
                }
            }

            itemIds.push_back(itemId);
        }
        registro->run("COMMIT", {});
    } catch (...) {
        registro->run("ROLLBACK", {});
        throw;
    }

    if (!sourceMedia.empty() || !collection.empty()) {
        std::string agora = matriz::model::agoraIso8601();
        for (auto& id : itemIds) {
            if (!sourceMedia.empty()) {
                registro->run(
                    "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                    "VALUES (?, ?, 'raiz', 0, 'source_media', ?, 'humano', ?) "
                    "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) "
                    "DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em "
                    "WHERE item_campo.fonte != 'leitura_tecnica'",
                    {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id),
                     matriz::db::Value::of(sourceMedia), matriz::db::Value::of(agora)});
            }
            if (!collection.empty()) {
                registro->run(
                    "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                    "VALUES (?, ?, 'raiz', 0, 'collection_type', ?, 'humano', ?) "
                    "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) "
                    "DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em "
                    "WHERE item_campo.fonte != 'leitura_tecnica'",
                    {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id),
                     matriz::db::Value::of(collection), matriz::db::Value::of(agora)});
            }
        }
    }

    if (!loteEmCurso_) {
        ingestsTotalLote_.store(0);
        pendentes_->store(0);
    }
    ingestsTotalLote_ += static_cast<int>(arquivos.size());
    *pendentes_ += static_cast<int>(arquivos.size());
    atualizarLabelProgresso();

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "ingest",
        "Ingest",
        static_cast<int>(arquivos.size()),
        [this] { cancelarLoteIngest(); },
        "Processing " + juce::String(arquivos.size()) + " files...");

    mostrarIntake();
    if (intakeWorkspace_) intakeWorkspace_->recarregar();
    if (mosaico_) mosaico_->recarregar();

    // Um lote novo rearma o token — cancelar um lote não pode deixar todos
    // os seguintes nascendo cancelados.
    if (!cancelamentoLote_) cancelamentoLote_ = matriz::app::novoCancelamento();
    cancelamentoLote_->rearmar();
    auto cancelamento = cancelamentoLote_;

    auto estadoLote = std::make_shared<EstadoLote>();
    estadoLote->todosItemIds = itemIds;
    estadoLoteAtual_ = estadoLote;
    loteEmCurso_ = true;

    juce::Component::SafePointer<MainComponent> safeThis(this);
    ingestModalDialog_ = IngestProgressModalDialog::showModal(static_cast<int>(arquivos.size()), [safeThis] {
        if (safeThis) safeThis->cancelarLoteIngest();
    });

    // 100 ms: rápido o bastante pra grade parecer viva, devagar o bastante
    // pra o acompanhamento do lote custar 10 idas à message thread por
    // segundo em vez de uma por arquivo.
    startTimer(100);
    // Atualiza a grade periodicamente durante o lote (não a cada arquivo —
    // custaria uma consulta completa por arquivo em lotes de milhares) pra
    // as miniaturas irem preenchendo visivelmente atrás, como pedido em
    // §2.1/§2.2, sem esperar o lote inteiro terminar.
    auto contadorParaAtualizar = std::make_shared<std::atomic<int>>(0);
    // Um mutex por LOTE, compartilhado por todos os jobs dele: serializa só a
    // escrita no registro, deixando a análise (o caro) genuinamente paralela.
    auto escritaRegistro = std::make_shared<std::mutex>();
    constexpr int kAtualizarACada = 20;

    for (size_t i = 0; i < arquivos.size(); ++i) {
        juce::File arquivo = arquivos[i];
        std::string itemId = itemIds[i];

        // A cópia + checksum + ffprobe/Exiv2 (matriz::ingest::ingerirArquivo)
        // rodava direto na thread de mensagens antes — era exatamente o
        // travamento reportado. Agora cada arquivo é um job no ThreadPool
        // (1 thread — sequencial, sem concorrência de escrita no registro).
        ingestPool_.addJob([registro, indice, pastaProjeto, itemId, arquivo, estadoLote, safeThis,
                             contadorParaAtualizar, cancelamento, prefixo, escritaRegistro,
                             pendentes = pendentes_]() mutable {
            struct PendentesGuard {
                std::shared_ptr<std::atomic<int>> p;
                ~PendentesGuard() {
                    if (p) p->fetch_sub(1);
                }
            } guard{pendentes};

            juce::String erro;
            bool sucesso = false;
            bool duplicata = false;

            // Checagem ANTES de tocar no arquivo: é o que faz o cancelamento
            // ser imediato pro operador. O arquivo que já estava em curso
            // termina; deste aqui pra frente, ninguém copia mais nada.
            if (cancelamento->pedido()) {
                {
                    const juce::ScopedLock sl(estadoLote->lock);
                    estadoLote->cancelado = true;
                    estadoLote->naoProcessados.push_back(itemId);
                }
                try {
                    const std::lock_guard<std::mutex> lock(*escritaRegistro);
                    registro->run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(itemId)});
                } catch (...) {}
                return;
            }

            auto inicio = std::chrono::system_clock::now();
            juce::int64 bytes = arquivo.getSize();
            std::string workerId = "WorkerThread-" + std::to_string((uint64_t)juce::Thread::getCurrentThreadId());

            try {
                // FASE 1 — cara e PURA: checksum, ffprobe/Exiv2, decodificação.
                // Não toca no banco, então N threads fazem isto ao mesmo tempo
                // (critério 3: ingestão concorrente).
                auto analise = matriz::ingest::analisarArquivo(arquivo);
                auto categoria = matriz::ingest::categoriaPorExtensao(arquivo);

                // FASE 1b — também pura, e a mais cara de todas: LUFS, forma de
                // onda, miniatura. Fica FORA do lock pelo mesmo motivo.
                matriz::ingest::AnaliseCache cache;
                if (!analise.ehPlaceholderNuvem)
                    cache = matriz::ingest::calcularCache(arquivo, categoria, pastaProjeto,
                                                          analise.leitura.duracaoSegundos);

                // FASE 2 — banco. Sob lock: o handle SQLite é um só, e a
                // numeração do acervo precisa de exclusão mútua pra dois
                // arquivos não gastarem o mesmo sequencial.
                // Auto-classify tipo_midia based on extension category so it maps to the correct Grid filter tabs
                std::string tipoMidia = "";
                if (categoria == matriz::ingest::CategoriaMidia::Audio) tipoMidia = "digital_audio";
                else if (categoria == matriz::ingest::CategoriaMidia::Video) tipoMidia = "digital_video";
                else if (categoria == matriz::ingest::CategoriaMidia::Imagem) tipoMidia = "foto";
                else if (categoria == matriz::ingest::CategoriaMidia::Documento) tipoMidia = "documento";
                else if (categoria == matriz::ingest::CategoriaMidia::Texto) tipoMidia = "documento";
                else if (categoria == matriz::ingest::CategoriaMidia::Sessao) tipoMidia = "sessao";

                std::string arquivoIdGravado;
                {
                    const std::lock_guard<std::mutex> lock(*escritaRegistro);

                    PapelInfo papelInfo = papelPorCategoria(categoria);
                    auto resultado = matriz::ingest::gravarArquivoAnalisado(*registro, itemId, analise,
                                                                              papelInfo.papel,
                                                                              papelInfo.ehMaster);
                    arquivoIdGravado = resultado.arquivoId;

                    // Sequencial permanente SÓ no sucesso (critério 6).
                    // Busca o maior sufixo numérico existente para este prefixo no banco,
                    // evitando conflitos causados por itens excluídos ou lacunas de id.
                    int proximoNumero = 1;
                    auto stmtContagem = registro->prepare(
                        "SELECT COALESCE(MAX(CAST(SUBSTR(codigo_acervo, INSTR(codigo_acervo, '-') + 1) AS INTEGER)), 0) "
                        "FROM item WHERE codigo_acervo LIKE ?");
                    stmtContagem.bind(1, matriz::db::Value::of(prefixo.toStdString() + "-%"));
                    if (stmtContagem.step())
                        proximoNumero = static_cast<int>(stmtContagem.columnInt(0)) + 1;
                    juce::String codigo =
                        prefixo + "-" + juce::String(proximoNumero).paddedLeft('0', 5);

                    // 'novo', não 'catalogado': o arquivo entrou e foi lido,
                    // mas tipo_midia ainda é NULL — ninguém classificou
                    // nada. Quem move pra 'catalogado' é a classificação
                    // (ProjetoAberto::atualizarTipoMidia), que é decisão
                    // humana. Marcar como catalogado aqui faria a coleção
                    // "Incompletos" (§10) e a faixa de orientação mentirem.
                    registro->run("UPDATE item SET codigo_acervo = ?, estado = 'novo', tipo_midia = ? WHERE id = ?",
                                  {matriz::db::Value::of(codigo.toStdString()),
                                   tipoMidia.empty() ? matriz::db::Value::null() : matriz::db::Value::of(tipoMidia),
                                   matriz::db::Value::of(itemId)});

                    matriz::ingest::gravarCache(*registro, arquivoIdGravado, cache);
                    sucesso = true;
                }

                if (!arquivoIdGravado.empty()) {
                    // Miniatura no índice: banco separado, sem disputa com o
                    // registro, então fica fora do lock.
                    matriz::ingest::gerarEGravarMiniaturaPrincipal(*indice, pastaProjeto, itemId,
                                                                     arquivoIdGravado, arquivo, categoria,
                                                                     analise.leitura.duracaoSegundos);
                }

                auto fim = std::chrono::system_clock::now();
                matriz::app::registrarLogOperacao(pastaProjeto,
                                                   duplicata ? "ingest_duplicate" : "ingest_success", itemId,
                                                   workerId, inicio, fim, duplicata ? 0 : bytes);
            } catch (const std::exception& e) {
                erro = arquivo.getFileName() + ": " + juce::String(e.what());
                auto fim = std::chrono::system_clock::now();
                matriz::app::registrarLogOperacao(pastaProjeto, "ingest_error", itemId, workerId, inicio, fim, 0, e.what());

                // Failed item does NOT get a code and is removed from the catalog.
                // We retry a few times in case of database locks.
                try {
                    const std::lock_guard<std::mutex> lock(*escritaRegistro);
                    int retries = 5;
                    while (retries-- > 0) {
                        try {
                            registro->run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(itemId)});
                            break;
                        } catch (...) {
                            if (retries == 0) break;
                            juce::Thread::sleep(50);
                        }
                    }
                } catch (...) {}
            } catch (...) {
                erro = arquivo.getFileName() + ": Unexpected fatal error during ingest";
                try {
                    const std::lock_guard<std::mutex> lock(*escritaRegistro);
                    registro->run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(itemId)});
                } catch (...) {}
            }

            {
                const juce::ScopedLock sl(estadoLote->lock);
                if (sucesso) {
                    ++estadoLote->sucessos;
                    if (duplicata) ++estadoLote->duplicatas;
                } else {
                    estadoLote->erros.push_back(erro);
                }
            }

            double duracaoArquivo = std::chrono::duration<double>(std::chrono::system_clock::now() - inicio).count();
            juce::MessageManager::callAsync([safeThis, duracaoArquivo, nome = arquivo.getFileName(), pendentes]() {
                if (safeThis && safeThis->ingestModalDialog_) {
                    safeThis->ingestModalDialog_->recordFileProcessed(duracaoArquivo);
                    int total = safeThis->ingestsTotalLote_.load();
                    int pend = pendentes->load();
                    safeThis->ingestModalDialog_->updateProgress(juce::jmax(0, total - pend), nome);
                }
            });
        });
    }
}

// Chamado da thread de trabalho, uma vez por arquivo. Deliberadamente
// mínimo: um decremento atômico, nada de banco, nada de UI.
void MainComponent::registrarUnidadeConcluida(const std::shared_ptr<std::atomic<int>>& pendentes) {
    pendentes->fetch_sub(1);
}

// Passo do lote na MESSAGE THREAD, chamado pelo timer — não uma vez por
// arquivo.
//
// Antes, cada job postava um callAsync ao terminar. Com 5.000 arquivos isso
// enfileirava 5.000 callbacks na message thread, e o loop de mensagens os
// drenava em rajadas: mesmo com cada callback custando menos de 35 ms, uma
// volta do loop passava de um segundo. O trabalho era pequeno; o problema
// era a QUANTIDADE de idas e voltas.
//
// Agora o worker só decrementa um átomo (registrarUnidadeConcluida) e a
// message thread consulta o estado no seu próprio ritmo. O custo de
// acompanhar um lote deixa de depender do tamanho do lote.
void MainComponent::finalizarUnidadeDeLote(std::shared_ptr<EstadoLote> estadoLote,
                                            std::shared_ptr<std::atomic<int>> contadorParaAtualizar) {
    MATRIZ_TRACE("MainComponent::finalizarUnidadeDeLote");
    juce::ignoreUnused(contadorParaAtualizar);
    if (estadoLote == nullptr || !projetoAberto_) return;
    constexpr int kIntervaloAtualizacaoMs = 1500;

    bool ultimo = pendentes_->load() <= 0;
    auto agora = juce::Time::getMillisecondCounter();
    bool naHora = static_cast<int>(agora - ultimaAtualizacaoDeLoteMs_) >= kIntervaloAtualizacaoMs;

    // Durante o lote, só o que o operador de fato acompanha: a grade
    // enchendo e os contadores. Os dois são assíncronos — a message thread
    // dispara e segue.
    if (ultimo || naHora) {
        ultimaAtualizacaoDeLoteMs_ = agora;
        // Também throttled: com 5.000 arquivos, redesenhar o rótulo a cada
        // um enfileirava 5.000 repinturas na message thread e o loop as
        // drenava em rajadas de mais de um segundo. Ninguém lê 5.000
        // atualizações de contador — o operador lê o número subindo.
        atualizarLabelProgresso();
        if (mosaico_) mosaico_->recarregar();  // Thread de Snapshot
        if (intakeWorkspace_) intakeWorkspace_->recarregar();
        if (filtros_) filtros_->recarregar();  // agregações em background
        atualizarPainelDeApoio();
    }

    if (!ultimo) return;

    // Daqui pra baixo, UMA vez, no fim do lote. Cada uma destas percorre o
    // acervo inteiro na message thread; rodar a cada 700 ms num lote de
    // 5.000 arquivos era o congelamento medido (17 s numa única volta do
    // loop de mensagens):
    //
    //  - a árvore EXPLORER remonta a hierarquia de caminhos de TODOS os itens;
    //  - a faixa de orientação reconta tipos de mídia;
    //  - o painel de inconsistências relê cada arquivo e recalcula SHA-256
    //    (este já vai pra background sozinho, mas nem faz sentido rodar
    //    antes do lote fechar: o quadro muda a cada arquivo).
    if (arvoreOrigem_) arvoreOrigem_->recarregar();

    int sucessos, duplicatas, totalErros;
    bool cancelado;
    std::vector<std::string> naoProcessados;
    juce::StringArray erros;
    {
        const juce::ScopedLock sl(estadoLote->lock);
        sucessos = estadoLote->sucessos;
        duplicatas = estadoLote->duplicatas;
        totalErros = static_cast<int>(estadoLote->erros.size());
        cancelado = estadoLote->cancelado;
        naoProcessados = estadoLote->naoProcessados;
        for (auto& e : estadoLote->erros) erros.add(e);
    }

    // Limpa os itens que a fase 1 criou mas que nunca foram processados —
    // ver nota em EstadoLote::naoProcessados. Feito aqui, na thread de
    // mensagens, com o pool já vazio: ninguém mais está olhando pra eles.
    if (!naoProcessados.empty() && projetoAberto_) {
        projetoAberto_->removerItensDoProjeto(naoProcessados);
        if (mosaico_) mosaico_->recarregar();
        if (arvoreOrigem_) arvoreOrigem_->recarregar();
        if (arvoreAcervo_) arvoreAcervo_->recarregar();
        if (filtros_) filtros_->recarregar();
        atualizarPainelDeApoio();
        atualizarEtapaDoFluxo();
    }

    if (aoConcluirLoteIngestParaTeste) aoConcluirLoteIngestParaTeste(sucessos, erros);

    if (cancelado) {
        // Spec Section 3 — Atomic Cancel:
        // Abort entirely: all partial progress (all items created in batch and partial cache) discarded.
        if (projetoAberto_ && !estadoLote->todosItemIds.empty()) {
            projetoAberto_->removerItensDoProjeto(estadoLote->todosItemIds);
        }
        if (ingestModalDialog_) {
            ingestModalDialog_->closeDialog();
            ingestModalDialog_ = nullptr;
        }
        if (mosaico_) mosaico_->recarregar();
        if (arvoreOrigem_) arvoreOrigem_->recarregar();
        if (arvoreAcervo_) arvoreAcervo_->recarregar();
        if (filtros_) filtros_->recarregar();
        atualizarPainelDeApoio();
        atualizarEtapaDoFluxo();

        mostrarResumoCancelado(0, static_cast<int>(estadoLote->todosItemIds.size()));
        return;
    }

    if (ingestModalDialog_) {
        ingestModalDialog_->closeDialog();
        ingestModalDialog_ = nullptr;
    }

    // Append entry to project log.md
    if (projetoAberto_) {
        matriz::model::ProjectLog pLog(projetoAberto_->projeto().pasta());
        juce::StringArray details;
        details.add("Files processed: " + juce::String(sucessos));
        details.add("Duplicates recognized: " + juce::String(duplicatas));
        if (totalErros > 0) details.add("Errors encountered: " + juce::String(totalErros));
        pLog.appendEntry("Ingest Batch Completed", details);
    }

    mostrarResumoLote(sucessos - duplicatas, duplicatas, totalErros);
}

void MainComponent::cancelarLoteIngest() {
    if (!cancelamentoLote_ || !ingestEmAndamento()) return;
    cancelamentoLote_->pedir();
    if (ingestModalDialog_) ingestModalDialog_->setCancelling();
    textoProgressoIngest_ = "Cancelling ingestion and rolling back...";
    ProgressoGlobal::obterInstancia().atualizarDetalhe("ingest", "Cancelling batch...");
}

void MainComponent::mostrarResumoCancelado(int processados, int total) {
    textoProgressoIngest_ = matriz::i18n::t("ingest.cancelado")
                                .replace("{feito}", juce::String(processados))
                                .replace("{total}", juce::String(total));
    ProgressoGlobal::obterInstancia().concluirTarefa("ingest", textoProgressoIngest_);
}

void MainComponent::atualizarLabelProgresso() {
    MATRIZ_TRACE("MainComponent::atualizarLabelProgresso");
    int pendentes = juce::jmax(0, pendentes_->load());
    int total = ingestsTotalLote_.load();
    int concluidos = juce::jmin(total, juce::jmax(0, total - pendentes));

    if (total > 0) {
        ProgressoGlobal::obterInstancia().atualizarProgresso(
            "ingest",
            concluidos,
            juce::String(concluidos) + " of " + juce::String(total) + " files ingested");
    }

    if (pendentes <= 0) return;

    textoProgressoIngest_ = matriz::i18n::t("ingest.progresso")
                                .replace("{feito}", juce::String(concluidos))
                                .replace("{total}", juce::String(total));

    ProgressoGlobal::obterInstancia().atualizarProgresso(
        "ingest",
        concluidos,
        juce::String(concluidos) + " of " + juce::String(total) + " files ingested");
}

void MainComponent::garantirLabelProgressoIngest() {
}

void MainComponent::mostrarResumoLote(int novos, int duplicatas, int erros) {
    juce::String texto = matriz::i18n::t("ingest.resumo_novos").replace("{n}", juce::String(novos));
    if (duplicatas > 0)
        texto += " | " + matriz::i18n::t("ingest.resumo_duplicatas").replace("{n}", juce::String(duplicatas));
    if (erros > 0) texto += " | " + matriz::i18n::t("ingest.resumo_erros").replace("{n}", juce::String(erros));
    textoProgressoIngest_ = texto;
    ProgressoGlobal::obterInstancia().concluirTarefa("ingest", texto);
    atualizarPainelDeApoio();
    atualizarEtapaDoFluxo();
}

void MainComponent::renomearItemSelecionado() {
    if (catalogWorkspace_) catalogWorkspace_->renomearSelecionados();
    else if (mosaico_) mosaico_->renomearSelecao();
}

void MainComponent::removerItemSelecionadoDoBackup() {
    if (catalogWorkspace_) catalogWorkspace_->removerSelecionadosDoBackup();
    else if (mosaico_) mosaico_->removerSelecaoDoBackup();
}

bool MainComponent::podeDesfazer() const {
    return projetoAberto_ && projetoAberto_->podeDesfazer();
}

void MainComponent::executarUndo() {
    if (!projetoAberto_) return;
    auto desc = projetoAberto_->descricaoUndoAtual();
    if (projetoAberto_->desfazer()) {
        if (mosaico_) mosaico_->recarregar();
        if (arvoreOrigem_) arvoreOrigem_->recarregar();
        if (arvoreAcervo_) arvoreAcervo_->recarregar();
        if (filtros_) filtros_->recarregar();
        atualizarPainelDeApoio();
        if (auto* win = findParentComponentOfClass<juce::DocumentWindow>()) {
            auto titulo = win->getName();
            win->setName(titulo + "  [Undo: " + juce::String(desc) + "]");
            juce::Component::SafePointer<juce::DocumentWindow> safeWin(win);
            juce::Timer::callAfterDelay(1500, [safeWin, titulo] {
                if (safeWin) safeWin->setName(titulo);
            });
        }
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::spaceKey) {
        if (preview_ && preview_->videoPlayerParaTeste()) {
            auto* vp = preview_->videoPlayerParaTeste();
            if (vp->estaTocando()) vp->pausar(); else vp->tocar();
            return true;
        }
    }
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
        executarUndo();
        return true;
    }
    if (key == juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0)) {
        salvarProjeto();
        return true;
    }
    if (key == juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) {
        if (aoSalvarComo) aoSalvarComo();
        return true;
    }
    return false;
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);

    // Highlight do painel ativo (item 9): borda colorida no painel que
    // está recebendo os comandos de filtro (MEDIA TYPE / STATUS / FILE TYPE).
    if (arvoreOrigemViewport_ && arvoreAcervoViewport_) {
        auto destacar = [&](juce::Viewport* viewport, bool ativo) {
            auto bounds = viewport->getBounds().expanded(1);
            if (ativo) {
                g.setColour(tema().bordaFoco);
                g.drawRect(bounds, 2);
            } else {
                g.setColour(tema().borda);
                g.drawRect(bounds, 1);
            }
        };
        destacar(arvoreOrigemViewport_.get(), painelAtivo_ == PainelAtivo::Source);
        destacar(arvoreAcervoViewport_.get(), painelAtivo_ == PainelAtivo::BackupTree);
    }

    if (!arrastandoArquivo_) return;
    g.setColour(tema().acento.withAlpha(0.10f));
    g.fillRect(getLocalBounds());
    g.setColour(tema().acento);
    g.drawRect(getLocalBounds(), 3);
}

void MainComponent::resized() {
    MATRIZ_TRACE("MainComponent::resized");

    // O overlay cobre a janela inteira, sempre — é o que faz o scrim
    // escurecer tudo e engolir o clique de qualquer painel atrás.
    overlay_.setBounds(getLocalBounds());

    auto area = getLocalBounds();

    if (catalogo_) {
        auto cabecalho = area.removeFromTop(BarraFerramentasComponent::kAltura).reduced(tema().espacoMedio, 8);
        catalogoFechar_->setBounds(cabecalho.removeFromRight(110));
        cabecalho.removeFromRight(tema().espacoMedio);
        catalogoBusca_->setBounds(cabecalho.removeFromRight(juce::jmin(320, cabecalho.getWidth() / 2)));
        cabecalho.removeFromRight(tema().espacoMedio);
        catalogoTitulo_->setBounds(cabecalho);

        catalogoViewport_->setBounds(area);
        int targetW = catalogoViewport_->getWidth() - catalogoViewport_->getScrollBarThickness();
        catalogo_->setSize(targetW, catalogo_->getHeight());
        return;
    }

    if (projetoAberto_ && barraNavegacao_) {
        if (telaAtiva_ != TelaAtiva::Inicial) {
            barraNavegacao_->setBounds(area.removeFromTop(44));
            barraNavegacao_->setVisible(true);
        } else {
            barraNavegacao_->setVisible(false);
        }
    }

    if (projetoAberto_ && barraProgressoGlobal_) {
        if (telaAtiva_ != TelaAtiva::Inicial) {
            barraProgressoGlobal_->setBounds(area.removeFromBottom(BarraProgressoGlobalComponent::kAlturaFixa));
            barraProgressoGlobal_->setVisible(true);
        } else {
            barraProgressoGlobal_->setVisible(false);
        }
    }

    if (catalogHubWorkspace_ && catalogHubWorkspace_->isVisible()) {
        catalogHubWorkspace_->setBounds(area);
        return;
    }

    if (homePanel_ && homePanel_->isVisible()) {
        homePanel_->setBounds(area);
        return;
    }

    if (intakeWorkspace_ && intakeWorkspace_->isVisible()) {
        intakeWorkspace_->setBounds(area);
        return;
    }

    if (catalogWorkspace_ && catalogWorkspace_->isVisible()) {
        catalogWorkspace_->setBounds(area);
        return;
    }

    if (duplicatesWorkspace_ && duplicatesWorkspace_->isVisible()) {
        duplicatesWorkspace_->setBounds(area);
        return;
    }

    if (analyticsWorkspace_ && analyticsWorkspace_->isVisible()) {
        analyticsWorkspace_->setBounds(area);
        return;
    }

    if (treeWorkspace_ && treeWorkspace_->isVisible()) {
        treeWorkspace_->setBounds(area);
        return;
    }

    if (backupWorkspace_ && backupWorkspace_->isVisible()) {
        backupWorkspace_->setBounds(area);
        return;
    }

    if (storageWorkspace_ && storageWorkspace_->isVisible()) {
        storageWorkspace_->setBounds(area);
        return;
    }

    if (ingestWizard_ && ingestWizard_->isVisible()) {
        ingestWizard_->setBounds(area);
        return;
    }

    if (!temProjetoAberto() && telaInicialTitulo_) {
        constexpr int kLarguraCartao = 320;
        constexpr int kAlturaCartao = 180;
        constexpr int kLarguraColuna = kLarguraCartao * 2 + 24; // dois cartões + espaço entre eles

        int alturaRecentes = telaInicialRecentesTitulo_ ? (24 + static_cast<int>(telaInicialLinhasRecentes_.size()) * 30) : 0;
        int alturaTotal = 30 /*titulo*/ + tema().espacoPequeno + 24 /*subtitulo*/ + tema().espacoGrande + kAlturaCartao +
                           tema().espacoGrande + 32 /*abrir*/ +
                           (alturaRecentes > 0 ? tema().espacoGrande + alturaRecentes : 0);

        auto coluna = area.withSizeKeepingCentre(kLarguraColuna, alturaTotal);

        telaInicialTitulo_->setBounds(coluna.removeFromTop(30));
        coluna.removeFromTop(tema().espacoPequeno);
        telaInicialSubtitulo_->setBounds(coluna.removeFromTop(24));
        coluna.removeFromTop(tema().espacoGrande);

        auto linhaCartoes = coluna.removeFromTop(kAlturaCartao);
        telaInicialCartaoArchive_->setBounds(linhaCartoes.removeFromLeft(kLarguraCartao));
        linhaCartoes.removeFromLeft(24);
        telaInicialCartaoCatalog_->setBounds(linhaCartoes.removeFromLeft(kLarguraCartao));
        coluna.removeFromTop(tema().espacoGrande);

        auto linhaAbrir = coluna.removeFromTop(32);
        telaInicialBotaoAbrir_->setBounds(linhaAbrir.withSizeKeepingCentre(260, 32));

        if (telaInicialRecentesTitulo_) {
            coluna.removeFromTop(tema().espacoGrande);
            telaInicialRecentesTitulo_->setBounds(coluna.removeFromTop(20));
            for (auto& linha : telaInicialLinhasRecentes_) linha->setBounds(coluna.removeFromTop(28));
        }
        return;
    }

    constexpr int kAlturaCabecalhoPainel = 28;
    constexpr int kLarguraDivisor = 6;

    if (barraFerramentas_) barraFerramentas_->setBounds(area.removeFromTop(BarraFerramentasComponent::kAltura));
    if (transport_) transport_->setBounds(area.removeFromTop(TransportComponent::kAltura));

    int larguraTotal = area.getWidth();
    int lSource = mostrarEstruturaOrigem_ ? juce::jmax(80, static_cast<int>(larguraTotal * propPainel1_)) : 0;
    
    bool mostrarColunaDireita = mostrarEstruturaBackup_ || (fichaPanel_ && fichaPanel_->isVisible());
    int lBackup = mostrarColunaDireita ? juce::jmax(80, static_cast<int>(larguraTotal * propPainel2_)) : 0;

    int numDivisores = (mostrarEstruturaOrigem_ ? 1 : 0) + (mostrarColunaDireita ? 1 : 0);
    int lMosaico = larguraTotal - lSource - lBackup - kLarguraDivisor * numDivisores;
    if (lMosaico < 120) {
        lMosaico = 120;
        lSource = mostrarEstruturaOrigem_ ? 150 : 0;
        lBackup = mostrarColunaDireita ? (larguraTotal - lSource - lMosaico - kLarguraDivisor * numDivisores) : 0;
    }

    // --- Left column (Filters / Source Tree) ---
    auto areaSource = area.removeFromLeft(lSource);
    if (mostrarEstruturaOrigem_) {
        auto cabecalhoSource = areaSource.removeFromTop(kAlturaCabecalhoPainel);
        if (botaoSourceIcones_) { botaoSourceIcones_->setVisible(true); botaoSourceIcones_->setBounds(cabecalhoSource.removeFromRight(42).reduced(2, 2)); }
        if (botaoSourceLista_) { botaoSourceLista_->setVisible(true); botaoSourceLista_->setBounds(cabecalhoSource.removeFromRight(36).reduced(2, 2)); }
        if (labelSource_) { labelSource_->setVisible(true); labelSource_->setBounds(cabecalhoSource.reduced(6, 2)); }

        int alturaFiltros = static_cast<int>(areaSource.getHeight() * 0.35f);
        auto areaFiltros = areaSource.removeFromBottom(alturaFiltros);
        if (filtrosViewport_) { filtrosViewport_->setVisible(true); filtrosViewport_->setBounds(areaFiltros); }
        if (arvoreOrigemViewport_) { arvoreOrigemViewport_->setVisible(true); arvoreOrigemViewport_->setBounds(areaSource); }
    } else {
        if (botaoSourceIcones_) botaoSourceIcones_->setVisible(false);
        if (botaoSourceLista_) botaoSourceLista_->setVisible(false);
        if (labelSource_) labelSource_->setVisible(false);
        if (arvoreOrigemViewport_) arvoreOrigemViewport_->setVisible(false);
        if (filtrosViewport_) filtrosViewport_->setVisible(false);
    }

    if (filtros_ && filtrosViewport_ && filtrosViewport_->isVisible()) {
        filtros_->setSize(filtrosViewport_->getWidth() - filtrosViewport_->getScrollBarThickness(), filtros_->getHeight());
        filtros_->resized();
    }
    if (arvoreOrigem_ && arvoreOrigemViewport_ && arvoreOrigemViewport_->isVisible()) {
        arvoreOrigem_->setSize(arvoreOrigemViewport_->getWidth() - arvoreOrigemViewport_->getScrollBarThickness(), arvoreOrigem_->getHeight());
        arvoreOrigem_->resized();
    }

    if (divisor1_) {
        divisor1_->setVisible(mostrarEstruturaOrigem_);
        if (mostrarEstruturaOrigem_) divisor1_->setBounds(area.removeFromLeft(kLarguraDivisor));
    }

    // --- Right column (Ficha / Backup Tree) ---
    if (mostrarColunaDireita) {
        auto areaBackup = area.removeFromRight(lBackup);
        if (mostrarEstruturaBackup_) {
            auto cabecalhoBackup = areaBackup.removeFromTop(kAlturaCabecalhoPainel);
            if (botaoNovaPastaAcervo_) { botaoNovaPastaAcervo_->setVisible(true); botaoNovaPastaAcervo_->setBounds(cabecalhoBackup.removeFromRight(28).reduced(2, 2)); }
            if (botaoBackupIcones_) { botaoBackupIcones_->setVisible(true); botaoBackupIcones_->setBounds(cabecalhoBackup.removeFromRight(42).reduced(2, 2)); }
            if (botaoBackupLista_) { botaoBackupLista_->setVisible(true); botaoBackupLista_->setBounds(cabecalhoBackup.removeFromRight(36).reduced(2, 2)); }
            if (labelBackupTree_) { labelBackupTree_->setVisible(true); labelBackupTree_->setBounds(cabecalhoBackup.reduced(6, 2)); }

            if (fichaPanel_ && fichaPanel_->isVisible()) {
                int alturaDisponivel = areaBackup.getHeight();
                int alturaFicha = juce::jmax(100, static_cast<int>(alturaDisponivel * propFichaBackup_));
                int alturaArvore = alturaDisponivel - alturaFicha - kLarguraDivisor;
                if (alturaArvore < 60) { alturaArvore = 60; alturaFicha = alturaDisponivel - alturaArvore - kLarguraDivisor; }

                auto areaArvore = areaBackup.removeFromTop(alturaArvore);
                if (arvoreAcervoViewport_) { arvoreAcervoViewport_->setVisible(true); arvoreAcervoViewport_->setBounds(areaArvore); }
                if (divisorFicha_) { divisorFicha_->setVisible(true); divisorFicha_->setBounds(areaBackup.removeFromTop(kLarguraDivisor)); }
                if (barraAcoesFicha_) { barraAcoesFicha_->setVisible(true); barraAcoesFicha_->setBounds(areaBackup.removeFromTop(BarraAcoesFicha::kAltura)); }
                if (fichaPanel_) { fichaPanel_->setVisible(true); fichaPanel_->setBounds(areaBackup); }
            } else {
                if (arvoreAcervoViewport_) { arvoreAcervoViewport_->setVisible(true); arvoreAcervoViewport_->setBounds(areaBackup); }
                if (divisorFicha_) divisorFicha_->setVisible(false);
                if (barraAcoesFicha_) barraAcoesFicha_->setVisible(false);
                if (fichaPanel_) fichaPanel_->setVisible(false);
            }
        } else {
            if (botaoNovaPastaAcervo_) botaoNovaPastaAcervo_->setVisible(false);
            if (botaoBackupIcones_) botaoBackupIcones_->setVisible(false);
            if (botaoBackupLista_) botaoBackupLista_->setVisible(false);
            if (labelBackupTree_) labelBackupTree_->setVisible(false);
            if (arvoreAcervoViewport_) arvoreAcervoViewport_->setVisible(false);
            if (divisorFicha_) divisorFicha_->setVisible(false);

            if (fichaPanel_ && fichaPanel_->isVisible()) {
                if (barraAcoesFicha_) { barraAcoesFicha_->setVisible(true); barraAcoesFicha_->setBounds(areaBackup.removeFromTop(BarraAcoesFicha::kAltura)); }
                if (fichaPanel_) { fichaPanel_->setVisible(true); fichaPanel_->setBounds(areaBackup); }
            }
        }

        if (arvoreAcervo_ && arvoreAcervoViewport_ && arvoreAcervoViewport_->isVisible()) {
            arvoreAcervo_->setSize(arvoreAcervoViewport_->getWidth() - arvoreAcervoViewport_->getScrollBarThickness(), arvoreAcervo_->getHeight());
            arvoreAcervo_->resized();
        }

        if (divisor2_) {
            divisor2_->setVisible(true);
            divisor2_->setBounds(area.removeFromRight(kLarguraDivisor));
        }
    } else {
        if (divisor2_) divisor2_->setVisible(false);
        if (divisorFicha_) divisorFicha_->setVisible(false);
        if (arvoreAcervoViewport_) arvoreAcervoViewport_->setVisible(false);
        if (fichaPanel_) fichaPanel_->setVisible(false);
        if (barraAcoesFicha_) barraAcoesFicha_->setVisible(false);
    }

    // --- Center: Grid/Preview/Listening (full height) ---
    auto areaCentro = area;

    if (mosaicoViewport_) mosaicoViewport_->setBounds(areaCentro);
    if (preview_) preview_->setBounds(areaCentro);
    if (escuta_) escuta_->setBounds(areaCentro);

    if (mosaico_) {
        mosaico_->setSize(mosaicoViewport_->getWidth() - mosaicoViewport_->getScrollBarThickness(), mosaico_->getHeight());
        mosaico_->resized();
    }

}

} // namespace matriz::ui
