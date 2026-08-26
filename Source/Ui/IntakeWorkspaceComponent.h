#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace matriz::ui {

class ProjetoAberto;
class MosaicoComponent;
class FichaPanelComponent;

class IntakeWorkspaceComponent : public juce::Component {
public:
    explicit IntakeWorkspaceComponent(ProjetoAberto& projeto);
    ~IntakeWorkspaceComponent() override;

    void recarregar();
    std::set<std::string> itensSelecionados() const;

    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void()> aoPedirIngerirArquivos;
    std::function<void()> aoConfirmarParaGrid;

private:
    void atualizarStatusContagem();
    void confirmarSelecaoParaGrid();
    void confirmarTodosParaGrid();

    ProjetoAberto& projeto_;

    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblSubtitulo_;
    std::unique_ptr<juce::Label> lblContador_;

    std::unique_ptr<juce::TextButton> btnIngerir_;
    std::unique_ptr<juce::TextButton> btnConfirmarSelecao_;
    std::unique_ptr<juce::TextButton> btnConfirmarTodos_;
    std::unique_ptr<juce::TextButton> btnSelecionarTodos_;
    std::unique_ptr<juce::TextButton> btnLimparSelecao_;

    std::unique_ptr<juce::Viewport> mosaicoViewport_;
    std::unique_ptr<MosaicoComponent> mosaico_;
    std::unique_ptr<FichaPanelComponent> fichaPanel_;
};

} // namespace matriz::ui
