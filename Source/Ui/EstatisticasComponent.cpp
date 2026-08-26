#include "EstatisticasComponent.h"
#include "Tokens.h"
#include "../Ingest/LeituraTecnica.h"
#include <map>
#include <algorithm>

namespace matriz::ui {

namespace {

juce::String formatSizeHuman(juce::int64 bytes) {
    if (bytes <= 0) return "0 B";
    constexpr double kKB = 1024.0;
    constexpr double kMB = 1024.0 * 1024.0;
    constexpr double kGB = 1024.0 * 1024.0 * 1024.0;
    constexpr double kTB = 1024.0 * 1024.0 * 1024.0 * 1024.0;

    double d = static_cast<double>(bytes);
    if (d >= kTB) return juce::String(d / kTB, 2) + " TB";
    if (d >= kGB) return juce::String(d / kGB, 2) + " GB";
    if (d >= kMB) return juce::String(d / kMB, 1) + " MB";
    if (d >= kKB) return juce::String(d / kKB, 1) + " KB";
    return juce::String(bytes) + " B";
}

} // namespace

EstatisticasComponent::EstatisticasComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    addAndMakeVisible(treemapComponent_);

    treemapComponent_.aoSelecionarItem = [this](const std::string& itemId) {
        if (aoSelecionarItem) {
            aoSelecionarItem(itemId);
        }
    };

    treemapComponent_.aoAbrirNoGrid = [this](const std::set<std::string>& assetIds) {
        if (aoAbrirNoGrid) {
            aoAbrirNoGrid(assetIds);
        }
    };

    recarregar();
}

void EstatisticasComponent::setSelectedAssets(const std::set<std::string>&) {
}

void EstatisticasComponent::recarregar() {
    matriz::db::Database& db = projeto_.projeto().registro();
    carregarMetricasDoBanco(db);
    treemapComponent_.recarregarDoBanco(db);
    repaint();
}

void EstatisticasComponent::carregarMetricasDoBanco(matriz::db::Database& db) {
    summaryKpi_ = SummaryKpi{};
    std::map<std::string, uint64_t> formatCounts;

    try {
        auto stmt = db.prepare(
            "SELECT i.id, IFNULL(a.caminho_relativo, IFNULL(a.caminho_absoluto_origem, '')), IFNULL(a.tamanho_bytes, 0) "
            "FROM item i "
            "LEFT JOIN arquivo a ON a.item_id = i.id AND a.eh_master = 1");

        while (stmt.step()) {
            std::string pathStr = stmt.columnText(1);
            juce::int64 sizeBytes = static_cast<juce::int64>(stmt.columnInt(2));

            juce::String ext = "";
            if (!pathStr.empty()) {
                int dotIdx = pathStr.rfind('.');
                if (dotIdx != std::string::npos && dotIdx < pathStr.length() - 1) {
                    ext = pathStr.substr(dotIdx + 1);
                    ext = ext.toLowerCase();
                }
            }

            std::string format = ext.isEmpty() ? "No Format" : ext.toUpperCase().toStdString();
            formatCounts[format]++;

            summaryKpi_.totalAssets++;
            summaryKpi_.totalBytes += sizeBytes;
        }
    } catch (...) {}

    std::string topFmt = "Audio";
    uint64_t topCnt = 0;
    for (const auto& [fmt, cnt] : formatCounts) {
        if (cnt > topCnt) {
            topCnt = cnt;
            topFmt = fmt;
        }
    }
    summaryKpi_.primaryFormatName = topFmt;
    summaryKpi_.primaryFormatCount = topCnt;
    summaryKpi_.vulnerableAssetsCount = summaryKpi_.totalAssets;
}

void EstatisticasComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    auto area = getLocalBounds().reduced(16, 12);

    // Section Header Title
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    g.drawText("Collection Analytics - SpaceMonger Treemap Explorer", area.removeFromTop(22), juce::Justification::left);
    area.removeFromTop(8);

    // Top 4 KPI Cards Area (Top 70px)
    auto topKpiArea = area.removeFromTop(70);
    desenharTopKpiCards(g, topKpiArea);
}

void EstatisticasComponent::desenharTopKpiCards(juce::Graphics& g, const juce::Rectangle<int>& area) {
    const auto& tk = tema();
    int cardW = (area.getWidth() - 36) / 4;
    int cardH = area.getHeight();

    auto drawCard = [&](int x, const juce::String& title, const juce::String& val, const juce::String& subtitle, juce::Colour accentColor) {
        juce::Rectangle<int> r(x, area.getY(), cardW, cardH);
        g.setColour(tk.painel);
        g.fillRoundedRectangle(r.toFloat(), 6.0f);

        // Accent strip on left
        auto strip = r.removeFromLeft(4);
        g.setColour(accentColor);
        g.fillRoundedRectangle(strip.toFloat(), 3.0f);

        g.setColour(tk.borda);
        g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.0f);

        auto inner = r.reduced(10, 6);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(title, inner.removeFromTop(12), juce::Justification::left);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.drawText(val, inner.removeFromTop(20), juce::Justification::left);

        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(subtitle, inner, juce::Justification::left);
    };

    int x = area.getX();
    drawCard(x, "TOTAL ASSETS", juce::String(summaryKpi_.totalAssets), "Cataloged files", juce::Colour(0xff3b82f6)); x += cardW + 12;
    drawCard(x, "STORAGE SIZE", formatSizeHuman(summaryKpi_.totalBytes), "Occupied volume", juce::Colour(0xff10b981)); x += cardW + 12;
    drawCard(x, "PRIMARY FORMAT", juce::String(summaryKpi_.primaryFormatName), juce::String(summaryKpi_.primaryFormatCount) + " assets", juce::Colour(0xfff59e0b)); x += cardW + 12;
    drawCard(x, "BACKUP HEALTH", juce::String(summaryKpi_.backupHealthPercentage, 0) + "%", juce::String(summaryKpi_.vulnerableAssetsCount) + " vulnerable", juce::Colour(0xffef4444));
}

void EstatisticasComponent::resized() {
    auto area = getLocalBounds().reduced(16, 12);
    area.removeFromTop(22); // Header
    area.removeFromTop(8);  // Gap
    area.removeFromTop(70); // KPI cards
    area.removeFromTop(12); // Gap

    treemapComponent_.setBounds(area);
}

} // namespace matriz::ui
