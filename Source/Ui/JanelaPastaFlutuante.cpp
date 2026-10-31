#include "JanelaPastaFlutuante.h"
#include "TiposContainer.h"

#include "../I18n/Strings.h"
#include "AcoesItem.h"
#include "ImportarMenu.h"
#include "OverlayComponent.h"
#include "PainelMetadadosArquivo.h"
#include "FiltrosComponent.h"
#include "FichaPanelComponent.h"
#include "FloatingPreviewWindow.h"
#include "LegendaAtalhosIntake.h"
#include "MatrizLookAndFeel.h"
#include "Moldura.h"
#include "MosaicoComponent.h"
#include "ContainerVisual.h"
#include "EstiloExplorer.h"
#include "Tokens.h"

#include <AssetsBinaryData.h>

namespace matriz::ui {

namespace {
// Layout da janela Explorer · Folder Map (padrão do BKR Matriz Mini).
constexpr int kLarguraLateral = 268;
constexpr int kLarguraFicha = 540;  // a mesma largura da ficha do GRID (aba METADATA): campos nas mesmas posições
constexpr int kAlturaBarraNav = 56;
constexpr int kAlturaTituloGrade = 46;
constexpr int kMargemGrade = 16;
constexpr int kAlturaLinhaArvore = 28;
constexpr int kRecuoArvore = 14;
constexpr int kMenuExtrairDaPasta = 9001;
constexpr int kMenuMoverParaPasta = 9002;
constexpr int kMenuExtrairDoContainer = 9003;

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

// Caminho de `raiz` até o nó `id` (inclusive); vazio se não achou.
bool caminhoAte(const ProjetoAberto::NoArvore& raiz, const std::string& id, std::vector<const ProjetoAberto::NoArvore*>& caminho) {
    caminho.push_back(&raiz);
    if (raiz.id == id) return true;
    for (const auto& f : raiz.filhos)
        if (caminhoAte(f, id, caminho)) return true;
    caminho.pop_back();
    return false;
}

int contarSubpastas(const ProjetoAberto::NoArvore& no) {
    int n = static_cast<int>(no.filhos.size());
    for (const auto& f : no.filhos) n += contarSubpastas(f);
    return n;
}

juce::String contagemI18n(const char* chave, int n) {
    const bool pt = i18n::localeAtivo().startsWith("pt");
    return i18n::t(juce::String(chave) + (n == 1 ? ".1" : ".n")).replace("{n}", explorer::comMilhares(n, pt));
}

const juce::String kPonto = juce::String::fromUTF8(" \xc2\xb7 ");
const juce::String kSeta = juce::String::fromUTF8(" \xe2\x80\xba ");

// A janela inteira em Jost (botões incluídos) e barras de rolagem finas, sem o "knob" redondo do resto do app.
class LookAndFeelExplorer : public MatrizLookAndFeel {
public:
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return explorer::fonte(13.0f, true); }
    int getDefaultScrollbarWidth() override { return 10; }
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical, int inicio, int tamanho,
                       bool sobre, bool apertado) override {
        if (tamanho <= 0) return;
        const auto c = explorer::cores();
        const float espessura = sobre || apertado ? 6.0f : 4.0f;
        juce::Rectangle<float> r = vertical ? juce::Rectangle<float>(x + (width - espessura) * 0.5f, static_cast<float>(inicio) + 2.0f,
                                                                     espessura, static_cast<float>(tamanho) - 4.0f)
                                            : juce::Rectangle<float>(static_cast<float>(inicio) + 2.0f, y + (height - espessura) * 0.5f,
                                                                     static_cast<float>(tamanho) - 4.0f, espessura);
        g.setColour(c.secundario.withAlpha(sobre || apertado ? 0.55f : 0.32f));
        g.fillRoundedRectangle(r, espessura * 0.5f);
    }
};

// Pílula de texto da barra de navegação ("‹ Back", Thumbs | List).
void desenharPilula(juce::Graphics& g, juce::Rectangle<float> r, bool ativa, const juce::String& texto, bool contorno = true) {
    const auto c = explorer::cores();
    g.setColour(ativa ? c.grafite : c.cartao);
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    if (contorno && !ativa) {
        g.setColour(c.borda);
        g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
    }
    g.setColour(ativa ? c.sobreGrafite : c.texto);
    g.setFont(explorer::fonte(13.5f, ativa));
    g.drawText(texto, r.toNearestInt(), juce::Justification::centred, false);
}
}  // namespace

// Janela da pasta (duplo clique no nó do Folder Map) e do Project Container, no padrão do Mini: cabeçalho da moldura; barra de
// navegação com "‹ Back", o caminho clicável, Thumbs | List e a busca; lateral com a árvore de pastas e os filtros; grade de
// cartões com título e resumo; painel da direita com o arquivo selecionado ou o resumo da pasta.
class JanelaPastaFlutuante::Conteudo : public juce::Component {
    friend class JanelaPastaFlutuante;

public:
    Conteudo(ProjetoAberto& projeto, const std::string& pastaId, const juce::String& nome, std::function<void()> aoMudouDados,
             std::string nestId, bool selecionarTudo)
        : projeto_(projeto), pastaId_(pastaId), nome_(nome), nomeRaiz_(nome), aoMudouDados_(std::move(aoMudouDados)),
          nestId_(std::move(nestId)), semPasta_(pastaId == kPastaSemPasta), selecionarTudo_(selecionarTudo) {
        configurarLookAndFeel(laf_);  // as mesmas cores do resto do app (a ficha do GRID depende delas)
        setLookAndFeel(&laf_);
        raizJanelaId_ = nestId_.empty() ? pastaId_ : nestId_;
        if (!nestId_.empty()) pastaId_ = nestId_;

        mosaico_ = std::make_unique<MosaicoComponent>(projeto_);
        mosaico_->definirEstiloExplorer(true);
        mosaico_->aoAbrirPreview = [this](const std::string& id) { abrirPreview(id); };
        mosaico_->aoPedirMenuContexto = [this](std::vector<std::string> ids) { abrirMenu(std::move(ids)); };
        mosaico_->aoMudarSelecao = [this] { aoMudarSelecao(); };
        mosaico_->aoMudarConteudoVisivel = [this] {
            atualizarContagem();
            if (aguardandoCarga_) {  // a grade terminou de carregar: agora as contagens dos filtros (só desta pasta) são calculáveis
                aguardandoCarga_ = false;
                filtros_->recarregar();
            }
            atualizarPainelSemSelecao();
            // Aberta pelo card NO FOLDER: chega com todos os arquivos selecionados — só depois que o snapshot chegou (o
            // definirFiltroItens de carregarGrade já avisa aqui, com a grade ainda vazia).
            if (selecionarTudo_ && !mosaico_->snapshotPendente() && mosaico_->totalItensCarregados() > 0) {
                selecionarTudo_ = false;
                mosaico_->selecionarTodos();
            }
        };
        if (!nestId_.empty()) mosaico_->definirAgruparNests(false);  // container: cada arquivo é uma célula, não a pilha do nest
        // Cartão de pasta: clique simples seleciona (resumo à direita), duplo clique entra.
        mosaico_->aoNavegarParaSubpasta = [this](const SubpastaInfo& sub) { entrarNaPasta(sub.id, sub.nome); };
        mosaico_->aoSelecionarSubpastaCard = [this](const SubpastaInfo& sub) {
            if (const auto* no = acharNo(arvoreDoMapa_, sub.id)) mostrarResumo(*no, sub.nome);
        };
        viewportGrade_.setViewedComponent(mosaico_.get(), false);
        viewportGrade_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewportGrade_);

        filtros_ = std::make_unique<FiltrosComponent>(projeto_, *mosaico_, false);
        filtros_->definirEstiloExplorer(true, true);
        coluna_ = std::make_unique<ColunaLateral>(*this);
        coluna_->addAndMakeVisible(*filtros_);
        filtros_->addComponentListener(coluna_.get());
        viewportLateral_.setViewedComponent(coluna_.get(), false);
        viewportLateral_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewportLateral_);

        // Painel da direita: o arquivo selecionado (somente leitura) ou, sem seleção, o resumo da pasta.
        painel_ = std::make_unique<PainelMetadadosArquivo>(projeto_);
        painel_->definirLayoutCompacto(true);
        painel_->mostrarItem({});
        addAndMakeVisible(*painel_);

        // Arquivo(s) selecionado(s): a MESMA ficha editável do GRID (aba METADATA) — mesmos campos, mesmas posições, mesmo
        // funcionamento (lote, presets, tags, GET EXIF, geo...). Pastas continuam só com o resumo (painel_).
        ficha_ = std::make_unique<FichaPanelComponent>(projeto_);
        ficha_->aoAplicarSucesso = [this](const std::string& itemId) { mosaico_->atualizarItemEmMemoria(itemId); };
        ficha_->aoAplicarEmLote = [this] { filtros_->recarregar(); };
        ficha_->aoAplicarGeoEmLote = [this] { filtros_->recarregar(); };
        ficha_->aoIniciarLoteAtualizacao = [this] { mosaico_->iniciarLoteAtualizacao(); };
        ficha_->aoFinalizarLoteAtualizacao = [this] { mosaico_->finalizarLoteAtualizacao(); };
        ficha_->aoPedirFocoGrade = [this] {
            juce::Component::SafePointer<MosaicoComponent> seguro(mosaico_.get());
            juce::MessageManager::callAsync([seguro] { if (seguro != nullptr) seguro->grabKeyboardFocus(); });
        };
        addChildComponent(*ficha_);

        trilha_ = std::make_unique<Trilha>(*this);
        addAndMakeVisible(*trilha_);
        seletorVisao_ = std::make_unique<SeletorVisao>(*this);
        addAndMakeVisible(*seletorVisao_);

        // `_no folder`: R (no mosaico) marca; REJECT MARKED manda os marcados com R para o _trash do mapa.
        btnRejeitar_.setButtonText(i18n::t("janela_pasta.rejeitar_marcados"));
        btnRejeitar_.setTooltip(i18n::t("janela_pasta.rejeitar_marcados_dica"));
        btnRejeitar_.onClick = [this] { rejeitarMarcados(); };
        moldura::estilizarBotao(btnRejeitar_, true);
        if (semPasta_) {
            addAndMakeVisible(btnRejeitar_);
            legendaR_ = std::make_unique<LegendaAtalhosIntake>(i18n::t("janela_pasta.rejeitar_marcados_dica"));  // "Shortcuts [R] Reject (?)", como no INTAKE
            addAndMakeVisible(*legendaR_);
        }

        btnVoltar_.setButtonText(juce::String::fromUTF8("\xe2\x80\xb9  ") + i18n::t("janela_pasta.voltar_curto"));
        btnVoltar_.onClick = [this] { voltarUmaPasta(); };
        btnVoltar_.setColour(juce::TextButton::buttonColourId, explorer::cores().cartao);
        btnVoltar_.setColour(juce::TextButton::textColourOffId, explorer::cores().texto);
        addChildComponent(btnVoltar_);

        // Renomear a pasta (Folder Map editável): duplo clique no nome atual do caminho abre este editor por cima dele.
        lblTitulo_.setFont(explorer::fonte(17.0f, true));
        lblTitulo_.setColour(juce::Label::textColourId, explorer::cores().texto);
        lblTitulo_.setColour(juce::Label::backgroundWhenEditingColourId, explorer::cores().cartao);
        if (podeRenomear()) {
            lblTitulo_.setEditable(false, true, true);
            lblTitulo_.onEditorHide = [this] { lblTitulo_.setVisible(false); };
            lblTitulo_.onTextChange = [this] {
                const auto novo = lblTitulo_.getText().trim();
                if (novo.isEmpty() || novo == nome_ || !projeto_.renomearPastaAcervo(pastaId_, novo.toStdString())) {
                    lblTitulo_.setText(nome_, juce::dontSendNotification);
                    return;
                }
                nome_ = novo;
                if (pastaId_ == raizJanelaId_) nomeRaiz_ = novo;
                aoMudarDados();
                recarregarItens();
            };
        }
        addChildComponent(lblTitulo_);

        campoBusca_.setTextToShowWhenEmpty(i18n::t("janela_pasta.busca"), explorer::cores().secundario);
        campoBusca_.setIndents(34, 0);
        campoBusca_.setJustification(juce::Justification::centredLeft);
        campoBusca_.setFont(explorer::fonte(13.5f));
        campoBusca_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        campoBusca_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        campoBusca_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        campoBusca_.setColour(juce::TextEditor::textColourId, explorer::cores().texto);
        campoBusca_.onTextChange = [this] { mosaico_->definirBusca(campoBusca_.getText().trim()); };
        addAndMakeVisible(campoBusca_);

        // Ocupa a tela sem passar dela (1920 x 1080 e 1440 x 900 sem nada cortado).
        auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                        ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea
                        : juce::Rectangle<int>(0, 0, 1440, 900);
        setSize(juce::jlimit(1000, 1680, area.getWidth() - 2 * moldura::kMargemJanelaNativa - 40),
                juce::jlimit(560, 960, area.getHeight() - 2 * moldura::kMargemJanelaNativa - moldura::kAlturaCabecalho - 40));
        recarregarItens();
    }

    ~Conteudo() override {
        if (ficha_ != nullptr) ficha_->salvarPendencias();  // texto digitado e ainda não confirmado não se perde ao fechar
        ++geracaoSemPasta_;
        poolSemPasta_.removeAllJobs(true, 5000);
        filtros_->removeComponentListener(coluna_.get());
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        const auto c = explorer::cores();
        g.fillAll(c.fundo);
        // barra de navegação
        g.setColour(c.barra);
        g.fillRect(0, 0, getWidth(), kAlturaBarraNav);
        g.setColour(c.borda);
        g.fillRect(0, kAlturaBarraNav - 1, getWidth(), 1);
        // lateral e divisórias
        g.setColour(c.lateral);
        g.fillRect(0, kAlturaBarraNav, kLarguraLateral, getHeight() - kAlturaBarraNav);
        g.setColour(c.borda);
        g.fillRect(kLarguraLateral, kAlturaBarraNav, 1, getHeight() - kAlturaBarraNav);
        g.fillRect(getWidth() - kLarguraFicha - 1, kAlturaBarraNav, 1, getHeight() - kAlturaBarraNav);
        // campo de busca: fundo em pílula com a lupa
        const auto busca = campoBusca_.getBounds().toFloat();
        g.setColour(c.cartao);
        g.fillRoundedRectangle(busca, 10.0f);
        g.setColour(c.borda);
        g.drawRoundedRectangle(busca.reduced(0.5f), 10.0f, 1.0f);
        const float lx = busca.getX() + 15.0f, ly = busca.getCentreY() - 1.0f;
        g.setColour(c.secundario);
        g.drawEllipse(lx - 5.0f, ly - 5.0f, 10.0f, 10.0f, 1.5f);
        g.drawLine(lx + 3.6f, ly + 3.6f, lx + 7.0f, ly + 7.0f, 1.6f);
        // título da grade: nome da pasta em negrito + resumo
        const auto titulo = areaTituloGrade_;
        const auto fonteNome = explorer::fonte(16.0f, true);
        const int wNome = juce::jmin(titulo.getWidth(), explorer::larguraDoTexto(fonteNome, nome_) + 2);
        g.setColour(c.texto);
        g.setFont(fonteNome);
        g.drawFittedText(nome_, titulo.withWidth(wNome), juce::Justification::centredLeft, 1, 0.85f);
        g.setColour(c.secundario);
        g.setFont(explorer::fonte(13.0f));
        g.drawFittedText(resumoGrade_, titulo.withTrimmedLeft(wNome + 12), juce::Justification::centredLeft, 2, 0.85f);
    }

    void resized() override {
        auto area = getLocalBounds();
        auto nav = area.removeFromTop(kAlturaBarraNav).reduced(18, 10);
        if (btnVoltar_.isVisible()) {
            btnVoltar_.setBounds(nav.removeFromLeft(86).reduced(0, 1));
            nav.removeFromLeft(14);
        }
        campoBusca_.setBounds(nav.removeFromRight(juce::jmin(320, juce::jmax(200, nav.getWidth() / 4))));
        nav.removeFromRight(12);
        seletorVisao_->setBounds(nav.removeFromRight(132).reduced(0, 1));
        nav.removeFromRight(12);
        if (btnRejeitar_.isVisible()) {
            btnRejeitar_.setBounds(nav.removeFromRight(210).reduced(0, 1));
            nav.removeFromRight(10);
            if (legendaR_ != nullptr) {
                legendaR_->setBounds(nav.removeFromRight(180));
                nav.removeFromRight(10);
            }
        }
        trilha_->setBounds(nav);
        if (lblTitulo_.isVisible()) lblTitulo_.setBounds(trilha_->areaDoAtual().translated(trilha_->getX(), trilha_->getY()).expanded(6, 2));

        viewportLateral_.setBounds(area.removeFromLeft(kLarguraLateral));
        area.removeFromLeft(1);
        const auto areaDireita = area.removeFromRight(kLarguraFicha).withTrimmedLeft(1);
        painel_->setBounds(areaDireita);
        ficha_->setBounds(areaDireita);
        area.removeFromRight(1);
        areaTituloGrade_ = area.removeFromTop(kAlturaTituloGrade).reduced(kMargemGrade + 4, 0).withTrimmedTop(6);
        viewportGrade_.setBounds(area.reduced(kMargemGrade - 4, 0).withTrimmedBottom(8));
        mosaico_->setSize(viewportGrade_.getMaximumVisibleWidth(), juce::jmax(viewportGrade_.getHeight(), mosaico_->getHeight()));
        coluna_->setSize(viewportLateral_.getMaximumVisibleWidth(), coluna_->alturaNecessaria());
        if (overlay_ != nullptr) overlay_->setBounds(getLocalBounds());
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey && !naRaiz()) { voltarUmaPasta(); return true; }
        return false;
    }

private:
    // ── coluna da esquerda: árvore de pastas (FOLDER / FOLDERS) + filtros no estilo explorer ─────────────────────────
    class ColunaLateral : public juce::Component, public juce::ComponentListener {
    public:
        explicit ColunaLateral(Conteudo& dono) : dono_(dono) {}

        void definirArvore(const ProjetoAberto::NoArvore* no, const std::string& atualId) {
            raiz_ = no;
            atualId_ = atualId;
            linhas_.clear();
            if (raiz_ != nullptr) achatar(*raiz_, 0);
            if (auto* p = getParentComponent()) setSize(p->getWidth(), alturaNecessaria());
            resized();
            repaint();
        }
        // Abre as pastas acima de `id` (a árvore inteira fica visível, como no Finder: entrar numa pasta não esconde as de cima).
        void revelar(const ProjetoAberto::NoArvore& raiz, const std::string& id) {
            std::vector<const ProjetoAberto::NoArvore*> caminho;
            if (!caminhoAte(raiz, id, caminho)) return;
            for (size_t i = 0; i + 1 < caminho.size(); ++i) {
                abertos_.insert(caminho[i]->id);
                fechados_.erase(caminho[i]->id);
            }
        }
        int alturaArvore() const { return raiz_ == nullptr ? 0 : 34 + static_cast<int>(linhas_.size()) * kAlturaLinhaArvore + 6; }
        int alturaNecessaria() const { return alturaArvore() + dono_.filtros_->getHeight() + 12; }

        void resized() override {
            dono_.filtros_->setBounds(0, alturaArvore(), getWidth(), dono_.filtros_->getHeight());
            dono_.filtros_->setSize(getWidth(), dono_.filtros_->getHeight());
        }
        void componentMovedOrResized(juce::Component&, bool, bool foiRedimensionado) override {
            if (!foiRedimensionado) return;
            const int h = alturaNecessaria();
            if (getHeight() != h) setSize(getWidth(), h);
        }

        void paint(juce::Graphics& g) override {
            const auto c = explorer::cores();
            g.fillAll(c.lateral);
            if (raiz_ == nullptr) return;
            const bool pt = i18n::localeAtivo().startsWith("pt");
            g.setColour(c.secundario);
            g.setFont(explorer::fonteSecao());
            g.drawText(moldura::caixaAlta(i18n::t(raiz_->filhos.empty() ? "janela_pasta.secao_pasta" : "janela_pasta.secao_pastas")),
                       juce::Rectangle<int>(16, 0, getWidth() - 32, 30), juce::Justification::bottomLeft, false);
            for (size_t i = 0; i < linhas_.size(); ++i) {
                const auto& l = linhas_[i];
                auto r = boundsDaLinha(static_cast<int>(i));
                const bool atual = l.no->id == atualId_;
                const bool filtrada = !atual && l.no->id == dono_.filtroArvoreId_;
                if (atual || filtrada) {
                    g.setColour(atual ? c.grafite : c.grafite.withAlpha(0.14f));
                    g.fillRoundedRectangle(r.toFloat().reduced(8.0f, 2.0f), 6.0f);
                }
                auto miolo = r.withTrimmedLeft(12 + l.profundidade * kRecuoArvore).withTrimmedRight(16);
                const auto seta = miolo.removeFromLeft(16);
                if (!l.no->filhos.empty()) {
                    juce::Path p;
                    const float cx = static_cast<float>(seta.getCentreX()), cy = static_cast<float>(seta.getCentreY());
                    if (l.aberto) { p.startNewSubPath(cx - 4.0f, cy - 2.0f); p.lineTo(cx, cy + 2.5f); p.lineTo(cx + 4.0f, cy - 2.0f); }
                    else { p.startNewSubPath(cx - 2.0f, cy - 4.0f); p.lineTo(cx + 2.5f, cy); p.lineTo(cx - 2.0f, cy + 4.0f); }
                    g.setColour(atual ? c.sobreGrafite : c.secundario);
                    g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                miolo.removeFromLeft(2);
                std::set<std::string> ids;
                coletarIdsRecursivos(*l.no, ids);
                const auto contagem = explorer::comMilhares(static_cast<int>(ids.size()), pt);
                const auto fonteContagem = explorer::fonte(12.5f);
                const auto areaContagem = miolo.removeFromRight(explorer::larguraDoTexto(fonteContagem, contagem) + 2);
                miolo.removeFromRight(8);
                g.setColour(atual ? c.sobreGrafite.withAlpha(0.85f) : c.secundario);
                g.setFont(fonteContagem);
                g.drawText(contagem, areaContagem, juce::Justification::centredRight, false);
                g.setColour(atual ? c.sobreGrafite : c.texto);
                g.setFont(explorer::fonte(13.5f, atual));
                g.drawFittedText(l.no->nome, miolo, juce::Justification::centredLeft, 1, 0.75f);
            }
        }

        void mouseDown(const juce::MouseEvent& e) override {
            const int i = indiceNaPosicao(e.y);
            if (i < 0) return;
            const auto& l = linhas_[static_cast<size_t>(i)];
            const int xSeta = 12 + l.profundidade * kRecuoArvore;
            if (!l.no->filhos.empty() && e.x >= xSeta && e.x < xSeta + 18) {  // seta: abre/fecha
                (l.aberto ? fechados_ : abertos_).insert(l.no->id);
                (l.aberto ? abertos_ : fechados_).erase(l.no->id);
                definirArvore(raiz_, atualId_);
                return;
            }
            const auto id = l.no->id;
            const auto nome = l.no->nome;
            // Container: a grade só carrega a pasta atual — clicar numa pasta entra nela (como a barra lateral do Finder).
            // Pasta do Folder Map: clicar filtra a grade por aquela subpasta (como antes); duplo clique entra.
            juce::Component::SafePointer<Conteudo> seguro(&dono_);
            if (!dono_.nestId_.empty()) {
                if (id != atualId_) juce::MessageManager::callAsync([seguro, id, nome] { if (seguro) seguro->irParaPasta(id, nome); });
            } else {
                std::set<std::string> ids;
                coletarIdsRecursivos(*l.no, ids);
                dono_.filtrarPelaArvore(id == atualId_ ? std::string() : id, std::move(ids));
                repaint();
            }
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override {
            const int i = indiceNaPosicao(e.y);
            if (i < 0 || !dono_.nestId_.empty()) return;
            const auto id = linhas_[static_cast<size_t>(i)].no->id;
            const auto nome = linhas_[static_cast<size_t>(i)].no->nome;
            juce::Component::SafePointer<Conteudo> seguro(&dono_);
            if (id != atualId_) juce::MessageManager::callAsync([seguro, id, nome] { if (seguro) seguro->irParaPasta(id, nome); });
        }

    private:
        struct Linha { const ProjetoAberto::NoArvore* no; int profundidade; bool aberto; };
        void achatar(const ProjetoAberto::NoArvore& no, int profundidade) {
            const bool aberto = !fechados_.count(no.id) && (profundidade < 3 || abertos_.count(no.id) > 0);
            linhas_.push_back({&no, profundidade, aberto && !no.filhos.empty()});
            if (aberto)
                for (const auto& f : no.filhos) achatar(f, profundidade + 1);
        }
        juce::Rectangle<int> boundsDaLinha(int i) const { return {0, 34 + i * kAlturaLinhaArvore, getWidth(), kAlturaLinhaArvore}; }
        int indiceNaPosicao(int y) const {
            if (y < 34) return -1;
            const int i = (y - 34) / kAlturaLinhaArvore;
            return i < static_cast<int>(linhas_.size()) ? i : -1;
        }

        Conteudo& dono_;
        const ProjetoAberto::NoArvore* raiz_ = nullptr;
        std::string atualId_;
        std::vector<Linha> linhas_;
        std::set<std::string> abertos_, fechados_;
    };

    // ── caminho clicável: Acervo › Site › IMAGES › PEDRA DE GUARATIBA ──────────────────────────────────────────────
    class Trilha : public juce::Component {
    public:
        explicit Trilha(Conteudo& dono) : dono_(dono) {}
        void definir(std::vector<std::pair<std::string, juce::String>> partes) {
            partes_ = std::move(partes);
            repaint();
        }
        juce::Rectangle<int> areaDoAtual() const { return areaAtual_; }

        void paint(juce::Graphics& g) override {
            const auto c = explorer::cores();
            areas_.clear();
            const auto fonteAnterior = explorer::fonte(14.0f);
            const auto fonteAtual = explorer::fonte(17.0f, true);
            const auto fonteSeta = explorer::fonte(14.0f);
            // Larguras inteiras; se não couber, as pastas anteriores do começo saem do caminho (a atual nunca é cortada).
            size_t primeira = 0;
            auto larguraTotal = [&](size_t de) {
                int w = 0;
                for (size_t i = de; i < partes_.size(); ++i) {
                    const bool atual = i + 1 == partes_.size();
                    w += explorer::larguraDoTexto(atual ? fonteAtual : fonteAnterior, partes_[i].second) + 4;
                    if (!atual) w += explorer::larguraDoTexto(fonteSeta, kSeta) + 2;
                }
                return w;
            };
            while (primeira + 1 < partes_.size() && larguraTotal(primeira) > getWidth()) ++primeira;
            int x = 0;
            for (size_t i = primeira; i < partes_.size(); ++i) {
                const bool atual = i + 1 == partes_.size();
                const auto& f = atual ? fonteAtual : fonteAnterior;
                const int w = juce::jmin(getWidth() - x, explorer::larguraDoTexto(f, partes_[i].second) + 4);
                const juce::Rectangle<int> r(x, 0, w, getHeight());
                g.setColour(atual ? c.texto : c.secundario);
                g.setFont(f);
                g.drawFittedText(partes_[i].second, r, juce::Justification::centredLeft, 1, 0.7f);
                if (atual) areaAtual_ = r;
                else areas_.push_back({r, i});
                x += w;
                if (!atual) {
                    const int ws = explorer::larguraDoTexto(fonteSeta, kSeta) + 2;
                    g.setColour(c.secundario.withAlpha(0.7f));
                    g.setFont(fonteSeta);
                    g.drawText(kSeta, juce::Rectangle<int>(x, 0, ws, getHeight()), juce::Justification::centred, false);
                    x += ws;
                }
            }
        }
        void mouseMove(const juce::MouseEvent& e) override {
            bool sobre = false;
            for (const auto& [r, i] : areas_) sobre = sobre || r.contains(e.getPosition());
            setMouseCursor(sobre ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            for (const auto& [r, i] : areas_)
                if (r.contains(e.getPosition())) {
                    const auto parte = partes_[i];
                    juce::Component::SafePointer<Conteudo> seguro(&dono_);
                    juce::MessageManager::callAsync([seguro, parte] { if (seguro) seguro->irParaPasta(parte.first, parte.second); });
                    return;
                }
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override {
            if (areaAtual_.contains(e.getPosition())) dono_.comecarRenomear();
        }

    private:
        Conteudo& dono_;
        std::vector<std::pair<std::string, juce::String>> partes_;
        std::vector<std::pair<juce::Rectangle<int>, size_t>> areas_;
        juce::Rectangle<int> areaAtual_;
    };

    // ── Thumbs | List em pílula (a opção ativa em grafite) ─────────────────────────────────────────────────────────
    class SeletorVisao : public juce::Component {
    public:
        explicit SeletorVisao(Conteudo& dono) : dono_(dono) {}
        void paint(juce::Graphics& g) override {
            const auto c = explorer::cores();
            const auto r = getLocalBounds().toFloat();
            g.setColour(c.cartao);
            g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
            g.setColour(c.borda);
            g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
            const bool grade = dono_.mosaico_->modoVisaoAtual() == MosaicoComponent::ModoVisao::Grade;
            auto metade = r.reduced(3.0f);
            const auto esquerda = metade.removeFromLeft(metade.getWidth() * 0.5f);
            desenharPilula(g, esquerda, grade, i18n::t("janela_pasta.view_grade"), false);
            desenharPilula(g, metade, !grade, i18n::t("janela_pasta.view_lista"), false);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            dono_.definirVisao(e.x < getWidth() / 2 ? MosaicoComponent::ModoVisao::Grade : MosaicoComponent::ModoVisao::Lista);
            repaint();
        }
    private:
        Conteudo& dono_;
    };

    bool podeRenomear() const {
        return nestId_.empty() && !semPasta_ && !projeto_.somenteLeitura() && !projeto_.mapaSelecionadoNoFolderMap().empty() &&
               projeto_.mapaSelecionadoNoFolderMap() != ProjetoAberto::kMapaOriginal;
    }

    void comecarRenomear() {
        if (!podeRenomear()) return;
        lblTitulo_.setText(nome_, juce::dontSendNotification);
        lblTitulo_.setVisible(true);
        resized();
        lblTitulo_.showEditor();
    }

    bool naRaiz() const { return semPasta_ || pastaId_ == raizJanelaId_ || caminho_.size() <= 1; }

    // Todos os arquivos da pasta e das subpastas — do mapa selecionado no Folder Map (o ORIGINAL é virtual: os ids vêm da
    // árvore dele, não de acervo_item_pasta).
    void recarregarItens() {
        ids_.clear();
        filtroArvoreId_.clear();
        if (!nestId_.empty()) { recarregarContainer(); return; }
        if (semPasta_) { recarregarSemPasta(); return; }
        const ProjetoAberto::NoArvore* noRaiz = nullptr;
        try {
            std::string mapa = projeto_.mapaSelecionadoNoFolderMap();
            if (mapa.empty()) mapa = projeto_.mapaAtivoPadrao();
            arvoreDoMapa_ = projeto_.arvoreAcervo(mapa.empty() ? ProjetoAberto::kMapaOriginal : mapa);
            noRaiz = acharNo(arvoreDoMapa_, pastaId_);
        } catch (...) {}
        if (noRaiz != nullptr) {
            coletarIdsRecursivos(*noRaiz, ids_);
        } else {
            ProjetoAberto::NoArvore avulsa;
            avulsa.id = pastaId_;
            avulsa.nome = nome_;
            arvoreDoMapa_ = avulsa;
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
            arvoreDoMapa_.itemIds = ids_;
        }
        atualizarNavegacao();
        const auto* atual = acharNo(arvoreDoMapa_, pastaId_);
        coluna_->definirArvore(atual, pastaId_);
        filtros_->definirEstiloExplorer(true, naRaiz());
        mosaico_->definirSubpastas({});
        carregarGrade(ids_);
    }

    // `_no folder`: os ids (itensSemPasta, a mesma regra do card NO FOLDER) e quantos deles estão no INTAKE ou com CONTENT =
    // Hidden — os dois casos que a grade da aba METADATA não mostra — são lidos fora da message thread.
    void recarregarSemPasta() {
        std::string mapa = projeto_.mapaSelecionadoNoFolderMap();
        if (mapa.empty()) mapa = projeto_.mapaAtivoPadrao();
        const int geracao = ++geracaoSemPasta_;
        juce::Component::SafePointer<Conteudo> seguro(this);
        ProjetoAberto* proj = &projeto_;
        poolSemPasta_.addJob([seguro, proj, mapa, geracao] {
            std::set<std::string> ids;
            int noIntake = 0, ocultos = 0;
            try {
                ids = proj->itensSemPasta(mapa);
                auto st = proj->projeto().registroLeitura().prepare(
                    "SELECT COALESCE(SUM(COALESCE(em_quarentena, 0) = 1), 0), "
                    "COALESCE(SUM(COALESCE(em_quarentena, 0) = 0 AND COALESCE(collection_type, '') = 'Hidden'), 0) "
                    "FROM item WHERE projeto_id = ? AND COALESCE(estado, '') != 'duplicata' AND id NOT IN "
                    "(SELECT aip.item_id FROM acervo_item_pasta aip WHERE aip.mapa_id = ?)");
                st.bind(1, matriz::db::Value::of(proj->projeto().projetoId()));
                st.bind(2, matriz::db::Value::of(mapa));
                if (st.step()) { noIntake = st.columnInt(0); ocultos = st.columnInt(1); }
            } catch (...) {}
            juce::MessageManager::callAsync([seguro, geracao, ids = std::move(ids), noIntake, ocultos]() mutable {
                if (seguro == nullptr || geracao != seguro->geracaoSemPasta_.load()) return;
                seguro->ids_ = std::move(ids);
                seguro->semPastaNoIntake_ = noIntake;
                seguro->semPastaOcultos_ = ocultos;
                ProjetoAberto::NoArvore raiz;
                raiz.id = kPastaSemPasta;
                raiz.nome = seguro->nome_;
                raiz.itemIds = seguro->ids_;
                seguro->arvoreDoMapa_ = std::move(raiz);
                seguro->atualizarNavegacao();
                seguro->coluna_->definirArvore(&seguro->arvoreDoMapa_, seguro->pastaId_);
                seguro->filtros_->definirEstiloExplorer(true, false);
                seguro->carregarGrade(seguro->ids_);
            });
        });
    }

    // CONTAINER: as pastas de origem dos membros, como no Finder — a grade mostra as subpastas (cards) e os arquivos que estão
    // direto na pasta atual; duplo clique entra, Back/ESC sobe. A árvore é lida fora da message thread.
    void recarregarContainer() {
        const int geracao = ++geracaoSemPasta_;
        juce::Component::SafePointer<Conteudo> seguro(this);
        ProjetoAberto* proj = &projeto_;
        poolSemPasta_.addJob([seguro, proj, nest = nestId_, geracao] {
            ProjetoAberto::NoArvore arvore;
            bool noIntake = false;  // container ainda no INTAKE: os arquivos estão em quarentena (a grade normal não os lista)
            try {
                arvore = proj->arvoreDoContainer(nest);
                auto st = proj->projeto().registroLeitura().prepare(
                    "SELECT COUNT(*) FROM nest_item ni JOIN item i ON i.id = ni.item_id WHERE ni.nest_id = ? AND COALESCE(i.em_quarentena, 0) = 1");
                st.bind(1, matriz::db::Value::of(nest));
                noIntake = st.step() && st.columnInt(0) > 0;
            } catch (...) { arvore.id = nest; }
            juce::MessageManager::callAsync([seguro, geracao, noIntake, arvore = std::move(arvore)]() mutable {
                if (seguro == nullptr || geracao != seguro->geracaoSemPasta_.load()) return;
                seguro->mosaico_->definirModoQuarentena(noIntake);
                seguro->arvoreDoMapa_ = std::move(arvore);
                seguro->arvoreDoMapa_.nome = seguro->nomeRaiz_;  // a raiz não tem nome no disco: vale o do container
                seguro->mostrarPastaDoContainer();
            });
        });
    }

    void mostrarPastaDoContainer() {
        const auto* atual = acharNo(arvoreDoMapa_, pastaId_);
        if (atual == nullptr) { pastaId_ = arvoreDoMapa_.id; atual = &arvoreDoMapa_; }
        nome_ = atual->nome;
        std::vector<SubpastaInfo> subpastas;
        for (const auto& f : atual->filhos) {
            SubpastaInfo s;
            s.id = f.id;
            s.nome = f.nome;
            coletarIdsRecursivos(f, s.itemIds);
            s.quantidade = static_cast<int>(s.itemIds.size());
            s.subpastas = static_cast<int>(f.filhos.size());
            subpastas.push_back(std::move(s));
        }
        // Sem subpastas a grade mostra todos os arquivos dela; com elas, só os que estão direto nesta pasta (as pastas viram cartões).
        ids_ = atual->filhos.empty() ? atual->itemIds : atual->itemIdsDiretos;
        atualizarNavegacao();
        mosaico_->definirSubpastas(std::move(subpastas));
        coluna_->revelar(arvoreDoMapa_, pastaId_);
        coluna_->definirArvore(&arvoreDoMapa_, pastaId_);  // a árvore inteira do container, a pasta atual destacada
        filtros_->definirEstiloExplorer(!ids_.empty(), naRaiz());
        std::set<std::string> todos;
        coletarIdsRecursivos(arvoreDoMapa_, todos);
        carregarGrade(todos);  // o container inteiro fica carregado: navegar entre as pastas não relê o banco
    }

    // REJECT MARKED (`_no folder`): os arquivos desta pasta marcados com R vão para o _trash do mapa (de lá, RESTORE os traz de
    // volta) e perdem a marca R. As marcas são lidas da memória (sem banco).
    void rejeitarMarcados() {
        std::vector<std::string> alvo;
        for (const auto& id : ids_)
            if (projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Rejeitado, id)) alvo.push_back(id);
        if (alvo.empty()) {
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::InfoIcon)
                                             .withTitle(i18n::t("janela_pasta.rejeitar_marcados"))
                                             .withMessage(i18n::t("janela_pasta.rejeitar_nenhum"))
                                             .withButton(i18n::t("dialogo.ok")),
                                         juce::ModalCallbackFunction::create([](int) {}));
            return;
        }
        if (projeto_.somenteLeitura()) { ProjetoAberto::avisarSomenteLeitura(); return; }
        std::string mapa = projeto_.mapaSelecionadoNoFolderMap();
        if (mapa.empty()) mapa = projeto_.mapaAtivoPadrao();
        // WINPORT: Nada de I/O na message thread — a mesma gravação síncrona do "Move to Trash" dos arquivos soltos no Folder Map.
        try {
            projeto_.moverItensParaTrash(alvo, mapa);
        } catch (...) {
            ProjetoAberto::avisarMapaTravado(i18n::t("janela_pasta.rejeitar_falha"));
            return;
        }
        projeto_.definirMarcacao(ProjetoAberto::TipoMarcacao::Rejeitado, alvo, false);
        recarregarItens();
        if (aoMudouDados_) aoMudouDados_();
    }

    // O snapshot da grade roda em background (como no resto do app); os filtros recontam quando ele termina.
    // `escopo` = o que fica carregado (no container, ele inteiro); a grade mostra ids_.
    void carregarGrade(const std::set<std::string>& escopo) {
        aguardandoCarga_ = true;
        const bool reler = !escopoCarregado_.has_value() || *escopoCarregado_ != escopo;
        if (reler) {
            escopoCarregado_ = escopo;
            mosaico_->definirEscopoCarga(escopo);  // só os arquivos desta janela, não o catálogo inteiro (19 mil itens = ~8 s de grade vazia)
        }
        mosaico_->limparSelecao();
        mosaico_->definirFiltroItens(ids_);
        filtros_->definirEscopo(ids_);
        if (reler) {
            mosaico_->recarregar();
        } else {
            aguardandoCarga_ = false;
            filtros_->recarregar();
        }
        viewportGrade_.setViewPosition(0, 0);
        atualizarContagem();
        atualizarPainelSemSelecao();
        resized();
    }

    // Caminho da raiz da janela até a pasta atual: o caminho clicável e o Back.
    void atualizarNavegacao() {
        caminho_.clear();
        std::vector<const ProjetoAberto::NoArvore*> nos;
        const auto* raizJanela = acharNo(arvoreDoMapa_, raizJanelaId_);
        if (raizJanela == nullptr) raizJanela = &arvoreDoMapa_;
        if (caminhoAte(*raizJanela, pastaId_, nos)) {
            for (size_t i = 0; i < nos.size(); ++i)
                caminho_.push_back({nos[i]->id, i == 0 ? nomeRaiz_ : nos[i]->nome});
        }
        if (caminho_.empty()) caminho_.push_back({pastaId_, nome_});
        nome_ = caminho_.back().second;
        trilha_->definir(caminho_);
        btnVoltar_.setVisible(!naRaiz());
        resized();
    }

    void irParaPasta(const std::string& id, const juce::String& nome) {
        if (id.empty() || id == pastaId_) return;
        pastaId_ = id;
        nome_ = nome;
        if (!nestId_.empty() && arvoreDoMapa_.id == nestId_) {  // container: a árvore já está carregada, só muda a pasta
            filtroArvoreId_.clear();
            mostrarPastaDoContainer();
            return;
        }
        recarregarItens();
    }

    void entrarNaPasta(const std::string& id, const juce::String& nome) { irParaPasta(id, nome); }

    void voltarUmaPasta() {
        if (caminho_.size() < 2) return;
        const auto anterior = caminho_[caminho_.size() - 2];
        irParaPasta(anterior.first, anterior.second);
    }

    // Pasta do Folder Map: clicar numa subpasta da lateral filtra a grade por ela (de novo na mesma = tira o filtro).
    void filtrarPelaArvore(const std::string& id, std::set<std::string> ids) {
        filtroArvoreId_ = (id == filtroArvoreId_) ? std::string() : id;
        mosaico_->definirFiltroItens(filtroArvoreId_.empty() ? ids_ : ids);
        atualizarPainelSemSelecao();
    }

    // ── painel da direita ───────────────────────────────────────────────────────────────────────────────────────
    void definirVisao(MosaicoComponent::ModoVisao modo) {
        mosaico_->definirModoVisao(modo);
        seletorVisao_->repaint();
        resized();
    }

    void aoMudarSelecao() {
        const auto sel = mosaico_->itensSelecionados();
        if (sel.empty()) {
            if (ficha_->isVisible()) ficha_->salvarPendencias();
            ficha_->setVisible(false);
            painel_->setVisible(true);
            atualizarPainelSemSelecao();
        } else {
            // Pasta do Folder Map: a grade mostra só a capa de um NEST/CONTAINER — editar vale para todos os arquivos dele (como
            // no GRID). Na janela do container cada arquivo é ele mesmo.
            std::vector<std::string> alvo(sel.begin(), sel.end());
            if (nestId_.empty()) alvo = projeto_.expandirMembrosDeNest(alvo);
            ficha_->mostrarSelecao(alvo);
            painel_->setVisible(false);
            ficha_->setVisible(true);
        }
        atualizarContagem();
        repaint(areaTituloGrade_);
    }

    // Nada selecionado: o resumo da pasta atual (ou do cartão de pasta selecionado na grade).
    void atualizarPainelSemSelecao() {
        if (painel_ == nullptr || !mosaico_->itensSelecionados().empty()) return;  // o mosaico já avisa durante a construção
        if (const auto* sub = mosaico_->subpastaSelecionada())
            if (const auto* no = acharNo(arvoreDoMapa_, sub->id)) { mostrarResumo(*no, sub->nome); return; }
        if (const auto* atual = acharNo(arvoreDoMapa_, pastaId_)) mostrarResumo(*atual, nome_);
        else { ProjetoAberto::NoArvore avulsa; avulsa.id = pastaId_; avulsa.itemIds = ids_; mostrarResumo(avulsa, nome_); }
    }

    // Arquivos, tamanho, tipos e datas saem da grade já carregada (nada de banco aqui).
    void mostrarResumo(const ProjetoAberto::NoArvore& no, const juce::String& nome) {
        PainelMetadadosArquivo::ResumoPasta r;
        r.nome = nome;
        std::vector<const ProjetoAberto::NoArvore*> nos;
        const auto* raizJanela = acharNo(arvoreDoMapa_, raizJanelaId_);
        if (raizJanela == nullptr) raizJanela = &arvoreDoMapa_;
        if (caminhoAte(*raizJanela, no.id, nos) && nos.size() > 1) {
            juce::StringArray partes;
            for (size_t i = 0; i < nos.size(); ++i) partes.add(i == 0 ? nomeRaiz_ : nos[i]->nome);
            r.caminho = partes.joinIntoString(kSeta);
        }
        std::set<std::string> ids;
        coletarIdsRecursivos(no, ids);
        if (ids.empty() && no.id == pastaId_) ids = ids_;
        r.arquivos = static_cast<int>(ids.size());
        r.subpastas = contarSubpastas(no);
        juce::StringArray volumes;
        for (const auto& item : mosaico_->todosItensEmMemoria()) {
            if (ids.count(item.id) == 0) continue;
            r.bytes += item.tamanhoBytes;
            switch (explorer::tipoDaExtensao(juce::String(item.extensaoArquivo))) {
                case explorer::Tipo::Video: ++r.video; break;
                case explorer::Tipo::Audio: ++r.audio; break;
                case explorer::Tipo::Foto: ++r.foto; break;
                case explorer::Tipo::Documento: ++r.documento; break;
            }
            auto data = juce::String::fromUTF8((item.dataCriacao.empty() ? item.criadoEm : item.dataCriacao).c_str()).substring(0, 10);
            data = data.replaceCharacter(':', '-');
            if (data.length() == 10 && data.substring(0, 4).containsOnly("0123456789")) {
                if (r.maisAntigo.isEmpty() || data < r.maisAntigo) r.maisAntigo = data;
                if (r.maisRecente.isEmpty() || data > r.maisRecente) r.maisRecente = data;
            }
            // Vault: o volume de onde os arquivos vieram (/Volumes/<nome>/... no macOS, a unidade no Windows).
            const auto origem = juce::String::fromUTF8(item.caminhoAbsolutoOrigem.c_str());
            juce::String volume;
            if (origem.startsWith("/Volumes/")) volume = origem.substring(9).upToFirstOccurrenceOf("/", false, false);
            else if (origem.length() > 2 && origem[1] == ':') volume = origem.substring(0, 2).toUpperCase();
            if (volume.isNotEmpty()) volumes.addIfNotAlreadyThere(volume);
        }
        r.vault = volumes.joinIntoString(", ");
        painel_->mostrarResumoPasta(r);
    }

    void atualizarContagem() {
        const int sel = static_cast<int>(mosaico_->itensSelecionados().size());
        juce::StringArray partes;
        const auto* atual = acharNo(arvoreDoMapa_, pastaId_);
        const int nPastas = (atual != nullptr && !nestId_.empty()) ? static_cast<int>(atual->filhos.size()) : 0;
        if (nPastas > 0) {
            std::set<std::string> todos;
            coletarIdsRecursivos(*atual, todos);
            partes.add(contagemI18n("janela_pasta.n_pastas", nPastas));
            partes.add(contagemI18n("janela_pasta.n_arquivos", static_cast<int>(todos.size())));
        } else {
            int foto = 0, video = 0, audio = 0, documento = 0;
            for (const auto& item : mosaico_->todosItensEmMemoria()) {
                if (ids_.count(item.id) == 0) continue;
                switch (explorer::tipoDaExtensao(juce::String(item.extensaoArquivo))) {
                    case explorer::Tipo::Video: ++video; break;
                    case explorer::Tipo::Audio: ++audio; break;
                    case explorer::Tipo::Foto: ++foto; break;
                    case explorer::Tipo::Documento: ++documento; break;
                }
            }
            partes.add(contagemI18n("janela_pasta.n_arquivos", static_cast<int>(ids_.size())));
            if (foto > 0) partes.add(contagemI18n("janela_pasta.n_fotos", foto));
            if (video > 0) partes.add(contagemI18n("janela_pasta.n_videos", video));
            if (audio > 0) partes.add(contagemI18n("janela_pasta.n_audios", audio));
            if (documento > 0) partes.add(contagemI18n("janela_pasta.n_documentos", documento));
        }
        if (sel > 0) partes.add(i18n::t("janela_pasta.selecionados").replace("{n}", juce::String(sel)));
        if (semPasta_ && semPastaOcultos_ > 0) partes.add(i18n::t("janela_pasta.sem_pasta_ocultos").replace("{n}", juce::String(semPastaOcultos_)));
        if (semPasta_ && semPastaNoIntake_ > 0) partes.add(i18n::t("janela_pasta.sem_pasta_intake").replace("{n}", juce::String(semPastaNoIntake_)));
        resumoGrade_ = partes.joinIntoString(kPonto);
        repaint(areaTituloGrade_);
    }

    void aoMudarDados() {
        mosaico_->recarregar();
        filtros_->recarregar();
        if (aoMudouDados_) aoMudouDados_();
    }

    acoes::Ganchos ganchos() {
        acoes::Ganchos g;
        juce::Component::SafePointer<Conteudo> seguro(this);
        g.aoMudarDados = [seguro] {
            if (!seguro) return;
            seguro->escopoCarregado_.reset();  // relê a grade: algo mudou no banco
            seguro->recarregarItens();
            if (seguro->aoMudouDados_) seguro->aoMudouDados_();
        };
        g.aoAbrirDetalhes = [] {};
        g.aoFiltrarItens = [seguro](std::set<std::string> ids) { if (seguro) seguro->mosaico_->definirFiltroItens(std::move(ids)); };
        g.itemEmMemoria = [seguro](const std::string& id) -> const ItemResumo* { return seguro ? seguro->mosaico_->itemEmMemoria(id) : nullptr; };
        g.aoEditarQuestionario = [seguro](const std::string& nestId, const std::string& origem) { if (seguro) seguro->editarProjeto(nestId, origem); };
        g.membrosIndividuais = !nestId_.empty();  // janela do container: cada arquivo é ele mesmo
        return g;
    }

    // Botão direito: o menu padrão do app + EXTRACT (tira da pasta) + MOVE TO FOLDER.
    void abrirMenu(std::vector<std::string> ids) {
        if (ids.empty()) return;
        auto menu = acoes::construirMenu(projeto_, ids, ganchos().itemEmMemoria, true);
        menu.addSeparator();
        // `_no folder` e container: não há pasta do Folder Map de onde extrair.
        if (!semPasta_ && nestId_.empty()) menu.addItem(kMenuExtrairDaPasta, i18n::t("janela_pasta.extrair"));
        // Container: tira os arquivos dele (voltam ao grid como itens comuns; com Undo). Container com 1 arquivo se desfaz.
        if (!nestId_.empty()) menu.addItem(kMenuExtrairDoContainer, i18n::t("janela_pasta.extrair_container"), !projeto_.somenteLeitura());
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
                proj->removerItensDasPastas(std::vector<std::string>(ids.begin(), ids.end()), pastas);  // um passo de Undo
                seguro->escopoCarregado_.reset();
                seguro->recarregarItens();
                if (seguro->aoMudouDados_) seguro->aoMudouDados_();
            } else if (r == kMenuExtrairDoContainer) {
                proj->extrairDoNest(std::vector<std::string>(ids.begin(), ids.end()));  // um passo de Undo
                seguro->escopoCarregado_.reset();
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
                seguro->escopoCarregado_.reset();
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
        // Container: a coluna da esquerda do preview lista só os arquivos da mesma pasta do arquivo aberto.
        std::optional<std::set<std::string>> mesmaPasta;
        if (!nestId_.empty()) {
            std::function<const ProjetoAberto::NoArvore*(const ProjetoAberto::NoArvore&)> pastaDe =
                [&](const ProjetoAberto::NoArvore& no) -> const ProjetoAberto::NoArvore* {
                if (no.itemIdsDiretos.count(itemId) > 0) return &no;
                for (const auto& f : no.filhos)
                    if (const auto* achou = pastaDe(f)) return achou;
                return nullptr;
            };
            if (const auto* pasta = pastaDe(arvoreDoMapa_)) mesmaPasta = pasta->itemIdsDiretos;
        }
        janelaPreview_ = std::make_unique<FloatingPreviewWindow>(projeto_, itemId, aoFechar, aoNavegar, aoItemMudou, false, std::move(mesmaPasta));
    }

    LookAndFeelExplorer laf_;  // primeiro membro: vive mais que os componentes que o usam
    ProjetoAberto& projeto_;
    std::string pastaId_;
    std::string raizJanelaId_;  // pasta (ou container) que a janela abriu: a raiz do caminho
    juce::String nome_, nomeRaiz_;
    std::function<void()> aoMudouDados_;
    std::set<std::string> ids_;
    std::optional<std::set<std::string>> escopoCarregado_;
    ProjetoAberto::NoArvore arvoreDoMapa_;
    std::vector<std::pair<std::string, juce::String>> caminho_;  // (id, nome) da raiz da janela até a pasta atual
    std::string filtroArvoreId_;  // pasta do Folder Map: subpasta da lateral que filtra a grade
    std::unique_ptr<MosaicoComponent> mosaico_;
    std::unique_ptr<FiltrosComponent> filtros_;
    std::unique_ptr<ColunaLateral> coluna_;
    std::unique_ptr<PainelMetadadosArquivo> painel_;  // resumo da pasta (nada selecionado / cartão de pasta)
    std::unique_ptr<FichaPanelComponent> ficha_;       // arquivo(s) selecionado(s): a ficha editável do GRID
    std::unique_ptr<Trilha> trilha_;
    std::unique_ptr<SeletorVisao> seletorVisao_;
    std::unique_ptr<PainelOverlay> overlay_;
    juce::Viewport viewportGrade_, viewportLateral_;
    juce::TextButton btnVoltar_, btnRejeitar_;
    std::unique_ptr<LegendaAtalhosIntake> legendaR_;  // só no `_no folder`
    std::string nestId_;  // não vazio = janela de um CONTAINER (só os membros dele)
    bool semPasta_ = false;        // pasta virtual `_no folder` (pastaId == kPastaSemPasta)
    bool selecionarTudo_ = false;  // seleciona tudo quando a grade terminar de carregar
    int semPastaNoIntake_ = 0, semPastaOcultos_ = 0;
    std::atomic<int> geracaoSemPasta_{0};
    juce::ThreadPool poolSemPasta_{1};
    bool aguardandoCarga_ = false;
    juce::Label lblTitulo_;
    juce::TextEditor campoBusca_;
    juce::Rectangle<int> areaTituloGrade_;
    juce::String resumoGrade_;
    std::unique_ptr<FloatingPreviewWindow> janelaPreview_;
};

JanelaPastaFlutuante::JanelaPastaFlutuante(ProjetoAberto& projeto, const std::string& pastaId, const juce::String& nomePasta,
                                           std::function<void()> aoFechar, std::function<void()> aoMudouDados, const std::string& nestId,
                                           bool selecionarTudo)
    : juce::DocumentWindow("FOLDER: " + nomePasta, juce::Colours::transparentBlack, juce::DocumentWindow::closeButton),
      aoFechar_(std::move(aoFechar)) {
    // Moldura do sistema de janelas: sem barra nativa; cabeçalho + X redondo (mesma ação do antigo EXIT). Janela transparente e
    // sem a sombra do sistema: fora dos cantos arredondados não há fundo (nada de retângulo bege ou cantos pretos atrás).
    setUsingNativeTitleBar(false);
    setTitleBarHeight(0);
    moldura::deixarJanelaTransparente(*this);
    setResizable(true, false);
    auto* conteudo = new Conteudo(projeto, pastaId, nomePasta, std::move(aoMudouDados), nestId, selecionarTudo);
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
    setResizeLimits(moldura::tamanhoComMargem(1000), moldura::tamanhoComMargem(560 + moldura::kAlturaCabecalho), 5000, 4000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    toFront(true);
}

JanelaPastaFlutuante::~JanelaPastaFlutuante() { clearContentComponent(); }

void JanelaPastaFlutuante::irParaPastaParaTeste(const std::string& id, const juce::String& nome) {
    if (conteudo_ != nullptr) conteudo_->irParaPasta(id, nome);
}

void JanelaPastaFlutuante::mostrarArquivoParaTeste(const std::string& itemId) {
    if (conteudo_ != nullptr) {
        conteudo_->mosaico_->definirSelecao({itemId});
        conteudo_->aoMudarSelecao();
    }
}

void JanelaPastaFlutuante::closeButtonPressed() {
    juce::Component::SafePointer<JanelaPastaFlutuante> seguro(this);
    juce::MessageManager::callAsync([seguro, cb = aoFechar_] { if (seguro != nullptr && cb) cb(); });
}

bool JanelaPastaFlutuante::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) { closeButtonPressed(); return true; }
    return DocumentWindow::keyPressed(key);
}

}  // namespace matriz::ui
