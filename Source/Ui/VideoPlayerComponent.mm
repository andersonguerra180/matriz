// AVFoundation headers clash with JUCE's Point/Component when included in the
// same translation unit. The standard JUCE workaround: rename Carbon's types
// before they're pulled in, then undo.
#define Point CarbonDummyPointName
#define Component CarbonDummyComponentName

#include "VideoPlayerComponent.h"
#include "Tokens.h"

#undef Point
#undef Component

#if defined(__APPLE__)

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>

namespace matriz::ui {

struct VideoPlayerComponent::Impl {
    AVPlayer* player = nil;
    AVPlayerLayer* playerLayer = nil;
    NSView* containerView = nil;
    std::unique_ptr<juce::NSViewComponent> viewComponent;
    double duracaoCache = 0.0;
    bool carregado = false;
};

VideoPlayerComponent::VideoPlayerComponent() : impl_(std::make_unique<Impl>()) {}

VideoPlayerComponent::~VideoPlayerComponent() {
    stopTimer();
    if (impl_->player) {
        [impl_->player pause];
        impl_->player = nil;
    }
    impl_->viewComponent.reset();
    impl_->containerView = nil;
    impl_->playerLayer = nil;
}

bool VideoPlayerComponent::carregar(const juce::File& arquivo) {
    if (impl_->player) {
        [impl_->player pause];
        impl_->player = nil;
    }
    impl_->viewComponent.reset();
    impl_->containerView = nil;
    impl_->playerLayer = nil;
    impl_->carregado = false;
    impl_->duracaoCache = 0.0;

    NSURL* url = [NSURL fileURLWithPath:
        [NSString stringWithUTF8String:arquivo.getFullPathName().toRawUTF8()]];
    AVPlayerItem* item = [AVPlayerItem playerItemWithURL:url];
    if (!item) return false;

    impl_->player = [AVPlayer playerWithPlayerItem:item];
    if (!impl_->player) return false;

    impl_->player.actionAtItemEnd = AVPlayerActionAtItemEndPause;

    impl_->containerView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 360)];
    impl_->containerView.wantsLayer = YES;

    impl_->playerLayer = [AVPlayerLayer playerLayerWithPlayer:impl_->player];
    impl_->playerLayer.videoGravity = AVLayerVideoGravityResizeAspect;
    impl_->playerLayer.backgroundColor = CGColorCreateGenericRGB(0.18, 0.18, 0.20, 1.0);
    [impl_->containerView.layer addSublayer:impl_->playerLayer];

    impl_->viewComponent = std::make_unique<juce::NSViewComponent>();
    impl_->viewComponent->setView(impl_->containerView);
    addAndMakeVisible(*impl_->viewComponent);

    CMTime dur = impl_->player.currentItem.asset.duration;
    if (CMTIME_IS_VALID(dur) && !CMTIME_IS_INDEFINITE(dur))
        impl_->duracaoCache = CMTimeGetSeconds(dur);

    impl_->carregado = true;
    startTimerHz(15);
    resized();
    return true;
}

void VideoPlayerComponent::tocar() {
    if (impl_->player) [impl_->player play];
}

void VideoPlayerComponent::pausar() {
    if (impl_->player) [impl_->player pause];
}

void VideoPlayerComponent::parar() {
    if (impl_->player) {
        [impl_->player pause];
        [impl_->player seekToTime:kCMTimeZero];
    }
}

void VideoPlayerComponent::irPara(double segundos) {
    if (impl_->player)
        [impl_->player seekToTime:CMTimeMakeWithSeconds(segundos, 600)
                  toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
}

bool VideoPlayerComponent::estaTocando() const {
    return impl_->player && impl_->player.rate > 0.0f;
}

double VideoPlayerComponent::posicaoAtual() const {
    if (!impl_->player) return 0.0;
    CMTime t = impl_->player.currentTime;
    return CMTIME_IS_VALID(t) ? CMTimeGetSeconds(t) : 0.0;
}

double VideoPlayerComponent::duracao() const {
    if (impl_->duracaoCache > 0.0) return impl_->duracaoCache;
    if (!impl_->player || !impl_->player.currentItem) return 0.0;
    CMTime dur = impl_->player.currentItem.asset.duration;
    return (CMTIME_IS_VALID(dur) && !CMTIME_IS_INDEFINITE(dur)) ? CMTimeGetSeconds(dur) : 0.0;
}

void VideoPlayerComponent::paint(juce::Graphics& g) {
    if (!impl_->carregado) {
        g.fillAll(tema().fundo);
        g.setColour(tema().textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText("No video loaded", getLocalBounds(), juce::Justification::centred);
    }
}

void VideoPlayerComponent::resized() {
    if (impl_->viewComponent)
        impl_->viewComponent->setBounds(getLocalBounds());
    if (impl_->playerLayer) {
        auto b = getLocalBounds();
        impl_->playerLayer.frame = CGRectMake(0, 0, b.getWidth(), b.getHeight());
    }
}

void VideoPlayerComponent::timerCallback() {
    if (!impl_->player) return;
    if (impl_->duracaoCache <= 0.0) {
        CMTime dur = impl_->player.currentItem.asset.duration;
        if (CMTIME_IS_VALID(dur) && !CMTIME_IS_INDEFINITE(dur))
            impl_->duracaoCache = CMTimeGetSeconds(dur);
    }
    if (aoPosicaoMudar) aoPosicaoMudar(posicaoAtual());
}

} // namespace matriz::ui

#else

namespace matriz::ui {
struct VideoPlayerComponent::Impl {};
VideoPlayerComponent::VideoPlayerComponent() : impl_(std::make_unique<Impl>()) {}
VideoPlayerComponent::~VideoPlayerComponent() { stopTimer(); }
bool VideoPlayerComponent::carregar(const juce::File&) { return false; }
void VideoPlayerComponent::tocar() {}
void VideoPlayerComponent::pausar() {}
void VideoPlayerComponent::parar() {}
void VideoPlayerComponent::irPara(double) {}
bool VideoPlayerComponent::estaTocando() const { return false; }
double VideoPlayerComponent::posicaoAtual() const { return 0.0; }
double VideoPlayerComponent::duracao() const { return 0.0; }
void VideoPlayerComponent::paint(juce::Graphics& g) { g.fillAll(tema().fundo); }
void VideoPlayerComponent::resized() {}
void VideoPlayerComponent::timerCallback() {}
} // namespace matriz::ui

#endif
