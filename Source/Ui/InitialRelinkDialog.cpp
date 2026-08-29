#include "InitialRelinkDialog.h"
#include "Tokens.h"

namespace matriz::ui {

InitialRelinkDialog::InitialRelinkDialog(const juce::String& sampleMissingExpectedPath,
                                         const juce::String& sampleMissingTitle,
                                         std::function<void(const juce::File&)> onLocateFile,
                                         std::function<void()> onWorkOffline)
    : sampleMissingPath_(sampleMissingExpectedPath),
      sampleTitle_(sampleMissingTitle),
      onLocateFile_(std::move(onLocateFile)),
      onWorkOffline_(std::move(onWorkOffline)) {
    const auto& tk = tema();

    lblHeader_.setText("ASSET LOCATION NOT FOUND", juce::dontSendNotification);
    lblHeader_.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    lblHeader_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblHeader_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblHeader_);

    lblDescription_.setText("The assets for this project could not be located at their recorded paths.\n\nPlease locate one known asset from this project. The system will automatically infer the new storage root and relink reachable assets.", juce::dontSendNotification);
    lblDescription_.setFont(juce::Font(juce::FontOptions(13.0f)));
    lblDescription_.setColour(juce::Label::textColourId, tk.textoSecundario);
    lblDescription_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblDescription_);

    lblExpectedHeader_.setText("Sample Expected File (" + (sampleTitle_.isEmpty() ? "Asset" : sampleTitle_) + "):", juce::dontSendNotification);
    lblExpectedHeader_.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblExpectedHeader_.setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(lblExpectedHeader_);

    txtExpectedPath_.setText(sampleMissingPath_);
    txtExpectedPath_.setReadOnly(true);
    txtExpectedPath_.setFont(juce::Font(juce::FontOptions(12.0f)));
    txtExpectedPath_.setColour(juce::TextEditor::backgroundColourId, tk.fundo);
    txtExpectedPath_.setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    txtExpectedPath_.setColour(juce::TextEditor::outlineColourId, tk.borda);
    addAndMakeVisible(txtExpectedPath_);

    btnLocate_.setColour(juce::TextButton::buttonColourId, tk.acento);
    btnLocate_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnLocate_.onClick = [this] {
        juce::String fname = juce::File(sampleMissingPath_).getFileName();
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Locate Known Asset (" + fname + ")",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.*");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result.existsAsFile()) {
                auto cb = onLocateFile_;
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
                    dw->exitModalState(1);
                }
                if (cb) cb(result);
            }
        });
    };
    addAndMakeVisible(btnLocate_);

    btnWorkOffline_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnWorkOffline_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnWorkOffline_.onClick = [this] {
        auto cb = onWorkOffline_;
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
        if (cb) cb();
    };
    addAndMakeVisible(btnWorkOffline_);

    setSize(560, 310);
}

void InitialRelinkDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);
}

void InitialRelinkDialog::resized() {
    auto r = getLocalBounds().reduced(24);

    lblHeader_.setBounds(r.removeFromTop(26));
    r.removeFromTop(12);

    lblDescription_.setBounds(r.removeFromTop(56));
    r.removeFromTop(14);

    lblExpectedHeader_.setBounds(r.removeFromTop(20));
    r.removeFromTop(4);

    txtExpectedPath_.setBounds(r.removeFromTop(36));
    r.removeFromTop(24);

    auto btnArea = r.removeFromBottom(38);
    int btnWidth = 160;
    btnWorkOffline_.setBounds(btnArea.removeFromLeft(btnWidth));
    btnLocate_.setBounds(btnArea.removeFromRight(btnWidth));
}

void InitialRelinkDialog::showModal(const juce::String& sampleExpectedPath,
                                   const juce::String& sampleTitle,
                                   std::function<void(const juce::File& fileSelected)> onLocateFile,
                                   std::function<void()> onWorkOffline) {
    auto* dialog = new InitialRelinkDialog(sampleExpectedPath, sampleTitle,
                                          std::move(onLocateFile), std::move(onWorkOffline));

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "Asset Location Not Found";
    opt.content.setOwned(dialog);
    opt.componentToCentreAround = nullptr;
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;

    opt.launchAsync();
}

} // namespace matriz::ui
