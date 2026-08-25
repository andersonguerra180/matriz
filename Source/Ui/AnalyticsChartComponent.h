#pragma once

#include <JuceHeader.h>
#include "../Analytics/AnalyticsTypes.h"
#include "../Analytics/AnalyticsFormatter.h"

namespace matriz::ui {

class AnalyticsChartComponent : public juce::Component {
public:
    AnalyticsChartComponent();
    ~AnalyticsChartComponent() override = default;

    void updateResult(const matriz::analytics::AnalyticsResult& result);
    void paint(juce::Graphics& g) override;

private:
    matriz::analytics::AnalyticsResult result_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyticsChartComponent)
};

} // namespace matriz::ui
