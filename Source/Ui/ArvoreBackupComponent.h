#pragma once

#include <JuceHeader.h>
#include "ProjetoAberto.h"
#include "EventBus.h"

namespace matriz::ui {

class TreeDetailContent;

class ArvoreBackupComponent : public juce::Component,
                               private juce::Timer,
                               public EventBusListener,
                               public juce::DragAndDropTarget {
public:
    friend int rodarLoteSelfTest();  // --selftest-lote (LoteSelfTest.cpp)
    explicit ArvoreBackupComponent(ProjetoAberto& projeto);
    ~ArvoreBackupComponent() override;

    void aoItemAlterado(const EventoItemAlterado& e) override;

    void recarregar();
    void moverItemParaPasta(const std::string& itemId, const std::string& novaPasta);
    void selecionarERenomearPasta(const std::string& pastaId);

    void criarNovaPasta(const std::string& nome, const std::optional<std::string>& pastaPaiId = std::nullopt);
    void renomearPastaSelecionada(const std::string& pastaId, const std::string& novoNome);
    void apagarPastaSelecionada(const std::string& pastaId);
    void conectarPastas(const std::string& pastaFilhoId, const std::optional<std::string>& novaPastaPaiId);
    void alternarAtivoPasta(const std::string& pastaId);
    // Item 7 — desconecta todas as pastas selecionadas de seus pais numa
    // única operação (menu de contexto e atalho D, ver keyPressed). Sem
    // pai: ignorada em silêncio. Travada pelo MAIN: pulada, contada num
    // aviso único no final (nunca um alerta por pasta). Tudo num só grupo
    // de desfazer.
    void desconectarSelecionadas();

    // FOLDER COLOR (item 12) — menu de contexto "FOLDER COLOR" abre um
    // color picker; a cor escolhida vira overlay translúcido em todas as
    // pastas de pastaIds, persistido em acervo_pasta.cor_customizada.
    void mostrarSeletorDeCorPasta(std::vector<std::string> pastaIds, juce::Rectangle<int> screenBounds);
    void aplicarCorAPastas(const std::vector<std::string>& pastaIds, juce::Colour cor);

    // Fase 3 — histórico de cores do Folder Color, por projeto (ver
    // ProjetoAberto::historicoCoresPasta/definirHistoricoCoresPasta).
    void registrarCorNoHistorico(juce::Colour cor);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::vector<juce::String> obterListaPastas() const;

    std::function<void(const std::set<std::string>& itemIds)> aoMostrarConteudoNaGrade;

    // Fase 1 (Folder Maps múltiplos) — SEM PASTA (NO FOLDER) só aceita
    // drop de itens arrastados da grade (MosaicoComponent::startDragging,
    // descrição = array de item ids); não interfere no drag de pastas
    // dentro do canvas, que continua em mouseDown/mouseDrag/mouseUp.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

private:
    struct FolderNode {
        std::string id;
        juce::String nome;
        std::string pastaPaiId;
        int contagemItens = 0;
        juce::int64 tamanhoBytes = 0;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> boundsOriginal; // bounds a 100% do slider de tamanho (S4/14)
        bool ativo = true;
        bool selecionado = false;
        std::set<std::string> itemIdsDiretos;
        // FOLDER COLOR (item 12) — overlay visual translúcido, persistido em
        // acervo_pasta.cor_customizada; hasCorCustomizada=false = sem marcação.
        juce::Colour corCustomizada;
        bool hasCorCustomizada = false;
    };

    ProjetoAberto& projeto_;
    std::vector<FolderNode> nodes_;
    int nodeDragIndice_ = -1;
    juce::Point<int> arrastoOffset_;
    // Item 10: quando o arrasto começa dentro de uma seleção múltipla, guarda
    // a posição (em canvas) de cada pasta selecionada no instante do clique,
    // pra aplicar o mesmo delta do nó primário (nodeDragIndice_) em todas —
    // sem isto só a pasta clicada se movia, o resto da seleção ficava parado.
    std::vector<std::pair<int, juce::Point<float>>> arrastoGrupoPosicoesIniciais_;

    float zoom_ = 1.0f;
    juce::Point<float> panOffset_{0.0f, 0.0f};
    bool panning_ = false;
    juce::Point<int> panStart_;

    std::unique_ptr<juce::TextButton> btnCriarPasta_;
    std::unique_ptr<juce::TextButton> btnRenomearPasta_;
    std::unique_ptr<juce::TextButton> btnApagarPasta_;
    std::unique_ptr<juce::TextButton> btnAutoArranjar_;
    std::unique_ptr<juce::TextButton> btnZoomIn_;
    std::unique_ptr<juce::TextButton> btnZoomOut_;
    std::unique_ptr<juce::TextButton> btnZoomFit_;

    // Fase 1 — dropdown com todos os folder maps do projeto (ORIGINAL +
    // mapas do usuário), botão "⋯" (rename/duplicate/delete/export/import)
    // e NEW FOLDER MAP. Substituem btnImportarEstrutura_/btnPresets_, que
    // não existem mais (ver AGENTS.md / decisão da Fase 1: ORIGINAL, ao
    // vivo, cobre o que "Import/Update Structure" fazia).
    std::unique_ptr<juce::ComboBox> comboMapas_;
    std::unique_ptr<juce::TextButton> btnMenuMapa_;
    std::unique_ptr<juce::TextButton> btnNovoMapa_;
    std::string mapaAtivoId_;
    std::vector<ProjetoAberto::FolderMapInfo> mapasCache_; // espelha comboMapas_, id por índice
    bool mapaAtivoEhOriginal() const { return mapaAtivoId_ == ProjetoAberto::kMapaOriginal; }
    void recarregarComboMapas();
    void selecionarMapaPorId(const std::string& mapaId);
    void atualizarEstadoBotoesParaMapa();
    void mostrarMenuMapa();
    void mostrarDialogoNovoMapa();
    void pedirNomeECriarMapa(std::optional<std::string> origemMapaId);
    // Migra presets_pastas/*.json (mecanismo antigo, por arquivo) pra
    // folder_map — uma vez só; cada arquivo migrado vira <nome>.json.importado
    // (não apagado, só marcado, pra não reimportar no próximo load).
    void migrarPresetsAntigosSeNecessario();

    // SEM PASTA (NO FOLDER) — painel fixo em espaço de TELA (não pan/zoom
    // com o canvas), canto inferior-esquerdo; nunca aparece com o ORIGINAL
    // ativo (S4/13: "Não aparece no ORIGINAL").
    juce::Rectangle<int> boundsSemPasta() const;
    void desenharSemPasta(juce::Graphics& g) const;
    bool semPastaHover_ = false;

    // S4/14 — slider de tamanho dos retângulos (independente do zoom)
    std::unique_ptr<juce::Slider> sliderTamanho_;
    float escalaTamanho_ = 1.0f;
    // S4/15 — pasta recém-criada, com destaque pulsante até o próximo clique
    std::string destaqueNovaPastaId_;

    std::unique_ptr<juce::TextEditor> inlineEditor_;
    std::string editingNodeId_;

    void recalcularNodes();
    void autoArranjar();

    // Fase 1 — Export/Import to file do mapa ATIVO, formato JSON herdado do
    // antigo mecanismo de presets (S4/13). pastaPresets()/listarPresetsSalvos()
    // hoje só servem pra migrarPresetsAntigosSeNecessario() achar os arquivos
    // antigos — "Export to file" grava onde o usuário escolher (FileChooser),
    // não mais nessa pasta.
    juce::File pastaPresets() const;
    std::vector<juce::String> listarPresetsSalvos() const;
    juce::var construirEsquemaAtualComoVar() const;
    bool salvarEsquemaComoPreset(const juce::String& nomePreset, juce::String& erro) const;
    // Import from file/migração de preset antigo SEMPRE cria um mapa novo
    // (nunca substitui o ativo) — ver S4/13. itensPulados conta itens cujo
    // item_id nem codigoAcervo foram encontrados neste projeto.
    void importarEsquemaComoNovoMapa(const juce::String& nomeMapa, const juce::var& dados, int& itensRelocados,
                                      int& itensPulados);
    void confirmarImportarComoNovoMapa(const juce::String& nomeSugerido, const juce::var& dados);
    void exportarPresetParaArquivo();
    void importarPresetDeArquivo();

    // S4/14 — slider de tamanho (não mexe em zoom_/panOffset_/posições persistidas)
    void aplicarEscalaTamanho(float escala);

    // S4/15 — posição livre mais próxima do centro da área visível
    juce::Point<int> posicaoLivrePertoDoCentro(int nodeW, int nodeH) const;

    void timerCallback() override; // pulso do destaque de pasta nova
    void desenharLinhaConexaoN8n(juce::Graphics& g, juce::Point<float> p1, juce::Point<float> p2, bool ativo, bool rascunho = false) const;
    bool ehDescendente(const std::string& noPaiId, const std::string& noFilhoId) const;
    // Item 8 — índices (em nodes_) de todas as descendentes de nodeIndex
    // (filhas, netas etc.), pra "arrastar pasta-pai move o bloco inteiro".
    void coletarDescendentesIndices(int nodeIndex, std::set<int>& acc) const;
    // Grava a posição atual (nodes_[idx].bounds) de cada índice numa única
    // transação — chamado do mouseUp ao soltar o arrasto (item 8); extraído
    // à parte pra dar pra testar sem simular um MouseEvent de verdade.
    void persistirPosicoesEmLote(const std::vector<int>& indices);

    void aplicarZoom(float novoZoom, juce::Point<float> centro);
    juce::Point<float> screenToCanvas(juce::Point<int> screen) const;
    juce::Point<int> canvasToScreen(juce::Point<float> canvas) const;
    void iniciarEdicaoInline(int nodeIndex);
    void finalizarEdicaoInline();
    void desenharMinimap(juce::Graphics& g) const;
    juce::Rectangle<int> minimapBounds() const;

    std::string socketDragParentId_;
    juce::Point<float> socketDragPos_;

    bool minimapDragging_ = false;

    bool marqueeSelecting_ = false;
    juce::Point<float> marqueeStartCanvas_;
    juce::Rectangle<float> marqueeRectCanvas_;

    static constexpr int kDetailPanelWidth = 280;
    std::unique_ptr<juce::Viewport> detailViewport_;
    std::unique_ptr<TreeDetailContent> detailContent_;
    std::string selectedFolderId_;
    void atualizarPainelDetalhe(const std::string& folderId, bool forcar = false);

    // Abas do Folder Map (isolar pasta + filhos numa visão à parte, tipo
    // navegador): puramente uma FILTRAGEM de visualização — todas as abas
    // leem o MESMO acervo_pasta ao vivo, então criar/renomear/apagar pasta
    // numa aba isolada aparece na hora em "All" e nas outras abas também.
    // Nada é persistido no banco; fecha o app, as abas somem.
    struct AbaEstrutura {
        std::optional<std::string> pastaRaizId; // nullopt = "All" (acervo inteiro)
        juce::String titulo;
        float zoom = 1.0f;
        juce::Point<float> panOffset{0.0f, 0.0f};
        std::string selectedFolderId;
    };
    std::vector<AbaEstrutura> abas_;
    int abaAtiva_ = 0;
    static constexpr int kAlturaBarraAbas = 26;
    juce::Rectangle<int> areaBarraAbas() const;
    juce::Rectangle<int> boundsDaAba(int indice) const;
    juce::Rectangle<int> boundsFecharAba(int indice) const;
    void abrirAbaParaPasta(const std::string& pastaId, const juce::String& nomePasta);
    void selecionarAba(int indice);
    void fecharAba(int indice);
    void desenharBarraDeAbas(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArvoreBackupComponent)
};

} // namespace matriz::ui
