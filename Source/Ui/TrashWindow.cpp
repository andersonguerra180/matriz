#include "TrashWindow.h"
#include "Tokens.h"
#include "Tablet.h"
#include "LogosTipoArquivo.h"
#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::ui {

namespace {

struct InfoItemTrash {
    std::string id;
    juce::String titulo;
    juce::String extensao;
    juce::String nomePastaOrigem;
    juce::int64 tamanhoBytes = 0;
    bool selecionado = false;
};

juce::String formatarTamanho(juce::int64 b) {
    if (b <= 0) return "0 B";
    if (b < 1024) return juce::String(b) + " B";
    if (b < 1024 * 1024) return juce::String(b / 1024) + " KB";
    if (b < 1024LL * 1024 * 1024) return juce::String(static_cast<double>(b) / (1024.0 * 1024.0), 1) + " MB";
    return juce::String(static_cast<double>(b) / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

} // namespace

class TrashWindow::Conteudo : public juce::Component, public juce::TableListBoxModel {
public:
    Conteudo(ProjetoAberto& projeto, const std::string& mapaId, std::function<void()> aoRestaurar)
        : projeto_(projeto), mapaId_(mapaId), aoRestaurar_(std::move(aoRestaurar)) {
        
        // Search
        txtBusca_.setTextToShowWhenEmpty(matriz::i18n::localeAtivo().startsWith("pt") ? "Filtrar arquivos..." : "Filter files...", tema().textoTerciario);
        txtBusca_.onTextChange = [this] { aplicarFiltro(); };
        addAndMakeVisible(txtBusca_);

        // View mode toggle (List / Miniature)
        btnModoLista_ = std::make_unique<juce::TextButton>(matriz::i18n::localeAtivo().startsWith("pt") ? "LISTA" : "LIST");
        btnModoLista_->setClickingTogglesState(true);
        btnModoLista_->setRadioGroupId(4101);
        btnModoLista_->setToggleState(modoLista_, juce::dontSendNotification);
        btnModoLista_->onClick = [this] {
            if (!modoLista_) {
                modoLista_ = true;
                tabela_->setVisible(true);
                viewportCards_->setVisible(false);
                resized();
            }
        };
        addAndMakeVisible(*btnModoLista_);

        btnModoMini_ = std::make_unique<juce::TextButton>(matriz::i18n::localeAtivo().startsWith("pt") ? "MINIATURAS" : "MINIATURES");
        btnModoMini_->setClickingTogglesState(true);
        btnModoMini_->setRadioGroupId(4101);
        btnModoMini_->setToggleState(!modoLista_, juce::dontSendNotification);
        btnModoMini_->onClick = [this] {
            if (modoLista_) {
                modoLista_ = false;
                tabela_->setVisible(false);
                viewportCards_->setVisible(true);
                resized();
            }
        };
        addAndMakeVisible(*btnModoMini_);

        btnSelTodos_ = std::make_unique<juce::TextButton>(matriz::i18n::localeAtivo().startsWith("pt") ? "Selecionar Todos" : "Select All");
        btnSelTodos_->onClick = [this] { selecionarTudo(true); };
        addAndMakeVisible(*btnSelTodos_);

        btnLimparSel_ = std::make_unique<juce::TextButton>(matriz::i18n::localeAtivo().startsWith("pt") ? "Limpar" : "Clear");
        btnLimparSel_->onClick = [this] { selecionarTudo(false); };
        addAndMakeVisible(*btnLimparSel_);

        // Table List
        tabela_ = std::make_unique<juce::TableListBox>("TabelaTrash", this);
        tabela_->getHeader().addColumn("", 1, 36, 36, 36, juce::TableHeaderComponent::notSortable);
        tabela_->getHeader().addColumn(matriz::i18n::localeAtivo().startsWith("pt") ? "Arquivo" : "File", 2, 240, 100, 400);
        tabela_->getHeader().addColumn(matriz::i18n::localeAtivo().startsWith("pt") ? "Pasta Original" : "Original Folder", 3, 150, 80, 250);
        tabela_->getHeader().addColumn(matriz::i18n::localeAtivo().startsWith("pt") ? "Tamanho" : "Size", 4, 80, 60, 120);
        tabela_->getHeader().addColumn(matriz::i18n::localeAtivo().startsWith("pt") ? "Ação" : "Action", 5, 90, 70, 120, juce::TableHeaderComponent::notSortable);
        tabela_->setRowHeight(32);
        addAndMakeVisible(*tabela_);

        // Viewport Cards (Miniatures)
        compCards_ = std::make_unique<juce::Component>();
        viewportCards_ = std::make_unique<juce::Viewport>();
        viewportCards_->setViewedComponent(compCards_.get(), false);
        viewportCards_->setScrollBarsShown(true, false);
        viewportCards_->setVisible(false);
        addChildComponent(*viewportCards_);

        // Bottom Bar
        lblResumo_.setFont(juce::Font(juce::FontOptions(12.0f)));
        lblResumo_.setColour(juce::Label::textColourId, tema().textoSecundario);
        addAndMakeVisible(lblResumo_);

        btnRestaurarSel_ = std::make_unique<juce::TextButton>(matriz::i18n::localeAtivo().startsWith("pt") ? "RESTAURAR SELECIONADOS" : "RESTORE SELECTED");
        btnRestaurarSel_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff16a34a));
        btnRestaurarSel_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btnRestaurarSel_->onClick = [this] { restaurarSelecionados(); };
        addAndMakeVisible(*btnRestaurarSel_);

        btnRestaurarTudo_ = std::make_unique<juce::TextButton>(matriz::i18n::localeAtivo().startsWith("pt") ? "RESTAURAR TUDO" : "RESTORE ALL");
        btnRestaurarTudo_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
        btnRestaurarTudo_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
        btnRestaurarTudo_->onClick = [this] { restaurarTodos(); };
        addAndMakeVisible(*btnRestaurarTudo_);

        recarregar();
    }

    void recarregar() {
        todosItens_.clear();
        auto ids = projeto_.itensNaPastaTrash(mapaId_);
        if (!ids.empty()) {
            auto& db = projeto_.projeto().registro();
            for (const auto& id : ids) {
                InfoItemTrash it;
                it.id = id;
                auto st = db.prepare("SELECT titulo, extensao_arquivo, tamanho_bytes FROM item WHERE id = ?");
                st.bind(1, matriz::db::Value::of(id));
                if (st.step()) {
                    it.titulo = st.columnText(0);
                    it.extensao = st.columnText(1);
                    it.tamanhoBytes = st.columnInt(2);
                }
                auto stL = db.prepare("SELECT nome_pasta_origem FROM lixeira_item WHERE item_id = ? AND mapa_id = ?");
                stL.bind(1, matriz::db::Value::of(id));
                stL.bind(2, matriz::db::Value::of(mapaId_));
                if (stL.step() && !stL.columnIsNull(0)) {
                    it.nomePastaOrigem = stL.columnText(0);
                }
                todosItens_.push_back(std::move(it));
            }
        }
        aplicarFiltro();
    }

    void aplicarFiltro() {
        juce::String busca = txtBusca_.getText().trim().toLowerCase();
        filtrados_.clear();
        for (const auto& it : todosItens_) {
            if (busca.isEmpty() || it.titulo.toLowerCase().contains(busca) ||
                it.nomePastaOrigem.toLowerCase().contains(busca) || it.extensao.toLowerCase().contains(busca)) {
                filtrados_.push_back(it);
            }
        }
        atualizarBotoes();
        if (tabela_) tabela_->updateContent();
        reconstruirMiniaturas();
        repaint();
    }

    void selecionarTudo(bool sel) {
        for (auto& it : filtrados_) it.selecionado = sel;
        for (auto& it : todosItens_) {
            for (const auto& f : filtrados_) {
                if (f.id == it.id) { it.selecionado = sel; break; }
            }
        }
        atualizarBotoes();
        if (tabela_) tabela_->repaint();
        if (compCards_) compCards_->repaint();
    }

    void atualizarBotoes() {
        int total = static_cast<int>(todosItens_.size());
        int sel = 0;
        for (const auto& it : todosItens_) if (it.selecionado) sel++;
        juce::String res = juce::String(total) + (matriz::i18n::localeAtivo().startsWith("pt") ? " arquivo(s) na lixeira" : " item(s) in trash");
        if (sel > 0) res += " (" + juce::String(sel) + (matriz::i18n::localeAtivo().startsWith("pt") ? " selecionado(s))" : " selected)");
        lblResumo_.setText(res, juce::dontSendNotification);
        btnRestaurarSel_->setEnabled(sel > 0);
        btnRestaurarTudo_->setEnabled(total > 0);
    }

    void restaurarSelecionados() {
        std::vector<std::string> ids;
        for (const auto& it : todosItens_) if (it.selecionado) ids.push_back(it.id);
        if (ids.empty()) return;
        projeto_.restaurarItensDaTrash(ids, mapaId_);
        recarregar();
        if (aoRestaurar_) aoRestaurar_();
    }

    void restaurarTodos() {
        std::vector<std::string> ids;
        for (const auto& it : todosItens_) ids.push_back(it.id);
        if (ids.empty()) return;
        projeto_.restaurarItensDaTrash(ids, mapaId_);
        recarregar();
        if (aoRestaurar_) aoRestaurar_();
    }

    void restaurarItem(const std::string& id) {
        projeto_.restaurarItensDaTrash({id}, mapaId_);
        recarregar();
        if (aoRestaurar_) aoRestaurar_();
    }

    // TableListBoxModel
    int getNumRows() override { return static_cast<int>(filtrados_.size()); }
    void paintRowBackground(juce::Graphics& g, int rowNumber, int, int, bool isRowSelected) override {
        const auto& tk = tema();
        if (isRowSelected) g.fillAll(tk.acento.withAlpha(0.20f));
        else if (rowNumber % 2 == 1) g.fillAll(tk.painelAlt.withAlpha(0.35f));
    }

    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) override {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(filtrados_.size())) return;
        const auto& it = filtrados_[static_cast<size_t>(rowNumber)];
        const auto& tk = tema();
        g.setColour(tk.textoPrimario);
        g.setFont(fonteUi(tk.tamanhoFonteCorpo));

        if (columnId == 1) { // Checkbox
            auto r = juce::Rectangle<int>(0, 0, width, height).withSizeKeepingCentre(16, 16);
            g.setColour(it.selecionado ? tk.acento : tk.borda);
            g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.2f);
            if (it.selecionado) {
                g.setColour(tk.acento);
                g.fillRoundedRectangle(r.reduced(2).toFloat(), 2.0f);
            }
        } else if (columnId == 2) { // File name
            auto ext = it.extensao.toLowerCase();
            auto logoFile = matriz::ingest::obterLogoParaExtensao(ext);
            auto rLogo = juce::Rectangle<int>(4, 4, 24, height - 8);
            if (logoFile.isNotEmpty()) {
                auto logo = matriz::ui::carregarLogoDeTipo(logoFile);
                if (logo.isValid()) g.drawImage(logo, rLogo.toFloat(), juce::RectanglePlacement::centred);
            }
            g.drawText(it.titulo, 34, 0, width - 38, height, juce::Justification::centredLeft, true);
        } else if (columnId == 3) { // Original folder
            g.setColour(tk.textoSecundario);
            g.drawText(it.nomePastaOrigem.isNotEmpty() ? it.nomePastaOrigem : "-", 4, 0, width - 8, height, juce::Justification::centredLeft, true);
        } else if (columnId == 4) { // Size
            g.setColour(tk.textoSecundario);
            g.drawText(formatarTamanho(it.tamanhoBytes), 4, 0, width - 8, height, juce::Justification::centredRight, true);
        }
    }

    juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool, juce::Component* existing) override {
        if (columnId == 5) {
            auto* btn = dynamic_cast<juce::TextButton*>(existing);
            if (!btn) btn = new juce::TextButton(matriz::i18n::localeAtivo().startsWith("pt") ? "Restaurar" : "Restore");
            btn->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
            btn->setColour(juce::TextButton::textColourOffId, tema().acento);
            btn->onClick = [this, rowNumber] {
                if (rowNumber >= 0 && rowNumber < static_cast<int>(filtrados_.size())) {
                    restaurarItem(filtrados_[static_cast<size_t>(rowNumber)].id);
                }
            };
            return btn;
        }
        delete existing;
        return nullptr;
    }

    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override {
        if (rowNumber >= 0 && rowNumber < static_cast<int>(filtrados_.size())) {
            if (columnId == 1 || columnId == 2) {
                filtrados_[static_cast<size_t>(rowNumber)].selecionado = !filtrados_[static_cast<size_t>(rowNumber)].selecionado;
                for (auto& it : todosItens_) {
                    if (it.id == filtrados_[static_cast<size_t>(rowNumber)].id) {
                        it.selecionado = filtrados_[static_cast<size_t>(rowNumber)].selecionado;
                        break;
                    }
                }
                atualizarBotoes();
                tabela_->repaintRow(rowNumber);
                if (compCards_) compCards_->repaint();
            }
        }
    }

    void reconstruirMiniaturas() {
        if (!compCards_) return;
        compCards_->removeAllChildren();
        const int cardW = 140;
        const int cardH = 130;
        const int pad = 12;
        const int larguraDisp = juce::jmax(200, getWidth() - 24);
        const int cols = juce::jmax(1, larguraDisp / (cardW + pad));

        for (size_t i = 0; i < filtrados_.size(); ++i) {
            const auto& it = filtrados_[i];
            int r = static_cast<int>(i) / cols;
            int c = static_cast<int>(i) % cols;
            int x = pad + c * (cardW + pad);
            int y = pad + r * (cardH + pad);

            auto card = std::make_unique<CardMiniaturaTrash>(it, [this, id = it.id] {
                restaurarItem(id);
            }, [this, i] {
                if (i < filtrados_.size()) {
                    filtrados_[i].selecionado = !filtrados_[i].selecionado;
                    for (auto& item : todosItens_) {
                        if (item.id == filtrados_[i].id) { item.selecionado = filtrados_[i].selecionado; break; }
                    }
                    atualizarBotoes();
                    reconstruirMiniaturas();
                    if (tabela_) tabela_->repaint();
                }
            });
            card->setBounds(x, y, cardW, cardH);
            compCards_->addAndMakeVisible(card.release());
        }

        int totalLinhas = (static_cast<int>(filtrados_.size()) + cols - 1) / cols;
        int totalH = pad + totalLinhas * (cardH + pad);
        compCards_->setSize(larguraDisp, totalH);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);

        // Top bar
        auto top = area.removeFromTop(28);
        txtBusca_.setBounds(top.removeFromLeft(200));
        top.removeFromLeft(12);
        btnModoLista_->setBounds(top.removeFromLeft(64));
        top.removeFromLeft(4);
        btnModoMini_->setBounds(top.removeFromLeft(96));
        top.removeFromLeft(12);
        btnSelTodos_->setBounds(top.removeFromLeft(100));
        top.removeFromLeft(4);
        btnLimparSel_->setBounds(top.removeFromLeft(64));

        area.removeFromTop(8);

        // Bottom bar
        auto bottom = area.removeFromBottom(32);
        btnRestaurarTudo_->setBounds(bottom.removeFromRight(130));
        bottom.removeFromRight(8);
        btnRestaurarSel_->setBounds(bottom.removeFromRight(170));
        lblResumo_.setBounds(bottom);

        area.removeFromBottom(8);

        // Center view
        if (tabela_ && tabela_->isVisible()) {
            tabela_->setBounds(area);
        }
        if (viewportCards_ && viewportCards_->isVisible()) {
            viewportCards_->setBounds(area);
            reconstruirMiniaturas();
        }
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(tema().fundo);
        if (filtrados_.empty()) {
            g.setColour(tema().textoTerciario);
            g.setFont(fonteUi(14.0f));
            g.drawText(matriz::i18n::localeAtivo().startsWith("pt") ? "A lixeira está vazia" : "Trash is empty",
                       getLocalBounds(), juce::Justification::centred, true);
        }
    }

private:
    class CardMiniaturaTrash : public juce::Component {
    public:
        CardMiniaturaTrash(const InfoItemTrash& info, std::function<void()> onRestaurar, std::function<void()> onToggle)
            : info_(info), onRestaurar_(std::move(onRestaurar)), onToggle_(std::move(onToggle)) {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void mouseUp(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu()) {
                juce::PopupMenu m;
                m.addItem(matriz::i18n::localeAtivo().startsWith("pt") ? "Restaurar Arquivo" : "Restore File", [this] {
                    if (onRestaurar_) onRestaurar_();
                });
                m.showMenuAsync(juce::PopupMenu::Options());
            } else {
                if (onToggle_) onToggle_();
            }
        }

        void paint(juce::Graphics& g) override {
            const auto& tk = tema();
            auto bounds = getLocalBounds().toFloat();
            g.setColour(info_.selecionado ? tk.acento.withAlpha(0.25f) : tk.painel);
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(info_.selecionado ? tk.acento : tk.borda);
            g.drawRoundedRectangle(bounds, 6.0f, info_.selecionado ? 1.8f : 1.0f);

            auto thumbArea = bounds.removeFromTop(75.0f).reduced(6.0f);
            g.setColour(juce::Colours::black);
            g.fillRoundedRectangle(thumbArea, 4.0f);

            auto ext = info_.extensao.toLowerCase();
            auto logoFile = matriz::ingest::obterLogoParaExtensao(ext);
            if (logoFile.isNotEmpty()) {
                auto logo = matriz::ui::carregarLogoDeTipo(logoFile);
                if (logo.isValid()) {
                    auto logoArea = thumbArea.withSizeKeepingCentre(juce::jmin(thumbArea.getWidth() * 0.6f, 36.0f),
                                                                    juce::jmin(thumbArea.getHeight() * 0.6f, 36.0f));
                    g.drawImage(logo, logoArea, juce::RectanglePlacement::centred);
                }
            }

            // Checkbox
            auto chk = bounds.removeFromTop(0).withPosition(6.0f, 6.0f).withSize(14.0f, 14.0f);
            if (info_.selecionado) {
                g.setColour(tk.acento);
                g.fillRoundedRectangle(chk, 2.0f);
            }

            // Title & info
            g.setColour(tk.textoPrimario);
            g.setFont(fonteUi(11.0f, juce::Font::bold));
            g.drawText(info_.titulo, bounds.removeFromTop(20.0f).reduced(6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);

            g.setColour(tk.textoTerciario);
            g.setFont(fonteUi(9.5f));
            juce::String sub = info_.nomePastaOrigem.isNotEmpty() ? info_.nomePastaOrigem : formatarTamanho(info_.tamanhoBytes);
            g.drawText(sub, bounds.reduced(6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
        }

    private:
        InfoItemTrash info_;
        std::function<void()> onRestaurar_;
        std::function<void()> onToggle_;
    };

    ProjetoAberto& projeto_;
    std::string mapaId_;
    std::function<void()> aoRestaurar_;
    std::vector<InfoItemTrash> todosItens_;
    std::vector<InfoItemTrash> filtrados_;
    bool modoLista_ = true;

    juce::TextEditor txtBusca_;
    std::unique_ptr<juce::TextButton> btnModoLista_;
    std::unique_ptr<juce::TextButton> btnModoMini_;
    std::unique_ptr<juce::TextButton> btnSelTodos_;
    std::unique_ptr<juce::TextButton> btnLimparSel_;

    std::unique_ptr<juce::TableListBox> tabela_;
    std::unique_ptr<juce::Viewport> viewportCards_;
    std::unique_ptr<juce::Component> compCards_;

    juce::Label lblResumo_;
    std::unique_ptr<juce::TextButton> btnRestaurarSel_;
    std::unique_ptr<juce::TextButton> btnRestaurarTudo_;
};

TrashWindow::TrashWindow(ProjetoAberto& projeto, const std::string& mapaId, std::function<void()> aoRestaurar)
    : juce::DocumentWindow("🗑 _trash", tema().fundo, juce::DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(500, 360, 1200, 900);
    conteudo_ = std::make_unique<Conteudo>(projeto, mapaId, std::move(aoRestaurar));
    setContentNonOwned(conteudo_.get(), true);
    centreWithSize(640, 480);
    setVisible(true);
}

TrashWindow::~TrashWindow() {
    clearContentComponent();
}

void TrashWindow::closeButtonPressed() {
    setVisible(false);
}

} // namespace matriz::ui
