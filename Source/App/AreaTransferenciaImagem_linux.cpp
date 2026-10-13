#include "AreaTransferenciaImagem.h"

namespace matriz::app {

bool copiarImagemParaAreaDeTransferencia(const juce::Image& imagem) {
    if (!imagem.isValid()) return false;
    const auto arquivo = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("matriz-clip", ".png");
    {
        juce::FileOutputStream saida(arquivo);
        if (saida.failedToOpen()) return false;
        juce::PNGImageFormat png;
        if (!png.writeImageToStream(imagem, saida)) { arquivo.deleteFile(); return false; }
    }
    bool ok = false;
    // Wayland primeiro, depois X11. O wl-copy/xclip ficam rodando em segundo plano segurando a seleção.
    const juce::String cmdWayland = "sh -c 'command -v wl-copy >/dev/null 2>&1 && wl-copy --type image/png < \"" + arquivo.getFullPathName() + "\"'";
    const juce::String cmdX11 = "sh -c 'command -v xclip >/dev/null 2>&1 && xclip -selection clipboard -t image/png -i \"" + arquivo.getFullPathName() + "\"'";
    for (const auto& cmd : { cmdWayland, cmdX11 }) {
        juce::ChildProcess p;
        if (!p.start(cmd)) continue;
        juce::Thread::sleep(400);  // o xclip fica rodando segurando a seleção; o wl-copy sai logo
        if (p.isRunning() || p.getExitCode() == 0) { ok = true; break; }
    }
    juce::Timer::callAfterDelay(60000, [arquivo] { arquivo.deleteFile(); });
    return ok;
}

}  // namespace matriz::app
