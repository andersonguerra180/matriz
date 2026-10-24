#include "EstatisticasComponent.h"
#include "VulnerabilidadesDialog.h"
#include "Tokens.h"
#include "ProgressoGlobal.h"
#include "../Ingest/LeituraTecnica.h"
#include "../I18n/Strings.h"
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
        bool isLight = (tk.fundo.getBrightness() > 0.5f);
        juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
        g.fillAll(bg);

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
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(i18n::t("analytics.cat_total"), totalInner.removeFromTop(18), juce::Justification::left);

        totalInner.removeFromTop(6);
        desenharLinhaMetricas(g, totalInner, owner_.catalogTotalKpi_, true);

        y += 126;

        // 2. Section Header for Collections
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        juce::String colTitle = juce::String(i18n::t("analytics.colecoes")).replace("{n}", juce::String(owner_.colecoesKpi_.size()));
        g.drawText(colTitle, 16, y, w, 22, juce::Justification::left);
        y += 30;

        if (owner_.colecoesKpi_.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
            g.drawText(i18n::t("analytics.sem_colecoes"), 16, y, w, 30, juce::Justification::left);
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
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
            g.drawText(juce::String(i18n::t("analytics.colecao_prefixo")) + kpi.name.toUpperCase(), headerArea.removeFromLeft(headerArea.getWidth() / 2), juce::Justification::left, true);

            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
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
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
            g.drawText(label, inner.removeFromTop(14), juce::Justification::left);

            g.setColour(color);
            g.setFont(juce::Font(juce::FontOptions(isTotal ? 16.0f : 14.0f, juce::Font::bold)));
            g.drawText(val, inner, juce::Justification::left);

            x += itemW;
        };

        drawMetric(i18n::t("analytics.total_assets"), juce::String(kpi.totalAssets), isTotal ? tk.acento : tk.textoPrimario);
        drawMetric(i18n::t("analytics.storage_size"), formatSizeHuman(kpi.totalBytes), juce::Colour(0xff10b981));
        drawMetric(i18n::t("analytics.primary_format"), kpi.primaryFormatName.empty() ? "None" : juce::String(kpi.primaryFormatName), juce::Colour(0xfff59e0b));
        drawMetric(i18n::t("analytics.needs_attention"), juce::String(kpi.needsAttentionCount), kpi.needsAttentionCount > 0 ? juce::Colour(0xfff97316) : tk.textoSecundario);
        drawMetric(i18n::t("analytics.backup_health"), juce::String(kpi.backupHealthPercentage, 0) + "%", juce::Colour(0xff3b82f6));
    }

    void recalculateHeight() {
        int h = 180 + static_cast<int>(owner_.colecoesKpi_.size()) * 114 + 40;
        setSize(getParentWidth(), std::max(h, getParentHeight()));
    }

private:
    EstatisticasComponent& owner_;
};

class EstatisticasComponent::ItemPastaSpaceMap : public juce::TreeViewItem {
public:
    ItemPastaSpaceMap(std::string id, juce::String nome, int itens,
                      std::function<void(const std::string&)> aoSelecionar)
        : id_(std::move(id)), nome_(std::move(nome)), itens_(itens),
          aoSelecionar_(std::move(aoSelecionar)) {}

    juce::String texto() const {
        return juce::String::fromUTF8("\xf0\x9f\x93\x81 ") + nome_ + (itens_ > 0 ? "  (" + juce::String(itens_) + ")" : juce::String());
    }

    static juce::Font fonte(bool negrito) {
        return juce::Font(juce::FontOptions(tema().tamanhoFontePequena, negrito ? juce::Font::bold : juce::Font::plain));
    }

    int getItemWidth() const override {
        return juce::GlyphArrangement::getStringWidthInt(fonte(true), texto()) + 20;
    }

    const std::string& id() const { return id_; }
    const juce::String& nome() const { return nome_; }
    bool mightContainSubItems() override { return getNumSubItems() > 0; }
    juce::String getUniqueName() const override { return id_.empty() ? juce::String("raiz") : juce::String(id_); }
    int getItemHeight() const override { return 22; }

    void paintItem(juce::Graphics& g, int width, int height) override {
        const auto& tk = tema();
        if (isSelected()) {
            g.setColour(tk.acento.withAlpha(0.25f));
            g.fillRoundedRectangle(juce::Rectangle<float>(0, 1, width, height - 2), 4.0f);
            g.setColour(tk.acento);
            g.drawRoundedRectangle(juce::Rectangle<float>(0, 1, width, height - 2), 4.0f, 1.0f);
        }
        g.setColour(isSelected() ? tk.textoPrimario : tk.textoSecundario);
        g.setFont(fonte(isSelected()));
        g.drawText(texto(), juce::Rectangle<int>(4, 0, width - 4, height), juce::Justification::centredLeft, false);
    }

    void itemSelectionChanged(bool selecionado) override {
        if (selecionado && aoSelecionar_ && !id_.empty()) {
            aoSelecionar_(id_);
        }
    }

private:
    std::string id_;
    juce::String nome_;
    int itens_;
    std::function<void(const std::string&)> aoSelecionar_;
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
        treeExplorador_.setRootItemVisible(false);
        treeExplorador_.setDefaultOpenness(true);
        treeExplorador_.setIndentSize(14);
        treeExplorador_.getViewport()->setScrollBarsShown(true, true);
        treeExplorador_.setColour(juce::TreeView::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(treeExplorador_);

        addAndMakeVisible(btnCreateSpaceMap_);
        btnCreateSpaceMap_.setColour(juce::TextButton::buttonColourId, tema().acento);
        btnCreateSpaceMap_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btnCreateSpaceMap_.onClick = [this] { executarScanPasta(); };

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
        treemapComponent_.limpar();
    }

    recarregar();
}

EstatisticasComponent::~EstatisticasComponent() = default;

void EstatisticasComponent::lookAndFeelChanged() {
    treemapComponent_.sendLookAndFeelChange();
    treemapComponent_.repaint();
    if (catalogAnalyticsContent_) {
        catalogAnalyticsContent_->repaint();
    }
    btnCreateSpaceMap_.setColour(juce::TextButton::buttonColourId, tema().acento);
    btnCreateSpaceMap_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    repaint();
}

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
        treeExplorador_.setVisible(false);
        btnCreateSpaceMap_.setVisible(false);
        treemapComponent_.setVisible(false);
        catalogAnalyticsViewport_->setVisible(true);
        carregarMetricasCatalogo();
        if (catalogAnalyticsContent_) {
            catalogAnalyticsContent_->recalculateHeight();
            catalogAnalyticsContent_->repaint();
        }
    } else {
        if (catalogAnalyticsViewport_) catalogAnalyticsViewport_->setVisible(false);
        treeExplorador_.setVisible(true);
        btnCreateSpaceMap_.setVisible(true);
        treemapComponent_.setVisible(true);
        reconstruirExplorador();
        if (spaceMapCarregado_ && !pastaEscaneadaId_.empty()) {
            auto itens = obterItensDaPastaRecursivo(pastaEscaneadaId_);
            matriz::db::Database& db = projeto_.projeto().registroLeitura();
            treemapComponent_.recarregarDoBanco(db, itens);
        } else {
            treemapComponent_.limpar();
        }
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
        juce::File dbFile = matriz::model::Project::resolverPastaProjeto(colDir).getChildFile("registro.sqlite");
        if (dbFile.existsAsFile()) {
            try {
                matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                std::map<std::string, uint64_t> formatCounts;
                auto stmt = colDb.prepare(
                    "SELECT i.id, IFNULL(a.caminho_relativo, IFNULL(a.caminho_absoluto_origem, '')), IFNULL(a.tamanho_bytes, 0) "
                    "FROM item i "
                    "LEFT JOIN arquivo a ON a.item_id = i.id AND a.eh_master = 1 "
                    "WHERE COALESCE(i.estado, '') != 'duplicata'");

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
                    "WHERE COALESCE(estado, '') != 'duplicata' AND ((ano IS NULL OR ano = 0) "
                    "   OR (source_media IS NULL OR TRIM(source_media) = '') "
                    "   OR (collection_type IS NULL OR TRIM(collection_type) = ''))");
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

juce::Colour EstatisticasComponent::corDeFundo() {
    const auto& tk = tema();
    bool isLight = (tk.fundo.getBrightness() > 0.5f);
    return (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
}

void EstatisticasComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(corDeFundo());

    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    if (isCatalogMode) {
        return; // Content is drawn inside catalogAnalyticsViewport_
    }

    auto area = getLocalBounds().reduced(16, 12);

    // Section Header Title
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    g.drawText(i18n::t("analytics.titulo"), area.removeFromTop(24), juce::Justification::centredLeft);

    // Card do Finder-Explorer à esquerda
    if (!cardExplorerBounds_.isEmpty()) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(cardExplorerBounds_.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cardExplorerBounds_.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);

        // Cabeçalho do Card FOLDERS
        auto headerExp = cardExplorerBounds_.reduced(12, 10).removeFromTop(22);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        g.drawText("FOLDERS", headerExp, juce::Justification::centredLeft);
    }

    // Card do Mosaico (Treemap) à direita
    if (!cardTreemapBounds_.isEmpty()) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(cardTreemapBounds_.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cardTreemapBounds_.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
    }
}

void EstatisticasComponent::mouseDown(const juce::MouseEvent&) {
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
        area.removeFromTop(24); // Header
        area.removeFromTop(8);  // Gap

        // Card do Explorer à esquerda (largura 270px)
        const int explorerW = 270;
        cardExplorerBounds_ = area.removeFromLeft(explorerW);
        area.removeFromLeft(12); // Gap

        auto innerExp = cardExplorerBounds_.reduced(12, 12);
        innerExp.removeFromTop(24); // Rótulo "FOLDERS"
        innerExp.removeFromTop(6);

        // Botão no rodapé dentro do próprio card
        btnCreateSpaceMap_.setBounds(innerExp.removeFromBottom(36));
        innerExp.removeFromBottom(8);

        treeExplorador_.setBounds(innerExp);

        // Card do Treemap à direita
        cardTreemapBounds_ = area;
        treemapComponent_.setBounds(cardTreemapBounds_.reduced(2));
    }
}

void EstatisticasComponent::reconstruirExplorador() {
    auto arvore = projeto_.arvoreAcervo(projeto_.mapaInicialDoFolderMap());

    std::function<std::unique_ptr<ItemPastaSpaceMap>(const ProjetoAberto::NoArvore&)> montar =
        [&](const ProjetoAberto::NoArvore& no) -> std::unique_ptr<ItemPastaSpaceMap> {
            auto item = std::make_unique<ItemPastaSpaceMap>(
                no.id, no.nome, static_cast<int>(no.itemIds.size()),
                [this](const std::string& pId) {
                    pastaSelecionadaId_ = pId;
                });
            for (const auto& filho : no.filhos) {
                item->addSubItem(montar(filho).release());
            }
            return item;
        };

    treeExplorador_.setRootItem(nullptr);
    raizExplorador_ = std::make_unique<ItemPastaSpaceMap>(std::string(), "Root", 0, nullptr);
    for (const auto& filho : arvore.filhos) {
        raizExplorador_->addSubItem(montar(filho).release());
    }
    treeExplorador_.setRootItem(raizExplorador_.get());

    if (!pastaSelecionadaId_.empty()) {
        selecionarPastaNoExplorer(pastaSelecionadaId_);
    } else if (raizExplorador_->getNumSubItems() > 0) {
        if (auto* prim = raizExplorador_->getSubItem(0)) {
            prim->setSelected(true, true);
        }
    }
}

void EstatisticasComponent::selecionarPastaNoExplorer(const std::string& pastaId) {
    if (pastaId.empty() || !raizExplorador_) return;

    std::function<bool(juce::TreeViewItem*)> procurar = [&](juce::TreeViewItem* item) -> bool {
        if (auto* p = dynamic_cast<ItemPastaSpaceMap*>(item)) {
            if (p->id() == pastaId) {
                juce::TreeViewItem* pai = p->getParentItem();
                while (pai != nullptr) {
                    pai->setOpen(true);
                    pai = pai->getParentItem();
                }
                p->setSelected(true, true);
                treeExplorador_.scrollToKeepItemVisible(p);
                return true;
            }
        }
        for (int i = 0; i < item->getNumSubItems(); ++i) {
            if (procurar(item->getSubItem(i))) return true;
        }
        return false;
    };

    procurar(raizExplorador_.get());
}

void EstatisticasComponent::definirPastaSelecionada(const std::string& pastaId) {
    if (pastaId.empty()) return;
    pastaSelecionadaId_ = pastaId;
    selecionarPastaNoExplorer(pastaId);
}

juce::String EstatisticasComponent::obterNomeDaPasta(const std::string& pastaId) const {
    if (pastaId.empty()) return {};
    try {
        matriz::db::Database& db = projeto_.projeto().registroLeitura();
        auto stmt = db.prepare("SELECT nome FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(pastaId));
        if (stmt.step()) return juce::String::fromUTF8(stmt.columnText(0).c_str());
    } catch (...) {}
    return {};
}

void EstatisticasComponent::executarScanPasta() {
    if (pastaSelecionadaId_.empty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Space Map",
            "Please select a folder in the explorer to create the Space map.");
        return;
    }

    juce::String nomePasta = obterNomeDaPasta(pastaSelecionadaId_);
    if (nomePasta.isEmpty()) nomePasta = "Selected Folder";

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "spacemap_scan",
        "Space Map",
        100,
        nullptr,
        "Scanning folder " + nomePasta + "...");

    juce::Component::SafePointer<EstatisticasComponent> seguro(this);
    juce::Timer::callAfterDelay(150, [seguro] {
        if (seguro == nullptr) return;
        auto itens = seguro->obterItensDaPastaRecursivo(seguro->pastaSelecionadaId_);
        matriz::db::Database& db = seguro->projeto_.projeto().registroLeitura();
        seguro->treemapComponent_.recarregarDoBanco(db, itens);
        seguro->pastaEscaneadaId_ = seguro->pastaSelecionadaId_;
        seguro->spaceMapCarregado_ = true;
        ProgressoGlobal::obterInstancia().concluirTarefa("spacemap_scan");
        seguro->repaint();
    });
}

std::set<std::string> EstatisticasComponent::obterItensDaPastaRecursivo(const std::string& pastaRaizId) {
    std::set<std::string> itens;
    if (pastaRaizId.empty()) return itens;

    std::set<std::string> pastasParaVisitar = { pastaRaizId };
    std::set<std::string> pastasVisitadas;
    matriz::db::Database& db = projeto_.projeto().registroLeitura();

    while (!pastasParaVisitar.empty()) {
        auto it = pastasParaVisitar.begin();
        std::string pId = *it;
        pastasParaVisitar.erase(it);
        pastasVisitadas.insert(pId);

        try {
            auto stmtItens = db.prepare("SELECT item_id FROM acervo_item_pasta WHERE pasta_id = ?");
            stmtItens.bind(1, matriz::db::Value::of(pId));
            while (stmtItens.step()) {
                itens.insert(stmtItens.columnText(0));
            }
        } catch (...) {}

        try {
            auto stmtSub = db.prepare("SELECT id FROM acervo_pasta WHERE pasta_pai_id = ?");
            stmtSub.bind(1, matriz::db::Value::of(pId));
            while (stmtSub.step()) {
                std::string subId = stmtSub.columnText(0);
                if (pastasVisitadas.count(subId) == 0) {
                    pastasParaVisitar.insert(subId);
                }
            }
        } catch (...) {}
    }
    return itens;
}

} // namespace matriz::ui
