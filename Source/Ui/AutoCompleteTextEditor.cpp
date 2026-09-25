#include "AutoCompleteTextEditor.h"

#include "Tokens.h"
#include "../Diag/Watchdog.h"

#include <algorithm>

namespace matriz::ui {

namespace {
constexpr int kAlturaLinha = 22;
constexpr int kMaxLinhasVisiveis = 8;
} // namespace

// Uma linha do popup. Componente simples (não Button/ListBox) de propósito:
// não pede foco de teclado, então clicar nela nunca tira o foco do
// TextEditor — evita a corrida clássica de popup que fecha antes do clique
// na linha ser processado.
class AutoCompleteTextEditor::PopupRow : public juce::Component {
public:
    PopupRow(juce::String valor, std::function<void(const juce::String&)> onSelecionar)
        : valor_(std::move(valor)), onSelecionar_(std::move(onSelecionar)) {
        setWantsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        if (hover_) g.fillAll(tk.acento.withAlpha(0.25f));
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(valor_, getLocalBounds().reduced(6, 0), juce::Justification::centredLeft, true);
    }

    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }
    void mouseUp(const juce::MouseEvent&) override {
        matriz::diag::WatchdogLogger::getInstance().log("[Autocomplete] PopupRow::mouseUp valor_='" + valor_ + "'");
        if (onSelecionar_) onSelecionar_(valor_);
    }

private:
    juce::String valor_;
    std::function<void(const juce::String&)> onSelecionar_;
    bool hover_ = false;
};

// O popup em si — filho do top-level window (escapa do clipping do
// juce::Viewport onde a ficha vive), nunca do TextEditor.
class AutoCompleteTextEditor::Popup : public juce::Component {
public:
    Popup() { setWantsKeyboardFocus(false); }

    void definirValores(const std::vector<juce::String>& valores, int largura,
                         std::function<void(const juce::String&)> onSelecionar) {
        linhas_.clear();
        for (const auto& v : valores) {
            auto row = std::make_unique<PopupRow>(v, onSelecionar);
            addAndMakeVisible(*row);
            linhas_.push_back(std::move(row));
        }
        setSize(largura, static_cast<int>(linhas_.size()) * kAlturaLinha);
        resized();
    }

    void resized() override {
        int y = 0;
        for (auto& l : linhas_) {
            l->setBounds(0, y, getWidth(), kAlturaLinha);
            y += kAlturaLinha;
        }
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);
        g.setColour(tk.borda);
        g.drawRect(getLocalBounds());
    }

private:
    std::vector<std::unique_ptr<PopupRow>> linhas_;
};

AutoCompleteTextEditor::AutoCompleteTextEditor(std::function<std::vector<juce::String>()> provedorValores)
    : provedorValores_(std::move(provedorValores)) {
    addListener(this);
}

AutoCompleteTextEditor::~AutoCompleteTextEditor() {
    removeListener(this);
    esconderPopup();
}

void AutoCompleteTextEditor::focusGained(juce::Component::FocusChangeType cause) {
    juce::TextEditor::focusGained(cause);
    atualizarPopup();
}

void AutoCompleteTextEditor::textEditorTextChanged(juce::TextEditor&) { atualizarPopup(); }
void AutoCompleteTextEditor::textEditorReturnKeyPressed(juce::TextEditor&) { esconderPopup(); }
void AutoCompleteTextEditor::textEditorEscapeKeyPressed(juce::TextEditor&) { esconderPopup(); }
void AutoCompleteTextEditor::textEditorFocusLost(juce::TextEditor&) { esconderPopup(); }

void AutoCompleteTextEditor::atualizarPopup() {
    if (!provedorValores_ || !hasKeyboardFocus(true)) {
        matriz::diag::WatchdogLogger::getInstance().log(
            "[Autocomplete] atualizarPopup: sem provedor ou sem foco (hasFocus=" +
            juce::String((int)hasKeyboardFocus(true)) + ") — escondendo popup");
        esconderPopup();
        return;
    }

    auto todos = provedorValores_();
    juce::String filtro = getText().trim();
    std::vector<juce::String> filtrados;
    for (const auto& v : todos) {
        if (filtro.isEmpty() || v.containsIgnoreCase(filtro)) filtrados.push_back(v);
    }
    std::sort(filtrados.begin(), filtrados.end(),
              [](const juce::String& a, const juce::String& b) { return a.compareIgnoreCase(b) < 0; });
    if (filtrados.size() > static_cast<size_t>(kMaxLinhasVisiveis)) filtrados.resize(kMaxLinhasVisiveis);

    matriz::diag::WatchdogLogger::getInstance().log(
        "[Autocomplete] atualizarPopup: filtro='" + filtro + "' todos=" + juce::String((int)todos.size()) +
        " filtrados=" + juce::String((int)filtrados.size()));

    // Não sugerir a própria opção já digitada por igual — só ajuda quando
    // há algo diferente do que já está no campo.
    if (filtrados.size() == 1 && filtrados.front() == filtro) { esconderPopup(); return; }

    if (filtrados.empty()) { esconderPopup(); return; }

    auto* topLevel = getTopLevelComponent();
    if (!topLevel) { esconderPopup(); return; }

    if (!popup_) {
        popup_ = std::make_unique<Popup>();
        topLevel->addAndMakeVisible(*popup_);
    }
    popup_->definirValores(filtrados, getWidth(), [this](const juce::String& valor) { selecionarValor(valor); });
    auto topoEsquerda = topLevel->getLocalPoint(this, juce::Point<int>(0, getHeight()));
    popup_->setTopLeftPosition(topoEsquerda.x, topoEsquerda.y);
    popup_->toFront(false);
}

void AutoCompleteTextEditor::esconderPopup() { popup_.reset(); }

void AutoCompleteTextEditor::selecionarValor(const juce::String& valor) {
    // Bug (campo fica vazio ao clicar numa sugestão): `valor` é uma
    // referência pro texto guardado DENTRO da PopupRow clicada — e
    // esconderPopup() destrói exatamente essa PopupRow. Sem a cópia, tudo
    // que vem depois lê uma referência pendurada (use-after-free).
    juce::String valorCopia = valor;
    esconderPopup();
    setText(valorCopia, false);
    matriz::diag::WatchdogLogger::getInstance().log(
        "[Autocomplete] selecionarValor: valor='" + valor + "' valorCopia='" + valorCopia +
        "' getText()_apos_setText='" + getText() + "' temOnReturnKey=" + juce::String((int)(bool)onReturnKey) +
        " temOnFocusLost=" + juce::String((int)(bool)onFocusLost));
    // Dispara os MESMOS callbacks que Enter/blur disparariam — nenhum
    // caminho de salvamento novo. onTextChange cobre o DEVICE
    // (OriginalSourceMediumEditorComponent, que salva a cada tecla);
    // onReturnKey/onFocusLost cobre os campos de ficha (single/lote), que
    // salvam no blur/Enter (ver Fase 0).
    if (onTextChange) onTextChange();
    if (onReturnKey) onReturnKey();
    else if (onFocusLost) onFocusLost();
    matriz::diag::WatchdogLogger::getInstance().log(
        "[Autocomplete] selecionarValor: FIM getText()='" + getText() + "'");
    grabKeyboardFocus();
}

} // namespace matriz::ui
