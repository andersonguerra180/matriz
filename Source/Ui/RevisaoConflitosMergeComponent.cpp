#include "RevisaoConflitosMergeComponent.h"

#include <algorithm>
#include <optional>

#include "ConflitosMergeDialog.h"
#include "Moldura.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::ui {

namespace mg = matriz::model::merge;

namespace {
// Mesmas medidas do card da aba Duplicates (sub-card com faixa de 24px, miniatura colada à esquerda,
// painel de 160px à direita com o checkbox e os dois botões).
constexpr int kAlturaSubCard = 132;
constexpr int kAlturaResumo = 28;
constexpr int kLarguraBotoes = 160;
constexpr int kAlturaValor = 46;  // legenda (14) + botão (30) + respiro
constexpr int kValoresPorLinha = 3;
constexpr int kEntreCards = 10;
constexpr int kGrupoRadioBase = 9100;

const juce::Colour kAmbar(0xfff59e0b);
const juce::Colour kVerde(0xff22c55e);

int numValores(const mg::GrupoConflito& g) { return 1 + static_cast<int>(g.alternativas.size()); }

int alturaDoCard(const mg::GrupoConflito& g) {
    const int linhas = (numValores(g) + kValoresPorLinha - 1) / kValoresPorLinha;
    return std::max(kAlturaSubCard, 22 + linhas * kAlturaValor) + 22;
}

// Id do combo de tipo de mídia (mesmos itens do filtro da aba Duplicates).
int categoriaDoItem(const std::string& tipoMidia, const std::string& caminho) {
    static const std::set<std::string> audio{"digital_audio", "audio", "cassete", "vinil", "fita_rolo", "cd", "dat", "minidisc",
                                             "sample", "sound_effects", "field_recording", "release"};
    static const std::set<std::string> video{"digital_video", "video", "vhs", "betacam", "betamax", "filme", "umatic", "dvd"};
    static const std::set<std::string> imagem{"foto", "negativo", "slide", "cover_art", "imagem"};
    static const std::set<std::string> docs{"documento", "3d_file", "docs", "texto"};
    if (imagem.count(tipoMidia)) return 2;
    if (video.count(tipoMidia)) return 3;
    if (audio.count(tipoMidia)) return 4;
    if (docs.count(tipoMidia)) return 5;
    if (tipoMidia == "sessao") return 6;
    using C = matriz::ingest::CategoriaMidia;
    switch (matriz::ingest::categoriaPorExtensao(juce::File::createFileWithoutCheckingPath(
                                                     juce::String::fromUTF8(caminho.c_str())).getFileExtension().trimCharactersAtStart("."))) {
        case C::Imagem: case C::Arte: return 2;
        case C::Video: return 3;
        case C::Audio: return 4;
        case C::Documento: case C::Texto: return 5;
        case C::Sessao: return 6;
        default: return 7;
    }
}

bool ehDePacote(const std::string& origem) { return origem.rfind("package ", 0) == 0; }

// A origem fica gravada em inglês no histórico ("duplicate resolved" / "package X").
juce::String origemLegivel(const std::string& origem) {
    if (ehDePacote(origem))
        return matriz::i18n::t("merge.janela.origem_pacote").replace("{nome}", juce::String::fromUTF8(origem.substr(8).c_str()));
    if (origem == "duplicate resolved") return matriz::i18n::t("merge.janela.origem_duplicata");
    return juce::String::fromUTF8(origem.c_str());
}

juce::String valorLegivel(const std::string& campo, const std::string& valor) {
    const auto v = mg::resumoDoValor(campo, valor);
    return v.isEmpty() ? juce::String("-") : v;
}

juce::String nomeDoItem(const mg::GrupoConflito& g) {
    if (!g.titulo.empty()) return juce::String::fromUTF8(g.titulo.c_str());
    const juce::String c = juce::String::fromUTF8(g.caminho.c_str());
    return c.fromLastOccurrenceOf("/", false, false);
}

const std::string& valorDaEscolha(const mg::GrupoConflito& g, int escolha) {
    return escolha <= 0 || escolha > static_cast<int>(g.alternativas.size()) ? g.valorAtual
                                                                               : g.alternativas[static_cast<size_t>(escolha - 1)].valor;
}
}  // namespace

juce::Colour RevisaoConflitosMergeComponent::corDoCampo(const std::string& campo) {
    const auto& tk = tema();
    if (campo == "ano" || campo == "dc_created" || campo == "dc_issued") return tk.tipoVideo;      // datas: roxo
    if (campo == mg::kCampoGeo) return kVerde;                                                      // GEO: verde
    if (campo == "tipo_midia" || campo == "collection_type" || campo == "content_type" || campo == "source_media" ||
        campo == "lista" || campo == "dc_type" || campo == "dc_format")
        return kAmbar;                                                                              // classificação
    if (campo.rfind("dc_", 0) == 0 || campo == "titulo" || campo == "isrc") return tk.acento;     // texto da ficha
    return tk.tipoDocumento;
}

// ---------------------------------------------------------------------------
// Card de um conflito (item × campo). Lê o estado de owner.entradas_[idx]: a
// lista é virtual, então o card some e volta ao rolar sem perder a escolha.
// ---------------------------------------------------------------------------
class RevisaoConflitosMergeComponent::CardComponent : public juce::Component {
public:
    CardComponent(RevisaoConflitosMergeComponent& owner, size_t idx) : owner_(owner), idx_(idx) {
        const auto& e = owner_.entradas_[idx_];
        const auto cor = corDoCampo(e.grupo.campo);

        chkSelecionar_.setButtonText(matriz::i18n::t("merge.janela.selecionar"));
        chkSelecionar_.setToggleState(e.selecionado, juce::dontSendNotification);
        chkSelecionar_.onClick = [this] {
            owner_.entradas_[idx_].selecionado = chkSelecionar_.getToggleState();
            owner_.atualizarBotoes();
        };
        addAndMakeVisible(chkSelecionar_);

        btnAplicar_.setButtonText(matriz::i18n::t("merge.janela.btn_aplicar"));
        btnAplicar_.setColour(juce::TextButton::buttonColourId, kVerde);
        btnAplicar_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btnAplicar_.onClick = [this] {
            const auto& en = owner_.entradas_[idx_];
            owner_.aplicar({{idx_, valorDaEscolha(en.grupo, en.escolha)}}, false);
        };
        addAndMakeVisible(btnAplicar_);

        btnManter_.setButtonText(matriz::i18n::t("merge.janela.btn_manter"));
        btnManter_.setColour(juce::TextButton::buttonColourId, tema().painelAlt);
        btnManter_.setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
        btnManter_.onClick = [this] { owner_.aplicar({{idx_, owner_.entradas_[idx_].grupo.valorAtual}}, false); };
        addAndMakeVisible(btnManter_);

        for (int k = 0; k < numValores(e.grupo); ++k) {
            const std::string& valor = valorDaEscolha(e.grupo, k);
            auto* b = valores_.add(new juce::TextButton(valorLegivel(e.grupo.campo, valor)));
            b->setRadioGroupId(kGrupoRadioBase + static_cast<int>(idx_ % 100000));
            b->setClickingTogglesState(true);
            b->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
            b->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
            b->setColour(juce::TextButton::buttonOnColourId, cor);
            b->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
            juce::String dica = juce::String::fromUTF8(valor.c_str());
            if (k > 0) dica << "\n" << origemLegivel(e.grupo.alternativas[static_cast<size_t>(k - 1)].origem);
            b->setTooltip(dica);
            b->setToggleState(k == e.escolha, juce::dontSendNotification);
            b->onClick = [this, k] {
                owner_.entradas_[idx_].escolha = k;
                repaint();
            };
            addAndMakeVisible(b);
        }
        atualizarHabilitados();
    }

    void sincronizar() {
        chkSelecionar_.setToggleState(owner_.entradas_[idx_].selecionado, juce::dontSendNotification);
        atualizarHabilitados();
    }

    void atualizarHabilitados() {
        for (auto* c : std::initializer_list<juce::Component*>{&btnAplicar_, &btnManter_}) c->setEnabled(!owner_.ocupado_);
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        const auto& e = owner_.entradas_[idx_];
        const auto cor = corDoCampo(e.grupo.campo);
        const int n = numValores(e.grupo);

        // Moldura externa: borda na cor do campo, como a borda por critério na aba Duplicates.
        g.setColour(tk.painel);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioMedio);
        g.setColour(cor.withAlpha(e.selecionado ? 1.0f : 0.6f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), tk.raioMedio, e.selecionado ? 2.5f : 1.5f);

        // Sub-card do arquivo (mesmo desenho do sub-card de Duplicates).
        const auto sub = areaSubCard();
        const auto subF = sub.toFloat();
        g.setColour(tk.painelAlt.withAlpha(0.35f));
        g.fillRoundedRectangle(subF, 6.0f);
        g.setColour(tk.borda.withAlpha(0.70f));
        g.drawRoundedRectangle(subF, 6.0f, 1.0f);

        g.saveState();
        g.reduceClipRegion(sub.withHeight(24));
        g.setColour(cor);
        g.fillRoundedRectangle(subF.withHeight(24.0f), 6.0f);
        g.fillRect(subF.withTrimmedTop(12.0f).withHeight(12.0f));
        g.restoreState();
        g.setColour(tk.borda.withAlpha(0.40f));
        g.drawHorizontalLine(sub.getY() + 24, subF.getX(), subF.getRight());

        // Selo de quantos valores diferentes (âmbar a partir de 3: não dá para resolver com "o outro").
        const juce::String rotuloSelo = matriz::i18n::t("merge.janela.n_valores").replace("{n}", juce::String(n));
        const juce::Font fonteSelo(juce::FontOptions(tk.tamanhoFonteMicro, juce::Font::bold));
        const int larguraSelo = juce::GlyphArrangement::getStringWidthInt(fonteSelo, rotuloSelo) + 14;
        juce::Rectangle<int> selo(sub.getRight() - larguraSelo - 6, sub.getY() + 4, larguraSelo, 16);
        g.setColour(n >= 3 ? juce::Colour(0xffef4444) : juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(selo.toFloat(), 8.0f);
        g.setColour(juce::Colours::white);
        g.setFont(fonteSelo);
        g.drawText(rotuloSelo, selo, juce::Justification::centred);

        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        juce::String cabecalho = ConflitosMergeDialog::rotuloDoCampo(e.grupo.campo);
        if (!e.grupo.codigo.empty()) cabecalho << " · " << juce::String::fromUTF8(e.grupo.codigo.c_str());
        g.drawText(cabecalho, sub.getX() + 8, sub.getY() + 2, sub.getWidth() - 22 - larguraSelo, 20,
                   juce::Justification::centredLeft, true);

        // Miniatura colada à esquerda, altura toda do sub-card.
        const int topo = sub.getY() + 25, altura = sub.getHeight() - 26;
        const juce::Rectangle<int> thumb(sub.getX() + 1, topo, juce::jmin(altura + 24, sub.getWidth() * 2 / 5), altura);
        g.saveState();
        {
            juce::Path clip;
            clip.addRoundedRectangle(thumb.toFloat().getX(), thumb.toFloat().getY(), thumb.toFloat().getWidth(),
                                     thumb.toFloat().getHeight(), 5.0f, 5.0f, false, false, true, false);
            g.reduceClipRegion(clip);
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRect(thumb);
            auto it = owner_.miniaturas_.find(e.grupo.itemId);
            if (it != owner_.miniaturas_.end() && it->second.isValid()) {
                g.setOpacity(1.0f);
                g.drawImageWithin(it->second, thumb.getX() + 4, thumb.getY() + 4, thumb.getWidth() - 8, thumb.getHeight() - 8,
                                  juce::RectanglePlacement::centred, false);
            } else {
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
                const juce::String ext = juce::String::fromUTF8(e.grupo.caminho.c_str()).fromLastOccurrenceOf(".", false, false);
                g.drawText(ext.toUpperCase(), thumb, juce::Justification::centred);
            }
        }
        g.restoreState();
        g.setColour(tk.borda.withAlpha(0.50f));
        g.drawVerticalLine(thumb.getRight(), static_cast<float>(thumb.getY()), static_cast<float>(thumb.getBottom()));

        const int metaX = thumb.getRight() + 12, metaW = sub.getRight() - metaX - 10;
        int linhaY = topo + 6;
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo + 1.0f, juce::Font::bold)));
        g.setColour(tk.textoPrimario);
        g.drawText(nomeDoItem(e.grupo), metaX, linhaY, metaW, 18, juce::Justification::centredLeft, true);
        linhaY += 22;
        auto linhaInfo = [&](const juce::String& rotulo, const juce::String& valor) {
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.setColour(tk.textoTerciario);
            const int rotuloW = 64;
            g.drawText(rotulo, metaX, linhaY, rotuloW, 16, juce::Justification::centredLeft, true);
            g.setColour(tk.textoSecundario);
            g.drawText(valor, metaX + rotuloW, linhaY, metaW - rotuloW, 16, juce::Justification::centredLeft, true);
            linhaY += 17;
        };
        linhaInfo(matriz::i18n::t("merge.janela.codigo"), e.grupo.codigo.empty() ? juce::String("N/A")
                                                                                  : juce::String::fromUTF8(e.grupo.codigo.c_str()));
        linhaInfo(matriz::i18n::t("merge.janela.pasta"), e.grupo.pasta.empty() ? matriz::i18n::t("merge.janela.sem_pasta")
                                                                                : juce::String::fromUTF8(e.grupo.pasta.c_str()));
        linhaInfo(matriz::i18n::t("merge.janela.midia"), juce::String::fromUTF8(e.grupo.tipoMidia.c_str()).toUpperCase());
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteMicro)));
        g.setColour(tk.textoTerciario);
        g.drawFittedText(juce::String::fromUTF8(e.grupo.caminho.c_str()), metaX, sub.getBottom() - 32, metaW, 28,
                         juce::Justification::topLeft, 2, 1.0f);

        // Legenda em cima de cada valor: ATUAL / DE <origem>.
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteMicro, juce::Font::bold)));
        for (int k = 0; k < valores_.size(); ++k) {
            const auto r = areaValor(k);
            g.setColour(k == e.escolha ? cor : tk.textoTerciario);
            const juce::String legenda = k == 0 ? matriz::i18n::t("merge.janela.valor_atual")
                                                : matriz::i18n::t("merge.janela.valor_de")
                                                      .replace("{origem}", origemLegivel(e.grupo.alternativas[static_cast<size_t>(k - 1)].origem))
                                                      .toUpperCase();
            g.drawText(legenda, r.withHeight(14), juce::Justification::centredLeft, true);
        }
        g.setColour(tk.textoTerciario);
        g.drawText(matriz::i18n::t("merge.janela.valores"), areaValores().withHeight(16), juce::Justification::centredLeft, true);
    }

    void resized() override {
        const int w = getWidth();
        chkSelecionar_.setBounds(w - kLarguraBotoes - 14, 2, kLarguraBotoes, 20);
        btnAplicar_.setBounds(w - kLarguraBotoes - 14, 25, kLarguraBotoes, 32);
        btnManter_.setBounds(w - kLarguraBotoes - 14, 65, kLarguraBotoes, 32);
        for (int k = 0; k < valores_.size(); ++k) valores_[k]->setBounds(areaValor(k).withTrimmedTop(15).withHeight(30));
    }

private:
    juce::Rectangle<int> areaSubCard() const {
        const int util = getWidth() - kLarguraBotoes - 28 - 28;
        return {14, 10, juce::jlimit(260, 440, util * 9 / 20), getHeight() - 22};
    }
    juce::Rectangle<int> areaValores() const {
        const auto sub = areaSubCard();
        const int x = sub.getRight() + 14;
        return {x, 10, getWidth() - kLarguraBotoes - 28 - x, getHeight() - 22};
    }
    juce::Rectangle<int> areaValor(int k) const {
        auto area = areaValores().withTrimmedTop(20);
        const int largura = (area.getWidth() - (kValoresPorLinha - 1) * 8) / kValoresPorLinha;
        const int col = k % kValoresPorLinha, lin = k / kValoresPorLinha;
        return {area.getX() + col * (largura + 8), area.getY() + lin * kAlturaValor, largura, kAlturaValor - 2};
    }

    RevisaoConflitosMergeComponent& owner_;
    size_t idx_;
    juce::ToggleButton chkSelecionar_;
    juce::TextButton btnAplicar_, btnManter_;
    juce::OwnedArray<juce::TextButton> valores_;
};

// ---------------------------------------------------------------------------
// Lista virtual: só existem os cards da área visível (com folga de uma tela).
// ---------------------------------------------------------------------------
class RevisaoConflitosMergeComponent::ListaComponent : public juce::Component {
public:
    explicit ListaComponent(RevisaoConflitosMergeComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        const bool claro = tk.fundo.getBrightness() > 0.5f;
        g.fillAll((claro ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f));
    }

    // Recalcula as posições (filtros mudaram) e recria os cards visíveis.
    void reconstruir(int largura) {
        ativos_.clear();
        topos_.clear();
        int y = kEntreCards;
        for (auto idx : owner_.visiveis_) {
            topos_.push_back(y);
            y += alturaDoCard(owner_.entradas_[idx].grupo) + kEntreCards;
        }
        setSize(largura, y + kEntreCards);
    }

    void mostrar(juce::Rectangle<int> area) {
        area = area.expanded(0, area.getHeight());
        for (auto it = ativos_.begin(); it != ativos_.end();) {
            if (!it->second->getBounds().intersects(area)) it = ativos_.erase(it);
            else ++it;
        }
        for (size_t pos = 0; pos < topos_.size(); ++pos) {
            const size_t idx = owner_.visiveis_[pos];
            const juce::Rectangle<int> r(10, topos_[pos], getWidth() - 20, alturaDoCard(owner_.entradas_[idx].grupo));
            if (r.getY() > area.getBottom()) break;
            if (!r.intersects(area) || ativos_.count(pos)) continue;
            auto card = std::make_unique<CardComponent>(owner_, idx);
            card->setBounds(r);
            addAndMakeVisible(*card);
            ativos_[pos] = std::move(card);
        }
    }

    void sincronizarCards() {
        for (auto& [pos, card] : ativos_) card->sincronizar();
        repaint();
    }

    std::vector<size_t> visiveisNaArea(juce::Rectangle<int> area) const {
        std::vector<size_t> out;
        for (size_t pos = 0; pos < topos_.size(); ++pos) {
            const size_t idx = owner_.visiveis_[pos];
            const int h = alturaDoCard(owner_.entradas_[idx].grupo);
            if (topos_[pos] > area.getBottom()) break;
            if (topos_[pos] + h >= area.getY()) out.push_back(idx);
        }
        return out;
    }

private:
    RevisaoConflitosMergeComponent& owner_;
    std::vector<int> topos_;
    std::map<size_t, std::unique_ptr<CardComponent>> ativos_;  // posição na lista -> card
};

class RevisaoConflitosMergeComponent::ViewportLista : public juce::Viewport {
public:
    explicit ViewportLista(RevisaoConflitosMergeComponent& owner) : owner_(owner) {}
    void visibleAreaChanged(const juce::Rectangle<int>& area) override {
        if (owner_.lista_) owner_.lista_->mostrar(area);
        owner_.pedirMiniaturas(area);
    }

private:
    RevisaoConflitosMergeComponent& owner_;
};

// ---------------------------------------------------------------------------

RevisaoConflitosMergeComponent::RevisaoConflitosMergeComponent(ProjetoAberto& projeto, std::set<std::string> escopo)
    : projeto_(projeto), vivo_(projeto.tokenVida()), escopo_(std::move(escopo)) {
    auto rotulo = [this](juce::Label& l, const juce::String& texto) {
        l.setText(texto, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(l);
    };
    lblStatus_.setJustificationType(juce::Justification::centredLeft);
    lblStatus_.setColour(juce::Label::textColourId, tema().textoSecundario);
    addAndMakeVisible(lblStatus_);
    btnRecarregar_.setButtonText(matriz::i18n::t("merge.janela.recarregar"));
    btnRecarregar_.onClick = [this] { recarregar(); };
    addAndMakeVisible(btnRecarregar_);

    rotulo(lblCampo_, matriz::i18n::t("merge.janela.lbl_campo"));
    rotulo(lblMidia_, matriz::i18n::t("merge.janela.lbl_midia"));
    rotulo(lblPasta_, matriz::i18n::t("merge.janela.lbl_pasta"));
    rotulo(lblValores_, matriz::i18n::t("merge.janela.lbl_valores"));
    rotulo(lblOrigem_, matriz::i18n::t("merge.janela.lbl_origem"));

    cbMidia_.addItem(matriz::i18n::t("duplicatas.type_all"), 1);
    cbMidia_.addItem(matriz::i18n::t("duplicatas.type_image"), 2);
    cbMidia_.addItem(matriz::i18n::t("duplicatas.type_video"), 3);
    cbMidia_.addItem(matriz::i18n::t("duplicatas.type_audio"), 4);
    cbMidia_.addItem(matriz::i18n::t("duplicatas.type_docs"), 5);
    cbMidia_.addItem(matriz::i18n::t("duplicatas.type_sessions"), 6);
    cbMidia_.addItem(matriz::i18n::t("duplicatas.type_other"), 7);
    cbValores_.addItem(matriz::i18n::t("merge.janela.valores_qualquer"), 1);
    cbValores_.addItem(matriz::i18n::t("merge.janela.valores_2"), 2);
    cbValores_.addItem(matriz::i18n::t("merge.janela.valores_3"), 3);
    cbOrigem_.addItem(matriz::i18n::t("merge.janela.origem_todas"), 1);
    cbOrigem_.addItem(matriz::i18n::t("merge.janela.origem_duplicatas"), 2);
    cbOrigem_.addItem(matriz::i18n::t("merge.janela.origem_pacotes"), 3);
    for (auto* cb : {&cbCampo_, &cbMidia_, &cbPasta_, &cbValores_, &cbOrigem_}) {
        cb->setSelectedId(1, juce::dontSendNotification);
        cb->onChange = [this] { aplicarFiltros(); };
        addAndMakeVisible(*cb);
    }

    btnSelecionarTodos_.setButtonText(matriz::i18n::t("merge.janela.sel_todos"));
    btnSelecionarTodos_.onClick = [this] {
        for (auto idx : visiveis_) entradas_[idx].selecionado = true;
        lista_->sincronizarCards();
        atualizarBotoes();
    };
    btnSelecionarNenhum_.setButtonText(matriz::i18n::t("merge.janela.sel_nenhum"));
    btnSelecionarNenhum_.onClick = [this] {
        for (auto& e : entradas_) e.selecionado = false;
        lista_->sincronizarCards();
        atualizarBotoes();
    };
    for (auto* b : {&btnSelecionarTodos_, &btnSelecionarNenhum_}) addAndMakeVisible(*b);
    lblSelecionados_.setJustificationType(juce::Justification::centredLeft);
    lblSelecionados_.setColour(juce::Label::textColourId, tema().textoSecundario);
    addAndMakeVisible(lblSelecionados_);

    btnManterSel_.setButtonText(matriz::i18n::t("merge.janela.lote_manter"));
    btnManterSel_.onClick = [this] { aplicarSelecionados(1); };
    btnUsarOutroSel_.setButtonText(matriz::i18n::t("merge.janela.lote_outro"));
    btnUsarOutroSel_.setTooltip(matriz::i18n::t("merge.janela.lote_outro_dica"));
    btnUsarOutroSel_.onClick = [this] { aplicarSelecionados(2); };
    btnUsarEscolhaSel_.setButtonText(matriz::i18n::t("merge.janela.lote_escolha"));
    btnUsarEscolhaSel_.onClick = [this] { aplicarSelecionados(3); };
    btnUsarValor_.setButtonText(matriz::i18n::t("merge.janela.lote_valor"));
    btnUsarValor_.setTooltip(matriz::i18n::t("merge.janela.lote_valor_dica"));
    btnUsarValor_.onClick = [this] { abrirMenuUsarValor(); };
    for (auto* b : {&btnUsarOutroSel_, &btnUsarEscolhaSel_, &btnUsarValor_}) {
        b->setColour(juce::TextButton::buttonColourId, kVerde);
        b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }
    for (auto* b : {&btnManterSel_, &btnUsarOutroSel_, &btnUsarEscolhaSel_, &btnUsarValor_}) addAndMakeVisible(*b);

    viewport_ = std::make_unique<ViewportLista>(*this);
    lista_ = std::make_unique<ListaComponent>(*this);
    viewport_->setViewedComponent(lista_.get(), false);
    viewport_->setScrollBarsShown(true, false);
    addChildComponent(*viewport_);

    recarregar();
}

RevisaoConflitosMergeComponent::~RevisaoConflitosMergeComponent() {
    poolMiniaturas_.removeAllJobs(true, 2000);
    viewport_->setViewedComponent(nullptr, false);
}

void RevisaoConflitosMergeComponent::abrir(ProjetoAberto& projeto, std::set<std::string> escopo) {
    auto conteudo = std::make_unique<RevisaoConflitosMergeComponent>(projeto, std::move(escopo));
    const auto tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto area = tela != nullptr ? tela->userArea : juce::Rectangle<int>(0, 0, 1440, 900);
    conteudo->setSize(juce::jmin(1320, area.getWidth() * 9 / 10), juce::jmin(860, area.getHeight() * 85 / 100));
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(conteudo.release());
    opt.dialogTitle = matriz::i18n::t("merge.janela_titulo");
    opt.escapeKeyTriggersCloseButton = true;
    opt.resizable = true;
    moldura::lancar(opt);
}

void RevisaoConflitosMergeComponent::recarregar() {
    if (!vivo_.lock()) return;
    carregando_ = true;
    atualizarBotoes();
    resized();
    juce::Component::SafePointer<RevisaoConflitosMergeComponent> seguro(this);
    projeto_.carregarConflitosMergeEmSegundoPlano(escopo_, [seguro](std::vector<mg::GrupoConflito> grupos,
                                                                   std::map<std::string, juce::String> miniaturas) {
        if (seguro) seguro->aoCarregar(std::move(grupos), std::move(miniaturas));
    });
}

void RevisaoConflitosMergeComponent::aoCarregar(std::vector<mg::GrupoConflito> grupos,
                                                std::map<std::string, juce::String> miniaturas) {
    // Recarregar mantém a seleção e a escolha de quem continua pendente.
    std::map<std::string, std::pair<bool, int>> anteriores;
    for (const auto& e : entradas_)
        if (!e.grupo.historicoIds.empty()) anteriores[e.grupo.historicoIds.front()] = {e.selecionado, e.escolha};
    entradas_.clear();
    entradas_.reserve(grupos.size());
    for (auto& g : grupos) {
        Entrada e;
        e.categoria = categoriaDoItem(g.tipoMidia, g.caminho);
        if (!g.historicoIds.empty())
            if (auto it = anteriores.find(g.historicoIds.front()); it != anteriores.end()) {
                e.selecionado = it->second.first;
                e.escolha = juce::jlimit(0, static_cast<int>(g.alternativas.size()), it->second.second);
            }
        e.grupo = std::move(g);
        entradas_.push_back(std::move(e));
    }
    caminhosMiniatura_ = std::move(miniaturas);
    carregando_ = false;
    reconstruirFiltros();
    aplicarFiltros();
}

void RevisaoConflitosMergeComponent::reconstruirFiltros() {
    auto selecionado = [](const juce::ComboBox& cb, const std::vector<std::string>& opcoes) -> std::optional<std::string> {
        const int id = cb.getSelectedId();
        if (id >= 2 && id - 2 < static_cast<int>(opcoes.size())) return opcoes[static_cast<size_t>(id - 2)];
        return std::nullopt;
    };
    const auto campoAntes = selecionado(cbCampo_, opcoesCampo_);
    const auto pastaAntes = selecionado(cbPasta_, opcoesPasta_);

    std::map<std::string, int> porCampo, porPasta;
    for (const auto& e : entradas_) {
        ++porCampo[e.grupo.campo];
        ++porPasta[e.grupo.pasta];
    }
    auto preencher = [](juce::ComboBox& cb, std::vector<std::string>& opcoes, const juce::String& todos,
                        const std::map<std::string, int>& contagem, const std::optional<std::string>& antes,
                        const std::function<juce::String(const std::string&)>& rotulo) {
        cb.clear(juce::dontSendNotification);
        opcoes.clear();
        cb.addItem(todos, 1);
        int idAntes = 1;
        for (const auto& [valor, n] : contagem) {
            const int id = static_cast<int>(opcoes.size()) + 2;
            opcoes.push_back(valor);
            cb.addItem(rotulo(valor) + "  (" + juce::String(n) + ")", id);
            if (antes && *antes == valor) idAntes = id;
        }
        cb.setSelectedId(idAntes, juce::dontSendNotification);
    };
    preencher(cbCampo_, opcoesCampo_,
              matriz::i18n::t("merge.janela.todos_campos").replace("{n}", juce::String(static_cast<int>(entradas_.size()))),
              porCampo, campoAntes, [](const std::string& c) { return ConflitosMergeDialog::rotuloDoCampo(c); });
    preencher(cbPasta_, opcoesPasta_, matriz::i18n::t("merge.janela.todas_pastas"), porPasta, pastaAntes,
              [](const std::string& p) {
                  return p.empty() ? matriz::i18n::t("merge.janela.sem_pasta") : juce::String::fromUTF8(p.c_str());
              });
}

void RevisaoConflitosMergeComponent::aplicarFiltros() {
    const int idCampo = cbCampo_.getSelectedId(), idPasta = cbPasta_.getSelectedId();
    const std::string* campo = idCampo >= 2 && idCampo - 2 < static_cast<int>(opcoesCampo_.size())
                                   ? &opcoesCampo_[static_cast<size_t>(idCampo - 2)] : nullptr;
    const std::string* pasta = idPasta >= 2 && idPasta - 2 < static_cast<int>(opcoesPasta_.size())
                                   ? &opcoesPasta_[static_cast<size_t>(idPasta - 2)] : nullptr;
    const int midia = cbMidia_.getSelectedId(), valores = cbValores_.getSelectedId(), origem = cbOrigem_.getSelectedId();

    visiveis_.clear();
    for (size_t i = 0; i < entradas_.size(); ++i) {
        const auto& e = entradas_[i];
        if (campo && e.grupo.campo != *campo) continue;
        if (pasta && e.grupo.pasta != *pasta) continue;
        if (midia > 1 && e.categoria != midia) continue;
        const int n = numValores(e.grupo);
        if ((valores == 2 && n != 2) || (valores == 3 && n < 3)) continue;
        if (origem > 1) {
            const bool algumPacote = std::any_of(e.grupo.alternativas.begin(), e.grupo.alternativas.end(),
                                                 [](const auto& a) { return ehDePacote(a.origem); });
            const bool algumaDuplicata = std::any_of(e.grupo.alternativas.begin(), e.grupo.alternativas.end(),
                                                     [](const auto& a) { return !ehDePacote(a.origem); });
            if ((origem == 2 && !algumaDuplicata) || (origem == 3 && !algumPacote)) continue;
        }
        visiveis_.push_back(i);
    }
    resized();
    lista_->reconstruir(viewport_->getWidth() - viewport_->getScrollBarThickness());
    viewport_->setViewPosition(0, 0);
    lista_->mostrar(viewport_->getViewArea());
    pedirMiniaturas(viewport_->getViewArea());
    atualizarBotoes();
    repaint();
}

std::vector<size_t> RevisaoConflitosMergeComponent::selecionadosVisiveis() const {
    std::vector<size_t> out;
    for (auto idx : visiveis_)
        if (entradas_[idx].selecionado) out.push_back(idx);
    return out;
}

void RevisaoConflitosMergeComponent::atualizarBotoes() {
    std::set<std::string> arquivos;
    for (const auto& e : entradas_) arquivos.insert(e.grupo.itemId);
    lblStatus_.setText(carregando_ ? matriz::i18n::t("merge.janela.carregando")
                                   : matriz::i18n::t("merge.janela.status")
                                         .replace("{n}", juce::String(static_cast<int>(entradas_.size())))
                                         .replace("{m}", juce::String(static_cast<int>(arquivos.size()))),
                       juce::dontSendNotification);
    const int sel = static_cast<int>(selecionadosVisiveis().size());
    lblSelecionados_.setText(matriz::i18n::t("merge.janela.selecionados").replace("{n}", juce::String(sel)), juce::dontSendNotification);
    const bool livre = !carregando_ && !ocupado_;
    btnRecarregar_.setEnabled(livre);
    btnSelecionarTodos_.setEnabled(livre && !visiveis_.empty());
    btnSelecionarNenhum_.setEnabled(livre && sel > 0);
    for (auto* b : {&btnManterSel_, &btnUsarOutroSel_, &btnUsarEscolhaSel_, &btnUsarValor_}) b->setEnabled(livre && sel > 0);
    if (lista_) lista_->sincronizarCards();
}

void RevisaoConflitosMergeComponent::pedirMiniaturas(juce::Rectangle<int> areaVisivel) {
    if (!lista_) return;
    juce::Component::SafePointer<RevisaoConflitosMergeComponent> seguro(this);
    for (auto idx : lista_->visiveisNaArea(areaVisivel.expanded(0, areaVisivel.getHeight()))) {
        const auto& itemId = entradas_[idx].grupo.itemId;
        if (!miniaturasPedidas_.insert(itemId).second) continue;
        auto it = caminhosMiniatura_.find(itemId);
        if (it == caminhosMiniatura_.end()) continue;
        const juce::String caminho = it->second;
        poolMiniaturas_.addJob([seguro, itemId, caminho] {
            juce::Image img;
            const juce::File f(caminho);
            if (f.existsAsFile()) img = juce::ImageFileFormat::loadFrom(f);
            if (img.isValid() && (img.getWidth() > 320 || img.getHeight() > 320)) {
                const float escala = 320.0f / static_cast<float>(std::max(img.getWidth(), img.getHeight()));
                img = img.rescaled(juce::roundToInt(img.getWidth() * escala), juce::roundToInt(img.getHeight() * escala));
            }
            if (!img.isValid()) return;
            juce::MessageManager::callAsync([seguro, itemId, img] {
                if (!seguro) return;
                seguro->miniaturas_[itemId] = img;
                if (seguro->lista_) seguro->lista_->repaint();
            });
        });
    }
}

void RevisaoConflitosMergeComponent::aplicarSelecionados(int modo) {
    std::vector<std::pair<size_t, std::string>> escolhas;
    int pulados = 0;
    for (auto idx : selecionadosVisiveis()) {
        const auto& g = entradas_[idx].grupo;
        if (modo == 1) escolhas.push_back({idx, g.valorAtual});
        else if (modo == 3) escolhas.push_back({idx, valorDaEscolha(g, entradas_[idx].escolha)});
        else if (g.alternativas.size() == 1) escolhas.push_back({idx, g.alternativas.front().valor});
        else ++pulados;
    }
    if (pulados > 0)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, matriz::i18n::t("merge.janela_titulo"),
                                               matriz::i18n::t("merge.janela.pulados").replace("{n}", juce::String(pulados)),
                                               {}, nullptr, juce::ModalCallbackFunction::create([](int) {}));
    if (!escolhas.empty()) aplicar(std::move(escolhas), true);
}

void RevisaoConflitosMergeComponent::abrirMenuUsarValor() {
    // Valores presentes nos selecionados (atual ou alternativa), por campo, do mais frequente ao menos.
    struct Opcao { std::string campo, valor; int n = 0; };
    std::map<std::pair<std::string, std::string>, int> contagem;
    const auto selecionados = selecionadosVisiveis();
    for (auto idx : selecionados) {
        const auto& g = entradas_[idx].grupo;
        for (int k = 0; k < numValores(g); ++k) ++contagem[{g.campo, valorDaEscolha(g, k)}];
    }
    std::vector<Opcao> opcoes;
    for (const auto& [chave, n] : contagem) opcoes.push_back({chave.first, chave.second, n});
    std::stable_sort(opcoes.begin(), opcoes.end(), [](const Opcao& a, const Opcao& b) { return a.n > b.n; });
    if (opcoes.size() > 40) opcoes.resize(40);
    if (opcoes.empty()) return;

    juce::PopupMenu menu;
    menu.addSectionHeader(matriz::i18n::t("merge.janela.lote_valor_dica"));
    for (size_t i = 0; i < opcoes.size(); ++i) {
        juce::String texto = ConflitosMergeDialog::rotuloDoCampo(opcoes[i].campo) + ": " + valorLegivel(opcoes[i].campo, opcoes[i].valor);
        if (texto.length() > 90) texto = texto.substring(0, 88) + "...";
        menu.addItem(static_cast<int>(i) + 1, texto + "  (" + juce::String(opcoes[i].n) + ")");
    }
    juce::Component::SafePointer<RevisaoConflitosMergeComponent> seguro(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&btnUsarValor_),
                       [seguro, opcoes, selecionados](int r) {
                           if (!seguro || r <= 0 || r > static_cast<int>(opcoes.size())) return;
                           const auto& o = opcoes[static_cast<size_t>(r - 1)];
                           std::vector<std::pair<size_t, std::string>> escolhas;
                           for (auto idx : selecionados) {
                               if (idx >= seguro->entradas_.size()) continue;
                               const auto& g = seguro->entradas_[idx].grupo;
                               if (g.campo != o.campo) continue;
                               for (int k = 0; k < numValores(g); ++k)
                                   if (valorDaEscolha(g, k) == o.valor) { escolhas.push_back({idx, o.valor}); break; }
                           }
                           if (!escolhas.empty()) seguro->aplicar(std::move(escolhas), true);
                       });
}

void RevisaoConflitosMergeComponent::aplicar(std::vector<std::pair<size_t, std::string>> escolhas, bool perguntar) {
    if (ocupado_ || carregando_ || escolhas.empty()) return;
    juce::Component::SafePointer<RevisaoConflitosMergeComponent> seguro(this);
    auto executar = [seguro, escolhas] {
        if (!seguro || !seguro->vivo_.lock() || seguro->ocupado_) return;
        std::vector<std::pair<mg::GrupoConflito, std::string>> lote;
        std::set<std::string> resolvidos;  // chave de cada grupo = 1º id do histórico
        for (const auto& [idx, valor] : escolhas) {
            if (idx >= seguro->entradas_.size()) continue;
            const auto& g = seguro->entradas_[idx].grupo;
            lote.push_back({g, valor});
            if (!g.historicoIds.empty()) resolvidos.insert(g.historicoIds.front());
        }
        seguro->ocupado_ = true;
        seguro->atualizarBotoes();
        seguro->projeto_.resolverConflitosMergeEmSegundoPlano(std::move(lote), [seguro, resolvidos](bool ok) {
            if (!seguro) return;
            seguro->ocupado_ = false;
            if (!ok) {
                seguro->atualizarBotoes();
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("duplicatas.falha_titulo"),
                                                       matriz::i18n::t("duplicatas.falha_corpo"), {}, nullptr,
                                                       juce::ModalCallbackFunction::create([](int) {}));
                return;
            }
            // Sai da lista só o que foi resolvido, sem reler o banco.
            auto& es = seguro->entradas_;
            es.erase(std::remove_if(es.begin(), es.end(), [&](const Entrada& e) {
                         return !e.grupo.historicoIds.empty() && resolvidos.count(e.grupo.historicoIds.front());
                     }), es.end());
            const int y = seguro->viewport_->getViewPositionY();
            seguro->reconstruirFiltros();
            seguro->aplicarFiltros();
            seguro->viewport_->setViewPosition(0, y);
        });
    };
    if (!perguntar || escolhas.size() == 1) {
        executar();
        return;
    }
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon,
        matriz::i18n::t("merge.janela.confirmar_titulo").replace("{n}", juce::String(static_cast<int>(escolhas.size()))),
        matriz::i18n::t("merge.janela.confirmar_corpo"), matriz::i18n::t("merge.confirmar"), matriz::i18n::t("dialogo.cancelar"),
        nullptr, juce::ModalCallbackFunction::create([executar](int r) {
            if (r == 1) executar();
        }));
}

void RevisaoConflitosMergeComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);
    coluna_.paint(g);

    auto direita = getLocalBounds().withTrimmedLeft(ColunaCardsLayout::kLargura).reduced(20);
    auto cardCentral = [&](const juce::String& titulo, const juce::String& dica) {
        auto card = direita.withSizeKeepingCentre(juce::jmin(440, direita.getWidth()), 120);
        g.setColour(tk.painelAlt.withAlpha(0.25f));
        g.fillRoundedRectangle(card.toFloat(), tk.raioPequeno);
        g.setColour(tk.borda.withAlpha(0.7f));
        g.drawRoundedRectangle(card.toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);
        auto interno = card.reduced(20, 16);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(titulo, interno.removeFromTop(24), juce::Justification::centred, true);
        interno.removeFromTop(6);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawFittedText(dica, interno, juce::Justification::centredTop, 3);
    };

    if (carregando_) {
        cardCentral(matriz::i18n::t("merge.janela.carregando"), {});
        return;
    }
    if (entradas_.empty()) {
        cardCentral(matriz::i18n::t("merge.janela.vazio_titulo"), matriz::i18n::t("merge.janela.vazio_dica"));
        return;
    }
    // Linha de resumo + legenda das cores dos campos (tags).
    auto resumo = direita.removeFromTop(kAlturaResumo);
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteMicro, juce::Font::bold)));
    const juce::String textoResumo = matriz::i18n::t("merge.janela.resumo")
                                         .replace("{v}", juce::String(static_cast<int>(visiveis_.size())))
                                         .replace("{n}", juce::String(static_cast<int>(entradas_.size())));
    g.drawText(textoResumo, resumo, juce::Justification::centredLeft, true);
    const std::pair<const char*, const char*> legenda[] = {{"ano", "merge.janela.tag_data"},
                                                           {"geo", "merge.janela.tag_geo"},
                                                           {"collection_type", "merge.janela.tag_classificacao"},
                                                           {"dc_title", "merge.janela.tag_texto"},
                                                           {"outro", "merge.janela.tag_outros"}};
    const juce::Font fonteTag(juce::FontOptions(tk.tamanhoFonteMicro, juce::Font::bold));
    g.setFont(fonteTag);
    int x = resumo.getRight();
    for (int i = static_cast<int>(std::size(legenda)) - 1; i >= 0; --i) {
        const juce::String t = matriz::i18n::t(legenda[i].second);
        const int w = juce::GlyphArrangement::getStringWidthInt(fonteTag, t) + 16;
        x -= w;
        const juce::Rectangle<int> tag(x, resumo.getCentreY() - 8, w, 16);
        g.setColour(corDoCampo(legenda[i].first));
        g.fillRoundedRectangle(tag.toFloat(), 8.0f);
        g.setColour(juce::Colours::black);
        g.drawText(t, tag, juce::Justification::centred);
        x -= 6;
    }
    if (visiveis_.empty()) cardCentral(matriz::i18n::t("merge.janela.filtro_vazio"), {});
}

void RevisaoConflitosMergeComponent::resized() {
    auto area = getLocalBounds();
    coluna_.comecar(area.removeFromLeft(ColunaCardsLayout::kLargura));
    auto& corpo = coluna_.corpo();

    // CONFLITOS — contagem e RECARREGAR.
    coluna_.iniciarCard(matriz::i18n::t("merge.janela.card_conflitos"));
    lblStatus_.setBounds(corpo.removeFromTop(36));
    corpo.removeFromTop(4);
    btnRecarregar_.setBounds(corpo.removeFromTop(28));
    coluna_.finalizarCard();

    // FILTROS — rótulo pequeno acima de cada combo (mesmo padrão da aba Duplicates).
    coluna_.iniciarCard(matriz::i18n::t("duplicatas.card_filtros"));
    auto linhaRotulo = [&](juce::Label& rotulo, juce::Component& controle) {
        rotulo.setBounds(corpo.removeFromTop(14));
        controle.setBounds(corpo.removeFromTop(26));
        corpo.removeFromTop(6);
    };
    linhaRotulo(lblCampo_, cbCampo_);
    linhaRotulo(lblMidia_, cbMidia_);
    linhaRotulo(lblPasta_, cbPasta_);
    linhaRotulo(lblValores_, cbValores_);
    linhaRotulo(lblOrigem_, cbOrigem_);
    coluna_.finalizarCard();

    // SELEÇÃO.
    coluna_.iniciarCard(matriz::i18n::t("merge.janela.card_selecao"));
    lblSelecionados_.setBounds(corpo.removeFromTop(18));
    corpo.removeFromTop(4);
    for (auto* b : {&btnSelecionarTodos_, &btnSelecionarNenhum_}) {
        b->setBounds(corpo.removeFromTop(28));
        corpo.removeFromTop(4);
    }
    coluna_.finalizarCard();

    // AÇÕES EM LOTE — valem para os selecionados que estão visíveis.
    coluna_.iniciarCard(matriz::i18n::t("merge.janela.card_lote"));
    for (auto* b : {&btnUsarValor_, &btnUsarOutroSel_, &btnUsarEscolhaSel_, &btnManterSel_}) {
        b->setBounds(corpo.removeFromTop(28));
        corpo.removeFromTop(4);
    }
    coluna_.finalizarCard();

    auto direita = area.reduced(20);
    const bool comLista = !carregando_ && !visiveis_.empty();
    viewport_->setVisible(comLista);
    if (comLista) {
        direita.removeFromTop(kAlturaResumo);
        const int larguraAntes = viewport_->getWidth();
        viewport_->setBounds(direita);
        const int largura = viewport_->getWidth() - viewport_->getScrollBarThickness();
        if (larguraAntes != viewport_->getWidth() && lista_) {
            const int y = viewport_->getViewPositionY();
            lista_->reconstruir(largura);
            viewport_->setViewPosition(0, y);
            lista_->mostrar(viewport_->getViewArea());
        }
    }
    repaint();
}

}  // namespace matriz::ui
