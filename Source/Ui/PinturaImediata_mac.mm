#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "PinturaImediata.h"

namespace matriz::ui {

// ComponentPeer::performAnyPendingRepaintsNow() não serve no macOS: o repaint() do JUCE só vira
// setNeedsDisplayInRect no próximo vblank (que não chega com a thread presa). Marca o retângulo direto
// na NSView (flipped, em pontos), desenha já e publica a camada com CATransaction flush.
void pintarAgora(juce::Component& c) {
    JUCE_ASSERT_MESSAGE_THREAD
    auto* peer = c.getPeer();
    if (peer == nullptr || !c.isShowing()) return;
    auto* view = static_cast<NSView*>(peer->getNativeHandle());
    if (view == nil) return;
    const auto r = peer->getComponent().getLocalArea(&c, c.getLocalBounds()).toFloat()
                   * juce::Desktop::getInstance().getGlobalScaleFactor();
    c.repaint();
    [view setNeedsDisplayInRect:NSMakeRect(r.getX(), r.getY(), r.getWidth(), r.getHeight())];
    [view displayIfNeeded];
    [CATransaction flush];
}

}  // namespace matriz::ui
