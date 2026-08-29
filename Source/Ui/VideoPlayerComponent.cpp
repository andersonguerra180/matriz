#include "VideoPlayerComponent.h"
#include "VideoPlayerBridge.h"
#include "Tokens.h"

namespace matriz::ui {

VideoPlayerComponent::VideoPlayerComponent() {
    bridge_ = vpCreate();
}

VideoPlayerComponent::~VideoPlayerComponent() {
    stopTimer();
    viewComponent_.reset();
    vpDestroy(static_cast<VPHandle>(bridge_));
    bridge_ = nullptr;
}

bool VideoPlayerComponent::carregar(const juce::File& arquivo) {
    stopTimer();
    viewComponent_.reset();
    carregado_ = false;

    if (!vpLoad(static_cast<VPHandle>(bridge_), arquivo.getFullPathName().toRawUTF8()))
        return false;

    void* nsView = vpGetNSView(static_cast<VPHandle>(bridge_));
    if (!nsView) return false;

    viewComponent_ = std::make_unique<juce::NSViewComponent>();
    viewComponent_->setView(nsView);
    addAndMakeVisible(*viewComponent_);

    carregado_ = true;
    startTimerHz(15);
    resized();
    return true;
}

void VideoPlayerComponent::tocar() { vpPlay(static_cast<VPHandle>(bridge_)); }
void VideoPlayerComponent::pausar() { vpPause(static_cast<VPHandle>(bridge_)); }
void VideoPlayerComponent::parar() { vpStop(static_cast<VPHandle>(bridge_)); }
void VideoPlayerComponent::irPara(double s) { vpSeek(static_cast<VPHandle>(bridge_), s); }
bool VideoPlayerComponent::estaTocando() const { return vpIsPlaying(static_cast<VPHandle>(bridge_)); }
double VideoPlayerComponent::posicaoAtual() const { return vpPosition(static_cast<VPHandle>(bridge_)); }
double VideoPlayerComponent::duracao() const { return vpDuration(static_cast<VPHandle>(bridge_)); }

void VideoPlayerComponent::definirTimecodeVisivel(bool visivel) {
    vpSetTimecodeVisible(static_cast<VPHandle>(bridge_), visivel);
}

void VideoPlayerComponent::atualizarTimecodeTexto(const juce::String& timecodeStr) {
    vpSetTimecodeText(static_cast<VPHandle>(bridge_), timecodeStr.toRawUTF8());
}

void VideoPlayerComponent::paint(juce::Graphics& g) {
    if (!carregado_) {
        g.fillAll(tema().fundo);
        g.setColour(tema().textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText("No video loaded", getLocalBounds(), juce::Justification::centred);
    }
}

void VideoPlayerComponent::resized() {
    if (viewComponent_)
        viewComponent_->setBounds(getLocalBounds());
    vpResize(static_cast<VPHandle>(bridge_), getWidth(), getHeight());
}

void VideoPlayerComponent::timerCallback() {
    if (!carregado_) return;
    if (aoPosicaoMudar) aoPosicaoMudar(posicaoAtual());
}

} // namespace matriz::ui
