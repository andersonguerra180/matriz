#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <set>
#include <vector>
#include <string>
#include <optional>

#include "ColunaCardsLayout.h"

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

private:
    static constexpr int kAlturaResumo = 28;
    // Thread method for background scanning
    void run() override;

    // Timer callback to update progress bar and UI state
    void timerCallback() override;

    struct DuplicateMatch {
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
    };

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
    std::atomic<int> faseScan_{0};  // 1 critérios, 2 checksums, 3 pHash, 4 comparação — só para o texto de status
    std::atomic<double> progressoScan_{0.0};  // escrito pela thread do scan, lido pelo timer da UI

    std::vector<DuplicateGroup> gruposDetectados_;

    struct ScanFilters {
        int scope = 1;
        std::set<std::string> selecionadosNoGrid;
        int fileType = 1;
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
    std::unique_ptr<juce::TextButton> btnReset_;

    // Filter Bar UI Elements
    std::unique_ptr<juce::Label> lblScope_;
    std::unique_ptr<juce::ComboBox> cbScope_;
    std::unique_ptr<juce::Label> lblFileType_;
    std::unique_ptr<juce::ComboBox> cbFileType_;
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
    
    // Results list viewport
    class ListaResultadosComponent;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<ListaResultadosComponent> listaComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuplicatesWorkspaceComponent)
};

} // namespace matriz::ui
