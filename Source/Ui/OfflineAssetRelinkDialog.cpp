#include "OfflineAssetRelinkDialog.h"
#include "Tokens.h"

namespace matriz::ui {

OfflineAssetRelinkDialog::OfflineAssetRelinkDialog(matriz::db::Database& db,
                                                   const std::string& itemId,
                                                   const juce::String& itemTitle,
                                                   const juce::String& expectedPath,
                                                   const juce::String& storageName,
                                                   std::function<void(const juce::File&)> onRelinkSuccess,
                                                   std::function<void()> onCancel)
    : db_(db),
      itemId_(itemId),
      itemTitle_(itemTitle),
      expectedPath_(expectedPath),
      storageName_(storageName),
      onRelinkSuccess_(std::move(onRelinkSuccess)),
      onCancel_(std::move(onCancel)) {
    const auto& tk = tema();

    lblHeader_.setText("ASSET OFFLINE", juce::dontSendNotification);
    lblHeader_.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    lblHeader_.setColour(juce::Label::textColourId, juce::Colour(0xfff97316)); // bright orange
    lblHeader_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblHeader_);

    lblStatusWarning_.setText("The original physical file could not be found at its recorded location.", juce::dontSendNotification);
    lblStatusWarning_.setFont(juce::Font(juce::FontOptions(13.0f)));
    lblStatusWarning_.setColour(juce::Label::textColourId, tk.textoSecundario);
    lblStatusWarning_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblStatusWarning_);

    lblTitleHeader_.setText("Title:", juce::dontSendNotification);
    lblTitleHeader_.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblTitleHeader_.setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(lblTitleHeader_);

    lblTitleValue_.setText(itemTitle_.isEmpty() ? "Asset" : itemTitle_, juce::dontSendNotification);
    lblTitleValue_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    lblTitleValue_.setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(lblTitleValue_);

    lblExpectedHeader_.setText("Expected location:", juce::dontSendNotification);
    lblExpectedHeader_.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblExpectedHeader_.setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(lblExpectedHeader_);

    txtExpectedPath_.setText(expectedPath_);
    txtExpectedPath_.setReadOnly(true);
    txtExpectedPath_.setFont(juce::Font(juce::FontOptions(12.0f)));
    txtExpectedPath_.setColour(juce::TextEditor::backgroundColourId, tk.fundo);
    txtExpectedPath_.setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    txtExpectedPath_.setColour(juce::TextEditor::outlineColourId, tk.borda);
    addAndMakeVisible(txtExpectedPath_);

    lblStorageHeader_.setText("Storage:", juce::dontSendNotification);
    lblStorageHeader_.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblStorageHeader_.setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(lblStorageHeader_);

    lblStorageValue_.setText(storageName_.isEmpty() ? "Local Storage" : storageName_, juce::dontSendNotification);
    lblStorageValue_.setFont(juce::Font(juce::FontOptions(13.0f)));
    lblStorageValue_.setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(lblStorageValue_);

    lblError_.setFont(juce::Font(juce::FontOptions(12.0f)));
    lblError_.setColour(juce::Label::textColourId, juce::Colour(0xffef4444)); // red
    lblError_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblError_);

    btnLocate_.setColour(juce::TextButton::buttonColourId, tk.acento);
    btnLocate_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnLocate_.onClick = [this] {
        lblError_.setText("", juce::dontSendNotification);

        juce::String fname = juce::File(expectedPath_).getFileName();
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Locate Asset File (" + fname + ")",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.*");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result.existsAsFile()) {
                auto val = matriz::vault::AssetRelinkEngine::validarAsset(db_, itemId_, result);
                if (!val.isValid) {
                    lblError_.setText(val.errorMessage, juce::dontSendNotification);
                    return;
                }

                if (val.isDifferentContent) {
                    juce::AlertWindow::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle("Checksum Mismatch — Different File Content")
                            .withMessage("The selected file does not match the original file hash or size.\n\n"
                                         "Original SHA-256: " + juce::String(val.expectedSha).substring(0, 16) + "...\n"
                                         "Selected SHA-256: " + juce::String(val.actualSha).substring(0, 16) + "...\n\n"
                                         "Would you like to replace the asset? A new Asset ID will be assigned, and the replacement will be recorded in the project log.")
                            .withButton("Replace Asset (New ID)")
                            .withButton("Cancel"),
                        [this, result, cb = onRelinkSuccess_](int buttonIndex) {
                            if (buttonIndex == 1) {
                                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
                                    dw->exitModalState(1);
                                }
                                if (cb) cb(result);
                            }
                        });
                    return;
                }

                auto cb = onRelinkSuccess_;
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
                    dw->exitModalState(1);
                }
                if (cb) cb(result);
            }
        });
    };
    addAndMakeVisible(btnLocate_);

    btnCancel_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancel_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnCancel_.onClick = [this] {
        auto cb = onCancel_;
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
        if (cb) cb();
    };
    addAndMakeVisible(btnCancel_);

    setSize(520, 340);
}

void OfflineAssetRelinkDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);
}

void OfflineAssetRelinkDialog::resized() {
    auto r = getLocalBounds().reduced(24);

    lblHeader_.setBounds(r.removeFromTop(24));
    r.removeFromTop(4);

    lblStatusWarning_.setBounds(r.removeFromTop(20));
    r.removeFromTop(12);

    lblTitleHeader_.setBounds(r.removeFromTop(18));
    lblTitleValue_.setBounds(r.removeFromTop(22));
    r.removeFromTop(10);

    lblExpectedHeader_.setBounds(r.removeFromTop(18));
    txtExpectedPath_.setBounds(r.removeFromTop(32));
    r.removeFromTop(10);

    lblStorageHeader_.setBounds(r.removeFromTop(18));
    lblStorageValue_.setBounds(r.removeFromTop(20));
    r.removeFromTop(6);

    lblError_.setBounds(r.removeFromTop(22));
    r.removeFromTop(6);

    auto btnArea = r.removeFromBottom(36);
    int btnWidth = 140;
    btnCancel_.setBounds(btnArea.removeFromLeft(btnWidth));
    btnLocate_.setBounds(btnArea.removeFromRight(btnWidth));
}

void OfflineAssetRelinkDialog::showModal(matriz::db::Database& db,
                                         const std::string& itemId,
                                         const juce::String& itemTitle,
                                         const juce::String& expectedPath,
                                         const juce::String& storageName,
                                         std::function<void(const juce::File& fileSelected)> onRelinkSuccess,
                                         std::function<void()> onCancel) {
    auto* dialog = new OfflineAssetRelinkDialog(db, itemId, itemTitle, expectedPath, storageName,
                                               std::move(onRelinkSuccess), std::move(onCancel));

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "Relink Offline Asset";
    opt.content.setOwned(dialog);
    opt.componentToCentreAround = nullptr;
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;

    opt.launchAsync();
}

} // namespace matriz::ui
