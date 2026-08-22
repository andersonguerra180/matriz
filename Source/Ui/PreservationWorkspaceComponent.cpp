#include "PreservationWorkspaceComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Preservation/Preservation.h"

namespace matriz::ui {

PreservationWorkspaceComponent::PreservationWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto)
{
    labelTitulo_ = std::make_unique<juce::Label>();
    labelTitulo_->setText("Archive Preservation & Health Dashboard", juce::dontSendNotification);
    labelTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteTitulo, juce::Font::bold)));
    labelTitulo_->setColour(juce::Label::textColourId, tema().textoPrimario);
    addAndMakeVisible(*labelTitulo_);

    labelSubtitulo_ = std::make_unique<juce::Label>();
    labelSubtitulo_->setText("Monitor integrity, backup status, rights, and metadata health.", juce::dontSendNotification);
    labelSubtitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    labelSubtitulo_->setColour(juce::Label::textColourId, tema().textoSecundario);
    addAndMakeVisible(*labelSubtitulo_);

    setInterceptsMouseClicks(true, true);
    recarregar();
}

PreservationWorkspaceComponent::~PreservationWorkspaceComponent() = default;

void PreservationWorkspaceComponent::recarregar() {
    auto& db = projeto_.projeto().registro();

    // --- Métricas primárias ---

    totalAssets_ = 0;
    try {
        auto s = db.prepare("SELECT COUNT(*) FROM item");
        if (s.step()) totalAssets_ = s.columnInt(0);
    } catch (...) {}

    comPersistentId_ = 0;
    try {
        auto s = db.prepare("SELECT COUNT(*) FROM item WHERE persistent_id IS NOT NULL");
        if (s.step()) comPersistentId_ = s.columnInt(0);
    } catch (...) {}

    comSha256_ = 0;
    try {
        auto s = db.prepare("SELECT COUNT(DISTINCT item_id) FROM arquivo WHERE checksum_sha256 IS NOT NULL");
        if (s.step()) comSha256_ = s.columnInt(0);
    } catch (...) {}

    semFixity_ = totalAssets_ - comSha256_;

    fixityVerificada_ = 0;
    try {
        auto s = db.prepare(
            "SELECT COUNT(DISTINCT item_id) FROM preservation_event "
            "WHERE event_type = 'FIXITY_CHECK' AND event_outcome = 'SUCCESS'");
        if (s.step()) fixityVerificada_ = s.columnInt(0);
    } catch (...) {}

    falhaIntegridade_ = 0;
    try {
        auto s = db.prepare("SELECT COUNT(DISTINCT item_id) FROM arquivo WHERE estado_presenca = 'corrompido'");
        if (s.step()) falhaIntegridade_ = s.columnInt(0);
    } catch (...) {}

    formatoIdentificado_ = 0;
    try {
        auto s = db.prepare(
            "SELECT COUNT(DISTINCT item_id) FROM arquivo "
            "WHERE caracteristicas_tecnicas_json IS NOT NULL AND caracteristicas_tecnicas_json <> '{}'");
        if (s.step()) formatoIdentificado_ = s.columnInt(0);
    } catch (...) {}

    backupVerificado_ = 0;
    try {
        auto s = db.prepare(
            "SELECT COUNT(DISTINCT item_id) FROM preservation_event "
            "WHERE event_type = 'BACKUP_VERIFIED' AND event_outcome = 'SUCCESS'");
        if (s.step()) backupVerificado_ = s.columnInt(0);
    } catch (...) {}

    semBackup_ = totalAssets_ - backupVerificado_;

    direitosDesconhecidos_ = 0;
    try {
        auto s = db.prepare(
            "SELECT COUNT(*) FROM item "
            "WHERE NOT EXISTS ("
            "  SELECT 1 FROM preservation_right pr "
            "  WHERE pr.item_id = item.id AND pr.rights_status <> 'UNKNOWN'"
            ")");
        if (s.step()) direitosDesconhecidos_ = s.columnInt(0);
    } catch (...) {}

    totalEventos_ = 0;
    try {
        auto s = db.prepare("SELECT COUNT(*) FROM preservation_event");
        if (s.step()) totalEventos_ = s.columnInt(0);
    } catch (...) {}

    problemasGraves_ = falhaIntegridade_; // CRITICAL = arquivos corrompidos

    // --- Compliance metrics ---
    formatosEmRisco_ = matriz::preservation::contarFormatosEmRisco(db);

    auto regra321 = matriz::preservation::avaliarRegra321(db);
    regra321Status_ = regra321.status;
    regra321Label_ = (regra321.status == "OK")
        ? juce::String("3-2-1: OK")
        : juce::String(regra321.status);

    auto vaultsAlert = matriz::preservation::detectarVaultsParaRefresh(db);
    vaultsParaRefresh_ = static_cast<int>(vaultsAlert.size());

    // --- Health banner ---
    const auto& tk = tema();
    if (falhaIntegridade_ > 0) {
        textoHealth_ = "CRITICAL";
        corHealth_   = tk.perigo;
    } else if (semBackup_ > 0 || semFixity_ > totalAssets_ / 2) {
        textoHealth_ = "WARNING";
        corHealth_   = tk.alerta;
    } else {
        textoHealth_ = "GOOD";
        corHealth_   = tk.estadoQcOk;
    }

    // --- 16 cards (4x4) ---
    metricas_ = {
        { "total",               "TOTAL ASSETS",              totalAssets_,          {}, false, false },
        { "persistent_id",       "PERSISTENT IDENTIFIER",     comPersistentId_,      {}, false, false },
        { "fixity_sha256",       "SHA-256 CALCULATED",        comSha256_,            {}, false, false },
        { "fixity_verificada",   "FIXITY VERIFIED",           fixityVerificada_,     {}, false, false },
        { "falha_integridade",   "INTEGRITY FAILURES",        falhaIntegridade_,     {}, false, true  },
        { "formato_identificado","FORMAT IDENTIFIED",         formatoIdentificado_,  {}, false, false },
        { "backup_verificado",   "BACKUP VERIFIED (PREMIS)",  backupVerificado_,     {}, false, false },
        { "sem_backup",          "WITHOUT VERIFIED BACKUP",   semBackup_,            {}, false, true  },
        { "direitos_desconhecidos","RIGHTS UNKNOWN",          direitosDesconhecidos_,{}, false, true  },
        { "sem_fixity",          "WITHOUT SHA-256",           semFixity_,            {}, false, true  },
        { "eventos",             "PRESERVATION EVENTS",       totalEventos_,         {}, false, false },
        { "vulneraveis",         "NEEDS BACKUP (SINGLE COPY)",static_cast<int>(projeto_.itensDaColecaoEmbutida("vulneraveis").size()), {}, false, true },
        // Compliance row
        { "formato_risco",       "FORMAT AT RISK",            formatosEmRisco_,      {}, false, true  },
        { "regra_321",           "BACKUP RULE 3-2-1",         (regra321Status_ == "OK") ? 1 : 0, {}, false, false },
        { "vault_refresh",       "VAULT NEEDS REFRESH",       vaultsParaRefresh_,    {}, false, true  },
        { "compliance_score",    "COMPLIANCE SCORE",          0,                     {}, false, false },
    };

    repaint();
}

void PreservationWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Health Banner
    g.setColour(tk.painel);
    g.fillRoundedRectangle(boundsBanner_.toFloat(), tk.raioMedio);
    g.setColour(tk.borda);
    g.drawRoundedRectangle(boundsBanner_.toFloat(), tk.raioMedio, 1.0f);

    g.setColour(corHealth_);
    g.fillRect(boundsBanner_.getX(), boundsBanner_.getY(), 8, boundsBanner_.getHeight());

    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    g.drawText("ARCHIVE HEALTH STATUS",
               boundsBanner_.getX() + 24, boundsBanner_.getY() + 12,
               boundsBanner_.getWidth() - 40, 16,
               juce::Justification::centredLeft, true);

    g.setColour(corHealth_);
    g.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
    g.drawText(textoHealth_,
               boundsBanner_.getX() + 24, boundsBanner_.getY() + 32,
               boundsBanner_.getWidth() - 40, 36,
               juce::Justification::centredLeft, true);

    // Metric Cards Grid
    for (const auto& met : metricas_) {
        g.setColour(met.hover ? tk.painelAlt : tk.painel);
        g.fillRoundedRectangle(met.bounds.toFloat(), tk.raioMedio);

        bool alerta = met.problematico && met.contagem > 0;
        if (alerta) {
            bool critico = (met.chave == "falha_integridade");
            g.setColour(critico ? tk.perigo : tk.alerta);
            g.drawRoundedRectangle(met.bounds.toFloat(), tk.raioMedio, 2.0f);
        } else {
            g.setColour(tk.borda);
            g.drawRoundedRectangle(met.bounds.toFloat(), tk.raioMedio, 1.0f);
        }

        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(met.titulo,
                   met.bounds.reduced(12, 8).withHeight(20),
                   juce::Justification::topLeft, true);

        // Cor do número: vermelho se problemático e > 0, verde se não-problemático e > 0
        if (alerta) {
            g.setColour(met.chave == "falha_integridade" ? tk.perigo : tk.alerta);
        } else if (!met.problematico && met.contagem > 0) {
            g.setColour(tk.estadoQcOk);
        } else {
            g.setColour(tk.textoPrimario);
        }

        auto numArea = met.bounds.reduced(12, 8).withTrimmedTop(24);

        if (met.chave == "regra_321") {
            // 3-2-1 card: show text status instead of number
            bool ok = (regra321Status_ == "OK");
            g.setColour(ok ? tk.estadoQcOk : tk.alerta);
            g.setFont(juce::Font(juce::FontOptions(ok ? 28.0f : 16.0f, juce::Font::bold)));
            g.drawText(regra321Label_, numArea,
                       juce::Justification::bottomLeft, true);
        } else if (met.chave == "compliance_score") {
            // Compliance score: percentage of OK metrics
            int checks = 0, passed = 0;
            if (totalAssets_ > 0) {
                ++checks; if (comPersistentId_ == totalAssets_) ++passed;
                ++checks; if (comSha256_ == totalAssets_) ++passed;
                ++checks; if (fixityVerificada_ > 0) ++passed;
                ++checks; if (falhaIntegridade_ == 0) ++passed;
                ++checks; if (formatoIdentificado_ > 0) ++passed;
                ++checks; if (backupVerificado_ > 0) ++passed;
                ++checks; if (formatosEmRisco_ == 0) ++passed;
                ++checks; if (regra321Status_ == "OK") ++passed;
                ++checks; if (vaultsParaRefresh_ == 0) ++passed;
            }
            int pct = (checks > 0) ? (passed * 100 / checks) : 0;
            g.setColour(pct >= 80 ? tk.estadoQcOk : (pct >= 50 ? tk.alerta : tk.perigo));
            g.setFont(juce::Font(juce::FontOptions(32.0f, juce::Font::bold)));
            g.drawText(juce::String(pct) + "%", numArea,
                       juce::Justification::bottomLeft, true);
        } else {
            g.setFont(juce::Font(juce::FontOptions(32.0f, juce::Font::bold)));
            g.drawText(juce::String(met.contagem), numArea,
                       juce::Justification::bottomLeft, true);
        }
    }
}

void PreservationWorkspaceComponent::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds().reduced(tk.espacoGrande);

    labelTitulo_->setBounds(area.removeFromTop(32));
    labelSubtitulo_->setBounds(area.removeFromTop(24));
    area.removeFromTop(tk.espacoGrande);

    boundsBanner_ = area.removeFromTop(80);
    area.removeFromTop(tk.espacoGrande);

    // Grid 4 colunas × 4 linhas = 16 cards
    constexpr int columns = 4;
    constexpr int rows    = 4;
    int gap       = tk.espacoGrande;
    int cardWidth = (area.getWidth()  - (columns - 1) * gap) / columns;
    int cardHeight= (area.getHeight() - (rows    - 1) * gap) / rows;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            size_t idx = static_cast<size_t>(r * columns + c);
            if (idx < metricas_.size()) {
                metricas_[idx].bounds = juce::Rectangle<int>(
                    area.getX() + c * (cardWidth + gap),
                    area.getY() + r * (cardHeight + gap),
                    cardWidth,
                    cardHeight);
            }
        }
    }
}

void PreservationWorkspaceComponent::mouseUp(const juce::MouseEvent& e) {
    if (e.mouseWasClicked()) {
        auto pos = e.getPosition();
        for (const auto& met : metricas_) {
            if (met.bounds.contains(pos)) {
                if (aoClicarMetrica) aoClicarMetrica(met.chave);
                break;
            }
        }
    }
}

void PreservationWorkspaceComponent::mouseMove(const juce::MouseEvent& e) {
    auto pos  = e.getPosition();
    bool mudou = false;
    for (auto& met : metricas_) {
        bool hover = met.bounds.contains(pos);
        if (met.hover != hover) { met.hover = hover; mudou = true; }
    }
    if (mudou) repaint();
}

void PreservationWorkspaceComponent::mouseExit(const juce::MouseEvent&) {
    bool mudou = false;
    for (auto& met : metricas_) {
        if (met.hover) { met.hover = false; mudou = true; }
    }
    if (mudou) repaint();
}

} // namespace matriz::ui
