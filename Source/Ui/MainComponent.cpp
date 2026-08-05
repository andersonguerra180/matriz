#include "MainComponent.h"

#include "../App/Cancelamento.h"
#include "../App/Preferencias.h"
#include "../I18n/Strings.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Ingest/Miniaturas.h"
#include "AcoesItem.h"
#include "ArvoreComponent.h"
#include "BarraFerramentasComponent.h"
#include "BarraMetricasComponent.h"
#include "BarraGuiaComponent.h"
#include "BarraSelecaoComponent.h"
#include "CatalogoComponent.h"
#include "FichaPanelComponent.h"
#include "FiltrosComponent.h"
#include "MosaicoComponent.h"
#include "PainelInconsistenciasComponent.h"
#include "NavegadorArquivosDialogo.h"
#include "PreviewComponent.h"
#include "Tokens.h"

namespace matriz::ui {

// Faixa de progresso do ingest — barra preenchida proporcionalmente, com o
// texto por cima. Substitui o juce::Label anterior: em lote de milhares de
// arquivos, "1287/9500" sozinho não dá noção nenhuma de quanto falta, e a
// sensação era de app travado. Mantém setText/getText porque o self-test de
// UI lê o resumo final por textoProgressoIngestParaTeste().
class FaixaProgressoIngest : public juce::Component {
public:
    FaixaProgressoIngest() { setInterceptsMouseClicks(false, false); }

    void setText(const juce::String& texto) {
        texto_ = texto;
        repaint();
    }
    juce::String getText() const { return texto_; }

    // fracao < 0 = lote terminado (mostra a faixa cheia, com o resumo).
    void definirFracao(double fracao) {
        fracao_ = fracao;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        bool concluido = fracao_ < 0.0;
        auto area = getLocalBounds();

        g.setColour(concluido ? tk.acento.withAlpha(0.92f) : tk.acento.withAlpha(0.28f));
        g.fillRect(area);
        if (!concluido) {
            auto preenchido = area.withWidth(juce::roundToInt(area.getWidth() * juce::jlimit(0.0, 1.0, fracao_)));
            g.setColour(tk.acento.withAlpha(0.92f));
            g.fillRect(preenchido);
        }

        g.setColour(tk.textoSobreAcento);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(texto_, area, juce::Justification::centred, true);
    }

private:
    juce::String texto_;
    double fracao_ = 0.0;
};

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
        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        auto areaSelo = miolo.removeFromRight(90);
        g.drawText(selo_, areaSelo, juce::Justification::centredRight);
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
    if (categoria == matriz::ingest::CategoriaMidia::Imagem) return {"foto_suporte", false};
    return {"documento", false};
}

} // namespace

MainComponent::MainComponent() {
    // Precisa existir antes de qualquer Component com setTooltip() — sem um
    // TooltipWindow vivo na hierarquia o JUCE simplesmente não desenha dica
    // nenhuma, e as explicações dos botões da barra nunca apareceriam.
    tooltips_ = std::make_unique<juce::TooltipWindow>(this, 600);
    reconstruirTelaInicial();
}

MainComponent::~MainComponent() {
    // Fechar o aplicativo durante uma operação equivale a cancelar, com a
    // mesma garantia (item 10): pede o cancelamento ANTES de esperar o pool,
    // pra os jobs ainda enfileirados desistirem em vez de copiar milhares de
    // arquivos enquanto a janela já sumiu da tela.
    if (cancelamentoLote_) cancelamentoLote_->pedir();
    ingestPool_.removeAllJobs(true, 5000);
}

void MainComponent::reconstruirTelaInicial() {
    mosaicoViewport_.reset();
    mosaico_.reset();
    preview_.reset();
    itemEmPreview_.clear();
    painelInconsistenciasViewport_.reset();
    painelInconsistencias_.reset();
    fichaPanel_.reset();
    barraAcoesFicha_.reset();
    barraFerramentas_.reset();
    barraGuia_.reset();
    barraSelecao_.reset();
    fichaAberta_ = false;
    arvoreViewport_.reset();
    arvore_.reset();
    botaoAbaOrigem_.reset();
    botaoAbaAcervo_.reset();
    botaoNovaPastaAcervo_.reset();
    filtrosViewport_.reset();
    filtros_.reset();

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
            juce::String selo = r.modo == "catalogo" ? matriz::i18n::t("tela_inicial.recente_modo_catalog")
                                                       : matriz::i18n::t("tela_inicial.recente_modo_archive");
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
    mosaico_->aoClicarEstadoVazio = [this] { if (aoPedirIngerirArquivos) aoPedirIngerirArquivos(); };
    mosaico_->aoMudarSelecao = [this] { atualizarPainelDeApoio(); };
    mosaico_->aoMudarConteudoVisivel = [this] {
        atualizarPainelDeApoio();
        atualizarBarraMetricas();
    };
    mosaico_->aoPedirMenuContexto = [this](std::vector<std::string> itemIds) { abrirMenuContextoItens(std::move(itemIds)); };

    mosaicoViewport_ = std::make_unique<juce::Viewport>();
    mosaicoViewport_->setViewedComponent(mosaico_.get(), false);
    addAndMakeVisible(*mosaicoViewport_);

    // Árvore Origem/Acervo (§3.1, §5) — painel esquerdo fixo. Cabeçalho
    // (abas + "+") mora aqui, fora do Viewport, pra nunca rolar junto com
    // a lista (ver ArvoreComponent.h). Criada DEPOIS do mosaico porque seu
    // callback de seleção precisa dele já existir.
    arvore_ = std::make_unique<ArvoreComponent>(*projetoAberto_);
    arvore_->aoSelecionarNo = [this](std::optional<std::set<std::string>> itemIds) {
        if (mosaico_) mosaico_->definirFiltroItens(std::move(itemIds));
    };
    arvoreViewport_ = std::make_unique<juce::Viewport>();
    arvoreViewport_->setViewedComponent(arvore_.get(), false);
    addAndMakeVisible(*arvoreViewport_);

    botaoAbaOrigem_ = std::make_unique<juce::TextButton>(matriz::i18n::t("arvore.aba_origem"));
    botaoAbaOrigem_->onClick = [this] { selecionarAbaArvore(0); };
    addAndMakeVisible(*botaoAbaOrigem_);

    botaoAbaAcervo_ = std::make_unique<juce::TextButton>(matriz::i18n::t("arvore.aba_acervo"));
    botaoAbaAcervo_->onClick = [this] { selecionarAbaArvore(1); };
    addAndMakeVisible(*botaoAbaAcervo_);

    botaoNovaPastaAcervo_ = std::make_unique<juce::TextButton>("+");
    botaoNovaPastaAcervo_->onClick = [this] { if (arvore_) arvore_->criarPastaDeTopoNivel(); };
    addAndMakeVisible(*botaoNovaPastaAcervo_);
    selecionarAbaArvore(0); // Origem por padrão — tem conteúdo desde o primeiro ingest

    // Filtros (chips) + busca + coleções inteligentes (item 7, Acréscimos
    // §10) — embaixo da árvore na mesma coluna (§9.1). Também precisa do
    // mosaico já existir (opera direto sobre ele, mesmo padrão de
    // ArvoreComponent sobre ProjetoAberto).
    filtros_ = std::make_unique<FiltrosComponent>(*projetoAberto_, *mosaico_);
    filtrosViewport_ = std::make_unique<juce::Viewport>();
    filtrosViewport_->setViewedComponent(filtros_.get(), false);
    addAndMakeVisible(*filtrosViewport_);

    // No Catalog, o painel de inconsistências fica numa faixa fixa embaixo
    // da área central — nunca escondido em menu (§1.3/§3.5), mas também
    // nunca bloqueia a grade acima dele.
    if (projetoAberto_->projeto().modo() == matriz::model::Modo::Catalogo) {
        painelInconsistencias_ = std::make_unique<PainelInconsistenciasComponent>(*projetoAberto_);
        painelInconsistenciasViewport_ = std::make_unique<juce::Viewport>();
        painelInconsistenciasViewport_->setViewedComponent(painelInconsistencias_.get(), false);
        addAndMakeVisible(*painelInconsistenciasViewport_);
        painelInconsistencias_->recarregar();
    }

    // Ficha lateral colapsada por padrão (§7.1 — "a ficha nunca bloqueia") —
    // o botão de alternar fica sempre visível, mesmo fechada.
    fichaPanel_ = std::make_unique<FichaPanelComponent>(*projetoAberto_);
    addAndMakeVisible(*fichaPanel_);
    fichaPanel_->setVisible(fichaAberta_);

    barraAcoesFicha_ = std::make_unique<BarraAcoesFicha>();
    addAndMakeVisible(*barraAcoesFicha_);
    barraAcoesFicha_->setVisible(fichaAberta_);
    {
        // Mesmos ganchos do menu de contexto — as duas portas compartilham
        // não só as funções de ação, mas também o que acontece depois delas.
        auto idsSelecionados = [this] {
            if (!mosaico_) return std::vector<std::string>{};
            return std::vector<std::string>(mosaico_->itensSelecionados().begin(),
                                             mosaico_->itensSelecionados().end());
        };
        barraAcoesFicha_->aoCategorizar = [this] { if (mosaico_) selecionarItem(mosaico_->itemSelecionado()); };
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
        if (arvore_) arvore_->recarregar();
        if (filtros_) filtros_->recarregar();
        if (painelInconsistencias_) painelInconsistencias_->recarregar();
        atualizarPainelDeApoio();
        atualizarEtapaDoFluxo(); // classificar em lote pode ter fechado a etapa "sem categoria"
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
    addAndMakeVisible(*barraFerramentas_);

    barraGuia_ = std::make_unique<BarraGuiaComponent>();
    barraGuia_->aoMudarAltura = [this] { resized(); };
    addAndMakeVisible(*barraGuia_);

    // Barra de métricas do rodapé (item 6) — LUFS-I/LRA/FPS vêm da leitura
    // técnica já gravada, e o VU é alimentado pelo preview quando toca.
    barraMetricas_ = std::make_unique<BarraMetricasComponent>();
    addAndMakeVisible(*barraMetricas_);

    barraSelecao_ = std::make_unique<BarraSelecaoComponent>();
    barraSelecao_->aoEditarEmLote = [this] {
        // Abrir a ficha já em modo lote é o ponto inteiro desta barra: o
        // recurso existia, mas exigia adivinhar que estava atrás de um
        // painel fechado que não anunciava conter edição em lote.
        if (!fichaAberta_) alternarFicha();
        if (mosaico_) selecionarItem(mosaico_->itemSelecionado());
    };
    barraSelecao_->aoSelecionarTodos = [this] {
        if (!mosaico_) return;
        mosaico_->selecionarTodos();
        selecionarItem(mosaico_->itemSelecionado());
    };
    barraSelecao_->aoLimparSelecao = [this] {
        if (mosaico_) mosaico_->limparSelecao();
    };
    addAndMakeVisible(*barraSelecao_);

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
        if (arvore_) arvore_->recarregar();
        if (filtros_) filtros_->recarregar();
        if (painelInconsistencias_) painelInconsistencias_->recarregar();
        atualizarPainelDeApoio();
        atualizarEtapaDoFluxo();
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
    if (!projetoAberto_ || !mosaico_) return;

    int total = mosaico_->totalItensCarregados();
    int visiveis = mosaico_->totalItensVisiveis();

    if (barraFerramentas_) {
        barraFerramentas_->definirContagem(visiveis, total, visiveis != total);
        barraFerramentas_->definirDetalhesAbertos(fichaAberta_);
    }

    if (barraAcoesFicha_)
        barraAcoesFicha_->definirQuantidadeSelecionada(static_cast<int>(mosaico_->itensSelecionados().size()));

    if (barraSelecao_) {
        int antes = barraSelecao_->quantidade();
        barraSelecao_->definirQuantidade(static_cast<int>(mosaico_->itensSelecionados().size()));
        // A barra some/aparece; o mosaico abaixo dela precisa de bounds novos.
        if ((antes > 0) != (barraSelecao_->quantidade() > 0)) resized();
    }
}

void MainComponent::atualizarEtapaDoFluxo() {
    if (!projetoAberto_ || !mosaico_ || !barraGuia_) return;

    int total = mosaico_->totalItensCarregados();

    // Em que ponto do fluxo o projeto está — a faixa sempre aponta o
    // próximo passo concreto, nunca um texto genérico de boas-vindas.
    if (total == 0) {
        barraGuia_->definirEtapa(BarraGuiaComponent::Etapa::Vazio);
        return;
    }

    auto contagens = projetoAberto_->contagensPorTipoMidia();
    auto naoClassificados = contagens.find("");
    if (naoClassificados != contagens.end() && naoClassificados->second > 0) {
        barraGuia_->definirEtapa(BarraGuiaComponent::Etapa::Classificar, naoClassificados->second);
        return;
    }

    // Tudo classificado: falta montar as pastas do backup ou gravá-lo.
    // Pasta de acervo com id vazio é o nó sintético "ainda sem pasta"
    // (ver ProjetoAberto::arvoreAcervo) — não conta como organização.
    int organizados = 0;
    for (auto& pasta : projetoAberto_->arvoreAcervo().filhos)
        if (!pasta.id.empty()) organizados += static_cast<int>(pasta.itemIds.size());

    barraGuia_->definirEtapa(organizados > 0 ? BarraGuiaComponent::Etapa::Backup
                                             : BarraGuiaComponent::Etapa::Organizar);
}

void MainComponent::selecionarAbaArvore(int aba) {
    if (!arvore_) return;
    arvore_->definirAba(aba == 0 ? ArvoreComponent::Aba::Origem : ArvoreComponent::Aba::Acervo);
    bool ehAcervo = aba != 0;
    if (botaoNovaPastaAcervo_) botaoNovaPastaAcervo_->setVisible(ehAcervo);
    if (botaoAbaOrigem_) botaoAbaOrigem_->setColour(juce::TextButton::buttonColourId,
                                                     ehAcervo ? tema().painel : tema().painelAlt);
    if (botaoAbaAcervo_) botaoAbaAcervo_->setColour(juce::TextButton::buttonColourId,
                                                     ehAcervo ? tema().painelAlt : tema().painel);
}

void MainComponent::alternarFicha() {
    fichaAberta_ = !fichaAberta_;
    if (fichaPanel_) fichaPanel_->setVisible(fichaAberta_);
    if (barraAcoesFicha_) barraAcoesFicha_->setVisible(fichaAberta_);
    if (barraFerramentas_) barraFerramentas_->definirDetalhesAbertos(fichaAberta_);
    resized();
    repaint();
}

void MainComponent::abrirPreview(const std::string& itemId) {
    if (!projetoAberto_) return;

    if (!preview_) {
        preview_ = std::make_unique<PreviewComponent>(*projetoAberto_);
        preview_->aoFechar = [this] { fecharPreview(); };
        preview_->aoNavegar = [this](int direcao) {
            if (!mosaico_ || !preview_) return;
            auto proximo = mosaico_->itemAdjacente(preview_->itemAtual(), direcao);
            if (proximo) abrirPreview(*proximo);
        };
        addAndMakeVisible(*preview_);
    }

    itemEmPreview_ = itemId;
    preview_->mostrarItem(itemId);
    mosaico_->selecionarItem(itemId);
    selecionarItem(itemId);

    mosaicoViewport_->setVisible(false);
    preview_->setVisible(true);
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
    return true;
}

void MainComponent::reconstruirLayoutCatalogo(const juce::File& pasta) {
    catalogoViewport_ = std::make_unique<juce::Viewport>();
    catalogoViewport_->setViewedComponent(catalogo_.get(), false);
    addAndMakeVisible(*catalogoViewport_);

    catalogoTitulo_ = std::make_unique<juce::Label>();
    catalogoTitulo_->setText(matriz::i18n::t("catalogo.titulo") + "  ·  " + pasta.getFileName(),
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
    projetoAberto_ = std::make_unique<ProjetoAberto>(std::move(projeto));
    reconstruirLayoutProjeto();
}

void MainComponent::fecharProjeto() {
    if (catalogo_) {
        catalogo_.reset();
        catalogoViewport_.reset();
        catalogoTitulo_.reset();
        catalogoBusca_.reset();
        catalogoFechar_.reset();
        reconstruirTelaInicial();
        return;
    }

    // Um lote em background ainda segura uma referência a registro_ —
    // destruir o projeto agora deixaria o job com uma referência
    // pendurada. O menu já desabilita esta ação durante o ingest; esta
    // checagem é o cinto de segurança pra qualquer outro chamador (ex.:
    // testes headless).
    if (ingestEmAndamento()) return;
    projetoAberto_.reset();
    reconstruirTelaInicial();
}

bool MainComponent::temPainelInconsistencias() const { return painelInconsistencias_ != nullptr; }

int MainComponent::totalInconsistencias() const {
    return painelInconsistencias_ ? painelInconsistencias_->totalInconsistencias() : -1;
}

juce::String MainComponent::textoProgressoIngestParaTeste() const {
    return labelProgressoIngest_ ? labelProgressoIngest_->getText() : juce::String();
}

std::unique_ptr<matriz::model::Project> MainComponent::destacarProjeto() {
    if (!projetoAberto_) return nullptr;
    jassert(!ingestEmAndamento());
    return projetoAberto_->destacarProjeto();
}

void MainComponent::selecionarItem(const std::string& itemId) {
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

    // Barra de métricas acompanha a seleção (item 6.2 — "com nenhum arquivo
    // selecionado, mostra o resumo do que está na grade").
    atualizarBarraMetricas();
}

void MainComponent::atualizarBarraMetricas() {
    if (!barraMetricas_ || !projetoAberto_) return;
    std::string sel = mosaico_ ? mosaico_->itemSelecionado() : std::string();
    if (sel.empty() || (mosaico_ && mosaico_->itensSelecionados().size() > 1)) {
        // Resumo da grade: soma o tamanho dos arquivos principais do que
        // está visível sob os filtros atuais, não do acervo inteiro.
        juce::int64 total = 0;
        int contagem = mosaico_ ? mosaico_->totalItensVisiveis() : 0;
        if (mosaico_)
            for (auto& r : projetoAberto_->listarItens())
                if (auto a = projetoAberto_->arquivoPrincipal(r.id)) total += juce::File(a->caminhoAbsoluto).getSize();
        barraMetricas_->mostrarResumoDaGrade(contagem, total);
        return;
    }
    barraMetricas_->mostrarItem(*projetoAberto_, sel);
}

void MainComponent::ingerirArquivos(const juce::Array<juce::File>& arquivosOuPastas) {
    if (!projetoAberto_) return;

    // Nenhum diálogo, nenhuma pergunta (Reorientação completa §2.1): solta
    // a pasta e o conteúdo aparece na grade imediatamente. Lotes que
    // chegam enquanto outro ainda está em andamento se somam ao mesmo
    // lote em vez de serem rejeitados — soltar mais arquivos durante o
    // processamento tem que funcionar (§2.2).
    auto arquivos = expandirArquivos(arquivosOuPastas);
    if (arquivos.empty()) return;
    processarLoteEmBackground(arquivos);
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

void MainComponent::filesDropped(const juce::StringArray& files, int, int) {
    arrastandoArquivo_ = false;
    if (mosaico_) mosaico_->definirArrastandoArquivo(false);
    repaint();
    if (!temProjetoAberto()) return;
    juce::Array<juce::File> arquivos;
    for (auto& caminho : files) arquivos.add(juce::File(caminho));
    ingerirArquivos(arquivos);
}

void MainComponent::fileDragEnter(const juce::StringArray&, int, int) {
    arrastandoArquivo_ = true;
    if (mosaico_) mosaico_->definirArrastandoArquivo(true);
    repaint();
}

void MainComponent::fileDragExit(const juce::StringArray&) {
    arrastandoArquivo_ = false;
    if (mosaico_) mosaico_->definirArrastandoArquivo(false);
    repaint();
}

std::vector<juce::File> MainComponent::expandirArquivos(const juce::Array<juce::File>& arquivosOuPastas) const {
    // Pastas entram recursivamente como arquivos individuais — ainda não é
    // a inferência de estrutura real do §7.3 (release/faixa/capa por
    // convenção de nome), só o mínimo pra "arrastei uma pasta" funcionar.
    std::vector<juce::File> arquivos;
    for (auto& entrada : arquivosOuPastas) {
        if (entrada.isDirectory()) {
            for (auto& sub : juce::RangedDirectoryIterator(entrada, true, "*", juce::File::findFiles))
                arquivos.push_back(sub.getFile());
        } else {
            arquivos.push_back(entrada);
        }
    }
    return arquivos;
}

void MainComponent::processarLoteEmBackground(std::vector<juce::File> arquivos) {
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

    auto stmtContagem = registro->prepare("SELECT COUNT(*) FROM item");
    stmtContagem.step();
    int proximoNumero = static_cast<int>(stmtContagem.columnInt(0)) + 1;

    // Fase 1 — síncrona, mas rápida (só INSERT, sem tocar em arquivo
    // nenhum): cada item existe no banco ANTES de qualquer processamento
    // pesado começar, pra grade poder mostrar o lote inteiro na hora
    // (Reorientação completa §2.1 — "os arquivos aparecem na grade
    // IMEDIATAMENTE"). tipo_midia fica NULL — classificar é trabalho de
    // depois, por cima do que já está visível (§7.1).
    std::vector<std::string> itemIds;
    itemIds.reserve(arquivos.size());
    registro->run("BEGIN", {});
    try {
        for (auto& arquivo : arquivos) {
            std::string itemId = matriz::model::novoUuid();
            std::string agora = matriz::model::agoraIso8601();
            juce::String codigo = prefixo + "-" + juce::String(proximoNumero++).paddedLeft('0', 5);
            registro->run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
                "VALUES (?, ?, ?, ?, NULL, 'capturado', ?, ?)",
                {matriz::db::Value::of(itemId), matriz::db::Value::of(projetoId),
                 matriz::db::Value::of(codigo.toStdString()),
                 matriz::db::Value::of(arquivo.getFileNameWithoutExtension().toStdString()),
                 matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
            itemIds.push_back(itemId);
        }
        registro->run("COMMIT", {});
    } catch (...) {
        registro->run("ROLLBACK", {});
        throw;
    }

    ingestsTotalLote_ += static_cast<int>(arquivos.size());
    ingestsPendentes_ += static_cast<int>(arquivos.size());
    atualizarLabelProgresso();
    if (mosaico_) mosaico_->recarregar(); // lote inteiro já visível, mesmo sem miniatura/checksum ainda

    // Um lote novo rearma o token — cancelar um lote não pode deixar todos
    // os seguintes nascendo cancelados.
    if (!cancelamentoLote_) cancelamentoLote_ = matriz::app::novoCancelamento();
    cancelamentoLote_->rearmar();
    auto cancelamento = cancelamentoLote_;

    auto estadoLote = std::make_shared<EstadoLote>();
    juce::Component::SafePointer<MainComponent> safeThis(this);
    // Atualiza a grade periodicamente durante o lote (não a cada arquivo —
    // custaria uma consulta completa por arquivo em lotes de milhares) pra
    // as miniaturas irem preenchendo visivelmente atrás, como pedido em
    // §2.1/§2.2, sem esperar o lote inteiro terminar.
    auto contadorParaAtualizar = std::make_shared<std::atomic<int>>(0);
    constexpr int kAtualizarACada = 20;

    for (size_t i = 0; i < arquivos.size(); ++i) {
        juce::File arquivo = arquivos[i];
        std::string itemId = itemIds[i];

        // A cópia + checksum + ffprobe/Exiv2 (matriz::ingest::ingerirArquivo)
        // rodava direto na thread de mensagens antes — era exatamente o
        // travamento reportado. Agora cada arquivo é um job no ThreadPool
        // (1 thread — sequencial, sem concorrência de escrita no registro).
        ingestPool_.addJob([registro, indice, pastaProjeto, itemId, arquivo, estadoLote, safeThis,
                             contadorParaAtualizar, cancelamento]() mutable {
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
                juce::MessageManager::callAsync([safeThis, estadoLote, contadorParaAtualizar]() {
                    if (!safeThis) return;
                    safeThis.getComponent()->finalizarUnidadeDeLote(estadoLote, contadorParaAtualizar);
                });
                return;
            }

            try {
                // Continuous ingestion (item 9, §8.2 — "um asset, muitas
                // localizações"): se o MESMO conteúdo já está no acervo
                // (SHA-256 igual, não importa nome nem pasta), reconhece em
                // vez de copiar de novo — o operador nunca reimporta.
                auto conhecido = matriz::ingest::buscarAssetConhecido(*registro, arquivo);
                if (conhecido) {
                    matriz::ingest::registrarLocalizacaoConhecida(*registro, conhecido->arquivoId, arquivo);
                    juce::String nota = "Same content as " + juce::String(conhecido->codigoAcervo) +
                                         " — already in the archive, not copied again.";
                    registro->run("UPDATE item SET estado = 'duplicata', notas_livres = ? WHERE id = ?",
                                  {matriz::db::Value::of(nota.toStdString()), matriz::db::Value::of(itemId)});
                    duplicata = true;
                    sucesso = true;
                } else {
                    auto categoria = matriz::ingest::categoriaPorExtensao(arquivo);
                    PapelInfo papelInfo = papelPorCategoria(categoria);

                    auto resultado = matriz::ingest::ingerirArquivo(*registro, pastaProjeto, itemId, arquivo,
                                                                      papelInfo.papel, papelInfo.ehMaster);
                    sucesso = true;

                    // Miniatura (§3.3 — "todo arquivo tem miniatura", nunca
                    // bloco cinza): nunca derruba o ingest se falhar, a
                    // própria função garante isso.
                    matriz::ingest::gerarEGravarMiniaturaPrincipal(*indice, pastaProjeto, itemId,
                                                                     resultado.arquivoId, resultado.arquivoNoProjeto,
                                                                     categoria, resultado.leitura.duracaoSegundos);
                }
            } catch (const std::exception& e) {
                erro = arquivo.getFileName() + ": " + juce::String(e.what());
                // Arquivo que falhou aparece marcado, nunca some em
                // silêncio (§2.2) — o item já existe (fase 1), só marca
                // estado de alerta e guarda o motivo.
                try {
                    registro->run("UPDATE item SET estado = 'alerta', notas_livres = ? WHERE id = ?",
                                  {matriz::db::Value::of(erro.toStdString()), matriz::db::Value::of(itemId)});
                } catch (const std::exception&) {
                }
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

            juce::MessageManager::callAsync([safeThis, estadoLote, contadorParaAtualizar]() {
                if (!safeThis) return;
                safeThis.getComponent()->finalizarUnidadeDeLote(estadoLote, contadorParaAtualizar);
            });
        });
    }
}

void MainComponent::finalizarUnidadeDeLote(std::shared_ptr<EstadoLote> estadoLote,
                                            std::shared_ptr<std::atomic<int>> contadorParaAtualizar) {
    constexpr int kAtualizarACada = 20;

    ingestsPendentes_.fetch_sub(1);
    atualizarLabelProgresso();

    int feitos = contadorParaAtualizar->fetch_add(1) + 1;
    bool ultimo = ingestsPendentes_.load() <= 0;
    if (ultimo || feitos % kAtualizarACada == 0) {
        if (mosaico_) mosaico_->recarregar();
        if (painelInconsistencias_) painelInconsistencias_->recarregar();
        if (filtros_) filtros_->recarregar(); // contagens dos chips mudam com novo material
        // Só na aba EXPLORER: cada arquivo novo pode ter vindo de uma pasta
        // de disco ainda não vista (§5.1) — se recarregasse também na aba
        // BACKUP, perderia qualquer seleção de pasta que o operador tivesse
        // acabado de fazer enquanto o lote ainda processava.
        if (arvore_ && arvore_->abaAtual() == ArvoreComponent::Aba::Origem) arvore_->recarregar();
        atualizarPainelDeApoio();
        atualizarEtapaDoFluxo();
    }

    if (!ultimo) return;

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
        if (arvore_) arvore_->recarregar();
        if (filtros_) filtros_->recarregar();
        atualizarPainelDeApoio();
        atualizarEtapaDoFluxo();
    }

    if (aoConcluirLoteIngestParaTeste) aoConcluirLoteIngestParaTeste(sucessos, erros);

    if (cancelado) {
        mostrarResumoCancelado(sucessos, static_cast<int>(naoProcessados.size()) + sucessos + totalErros);
        return;
    }

    // Produção não mostra diálogo de resumo (§2.1 — nenhum diálogo, nenhuma
    // pergunta): erro já ficou marcado no próprio item (estado='alerta' +
    // notas_livres), visível na grade. O resumo final (item 9, §5 — "HD
    // reconectado... X novos, Y já conhecidos") é discreto, no lugar da
    // barra de progresso, nunca um popup que o operador precisa fechar.
    mostrarResumoLote(sucessos - duplicatas, duplicatas, totalErros);
}

void MainComponent::cancelarLoteIngest() {
    if (!cancelamentoLote_ || !ingestEmAndamento()) return;
    cancelamentoLote_->pedir();
    // Feedback na hora, sem esperar o último job drenar: o operador clicou
    // e precisa ver que o clique valeu, mesmo que ainda faltem alguns
    // callbacks de arquivos em curso chegarem.
    if (labelProgressoIngest_) labelProgressoIngest_->setText(matriz::i18n::t("ingest.cancelando"));
    if (botaoCancelarIngest_) botaoCancelarIngest_->setEnabled(false);
}

void MainComponent::mostrarResumoCancelado(int processados, int total) {
    garantirLabelProgressoIngest();
    labelProgressoIngest_->setText(matriz::i18n::t("ingest.cancelado")
                                        .replace("{feito}", juce::String(processados))
                                        .replace("{total}", juce::String(total)));
    labelProgressoIngest_->definirFracao(-1.0);
    if (botaoCancelarIngest_) {
        botaoCancelarIngest_.reset();
        resized();
    }
}

void MainComponent::atualizarLabelProgresso() {
    int pendentes = ingestsPendentes_.load();
    // Enquanto o lote está rodando, mostra progresso. O estado "terminou"
    // não reseta o rótulo aqui — quem decide o que mostrar depois do
    // último arquivo é mostrarResumoLote(), chamado à parte (item 9, §5:
    // resumo final "X novos, Y já conhecidos" fica discreto no lugar da
    // barra, não desaparece sozinho).
    if (pendentes <= 0) return;

    garantirLabelProgressoIngest();
    int total = ingestsTotalLote_.load();
    int concluidos = total - pendentes;
    labelProgressoIngest_->setText(matriz::i18n::t("ingest.progresso")
                                        .replace("{feito}", juce::String(concluidos))
                                        .replace("{total}", juce::String(total)));
    labelProgressoIngest_->definirFracao(total > 0 ? static_cast<double>(concluidos) / total : 0.0);
}

void MainComponent::garantirLabelProgressoIngest() {
    if (labelProgressoIngest_) return;
    labelProgressoIngest_ = std::make_unique<FaixaProgressoIngest>();
    addAndMakeVisible(*labelProgressoIngest_);

    botaoCancelarIngest_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.cancelar"));
    botaoCancelarIngest_->setColour(juce::TextButton::buttonColourId, tema().painel);
    botaoCancelarIngest_->setColour(juce::TextButton::textColourOffId, tema().perigo);
    botaoCancelarIngest_->onClick = [this] { cancelarLoteIngest(); };
    addAndMakeVisible(*botaoCancelarIngest_);

    resized();
}

void MainComponent::mostrarResumoLote(int novos, int duplicatas, int erros) {
    garantirLabelProgressoIngest();
    juce::String texto = matriz::i18n::t("ingest.resumo_novos").replace("{n}", juce::String(novos));
    if (duplicatas > 0)
        texto += " · " + matriz::i18n::t("ingest.resumo_duplicatas").replace("{n}", juce::String(duplicatas));
    if (erros > 0) texto += " · " + matriz::i18n::t("ingest.resumo_erros").replace("{n}", juce::String(erros));
    labelProgressoIngest_->setText(texto);
    labelProgressoIngest_->definirFracao(-1.0); // lote terminado — faixa cheia com o resumo
    if (botaoCancelarIngest_) {
        botaoCancelarIngest_.reset(); // nada mais a cancelar
        resized();
    }
    atualizarPainelDeApoio();
    atualizarEtapaDoFluxo();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);
    if (!arrastandoArquivo_) return;

    // Feedback visual da janela inteira como alvo de drop (Parte 3.2 da
    // correção crítica) — sem isso o operador não sabe se pode soltar ali.
    g.setColour(tema().acento.withAlpha(0.10f));
    g.fillRect(getLocalBounds());
    g.setColour(tema().acento);
    g.drawRect(getLocalBounds(), 3);
}

void MainComponent::resized() {
    auto area = getLocalBounds();

    if (catalogo_) {
        auto cabecalho = area.removeFromTop(BarraFerramentasComponent::kAltura).reduced(tema().espacoMedio, 8);
        catalogoFechar_->setBounds(cabecalho.removeFromRight(110));
        cabecalho.removeFromRight(tema().espacoMedio);
        catalogoBusca_->setBounds(cabecalho.removeFromRight(juce::jmin(320, cabecalho.getWidth() / 2)));
        cabecalho.removeFromRight(tema().espacoMedio);
        catalogoTitulo_->setBounds(cabecalho);

        catalogoViewport_->setBounds(area);
        catalogo_->setSize(catalogoViewport_->getWidth() - catalogoViewport_->getScrollBarThickness(),
                            catalogo_->getHeight());
        catalogo_->resized();
        return;
    }

    if (!temProjetoAberto()) {
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
        // 160 px cortava "Abrir um projeto que já existe…" em duas linhas
        // dentro de um botão de 32 px de altura.
        telaInicialBotaoAbrir_->setBounds(linhaAbrir.withSizeKeepingCentre(260, 32));

        if (telaInicialRecentesTitulo_) {
            coluna.removeFromTop(tema().espacoGrande);
            telaInicialRecentesTitulo_->setBounds(coluna.removeFromTop(20));
            for (auto& linha : telaInicialLinhasRecentes_) linha->setBounds(coluna.removeFromTop(28));
        }
        return;
    }

    constexpr int kLarguraFicha = 340;
    constexpr int kLarguraArvore = 220;
    constexpr int kAlturaFaixaBotao = 34;
    constexpr int kAlturaPainelInconsistencias = 180;

    // Barra de ferramentas e faixa de orientação atravessam a janela
    // inteira, ACIMA de tudo — inclusive da coluna esquerda. Elas falam do
    // projeto como um todo, não de um painel específico, e ficar no topo
    // absoluto é o que garante que sejam a primeira coisa lida.
    if (barraFerramentas_) barraFerramentas_->setBounds(area.removeFromTop(BarraFerramentasComponent::kAltura));
    if (barraGuia_) barraGuia_->setBounds(area.removeFromTop(barraGuia_->alturaDesejada()));

    // Barra de métricas fixa no rodapé da janela inteira (item 6) — abaixo
    // de tudo, inclusive da coluna esquerda e da ficha, porque fala do
    // arquivo selecionado, não de um painel. Sai do `area` antes de todos os
    // outros painéis pra que nenhum deles a cubra.
    if (barraMetricas_) barraMetricas_->setBounds(area.removeFromBottom(BarraMetricasComponent::kAlturaPreferida));

    // Árvore Origem/Acervo — painel esquerdo fixo (§3.1), largura
    // constante, sempre visível (ao contrário da ficha, que só reserva
    // espaço quando aberta).
    auto areaArvore = area.removeFromLeft(kLarguraArvore);
    auto areaCabecalhoArvore = areaArvore.removeFromTop(kAlturaFaixaBotao).reduced(4, 4);
    botaoNovaPastaAcervo_->setBounds(areaCabecalhoArvore.removeFromRight(28));
    areaCabecalhoArvore.removeFromRight(4);
    int metadeAbas = areaCabecalhoArvore.getWidth() / 2;
    botaoAbaOrigem_->setBounds(areaCabecalhoArvore.removeFromLeft(metadeAbas));
    botaoAbaAcervo_->setBounds(areaCabecalhoArvore);

    // Árvore ocupa uma fração do que sobrou da coluna esquerda; filtros +
    // busca + coleções ficam com o resto embaixo (§9.1 — "Árvore / Filtros
    // / Busca" empilhados na mesma coluna). Ambos roláveis por conta própria.
    int alturaArvore = static_cast<int>(areaArvore.getHeight() * 0.55f);
    arvoreViewport_->setBounds(areaArvore.removeFromTop(alturaArvore));
    filtrosViewport_->setBounds(areaArvore);

    if (arvore_) {
        arvore_->setSize(arvoreViewport_->getWidth() - arvoreViewport_->getScrollBarThickness(), arvore_->getHeight());
        arvore_->resized();
    }
    if (filtros_) {
        filtros_->setSize(filtrosViewport_->getWidth() - filtrosViewport_->getScrollBarThickness(), filtros_->getHeight());
        filtros_->resized();
    }

    // Ficha lateral só reserva espaço quando aberta (§7.1 — "a ficha nunca
    // bloqueia"); fechada, a grade recupera essa largura inteira.
    if (fichaAberta_) {
        auto colunaFicha = area.removeFromRight(kLarguraFicha);
        barraAcoesFicha_->setBounds(colunaFicha.removeFromTop(BarraAcoesFicha::kAltura));
        fichaPanel_->setBounds(colunaFicha);
    }

    // Faixa fixa do painel de inconsistências no Catalog — nunca escondida
    // em menu (§1.3/§3.5), mas nunca por cima da grade acima dela.
    if (painelInconsistenciasViewport_) {
        painelInconsistenciasViewport_->setBounds(area.removeFromBottom(kAlturaPainelInconsistencias));
        if (painelInconsistencias_) {
            painelInconsistencias_->setSize(
                painelInconsistenciasViewport_->getWidth() - painelInconsistenciasViewport_->getScrollBarThickness(),
                painelInconsistencias_->getHeight());
        }
    }

    // Barra de seleção flutua sobre o rodapé da grade — reserva espaço em
    // vez de cobrir a última fileira de células, senão esconderia
    // justamente parte do que está selecionado.
    if (barraSelecao_) {
        // Só desconta espaço da grade quando está de fato à mostra; escondida,
        // continua recebendo bounds (sobrepostos à grade, sem efeito visual)
        // em vez de ficar 0x0 — Component sem tamanho é o que o self-test de
        // UI trata como layout quebrado, e com razão: quando reaparecesse
        // dependeria de um resized() extra pra ter dimensão.
        bool visivel = barraSelecao_->quantidade() > 0;
        auto areaSelecao = visivel ? area.removeFromBottom(BarraSelecaoComponent::kAltura + tema().espacoMedio)
                                   : area.withTop(area.getBottom() - BarraSelecaoComponent::kAltura -
                                                   tema().espacoMedio);
        barraSelecao_->setBounds(areaSelecao.reduced(tema().espacoMedio, 0)
                                      .withTrimmedBottom(tema().espacoMedio)
                                      .withHeight(BarraSelecaoComponent::kAltura));
    }

    // Grade e preview dividem o MESMO espaço, alternando visibilidade
    // (abrirPreview/fecharPreview) — nunca os dois ao mesmo tempo (§3.1/§3.4).
    mosaicoViewport_->setBounds(area);
    if (preview_) preview_->setBounds(area);

    if (mosaico_) {
        mosaico_->setSize(mosaicoViewport_->getWidth() - mosaicoViewport_->getScrollBarThickness(), mosaico_->getHeight());
        mosaico_->resized();
    }

    // Progresso do ingest fica logo abaixo da barra de ferramentas, não no
    // topo absoluto — no topo ele cobria os próprios botões da barra.
    if (labelProgressoIngest_) {
        int y = barraFerramentas_ ? BarraFerramentasComponent::kAltura : 0;
        juce::Rectangle<int> faixa(0, y, getWidth(), 26);
        if (botaoCancelarIngest_) {
            botaoCancelarIngest_->setBounds(faixa.removeFromRight(110).reduced(4, 2));
            botaoCancelarIngest_->toFront(false);
        }
        labelProgressoIngest_->setBounds(faixa);
        labelProgressoIngest_->toFront(false);
    }
}

} // namespace matriz::ui
