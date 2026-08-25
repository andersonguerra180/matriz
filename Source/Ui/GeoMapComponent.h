#pragma once

#include <JuceHeader.h>
#include "../Analytics/AssetGeolocation.h"
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace matriz::ui {

class GeoMapComponent : public juce::Component, public juce::Timer {
public:
    GeoMapComponent();
    ~GeoMapComponent() override;

    void updateDataset(const matriz::analytics::GeoMapDataset& dataset);
    void setSelectedAssets(const std::set<std::string>& selectedAssetIds);

    std::function<void(const std::string& assetId)> aoSelecionarAsset;
    std::function<void()> aoFiltrarNaoGeolocalizados;
    std::function<void()> aoSolicitarLocalizacaoEmLote;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void timerCallback() override;

    void resetView();

private:
    matriz::analytics::GeoMapDataset dataset_;
    std::set<std::string> selectedAssetIds_;

    double mapCenterLat_ = 20.0;
    double mapCenterLng_ = 0.0;
    double zoomLevel_ = 1.0;

    juce::Point<float> lastMousePos_;
    bool isDragging_ = false;

    int hoveredMarkerIdx_ = -1;
    int hoveredClusterIdx_ = -1;

    juce::Point<float> latLngToScreen(double lat, double lng, const juce::Rectangle<float>& bounds) const;
    bool screenToLatLng(const juce::Point<float>& screenPos, const juce::Rectangle<float>& bounds, double& outLat, double& outLng) const;

#if JUCE_WEB_BROWSER
    std::unique_ptr<juce::WebBrowserComponent> webBrowser_;
#endif
};

} // namespace matriz::ui
