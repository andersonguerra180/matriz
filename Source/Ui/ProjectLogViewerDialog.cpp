#include "ProjectLogViewerDialog.h"
#include "Tokens.h"

namespace matriz::ui {

ProjectLogViewerDialog::ProjectLogViewerDialog(matriz::model::ProjectLog log)
    : log_(std::move(log)) {
    const auto& tk = tema();

    lblTitle_.setText("PROJECT LOG (log.md)", juce::dontSendNotification);
    lblTitle_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    lblTitle_.setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(lblTitle_);

    lblSubtitle_.setText("Location: " + log_.getLogFile().getFullPathName(), juce::dontSendNotification);
    lblSubtitle_.setFont(juce::Font(juce::FontOptions(12.0f)));
    lblSubtitle_.setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(lblSubtitle_);

    txtEditor_.setMultiLine(true);
    txtEditor_.setReturnKeyStartsNewLine(true);
    txtEditor_.setScrollbarsShown(true);
    txtEditor_.setFont(juce::Font(juce::FontOptions(13.0f)));
    txtEditor_.setColour(juce::TextEditor::backgroundColourId, tk.fundo);
    txtEditor_.setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    txtEditor_.setColour(juce::TextEditor::outlineColourId, tk.borda);
    txtEditor_.setText(log_.readContent(), false);
    addAndMakeVisible(txtEditor_);

    lblStatus_.setFont(juce::Font(juce::FontOptions(12.0f)));
    lblStatus_.setColour(juce::Label::textColourId, juce::Colour(0xff22c55e)); // Green
    addAndMakeVisible(lblStatus_);

    btnSave_.setButtonText("SAVE LOG");
    btnSave_.setColour(juce::TextButton::buttonColourId, tk.acento);
    btnSave_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnSave_.onClick = [this] {
        if (log_.saveContent(txtEditor_.getText())) {
            lblStatus_.setText("Changes saved to log.md successfully.", juce::dontSendNotification);
        } else {
            lblStatus_.setText("Error saving log.md.", juce::dontSendNotification);
        }
    };
    addAndMakeVisible(btnSave_);

    btnClose_.setButtonText("CLOSE");
    btnClose_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnClose_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnClose_.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
    };
    addAndMakeVisible(btnClose_);

    setSize(720, 520);
}

void ProjectLogViewerDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);
    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);
}

void ProjectLogViewerDialog::resized() {
    auto r = getLocalBounds().reduced(20);

    lblTitle_.setBounds(r.removeFromTop(24));
    lblSubtitle_.setBounds(r.removeFromTop(18));
    r.removeFromTop(12);

    auto bottom = r.removeFromBottom(36);
    btnClose_.setBounds(bottom.removeFromRight(100));
    bottom.removeFromRight(10);
    btnSave_.setBounds(bottom.removeFromRight(120));
    lblStatus_.setBounds(bottom);

    r.removeFromBottom(10);
    txtEditor_.setBounds(r);
}

void ProjectLogViewerDialog::showModal(matriz::model::ProjectLog log) {
    auto* dialog = new ProjectLogViewerDialog(std::move(log));
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "Project Log (log.md)";
    options.dialogBackgroundColour = tema().painel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    options.launchAsync();
}

} // namespace matriz::ui
