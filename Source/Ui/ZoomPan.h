#pragma once

#include <JuceHeader.h>

namespace matriz::ui {

// Zoom + pan de um conteúdo (imagem, texto) dentro de uma área. zoom 1 = ajustado à área (fit). Roda do mouse
// com ⌘/Ctrl e pinça no trackpad = zoom no cursor; roda/trackpad sem modificador = rolar; arrastar = mãozinha;
// duplo clique = voltar ao fit. Só estado e conta; quem usa desenha e repinta.
struct ZoomPan {
    static constexpr float kMin = 0.25f, kMax = 40.0f;
    float zoom = 1.0f;
    juce::Point<float> pan;  // deslocamento em pixels de tela, depois do zoom
    bool arrastando = false;
    juce::Point<float> arrastoInicio, panInicio;

    void reset() { zoom = 1.0f; pan = {}; arrastando = false; }
    bool alterado() const { return zoom != 1.0f || pan.x != 0.0f || pan.y != 0.0f; }

    // Zoom em torno do ponto p (coordenadas do componente), `area` = área de desenho.
    void zoomarEm(float novoZoom, juce::Point<float> p, juce::Rectangle<float> area) {
        novoZoom = juce::jlimit(kMin, kMax, novoZoom);
        const auto ac = area.getCentre();
        pan = p - ac - (p - ac - pan) * (novoZoom / zoom);
        zoom = novoZoom;
    }

    // Retângulo do conteúdo (já ajustado ao fit em `base`) com zoom e pan; o pan é limitado para o conteúdo
    // nunca sair inteiro da área.
    juce::Rectangle<float> aplicar(juce::Rectangle<float> base, juce::Rectangle<float> area) {
        const auto ac = area.getCentre();
        auto r = base.transformedBy(juce::AffineTransform::scale(zoom, zoom, ac.x, ac.y).translated(pan.x, pan.y));
        constexpr float kResto = 48.0f;
        float dx = 0.0f, dy = 0.0f;
        if (r.getRight() < area.getX() + kResto) dx = area.getX() + kResto - r.getRight();
        if (r.getX() > area.getRight() - kResto) dx = area.getRight() - kResto - r.getX();
        if (r.getBottom() < area.getY() + kResto) dy = area.getY() + kResto - r.getBottom();
        if (r.getY() > area.getBottom() - kResto) dy = area.getBottom() - kResto - r.getY();
        if (dx != 0.0f || dy != 0.0f) { pan += {dx, dy}; r = r.translated(dx, dy); }
        return r;
    }

    // Roda: com ⌘/Ctrl dá zoom no cursor; sem modificador rola (deltaX/deltaY do trackpad). true = tratou.
    bool roda(const juce::MouseEvent& e, const juce::MouseWheelDetails& w, juce::Rectangle<float> area) {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
            const float passo = std::pow(1.0f + 0.25f, (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f) * 3.0f);
            zoomarEm(zoom * passo, e.position, area);
        } else {
            pan += {w.deltaX * 400.0f, w.deltaY * 400.0f};
        }
        return true;
    }
    void pinca(const juce::MouseEvent& e, float fator, juce::Rectangle<float> area) {
        if (fator > 0.0f) zoomarEm(zoom * fator, e.position, area);
    }
    void iniciarArrasto(const juce::MouseEvent& e) { arrastando = true; arrastoInicio = e.position; panInicio = pan; }
    void arrastar(const juce::MouseEvent& e) { if (arrastando) pan = panInicio + (e.position - arrastoInicio); }
    void terminarArrasto() { arrastando = false; }
};

// SVG (vetor) rasterizado no lado maior `ladoMax`, para o mesmo zoom/pan das imagens; imagem vazia se não abrir.
inline juce::Image rasterizarSvg(const juce::File& arquivo, int ladoMax = 2048) {
    std::unique_ptr<juce::Drawable> d(juce::Drawable::createFromImageFile(arquivo));
    if (d == nullptr) return {};
    auto caixa = d->getDrawableBounds();
    if (caixa.isEmpty()) return {};
    const float f = static_cast<float>(ladoMax) / std::max(caixa.getWidth(), caixa.getHeight());
    const int w = std::max(1, juce::roundToInt(caixa.getWidth() * f)), h = std::max(1, juce::roundToInt(caixa.getHeight() * f));
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Graphics g(img);
    d->drawWithin(g, juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), juce::RectanglePlacement::centred, 1.0f);
    return img;
}

}  // namespace matriz::ui
