// ==============================================================================
// INTAKE WORKSPACE COMPONENT
// STRICT APP-WIDE RULE: 100% ENGLISH UI. ZERO PORTUGUESE TEXT IN USER INTERFACE.
// ==============================================================================

#include "IntakeWorkspaceComponent.h"

#include "OriginalSourceMedium.h"
#include "ProjetoAberto.h"
#include "ProgressoGlobal.h"
#include "Tokens.h"
#include "../Analytics/AssetGeolocation.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::ui {

namespace {

enum ColumnId {
    kColSelect = 1,
    kColType = 2,
    kColName = 3,
    kColDateCreated = 4,
    kColSize = 5,
    kColPath = 6,
    kColCollection = 7,
    kColSourceMedia = 8,
    kColAction = 9
};

juce::String formatarBytes(juce::int64 bytes) {
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024.0, 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return juce::String(bytes / (1024.0 * 1024.0), 1) + " MB";
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

juce::Colour corParaCategoria(const juce::String& cat) {
    if (cat == "Audio") return juce::Colour(0xff38bdf8); // Sky blue
    if (cat == "Video") return juce::Colour(0xffa855f7); // Purple
    if (cat == "Image") return juce::Colour(0xfff59e0b); // Amber
    if (cat == "Document") return juce::Colour(0xff10b981); // Emerald
    if (cat == "Project") return juce::Colour(0xffec4899); // Pink
    return juce::Colour(0xff94a3b8); // Slate gray
}

// Cell component for Selection Checkbox
class CheckboxCellComponent : public juce::Component {
public:
    std::function<void(bool)> onToggle;

    CheckboxCellComponent() {
        toggle_.onClick = [this] {
            if (onToggle) onToggle(toggle_.getToggleState());
        };
        addAndMakeVisible(toggle_);
    }

    void setChecked(bool checked) {
        toggle_.setToggleState(checked, juce::dontSendNotification);
    }

    void resized() override {
        toggle_.setBounds(getLocalBounds().reduced(2));
    }

private:
    juce::ToggleButton toggle_;
};

// Cell component for Quick Collection Change button
class ActionCellComponent : public juce::Component {
public:
    std::function<void(juce::Rectangle<int>)> onClick;

    ActionCellComponent() {
        btn_.setButtonText("Set...");
        btn_.onClick = [this] {
            if (onClick) onClick(getScreenBounds());
        };
        addAndMakeVisible(btn_);
    }

    void resized() override {
        btn_.setBounds(getLocalBounds().reduced(4, 3));
    }

private:
    juce::TextButton btn_;
};

class PillButton : public juce::TextButton {
public:
    juce::Colour corPonto = juce::Colours::transparentBlack;
    juce::Colour corBordaCustom = juce::Colours::transparentBlack;
    juce::Colour corTextoCustom = juce::Colours::transparentBlack;
    float tamanhoFonte = 13.0f;
    bool ativo = false;

    PillButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        const auto& tk = tema();
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        float cornerRadius = 4.0f;

        // Background (transparent by default, soft fill when active)
        if (ativo) {
            g.setColour(tk.acento.withAlpha(0.20f));
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else if (shouldDrawButtonAsDown) {
            g.setColour(tk.painelAlt.withAlpha(0.6f));
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else if (shouldDrawButtonAsHighlighted) {
            g.setColour(tk.painelAlt.withAlpha(0.3f));
            g.fillRoundedRectangle(bounds, cornerRadius);
        }

        // Outline (Fine border)
        juce::Colour borderCol = corBordaCustom.isOpaque() ? corBordaCustom 
                               : (ativo ? tk.acento : tk.borda);
        g.setColour(borderCol);
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

        // Content (Dot + Text)
        auto contentBounds = getLocalBounds().reduced(6, 0);
        if (!corPonto.isTransparent()) {
            float dotSize = 7.0f;
            float dotX = static_cast<float>(contentBounds.getX() + 2);
            float dotY = static_cast<float>(contentBounds.getCentreY()) - dotSize * 0.5f;
            g.setColour(corPonto);
            g.fillEllipse(dotX, dotY, dotSize, dotSize);
            contentBounds.removeFromLeft(static_cast<int>(dotSize + 6.0f));
        }

        juce::Colour textCol = corTextoCustom.isOpaque() ? corTextoCustom
                             : (ativo ? tk.textoPrimario : tk.textoSecundario);
        g.setColour(textCol);
        g.setFont(juce::Font(juce::FontOptions(tamanhoFonte, ativo ? juce::Font::bold : juce::Font::plain)));
        g.drawText(getButtonText(), contentBounds, juce::Justification::centred, true);
    }
};

class ActionButton : public juce::TextButton {
public:
    explicit ActionButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        float radius = 5.0f;

        juce::Colour bg = juce::Colour(0xff21262d);
        if (shouldDrawButtonAsDown) bg = juce::Colour(0xff30363d);
        else if (shouldDrawButtonAsHighlighted) bg = juce::Colour(0xff2b313a);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, radius);

        g.setColour(juce::Colour(0xff30363d));
        g.drawRoundedRectangle(bounds, radius, 1.0f);

        g.setColour(juce::Colour(0xffe6edf3));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(getButtonText(), bounds, juce::Justification::centred, true);
    }
};

class ActionMenuComponent : public juce::Component {
public:
    ActionMenuComponent(int rowNumber, std::function<void(int)> onOpen, std::function<void(int)> onProperties)
        : rowNumber_(rowNumber), onOpen_(std::move(onOpen)), onProperties_(std::move(onProperties)) {
        btnOpen_ = std::make_unique<ActionButton>("Open");
        btnOpen_->onClick = [this] { if (onOpen_) onOpen_(rowNumber_); };
        addAndMakeVisible(*btnOpen_);

        btnMenu_ = std::make_unique<ActionButton>("...");
        btnMenu_->onClick = [this] { if (onProperties_) onProperties_(rowNumber_); };
        addAndMakeVisible(*btnMenu_);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(2, 3);
        btnMenu_->setBounds(area.removeFromRight(26));
        area.removeFromRight(4);
        btnOpen_->setBounds(area);
    }

    void setRow(int row) { rowNumber_ = row; }

private:
    int rowNumber_;
    std::unique_ptr<ActionButton> btnOpen_;
    std::unique_ptr<ActionButton> btnMenu_;
    std::function<void(int)> onOpen_;
    std::function<void(int)> onProperties_;
};

class VerticalDividerComponent : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff30363d));
        g.fillRect(0, 0, getWidth(), getHeight());
    }
};

class OriginalSourceMediumPopupContent : public juce::Component {
public:
    OriginalSourceMediumPopupContent(const std::string& initialVal, bool isBatch, std::function<void(const std::string&)> onApply)
        : onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", isBatch ? "ORIGINAL SOURCE MEDIUM (BATCH)" : "ORIGINAL SOURCE MEDIUM");
        lblTitle_->setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitle_);

        editor_ = std::make_unique<OriginalSourceMediumEditorComponent>();
        editor_->setValueString(initialVal);
        editor_->onChange = [this] {
            if (editor_) {
                editor_->setBounds(0, 0, 360, editor_->getPreferredHeight());
            }
        };

        viewport_ = std::make_unique<juce::Viewport>();
        viewport_->setViewedComponent(editor_.get(), false);
        viewport_->setScrollBarsShown(true, false);
        addAndMakeVisible(*viewport_);

        btnApply_ = std::make_unique<PillButton>(isBatch ? "Apply to Selected" : "Apply");
        btnApply_->corTextoCustom = juce::Colour(0xff2a9d8f);
        btnApply_->corBordaCustom = juce::Colour(0xff2a9d8f);
        btnApply_->tamanhoFonte = 13.0f;
        btnApply_->onClick = [this] {
            if (onApply_) onApply_(editor_->getValueString());
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) {
                callout->dismiss();
            }
        };
        addAndMakeVisible(*btnApply_);

        setSize(400, 380);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        lblTitle_->setBounds(area.removeFromTop(24));
        area.removeFromTop(8);

        auto bottomArea = area.removeFromBottom(30);
        btnApply_->setBounds(bottomArea.removeFromRight(150));

        area.removeFromBottom(8);
        viewport_->setBounds(area);
        if (editor_) {
            editor_->setBounds(0, 0, area.getWidth() - 10, editor_->getPreferredHeight());
        }
    }

private:
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<OriginalSourceMediumEditorComponent> editor_;
    std::unique_ptr<PillButton> btnApply_;
    std::function<void(const std::string&)> onApply_;
};

class GeoLocationPopupContent : public juce::Component {
public:
    GeoLocationPopupContent(bool isBatch, std::function<void(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&)> onApply)
        : onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", isBatch ? "GEO LOCATION (BATCH OVERRIDE)" : "GEO LOCATION");
        lblTitle_->setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitle_);

        auto makeField = [this, &tk](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed,
                                     const juce::String& labelText, const juce::String& placeholder) {
            lbl = std::make_unique<juce::Label>("", labelText);
            lbl->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            lbl->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lbl);

            ed = std::make_unique<juce::TextEditor>();
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
            ed->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
            ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
            ed->setTextToShowWhenEmpty(placeholder, tk.textoTerciario);
            addAndMakeVisible(*ed);
        };

        makeField(lblCoords_, edCoords_, "GPS Coordinates (Lat, Lng)", "e.g. -16.4435, -39.0643");
        makeField(lblAddress_, edAddress_, "Formatted Address", "e.g. Av. Paulista, 1000");
        makeField(lblCity_, edCity_, "City", "e.g. Porto Seguro");
        makeField(lblState_, edState_, "State / Province", "e.g. Bahia");
        makeField(lblCountry_, edCountry_, "Country", "e.g. Brazil");

        btnApply_ = std::make_unique<PillButton>(isBatch ? "Apply to Selected" : "Apply");
        btnApply_->corTextoCustom = juce::Colour(0xff2a9d8f);
        btnApply_->corBordaCustom = juce::Colour(0xff2a9d8f);
        btnApply_->tamanhoFonte = 13.0f;
        btnApply_->onClick = [this] {
            if (onApply_) {
                onApply_(edCoords_->getText().toStdString(),
                         edAddress_->getText().toStdString(),
                         edCity_->getText().toStdString(),
                         edState_->getText().toStdString(),
                         edCountry_->getText().toStdString());
            }
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) {
                callout->dismiss();
            }
        };
        addAndMakeVisible(*btnApply_);

        setSize(380, 360);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        lblTitle_->setBounds(area.removeFromTop(24));
        area.removeFromTop(6);

        auto layoutSubfield = [&](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed) {
            if (lbl) { lbl->setBounds(area.removeFromTop(16)); area.removeFromTop(2); }
            if (ed) { ed->setBounds(area.removeFromTop(26)); area.removeFromTop(6); }
        };

        layoutSubfield(lblCoords_, edCoords_);
        layoutSubfield(lblAddress_, edAddress_);
        layoutSubfield(lblCity_, edCity_);
        layoutSubfield(lblState_, edState_);
        layoutSubfield(lblCountry_, edCountry_);

        area.removeFromTop(4);
        btnApply_->setBounds(area.removeFromBottom(28).removeFromRight(150));
    }

private:
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<juce::Label> lblCoords_;
    std::unique_ptr<juce::TextEditor> edCoords_;
    std::unique_ptr<juce::Label> lblAddress_;
    std::unique_ptr<juce::TextEditor> edAddress_;
    std::unique_ptr<juce::Label> lblCity_;
    std::unique_ptr<juce::TextEditor> edCity_;
    std::unique_ptr<juce::Label> lblState_;
    std::unique_ptr<juce::TextEditor> edState_;
    std::unique_ptr<juce::Label> lblCountry_;
    std::unique_ptr<juce::TextEditor> edCountry_;
    std::unique_ptr<PillButton> btnApply_;
    std::function<void(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&)> onApply_;
};

} // namespace

class IntakeWorkspaceComponent::ThumbnailsGridComponent : public juce::Component {
public:
    explicit ThumbnailsGridComponent(IntakeWorkspaceComponent& owner) : owner_(owner) {
        setWantsKeyboardFocus(false);
    }

    ~ThumbnailsGridComponent() override {
        poolThumbnails_.removeAllJobs(true, 2000);
    }

    void recalcularLayout() {
        int w = owner_.gridViewport_ ? owner_.gridViewport_->getViewWidth() : getWidth();
        if (w <= 0) w = owner_.getWidth();
        if (w <= 0) return;

        int total = static_cast<int>(owner_.indicesFiltrados_.size());
        if (total == 0) {
            setSize(w, 200);
            repaint();
            return;
        }

        int padding = 14;
        int cardMinW = 190;
        int cardH = 210;
        int colunas = std::max(1, (w - padding) / (cardMinW + padding));
        int cardW = (w - padding * (colunas + 1)) / colunas;
        int linhas = (total + colunas - 1) / colunas;
        int totalH = padding + linhas * (cardH + padding);

        colunas_ = colunas;
        cardW_ = cardW;
        cardH_ = cardH;
        padding_ = padding;

        setSize(w, std::max(totalH, 200));
        repaint();
    }

    juce::Rectangle<int> boundsDoCard(int indice) const {
        if (colunas_ <= 0) return {};
        int col = indice % colunas_;
        int row = indice / colunas_;
        int x = padding_ + col * (cardW_ + padding_);
        int y = padding_ + row * (cardH_ + padding_);
        return { x, y, cardW_, cardH_ };
    }

    int indiceNaPosicao(juce::Point<int> pos) const {
        int total = static_cast<int>(owner_.indicesFiltrados_.size());
        for (int i = 0; i < total; ++i) {
            if (boundsDoCard(i).contains(pos)) return i;
        }
        return -1;
    }

    void pedirMiniatura(const std::string& itemId) {
        {
            const juce::ScopedLock sl(lock_);
            if (loading_.count(itemId) || cache_.count(itemId) || noThumbnail_.count(itemId)) return;
            loading_[itemId] = true;
        }

        juce::Component::SafePointer<ThumbnailsGridComponent> safeThis(this);
        ProjetoAberto* proj = &owner_.projeto_;

        poolThumbnails_.addJob([safeThis, proj, itemId]() mutable {
            auto caminho = proj->caminhoMiniaturaPrincipal(itemId);
            juce::Image img;
            if (caminho) img = juce::ImageFileFormat::loadFrom(juce::File(*caminho));
            bool temCaminho = caminho.has_value();

            if (!img.isValid()) {
                if (auto arq = proj->arquivoPrincipal(itemId)) {
                    juce::File f(arq->caminhoAbsoluto);
                    juce::String ext = f.getFileExtension().toLowerCase().replace(".", "");
                    juce::String logoFile = matriz::ingest::obterLogoParaExtensao(ext);
                    if (logoFile.isEmpty() && ext == "pd") logoFile = "puredata.png";
                    if (logoFile.isNotEmpty()) {
                        juce::File assetsFolder = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("Assets");
                        juce::File logoImgFile = assetsFolder.getChildFile(logoFile);
                        if (logoImgFile.existsAsFile()) {
                            img = juce::ImageFileFormat::loadFrom(logoImgFile);
                            if (img.isValid()) temCaminho = true;
                        }
                    }
                }
            }

            juce::MessageManager::callAsync([safeThis, itemId, img, temCaminho]() mutable {
                if (!safeThis) return;
                const juce::ScopedLock sl(safeThis->lock_);
                safeThis->loading_.erase(itemId);
                if (img.isValid()) {
                    safeThis->cache_[itemId] = img;
                } else if (!temCaminho) {
                    safeThis->noThumbnail_[itemId] = true;
                }
                safeThis->repaint();
            });
        });
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.fundo);

        int total = static_cast<int>(owner_.indicesFiltrados_.size());
        if (total == 0) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawText("No intake items match current filter.", getLocalBounds(), juce::Justification::centred);
            return;
        }

        static const juce::Font font12Bold(juce::FontOptions(12.0f, juce::Font::bold));
        static const juce::Font font11Normal(juce::FontOptions(11.0f));
        static const juce::Font font10Bold(juce::FontOptions(10.0f, juce::Font::bold));
        static const juce::Font font9Bold(juce::FontOptions(9.0f, juce::Font::bold));

        static const juce::Path checkmarkPath = []() {
            juce::Path p;
            p.startNewSubPath(0.24f, 0.50f);
            p.lineTo(0.42f, 0.70f);
            p.lineTo(0.78f, 0.30f);
            return p;
        }();

        auto clip = g.getClipBounds();

        for (int i = 0; i < total; ++i) {
            auto bounds = boundsDoCard(i);
            if (!bounds.intersects(clip)) continue;

            int realIdx = owner_.indicesFiltrados_[static_cast<size_t>(i)];
            if (realIdx < 0 || realIdx >= static_cast<int>(owner_.todosItens_.size())) continue;
            const auto& item = owner_.todosItens_[static_cast<size_t>(realIdx)];

            bool selecionado = item.selecionado;
            bool hover = (hoverIndex_ == i);
            juce::Colour corCat = corParaCategoria(item.categoria);

            // Card Background
            g.setColour(tk.painel);
            g.fillRoundedRectangle(bounds.toFloat(), tk.raioMedio);

            // Thumbnail Area (~115px height)
            auto thumbArea = bounds.reduced(6, 6).removeFromTop(115);
            juce::Image img;
            {
                const juce::ScopedLock sl(lock_);
                auto it = cache_.find(item.id);
                if (it != cache_.end()) img = it->second;
            }

            if (img.isValid()) {
                g.setColour(tk.painelAlt);
                g.fillRoundedRectangle(thumbArea.toFloat(), tk.raioPequeno);
                g.drawImage(img, thumbArea.toFloat(), juce::RectanglePlacement::centred);
            } else {
                pedirMiniatura(item.id);
                g.setColour(corCat.withAlpha(0.12f));
                g.fillRoundedRectangle(thumbArea.toFloat(), tk.raioPequeno);
                g.setColour(corCat);
                g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
                g.drawText(item.extensao.isNotEmpty() ? item.extensao : item.categoria.substring(0, 3).toUpperCase(),
                           thumbArea, juce::Justification::centred);
            }

            // Thumbnail Border
            g.setColour(corCat.withAlpha(0.45f));
            g.drawRoundedRectangle(thumbArea.toFloat(), tk.raioPequeno, 1.0f);

            // Top-Left Checkbox on thumbnail
            juce::Rectangle<int> checkRect(thumbArea.getX() + 6, thumbArea.getY() + 6, 18, 18);
            if (selecionado) {
                g.setColour(tk.acento);
                g.fillRoundedRectangle(checkRect.toFloat(), 3.0f);
                g.setColour(tk.textoSobreAcento);
                g.strokePath(checkmarkPath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                             juce::AffineTransform::scale(18.0f).translated(static_cast<float>(checkRect.getX()), static_cast<float>(checkRect.getY())));
            } else {
                g.setColour(juce::Colour(0xd0000000));
                g.fillRoundedRectangle(checkRect.toFloat(), 3.0f);
                g.setColour(tk.borda);
                g.drawRoundedRectangle(checkRect.toFloat(), 3.0f, 1.2f);
            }

            // Top-Right Category Badge
            juce::String catBadgeText = item.categoria.toUpperCase();
            int catBadgeW = juce::jmax(36, static_cast<int>(catBadgeText.length()) * 7 + 10);
            juce::Rectangle<int> catBadgeRect(thumbArea.getRight() - catBadgeW - 6, thumbArea.getY() + 6, catBadgeW, 18);
            g.setColour(corCat.withAlpha(0.85f));
            g.fillRoundedRectangle(catBadgeRect.toFloat(), 4.0f);
            g.setColour(juce::Colours::white);
            g.setFont(font9Bold);
            g.drawText(catBadgeText, catBadgeRect, juce::Justification::centred);

            // OFFLINE Badge
            if (item.offline) {
                juce::Rectangle<int> offBadge(thumbArea.getCentreX() - 28, thumbArea.getCentreY() - 9, 56, 18);
                g.setColour(juce::Colour(0xd0000000));
                g.fillRoundedRectangle(offBadge.toFloat(), 4.0f);
                g.setColour(juce::Colour(0xfff97316));
                g.drawRoundedRectangle(offBadge.toFloat(), 4.0f, 1.0f);
                g.setFont(font9Bold);
                g.drawText("OFFLINE", offBadge, juce::Justification::centred);
            }

            // Text Info Section below thumbnail
            auto infoArea = bounds.reduced(8, 0).withTop(thumbArea.getBottom() + 6);

            // Filename / Title (Line 1)
            g.setColour(tk.textoPrimario);
            g.setFont(font12Bold);
            g.drawText(item.nomeArquivo, infoArea.removeFromTop(18), juce::Justification::centredLeft, true);

            // Size & Date (Line 2)
            juce::String sizeDate = formatarBytes(item.tamanhoBytes);
            if (item.dataCriacao.isNotEmpty()) {
                sizeDate += "  |  " + item.dataCriacao.substring(0, 10);
            }
            g.setColour(tk.textoSecundario);
            g.setFont(font11Normal);
            g.drawText(sizeDate, infoArea.removeFromTop(16), juce::Justification::centredLeft, true);

            // Content / Collection Badge (Line 3 if present)
            if (item.collection.isNotEmpty()) {
                auto collBadge = infoArea.removeFromTop(18).reduced(0, 1);
                g.setColour(tk.acento.withAlpha(0.20f));
                g.fillRoundedRectangle(collBadge.toFloat(), 3.0f);
                g.setColour(tk.acento);
                g.drawRoundedRectangle(collBadge.toFloat(), 3.0f, 1.0f);
                g.setFont(font10Bold);
                g.drawText(item.collection, collBadge, juce::Justification::centred, true);
            }

            // Outer Card Border
            if (selecionado) {
                g.setColour(tk.acento);
                g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), tk.raioMedio, 2.0f);
            } else if (hover) {
                g.setColour(tk.bordaFoco);
                g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), tk.raioMedio, 1.5f);
            } else {
                g.setColour(tk.borda);
                g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int idx = indiceNaPosicao(e.getPosition());
        if (idx < 0 || idx >= static_cast<int>(owner_.indicesFiltrados_.size())) return;

        int realIdx = owner_.indicesFiltrados_[static_cast<size_t>(idx)];
        if (realIdx < 0 || realIdx >= static_cast<int>(owner_.todosItens_.size())) return;

        if (e.mods.isPopupMenu()) {
            owner_.mostrarMenuContexto(realIdx, e.getScreenPosition());
            return;
        }

        if (e.getNumberOfClicks() >= 2) {
            owner_.abrirArquivoOrigem(idx);
            return;
        }

        owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = !owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado;
        owner_.atualizarContagens();
        if (owner_.tabela_) owner_.tabela_->repaint();
        repaint();
    }

    void mouseMove(const juce::MouseEvent& e) override {
        int idx = indiceNaPosicao(e.getPosition());
        if (hoverIndex_ != idx) {
            hoverIndex_ = idx;
            setMouseCursor(hoverIndex_ >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoverIndex_ != -1) {
            hoverIndex_ = -1;
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

private:
    IntakeWorkspaceComponent& owner_;
    juce::ThreadPool poolThumbnails_{2};
    std::unordered_map<std::string, juce::Image> cache_;
    std::unordered_map<std::string, bool> loading_;
    std::unordered_map<std::string, bool> noThumbnail_;
    juce::CriticalSection lock_;
    int hoverIndex_ = -1;
    int colunas_ = 1;
    int cardW_ = 200;
    int cardH_ = 210;
    int padding_ = 14;
};

const std::vector<IntakeWorkspaceComponent::CategoriaColecao>& IntakeWorkspaceComponent::vocabularioColecoes() {
    static const std::vector<CategoriaColecao> kVocab = {
        { "AUDIO", {
            "Album", "EP", "Single", "Compilation", "Soundtrack",
            "Stems", "Multitracks", "Sample Pack", "DAW Session",
            "Field Recording", "Sound FX", "MIDI",
            "Artist Catalog", "Artist Backup"
        }},
        { "VIDEO", {
            "Raw Footage", "Home Video", "Music Video", "Film",
            "Documentary", "Corporate Video", "Commercial", "Live Performance",
            "NLE Project"
        }},
        { "IMAGE", {
            "Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional",
            "Image Edit Project"
        }},
        { "DOCUMENT", {
            "Documentation", "Book", "Contract", "Manual", "Report",
            "Reference", "Technical Documentation"
        }}
    };
    return kVocab;
}

void IntakeWorkspaceComponent::popularComboColecoes(juce::ComboBox& combo, bool incluirNone) {
    combo.clear(juce::dontSendNotification);
    int id = 1;
    if (incluirNone) {
        combo.addItem("None", id++);
        combo.addSeparator();
    }
    for (const auto& cat : vocabularioColecoes()) {
        combo.addSectionHeading(cat.grupo);
        for (const auto& item : cat.itens) {
            combo.addItem(item, id++);
        }
    }
    if (incluirNone) {
        combo.setSelectedId(1, juce::dontSendNotification);
    }
}

IntakeWorkspaceComponent::IntakeWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    const auto& tk = tema();

    // 1. LINHA 1 (BatchHeaderBar)
    lblTitulo_ = std::make_unique<juce::Label>("", "INGEST BATCH");
    lblTitulo_->setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    lblContadorTotal_ = std::make_unique<juce::Label>("", "0 active in intake");
    lblContadorTotal_->setFont(juce::Font(juce::FontOptions(13.0f)));
    lblContadorTotal_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblContadorTotal_);

    btnVisaoLista_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x89\xa1")); // ≡
    btnVisaoLista_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnVisaoLista_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnVisaoLista_->setTooltip("List view (table)");
    btnVisaoLista_->onClick = [this] { definirModoVisao(ModoVisao::Lista); };
    addAndMakeVisible(*btnVisaoLista_);

    btnVisaoIcones_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x96\xa6\xe2\x96\xa6")); // ⊞
    btnVisaoIcones_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnVisaoIcones_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
    btnVisaoIcones_->setTooltip("Icons view (thumbnails)");
    btnVisaoIcones_->onClick = [this] { definirModoVisao(ModoVisao::Icones); };
    addAndMakeVisible(*btnVisaoIcones_);

    auto btnIngerirPill = std::make_unique<PillButton>("+ Ingest Files / Folders");
    btnIngerirPill->tamanhoFonte = 13.0f;
    btnIngerirPill->corTextoCustom = tk.textoPrimario;
    btnIngerirPill->onClick = [this] { if (aoPedirIngerirArquivos) aoPedirIngerirArquivos(); };
    btnIngerirPill->setTooltip("Import more files or folders into intake");
    btnIngerir_ = std::move(btnIngerirPill);
    addAndMakeVisible(*btnIngerir_);

    // 2. LINHA 2 (FilterBar)
    auto setupPill = [this](std::unique_ptr<juce::TextButton>& btn, const juce::String& text, const juce::String& cat, juce::Colour dotCol) {
        auto pill = std::make_unique<PillButton>(text);
        pill->tamanhoFonte = 13.0f;
        pill->corPonto = dotCol;
        pill->onClick = [this, cat] {
            filtroCategoriaAtual_ = cat;
            atualizarFiltragem();
        };
        btn = std::move(pill);
        addAndMakeVisible(*btn);
    };

    setupPill(btnFiltroAll_, "All (0)", "ALL", juce::Colours::transparentBlack);
    setupPill(btnFiltroAudio_, "Audio (0)", "Audio", juce::Colour(0xff2a9d8f));
    setupPill(btnFiltroVideo_, "Video (0)", "Video", juce::Colour(0xff9d4edd));
    setupPill(btnFiltroImage_, "Images (0)", "Image", juce::Colour(0xfff4a261));
    setupPill(btnFiltroDoc_, "Documents (0)", "Document", juce::Colour(0xff2b9348));
    setupPill(btnFiltroOther_, "Other (0)", "Other", juce::Colour(0xff888888));

    // 3. LINHA 3 (SelectionActionBar)
    // Cluster A (Left)
    lblSubtitulo_ = std::make_unique<juce::Label>("", "0 selected");
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(13.0f)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblSubtitulo_);

    auto btnSelTodos = std::make_unique<PillButton>("Select all");
    btnSelTodos->tamanhoFonte = 12.5f;
    btnSelTodos->corTextoCustom = tk.textoPrimario;
    btnSelTodos->onClick = [this] { selecionarTodos(true); };
    btnSelecionarTodos_ = std::move(btnSelTodos);
    addAndMakeVisible(*btnSelecionarTodos_);

    auto btnLimpar = std::make_unique<PillButton>("Clear selection");
    btnLimpar->tamanhoFonte = 12.5f;
    btnLimpar->corTextoCustom = tk.textoSecundario;
    btnLimpar->onClick = [this] { selecionarTodos(false); };
    btnLimparSelecao_ = std::move(btnLimpar);
    addAndMakeVisible(*btnLimparSelecao_);

    // Cluster B (Center)
    lblRotuloColecao_ = std::make_unique<juce::Label>("", "Content:");
    lblRotuloColecao_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    lblRotuloColecao_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblRotuloColecao_);

    comboColecaoLote_ = std::make_unique<juce::ComboBox>();
    popularComboColecoes(*comboColecaoLote_, true);
    addAndMakeVisible(*comboColecaoLote_);

    auto btnAplicar = std::make_unique<PillButton>("Apply");
    btnAplicar->tamanhoFonte = 12.5f;
    btnAplicar->corTextoCustom = tk.textoPrimario;
    btnAplicar->onClick = [this] {
        juce::String chosen = comboColecaoLote_->getText();
        if (comboColecaoLote_->getSelectedId() == 1 || chosen == "None") {
            aplicarColecaoAosSelecionados("");
        } else {
            aplicarColecaoAosSelecionados(chosen);
        }
    };
    btnAplicarColecaoLote_ = std::move(btnAplicar);
    addAndMakeVisible(*btnAplicarColecaoLote_);

    auto btnOrigMed = std::make_unique<PillButton>("Original Medium...");
    btnOrigMed->tamanhoFonte = 12.5f;
    btnOrigMed->corTextoCustom = tk.textoPrimario;
    btnOrigMed->onClick = [this] {
        if (btnOriginalMediumLote_) mostrarEditorOriginalSourceMediumLote(btnOriginalMediumLote_->getScreenBounds());
    };
    btnOriginalMediumLote_ = std::move(btnOrigMed);
    addAndMakeVisible(*btnOriginalMediumLote_);

    auto btnGeo = std::make_unique<PillButton>("Geo Location...");
    btnGeo->tamanhoFonte = 12.5f;
    btnGeo->corTextoCustom = tk.textoPrimario;
    btnGeo->onClick = [this] {
        if (btnGeolocationLote_) mostrarEditorGeolocationLote(btnGeolocationLote_->getScreenBounds());
    };
    btnGeolocationLote_ = std::move(btnGeo);
    addAndMakeVisible(*btnGeolocationLote_);

    // Cluster C (Right)
    auto btnConfSel = std::make_unique<PillButton>("Send selected to GRID");
    btnConfSel->tamanhoFonte = 12.5f;
    btnConfSel->corTextoCustom = juce::Colour(0xff2a9d8f);
    btnConfSel->onClick = [this] { confirmarSelecaoParaGrid(); };
    btnConfSel->setTooltip("Send selected intake items to GRID");
    btnConfSel->setAlpha(0.4f);
    btnConfSel->setEnabled(false);
    btnConfirmarSelecao_ = std::move(btnConfSel);
    addAndMakeVisible(*btnConfirmarSelecao_);

    auto btnConfTodos = std::make_unique<PillButton>("Send all to GRID");
    btnConfTodos->tamanhoFonte = 12.5f;
    btnConfTodos->corTextoCustom = juce::Colour(0xff2a9d8f);
    btnConfTodos->corBordaCustom = juce::Colour(0xff2a9d8f);
    btnConfTodos->onClick = [this] { confirmarTodosParaGrid(); };
    btnConfTodos->setTooltip("Send all items in this batch to GRID");
    btnConfirmarTodos_ = std::move(btnConfTodos);
    addAndMakeVisible(*btnConfirmarTodos_);

    // Divisors for SelectionActionBar
    divisor1_ = std::make_unique<VerticalDividerComponent>();
    addAndMakeVisible(*divisor1_);

    divisor2_ = std::make_unique<VerticalDividerComponent>();
    addAndMakeVisible(*divisor2_);

    auto btnRemover = std::make_unique<PillButton>("Reject selected");
    btnRemover->tamanhoFonte = 12.5f;
    btnRemover->corTextoCustom = juce::Colour(0xffcc3333);
    btnRemover->corBordaCustom = juce::Colour(0xffcc3333);
    btnRemover->onClick = [this] { removerSelecionadosDoIntake(); };
    btnRemover->setTooltip("Remove selected items from intake");
    btnRemover->setAlpha(1.0f);
    btnRemover->setEnabled(true);
    btnRemoverSelecao_ = std::move(btnRemover);
    addAndMakeVisible(*btnRemoverSelecao_);

    // 4. Main Table List
    tabela_ = std::make_unique<juce::TableListBox>("IntakeTable", this);
    tabela_->setHeaderHeight(28);
    tabela_->setRowHeight(32);
    tabela_->setMultipleSelectionEnabled(true);

    auto& hdr = tabela_->getHeader();
    hdr.addColumn("", kColSelect, 36, 36, 36, juce::TableHeaderComponent::notSortable);
    hdr.addColumn("TYPE", kColType, 80, 70, 110, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("ASSET / FILENAME", kColName, 220, 140, 500, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("DATE CREATED", kColDateCreated, 150, 110, 200, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("SIZE", kColSize, 80, 60, 120, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("PATH", kColPath, 250, 120, 600, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("CONTENT", kColCollection, 150, 100, 240, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("ORIGINAL SOURCE MEDIUM", kColSourceMedia, 200, 130, 350, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("ACTION", kColAction, 80, 70, 100, juce::TableHeaderComponent::notSortable);
    addAndMakeVisible(*tabela_);

    gridComponent_ = std::make_unique<ThumbnailsGridComponent>(*this);
    gridViewport_ = std::make_unique<juce::Viewport>();
    gridViewport_->setViewedComponent(gridComponent_.get(), false);
    gridViewport_->setScrollBarsShown(true, false);
    gridViewport_->setVisible(false);
    addChildComponent(*gridViewport_);

    recarregar();
}

IntakeWorkspaceComponent::~IntakeWorkspaceComponent() = default;

void IntakeWorkspaceComponent::carregarItens() {
    todosItens_.clear();
    auto quarentena = projeto_.listarItensEmQuarentena();

    for (const auto& item : quarentena) {
        ItemIntake it;
        it.id = item.id;
        it.titulo = juce::String::fromUTF8(item.titulo.c_str());
        it.nomeArquivo = item.nomeOriginalArquivo.empty() ? it.titulo : juce::String::fromUTF8(item.nomeOriginalArquivo.c_str());
        it.extensao = juce::String(item.extensaoArquivo).toUpperCase();
        it.tamanhoBytes = item.tamanhoBytes;

        auto cat = matriz::ingest::categoriaPorExtensao(juce::String(item.extensaoArquivo));
        switch (cat) {
            case matriz::ingest::CategoriaMidia::Audio:      it.categoria = "Audio"; break;
            case matriz::ingest::CategoriaMidia::Video:      it.categoria = "Video"; break;
            case matriz::ingest::CategoriaMidia::Imagem:     it.categoria = "Image"; break;
            case matriz::ingest::CategoriaMidia::Documento:  it.categoria = "Document"; break;
            case matriz::ingest::CategoriaMidia::Texto:      it.categoria = "Document"; break;
            case matriz::ingest::CategoriaMidia::Sessao:     it.categoria = "Project"; break;
            default:                                         it.categoria = "Other"; break;
        }

        it.collection = projeto_.lerMetadado(item.id, "collection_type").value_or("");
        it.sourceMedia = projeto_.lerMetadado(item.id, "source_media").value_or("");
        it.offline = item.offline;
        it.selecionado = false;

        it.dataCriacao = juce::String::fromUTF8(projeto_.lerMetadado(item.id, "dc_created").value_or("").c_str());
        if (it.dataCriacao.isEmpty()) {
            it.dataCriacao = juce::String::fromUTF8(projeto_.lerMetadado(item.id, "data_criacao").value_or("").c_str());
        }

        auto caminhoOpt = projeto_.caminhoDeOrigem(item.id);
        auto arqPrinc = projeto_.arquivoPrincipal(item.id);
        it.caminhoOrigem = caminhoOpt ? *caminhoOpt : (arqPrinc ? arqPrinc->caminhoAbsoluto : "");

        if (it.dataCriacao.isEmpty() && !it.caminhoOrigem.isEmpty()) {
            juce::File f(it.caminhoOrigem);
            if (f.existsAsFile()) {
                it.dataCriacao = f.getCreationTime().formatted("%Y-%m-%d %H:%M:%S");
            }
        }

        todosItens_.push_back(std::move(it));
    }

    if (ultimoSortColumnId_ > 0) {
        sortOrderChanged(ultimoSortColumnId_, sortAscendente_);
    } else {
        atualizarFiltragem();
    }
    atualizarContagens();
}

void IntakeWorkspaceComponent::definirModoVisao(ModoVisao modo) {
    if (modoVisao_ != modo) {
        modoVisao_ = modo;
        const auto& tk = tema();
        if (btnVisaoLista_) {
            btnVisaoLista_->setColour(juce::TextButton::buttonColourId, modoVisao_ == ModoVisao::Lista ? tk.acento : tk.painelAlt);
            btnVisaoLista_->setColour(juce::TextButton::textColourOffId, modoVisao_ == ModoVisao::Lista ? tk.textoSobreAcento : tk.textoSecundario);
        }
        if (btnVisaoIcones_) {
            btnVisaoIcones_->setColour(juce::TextButton::buttonColourId, modoVisao_ == ModoVisao::Icones ? tk.acento : tk.painelAlt);
            btnVisaoIcones_->setColour(juce::TextButton::textColourOffId, modoVisao_ == ModoVisao::Icones ? tk.textoSobreAcento : tk.textoSecundario);
        }
        if (tabela_) tabela_->setVisible(modoVisao_ == ModoVisao::Lista);
        if (gridViewport_) {
            gridViewport_->setVisible(modoVisao_ == ModoVisao::Icones);
            if (gridComponent_) gridComponent_->recalcularLayout();
        }
        resized();
        repaint();
    }
}

void IntakeWorkspaceComponent::atualizarFiltragem() {
    indicesFiltrados_.clear();
    for (size_t i = 0; i < todosItens_.size(); ++i) {
        if (filtroCategoriaAtual_ == "ALL" || todosItens_[i].categoria == filtroCategoriaAtual_) {
            indicesFiltrados_.push_back(static_cast<int>(i));
        }
    }

    auto updatePill = [](juce::TextButton* btn, bool active) {
        if (auto* pill = dynamic_cast<PillButton*>(btn)) {
            pill->ativo = active;
            pill->repaint();
        }
    };

    updatePill(btnFiltroAll_.get(), filtroCategoriaAtual_ == "ALL");
    updatePill(btnFiltroAudio_.get(), filtroCategoriaAtual_ == "Audio");
    updatePill(btnFiltroVideo_.get(), filtroCategoriaAtual_ == "Video");
    updatePill(btnFiltroImage_.get(), filtroCategoriaAtual_ == "Image");
    updatePill(btnFiltroDoc_.get(), filtroCategoriaAtual_ == "Document");
    updatePill(btnFiltroOther_.get(), filtroCategoriaAtual_ == "Other");

    if (tabela_) tabela_->updateContent();
    if (gridComponent_) {
        gridComponent_->recalcularLayout();
        gridComponent_->repaint();
    }
    repaint();
}

void IntakeWorkspaceComponent::atualizarContagens() {
    contagemAudio_ = 0;
    contagemVideo_ = 0;
    contagemImage_ = 0;
    contagemDoc_ = 0;
    contagemOther_ = 0;

    int totalSelecionados = 0;
    for (const auto& item : todosItens_) {
        if (item.categoria == "Audio") contagemAudio_++;
        else if (item.categoria == "Video") contagemVideo_++;
        else if (item.categoria == "Image") contagemImage_++;
        else if (item.categoria == "Document") contagemDoc_++;
        else contagemOther_++;

        if (item.selecionado) totalSelecionados++;
    }

    if (btnFiltroAll_) btnFiltroAll_->setButtonText("All (" + juce::String(todosItens_.size()) + ")");
    if (btnFiltroAudio_) btnFiltroAudio_->setButtonText("Audio (" + juce::String(contagemAudio_) + ")");
    if (btnFiltroVideo_) btnFiltroVideo_->setButtonText("Video (" + juce::String(contagemVideo_) + ")");
    if (btnFiltroImage_) btnFiltroImage_->setButtonText("Images (" + juce::String(contagemImage_) + ")");
    if (btnFiltroDoc_) btnFiltroDoc_->setButtonText("Documents (" + juce::String(contagemDoc_) + ")");
    if (btnFiltroOther_) btnFiltroOther_->setButtonText("Other (" + juce::String(contagemOther_) + ")");

    if (lblContadorTotal_) {
        lblContadorTotal_->setText(juce::String(todosItens_.size()) + " active in intake", juce::dontSendNotification);
    }

    if (lblSubtitulo_) {
        lblSubtitulo_->setText(juce::String(totalSelecionados) + " selected", juce::dontSendNotification);
    }

    if (btnConfirmarSelecao_) {
        btnConfirmarSelecao_->setEnabled(totalSelecionados > 0);
        btnConfirmarSelecao_->setAlpha(totalSelecionados > 0 ? 1.0f : 0.4f);
    }

    if (btnConfirmarTodos_) {
        btnConfirmarTodos_->setEnabled(!todosItens_.empty());
        btnConfirmarTodos_->setAlpha(!todosItens_.empty() ? 1.0f : 0.4f);
    }

    if (btnRemoverSelecao_) {
        btnRemoverSelecao_->setEnabled(true);
        btnRemoverSelecao_->setAlpha(1.0f);
    }
}

void IntakeWorkspaceComponent::recarregar() {
    carregarItens();
}

std::set<std::string> IntakeWorkspaceComponent::itensSelecionados() const {
    std::set<std::string> ids;
    for (const auto& item : todosItens_) {
        if (item.selecionado) ids.insert(item.id);
    }
    return ids;
}

void IntakeWorkspaceComponent::selecionarTodos(bool selecionar) {
    for (int idx : indicesFiltrados_) {
        if (idx >= 0 && idx < static_cast<int>(todosItens_.size())) {
            todosItens_[static_cast<size_t>(idx)].selecionado = selecionar;
        }
    }
    if (tabela_) {
        tabela_->updateContent();
        tabela_->repaint();
    }
    if (gridComponent_) {
        gridComponent_->repaint();
    }
    atualizarContagens();
}

void IntakeWorkspaceComponent::selecionarPorCategoria(const juce::String& categoria) {
    for (auto& item : todosItens_) {
        if (categoria == "ALL" || item.categoria == categoria) {
            item.selecionado = true;
        } else {
            item.selecionado = false;
        }
    }
    if (tabela_) {
        tabela_->updateContent();
        tabela_->repaint();
    }
    if (gridComponent_) {
        gridComponent_->repaint();
    }
    atualizarContagens();
}

void IntakeWorkspaceComponent::aplicarColecaoAosSelecionados(const juce::String& colecao) {
    for (auto& item : todosItens_) {
        if (item.selecionado) {
            definirColecaoItem(item.id, colecao);
            item.collection = colecao;
        }
    }

    if (tabela_) tabela_->repaint();
    if (gridComponent_) gridComponent_->repaint();
    atualizarContagens();
}

void IntakeWorkspaceComponent::definirColecaoItem(const std::string& itemId, const juce::String& colecao) {
    projeto_.salvarMetadado(itemId, "collection_type", colecao.toStdString());
}

void IntakeWorkspaceComponent::aplicarOriginalSourceMediumAosSelecionados(const std::string& sourceMediaJson) {
    for (auto& item : todosItens_) {
        if (item.selecionado) {
            definirSourceMediaItem(item.id, sourceMediaJson);
            item.sourceMedia = juce::String::fromUTF8(sourceMediaJson.c_str());
        }
    }
    if (tabela_) tabela_->repaint();
    if (gridComponent_) gridComponent_->repaint();
}

void IntakeWorkspaceComponent::definirSourceMediaItem(const std::string& itemId, const std::string& sourceMediaJson) {
    projeto_.salvarMetadado(itemId, "source_media", sourceMediaJson);
}

void IntakeWorkspaceComponent::mostrarEditorOriginalSourceMediumLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<OriginalSourceMediumPopupContent>(
        "", true, [this](const std::string& val) {
            aplicarOriginalSourceMediumAosSelecionados(val);
        });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorOriginalSourceMedium(int itemIndex, juce::Rectangle<int> screenBounds) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;
    std::string itemId = todosItens_[static_cast<size_t>(itemIndex)].id;
    std::string currentVal = todosItens_[static_cast<size_t>(itemIndex)].sourceMedia.toStdString();

    auto content = std::make_unique<OriginalSourceMediumPopupContent>(
        currentVal, false, [this, itemId, itemIndex](const std::string& val) {
            definirSourceMediaItem(itemId, val);
            if (itemIndex < static_cast<int>(todosItens_.size())) {
                todosItens_[static_cast<size_t>(itemIndex)].sourceMedia = juce::String::fromUTF8(val.c_str());
            }
            if (tabela_) tabela_->repaint();
        });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorGeolocationLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<GeoLocationPopupContent>(
        true, [this](const std::string& coords, const std::string& addr, const std::string& city, const std::string& state, const std::string& country) {
            aplicarGeolocationAosSelecionados(coords, addr, city, state, country);
        });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::aplicarGeolocationAosSelecionados(const std::string& coords, const std::string& addr, const std::string& city, const std::string& state, const std::string& country) {
    matriz::analytics::AssetGeolocation geoTemplate;
    if (!coords.empty()) {
        auto commaPos = coords.find(',');
        if (commaPos != std::string::npos) {
            try {
                double lat = std::stod(coords.substr(0, commaPos));
                double lng = std::stod(coords.substr(commaPos + 1));
                if (lat >= -90.0 && lat <= 90.0 && lng >= -180.0 && lng <= 180.0) {
                    geoTemplate.latitude = lat;
                    geoTemplate.longitude = lng;
                    geoTemplate.source = matriz::analytics::GeoSource::UserCoordinates;
                }
            } catch (...) {}
        }
    }
    if (!addr.empty()) {
        geoTemplate.formattedAddress = addr;
        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserAddress;
    }
    if (!city.empty()) {
        geoTemplate.city = city;
        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserCity;
    }
    if (!state.empty()) {
        geoTemplate.stateProvince = state;
        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserState;
    }
    if (!country.empty()) {
        geoTemplate.country = country;
        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserCountry;
    }

    if (!geoTemplate.hasAnyLocationData()) return;

    std::vector<std::string> ids;
    for (const auto& item : todosItens_) {
        if (item.selecionado) ids.push_back(item.id);
    }
    if (ids.empty()) return;

    matriz::analytics::AssetGeolocationRepository::salvarEmLote(projeto_.projeto().registro(), ids, geoTemplate);
    if (tabela_) tabela_->repaint();
}

void IntakeWorkspaceComponent::confirmarSelecaoParaGrid() {
    std::vector<std::string> ids;
    for (const auto& item : todosItens_) {
        if (item.selecionado) ids.push_back(item.id);
    }
    if (ids.empty()) return;

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "intake_confirm", "Promoting to GRID", static_cast<int>(ids.size()), nullptr,
        "Promoting " + juce::String(ids.size()) + " selected items to GRID...");

    projeto_.confirmarLoteGrid(ids);
    recarregar();

    ProgressoGlobal::obterInstancia().concluirTarefa(
        "intake_confirm", juce::String(ids.size()) + " items confirmed to GRID");

    if (aoConfirmarParaGrid) aoConfirmarParaGrid();
}

void IntakeWorkspaceComponent::confirmarTodosParaGrid() {
    if (todosItens_.empty()) return;
    std::vector<std::string> ids;
    ids.reserve(todosItens_.size());
    for (const auto& item : todosItens_) {
        ids.push_back(item.id);
    }

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "intake_confirm_all", "Promoting to GRID", static_cast<int>(ids.size()), nullptr,
        "Promoting all " + juce::String(ids.size()) + " intake items to GRID...");

    projeto_.confirmarLoteGrid(ids);
    recarregar();

    ProgressoGlobal::obterInstancia().concluirTarefa(
        "intake_confirm_all", "All " + juce::String(ids.size()) + " items confirmed to GRID");

    if (aoConfirmarParaGrid) aoConfirmarParaGrid();
}

void IntakeWorkspaceComponent::removerSelecionadosDoIntake() {
    std::vector<std::string> ids;
    for (const auto& item : todosItens_) {
        if (item.selecionado) ids.push_back(item.id);
    }
    if (ids.empty()) return;

    projeto_.removerItensDoProjeto(ids);
    recarregar();
}

void IntakeWorkspaceComponent::mostrarMenuColecaoParaItem(int itemIndex, juce::Rectangle<int> screenBounds) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;

    juce::PopupMenu menu;
    menu.addItem(1, "None (Clear)");
    menu.addSeparator();

    int id = 2;
    std::map<int, juce::String> idParaNome;
    for (const auto& cat : vocabularioColecoes()) {
        juce::PopupMenu sub;
        for (const auto& item : cat.itens) {
            sub.addItem(id, item);
            idParaNome[id] = item;
            id++;
        }
        menu.addSubMenu(cat.grupo, sub);
    }

    juce::Component::SafePointer<IntakeWorkspaceComponent> safeThis(this);
    std::string itemId = todosItens_[static_cast<size_t>(itemIndex)].id;

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenBounds),
                       [safeThis, itemId, idParaNome, itemIndex](int result) {
        if (!safeThis || result == 0) return;
        juce::String chosen;
        if (result > 1) {
            auto it = idParaNome.find(result);
            if (it != idParaNome.end()) chosen = it->second;
        }

        safeThis->definirColecaoItem(itemId, chosen);
        if (itemIndex < static_cast<int>(safeThis->todosItens_.size())) {
            safeThis->todosItens_[static_cast<size_t>(itemIndex)].collection = chosen;
        }
        if (safeThis->tabela_) safeThis->tabela_->repaint();
    });
}

// TableListBoxModel implementation
int IntakeWorkspaceComponent::getNumRows() {
    return static_cast<int>(indicesFiltrados_.size());
}

void IntakeWorkspaceComponent::paintRowBackground(juce::Graphics& g, int rowNumber, int, int, bool rowIsSelected) {
    const auto& tk = tema();
    if (rowIsSelected) {
        g.fillAll(tk.painelAlt.withAlpha(0.7f));
    } else if (rowNumber % 2 == 1) {
        g.fillAll(juce::Colour(0x08ffffff));
    }
}

void IntakeWorkspaceComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(indicesFiltrados_.size())) return;
    int realIndex = indicesFiltrados_[static_cast<size_t>(rowNumber)];
    if (realIndex < 0 || realIndex >= static_cast<int>(todosItens_.size())) return;

    const auto& item = todosItens_[static_cast<size_t>(realIndex)];
    const auto& tk = tema();

    if (columnId == kColType) {
        juce::Colour corCat = corParaCategoria(item.categoria);
        auto badgeArea = juce::Rectangle<int>(4, 5, width - 8, height - 10);
        g.setColour(corCat.withAlpha(0.2f));
        g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
        g.setColour(corCat);
        g.drawRoundedRectangle(badgeArea.toFloat(), 3.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(item.categoria.toUpperCase(), badgeArea, juce::Justification::centred, true);
    } else if (columnId == kColName) {
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        if (item.offline) {
            int offBadgeW = 58;
            auto offBadge = juce::Rectangle<int>(width - offBadgeW - 6, (height - 18) / 2, offBadgeW, 18);
            g.drawText(item.nomeArquivo, 6, 0, width - offBadgeW - 16, height, juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xd0000000));
            g.fillRoundedRectangle(offBadge.toFloat(), 3.0f);
            g.setColour(juce::Colour(0xfff97316));
            g.drawRoundedRectangle(offBadge.toFloat(), 3.0f, 1.0f);
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            g.drawText("OFFLINE", offBadge, juce::Justification::centred);
        } else {
            g.drawText(item.nomeArquivo, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
    } else if (columnId == kColDateCreated) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(item.dataCriacao.isNotEmpty() ? item.dataCriacao : "-", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColSize) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(formatarBytes(item.tamanhoBytes), 4, 0, width - 8, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColPath) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(item.caminhoOrigem.isNotEmpty() ? item.caminhoOrigem : "-", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColCollection) {
        if (!item.collection.isEmpty()) {
            auto badgeArea = juce::Rectangle<int>(4, 5, width - 8, height - 10);
            g.setColour(tk.acento.withAlpha(0.25f));
            g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
            g.setColour(tk.acento);
            g.drawRoundedRectangle(badgeArea.toFloat(), 3.0f, 1.0f);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(item.collection, badgeArea, juce::Justification::centred, true);
        } else {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
            g.drawText("None", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
    } else if (columnId == kColSourceMedia) {
        if (!item.sourceMedia.isEmpty()) {
            auto info = OriginalSourceMediumInfo::deserialize(item.sourceMedia.toStdString());
            juce::String text = juce::String::fromUTF8(info.toDisplaySummary().c_str());
            if (text.isEmpty() || text == "None / Unknown") {
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
                g.drawText("None", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
            } else {
                auto badgeArea = juce::Rectangle<int>(4, 5, width - 8, height - 10);
                g.setColour(juce::Colour(0xff2a9d8f).withAlpha(0.20f));
                g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
                g.setColour(juce::Colour(0xff2a9d8f));
                g.drawRoundedRectangle(badgeArea.toFloat(), 3.0f, 1.0f);
                g.setFont(juce::Font(juce::FontOptions(11.0f)));
                g.drawText(text, badgeArea.reduced(6, 0), juce::Justification::centredLeft, true);
            }
        } else {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
            g.drawText("None", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
    }
}

juce::Component* IntakeWorkspaceComponent::refreshComponentForCell(int rowNumber, int columnId, bool, juce::Component* existingComponentToUpdate) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(indicesFiltrados_.size())) {
        delete existingComponentToUpdate;
        return nullptr;
    }
    int realIndex = indicesFiltrados_[static_cast<size_t>(rowNumber)];
    if (realIndex < 0 || realIndex >= static_cast<int>(todosItens_.size())) {
        delete existingComponentToUpdate;
        return nullptr;
    }

    if (columnId == kColSelect) {
        auto* cell = dynamic_cast<CheckboxCellComponent*>(existingComponentToUpdate);
        if (!cell) {
            delete existingComponentToUpdate;
            cell = new CheckboxCellComponent();
        }
        cell->setChecked(todosItens_[static_cast<size_t>(realIndex)].selecionado);
        cell->onToggle = [this, realIndex](bool checked) {
            todosItens_[static_cast<size_t>(realIndex)].selecionado = checked;
            atualizarContagens();
        };
        return cell;
    } else if (columnId == kColAction) {
        auto* cell = dynamic_cast<ActionCellComponent*>(existingComponentToUpdate);
        if (!cell) {
            delete existingComponentToUpdate;
            cell = new ActionCellComponent();
        }
        cell->onClick = [this, realIndex](juce::Rectangle<int> bounds) {
            mostrarMenuColecaoParaItem(realIndex, bounds);
        };
        return cell;
    }

    delete existingComponentToUpdate;
    return nullptr;
}

void IntakeWorkspaceComponent::selectedRowsChanged(int) {
    // Avoid double toggle since row click is handled in cellClicked
}

void IntakeWorkspaceComponent::sortOrderChanged(int newSortColumnId, bool isForwards) {
    ultimoSortColumnId_ = newSortColumnId;
    sortAscendente_ = isForwards;

    std::stable_sort(todosItens_.begin(), todosItens_.end(), [newSortColumnId, isForwards](const ItemIntake& a, const ItemIntake& b) {
        int cmp = 0;
        switch (newSortColumnId) {
            case kColType:
                cmp = a.categoria.compareIgnoreCase(b.categoria);
                break;
            case kColName:
                cmp = a.nomeArquivo.compareIgnoreCase(b.nomeArquivo);
                if (cmp == 0) cmp = a.titulo.compareIgnoreCase(b.titulo);
                break;
            case kColDateCreated:
                cmp = a.dataCriacao.compareIgnoreCase(b.dataCriacao);
                break;
            case kColSize:
                if (a.tamanhoBytes < b.tamanhoBytes) cmp = -1;
                else if (a.tamanhoBytes > b.tamanhoBytes) cmp = 1;
                break;
            case kColPath:
                cmp = a.caminhoOrigem.compareIgnoreCase(b.caminhoOrigem);
                break;
            case kColCollection:
                cmp = a.collection.compareIgnoreCase(b.collection);
                break;
            case kColSourceMedia:
                cmp = a.sourceMedia.compareIgnoreCase(b.sourceMedia);
                break;
            default:
                cmp = 0;
                break;
        }
        return isForwards ? (cmp < 0) : (cmp > 0);
    });

    atualizarFiltragem();
}

void IntakeWorkspaceComponent::cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(indicesFiltrados_.size())) return;
    int realIndex = indicesFiltrados_[static_cast<size_t>(rowNumber)];
    if (realIndex < 0 || realIndex >= static_cast<int>(todosItens_.size())) return;

    if (e.mods.isPopupMenu()) {
        mostrarMenuContexto(realIndex, e.getScreenPosition());
        return;
    }

    if (e.getNumberOfClicks() >= 2) {
        abrirArquivoOrigem(rowNumber);
        return;
    }

    if (columnId == kColSourceMedia) {
        mostrarEditorOriginalSourceMedium(realIndex, juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1));
        return;
    }
    if (columnId == kColCollection) {
        mostrarMenuColecaoParaItem(realIndex, juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1));
        return;
    }

    if (columnId != kColSelect && columnId != kColAction) {
        todosItens_[static_cast<size_t>(realIndex)].selecionado = !todosItens_[static_cast<size_t>(realIndex)].selecionado;
        if (tabela_) {
            tabela_->updateContent();
            tabela_->repaint();
        }
        atualizarContagens();
    }
}

void IntakeWorkspaceComponent::cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&) {
    abrirArquivoOrigem(rowNumber);
}

void IntakeWorkspaceComponent::rejeitarItemDoIntake(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;
    std::string itemId = todosItens_[static_cast<size_t>(itemIndex)].id;
    projeto_.removerItensDoProjeto({itemId});
    recarregar();
}

void IntakeWorkspaceComponent::mostrarMenuContexto(int itemIndex, juce::Point<int> screenPos) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;

    juce::PopupMenu m;
    m.addItem(1, "Reject");
    m.addItem(2, "Show at Source");
    m.addSeparator();
    m.addItem(3, "Get Info...");

    juce::Component::SafePointer<IntakeWorkspaceComponent> safeThis(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [safeThis, itemIndex](int result) {
            if (!safeThis) return;
            if (result == 1) {
                safeThis->rejeitarItemDoIntake(itemIndex);
            } else if (result == 2) {
                safeThis->abrirArquivoOrigem(itemIndex);
            } else if (result == 3) {
                safeThis->mostrarDialogoGetInfo(itemIndex);
            }
        });
}

void IntakeWorkspaceComponent::mostrarDialogoGetInfo(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;
    const auto& item = todosItens_[static_cast<size_t>(itemIndex)];

    juce::String msg;
    msg << "ASSET & FILE INFORMATION\n";
    msg << "-----------------------------------------\n";
    msg << "Filename: " << item.nomeArquivo << "\n";
    msg << "Title: " << item.titulo << "\n";
    msg << "Asset ID: " << item.id << "\n";
    msg << "Category: " << item.categoria << " (" << item.extensao << ")\n";
    msg << "Size: " << formatarBytes(item.tamanhoBytes) << " (" << juce::String(item.tamanhoBytes) << " bytes)\n";
    msg << "Date Created: " << (item.dataCriacao.isNotEmpty() ? item.dataCriacao : "Unknown") << "\n";
    msg << "Original Path: " << (item.caminhoOrigem.isNotEmpty() ? item.caminhoOrigem : "Unknown") << "\n\n";

    msg << "METADATA ASSIGNMENTS\n";
    msg << "-----------------------------------------\n";
    msg << "Content: " << (item.collection.isNotEmpty() ? item.collection : "None") << "\n";
    if (item.sourceMedia.isNotEmpty()) {
        auto info = OriginalSourceMediumInfo::deserialize(item.sourceMedia.toStdString());
        msg << "Original Medium: " << juce::String::fromUTF8(info.toDisplaySummary().c_str()) << "\n";
        if (!info.recordingDevice.empty()) {
            msg << "Recording Device: " << juce::String::fromUTF8(info.recordingDevice.c_str()) << "\n";
        }
    } else {
        msg << "Original Medium: None\n";
    }

    auto notes = projeto_.lerMetadado(item.id, "notas_livres").value_or("");
    if (!notes.empty()) {
        msg << "Notes: " << juce::String::fromUTF8(notes.c_str()) << "\n";
    }

    auto creator = projeto_.lerMetadado(item.id, "dc_creator").value_or("");
    if (!creator.empty()) {
        msg << "Creator: " << juce::String::fromUTF8(creator.c_str()) << "\n";
    }

    auto rights = projeto_.lerMetadado(item.id, "dc_rights").value_or("");
    if (!rights.empty()) {
        msg << "Rights: " << juce::String::fromUTF8(rights.c_str()) << "\n";
    }

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::InfoIcon,
        "File Info - " + item.nomeArquivo,
        msg,
        "Reveal in Finder",
        "Close",
        this,
        juce::ModalCallbackFunction::create([this, itemIndex](int result) {
            if (result == 1) {
                abrirArquivoOrigem(itemIndex);
            }
        })
    );
}

void IntakeWorkspaceComponent::abrirArquivoOrigem(int rowNumber) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(indicesFiltrados_.size())) return;
    int realIndex = indicesFiltrados_[static_cast<size_t>(rowNumber)];
    if (realIndex < 0 || realIndex >= static_cast<int>(todosItens_.size())) return;

    const auto& item = todosItens_[static_cast<size_t>(realIndex)];
    auto caminhoOpt = projeto_.caminhoDeOrigem(item.id);
    if (caminhoOpt && !caminhoOpt->isEmpty()) {
        juce::File f(*caminhoOpt);
        if (f.existsAsFile() || f.isDirectory()) {
            f.revealToUser();
            return;
        }
    }

    auto arqPrinc = projeto_.arquivoPrincipal(item.id);
    if (arqPrinc && !arqPrinc->caminhoAbsoluto.isEmpty()) {
        juce::File f(arqPrinc->caminhoAbsoluto);
        if (f.existsAsFile() || f.isDirectory()) {
            f.revealToUser();
            return;
        }
    }
}

// FileDragAndDropTarget implementation
bool IntakeWorkspaceComponent::isInterestedInFileDrag(const juce::StringArray&) {
    return true;
}

void IntakeWorkspaceComponent::filesDropped(const juce::StringArray& files, int, int) {
    juce::Array<juce::File> arquivos;
    for (const auto& caminho : files) {
        arquivos.add(juce::File(caminho));
    }
    if (aoIngerirArquivosDireto) {
        aoIngerirArquivosDireto(arquivos);
    } else if (aoPedirIngerirArquivos) {
        aoPedirIngerirArquivos();
    }
}

void IntakeWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Entire toolbar background = same sidebar background (tk.painel)
    g.setColour(tk.painel);
    g.fillRect(0, 0, getWidth(), 120);

    // Row dividers
    g.setColour(tk.borda);
    g.fillRect(0, 39, getWidth(), 1);
    g.fillRect(0, 75, getWidth(), 1);
    g.fillRect(0, 119, getWidth(), 1);
}

void IntakeWorkspaceComponent::resized() {
    auto area = getLocalBounds();

    // LINHA 1 (BatchHeaderBar - 40px high)
    auto row1 = area.removeFromTop(40).reduced(14, 6);
    lblTitulo_->setBounds(row1.removeFromLeft(140));
    row1.removeFromLeft(8);
    lblContadorTotal_->setBounds(row1.removeFromLeft(150));
    row1.removeFromLeft(12);
    if (btnVisaoLista_) btnVisaoLista_->setBounds(row1.removeFromLeft(34));
    row1.removeFromLeft(4);
    if (btnVisaoIcones_) btnVisaoIcones_->setBounds(row1.removeFromLeft(34));
    btnIngerir_->setBounds(row1.removeFromRight(220));

    // LINHA 2 (FilterBar - 36px high)
    auto row2 = area.removeFromTop(36).reduced(14, 4);
    btnFiltroAll_->setBounds(row2.removeFromLeft(75));
    row2.removeFromLeft(6);
    btnFiltroAudio_->setBounds(row2.removeFromLeft(95));
    row2.removeFromLeft(6);
    btnFiltroVideo_->setBounds(row2.removeFromLeft(95));
    row2.removeFromLeft(6);
    btnFiltroImage_->setBounds(row2.removeFromLeft(105));
    row2.removeFromLeft(6);
    btnFiltroDoc_->setBounds(row2.removeFromLeft(130));
    row2.removeFromLeft(6);
    btnFiltroOther_->setBounds(row2.removeFromLeft(90));

    // LINHA 3 (SelectionActionBar - 44px high)
    // Strictly left-aligned sequential flow with 16px gaps to dividers
    const int row3Y = 76;
    const int compY = row3Y + 8; // 28px height centered in 44px
    const int divY = row3Y + 12; // 20px height centered in 44px

    int x = 14;

    // Cluster 1: [Selecionar tudo / Limpar seleção]
    lblSubtitulo_->setBounds(x, compY, 105, 28);
    x += 105 + 8;
    btnSelecionarTodos_->setBounds(x, compY, 115, 28);
    x += 115 + 8;
    btnLimparSelecao_->setBounds(x, compY, 120, 28);
    x += 120;

    // Gap (16px) -> Divisor 1 -> Gap (16px)
    x += 16;
    if (divisor1_) divisor1_->setBounds(x, divY, 1, 20);
    x += 1 + 16;

    // Cluster 2: [Coleção + Aplicar] | [Original Medium...] | [Geo Location...]
    lblRotuloColecao_->setBounds(x, compY, 75, 28);
    x += 75 + 8;
    comboColecaoLote_->setBounds(x, compY, 155, 28);
    x += 155 + 8;
    btnAplicarColecaoLote_->setBounds(x, compY, 75, 28);
    x += 75 + 8;
    btnOriginalMediumLote_->setBounds(x, compY, 150, 28);
    x += 150 + 8;
    btnGeolocationLote_->setBounds(x, compY, 130, 28);
    x += 130;

    // Gap (16px) -> Divisor 2 -> Gap (16px)
    x += 16;
    if (divisor2_) divisor2_->setBounds(x, divY, 1, 20);
    x += 1 + 16;

    // Cluster 3: [Send selected to GRID / Send all to GRID / Reject selected]
    btnConfirmarSelecao_->setBounds(x, compY, 185, 28);
    x += 185 + 8;
    btnConfirmarTodos_->setBounds(x, compY, 155, 28);
    x += 155 + 8;
    btnRemoverSelecao_->setBounds(x, compY, 150, 28);

    // Main Table or Grid fills remaining area (from y = 120)
    if (tabela_) {
        tabela_->setBounds(0, 120, getWidth(), getHeight() - 120);
    }
    if (gridViewport_) {
        gridViewport_->setBounds(0, 120, getWidth(), getHeight() - 120);
        if (gridComponent_) gridComponent_->recalcularLayout();
    }
}

} // namespace matriz::ui
