#include "AnalyticsChartComponent.h"
#include "Tokens.h"

namespace matriz::ui {

AnalyticsChartComponent::AnalyticsChartComponent() {}

void AnalyticsChartComponent::updateResult(const matriz::analytics::AnalyticsResult& res) {
    result_ = res;
    repaint();
}

void AnalyticsChartComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    if (result_.rowKeys.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo)));
        g.drawText("No data returned to display chart.", getLocalBounds(), juce::Justification::centred);
        return;
    }

    auto area = getLocalBounds().reduced(20, 16);

    // Title
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    std::string title = result_.dimensionALabel + " - " + result_.query.measure.name();
    g.drawText(title, area.removeFromTop(24), juce::Justification::centredLeft);
    area.removeFromTop(12);

    // Color palette for chart bars
    const std::vector<juce::Colour> palette = {
        juce::Colour(0xff2a9d8f), juce::Colour(0xff9d4edd), juce::Colour(0xfff4a261),
        juce::Colour(0xff2b9348), juce::Colour(0xffe76f51), juce::Colour(0xff3b82f6),
        juce::Colour(0xff10b981), juce::Colour(0xffd35400)
    };

    // Calculate max value for scaling
    double maxVal = 0.00001;
    for (const auto& rKey : result_.rowKeys) {
        auto it = result_.rowTotals.find(rKey);
        if (it != result_.rowTotals.end()) {
            maxVal = std::max(maxVal, it->second);
        }
    }

    int rowHeight = 36;
    for (size_t i = 0; i < result_.rowKeys.size(); ++i) {
        if (area.getHeight() < rowHeight) break;

        const auto& rKey = result_.rowKeys[i];
        auto rIt = result_.rowTotals.find(rKey);
        double val = (rIt != result_.rowTotals.end()) ? rIt->second : 0.0;

        auto rowArea = area.removeFromTop(rowHeight);
        area.removeFromTop(6);

        // Label (left 180px)
        g.setColour(rKey == "Other" ? tk.textoTerciario : tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText(rKey, rowArea.removeFromLeft(180), juce::Justification::centredLeft, true);

        // Bar container
        float pct = static_cast<float>(val / maxVal);
        pct = juce::jlimit(0.0f, 1.0f, pct);

        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(rowArea.toFloat(), tk.raioPequeno);

        juce::Colour barCol = palette[i % palette.size()];
        if (rKey == "Other") barCol = tk.textoTerciario;

        int barW = juce::jmax(8, static_cast<int>(rowArea.getWidth() * pct));
        g.setColour(barCol);
        g.fillRoundedRectangle(rowArea.withWidth(barW).toFloat(), tk.raioPequeno);

        // Value text over bar
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        std::string formattedVal = matriz::analytics::AnalyticsFormatter::formatValue(val, result_.query.measure.field, result_.query.measure.aggregation);
        g.drawText("  " + formattedVal, rowArea.withTrimmedLeft(barW), juce::Justification::centredLeft, true);
    }
}

} // namespace matriz::ui
