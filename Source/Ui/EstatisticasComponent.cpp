#include "EstatisticasComponent.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Vault/Resolucao.h"
#include "Tokens.h"

namespace matriz::ui {

EstatisticasComponent::EstatisticasComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    preservationSection_ = std::make_unique<PreservationWorkspaceComponent>(projeto_);
    addAndMakeVisible(*preservationSection_);
    recarregar();
}

void EstatisticasComponent::recarregar() {
    if (preservationSection_) preservationSection_->recarregar();
    cards_.clear();
    categorias_.clear();
    extensoes_.clear();
    decadas_.clear();
    tags_.clear();

    auto itens = projeto_.listarItens();
    totalItens_ = static_cast<int>(itens.size());
    totalBytes_ = 0;

    std::map<std::string, CategoriaStat> catMap;
    catMap["Audio"] = {"Audio", 0, 0, juce::Colour(0xff2a9d8f)};
    catMap["Video"] = {"Video", 0, 0, juce::Colour(0xff9d4edd)};
    catMap["Image"] = {"Photo / Image", 0, 0, juce::Colour(0xfff4a261)};
    catMap["Document"] = {"Document", 0, 0, juce::Colour(0xff2b9348)};
    catMap["3D"] = {"3D & Other", 0, 0, juce::Colour(0xffe76f51)};

    std::map<std::string, std::pair<int, juce::int64>> extMap;
    std::map<int, int> decadaMap;
    std::map<std::string, int> tagMap;

    for (const auto& item : itens) {
        juce::String extStr = juce::String(item.extensaoArquivo).toLowerCase();
        auto cat = matriz::ingest::categoriaPorExtensao(extStr);

        // Estimate size if available or query vault
        juce::int64 sz = item.tamanhoBytes;
        if (sz <= 0) {
            auto res = matriz::vault::resolverArquivo(projeto_.projeto().registro(), item.id, projeto_.projeto().pasta());
            if (res && res->existsAsFile()) sz = res->getSize();
        }
        totalBytes_ += sz;

        if (cat == matriz::ingest::CategoriaMidia::Audio) {
            catMap["Audio"].quantidade++;
            catMap["Audio"].tamanhoBytes += sz;
        } else if (cat == matriz::ingest::CategoriaMidia::Video) {
            catMap["Video"].quantidade++;
            catMap["Video"].tamanhoBytes += sz;
        } else if (cat == matriz::ingest::CategoriaMidia::Imagem) {
            catMap["Image"].quantidade++;
            catMap["Image"].tamanhoBytes += sz;
        } else if (cat == matriz::ingest::CategoriaMidia::Documento || cat == matriz::ingest::CategoriaMidia::Texto) {
            catMap["Document"].quantidade++;
            catMap["Document"].tamanhoBytes += sz;
        } else {
            catMap["3D"].quantidade++;
            catMap["3D"].tamanhoBytes += sz;
        }

        if (extStr.isNotEmpty()) {
            extMap[extStr.toStdString()].first++;
            extMap[extStr.toStdString()].second += sz;
        }

        if (item.ano && *item.ano > 1800 && *item.ano < 2100) {
            int dec = (*item.ano / 10) * 10;
            decadaMap[dec]++;
        }
    }

    // Backup health query
    auto colecoes = projeto_.listarColecoesEmbutidas();
    for (const auto& c : colecoes) {
        if (c.chave == "vulneraveis") vulneraveis_ = c.contagem;
        else if (c.chave == "single_copy") singleCopy_ = c.contagem;
        else if (c.chave == "ausentes") ausentes_ = c.contagem;
    }

    // Build cards
    cards_.push_back({"TOTAL ASSETS", juce::String(totalItens_), "Cataloged files", juce::Colour(0xff3b82f6)});
    cards_.push_back({"STORAGE SIZE", juce::File::descriptionOfSizeInBytes(totalBytes_), "Occupied volume", juce::Colour(0xff10b981)});

    juce::String catDominante = "None";
    int maxCatCount = 0;
    for (const auto& [k, c] : catMap) {
        if (c.quantidade > maxCatCount) {
            maxCatCount = c.quantidade;
            catDominante = c.nome;
        }
    }
    cards_.push_back({"PRIMARY FORMAT", catDominante, juce::String(maxCatCount) + " assets", juce::Colour(0xfff59e0b)});

    int protegidos = juce::jmax(0, totalItens_ - vulneraveis_);
    int pctProtegido = totalItens_ > 0 ? (protegidos * 100 / totalItens_) : 100;
    cards_.push_back({"BACKUP HEALTH", juce::String(pctProtegido) + "%", juce::String(vulneraveis_) + " vulnerable",
                      vulneraveis_ > 0 ? juce::Colour(0xffef4444) : juce::Colour(0xff10b981)});

    for (const auto& [k, c] : catMap) {
        if (c.quantidade > 0) categorias_.push_back(c);
    }

    for (const auto& [ext, data] : extMap) {
        extensoes_.push_back({"." + juce::String(ext).toUpperCase(), data.first, data.second});
    }
    std::sort(extensoes_.begin(), extensoes_.end(), [](const ExtensaoStat& a, const ExtensaoStat& b) {
        return a.quantidade > b.quantidade;
    });
    if (extensoes_.size() > 8) extensoes_.resize(8);

    for (const auto& [dec, count] : decadaMap) {
        decadas_.push_back({juce::String(dec) + "s", count});
    }

    repaint();
}

void EstatisticasComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);

    auto area = getLocalBounds().reduced(20, 16);
    if (preservationSection_) area.removeFromTop(408);

    // Header
    g.setColour(tema().textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    g.drawText("Collection Analytics & Statistics", area.removeFromTop(24), juce::Justification::centredLeft);
    area.removeFromTop(8);

    // Summary Cards (Row of 4)
    auto rowCards = area.removeFromTop(84);
    int cardW = (rowCards.getWidth() - 36) / 4;
    for (size_t i = 0; i < cards_.size(); ++i) {
        auto cardArea = rowCards.removeFromLeft(cardW);
        rowCards.removeFromLeft(12);

        g.setColour(tema().painel);
        g.fillRoundedRectangle(cardArea.toFloat(), tema().raioMedio);
        g.setColour(cards_[i].cor);
        g.fillRoundedRectangle(cardArea.getX(), cardArea.getY(), 4.0f, cardArea.getHeight(), 2.0f);

        auto inner = cardArea.reduced(14, 10);
        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(cards_[i].titulo, inner.removeFromTop(14), juce::Justification::centredLeft);

        g.setColour(tema().textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
        g.drawText(cards_[i].valor, inner.removeFromTop(26), juce::Justification::centredLeft);

        g.setColour(tema().textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(cards_[i].detalhe, inner, juce::Justification::centredLeft);
    }
    area.removeFromTop(24);

    // Two Column Layout
    int colW = (area.getWidth() - 20) / 2;
    auto leftCol = area.removeFromLeft(colW);
    auto rightCol = area;
    rightCol.removeFromLeft(20);

    // Left Column: Format Breakdown & Extension Distribution
    {
        g.setColour(tema().textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText("Format Category Distribution", leftCol.removeFromTop(20), juce::Justification::centredLeft);
        leftCol.removeFromTop(8);

        for (const auto& cat : categorias_) {
            auto itemBox = leftCol.removeFromTop(36);
            g.setColour(tema().textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));

            juce::String label = cat.nome + "  (" + juce::String(cat.quantidade) + " assets - " +
                                 juce::File::descriptionOfSizeInBytes(cat.tamanhoBytes) + ")";
            g.drawText(label, itemBox.removeFromTop(16), juce::Justification::centredLeft);

            float pct = totalItens_ > 0 ? (static_cast<float>(cat.quantidade) / totalItens_) : 0.0f;
            g.setColour(tema().painelAlt);
            g.fillRoundedRectangle(itemBox.toFloat(), 4.0f);
            g.setColour(cat.cor);
            g.fillRoundedRectangle(itemBox.withWidth(juce::jmax(8, static_cast<int>(itemBox.getWidth() * pct))).toFloat(), 4.0f);
        }

        leftCol.removeFromTop(20);
        g.setColour(tema().textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText("Top File Extensions", leftCol.removeFromTop(20), juce::Justification::centredLeft);
        leftCol.removeFromTop(8);

        for (const auto& ext : extensoes_) {
            auto row = leftCol.removeFromTop(22);
            g.setColour(tema().acento);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(ext.extensao, row.removeFromLeft(60), juce::Justification::centredLeft);

            g.setColour(tema().textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(juce::String(ext.quantidade) + " files  |  " + juce::File::descriptionOfSizeInBytes(ext.tamanhoBytes),
                       row, juce::Justification::centredLeft);
        }
    }

    // Right Column: Timeline / Decade Distribution & Health
    {
        g.setColour(tema().textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText("Timeline & Year Distribution", rightCol.removeFromTop(20), juce::Justification::centredLeft);
        rightCol.removeFromTop(8);

        if (decadas_.empty()) {
            g.setColour(tema().textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText("No release year metadata assigned to assets yet.", rightCol.removeFromTop(24), juce::Justification::centredLeft);
        } else {
            int maxDec = 1;
            for (const auto& d : decadas_) maxDec = juce::jmax(maxDec, d.quantidade);

            for (const auto& d : decadas_) {
                auto row = rightCol.removeFromTop(28);
                g.setColour(tema().textoSecundario);
                g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
                g.drawText(d.decada, row.removeFromLeft(60), juce::Justification::centredLeft);

                float ratio = static_cast<float>(d.quantidade) / maxDec;
                g.setColour(tema().painelAlt);
                g.fillRoundedRectangle(row.removeFromTop(18).toFloat(), 3.0f);
                g.setColour(tema().acento);
                g.fillRoundedRectangle(row.withWidth(juce::jmax(6, static_cast<int>(row.getWidth() * ratio))).toFloat(), 3.0f);
            }
        }

        rightCol.removeFromTop(24);
        g.setColour(tema().textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText("Storage & Protection Status", rightCol.removeFromTop(20), juce::Justification::centredLeft);
        rightCol.removeFromTop(8);

        auto drawStatusRow = [&](const juce::String& label, int count, juce::Colour cor) {
            auto r = rightCol.removeFromTop(24);
            g.setColour(cor);
            g.fillEllipse(r.getX() + 2, r.getY() + 6, 8, 8);
            g.setColour(tema().textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(label + ":  " + juce::String(count) + " assets", r.withTrimmedLeft(18), juce::Justification::centredLeft);
        };

        drawStatusRow("Backed up in multiple vaults", juce::jmax(0, totalItens_ - vulneraveis_), juce::Colour(0xff10b981));
        drawStatusRow("Single copy (Vulnerable)", singleCopy_, juce::Colour(0xfff59e0b));
        drawStatusRow("Offline or unverified", ausentes_, juce::Colour(0xffef4444));
    }
}

void EstatisticasComponent::resized() {
    auto area = getLocalBounds().reduced(20, 16);
    if (preservationSection_) {
        preservationSection_->setBounds(area.removeFromTop(400));
    }
}

} // namespace matriz::ui
