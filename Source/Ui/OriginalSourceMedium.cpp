// ==============================================================================
// ORIGINAL SOURCE MEDIUM IMPLEMENTATION
// STRICT APP-WIDE RULE: 100% ENGLISH UI. ZERO PORTUGUESE TEXT IN USER INTERFACE.
// ==============================================================================

#include "OriginalSourceMedium.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

#if JUCE_MODULE_AVAILABLE_juce_gui_basics
#include "AutoCompleteTextEditor.h"
#endif

namespace matriz::ui {

static const std::vector<MediumCategoryGroup> kMediumVocab = {
    {
        "SOLID STATE & FLASH MEMORY (PHOTO / VIDEO / AUDIO)",
        {
            "SD Card",
            "microSD Card",
            "CF Card (CompactFlash)",
            "CFexpress Card",
            "CFast Card",
            "XQD Card",
            "SxS Card",
            "P2 Card",
            "Flash Drive / USB Drive",
            "External SSD / Portable SSD",
            "External Hard Drive (HDD)",
            "Internal Storage / Smartphone"
        }
    },
    {
        "AUDIO (ANALOG & DIGITAL TAPE / DISC)",
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
            "MiniDisc (MD)",
            "Audio CD / CD-R",
            "12\" Vinyl",
            "10\" Vinyl",
            "7\" Vinyl",
            "Lacquer",
            "Shellac"
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
        "OPTICAL DISC (AUDIO / VIDEO / DATA)",
        {
            "Audio CD / CD-R / CD-RW",
            "DVD / DVD-R / DVD-Video",
            "Blu-ray Disc (BD / BD-R)",
            "MiniDisc (MD)",
            "LaserDisc"
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
        "IMAGE (STILL FILM & PRINTS)",
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

static const std::pair<const char*, const char*> kMediumTranslations[] = {
    {"None / Unknown", "Nenhum / Desconhecido"},
    {"Native Digital", "Digital Nativo"},
    {"Other", "Outro"},
    // Solid State & Flash
    {"SD Card", "Cartão SD"},
    {"microSD Card", "Cartão microSD"},
    {"CF Card (CompactFlash)", "Cartão CF (CompactFlash)"},
    {"CFexpress Card", "Cartão CFexpress"},
    {"CFast Card", "Cartão CFast"},
    {"XQD Card", "Cartão XQD"},
    {"SxS Card", "Cartão SxS"},
    {"P2 Card", "Cartão P2"},
    {"Flash Drive / USB Drive", "Pendrive / Unidade USB"},
    {"External SSD / Portable SSD", "SSD Externo / Portátil"},
    {"External Hard Drive (HDD)", "Disco Rígido Externo (HDD)"},
    {"Internal Storage / Smartphone", "Armazenamento Interno / Celular"},
    // Audio Tape / Disc
    {"1/4\" Tape", "Fita 1/4\""},
    {"1/2\" Tape", "Fita 1/2\""},
    {"1\" Tape", "Fita 1\""},
    {"2\" Tape", "Fita 2\""},
    {"Cassette Tape", "Fita Cassete"},
    {"8-Track Cartridge", "Cartucho 8 Pistas"},
    {"DAT", "DAT (Fita de Áudio Digital)"},
    {"ADAT", "ADAT"},
    {"DTRS / Hi8 Digital Audio Tape", "Fita DTRS / Hi8 Digital Audio"},
    {"MiniDisc (MD)", "MiniDisc (MD)"},
    {"Audio CD / CD-R", "CD de Áudio / CD-R"},
    {"12\" Vinyl", "Disco de Vinil 12\""},
    {"10\" Vinyl", "Disco de Vinil 10\""},
    {"7\" Vinyl", "Disco de Vinil 7\" (Compacto)"},
    {"Lacquer", "Acetato / Disco Master"},
    {"Shellac", "Disco de Goma-Laca (78 RPM)"},
    // Video Tape
    {"VHS", "Fita VHS"},
    {"VHS-C", "Fita VHS-C"},
    {"S-VHS", "Fita S-VHS"},
    {"Betamax", "Fita Betamax"},
    {"Betacam / Betacam SP", "Fita Betacam / Betacam SP"},
    {"U-matic", "Fita U-matic"},
    {"1\" Type B / 1\" Type C", "Fita 1\" Tipo B / Tipo C"},
    {"Video8", "Fita Video8"},
    {"Hi8", "Fita Hi8"},
    {"MiniDV", "Fita MiniDV"},
    {"DV / DVCAM", "Fita DV / DVCAM"},
    {"DVCPRO / DVCPRO HD", "Fita DVCPRO / DVCPRO HD"},
    {"HDV", "Fita HDV"},
    {"Digital8", "Fita Digital8"},
    {"HDCAM / HDCAM SR", "Fita HDCAM / HDCAM SR"},
    {"Digital Betacam", "Fita Betacam Digital"},
    // Optical
    {"Audio CD / CD-R / CD-RW", "CD de Áudio / CD-R / CD-RW"},
    {"DVD / DVD-R / DVD-Video", "DVD / DVD-R / DVD-Vídeo"},
    {"Blu-ray Disc (BD / BD-R)", "Disco Blu-ray (BD / BD-R)"},
    {"LaserDisc", "LaserDisc"},
    // Film motion
    {"35mm Film", "Película 35mm"},
    {"16mm Film", "Película 16mm"},
    {"Super 16 Film", "Película Super 16"},
    {"8mm Film", "Película 8mm"},
    {"Super 8 Film", "Película Super 8"},
    // Still
    {"35mm Film (Still)", "Filme Fotográfico 35mm"},
    {"120 / 220 Film", "Filme Médio Formato 120 / 220"},
    {"4x5 / 5x7 / 8x10 Large Format Film", "Filme Grande Formato (4x5, 5x7, 8x10)"},
    {"Instant Film", "Filme Instantâneo (Polaroid)"},
    {"Glass Plate / Glass Negative", "Placa de Vidro / Negativo de Vidro"},
    {"Photographic Print", "Cópia Fotográfica / Foto em Papel"},
    // Document
    {"Physical Paper / Document", "Papel Físico / Documento"},
    {"Microfilm / Microfiche", "Microfilme / Microficha"}
};

static const std::pair<const char*, const char*> kGroupTranslations[] = {
    {"SOLID STATE & FLASH MEMORY (PHOTO / VIDEO / AUDIO)", "MEMÓRIA FLASH E ESTADO SÓLIDO (FOTO / VÍDEO / ÁUDIO)"},
    {"AUDIO (ANALOG & DIGITAL TAPE / DISC)", "ÁUDIO (FITA ANALÓGICA E DIGITAL / DISCO)"},
    {"VIDEO (ANALOG & DIGITAL TAPE)", "VÍDEO (FITA ANALÓGICA E DIGITAL)"},
    {"OPTICAL DISC (AUDIO / VIDEO / DATA)", "DISCO ÓPTICO (ÁUDIO / VÍDEO / DADOS)"},
    {"FILM (MOTION PICTURE)", "FILME (CINEMATOGRÁFICO)"},
    {"IMAGE (STILL FILM & PRINTS)", "IMAGEM (FILME FOTOGRÁFICO E CÓPIAS)"},
    {"DOCUMENT", "DOCUMENTO"},
    {"UNIVERSAL & BORN-DIGITAL", "UNIVERSAL E NATIVO DIGITAL"}
};

juce::String OriginalSourceMediumVocabulary::translateMedium(const juce::String& name, bool isPt) {
    if (!isPt) return name;
    for (const auto& p : kMediumTranslations) {
        if (name.equalsIgnoreCase(p.first)) {
            return juce::String::fromUTF8(p.second);
        }
    }
    return name;
}

std::string OriginalSourceMediumVocabulary::canonicalMedium(const std::string& name) {
    juce::String jName = juce::String::fromUTF8(name.c_str()).trim();
    for (const auto& p : kMediumTranslations) {
        if (jName.equalsIgnoreCase(p.first) || jName.equalsIgnoreCase(juce::String::fromUTF8(p.second))) {
            return p.first;
        }
    }
    return name;
}

static juce::String translateGroup(const juce::String& groupName, bool isPt) {
    if (!isPt) return groupName;
    for (const auto& p : kGroupTranslations) {
        if (groupName.equalsIgnoreCase(p.first)) {
            return juce::String::fromUTF8(p.second);
        }
    }
    return groupName;
}

static juce::String traduzirRotuloSubcampo(const juce::String& rotulo, bool isPt) {
    if (!isPt) return rotulo;
    if (rotulo == "TAPE SPEED") return juce::String::fromUTF8("VELOCIDADE DA FITA");
    if (rotulo == "TRACK FORMAT") return juce::String::fromUTF8("FORMATO DE PISTAS");
    if (rotulo == "REFERENCE / EQ") return juce::String::fromUTF8("REFERÊNCIA / EQUALIZAÇÃO");
    if (rotulo == "TAPE FORMULATION") return juce::String::fromUTF8("FORMULAÇÃO DA FITA");
    if (rotulo == "TAPE TYPE") return juce::String::fromUTF8("TIPO DE FITA");
    if (rotulo == "NOISE REDUCTION") return juce::String::fromUTF8("REDUÇÃO DE RUÍDO");
    if (rotulo == "CARTRIDGE FORMAT") return juce::String::fromUTF8("FORMATO DO CARTUCHO");
    if (rotulo == "RECORDING STANDARD") return juce::String::fromUTF8("PADRÃO DE GRAVAÇÃO");
    if (rotulo == "RECORDING MODE") return juce::String::fromUTF8("MODO DE GRAVAÇÃO");
    if (rotulo == "TRACK CONFIGURATION") return juce::String::fromUTF8("CONFIGURAÇÃO DE PISTAS");
    if (rotulo == "SPEED") return juce::String::fromUTF8("VELOCIDADE");
    if (rotulo == "DISC TYPE") return juce::String::fromUTF8("TIPO DE DISCO");
    if (rotulo == "EQUALIZATION / REFERENCE") return juce::String::fromUTF8("EQUALIZAÇÃO / REFERÊNCIA");
    if (rotulo == "FILM TYPE") return juce::String::fromUTF8("TIPO DE PELÍCULA");
    if (rotulo == "SOUND") return juce::String::fromUTF8("ÁUDIO / SOM");
    if (rotulo == "PROJECTION REFERENCE") return juce::String::fromUTF8("REFERÊNCIA DE PROJEÇÃO");
    if (rotulo == "VIDEO STANDARD") return juce::String::fromUTF8("PADRÃO DE VÍDEO");
    if (rotulo == "RECORDING FORMAT") return juce::String::fromUTF8("FORMATO DE GRAVAÇÃO");
    if (rotulo == "AUDIO CONFIGURATION") return juce::String::fromUTF8("CONFIGURAÇÃO DE ÁUDIO");
    if (rotulo == "PROCESS / TYPE") return juce::String::fromUTF8("PROCESSO / TIPO");
    if (rotulo == "COLOR") return juce::String::fromUTF8("COR");
    if (rotulo == "DOCUMENT FORMAT") return juce::String::fromUTF8("FORMATO DO DOCUMENTO");
    if (rotulo == "ORIGINAL MEDIUM DESCRIPTION") return juce::String::fromUTF8("DESCRIÇÃO DA MÍDIA DE ORIGEM");
    if (rotulo == "DEVICE") return juce::String::fromUTF8("DISPOSITIVO / EQUIPAMENTO");
    return rotulo;
}

static juce::String traduzirOpcaoSubcampo(const juce::String& op, bool isPt) {
    if (!isPt) return op;
    if (op == "Other") return juce::String::fromUTF8("Outro");
    if (op == "Unknown") return juce::String::fromUTF8("Desconhecido");
    if (op == "None") return juce::String::fromUTF8("Nenhum");
    if (op == "Silent") return juce::String::fromUTF8("Mudo");
    if (op == "Optical Sound") return juce::String::fromUTF8("Som Óptico");
    if (op == "Magnetic Sound") return juce::String::fromUTF8("Som Magnético");
    if (op == "Sync Sound") return juce::String::fromUTF8("Som Sincronizado");
    if (op == "Negative") return juce::String::fromUTF8("Negativo");
    if (op == "Positive") return juce::String::fromUTF8("Positivo");
    if (op == "Reversal") return juce::String::fromUTF8("Reversível (Cromo)");
    if (op == "Reversal / Slide") return juce::String::fromUTF8("Reversível / Diapositivo (Slide)");
    if (op == "Print") return juce::String::fromUTF8("Cópia em Papel / Filme");
    if (op == "Color") return juce::String::fromUTF8("Colorido");
    if (op == "B&W") return juce::String::fromUTF8("Preto e Branco (P&B)");
    if (op == "Original Document") return juce::String::fromUTF8("Documento Original");
    if (op == "Manuscript") return juce::String::fromUTF8("Manuscrito");
    if (op == "Typed Paper") return juce::String::fromUTF8("Papel Datilografado / Impresso");
    if (op == "Photocopy / Xerox") return juce::String::fromUTF8("Fotocópia / Xerox");
    if (op == "Book / Bound Volume") return juce::String::fromUTF8("Livro / Volume Encadernado");
    if (op == "35mm Roll Microfilm") return juce::String::fromUTF8("Rolo de Microfilme 35mm");
    if (op == "16mm Roll Microfilm") return juce::String::fromUTF8("Rolo de Microfilme 16mm");
    if (op == "Microfiche Card") return juce::String::fromUTF8("Cartão de Microficha");
    if (op == "Hi-Fi Stereo") return juce::String::fromUTF8("Hi-Fi Estéreo");
    if (op == "Linear Mono") return juce::String::fromUTF8("Mono Linear");
    if (op == "AFM Stereo") return juce::String::fromUTF8("AFM Estéreo");
    if (op == "PCM Digital Audio") return juce::String::fromUTF8("Áudio Digital PCM");
    return op;
}

const std::vector<MediumCategoryGroup>& OriginalSourceMediumVocabulary::getMediumCategories() {
    return kMediumVocab;
}

#if JUCE_MODULE_AVAILABLE_juce_gui_basics
void OriginalSourceMediumVocabulary::populateMediumCombo(juce::ComboBox& combo, bool includeNone) {
    combo.clear(juce::dontSendNotification);
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    int id = 1;
    if (includeNone) {
        combo.addItem(translateMedium("None / Unknown", isPt), id++);
        combo.addItem(translateMedium("Native Digital", isPt), id++);
        combo.addItem(translateMedium("Other", isPt), id++);
        combo.addSeparator();
    }
    for (const auto& grp : kMediumVocab) {
        combo.addSectionHeading(translateGroup(grp.groupName, isPt));
        for (const auto& med : grp.mediums) {
            if (includeNone && (med == "None / Unknown" || med == "Native Digital" || med == "Other"))
                continue;
            combo.addItem(translateMedium(med, isPt), id++);
        }
    }
}
#endif

std::string OriginalSourceMediumInfo::toDisplaySummary() const {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    if (isNoneOrUnknown()) {
        if (!recordingDevice.empty()) return isPt ? ("Nenhum / Desconhecido (Dispositivo: " + recordingDevice + ")") : ("None / Unknown (Device: " + recordingDevice + ")");
        return isPt ? "Nenhum / Desconhecido" : "None / Unknown";
    }
    if (isNativeDigital()) {
        if (!recordingDevice.empty()) return isPt ? ("Digital Nativo (Dispositivo: " + recordingDevice + ")") : ("Native Digital (Device: " + recordingDevice + ")");
        return isPt ? "Digital Nativo" : "Native Digital";
    }
    std::string medDisp = OriginalSourceMediumVocabulary::translateMedium(juce::String::fromUTF8(medium.c_str()), isPt).toStdString();
    if (medium == "Other" || medium == "Outro") {
        std::string s = customNote.empty() ? (isPt ? "Outro" : "Other") : ((isPt ? "Outro: " : "Other: ") + customNote);
        if (!recordingDevice.empty()) s += isPt ? (" (Dispositivo: " + recordingDevice + ")") : (" (Device: " + recordingDevice + ")");
        return s;
    }

    std::vector<std::string> parts;
    auto addIfVal = [&](const std::string& v) {
        if (!v.empty() && v != "Unknown" && v != "None" && v != "Desconhecido" && v != "Nenhum") {
            parts.push_back(traduzirOpcaoSubcampo(juce::String::fromUTF8(v.c_str()), isPt).toStdString());
        }
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
    if (!recordingDevice.empty()) parts.push_back((isPt ? "Dispositivo: " : "Device: ") + recordingDevice);

    if (parts.empty()) return medDisp;

    std::string summary = medDisp + " (";
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
    if (!recordingDevice.empty()) obj->setProperty("recordingDevice", juce::String::fromUTF8(recordingDevice.c_str()));
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
        if (obj->hasProperty("recordingDevice")) info.recordingDevice = obj->getProperty("recordingDevice").toString().toStdString();
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

#if JUCE_MODULE_AVAILABLE_juce_gui_basics
OriginalSourceMediumEditorComponent::OriginalSourceMediumEditorComponent() {
    comboMedium_ = std::make_unique<juce::ComboBox>();
    OriginalSourceMediumVocabulary::populateMediumCombo(*comboMedium_, true);
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    comboMedium_->setText(OriginalSourceMediumVocabulary::translateMedium("None / Unknown", isPt), juce::dontSendNotification);
    comboMedium_->onChange = [this] {
        currentInfo_.medium = OriginalSourceMediumVocabulary::canonicalMedium(comboMedium_->getText().toStdString());
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
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    std::string canon = OriginalSourceMediumVocabulary::canonicalMedium(currentInfo_.medium);
    int selId = 0;
    for (int i = 0; i < comboMedium_->getNumItems(); ++i) {
        std::string itemCanon = OriginalSourceMediumVocabulary::canonicalMedium(comboMedium_->getItemText(i).toStdString());
        if (itemCanon == canon) {
            selId = comboMedium_->getItemId(i);
            break;
        }
    }
    if (selId > 0) {
        comboMedium_->setSelectedId(selId, juce::dontSendNotification);
    } else {
        comboMedium_->setText(OriginalSourceMediumVocabulary::translateMedium(juce::String::fromUTF8(currentInfo_.medium.c_str()), isPt), juce::dontSendNotification);
    }
    rebuildSubfields();
}

OriginalSourceMediumInfo OriginalSourceMediumEditorComponent::getValue() const {
    OriginalSourceMediumInfo info = currentInfo_;
    info.medium = OriginalSourceMediumVocabulary::canonicalMedium(comboMedium_->getText().toStdString());
    for (const auto& sf : subfields_) {
        std::string val;
        if (sf.combo) {
            std::string raw = sf.combo->getText().toStdString();
            if (raw == "Desconhecido") val = "Unknown";
            else if (raw == "Nenhum") val = "None";
            else if (raw == "Outro") val = "Other";
            else val = raw;
        } else if (sf.textEditor) {
            val = sf.textEditor->getText().toStdString();
        }

        if (sf.key == "recordingDevice") info.recordingDevice = val;
        else if (sf.key == "speed") info.speed = val;
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
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    Subfield sf;
    sf.key = key;
    sf.labelText = label;

    sf.label = std::make_unique<juce::Label>("", traduzirRotuloSubcampo(label, isPt));
    sf.label->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    sf.label->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*sf.label);

    sf.combo = std::make_unique<juce::ComboBox>();
    int id = 1;
    for (const auto& opt : options) {
        sf.combo->addItem(traduzirOpcaoSubcampo(opt, isPt), id++);
    }
    if (!selectedVal.empty()) {
        juce::String sVal = juce::String::fromUTF8(selectedVal.c_str());
        sf.combo->setText(traduzirOpcaoSubcampo(sVal, isPt), juce::dontSendNotification);
    } else {
        sf.combo->setText(isPt ? juce::String::fromUTF8("Desconhecido") : juce::String("Unknown"), juce::dontSendNotification);
    }
    sf.combo->onChange = [this] { fireChange(); };
    addAndMakeVisible(*sf.combo);

    subfields_.push_back(std::move(sf));
}

void OriginalSourceMediumEditorComponent::addTextField(const juce::String& key, const juce::String& label, const std::string& currentVal) {
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    Subfield sf;
    sf.key = key;
    sf.labelText = label;

    sf.label = std::make_unique<juce::Label>("", traduzirRotuloSubcampo(label, isPt));
    sf.label->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    sf.label->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*sf.label);

    // Fase 4: só DEVICE tem histórico pra sugerir — os outros textFields
    // (tapeFormulation, customNote) continuam um TextEditor comum.
    if (key == "recordingDevice" && provedorHistoricoDevice) {
        sf.textEditor = std::make_unique<AutoCompleteTextEditor>(provedorHistoricoDevice);
    } else {
        sf.textEditor = std::make_unique<juce::TextEditor>();
    }
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

    // Always add DEVICE field
    addTextField("recordingDevice", "DEVICE", currentInfo_.recordingDevice);

    resized();
}

int OriginalSourceMediumEditorComponent::getPreferredHeight() const {
    return 34 + static_cast<int>(subfields_.size()) * 52;
}

void OriginalSourceMediumEditorComponent::paint(juce::Graphics& g) {
    juce::ignoreUnused(g);
}

void OriginalSourceMediumEditorComponent::resized() {
    auto area = getLocalBounds();
    if (area.isEmpty()) return;

    if (comboMedium_) {
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
#endif

} // namespace matriz::ui
