#include "PreservationWorkspaceComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

PreservationWorkspaceComponent::PreservationWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto)
{
    labelTitulo_ = std::make_unique<juce::Label>();
    labelTitulo_->setText("Archive Preservation & Health Dashboard", juce::dontSendNotification);
    labelTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteTitulo, juce::Font::bold)));
    labelTitulo_->setColour(juce::Label::textColourId, tema().textoPrimario);
    addAndMakeVisible(*labelTitulo_);

    labelSubtitulo_ = std::make_unique<juce::Label>();
    labelSubtitulo_->setText("Monitor integrity status, missing backups, and metadata health.", juce::dontSendNotification);
    labelSubtitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    labelSubtitulo_->setColour(juce::Label::textColourId, tema().textoSecundario);
    addAndMakeVisible(*labelSubtitulo_);

    setInterceptsMouseClicks(true, true);
    recarregar();
}

PreservationWorkspaceComponent::~PreservationWorkspaceComponent() = default;

void PreservationWorkspaceComponent::recarregar() {
    auto& db = projeto_.projeto().registro();
    
    // 1. Total Assets
    totalAssets_ = 0;
    try {
        auto stmt = db.prepare("SELECT COUNT(*) FROM item");
        if (stmt.step()) totalAssets_ = stmt.columnInt(0);
    } catch (...) {}

    // 2. Verified Count
    verifiedCount_ = 0;
    try {
        auto stmt = db.prepare("SELECT COUNT(DISTINCT item_id) FROM arquivo WHERE checksum_verificado_em IS NOT NULL AND estado_presenca = 'presente'");
        if (stmt.step()) verifiedCount_ = stmt.columnInt(0);
    } catch (...) {}

    // 3. Single Copy Count
    singleCopyCount_ = static_cast<int>(projeto_.itensDaColecaoEmbutida("vulneraveis").size());

    // 4. Checksum Problems Count
    checksumProblemsCount_ = 0;
    try {
        auto stmt = db.prepare("SELECT COUNT(DISTINCT item_id) FROM arquivo WHERE estado_presenca = 'corrompido'");
        if (stmt.step()) checksumProblemsCount_ = stmt.columnInt(0);
    } catch (...) {}

    // 5. Incomplete Count
    incompleteCount_ = static_cast<int>(projeto_.itensDaColecaoEmbutida("incompletos").size());

    // 6. Review Count
    reviewCount_ = static_cast<int>(projeto_.itensDaColecaoEmbutida("revisao").size());

    // Health Assessment
    const auto& tk = tema();
    if (checksumProblemsCount_ > 0) {
        textoHealth_ = "CRITICAL";
        corHealth_ = tk.perigo;
    } else if (singleCopyCount_ > 0 || incompleteCount_ > 0) {
        textoHealth_ = "WARNING";
        corHealth_ = tk.alerta;
    } else {
        textoHealth_ = "GOOD";
        corHealth_ = tk.estadoQcOk;
    }

    // Refresh Metrica structures
    metricas_ = {
        { "total", "TOTAL ASSETS", totalAssets_, {}, false },
        { "verified", "VERIFIED", verifiedCount_, {}, false },
        { "vulneraveis", "NEEDS BACKUP / SINGLE COPY", singleCopyCount_, {}, false },
        { "corrompido", "CHECKSUM PROBLEMS", checksumProblemsCount_, {}, false },
        { "incompletos", "INCOMPLETE METADATA", incompleteCount_, {}, false },
        { "revisao", "NEEDS REVIEW", reviewCount_, {}, false }
    };

    repaint();
}

void PreservationWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    
    // Draw background
    g.fillAll(tk.fundo);

    // Draw Health Banner Box
    g.setColour(tk.painel);
    g.fillRoundedRectangle(boundsBanner_.toFloat(), tk.raioMedio);
    g.setColour(tk.borda);
    g.drawRoundedRectangle(boundsBanner_.toFloat(), tk.raioMedio, 1.0f);

    // Health border highlights
    g.setColour(corHealth_);
    g.fillRect(boundsBanner_.getX(), boundsBanner_.getY(), 8, boundsBanner_.getHeight());

    // Health Banner Text
    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    g.drawText("ARCHIVE HEALTH STATUS", boundsBanner_.getX() + 24, boundsBanner_.getY() + 12, boundsBanner_.getWidth() - 40, 16, juce::Justification::centredLeft, true);

    g.setColour(corHealth_);
    g.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
    g.drawText(textoHealth_, boundsBanner_.getX() + 24, boundsBanner_.getY() + 32, boundsBanner_.getWidth() - 40, 36, juce::Justification::centredLeft, true);

    // Draw Metrics Grid Cards
    for (const auto& met : metricas_) {
        // Draw card background
        g.setColour(met.hover ? tk.painelAlt : tk.painel);
        g.fillRoundedRectangle(met.bounds.toFloat(), tk.raioMedio);
        
        // Highlight problematic counts
        if (met.contagem > 0 && (met.chave == "corrompido" || met.chave == "vulneraveis" || met.chave == "revisao")) {
            g.setColour(met.chave == "corrompido" ? tk.perigo : tk.alerta);
            g.drawRoundedRectangle(met.bounds.toFloat(), tk.raioMedio, 2.0f);
        } else {
            g.setColour(tk.borda);
            g.drawRoundedRectangle(met.bounds.toFloat(), tk.raioMedio, 1.0f);
        }

        // Draw Card Title
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(met.titulo, met.bounds.reduced(12, 8).withHeight(20), juce::Justification::topLeft, true);

        // Draw Card Count
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(32.0f, juce::Font::bold)));
        g.drawText(juce::String(met.contagem), met.bounds.reduced(12, 8).withTrimmedTop(24), juce::Justification::bottomLeft, true);
    }
}

void PreservationWorkspaceComponent::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds().reduced(tk.espacoGrande);

    // Titles
    labelTitulo_->setBounds(area.removeFromTop(32));
    labelSubtitulo_->setBounds(area.removeFromTop(24));
    area.removeFromTop(tk.espacoGrande);

    // Health Banner
    boundsBanner_ = area.removeFromTop(80);
    area.removeFromTop(tk.espacoGrande);

    // Grid details (3 columns x 2 rows)
    int columns = 3;
    int rows = 2;
    int gap = tk.espacoGrande;
    int cardWidth = (area.getWidth() - (columns - 1) * gap) / columns;
    int cardHeight = (area.getHeight() - (rows - 1) * gap) / rows;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            size_t idx = static_cast<size_t>(r * columns + c);
            if (idx < metricas_.size()) {
                metricas_[idx].bounds = juce::Rectangle<int>(
                    area.getX() + c * (cardWidth + gap),
                    area.getY() + r * (cardHeight + gap),
                    cardWidth,
                    cardHeight
                );
            }
        }
    }
}

void PreservationWorkspaceComponent::mouseUp(const juce::MouseEvent& e) {
    if (e.mouseWasClicked()) {
        auto pos = e.getPosition();
        for (const auto& met : metricas_) {
            if (met.bounds.contains(pos)) {
                if (aoClicarMetrica) aoClicarMetrica(met.chave);
                break;
            }
        }
    }
}

void PreservationWorkspaceComponent::mouseMove(const juce::MouseEvent& e) {
    auto pos = e.getPosition();
    bool mudou = false;
    for (auto& met : metricas_) {
        bool hover = met.bounds.contains(pos);
        if (met.hover != hover) {
            met.hover = hover;
            mudou = true;
        }
    }
    if (mudou) repaint();
}

void PreservationWorkspaceComponent::mouseExit(const juce::MouseEvent&) {
    bool mudou = false;
    for (auto& met : metricas_) {
        if (met.hover) {
            met.hover = false;
            mudou = true;
        }
    }
    if (mudou) repaint();
}

} // namespace matriz::ui
