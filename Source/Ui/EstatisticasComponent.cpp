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

class EstatisticasComponent::CatalogAnalyticsContent : public juce::Component {
public:
    CatalogAnalyticsContent(EstatisticasComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.fundo);

        int y = 16;
        int w = getWidth() - 32;

        // 1. Draw CATALOG TOTAL Top Block
        auto totalRect = juce::Rectangle<int>(16, y, w, 110);
        g.setColour(tk.painel);
        g.fillRoundedRectangle(totalRect.toFloat(), 6.0f);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(totalRect.toFloat(), 6.0f, 2.0f);

        auto totalInner = totalRect.reduced(16, 12);
        g.setColour(tk.acento);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText("CATALOG TOTAL (AGGREGATED)", totalInner.removeFromTop(18), juce::Justification::left);

        totalInner.removeFromTop(6);
        desenharLinhaMetricas(g, totalInner, owner_.catalogTotalKpi_, true);

        y += 126;

        // 2. Section Header for Collections
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText("COLLECTIONS (" + juce::String(owner_.colecoesKpi_.size()) + ")", 16, y, w, 22, juce::Justification::left);
        y += 30;

        if (owner_.colecoesKpi_.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText("No linked collections yet. Import collections in the COLLECTIONS tab to view multi-collection analytics.", 16, y, w, 30, juce::Justification::left);
            return;
        }

        // 3. Draw each collection's KPI block
        for (const auto& kpi : owner_.colecoesKpi_) {
            auto colRect = juce::Rectangle<int>(16, y, w, 100);
            g.setColour(tk.painel);
            g.fillRoundedRectangle(colRect.toFloat(), 6.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(colRect.toFloat(), 6.0f, 1.0f);

            auto inner = colRect.reduced(16, 10);
            
            auto headerArea = inner.removeFromTop(18);
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            g.drawText("COLLECTION: " + kpi.name.toUpperCase(), headerArea.removeFromLeft(headerArea.getWidth() / 2), juce::Justification::left, true);

            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(kpi.path, headerArea, juce::Justification::right, true);

            inner.removeFromTop(6);
            desenharLinhaMetricas(g, inner, kpi, false);

            y += 114;
        }
    }

    void desenharLinhaMetricas(juce::Graphics& g, const juce::Rectangle<int>& area, const CollectionKpi& kpi, bool isTotal) {
        const auto& tk = tema();
        int itemW = area.getWidth() / 5;
        int x = area.getX();
        int y = area.getY();
        int h = area.getHeight();

        auto drawMetric = [&](const juce::String& label, const juce::String& val, juce::Colour color) {
            auto r = juce::Rectangle<int>(x, y, itemW - 12, h);
            g.setColour(tk.painelAlt);
            g.fillRoundedRectangle(r.toFloat(), 4.0f);

            auto inner = r.reduced(8, 4);
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
            g.drawText(label, inner.removeFromTop(14), juce::Justification::left);

            g.setColour(color);
            g.setFont(juce::Font(juce::FontOptions(isTotal ? 16.0f : 14.0f, juce::Font::bold)));
            g.drawText(val, inner, juce::Justification::left);

            x += itemW;
        };

        drawMetric("TOTAL ASSETS", juce::String(kpi.totalAssets), isTotal ? tk.acento : tk.textoPrimario);
        drawMetric("STORAGE SIZE", formatSizeHuman(kpi.totalBytes), juce::Colour(0xff10b981));
        drawMetric("PRIMARY FORMAT", kpi.primaryFormatName.empty() ? "None" : juce::String(kpi.primaryFormatName), juce::Colour(0xfff59e0b));
        drawMetric("NEEDS ATTENTION", juce::String(kpi.needsAttentionCount), kpi.needsAttentionCount > 0 ? juce::Colour(0xfff97316) : tk.textoSecundario);
        drawMetric("BACKUP HEALTH", juce::String(kpi.backupHealthPercentage, 0) + "%", juce::Colour(0xff3b82f6));
    }

    void recalculateHeight() {
        int h = 180 + static_cast<int>(owner_.colecoesKpi_.size()) * 114 + 40;
        setSize(getParentWidth(), std::max(h, getParentHeight()));
    }

private:
    EstatisticasComponent& owner_;
};

EstatisticasComponent::EstatisticasComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    if (isCatalogMode) {
        catalogAnalyticsContent_ = std::make_unique<CatalogAnalyticsContent>(*this);
        catalogAnalyticsViewport_ = std::make_unique<juce::Viewport>();
        catalogAnalyticsViewport_->setViewedComponent(catalogAnalyticsContent_.get(), false);
        catalogAnalyticsViewport_->setScrollBarsShown(true, false);
        addAndMakeVisible(*catalogAnalyticsViewport_);
    } else {
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
    }

    recarregar();
}

EstatisticasComponent::~EstatisticasComponent() = default;

void EstatisticasComponent::setSelectedAssets(const std::set<std::string>&) {
}

void EstatisticasComponent::recarregar() {
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    if (isCatalogMode) {
        if (!catalogAnalyticsViewport_) {
            catalogAnalyticsContent_ = std::make_unique<CatalogAnalyticsContent>(*this);
            catalogAnalyticsViewport_ = std::make_unique<juce::Viewport>();
            catalogAnalyticsViewport_->setViewedComponent(catalogAnalyticsContent_.get(), false);
            catalogAnalyticsViewport_->setScrollBarsShown(true, false);
            addAndMakeVisible(*catalogAnalyticsViewport_);
        }
        treemapComponent_.setVisible(false);
        catalogAnalyticsViewport_->setVisible(true);
        carregarMetricasCatalogo();
        if (catalogAnalyticsContent_) {
            catalogAnalyticsContent_->recalculateHeight();
            catalogAnalyticsContent_->repaint();
        }
    } else {
        if (catalogAnalyticsViewport_) catalogAnalyticsViewport_->setVisible(false);
        treemapComponent_.setVisible(true);
        matriz::db::Database& db = projeto_.projeto().registro();
        carregarMetricasDoBanco(db);
        treemapComponent_.recarregarDoBanco(db);
    }
    repaint();
}

void EstatisticasComponent::carregarMetricasCatalogo() {
    colecoesKpi_.clear();
    catalogTotalKpi_ = CollectionKpi{};
    catalogTotalKpi_.name = "CATALOG TOTAL";

    auto colecoes = projeto_.listarColecoesLinkadas();
    std::map<std::string, uint64_t> globalFormatCounts;

    for (const auto& c : colecoes) {
        if (!c.valido) continue;
        CollectionKpi kpi;
        kpi.name = c.nome;
        kpi.path = c.caminhoProjeto;

        juce::File colDir(c.caminhoProjeto);
        juce::File dbFile = colDir.getChildFile("registro.sqlite");
        if (dbFile.existsAsFile()) {
            try {
                matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                std::map<std::string, uint64_t> formatCounts;
                auto stmt = colDb.prepare(
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
                            ext = juce::String(pathStr.substr(dotIdx + 1)).toLowerCase();
                        }
                    }
                    std::string format = ext.isEmpty() ? "No Format" : ext.toUpperCase().toStdString();
                    formatCounts[format]++;
                    globalFormatCounts[format]++;

                    kpi.totalAssets++;
                    kpi.totalBytes += sizeBytes;
                }

                std::string topFmt = "Audio";
                uint64_t topCnt = 0;
                for (const auto& [fmt, cnt] : formatCounts) {
                    if (cnt > topCnt) {
                        topCnt = cnt;
                        topFmt = fmt;
                    }
                }
                kpi.primaryFormatName = topFmt;
                kpi.primaryFormatCount = topCnt;

                auto stmtRev = colDb.prepare(
                    "SELECT COUNT(id) FROM item "
                    "WHERE (ano IS NULL OR ano = 0) "
                    "   OR (source_media IS NULL OR TRIM(source_media) = '') "
                    "   OR (collection_type IS NULL OR TRIM(collection_type) = '')");
                if (stmtRev.step()) {
                    kpi.needsAttentionCount = static_cast<uint64_t>(stmtRev.columnInt(0));
                }

                kpi.backupHealthPercentage = 100.0;
            } catch (...) {}
        }

        catalogTotalKpi_.totalAssets += kpi.totalAssets;
        catalogTotalKpi_.totalBytes += kpi.totalBytes;
        catalogTotalKpi_.needsAttentionCount += kpi.needsAttentionCount;
        colecoesKpi_.push_back(kpi);
    }

    std::string topGlobalFmt = "Audio";
    uint64_t topGlobalCnt = 0;
    for (const auto& [fmt, cnt] : globalFormatCounts) {
        if (cnt > topGlobalCnt) {
            topGlobalCnt = cnt;
            topGlobalFmt = fmt;
        }
    }
    catalogTotalKpi_.primaryFormatName = topGlobalFmt;
    catalogTotalKpi_.primaryFormatCount = topGlobalCnt;
    catalogTotalKpi_.backupHealthPercentage = 100.0;
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

    // Calculate Needs Attention (§ B.2)
    needsAttentionIds_.clear();
    try {
        auto stmtRev = db.prepare(
            "SELECT id FROM item "
            "WHERE (ano IS NULL OR ano = 0) "
            "   OR (source_media IS NULL OR TRIM(source_media) = '') "
            "   OR (collection_type IS NULL OR TRIM(collection_type) = '')");
        while (stmtRev.step()) {
            needsAttentionIds_.insert(stmtRev.columnText(0));
        }
        summaryKpi_.needsAttentionCount = static_cast<uint64_t>(needsAttentionIds_.size());
    } catch (...) {}
}

void EstatisticasComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    if (isCatalogMode) {
        return; // Content is drawn inside catalogAnalyticsViewport_
    }

    auto area = getLocalBounds().reduced(16, 12);

    // Section Header Title (§ A.3 - Treemap Explorer)
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    g.drawText("Collection Analytics - Treemap Explorer", area.removeFromTop(22), juce::Justification::left);
    area.removeFromTop(8);

    // Top 5 KPI Cards Area (Top 70px)
    auto topKpiArea = area.removeFromTop(70);
    desenharTopKpiCards(g, topKpiArea);
}

void EstatisticasComponent::desenharTopKpiCards(juce::Graphics& g, const juce::Rectangle<int>& area) {
    const auto& tk = tema();
    int cardW = (area.getWidth() - 48) / 5;
    int cardH = area.getHeight();

    auto drawCard = [&](int x, const juce::String& title, const juce::String& val, const juce::String& subtitle, juce::Colour accentColor) -> juce::Rectangle<int> {
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

        return juce::Rectangle<int>(x, area.getY(), cardW, cardH);
    };

    int x = area.getX();
    drawCard(x, "TOTAL ASSETS", juce::String(summaryKpi_.totalAssets), "Cataloged files", juce::Colour(0xff3b82f6)); x += cardW + 12;
    drawCard(x, "STORAGE SIZE", formatSizeHuman(summaryKpi_.totalBytes), "Occupied volume", juce::Colour(0xff10b981)); x += cardW + 12;
    drawCard(x, "PRIMARY FORMAT", juce::String(summaryKpi_.primaryFormatName), juce::String(summaryKpi_.primaryFormatCount) + " assets", juce::Colour(0xfff59e0b)); x += cardW + 12;
    needsAttentionCardBounds_ = drawCard(x, "NEEDS ATTENTION", juce::String(summaryKpi_.needsAttentionCount), "files missing required metadata", juce::Colour(0xfff97316)); x += cardW + 12;
    drawCard(x, "BACKUP HEALTH", juce::String(summaryKpi_.backupHealthPercentage, 0) + "%", juce::String(summaryKpi_.vulnerableAssetsCount) + " vulnerable", juce::Colour(0xffef4444));
}

void EstatisticasComponent::mouseDown(const juce::MouseEvent& e) {
    if (needsAttentionCardBounds_.contains(e.getPosition())) {
        if (aoClicarNeedsAttention) {
            aoClicarNeedsAttention(needsAttentionIds_);
        } else if (aoAbrirNoGrid) {
            aoAbrirNoGrid(needsAttentionIds_);
        }
    }
}

void EstatisticasComponent::resized() {
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    if (isCatalogMode) {
        if (catalogAnalyticsViewport_) {
            catalogAnalyticsViewport_->setBounds(getLocalBounds());
            if (catalogAnalyticsContent_) {
                catalogAnalyticsContent_->recalculateHeight();
            }
        }
    } else {
        auto area = getLocalBounds().reduced(16, 12);
        area.removeFromTop(22); // Header
        area.removeFromTop(8);  // Gap
        area.removeFromTop(70); // KPI cards
        area.removeFromTop(12); // Gap

        treemapComponent_.setBounds(area);
    }
}

} // namespace matriz::ui
