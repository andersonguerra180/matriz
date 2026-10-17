#include "PastaPickerWindow.h"

#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

namespace {

struct No {
    std::string id;
    juce::String nome;
    std::vector<No> filhos;
};

No converter(const ProjetoAberto::NoArvore& n) {
    No out;
    out.id = n.id;
    out.nome = n.nome;
    for (const auto& f : n.filhos) {
        if (f.id.empty()) continue;  // nó sintético "não organizados" — não é destino
        out.filhos.push_back(converter(f));
    }
    return out;
}

// Poda a árvore: fica o nó que casa (com a subárvore inteira, pra poder navegar
// dentro dele) ou o que tem algum descendente que casa.
bool filtrar(const No& n, const juce::String& q, No& out) {
    if (n.nome.containsIgnoreCase(q)) { out = n; return true; }
    bool algum = false;
    No copia;
    copia.id = n.id;
    copia.nome = n.nome;
    for (const auto& f : n.filhos) {
        No filho;
        if (filtrar(f, q, filho)) { copia.filhos.push_back(std::move(filho)); algum = true; }
    }
    if (algum) out = std::move(copia);
    return algum;
}

}  // namespace

// ------------------------------------------------------------------ conteúdo

class PastaPickerWindow::Conteudo : public juce::Component {
public:
    std::function<void(const std::string&)> aoConfirmar;
    std::function<void()> aoCancelar;

    explicit Conteudo(const ProjetoAberto::NoArvore& raiz) : raiz_(converter(raiz)) {
        const auto& tk = tema();
        busca_.setTextToShowWhenEmpty(matriz::i18n::t("pasta_picker.buscar"), tk.textoTerciario);
        busca_.setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
        busca_.setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        busca_.setColour(juce::TextEditor::outlineColourId, tk.borda);
        busca_.setColour(juce::TextEditor::focusedOutlineColourId, tk.bordaFoco);
        busca_.onTextChange = [this] { reconstruir(); };
        busca_.onReturnKey = [this] { confirmar(); };
        busca_.onEscapeKey = [this] { if (aoCancelar) aoCancelar(); };
        addAndMakeVisible(busca_);

        arvore_.setRootItemVisible(false);
        arvore_.setDefaultOpenness(false);
        arvore_.setMultiSelectEnabled(false);
        arvore_.setIndentSize(16);
        arvore_.setColour(juce::TreeView::backgroundColourId, tk.painel);
        arvore_.setColour(juce::TreeView::selectedItemBackgroundColourId, tk.acento.withAlpha(0.45f));
        addAndMakeVisible(arvore_);

        vazio_.setText(matriz::i18n::t("pasta_picker.sem_resultados"), juce::dontSendNotification);
        vazio_.setJustificationType(juce::Justification::centred);
        vazio_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addChildComponent(vazio_);

        btnOk_.setButtonText(matriz::i18n::t("pasta_picker.mover"));
        btnOk_.setColour(juce::TextButton::buttonColourId, tk.acento);
        btnOk_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
        btnOk_.onClick = [this] { confirmar(); };
        btnOk_.setEnabled(false);
        addAndMakeVisible(btnOk_);

        btnCancelar_.setButtonText(matriz::i18n::t("dialogo.cancelar"));
        btnCancelar_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnCancelar_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnCancelar_.onClick = [this] { if (aoCancelar) aoCancelar(); };
        addAndMakeVisible(btnCancelar_);

        setSize(380, 520);
        reconstruir();
    }

    ~Conteudo() override { arvore_.setRootItem(nullptr); }

    void paint(juce::Graphics& g) override { g.fillAll(tema().fundo); }

    void resized() override {
        auto r = getLocalBounds().reduced(8);
        busca_.setBounds(r.removeFromTop(26));
        r.removeFromTop(6);
        auto rodape = r.removeFromBottom(28);
        r.removeFromBottom(6);
        btnCancelar_.setBounds(rodape.removeFromRight(90));
        rodape.removeFromRight(6);
        btnOk_.setBounds(rodape.removeFromRight(110));
        arvore_.setBounds(r);
        vazio_.setBounds(r);
    }

    void focarBusca() { busca_.grabKeyboardFocus(); }

    void selecionar(const std::string& id) {
        selecionadoId_ = id;
        btnOk_.setEnabled(!id.empty());
    }

    void confirmar() {
        // Sem seleção explícita, Enter na busca escolhe o primeiro resultado.
        if (selecionadoId_.empty() && !visiveis_.filhos.empty()) selecionadoId_ = primeiraFolha(visiveis_).id;
        if (!selecionadoId_.empty() && aoConfirmar) aoConfirmar(selecionadoId_);
    }

private:
    class Item : public juce::TreeViewItem {
    public:
        Item(Conteudo& dono, const No& no, bool abrirTudo) : dono_(dono), no_(no), abrirTudo_(abrirTudo) {}  // `no` vive em Conteudo::visiveis_

        bool mightContainSubItems() override { return !no_.filhos.empty(); }
        juce::String getUniqueName() const override { return juce::String(no_.id); }

        void paintItem(juce::Graphics& g, int w, int h) override {
            const auto& tk = tema();
            g.setColour(isSelected() ? tk.textoSobreAcento : tk.textoPrimario);
            g.setFont(tk.tamanhoFonteCorpo);
            g.drawText(no_.nome, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
        }

        void itemOpennessChanged(bool aberto) override {
            if (aberto && getNumSubItems() == 0) {
                for (const auto& f : no_.filhos) addSubItem(new Item(dono_, f, abrirTudo_));
                if (abrirTudo_)
                    for (int i = 0; i < getNumSubItems(); ++i) getSubItem(i)->setOpen(true);
            }
        }

        void itemSelectionChanged(bool selecionado) override {
            if (selecionado) dono_.selecionar(no_.id);
        }
        void itemClicked(const juce::MouseEvent&) override { dono_.selecionar(no_.id); }
        void itemDoubleClicked(const juce::MouseEvent&) override {
            dono_.selecionar(no_.id);
            dono_.confirmar();
        }

    private:
        Conteudo& dono_;
        const No& no_;
        bool abrirTudo_;
    };

    static const No& primeiraFolha(const No& n) { return n.filhos.empty() ? n : primeiraFolha(n.filhos.front()); }

    void reconstruir() {
        const auto q = busca_.getText().trim();
        const bool filtrando = q.isNotEmpty();
        arvore_.setRootItem(nullptr);
        itemRaiz_.reset();
        selecionar({});
        visiveis_ = No{};
        if (filtrando) {
            visiveis_.id = raiz_.id;
            for (const auto& f : raiz_.filhos) {
                No filho;
                if (filtrar(f, q, filho)) visiveis_.filhos.push_back(std::move(filho));
            }
        } else {
            visiveis_ = raiz_;
        }
        itemRaiz_ = std::make_unique<Item>(*this, visiveis_, filtrando);
        arvore_.setRootItem(itemRaiz_.get());
        itemRaiz_->setOpen(true);
        vazio_.setVisible(visiveis_.filhos.empty());
        arvore_.setVisible(!visiveis_.filhos.empty());
    }

    No raiz_;      // árvore completa
    No visiveis_;  // árvore após o filtro (os Items guardam cópia própria, mas a raiz precisa viver)
    juce::TextEditor busca_;
    juce::TreeView arvore_;
    juce::Label vazio_;
    juce::TextButton btnOk_, btnCancelar_;
    std::unique_ptr<Item> itemRaiz_;
    std::string selecionadoId_;
};

// ------------------------------------------------------------------ janela

namespace {
juce::Component::SafePointer<PastaPickerWindow>& janelaAberta() {
    static juce::Component::SafePointer<PastaPickerWindow> p;
    return p;
}
}  // namespace

PastaPickerWindow::PastaPickerWindow(const juce::String& titulo, const ProjetoAberto::NoArvore& raiz,
                                     AoEscolher aoEscolher)
    : juce::DocumentWindow(titulo, tema().fundo, juce::DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    auto* c = new Conteudo(raiz);
    juce::WeakReference<PastaPickerWindow> weak(this);
    c->aoCancelar = [weak] { if (auto* w = weak.get()) w->fecharAssincrono(); };
    c->aoConfirmar = [weak, escolher = std::move(aoEscolher)](const std::string& id) {
        auto* w = weak.get();
        if (!w) return;
        w->fecharAssincrono();
        if (escolher) escolher(id);
    };
    setContentOwned(c, true);
    setResizable(true, true);
    setResizeLimits(260, 280, 900, 1400);
    setAlwaysOnTop(true);

    // Perto do cursor (onde o menu abria), sem sair do monitor.
    const auto mouse = juce::Desktop::getInstance().getMousePosition();
    const auto area = juce::Desktop::getInstance().getDisplays().getDisplayForPoint(mouse.toFloat())->userBounds.toNearestInt();
    auto b = getBounds().withPosition(mouse.x - 20, mouse.y - 20);
    setBounds(b.constrainedWithin(area));
}

PastaPickerWindow::~PastaPickerWindow() = default;

void PastaPickerWindow::closeButtonPressed() { fecharAssincrono(); }

void PastaPickerWindow::fecharAssincrono() {
    // Chamado de dentro de callbacks de componentes da própria janela — destruir
    // aqui seria destruir o objeto em cima da pilha dele.
    setVisible(false);
    juce::Component::SafePointer<PastaPickerWindow> safe(this);
    juce::MessageManager::callAsync([safe] { delete safe.getComponent(); });
}

void PastaPickerWindow::abrir(const juce::String& titulo, const ProjetoAberto::NoArvore& raiz, AoEscolher aoEscolher) {
    if (auto* velha = janelaAberta().getComponent()) velha->fecharAssincrono();
    auto* w = new PastaPickerWindow(titulo, raiz, std::move(aoEscolher));
    janelaAberta() = w;
    w->setVisible(true);
    w->toFront(true);
    if (auto* c = dynamic_cast<Conteudo*>(w->getContentComponent())) c->focarBusca();
}

}  // namespace matriz::ui
