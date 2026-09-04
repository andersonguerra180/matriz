#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>
#include <string>

namespace matriz::ui {

class TagChipsEditor : public juce::Component {
public:
    TagChipsEditor();
    ~TagChipsEditor() override;


    void setTags(const std::vector<std::string>& tags);
    std::vector<std::string> getTags() const;
    void addTag(const juce::String& text);

    std::function<void()> aoMudar;
    std::function<void()> aoRedimensionar;

    int getPreferredHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;

    juce::TextEditor* getInputForTest() { return input_.get(); }

private:
    struct Chip {
        std::string tag;
        juce::String display;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> closeBounds;
    };

    class TagInput : public juce::TextEditor {
    public:
        std::function<void()> onCommit;
        std::function<void()> onBackspaceEmpty;

        bool keyPressed(const juce::KeyPress& key) override {
            if (key == juce::KeyPress::returnKey || key == juce::KeyPress::tabKey) {
                if (onCommit) onCommit();
                return true;
            }
            if (key == juce::KeyPress::backspaceKey && getText().isEmpty()) {
                if (onBackspaceEmpty) onBackspaceEmpty();
                return true;
            }
            return juce::TextEditor::keyPressed(key);
        }

        void focusGained(FocusChangeType) override {
            juce::TextEditor::focusGained(juce::Component::focusChangedDirectly);
        }
    };

    std::vector<Chip> chips_;
    std::unique_ptr<TagInput> input_;

    void commitText();
    void removeTag(int index);
    void removeLastTag();
    juce::String canonicalize(const juce::String& text) const;
    void layoutChips();
    void flashChip(int index);

    int flashIndex_ = -1;
    int flashCounter_ = 0;
    bool emLayout_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TagChipsEditor)
};

} // namespace matriz::ui
