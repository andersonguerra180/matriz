#pragma once

#include <JuceHeader.h>
#include <functional>
#include <string>
#include <vector>

#include "ProjetoAberto.h"

namespace matriz::ui {

class TrashWindow : public juce::DocumentWindow {
public:
    TrashWindow(ProjetoAberto& projeto, const std::string& mapaId, std::function<void()> aoRestaurar);
    ~TrashWindow() override;

    void closeButtonPressed() override;

private:
    class Conteudo;
    std::unique_ptr<Conteudo> conteudo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrashWindow)
};

} // namespace matriz::ui
