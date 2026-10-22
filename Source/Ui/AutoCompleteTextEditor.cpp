#include "../Model/NomesCanonicos.h"
#include "AutoCompleteTextEditor.h"

#include "Tokens.h"
#include "../Diag/Watchdog.h"

#include <algorithm>

namespace matriz::ui {

namespace {
constexpr int kMaxSugestoes = 24;
constexpr int kAlturaChip = 28;
constexpr int kPadHorizontalChip = 14;
constexpr int kGapChip = 6;
constexpr int kMargemPopup = 6;
} // namespace

// O popup é UM componente que desenha e testa o clique de todos os chips (sem componentes filhos): o chip
// inteiro — espaço em volta do texto incluído — e até o espaçamento entre chips aceitam o clique (vai para o chip
// mais próximo). Não pede foco de teclado, então clicar nunca tira o foco do TextEditor (evita a corrida de
// popup que fecha antes do clique ser processado).
class AutoCompleteTextEditor::Popup : public juce::Component {
public:
    Popup() {
        setWantsKeyboardFocus(false);
        // Sem isto o JUCE move o foco no mouseDown, o TextEditor perde o foco e esconde — destrói — o popup
        // antes do mouseUp.
        setMouseClickGrabsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void definirValores(const std::vector<juce::String>& valores, int largura,
                         std::function<void(const juce::String&)> onSelecionar) {
        valores_ = valores;
        onSelecionar_ = std::move(onSelecionar);
        destaque_ = -1;
        hover_ = -1;
        const auto fonte = juce::Font(juce::FontOptions(tema().tamanhoFontePequena));
        const int larguraUtil = juce::jmax(kPadHorizontalChip * 2 + 40, largura) - 2 * kMargemPopup;
        chips_.clear();
        int x = kMargemPopup, y = kMargemPopup;
        for (const auto& v : valores_) {
            int w = kPadHorizontalChip * 2 + juce::GlyphArrangement::getStringWidthInt(fonte, v) + 2;
            w = juce::jmin(w, larguraUtil);
            if (x + w > kMargemPopup + larguraUtil && x > kMargemPopup) {
                x = kMargemPopup;
                y += kAlturaChip + kGapChip;
            }
            chips_.push_back({x, y, w, kAlturaChip});
            x += w + kGapChip;
        }
        setSize(largura, y + kAlturaChip + kMargemPopup);
        repaint();
    }

    // Teclado (do TextEditor dono): setas movem o destaque; devolve o valor destacado (ou vazio).
    void mover(int delta) {
        if (chips_.empty()) return;
        destaque_ = destaque_ < 0 ? (delta > 0 ? 0 : static_cast<int>(chips_.size()) - 1)
                                  : (destaque_ + delta + static_cast<int>(chips_.size())) % static_cast<int>(chips_.size());
        repaint();
    }
    juce::String valorDestacado() const {
        return destaque_ >= 0 && destaque_ < static_cast<int>(valores_.size()) ? valores_[static_cast<size_t>(destaque_)] : juce::String();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.setColour(tk.painel);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioPequeno);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
            const auto r = chips_[static_cast<size_t>(i)].toFloat();
            const bool ativo = (i == hover_ || i == destaque_);
            // Mesmo visual dos chips do TagChipsEditor (pílula com o acento translúcido).
            g.setColour(ativo ? tk.acento.brighter(0.3f) : tk.acento.withAlpha(0.25f));
            g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
            g.setColour(ativo ? tk.textoSobreAcento : tk.textoPrimario);
            g.drawText(valores_[static_cast<size_t>(i)], chips_[static_cast<size_t>(i)].reduced(kPadHorizontalChip, 0),
                       juce::Justification::centredLeft, true);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override { atualizarHover(e.getPosition()); }
    void mouseDrag(const juce::MouseEvent& e) override { atualizarHover(e.getPosition()); }
    void mouseExit(const juce::MouseEvent&) override { if (hover_ != -1) { hover_ = -1; repaint(); } }
    void mouseUp(const juce::MouseEvent& e) override {
        const int i = chipMaisProximo(e.getPosition());
        if (i < 0) return;
        // O callback esconde (destrói) este popup: copiar valor e callback antes, para não ler nada de `this` depois.
        auto callback = onSelecionar_;
        const juce::String valor = valores_[static_cast<size_t>(i)];
        if (callback) callback(valor);
    }

private:
    void atualizarHover(juce::Point<int> p) {
        const int i = chipMaisProximo(p);
        if (i != hover_) { hover_ = i; repaint(); }
    }
    // Chip sob o ponto ou, se o ponto cai no espaço entre chips/margem, o mais próximo (área de clique generosa).
    int chipMaisProximo(juce::Point<int> p) const {
        int melhor = -1;
        float menor = 1.0e9f;
        for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
            const auto& r = chips_[static_cast<size_t>(i)];
            const float dx = static_cast<float>(juce::jmax(r.getX() - p.x, 0, p.x - r.getRight()));
            const float dy = static_cast<float>(juce::jmax(r.getY() - p.y, 0, p.y - r.getBottom()));
            const float d = dx * dx + dy * dy;
            if (d < menor) { menor = d; melhor = i; }
        }
        return (melhor >= 0 && menor <= static_cast<float>(kGapChip * kGapChip)) ? melhor : -1;
    }

    std::vector<juce::String> valores_;
    std::vector<juce::Rectangle<int>> chips_;
    std::function<void(const juce::String&)> onSelecionar_;
    int hover_ = -1;
    int destaque_ = -1;
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

// Teclado: Down/Up percorrem as sugestões; Enter aceita a destacada; Esc fecha (textEditorEscapeKeyPressed).
bool AutoCompleteTextEditor::keyPressed(const juce::KeyPress& k) {
    if (popup_ != nullptr) {
        if (k == juce::KeyPress::downKey) { popup_->mover(1); return true; }
        if (k == juce::KeyPress::upKey) { popup_->mover(-1); return true; }
        if (k == juce::KeyPress::returnKey) {
            const auto escolhido = popup_->valorDestacado();
            if (escolhido.isNotEmpty()) { selecionarValor(escolhido); return true; }
        }
    }
    return juce::TextEditor::keyPressed(k);
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
        if (filtro.isEmpty() || juce::String(matriz::model::nomes::chave(v.toStdString())).contains(juce::String(matriz::model::nomes::chave(filtro.toStdString())))) filtrados.push_back(v);
    }
    std::sort(filtrados.begin(), filtrados.end(),
              [](const juce::String& a, const juce::String& b) { return a.compareIgnoreCase(b) < 0; });
    if (filtrados.size() > static_cast<size_t>(kMaxSugestoes)) filtrados.resize(kMaxSugestoes);

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
        "[Autocomplete] selecionarValor: valorCopia='" + valorCopia +
        "' getText()_apos_setText='" + getText() + "' temOnReturnKey=" + juce::String((int)(bool)onReturnKey) +
        " temOnFocusLost=" + juce::String((int)(bool)onFocusLost));
    // Dispara os MESMOS callbacks que Enter/blur disparariam — nenhum
    // caminho de salvamento novo. onTextChange cobre o DEVICE
    // (OriginalSourceMediumEditorComponent, que salva a cada tecla);
    // onReturnKey/onFocusLost cobre os campos de ficha (single/lote), que
    // salvam no blur/Enter (ver Fase 0).
    // onReturnKey/onFocusLost podem reconstruir a ficha e destruir ESTE
    // editor — nada de tocar em `this` depois sem conferir.
    juce::Component::SafePointer<AutoCompleteTextEditor> vivo(this);
    if (onTextChange) onTextChange();
    if (vivo == nullptr) return;
    if (onReturnKey) onReturnKey();
    else if (onFocusLost) onFocusLost();
    if (vivo == nullptr) return;
    matriz::diag::WatchdogLogger::getInstance().log(
        "[Autocomplete] selecionarValor: FIM getText()='" + getText() + "'");
    grabKeyboardFocus();
}

} // namespace matriz::ui
