#include "QrCodeImagem.h"

#include <qrcodegen.hpp>

namespace matriz::celular {

std::vector<std::vector<bool>> gerarModulosQr(const std::string& texto) {
    std::vector<std::vector<bool>> out;
    try {
        const auto qr = qrcodegen::QrCode::encodeText(texto.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int n = qr.getSize();
        out.assign(static_cast<size_t>(n), std::vector<bool>(static_cast<size_t>(n), false));
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) out[static_cast<size_t>(y)][static_cast<size_t>(x)] = qr.getModule(x, y);
    } catch (const std::exception&) {
        out.clear();
    }
    return out;
}

juce::Image criarImagemQr(const std::string& texto, int pixelsPorModulo) {
    const auto m = gerarModulosQr(texto);
    if (m.empty()) return {};
    const int n = static_cast<int>(m.size());
    const int borda = 4;
    const int px = juce::jmax(1, pixelsPorModulo);
    const int lado = (n + 2 * borda) * px;
    juce::Image img(juce::Image::RGB, lado, lado, true);
    juce::Graphics g(img);
    g.fillAll(juce::Colours::white);
    g.setColour(juce::Colours::black);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (m[static_cast<size_t>(y)][static_cast<size_t>(x)]) g.fillRect((x + borda) * px, (y + borda) * px, px, px);
    return img;
}

} // namespace matriz::celular
