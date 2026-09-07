#include "OfflineAssetRelinkDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

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

    lblHeader_.setText(i18n::t("relink.ativo_offline"), juce::dontSendNotification);
    lblHeader_.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    lblHeader_.setColour(juce::Label::textColourId, juce::Colour(0xfff97316)); // bright orange
    lblHeader_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblHeader_);

    lblStatusWarning_.setText(i18n::t("relink.offline_desc"), juce::dontSendNotification);
    lblStatusWarning_.setFont(juce::Font(juce::FontOptions(13.0f)));
    lblStatusWarning_.setColour(juce::Label::textColourId, tk.textoSecundario);
    lblStatusWarning_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblStatusWarning_);

    lblTitleHeader_.setText(i18n::t("relink.rotulo_titulo"), juce::dontSendNotification);
    lblTitleHeader_.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblTitleHeader_.setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(lblTitleHeader_);

    lblTitleValue_.setText(itemTitle_.isEmpty() ? "Asset" : itemTitle_, juce::dontSendNotification);
    lblTitleValue_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    lblTitleValue_.setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(lblTitleValue_);

    lblExpectedHeader_.setText(i18n::t("relink.rotulo_localizacao_esperada"), juce::dontSendNotification);
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

    lblStorageHeader_.setText(i18n::t("relink.rotulo_armazenamento"), juce::dontSendNotification);
    lblStorageHeader_.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblStorageHeader_.setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(lblStorageHeader_);

    lblStorageValue_.setText(storageName_.isEmpty() ? i18n::t("relink.armazenamento_local") : storageName_, juce::dontSendNotification);
    lblStorageValue_.setFont(juce::Font(juce::FontOptions(13.0f)));
    lblStorageValue_.setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(lblStorageValue_);

    lblError_.setFont(juce::Font(juce::FontOptions(12.0f)));
    lblError_.setColour(juce::Label::textColourId, juce::Colour(0xffef4444)); // red
    lblError_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblError_);

    btnLocate_.setButtonText(i18n::t("dialogo.localizar_arquivo"));
    btnLocate_.setColour(juce::TextButton::buttonColourId, tk.acento);
    btnLocate_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnLocate_.onClick = [this] {
        lblError_.setText("", juce::dontSendNotification);

        juce::String fname = juce::File(expectedPath_).getFileName();
        fileChooser_ = std::make_unique<juce::FileChooser>(
            i18n::t("relink.localizar_arquivo_ativo").replace("{n}", fname),
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
                            .withTitle(i18n::t("relink.divergencia_checksum_titulo"))
                            .withMessage(i18n::t("relink.divergencia_checksum_msg")
                                             .replace("{o}", juce::String(val.expectedSha).substring(0, 16))
                                             .replace("{s}", juce::String(val.actualSha).substring(0, 16)))
                            .withButton(i18n::t("relink.btn_substituir_ativo"))
                            .withButton(i18n::t("dialogo.cancelar")),
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

    btnCancel_.setButtonText(i18n::t("dialogo.cancelar"));
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

void OfflineAssetRelinkDialog::lookAndFeelChanged() {
    lblHeader_.setText(i18n::t("relink.ativo_offline"), juce::dontSendNotification);
    lblStatusWarning_.setText(i18n::t("relink.offline_desc"), juce::dontSendNotification);
    lblTitleHeader_.setText(i18n::t("relink.rotulo_titulo"), juce::dontSendNotification);
    lblExpectedHeader_.setText(i18n::t("relink.rotulo_localizacao_esperada"), juce::dontSendNotification);
    lblStorageHeader_.setText(i18n::t("relink.rotulo_armazenamento"), juce::dontSendNotification);
    lblStorageValue_.setText(storageName_.isEmpty() ? i18n::t("relink.armazenamento_local") : storageName_, juce::dontSendNotification);
    btnLocate_.setButtonText(i18n::t("dialogo.localizar_arquivo"));
    btnCancel_.setButtonText(i18n::t("dialogo.cancelar"));
    repaint();
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
    opt.dialogTitle = i18n::t("relink.dialog_offline_titulo");
    opt.content.setOwned(dialog);
    opt.componentToCentreAround = nullptr;
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;

    opt.launchAsync();
}

} // namespace matriz::ui
