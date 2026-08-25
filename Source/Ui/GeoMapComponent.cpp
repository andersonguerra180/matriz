#include "GeoMapComponent.h"
#include <cmath>
#include <sstream>
#include <iomanip>

namespace matriz::ui {

GeoMapComponent::GeoMapComponent() {
    setOpaque(true);

#if JUCE_WEB_BROWSER
    try {
        webBrowser_ = std::make_unique<juce::WebBrowserComponent>();
        addAndMakeVisible(*webBrowser_);
    } catch (...) {}
#endif

    startTimerHz(60);
}

GeoMapComponent::~GeoMapComponent() {
    stopTimer();
}

void GeoMapComponent::updateDataset(const matriz::analytics::GeoMapDataset& dataset) {
    dataset_ = dataset;
    hoveredMarkerIdx_ = -1;
    hoveredClusterIdx_ = -1;

#if JUCE_WEB_BROWSER
    if (webBrowser_) {
        // Generate Leaflet Esri Satellite & OpenStreetMap Hybrid HTML Map
        std::ostringstream html;
        html << "<!DOCTYPE html><html><head><meta charset='utf-8'/>"
             << "<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/>"
             << "<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>"
             << "<style>html,body,#map{margin:0;padding:0;width:100%;height:100%;background:#0b0f19;font-family:sans-serif;}</style>"
             << "</head><body><div id='map'></div><script>"
             << "var map = L.map('map').setView([10, 0], 2);"
             << "L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {"
             << "  maxZoom: 19, attribution: 'Esri Satellite'"
             << "}).addTo(map);"
             << "L.tileLayer('https://{s}.basemaps.cartocdn.com/rastertiles/voyager_only_labels/{z}/{x}/{y}{r}.png', {"
             << "  maxZoom: 19"
             << "}).addTo(map);";

        for (const auto& marker : dataset_.geoMarkers) {
            std::string titleEsc = juce::String(marker.title).replace("'", "\\'").toStdString();
            std::string locEsc = juce::String(marker.locationName).replace("'", "\\'").toStdString();

            html << "L.circleMarker([" << marker.latitude << ", " << marker.longitude << "], {"
                 << "  radius: 8, color: '#facc15', fillColor: '#38bdf8', fillOpacity: 0.9"
                 << "}).addTo(map).bindPopup('<b>" << titleEsc << "</b><br/>" << locEsc << "');";
        }

        for (const auto& cluster : dataset_.geoClusters) {
            std::string regEsc = juce::String(cluster.regionName).replace("'", "\\'").toStdString();
            html << "L.circleMarker([" << cluster.latitude << ", " << cluster.longitude << "], {"
                 << "  radius: 16, color: '#38bdf8', fillColor: '#2563eb', fillOpacity: 0.8"
                 << "}).addTo(map).bindPopup('<b>" << cluster.assetCount << " Assets</b><br/>" << regEsc << "');";
        }

        html << "</script></body></html>";

        webBrowser_->goToURL("data:text/html;charset=utf-8," + juce::URL::addEscapeChars(html.str(), true));
    }
#endif

    repaint();
}

void GeoMapComponent::setSelectedAssets(const std::set<std::string>& selectedAssetIds) {
    selectedAssetIds_ = selectedAssetIds;
    repaint();
}

void GeoMapComponent::resetView() {
    mapCenterLat_ = 20.0;
    mapCenterLng_ = 0.0;
    zoomLevel_ = 1.0;
    repaint();
}

juce::Point<float> GeoMapComponent::latLngToScreen(double lat, double lng, const juce::Rectangle<float>& bounds) const {
    double normLng = (lng - mapCenterLng_) / 360.0 * zoomLevel_;
    double normLat = (lat - mapCenterLat_) / 180.0 * zoomLevel_;

    float x = bounds.getCentreX() + static_cast<float>(normLng * bounds.getWidth());
    float y = bounds.getCentreY() - static_cast<float>(normLat * bounds.getHeight());
    return {x, y};
}

bool GeoMapComponent::screenToLatLng(const juce::Point<float>& screenPos, const juce::Rectangle<float>& bounds, double& outLat, double& outLng) const {
    double normLng = (screenPos.x - bounds.getCentreX()) / (bounds.getWidth() * zoomLevel_);
    double normLat = (bounds.getCentreY() - screenPos.y) / (bounds.getHeight() * zoomLevel_);

    outLng = mapCenterLng_ + normLng * 360.0;
    outLat = mapCenterLat_ + normLat * 180.0;
    return (outLat >= -90.0 && outLat <= 90.0 && outLng >= -180.0 && outLng <= 180.0);
}

void GeoMapComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    auto statusArea = bounds.removeFromBottom(30);

    // 1. Deep Ocean Navy Gradient (Never Black)
    juce::ColourGradient oceanGradient(
        juce::Colour(0xff0d1b2a), bounds.getCentreX(), bounds.getY(),
        juce::Colour(0xff1b263b), bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(oceanGradient);
    g.fillRect(bounds);

    // 2. Vector Continent Landmass Polygons (Guarantees visible Earth continents)
    auto drawContinent = [&](const std::vector<std::pair<double, double>>& coords, juce::Colour fillCol, juce::Colour strokeCol) {
        if (coords.size() < 3) return;
        juce::Path path;
        auto p0 = latLngToScreen(coords[0].first, coords[0].second, bounds);
        path.startNewSubPath(p0);
        for (size_t i = 1; i < coords.size(); ++i) {
            auto pt = latLngToScreen(coords[i].first, coords[i].second, bounds);
            path.lineTo(pt);
        }
        path.closeSubPath();

        g.setColour(fillCol);
        g.fillPath(path);
        g.setColour(strokeCol);
        g.strokePath(path, juce::PathStrokeType(1.2f));
    };

    juce::Colour landFill(0xff1e293b);
    juce::Colour landBorder(0xff38bdf8);

    // South America
    drawContinent({{-12, -77}, {-5, -80}, {10, -75}, {10, -60}, {-5, -35}, {-23, -43}, {-35, -57}, {-55, -68}, {-45, -75}}, landFill, landBorder);

    // North America
    drawContinent({{60, -165}, {70, -140}, {60, -100}, {50, -60}, {25, -80}, {15, -90}, {15, -105}, {30, -115}, {50, -125}}, landFill, landBorder);

    // Europe & Asia (Eurasia)
    drawContinent({{70, 10}, {70, 80}, {60, 170}, {35, 140}, {20, 110}, {10, 80}, {25, 60}, {40, 30}, {35, -10}, {50, -10}, {60, 5}}, landFill, landBorder);

    // Africa
    drawContinent({{35, -10}, {30, 32}, {10, 50}, {-35, 20}, {-34, 18}, {5, 10}, {15, -17}}, landFill, landBorder);

    // Australia & Oceania
    drawContinent({{-12, 130}, {-15, 145}, {-35, 150}, {-38, 140}, {-32, 115}, {-20, 115}}, landFill, landBorder);

    // Antarctica
    drawContinent({{-70, -180}, {-70, -90}, {-70, 0}, {-70, 90}, {-70, 180}, {-85, 180}, {-85, -180}}, juce::Colour(0xff334155), juce::Colour(0xff94a3b8));

    // 3. Grid Lines
    g.setColour(juce::Colour(0xff38bdf8).withAlpha(0.25f));
    for (int lat = -80; lat <= 80; lat += 20) {
        auto p1 = latLngToScreen(lat, -180, bounds);
        auto p2 = latLngToScreen(lat, 180, bounds);
        g.drawLine(p1.x, p1.y, p2.x, p2.y, 0.8f);
    }
    for (int lng = -180; lng <= 180; lng += 30) {
        auto p1 = latLngToScreen(-80, lng, bounds);
        auto p2 = latLngToScreen(80, lng, bounds);
        g.drawLine(p1.x, p1.y, p2.x, p2.y, 0.8f);
    }

    // Equator & Prime Meridian
    g.setColour(juce::Colour(0xfffacc15).withAlpha(0.5f));
    auto eq1 = latLngToScreen(0, -180, bounds);
    auto eq2 = latLngToScreen(0, 180, bounds);
    g.drawLine(eq1.x, eq1.y, eq2.x, eq2.y, 1.2f);

    // 4. Geographic Clusters
    for (size_t c = 0; c < dataset_.geoClusters.size(); ++c) {
        const auto& cluster = dataset_.geoClusters[c];
        auto pt = latLngToScreen(cluster.latitude, cluster.longitude, bounds);
        if (!bounds.contains(pt)) continue;

        bool isHovered = (static_cast<int>(c) == hoveredClusterIdx_);
        float r = 14.0f + std::min(static_cast<float>(std::log10(cluster.assetCount + 1.0) * 8.0f), 26.0f);

        // Glow ring
        g.setColour(juce::Colour(0xff38bdf8).withAlpha(isHovered ? 0.6f : 0.35f));
        g.fillEllipse(pt.x - r - 5, pt.y - r - 5, (r + 5) * 2, (r + 5) * 2);

        // Core Cluster Node
        g.setColour(juce::Colour(0xff2563eb));
        g.fillEllipse(pt.x - r, pt.y - r, r * 2, r * 2);

        g.setColour(juce::Colours::white);
        g.drawEllipse(pt.x - r, pt.y - r, r * 2, r * 2, isHovered ? 2.5f : 1.5f);

        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(std::to_string(cluster.assetCount), pt.x - r, pt.y - r, r * 2, r * 2, juce::Justification::centred);
    }

    // 5. Individual Asset Markers
    for (size_t m = 0; m < dataset_.geoMarkers.size(); ++m) {
        const auto& marker = dataset_.geoMarkers[m];
        auto pt = latLngToScreen(marker.latitude, marker.longitude, bounds);
        if (!bounds.contains(pt)) continue;

        bool isSelected = (selectedAssetIds_.find(marker.assetId) != selectedAssetIds_.end());
        bool isHovered = (static_cast<int>(m) == hoveredMarkerIdx_);

        float r = isSelected ? 9.0f : (isHovered ? 8.0f : 6.0f);

        if (isSelected) {
            g.setColour(juce::Colour(0xfffacc15));
            g.drawEllipse(pt.x - r - 4, pt.y - r - 4, (r + 4) * 2, (r + 4) * 2, 2.0f);
        }

        g.setColour(juce::Colour(0xfffacc15));
        g.fillEllipse(pt.x - r, pt.y - r, r * 2, r * 2);
        g.setColour(juce::Colours::black);
        g.drawEllipse(pt.x - r, pt.y - r, r * 2, r * 2, 1.2f);
    }

    // 6. Hover Tooltip
    if (hoveredMarkerIdx_ >= 0 && hoveredMarkerIdx_ < static_cast<int>(dataset_.geoMarkers.size())) {
        const auto& marker = dataset_.geoMarkers[static_cast<size_t>(hoveredMarkerIdx_)];
        auto pt = latLngToScreen(marker.latitude, marker.longitude, bounds);

        juce::Rectangle<float> tooltipBox(pt.x + 12, pt.y + 12, 260, 130);
        if (tooltipBox.getRight() > bounds.getRight()) tooltipBox.setX(pt.x - 272);
        if (tooltipBox.getBottom() > bounds.getBottom()) tooltipBox.setY(pt.y - 142);

        g.setColour(juce::Colour(0xff0f172a));
        g.fillRoundedRectangle(tooltipBox, 8.0f);
        g.setColour(juce::Colour(0xff38bdf8));
        g.drawRoundedRectangle(tooltipBox, 8.0f, 1.5f);

        auto inner = tooltipBox.reduced(12, 10);
        g.setColour(juce::Colour(0xffffffff));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(marker.title, inner.removeFromTop(20), juce::Justification::centredLeft, true);

        auto drawRow = [&](const std::string& lbl, const std::string& val) {
            auto r = inner.removeFromTop(16);
            g.setColour(juce::Colour(0xfffacc15));
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(lbl, r.removeFromLeft(95), juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xffffffff));
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(val, r, juce::Justification::centredLeft, true);
        };

        drawRow("Media Type:", marker.mediaType);
        drawRow("Location:", marker.locationName);

        std::ostringstream ssCoords;
        ssCoords << std::fixed << std::setprecision(4) << marker.latitude << ", " << marker.longitude;
        drawRow("Coordinates:", ssCoords.str());
        drawRow("Geo Source:", matriz::analytics::geoSourceToString(marker.source));
    }

    // 7. Status Bar HUD at bottom
    g.setColour(juce::Colour(0xff0f172a));
    g.fillRect(statusArea);
    g.setColour(juce::Colour(0xff38bdf8));
    g.drawRect(statusArea.removeFromTop(1.0f));

    g.setColour(juce::Colour(0xfff8fafc));
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));

    std::ostringstream ss;
    ss << "Total Assets: " << dataset_.geoCoverage.totalAssets
       << " | Geolocated: " << dataset_.geoCoverage.geolocatedAssets
       << " | Unmapped: " << dataset_.geoCoverage.unmappedAssets
       << " | Coverage: " << std::fixed << std::setprecision(1) << dataset_.geoCoverage.coveragePercentage << "%";

    if (!selectedAssetIds_.empty()) {
        ss << " | Selected: " << selectedAssetIds_.size();
    }

    g.drawText(ss.str(), statusArea.reduced(12, 0), juce::Justification::centredLeft);
}

void GeoMapComponent::resized() {
#if JUCE_WEB_BROWSER
    if (webBrowser_) {
        auto bounds = getLocalBounds();
        bounds.removeFromBottom(30);
        webBrowser_->setBounds(bounds);
    }
#endif
    repaint();
}

void GeoMapComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.eventComponent != this) return;
    lastMousePos_ = e.position;
    isDragging_ = true;

    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromBottom(30);

    for (size_t m = 0; m < dataset_.geoMarkers.size(); ++m) {
        const auto& marker = dataset_.geoMarkers[m];
        auto pt = latLngToScreen(marker.latitude, marker.longitude, bounds);
        if (pt.getDistanceFrom(e.position) <= 12.0f) {
            if (aoSelecionarAsset) aoSelecionarAsset(marker.assetId);
            return;
        }
    }
}

void GeoMapComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_) return;
    auto delta = e.position - lastMousePos_;
    lastMousePos_ = e.position;

    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromBottom(30);

    double deltaLng = -static_cast<double>(delta.x) / bounds.getWidth() * 360.0 / zoomLevel_;
    double deltaLat = static_cast<double>(delta.y) / bounds.getHeight() * 180.0 / zoomLevel_;

    mapCenterLng_ = juce::jlimit(-180.0, 180.0, mapCenterLng_ + deltaLng);
    mapCenterLat_ = juce::jlimit(-90.0, 90.0, mapCenterLat_ + deltaLat);

    repaint();
}

void GeoMapComponent::mouseUp(const juce::MouseEvent&) {
    isDragging_ = false;
}

void GeoMapComponent::mouseMove(const juce::MouseEvent& e) {
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromBottom(30);

    int prevMarker = hoveredMarkerIdx_;
    int prevCluster = hoveredClusterIdx_;
    hoveredMarkerIdx_ = -1;
    hoveredClusterIdx_ = -1;

    for (size_t m = 0; m < dataset_.geoMarkers.size(); ++m) {
        const auto& marker = dataset_.geoMarkers[m];
        auto pt = latLngToScreen(marker.latitude, marker.longitude, bounds);
        if (pt.getDistanceFrom(e.position) <= 12.0f) {
            hoveredMarkerIdx_ = static_cast<int>(m);
            break;
        }
    }

    if (hoveredMarkerIdx_ < 0) {
        for (size_t c = 0; c < dataset_.geoClusters.size(); ++c) {
            const auto& cluster = dataset_.geoClusters[c];
            auto pt = latLngToScreen(cluster.latitude, cluster.longitude, bounds);
            if (pt.getDistanceFrom(e.position) <= 22.0f) {
                hoveredClusterIdx_ = static_cast<int>(c);
                break;
            }
        }
    }

    if (prevMarker != hoveredMarkerIdx_ || prevCluster != hoveredClusterIdx_) {
        repaint();
    }
}

void GeoMapComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    double factor = (wheel.deltaY > 0) ? 1.25 : 0.8;
    zoomLevel_ = juce::jlimit(0.8, 50.0, zoomLevel_ * factor);
    repaint();
}

void GeoMapComponent::timerCallback() {
}

} // namespace matriz::ui
