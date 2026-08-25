#include "AnalyticsEngine.h"
#include "AnalyticsFormatter.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace matriz::analytics {

std::string AnalyticsEngine::buildDimensionExpression(DimensionType dim, TimeGranularity gran, const std::string& tableAlias) {
    switch (dim) {
        case DimensionType::None:
            return "'TOTAL'";
        case DimensionType::MediaType:
            return "COALESCE(" + tableAlias + ".tipo_midia, 'Sem Classificação')";
        case DimensionType::Extension:
            return "COALESCE(LOWER(SUBSTR(a.caminho_relativo, INSTR(a.caminho_relativo, '.'))), 'sem_extensao')";
        case DimensionType::Collection:
            return "COALESCE(ap.nome, 'Sem Coleção')";
        case DimensionType::Year: {
            if (gran == TimeGranularity::YearMonth) {
                return "COALESCE(STRFTIME('%Y-%m', " + tableAlias + ".criado_em), 'Sem Data')";
            } else if (gran == TimeGranularity::Month) {
                return "COALESCE(STRFTIME('%m', " + tableAlias + ".criado_em), 'Sem Data')";
            } else if (gran == TimeGranularity::Day) {
                return "COALESCE(STRFTIME('%Y-%m-%d', " + tableAlias + ".criado_em), 'Sem Data')";
            }
            return "COALESCE(CAST((SELECT valor FROM item_campo WHERE item_id = " + tableAlias + ".id AND campo_id = 'ano' LIMIT 1) AS TEXT), 'Sem Ano')";
        }
        case DimensionType::Codec:
            return "COALESCE(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.codec'), 'Desconhecido')";
        case DimensionType::SampleRate:
            return "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.sample_rate') AS TEXT), 'N/A')";
        case DimensionType::BitDepth:
            return "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.bit_depth') AS TEXT), 'N/A')";
        case DimensionType::Content:
            return "COALESCE((SELECT valor FROM item_campo WHERE item_id = " + tableAlias + ".id AND campo_id = 'content_type' LIMIT 1), 'Geral')";
        case DimensionType::IsrcStatus:
            return "CASE WHEN EXISTS (SELECT 1 FROM item_campo WHERE item_id = " + tableAlias + ".id AND campo_id = 'isrc' AND TRIM(IFNULL(valor, '')) <> '') THEN 'Com ISRC' ELSE 'Sem ISRC' END";
        case DimensionType::PreservationStatus:
            return "COALESCE(aps.fixity_status, 'UNKNOWN')";
        case DimensionType::MetadataStatus:
            return "CASE WHEN EXISTS (SELECT 1 FROM item_campo WHERE item_id = " + tableAlias + ".id) THEN 'Com Metadata' ELSE 'Sem Metadata' END";
        case DimensionType::AssetOrigin:
            return "COALESCE((SELECT valor FROM item_campo WHERE item_id = " + tableAlias + ".id AND campo_id = 'origem' LIMIT 1), 'Digital')";
        case DimensionType::IngestionDate: {
            if (gran == TimeGranularity::YearMonth) {
                return "STRFTIME('%Y-%m', " + tableAlias + ".criado_em)";
            } else if (gran == TimeGranularity::Month) {
                return "STRFTIME('%m', " + tableAlias + ".criado_em)";
            } else if (gran == TimeGranularity::Day) {
                return "STRFTIME('%Y-%m-%d', " + tableAlias + ".criado_em)";
            }
            return "STRFTIME('%Y', " + tableAlias + ".criado_em)";
        }
        case DimensionType::Folder:
            return "COALESCE(ap.nome, 'Não organizado')";
        case DimensionType::Tag:
            return "COALESCE(it_sub.tag, 'Sem Tag')";
        case DimensionType::AssetState:
            return tableAlias + ".estado";
        case DimensionType::HasError:
            return "CASE WHEN EXISTS (SELECT 1 FROM arquivo a_err WHERE a_err.item_id = " + tableAlias + ".id AND a_err.estado_presenca = 'corrompido') THEN 'Com Erro' ELSE 'Sem Erro' END";
        case DimensionType::HasThumbnail:
            return "CASE WHEN EXISTS (SELECT 1 FROM cache_arquivo ca JOIN arquivo a_t ON a_t.id = ca.arquivo_id WHERE a_t.item_id = " + tableAlias + ".id AND ca.miniatura IS NOT NULL) THEN 'Com Thumbnail' ELSE 'Sem Thumbnail' END";
        case DimensionType::HasSidecar:
        case DimensionType::HasXmp:
            return "CASE WHEN EXISTS (SELECT 1 FROM arquivo a_xmp WHERE a_xmp.item_id = " + tableAlias + ".id AND LOWER(a_xmp.caminho_relativo) LIKE '%.xmp') THEN 'Com XMP/Sidecar' ELSE 'Sem XMP/Sidecar' END";
        case DimensionType::HasMetadata:
            return "CASE WHEN EXISTS (SELECT 1 FROM item_campo WHERE item_id = " + tableAlias + ".id) THEN 'Com Metadata' ELSE 'Sem Metadata' END";
        case DimensionType::IsDuplicate:
            return "CASE WHEN " + tableAlias + ".estado = 'duplicata' THEN 'Duplicado' ELSE 'Único' END";
        case DimensionType::HasHash:
            return "CASE WHEN EXISTS (SELECT 1 FROM arquivo a_h WHERE a_h.item_id = " + tableAlias + ".id AND a_h.checksum_sha256 IS NOT NULL) THEN 'Com Hash' ELSE 'Sem Hash' END";
        case DimensionType::IsPreserved:
            return "CASE WHEN EXISTS (SELECT 1 FROM item_publicacao ip JOIN publicacao p ON p.id = ip.publicacao_id WHERE ip.item_id = " + tableAlias + ".id AND p.status = 'concluida') THEN 'Preservado' ELSE 'Não Preservado' END";
        case DimensionType::IsParcialPreserved:
            return "CASE WHEN EXISTS (SELECT 1 FROM consolidacao_registro cr WHERE cr.item_id = " + tableAlias + ".id) THEN 'Preservação Parcial' ELSE 'Sem Preservação Parcial' END";
    }
    return "'TOTAL'";
}

std::string AnalyticsEngine::buildMeasureExpression(const Measure& measure, const std::string& tableAlias) {
    if (measure.field == MeasureField::AssetCount) {
        return "COUNT(DISTINCT " + tableAlias + ".id)";
    }

    std::string valExpr;
    switch (measure.field) {
        case MeasureField::FileSize:
            valExpr = "COALESCE(a.tamanho_bytes, 0)";
            break;
        case MeasureField::DurationMs:
            valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.duracao_ms') AS REAL), 0.0)";
            break;
        case MeasureField::SampleRate:
            valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.sample_rate') AS REAL), 0.0)";
            break;
        case MeasureField::BitDepth:
            valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.bit_depth') AS REAL), 0.0)";
            break;
        case MeasureField::Year:
            valExpr = "COALESCE(CAST((SELECT valor FROM item_campo WHERE item_id = " + tableAlias + ".id AND campo_id = 'ano' LIMIT 1) AS REAL), 0.0)";
            break;
        case MeasureField::MarkersCount:
            valExpr = "(SELECT COUNT(*) FROM marcador WHERE item_id = " + tableAlias + ".id)";
            break;
        case MeasureField::ObsCount:
            valExpr = "(SELECT COUNT(*) FROM item_observacao WHERE item_id = " + tableAlias + ".id)";
            break;
        default:
            valExpr = "1.0";
            break;
    }

    switch (measure.aggregation) {
        case AggregationType::Count:
            return "COUNT(" + valExpr + ")";
        case AggregationType::Sum:
            return "SUM(" + valExpr + ")";
        case AggregationType::Avg:
            return "AVG(" + valExpr + ")";
        case AggregationType::Min:
            return "MIN(" + valExpr + ")";
        case AggregationType::Max:
            return "MAX(" + valExpr + ")";
        case AggregationType::PercentOfTotal:
        case AggregationType::Median:
        case AggregationType::StdDevSamp:
        case AggregationType::StdDevPop:
            return "SUM(" + valExpr + ")";
    }
    return "COUNT(*)";
}

std::string AnalyticsEngine::buildWhereClause(const AnalyticsFilter& filter, std::vector<matriz::db::Value>& params) {
    std::vector<std::string> clauses;

    if (filter.mediaType.has_value() && !filter.mediaType->empty()) {
        clauses.push_back("i.tipo_midia = ?");
        params.push_back(matriz::db::Value::of(*filter.mediaType));
    }
    if (filter.collectionId.has_value() && !filter.collectionId->empty()) {
        clauses.push_back("EXISTS (SELECT 1 FROM acervo_item_pasta aip_f WHERE aip_f.item_id = i.id AND aip_f.pasta_id = ?)");
        params.push_back(matriz::db::Value::of(*filter.collectionId));
    }
    if (filter.yearMin.has_value()) {
        clauses.push_back("CAST((SELECT valor FROM item_campo WHERE item_id = i.id AND campo_id = 'ano' LIMIT 1) AS INTEGER) >= ?");
        params.push_back(matriz::db::Value::of(*filter.yearMin));
    }
    if (filter.yearMax.has_value()) {
        clauses.push_back("CAST((SELECT valor FROM item_campo WHERE item_id = i.id AND campo_id = 'ano' LIMIT 1) AS INTEGER) <= ?");
        params.push_back(matriz::db::Value::of(*filter.yearMax));
    }
    if (filter.onlyMissingMetadata.has_value() && *filter.onlyMissingMetadata) {
        clauses.push_back("NOT EXISTS (SELECT 1 FROM item_campo WHERE item_id = i.id)");
    }
    if (filter.onlyError.has_value() && *filter.onlyError) {
        clauses.push_back("EXISTS (SELECT 1 FROM arquivo a_err WHERE a_err.item_id = i.id AND a_err.estado_presenca = 'corrompido')");
    }
    if (filter.tag.has_value() && !filter.tag->empty()) {
        clauses.push_back("EXISTS (SELECT 1 FROM item_tag it_f WHERE it_f.item_id = i.id AND it_f.tag = ?)");
        params.push_back(matriz::db::Value::of(*filter.tag));
    }

    if (clauses.empty()) return "";
    std::string sql = " WHERE " + clauses[0];
    for (size_t k = 1; k < clauses.size(); ++k) {
        sql += " AND " + clauses[k];
    }
    return sql;
}

AnalyticsResult AnalyticsEngine::executarQuery(matriz::db::Database& db, const AnalyticsQuery& query) {
    auto startTime = std::chrono::high_resolution_clock::now();

    AnalyticsResult result;
    result.query = query;
    result.dimensionALabel = AnalyticsFormatter::formatDimensionLabel(query.dimensionA);
    result.dimensionBLabel = query.dimensionB.has_value() ? AnalyticsFormatter::formatDimensionLabel(*query.dimensionB) : "";

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timeSs;
    timeSs << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    result.executionTimestamp = timeSs.str();

    std::string dimAExpr = buildDimensionExpression(query.dimensionA, query.granularityA);
    bool hasDimB = query.dimensionB.has_value() && query.dimensionB != DimensionType::None;
    std::string dimBExpr = hasDimB ? buildDimensionExpression(*query.dimensionB, query.granularityB) : "'TOTAL'";

    // Determine FROM clause & CTE joins
    std::string fromClause = " FROM item i ";
    fromClause += " LEFT JOIN arquivo a ON a.item_id = i.id AND a.eh_master = 1 ";
    fromClause += " LEFT JOIN acervo_item_pasta aip ON aip.item_id = i.id ";
    fromClause += " LEFT JOIN acervo_pasta ap ON ap.id = aip.pasta_id ";
    fromClause += " LEFT JOIN asset_preservation_status aps ON aps.item_id = i.id ";

    if (query.dimensionA == DimensionType::Tag || (hasDimB && *query.dimensionB == DimensionType::Tag)) {
        fromClause += " LEFT JOIN (SELECT item_id, MIN(tag) as tag FROM item_tag GROUP BY item_id) it_sub ON it_sub.item_id = i.id ";
    }

    std::vector<matriz::db::Value> params;
    std::string whereClause = buildWhereClause(query.filters, params);

    // 1. Handle MEDIAN special SQL query computation
    if (query.measure.aggregation == AggregationType::Median) {
        std::string valExpr;
        switch (query.measure.field) {
            case MeasureField::FileSize: valExpr = "COALESCE(a.tamanho_bytes, 0)"; break;
            case MeasureField::DurationMs: valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.duracao_ms') AS REAL), 0.0)"; break;
            case MeasureField::SampleRate: valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.sample_rate') AS REAL), 0.0)"; break;
            case MeasureField::BitDepth: valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.bit_depth') AS REAL), 0.0)"; break;
            case MeasureField::Year: valExpr = "COALESCE(CAST((SELECT valor FROM item_campo WHERE item_id = i.id AND campo_id = 'ano' LIMIT 1) AS REAL), 0.0)"; break;
            default: valExpr = "1.0"; break;
        }

        std::string medianSql =
            "WITH OrderedData AS ("
            "  SELECT " + dimAExpr + " AS dimA, " + dimBExpr + " AS dimB, " + valExpr + " AS val, "
            "         ROW_NUMBER() OVER (PARTITION BY " + dimAExpr + ", " + dimBExpr + " ORDER BY " + valExpr + ") as row_num, "
            "         COUNT(*) OVER (PARTITION BY " + dimAExpr + ", " + dimBExpr + ") as total_count "
            + fromClause + whereClause +
            ") "
            "SELECT dimA, dimB, AVG(val) as median_val "
            "FROM OrderedData "
            "WHERE row_num IN ((total_count + 1) / 2, (total_count + 2) / 2) "
            "GROUP BY dimA, dimB";

        auto stmt = db.prepare(medianSql);
        for (size_t p = 0; p < params.size(); ++p) stmt.bind(static_cast<int>(p + 1), params[p]);

        std::map<std::pair<std::string, std::string>, double> rawCells;
        while (stmt.step()) {
            std::string dA = stmt.columnText(0);
            std::string dB = stmt.columnText(1);
            double med = stmt.columnReal(2);
            rawCells[{dA, dB}] = med;
        }

        // Aggregate Top N + Other logic
        std::map<std::string, double> dimASums;
        for (const auto& [key, val] : rawCells) {
            dimASums[key.first] += val;
        }

        std::vector<std::pair<std::string, double>> sortedA(dimASums.begin(), dimASums.end());
        std::sort(sortedA.begin(), sortedA.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

        std::set<std::string> topAKeys;
        int limit = (query.topN > 0) ? std::min(query.topN, static_cast<int>(sortedA.size())) : static_cast<int>(sortedA.size());
        for (int i = 0; i < limit; ++i) topAKeys.insert(sortedA[i].first);

        bool hasOther = sortedA.size() > topAKeys.size();
        result.hasOtherCategory = hasOther;

        std::set<std::string> allCols;
        for (const auto& [key, val] : rawCells) {
            std::string rKey = topAKeys.count(key.first) ? key.first : "Other";
            std::string cKey = key.second;
            if (hasDimB) allCols.insert(cKey);
            result.cells[{rKey, cKey}] += val;
            result.rowTotals[rKey] += val;
            if (hasDimB) result.colTotals[cKey] += val;
            result.grandTotal += val;
        }

        for (const auto& pair : sortedA) {
            if (topAKeys.count(pair.first)) result.rowKeys.push_back(pair.first);
        }
        if (hasOther) result.rowKeys.push_back("Other");
        if (hasDimB) result.colKeys.assign(allCols.begin(), allCols.end());

        auto endTime = std::chrono::high_resolution_clock::now();
        result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        return result;
    }

    // 2. Handle STDDEV_SAMP / STDDEV_POP stable two-pass CTE calculation in SQL
    if (query.measure.aggregation == AggregationType::StdDevSamp || query.measure.aggregation == AggregationType::StdDevPop) {
        std::string valExpr;
        switch (query.measure.field) {
            case MeasureField::FileSize: valExpr = "COALESCE(a.tamanho_bytes, 0)"; break;
            case MeasureField::DurationMs: valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.duracao_ms') AS REAL), 0.0)"; break;
            case MeasureField::SampleRate: valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.sample_rate') AS REAL), 0.0)"; break;
            case MeasureField::BitDepth: valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.bit_depth') AS REAL), 0.0)"; break;
            case MeasureField::Year: valExpr = "COALESCE(CAST((SELECT valor FROM item_campo WHERE item_id = i.id AND campo_id = 'ano' LIMIT 1) AS REAL), 0.0)"; break;
            default: valExpr = "1.0"; break;
        }

        bool isSamp = (query.measure.aggregation == AggregationType::StdDevSamp);

        std::string stddevSql =
            "WITH GroupMeans AS ("
            "  SELECT " + dimAExpr + " AS dimA, " + dimBExpr + " AS dimB, AVG(" + valExpr + ") as mean_val, COUNT(*) as cnt "
            + fromClause + whereClause +
            "  GROUP BY " + dimAExpr + ", " + dimBExpr +
            "), RawData AS ("
            "  SELECT " + dimAExpr + " AS dimA, " + dimBExpr + " AS dimB, " + valExpr + " AS val "
            + fromClause + whereClause +
            ") "
            "SELECT r.dimA, r.dimB, m.cnt, "
            "       SUM((r.val - m.mean_val) * (r.val - m.mean_val)) as sum_sq_diff "
            "FROM RawData r "
            "JOIN GroupMeans m ON m.dimA = r.dimA AND m.dimB = r.dimB "
            "GROUP BY r.dimA, r.dimB";

        auto stmt = db.prepare(stddevSql);
        for (size_t p = 0; p < params.size(); ++p) stmt.bind(static_cast<int>(p + 1), params[p]);

        std::map<std::pair<std::string, std::string>, double> rawCells;
        while (stmt.step()) {
            std::string dA = stmt.columnText(0);
            std::string dB = stmt.columnText(1);
            long long cnt = stmt.columnInt(2);
            double sumSqDiff = stmt.columnReal(3);

            double variance = 0.0;
            if (isSamp) {
                variance = (cnt > 1) ? (sumSqDiff / static_cast<double>(cnt - 1)) : 0.0;
            } else {
                variance = (cnt > 0) ? (sumSqDiff / static_cast<double>(cnt)) : 0.0;
            }
            if (variance < 0.0) variance = 0.0; // Floating point precision safety clamp
            rawCells[{dA, dB}] = std::sqrt(variance);
        }

        std::map<std::string, double> dimASums;
        for (const auto& [key, val] : rawCells) dimASums[key.first] += val;

        std::vector<std::pair<std::string, double>> sortedA(dimASums.begin(), dimASums.end());
        std::sort(sortedA.begin(), sortedA.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

        std::set<std::string> topAKeys;
        int limit = (query.topN > 0) ? std::min(query.topN, static_cast<int>(sortedA.size())) : static_cast<int>(sortedA.size());
        for (int i = 0; i < limit; ++i) topAKeys.insert(sortedA[i].first);

        bool hasOther = sortedA.size() > topAKeys.size();
        result.hasOtherCategory = hasOther;

        std::set<std::string> allCols;
        for (const auto& [key, val] : rawCells) {
            std::string rKey = topAKeys.count(key.first) ? key.first : "Other";
            std::string cKey = key.second;
            if (hasDimB) allCols.insert(cKey);
            result.cells[{rKey, cKey}] += val;
            result.rowTotals[rKey] += val;
            if (hasDimB) result.colTotals[cKey] += val;
            result.grandTotal += val;
        }

        for (const auto& pair : sortedA) {
            if (topAKeys.count(pair.first)) result.rowKeys.push_back(pair.first);
        }
        if (hasOther) result.rowKeys.push_back("Other");
        if (hasDimB) result.colKeys.assign(allCols.begin(), allCols.end());

        auto endTime = std::chrono::high_resolution_clock::now();
        result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        return result;
    }

    // 3. Standard aggregations (COUNT, SUM, AVG, MIN, MAX)
    std::string measureExpr = buildMeasureExpression(query.measure);
    std::string sql = "SELECT " + dimAExpr + " AS dimA, " + dimBExpr + " AS dimB, " + measureExpr + " AS metric_val "
                      + fromClause + whereClause + " GROUP BY " + dimAExpr + ", " + dimBExpr;

    auto stmt = db.prepare(sql);
    for (size_t p = 0; p < params.size(); ++p) stmt.bind(static_cast<int>(p + 1), params[p]);

    std::map<std::pair<std::string, std::string>, double> rawCells;
    std::map<std::string, double> dimASums;

    while (stmt.step()) {
        std::string dA = stmt.columnText(0);
        std::string dB = stmt.columnText(1);
        double val = stmt.columnReal(2);

        rawCells[{dA, dB}] = val;
        dimASums[dA] += val;
    }

    // Sort Dimension A by metric sum for Top N + Other determination
    std::vector<std::pair<std::string, double>> sortedA(dimASums.begin(), dimASums.end());
    std::sort(sortedA.begin(), sortedA.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::set<std::string> topAKeys;
    int limit = (query.topN > 0) ? std::min(query.topN, static_cast<int>(sortedA.size())) : static_cast<int>(sortedA.size());
    for (int i = 0; i < limit; ++i) {
        topAKeys.insert(sortedA[i].first);
    }

    bool hasOther = sortedA.size() > topAKeys.size();
    result.hasOtherCategory = hasOther;

    std::set<std::string> allCols;

    for (const auto& [key, val] : rawCells) {
        std::string rKey = topAKeys.count(key.first) ? key.first : "Other";
        std::string cKey = key.second;

        if (hasDimB) allCols.insert(cKey);

        result.cells[{rKey, cKey}] += val;
        result.rowTotals[rKey] += val;
        if (hasDimB) result.colTotals[cKey] += val;
        result.grandTotal += val;
    }

    for (const auto& pair : sortedA) {
        if (topAKeys.count(pair.first)) {
            result.rowKeys.push_back(pair.first);
        }
    }
    if (hasOther) {
        result.rowKeys.push_back("Other");
    }

    if (hasDimB) {
        result.colKeys.assign(allCols.begin(), allCols.end());
    }

    // Compute 1D Descriptive Statistics when Dimension B is None and field is numeric
    if (!hasDimB && query.measure.field != MeasureField::AssetCount) {
        result.stats1D.hasNumericStats = true;

        std::string valExpr;
        switch (query.measure.field) {
            case MeasureField::FileSize: valExpr = "COALESCE(a.tamanho_bytes, 0)"; break;
            case MeasureField::DurationMs: valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.duracao_ms') AS REAL), 0.0)"; break;
            case MeasureField::SampleRate: valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.sample_rate') AS REAL), 0.0)"; break;
            case MeasureField::BitDepth: valExpr = "COALESCE(CAST(JSON_EXTRACT(a.caracteristicas_tecnicas_json, '$.bit_depth') AS REAL), 0.0)"; break;
            case MeasureField::Year: valExpr = "COALESCE(CAST((SELECT valor FROM item_campo WHERE item_id = i.id AND campo_id = 'ano' LIMIT 1) AS REAL), 0.0)"; break;
            default: valExpr = "1.0"; break;
        }

        std::string statsSql = "SELECT COUNT(" + valExpr + "), SUM(" + valExpr + "), AVG(" + valExpr + "), MIN(" + valExpr + "), MAX(" + valExpr + ") "
                               + fromClause + whereClause;

        auto stStats = db.prepare(statsSql);
        for (size_t p = 0; p < params.size(); ++p) stStats.bind(static_cast<int>(p + 1), params[p]);

        if (stStats.step()) {
            result.stats1D.count = static_cast<int>(stStats.columnInt(0));
            result.stats1D.sum = stStats.columnReal(1);
            result.stats1D.mean = stStats.columnReal(2);
            result.stats1D.min = stStats.columnReal(3);
            result.stats1D.max = stStats.columnReal(4);
        }

        // Median 1D
        if (result.stats1D.count > 0) {
            std::string med1dSql =
                "WITH OrderedData AS ("
                "  SELECT " + valExpr + " AS val, "
                "         ROW_NUMBER() OVER (ORDER BY " + valExpr + ") as row_num, "
                "         COUNT(*) OVER () as total_count "
                + fromClause + whereClause +
                ") "
                "SELECT AVG(val) FROM OrderedData "
                "WHERE row_num IN ((total_count + 1) / 2, (total_count + 2) / 2)";

            auto stMed = db.prepare(med1dSql);
            for (size_t p = 0; p < params.size(); ++p) stMed.bind(static_cast<int>(p + 1), params[p]);
            if (stMed.step()) result.stats1D.median = stMed.columnReal(0);
        }

        // StdDev 1D
        if (result.stats1D.count > 1) {
            std::string std1dSql =
                "WITH MeanVal AS (SELECT AVG(" + valExpr + ") as mean_val " + fromClause + whereClause + ") "
                "SELECT SUM((val - mean_val) * (val - mean_val)) "
                "FROM (SELECT " + valExpr + " AS val " + fromClause + whereClause + "), MeanVal";

            auto stStd = db.prepare(std1dSql);
            for (size_t p = 0; p < params.size(); ++p) stStd.bind(static_cast<int>(p + 1), params[p]);
            if (stStd.step()) {
                double sumSqDiff = stStd.columnReal(0);
                double varSamp = std::max(0.0, sumSqDiff / static_cast<double>(result.stats1D.count - 1));
                double varPop = std::max(0.0, sumSqDiff / static_cast<double>(result.stats1D.count));
                result.stats1D.stddevSamp = std::sqrt(varSamp);
                result.stats1D.stddevPop = std::sqrt(varPop);
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

} // namespace matriz::analytics
