#include "JanelaContainerDdp.h"

#include "../I18n/Strings.h"
#include "Moldura.h"
#include "ProjetoAberto.h"

namespace matriz::ui {

using matriz::i18n::t;

namespace {
constexpr int kLarguraLista = 220, kLarguraPlayer = 700, kLarguraMetadados = 320, kEspaco = 12, kAlturaCorpo = 560;
}

JanelaContainerDdp::JanelaContainerDdp(ProjetoAberto& projeto, const std::string& nestId, const juce::File& pastaDdp) : projeto_(projeto) {
    for (const auto& id : projeto_.membrosDoNest(nestId)) {
        Arquivo a;
        a.itemId = id;
        if (auto r = projeto_.obterItemResumo(id))
            a.nome = juce::String::fromUTF8((r->nomeOriginalArquivo.empty() ? r->titulo : r->nomeOriginalArquivo).c_str());
        if (a.nome.isEmpty()) a.nome = juce::String::fromUTF8(id.c_str());
        arquivos_.push_back(std::move(a));
    }

    player_ = std::make_unique<JanelaPlayerDdp>(pastaDdp, true);
    if (!player_->carregado()) { erro_ = player_->erro(); return; }
    addAndMakeVisible(*player_);

    metadados_ = std::make_unique<PainelMetadadosArquivo>(projeto_);
    addAndMakeVisible(*metadados_);

    lista_.setModel(this);
    lista_.setRowHeight(30);
    lista_.setOutlineThickness(0);
    const auto pal = moldura::paleta();
    lista_.setColour(juce::ListBox::backgroundColourId, pal.campo);
    lista_.setColour(juce::ListBox::outlineColourId, pal.linha);
    addAndMakeVisible(lista_);

    const int larguraCorpo = kLarguraLista + kLarguraPlayer + kLarguraMetadados + 2 * kEspaco + 2 * moldura::kMargemCorpo;
    setSize(moldura::tamanhoComMargem(larguraCorpo), moldura::tamanhoComMargem(moldura::kAlturaCabecalho + kAlturaCorpo + 2 * moldura::kMargemCorpo));
    if (!arquivos_.empty()) lista_.selectRow(0);
}

JanelaContainerDdp::~JanelaContainerDdp() = default;

bool JanelaContainerDdp::abrir(ProjetoAberto& projeto, const std::string& nestId, const juce::File& pastaDdp, juce::String& erro) {
    auto* janela = new JanelaContainerDdp(projeto, nestId, pastaDdp);
    if (!janela->carregado()) {
        erro = janela->erro();
        delete janela;
        return false;
    }
    moldura::abrirJanela(janela, t("ddp.title"), true);
    return true;
}

void JanelaContainerDdp::paint(juce::Graphics& g) {
    const auto pal = moldura::paleta();
    g.fillAll(pal.fundo);
    const auto geo = moldura::geometria(moldura::areaDoCartaoNativo(getLocalBounds()), false);
    moldura::desenhar(g, geo, moldura::Secao::Auto, t("ddp.trail"), t("ddp.title"), false);
    if (areaLista_.isEmpty()) return;
    // Títulos discretos das colunas laterais.
    g.setColour(pal.mudo);
    g.setFont(fonteUi(10.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
    g.drawText(moldura::caixaAlta(t("ddp.container.arquivos")), areaLista_.getX(), areaLista_.getY() - 18, areaLista_.getWidth(), 16, juce::Justification::centredLeft, false);
    g.drawText(moldura::caixaAlta(t("ddp.container.metadados")), areaMetadados_.getX(), areaMetadados_.getY() - 18, areaMetadados_.getWidth(), 16, juce::Justification::centredLeft, false);
}

void JanelaContainerDdp::resized() {
    const auto geo = moldura::geometria(moldura::areaDoCartaoNativo(getLocalBounds()), false);
    auto corpo = geo.corpo.reduced(moldura::kMargemCorpo);
    corpo.removeFromTop(18);  // espaço dos títulos das colunas
    areaLista_ = corpo.removeFromLeft(kLarguraLista);
    corpo.removeFromLeft(kEspaco);
    areaMetadados_ = corpo.removeFromRight(kLarguraMetadados);
    corpo.removeFromRight(kEspaco);
    areaPlayer_ = corpo;
    lista_.setBounds(areaLista_);
    if (player_) player_->setBounds(areaPlayer_.withTrimmedTop(-18));
    if (metadados_) metadados_->setBounds(areaMetadados_);
}

void JanelaContainerDdp::mouseDown(const juce::MouseEvent& e) {
    if (moldura::geometria(moldura::areaDoCartaoNativo(getLocalBounds()), false).botaoFechar.contains(e.getPosition()))
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
}

void JanelaContainerDdp::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selecionada) {
    if (row < 0 || row >= static_cast<int>(arquivos_.size())) return;
    const auto pal = moldura::paleta();
    if (selecionada) g.fillAll(pal.grafite.withAlpha(0.14f));
    else if (row % 2) g.fillAll(pal.poco.withAlpha(0.5f));
    g.setColour(pal.tinta);
    g.setFont(fonteUi(13.0f));
    g.drawText(arquivos_[static_cast<size_t>(row)].nome, 8, 0, width - 12, height, juce::Justification::centredLeft, true);
}

void JanelaContainerDdp::selectedRowsChanged(int ultimaLinha) {
    if (metadados_ == nullptr) return;
    if (ultimaLinha >= 0 && ultimaLinha < static_cast<int>(arquivos_.size()))
        metadados_->mostrarItem(arquivos_[static_cast<size_t>(ultimaLinha)].itemId);
    else
        metadados_->mostrarItem({});
}

}  // namespace matriz::ui
