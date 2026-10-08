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
    void lookAndFeelChanged() override;

    void aoProgressoAtualizado(const EstadoProgresso& estado) override;
    void aoDicaAlterada() override { atualizarVisual(); }

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
    // Loop de mensagens parado (lote síncrono na message thread): o timer não roda, então a barra se pinta
    // na hora a cada atualização (ver aoProgressoAtualizado), no máximo a cada 100 ms.
    juce::uint32 ultimoTimerMs_ = 0;
    juce::uint32 ultimaPinturaImediataMs_ = 0;
};

} // namespace matriz::ui
