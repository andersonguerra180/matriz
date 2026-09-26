#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "EventBus.h"
#include "FloatingPreviewWindow.h"
#include "ViewModeIconButton.h"

namespace matriz::ui {

class ProjetoAberto;
class MosaicoComponent;
class FichaPanelComponent;
class EstatisticasComponent;
class ArvoreBackupComponent;

class CatalogWorkspaceComponent : public juce::Component, private juce::Timer {
public:
    explicit CatalogWorkspaceComponent(ProjetoAberto& projeto);
    ~CatalogWorkspaceComponent() override;

    void recarregar();
    void filtrarPorChave(const std::string& chave);
    void filtrarPorIds(std::set<std::string> ids);
    std::set<std::string> itensSelecionados() const;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void renomearSelecionados();
    void removerSelecionadosDoBackup();

    void selecionarFiltroSelecionados();
    void selecionarFiltroTodos();
    void definirSelecaoItens(const std::set<std::string>& itemIds);

    std::function<void()> aoVoltar;
    std::function<void(std::string folderId)> aoAgruparEIrParaTree;
    std::function<void(const std::string& itemId)> aoItemAlterado;

private:
    struct CategoriaItem {
        juce::String rotulo;
        std::string chave;
        int contagem = 0;
    };

    struct ContagensResultado {
        int total = 0;
        int audio = 0;
        int video = 0;
        int img = 0;
        int doc = 0;
        int sessao = 0;
        int revisao = 0;
        int vulneraveis = 0;
        int single_copy = 0;
        int ausentes = 0;
        std::vector<std::pair<int, int>> anos;
        std::vector<std::pair<std::string, int>> collections;
    };

    void construirSidebar();
    void construirFiltroAnos();
    void construirFiltroCollection();
    void aplicarFiltroAno();
    void aplicarFiltrosAdicionais();
    // Só repinta o destaque dos botões de ano/combo de collection a partir de
    // anosSelecionados_/collectionSelecionado_ — sem tocar no filtro da grade
    // nem chamar mosaico_->recarregar() (ver comentário em aoMudar).
    void atualizarDestaqueBotoesAnoCollection();
    void atualizarContagens();
    void aplicarContagens(const ContagensResultado& res);
    void selecionarCategoria(int indice);
    // Reset de todos os filtros (botão HOME / "All Assets"). incluirBusca
    // = false mantém o termo digitado — usado por executarBusca().
    void limparTodosOsFiltros(bool incluirBusca = true);
    // Dispara a busca da lupa (zera os filtros antes de buscar).
    void executarBusca();
    // item 6: remove TODOS os chips de busca ativos de uma vez (usado pelo
    // × existente ao lado do campo e pelo reset de HOME).
    void limparChipsDeBusca();
    void atualizarBotoesSidebar();
    void navegarParaPastaOrigem(std::optional<std::string> nomePasta);
    void revalidarPastaAtual();
    void abrirWorkbench(const std::string& itemId);
    void fecharWorkbench();
    void abrirRelinkOffline(const std::string& itemId);
    void abrirMenuContexto(std::vector<std::string> itemIds);
    void selecionarItem(const std::string& itemId);
    void timerCallback() override;

    ProjetoAberto& projeto_;

    std::unique_ptr<juce::Viewport> mosaicoViewport_;
    std::unique_ptr<MosaicoComponent> mosaico_;
    std::unique_ptr<FichaPanelComponent> fichaPanel_;

    std::unique_ptr<FloatingPreviewWindow> activePreviewWindow_;

    std::unique_ptr<juce::TextEditor> campoBusca_;
    // Lupa à esquerda do campo: é o gatilho da busca (Enter também serve).
    // Não existe mais busca ao vivo por tecla digitada — e por isso o
    // debounce que existia aqui deixou de ser necessário.
    std::unique_ptr<juce::Button> btnLupaBusca_;
    std::unique_ptr<juce::TextButton> btnLimparBusca_;

    // Item 1 (lista nova de hoje): a busca ativa aparece como chips (estilo
    // tag, com × pra fechar cada um) logo abaixo da caixa, em vez de só
    // sumir — ficam visíveis/filtrando até o × de cada um ser clicado ou
    // até HOME (All Assets), que já passa por
    // limparTodosOsFiltros(incluirBusca=true). Item 6: múltiplos chips
    // simultâneos, combinados com E (ver MosaicoComponent::buscaTermos_) —
    // Enter/lupa ACRESCENTA um chip novo em vez de substituir o existente.
    // Quando não cabem mais na largura visível, rolam na horizontal (ver
    // viewportChipsBusca_ logo abaixo).
    class ChipsBuscaAtivaComponent : public juce::Component {
    public:
        void definirTermos(const juce::StringArray& termos) {
            termos_ = termos;
            recalcularLargura();
            repaint();
        }
        bool temTermos() const { return !termos_.isEmpty(); }

        std::function<void(int indice)> aoFecharChip;

        void paint(juce::Graphics& g) override {
            if (termos_.isEmpty()) return;
            const auto& tk = matriz::ui::tema();
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));

            for (int i = 0; i < termos_.size(); ++i) {
                auto chip = pillBounds(i).toFloat();
                float raio = chip.getHeight() / 2.0f;
                g.setColour(tk.acento.withAlpha(0.25f));
                g.fillRoundedRectangle(chip, raio);
                g.setColour(tk.acento.withAlpha(0.7f));
                g.drawRoundedRectangle(chip.reduced(0.5f), raio, 1.0f);

                g.setColour(tk.textoPrimario);
                auto textArea = pillBounds(i).reduced(10, 0).withTrimmedRight(16);
                g.drawText(termos_[i], textArea, juce::Justification::centredLeft, true);

                g.setColour(tk.textoPrimario.withAlpha(0.65f));
                auto fechar = closeBounds(i).toFloat();
                float cx = fechar.getCentreX(), cy = fechar.getCentreY(), sz = 3.0f;
                g.drawLine(cx - sz, cy - sz, cx + sz, cy + sz, 1.5f);
                g.drawLine(cx + sz, cy - sz, cx - sz, cy + sz, 1.5f);
            }
        }

        void mouseUp(const juce::MouseEvent& e) override {
            for (int i = 0; i < termos_.size(); ++i) {
                if (closeBounds(i).contains(e.getPosition()) || pillBounds(i).contains(e.getPosition())) {
                    if (aoFecharChip) aoFecharChip(i);
                    return;
                }
            }
        }

    private:
        int larguraChip(int indice) const {
            auto font = juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena));
            int textW = juce::GlyphArrangement::getStringWidthInt(font, termos_[indice]);
            return juce::jmax(40, 10 + textW + 6 + 14 + 8);
        }
        juce::Rectangle<int> pillBounds(int indice) const {
            int x = 0;
            for (int i = 0; i < indice; ++i) x += larguraChip(i) + 6;
            return { x, 0, larguraChip(indice), getHeight() };
        }
        juce::Rectangle<int> closeBounds(int indice) const {
            auto p = pillBounds(indice);
            return { p.getRight() - 8 - 14, 3, 14, juce::jmax(1, p.getHeight() - 6) };
        }
        void recalcularLargura() {
            int total = 0;
            for (int i = 0; i < termos_.size(); ++i) total += larguraChip(i) + 6;
            setSize(juce::jmax(total, 1), juce::jmax(getHeight(), 1));
        }

        juce::StringArray termos_;
    };
    std::unique_ptr<ChipsBuscaAtivaComponent> chipBuscaAtiva_;
    std::unique_ptr<juce::Viewport> viewportChipsBusca_; // rolagem horizontal quando os chips não cabem
    std::unique_ptr<juce::Slider> sliderTamanho_;
    std::unique_ptr<juce::Label> lblTamanho_;
    std::unique_ptr<ViewModeIconButton> btnVisaoGrade_;
    std::unique_ptr<ViewModeIconButton> btnVisaoLista_;
    std::unique_ptr<juce::TextButton> btnOcultarEditados_;
    std::unique_ptr<juce::TextButton> btnOcultarNaoSelecionados_;
    std::unique_ptr<juce::TextButton> btnMostrarRecentes_;
    std::unique_ptr<juce::TextButton> btnSelecionarTodos_;
    std::unique_ptr<juce::TextButton> btnLimparSelecao_;
    bool modoVisaoGrade_ = true;
    bool ocultarEditados_ = false;
    bool ocultarNaoSelecionados_ = false;
    bool mostrarApenasRecentes_ = false;
    bool editMode_ = true;
    std::optional<std::string> pastaNavegarAtual_;
    std::vector<std::string> caminhoNavegacao_;
    std::unique_ptr<juce::Label> lblCaminhoNavegacao_;

    struct SecaoSidebar { int indicePrimeiro; juce::String titulo; };
    std::vector<SecaoSidebar> secoesSidebar_;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> secaoHeaderBounds_;
    // Cards da coluna esquerda (item 7): um retângulo por seção
    // (LIBRARY / MEDIA TYPE / DATE), pintado com borda própria em paint().
    std::vector<juce::Rectangle<int>> secaoCardBounds_;

    struct SidebarButtonLookAndFeel : public juce::LookAndFeel_V4 {
        juce::Font getTextButtonFont(juce::TextButton&, int) override {
            return juce::Font(juce::FontOptions(11.0f));
        }
    };
    SidebarButtonLookAndFeel sidebarButtonLf_;

    // HOME ("All Assets"): desenha o ícone home.png (fundo branco removido no
    // carregamento) na metade de cima do botão e o rótulo logo abaixo — por
    // isso o botão tem o dobro da altura dos demais itens da coluna.
    struct HomeButtonLookAndFeel : public juce::LookAndFeel_V4 {
        HomeButtonLookAndFeel();
        juce::Font getTextButtonFont(juce::TextButton&, int) override {
            return juce::Font(juce::FontOptions(11.0f));
        }
        void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
        juce::Image icone;
    };
    HomeButtonLookAndFeel homeButtonLf_;

    std::vector<CategoriaItem> categorias_;
    int categoriaSelecionada_ = 0;
    std::vector<std::unique_ptr<juce::TextButton>> botoesCategorias_;
    std::optional<std::string> tipoMidiaSelecionado_;
    int indiceInicioMediaType_ = 0;

    // Um EVENT DATE gravado na ficha precisa reavaliar os filtros de data na
    // hora. O listener é um adaptador em vez de herança porque o nome
    // aoItemAlterado já é usado pelo callback público acima.
    struct EscutaEventos : public EventBusListener {
        explicit EscutaEventos(CatalogWorkspaceComponent& o) : dono(o) {}
        void aoItemAlterado(const EventoItemAlterado& e) override;
        CatalogWorkspaceComponent& dono;
    };
    EscutaEventos escutaEventos_{*this};
    void atualizarFiltrosDeData();
    bool refreshDataPendente_ = false;
    bool contagensAguardandoSnapshot_ = false;
    bool filtrosAguardandoSnapshot_ = false;
    bool reaplicandoFiltrosAposSnapshot_ = false;

    std::vector<std::pair<int, int>> anosDisponiveis_;
    std::vector<std::unique_ptr<juce::TextButton>> botoesAnos_;
    // Item 6 (nova lista): conjunto em vez de optional<int> — permite marcar
    // mais de um ano quando permitirMultiplosAnos_ está ON. Com o toggle
    // OFF (default), o conjunto nunca passa de 1 elemento (mesmo
    // comportamento de antes).
    std::set<int> anosSelecionados_;
    bool permitirMultiplosAnos_ = false;
    std::unique_ptr<juce::ToggleButton> toggleMultiplosAnos_;
    juce::Component anosContainer_;
    std::unique_ptr<juce::Viewport> anosViewport_;

    std::vector<std::pair<std::string, int>> collectionDisponiveis_;
    std::unique_ptr<juce::ComboBox> comboContentType_;
    std::optional<std::string> collectionSelecionado_;

    juce::ThreadPool poolMiniaturas_{juce::ThreadPoolOptions{}.withThreadName("MatrizMiniGen")
                                     .withNumberOfThreads(1)
                                     .withDesiredThreadPriority(juce::Thread::Priority::low)};

    juce::ThreadPool poolContagens_{juce::ThreadPoolOptions{}.withThreadName("MatrizContagens")
                                    .withNumberOfThreads(1)
                                    .withDesiredThreadPriority(juce::Thread::Priority::low)};

    static constexpr int kLarguraSidebar = 200;
    static constexpr int kLarguraFichaMin = 280;
    static constexpr int kLarguraFichaMax = 900;

    int larguraFicha_ = 540;
    bool fichaColapsada_ = false;

    std::unique_ptr<juce::Component> fichaResizerBar_;
    juce::Rectangle<int> toolbarBounds_;
    std::unique_ptr<juce::TextButton> btnToggleFicha_;
    std::unique_ptr<juce::Component> legendaAtalhos_;
};

} // namespace matriz::ui
