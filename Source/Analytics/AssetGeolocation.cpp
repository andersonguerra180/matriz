#include "AssetGeolocation.h"
#include <cmath>
#include <ctime>

namespace matriz::analytics {

std::string geoSourceToString(GeoSource source) {
    switch (source) {
        case GeoSource::None: return "NONE";
        case GeoSource::EmbeddedMetadata: return "EMBEDDED_METADATA";
        case GeoSource::UserGps: return "USER_GPS";
        case GeoSource::UserCoordinates: return "USER_COORDINATES";
        case GeoSource::UserAddress: return "USER_ADDRESS";
        case GeoSource::UserPostalCode: return "USER_POSTAL_CODE";
        case GeoSource::UserCity: return "USER_CITY";
        case GeoSource::UserState: return "USER_STATE";
        case GeoSource::UserCountry: return "USER_COUNTRY";
        case GeoSource::UserContinent: return "USER_CONTINENT";
        case GeoSource::Imported: return "IMPORTED";
        case GeoSource::Geocoded: return "GEOCODED";
    }
    return "NONE";
}

GeoSource geoSourceFromString(const std::string& str) {
    if (str == "EMBEDDED_METADATA") return GeoSource::EmbeddedMetadata;
    if (str == "USER_GPS") return GeoSource::UserGps;
    if (str == "USER_COORDINATES") return GeoSource::UserCoordinates;
    if (str == "USER_ADDRESS") return GeoSource::UserAddress;
    if (str == "USER_POSTAL_CODE") return GeoSource::UserPostalCode;
    if (str == "USER_CITY") return GeoSource::UserCity;
    if (str == "USER_STATE") return GeoSource::UserState;
    if (str == "USER_COUNTRY") return GeoSource::UserCountry;
    if (str == "USER_CONTINENT") return GeoSource::UserContinent;
    if (str == "IMPORTED") return GeoSource::Imported;
    if (str == "GEOCODED") return GeoSource::Geocoded;
    return GeoSource::None;
}

static std::string agoraIso() {
    std::time_t t = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&t, &utc);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(buf);
}

void AssetGeolocationRepository::salvar(matriz::db::Database& db, const AssetGeolocation& geo) {
    if (geo.assetId.empty()) return;

    std::string sql = "INSERT INTO asset_geolocation ("
                      "asset_id, latitude, longitude, altitude, continent, country, country_code, "
                      "state_province, state_code, city, municipality, neighborhood, district, "
                      "postal_code, street, street_number, locality, formatted_address, source, "
                      "precision_accuracy, confidence, created_at, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                      "ON CONFLICT(asset_id) DO UPDATE SET "
                      "latitude = excluded.latitude, longitude = excluded.longitude, altitude = excluded.altitude, "
                      "continent = excluded.continent, country = excluded.country, country_code = excluded.country_code, "
                      "state_province = excluded.state_province, state_code = excluded.state_code, city = excluded.city, "
                      "municipality = excluded.municipality, neighborhood = excluded.neighborhood, district = excluded.district, "
                      "postal_code = excluded.postal_code, street = excluded.street, street_number = excluded.street_number, "
                      "locality = excluded.locality, formatted_address = excluded.formatted_address, source = excluded.source, "
                      "precision_accuracy = excluded.precision_accuracy, confidence = excluded.confidence, "
                      "updated_at = excluded.updated_at";

    std::vector<matriz::db::Value> params;
    params.push_back(matriz::db::Value::of(geo.assetId));
    params.push_back(geo.latitude ? matriz::db::Value::of(*geo.latitude) : matriz::db::Value::null());
    params.push_back(geo.longitude ? matriz::db::Value::of(*geo.longitude) : matriz::db::Value::null());
    params.push_back(geo.altitude ? matriz::db::Value::of(*geo.altitude) : matriz::db::Value::null());
    params.push_back(geo.continent ? matriz::db::Value::of(*geo.continent) : matriz::db::Value::null());
    params.push_back(geo.country ? matriz::db::Value::of(*geo.country) : matriz::db::Value::null());
    params.push_back(geo.countryCode ? matriz::db::Value::of(*geo.countryCode) : matriz::db::Value::null());
    params.push_back(geo.stateProvince ? matriz::db::Value::of(*geo.stateProvince) : matriz::db::Value::null());
    params.push_back(geo.stateCode ? matriz::db::Value::of(*geo.stateCode) : matriz::db::Value::null());
    params.push_back(geo.city ? matriz::db::Value::of(*geo.city) : matriz::db::Value::null());
    params.push_back(geo.municipality ? matriz::db::Value::of(*geo.municipality) : matriz::db::Value::null());
    params.push_back(geo.neighborhood ? matriz::db::Value::of(*geo.neighborhood) : matriz::db::Value::null());
    params.push_back(geo.district ? matriz::db::Value::of(*geo.district) : matriz::db::Value::null());
    params.push_back(geo.postalCode ? matriz::db::Value::of(*geo.postalCode) : matriz::db::Value::null());
    params.push_back(geo.street ? matriz::db::Value::of(*geo.street) : matriz::db::Value::null());
    params.push_back(geo.streetNumber ? matriz::db::Value::of(*geo.streetNumber) : matriz::db::Value::null());
    params.push_back(geo.locality ? matriz::db::Value::of(*geo.locality) : matriz::db::Value::null());
    params.push_back(geo.formattedAddress ? matriz::db::Value::of(*geo.formattedAddress) : matriz::db::Value::null());
    params.push_back(matriz::db::Value::of(geoSourceToString(geo.source)));
    params.push_back(geo.precisionAccuracy ? matriz::db::Value::of(*geo.precisionAccuracy) : matriz::db::Value::null());
    params.push_back(geo.confidence ? matriz::db::Value::of(*geo.confidence) : matriz::db::Value::null());

    std::string now = agoraIso();
    params.push_back(matriz::db::Value::of(geo.createdAt.empty() ? now : geo.createdAt));
    params.push_back(matriz::db::Value::of(now));

    db.run(sql, params);
}

std::optional<AssetGeolocation> AssetGeolocationRepository::obterPorAssetId(matriz::db::Database& db, const std::string& assetId) {
    std::string sql = "SELECT asset_id, latitude, longitude, altitude, continent, country, country_code, "
                      "state_province, state_code, city, municipality, neighborhood, district, postal_code, "
                      "street, street_number, locality, formatted_address, source, precision_accuracy, confidence, "
                      "created_at, updated_at FROM asset_geolocation WHERE asset_id = ?";
    auto stmt = db.prepare(sql);
    stmt.bind(1, matriz::db::Value::of(assetId));

    if (stmt.step()) {
        AssetGeolocation g;
        g.assetId = stmt.columnText(0);
        if (!stmt.columnIsNull(1)) g.latitude = stmt.columnReal(1);
        if (!stmt.columnIsNull(2)) g.longitude = stmt.columnReal(2);
        if (!stmt.columnIsNull(3)) g.altitude = stmt.columnReal(3);
        if (!stmt.columnIsNull(4)) g.continent = stmt.columnText(4);
        if (!stmt.columnIsNull(5)) g.country = stmt.columnText(5);
        if (!stmt.columnIsNull(6)) g.countryCode = stmt.columnText(6);
        if (!stmt.columnIsNull(7)) g.stateProvince = stmt.columnText(7);
        if (!stmt.columnIsNull(8)) g.stateCode = stmt.columnText(8);
        if (!stmt.columnIsNull(9)) g.city = stmt.columnText(9);
        if (!stmt.columnIsNull(10)) g.municipality = stmt.columnText(10);
        if (!stmt.columnIsNull(11)) g.neighborhood = stmt.columnText(11);
        if (!stmt.columnIsNull(12)) g.district = stmt.columnText(12);
        if (!stmt.columnIsNull(13)) g.postalCode = stmt.columnText(13);
        if (!stmt.columnIsNull(14)) g.street = stmt.columnText(14);
        if (!stmt.columnIsNull(15)) g.streetNumber = stmt.columnText(15);
        if (!stmt.columnIsNull(16)) g.locality = stmt.columnText(16);
        if (!stmt.columnIsNull(17)) g.formattedAddress = stmt.columnText(17);
        g.source = geoSourceFromString(stmt.columnText(18));
        if (!stmt.columnIsNull(19)) g.precisionAccuracy = stmt.columnReal(19);
        if (!stmt.columnIsNull(20)) g.confidence = stmt.columnReal(20);
        g.createdAt = stmt.columnText(21);
        g.updatedAt = stmt.columnText(22);
        return g;
    }
    return std::nullopt;
}

std::vector<AssetGeolocation> AssetGeolocationRepository::obterTodosGeolocalizados(matriz::db::Database& db) {
    std::vector<AssetGeolocation> result;
    std::string sql = "SELECT asset_id, latitude, longitude, altitude, continent, country, country_code, "
                      "state_province, state_code, city, municipality, neighborhood, district, postal_code, "
                      "street, street_number, locality, formatted_address, source, precision_accuracy, confidence, "
                      "created_at, updated_at FROM asset_geolocation";
    auto stmt = db.prepare(sql);

    while (stmt.step()) {
        AssetGeolocation g;
        g.assetId = stmt.columnText(0);
        if (!stmt.columnIsNull(1)) g.latitude = stmt.columnReal(1);
        if (!stmt.columnIsNull(2)) g.longitude = stmt.columnReal(2);
        if (!stmt.columnIsNull(3)) g.altitude = stmt.columnReal(3);
        if (!stmt.columnIsNull(4)) g.continent = stmt.columnText(4);
        if (!stmt.columnIsNull(5)) g.country = stmt.columnText(5);
        if (!stmt.columnIsNull(6)) g.countryCode = stmt.columnText(6);
        if (!stmt.columnIsNull(7)) g.stateProvince = stmt.columnText(7);
        if (!stmt.columnIsNull(8)) g.stateCode = stmt.columnText(8);
        if (!stmt.columnIsNull(9)) g.city = stmt.columnText(9);
        if (!stmt.columnIsNull(10)) g.municipality = stmt.columnText(10);
        if (!stmt.columnIsNull(11)) g.neighborhood = stmt.columnText(11);
        if (!stmt.columnIsNull(12)) g.district = stmt.columnText(12);
        if (!stmt.columnIsNull(13)) g.postalCode = stmt.columnText(13);
        if (!stmt.columnIsNull(14)) g.street = stmt.columnText(14);
        if (!stmt.columnIsNull(15)) g.streetNumber = stmt.columnText(15);
        if (!stmt.columnIsNull(16)) g.locality = stmt.columnText(16);
        if (!stmt.columnIsNull(17)) g.formattedAddress = stmt.columnText(17);
        g.source = geoSourceFromString(stmt.columnText(18));
        if (!stmt.columnIsNull(19)) g.precisionAccuracy = stmt.columnReal(19);
        if (!stmt.columnIsNull(20)) g.confidence = stmt.columnReal(20);
        g.createdAt = stmt.columnText(21);
        g.updatedAt = stmt.columnText(22);
        result.push_back(g);
    }
    return result;
}

GeolocationCoverageStats AssetGeolocationRepository::obterEstatisticasCobertura(matriz::db::Database& db) {
    GeolocationCoverageStats stats;

    auto stmtTotal = db.prepare("SELECT COUNT(DISTINCT id) FROM item");
    if (stmtTotal.step()) stats.totalAssets = static_cast<uint64_t>(stmtTotal.columnInt(0));

    auto stmtGeo = db.prepare("SELECT COUNT(DISTINCT asset_id) FROM asset_geolocation "
                              "WHERE latitude IS NOT NULL OR country IS NOT NULL OR city IS NOT NULL OR postal_code IS NOT NULL");
    if (stmtGeo.step()) stats.geolocatedAssets = static_cast<uint64_t>(stmtGeo.columnInt(0));

    if (stats.totalAssets >= stats.geolocatedAssets) {
        stats.unmappedAssets = stats.totalAssets - stats.geolocatedAssets;
    } else {
        stats.unmappedAssets = 0;
    }

    if (stats.totalAssets > 0) {
        stats.coveragePercentage = (static_cast<double>(stats.geolocatedAssets) / static_cast<double>(stats.totalAssets)) * 100.0;
    } else {
        stats.coveragePercentage = 0.0;
    }

    return stats;
}

void AssetGeolocationRepository::salvarEmLote(matriz::db::Database& db, const std::vector<std::string>& assetIds, const AssetGeolocation& geoTemplate) {
    for (const auto& id : assetIds) {
        AssetGeolocation g = geoTemplate;
        g.assetId = id;
        salvar(db, g);
    }
}

} // namespace matriz::analytics
