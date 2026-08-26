#include "AnalyticsTableComponent.h"
#include "Tokens.h"

namespace matriz::ui {

AnalyticsTableComponent::AnalyticsTableComponent() {
    table_.setModel(this);
    table_.setOutlineThickness(1);
    addAndMakeVisible(table_);
}

void AnalyticsTableComponent::updateResult(const matriz::analytics::AnalyticsResult& res) {
    result_ = res;

    auto& header = table_.getHeader();
    header.removeAllColumns();

    // Column 1: Dimension A
    header.addColumn(result_.dimensionALabel.empty() ? "Dimension" : result_.dimensionALabel, 1, 200, 100, 400);

    // Columns for Dimension B
    int colId = 2;
    for (const auto& colName : result_.colKeys) {
        header.addColumn(colName, colId++, 120, 60, 300);
    }

    // Last Column: Row TOTAL
    header.addColumn("TOTAL", colId, 120, 60, 300);

    table_.updateContent();
    repaint();
}

int AnalyticsTableComponent::getNumRows() {
    // Row keys + 1 for column TOTAL row
    if (result_.rowKeys.empty()) return 0;
    return static_cast<int>(result_.rowKeys.size()) + 1;
}

void AnalyticsTableComponent::paintRowBackground(juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected) {
    const auto& tk = tema();
    bool isTotalRow = (rowNumber == static_cast<int>(result_.rowKeys.size()));

    if (isTotalRow) {
        g.fillAll(tk.painelAlt);
    } else if (rowIsSelected) {
        g.fillAll(tk.acento.withAlpha(0.15f));
    } else if (rowNumber % 2 == 1) {
        g.fillAll(tk.painel.withAlpha(0.4f));
    } else {
        g.fillAll(tk.fundo);
    }
}

void AnalyticsTableComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/) {
    const auto& tk = tema();
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));

    bool isTotalRow = (rowNumber == static_cast<int>(result_.rowKeys.size()));
    juce::Rectangle<int> area(4, 0, width - 8, height);

    if (isTotalRow) {
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        if (columnId == 1) {
            g.drawText("TOTAL", area, juce::Justification::centredLeft, true);
        } else {
            size_t colIdx = static_cast<size_t>(columnId - 2);
            double val = 0.0;
            if (colIdx < result_.colKeys.size()) {
                std::string colName = result_.colKeys[colIdx];
                auto it = result_.colTotals.find(colName);
                if (it != result_.colTotals.end()) val = it->second;
            } else {
                val = result_.grandTotal;
            }
            std::string text = matriz::analytics::AnalyticsFormatter::formatValue(val, result_.query.measure.field, result_.query.measure.aggregation, result_.query.showPercentOfTotal);
            g.drawText(text, area, juce::Justification::centredRight, true);
        }
        return;
    }

    std::string rowName = result_.rowKeys[static_cast<size_t>(rowNumber)];

    if (columnId == 1) {
        if (rowName == "Other") {
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::italic)));
            g.setColour(tk.textoSecundario);
        }
        g.drawText(rowName, area, juce::Justification::centredLeft, true);
    } else {
        size_t colIdx = static_cast<size_t>(columnId - 2);
        double val = 0.0;

        if (colIdx < result_.colKeys.size()) {
            std::string colName = result_.colKeys[colIdx];
            auto it = result_.cells.find({rowName, colName});
            if (it != result_.cells.end()) val = it->second;
        } else {
            auto it = result_.rowTotals.find(rowName);
            if (it != result_.rowTotals.end()) val = it->second;
        }

        std::string text = matriz::analytics::AnalyticsFormatter::formatValue(val, result_.query.measure.field, result_.query.measure.aggregation, result_.query.showPercentOfTotal);
        g.drawText(text, area, juce::Justification::centredRight, true);
    }
}

void AnalyticsTableComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    if (result_.rowKeys.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo)));
        g.drawText("No data returned for the query criteria.", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Paint 1D Summary Statistics Box if applicable
    if (result_.stats1D.hasNumericStats) {
        auto area = getLocalBounds();
        auto statsBox = area.removeFromBottom(84).reduced(8, 4);

        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(statsBox.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(statsBox.toFloat(), tk.raioMedio, 1.0f);

        auto inner = statsBox.reduced(12, 8);
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("DESCRIPTIVE STATISTICS (1D)", inner.removeFromTop(14), juce::Justification::centredLeft);

        auto row = inner;
        int statW = row.getWidth() / 7;

        auto drawStat = [&](const std::string& label, const std::string& val) {
            auto b = row.removeFromLeft(statW);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(label, b.removeFromTop(14), juce::Justification::centredLeft);
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
            g.drawText(val, b, juce::Justification::centredLeft);
        };

        drawStat("COUNT", matriz::analytics::AnalyticsFormatter::formatValue(result_.stats1D.count, matriz::analytics::MeasureField::AssetCount, matriz::analytics::AggregationType::Count));
        drawStat("TOTAL", matriz::analytics::AnalyticsFormatter::formatValue(result_.stats1D.sum, result_.query.measure.field, matriz::analytics::AggregationType::Sum));
        drawStat("MEAN", matriz::analytics::AnalyticsFormatter::formatValue(result_.stats1D.mean, result_.query.measure.field, matriz::analytics::AggregationType::Avg));
        drawStat("MEDIANA", matriz::analytics::AnalyticsFormatter::formatValue(result_.stats1D.median, result_.query.measure.field, matriz::analytics::AggregationType::Median));
        drawStat("MIN", matriz::analytics::AnalyticsFormatter::formatValue(result_.stats1D.min, result_.query.measure.field, matriz::analytics::AggregationType::Min));
        drawStat("MAX", matriz::analytics::AnalyticsFormatter::formatValue(result_.stats1D.max, result_.query.measure.field, matriz::analytics::AggregationType::Max));
        drawStat("STDDEV (S)", matriz::analytics::AnalyticsFormatter::formatValue(result_.stats1D.stddevSamp, result_.query.measure.field, matriz::analytics::AggregationType::StdDevSamp));
    }
}

void AnalyticsTableComponent::resized() {
    auto area = getLocalBounds();
    if (result_.stats1D.hasNumericStats) {
        area.removeFromBottom(88);
    }
    table_.setBounds(area);
}

} // namespace matriz::ui
