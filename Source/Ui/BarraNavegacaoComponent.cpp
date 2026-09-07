#include "BarraNavegacaoComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

BarraNavegacaoComponent::BarraNavegacaoComponent() {
    reconstruirTabs();

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    botaoFechar_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("FECHAR PROJETO") : "CLOSE PROJECT");
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
    projectName_ = projectName;
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    juce::String prefixo = isCatalog ? (isPt ? juce::String::fromUTF8("CATÁLOGO") : "CATALOG")
                                     : (isPt ? juce::String::fromUTF8("COLEÇÃO") : "COLLECTION");
    brandText_ = projectName.isNotEmpty() ? prefixo + " - " + projectName : prefixo;
    reconstruirTabs();
    resized();
    repaint();
}

void BarraNavegacaoComponent::setHasParentCatalog(bool hasParent) {
    hasParentCatalog_ = hasParent;
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    if (botaoFechar_) {
        if (hasParentCatalog_) {
            botaoFechar_->setButtonText(isPt ? juce::String::fromUTF8("VOLTAR AO CATÁLOGO") : "RETURN TO CATALOG");
            botaoFechar_->setColour(juce::TextButton::textColourOffId, tk.acento);
        } else {
            botaoFechar_->setButtonText(isPt ? juce::String::fromUTF8("FECHAR PROJETO") : "CLOSE PROJECT");
            botaoFechar_->setColour(juce::TextButton::textColourOffId, tk.perigo);
        }
    }
    resized();
    repaint();
}

void BarraNavegacaoComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    if (botaoFechar_) {
        botaoFechar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        botaoFechar_->setColour(juce::TextButton::textColourOffId, hasParentCatalog_ ? tk.acento : tk.perigo);
        if (hasParentCatalog_) {
            botaoFechar_->setButtonText(isPt ? juce::String::fromUTF8("VOLTAR AO CATÁLOGO") : "RETURN TO CATALOG");
        } else {
            botaoFechar_->setButtonText(isPt ? juce::String::fromUTF8("FECHAR PROJETO") : "CLOSE PROJECT");
        }
    }
    juce::String prefixo = isCatalog_ ? (isPt ? juce::String::fromUTF8("CATÁLOGO") : "CATALOG")
                                      : (isPt ? juce::String::fromUTF8("COLEÇÃO") : "COLLECTION");
    brandText_ = projectName_.isNotEmpty() ? prefixo + " - " + projectName_ : prefixo;
    reconstruirTabs();
    resized();
    repaint();
}

void BarraNavegacaoComponent::reconstruirTabs() {
    tabs_.clear();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    if (isCatalog_) {
        tabs_.push_back({ Tab::Catalog, isPt ? juce::String::fromUTF8("COLEÇÕES") : "COLLECTIONS", {}, {}, false });
        tabs_.push_back({ Tab::Duplicates, isPt ? juce::String::fromUTF8("DUPLICATAS") : "DUPLICATES", {}, {}, false });
        tabs_.push_back({ Tab::Analytics, isPt ? juce::String::fromUTF8("ARMAZENAMENTO") : "STORAGE", {}, {}, false });
        tabs_.push_back({ Tab::Storage, isPt ? juce::String::fromUTF8("DISCO") : "DISK", {}, {}, false });
        tabs_.push_back({ Tab::Backup, "BACKUP", {}, {}, false });
    } else {
        tabs_.push_back({ Tab::Intake, isPt ? juce::String::fromUTF8("INGESTÃO") : "INTAKE", {}, {}, false });
        tabs_.push_back({ Tab::Grid, isPt ? juce::String::fromUTF8("METADADOS") : "METADATA", {}, {}, false });
        tabs_.push_back({ Tab::Duplicates, isPt ? juce::String::fromUTF8("DUPLICATAS") : "DUPLICATES", {}, {}, false });
        tabs_.push_back({ Tab::Analytics, isPt ? juce::String::fromUTF8("ARMAZENAMENTO") : "STORAGE", {}, {}, false });
        tabs_.push_back({ Tab::Tree, isPt ? juce::String::fromUTF8("MAPA") : "TREEMAP", {}, {}, false });
        tabs_.push_back({ Tab::Storage, isPt ? juce::String::fromUTF8("DISCO") : "DISK", {}, {}, false });
        tabs_.push_back({ Tab::Backup, "BACKUP", {}, {}, false });
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
    
    // Header background fill - 20% brighter in both light and dark modes
    g.fillAll(tk.painel.brighter(0.20f));
    
    // Bottom border separating header from content
    g.setColour(tk.borda);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    
    // Draw Brand text (COLLECTION - project or CATALOG - project)
    auto fonteBrand = juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold));
    int maxBrandW = 320;
    g.setColour(tk.textoPrimario);
    g.setFont(fonteBrand);
    g.drawText(brandText_, 16, 0, maxBrandW, getHeight(), juce::Justification::centredLeft, true);
    
    // Draw Tabs in Folder-Tab style with rounded top corners
    for (const auto& tab : tabs_) {
        bool ativo = (tab.tab == selectedTab_);
        
        auto tb = tab.bounds.toFloat();
        float r = 5.0f;
        float bx = tb.getX();
        float by = tb.getY();
        float bw = tb.getWidth();
        float bh = tb.getHeight();
        
        // Build folder tab path (rounded top corners, straight vertical sides, flush bottom)
        juce::Path tabPath;
        tabPath.startNewSubPath(bx, by + bh);
        tabPath.lineTo(bx, by + r);
        tabPath.addArc(bx, by, r * 2.0f, r * 2.0f, juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f, false);
        tabPath.lineTo(bx + bw - r, by);
        tabPath.addArc(bx + bw - r * 2.0f, by, r * 2.0f, r * 2.0f, juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi, false);
        tabPath.lineTo(bx + bw, by + bh);
        
        if (ativo) {
            // Active folder tab: elevated background matching the workspace panel below
            juce::Path fillPath(tabPath);
            fillPath.closeSubPath();
            g.setColour(tk.painel);
            g.fillPath(fillPath);
            
            // Highlighted folder tab border
            g.setColour(tk.acento.withAlpha(0.75f));
            g.strokePath(tabPath, juce::PathStrokeType(1.5f));
            
            // Accent stripe on the top of the active tab
            juce::Path topStripe;
            topStripe.startNewSubPath(bx + r, by);
            topStripe.lineTo(bx + bw - r, by);
            g.setColour(tk.acento);
            g.strokePath(topStripe, juce::PathStrokeType(2.5f));
            
            g.setColour(tk.acento);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        } else if (tab.hover) {
            juce::Path fillPath(tabPath);
            fillPath.closeSubPath();
            g.setColour(tk.painelAlt.withAlpha(0.40f));
            g.fillPath(fillPath);
            
            g.setColour(tk.borda.withAlpha(0.60f));
            g.strokePath(tabPath, juce::PathStrokeType(1.0f));
            
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        } else {
            juce::Path fillPath(tabPath);
            fillPath.closeSubPath();
            g.setColour(tk.painelAlt.withAlpha(0.15f));
            g.fillPath(fillPath);
            
            g.setColour(tk.borda.withAlpha(0.40f));
            g.strokePath(tabPath, juce::PathStrokeType(0.8f));
            
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        }
        
        g.drawText(tab.label, tab.bounds, juce::Justification::centred, true);
        
        // Draw chronological workflow arrow (──›) between sequential tabs with prominent accent contrast
        if (!tab.sepBounds.isEmpty()) {
            auto cx = static_cast<float>(tab.sepBounds.getCentreX());
            auto cy = static_cast<float>(tab.sepBounds.getCentreY());
            juce::Path arrow;
            // Horizontal arrow shaft
            arrow.startNewSubPath(cx - 4.5f, cy);
            arrow.lineTo(cx + 4.0f, cy);
            // Arrowhead
            arrow.startNewSubPath(cx + 0.5f, cy - 3.5f);
            arrow.lineTo(cx + 4.5f, cy);
            arrow.lineTo(cx + 0.5f, cy + 3.5f);
            g.setColour(tk.acento.withAlpha(0.85f));
            g.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
}

void BarraNavegacaoComponent::resized() {
    const auto& tk = tema();
    
    // Close button sizing on the right
    int btnWidth = hasParentCatalog_ ? 175 : 130;
    int btnHeight = 28;
    botaoFechar_->setBounds(getWidth() - btnWidth - 16, (getHeight() - btnHeight) / 2, btnWidth, btnHeight);
    
    // Brand padding (dynamic based on brand text width) on the left
    auto fonteBrand = juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold));
    int brandWidth = std::min(340, juce::GlyphArrangement::getStringWidthInt(fonteBrand, brandText_) + 32);
    brandWidth = std::max(160, brandWidth);
    
    int leftBoundary = brandWidth + 16;
    int rightBoundary = getWidth() - btnWidth - 24;
    int availableWidth = std::max(0, rightBoundary - leftBoundary);
    
    // Measure tabs width dynamically to ensure words like "ARMAZENAMENTO" fit without truncation
    auto fonteTab = juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold));
    std::vector<int> tabWidths;
    tabWidths.reserve(tabs_.size());
    int totalTabsW = 0;
    const int sepWidth = 18;
    
    for (const auto& tab : tabs_) {
        int textW = juce::GlyphArrangement::getStringWidthInt(fonteTab, tab.label);
        // Add 26px padding so text never clips, with minimum width of 88px
        int w = std::max(88, textW + 26);
        tabWidths.push_back(w);
        totalTabsW += w;
    }
    
    if (tabs_.size() > 1) {
        totalTabsW += static_cast<int>(tabs_.size() - 1) * sepWidth;
    }
    
    // Center the entire tabs cluster between leftBoundary and rightBoundary
    int startX = leftBoundary + std::max(0, (availableWidth - totalTabsW) / 2);
    int tabY = 5;
    int tabH = getHeight() - 6; // Rests on bottom edge like a folder tab
    
    for (size_t i = 0; i < tabs_.size(); ++i) {
        tabs_[i].bounds = juce::Rectangle<int>(startX, tabY, tabWidths[i], tabH);
        startX += tabWidths[i];
        
        if (i + 1 < tabs_.size()) {
            tabs_[i].sepBounds = juce::Rectangle<int>(startX, tabY, sepWidth, tabH);
            startX += sepWidth;
        } else {
            tabs_[i].sepBounds = {};
        }
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
                case Tab::Intake: return "Manage recently ingested files awaiting verification to METADATA";
                case Tab::Grid: return "Browse, filter, and edit metadata of all assets";
                case Tab::Duplicates: return "Scan and resolve duplicate files in active project";
                case Tab::Analytics: return "View storage capacity, charts, and preservation metrics";
                case Tab::Tree: return "Explore assets structure via vault directories treemap";
                case Tab::Backup: return "Plan, check conflicts, and consolidate backup publication package";
                case Tab::Storage: return "Inspect and manage physical disk devices and history";
            }
        }
    }
    if (botaoFechar_ && botaoFechar_->getBounds().contains(pos)) {
        return "Close project and return to start screen";
    }
    return "";
}

} // namespace matriz::ui
