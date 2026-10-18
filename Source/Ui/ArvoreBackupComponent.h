#pragma once

#include <JuceHeader.h>
#include "ColunaCardsLayout.h"
#include "EventBus.h"
#include "ProjetoAberto.h"

namespace matriz::ui {

class TreeDetailContent;
class PainelPropostas;
class ItemPastaExplorador;
class PainelOverlay;
class PainelReceberCelular;
class EscolhaImportacaoCelular;
class ConversaWhatsappComponent;
enum class ModoImportacaoCelular;
class TrashWindow;

class ArvoreBackupComponent : public juce::Component,
                               private juce::Timer,
                               public EventBusListener,
                               public juce::DragAndDropTarget,
                               public juce::FileDragAndDropTarget {
public:
    friend int rodarLoteSelfTest();  // --selftest-lote (LoteSelfTest.cpp)
    explicit ArvoreBackupComponent(ProjetoAberto& projeto);
    ~ArvoreBackupComponent() override;

    void aoItemAlterado(const EventoItemAlterado& e) override;

    void recarregar();
    // true enquanto uma ingestão ainda está processando arquivos: a árvore proposta só nasce depois.
    std::function<bool()> aoPerguntarIngestEmProcessamento;
    // Ao entrar na aba STRUCTURE com arquivo(s) selecionados no grid: seleciona a(s) pasta(s) onde ele(s) está(ão)
    // e centraliza o mapa nela(s). Fica pendente até o canvas ter tamanho.
    void definirFocoNosItens(std::set<std::string> itemIds);
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
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::vector<juce::String> obterListaPastas() const;
    // Só pro self-test: a janela ACTION NEEDED está visível (e com área)?
    bool painelPropostasVisivel() const;
    ArvoreBackupComponent* mapaDaSobreposicao() const;  // idem: o Folder Map dentro da sobreposição aberta (ou nullptr)

    // Folder Map SOBREPOSTO (duplo clique num folder-card): janela por cima do app (90%) com um Folder Map cuja raiz
    // é a pasta clicada, sempre auto-arranjado. Edições de dados (criar/renomear/conectar/apagar/mover/cor) valem no
    // mapa de verdade; posição e tamanho dos cards ali são só da sobreposição (não mexem no layout do mapa principal).
    // SAIR fecha e recarrega este mapa.
    void abrirSobreposicao(const std::string& pastaId, const juce::String& nomePasta);
    // Duplo clique no card de uma pasta (mapa principal): quem hospeda abre a janela flutuante da pasta (miniaturas + filtros).
    // Sem este gancho, cai na sobreposição do Folder Map de antes.
    std::function<void(const std::string& pastaId, const juce::String& nomePasta)> aoAbrirPastaJanela;
    // Duplo clique num CONTAINER: abre a grade só com o conteúdo dele (nestId, nome mostrado).
    std::function<void(const std::string& nestId, const juce::String& nome)> aoAbrirContainerJanela;
    // Botão direito num PROJECT > Open Project: a janela própria do projeto (player de CD no DDP; preview com a capa nos demais).
    std::function<void(const std::string& nestId, const std::string& origem)> aoAbrirProject;
    void fecharSobreposicao();
    // Chamado na instância DENTRO da sobreposição, logo depois de construída.
    void definirModoSobreposto(const std::string& mapaId, const std::string& pastaRaizId, const juce::String& nomePasta);

    std::function<void(const std::set<std::string>& itemIds)> aoMostrarConteudoNaGrade;
    std::function<void(const juce::Array<juce::File>& files, const std::string& pastaId)> aoIngerirArquivos;

    void abrirConversaWhatsapp(const std::string& itemId);
    void fecharConversaWhatsapp();
    bool conversaAberta() const { return conversaWhatsapp_ != nullptr; }
    void fecharOverlayImportacao();
    std::string pastaSelecionadaId() const { return selectedFolderId_; }

    // Fase 1 (Folder Maps múltiplos) — SEM PASTA (NO FOLDER) só aceita
    // drop de itens arrastados da grade (MosaicoComponent::startDragging,
    // descrição = array de item ids); não interfere no drag de pastas
    // dentro do canvas, que continua em mouseDown/mouseDrag/mouseUp.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    struct FolderNode {
        std::string id;
        juce::String nome;
        std::string pastaPaiId;
        int contagemItens = 0;
        juce::int64 tamanhoBytes = 0;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> boundsOriginal; // bounds a 100% da escala global do slider (posição e tamanho)
        // Tamanho do folder: calculado pelo nível na hierarquia e pelo fator do mapa (autoW/autoH), vezes o
        // multiplicador do usuário (escalaManual, o slider com o folder selecionado). Persistido por pasta.
        float escalaManual = 1.0f;
        int autoW = 190, autoH = 84;
        int nivel = 0;
        bool ativo = true;
        bool selecionado = false;
        std::set<std::string> itemIdsDiretos;
        // FOLDER COLOR (item 12) — overlay visual translúcido, persistido em
        // acervo_pasta.cor_customizada; hasCorCustomizada=false = sem marcação.
        juce::Colour corCustomizada;
        bool hasCorCustomizada = false;
        bool proposta = false;  // árvore nova ainda não aceita (NEW FOLDERS)
        // Auto-organização: regra (CSV) da pasta e blocos já lidos; autoSub = subpasta criada pela regra.
        juce::String regra;
        matriz::consolidacao::HierarquiaBackup niveisRegra;
        bool autoSub = false;
    };

    struct LooseFileNode {
        std::string id;
        juce::String titulo;
        juce::String extensao;
        juce::String tipoMidia;
        juce::int64 tamanhoBytes = 0;
        juce::Rectangle<int> bounds;
        bool selecionado = false;
        bool ehConversaWhatsapp = false;
        int nestTotal = 0;  // >1: este arquivo solto é a capa de um NEST
        // CONTAINER de importação (nest com origem): solto no canvas como container, só com porta de ENTRADA (esquerda).
        bool ehContainer = false;
        std::string nestId, containerOrigem, containerIcone, containerCapa;  // capa = caminho absoluto ("" = sem capa)
        std::string pastaPaiId;  // pasta do mapa em que o container está ("" = solto na raiz): liga por uma linha, só na entrada
    };

    ProjetoAberto& projeto_;
    std::vector<FolderNode> nodes_;
    std::vector<LooseFileNode> looseFiles_;
    // Containers recém-aprovados no Intake, ainda sem posição: vão para o canto superior esquerdo da vista DEPOIS do
    // enquadramento inicial (que muda pan/zoom). Ver posicionarContainersNovos().
    std::set<std::string> containersSemPosicao_;
    // SHOW CONTAINERS AS FOLDERS: containers desenhados como pastas comuns (só com porta de entrada) em vez do ícone do tipo.
    bool containersComoPastas_ = false;
    std::unique_ptr<juce::ToggleButton> chkContainersComoPastas_;
    juce::Point<float> portaEntradaDoContainer(const LooseFileNode& lf) const;
    int containerSobre(juce::Point<float> canvas) const;
    void moverContainersDaPasta(const std::string& pastaId, int dx, int dy);
    void desconectarContainersSelecionados();
    void posicionarContainersNovos();
    int nodeDragIndice_ = -1;
    int looseFileDragIndice_ = -1;
    int nodeHoverDrop_ = -1;
    std::vector<std::string> sequenciaPreview_;
    std::vector<std::pair<size_t, juce::Point<int>>> soltosArrastoIniciais_;
    int ancoraTipo_ = 0;
    std::string ancoraId_;
    juce::Point<int> arrastoOffset_;
    // Item 10: quando o arrasto começa dentro de uma seleção múltipla, guarda
    // a posição (em canvas) de cada pasta selecionada no instante do clique,
    // pra aplicar o mesmo delta do nó primário (nodeDragIndice_) em todas —
    // sem isto só a pasta clicada se movia, o resto da seleção ficava parado.
    std::vector<std::pair<int, juce::Point<float>>> arrastoGrupoPosicoesIniciais_;
    // Auto-rolagem: arrastando pastas até a borda do canvas, a vista rola devagar e o bloco acompanha.
    struct TimerAutoRolagem : juce::Timer {
        std::function<void()> aoTick;
        void timerCallback() override { if (aoTick) aoTick(); }
    } autoRolagem_;
    // Dica no rodapé: o mouse parado mais de 1 s sobre uma pasta (sem clicar) mostra o nome dela no footer do app.
    struct TimerDicaPasta : juce::Timer {
        std::function<void()> aoTick;
        void timerCallback() override { stopTimer(); if (aoTick) aoTick(); }
    } dicaPasta_;
    std::string dicaPastaId_;  // pasta sob o mouse (candidata ou já mostrada); vazio = nenhuma
    void limparDicaPasta();
    void moverArrastoPara(juce::Point<int> mouseTela);
    void tickAutoRolagem();

    float zoom_ = 1.0f;
    juce::Point<float> panOffset_{0.0f, 0.0f};
    bool panning_ = false;
    juce::Point<int> panStart_;

    std::unique_ptr<juce::TextButton> btnCriarPasta_;
    std::unique_ptr<juce::TextButton> btnRenomearPasta_;
    std::unique_ptr<juce::TextButton> btnAutoArranjar_;
    std::unique_ptr<juce::TextButton> btnZoomIn_;
    std::unique_ptr<juce::TextButton> btnZoomOut_;
    std::unique_ptr<juce::TextButton> btnZoomFit_;
    std::unique_ptr<juce::TextButton> btnResetTamanhos_;

    // Pasta ingerida inteira: árvore PROPOSTA (card NEW FOLDERS na coluna esquerda).
    struct LinhaProposta {
        std::string raizId;
        int arquivos = 0;
        std::unique_ptr<juce::Label> rotulo;
        std::unique_ptr<juce::TextButton> usar, mesclar, descartar;
    };
    std::vector<LinhaProposta> propostas_;
    // As propostas moram numa janela flutuante sobre o stage (canto superior esquerdo do canvas): só existe quando há
    // ação necessária e o usuário pode recolher/expandir pelo cabeçalho.
    std::unique_ptr<PainelPropostas> painelPropostas_;
    bool painelPropostasRecolhido_ = false;
    size_t ultimoTotalPropostas_ = 0;
    void posicionarPainelPropostas();
    void usarTodasPropostas();
    void mesclarTodasPropostas();
    void mesclarProximaDaFila(std::vector<std::pair<std::string, juce::String>> fila);
    void confirmarDescarteTodasPropostas();
    void reconstruirPropostas();

    // Card FOLDER EXPLORER (coluna esquerda): só pastas do mapa ativo; FIND seleciona a pasta + descendentes no
    // stage e enquadra a vista nelas.
    std::unique_ptr<ItemPastaExplorador> raizExplorador_;
    std::unique_ptr<juce::TreeView> treeExplorador_;
    std::unique_ptr<juce::TextButton> btnFind_;
    std::string pastaExploradorId_;  // pasta selecionada no explorer
    bool reconstruindoExplorador_ = false;
    void ajustarVistaExplorador(const std::string& pastaId);
    void reconstruirExplorador();
    void localizarPasta(const std::string& pastaId);
    void mostrarMenuExplorador(const juce::MouseEvent& e);
    void iniciarMesclaPastas(const std::string& origemId, const std::string& alvoId);
    void mostrarDialogoMescla(const std::string& raizId, const juce::String& nome);
    void confirmarDescarteProposta(const std::string& raizId, int arquivos);

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

    // Coluna esquerda em cards (MAPA / PASTAS / VISUALIZAÇÃO / SEM PASTA), padrão do
    // INTAKE. O canvas ocupa o resto; tudo aqui é em coordenadas de TELA (do componente).
    ColunaCardsLayout coluna_;
    juce::Rectangle<int> semPastaBounds_;
    juce::Rectangle<int> detalheBounds_;  // lista da pasta selecionada, no espaço livre da coluna
    juce::Rectangle<int> zoomIndicadorBounds_;
    // Canvas (pan/zoom): à direita da coluna, abaixo da barra de abas. screenToCanvas /
    // canvasToScreen já descontam a origem dele.
    juce::Rectangle<int> areaCanvas() const;
    // Primeira abertura de um mapa: nós visíveis e centralizados na área útil do canvas.
    bool enquadrarPendente_ = true;
    std::string mapaEnquadradoId_;
    void tentarEnquadrar();
    bool aplicarFocoPendente();
    std::set<std::string> itensEmFoco_;
    bool focoPendente_ = false;

    // Auto-organização por pasta (menu de contexto da pasta).
    bool pastaPodeAutoOrganizar(const std::string& pastaId);
    void mostrarEditorAutoOrganizar(const std::string& pastaId, const juce::String& nomePasta, const juce::String& regraAtual);
    void aplicarAutoOrganizacaoNaPasta(const std::string& pastaId, const std::string& regraCsv);
    void desenharIndicadorZoom(juce::Graphics& g) const;
    void desenharCabecalhoNo(juce::Graphics& g, const FolderNode& node, juce::Rectangle<float> header) const;

    // SEM PASTA (NO FOLDER) — card fixo na coluna esquerda (fora do canvas); nunca
    // aparece com o ORIGINAL ativo (S4/13: "Não aparece no ORIGINAL").
    juce::Rectangle<int> boundsSemPasta() const;
    void desenharSemPasta(juce::Graphics& g) const;
    // Contagem do SEM PASTA: era um COUNT(*) no banco A CADA paint (o maior custo do Folder Map, medido com
    // `sample`). Guardada aqui; refeita no máximo 1x/s e zerada a cada recalcularNodes().
    mutable int contagemSemPasta_ = -1;
    mutable juce::uint32 contagemSemPastaMs_ = 0;
    mutable std::string contagemSemPastaMapa_;
    bool semPastaHover_ = false;

    // LIXEIRA (_trash) — card fixo no canto superior esquerdo do canvas (fora do zoom).
    juce::Rectangle<int> boundsPastaTrash() const;
    void desenharPastaTrash(juce::Graphics& g) const;
    void confirmarEApagarPastas(const std::vector<std::string>& pastaIds);
    void mostrarMenuArquivoDetalhe(const std::string& itemId);
    void mostrarMenuPastaTrash();
    std::string trashFolderId_;
    int trashContagem_ = 0;
    bool trashHover_ = false;

    // Slider de tamanho. Com folders selecionados age só neles (e PARA quando um encosta num vizinho);
    // sem seleção é a escala GLOBAL: cartões e espaços crescem juntos, então nada se sobrepõe.
    std::unique_ptr<juce::Slider> sliderTamanho_;
    float escalaTamanho_ = 1.0f;  // escala global (visual; não é persistida)
    // SPREAD: afasta as pastas só na horizontal (posições x em torno do pivot; tamanhos não mudam). Visual,
    // não persistida; persistirPosicoesEmLote desfaz o fator ao gravar.
    std::unique_ptr<juce::Slider> sliderSpread_, sliderSpreadV_;
    std::unique_ptr<juce::Label> rotuloSpread_, rotuloSpreadV_;
    float espalharX_ = 1.0f, espalharY_ = 1.0f;
    juce::Point<float> pivotEscala_{0.0f, 0.0f};
    double fatorCards_ = 0.0;  // fator do mapa escolhido pelo AJUSTAR; 0 = mapa antigo (tamanho único)
    std::map<std::string, float> escalasOriginal_;  // ORIGINAL é virtual: multiplicadores só em memória
    bool autoLayoutPendente_ = false;  // canvas ainda sem tamanho quando o mapa abriu: resized() refaz
    bool escalasSujas_ = false;
    std::string assinaturaSelecaoSlider_;
    void recalcularPivot();
    bool calcularEAplicarAutoArranjo();
    void sincronizarSlider();
    void aplicarSlider();
    void redefinirTamanhos();
    void persistirEscalas();
    void enquadrarTudo();
    void reunirArquivosSoltos();
    std::vector<int> indicesSelecionados() const;

    // cards_layout.json: posições persistidas para arquivos soltos no canvas
    juce::File arquivoLayout() const;
    std::map<std::string, juce::Point<int>> layoutSalvo_;
    void carregarLayoutSalvo();
    void gravarLayoutSalvo();
    std::string chaveLayout(const std::string& idNo) const { return mapaAtivoId_ + "|" + idNo; }
    void concluirArrastoDeArquivoSolto(juce::Point<float> canvasRelease);
    int pastaAlvoDoDrop(juce::Point<float> cursorCanvas, juce::Rectangle<int> iconeCanvas) const;

    void limparSelecaoTudo();
    std::vector<size_t> ordemDeLeituraSoltos() const;
    std::vector<size_t> ordemDeLeituraPastas() const;
    void selecionarIntervalo(int tipo, const std::string& deId, const std::string& ateId);
    std::vector<std::string> idsSoltosSelecionados() const;
    void mostrarMenuDeArquivosSoltos();
    void editarQuestionarioDoContainerSolto(const std::string& nestId, const std::string& origem);
    void adicionarSubmenuDePastas(juce::PopupMenu& menu, const std::string& paiId, const std::vector<std::string>& ids);
    void moverSoltosParaPasta(const std::vector<std::string>& ids, const std::string& pastaId);
    void mostrarNaOrigem(const std::string& itemId);
    void removerSoltosDoBackup(const std::vector<std::string>& ids);
    bool aplicarMovimentoDeSoltos(const std::vector<std::string>& ids, const std::string& pastaId);
    std::optional<std::string> soltoAdjacente(const std::string& idAtual, int direcao) const;
    void selecionarSolto(const std::string& id);
    void arranjarSoltosSelecionados();
    // S4/15 — pasta recém-criada, com destaque pulsante até o próximo clique
    std::string destaqueNovaPastaId_;

    bool sobreposto_ = false;           // esta instância é a do Folder Map sobreposto
    std::string raizSobreposicaoId_;    // raiz da sobreposição (pasta clicada)
    std::unique_ptr<juce::Component> sobreposicao_;  // a camada aberta por esta instância (no content da janela)

    std::unique_ptr<juce::TextEditor> inlineEditor_;
    std::string editingNodeId_;

    void recalcularNodes();
    void recalcularNodesInterno();
    void autoArranjar();
    // AJUSTAR com 2+ pastas selecionadas: reorganiza só elas (mesmos tamanhos), na região que já ocupavam.
    void autoArranjarSelecionadas(const std::vector<int>& selecionadas);

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
    // O pulso dura ~2 s e para antes se o componente for escondido (o timer era de 30 ms até o
    // próximo clique, repintando o mapa inteiro sem parar).
    void visibilityChanged() override;
    void pararPulso();
    juce::uint32 inicioPulsoMs_ = 0;
    static constexpr juce::uint32 kDuracaoPulsoMs = 2000;
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

    std::string socketDragParentId_;
    juce::Point<float> socketDragPos_;

    bool marqueeSelecting_ = false;
    juce::Point<float> marqueeStartCanvas_;
    juce::Rectangle<float> marqueeRectCanvas_;

    // Item 9 — C abre o color picker (mostrarSeletorDeCorPasta já guarda
    // aqui); C de novo com ele aberto fecha em vez de reabrir.
    juce::Component::SafePointer<juce::CallOutBox> corCallout_;

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

    // Importação do celular (Wi-Fi: fotos/vídeos ou conversas do WhatsApp)
    std::unique_ptr<juce::Button> btnReceberCelular_;
    std::unique_ptr<PainelOverlay> overlayAtual_;
    std::weak_ptr<PainelReceberCelular> painelCelularAtual_;
    std::weak_ptr<EscolhaImportacaoCelular> escolhaImportacaoAtual_;
    void abrirReceberDoCelular();
    void iniciarReceberDoCelular(ModoImportacaoCelular modo);

    // Visualização da conversa de WhatsApp dentro do Structure
    std::unique_ptr<ConversaWhatsappComponent> conversaWhatsapp_;
    std::unique_ptr<juce::TextButton> btnVoltarMapa_;

    // Janela flutuante da lixeira (_trash)
    std::unique_ptr<TrashWindow> janelaTrash_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArvoreBackupComponent)
};

} // namespace matriz::ui
