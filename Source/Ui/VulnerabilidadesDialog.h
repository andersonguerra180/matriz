#pragma once

#include <JuceHeader.h>
#include "ProjetoAberto.h"
#include <string>
#include <vector>
#include <set>
#include <functional>

namespace matriz::ui {

struct VulnerabilidadeCategoria {
    std::string id;
    juce::String titulo;
    juce::String descricao;
    juce::Colour corAcento;
    std::set<std::string> itemIds;
};

class VulnerabilidadesDialog : public juce::Component {
public:
    explicit VulnerabilidadesDialog(ProjetoAberto& projeto);
    ~VulnerabilidadesDialog() override = default;

    std::function<void(const std::set<std::string>& itemIds)> aoSelecionarCategoria;
    std::function<void()> aoFechar;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

    static void exibirModal(ProjetoAberto& projeto,
                            juce::Component* parentComp,
                            std::function<void(const std::set<std::string>&)> callbackAoAbrirNoGrid);

private:
    void carregarVulnerabilidades();

    ProjetoAberto& projeto_;
    std::vector<VulnerabilidadeCategoria> categorias_;
    std::vector<juce::Rectangle<int>> subcardBounds_;
    int hoveredIdx_ = -1;

    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblSubtitulo_;
    std::unique_ptr<juce::TextButton> btnFechar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VulnerabilidadesDialog)
};

} // namespace matriz::ui
