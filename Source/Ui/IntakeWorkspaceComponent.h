#pragma once

// ==============================================================================
// INTAKE WORKSPACE COMPONENT
// STRICT APP-WIDE RULE: 100% ENGLISH UI. ZERO PORTUGUESE TEXT IN USER INTERFACE.
// ==============================================================================

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace matriz::ui {

class ProjetoAberto;

class IntakeWorkspaceComponent : public juce::Component,
                                 public juce::TableListBoxModel,
                                 public juce::FileDragAndDropTarget {
public:
    explicit IntakeWorkspaceComponent(ProjetoAberto& projeto);
    ~IntakeWorkspaceComponent() override;

    void recarregar();
    std::set<std::string> itensSelecionados() const;

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;
    void abrirArquivoOrigem(int rowNumber);

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    enum class ModoVisao {
        Lista,
        Icones
    };
    void definirModoVisao(ModoVisao modo);
    ModoVisao modoVisaoAtual() const { return modoVisao_; }

    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void()> aoPedirIngerirArquivos;
    std::function<void(const juce::Array<juce::File>&)> aoIngerirArquivosDireto;
    std::function<void()> aoConfirmarParaGrid;

    // Static helper to get controlled collections vocabulary
    struct CategoriaColecao {
        juce::String grupo;
        std::vector<juce::String> itens;
    };
    static const std::vector<CategoriaColecao>& vocabularioColecoes();
    static void popularComboColecoes(juce::ComboBox& combo, bool incluirNone = true);

private:
    struct ItemIntake {
        std::string id;
        juce::String titulo;
        juce::String nomeArquivo;
        juce::String extensao;
        juce::String dataCriacao;
        juce::int64 tamanhoBytes = 0;
        juce::String caminhoOrigem;
        juce::String categoria; // "Audio", "Video", "Image", "Document", "Project", "Other"
        juce::String collection; // assigned collection or ""
        juce::String sourceMedia; // assigned original source medium or ""
        bool offline = false;
        bool selecionado = false;
    };

    class ThumbnailsGridComponent;

    void carregarItens();
    void atualizarFiltragem();
    void atualizarContagens();
    void aplicarColecaoAosSelecionados(const juce::String& colecao);
    void definirColecaoItem(const std::string& itemId, const juce::String& colecao);
    void aplicarOriginalSourceMediumAosSelecionados(const std::string& sourceMediaJson);
    void definirSourceMediaItem(const std::string& itemId, const std::string& sourceMediaJson);
    void mostrarEditorOriginalSourceMedium(int itemIndex, juce::Rectangle<int> screenBounds);
    void mostrarEditorOriginalSourceMediumLote(juce::Rectangle<int> screenBounds);
    void aplicarGeolocationAosSelecionados(const std::string& coords, const std::string& addr, const std::string& city, const std::string& state, const std::string& country);
    void mostrarEditorGeolocationLote(juce::Rectangle<int> screenBounds);
    void confirmarSelecaoParaGrid();
    void confirmarTodosParaGrid();
    void removerSelecionadosDoIntake();
    void rejeitarItemDoIntake(int itemIndex);
    void mostrarMenuContexto(int itemIndex, juce::Point<int> screenPos);
    void mostrarDialogoGetInfo(int itemIndex);
    void selecionarTodos(bool selecionar);
    void selecionarPorCategoria(const juce::String& categoria);
    void mostrarMenuColecaoParaItem(int itemIndex, juce::Rectangle<int> screenBounds);

    ProjetoAberto& projeto_;
    std::vector<ItemIntake> todosItens_;
    std::vector<int> indicesFiltrados_; // indices into todosItens_
    juce::String filtroCategoriaAtual_ = "ALL"; // "ALL", "Audio", "Video", "Image", "Document", "Other"
    int ultimoSortColumnId_ = 0;
    bool sortAscendente_ = true;
    ModoVisao modoVisao_ = ModoVisao::Lista;

    int contagemAudio_ = 0;
    int contagemVideo_ = 0;
    int contagemImage_ = 0;
    int contagemDoc_ = 0;
    int contagemOther_ = 0;

    // Top Header
    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblSubtitulo_;
    std::unique_ptr<juce::Label> lblContadorTotal_;
    std::unique_ptr<juce::TextButton> btnVisaoLista_;
    std::unique_ptr<juce::TextButton> btnVisaoIcones_;
    std::unique_ptr<juce::TextButton> btnIngerir_;
    std::unique_ptr<juce::TextButton> btnConfirmarSelecao_;
    std::unique_ptr<juce::TextButton> btnConfirmarTodos_;
    std::unique_ptr<juce::TextButton> btnRemoverSelecao_;

    // Batch Assignment Bar
    std::unique_ptr<juce::Label> lblLoteTitulo_;
    std::unique_ptr<juce::TextButton> btnFiltroAll_;
    std::unique_ptr<juce::TextButton> btnFiltroAudio_;
    std::unique_ptr<juce::TextButton> btnFiltroVideo_;
    std::unique_ptr<juce::TextButton> btnFiltroImage_;
    std::unique_ptr<juce::TextButton> btnFiltroDoc_;
    std::unique_ptr<juce::TextButton> btnFiltroOther_;

    std::unique_ptr<juce::TextButton> btnSelecionarTodos_;
    std::unique_ptr<juce::TextButton> btnLimparSelecao_;
    std::unique_ptr<juce::Label> lblRotuloColecao_;
    std::unique_ptr<juce::ComboBox> comboColecaoLote_;
    std::unique_ptr<juce::TextButton> btnAplicarColecaoLote_;
    std::unique_ptr<juce::TextButton> btnOriginalMediumLote_;
    std::unique_ptr<juce::TextButton> btnGeolocationLote_;

    // Table List & Thumbnails Grid
    std::unique_ptr<juce::TableListBox> tabela_;
    std::unique_ptr<juce::Viewport> gridViewport_;
    std::unique_ptr<ThumbnailsGridComponent> gridComponent_;

    std::unique_ptr<juce::Label> lblDica_;

    std::unique_ptr<juce::Component> divisor1_;
    std::unique_ptr<juce::Component> divisor2_;
};

} // namespace matriz::ui
