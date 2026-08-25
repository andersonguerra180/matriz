#pragma once

#include <JuceHeader.h>
#include "AnalyticsTypes.h"
#include <string>

namespace matriz::analytics {

class AnalyticsFormatter {
public:
    static std::string formatValue(double rawValue, MeasureField field, AggregationType agg, bool asPercentage = false);
    static std::string formatDimensionLabel(DimensionType dim);
    static std::string formatTimeGranularityLabel(TimeGranularity gran);
    static std::string formatMeasureLabel(const Measure& m);
};

} // namespace matriz::analytics
