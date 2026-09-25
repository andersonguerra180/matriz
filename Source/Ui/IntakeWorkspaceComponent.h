#pragma once

// ==============================================================================
// INTAKE WORKSPACE COMPONENT
// STRICT APP-WIDE RULE: 100% ENGLISH UI. ZERO PORTUGUESE TEXT IN USER INTERFACE.
// ==============================================================================

#include <JuceHeader.h>
#include "ViewModeIconButton.h"
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
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

    enum class RescanOrigem {
        Nenhum,
        Novo,
        Modificado
    };

    void registrarItensRescan(const std::vector<std::pair<std::string, RescanOrigem>>& itensRescan);

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
    void lookAndFeelChanged() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    std::function<void()> aoPedirIngerirArquivos;
    std::function<void(const juce::Array<juce::File>&)> aoIngerirArquivosDireto;
    // Called with the Google Drive root folder when the GD button is clicked
    // and the mount point exists. Host should open a FileChooser at that path.
    std::function<void(const juce::File& gdFolder)> aoIngerirDeGoogleDrive;
    // Called when the Lightroom import button is clicked
    std::function<void()> aoIngerirDeLightroom;
    std::function<void()> aoConfirmarParaGrid;
    std::function<void()> aoPedirAjuda;
    void setHasParentCatalog(bool hasParent);

    // Os comandos do topo do INTAKE moraram numa faixa própria de 44px logo
    // abaixo das tabs; agora são hospedados pela barra de navegação, na mesma
    // linha das tabs (itens 2 e 3 do ajuste de layout). Os botões continuam
    // sendo destes objeto — a barra só os posiciona.
    void componentesBarraSuperior(std::vector<std::pair<juce::Component*, int>>& esquerda,
                                  std::vector<std::pair<juce::Component*, int>>& direita);

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
        RescanOrigem rescanOrigem = RescanOrigem::Nenhum;
    };

    class ThumbnailsGridComponent;
    class IntakeDragDropEmptyState;
    friend class IntakeDragDropEmptyState;

    void carregarItens();
    void atualizarFiltragem();
    void atualizarContagens();
    void atualizarVisibilidadeEmptyState();
    void aplicarColecaoAosSelecionados(const juce::String& colecao);
    void definirColecaoItem(const std::string& itemId, const juce::String& colecao);
    void aplicarOriginalSourceMediumAosSelecionados(const std::string& sourceMediaJson);
    void definirSourceMediaItem(const std::string& itemId, const std::string& sourceMediaJson);
    void mostrarEditorOriginalSourceMedium(int itemIndex, juce::Rectangle<int> screenBounds);
    void mostrarEditorOriginalSourceMediumLote(juce::Rectangle<int> screenBounds);
    void aplicarGeolocationAosSelecionados(const std::string& coords, const std::string& addr, const std::string& city, const std::string& state, const std::string& country);
    void mostrarEditorGeolocationLote(juce::Rectangle<int> screenBounds);

    // Batch Assignment — CREATOR/SUBJECT (texto com autocomplete) e CONTENT
    // (dropdown, substitui o combo+botão inline antigo por um popup, igual
    // aos outros três campos).
    void aplicarCreatorAosSelecionados(const juce::String& valor);
    void aplicarSubjectAosSelecionados(const juce::String& valor);
    void mostrarEditorCreatorLote(juce::Rectangle<int> screenBounds);
    void mostrarEditorSubjectLote(juce::Rectangle<int> screenBounds);
    void mostrarEditorContentLote(juce::Rectangle<int> screenBounds);
    // Item 2 (nova lista): EVENT DATE em lote — mesmo campo "ano" que a
    // ficha já edita, só que com um botão dedicado aqui no INTAKE. O
    // default (herdar de DATE CREATED) continua intocado: isto só grava
    // quando o operador de fato escolhe um valor no popup.
    void aplicarEventDateAosSelecionados(const juce::String& valor);
    void mostrarEditorEventDateLote(juce::Rectangle<int> screenBounds);
    // Valores já usados nessa coluna no projeto inteiro, sem duplicata,
    // ordenados alfabeticamente — base do autocomplete (item 4), lida
    // direto da tabela item (mesma fonte que a ficha grava).
    std::vector<juce::String> valoresExistentesParaColuna(const std::string& coluna) const;
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
    // Fase 2b (freeze de edição em lote): aplicarXAosSelecionados grava
    // metadado de N itens de uma vez — sai da message thread via este pool
    // dedicado (ver ProjetoAberto::salvarMetadadoEmLote). 1 thread só:
    // essas chamadas são raras (ação explícita do operador), nunca
    // concorrentes entre si.
    juce::ThreadPool poolMetadadoLote_{1};
    std::map<std::string, RescanOrigem> badgesRescanSessao_;
    std::vector<ItemIntake> todosItens_;
    std::vector<int> indicesFiltrados_; // indices into todosItens_
    // item (shift-click seleciona intervalo): âncora do último clique
    // simples (não-shift), em índice de indicesFiltrados_ (posição visível,
    // não realIdx) — shift+clique seleciona tudo entre esta âncora e o novo
    // clique. Compartilhado entre a lista (cellClicked) e o grid de ícones.
    int ultimaPosicaoClicadaParaSelecao_ = -1;
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
    std::unique_ptr<ViewModeIconButton> btnVisaoLista_;
    std::unique_ptr<ViewModeIconButton> btnVisaoIcones_;
    std::unique_ptr<juce::TextButton> btnIngerir_;
    std::unique_ptr<juce::Button> btnGoogleDrive_;
    std::unique_ptr<juce::Button> btnLightroom_;
    std::unique_ptr<juce::TextButton> btnAjuda_;
    bool hasParentCatalog_ = false;
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
    // Batch Assignment (item 2 da correção "BACKUP e INTAKE"): um botão
    // colorido por campo, cor idêntica ao bloco correspondente no Visual
    // Editor da aba BACKUP (HierarquiaEditorComponent::corDoNivel) — cada
    // um abre um popup próprio e grava direto na mesma coluna de metadado
    // que a ficha usa, sem estrutura paralela.
    std::unique_ptr<juce::TextButton> btnSourceMediumLote_;
    std::unique_ptr<juce::TextButton> btnCreatorLote_;
    std::unique_ptr<juce::TextButton> btnContentLote_;
    std::unique_ptr<juce::TextButton> btnSubjectLote_;
    std::unique_ptr<juce::TextButton> btnEventDateLote_;
    std::unique_ptr<juce::TextButton> btnGeolocationLote_;

    // Table List & Thumbnails Grid
    std::unique_ptr<juce::TableListBox> tabela_;
    std::unique_ptr<juce::ToggleButton> chkSelectAllHeader_;
    bool isDraggingRows_ = false;
    int dragStartRow_ = -1;
    bool dragSelectState_ = true;
    std::unique_ptr<juce::Viewport> gridViewport_;
    std::unique_ptr<ThumbnailsGridComponent> gridComponent_;
    std::unique_ptr<IntakeDragDropEmptyState> emptyState_;

    std::unique_ptr<juce::Label> lblDica_;

    std::unique_ptr<juce::Component> divisor1_;
    std::unique_ptr<juce::Component> divisor2_;

    // Cards da coluna esquerda (mesmo tratamento aplicado na aba METADATA):
    // um retângulo com título por seção, em vez das linhas divisórias soltas.
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> secaoHeaderBounds_;
    std::vector<juce::Rectangle<int>> secaoCardBounds_;
    juce::Image iconeGeo_; // geo.png, fundo branco removido — ao lado do título GEO LOCATION
};

} // namespace matriz::ui
