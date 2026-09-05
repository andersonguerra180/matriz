#include "VulnerabilidadesDialog.h"
#include "Tokens.h"
#include "../Vault/Reconciliacao.h"
#include <map>

namespace matriz::ui {

VulnerabilidadesDialog::VulnerabilidadesDialog(ProjetoAberto& projeto)
    : projeto_(projeto) {
    const auto& tk = tema();

    lblTitulo_ = std::make_unique<juce::Label>("lblTitulo", "BACKUP HEALTH & VULNERABILITY ANALYTICS");
    lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    lblSubtitulo_ = std::make_unique<juce::Label>(
        "lblSubtitulo",
        "Categorized risk breakdown for project assets. Click any issue card to open and filter affected files directly in the Mosaic Grid.");
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblSubtitulo_);

    btnFechar_ = std::make_unique<juce::TextButton>("CLOSE");
    btnFechar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnFechar_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnFechar_->onClick = [this] {
        if (aoFechar) aoFechar();
    };
    addAndMakeVisible(*btnFechar_);

    carregarVulnerabilidades();
}

void VulnerabilidadesDialog::carregarVulnerabilidades() {
    categorias_.clear();
    auto& db = projeto_.projeto().registro();

    // 1. Missing Required Metadata
    VulnerabilidadeCategoria catMeta;
    catMeta.id = "missing_metadata";
    catMeta.titulo = "MISSING METADATA";
    catMeta.descricao = "Assets missing required catalog metadata (Release/Year, Media Type, or Collection Type).";
    catMeta.corAcento = juce::Colour(0xfff59e0b); // Amber

    try {
        auto stmt = db.prepare(
            "SELECT id FROM item "
            "WHERE (ano IS NULL OR ano = 0) "
            "   OR (source_media IS NULL OR TRIM(source_media) = '') "
            "   OR (collection_type IS NULL OR TRIM(collection_type) = '');");
        while (stmt.step()) {
            catMeta.itemIds.insert(stmt.columnText(0));
        }
    } catch (...) {}
    categorias_.push_back(std::move(catMeta));

    // 2. Duplicate Files
    VulnerabilidadeCategoria catDup;
    catDup.id = "duplicates";
    catDup.titulo = "DUPLICATE FILES";
    catDup.descricao = "Assets sharing identical SHA-256 checksums across the project or duplicate intake files.";
    catDup.corAcento = juce::Colour(0xff3b82f6); // Blue

    try {
        auto stmt = db.prepare(
            "SELECT item_id FROM arquivo "
            "WHERE hash_sha256 IN ("
            "    SELECT hash_sha256 FROM arquivo "
            "    WHERE hash_sha256 IS NOT NULL AND TRIM(hash_sha256) != '' "
            "    GROUP BY hash_sha256 HAVING COUNT(*) > 1"
            ");");
        while (stmt.step()) {
            catDup.itemIds.insert(stmt.columnText(0));
        }
    } catch (...) {}
    categorias_.push_back(std::move(catDup));

    // 3. Offline / Missing Source Media
    VulnerabilidadeCategoria catOffline;
    catOffline.id = "offline";
    catOffline.titulo = "OFFLINE / MISSING SOURCE";
    catOffline.descricao = "Assets whose master files are on disconnected volumes or missing from disk.";
    catOffline.corAcento = juce::Colour(0xffef4444); // Red

    auto itensResumo = projeto_.listarItens();
    for (const auto& it : itensResumo) {
        if (it.offline) {
            catOffline.itemIds.insert(it.id);
        }
    }
    categorias_.push_back(std::move(catOffline));

    // 4. Single Copy / Unbacked Assets
    VulnerabilidadeCategoria catSingleCopy;
    catSingleCopy.id = "single_copy";
    catSingleCopy.titulo = "SINGLE COPY (NO BACKUP)";
    catSingleCopy.descricao = "Assets with no verified replica in any secondary backup vault.";
    catSingleCopy.corAcento = juce::Colour(0xfff97316); // Orange

    try {
        auto stmt = db.prepare(
            "SELECT i.id FROM item i "
            "WHERE (SELECT COUNT(DISTINCT a.vault_id) FROM arquivo a WHERE a.item_id = i.id AND a.vault_id IS NOT NULL) <= 1;");
        while (stmt.step()) {
            catSingleCopy.itemIds.insert(stmt.columnText(0));
        }
    } catch (...) {}
    categorias_.push_back(std::move(catSingleCopy));

    repaint();
}

void VulnerabilidadesDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Dialog outer border
    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);

    subcardBounds_.clear();

    auto area = getLocalBounds().reduced(24, 18);
    area.removeFromTop(44); // Header
    area.removeFromTop(12);

    int numCards = static_cast<int>(categorias_.size());
    if (numCards == 0) return;

    int cols = 2;
    int rows = (numCards + 1) / cols;
    int gap = 12;

    int cardW = (area.getWidth() - (cols - 1) * gap) / cols;
    int cardH = (area.getHeight() - (rows - 1) * gap) / rows;

    for (int i = 0; i < numCards; ++i) {
        int r = i / cols;
        int c = i % cols;

        juce::Rectangle<int> cardBounds(area.getX() + c * (cardW + gap),
                                        area.getY() + r * (cardH + gap),
                                        cardW, cardH);
        subcardBounds_.push_back(cardBounds);

        const auto& cat = categorias_[static_cast<size_t>(i)];
        bool isHovered = (i == hoveredIdx_);

        // Subcard Background
        if (isHovered) {
            g.setColour(tk.painelAlt.brighter(0.08f));
        } else {
            g.setColour(tk.painel);
        }
        g.fillRoundedRectangle(cardBounds.toFloat(), 6.0f);

        // Accent strip on the left edge
        auto strip = cardBounds.removeFromLeft(5);
        g.setColour(cat.corAcento);
        g.fillRoundedRectangle(strip.toFloat(), 3.0f);

        // Subcard Border
        g.setColour(isHovered ? cat.corAcento : tk.borda);
        g.drawRoundedRectangle(cardBounds.toFloat(), 6.0f, isHovered ? 1.5f : 1.0f);

        auto inner = cardBounds.reduced(14, 12);

        // Header Row: Category Title & Count Badge
        auto topRow = inner.removeFromTop(24);

        g.setColour(cat.corAcento);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(cat.titulo, topRow.removeFromLeft(topRow.getWidth() - 110), juce::Justification::centredLeft, true);

        // Big Count Badge on top right
        auto countBadge = topRow;
        g.setColour(cat.corAcento.withAlpha(0.20f));
        g.fillRoundedRectangle(countBadge.toFloat(), 4.0f);
        g.setColour(cat.corAcento);
        g.drawRoundedRectangle(countBadge.toFloat(), 4.0f, 1.0f);

        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        juce::String countText = juce::String(cat.itemIds.size()) + " files";
        g.drawText(countText, countBadge, juce::Justification::centred);

        inner.removeFromTop(8);

        // Description
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawFittedText(cat.descricao, inner.removeFromTop(44), juce::Justification::topLeft, 2);

        // Bottom Action Cue
        inner.removeFromTop(6);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.setColour(isHovered ? tk.acento : tk.textoTerciario);
        g.drawText("Click to view & select in Grid \u2192", inner, juce::Justification::bottomRight);
    }
}

void VulnerabilidadesDialog::resized() {
    auto area = getLocalBounds().reduced(24, 18);
    auto headerArea = area.removeFromTop(44);

    btnFechar_->setBounds(headerArea.removeFromRight(85).reduced(0, 8));
    headerArea.removeFromRight(12);

    lblTitulo_->setBounds(headerArea.removeFromTop(22));
    lblSubtitulo_->setBounds(headerArea);
}

void VulnerabilidadesDialog::mouseMove(const juce::MouseEvent& e) {
    int oldHover = hoveredIdx_;
    hoveredIdx_ = -1;

    for (size_t i = 0; i < subcardBounds_.size(); ++i) {
        if (subcardBounds_[i].contains(e.getPosition())) {
            hoveredIdx_ = static_cast<int>(i);
            break;
        }
    }

    if (hoveredIdx_ != oldHover) {
        repaint();
    }
}

void VulnerabilidadesDialog::mouseExit(const juce::MouseEvent&) {
    if (hoveredIdx_ != -1) {
        hoveredIdx_ = -1;
        repaint();
    }
}

void VulnerabilidadesDialog::mouseDown(const juce::MouseEvent& e) {
    for (size_t i = 0; i < subcardBounds_.size(); ++i) {
        if (subcardBounds_[i].contains(e.getPosition())) {
            const auto& cat = categorias_[i];
            if (aoSelecionarCategoria) {
                aoSelecionarCategoria(cat.itemIds);
            }
            if (aoFechar) {
                aoFechar();
            }
            return;
        }
    }
}

void VulnerabilidadesDialog::exibirModal(ProjetoAberto& projeto,
                                        juce::Component* parentComp,
                                        std::function<void(const std::set<std::string>&)> callbackAoAbrirNoGrid) {
    if (!parentComp) return;

    auto dlg = std::make_unique<VulnerabilidadesDialog>(projeto);
    dlg->setSize(680, 420);

    auto* rawDlg = dlg.get();

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dlg.release());
    opt.dialogTitle = "Backup Health & Vulnerability Diagnostics";
    opt.dialogBackgroundColour = tema().fundo;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = false;
    opt.resizable = false;

    auto* win = opt.launchAsync();

    rawDlg->aoFechar = [win] {
        if (win) win->exitModalState(0);
    };

    rawDlg->aoSelecionarCategoria = [win, callback = std::move(callbackAoAbrirNoGrid)](const std::set<std::string>& ids) {
        if (callback) callback(ids);
        if (win) win->exitModalState(0);
    };
}

} // namespace matriz::ui
