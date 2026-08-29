#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <memory>

#include "../App/Cancelamento.h"
#include "AcoesItem.h"
#include "../Vault/Reconciliacao.h"
#include "../Vault/AssetRelinkEngine.h"
#include "AudioWorkspace.h"
#include "OverlayComponent.h"
#include "ProjetoAberto.h"

// Workflow layer (Phase 1+2)
#include "HomePanelComponent.h"
#include "IngestWizardComponent.h"
#include "BarraNavegacaoComponent.h"
#include "BackupWorkspaceComponent.h"
#include "PreservationWorkspaceComponent.h"
#include "CatalogWorkspaceComponent.h"
#include "EstatisticasComponent.h"
#include "ArvoreBackupComponent.h"
#include "FloatingPreviewWindow.h"
#include "DuplicatesWorkspaceComponent.h"
#include "BarraProgressoGlobalComponent.h"

// Layout de três painéis (§11.1): mosaico | visualizador+transporte+tira |
// ficha. Sem abas, sem janela flutuante. Nesta etapa (B.1.1-B.1.3): mosaico
// funcional e ficha lateral genérica; visualizador/transporte/tira de
// diagnóstico são B.1.4/B.1.5 (fronteira de etapa, ver walkthrough).

namespace matriz::ui {

class MosaicoComponent;
class FichaPanelComponent;
class PainelInconsistenciasComponent;
class PreviewComponent;
class ArvoreComponent;
class FiltrosComponent;
class CartaoModo;
class LinhaProjetoRecente;
class BarraFerramentasComponent;
class TransportComponent;
class BarraAcoesFicha;
class CatalogHubComponent;
class CatalogoComponent;
class IntakeWorkspaceComponent;
struct EstadoLote;

// juce::DragAndDropContainer: precisa envolver tanto a origem (mosaico,
// arrastando itens selecionados) quanto o alvo (ArvoreComponent, soltando
// numa pasta do Acervo) — §5.2 "arrastar da Origem pro Acervo é o gesto
// principal de organização".
class MainComponent : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       public juce::DragAndDropContainer,
                       private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void atualizarTooltips();

    void abrirProjeto(std::unique_ptr<matriz::model::Project> projeto);
    void salvarProjeto();
    juce::File pastaProjeto() const;
    void fecharProjeto();

    // Abre uma pasta de backup como CATÁLOGO (item 11) — modo de consulta,
    // somente leitura, sem projeto nenhum por trás. false se a pasta não
    // tiver catálogo dentro.
    bool abrirCatalogo(const juce::File& pasta);
    bool temCatalogoAberto() const { return catalogo_ != nullptr; }
    CatalogoComponent* catalogoParaTeste() { return catalogo_.get(); }

    // Move o projeto atualmente aberto (se houver) pra fora deste
    // MainComponent — usado só pela troca de idioma em Preferences, que
    // destrói e recria o MainComponent inteiro pra retraduzir toda a UI, e
    // precisa entregar o projeto aberto pro substituto. nullptr se nenhum
    // projeto estava aberto. Nunca chamar com ingest em andamento.
    std::unique_ptr<matriz::model::Project> destacarProjeto();
    bool temProjetoAberto() const { return projetoAberto_ != nullptr; }
    ProjetoAberto* projetoAberto() { return projetoAberto_.get(); }

    // Introspecção pra teste (Parte 1 — painel de inconsistências fixo no
    // Catalog, §1.3/§3.5): definido em .cpp porque PainelInconsistenciasComponent
    // só está forward-declarado aqui.
    bool temPainelInconsistencias() const;
    int totalInconsistencias() const;

    void mostrarHome();
    void mostrarCatalogHub();
    void mostrarIntake();
    void mostrarCatalog();
    void mostrarGrid();
    void mostrarDuplicates();
    void mostrarAnalytics();
    void mostrarTree();
    void mostrarBackup();

    void abrirColecaoDoCatalogo(const juce::File& pastaColecao);
    void retornarAoCatalogo();

    // Overlay de diálogo interno (§3) — substitui os modais nativos, que
    // criavam peer próprio e crashavam sendo repintados durante a destruição.
    // Público porque quem monta um fluxo (ingest, vault, publicação) precisa
    // pedir uma resposta ao operador sem abrir janela nenhuma.
    PainelOverlay& overlay() { return overlay_; }

    // Introspecção pra teste (item 9 — resumo discreto de fim de lote):
    // texto atual do rótulo de progresso/resumo, "" se nenhum lote rodou
    // ainda nesta janela.
    juce::String textoProgressoIngestParaTeste() const;

    // Ingere arquivos (ou pastas, expandidas recursivamente) como itens
    // novos — sem diálogo, sem pergunta (Reorientação completa §2.1): cada
    // arquivo vira um item com tipo_midia ainda indefinido, aparece na
    // grade na hora, e miniatura/checksum/leitura técnica rodam depois em
    // background (nunca trava a UI). Ponte entre o motor de ingestão
    // (Etapa 2, headless) e a UI — chamada tanto pelo botão "Add material"
    // quanto por arrastar arquivos do Finder na janela inteira.
    void ingerirArquivos(const juce::Array<juce::File>& arquivosOuPastas);

    // Abre o navegador estilo Finder embutido (item 3) — segunda porta de
    // entrada, ao lado de arrastar do Finder. Público porque é uma ação de
    // UI real (botão da barra) e é por onde o harness exercita o navegador.
    void abrirNavegadorArquivos();

    // Enquanto um lote está sendo processado em background, fechar/trocar
    // de projeto destruiria o banco que o job em background ainda está
    // usando — o menu consulta isto pra desabilitar essas ações até terminar.
    // Inclui o FECHAMENTO do lote, não só os arquivos pendentes.
    //
    // A finalização (resumo, árvore, painel de inconsistências) roda no
    // timer, depois do último worker terminar. Enquanto ela não rodou, o
    // lote não acabou: liberar o menu aí deixaria fechar o projeto no meio
    // do fechamento — e fazia o resumo de fim de lote ser lido antes de
    // existir.
    bool ingestEmAndamento() const { return pendentes_->load() > 0 || loteEmCurso_; }

    // Item 10 — cancelar operação longa. Pede o cancelamento; os jobs ainda
    // enfileirados desistem antes de tocar em arquivo, e o que já foi
    // processado continua válido. Público porque é uma ação de UI de
    // verdade (o botão na barra de progresso) — e é por onde o self-test
    // headless exercita o cancelamento.
    void cancelarLoteIngest();

    void renomearItemSelecionado();
    void removerItemSelecionadoDoBackup();

    bool podeDesfazer() const;
    void executarUndo();

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    // juce::FileDragAndDropTarget — a janela inteira é o alvo (Parte 3 da
    // correção crítica: antes só o mosaico, uma coluna estreita, aceitava
    // drop; soltar em qualquer outro lugar da janela não fazia nada).
    bool isInterestedInFileDrag(const juce::StringArray&) override { return temProjetoAberto(); }
    void filesDropped(const juce::StringArray& files, int, int) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;

private:
    void selecionarItem(const std::string& itemId);
    // Item 6.2 — a barra do rodapé segue a seleção; sem seleção, resume a grade.
    void atualizarBarraMetricas();
    void reconstruirTelaInicial();
    void reconstruirLayoutProjeto();
    void reconstruirLayoutCatalogo(const juce::File& pasta);
    void mostrarIngestWizard();
    void mostrarPreservation();

private:
    std::vector<juce::File> expandirArquivos(const juce::Array<juce::File>& arquivosOuPastas) const;
    void expandirArquivosAsync(const juce::Array<juce::File>& arquivosOuPastas, std::function<void(std::vector<juce::File>)> aoConcluir);
    void processarLoteEmBackground(std::vector<juce::File> arquivos,
                                    const std::string& sourceMedia = {},
                                    const std::string& collection = {});
    void atualizarLabelProgresso();
    void garantirLabelProgressoIngest();
    // Resumo discreto de fim de lote (item 9, §5 — "HD reconectado... X
    // novos, Y já conhecidos"), no lugar da barra de progresso — nunca um
    // diálogo modal, nunca desaparece sozinho.
    void mostrarResumoLote(int novos, int duplicatas, int erros);

    // Fim de UMA unidade de trabalho do lote (um arquivo), já na thread de
    // mensagens. Caminho único: o arquivo processado e o arquivo pulado por
    // cancelamento passam os dois por aqui, senão a contagem de pendentes
    // nunca zeraria num lote cancelado e o lote ficaria "em andamento" pra
    // sempre — travando fechar projeto, trocar idioma e sair.
    void finalizarUnidadeDeLote(std::shared_ptr<EstadoLote> estadoLote,
                                 std::shared_ptr<std::atomic<int>> contadorParaAtualizar);
    // Chamado da THREAD DE TRABALHO ao fim de cada arquivo. Não toca no
    // Component — só no contador compartilhado.
    static void registrarUnidadeConcluida(const std::shared_ptr<std::atomic<int>>& pendentes);

    void mostrarResumoCancelado(int processados, int total);
    void alternarFicha();
    void abrirPreview(const std::string& itemId);
    void fecharPreview();
    // Estação de escuta (§7): o workspace de áudio ocupa o mesmo espaço
    // central da grade e do preview, nunca os três ao mesmo tempo.
    void abrirEscuta(const std::string& itemId);
    void fecharEscuta();

    // §8 — detecção de volume montado e reconciliação automática. O timer
    // só compara caminho/UUID (barato); a varredura de verdade roda no
    // ThreadPool.
    void timerCallback() override;
    void verificarVaultsConectados();
    void verificarPresencaInicialAssets();
    void mostrarDialogoRelinkInicial(const matriz::vault::AssetPresenceReport& report);
    void abrirDialogoRelinkOffline(const std::string& itemId);
    void atualizarCacheDeTamanhoTotal();
    void aoTerminarReavaliacaoDeVaults(const std::vector<std::string>& reconectados);
    void concluirReconciliacao(const matriz::vault::ResumoReconciliacao& resumo);
    // Atualiza contagem da barra de ferramentas e barra de seleção. Só lê
    // estado que o mosaico já tem em memória — barato, pode ser chamado a
    // cada tecla digitada na busca.
    void atualizarPainelDeApoio();

    // Reavalia em que ponto do fluxo o projeto está (faixa de orientação).
    // Consulta o banco (contagem por tipo de mídia + árvore do acervo),
    // então só é chamado quando o DADO pode ter mudado — ingest, edição em
    // lote — nunca a cada mudança de filtro ou de seleção, que só mexem em
    // como o mesmo dado está sendo exibido.
    void atualizarEtapaDoFluxo();

    // Menu de botão direito da grade (item 6): vale igual pra um arquivo ou
    // pra seleção inteira. As ações em si moram em AcoesItem.h, pra o painel
    // direito poder oferecer exatamente as mesmas.
    void abrirMenuContextoItens(std::vector<std::string> itemIds);

    // O que a UI faz depois de uma ação (recarregar, abrir detalhes,
    // filtrar). Um único lugar, usado tanto pelo menu de contexto quanto
    // pelos botões do painel direito — se divergissem, a mesma ação
    // deixaria a tela em estados diferentes conforme por onde foi chamada.
    matriz::ui::acoes::Ganchos ganchosDeAcao();
    enum class PainelAtivo { Source, BackupTree };
    void ativarPainel(PainelAtivo painel);

    enum class TelaAtiva { Inicial, Home, Catalog, Ingest, Backup, Preservation };
    TelaAtiva telaAtiva_ = TelaAtiva::Inicial;

    std::unique_ptr<ProjetoAberto> projetoAberto_;
    juce::File catalogoPai_;
    bool arrastandoArquivo_ = false;

    std::unique_ptr<juce::Label> telaInicialTitulo_;
    std::unique_ptr<juce::Label> telaInicialSubtitulo_;
    std::unique_ptr<CartaoModo> telaInicialCartaoArchive_;
    std::unique_ptr<CartaoModo> telaInicialCartaoCatalog_;
    std::unique_ptr<juce::TextButton> telaInicialBotaoAbrir_;
    std::unique_ptr<juce::Label> telaInicialRecentesTitulo_;
    std::vector<std::unique_ptr<LinhaProjetoRecente>> telaInicialLinhasRecentes_;

    PainelAtivo painelAtivo_ = PainelAtivo::Source;
    std::unique_ptr<juce::Label> labelSource_;
    std::unique_ptr<juce::TextButton> botaoSourceLista_;
    std::unique_ptr<juce::TextButton> botaoSourceIcones_;
    std::unique_ptr<juce::Label> labelBackupTree_;
    std::unique_ptr<juce::TextButton> botaoBackupLista_;
    std::unique_ptr<juce::TextButton> botaoBackupIcones_;
    std::unique_ptr<juce::TextButton> botaoNovaPastaAcervo_;
    std::unique_ptr<juce::Viewport> arvoreOrigemViewport_;
    std::unique_ptr<ArvoreComponent> arvoreOrigem_;
    std::unique_ptr<juce::Viewport> arvoreAcervoViewport_;
    std::unique_ptr<ArvoreComponent> arvoreAcervo_;
    std::unique_ptr<juce::Component> divisor1_;
    std::unique_ptr<juce::Component> divisor2_;
    std::unique_ptr<juce::Component> divisorFicha_;
    float propPainel1_ = 0.25f;
    float propPainel2_ = 0.25f;
    float propFichaBackup_ = 0.40f;

    // Filtros (chips) + busca + coleções inteligentes (Acréscimos §10) —
    // embaixo da árvore, na mesma coluna esquerda (§9.1). Altura fixa,
    // rolável por conta própria se o conteúdo (muitos chips/coleções)
    // passar do espaço reservado.
    std::unique_ptr<juce::Viewport> filtrosViewport_;
    std::unique_ptr<FiltrosComponent> filtros_;

    // Layout de projeto aberto (Reorientação completa §3.1): a grade (ou o
    // preview, que alterna no mesmo espaço) é o protagonista — ocupa o
    // essencial da janela. A ficha é lateral, colapsada por padrão (§7.1 —
    // "a ficha nunca bloqueia").
    std::unique_ptr<juce::Viewport> mosaicoViewport_;
    std::unique_ptr<MosaicoComponent> mosaico_;
    std::unique_ptr<PreviewComponent> preview_; // só existe enquanto um item está em preview (§3.4)
    // Criado sob demanda: abrir uma placa de som e alocar o buffer de
    // reprodução num projeto que nunca vai tocar áudio seria custo puro.
    std::unique_ptr<AudioWorkspace> escuta_;
    std::string itemEmEscuta_;
    bool reconciliacaoEmAndamento_ = false;
    // Throttle das atualizações de painel durante um lote (ver
    // finalizarUnidadeDeLote).
    juce::uint32 ultimaAtualizacaoDeLoteMs_ = 0;
    std::string itemEmPreview_;

    std::unique_ptr<FichaPanelComponent> fichaPanel_;
    // Faixa de ações no topo do painel direito — categorizar, renomear,
    // enviar pra pasta, tirar do backup, ver prévia (item 5).
    std::unique_ptr<BarraAcoesFicha> barraAcoesFicha_;
    bool fichaAberta_ = false;

    // Barras de apoio (correção de usabilidade): as ações reais do fluxo
    // deixam de morar só no menu do sistema, o app passa a dizer sozinho
    // qual é o próximo passo, e a seleção anuncia a edição em lote — que
    // existia mas era invisível dentro da ficha fechada.
    std::unique_ptr<BarraFerramentasComponent> barraFerramentas_;
    std::unique_ptr<TransportComponent> transport_;
    // Janela do navegador (item 3) — só uma por vez; abrir de novo reusa.
    std::unique_ptr<juce::DocumentWindow> janelaNavegador_;
    // Lembra a última localização visitada, por projeto (item 3.2).
    juce::File ultimaLocalizacaoNavegador_;
    // Tooltips só aparecem se existir um TooltipWindow vivo na hierarquia.
    std::unique_ptr<juce::TooltipWindow> tooltips_;

    // Modo catálogo (item 11): terceira tela possível, ao lado da inicial e
    // da de projeto aberto. Consulta pura — nenhum caminho daqui escreve.
    // Membro por valor, não unique_ptr: o overlay vive tanto quanto a janela
    // e é destruído com ela, na ordem certa — nada de um diálogo sobrevivendo
    // ao dono, que era metade do problema do AlertWindow.
    PainelOverlay overlay_;

    std::unique_ptr<CatalogoComponent> catalogo_;
    std::unique_ptr<juce::Viewport> catalogoViewport_;
    std::unique_ptr<juce::Label> catalogoTitulo_;
    std::unique_ptr<juce::TextEditor> catalogoBusca_;
    std::unique_ptr<juce::TextButton> catalogoFechar_;

    // Ingest em background (corrige o travamento reportado: cópia + checksum
    // + ffprobe/Exiv2 nunca rodam na thread de mensagens).
    // Ingest concorrente (critério 3). Era 1 thread — sequencial. Com a
    // análise separada da gravação (IngestArquivo.h), a escrita continua
    // serializada por mutex e o trabalho caro passa a ser de fato paralelo.
    //
    // Duas escolhas que a medição impôs:
    //
    //  - METADE dos núcleos, não todos menos um. Com 7 workers num Mac de 8
    //    núcleos, a message thread simplesmente não era escalonada: um único
    //    callback chegou a 18 s de relógio sem gastar 18 s de CPU. Ingest
    //    rápido com a janela travada não serve pra nada.
    //  - Prioridade BAIXA. Ingest é trabalho de fundo por definição; quando
    //    disputa CPU com o operador clicando, quem tem que ganhar é o
    //    operador. É isto que segura o critério 1 durante um lote grande.
    juce::ThreadPool ingestPool_{
        juce::ThreadPoolOptions{}
            .withThreadName("MatrizIngest")
            .withNumberOfThreads(juce::jlimit(2, 6, juce::SystemStats::getNumCpus() / 2))
            .withDesiredThreadPriority(juce::Thread::Priority::low)};
    // Barra de progresso de verdade (com preenchimento proporcional), não
    // mais um rótulo de texto: em lote de milhares de arquivos, "1287/9500"
    // sozinho não dá noção de quanto falta.
    juce::String textoProgressoIngest_;
    matriz::app::CancelamentoPtr cancelamentoLote_;
    // shared_ptr e não membro atômico simples: os jobs do ThreadPool
    // decrementam isto sem tocar no MainComponent. Ler um Component (mesmo
    // via SafePointer) de fora da message thread é corrida — o contador
    // compartilhado evita a questão inteira.
    std::shared_ptr<std::atomic<int>> pendentes_ = std::make_shared<std::atomic<int>>(0);
    std::atomic<int> ingestsTotalLote_{0};

    // Estado do lote em curso, pro timer conseguir fechá-lo sem que cada
    // arquivo poste um callback.
    std::shared_ptr<EstadoLote> estadoLoteAtual_;
    bool loteEmCurso_ = false;
    int ticksDoTimer_ = 0;
    juce::int64 tamanhoTotalEmCache_ = 0;
    bool calculandoTamanhoTotal_ = false;

    // Pool própria pros Vaults: a de ingest fica saturada durante um lote, e
    // uma varredura de volume atrás de 5.000 arquivos nunca rodaria.
    juce::ThreadPool poolVaults_{juce::ThreadPoolOptions{}.withThreadName("MatrizVaults")
                                     .withNumberOfThreads(1)
                                     .withDesiredThreadPriority(juce::Thread::Priority::low)};
    std::atomic<int> ingestsErrosLote_{0};

    // Workflow layer (5 Primary Sibling Workspaces)
    std::unique_ptr<HomePanelComponent> homePanel_;
    std::unique_ptr<CatalogHubComponent> catalogHubWorkspace_;
    std::unique_ptr<IngestWizardComponent> ingestWizard_;
    std::unique_ptr<BarraNavegacaoComponent> barraNavegacao_;
    std::unique_ptr<IntakeWorkspaceComponent> intakeWorkspace_; // INTAKE (Quarentena)
    std::unique_ptr<CatalogWorkspaceComponent> catalogWorkspace_; // GRID
    std::unique_ptr<DuplicatesWorkspaceComponent> duplicatesWorkspace_; // DUPLICATES
    std::unique_ptr<EstatisticasComponent> analyticsWorkspace_;   // ANALYTICS (Statistics + Preservation)
    std::unique_ptr<ArvoreBackupComponent> treeWorkspace_;        // TREE (n8n Node Graph Editor)
    std::unique_ptr<BackupWorkspaceComponent> backupWorkspace_;   // BACKUP
    std::unique_ptr<PreservationWorkspaceComponent> preservationWorkspace_;
    std::unique_ptr<FloatingPreviewWindow> activePreviewWindow_;
    std::unique_ptr<BarraProgressoGlobalComponent> barraProgressoGlobal_;
    bool mostrarEstruturaOrigem_ = false;
    bool mostrarEstruturaBackup_ = false;

public:
    // Modo já decidido (o cartão clicado) — o diálogo de novo projeto só
    // pergunta o que é comum aos dois modos (nome, pasta...).
    std::function<void(matriz::model::Modo)> aoPedirNovoProjeto;
    std::function<void()> aoPedirAbrirProjeto;

    // Abre diretamente uma pasta de projeto já conhecida (clique num item
    // da lista de recentes), sem passar pelo FileChooser.
    std::function<void(juce::File)> aoAbrirRecente;

    // Estado vazio da grade é clicável (§3.2) — abre o mesmo seletor de
    // arquivos do menu Arquivo > Ingerir arquivos, já ligado a
    // ingerirArquivos() por quem instancia a janela.
    std::function<void()> aoPedirIngerirArquivos;
    std::function<void()> aoSalvarComo;

    // "Fazer backup" da barra de ferramentas — mesma ação do menu
    // Projeto > Fazer backup organizado, agora também visível na tela.
    std::function<void()> aoPedirBackup;

    // Disparado sempre que o estado do projeto/catálogo muda (aberto, fechado, trocado)
    // para que a barra de menus do macOS/desktop atualize itens habilitados e títulos.
    std::function<void()> aoMudarEstadoProjeto;

    // Se definido, substitui o AlertWindow padrão de resumo ao final de um
    // lote de ingest — único jeito de rodar o fluxo real em
    // --selftest-ingerir-arquivos, que não tem humano pra clicar OK num
    // modal (AlertWindow::showAsync com callback nulo roda um loop modal
    // síncrono nesta versão do JUCE, e travaria o self-test pra sempre).
    // Em produção fica vazio; o alerta normal continua aparecendo.
    std::function<void(int sucessos, juce::StringArray erros)> aoConcluirLoteIngestParaTeste;
};

} // namespace matriz::ui
