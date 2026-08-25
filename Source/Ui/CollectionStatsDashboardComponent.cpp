#include "CollectionStatsDashboardComponent.h"
#include "Tokens.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>

namespace matriz::ui {

CollectionStatsDashboardComponent::CollectionStatsDashboardComponent() {
    setOpaque(true);
}

void CollectionStatsDashboardComponent::updateData(const CollectionStatsData& data) {
    data_ = data;
    repaint();
}

void CollectionStatsDashboardComponent::recarregarDoBanco(matriz::db::Database& db) {
    CollectionStatsData d;

    // 1. Total assets count
    auto stmtTotal = db.prepare("SELECT COUNT(*) FROM item");
    if (stmtTotal.step()) d.totalAssets = static_cast<uint64_t>(stmtTotal.columnInt(0));

    // 2. Storage size
    auto stmtSize = db.prepare("SELECT COALESCE(SUM(tamanho_bytes), 0) FROM arquivo WHERE eh_master = 1");
    if (stmtSize.step()) d.totalSizeBytes = static_cast<juce::int64>(stmtSize.columnInt(0));

    // 3. Format category distribution
    std::map<std::string, std::pair<uint64_t, juce::int64>> catMap;
    auto stmtCat = db.prepare("SELECT COALESCE(i.tipo_midia, 'other'), COUNT(DISTINCT i.id), COALESCE(SUM(a.tamanho_bytes), 0) "
                              "FROM item i LEFT JOIN arquivo a ON a.item_id = i.id AND a.eh_master = 1 "
                              "GROUP BY i.tipo_midia");

    while (stmtCat.step()) {
        std::string rawCat = stmtCat.columnText(0);
        uint64_t cnt = static_cast<uint64_t>(stmtCat.columnInt(1));
        juce::int64 sz = static_cast<juce::int64>(stmtCat.columnInt(2));
        catMap[rawCat] = {cnt, sz};
    }

    auto getCat = [&](const std::string& key) {
        auto it = catMap.find(key);
        if (it != catMap.end()) return it->second;
        return std::make_pair(0ULL, 0LL);
    };

    auto p3d = getCat("other");
    auto pAud = getCat("audio");
    auto pImg = getCat("foto");
    if (pImg.first == 0) pImg = getCat("imagem");
    auto pVid = getCat("video");

    FormatCategoryStat c1; c1.categoryName = "3D & Other"; c1.assetCount = p3d.first; c1.sizeBytes = p3d.second; c1.color = juce::Colour(0xffe05638);
    FormatCategoryStat c2; c2.categoryName = "Audio"; c2.assetCount = pAud.first; c2.sizeBytes = pAud.second; c2.color = juce::Colour(0xff2b9348);
    FormatCategoryStat c3; c3.categoryName = "Photo / Image"; c3.assetCount = pImg.first; c3.sizeBytes = pImg.second; c3.color = juce::Colour(0xffe9c46a);
    FormatCategoryStat c4; c4.categoryName = "Video"; c4.assetCount = pVid.first; c4.sizeBytes = pVid.second; c4.color = juce::Colour(0xff9d4edd);

    d.categories = {c1, c2, c3, c4};

    // Primary Format
    FormatCategoryStat topCat = c1;
    for (const auto& cat : d.categories) {
        if (cat.assetCount > topCat.assetCount) topCat = cat;
    }
    d.primaryFormatName = topCat.categoryName;
    d.primaryFormatCount = topCat.assetCount;

    // 4. Top File Extensions
    auto stmtExt = db.prepare("SELECT LOWER(SUBSTR(caminho_relativo, INSTR(caminho_relativo, '.'))) AS ext, COUNT(*), SUM(tamanho_bytes) "
                              "FROM arquivo WHERE eh_master = 1 AND caminho_relativo LIKE '%.%' "
                              "GROUP BY ext ORDER BY COUNT(*) DESC LIMIT 7");
    while (stmtExt.step()) {
        FileExtensionStat ext;
        std::string extName = stmtExt.columnText(0);
        if (!extName.empty() && extName[0] != '.') extName = "." + extName;
        ext.extension = juce::String(extName).toUpperCase().toStdString();
        ext.count = static_cast<uint64_t>(stmtExt.columnInt(1));
        ext.sizeBytes = static_cast<juce::int64>(stmtExt.columnInt(2));
        d.topExtensions.push_back(ext);
    }

    // 5. Timeline & Decade Distribution
    auto stmtDec = db.prepare("SELECT (CAST(SUBSTR(criado_em, 1, 4) AS INT) / 10 * 10) AS decade, COUNT(*) "
                              "FROM item WHERE criado_em IS NOT NULL AND LENGTH(criado_em) >= 4 "
                              "GROUP BY decade ORDER BY decade ASC");
    while (stmtDec.step()) {
        int dec = stmtDec.columnInt(0);
        uint64_t cnt = static_cast<uint64_t>(stmtDec.columnInt(1));
        if (dec > 1800 && dec < 2100) {
            d.decadeDistribution.push_back({std::to_string(dec) + "s", cnt});
        }
    }
    if (d.decadeDistribution.empty()) {
        d.decadeDistribution.push_back({"2000s", d.totalAssets});
    }

    // 6. Backup Health & Vault Status
    d.vulnerableAssetsCount = d.totalAssets;
    d.backupHealthPercentage = 0.0;
    d.singleCopyVulnerableCount = d.totalAssets;

    updateData(d);
}

void CollectionStatsDashboardComponent::paint(juce::Graphics& g) {
    // Warm Sand / Clean Dashboard Background (Matching User UI Theme)
    g.fillAll(juce::Colour(0xfff5f0eb));

    auto area = getLocalBounds().reduced(20, 16);

    // Section Header Title
    g.setColour(juce::Colour(0xff1e293b));
    g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    g.drawText("Collection Analytics & Statistics", area.removeFromTop(24), juce::Justification::centredLeft);
    area.removeFromTop(12);

    // --- 1. TOP 4 KPI CARDS ---
    auto cardArea = area.removeFromTop(80);
    int cardW = (cardArea.getWidth() - 3 * 12) / 4;

    auto drawKpiCard = [&](juce::Rectangle<int> r, const std::string& label, const std::string& value, const std::string& subtitle, juce::Colour accentColor) {
        g.setColour(juce::Colour(0xffeae3da)); // Card background
        g.fillRoundedRectangle(r.toFloat(), 6.0f);

        // Accent strip on left
        auto strip = r.removeFromLeft(4);
        g.setColour(accentColor);
        g.fillRoundedRectangle(strip.toFloat(), 3.0f);

        auto inner = r.reduced(10, 8);
        g.setColour(juce::Colour(0xff64748b));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(label, inner.removeFromTop(14), juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff0f172a));
        g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
        g.drawText(value, inner.removeFromTop(24), juce::Justification::centredLeft, true);

        g.setColour(juce::Colour(0xff64748b));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::plain)));
        g.drawText(subtitle, inner, juce::Justification::centredLeft, true);
    };

    // Card 1: Total Assets
    auto r1 = cardArea.removeFromLeft(cardW); cardArea.removeFromLeft(12);
    drawKpiCard(r1, "TOTAL ASSETS", std::to_string(data_.totalAssets), "Cataloged files", juce::Colour(0xff3b82f6));

    // Card 2: Storage Size
    auto r2 = cardArea.removeFromLeft(cardW); cardArea.removeFromLeft(12);
    std::string szStr = juce::File::descriptionOfSizeInBytes(data_.totalSizeBytes).toStdString();
    drawKpiCard(r2, "STORAGE SIZE", szStr, "Occupied volume", juce::Colour(0xff10b981));

    // Card 3: Primary Format
    auto r3 = cardArea.removeFromLeft(cardW); cardArea.removeFromLeft(12);
    drawKpiCard(r3, "PRIMARY FORMAT", data_.primaryFormatName, std::to_string(data_.primaryFormatCount) + " assets", juce::Colour(0xfff59e0b));

    // Card 4: Backup Health
    auto r4 = cardArea;
    std::ostringstream ssHp; ssHp << std::fixed << std::setprecision(0) << data_.backupHealthPercentage << "%";
    drawKpiCard(r4, "BACKUP HEALTH", ssHp.str(), std::to_string(data_.vulnerableAssetsCount) + " vulnerable", juce::Colour(0xffef4444));

    area.removeFromTop(20);

    // --- 2. TWO-COLUMN DISTRIBUTION CHARTS ---
    int colW = (area.getWidth() - 24) / 2;
    auto leftCol = area.removeFromLeft(colW);
    auto rightCol = area;

    // LEFT COLUMN: Format Category Distribution & Top File Extensions
    g.setColour(juce::Colour(0xff1e293b));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText("Format Category Distribution", leftCol.removeFromTop(20), juce::Justification::centredLeft);
    leftCol.removeFromTop(8);

    uint64_t maxCatCount = 1;
    for (const auto& cat : data_.categories) maxCatCount = std::max(maxCatCount, cat.assetCount);

    for (const auto& cat : data_.categories) {
        auto row = leftCol.removeFromTop(26);
        leftCol.removeFromTop(6);

        std::string labelStr = cat.categoryName + " (" + std::to_string(cat.assetCount) + " assets - " +
                               juce::File::descriptionOfSizeInBytes(cat.sizeBytes).toStdString() + ")";

        g.setColour(juce::Colour(0xff334155));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(labelStr, row.removeFromTop(14), juce::Justification::centredLeft, true);

        // Bar container
        g.setColour(juce::Colour(0xffeae3da));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);

        float pct = static_cast<float>(cat.assetCount) / static_cast<float>(maxCatCount);
        int barW = juce::jmax(8, static_cast<int>(row.getWidth() * pct));
        g.setColour(cat.color);
        g.fillRoundedRectangle(row.withWidth(barW).toFloat(), 4.0f);
    }

    leftCol.removeFromTop(16);

    // Top File Extensions
    g.setColour(juce::Colour(0xff1e293b));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText("Top File Extensions", leftCol.removeFromTop(20), juce::Justification::centredLeft);
    leftCol.removeFromTop(6);

    for (const auto& ext : data_.topExtensions) {
        if (leftCol.getHeight() < 18) break;
        auto row = leftCol.removeFromTop(18);

        std::string extLine = ext.extension + "   " + std::to_string(ext.count) + " files  |  " +
                              juce::File::descriptionOfSizeInBytes(ext.sizeBytes).toStdString();

        g.setColour(juce::Colour(0xff3b82f6));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(extLine, row, juce::Justification::centredLeft, true);
    }

    // RIGHT COLUMN: Timeline & Year Distribution & Storage Status
    g.setColour(juce::Colour(0xff1e293b));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText("Timeline & Year Distribution", rightCol.removeFromTop(20), juce::Justification::centredLeft);
    rightCol.removeFromTop(8);

    uint64_t maxDecCount = 1;
    for (const auto& dec : data_.decadeDistribution) maxDecCount = std::max(maxDecCount, dec.second);

    for (const auto& dec : data_.decadeDistribution) {
        auto row = rightCol.removeFromTop(24);
        rightCol.removeFromTop(6);

        g.setColour(juce::Colour(0xff334155));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(dec.first, row.removeFromLeft(50), juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xffeae3da));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);

        float pct = static_cast<float>(dec.second) / static_cast<float>(maxDecCount);
        int barW = juce::jmax(8, static_cast<int>(row.getWidth() * pct));
        g.setColour(juce::Colour(0xff3b82f6));
        g.fillRoundedRectangle(row.withWidth(barW).toFloat(), 4.0f);
    }

    rightCol.removeFromTop(20);

    // Storage & Protection Status
    g.setColour(juce::Colour(0xff1e293b));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText("Storage & Protection Status", rightCol.removeFromTop(20), juce::Justification::centredLeft);
    rightCol.removeFromTop(8);

    auto drawStatusDotRow = [&](juce::Colour dotColor, const std::string& text) {
        auto r = rightCol.removeFromTop(18);
        rightCol.removeFromTop(4);

        g.setColour(dotColor);
        g.fillEllipse(r.removeFromLeft(12).reduced(2).toFloat());
        r.removeFromLeft(6);

        g.setColour(juce::Colour(0xff334155));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(text, r, juce::Justification::centredLeft, true);
    };

    drawStatusDotRow(juce::Colour(0xff10b981), "Backed up in multiple vaults: " + std::to_string(data_.backedUpVaultsCount) + " assets");
    drawStatusDotRow(juce::Colour(0xfff59e0b), "Single copy (Vulnerable): " + std::to_string(data_.singleCopyVulnerableCount) + " assets");
    drawStatusDotRow(juce::Colour(0xffef4444), "Offline or unverified: " + std::to_string(data_.offlineUnverifiedCount) + " assets");
}

void CollectionStatsDashboardComponent::resized() {
    repaint();
}

} // namespace matriz::ui
