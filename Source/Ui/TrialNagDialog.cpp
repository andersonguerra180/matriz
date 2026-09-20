#include "TrialNagDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

bool TrialNagDialog::isTrial() {
#if defined(MATRIZ_IS_TRIAL) && MATRIZ_IS_TRIAL
    return true;
#else
    if (auto* app = juce::JUCEApplication::getInstance()) {
        auto name = app->getApplicationName();
        if (name.containsIgnoreCase("Trial"))
            return true;
    }
#if defined(MATRIZ_UI_APP_NAME_STRING)
    if (juce::String(MATRIZ_UI_APP_NAME_STRING).containsIgnoreCase("Trial"))
        return true;
#endif
    return false;
#endif
}

TrialNagDialog::TrialNagDialog() {
    const auto& tk = tema();

    btnComprar_ = std::make_unique<juce::TextButton>(i18n::t("trial.comprar"));
    btnComprar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnComprar_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnComprar_->onClick = [] {
        juce::URL("https://www.bunkeranalog.com/bkr").launchInDefaultBrowser();
    };
    addAndMakeVisible(*btnComprar_);

    auto waitText = i18n::t("trial.aguarde").replace("{n}", juce::String(segundosRestantes_));
    btnContinuar_ = std::make_unique<juce::TextButton>(waitText);
    btnContinuar_->setEnabled(false);
    btnContinuar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt.withAlpha(0.6f));
    btnContinuar_->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
    btnContinuar_->setColour(juce::TextButton::textColourOnId, tk.textoTerciario);
    btnContinuar_->onClick = [this] {
        if (segundosRestantes_ <= 0 && aoFechar)
            aoFechar();
    };
    addAndMakeVisible(*btnContinuar_);

    setSize(580, 440);
    startTimer(1000);
}

TrialNagDialog::~TrialNagDialog() {
    stopTimer();
}

void TrialNagDialog::timerCallback() {
    if (segundosRestantes_ > 1) {
        segundosRestantes_--;
        auto waitText = i18n::t("trial.aguarde").replace("{n}", juce::String(segundosRestantes_));
        btnContinuar_->setButtonText(waitText);
    } else if (segundosRestantes_ == 1) {
        segundosRestantes_ = 0;
        stopTimer();
        btnContinuar_->setEnabled(true);
        btnContinuar_->setButtonText(i18n::t("trial.continuar"));
        const auto& tk = tema();
        btnContinuar_->setColour(juce::TextButton::buttonColourId, tk.acento);
        btnContinuar_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
        btnContinuar_->setColour(juce::TextButton::textColourOnId, tk.textoSobreAcento);
        repaint();
    }
}

bool TrialNagDialog::keyPressed(const juce::KeyPress& key) {
    if (segundosRestantes_ <= 0 && (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey || key == juce::KeyPress::escapeKey)) {
        if (aoFechar) aoFechar();
        return true;
    }
    return false;
}

void TrialNagDialog::lookAndFeelChanged() {
    if (btnComprar_)
        btnComprar_->setButtonText(i18n::t("trial.comprar"));

    if (btnContinuar_) {
        if (segundosRestantes_ > 0)
            btnContinuar_->setButtonText(i18n::t("trial.aguarde").replace("{n}", juce::String(segundosRestantes_)));
        else
            btnContinuar_->setButtonText(i18n::t("trial.continuar"));
    }
    repaint();
}

void TrialNagDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto bounds = getLocalBounds().toFloat();

    // Dark sleek background with subtle border
    g.setColour(tk.fundo);
    g.fillRoundedRectangle(bounds, 10.0f);

    // Glowing subtle amber border
    g.setColour(juce::Colour(0xffff9900).withAlpha(0.7f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.5f);

    auto inner = bounds.reduced(24.0f, 20.0f);

    // Header Area: Badge + Titles
    auto headerRow = inner.removeFromTop(28.0f);

    // Badge "TRIAL EVALUATION"
    float badgeW = 140.0f;
    auto badgeRect = headerRow.removeFromRight(badgeW);
    g.setColour(juce::Colour(0xff2d2416));
    g.fillRoundedRectangle(badgeRect, 4.0f);
    g.setColour(juce::Colour(0xffff9900));
    g.drawRoundedRectangle(badgeRect, 4.0f, 1.0f);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(i18n::t("trial.badge"), badgeRect, juce::Justification::centred, false);

    // App Main Title
    g.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    g.setColour(tk.textoPrimario);
    g.drawText(i18n::t("trial.app_titulo"), headerRow, juce::Justification::centredLeft, true);

    // Subtitle / License notice
    auto subRow = inner.removeFromTop(20.0f);
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    g.setColour(juce::Colour(0xffffa826));
    g.drawText(i18n::t("trial.app_subtitulo"), subRow, juce::Justification::centredLeft, true);

    inner.removeFromTop(12.0f);

    // Recessed card for information
    auto cardArea = inner.removeFromTop(250.0f);
    g.setColour(tk.painel);
    g.fillRoundedRectangle(cardArea, 8.0f);
    g.setColour(tk.borda);
    g.drawRoundedRectangle(cardArea, 8.0f, 1.0f);

    auto cardInner = cardArea.reduced(18.0f, 16.0f);

    // Paragraph intro
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::plain)));
    g.setColour(tk.textoPrimario);
    auto introText = i18n::t("trial.paragrafo_intro");
    g.drawFittedText(introText, cardInner.removeFromTop(60.0f).toNearestInt(), juce::Justification::topLeft, 4);

    cardInner.removeFromTop(10.0f);

    // Feature items (Checklist)
    auto drawItem = [&](const juce::String& text, juce::Colour col) {
        auto itemRow = cardInner.removeFromTop(26.0f);
        g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::plain)));
        g.setColour(col);
        g.drawText(text, itemRow, juce::Justification::centredLeft, true);
    };

    drawItem(i18n::t("trial.item1"), juce::Colour(0xff2ecc71));
    drawItem(i18n::t("trial.item2"), juce::Colour(0xff2ecc71));
    drawItem(i18n::t("trial.item3"), juce::Colour(0xffff9900));

    cardInner.removeFromTop(12.0f);

    // Footer note inside card
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::plain)));
    g.setColour(tk.textoTerciario);
    g.drawFittedText(i18n::t("trial.rodape_apoio"), cardInner.toNearestInt(), juce::Justification::topLeft, 2);
}

void TrialNagDialog::resized() {
    auto r = getLocalBounds().reduced(24, 20);
    auto bottom = r.removeFromBottom(36);

    btnComprar_->setBounds(bottom.removeFromLeft(180));
    btnContinuar_->setBounds(bottom.removeFromRight(230));
}

void TrialNagDialog::exibirSeNecessario(juce::Component* parent) {
    if (!isTrial())
        return;

    static bool s_jaExibido = false;
    if (s_jaExibido)
        return;

    s_jaExibido = true;
    exibirModal(parent);
}

void TrialNagDialog::exibirModal(juce::Component* parent) {
    auto dlg = std::make_unique<TrialNagDialog>();
    dlg->setSize(580, 440);

    auto* rawDlg = dlg.get();

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dlg.release());
    opt.dialogTitle = i18n::t("trial.dialog_titulo");
    opt.dialogBackgroundColour = juce::Colours::transparentBlack;
    opt.escapeKeyTriggersCloseButton = false;
    opt.useNativeTitleBar = false;
    opt.resizable = false;

    if (parent != nullptr) {
        opt.componentToCentreAround = parent;
    }

    auto* win = opt.launchAsync();

    rawDlg->aoFechar = [win] {
        if (win) win->exitModalState(0);
    };
}

} // namespace matriz::ui
