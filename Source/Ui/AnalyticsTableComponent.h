#pragma once

#include <JuceHeader.h>
#include "../Analytics/AnalyticsTypes.h"
#include "../Analytics/AnalyticsFormatter.h"

namespace matriz::ui {

class AnalyticsTableComponent : public juce::Component, public juce::TableListBoxModel {
public:
    AnalyticsTableComponent();
    ~AnalyticsTableComponent() override = default;

    void updateResult(const matriz::analytics::AnalyticsResult& result);

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    matriz::analytics::AnalyticsResult result_;
    juce::TableListBox table_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyticsTableComponent)
};

} // namespace matriz::ui
