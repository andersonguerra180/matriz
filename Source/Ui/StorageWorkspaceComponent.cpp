#include "StorageWorkspaceComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
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
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0), 2) + " TB";
}

juce::String formatCompactDate(const juce::String& isoDateStr) {
    if (isoDateStr.isEmpty()) return {};
    juce::String s = isoDateStr.trim();
    if (s.length() >= 10 && s[4] == '-' && s[7] == '-') {
        int day = s.substring(8, 10).getIntValue();
        int month = s.substring(5, 7).getIntValue();
        static const char* mesesEn[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        static const char* mesesPt[] = { "jan", "fev", "mar", "abr", "mai", "jun",
                                         "jul", "ago", "set", "out", "nov", "dez" };
        bool isPt = (matriz::i18n::localeAtivo().startsWith("pt"));
        const char** meses = isPt ? mesesPt : mesesEn;
        if (month >= 1 && month <= 12 && day > 0) {
            return juce::String::formatted("%02d %s", day, meses[month - 1]);
        }
    }
    return s.substring(0, 10);
}

juce::String formatSmartStatus(const juce::String& status) {
    auto s = status.trim();
    if (s.isEmpty() || s.equalsIgnoreCase("NOT SUPPORTED") || s.equalsIgnoreCase("NOT_SUPPORTED") || s == "-") {
        return matriz::i18n::t("storage.not_supported");
    }
    if (s.equalsIgnoreCase("PASSED") || s.equalsIgnoreCase("VERIFIED")) {
        return matriz::i18n::t("storage.passed");
    }
    if (s.equalsIgnoreCase("FAILING") || s.equalsIgnoreCase("FAILED")) {
        return matriz::i18n::t("storage.failing");
    }
    if (s.equalsIgnoreCase("UNKNOWN")) {
        return matriz::i18n::t("storage.unknown");
    }
    return s;
}

juce::String formatHealthTitle(const matriz::vault::SmartHealthReport& rep, bool online) {
    if (!online) return "Offline";
    if (rep.state == matriz::vault::HealthState::Healthy) return matriz::i18n::t("storage.healthy");
    if (rep.state == matriz::vault::HealthState::Warning) return matriz::i18n::t("storage.attention");
    if (rep.state == matriz::vault::HealthState::Failing) return matriz::i18n::t("storage.critical");
    if (rep.state == matriz::vault::HealthState::Unavailable) return matriz::i18n::t("storage.healthy");
    if (rep.stateLabel.isNotEmpty()) {
        auto lbl = rep.stateLabel.toLowerCase();
        return lbl.substring(0, 1).toUpperCase() + lbl.substring(1);
    }
    return matriz::i18n::t("storage.healthy");
}

juce::String formatSensors(const matriz::vault::SmartHealthReport& rep) {
    if (rep.temperatureC > 0) {
        juce::String s = juce::String(rep.temperatureC) + " \u00B0C";
        if (rep.powerOnHours > 0) {
            s << " \u00B7 " << rep.powerOnHours << "h " << matriz::i18n::t("storage.power");
        }
        return s;
    }
    return matriz::i18n::t("storage.temp_na");
}

juce::String formatSectors(const matriz::vault::SmartHealthReport& rep) {
    juce::int64 realloc = juce::jmax<juce::int64>(0, rep.reallocatedSectors);
    juce::int64 pending = juce::jmax<juce::int64>(0, rep.pendingSectors);
    return matriz::i18n::t("storage.realloc") + juce::String(realloc) + " \u00B7 " + matriz::i18n::t("storage.pending") + juce::String(pending);
}

juce::String formatLogDateTime(const juce::String& isoStr) {
    if (isoStr.isEmpty()) return "-";
    juce::String s = isoStr.trim();
    if (s.length() >= 16 && (s[4] == '-' || s[4] == '/')) {
        return s.substring(5, 10) + " " + s.substring(11, 16);
    }
    return s.substring(0, 16);
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

// =============================================================================
// LogCalendarComponent: Interactive Month / Day Log Browser
// =============================================================================
class StorageWorkspaceComponent::LogCalendarComponent : public juce::Component {
public:
    explicit LogCalendarComponent(StorageWorkspaceComponent& owner)
        : owner_(owner) {
        auto now = juce::Time::getCurrentTime();
        mesAnoAtual_ = juce::Time(now.getYear(), now.getMonth(), 1, 0, 0);

        btnMesAnterior_ = std::make_unique<juce::TextButton>("\u2039");
        btnMesAnterior_->onClick = [this] { navegarMes(-1); };
        addAndMakeVisible(*btnMesAnterior_);

        btnMesProximo_ = std::make_unique<juce::TextButton>("\u203A");
        btnMesProximo_->onClick = [this] { navegarMes(1); };
        addAndMakeVisible(*btnMesProximo_);

        lblMesAno_ = std::make_unique<juce::Label>("lblMesAno", "");
        lblMesAno_->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*lblMesAno_);

        atualizarRotuloMes();
    }

    void navegarMes(int deltaMeses) {
        int y = mesAnoAtual_.getYear();
        int m = mesAnoAtual_.getMonth() + deltaMeses;
        while (m < 0) { m += 12; y--; }
        while (m > 11) { m -= 12; y++; }
        mesAnoAtual_ = juce::Time(y, m, 1, 0, 0);
        atualizarRotuloMes();
        repaint();
    }

    void atualizarRotuloMes() {
        const auto& tk = tema();
        lblMesAno_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        lblMesAno_->setColour(juce::Label::textColourId, tk.textoPrimario);

        btnMesAnterior_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btnMesAnterior_->setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        btnMesAnterior_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);

        btnMesProximo_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btnMesProximo_->setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        btnMesProximo_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);

        static const char* mesesEn[] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };
        static const char* mesesPt[] = {
            "Janeiro", "Fevereiro", "Março", "Abril", "Maio", "Junho",
            "Julho", "Agosto", "Setembro", "Outubro", "Novembro", "Dezembro"
        };
        bool isPt = (matriz::i18n::localeAtivo().startsWith("pt"));
        const char** meses = isPt ? mesesPt : mesesEn;
        int m = juce::jlimit(0, 11, mesAnoAtual_.getMonth());
        lblMesAno_->setText(juce::String(meses[m]) + " " + juce::String(mesAnoAtual_.getYear()), juce::dontSendNotification);
    }

    void resized() override {
        auto header = getLocalBounds().removeFromTop(26);
        btnMesAnterior_->setBounds(header.removeFromLeft(30));
        btnMesProximo_->setBounds(header.removeFromRight(30));
        lblMesAno_->setBounds(header);
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();

        auto area = getLocalBounds();
        area.removeFromTop(26); // Month navigation header

        // Card Container for Calendar
        g.setColour(tk.painel);
        g.fillRoundedRectangle(area.toFloat(), 6.0f);
        g.setColour(tk.borda.withAlpha(0.6f));
        g.drawRoundedRectangle(area.toFloat(), 6.0f, 1.0f);

        auto inner = area.reduced(8, 6);

        // Days of week header (S M T W T F S) / (D S T Q Q S S)
        auto daysHeader = inner.removeFromTop(18);
        static const char* diaSemanaEn[] = { "S", "M", "T", "W", "T", "F", "S" };
        static const char* diaSemanaPt[] = { "D", "S", "T", "Q", "Q", "S", "S" };
        bool isPtSemana = (matriz::i18n::localeAtivo().startsWith("pt"));
        const char** diaSemana = isPtSemana ? diaSemanaPt : diaSemanaEn;
        int colW = daysHeader.getWidth() / 7;

        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.setColour(tk.textoTerciario);
        for (int i = 0; i < 7; ++i) {
            juce::Rectangle<int> col(daysHeader.getX() + i * colW, daysHeader.getY(), colW, daysHeader.getHeight());
            g.drawText(diaSemana[i], col, juce::Justification::centred);
        }

        inner.removeFromTop(2);

        // Grid of days
        cellBounds_.clear();
        cellDates_.clear();

        int ano = mesAnoAtual_.getYear();
        int mes = mesAnoAtual_.getMonth();
        juce::Time primeiroDia(ano, mes, 1, 0, 0);
        int primeiroDiaSemana = primeiroDia.getDayOfWeek(); // 0 = Sunday

        int diasNoMes = 31;
        if (mes == 1) { // Feb
            bool leap = (ano % 4 == 0 && (ano % 100 != 0 || ano % 400 == 0));
            diasNoMes = leap ? 29 : 28;
        } else if (mes == 3 || mes == 5 || mes == 8 || mes == 10) {
            diasNoMes = 30;
        }

        int rowH = inner.getHeight() / 6;
        int diaCont = 1;

        auto now = juce::Time::getCurrentTime();
        juce::String hojeStr = now.formatted("%Y-%m-%d");

        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 7; ++col) {
                int cellIdx = row * 7 + col;
                if (cellIdx < primeiroDiaSemana || diaCont > diasNoMes) {
                    continue;
                }

                juce::Rectangle<int> cell(inner.getX() + col * colW, inner.getY() + row * rowH, colW, rowH);
                cellBounds_.push_back(cell);

                juce::String dataStr = juce::String::formatted("%04d-%02d-%02d", ano, mes + 1, diaCont);
                cellDates_.push_back(dataStr);

                bool isSelected = (owner_.selectedDate_ == dataStr);
                bool isToday = (dataStr == hojeStr);
                bool isHovered = (hoveredCellIdx_ == static_cast<int>(cellDates_.size() - 1));

                juce::Colour pillColor = (tk.fundo.getBrightness() > 0.5f) ? juce::Colour(0xffb87a1a) : tk.acento;

                if (isSelected || (owner_.selectedDate_.isEmpty() && isToday)) {
                    auto pillRect = cell.reduced(2, 1);
                    g.setColour(pillColor);
                    g.fillRoundedRectangle(pillRect.toFloat(), 4.0f);

                    g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
                    g.setColour(juce::Colours::white);
                    g.drawText(juce::String(diaCont), pillRect, juce::Justification::centred);
                } else {
                    if (isHovered) {
                        g.setColour(tk.painelAlt);
                        g.fillRoundedRectangle(cell.reduced(1).toFloat(), 3.0f);
                    }

                    g.setFont(juce::Font(juce::FontOptions(12.0f)));
                    g.setColour(isToday ? pillColor : tk.textoPrimario);
                    g.drawText(juce::String(diaCont), cell, juce::Justification::centred);
                }

                // Dot indicator if logs exist for this date
                auto it = owner_.datesWithLogs_.find(dataStr);
                if (it != owner_.datesWithLogs_.end() && it->second > 0) {
                    int dotSize = 4;
                    int dotX = cell.getCentreX() - dotSize / 2;
                    int dotY = cell.getBottom() - dotSize - 1;
                    g.setColour(isSelected ? juce::Colours::white : tk.acento);
                    g.fillEllipse((float)dotX, (float)dotY, (float)dotSize, (float)dotSize);
                }

                diaCont++;
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        int oldHover = hoveredCellIdx_;
        hoveredCellIdx_ = -1;
        for (size_t i = 0; i < cellBounds_.size(); ++i) {
            if (cellBounds_[i].contains(e.getPosition())) {
                hoveredCellIdx_ = static_cast<int>(i);
                break;
            }
        }
        if (oldHover != hoveredCellIdx_) {
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoveredCellIdx_ != -1) {
            hoveredCellIdx_ = -1;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        for (size_t i = 0; i < cellBounds_.size(); ++i) {
            if (cellBounds_[i].contains(e.getPosition())) {
                owner_.selecionarDataCalendario(cellDates_[i]);
                return;
            }
        }
    }

private:
    StorageWorkspaceComponent& owner_;
    juce::Time mesAnoAtual_;
    std::unique_ptr<juce::TextButton> btnMesAnterior_;
    std::unique_ptr<juce::TextButton> btnMesProximo_;
    std::unique_ptr<juce::Label> lblMesAno_;

    std::vector<juce::Rectangle<int>> cellBounds_;
    std::vector<juce::String> cellDates_;
    int hoveredCellIdx_ = -1;
};

// =============================================================================
// ColumnCardsContainer: HD Cards (Clean Single-Column Layout matching Spec)
// =============================================================================
class StorageWorkspaceComponent::ColumnCardsContainer : public juce::Component {
public:
    ColumnCardsContainer(StorageWorkspaceComponent& owner, bool isSourceColumn)
        : owner_(owner), isSourceColumn_(isSourceColumn) {}

    const std::vector<StorageDevice>& getDevices() const {
        return isSourceColumn_ ? owner_.sourceDevices_ : owner_.backupDevices_;
    }

    static constexpr int kCardW = 336;
    static constexpr int kCardH = 248;
    static constexpr int kGap = 13;

    juce::Rectangle<int> getCardBounds(size_t index) const {
        int availW = getWidth();
        int cardsPerRow = juce::jmax(1, (availW + kGap) / (kCardW + kGap));
        int cardW = juce::jmin(kCardW, juce::jmax(273, availW));
        int row = static_cast<int>(index) / cardsPerRow;
        int col = static_cast<int>(index) % cardsPerRow;
        return juce::Rectangle<int>(col * (cardW + kGap), row * (kCardH + kGap), cardW, kCardH);
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
                g.drawText(i18n::t("storage.diag_titulo"), innerErr.removeFromTop(18), juce::Justification::centredLeft);

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
                g.drawText(i18n::t("storage.empty_source"),
                           bounds, juce::Justification::centred, true);
            } else {
                g.drawText(i18n::t("storage.empty_backup"),
                           bounds, juce::Justification::centred, true);
            }
            return;
        }

        for (size_t i = 0; i < devs.size(); ++i) {
            const auto& d = devs[i];
            bool isSelected = (d.id == owner_.selectedVaultId_);
            bool isHovered = (static_cast<int>(i) == hoveredIndex_);

            juce::Rectangle<int> cardBounds = getCardBounds(i);

            // Card Background
            if (isSelected) {
                g.setColour(tk.painelAlt);
            } else if (isHovered) {
                g.setColour(tk.painel.interpolatedWith(tk.painelAlt, 0.45f));
            } else {
                g.setColour(tk.painel);
            }
            g.fillRoundedRectangle(cardBounds.toFloat(), 6.3f);

            // Card Border
            if (isSelected) {
                g.setColour(tk.acento);
                g.drawRoundedRectangle(cardBounds.toFloat(), 6.3f, 1.5f);
            } else {
                g.setColour(isHovered ? tk.borda.brighter(0.25f) : tk.borda);
                g.drawRoundedRectangle(cardBounds.toFloat(), 6.3f, 1.0f);
            }

            auto content = cardBounds.reduced(13, 11);

            // =================================================================
            // 1. TOP HEADER ROW
            // Left: "Bunker 4TB · Online"
            // Right: "3.64 TB"
            // =================================================================
            auto headerRow = content.removeFromTop(19);

            // Total capacity on the right
            juce::int64 capTotal = d.espacoTotalBytes > 0 ? d.espacoTotalBytes : d.capacidadeBytes;
            juce::String capStr = formatBytes(capTotal);
            g.setFont(juce::Font(juce::FontOptions(13.0f)));
            g.setColour(tk.textoSecundario);
            g.drawText(capStr, headerRow.removeFromRight(68), juce::Justification::centredRight);

            // Name & Online / Offline on the left
            juce::String displayTitle = d.nome.isNotEmpty() ? d.nome : juce::String(d.modelo);
            if (displayTitle.isEmpty()) displayTitle = i18n::t("storage.storage_device");

            auto titleFont = juce::Font(juce::FontOptions(13.5f, juce::Font::bold));
            g.setFont(titleFont);
            g.setColour(isSelected ? tk.acento : tk.textoPrimario);

            int maxTitleW = headerRow.getWidth() - 60;
            int titleW = juce::jmin(maxTitleW, (int)juce::GlyphArrangement::getStringWidth(titleFont, displayTitle) + 2);
            g.drawText(displayTitle, headerRow.removeFromLeft(titleW), juce::Justification::centredLeft, true);

            headerRow.removeFromLeft(3);
            g.setFont(juce::Font(juce::FontOptions(12.5f)));
            g.setColour(tk.textoTerciario);
            g.drawText("\u00B7", headerRow.removeFromLeft(6), juce::Justification::centred);

            headerRow.removeFromLeft(3);
            g.setFont(juce::Font(juce::FontOptions(12.5f)));
            juce::Colour statusColor = d.online ? juce::Colour(0xff22c55e) : tk.perigo;
            g.setColour(statusColor);
            g.drawText(d.online ? "Online" : "Offline", headerRow, juce::Justification::centredLeft);

            // Divider 1
            content.removeFromTop(6);
            auto div1 = content.removeFromTop(1);
            g.setColour(tk.borda.withAlpha(0.5f));
            g.fillRect(div1);
            content.removeFromTop(6);

            // =================================================================
            // 2. HARDWARE SPECIFICATIONS (4 Rows: Model, Serial, Mount, Format)
            // =================================================================
            auto drawRow = [&](juce::Rectangle<int> rowRect, const juce::String& label, const juce::String& value) {
                g.setFont(juce::Font(juce::FontOptions(12.5f)));
                g.setColour(tk.textoTerciario);
                g.drawText(label, rowRect.removeFromLeft(58), juce::Justification::centredLeft);

                g.setFont(juce::Font(juce::FontOptions(12.5f)));
                g.setColour(tk.textoPrimario);
                g.drawText(value, rowRect, juce::Justification::centredRight, true);
            };

            // Model
            juce::String hwModel;
            if (!d.vendor.empty() && !juce::String(d.modelo).containsIgnoreCase(d.vendor)) {
                hwModel << juce::String(d.vendor) << " ";
            }
            if (!d.modelo.empty()) {
                hwModel << juce::String(d.modelo);
            } else if (hwModel.isEmpty()) {
                hwModel = i18n::t("storage.storage_device");
            }
            drawRow(content.removeFromTop(17), i18n::t("storage.model"), hwModel.trim());
            content.removeFromTop(1);

            // Serial
            juce::String snText = !d.numeroSerie.empty() ? juce::String(d.numeroSerie)
                                : (!d.uuidVolume.empty() ? juce::String(d.uuidVolume) : "-");
            drawRow(content.removeFromTop(17), i18n::t("storage.serial"), snText);
            content.removeFromTop(1);

            // Mount
            juce::String mountText = d.localizacao.isNotEmpty() ? d.localizacao : juce::String(i18n::t("storage.unmounted"));
            drawRow(content.removeFromTop(17), i18n::t("storage.mount"), mountText);
            content.removeFromTop(1);

            // Format
            juce::String fsText = !d.sistemaArquivos.empty() ? juce::String(d.sistemaArquivos).toUpperCase() : "HFS";
            fsText << " \u00B7 " << (d.online ? i18n::t("storage.connected") : i18n::t("storage.disconnected"));
            drawRow(content.removeFromTop(17), i18n::t("storage.format"), fsText);

            // Divider 2
            content.removeFromTop(6);
            auto div2 = content.removeFromTop(1);
            g.setColour(tk.borda.withAlpha(0.5f));
            g.fillRect(div2);
            content.removeFromTop(6);

            // =================================================================
            // 3. DRIVE HEALTH & SMART SECTION
            // Header: Healthy, SMART, Sensors, Sectors
            // =================================================================
            auto hTitleRow = content.removeFromTop(17);
            juce::String hTitle = formatHealthTitle(d.smartReport, d.online);
            juce::Colour hColor = (hTitle == i18n::t("storage.healthy") || hTitle == "Healthy") ? juce::Colour(0xff22c55e)
                                : ((hTitle == i18n::t("storage.attention") || hTitle == "Attention") ? tk.alerta
                                : ((hTitle == i18n::t("storage.critical") || hTitle == "Critical") ? tk.perigo : tk.textoSecundario));
            g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            g.setColour(hColor);
            g.drawText(hTitle, hTitleRow, juce::Justification::centredLeft);

            content.removeFromTop(1);
            drawRow(content.removeFromTop(17), i18n::t("storage.smart"), formatSmartStatus(d.smartReport.smartStatus));
            content.removeFromTop(1);
            drawRow(content.removeFromTop(17), i18n::t("storage.sensors"), formatSensors(d.smartReport));
            content.removeFromTop(1);
            drawRow(content.removeFromTop(17), i18n::t("storage.sectors"), formatSectors(d.smartReport));

            // Divider 3
            content.removeFromTop(6);
            auto div3 = content.removeFromTop(1);
            g.setColour(tk.borda.withAlpha(0.5f));
            g.fillRect(div3);
            content.removeFromTop(6);

            // =================================================================
            // 4. BOTTOM PROGRESS BAR & SUMMARY
            // =================================================================
            auto barRect = content.removeFromTop(5);
            g.setColour(tk.painelAlt);
            g.fillRoundedRectangle(barRect.toFloat(), 2.5f);

            if (d.metricasEspacoDisponiveis && d.espacoTotalBytes > 0) {
                float fillW = static_cast<float>(barRect.getWidth()) * static_cast<float>(d.pctUsado / 100.0);
                fillW = juce::jlimit(0.0f, static_cast<float>(barRect.getWidth()), fillW);
                juce::Rectangle<float> fillRect(static_cast<float>(barRect.getX()), static_cast<float>(barRect.getY()), fillW, static_cast<float>(barRect.getHeight()));

                juce::Colour barColor = (tk.fundo.getBrightness() > 0.5f) ? juce::Colour(0xffb87a1a) : tk.acento;
                if (d.pctUsado >= 92.0) barColor = tk.perigo;
                else if (d.pctUsado >= 80.0) barColor = tk.alerta;

                g.setColour(barColor);
                g.fillRoundedRectangle(fillRect, 2.5f);
            }

            content.removeFromTop(5);
            auto summaryRow = content.removeFromTop(15);
            g.setFont(juce::Font(juce::FontOptions(11.5f)));
            g.setColour(tk.textoSecundario);

            juce::String summaryText;
            if (d.metricasEspacoDisponiveis && d.espacoTotalBytes > 0) {
                summaryText = juce::String(i18n::t("storage.used_free"))
                    .replace("{u}", formatBytes(d.espacoUsadoBytes))
                    .replace("{p}", juce::String(d.pctUsado, 1))
                    .replace("{f}", formatBytes(d.espacoLivreBytes));
                if (isSourceColumn_) {
                    if (d.totalArquivos > 0) {
                        summaryText << " \u00B7 " << juce::String(i18n::t("storage.files_count")).replace("{n}", juce::String(d.totalArquivos));
                    }
                    if (!d.ultimoIngest.empty()) {
                        summaryText << ", " << i18n::t("storage.last") << " " << formatCompactDate(juce::String(d.ultimoIngest));
                    }
                } else {
                    if (d.totalBackups > 0) {
                        summaryText << " \u00B7 " << juce::String(i18n::t(d.totalBackups == 1 ? "storage.backup_count" : "storage.backups_count")).replace("{n}", juce::String(d.totalBackups));
                    }
                    if (!d.ultimoBackup.empty()) {
                        summaryText << ", " << i18n::t("storage.last") << " " << formatCompactDate(juce::String(d.ultimoBackup));
                    }
                }
            } else {
                summaryText = d.online ? (juce::String(i18n::t("storage.capacity")) + formatBytes(d.capacidadeBytes) + " \u00B7 " + i18n::t("storage.scanning_telemetry"))
                                       : (formatBytes(d.capacidadeBytes > 0 ? d.capacidadeBytes : 0) + " \u00B7 Offline \u00B7 " + i18n::t("storage.telemetry_unavailable"));
            }
            g.drawText(summaryText, summaryRow, juce::Justification::centredLeft, true);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const auto& devs = getDevices();
        if (devs.empty()) return;

        for (size_t i = 0; i < devs.size(); ++i) {
            if (getCardBounds(i).contains(e.getPosition())) {
                owner_.selecionarDevice(devs[i].id, isSourceColumn_);
                if (e.getNumberOfClicks() >= 2) {
                    owner_.atualizarSaudeSmartDoDevice(devs[i].id, true);
                }
                return;
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const auto& devs = getDevices();
        if (devs.empty()) {
            hoveredIndex_ = -1;
            return;
        }

        int newHover = -1;
        for (size_t i = 0; i < devs.size(); ++i) {
            if (getCardBounds(i).contains(e.getPosition())) {
                newHover = static_cast<int>(i);
                break;
            }
        }

        if (hoveredIndex_ != newHover) {
            hoveredIndex_ = newHover;
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
        int availW = getWidth();
        int cardsPerRow = juce::jmax(1, (availW + kGap) / (kCardW + kGap));
        int numRows = (static_cast<int>(devs.size()) + cardsPerRow - 1) / cardsPerRow;
        int totalH = numRows * (kCardH + kGap) + 16;
        setSize(getWidth(), juce::jmax(getHeight(), totalH));
        repaint();
    }

private:
    StorageWorkspaceComponent& owner_;
    bool isSourceColumn_ = false;
    int hoveredIndex_ = -1;
};

// =============================================================================
// StorageWorkspaceComponent Implementation
// =============================================================================
StorageWorkspaceComponent::StorageWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    isCatalog_ = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    const auto& tk = tema();

    lblTitle_ = std::make_unique<juce::Label>("lblTitle", i18n::t("storage.titulo"));
    lblTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitle_);

    lblSubtitle_ = std::make_unique<juce::Label>(
        "lblSubtitle",
        i18n::t("storage.subtitulo"));
    lblSubtitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblSubtitle_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblSubtitle_);

    btnRefresh_ = std::make_unique<juce::TextButton>(i18n::t("storage.btn_refresh"));
    btnRefresh_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnRefresh_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnRefresh_->onClick = [this] { recarregar(); };
    addAndMakeVisible(*btnRefresh_);

    // Top 80%: Source & Backup Cards Columns
    lblSourceColumnTitle_ = std::make_unique<juce::Label>("lblSrcCol", i18n::t("storage.col_source"));
    lblSourceColumnTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    lblSourceColumnTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblSourceColumnTitle_);

    sourceCardsContainer_ = std::make_unique<ColumnCardsContainer>(*this, true);
    sourceCardsViewport_ = std::make_unique<juce::Viewport>();
    sourceCardsViewport_->setViewedComponent(sourceCardsContainer_.get(), false);
    sourceCardsViewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*sourceCardsViewport_);

    lblBackupColumnTitle_ = std::make_unique<juce::Label>("lblBkpCol", i18n::t("storage.col_backup"));
    lblBackupColumnTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    lblBackupColumnTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblBackupColumnTitle_);

    backupCardsContainer_ = std::make_unique<ColumnCardsContainer>(*this, false);
    backupCardsViewport_ = std::make_unique<juce::Viewport>();
    backupCardsViewport_->setViewedComponent(backupCardsContainer_.get(), false);
    backupCardsViewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*backupCardsViewport_);

    // Bottom: Interactive Log Calendar & Sessions Dock
    logDockContainer_ = std::make_unique<juce::Component>();
    addAndMakeVisible(*logDockContainer_);

    logCalendarComp_ = std::make_unique<LogCalendarComponent>(*this);
    logDockContainer_->addAndMakeVisible(*logCalendarComp_);

    lblDayLogsTitle_ = std::make_unique<juce::Label>("lblDayLogs", "");
    lblDayLogsTitle_->setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
    lblDayLogsTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    logDockContainer_->addAndMakeVisible(*lblDayLogsTitle_);

    btnShowAllLogs_ = std::make_unique<juce::TextButton>(i18n::t("storage.btn_all_sessions"));
    btnShowAllLogs_->setColour(juce::TextButton::buttonColourId, tk.painelAlt.withAlpha(0.6f));
    btnShowAllLogs_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnShowAllLogs_->onClick = [this] {
        selectedDate_ = "";
        atualizarListaLogsFiltrada();
        if (logCalendarComp_) logCalendarComp_->repaint();
    };
    logDockContainer_->addAndMakeVisible(*btnShowAllLogs_);

    btnOpenLogFolder_ = std::make_unique<juce::TextButton>(i18n::t("storage.btn_open_folder"));
    btnOpenLogFolder_->setColour(juce::TextButton::buttonColourId, tk.painelAlt.withAlpha(0.6f));
    btnOpenLogFolder_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnOpenLogFolder_->onClick = [this] { abrirPastaLogs(); };
    logDockContainer_->addAndMakeVisible(*btnOpenLogFolder_);

    tableHistory_ = std::make_unique<juce::TableListBox>("StorageHistoryTable", this);
    tableHistory_->setColour(juce::ListBox::backgroundColourId, tk.painel);
    tableHistory_->setColour(juce::ListBox::outlineColourId, tk.borda.withAlpha(0.6f));
    tableHistory_->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, tk.painel);
    tableHistory_->getHeader().setColour(juce::TableHeaderComponent::textColourId, tk.textoTerciario);
    tableHistory_->getHeader().setColour(juce::TableHeaderComponent::outlineColourId, tk.borda.withAlpha(0.4f));
    tableHistory_->setHeaderHeight(26);
    tableHistory_->setRowHeight(30);
    logDockContainer_->addAndMakeVisible(*tableHistory_);

    auto& hdr = tableHistory_->getHeader();
    hdr.removeAllColumns();
    hdr.addColumn(i18n::t("storage.col_date"), kColDate, 115, 90, 150);
    hdr.addColumn(i18n::t("storage.col_action"), kColAction, 105, 80, 130);
    hdr.addColumn(i18n::t("storage.col_operator"), kColHost, 120, 90, 160);
    hdr.addColumn(i18n::t("storage.col_volume"), kColVolume, 140, 100, 220);
    hdr.addColumn(i18n::t("storage.col_health"), kColHealth, 75, 60, 100);
    hdr.addColumn(i18n::t("storage.col_report"), kColReport, 65, 50, 90);

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

    try {
        matriz::vault::sincronizarDrivesDoProjeto(projeto_.projeto().registro(), projeto_.projeto().projetoId());
    } catch (...) {}

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

            dev.online = (dev.status == "online");
            if (!dev.localizacao.isEmpty()) {
                juce::File loc(dev.localizacao);
                if (loc.isDirectory()) {
                    dev.online = true;
                    juce::int64 volTotal = loc.getVolumeTotalSize();
                    juce::int64 volFree = loc.getBytesFreeOnVolume();
                    if (volTotal <= 0 && dev.capacidadeBytes > 0) {
                        volTotal = dev.capacidadeBytes;
                    }
                    if (volTotal > 0 && volFree >= 0) {
                        dev.espacoTotalBytes = volTotal;
                        dev.espacoLivreBytes = volFree;
                        dev.espacoUsadoBytes = juce::jmax<juce::int64>(0, volTotal - volFree);
                        dev.pctUsado = juce::jlimit(0.0, 100.0, (static_cast<double>(dev.espacoUsadoBytes) / static_cast<double>(volTotal)) * 100.0);
                        dev.pctLivre = juce::jlimit(0.0, 100.0, 100.0 - dev.pctUsado);
                        dev.metricasEspacoDisponiveis = true;
                    } else if (dev.capacidadeBytes > 0) {
                        dev.espacoTotalBytes = dev.capacidadeBytes;
                    }
                }
            }
            if (!dev.metricasEspacoDisponiveis && dev.capacidadeBytes > 0) {
                dev.espacoTotalBytes = dev.capacidadeBytes;
            }

            // Retrieve SMART report for this device
            try {
                dev.smartReport = matriz::vault::obterUltimoLogOuConsultar(db, dev.id, dev.numeroSerie, juce::File(dev.localizacao));
            } catch (...) {}

            allDevs.push_back(std::move(dev));
        }
    } catch (...) {}

    for (auto& dev : allDevs) {
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

    // Injetar card virtual do Google Drive se a pasta local estiver montada
    {
        auto detectGDrive = [] () -> juce::File {
            juce::File base = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                  .getChildFile("Library/CloudStorage");
            if (base.isDirectory()) {
                for (auto& c : base.findChildFiles(juce::File::findDirectories, false, "GoogleDrive-*")) {
                    auto myDrive = c.getChildFile("My Drive");
                    if (myDrive.isDirectory()) return myDrive;
                    if (c.isDirectory()) return c;
                }
            }
            juce::File legacy = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                    .getChildFile("Google Drive");
            return legacy.isDirectory() ? legacy : juce::File();
        };
        juce::File gdPath = detectGDrive();
        if (gdPath.isDirectory()) {
            StorageDevice gd;
            gd.id = "__google_drive__";
            gd.nome = "Google Drive";
            gd.tipo = "cloud";
            gd.localizacao = gdPath.getFullPathName();
            gd.online = true;
            gd.isBackup = true;
            juce::int64 volTotal = gdPath.getVolumeTotalSize();
            juce::int64 volFree  = gdPath.getBytesFreeOnVolume();
            if (volTotal > 0 && volFree >= 0) {
                gd.espacoTotalBytes = volTotal;
                gd.espacoLivreBytes = volFree;
                gd.espacoUsadoBytes = juce::jmax<juce::int64>(0, volTotal - volFree);
                gd.pctUsado = juce::jlimit(0.0, 100.0, (static_cast<double>(gd.espacoUsadoBytes) / static_cast<double>(volTotal)) * 100.0);
                gd.pctLivre = 100.0 - gd.pctUsado;
                gd.metricasEspacoDisponiveis = true;
            }
            backupDevices_.push_back(std::move(gd));
        }
    }

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
    } else {
        allDeviceUsageLogs_.clear();
        datesWithLogs_.clear();
        atualizarListaLogsFiltrada();
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

    auto& db = projeto_.projeto().registro();
    if (selectedDev) {
        allDeviceUsageLogs_ = matriz::vault::listarHistoricoUsoDoDispositivo(db, selectedDev->id);
        if (allDeviceUsageLogs_.empty() && selectedDev->online) {
            matriz::vault::registrarUsoDoDispositivo(db, projeto_.projeto().pasta(), selectedDev->id, "ONLINE SCAN", 0, 0, {}, "Device connected and verified");
            allDeviceUsageLogs_ = matriz::vault::listarHistoricoUsoDoDispositivo(db, selectedDev->id);
        }
    } else {
        allDeviceUsageLogs_.clear();
    }

    // Populate dates with logs map (YYYY-MM-DD -> count)
    datesWithLogs_.clear();
    for (const auto& log : allDeviceUsageLogs_) {
        if (log.criadoEm.length() >= 10) {
            juce::String dateStr = juce::String(log.criadoEm).substring(0, 10);
            datesWithLogs_[dateStr]++;
        }
    }

    atualizarListaLogsFiltrada();

    if (logCalendarComp_) {
        logCalendarComp_->repaint();
    }

    if (sourceCardsContainer_) sourceCardsContainer_->repaint();
    if (backupCardsContainer_) backupCardsContainer_->repaint();
}

void StorageWorkspaceComponent::atualizarSaudeSmartDoDevice(const std::string& vaultId, bool forcarNovaConsulta) {
    if (vaultId.empty()) return;

    StorageDevice* targetDev = nullptr;
    for (auto& d : sourceDevices_) {
        if (d.id == vaultId) { targetDev = &d; break; }
    }
    if (!targetDev) {
        for (auto& d : backupDevices_) {
            if (d.id == vaultId) { targetDev = &d; break; }
        }
    }

    if (!targetDev) return;

    auto& db = projeto_.projeto().registro();
    matriz::vault::SmartHealthReport rep;

    if (forcarNovaConsulta) {
        rep = matriz::vault::consultarSaudeSmart(targetDev->numeroSerie, juce::File(targetDev->localizacao));
        matriz::vault::gravarLogSmart(db, targetDev->id, rep);
        matriz::vault::registrarUsoDoDispositivo(db, projeto_.projeto().pasta(), targetDev->id, "SMART CHECK", 0, 0, {}, "Drive health telemetry refreshed", &rep);
    } else {
        rep = matriz::vault::obterUltimoLogOuConsultar(db, targetDev->id, targetDev->numeroSerie, juce::File(targetDev->localizacao));
    }

    targetDev->smartReport = rep;

    if (targetDev->id == selectedVaultId_) {
        selecionarDevice(targetDev->id, selectedIsSource_);
    } else {
        if (sourceCardsContainer_) sourceCardsContainer_->repaint();
        if (backupCardsContainer_) backupCardsContainer_->repaint();
    }
}

void StorageWorkspaceComponent::selecionarDataCalendario(const juce::String& yyyyMmDd) {
    selectedDate_ = yyyyMmDd;
    atualizarListaLogsFiltrada();
    if (logCalendarComp_) logCalendarComp_->repaint();
}

void StorageWorkspaceComponent::atualizarListaLogsFiltrada() {
    displayedUsageLogs_.clear();

    for (const auto& log : allDeviceUsageLogs_) {
        if (selectedDate_.isEmpty() || juce::String(log.criadoEm).startsWith(selectedDate_)) {
            displayedUsageLogs_.push_back(log);
        }
    }

    if (selectedDate_.isNotEmpty()) {
        juce::String title = juce::String(i18n::t("storage.logs_for_date"))
            .replace("{d}", selectedDate_)
            .replace("{n}", juce::String(displayedUsageLogs_.size()));
        lblDayLogsTitle_->setText(title, juce::dontSendNotification);
    } else {
        juce::String title = juce::String(i18n::t("storage.all_sessions"))
            .replace("{n}", juce::String(displayedUsageLogs_.size()));
        lblDayLogsTitle_->setText(title, juce::dontSendNotification);
    }

    if (tableHistory_) {
        tableHistory_->updateContent();
        tableHistory_->repaint();
    }
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

void StorageWorkspaceComponent::abrirRelatorioTxt(const std::string& caminho) {
    if (caminho.empty()) return;
    juce::File f(caminho);
    if (f.existsAsFile()) {
        f.startAsProcess();
    } else {
        abrirPastaLogs();
    }
}

// TableListBoxModel implementation
int StorageWorkspaceComponent::getNumRows() {
    return static_cast<int>(displayedUsageLogs_.size());
}

void StorageWorkspaceComponent::paintRowBackground(juce::Graphics& g, int, int width, int height, bool rowIsSelected) {
    const auto& tk = tema();
    if (rowIsSelected) {
        g.fillAll(tk.acento.withAlpha(0.18f));
    } else {
        g.fillAll(tk.painel);
    }
    g.setColour(tk.borda.withAlpha(0.4f));
    g.fillRect(0, height - 1, width, 1);
}

void StorageWorkspaceComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    const auto& tk = tema();
    if (rowNumber < 0 || rowNumber >= static_cast<int>(displayedUsageLogs_.size())) return;
    const auto& item = displayedUsageLogs_[static_cast<size_t>(rowNumber)];

    juce::Rectangle<int> cellBounds(6, 0, width - 12, height);

    if (columnId == kColDate) {
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.setColour(tk.textoPrimario);
        g.drawText(formatLogDateTime(item.criadoEm), cellBounds, juce::Justification::centredLeft, true);
    } else if (columnId == kColAction) {
        bool isPt = (matriz::i18n::localeAtivo().startsWith("pt"));
        juce::String actionText = item.acao;
        if (actionText.equalsIgnoreCase("SMART CHECK")) actionText = isPt ? juce::String::fromUTF8("Verificação SMART") : "Smart check";
        else if (actionText.equalsIgnoreCase("INGEST")) actionText = isPt ? juce::String::fromUTF8("Ingestão") : "Ingest";
        else if (actionText.equalsIgnoreCase("BACKUP")) actionText = "Backup";
        else if (actionText.equalsIgnoreCase("ONLINE SCAN")) actionText = isPt ? juce::String::fromUTF8("Varredura online") : "Online scan";
        else if (actionText.isNotEmpty()) {
            actionText = actionText.toLowerCase();
            actionText = actionText.substring(0, 1).toUpperCase() + actionText.substring(1);
        }

        auto badgeFont = juce::Font(juce::FontOptions(11.5f));
        int textW = (int)juce::GlyphArrangement::getStringWidth(badgeFont, actionText);
        auto badgeRect = cellBounds.reduced(0, 4).withWidth(juce::jmin(cellBounds.getWidth() - 4, textW + 20));

        g.setColour(tk.painelAlt.withAlpha(0.6f));
        g.fillRoundedRectangle(badgeRect.toFloat(), badgeRect.getHeight() / 2.0f);
        g.setColour(tk.borda.withAlpha(0.6f));
        g.drawRoundedRectangle(badgeRect.toFloat(), badgeRect.getHeight() / 2.0f, 0.8f);

        g.setColour(tk.textoPrimario);
        g.setFont(badgeFont);
        g.drawText(actionText, badgeRect, juce::Justification::centred);
    } else if (columnId == kColHost) {
        juce::String hostInfo = item.operador;
        if (hostInfo.isEmpty()) hostInfo = "user";
        if (!item.computador.empty()) hostInfo << "@" << juce::String(item.computador);
        else hostInfo << "@Mac-mini";
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(tk.textoPrimario);
        g.drawText(hostInfo, cellBounds, juce::Justification::centredLeft, true);
    } else if (columnId == kColVolume) {
        bool isPt = (matriz::i18n::localeAtivo().startsWith("pt"));
        juce::String volText;
        juce::String act = juce::String(item.acao);
        if (act.equalsIgnoreCase("SMART CHECK")) {
            volText = isPt ? juce::String::fromUTF8("Verificação manual de integridade") : "Manual health check";
        } else if (item.totalArquivos > 0 || item.totalBytes > 0) {
            volText = juce::String(item.totalArquivos) + (isPt ? " itens (" : " items (") + formatBytes(item.totalBytes) + ")";
        } else if (!item.detalhes.empty()) {
            volText = juce::String::fromUTF8(item.detalhes.c_str());
        } else {
            volText = "-";
        }
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(tk.textoPrimario);
        g.drawText(volText, cellBounds, juce::Justification::centredLeft, true);
    } else if (columnId == kColHealth) {
        bool isPt = (matriz::i18n::localeAtivo().startsWith("pt"));
        juce::Colour hColor = juce::Colour(0xff22c55e);
        juce::String hLabel = isPt ? juce::String::fromUTF8("Saudável") : "Healthy";
        juce::String estadoStr = juce::String(item.saudeEstado);
        if (estadoStr.equalsIgnoreCase("WARNING")) {
            hColor = tk.alerta;
            hLabel = isPt ? juce::String::fromUTF8("Atenção") : "Warning";
        } else if (estadoStr.equalsIgnoreCase("FAILING") || estadoStr.equalsIgnoreCase("CRITICAL")) {
            hColor = tk.perigo;
            hLabel = isPt ? juce::String::fromUTF8("Crítico") : "Critical";
        } else if (estadoStr.equalsIgnoreCase("UNAVAILABLE")) {
            hColor = tk.textoTerciario;
            hLabel = isPt ? juce::String::fromUTF8("Indisponível") : "Unavailable";
        } else if (estadoStr.isNotEmpty()) {
            hLabel = estadoStr.toLowerCase();
            hLabel = hLabel.substring(0, 1).toUpperCase() + hLabel.substring(1);
        }

        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.setColour(hColor);
        g.drawText(hLabel, cellBounds, juce::Justification::centredLeft, true);
    } else if (columnId == kColReport) {
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::underlined)));
        g.setColour(tk.textoSecundario);
        g.drawText(".txt", cellBounds, juce::Justification::centredLeft, true);
    }
}

void StorageWorkspaceComponent::cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(displayedUsageLogs_.size())) return;
    const auto& item = displayedUsageLogs_[static_cast<size_t>(rowNumber)];
    abrirRelatorioTxt(item.relatorioTxtCaminho);
}

void StorageWorkspaceComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    if (lblTitle_) {
        lblTitle_->setText(i18n::t("storage.titulo"), juce::dontSendNotification);
        lblTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (lblSubtitle_) {
        lblSubtitle_->setText(i18n::t("storage.subtitulo"), juce::dontSendNotification);
        lblSubtitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        lblSubtitle_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (btnRefresh_) {
        btnRefresh_->setButtonText(i18n::t("storage.btn_refresh"));
        btnRefresh_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnRefresh_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (lblSourceColumnTitle_) {
        lblSourceColumnTitle_->setText(i18n::t("storage.col_source"), juce::dontSendNotification);
        lblSourceColumnTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
        lblSourceColumnTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (lblBackupColumnTitle_) {
        lblBackupColumnTitle_->setText(i18n::t("storage.col_backup"), juce::dontSendNotification);
        lblBackupColumnTitle_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
        lblBackupColumnTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (lblDayLogsTitle_) {
        lblDayLogsTitle_->setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
        lblDayLogsTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (btnShowAllLogs_) {
        btnShowAllLogs_->setButtonText(i18n::t("storage.btn_all_sessions"));
        btnShowAllLogs_->setColour(juce::TextButton::buttonColourId, tk.painelAlt.withAlpha(0.6f));
        btnShowAllLogs_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (btnOpenLogFolder_) {
        btnOpenLogFolder_->setButtonText(i18n::t("storage.btn_open_folder"));
        btnOpenLogFolder_->setColour(juce::TextButton::buttonColourId, tk.painelAlt.withAlpha(0.6f));
        btnOpenLogFolder_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (tableHistory_) {
        tableHistory_->setColour(juce::ListBox::backgroundColourId, tk.painel);
        tableHistory_->setColour(juce::ListBox::outlineColourId, tk.borda.withAlpha(0.6f));
        auto& hdr = tableHistory_->getHeader();
        hdr.setColour(juce::TableHeaderComponent::backgroundColourId, tk.painel);
        hdr.setColour(juce::TableHeaderComponent::textColourId, tk.textoTerciario);
        hdr.setColour(juce::TableHeaderComponent::outlineColourId, tk.borda.withAlpha(0.4f));
        hdr.setColumnName(kColDate, i18n::t("storage.col_date"));
        hdr.setColumnName(kColAction, i18n::t("storage.col_action"));
        hdr.setColumnName(kColHost, i18n::t("storage.col_operator"));
        hdr.setColumnName(kColVolume, i18n::t("storage.col_volume"));
        hdr.setColumnName(kColHealth, i18n::t("storage.col_health"));
        hdr.setColumnName(kColReport, i18n::t("storage.col_report"));
        tableHistory_->repaint();
    }
    atualizarListaLogsFiltrada();
    if (logCalendarComp_) {
        logCalendarComp_->atualizarRotuloMes();
        logCalendarComp_->repaint();
    }
    if (sourceCardsContainer_) sourceCardsContainer_->repaint();
    if (backupCardsContainer_) backupCardsContainer_->repaint();
    repaint();
}

void StorageWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    bool isLight = (tk.fundo.getBrightness() > 0.5f);
    juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
    g.fillAll(bg);

    g.setColour(tk.borda);
    g.fillRect(0, 62, getWidth(), 1);
}

void StorageWorkspaceComponent::resized() {
    auto area = getLocalBounds().reduced(16, 10);

    // Header Area
    auto headerArea = area.removeFromTop(44);
    btnRefresh_->setBounds(headerArea.removeFromRight(150).reduced(0, 6));
    lblTitle_->setBounds(headerArea.removeFromTop(22));
    lblSubtitle_->setBounds(headerArea);

    area.removeFromTop(8);

    int totalAvailH = area.getHeight();

    // Bottom Section: Interactive Log Calendar + Sessions Table (+20% size)
    int bottomH = juce::jlimit(210, 275, static_cast<int>(totalAvailH * 0.35f));
    int topH = totalAvailH - bottomH - 8;

    // Top Section (HD Cards occupy upper area)
    auto topArea = area.removeFromTop(topH);

    int colW = (topArea.getWidth() - 14) / 2;
    auto leftColArea = topArea.removeFromLeft(colW);
    topArea.removeFromLeft(14);
    auto rightColArea = topArea;

    // Left Column (Source Drives)
    lblSourceColumnTitle_->setBounds(leftColArea.removeFromTop(22));
    leftColArea.removeFromTop(4);
    sourceCardsViewport_->setBounds(leftColArea);
    if (sourceCardsContainer_) {
        sourceCardsContainer_->setSize(leftColArea.getWidth() - 10, sourceCardsContainer_->getHeight());
        sourceCardsContainer_->atualizarAltura();
    }

    // Right Column (Backup Drives)
    lblBackupColumnTitle_->setBounds(rightColArea.removeFromTop(22));
    rightColArea.removeFromTop(4);
    backupCardsViewport_->setBounds(rightColArea);
    if (backupCardsContainer_) {
        backupCardsContainer_->setSize(rightColArea.getWidth() - 10, backupCardsContainer_->getHeight());
        backupCardsContainer_->atualizarAltura();
    }

    area.removeFromTop(8);

    // Bottom Section: Interactive Log Calendar (Left) + Day Sessions Table (Right)
    logDockContainer_->setBounds(area);

    auto dockArea = logDockContainer_->getLocalBounds();

    // Left Calendar Widget (Fixed width ~220px, +22% wider)
    int calW = 220;
    auto calArea = dockArea.removeFromLeft(calW);
    dockArea.removeFromLeft(16); // Gap

    // Right Sessions Table
    auto tableArea = dockArea;

    logCalendarComp_->setBounds(calArea);

    auto tableHdr = tableArea.removeFromTop(24);
    btnOpenLogFolder_->setBounds(tableHdr.removeFromRight(125).reduced(0, 1));
    tableHdr.removeFromRight(6);
    btnShowAllLogs_->setBounds(tableHdr.removeFromRight(95).reduced(0, 1));
    tableHdr.removeFromRight(10);
    lblDayLogsTitle_->setBounds(tableHdr);

    tableArea.removeFromTop(4);
    tableHistory_->setBounds(tableArea);
}

} // namespace matriz::ui
