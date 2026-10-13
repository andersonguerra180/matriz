#include <windows.h>

#include <cstring>
#include <vector>

#include "AreaTransferenciaImagem.h"

namespace matriz::app {

bool copiarImagemParaAreaDeTransferencia(const juce::Image& imagem) {
    if (!imagem.isValid()) return false;
    const int w = imagem.getWidth(), h = imagem.getHeight();
    const size_t bytesPixels = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bytesPixels);
    if (mem == nullptr) return false;
    auto* base = static_cast<unsigned char*>(GlobalLock(mem));
    if (base == nullptr) { GlobalFree(mem); return false; }
    BITMAPINFOHEADER cab{};
    cab.biSize = sizeof(BITMAPINFOHEADER);
    cab.biWidth = w;
    cab.biHeight = h;  // positivo = de baixo para cima
    cab.biPlanes = 1;
    cab.biBitCount = 32;
    cab.biCompression = BI_RGB;
    cab.biSizeImage = static_cast<DWORD>(bytesPixels);
    std::memcpy(base, &cab, sizeof(cab));
    auto* px = base + sizeof(cab);
    juce::Image::BitmapData dados(imagem, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < h; ++y) {
        auto* dst = px + static_cast<size_t>(h - 1 - y) * static_cast<size_t>(w) * 4;
        for (int x = 0; x < w; ++x) {
            const auto c = dados.getPixelColour(x, y);
            dst[x * 4 + 0] = c.getBlue();
            dst[x * 4 + 1] = c.getGreen();
            dst[x * 4 + 2] = c.getRed();
            dst[x * 4 + 3] = 255;
        }
    }
    GlobalUnlock(mem);
    bool ok = false;
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        ok = SetClipboardData(CF_DIB, mem) != nullptr;
        CloseClipboard();
    }
    if (!ok) GlobalFree(mem);  // se deu certo, o sistema é o dono da memória
    return ok;
}

}  // namespace matriz::app
