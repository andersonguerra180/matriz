#include "IntakeWorkspaceComponent.h"

#include "FichaPanelComponent.h"
#include "MosaicoComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"

namespace matriz::ui {

IntakeWorkspaceComponent::IntakeWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    const auto& tk = tema();

    lblTitulo_ = std::make_unique<juce::Label>("", "INTAKE");
    lblTitulo_->setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    lblSubtitulo_ = std::make_unique<juce::Label>("", "All ingested files pass through Intake before entering the GRID. Confirm each with '+' or in batch.");
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblSubtitulo_);

    lblContador_ = std::make_unique<juce::Label>("", "0 in intake");
    lblContador_->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblContador_->setColour(juce::Label::textColourId, tk.acento);
    lblContador_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lblContador_);

    btnIngerir_ = std::make_unique<juce::TextButton>("+ Ingest Files");
    btnIngerir_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnIngerir_->setColour(juce::TextButton::textColourOffId, tk.acento);
    btnIngerir_->onClick = [this] { if (aoPedirIngerirArquivos) aoPedirIngerirArquivos(); };
    addAndMakeVisible(*btnIngerir_);

    btnConfirmarSelecao_ = std::make_unique<juce::TextButton>("+ Confirm Selected to GRID");
    btnConfirmarSelecao_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22c55e));
    btnConfirmarSelecao_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnConfirmarSelecao_->onClick = [this] { confirmarSelecaoParaGrid(); };
    addAndMakeVisible(*btnConfirmarSelecao_);

    btnConfirmarTodos_ = std::make_unique<juce::TextButton>("+ Confirm All to GRID");
    btnConfirmarTodos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnConfirmarTodos_->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff22c55e));
    btnConfirmarTodos_->onClick = [this] { confirmarTodosParaGrid(); };
    addAndMakeVisible(*btnConfirmarTodos_);

    btnSelecionarTodos_ = std::make_unique<juce::TextButton>("Select All");
    btnSelecionarTodos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnSelecionarTodos_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnSelecionarTodos_->onClick = [this] { if (mosaico_) mosaico_->selecionarTodos(); };
    addAndMakeVisible(*btnSelecionarTodos_);

    btnLimparSelecao_ = std::make_unique<juce::TextButton>("Deselect");
    btnLimparSelecao_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnLimparSelecao_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
    btnLimparSelecao_->onClick = [this] { if (mosaico_) mosaico_->limparSelecao(); };
    addAndMakeVisible(*btnLimparSelecao_);

    mosaico_ = std::make_unique<MosaicoComponent>(projeto_);
    mosaico_->definirModoQuarentena(true);

    mosaico_->aoConfirmarEntradaGrid = [this](const std::string& itemId) {
        projeto_.confirmarItemGrid(itemId);
        recarregar();
        if (aoConfirmarParaGrid) aoConfirmarParaGrid();
    };

    mosaico_->aoSelecionar = [this](const std::string& itemId) {
        if (fichaPanel_) fichaPanel_->mostrarItem(itemId);
    };

    mosaico_->aoMudarSelecao = [this] {
        atualizarStatusContagem();
        if (fichaPanel_ && mosaico_) {
            auto sel = mosaico_->itensSelecionados();
            if (sel.size() > 1) {
                std::vector<std::string> ids(sel.begin(), sel.end());
                fichaPanel_->mostrarSelecao(ids);
            } else if (sel.size() == 1) {
                fichaPanel_->mostrarItem(*sel.begin());
            } else {
                fichaPanel_->mostrarItem("");
            }
        }
    };

    mosaico_->aoMudarConteudoVisivel = [this] {
        atualizarStatusContagem();
    };

    mosaicoViewport_ = std::make_unique<juce::Viewport>();
    mosaicoViewport_->setViewedComponent(mosaico_.get(), false);
    mosaicoViewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*mosaicoViewport_);

    fichaPanel_ = std::make_unique<FichaPanelComponent>(projeto_);
    addAndMakeVisible(*fichaPanel_);

    recarregar();
}

IntakeWorkspaceComponent::~IntakeWorkspaceComponent() = default;

void IntakeWorkspaceComponent::recarregar() {
    if (mosaico_) {
        mosaico_->recarregar();
    }
    atualizarStatusContagem();
}

std::set<std::string> IntakeWorkspaceComponent::itensSelecionados() const {
    return mosaico_ ? mosaico_->itensSelecionados() : std::set<std::string>{};
}

void IntakeWorkspaceComponent::confirmarSelecaoParaGrid() {
    if (!mosaico_) return;
    auto sel = mosaico_->itensSelecionados();
    if (sel.empty()) return;
    std::vector<std::string> ids(sel.begin(), sel.end());
    projeto_.confirmarLoteGrid(ids);
    recarregar();
    if (aoConfirmarParaGrid) aoConfirmarParaGrid();
}

void IntakeWorkspaceComponent::confirmarTodosParaGrid() {
    auto quarentenaItens = projeto_.listarItensEmQuarentena();
    if (quarentenaItens.empty()) return;
    std::vector<std::string> ids;
    ids.reserve(quarentenaItens.size());
    for (const auto& item : quarentenaItens) {
        ids.push_back(item.id);
    }
    projeto_.confirmarLoteGrid(ids);
    recarregar();
    if (aoConfirmarParaGrid) aoConfirmarParaGrid();
}

void IntakeWorkspaceComponent::atualizarStatusContagem() {
    if (!lblContador_ || !mosaico_) return;
    int totalQuarentena = mosaico_->totalItensCarregados();
    int selecionadosCount = static_cast<int>(mosaico_->itensSelecionados().size());

    if (totalQuarentena == 0) {
        lblContador_->setText("0 in intake (GRID clear)", juce::dontSendNotification);
    } else if (selecionadosCount > 0) {
        lblContador_->setText(juce::String(selecionadosCount) + " of " + juce::String(totalQuarentena) + " selected", juce::dontSendNotification);
    } else {
        lblContador_->setText(juce::String(totalQuarentena) + " files in intake", juce::dontSendNotification);
    }
}

void IntakeWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Top header background panel
    g.setColour(tk.painel);
    g.fillRect(0, 0, getWidth(), 74);
    g.setColour(tk.borda);
    g.fillRect(0, 73, getWidth(), 1);
}

void IntakeWorkspaceComponent::resized() {
    auto area = getLocalBounds();

    // Top header bar (74px high)
    auto bar = area.removeFromTop(74).reduced(12, 6);

    auto topRow = bar.removeFromTop(26);
    lblTitulo_->setBounds(topRow.removeFromLeft(360));
    lblContador_->setBounds(topRow.removeFromRight(220));

    auto bottomRow = bar.removeFromTop(32);
    lblSubtitulo_->setBounds(bottomRow.removeFromLeft(400));

    btnLimparSelecao_->setBounds(bottomRow.removeFromRight(85));
    bottomRow.removeFromRight(6);
    btnSelecionarTodos_->setBounds(bottomRow.removeFromRight(115));
    bottomRow.removeFromRight(12);
    btnConfirmarTodos_->setBounds(bottomRow.removeFromRight(170));
    bottomRow.removeFromRight(6);
    btnConfirmarSelecao_->setBounds(bottomRow.removeFromRight(185));
    bottomRow.removeFromRight(6);
    btnIngerir_->setBounds(bottomRow.removeFromRight(135));

    // Main workspace split: Mosaic viewport on left, Ficha panel on right (340px)
    constexpr int kLarguraFicha = 340;
    auto areaDireita = area.removeFromRight(kLarguraFicha);
    if (fichaPanel_) fichaPanel_->setBounds(areaDireita);

    if (mosaicoViewport_) {
        mosaicoViewport_->setBounds(area);
        if (mosaico_) {
            mosaico_->setBounds(0, 0, area.getWidth() - mosaicoViewport_->getScrollBarThickness(), juce::jmax(area.getHeight(), 400));
        }
    }
}

} // namespace matriz::ui
