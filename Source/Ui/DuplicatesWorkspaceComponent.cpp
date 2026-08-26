#include "DuplicatesWorkspaceComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::ui {

// Card component for a single duplicate match group
class DuplicatesWorkspaceComponent::ListaResultadosComponent : public juce::Component {
public:
    explicit ListaResultadosComponent(DuplicatesWorkspaceComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.fundo);
    }

    void resized() override {
        auto area = getLocalBounds();
        int y = 10;
        
        for (auto* card : cards_) {
            card->setBounds(10, y, getWidth() - 20, 160);
            y += 170;
        }
    }

    void updateList(const std::vector<DuplicateGroup>& grupos) {
        cards_.clear();
        
        for (size_t i = 0; i < grupos.size(); ++i) {
            auto* card = new CardComponent(owner_, i, grupos[i]);
            cards_.add(card);
            addAndMakeVisible(card);
        }
        
        setSize(getWidth(), grupos.size() * 170 + 20);
        resized();
    }

private:
    class CardComponent : public juce::Component {
    public:
        CardComponent(DuplicatesWorkspaceComponent& owner, size_t index, const DuplicateGroup& grupo)
            : owner_(owner), index_(index), grupo_(grupo) {
            
            btnValidate_ = std::make_unique<juce::TextButton>("VALIDATE AS DUPLICATE");
            btnValidate_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22c55e)); // success green
            btnValidate_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            btnValidate_->onClick = [this] {
                owner_.resolverDuplicata(static_cast<int>(index_), true);
            };
            addAndMakeVisible(*btnValidate_);

            btnDismiss_ = std::make_unique<juce::TextButton>("NOT A DUPLICATE (DISMISS)");
            btnDismiss_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
            btnDismiss_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
            btnDismiss_->onClick = [this] {
                owner_.resolverDuplicata(static_cast<int>(index_), false);
            };
            addAndMakeVisible(*btnDismiss_);
        }

        void paint(juce::Graphics& g) override {
            const auto& tk = tema();
            
            // Background box
            g.setColour(tk.painel);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio, 1.0f);

            // Columns layout
            int w = getWidth() - 20;
            int colW = w / 2 - 20;

            auto drawColumn = [&](juce::Graphics& g, int x, const DuplicateMatch& m, const DuplicateMatch& other, bool isDup) {
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
                g.drawText(isDup ? "POSSIBLE DUPLICATE" : "EXISTING ORIGINAL", x, 12, colW, 20, juce::Justification::left);

                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
                g.setColour(m.nomeCoincide ? juce::Colour(0xff22c55e) : tk.textoPrimario);
                g.drawText("Title: " + m.titulo, x, 36, colW, 18, juce::Justification::left, true);

                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                g.setColour(tk.textoSecundario);
                g.drawText("Code: " + (m.codigoAcervo.empty() ? "N/A" : m.codigoAcervo), x, 56, colW, 16, juce::Justification::left);
                
                g.setColour(m.extCoincide ? juce::Colour(0xff22c55e) : tk.textoSecundario);
                g.drawText("Format: " + juce::String(m.ext).toUpperCase(), x, 72, colW, 16, juce::Justification::left);

                if (m.duracao > 0.0) {
                    g.setColour(m.duracaoCoincide ? juce::Colour(0xff22c55e) : tk.textoSecundario);
                    int min = static_cast<int>(m.duracao) / 60;
                    int sec = static_cast<int>(m.duracao) % 60;
                    g.drawText("Duration: " + juce::String::formatted("%02d:%02d", min, sec), x, 88, colW, 16, juce::Justification::left);
                } else {
                    g.setColour(tk.textoTerciario);
                    g.drawText("Duration: N/A", x, 88, colW, 16, juce::Justification::left);
                }

                if (m.largura > 0 && m.altura > 0) {
                    g.setColour(m.dimCoincide ? juce::Colour(0xff22c55e) : tk.textoSecundario);
                    g.drawText("Dimensions: " + juce::String(m.largura) + "x" + juce::String(m.altura), x, 104, colW, 16, juce::Justification::left);
                } else {
                    g.setColour(tk.textoTerciario);
                    g.drawText("Dimensions: N/A", x, 104, colW, 16, juce::Justification::left);
                }

                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.drawText("Path: " + m.caminhoRelativo, x, 124, colW, 14, juce::Justification::left, true);
            };

            // Draw original details
            drawColumn(g, 15, grupo_.original, grupo_.duplicata, false);

            // Draw duplicate details
            drawColumn(g, w / 2 + 5, grupo_.duplicata, grupo_.original, true);
        }

        void resized() override {
            int w = getWidth();
            btnValidate_->setBounds(w - 180, 20, 160, 32);
            btnDismiss_->setBounds(w - 180, 60, 160, 32);
        }

    private:
        DuplicatesWorkspaceComponent& owner_;
        size_t index_;
        DuplicateGroup grupo_;
        std::unique_ptr<juce::TextButton> btnValidate_;
        std::unique_ptr<juce::TextButton> btnDismiss_;
    };

    DuplicatesWorkspaceComponent& owner_;
    juce::OwnedArray<CardComponent> cards_;
};

DuplicatesWorkspaceComponent::DuplicatesWorkspaceComponent(ProjetoAberto& projeto)
    : Thread("BkrDuplicatesScan"), projeto_(projeto) {
    
    btnScan_ = std::make_unique<juce::TextButton>("SCAN FOR DUPLICATES");
    btnScan_->onClick = [this] { iniciarScan(); };
    addAndMakeVisible(*btnScan_);

    lblStatus_ = std::make_unique<juce::Label>("lblStatus", "This tool analyzes all assets in the active catalog and identifies possible duplicates based on matching file attributes.");
    lblStatus_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblStatus_);

    viewport_ = std::make_unique<juce::Viewport>();
    viewport_->setScrollBarThickness(10);
    viewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*viewport_);

    listaComponent_ = std::make_unique<ListaResultadosComponent>(*this);
    viewport_->setViewedComponent(listaComponent_.get(), false);
}

DuplicatesWorkspaceComponent::~DuplicatesWorkspaceComponent() {
    stopTimer();
    if (isThreadRunning()) {
        signalThreadShouldExit();
        waitForThreadToExit(2000);
    }
    listaComponent_.reset();
    viewport_.reset();
}

void DuplicatesWorkspaceComponent::recarregar() {
    // Reset view
    estado_ = State::Idle;
    gruposDetectados_.clear();
    lblStatus_->setText("This tool analyzes all assets in the active catalog and identifies possible duplicates based on matching file attributes.", juce::dontSendNotification);
    btnScan_->setButtonText("SCAN FOR DUPLICATES");
    btnScan_->setEnabled(true);
    viewport_->setVisible(false);
    resized();
    repaint();
}

void DuplicatesWorkspaceComponent::iniciarScan() {
    if (isThreadRunning()) return;
    
    estado_ = State::Scanning;
    progressoScan_ = 0.0;
    gruposDetectados_.clear();
    btnScan_->setEnabled(false);
    btnScan_->setButtonText("SCANNING...");
    viewport_->setVisible(false);
    
    startTimer(100); // Poll scan progress
    startThread(juce::Thread::Priority::normal);
}

void DuplicatesWorkspaceComponent::run() {
    auto& db = projeto_.projeto().registro();

    // 1. Get all candidate items for duplicate detection
    struct ItemInfo {
        std::string itemId;
        std::string codigoAcervo;
        std::string titulo;
        std::string ext;
        double duracao = 0.0;
        int largura = 0;
        int altura = 0;
        std::string caminhoRelativo;
        juce::int64 tamanhoBytes = 0;
    };
    std::vector<ItemInfo> items;

    try {
        auto stmt = db.prepare(
            "SELECT i.id, i.codigo_acervo, i.titulo, a.caminho_relativo, a.caracteristicas_tecnicas_json, a.tamanho_bytes "
            "FROM item i "
            "JOIN arquivo a ON a.item_id = i.id "
            "WHERE a.eh_master = 1 "
            "  AND (i.notas_livres IS NULL OR i.notas_livres NOT LIKE '%[USER_VERIFIED_NOT_DUPLICATE]%') "
            "  AND (i.notas_livres IS NULL OR i.notas_livres NOT LIKE '%[USER_VERIFIED_DUPLICATE]%') "
            "  AND i.estado != 'duplicata'");

        while (stmt.step()) {
            if (threadShouldExit()) return;
            ItemInfo info;
            info.itemId = stmt.columnText(0);
            info.codigoAcervo = stmt.columnText(1);
            info.titulo = stmt.columnText(2);
            std::string path = stmt.columnText(3);
            info.caminhoRelativo = path;
            info.ext = juce::File(path).getFileExtension().replaceCharacter('.', ' ').trim().toLowerCase().toStdString();
            info.tamanhoBytes = stmt.columnInt(5);

            std::string jsonStr = stmt.columnText(4);
            auto jsonVar = juce::JSON::parse(jsonStr);
            if (auto* obj = jsonVar.getDynamicObject()) {
                if (obj->hasProperty("duracaoSegundos")) {
                    info.duracao = obj->getProperty("duracaoSegundos");
                }
                if (obj->hasProperty("larguraPx")) {
                    info.largura = obj->getProperty("larguraPx");
                }
                if (obj->hasProperty("alturaPx")) {
                    info.altura = obj->getProperty("alturaPx");
                }
            }
            items.push_back(info);
        }
    } catch (...) {
        return;
    }

    if (items.empty()) {
        progressoScan_ = 1.0;
        return;
    }

    // 2. Perform cross-matching for duplicates
    for (size_t i = 0; i < items.size(); ++i) {
        if (threadShouldExit()) return;

        progressoScan_ = static_cast<double>(i) / static_cast<double>(items.size());

        const auto& item = items[i];
        
        // Find if this item has any metadata duplicates in the database that are NOT the item itself
        // and which were ingested before it (or just has a lexicographically smaller ID to avoid double-listing).
        auto matchOpt = matriz::ingest::buscarAssetPorMetadados(
            db, item.titulo, item.ext, item.duracao, item.largura, item.altura, item.tamanhoBytes, item.itemId,
            projeto_.projeto().pasta());

        if (matchOpt && matchOpt->itemId < item.itemId) {
            // Found a duplicate match group! Let's load the original's details from database.
            DuplicateGroup group;
            group.duplicata.itemId = item.itemId;
            group.duplicata.codigoAcervo = item.codigoAcervo;
            group.duplicata.titulo = item.titulo;
            group.duplicata.ext = item.ext;
            group.duplicata.duracao = item.duracao;
            group.duplicata.largura = item.largura;
            group.duplicata.altura = item.altura;
            group.duplicata.caminhoRelativo = item.caminhoRelativo;

            group.original.itemId = matchOpt->itemId;
            group.original.codigoAcervo = matchOpt->codigoAcervo;
            
            // Query original properties
            try {
                auto stmt = db.prepare(
                    "SELECT i.titulo, a.caminho_relativo, a.caracteristicas_tecnicas_json "
                    "FROM item i JOIN arquivo a ON a.item_id = i.id "
                    "WHERE i.id = ? AND a.eh_master = 1 LIMIT 1");
                stmt.bind(1, matriz::db::Value::of(matchOpt->itemId));
                if (stmt.step()) {
                    group.original.titulo = stmt.columnText(0);
                    std::string origPath = stmt.columnText(1);
                    group.original.caminhoRelativo = origPath;
                    group.original.ext = juce::File(origPath).getFileExtension().replaceCharacter('.', ' ').trim().toLowerCase().toStdString();
                    
                    std::string origJson = stmt.columnText(2);
                    auto jsonVar = juce::JSON::parse(origJson);
                    if (auto* obj = jsonVar.getDynamicObject()) {
                        if (obj->hasProperty("duracaoSegundos")) {
                            group.original.duracao = obj->getProperty("duracaoSegundos");
                        }
                        if (obj->hasProperty("larguraPx")) {
                            group.original.largura = obj->getProperty("larguraPx");
                        }
                        if (obj->hasProperty("alturaPx")) {
                            group.original.altura = obj->getProperty("alturaPx");
                        }
                    }
                }
            } catch (...) {}

            // Set matches
            group.duplicata.nomeCoincide = group.original.nomeCoincide = juce::String(group.original.titulo).equalsIgnoreCase(juce::String(group.duplicata.titulo));
            group.duplicata.extCoincide = group.original.extCoincide = (group.original.ext == group.duplicata.ext);
            group.duplicata.duracaoCoincide = group.original.duracaoCoincide = (group.original.duracao > 0 && group.duplicata.duracao > 0 && std::abs(group.original.duracao - group.duplicata.duracao) < 0.1);
            group.duplicata.dimCoincide = group.original.dimCoincide = (group.original.largura > 0 && group.duplicata.largura > 0 && group.original.largura == group.duplicata.largura && group.original.altura == group.duplicata.altura);

            gruposDetectados_.push_back(group);
        }
    }

    progressoScan_ = 1.0;
}

void DuplicatesWorkspaceComponent::timerCallback() {
    if (estado_ == State::Scanning) {
        if (!isThreadRunning()) {
            stopTimer();
            btnScan_->setEnabled(true);
            btnScan_->setButtonText("SCAN FOR DUPLICATES");
            
            if (gruposDetectados_.empty()) {
                estado_ = State::Clean;
                lblStatus_->setText("No duplicates found. Your archive is 100% clean!", juce::dontSendNotification);
            } else {
                estado_ = State::Results;
                lblStatus_->setText("Scan completed. Found " + juce::String(gruposDetectados_.size()) + " duplicate groups.", juce::dontSendNotification);
                viewport_->setVisible(true);
                listaComponent_->updateList(gruposDetectados_);
            }
            resized();
            repaint();
        } else {
            lblStatus_->setText("Scanning catalog database: " + juce::String(static_cast<int>(progressoScan_ * 100)) + "% completed...", juce::dontSendNotification);
        }
    }
}

void DuplicatesWorkspaceComponent::resolverDuplicata(int grupoIdx, bool ehDuplicataReal) {
    if (grupoIdx < 0 || grupoIdx >= static_cast<int>(gruposDetectados_.size())) return;
    
    auto group = gruposDetectados_[static_cast<size_t>(grupoIdx)];
    auto& db = projeto_.projeto().registro();
    
    try {
        if (ehDuplicataReal) {
            // Confirm/Validate as duplicate: set state to 'duplicata' and append [USER_VERIFIED_DUPLICATE]
            juce::String nota = "Validated as duplicate of " + juce::String(group.original.codigoAcervo) + " by user. [USER_VERIFIED_DUPLICATE]";
            db.run("UPDATE item SET estado = 'duplicata', notas_livres = ? WHERE id = ?",
                   {matriz::db::Value::of(nota.toStdString()),
                    matriz::db::Value::of(group.duplicata.itemId)});
        } else {
            // Dismiss/Not duplicate: keep current state (e.g. 'capturado') and write [USER_VERIFIED_NOT_DUPLICATE]
            juce::String nota = "Dismissed as duplicate by user. [USER_VERIFIED_NOT_DUPLICATE]";
            db.run("UPDATE item SET notas_livres = ? WHERE id = ?",
                   {matriz::db::Value::of(nota.toStdString()),
                    matriz::db::Value::of(group.duplicata.itemId)});
        }
    } catch (...) {}

    // Remove resolved group from local list
    gruposDetectados_.erase(gruposDetectados_.begin() + grupoIdx);
    
    if (gruposDetectados_.empty()) {
        estado_ = State::Clean;
        lblStatus_->setText("All duplicates have been resolved! Your archive is clean.", juce::dontSendNotification);
        viewport_->setVisible(false);
    } else {
        lblStatus_->setText("Found " + juce::String(gruposDetectados_.size()) + " duplicate groups.", juce::dontSendNotification);
        listaComponent_->updateList(gruposDetectados_);
    }
    
    resized();
    repaint();
}

void DuplicatesWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    if (estado_ == State::Idle) {
        g.setColour(tk.borda.withAlpha(0.3f));
        auto r = getLocalBounds().reduced(20).withHeight(getHeight() - 100);
        g.drawRoundedRectangle(r.toFloat(), tk.raioMedio, 1.5f);
    }
}

void DuplicatesWorkspaceComponent::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds().reduced(20);

    // Title label
    auto areaHeader = area.removeFromTop(40);
    
    lblStatus_->setBounds(area.removeFromTop(40));

    if (estado_ == State::Idle) {
        btnScan_->setVisible(true);
        viewport_->setVisible(false);
        btnScan_->setBounds(getLocalBounds().withSizeKeepingCentre(240, 48));
    } else if (estado_ == State::Scanning) {
        btnScan_->setVisible(true);
        viewport_->setVisible(false);
        btnScan_->setBounds(getLocalBounds().withSizeKeepingCentre(240, 48));
    } else if (estado_ == State::Results) {
        btnScan_->setVisible(false);
        viewport_->setBounds(area);
        listaComponent_->setSize(viewport_->getWidth() - viewport_->getScrollBarThickness(), listaComponent_->getHeight());
    } else if (estado_ == State::Clean) {
        btnScan_->setVisible(true);
        viewport_->setVisible(false);
        btnScan_->setBounds(getLocalBounds().withSizeKeepingCentre(240, 48));
    }
}

} // namespace matriz::ui
