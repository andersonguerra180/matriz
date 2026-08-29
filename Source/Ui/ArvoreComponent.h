#pragma once

#include <JuceHeader.h>

#include <optional>

#include "ProjetoAberto.h"

namespace matriz::ui {

struct SubpastaInfo;

class ArvoreComponent : public juce::Component, public juce::DragAndDropTarget {
public:
    explicit ArvoreComponent(ProjetoAberto& projeto);

    void recarregar();
    void recarregarSincrono();

    enum class Aba { Origem, Acervo };
    void definirAba(Aba aba);
    Aba abaAtual() const { return aba_; }

    void criarPastaDeTopoNivel();

    std::function<void(std::optional<std::set<std::string>> itemIdsFiltrados)> aoSelecionarNo;
    std::function<void(const std::set<std::string>& itemIds)> aoMostrarConteudoNaGrade;
    std::function<void()> aoMudarOrganizacao;

    void definirIncluirSubpastas(bool incluir);
    bool incluirSubpastas() const { return incluirSubpastas_; }

    const std::set<std::string>& todosOsItensVisiveis() const { return raiz_.itemIds; }

    std::function<void(std::vector<SubpastaInfo>)> aoMudarSubpastas;

    void selecionarNoPorItemIds(const std::set<std::string>& itemIds);

    enum class ModoVisao { Lista, Icones };
    void definirModoVisao(ModoVisao modo);
    ModoVisao modoVisao() const { return modoVisao_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

private:
    struct LinhaAchatada {
        const ProjetoAberto::NoArvore* no;
        int profundidade;
        bool ehPseudoTodos = false;
        bool ehPasta = false;
    };

    void reconstruirLinhas();
    void notificarSelecao();
    void soltarPastaDaExplorer(const std::string& pastaDestinoId);
    void achatar(const ProjetoAberto::NoArvore& no, int profundidade);
    int indiceLinhaNaPosicao(int y) const;
    juce::Rectangle<int> boundsDaLinha(int indice) const;
    void abrirMenuContexto(int indiceLinha);
    const ProjetoAberto::NoArvore* encontrarNoPorId(const ProjetoAberto::NoArvore& no, const std::string& id) const;

    // Icon view helpers
    void pintarModoIcones(juce::Graphics& g);
    void pintarModoLista(juce::Graphics& g);
    int indiceCelulaNaPosicao(int x, int y) const;
    juce::Rectangle<int> boundsDaCelula(int indice) const;
    void navegarParaPasta(const ProjetoAberto::NoArvore* pasta);
    void navegarPraCima();
    void reconstruirConteudoPastaAtual();
    juce::Image miniaturaParaItem(const std::string& itemId);

    ProjetoAberto& projeto_;
    Aba aba_ = Aba::Origem;
    ModoVisao modoVisao_ = ModoVisao::Lista;
    ProjetoAberto::NoArvore raiz_;
    juce::ThreadPool poolArvore_{juce::ThreadPoolOptions{}.withThreadName("MatrizArvore")
                                 .withNumberOfThreads(1)
                                 .withDesiredThreadPriority(juce::Thread::Priority::low)};
    int geracaoArvore_ = 0;
    bool arvorePendente_ = false;
    std::vector<LinhaAchatada> linhas_;
    const ProjetoAberto::NoArvore* selecionado_ = nullptr;

    // Expand/collapse state for list mode
    std::set<std::string> colapsados_;

    // Icon mode navigation
    const ProjetoAberto::NoArvore* pastaAtual_ = nullptr;
    struct ItemIcone {
        const ProjetoAberto::NoArvore* noPasta = nullptr;
        std::string itemId;
        juce::String nome;
        bool ehPasta = false;
    };
    std::vector<ItemIcone> itensIcone_;
    int iconeHover_ = -1;
    std::vector<juce::String> breadcrumb_;

    int linhaAlvoDrop_ = -1;
    bool incluirSubpastas_ = false;

    std::optional<ProjetoAberto::NoArvore> subarvoreArrastada_;
public:
    std::optional<ProjetoAberto::NoArvore> pegarSubarvoreArrastada() {
        auto r = std::move(subarvoreArrastada_);
        subarvoreArrastada_.reset();
        return r;
    }
private:

    // Image cache for icon view
    std::unordered_map<std::string, juce::Image> cacheMiniaturas_;

    static constexpr int kAlturaLinha = 26;
    static constexpr int kIndentacao = 16;
    static constexpr int kLarguraCelula = 96;
    static constexpr int kAlturaCelula = 96;
    static constexpr int kAlturaBreadcrumb = 28;
};

} // namespace matriz::ui
