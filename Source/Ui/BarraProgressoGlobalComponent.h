#pragma once

#include <JuceHeader.h>
#include "ProgressoGlobal.h"

namespace matriz::ui {

class BarraProgressoGlobalComponent : public juce::Component,
                                      public ProgressoGlobalListener,
                                      private juce::Timer {
public:
    BarraProgressoGlobalComponent();
    ~BarraProgressoGlobalComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void aoProgressoAtualizado(const EstadoProgresso& estado) override;

    static constexpr int kAlturaFixa = 32;

private:
    void timerCallback() override;
    void atualizarVisual();

    EstadoProgresso estado_;

    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblDetalhe_;
    std::unique_ptr<juce::TextButton> btnCancelar_;

    float animOffset_ = 0.0f;
    double progressoSuavizado_ = 0.0;
};

} // namespace matriz::ui
