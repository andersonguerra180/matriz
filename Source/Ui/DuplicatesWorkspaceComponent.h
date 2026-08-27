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
        std::string caminhoRelativo;
        
        // Match flags
        bool nomeCoincide = false;
        bool extCoincide = false;
        bool duracaoCoincide = false;
        bool dimCoincide = false;
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

    void resolverTudo(bool ehDuplicataReal);

    // UI Elements
    std::unique_ptr<juce::TextButton> btnScan_;
    std::unique_ptr<juce::Label> lblStatus_;
    std::unique_ptr<juce::TextButton> btnValidateAll_;
    std::unique_ptr<juce::TextButton> btnDismissAll_;
    
    // Results list viewport
    class ListaResultadosComponent;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<ListaResultadosComponent> listaComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuplicatesWorkspaceComponent)
};

} // namespace matriz::ui
