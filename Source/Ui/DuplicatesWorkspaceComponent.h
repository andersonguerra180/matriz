#pragma once

#include <JuceHeader.h>
#include <vector>
#include <string>
#include <optional>

namespace matriz::ui {

class ProjetoAberto;

class DuplicatesWorkspaceComponent : public juce::Component,
                                      public juce::Thread,
                                      private juce::Timer {
public:
    explicit DuplicatesWorkspaceComponent(ProjetoAberto& projeto);
    ~DuplicatesWorkspaceComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void recarregar();
    void iniciarScan();

private:
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
    double progressoScan_ = 0.0;

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
    } activeFilters_;

    void resolverTudo(bool ehDuplicataReal);
    void resolverSelecionados(bool ehDuplicataReal);
    // item (VALIDATE ALL em lote): uma escolha só (manter arquivo 1 / 2 /
    // ambos), aplicada de uma vez a TODOS os grupos detectados — não apaga
    // nada do catálogo nem do disco, só marca o lado descartado como
    // 'duplicata' pra sair da próxima leva de Make Backup.
    void aplicarEscolhaGlobal(int escolha);
    void atualizarBotoesSelecionados();
    void atualizarListaEStatusAposResolucao();

    // UI Elements
    std::unique_ptr<juce::TextButton> btnScan_;
    std::unique_ptr<juce::Label> lblStatus_;
    std::unique_ptr<juce::TextButton> btnValidateAll_;
    std::unique_ptr<juce::TextButton> btnDismissAll_;
    std::unique_ptr<juce::TextButton> btnValidateSelected_;
    std::unique_ptr<juce::TextButton> btnDismissSelected_;

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
    
    // Results list viewport
    class ListaResultadosComponent;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<ListaResultadosComponent> listaComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuplicatesWorkspaceComponent)
};

} // namespace matriz::ui
