#pragma once

#include <JuceHeader.h>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace matriz::analytics {

enum class AggregationType {
    Count,
    Sum,
    Avg,
    Min,
    Max,
    Median,
    StdDevSamp,
    StdDevPop,
    PercentOfTotal
};

enum class MeasureField {
    AssetCount,
    FileSize,
    DurationMs,
    SampleRate,
    BitDepth,
    Year,
    MarkersCount,
    ObsCount
};

struct Measure {
    MeasureField field = MeasureField::AssetCount;
    AggregationType aggregation = AggregationType::Count;

    bool isValid() const {
        if (field == MeasureField::AssetCount) {
            return aggregation == AggregationType::Count || aggregation == AggregationType::PercentOfTotal;
        }
        return true;
    }

    std::string name() const {
        std::string aggStr;
        switch (aggregation) {
            case AggregationType::Count: aggStr = "COUNT"; break;
            case AggregationType::Sum: aggStr = "SUM"; break;
            case AggregationType::Avg: aggStr = "AVG"; break;
            case AggregationType::Min: aggStr = "MIN"; break;
            case AggregationType::Max: aggStr = "MAX"; break;
            case AggregationType::Median: aggStr = "MEDIAN"; break;
            case AggregationType::StdDevSamp: aggStr = "STDDEV_SAMP"; break;
            case AggregationType::StdDevPop: aggStr = "STDDEV_POP"; break;
            case AggregationType::PercentOfTotal: aggStr = "PERCENT_OF_TOTAL"; break;
        }

        std::string fieldStr;
        switch (field) {
            case MeasureField::AssetCount: fieldStr = "assets"; break;
            case MeasureField::FileSize: fieldStr = "file_size"; break;
            case MeasureField::DurationMs: fieldStr = "duration"; break;
            case MeasureField::SampleRate: fieldStr = "sample_rate"; break;
            case MeasureField::BitDepth: fieldStr = "bit_depth"; break;
            case MeasureField::Year: fieldStr = "year"; break;
            case MeasureField::MarkersCount: fieldStr = "markers"; break;
            case MeasureField::ObsCount: fieldStr = "observations"; break;
        }
        return aggStr + "(" + fieldStr + ")";
    }
};

enum class DimensionType {
    None,
    MediaType,
    Extension,
    Collection,
    Year,
    Codec,
    SampleRate,
    BitDepth,
    Content,
    IsrcStatus,
    PreservationStatus,
    MetadataStatus,
    AssetOrigin,
    IngestionDate,
    Folder,
    Tag,
    AssetState,
    HasError,
    HasThumbnail,
    HasSidecar,
    HasXmp,
    HasMetadata,
    IsDuplicate,
    HasHash,
    IsPreserved,
    IsParcialPreserved
};

enum class TimeGranularity {
    Year,
    YearMonth,
    Month,
    Day
};

struct AnalyticsFilter {
    std::optional<std::string> mediaType;
    std::optional<std::string> collectionId;
    std::optional<int> yearMin;
    std::optional<int> yearMax;
    std::optional<bool> onlyMissingMetadata;
    std::optional<bool> onlyError;
    std::optional<std::string> folderPath;
    std::optional<std::string> tag;
    std::optional<std::string> searchQuery;
};

struct AnalyticsQuery {
    Measure measure;
    DimensionType dimensionA = DimensionType::MediaType;
    TimeGranularity granularityA = TimeGranularity::Year;

    std::optional<DimensionType> dimensionB = std::nullopt;
    TimeGranularity granularityB = TimeGranularity::Year;

    AnalyticsFilter filters;

    int topN = 50; // High-cardinality limit for Dimension A (0 = no limit)
    bool showPercentOfTotal = false;

    std::string savedName;
};

struct DescriptiveStats1D {
    int count = 0;
    double sum = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double min = 0.0;
    double max = 0.0;
    double stddevSamp = 0.0;
    double stddevPop = 0.0;
    bool hasNumericStats = false;
};

struct AnalyticsResult {
    AnalyticsQuery query;

    std::string dimensionALabel;
    std::string dimensionBLabel;

    std::vector<std::string> rowKeys; // Distinct categories of Dimension A (including "Other" if truncated)
    std::vector<std::string> colKeys; // Distinct categories of Dimension B (empty if 1D)

    // Cell matrix: (rowKey, colKey) -> double value
    std::map<std::pair<std::string, std::string>, double> cells;

    std::map<std::string, double> rowTotals;
    std::map<std::string, double> colTotals;
    double grandTotal = 0.0;

    DescriptiveStats1D stats1D;

    bool hasOtherCategory = false;
    double executionTimeMs = 0.0;
    std::string executionTimestamp;
};

} // namespace matriz::analytics
