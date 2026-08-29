// ==============================================================================
// ORIGINAL SOURCE MEDIUM IMPLEMENTATION
// STRICT APP-WIDE RULE: 100% ENGLISH UI. ZERO PORTUGUESE TEXT IN USER INTERFACE.
// ==============================================================================

#include "OriginalSourceMedium.h"
#include "Tokens.h"

namespace matriz::ui {

static const std::vector<MediumCategoryGroup> kMediumVocab = {
    {
        "AUDIO (ANALOG & DIGITAL)",
        {
            "1/4\" Tape",
            "1/2\" Tape",
            "1\" Tape",
            "2\" Tape",
            "Cassette Tape",
            "8-Track Cartridge",
            "DAT",
            "ADAT",
            "DTRS / Hi8 Digital Audio Tape",
            "12\" Vinyl",
            "10\" Vinyl",
            "7\" Vinyl",
            "Lacquer",
            "Shellac"
        }
    },
    {
        "FILM (MOTION PICTURE)",
        {
            "35mm Film",
            "16mm Film",
            "Super 16 Film",
            "8mm Film",
            "Super 8 Film"
        }
    },
    {
        "VIDEO (ANALOG & DIGITAL TAPE)",
        {
            "VHS",
            "VHS-C",
            "S-VHS",
            "Betamax",
            "Betacam / Betacam SP",
            "U-matic",
            "1\" Type B / 1\" Type C",
            "Video8",
            "Hi8",
            "MiniDV",
            "DV / DVCAM",
            "DVCPRO / DVCPRO HD",
            "HDV",
            "Digital8",
            "HDCAM / HDCAM SR",
            "Digital Betacam"
        }
    },
    {
        "IMAGE (STILL & PHYSICAL)",
        {
            "35mm Film (Still)",
            "120 / 220 Film",
            "4x5 / 5x7 / 8x10 Large Format Film",
            "Instant Film",
            "Glass Plate / Glass Negative",
            "Photographic Print"
        }
    },
    {
        "DOCUMENT",
        {
            "Physical Paper / Document",
            "Microfilm / Microfiche"
        }
    },
    {
        "UNIVERSAL & BORN-DIGITAL",
        {
            "Native Digital",
            "None / Unknown",
            "Other"
        }
    }
};

const std::vector<MediumCategoryGroup>& OriginalSourceMediumVocabulary::getMediumCategories() {
    return kMediumVocab;
}

void OriginalSourceMediumVocabulary::populateMediumCombo(juce::ComboBox& combo, bool includeNone) {
    combo.clear(juce::dontSendNotification);
    int id = 1;
    if (includeNone) {
        combo.addItem("None / Unknown", id++);
        combo.addItem("Native Digital", id++);
        combo.addItem("Other", id++);
        combo.addSeparator();
    }
    for (const auto& grp : kMediumVocab) {
        combo.addSectionHeading(grp.groupName);
        for (const auto& med : grp.mediums) {
            if (includeNone && (med == "None / Unknown" || med == "Native Digital" || med == "Other"))
                continue;
            combo.addItem(med, id++);
        }
    }
}

std::string OriginalSourceMediumInfo::toDisplaySummary() const {
    if (isNoneOrUnknown()) return "None / Unknown";
    if (isNativeDigital()) return "Native Digital";
    if (medium == "Other") {
        return customNote.empty() ? "Other" : ("Other: " + customNote);
    }

    std::vector<std::string> parts;
    auto addIfVal = [&](const std::string& v) {
        if (!v.empty() && v != "Unknown" && v != "None") parts.push_back(v);
    };

    addIfVal(speed);
    addIfVal(trackFormat);
    addIfVal(referenceEq);
    addIfVal(tapeType);
    addIfVal(noiseReduction);
    addIfVal(discType);
    addIfVal(recordingStandard);
    addIfVal(recordingMode);
    addIfVal(filmType);
    addIfVal(sound);
    addIfVal(gaugeFormat);
    addIfVal(projectionSpeed);
    addIfVal(videoStandard);
    addIfVal(recordingFormat);
    addIfVal(audioConfig);
    addIfVal(color);
    if (!tapeFormulation.empty()) parts.push_back(tapeFormulation);
    if (!customNote.empty()) parts.push_back(customNote);

    if (parts.empty()) return medium;

    std::string summary = medium + " (";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) summary += ", ";
        summary += parts[i];
    }
    summary += ")";
    return summary;
}

std::string OriginalSourceMediumInfo::serialize() const {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("medium", juce::String::fromUTF8(medium.c_str()));
    if (!speed.empty()) obj->setProperty("speed", juce::String::fromUTF8(speed.c_str()));
    if (!trackFormat.empty()) obj->setProperty("trackFormat", juce::String::fromUTF8(trackFormat.c_str()));
    if (!referenceEq.empty()) obj->setProperty("referenceEq", juce::String::fromUTF8(referenceEq.c_str()));
    if (!tapeFormulation.empty()) obj->setProperty("tapeFormulation", juce::String::fromUTF8(tapeFormulation.c_str()));
    if (!tapeType.empty()) obj->setProperty("tapeType", juce::String::fromUTF8(tapeType.c_str()));
    if (!noiseReduction.empty()) obj->setProperty("noiseReduction", juce::String::fromUTF8(noiseReduction.c_str()));
    if (!discType.empty()) obj->setProperty("discType", juce::String::fromUTF8(discType.c_str()));
    if (!recordingStandard.empty()) obj->setProperty("recordingStandard", juce::String::fromUTF8(recordingStandard.c_str()));
    if (!recordingMode.empty()) obj->setProperty("recordingMode", juce::String::fromUTF8(recordingMode.c_str()));
    if (!filmType.empty()) obj->setProperty("filmType", juce::String::fromUTF8(filmType.c_str()));
    if (!sound.empty()) obj->setProperty("sound", juce::String::fromUTF8(sound.c_str()));
    if (!gaugeFormat.empty()) obj->setProperty("gaugeFormat", juce::String::fromUTF8(gaugeFormat.c_str()));
    if (!projectionSpeed.empty()) obj->setProperty("projectionSpeed", juce::String::fromUTF8(projectionSpeed.c_str()));
    if (!videoStandard.empty()) obj->setProperty("videoStandard", juce::String::fromUTF8(videoStandard.c_str()));
    if (!recordingFormat.empty()) obj->setProperty("recordingFormat", juce::String::fromUTF8(recordingFormat.c_str()));
    if (!audioConfig.empty()) obj->setProperty("audioConfig", juce::String::fromUTF8(audioConfig.c_str()));
    if (!color.empty()) obj->setProperty("color", juce::String::fromUTF8(color.c_str()));
    if (!customNote.empty()) obj->setProperty("customNote", juce::String::fromUTF8(customNote.c_str()));

    juce::var v(obj.get());
    return juce::JSON::toString(v, true).toStdString();
}

OriginalSourceMediumInfo OriginalSourceMediumInfo::deserialize(const std::string& str) {
    OriginalSourceMediumInfo info;
    if (str.empty()) return info;

    juce::var parsed = juce::JSON::parse(juce::String::fromUTF8(str.c_str()));
    if (parsed.isObject()) {
        auto* obj = parsed.getDynamicObject();
        if (obj->hasProperty("medium")) info.medium = obj->getProperty("medium").toString().toStdString();
        if (obj->hasProperty("speed")) info.speed = obj->getProperty("speed").toString().toStdString();
        if (obj->hasProperty("trackFormat")) info.trackFormat = obj->getProperty("trackFormat").toString().toStdString();
        if (obj->hasProperty("referenceEq")) info.referenceEq = obj->getProperty("referenceEq").toString().toStdString();
        if (obj->hasProperty("tapeFormulation")) info.tapeFormulation = obj->getProperty("tapeFormulation").toString().toStdString();
        if (obj->hasProperty("tapeType")) info.tapeType = obj->getProperty("tapeType").toString().toStdString();
        if (obj->hasProperty("noiseReduction")) info.noiseReduction = obj->getProperty("noiseReduction").toString().toStdString();
        if (obj->hasProperty("discType")) info.discType = obj->getProperty("discType").toString().toStdString();
        if (obj->hasProperty("recordingStandard")) info.recordingStandard = obj->getProperty("recordingStandard").toString().toStdString();
        if (obj->hasProperty("recordingMode")) info.recordingMode = obj->getProperty("recordingMode").toString().toStdString();
        if (obj->hasProperty("filmType")) info.filmType = obj->getProperty("filmType").toString().toStdString();
        if (obj->hasProperty("sound")) info.sound = obj->getProperty("sound").toString().toStdString();
        if (obj->hasProperty("gaugeFormat")) info.gaugeFormat = obj->getProperty("gaugeFormat").toString().toStdString();
        if (obj->hasProperty("projectionSpeed")) info.projectionSpeed = obj->getProperty("projectionSpeed").toString().toStdString();
        if (obj->hasProperty("videoStandard")) info.videoStandard = obj->getProperty("videoStandard").toString().toStdString();
        if (obj->hasProperty("recordingFormat")) info.recordingFormat = obj->getProperty("recordingFormat").toString().toStdString();
        if (obj->hasProperty("audioConfig")) info.audioConfig = obj->getProperty("audioConfig").toString().toStdString();
        if (obj->hasProperty("color")) info.color = obj->getProperty("color").toString().toStdString();
        if (obj->hasProperty("customNote")) info.customNote = obj->getProperty("customNote").toString().toStdString();
    } else {
        // Plain text fallback
        info.medium = str;
    }
    return info;
}

OriginalSourceMediumEditorComponent::OriginalSourceMediumEditorComponent() {
    const auto& tk = tema();

    lblMedium_ = std::make_unique<juce::Label>("", "MEDIUM");
    lblMedium_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblMedium_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblMedium_);

    comboMedium_ = std::make_unique<juce::ComboBox>();
    OriginalSourceMediumVocabulary::populateMediumCombo(*comboMedium_, true);
    comboMedium_->setText("None / Unknown", juce::dontSendNotification);
    comboMedium_->onChange = [this] {
        currentInfo_.medium = comboMedium_->getText().toStdString();
        rebuildSubfields();
        fireChange();
    };
    addAndMakeVisible(*comboMedium_);

    rebuildSubfields();
}

OriginalSourceMediumEditorComponent::~OriginalSourceMediumEditorComponent() = default;

void OriginalSourceMediumEditorComponent::setCompactMode(bool compact) {
    compactMode_ = compact;
    resized();
}

void OriginalSourceMediumEditorComponent::setValue(const OriginalSourceMediumInfo& info) {
    currentInfo_ = info;
    comboMedium_->setText(juce::String::fromUTF8(currentInfo_.medium.c_str()), juce::dontSendNotification);
    rebuildSubfields();
}

OriginalSourceMediumInfo OriginalSourceMediumEditorComponent::getValue() const {
    OriginalSourceMediumInfo info = currentInfo_;
    info.medium = comboMedium_->getText().toStdString();
    for (const auto& sf : subfields_) {
        std::string val;
        if (sf.combo) val = sf.combo->getText().toStdString();
        else if (sf.textEditor) val = sf.textEditor->getText().toStdString();

        if (sf.key == "speed") info.speed = val;
        else if (sf.key == "trackFormat") info.trackFormat = val;
        else if (sf.key == "referenceEq") info.referenceEq = val;
        else if (sf.key == "tapeFormulation") info.tapeFormulation = val;
        else if (sf.key == "tapeType") info.tapeType = val;
        else if (sf.key == "noiseReduction") info.noiseReduction = val;
        else if (sf.key == "discType") info.discType = val;
        else if (sf.key == "recordingStandard") info.recordingStandard = val;
        else if (sf.key == "recordingMode") info.recordingMode = val;
        else if (sf.key == "filmType") info.filmType = val;
        else if (sf.key == "sound") info.sound = val;
        else if (sf.key == "gaugeFormat") info.gaugeFormat = val;
        else if (sf.key == "projectionSpeed") info.projectionSpeed = val;
        else if (sf.key == "videoStandard") info.videoStandard = val;
        else if (sf.key == "recordingFormat") info.recordingFormat = val;
        else if (sf.key == "audioConfig") info.audioConfig = val;
        else if (sf.key == "color") info.color = val;
        else if (sf.key == "customNote") info.customNote = val;
    }
    return info;
}

void OriginalSourceMediumEditorComponent::setValueString(const std::string& raw) {
    setValue(OriginalSourceMediumInfo::deserialize(raw));
}

std::string OriginalSourceMediumEditorComponent::getValueString() const {
    return getValue().serialize();
}

void OriginalSourceMediumEditorComponent::addComboField(const juce::String& key, const juce::String& label, const std::vector<juce::String>& options, const std::string& selectedVal) {
    const auto& tk = tema();
    Subfield sf;
    sf.key = key;
    sf.labelText = label;

    sf.label = std::make_unique<juce::Label>("", label);
    sf.label->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    sf.label->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*sf.label);

    sf.combo = std::make_unique<juce::ComboBox>();
    int id = 1;
    for (const auto& opt : options) {
        sf.combo->addItem(opt, id++);
    }
    if (!selectedVal.empty()) {
        sf.combo->setText(juce::String::fromUTF8(selectedVal.c_str()), juce::dontSendNotification);
    } else {
        sf.combo->setText("Unknown", juce::dontSendNotification);
    }
    sf.combo->onChange = [this] { fireChange(); };
    addAndMakeVisible(*sf.combo);

    subfields_.push_back(std::move(sf));
}

void OriginalSourceMediumEditorComponent::addTextField(const juce::String& key, const juce::String& label, const std::string& currentVal) {
    const auto& tk = tema();
    Subfield sf;
    sf.key = key;
    sf.labelText = label;

    sf.label = std::make_unique<juce::Label>("", label);
    sf.label->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    sf.label->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*sf.label);

    sf.textEditor = std::make_unique<juce::TextEditor>();
    sf.textEditor->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    sf.textEditor->setText(juce::String::fromUTF8(currentVal.c_str()), false);
    sf.textEditor->onTextChange = [this] { fireChange(); };
    addAndMakeVisible(*sf.textEditor);

    subfields_.push_back(std::move(sf));
}

void OriginalSourceMediumEditorComponent::rebuildSubfields() {
    for (auto& sf : subfields_) {
        if (sf.label) removeChildComponent(sf.label.get());
        if (sf.combo) removeChildComponent(sf.combo.get());
        if (sf.textEditor) removeChildComponent(sf.textEditor.get());
    }
    subfields_.clear();

    const std::string med = currentInfo_.medium;

    if (med == "1/4\" Tape") {
        addComboField("speed", "TAPE SPEED", {"3.75 IPS", "7.5 IPS", "15 IPS", "30 IPS", "Other", "Unknown"}, currentInfo_.speed);
        addComboField("trackFormat", "TRACK FORMAT", {"Full Track", "Half Track", "2-Track", "3-Track", "4-Track", "Other", "Unknown"}, currentInfo_.trackFormat);
        addComboField("referenceEq", "REFERENCE / EQ", {"NAB", "IEC / CCIR", "AES", "DIN", "Other", "Unknown"}, currentInfo_.referenceEq);
        addTextField("tapeFormulation", "TAPE FORMULATION", currentInfo_.tapeFormulation);
    } else if (med == "1/2\" Tape") {
        addComboField("speed", "TAPE SPEED", {"7.5 IPS", "15 IPS", "30 IPS", "Other", "Unknown"}, currentInfo_.speed);
        addComboField("trackFormat", "TRACK FORMAT", {"2-Track", "4-Track", "8-Track", "Other", "Unknown"}, currentInfo_.trackFormat);
        addComboField("referenceEq", "REFERENCE / EQ", {"NAB", "IEC / CCIR", "AES", "Other", "Unknown"}, currentInfo_.referenceEq);
        addTextField("tapeFormulation", "TAPE FORMULATION", currentInfo_.tapeFormulation);
    } else if (med == "1\" Tape") {
        addComboField("speed", "TAPE SPEED", {"15 IPS", "30 IPS", "Other", "Unknown"}, currentInfo_.speed);
        addComboField("trackFormat", "TRACK FORMAT", {"8-Track", "16-Track", "Other", "Unknown"}, currentInfo_.trackFormat);
        addComboField("referenceEq", "REFERENCE / EQ", {"NAB", "IEC / CCIR", "AES", "Other", "Unknown"}, currentInfo_.referenceEq);
        addTextField("tapeFormulation", "TAPE FORMULATION", currentInfo_.tapeFormulation);
    } else if (med == "2\" Tape") {
        addComboField("speed", "TAPE SPEED", {"15 IPS", "30 IPS", "Other", "Unknown"}, currentInfo_.speed);
        addComboField("trackFormat", "TRACK FORMAT", {"16-Track", "24-Track", "Other", "Unknown"}, currentInfo_.trackFormat);
        addComboField("referenceEq", "REFERENCE / EQ", {"NAB", "IEC / CCIR", "AES", "Other", "Unknown"}, currentInfo_.referenceEq);
    } else if (med == "Cassette Tape") {
        addComboField("speed", "TAPE SPEED", {"1 7/8 IPS (1.875)", "Other", "Unknown"}, currentInfo_.speed);
        addComboField("tapeType", "TAPE TYPE", {"Type I - Normal", "Type II - Chrome", "Type III - Ferric-Chrome", "Type IV - Metal", "Unknown"}, currentInfo_.tapeType);
        addComboField("trackFormat", "TRACK FORMAT", {"Mono", "Stereo", "4-Track", "Other", "Unknown"}, currentInfo_.trackFormat);
        addComboField("noiseReduction", "NOISE REDUCTION", {"None", "Dolby B", "Dolby C", "Dolby S", "dbx", "Other", "Unknown"}, currentInfo_.noiseReduction);
    } else if (med == "8-Track Cartridge") {
        addComboField("speed", "TAPE SPEED", {"3.75 IPS", "Other", "Unknown"}, currentInfo_.speed);
        addComboField("trackFormat", "TRACK FORMAT", {"8-Track", "Other", "Unknown"}, currentInfo_.trackFormat);
        addComboField("discType", "CARTRIDGE FORMAT", {"Standard 8-Track", "Quadraphonic", "Other", "Unknown"}, currentInfo_.discType);
    } else if (med == "DAT") {
        addComboField("recordingStandard", "RECORDING STANDARD", {"SP (Standard Play)", "LP (Long Play)", "Other", "Unknown"}, currentInfo_.recordingStandard);
        addComboField("recordingMode", "RECORDING MODE", {"Standard 2-Ch", "Wide Mode", "Other", "Unknown"}, currentInfo_.recordingMode);
    } else if (med == "ADAT") {
        addComboField("recordingStandard", "TAPE FORMAT", {"Type I (16-bit)", "Type II (20-bit)", "Other", "Unknown"}, currentInfo_.recordingStandard);
        addComboField("trackFormat", "TRACK CONFIGURATION", {"8-Track", "16-Track", "24-Track", "Other", "Unknown"}, currentInfo_.trackFormat);
        addComboField("referenceEq", "RECORDING STANDARD", {"ADAT Optical / S-VHS", "Other", "Unknown"}, currentInfo_.referenceEq);
    } else if (med == "DTRS / Hi8 Digital Audio Tape") {
        addComboField("recordingStandard", "TAPE FORMAT", {"DA-88 (16-bit)", "DA-78HR (24-bit)", "DA-98HR (24-bit)", "Other", "Unknown"}, currentInfo_.recordingStandard);
        addComboField("trackFormat", "TRACK CONFIGURATION", {"8-Track", "16-Track", "24-Track", "Other", "Unknown"}, currentInfo_.trackFormat);
        addComboField("referenceEq", "RECORDING STANDARD", {"DTRS", "Other", "Unknown"}, currentInfo_.referenceEq);
    } else if (med == "12\" Vinyl" || med == "10\" Vinyl" || med == "7\" Vinyl") {
        addComboField("speed", "SPEED", {"33 1/3 RPM", "45 RPM", "78 RPM", "Other", "Unknown"}, currentInfo_.speed);
        addComboField("discType", "DISC TYPE", {"LP", "EP", "Single", "Acetate", "Test Pressing", "Other", "Unknown"}, currentInfo_.discType);
        addComboField("referenceEq", "EQUALIZATION / REFERENCE", {"RIAA", "Pre-RIAA", "Columbia", "Decca", "NAB", "AES", "Other", "Unknown"}, currentInfo_.referenceEq);
    } else if (med == "Lacquer") {
        addComboField("speed", "SPEED", {"33 1/3 RPM", "45 RPM", "78 RPM", "Other", "Unknown"}, currentInfo_.speed);
        addComboField("discType", "DISC TYPE", {"Acetate / Master Disc", "Reference Cut", "Other", "Unknown"}, currentInfo_.discType);
        addComboField("referenceEq", "EQUALIZATION / REFERENCE", {"RIAA", "Pre-RIAA", "Columbia", "Decca", "NAB", "AES", "Other", "Unknown"}, currentInfo_.referenceEq);
    } else if (med == "Shellac") {
        addComboField("speed", "SPEED", {"78 RPM", "80 RPM", "Other", "Unknown"}, currentInfo_.speed);
        addComboField("discType", "DISC TYPE", {"10\" 78 RPM", "12\" 78 RPM", "Other", "Unknown"}, currentInfo_.discType);
        addComboField("referenceEq", "EQUALIZATION / REFERENCE", {"Pre-RIAA", "Columbia 78", "Victor 78", "Decca 78", "NAB", "Other", "Unknown"}, currentInfo_.referenceEq);
    } else if (med.find("Film") != std::string::npos && med.find("Still") == std::string::npos && med.find("120") == std::string::npos && med.find("Large") == std::string::npos && med.find("Instant") == std::string::npos && med.find("Plate") == std::string::npos && med.find("Print") == std::string::npos) {
        // Motion picture film
        addComboField("filmType", "FILM TYPE", {"Negative", "Positive", "Reversal", "Print", "Internegative", "Other", "Unknown"}, currentInfo_.filmType);
        addComboField("sound", "SOUND", {"Silent", "Optical Sound", "Magnetic Sound", "Sync Sound", "Other", "Unknown"}, currentInfo_.sound);
        addComboField("projectionSpeed", "PROJECTION REFERENCE", {"24 fps", "18 fps", "16 fps", "25 fps", "Other", "Unknown"}, currentInfo_.projectionSpeed);
    } else if (med == "VHS" || med == "VHS-C" || med == "S-VHS" || med == "Betamax" || med == "Betacam / Betacam SP" || med == "U-matic" || med == "1\" Type B / 1\" Type C" || med == "Video8" || med == "Hi8") {
        // Analog video tape
        addComboField("videoStandard", "VIDEO STANDARD", {"NTSC", "PAL", "SECAM", "Other", "Unknown"}, currentInfo_.videoStandard);
        addComboField("recordingFormat", "RECORDING FORMAT", {"SP", "LP", "EP / SLP", "Betacam SP", "U-matic SP", "Hi8 Analog", "Other", "Unknown"}, currentInfo_.recordingFormat);
        addComboField("audioConfig", "AUDIO CONFIGURATION", {"Hi-Fi Stereo", "Linear Mono", "AFM Stereo", "PCM Digital Audio", "Other", "Unknown"}, currentInfo_.audioConfig);
    } else if (med == "MiniDV" || med == "DV / DVCAM" || med == "DVCPRO / DVCPRO HD" || med == "HDV" || med == "Digital8" || med == "HDCAM / HDCAM SR" || med == "Digital Betacam") {
        // Digital video tape
        addComboField("videoStandard", "VIDEO STANDARD", {"NTSC", "PAL", "1080i", "720p", "1080/24p", "Other", "Unknown"}, currentInfo_.videoStandard);
        addComboField("recordingFormat", "RECORDING FORMAT", {"Standard DV (SP)", "DV (LP)", "DVCAM", "DVCPRO 25", "DVCPRO 50", "DVCPRO HD", "HDV 1080i", "HDCAM", "HDCAM SR", "DigiBeta", "Other", "Unknown"}, currentInfo_.recordingFormat);
        addComboField("audioConfig", "AUDIO CONFIGURATION", {"16-bit 48kHz (2-Ch)", "12-bit 32kHz (4-Ch)", "4-Ch / 12-Ch PCM", "MPEG-1 Layer II", "Other", "Unknown"}, currentInfo_.audioConfig);
    } else if (med == "35mm Film (Still)" || med == "120 / 220 Film" || med == "4x5 / 5x7 / 8x10 Large Format Film" || med == "Instant Film" || med == "Glass Plate / Glass Negative" || med == "Photographic Print") {
        // Still photographic medium
        addComboField("filmType", "PROCESS / TYPE", {"Negative", "Positive", "Reversal / Slide", "Print", "Gelatin Dry Plate", "Collodion Wet Plate", "Silver Gelatin", "Cyanotype", "C-Print", "Other", "Unknown"}, currentInfo_.filmType);
        addComboField("color", "COLOR", {"Color", "B&W", "Infrared", "Sepia / Hand-Colored", "Other", "Unknown"}, currentInfo_.color);
    } else if (med == "Physical Paper / Document" || med == "Microfilm / Microfiche") {
        addComboField("filmType", "DOCUMENT FORMAT", {"Original Document", "Manuscript", "Typed Paper", "Photocopy / Xerox", "Book / Bound Volume", "35mm Roll Microfilm", "16mm Roll Microfilm", "Microfiche Card", "Other", "Unknown"}, currentInfo_.filmType);
    } else if (med == "Other") {
        addTextField("customNote", "ORIGINAL MEDIUM DESCRIPTION", currentInfo_.customNote);
    }

    resized();
}

int OriginalSourceMediumEditorComponent::getPreferredHeight() const {
    return (1 + static_cast<int>(subfields_.size())) * 52;
}

void OriginalSourceMediumEditorComponent::paint(juce::Graphics& g) {
    juce::ignoreUnused(g);
}

void OriginalSourceMediumEditorComponent::resized() {
    auto area = getLocalBounds();
    if (area.isEmpty()) return;

    if (lblMedium_ && comboMedium_) {
        lblMedium_->setBounds(area.removeFromTop(16));
        area.removeFromTop(2);
        comboMedium_->setBounds(area.removeFromTop(26));
        area.removeFromTop(8);
    }

    for (auto& sf : subfields_) {
        if (sf.label) sf.label->setBounds(area.removeFromTop(16));
        area.removeFromTop(2);
        if (sf.combo) sf.combo->setBounds(area.removeFromTop(26));
        else if (sf.textEditor) sf.textEditor->setBounds(area.removeFromTop(26));
        area.removeFromTop(8);
    }
}

void OriginalSourceMediumEditorComponent::fireChange() {
    currentInfo_ = getValue();
    if (onChange) onChange();
}

} // namespace matriz::ui
