#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include <string>
#include "../Vault/AssetRelinkEngine.h"

namespace matriz::ui {

class InitialRelinkDialog : public juce::Component {
public:
    InitialRelinkDialog(const juce::String& sampleMissingExpectedPath,
                        const juce::String& sampleMissingTitle,
                        std::function<void(const juce::File& fileSelected)> onLocateFile,
                        std::function<void()> onWorkOffline,
                        std::function<void()> onSkip = {});
    ~InitialRelinkDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static void showModal(const juce::String& sampleExpectedPath,
                          const juce::String& sampleTitle,
                          std::function<void(const juce::File& fileSelected)> onLocateFile,
                          std::function<void()> onWorkOffline,
                          std::function<void()> onSkip = {});

    // LOCATE abre direto na pasta onde o arquivo estava; se ela não existe
    // mais, na pasta mais próxima que ainda existe (depois a pasta pessoal).
    static juce::File pastaInicialParaLocalizar(const juce::String& caminhoEsperado);

private:
    juce::String sampleMissingPath_;
    juce::String sampleTitle_;
    std::function<void(const juce::File&)> onLocateFile_;
    std::function<void()> onWorkOffline_;
    std::function<void()> onSkip_;

    juce::Label lblHeader_;
    juce::Label lblDescription_;
    juce::Label lblExpectedHeader_;
    juce::TextEditor txtExpectedPath_;
    juce::TextButton btnLocate_;
    juce::TextButton btnWorkOffline_;
    juce::TextButton btnSkip_;

    std::unique_ptr<juce::FileChooser> fileChooser_;
};

} // namespace matriz::ui
