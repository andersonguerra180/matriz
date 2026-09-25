#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <vector>

namespace matriz::ui {

// Generic "list of matches, pick one of 3 actions per row (or apply to all)" modal.
// Shared between INTAKE exact-duplicate resolution and DUPLICATES workspace batch validation
// (the label set for the 3 actions differs per caller; the interaction model is the same).
class DuplicateResolutionDialog : public juce::Component {
public:
    struct Entry {
        juce::String primaryLabel;
        juce::String secondaryLabel;
        int action = 0; // index into actionLabels, filled in as the user picks

        // Optional thumbnails (already decoded by the caller — this dialog does no file I/O).
        // imagemA/imagemB show side by side when both are valid (e.g. "file 1" vs "file 2");
        // only imagemA when there's a single file to preview (e.g. INTAKE's incoming file).
        // Clicking a thumbnail enlarges it, same interaction as the DUPLICATES tab cards.
        juce::Image imagemA;
        juce::Image imagemB;

        // Preenchido internamente a partir de actionLabels — RowComponent usa
        // isso pra remontar o texto do toggle (rótulo + ✓ quando marcado).
        std::array<juce::String, 3> rotuloBase;
    };

    DuplicateResolutionDialog(juce::String intro, std::vector<Entry> entries, std::array<juce::String, 3> actionLabels);
    ~DuplicateResolutionDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // onDone(confirmado, entries): confirmado == false means the user cancelled (entries unspecified).
    static void show(const juce::String& windowTitle,
                      const juce::String& intro,
                      std::vector<Entry> entries,
                      std::array<juce::String, 3> actionLabels,
                      std::function<void(bool confirmado, std::vector<Entry>)> onDone);

    std::function<void()> aoFechar;

private:
    class RowComponent;
    class AmpliadorComponent;

    void aplicarATodos(int action);
    void confirmar();
    void cancelar();
    void ampliarImagem(juce::Image imagem);
    void fecharAmpliador();

    std::vector<Entry> entries_;
    std::array<juce::String, 3> actionLabels_;

    std::unique_ptr<juce::Label> lblIntro_;
    std::unique_ptr<juce::Label> lblApplyAll_;
    std::array<std::unique_ptr<juce::TextButton>, 3> btnsApplyAll_;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<juce::Component> listaContainer_;
    juce::OwnedArray<RowComponent> linhas_;
    std::unique_ptr<juce::TextButton> btnCancelar_;
    std::unique_ptr<juce::TextButton> btnConfirmar_;
    std::unique_ptr<AmpliadorComponent> ampliador_;

    std::function<void(bool, std::vector<Entry>)> onDone_;
    bool concluido_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuplicateResolutionDialog)
};

} // namespace matriz::ui
