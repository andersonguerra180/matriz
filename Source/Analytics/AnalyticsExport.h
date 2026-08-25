#pragma once

#include <JuceHeader.h>
#include "AnalyticsTypes.h"

namespace matriz::analytics {

class AnalyticsExport {
public:
    static bool exportarCSV(const AnalyticsResult& result, const juce::File& file, bool formatValues = true);
    static bool exportarPDF(const AnalyticsResult& result, const juce::File& file);
};

} // namespace matriz::analytics
