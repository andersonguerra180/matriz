// ==============================================================================
// INTAKE WORKSPACE COMPONENT
// STRICT APP-WIDE RULE: 100% ENGLISH UI. ZERO PORTUGUESE TEXT IN USER INTERFACE.
// ==============================================================================

#include "IntakeWorkspaceComponent.h"

#include <AssetsBinaryData.h>
#include "OriginalSourceMedium.h"
#include "ProjetoAberto.h"
#include "ProgressoGlobal.h"
#include "Tokens.h"
#include "../Analytics/AssetGeolocation.h"
#include "../Ingest/LeituraTecnica.h"
#include "../I18n/Strings.h"

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

class GoogleDriveIconButton : public juce::Button {
public:
    GoogleDriveIconButton() : juce::Button("GoogleDrive") {
        img_ = juce::ImageFileFormat::loadFrom(AssetsBinaryData::googledrive_png, AssetsBinaryData::googledrive_pngSize);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat();
        
        g.setColour(shouldDrawButtonAsDown ? tk.painelAlt.darker(0.1f)
                    : (shouldDrawButtonAsHighlighted ? tk.painelAlt.brighter(0.15f) : tk.painelAlt));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(shouldDrawButtonAsHighlighted ? juce::Colour(0xff1a73e8) : tk.borda);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);

        if (img_.isValid()) {
            auto iconArea = r.reduced(4.0f);
            g.drawImageWithin(img_, (int)iconArea.getX(), (int)iconArea.getY(),
                              (int)iconArea.getWidth(), (int)iconArea.getHeight(),
                              juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, false);
        } else {
            g.setColour(juce::Colour(0xff1a73e8));
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText("GD", getLocalBounds(), juce::Justification::centred);
        }
    }
private:
    juce::Image img_;
};

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
        btn_.setButtonText(i18n::t("intake.definir"));
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
        const auto& tk = tema();
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        float radius = 5.0f;

        juce::Colour bg = tk.painelAlt;
        if (shouldDrawButtonAsDown) bg = tk.painelAlt.darker(0.15f);
        else if (shouldDrawButtonAsHighlighted) bg = tk.painelAlt.brighter(0.10f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, radius);

        g.setColour(tk.borda);
        g.drawRoundedRectangle(bounds, radius, 1.0f);

        g.setColour(tk.textoPrimario);
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
        g.setColour(tema().borda);
        g.fillRect(0, 0, getWidth(), getHeight());
    }
};

class OriginalSourceMediumPopupContent : public juce::Component {
public:
    OriginalSourceMediumPopupContent(const std::string& initialVal, bool isBatch, std::function<void(const std::string&)> onApply)
        : onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", isBatch ? i18n::t("intake.popup_osm_batch") : i18n::t("intake.popup_osm"));
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

        btnApply_ = std::make_unique<PillButton>(isBatch ? i18n::t("intake.btn_aplicar_selecionados") : i18n::t("intake.btn_aplicar"));
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

        lblTitle_ = std::make_unique<juce::Label>("", isBatch ? i18n::t("intake.popup_geo_batch") : i18n::t("intake.popup_geo"));
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

        btnApply_ = std::make_unique<PillButton>(isBatch ? i18n::t("intake.btn_aplicar_selecionados") : i18n::t("intake.btn_aplicar"));
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
            g.drawText(i18n::t("intake.sem_itens_filtro"), getLocalBounds(), juce::Justification::centred);
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
        combo.addItem(i18n::t("intake.nenhuma_colecao"), id++);
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

class IntakeWorkspaceComponent::IntakeDragDropEmptyState : public juce::Component,
                                                           public juce::FileDragAndDropTarget {
public:
    IntakeDragDropEmptyState(IntakeWorkspaceComponent& owner) : owner_(owner) {
        setInterceptsMouseClicks(true, true);
    }

    bool isInterestedInFileDrag(const juce::StringArray&) override {
        return true;
    }

    void fileDragEnter(const juce::StringArray&, int, int) override {
        dragOver_ = true;
        repaint();
    }

    void fileDragExit(const juce::StringArray&) override {
        dragOver_ = false;
        repaint();
    }

    void filesDropped(const juce::StringArray& files, int x, int y) override {
        dragOver_ = false;
        repaint();
        owner_.filesDropped(files, x, y);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        hover_ = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override {
        hover_ = false;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (!e.mouseWasDraggedSinceMouseDown()) {
            if (owner_.aoPedirIngerirArquivos) {
                owner_.aoPedirIngerirArquivos();
            } else if (owner_.btnIngerir_ && owner_.btnIngerir_->onClick) {
                owner_.btnIngerir_->onClick();
            }
        }
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
        auto area = getLocalBounds();

        int cardW = juce::jmin(640, getWidth() - 60);
        int cardH = juce::jmin(340, getHeight() - 40);
        if (cardW < 200 || cardH < 150) return;

        auto boxRect = area.withSizeKeepingCentre(cardW, cardH);

        juce::Colour fillCol = dragOver_ ? tk.acento.withAlpha(0.12f)
                             : (hover_ ? tk.painelAlt.withAlpha(0.6f) : tk.painel.withAlpha(0.35f));
        g.setColour(fillCol);
        g.fillRoundedRectangle(boxRect.toFloat(), 14.0f);

        juce::Colour borderCol = dragOver_ ? tk.acento
                               : (hover_ ? tk.bordaFoco : tk.borda.withAlpha(0.85f));
        g.setColour(borderCol);
        float dashLengths[2] = { 8.0f, 6.0f };
        juce::Path p;
        p.addRoundedRectangle(boxRect.toFloat().reduced(1.0f), 14.0f);
        juce::Path dashed;
        juce::PathStrokeType(2.0f).createDashedStroke(dashed, p, dashLengths, 2);
        g.fillPath(dashed);

        auto contentArea = boxRect.reduced(24, 20);

        int iconSize = 72;
        auto iconBox = contentArea.removeFromTop(iconSize + 16).withSizeKeepingCentre(iconSize, iconSize).toFloat();

        juce::Colour iconCol = dragOver_ ? tk.acento : (hover_ ? tk.acento.withAlpha(0.95f) : tk.textoTerciario.interpolatedWith(tk.acento, 0.45f));
        g.setColour(iconCol);

        float ix = iconBox.getX();
        float iy = iconBox.getY();
        float iw = iconBox.getWidth();
        float ih = iconBox.getHeight();

        juce::Path tray;
        float trayTop = iy + ih * 0.44f;
        float trayBottom = iy + ih * 0.96f;
        float trayLeft = ix + iw * 0.12f;
        float trayRight = ix + iw * 0.88f;
        float cr = 10.0f;

        tray.startNewSubPath(trayLeft, trayTop);
        tray.lineTo(trayLeft, trayBottom - cr);
        tray.quadraticTo(trayLeft, trayBottom, trayLeft + cr, trayBottom);
        tray.lineTo(trayRight - cr, trayBottom);
        tray.quadraticTo(trayRight, trayBottom, trayRight, trayBottom - cr);
        tray.lineTo(trayRight, trayTop);
        tray.lineTo(trayRight - iw * 0.22f, trayTop);
        tray.lineTo(trayRight - iw * 0.28f, trayTop + ih * 0.14f);
        tray.lineTo(trayLeft + iw * 0.28f, trayTop + ih * 0.14f);
        tray.lineTo(trayLeft + iw * 0.22f, trayTop);
        tray.closeSubPath();

        g.strokePath(tray, juce::PathStrokeType(2.5f));

        juce::Path arrow;
        float midX = ix + iw * 0.5f;
        float arrowTop = iy + ih * 0.06f;
        float arrowTip = iy + ih * 0.60f;
        float hw = iw * 0.22f;
        float hh = ih * 0.22f;

        arrow.startNewSubPath(midX, arrowTop);
        arrow.lineTo(midX, arrowTip);
        arrow.startNewSubPath(midX - hw, arrowTip - hh);
        arrow.lineTo(midX, arrowTip);
        arrow.lineTo(midX + hw, arrowTip - hh);

        g.strokePath(arrow, juce::PathStrokeType(3.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

        contentArea.removeFromTop(8);
        g.setColour(dragOver_ ? tk.acento : tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
        juce::String titulo = isPt ? juce::String::fromUTF8("ARRASTE E SOLTE ARQUIVOS OU PASTAS AQUI")
                                   : "DRAG & DROP FILES OR FOLDERS HERE";
        g.drawText(titulo, contentArea.removeFromTop(26), juce::Justification::centred);

        contentArea.removeFromTop(6);
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        juce::String subtitulo = isPt ? juce::String::fromUTF8("Suporta áudio, vídeo, imagens, documentos e sessões multitrack\nou clique em qualquer lugar desta área para selecionar")
                                      : "Supports audio, video, images, documents and multitrack sessions\nor click anywhere in this area to browse";
        g.drawFittedText(subtitulo, contentArea.removeFromTop(36), juce::Justification::centred, 2);
    }

private:
    IntakeWorkspaceComponent& owner_;
    bool hover_ = false;
    bool dragOver_ = false;
};

IntakeWorkspaceComponent::IntakeWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    const auto& tk = tema();

    // 1. LINHA 1 (BatchHeaderBar)
    lblTitulo_ = std::make_unique<juce::Label>("", i18n::t("intake.titulo"));
    lblTitulo_->setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    lblContadorTotal_ = std::make_unique<juce::Label>("", "");
    lblContadorTotal_->setFont(juce::Font(juce::FontOptions(13.0f)));
    lblContadorTotal_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblContadorTotal_);

    btnVisaoLista_ = std::make_unique<ViewModeIconButton>(ViewModeIconButton::IconType::List);
    btnVisaoLista_->setAtivo(true);
    btnVisaoLista_->setTooltip(i18n::t("intake.tooltip_lista"));
    btnVisaoLista_->onClick = [this] { definirModoVisao(ModoVisao::Lista); };
    addAndMakeVisible(*btnVisaoLista_);

    btnVisaoIcones_ = std::make_unique<ViewModeIconButton>(ViewModeIconButton::IconType::Grid);
    btnVisaoIcones_->setAtivo(false);
    btnVisaoIcones_->setTooltip(i18n::t("intake.tooltip_icones"));
    btnVisaoIcones_->onClick = [this] { definirModoVisao(ModoVisao::Icones); };
    addAndMakeVisible(*btnVisaoIcones_);

    auto btnIngerirPill = std::make_unique<PillButton>(i18n::t("intake.btn_ingerir"));
    btnIngerirPill->tamanhoFonte = 13.0f;
    btnIngerirPill->corTextoCustom = tk.textoPrimario;
    btnIngerirPill->onClick = [this] { if (aoPedirIngerirArquivos) aoPedirIngerirArquivos(); };
    btnIngerirPill->setTooltip(i18n::t("intake.tooltip_ingerir"));
    btnIngerir_ = std::move(btnIngerirPill);
    addAndMakeVisible(*btnIngerir_);

    // Botão Google Drive — com logo googledrive.png
    auto btnGD = std::make_unique<GoogleDriveIconButton>();
    btnGD->onClick = [this] {
        // Detectar pasta montada do Google Drive Desktop (macOS)
        juce::File gdBase = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                .getChildFile("Library/CloudStorage");
        juce::File gdFolder;
        if (gdBase.isDirectory()) {
            for (auto& child : gdBase.findChildFiles(juce::File::findDirectories, false, "GoogleDrive-*")) {
                auto myDrive = child.getChildFile("My Drive");
                if (myDrive.isDirectory()) { gdFolder = myDrive; break; }
                if (child.isDirectory()) { gdFolder = child; break; }
            }
        }
        // Fallback: pasta legada
        if (!gdFolder.isDirectory())
            gdFolder = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Google Drive");

        if (gdFolder.isDirectory()) {
            // Abrir o seletor de arquivos apontado para a pasta do Google Drive
            if (aoIngerirDeGoogleDrive) {
                aoIngerirDeGoogleDrive(gdFolder);
            } else if (aoPedirIngerirArquivos) {
                aoPedirIngerirArquivos();
            }
        } else {
            bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Google Drive",
                isPt
                    ? juce::String::fromUTF8("Pasta do Google Drive não encontrada.\nInstale o Google Drive para Desktop em drive.google.com/drive/download")
                    : "Google Drive folder not found.\nInstall Google Drive for Desktop from drive.google.com/drive/download");
        }
    };
    btnGoogleDrive_ = std::move(btnGD);
    addAndMakeVisible(*btnGoogleDrive_);

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
    lblSubtitulo_ = std::make_unique<juce::Label>("", "");
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(13.0f)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblSubtitulo_);

    auto btnSelTodos = std::make_unique<PillButton>(i18n::t("intake.selecionar_todos"));
    btnSelTodos->tamanhoFonte = 12.5f;
    btnSelTodos->corTextoCustom = tk.textoPrimario;
    btnSelTodos->onClick = [this] { selecionarTodos(true); };
    btnSelecionarTodos_ = std::move(btnSelTodos);
    addAndMakeVisible(*btnSelecionarTodos_);

    auto btnLimpar = std::make_unique<PillButton>(i18n::t("intake.limpar_selecao"));
    btnLimpar->tamanhoFonte = 12.5f;
    btnLimpar->corTextoCustom = tk.textoSecundario;
    btnLimpar->onClick = [this] { selecionarTodos(false); };
    btnLimparSelecao_ = std::move(btnLimpar);
    addAndMakeVisible(*btnLimparSelecao_);

    // Cluster B (Center)
    lblRotuloColecao_ = std::make_unique<juce::Label>("", i18n::t("intake.rotulo_conteudo"));
    lblRotuloColecao_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    lblRotuloColecao_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblRotuloColecao_);

    comboColecaoLote_ = std::make_unique<juce::ComboBox>();
    comboColecaoLote_->setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
    comboColecaoLote_->setColour(juce::ComboBox::textColourId, tk.textoPrimario);
    comboColecaoLote_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboColecaoLote_->setColour(juce::ComboBox::arrowColourId, tk.textoPrimario);
    popularComboColecoes(*comboColecaoLote_, true);
    addAndMakeVisible(*comboColecaoLote_);

    auto btnAplicar = std::make_unique<PillButton>(i18n::t("intake.btn_aplicar"));
    btnAplicar->tamanhoFonte = 12.5f;
    btnAplicar->corTextoCustom = tk.textoPrimario;
    btnAplicar->corBordaCustom = tk.borda;
    btnAplicar->onClick = [this] {
        juce::String chosen = comboColecaoLote_->getText();
        if (comboColecaoLote_->getSelectedId() == 1 || chosen == i18n::t("intake.nenhuma_colecao") || chosen == "None") {
            aplicarColecaoAosSelecionados("");
        } else {
            aplicarColecaoAosSelecionados(chosen);
        }
    };
    btnAplicarColecaoLote_ = std::move(btnAplicar);
    addAndMakeVisible(*btnAplicarColecaoLote_);

    auto btnOrigMed = std::make_unique<PillButton>(i18n::t("intake.btn_origem_lote"));
    btnOrigMed->tamanhoFonte = 12.5f;
    btnOrigMed->corTextoCustom = tk.textoPrimario;
    btnOrigMed->corBordaCustom = tk.borda;
    btnOrigMed->onClick = [this] {
        if (btnOriginalMediumLote_) mostrarEditorOriginalSourceMediumLote(btnOriginalMediumLote_->getScreenBounds());
    };
    btnOriginalMediumLote_ = std::move(btnOrigMed);
    addAndMakeVisible(*btnOriginalMediumLote_);

    auto btnGeo = std::make_unique<PillButton>(i18n::t("intake.btn_geo_lote"));
    btnGeo->tamanhoFonte = 12.5f;
    btnGeo->corTextoCustom = tk.textoPrimario;
    btnGeo->corBordaCustom = tk.borda;
    btnGeo->onClick = [this] {
        if (btnGeolocationLote_) mostrarEditorGeolocationLote(btnGeolocationLote_->getScreenBounds());
    };
    btnGeolocationLote_ = std::move(btnGeo);
    addAndMakeVisible(*btnGeolocationLote_);

    // Cluster C (Right)
    auto btnConfSel = std::make_unique<PillButton>(i18n::t("intake.btn_enviar_selecionados"));
    btnConfSel->tamanhoFonte = 12.5f;
    btnConfSel->corTextoCustom = juce::Colour(0xff22c55e);
    btnConfSel->corBordaCustom = juce::Colour(0xff22c55e);
    btnConfSel->onClick = [this] { confirmarSelecaoParaGrid(); };
    btnConfSel->setTooltip("Send selected intake items to GRID");
    btnConfSel->setAlpha(0.4f);
    btnConfSel->setEnabled(false);
    btnConfirmarSelecao_ = std::move(btnConfSel);
    addAndMakeVisible(*btnConfirmarSelecao_);

    auto btnConfTodos = std::make_unique<PillButton>(i18n::t("intake.btn_enviar_todos"));
    btnConfTodos->tamanhoFonte = 12.5f;
    btnConfTodos->corTextoCustom = juce::Colour(0xff22c55e);
    btnConfTodos->corBordaCustom = juce::Colour(0xff22c55e);
    btnConfTodos->onClick = [this] { confirmarTodosParaGrid(); };
    btnConfTodos->setTooltip("Send all items in this batch to GRID");
    btnConfirmarTodos_ = std::move(btnConfTodos);
    addAndMakeVisible(*btnConfirmarTodos_);

    // Divisors for SelectionActionBar
    divisor1_ = std::make_unique<VerticalDividerComponent>();
    addAndMakeVisible(*divisor1_);

    divisor2_ = std::make_unique<VerticalDividerComponent>();
    addAndMakeVisible(*divisor2_);

    auto btnRemover = std::make_unique<PillButton>(i18n::t("intake.btn_rejeitar_selecionados"));
    btnRemover->tamanhoFonte = 12.5f;
    btnRemover->corTextoCustom = tk.perigo;
    btnRemover->corBordaCustom = tk.perigo;
    btnRemover->onClick = [this] { removerSelecionadosDoIntake(); };
    btnRemover->setTooltip("Remove selected items from intake");
    btnRemover->setAlpha(1.0f);
    btnRemover->setEnabled(true);
    btnRemoverSelecao_ = std::move(btnRemover);
    addAndMakeVisible(*btnRemoverSelecao_);

    // 4. Main Table List
    tabela_ = std::make_unique<juce::TableListBox>("IntakeTable", this);
    tabela_->setColour(juce::ListBox::backgroundColourId, tk.painel);
    tabela_->setColour(juce::ListBox::outlineColourId, tk.borda);
    tabela_->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, tk.painelAlt);
    tabela_->getHeader().setColour(juce::TableHeaderComponent::textColourId, tk.textoPrimario);
    tabela_->getHeader().setColour(juce::TableHeaderComponent::outlineColourId, tk.borda);
    tabela_->setHeaderHeight(28);
    tabela_->setRowHeight(32);
    tabela_->setMultipleSelectionEnabled(true);

    auto& hdr = tabela_->getHeader();
    hdr.addColumn("", kColSelect, 36, 36, 36, juce::TableHeaderComponent::notSortable);
    hdr.addColumn(i18n::t("intake.col_type"), kColType, 80, 70, 110, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_name"), kColName, 220, 140, 500, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_date"), kColDateCreated, 150, 110, 200, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_size"), kColSize, 80, 60, 120, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_path"), kColPath, 250, 120, 600, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_collection"), kColCollection, 150, 100, 240, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_source_media"), kColSourceMedia, 200, 130, 350, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_action"), kColAction, 80, 70, 100, juce::TableHeaderComponent::notSortable);
    addAndMakeVisible(*tabela_);

    gridComponent_ = std::make_unique<ThumbnailsGridComponent>(*this);
    gridViewport_ = std::make_unique<juce::Viewport>();
    gridViewport_->setViewedComponent(gridComponent_.get(), false);
    gridViewport_->setScrollBarsShown(true, false);
    gridViewport_->setVisible(false);
    addChildComponent(*gridViewport_);

    emptyState_ = std::make_unique<IntakeDragDropEmptyState>(*this);
    addChildComponent(*emptyState_);

    recarregar();
}

IntakeWorkspaceComponent::~IntakeWorkspaceComponent() = default;

void IntakeWorkspaceComponent::atualizarVisibilidadeEmptyState() {
    bool vazio = todosItens_.empty();
    if (emptyState_) {
        emptyState_->setVisible(vazio);
        if (vazio) emptyState_->toFront(false);
    }
    if (vazio) {
        if (tabela_) tabela_->setVisible(false);
        if (gridViewport_) gridViewport_->setVisible(false);
    } else {
        if (tabela_) tabela_->setVisible(modoVisao_ == ModoVisao::Lista);
        if (gridViewport_) {
            gridViewport_->setVisible(modoVisao_ == ModoVisao::Icones);
            if (gridComponent_) gridComponent_->recalcularLayout();
        }
    }
}

void IntakeWorkspaceComponent::carregarItens() {
    auto quarentena = projeto_.listarItensEmQuarentena();
    std::vector<ItemIntake> novosItens;
    novosItens.reserve(quarentena.size());

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

        it.collection = item.collectionType.has_value() ? juce::String::fromUTF8(item.collectionType->c_str()) : juce::String();
        it.sourceMedia = juce::String::fromUTF8(item.sourceMedia.c_str());
        it.offline = item.offline;
        it.selecionado = false;

        it.dataCriacao = juce::String::fromUTF8(item.dataCriacao.c_str());
        if (it.dataCriacao.isEmpty()) {
            it.dataCriacao = juce::String::fromUTF8(item.criadoEm.c_str());
        }

        it.caminhoOrigem = !item.caminhoAbsolutoOrigem.empty() ? juce::String::fromUTF8(item.caminhoAbsolutoOrigem.c_str())
                                                              : juce::String::fromUTF8(item.caminhoRelativoArquivo.c_str());

        novosItens.push_back(std::move(it));
    }

    todosItens_ = std::move(novosItens);

    if (ultimoSortColumnId_ > 0) {
        sortOrderChanged(ultimoSortColumnId_, sortAscendente_);
    } else {
        atualizarFiltragem();
    }
    atualizarContagens();
    atualizarVisibilidadeEmptyState();
}

void IntakeWorkspaceComponent::definirModoVisao(ModoVisao modo) {
    if (modoVisao_ != modo) {
        modoVisao_ = modo;
        const auto& tk = tema();
        if (btnVisaoLista_) {
            btnVisaoLista_->setAtivo(modoVisao_ == ModoVisao::Lista);
        }
        if (btnVisaoIcones_) {
            btnVisaoIcones_->setAtivo(modoVisao_ == ModoVisao::Icones);
        }
        atualizarVisibilidadeEmptyState();
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

    if (btnFiltroAll_) btnFiltroAll_->setButtonText(i18n::t("intake.filtro_all").replace("{n}", juce::String(todosItens_.size())));
    if (btnFiltroAudio_) btnFiltroAudio_->setButtonText(i18n::t("intake.filtro_audio").replace("{n}", juce::String(contagemAudio_)));
    if (btnFiltroVideo_) btnFiltroVideo_->setButtonText(i18n::t("intake.filtro_video").replace("{n}", juce::String(contagemVideo_)));
    if (btnFiltroImage_) btnFiltroImage_->setButtonText(i18n::t("intake.filtro_image").replace("{n}", juce::String(contagemImage_)));
    if (btnFiltroDoc_) btnFiltroDoc_->setButtonText(i18n::t("intake.filtro_doc").replace("{n}", juce::String(contagemDoc_)));
    if (btnFiltroOther_) btnFiltroOther_->setButtonText(i18n::t("intake.filtro_other").replace("{n}", juce::String(contagemOther_)));

    if (lblContadorTotal_) {
        lblContadorTotal_->setText(i18n::t("intake.ativos_contador").replace("{n}", juce::String(todosItens_.size())), juce::dontSendNotification);
    }

    if (lblSubtitulo_) {
        lblSubtitulo_->setText(i18n::t("intake.selecionados").replace("{n}", juce::String(totalSelecionados)), juce::dontSendNotification);
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
        g.fillAll(tk.acento.withAlpha(0.25f));
    } else {
        g.fillAll(rowNumber % 2 == 1 ? tk.painelAlt.withAlpha(0.35f) : tk.painel);
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
        g.setColour(corCat);
        g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawText(item.categoria.toUpperCase(), badgeArea, juce::Justification::centred, true);
    } else if (columnId == kColName) {
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        if (item.offline) {
            int offBadgeW = 58;
            auto offBadge = juce::Rectangle<int>(width - offBadgeW - 6, (height - 18) / 2, offBadgeW, 18);
            g.drawText(item.nomeArquivo, 6, 0, width - offBadgeW - 16, height, juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xffef4444));
            g.fillRoundedRectangle(offBadge.toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
            g.drawText("OFFLINE", offBadge, juce::Justification::centred);
        } else {
            g.drawText(item.nomeArquivo, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
    } else if (columnId == kColDateCreated) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(item.dataCriacao.isNotEmpty() ? item.dataCriacao : "-", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColSize) {
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(formatarBytes(item.tamanhoBytes), 4, 0, width - 8, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColPath) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(item.caminhoOrigem.isNotEmpty() ? item.caminhoOrigem : "-", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColCollection) {
        if (!item.collection.isEmpty()) {
            auto badgeArea = juce::Rectangle<int>(4, 5, width - 8, height - 10);
            g.setColour(tk.acento);
            g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
            g.setColour(tk.textoSobreAcento);
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
                g.setColour(juce::Colour(0xff0d9488));
                g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
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
            msg << "Device: " << juce::String::fromUTF8(info.recordingDevice.c_str()) << "\n";
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

    // Top Header background (44px)
    g.setColour(tk.painel);
    g.fillRect(0, 0, getWidth(), 44);

    // Left Sidebar background (230px wide, from y=44 to bottom)
    g.setColour(tk.painel);
    g.fillRect(0, 44, 230, getHeight() - 44);

    // Dividers
    g.setColour(tk.borda);
    g.fillRect(0, 43, getWidth(), 1);
    g.fillRect(229, 44, 1, getHeight() - 44);
}

void IntakeWorkspaceComponent::resized() {
    auto area = getLocalBounds();

    // Top Header Bar (44px high)
    auto topBar = area.removeFromTop(44).reduced(14, 6);
    lblTitulo_->setBounds(topBar.removeFromLeft(120));
    topBar.removeFromLeft(8);
    lblContadorTotal_->setBounds(topBar.removeFromLeft(160));
    topBar.removeFromLeft(12);
    if (btnVisaoLista_) btnVisaoLista_->setBounds(topBar.removeFromLeft(34));
    topBar.removeFromLeft(4);
    if (btnVisaoIcones_) btnVisaoIcones_->setBounds(topBar.removeFromLeft(34));

    btnIngerir_->setBounds(topBar.removeFromRight(180));
    if (btnGoogleDrive_) {
        topBar.removeFromRight(8);
        btnGoogleDrive_->setBounds(topBar.removeFromRight(36));
    }

    // Left Sidebar (230px wide)
    auto sidebar = area.removeFromLeft(230).reduced(10, 8);

    // 1. Category Filter Pills
    btnFiltroAll_->setBounds(sidebar.removeFromTop(24));
    sidebar.removeFromTop(4);
    btnFiltroAudio_->setBounds(sidebar.removeFromTop(24));
    sidebar.removeFromTop(4);
    btnFiltroVideo_->setBounds(sidebar.removeFromTop(24));
    sidebar.removeFromTop(4);
    btnFiltroImage_->setBounds(sidebar.removeFromTop(24));
    sidebar.removeFromTop(4);
    btnFiltroDoc_->setBounds(sidebar.removeFromTop(24));
    sidebar.removeFromTop(4);
    btnFiltroOther_->setBounds(sidebar.removeFromTop(24));
    sidebar.removeFromTop(10);

    // Divider line 1
    if (divisor1_) divisor1_->setBounds(sidebar.removeFromTop(1));
    sidebar.removeFromTop(10);

    // 2. Selection Cluster
    lblSubtitulo_->setBounds(sidebar.removeFromTop(18));
    sidebar.removeFromTop(4);
    {
        auto selRow = sidebar.removeFromTop(24);
        int half = (selRow.getWidth() - 6) / 2;
        btnSelecionarTodos_->setBounds(selRow.removeFromLeft(half));
        selRow.removeFromLeft(6);
        btnLimparSelecao_->setBounds(selRow);
    }
    sidebar.removeFromTop(10);

    // 3. Batch Assignment Cluster
    lblRotuloColecao_->setBounds(sidebar.removeFromTop(18));
    sidebar.removeFromTop(4);
    comboColecaoLote_->setBounds(sidebar.removeFromTop(26));
    sidebar.removeFromTop(4);
    btnAplicarColecaoLote_->setBounds(sidebar.removeFromTop(24));
    sidebar.removeFromTop(6);
    btnOriginalMediumLote_->setBounds(sidebar.removeFromTop(24));
    sidebar.removeFromTop(4);
    btnGeolocationLote_->setBounds(sidebar.removeFromTop(24));
    sidebar.removeFromTop(10);

    // Divider line 2
    if (divisor2_) divisor2_->setBounds(sidebar.removeFromTop(1));
    sidebar.removeFromTop(10);

    // 4. Batch Actions (Send to Grid / Reject)
    btnConfirmarSelecao_->setBounds(sidebar.removeFromTop(28));
    sidebar.removeFromTop(4);
    btnConfirmarTodos_->setBounds(sidebar.removeFromTop(28));
    sidebar.removeFromTop(4);
    btnRemoverSelecao_->setBounds(sidebar.removeFromTop(28));

    // Right Area fills the remaining workspace (x = 230 to width, y = 44 to height)
    if (tabela_) {
        tabela_->setBounds(area);
    }
    if (gridViewport_) {
        gridViewport_->setBounds(area);
        if (gridComponent_) gridComponent_->recalcularLayout();
    }
    if (emptyState_) {
        emptyState_->setBounds(area);
    }
}

void IntakeWorkspaceComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    if (lblTitulo_) {
        lblTitulo_->setText(i18n::t("intake.titulo"), juce::dontSendNotification);
    }
    if (lblSubtitulo_) {
        lblSubtitulo_->setFont(juce::Font(juce::FontOptions(13.0f)));
        lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (lblRotuloColecao_) {
        lblRotuloColecao_->setText(i18n::t("intake.rotulo_conteudo"), juce::dontSendNotification);
        lblRotuloColecao_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        lblRotuloColecao_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (btnVisaoLista_) {
        btnVisaoLista_->setAtivo(modoVisao_ == ModoVisao::Lista);
        btnVisaoLista_->setTooltip(i18n::t("intake.tooltip_lista"));
    }
    if (btnVisaoIcones_) {
        btnVisaoIcones_->setAtivo(modoVisao_ == ModoVisao::Icones);
        btnVisaoIcones_->setTooltip(i18n::t("intake.tooltip_icones"));
    }
    if (btnIngerir_) {
        btnIngerir_->setButtonText(i18n::t("intake.btn_ingerir"));
        btnIngerir_->setTooltip(i18n::t("intake.tooltip_ingerir"));
    }
    if (comboColecaoLote_) {
        comboColecaoLote_->setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
        comboColecaoLote_->setColour(juce::ComboBox::textColourId, tk.textoPrimario);
        comboColecaoLote_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboColecaoLote_->setColour(juce::ComboBox::arrowColourId, tk.textoPrimario);
    }
    if (btnSelecionarTodos_) {
        btnSelecionarTodos_->setButtonText(i18n::t("intake.selecionar_todos"));
        btnSelecionarTodos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnSelecionarTodos_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (btnLimparSelecao_) {
        btnLimparSelecao_->setButtonText(i18n::t("intake.limpar_selecao"));
        btnLimparSelecao_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnLimparSelecao_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
    }
    if (btnAplicarColecaoLote_) {
        btnAplicarColecaoLote_->setButtonText(i18n::t("intake.btn_aplicar"));
        btnAplicarColecaoLote_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnAplicarColecaoLote_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (btnOriginalMediumLote_) {
        btnOriginalMediumLote_->setButtonText(i18n::t("intake.btn_origem_lote"));
        btnOriginalMediumLote_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnOriginalMediumLote_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (btnGeolocationLote_) {
        btnGeolocationLote_->setButtonText(i18n::t("intake.btn_geo_lote"));
        btnGeolocationLote_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnGeolocationLote_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (btnConfirmarSelecao_) {
        btnConfirmarSelecao_->setButtonText(i18n::t("intake.btn_enviar_selecionados"));
    }
    if (btnConfirmarTodos_) {
        btnConfirmarTodos_->setButtonText(i18n::t("intake.btn_enviar_todos"));
    }
    if (btnRemoverSelecao_) {
        btnRemoverSelecao_->setButtonText(i18n::t("intake.btn_rejeitar_selecionados"));
        btnRemoverSelecao_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnRemoverSelecao_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    }
    if (tabela_) {
        auto& hdr = tabela_->getHeader();
        hdr.setColumnName(kColType, i18n::t("intake.col_type"));
        hdr.setColumnName(kColName, i18n::t("intake.col_name"));
        hdr.setColumnName(kColDateCreated, i18n::t("intake.col_date"));
        hdr.setColumnName(kColSize, i18n::t("intake.col_size"));
        hdr.setColumnName(kColPath, i18n::t("intake.col_path"));
        hdr.setColumnName(kColCollection, i18n::t("intake.col_collection"));
        hdr.setColumnName(kColSourceMedia, i18n::t("intake.col_source_media"));
        hdr.setColumnName(kColAction, i18n::t("intake.col_action"));
        tabela_->setColour(juce::ListBox::backgroundColourId, tk.painel);
        tabela_->setColour(juce::ListBox::outlineColourId, tk.borda);
        tabela_->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, tk.painelAlt);
        tabela_->getHeader().setColour(juce::TableHeaderComponent::textColourId, tk.textoPrimario);
        tabela_->getHeader().setColour(juce::TableHeaderComponent::outlineColourId, tk.borda);
        tabela_->repaint();
    }
    if (gridComponent_) {
        gridComponent_->repaint();
    }
    if (emptyState_) {
        emptyState_->repaint();
    }
    atualizarContagens();
    repaint();
}

} // namespace matriz::ui
