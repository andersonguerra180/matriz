#pragma once

#include <JuceHeader.h>

// Coloca uma imagem na área de transferência do sistema (o JUCE só copia texto).
// macOS: NSPasteboard. Windows: CF_DIB. Linux: wl-copy (Wayland) ou xclip (X11), se instalados.
// false = não foi possível (o chamador segue com "Salvar como…").

namespace matriz::app {

bool copiarImagemParaAreaDeTransferencia(const juce::Image& imagem);

} // namespace matriz::app
