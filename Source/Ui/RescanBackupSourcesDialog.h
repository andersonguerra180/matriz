#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>
#include "../Vault/RescanEngine.h"

namespace matriz::ui {

class RescanBackupSourcesDialog : public juce::Component {
public:
    RescanBackupSourcesDialog(
        std::vector<matriz::vault::RescanPair> pares,
        std::function<void(std::vector<matriz::vault::RescanPair> paresEscolhidos)> onConfirmar);
    ~RescanBackupSourcesDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static void showDialog(
        std::vector<matriz::vault::RescanPair> pares,
        std::function<void(std::vector<matriz::vault::RescanPair> paresEscolhidos)> onConfirmar);

private:
    struct ItemLinha {
        matriz::vault::RescanPair par;
        std::unique_ptr<juce::ToggleButton> toggle;
    };

    void atualizarEstadoBotoes();
    void fecharDialogo();

    std::vector<ItemLinha> linhas_;
    std::function<void(std::vector<matriz::vault::RescanPair>)> onConfirmar_;

    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<juce::Component> containerLinhas_;

    std::unique_ptr<juce::TextButton> btnRescanSelecionados_;
    std::unique_ptr<juce::TextButton> btnRescanTodos_;
    std::unique_ptr<juce::TextButton> btnCancelar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RescanBackupSourcesDialog)
};

} // namespace matriz::ui
