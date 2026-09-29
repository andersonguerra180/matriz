#include "MainEditPanel.h"

#include "ModalMitigacao.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

namespace {

juce::String tamanhoHumano(juce::int64 bytes) { return juce::File::descriptionOfSizeInBytes(bytes); }

void estilizar(juce::TextButton& b, bool perigo = false) {
    const auto& tk = tema();
    b.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    b.setColour(juce::TextButton::textColourOffId, perigo ? tk.perigo : tk.textoPrimario);
}

void achatarPastas(const ProjetoAberto::NoArvore& no, const juce::String& prefixo,
                   std::vector<std::pair<std::string, juce::String>>& out) {
    for (const auto& f : no.filhos) {
        if (f.id.empty()) continue;  // nó sintético "não organizados"
        const juce::String caminho = prefixo.isEmpty() ? juce::String(f.nome) : prefixo + " / " + juce::String(f.nome);
        out.push_back({f.id, caminho});
        achatarPastas(f, caminho, out);
    }
}

}  // namespace

// ------------------------------------------------------------------ árvore

class MainEditPanel::ItemArvore : public juce::TreeViewItem {
public:
    ItemArvore(MainEditPanel& dono, NoMem* no, juce::String rel) : dono_(dono), no_(no), rel_(std::move(rel)) {}

    bool mightContainSubItems() override { return no_ != nullptr && !no_->ehArquivo() && !no_->filhos.empty(); }
    juce::String getUniqueName() const override { return rel_.isEmpty() ? "/" : rel_; }
    int getItemHeight() const override { return 22; }

    void itemOpennessChanged(bool aberto) override {
        if (aberto && getNumSubItems() == 0) preencher();
    }
    void itemSelectionChanged(bool) override { dono_.atualizarBotoes(); }

    void paintItem(juce::Graphics& g, int w, int h) override {
        const auto& tk = tema();
        if (isSelected()) g.fillAll(tk.acento.withAlpha(0.35f));
        g.setColour(no_ && no_->ehArquivo() ? tk.textoPrimario : tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(12.5f, no_ && no_->ehArquivo() ? juce::Font::plain : juce::Font::bold)));
        g.drawText(no_ ? no_->nome : juce::String(), 4, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    const NoMem* no() const { return no_; }
    const juce::String& rel() const { return rel_; }

private:
    // Poda: pasta com centenas de milhares de arquivos não vira centenas de
    // milhares de linhas de UI de uma vez.
    static constexpr int kMaxFilhos = 3000;

    void preencher() {
        if (!no_) return;
        std::vector<NoMem*> pastas, arquivos;
        for (auto& [nome, filho] : no_->filhos) (filho->ehArquivo() ? arquivos : pastas).push_back(filho.get());
        int n = 0;
        for (auto* v : {&pastas, &arquivos}) {
            for (auto* f : *v) {
                if (n++ >= kMaxFilhos) return;
                addSubItem(new ItemArvore(dono_, f, rel_.isEmpty() ? f->nome : rel_ + "/" + f->nome));
            }
        }
    }

    MainEditPanel& dono_;
    NoMem* no_;
    juce::String rel_;
};

// ------------------------------------------------------------------ painel

MainEditPanel::MainEditPanel(ProjetoAberto& projeto) : projeto_(projeto) {
    const auto& tk = tema();
    auto texto = [](const char* k) { return matriz::i18n::t(k); };

    btnAbaArquivos_.setButtonText(texto("main_edit.aba_arquivos"));
    btnAbaQuarentena_.setButtonText(texto("main_edit.aba_quarentena"));
    btnAbaArquivos_.onClick = [this] { mostrarAba(true); };
    btnAbaQuarentena_.onClick = [this] { mostrarAba(false); };
    addAndMakeVisible(btnAbaArquivos_);
    addAndMakeVisible(btnAbaQuarentena_);

    arvore_ = std::make_unique<juce::TreeView>();
    arvore_->setColour(juce::TreeView::backgroundColourId, tk.painel);
    arvore_->setDefaultOpenness(false);
    arvore_->setMultiSelectEnabled(false);
    addAndMakeVisible(*arvore_);

    infoArquivos_.setColour(juce::Label::textColourId, tk.textoSecundario);
    infoArquivos_.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(infoArquivos_);

    btnRenomear_.setButtonText(texto("main_edit.renomear"));
    btnMover_.setButtonText(texto("main_edit.mover"));
    btnSubstituir_.setButtonText(texto("main_edit.substituir"));
    btnDeletar_.setButtonText(texto("main_edit.deletar"));
    btnConverter_.setButtonText(texto("main_edit.converter"));
    btnRestaurar_.setButtonText(texto("main_edit.restaurar"));
    btnEsvaziar_.setButtonText(texto("main_edit.esvaziar"));
    for (auto* b : {&btnRenomear_, &btnMover_, &btnSubstituir_, &btnConverter_, &btnRestaurar_}) estilizar(*b);
    estilizar(btnDeletar_, true);
    estilizar(btnEsvaziar_, true);
    btnRenomear_.onClick = [this] { renomear(); };
    btnMover_.onClick = [this] { mover(); };
    btnSubstituir_.onClick = [this] { substituir(); };
    btnDeletar_.onClick = [this] { deletar(); };
    btnConverter_.onClick = [this] { converter(); };
    btnRestaurar_.onClick = [this] { restaurar(); };
    btnEsvaziar_.onClick = [this] { esvaziar(); };
    for (auto* b : {&btnRenomear_, &btnMover_, &btnSubstituir_, &btnDeletar_, &btnConverter_, &btnRestaurar_, &btnEsvaziar_})
        addAndMakeVisible(*b);

    listaQuarentena_.setModel(this);
    listaQuarentena_.setColour(juce::ListBox::backgroundColourId, tk.painel);
    listaQuarentena_.setRowHeight(24);
    addAndMakeVisible(listaQuarentena_);
    resumoQuarentena_.setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(resumoQuarentena_);

    setSize(880, 580);
    mostrarAba(true);
    carregarArquivos();
    carregarQuarentena();
}

MainEditPanel::~MainEditPanel() {
    arvore_->setRootItem(nullptr);
    listaQuarentena_.setModel(nullptr);
}

void MainEditPanel::paint(juce::Graphics& g) { g.fillAll(tema().fundo); }

void MainEditPanel::resized() {
    auto r = getLocalBounds().reduced(12);
    auto abas = r.removeFromTop(30);
    btnAbaArquivos_.setBounds(abas.removeFromLeft(140));
    abas.removeFromLeft(6);
    btnAbaQuarentena_.setBounds(abas.removeFromLeft(140));
    r.removeFromTop(8);

    auto botoes = r.removeFromBottom(32);
    if (abaArquivos_) {
        auto larg = [&](juce::TextButton& b, int w) { b.setBounds(botoes.removeFromLeft(w)); botoes.removeFromLeft(6); };
        larg(btnRenomear_, 100);
        larg(btnMover_, 120);
        larg(btnSubstituir_, 130);
        larg(btnDeletar_, 100);
        btnConverter_.setBounds(botoes.removeFromRight(260));
        r.removeFromBottom(6);
        infoArquivos_.setBounds(r.removeFromBottom(20));
        arvore_->setBounds(r);
    } else {
        btnRestaurar_.setBounds(botoes.removeFromLeft(130));
        btnEsvaziar_.setBounds(botoes.removeFromRight(230));
        r.removeFromBottom(6);
        resumoQuarentena_.setBounds(r.removeFromTop(22));
        listaQuarentena_.setBounds(r);
    }
}

void MainEditPanel::mostrarAba(bool arquivos) {
    abaArquivos_ = arquivos;
    const auto& tk = tema();
    btnAbaArquivos_.setColour(juce::TextButton::buttonColourId, arquivos ? tk.acento : tk.painelAlt);
    btnAbaQuarentena_.setColour(juce::TextButton::buttonColourId, arquivos ? tk.painelAlt : tk.acento);
    btnAbaArquivos_.setColour(juce::TextButton::textColourOffId, arquivos ? juce::Colours::white : tk.textoPrimario);
    btnAbaQuarentena_.setColour(juce::TextButton::textColourOffId, arquivos ? tk.textoPrimario : juce::Colours::white);
    for (auto* c : std::initializer_list<juce::Component*>{arvore_.get(), &infoArquivos_, &btnRenomear_, &btnMover_,
                                                            &btnSubstituir_, &btnDeletar_, &btnConverter_})
        c->setVisible(arquivos);
    for (auto* c : std::initializer_list<juce::Component*>{&listaQuarentena_, &resumoQuarentena_, &btnRestaurar_, &btnEsvaziar_})
        c->setVisible(!arquivos);
    atualizarBotoes();
    resized();
}

// ------------------------------------------------------------------ dados

void MainEditPanel::carregarArquivos() {
    carregando_ = true;
    infoArquivos_.setText(matriz::i18n::t("main_edit.carregando"), juce::dontSendNotification);
    atualizarBotoes();
    ProjetoAberto* projeto = &projeto_;
    juce::WeakReference<MainEditPanel> weak(this);
    // Leitura do banco em background (a thread do pool do MAIN é encerrada antes
    // do projeto): a árvore nasce da lista registrada, sem tocar no disco.
    projeto_.agendarNoPoolDoMain([projeto, weak] {
        auto raiz = std::make_shared<NoMem>();
        int total = 0;
        try {
            auto st = projeto->projeto().registro().prepare("SELECT id, caminho_relativo_destino FROM consolidacao_registro");
            while (st.step()) {
                const std::string id = st.columnText(0);
                juce::StringArray partes;
                partes.addTokens(juce::String::fromUTF8(st.columnText(1).c_str()), "/", "");
                partes.removeEmptyStrings();
                if (partes.isEmpty()) continue;
                NoMem* atual = raiz.get();
                for (int i = 0; i < partes.size(); ++i) {
                    auto& slot = atual->filhos[partes[i]];
                    if (!slot) { slot = std::make_unique<NoMem>(); slot->nome = partes[i]; }
                    atual = slot.get();
                }
                atual->registroId = id;
                ++total;
            }
        } catch (...) {}
        juce::MessageManager::callAsync([weak, raiz, total] {
            if (auto* p = weak.get()) {
                p->raiz_ = raiz;
                p->carregando_ = false;
                p->infoArquivos_.setText(juce::String(total) + " files registered in the MAIN", juce::dontSendNotification);
                p->reconstruirArvore();
                p->atualizarBotoes();
            }
        });
    });
}

void MainEditPanel::reconstruirArvore() {
    arvore_->setRootItem(nullptr);
    itemRaiz_.reset();
    if (!raiz_) return;
    itemRaiz_ = std::make_unique<ItemArvore>(*this, raiz_.get(), juce::String());
    arvore_->setRootItemVisible(false);
    arvore_->setRootItem(itemRaiz_.get());
    itemRaiz_->setOpen(true);
}

void MainEditPanel::carregarQuarentena() {
    ProjetoAberto* projeto = &projeto_;
    juce::WeakReference<MainEditPanel> weak(this);
    projeto_.agendarNoPoolDoMain([projeto, weak] {
        auto ctx = projeto->contextoDoMain();
        auto itens = matriz::mainedit::listarQuarentena(ctx);
        auto resumo = matriz::mainedit::resumoQuarentena(ctx);
        juce::MessageManager::callAsync([weak, itens = std::move(itens), resumo]() mutable {
            if (auto* p = weak.get()) {
                p->quarentena_ = std::move(itens);
                p->resumoQuarentena_.setText(matriz::i18n::t("main_edit.quarentena_resumo")
                                                 .replace("{n}", juce::String(resumo.itens))
                                                 .replace("{tam}", tamanhoHumano(resumo.bytes)),
                                             juce::dontSendNotification);
                p->listaQuarentena_.updateContent();
                p->listaQuarentena_.repaint();
                p->atualizarBotoes();
            }
        });
    });
}

std::string MainEditPanel::registroSelecionado() const {
    if (auto* it = dynamic_cast<ItemArvore*>(arvore_->getSelectedItem(0)))
        if (it->no() && it->no()->ehArquivo()) return it->no()->registroId;
    return {};
}

juce::String MainEditPanel::caminhoSelecionado() const {
    if (auto* it = dynamic_cast<ItemArvore*>(arvore_->getSelectedItem(0))) return it->rel();
    return {};
}

void MainEditPanel::atualizarBotoes() {
    const bool livre = !carregando_ && !projeto_.operacaoMainEmCurso();
    const bool sel = !registroSelecionado().empty();
    const bool usaMapa = projeto_.mainUsaMapa();
    btnRenomear_.setEnabled(livre && sel);
    btnMover_.setEnabled(livre && sel && usaMapa);
    btnMover_.setTooltip(usaMapa ? juce::String() : matriz::i18n::t("main_edit.mover_sem_mapa"));
    btnSubstituir_.setEnabled(livre && sel);
    btnDeletar_.setEnabled(livre && sel);
    btnConverter_.setVisible(abaArquivos_ && !usaMapa);
    btnConverter_.setEnabled(livre && !usaMapa);
    btnRestaurar_.setEnabled(livre && listaQuarentena_.getSelectedRow() >= 0);
    btnEsvaziar_.setEnabled(livre && !quarentena_.empty());
}

void MainEditPanel::avisar(const juce::String& titulo, const juce::String& msg) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::InfoIcon)
                                     .withTitle(titulo)
                                     .withMessage(msg)
                                     .withButton(matriz::i18n::t("dialogo.ok")),
                                 juce::ModalCallbackFunction::create([](int) {}));
}

void MainEditPanel::concluir(const matriz::mainedit::Resultado& r, const juce::String& msgOk) {
    if (!r.ok && !r.destinoOcupado) avisar(matriz::i18n::t("main_edit.erro"), juce::String::fromUTF8(r.erro.c_str()));
    else if (r.ok && msgOk.isNotEmpty()) avisar(matriz::i18n::t("main_edit.titulo"), msgOk);
    carregarArquivos();
    carregarQuarentena();
    atualizarBotoes();
    if (aoMudar) aoMudar();
}

// ------------------------------------------------------------------ ações

void MainEditPanel::renomear() {
    const auto id = registroSelecionado();
    if (id.empty()) { avisar(matriz::i18n::t("main_edit.titulo"), matriz::i18n::t("main_edit.sem_selecao")); return; }
    const auto nomeAtual = juce::File(caminhoSelecionado()).getFileName();
    juce::WeakReference<MainEditPanel> weak(this);
    ModalTextoDialog::exibir(matriz::i18n::t("main_edit.novo_nome_titulo"), matriz::i18n::t("main_edit.novo_nome_msg"), nomeAtual,
                             [weak, id](std::optional<juce::String> novo) {
                                 auto* p = weak.get();
                                 if (!p || !novo || novo->trim().isEmpty()) return;
                                 p->projeto_.editarMainRenomearArquivo(id, *novo, [weak](const matriz::mainedit::Resultado& r) {
                                     if (auto* q = weak.get()) q->concluir(r);
                                 });
                                 p->atualizarBotoes();
                             });
}

void MainEditPanel::mover() {
    const auto id = registroSelecionado();
    if (id.empty()) { avisar(matriz::i18n::t("main_edit.titulo"), matriz::i18n::t("main_edit.sem_selecao")); return; }
    if (!projeto_.mainUsaMapa()) { avisar(matriz::i18n::t("main_edit.titulo"), matriz::i18n::t("main_edit.mover_sem_mapa")); return; }
    std::vector<std::pair<std::string, juce::String>> pastas;
    achatarPastas(projeto_.arvoreAcervo(projeto_.mapaDoMainId()), {}, pastas);
    if (pastas.empty()) { avisar(matriz::i18n::t("main_edit.titulo"), matriz::i18n::t("main_edit.mover_sem_pastas")); return; }
    juce::PopupMenu menu;
    for (size_t i = 0; i < pastas.size(); ++i) menu.addItem(static_cast<int>(i) + 1, pastas[i].second);
    juce::WeakReference<MainEditPanel> weak(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&btnMover_), [weak, id, pastas](int escolha) {
        auto* p = weak.get();
        if (!p || escolha <= 0) return;
        p->projeto_.editarMainMoverArquivo(id, pastas[static_cast<size_t>(escolha) - 1].first, [weak](const matriz::mainedit::Resultado& r) {
            if (auto* q = weak.get()) q->concluir(r);
        });
        p->atualizarBotoes();
    });
}

void MainEditPanel::substituir() {
    const auto id = registroSelecionado();
    if (id.empty()) { avisar(matriz::i18n::t("main_edit.titulo"), matriz::i18n::t("main_edit.sem_selecao")); return; }
    juce::WeakReference<MainEditPanel> weak(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle(matriz::i18n::t("main_edit.substituir"))
            .withMessage(matriz::i18n::t("main_edit.confirmar_substituir"))
            .withButton(matriz::i18n::t("main_edit.substituir"))
            .withButton(matriz::i18n::t("dialogo.cancelar")),
        juce::ModalCallbackFunction::create([weak, id](int res) {
            auto* p = weak.get();
            if (res != 1 || !p) return;
            p->chooser_ = std::make_unique<juce::FileChooser>(matriz::i18n::t("main_edit.substituir"), juce::File(), "*");
            p->chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                     [weak, id](const juce::FileChooser& fc) {
                                         auto* q = weak.get();
                                         if (!q || fc.getResult() == juce::File()) return;
                                         q->projeto_.editarMainSubstituirArquivo(id, fc.getResult(), [weak](const matriz::mainedit::Resultado& r) {
                                             if (auto* z = weak.get()) z->concluir(r);
                                         });
                                         q->atualizarBotoes();
                                     });
        }));
}

void MainEditPanel::deletar() {
    const auto id = registroSelecionado();
    if (id.empty()) { avisar(matriz::i18n::t("main_edit.titulo"), matriz::i18n::t("main_edit.sem_selecao")); return; }
    juce::WeakReference<MainEditPanel> weak(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle(matriz::i18n::t("main_edit.deletar"))
            .withMessage(matriz::i18n::t("main_edit.confirmar_deletar"))
            .withButton(matriz::i18n::t("main_edit.deletar"))
            .withButton(matriz::i18n::t("dialogo.cancelar")),
        juce::ModalCallbackFunction::create([weak, id](int res) {
            auto* p = weak.get();
            if (res != 1 || !p) return;
            p->projeto_.editarMainDeletarArquivo(id, [weak](const matriz::mainedit::Resultado& r) {
                if (auto* q = weak.get()) q->concluir(r);
            });
            p->atualizarBotoes();
        }));
}

void MainEditPanel::converter() {
    juce::WeakReference<MainEditPanel> weak(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle(matriz::i18n::t("main_edit.converter"))
            .withMessage(matriz::i18n::t("main_edit.confirmar_converter"))
            .withButton(matriz::i18n::t("main_edit.converter"))
            .withButton(matriz::i18n::t("dialogo.cancelar")),
        juce::ModalCallbackFunction::create([weak](int res) {
            auto* p = weak.get();
            if (res != 1 || !p) return;
            p->projeto_.converterMainParaFolderMap([weak](const juce::String& erro, const juce::String& nome) {
                auto* q = weak.get();
                if (!q) return;
                matriz::mainedit::Resultado r;
                r.ok = erro.isEmpty();
                r.erro = erro.toStdString();
                q->concluir(r, matriz::i18n::t("main_edit.convertido").replace("{nome}", nome));
            });
            p->atualizarBotoes();
        }));
}

void MainEditPanel::restaurar() {
    const int row = listaQuarentena_.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(quarentena_.size())) return;
    const auto item = quarentena_[static_cast<size_t>(row)];
    juce::WeakReference<MainEditPanel> weak(this);
    projeto_.editarMainRestaurar(item.id, {}, [weak, item](const matriz::mainedit::Resultado& r) {
        auto* p = weak.get();
        if (!p) return;
        if (!r.destinoOcupado || item.motivo == "substituido") { auto r2 = r; r2.destinoOcupado = false; p->concluir(r2); return; }
        // Já existe algo no caminho original: pede outro destino (nunca sobrescreve).
        const juce::File original(item.caminhoOriginal);
        const juce::String sugestao = (original.getParentDirectory().getFullPathName().isNotEmpty() && item.caminhoOriginal.contains("/")
                                           ? item.caminhoOriginal.upToLastOccurrenceOf("/", true, false) : juce::String())
                                      + original.getFileNameWithoutExtension() + " (restored)" + original.getFileExtension();
        ModalTextoDialog::exibir(matriz::i18n::t("main_edit.restaurar_ocupado_titulo"),
                                 matriz::i18n::t("main_edit.restaurar_ocupado_msg"), sugestao,
                                 [weak, item](std::optional<juce::String> novo) {
                                     auto* q = weak.get();
                                     if (!q || !novo || novo->trim().isEmpty()) return;
                                     q->projeto_.editarMainRestaurar(item.id, novo->trim(), [weak](const matriz::mainedit::Resultado& r2) {
                                         if (auto* z = weak.get()) z->concluir(r2);
                                     });
                                 });
    });
    atualizarBotoes();
}

void MainEditPanel::esvaziar() {
    const auto resumo = matriz::mainedit::resumoQuarentena(projeto_.contextoDoMain());
    if (resumo.itens == 0) return;
    juce::WeakReference<MainEditPanel> weak(this);
    pedirNomeDoProjeto(projeto_, matriz::i18n::t("main_edit.esvaziar"),
                       matriz::i18n::t("main_edit.esvaziar_msg").replace("{n}", juce::String(resumo.itens))
                                                                 .replace("{tam}", tamanhoHumano(resumo.bytes)),
                       matriz::i18n::t("main_edit.esvaziar"), [weak](const juce::String&) {
                           auto* p = weak.get();
                           if (!p) return;
                           p->projeto_.editarMainEsvaziarQuarentena([weak](const matriz::mainedit::ResultadoEsvaziar& res) {
                               auto* q = weak.get();
                               if (!q) return;
                               matriz::mainedit::Resultado r;
                               r.ok = res.falhas.empty();
                               if (!res.falhas.empty()) r.erro = res.falhas.front();
                               q->concluir(r, matriz::i18n::t("main_edit.esvaziado").replace("{n}", juce::String(res.apagados)));
                           });
                           p->atualizarBotoes();
                       });
}

void MainEditPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selecionada) {
    if (row < 0 || row >= static_cast<int>(quarentena_.size())) return;
    const auto& tk = tema();
    const auto& i = quarentena_[static_cast<size_t>(row)];
    if (selecionada) g.fillAll(tk.acento.withAlpha(0.35f));
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(12.5f)));
    const juce::String motivo = matriz::i18n::t(i.motivo == "deletado" ? "main_edit.motivo_deletado" : "main_edit.motivo_substituido");
    g.drawText(i.caminhoOriginal, 6, 0, w - 330, h, juce::Justification::centredLeft, true);
    g.setColour(tk.textoSecundario);
    g.drawText(motivo + "   " + i.criadoEm.substring(0, 10) + "   " + tamanhoHumano(i.tamanhoBytes), w - 320, 0, 314, h,
               juce::Justification::centredRight, true);
}

// ------------------------------------------------------------------ estáticos

void MainEditPanel::abrir(ProjetoAberto& projeto, std::function<void()> aoMudar) {
    auto* painel = new MainEditPanel(projeto);
    painel->aoMudar = std::move(aoMudar);
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = matriz::i18n::t("main_edit.titulo");
    opt.content.setOwned(painel);
    opt.dialogBackgroundColour = tema().fundo;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;
    opt.launchAsync();
}

void MainEditPanel::pedirNomeDoProjeto(ProjetoAberto& projeto, const juce::String& titulo, const juce::String& mensagem,
                                       const juce::String& textoBotao, std::function<void(const juce::String& digitado)> aoConfirmar) {
    auto* w = new juce::AlertWindow(titulo, mensagem, juce::MessageBoxIconType::WarningIcon);
    w->addTextEditor("nome", "", matriz::i18n::t("main_edit.barreira_campo"));
    w->addButton(textoBotao, 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton(matriz::i18n::t("dialogo.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    ProjetoAberto* p = &projeto;
    w->enterModalState(true, juce::ModalCallbackFunction::create([w, p, aoConfirmar](int res) {
        if (res != 1) return;
        // Compara com o nome ATUAL do projeto (File > Rename Project pode ter mudado).
        const auto digitado = w->getTextEditorContents("nome");
        if (p->nomeConfereComProjeto(digitado)) {
            if (aoConfirmar) aoConfirmar(digitado);
        } else {
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                                             .withTitle(matriz::i18n::t("main_edit.titulo"))
                                             .withMessage(matriz::i18n::t("main_edit.barreira_errada"))
                                             .withButton(matriz::i18n::t("dialogo.ok")),
                                         juce::ModalCallbackFunction::create([](int) {}));
        }
    }), true);
}

} // namespace matriz::ui
