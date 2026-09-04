#include "StorageWorkspaceComponent.h"
#include "Tokens.h"
#include "../Vault/Reconciliacao.h"
#include "../Vault/Volume.h"
#include "../Vault/SmartHealth.h"
#include "../Model/ProjectLog.h"
#include "../Model/Project.h"

namespace matriz::ui {

namespace {

juce::String formatBytes(juce::int64 bytes) {
    if (bytes <= 0) return "0 B";
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024.0, 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return juce::String(bytes / (1024.0 * 1024.0), 1) + " MB";
    if (bytes < 1024LL * 1024LL * 1024LL * 1024LL) return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0), 2) + " TB";
}

juce::String categoriaParaRotulo(const std::string& cat) {
    if (cat == "hd_externo") return "External Drive (HDD/SSD)";
    if (cat == "pen_drive") return "USB Flash Drive / SD Card";
    if (cat == "cd" || cat == "dvd") return "Optical Disc (CD/DVD/BD)";
    if (cat == "hd_interno") return "Internal Drive";
    if (cat == "rede") return "Network Share (NAS/SMB)";
    return "Other / Unknown";
}

juce::String categoriaParaSigla(const std::string& cat) {
    if (cat == "hd_externo") return "EXT HD";
    if (cat == "pen_drive") return "USB/SD";
    if (cat == "cd" || cat == "dvd") return "OPTICAL";
    if (cat == "hd_interno") return "INT HD";
    if (cat == "rede") return "NAS";
    return "DEV";
}

juce::Colour categoriaParaCor(const std::string& cat) {
    if (cat == "hd_externo") return juce::Colour(0xff3d7fd6);
    if (cat == "pen_drive") return juce::Colour(0xff0284c7);
    if (cat == "cd" || cat == "dvd") return juce::Colour(0xff9333ea);
    if (cat == "hd_interno") return juce::Colour(0xff64748b);
    if (cat == "rede") return juce::Colour(0xff0d9488);
    return juce::Colour(0xff71717a);
}

enum ColumnIds {
    kColDate = 1,
    kColFileName = 2,
    kColAssetTitle = 3,
    kColFormat = 4,
    kColSize = 5,
    kColMode = 6,
    kColCopied = 7,
    kColFailed = 8,
    kColStatus = 9
};

} // namespace

class StorageWorkspaceComponent::HardwarePropsComponent : public juce::Component {
public:
    void setProps(const std::vector<StorageWorkspaceComponent::PropRow>& props) {
        props_ = props;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        int y = 0;
        auto fontLbl = juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold));
        auto fontVal = juce::Font(juce::FontOptions(tk.tamanhoFontePequena));

        for (size_t i = 0; i < props_.size(); ++i) {
            juce::Rectangle<int> rowRect(0, y, getWidth(), 18);
            if (i % 2 == 1) {
                g.setColour(tk.painelAlt.withAlpha(0.35f));
                g.fillRoundedRectangle(rowRect.toFloat(), 2.0f);
            }

            g.setColour(tk.textoSecundario);
            g.setFont(fontLbl);
            g.drawText(props_[i].label, rowRect.removeFromLeft(130).reduced(4, 0), juce::Justification::centredLeft, true);

            g.setColour(tk.textoPrimario);
            g.setFont(fontVal);
            g.drawText(props_[i].value, rowRect.reduced(4, 0), juce::Justification::centredLeft, true);

            y += 19;
        }
    }

private:
    std::vector<StorageWorkspaceComponent::PropRow> props_;
};

class StorageWorkspaceComponent::DriveHealthComponent : public juce::Component {
public:
    void setReport(const matriz::vault::SmartHealthReport& rep) {
        rep_ = rep;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        auto area = getLocalBounds();
        if (area.isEmpty()) return;

        // Top Status Header: Colored Bullet + Status Text
        auto statusRow = area.removeFromTop(18);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));

        // Bullet ●
        g.setColour(rep_.stateColour);
        juce::String label = rep_.stateLabel.isNotEmpty() ? rep_.stateLabel : "HEALTHY";
        g.drawText(juce::String::charToString(0x25CF) + "  " + label, statusRow, juce::Justification::centredLeft, true);

        area.removeFromTop(4);

        std::vector<StorageWorkspaceComponent::PropRow> rows;
        rows.push_back({ "SMART Status:", (rep_.smartStatus.isEmpty() || rep_.smartStatus == "-") ? "NOT SUPPORTED" : rep_.smartStatus });
        rows.push_back({ "Temperature:", rep_.temperatureC >= 0 ? (juce::String(rep_.temperatureC) + " \xc2\xb0" + "C") : "-" });
        rows.push_back({ "Power-On Hours:", rep_.powerOnHours >= 0 ? (juce::String(rep_.powerOnHours) + " h") : "-" });
        rows.push_back({ "Reallocated:", rep_.reallocatedSectors >= 0 ? juce::String(rep_.reallocatedSectors) : "0" });
        rows.push_back({ "Pending:", rep_.pendingSectors >= 0 ? juce::String(rep_.pendingSectors) : "0" });
        rows.push_back({ "Uncorrectable:", rep_.uncorrectableSectors >= 0 ? juce::String(rep_.uncorrectableSectors) : "0" });
        rows.push_back({ "Last Scan:", (rep_.lastScanTime.isEmpty() || rep_.lastScanTime == "-") ? juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M") : rep_.lastScanTime });

        int y = area.getY();
        auto fontLbl = juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold));
        auto fontVal = juce::Font(juce::FontOptions(tk.tamanhoFontePequena));

        for (size_t i = 0; i < rows.size(); ++i) {
            juce::Rectangle<int> rowRect(0, y, getWidth(), 18);
            if (i % 2 == 1) {
                g.setColour(tk.painelAlt.withAlpha(0.35f));
                g.fillRoundedRectangle(rowRect.toFloat(), 2.0f);
            }

            g.setColour(tk.textoSecundario);
            g.setFont(fontLbl);
            g.drawText(rows[i].label, rowRect.removeFromLeft(130).reduced(4, 0), juce::Justification::centredLeft, true);

            g.setColour(tk.textoPrimario);
            g.setFont(fontVal);
            g.drawText(rows[i].value, rowRect.reduced(4, 0), juce::Justification::centredLeft, true);

            y += 19;
        }
    }

private:
    matriz::vault::SmartHealthReport rep_;
};

class StorageWorkspaceComponent::ColumnCardsContainer : public juce::Component {
public:
    ColumnCardsContainer(StorageWorkspaceComponent& owner, bool isSourceColumn)
        : owner_(owner), isSourceColumn_(isSourceColumn) {}

    const std::vector<StorageDevice>& getDevices() const {
        return isSourceColumn_ ? owner_.sourceDevices_ : owner_.backupDevices_;
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        const auto& devs = getDevices();

        if (devs.empty()) {
            auto bounds = getLocalBounds().reduced(16);

            if (!isSourceColumn_ && owner_.lastStorageError_.isNotEmpty()) {
                juce::Rectangle<int> errBox = bounds.removeFromTop(120);
                g.setColour(tk.perigo.withAlpha(0.12f));
                g.fillRoundedRectangle(errBox.toFloat(), tk.raioMedio);
                g.setColour(tk.perigo);
                g.drawRoundedRectangle(errBox.toFloat(), tk.raioMedio, 1.0f);

                auto innerErr = errBox.reduced(12, 8);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
                g.setColour(tk.perigo);
                g.drawText("STORAGE REGISTRATION DIAGNOSTIC", innerErr.removeFromTop(18), juce::Justification::centredLeft);

                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
                g.setColour(tk.textoPrimario);
                g.drawText(owner_.lastStorageError_, innerErr.removeFromTop(20), juce::Justification::centredLeft, true);

                if (owner_.lastStorageErrorDetails_.isNotEmpty()) {
                    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                    g.setColour(tk.textoSecundario);
                    g.drawText(owner_.lastStorageErrorDetails_, innerErr.removeFromTop(38), juce::Justification::centredLeft, true);
                }
                bounds.removeFromTop(16);
            }

            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            if (isSourceColumn_) {
                g.drawText("No source media registered yet.\nImport footage or media via INTAKE to automatically register source devices.",
                           bounds, juce::Justification::centred, true);
            } else {
                g.drawText("No backup storage drives registered yet.\nExecute a backup in the BACKUP tab to register physical target media.",
                           bounds, juce::Justification::centred, true);
            }
            return;
        }

        const int cardH = 104;
        const int gap = 8;
        int y = 0;

        auto fontTitle = juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold));
        auto fontMeta = juce::Font(juce::FontOptions(tk.tamanhoFontePequena));
        auto fontBadge = juce::Font(juce::FontOptions(10.0f, juce::Font::bold));

        for (size_t i = 0; i < devs.size(); ++i) {
            const auto& d = devs[i];
            juce::Rectangle<int> cardBounds(0, y, getWidth(), cardH);
            bool isSelected = (d.id == owner_.selectedVaultId_ && isSourceColumn_ == owner_.selectedIsSource_);
            bool isHovered = (static_cast<int>(i) == hoveredIndex_);

            // Background
            if (isSelected) {
                g.setColour(tk.painelAlt.brighter(0.12f));
            } else if (isHovered) {
                g.setColour(tk.painelAlt.brighter(0.06f));
            } else {
                g.setColour(tk.painelAlt);
            }
            g.fillRoundedRectangle(cardBounds.toFloat(), tk.raioMedio);

            // Border
            if (isSelected) {
                g.setColour(tk.acento);
                g.drawRoundedRectangle(cardBounds.toFloat().reduced(0.5f), tk.raioMedio, 2.0f);
            } else {
                g.setColour(tk.borda);
                g.drawRoundedRectangle(cardBounds.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
            }

            auto inner = cardBounds.reduced(10, 8);

            // Top Row: Category Pill + Device Name + Status Pill
            auto topRow = inner.removeFromTop(22);

            // Category Pill (Left)
            juce::String sigla = categoriaParaSigla(d.categoriaDispositivo);
            int siglaW = 54;
            juce::Rectangle<int> siglaRect = topRow.removeFromLeft(siglaW);
            juce::Colour catColor = categoriaParaCor(d.categoriaDispositivo);
            g.setColour(catColor.withAlpha(0.20f));
            g.fillRoundedRectangle(siglaRect.toFloat(), 3.0f);
            g.setColour(catColor);
            g.drawRoundedRectangle(siglaRect.toFloat(), 3.0f, 1.0f);
            g.setFont(fontBadge);
            g.drawText(sigla, siglaRect, juce::Justification::centred);

            topRow.removeFromLeft(8);

            // Status Pill (Right)
            juce::String statusText = d.online ? "ONLINE" : "OFFLINE";
            juce::Colour statusColor = d.online ? juce::Colour(0xff22c55e) : juce::Colour(0xff71717a);
            int statusW = 64;
            juce::Rectangle<int> statusRect = topRow.removeFromRight(statusW);
            g.setColour(statusColor.withAlpha(0.15f));
            g.fillRoundedRectangle(statusRect.toFloat(), 3.0f);
            g.setColour(statusColor);
            g.drawRoundedRectangle(statusRect.toFloat(), 3.0f, 1.0f);
            g.setFont(fontBadge);
            g.drawText(statusText, statusRect, juce::Justification::centred);

            // Name (Middle)
            juce::String displayName = d.nome.isNotEmpty() ? d.nome : (!d.modelo.empty() ? juce::String(d.modelo) : (d.localizacao.isNotEmpty() ? d.localizacao : "Storage Device"));
            g.setColour(tk.textoPrimario);
            g.setFont(fontTitle);
            g.drawText(displayName, topRow, juce::Justification::centredLeft, true);

            inner.removeFromTop(5);

            // Middle Row: Specs (Capacity, FS, Category Full)
            auto midRow = inner.removeFromTop(18);
            juce::String specs;
            if (d.capacidadeBytes > 0) specs += formatBytes(d.capacidadeBytes) + "  •  ";
            if (!d.sistemaArquivos.empty()) specs += juce::String(d.sistemaArquivos).toUpperCase() + "  •  ";
            specs += categoriaParaRotulo(d.categoriaDispositivo);

            g.setColour(tk.textoSecundario);
            g.setFont(fontMeta);
            g.drawText(specs, midRow, juce::Justification::centredLeft, true);

            // Bottom Row: Source vs Backup specific stats
            auto btmRow = inner.removeFromTop(18);
            juce::String btmInfo;
            if (isSourceColumn_) {
                if (d.totalArquivos > 0) {
                    btmInfo += juce::String(d.totalArquivos) + (d.totalArquivos == 1 ? " Ingested File (" : " Ingested Files (") + formatBytes(d.totalBytes) + ")";
                    if (!d.ultimoIngest.empty()) {
                        btmInfo += "  •  " + juce::String(d.ultimoIngest).substring(0, 10);
                    }
                } else {
                    btmInfo += "Registered Source Device";
                }
            } else {
                if (d.totalBackups > 0) {
                    btmInfo += juce::String(d.totalBackups) + (d.totalBackups == 1 ? " Backup Run (" : " Backup Runs (") + juce::String(d.totalItensCopiados) + " items copied)";
                    if (!d.ultimoBackup.empty()) {
                        btmInfo += "  •  " + juce::String(d.ultimoBackup).substring(0, 10);
                    }
                } else {
                    btmInfo += "Registered Backup Target";
                }
            }

            if (!d.numeroSerie.empty()) {
                btmInfo += "  |  S/N: " + juce::String(d.numeroSerie);
            }

            g.setColour(tk.textoTerciario);
            g.setFont(fontMeta);
            g.drawText(btmInfo, btmRow, juce::Justification::centredLeft, true);

            y += cardH + gap;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const int cardH = 104;
        const int gap = 8;
        int idx = e.getPosition().getY() / (cardH + gap);
        const auto& devs = getDevices();
        if (idx >= 0 && idx < static_cast<int>(devs.size())) {
            owner_.selecionarDevice(devs[static_cast<size_t>(idx)].id, isSourceColumn_);
            owner_.sourceCardsContainer_->repaint();
            owner_.backupCardsContainer_->repaint();
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int cardH = 104;
        const int gap = 8;
        int idx = e.getPosition().getY() / (cardH + gap);
        const auto& devs = getDevices();
        if (idx >= 0 && idx < static_cast<int>(devs.size())) {
            if (hoveredIndex_ != idx) {
                hoveredIndex_ = idx;
                repaint();
            }
        } else if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            repaint();
        }
    }

    void atualizarAltura() {
        const int cardH = 104;
        const int gap = 8;
        const auto& devs = getDevices();
        int totalH = static_cast<int>(devs.size()) * (cardH + gap) + 16;
        setSize(getWidth(), juce::jmax(totalH, 140));
        repaint();
    }

private:
    StorageWorkspaceComponent& owner_;
    bool isSourceColumn_ = false;
    int hoveredIndex_ = -1;
};

StorageWorkspaceComponent::StorageWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    isCatalog_ = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    const auto& tk = tema();

    lblTitle_ = std::make_unique<juce::Label>("lblTitle", "STORAGE & MEDIA LOG");
    lblTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitle_);

    lblSubtitle_ = std::make_unique<juce::Label>(
        "lblSubtitle",
        "Hardware identities and activity records for physical drives used across Ingest and Backup operations.");
    lblSubtitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblSubtitle_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblSubtitle_);

    btnRefresh_ = std::make_unique<juce::TextButton>("SCAN & REFRESH");
    btnRefresh_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnRefresh_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnRefresh_->onClick = [this] { recarregar(); };
    addAndMakeVisible(*btnRefresh_);

    // Left Column (Source Drives)
    lblSourceColumnTitle_ = std::make_unique<juce::Label>("lblSrcCol", "SOURCE DRIVES  (INGEST / CAMERA CARDS)");
    lblSourceColumnTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    lblSourceColumnTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblSourceColumnTitle_);

    sourceCardsContainer_ = std::make_unique<ColumnCardsContainer>(*this, true);
    sourceCardsViewport_ = std::make_unique<juce::Viewport>();
    sourceCardsViewport_->setViewedComponent(sourceCardsContainer_.get(), false);
    sourceCardsViewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*sourceCardsViewport_);

    // Right Column (Backup Drives)
    lblBackupColumnTitle_ = std::make_unique<juce::Label>("lblBkpCol", "BACKUP DRIVES  (DESTINATIONS / ARCHIVE)");
    lblBackupColumnTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    lblBackupColumnTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblBackupColumnTitle_);

    backupCardsContainer_ = std::make_unique<ColumnCardsContainer>(*this, false);
    backupCardsViewport_ = std::make_unique<juce::Viewport>();
    backupCardsViewport_->setViewedComponent(backupCardsContainer_.get(), false);
    backupCardsViewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*backupCardsViewport_);

    // Bottom Area: Inspector & History Container
    inspectorContainer_ = std::make_unique<juce::Component>();
    addAndMakeVisible(*inspectorContainer_);

    lblInspectorTitle_ = std::make_unique<juce::Label>("lblInspTitle", "DEVICE INSPECTOR");
    lblInspectorTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    lblInspectorTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    inspectorContainer_->addAndMakeVisible(*lblInspectorTitle_);

    lblNickName_ = std::make_unique<juce::Label>("lblNickName", "Drive Nickname:");
    lblNickName_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblNickName_->setColour(juce::Label::textColourId, tk.textoSecundario);
    inspectorContainer_->addAndMakeVisible(*lblNickName_);

    txtNickName_ = std::make_unique<juce::TextEditor>();
    txtNickName_->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
    txtNickName_->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    txtNickName_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    txtNickName_->onReturnKey = [this] { salvarNomeVault(); };
    inspectorContainer_->addAndMakeVisible(*txtNickName_);

    btnSaveNickName_ = std::make_unique<juce::TextButton>("SAVE");
    btnSaveNickName_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnSaveNickName_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnSaveNickName_->onClick = [this] { salvarNomeVault(); };
    inspectorContainer_->addAndMakeVisible(*btnSaveNickName_);

    lblCategory_ = std::make_unique<juce::Label>("lblCategory", "Drive Category:");
    lblCategory_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblCategory_->setColour(juce::Label::textColourId, tk.textoSecundario);
    inspectorContainer_->addAndMakeVisible(*lblCategory_);

    comboCategory_ = std::make_unique<juce::ComboBox>();
    comboCategory_->addItem("External Drive (HDD / SSD)", 1);
    comboCategory_->addItem("USB Flash Drive / SD Card", 2);
    comboCategory_->addItem("Optical Disc (CD / DVD / BD)", 3);
    comboCategory_->addItem("Internal Drive", 4);
    comboCategory_->addItem("Network Storage (NAS / SMB)", 5);
    comboCategory_->addItem("Other / Unknown", 6);
    comboCategory_->onChange = [this] {
        int id = comboCategory_->getSelectedId();
        std::string novaCat = "desconhecido";
        if (id == 1) novaCat = "hd_externo";
        else if (id == 2) novaCat = "pen_drive";
        else if (id == 3) novaCat = "cd";
        else if (id == 4) novaCat = "hd_interno";
        else if (id == 5) novaCat = "rede";
        salvarCategoriaVault(novaCat);
    };
    inspectorContainer_->addAndMakeVisible(*comboCategory_);

    lblHardwareTitle_ = std::make_unique<juce::Label>("lblHwTitle", "HARDWARE IDENTITY (READ-ONLY)");
    lblHardwareTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblHardwareTitle_->setColour(juce::Label::textColourId, tk.acento);
    inspectorContainer_->addAndMakeVisible(*lblHardwareTitle_);

    hardwarePropsComp_ = std::make_unique<HardwarePropsComponent>();
    inspectorContainer_->addAndMakeVisible(*hardwarePropsComp_);

    // Drive Health (Compact subordinate section)
    lblHealthTitle_ = std::make_unique<juce::Label>("lblHealthTitle", "DRIVE HEALTH");
    lblHealthTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblHealthTitle_->setColour(juce::Label::textColourId, tk.acento);
    inspectorContainer_->addAndMakeVisible(*lblHealthTitle_);

    btnRefreshHealth_ = std::make_unique<juce::TextButton>("REFRESH");
    btnRefreshHealth_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnRefreshHealth_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnRefreshHealth_->onClick = [this] { atualizarSaudeSmart(true); };
    inspectorContainer_->addAndMakeVisible(*btnRefreshHealth_);

    driveHealthComp_ = std::make_unique<DriveHealthComponent>();
    inspectorContainer_->addAndMakeVisible(*driveHealthComp_);

    // History Table
    lblHistoryTitle_ = std::make_unique<juce::Label>("lblHistTitle", "ACTIVITY LOG");
    lblHistoryTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    lblHistoryTitle_->setColour(juce::Label::textColourId, tk.acento);
    inspectorContainer_->addAndMakeVisible(*lblHistoryTitle_);

    tableHistory_ = std::make_unique<juce::TableListBox>("StorageHistoryTable", this);
    tableHistory_->setHeaderHeight(24);
    tableHistory_->setRowHeight(24);
    inspectorContainer_->addAndMakeVisible(*tableHistory_);

    carregarDados();
    startTimer(2000);
}

StorageWorkspaceComponent::~StorageWorkspaceComponent() {
    stopTimer();
}

void StorageWorkspaceComponent::timerCallback() {
    if (isShowing()) {
        try {
            projeto_.reavaliarVaults();
        } catch (...) {}
        carregarDados();
    }
}

void StorageWorkspaceComponent::recarregar() {
    try {
        projeto_.reavaliarVaults();
    } catch (...) {}
    carregarDados();
}

void StorageWorkspaceComponent::carregarDados() {
    sourceDevices_.clear();
    backupDevices_.clear();
    lastStorageError_ = "";
    lastStorageErrorDetails_ = "";

    // Sync all mounted physical drives & resolve unlinked project files
    try {
        matriz::vault::sincronizarDrivesDoProjeto(projeto_.projeto().registro(), projeto_.projeto().projetoId());
    } catch (...) {}

    // Scan ProjectLog for any recent storage error diagnostics
    try {
        matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
        juce::String logText = pLog.readContent();
        int idx = logText.lastIndexOf("Storage: failed to register");
        if (idx >= 0) {
            int lineStart = logText.substring(0, idx).lastIndexOfChar('\n');
            if (lineStart < 0) lineStart = 0;
            int lineEnd = logText.indexOfChar(idx, '\n');
            if (lineEnd < 0) lineEnd = logText.length();
            lastStorageError_ = logText.substring(lineStart, lineEnd).trim();
            if (lastStorageError_.startsWith("###")) {
                lastStorageError_ = lastStorageError_.substring(3).trim();
            }

            int nextSection = logText.indexOf(lineEnd, "###");
            juce::String block = (nextSection > lineEnd) ? logText.substring(lineEnd, nextSection) : logText.substring(lineEnd);
            juce::StringArray lines;
            lines.addLines(block);
            juce::StringArray detailItems;
            for (auto& l : lines) {
                auto trimmed = l.trim();
                if (trimmed.startsWith("- Error:") || trimmed.startsWith("- Destination:")) {
                    detailItems.add(trimmed.substring(2));
                }
            }
            lastStorageErrorDetails_ = detailItems.joinIntoString("   |   ");
        }
    } catch (...) {}

    auto& db = projeto_.projeto().registro();
    std::vector<StorageDevice> allDevs;

    // Query all registered vaults
    try {
        auto stmt = db.prepare(
            "SELECT id, COALESCE(projeto_id, ''), COALESCE(nome, ''), COALESCE(tipo, ''), "
            "       COALESCE(localizacao, ''), COALESCE(uuid_volume, ''), COALESCE(vendor, ''), "
            "       COALESCE(modelo, ''), COALESCE(numero_serie, ''), COALESCE(capacidade_bytes, 0), "
            "       COALESCE(removivel, 0), COALESCE(sistema_arquivos, ''), "
            "       COALESCE(categoria_dispositivo, 'desconhecido'), COALESCE(categoria_manual, 0), "
            "       COALESCE(status, 'offline'), COALESCE(criado_em, ''), COALESCE(visto_em, '') "
            "FROM vault "
            "ORDER BY nome ASC, criado_em DESC;");

        while (stmt.step()) {
            StorageDevice dev;
            dev.id = stmt.columnText(0);
            dev.projetoId = stmt.columnText(1);
            dev.nome = stmt.columnText(2);
            dev.tipo = stmt.columnText(3);
            dev.localizacao = stmt.columnText(4);
            dev.uuidVolume = stmt.columnText(5);
            dev.vendor = stmt.columnText(6);
            dev.modelo = stmt.columnText(7);
            dev.numeroSerie = stmt.columnText(8);
            dev.capacidadeBytes = stmt.columnInt(9);
            dev.removivel = (stmt.columnInt(10) != 0);
            dev.sistemaArquivos = stmt.columnText(11);
            dev.categoriaDispositivo = stmt.columnText(12);
            dev.categoriaManual = (stmt.columnInt(13) != 0);
            dev.status = stmt.columnText(14);
            dev.criadoEm = stmt.columnText(15);
            dev.vistoEm = stmt.columnText(16);

            // Evaluate live online state
            dev.online = (dev.status == "online");
            if (!dev.online && !dev.localizacao.isEmpty()) {
                juce::File loc(dev.localizacao);
                if (loc.isDirectory()) dev.online = true;
            }

            allDevs.push_back(std::move(dev));
        }
    } catch (...) {}

    // Categorize into Source and Backup drives
    for (auto& dev : allDevs) {
        // Check ingest activity
        try {
            auto stmt = db.prepare(
                "SELECT COUNT(*), COALESCE(SUM(tamanho_bytes), 0), COALESCE(MAX(criado_em), '') "
                "FROM arquivo WHERE vault_id = ?;");
            stmt.bind(1, matriz::db::Value::of(dev.id));
            if (stmt.step()) {
                dev.totalArquivos = static_cast<int>(stmt.columnInt(0));
                dev.totalBytes = stmt.columnInt(1);
                dev.ultimoIngest = stmt.columnText(2);
                if (dev.totalArquivos > 0) {
                    dev.isSource = true;
                }
            }
        } catch (...) {}

        // Check backup activity
        try {
            auto stmt = db.prepare(
                "SELECT COUNT(*), COALESCE(SUM(itens_copiados), 0), COALESCE(SUM(itens_falha), 0), COALESCE(MAX(criado_em), '') "
                "FROM vault_evento WHERE vault_id = ?;");
            stmt.bind(1, matriz::db::Value::of(dev.id));
            if (stmt.step()) {
                dev.totalBackups = static_cast<int>(stmt.columnInt(0));
                dev.totalItensCopiados = static_cast<int>(stmt.columnInt(1));
                dev.totalItensFalha = static_cast<int>(stmt.columnInt(2));
                dev.ultimoBackup = stmt.columnText(3);
                if (dev.totalBackups > 0) {
                    dev.isBackup = true;
                }
            }
        } catch (...) {}

        if (dev.isSource) {
            sourceDevices_.push_back(dev);
        }
        if (dev.isBackup) {
            backupDevices_.push_back(dev);
        }
        if (!dev.isSource && !dev.isBackup) {
            if (dev.tipo == "backup") {
                backupDevices_.push_back(dev);
            } else {
                sourceDevices_.push_back(dev);
            }
        }
    }

    // Maintain or establish selection
    std::string targetVaultId = selectedVaultId_;
    bool targetIsSource = selectedIsSource_;

    if (targetVaultId.empty()) {
        if (!sourceDevices_.empty()) {
            targetVaultId = sourceDevices_.front().id;
            targetIsSource = true;
        } else if (!backupDevices_.empty()) {
            targetVaultId = backupDevices_.front().id;
            targetIsSource = false;
        }
    }

    selecionarDevice(targetVaultId, targetIsSource);

    if (sourceCardsContainer_) sourceCardsContainer_->atualizarAltura();
    if (backupCardsContainer_) backupCardsContainer_->atualizarAltura();
    repaint();
}

void StorageWorkspaceComponent::selecionarDevice(const std::string& vaultId, bool isSourceSelection) {
    selectedVaultId_ = vaultId;
    selectedIsSource_ = isSourceSelection;
    selectedSourceLogs_.clear();
    selectedBackupLogs_.clear();

    const StorageDevice* selectedDev = nullptr;
    const auto& currentList = isSourceSelection ? sourceDevices_ : backupDevices_;
    for (const auto& d : currentList) {
        if (d.id == vaultId) {
            selectedDev = &d;
            break;
        }
    }

    if (selectedDev == nullptr) {
        // Fallback to the other list if not found in current
        const auto& otherList = isSourceSelection ? backupDevices_ : sourceDevices_;
        for (const auto& d : otherList) {
            if (d.id == vaultId) {
                selectedDev = &d;
                selectedIsSource_ = !isSourceSelection;
                break;
            }
        }
    }

    if (selectedDev == nullptr) {
        inspectorContainer_->setVisible(false);
        return;
    }

    inspectorContainer_->setVisible(true);
    const auto& dev = *selectedDev;

    // Header Title
    juce::String displayName = dev.nome.isNotEmpty() ? dev.nome : (!dev.modelo.empty() ? juce::String(dev.modelo) : (dev.localizacao.isNotEmpty() ? dev.localizacao : "Storage Device"));
    lblInspectorTitle_->setText("DEVICE INSPECTOR: " + displayName, juce::dontSendNotification);

    // Populate Nickname Editor
    txtNickName_->setText(dev.nome, juce::dontSendNotification);

    // Populate Category Combo
    int catId = 6;
    if (dev.categoriaDispositivo == "hd_externo") catId = 1;
    else if (dev.categoriaDispositivo == "pen_drive") catId = 2;
    else if (dev.categoriaDispositivo == "cd" || dev.categoriaDispositivo == "dvd") catId = 3;
    else if (dev.categoriaDispositivo == "hd_interno") catId = 4;
    else if (dev.categoriaDispositivo == "rede") catId = 5;
    comboCategory_->setSelectedId(catId, juce::dontSendNotification);

    // Populate Hardware Specs List
    std::vector<PropRow> props;
    props.push_back({ "Vendor:", dev.vendor.empty() ? "-" : juce::String(dev.vendor) });
    props.push_back({ "Model:", dev.modelo.empty() ? "-" : juce::String(dev.modelo) });
    props.push_back({ "Serial Number:", dev.numeroSerie.empty() ? "-" : juce::String(dev.numeroSerie) });
    props.push_back({ "Volume UUID:", dev.uuidVolume.empty() ? "-" : juce::String(dev.uuidVolume) });
    props.push_back({ "Mount Location:", dev.localizacao.isEmpty() ? "-" : dev.localizacao });
    props.push_back({ "File System:", dev.sistemaArquivos.empty() ? "-" : juce::String(dev.sistemaArquivos).toUpperCase() });
    props.push_back({ "Capacity:", dev.capacidadeBytes > 0 ? formatBytes(dev.capacidadeBytes) : "-" });
    props.push_back({ "Media Type:", dev.removivel ? "Removable (External / SD Card)" : "Fixed / Internal Storage" });
    props.push_back({ "Registered Date:", dev.criadoEm.empty() ? "-" : juce::String(dev.criadoEm) });

    if (hardwarePropsComp_) {
        hardwarePropsComp_->setProps(props);
    }

    // Configure Table Header & Load History Logs
    auto& db = projeto_.projeto().registro();
    auto& hdr = tableHistory_->getHeader();
    hdr.removeAllColumns();

    if (selectedIsSource_) {
        try {
            auto stmt = db.prepare(
                "SELECT a.id, COALESCE(a.item_id, ''), COALESCE(a.caminho_relativo, ''), "
                "       COALESCE(a.caminho_absoluto_origem, ''), COALESCE(a.papel, ''), "
                "       COALESCE(a.tamanho_bytes, 0), COALESCE(a.criado_em, ''), "
                "       COALESCE(i.titulo, '') "
                "FROM arquivo a "
                "LEFT JOIN item i ON i.id = a.item_id "
                "WHERE a.vault_id = ? "
                "ORDER BY a.criado_em DESC;");
            stmt.bind(1, matriz::db::Value::of(dev.id));

            while (stmt.step()) {
                SourceItemLog item;
                item.arquivoId = stmt.columnText(0);
                item.itemId = stmt.columnText(1);
                juce::String rel = stmt.columnText(2);
                juce::String orig = stmt.columnText(3);
                item.nomeArquivo = juce::File(orig.isNotEmpty() ? orig : rel).getFileName();
                item.papel = stmt.columnText(4);
                item.formato = juce::File(orig.isNotEmpty() ? orig : rel).getFileExtension().replace(".", "");
                item.tamanhoBytes = stmt.columnInt(5);
                item.criadoEm = stmt.columnText(6);
                item.titulo = stmt.columnText(7);
                selectedSourceLogs_.push_back(std::move(item));
            }
        } catch (...) {}

        lblHistoryTitle_->setText("INGESTED ASSETS LOG (" + juce::String(selectedSourceLogs_.size()) + " files received)", juce::dontSendNotification);
        hdr.addColumn("Date Imported", kColDate, 140, 100, 180);
        hdr.addColumn("File Name", kColFileName, 190, 120, 280);
        hdr.addColumn("Asset Title", kColAssetTitle, 160, 100, 240);
        hdr.addColumn("Role / Format", kColFormat, 110, 80, 160);
        hdr.addColumn("Size", kColSize, 90, 70, 130);
    } else {
        try {
            auto stmt = db.prepare(
                "SELECT id, vault_id, modo, itens_copiados, itens_falha, cancelado, criado_em "
                "FROM vault_evento "
                "WHERE vault_id = ? "
                "ORDER BY criado_em DESC;");
            stmt.bind(1, matriz::db::Value::of(dev.id));

            while (stmt.step()) {
                BackupEventLog ev;
                ev.id = stmt.columnText(0);
                ev.vaultId = stmt.columnText(1);
                ev.modo = stmt.columnText(2);
                ev.itensCopiados = static_cast<int>(stmt.columnInt(3));
                ev.itensFalha = static_cast<int>(stmt.columnInt(4));
                ev.cancelado = (stmt.columnInt(5) != 0);
                ev.criadoEm = stmt.columnText(6);
                selectedBackupLogs_.push_back(std::move(ev));
            }
        } catch (...) {}

        lblHistoryTitle_->setText("BACKUP RUNS LOG (" + juce::String(selectedBackupLogs_.size()) + " runs dispatched)", juce::dontSendNotification);
        hdr.addColumn("Date & Time", kColDate, 140, 100, 180);
        hdr.addColumn("Backup Mode", kColMode, 110, 80, 160);
        hdr.addColumn("Copied Items", kColCopied, 90, 60, 120);
        hdr.addColumn("Failed", kColFailed, 70, 50, 100);
        hdr.addColumn("Status", kColStatus, 90, 70, 120);
    }

    if (tableHistory_) {
        tableHistory_->updateContent();
        tableHistory_->repaint();
    }

    atualizarSaudeSmart(false);

    inspectorContainer_->repaint();
}

void StorageWorkspaceComponent::atualizarSaudeSmart(bool forcarNovaConsulta) {
    if (selectedVaultId_.empty() || !driveHealthComp_) return;

    const StorageDevice* selectedDev = nullptr;
    const auto& currentList = selectedIsSource_ ? sourceDevices_ : backupDevices_;
    for (const auto& d : currentList) {
        if (d.id == selectedVaultId_) {
            selectedDev = &d;
            break;
        }
    }

    if (!selectedDev) {
        const auto& otherList = selectedIsSource_ ? backupDevices_ : sourceDevices_;
        for (const auto& d : otherList) {
            if (d.id == selectedVaultId_) {
                selectedDev = &d;
                break;
            }
        }
    }

    if (!selectedDev) return;

    auto& db = projeto_.projeto().registro();
    matriz::vault::SmartHealthReport rep;

    if (forcarNovaConsulta) {
        rep = matriz::vault::consultarSaudeSmart(selectedDev->numeroSerie, juce::File(selectedDev->localizacao));
        matriz::vault::gravarLogSmart(db, selectedDev->id, rep);
    } else {
        rep = matriz::vault::obterUltimoLogOuConsultar(db, selectedDev->id, selectedDev->numeroSerie, juce::File(selectedDev->localizacao));
    }

    driveHealthComp_->setReport(rep);
}

void StorageWorkspaceComponent::salvarNomeVault() {
    if (selectedVaultId_.empty()) return;

    juce::String novoNome = txtNickName_->getText().trim();

    try {
        auto& db = projeto_.projeto().registro();
        auto stmt = db.prepare("UPDATE vault SET nome = ?, visto_em = datetime('now') WHERE id = ?;");
        stmt.bind(1, matriz::db::Value::of(novoNome.toStdString()));
        stmt.bind(2, matriz::db::Value::of(selectedVaultId_));
        stmt.step();
    } catch (...) {}

    for (auto& d : sourceDevices_) {
        if (d.id == selectedVaultId_) d.nome = novoNome;
    }
    for (auto& d : backupDevices_) {
        if (d.id == selectedVaultId_) d.nome = novoNome;
    }

    lblInspectorTitle_->setText("DEVICE INSPECTOR: " + (novoNome.isNotEmpty() ? novoNome : "Storage Device"), juce::dontSendNotification);

    if (sourceCardsContainer_) sourceCardsContainer_->repaint();
    if (backupCardsContainer_) backupCardsContainer_->repaint();
}

void StorageWorkspaceComponent::salvarCategoriaVault(const std::string& novaCategoria) {
    if (selectedVaultId_.empty()) return;

    try {
        auto& db = projeto_.projeto().registro();
        auto stmt = db.prepare("UPDATE vault SET categoria_dispositivo = ?, categoria_manual = 1, visto_em = datetime('now') WHERE id = ?;");
        stmt.bind(1, matriz::db::Value::of(novaCategoria));
        stmt.bind(2, matriz::db::Value::of(selectedVaultId_));
        stmt.step();
    } catch (...) {}

    for (auto& d : sourceDevices_) {
        if (d.id == selectedVaultId_) {
            d.categoriaDispositivo = novaCategoria;
            d.categoriaManual = true;
        }
    }
    for (auto& d : backupDevices_) {
        if (d.id == selectedVaultId_) {
            d.categoriaDispositivo = novaCategoria;
            d.categoriaManual = true;
        }
    }

    if (sourceCardsContainer_) sourceCardsContainer_->repaint();
    if (backupCardsContainer_) backupCardsContainer_->repaint();
}

// TableListBoxModel implementation
int StorageWorkspaceComponent::getNumRows() {
    return selectedIsSource_ ? static_cast<int>(selectedSourceLogs_.size())
                             : static_cast<int>(selectedBackupLogs_.size());
}

void StorageWorkspaceComponent::paintRowBackground(juce::Graphics& g, int rowNumber, int, int, bool rowIsSelected) {
    const auto& tk = tema();
    if (rowIsSelected) {
        g.fillAll(tk.acento.withAlpha(0.18f));
    } else if (rowNumber % 2 == 1) {
        g.fillAll(tk.painelAlt.withAlpha(0.40f));
    }
}

void StorageWorkspaceComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    const auto& tk = tema();
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    juce::Rectangle<int> cellBounds(6, 0, width - 12, height);

    if (selectedIsSource_) {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(selectedSourceLogs_.size())) return;
        const auto& item = selectedSourceLogs_[static_cast<size_t>(rowNumber)];

        if (columnId == kColDate) {
            g.setColour(tk.textoSecundario);
            g.drawText(item.criadoEm, cellBounds, juce::Justification::centredLeft, true);
        } else if (columnId == kColFileName) {
            g.setColour(tk.textoPrimario);
            g.drawText(item.nomeArquivo, cellBounds, juce::Justification::centredLeft, true);
        } else if (columnId == kColAssetTitle) {
            g.setColour(tk.textoSecundario);
            g.drawText(item.titulo.isNotEmpty() ? item.titulo : "-", cellBounds, juce::Justification::centredLeft, true);
        } else if (columnId == kColFormat) {
            juce::String roleFmt = item.papel;
            if (item.formato.isNotEmpty()) roleFmt += " (" + item.formato.toUpperCase() + ")";
            g.setColour(tk.textoSecundario);
            g.drawText(roleFmt, cellBounds, juce::Justification::centredLeft, true);
        } else if (columnId == kColSize) {
            g.setColour(tk.textoPrimario);
            g.drawText(formatBytes(item.tamanhoBytes), cellBounds, juce::Justification::centredLeft, true);
        }
    } else {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(selectedBackupLogs_.size())) return;
        const auto& ev = selectedBackupLogs_[static_cast<size_t>(rowNumber)];

        if (columnId == kColDate) {
            g.setColour(tk.textoPrimario);
            g.drawText(ev.criadoEm, cellBounds, juce::Justification::centredLeft, true);
        } else if (columnId == kColMode) {
            g.setColour(tk.textoSecundario);
            g.drawText(ev.modo.toUpperCase(), cellBounds, juce::Justification::centredLeft, true);
        } else if (columnId == kColCopied) {
            g.setColour(tk.textoPrimario);
            g.drawText(juce::String(ev.itensCopiados), cellBounds, juce::Justification::centredLeft, true);
        } else if (columnId == kColFailed) {
            g.setColour(ev.itensFalha > 0 ? tk.perigo : tk.textoTerciario);
            g.drawText(juce::String(ev.itensFalha), cellBounds, juce::Justification::centredLeft, true);
        } else if (columnId == kColStatus) {
            juce::String statusText = "SUCCESS";
            juce::Colour statusColor = juce::Colour(0xff22c55e); // Green

            if (ev.cancelado) {
                statusText = "CANCELLED";
                statusColor = tk.perigo;
            } else if (ev.itensFalha > 0) {
                statusText = "PARTIAL";
                statusColor = tk.alerta;
            }

            auto badgeRect = cellBounds.reduced(2, 3);
            g.setColour(statusColor.withAlpha(0.18f));
            g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
            g.setColour(statusColor);
            g.drawRoundedRectangle(badgeRect.toFloat(), 3.0f, 1.0f);
            g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
            g.drawText(statusText, badgeRect, juce::Justification::centred);
        }
    }
}

void StorageWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Divider line between header and main section
    g.setColour(tk.borda);
    g.fillRect(0, 68, getWidth(), 1);
}

void StorageWorkspaceComponent::resized() {
    auto area = getLocalBounds().reduced(20, 12);

    // Header Area
    auto headerArea = area.removeFromTop(48);
    btnRefresh_->setBounds(headerArea.removeFromRight(140).reduced(0, 8));
    lblTitle_->setBounds(headerArea.removeFromTop(24));
    lblSubtitle_->setBounds(headerArea);

    area.removeFromTop(12);

    // Top Section: Two Columns (Source Drives & Backup Drives)
    // Allocate ~42% of remaining height to the top columns
    int topH = juce::jmax(180, static_cast<int>(area.getHeight() * 0.42f));
    auto topArea = area.removeFromTop(topH);

    int colW = (topArea.getWidth() - 16) / 2;
    auto leftColArea = topArea.removeFromLeft(colW);
    topArea.removeFromLeft(16); // Gap between columns
    auto rightColArea = topArea;

    // Left Column
    lblSourceColumnTitle_->setBounds(leftColArea.removeFromTop(24));
    leftColArea.removeFromTop(4);
    sourceCardsViewport_->setBounds(leftColArea);
    if (sourceCardsContainer_) {
        sourceCardsContainer_->setSize(leftColArea.getWidth() - 12, sourceCardsContainer_->getHeight());
        sourceCardsContainer_->atualizarAltura();
    }

    // Right Column
    lblBackupColumnTitle_->setBounds(rightColArea.removeFromTop(24));
    rightColArea.removeFromTop(4);
    backupCardsViewport_->setBounds(rightColArea);
    if (backupCardsContainer_) {
        backupCardsContainer_->setSize(rightColArea.getWidth() - 12, backupCardsContainer_->getHeight());
        backupCardsContainer_->atualizarAltura();
    }

    area.removeFromTop(14);

    // Bottom Section: Inspector & History Table
    inspectorContainer_->setBounds(area);

    auto inspArea = inspectorContainer_->getLocalBounds();
    int leftInspW = juce::jmax(340, static_cast<int>(inspArea.getWidth() * 0.40f));
    auto leftInspArea = inspArea.removeFromLeft(leftInspW);
    inspArea.removeFromLeft(20); // Gap between inspector and history table
    auto rightInspArea = inspArea;

    // Left Inspector: Device details & hardware props
    lblInspectorTitle_->setBounds(leftInspArea.removeFromTop(26));
    leftInspArea.removeFromTop(6);

    // Row 1: Nickname editor
    auto nickRow = leftInspArea.removeFromTop(28);
    lblNickName_->setBounds(nickRow.removeFromLeft(130));
    btnSaveNickName_->setBounds(nickRow.removeFromRight(65));
    nickRow.removeFromRight(6);
    txtNickName_->setBounds(nickRow);

    leftInspArea.removeFromTop(6);

    // Row 2: Category selector
    auto catRow = leftInspArea.removeFromTop(28);
    lblCategory_->setBounds(catRow.removeFromLeft(130));
    comboCategory_->setBounds(catRow);

    leftInspArea.removeFromTop(10);

    // Hardware Specs Section
    lblHardwareTitle_->setBounds(leftInspArea.removeFromTop(20));
    leftInspArea.removeFromTop(4);
    int specsH = 9 * 19 + 6;
    hardwarePropsComp_->setBounds(leftInspArea.removeFromTop(specsH));

    leftInspArea.removeFromTop(10);

    // Drive Health Section (subordinate to Device Inspector)
    auto healthHdr = leftInspArea.removeFromTop(22);
    btnRefreshHealth_->setBounds(healthHdr.removeFromRight(65));
    healthHdr.removeFromRight(6);
    lblHealthTitle_->setBounds(healthHdr);

    leftInspArea.removeFromTop(4);
    driveHealthComp_->setBounds(leftInspArea);

    // Right Inspector: History Table
    lblHistoryTitle_->setBounds(rightInspArea.removeFromTop(26));
    rightInspArea.removeFromTop(6);
    tableHistory_->setBounds(rightInspArea);
}

} // namespace matriz::ui

