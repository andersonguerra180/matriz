#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <set>
#include <vector>
#include <string>
#include <optional>

#include "ColunaCardsLayout.h"
#include "PainelListaEscolha.h"

namespace matriz::ui {

class ProjetoAberto;

class DuplicatesWorkspaceComponent : public juce::Component,
                                      public juce::Thread,
                                      private juce::Timer {
public:
    friend int rodarLoteSelfTest();  // --selftest-lote: critérios rápidos (etapa 9)
    explicit DuplicatesWorkspaceComponent(ProjetoAberto& projeto);
    ~DuplicatesWorkspaceComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void recarregar();
    void iniciarScan();

    // Seleção atual do grid (para o escopo "Selected files"). Quem hospeda liga ao grid de verdade (aba METADATA).
    std::function<std::set<std::string>()> fornecerSelecaoDoGrid;
    // "SELECTED FILES (N)" no escopo: relê quantos itens estão selecionados no grid agora.
    void atualizarContagemSelecionados();
    // Fim de uma verificação (message thread): quantos grupos achou (0 = nenhuma). Alimenta o selo do Find duplicates.
    std::function<void(int grupos)> aoConcluirScan;

private:
    static constexpr int kAlturaResumo = 28;
    // Thread method for background scanning
    void run() override;

    // Timer callback to update progress bar and UI state
    void timerCallback() override;

    struct DuplicateMatch {
        int ordemChegada = 0;
        int totalRelacionados = 0;
        std::string criadoEm;
        std::string itemId;
        std::string codigoAcervo;
        std::string titulo;
        std::string ext;
        double duracao = 0.0;
        int largura = 0;
        int altura = 0;
        double lufs = 0.0;
        juce::int64 tamanhoBytes = 0;
        std::string orientation;
        std::string colorSpace;
        std::string caminhoRelativo;
        std::string fullPath;
        std::string collectionNome;
        std::string collectionCaminho;
        std::string dataOriginal;  // EXIF DateTimeOriginal (aba SEQUENCES)
        std::string camera;        // EXIF Model (ou Make)

        // Match flags
        bool nomeCoincide = false;
        bool extCoincide = false;
        bool duracaoCoincide = false;
        bool dimCoincide = false;
        bool tamanhoCoincide = false;
        bool orientationCoincide = false;
        bool colorSpaceCoincide = false;
        bool lufsCoincide = false;
    };

    struct DuplicateGroup {
        DuplicateMatch original;
        DuplicateMatch duplicata;
        // Qual critério casou: "IDENTICAL" (mesmo SHA-256, qualquer nome/caminho), "PROBABLE" (só imagens: pHash
        // parecido) , "SIMILAR" (imagens quase iguais tiradas com segundos de diferença: EXIF diferente, pHash bem baixo)
        // — PROBABLE e SIMILAR são SEMPRE decisão do usuário, nunca resolvidos em lote — ou "" (critérios de sempre).
        std::string criterio;
        int distanciaPHash = -1;  // só PROBABLE
        // Selo "pasta inteira duplicada" (só IDENTICAL): TODOS os arquivos da pasta pastaNome também existem em
        // pastaEmNome. pastaArquivos == 0 = sem selo. pastaMesmoConteudo = vale nos dois sentidos.
        int pastaArquivos = 0;
        std::string pastaNome, pastaEmNome;
        bool pastaMesmoConteudo = false;
    };

    // Rajada (fotos em sequência): pares do pHash cujos arquivos têm nome sequencial. Ficam FORA da lista de
    // duplicatas e de qualquer ação em lote, sem marca nenhuma no banco: só para conferir na aba SEQUENCES.
    struct GrupoSequencia {
        std::vector<DuplicateMatch> fotos;  // em ordem de nome
        std::vector<DuplicateGroup> pares;  // os pares do pHash que formaram a rajada ("é duplicata sim" os devolve)
    };
    // nome = nome do arquivo sem extensão; segundos = horário EXIF (-1 = sem data); camera vazia = desconhecida.
    // Sequência: mesmo prefixo e sufixo, contador diferente a até 10 (com volta 9999 -> 0001) ou nome-horário
    // (20200826_165804) a até 60 s; EXIF a até 60 s quando os dois têm data; mesma câmera quando os dois têm.
    static bool ehSequenciaDeFotos(const std::string& nomeA, long long segundosA, const std::string& cameraA,
                                   const std::string& nomeB, long long segundosB, const std::string& cameraB);
    std::vector<GrupoSequencia> sequenciasDetectadas_;
    // "É duplicata sim": os pares da rajada vão para a lista principal, para decidir um a um.
    void enviarSequenciaParaDuplicatas(size_t indice);
    bool abaSequencias_ = false;
    void mostrarAba(bool sequencias);

    void resolverDuplicata(int grupoIdx, bool ehDuplicataReal);

    ProjetoAberto& projeto_;

    // UI State
public:
    enum class State {
        Idle,
        Scanning,
        Results,
        Clean
    };
private:
    State estado_ = State::Idle;
    // Sem duplicatas na lista: Results enquanto houver rajadas para conferir, senão Clean.
    State estadoSemDuplicatas() const { return sequenciasDetectadas_.empty() ? State::Clean : State::Results; }
    std::atomic<int> faseScan_{0};  // 1 critérios, 2 checksums, 3 pHash, 4 comparação — só para o texto de status
    std::atomic<double> progressoScan_{0.0};  // escrito pela thread do scan, lido pelo timer da UI
    // Contador ao vivo (a partir da fase de checksums): pares achados e bytes do lado "duplicata". gruposDetectados_
    // é da thread do scan até ela terminar, então a UI só lê estes dois atômicos.
    std::atomic<int> achadosScan_{0};
    std::atomic<juce::int64> bytesScan_{0};

    std::vector<DuplicateGroup> gruposDetectados_;

    struct ScanFilters {
        int scope = 1;
        std::set<std::string> selecionadosNoGrid;
        std::set<int> fileTypes;  // vazio = todos (ver tiposSelecionados_)
        int sizeFilter = 1;
        juce::int64 sizeLimitBytes = 0;
        // item: filtro por ano — 0 em qualquer um dos dois = sem limite
        // naquela ponta (ex.: só "De" preenchido = "esse ano em diante").
        int anoDe = 0;
        int anoAte = 0;

        // Critérios de coincidência
        bool coincidirSha256 = true;
        bool coincidirPHash = true;
        bool coincidirNome = false;
        bool coincidirTamanho = true;
        bool coincidirData = false;
        bool coincidirDuracao = true;
        bool coincidirDimensoes = false;
        bool coincidirEspacoCor = true;
        bool coincidirFormato = false;
    } activeFilters_;

    void resolverTudo(bool ehDuplicataReal);

    // RESET: esvazia a lista atual SEM resolver nada (os pares continuam não resolvidos no banco) e volta ao estado

    // inicial, pra o usuário mudar os filtros e rodar outro recorte.

    void resetarAba();
    void resolverSelecionados(bool ehDuplicataReal);
    // item (VALIDATE ALL em lote): uma escolha só (manter arquivo 1 / 2 /
    // ambos), aplicada de uma vez a TODOS os grupos detectados — não apaga
    // nada do catálogo nem do disco, só marca o lado descartado como
    // 'duplicata' pra sair da próxima leva de Make Backup.
    void aplicarEscolhaGlobal(int escolha);
    void atualizarBotoesSelecionados();
public:
    // Preferences > "lote em todas as categorias" mudou: SELECT ALL / CLEAR aparecem ou somem.
    void aplicarPreferenciaLote() { resized(); repaint(); }
private:
    // Preferences: VALIDATE/DISMISS ALL e SELECT ALL valem também para PROBABLE e SIMILAR (default: não).
    static bool loteTodasCategorias();
    void atualizarListaEStatusAposResolucao();

    // Coluna esquerda em cards (SCAN / FILTROS / AÇÕES), padrão do INTAKE.
    ColunaCardsLayout coluna_;

    // UI Elements
    std::unique_ptr<juce::TextButton> btnScan_;
    std::unique_ptr<juce::Label> lblStatus_;
    std::unique_ptr<juce::TextButton> btnValidateAll_;
    std::unique_ptr<juce::TextButton> btnDismissAll_;
    std::unique_ptr<juce::TextButton> btnValidateSelected_;
    std::unique_ptr<juce::TextButton> btnDismissSelected_;
    std::unique_ptr<juce::TextButton> btnSelecionarTodos_;  // só com a preferência de lote em todas as categorias
    std::unique_ptr<juce::TextButton> btnLimparSelecao_;
    std::unique_ptr<juce::TextButton> btnReset_;
    // Duplicatas já resolvidas ficam escondidas do app inteiro; este botão abre a lista com RESTAURAR.
    std::unique_ptr<juce::TextButton> btnResolvidas_;
    void atualizarContagemResolvidas();
    void abrirResolvidas();

    // Filter Bar UI Elements
    std::unique_ptr<juce::Label> lblScope_;
    std::unique_ptr<juce::ComboBox> cbScope_;
    std::unique_ptr<juce::Label> lblFileType_;
    std::unique_ptr<ComboDePainel> cbFileType_;  // abre os interruptores de tipo (múltipla escolha)
    std::set<int> tiposSelecionados_;            // vazio = todos; 2..7 = Image, Video, Audio, Docs, Sessions, Other
    void abrirPainelTipos();
    void atualizarTextoTipos();
    std::unique_ptr<juce::Label> lblFileSize_;
    std::unique_ptr<juce::ComboBox> cbSizeFilter_;
    std::unique_ptr<juce::TextEditor> txtSizeValue_;
    std::unique_ptr<juce::ComboBox> cbSizeUnit_;
    // item: filtro por ano (De/Até) — mesmo padrão de campo numérico do size filter.
    std::unique_ptr<juce::Label> lblAno_;
    std::unique_ptr<juce::TextEditor> txtAnoDe_;
    std::unique_ptr<juce::Label> lblAnoAte_;
    std::unique_ptr<juce::TextEditor> txtAnoAte_;

    // Toggles de critérios de coincidência
    std::unique_ptr<juce::ToggleButton> tbSha256_;
    std::unique_ptr<juce::ToggleButton> tbPHash_;
    std::unique_ptr<juce::ToggleButton> tbNome_;
    std::unique_ptr<juce::ToggleButton> tbTamanho_;
    std::unique_ptr<juce::ToggleButton> tbData_;
    std::unique_ptr<juce::ToggleButton> tbDuracao_;
    std::unique_ptr<juce::ToggleButton> tbDimensoes_;
    std::unique_ptr<juce::ToggleButton> tbEspacoCor_;
    std::unique_ptr<juce::ToggleButton> tbFormato_;
    
    // Results list viewport
    class ListaResultadosComponent;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<ListaResultadosComponent> listaComponent_;
    // Aba SEQUENCES: abas DUPLICATES (N) | SEQUENCES (N) na linha do resumo e a lista das rajadas.
    class ListaSequenciasComponent;
    std::unique_ptr<ListaSequenciasComponent> listaSequencias_;
    std::unique_ptr<juce::TextButton> btnAbaDuplicatas_;
    std::unique_ptr<juce::TextButton> btnAbaSequencias_;
    void atualizarTextoAbas();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuplicatesWorkspaceComponent)
};

} // namespace matriz::ui
