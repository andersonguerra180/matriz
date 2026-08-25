#pragma once

#include <JuceHeader.h>
#include "../Db/Database.h"
#include "AnalyticsTypes.h"

namespace matriz::analytics {

class AnalyticsEngine {
public:
    static AnalyticsResult executarQuery(matriz::db::Database& db, const AnalyticsQuery& query);

private:
    static std::string buildDimensionExpression(DimensionType dim, TimeGranularity gran, const std::string& tableAlias = "i");
    static std::string buildMeasureExpression(const Measure& measure, const std::string& tableAlias = "i");
    static std::string buildWhereClause(const AnalyticsFilter& filter, std::vector<matriz::db::Value>& params);
};

} // namespace matriz::analytics
