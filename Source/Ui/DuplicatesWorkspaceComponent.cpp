#include "DuplicatesWorkspaceComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "Moldura.h"
#include "../I18n/Strings.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Ingest/Checksum.h"
#include "../Ingest/Duplicata.h"
#include "VideoPlayerComponent.h"
#include "../Vault/Resolucao.h"
#include "ModalMitigacao.h"
#include "ProgressoGlobal.h"
#include "DuplicateResolutionDialog.h"
#include "../Model/ProjectLog.h"
#include "EventBus.h"
#include <unordered_map>
#include <unordered_set>
#include "ConflitosMergeDialog.h"

namespace matriz::ui {

namespace {
    // Diálogo de decisão de duplicata: mensagem + GRADE simétrica de botões (3 colunas, mesma largura/altura) + VOLTAR em
    // largura total. Substitui o AlertWindow, cujos 6 botões numa linha só estouravam a largura e cortavam o último.
    class DialogoEscolhaDuplicata : public juce::Component {
    public:
        struct Botao { juce::String texto; int codigo; };

        DialogoEscolhaDuplicata(const juce::String& mensagem, std::vector<Botao> botoes, const juce::String& textoVoltar,
                                std::function<void(int)> aoEscolher)
            : botoes_(std::move(botoes)), aoEscolher_(std::move(aoEscolher)) {
            const auto& tk = tema();
            lblMensagem_.setText(mensagem, juce::dontSendNotification);
            lblMensagem_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            lblMensagem_.setColour(juce::Label::textColourId, tk.textoPrimario);
            lblMensagem_.setJustificationType(juce::Justification::topLeft);
            lblMensagem_.setMinimumHorizontalScale(1.0f);
            addAndMakeVisible(lblMensagem_);
            for (const auto& b : botoes_) {
                auto btn = std::make_unique<juce::TextButton>(b.texto);
                btn->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
                btn->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
                const int codigo = b.codigo;
                btn->onClick = [this, codigo] { escolher(codigo); };
                addAndMakeVisible(*btn);
                botoesUi_.push_back(std::move(btn));
            }
            btnVoltar_.setButtonText(textoVoltar);
            btnVoltar_.setColour(juce::TextButton::buttonColourId, tk.painel);
            btnVoltar_.setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
            btnVoltar_.onClick = [this] { escolher(0); };
            addAndMakeVisible(btnVoltar_);
            const int linhas = (static_cast<int>(botoes_.size()) + kColunas - 1) / kColunas;
            setSize(kLargura, kMargem + kAlturaMensagem + kGap + linhas * kAlturaBotao + (linhas - 1) * kGap + kGap + 34 + kMargem);
        }

        void paint(juce::Graphics& g) override { g.fillAll(tema().fundo); }

        void resized() override {
            auto area = getLocalBounds().reduced(kMargem);
            lblMensagem_.setBounds(area.removeFromTop(kAlturaMensagem));
            area.removeFromTop(kGap);
            const int largura = (area.getWidth() - (kColunas - 1) * kGap) / kColunas;
            const int linhas = (static_cast<int>(botoesUi_.size()) + kColunas - 1) / kColunas;
            for (int i = 0; i < static_cast<int>(botoesUi_.size()); ++i) {
                const int col = i % kColunas, lin = i / kColunas;
                botoesUi_[static_cast<size_t>(i)]->setBounds(area.getX() + col * (largura + kGap),
                                                             area.getY() + lin * (kAlturaBotao + kGap), largura, kAlturaBotao);
            }
            area.removeFromTop(linhas * kAlturaBotao + (linhas - 1) * kGap + kGap);
            btnVoltar_.setBounds(area.removeFromTop(34));
        }

        static void exibir(const juce::String& titulo, const juce::String& mensagem, std::vector<Botao> botoes,
                           const juce::String& textoVoltar, std::function<void(int)> aoEscolher) {
            auto* conteudo = new DialogoEscolhaDuplicata(mensagem, std::move(botoes), textoVoltar, std::move(aoEscolher));
            juce::DialogWindow::LaunchOptions opt;
            opt.dialogTitle = titulo;
            opt.content.setOwned(conteudo);
            opt.dialogBackgroundColour = tema().fundo;
            opt.escapeKeyTriggersCloseButton = true;
            opt.useNativeTitleBar = true;
            opt.resizable = false;
            moldura::lancar(opt);
        }

    private:
        void escolher(int codigo) {
            auto cb = aoEscolher_;
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(codigo);
            if (codigo != 0 && cb) juce::MessageManager::callAsync([cb, codigo] { cb(codigo); });
        }

        static constexpr int kLargura = 640, kMargem = 24, kGap = 12, kColunas = 3, kAlturaBotao = 44, kAlturaMensagem = 92;
        std::vector<Botao> botoes_;
        std::function<void(int)> aoEscolher_;
        juce::Label lblMensagem_;
        std::vector<std::unique_ptr<juce::TextButton>> botoesUi_;
        juce::TextButton btnVoltar_;
    };

    // Closed: top margin (10) + subcard height (130) + bottom margin (12).
    // Sub-card maior (miniatura ocupando a altura toda — item 2).
    constexpr int kAlturaSubCard = 196;  // cabe título + 5 linhas (Código, Match, Formato, Dimensões, Tamanho) + caminho sem sobrepor
    constexpr int kAlturaFechado = kAlturaSubCard + 22;
    constexpr int kAlturaAberto = kAlturaSubCard + 360;

    // PROBABLE/SIMILAR (pHash) nunca entram em decisão em lote: só um a um, olhando.
    template <class G> bool ehPrevisto(const G& g) { return g.criterio == "PROBABLE" || g.criterio == "SIMILAR"; }
    template <class V> int contarPrevistos(const V& gs) {
        return static_cast<int>(std::count_if(gs.begin(), gs.end(), [](const auto& g) { return ehPrevisto(g); }));
    }
    constexpr int kAlturaBannerRisco = 52;
    // A resolução em background desfaz tudo (rollback) se algo falha; sem isto o clique parecia "não fazer nada".
    void avisarFalhaResolucao() {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("duplicatas.falha_titulo"),
                                               matriz::i18n::t("duplicatas.falha_corpo"), {}, nullptr,
                                               juce::ModalCallbackFunction::create([](int) {}));
    }
    // Texto do que sobrou depois de uma decisão em lote: parecidos (pHash) são decisão olhando; empate/sem backup é outro motivo.
    template <class V> juce::String textoManuais(const V& gs) {
        const int previstos = contarPrevistos(gs);
        if (previstos == static_cast<int>(gs.size()))
            return matriz::i18n::t("duplicatas.manuais_previstos").replace("{n}", juce::String(previstos));
        return matriz::i18n::t("duplicatas.manuais").replace("{n}", juce::String((int) gs.size()));
    }

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
        // Fase 4: quantos itens mantidos ficaram com conflito (valor perdedor
        // no histórico) — revisão no filtro "Merge conflicts" do METADATA.
        int comConflito = 0;
        for (const auto& r : resultados) if (r.conflitos > 0) ++comConflito;
        if (comConflito > 0)
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, matriz::i18n::t("merge.resolver_titulo"),
                                                   matriz::i18n::t("duplicatas.conflitos_lote").replace("{n}", juce::String(comConflito)),
                                                   {}, nullptr, juce::ModalCallbackFunction::create([](int) {}));
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
                g.setOpacity(1.0f);  // a imagem herda o alfa da cor atual
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
            btnValidate_->setColour(juce::TextButton::buttonColourId,  // verde = idêntico/seguro; âmbar = parecido, decidir olhando
                                    (grupo.criterio == "PROBABLE" || grupo.criterio == "SIMILAR") ? juce::Colour(0xfff59e0b)
                                                                                                 : juce::Colour(0xff22c55e));
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
            // Risco visível: par "parecido" (pHash) ganha borda e fundo âmbar; idêntico (SHA-256) borda verde.
            const bool ehPrevisto = grupo_.criterio == "PROBABLE" || grupo_.criterio == "SIMILAR";
            const juce::Colour corAmbar = grupo_.criterio == "PROBABLE" ? juce::Colour(0xffffa000) : juce::Colour(0xff32bced);
            const juce::Colour corSeguro(0xff1ec45b);
            const auto corCriterio = grupo_.criterio == "IDENTICAL" ? corSeguro : corAmbar;
            g.setColour(tk.painel);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio);
            if (ehPrevisto) {
                g.setColour(corAmbar.withAlpha(0.07f));
                g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio);
                g.setColour(corAmbar);
                g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), tk.raioMedio, 2.0f);
            } else if (grupo_.criterio == "IDENTICAL") {
                g.setColour(corSeguro.withAlpha(0.75f));
                g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), tk.raioMedio, 2.0f);
            } else {
                g.setColour(tk.borda);
                g.drawRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio, 1.0f);
            }

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
                g.setColour(corCriterio);
                g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(subWidth), 24.0f, 6.0f);
                g.fillRect(static_cast<float>(x), static_cast<float>(y) + 12.0f, static_cast<float>(subWidth), 12.0f);
                g.restoreState();

                // Banner divider line
                g.setColour(tk.borda.withAlpha(0.40f));
                g.drawHorizontalLine(y + 24, static_cast<float>(x), static_cast<float>(x + subWidth));

                // Badge / Header text
                g.setColour(juce::Colours::black);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
                juce::String headerTag = "#" + juce::String(m.ordemChegada) + "/" + juce::String(m.totalRelacionados);
                if (!m.collectionNome.empty()) {
                    headerTag << " \u00B7 " << juce::String(m.collectionNome).toUpperCase();
                }
                if (grupo_.criterio == "IDENTICAL") headerTag << " \u00B7 IDENTICAL";
                else if (grupo_.criterio == "PROBABLE") headerTag << " \u00B7 PROBABLE \u00B7 REVIEW";
                else headerTag << " \u00B7 POSSIBLE \u00B7 REVIEW";
                // Versão de resolução menor (ex.: foto recuperada de HD com problema): marca qual lado tem menos qualidade.
                int larguraSelo = 0;
                if (m.largura > 0 && m.altura > 0 && other.largura > 0 && other.altura > 0) {
                    const long long areaM = static_cast<long long>(m.largura) * m.altura;
                    const long long areaO = static_cast<long long>(other.largura) * other.altura;
                    if (areaM != areaO) {
                        const bool menor = areaM < areaO;
                        const juce::String rotulo = menor ? "LOWER RESOLUTION" : "HIGHER RESOLUTION";
                        const juce::Font fonteSelo(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold));
                        larguraSelo = juce::GlyphArrangement::getStringWidthInt(fonteSelo, rotulo) + 14;
                        juce::Rectangle<int> selo(x + subWidth - larguraSelo - 6, y + 4, larguraSelo, 16);
                        g.setColour(menor ? juce::Colour(0xffef4444) : juce::Colour(0xff22c55e));
                        g.fillRoundedRectangle(selo.toFloat(), 8.0f);
                        g.setColour(juce::Colours::white);
                        g.setFont(fonteSelo);
                        g.drawText(rotulo, selo, juce::Justification::centred);
                        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
                        g.setColour(isDup ? tk.alerta : tk.acento);
                        larguraSelo += 6;
                    }
                }
                g.drawText(headerTag, x + 8, y + 2, subWidth - 16 - larguraSelo, 20, juce::Justification::centredLeft, true);

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
                        g.setOpacity(1.0f);  // a cor acima tem alfa 0.35 e a imagem herdava isso (miniatura opaca)
                        g.drawImageWithin(thumb, thumbRect.getX() + 4, thumbRect.getY() + 4, thumbRect.getWidth() - 8,
                                          thumbRect.getHeight() - 8, juce::RectanglePlacement::centred, false);
                    } else {
                        g.setColour(tk.textoTerciario);
                        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
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
                if (grupo_.criterio == "IDENTICAL")
                    linhaInfo("Match", "same SHA-256", juce::Colour(0xffef4444));          // coincidência (vermelho, como os outros)
                else if (grupo_.criterio == "PROBABLE")
                    linhaInfo("Match", "looks identical (pHash d=" + juce::String(grupo_.distanciaPHash) + ")", juce::Colour(0xfff59e0b));
                else if (grupo_.criterio == "SIMILAR")
                    linhaInfo("Match", "similar shot, seconds apart (pHash d=" + juce::String(grupo_.distanciaPHash) + ")", juce::Colour(0xfff59e0b));
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
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
                juce::String displayPath = m.fullPath.empty() ? m.caminhoRelativo : m.fullPath;
                g.drawFittedText(displayPath, metaX, rodapeY, metaW, 30, juce::Justification::topLeft, 2, 1.0f);
            };

            // Draw Original sub-card
            drawSubCard(g, 14, 10, subW, subH, grupo_.original, grupo_.duplicata, false, thumbOriginal_);

            // Draw Duplicate sub-card
            drawSubCard(g, 14 + subW + 14, 10, subW, subH, grupo_.duplicata, grupo_.original, true, thumbDuplicata_);
            
            // Nota de risco sob os botões (painel direito): âmbar = decida olhando, verde = byte a byte igual.
            {
                const juce::Rectangle<int> nota(w - btnW - 14, 105, btnW, subH - 95);
                if (ehPrevisto) {
                    g.setColour(corAmbar.withAlpha(0.16f));
                    g.fillRoundedRectangle(nota.toFloat(), 5.0f);
                    g.setColour(corAmbar);
                    g.drawRoundedRectangle(nota.toFloat().reduced(0.5f), 5.0f, 1.0f);
                    auto in = nota.reduced(7, 5);
                    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
                    g.drawText(juce::String::fromUTF8("\u26A0 NOT BYTE-IDENTICAL"), in.removeFromTop(14), juce::Justification::centredLeft, true);
                    g.setColour(tk.textoSecundario);
                    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
                    g.drawFittedText(grupo_.criterio == "SIMILAR"
                                         ? "Looks alike but may be a different shot. Compare both before choosing."
                                         : "Looks alike but may be an edit or a different file. Compare both before choosing.",
                                     in, juce::Justification::topLeft, 4, 1.0f);
                } else if (grupo_.criterio == "IDENTICAL") {
                    g.setColour(corSeguro.withAlpha(0.14f));
                    g.fillRoundedRectangle(nota.toFloat(), 5.0f);
                    g.setColour(corSeguro);
                    g.drawRoundedRectangle(nota.toFloat().reduced(0.5f), 5.0f, 1.0f);
                    auto in = nota.reduced(7, 5);
                    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
                    g.drawText(juce::String::fromUTF8("\u2713 IDENTICAL FILES"), in.removeFromTop(14), juce::Justification::centredLeft, true);
                    g.setColour(tk.textoSecundario);
                    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
                    juce::String textoNota("Same SHA-256: safe to validate.");
                    if (grupo_.pastaArquivos > 0)
                        textoNota << "\n" << matriz::i18n::t(grupo_.pastaMesmoConteudo ? "duplicatas.pasta_mesmo_conteudo"
                                                                                       : "duplicatas.pasta_inteira")
                                                 .replace("{n}", juce::String(grupo_.pastaArquivos))
                                                 .replace("{a}", juce::String::fromUTF8(grupo_.pastaNome.c_str()))
                                                 .replace("{b}", juce::String::fromUTF8(grupo_.pastaEmNome.c_str()));
                    g.drawFittedText(textoNota, in, juce::Justification::topLeft, grupo_.pastaArquivos > 0 ? 6 : 3, 1.0f);
                }
            }

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
    lblScope_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*lblScope_);

    cbScope_ = std::make_unique<juce::ComboBox>("cbScope");
    cbScope_->addItem(matriz::i18n::t("duplicatas.scope_all"), 1);
    cbScope_->addItem(matriz::i18n::t("duplicatas.scope_selected"), 2);
    cbScope_->setSelectedId(1);
    cbScope_->setTooltip("Choose search scope (all database files vs selected grid items)");
    addAndMakeVisible(*cbScope_);

    lblFileType_ = std::make_unique<juce::Label>("lblFileType", matriz::i18n::t("duplicatas.lbl_type"));
    lblFileType_->setJustificationType(juce::Justification::centredLeft);
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
    lblFileSize_->setJustificationType(juce::Justification::centredLeft);
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
    lblAno_ = std::make_unique<juce::Label>("lblAno", matriz::i18n::t("dup.ano"));
    lblAno_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*lblAno_);

    txtAnoDe_ = std::make_unique<juce::TextEditor>("txtAnoDe");
    txtAnoDe_->setInputRestrictions(4, "0123456789");
    txtAnoDe_->setTextToShowWhenEmpty(matriz::i18n::t("dup.de"), juce::Colours::grey);
    txtAnoDe_->setTooltip("Only include items from this year onward (blank = no lower limit)");
    addAndMakeVisible(*txtAnoDe_);

    lblAnoAte_ = std::make_unique<juce::Label>("lblAnoAte", juce::CharPointer_UTF8("\xe2\x80\x93")); // "–"
    lblAnoAte_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblAnoAte_);

    txtAnoAte_ = std::make_unique<juce::TextEditor>("txtAnoAte");
    txtAnoAte_->setInputRestrictions(4, "0123456789");
    txtAnoAte_->setTextToShowWhenEmpty(matriz::i18n::t("dup.ate"), juce::Colours::grey);
    txtAnoAte_->setTooltip("Only include items up to this year (blank = no upper limit)");
    addAndMakeVisible(*txtAnoAte_);

    auto criarToggle = [this](std::unique_ptr<juce::ToggleButton>& btn, const juce::String& chaveTexto, const juce::String& chaveDica, bool inicial) {
        btn = std::make_unique<juce::ToggleButton>(matriz::i18n::t(chaveTexto));
        btn->setToggleState(inicial, juce::dontSendNotification);
        btn->setTooltip(matriz::i18n::t(chaveDica));
        btn->setColour(juce::ToggleButton::textColourId, tema().textoSecundario);
        btn->setColour(juce::ToggleButton::tickColourId, tema().acento);
        addAndMakeVisible(*btn);
    };

    criarToggle(tbSha256_, "duplicatas.match_sha256", "duplicatas.match_sha256_dica", true);
    criarToggle(tbPHash_, "duplicatas.match_visual", "duplicatas.match_visual_dica", true);
    criarToggle(tbNome_, "duplicatas.match_nome", "duplicatas.match_nome_dica", false);
    criarToggle(tbTamanho_, "duplicatas.match_tamanho", "duplicatas.match_tamanho_dica", true);
    criarToggle(tbData_, "duplicatas.match_data", "duplicatas.match_data_dica", false);
    criarToggle(tbDuracao_, "duplicatas.match_duracao", "duplicatas.match_duracao_dica", true);
    criarToggle(tbDimensoes_, "duplicatas.match_dimensoes", "duplicatas.match_dimensoes_dica", false);
    criarToggle(tbEspacoCor_, "duplicatas.match_espaco_cor", "duplicatas.match_espaco_cor_dica", true);
    criarToggle(tbFormato_, "duplicatas.match_formato", "duplicatas.match_formato_dica", false);

    lblStatus_ = std::make_unique<juce::Label>("lblStatus", "");
    lblStatus_->setJustificationType(juce::Justification::centred);
    lblStatus_->setMinimumHorizontalScale(1.0f);  // o status quebra em linhas na coluna estreita
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

    btnReset_ = std::make_unique<juce::TextButton>(matriz::i18n::t("duplicatas.btn_reset"));
    btnReset_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnReset_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnReset_->setTooltip(matriz::i18n::t("duplicatas.btn_reset_dica"));
    btnReset_->onClick = [this] { resetarAba(); };
    addChildComponent(*btnReset_);

    btnResolvidas_ = std::make_unique<juce::TextButton>();
    btnResolvidas_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnResolvidas_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnResolvidas_->onClick = [this] { abrirResolvidas(); };
    addAndMakeVisible(*btnResolvidas_);
    atualizarContagemResolvidas();

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

namespace {

// Lista das duplicatas já resolvidas (escondidas do app) com RESTAURAR. Vive numa janela própria.
class DuplicatasResolvidasDialogo : public juce::Component, private juce::ListBoxModel {
public:
    using Item = ProjetoAberto::DuplicataResolvida;
    DuplicatasResolvidasDialogo(std::vector<Item> itens, std::function<int(const std::vector<std::string>&)> aoRestaurar,
                                std::function<std::vector<Item>()> recarregar)
        : itens_(std::move(itens)), aoRestaurar_(std::move(aoRestaurar)), recarregar_(std::move(recarregar)) {
        const auto& tk = tema();
        lblIntro_.setText(matriz::i18n::t("duplicatas.resolvidas_intro"), juce::dontSendNotification);
        lblIntro_.setColour(juce::Label::textColourId, tk.textoSecundario);
        lblIntro_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        addAndMakeVisible(lblIntro_);
        lista_.setModel(this);
        lista_.setMultipleSelectionEnabled(true);
        lista_.setRowHeight(26);
        addAndMakeVisible(lista_);
        lblVazio_.setText(matriz::i18n::t("duplicatas.resolvidas_vazio"), juce::dontSendNotification);
        lblVazio_.setColour(juce::Label::textColourId, tk.textoTerciario);
        lblVazio_.setJustificationType(juce::Justification::centred);
        addChildComponent(lblVazio_);
        for (auto* b : {&btnSel_, &btnTudo_, &btnFechar_}) {
            b->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
            b->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
            addAndMakeVisible(*b);
        }
        btnSel_.setButtonText(matriz::i18n::t("duplicatas.btn_restaurar_sel"));
        btnTudo_.setButtonText(matriz::i18n::t("duplicatas.btn_restaurar_tudo"));
        btnFechar_.setButtonText(matriz::i18n::t("duplicatas.btn_fechar"));
        btnSel_.onClick = [this] {
            std::vector<std::string> ids;
            const auto sel = lista_.getSelectedRows();
            for (int i = 0; i < sel.size(); ++i) {
                const int r = sel[i];
                if (r >= 0 && r < static_cast<int>(itens_.size())) ids.push_back(itens_[static_cast<size_t>(r)].id);
            }
            restaurar(ids);
        };
        btnTudo_.onClick = [this] {
            juce::Component::SafePointer<DuplicatasResolvidasDialogo> seguro(this);
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle(matriz::i18n::t("duplicatas.resolvidas_titulo"))
                    .withMessage(matriz::i18n::t("duplicatas.restaurar_tudo_confirma").replace("{n}", juce::String((int) itens_.size())))
                    .withButton(matriz::i18n::t("duplicatas.btn_restaurar_tudo"))
                    .withButton(matriz::i18n::t("dialogo.cancelar")),
                juce::ModalCallbackFunction::create([seguro](int r) {
                    if (r != 1 || seguro == nullptr) return;
                    std::vector<std::string> ids;
                    for (const auto& it : seguro->itens_) ids.push_back(it.id);
                    seguro->restaurar(ids);
                }));
        };
        btnFechar_.onClick = [this] {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
        };
        atualizarEstado();
        setSize(780, 480);
    }

    void paint(juce::Graphics& g) override { g.fillAll(tema().painel); }

    void resized() override {
        auto r = getLocalBounds().reduced(16);
        lblIntro_.setBounds(r.removeFromTop(48));
        r.removeFromTop(6);
        auto botoes = r.removeFromBottom(32);
        btnFechar_.setBounds(botoes.removeFromRight(110));
        botoes.removeFromRight(8);
        btnTudo_.setBounds(botoes.removeFromRight(170));
        botoes.removeFromRight(8);
        btnSel_.setBounds(botoes.removeFromRight(210));
        r.removeFromBottom(8);
        lista_.setBounds(r);
        lblVazio_.setBounds(r);
    }

private:
    int getNumRows() override { return static_cast<int>(itens_.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selecionada) override {
        if (row < 0 || row >= static_cast<int>(itens_.size())) return;
        const auto& tk = tema();
        if (selecionada) { g.setColour(tk.acento.withAlpha(0.18f)); g.fillRect(0, 0, w, h); }
        const auto& it = itens_[static_cast<size_t>(row)];
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.setColour(tk.textoPrimario);
        g.drawText(juce::String::fromUTF8(it.codigo.c_str()), 8, 0, 110, h, juce::Justification::centredLeft, true);
        g.drawText(juce::String::fromUTF8(it.titulo.c_str()), 124, 0, 220, h, juce::Justification::centredLeft, true);
        g.setColour(tk.textoSecundario);
        g.drawText(juce::String::fromUTF8(it.caminho.c_str()), 352, 0, w - 360, h, juce::Justification::centredLeft, true);
    }

    void restaurar(const std::vector<std::string>& ids) {
        if (ids.empty() || !aoRestaurar_) return;
        aoRestaurar_(ids);
        if (recarregar_) itens_ = recarregar_();
        lista_.deselectAllRows();
        lista_.updateContent();
        atualizarEstado();
    }
    void atualizarEstado() {
        lista_.updateContent();
        lista_.repaint();
        lblVazio_.setVisible(itens_.empty());
        btnSel_.setEnabled(!itens_.empty());
        btnTudo_.setEnabled(!itens_.empty());
    }

    std::vector<Item> itens_;
    std::function<int(const std::vector<std::string>&)> aoRestaurar_;
    std::function<std::vector<Item>()> recarregar_;
    juce::Label lblIntro_, lblVazio_;
    juce::ListBox lista_;
    juce::TextButton btnSel_, btnTudo_, btnFechar_;
};

}  // namespace

void DuplicatesWorkspaceComponent::abrirResolvidas() {
    juce::Component::SafePointer<DuplicatesWorkspaceComponent> seguro(this);
    auto* conteudo = new DuplicatasResolvidasDialogo(
        projeto_.listarDuplicatasResolvidas(),
        [seguro](const std::vector<std::string>& ids) {
            if (seguro == nullptr) return 0;
            const int n = seguro->projeto_.restaurarDuplicatas(ids);  // message thread (EventBus)
            seguro->atualizarContagemResolvidas();
            return n;
        },
        [seguro] { return seguro != nullptr ? seguro->projeto_.listarDuplicatasResolvidas()
                                            : std::vector<ProjetoAberto::DuplicataResolvida>{}; });
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(conteudo);
    opts.dialogTitle = matriz::i18n::t("duplicatas.resolvidas_titulo");
    opts.dialogBackgroundColour = tema().painel;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.componentToCentreAround = this;
    moldura::lancar(opts);
}

void DuplicatesWorkspaceComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    if (lblStatus_) {
        lblStatus_->setColour(juce::Label::textColourId, tk.textoSecundario);
        lblStatus_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    }
    if (lblScope_) {
        lblScope_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        lblScope_->setColour(juce::Label::textColourId, tk.textoSecundario);
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
        lblFileType_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        lblFileType_->setColour(juce::Label::textColourId, tk.textoSecundario);
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
        lblFileSize_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        lblFileSize_->setColour(juce::Label::textColourId, tk.textoSecundario);
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
        lblAno_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        lblAno_->setColour(juce::Label::textColourId, tk.textoSecundario);
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
    if (btnReset_) {
        btnReset_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnReset_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnReset_->setButtonText(matriz::i18n::t("duplicatas.btn_reset"));
        btnReset_->setTooltip(matriz::i18n::t("duplicatas.btn_reset_dica"));
    }
    auto atualizarToggle = [&](std::unique_ptr<juce::ToggleButton>& btn, const juce::String& chaveTexto, const juce::String& chaveDica) {
        if (btn) {
            btn->setButtonText(matriz::i18n::t(chaveTexto));
            btn->setTooltip(matriz::i18n::t(chaveDica));
            btn->setColour(juce::ToggleButton::textColourId, tk.textoSecundario);
            btn->setColour(juce::ToggleButton::tickColourId, tk.acento);
        }
    };
    atualizarToggle(tbSha256_, "duplicatas.match_sha256", "duplicatas.match_sha256_dica");
    atualizarToggle(tbPHash_, "duplicatas.match_visual", "duplicatas.match_visual_dica");
    atualizarToggle(tbNome_, "duplicatas.match_nome", "duplicatas.match_nome_dica");
    atualizarToggle(tbTamanho_, "duplicatas.match_tamanho", "duplicatas.match_tamanho_dica");
    atualizarToggle(tbData_, "duplicatas.match_data", "duplicatas.match_data_dica");
    atualizarToggle(tbDuracao_, "duplicatas.match_duracao", "duplicatas.match_duracao_dica");
    atualizarToggle(tbDimensoes_, "duplicatas.match_dimensoes", "duplicatas.match_dimensoes_dica");
    atualizarToggle(tbEspacoCor_, "duplicatas.match_espaco_cor", "duplicatas.match_espaco_cor_dica");
    atualizarToggle(tbFormato_, "duplicatas.match_formato", "duplicatas.match_formato_dica");

    if (listaComponent_) {
        listaComponent_->updateButtonsI18n();
        listaComponent_->repaint();
    }
    repaint();
}

void DuplicatesWorkspaceComponent::atualizarContagemResolvidas() {
    if (!btnResolvidas_) return;
    btnResolvidas_->setButtonText(matriz::i18n::t("duplicatas.btn_resolvidas")
                                      .replace("{n}", juce::String(projeto_.contarDuplicatasResolvidas())));
}

void DuplicatesWorkspaceComponent::recarregar() {
    atualizarContagemResolvidas();
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
    // A seleção vem do grid de verdade (aba METADATA) — antes vinha de projeto_.obterItensSelecionadosNoGrid(), que só o
    // MosaicoComponent antigo do MainComponent atualizava: com "Selected files" o scan recebia uma seleção vazia.
    activeFilters_.selecionadosNoGrid = fornecerSelecaoDoGrid ? fornecerSelecaoDoGrid() : projeto_.obterItensSelecionadosNoGrid();
    if (activeFilters_.scope == 2 && activeFilters_.selecionadosNoGrid.empty()) {
        const bool pt = matriz::i18n::localeAtivo().startsWith("pt");
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "DUPLICATES",
            pt ? juce::String::fromUTF8("Nenhum arquivo selecionado no grid. Selecione arquivos na aba METADATA ou escolha \"Todos\" no escopo.")
               : juce::String("No files are selected in the grid. Select files in the METADATA tab or choose \"All\" as the scope."));
        return;
    }
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

    activeFilters_.coincidirSha256 = tbSha256_ ? tbSha256_->getToggleState() : true;
    activeFilters_.coincidirPHash = tbPHash_ ? tbPHash_->getToggleState() : true;
    activeFilters_.coincidirNome = tbNome_ ? tbNome_->getToggleState() : false;
    activeFilters_.coincidirTamanho = tbTamanho_ ? tbTamanho_->getToggleState() : true;
    activeFilters_.coincidirData = tbData_ ? tbData_->getToggleState() : false;
    activeFilters_.coincidirDuracao = tbDuracao_ ? tbDuracao_->getToggleState() : true;
    activeFilters_.coincidirDimensoes = tbDimensoes_ ? tbDimensoes_->getToggleState() : false;
    activeFilters_.coincidirEspacoCor = tbEspacoCor_ ? tbEspacoCor_->getToggleState() : true;
    activeFilters_.coincidirFormato = tbFormato_ ? tbFormato_->getToggleState() : false;

    estado_ = State::Scanning;
    progressoScan_ = 0.0;
    achadosScan_ = 0;
    bytesScan_ = 0;
    gruposDetectados_.clear();
    btnScan_->setEnabled(false);
    btnScan_->setButtonText(matriz::i18n::t("duplicatas.scanning"));
    lblStatus_->setText(matriz::i18n::t("duplicatas.scanning"), juce::dontSendNotification);  // dá altura ao status antes do resized()
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
    faseScan_ = 1;

    // 1. Get all candidate items for duplicate detection
    struct ItemInfo {
        std::string criadoEm;
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
        std::string arquivoId;
        std::string sha256;
        std::string dataOriginal;  // EXIF DateTimeOriginal (só imagens), se houver
    };
    std::vector<ItemInfo> items;

    auto carregarItensDeDb = [&](matriz::db::Database& database, const juce::File& pastaProjeto, const std::string& colNome, const std::string& colCaminho) {
        try {
            auto stmt = database.prepare(
                "SELECT i.id, i.codigo_acervo, i.titulo, a.caminho_relativo, a.caracteristicas_tecnicas_json, a.tamanho_bytes, a.caminho_absoluto_origem, i.ano, "
                "       a.id, a.checksum_sha256, i.criado_em "
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
                info.arquivoId = stmt.columnText(8);
                info.sha256 = stmt.columnText(9);
                info.criadoEm = stmt.columnText(10);

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
                    if (obj->hasProperty("exifDataOriginal")) info.dataOriginal = obj->getProperty("exifDataOriginal").toString().toStdString();
                    if (auto* bruto = obj->getProperty("bruto").getDynamicObject()) {
                        if (auto* exif = bruto->getProperty("exif").getDynamicObject()) {
                            if (info.dataOriginal.empty() && exif->hasProperty("Exif.Photo.DateTimeOriginal"))
                                info.dataOriginal = exif->getProperty("Exif.Photo.DateTimeOriginal").toString().toStdString();
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

    struct PerfilCorImagem {
        bool ehMonocromatico = false;
        double chromaMedio = 0.0;
        double fracaoPixelsComCor = 0.0;
    };
    auto analisarPerfilCor = [](const juce::Image& img) -> PerfilCorImagem {
        if (img.isNull()) return {};
        if (img.getFormat() == juce::Image::SingleChannel) return { true, 0.0, 0.0 };
        const int w = img.getWidth(), h = img.getHeight();
        if (w <= 0 || h <= 0) return {};
        const int stepX = std::max(1, w / 64);
        const int stepY = std::max(1, h / 64);
        int totalAmostrado = 0;
        int pixelsComCor = 0;
        double somaChroma = 0.0;
        for (int y = 0; y < h; y += stepY) {
            for (int x = 0; x < w; x += stepX) {
                auto c = img.getPixelAt(x, y);
                int r = c.getRed(), g = c.getGreen(), b = c.getBlue();
                int chroma = std::max({r, g, b}) - std::min({r, g, b});
                somaChroma += chroma;
                if (chroma >= 18) pixelsComCor++;
                totalAmostrado++;
            }
        }
        if (totalAmostrado == 0) return {};
        double chromaMedio = somaChroma / totalAmostrado;
        double fracaoComCor = static_cast<double>(pixelsComCor) / totalAmostrado;
        bool mono = (fracaoComCor < 0.05 && chromaMedio < 7.5);
        return { mono, chromaMedio, fracaoComCor };
    };

    std::unordered_map<std::string, bool> monoDe;
    auto obterMonocromatico = [&](const std::string& itemId, const std::string& fullPath) -> bool {
        auto itM = monoDe.find(itemId);
        if (itM != monoDe.end()) return itM->second;
        juce::File f;
        if (auto mini = projeto_.caminhoMiniaturaPrincipal(itemId)) f = juce::File(*mini);
        if (!f.existsAsFile() && !fullPath.empty()) f = juce::File(juce::String::fromUTF8(fullPath.c_str()));
        bool mono = false;
        if (f.existsAsFile()) {
            juce::Image img = juce::ImageFileFormat::loadFrom(f);
            mono = analisarPerfilCor(img).ehMonocromatico;
        }
        monoDe[itemId] = mono;
        return mono;
    };

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

                if (activeFilters_.coincidirNome && !nomeIgual) continue;
                if (activeFilters_.coincidirFormato && !extIgual) continue;
                if (activeFilters_.coincidirTamanho && !tamanhoIgual) continue;
                if (activeFilters_.coincidirDuracao && (a.duracao > 0 || b.duracao > 0) && !duracaoIgual) continue;
                if (activeFilters_.coincidirDimensoes && (a.largura > 0 || b.largura > 0) && !dimIgual) continue;
                if (activeFilters_.coincidirEspacoCor) {
                    if (!a.colorSpace.empty() && !b.colorSpace.empty() && !colorSpaceIgual) continue;
                    if (obterMonocromatico(a.itemId, a.fullPath) != obterMonocromatico(b.itemId, b.fullPath)) continue;
                }
                if (activeFilters_.coincidirData && a.ano > 0 && b.ano > 0 && a.ano != b.ano) continue;
                if (activeFilters_.coincidirData && !a.dataOriginal.empty() && !b.dataOriginal.empty() && a.dataOriginal != b.dataOriginal) continue;

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
        // Com qualquer filtro ativo (escopo, tipo, tamanho, ano) o "original" também tem que passar pelos filtros:
        // só se comparam arquivos que estão dentro do que o usuário escolheu.
        const bool filtrosAtivos = activeFilters_.scope == 2 || activeFilters_.fileType > 1 || activeFilters_.sizeFilter > 1 ||
                                   activeFilters_.anoDe > 0 || activeFilters_.anoAte > 0;
        std::set<std::string> idsFiltrados;
        if (filtrosAtivos) for (const auto& it : items) idsFiltrados.insert(it.itemId);

        for (size_t i = 0; i < items.size(); ++i) {
            if (threadShouldExit()) return;
            progressoScan_ = static_cast<double>(i) / static_cast<double>(items.size());

            const auto& item = items[i];
            progressoScan_ = 0.2 * static_cast<double>(i) / static_cast<double>(items.size());  // fase A = 0..20%
            
            auto matchOpt = matriz::ingest::buscarAssetPorMetadados(
                db, item.titulo, item.ext, item.duracao, item.largura, item.altura, item.tamanhoBytes, item.caminhoRelativo, item.itemId,
                projeto_.projeto().pasta(), "", filtrosAtivos ? &idsFiltrados : nullptr);

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

                if (activeFilters_.coincidirNome && !group.duplicata.nomeCoincide) continue;
                if (activeFilters_.coincidirFormato && !group.duplicata.extCoincide) continue;
                if (activeFilters_.coincidirTamanho && !group.duplicata.tamanhoCoincide) continue;
                if (activeFilters_.coincidirDuracao && (group.duplicata.duracao > 0 || group.original.duracao > 0) && !group.duplicata.duracaoCoincide) continue;
                if (activeFilters_.coincidirDimensoes && (group.duplicata.largura > 0 || group.original.largura > 0) && !group.duplicata.dimCoincide) continue;
                if (activeFilters_.coincidirEspacoCor) {
                    if (!group.original.colorSpace.empty() && !group.duplicata.colorSpace.empty() && !group.duplicata.colorSpaceCoincide) continue;
                    if (obterMonocromatico(group.original.itemId, group.original.fullPath) != obterMonocromatico(group.duplicata.itemId, group.duplicata.fullPath)) continue;
                }

                gruposDetectados_.push_back(group);
            }
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Fase B (HD recuperado): arquivos recuperados costumam ter outro nome e alguns bytes a mais/menos, então os
    // critérios acima (tamanho idêntico etc.) não os pegam. Dois níveis novos, SEM trocar os de cima:
    //  - IDENTICAL: mesmo SHA-256 (independe de nome, extensão e caminho). Checksum que falta é calculado aqui,
    //    em background, e gravado em arquivo.checksum_sha256 (só o que falta nos próximos scans).
    //  - PROBABLE (só imagens): pHash parecido (limiar de Duplicata.h). pHash só é calculado AQUI (SCAN), nunca no
    //    ingest, e fica em phash_imagem (indice.sqlite): os próximos scans só calculam o que falta.
    // ---------------------------------------------------------------------------------------------
    if (!isCatalogMode) {
        // Contador ao vivo: parte do que a fase A já achou e soma cada par novo (B2, B3, B4).
        achadosScan_ = static_cast<int>(gruposDetectados_.size());
        {
            juce::int64 bytesIniciais = 0;
            for (const auto& g : gruposDetectados_) bytesIniciais += g.duplicata.tamanhoBytes;
            bytesScan_ = bytesIniciais;
        }
        auto contarAchado = [this](const DuplicateGroup& g) {
            ++achadosScan_;
            bytesScan_ += g.duplicata.tamanhoBytes;
        };
        auto gruposParaPar = [&](const ItemInfo& a, const ItemInfo& b) {
            DuplicateGroup group;
            auto preencher = [](DuplicateMatch& m, const ItemInfo& it) {
                m.criadoEm = it.criadoEm;
                m.itemId = it.itemId; m.codigoAcervo = it.codigoAcervo; m.titulo = it.titulo; m.ext = it.ext;
                m.duracao = it.duracao; m.largura = it.largura; m.altura = it.altura; m.lufs = it.lufs;
                m.tamanhoBytes = it.tamanhoBytes; m.orientation = it.orientation; m.colorSpace = it.colorSpace;
                m.caminhoRelativo = it.caminhoRelativo; m.fullPath = it.fullPath;
                m.collectionNome = it.collectionNome; m.collectionCaminho = it.collectionCaminho;
            };
            preencher(group.original, a);
            preencher(group.duplicata, b);
            const bool nomeIgual = juce::String(a.titulo).equalsIgnoreCase(juce::String(b.titulo));
            const bool extIgual = (a.ext == b.ext);
            const bool duracaoIgual = (a.duracao > 0 && b.duracao > 0 && std::abs(a.duracao - b.duracao) < 0.1);
            const bool dimIgual = (a.largura > 0 && b.largura > 0 && a.largura == b.largura && a.altura == b.altura);
            const bool tamanhoIgual = (a.tamanhoBytes > 0 && a.tamanhoBytes == b.tamanhoBytes);
            const bool lufsIgual = (a.lufs != 0.0 && b.lufs != 0.0 && std::abs(a.lufs - b.lufs) < 0.1);
            group.original.nomeCoincide = group.duplicata.nomeCoincide = nomeIgual;
            group.original.extCoincide = group.duplicata.extCoincide = extIgual;
            group.original.duracaoCoincide = group.duplicata.duracaoCoincide = duracaoIgual;
            group.original.dimCoincide = group.duplicata.dimCoincide = dimIgual;
            group.original.tamanhoCoincide = group.duplicata.tamanhoCoincide = tamanhoIgual;
            group.original.orientationCoincide = group.duplicata.orientationCoincide = (!a.orientation.empty() && a.orientation == b.orientation);
            group.original.colorSpaceCoincide = group.duplicata.colorSpaceCoincide = (!a.colorSpace.empty() && a.colorSpace == b.colorSpace);
            group.original.lufsCoincide = group.duplicata.lufsCoincide = lufsIgual;
            return group;
        };
        // Original = o de menor id (mesma convenção do critério de sempre).
        auto ordenado = [](const ItemInfo& x, const ItemInfo& y) { return x.itemId < y.itemId ? std::make_pair(&x, &y) : std::make_pair(&y, &x); };
        auto chavePar = [](const std::string& a, const std::string& b) { return a < b ? a + "|" + b : b + "|" + a; };
        // "2015:04:05 11:18:36" ou "2015-04-05T11:18:36": segundos desde 2000 (só para diferenças); -1 se não der para ler.
        auto segundosExif = [](const std::string& t) -> long long {
            std::string d;
            for (char c : t) if (c >= '0' && c <= '9') d += c;
            if (d.size() < 14) return -1;
            const int ano = std::stoi(d.substr(0, 4)), mes = std::stoi(d.substr(4, 2)), dia = std::stoi(d.substr(6, 2));
            const int hh = std::stoi(d.substr(8, 2)), mm = std::stoi(d.substr(10, 2)), ss = std::stoi(d.substr(12, 2));
            const long long dias = static_cast<long long>(ano) * 372 + (mes - 1) * 31 + (dia - 1);  // só precisa ser monotônico por dia
            return dias * 86400 + hh * 3600 + mm * 60 + ss;
        };

        std::unordered_map<std::string, size_t> grupoExistente;  // par -> índice em gruposDetectados_ (fase A)
        for (size_t g = 0; g < gruposDetectados_.size(); ++g)
            grupoExistente[chavePar(gruposDetectados_[g].original.itemId, gruposDetectados_[g].duplicata.itemId)] = g;

        std::unordered_set<std::string> paresIdenticos;
        std::unordered_set<std::string> paresVistos;
        if (activeFilters_.coincidirSha256) {
            // ---- B1: checksums que faltam (em background, em blocos de 100 por transação)
            faseScan_ = 2;
            std::vector<std::pair<std::string, std::pair<std::string, std::string>>> paraGravar;  // arquivoId -> (md5, sha256)
            auto gravarChecksums = [&]() {
                if (paraGravar.empty()) return;
                try {
                    db.exec("BEGIN IMMEDIATE");
                    try {
                        const auto agora = matriz::model::agoraIso8601();
                        // O gatilho arquivo_master_travado proíbe mexer em checksum de master. Aqui só PREENCHEMOS um checksum
                        // que nunca foi calculado (nunca trocamos um existente — a cláusula WHERE garante), com o mesmo
                        // mecanismo da reconciliação de Vault, dentro da transação (a marca some antes do COMMIT).
                        db.run("INSERT OR IGNORE INTO reconciliacao_em_curso (marca) VALUES (1)", {});
                        for (const auto& [arqId, par] : paraGravar)
                            db.run("UPDATE arquivo SET checksum_md5 = ?, checksum_sha256 = ?, checksum_gerado_em = ? "
                                   "WHERE id = ? AND (checksum_sha256 IS NULL OR checksum_sha256 = '')",
                                   {matriz::db::Value::of(par.first), matriz::db::Value::of(par.second), matriz::db::Value::of(agora),
                                    matriz::db::Value::of(arqId)});
                        db.run("DELETE FROM reconciliacao_em_curso", {});
                        db.exec("COMMIT");
                    } catch (...) { try { db.exec("ROLLBACK"); } catch (...) {} }
                } catch (...) {}
                paraGravar.clear();
            };
            size_t semChecksum = 0;
            for (const auto& it : items) if (it.sha256.empty() && !it.arquivoId.empty()) ++semChecksum;
            size_t feitos = 0;
            for (auto& it : items) {
                if (threadShouldExit()) { gravarChecksums(); return; }
                if (!it.sha256.empty() || it.arquivoId.empty() || it.fullPath.empty()) continue;
                progressoScan_ = 0.2 + 0.4 * static_cast<double>(feitos) / static_cast<double>(std::max<size_t>(1, semChecksum));  // 20..60%
                ++feitos;
                const juce::File f(juce::String::fromUTF8(it.fullPath.c_str()));
                if (!f.existsAsFile()) continue;
                try {
                    auto cs = matriz::ingest::calcularChecksums(f);
                    it.sha256 = cs.sha256;
                    paraGravar.push_back({it.arquivoId, {cs.md5, cs.sha256}});
                    if (paraGravar.size() >= 100) gravarChecksums();
                } catch (...) {}
            }
            gravarChecksums();

            // ---- B2: IDENTICAL — mesmo SHA-256
            std::unordered_map<std::string, std::vector<size_t>> porSha;
            for (size_t i = 0; i < items.size(); ++i)
                if (!items[i].sha256.empty()) porSha[items[i].sha256].push_back(i);
            for (auto& [sha, idxs] : porSha) {
                if (idxs.size() < 2) continue;
                std::sort(idxs.begin(), idxs.end(), [&](size_t x, size_t y) { return items[x].itemId < items[y].itemId; });
                for (size_t k = 1; k < idxs.size(); ++k) {
                    const auto& a = items[idxs[0]];
                    const auto& b = items[idxs[k]];
                    if (activeFilters_.coincidirNome && !juce::String(a.titulo).equalsIgnoreCase(juce::String(b.titulo))) continue;
                    if (activeFilters_.coincidirFormato && a.ext != b.ext) continue;
                    if (activeFilters_.coincidirData && !a.dataOriginal.empty() && !b.dataOriginal.empty() && a.dataOriginal != b.dataOriginal) continue;
                    if (activeFilters_.coincidirData && a.ano > 0 && b.ano > 0 && a.ano != b.ano) continue;
                    const auto chave = chavePar(a.itemId, b.itemId);
                    paresIdenticos.insert(chave);
                    auto ex = grupoExistente.find(chave);
                    if (ex != grupoExistente.end()) {   // já pego por outro critério: ganha o rótulo IDENTICAL
                        gruposDetectados_[ex->second].criterio = "IDENTICAL";
                        continue;
                    }
                    auto [o, d] = ordenado(a, b);
                    auto grupo = gruposParaPar(*o, *d);
                    grupo.criterio = "IDENTICAL";
                    grupoExistente[chave] = gruposDetectados_.size();
                    contarAchado(grupo);
                    gruposDetectados_.push_back(std::move(grupo));
                }
            }

            // ---- B2b: selo "pasta inteira duplicada". Pasta X está inteira em Y quando TODO arquivo master de X
            // (conferido contra o banco, não só contra os itens deste scan) tem cópia idêntica em Y. Conservador: filtro
            // ativo, item já resolvido ou arquivo sem checksum fazem a conta não fechar, e aí não há selo.
            {
                auto pastaDe = [](const std::string& fullPath) {
                    return fullPath.empty() ? std::string()
                                            : juce::File(juce::String::fromUTF8(fullPath.c_str())).getParentDirectory().getFullPathName().toStdString();
                };
                std::unordered_map<std::string, int> totalPorPasta;  // masters do projeto por pasta, sem filtro nenhum
                try {
                    const auto pastaProj = projeto_.projeto().pasta();
                    auto st = db.prepare("SELECT caminho_relativo, caminho_absoluto_origem FROM arquivo WHERE eh_master = 1");
                    while (st.step()) {
                        if (threadShouldExit()) return;
                        const std::string abs = st.columnText(1);
                        const std::string caminho = !abs.empty() ? abs
                            : pastaProj.getChildFile(juce::String::fromUTF8(st.columnText(0).c_str())).getFullPathName().toStdString();
                        const auto pasta = pastaDe(caminho);
                        if (!pasta.empty()) ++totalPorPasta[pasta];
                    }
                } catch (...) { totalPorPasta.clear(); }

                std::unordered_map<std::string, int> comCopiaEm;  // "X\nY" -> arquivos de X com cópia idêntica em Y
                for (const auto& [sha, idxs] : porSha) {
                    if (idxs.size() < 2) continue;
                    std::vector<std::string> pastas;
                    pastas.reserve(idxs.size());
                    for (auto i : idxs) pastas.push_back(pastaDe(items[i].fullPath));
                    std::vector<std::string> distintas;
                    for (const auto& p : pastas) if (!p.empty()) distintas.push_back(p);
                    std::sort(distintas.begin(), distintas.end());
                    distintas.erase(std::unique(distintas.begin(), distintas.end()), distintas.end());
                    if (distintas.size() < 2 || distintas.size() > 20) continue;  // cópia espalhada demais: sem selo
                    for (size_t k = 0; k < idxs.size(); ++k)
                        if (!pastas[k].empty())
                            for (const auto& y : distintas)
                                if (y != pastas[k]) ++comCopiaEm[pastas[k] + "\n" + y];
                }

                auto estaInteiraEm = [&](const std::string& x, const std::string& y) {
                    const auto t = totalPorPasta.find(x);
                    const auto c = comCopiaEm.find(x + "\n" + y);
                    return t != totalPorPasta.end() && c != comCopiaEm.end() && t->second >= 2 && c->second == t->second;
                };
                for (auto& g : gruposDetectados_) {
                    if (g.criterio != "IDENTICAL") continue;
                    const auto pastaOrig = pastaDe(g.original.fullPath), pastaDup = pastaDe(g.duplicata.fullPath);
                    if (pastaOrig.empty() || pastaDup.empty() || pastaOrig == pastaDup) continue;
                    const bool dupEmOrig = estaInteiraEm(pastaDup, pastaOrig);
                    const bool origEmDup = estaInteiraEm(pastaOrig, pastaDup);
                    if (!dupEmOrig && !origEmDup) continue;
                    const auto& coberta = dupEmOrig ? pastaDup : pastaOrig;
                    const auto& em = dupEmOrig ? pastaOrig : pastaDup;
                    g.pastaArquivos = totalPorPasta[coberta];
                    g.pastaNome = juce::File(juce::String::fromUTF8(coberta.c_str())).getFileName().toStdString();
                    g.pastaEmNome = juce::File(juce::String::fromUTF8(em.c_str())).getFileName().toStdString();
                    g.pastaMesmoConteudo = dupEmOrig && origEmDup && totalPorPasta[pastaOrig] == totalPorPasta[pastaDup];
                }
            }
        }

        // ---- B3: PROBABLE — similaridade visual de imagens (pHash), com cancelamento e progresso
        if (activeFilters_.coincidirPHash) {
            faseScan_ = 3;
            std::vector<size_t> imagens;
            for (size_t i = 0; i < items.size(); ++i)
                if (matriz::ingest::categoriaPorExtensao(items[i].ext) == matriz::ingest::CategoriaMidia::Imagem && !items[i].arquivoId.empty())
                    imagens.push_back(i);
            std::unordered_map<std::string, uint64_t> phashDe;  // arquivo_id -> hash
        auto& indice = projeto_.projeto().indice();
        try {
            auto st = indice.prepare("SELECT arquivo_id, hash FROM phash_imagem");
            while (st.step()) {
                try { phashDe[st.columnText(0)] = std::stoull(st.columnText(1)); } catch (...) {}
            }
        } catch (...) {}
        std::vector<std::pair<size_t, uint64_t>> novos;  // (índice do item, hash) ainda não gravados
        auto gravarPHashes = [&]() {
            if (novos.empty()) return;
            try {
                indice.exec("BEGIN IMMEDIATE");
                try {
                    const auto agora = matriz::model::agoraIso8601();
                    for (const auto& [idx, h] : novos)
                        indice.run("INSERT INTO phash_imagem (id, item_id, arquivo_id, hash, processado_em) VALUES (?, ?, ?, ?, ?)",
                                   {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(items[idx].itemId),
                                    matriz::db::Value::of(items[idx].arquivoId), matriz::db::Value::of(std::to_string(h)),
                                    matriz::db::Value::of(agora)});
                    indice.exec("COMMIT");
                } catch (...) { try { indice.exec("ROLLBACK"); } catch (...) {} }
            } catch (...) {}
            novos.clear();
        };
        size_t faltam = 0;
        for (auto i : imagens) if (!phashDe.count(items[i].arquivoId)) ++faltam;
        size_t calculados = 0;
        for (auto i : imagens) {
            if (threadShouldExit()) { gravarPHashes(); return; }
            if (phashDe.count(items[i].arquivoId)) continue;
            progressoScan_ = 0.6 + 0.3 * static_cast<double>(calculados) / static_cast<double>(std::max<size_t>(1, faltam));  // 60..90%
            ++calculados;
            // Usa a MINIATURA do projeto (pequena, decodifica em ms) em vez de decodificar o original inteiro (foto de
            // vários MB): o pHash redimensiona para 32x32 de qualquer jeito. Sem miniatura: cai no original.
            juce::File f;
            if (auto mini = projeto_.caminhoMiniaturaPrincipal(items[i].itemId)) f = juce::File(*mini);
            if (!f.existsAsFile()) f = juce::File(juce::String::fromUTF8(items[i].fullPath.c_str()));
            if (!f.existsAsFile()) continue;
            try {
                const uint64_t h = matriz::ingest::calcularPHashImagem(f, juce::File());
                phashDe[items[i].arquivoId] = h;
                novos.push_back({i, h});
                if (novos.size() >= 100) gravarPHashes();
            } catch (...) {}  // formato que o decodificador não lê: fica de fora do nível PROBABLE
        }
        gravarPHashes();

        faseScan_ = 4;
        // Comparação SEM varrer todos contra todos: 64 bits em 11 faixas (princípio da casa dos pombos: dois hashes a
        // distância <= 10 coincidem exatamente em ao menos uma das 11 faixas) — só se comparam os que caem na mesma faixa.
        constexpr int kLimiar = 10;
        struct Cand { size_t idx; uint64_t h; };
        std::vector<Cand> cands;
        for (auto i : imagens) {
            auto it = phashDe.find(items[i].arquivoId);
            if (it != phashDe.end()) cands.push_back({i, it->second});
        }
        static const int kLarg[11] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5};
        for (int faixa = 0, desloc = 0; faixa < 11; desloc += kLarg[faixa], ++faixa) {
            if (threadShouldExit()) return;
            progressoScan_ = 0.9 + 0.1 * static_cast<double>(faixa) / 11.0;
            std::unordered_map<uint32_t, std::vector<size_t>> baldes;
            const uint64_t mascara = (uint64_t(1) << kLarg[faixa]) - 1;
            for (size_t c = 0; c < cands.size(); ++c)
                baldes[static_cast<uint32_t>((cands[c].h >> desloc) & mascara)].push_back(c);
            for (const auto& [chaveBalde, membros] : baldes) {
                for (size_t x = 0; x < membros.size(); ++x) {
                    if ((x & 0x3ff) == 0 && threadShouldExit()) return;
                    for (size_t y = x + 1; y < membros.size(); ++y) {
                        const auto& ca = cands[membros[x]];
                        const auto& cb = cands[membros[y]];
                        const int dist = matriz::ingest::distanciaHamming(ca.h, cb.h);
                        if (dist > kLimiar) continue;
                        const auto& a = items[ca.idx];
                        const auto& b = items[cb.idx];
                        const auto chave = chavePar(a.itemId, b.itemId);
                        if (paresIdenticos.count(chave) || grupoExistente.count(chave) || !paresVistos.insert(chave).second) continue;
                        // Mesmo SHA-256 = IDENTICAL, nunca PROBABLE (com 3+ cópias o par B-C não está no "em estrela" acima).
                        if (!a.sha256.empty() && a.sha256 == b.sha256) continue;

                        if (activeFilters_.coincidirNome && !juce::String(a.titulo).equalsIgnoreCase(juce::String(b.titulo))) continue;
                        if (activeFilters_.coincidirFormato && a.ext != b.ext) continue;
                        if (activeFilters_.coincidirTamanho && a.tamanhoBytes > 0 && b.tamanhoBytes > 0 && a.tamanhoBytes != b.tamanhoBytes) continue;
                        if (activeFilters_.coincidirData && !a.dataOriginal.empty() && !b.dataOriginal.empty() && a.dataOriginal != b.dataOriginal) continue;
                        if (activeFilters_.coincidirData && a.ano > 0 && b.ano > 0 && a.ano != b.ano) continue;
                        if (activeFilters_.coincidirDimensoes && a.largura > 0 && b.largura > 0 && (a.largura != b.largura || a.altura != b.altura)) continue;
                        if (activeFilters_.coincidirEspacoCor) {
                            if (!a.colorSpace.empty() && !b.colorSpace.empty() && a.colorSpace != b.colorSpace) continue;
                            if (obterMonocromatico(a.itemId, a.fullPath) != obterMonocromatico(b.itemId, b.fullPath)) continue;
                        }

                        // EXIF DateTimeOriginal diferente:
                        //  - até 30 s de diferença e parecidas (pHash <= 10): foto em SEQUÊNCIA -> SIMILAR (decisão manual);
                        //  - mais de 30 s: outra foto teria pHash bem diferente; se o pHash é quase igual (<= 4) é a mesma
                        //    imagem com horário diferente (ex.: cópia editada) -> PROBABLE; senão, descarta.
                        bool similar = false;
                        if (!a.dataOriginal.empty() && !b.dataOriginal.empty() && a.dataOriginal != b.dataOriginal) {
                            const auto sa = segundosExif(a.dataOriginal), sb = segundosExif(b.dataOriginal);
                            if (sa < 0 || sb < 0) continue;
                            if (std::llabs(sa - sb) <= 30) {
                                if (dist > 10) continue;
                                similar = true;
                            } else if (dist > 4) {
                                continue;
                            }
                        }
                        auto [o, d] = ordenado(a, b);
                        auto grupo = gruposParaPar(*o, *d);
                        grupo.criterio = similar ? "SIMILAR" : "PROBABLE";
                        grupo.distanciaPHash = dist;
                        contarAchado(grupo);
                        gruposDetectados_.push_back(std::move(grupo));
                    }
                }
            }
        }
        // PROVÁVEIS e depois SIMILARES vão ao fim (categorias separadas na lista).
        std::stable_partition(gruposDetectados_.begin(), gruposDetectados_.end(),
                              [](const DuplicateGroup& g) { return g.criterio != "PROBABLE" && g.criterio != "SIMILAR"; });
        std::stable_partition(std::find_if(gruposDetectados_.begin(), gruposDetectados_.end(),
                                           [](const DuplicateGroup& g) { return g.criterio == "PROBABLE" || g.criterio == "SIMILAR"; }),
                              gruposDetectados_.end(), [](const DuplicateGroup& g) { return g.criterio == "PROBABLE"; });
        }

        // ---- B4: Mesmo nome de arquivo (quando coincidirNome está ativo)
        if (activeFilters_.coincidirNome) {
            std::unordered_map<std::string, std::vector<size_t>> porNome;
            for (size_t i = 0; i < items.size(); ++i) {
                if (!items[i].titulo.empty()) {
                    porNome[juce::String(items[i].titulo).toLowerCase().toStdString()].push_back(i);
                }
            }
            for (auto& [nome, idxs] : porNome) {
                if (idxs.size() < 2) continue;
                for (size_t x = 0; x < idxs.size(); ++x) {
                    for (size_t y = x + 1; y < idxs.size(); ++y) {
                        const auto& a = items[idxs[x]];
                        const auto& b = items[idxs[y]];
                        const auto chave = chavePar(a.itemId, b.itemId);
                        if (paresIdenticos.count(chave) || grupoExistente.count(chave) || !paresVistos.insert(chave).second) continue;
                        if (activeFilters_.coincidirTamanho && a.tamanhoBytes > 0 && b.tamanhoBytes > 0 && a.tamanhoBytes != b.tamanhoBytes) continue;
                        if (activeFilters_.coincidirFormato && a.ext != b.ext) continue;
                        if (activeFilters_.coincidirData && !a.dataOriginal.empty() && !b.dataOriginal.empty() && a.dataOriginal != b.dataOriginal) continue;
                        if (activeFilters_.coincidirData && a.ano > 0 && b.ano > 0 && a.ano != b.ano) continue;
                        if (activeFilters_.coincidirDimensoes && a.largura > 0 && b.largura > 0 && (a.largura != b.largura || a.altura != b.altura)) continue;
                        if (activeFilters_.coincidirDuracao && (a.duracao > 0 || b.duracao > 0) && std::abs(a.duracao - b.duracao) >= 0.1) continue;
                        if (activeFilters_.coincidirEspacoCor) {
                            if (!a.colorSpace.empty() && !b.colorSpace.empty() && a.colorSpace != b.colorSpace) continue;
                            if (obterMonocromatico(a.itemId, a.fullPath) != obterMonocromatico(b.itemId, b.fullPath)) continue;
                        }
                        auto [o, d] = ordenado(a, b);
                        auto grupo = gruposParaPar(*o, *d);
                        grupo.criterio = "NAME_MATCH";
                        grupoExistente[chave] = gruposDetectados_.size();
                        contarAchado(grupo);
                        gruposDetectados_.push_back(std::move(grupo));
                    }
                }
            }
        }
    }

    // Numeração consistente em todos os pares do mesmo conjunto relacionado.
    // Não cria pares transitivos: apenas identifica os arquivos já encontrados.
    std::map<std::string, std::string> datas;
    for (const auto& item : items) datas[item.collectionCaminho + "\n" + item.itemId] = item.criadoEm;
    for (auto& grupo : gruposDetectados_) for (auto* m : {&grupo.original, &grupo.duplicata}) {
        const auto data = datas.find(m->collectionCaminho + "\n" + m->itemId);
        if (data != datas.end()) m->criadoEm = data->second;
        else if (m->collectionCaminho.empty()) {
            auto st = db.prepare("SELECT criado_em FROM item WHERE id = ?");
            st.bind(1, matriz::db::Value::of(m->itemId));
            if (st.step()) m->criadoEm = st.columnText(0);
        }
    }
    std::map<std::string, std::string> raiz;
    auto chaveMembro = [](const DuplicateMatch& m) { return m.collectionCaminho + "\n" + m.itemId; };
    auto achar = [&](std::string id) {
        while (raiz.at(id) != id) id = raiz.at(id);
        return id;
    };
    std::map<std::string, DuplicateMatch*> membros;
    for (auto& grupo : gruposDetectados_) {
        for (auto* m : {&grupo.original, &grupo.duplicata}) {
            const auto id = chaveMembro(*m);
            raiz.emplace(id, id);
            membros.emplace(id, m);
        }
        raiz[achar(chaveMembro(grupo.duplicata))] = achar(chaveMembro(grupo.original));
    }
    std::map<std::string, std::vector<std::string>> conjuntos;
    for (const auto& [id, m] : membros) conjuntos[achar(id)].push_back(id);
    std::map<std::string, std::pair<int, int>> numeracao;
    for (auto& [raizId, ids] : conjuntos) {
        std::sort(ids.begin(), ids.end(), [&](const auto& a, const auto& b) {
            if (membros[a]->criadoEm != membros[b]->criadoEm) return membros[a]->criadoEm < membros[b]->criadoEm;
            return a < b;
        });
        for (size_t i = 0; i < ids.size(); ++i) numeracao[ids[i]] = {static_cast<int>(i + 1), static_cast<int>(ids.size())};
    }
    for (auto& grupo : gruposDetectados_) for (auto* m : {&grupo.original, &grupo.duplicata}) {
        const auto n = numeracao.at(chaveMembro(*m));
        m->ordemChegada = n.first;
        m->totalRelacionados = n.second;
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
            static const char* kFases[] = {"Scanning", "Matching files", "Computing checksums", "Computing image fingerprints (pHash)", "Comparing images"};
            const juce::String fase = kFases[juce::jlimit(0, 4, faseScan_.load())];
            juce::String achados;
            if (achadosScan_.load() > 0)
                achados = " · " + matriz::i18n::t("duplicatas.achadas_ate_agora")
                                           .replace("{n}", juce::String(achadosScan_.load()))
                                           .replace("{size}", juce::File::descriptionOfSizeInBytes(bytesScan_.load()));
            lblStatus_->setText(fase + ": " + juce::String(static_cast<int>(progressoScan_ * 100)) + "% completed..." + achados,
                                juce::dontSendNotification);
            repaint();  // barra de progresso da área direita
            ProgressoGlobal::obterInstancia().atualizarFracao(
                "duplicates_scan", progressoScan_,
                fase + ": " + juce::String(static_cast<int>(progressoScan_ * 100)) + "%");
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
    auto descricaoLado = [](const juce::String& rotulo, const DuplicateMatch& m) {
        juce::String t = rotulo + ": " + juce::String::fromUTF8(m.titulo.c_str());
        if (m.largura > 0 && m.altura > 0) t += "   (" + juce::String(m.largura) + " x " + juce::String(m.altura) + " px)";
        return t;
    };
    const juce::String mensagem = isPt
        ? (juce::String::fromUTF8("Como você deseja tratar este par de duplicatas?\n\n") +
           descricaoLado(juce::String("Arquivo 1 (Original)"), group.original) + "\n" +
           descricaoLado(juce::String("Arquivo 2 (Duplicata)"), group.duplicata))
        : ("How would you like to handle this duplicate pair?\n\n" + descricaoLado(juce::String("File 1 (Original)"), group.original) + "\n" +
           descricaoLado(juce::String("File 2 (Duplicate)"), group.duplicata));
    // Códigos: 1 = manter 1, 2 = manter 2, 3 = ambos, 5 = mais recente, 6 = primeira no backup, 7 = maior resolução.
    std::vector<DialogoEscolhaDuplicata::Botao> botoes = {
        {matriz::i18n::t("duplicatas.action_keep1"), 1},
        {matriz::i18n::t("duplicatas.action_keep2"), 2},
        {matriz::i18n::t("duplicatas.action_keep_both"), 3},
        {matriz::i18n::t("duplicatas.criterio_recente"), 5},
        {matriz::i18n::t("duplicatas.criterio_backup"), 6},
        {matriz::i18n::t("duplicatas.criterio_resolucao"), 7},
    };

    // safeThis: o diálogo não impede o componente de ser destruído enquanto está aberto (ex.: trocar de aba).
    juce::Component::SafePointer<DuplicatesWorkspaceComponent> safeThis(this);
    DialogoEscolhaDuplicata::exibir(
        isPt ? juce::String::fromUTF8("Resolver Correspondência de Duplicata") : juce::String("Resolve Duplicate Match"),
        mensagem, std::move(botoes), isPt ? juce::String::fromUTF8("VOLTAR") : juce::String("RETURN"),
        [safeThis, grupoIdx, group, isPt](int buttonResult) mutable {
        if (!safeThis) return;
        if (buttonResult == 7) {  // maior resolução: mantém o lado com mais pixels
            const long long ao = static_cast<long long>(group.original.largura) * group.original.altura;
            const long long ad = static_cast<long long>(group.duplicata.largura) * group.duplicata.altura;
            if (ao <= 0 || ad <= 0 || ao == ad) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, matriz::i18n::t("duplicatas.criterios"),
                                                       matriz::i18n::t("duplicatas.criterio_nao_aplica"), {}, nullptr,
                                                       juce::ModalCallbackFunction::create([](int) {}));
                return;
            }
            buttonResult = ao > ad ? 1 : 2;
        }
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

        // Fase 4: tudo em background, numa transação, com Undo. Sanitizar: o
        // lado não escolhido fica no SOURCE e no catálogo (estado
        // 'duplicata'), só não entra no MAIN — nada é apagado; a ficha dele é
        // somada à do mantido.
        const std::string idOriginal = group.original.itemId, idDuplicata = group.duplicata.itemId;
        auto aoResolver = [safeThis, idOriginal, idDuplicata, isPt](bool ok, std::vector<ProjetoAberto::ResultadoSanitizacao> resultados) {
            if (!safeThis) return;
            if (!ok) { avisarFalhaResolucao(); return; }
            publicarSanitizacoes(safeThis->projeto_, resultados, {idOriginal, idDuplicata});
            // Pelo par (não pelo índice): a lista pode ter mudado enquanto isto rodava.
            auto& grupos = safeThis->gruposDetectados_;
            auto it = std::find_if(grupos.begin(), grupos.end(), [&](const DuplicateGroup& g) {
                return g.original.itemId == idOriginal && g.duplicata.itemId == idDuplicata;
            });
            if (it != grupos.end()) {
                grupos.erase(it);

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
        };
        if (buttonResult == 3) {  // Keep Both — os dois continuam entrando no backup, nada é juntado
            const juce::String codigoOriginal(group.original.codigoAcervo);
            safeThis->projeto_.resolverDuplicatasEmSegundoPlano(
                {idDuplicata},
                [idDuplicata, codigoOriginal](matriz::db::Database& db) {
                    acrescentarNota(db, idDuplicata,
                                    "Validated as a known duplicate pair of " + codigoOriginal +
                                    juce::String::fromUTF8(" by user — both sides kept in Make Backup. [USER_VERIFIED_DUPLICATE_KEEP_BOTH]"));
                    return std::vector<ProjetoAberto::ResultadoSanitizacao>{};
                },
                aoResolver);
            return;
        }
        const bool manterOriginal = buttonResult == 1;
        const std::string manter = manterOriginal ? idOriginal : idDuplicata;
        const std::string descartar = manterOriginal ? idDuplicata : idOriginal;
        const juce::String codigoManter(manterOriginal ? group.original.codigoAcervo : group.duplicata.codigoAcervo);
        const juce::String codigoDescartar(manterOriginal ? group.duplicata.codigoAcervo : group.original.codigoAcervo);
        auto juntar = [safeThis, manter, descartar, aoResolver](std::set<std::string> usarDescartado) {
            if (!safeThis) return;
            safeThis->projeto_.resolverDuplicatasEmSegundoPlano(
                {manter, descartar},
                [manter, descartar, usarDescartado](matriz::db::Database& db) {
                    return std::vector<ProjetoAberto::ResultadoSanitizacao>{
                        ProjetoAberto::sanitizarDuplicata(db, manter, descartar, usarDescartado)};
                },
                aoResolver);
        };
        // Conflito de verdade: mostra os dois valores lado a lado, o do
        // mantido marcado; sem conflito, junta direto.
        safeThis->projeto_.simularJuncaoEmSegundoPlano(
            manter, descartar,
            [juntar, codigoManter, codigoDescartar](std::vector<matriz::model::merge::Conflito> conflitos) {
                if (conflitos.empty()) {
                    juntar({});
                    return;
                }
                std::vector<ConflitosMergeDialog::Linha> linhas;
                for (const auto& c : conflitos)
                    linhas.push_back({c.campo, c.campo, matriz::model::merge::resumoDoValor(c.campo, c.valorMantido),
                                      matriz::model::merge::resumoDoValor(c.campo, c.valorDescartado)});
                ConflitosMergeDialog::mostrar(
                    matriz::i18n::t("merge.resolver_titulo"), matriz::i18n::t("merge.resolver_intro"),
                    matriz::i18n::t("merge.col_mantido").replace("{codigo}", codigoManter),
                    matriz::i18n::t("merge.col_descartado").replace("{codigo}", codigoDescartar), std::move(linhas),
                    [juntar](bool confirmado, std::set<std::string> trocados) {
                        if (confirmado) juntar(std::move(trocados));
                    });
            });
    });
}

void DuplicatesWorkspaceComponent::resetarAba() {
    if (estado_ != State::Results) return;
    // Nada foi gravado pela detecção em si: só o que o usuário já resolveu fica no banco. Os pares que estavam na
    // lista voltam a ser "não resolvidos" e reaparecem num próximo SCAN.
    gruposDetectados_.clear();
    if (listaComponent_) listaComponent_->updateList(gruposDetectados_);
    estado_ = State::Idle;
    lblStatus_->setText("", juce::dontSendNotification);
    if (viewport_) viewport_->setVisible(false);
    atualizarBotoesSelecionados();
    resized();
    repaint();
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
                if (group.criterio == "PROBABLE" || group.criterio == "SIMILAR") continue;  // só decisão manual
                acrescentarNota(db, group.duplicata.itemId,
                                "Dismissed as duplicate by user in batch. [USER_VERIFIED_NOT_DUPLICATE]");
            }
            db.run("COMMIT", {});
        } catch (...) {
            try { db.run("ROLLBACK", {}); } catch (...) {}
        }

        gruposDetectados_.erase(std::remove_if(gruposDetectados_.begin(), gruposDetectados_.end(),
                                               [](const DuplicateGroup& g) { return g.criterio != "PROBABLE" && g.criterio != "SIMILAR"; }),
                                gruposDetectados_.end());
        if (!gruposDetectados_.empty()) {  // sobraram só PROVÁVEIS: continuam na lista
            listaComponent_->updateList(gruposDetectados_);
            lblStatus_->setText(textoManuais(gruposDetectados_), juce::dontSendNotification);
            resized();
            repaint();
            return;
        }
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
           juce::String::fromUTF8(" detected duplicate groups. Nothing is deleted from disk or the catalog — the discarded side just "
           "won't be included in the next Make Backup run."));

    // Grade simétrica de botões (3 colunas). Códigos de aplicarEscolhaGlobal: 1/2/3 manter, 4 mais recente, 5 primeira no
    // backup, 6 maior resolução.
    std::vector<DialogoEscolhaDuplicata::Botao> botoes = {
        {matriz::i18n::t("duplicatas.action_keep1"), 1},
        {matriz::i18n::t("duplicatas.action_keep2"), 2},
        {matriz::i18n::t("duplicatas.action_keep_both"), 3},
        {matriz::i18n::t("duplicatas.criterio_recente"), 4},
        {matriz::i18n::t("duplicatas.criterio_backup"), 5},
        {matriz::i18n::t("duplicatas.criterio_resolucao"), 6},
    };
    juce::Component::SafePointer<DuplicatesWorkspaceComponent> safeThis(this);
    DialogoEscolhaDuplicata::exibir(titulo, msg, std::move(botoes), isPt ? juce::String::fromUTF8("Cancelar") : juce::String("Cancel"),
                                    [safeThis](int resultado) {
        if (!safeThis || resultado == 0) return;
        safeThis->aplicarEscolhaGlobal(resultado);
    });
}

void DuplicatesWorkspaceComponent::aplicarEscolhaGlobal(int escolha) {
    if (gruposDetectados_.empty()) return;

    // Fase 4: em background, numa transação, com Undo; as regras de junção
    // valem sem perguntar (conflitos no histórico + filtro "Merge conflicts").
    // PROVÁVEIS (pHash) nunca entram em decisão em lote: ficam na lista para decisão manual, um a um.
    std::vector<DuplicateGroup> grupos, provaveis;
    for (const auto& g : gruposDetectados_) ((g.criterio == "PROBABLE" || g.criterio == "SIMILAR") ? provaveis : grupos).push_back(g);
    if (grupos.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("duplicatas.aviso_lote_titulo"),
                                               matriz::i18n::t("duplicatas.aviso_lote_corpo").replace("{n}", juce::String((int) provaveis.size())),
                                               {}, nullptr, juce::ModalCallbackFunction::create([](int) {}));
        return;
    }
    std::vector<std::string> ids;
    for (const auto& g : grupos) {
        ids.push_back(g.original.itemId);
        ids.push_back(g.duplicata.itemId);
    }
    auto paraDecisaoManual = std::make_shared<std::vector<DuplicateGroup>>();  // critério não se aplica / empate (etapa 9)
    juce::Component::SafePointer<DuplicatesWorkspaceComponent> safeThis(this);
    projeto_.resolverDuplicatasEmSegundoPlano(ids, [grupos, escolha, paraDecisaoManual](matriz::db::Database& db) {
        std::vector<ProjetoAberto::ResultadoSanitizacao> resultados;
        for (const auto& grupo : grupos) {
            const auto& group = grupo;
            int escolhaDoGrupo = escolha;
            if (escolha == 6) {  // maior resolução: o lado com mais pixels; sem dimensões ou empate => decisão manual
                const long long ao = static_cast<long long>(group.original.largura) * group.original.altura;
                const long long ad = static_cast<long long>(group.duplicata.largura) * group.duplicata.altura;
                escolhaDoGrupo = (ao > 0 && ad > 0 && ao != ad) ? (ao > ad ? 1 : 2) : 0;
                if (escolhaDoGrupo == 0) {
                    paraDecisaoManual->push_back(group);
                    continue;
                }
            }
            if (escolha == 4 || escolha == 5) {
                const auto c = criteriosDoPar(db, group.original.itemId, group.duplicata.itemId);
                escolhaDoGrupo = escolha == 4 ? c.maisRecente : c.backupPrimeiro;
                if (escolhaDoGrupo == 0) {
                    paraDecisaoManual->push_back(group);
                    continue;
                }
            }
            if (escolhaDoGrupo == 1) { // Keep File 1 (original) — o lado "duplicata" fica só no SOURCE
                resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.original.itemId, group.duplicata.itemId));
            } else if (escolhaDoGrupo == 2) { // Keep File 2 (duplicata) — o "original" fica só no SOURCE
                resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.duplicata.itemId, group.original.itemId));
            } else { // Keep Both — só documenta o par, os dois continuam entrando no backup normalmente
                acrescentarNota(db, group.duplicata.itemId,
                                juce::String::fromUTF8("Validated as a known duplicate pair by user in batch — both sides kept in Make Backup. "
                                "[USER_VERIFIED_DUPLICATE_KEEP_BOTH]"));
            }
        }
        return resultados;
    }, [safeThis, ids, paraDecisaoManual, provaveis](bool ok, std::vector<ProjetoAberto::ResultadoSanitizacao> resultados) {
        if (!safeThis) return;
            if (!ok) { avisarFalhaResolucao(); return; }
        publicarSanitizacoes(safeThis->projeto_, resultados, ids);
        safeThis->gruposDetectados_ = *paraDecisaoManual;
        safeThis->gruposDetectados_.insert(safeThis->gruposDetectados_.end(), provaveis.begin(), provaveis.end());
        if (!safeThis->gruposDetectados_.empty()) {
            // Sinalizados no resultado: continuam na lista pra decisão manual.
            safeThis->lblStatus_->setText(textoManuais(safeThis->gruposDetectados_), juce::dontSendNotification);
            safeThis->listaComponent_->updateList(safeThis->gruposDetectados_);
            safeThis->resized();
            safeThis->repaint();
            return;
        }
        safeThis->estado_ = State::Clean;
        safeThis->lblStatus_->setText("All duplicates have been resolved! Your archive is clean.", juce::dontSendNotification);
        safeThis->viewport_->setVisible(false);
        safeThis->resized();
        safeThis->repaint();
    });
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
            const long long ao = static_cast<long long>(group.original.largura) * group.original.altura;
            const long long ad = static_cast<long long>(group.duplicata.largura) * group.duplicata.altura;
            e.acaoMaiorResolucao = (ao > 0 && ad > 0 && ao != ad) ? (ao > ad ? 0 : 1) : -1;
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

            // Fase 4: em background, numa transação, com Undo.
            std::vector<std::pair<DuplicateGroup, int>> escolhas;
            std::vector<std::string> ids;
            for (size_t i = 0; i < indices.size() && i < resultado.size(); ++i) {
                int idx = indices[i];
                if (idx < 0 || idx >= static_cast<int>(safeThis->gruposDetectados_.size())) continue;
                const auto& group = safeThis->gruposDetectados_[static_cast<size_t>(idx)];
                escolhas.push_back({group, resultado[i].action});
                ids.push_back(group.original.itemId);
                ids.push_back(group.duplicata.itemId);
            }
            safeThis->projeto_.resolverDuplicatasEmSegundoPlano(ids, [escolhas](matriz::db::Database& db) {
                std::vector<ProjetoAberto::ResultadoSanitizacao> resultados;
                for (const auto& [group, action] : escolhas) {

                    if (action == 0) { // Keep File 1 — o outro fica só no SOURCE, fora do backup
                        resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.original.itemId, group.duplicata.itemId));
                    } else if (action == 1) { // Keep File 2
                        resultados.push_back(ProjetoAberto::sanitizarDuplicata(db, group.duplicata.itemId, group.original.itemId));
                    } else { // Keep Both — os dois continuam entrando no backup
                        acrescentarNota(db, group.duplicata.itemId,
                                        "Validated as a known duplicate pair of " + juce::String(group.original.codigoAcervo) +
                                        juce::String::fromUTF8(" by user in batch — both sides kept in Make Backup. [USER_VERIFIED_DUPLICATE_KEEP_BOTH]"));
                    }
                }
                return resultados;
            }, [safeThis, ids, escolhas](bool ok, std::vector<ProjetoAberto::ResultadoSanitizacao> resultados) {
                if (!safeThis) return;
            if (!ok) { avisarFalhaResolucao(); return; }
                publicarSanitizacoes(safeThis->projeto_, resultados, ids);
                // Pelo par (não pelo índice): a lista pode ter mudado enquanto isto rodava.
                auto& grupos = safeThis->gruposDetectados_;
                for (const auto& escolha : escolhas) {
                    const DuplicateGroup& group = escolha.first;
                    grupos.erase(std::remove_if(grupos.begin(), grupos.end(), [&](const DuplicateGroup& g) {
                                     return g.original.itemId == group.original.itemId &&
                                            g.duplicata.itemId == group.duplicata.itemId;
                                 }),
                                 grupos.end());
                }
                safeThis->atualizarListaEStatusAposResolucao();
            });
        });
}

void DuplicatesWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);
    coluna_.paint(g);

    // Área da direita: o estado vazio/varredura vira card centralizado; com
    // resultados, a linha de resumo e a lista (o viewport pinta a lista).
    auto direita = getLocalBounds().withTrimmedLeft(ColunaCardsLayout::kLargura).reduced(20);
    auto desenharCardCentral = [&](const juce::String& titulo, const juce::String& dica, float progresso) {
        auto card = direita.withSizeKeepingCentre(juce::jmin(440, direita.getWidth()), progresso >= 0.0f ? 110 : 120);
        g.setColour(tk.painelAlt.withAlpha(0.25f));
        g.fillRoundedRectangle(card.toFloat(), tk.raioPequeno);
        g.setColour(tk.borda.withAlpha(0.7f));
        g.drawRoundedRectangle(card.toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);
        auto interno = card.reduced(20, 16);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(titulo, interno.removeFromTop(24), juce::Justification::centred, true);
        interno.removeFromTop(6);
        if (progresso >= 0.0f) {
            auto barra = interno.removeFromTop(8).toFloat();
            g.setColour(tk.borda.withAlpha(0.5f));
            g.fillRoundedRectangle(barra, 4.0f);
            g.setColour(tk.acento);
            g.fillRoundedRectangle(barra.withWidth(barra.getWidth() * juce::jlimit(0.0f, 1.0f, progresso)), 4.0f);
            interno.removeFromTop(6);
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
            g.drawText(juce::String(static_cast<int>(progresso * 100)) + "%", interno, juce::Justification::centred);
        } else {
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
            g.drawFittedText(dica, interno, juce::Justification::centredTop, 3);
        }
    };

    switch (estado_) {
        case State::Idle:
            desenharCardCentral(matriz::i18n::t("duplicatas.vazio_titulo"), matriz::i18n::t("duplicatas.vazio_dica"), -1.0f);
            break;
        case State::Scanning:
            desenharCardCentral(matriz::i18n::t("duplicatas.scanning"), {}, static_cast<float>(progressoScan_));
            break;
        case State::Clean:
            desenharCardCentral(lblStatus_ ? lblStatus_->getText() : juce::String(), matriz::i18n::t("duplicatas.vazio_dica"), -1.0f);
            break;
        case State::Results:
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
            g.drawText(matriz::i18n::t("duplicatas.resumo").replace("{n}", juce::String(gruposDetectados_.size())),
                       direita.removeFromTop(kAlturaResumo), juce::Justification::centredLeft, true);
            if (const int previstos = contarPrevistos(gruposDetectados_); previstos > 0) {
                const juce::Colour ambar(0xfff59e0b);
                auto banner = direita.removeFromTop(kAlturaBannerRisco - 6);
                g.setColour(ambar.withAlpha(0.16f));
                g.fillRoundedRectangle(banner.toFloat(), 6.0f);
                g.setColour(ambar);
                g.drawRoundedRectangle(banner.toFloat().reduced(1.0f), 6.0f, 2.0f);
                auto in = banner.reduced(12, 6);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
                g.drawText(juce::String::fromUTF8("\u26A0  ") +
                               matriz::i18n::t("duplicatas.aviso_titulo").replace("{n}", juce::String(previstos)),
                           in.removeFromTop(16), juce::Justification::centredLeft, true);
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
                g.drawFittedText(matriz::i18n::t("duplicatas.aviso_corpo"), in, juce::Justification::topLeft, 2, 1.0f);
            }
            break;
    }
}

void DuplicatesWorkspaceComponent::resized() {
    auto area = getLocalBounds();
    coluna_.comecar(area.removeFromLeft(ColunaCardsLayout::kLargura));
    auto& corpo = coluna_.corpo();
    const bool emResultados = estado_ == State::Results;
    const bool isPt = matriz::i18n::localeAtivo() == "pt_BR";

    // SCAN — botão em largura total e o status.
    coluna_.iniciarCard(matriz::i18n::t("duplicatas.card_scan"));
    btnScan_->setVisible(true);
    btnScan_->setButtonText(emResultados ? (isPt ? "NOVA VARREDURA" : "RE-SCAN")
                                         : (estado_ == State::Scanning ? matriz::i18n::t("duplicatas.scanning")
                                                                        : matriz::i18n::t("duplicatas.btn_scan")));
    btnScan_->setBounds(corpo.removeFromTop(30));
    corpo.removeFromTop(4);
    lblStatus_->setJustificationType(juce::Justification::centredLeft);
    lblStatus_->setBounds(corpo.removeFromTop(lblStatus_->getText().isEmpty() ? 0 : 44));
    coluna_.finalizarCard();

    // FILTROS — rótulo pequeno acima de cada controle.
    coluna_.iniciarCard(matriz::i18n::t("duplicatas.card_filtros"));
    auto linhaRotulo = [&](juce::Label& rotulo, juce::Component& controle) {
        rotulo.setBounds(corpo.removeFromTop(14));
        controle.setBounds(corpo.removeFromTop(26));
        corpo.removeFromTop(6);
    };
    linhaRotulo(*lblScope_, *cbScope_);
    linhaRotulo(*lblFileType_, *cbFileType_);
    linhaRotulo(*lblFileSize_, *cbSizeFilter_);
    if (txtSizeValue_->isVisible()) {
        auto linha = corpo.removeFromTop(26);
        txtSizeValue_->setBounds(linha.removeFromLeft(linha.getWidth() / 2 - 3));
        linha.removeFromLeft(6);
        cbSizeUnit_->setBounds(linha);
        corpo.removeFromTop(6);
    }
    lblAno_->setBounds(corpo.removeFromTop(14));
    {
        auto linha = corpo.removeFromTop(26);
        const int campo = (linha.getWidth() - 16) / 2;
        txtAnoDe_->setBounds(linha.removeFromLeft(campo));
        lblAnoAte_->setBounds(linha.removeFromLeft(16));
        txtAnoAte_->setBounds(linha);
    }
    coluna_.finalizarCard();

    // COINCIDÊNCIAS — critérios que o usuário decide incluir ou não
    coluna_.iniciarCard(matriz::i18n::t("duplicatas.card_coincidencias"));
    auto linhaToggle = [&](juce::ToggleButton* tb) {
        if (tb != nullptr) {
            tb->setBounds(corpo.removeFromTop(18));
            corpo.removeFromTop(2);
        }
    };
    linhaToggle(tbSha256_.get());
    linhaToggle(tbPHash_.get());
    linhaToggle(tbNome_.get());
    linhaToggle(tbTamanho_.get());
    linhaToggle(tbData_.get());
    linhaToggle(tbDuracao_.get());
    linhaToggle(tbDimensoes_.get());
    linhaToggle(tbEspacoCor_.get());
    linhaToggle(tbFormato_.get());
    coluna_.finalizarCard();

    // AÇÕES — só com resultados.
    for (auto* b : {btnValidateAll_.get(), btnValidateSelected_.get(), btnDismissAll_.get(), btnDismissSelected_.get(), btnReset_.get()})
        b->setVisible(emResultados);
    if (emResultados) {
        coluna_.iniciarCard(matriz::i18n::t("duplicatas.card_acoes"));
        for (auto* b : {btnValidateAll_.get(), btnValidateSelected_.get(), btnDismissAll_.get(), btnDismissSelected_.get()}) {
            b->setBounds(corpo.removeFromTop(28));
            corpo.removeFromTop(4);
        }
        corpo.removeFromTop(6);  // RESET fica separado: não resolve nada, só limpa o recorte atual
        btnReset_->setBounds(corpo.removeFromTop(28));
        corpo.removeFromTop(4);
        coluna_.finalizarCard();
    }

    // DUPLICATAS RESOLVIDAS — sempre visível: a lista dos escondidos e o RESTAURAR.
    coluna_.iniciarCard(matriz::i18n::t("duplicatas.card_resolvidas"));
    btnResolvidas_->setBounds(corpo.removeFromTop(28));
    corpo.removeFromTop(4);
    coluna_.finalizarCard();

    // Área da direita.
    auto direita = area.reduced(20);
    if (emResultados) {
        direita.removeFromTop(kAlturaResumo + (contarPrevistos(gruposDetectados_) > 0 ? kAlturaBannerRisco : 0));
        viewport_->setBounds(direita);
        viewport_->setVisible(true);
        listaComponent_->setSize(viewport_->getWidth() - viewport_->getScrollBarThickness(), listaComponent_->getHeight());
    } else {
        viewport_->setVisible(false);
    }
    repaint();
}

} // namespace matriz::ui
