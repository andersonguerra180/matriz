#include "NotesEstruturadasComponent.h"
#include "Tokens.h"

#include <algorithm>

namespace matriz::ui {

namespace {

constexpr int kAlturaCabecalho = 22;
constexpr int kAlturaCorpoEditavel = 84;
constexpr int kAlturaBotaoAddNote = 18;
constexpr int kLarguraBotaoAddNote = 86;
constexpr int kEspacoEntreLinhas = 6;
constexpr int kLarguraBotaoRemover = 20;
constexpr int kPadCard = 6; // respiro entre a borda do sub-card e o conteúdo

int alturaTextoMultilinha(const juce::String& texto, float largura, const juce::Font& fonte) {
    if (texto.isEmpty() || largura <= 1.0f) return 0;
    juce::AttributedString as;
    as.setText(texto);
    as.setFont(fonte);
    juce::TextLayout layout;
    layout.createLayout(as, largura);
    return juce::roundToInt(layout.getHeight()) + 4;
}

} // namespace

NotesEstruturadasComponent::NotesEstruturadasComponent(bool isPt) : isPt_(isPt) {
    conteudoInterno_ = std::make_unique<juce::Component>();
    viewport_ = std::make_unique<juce::Viewport>();
    viewport_->setViewedComponent(conteudoInterno_.get(), false);
    viewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*viewport_);

    botaoAddNote_ = std::make_unique<juce::TextButton>(isPt ? "+ ADICIONAR NOTA" : "+ ADD NOTE");
    const auto& tk = tema();
    botaoAddNote_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    botaoAddNote_->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
    botaoAddNote_->onClick = [this] {
        // O quarto argumento é o componente de referência: a AlertWindow se
        // centraliza nele (e não no meio da tela), já limitada à área útil do
        // monitor — ver TopLevelWindow::centreAroundComponent.
        juce::AlertWindow dlg(isPt_ ? "Nova Seção de Nota" : "New Note Section",
                               isPt_ ? "Título da seção (ex.: TECHNICAL NOTES):" : "Section title (e.g. TECHNICAL NOTES):",
                               juce::MessageBoxIconType::NoIcon,
                               this);
        dlg.addTextEditor("titulo", "", "");
        dlg.addButton(isPt_ ? "Criar" : "Create", 1);
        dlg.addButton(isPt_ ? "Cancelar" : "Cancel", 0);
        if (dlg.runModalLoop() != 1) return;
        juce::String titulo = dlg.getTextEditorContents("titulo").trim();
        if (titulo.isEmpty()) return;

        auto linha = std::make_unique<LinhaSecao>();
        linha->dados.titulo = titulo.toUpperCase().toStdString();
        linha->dados.conteudo = "";
        linha->dados.automatica = false;
        linha->colapsada = false;
        linhas_.push_back(std::move(linha));
        reconstruirLinhas();
        resized();
        commit();
    };
    conteudoInterno_->addAndMakeVisible(*botaoAddNote_);
}

NotesEstruturadasComponent::~NotesEstruturadasComponent() = default;

void NotesEstruturadasComponent::setTexto(const std::string& texto) {
    linhas_.clear();
    auto secoes = matriz::model::parseNotasEstruturadas(texto);
    for (auto& s : secoes) {
        auto linha = std::make_unique<LinhaSecao>();
        linha->dados = s;
        (void) s;
        linha->colapsada = true; // todas recolhidas, inclusive OTHER METADATA
        linhas_.push_back(std::move(linha));
    }
    reconstruirLinhas();
    resized();
}

std::string NotesEstruturadasComponent::getTexto() const {
    std::vector<matriz::model::SecaoNota> secoes;
    secoes.reserve(linhas_.size());
    for (auto& l : linhas_) {
        matriz::model::SecaoNota s = l->dados;
        if (l->corpoEditavel) s.conteudo = l->corpoEditavel->getText().toStdString();
        secoes.push_back(std::move(s));
    }
    return matriz::model::serializarNotasEstruturadas(secoes);
}

void NotesEstruturadasComponent::commit() {
    if (onCommit) onCommit();
}

void NotesEstruturadasComponent::atualizarTextoCabecalho(LinhaSecao& linha) {
    if (!linha.cabecalho) return;
    juce::String seta = linha.colapsada ? juce::String::fromUTF8("\xe2\x96\xb6") : juce::String::fromUTF8("\xe2\x96\xbc");
    linha.cabecalho->setButtonText(seta + " " + juce::String(linha.dados.titulo));
}

void NotesEstruturadasComponent::reconstruirLinhas() {
    const auto& tk = tema();
    for (auto& linha : linhas_) {
        LinhaSecao* l = linha.get();

        l->cabecalho = std::make_unique<juce::TextButton>();
        l->cabecalho->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        l->cabecalho->setColour(juce::TextButton::textColourOffId, l->dados.automatica ? tk.campoLeituraTecnica : tk.textoPrimario);
        atualizarTextoCabecalho(*l);
        l->cabecalho->onClick = [this, l] {
            l->colapsada = !l->colapsada;
            atualizarTextoCabecalho(*l);
            if (l->corpoSomenteLeitura) l->corpoSomenteLeitura->setVisible(!l->colapsada);
            if (l->corpoEditavel) l->corpoEditavel->setVisible(!l->colapsada);
            resized();
        };
        conteudoInterno_->addAndMakeVisible(*l->cabecalho);

        if (l->dados.automatica) {
            l->corpoSomenteLeitura = std::make_unique<juce::Label>();
            l->corpoSomenteLeitura->setText(juce::String(l->dados.conteudo), juce::dontSendNotification);
            l->corpoSomenteLeitura->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            l->corpoSomenteLeitura->setColour(juce::Label::textColourId, tk.campoLeituraTecnica);
            l->corpoSomenteLeitura->setColour(juce::Label::backgroundColourId, tk.painelAlt.withAlpha(0.6f));
            l->corpoSomenteLeitura->setJustificationType(juce::Justification::topLeft);
            l->corpoSomenteLeitura->setMinimumHorizontalScale(1.0f);
            l->corpoSomenteLeitura->setVisible(!l->colapsada);
            conteudoInterno_->addAndMakeVisible(*l->corpoSomenteLeitura);
        } else {
            l->botaoRemover = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xc3\x97"));
            l->botaoRemover->setTooltip(isPt_ ? "Remover esta seção de nota" : "Remove this note section");
            l->botaoRemover->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            l->botaoRemover->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
            l->botaoRemover->onClick = [this, l] {
                linhas_.erase(std::remove_if(linhas_.begin(), linhas_.end(),
                                              [l](const std::unique_ptr<LinhaSecao>& p) { return p.get() == l; }),
                              linhas_.end());
                reconstruirLinhas();
                resized();
                commit();
            };
            conteudoInterno_->addAndMakeVisible(*l->botaoRemover);

            l->corpoEditavel = std::make_unique<juce::TextEditor>();
            l->corpoEditavel->setMultiLine(true, true);
            l->corpoEditavel->setReturnKeyStartsNewLine(true);
            l->corpoEditavel->setText(juce::String(l->dados.conteudo), false);
            l->corpoEditavel->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            l->corpoEditavel->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            l->corpoEditavel->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            l->corpoEditavel->setColour(juce::TextEditor::outlineColourId, tk.borda);
            l->corpoEditavel->onFocusLost = [this, l] {
                l->dados.conteudo = l->corpoEditavel->getText().toStdString();
                commit();
            };
            l->corpoEditavel->setVisible(!l->colapsada);
            conteudoInterno_->addAndMakeVisible(*l->corpoEditavel);
            if (!l->colapsada) l->corpoEditavel->grabKeyboardFocus();
        }
    }
}

void NotesEstruturadasComponent::resized() {
    auto area = getLocalBounds().reduced(kPadCard);
    viewport_->setBounds(area);

    int largura = std::max(20, area.getWidth() - viewport_->getScrollBarThickness() - 4);
    const auto& tk = tema();
    int y = 0;

    for (auto& linha : linhas_) {
        LinhaSecao* l = linha.get();
        if (l->dados.automatica) {
            l->cabecalho->setBounds(0, y, largura, kAlturaCabecalho);
            y += kAlturaCabecalho;
            if (!l->colapsada && l->corpoSomenteLeitura) {
                int alturaTexto = alturaTextoMultilinha(juce::String(l->dados.conteudo), (float) (largura - 12),
                                                         juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
                l->corpoSomenteLeitura->setBounds(0, y, largura, std::max(20, alturaTexto));
                y += std::max(20, alturaTexto) + kEspacoEntreLinhas;
            } else {
                y += kEspacoEntreLinhas;
            }
        } else {
            l->cabecalho->setBounds(0, y, largura - kLarguraBotaoRemover - 4, kAlturaCabecalho);
            if (l->botaoRemover) l->botaoRemover->setBounds(largura - kLarguraBotaoRemover, y, kLarguraBotaoRemover, kAlturaCabecalho);
            y += kAlturaCabecalho;
            if (!l->colapsada && l->corpoEditavel) {
                l->corpoEditavel->setBounds(0, y, largura, kAlturaCorpoEditavel);
                y += kAlturaCorpoEditavel + kEspacoEntreLinhas;
            } else {
                y += kEspacoEntreLinhas;
            }
        }
    }

    // Botão discreto, encostado no canto direito da janela de notas.
    botaoAddNote_->setBounds(std::max(0, largura - kLarguraBotaoAddNote), y,
                             std::min(largura, kLarguraBotaoAddNote), kAlturaBotaoAddNote);
    y += kAlturaBotaoAddNote;

    conteudoInterno_->setSize(largura, y);
    conteudoInterno_->setBounds(0, 0, area.getWidth() - viewport_->getScrollBarThickness(), y);
}

void NotesEstruturadasComponent::paint(juce::Graphics& g) {
    // NOTES é um sub-card dentro da ficha: fundo um tom acima do painel e
    // uma borda discreta, para separar visualmente das seções vizinhas.
    const auto& tk = tema();
    auto r = getLocalBounds().toFloat();
    g.setColour(tk.painelAlt.withAlpha(0.35f));
    g.fillRoundedRectangle(r, tk.raioPequeno);
    g.setColour(tk.borda.withAlpha(0.7f));
    g.drawRoundedRectangle(r.reduced(0.5f), tk.raioPequeno, 1.0f);
}

} // namespace matriz::ui
