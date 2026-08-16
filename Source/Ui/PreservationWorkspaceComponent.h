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

    // Metrics
    struct Metrica {
        std::string chave;
        juce::String titulo;
        int contagem = 0;
        juce::Rectangle<int> bounds;
        bool hover = false;
    };
    std::vector<Metrica> metricas_;

    int totalAssets_ = 0;
    int verifiedCount_ = 0;
    int singleCopyCount_ = 0;
    int checksumProblemsCount_ = 0;
    int incompleteCount_ = 0;
    int reviewCount_ = 0;
};

} // namespace matriz::ui
