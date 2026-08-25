#include "AnalyticsExport.h"
#include "AnalyticsFormatter.h"
#include <sstream>
#include <fstream>

namespace matriz::analytics {

static std::string escapeCSV(const std::string& input) {
    if (input.find(',') != std::string::npos || input.find('"') != std::string::npos || input.find('\n') != std::string::npos) {
        std::string s = input;
        size_t pos = 0;
        while ((pos = s.find('"', pos)) != std::string::npos) {
            s.replace(pos, 1, "\"\"");
            pos += 2;
        }
        return "\"" + s + "\"";
    }
    return input;
}

bool AnalyticsExport::exportarCSV(const AnalyticsResult& result, const juce::File& file, bool formatValues) {
    std::ostringstream ss;

    // Header metadata
    ss << "# BKR Matriz Analytics Export\n";
    ss << "# Title," << escapeCSV(result.query.savedName.empty() ? "Multidimensional Analysis" : result.query.savedName) << "\n";
    ss << "# Measure," << escapeCSV(result.query.measure.name()) << "\n";
    ss << "# Dimension A," << escapeCSV(result.dimensionALabel) << "\n";
    if (!result.dimensionBLabel.empty()) {
        ss << "# Dimension B," << escapeCSV(result.dimensionBLabel) << "\n";
    }
    ss << "# Generated," << escapeCSV(result.executionTimestamp) << "\n\n";

    // Table Header
    ss << escapeCSV(result.dimensionALabel);
    if (!result.colKeys.empty()) {
        for (const auto& col : result.colKeys) {
            ss << "," << escapeCSV(col);
        }
    }
    ss << ",TOTAL\n";

    // Table Rows
    for (const auto& row : result.rowKeys) {
        ss << escapeCSV(row);
        if (!result.colKeys.empty()) {
            for (const auto& col : result.colKeys) {
                auto it = result.cells.find({row, col});
                double val = (it != result.cells.end()) ? it->second : 0.0;

                if (formatValues) {
                    ss << "," << escapeCSV(AnalyticsFormatter::formatValue(val, result.query.measure.field, result.query.measure.aggregation));
                } else {
                    ss << "," << val;
                }
            }
        }
        auto rIt = result.rowTotals.find(row);
        double rTot = (rIt != result.rowTotals.end()) ? rIt->second : 0.0;
        if (formatValues) {
            ss << "," << escapeCSV(AnalyticsFormatter::formatValue(rTot, result.query.measure.field, result.query.measure.aggregation));
        } else {
            ss << "," << rTot;
        }
        ss << "\n";
    }

    // Table Column Totals
    ss << "TOTAL";
    if (!result.colKeys.empty()) {
        for (const auto& col : result.colKeys) {
            auto cIt = result.colTotals.find(col);
            double cTot = (cIt != result.colTotals.end()) ? cIt->second : 0.0;
            if (formatValues) {
                ss << "," << escapeCSV(AnalyticsFormatter::formatValue(cTot, result.query.measure.field, result.query.measure.aggregation));
            } else {
                ss << "," << cTot;
            }
        }
    }
    if (formatValues) {
        ss << "," << escapeCSV(AnalyticsFormatter::formatValue(result.grandTotal, result.query.measure.field, result.query.measure.aggregation));
    } else {
        ss << "," << result.grandTotal;
    }
    ss << "\n";

    // 1D Descriptive Stats block if present
    if (result.stats1D.hasNumericStats) {
        ss << "\n# 1D Summary Statistics\n";
        ss << "# Stat,Value\n";
        ss << "# Count," << result.stats1D.count << "\n";
        ss << "# Total," << escapeCSV(AnalyticsFormatter::formatValue(result.stats1D.sum, result.query.measure.field, AggregationType::Sum)) << "\n";
        ss << "# Mean," << escapeCSV(AnalyticsFormatter::formatValue(result.stats1D.mean, result.query.measure.field, AggregationType::Avg)) << "\n";
        ss << "# Median," << escapeCSV(AnalyticsFormatter::formatValue(result.stats1D.median, result.query.measure.field, AggregationType::Median)) << "\n";
        ss << "# Min," << escapeCSV(AnalyticsFormatter::formatValue(result.stats1D.min, result.query.measure.field, AggregationType::Min)) << "\n";
        ss << "# Max," << escapeCSV(AnalyticsFormatter::formatValue(result.stats1D.max, result.query.measure.field, AggregationType::Max)) << "\n";
        ss << "# StdDev (Sample)," << escapeCSV(AnalyticsFormatter::formatValue(result.stats1D.stddevSamp, result.query.measure.field, AggregationType::StdDevSamp)) << "\n";
    }

    return file.replaceWithText(ss.str());
}

bool AnalyticsExport::exportarPDF(const AnalyticsResult& result, const juce::File& file) {
    // Generate text/graphics report file
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                       BKR MATRIZ - ANALYTICS REPORT                            \n";
    ss << "================================================================================\n\n";

    ss << "Title:       " << (result.query.savedName.empty() ? "Multidimensional Analysis" : result.query.savedName) << "\n";
    ss << "Measure:     " << result.query.measure.name() << "\n";
    ss << "Dimension A: " << result.dimensionALabel << "\n";
    if (!result.dimensionBLabel.empty()) {
        ss << "Dimension B: " << result.dimensionBLabel << "\n";
    }
    ss << "Generated:   " << result.executionTimestamp << "\n";
    ss << "Exec Time:   " << std::fixed << std::setprecision(1) << result.executionTimeMs << " ms\n\n";

    ss << "--------------------------------------------------------------------------------\n";
    ss << "ANALYSIS RESULTS\n";
    ss << "--------------------------------------------------------------------------------\n";

    for (const auto& row : result.rowKeys) {
        auto rIt = result.rowTotals.find(row);
        double rTot = (rIt != result.rowTotals.end()) ? rIt->second : 0.0;
        double pct = (result.grandTotal > 0.0) ? (rTot * 100.0 / result.grandTotal) : 0.0;

        ss << std::left << std::setw(30) << row << " : "
           << std::right << std::setw(15) << AnalyticsFormatter::formatValue(rTot, result.query.measure.field, result.query.measure.aggregation)
           << "  (" << std::fixed << std::setprecision(1) << pct << "%)\n";
    }
    ss << "--------------------------------------------------------------------------------\n";
    ss << std::left << std::setw(30) << "TOTAL" << " : "
       << std::right << std::setw(15) << AnalyticsFormatter::formatValue(result.grandTotal, result.query.measure.field, result.query.measure.aggregation)
       << "  (100.0%)\n";

    if (result.stats1D.hasNumericStats) {
        ss << "\n--------------------------------------------------------------------------------\n";
        ss << "SUMMARY STATISTICS (1D)\n";
        ss << "--------------------------------------------------------------------------------\n";
        ss << "COUNT       : " << result.stats1D.count << "\n";
        ss << "TOTAL       : " << AnalyticsFormatter::formatValue(result.stats1D.sum, result.query.measure.field, AggregationType::Sum) << "\n";
        ss << "MEAN        : " << AnalyticsFormatter::formatValue(result.stats1D.mean, result.query.measure.field, AggregationType::Avg) << "\n";
        ss << "MEDIAN      : " << AnalyticsFormatter::formatValue(result.stats1D.median, result.query.measure.field, AggregationType::Median) << "\n";
        ss << "MIN         : " << AnalyticsFormatter::formatValue(result.stats1D.min, result.query.measure.field, AggregationType::Min) << "\n";
        ss << "MAX         : " << AnalyticsFormatter::formatValue(result.stats1D.max, result.query.measure.field, AggregationType::Max) << "\n";
        ss << "STDDEV (S)  : " << AnalyticsFormatter::formatValue(result.stats1D.stddevSamp, result.query.measure.field, AggregationType::StdDevSamp) << "\n";
    }

    return file.replaceWithText(ss.str());
}

} // namespace matriz::analytics
