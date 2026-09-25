#include "TagChipsEditor.h"
#include "Tokens.h"

namespace matriz::ui {

TagChipsEditor::~TagChipsEditor() {
    commitText();
    input_->onFocusLost = nullptr;
    input_->onCommit = nullptr;
    input_->onBackspaceEmpty = nullptr;
    input_->onTextChange = nullptr;
}

TagChipsEditor::TagChipsEditor() {
    input_ = std::make_unique<TagInput>();
    input_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    input_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
    input_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    input_->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    input_->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    input_->setTextToShowWhenEmpty("Add tag...", juce::Colours::grey);
    input_->setScrollbarsShown(false);

    input_->onCommit = [this] { commitText(); };
    input_->onBackspaceEmpty = [this] { removeLastTag(); };
    input_->onFocusLost = [this] { commitText(); };

    input_->onTextChange = [this] {
        auto text = input_->getText();
        if (text.containsChar(',') || text.containsChar('\n') || text.containsChar('\t')) {
            juce::StringArray parts;
            parts.addTokens(text, ",\n\t", "");
            input_->setText("", false);
            for (auto& p : parts)
                addTag(p.trim());
        }
    };

    addAndMakeVisible(*input_);
}

void TagChipsEditor::setTags(const std::vector<std::string>& tags) {
    chips_.clear();
    for (auto& t : tags) {
        if (t.empty()) continue;
        Chip c;
        c.tag = t;
        c.display = juce::String(t);
        chips_.push_back(std::move(c));
    }
    layoutChips();
    repaint();
}

std::vector<std::string> TagChipsEditor::getTags() const {
    std::vector<std::string> out;
    out.reserve(chips_.size());
    for (auto& c : chips_)
        out.push_back(c.tag);
    return out;
}

int TagChipsEditor::getPreferredHeight() const {
    if (chips_.empty())
        return 30;

    int maxY = 0;
    for (auto& c : chips_)
        maxY = std::max(maxY, c.bounds.getBottom());

    return std::max(maxY + 4 + 24 + 4, 30);
}

juce::String TagChipsEditor::canonicalize(const juce::String& text) const {
    auto s = text.trim().toLowerCase();
    while (s.contains("  "))
        s = s.replace("  ", " ");
    return s;
}

void TagChipsEditor::commitText() {
    auto text = input_->getText().trim();
    if (text.isEmpty()) return;

    input_->setText("", false);

    juce::StringArray parts;
    parts.addTokens(text, ",\n\t", "");
    for (auto& p : parts)
        addTag(p.trim());

    input_->grabKeyboardFocus();
}

void TagChipsEditor::addTag(const juce::String& text) {
    juce::String clean = text.trimCharactersAtStart("#").trim();
    if (clean.isEmpty()) return;

    juce::String canonical = canonicalize(clean);
    if (canonical.isEmpty()) return;

    for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
        if (canonicalize(juce::String(chips_[static_cast<size_t>(i)].tag)) == canonical) {
            flashChip(i);
            return;
        }
    }

    Chip c;
    c.tag = canonical.toStdString();
    c.display = clean;
    chips_.push_back(std::move(c));
    layoutChips();
    repaint();
    if (aoMudar) aoMudar();
}

void TagChipsEditor::removeTag(int index) {
    if (index < 0 || index >= static_cast<int>(chips_.size())) return;
    chips_.erase(chips_.begin() + index);
    layoutChips();
    repaint();
    if (aoMudar) aoMudar();
}

void TagChipsEditor::removeLastTag() {
    if (chips_.empty()) return;
    removeTag(static_cast<int>(chips_.size()) - 1);
}

void TagChipsEditor::flashChip(int index) {
    flashIndex_ = index;
    flashCounter_ = 4;
    auto safeThis = juce::Component::SafePointer<TagChipsEditor>(this);
    auto tick = [safeThis]() {
        if (!safeThis) return;
        safeThis->flashCounter_--;
        safeThis->repaint();
        if (safeThis->flashCounter_ <= 0)
            safeThis->flashIndex_ = -1;
    };
    for (int i = 1; i <= 4; ++i)
        juce::Timer::callAfterDelay(i * 80, tick);
}

void TagChipsEditor::layoutChips() {
    if (emLayout_) return;
    emLayout_ = true;

    int w = getWidth();
    if (w <= 0) w = 200;

    const int chipH = 20;
    const int hPad = 6;
    const int gap = 4;
    const int closeW = 14;
    // item 2/item 1 (nova lista): reserva o canto superior direito pros
    // ícones de copiar e colar — só na primeira linha, onde eles realmente
    // ficam desenhados.
    const int larguraIconeCopiar = (areaIconeCopiar().getRight() - areaIconeColar().getX()) + 6;
    auto font = juce::Font(juce::FontOptions(tema().tamanhoFontePequena));

    int x = 4;
    int y = 4;

    for (auto& c : chips_) {
        int textW = juce::GlyphArrangement::getStringWidthInt(font, c.display) + 2;
        int chipW = hPad + textW + 4 + closeW + hPad;
        chipW = std::min(chipW, w - 8);

        int wDisponivel = (y == 4) ? (w - larguraIconeCopiar) : w;
        if (x + chipW > wDisponivel - 4 && x > 4) {
            x = 4;
            y += chipH + gap;
        }

        c.bounds = {x, y, chipW, chipH};
        c.closeBounds = {x + chipW - hPad - closeW, y + 2, closeW, chipH - 4};
        x += chipW + gap;
    }

    int inputY = chips_.empty() ? 2 : (y + chipH + gap);
    input_->setBounds(4, inputY, w - 8, 24);

    int totalH = inputY + 24 + 4;
    if (getHeight() != totalH) {
        setSize(w, totalH);
        if (aoRedimensionar)
            aoRedimensionar();
    }

    emLayout_ = false;
}

juce::Rectangle<int> TagChipsEditor::areaIconeCopiar() const {
    return { getWidth() - 20, 3, 14, 14 };
}

juce::Rectangle<int> TagChipsEditor::areaIconeColar() const {
    // Item 1 (nova lista): mesmo estilo do ícone de copiar, logo à esquerda dele.
    return areaIconeCopiar().translated(-20, 0);
}

void TagChipsEditor::copiarTagsParaClipboard() {
    if (chips_.empty()) return;
    juce::StringArray tags;
    for (const auto& c : chips_) tags.add(c.display);
    juce::SystemClipboard::copyTextToClipboard(tags.joinIntoString(", "));

    flashIconeCopiarCounter_ = 4;
    auto safeThis = juce::Component::SafePointer<TagChipsEditor>(this);
    auto tick = [safeThis]() {
        if (!safeThis) return;
        safeThis->flashIconeCopiarCounter_--;
        safeThis->repaint();
    };
    for (int i = 1; i <= 4; ++i)
        juce::Timer::callAfterDelay(i * 80, tick);
}

void TagChipsEditor::colarTagsDoClipboard() {
    juce::String clipText = juce::SystemClipboard::getTextFromClipboard();
    if (clipText.trim().isEmpty()) return;

    juce::StringArray parts;
    parts.addTokens(clipText, ",\n\t", "");
    for (auto& p : parts)
        addTag(p.trim());

    flashIconeColarCounter_ = 4;
    auto safeThis = juce::Component::SafePointer<TagChipsEditor>(this);
    auto tick = [safeThis]() {
        if (!safeThis) return;
        safeThis->flashIconeColarCounter_--;
        safeThis->repaint();
    };
    for (int i = 1; i <= 4; ++i)
        juce::Timer::callAfterDelay(i * 80, tick);
}

void TagChipsEditor::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto font = juce::Font(juce::FontOptions(tk.tamanhoFontePequena));
    g.setFont(font);

    g.setColour(juce::Colours::white);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioPequeno);

    g.setColour(tk.borda);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);

    // item 2 (correção METADATA): ícone de copiar no canto superior
    // direito — mesma ação do "Copy tags" que já existia só no menu de
    // botão direito, agora também visível/descobrível como ícone.
    {
        bool flashing = (flashIconeCopiarCounter_ % 2) == 1;
        auto icone = areaIconeCopiar().toFloat();
        g.setColour(flashing ? tk.acento : tk.textoTerciario);
        // Dois retângulos arredondados sobrepostos — glifo padrão de "copiar".
        auto retFundo = icone.withTrimmedLeft(icone.getWidth() * 0.28f).withTrimmedTop(icone.getHeight() * 0.28f);
        auto retFrente = icone.withTrimmedRight(icone.getWidth() * 0.28f).withTrimmedBottom(icone.getHeight() * 0.28f);
        g.drawRoundedRectangle(retFundo, 2.0f, 1.3f);
        g.setColour(juce::Colours::white);
        g.fillRoundedRectangle(retFrente, 2.0f);
        g.setColour(flashing ? tk.acento : tk.textoTerciario);
        g.drawRoundedRectangle(retFrente, 2.0f, 1.3f);
    }

    // item 1 (nova lista): ícone de colar, mesmo estilo — prancheta com uma
    // aba no topo, pra diferenciar visualmente do ícone de copiar ao lado.
    {
        bool flashing = (flashIconeColarCounter_ % 2) == 1;
        auto icone = areaIconeColar();
        g.setColour(flashing ? tk.acento : tk.textoTerciario);
        auto prancheta = icone.toFloat().withTrimmedTop(2.0f);
        g.drawRoundedRectangle(prancheta, 2.0f, 1.3f);
        auto aba = juce::Rectangle<float>(icone.getCentreX() - 3.0f, static_cast<float>(icone.getY()), 6.0f, 3.0f);
        g.fillRoundedRectangle(aba, 1.0f);
    }

    for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
        auto& c = chips_[static_cast<size_t>(i)];
        auto chipRect = c.bounds.toFloat();

        bool flashing = (i == flashIndex_ && (flashCounter_ % 2) == 1);

        juce::Colour bg = flashing ? tk.acento.brighter(0.3f) : tk.acento.withAlpha(0.25f);
        juce::Colour fg = flashing ? tk.textoSobreAcento : tk.textoPrimario;

        g.setColour(bg);
        g.fillRoundedRectangle(chipRect, static_cast<float>(c.bounds.getHeight()) / 2.0f);

        g.setColour(fg);
        auto textArea = c.bounds.reduced(6, 0).withTrimmedRight(18);
        g.drawText(c.display, textArea, juce::Justification::centredLeft, true);

        g.setColour(fg.withAlpha(0.6f));
        auto closeRect = c.closeBounds.toFloat();
        float cx = closeRect.getCentreX();
        float cy = closeRect.getCentreY();
        float sz = 3.0f;
        g.drawLine(cx - sz, cy - sz, cx + sz, cy + sz, 1.5f);
        g.drawLine(cx + sz, cy - sz, cx - sz, cy + sz, 1.5f);
    }
}

void TagChipsEditor::resized() {
    layoutChips();
}

void TagChipsEditor::mouseUp(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addItem(1, "Copy tags");

        juce::String clipText = juce::SystemClipboard::getTextFromClipboard();
        bool hasPaste = !clipText.trim().isEmpty();
        menu.addItem(2, "Paste tags", hasPaste);

        juce::Component::SafePointer<TagChipsEditor> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int r) {
            if (!safeThis) return;
            if (r == 1) {
                safeThis->copiarTagsParaClipboard();
            } else if (r == 2) {
                safeThis->colarTagsDoClipboard();
            }
        });
        return;
    }

    auto pos = e.getPosition();
    if (areaIconeColar().expanded(3).contains(pos)) {
        colarTagsDoClipboard();
        return;
    }
    if (areaIconeCopiar().expanded(3).contains(pos)) {
        copiarTagsParaClipboard();
        return;
    }
    for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
        if (chips_[static_cast<size_t>(i)].closeBounds.contains(pos)) {
            removeTag(i);
            return;
        }
    }
    input_->grabKeyboardFocus();
}

} // namespace matriz::ui
