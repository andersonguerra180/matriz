#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "FloatingPreviewWindow.h"

namespace matriz::ui {

class ProjetoAberto;
class MosaicoComponent;
class FichaPanelComponent;
class EstatisticasComponent;
class ArvoreBackupComponent;
class PainelDuplicatasComponent;

class CatalogWorkspaceComponent : public juce::Component, private juce::Timer {
public:
    explicit CatalogWorkspaceComponent(ProjetoAberto& projeto);
    ~CatalogWorkspaceComponent() override;

    void recarregar();
    void filtrarRecentes();
    void filtrarPorChave(const std::string& chave);
    void filtrarPorIds(std::set<std::string> ids);
    std::set<std::string> itensSelecionados() const;

    void paint(juce::Graphics&) override;
    void resized() override;

    void renomearSelecionados();
    void removerSelecionadosDoBackup();

    std::function<void()> aoVoltar;
    std::function<void(std::string folderId)> aoAgruparEIrParaTree;

private:
    struct CategoriaItem {
        juce::String rotulo;
        std::string chave;
        int contagem = 0;
    };

    void construirSidebar();
    void construirFiltroAnos();
    void construirFiltroContent();
    void construirFiltroCollection();
    void aplicarFiltroAno();
    void aplicarFiltrosAdicionais();
    void atualizarContagens();
    void selecionarCategoria(int indice);
    void atualizarBotoesSidebar();
    void navegarParaPastaOrigem(std::optional<std::string> nomePasta);
    void revalidarPastaAtual();
    void abrirWorkbench(const std::string& itemId);
    void fecharWorkbench();
    void abrirMenuContexto(std::vector<std::string> itemIds);
    void selecionarItem(const std::string& itemId);
    void timerCallback() override;

    ProjetoAberto& projeto_;

    std::unique_ptr<juce::Viewport> mosaicoViewport_;
    std::unique_ptr<MosaicoComponent> mosaico_;
    std::unique_ptr<FichaPanelComponent> fichaPanel_;
    std::unique_ptr<PainelDuplicatasComponent> painelDuplicatas_;

    std::unique_ptr<FloatingPreviewWindow> activePreviewWindow_;

    std::unique_ptr<juce::TextEditor> campoBusca_;
    std::unique_ptr<juce::TextButton> btnLimparBusca_;
    std::unique_ptr<juce::Slider> sliderTamanho_;
    std::unique_ptr<juce::Label> lblTamanho_;
    std::unique_ptr<juce::TextButton> btnVisaoGrade_;
    std::unique_ptr<juce::TextButton> btnVisaoLista_;
    std::unique_ptr<juce::TextButton> btnSelecionarTodos_;
    std::unique_ptr<juce::TextButton> btnLimparSelecao_;
    bool editMode_ = true;
    std::optional<std::string> pastaNavegarAtual_;
    std::vector<std::string> caminhoNavegacao_;
    std::unique_ptr<juce::Label> lblCaminhoNavegacao_;

    struct SecaoSidebar { int indicePrimeiro; juce::String titulo; };
    std::vector<SecaoSidebar> secoesSidebar_;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> secaoHeaderBounds_;

    std::vector<CategoriaItem> categorias_;
    int categoriaSelecionada_ = 0;
    std::vector<std::unique_ptr<juce::TextButton>> botoesCategorias_;
    std::optional<std::string> tipoMidiaSelecionado_;
    std::optional<std::string> statusSelecionado_;
    int indiceInicioMediaType_ = 0;
    int indiceInicioStatus_ = 0;

    std::vector<std::pair<int, int>> anosDisponiveis_;
    std::vector<std::unique_ptr<juce::TextButton>> botoesAnos_;
    std::optional<int> anoSelecionado_;

    std::vector<std::pair<std::string, int>> contentDisponiveis_;
    std::vector<std::unique_ptr<juce::TextButton>> botoesContent_;
    std::optional<std::string> contentSelecionado_;

    std::vector<std::pair<std::string, int>> collectionDisponiveis_;
    std::vector<std::unique_ptr<juce::TextButton>> botoesCollection_;
    std::optional<std::string> collectionSelecionado_;

    juce::ThreadPool poolMiniaturas_{juce::ThreadPoolOptions{}.withThreadName("MatrizMiniGen")
                                     .withNumberOfThreads(1)
                                     .withDesiredThreadPriority(juce::Thread::Priority::low)};

    static constexpr int kLarguraSidebar = 200;
    static constexpr int kLarguraFicha = 280;
};

} // namespace matriz::ui
