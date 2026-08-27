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

    void recarregar();

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

    void iniciarScan();
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
    } activeFilters_;

    void resolverTudo(bool ehDuplicataReal);

    // UI Elements
    std::unique_ptr<juce::TextButton> btnScan_;
    std::unique_ptr<juce::Label> lblStatus_;
    std::unique_ptr<juce::TextButton> btnValidateAll_;
    std::unique_ptr<juce::TextButton> btnDismissAll_;

    // Filter Bar UI Elements
    std::unique_ptr<juce::Label> lblScope_;
    std::unique_ptr<juce::ComboBox> cbScope_;
    std::unique_ptr<juce::Label> lblFileType_;
    std::unique_ptr<juce::ComboBox> cbFileType_;
    std::unique_ptr<juce::Label> lblFileSize_;
    std::unique_ptr<juce::ComboBox> cbSizeFilter_;
    std::unique_ptr<juce::TextEditor> txtSizeValue_;
    std::unique_ptr<juce::ComboBox> cbSizeUnit_;
    
    // Results list viewport
    class ListaResultadosComponent;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<ListaResultadosComponent> listaComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuplicatesWorkspaceComponent)
};

} // namespace matriz::ui
