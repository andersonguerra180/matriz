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

namespace matriz::ui {

struct OriginalSourceMediumInfo {
    std::string medium = "None / Unknown";
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
        return medium.empty() || medium == "None / Unknown" || medium == "None" || medium == "Unknown";
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
    static void populateMediumCombo(juce::ComboBox& combo, bool includeNone = true);
};

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

private:
    void rebuildSubfields();
    void layoutSubfields();
    void fireChange();

    bool compactMode_ = false;
    OriginalSourceMediumInfo currentInfo_;

    std::unique_ptr<juce::Label> lblMedium_;
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

} // namespace matriz::ui
