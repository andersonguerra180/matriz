#pragma once

#include <JuceHeader.h>
#include "../Db/Database.h"
#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace matriz::analytics {

enum class GeoSource {
    None,
    EmbeddedMetadata,
    UserGps,
    UserCoordinates,
    UserAddress,
    UserPostalCode,
    UserCity,
    UserState,
    UserCountry,
    UserContinent,
    Imported,
    Geocoded
};

std::string geoSourceToString(GeoSource source);
GeoSource geoSourceFromString(const std::string& str);

struct AssetGeolocation {
    std::string assetId;
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<double> altitude;

    std::optional<std::string> continent;
    std::optional<std::string> country;
    std::optional<std::string> countryCode;
    std::optional<std::string> stateProvince;
    std::optional<std::string> stateCode;
    std::optional<std::string> city;
    std::optional<std::string> municipality;
    std::optional<std::string> neighborhood;
    std::optional<std::string> district;
    std::optional<std::string> postalCode;
    std::optional<std::string> street;
    std::optional<std::string> streetNumber;
    std::optional<std::string> locality;
    std::optional<std::string> formattedAddress;

    GeoSource source = GeoSource::None;
    std::optional<double> precisionAccuracy;
    std::optional<double> confidence;

    std::string createdAt;
    std::string updatedAt;

    bool hasValidCoordinates() const {
        if (!latitude.has_value() || !longitude.has_value()) return false;
        double lat = *latitude;
        double lng = *longitude;
        if (std::isnan(lat) || std::isinf(lat) || std::isnan(lng) || std::isinf(lng)) return false;
        return (lat >= -90.0 && lat <= 90.0 && lng >= -180.0 && lng <= 180.0);
    }

    bool hasAnyLocationData() const {
        return hasValidCoordinates() || continent.has_value() || country.has_value() ||
               stateProvince.has_value() || city.has_value() || postalCode.has_value() ||
               formattedAddress.has_value();
    }
};

struct GeolocationCoverageStats {
    uint64_t totalAssets = 0;
    uint64_t geolocatedAssets = 0;
    uint64_t unmappedAssets = 0;
    double coveragePercentage = 0.0;
};

struct GeoMapMarker {
    uint64_t numericId = 0;
    std::string assetId;
    std::string title;
    std::string mediaType;
    std::string locationName;
    double latitude = 0.0;
    double longitude = 0.0;
    GeoSource source = GeoSource::None;
    juce::Colour color;
};

struct GeoMapCluster {
    std::string clusterId;
    std::string regionName;
    double latitude = 0.0;
    double longitude = 0.0;
    uint64_t assetCount = 0;
    std::vector<std::string> sampleAssetIds;
    juce::Colour color;
};

struct GeoMapDataset {
    std::vector<GeoMapMarker> geoMarkers;
    std::vector<GeoMapCluster> geoClusters;
    GeolocationCoverageStats geoCoverage;
    uint64_t totalAssetCount = 0;
    uint64_t validAssetCount = 0;
    uint64_t displayedPointCount = 0;
    double queryTimeMs = 0.0;
};

class AssetGeolocationRepository {
public:
    static void salvar(matriz::db::Database& db, const AssetGeolocation& geo);
    static void remover(matriz::db::Database& db, const std::string& assetId);
    static std::optional<AssetGeolocation> obterPorAssetId(matriz::db::Database& db, const std::string& assetId);
    static std::vector<AssetGeolocation> obterTodosGeolocalizados(matriz::db::Database& db);
    static GeolocationCoverageStats obterEstatisticasCobertura(matriz::db::Database& db);
    static void salvarEmLote(matriz::db::Database& db, const std::vector<std::string>& assetIds, const AssetGeolocation& geoTemplate);
};

} // namespace matriz::analytics
