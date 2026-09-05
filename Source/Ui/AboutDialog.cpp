#include "AboutDialog.h"
#include "Tokens.h"
#include <AssetsBinaryData.h>

namespace matriz::ui {

AboutDialog::AboutDialog() {
    auto imgData = juce::MemoryBlock(AssetsBinaryData::splash_png, AssetsBinaryData::splash_pngSize);
    splashImage_ = juce::ImageFileFormat::loadFrom(imgData.getData(), imgData.getSize());

    const auto& tk = tema();

    btnClose_ = std::make_unique<juce::TextButton>("CLOSE");
    btnClose_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnClose_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnClose_->onClick = [this] {
        if (aoFechar) aoFechar();
    };
    addAndMakeVisible(*btnClose_);

    setSize(540, 480);
}

void AboutDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto bounds = getLocalBounds().toFloat();

    // Background panel with subtle border
    g.setColour(tk.fundo);
    g.fillRoundedRectangle(bounds, 8.0f);

    // Splash Banner Area
    float splashHeight = 270.0f;
    auto splashBounds = bounds.removeFromTop(splashHeight);

    if (splashImage_.isValid()) {
        juce::Graphics::ScopedSaveState sss(g);
        juce::Path clipPath;
        clipPath.addRoundedRectangle(0.0f, 0.0f, static_cast<float>(getWidth()), splashHeight, 8.0f, 8.0f, true, true, false, false);
        g.reduceClipRegion(clipPath);

        g.drawImage(splashImage_,
                    splashBounds,
                    juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    } else {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(splashBounds, 8.0f);
    }

    // Divider line below splash
    g.setColour(tk.borda);
    g.drawHorizontalLine(static_cast<int>(splashHeight), 0.0f, static_cast<float>(getWidth()));

    // Info card area
    auto infoBounds = bounds.reduced(24.0f, 16.0f);

    // Title: BKR Matriz
    g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    g.setColour(tk.textoPrimario);
    g.drawText("BKR MATRIZ", infoBounds.removeFromTop(26.0f), juce::Justification::centredLeft, true);

    // Subtitle / Version tag
    auto versionRow = infoBounds.removeFromTop(20.0f);
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.setColour(tk.acento);
    g.drawText("Version 1.0", versionRow, juce::Justification::centredLeft, true);

    infoBounds.removeFromTop(6.0f);

    // Developed by A. Guerra
    auto devRow = infoBounds.removeFromTop(20.0f);
    g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::plain)));
    g.setColour(tk.textoSecundario);
    g.drawText("Developed by A. Guerra", devRow, juce::Justification::centredLeft, true);

    // BKR Systems
    auto brandRow = infoBounds.removeFromTop(18.0f);
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    g.setColour(tk.textoTerciario);
    g.drawText("BKR Systems", brandRow, juce::Justification::centredLeft, true);

    // Copyright
    auto copyRow = infoBounds.removeFromTop(16.0f);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::plain)));
    g.setColour(tk.textoTerciario.withAlpha(0.7f));
    g.drawText("© 2026 BKR Systems. All rights reserved.", copyRow, juce::Justification::centredLeft, true);

    // Outline
    g.setColour(tk.borda);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f, 1.0f);
}

void AboutDialog::resized() {
    auto r = getLocalBounds().reduced(20);
    auto bottom = r.removeFromBottom(34);
    btnClose_->setBounds(bottom.removeFromRight(100));
}

void AboutDialog::mouseDown(const juce::MouseEvent&) {
    // Dismissing by clicking background if desired
}

void AboutDialog::exibirModal() {
    auto dlg = std::make_unique<AboutDialog>();
    dlg->setSize(540, 480);

    auto* rawDlg = dlg.get();

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dlg.release());
    opt.dialogTitle = "About BKR Matriz";
    opt.dialogBackgroundColour = juce::Colours::transparentBlack;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = false;
    opt.resizable = false;

    auto* win = opt.launchAsync();

    rawDlg->aoFechar = [win] {
        if (win) win->exitModalState(0);
    };
}

} // namespace matriz::ui
