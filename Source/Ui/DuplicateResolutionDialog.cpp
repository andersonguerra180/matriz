#include "DuplicateResolutionDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

namespace {
constexpr int kThumbSize = 48;
constexpr int kThumbGap = 4;
// Espaço suficiente pra 4 linhas de secondaryLabel quebrarem (item 6:
// caminhos completos de arquivo truncavam numa linha só de 32px).
constexpr int kRowH = 96;
}

// Overlay que cobre o dialog inteiro e mostra a imagem clicada em tamanho
// grande — mesma ideia do "clique pra ampliar" da aba DUPLICATES, só que
// como um overlay simples em vez de expandir o card inline.
class DuplicateResolutionDialog::AmpliadorComponent : public juce::Component {
public:
    explicit AmpliadorComponent(juce::Image imagem, std::function<void()> aoFechar)
        : imagem_(std::move(imagem)), aoFechar_(std::move(aoFechar)) {
        setInterceptsMouseClicks(true, false);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black.withAlpha(0.78f));
        if (!imagem_.isValid()) return;
        auto area = getLocalBounds().reduced(48);
        g.setColour(juce::Colours::black);
        g.fillRect(area);
        g.drawImageWithin(imagem_, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                           juce::RectanglePlacement::centred);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawRect(area, 1);
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (aoFechar_) aoFechar_();
    }

private:
    juce::Image imagem_;
    std::function<void()> aoFechar_;
};

class DuplicateResolutionDialog::RowComponent : public juce::Component {
public:
    RowComponent(DuplicateResolutionDialog::Entry& entry, const std::array<juce::String, 3>& actionLabels,
                 std::function<void(juce::Image)> aoAmpliar)
        : entry_(entry), aoAmpliar_(std::move(aoAmpliar)) {
        for (int i = 0; i < 3; ++i) {
            btns_[i] = std::make_unique<juce::TextButton>(actionLabels[static_cast<size_t>(i)]);
            btns_[i]->onClick = [this, i] {
                entry_.action = i;
                atualizarDestaque();
            };
            addAndMakeVisible(*btns_[i]);
        }
        atualizarDestaque();
    }

    void atualizarDestaque() {
        const auto& tk = tema();
        for (int i = 0; i < 3; ++i) {
            bool selecionado = (entry_.action == i);
            auto& b = *btns_[i];
            b.setColour(juce::TextButton::buttonColourId, selecionado ? tk.acento : juce::Colours::transparentBlack);
            b.setColour(juce::TextButton::buttonOnColourId, tk.acento);
            b.setColour(juce::TextButton::textColourOffId, selecionado ? juce::Colours::white : tk.textoSecundario);
            b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            // Toggle visual claro: preenchido + ✓ quando marcado, contorno quando não —
            // o usuário via botões "mortos" sem indicação de qual opção estava ativa.
            b.setButtonText((selecionado ? juce::String::fromUTF8("\xE2\x9C\x93 ") : juce::String())
                             + entry_.rotuloBase[static_cast<size_t>(i)]);
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.setColour(tk.painelAlt.withAlpha(0.5f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioPequeno);
        g.setColour(tk.borda.withAlpha(0.7f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);

        int thumbsW = larguraThumbs();
        int textX = 10 + thumbsW + (thumbsW > 0 ? kThumbGap : 0);
        int textW = juce::jmax(20, getWidth() - textX - 340);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(entry_.primaryLabel, textX, 8, textW, 18, juce::Justification::left, true);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        // drawFittedText quebra linha (não só elide com "...") — os caminhos
        // de arquivo do card (item 6) são longos demais pra uma linha só
        // truncada dar pra ler.
        g.drawFittedText(entry_.secondaryLabel, textX, 28, textW, getHeight() - 28 - 6,
                          juce::Justification::topLeft, 4, 1.0f);

        int tx = 10;
        int ty = getHeight() / 2 - kThumbSize / 2;
        if (entry_.imagemA.isValid()) {
            desenharThumb(g, entry_.imagemA, tx, ty);
            tx += kThumbSize + kThumbGap;
        }
        if (entry_.imagemB.isValid()) {
            desenharThumb(g, entry_.imagemB, tx, ty);
        }
    }

    void resized() override {
        int w = getWidth();
        int btnW = 108;
        int x = w - (btnW * 3 + 8 * 2) - 12;
        int y = getHeight() / 2 - 15;
        for (int i = 0; i < 3; ++i) {
            btns_[i]->setBounds(x, y, btnW, 30);
            x += btnW + 8;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int tx = 10;
        int ty = getHeight() / 2 - kThumbSize / 2;
        juce::Rectangle<int> rA(tx, ty, kThumbSize, kThumbSize);
        if (entry_.imagemA.isValid() && rA.contains(e.getPosition())) {
            if (aoAmpliar_) aoAmpliar_(entry_.imagemA);
            return;
        }
        if (entry_.imagemB.isValid()) {
            juce::Rectangle<int> rB(tx + kThumbSize + kThumbGap, ty, kThumbSize, kThumbSize);
            if (rB.contains(e.getPosition())) {
                if (aoAmpliar_) aoAmpliar_(entry_.imagemB);
            }
        }
    }

private:
    int larguraThumbs() const {
        int n = (entry_.imagemA.isValid() ? 1 : 0) + (entry_.imagemB.isValid() ? 1 : 0);
        if (n == 0) return 0;
        return n * kThumbSize + (n - 1) * kThumbGap;
    }

    void desenharThumb(juce::Graphics& g, const juce::Image& img, int x, int y) {
        const auto& tk = tema();
        juce::Rectangle<int> r(x, y, kThumbSize, kThumbSize);
        g.setColour(juce::Colours::black);
        g.fillRect(r);
        g.drawImageWithin(img, x, y, kThumbSize, kThumbSize, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        g.setColour(tk.borda.withAlpha(0.8f));
        g.drawRect(r, 1);
    }

    DuplicateResolutionDialog::Entry& entry_;
    std::array<std::unique_ptr<juce::TextButton>, 3> btns_;
    std::function<void(juce::Image)> aoAmpliar_;
};

DuplicateResolutionDialog::DuplicateResolutionDialog(juce::String intro, std::vector<Entry> entries,
                                                       std::array<juce::String, 3> actionLabels)
    : entries_(std::move(entries)), actionLabels_(std::move(actionLabels)) {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    lblIntro_ = std::make_unique<juce::Label>("", intro);
    lblIntro_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    lblIntro_->setColour(juce::Label::textColourId, tema().textoSecundario);
    lblIntro_->setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(*lblIntro_);

    lblApplyAll_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Aplicar a todos:") : "Apply to all:");
    lblApplyAll_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    lblApplyAll_->setColour(juce::Label::textColourId, tema().textoTerciario);
    addAndMakeVisible(*lblApplyAll_);

    for (int i = 0; i < 3; ++i) {
        btnsApplyAll_[static_cast<size_t>(i)] = std::make_unique<juce::TextButton>(actionLabels_[static_cast<size_t>(i)]);
        btnsApplyAll_[static_cast<size_t>(i)]->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
        btnsApplyAll_[static_cast<size_t>(i)]->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
        btnsApplyAll_[static_cast<size_t>(i)]->onClick = [this, i] { aplicarATodos(i); };
        addAndMakeVisible(*btnsApplyAll_[static_cast<size_t>(i)]);
    }

    for (auto& e : entries_) e.rotuloBase = actionLabels_;

    listaContainer_ = std::make_unique<juce::Component>();
    for (auto& entry : entries_) {
        auto* row = new RowComponent(entry, actionLabels_, [this](juce::Image img) { ampliarImagem(std::move(img)); });
        linhas_.add(row);
        listaContainer_->addAndMakeVisible(row);
    }
    viewport_ = std::make_unique<juce::Viewport>();
    viewport_->setViewedComponent(listaContainer_.get(), false);
    addAndMakeVisible(*viewport_);

    btnCancelar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("comum.cancelar"));
    btnCancelar_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnCancelar_->onClick = [this] { cancelar(); };
    addAndMakeVisible(*btnCancelar_);

    btnConfirmar_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Continuar") : "Continue");
    btnConfirmar_->setColour(juce::TextButton::buttonColourId, tema().acento);
    btnConfirmar_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnConfirmar_->onClick = [this] { confirmar(); };
    addAndMakeVisible(*btnConfirmar_);
}

DuplicateResolutionDialog::~DuplicateResolutionDialog() {
    if (!concluido_ && onDone_) onDone_(false, {});
}

void DuplicateResolutionDialog::aplicarATodos(int action) {
    for (auto& entry : entries_) entry.action = action;
    for (auto* row : linhas_) row->atualizarDestaque();
    repaint();
}

void DuplicateResolutionDialog::confirmar() {
    concluido_ = true;
    if (onDone_) onDone_(true, entries_);
    if (aoFechar) aoFechar();
}

void DuplicateResolutionDialog::cancelar() {
    concluido_ = true;
    if (onDone_) onDone_(false, {});
    if (aoFechar) aoFechar();
}

void DuplicateResolutionDialog::ampliarImagem(juce::Image imagem) {
    if (!imagem.isValid()) return;
    ampliador_ = std::make_unique<AmpliadorComponent>(std::move(imagem), [this] { fecharAmpliador(); });
    addAndMakeVisible(*ampliador_);
    ampliador_->setBounds(getLocalBounds());
}

void DuplicateResolutionDialog::fecharAmpliador() {
    ampliador_.reset();
}

void DuplicateResolutionDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);
    auto card = getLocalBounds().toFloat().reduced(8.0f);
    g.setColour(tk.painel);
    g.fillRoundedRectangle(card, tk.raioMedio);
    g.setColour(tk.borda.withAlpha(0.6f));
    g.drawRoundedRectangle(card, tk.raioMedio, 1.0f);
}

void DuplicateResolutionDialog::resized() {
    if (ampliador_) ampliador_->setBounds(getLocalBounds());

    auto area = getLocalBounds().reduced(24);

    lblIntro_->setBounds(area.removeFromTop(40));
    area.removeFromTop(8);

    auto applyAllRow = area.removeFromTop(32);
    lblApplyAll_->setBounds(applyAllRow.removeFromLeft(120));
    for (auto& btn : btnsApplyAll_) {
        btn->setBounds(applyAllRow.removeFromLeft(120));
        applyAllRow.removeFromLeft(8);
    }
    area.removeFromTop(14);

    auto bottomRow = area.removeFromBottom(40);
    btnConfirmar_->setBounds(bottomRow.removeFromRight(140));
    bottomRow.removeFromRight(8);
    btnCancelar_->setBounds(bottomRow.removeFromRight(120));
    area.removeFromBottom(10);

    viewport_->setBounds(area);
    int rowH = kRowH;
    int y = 0;
    int listW = viewport_->getWidth() - viewport_->getScrollBarThickness();
    for (auto* row : linhas_) {
        row->setBounds(0, y, listW, rowH);
        y += rowH + 8;
    }
    listaContainer_->setSize(listW, y);
}

void DuplicateResolutionDialog::show(const juce::String& windowTitle,
                                      const juce::String& intro,
                                      std::vector<Entry> entries,
                                      std::array<juce::String, 3> actionLabels,
                                      std::function<void(bool, std::vector<Entry>)> onDone) {
    auto dlg = std::make_unique<DuplicateResolutionDialog>(intro, std::move(entries), std::move(actionLabels));
    dlg->onDone_ = std::move(onDone);
    int alturaLista = static_cast<int>(dlg->entries_.size()) * (kRowH + 8);
    dlg->setSize(920, juce::jlimit(360, 700, 180 + alturaLista));
    auto* rawDlg = dlg.get();

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dlg.release());
    opt.dialogTitle = windowTitle;
    opt.dialogBackgroundColour = tema().fundo;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;

    auto* win = opt.launchAsync();
    rawDlg->aoFechar = [win] {
        if (win) win->exitModalState(0);
    };
}

} // namespace matriz::ui
