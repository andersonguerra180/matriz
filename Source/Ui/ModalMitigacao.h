#pragma once

#include <JuceHeader.h>

// Mitigação para os juce::AlertWindow que sobraram (§3).
//
// O caminho definitivo é o PainelOverlay (OverlayComponent.h) — sem peer
// nativo, sem loop modal do sistema, sem crash de repintura. Alguns diálogos
// ainda são AlertWindow porque dependem do layout automático dele (campos
// nomeados, botões, custom components); enquanto forem, todo callback modal
// tem que começar tirando o peer da tela.
//
// O motivo: o callback é onde o diálogo começa a ser desmontado, e enquanto
// ele tem peer o AppKit continua livre pra chamar paint() em cima — que era
// o SIGABRT em juce::AlertWindow::paint() -> Component::getName(). Sem peer
// não há mais quem repinte, e ler o conteúdo (getTextEditorContents etc.)
// continua funcionando normalmente, porque o objeto segue vivo.

namespace matriz::ui {

inline void retirarPeerDaTela(juce::Component& janela) {
    janela.setVisible(false);
    janela.removeFromDesktop();
}

} // namespace matriz::ui
