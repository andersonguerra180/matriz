#include "JanelaPastaFlutuante.h"

#include "../I18n/Strings.h"
#include "AcoesItem.h"
#include "FiltrosComponent.h"
#include "FloatingPreviewWindow.h"
#include "Moldura.h"
#include "MosaicoComponent.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {
constexpr int kLarguraFiltros = 290;
constexpr int kMenuExtrairDaPasta = 9001;
constexpr int kMenuMoverParaPasta = 9002;
}  // namespace

class JanelaPastaFlutuante::Conteudo : public juce::Component {
public:
    Conteudo(ProjetoAberto& projeto, const std::string& pastaId, const juce::String& nome, std::function<void()> aoMudouDados)
        : projeto_(projeto), pastaId_(pastaId), nome_(nome), aoMudouDados_(std::move(aoMudouDados)) {
        mosaico_ = std::make_unique<MosaicoComponent>(projeto_);
        mosaico_->aoAbrirPreview = [this](const std::string& id) { abrirPreview(id); };
        mosaico_->aoPedirMenuContexto = [this](std::vector<std::string> ids) { abrirMenu(std::move(ids)); };
        mosaico_->aoMudarSelecao = [this] { atualizarContagem(); };
        mosaico_->aoMudarConteudoVisivel = [this] { atualizarContagem(); };
        viewportGrade_.setViewedComponent(mosaico_.get(), false);
        addAndMakeVisible(viewportGrade_);

        filtros_ = std::make_unique<FiltrosComponent>(projeto_, *mosaico_);
        viewportFiltros_.setViewedComponent(filtros_.get(), false);
        addAndMakeVisible(viewportFiltros_);

        lblContagem_.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        lblContagem_.setColour(juce::Label::textColourId, tema().textoSecundario);
        addAndMakeVisible(lblContagem_);

        setSize(1180, 720);
        recarregarItens();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(tema().fundo);
        g.setColour(tema().borda);
        g.fillRect(kLarguraFiltros, 0, 1, getHeight());
    }

    void resized() override {
        auto area = getLocalBounds();
        viewportFiltros_.setBounds(area.removeFromLeft(kLarguraFiltros));
        filtros_->setSize(viewportFiltros_.getMaximumVisibleWidth(), juce::jmax(filtros_->getHeight(), viewportFiltros_.getHeight()));
        area.removeFromLeft(1);
        auto topo = area.removeFromTop(30).reduced(12, 4);
        lblContagem_.setBounds(topo);
        viewportGrade_.setBounds(area);
        mosaico_->setSize(viewportGrade_.getMaximumVisibleWidth(), juce::jmax(viewportGrade_.getHeight(), mosaico_->getHeight()));
    }

    // Todos os arquivos da pasta e das subpastas (consulta de leitura).
    void recarregarItens() {
        ids_.clear();
        pastasDaArvore_.clear();
        try {
            auto& db = projeto_.projeto().registroLeitura();
            auto st = db.prepare(
                "WITH RECURSIVE sub(id) AS (SELECT ? UNION ALL SELECT p.id FROM acervo_pasta p JOIN sub s ON p.pasta_pai_id = s.id) "
                "SELECT id FROM sub");
            st.bind(1, matriz::db::Value::of(pastaId_));
            while (st.step()) pastasDaArvore_.push_back(st.columnText(0));
            for (const auto& pid : pastasDaArvore_) {
                auto it = db.prepare("SELECT item_id FROM acervo_item_pasta WHERE pasta_id = ?");
                it.bind(1, matriz::db::Value::of(pid));
                while (it.step()) ids_.insert(it.columnText(0));
            }
        } catch (...) {}
        mosaico_->recarregar();
        mosaico_->definirFiltroItens(ids_);
        filtros_->recarregar();
        atualizarContagem();
    }

private:
    void atualizarContagem() {
        const int n = static_cast<int>(ids_.size());
        const int sel = static_cast<int>(mosaico_->itensSelecionados().size());
        lblContagem_.setText(nome_ + juce::String::fromUTF8("  \xc2\xb7  ") +
                                 i18n::t("janela_pasta.contagem").replace("{n}", juce::String(n)) +
                                 (sel > 0 ? juce::String::fromUTF8("  \xc2\xb7  ") + i18n::t("janela_pasta.selecionados").replace("{n}", juce::String(sel)) : juce::String()),
                             juce::dontSendNotification);
    }

    void aoMudarDados() {
        recarregarItens();
        if (aoMudouDados_) aoMudouDados_();
    }

    acoes::Ganchos ganchos() {
        acoes::Ganchos g;
        juce::Component::SafePointer<Conteudo> seguro(this);
        g.aoMudarDados = [seguro] { if (seguro) seguro->aoMudarDados(); };
        g.aoAbrirDetalhes = [] {};
        g.aoFiltrarItens = [seguro](std::set<std::string> ids) { if (seguro) seguro->mosaico_->definirFiltroItens(std::move(ids)); };
        g.itemEmMemoria = [seguro](const std::string& id) -> const ItemResumo* { return seguro ? seguro->mosaico_->itemEmMemoria(id) : nullptr; };
        return g;
    }

    // Botão direito: o menu padrão do app + EXTRACT (tira da pasta) + MOVE TO FOLDER.
    void abrirMenu(std::vector<std::string> ids) {
        if (ids.empty()) return;
        auto menu = acoes::construirMenu(projeto_, ids, ganchos().itemEmMemoria);
        menu.addSeparator();
        menu.addItem(kMenuExtrairDaPasta, i18n::t("janela_pasta.extrair"));
        menu.addItem(kMenuMoverParaPasta, i18n::t("janela_pasta.mover"));
        juce::Component::SafePointer<Conteudo> seguro(this);
        ProjetoAberto* proj = &projeto_;
        const auto gs = ganchos();
        menu.showMenuAsync(juce::PopupMenu::Options(), [seguro, proj, ids, gs](int r) {
            if (seguro == nullptr || r == 0) return;
            if (r == kMenuExtrairDaPasta) {
                for (const auto& id : ids)
                    for (const auto& pid : seguro->pastasDaArvore_) proj->removerItemDaPasta(id, pid);
                seguro->aoMudarDados();
            } else if (r == kMenuMoverParaPasta) {
                acoes::enviarParaPasta(*proj, ids, gs, seguro.getComponent());
            } else {
                acoes::executar(r, *proj, ids, gs);
            }
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
    std::vector<std::string> pastasDaArvore_;
    std::unique_ptr<MosaicoComponent> mosaico_;
    std::unique_ptr<FiltrosComponent> filtros_;
    juce::Viewport viewportGrade_, viewportFiltros_;
    juce::Label lblContagem_;
    std::unique_ptr<FloatingPreviewWindow> janelaPreview_;
};

JanelaPastaFlutuante::JanelaPastaFlutuante(ProjetoAberto& projeto, const std::string& pastaId, const juce::String& nomePasta,
                                           std::function<void()> aoFechar, std::function<void()> aoMudouDados)
    : juce::DocumentWindow("FOLDER: " + nomePasta, moldura::paleta().fundo, juce::DocumentWindow::closeButton),
      aoFechar_(std::move(aoFechar)) {
    // Moldura do sistema de janelas: sem barra nativa; cabeçalho + X redondo (mesma ação do antigo EXIT).
    setUsingNativeTitleBar(false);
    setTitleBarHeight(0);
    setResizable(true, false);
    auto* conteudo = new Conteudo(projeto, pastaId, nomePasta, std::move(aoMudouDados));
    conteudo_ = conteudo;
    const juce::String titulo = i18n::t("arvore_backup.sobreposto_titulo").replace("{nome}", nomePasta);
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
