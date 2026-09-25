#pragma once

#include <JuceHeader.h>

namespace matriz::ui {

// Item 6 (nova lista): o thumb padrão do LookAndFeel_V4 é uma barra fina e
// proporcional ao conteúdo — em listas longas ela fica comprida e continua
// fina, difícil de mirar. Este LookAndFeel desenha em vez disso um "knob"
// curto e arredondado, de largura bem mais gorda que a trilha, centrado na
// posição proporcional do scroll — fácil de agarrar sem precisar mirar.
//
// Vive em arquivo próprio (não em Tokens.h) porque Tokens.h é incluído
// também pelos alvos de selftest em console (sem juce_gui_basics), que não
// enxergam juce::LookAndFeel_V4/juce::ScrollBar completos.
class MatrizLookAndFeel : public juce::LookAndFeel_V4 {
public:
    // JUCE clipa a pintura de cada Component às próprias bounds — um knob
    // redondo maior que a trilha padrão (a antiga, ~8px) ficaria cortado.
    // Este valor é o que os Viewports que não chamam setScrollBarThickness()
    // explicitamente usam (a maioria — ver MosaicoComponent/sidebar), então
    // aumentá-lo aqui já abre espaço suficiente pro knob em toda a app.
    int getDefaultScrollbarWidth() override { return 16; }

    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
};

} // namespace matriz::ui
