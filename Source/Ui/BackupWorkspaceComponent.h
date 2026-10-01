#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <set>

#include "ProjetoAberto.h"
#include "../Consolidacao/Consolidacao.h"
#include "../Consolidacao/BackupScanEngine.h"
#include "HierarquiaEditorComponent.h"
#include "../App/Cancelamento.h"
#include "OverlayComponent.h"
#include "BackupScanProgressDialog.h"
#include "EventBus.h"
#include "MatrizLookAndFeel.h"
#include "Tokens.h"

namespace matriz::ui {

// O dropdown do EXPORT fica no rodapé: o menu tem que abrir sempre pra cima.
class ComboAbrePraCimaLookAndFeel : public MatrizLookAndFeel {
public:
    ComboAbrePraCimaLookAndFeel() { configurarLookAndFeel(*this); }
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override {
        // Sem withInitiallySelectedItem/withItemThatMustBeVisible (o padrão do JUCE): eles alinham o
        // item marcado em cima do combo e o menu descia a partir dele, cortado na base da tela.
        return juce::PopupMenu::Options()
            .withTargetComponent(&box)
            .withMinimumWidth(box.getWidth())
            .withMaximumNumColumns(1)
            .withStandardItemHeight(label.getHeight())
            .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards);
    }
};

class BackupWorkspaceComponent : public juce::Component,
                                 public EventBusListener,
                                 private juce::ListBoxModel {
public:
    enum class Estado {
        Config,
        Running,
        Done
    };

    BackupWorkspaceComponent(ProjetoAberto& projeto, const std::set<std::string>& selectedItemIds);
    ~BackupWorkspaceComponent() override;

    std::function<void()> aoConcluir;
    std::function<void()> aoVoltarHome;
    std::function<void(const juce::File&)> aoAbrirCatalogo;
    std::function<void()> aoPedirIrParaDuplicatas;
    std::function<void(const std::set<std::string>&)> aoAbrirNoGrid;
    // Consulta a seleção atual do grid de metadados (item 4) — usada para
    // popular "Selected Files" sem depender do snapshot passado no construtor,
    // já que o componente agora persiste entre visitas à aba (item 3).
    std::function<std::set<std::string>()> obterSelecaoAtualDoGrid;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& tecla) override;
    void recarregar();
    void aoItemAlterado(const EventoItemAlterado& e) override;
    // Escondida (outra aba na frente): eventos só marcam "precisa atualizar"; a tela
    // atualiza ao voltar a aparecer (e, se quem apareceu foi um ancestral, no 1º paint).
    void visibilityChanged() override;

    // Chamado pelo MainComponent toda vez que a aba Backup é reaberta, para
    // manter "Selected Files" em dia com o grid sem resetar o restante do
    // setup (item 3 e item 4).
    // recalcular=false: o chamador vai chamar recarregar() logo em seguida (que
    // já recalcula a prévia) — evita planejar o catálogo inteiro duas vezes.
    void atualizarSelecaoDoGridSeNecessario(bool recalcular = true);

private:
    friend int rodarLoteSelfTest();  // --selftest-lote (LoteSelfTest.cpp): travas da etapa 5
    class PreviaLista;

    // ListBoxModel methods for Vaults list
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int rowNumber, const juce::MouseEvent&) override;
    juce::String getTooltipForRow(int rowNumber) override;

    bool escondido() const { return getPeer() != nullptr && !isShowing(); }
    bool botoesPendentes_ = false;       // evento chegou escondida: refazer os botões das listas
    bool resumoDeTituloPendente_ = false;  // título mudou escondida: refazer prévia/plano
    bool aplicacaoPendenteAgendada_ = false;
    void agendarAplicarPendentes();
    void atualizarResumo();
    // Resultado do planejarConsolidacao calculado em background (já filtrado
    // pela seleção) — aplicado na message thread por aplicarPlanoCalculado().
    struct ResultadoPlano {
        matriz::consolidacao::PlanoConsolidacao plano;
        juce::int64 espacoACopiar = 0;
        juce::int64 tamanhoTotal = 0;
        size_t totalIdsSelecionados = 0;
        bool erro = false;
        juce::String mensagemErro;
    };
    void aplicarPlanoCalculado(ResultadoPlano resultado, bool forcarRebackup);
    // Plano atual (plano_) válido e botão de backup liberável. Falso entre o
    // início do cálculo em background e a chegada do resultado.
    bool planoPronto_ = true;
    void concluirPlano();
    // Roda `fn` já se o plano está pronto; senão, quando ele chegar.
    void aoPlanoPronto(std::function<void()> fn);
    std::vector<std::function<void()>> aoPlanoPronto_;
    void continuarScanDestino();
    // Marcar CONSERVAR ESTRUTURA ORIGINAL precisa do plano novo pra decidir se
    // mostra o popup de conflito: o aviso espera o resultado chegar.
    bool popupConflitoAposPlano_ = false;
    bool scanAposPlano_ = false;  // scan do destino já agendado pra quando o plano chegar
    // Contador de geração (padrão de carregarColecoesBackupCatalogo): resposta
    // atrasada de um cálculo já superado é descartada. Atômico + compartilhado
    // porque o job também lê (cancela antes de começar se já foi superado).
    juce::ThreadPool poolPlano_{1};
    std::shared_ptr<std::atomic<int>> geracaoPlano_ = std::make_shared<std::atomic<int>>(0);
    // Prévia calculada UMA vez por visita: dentro de recarregar() (e do
    // construtor) as chamadas a atualizarResumo() só marcam pendente.
    int resumoAdiado_ = 0;
    bool resumoPendente_ = false;
    struct AdiaResumo {
        explicit AdiaResumo(BackupWorkspaceComponent& o) : o_(o) { ++o_.resumoAdiado_; }
        ~AdiaResumo() {
            if (--o_.resumoAdiado_ == 0 && o_.resumoPendente_) {
                o_.resumoPendente_ = false;
                o_.atualizarResumo();
            }
        }
        BackupWorkspaceComponent& o_;
    };
    void iniciarBackup();
    void dispararScanDestino(bool forcado = false);
    void executarBackupAcao(bool forcarOverride);
    void iniciarSyncComOutroDestino();
    void carregarDestinoAtivoInicial();

    ProjetoAberto& projeto_;
    std::set<std::string> selectedItemIds_;

    Estado estado_ = Estado::Config;

    // Selection criteria
    enum class WhatOption {
        Everything,
        Intake,
        SelectedAssets,
        NeedsBackup,
        Collection
    };
    WhatOption whatOption_ = WhatOption::Everything;
    struct OpcaoContent {
        std::string chave;
        juce::String rotulo;
        int contagem = 0;
    };
    std::vector<OpcaoContent> opcoesContent_;
    int selectedContentIdx_ = 0;
    void carregarOpcoesContent();
    std::vector<ProjetoAberto::ColecaoDisponivel> colecoes_;
    int selectedCollectionIdx_ = 0;

    // Target Selection
    struct DestinoBackupItem {
        std::string id;
        juce::String rotulo;
        juce::String caminho;
        std::string papel;
        bool online = false;
        bool ativo = false;
    };
    std::vector<DestinoBackupItem> destinosBackup_;
    int selectedDestinoIdx_ = -1;
    // Modelo SOURCE/MAIN/CLONE (etapa 4): backup só vai pro MAIN (a pasta do
    // projeto). Outros destinos só como CLONE ou EXPORT.
    bool destacadoEhMain() const {
        return selectedDestinoIdx_ >= 0 && selectedDestinoIdx_ < static_cast<int>(destinosBackup_.size()) &&
               destinosBackup_[static_cast<size_t>(selectedDestinoIdx_)].papel == "ORIGINAL";
    }
    bool mainSelado_ = false;  // o MAIN já recebeu o primeiro backup (FAZER BACKUP -> ADICIONAR AO MAIN)
    // Etapa 5: estrutura de pastas e nomes são escolhidos no primeiro backup
    // e ficam travados depois (projeto.backup_config_main). Projeto antigo já
    // com MAIN mas sem config gravada: trava a partir do próximo backup.
    bool configTravada_ = false;
    bool organizarPorSource_ = false;  // pasta raiz/sufixo por SOURCE (só MAIN criado a partir da etapa 5)
    void atualizarTravasDoMain();
    // Nada sai do projeto antes do FAZER BACKUP criar o MAIN (modo Coleção):
    // PUBLISH TO HTML, EXPORTAR..., EXPORTAR PLANILHA e BACKUP SYNC... só
    // liberam com mainSelado_. Único ponto de liberação/dica desses 4 —
    // chamado pelo layout e pela atualização do resumo.
    void atualizarBotoesDependentesDoMain();
    bool saidaBloqueadaSemMain() const;
    void gravarConfigDoMain();
    // Escolhas da seção ESTRUTURA DE PASTAS gravadas na hora (projeto.backup_config_rascunho)
    // e reaplicadas ao recriar a janela; só valem antes do 1º backup (!configTravada_).
    void gravarRascunhoOrganizacao();
    void aplicarRascunhoOrganizacao();
    bool aplicandoRascunho_ = false;
    void perguntarSincronizarClones();
    // DONE: sai da tela de conclusão e volta à configuração (estado_ nunca voltava de Done).
    void voltarParaConfiguracao();
    std::string destinoIdDoMain();
    juce::ThreadPool poolSyncClones_{1};
    // EXPORT (etapa 6)
    std::unique_ptr<juce::TextButton> btnExportar_;
    juce::ThreadPool poolExport_{1};
    bool exportando_ = false;
    std::shared_ptr<std::atomic<bool>> cancelarExport_ = std::make_shared<std::atomic<bool>>(false);
    // Sidecars XMP (etapa 8): no lugar do embed depois que o MAIN existe.
    std::unique_ptr<juce::TextButton> btnAtualizarSidecars_;
    void atualizarSidecars(bool sobrescreverEditados, std::vector<juce::String> importar = {});
    void abrirExport();
    // Pacote de collection (EXPORT -> INTAKE de outra collection): seleção +
    // folder map + matriz-pacote.json, sempre do MAIN, em background.
    void abrirPacote();
    void iniciarPacote(const juce::File& destino, const juce::String& nome, const std::string& mapaId,
                       const juce::String& nomeMapa);
    void iniciarExport(const juce::File& destino, const matriz::consolidacao::HierarquiaBackup& hierarquia,
                       matriz::consolidacao::ModoPrefixoArquivo modo, const juce::String& prefixo, bool embutir,
                       bool marcaDagua, const std::string& mapaId = {});
    juce::File customDestFolder_;
    juce::File resolvedDestFolder_;

    void carregarDestinosBackup();
    // isDirectory() de cada destino (volume de rede offline trava a message
    // thread) roda em background; geracaoDestinos_ descarta resposta superada.
    juce::ThreadPool poolDestinos_{1};
    int geracaoDestinos_ = 0;
    void adicionarOuAtivarDestino(const juce::File& pasta, const juce::String& rotuloSugerido);
    void criarNovoClone(const juce::File& folder);
    void desvincularDestino(const DestinoBackupItem& dest);

    // Scan & Integrity State
    matriz::consolidacao::ResultadoScanBackup scanResult_;
    bool scanRealizado_ = false;

    // Plan & Execution
    matriz::consolidacao::PlanoConsolidacao plano_;
    matriz::app::CancelamentoPtr cancelamento_ = std::make_shared<matriz::app::Cancelamento>();
    bool executando_ = false;
    double progressoValor_ = 0.0;

    // Done stats
    int copiadoCount_ = 0;
    int verificadoCount_ = 0;
    int falhasCount_ = 0;
    std::vector<juce::String> falhasLista_;

    // UI elements — ALL visible at once in Config state
    std::unique_ptr<juce::Label> labelTitulo_;

    void registrarDestinoBackup(const juce::File& destFolder, const juce::String& rotuloSugerido,
                                int copiado, int pulados, int falhas, bool cancelado);

    // === CONFIG CONTAINER & VIEWPORT ===
    class ConfigContainerComponent;
    std::unique_ptr<juce::Viewport> configViewport_;
    std::unique_ptr<ConfigContainerComponent> configContainer_;
    juce::Rectangle<int> cartaoPrevia_;

    struct CatalogBackupItem {
        juce::String name;
        juce::String path;
        juce::int64 sizeBytes = 0;
        uint64_t totalAssets = 0;
        uint64_t backedUpAssets = 0;
        uint64_t missingAssets = 0;
        uint64_t needsAttention = 0;
        juce::String status = "READY";
    };

    class CatalogBackupContainerComponent;
    std::unique_ptr<juce::Viewport> catalogBackupViewport_;
    std::unique_ptr<CatalogBackupContainerComponent> catalogBackupContainer_;
    std::vector<CatalogBackupItem> catalogBackupItems_;
    CatalogBackupItem catalogBackupTotal_;
    // item 3 (correção METADATA — lentidão remanescente): antes rodava
    // síncrono na message thread, abrindo um banco SQLite por coleção
    // vinculada só pra contar itens — travava a UI a cada recarregar(),
    // inclusive depois de REMOVE FROM THIS LIST. Agora roda em background;
    // geracaoCatalogoBackup_ descarta resposta atrasada de uma recarga já
    // superada por uma mais nova.
    juce::ThreadPool poolCatalogoBackup_{1};
    int geracaoCatalogoBackup_ = 0;

    void carregarColecoesBackupCatalogo();

    void abrirJanelaSelecionarArquivos();
    juce::String rotuloOpcaoSelecionados() const;

    // === SOURCE section ===
    std::unique_ptr<juce::Label> labelSource_;
    std::unique_ptr<juce::ComboBox> comboSource_;
    std::unique_ptr<juce::TextButton> btnEditarSelecao_;
    std::unique_ptr<juce::ComboBox> comboColecoes_;

    // === DESTINATION section ===
    std::unique_ptr<juce::Label> labelDest_;
    std::unique_ptr<juce::ListBox> listVaults_;
    std::unique_ptr<juce::TextButton> btnBrowseVault_;
    std::unique_ptr<juce::Button> btnGoogleDriveDest_; // Export to Google Drive (local mount)
    std::unique_ptr<juce::Label> labelDestInfo_;
    bool googleDriveComoDestino_ = false;
    juce::File pastaGoogleDrive_;  // resolved GD mount, if selected


    // === ORGANIZATION section ===
    std::unique_ptr<juce::Label> labelOrg_;
    std::unique_ptr<juce::ComboBox> comboOrg_;
    std::unique_ptr<juce::TextButton> btnEditarHierarquia_;
    matriz::consolidacao::HierarquiaBackup hierarquiaCustom_;
    std::unique_ptr<juce::ToggleButton> togglePreservarEstrutura_;
    std::unique_ptr<juce::ToggleButton> toggleUsarEstruturaMapa_;
    // Fase 2: dropdown de mapas do USUÁRIO (sem o ORIGINAL). No 1º backup o
    // escolhido vira o mapa do MAIN (backup_config_main.mapa_id); depois trava.
    std::unique_ptr<juce::ComboBox> comboMapaMain_;
    std::vector<std::string> idsComboMapaMain_;  // id do ComboBox (i+1) -> folder_map.id
    void recarregarComboMapaMain();
    bool hierarquiaUsaMapa() const;
    std::string mapaParaBackup() const;  // "" quando a estrutura escolhida não usa mapa
    // Fase 2: oferta de mover itens de _SEM_PASTA (backup seguinte).
    bool movimentosDecididos_ = false;
    bool moverSemPasta_ = false;
    void perguntarMoverSemPasta();
    std::unique_ptr<juce::Label> labelPrefixo_;
    std::unique_ptr<juce::ComboBox> comboModoPrefixo_;
    std::unique_ptr<juce::TextEditor> editPrefixo_;
    matriz::consolidacao::ModoPrefixoArquivo modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Nenhum;
    juce::String prefixoAuto_ = "BKR";
    juce::String prefixoCustomizado_ = "BKR";

    // === OPTIONS section ===
    std::unique_ptr<juce::Label> labelOpcoes_;
    std::unique_ptr<juce::ToggleButton> toggleGerarCatalogo_;
    std::unique_ptr<juce::ToggleButton> toggleEmbutirMetadados_;
    std::unique_ptr<juce::ToggleButton> toggleVerificarChecksum_;
    std::unique_ptr<juce::ToggleButton> toggleAutoResolverConflitos_;
    std::unique_ptr<juce::ToggleButton> toggleForcarRebackup_;

    // === PREVIEW section ===
    class LegendaStatusComponent;
    std::unique_ptr<juce::Label> labelResumo_;
    std::unique_ptr<LegendaStatusComponent> barraLegenda_;
    std::unique_ptr<juce::Viewport> listPreviaViewport_;
    std::unique_ptr<PreviaLista> listPrevia_;

    // === PROGRESS ===
    std::unique_ptr<juce::ProgressBar> barraProgresso_;
    std::unique_ptr<juce::Label> labelProgressoStatus_;

    // === BUTTONS ===
    std::unique_ptr<juce::TextButton> btnStartBackup_;
    std::unique_ptr<juce::TextButton> btnSyncDestino_;
    std::unique_ptr<juce::TextButton> btnPublishHtml_;
    std::unique_ptr<juce::TextButton> btnExportZip_;
    std::unique_ptr<juce::TextButton> btnLimparZip_;
    std::unique_ptr<juce::TextButton> btnSendToPrint_;
    std::unique_ptr<juce::TextButton> btnLimparPrint_;
    std::unique_ptr<juce::TextButton> btnExportWatermark_;
    std::unique_ptr<juce::TextButton> btnLimparWatermark_;
    std::unique_ptr<juce::TextButton> btnCancelarExecucao_;
    std::unique_ptr<juce::TextButton> btnDone_;
    std::unique_ptr<juce::TextButton> btnOpenCatalog_;
    std::unique_ptr<juce::TextButton> btnExportXls_;
    std::unique_ptr<juce::TextButton> btnExportCsv_;
    std::unique_ptr<juce::TextButton> btnExportDublinCore_;
    std::unique_ptr<juce::TextButton> btnExportChecksums_;
    std::unique_ptr<juce::TextButton> btnExportJanela_;
    // EXPORT unificado (Fase 6): um botão + dropdown de origem no lugar dos 4 botões de saída.
    // Os botões antigos continuam existindo (escondidos): cada opção dispara o onClick do antigo,
    // então a ação e o diálogo são exatamente os de antes.
    enum ExportOrigemId { kExpSelecionados = 1, kExpZip, kExpPrint, kExpWatermark, kExpPlanilha, kExpPacote };
    ComboAbrePraCimaLookAndFeel comboExportLf_;  // antes do combo: o combo o usa até ser destruído
    std::unique_ptr<juce::ComboBox> comboExportOrigem_;
    std::unique_ptr<juce::TextButton> btnExportUnificado_;
    int contagemSelecionados_ = 0;
    void atualizarExportUnificado();
    void executarExportUnificado();
    juce::File arquivoExportOrigem() const;

    void mostrarJanelaExportar();
    void exportarXls();
    void exportarCsv();
    void exportarDublinCore();
    void exportarChecksums();
    void publicarHtml();
    void atualizarBotoesListas();

    // Auto-export to a specific folder (no FileChooser dialog)
    void exportarCsvPara(const juce::File& destFolder);
    void exportarXlsPara(const juce::File& destFolder);
    void exportarDublinCorePara(const juce::File& destFolder);
    void exportarChecksumsPara(const juce::File& destFolder);
    juce::String gerarManifestChecksumsBackup(const std::function<void(int, int)>& onProgress = nullptr);

    // Helpers
    std::set<std::string> obterItensSelecionadosPeloCriterio();
    void aplicarEstiloBotao(juce::TextButton& botao, bool primario);

    // Config e progresso são telas diferentes no mesmo Component. Sem esconder
    // uma ao mostrar a outra, os controles antigos continuam com os bounds da
    // passada anterior e a barra de progresso é desenhada por cima deles.
    void mostrarControlesConfig(bool mostrar);

    // Retângulos calculados em resized() e pintados em paint(): os cartões que
    // agrupam cada seção. Guardados para as duas funções não recalcularem
    // geometria em duplicata e sairem de sincronia.
    std::vector<juce::Rectangle<int>> cartoes_;
    juce::Rectangle<int> cartaoCentral_;
    juce::Rectangle<int> faixaCabecalho_;
    juce::Rectangle<int> faixaRodape_;

    void mostrarPopupConflitoPreservacao();
    PainelOverlay overlay_;
};

} // namespace matriz::ui
