#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <set>

#include "ProjetoAberto.h"

namespace matriz::ui {

class PreservationWorkspaceComponent : public juce::Component {
public:
    explicit PreservationWorkspaceComponent(ProjetoAberto& projeto);
    ~PreservationWorkspaceComponent() override;

    void recarregar();

    // Callback when a category card is clicked. Passes the category key.
    std::function<void(const std::string& chave)> aoClicarMetrica;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    ProjetoAberto& projeto_;

    std::unique_ptr<juce::Label> labelTitulo_;
    std::unique_ptr<juce::Label> labelSubtitulo_;

    // Health Banner
    juce::Rectangle<int> boundsBanner_;
    juce::String textoHealth_;
    juce::Colour corHealth_;

    // Metrics (12 cards)
    struct Metrica {
        std::string chave;
        juce::String titulo;
        int contagem = 0;
        juce::Rectangle<int> bounds;
        bool hover = false;
        // Indica se contagem > 0 é um problema (vermelho) ou positivo (verde)
        bool problematico = false;
    };
    std::vector<Metrica> metricas_;

    // Contagens individuais para o banner de health
    int totalAssets_           = 0;
    int comPersistentId_       = 0;
    int comSha256_             = 0;
    int fixityVerificada_      = 0;
    int falhaIntegridade_      = 0;
    int formatoIdentificado_   = 0;
    int backupVerificado_      = 0;
    int direitosDesconhecidos_ = 0;
    int totalEventos_          = 0;
    int problemasGraves_       = 0;
    int semBackup_             = 0;
    int semFixity_             = 0;

    // Compliance metrics
    int formatosEmRisco_        = 0;
    int vaultsParaRefresh_      = 0;
    std::string regra321Status_;
    juce::String regra321Label_;
};

} // namespace matriz::ui
