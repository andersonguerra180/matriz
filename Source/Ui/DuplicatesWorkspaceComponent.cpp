#include "DuplicatesWorkspaceComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/LeituraTecnica.h"
#include "VideoPlayerComponent.h"
#include "../Vault/Resolucao.h"
#include "ModalMitigacao.h"
#include "ProgressoGlobal.h"
#include "DuplicateResolutionDialog.h"
#include "../Model/ProjectLog.h"
#include "EventBus.h"

namespace matriz::ui {

namespace {
    // Closed: top margin (10) + subcard height (130) + bottom margin (12).
    // Sub-card maior (miniatura ocupando a altura toda — item 2).
    constexpr int kAlturaSubCard = 168;
    constexpr int kAlturaFechado = kAlturaSubCard + 22;
    constexpr int kAlturaAberto = kAlturaSubCard + 360;

    // Notas nunca são sobrescritas pela resolução de duplicatas: acrescenta.
    const char* const kSqlAcrescentarNota =
        "UPDATE item SET notas_livres = CASE WHEN TRIM(COALESCE(notas_livres, '')) = '' THEN ? "
        "ELSE notas_livres || char(10) || char(10) || ? END WHERE id = ?";
    void acrescentarNota(matriz::db::Database& db, const std::string& itemId, const juce::String& nota) {
        const auto v = matriz::db::Value::of(nota.toStdString());
        db.run(kSqlAcrescentarNota, {v, v, matriz::db::Value::of(itemId)});
    }

    // Depois do COMMIT de uma ou mais sanitizações (message thread): log.md
    // do projeto em background (disco fora da message thread) e aviso pra
    // Catalog/Intake relerem os dois itens.
    void publicarSanitizacoes(ProjetoAberto& proj,
                              const std::vector<ProjetoAberto::ResultadoSanitizacao>& resultados,
                              const std::vector<std::string>& idsAlterados) {
        if (!resultados.empty()) {
            juce::StringArray linhas;
            for (const auto& r : resultados) linhas.addArray(r.linhasLog);
            juce::File pasta = proj.projeto().pasta();
            juce::Thread::launch([pasta, linhas]() {
                matriz::model::ProjectLog(pasta).appendEntry("Duplicates Resolved", linhas, "User");
            });
        }
        // Lote grande: UM aviso de recarga. Um evento por item (Validate All com
        // ~2.000 itens) fazia a grade rodar atualizarItemEmMemoria ~2.000 vezes
        // na message thread (43 s travada) e cada chamada invalidava o
        // snapshot em andamento — a grade ficava vazia.
        constexpr size_t kLimiteItemAItem = 20;
        if (idsAlterados.size() > kLimiteItemAItem) {
            EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");
            return;
        }
        for (const auto& id : idsAlterados) EventBus::obterInstancia().dispararItemAlterado(id, "metadado");
    }

    // Etapa 9 — critérios rápidos. 1 = manter o arquivo 1 (original), 2 =
    // manter o 2 (duplicata), 0 = decisão manual (não se aplica ou empate).
    struct CriteriosDoPar {
        int maisRecente = 0;
        int backupPrimeiro = 0;
    };
    CriteriosDoPar criteriosDoPar(matriz::db::Database& db, const std::string& id1, const std::string& id2) {
        auto valor = [&](const char* sql, const std::string& id) {
            try {
                auto st = db.prepare(sql);
                st.bind(1, matriz::db::Value::of(id));
                if (st.step() && !st.columnIsNull(0)) return st.columnText(0);
            } catch (...) {}
            return std::string();
        };
        CriteriosDoPar c;
        // Ingestão mais recente: quando o arquivo entrou no catálogo.
        const char* sqlIngest = "SELECT MAX(criado_em) FROM arquivo WHERE item_id = ?";
        const auto i1 = valor(sqlIngest, id1), i2 = valor(sqlIngest, id2);
        if (!i1.empty() && !i2.empty() && i1 != i2) c.maisRecente = i1 > i2 ? 1 : 2;
        // Entrou no backup primeiro: o primeiro registro em consolidacao_registro.
        const char* sqlBackup = "SELECT MIN(consolidado_em) FROM consolidacao_registro WHERE item_id = ?";
        const auto b1 = valor(sqlBackup, id1), b2 = valor(sqlBackup, id2);
        if (!b1.empty() && b2.empty()) c.backupPrimeiro = 1;
        else if (b1.empty() && !b2.empty()) c.backupPrimeiro = 2;
        else if (!b1.empty() && b1 != b2) c.backupPrimeiro = b1 < b2 ? 1 : 2;
        return c;
    }

    // Miniatura pro dialog de resolução (item 1/2) — mesma busca que CardComponent
    // usa pro preview inline: miniatura pré-gerada do projeto, senão a da coleção linkada.
    juce::Image carregarMiniaturaDoItem(ProjetoAberto& proj, const std::string& itemId, const std::string& collectionCaminho) {
        if (auto caminho = proj.caminhoMiniaturaPrincipal(itemId)) {
            juce::File f(caminho->toStdString());
            if (f.existsAsFile()) return juce::ImageFileFormat::loadFrom(f);
        } else if (!collectionCaminho.empty()) {
            juce::File colDir(collectionCaminho);
            juce::File indFile = colDir.getChildFile("indice.sqlite");
            if (indFile.existsAsFile()) {
                try {
                    matriz::db::Database indDb(indFile.getFullPathName().toStdString());
                    auto stmt = indDb.prepare("SELECT caminho_arquivo FROM miniatura WHERE item_id = ? AND eh_principal = 1 LIMIT 1");
                    stmt.bind(1, matriz::db::Value::of(itemId));
                    if (stmt.step()) {
                        juce::File thumbF = colDir.getChildFile(stmt.columnText(0));
                        if (thumbF.existsAsFile()) return juce::ImageFileFormat::loadFrom(thumbF);
                    }
                } catch (...) {}
            }
        }
        return {};
    }
}

// A unified side-by-side preview component for a single file (image, document, audio or video)
class SingleFilePreviewComponent : public juce::Component, private juce::Timer {
public:
    SingleFilePreviewComponent(ProjetoAberto& proj, const std::string& itemId, const std::string& ext, std::function<void()> onPlayCallback,
                               const std::string& fullPath = {}, const std::string& colPasta = {})
        : projeto_(proj), itemId_(itemId), ext_(ext), onPlay_(onPlayCallback) {
        
        spectrum_.assign(24, 0.0f);
        
        auto& db = projeto_.projeto().registro();
        try {
            auto stmtArq = db.prepare(
                "SELECT a.id FROM arquivo a WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
            stmtArq.bind(1, matriz::db::Value::of(itemId_));
            if (stmtArq.step()) {
                std::string arquivoId = stmtArq.columnText(0);
                auto fileOpt = matriz::vault::resolverArquivo(db, arquivoId, projeto_.projeto().pasta());
                if (fileOpt && fileOpt->existsAsFile())
                    file_ = *fileOpt;
            }
        } catch (...) {}

        if (file_ == juce::File() && !colPasta.empty()) {
            juce::File colDir(colPasta);
            juce::File resolvedColDir = matriz::model::Project::resolverPastaProjeto(colDir);
            juce::File colDbFile = resolvedColDir.getChildFile("registro.sqlite");
            if (colDbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(colDbFile.getFullPathName().toStdString());
                    auto stmtArq = colDb.prepare("SELECT a.id FROM arquivo a WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
                    stmtArq.bind(1, matriz::db::Value::of(itemId_));
                    if (stmtArq.step()) {
                        std::string arquivoId = stmtArq.columnText(0);
                        auto fileOpt = matriz::vault::resolverArquivo(colDb, arquivoId, resolvedColDir);
                        if (fileOpt && fileOpt->existsAsFile())
                            file_ = *fileOpt;
                    }
                } catch (...) {}
            }
        }

        if (file_ == juce::File() && !fullPath.empty()) {
            juce::File fp(fullPath);
            if (fp.existsAsFile()) file_ = fp;
        }

        if (file_ != juce::File()) {
            juce::String extension = juce::String(ext_).toLowerCase();
            isImage_ = (extension == "jpg" || extension == "jpeg" || extension == "png" || extension == "gif" || extension == "tiff");
            isDoc_ = (extension == "pdf" || extension == "txt" || extension == "doc" || extension == "docx" || extension == "json" || extension == "xml");
            
            if (isImage_) {
                image_ = juce::ImageFileFormat::loadFrom(file_);
            } else if (isDoc_) {
                if (extension == "txt" || extension == "json" || extension == "xml") {
                    docText_ = file_.loadFileAsString();
                } else {
                    docText_ = "Document: " + file_.getFileName() + "\n(Binary preview not supported)";
                }
            } else {
                // Audio or Video
                player_ = std::make_unique<VideoPlayerComponent>();
                if (player_->carregar(file_)) {
                    addAndMakeVisible(*player_);
                    
                    btnPlay_ = std::make_unique<juce::TextButton>("PLAY");
                    btnPlay_->onClick = [this] {
                        if (player_->estaTocando()) {
                            player_->pausar();
                            btnPlay_->setButtonText("PLAY");
                        } else {
                            if (onPlay_) onPlay_(); // Stop/pause the other player
                            player_->tocar();
                            btnPlay_->setButtonText("PAUSE");
                        }
                    };
                    addAndMakeVisible(*btnPlay_);
                    
                    lblTimecode_ = std::make_unique<juce::Label>("tc", "00:00.00");
                    lblTimecode_->setColour(juce::Label::textColourId, tema().textoSecundario);
                    addAndMakeVisible(*lblTimecode_);
                    
                    player_->aoPosicaoMudar = [this](double pos) {
                        int min = static_cast<int>(pos) / 60;
                        int sec = static_cast<int>(pos) % 60;
                        int ms = static_cast<int>((pos - static_cast<int>(pos)) * 100);
                        lblTimecode_->setText(juce::String::formatted("%02d:%02d.%02d", min, sec, ms), juce::dontSendNotification);
                    };
                    
                    startTimerHz(30); // 30Hz refresh rate for timecode & spectrum animation
                }
            }
        }
    }
    
    ~SingleFilePreviewComponent() override {
        stopTimer();
        if (player_) {
            player_->parar();
        }
        player_.reset();
    }
    
    void play() {
        if (player_ && !player_->estaTocando()) {
            player_->tocar();
            if (btnPlay_) btnPlay_->setButtonText("PAUSE");
        }
    }
    
    void pause() {
        if (player_ && player_->estaTocando()) {
            player_->pausar();
            if (btnPlay_) btnPlay_->setButtonText("PLAY");
        }
    }
    
    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioPequeno);
        
        if (file_ == juce::File()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            g.drawText(matriz::i18n::t("duplicatas.offline"), getLocalBounds(), juce::Justification::centred);
            return;
        }
        
        if (isImage_) {
            if (image_.isValid()) {
                g.drawImageWithin(image_, 10, 10, getWidth() - 20, getHeight() - 20,
                                  juce::RectanglePlacement::centred, false);
            } else {
                g.setColour(tk.textoTerciario);
                g.drawText(matriz::i18n::t("duplicatas.imagem_invalida"), getLocalBounds(), juce::Justification::centred);
            }
        } else if (isDoc_) {
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawFittedText(docText_, getLocalBounds().reduced(15), juce::Justification::topLeft, 12);
        } else {
            // Audio or Video
            if (!player_) {
                g.setColour(tk.textoTerciario);
                g.drawText("Player not available", getLocalBounds(), juce::Justification::centred);
            } else {
                juce::String extension = juce::String(ext_).toLowerCase();
                bool isAudioOnly = (extension == "wav" || extension == "mp3" || extension == "aif" || extension == "aiff" || extension == "flac" || extension == "m4a");
                if (isAudioOnly) {
                    g.setColour(tk.textoTerciario.withAlpha(0.2f));
                    int centerX = getWidth() / 2;
                    int centerY = getHeight() / 2 - 20;
                    g.drawEllipse(centerX - 40, centerY - 40, 80, 80, 2.0f);
                    
                    // Draw simulated spectrum analyzer bars in bottom half of preview area
                    juce::Rectangle<int> areaSpect = getLocalBounds().reduced(15).withHeight(120).withY(getHeight() - 170);
                    int numBars = static_cast<int>(spectrum_.size());
                    float barW = static_cast<float>(areaSpect.getWidth()) / numBars;
                    for (int i = 0; i < numBars; ++i) {
                        float val = spectrum_[static_cast<size_t>(i)];
                        float h = val * areaSpect.getHeight();
                        juce::Rectangle<float> bar(
                            static_cast<float>(areaSpect.getX()) + i * barW + 1.0f,
                            static_cast<float>(areaSpect.getBottom()) - h,
                            barW - 2.0f,
                            h
                        );
                        g.setColour(tk.acento.withAlpha(0.3f + 0.7f * val));
                        g.fillRoundedRectangle(bar, 1.5f);
                    }
                }
            }
        }
    }
    
    void resized() override {
        if (player_) {
            juce::String extension = juce::String(ext_).toLowerCase();
            bool isAudioOnly = (extension == "wav" || extension == "mp3" || extension == "aif" || extension == "aiff" || extension == "flac" || extension == "m4a");
            
            if (isAudioOnly) {
                player_->setBounds(0, 0, 0, 0);
            } else {
                player_->setBounds(10, 10, getWidth() - 20, getHeight() - 60);
            }
            
            if (btnPlay_) btnPlay_->setBounds(10, getHeight() - 42, 80, 32);
            if (lblTimecode_) lblTimecode_->setBounds(100, getHeight() - 42, 120, 32);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            menu.addItem(1, "SHOW SOURCE");
            menu.addItem(2, "COPY PATH");
            std::string itemId = itemId_;
            juce::Component::SafePointer<SingleFilePreviewComponent> safeThis(this);
            menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, itemId](int res) {
                if (!safeThis) return;
                if (res == 1) {
                    auto caminhoOpt = safeThis->projeto_.caminhoDeOrigem(itemId);
                    if (caminhoOpt && caminhoOpt->isNotEmpty()) {
                        juce::File f(*caminhoOpt);
                        if (f.existsAsFile() || f.isDirectory()) f.revealToUser();
                        else {
                            juce::AlertWindow::showAsync(
                                juce::MessageBoxOptions()
                                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                                    .withTitle("Source Not Found")
                                    .withMessage("The source file was not found at:\n" + *caminhoOpt)
                                    .withButton("OK"),
                                nullptr);
                        }
                    }
                } else if (res == 2) {
                    auto caminhoOpt = safeThis->projeto_.caminhoDeOrigem(itemId);
                    if (caminhoOpt && caminhoOpt->isNotEmpty()) {
                        juce::SystemClipboard::copyTextToClipboard(*caminhoOpt);
                    }
                }
            });
        }
    }
    
private:
    void timerCallback() override {
        if (player_ && player_->estaTocando()) {
            for (size_t i = 0; i < spectrum_.size(); ++i) {
                float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
                float bandFactor = 1.0f - (static_cast<float>(i) / spectrum_.size()) * 0.6f;
                float target = r * bandFactor;
                spectrum_[i] = spectrum_[i] * 0.6f + target * 0.4f;
            }
        } else {
            for (size_t i = 0; i < spectrum_.size(); ++i) {
                spectrum_[i] *= 0.8f;
            }
        }
        repaint();
    }

    ProjetoAberto& projeto_;
    std::string itemId_;
    std::string ext_;
    std::function<void()> onPlay_;
    juce::File file_;
    bool isImage_ = false;
    bool isDoc_ = false;
    juce::Image image_;
    juce::String docText_;
    std::unique_ptr<VideoPlayerComponent> player_;
    std::unique_ptr<juce::TextButton> btnPlay_;
    std::unique_ptr<juce::Label> lblTimecode_;
    std::vector<float> spectrum_;
};

// Card component and list view container
class DuplicatesWorkspaceComponent::ListaResultadosComponent : public juce::Component {
public:
    explicit ListaResultadosComponent(DuplicatesWorkspaceComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        bool isLight = (tk.fundo.getBrightness() > 0.5f);
        juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
        g.fillAll(bg);
    }

    void resized() override {
        auto area = getLocalBounds();
        int y = 10;
        
        for (auto* card : cards_) {
            int cardHeight = card->isExpanded() ? kAlturaAberto : kAlturaFechado;
            card->setBounds(10, y, getWidth() - 20, cardHeight);
            y += cardHeight + 10;
        }
    }

    void updateList(const std::vector<DuplicateGroup>& grupos) {
        cards_.clear();
        
        for (size_t i = 0; i < grupos.size(); ++i) {
            auto* card = new CardComponent(owner_, i, grupos[i], *this);
            cards_.add(card);
            addAndMakeVisible(card);
        }
        
        recalculateHeight();
        notifySelectionChanged();
    }

    void updateButtonsI18n() {
        for (auto* card : cards_) {
            card->updateButtonsI18n();
        }
    }

    std::vector<int> indicesSelecionados() const {
        std::vector<int> resultado;
        for (auto* card : cards_) {
            if (card->isSelected()) resultado.push_back(static_cast<int>(card->groupIndex()));
        }
        return resultado;
    }

    void notifySelectionChanged() {
        if (onSelectionChanged) onSelectionChanged();
    }

    std::function<void()> onSelectionChanged;

    void recalculateHeight() {
        int totalHeight = 20;
        for (auto* card : cards_) {
            totalHeight += (card->isExpanded() ? kAlturaAberto : kAlturaFechado) + 10;
        }
        setSize(getWidth(), totalHeight);
        resized();
    }

private:
    class CardComponent : public juce::Component {
    public:
        CardComponent(DuplicatesWorkspaceComponent& owner, size_t index, const DuplicateGroup& grupo, ListaResultadosComponent& parent)
            : owner_(owner), index_(index), grupo_(grupo), parent_(parent) {
            
            btnValidate_ = std::make_unique<juce::TextButton>(matriz::i18n::t("duplicatas.btn_validate"));
            btnValidate_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22c55e)); // success green
            btnValidate_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            btnValidate_->onClick = [this] {
                owner_.resolverDuplicata(static_cast<int>(index_), true);
            };
            addAndMakeVisible(*btnValidate_);

            btnDismiss_ = std::make_unique<juce::TextButton>(matriz::i18n::t("duplicatas.btn_dismiss"));
            btnDismiss_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
            btnDismiss_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
            btnDismiss_->onClick = [this] {
                owner_.resolverDuplicata(static_cast<int>(index_), false);
            };
            addAndMakeVisible(*btnDismiss_);

            chkSelecionar_ = std::make_unique<juce::ToggleButton>();
            chkSelecionar_->onClick = [this] { parent_.notifySelectionChanged(); };
            addAndMakeVisible(*chkSelecionar_);

            // Load thumbnails from cache/database
            auto& proj = owner_.projeto_;
            if (auto caminhoOrig = proj.caminhoMiniaturaPrincipal(grupo_.original.itemId)) {
                juce::File f(caminhoOrig->toStdString());
                if (f.existsAsFile()) {
                    thumbOriginal_ = juce::ImageFileFormat::loadFrom(f);
                }
            } else if (!grupo_.original.collectionCaminho.empty()) {
                juce::File colDir(grupo_.original.collectionCaminho);
                juce::File indFile = colDir.getChildFile("indice.sqlite");
                if (indFile.existsAsFile()) {
                    try {
                        matriz::db::Database indDb(indFile.getFullPathName().toStdString());
                        auto stmt = indDb.prepare("SELECT caminho_arquivo FROM miniatura WHERE item_id = ? AND eh_principal = 1 LIMIT 1");
                        stmt.bind(1, matriz::db::Value::of(grupo_.original.itemId));
                        if (stmt.step()) {
                            juce::File thumbF = colDir.getChildFile(stmt.columnText(0));
                            if (thumbF.existsAsFile()) thumbOriginal_ = juce::ImageFileFormat::loadFrom(thumbF);
                        }
                    } catch (...) {}
                }
            }

            if (auto caminhoDup = proj.caminhoMiniaturaPrincipal(grupo_.duplicata.itemId)) {
                juce::File f(caminhoDup->toStdString());
                if (f.existsAsFile()) {
                    thumbDuplicata_ = juce::ImageFileFormat::loadFrom(f);
                }
            } else if (!grupo_.duplicata.collectionCaminho.empty()) {
                juce::File colDir(grupo_.duplicata.collectionCaminho);
                juce::File indFile = colDir.getChildFile("indice.sqlite");
                if (indFile.existsAsFile()) {
                    try {
                        matriz::db::Database indDb(indFile.getFullPathName().toStdString());
                        auto stmt = indDb.prepare("SELECT caminho_arquivo FROM miniatura WHERE item_id = ? AND eh_principal = 1 LIMIT 1");
                        stmt.bind(1, matriz::db::Value::of(grupo_.duplicata.itemId));
                        if (stmt.step()) {
                            juce::File thumbF = colDir.getChildFile(stmt.columnText(0));
                            if (thumbF.existsAsFile()) thumbDuplicata_ = juce::ImageFileFormat::loadFrom(thumbF);
                        }
                    } catch (...) {}
                }
            }
        }

        void updateButtonsI18n() {
            if (btnValidate_) btnValidate_->setButtonText(matriz::i18n::t("duplicatas.btn_validate"));
            if (btnDismiss_) btnDismiss_->setButtonText(matriz::i18n::t("duplicatas.btn_dismiss"));
        }

        void mouseDown(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu()) {
                int w = getWidth();
                int btnW = 160;
                int rightPanelW = btnW + 20;
                int subCardsAreaW = w - rightPanelW - 20;
                int subW = std::max(120, (subCardsAreaW - 14) / 2);
                int origX = 14;
                int dupX = 14 + subW + 14;

                std::string targetItemId;
                if (e.x >= origX && e.x < origX + subW && e.y >= 10 && e.y <= 140) {
                    targetItemId = grupo_.original.itemId;
                } else if (e.x >= dupX && e.x < dupX + subW && e.y >= 10 && e.y <= 140) {
                    targetItemId = grupo_.duplicata.itemId;
                }

                if (!targetItemId.empty()) {
                    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
                    juce::PopupMenu menu;
                    menu.addItem(1, matriz::i18n::t("duplicatas.show_source"));
                    menu.addItem(2, matriz::i18n::t("duplicatas.copy_path"));
                    juce::Component::SafePointer<CardComponent> safeThis(this);
                    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, targetItemId, isPt](int res) {
                        if (!safeThis) return;
                        if (res == 1) {
                            auto caminhoOpt = safeThis->owner_.projeto_.caminhoDeOrigem(targetItemId);
                            if (caminhoOpt && caminhoOpt->isNotEmpty()) {
                                juce::File f(*caminhoOpt);
                                if (f.existsAsFile() || f.isDirectory()) {
                                    f.revealToUser();
                                } else {
                                    juce::AlertWindow::showAsync(
                                        juce::MessageBoxOptions()
                                            .withIconType(juce::MessageBoxIconType::InfoIcon)
                                            .withTitle(isPt ? juce::String::fromUTF8("Origem Não Encontrada") : "Source Not Found")
                                            .withMessage((isPt ? juce::String::fromUTF8("O arquivo de origem não foi encontrado em:\n") : "The source file was not found at:\n") + *caminhoOpt)
                                            .withButton("OK"),
                                        nullptr);
                                }
                            } else {
                                juce::AlertWindow::showAsync(
                                    juce::MessageBoxOptions()
                                        .withIconType(juce::MessageBoxIconType::InfoIcon)
                                        .withTitle(isPt ? juce::String::fromUTF8("Origem Não Encontrada") : "Source Not Found")
                                        .withMessage(isPt ? juce::String::fromUTF8("Nenhum caminho de origem registrado para este item.") : "No source path recorded for this item.")
                                        .withButton("OK"),
                                    nullptr);
                            }
                        } else if (res == 2) {
                            auto caminhoOpt = safeThis->owner_.projeto_.caminhoDeOrigem(targetItemId);
                            if (caminhoOpt && caminhoOpt->isNotEmpty()) {
                                juce::SystemClipboard::copyTextToClipboard(*caminhoOpt);
                            }
                        }
                    });
                    return;
                }
            }
        }

        void mouseDoubleClick(const juce::MouseEvent&) override {
            toggleExpanded();
        }

        void paint(juce::Graphics& g) override {
            const auto& tk = tema();
            
            // Outer container box
            g.setColour(tk.painel);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio, 1.0f);

            int w = getWidth();
            int btnW = 160;
            int rightPanelW = btnW + 20;
            int subCardsAreaW = w - rightPanelW - 20;
            int subW = std::max(120, (subCardsAreaW - 14) / 2);
            int subH = kAlturaSubCard;

            auto drawSubCard = [&](juce::Graphics& g, int x, int y, int subWidth, int subHeight,
                                   const DuplicateMatch& m, const DuplicateMatch& other, bool isDup, const juce::Image& thumb) {
                juce::Rectangle<float> cardRect(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(subWidth), static_cast<float>(subHeight));

                // 1. Sub-card background fill (HD Storage card style)
                g.setColour(tk.painelAlt.withAlpha(0.35f));
                g.fillRoundedRectangle(cardRect, 6.0f);

                // 2. Sub-card border
                g.setColour(tk.borda.withAlpha(0.70f));
                g.drawRoundedRectangle(cardRect, 6.0f, 1.0f);

                // 3. Top Banner strip (Storage card style header)
                g.saveState();
                g.reduceClipRegion(cardRect.toNearestInt().withHeight(24));
                g.setColour(isDup ? tk.alerta.withAlpha(0.18f) : tk.acento.withAlpha(0.18f));
                g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(subWidth), 24.0f, 6.0f);
                g.fillRect(static_cast<float>(x), static_cast<float>(y) + 12.0f, static_cast<float>(subWidth), 12.0f);
                g.restoreState();

                // Banner divider line
                g.setColour(tk.borda.withAlpha(0.40f));
                g.drawHorizontalLine(y + 24, static_cast<float>(x), static_cast<float>(x + subWidth));

                // Badge / Header text
                g.setColour(isDup ? tk.alerta : tk.acento);
                g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
                juce::String headerTag = isDup ? matriz::i18n::t("duplicatas.possible_duplicate") : matriz::i18n::t("duplicatas.existing_original");
                if (!m.collectionNome.empty()) {
                    headerTag << " \u00B7 " << juce::String(m.collectionNome).toUpperCase();
                }
                g.drawText(headerTag, x + 8, y + 2, subWidth - 16, 20, juce::Justification::centredLeft, true);

                // 4. Miniatura grande: ocupa toda a altura do sub-card abaixo
                // da faixa do cabeçalho, colada à esquerda; as informações
                // ficam à direita, em linhas com respiro.
                const int topoConteudo = y + 25;
                const int alturaConteudo = subHeight - 26;
                const int thumbW = juce::jmin(alturaConteudo + 24, subWidth * 2 / 5);
                juce::Rectangle<int> thumbRect(x + 1, topoConteudo, thumbW, alturaConteudo);

                g.saveState();
                {
                    juce::Path clip;
                    clip.addRoundedRectangle(static_cast<float>(thumbRect.getX()), static_cast<float>(thumbRect.getY()),
                                             static_cast<float>(thumbRect.getWidth()), static_cast<float>(thumbRect.getHeight()),
                                             5.0f, 5.0f, false, false, true, false);
                    g.reduceClipRegion(clip);
                    g.setColour(juce::Colours::black.withAlpha(0.35f));
                    g.fillRect(thumbRect);
                    if (thumb.isValid()) {
                        g.drawImageWithin(thumb, thumbRect.getX() + 4, thumbRect.getY() + 4, thumbRect.getWidth() - 8,
                                          thumbRect.getHeight() - 8, juce::RectanglePlacement::centred, false);
                    } else {
                        g.setColour(tk.textoTerciario);
                        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
                        g.drawText(juce::String(m.ext).toUpperCase(), thumbRect, juce::Justification::centred);
                    }
                }
                g.restoreState();
                g.setColour(tk.borda.withAlpha(0.50f));
                g.drawVerticalLine(thumbRect.getRight(), static_cast<float>(thumbRect.getY()), static_cast<float>(thumbRect.getBottom()));

                const int metaX = thumbRect.getRight() + 12;
                const int metaW = x + subWidth - metaX - 10;
                int linhaY = topoConteudo + 6;

                // Título
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo + 1.0f, juce::Font::bold)));
                g.setColour(m.nomeCoincide ? juce::Colour(0xffef4444) : tk.textoPrimario);
                g.drawText(m.titulo.empty() ? juce::String(m.caminhoRelativo) : juce::String(m.titulo),
                           metaX, linhaY, metaW, 18, juce::Justification::centredLeft, true);
                linhaY += 22;

                auto linhaInfo = [&](const juce::String& rotulo, const juce::String& valor, juce::Colour corValor) {
                    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                    g.setColour(tk.textoTerciario);
                    const int rotuloW = 78;
                    g.drawText(rotulo, metaX, linhaY, rotuloW, 16, juce::Justification::centredLeft, true);
                    g.setColour(corValor);
                    g.drawText(valor, metaX + rotuloW, linhaY, metaW - rotuloW, 16, juce::Justification::centredLeft, true);
                    linhaY += 17;
                };

                linhaInfo(matriz::i18n::t("duplicatas.code"), m.codigoAcervo.empty() ? juce::String("N/A") : juce::String(m.codigoAcervo),
                          tk.textoSecundario);
                linhaInfo(matriz::i18n::t("duplicatas.format"), juce::String(m.ext).toUpperCase(), tk.textoSecundario);

                if (m.duracao > 0.0) {
                    int min = static_cast<int>(m.duracao) / 60;
                    int sec = static_cast<int>(m.duracao) % 60;
                    juce::String dur = juce::String::formatted("%02d:%02d", min, sec);
                    if (m.lufs != 0.0) dur += juce::String::fromUTF8("  \u00B7  LUFS ") + juce::String::formatted("%.1f", m.lufs);
                    linhaInfo(matriz::i18n::t("duplicatas.duration"), dur, tk.textoSecundario);
                } else if (m.largura > 0 && m.altura > 0) {
                    juce::String dim = juce::String(m.largura) + " x " + juce::String(m.altura);
                    if (!m.orientation.empty() || !m.colorSpace.empty())
                        dim += juce::String::fromUTF8("  \u00B7  ") + juce::String(m.orientation) + " " + juce::String(m.colorSpace);
                    linhaInfo(matriz::i18n::t("duplicatas.dimensions"), dim.trim(), tk.textoSecundario);
                }

                const double kb = static_cast<double>(m.tamanhoBytes) / 1024.0;
                const juce::String tamanho = kb >= 1024.0 ? juce::String::formatted("%.1f MB", kb / 1024.0)
                                                          : juce::String::formatted("%.1f KB", kb);
                linhaInfo(matriz::i18n::t("duplicatas.file_size"), tamanho,
                          m.tamanhoCoincide ? juce::Colour(0xffef4444) : tk.textoSecundario);

                // Caminho no rodapé, até 2 linhas.
                const int rodapeY = y + subHeight - 36;
                g.setColour(tk.borda.withAlpha(0.35f));
                g.drawHorizontalLine(rodapeY - 3, static_cast<float>(metaX), static_cast<float>(x + subWidth - 8));
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(10.5f)));
                juce::String displayPath = m.fullPath.empty() ? m.caminhoRelativo : m.fullPath;
                g.drawFittedText(displayPath, metaX, rodapeY, metaW, 30, juce::Justification::topLeft, 2, 1.0f);
            };

            // Draw Original sub-card
            drawSubCard(g, 14, 10, subW, subH, grupo_.original, grupo_.duplicata, false, thumbOriginal_);

            // Draw Duplicate sub-card
            drawSubCard(g, 14 + subW + 14, 10, subW, subH, grupo_.duplicata, grupo_.original, true, thumbDuplicata_);
            
            // Draw horizontal dividing line if expanded
            if (isExpanded_) {
                g.setColour(tk.borda);
                g.drawHorizontalLine(10 + kAlturaSubCard + 10, 10.0f, static_cast<float>(getWidth() - 10));
            }
        }

        void resized() override {
            int w = getWidth();
            int btnW = 160;
            chkSelecionar_->setBounds(w - btnW - 14, 2, btnW, 20);
            btnValidate_->setBounds(w - btnW - 14, 25, btnW, 32);
            btnDismiss_->setBounds(w - btnW - 14, 65, btnW, 32);

            if (isExpanded_) {
                int previewW = w / 2 - 25;
                const int previewY = 10 + kAlturaSubCard + 55;
                if (previewOriginal_) previewOriginal_->setBounds(15, previewY, previewW, 280);
                if (previewDuplicata_) previewDuplicata_->setBounds(w / 2 + 10, previewY, previewW, 280);
            }
        }
        
        bool isExpanded() const { return isExpanded_; }
        bool isSelected() const { return chkSelecionar_->getToggleState(); }
        size_t groupIndex() const { return index_; }

        void toggleExpanded() {
            isExpanded_ = !isExpanded_;
            
            if (isExpanded_) {
                // Create previews
                previewOriginal_ = std::make_unique<SingleFilePreviewComponent>(
                    owner_.projeto_, grupo_.original.itemId, grupo_.original.ext,
                    [this] { if (previewDuplicata_) previewDuplicata_->pause(); },
                    grupo_.original.fullPath, grupo_.original.collectionCaminho);
                addAndMakeVisible(*previewOriginal_);
                
                previewDuplicata_ = std::make_unique<SingleFilePreviewComponent>(
                    owner_.projeto_, grupo_.duplicata.itemId, grupo_.duplicata.ext,
                    [this] { if (previewOriginal_) previewOriginal_->pause(); },
                    grupo_.duplicata.fullPath, grupo_.duplicata.collectionCaminho);
                addAndMakeVisible(*previewDuplicata_);
            } else {
                previewOriginal_.reset();
                previewDuplicata_.reset();
            }
            
            parent_.recalculateHeight();
        }

    private:
        DuplicatesWorkspaceComponent& owner_;
        size_t index_;
        DuplicateGroup grupo_;
        ListaResultadosComponent& parent_;
        bool isExpanded_ = false;
        
        std::unique_ptr<juce::TextButton> btnValidate_;
        std::unique_ptr<juce::TextButton> btnDismiss_;
        std::unique_ptr<juce::ToggleButton> chkSelecionar_;

        std::unique_ptr<SingleFilePreviewComponent> previewOriginal_;
        std::unique_ptr<SingleFilePreviewComponent> previewDuplicata_;
        juce::Image thumbOriginal_;
        juce::Image thumbDuplicata_;
    };

    DuplicatesWorkspaceComponent& owner_;
    juce::OwnedArray<CardComponent> cards_;
};

DuplicatesWorkspaceComponent::DuplicatesWorkspaceComponent(ProjetoAberto& projeto)
    : Thread("BkrDuplicatesScan"), projeto_(projeto) {
    
    btnScan_ = std::make_unique<juce::TextButton>(matriz::i18n::t("duplicatas.btn_scan"));
    btnScan_->onClick = [this] { iniciarScan(); };
    btnScan_->setTooltip(matriz::i18n::t("duplicatas.btn_scan_dica"));
    addAndMakeVisible(*btnScan_);

    lblScope_ = std::make_unique<juce::Label>("lblScope", matriz::i18n::t("duplicatas.lbl_scope"));
    lblScope_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lblScope_);

    cbScope_ = std::make_unique<juce::ComboBox>("cbScope");
    cbScope_->addItem(matriz::i18n::t("duplicatas.scope_all"), 1);
    cbScope_->addItem(matriz::i18n::t("duplicatas.scope_selected"), 2);
    cbScope_->setSelectedId(1);
    cbScope_->setTooltip("Choose search scope (all database files vs selected grid items)");
    addAndMakeVisible(*cbScope_);

    lblFileType_ = std::make_unique<juce::Label>("lblFileType", matriz::i18n::t("duplicatas.lbl_type"));
    lblFileType_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lblFileType_);

    cbFileType_ = std::make_unique<juce::ComboBox>("cbFileType");
    cbFileType_->addItem(matriz::i18n::t("duplicatas.type_all"), 1);
    cbFileType_->addItem(matriz::i18n::t("duplicatas.type_image"), 2);
    cbFileType_->addItem(matriz::i18n::t("duplicatas.type_video"), 3);
    cbFileType_->addItem(matriz::i18n::t("duplicatas.type_audio"), 4);
    cbFileType_->addItem(matriz::i18n::t("duplicatas.type_docs"), 5);
    cbFileType_->addItem(matriz::i18n::t("duplicatas.type_sessions"), 6);
    cbFileType_->addItem(matriz::i18n::t("duplicatas.type_other"), 7);
    cbFileType_->setSelectedId(1);
    cbFileType_->setTooltip("Filter candidates by media type");
    addAndMakeVisible(*cbFileType_);

    lblFileSize_ = std::make_unique<juce::Label>("lblFileSize", matriz::i18n::t("duplicatas.lbl_size"));
    lblFileSize_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lblFileSize_);

    cbSizeFilter_ = std::make_unique<juce::ComboBox>("cbSizeFilter");
    cbSizeFilter_->addItem(matriz::i18n::t("duplicatas.size_none"), 1);
    cbSizeFilter_->addItem(matriz::i18n::t("duplicatas.size_bigger"), 2);
    cbSizeFilter_->addItem(matriz::i18n::t("duplicatas.size_smaller"), 3);
    cbSizeFilter_->addItem(matriz::i18n::t("duplicatas.size_equals"), 4);
    cbSizeFilter_->setSelectedId(1);
    cbSizeFilter_->setTooltip("Filter candidates by file size rules");
    cbSizeFilter_->onChange = [this] {
        bool showValue = cbSizeFilter_->getSelectedId() > 1;
        txtSizeValue_->setVisible(showValue);
        cbSizeUnit_->setVisible(showValue);
        resized();
    };
    addAndMakeVisible(*cbSizeFilter_);

    txtSizeValue_ = std::make_unique<juce::TextEditor>("txtSizeValue");
    txtSizeValue_->setInputRestrictions(0, "0123456789.");
    txtSizeValue_->setText("100"); // default value e.g. 100
    txtSizeValue_->setVisible(false);
    txtSizeValue_->setTooltip("File size value threshold");
    addAndMakeVisible(*txtSizeValue_);

    cbSizeUnit_ = std::make_unique<juce::ComboBox>("cbSizeUnit");
    cbSizeUnit_->addItem("Bytes", 1);
    cbSizeUnit_->addItem("KB", 2);
    cbSizeUnit_->addItem("MB", 3);
    cbSizeUnit_->addItem("GB", 4);
    cbSizeUnit_->setSelectedId(3); // default MB
    cbSizeUnit_->setVisible(false);
    cbSizeUnit_->setTooltip("File size unit");
    addAndMakeVisible(*cbSizeUnit_);

    // item: filtro por ano — De/Até, mesmo padrão de campo numérico do
    // size filter. Vazio de qualquer lado = sem limite naquela ponta.
    lblAno_ = std::make_unique<juce::Label>("lblAno", "Year");
    lblAno_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lblAno_);

    txtAnoDe_ = std::make_unique<juce::TextEditor>("txtAnoDe");
    txtAnoDe_->setInputRestrictions(4, "0123456789");
    txtAnoDe_->setTextToShowWhenEmpty("From", juce::Colours::grey);
    txtAnoDe_->setTooltip("Only include items from this year onward (blank = no lower limit)");
    addAndMakeVisible(*txtAnoDe_);

    lblAnoAte_ = std::make_unique<juce::Label>("lblAnoAte", juce::CharPointer_UTF8("\xe2\x80\x93")); // "–"
    lblAnoAte_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblAnoAte_);

    txtAnoAte_ = std::make_unique<juce::TextEditor>("txtAnoAte");
    txtAnoAte_->setInputRestrictions(4, "0123456789");
    txtAnoAte_->setTextToShowWhenEmpty("To", juce::Colours::grey);
    txtAnoAte_->setTooltip("Only include items up to this year (blank = no upper limit)");
    addAndMakeVisible(*txtAnoAte_);

    lblStatus_ = std::make_unique<juce::Label>("lblStatus", "");
    lblStatus_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblStatus_);

    btnValidateAll_ = std::make_unique<juce::TextButton>(matriz::i18n::t("duplicatas.btn_validate_all"));
    btnValidateAll_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22c55e)); // success green
    btnValidateAll_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnValidateAll_->onClick = [this] { resolverTudo(true); };
    btnValidateAll_->setTooltip("Validate all detected duplicates, keeping original versions");
    addChildComponent(*btnValidateAll_);

    btnDismissAll_ = std::make_unique<juce::TextButton>(matriz::i18n::t("duplicatas.btn_dismiss_all"));
    btnDismissAll_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnDismissAll_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnDismissAll_->onClick = [this] { resolverTudo(false); };
    btnDismissAll_->setTooltip("Dismiss all duplicate alerts, keeping both files");
    addChildComponent(*btnDismissAll_);

    btnValidateSelected_ = std::make_unique<juce::TextButton>(matriz::i18n::t("duplicatas.btn_validate_selected"));
    btnValidateSelected_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22c55e));
    btnValidateSelected_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnValidateSelected_->onClick = [this] { resolverSelecionados(true); };
    btnValidateSelected_->setEnabled(false);
    addChildComponent(*btnValidateSelected_);

    btnDismissSelected_ = std::make_unique<juce::TextButton>(matriz::i18n::t("duplicatas.btn_dismiss_selected"));
    btnDismissSelected_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnDismissSelected_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnDismissSelected_->onClick = [this] { resolverSelecionados(false); };
    btnDismissSelected_->setEnabled(false);
    addChildComponent(*btnDismissSelected_);

    viewport_ = std::make_unique<juce::Viewport>();
    // Item 6 (lista nova de hoje): 10px cortava o knob redondo do
    // MatrizLookAndFeel (pensado pra a espessura default de 16px) — sem
    // espaço suficiente, ele ficava fatiado nas bordas.
    viewport_->setScrollBarThickness(16);
    viewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*viewport_);

    listaComponent_ = std::make_unique<ListaResultadosComponent>(*this);
    listaComponent_->onSelectionChanged = [this] { atualizarBotoesSelecionados(); };
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

void DuplicatesWorkspaceComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    if (lblStatus_) {
        lblStatus_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (lblScope_) {
        lblScope_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        lblScope_->setColour(juce::Label::textColourId, tk.textoPrimario);
        lblScope_->setText(matriz::i18n::t("duplicatas.lbl_scope"), juce::dontSendNotification);
    }
    if (cbScope_) {
        int sel = cbScope_->getSelectedId();
        cbScope_->clear(juce::dontSendNotification);
        cbScope_->addItem(matriz::i18n::t("duplicatas.scope_all"), 1);
        cbScope_->addItem(matriz::i18n::t("duplicatas.scope_selected"), 2);
        cbScope_->setSelectedId(sel > 0 ? sel : 1, juce::dontSendNotification);
        cbScope_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        cbScope_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        cbScope_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        cbScope_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (lblFileType_) {
        lblFileType_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        lblFileType_->setColour(juce::Label::textColourId, tk.textoPrimario);
        lblFileType_->setText(matriz::i18n::t("duplicatas.lbl_type"), juce::dontSendNotification);
    }
    if (cbFileType_) {
        int sel = cbFileType_->getSelectedId();
        cbFileType_->clear(juce::dontSendNotification);
        cbFileType_->addItem(matriz::i18n::t("duplicatas.type_all"), 1);
        cbFileType_->addItem(matriz::i18n::t("duplicatas.type_image"), 2);
        cbFileType_->addItem(matriz::i18n::t("duplicatas.type_video"), 3);
        cbFileType_->addItem(matriz::i18n::t("duplicatas.type_audio"), 4);
        cbFileType_->addItem(matriz::i18n::t("duplicatas.type_docs"), 5);
        cbFileType_->addItem(matriz::i18n::t("duplicatas.type_sessions"), 6);
        cbFileType_->addItem(matriz::i18n::t("duplicatas.type_other"), 7);
        cbFileType_->setSelectedId(sel > 0 ? sel : 1, juce::dontSendNotification);
        cbFileType_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        cbFileType_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        cbFileType_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        cbFileType_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (lblFileSize_) {
        lblFileSize_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        lblFileSize_->setColour(juce::Label::textColourId, tk.textoPrimario);
        lblFileSize_->setText(matriz::i18n::t("duplicatas.lbl_size"), juce::dontSendNotification);
    }
    if (cbSizeFilter_) {
        int sel = cbSizeFilter_->getSelectedId();
        cbSizeFilter_->clear(juce::dontSendNotification);
        cbSizeFilter_->addItem(matriz::i18n::t("duplicatas.size_none"), 1);
        cbSizeFilter_->addItem(matriz::i18n::t("duplicatas.size_bigger"), 2);
        cbSizeFilter_->addItem(matriz::i18n::t("duplicatas.size_smaller"), 3);
        cbSizeFilter_->addItem(matriz::i18n::t("duplicatas.size_equals"), 4);
        cbSizeFilter_->setSelectedId(sel > 0 ? sel : 1, juce::dontSendNotification);
        cbSizeFilter_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        cbSizeFilter_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        cbSizeFilter_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        cbSizeFilter_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (txtSizeValue_) {
        txtSizeValue_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        txtSizeValue_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        txtSizeValue_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    }
    if (cbSizeUnit_) {
        cbSizeUnit_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        cbSizeUnit_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        cbSizeUnit_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        cbSizeUnit_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (lblAno_) {
        lblAno_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        lblAno_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (lblAnoAte_) {
        lblAnoAte_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (txtAnoDe_) {
        txtAnoDe_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        txtAnoDe_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        txtAnoDe_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    }
    if (txtAnoAte_) {
        txtAnoAte_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        txtAnoAte_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        txtAnoAte_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    }
    if (btnScan_) {
        btnScan_->setColour(juce::TextButton::buttonColourId, tk.acento);
        btnScan_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
        if (estado_ != State::Scanning) {
            btnScan_->setButtonText(matriz::i18n::t("duplicatas.btn_scan"));
            btnScan_->setTooltip(matriz::i18n::t("duplicatas.btn_scan_dica"));
        }
    }
    if (btnValidateAll_) {
        btnValidateAll_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnValidateAll_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnValidateAll_->setButtonText(matriz::i18n::t("duplicatas.btn_validate_all"));
    }
    if (btnDismissAll_) {
        btnDismissAll_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnDismissAll_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnDismissAll_->setButtonText(matriz::i18n::t("duplicatas.btn_dismiss_all"));
    }
    if (btnValidateSelected_) {
        btnValidateSelected_->setButtonText(matriz::i18n::t("duplicatas.btn_validate_selected"));
    }
    if (btnDismissSelected_) {
        btnDismissSelected_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnDismissSelected_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnDismissSelected_->setButtonText(matriz::i18n::t("duplicatas.btn_dismiss_selected"));
    }
    if (listaComponent_) {
        listaComponent_->updateButtonsI18n();
        listaComponent_->repaint();
    }
    repaint();
}

void DuplicatesWorkspaceComponent::recarregar() {
    if (isThreadRunning() || estado_ == State::Results) {
        return; // Don't interrupt active scan or clear results on view reload
    }
    
    // Reset view
    estado_ = State::Idle;
    gruposDetectados_.clear();
    lblStatus_->setText("", juce::dontSendNotification);
    btnScan_->setButtonText(matriz::i18n::t("duplicatas.btn_scan"));
    btnScan_->setEnabled(true);
    viewport_->setVisible(false);
    resized();
    repaint();
}

void DuplicatesWorkspaceComponent::iniciarScan() {
    if (isThreadRunning()) return;
    
    // Capture filter selections from UI safely on main thread before starting the thread
    activeFilters_.scope = cbScope_->getSelectedId();
    activeFilters_.selecionadosNoGrid = projeto_.obterItensSelecionadosNoGrid();
    activeFilters_.fileType = cbFileType_->getSelectedId();
    activeFilters_.sizeFilter = cbSizeFilter_->getSelectedId();
    
    double userValue = txtSizeValue_->getText().getDoubleValue();
    int unitId = cbSizeUnit_->getSelectedId();
    if (unitId == 1)      activeFilters_.sizeLimitBytes = static_cast<juce::int64>(userValue);
    else if (unitId == 2) activeFilters_.sizeLimitBytes = static_cast<juce::int64>(userValue * 1024.0);
    else if (unitId == 3) activeFilters_.sizeLimitBytes = static_cast<juce::int64>(userValue * 1024.0 * 1024.0);
    else if (unitId == 4) activeFilters_.sizeLimitBytes = static_cast<juce::int64>(userValue * 1024.0 * 1024.0 * 1024.0);

    activeFilters_.anoDe = txtAnoDe_->getText().trim().getIntValue();
    activeFilters_.anoAte = txtAnoAte_->getText().trim().getIntValue();

    estado_ = State::Scanning;
    progressoScan_ = 0.0;
    gruposDetectados_.clear();
    btnScan_->setEnabled(false);
    btnScan_->setButtonText(matriz::i18n::t("duplicatas.scanning"));
    viewport_->setVisible(false);

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "duplicates_scan", "Scanning Duplicates", 0, [this] { signalThreadShouldExit(); },
        "Analyzing catalog for duplicate assets...");
    
    startTimer(100); // Poll scan progress
    resized();
    repaint();
    startThread(juce::Thread::Priority::normal);
}

void DuplicatesWorkspaceComponent::run() {
    auto& db = projeto_.projeto().registro();
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    // 1. Get all candidate items for duplicate detection
    struct ItemInfo {
        std::string itemId;
        std::string codigoAcervo;
        std::string titulo;
        std::string ext;
        double duracao = 0.0;
        int largura = 0;
        int altura = 0;
        double lufs = 0.0;
        std::string orientation;
        std::string colorSpace;
        std::string caminhoRelativo;
        std::string fullPath;
        std::string collectionNome;
        std::string collectionCaminho;
        juce::int64 tamanhoBytes = 0;
        int ano = 0;
    };
    std::vector<ItemInfo> items;

    auto carregarItensDeDb = [&](matriz::db::Database& database, const juce::File& pastaProjeto, const std::string& colNome, const std::string& colCaminho) {
        try {
            auto stmt = database.prepare(
                "SELECT i.id, i.codigo_acervo, i.titulo, a.caminho_relativo, a.caracteristicas_tecnicas_json, a.tamanho_bytes, a.caminho_absoluto_origem, i.ano "
                "FROM item i "
                "JOIN arquivo a ON a.item_id = i.id "
                "WHERE a.eh_master = 1 "
                "  AND (i.notas_livres IS NULL OR i.notas_livres NOT LIKE '%[USER_VERIFIED_NOT_DUPLICATE]%') "
                "  AND (i.notas_livres IS NULL OR i.notas_livres NOT LIKE '%[USER_VERIFIED_DUPLICATE]%') "
                "  AND (i.notas_livres IS NULL OR i.notas_livres NOT LIKE '%[USER_VERIFIED_DUPLICATE_KEEP_BOTH]%') "
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
                info.collectionNome = colNome;
                info.collectionCaminho = colCaminho;
                // item.ano é TEXT (garantirColuna em Project.cpp), não INTEGER.
                info.ano = juce::String(stmt.columnText(7)).trim().getIntValue();

                std::string absOrig = stmt.columnText(6);
                if (!absOrig.empty()) {
                    info.fullPath = absOrig;
                } else if (pastaProjeto.exists()) {
                    info.fullPath = pastaProjeto.getChildFile(path).getFullPathName().toStdString();
                }

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
                    if (obj->hasProperty("lufsIntegrado")) {
                        info.lufs = obj->getProperty("lufsIntegrado");
                    }
                    if (auto* bruto = obj->getProperty("bruto").getDynamicObject()) {
                        if (auto* exif = bruto->getProperty("exif").getDynamicObject()) {
                            if (exif->hasProperty("Exif.Image.Orientation")) {
                                info.orientation = exif->getProperty("Exif.Image.Orientation").toString().toStdString();
                            }
                            if (exif->hasProperty("Exif.Photo.ColorSpace")) {
                                info.colorSpace = exif->getProperty("Exif.Photo.ColorSpace").toString().toStdString();
                            }
                        }
                    }
                }

                // Filter 1: Scope
                if (activeFilters_.scope == 2) {
                    if (activeFilters_.selecionadosNoGrid.count(info.itemId) == 0) {
                        continue;
                    }
                }

                // Filter 2: File Type
                if (activeFilters_.fileType > 1) {
                    auto cat = matriz::ingest::categoriaPorExtensao(info.ext);
                    bool match = false;
                    switch (activeFilters_.fileType) {
                        case 2: match = (cat == matriz::ingest::CategoriaMidia::Imagem); break;
                        case 3: match = (cat == matriz::ingest::CategoriaMidia::Video); break;
                        case 4: match = (cat == matriz::ingest::CategoriaMidia::Audio); break;
                        case 5: match = (cat == matriz::ingest::CategoriaMidia::Documento || cat == matriz::ingest::CategoriaMidia::Texto); break;
                        case 6: match = (cat == matriz::ingest::CategoriaMidia::Sessao); break;
                        case 7: match = (cat != matriz::ingest::CategoriaMidia::Imagem &&
                                         cat != matriz::ingest::CategoriaMidia::Video &&
                                         cat != matriz::ingest::CategoriaMidia::Audio &&
                                         cat != matriz::ingest::CategoriaMidia::Documento &&
                                         cat != matriz::ingest::CategoriaMidia::Texto &&
                                         cat != matriz::ingest::CategoriaMidia::Sessao); break;
                    }
                    if (!match) continue;
                }

                // Filter 3: File Size
                if (activeFilters_.sizeFilter > 1) {
                    bool match = false;
                    switch (activeFilters_.sizeFilter) {
                        case 2: match = (info.tamanhoBytes > activeFilters_.sizeLimitBytes); break;
                        case 3: match = (info.tamanhoBytes < activeFilters_.sizeLimitBytes); break;
                        case 4: match = (info.tamanhoBytes == activeFilters_.sizeLimitBytes); break;
                    }
                    if (!match) continue;
                }

                // Filter 4: Year range — 0 em qualquer ponta = sem limite
                // naquela ponta. Item sem ano preenchido nunca entra numa
                // faixa restrita (mesma regra já usada em MosaicoComponent
                // pro filtro de ano do grid — "faixa é sobre o que se sabe").
                if (activeFilters_.anoDe > 0 || activeFilters_.anoAte > 0) {
                    if (info.ano <= 0) continue;
                    if (activeFilters_.anoDe > 0 && info.ano < activeFilters_.anoDe) continue;
                    if (activeFilters_.anoAte > 0 && info.ano > activeFilters_.anoAte) continue;
                }

                items.push_back(info);
            }
        } catch (...) {}
    };

    if (isCatalogMode) {
        auto colecoes = projeto_.listarColecoesLinkadas();
        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File resolvedColDir = matriz::model::Project::resolverPastaProjeto(colDir);
            juce::File dbF = resolvedColDir.getChildFile("registro.sqlite");
            if (dbF.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbF.getFullPathName().toStdString());
                    carregarItensDeDb(colDb, resolvedColDir, c.nome.toStdString(), c.caminhoProjeto.toStdString());
                } catch (...) {}
            }
        }
        carregarItensDeDb(db, projeto_.projeto().pasta(), "", "");
    } else {
        carregarItensDeDb(db, projeto_.projeto().pasta(), "", "");
    }

    if (items.empty()) {
        progressoScan_ = 1.0;
        return;
    }

    // 2. Perform cross-matching for duplicates
    if (isCatalogMode) {
        for (size_t i = 0; i < items.size(); ++i) {
            if (threadShouldExit()) return;
            progressoScan_ = static_cast<double>(i) / static_cast<double>(items.size());

            const auto& a = items[i];
            for (size_t j = i + 1; j < items.size(); ++j) {
                if (threadShouldExit()) return;
                const auto& b = items[j];

                bool nomeIgual = juce::String(a.titulo).equalsIgnoreCase(juce::String(b.titulo));
                bool extIgual = (a.ext == b.ext);
                bool duracaoIgual = (a.duracao > 0 && b.duracao > 0 && std::abs(a.duracao - b.duracao) < 0.1);
                bool dimIgual = (a.largura > 0 && b.largura > 0 && a.largura == b.largura && a.altura == b.altura);
                bool tamanhoIgual = (a.tamanhoBytes > 0 && a.tamanhoBytes == b.tamanhoBytes);
                bool lufsIgual = (a.lufs != 0.0 && b.lufs != 0.0 && std::abs(a.lufs - b.lufs) < 0.1);
                bool orientationIgual = (!a.orientation.empty() && a.orientation == b.orientation);
                bool colorSpaceIgual = (!a.colorSpace.empty() && a.colorSpace == b.colorSpace);

                bool isDupMatch = (nomeIgual && extIgual) ||
                                  (tamanhoIgual && extIgual) ||
                                  (duracaoIgual && lufsIgual && extIgual) ||
                                  (dimIgual && tamanhoIgual);

                if (isDupMatch) {
                    DuplicateGroup group;
                    group.original.itemId = a.itemId;
                    group.original.codigoAcervo = a.codigoAcervo;
                    group.original.titulo = a.titulo;
                    group.original.ext = a.ext;
                    group.original.duracao = a.duracao;
                    group.original.largura = a.largura;
                    group.original.altura = a.altura;
                    group.original.lufs = a.lufs;
                    group.original.tamanhoBytes = a.tamanhoBytes;
                    group.original.orientation = a.orientation;
                    group.original.colorSpace = a.colorSpace;
                    group.original.caminhoRelativo = a.caminhoRelativo;
                    group.original.fullPath = a.fullPath;
                    group.original.collectionNome = a.collectionNome;
                    group.original.collectionCaminho = a.collectionCaminho;

                    group.duplicata.itemId = b.itemId;
                    group.duplicata.codigoAcervo = b.codigoAcervo;
                    group.duplicata.titulo = b.titulo;
                    group.duplicata.ext = b.ext;
                    group.duplicata.duracao = b.duracao;
                    group.duplicata.largura = b.largura;
                    group.duplicata.altura = b.altura;
                    group.duplicata.lufs = b.lufs;
                    group.duplicata.tamanhoBytes = b.tamanhoBytes;
                    group.duplicata.orientation = b.orientation;
                    group.duplicata.colorSpace = b.colorSpace;
                    group.duplicata.caminhoRelativo = b.caminhoRelativo;
                    group.duplicata.fullPath = b.fullPath;
                    group.duplicata.collectionNome = b.collectionNome;
                    group.duplicata.collectionCaminho = b.collectionCaminho;

                    group.original.nomeCoincide = group.duplicata.nomeCoincide = nomeIgual;
                    group.original.extCoincide = group.duplicata.extCoincide = extIgual;
                    group.original.duracaoCoincide = group.duplicata.duracaoCoincide = duracaoIgual;
                    group.original.dimCoincide = group.duplicata.dimCoincide = dimIgual;
                    group.original.tamanhoCoincide = group.duplicata.tamanhoCoincide = tamanhoIgual;
                    group.original.orientationCoincide = group.duplicata.orientationCoincide = orientationIgual;
                    group.original.colorSpaceCoincide = group.duplicata.colorSpaceCoincide = colorSpaceIgual;
                    group.original.lufsCoincide = group.duplicata.lufsCoincide = lufsIgual;

                    gruposDetectados_.push_back(group);
                }
            }
        }
    } else {
        for (size_t i = 0; i < items.size(); ++i) {
            if (threadShouldExit()) return;
            progressoScan_ = static_cast<double>(i) / static_cast<double>(items.size());

            const auto& item = items[i];
            
            auto matchOpt = matriz::ingest::buscarAssetPorMetadados(
                db, item.titulo, item.ext, item.duracao, item.largura, item.altura, item.tamanhoBytes, item.caminhoRelativo, item.itemId,
                projeto_.projeto().pasta());

            if (matchOpt && matchOpt->itemId < item.itemId) {
                DuplicateGroup group;
                group.duplicata.itemId = item.itemId;
                group.duplicata.codigoAcervo = item.codigoAcervo;
                group.duplicata.titulo = item.titulo;
                group.duplicata.ext = item.ext;
                group.duplicata.duracao = item.duracao;
                group.duplicata.largura = item.largura;
                group.duplicata.altura = item.altura;
                group.duplicata.lufs = item.lufs;
                group.duplicata.tamanhoBytes = item.tamanhoBytes;
                group.duplicata.orientation = item.orientation;
                group.duplicata.colorSpace = item.colorSpace;
                group.duplicata.caminhoRelativo = item.caminhoRelativo;

                group.original.itemId = matchOpt->itemId;
                group.original.codigoAcervo = matchOpt->codigoAcervo;
                
                try {
                    auto stmt = db.prepare(
                        "SELECT i.titulo, a.caminho_relativo, a.caracteristicas_tecnicas_json, a.tamanho_bytes "
                        "FROM item i JOIN arquivo a ON a.item_id = i.id "
                        "WHERE i.id = ? AND a.eh_master = 1 LIMIT 1");
                    stmt.bind(1, matriz::db::Value::of(matchOpt->itemId));
                    if (stmt.step()) {
                        group.original.titulo = stmt.columnText(0);
                        std::string origPath = stmt.columnText(1);
                        group.original.caminhoRelativo = origPath;
                        group.original.ext = juce::File(origPath).getFileExtension().replaceCharacter('.', ' ').trim().toLowerCase().toStdString();
                        group.original.tamanhoBytes = stmt.columnInt(3);
                        
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
                            if (obj->hasProperty("lufsIntegrado")) {
                                group.original.lufs = obj->getProperty("lufsIntegrado");
                            }
                            if (auto* bruto = obj->getProperty("bruto").getDynamicObject()) {
                                if (auto* exif = bruto->getProperty("exif").getDynamicObject()) {
                                    if (exif->hasProperty("Exif.Image.Orientation")) {
                                        group.original.orientation = exif->getProperty("Exif.Image.Orientation").toString().toStdString();
                                    }
                                    if (exif->hasProperty("Exif.Photo.ColorSpace")) {
                                        group.original.colorSpace = exif->getProperty("Exif.Photo.ColorSpace").toString().toStdString();
                                    }
                                }
                            }
                        }
                    }
                } catch (...) {}

                group.duplicata.nomeCoincide = group.original.nomeCoincide = juce::String(group.original.titulo).equalsIgnoreCase(juce::String(group.duplicata.titulo));
                group.duplicata.extCoincide = group.original.extCoincide = (group.original.ext == group.duplicata.ext);
                group.duplicata.duracaoCoincide = group.original.duracaoCoincide = (group.original.duracao > 0 && group.duplicata.duracao > 0 && std::abs(group.original.duracao - group.duplicata.duracao) < 0.1);
                group.duplicata.dimCoincide = group.original.dimCoincide = (group.original.largura > 0 && group.duplicata.largura > 0 && group.original.largura == group.duplicata.largura && group.original.altura == group.duplicata.altura);
                group.duplicata.tamanhoCoincide = group.original.tamanhoCoincide = (group.original.tamanhoBytes == group.duplicata.tamanhoBytes);
                group.duplicata.orientationCoincide = group.original.orientationCoincide = (group.original.orientation == group.duplicata.orientation);
                group.duplicata.colorSpaceCoincide = group.original.colorSpaceCoincide = (group.original.colorSpace == group.duplicata.colorSpace);
                group.duplicata.lufsCoincide = group.original.lufsCoincide = (group.original.lufs != 0.0 && group.duplicata.lufs != 0.0 && std::abs(group.original.lufs - group.duplicata.lufs) < 0.1);

                gruposDetectados_.push_back(group);
            }
        }
    }

    progressoScan_ = 1.0;
}

void DuplicatesWorkspaceComponent::timerCallback() {
    if (estado_ == State::Scanning) {
        if (!isThreadRunning()) {
            stopTimer();
            btnScan_->setEnabled(true);
            btnScan_->setButtonText(matriz::i18n::t("duplicatas.btn_scan"));
            
            juce::String msgFinal;
            if (gruposDetectados_.empty()) {
                estado_ = State::Clean;
                msgFinal = matriz::i18n::t("duplicatas.nenhuma");
                lblStatus_->setText(msgFinal, juce::dontSendNotification);
            } else {
                estado_ = State::Results;
                msgFinal = matriz::i18n::t("duplicatas.encontradas").replace("{n}", juce::String(gruposDetectados_.size()));
                lblStatus_->setText(msgFinal, juce::dontSendNotification);
                viewport_->setVisible(true);
                listaComponent_->updateList(gruposDetectados_);
            }
            ProgressoGlobal::obterInstancia().concluirTarefa("duplicates_scan", msgFinal);
            resized();
            repaint();
        } else {
            lblStatus_->setText("Scanning catalog database: " + juce::String(static_cast<int>(progressoScan_ * 100)) + "% completed...", juce::dontSendNotification);
            ProgressoGlobal::obterInstancia().atualizarFracao(
                "duplicates_scan", progressoScan_,
                "Scanning catalog database: " + juce::String(static_cast<int>(progressoScan_ * 100)) + "%");
        }
    }
}

void DuplicatesWorkspaceComponent::resolverDuplicata(int grupoIdx, bool ehDuplicataReal) {
    if (grupoIdx < 0 || grupoIdx >= static_cast<int>(gruposDetectados_.size())) return;
    
    auto group = gruposDetectados_[static_cast<size_t>(grupoIdx)];
    
    if (!ehDuplicataReal) {
        // Dismiss/Not duplicate: keep current state and write [USER_VERIFIED_NOT_DUPLICATE]
        auto& db = projeto_.projeto().registro();
        try {
            acrescentarNota(db, group.duplicata.itemId, "Dismissed as duplicate by user. [USER_VERIFIED_NOT_DUPLICATE]");
        } catch (...) {}

        // Remove resolved group from local list
        gruposDetectados_.erase(gruposDetectados_.begin() + grupoIdx);
        
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
        if (gruposDetectados_.empty()) {
            estado_ = State::Clean;
            lblStatus_->setText(isPt ? juce::String::fromUTF8("Todas as duplicatas foram resolvidas! Seu acervo está limpo.")
                                     : "All duplicates have been resolved! Your archive is clean.", juce::dontSendNotification);
            viewport_->setVisible(false);
        } else {
            lblStatus_->setText(isPt ? (juce::String::fromUTF8("Encontrados ") + juce::String(gruposDetectados_.size()) + juce::String::fromUTF8(" grupos de duplicatas."))
                                     : ("Found " + juce::String(gruposDetectados_.size()) + " duplicate groups."), juce::dontSendNotification);
            listaComponent_->updateList(gruposDetectados_);
        }
        resized();
        repaint();
        return;
    }

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    // Validation path: Show prompt to ask which file to keep
    auto janela = std::make_shared<juce::AlertWindow>(
        isPt ? juce::String::fromUTF8("Resolver Correspondência de Duplicata") : "Resolve Duplicate Match",
        isPt ? (juce::String::fromUTF8("Como você deseja tratar este par de duplicatas?\n\nArquivo 1 (Original): ") + juce::String(group.original.titulo) + juce::String::fromUTF8("\nArquivo 2 (Duplicata): ") + juce::String(group.duplicata.titulo))
             : ("How would you like to handle this duplicate pair?\n\nFile 1 (Original): " + juce::String(group.original.titulo) + "\nFile 2 (Duplicate): " + juce::String(group.duplicata.titulo)),
        juce::MessageBoxIconType::QuestionIcon
    );
    janela->addButton(isPt ? juce::String::fromUTF8("MANTER ARQUIVO 1") : "KEEP FILE 1", 1);
    janela->addButton(isPt ? juce::String::fromUTF8("MANTER ARQUIVO 2") : "KEEP FILE 2", 2);
    janela->addButton(isPt ? juce::String::fromUTF8("MANTER AMBOS") : "KEEP BOTH", 3);
    janela->addButton(matriz::i18n::t("duplicatas.criterio_recente"), 5);
    janela->addButton(matriz::i18n::t("duplicatas.criterio_backup"), 6);
    janela->addButton(isPt ? juce::String::fromUTF8("VOLTAR") : "RETURN", 4, juce::KeyPress(juce::KeyPress::escapeKey));

    // safeThis: enterModalState() não impede o componente de ser destruído
    // enquanto a janela de confirmação está aberta (ex.: trocar de aba) —
    // sem isso, tanto o callback do ModalCallbackFunction quanto o
    // callAsync aninhado dentro dele desreferenciariam `this` já liberado.
    juce::Component::SafePointer<DuplicatesWorkspaceComponent> safeThis(this);
    janela->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, janela, grupoIdx, group, isPt](int buttonResult) {
        retirarPeerDaTela(*janela);
        if (buttonResult == 0 || buttonResult == 4) return; // User cancelled/returned or closed without selecting
        if (!safeThis) return;
        if (buttonResult == 5 || buttonResult == 6) {  // critério rápido (etapa 9)
            const auto c = criteriosDoPar(safeThis->projeto_.projeto().registro(), group.original.itemId,
                                          group.duplicata.itemId);
            const int lado = buttonResult == 5 ? c.maisRecente : c.backupPrimeiro;
            if (lado == 0) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, matriz::i18n::t("duplicatas.criterios"),
                                                       matriz::i18n::t("duplicatas.criterio_nao_aplica"), {}, nullptr,
                                                       juce::ModalCallbackFunction::create([](int) {}));
                return;
            }
            buttonResult = lado;  // 1 = manter arquivo 1, 2 = manter arquivo 2
        }

        auto& db = safeThis->projeto_.projeto().registro();
        std::vector<ProjetoAberto::ResultadoSanitizacao> resultados;
        try {
            db.run("BEGIN TRANSACTION", {});
            
            // Sanitizar: o lado não escolhido fica no SOURCE e no catálogo
            // (estado 'duplicata'), só não entra no MAIN — nada é apagado.
            if (buttonResult == 1) {
                resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.original.itemId, group.duplicata.itemId));
            }
            else if (buttonResult == 2) {
                resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.duplicata.itemId, group.original.itemId));
            }
            else if (buttonResult == 3) { // Keep Both — os dois continuam entrando no backup
                acrescentarNota(db, group.duplicata.itemId,
                                "Validated as a known duplicate pair of " + juce::String(group.original.codigoAcervo) +
                                " by user — both sides kept in Make Backup. [USER_VERIFIED_DUPLICATE_KEEP_BOTH]");
            }
            
            db.run("COMMIT", {});
            publicarSanitizacoes(safeThis->projeto_, resultados, {group.original.itemId, group.duplicata.itemId});
        } catch (...) {
            try { db.run("ROLLBACK", {}); } catch (...) {}
        }

        // Run UI update on MessageThread context
        juce::MessageManager::callAsync([safeThis, grupoIdx, isPt]() {
            if (!safeThis) return;
            if (grupoIdx >= 0 && grupoIdx < static_cast<int>(safeThis->gruposDetectados_.size())) {
                safeThis->gruposDetectados_.erase(safeThis->gruposDetectados_.begin() + grupoIdx);

                if (safeThis->gruposDetectados_.empty()) {
                    safeThis->estado_ = State::Clean;
                    safeThis->lblStatus_->setText(isPt ? juce::String::fromUTF8("Todas as duplicatas foram resolvidas! Seu acervo está limpo.")
                                             : "All duplicates have been resolved! Your archive is clean.", juce::dontSendNotification);
                    safeThis->viewport_->setVisible(false);
                } else {
                    safeThis->lblStatus_->setText(isPt ? (juce::String::fromUTF8("Encontrados ") + juce::String(safeThis->gruposDetectados_.size()) + juce::String::fromUTF8(" grupos de duplicatas."))
                                             : ("Found " + juce::String(safeThis->gruposDetectados_.size()) + " duplicate groups."), juce::dontSendNotification);
                    safeThis->listaComponent_->updateList(safeThis->gruposDetectados_);
                }
                safeThis->resized();
                safeThis->repaint();
            }
        });
    }));
}

void DuplicatesWorkspaceComponent::resolverTudo(bool ehDuplicataReal) {
    if (gruposDetectados_.empty()) return;

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (!ehDuplicataReal) {
        // Dismiss All: inalterado — não sinaliza nada como duplicata, então
        // não afeta o Make Backup de forma nenhuma.
        juce::String titulo = isPt ? juce::String::fromUTF8("Ignorar Todas as Duplicatas") : juce::String("Dismiss All Duplicates");
        juce::String msg = isPt
            ? (juce::String::fromUTF8("Tem certeza de que deseja ignorar todos os ") + juce::String(gruposDetectados_.size()) + juce::String::fromUTF8(" grupos? Eles não serão mais sinalizados como duplicatas."))
            : ("Are you sure you want to dismiss all " + juce::String(gruposDetectados_.size()) + " duplicate groups? They will not be flagged as duplicates again.");

        bool confirm = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon, titulo, msg,
            isPt ? juce::String::fromUTF8("Sim") : "Yes",
            isPt ? juce::String::fromUTF8("Não") : "No", this);
        if (!confirm) return;

        auto& db = projeto_.projeto().registro();
        try {
            db.run("BEGIN TRANSACTION", {});
            for (const auto& group : gruposDetectados_) {
                acrescentarNota(db, group.duplicata.itemId,
                                "Dismissed as duplicate by user in batch. [USER_VERIFIED_NOT_DUPLICATE]");
            }
            db.run("COMMIT", {});
        } catch (...) {
            try { db.run("ROLLBACK", {}); } catch (...) {}
        }

        gruposDetectados_.clear();
        estado_ = State::Clean;
        lblStatus_->setText("All duplicates have been resolved! Your archive is clean.", juce::dontSendNotification);
        viewport_->setVisible(false);
        resized();
        repaint();
        return;
    }

    // Validate All em lote (item): uma escolha só, aplicada a TODOS os
    // grupos detectados de uma vez — não é mais um confirm/cancel binário.
    // Não apaga nada (nem catálogo, nem disco/fonte): só decide qual lado
    // de cada par sai da PRÓXIMA leva de Make Backup (via estado =
    // 'duplicata', que planejarConsolidacao agora respeita).
    juce::String titulo = isPt ? juce::String::fromUTF8("Validar Todas as Duplicatas") : juce::String("Validate All Duplicates");
    juce::String msg = isPt
        ? (juce::String::fromUTF8("Aplicar a mesma escolha aos ") + juce::String(gruposDetectados_.size()) +
           juce::String::fromUTF8(" grupos de duplicatas encontrados. Nada é apagado do disco nem do catálogo — "
                                   "o lado descartado só sai da próxima leva de Make Backup."))
        : ("Apply the same choice to all " + juce::String(gruposDetectados_.size()) +
           " detected duplicate groups. Nothing is deleted from disk or the catalog — the discarded side just "
           "won't be included in the next Make Backup run.");

    auto* aw = new juce::AlertWindow(titulo, msg, juce::AlertWindow::QuestionIcon);
    aw->addButton(matriz::i18n::t("duplicatas.action_keep1"), 1);
    aw->addButton(matriz::i18n::t("duplicatas.action_keep2"), 2);
    aw->addButton(matriz::i18n::t("duplicatas.action_keep_both"), 3);
    aw->addButton(matriz::i18n::t("duplicatas.criterio_recente"), 4);
    aw->addButton(matriz::i18n::t("duplicatas.criterio_backup"), 5);
    aw->addButton(isPt ? juce::String::fromUTF8("Cancelar") : juce::String("Cancel"), 0);

    juce::Component::SafePointer<DuplicatesWorkspaceComponent> safeThis(this);
    aw->enterModalState(true, juce::ModalCallbackFunction::create([safeThis](int resultado) {
        if (!safeThis || resultado == 0) return;
        safeThis->aplicarEscolhaGlobal(resultado);
    }), true);
}

void DuplicatesWorkspaceComponent::aplicarEscolhaGlobal(int escolha) {
    if (gruposDetectados_.empty()) return;

    auto& db = projeto_.projeto().registro();
    std::vector<ProjetoAberto::ResultadoSanitizacao> resultados;
    std::vector<std::string> ids;
    std::vector<DuplicateGroup> paraDecisaoManual;  // critério não se aplica / empate (etapa 9)
    try {
        db.run("BEGIN TRANSACTION", {});
        for (const auto& grupo : gruposDetectados_) {
            const auto& group = grupo;
            int escolhaDoGrupo = escolha;
            if (escolha == 4 || escolha == 5) {
                const auto c = criteriosDoPar(db, group.original.itemId, group.duplicata.itemId);
                escolhaDoGrupo = escolha == 4 ? c.maisRecente : c.backupPrimeiro;
                if (escolhaDoGrupo == 0) {
                    paraDecisaoManual.push_back(group);
                    continue;
                }
            }
            if (escolhaDoGrupo == 1) { // Keep File 1 (original) — o lado "duplicata" fica só no SOURCE
                resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.original.itemId, group.duplicata.itemId));
            } else if (escolhaDoGrupo == 2) { // Keep File 2 (duplicata) — o "original" fica só no SOURCE
                resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.duplicata.itemId, group.original.itemId));
            } else { // Keep Both — só documenta o par, os dois continuam entrando no backup normalmente
                acrescentarNota(db, group.duplicata.itemId,
                                "Validated as a known duplicate pair by user in batch — both sides kept in Make Backup. "
                                "[USER_VERIFIED_DUPLICATE_KEEP_BOTH]");
            }
            ids.push_back(group.original.itemId);
            ids.push_back(group.duplicata.itemId);
        }
        db.run("COMMIT", {});
        publicarSanitizacoes(projeto_, resultados, ids);
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
    }

    gruposDetectados_ = std::move(paraDecisaoManual);
    if (!gruposDetectados_.empty()) {
        // Sinalizados no resultado: continuam na lista pra decisão manual.
        lblStatus_->setText(matriz::i18n::t("duplicatas.manuais").replace("{n}", juce::String((int) gruposDetectados_.size())),
                            juce::dontSendNotification);
        listaComponent_->updateList(gruposDetectados_);
        resized();
        repaint();
        return;
    }
    estado_ = State::Clean;
    lblStatus_->setText("All duplicates have been resolved! Your archive is clean.", juce::dontSendNotification);
    viewport_->setVisible(false);
    resized();
    repaint();
}

void DuplicatesWorkspaceComponent::atualizarBotoesSelecionados() {
    if (!listaComponent_) return;
    bool temSelecao = !listaComponent_->indicesSelecionados().empty();
    if (btnValidateSelected_) btnValidateSelected_->setEnabled(temSelecao);
    if (btnDismissSelected_) btnDismissSelected_->setEnabled(temSelecao);
}

void DuplicatesWorkspaceComponent::atualizarListaEStatusAposResolucao() {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    if (gruposDetectados_.empty()) {
        estado_ = State::Clean;
        lblStatus_->setText(isPt ? juce::String::fromUTF8("Todas as duplicatas foram resolvidas! Seu acervo está limpo.")
                                 : "All duplicates have been resolved! Your archive is clean.", juce::dontSendNotification);
        viewport_->setVisible(false);
    } else {
        lblStatus_->setText(isPt ? (juce::String::fromUTF8("Encontrados ") + juce::String(gruposDetectados_.size()) + juce::String::fromUTF8(" grupos de duplicatas."))
                                 : ("Found " + juce::String(gruposDetectados_.size()) + " duplicate groups."), juce::dontSendNotification);
        listaComponent_->updateList(gruposDetectados_);
    }
    atualizarBotoesSelecionados();
    resized();
    repaint();
}

void DuplicatesWorkspaceComponent::resolverSelecionados(bool ehDuplicataReal) {
    if (!listaComponent_) return;
    auto indices = listaComponent_->indicesSelecionados();
    if (indices.empty()) return;
    std::sort(indices.begin(), indices.end());

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (!ehDuplicataReal) {
        // Dismiss selected: same as "not a duplicate" for each selected pair, no dialog needed.
        juce::String msg = isPt
            ? (juce::String::fromUTF8("Tem certeza de que deseja ignorar os ") + juce::String(static_cast<int>(indices.size())) + juce::String::fromUTF8(" grupos selecionados? Eles não serão mais sinalizados como duplicatas."))
            : ("Are you sure you want to dismiss the " + juce::String(static_cast<int>(indices.size())) + " selected groups? They will not be flagged as duplicates again.");
        bool confirm = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon,
            isPt ? juce::String::fromUTF8("Ignorar Selecionadas") : "Dismiss Selected",
            msg,
            isPt ? juce::String::fromUTF8("Sim") : "Yes",
            isPt ? juce::String::fromUTF8("Não") : "No",
            this);
        if (!confirm) return;

        auto& db = projeto_.projeto().registro();
        try {
            db.run("BEGIN TRANSACTION", {});
            for (int idx : indices) {
                if (idx < 0 || idx >= static_cast<int>(gruposDetectados_.size())) continue;
                const auto& group = gruposDetectados_[static_cast<size_t>(idx)];
                acrescentarNota(db, group.duplicata.itemId,
                                "Dismissed as duplicate by user in batch. [USER_VERIFIED_NOT_DUPLICATE]");
            }
            db.run("COMMIT", {});
        } catch (...) {
            try { db.run("ROLLBACK", {}); } catch (...) {}
        }

        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            gruposDetectados_.erase(gruposDetectados_.begin() + *it);
        }
        atualizarListaEStatusAposResolucao();
        return;
    }

    // Validate selected: shared dialog, one action (keep 1 / keep 2 / keep both) per selected pair.
    std::vector<DuplicateResolutionDialog::Entry> entries;
    entries.reserve(indices.size());
    for (int idx : indices) {
        const auto& group = gruposDetectados_[static_cast<size_t>(idx)];
        DuplicateResolutionDialog::Entry e;
        e.primaryLabel = juce::String(group.original.titulo) + "  <->  " + juce::String(group.duplicata.titulo);
        e.secondaryLabel = (isPt ? juce::String::fromUTF8("Arquivo 1: ") : "File 1: ") + juce::String(group.original.caminhoRelativo)
                          + "\n" + (isPt ? juce::String::fromUTF8("Arquivo 2: ") : "File 2: ") + juce::String(group.duplicata.caminhoRelativo);
        e.action = 2; // default: keep both (validate as duplicate, delete nothing)
        {
            const auto c = criteriosDoPar(projeto_.projeto().registro(), group.original.itemId, group.duplicata.itemId);
            e.temCriterios = true;
            e.acaoMaisRecente = c.maisRecente == 0 ? -1 : c.maisRecente - 1;
            e.acaoBackupPrimeiro = c.backupPrimeiro == 0 ? -1 : c.backupPrimeiro - 1;
        }
        e.imagemA = carregarMiniaturaDoItem(projeto_, group.original.itemId, group.original.collectionCaminho);
        e.imagemB = carregarMiniaturaDoItem(projeto_, group.duplicata.itemId, group.duplicata.collectionCaminho);
        entries.push_back(e);
    }

    std::array<juce::String, 3> actionLabels{
        matriz::i18n::t("duplicatas.action_keep1"),
        matriz::i18n::t("duplicatas.action_keep2"),
        matriz::i18n::t("duplicatas.action_keep_both")
    };

    juce::Component::SafePointer<DuplicatesWorkspaceComponent> safeThis(this);
    DuplicateResolutionDialog::show(
        matriz::i18n::t("duplicatas.selected_dialog_titulo"),
        matriz::i18n::t("duplicatas.selected_dialog_intro"),
        entries, actionLabels,
        [safeThis, indices](bool confirmado, std::vector<DuplicateResolutionDialog::Entry> resultado) {
            if (!safeThis || !confirmado) return;

            auto& db = safeThis->projeto_.projeto().registro();
            std::vector<ProjetoAberto::ResultadoSanitizacao> resultados;
            std::vector<std::string> ids;
            try {
                db.run("BEGIN TRANSACTION", {});
                for (size_t i = 0; i < indices.size(); ++i) {
                    int idx = indices[i];
                    if (idx < 0 || idx >= static_cast<int>(safeThis->gruposDetectados_.size())) continue;
                    const auto& group = safeThis->gruposDetectados_[static_cast<size_t>(idx)];
                    int action = resultado[i].action;

                    if (action == 0) { // Keep File 1 — o outro fica só no SOURCE, fora do backup
                        resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.original.itemId, group.duplicata.itemId));
                    } else if (action == 1) { // Keep File 2
                        resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.duplicata.itemId, group.original.itemId));
                    } else { // Keep Both — os dois continuam entrando no backup
                        acrescentarNota(db, group.duplicata.itemId,
                                        "Validated as a known duplicate pair of " + juce::String(group.original.codigoAcervo) +
                                        " by user in batch — both sides kept in Make Backup. [USER_VERIFIED_DUPLICATE_KEEP_BOTH]");
                    }
                    ids.push_back(group.original.itemId);
                    ids.push_back(group.duplicata.itemId);
                }
                db.run("COMMIT", {});
                publicarSanitizacoes(safeThis->projeto_, resultados, ids);
            } catch (...) {
                try { db.run("ROLLBACK", {}); } catch (...) {}
            }

            juce::MessageManager::callAsync([safeThis, indices]() {
                if (!safeThis) return;
                for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
                    if (*it >= 0 && *it < static_cast<int>(safeThis->gruposDetectados_.size()))
                        safeThis->gruposDetectados_.erase(safeThis->gruposDetectados_.begin() + *it);
                }
                safeThis->atualizarListaEStatusAposResolucao();
            });
        });
}

void DuplicatesWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    bool isLight = (tk.fundo.getBrightness() > 0.5f);
    juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
    g.fillAll(bg);

    if (estado_ == State::Idle) {
        g.setColour(tk.borda.withAlpha(0.3f));
        auto r = getLocalBounds().reduced(20).withTrimmedTop(60).withHeight(getHeight() - 120);
        g.drawRoundedRectangle(r.toFloat(), tk.raioMedio, 1.5f);
    }
}

void DuplicatesWorkspaceComponent::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds().reduced(20);

    // Filter toolbar at the top
    auto areaFilter = area.removeFromTop(32);
    
    bool isPt = lblScope_->getText().containsIgnoreCase("Varredura");
    int wScopeLbl = isPt ? 75 : 50;
    int wScopeCb  = isPt ? 180 : 130;
    int wTypeLbl  = isPt ? 45 : 40;
    int wTypeCb   = isPt ? 165 : 125;
    int wSizeLbl  = isPt ? 70 : 45;
    int wSizeCb   = isPt ? 185 : 160;

    lblScope_->setBounds(areaFilter.removeFromLeft(wScopeLbl));
    areaFilter.removeFromLeft(4);
    cbScope_->setBounds(areaFilter.removeFromLeft(wScopeCb));
    areaFilter.removeFromLeft(16);
    
    lblFileType_->setBounds(areaFilter.removeFromLeft(wTypeLbl));
    areaFilter.removeFromLeft(4);
    cbFileType_->setBounds(areaFilter.removeFromLeft(wTypeCb));
    areaFilter.removeFromLeft(16);
    
    lblFileSize_->setBounds(areaFilter.removeFromLeft(wSizeLbl));
    areaFilter.removeFromLeft(4);
    cbSizeFilter_->setBounds(areaFilter.removeFromLeft(wSizeCb));
    
    if (txtSizeValue_->isVisible()) {
        areaFilter.removeFromLeft(8);
        txtSizeValue_->setBounds(areaFilter.removeFromLeft(60));
        areaFilter.removeFromLeft(8);
        cbSizeUnit_->setBounds(areaFilter.removeFromLeft(70));
    }

    areaFilter.removeFromLeft(16);
    lblAno_->setBounds(areaFilter.removeFromLeft(isPt ? 40 : 35));
    areaFilter.removeFromLeft(4);
    txtAnoDe_->setBounds(areaFilter.removeFromLeft(50));
    areaFilter.removeFromLeft(4);
    lblAnoAte_->setBounds(areaFilter.removeFromLeft(12));
    areaFilter.removeFromLeft(4);
    txtAnoAte_->setBounds(areaFilter.removeFromLeft(50));

    area.removeFromTop(10); // Spacing below filter bar

    if (estado_ == State::Results) {
        btnScan_->setVisible(true);
        btnScan_->setButtonText(isPt ? "NOVA VARREDURA" : "RE-SCAN");
        btnValidateAll_->setVisible(true);
        btnDismissAll_->setVisible(true);
        btnValidateSelected_->setVisible(true);
        btnDismissSelected_->setVisible(true);

        auto areaControle = area.removeFromTop(40);
        int btnW = isPt ? 160 : 180;
        int btnSelW = isPt ? 150 : 170;
        int scanW = isPt ? 140 : 120;
        btnValidateAll_->setBounds(areaControle.removeFromRight(btnW));
        areaControle.removeFromRight(6);
        btnValidateSelected_->setBounds(areaControle.removeFromRight(btnSelW));
        areaControle.removeFromRight(10);
        btnDismissAll_->setBounds(areaControle.removeFromRight(btnW));
        areaControle.removeFromRight(6);
        btnDismissSelected_->setBounds(areaControle.removeFromRight(btnSelW));
        areaControle.removeFromRight(10);
        btnScan_->setBounds(areaControle.removeFromRight(scanW));
        areaControle.removeFromRight(16);
        
        lblStatus_->setJustificationType(juce::Justification::centredLeft);
        lblStatus_->setBounds(areaControle);
        
        viewport_->setBounds(area);
        viewport_->setVisible(true);
        listaComponent_->setSize(viewport_->getWidth() - viewport_->getScrollBarThickness(), listaComponent_->getHeight());
    } else {
        btnScan_->setVisible(true);
        btnScan_->setButtonText(matriz::i18n::t("duplicatas.btn_scan"));
        btnValidateAll_->setVisible(false);
        btnDismissAll_->setVisible(false);
        btnValidateSelected_->setVisible(false);
        btnDismissSelected_->setVisible(false);
        lblStatus_->setJustificationType(juce::Justification::centred);
        lblStatus_->setBounds(area.removeFromTop(40));
        viewport_->setVisible(false);
        btnScan_->setBounds(getLocalBounds().withSizeKeepingCentre(240, 48));
    }
}

} // namespace matriz::ui
