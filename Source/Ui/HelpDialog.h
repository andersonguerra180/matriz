#pragma once

#include <JuceHeader.h>
#include <vector>
#include <string>

namespace matriz::ui {

class HelpContentView : public juce::Component {
public:
    HelpContentView();
    ~HelpContentView() override = default;

    void setContent(const juce::String& title, const juce::String& category, const juce::String& rawBody);
    void paint(juce::Graphics& g) override;
    void resized() override;
    int getCalculatedHeight(int availableWidth) const;

private:
    juce::String title_;
    juce::String category_;
    juce::String rawBody_;

    struct SectionBlock {
        bool isHeader = false;
        bool isFaqQuestion = false;
        bool isFaqAnswer = false;
        bool isBullet = false;
        juce::String text;
    };
    std::vector<SectionBlock> blocks_;

    void parseContent();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpContentView)
};

class HelpDialog : public juce::Component, public juce::ListBoxModel {
public:
    HelpDialog();
    ~HelpDialog() override = default;

    static void exibirModal();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    // ListBoxModel interface
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

private:
    struct HelpTopic {
        juce::String id;
        juce::String titlePt;
        juce::String titleEn;
        juce::String categoryPt;
        juce::String categoryEn;
        juce::String contentPt;
        juce::String contentEn;
        juce::String keywords;
    };

    std::vector<HelpTopic> allTopics_;
    std::vector<int> filteredIndices_;

    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::TextEditor> searchEditor_;
    std::unique_ptr<juce::ListBox> topicList_;
    std::unique_ptr<juce::Viewport> contentViewport_;
    std::unique_ptr<HelpContentView> contentView_;
    std::unique_ptr<juce::TextButton> btnClose_;

    void inicializarTopicos();
    void aplicarFiltro();
    void atualizarVisualizacaoConteudo();

    std::function<void()> aoFechar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpDialog)
};

} // namespace matriz::ui
