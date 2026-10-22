#include "JanelaPastaFlutuante.h"
#include "TiposContainer.h"

#include "../I18n/Strings.h"
#include "AcoesItem.h"
#include "ImportarMenu.h"
#include "OverlayComponent.h"
#include "PainelMetadadosArquivo.h"
#include "FiltrosComponent.h"
#include "FloatingPreviewWindow.h"
#include "Moldura.h"
#include "MosaicoComponent.h"
#include "ContainerVisual.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {
constexpr int kLarguraFiltros = 290;
constexpr int kLarguraFicha = 340;
constexpr int kAlturaCabecalho = 46;
constexpr int kMenuExtrairDaPasta = 9001;
constexpr int kMenuMoverParaPasta = 9002;

void coletarIdsRecursivos(const ProjetoAberto::NoArvore& no, std::set<std::string>& out) {
    out.insert(no.itemIds.begin(), no.itemIds.end());
    out.insert(no.itemIdsDiretos.begin(), no.itemIdsDiretos.end());
    for (const auto& f : no.filhos) coletarIdsRecursivos(f, out);
}

const ProjetoAberto::NoArvore* acharNo(const ProjetoAberto::NoArvore& no, const std::string& id) {
    if (no.id == id) return &no;
    for (const auto& f : no.filhos)
        if (const auto* achou = acharNo(f, id)) return achou;
    return nullptr;
}
}  // namespace

// Janela da pasta (duplo clique no nó do Folder Map), como no Mini: cabeçalho com o nome (duplo clique renomeia), VOLTAR e
// busca; coluna esquerda com os filtros (só desta pasta) e a árvore das subpastas; grade com todos os arquivos da pasta e das
// subpastas; ficha de metadados que só aparece quando há arquivo selecionado.
class JanelaPastaFlutuante::Conteudo : public juce::Component {
public:
    Conteudo(ProjetoAberto& projeto, const std::string& pastaId, const juce::String& nome, std::function<void()> aoMudouDados,
             std::string nestId)
        : projeto_(projeto), pastaId_(pastaId), nome_(nome), aoMudouDados_(std::move(aoMudouDados)), nestId_(std::move(nestId)) {
        mosaico_ = std::make_unique<MosaicoComponent>(projeto_);
        mosaico_->aoAbrirPreview = [this](const std::string& id) { abrirPreview(id); };
        mosaico_->aoPedirMenuContexto = [this](std::vector<std::string> ids) { abrirMenu(std::move(ids)); };
        mosaico_->aoMudarSelecao = [this] { aoMudarSelecao(); };
        mosaico_->aoMudarConteudoVisivel = [this] {
            atualizarContagem();
            if (aguardandoCarga_) {  // a grade terminou de carregar: agora as contagens dos filtros (só desta pasta) são calculáveis
                aguardandoCarga_ = false;
                filtros_->recarregar();
            }
        };
        viewportGrade_.setViewedComponent(mosaico_.get(), false);
        viewportGrade_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewportGrade_);

        filtros_ = std::make_unique<FiltrosComponent>(projeto_, *mosaico_, false);
        viewportFiltros_.setViewedComponent(filtros_.get(), false);
        addAndMakeVisible(viewportFiltros_);

        // Metadados originais do arquivo selecionado, SOMENTE LEITURA (o mesmo painel do INTAKE).
        painel_ = std::make_unique<PainelMetadadosArquivo>(projeto_);
        addChildComponent(*painel_);

        // Thumbnails / lista, como na aba METADATA.
        for (auto* b : { &btnGrade_, &btnLista_ }) {
            b->setClickingTogglesState(false);
            addAndMakeVisible(*b);
        }
        btnGrade_.setButtonText(i18n::t("janela_pasta.view_grade"));
        btnLista_.setButtonText(i18n::t("janela_pasta.view_lista"));
        btnGrade_.setToggleState(true, juce::dontSendNotification);
        btnGrade_.onClick = [this] { definirVisao(MosaicoComponent::ModoVisao::Grade); };
        btnLista_.onClick = [this] { definirVisao(MosaicoComponent::ModoVisao::Lista); };

        btnVoltar_.setButtonText(i18n::t("janela_pasta.voltar"));
        btnVoltar_.onClick = [this] { voltarUmaPasta(); };
        addChildComponent(btnVoltar_);

        lblTitulo_.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
        lblTitulo_.setColour(juce::Label::textColourId, tema().textoPrimario);
        // Duplo clique no nome abre a caixinha de renomear a pasta.
        if (nestId_.empty() && !projeto_.somenteLeitura() && !projeto_.mapaSelecionadoNoFolderMap().empty() &&
            projeto_.mapaSelecionadoNoFolderMap() != ProjetoAberto::kMapaOriginal) {
            lblTitulo_.setEditable(false, true, true);
            lblTitulo_.onTextChange = [this] {
                const auto novo = lblTitulo_.getText().trim();
                if (novo.isEmpty() || novo == nome_ || !projeto_.renomearPastaAcervo(pastaId_, novo.toStdString())) {
                    lblTitulo_.setText(nome_, juce::dontSendNotification);
                    return;
                }
                nome_ = novo;
                aoMudarDados();
            };
        }
        addAndMakeVisible(lblTitulo_);

        campoBusca_.setTextToShowWhenEmpty(i18n::t("janela_pasta.busca"), tema().textoTerciario);
        campoBusca_.setIndents(12, 0);
        campoBusca_.onTextChange = [this] { mosaico_->definirBusca(campoBusca_.getText().trim()); };
        addAndMakeVisible(campoBusca_);

        lblContagem_.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        lblContagem_.setColour(juce::Label::textColourId, tema().textoSecundario);
        addAndMakeVisible(lblContagem_);

        arvore_.setRootItemVisible(true);
        arvore_.setDefaultOpenness(true);
        arvore_.setIndentSize(14);
        arvore_.setColour(juce::TreeView::backgroundColourId, tema().painel);
        addAndMakeVisible(arvore_);

        setSize(1180, 720);
        recarregarItens();
    }

    ~Conteudo() override { arvore_.setRootItem(nullptr); }

    void paint(juce::Graphics& g) override {
        g.fillAll(tema().fundo);
        g.setColour(tema().borda);
        g.fillRect(kLarguraFiltros, kAlturaCabecalho, 1, getHeight() - kAlturaCabecalho);
        g.fillRect(0, kAlturaCabecalho - 1, getWidth(), 1);
        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        g.drawText(i18n::t("janela_pasta.subpastas"), rotuloArvore_, juce::Justification::centredLeft, false);
    }

    void resized() override {
        auto area = getLocalBounds();
        auto cab = area.removeFromTop(kAlturaCabecalho).reduced(12, 6);
        if (btnVoltar_.isVisible()) {
            btnVoltar_.setBounds(cab.removeFromLeft(86));
            cab.removeFromLeft(10);
        }
        campoBusca_.setBounds(cab.removeFromRight(juce::jmin(320, cab.getWidth() / 3)));
        cab.removeFromRight(12);
        btnLista_.setBounds(cab.removeFromRight(70));
        btnGrade_.setBounds(cab.removeFromRight(70));
        cab.removeFromRight(12);
        lblTitulo_.setBounds(cab);

        auto esquerda = area.removeFromLeft(kLarguraFiltros);
        area.removeFromLeft(1);
        const int alturaFiltros = juce::jmin(esquerda.getHeight() * 3 / 5, 520);
        viewportFiltros_.setBounds(esquerda.removeFromTop(alturaFiltros));
        filtros_->setSize(viewportFiltros_.getMaximumVisibleWidth(), juce::jmax(filtros_->getHeight(), viewportFiltros_.getHeight()));
        rotuloArvore_ = esquerda.removeFromTop(22).reduced(10, 0);
        arvore_.setBounds(esquerda);

        if (painel_->isVisible()) {
            painel_->setBounds(area.removeFromRight(kLarguraFicha));
            area.removeFromRight(1);
        }
        auto topo = area.removeFromTop(30).reduced(12, 4);
        lblContagem_.setBounds(topo);
        if (overlay_ != nullptr) overlay_->setBounds(getLocalBounds());
        viewportGrade_.setBounds(area);
        mosaico_->setSize(viewportGrade_.getMaximumVisibleWidth(), juce::jmax(viewportGrade_.getHeight(), mosaico_->getHeight()));
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey && !historico_.empty()) { voltarUmaPasta(); return true; }
        return false;
    }

    // Todos os arquivos da pasta e das subpastas — do mapa selecionado no Folder Map (o ORIGINAL é virtual: os ids vêm da
    // árvore dele, não de acervo_item_pasta).
    void recarregarItens() {
        ids_.clear();
        const ProjetoAberto::NoArvore* noRaiz = nullptr;
        if (!nestId_.empty()) {  // container: só os arquivos dele
            for (const auto& id : projeto_.membrosDoNest(nestId_)) ids_.insert(id);
            ProjetoAberto::NoArvore soRaiz;
            soRaiz.id = nestId_;
            soRaiz.nome = nome_;
            soRaiz.itemIds = ids_;
            lblTitulo_.setText(nome_, juce::dontSendNotification);
            construirArvore(soRaiz);
            carregarGrade();
            return;
        }
        try {
            std::string mapa = projeto_.mapaSelecionadoNoFolderMap();
            if (mapa.empty()) mapa = projeto_.mapaAtivoPadrao();
            arvoreDoMapa_ = projeto_.arvoreAcervo(mapa.empty() ? ProjetoAberto::kMapaOriginal : mapa);
            noRaiz = acharNo(arvoreDoMapa_, pastaId_);
        } catch (...) {}
        ProjetoAberto::NoArvore noCopia;
        if (noRaiz != nullptr) {
            noCopia = *noRaiz;
            coletarIdsRecursivos(*noRaiz, ids_);
        } else {
            noCopia.id = pastaId_;
            noCopia.nome = nome_;
        }
        if (ids_.empty() && noRaiz == nullptr) {  // pasta fora da árvore (ex.: _trash): consulta direta
            try {
                auto& db = projeto_.projeto().registroLeitura();
                auto st = db.prepare(
                    "WITH RECURSIVE sub(id) AS (SELECT ? UNION ALL SELECT p.id FROM acervo_pasta p JOIN sub s ON p.pasta_pai_id = s.id) "
                    "SELECT DISTINCT item_id FROM acervo_item_pasta WHERE pasta_id IN (SELECT id FROM sub)");
                st.bind(1, matriz::db::Value::of(pastaId_));
                while (st.step()) ids_.insert(st.columnText(0));
            } catch (...) {}
        }
        lblTitulo_.setText(nome_, juce::dontSendNotification);
        construirArvore(noCopia);
        carregarGrade();
    }

private:
    // O snapshot da grade roda em background (como no resto do app); os filtros recontam quando ele termina.
    void carregarGrade() {
        aguardandoCarga_ = true;
        mosaico_->definirFiltroItens(ids_);
        filtros_->definirEscopo(ids_);
        mosaico_->recarregar();
        atualizarContagem();
        resized();
    }

    // ── árvore das subpastas ──────────────────────────────────────────────────────────────────────────────
    class ItemPasta : public juce::TreeViewItem {
    public:
        ItemPasta(Conteudo& dono, ProjetoAberto::NoArvore no, bool raiz) : dono_(dono), no_(std::move(no)), raiz_(raiz) {
            coletarIdsRecursivos(no_, ids_);
        }
        bool mightContainSubItems() override { return !no_.filhos.empty(); }
        int getItemHeight() const override { return 22; }
        void itemOpennessChanged(bool aberto) override {
            if (aberto && getNumSubItems() == 0)
                for (const auto& f : no_.filhos) addSubItem(new ItemPasta(dono_, f, false));
        }
        void paintItem(juce::Graphics& g, int w, int h) override {
            const auto& tk = tema();
            if (isSelected()) { g.setColour(tk.acento.withAlpha(0.18f)); g.fillRoundedRectangle(2.0f, 1.0f, static_cast<float>(w - 4), static_cast<float>(h - 2), 4.0f); }
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena + 1.0f, raiz_ ? juce::Font::bold : juce::Font::plain)));
            g.drawText(no_.nome, 6, 0, w - 52, h, juce::Justification::centredLeft, true);
            g.setColour(tk.textoTerciario);
            g.drawText(juce::String(static_cast<int>(ids_.size())), w - 48, 0, 42, h, juce::Justification::centredRight, false);
        }
        void itemClicked(const juce::MouseEvent&) override { dono_.aoSelecionarSubpasta(ids_); }
        void itemDoubleClicked(const juce::MouseEvent&) override { if (!raiz_) dono_.entrarNaPasta(no_.id, no_.nome); }
        juce::String getUniqueName() const override { return juce::String(no_.id); }

    private:
        Conteudo& dono_;
        ProjetoAberto::NoArvore no_;
        bool raiz_;
        std::set<std::string> ids_;
    };

    void construirArvore(const ProjetoAberto::NoArvore& no) {
        arvore_.setRootItem(nullptr);
        raizArvore_ = std::make_unique<ItemPasta>(*this, no, true);
        arvore_.setRootItem(raizArvore_.get());
        raizArvore_->setOpen(true);
    }

    void aoSelecionarSubpasta(const std::set<std::string>& ids) {
        mosaico_->definirFiltroItens(ids.empty() ? ids_ : ids);
        mosaico_->recarregar();
    }

    void entrarNaPasta(const std::string& id, const juce::String& nome) {
        if (id.empty()) return;
        historico_.emplace_back(pastaId_, nome_);
        pastaId_ = id;
        nome_ = nome;
        btnVoltar_.setVisible(true);
        recarregarItens();
    }

    void voltarUmaPasta() {
        if (historico_.empty()) return;
        const auto anterior = historico_.back();
        historico_.pop_back();
        pastaId_ = anterior.first;
        nome_ = anterior.second;
        btnVoltar_.setVisible(!historico_.empty());
        recarregarItens();
    }

    // ── ficha lateral ─────────────────────────────────────────────────────────────────────────────────────
    void definirVisao(MosaicoComponent::ModoVisao modo) {
        mosaico_->definirModoVisao(modo);
        btnGrade_.setToggleState(modo == MosaicoComponent::ModoVisao::Grade, juce::dontSendNotification);
        btnLista_.setToggleState(modo == MosaicoComponent::ModoVisao::Lista, juce::dontSendNotification);
        resized();
    }

    void aoMudarSelecao() {
        const auto sel = mosaico_->itensSelecionados();
        if (sel.empty()) {
            painel_->setVisible(false);
        } else {
            painel_->mostrarItem(*sel.begin());
            painel_->setVisible(true);
        }
        atualizarContagem();
        resized();
    }

    void atualizarContagem() {
        const int n = static_cast<int>(ids_.size());
        const int sel = static_cast<int>(mosaico_->itensSelecionados().size());
        lblContagem_.setText(nome_ + juce::String::fromUTF8("  \xc2\xb7  ") +
                                 i18n::t("janela_pasta.contagem").replace("{n}", juce::String(n)) +
                                 (sel > 0 ? juce::String::fromUTF8("  \xc2\xb7  ") + i18n::t("janela_pasta.selecionados").replace("{n}", juce::String(sel)) : juce::String()),
                             juce::dontSendNotification);
    }

    void aoMudarDados() {
        mosaico_->recarregar();
        filtros_->recarregar();
        if (aoMudouDados_) aoMudouDados_();
    }

    acoes::Ganchos ganchos() {
        acoes::Ganchos g;
        juce::Component::SafePointer<Conteudo> seguro(this);
        g.aoMudarDados = [seguro] { if (seguro) seguro->recarregarItens(); if (seguro && seguro->aoMudouDados_) seguro->aoMudouDados_(); };
        g.aoAbrirDetalhes = [] {};
        g.aoFiltrarItens = [seguro](std::set<std::string> ids) { if (seguro) seguro->mosaico_->definirFiltroItens(std::move(ids)); };
        g.itemEmMemoria = [seguro](const std::string& id) -> const ItemResumo* { return seguro ? seguro->mosaico_->itemEmMemoria(id) : nullptr; };
        g.aoEditarQuestionario = [seguro](const std::string& nestId, const std::string& origem) { if (seguro) seguro->editarProjeto(nestId, origem); };
        return g;
    }

    // Botão direito: o menu padrão do app + EXTRACT (tira da pasta) + MOVE TO FOLDER.
    void abrirMenu(std::vector<std::string> ids) {
        if (ids.empty()) return;
        auto menu = acoes::construirMenu(projeto_, ids, ganchos().itemEmMemoria, true);
        menu.addSeparator();
        menu.addItem(kMenuExtrairDaPasta, i18n::t("janela_pasta.extrair"));
        menu.addItem(kMenuMoverParaPasta, i18n::t("janela_pasta.mover"));
        juce::Component::SafePointer<Conteudo> seguro(this);
        ProjetoAberto* proj = &projeto_;
        const auto gs = ganchos();
        menu.showMenuAsync(juce::PopupMenu::Options(), [seguro, proj, ids, gs](int r) {
            if (seguro == nullptr || r == 0) return;
            if (r == kMenuExtrairDaPasta) {
                std::set<std::string> pastas;
                std::function<void(const ProjetoAberto::NoArvore&)> coleta = [&](const ProjetoAberto::NoArvore& n) {
                    pastas.insert(n.id);
                    for (const auto& f : n.filhos) coleta(f);
                };
                if (const auto* raiz = acharNo(seguro->arvoreDoMapa_, seguro->pastaId_)) coleta(*raiz);
                for (const auto& id : ids)
                    for (const auto& pid : pastas) proj->removerItemDaPasta(id, pid);
                seguro->recarregarItens();
                if (seguro->aoMudouDados_) seguro->aoMudouDados_();
            } else if (r == kMenuMoverParaPasta) {
                acoes::enviarParaPasta(*proj, ids, gs, seguro.getComponent());
            } else {
                acoes::executar(r, *proj, ids, gs);
            }
        });
    }

    // Edit Project Info: a ficha do IMPORT reabre preenchida sobre esta janela.
    void editarProjeto(const std::string& nestId, const std::string& origem) {
        if (overlay_ != nullptr) return;
        overlay_ = std::make_unique<PainelOverlay>();
        addAndMakeVisible(*overlay_);
        overlay_->setBounds(getLocalBounds());
        juce::Component::SafePointer<Conteudo> seguro(this);
        editarQuestionarioDoContainer(*overlay_, projeto_, nestId, origem, [seguro](bool aplicou) {
            juce::MessageManager::callAsync([seguro, aplicou] {
                if (seguro == nullptr) return;
                seguro->overlay_.reset();
                if (aplicou) seguro->aoMudarDados();
                seguro->recarregarItens();
            });
        });
    }

    void abrirPreview(const std::string& itemId) {
        janelaPreview_.reset();
        juce::Component::SafePointer<Conteudo> seguro(this);
        auto aoFechar = [seguro] {
            juce::MessageManager::callAsync([seguro] { if (seguro) seguro->janelaPreview_.reset(); });
        };
        auto aoNavegar = [seguro](const std::string& atual, int dir) -> std::optional<std::string> {
            if (!seguro) return std::nullopt;
            return seguro->mosaico_->itemAdjacente(atual, dir);
        };
        auto aoItemMudou = [seguro](const std::string& novo) { if (seguro) seguro->mosaico_->selecionarItem(novo); };
        janelaPreview_ = std::make_unique<FloatingPreviewWindow>(projeto_, itemId, aoFechar, aoNavegar, aoItemMudou);
    }

    ProjetoAberto& projeto_;
    std::string pastaId_;
    juce::String nome_;
    std::function<void()> aoMudouDados_;
    std::set<std::string> ids_;
    ProjetoAberto::NoArvore arvoreDoMapa_;
    std::vector<std::pair<std::string, juce::String>> historico_;  // (id, nome) das pastas por onde se entrou, para VOLTAR
    std::unique_ptr<MosaicoComponent> mosaico_;
    std::unique_ptr<FiltrosComponent> filtros_;
    std::unique_ptr<PainelMetadadosArquivo> painel_;
    std::unique_ptr<PainelOverlay> overlay_;
    juce::Viewport viewportGrade_, viewportFiltros_;
    juce::TextButton btnVoltar_, btnGrade_, btnLista_;
    std::string nestId_;  // não vazio = janela de um CONTAINER (só os membros dele)
    bool aguardandoCarga_ = false;
    juce::Label lblTitulo_, lblContagem_;
    juce::TextEditor campoBusca_;
    juce::TreeView arvore_;
    std::unique_ptr<ItemPasta> raizArvore_;
    juce::Rectangle<int> rotuloArvore_;
    std::unique_ptr<FloatingPreviewWindow> janelaPreview_;
};

JanelaPastaFlutuante::JanelaPastaFlutuante(ProjetoAberto& projeto, const std::string& pastaId, const juce::String& nomePasta,
                                           std::function<void()> aoFechar, std::function<void()> aoMudouDados, const std::string& nestId)
    : juce::DocumentWindow("FOLDER: " + nomePasta, moldura::paleta().fundo, juce::DocumentWindow::closeButton),
      aoFechar_(std::move(aoFechar)) {
    // Moldura do sistema de janelas: sem barra nativa; cabeçalho + X redondo (mesma ação do antigo EXIT).
    setUsingNativeTitleBar(false);
    setTitleBarHeight(0);
    setResizable(true, false);
    auto* conteudo = new Conteudo(projeto, pastaId, nomePasta, std::move(aoMudouDados), nestId);
    conteudo_ = conteudo;
    juce::String titulo = i18n::t("arvore_backup.sobreposto_titulo").replace("{nome}", nomePasta);
    if (!nestId.empty()) {  // container: o tipo do PROJECT em destaque (DDP MASTER, VINYL TRANSCRIPTION...) e o nome
        const std::string origemDoNest = projeto.origemDoContainer(nestId);
        const TipoContainer* tipoDoNest = tipoDeContainerDaOrigem(projeto.tipoDoContainer(nestId), origemDoNest);
        const auto tipo = (tipoDoNest != nullptr && !tipoDoNest->temPlayer()) ? rotuloDoTipoContainer(*tipoDoNest).toUpperCase()
                                                                               : rotuloDoProject(juce::String(origemDoNest));
        titulo = tipo.isNotEmpty() ? tipo + juce::String::fromUTF8("  \xc2\xb7  ") + nomePasta : nomePasta;
    }
    auto* moldurada = new moldura::JanelaMoldura(conteudo, moldura::Secao::Explorador, "Folder Map", titulo);
    setContentOwned(moldurada, true);
    setResizeLimits(900, 560, 4000, 3000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    toFront(true);
}

JanelaPastaFlutuante::~JanelaPastaFlutuante() { clearContentComponent(); }

void JanelaPastaFlutuante::closeButtonPressed() {
    juce::Component::SafePointer<JanelaPastaFlutuante> seguro(this);
    juce::MessageManager::callAsync([seguro, cb = aoFechar_] { if (seguro != nullptr && cb) cb(); });
}

bool JanelaPastaFlutuante::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) { closeButtonPressed(); return true; }
    return DocumentWindow::keyPressed(key);
}

}  // namespace matriz::ui
