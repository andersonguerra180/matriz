#include "BarraNavegacaoComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

BarraNavegacaoComponent::BarraNavegacaoComponent() {
    reconstruirTabs();

    botaoFechar_ = std::make_unique<juce::TextButton>("CLOSE PROJECT");
    botaoFechar_->onClick = [this] { if (aoClicarFechar) aoClicarFechar(); };
    
    // Style Close button
    const auto& tk = tema();
    botaoFechar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    botaoFechar_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    addAndMakeVisible(*botaoFechar_);
    
    setInterceptsMouseClicks(true, true);
}

BarraNavegacaoComponent::~BarraNavegacaoComponent() = default;

void BarraNavegacaoComponent::setProjectInfo(const juce::String& projectName, bool isCatalog) {
    isCatalog_ = isCatalog;
    juce::String prefixo = isCatalog ? "CATALOG" : "COLLECTION";
    brandText_ = projectName.isNotEmpty() ? prefixo + " - " + projectName : prefixo;
    reconstruirTabs();
    resized();
    repaint();
}

void BarraNavegacaoComponent::setHasParentCatalog(bool hasParent) {
    hasParentCatalog_ = hasParent;
    const auto& tk = tema();
    if (botaoFechar_) {
        if (hasParentCatalog_) {
            botaoFechar_->setButtonText("RETURN TO CATALOG");
            botaoFechar_->setColour(juce::TextButton::textColourOffId, tk.acento);
        } else {
            botaoFechar_->setButtonText("CLOSE PROJECT");
            botaoFechar_->setColour(juce::TextButton::textColourOffId, tk.perigo);
        }
    }
    resized();
    repaint();
}

void BarraNavegacaoComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    if (botaoFechar_) {
        botaoFechar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        botaoFechar_->setColour(juce::TextButton::textColourOffId, hasParentCatalog_ ? tk.acento : tk.perigo);
    }
    repaint();
}

void BarraNavegacaoComponent::reconstruirTabs() {
    tabs_.clear();
    if (isCatalog_) {
        tabs_.push_back({ Tab::Catalog, "COLLECTIONS", {}, false });
        tabs_.push_back({ Tab::Duplicates, "DUPLICATES", {}, false });
        tabs_.push_back({ Tab::Analytics, "ANALYTICS", {}, false });
        tabs_.push_back({ Tab::Storage, "STORAGE", {}, false });
        tabs_.push_back({ Tab::Backup, "BACKUP", {}, false });
    } else {
        tabs_.push_back({ Tab::Intake, "INTAKE", {}, false });
        tabs_.push_back({ Tab::Grid, "GRID", {}, false });
        tabs_.push_back({ Tab::Duplicates, "DUPLICATES", {}, false });
        tabs_.push_back({ Tab::Analytics, "ANALYTICS", {}, false });
        tabs_.push_back({ Tab::Tree, "TREE", {}, false });
        tabs_.push_back({ Tab::Storage, "STORAGE", {}, false });
        tabs_.push_back({ Tab::Backup, "BACKUP", {}, false });
    }
}

void BarraNavegacaoComponent::setSelectedTab(Tab tab) {
    if (selectedTab_ != tab) {
        selectedTab_ = tab;
        repaint();
    }
}

void BarraNavegacaoComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    
    // Background fill
    g.fillAll(tk.painel);
    
    // Bottom border
    g.setColour(tk.borda);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    
    // Draw Brand text (COLLECTION - project or CATALOG - project)
    auto fonteBrand = juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold));
    int maxBrandW = 320;
    g.setColour(tk.textoPrimario);
    g.setFont(fonteBrand);
    g.drawText(brandText_, 16, 0, maxBrandW, getHeight(), juce::Justification::centredLeft, true);
    
    // Draw Tabs
    for (const auto& tab : tabs_) {
        bool ativo = (tab.tab == selectedTab_);
        
        // Draw button-like background
        if (ativo) {
            g.setColour(tk.painelAlt);
            g.fillRoundedRectangle(tab.bounds.reduced(6, 6).toFloat(), tk.raioPequeno);
            g.setColour(tk.acento);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        } else if (tab.hover) {
            g.setColour(tk.painelAlt.withAlpha(0.6f));
            g.fillRoundedRectangle(tab.bounds.reduced(6, 6).toFloat(), tk.raioPequeno);
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        } else {
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        }
        
        g.drawText(tab.label, tab.bounds, juce::Justification::centred, true);
        
        // Active indicator line
        if (ativo) {
            g.setColour(tk.acento);
            g.fillRect(tab.bounds.getX() + 16, getHeight() - 3, tab.bounds.getWidth() - 32, 3);
        }
    }

    // Draw persistent and ostensive "1.0 TRIAL - NOT FOR SALE" badge between BACKUP tab and CLOSE PROJECT button
    if (!trialBadgeBounds_.isEmpty()) {
        auto badgeArea = trialBadgeBounds_.toFloat().reduced(2.0f, 4.0f);
        
        // Background
        g.setColour(juce::Colour(0x2ef87171));
        g.fillRoundedRectangle(badgeArea, 4.0f);
        
        // Border
        g.setColour(juce::Colour(0xffef4444));
        g.drawRoundedRectangle(badgeArea, 4.0f, 1.5f);
        
        // Glowing bold text
        g.setColour(juce::Colour(0xfffee2e2));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText("1.0 TRIAL — NOT FOR SALE", trialBadgeBounds_, juce::Justification::centred, true);
    }
}

void BarraNavegacaoComponent::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds();
    
    // Brand padding (dynamic based on brand text width)
    auto fonteBrand = juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold));
    int brandWidth = std::min(340, juce::GlyphArrangement::getStringWidthInt(fonteBrand, brandText_) + 32);
    brandWidth = std::max(160, brandWidth);
    area.removeFromLeft(brandWidth);
    
    // Tabs sizing
    int tabWidth = isCatalog_ ? 115 : 100;
    for (auto& tab : tabs_) {
        tab.bounds = area.removeFromLeft(tabWidth);
    }
    
    // Close button sizing
    int btnWidth = hasParentCatalog_ ? 175 : 130;
    int btnHeight = 28;
    int btnX = getWidth() - btnWidth - 16;
    int btnY = (getHeight() - btnHeight) / 2;
    botaoFechar_->setBounds(btnX, btnY, btnWidth, btnHeight);

    // Calculate TRIAL - NOT FOR SALE badge bounds between last tab (BACKUP) and CLOSE PROJECT button
    int lastTabRight = tabs_.empty() ? brandWidth : tabs_.back().bounds.getRight();
    int spaceAvailable = btnX - lastTabRight - 20;
    if (spaceAvailable >= 140) {
        int badgeWidth = std::min(260, spaceAvailable);
        int badgeX = lastTabRight + (btnX - lastTabRight - badgeWidth) / 2;
        int badgeHeight = 26;
        int badgeY = (getHeight() - badgeHeight) / 2;
        trialBadgeBounds_ = juce::Rectangle<int>(badgeX, badgeY, badgeWidth, badgeHeight);
    } else {
        trialBadgeBounds_ = {};
    }
}

void BarraNavegacaoComponent::mouseDown(const juce::MouseEvent& e) {
    auto pos = e.getPosition();
    for (const auto& tab : tabs_) {
        if (tab.bounds.contains(pos)) {
            setSelectedTab(tab.tab);
            if (aoMudarTab) aoMudarTab(tab.tab);
            break;
        }
    }
}

void BarraNavegacaoComponent::mouseMove(const juce::MouseEvent& e) {
    auto pos = e.getPosition();
    bool mudou = false;
    bool hoverAny = false;
    for (auto& tab : tabs_) {
        bool hover = tab.bounds.contains(pos);
        if (hover) hoverAny = true;
        if (tab.hover != hover) {
            tab.hover = hover;
            mudou = true;
        }
    }
    setMouseCursor(hoverAny ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (mudou) repaint();
}

void BarraNavegacaoComponent::mouseExit(const juce::MouseEvent&) {
    bool mudou = false;
    for (auto& tab : tabs_) {
        if (tab.hover) {
            tab.hover = false;
            mudou = true;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
    if (mudou) repaint();
}

juce::String BarraNavegacaoComponent::getTooltip() {
    auto pos = getMouseXYRelative();
    for (const auto& tab : tabs_) {
        if (tab.bounds.contains(pos)) {
            switch (tab.tab) {
                case Tab::Catalog: return "Manage and explore collections linked to this catalog";
                case Tab::Intake: return "Manage recently ingested files awaiting verification to GRID";
                case Tab::Grid: return "Browse, filter, and edit metadata of all assets";
                case Tab::Duplicates: return "Scan and resolve duplicate files in active project";
                case Tab::Analytics: return "View statistics, charts, and preservation metrics";
                case Tab::Tree: return "Explore assets structure via vault directories tree";
                case Tab::Backup: return "Plan, check conflicts, and consolidate backup publication package";
                case Tab::Storage: return "Inspect and manage physical backup storage devices and history";
            }
        }
    }
    if (botaoFechar_ && botaoFechar_->getBounds().contains(pos)) {
        return "Close project and return to start screen";
    }
    if (!trialBadgeBounds_.isEmpty() && trialBadgeBounds_.contains(pos)) {
        return "BKR Matriz 1.0 — Trial Evaluation Version (Not For Commercial Sale)";
    }
    return "";
}

} // namespace matriz::ui
