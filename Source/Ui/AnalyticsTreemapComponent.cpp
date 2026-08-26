#include "AnalyticsTreemapComponent.h"
#include "Tokens.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace matriz::ui {

AnalyticsTreemapComponent::AnalyticsTreemapComponent() {
    addAndMakeVisible(btnUp_);
    addAndMakeVisible(btnRoot_);

    btnUp_.onClick = [this] {
        if (noAtual_ && noAtual_->parent) {
            noAtual_ = noAtual_->parent;
            recalcularLayout();
            repaint();
        }
    };

    btnRoot_.onClick = [this] {
        resetarNavegacao();
    };
}

void AnalyticsTreemapComponent::resetarNavegacao() {
    if (rootNode_) {
        noAtual_ = rootNode_.get();
        recalcularLayout();
        repaint();
    }
}

std::string AnalyticsTreemapComponent::inferirCategoria(const std::string& mediaType, const std::string& ext) const {
    juce::String mt = juce::String(mediaType).toLowerCase().trim();
    juce::String e = juce::String(ext).toLowerCase().trim();

    if (mt == "audio" || e == "wav" || e == "aiff" || e == "aif" || e == "flac" || e == "mp3" || e == "m4a" || e == "ogg" || e == "aac") {
        return "AUDIO";
    }
    if (mt == "video" || mt == "digital_video" || e == "mp4" || e == "mov" || e == "mkv" || e == "avi" || e == "m4v" || e == "webm" || e == "prores") {
        return "VIDEO";
    }
    if (mt == "image" || mt == "foto" || e == "jpg" || e == "jpeg" || e == "png" || e == "tiff" || e == "tif" || e == "webp" || e == "raw" || e == "cr2" || e == "nef" || e == "dng") {
        return "IMAGE";
    }
    if (mt == "document" || mt == "documento" || e == "pdf" || e == "doc" || e == "docx" || e == "xls" || e == "xlsx" || e == "ppt" || e == "pptx") {
        return "DOCUMENT";
    }
    if (mt == "texto" || e == "txt" || e == "md" || e == "json" || e == "csv" || e == "xml" || e == "yaml" || e == "yml") {
        return "TEXT";
    }
    return "OTHER";
}

juce::Colour AnalyticsTreemapComponent::obterCorCategoria(const std::string& category) const {
    if (category == "AUDIO") return juce::Colour(0xff2a9d8f);    // Teal (GRID legend)
    if (category == "VIDEO") return juce::Colour(0xff9d4edd);    // Purple (GRID legend)
    if (category == "IMAGE") return juce::Colour(0xfff4a261);    // Amber/Orange (GRID legend)
    if (category == "DOCUMENT") return juce::Colour(0xff2b9348); // Green (GRID legend)
    if (category == "3D") return juce::Colour(0xffe76f51);       // Coral (GRID legend)
    if (category == "TEXT") return juce::Colour(0xffe76f51);     // Coral
    return juce::Colour(0xff6c757d);                             // Slate Grey
}

juce::String AnalyticsTreemapComponent::formatarTamanho(uint64_t bytes) const {
    if (bytes == 0) return "0 B";
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

void AnalyticsTreemapComponent::recarregarDoBanco(matriz::db::Database& db) {
    construirArvoreDoBanco(db);
    recalcularLayout();
    repaint();
}

void AnalyticsTreemapComponent::construirArvoreDoBanco(matriz::db::Database& db) {
    rootNode_ = std::make_unique<AnalyticsTreemapNode>();
    rootNode_->id = "ROOT";
    rootNode_->name = "ALL ASSETS";
    rootNode_->isDirectory = true;
    rootNode_->isLeaf = false;

    totalAssetsNoCatalogo_ = 0;
    totalTamanhoNoCatalogo_ = 0;

    std::map<std::string, std::map<std::string, std::vector<std::unique_ptr<AnalyticsTreemapNode>>>> catMap;

    try {
        auto stmt = db.prepare(
            "SELECT i.id, i.titulo, i.tipo_midia, "
            "COALESCE((SELECT COALESCE(a.caminho_absoluto_origem, a.caminho_relativo, '') "
            "          FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), ''), "
            "COALESCE((SELECT a.tamanho_bytes "
            "          FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), 0) "
            "FROM item i ORDER BY i.id");

        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string titulo = stmt.columnText(1);
            std::string tipoMidia = stmt.columnText(2);
            std::string pathStr = stmt.columnText(3);
            uint64_t bytes = static_cast<uint64_t>(stmt.columnInt(4));

            std::string ext = "";
            if (!pathStr.empty()) {
                int dotIdx = pathStr.rfind('.');
                if (dotIdx != std::string::npos && dotIdx < pathStr.length() - 1) {
                    ext = pathStr.substr(dotIdx + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
                }
            }
            if (ext.empty()) ext = "NO EXT";

            std::string category = inferirCategoria(tipoMidia, ext);

            auto leafNode = std::make_unique<AnalyticsTreemapNode>();
            leafNode->id = itemId;
            leafNode->assetId = itemId;
            leafNode->name = titulo.empty() ? pathStr : titulo;
            leafNode->path = pathStr;
            leafNode->directSize = bytes;
            leafNode->aggregateSize = bytes;
            leafNode->mediaType = category;
            leafNode->extension = ext;
            leafNode->isDirectory = false;
            leafNode->isLeaf = true;

            totalAssetsNoCatalogo_++;
            totalTamanhoNoCatalogo_ += bytes;

            catMap[category][ext].push_back(std::move(leafNode));
        }
    } catch (...) {}

    // Build hierarchy: Root -> Category -> Extension -> Asset
    for (auto& [catName, extGroup] : catMap) {
        auto catNode = std::make_unique<AnalyticsTreemapNode>();
        catNode->id = "CAT_" + catName;
        catNode->name = catName;
        catNode->mediaType = catName;
        catNode->isDirectory = true;
        catNode->isLeaf = false;
        catNode->parent = rootNode_.get();

        uint64_t catTotal = 0;

        for (auto& [extName, assetList] : extGroup) {
            auto extNode = std::make_unique<AnalyticsTreemapNode>();
            extNode->id = "EXT_" + catName + "_" + extName;
            extNode->name = extName;
            extNode->mediaType = catName;
            extNode->extension = extName;
            extNode->isDirectory = true;
            extNode->isLeaf = false;
            extNode->parent = catNode.get();

            uint64_t extTotal = 0;

            for (auto& assetNode : assetList) {
                extTotal += assetNode->aggregateSize;
                assetNode->parent = extNode.get();
                extNode->children.push_back(std::move(assetNode));
            }

            extNode->aggregateSize = extTotal;
            catTotal += extTotal;
            catNode->children.push_back(std::move(extNode));
        }

        catNode->aggregateSize = catTotal;
        rootNode_->children.push_back(std::move(catNode));
    }

    // Sort category children (extensions) by aggregateSize descending and assign tom-sobre-tom colors
    for (auto& catChild : rootNode_->children) {
        std::sort(catChild->children.begin(), catChild->children.end(),
                  [](const std::unique_ptr<AnalyticsTreemapNode>& a, const std::unique_ptr<AnalyticsTreemapNode>& b) {
                      return a->aggregateSize > b->aggregateSize;
                  });

        juce::Colour baseCatColor = obterCorCategoria(catChild->mediaType);
        size_t totalExts = catChild->children.size();

        auto calcularTomSobreTom = [](const juce::Colour& baseColor, size_t extIndex, size_t totalCount) -> juce::Colour {
            if (totalCount <= 1) return baseColor;

            static const float brightnessFactors[] = {
                1.00f,  // 0: Base category shade
                1.25f,  // 1: Brighter tint
                0.75f,  // 2: Deeper/Darker shade
                1.42f,  // 3: High bright tint
                0.60f,  // 4: Deep shade
                1.14f,  // 5: Soft bright
                0.86f,  // 6: Soft dark
                1.32f,  // 7: Very bright
                0.70f   // 8: Medium dark
            };

            float factor = brightnessFactors[extIndex % 9];
            float hueShift = (((static_cast<int>(extIndex) % 3) - 1)) * 0.012f;

            return baseColor.withRotatedHue(hueShift).withMultipliedBrightness(factor);
        };

        for (size_t i = 0; i < totalExts; ++i) {
            auto& extNode = catChild->children[i];
            juce::Colour extColor = calcularTomSobreTom(baseCatColor, i, totalExts);
            extNode->customColor = extColor;
            extNode->hasCustomColor = true;

            for (auto& leafNode : extNode->children) {
                leafNode->customColor = extColor;
                leafNode->hasCustomColor = true;
            }
        }
    }

    rootNode_->aggregateSize = totalTamanhoNoCatalogo_;
    noAtual_ = rootNode_.get();
    noHover_ = nullptr;
    noSelecionado_ = nullptr;
}

void AnalyticsTreemapComponent::recalcularLayout() {
    if (!noAtual_) return;

    auto bounds = getLocalBounds();
    bounds.removeFromTop(70); // Space for top KPI + Breadcrumbs bar

    if (bounds.isEmpty()) return;

    juce::Rectangle<float> rect = bounds.toFloat();
    noAtual_->bounds = rect;

    layoutNode(noAtual_, rect);
}

void AnalyticsTreemapComponent::layoutNode(AnalyticsTreemapNode* parentNode, const juce::Rectangle<float>& area) {
    if (!parentNode || parentNode->children.empty()) return;

    // Header padding for container nodes (except the current viewport root)
    juce::Rectangle<float> contentArea = area;
    if (parentNode != noAtual_ && parentNode->isDirectory) {
        float headerH = std::min(18.0f, area.getHeight() * 0.3f);
        if (headerH > 10.0f) {
            contentArea.removeFromTop(headerH);
        }
        contentArea = contentArea.reduced(1.5f);
    }

    if (contentArea.isEmpty() || contentArea.getWidth() <= 1.0f || contentArea.getHeight() <= 1.0f) {
        for (auto& child : parentNode->children) {
            child->bounds = juce::Rectangle<float>();
            layoutNode(child.get(), child->bounds);
        }
        return;
    }

    squarify(parentNode, contentArea);
}

void AnalyticsTreemapComponent::squarify(AnalyticsTreemapNode* parentNode, const juce::Rectangle<float>& area) {
    if (parentNode->children.empty()) return;

    std::vector<AnalyticsTreemapNode*> children;
    for (auto& c : parentNode->children) {
        children.push_back(c.get());
    }

    std::sort(children.begin(), children.end(), [](const AnalyticsTreemapNode* a, const AnalyticsTreemapNode* b) {
        return a->aggregateSize > b->aggregateSize;
    });

    uint64_t totalSize = parentNode->aggregateSize;
    if (totalSize == 0) {
        // Equal split if size is 0
        float count = static_cast<float>(children.size());
        float h = area.getHeight() / count;
        float currY = area.getY();
        for (auto* child : children) {
            child->bounds = juce::Rectangle<float>(area.getX(), currY, area.getWidth(), h);
            currY += h;
            layoutNode(child, child->bounds);
        }
        return;
    }

    float totalArea = area.getWidth() * area.getHeight();

    auto worstRatio = [&](const std::vector<AnalyticsTreemapNode*>& row, float sideLength) -> float {
        if (row.empty() || sideLength <= 0.0f) return 1e9f;
        uint64_t rowSum = 0;
        uint64_t maxVal = 0;
        uint64_t minVal = UINT64_MAX;

        for (auto* node : row) {
            rowSum += node->aggregateSize;
            if (node->aggregateSize > maxVal) maxVal = node->aggregateSize;
            if (node->aggregateSize < minVal) minVal = node->aggregateSize;
        }

        if (rowSum == 0) return 1.0f;

        float s2 = sideLength * sideLength;
        float areaSum = totalArea * (static_cast<float>(rowSum) / static_cast<float>(totalSize));
        float areaMax = totalArea * (static_cast<float>(maxVal) / static_cast<float>(totalSize));
        float areaMin = totalArea * (static_cast<float>(minVal) / static_cast<float>(totalSize));

        if (areaSum <= 0.0f || areaMin <= 0.0f) return 1e9f;

        return std::max((s2 * areaMax) / (areaSum * areaSum), (areaSum * areaSum) / (s2 * areaMin));
    };

    auto layoutRow = [&](const std::vector<AnalyticsTreemapNode*>& row, juce::Rectangle<float>& currContainer) {
        if (row.empty()) return;

        uint64_t rowSum = 0;
        for (auto* node : row) rowSum += node->aggregateSize;

        float rowFraction = (totalSize > 0) ? (static_cast<float>(rowSum) / static_cast<float>(totalSize)) : 0.0f;
        float rowArea = totalArea * rowFraction;

        bool horizontal = currContainer.getWidth() >= currContainer.getHeight();

        if (horizontal) {
            float rowWidth = (currContainer.getHeight() > 0.0f) ? (rowArea / currContainer.getHeight()) : 0.0f;
            rowWidth = std::min(rowWidth, currContainer.getWidth());

            float currY = currContainer.getY();
            for (auto* node : row) {
                float childHeight = (rowSum > 0) ? (currContainer.getHeight() * (static_cast<float>(node->aggregateSize) / static_cast<float>(rowSum))) : 0.0f;
                node->bounds = juce::Rectangle<float>(currContainer.getX(), currY, rowWidth, childHeight);
                currY += childHeight;
                layoutNode(node, node->bounds);
            }
            currContainer.removeFromLeft(rowWidth);
        } else {
            float rowHeight = (currContainer.getWidth() > 0.0f) ? (rowArea / currContainer.getWidth()) : 0.0f;
            rowHeight = std::min(rowHeight, currContainer.getHeight());

            float currX = currContainer.getX();
            for (auto* node : row) {
                float childWidth = (rowSum > 0) ? (currContainer.getWidth() * (static_cast<float>(node->aggregateSize) / static_cast<float>(rowSum))) : 0.0f;
                node->bounds = juce::Rectangle<float>(currX, currContainer.getY(), childWidth, rowHeight);
                currX += childWidth;
                layoutNode(node, node->bounds);
            }
            currContainer.removeFromTop(rowHeight);
        }
    };

    juce::Rectangle<float> remaining = area;
    std::vector<AnalyticsTreemapNode*> currentRow;

    for (size_t i = 0; i < children.size(); ++i) {
        auto* child = children[i];
        float side = std::min(remaining.getWidth(), remaining.getHeight());

        if (currentRow.empty()) {
            currentRow.push_back(child);
        } else {
            std::vector<AnalyticsTreemapNode*> testRow = currentRow;
            testRow.push_back(child);

            if (worstRatio(testRow, side) <= worstRatio(currentRow, side)) {
                currentRow.push_back(child);
            } else {
                layoutRow(currentRow, remaining);
                currentRow.clear();
                currentRow.push_back(child);
            }
        }
    }

    if (!currentRow.empty()) {
        layoutRow(currentRow, remaining);
    }
}

void AnalyticsTreemapComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    auto area = getLocalBounds();

    desenharTopBar(g, area);
    desenharBreadcrumbs(g, area);

    if (!noAtual_ || noAtual_->children.empty()) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText("No assets cataloged yet.", area, juce::Justification::centred);
        return;
    }

    desenharTreemap(g, area);
    desenharTooltip(g);
}

void AnalyticsTreemapComponent::desenharTopBar(juce::Graphics& g, juce::Rectangle<int>& area) {
    const auto& tk = tema();
    auto topArea = area.removeFromTop(36);

    g.setColour(tk.painel);
    g.fillRect(topArea);

    g.setColour(tk.borda);
    g.drawHorizontalLine(topArea.getBottom() - 1, 0.0f, static_cast<float>(topArea.getWidth()));

    auto inner = topArea.reduced(12, 6);

    // Navigation buttons
    btnRoot_.setBounds(inner.removeFromLeft(90));
    inner.removeFromLeft(6);
    btnUp_.setBounds(inner.removeFromLeft(60));
    inner.removeFromLeft(16);

    btnUp_.setEnabled(noAtual_ && noAtual_->parent != nullptr);

    // KPI indicators
    juce::String currentName = noAtual_ ? juce::String(noAtual_->name) : "ALL ASSETS";
    uint64_t currentSize = noAtual_ ? noAtual_->aggregateSize : totalTamanhoNoCatalogo_;

    juce::String statsText = juce::String("TOTAL CATALOG: ") + juce::String(totalAssetsNoCatalogo_) + " ASSETS (" + formatarTamanho(totalTamanhoNoCatalogo_) + ")"
                             + "  |  VIEWPORT: " + currentName + " (" + formatarTamanho(currentSize) + ")";

    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(statsText, inner, juce::Justification::right);
}

void AnalyticsTreemapComponent::desenharBreadcrumbs(juce::Graphics& g, juce::Rectangle<int>& area) {
    const auto& tk = tema();
    auto bcArea = area.removeFromTop(28);

    g.setColour(tk.fundo);
    g.fillRect(bcArea);

    g.setColour(tk.borda);
    g.drawHorizontalLine(bcArea.getBottom() - 1, 0.0f, static_cast<float>(bcArea.getWidth()));

    breadcrumbs_.clear();

    std::vector<AnalyticsTreemapNode*> pathStack;
    AnalyticsTreemapNode* curr = noAtual_;
    while (curr != nullptr) {
        pathStack.push_back(curr);
        curr = curr->parent;
    }
    std::reverse(pathStack.begin(), pathStack.end());

    int x = bcArea.getX() + 12;
    int y = bcArea.getY() + 4;
    int h = bcArea.getHeight() - 8;

    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));

    for (size_t i = 0; i < pathStack.size(); ++i) {
        auto* node = pathStack[i];
        juce::String label = juce::String(node->name);
        int textW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), label) + 12;

        BreadcrumbSegment seg;
        seg.label = label.toStdString();
        seg.node = node;
        seg.bounds = juce::Rectangle<int>(x, y, textW, h);
        breadcrumbs_.push_back(seg);

        bool isHover = (seg.bounds.contains(getMouseXYRelative()));
        bool isLast = (i == pathStack.size() - 1);

        g.setColour(isHover ? tk.acento : (isLast ? tk.textoPrimario : tk.textoSecundario));
        g.drawText(label, seg.bounds, juce::Justification::left);

        x += textW;

        if (!isLast) {
            g.setColour(tk.textoSecundario.withAlpha(0.5f));
            g.drawText("/", x, y, 12, h, juce::Justification::centred);
            x += 16;
        }
    }
}

void AnalyticsTreemapComponent::desenharTreemap(juce::Graphics& g, const juce::Rectangle<int>& area) {
    if (!noAtual_) return;

    juce::Rectangle<float> clipArea = area.toFloat();
    desenharNo(g, noAtual_, 0, clipArea);
}

void AnalyticsTreemapComponent::desenharNo(juce::Graphics& g, AnalyticsTreemapNode* node, int depth, const juce::Rectangle<float>& clipArea) {
    if (!node || node->bounds.isEmpty()) return;
    if (!clipArea.intersects(node->bounds)) return;

    const auto& tk = tema();

    if (node->isDirectory) {
        // Container background
        g.setColour(tk.painel.withAlpha(0.85f));
        g.fillRect(node->bounds);

        // Window border follows custom extension shade or category color scheme
        juce::Colour nodeColor = node->hasCustomColor ? node->customColor : obterCorCategoria(node->mediaType);
        g.setColour(nodeColor);
        g.drawRect(node->bounds, 2.0f);

        // Folder Title Header (SpaceMonger style)
        if (node != noAtual_ && node->bounds.getHeight() > 18.0f && node->bounds.getWidth() > 30.0f) {
            auto headerR = juce::Rectangle<float>(node->bounds.getX(), node->bounds.getY(), node->bounds.getWidth(), std::min(16.0f, node->bounds.getHeight()));
            g.setColour(nodeColor.darker(0.45f).withAlpha(0.92f));
            g.fillRect(headerR);

            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(juce::String(node->name) + " (" + formatarTamanho(node->aggregateSize) + ")", headerR.reduced(4, 0), juce::Justification::left, true);
        }

        // Draw children
        for (auto& child : node->children) {
            desenharNo(g, child.get(), depth + 1, clipArea);
        }
    } else {
        // Leaf Asset Rectangle using tom-sobre-tom shade
        juce::Colour baseColor = node->hasCustomColor ? node->customColor : obterCorCategoria(node->mediaType);
        if (node == noHover_) baseColor = baseColor.brighter(0.35f);

        g.setColour(baseColor);
        g.fillRect(node->bounds.reduced(0.5f));

        g.setColour(tk.fundo.withAlpha(0.4f));
        g.drawRect(node->bounds.reduced(0.5f), 1.0f);

        // Highlight border for hover or selection
        if (node == noHover_) {
            g.setColour(juce::Colours::yellow);
            g.drawRect(node->bounds, 2.0f);
        } else if (node == noSelecionado_) {
            g.setColour(juce::Colours::cyan);
            g.drawRect(node->bounds, 2.0f);
        }

        // Asset Name and Size label if box is large enough
        if (node->bounds.getWidth() > 36.0f && node->bounds.getHeight() > 18.0f) {
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));

            auto textR = node->bounds.reduced(3.0f);
            juce::String label = juce::String(node->name);
            if (node->bounds.getHeight() > 32.0f) {
                g.drawText(label, textR.removeFromTop(14.0f), juce::Justification::left, true);
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.setColour(juce::Colours::white.withAlpha(0.8f));
                g.drawText(formatarTamanho(node->aggregateSize), textR, juce::Justification::left, true);
            } else {
                g.drawText(label + " (" + formatarTamanho(node->aggregateSize) + ")", textR, juce::Justification::left, true);
            }
        }
    }
}

void AnalyticsTreemapComponent::desenharTooltip(juce::Graphics& g) {
    if (!noHover_) return;

    const auto& tk = tema();

    juce::String nameStr = juce::String(noHover_->name);
    juce::String sizeStr = formatarTamanho(noHover_->aggregateSize) + " (" + juce::String(noHover_->aggregateSize) + " bytes)";
    juce::String typeStr = noHover_->mediaType.empty() ? "N/A" : juce::String(noHover_->mediaType);
    juce::String extStr = noHover_->extension.empty() ? "N/A" : juce::String(noHover_->extension);
    juce::String pathStr = noHover_->path.empty() ? "N/A" : juce::String(noHover_->path);

    juce::StringArray lines;
    lines.add("Name: " + nameStr);
    lines.add("Size: " + sizeStr);
    lines.add("Media Type: " + typeStr);
    lines.add("Extension: " + extStr);
    lines.add("Path: " + pathStr);

    int maxW = 320;
    int lineH = 14;
    int boxH = lines.size() * lineH + 16;

    auto pos = getMouseXYRelative();
    int boxX = pos.x + 16;
    int boxY = pos.y + 16;

    if (boxX + maxW > getWidth() - 10) boxX = pos.x - maxW - 10;
    if (boxY + boxH > getHeight() - 10) boxY = pos.y - boxH - 10;
    if (boxX < 10) boxX = 10;
    if (boxY < 10) boxY = 10;

    juce::Rectangle<int> boxR(boxX, boxY, maxW, boxH);

    g.setColour(tk.painel.withAlpha(0.95f));
    g.fillRoundedRectangle(boxR.toFloat(), 4.0f);

    g.setColour(tk.acento);
    g.drawRoundedRectangle(boxR.toFloat(), 4.0f, 1.5f);

    auto inner = boxR.reduced(8, 8);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));

    for (int i = 0; i < lines.size(); ++i) {
        g.setColour(i == 0 ? tk.textoPrimario : tk.textoSecundario);
        g.drawText(lines[i], inner.removeFromTop(lineH), juce::Justification::left, true);
    }
}

AnalyticsTreemapNode* AnalyticsTreemapComponent::encontrarNoEm(AnalyticsTreemapNode* startNode, juce::Point<float> pt) {
    if (!startNode || !startNode->bounds.contains(pt)) return nullptr;

    // Search children deepest first
    for (auto& child : startNode->children) {
        if (child->bounds.contains(pt)) {
            AnalyticsTreemapNode* found = encontrarNoEm(child.get(), pt);
            if (found) return found;
        }
    }
    return startNode;
}

void AnalyticsTreemapComponent::mouseMove(const juce::MouseEvent& e) {
    if (!noAtual_) return;

    auto pt = e.position;
    AnalyticsTreemapNode* prevHover = noHover_;
    noHover_ = encontrarNoEm(noAtual_, pt);

    if (noHover_ != prevHover) {
        repaint();
    }
}

void AnalyticsTreemapComponent::mouseExit(const juce::MouseEvent&) {
    if (noHover_ != nullptr) {
        noHover_ = nullptr;
        repaint();
    }
}

void AnalyticsTreemapComponent::mouseDown(const juce::MouseEvent& e) {
    // Check breadcrumb clicks first
    auto ptInt = e.getPosition();
    for (const auto& seg : breadcrumbs_) {
        if (seg.bounds.contains(ptInt) && seg.node) {
            noAtual_ = seg.node;
            recalcularLayout();
            repaint();
            return;
        }
    }

    if (!noAtual_) return;

    AnalyticsTreemapNode* clicked = encontrarNoEm(noAtual_, e.position);
    if (!clicked) return;

    if (clicked->isDirectory) {
        // Container node clicked: Semantic Zoom into container!
        noAtual_ = clicked;
        recalcularLayout();
        repaint();
    } else {
        // Leaf asset clicked
        noSelecionado_ = clicked;
        repaint();

        if (aoSelecionarItem && !clicked->assetId.empty()) {
            aoSelecionarItem(clicked->assetId);
        }

        if (e.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            menu.addItem(1, "SHOW SOURCE");

            juce::String targetPath = juce::String(clicked->path);
            juce::String assetIdStr = juce::String(clicked->assetId);
            auto abrirGridCb = aoAbrirNoGrid;

            menu.showMenuAsync(juce::PopupMenu::Options(), [targetPath, assetIdStr, abrirGridCb](int resultado) {
                if (resultado == 1) {
                    if (!targetPath.isEmpty()) {
                        juce::File arq(targetPath);
                        if (arq.existsAsFile() || arq.isDirectory()) {
                            arq.revealToUser();
                        } else {
                            juce::AlertWindow::showAsync(
                                juce::MessageBoxOptions()
                                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                                    .withTitle("Origem Ausente")
                                    .withMessage("O arquivo de origem não foi encontrado no caminho:\n" + targetPath)
                                    .withButton("OK"),
                                nullptr);
                        }
                    }
                }
            });
        }
    }
}

void AnalyticsTreemapComponent::resized() {
    btnUp_.setBounds(0, 0, 0, 0); // Re-positioned in desenharTopBar
    recalcularLayout();
}

} // namespace matriz::ui
