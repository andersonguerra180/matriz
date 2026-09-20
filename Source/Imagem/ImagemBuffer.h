#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>

namespace matriz::imagem {

struct ImagemBuffer {
    int largura = 0;
    int altura = 0;
    std::vector<uint8_t> pixels; // 8-bit RGBA contíguo (largura * altura * 4)

    ImagemBuffer() = default;

    ImagemBuffer(int w, int h)
        : largura(std::max(0, w)), altura(std::max(0, h)),
          pixels(static_cast<size_t>(largura * altura * 4), 255) {}

    ImagemBuffer(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : largura(std::max(0, w)), altura(std::max(0, h)),
          pixels(static_cast<size_t>(largura * altura * 4)) {
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i + 0] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = a;
        }
    }

    bool valido() const {
        return largura > 0 && altura > 0 && pixels.size() == static_cast<size_t>(largura * altura * 4);
    }

    inline const uint8_t* pixel(int x, int y) const {
        return &pixels[static_cast<size_t>((y * largura + x) * 4)];
    }

    inline uint8_t* pixel(int x, int y) {
        return &pixels[static_cast<size_t>((y * largura + x) * 4)];
    }

    inline void definirPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        auto* p = pixel(x, y);
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    }
};

} // namespace matriz::imagem
