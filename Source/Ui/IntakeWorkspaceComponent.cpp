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
#include "HierarquiaEditorComponent.h"
#include "../Analytics/AssetGeolocation.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Diag/Watchdog.h"
#include "../I18n/Strings.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace matriz::ui {

namespace {

enum ColumnId {
    kColSelect = 1,
    kColType = 2,
    kColName = 3,
    kColOrigin = 10,
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

inline const std::vector<std::pair<juce::String, juce::String>>& mapaTraducoesContent() {
    static const std::vector<std::pair<juce::String, juce::String>> mapa = {
        // Audio
        {"Album", juce::String::fromUTF8("Álbum")},
        {"EP", "EP"},
        {"Single", "Single"},
        {"Compilation", juce::String::fromUTF8("Coletânea")},
        {"Soundtrack", "Trilha Sonora"},
        {"Stems", "Stems / Pistas Separadas"},
        {"Multitracks", "Multitracks"},
        {"Sample Pack", "Pacote de Samples"},
        {"Sample", "Sample"},
        {"Preset", "Preset / Predefinição"},
        {"DAW Session", juce::String::fromUTF8("Sessão de DAW")},
        {"Field Recording", juce::String::fromUTF8("Gravação de Campo")},
        {"Sound FX", "Efeitos Sonoros (SFX)"},
        {"MIDI", "Arquivo MIDI"},
        {"Artist Catalog", juce::String::fromUTF8("Catálogo de Artista")},
        {"Artist Backup", "Backup do Artista"},
        // Video
        {"Raw Footage", juce::String::fromUTF8("Gravação Bruta")},
        {"Home Video", juce::String::fromUTF8("Vídeo Caseiro")},
        {"Music Video", "Videoclipe"},
        {"Film", "Filme / Curta"},
        {"Documentary", juce::String::fromUTF8("Documentário")},
        {"Corporate Video", juce::String::fromUTF8("Vídeo Institucional")},
        {"Commercial", "Comercial / Publicidade"},
        {"Live Performance", "Show / Ao Vivo"},
        {"NLE Project", juce::String::fromUTF8("Projeto de Edição (NLE)")},
        {"Social Media Video", juce::String::fromUTF8("Vídeos para Redes Sociais")},
        {"WhatsApp Video", juce::String::fromUTF8("Vídeo do WhatsApp")},
        {"TV Video", juce::String::fromUTF8("Vídeo de TV")},
        {"YouTube Video", juce::String::fromUTF8("Vídeo do YouTube")},
        {"360 Video", juce::String::fromUTF8("Vídeo 360°")},
        {"Making Of", juce::String::fromUTF8("Making Of")},
        // Image
        {"Photo", "Foto"},
        {"Artwork", juce::String::fromUTF8("Arte / Ilustração")},
        {"Album Cover", juce::String::fromUTF8("Capa de Álbum")},
        {"Poster", juce::String::fromUTF8("Pôster / Cartaz")},
        {"Press / Promotional", juce::String::fromUTF8("Material de Divulgação")},
        {"Image Edit Project", "Projeto de Imagem"},
        {"Graphics", juce::String::fromUTF8("Gráficos / Design")},
        {"Logo", "Logotipo"},
        {"3D", juce::String::fromUTF8("Modelagem 3D")},
        // Docs
        {"Documentation", juce::String::fromUTF8("Documentação")},
        {"Book", "Livro"},
        {"Contract", "Contrato"},
        {"Manual", "Manual"},
        {"Report", juce::String::fromUTF8("Relatório")},
        {"Reference", juce::String::fromUTF8("Referência / Pesquisa")},
        {"Technical Documentation", juce::String::fromUTF8("Documentação Técnica")},
        {"Spreadsheet", "Planilha"},
        {"Planilha", "Planilha"}
    };
    return mapa;
}

inline juce::String traduzirContent(const juce::String& val, bool paraPt) {
    if (val.isEmpty()) return {};
    for (const auto& par : mapaTraducoesContent()) {
        if (paraPt) {
            if (val.equalsIgnoreCase(par.first)) return par.second;
        } else {
            if (val.equalsIgnoreCase(par.second)) return par.first;
        }
    }
    return val;
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

class LightroomIconButton : public juce::Button {
public:
    LightroomIconButton() : juce::Button("Lightroom") {
        img_ = juce::ImageFileFormat::loadFrom(AssetsBinaryData::lrlogo_png, AssetsBinaryData::lrlogo_pngSize);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat();
        
        g.setColour(shouldDrawButtonAsDown ? tk.painelAlt.darker(0.1f)
                    : (shouldDrawButtonAsHighlighted ? tk.painelAlt.brighter(0.15f) : tk.painelAlt));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(shouldDrawButtonAsHighlighted ? juce::Colour(0xff31a8ff) : tk.borda);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);

        if (img_.isValid()) {
            auto iconArea = r.reduced(4.0f);
            g.drawImageWithin(img_, (int)iconArea.getX(), (int)iconArea.getY(),
                              (int)iconArea.getWidth(), (int)iconArea.getHeight(),
                              juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, false);
        } else {
            g.setColour(juce::Colour(0xff31a8ff));
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText("Lr", getLocalBounds(), juce::Justification::centred);
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
    juce::Colour corFundoCustom = juce::Colours::transparentBlack;
    float tamanhoFonte = 13.0f;
    bool ativo = false;

    PillButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        const auto& tk = tema();
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        float cornerRadius = 4.0f;

        // Background
        if (corFundoCustom.isOpaque()) {
            juce::Colour bg = isEnabled() ? corFundoCustom : corFundoCustom.withAlpha(0.35f);
            if (shouldDrawButtonAsDown) bg = bg.darker(0.15f);
            else if (shouldDrawButtonAsHighlighted) bg = bg.brighter(0.15f);
            g.setColour(bg);
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else if (ativo) {
            g.setColour(tk.acento.withAlpha(0.20f));
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else if (shouldDrawButtonAsDown) {
            g.setColour(tk.painelAlt.darker(0.15f));
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else if (shouldDrawButtonAsHighlighted) {
            g.setColour(tk.painelAlt.brighter(0.12f));
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else {
            g.setColour(tk.painelAlt);
            g.fillRoundedRectangle(bounds, cornerRadius);
        }

        // Outline (Fine border)
        juce::Colour borderCol = corBordaCustom.isOpaque() ? corBordaCustom 
                               : (ativo ? tk.acento : tk.borda);
        if (!isEnabled()) {
            borderCol = borderCol.withAlpha(0.40f);
        }
        g.setColour(borderCol);
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

        // Content (Dot + Text)
        auto contentBounds = getLocalBounds().reduced(6, 0);
        if (!corPonto.isTransparent()) {
            float dotSize = 7.0f;
            float dotX = static_cast<float>(contentBounds.getX() + 2);
            float dotY = static_cast<float>(contentBounds.getCentreY()) - dotSize * 0.5f;
            g.setColour(isEnabled() ? corPonto : corPonto.withAlpha(0.40f));
            g.fillEllipse(dotX, dotY, dotSize, dotSize);
            contentBounds.removeFromLeft(static_cast<int>(dotSize + 6.0f));
        }

        juce::Colour textCol = corTextoCustom.isOpaque() ? corTextoCustom
                             : (ativo ? tk.textoPrimario : (isEnabled() ? tk.textoPrimario : tk.textoTerciario));
        g.setColour(textCol);
        bool ehNegativo = corFundoCustom.isOpaque();
        g.setFont(juce::Font(juce::FontOptions(tamanhoFonte, (ativo || isMouseOver() || ehNegativo) ? juce::Font::bold : juce::Font::plain)));
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
    GeoLocationPopupContent(matriz::db::Database& registro, bool isBatch,
                             std::function<void(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&)> onApply)
        : registro_(registro), onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", isBatch ? i18n::t("intake.popup_geo_batch") : i18n::t("intake.popup_geo"));
        lblTitle_->setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitle_);

        // Favoritos (item 3 da correção "BACKUP e INTAKE"): mesmo
        // GeoFavoritosRepository que a ficha usa — sem lista paralela.
        btnFavoritos_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x96\xbe Favorites"));
        btnFavoritos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnFavoritos_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnFavoritos_->onClick = [this] { mostrarMenuFavoritos(); };
        addAndMakeVisible(*btnFavoritos_);

        auto makeField = [this, &tk](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed,
                                     const juce::String& labelText, const juce::String& placeholder) {
            lbl = std::make_unique<juce::Label>("", labelText);
            lbl->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            lbl->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lbl);

            ed = std::make_unique<juce::TextEditor>();
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
            ed->setTextToShowWhenEmpty(placeholder, juce::Colours::grey);
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

        setSize(380, 400);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        auto topRow = area.removeFromTop(24);
        lblTitle_->setBounds(topRow.removeFromLeft(topRow.getWidth() - 110));
        btnFavoritos_->setBounds(topRow);
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
    void mostrarMenuFavoritos() {
        auto favs = matriz::analytics::GeoFavoritosRepository::listar(registro_);
        if (favs.empty()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon, "Favorites",
                "No favorite places saved yet. Save one from the ficha's GEO LOCATION section first.");
            return;
        }
        juce::PopupMenu menu;
        for (int i = 0; i < static_cast<int>(favs.size()); ++i) {
            juce::String label = juce::String(favs[static_cast<size_t>(i)].nome);
            if (favs[static_cast<size_t>(i)].city)
                label += juce::String::fromUTF8(" \xe2\x80\x93 ") + juce::String(*favs[static_cast<size_t>(i)].city);
            menu.addItem(i + 1, label);
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(btnFavoritos_.get()),
            [this, favs](int result) {
                if (result < 1 || result > static_cast<int>(favs.size())) return;
                const auto& fav = favs[static_cast<size_t>(result - 1)];
                if (edCoords_ && fav.latitude && fav.longitude) {
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(6) << *fav.latitude << ", " << *fav.longitude;
                    edCoords_->setText(ss.str());
                }
                if (edAddress_ && fav.formattedAddress) edAddress_->setText(juce::String(*fav.formattedAddress));
                if (edCity_ && fav.city) edCity_->setText(juce::String(*fav.city));
                if (edState_ && fav.stateProvince) edState_->setText(juce::String(*fav.stateProvince));
                if (edCountry_ && fav.country) edCountry_->setText(juce::String(*fav.country));
            });
    }

    matriz::db::Database& registro_;
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<juce::TextButton> btnFavoritos_;
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

// Campo de texto com autocomplete (item 4 da correção "BACKUP e INTAKE"):
// mostra os valores já usados nessa mesma coluna em qualquer lugar do
// projeto (CREATOR/SUBJECT), filtra por prefixo conforme digita, sempre em
// ordem alfabética. Não cria uma base de valores paralela — os valores vêm
// prontos de fora (query direta na tabela item, ver
// IntakeWorkspaceComponent::valoresExistentesParaColuna).
class AutocompleteAssistedField : public juce::Component, private juce::ListBoxModel {
public:
    AutocompleteAssistedField(std::vector<juce::String> valoresExistentes, const juce::String& placeholder) {
        valores_ = std::move(valoresExistentes);
        std::sort(valores_.begin(), valores_.end(),
                  [](const juce::String& a, const juce::String& b) { return a.compareIgnoreCase(b) < 0; });

        const auto& tk = tema();
        editor_ = std::make_unique<FocusAwareTextEditor>();
        editor_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        editor_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        editor_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        editor_->setColour(juce::TextEditor::outlineColourId, tk.borda);
        editor_->setTextToShowWhenEmpty(placeholder, juce::Colours::grey);
        editor_->onTextChange = [this] { atualizarSugestoes(); };
        editor_->onFocus = [this] { mostrarTodasSugestoes(); };
        editor_->onFocusLost = [this] {
            juce::Component::SafePointer<AutocompleteAssistedField> safe(this);
            juce::Timer::callAfterDelay(150, [safe] { if (safe && safe->lista_) safe->lista_->setVisible(false); });
        };
        addAndMakeVisible(*editor_);

        lista_ = std::make_unique<juce::ListBox>();
        lista_->setModel(this);
        lista_->setRowHeight(22);
        lista_->setColour(juce::ListBox::backgroundColourId, juce::Colours::white);
        lista_->setColour(juce::ListBox::outlineColourId, tk.borda);
        lista_->setVisible(false);
        addAndMakeVisible(*lista_);
        lista_->toFront(false);
    }

    juce::String getText() const { return editor_->getText().trim(); }
    void setText(const juce::String& t) { editor_->setText(t, false); }

    void resized() override {
        auto area = getLocalBounds();
        editor_->setBounds(area.removeFromTop(26));
        area.removeFromTop(2);
        int rows = juce::jmin(5, static_cast<int>(sugestoes_.size()));
        lista_->setBounds(area.removeFromTop(rows * 22));
    }

private:
    int getNumRows() override { return static_cast<int>(sugestoes_.size()); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(sugestoes_.size())) return;
        g.fillAll(rowIsSelected ? tema().acento.withAlpha(0.25f) : juce::Colours::white);
        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(sugestoes_[static_cast<size_t>(rowNumber)], 6, 0, width - 12, height, juce::Justification::centredLeft);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override {
        if (row < 0 || row >= static_cast<int>(sugestoes_.size())) return;
        editor_->setText(sugestoes_[static_cast<size_t>(row)], false);
        lista_->setVisible(false);
    }

    void mostrarTodasSugestoes() {
        sugestoes_ = valores_;
        lista_->updateContent();
        lista_->setVisible(!sugestoes_.empty());
        resized();
        if (auto* parent = getParentComponent()) parent->resized();
    }

    void atualizarSugestoes() {
        juce::String q = editor_->getText().trim();
        if (q.isEmpty()) { mostrarTodasSugestoes(); return; }
        sugestoes_.clear();
        for (auto& v : valores_)
            if (v.startsWithIgnoreCase(q)) sugestoes_.push_back(v);
        lista_->updateContent();
        lista_->setVisible(!sugestoes_.empty());
        resized();
        if (auto* parent = getParentComponent()) parent->resized();
    }

    class FocusAwareTextEditor : public juce::TextEditor {
    public:
        std::function<void()> onFocus;
        void focusGained(FocusChangeType) override { if (onFocus) onFocus(); }
    };

    std::vector<juce::String> valores_;
    std::vector<juce::String> sugestoes_;
    std::unique_ptr<FocusAwareTextEditor> editor_;
    std::unique_ptr<juce::ListBox> lista_;
};

class AutocompleteLotePopupContent : public juce::Component {
public:
    AutocompleteLotePopupContent(const juce::String& title, const juce::String& placeholder,
                                  std::vector<juce::String> valoresExistentes, juce::Colour corDestaque,
                                  std::function<void(const juce::String&)> onApply)
        : onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", title);
        lblTitle_->setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitle_);

        campo_ = std::make_unique<AutocompleteAssistedField>(std::move(valoresExistentes), placeholder);
        addAndMakeVisible(*campo_);

        btnApply_ = std::make_unique<PillButton>(i18n::t("intake.btn_aplicar_selecionados"));
        btnApply_->corTextoCustom = corDestaque;
        btnApply_->corBordaCustom = corDestaque;
        btnApply_->tamanhoFonte = 13.0f;
        btnApply_->onClick = [this] {
            if (onApply_) onApply_(campo_->getText());
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
        };
        addAndMakeVisible(*btnApply_);

        setSize(340, 240);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        lblTitle_->setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
        campo_->setBounds(area.removeFromTop(140));
        area.removeFromTop(8);
        btnApply_->setBounds(area.removeFromBottom(30).removeFromRight(170));
    }

private:
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<AutocompleteAssistedField> campo_;
    std::unique_ptr<PillButton> btnApply_;
    std::function<void(const juce::String&)> onApply_;
};

class ContentLotePopupContent : public juce::Component {
public:
    ContentLotePopupContent(juce::Colour corDestaque, std::function<void(const juce::String&)> onApply)
        : onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", "Set CONTENT for Selected");
        lblTitle_->setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitle_);

        combo_ = std::make_unique<juce::ComboBox>();
        combo_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        combo_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        combo_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        combo_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
        IntakeWorkspaceComponent::popularComboColecoes(*combo_, true);
        addAndMakeVisible(*combo_);

        btnApply_ = std::make_unique<PillButton>(i18n::t("intake.btn_aplicar_selecionados"));
        btnApply_->corTextoCustom = corDestaque;
        btnApply_->corBordaCustom = corDestaque;
        btnApply_->tamanhoFonte = 13.0f;
        btnApply_->onClick = [this] {
            juce::String chosen = combo_->getText();
            if (onApply_) {
                if (combo_->getSelectedId() == 1 || chosen.equalsIgnoreCase("None")) onApply_("");
                else onApply_(chosen);
            }
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
        };
        addAndMakeVisible(*btnApply_);

        setSize(320, 150);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        lblTitle_->setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
        combo_->setBounds(area.removeFromTop(28));
        area.removeFromTop(10);
        btnApply_->setBounds(area.removeFromBottom(30).removeFromRight(170));
    }

private:
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<juce::ComboBox> combo_;
    std::unique_ptr<PillButton> btnApply_;
    std::function<void(const juce::String&)> onApply_;
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

            // Top-Left Type / Category Badge (adjacent to checkbox)
            juce::String catBadgeText = item.categoria.toUpperCase();
            int catBadgeW = juce::jmax(36, static_cast<int>(catBadgeText.length()) * 7 + 8);
            juce::Rectangle<int> catBadgeRect(thumbArea.getX() + 28, thumbArea.getY() + 6, catBadgeW, 18);
            g.setColour(corCat.withAlpha(0.85f));
            g.fillRoundedRectangle(catBadgeRect.toFloat(), 4.0f);
            g.setColour(juce::Colours::white);
            g.setFont(font9Bold);
            g.drawText(catBadgeText, catBadgeRect, juce::Justification::centred);

            // Top-Right Rescan Origin Badge (Section 5.2)
            if (item.rescanOrigem == RescanOrigem::Novo) {
                int rescanBadgeW = 46;
                juce::Rectangle<int> rescanBadgeRect(thumbArea.getRight() - rescanBadgeW - 6, thumbArea.getY() + 6, rescanBadgeW, 18);
                g.setColour(juce::Colour(0xff10b981)); // Green pill
                g.fillRoundedRectangle(rescanBadgeRect.toFloat(), 4.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font9Bold);
                g.drawText("NOVO", rescanBadgeRect, juce::Justification::centred);
            } else if (item.rescanOrigem == RescanOrigem::Modificado) {
                int rescanBadgeW = 76;
                juce::Rectangle<int> rescanBadgeRect(thumbArea.getRight() - rescanBadgeW - 6, thumbArea.getY() + 6, rescanBadgeW, 18);
                g.setColour(juce::Colour(0xfff59e0b)); // Amber pill
                g.fillRoundedRectangle(rescanBadgeRect.toFloat(), 4.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font9Bold);
                g.drawText("MODIFICADO", rescanBadgeRect, juce::Justification::centred);
            }

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

        // Marquee selection overlay
        if (lacoAtivo_ && !lacoAtual_.isEmpty()) {
            g.setColour(tk.acento.withAlpha(0.18f));
            g.fillRect(lacoAtual_);
            g.setColour(tk.acento);
            g.drawRect(lacoAtual_, 1);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int idx = indiceNaPosicao(e.getPosition());
        matriz::diag::WatchdogLogger::getInstance().log(
            "[IntakeGrid] mouseDown pos=" + e.getPosition().toString() + " idx=" + juce::String(idx) +
            " colunas_=" + juce::String(colunas_) + " cardW_=" + juce::String(cardW_) + " cardH_=" + juce::String(cardH_));

        if (e.mods.isPopupMenu()) {
            if (idx >= 0 && idx < static_cast<int>(owner_.indicesFiltrados_.size())) {
                int realIdx = owner_.indicesFiltrados_[static_cast<size_t>(idx)];
                if (realIdx >= 0 && realIdx < static_cast<int>(owner_.todosItens_.size())) {
                    owner_.mostrarMenuContexto(realIdx, e.getScreenPosition());
                }
            }
            return;
        }

        if (e.getNumberOfClicks() >= 2) {
            if (idx >= 0 && idx < static_cast<int>(owner_.indicesFiltrados_.size())) {
                owner_.abrirArquivoOrigem(idx);
            }
            return;
        }

        lacoInicio_ = e.getPosition();
        lacoAtual_ = {};
        lacoAtivo_ = false;
        clickedCardIdx_ = idx;

        selecaoAntesDoLaco_.resize(owner_.todosItens_.size());
        for (size_t k = 0; k < owner_.todosItens_.size(); ++k) {
            selecaoAntesDoLaco_[k] = owner_.todosItens_[k].selecionado;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        static int contadorLog = 0;
        if (++contadorLog % 5 == 1) { // não loga TODO evento de drag (spam) — só 1 em 5
            matriz::diag::WatchdogLogger::getInstance().log(
                "[IntakeGrid] mouseDrag pos=" + e.getPosition().toString() +
                " dist=" + juce::String(e.getDistanceFromDragStart()) + " lacoAtivo_=" + juce::String((int)lacoAtivo_));
        }
        if (e.getDistanceFromDragStart() >= 5) {
            lacoAtivo_ = true;
            lacoAtual_ = juce::Rectangle<int>(lacoInicio_, e.getPosition());

            bool invertendo = e.mods.isCommandDown() || e.mods.isCtrlDown();
            bool acumulando = e.mods.isShiftDown();

            int total = static_cast<int>(owner_.indicesFiltrados_.size());
            for (int i = 0; i < total; ++i) {
                int realIdx = owner_.indicesFiltrados_[static_cast<size_t>(i)];
                if (realIdx < 0 || realIdx >= static_cast<int>(owner_.todosItens_.size())) continue;

                bool touched = boundsDoCard(i).intersects(lacoAtual_);
                bool original = (realIdx < static_cast<int>(selecaoAntesDoLaco_.size())) ? selecaoAntesDoLaco_[static_cast<size_t>(realIdx)] : false;

                if (touched) {
                    if (invertendo) {
                        owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = !original;
                    } else {
                        owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = true;
                    }
                } else {
                    if (acumulando || invertendo) {
                        owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = original;
                    } else {
                        owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = original;
                    }
                }
            }
            owner_.atualizarContagens();
            if (owner_.tabela_) owner_.tabela_->repaint();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (lacoAtivo_) {
            lacoAtivo_ = false;
            lacoAtual_ = {};
            selecaoAntesDoLaco_.clear();
            owner_.atualizarContagens();
            if (owner_.tabela_) owner_.tabela_->repaint();
            repaint();
            return;
        }

        // Single click (not dragged)
        if (clickedCardIdx_ >= 0 && clickedCardIdx_ < static_cast<int>(owner_.indicesFiltrados_.size())) {
            int realIdx = owner_.indicesFiltrados_[static_cast<size_t>(clickedCardIdx_)];
            if (realIdx >= 0 && realIdx < static_cast<int>(owner_.todosItens_.size())) {
                // item (shift-clique seleciona intervalo): mesma âncora
                // compartilhada com a visão em lista (cellClicked).
                if (e.mods.isShiftDown() && owner_.ultimaPosicaoClicadaParaSelecao_ >= 0 &&
                    owner_.ultimaPosicaoClicadaParaSelecao_ < static_cast<int>(owner_.indicesFiltrados_.size())) {
                    int de = std::min(owner_.ultimaPosicaoClicadaParaSelecao_, clickedCardIdx_);
                    int ate = std::max(owner_.ultimaPosicaoClicadaParaSelecao_, clickedCardIdx_);
                    for (int p = de; p <= ate; ++p) {
                        int ri = owner_.indicesFiltrados_[static_cast<size_t>(p)];
                        if (ri >= 0 && ri < static_cast<int>(owner_.todosItens_.size()))
                            owner_.todosItens_[static_cast<size_t>(ri)].selecionado = true;
                    }
                } else {
                    owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = !owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado;
                    owner_.ultimaPosicaoClicadaParaSelecao_ = clickedCardIdx_;
                }
                owner_.atualizarContagens();
                if (owner_.tabela_) owner_.tabela_->repaint();
                repaint();
            }
        }
        clickedCardIdx_ = -1;
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

    bool lacoAtivo_ = false;
    juce::Point<int> lacoInicio_;
    juce::Rectangle<int> lacoAtual_;
    std::vector<bool> selecaoAntesDoLaco_;
    int clickedCardIdx_ = -1;
};

const std::vector<IntakeWorkspaceComponent::CategoriaColecao>& IntakeWorkspaceComponent::vocabularioColecoes() {
    static const std::vector<CategoriaColecao> kVocab = {
        { "AUDIO", {
            "Album", "EP", "Single", "Compilation", "Soundtrack",
            "Stems", "Multitracks", "Sample Pack", "Sample", "Preset", "DAW Session",
            "Field Recording", "Sound FX", "MIDI",
            "Artist Catalog", "Artist Backup"
        }},
        { "VIDEO", {
            "Raw Footage", "Home Video", "Music Video", "Film",
            "Documentary", "Corporate Video", "Commercial", "Live Performance",
            "NLE Project", "Social Media Video",
            "WhatsApp Video", "TV Video", "YouTube Video", "360 Video", "Making Of"
        }},
        { "IMAGE", {
            "Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional",
            "Image Edit Project", "Graphics", "Logo", "3D"
        }},
        { "DOCUMENT", {
            "Documentation", "Book", "Contract", "Manual", "Report",
            "Reference", "Technical Documentation", "Planilha"
        }}
    };
    return kVocab;
}

void IntakeWorkspaceComponent::popularComboColecoes(juce::ComboBox& combo, bool incluirNone) {
    combo.clear(juce::dontSendNotification);
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    int id = 1;
    if (incluirNone) {
        combo.addItem(i18n::t("intake.nenhuma_colecao"), id++);
        combo.addSeparator();
    }
    for (const auto& cat : vocabularioColecoes()) {
        juce::String grupoNome = cat.grupo;
        if (isPt) {
            if (grupoNome == "AUDIO") grupoNome = juce::String::fromUTF8("ÁUDIO");
            else if (grupoNome == "VIDEO") grupoNome = juce::String::fromUTF8("VÍDEO");
            else if (grupoNome == "IMAGE") grupoNome = juce::String::fromUTF8("IMAGEM");
            else if (grupoNome == "DOCUMENT") grupoNome = juce::String::fromUTF8("DOCUMENTO");
        }
        combo.addSectionHeading(grupoNome);
        for (const auto& item : cat.itens) {
            combo.addItem(isPt ? traduzirContent(item, true) : item, id++);
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

    // Ícone GEO LOCATION — o PNG vem com fundo branco chapado; aqui ele vira
    // transparente para o ícone assentar sobre o fundo do card.
    iconeGeo_ = juce::ImageFileFormat::loadFrom(AssetsBinaryData::geo_png, AssetsBinaryData::geo_pngSize);
    if (iconeGeo_.isValid()) {
        iconeGeo_ = iconeGeo_.convertedToFormat(juce::Image::ARGB);
        juce::Image::BitmapData bmp(iconeGeo_, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < bmp.height; ++y) {
            for (int x = 0; x < bmp.width; ++x) {
                auto cor = bmp.getPixelColour(x, y);
                if (cor.getRed() >= 240 && cor.getGreen() >= 240 && cor.getBlue() >= 240)
                    bmp.setPixelColour(x, y, juce::Colours::transparentBlack);
            }
        }
    }

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

    auto btnLR = std::make_unique<LightroomIconButton>();
    btnLR->setTooltip(i18n::t("intake.importar_lightroom"));
    btnLR->onClick = [this] {
        if (aoIngerirDeLightroom) {
            aoIngerirDeLightroom();
        }
    };
    btnLightroom_ = std::move(btnLR);
    addAndMakeVisible(*btnLightroom_);

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    // CLOSE PROJECT / RETURN TO CATALOG saíram da barra desta aba (e de todas
    // as outras): ambos vivem agora só no menu File.

    btnAjuda_ = std::make_unique<juce::TextButton>("?");
    btnAjuda_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnAjuda_->setColour(juce::TextButton::textColourOffId, tk.acento);
    btnAjuda_->setColour(juce::TextButton::textColourOnId, tk.acento);
    btnAjuda_->setTooltip(isPt ? juce::String::fromUTF8("Gerenciar arquivos recém-ingeridos aguardando verificação para METADADOS") : "Manage recently ingested files awaiting verification to METADATA");
    btnAjuda_->onClick = [this] { if (aoPedirAjuda) aoPedirAjuda(); };
    addAndMakeVisible(*btnAjuda_);

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
    btnSelTodos->onClick = [this] { selecionarTodos(true); };
    btnSelecionarTodos_ = std::move(btnSelTodos);
    addAndMakeVisible(*btnSelecionarTodos_);

    auto btnLimpar = std::make_unique<PillButton>(i18n::t("intake.limpar_selecao"));
    btnLimpar->tamanhoFonte = 12.5f;
    btnLimpar->onClick = [this] { selecionarTodos(false); };
    btnLimparSelecao_ = std::move(btnLimpar);
    addAndMakeVisible(*btnLimparSelecao_);

    // Cluster B (Center) — Batch Assignment: um botão colorido por campo,
    // mesma cor do bloco correspondente no Visual Editor da aba BACKUP
    // (item 2 da correção "BACKUP e INTAKE").
    using NH = matriz::consolidacao::NivelHierarquia;
    auto criarBotaoLote = [this](const juce::String& texto, juce::Colour cor, std::function<void()> aoClicar) {
        auto btn = std::make_unique<PillButton>(texto);
        btn->tamanhoFonte = 12.5f;
        btn->corFundoCustom = cor;
        btn->corTextoCustom = juce::Colours::white;
        btn->corBordaCustom = cor.darker(0.2f);
        btn->onClick = std::move(aoClicar);
        return btn;
    };

    auto btnOrigMed = criarBotaoLote("SOURCE MEDIUM", corDoNivelHierarquia(NH::Origem), [this] {
        if (btnSourceMediumLote_) mostrarEditorOriginalSourceMediumLote(btnSourceMediumLote_->getScreenBounds());
    });
    btnSourceMediumLote_ = std::move(btnOrigMed);
    addAndMakeVisible(*btnSourceMediumLote_);

    auto btnCreator = criarBotaoLote("CREATOR", corDoNivelHierarquia(NH::Artista), [this] {
        if (btnCreatorLote_) mostrarEditorCreatorLote(btnCreatorLote_->getScreenBounds());
    });
    btnCreatorLote_ = std::move(btnCreator);
    addAndMakeVisible(*btnCreatorLote_);

    auto btnContent = criarBotaoLote("CONTENT", corDoNivelHierarquia(NH::ContentType), [this] {
        if (btnContentLote_) mostrarEditorContentLote(btnContentLote_->getScreenBounds());
    });
    btnContentLote_ = std::move(btnContent);
    addAndMakeVisible(*btnContentLote_);

    auto btnSubject = criarBotaoLote("SUBJECT", corDoNivelHierarquia(NH::Subject), [this] {
        if (btnSubjectLote_) mostrarEditorSubjectLote(btnSubjectLote_->getScreenBounds());
    });
    btnSubjectLote_ = std::move(btnSubject);
    addAndMakeVisible(*btnSubjectLote_);

    // Item 2 (nova lista) — o default (herdar o ano de DATE CREATED) já
    // existe em FichaPanelComponent; este botão só dá ao operador um jeito
    // rápido de sobrescrever em lote no INTAKE, antes de mandar pra GRID.
    // Cor fixa em roxo clarinho (item 2, lista nova de hoje) só neste
    // botão — corDoNivelHierarquia(Ano) é o mesmo verde de CONTENT
    // (ContentType) e ficava fácil de confundir um com o outro aqui; o
    // Visual Editor do BACKUP continua usando o verde original pro nível
    // Ano, não mexi nisso.
    auto btnEventDate = criarBotaoLote("EVENT DATE", juce::Colour(0xff9575cd), [this] {
        if (btnEventDateLote_) mostrarEditorEventDateLote(btnEventDateLote_->getScreenBounds());
    });
    btnEventDateLote_ = std::move(btnEventDate);
    addAndMakeVisible(*btnEventDateLote_);

    // GEO LOCATION vive numa seção própria (item 3), separada dos quatro
    // campos de metadata acima — cor neutra, não é um bloco do Visual Editor.
    auto btnGeo = std::make_unique<PillButton>(i18n::t("intake.btn_geo_lote"));
    btnGeo->tamanhoFonte = 12.5f;
    btnGeo->onClick = [this] {
        if (btnGeolocationLote_) mostrarEditorGeolocationLote(btnGeolocationLote_->getScreenBounds());
    };
    btnGeolocationLote_ = std::move(btnGeo);
    addAndMakeVisible(*btnGeolocationLote_);

    // Cluster C (Right)
    auto btnConfSel = std::make_unique<PillButton>(i18n::t("intake.btn_enviar_selecionados"));
    btnConfSel->tamanhoFonte = 12.0f;
    btnConfSel->corFundoCustom = juce::Colour(0xff16a34a);
    btnConfSel->corTextoCustom = juce::Colours::white;
    btnConfSel->corBordaCustom = juce::Colour(0xff15803d);
    btnConfSel->onClick = [this] { confirmarSelecaoParaGrid(); };
    btnConfSel->setTooltip("Send selected intake items to GRID");
    btnConfSel->setEnabled(false);
    btnConfirmarSelecao_ = std::move(btnConfSel);
    addAndMakeVisible(*btnConfirmarSelecao_);

    auto btnConfTodos = std::make_unique<PillButton>(i18n::t("intake.btn_enviar_todos"));
    btnConfTodos->tamanhoFonte = 12.0f;
    btnConfTodos->corFundoCustom = juce::Colour(0xff16a34a);
    btnConfTodos->corTextoCustom = juce::Colours::white;
    btnConfTodos->corBordaCustom = juce::Colour(0xff15803d);
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
    btnRemover->tamanhoFonte = 12.0f;
    btnRemover->corFundoCustom = tk.perigo;
    btnRemover->corTextoCustom = juce::Colours::white;
    btnRemover->corBordaCustom = tk.perigo.darker(0.2f);
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
    hdr.addColumn(i18n::t("intake.col_origin"), kColOrigin, 110, 90, 140, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_date"), kColDateCreated, 150, 110, 200, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_size"), kColSize, 80, 60, 120, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_path"), kColPath, 250, 120, 600, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_collection"), kColCollection, 150, 100, 240, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_source_media"), kColSourceMedia, 200, 130, 350, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_action"), kColAction, 80, 70, 100, juce::TableHeaderComponent::notSortable);
    addAndMakeVisible(*tabela_);
    tabela_->addMouseListener(this, true);

    chkSelectAllHeader_ = std::make_unique<juce::ToggleButton>();
    chkSelectAllHeader_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    chkSelectAllHeader_->setTooltip(matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Selecionar / Desmarcar Todos") : "Select / Deselect All");
    chkSelectAllHeader_->onClick = [this] {
        bool checked = chkSelectAllHeader_->getToggleState();
        selecionarTodos(checked);
    };
    tabela_->getHeader().addAndMakeVisible(*chkSelectAllHeader_);

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

void IntakeWorkspaceComponent::registrarItensRescan(const std::vector<std::pair<std::string, RescanOrigem>>& itensRescan) {
    for (const auto& par : itensRescan) {
        badgesRescanSessao_[par.first] = par.second;
    }
    carregarItens();
    for (auto& it : todosItens_) {
        if (badgesRescanSessao_.count(it.id)) {
            it.selecionado = true;
        }
    }
    atualizarContagens();
    if (tabela_) tabela_->updateContent();
    if (gridComponent_) gridComponent_->repaint();
    repaint();
}

void IntakeWorkspaceComponent::carregarItens() {
    // Preserve selection of existing items
    std::set<std::string> selecionadosAnteriores;
    for (const auto& it : todosItens_) {
        if (it.selecionado) selecionadosAnteriores.insert(it.id);
    }

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

        auto bIt = badgesRescanSessao_.find(item.id);
        if (bIt != badgesRescanSessao_.end()) {
            it.rescanOrigem = bIt->second;
            it.selecionado = true;
        } else {
            it.rescanOrigem = RescanOrigem::Nenhum;
            it.selecionado = (selecionadosAnteriores.count(item.id) > 0);
        }

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

    for (const auto& item : todosItens_) {
        if (item.categoria == "Audio") contagemAudio_++;
        else if (item.categoria == "Video") contagemVideo_++;
        else if (item.categoria == "Image") contagemImage_++;
        else if (item.categoria == "Document") contagemDoc_++;
        else contagemOther_++;
    }

    // Item 1 (lista nova de hoje): a contagem/estado dos botões de lote tem
    // que bater com o que a ação de verdade vai atingir — só o que está
    // presente sob o filtro atual, mesmo que esteja marcado "selecionado"
    // por baixo do pano.
    int totalSelecionados = 0;
    for (int idx : indicesFiltrados_) {
        if (idx >= 0 && idx < static_cast<int>(todosItens_.size()) && todosItens_[static_cast<size_t>(idx)].selecionado)
            totalSelecionados++;
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
        // "Send All" agora significa "todos os visíveis sob o filtro atual".
        btnConfirmarTodos_->setEnabled(!indicesFiltrados_.empty());
        btnConfirmarTodos_->setAlpha(!indicesFiltrados_.empty() ? 1.0f : 0.4f);
    }

    if (btnRemoverSelecao_) {
        btnRemoverSelecao_->setEnabled(true);
        btnRemoverSelecao_->setAlpha(1.0f);
    }

    if (chkSelectAllHeader_) {
        bool allSel = !indicesFiltrados_.empty();
        for (int idx : indicesFiltrados_) {
            if (idx >= 0 && idx < static_cast<int>(todosItens_.size())) {
                if (!todosItens_[static_cast<size_t>(idx)].selecionado) {
                    allSel = false;
                    break;
                }
            }
        }
        chkSelectAllHeader_->setToggleState(allSel, juce::dontSendNotification);
    }
}

void IntakeWorkspaceComponent::recarregar() {
    carregarItens();
}

std::set<std::string> IntakeWorkspaceComponent::itensSelecionados() const {
    // Item 1 (lista nova de hoje): só os que estão presentes sob o filtro
    // ativo — os escondidos por um filtro (media type etc.) não entram,
    // mesmo que continuem marcados "selecionado" por baixo do pano.
    std::set<std::string> ids;
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        const auto& item = todosItens_[static_cast<size_t>(idx)];
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
    // Item 1 (lista nova de hoje): só os selecionados que também estão
    // visíveis sob o filtro ativo.
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        auto& item = todosItens_[static_cast<size_t>(idx)];
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
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        auto& item = todosItens_[static_cast<size_t>(idx)];
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

// CREATOR e SUBJECT (item 2 da correção "BACKUP e INTAKE"): mesma coluna
// que a ficha grava (dc_creator/dc_subject via ProjetoAberto::salvarMetadado)
// — o valor aparece direto no ASSET & USER METADATA e no DUBLIN CORE do
// item assim que ele chega no Grid, sem estrutura própria de metadado.
// Fase 2b (freeze de edição em lote): a gravação em si (N itens) sai da
// message thread via poolMetadadoLote_ e vira UMA transação com UM evento
// amplo no fim, em vez de N chamadas síncronas de salvarMetadado — cada
// uma com sua própria transação implícita e seu próprio evento.
void IntakeWorkspaceComponent::aplicarCreatorAosSelecionados(const juce::String& valor) {
    std::vector<std::string> ids;
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        if (todosItens_[static_cast<size_t>(idx)].selecionado)
            ids.push_back(todosItens_[static_cast<size_t>(idx)].id);
    }
    if (ids.empty()) return;
    ProjetoAberto* projeto = &projeto_;
    std::string valorStd = valor.toStdString();
    poolMetadadoLote_.addJob([projeto, ids, valorStd] {
        projeto->salvarMetadadoEmLote(ids, {{"dc_creator", valorStd}});
    });
}

void IntakeWorkspaceComponent::aplicarSubjectAosSelecionados(const juce::String& valor) {
    std::vector<std::string> ids;
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        if (todosItens_[static_cast<size_t>(idx)].selecionado)
            ids.push_back(todosItens_[static_cast<size_t>(idx)].id);
    }
    if (ids.empty()) return;
    ProjetoAberto* projeto = &projeto_;
    std::string valorStd = valor.toStdString();
    poolMetadadoLote_.addJob([projeto, ids, valorStd] {
        projeto->salvarMetadadoEmLote(ids, {{"dc_subject", valorStd}});
    });
}

void IntakeWorkspaceComponent::aplicarEventDateAosSelecionados(const juce::String& valor) {
    juce::String v = valor.trim();
    if (v.isEmpty()) return;
    // Item 1 (lista nova de hoje): só os selecionados visíveis sob o
    // filtro ativo. Item 3: EVENT DATE passa a alimentar também DATE
    // CREATED (dc_created, a mesma coluna que a lista exibe) — a partir de
    // agora o arquivo assume esta data como data de criação. A atualização
    // do modelo em memória (item.dataCriacao) e o refresh da tabela
    // continuam síncronos (baratos, só tocam o vetor local); só a gravação
    // no banco sai da message thread.
    std::vector<std::string> ids;
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        auto& item = todosItens_[static_cast<size_t>(idx)];
        if (!item.selecionado) continue;
        ids.push_back(item.id);
        item.dataCriacao = v;
    }
    if (ids.empty()) return;
    ProjetoAberto* projeto = &projeto_;
    std::string vStd = v.toStdString();
    poolMetadadoLote_.addJob([projeto, ids, vStd] {
        projeto->salvarMetadadoEmLote(ids, {{"ano", vStd}, {"dc_created", vStd}});
    });
    if (tabela_) {
        tabela_->updateContent();
        tabela_->repaint();
    }
    atualizarContagens();
}

std::vector<juce::String> IntakeWorkspaceComponent::valoresExistentesParaColuna(const std::string& coluna) const {
    std::vector<juce::String> out;
    try {
        auto stmt = projeto_.projeto().registro().prepare(
            "SELECT DISTINCT " + coluna + " FROM item WHERE " + coluna + " IS NOT NULL AND TRIM(" + coluna + ") <> '' "
            "ORDER BY " + coluna + " COLLATE NOCASE ASC");
        while (stmt.step()) out.push_back(juce::String(stmt.columnText(0)));
    } catch (...) {}
    return out;
}

void IntakeWorkspaceComponent::mostrarEditorCreatorLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<AutocompleteLotePopupContent>(
        "Set CREATOR for Selected", "Creator name...",
        valoresExistentesParaColuna("dc_creator"),
        corDoNivelHierarquia(matriz::consolidacao::NivelHierarquia::Artista),
        [this](const juce::String& valor) { aplicarCreatorAosSelecionados(valor); });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorSubjectLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<AutocompleteLotePopupContent>(
        "Set SUBJECT for Selected", "Subject...",
        valoresExistentesParaColuna("dc_subject"),
        corDoNivelHierarquia(matriz::consolidacao::NivelHierarquia::Subject),
        [this](const juce::String& valor) { aplicarSubjectAosSelecionados(valor); });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorEventDateLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<AutocompleteLotePopupContent>(
        "Set EVENT DATE for Selected", "YYYY",
        valoresExistentesParaColuna("ano"),
        corDoNivelHierarquia(matriz::consolidacao::NivelHierarquia::Ano),
        [this](const juce::String& valor) { aplicarEventDateAosSelecionados(valor); });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorContentLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<ContentLotePopupContent>(
        corDoNivelHierarquia(matriz::consolidacao::NivelHierarquia::ContentType),
        [this](const juce::String& valor) { aplicarColecaoAosSelecionados(traduzirContent(valor, false)); });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
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
            auto selecionados = itensSelecionados();
            std::vector<std::string> idsParaAplicar;
            if (selecionados.count(itemId) > 0 && selecionados.size() > 1) {
                idsParaAplicar.assign(selecionados.begin(), selecionados.end());
            } else {
                idsParaAplicar.push_back(itemId);
            }

            for (const auto& idStr : idsParaAplicar) {
                definirSourceMediaItem(idStr, val);
                for (auto& it : todosItens_) {
                    if (it.id == idStr) {
                        it.sourceMedia = juce::String::fromUTF8(val.c_str());
                        break;
                    }
                }
            }
            if (tabela_) tabela_->repaint();
            if (gridComponent_) gridComponent_->repaint();
        });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorGeolocationLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<GeoLocationPopupContent>(
        projeto_.projeto().registro(), true,
        [this](const std::string& coords, const std::string& addr, const std::string& city, const std::string& state, const std::string& country) {
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

    // Item 1 (lista nova de hoje): itensSelecionados() já respeita o filtro
    // ativo — o que estiver escondido não entra, mesmo marcado.
    auto selecionados = itensSelecionados();
    std::vector<std::string> ids(selecionados.begin(), selecionados.end());
    if (ids.empty()) return;

    matriz::analytics::AssetGeolocationRepository::salvarEmLote(projeto_.projeto().registro(), ids, geoTemplate);
    if (tabela_) tabela_->repaint();
}

void IntakeWorkspaceComponent::confirmarSelecaoParaGrid() {
    // Item 1 (lista nova de hoje): idem — só o que está selecionado E
    // visível sob o filtro atual.
    auto selecionados = itensSelecionados();
    std::vector<std::string> ids(selecionados.begin(), selecionados.end());
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
    // Item 1 (lista nova de hoje): "ALL" passa a significar "todos os
    // visíveis sob o filtro atual" — não literalmente cada item do INTAKE.
    if (indicesFiltrados_.empty()) return;
    std::vector<std::string> ids;
    ids.reserve(indicesFiltrados_.size());
    for (int idx : indicesFiltrados_) {
        if (idx >= 0 && idx < static_cast<int>(todosItens_.size()))
            ids.push_back(todosItens_[static_cast<size_t>(idx)].id);
    }
    if (ids.empty()) return;

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
    // Item 1 (lista nova de hoje): REJECT SELECTED só atinge o que está
    // selecionado E visível sob o filtro ativo.
    auto selecionados = itensSelecionados();
    std::vector<std::string> ids(selecionados.begin(), selecionados.end());
    if (ids.empty()) return;

    projeto_.removerItensDoProjeto(ids);
    recarregar();
}

void IntakeWorkspaceComponent::mostrarMenuColecaoParaItem(int itemIndex, juce::Rectangle<int> screenBounds) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;

    juce::PopupMenu menu;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    menu.addItem(1, isPt ? juce::String::fromUTF8("Nenhum (Limpar)") : "None (Clear)");
    menu.addSeparator();

    int id = 2;
    std::map<int, juce::String> idParaNome;
    for (const auto& cat : vocabularioColecoes()) {
        juce::PopupMenu sub;
        for (const auto& item : cat.itens) {
            sub.addItem(id, isPt ? traduzirContent(item, true) : item);
            idParaNome[id] = item;
            id++;
        }
        juce::String grupoNome = cat.grupo;
        if (isPt) {
            if (grupoNome == "AUDIO") grupoNome = juce::String::fromUTF8("ÁUDIO");
            else if (grupoNome == "VIDEO") grupoNome = juce::String::fromUTF8("VÍDEO");
            else if (grupoNome == "IMAGE") grupoNome = juce::String::fromUTF8("IMAGEM");
            else if (grupoNome == "DOCUMENT") grupoNome = juce::String::fromUTF8("DOCUMENTO");
        }
        menu.addSubMenu(grupoNome, sub);
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

        auto selecionados = safeThis->itensSelecionados();
        std::vector<std::string> idsParaAplicar;
        if (selecionados.count(itemId) > 0 && selecionados.size() > 1) {
            idsParaAplicar.assign(selecionados.begin(), selecionados.end());
        } else {
            idsParaAplicar.push_back(itemId);
        }

        for (const auto& idStr : idsParaAplicar) {
            safeThis->definirColecaoItem(idStr, chosen);
            for (auto& it : safeThis->todosItens_) {
                if (it.id == idStr) {
                    it.collection = chosen;
                    break;
                }
            }
        }
        if (safeThis->tabela_) safeThis->tabela_->repaint();
        if (safeThis->gridComponent_) safeThis->gridComponent_->repaint();
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
    } else if (columnId == kColOrigin) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        juce::String extStr = item.extensao.isNotEmpty() ? item.extensao.toUpperCase() : "-";
        g.drawText(extStr, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColDateCreated) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        // Item 3 (nova lista): a coluna mostra só o ano (AAAA) — a data
        // completa continua guiando a ordenação por esta coluna (ver
        // compareElements, kColDateCreated), só a exibição encolheu.
        juce::String ano = item.dataCriacao.length() >= 4 ? item.dataCriacao.substring(0, 4) : juce::String();
        g.drawText(ano.isNotEmpty() ? ano : "-", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
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
            bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(traduzirContent(item.collection, isPt), badgeArea, juce::Justification::centred, true);
        } else {
            bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
            g.drawText(isPt ? juce::String::fromUTF8("Nenhum") : "None", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
    } else if (columnId == kColSourceMedia) {
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        if (!item.sourceMedia.isEmpty()) {
            auto info = OriginalSourceMediumInfo::deserialize(item.sourceMedia.toStdString());
            juce::String text = juce::String::fromUTF8(info.toDisplaySummary().c_str());
            if (text.isEmpty() || text == "None / Unknown") {
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
                g.drawText(isPt ? juce::String::fromUTF8("Nenhum") : "None", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
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
            g.drawText(isPt ? juce::String::fromUTF8("Nenhum") : "None", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
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
            case kColOrigin:
                cmp = a.extensao.compareIgnoreCase(b.extensao);
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
        // item (shift-clique seleciona intervalo): segura shift e clica em
        // outra linha — tudo entre a âncora (último clique simples) e esta
        // linha vira selecionado, igual Finder/Explorer. A âncora só muda
        // num clique simples (sem shift), pra dar pra esticar/encolher o
        // intervalo repetindo o shift+clique a partir do mesmo ponto.
        if (e.mods.isShiftDown() && ultimaPosicaoClicadaParaSelecao_ >= 0 &&
            ultimaPosicaoClicadaParaSelecao_ < static_cast<int>(indicesFiltrados_.size())) {
            int de = std::min(ultimaPosicaoClicadaParaSelecao_, rowNumber);
            int ate = std::max(ultimaPosicaoClicadaParaSelecao_, rowNumber);
            for (int p = de; p <= ate; ++p) {
                int ri = indicesFiltrados_[static_cast<size_t>(p)];
                if (ri >= 0 && ri < static_cast<int>(todosItens_.size()))
                    todosItens_[static_cast<size_t>(ri)].selecionado = true;
            }
        } else {
            todosItens_[static_cast<size_t>(realIndex)].selecionado = !todosItens_[static_cast<size_t>(realIndex)].selecionado;
            ultimaPosicaoClicadaParaSelecao_ = rowNumber;
        }
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
    auto selecionados = itensSelecionados();
    if (selecionados.count(itemId) > 0 && selecionados.size() > 1) {
        std::vector<std::string> ids(selecionados.begin(), selecionados.end());
        projeto_.removerItensDoProjeto(ids);
    } else {
        projeto_.removerItensDoProjeto({itemId});
    }
    recarregar();
}

void IntakeWorkspaceComponent::mostrarMenuContexto(int itemIndex, juce::Point<int> screenPos) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    juce::PopupMenu m;
    m.addItem(1, isPt ? juce::String::fromUTF8("Rejeitar") : "Reject");
    m.addItem(2, isPt ? juce::String::fromUTF8("Mostrar Origem") : "Show at Source");
    m.addSeparator();
    m.addItem(3, isPt ? juce::String::fromUTF8("Obter Informações...") : "Get Info...");

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

void IntakeWorkspaceComponent::mouseDown(const juce::MouseEvent& e) {
    if (!tabela_ || modoVisao_ != ModoVisao::Lista) return;

    auto rel = e.getEventRelativeTo(tabela_.get());
    if (rel.x < 0 || rel.y < 0 || rel.x >= tabela_->getWidth() || rel.y >= tabela_->getHeight())
        return;

    // Check click in header area of column kColSelect
    if (rel.y < tabela_->getHeaderHeight()) {
        int colIdx = tabela_->getHeader().getIndexOfColumnId(kColSelect, true);
        auto pos = tabela_->getHeader().getColumnPosition(colIdx >= 0 ? colIdx : 0);
        if (rel.x >= pos.getX() && rel.x < pos.getRight()) {
            if (e.eventComponent != chkSelectAllHeader_.get()) {
                if (chkSelectAllHeader_) {
                    chkSelectAllHeader_->setToggleState(!chkSelectAllHeader_->getToggleState(), juce::sendNotification);
                }
            }
        }
        return;
    }

    if (!e.mods.isLeftButtonDown()) return;

    int row = tabela_->getRowContainingPosition(rel.x, rel.y);
    if (row >= 0 && row < static_cast<int>(indicesFiltrados_.size())) {
        isDraggingRows_ = true;
        dragStartRow_ = row;
        dragSelectState_ = true;
    }
}

void IntakeWorkspaceComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDraggingRows_ || !tabela_ || modoVisao_ != ModoVisao::Lista) return;

    auto rel = e.getEventRelativeTo(tabela_.get());
    int row = tabela_->getRowContainingPosition(rel.x, rel.y);
    if (row >= 0 && row < static_cast<int>(indicesFiltrados_.size())) {
        int rStart = std::min(dragStartRow_, row);
        int rEnd = std::max(dragStartRow_, row);
        bool changed = false;
        for (int r = rStart; r <= rEnd; ++r) {
            int realIdx = indicesFiltrados_[static_cast<size_t>(r)];
            if (realIdx >= 0 && realIdx < static_cast<int>(todosItens_.size())) {
                if (todosItens_[static_cast<size_t>(realIdx)].selecionado != dragSelectState_) {
                    todosItens_[static_cast<size_t>(realIdx)].selecionado = dragSelectState_;
                    changed = true;
                }
            }
        }
        if (changed) {
            tabela_->updateContent();
            tabela_->repaint();
            atualizarContagens();
        }
    }
}

void IntakeWorkspaceComponent::mouseUp(const juce::MouseEvent&) {
    isDraggingRows_ = false;
    dragStartRow_ = -1;
}

void IntakeWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();

    g.fillAll(tk.fundo);

    // A faixa superior de 44px saiu (itens 2 e 3 do ajuste de layout do
    // INTAKE): INGEST/GOOGLE DRIVE/LIGHTROOM e HELP/CLOSE PROJECT subiram
    // para a linha das tabs, e a linha que sobrou foi eliminada — o
    // conteúdo começa direto no topo do workspace.

    // Left Sidebar background (230px wide, full height)
    g.setColour(tk.fundo);
    g.fillRect(0, 0, 230, getHeight());

    // Divider
    g.setColour(tk.borda);
    g.fillRect(229, 0, 1, getHeight());

    // Cards da coluna esquerda (FILTER / SELECTION / BATCH ASSIGNMENT /
    // ACTIONS) — mesmo tratamento visual da aba METADATA.
    for (const auto& cardBounds : secaoCardBounds_) {
        g.setColour(tk.painelAlt.withAlpha(0.25f));
        g.fillRoundedRectangle(cardBounds.toFloat(), tk.raioPequeno);
        g.setColour(tk.borda.withAlpha(0.7f));
        g.drawRoundedRectangle(cardBounds.toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);
    }

    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    for (const auto& [titulo, bounds] : secaoHeaderBounds_) {
        auto textoBounds = bounds;
        if (titulo == "GEO LOCATION" && iconeGeo_.isValid()) {
            const int iconeAltura = juce::jmin(bounds.getHeight(), 14);
            auto iconeBounds = textoBounds.removeFromLeft(iconeAltura).withSizeKeepingCentre(iconeAltura, iconeAltura);
            g.drawImage(iconeGeo_, iconeBounds.toFloat(), juce::RectanglePlacement::centred);
            textoBounds.removeFromLeft(4);
        }
        g.setColour(tk.textoPrimario);
        g.drawText(titulo, textoBounds, juce::Justification::centredLeft);
    }
}

void IntakeWorkspaceComponent::setHasParentCatalog(bool hasParent) {
    hasParentCatalog_ = hasParent;
    resized();
}

void IntakeWorkspaceComponent::componentesBarraSuperior(
        std::vector<std::pair<juce::Component*, int>>& esquerda,
        std::vector<std::pair<juce::Component*, int>>& direita) {
    esquerda.clear();
    direita.clear();
    // INGEST FILES / GOOGLE DRIVE / LIGHTROOM saíram da linha das tabs e
    // moraram no card IMPORT, no topo da coluna esquerda (acima do FILTER)
    // — ver resized(). A barra de navegação continua hospedando só o HELP.
    if (btnAjuda_)        direita.push_back({btnAjuda_.get(), 28});
}

void IntakeWorkspaceComponent::resized() {
    auto area = getLocalBounds();

    // Hide old labels
    if (lblTitulo_) lblTitulo_->setVisible(false);
    if (lblContadorTotal_) lblContadorTotal_->setVisible(false);

    // A barra superior de 44px não existe mais aqui: os cinco comandos
    // (INGEST FILES / GOOGLE DRIVE / LIGHTROOM à esquerda, HELP / CLOSE
    // PROJECT à direita) são posicionados pela barra de navegação, que os
    // hospeda na mesma linha das tabs — ver componentesBarraSuperior().

    // Left Sidebar (230px wide)
    auto sidebar = area.removeFromLeft(230).reduced(10, 8);

    // O seletor de modo de visualização (List / Miniatures) é posicionado
    // dentro do card FILTER, logo abaixo — não há mais um bloco solto aqui.

    // Cards da coluna esquerda (mesmo tratamento da aba METADATA, item 4 do
    // segundo lote de correções): cada cluster lógico vira um card com
    // título e borda próprios, no lugar das linhas divisórias soltas.
    if (divisor1_) divisor1_->setVisible(false);
    if (divisor2_) divisor2_->setVisible(false);

    secaoHeaderBounds_.clear();
    secaoCardBounds_.clear();
    const int kCardPad = 6;
    const int kHeaderH = 18;
    int cardTop = 0;
    auto iniciarCard = [&] {
        sidebar.removeFromTop(kCardPad);
        cardTop = sidebar.getY();
    };
    auto finalizarCard = [&] {
        sidebar.removeFromTop(kCardPad);
        secaoCardBounds_.push_back(juce::Rectangle<int>(sidebar.getX() - 4, cardTop - kCardPad,
                                                          sidebar.getWidth() + 8, sidebar.getY() - cardTop + kCardPad));
    };

    // 0. IMPORT — os três comandos de entrada de arquivo (INGEST FILES,
    // GOOGLE DRIVE, LIGHTROOM) vivem aqui, em card próprio no topo da
    // coluna esquerda, em vez de emprestados para a linha das tabs.
    if (btnIngerir_ && btnGoogleDrive_ && btnLightroom_) {
        for (juce::Component* c : {static_cast<juce::Component*>(btnIngerir_.get()),
                                   static_cast<juce::Component*>(btnGoogleDrive_.get()),
                                   static_cast<juce::Component*>(btnLightroom_.get())}) {
            if (c->getParentComponent() != this) addAndMakeVisible(*c);
        }
        iniciarCard();
        secaoHeaderBounds_.push_back({i18n::t("intake.secao_import"), sidebar.removeFromTop(kHeaderH)});
        sidebar.removeFromTop(4);
        btnIngerir_->setBounds(sidebar.removeFromTop(28));
        sidebar.removeFromTop(4);
        {
            auto importRow = sidebar.removeFromTop(28);
            int half = (importRow.getWidth() - 6) / 2;
            btnGoogleDrive_->setBounds(importRow.removeFromLeft(half));
            importRow.removeFromLeft(6);
            btnLightroom_->setBounds(importRow);
        }
        finalizarCard();
        sidebar.removeFromTop(8);
    }

    // 1. FILTER (view mode + category pills)
    iniciarCard();
    secaoHeaderBounds_.push_back({i18n::t("intake.secao_filter"), sidebar.removeFromTop(kHeaderH)});
    sidebar.removeFromTop(4);
    if (btnVisaoLista_ && btnVisaoIcones_) {
        auto viewRow = sidebar.removeFromTop(26);
        int half = (viewRow.getWidth() - 6) / 2;
        btnVisaoLista_->setBounds(viewRow.removeFromLeft(half));
        viewRow.removeFromLeft(6);
        btnVisaoIcones_->setBounds(viewRow);
        sidebar.removeFromTop(6);
    }
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
    finalizarCard();
    sidebar.removeFromTop(8);

    // 2. SELECTION
    iniciarCard();
    secaoHeaderBounds_.push_back({i18n::t("intake.secao_selection"), sidebar.removeFromTop(kHeaderH)});
    sidebar.removeFromTop(4);
    lblSubtitulo_->setBounds(sidebar.removeFromTop(18));
    sidebar.removeFromTop(4);
    {
        auto selRow = sidebar.removeFromTop(24);
        int half = (selRow.getWidth() - 6) / 2;
        btnSelecionarTodos_->setBounds(selRow.removeFromLeft(half));
        selRow.removeFromLeft(6);
        btnLimparSelecao_->setBounds(selRow);
    }
    finalizarCard();
    sidebar.removeFromTop(8);

    // 3. BATCH ASSIGNMENT — quatro botões coloridos, um por campo de
    // metadata (item 2 da correção "BACKUP e INTAKE"), sem rótulo/combo
    // genérico. GEO LOCATION fica de fora, em card próprio logo abaixo
    // (item 3 — "deixar clara a separação").
    iniciarCard();
    secaoHeaderBounds_.push_back({i18n::t("intake.secao_batch"), sidebar.removeFromTop(kHeaderH)});
    sidebar.removeFromTop(4);
    btnSourceMediumLote_->setBounds(sidebar.removeFromTop(26));
    sidebar.removeFromTop(4);
    btnCreatorLote_->setBounds(sidebar.removeFromTop(26));
    sidebar.removeFromTop(4);
    btnContentLote_->setBounds(sidebar.removeFromTop(26));
    sidebar.removeFromTop(4);
    btnSubjectLote_->setBounds(sidebar.removeFromTop(26));
    sidebar.removeFromTop(4);
    btnEventDateLote_->setBounds(sidebar.removeFromTop(26));
    finalizarCard();
    sidebar.removeFromTop(8);

    // 3b. GEO LOCATION — seção separada dos quatro campos de metadata acima.
    iniciarCard();
    secaoHeaderBounds_.push_back({"GEO LOCATION", sidebar.removeFromTop(kHeaderH)});
    sidebar.removeFromTop(4);
    btnGeolocationLote_->setBounds(sidebar.removeFromTop(26));
    finalizarCard();
    sidebar.removeFromTop(8);

    // 4. ACTIONS (Send to Grid / Reject)
    iniciarCard();
    secaoHeaderBounds_.push_back({i18n::t("intake.secao_actions"), sidebar.removeFromTop(kHeaderH)});
    sidebar.removeFromTop(4);
    btnConfirmarSelecao_->setBounds(sidebar.removeFromTop(28));
    sidebar.removeFromTop(4);
    btnConfirmarTodos_->setBounds(sidebar.removeFromTop(28));
    sidebar.removeFromTop(4);
    btnRemoverSelecao_->setBounds(sidebar.removeFromTop(28));
    finalizarCard();

    // Right Area fills the remaining workspace (x = 230 to width, full height)
    if (tabela_) {
        tabela_->setBounds(area);
        if (chkSelectAllHeader_) {
            int colIdx = tabela_->getHeader().getIndexOfColumnId(kColSelect, true);
            auto pos = tabela_->getHeader().getColumnPosition(colIdx >= 0 ? colIdx : 0);
            chkSelectAllHeader_->setBounds(pos.getX() + 2, 2, pos.getWidth() - 4, tabela_->getHeaderHeight() - 4);
        }
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
    if (chkSelectAllHeader_) {
        chkSelectAllHeader_->setTooltip(matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Selecionar / Desmarcar Todos") : "Select / Deselect All");
        chkSelectAllHeader_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    auto updatePill = [](juce::TextButton* btn, juce::Colour textCol, juce::Colour borderCol) {
        if (auto* p = dynamic_cast<PillButton*>(btn)) {
            p->corTextoCustom = textCol;
            p->corBordaCustom = borderCol;
            p->repaint();
        }
    };

    if (btnSelecionarTodos_) {
        btnSelecionarTodos_->setButtonText(i18n::t("intake.selecionar_todos"));
        updatePill(btnSelecionarTodos_.get(), juce::Colours::transparentBlack, juce::Colours::transparentBlack);
    }
    if (btnLimparSelecao_) {
        btnLimparSelecao_->setButtonText(i18n::t("intake.limpar_selecao"));
        updatePill(btnLimparSelecao_.get(), juce::Colours::transparentBlack, juce::Colours::transparentBlack);
    }
    // SOURCE MEDIUM/CREATOR/CONTENT/SUBJECT mantêm a cor fixa do bloco
    // correspondente no Visual Editor (setada no construtor) — não passam
    // por updatePill, que zeraria essa cor a cada mudança de tema/locale.
    if (btnGeolocationLote_) {
        btnGeolocationLote_->setButtonText(i18n::t("intake.btn_geo_lote"));
        updatePill(btnGeolocationLote_.get(), juce::Colours::transparentBlack, juce::Colours::transparentBlack);
    }
    if (btnConfirmarSelecao_) {
        btnConfirmarSelecao_->setButtonText(i18n::t("intake.btn_enviar_selecionados"));
        updatePill(btnConfirmarSelecao_.get(), juce::Colours::white, juce::Colour(0xff22c55e));
    }
    if (btnConfirmarTodos_) {
        btnConfirmarTodos_->setButtonText(i18n::t("intake.btn_enviar_todos"));
        updatePill(btnConfirmarTodos_.get(), juce::Colours::white, juce::Colour(0xff22c55e));
    }
    if (btnRemoverSelecao_) {
        btnRemoverSelecao_->setButtonText(i18n::t("intake.btn_rejeitar_selecionados"));
        updatePill(btnRemoverSelecao_.get(), juce::Colours::white, tk.perigo);
    }
    if (btnAjuda_) {
        btnAjuda_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnAjuda_->setColour(juce::TextButton::textColourOffId, tk.acento);
        btnAjuda_->setColour(juce::TextButton::textColourOnId, tk.acento);
    }
    if (tabela_) {
        auto& hdr = tabela_->getHeader();
        hdr.setColumnName(kColType, i18n::t("intake.col_type"));
        hdr.setColumnName(kColName, i18n::t("intake.col_name"));
        hdr.setColumnName(kColOrigin, i18n::t("intake.col_origin"));
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
        tabela_->updateContent();
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
