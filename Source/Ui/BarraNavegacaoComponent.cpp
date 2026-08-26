#include "BarraNavegacaoComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

BarraNavegacaoComponent::BarraNavegacaoComponent() {
    tabs_ = {
        { Tab::Home, "HOME", {}, false },
        { Tab::Intake, "INTAKE", {}, false },
        { Tab::Grid, "GRID", {}, false },
        { Tab::Analytics, "ANALYTICS", {}, false },
        { Tab::Tree, "TREE", {}, false },
        { Tab::Backup, "BACKUP", {}, false }
    };

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
    
    // Draw BKR ACERVO brand text
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    g.drawText("BKR ACERVO", 16, 0, 120, getHeight(), juce::Justification::centredLeft, true);
    
    // Draw Tabs
    for (const auto& tab : tabs_) {
        bool ativo = (tab.tab == selectedTab_);
        
        if (ativo) {
            g.setColour(tk.acento);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        } else if (tab.hover) {
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
            g.fillRect(tab.bounds.getX() + 8, getHeight() - 3, tab.bounds.getWidth() - 16, 3);
        }
    }
}

void BarraNavegacaoComponent::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds();
    
    // Brand padding
    area.removeFromLeft(150);
    
    // Tabs sizing
    int tabWidth = 120;
    for (auto& tab : tabs_) {
        tab.bounds = area.removeFromLeft(tabWidth);
    }
    
    // Close button sizing
    int btnWidth = 130;
    int btnHeight = 28;
    botaoFechar_->setBounds(getWidth() - btnWidth - 16, (getHeight() - btnHeight) / 2, btnWidth, btnHeight);
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
    for (auto& tab : tabs_) {
        bool hover = tab.bounds.contains(pos);
        if (tab.hover != hover) {
            tab.hover = hover;
            mudou = true;
        }
    }
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
    if (mudou) repaint();
}

} // namespace matriz::ui
