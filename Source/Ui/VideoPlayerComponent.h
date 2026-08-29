#pragma once

#include <JuceHeader.h>
#include <functional>

namespace matriz::ui {

class VideoPlayerComponent : public juce::Component, private juce::Timer {
public:
    VideoPlayerComponent();
    ~VideoPlayerComponent() override;

    bool carregar(const juce::File& arquivo);
    void tocar();
    void pausar();
    void parar();
    void irPara(double segundos);
    bool estaTocando() const;
    double posicaoAtual() const;
    double duracao() const;

    void definirTimecodeVisivel(bool visivel);
    void atualizarTimecodeTexto(const juce::String& timecodeStr);

    std::function<void(double posicaoSegundos)> aoPosicaoMudar;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void* bridge_ = nullptr;
    std::unique_ptr<juce::NSViewComponent> viewComponent_;
    bool carregado_ = false;
};

} // namespace matriz::ui
