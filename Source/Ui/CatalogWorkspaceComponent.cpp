#include "CatalogWorkspaceComponent.h"
#include "AcoesItem.h"
#include "FichaPanelComponent.h"
#include "MosaicoComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../App/Preferencias.h"
#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"

#include "ArvoreBackupComponent.h"
#include "EstatisticasComponent.h"
#include "OfflineAssetRelinkDialog.h"
#include "../Vault/Resolucao.h"
#include "AssetsBinaryData.h"

namespace {

// Lupa da caixa de busca (ícone padrão: círculo + cabo a 45°). É ela que
// dispara a busca — digitar no campo não busca mais sozinho.
class LupaBuscaButton : public juce::Button {
public:
    LupaBuscaButton() : juce::Button("Search") {}

    void paintButton(juce::Graphics& g, bool destacado, bool pressionado) override {
        const auto& tk = matriz::ui::tema();
        auto r = getLocalBounds().toFloat();

        if (destacado || pressionado) {
            g.setColour(tk.painelAlt.withAlpha(pressionado ? 0.9f : 0.6f));
            g.fillRoundedRectangle(r.reduced(1.0f), 4.0f);
        }

        // Lente ocupando ~55% da menor dimensão, cabo saindo na diagonal
        // inferior direita — desenho, não glifo, para nunca depender de
        // fonte com emoji instalada.
        float lado = std::min(r.getWidth(), r.getHeight());
        float d = lado * 0.52f;
        float cx = r.getCentreX() - lado * 0.06f;
        float cy = r.getCentreY() - lado * 0.06f;
        juce::Rectangle<float> lente(cx - d * 0.5f, cy - d * 0.5f, d, d);

        g.setColour(pressionado ? tk.acento : (destacado ? tk.textoPrimario : tk.textoSecundario));
        g.drawEllipse(lente, 1.6f);

        float k = d * 0.354f; // raio * sen(45°)
        g.drawLine(cx + k, cy + k, cx + k + lado * 0.22f, cy + k + lado * 0.22f, 1.8f);
    }
};

class FichaResizerBar : public juce::Component {
public:
    std::function<void(int deltaX)> aoArrastar;

    FichaResizerBar() {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        startX_ = e.getScreenX();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        int delta = e.getScreenX() - startX_;
        startX_ = e.getScreenX();
        if (aoArrastar) aoArrastar(delta);
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = matriz::ui::tema();
        g.setColour(tk.borda);
        g.drawVerticalLine(getWidth() / 2, 0.0f, static_cast<float>(getHeight()));
    }

private:
    int startX_ = 0;
};

class ShortcutLegendComponent : public juce::Component, public juce::SettableTooltipClient {
    struct HelpCircle : public juce::Component, public juce::SettableTooltipClient {
        void paint(juce::Graphics& g) override {
            auto b = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(matriz::ui::tema().textoSecundario.withAlpha(0.6f));
            g.drawEllipse(b, 1.0f);
            g.setFont(juce::FontOptions(b.getHeight() * 0.65f, juce::Font::bold));
            g.drawText("?", b, juce::Justification::centred, false);
        }
    };
public:
    std::function<void(char)> aoClicarAtalho;

    ShortcutLegendComponent() {
        setTooltip("");
        helpCircle_ = std::make_unique<HelpCircle>();
        helpCircle_->setTooltip(matriz::i18n::t("catwork.shortcuts_help"));
        addAndMakeVisible(*helpCircle_);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int x = e.x;
        for (const auto& item : boundsAtalhos_) {
            if (x >= item.xStart && x <= item.xEnd) {
                if (aoClicarAtalho) aoClicarAtalho(item.key);
                break;
            }
        }
    }

    void paint(juce::Graphics& g) override {
        boundsAtalhos_.clear();
        const auto& tk = matriz::ui::tema();
        auto r = getLocalBounds().toFloat();
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

        juce::Font fontKey(juce::FontOptions(10.0f, juce::Font::bold));
        juce::Font fontLabel(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold));
        juce::Font fontInst(juce::FontOptions(tk.tamanhoFonteCorpo - 1.0f, juce::Font::bold));

        float x = 6.0f;
        float cy = r.getCentreY();
        float badgeSize = 20.0f;
        float badgeCorner = 4.0f;

        juce::String textoInstrucao = isPt ? "Atalhos" : "Shortcuts";
        g.setFont(fontInst);
        g.setColour(tk.textoSecundario);
        float instW = juce::GlyphArrangement::getStringWidth(fontInst, textoInstrucao);
        g.drawText(textoInstrucao, (int)std::round(x), 0, (int)std::ceil(instW), getHeight(), juce::Justification::centredLeft, false);
        x += instW + 14.0f;

        auto drawLegendItem = [&](char keyChar, const juce::String& keyStr, juce::Colour bgKey, juce::Colour textKeyCol, const juce::String& label) {
            float startX = x;

            // Key badge
            juce::Rectangle<float> badge(x, cy - badgeSize * 0.5f, badgeSize, badgeSize);
            g.setColour(bgKey);
            g.fillRoundedRectangle(badge, badgeCorner);
            g.setColour(textKeyCol);
            g.setFont(fontKey);
            g.drawText(keyStr, badge, juce::Justification::centred, false);
            x += badgeSize + 6.0f;

            // Label
            g.setFont(fontLabel);
            g.setColour(tk.textoPrimario);
            float labelW = juce::GlyphArrangement::getStringWidth(fontLabel, label);
            g.drawText(label, (int)std::round(x), 0, (int)std::ceil(labelW), getHeight(), juce::Justification::centredLeft, false);
            x += labelW;

            boundsAtalhos_.push_back({ keyChar, (int)std::round(startX), (int)std::round(x) });
            x += 20.0f; // Gap between legend items
        };

        // 1. P - Send to Print
        drawLegendItem('P', "P", juce::Colour(0xffff6b00), juce::Colours::white, isPt ? juce::String::fromUTF8("Enviar para Impressão") : "Send to Print");

        // 2. K - Send to ZIP List
        drawLegendItem('K', "K", juce::Colour(0xff0077ff), juce::Colours::white, isPt ? juce::String::fromUTF8("Enviar para Lista ZIP") : "Send to ZIP List");

        // 3. H - HTML Publish
        drawLegendItem('H', "H", juce::Colour(0xff39ff14), juce::Colours::black, isPt ? juce::String::fromUTF8("Publicação HTML") : "HTML Publish");

        // 4. W - Apply Watermark / Aplicar Marca D'Água
        drawLegendItem('W', "W", juce::Colour(0xffffcc00), juce::Colours::black, isPt ? juce::String::fromUTF8("Aplicar Marca D'Água") : "Apply Watermark");

        // 5. E - Tag as Edited (marcação manual, ver item 6)
        drawLegendItem('E', "E", juce::Colour(0xffFFEE00), juce::Colours::black, isPt ? juce::String::fromUTF8("Marcar como Editado") : "Tag as Edited");

        float helpSize = 16.0f;
        if (helpCircle_)
            helpCircle_->setBounds((int)std::round(x), (int)std::round(cy - helpSize * 0.5f), (int)helpSize, (int)helpSize);
    }

private:
    struct ItemBound {
        char key;
        int xStart;
        int xEnd;
    };
    std::vector<ItemBound> boundsAtalhos_;
    std::unique_ptr<HelpCircle> helpCircle_;
};

} // namespace

namespace matriz::ui {

CatalogWorkspaceComponent::CatalogWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto)
{
    EventBus::obterInstancia().registrarListener(&escutaEventos_);
    mosaico_ = std::make_unique<MosaicoComponent>(projeto_);
    // Item 9: METADATA não tem Vault/pasta pra soltar arquivo arrastado —
    // clicar numa miniatura e arrastar deve criar seleção em laço, não
    // tentar um arrasto de arquivo sem destino nenhum.
    mosaico_->definirPermiteArrastarParaFora(false);
    // Contagens/filtros pedidos com o snapshot ainda em voo esperam por ele
    // (em vez de uma listarItens() própria) e são refeitos quando chega.
    mosaico_->aoMudarConteudoVisivel = [this] {
        if (!mosaico_ || mosaico_->snapshotPendente()) return;
        if (filtrosAguardandoSnapshot_) {
            filtrosAguardandoSnapshot_ = false;
            reaplicandoFiltrosAposSnapshot_ = true;
            aplicarFiltrosAdicionais();
            reaplicandoFiltrosAposSnapshot_ = false;
        }
        if (contagensAguardandoSnapshot_) {
            contagensAguardandoSnapshot_ = false;
            atualizarContagens();
        }
    };
    mosaico_->aoSelecionar = [this](const std::string& itemId) { selecionarItem(itemId); };
    mosaico_->aoAbrirPreview = [this](const std::string& itemId) { abrirWorkbench(itemId); };
    mosaico_->aoAbrirRelinkOffline = [this](const std::string& itemId) { abrirRelinkOffline(itemId); };
    mosaico_->aoPedirMenuContexto = [this](std::vector<std::string> itemIds) {
        abrirMenuContexto(std::move(itemIds));
    };
    mosaico_->aoMudarSelecao = [this] {
        if (categoriaSelecionada_ >= 0 && categoriaSelecionada_ < static_cast<int>(categorias_.size())) {
            if (categorias_[static_cast<size_t>(categoriaSelecionada_)].chave == "selected") {
                aplicarFiltrosAdicionais();
            }
        }
        // Performance (item "METADATA está ficando lenta / spinning wheel"):
        // atualizarContagens() varre TODO o catálogo (proj->listarItens())
        // e antes disso interrompe/espera (até 100ms) qualquer job anterior
        // do mesmo tipo — mas nenhuma contagem da sidebar (por tipo/ano/
        // collection) depende de QUAL item está selecionado, só do catálogo
        // em si; a única que muda com a seleção é o número no botão
        // "Selected", e esse já vem de mosaico_->itensSelecionados().size()
        // (ver aplicarContagens) — não precisa da varredura completa pra
        // isso. Rodar esse job pesado a cada clique só empilhava trabalho e
        // concorrência no pool a cada seleção, piorando conforme o acervo
        // cresce; as outras contagens continuam atualizadas normalmente via
        // aoMudar/aoAplicarEmLote (edição real) e o timer de 60s.
        for (size_t i = 0; i < categorias_.size(); ++i) {
            if (categorias_[i].chave == "selected") {
                int selCount = mosaico_ ? static_cast<int>(mosaico_->itensSelecionados().size()) : 0;
                categorias_[i].contagem = selCount;
                if (i < botoesCategorias_.size()) {
                    juce::String label = categorias_[i].rotulo;
                    if (selCount > 0) label += " (" + juce::String(selCount) + ")";
                    botoesCategorias_[i]->setButtonText(label);
                }
                break;
            }
        }
    };
    mosaico_->aoLimparMetadados = [this](const std::vector<std::string>& itemIds) {
        if (itemIds.empty()) return;
        acoes::limparMetadados(projeto_, itemIds, acoes::Ganchos{
            [this] { recarregar(); },
            {},
            {}
        });
    };
    mosaico_->aoRenomearItem = [this] {
        recarregar();
        if (aoItemAlterado) aoItemAlterado({});
    };
    mosaicoViewport_ = std::make_unique<juce::Viewport>();
    mosaicoViewport_->setViewedComponent(mosaico_.get(), false);
    addAndMakeVisible(*mosaicoViewport_);

    configurarLookAndFeel(sidebarButtonLf_);

    fichaPanel_ = std::make_unique<FichaPanelComponent>(projeto_);
    // EDIT METADATA != REMOVE FROM THIS LIST (correção "METADATA"): editar
    // um campo já não pode disparar o recarregar() pesado daqui — ele
    // refaz aplicarFiltrosAdicionais() (que reconstrói o filtro "Selected"/
    // "Hide Unselected" a partir da seleção atual) E manda o mosaico buscar
    // tudo de novo no banco, tudo em cima do clique de um X de tag. Era
    // esse cruzamento — não qualquer rotina de remoção de verdade — que
    // fazia o arquivo sumir da lista ao editar. Cada campo agora chama
    // aoAplicarSucesso (abaixo), que só atualiza O ITEM EDITADO em memória
    // e repinta; aoMudar sobra só pra refletir contagem nas abas da
    // sidebar, sem tocar no filtro nem no conjunto de itens carregados.
    // Debounce (~500 ms): cada tecla/campo editado disparava uma contagem
    // completa; uma rajada de edições agora vira uma contagem só no fim.
    fichaPanel_->aoMudar = [this] {
        const int geracao = ++geracaoContagensAgendadas_;
        juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
        juce::Timer::callAfterDelay(500, [safeThis, geracao] {
            if (safeThis != nullptr && geracao == safeThis->geracaoContagensAgendadas_)
                safeThis->atualizarContagens();
        });
    };
    fichaPanel_->aoAplicarEmLote = [this] {
        atualizarContagens();
    };
    fichaPanel_->aoAplicarSucesso = [this](const std::string& itemId) {
        if (mosaico_) mosaico_->atualizarItemEmMemoria(itemId);
        if (aoItemAlterado) aoItemAlterado(itemId);
    };
    addAndMakeVisible(*fichaPanel_);

    auto resizer = std::make_unique<FichaResizerBar>();
    resizer->aoArrastar = [this](int deltaX) {
        if (fichaColapsada_) return;
        larguraFicha_ -= deltaX;
        larguraFicha_ = juce::jlimit(kLarguraFichaMin, kLarguraFichaMax, larguraFicha_);
        resized();
    };
    fichaResizerBar_ = std::move(resizer);
    addAndMakeVisible(*fichaResizerBar_);

    btnToggleFicha_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x96\xb6"));
    btnToggleFicha_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnToggleFicha_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnToggleFicha_->setTooltip(matriz::i18n::t("catwork.toggle_ficha_tooltip"));
    btnToggleFicha_->onClick = [this] {
        fichaColapsada_ = !fichaColapsada_;
        if (fichaColapsada_) {
            btnToggleFicha_->setButtonText(matriz::i18n::t("catwork.toggle_ficha_texto"));
            btnToggleFicha_->setTooltip(matriz::i18n::t("catwork.toggle_ficha_tooltip_show"));
            if (fichaPanel_) fichaPanel_->setVisible(false);
            if (fichaResizerBar_) fichaResizerBar_->setVisible(false);
        } else {
            btnToggleFicha_->setButtonText(juce::CharPointer_UTF8("\xe2\x96\xb6"));
            btnToggleFicha_->setTooltip(matriz::i18n::t("catwork.toggle_ficha_tooltip_hide"));
            if (fichaPanel_) fichaPanel_->setVisible(true);
            if (fichaResizerBar_) fichaResizerBar_->setVisible(true);
        }
        resized();
    };
    addAndMakeVisible(*btnToggleFicha_);

    campoBusca_ = std::make_unique<juce::TextEditor>();
    campoBusca_->setTextToShowWhenEmpty(matriz::i18n::t("barra.buscar"), juce::Colours::grey);
    campoBusca_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    campoBusca_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
    campoBusca_->setColour(juce::TextEditor::outlineColourId, tema().borda);
    // A busca deixou de ser "ao vivo" (o debounce de 280ms por tecla saiu):
    // agora ela só roda quando o usuário clica na lupa à esquerda do campo
    // ou aperta Enter — ver executarBusca(). Digitar apenas mostra/esconde
    // o × de limpar.
    campoBusca_->onTextChange = [this] {
        // item 6: o × fica visível se há texto digitado OU chips ativos —
        // ele agora limpa os dois de uma vez, não só o texto em digitação.
        bool temChips = mosaico_ && !mosaico_->termosBuscaAtuais().isEmpty();
        if (btnLimparBusca_) btnLimparBusca_->setVisible(campoBusca_->getText().isNotEmpty() || temChips);
    };
    campoBusca_->onReturnKey = [this] { executarBusca(); };
    campoBusca_->setTooltip("Type a term and click the magnifier (or press Enter) to search");
    addAndMakeVisible(*campoBusca_);

    btnLupaBusca_ = std::make_unique<LupaBuscaButton>();
    btnLupaBusca_->setTooltip("Run search — clears every active filter first, so the search is always project-wide");
    btnLupaBusca_->onClick = [this] { executarBusca(); };
    addAndMakeVisible(*btnLupaBusca_);

    btnLimparBusca_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xc3\x97"));
    btnLimparBusca_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    btnLimparBusca_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnLimparBusca_->onClick = [this] {
        // item 6: limpa o texto em digitação E todos os chips de busca
        // ativos de uma vez (mantém este × como o "limpar tudo" existente).
        campoBusca_->setText("", juce::dontSendNotification);
        limparChipsDeBusca();
        btnLimparBusca_->setVisible(false);
        resized();
    };
    btnLimparBusca_->setTooltip("Clear search term and all active search chips");
    // addChildComponent, não addAndMakeVisible: o segundo força visible=true e
    // desfaz o estado inicial escondido.
    addChildComponent(*btnLimparBusca_);

    // Item 1 (lista nova de hoje): busca ativa vira chips com × logo abaixo
    // da caixa, em vez de só o × interno da caixa sumir/ficar. Ficam
    // visíveis e filtrando até o usuário fechar um chip (ou todos, via
    // HOME/limpar). Item 6: cada chip fecha individualmente, sem mexer nos
    // outros; a fileira rola na horizontal (viewportChipsBusca_) quando não
    // cabe mais na largura visível.
    chipBuscaAtiva_ = std::make_unique<ChipsBuscaAtivaComponent>();
    chipBuscaAtiva_->aoFecharChip = [this](int indice) {
        if (mosaico_) mosaico_->removerTermoBusca(indice);
        if (chipBuscaAtiva_ && mosaico_) {
            chipBuscaAtiva_->definirTermos(mosaico_->termosBuscaAtuais());
            if (viewportChipsBusca_) viewportChipsBusca_->setVisible(chipBuscaAtiva_->temTermos());
        }
        resized();
    };
    viewportChipsBusca_ = std::make_unique<juce::Viewport>();
    viewportChipsBusca_->setViewedComponent(chipBuscaAtiva_.get(), false);
    viewportChipsBusca_->setScrollBarsShown(false, true);
    addChildComponent(*viewportChipsBusca_);

    editMode_ = true;

    sliderTamanho_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    sliderTamanho_->setRange(0.0, 1.0, 0.01);
    sliderTamanho_->setValue(0.24, juce::dontSendNotification);
    if (mosaico_) mosaico_->definirTamanhoContinuo(0.24);
    sliderTamanho_->setColour(juce::Slider::trackColourId, tema().borda);
    sliderTamanho_->setColour(juce::Slider::thumbColourId, tema().acento);
    sliderTamanho_->setColour(juce::Slider::backgroundColourId, tema().painelAlt);
    sliderTamanho_->onValueChange = [this] {
        if (mosaico_) mosaico_->definirTamanhoContinuo(sliderTamanho_->getValue());
    };
    sliderTamanho_->setTooltip("Adjust thumbnail display size");
    addAndMakeVisible(*sliderTamanho_);

    lblTamanho_ = std::make_unique<juce::Label>("", "SIZE");
    lblTamanho_->setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    lblTamanho_->setColour(juce::Label::textColourId, tema().textoTerciario);
    lblTamanho_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblTamanho_);

    btnVisaoGrade_ = std::make_unique<ViewModeIconButton>(ViewModeIconButton::IconType::Grid);
    btnVisaoGrade_->setAtivo(true);
    btnVisaoGrade_->setTooltip("Grid view");
    btnVisaoGrade_->onClick = [this] {
        modoVisaoGrade_ = true;
        if (mosaico_) {
            mosaico_->definirModoVisao(MosaicoComponent::ModoVisao::Grade);
            if (sliderTamanho_) {
                mosaico_->definirTamanhoContinuo(sliderTamanho_->getValue());
            }
        }
        btnVisaoGrade_->setAtivo(true);
        btnVisaoLista_->setAtivo(false);
        if (sliderTamanho_) sliderTamanho_->setEnabled(true);
        resized();
        repaint();
    };
    addAndMakeVisible(*btnVisaoGrade_);

    btnVisaoLista_ = std::make_unique<ViewModeIconButton>(ViewModeIconButton::IconType::List);
    btnVisaoLista_->setAtivo(false);
    btnVisaoLista_->setTooltip("List view");
    btnVisaoLista_->onClick = [this] {
        modoVisaoGrade_ = false;
        if (mosaico_) mosaico_->definirModoVisao(MosaicoComponent::ModoVisao::Lista);
        btnVisaoLista_->setAtivo(true);
        btnVisaoGrade_->setAtivo(false);
        if (sliderTamanho_) sliderTamanho_->setEnabled(false);
        resized();
        repaint();
    };
    addAndMakeVisible(*btnVisaoLista_);

    btnOcultarEditados_ = std::make_unique<juce::TextButton>(matriz::i18n::t("catwork.ocultar_editados_off"));
    btnOcultarEditados_->setLookAndFeel(&sidebarButtonLf_);
    btnOcultarEditados_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnOcultarEditados_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnOcultarEditados_->setTooltip("Hide already edited assets from the grid (ON/OFF)");
    btnOcultarEditados_->onClick = [this] {
        ocultarEditados_ = !ocultarEditados_;
        btnOcultarEditados_->setButtonText(ocultarEditados_ ? matriz::i18n::t("catwork.ocultar_editados_on") : matriz::i18n::t("catwork.ocultar_editados_off"));
        btnOcultarEditados_->setColour(juce::TextButton::buttonColourId, ocultarEditados_ ? tema().acento : tema().painelAlt);
        btnOcultarEditados_->setColour(juce::TextButton::textColourOffId, ocultarEditados_ ? tema().textoSobreAcento : tema().textoSecundario);
        if (mosaico_) mosaico_->definirOcultarEditados(ocultarEditados_);
    };
    addAndMakeVisible(*btnOcultarEditados_);

    btnOcultarNaoSelecionados_ = std::make_unique<juce::TextButton>(matriz::i18n::t("catwork.ocultar_nao_selecionados_off"));
    btnOcultarNaoSelecionados_->setLookAndFeel(&sidebarButtonLf_);
    btnOcultarNaoSelecionados_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnOcultarNaoSelecionados_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnOcultarNaoSelecionados_->setTooltip("Hide unselected assets from the grid (ON/OFF)");
    btnOcultarNaoSelecionados_->onClick = [this] {
        ocultarNaoSelecionados_ = !ocultarNaoSelecionados_;
        btnOcultarNaoSelecionados_->setButtonText(ocultarNaoSelecionados_ ? matriz::i18n::t("catwork.ocultar_nao_selecionados_on") : matriz::i18n::t("catwork.ocultar_nao_selecionados_off"));
        btnOcultarNaoSelecionados_->setColour(juce::TextButton::buttonColourId, ocultarNaoSelecionados_ ? tema().acento : tema().painelAlt);
        btnOcultarNaoSelecionados_->setColour(juce::TextButton::textColourOffId, ocultarNaoSelecionados_ ? tema().textoSobreAcento : tema().textoSecundario);
        if (mosaico_) mosaico_->definirOcultarNaoSelecionados(ocultarNaoSelecionados_);
    };
    addAndMakeVisible(*btnOcultarNaoSelecionados_);

    // Item C.8: mostra só os arquivos do último lote importado pelo
    // ingest — mesmo mecanismo de filtro de anosSelecionados_/tipoMidiaSelecionado_
    // (aplicarFiltrosAdicionais), combinável com os outros filtros da coluna.
    btnMostrarRecentes_ = std::make_unique<juce::TextButton>(matriz::i18n::t("catwork.mostrar_recentes_off"));
    btnMostrarRecentes_->setLookAndFeel(&sidebarButtonLf_);
    btnMostrarRecentes_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnMostrarRecentes_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnMostrarRecentes_->setTooltip("Show only files from the most recent ingest batch (ON/OFF)");
    btnMostrarRecentes_->onClick = [this] {
        mostrarApenasRecentes_ = !mostrarApenasRecentes_;
        btnMostrarRecentes_->setButtonText(mostrarApenasRecentes_ ? matriz::i18n::t("catwork.mostrar_recentes_on") : matriz::i18n::t("catwork.mostrar_recentes_off"));
        btnMostrarRecentes_->setColour(juce::TextButton::buttonColourId, mostrarApenasRecentes_ ? tema().acento : tema().painelAlt);
        btnMostrarRecentes_->setColour(juce::TextButton::textColourOffId, mostrarApenasRecentes_ ? tema().textoSobreAcento : tema().textoSecundario);
        aplicarFiltrosAdicionais();
    };
    addAndMakeVisible(*btnMostrarRecentes_);

    btnSelecionarTodos_ = std::make_unique<juce::TextButton>(matriz::i18n::t("catwork.selecionar_todos"));
    btnSelecionarTodos_->setLookAndFeel(&sidebarButtonLf_);
    btnSelecionarTodos_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnSelecionarTodos_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnSelecionarTodos_->onClick = [this] {
        if (!mosaico_) return;
        mosaico_->selecionarTodos();
        // Item E.11: sem isto, o foco de teclado fica no botão "Select All"
        // depois do clique, e os atalhos E/P/K/H/W (que MosaicoComponent
        // trata em keyPressed) nunca chegam até a grade.
        mosaico_->grabKeyboardFocus();
    };
    btnSelecionarTodos_->setTooltip("Select all items currently showing in the grid");
    addAndMakeVisible(*btnSelecionarTodos_);

    btnLimparSelecao_ = std::make_unique<juce::TextButton>(matriz::i18n::t("catwork.desmarcar"));
    btnLimparSelecao_->setLookAndFeel(&sidebarButtonLf_);
    btnLimparSelecao_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnLimparSelecao_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnLimparSelecao_->onClick = [this] { if (mosaico_) mosaico_->limparSelecao(); };
    btnLimparSelecao_->setTooltip("Clear current grid selection");
    addAndMakeVisible(*btnLimparSelecao_);

    lblCaminhoNavegacao_ = std::make_unique<juce::Label>("", "");
    lblCaminhoNavegacao_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblCaminhoNavegacao_->setColour(juce::Label::textColourId, tema().textoSecundario);
    lblCaminhoNavegacao_->setColour(juce::Label::backgroundColourId, tema().painelAlt);
    addChildComponent(*lblCaminhoNavegacao_);

    auto leg = std::make_unique<ShortcutLegendComponent>();
    leg->aoClicarAtalho = [this](char k) {
        if (k == 'E') {
            projeto_.limparTodosMarcadosRevisado();
            if (mosaico_) mosaico_->recarregar();
            return;
        }
        ProjetoAberto::TipoMarcacao tipo;
        switch (k) {
            case 'H': tipo = ProjetoAberto::TipoMarcacao::Html;      break;
            case 'K': tipo = ProjetoAberto::TipoMarcacao::Zip;       break;
            case 'P': tipo = ProjetoAberto::TipoMarcacao::Print;     break;
            case 'W': tipo = ProjetoAberto::TipoMarcacao::Watermark; break;
            default: return;
        }
        projeto_.limparMarcacoes(tipo);
        if (mosaico_) mosaico_->recarregar();
    };
    legendaAtalhos_ = std::move(leg);
    addAndMakeVisible(*legendaAtalhos_);

    construirSidebar();
    mosaico_->recarregar();
    startTimer(60000);

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    ProjetoAberto* proj = &projeto_;
    poolMiniaturas_.addJob([safeThis, proj]() {
        juce::Thread::sleep(500);
        if (!safeThis) return;
        DBG("MatrizMiniGen: iniciando geração de miniaturas faltantes");
        proj->gerarMiniaturasFaltantes();
        DBG("MatrizMiniGen: geração concluída, recarregando grade");
        juce::MessageManager::callAsync([safeThis]() {
            if (!safeThis) return;
            safeThis->recarregar();
        });
    });
}

CatalogWorkspaceComponent::HomeButtonLookAndFeel::HomeButtonLookAndFeel() {
    icone = juce::ImageFileFormat::loadFrom(AssetsBinaryData::home_png, AssetsBinaryData::home_pngSize);
    if (!icone.isValid()) return;

    // O PNG vem com fundo branco chapado; aqui ele vira transparente para o
    // ícone assentar sobre o fundo da coluna esquerda.
    icone = icone.convertedToFormat(juce::Image::ARGB);
    juce::Image::BitmapData bmp(icone, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < bmp.height; ++y) {
        for (int x = 0; x < bmp.width; ++x) {
            auto cor = bmp.getPixelColour(x, y);
            if (cor.getRed() >= 240 && cor.getGreen() >= 240 && cor.getBlue() >= 240)
                bmp.setPixelColour(x, y, juce::Colours::transparentBlack);
        }
    }
}

void CatalogWorkspaceComponent::HomeButtonLookAndFeel::drawButtonText(
        juce::Graphics& g, juce::TextButton& botao, bool, bool) {
    auto area = botao.getLocalBounds().reduced(4, 3);
    auto areaTexto = area.removeFromBottom(14);

    auto cor = botao.findColour(botao.getToggleState() ? juce::TextButton::textColourOnId
                                                       : juce::TextButton::textColourOffId);
    if (icone.isValid()) {
        g.setOpacity(1.0f);
        g.drawImageWithin(icone, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                          juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, false);
    } else {
        g.setColour(cor);
        g.setFont(juce::Font(juce::FontOptions(16.0f)));
        g.drawText(juce::String::fromUTF8("\xf0\x9f\x8f\xa0"), area, juce::Justification::centred, false);
    }

    g.setColour(cor);
    g.setFont(getTextButtonFont(botao, botao.getHeight()));
    g.drawText(botao.getButtonText(), areaTexto, juce::Justification::centred, true);
}

CatalogWorkspaceComponent::~CatalogWorkspaceComponent() {
    EventBus::obterInstancia().removerListener(&escutaEventos_);
    stopTimer();
    poolContagens_.removeAllJobs(true, 1000);
    poolMiniaturas_.removeAllJobs(true, 30000);

    if (btnSelecionarTodos_) btnSelecionarTodos_->setLookAndFeel(nullptr);
    if (btnLimparSelecao_) btnLimparSelecao_->setLookAndFeel(nullptr);
    if (btnOcultarEditados_) btnOcultarEditados_->setLookAndFeel(nullptr);
    if (btnOcultarNaoSelecionados_) btnOcultarNaoSelecionados_->setLookAndFeel(nullptr);
    if (btnMostrarRecentes_) btnMostrarRecentes_->setLookAndFeel(nullptr);
    for (auto& b : botoesCategorias_) if (b) b->setLookAndFeel(nullptr);
    for (auto& b : botoesAnos_) if (b) b->setLookAndFeel(nullptr);
}

void CatalogWorkspaceComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    if (campoBusca_) {
        campoBusca_->setTextToShowWhenEmpty(matriz::i18n::t("barra.buscar"), juce::Colours::grey);
        campoBusca_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        campoBusca_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        campoBusca_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    }
    if (comboContentType_) {
        comboContentType_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboContentType_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboContentType_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboContentType_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (toggleMultiplosAnos_) {
        toggleMultiplosAnos_->setButtonText(matriz::i18n::t("catwork.multiplos_anos"));
        toggleMultiplosAnos_->setColour(juce::ToggleButton::textColourId, tk.textoSecundario);
        toggleMultiplosAnos_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (btnLimparBusca_) {
        btnLimparBusca_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
    }
    if (sliderTamanho_) {
        sliderTamanho_->setColour(juce::Slider::trackColourId, tk.borda);
        sliderTamanho_->setColour(juce::Slider::thumbColourId, tk.acento);
        sliderTamanho_->setColour(juce::Slider::backgroundColourId, tk.painelAlt);
    }
    if (lblTamanho_) {
        lblTamanho_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        lblTamanho_->setColour(juce::Label::textColourId, tk.textoTerciario);
    }
    if (btnVisaoGrade_) {
        btnVisaoGrade_->setAtivo(modoVisaoGrade_);
    }
    if (btnVisaoLista_) {
        btnVisaoLista_->setAtivo(!modoVisaoGrade_);
    }
    if (btnOcultarEditados_) {
        btnOcultarEditados_->setColour(juce::TextButton::buttonColourId, ocultarEditados_ ? tk.acento : tk.painelAlt);
        btnOcultarEditados_->setColour(juce::TextButton::textColourOffId, ocultarEditados_ ? tk.textoSobreAcento : tk.textoSecundario);
        btnOcultarEditados_->setButtonText(ocultarEditados_ ? matriz::i18n::t("catwork.ocultar_editados_on") : matriz::i18n::t("catwork.ocultar_editados_off"));
    }
    if (btnOcultarNaoSelecionados_) {
        btnOcultarNaoSelecionados_->setColour(juce::TextButton::buttonColourId, ocultarNaoSelecionados_ ? tk.acento : tk.painelAlt);
        btnOcultarNaoSelecionados_->setColour(juce::TextButton::textColourOffId, ocultarNaoSelecionados_ ? tk.textoSobreAcento : tk.textoSecundario);
        btnOcultarNaoSelecionados_->setButtonText(ocultarNaoSelecionados_ ? matriz::i18n::t("catwork.ocultar_nao_selecionados_on") : matriz::i18n::t("catwork.ocultar_nao_selecionados_off"));
    }
    if (btnMostrarRecentes_) {
        btnMostrarRecentes_->setColour(juce::TextButton::buttonColourId, mostrarApenasRecentes_ ? tk.acento : tk.painelAlt);
        btnMostrarRecentes_->setColour(juce::TextButton::textColourOffId, mostrarApenasRecentes_ ? tk.textoSobreAcento : tk.textoSecundario);
        btnMostrarRecentes_->setButtonText(mostrarApenasRecentes_ ? matriz::i18n::t("catwork.mostrar_recentes_on") : matriz::i18n::t("catwork.mostrar_recentes_off"));
    }
    if (btnSelecionarTodos_) {
        btnSelecionarTodos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnSelecionarTodos_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnSelecionarTodos_->setButtonText(matriz::i18n::t("catwork.selecionar_todos"));
    }
    if (btnLimparSelecao_) {
        btnLimparSelecao_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnLimparSelecao_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnLimparSelecao_->setButtonText(matriz::i18n::t("catwork.desmarcar"));
    }
    if (lblCaminhoNavegacao_) {
        lblCaminhoNavegacao_->setColour(juce::Label::textColourId, tk.textoSecundario);
        lblCaminhoNavegacao_->setColour(juce::Label::backgroundColourId, tk.painelAlt);
    }
    if (legendaAtalhos_) {
        legendaAtalhos_->repaint();
    }
    if (fichaPanel_) {
        fichaPanel_->sendLookAndFeelChange();
        fichaPanel_->repaint();
    }
    if (btnToggleFicha_) {
        btnToggleFicha_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnToggleFicha_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        if (fichaColapsada_) {
            btnToggleFicha_->setButtonText(matriz::i18n::t("catwork.toggle_ficha_texto"));
            btnToggleFicha_->setTooltip(matriz::i18n::t("catwork.toggle_ficha_tooltip_show"));
        } else {
            btnToggleFicha_->setButtonText(juce::CharPointer_UTF8("\xe2\x96\xb6"));
            btnToggleFicha_->setTooltip(matriz::i18n::t("catwork.toggle_ficha_tooltip_hide"));
        }
    }

    configurarLookAndFeel(sidebarButtonLf_);

    for (auto& cat : categorias_) {
        // "all" é o botão HOME: ícone home.png com o rótulo "All Assets"
        // logo abaixo (ver HomeButtonLookAndFeel e construirSidebar).
        if      (cat.chave == "all")         cat.rotulo = matriz::i18n::t("catwork.all");
        else if (cat.chave == "audio")       cat.rotulo = matriz::i18n::t("catwork.audio");
        else if (cat.chave == "video")       cat.rotulo = matriz::i18n::t("catwork.video");
        else if (cat.chave == "images")      cat.rotulo = matriz::i18n::t("catwork.images");
        else if (cat.chave == "documents")   cat.rotulo = matriz::i18n::t("catwork.documents");
        else if (cat.chave == "sessions")    cat.rotulo = matriz::i18n::t("catwork.sessions");
    }
    for (size_t i = 0; i < categorias_.size() && i < botoesCategorias_.size(); ++i) {
        juce::String texto = categorias_[i].rotulo;
        if (categorias_[i].contagem > 0 || categorias_[i].chave == "all") {
            texto += " (" + juce::String(categorias_[i].contagem) + ")";
        }
        botoesCategorias_[i]->setButtonText(texto);
        if (categorias_[i].chave == "all") botoesCategorias_[i]->setTooltip(matriz::i18n::t("catwork.all"));
    }

    atualizarBotoesSidebar();
    if (mosaico_) {
        mosaico_->sendLookAndFeelChange();
        mosaico_->repaint();
    }
    if (fichaPanel_) {
        fichaPanel_->sendLookAndFeelChange();
        fichaPanel_->repaint();
    }
    repaint();
}

void CatalogWorkspaceComponent::timerCallback() {
    atualizarContagens();
}

void CatalogWorkspaceComponent::construirSidebar() {
    for (auto& b : botoesCategorias_) if (b) b->setLookAndFeel(nullptr);
    categorias_.clear();
    botoesCategorias_.clear();
    secoesSidebar_.clear();
    anosDisponiveis_.clear();
    botoesAnos_.clear();
    collectionDisponiveis_.clear();

    // Item "Coluna esquerda da aba METADATA": o card LIBRARY foi eliminado
    // por completo, junto dos botões Selected files/Folders/No backup
    // (removidos, não realocados). "All Assets" sobrevive sozinho, virando
    // um botão HOME (ícone home.png com o rótulo "All Assets" abaixo)
    // posicionado acima de "Hide Edited: OFF" em vez de dentro de um card —
    // ver resized() e HomeButtonLookAndFeel.
    categorias_.push_back({matriz::i18n::t("catwork.all"), "all", 0});

    indiceInicioMediaType_ = static_cast<int>(categorias_.size());
    secoesSidebar_.push_back({indiceInicioMediaType_, matriz::i18n::t("catwork.secao_media_type")});
    categorias_.push_back({matriz::i18n::t("catwork.audio"), "audio", 0});
    categorias_.push_back({matriz::i18n::t("catwork.video"), "video", 0});
    categorias_.push_back({matriz::i18n::t("catwork.images"), "images", 0});
    categorias_.push_back({matriz::i18n::t("catwork.documents"), "documents", 0});
    categorias_.push_back({matriz::i18n::t("catwork.sessions"), "sessions", 0});

    for (size_t i = 0; i < categorias_.size(); ++i) {
        auto btn = std::make_unique<juce::TextButton>(categorias_[i].rotulo);
        btn->setLookAndFeel(i == 0 ? static_cast<juce::LookAndFeel*>(&homeButtonLf_)
                                   : static_cast<juce::LookAndFeel*>(&sidebarButtonLf_));
        btn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btn->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
        btn->onClick = [this, i] { selecionarCategoria(static_cast<int>(i)); };
        if (i == 0) btn->setTooltip(matriz::i18n::t("catwork.all"));
        addAndMakeVisible(*btn);
        botoesCategorias_.push_back(std::move(btn));
    }

    construirFiltroAnos();
    construirFiltroCollection();
    atualizarContagens();
    selecionarCategoria(0);
}

void CatalogWorkspaceComponent::construirFiltroAnos() {
    for (auto& b : botoesAnos_) {
        if (b) b->setLookAndFeel(nullptr);
        anosContainer_.removeChildComponent(b.get());
    }
    botoesAnos_.clear();

    if (!anosViewport_) {
        anosViewport_ = std::make_unique<juce::Viewport>();
        anosViewport_->setViewedComponent(&anosContainer_, false);
        addAndMakeVisible(*anosViewport_);
    }

    const auto& tk = tema();

    // Item 6 (nova lista): toggle "permitir múltiplos anos", default OFF —
    // substitui os campos FROM/TO/CLEAR removidos (item 5). Marcar vários
    // anos individualmente cobre o mesmo caso de uso de um range.
    if (!toggleMultiplosAnos_) {
        toggleMultiplosAnos_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("catwork.multiplos_anos"));
        toggleMultiplosAnos_->setColour(juce::ToggleButton::textColourId, tk.textoSecundario);
        toggleMultiplosAnos_->setColour(juce::ToggleButton::tickColourId, tk.acento);
        toggleMultiplosAnos_->onClick = [this] {
            permitirMultiplosAnos_ = toggleMultiplosAnos_->getToggleState();
            // Desligar com mais de um ano marcado deixaria ambíguo qual
            // sobrevive — mais previsível zerar e o operador escolher de novo.
            if (!permitirMultiplosAnos_ && anosSelecionados_.size() > 1) {
                anosSelecionados_.clear();
                aplicarFiltroAno();
            }
        };
        addAndMakeVisible(*toggleMultiplosAnos_);
    }

    for (size_t i = 0; i < anosDisponiveis_.size(); ++i) {
        auto& [ano, contagem] = anosDisponiveis_[i];
        juce::String label = (ano == -1) ? matriz::i18n::t("comum.desconhecido") : juce::String(ano);
        label += " (" + juce::String(contagem) + ")";
        auto btn = std::make_unique<juce::TextButton>(label);
        btn->setLookAndFeel(&sidebarButtonLf_);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btn->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
        btn->onClick = [this, i] {
            if (i < anosDisponiveis_.size()) {
                int anoSel = anosDisponiveis_[i].first;
                if (permitirMultiplosAnos_) {
                    if (anosSelecionados_.count(anoSel)) anosSelecionados_.erase(anoSel);
                    else anosSelecionados_.insert(anoSel);
                } else {
                    bool jaEraOUnico = anosSelecionados_.size() == 1 && *anosSelecionados_.begin() == anoSel;
                    anosSelecionados_.clear();
                    if (!jaEraOUnico) anosSelecionados_.insert(anoSel);
                }
                aplicarFiltroAno();
            }
        };
        anosContainer_.addAndMakeVisible(*btn);
        botoesAnos_.push_back(std::move(btn));
    }
}

void CatalogWorkspaceComponent::EscutaEventos::aoItemAlterado(const EventoItemAlterado& e) {
    if (e.tipoAlteracao != "metadado_data") return;
    // Uma aplicação em lote grava um EVENT DATE por item; sem este guard,
    // cada item da seleção dispararia uma refiltragem completa.
    if (dono.refreshDataPendente_) return;
    dono.refreshDataPendente_ = true;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CatalogWorkspaceComponent>(&dono)] {
        if (safe == nullptr) return;
        safe->refreshDataPendente_ = false;
        safe->atualizarFiltrosDeData();
    });
}

void CatalogWorkspaceComponent::atualizarFiltrosDeData() {
    // O item editado já foi atualizado em memória por aoAplicarSucesso
    // (mosaico_->atualizarItemEmMemoria), então basta reavaliar os filtros
    // que dependem do ano: os botões de ano da coluna esquerda. A lista de
    // anos disponíveis em si é reconstruída pelo job de contagens que
    // aoMudar já dispara. FROM/TO/CLEAR saíram (item 5, nova lista).
    aplicarFiltrosAdicionais();
}

void CatalogWorkspaceComponent::aplicarFiltroAno() {
    aplicarFiltrosAdicionais();
}

void CatalogWorkspaceComponent::atualizarDestaqueBotoesAnoCollection() {
    for (size_t i = 0; i < botoesAnos_.size() && i < anosDisponiveis_.size(); ++i) {
        bool ativo = anosSelecionados_.count(anosDisponiveis_[i].first) > 0;
        botoesAnos_[i]->setColour(juce::TextButton::buttonColourId,
            ativo ? tema().acento.withAlpha(0.2f) : juce::Colours::transparentBlack);
        botoesAnos_[i]->setColour(juce::TextButton::textColourOffId,
            ativo ? tema().textoPrimario : tema().textoSecundario);
    }

    // CONTENT TYPE (item 4, nova lista) é um dropdown, não botões — só
    // precisa refletir collectionSelecionado_ no item selecionado do combo,
    // sem tocar na grade (mesma regra dos botões de ano/tipo acima).
    if (comboContentType_) {
        int idTodos = static_cast<int>(collectionDisponiveis_.size()) + 1;
        int idSel = idTodos;
        if (collectionSelecionado_.has_value()) {
            for (size_t i = 0; i < collectionDisponiveis_.size(); ++i) {
                if (collectionDisponiveis_[i].first == *collectionSelecionado_) {
                    idSel = static_cast<int>(i + 1);
                    break;
                }
            }
        }
        comboContentType_->setSelectedId(idSel, juce::dontSendNotification);
    }
}

void CatalogWorkspaceComponent::aplicarFiltrosAdicionais() {
    atualizarDestaqueBotoesAnoCollection();

    if (!mosaico_) return;

    const auto& libChave = (categoriaSelecionada_ >= 0 && categoriaSelecionada_ < static_cast<int>(categorias_.size()))
        ? categorias_[static_cast<size_t>(categoriaSelecionada_)].chave : std::string();
    if (libChave == "folders") return;

    // Mosaico vazio (abertura, snapshot não chegou): não lista na message
    // thread — carrega e reaplica os filtros quando o snapshot chegar.
    if (mosaico_->todosItensEmMemoria().empty() && !reaplicandoFiltrosAposSnapshot_) {
        filtrosAguardandoSnapshot_ = true;
        mosaico_->recarregar();
        return;
    }
    const auto& itens = mosaico_->todosItensEmMemoria();
    std::set<std::string> filteredIds;
    bool anyFilter = false;

    std::set<std::string> selectedIds;
    if (libChave == "selected") {
        anyFilter = true;
        selectedIds = mosaico_ ? mosaico_->itensSelecionados() : std::set<std::string>{};
    }

    std::set<std::string> vulneraveisIds;
    if (libChave == "vulneraveis") {
        anyFilter = true;
        auto ids = projeto_.itensDaColecaoEmbutida("vulneraveis");
        vulneraveisIds.insert(ids.begin(), ids.end());
    }

    std::set<std::string> recentesIds;
    if (mostrarApenasRecentes_) {
        anyFilter = true;
        const auto& ids = projeto_.ultimosItensIngeridos();
        recentesIds.insert(ids.begin(), ids.end());
    }

    for (const auto& item : itens) {
        if (libChave == "selected") {
            if (selectedIds.find(item.id) == selectedIds.end()) continue;
        }

        if (libChave == "vulneraveis") {
            if (vulneraveisIds.find(item.id) == vulneraveisIds.end()) continue;
        }

        if (mostrarApenasRecentes_) {
            if (recentesIds.find(item.id) == recentesIds.end()) continue;
        }

        if (tipoMidiaSelecionado_.has_value()) {
            anyFilter = true;
            auto ext = juce::String(item.extensaoArquivo).toLowerCase();
            auto cat = matriz::ingest::categoriaPorExtensao(ext);
            bool match = false;
            if (*tipoMidiaSelecionado_ == "audio") match = (cat == matriz::ingest::CategoriaMidia::Audio);
            else if (*tipoMidiaSelecionado_ == "video") match = (cat == matriz::ingest::CategoriaMidia::Video);
            else if (*tipoMidiaSelecionado_ == "images") match = (cat == matriz::ingest::CategoriaMidia::Imagem);
            else if (*tipoMidiaSelecionado_ == "documents") match = (cat == matriz::ingest::CategoriaMidia::Documento || cat == matriz::ingest::CategoriaMidia::Texto);
            else if (*tipoMidiaSelecionado_ == "sessions") match = (cat == matriz::ingest::CategoriaMidia::Sessao);
            if (!match) continue;
        }

        if (!anosSelecionados_.empty()) {
            anyFilter = true;
            // Item 6 (nova lista): com múltiplos anos marcados, o item passa
            // se bater com QUALQUER um deles (união, não interseção) — é o
            // que torna "marcar vários anos" equivalente a um range.
            bool anoBate = false;
            if (anosSelecionados_.count(-1) && !item.ano.has_value()) anoBate = true;
            if (!anoBate && item.ano.has_value() && anosSelecionados_.count(*item.ano)) anoBate = true;
            if (!anoBate) continue;
        }

        if (collectionSelecionado_.has_value()) {
            anyFilter = true;
            if (*collectionSelecionado_ == "Unknown") {
                if (item.collectionType.has_value() && !item.collectionType->empty()) continue;
            } else {
                if (!item.collectionType.has_value() || *item.collectionType != *collectionSelecionado_) continue;
            }
        }

        filteredIds.insert(item.id);
    }

    if (anyFilter) {
        mosaico_->definirFiltroItens(std::move(filteredIds));
    } else {
        mosaico_->definirFiltroItens(std::nullopt);
    }

    mosaico_->recarregar();
}

void CatalogWorkspaceComponent::construirFiltroCollection() {
    // Item 4 (nova lista): o filtro CONTENT TYPE volta como card próprio
    // abaixo de MEDIA TYPE, desta vez como dropdown em vez da lista de
    // botões de antes — mesmo collectionSelecionado_/collectionDisponiveis_
    // que já alimentavam aplicarFiltrosAdicionais() e atualizarContagens(),
    // só a apresentação mudou. O combo em si nasce uma vez só; aqui só
    // repõe os itens quando collectionDisponiveis_ muda.
    if (!comboContentType_) {
        comboContentType_ = std::make_unique<juce::ComboBox>();
        addAndMakeVisible(*comboContentType_);
        comboContentType_->onChange = [this] {
            int sel = comboContentType_->getSelectedId();
            int idTodos = static_cast<int>(collectionDisponiveis_.size()) + 1;
            if (sel <= 0 || sel >= idTodos) {
                collectionSelecionado_ = std::nullopt;
            } else {
                collectionSelecionado_ = collectionDisponiveis_[static_cast<size_t>(sel - 1)].first;
            }
            aplicarFiltrosAdicionais();
        };
    }

    comboContentType_->clear(juce::dontSendNotification);
    int idTodos = static_cast<int>(collectionDisponiveis_.size()) + 1;
    comboContentType_->addItem(matriz::i18n::t("catwork.content_type_todos"), idTodos);
    for (size_t i = 0; i < collectionDisponiveis_.size(); ++i) {
        const auto& [valor, contagem] = collectionDisponiveis_[i];
        juce::String rotulo = (valor == "Unknown")
            ? matriz::i18n::t("catwork.content_type_desconhecido")
            : juce::String(valor);
        rotulo += " (" + juce::String(contagem) + ")";
        comboContentType_->addItem(rotulo, static_cast<int>(i + 1));
    }

    int idSel = idTodos;
    if (collectionSelecionado_.has_value()) {
        for (size_t i = 0; i < collectionDisponiveis_.size(); ++i) {
            if (collectionDisponiveis_[i].first == *collectionSelecionado_) {
                idSel = static_cast<int>(i + 1);
                break;
            }
        }
    }
    comboContentType_->setSelectedId(idSel, juce::dontSendNotification);
}

void CatalogWorkspaceComponent::atualizarContagens() {
    poolContagens_.removeAllJobs(true, 100);

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    ProjetoAberto* proj = &projeto_;
    std::optional<std::string> filtroTipoMidiaAnos = tipoMidiaSelecionado_;
    std::set<int> anosParaTipo = anosSelecionados_;

    // Fase 3c: reutilizar itens já em memória no Mosaico — evita segunda query
    // completa para o mesmo evento. Cópia feita aqui na message thread; se o
    // Mosaico ainda estiver vazio (snapshot não chegou) cai no listarItens().
    // Snapshot em voo: conta com o que há (se houver) e reconta quando
    // chegar; vazio + em voo não conta nada agora (evita 2ª listarItens()).
    std::vector<ItemResumo> itensCopia;
    if (mosaico_ && !mosaico_->modoQuarentenaAtual()) {
        if (mosaico_->snapshotPendente()) {
            contagensAguardandoSnapshot_ = true;
            if (mosaico_->totalItensCarregados() == 0) return;
        }
        if (mosaico_->totalItensCarregados() > 0)
            itensCopia = mosaico_->todosItensEmMemoria();
    }

    poolContagens_.addJob([safeThis, proj, filtroTipoMidiaAnos, anosParaTipo,
                           itensCopia = std::move(itensCopia)]() mutable {
        ContagensResultado res;
        try {
            auto itens = itensCopia.empty() ? proj->listarItens() : std::move(itensCopia);
            res.total = static_cast<int>(itens.size());

            std::map<int, int> contagemPorAno;
            int semAno = 0;
            std::map<std::string, int> contagemPorCollection;
            int semCollection = 0;

            for (const auto& item : itens) {
                auto ext = juce::String(item.extensaoArquivo).toLowerCase();
                auto cat = matriz::ingest::categoriaPorExtensao(ext);

                // item: quando há filtro de ano ativo, os contadores de MEDIA
                // TYPE passam a contar só os arquivos daquele(s) ano(s) — sem
                // filtro de ano nenhum, continuam mostrando o total global
                // (mesmo comportamento de sempre).
                bool passaFiltroAnoParaTipo = anosParaTipo.empty() ||
                    anosParaTipo.count(item.ano.value_or(-1)) > 0;

                if (passaFiltroAnoParaTipo) {
                    switch (cat) {
                        case matriz::ingest::CategoriaMidia::Audio: ++res.audio; break;
                        case matriz::ingest::CategoriaMidia::Video: ++res.video; break;
                        case matriz::ingest::CategoriaMidia::Imagem: ++res.img; break;
                        case matriz::ingest::CategoriaMidia::Sessao: ++res.sessao; break;
                        case matriz::ingest::CategoriaMidia::Documento: ++res.doc; break;
                        case matriz::ingest::CategoriaMidia::Texto: ++res.doc; break;
                        default: break;
                    }
                }

                // Item C.6: quando um tipo em MEDIA TYPE está selecionado, os
                // botões de ano só contam os arquivos daquele tipo — os
                // contadores de MEDIA TYPE acima (res.audio/video/...) continuam
                // sempre totais, não filtrados por si mesmos.
                bool passaFiltroTipoParaAno = true;
                if (filtroTipoMidiaAnos.has_value()) {
                    if (*filtroTipoMidiaAnos == "audio") passaFiltroTipoParaAno = (cat == matriz::ingest::CategoriaMidia::Audio);
                    else if (*filtroTipoMidiaAnos == "video") passaFiltroTipoParaAno = (cat == matriz::ingest::CategoriaMidia::Video);
                    else if (*filtroTipoMidiaAnos == "images") passaFiltroTipoParaAno = (cat == matriz::ingest::CategoriaMidia::Imagem);
                    else if (*filtroTipoMidiaAnos == "documents") passaFiltroTipoParaAno = (cat == matriz::ingest::CategoriaMidia::Documento || cat == matriz::ingest::CategoriaMidia::Texto);
                    else if (*filtroTipoMidiaAnos == "sessions") passaFiltroTipoParaAno = (cat == matriz::ingest::CategoriaMidia::Sessao);
                }

                if (passaFiltroTipoParaAno) {
                    if (item.ano.has_value()) contagemPorAno[*item.ano]++;
                    else semAno++;
                }

                if (item.collectionType.has_value() && !item.collectionType->empty())
                    contagemPorCollection[*item.collectionType]++;
                else
                    semCollection++;
            }

            for (auto it = contagemPorAno.rbegin(); it != contagemPorAno.rend(); ++it)
                res.anos.push_back({it->first, it->second});
            if (semAno > 0) res.anos.push_back({-1, semAno});

            for (const auto& pair : contagemPorCollection)
                res.collections.push_back(pair);
            if (semCollection > 0)
                res.collections.push_back({"Unknown", semCollection});

            auto colecoes = proj->listarColecoesEmbutidas();
            for (const auto& c : colecoes) {
                if (c.chave == "revisao") res.revisao = c.contagem;
                else if (c.chave == "vulneraveis") res.vulneraveis = c.contagem;
                else if (c.chave == "single_copy") res.single_copy = c.contagem;
                else if (c.chave == "ausentes") res.ausentes = c.contagem;
            }

        } catch (...) {
            return;
        }

        juce::MessageManager::callAsync([safeThis, res]() {
            if (safeThis) {
                safeThis->aplicarContagens(res);
            }
        });
    });
}

void CatalogWorkspaceComponent::aplicarContagens(const ContagensResultado& res) {
    auto definirContagem = [&](const std::string& chave, int count) {
        for (size_t i = 0; i < categorias_.size(); ++i) {
            if (categorias_[i].chave == chave) {
                categorias_[i].contagem = count;
                if (i < botoesCategorias_.size()) {
                    juce::String label = categorias_[i].rotulo;
                    if (count > 0) label += " (" + juce::String(count) + ")";
                    botoesCategorias_[i]->setButtonText(label);
                }
                break;
            }
        }
    };

    int selCount = mosaico_ ? static_cast<int>(mosaico_->itensSelecionados().size()) : 0;

    definirContagem("all", res.total);
    definirContagem("selected", selCount);
    definirContagem("folders", res.total);
    definirContagem("vulneraveis", res.vulneraveis);
    definirContagem("audio", res.audio);
    definirContagem("video", res.video);
    definirContagem("images", res.img);
    definirContagem("documents", res.doc);
    definirContagem("sessions", res.sessao);

    bool reconstruiuAnoOuCollection = false;

    if (anosDisponiveis_ != res.anos) {
        anosDisponiveis_ = res.anos;
        construirFiltroAnos();
        reconstruiuAnoOuCollection = true;
    }

    if (collectionDisponiveis_ != res.collections) {
        collectionDisponiveis_ = res.collections;
        construirFiltroCollection();
        reconstruiuAnoOuCollection = true;
    }

    if (reconstruiuAnoOuCollection) {
        // Item C.7: construirFiltroAnos()/construirFiltroCollection() recriam
        // os botões do zero (sem destaque); sem isto, o filtro de ano/collection
        // continua ativo mas o botão parece "desmarcado" na primeira contagem
        // recalculada após uma edição — daí reaplicar só o destaque aqui, sem
        // chamar aplicarFiltrosAdicionais() inteiro (isso tocaria a grade e
        // reintroduziria o problema de performance que aoMudar evita).
        atualizarDestaqueBotoesAnoCollection();
        resized();
    }
}

// Reset completo dos filtros (botão HOME / "All Assets"). Não toca na
// seleção de itens do usuário nem no modo de visualização — só nos
// filtros que escondem arquivos da grade.
// Busca disparada pela lupa (ou por Enter no campo). Toda busca começa do
// zero: media type, DATE, collection, Hide Edited, Hide Unselected e a
// seleção herdada da árvore são desligados ANTES de buscar, para o
// resultado ser sempre do projeto inteiro. Filtrar por cima vem depois, por
// escolha do usuário.
void CatalogWorkspaceComponent::executarBusca() {
    if (!campoBusca_) return;
    juce::String termo = campoBusca_->getText().trim();

    limparTodosOsFiltros(/*incluirBusca*/ false);
    atualizarBotoesSidebar();
    aplicarFiltrosAdicionais();

    // Item 6: acrescenta um chip novo em vez de substituir o(s) já
    // ativo(s) — vários termos combinados com E (ver
    // MosaicoComponent::adicionarTermoBusca). Repetir o processo (digitar +
    // lupa/Enter) permite empilhar quantos chips o usuário quiser.
    if (mosaico_ && termo.isNotEmpty()) mosaico_->adicionarTermoBusca(termo);

    // Item 1 (nova lista de hoje): a caixa some de vista assim que a busca
    // roda — o filtro em si mora em mosaico_ (não no texto do campo), então
    // limpar aqui não desfaz nada, e libera a caixa pra digitar o próximo
    // termo na hora. Quem passa a ser o indicador/ação de "busca ativa" são
    // os chips abaixo da caixa: ficam visíveis enquanto continuarem
    // filtrando, e somem só quando o × de cada um for clicado ou até HOME.
    if (termo.isNotEmpty()) campoBusca_->setText("", juce::dontSendNotification);
    bool aindaTemChips = false;
    if (chipBuscaAtiva_ && mosaico_) {
        chipBuscaAtiva_->definirTermos(mosaico_->termosBuscaAtuais());
        aindaTemChips = chipBuscaAtiva_->temTermos();
        if (viewportChipsBusca_) viewportChipsBusca_->setVisible(aindaTemChips);
    }
    // × continua visível se sobrou algum chip pra limpar, mesmo com a
    // caixa de texto vazia de novo.
    if (btnLimparBusca_) btnLimparBusca_->setVisible(aindaTemChips);
    resized();
}

void CatalogWorkspaceComponent::limparTodosOsFiltros(bool incluirBusca) {
    tipoMidiaSelecionado_ = std::nullopt;
    anosSelecionados_.clear();
    collectionSelecionado_ = std::nullopt;

    if (ocultarEditados_) {
        ocultarEditados_ = false;
        if (mosaico_) mosaico_->definirOcultarEditados(false);
    }
    if (btnOcultarEditados_) {
        btnOcultarEditados_->setButtonText(matriz::i18n::t("catwork.ocultar_editados_off"));
        btnOcultarEditados_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
        btnOcultarEditados_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    }

    if (ocultarNaoSelecionados_) {
        ocultarNaoSelecionados_ = false;
        if (mosaico_) mosaico_->definirOcultarNaoSelecionados(false);
    }
    if (btnOcultarNaoSelecionados_) {
        btnOcultarNaoSelecionados_->setButtonText(matriz::i18n::t("catwork.ocultar_nao_selecionados_off"));
        btnOcultarNaoSelecionados_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
        btnOcultarNaoSelecionados_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    }

    mostrarApenasRecentes_ = false;
    if (btnMostrarRecentes_) {
        btnMostrarRecentes_->setButtonText(matriz::i18n::t("catwork.mostrar_recentes_off"));
        btnMostrarRecentes_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
        btnMostrarRecentes_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    }

    // Busca: o HOME também zera o termo. Já o caminho contrário (clicar na
    // lupa) chama esta função com incluirBusca=false, porque ali os filtros
    // caem justamente para dar lugar à busca que está sendo disparada.
    if (incluirBusca) {
        // Os chips (não o texto da caixa, que já está vazia depois de uma
        // busca rodada) são quem sabe de verdade se há uma busca ativa.
        bool tinhaBuscaAtiva = (chipBuscaAtiva_ && chipBuscaAtiva_->temTermos()) ||
                               (campoBusca_ && campoBusca_->getText().isNotEmpty());
        if (campoBusca_ && campoBusca_->getText().isNotEmpty()) {
            campoBusca_->setText("", juce::dontSendNotification);
        }
        if (btnLimparBusca_) btnLimparBusca_->setVisible(false);
        // HOME/limpar tudo: TODOS os chips caem de uma vez.
        if (tinhaBuscaAtiva) limparChipsDeBusca();
    }

    // Seleção herdada da árvore / SOURCE (definirFiltroItens) também cai.
    if (mosaico_) mosaico_->definirFiltroItens(std::nullopt);
}

void CatalogWorkspaceComponent::limparChipsDeBusca() {
    if (chipBuscaAtiva_) chipBuscaAtiva_->definirTermos({});
    if (viewportChipsBusca_) viewportChipsBusca_->setVisible(false);
    if (mosaico_) mosaico_->definirBusca("");
}

void CatalogWorkspaceComponent::selecionarCategoria(int indice) {
    if (indice < 0 || indice >= static_cast<int>(categorias_.size())) return;
    const auto& chave = categorias_[static_cast<size_t>(indice)].chave;

    // HOME ("All Assets") é o botão de reset: volta a mostrar o projeto
    // inteiro, desligando TODOS os filtros ativos da coluna esquerda e da
    // barra (media type, ano/DATE, collection, Hide Edited, Hide Unselected,
    // busca e a seleção herdada da árvore). O aplicarFiltrosAdicionais()
    // no fim desta função recalcula a grade já sem nada marcado.
    if (chave == "all") limparTodosOsFiltros();

    if (indice < indiceInicioMediaType_) {
        categoriaSelecionada_ = indice;
    } else {
        if (tipoMidiaSelecionado_.has_value() && *tipoMidiaSelecionado_ == chave)
            tipoMidiaSelecionado_ = std::nullopt;
        else
            tipoMidiaSelecionado_ = chave;
        atualizarContagens();
    }

    atualizarBotoesSidebar();

    if (!mosaico_) return;

    if (mosaicoViewport_) mosaicoViewport_->setVisible(true);
    mosaico_->definirSubpastas({});
    pastaNavegarAtual_ = std::nullopt;
    caminhoNavegacao_.clear();

    const auto& libChave = categorias_[static_cast<size_t>(categoriaSelecionada_)].chave;
    if (libChave == "folders") {
        navegarParaPastaOrigem(std::nullopt);
        return;
    }

    aplicarFiltrosAdicionais();
}

void CatalogWorkspaceComponent::atualizarBotoesSidebar() {
    for (size_t i = 0; i < botoesCategorias_.size(); ++i) {
        int idx = static_cast<int>(i);
        const auto& ch = categorias_[i].chave;
        bool ativo = false;
        if (idx < indiceInicioMediaType_)
            ativo = (idx == categoriaSelecionada_);
        else
            ativo = (tipoMidiaSelecionado_.has_value() && *tipoMidiaSelecionado_ == ch);
        botoesCategorias_[i]->setColour(juce::TextButton::buttonColourId,
            ativo ? tema().acento.withAlpha(0.2f) : juce::Colours::transparentBlack);
        botoesCategorias_[i]->setColour(juce::TextButton::textColourOffId,
            ativo ? tema().textoPrimario : tema().textoSecundario);
    }
}

void CatalogWorkspaceComponent::navegarParaPastaOrigem(std::optional<std::string> nomePasta) {
    auto raiz = projeto_.arvoreOrigem(true);

    const auto& libChave = categorias_.empty()
        ? std::string()
        : categorias_[static_cast<size_t>(categoriaSelecionada_)].chave;

    if (!nomePasta.has_value()) {
        caminhoNavegacao_.clear();
    } else {
        auto it = std::find(caminhoNavegacao_.begin(), caminhoNavegacao_.end(), *nomePasta);
        if (it != caminhoNavegacao_.end()) {
            caminhoNavegacao_.erase(it + 1, caminhoNavegacao_.end());
        } else {
            caminhoNavegacao_.push_back(*nomePasta);
        }
    }

    const ProjetoAberto::NoArvore* alvo = &raiz;
    for (auto& segmento : caminhoNavegacao_) {
        bool encontrado = false;
        for (auto& f : alvo->filhos) {
            if (f.nome.toStdString() == segmento) {
                alvo = &f;
                encontrado = true;
                break;
            }
        }
        if (!encontrado) break;
    }

    pastaNavegarAtual_ = nomePasta;

    if (lblCaminhoNavegacao_) {
        if (caminhoNavegacao_.empty()) {
            lblCaminhoNavegacao_->setVisible(false);
        } else {
            juce::String caminho = "  /";
            for (auto& seg : caminhoNavegacao_)
                caminho += " " + juce::String(seg) + " /";
            caminho = caminho.dropLastCharacters(2);
            lblCaminhoNavegacao_->setText(caminho, juce::dontSendNotification);
            lblCaminhoNavegacao_->setVisible(true);
        }
    }

    std::vector<SubpastaInfo> subpastas;
    if (!caminhoNavegacao_.empty()) {
        SubpastaInfo voltar;
        voltar.nome = juce::CharPointer_UTF8("\xe2\x86\x90 Back");
        voltar.quantidade = 0;
        voltar.itemIds = {};
        subpastas.push_back(voltar);
    }
    for (auto& filho : alvo->filhos) {
        if (filho.filhos.empty() && filho.itemIds.empty()) continue;
        SubpastaInfo sub;
        sub.nome = filho.nome;
        sub.quantidade = static_cast<int>(filho.itemIds.size());
        sub.itemIds = filho.itemIds;
        subpastas.push_back(sub);
    }

    mosaico_->definirSubpastas(subpastas);
    mosaico_->aoNavegarParaSubpasta = [this](const SubpastaInfo& sub) {
        if (sub.nome.startsWith(juce::CharPointer_UTF8("\xe2\x86\x90"))) {
            if (caminhoNavegacao_.size() <= 1)
                navegarParaPastaOrigem(std::nullopt);
            else
                navegarParaPastaOrigem(caminhoNavegacao_[caminhoNavegacao_.size() - 2]);
        } else {
            navegarParaPastaOrigem(sub.nome.toStdString());
        }
    };

    mosaico_->definirFiltroItens(alvo->itemIdsDiretos);
    mosaico_->recarregar();
    resized();
}

void CatalogWorkspaceComponent::revalidarPastaAtual() {
    if (caminhoNavegacao_.empty()) {
        navegarParaPastaOrigem(std::nullopt);
    } else {
        auto ultimo = caminhoNavegacao_.back();
        navegarParaPastaOrigem(ultimo);
    }
}

void CatalogWorkspaceComponent::selecionarItem(const std::string& itemId) {
    if (fichaPanel_) {
        auto sel = mosaico_ ? mosaico_->itensSelecionados() : std::set<std::string>{};
        fichaPanel_->mostrarSelecao(std::vector<std::string>(sel.begin(), sel.end()));
    }
}

void CatalogWorkspaceComponent::abrirWorkbench(const std::string& itemId) {
    activePreviewWindow_.reset();

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    auto aoFechar = [safeThis]() {
        juce::MessageManager::callAsync([safeThis]() {
            if (!safeThis) return;
            safeThis->activePreviewWindow_.reset();
            safeThis->recarregar();
        });
    };

    auto aoNavegar = [safeThis](const std::string& currentId, int dir) -> std::optional<std::string> {
        if (!safeThis || !safeThis->mosaico_) return std::nullopt;
        return safeThis->mosaico_->itemAdjacente(currentId, dir);
    };
    auto aoItemMudou = [safeThis](const std::string& newItemId) {
        if (!safeThis || !safeThis->mosaico_) return;
        safeThis->mosaico_->selecionarItem(newItemId);
    };

    activePreviewWindow_ = std::make_unique<FloatingPreviewWindow>(projeto_, itemId, aoFechar, aoNavegar, aoItemMudou);
}

void CatalogWorkspaceComponent::fecharWorkbench() {
    activePreviewWindow_.reset();
    recarregar();
}

void CatalogWorkspaceComponent::abrirRelinkOffline(const std::string& itemId) {
    std::string titulo, tipoMidia, codigoAcervo;
    projeto_.obterItemInfo(itemId, titulo, tipoMidia, codigoAcervo);

    juce::String expectedPath;
    juce::String storageName = "Local Storage";

    try {
        auto stmt = projeto_.projeto().registro().prepare(
            "SELECT a.caminho_relativo, COALESCE(a.caminho_absoluto_origem, ''), COALESCE(v.nome, 'Local Storage'), COALESCE(v.localizacao, '') "
            "FROM arquivo a "
            "LEFT JOIN vault v ON v.id = a.vault_id "
            "WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) {
            std::string camRel = stmt.columnText(0);
            std::string camAbs = stmt.columnText(1);
            storageName = stmt.columnText(2);
            std::string locVault = stmt.columnText(3);
            auto expFile = matriz::vault::caminhoEsperado(projeto_.projeto().pasta(), locVault, camRel, camAbs);
            expectedPath = expFile != juce::File() ? expFile.getFullPathName() : (camAbs.empty() ? camRel : camAbs);
        }
    } catch (...) {}

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    OfflineAssetRelinkDialog::showModal(
        projeto_.projeto().registro(),
        itemId,
        juce::String(titulo),
        expectedPath,
        storageName,
        [safeThis, itemId](const juce::File& fileSelected) {
            if (!safeThis) return;
            std::string novoItemId;
            juce::String err;
            bool ok = matriz::vault::AssetRelinkEngine::executarRelinkIndividual(
                safeThis->projeto_.projeto().registro(),
                safeThis->projeto_.projeto().pasta(),
                itemId,
                fileSelected,
                true,
                novoItemId,
                err);
            if (ok) {
                safeThis->projeto_.transferirMarcacoes(itemId, novoItemId);
                safeThis->projeto_.salvar();
                if (safeThis->mosaico_) safeThis->mosaico_->recarregar();
            }
        });
}

void CatalogWorkspaceComponent::abrirMenuContexto(std::vector<std::string> itemIds) {
    if (itemIds.empty()) return;

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    acoes::Ganchos ganchos;
    ganchos.aoMudarDados = [safeThis] {
        if (safeThis) safeThis->recarregar();
    };
    ganchos.aoFiltrarItens = [safeThis](std::set<std::string> ids) {
        if (safeThis) safeThis->filtrarPorIds(std::move(ids));
    };

    auto menu = acoes::construirMenu(projeto_, itemIds);

    if (itemIds.size() > 1) {
        menu.addSeparator();
        menu.addItem(500, matriz::i18n::t("menu.agrupar_pasta"));
    }

    juce::PopupMenu subMenuPastas;
    auto arvore = projeto_.arvoreAcervo();
    std::vector<std::pair<std::string, juce::String>> pastas;

    std::function<void(const ProjetoAberto::NoArvore&, const juce::String&)> coletarPastas =
        [&](const ProjetoAberto::NoArvore& n, const juce::String& prefixo) {
        for (const auto& f : n.filhos) {
            if (f.id.empty()) continue;
            juce::String caminho = prefixo.isEmpty() ? f.nome : prefixo + " / " + f.nome;
            pastas.push_back({f.id, caminho});
            coletarPastas(f, caminho);
        }
    };
    coletarPastas(arvore, {});

    int pastaIdx = 1000;
    for (const auto& p : pastas) {
        subMenuPastas.addItem(pastaIdx++, p.second);
    }
    menu.addSubMenu(matriz::i18n::t("menu.mover_para_pasta"), subMenuPastas);

    ProjetoAberto* p = &projeto_;
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, p, itemIds, ganchos, pastas](int resultado) {
        if (!safeThis) return;
        if (resultado == 500) {
            std::string newFolderId = p->agruparItensEmNovaPasta(itemIds);
            if (safeThis->aoAgruparEIrParaTree && !newFolderId.empty()) {
                safeThis->aoAgruparEIrParaTree(newFolderId);
            }
            return;
        }
        if (resultado >= 1000 && resultado < 1000 + static_cast<int>(pastas.size())) {
            std::string pastaId = pastas[static_cast<size_t>(resultado - 1000)].first;
            p->adicionarItensAPasta(itemIds, pastaId);
            safeThis->recarregar();
            return;
        }
        acoes::executar(resultado, *p, itemIds, ganchos);
    });
}

void CatalogWorkspaceComponent::recarregar() {
    atualizarContagens();
    construirFiltroAnos();
    construirFiltroCollection();
    atualizarBotoesSidebar();

    const auto& libChave = categorias_.empty()
        ? std::string()
        : categorias_[static_cast<size_t>(categoriaSelecionada_)].chave;
    if (libChave == "folders") {
        revalidarPastaAtual();
    } else {
        aplicarFiltrosAdicionais();
    }

    resized();
    if (mosaico_) mosaico_->recarregar();
}


void CatalogWorkspaceComponent::filtrarPorChave(const std::string& chave) {
    for (size_t i = 0; i < categorias_.size(); ++i) {
        if (categorias_[i].chave == chave) {
            selecionarCategoria(static_cast<int>(i));
            return;
        }
    }
    // Keys from PreservationWorkspace that aren't sidebar categories —
    // apply them as embedded-collection filters directly.
    if (mosaico_) {
        auto ids = projeto_.itensDaColecaoEmbutida(chave);
        mosaico_->definirFiltroItens(std::move(ids));
        mosaico_->recarregar();
    }
}

void CatalogWorkspaceComponent::filtrarPorIds(std::set<std::string> ids) {
    if (mosaico_) {
        mosaico_->definirFiltroItens(std::move(ids));
        mosaico_->recarregar();
    }
}

std::set<std::string> CatalogWorkspaceComponent::itensSelecionados() const {
    if (mosaico_) return mosaico_->itensSelecionados();
    return {};
}

void CatalogWorkspaceComponent::renomearSelecionados() {
    if (mosaico_) mosaico_->renomearSelecao();
}

void CatalogWorkspaceComponent::removerSelecionadosDoBackup() {
    if (mosaico_) mosaico_->removerSelecaoDoBackup();
}

void CatalogWorkspaceComponent::selecionarFiltroSelecionados() {
    for (size_t i = 0; i < categorias_.size(); ++i) {
        if (categorias_[i].chave == "selected") {
            selecionarCategoria(static_cast<int>(i));
            break;
        }
    }
}

void CatalogWorkspaceComponent::selecionarFiltroTodos() {
    for (size_t i = 0; i < categorias_.size(); ++i) {
        if (categorias_[i].chave == "all") {
            selecionarCategoria(static_cast<int>(i));
            break;
        }
    }
}

void CatalogWorkspaceComponent::definirSelecaoItens(const std::set<std::string>& itemIds) {
    categoriaSelecionada_ = 0;
    tipoMidiaSelecionado_ = std::nullopt;
    anosSelecionados_.clear();
    collectionSelecionado_ = std::nullopt;
    if (mosaico_) {
        mosaico_->definirFiltroItens(itemIds);
        mosaico_->definirSelecao(itemIds);
    }
    repaint();
}

void CatalogWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    auto sidebar = getLocalBounds().removeFromLeft(kLarguraSidebar);
    g.setColour(tk.fundo);
    g.fillRect(sidebar);
    g.setColour(tk.borda);
    g.fillRect(sidebar.getRight() - 1, sidebar.getY(), 1, sidebar.getHeight());

    // Cards da coluna esquerda (item 7): LIBRARY / MEDIA TYPE / DATE, cada
    // um com fundo e borda próprios, em vez de botões soltos direto no
    // fundo da sidebar.
    for (const auto& cardBounds : secaoCardBounds_) {
        g.setColour(tk.painelAlt.withAlpha(0.25f));
        g.fillRoundedRectangle(cardBounds.toFloat(), tk.raioPequeno);
        g.setColour(tk.borda.withAlpha(0.7f));
        g.drawRoundedRectangle(cardBounds.toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);
    }

    for (auto& [titulo, bounds] : secaoHeaderBounds_) {
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(titulo, bounds, juce::Justification::centredLeft);
    }

    for (size_t i = 0; i < categorias_.size(); ++i) {
        juce::Colour cor;
        const auto& ch = categorias_[i].chave;
        if      (ch == "audio")     cor = juce::Colour(0xff2a9d8f);
        else if (ch == "video")     cor = juce::Colour(0xff9d4edd);
        else if (ch == "images")    cor = juce::Colour(0xfff4a261);
        else if (ch == "documents") cor = juce::Colour(0xff2b9348);
        else if (ch == "sessions")  cor = juce::Colour(0xff2b9348);
        else continue;
        auto btnBounds = botoesCategorias_[i]->getBounds();
        float cy = btnBounds.getCentreY() - 4.0f;
        float cx = btnBounds.getX() + 2.0f;
        g.setColour(cor);
        g.fillEllipse(cx, cy, 8.0f, 8.0f);
    }

    if (!toolbarBounds_.isEmpty()) {
        g.setColour(tema().borda.withAlpha(0.5f));
        g.fillRect(toolbarBounds_.getX(), toolbarBounds_.getBottom() - 1, toolbarBounds_.getWidth(), 1);
    }
}

void CatalogWorkspaceComponent::resized() {
    secaoHeaderBounds_.clear();
    secaoCardBounds_.clear();
    auto area = getLocalBounds();

    auto sidebar = area.removeFromLeft(kLarguraSidebar);
    int availH = sidebar.getHeight();

    // Symmetrical, uniform button dimensions and typography
    const int kBtnH = 24;
    const int kHeaderH = 20;
    const int kItemGap = 2;

    // Calculate total required vertical height to distribute gaps without squashing bottom buttons
    int totalRequiredH = 8 + 28 + 4; // top margin + search + gap
    // Item 1 (lista nova de hoje): quando o chip de busca ativa está
    // visível ele entra na conta, senão a coluna fica mais folgada — não é
    // um "sempre presente" como o resto.
    if (chipBuscaAtiva_ && chipBuscaAtiva_->temTermos()) totalRequiredH += 24 + 3;
    totalRequiredH += 24 + 3 + 18 + 2; // View modes row 24, slider 18 (compactados no item 1 de hoje)
    if (btnSelecionarTodos_ && btnLimparSelecao_) totalRequiredH += 20 + kItemGap; // Select all / clear row
    totalRequiredH += 42 + kItemGap; // HOME: altura dobrada (comprimida a 42) pra caber ícone + "All Assets"
    totalRequiredH += (3 * (20 + 1)) + 6; // 3 toggle buttons
    // Card LIBRARY eliminado (item "Coluna esquerda da aba METADATA") — só
    // MEDIA TYPE e DATE continuam sendo cards.
    totalRequiredH += kHeaderH + kItemGap + ((static_cast<int>(botoesCategorias_.size()) - indiceInicioMediaType_) * (kBtnH + kItemGap)); // Media Type
    totalRequiredH += kHeaderH + kItemGap + kBtnH + kItemGap; // Content Type (item 4, nova lista): header + um combo só
    totalRequiredH += kHeaderH + kItemGap; // Date
    totalRequiredH += 22 + kItemGap; // Item 5/6 (nova lista): FROM/TO/CLEAR saíram, entrou o toggle "multiple years"
    const int kCardPad = 6; // respiro interno do card (item 7), topo + base
    totalRequiredH += 3 * (2 * kCardPad); // Media Type, Content Type e Date
    // A seção DATE agora vive num Viewport próprio e usa todo o espaço
    // vertical restante da sidebar (item 5) — não entra mais na conta de
    // altura fixa.
    int extraSpace = availH - totalRequiredH;
    int numGaps = 4;
    int sectionSpacing = juce::jlimit(6, 24, extraSpace > 0 ? (extraSpace / (numGaps + 1)) : 4);

    sidebar.removeFromTop(8);

    auto buscaArea = sidebar.removeFromTop(28).reduced(8, 2);
    // Os dois botões ficam à DIREITA do campo, nesta ordem a partir da borda:
    // × (limpar) na ponta e a lupa logo à esquerda dele. O campo de texto
    // começa colado na borda esquerda — sem vão sobrando de onde a lupa saiu.
    if (btnLimparBusca_) {
        btnLimparBusca_->setBounds(buscaArea.removeFromRight(24));
    }
    if (btnLupaBusca_) {
        btnLupaBusca_->setBounds(buscaArea.removeFromRight(26));
        buscaArea.removeFromRight(2);
    }
    campoBusca_->setBounds(buscaArea);
    sidebar.removeFromTop(4);

    // Item 1 (lista nova de hoje): chip de busca ativa logo abaixo da
    // caixa — só ocupa espaço quando há um termo filtrando, senão a linha
    // desaparece e o resto da coluna sobe pro lugar de sempre. Pra caber
    // sem empurrar o card MEDIA TYPE pra baixo, o grupo de controles daqui
    // até SHOW RECENTLY INGESTED ficou um pouco mais compacto (linhas e
    // vãos alguns px menores) — ver alturas ajustadas abaixo.
    if (chipBuscaAtiva_ && chipBuscaAtiva_->temTermos() && viewportChipsBusca_) {
        auto chipArea = sidebar.removeFromTop(24).reduced(8, 2);
        // O viewport é o frame visível (rola na horizontal quando os chips
        // não cabem); chipBuscaAtiva_ mantém sua própria largura de
        // conteúdo (calculada em definirTermos) e só a altura acompanha o
        // frame.
        chipBuscaAtiva_->setSize(juce::jmax(chipBuscaAtiva_->getWidth(), 1), chipArea.getHeight());
        viewportChipsBusca_->setBounds(chipArea);
        sidebar.removeFromTop(3);
    } else if (viewportChipsBusca_) {
        viewportChipsBusca_->setBounds(0, 0, 0, 0);
    }

    // 0. VIEW MODES & DISPLAY CONTROLS (directly below Search)
    // Row 1: List and Miniatures side-by-side (50/50, matching Intake tab)
    if (btnVisaoLista_ && btnVisaoGrade_) {
        auto viewRow = sidebar.removeFromTop(24).reduced(4, 0);
        int half = (viewRow.getWidth() - 6) / 2;
        btnVisaoLista_->setBounds(viewRow.removeFromLeft(half));
        viewRow.removeFromLeft(6);
        btnVisaoGrade_->setBounds(viewRow);
        sidebar.removeFromTop(3);
    }
    // Row 2: Size slider
    if (sliderTamanho_) {
        auto sliderRow = sidebar.removeFromTop(18).reduced(4, 0);
        if (lblTamanho_) lblTamanho_->setBounds(sliderRow.removeFromLeft(20));
        sliderTamanho_->setBounds(sliderRow);
        sidebar.removeFromTop(2);
    }
    // Row 3: Select All & Deselect directly below slider
    if (btnSelecionarTodos_ && btnLimparSelecao_) {
        auto selRow = sidebar.removeFromTop(20).reduced(4, 0);
        int halfW = (selRow.getWidth() - 4) / 2;
        btnSelecionarTodos_->setBounds(selRow.removeFromLeft(halfW));
        selRow.removeFromLeft(4);
        btnLimparSelecao_->setBounds(selRow);
        sidebar.removeFromTop(2);
    }
    // HOME (item "Coluna esquerda da aba METADATA", A): era o botão "All
    // Assets" dentro do card LIBRARY — agora um botão ícone, acima de
    // "Hide Edited: OFF". Continua sendo categorias_[0]/botoesCategorias_[0]
    // (mesma seleção/realce/contagem de sempre), só a posição/aparência
    // mudou.
    if (!botoesCategorias_.empty()) {
        // Altura dobrada (22 -> 44, comprimida a 42 no item 1 de hoje pra
        // abrir espaço pro chip) para o ícone home.png caber acima do
        // rótulo "All Assets".
        botoesCategorias_[0]->setBounds(sidebar.removeFromTop(42).reduced(4, 0));
        sidebar.removeFromTop(2);
    }
    if (btnOcultarEditados_) {
        btnOcultarEditados_->setBounds(sidebar.removeFromTop(20).reduced(4, 0));
        sidebar.removeFromTop(1);
    }
    if (btnOcultarNaoSelecionados_) {
        btnOcultarNaoSelecionados_->setBounds(sidebar.removeFromTop(20).reduced(4, 0));
        sidebar.removeFromTop(1);
    }
    if (btnMostrarRecentes_) {
        btnMostrarRecentes_->setBounds(sidebar.removeFromTop(20).reduced(4, 0));
        sidebar.removeFromTop(1);
    }
    sidebar.removeFromTop(sectionSpacing);

    // Cada seção da coluna esquerda vira um card com borda própria (item 7):
    // um respiro fixo no topo/base marca onde o card começa e termina, e o
    // retângulo resultante (header + conteúdo + respiro) é guardado pra
    // paint() desenhar o contorno.
    int cardTop = 0;
    auto iniciarCard = [&] {
        sidebar.removeFromTop(kCardPad);
        cardTop = sidebar.getY();
    };
    auto finalizarCard = [&] {
        sidebar.removeFromTop(kCardPad);
        secaoCardBounds_.push_back(juce::Rectangle<int>(4, cardTop - kCardPad, kLarguraSidebar - 8,
                                                          sidebar.getY() - cardTop + kCardPad));
    };

    // Card LIBRARY eliminado por completo (item "Coluna esquerda da aba
    // METADATA", B) — MEDIA TYPE é o primeiro card agora.

    // 2. MEDIA TYPE
    iniciarCard();
    secaoHeaderBounds_.push_back({matriz::i18n::t("catwork.secao_media_type"), sidebar.removeFromTop(kHeaderH).reduced(8, 0)});
    sidebar.removeFromTop(kItemGap);
    for (size_t i = static_cast<size_t>(indiceInicioMediaType_); i < botoesCategorias_.size(); ++i) {
        auto btnArea = sidebar.removeFromTop(kBtnH).reduced(4, 0);
        btnArea.removeFromLeft(14);
        botoesCategorias_[i]->setBounds(btnArea);
        sidebar.removeFromTop(kItemGap);
    }
    finalizarCard();
    sidebar.removeFromTop(sectionSpacing);

    // 2b. CONTENT TYPE (item 4, nova lista) — card próprio logo abaixo de
    // MEDIA TYPE, um dropdown só, sem lista de botões.
    iniciarCard();
    secaoHeaderBounds_.push_back({matriz::i18n::t("catwork.secao_content_type"), sidebar.removeFromTop(kHeaderH).reduced(8, 0)});
    sidebar.removeFromTop(kItemGap);
    if (comboContentType_) {
        comboContentType_->setBounds(sidebar.removeFromTop(kBtnH).reduced(4, 0));
        sidebar.removeFromTop(kItemGap);
    }
    finalizarCard();
    sidebar.removeFromTop(sectionSpacing);

    // 3. DATE — o card fecha só depois do grid de anos, no fim da função,
    // porque essa seção usa todo o espaço vertical que sobrar da sidebar.
    iniciarCard();
    secaoHeaderBounds_.push_back({matriz::i18n::t("catwork.secao_date"), sidebar.removeFromTop(kHeaderH).reduced(8, 0)});
    sidebar.removeFromTop(kItemGap);

    // Item 5/6 (nova lista): FROM/TO/CLEAR deram lugar a este toggle —
    // marcar vários anos nos botões abaixo faz o papel do range antigo.
    if (toggleMultiplosAnos_) {
        toggleMultiplosAnos_->setBounds(sidebar.removeFromTop(22).reduced(6, 0));
        sidebar.removeFromTop(kItemGap);
    }

    // O que sobrar da sidebar (espaço liberado pela remoção do filtro
    // CONTENT) vai inteiro para a lista de anos, que rola internamente
    // quando não couber — nenhum ANO fica oculto sem acesso.
    if (anosViewport_) {
        auto anosArea = sidebar;
        anosViewport_->setBounds(anosArea);

        int numAnoRows = (static_cast<int>(botoesAnos_.size()) + 1) / 2;
        int contentH = numAnoRows * (kBtnH + kItemGap);
        int containerW = anosArea.getWidth() - anosViewport_->getScrollBarThickness();
        anosContainer_.setSize(juce::jmax(containerW, 0), contentH);

        for (size_t i = 0; i < botoesAnos_.size(); i += 2) {
            auto rowArea = juce::Rectangle<int>(0, static_cast<int>(i / 2) * (kBtnH + kItemGap),
                                                 anosContainer_.getWidth(), kBtnH).reduced(4, 0);
            int halfW = rowArea.getWidth() / 2;
            botoesAnos_[i]->setBounds(rowArea.removeFromLeft(halfW).reduced(2, 0));
            if (i + 1 < botoesAnos_.size()) {
                botoesAnos_[i + 1]->setBounds(rowArea.reduced(2, 0));
            }
        }
    }

    // Fecha o card DATE no fundo da sidebar — essa seção usa todo o espaço
    // vertical que sobrar, então o card acompanha (sidebar.getBottom() não
    // muda com os removeFromTop() acima: cada um empurra o topo, nunca o
    // fundo).
    secaoCardBounds_.push_back(juce::Rectangle<int>(4, cardTop - kCardPad, kLarguraSidebar - 8,
                                                      sidebar.getBottom() - (cardTop - kCardPad)));

    if (!fichaColapsada_ && fichaPanel_) {
        fichaPanel_->setVisible(true);
        auto fichaArea = area.removeFromRight(larguraFicha_);
        fichaPanel_->setBounds(fichaArea);

        if (fichaResizerBar_) {
            fichaResizerBar_->setVisible(true);
            fichaResizerBar_->setBounds(fichaArea.getX() - 4, fichaArea.getY(), 6, fichaArea.getHeight());
        }
    } else {
        if (fichaPanel_) fichaPanel_->setVisible(false);
        if (fichaResizerBar_) fichaResizerBar_->setVisible(false);
    }

    auto toolbar = area.removeFromTop(36);
    toolbarBounds_ = toolbar;
    toolbar = toolbar.reduced(8, 4);

    if (btnToggleFicha_) {
        int toggleW = fichaColapsada_ ? 95 : 28;
        btnToggleFicha_->setBounds(toolbar.removeFromRight(toggleW));
        toolbar.removeFromRight(8);
    }

    if (legendaAtalhos_) {
        legendaAtalhos_->setBounds(toolbar);
    }

    if (lblCaminhoNavegacao_ && lblCaminhoNavegacao_->isVisible()) {
        lblCaminhoNavegacao_->setBounds(area.removeFromTop(24));
    }

    if (mosaicoViewport_) mosaicoViewport_->setBounds(area);
    if (mosaico_) {
        mosaico_->setSize(mosaicoViewport_->getWidth() - mosaicoViewport_->getScrollBarThickness(),
                          mosaico_->getHeight());
    }
}

} // namespace matriz::ui
