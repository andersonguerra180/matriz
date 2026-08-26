#include "AnalyticsFormatter.h"
#include <sstream>
#include <iomanip>

namespace matriz::analytics {

std::string AnalyticsFormatter::formatValue(double rawValue, MeasureField field, AggregationType agg, bool asPercentage) {
    if (asPercentage || agg == AggregationType::PercentOfTotal) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << rawValue << "%";
        return ss.str();
    }

    if (agg == AggregationType::Count || field == MeasureField::AssetCount || field == MeasureField::MarkersCount || field == MeasureField::ObsCount) {
        long long val = static_cast<long long>(rawValue + 0.5);
        juce::String s(val);
        // Format with thousands separator
        std::string res = s.toStdString();
        int insertPosition = static_cast<int>(res.length()) - 3;
        while (insertPosition > 0) {
            res.insert(insertPosition, ".");
            insertPosition -= 3;
        }
        return res;
    }

    if (field == MeasureField::FileSize) {
        return juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(rawValue)).toStdString();
    }

    if (field == MeasureField::DurationMs) {
        double seconds = rawValue / 1000.0;
        if (seconds < 0) seconds = 0;
        int totalSec = static_cast<int>(seconds);
        int hours = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;

        if (hours > 0) {
            std::ostringstream ss;
            ss << hours << "h " << std::setw(2) << std::setfill('0') << mins << "m " << std::setw(2) << std::setfill('0') << secs << "s";
            return ss.str();
        } else {
            std::ostringstream ss;
            ss << mins << "m " << std::setw(2) << std::setfill('0') << secs << "s";
            return ss.str();
        }
    }

    // Default numeric formatting with 2 decimals
    std::ostringstream ss;
    if (rawValue == static_cast<long long>(rawValue)) {
        ss << static_cast<long long>(rawValue);
    } else {
        ss << std::fixed << std::setprecision(2) << rawValue;
    }
    return ss.str();
}

std::string AnalyticsFormatter::formatDimensionLabel(DimensionType dim) {
    switch (dim) {
        case DimensionType::None: return "None";
        case DimensionType::MediaType: return "Media Type";
        case DimensionType::Extension: return "Extension";
        case DimensionType::Collection: return "Collection";
        case DimensionType::Year: return "Year";
        case DimensionType::Codec: return "Codec";
        case DimensionType::SampleRate: return "Sample Rate";
        case DimensionType::BitDepth: return "Bit Depth";
        case DimensionType::Content: return "Content";
        case DimensionType::IsrcStatus: return "Status ISRC";
        case DimensionType::PreservationStatus: return "Preservation Status";
        case DimensionType::MetadataStatus: return "Metadata Status";
        case DimensionType::AssetOrigin: return "Asset Origin";
        case DimensionType::IngestionDate: return "Ingestion Date";
        case DimensionType::Folder: return "Archive Folder";
        case DimensionType::Tag: return "Tag";
        case DimensionType::AssetState: return "Asset State";
        case DimensionType::HasError: return "Error Status";
        case DimensionType::HasThumbnail: return "Has Thumbnail";
        case DimensionType::HasSidecar: return "Has Sidecar";
        case DimensionType::HasXmp: return "Has XMP";
        case DimensionType::HasMetadata: return "Has Metadata";
        case DimensionType::IsDuplicate: return "Duplicate";
        case DimensionType::HasHash: return "Has SHA-256 Hash";
        case DimensionType::IsPreserved: return "Preserved";
        case DimensionType::IsParcialPreserved: return "Partial Preservation";
    }
    return "Unknown";
}

std::string AnalyticsFormatter::formatTimeGranularityLabel(TimeGranularity gran) {
    switch (gran) {
        case TimeGranularity::Year: return "Year (YYYY)";
        case TimeGranularity::YearMonth: return "Month/Year (YYYY-MM)";
        case TimeGranularity::Month: return "Month (MM)";
        case TimeGranularity::Day: return "Day (YYYY-MM-DD)";
    }
    return "";
}

std::string AnalyticsFormatter::formatMeasureLabel(const Measure& m) {
    return m.name();
}

} // namespace matriz::analytics
