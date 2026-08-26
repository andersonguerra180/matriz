#include "SavedAnalyses.h"

namespace matriz::analytics {

void SavedAnalyses::garantirTabela(matriz::db::Database& db) {
    db.execScript(
        "CREATE TABLE IF NOT EXISTS analytics_saved_queries ("
        "  nome TEXT PRIMARY KEY, "
        "  measure_field INT NOT NULL, "
        "  measure_agg INT NOT NULL, "
        "  dimension_a INT NOT NULL, "
        "  granularity_a INT NOT NULL, "
        "  dimension_b INT NOT NULL, "
        "  granularity_b INT NOT NULL, "
        "  top_n INT NOT NULL DEFAULT 50, "
        "  criado_em TEXT NOT NULL"
        ");"
    );
}

bool SavedAnalyses::salvarAnalise(matriz::db::Database& db, const std::string& nome, const AnalyticsQuery& query) {
    garantirTabela(db);
    std::string sql =
        "INSERT OR REPLACE INTO analytics_saved_queries "
        "(nome, measure_field, measure_agg, dimension_a, granularity_a, dimension_b, granularity_b, top_n, criado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))";

    try {
        db.run(sql, {
            matriz::db::Value::of(nome),
            matriz::db::Value::of(static_cast<int>(query.measure.field)),
            matriz::db::Value::of(static_cast<int>(query.measure.aggregation)),
            matriz::db::Value::of(static_cast<int>(query.dimensionA)),
            matriz::db::Value::of(static_cast<int>(query.granularityA)),
            matriz::db::Value::of(static_cast<int>(query.dimensionB.value_or(DimensionType::None))),
            matriz::db::Value::of(static_cast<int>(query.granularityB)),
            matriz::db::Value::of(query.topN)
        });
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::pair<std::string, AnalyticsQuery>> SavedAnalyses::listarAnalises(matriz::db::Database& db) {
    garantirTabela(db);
    std::vector<std::pair<std::string, AnalyticsQuery>> list = obterPresetsPadrao();

    try {
        auto stmt = db.prepare("SELECT nome, measure_field, measure_agg, dimension_a, granularity_a, dimension_b, granularity_b, top_n FROM analytics_saved_queries ORDER BY nome");
        while (stmt.step()) {
            std::string nome = stmt.columnText(0);
            AnalyticsQuery q;
            q.measure.field = static_cast<MeasureField>(stmt.columnInt(1));
            q.measure.aggregation = static_cast<AggregationType>(stmt.columnInt(2));
            q.dimensionA = static_cast<DimensionType>(stmt.columnInt(3));
            q.granularityA = static_cast<TimeGranularity>(stmt.columnInt(4));
            
            int dimBVal = static_cast<int>(stmt.columnInt(5));
            if (dimBVal != static_cast<int>(DimensionType::None)) {
                q.dimensionB = static_cast<DimensionType>(dimBVal);
            }
            q.granularityB = static_cast<TimeGranularity>(stmt.columnInt(6));
            q.topN = static_cast<int>(stmt.columnInt(7));
            q.savedName = nome;

            list.push_back({nome, q});
        }
    } catch (...) {}

    return list;
}

bool SavedAnalyses::removerAnalise(matriz::db::Database& db, const std::string& nome) {
    garantirTabela(db);
    try {
        db.run("DELETE FROM analytics_saved_queries WHERE nome = ?", {matriz::db::Value::of(nome)});
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::pair<std::string, AnalyticsQuery>> SavedAnalyses::obterPresetsPadrao() {
    std::vector<std::pair<std::string, AnalyticsQuery>> presets;

    {
        AnalyticsQuery q;
        q.measure = {MeasureField::AssetCount, AggregationType::Count};
        q.dimensionA = DimensionType::MediaType;
        q.dimensionB = std::nullopt;
        q.savedName = "Count by Media Type";
        presets.push_back({q.savedName, q});
    }
    {
        AnalyticsQuery q;
        q.measure = {MeasureField::FileSize, AggregationType::Sum};
        q.dimensionA = DimensionType::Collection;
        q.dimensionB = DimensionType::MediaType;
        q.savedName = "Storage by Collection × Type";
        presets.push_back({q.savedName, q});
    }
    {
        AnalyticsQuery q;
        q.measure = {MeasureField::AssetCount, AggregationType::Count};
        q.dimensionA = DimensionType::MediaType;
        q.dimensionB = DimensionType::PreservationStatus;
        q.savedName = "Type × Preservation Status";
        presets.push_back({q.savedName, q});
    }
    {
        AnalyticsQuery q;
        q.measure = {MeasureField::DurationMs, AggregationType::Avg};
        q.dimensionA = DimensionType::MediaType;
        q.dimensionB = std::nullopt;
        q.savedName = "Average Duration by Type";
        presets.push_back({q.savedName, q});
    }
    {
        AnalyticsQuery q;
        q.measure = {MeasureField::AssetCount, AggregationType::Count};
        q.dimensionA = DimensionType::MediaType;
        q.dimensionB = DimensionType::MetadataStatus;
        q.savedName = "Files without Metadata by Type";
        presets.push_back({q.savedName, q});
    }

    return presets;
}

} // namespace matriz::analytics
