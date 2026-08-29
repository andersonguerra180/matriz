#include "DuplicatesWorkspaceComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/LeituraTecnica.h"
#include "VideoPlayerComponent.h"
#include "../Vault/Resolucao.h"
#include "ModalMitigacao.h"
#include "ProgressoGlobal.h"

namespace matriz::ui {

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
            juce::File colDbFile = colDir.getChildFile("registro.sqlite");
            if (colDbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(colDbFile.getFullPathName().toStdString());
                    auto stmtArq = colDb.prepare("SELECT a.id FROM arquivo a WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
                    stmtArq.bind(1, matriz::db::Value::of(itemId_));
                    if (stmtArq.step()) {
                        std::string arquivoId = stmtArq.columnText(0);
                        auto fileOpt = matriz::vault::resolverArquivo(colDb, arquivoId, colDir);
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
            g.drawText("File offline or not found", getLocalBounds(), juce::Justification::centred);
            return;
        }
        
        if (isImage_) {
            if (image_.isValid()) {
                g.drawImageWithin(image_, 10, 10, getWidth() - 20, getHeight() - 20,
                                  juce::RectanglePlacement::centred, false);
            } else {
                g.setColour(tk.textoTerciario);
                g.drawText("Invalid Image File", getLocalBounds(), juce::Justification::centred);
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
        g.fillAll(tk.fundo);
    }

    void resized() override {
        auto area = getLocalBounds();
        int y = 10;
        
        for (auto* card : cards_) {
            int cardHeight = card->isExpanded() ? 490 : 190;
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
    }

    void recalculateHeight() {
        int totalHeight = 20;
        for (auto* card : cards_) {
            totalHeight += (card->isExpanded() ? 490 : 190) + 10;
        }
        setSize(getWidth(), totalHeight);
        resized();
    }

private:
    class CardComponent : public juce::Component {
    public:
        CardComponent(DuplicatesWorkspaceComponent& owner, size_t index, const DuplicateGroup& grupo, ListaResultadosComponent& parent)
            : owner_(owner), index_(index), grupo_(grupo), parent_(parent) {
            
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

        void mouseDown(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu()) {
                int w = getWidth() - 20;
                juce::Rectangle<int> origThumbRect(15, 36, 64, 64);
                juce::Rectangle<int> dupThumbRect(w / 2 + 5, 36, 64, 64);

                std::string targetItemId;
                if (origThumbRect.contains(e.getPosition()) || (e.x < getWidth() / 2 && e.y < 180)) {
                    targetItemId = grupo_.original.itemId;
                } else if (dupThumbRect.contains(e.getPosition()) || (e.x >= getWidth() / 2 && e.y < 180)) {
                    targetItemId = grupo_.duplicata.itemId;
                }

                if (!targetItemId.empty()) {
                    juce::PopupMenu menu;
                    menu.addItem(1, "SHOW SOURCE");
                    menu.addItem(2, "COPY PATH");
                    juce::Component::SafePointer<CardComponent> safeThis(this);
                    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, targetItemId](int res) {
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
                                            .withTitle("Source Not Found")
                                            .withMessage("The source file was not found at:\n" + *caminhoOpt)
                                            .withButton("OK"),
                                        nullptr);
                                }
                            } else {
                                juce::AlertWindow::showAsync(
                                    juce::MessageBoxOptions()
                                        .withIconType(juce::MessageBoxIconType::InfoIcon)
                                        .withTitle("Source Not Found")
                                        .withMessage("No source path recorded for this item.")
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
            
            // Background box
            g.setColour(tk.painel);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio, 1.0f);

            // Columns layout
            int w = getWidth() - 20;
            int colW = w / 2 - 20;

            auto drawColumn = [&](juce::Graphics& g, int x, const DuplicateMatch& m, const DuplicateMatch& other, bool isDup, const juce::Image& thumb) {
                if (!m.collectionNome.empty()) {
                    g.setColour(tk.acento);
                    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
                    g.drawText("COLLECTION: " + juce::String(m.collectionNome).toUpperCase(), x, 2, colW, 14, juce::Justification::left, true);
                }

                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
                g.drawText(isDup ? "POSSIBLE DUPLICATE" : "EXISTING ORIGINAL", x, 16, colW, 18, juce::Justification::left);

                // Draw thumbnail rectangle
                juce::Rectangle<int> thumbRect(x, 36, 64, 64);
                if (thumb.isValid()) {
                    g.drawImageWithin(thumb, thumbRect.getX(), thumbRect.getY(), thumbRect.getWidth(), thumbRect.getHeight(),
                                      juce::RectanglePlacement::centred, false);
                } else {
                    g.setColour(tk.painelAlt);
                    g.fillRoundedRectangle(thumbRect.toFloat(), tk.raioPequeno);
                    g.setColour(tk.textoTerciario);
                    g.setFont(juce::Font(juce::FontOptions(10.0f)));
                    juce::String extension = juce::String(m.ext).toUpperCase();
                    g.drawText(extension, thumbRect, juce::Justification::centred);
                }
                
                g.setColour(tk.borda);
                g.drawRoundedRectangle(thumbRect.toFloat(), tk.raioPequeno, 1.0f);

                // Text fields shifted by 74 pixels
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
                g.setColour(m.nomeCoincide ? juce::Colour(0xffef4444) : tk.textoPrimario);
                g.drawText("Title: " + m.titulo, x + 74, 36, colW - 74, 18, juce::Justification::left, true);

                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                g.setColour(tk.textoSecundario);
                g.drawText("Code: " + (m.codigoAcervo.empty() ? "N/A" : m.codigoAcervo), x + 74, 56, colW - 74, 16, juce::Justification::left);
                
                g.setColour(m.extCoincide ? juce::Colour(0xffef4444) : tk.textoSecundario);
                g.drawText("Format: " + juce::String(m.ext).toUpperCase(), x + 74, 72, colW - 74, 16, juce::Justification::left);

                if (m.duracao > 0.0) {
                    g.setColour(m.duracaoCoincide ? juce::Colour(0xffef4444) : tk.textoSecundario);
                    int min = static_cast<int>(m.duracao) / 60;
                    int sec = static_cast<int>(m.duracao) % 60;
                    g.drawText("Duration: " + juce::String::formatted("%02d:%02d", min, sec), x + 74, 88, colW - 74, 16, juce::Justification::left);
                } else {
                    g.setColour(tk.textoTerciario);
                    g.drawText("Duration: N/A", x + 74, 88, colW - 74, 16, juce::Justification::left);
                }

                if (m.lufs != 0.0) {
                    g.setColour(m.lufsCoincide ? juce::Colour(0xffef4444) : tk.textoSecundario);
                    g.drawText("LUFS-I: " + juce::String::formatted("%.2f LUFS", m.lufs), x + 74, 104, colW - 74, 16, juce::Justification::left);
                } else if (m.largura > 0 && m.altura > 0) {
                    g.setColour(m.dimCoincide ? juce::Colour(0xffef4444) : tk.textoSecundario);
                    g.drawText("Dimensions: " + juce::String(m.largura) + "x" + juce::String(m.altura), x + 74, 104, colW - 74, 16, juce::Justification::left);
                } else {
                    g.setColour(tk.textoTerciario);
                    g.drawText("Dimensions/LUFS-I: N/A", x + 74, 104, colW - 74, 16, juce::Justification::left);
                }

                g.setColour(m.tamanhoCoincide ? juce::Colour(0xffef4444) : tk.textoSecundario);
                double kb = static_cast<double>(m.tamanhoBytes) / 1024.0;
                g.drawText("File Size: " + juce::String::formatted("%.1f KB", kb), x + 74, 120, colW - 74, 16, juce::Justification::left);

                juce::String extraInfo = "";
                if (!m.orientation.empty()) {
                    extraInfo += "Orientation: " + juce::String(m.orientation);
                }
                if (!m.colorSpace.empty()) {
                    if (!extraInfo.isEmpty()) extraInfo += " | ";
                    extraInfo += "Color Space: " + juce::String(m.colorSpace);
                }
                if (!extraInfo.isEmpty()) {
                    g.setColour((m.orientationCoincide && m.colorSpaceCoincide) ? juce::Colour(0xffef4444) : tk.textoSecundario);
                    g.drawText(extraInfo, x + 74, 136, colW - 74, 16, juce::Justification::left, true);
                } else {
                    g.setColour(tk.textoTerciario);
                    g.drawText("EXIF Orientation & ColorSpace: N/A", x + 74, 136, colW - 74, 16, juce::Justification::left);
                }

                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                juce::String displayPath = m.fullPath.empty() ? m.caminhoRelativo : m.fullPath;
                g.drawText("Path: " + displayPath, x, 158, colW, 14, juce::Justification::left, true);
            };

            // Draw original details with its thumbnail
            drawColumn(g, 15, grupo_.original, grupo_.duplicata, false, thumbOriginal_);

            // Draw duplicate details with its thumbnail
            drawColumn(g, w / 2 + 5, grupo_.duplicata, grupo_.original, true, thumbDuplicata_);
            
            // Draw horizontal dividing line if expanded
            if (isExpanded_) {
                g.setColour(tk.borda);
                g.drawHorizontalLine(180, 10.0f, getWidth() - 10.0f);
            }
        }

        void resized() override {
            int w = getWidth();
            btnValidate_->setBounds(w - 180, 20, 160, 32);
            btnDismiss_->setBounds(w - 180, 60, 160, 32);
            
            if (isExpanded_) {
                int previewW = w / 2 - 25;
                if (previewOriginal_) previewOriginal_->setBounds(15, 195, previewW, 280);
                if (previewDuplicata_) previewDuplicata_->setBounds(w / 2 + 10, 195, previewW, 280);
            }
        }
        
        bool isExpanded() const { return isExpanded_; }
        
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
    
    btnScan_ = std::make_unique<juce::TextButton>("SCAN FOR DUPLICATES");
    btnScan_->onClick = [this] { iniciarScan(); };
    btnScan_->setTooltip("Start duplicate detection scan based on attributes");
    addAndMakeVisible(*btnScan_);

    lblScope_ = std::make_unique<juce::Label>("lblScope", "Scan:");
    lblScope_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lblScope_);

    cbScope_ = std::make_unique<juce::ComboBox>("cbScope");
    cbScope_->addItem("ALL FILES", 1);
    cbScope_->addItem("SELECTED FILES", 2);
    cbScope_->setSelectedId(1);
    cbScope_->setTooltip("Choose search scope (all database files vs selected grid items)");
    addAndMakeVisible(*cbScope_);

    lblFileType_ = std::make_unique<juce::Label>("lblFileType", "Type:");
    lblFileType_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lblFileType_);

    cbFileType_ = std::make_unique<juce::ComboBox>("cbFileType");
    cbFileType_->addItem("ALL FILES", 1);
    cbFileType_->addItem("IMAGE", 2);
    cbFileType_->addItem("VIDEO", 3);
    cbFileType_->addItem("AUDIO", 4);
    cbFileType_->addItem("DOCS", 5);
    cbFileType_->addItem("SESSIONS", 6);
    cbFileType_->addItem("OTHER", 7);
    cbFileType_->setSelectedId(1);
    cbFileType_->setTooltip("Filter candidates by media type");
    addAndMakeVisible(*cbFileType_);

    lblFileSize_ = std::make_unique<juce::Label>("lblFileSize", "Size:");
    lblFileSize_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lblFileSize_);

    cbSizeFilter_ = std::make_unique<juce::ComboBox>("cbSizeFilter");
    cbSizeFilter_->addItem("No Size Filter", 1);
    cbSizeFilter_->addItem("Size is bigger than", 2);
    cbSizeFilter_->addItem("Size is smaller than", 3);
    cbSizeFilter_->addItem("Size equals", 4);
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

    lblStatus_ = std::make_unique<juce::Label>("lblStatus", "");
    lblStatus_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblStatus_);

    btnValidateAll_ = std::make_unique<juce::TextButton>("VALIDATE ALL DUPLICATES");
    btnValidateAll_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22c55e)); // success green
    btnValidateAll_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnValidateAll_->onClick = [this] { resolverTudo(true); };
    btnValidateAll_->setTooltip("Validate all detected duplicates, keeping original versions");
    addChildComponent(*btnValidateAll_);

    btnDismissAll_ = std::make_unique<juce::TextButton>("DISMISS ALL DUPLICATES");
    btnDismissAll_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnDismissAll_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnDismissAll_->onClick = [this] { resolverTudo(false); };
    btnDismissAll_->setTooltip("Dismiss all duplicate alerts, keeping both files");
    addChildComponent(*btnDismissAll_);

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
    if (isThreadRunning() || estado_ == State::Results) {
        return; // Don't interrupt active scan or clear results on view reload
    }
    
    // Reset view
    estado_ = State::Idle;
    gruposDetectados_.clear();
    lblStatus_->setText("", juce::dontSendNotification);
    btnScan_->setButtonText("SCAN FOR DUPLICATES");
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

    estado_ = State::Scanning;
    progressoScan_ = 0.0;
    gruposDetectados_.clear();
    btnScan_->setEnabled(false);
    btnScan_->setButtonText("SCANNING...");
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
    };
    std::vector<ItemInfo> items;

    auto carregarItensDeDb = [&](matriz::db::Database& database, const juce::File& pastaProjeto, const std::string& colNome, const std::string& colCaminho) {
        try {
            auto stmt = database.prepare(
                "SELECT i.id, i.codigo_acervo, i.titulo, a.caminho_relativo, a.caracteristicas_tecnicas_json, a.tamanho_bytes, a.caminho_absoluto_origem "
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
                info.collectionNome = colNome;
                info.collectionCaminho = colCaminho;

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

                items.push_back(info);
            }
        } catch (...) {}
    };

    if (isCatalogMode) {
        auto colecoes = projeto_.listarColecoesLinkadas();
        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbF = colDir.getChildFile("registro.sqlite");
            if (dbF.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbF.getFullPathName().toStdString());
                    carregarItensDeDb(colDb, colDir, c.nome.toStdString(), c.caminhoProjeto.toStdString());
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
            btnScan_->setButtonText("SCAN FOR DUPLICATES");
            
            juce::String msgFinal;
            if (gruposDetectados_.empty()) {
                estado_ = State::Clean;
                msgFinal = "No duplicates found. Your archive is 100% clean!";
                lblStatus_->setText(msgFinal, juce::dontSendNotification);
            } else {
                estado_ = State::Results;
                msgFinal = "Scan completed. Found " + juce::String(gruposDetectados_.size()) + " duplicate groups.";
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
            juce::String nota = "Dismissed as duplicate by user. [USER_VERIFIED_NOT_DUPLICATE]";
            db.run("UPDATE item SET notas_livres = ? WHERE id = ?",
                   {matriz::db::Value::of(nota.toStdString()),
                    matriz::db::Value::of(group.duplicata.itemId)});
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
        return;
    }

    // Validation path: Show prompt to ask which file to keep
    auto janela = std::make_shared<juce::AlertWindow>(
        "Resolve Duplicate Match",
        "How would you like to handle this duplicate pair?\n\n"
        "File 1 (Original): " + juce::String(group.original.titulo) + "\n"
        "File 2 (Duplicate): " + juce::String(group.duplicata.titulo),
        juce::MessageBoxIconType::QuestionIcon
    );
    janela->addButton("KEEP FILE 1", 1);
    janela->addButton("KEEP FILE 2", 2);
    janela->addButton("KEEP BOTH", 3);
    janela->addButton("RETURN", 4, juce::KeyPress(juce::KeyPress::escapeKey));

    janela->enterModalState(true, juce::ModalCallbackFunction::create([this, janela, grupoIdx, group](int buttonResult) {
        retirarPeerDaTela(*janela);
        if (buttonResult == 0 || buttonResult == 4) return; // User cancelled/returned or closed without selecting

        auto& db = projeto_.projeto().registro();
        try {
            db.run("BEGIN TRANSACTION", {});
            
            if (buttonResult == 1) { // Keep Original (1) -> Delete Duplicate (2)
                db.run("DELETE FROM acervo_item_pasta WHERE item_id = ?", {matriz::db::Value::of(group.duplicata.itemId)});
                db.run("DELETE FROM arquivo WHERE item_id = ?", {matriz::db::Value::of(group.duplicata.itemId)});
                db.run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(group.duplicata.itemId)});
            }
            else if (buttonResult == 2) { // Keep Duplicate (2) -> Delete Original (1)
                db.run("DELETE FROM acervo_item_pasta WHERE item_id = ?", {matriz::db::Value::of(group.original.itemId)});
                db.run("DELETE FROM arquivo WHERE item_id = ?", {matriz::db::Value::of(group.original.itemId)});
                db.run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(group.original.itemId)});
                
                juce::String nota = "Kept as unique item after duplicate resolution. Original was deleted.";
                db.run("UPDATE item SET estado = 'novo', notas_livres = ? WHERE id = ?",
                       {matriz::db::Value::of(nota.toStdString()),
                        matriz::db::Value::of(group.duplicata.itemId)});
            }
            else if (buttonResult == 3) { // Keep Both (3) -> Set state to 'duplicata'
                juce::String nota = "Validated as duplicate of " + juce::String(group.original.codigoAcervo) + " by user. [USER_VERIFIED_DUPLICATE]";
                db.run("UPDATE item SET estado = 'duplicata', notas_livres = ? WHERE id = ?",
                       {matriz::db::Value::of(nota.toStdString()),
                        matriz::db::Value::of(group.duplicata.itemId)});
            }
            
            db.run("COMMIT", {});
        } catch (...) {
            try { db.run("ROLLBACK", {}); } catch (...) {}
        }

        // Run UI update on MessageThread context
        juce::MessageManager::callAsync([this, grupoIdx]() {
            if (grupoIdx >= 0 && grupoIdx < static_cast<int>(gruposDetectados_.size())) {
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
        });
    }));
}

void DuplicatesWorkspaceComponent::resolverTudo(bool ehDuplicataReal) {
    if (gruposDetectados_.empty()) return;
    
    juce::String msg = ehDuplicataReal
        ? "Are you sure you want to validate all " + juce::String(gruposDetectados_.size()) + " duplicate groups as duplicates? This will update their state to 'duplicata'."
        : "Are you sure you want to dismiss all " + juce::String(gruposDetectados_.size()) + " duplicate groups? They will not be flagged as duplicates again.";
        
    bool confirm = juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon,
        ehDuplicataReal ? "Validate All Duplicates" : "Dismiss All Duplicates",
        msg,
        "Yes", "No",
        this
    );
    
    if (!confirm) return;
    
    auto& db = projeto_.projeto().registro();
    
    try {
        db.run("BEGIN TRANSACTION", {});
        for (const auto& group : gruposDetectados_) {
            if (ehDuplicataReal) {
                juce::String nota = "Validated as duplicate of " + juce::String(group.original.codigoAcervo) + " by user in batch. [USER_VERIFIED_DUPLICATE]";
                db.run("UPDATE item SET estado = 'duplicata', notas_livres = ? WHERE id = ?",
                       {matriz::db::Value::of(nota.toStdString()),
                        matriz::db::Value::of(group.duplicata.itemId)});
            } else {
                juce::String nota = "Dismissed as duplicate by user in batch. [USER_VERIFIED_NOT_DUPLICATE]";
                db.run("UPDATE item SET notas_livres = ? WHERE id = ?",
                       {matriz::db::Value::of(nota.toStdString()),
                        matriz::db::Value::of(group.duplicata.itemId)});
            }
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
}

void DuplicatesWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

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
    
    lblScope_->setBounds(areaFilter.removeFromLeft(40));
    cbScope_->setBounds(areaFilter.removeFromLeft(130));
    areaFilter.removeFromLeft(15);
    
    lblFileType_->setBounds(areaFilter.removeFromLeft(40));
    cbFileType_->setBounds(areaFilter.removeFromLeft(120));
    areaFilter.removeFromLeft(15);
    
    lblFileSize_->setBounds(areaFilter.removeFromLeft(40));
    cbSizeFilter_->setBounds(areaFilter.removeFromLeft(160));
    
    if (txtSizeValue_->isVisible()) {
        areaFilter.removeFromLeft(8);
        txtSizeValue_->setBounds(areaFilter.removeFromLeft(60));
        areaFilter.removeFromLeft(8);
        cbSizeUnit_->setBounds(areaFilter.removeFromLeft(70));
    }
    
    area.removeFromTop(10); // Spacing below filter bar

    if (estado_ == State::Results) {
        btnScan_->setVisible(true);
        btnScan_->setButtonText("RE-SCAN");
        btnValidateAll_->setVisible(true);
        btnDismissAll_->setVisible(true);
        
        auto areaControle = area.removeFromTop(40);
        lblStatus_->setJustificationType(juce::Justification::centredLeft);
        lblStatus_->setBounds(areaControle.removeFromLeft(areaControle.getWidth() - 510));
        
        btnValidateAll_->setBounds(areaControle.removeFromRight(180));
        areaControle.removeFromRight(10);
        btnDismissAll_->setBounds(areaControle.removeFromRight(180));
        areaControle.removeFromRight(10);
        btnScan_->setBounds(areaControle.removeFromRight(120));
        
        viewport_->setBounds(area);
        viewport_->setVisible(true);
        listaComponent_->setSize(viewport_->getWidth() - viewport_->getScrollBarThickness(), listaComponent_->getHeight());
    } else {
        btnScan_->setVisible(true);
        btnScan_->setButtonText("SCAN FOR DUPLICATES");
        btnValidateAll_->setVisible(false);
        btnDismissAll_->setVisible(false);
        lblStatus_->setJustificationType(juce::Justification::centred);
        lblStatus_->setBounds(area.removeFromTop(40));
        viewport_->setVisible(false);
        btnScan_->setBounds(getLocalBounds().withSizeKeepingCentre(240, 48));
    }
}

} // namespace matriz::ui
