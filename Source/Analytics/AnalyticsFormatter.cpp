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
        case DimensionType::None: return "Nenhuma";
        case DimensionType::MediaType: return "Tipo de Mídia";
        case DimensionType::Extension: return "Extensão";
        case DimensionType::Collection: return "Coleção";
        case DimensionType::Year: return "Ano";
        case DimensionType::Codec: return "Codec";
        case DimensionType::SampleRate: return "Sample Rate";
        case DimensionType::BitDepth: return "Bit Depth";
        case DimensionType::Content: return "Conteúdo";
        case DimensionType::IsrcStatus: return "Status ISRC";
        case DimensionType::PreservationStatus: return "Status de Preservação";
        case DimensionType::MetadataStatus: return "Status de Metadata";
        case DimensionType::AssetOrigin: return "Origem do Asset";
        case DimensionType::IngestionDate: return "Data de Ingestão";
        case DimensionType::Folder: return "Pasta do Acervo";
        case DimensionType::Tag: return "Tag";
        case DimensionType::AssetState: return "Estado do Asset";
        case DimensionType::HasError: return "Status de Erro";
        case DimensionType::HasThumbnail: return "Existência de Thumbnail";
        case DimensionType::HasSidecar: return "Existência de Sidecar";
        case DimensionType::HasXmp: return "Existência de XMP";
        case DimensionType::HasMetadata: return "Existência de Metadata";
        case DimensionType::IsDuplicate: return "Duplicado";
        case DimensionType::HasHash: return "Existência de Hash SHA-256";
        case DimensionType::IsPreserved: return "Preservado";
        case DimensionType::IsParcialPreserved: return "Preservação Parcial";
    }
    return "Desconhecido";
}

std::string AnalyticsFormatter::formatTimeGranularityLabel(TimeGranularity gran) {
    switch (gran) {
        case TimeGranularity::Year: return "Ano (YYYY)";
        case TimeGranularity::YearMonth: return "Mês/Ano (YYYY-MM)";
        case TimeGranularity::Month: return "Mês (MM)";
        case TimeGranularity::Day: return "Dia (YYYY-MM-DD)";
    }
    return "";
}

std::string AnalyticsFormatter::formatMeasureLabel(const Measure& m) {
    return m.name();
}

} // namespace matriz::analytics
