#include "ConflitosMergeDialog.h"

#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

namespace {
constexpr int kAlturaLinha = 58;
constexpr int kGrupoRadio = 7301;
}

class ConflitosMergeDialog::LinhaComponent : public juce::Component {
public:
    LinhaComponent(const Linha& l, int indice) {
        const auto& tk = tema();
        rotulo_.setText(rotuloDoCampo(l.campo), juce::dontSendNotification);
        rotulo_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        rotulo_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(rotulo_);
        for (auto* b : {&mantido_, &outro_}) {
            b->setRadioGroupId(kGrupoRadio + indice);
            b->setClickingTogglesState(true);
            addAndMakeVisible(*b);
        }
        mantido_.setButtonText(l.valorMantido.isEmpty() ? juce::String("-") : l.valorMantido);
        outro_.setButtonText(l.valorOutro.isEmpty() ? juce::String("-") : l.valorOutro);
        mantido_.setTooltip(l.valorMantido);
        outro_.setTooltip(l.valorOutro);
        mantido_.setToggleState(true, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(tema().borda.withAlpha(0.5f));
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    }

    void resized() override {
        auto r = getLocalBounds().reduced(6, 4);
        rotulo_.setBounds(r.removeFromTop(18));
        const int metade = r.getWidth() / 2;
        mantido_.setBounds(r.removeFromLeft(metade).reduced(2, 0));
        outro_.setBounds(r.reduced(2, 0));
    }

    bool outroEscolhido() const { return outro_.getToggleState(); }
    void escolherOutro() { outro_.setToggleState(true, juce::sendNotificationSync); }

private:
    juce::Label rotulo_;
    juce::ToggleButton mantido_, outro_;
};

ConflitosMergeDialog::ConflitosMergeDialog(const juce::String& intro, const juce::String& tituloMantido,
                                           const juce::String& tituloOutro, std::vector<Linha> linhas)
    : linhas_(std::move(linhas)) {
    const auto& tk = tema();
    lblIntro_.setText(intro, juce::dontSendNotification);
    lblIntro_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblIntro_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(lblIntro_);
    for (auto [l, t] : {std::pair<juce::Label*, const juce::String*>{&lblMantido_, &tituloMantido},
                        std::pair<juce::Label*, const juce::String*>{&lblOutro_, &tituloOutro}}) {
        l->setText(*t, juce::dontSendNotification);
        l->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        l->setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(*l);
    }
    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
        auto* c = componentes_.add(new LinhaComponent(linhas_[static_cast<size_t>(i)], i));
        lista_.addAndMakeVisible(c);
    }
    viewport_.setViewedComponent(&lista_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    btnCancelar_.setButtonText(matriz::i18n::t("dialogo.cancelar"));
    btnCancelar_.onClick = [this] {
        concluido_ = true;
        auto cb = aoConcluir_;
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
        if (cb) cb(false, {});
    };
    addAndMakeVisible(btnCancelar_);
    btnConfirmar_.setButtonText(matriz::i18n::t("merge.confirmar"));
    btnConfirmar_.onClick = [this] { confirmar(); };
    addAndMakeVisible(btnConfirmar_);
}

ConflitosMergeDialog::~ConflitosMergeDialog() {
    // Janela fechada no X/Esc sem escolher: conta como cancelar.
    if (!concluido_ && aoConcluir_) aoConcluir_(false, {});
}

void ConflitosMergeDialog::paint(juce::Graphics& g) { g.fillAll(tema().painel); }

void ConflitosMergeDialog::resized() {
    auto r = getLocalBounds().reduced(16);
    lblIntro_.setBounds(r.removeFromTop(48));
    r.removeFromTop(6);
    auto cab = r.removeFromTop(20).reduced(8, 0);
    lblMantido_.setBounds(cab.removeFromLeft(cab.getWidth() / 2));
    lblOutro_.setBounds(cab);
    auto botoes = r.removeFromBottom(32);
    btnConfirmar_.setBounds(botoes.removeFromRight(150));
    botoes.removeFromRight(8);
    btnCancelar_.setBounds(botoes.removeFromRight(110));
    r.removeFromBottom(8);
    viewport_.setBounds(r);
    const int largura = r.getWidth() - viewport_.getScrollBarThickness();
    lista_.setSize(largura, kAlturaLinha * componentes_.size());
    for (int i = 0; i < componentes_.size(); ++i) componentes_[i]->setBounds(0, i * kAlturaLinha, largura, kAlturaLinha);
}

void ConflitosMergeDialog::confirmar() {
    std::set<std::string> trocadas;
    for (int i = 0; i < componentes_.size(); ++i)
        if (componentes_[i]->outroEscolhido()) trocadas.insert(linhas_[static_cast<size_t>(i)].chave);
    concluido_ = true;
    auto cb = aoConcluir_;
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(1);
    if (cb) cb(true, trocadas);
}

void ConflitosMergeDialog::escolherOutroParaTeste(int indice) {
    if (indice >= 0 && indice < componentes_.size()) componentes_[indice]->escolherOutro();
}

juce::String ConflitosMergeDialog::rotuloDoCampo(const std::string& campo) {
    const juce::String chave = "merge.campo." + juce::String(campo);
    if (matriz::i18n::existe(chave)) return matriz::i18n::t(chave);
    juce::String c(campo);
    if (c.startsWith("dc_")) c = c.substring(3);
    return c.replaceCharacter('_', ' ').toUpperCase();
}

void ConflitosMergeDialog::mostrar(const juce::String& tituloJanela, const juce::String& intro,
                                   const juce::String& tituloMantido, const juce::String& tituloOutro,
                                   std::vector<Linha> linhas, std::function<void(bool, std::set<std::string>)> aoConcluir) {
    auto dlg = std::make_unique<ConflitosMergeDialog>(intro, tituloMantido, tituloOutro, std::move(linhas));
    dlg->aoConcluir_ = std::move(aoConcluir);
    dlg->setSize(760, juce::jlimit(260, 640, 150 + kAlturaLinha * dlg->componentes_.size()));
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dlg.release());
    opt.dialogTitle = tituloJanela;
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;
    opt.launchAsync();
}

}  // namespace matriz::ui
