#include "StorageWorkspaceComponent.h"
#include "Tokens.h"
#include "../Vault/Reconciliacao.h"
#include "../Vault/Volume.h"
#include "../Vault/SmartHealth.h"
#include "../Vault/DeviceUsageLog.h"
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
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " TB";
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
    kColAction = 2,
    kColHost = 3,
    kColVolume = 4,
    kColHealth = 5,
    kColReport = 6
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
            juce::Rectangle<int> rowRect(0, y, getWidth(), 17);
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

            y += 18;
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

        area.removeFromTop(3);

        // Single SMART Status Row
        juce::Rectangle<int> rowRect(0, area.getY(), getWidth(), 17);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText("SMART Status:", rowRect.removeFromLeft(130).reduced(4, 0), juce::Justification::centredLeft, true);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        juce::String sStatus = (rep_.smartStatus.isEmpty() || rep_.smartStatus == "-") ? "NOT SUPPORTED" : rep_.smartStatus;
        g.drawText(sStatus, rowRect.reduced(4, 0), juce::Justification::centredLeft, true);
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

        for (size_t i = 0; i < devs.size(); ++i) {
            const auto& d = devs[i];
            bool isSelected = (d.id == owner_.selectedVaultId_);
            bool isHovered = (static_cast<int>(i) == hoveredIndex_);

            juce::Rectangle<int> cardBounds(0, y, getWidth(), cardH);

            // Background
            if (isSelected) {
                g.setColour(tk.painelAlt);
            } else if (isHovered) {
                g.setColour(tk.painel.interpolatedWith(tk.painelAlt, 0.5f));
            } else {
                g.setColour(tk.painel);
            }
            g.fillRoundedRectangle(cardBounds.toFloat(), tk.raioMedio);

            // Border
            if (isSelected) {
                g.setColour(tk.acento);
                g.drawRoundedRectangle(cardBounds.toFloat(), tk.raioMedio, 1.5f);
            } else {
                g.setColour(tk.borda);
                g.drawRoundedRectangle(cardBounds.toFloat(), tk.raioMedio, 1.0f);
            }

            auto content = cardBounds.reduced(10, 8);

            // Header row: Badge + Online Indicator + Title + Size
            auto topRow = content.removeFromTop(20);

            // Category Badge
            juce::Colour catColor = categoriaParaCor(d.categoriaDispositivo);
            juce::String catSigla = categoriaParaSigla(d.categoriaDispositivo);
            auto badgeRect = topRow.removeFromLeft(48);
            g.setColour(catColor.withAlpha(0.20f));
            g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
            g.setColour(catColor);
            g.drawRoundedRectangle(badgeRect.toFloat(), 3.0f, 1.0f);
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            g.drawText(catSigla, badgeRect, juce::Justification::centred);

            topRow.removeFromLeft(8);

            // Online / Offline Bullet
            juce::Colour statusColor = d.online ? juce::Colour(0xff22c55e) : juce::Colour(0xff71717a);
            auto bulletRect = topRow.removeFromLeft(12);
            g.setColour(statusColor);
            g.fillEllipse(bulletRect.toFloat().reduced(2.0f));

            topRow.removeFromLeft(6);

            // Capacity on the right
            juce::String capStr = formatBytes(d.capacidadeBytes);
            auto capRect = topRow.removeFromRight(80);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            g.setColour(tk.textoPrimario);
            g.drawText(capStr, capRect, juce::Justification::centredRight, true);

            // Drive Name
            juce::String displayTitle = d.nome.isNotEmpty() ? d.nome : juce::String(d.modelo);
            if (displayTitle.isEmpty()) displayTitle = "Storage Device";
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
            g.setColour(isSelected ? tk.acento : tk.textoPrimario);
            g.drawText(displayTitle, topRow, juce::Justification::centredLeft, true);

            content.removeFromTop(4);

            // Model & Vendor row
            auto modelRow = content.removeFromTop(18);
            juce::String hwInfo;
            if (!d.vendor.empty()) hwInfo << juce::String(d.vendor) << " ";
            if (!d.modelo.empty()) hwInfo << juce::String(d.modelo);
            if (hwInfo.isEmpty()) hwInfo = "Hardware details offline / pending";
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.setColour(tk.textoSecundario);
            g.drawText(hwInfo, modelRow, juce::Justification::centredLeft, true);

            content.removeFromTop(2);

            // Location & Serial row
            auto locRow = content.removeFromTop(18);
            juce::String locInfo;
            if (d.localizacao.isNotEmpty()) locInfo << d.localizacao;
            else locInfo << "Mount: Offline";
            if (!d.sistemaArquivos.empty()) locInfo << " (" << juce::String(d.sistemaArquivos) << ")";
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.setColour(tk.textoSecundario);
            g.drawText(locInfo, locRow, juce::Justification::centredLeft, true);

            content.removeFromTop(2);

            // Stats / Activity row
            auto statsRow = content.removeFromTop(18);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            if (isSourceColumn_) {
                g.setColour(tk.textoPrimario);
                juce::String statText = juce::String(d.totalArquivos) + " files imported (" + formatBytes(d.totalBytes) + ")";
                g.drawText(statText, statsRow.removeFromLeft(200), juce::Justification::centredLeft, true);

                if (!d.ultimoIngest.empty()) {
                    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                    g.setColour(tk.textoSecundario);
                    g.drawText("Last: " + juce::String(d.ultimoIngest).substring(0, 16).replace("T", " "),
                               statsRow, juce::Justification::centredRight, true);
                }
            } else {
                g.setColour(tk.textoPrimario);
                juce::String statText = juce::String(d.totalBackups) + " backup runs (" + juce::String(d.totalItensCopiados) + " files)";
                g.drawText(statText, statsRow.removeFromLeft(200), juce::Justification::centredLeft, true);

                if (!d.ultimoBackup.empty()) {
                    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                    g.setColour(tk.textoSecundario);
                    g.drawText("Last: " + juce::String(d.ultimoBackup).substring(0, 16).replace("T", " "),
                               statsRow, juce::Justification::centredRight, true);
                }
            }

            y += cardH + gap;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const auto& devs = getDevices();
        if (devs.empty()) return;

        const int cardH = 104;
        const int gap = 8;
        int idx = e.y / (cardH + gap);

        if (idx >= 0 && idx < static_cast<int>(devs.size())) {
            owner_.selecionarDevice(devs[static_cast<size_t>(idx)].id, isSourceColumn_);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const auto& devs = getDevices();
        if (devs.empty()) {
            hoveredIndex_ = -1;
            return;
        }

        const int cardH = 104;
        const int gap = 8;
        int idx = e.y / (cardH + gap);

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
        const auto& devs = getDevices();
        const int cardH = 104;
        const int gap = 8;
        int totalH = static_cast<int>(devs.size()) * (cardH + gap) + 16;
        setSize(getWidth(), juce::jmax(getHeight(), totalH));
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
        "Hardware identities, health telemetry, and usage timelines for physical drives used across Ingest and Backup operations.");
    lblSubtitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblSubtitle_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblSubtitle_);

    btnRefresh_ = std::make_unique<juce::TextButton>("SCAN & REFRESH");
    btnRefresh_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnRefresh_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnRefresh_->onClick = [this] { recarregar(); };
    addAndMakeVisible(*btnRefresh_);

    // Left Column (Source Drives)
    lblSourceColumnTitle_ = std::make_unique<juce::Label>("lblSrcCol", "SOURCE DRIVES  (INGEST)");
    lblSourceColumnTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    lblSourceColumnTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblSourceColumnTitle_);

    sourceCardsContainer_ = std::make_unique<ColumnCardsContainer>(*this, true);
    sourceCardsViewport_ = std::make_unique<juce::Viewport>();
    sourceCardsViewport_->setViewedComponent(sourceCardsContainer_.get(), false);
    sourceCardsViewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*sourceCardsViewport_);

    // Right Column (Backup Drives)
    lblBackupColumnTitle_ = std::make_unique<juce::Label>("lblBkpCol", "BACKUP DRIVES  (DESTINATION)");
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

    // History Table Header & Button
    lblHistoryTitle_ = std::make_unique<juce::Label>("lblHistTitle", "DEVICE USAGE TIMELINE & LOGS");
    lblHistoryTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    lblHistoryTitle_->setColour(juce::Label::textColourId, tk.acento);
    inspectorContainer_->addAndMakeVisible(*lblHistoryTitle_);

    btnOpenLogFolder_ = std::make_unique<juce::TextButton>("OPEN LOGS FOLDER");
    btnOpenLogFolder_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnOpenLogFolder_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnOpenLogFolder_->onClick = [this] { abrirPastaLogs(); };
    inspectorContainer_->addAndMakeVisible(*btnOpenLogFolder_);

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
    if (driveHealthComp_) {
        atualizarSaudeSmart(true);
    }
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
    } else {
        bool exists = false;
        for (const auto& d : sourceDevices_) {
            if (d.id == targetVaultId) { exists = true; targetIsSource = true; break; }
        }
        if (!exists) {
            for (const auto& d : backupDevices_) {
                if (d.id == targetVaultId) { exists = true; targetIsSource = false; break; }
            }
        }
        if (!exists) {
            if (!sourceDevices_.empty()) {
                targetVaultId = sourceDevices_.front().id;
                targetIsSource = true;
            } else if (!backupDevices_.empty()) {
                targetVaultId = backupDevices_.front().id;
                targetIsSource = false;
            } else {
                targetVaultId.clear();
            }
        }
    }

    if (!targetVaultId.empty()) {
        selecionarDevice(targetVaultId, targetIsSource);
    }

    if (sourceCardsContainer_) {
        sourceCardsContainer_->atualizarAltura();
        sourceCardsContainer_->repaint();
    }
    if (backupCardsContainer_) {
        backupCardsContainer_->atualizarAltura();
        backupCardsContainer_->repaint();
    }
}

void StorageWorkspaceComponent::selecionarDevice(const std::string& vaultId, bool isSourceSelection) {
    selectedVaultId_ = vaultId;
    selectedIsSource_ = isSourceSelection;
    selectedUsageLogs_.clear();

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
    const auto& dev = *selectedDev;

    // Update Title & Nickname editor
    juce::String displayTitle = dev.nome.isNotEmpty() ? dev.nome : juce::String(dev.modelo);
    if (displayTitle.isEmpty()) displayTitle = "Storage Device";
    lblInspectorTitle_->setText("DEVICE INSPECTOR: " + displayTitle, juce::dontSendNotification);
    txtNickName_->setText(dev.nome, false);

    // Update Category ComboBox
    if (dev.categoriaDispositivo == "hd_externo") comboCategory_->setSelectedId(1, juce::dontSendNotification);
    else if (dev.categoriaDispositivo == "pen_drive") comboCategory_->setSelectedId(2, juce::dontSendNotification);
    else if (dev.categoriaDispositivo == "cd" || dev.categoriaDispositivo == "dvd") comboCategory_->setSelectedId(3, juce::dontSendNotification);
    else if (dev.categoriaDispositivo == "hd_interno") comboCategory_->setSelectedId(4, juce::dontSendNotification);
    else if (dev.categoriaDispositivo == "rede") comboCategory_->setSelectedId(5, juce::dontSendNotification);
    else comboCategory_->setSelectedId(6, juce::dontSendNotification);

    // Populate Read-Only Hardware Specifications
    std::vector<PropRow> props;
    props.push_back({ "Device Model:", dev.modelo.empty() ? "-" : dev.modelo });
    props.push_back({ "Vendor / Brand:", dev.vendor.empty() ? "-" : dev.vendor });
    props.push_back({ "Hardware Serial:", dev.numeroSerie.empty() ? "-" : dev.numeroSerie });
    props.push_back({ "Volume UUID:", dev.uuidVolume.empty() ? "-" : dev.uuidVolume });
    props.push_back({ "File System:", dev.sistemaArquivos.empty() ? "-" : dev.sistemaArquivos });
    props.push_back({ "Total Capacity:", formatBytes(dev.capacidadeBytes) });
    props.push_back({ "Media Location:", dev.localizacao.isEmpty() ? "Disconnected" : dev.localizacao });
    props.push_back({ "Connection State:", dev.online ? "Connected (Online)" : "Offline / Unmounted" });
    props.push_back({ "First Registered:", dev.criadoEm.empty() ? "-" : dev.criadoEm.substr(0, 16) });

    if (hardwarePropsComp_) {
        hardwarePropsComp_->setProps(props);
    }

    // Load Device Usage Timeline Logs
    auto& db = projeto_.projeto().registro();
    selectedUsageLogs_ = matriz::vault::listarHistoricoUsoDoDispositivo(db, dev.id);

    // If no timeline records exist yet for this device, initialize first online scan log
    if (selectedUsageLogs_.empty() && dev.online) {
        matriz::vault::registrarUsoDoDispositivo(db, projeto_.projeto().pasta(), dev.id, "ONLINE SCAN", 0, 0, {}, "Device connected and verified");
        selectedUsageLogs_ = matriz::vault::listarHistoricoUsoDoDispositivo(db, dev.id);
    }

    // Configure Table Header for Device Usage Timeline
    auto& hdr = tableHistory_->getHeader();
    hdr.removeAllColumns();

    lblHistoryTitle_->setText("DEVICE USAGE TIMELINE & LOGS (" + juce::String(selectedUsageLogs_.size()) + " sessions)", juce::dontSendNotification);
    hdr.addColumn("Date & Time", kColDate, 140, 110, 180);
    hdr.addColumn("Action", kColAction, 95, 80, 130);
    hdr.addColumn("Operator & Host", kColHost, 150, 110, 220);
    hdr.addColumn("Items / Volume", kColVolume, 120, 90, 180);
    hdr.addColumn("Health", kColHealth, 85, 70, 110);
    hdr.addColumn("Daily Report (.md)", kColReport, 130, 100, 200);

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
        // Log telemetry update to timeline
        matriz::vault::registrarUsoDoDispositivo(db, projeto_.projeto().pasta(), selectedDev->id, "SMART CHECK", 0, 0, {}, "Manual health telemetry scan", &rep);
        selectedUsageLogs_ = matriz::vault::listarHistoricoUsoDoDispositivo(db, selectedDev->id);
        if (tableHistory_) tableHistory_->updateContent();
    } else {
        rep = matriz::vault::obterUltimoLogOuConsultar(db, selectedDev->id, selectedDev->numeroSerie, juce::File(selectedDev->localizacao));
    }

    driveHealthComp_->setReport(rep);
}

void StorageWorkspaceComponent::abrirPastaLogs() {
    if (selectedVaultId_.empty()) return;
    const StorageDevice* selectedDev = nullptr;
    for (const auto& d : sourceDevices_) if (d.id == selectedVaultId_) { selectedDev = &d; break; }
    if (!selectedDev) for (const auto& d : backupDevices_) if (d.id == selectedVaultId_) { selectedDev = &d; break; }
    if (!selectedDev) return;

    juce::String safeDisk = selectedDev->nome.isNotEmpty() ? selectedDev->nome : juce::String(selectedDev->modelo);
    if (safeDisk.isEmpty()) safeDisk = "storage_device";
    juce::File logDir = projeto_.projeto().pasta().getChildFile("log").getChildFile("disk").getChildFile(safeDisk);
    if (!logDir.isDirectory()) {
        logDir.createDirectory();
    }
    logDir.revealToUser();
}

void StorageWorkspaceComponent::abrirRelatorioMd(const std::string& caminho) {
    if (caminho.empty()) return;
    juce::File f(caminho);
    if (f.existsAsFile()) {
        f.startAsProcess();
    } else {
        abrirPastaLogs();
    }
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
    return static_cast<int>(selectedUsageLogs_.size());
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
    if (rowNumber < 0 || rowNumber >= static_cast<int>(selectedUsageLogs_.size())) return;
    const auto& item = selectedUsageLogs_[static_cast<size_t>(rowNumber)];

    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    juce::Rectangle<int> cellBounds(6, 0, width - 12, height);

    if (columnId == kColDate) {
        g.setColour(tk.textoPrimario);
        g.drawText(item.criadoEm, cellBounds, juce::Justification::centredLeft, true);
    } else if (columnId == kColAction) {
        juce::Colour actColor = tk.acento;
        if (item.acao == "INGEST") actColor = juce::Colour(0xff0284c7);
        else if (item.acao == "BACKUP") actColor = juce::Colour(0xff10b981);
        else if (item.acao == "ONLINE SCAN" || item.acao == "SMART CHECK") actColor = tk.textoSecundario;

        auto badgeRect = cellBounds.reduced(2, 3);
        g.setColour(actColor.withAlpha(0.18f));
        g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
        g.setColour(actColor);
        g.drawRoundedRectangle(badgeRect.toFloat(), 3.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText(juce::String(item.acao).toUpperCase(), badgeRect, juce::Justification::centred);
    } else if (columnId == kColHost) {
        juce::String hostInfo = item.operador;
        if (!item.computador.empty()) hostInfo << "@" << juce::String(item.computador);
        g.setColour(tk.textoSecundario);
        g.drawText(hostInfo, cellBounds, juce::Justification::centredLeft, true);
    } else if (columnId == kColVolume) {
        juce::String volText;
        if (item.totalArquivos > 0 || item.totalBytes > 0) {
            volText << item.totalArquivos << " items (" << formatBytes(item.totalBytes) << ")";
        } else {
            volText = !item.detalhes.empty() ? juce::String::fromUTF8(item.detalhes.c_str()) : "-";
        }
        g.setColour(tk.textoPrimario);
        g.drawText(volText, cellBounds, juce::Justification::centredLeft, true);
    } else if (columnId == kColHealth) {
        juce::Colour hColor = juce::Colour(0xff22c55e);
        if (item.saudeEstado == "WARNING") hColor = juce::Colour(0xffeab308);
        else if (item.saudeEstado == "FAILING") hColor = juce::Colour(0xffef4444);
        else if (item.saudeEstado == "UNAVAILABLE") hColor = juce::Colour(0xff71717a);

        auto badgeRect = cellBounds.reduced(2, 3);
        g.setColour(hColor.withAlpha(0.18f));
        g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
        g.setColour(hColor);
        g.drawRoundedRectangle(badgeRect.toFloat(), 3.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(item.saudeEstado, badgeRect, juce::Justification::centred);
    } else if (columnId == kColReport) {
        auto btnRect = cellBounds.reduced(2, 2);
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(btnRect.toFloat(), 3.0f);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(btnRect.toFloat(), 3.0f, 1.0f);
        g.setColour(tk.acento);
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText("VIEW .MD", btnRect, juce::Justification::centred);
    }
}

void StorageWorkspaceComponent::cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(selectedUsageLogs_.size())) return;
    const auto& item = selectedUsageLogs_[static_cast<size_t>(rowNumber)];
    abrirRelatorioMd(item.relatorioMdCaminho);
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

    area.removeFromTop(10);

    // Top Section: Two Columns (Source Drives & Backup Drives)
    // Allocate ~50% of available height to give ample room for all drive cards
    int topH = juce::jmax(220, static_cast<int>(area.getHeight() * 0.50f));
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

    area.removeFromTop(12);

    // Bottom Section: Inspector & History Table
    inspectorContainer_->setBounds(area);

    auto inspArea = inspectorContainer_->getLocalBounds();
    int leftInspW = juce::jmax(340, static_cast<int>(inspArea.getWidth() * 0.38f));
    auto leftInspArea = inspArea.removeFromLeft(leftInspW);
    inspArea.removeFromLeft(20); // Gap between inspector and history table
    auto rightInspArea = inspArea;

    // Left Inspector: Device details & hardware props
    lblInspectorTitle_->setBounds(leftInspArea.removeFromTop(24));
    leftInspArea.removeFromTop(4);

    // Row 1: Nickname editor
    auto nickRow = leftInspArea.removeFromTop(24);
    lblNickName_->setBounds(nickRow.removeFromLeft(130));
    btnSaveNickName_->setBounds(nickRow.removeFromRight(65));
    nickRow.removeFromRight(6);
    txtNickName_->setBounds(nickRow);

    leftInspArea.removeFromTop(4);

    // Row 2: Category selector
    auto catRow = leftInspArea.removeFromTop(24);
    lblCategory_->setBounds(catRow.removeFromLeft(130));
    comboCategory_->setBounds(catRow);

    leftInspArea.removeFromTop(6);

    // Hardware Specs Section
    lblHardwareTitle_->setBounds(leftInspArea.removeFromTop(18));
    leftInspArea.removeFromTop(2);
    int specsH = 9 * 18 + 2;
    hardwarePropsComp_->setBounds(leftInspArea.removeFromTop(specsH));

    leftInspArea.removeFromTop(6);

    // Drive Health Section (Streamlined 2-line compact section)
    auto healthHdr = leftInspArea.removeFromTop(20);
    btnRefreshHealth_->setBounds(healthHdr.removeFromRight(65));
    healthHdr.removeFromRight(6);
    lblHealthTitle_->setBounds(healthHdr);

    leftInspArea.removeFromTop(2);
    driveHealthComp_->setBounds(leftInspArea.removeFromTop(44));

    // Right Inspector: History Table & Open Folder Button
    auto histHdr = rightInspArea.removeFromTop(24);
    btnOpenLogFolder_->setBounds(histHdr.removeFromRight(150).reduced(0, 1));
    histHdr.removeFromRight(8);
    lblHistoryTitle_->setBounds(histHdr);

    rightInspArea.removeFromTop(4);
    tableHistory_->setBounds(rightInspArea);
}

} // namespace matriz::ui
