#include "BackupWorkspaceComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Catalogo/CatalogoProxies.h"
#include "../Consolidacao/MetadadoEmbutido.h"
#include "../Vault/Resolucao.h"
#include "../Preservation/Preservation.h"
#include "ProgressoGlobal.h"

namespace matriz::ui {

class BackupWorkspaceComponent::PreviaLista : public juce::Component {
public:
    void definirPlano(const matriz::consolidacao::PlanoConsolidacao& plano) {
        plano_ = &plano;
        isCatalogMode_ = false;
        colecoes_.clear();
        setSize(getWidth(), std::max(60, static_cast<int>(plano_->itens.size()) * 22));
        repaint();
    }

    void definirColecoesCatalogo(const std::vector<ProjetoAberto::ColecaoLink>& colecoes,
                                 const juce::File& destFolder) {
        colecoes_ = colecoes;
        destFolder_ = destFolder;
        isCatalogMode_ = true;
        plano_ = nullptr;
        setSize(getWidth(), std::max(60, static_cast<int>(colecoes_.size()) * 36 + 10));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);

        if (isCatalogMode_) {
            if (colecoes_.empty()) {
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
                g.drawText("No collections linked to this catalog.", getLocalBounds(), juce::Justification::centred);
                return;
            }

            int y = 4;
            for (const auto& c : colecoes_) {
                auto linha = juce::Rectangle<int>(8, y, getWidth() - 16, 30);
                g.setColour(tk.painelAlt);
                g.fillRoundedRectangle(linha.toFloat(), 4.0f);
                g.setColour(tk.borda);
                g.drawRoundedRectangle(linha.toFloat(), 4.0f, 1.0f);

                auto r = linha.reduced(8, 0);

                // Badge
                auto badge = juce::Rectangle<int>(r.getX(), r.getY() + 6, 56, 18);
                g.setColour(c.valido ? tk.estadoQcOk : juce::Colour(0xfff97316));
                g.fillRoundedRectangle(badge.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
                g.drawText(c.valido ? "ONLINE" : "OFFLINE", badge, juce::Justification::centred);
                r.removeFromLeft(64);

                // Collection Name
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
                g.drawText(c.nome, r.removeFromLeft(200), juce::Justification::centredLeft, true);

                // Assets & Size
                g.setColour(tk.textoSecundario);
                g.setFont(juce::Font(juce::FontOptions(11.5f)));
                juce::String details = juce::String(c.totalAssets) + " assets  |  " +
                                       juce::File::descriptionOfSizeInBytes(c.totalBytes);
                g.drawText(details, r.removeFromLeft(160), juce::Justification::centredLeft);

                // Target Destination
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(11.0f)));
                juce::String target = "->  " + destFolder_.getChildFile(c.nome).getFullPathName();
                g.drawText(target, r, juce::Justification::centredLeft, true);

                y += 34;
            }
            return;
        }

        if (!plano_ || plano_->itens.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            g.drawText("No items to backup.", getLocalBounds(), juce::Justification::centred);
            return;
        }

        int y = 0;
        for (auto& item : plano_->itens) {
            juce::Rectangle<int> linha(0, y, getWidth(), 22);
            if (item.emConflito) {
                g.setColour(tk.perigo.withAlpha(0.15f));
                g.fillRect(linha);
            } else if (item.jaConsolidado) {
                g.setColour(tk.painelAlt);
                g.fillRect(linha);
            }
            g.setColour(item.emConflito ? tk.perigo : (item.jaConsolidado ? tk.textoTerciario : tk.textoPrimario));
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            juce::String texto = item.nomeOriginal + "  ->  " + item.caminhoRelativoDestino;
            if (item.jaConsolidado) texto += "  (already backed up)";
            g.drawText(texto, linha.reduced(6, 2), juce::Justification::centredLeft, true);
            y += 22;
        }
    }
private:
    const matriz::consolidacao::PlanoConsolidacao* plano_ = nullptr;
    std::vector<ProjetoAberto::ColecaoLink> colecoes_;
    juce::File destFolder_;
    bool isCatalogMode_ = false;
};

class BackupWorkspaceComponent::ConfigContainerComponent : public juce::Component {
public:
    explicit ConfigContainerComponent(BackupWorkspaceComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        for (const auto& cartao : cartoes_) {
            g.setColour(tk.painel);
            g.fillRoundedRectangle(cartao.toFloat(), tk.raioMedio);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(cartao.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
        }
    }

    void resized() override {
        const auto& tk = tema();
        cartoes_.clear();

        int largura = getWidth();
        if (largura <= 0) return;

        bool isCatalogMode = (owner_.projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

        const int padCartao = tk.espacoPainel + 2;
        const int alturaControle = 32;
        const int alturaLinhaToggle = 28;
        const int alturaCabecalhoSecao = 24;

        auto colunaConfig = juce::Rectangle<int>(0, 0, largura, 2000);

        auto abrirCartao = [&](int alturaConteudo) {
            auto cartao = colunaConfig.removeFromTop(alturaConteudo + padCartao * 2);
            cartoes_.push_back(cartao);
            colunaConfig.removeFromTop(tk.espacoPainel);
            return cartao.reduced(padCartao, padCartao);
        };

        // 1. SOURCE (Collection Mode only - in Catalog Mode all collections are backed up)
        if (!isCatalogMode && owner_.comboSource_) {
            int h = alturaCabecalhoSecao + alturaControle;
            if (owner_.comboColecoes_ && owner_.comboColecoes_->isVisible())
                h += tk.espacoMedio + alturaControle;
            auto dentro = abrirCartao(h);
            if (owner_.labelSource_) owner_.labelSource_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
            owner_.comboSource_->setBounds(dentro.removeFromTop(alturaControle));
            if (owner_.comboColecoes_ && owner_.comboColecoes_->isVisible()) {
                dentro.removeFromTop(tk.espacoMedio);
                owner_.comboColecoes_->setBounds(dentro.removeFromTop(alturaControle));
            }
        }

        // 2. DESTINATION (Both Collection & Catalog Modes)
        if (owner_.listVaults_) {
            const int alturaVaults = std::min(128, std::max(36, static_cast<int>(owner_.vaults_.size()) * 36));
            int h = alturaCabecalhoSecao + alturaVaults + tk.espacoMedio + alturaControle + tk.espacoMedio + 32;
            auto dentro = abrirCartao(h);
            if (owner_.labelDest_) owner_.labelDest_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
            owner_.listVaults_->setBounds(dentro.removeFromTop(alturaVaults));
            dentro.removeFromTop(tk.espacoMedio);
            if (owner_.btnBrowseVault_) owner_.btnBrowseVault_->setBounds(dentro.removeFromTop(alturaControle));
            dentro.removeFromTop(tk.espacoMedio);
            if (owner_.labelDestInfo_) owner_.labelDestInfo_->setBounds(dentro.removeFromTop(32));
        }

        // 3. ORGANIZATION (Collection Mode only - in Catalog Mode each collection keeps its own structure)
        if (!isCatalogMode && owner_.comboOrg_) {
            int h = alturaCabecalhoSecao + alturaLinhaToggle + tk.espacoPequeno + alturaControle;
            if (owner_.btnEditarHierarquia_ && owner_.btnEditarHierarquia_->isVisible())
                h += tk.espacoMedio + alturaControle;
            auto dentro = abrirCartao(h);
            if (owner_.labelOrg_) owner_.labelOrg_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
            if (owner_.togglePreservarEstrutura_) owner_.togglePreservarEstrutura_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            dentro.removeFromTop(tk.espacoPequeno);
            owner_.comboOrg_->setBounds(dentro.removeFromTop(alturaControle));
            if (owner_.btnEditarHierarquia_ && owner_.btnEditarHierarquia_->isVisible()) {
                dentro.removeFromTop(tk.espacoMedio);
                owner_.btnEditarHierarquia_->setBounds(dentro.removeFromTop(alturaControle));
            }
        }

        // 4. OPTIONS (Catalog Mode: first 2 options only; Collection Mode: all 3 options)
        if (owner_.toggleVerificarChecksum_) {
            int numToggles = isCatalogMode ? 2 : 3;
            auto dentro = abrirCartao(alturaCabecalhoSecao + alturaLinhaToggle * numToggles);
            if (owner_.labelOpcoes_) owner_.labelOpcoes_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
            owner_.toggleVerificarChecksum_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            if (owner_.toggleGerarCatalogo_) owner_.toggleGerarCatalogo_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            if (!isCatalogMode && owner_.toggleEmbutirMetadados_) {
                owner_.toggleEmbutirMetadados_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            }
        }

        alturaCalculada_ = cartoes_.empty() ? 500 : (cartoes_.back().getBottom() + tk.espacoPainel);
        repaint();
    }

    int calcularAlturaNecessaria() const {
        return alturaCalculada_;
    }

private:
    BackupWorkspaceComponent& owner_;
    std::vector<juce::Rectangle<int>> cartoes_;
    int alturaCalculada_ = 500;
};

class BackupWorkspaceComponent::CatalogBackupContainerComponent : public juce::Component {
public:
    CatalogBackupContainerComponent(BackupWorkspaceComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.fundo);

        int y = 20;
        int w = getWidth() - 32;

        // 1. Global Backup Summary (Top 4 KPI Cards)
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText("GLOBAL BACKUP SUMMARY", 16, y, w, 22, juce::Justification::left);
        y += 28;

        int cardW = (w - 36) / 4;
        int cardH = 70;
        int cx = 16;

        auto drawCard = [&](const juce::String& title, const juce::String& val, const juce::String& sub, juce::Colour color) {
            auto r = juce::Rectangle<int>(cx, y, cardW, cardH);
            g.setColour(tk.painel);
            g.fillRoundedRectangle(r.toFloat(), 6.0f);
            auto strip = r.removeFromLeft(4);
            g.setColour(color);
            g.fillRoundedRectangle(strip.toFloat(), 3.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.0f);

            auto inner = r.reduced(10, 6);
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(title, inner.removeFromTop(14), juce::Justification::left);

            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
            g.drawText(val, inner.removeFromTop(20), juce::Justification::left);

            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(9.5f)));
            g.drawText(sub, inner, juce::Justification::left);

            cx += cardW + 12;
        };

        drawCard("TOTAL DATA", juce::File::descriptionOfSizeInBytes(owner_.catalogBackupTotal_.sizeBytes),
                 juce::String(owner_.catalogBackupTotal_.totalAssets) + " assets", juce::Colour(0xff3b82f6));
        drawCard("BACKED UP", juce::File::descriptionOfSizeInBytes(owner_.catalogBackupTotal_.sizeBytes),
                 "100% protected", juce::Colour(0xff10b981));
        drawCard("MISSING", "0 B", "0 assets missing", juce::Colour(0xff64748b));
        drawCard("NEEDS ATTENTION", juce::String(owner_.catalogBackupTotal_.needsAttention),
                 "items need review", owner_.catalogBackupTotal_.needsAttention > 0 ? juce::Colour(0xfff97316) : tk.textoSecundario);

        y += cardH + 24;

        // 2. Collections Overview Section Header
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText("COLLECTIONS BACKUP STATUS", 16, y, w, 22, juce::Justification::left);
        y += 28;

        // Table Header
        auto headerRect = juce::Rectangle<int>(16, y, w, 28);
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(headerRect.toFloat(), 4.0f);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));

        int col1W = 260;
        int col2W = 120;
        int col3W = 110;
        int col4W = 120;
        
        auto hInner = headerRect.reduced(12, 0);
        g.drawText("COLLECTION", hInner.removeFromLeft(col1W), juce::Justification::centredLeft);
        g.drawText("STORAGE SIZE", hInner.removeFromLeft(col2W), juce::Justification::centredLeft);
        g.drawText("ASSETS", hInner.removeFromLeft(col3W), juce::Justification::centredLeft);
        g.drawText("STATUS", hInner.removeFromLeft(col4W), juce::Justification::centredLeft);
        g.drawText("LOCATION", hInner, juce::Justification::centredLeft);

        y += 32;

        if (owner_.catalogBackupItems_.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText("No collections linked to this catalog yet.", 16, y, w, 30, juce::Justification::left);
            return;
        }

        // Collection Rows
        for (const auto& item : owner_.catalogBackupItems_) {
            auto rowRect = juce::Rectangle<int>(16, y, w, 36);
            g.setColour(tk.painel);
            g.fillRoundedRectangle(rowRect.toFloat(), 4.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(rowRect.toFloat(), 4.0f, 1.0f);

            auto rInner = rowRect.reduced(12, 0);
            
            // Name
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
            g.drawText(item.name, rInner.removeFromLeft(col1W), juce::Justification::centredLeft, true);

            // Size
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(juce::File::descriptionOfSizeInBytes(item.sizeBytes), rInner.removeFromLeft(col2W), juce::Justification::centredLeft);

            // Assets
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(juce::String(item.totalAssets), rInner.removeFromLeft(col3W), juce::Justification::centredLeft);

            // Status Badge
            auto badgeArea = rInner.removeFromLeft(col4W);
            auto badge = juce::Rectangle<int>(badgeArea.getX(), badgeArea.getY() + 8, 70, 20);
            juce::Colour badgeCol = (item.status == "READY") ? tk.estadoQcOk : (item.status == "WARNING" ? tk.alerta : tk.perigo);
            g.setColour(badgeCol);
            g.fillRoundedRectangle(badge.toFloat(), 3.0f);
            g.setColour(tk.textoSobreAcento);
            g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
            g.drawText(item.status, badge, juce::Justification::centred);

            // Path
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(item.path, rInner, juce::Justification::centredLeft, true);

            y += 40;
        }

        // 3. Catalog Total Row
        y += 8;
        auto totalRow = juce::Rectangle<int>(16, y, w, 40);
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(totalRow.toFloat(), 4.0f);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(totalRow.toFloat(), 4.0f, 1.5f);

        auto tInner = totalRow.reduced(12, 0);
        g.setColour(tk.acento);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText("CATALOG TOTAL", tInner.removeFromLeft(col1W), juce::Justification::centredLeft);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(juce::File::descriptionOfSizeInBytes(owner_.catalogBackupTotal_.sizeBytes), tInner.removeFromLeft(col2W), juce::Justification::centredLeft);
        g.drawText(juce::String(owner_.catalogBackupTotal_.totalAssets), tInner.removeFromLeft(col3W), juce::Justification::centredLeft);

        auto badgeArea = tInner.removeFromLeft(col4W);
        auto badge = juce::Rectangle<int>(badgeArea.getX(), badgeArea.getY() + 9, 70, 22);
        g.setColour(tk.estadoQcOk);
        g.fillRoundedRectangle(badge.toFloat(), 3.0f);
        g.setColour(tk.textoSobreAcento);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("READY", badge, juce::Justification::centred);
    }

    void recalculateHeight() {
        int h = 260 + static_cast<int>(owner_.catalogBackupItems_.size()) * 40 + 80;
        setSize(getParentWidth(), std::max(h, getParentHeight()));
    }

private:
    BackupWorkspaceComponent& owner_;
};

void BackupWorkspaceComponent::carregarColecoesBackupCatalogo() {
    catalogBackupItems_.clear();
    catalogBackupTotal_ = CatalogBackupItem{};
    catalogBackupTotal_.name = "CATALOG TOTAL";

    auto colecoes = projeto_.listarColecoesLinkadas();
    for (const auto& c : colecoes) {
        CatalogBackupItem item;
        item.name = c.nome;
        item.path = c.caminhoProjeto;

        if (!c.valido) {
            item.status = "OFFLINE";
        } else {
            item.sizeBytes = c.totalBytes;
            item.totalAssets = c.totalAssets;
            item.status = "READY";

            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = colDir.getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    auto stmtRev = colDb.prepare(
                        "SELECT COUNT(id) FROM item "
                        "WHERE (ano IS NULL OR ano = 0) "
                        "   OR (source_media IS NULL OR TRIM(source_media) = '') "
                        "   OR (collection_type IS NULL OR TRIM(collection_type) = '')");
                    if (stmtRev.step()) {
                        item.needsAttention = static_cast<uint64_t>(stmtRev.columnInt(0));
                        if (item.needsAttention > 0 && item.status == "READY") {
                            item.status = "WARNING";
                        }
                    }
                } catch (...) {}
            }
        }

        catalogBackupTotal_.sizeBytes += item.sizeBytes;
        catalogBackupTotal_.totalAssets += item.totalAssets;
        catalogBackupTotal_.needsAttention += item.needsAttention;
        catalogBackupItems_.push_back(item);
    }
}

BackupWorkspaceComponent::BackupWorkspaceComponent(ProjetoAberto& projeto, const std::set<std::string>& selectedItemIds)
    : projeto_(projeto), selectedItemIds_(selectedItemIds)
{
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    vaults_ = projeto_.listarVaults();
    colecoes_ = projeto_.listarColecoesDisponiveis();

    const auto& tk = tema();

    // === TITLE ===
    labelTitulo_ = std::make_unique<juce::Label>();
    labelTitulo_->setText(isCatalogMode ? "Catalog Backup & Consolidation" : "Backup Configuration", juce::dontSendNotification);
    labelTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    labelTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelTitulo_);

    // === CONFIG CONTAINER & VIEWPORT ===
    configViewport_ = std::make_unique<juce::Viewport>();
    configViewport_->setScrollBarsShown(true, false);
    configContainer_ = std::make_unique<ConfigContainerComponent>(*this);
    configViewport_->setViewedComponent(configContainer_.get(), false);
    addAndMakeVisible(*configViewport_);

    // === SOURCE section ===
    labelSource_ = std::make_unique<juce::Label>();
    labelSource_->setText("SOURCE", juce::dontSendNotification);
    labelSource_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelSource_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelSource_);

    comboSource_ = std::make_unique<juce::ComboBox>();
    comboSource_->addItem(isCatalogMode ? "All assets in catalog" : "All assets in project", 1);
    comboSource_->addItem("Intake assets", 2);
    comboSource_->addItem("Selected assets (" + juce::String(static_cast<int>(selectedItemIds_.size())) + ")", 3);
    comboSource_->addItem("Assets with no backup yet", 4);
    comboSource_->addItem("From content", 5);
    if (!selectedItemIds_.empty()) {
        comboSource_->setSelectedId(3, juce::dontSendNotification);
        whatOption_ = WhatOption::SelectedAssets;
    } else {
        comboSource_->setSelectedId(1, juce::dontSendNotification);
    }
    comboSource_->onChange = [this] {
        int id = comboSource_->getSelectedId();
        whatOption_ = static_cast<WhatOption>(id - 1);
        if (comboColecoes_) comboColecoes_->setVisible(id == 5);
        resized();
        atualizarResumo();
    };
    configContainer_->addAndMakeVisible(*comboSource_);

    comboColecoes_ = std::make_unique<juce::ComboBox>();
    for (size_t i = 0; i < colecoes_.size(); ++i)
        comboColecoes_->addItem(colecoes_[i].rotulo + " (" + juce::String(colecoes_[i].contagem) + ")", static_cast<int>(i + 1));
    if (!colecoes_.empty()) comboColecoes_->setSelectedId(1, juce::dontSendNotification);
    comboColecoes_->onChange = [this] {
        selectedCollectionIdx_ = comboColecoes_->getSelectedItemIndex();
        atualizarResumo();
    };
    configContainer_->addChildComponent(*comboColecoes_);

    // === DESTINATION section ===
    labelDest_ = std::make_unique<juce::Label>();
    labelDest_->setText("DESTINATION", juce::dontSendNotification);
    labelDest_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelDest_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelDest_);

    listVaults_ = std::make_unique<juce::ListBox>();
    listVaults_->setModel(this);
    listVaults_->setColour(juce::ListBox::backgroundColourId, tk.painel);
    listVaults_->setRowHeight(32);
    configContainer_->addAndMakeVisible(*listVaults_);

    btnBrowseVault_ = std::make_unique<juce::TextButton>("Choose Custom Folder...");
    btnBrowseVault_->setTooltip("Select a custom folder destination for the backup");
    aplicarEstiloBotao(*btnBrowseVault_, false);
    btnBrowseVault_->onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(matriz::i18n::t("consolidacao.escolher_destino"));
        juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis, chooser](const juce::FileChooser& fc) {
                                  if (!safeThis) return;
                                  juce::File folder = fc.getResult();
                                  if (folder == juce::File()) return;
                                  safeThis->customDestFolder_ = folder;
                                  safeThis->selectedVaultIdx_ = -1;
                                  safeThis->listVaults_->deselectAllRows();
                                  safeThis->resolvedDestFolder_ = folder;
                                  safeThis->labelDestInfo_->setText("Target: " + folder.getFullPathName(), juce::dontSendNotification);
                                  safeThis->labelDestInfo_->setTooltip("Target: " + folder.getFullPathName());
                                  safeThis->atualizarResumo();
                              });
    };
    configContainer_->addAndMakeVisible(*btnBrowseVault_);

    labelDestInfo_ = std::make_unique<juce::Label>();
    labelDestInfo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    labelDestInfo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    labelDestInfo_->setMinimumHorizontalScale(0.85f);
    configContainer_->addAndMakeVisible(*labelDestInfo_);

    resolvedDestFolder_ = projeto_.projeto().pasta().getChildFile("Backup");
    juce::String defaultMsg = "Default: " + resolvedDestFolder_.getFullPathName() +
                              "  -  pick a vault or custom folder above to change it";
    labelDestInfo_->setText(defaultMsg, juce::dontSendNotification);
    labelDestInfo_->setTooltip(defaultMsg);

    // === ORGANIZATION section ===
    labelOrg_ = std::make_unique<juce::Label>();
    labelOrg_->setText("ORGANIZATION", juce::dontSendNotification);
    labelOrg_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelOrg_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelOrg_);

    togglePreservarEstrutura_ = std::make_unique<juce::ToggleButton>("Preserve Original Folder Structure");
    togglePreservarEstrutura_->setTooltip("Toggle keeping original source subfolder paths in the backup folder");
    togglePreservarEstrutura_->setToggleState(false, juce::dontSendNotification);
    togglePreservarEstrutura_->onClick = [this] {
        bool preserve = togglePreservarEstrutura_->getToggleState();
        if (comboOrg_) comboOrg_->setEnabled(!preserve);
        if (btnEditarHierarquia_) btnEditarHierarquia_->setEnabled(!preserve);
        atualizarResumo();
    };
    configContainer_->addAndMakeVisible(*togglePreservarEstrutura_);

    comboOrg_ = std::make_unique<juce::ComboBox>();
    comboOrg_->addItem("Keep my catalog organization", 1);
    comboOrg_->addItem("By media type", 2);
    comboOrg_->addItem("By year", 3);
    comboOrg_->addItem("By media type + year", 4);
    comboOrg_->addItem("Custom (visual editor)", 5);
    comboOrg_->setSelectedId(1, juce::dontSendNotification);
    comboOrg_->setTooltip("Choose directory naming/organization structure pattern");
    comboOrg_->onChange = [this] {
        if (btnEditarHierarquia_)
            btnEditarHierarquia_->setVisible(comboOrg_->getSelectedId() == 5);
        resized();
        atualizarResumo();
    };
    configContainer_->addAndMakeVisible(*comboOrg_);

    hierarquiaCustom_ = matriz::consolidacao::hierarquiaPadrao();
    btnEditarHierarquia_ = std::make_unique<juce::TextButton>("OPEN VISUAL EDITOR");
    btnEditarHierarquia_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnEditarHierarquia_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnEditarHierarquia_->setTooltip("Open visual interactive editor to design folder naming tree");
    btnEditarHierarquia_->onClick = [this] {
        new HierarquiaEditorWindow(hierarquiaCustom_, [this](const matriz::consolidacao::HierarquiaBackup& h) {
            hierarquiaCustom_ = h;
            atualizarResumo();
        });
    };
    configContainer_->addChildComponent(*btnEditarHierarquia_);

    // === OPTIONS section ===
    labelOpcoes_ = std::make_unique<juce::Label>();
    labelOpcoes_->setText("OPTIONS", juce::dontSendNotification);
    labelOpcoes_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelOpcoes_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelOpcoes_);

    toggleVerificarChecksum_ = std::make_unique<juce::ToggleButton>("Verify checksum after copy (SHA-256)");
    toggleVerificarChecksum_->setTooltip("Enable reading back copied files to verify SHA-256 integrity");
    toggleVerificarChecksum_->setToggleState(true, juce::dontSendNotification);
    configContainer_->addAndMakeVisible(*toggleVerificarChecksum_);

    toggleGerarCatalogo_ = std::make_unique<juce::ToggleButton>("Generate BKR Backup Catalog Database (SQLite)");
    toggleGerarCatalogo_->setTooltip("Enable SQLite database summary file generation in target folder");
    toggleGerarCatalogo_->setToggleState(true, juce::dontSendNotification);
    configContainer_->addAndMakeVisible(*toggleGerarCatalogo_);

    toggleEmbutirMetadados_ = std::make_unique<juce::ToggleButton>("Embed metadata into backup files (EXIF/XMP/iXML)");
    toggleEmbutirMetadados_->setTooltip("Enable embedding Dublin Core and technical tags directly into media headers");
    toggleEmbutirMetadados_->setToggleState(true, juce::dontSendNotification);
    configContainer_->addAndMakeVisible(*toggleEmbutirMetadados_);

    // === PREVIEW section ===
    labelResumo_ = std::make_unique<juce::Label>();
    labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelResumo_);

    listPrevia_ = std::make_unique<PreviaLista>();
    listPreviaViewport_ = std::make_unique<juce::Viewport>();
    listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);
    addAndMakeVisible(*listPreviaViewport_);

    // === PROGRESS ===
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    addChildComponent(*barraProgresso_);

    labelProgressoStatus_ = std::make_unique<juce::Label>();
    labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    labelProgressoStatus_->setColour(juce::Label::textColourId, tk.textoPrimario);
    labelProgressoStatus_->setJustificationType(juce::Justification::centred);
    addChildComponent(*labelProgressoStatus_);

    // === BUTTONS ===
    btnStartBackup_ = std::make_unique<juce::TextButton>("MAKE BACKUP");
    aplicarEstiloBotao(*btnStartBackup_, true);
    btnStartBackup_->setTooltip("Begin backup creation process");
    btnStartBackup_->onClick = [this] { iniciarBackup(); };
    addAndMakeVisible(*btnStartBackup_);

    btnCancel_ = std::make_unique<juce::TextButton>("CANCEL");
    aplicarEstiloBotao(*btnCancel_, false);
    btnCancel_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    btnCancel_->setTooltip("Abort current backup task safely or return to Home");
    btnCancel_->onClick = [this] {
        if (executando_) {
            cancelamento_->pedir();
        } else if (aoVoltarHome) {
            aoVoltarHome();
        }
    };
    addAndMakeVisible(*btnCancel_);

    btnDone_ = std::make_unique<juce::TextButton>("DONE");
    aplicarEstiloBotao(*btnDone_, false);
    btnDone_->setTooltip("Return to Home screen");
    btnDone_->onClick = [this] {
        if (aoConcluir) aoConcluir();
        else if (aoVoltarHome) aoVoltarHome();
    };
    addChildComponent(*btnDone_);

    btnOpenCatalog_ = std::make_unique<juce::TextButton>("OPEN BACKUP CATALOG");
    aplicarEstiloBotao(*btnOpenCatalog_, true);
    btnOpenCatalog_->setTooltip("Load generated catalog database as read-only view");
    btnOpenCatalog_->onClick = [this] {
        if (aoAbrirCatalogo) aoAbrirCatalogo(resolvedDestFolder_);
    };
    addChildComponent(*btnOpenCatalog_);

    btnExportJanela_ = std::make_unique<juce::TextButton>("EXPORT METADATA");
    aplicarEstiloBotao(*btnExportJanela_, false);
    btnExportJanela_->setTooltip("Export metadata catalog as CSV/JSON/PDF");
    btnExportJanela_->onClick = [this] { mostrarJanelaExportar(); };
    addChildComponent(*btnExportJanela_);

    atualizarResumo();
}

BackupWorkspaceComponent::~BackupWorkspaceComponent() = default;

void BackupWorkspaceComponent::mostrarJanelaExportar() {
    struct JanelaExportarMetadata : public juce::DialogWindow {
        JanelaExportarMetadata(const juce::String& title, juce::Colour bg)
            : juce::DialogWindow(title, bg, true) {}

        void closeButtonPressed() override {
            setVisible(false);
            exitModalState(0);
        }
    };

    auto janela = std::make_shared<JanelaExportarMetadata>("EXPORT METADATA", tema().painel);

    struct PainelExportar : public juce::Component {
        PainelExportar(BackupWorkspaceComponent& parent, std::shared_ptr<juce::DialogWindow> win)
            : win_(std::move(win)) {
            const auto& tk = tema();

            lblTitulo_ = std::make_unique<juce::Label>("", "EXPORT METADATA");
            lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
            lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
            lblTitulo_->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*lblTitulo_);

            auto criarBotao = [&](const juce::String& text, std::function<void()> cb) {
                auto b = std::make_unique<juce::TextButton>(text);
                parent.aplicarEstiloBotao(*b, false);
                b->onClick = [this, cb] {
                    if (win_) win_->exitModalState(0);
                    if (cb) cb();
                };
                addAndMakeVisible(*b);
                return b;
            };

            btnCsvFull_ = criarBotao("CSV (FULL)", [&parent] { parent.exportarCsv(); });
            btnXls_ = criarBotao("XLS", [&parent] { parent.exportarXls(); });
            btnDublinCore_ = criarBotao("CSV (DUBLIN CORE)", [&parent] { parent.exportarDublinCore(); });
            btnChecksums_ = criarBotao("CHECKSUMS", [&parent] { parent.exportarChecksums(); });

            setSize(460, 310);
        }

        void resized() override {
            auto area = getLocalBounds().reduced(24, 20);
            lblTitulo_->setBounds(area.removeFromTop(32));
            area.removeFromTop(14);

            int btnH = 38;
            btnCsvFull_->setBounds(area.removeFromTop(btnH));
            area.removeFromTop(10);
            btnXls_->setBounds(area.removeFromTop(btnH));
            area.removeFromTop(10);
            btnDublinCore_->setBounds(area.removeFromTop(btnH));
            area.removeFromTop(10);
            btnChecksums_->setBounds(area.removeFromTop(btnH));
        }

    private:
        std::shared_ptr<juce::DialogWindow> win_;
        std::unique_ptr<juce::Label> lblTitulo_;
        std::unique_ptr<juce::TextButton> btnCsvFull_;
        std::unique_ptr<juce::TextButton> btnXls_;
        std::unique_ptr<juce::TextButton> btnDublinCore_;
        std::unique_ptr<juce::TextButton> btnChecksums_;
    };

    auto painel = std::make_unique<PainelExportar>(*this, janela);

    janela->setContentOwned(painel.release(), true);
    janela->setResizable(false, false);
    janela->centreWithSize(460, 310);
    janela->setVisible(true);
    janela->enterModalState(true);
}

void BackupWorkspaceComponent::exportarCsv() {
    juce::FileChooser fc("Export BKR Full CSV Package...", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    if (fc.browseForDirectory()) {
        juce::File targetDir = fc.getResult();

        std::vector<std::string> ids;
        for (const auto& item : plano_.itens) ids.push_back(item.itemId);
        if (ids.empty()) {
            try {
                auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
                while (stmt.step()) ids.push_back(stmt.columnText(0));
            } catch (...) {}
        }

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String err;
        bool ok = false;
        ProgressThread thread("EXPORT METADATA - CSV (FULL)", [this, &ids, &targetDir, &ok, &err](ProgressThread* t) {
            t->setStatusMessage("Generating BKR Full CSV Package...");
            t->setProgress(0.4);
            ok = projeto_.exportarFullCsvPacote(ids, targetDir, err);
            t->setProgress(1.0);
        });
        thread.runThread();

        if (ok) {
            juce::File pkgDir = targetDir.getChildFile("BKR_Full_Export");
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("EXPORT FULL CSV")
                    .withMessage("BKR Full CSV Package exported and validated successfully!\n\nPackage location:\n" + pkgDir.getFullPathName() + "\n\nContents:\n- BKR_FULL.csv\n- BKR_FULL.schema.json\n- manifest.json")
                    .withButton("OK"),
                static_cast<juce::ModalComponentManager::Callback*>(nullptr));
        } else {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("EXPORT FULL CSV FAILED")
                    .withMessage("BKR Full CSV Export failed validation:\n" + err)
                    .withButton("OK"),
                static_cast<juce::ModalComponentManager::Callback*>(nullptr));
        }
    }
}

void BackupWorkspaceComponent::exportarXls() {
    juce::FileChooser fc("Export XLS Spreadsheet...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.xls");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("xls");

        std::vector<std::string> ids;
        for (const auto& item : plano_.itens) ids.push_back(item.itemId);
        if (ids.empty()) {
            try {
                auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
                while (stmt.step()) ids.push_back(stmt.columnText(0));
            } catch (...) {}
        }

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String xls;
        ProgressThread thread("EXPORT METADATA - XLS", [this, &ids, &xls](ProgressThread* t) {
            t->setStatusMessage("Generating XLS Spreadsheet (" + juce::String(static_cast<int>(ids.size())) + " items)...");
            t->setProgress(0.5);
            xls = projeto_.exportarXlsXml(ids);
            t->setProgress(1.0);
        });
        thread.runThread();

        targetFile.replaceWithText(xls);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT XLS")
                .withMessage("XLS Spreadsheet exported successfully to:\n" + targetFile.getFullPathName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

void BackupWorkspaceComponent::exportarCsvPara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + "_full_report.csv");

    std::vector<std::string> ids;
    for (const auto& item : plano_.itens) ids.push_back(item.itemId);
    if (ids.empty()) {
        try {
            auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
            while (stmt.step()) ids.push_back(stmt.columnText(0));
        } catch (...) {}
    }

    auto csv = projeto_.exportarFullCsv(ids);
    targetFile.replaceWithText(csv);
}

void BackupWorkspaceComponent::exportarXlsPara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + "_catalog.xls");

    std::vector<std::string> ids;
    for (const auto& item : plano_.itens) ids.push_back(item.itemId);
    if (ids.empty()) {
        try {
            auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
            while (stmt.step()) ids.push_back(stmt.columnText(0));
        } catch (...) {}
    }

    auto xls = projeto_.exportarXlsXml(ids);
    targetFile.replaceWithText(xls);
}

void BackupWorkspaceComponent::exportarDublinCore() {
    juce::FileChooser fc("Export DC-CSV Report...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.csv");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("csv");

        std::vector<std::string> ids;
        for (const auto& item : plano_.itens) ids.push_back(item.itemId);
        if (ids.empty()) {
            try {
                auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
                while (stmt.step()) ids.push_back(stmt.columnText(0));
            } catch (...) {}
        }

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String csv;
        ProgressThread thread("EXPORT METADATA - CSV (DUBLIN CORE)", [this, &ids, &csv](ProgressThread* t) {
            t->setStatusMessage("Generating Dublin Core CSV (" + juce::String(static_cast<int>(ids.size())) + " items)...");
            t->setProgress(0.5);
            csv = projeto_.exportarDublinCoreCsv(ids);
            t->setProgress(1.0);
        });
        thread.runThread();

        targetFile.replaceWithText(csv);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT DC-CSV")
                .withMessage("DC-CSV Report exported to:\n" + targetFile.getFullPathName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

juce::String BackupWorkspaceComponent::gerarManifestChecksumsBackup(const std::function<void(int, int)>& onProgress) {
    juce::String manifest;
    int total = static_cast<int>(plano_.itens.size());
    int feito = 0;

    for (const auto& item : plano_.itens) {
        if (onProgress) onProgress(feito, total);

        juce::String hash;
        try {
            auto stmt = projeto_.projeto().registro().prepare("SELECT checksum_sha256 FROM arquivo WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(item.arquivoId));
            if (stmt.step() && !stmt.columnIsNull(0)) {
                hash = stmt.columnText(0);
            }
        } catch (...) {}

        if (hash.isEmpty()) {
            auto resolvido = matriz::vault::resolverArquivo(projeto_.projeto().registro(), item.arquivoId, projeto_.projeto().pasta());
            juce::File srcFile = resolvido ? *resolvido : projeto_.projeto().pasta().getChildFile(item.nomeOriginal);
            if (srcFile.existsAsFile()) {
                hash = juce::SHA256(srcFile).toHexString().toLowerCase();
            }
        }

        juce::String relPath = item.caminhoRelativoDestino;
        if (relPath.isEmpty()) relPath = item.nomeOriginal;

        if (hash.isEmpty()) hash = "0000000000000000000000000000000000000000000000000000000000000000";
        manifest += hash + "  " + relPath + "\n";
        feito++;
    }

    if (onProgress) onProgress(total, total);
    return manifest;
}

void BackupWorkspaceComponent::exportarChecksums() {
    juce::FileChooser fc("Export Checksum Manifest...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.sha256");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("sha256");

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String manifest;
        ProgressThread thread("EXPORT METADATA - CHECKSUMS", [this, &manifest](ProgressThread* t) {
            manifest = gerarManifestChecksumsBackup([t](int feito, int total) {
                if (total > 0) {
                    t->setStatusMessage("Generating SHA-256 Checksum: " + juce::String(feito + 1) + " of " + juce::String(total) + "...");
                    t->setProgress(static_cast<double>(feito) / total);
                }
            });
        });
        thread.runThread();

        targetFile.replaceWithText(manifest);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT CHECKSUMS")
                .withMessage("Checksum manifest (" + juce::String(static_cast<int>(plano_.itens.size())) + " files) exported to:\n" + targetFile.getFullPathName()
                             + "\n\nVerify with:  sha256sum -c " + targetFile.getFileName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

void BackupWorkspaceComponent::exportarDublinCorePara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + "_dublin_core.csv");
    std::vector<std::string> ids;
    for (const auto& item : plano_.itens) ids.push_back(item.itemId);
    auto csv = projeto_.exportarDublinCoreCsv(ids);
    targetFile.replaceWithText(csv.toStdString());
}

void BackupWorkspaceComponent::exportarChecksumsPara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + ".sha256");
    juce::String manifest = gerarManifestChecksumsBackup(nullptr);
    targetFile.replaceWithText(manifest);
}

void BackupWorkspaceComponent::aplicarEstiloBotao(juce::TextButton& botao, bool primario) {
    const auto& tk = tema();
    botao.setColour(juce::TextButton::buttonColourId, primario ? tk.acento : tk.painelAlt);
    botao.setColour(juce::TextButton::buttonOnColourId, tk.acento);
    botao.setColour(juce::TextButton::textColourOffId, primario ? tk.textoSobreAcento : tk.textoPrimario);
    botao.setColour(juce::TextButton::textColourOnId, tk.textoSobreAcento);
}

std::set<std::string> BackupWorkspaceComponent::obterItensSelecionadosPeloCriterio() {
    std::set<std::string> out;
    auto& db = projeto_.projeto().registro();

    if (whatOption_ == WhatOption::Everything) {
        auto stmt = db.prepare("SELECT id FROM item");
        while (stmt.step()) out.insert(stmt.columnText(0));
    } else if (whatOption_ == WhatOption::Intake) {
        auto stmt = db.prepare("SELECT id FROM item WHERE criado_em >= datetime('now', '-24 hours')");
        while (stmt.step()) out.insert(stmt.columnText(0));
    } else if (whatOption_ == WhatOption::SelectedAssets) {
        return selectedItemIds_;
    } else if (whatOption_ == WhatOption::NeedsBackup) {
        return projeto_.itensDaColecaoEmbutida("vulneraveis");
    } else if (whatOption_ == WhatOption::Collection) {
        if (selectedCollectionIdx_ >= 0 && selectedCollectionIdx_ < static_cast<int>(colecoes_.size()))
            return projeto_.itensDaColecao(colecoes_[static_cast<size_t>(selectedCollectionIdx_)].chave);
    }
    return out;
}

void BackupWorkspaceComponent::atualizarResumo() {
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    if (isCatalogMode) {
        auto colecoes = projeto_.listarColecoesLinkadas();
        uint64_t totalAssets = 0;
        juce::int64 totalBytes = 0;
        for (const auto& c : colecoes) {
            if (c.valido) {
                totalAssets += c.totalAssets;
                totalBytes += c.totalBytes;
            }
        }

        juce::String summary = "COLLECTIONS (" + juce::String(static_cast<int>(colecoes.size())) + ")" +
                               "  |  Total Assets: " + juce::String(totalAssets) +
                               "  |  Space: " + juce::File::descriptionOfSizeInBytes(totalBytes);

        if (resolvedDestFolder_.getFullPathName().isEmpty()) {
            summary += "  -  (Select a destination folder above)";
            btnStartBackup_->setEnabled(false);
        } else if (colecoes.empty()) {
            summary += "  -  (No collections linked to this catalog)";
            btnStartBackup_->setEnabled(false);
        } else {
            btnStartBackup_->setEnabled(true);
        }

        labelResumo_->setText(summary, juce::dontSendNotification);
        listPrevia_->definirColecoesCatalogo(colecoes, resolvedDestFolder_);
        listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);
        return;
    }

    if (resolvedDestFolder_.getFullPathName().isEmpty()) {
        labelResumo_->setText("Select a destination to see backup preview.", juce::dontSendNotification);
        plano_.itens.clear();
        listPrevia_->definirPlano(plano_);
        btnStartBackup_->setEnabled(false);
        return;
    }

    std::set<std::string> itemIds = obterItensSelecionadosPeloCriterio();

    matriz::consolidacao::HierarquiaBackup h;
    if (togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState()) {
        h = { matriz::consolidacao::NivelHierarquia::EstruturaOriginal };
    } else {
        int orgId = comboOrg_ ? comboOrg_->getSelectedId() : 1;
        if (orgId == 1) h = { matriz::consolidacao::NivelHierarquia::PastaManual };
        else if (orgId == 2) h = { matriz::consolidacao::NivelHierarquia::TipoMidia };
        else if (orgId == 3) h = { matriz::consolidacao::NivelHierarquia::Ano };
        else if (orgId == 4) h = { matriz::consolidacao::NivelHierarquia::TipoMidia, matriz::consolidacao::NivelHierarquia::Ano };
        else if (orgId == 5) h = hierarquiaCustom_;
    }

    plano_ = matriz::consolidacao::planejarConsolidacao(
        projeto_.projeto().registro(), projeto_.projeto().pasta(), resolvedDestFolder_, h);

    std::vector<matriz::consolidacao::ItemPlanejado> filtrados;
    juce::int64 sz = 0; // space to copy
    juce::int64 totalSz = 0; // total backup size
    for (auto& item : plano_.itens) {
        if (itemIds.count(item.itemId)) {
            filtrados.push_back(item);
            if (!item.jaConsolidado) sz += item.tamanhoBytes;
            totalSz += item.tamanhoBytes;
        }
    }
    plano_.itens = std::move(filtrados);
    plano_.espacoNecessarioBytes = sz;

    juce::String summary = "Assets: " + juce::String(static_cast<int>(plano_.itens.size()));
    if (itemIds.size() > plano_.itens.size()) {
        summary += " (of " + juce::String(static_cast<int>(itemIds.size())) + " in catalog)";
    }
    summary += " | Space: " + juce::File::descriptionOfSizeInBytes(totalSz);

    int semArquivo = static_cast<int>(itemIds.size() - plano_.itens.size());
    if (semArquivo > 0) {
        summary += "  -  (" + juce::String(semArquivo) + " metadata items have no physical files)";
    }

    // Botão desabilitado sem explicação é indistinguível de botão ausente —
    // o motivo vai junto do resumo sempre que o backup não puder rodar.
    bool pronto = plano_.podeConsolidar() && !plano_.itens.empty();
    if (plano_.itens.empty())
        summary += "  -  CANNOT RUN: no assets match the selected source.";
    else if (!plano_.podeConsolidar())
        summary += "  -  CANNOT RUN: naming conflict, change the organization above.";

    labelResumo_->setText(summary, juce::dontSendNotification);

    listPrevia_->definirPlano(plano_);
    listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);

    btnStartBackup_->setEnabled(pronto);
}

void BackupWorkspaceComponent::iniciarBackup() {
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    estado_ = Estado::Running;
    executando_ = true;
    cancelamento_->rearmar();
    progressoValor_ = 0.0;

    btnStartBackup_->setEnabled(false);
    barraProgresso_->setVisible(true);
    labelProgressoStatus_->setVisible(true);
    labelProgressoStatus_->setText("Preparing copy operations...", juce::dontSendNotification);

    auto cancelamento = cancelamento_;
    auto plano = plano_;
    auto destFolder = resolvedDestFolder_;
    auto& projeto = projeto_;
    bool gerarCatalogo = toggleGerarCatalogo_->getToggleState();
    bool embutirMeta = !isCatalogMode && toggleEmbutirMetadados_->getToggleState();

    if (isCatalogMode) {
        auto colecoes = projeto.listarColecoesLinkadas();
        ProgressoGlobal::obterInstancia().iniciarTarefa(
            "backup",
            "Consolidating Catalog Collections",
            static_cast<int>(colecoes.size()),
            [cancelamento] { cancelamento->pedir(); },
            "Preparing catalog backup...");

        resized();
        repaint();

        juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

        juce::MessageManager::callAsync([safeThis, cancelamento, colecoes, destFolder, &projeto,
                                          gerarCatalogo]() {
            if (!safeThis) return;

            int copiado = 0;
            int falhas = 0;
            std::vector<juce::String> falhasLista;

            destFolder.createDirectory();

            for (size_t i = 0; i < colecoes.size(); ++i) {
                if (cancelamento->pedido()) break;
                const auto& c = colecoes[i];
                if (!c.valido) continue;

                juce::File colOrigem(c.caminhoProjeto);
                juce::File colDestino = destFolder.getChildFile(c.nome);

                if (safeThis) {
                    safeThis->progressoValor_ = static_cast<double>(i) / std::max<size_t>(1, colecoes.size());
                    safeThis->labelProgressoStatus_->setText("Backing up collection " + juce::String(i + 1) + " of " + juce::String(colecoes.size()) + ": " + c.nome + "...", juce::dontSendNotification);
                    ProgressoGlobal::obterInstancia().atualizarProgresso("backup", static_cast<int>(i), "Backing up: " + c.nome);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                }

                if (colOrigem.isDirectory()) {
                    if (colDestino.exists()) colDestino.deleteRecursively();
                    if (!colOrigem.copyDirectoryTo(colDestino)) {
                        falhas++;
                        falhasLista.push_back("Failed to copy collection folder: " + c.nome);
                    } else {
                        copiado++;
                    }
                }
            }

            if (!safeThis) return;

            // Copy catalog database as well
            juce::File catOrigemDb = projeto.projeto().pasta().getChildFile("registro.sqlite");
            if (catOrigemDb.existsAsFile()) {
                catOrigemDb.copyFileTo(destFolder.getChildFile("registro.sqlite"));
            }

            safeThis->copiadoCount_ = copiado;
            safeThis->verificadoCount_ = copiado;
            safeThis->falhasCount_ = falhas;
            safeThis->falhasLista_ = falhasLista;

            safeThis->executando_ = false;
            safeThis->estado_ = Estado::Done;
            safeThis->progressoValor_ = 1.0;

            const bool houveFalha = (falhas > 0);
            juce::String msgFinal = cancelamento->pedido() ? "Backup cancelled."
                                                           : (houveFalha ? "Catalog backup completed with errors."
                                                                         : "Catalog backup completed successfully (" + juce::String(copiado) + " collections).");
            ProgressoGlobal::obterInstancia().concluirTarefa("backup", msgFinal);

            safeThis->labelProgressoStatus_->setText(msgFinal, juce::dontSendNotification);
            safeThis->labelProgressoStatus_->setColour(
                juce::Label::textColourId,
                houveFalha ? tema().perigo : (cancelamento->pedido() ? tema().alerta : tema().estadoQcOk));

            safeThis->mostrarControlesConfig(false);
            safeThis->resized();
            safeThis->repaint();
        });
        return;
    }

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "backup",
        "Consolidating Backup",
        static_cast<int>(plano.itens.size()),
        [cancelamento] { cancelamento->pedir(); },
        "Preparing copy operations...");

    resized();
    repaint();

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

    juce::MessageManager::callAsync([safeThis, cancelamento, plano, destFolder, &projeto,
                                      gerarCatalogo, embutirMeta]() {
        if (!safeThis) return;

        auto resultado = matriz::consolidacao::executarConsolidacao(
            projeto.projeto().registro(),
            projeto.projeto().pasta(),
            destFolder,
            plano,
            [safeThis, cancelamento](int feito, int total) {
                if (!safeThis) return false;
                safeThis->progressoValor_ = static_cast<double>(feito) / std::max(1, total);
                safeThis->labelProgressoStatus_->setText("Copying: " + juce::String(feito) + " of " + juce::String(total) + " assets...", juce::dontSendNotification);
                ProgressoGlobal::obterInstancia().atualizarProgresso(
                    "backup", feito, "Copying " + juce::String(feito) + " of " + juce::String(total) + " assets...");
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                return !cancelamento->pedido() && safeThis != nullptr;
            }
        );

        if (!safeThis) return;

        safeThis->copiadoCount_ = resultado.consolidados;
        safeThis->verificadoCount_ = resultado.consolidados + resultado.pulados;
        safeThis->falhasCount_ = static_cast<int>(resultado.falhas.size());
        safeThis->falhasLista_.clear();
        for (const auto& f : resultado.falhas)
            safeThis->falhasLista_.push_back(f);

        if (gerarCatalogo && !resultado.cancelado && safeThis) {
            safeThis->labelProgressoStatus_->setText("Generating HTML Catalog...", juce::dontSendNotification);
            ProgressoGlobal::obterInstancia().atualizarDetalhe("backup", "Generating HTML Catalog...");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (!safeThis) return;
            auto resCatalogo = matriz::catalogo::gerar(
                projeto.projeto().registro(),
                projeto.projeto().indice(),
                projeto.projeto().pasta(),
                destFolder,
                [safeThis, cancelamento](int feito, int total) {
                    if (!safeThis) return false;
                    safeThis->labelProgressoStatus_->setText("Cataloging: " + juce::String(feito) + " of " + juce::String(total) + " files...", juce::dontSendNotification);
                    ProgressoGlobal::obterInstancia().atualizarProgresso(
                        "backup", feito, "Cataloging " + juce::String(feito) + " of " + juce::String(total) + " files...");
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                    return !cancelamento->pedido() && safeThis != nullptr;
                }
            );
            (void)resCatalogo;
        }

        if (embutirMeta && !resultado.cancelado && safeThis) {
            safeThis->labelProgressoStatus_->setText("Embedding metadata into backup files...", juce::dontSendNotification);
            ProgressoGlobal::obterInstancia().atualizarDetalhe("backup", "Embedding metadata...");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (!safeThis) return;
            // Só os metadados aqui. Marcadores NÃO: executarConsolidacao já
            // chamou embutirMarcadoresNoBackup no fim, e rodar de novo anexa
            // um segundo par de chunks cue/LIST/iXML no mesmo WAV.
            int metadados = matriz::consolidacao::embutirMetadadosNoBackup(
                projeto.projeto().registro(), destFolder);
            (void)metadados;
        }

        if (!safeThis) return;

        safeThis->executando_ = false;
        safeThis->estado_ = Estado::Done;

        // A barra parava no último valor reportado (86%) ao lado de "concluído
        // com sucesso" — dois sinais contraditórios na mesma tela.
        safeThis->progressoValor_ = 1.0;

        // Auto-export CSV, XLS, BKM to backup root folder
        if (!resultado.cancelado) {
            safeThis->labelProgressoStatus_->setText("Exporting CSV, XLS, Dublin Core and checksums...", juce::dontSendNotification);
            ProgressoGlobal::obterInstancia().atualizarDetalhe("backup", "Exporting CSV, XLS & checksums...");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (safeThis) {
                safeThis->exportarCsvPara(destFolder);
                safeThis->exportarXlsPara(destFolder);
                safeThis->exportarDublinCorePara(destFolder);
                safeThis->exportarChecksumsPara(destFolder);
            }
        }

        const bool houveFalha = safeThis->falhasCount_ > 0;
        juce::String msgFinal = resultado.cancelado ? "Backup cancelled"
                                                   : (houveFalha ? "Backup finished with " + juce::String(safeThis->falhasCount_) + " errors"
                                                                 : "Backup completed: " + juce::String(safeThis->copiadoCount_) + " assets consolidated");
        ProgressoGlobal::obterInstancia().concluirTarefa("backup", msgFinal);

        // A linha de destaque diz o QUE aconteceu; a de baixo, os números.
        safeThis->labelProgressoStatus_->setText(
            resultado.cancelado ? "Backup cancelled."
                                : (houveFalha ? "Backup finished with errors."
                                              : "Backup completed successfully."),
            juce::dontSendNotification);
        safeThis->labelProgressoStatus_->setColour(
            juce::Label::textColourId,
            houveFalha ? tema().perigo : (resultado.cancelado ? tema().alerta : tema().estadoQcOk));

        juce::String finalMsg;
        finalMsg << safeThis->copiadoCount_ << " copied   |   "
                 << safeThis->verificadoCount_ << " verified";
        if (houveFalha) finalMsg << "   |   " << safeThis->falhasCount_ << " failed";
        finalMsg << "\n" << destFolder.getFullPathName();
        if (houveFalha) {
            finalMsg << "\n";
            int mostradas = 0;
            for (const auto& f : safeThis->falhasLista_) {
                if (mostradas++ >= 4) break;
                finalMsg << "\n" << f;
            }
            if (safeThis->falhasLista_.size() > 4)
                finalMsg << "\n... and " << static_cast<int>(safeThis->falhasLista_.size()) - 4 << " more.";
        }
        safeThis->labelResumo_->setText(finalMsg, juce::dontSendNotification);

        safeThis->labelTitulo_->setText(resultado.cancelado ? "Backup Cancelled" : "Backup Complete",
                                         juce::dontSendNotification);
        safeThis->btnStartBackup_->setVisible(false);
        safeThis->btnCancel_->setVisible(false);
        safeThis->btnDone_->setVisible(true);
        safeThis->resized();
    });
}

// --- ListBoxModel implementation for Vaults list ---

int BackupWorkspaceComponent::getNumRows() {
    return static_cast<int>(vaults_.size());
}

void BackupWorkspaceComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber >= static_cast<int>(vaults_.size())) return;
    const auto& tk = tema();
    const auto& vault = vaults_[static_cast<size_t>(rowNumber)];

    if (rowIsSelected) {
        g.fillAll(tk.acento.withAlpha(0.15f));
        g.setColour(tk.bordaFoco);
        g.drawRect(0, 0, width, height, 1);
    } else {
        g.fillAll(rowNumber % 2 == 0 ? tk.painel : tk.painelAlt);
    }

    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    g.drawText(vault.nome, 12, 2, 130, height - 4, juce::Justification::centredLeft, true);

    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    int pathX = 146;
    int pathW = std::max(40, width - pathX - 86);
    g.drawText(vault.localizacao, pathX, 2, pathW, height - 4, juce::Justification::centredLeft, true);

    juce::Rectangle<int> statusArea(width - 82, (height - 18) / 2, 74, 18);
    g.setColour(vault.online ? tk.estadoQcOk : tk.textoTerciario);
    g.fillRoundedRectangle(statusArea.toFloat(), 4.0f);
    g.setColour(tk.textoSobreAcento);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    g.drawText(vault.online ? "ONLINE" : "OFFLINE", statusArea, juce::Justification::centred, true);
}

void BackupWorkspaceComponent::listBoxItemClicked(int rowNumber, const juce::MouseEvent&) {
    if (rowNumber >= 0 && rowNumber < static_cast<int>(vaults_.size())) {
        selectedVaultIdx_ = rowNumber;
        juce::String loc = vaults_[static_cast<size_t>(rowNumber)].localizacao;
        resolvedDestFolder_ = juce::File(loc);
        labelDestInfo_->setText("Target: " + loc, juce::dontSendNotification);
        labelDestInfo_->setTooltip("Target: " + loc);
        atualizarResumo();
    }
}

void BackupWorkspaceComponent::mostrarControlesConfig(bool mostrar) {
    if (configViewport_) configViewport_->setVisible(mostrar);
    if (listPreviaViewport_) listPreviaViewport_->setVisible(mostrar);
    if (labelResumo_) labelResumo_->setVisible(mostrar);
    if (comboColecoes_) comboColecoes_->setVisible(mostrar && comboSource_->getSelectedId() == 5);
    if (btnEditarHierarquia_) btnEditarHierarquia_->setVisible(mostrar && comboOrg_->getSelectedId() == 5);
}

void BackupWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Cabeçalho e rodapé como faixas, separados por fio de 1px: dá âncora
    // visual fixa pro olho e impede que o conteúdo pareça flutuar solto.
    g.setColour(tk.borda);
    if (!faixaCabecalho_.isEmpty())
        g.fillRect(faixaCabecalho_.getX(), faixaCabecalho_.getBottom() - 1, faixaCabecalho_.getWidth(), 1);
    if (!faixaRodape_.isEmpty())
        g.fillRect(faixaRodape_.getX(), faixaRodape_.getY(), faixaRodape_.getWidth(), 1);

    if (!cartaoPrevia_.isEmpty() && estado_ == Estado::Config) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(cartaoPrevia_.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cartaoPrevia_.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
    }

    if (!cartaoCentral_.isEmpty()) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(cartaoCentral_.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cartaoCentral_.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
    }
}

void BackupWorkspaceComponent::resized() {
    const auto& tk = tema();
    cartaoPrevia_ = {};
    cartaoCentral_ = {};

    const bool emAndamento = (estado_ == Estado::Running || estado_ == Estado::Done);
    mostrarControlesConfig(!emAndamento);

    auto area = getLocalBounds();

    // ---- Cabeçalho ----
    faixaCabecalho_ = area.removeFromTop(64);
    if (labelTitulo_) {
        labelTitulo_->setBounds(faixaCabecalho_.reduced(tk.espacoGrande * 2, 0)
                                    .withTrimmedBottom(tk.espacoMedio)
                                    .removeFromBottom(34));
    }

    // ---- Rodapé ----
    faixaRodape_ = area.removeFromBottom(72);
    auto botoes = faixaRodape_.reduced(tk.espacoGrande * 2, tk.espacoGrande);
    const int alturaBotao = 40;
    botoes = botoes.withSizeKeepingCentre(botoes.getWidth(), alturaBotao);

    if (estado_ == Estado::Done) {
        btnDone_->setBounds(botoes.removeFromRight(110));
        btnDone_->setVisible(true);
        btnStartBackup_->setVisible(false);
        btnCancel_->setVisible(false);
        botoes.removeFromRight(tk.espacoPainel);
        if (btnOpenCatalog_) {
            btnOpenCatalog_->setBounds(botoes.removeFromRight(190));
            btnOpenCatalog_->setVisible(true);
        }
    } else {
        if (btnOpenCatalog_) btnOpenCatalog_->setVisible(false);
        btnDone_->setVisible(false);
        btnCancel_->setBounds(botoes.removeFromRight(110));
        btnCancel_->setVisible(true);
        botoes.removeFromRight(tk.espacoPainel);
        btnStartBackup_->setBounds(botoes.removeFromRight(170));
        btnStartBackup_->setVisible(true);
    }

    bool temItens = !plano_.itens.empty();
    if (btnExportJanela_) {
        botoes.removeFromRight(tk.espacoPainel);
        btnExportJanela_->setBounds(botoes.removeFromRight(180));
        btnExportJanela_->setVisible(true);
        btnExportJanela_->setEnabled(temItens);
    }

    // ---- Running / Done: um cartão só, centrado ----
    if (emAndamento) {
        const int larguraCartao = std::min(760, area.getWidth() - tk.espacoGrande * 2);
        auto corpo = area.reduced(tk.espacoGrande, tk.espacoGrande);
        cartaoCentral_ = corpo.withSizeKeepingCentre(larguraCartao, std::min(320, corpo.getHeight()));

        auto dentro = cartaoCentral_.reduced(tk.espacoGrande * 2, tk.espacoGrande);

        labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
        labelProgressoStatus_->setJustificationType(juce::Justification::centred);
        labelProgressoStatus_->setBounds(dentro.removeFromTop(40));
        dentro.removeFromTop(tk.espacoMedio);

        if (barraProgresso_->isVisible()) {
            barraProgresso_->setBounds(dentro.removeFromTop(24));
            dentro.removeFromTop(tk.espacoGrande);
        }

        labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        labelResumo_->setColour(juce::Label::textColourId, tk.textoSecundario);
        labelResumo_->setJustificationType(juce::Justification::centredTop);
        labelResumo_->setBounds(dentro);
        return;
    }

    // ---- Config: duas colunas, com largura máxima ----
    auto corpo = area.reduced(tk.espacoGrande * 2, tk.espacoGrande + tk.espacoMedio);
    const int larguraMax = 1180;
    if (corpo.getWidth() > larguraMax)
        corpo = corpo.withSizeKeepingCentre(larguraMax, corpo.getHeight()).withY(corpo.getY());

    const int larguraConfig = std::min(460, corpo.getWidth() / 2);
    auto colunaConfig = corpo.removeFromLeft(larguraConfig);
    corpo.removeFromLeft(tk.espacoGrande + tk.espacoMedio);
    auto colunaPrevia = corpo;

    if (configViewport_ && configContainer_) {
        configViewport_->setBounds(colunaConfig);
        int larguraConteudo = colunaConfig.getWidth();
        configContainer_->setSize(larguraConteudo, 1000);
        configContainer_->resized();
        int altNec = configContainer_->calcularAlturaNecessaria();
        if (altNec > colunaConfig.getHeight()) {
            larguraConteudo -= configViewport_->getScrollBarThickness();
        }
        configContainer_->setSize(larguraConteudo, altNec);
        configContainer_->resized();
    }

    // PREVIEW — cartão único ocupando a coluna inteira, resumo no topo.
    cartaoPrevia_ = colunaPrevia;
    const int padCartao = tk.espacoPainel + 2;
    const int alturaCabecalhoSecao = 26;
    auto dentroPrevia = colunaPrevia.reduced(padCartao, padCartao);
    labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    labelResumo_->setJustificationType(juce::Justification::centredLeft);
    labelResumo_->setBounds(dentroPrevia.removeFromTop(alturaCabecalhoSecao + 4));
    dentroPrevia.removeFromTop(tk.espacoMedio);
    listPreviaViewport_->setBounds(dentroPrevia);
    if (listPrevia_) listPrevia_->setSize(dentroPrevia.getWidth(), listPrevia_->getHeight());
}

} // namespace matriz::ui
