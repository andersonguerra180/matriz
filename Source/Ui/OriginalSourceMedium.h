#pragma once

// ==============================================================================
// ORIGINAL SOURCE MEDIUM
// Identifies what physical or original medium the material came from.
// STRICT APP-WIDE RULE: 100% ENGLISH UI. ZERO PORTUGUESE TEXT IN USER INTERFACE.
// ==============================================================================

#include <JuceHeader.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace matriz::db { class Database; }

namespace matriz::ui {

// Valor de ORIGINAL SOURCE MEDIUM gravado em tudo que entra pelo fluxo PHONE DUMP do Intake
// (QR Code/USB e WhatsApp) e usado pelo filtro PHONE DUMP.
inline constexpr const char* kMediumPhoneDump = "Phone Dump";

struct OriginalSourceMediumInfo {
    std::string medium = "None / Unknown";
    std::string recordingDevice;
    std::string speed;
    std::string trackFormat;
    std::string referenceEq;
    std::string tapeFormulation;
    std::string tapeType;
    std::string noiseReduction;
    std::string discType;
    std::string recordingStandard;
    std::string recordingMode;
    std::string filmType;
    std::string sound;
    std::string gaugeFormat;
    std::string projectionSpeed;
    std::string videoStandard;
    std::string recordingFormat;
    std::string audioConfig;
    std::string color;
    std::string customNote;

    bool isNoneOrUnknown() const {
        return (medium.empty() || medium == "None / Unknown" || medium == "None" || medium == "Unknown")
               && recordingDevice.empty();
    }

    bool isNativeDigital() const {
        return medium == "Native Digital";
    }

    std::string toDisplaySummary() const;
    std::string serialize() const;
    static OriginalSourceMediumInfo deserialize(const std::string& str);
};

struct MediumCategoryGroup {
    juce::String groupName;
    std::vector<juce::String> mediums;
};

class OriginalSourceMediumVocabulary {
public:
    static const std::vector<MediumCategoryGroup>& getMediumCategories();
    static juce::String translateMedium(const juce::String& name, bool isPt);
    static std::string canonicalMedium(const std::string& name);
    // rawSourceMedium = o valor gravado (JSON do Original Source Medium ou texto legado).
    static bool isPhoneDump(const std::string& rawSourceMedium);
    // Filtro PHONE DUMP: medium = Phone Dump OU registro explícito de importação pelo fluxo PHONE DUMP.
    static bool itemIsPhoneDump(const std::string& rawSourceMedium, const std::string& containerOrigin,
                                const std::string& collectionType);
#if JUCE_MODULE_AVAILABLE_juce_gui_basics
    static void populateMediumCombo(juce::ComboBox& combo, bool includeNone = true);
#endif
};

// Grava ORIGINAL SOURCE MEDIUM = Phone Dump nos itens que entraram pelo fluxo PHONE DUMP do Intake
// (QR Code/USB, WhatsApp). Mantém o resto do medium (ex.: DEVICE lido do EXIF). Silencioso como um
// backfill: não marca metadados_editados, não entra no Undo e não dispara evento. Quem chama cuida da
// transação e do writeMutex; roda em qualquer thread.
void gravarOrigemPhoneDump(matriz::db::Database& registro, const std::vector<std::string>& itemIds);

#if JUCE_MODULE_AVAILABLE_juce_gui_basics
class OriginalSourceMediumEditorComponent : public juce::Component {
public:
    OriginalSourceMediumEditorComponent();
    ~OriginalSourceMediumEditorComponent() override;

    void setValue(const OriginalSourceMediumInfo& info);
    OriginalSourceMediumInfo getValue() const;

    void setValueString(const std::string& raw);
    std::string getValueString() const;

    void setCompactMode(bool compact);
    int getPreferredHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onChange;

    // Fase 4 (autocomplete por projeto): quem monta este componente injeta
    // aqui o histórico de valores de DEVICE já usados no projeto. Vazio/nulo
    // = campo DEVICE fica um TextEditor comum, sem popup.
    std::function<std::vector<juce::String>()> provedorHistoricoDevice;

private:
    void rebuildSubfields();
    void layoutSubfields();
    void fireChange();

    bool compactMode_ = false;
    OriginalSourceMediumInfo currentInfo_;

    std::unique_ptr<juce::ComboBox> comboMedium_;

    struct Subfield {
        juce::String key;
        juce::String labelText;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::TextEditor> textEditor;
    };
    std::vector<Subfield> subfields_;

    void addComboField(const juce::String& key, const juce::String& label, const std::vector<juce::String>& options, const std::string& selectedVal);
    void addTextField(const juce::String& key, const juce::String& label, const std::string& currentVal);
};
#endif

} // namespace matriz::ui
