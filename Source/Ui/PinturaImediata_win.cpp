#include "PinturaImediata.h"

namespace matriz::ui {

void pintarAgora(juce::Component& c) {
    JUCE_ASSERT_MESSAGE_THREAD
    if (auto* peer = c.getPeer()) {
        c.repaint();
        peer->performAnyPendingRepaintsNow();
    }
}

}  // namespace matriz::ui
