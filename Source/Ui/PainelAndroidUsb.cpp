#include "PainelAndroidUsb.h"

#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Model/NomesSeguros.h"
#include "GeoAcoes.h"
#include "LogosTipoArquivo.h"
#include "Tokens.h"
#include "VideoPlayerComponent.h"

#include <thread>

namespace matriz::ui {

using matriz::i18n::t;
using namespace matriz::celular;
using matriz::ingest::CategoriaMidia;

namespace {
constexpr std::uint32_t kRaiz = 0xffffffffu;  // LIBMTP_FILES_AND_FOLDERS_ROOT
constexpr int kLarguraEsquerda = 230;
constexpr int kLarguraInfo = 270;
constexpr int kAlturaNome = 22;
constexpr int kGap = 10;
constexpr int kMaxMiniaturas = 2500;

juce::String bytesLegiveis(std::uint64_t b) { return juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(b)); }
juce::String sep() { return juce::String::fromUTF8("  \xc2\xb7  "); }

CategoriaMidia categoriaDe(const EntradaAndroid& e) {
    return matriz::ingest::categoriaPorExtensao(e.nome.fromLastOccurrenceOf(".", false, false).toLowerCase());
}
bool temMiniaturaProvavel(const EntradaAndroid& e) {
    const auto c = categoriaDe(e);
    return c == CategoriaMidia::Imagem || c == CategoriaMidia::Video || c == CategoriaMidia::Arte;
}
} // namespace

// ── Mosaico ─────────────────────────────────────────────────────────────────────────────────────

class PainelAndroidUsb::Grade : public juce::Component {
public:
    explicit Grade(PainelAndroidUsb& p) : p_(p) { setWantsKeyboardFocus(false); }

    int colunas() const { return juce::jmax(1, (getWidth() - kGap) / (p_.ladoTile_ + kGap)); }
    int alturaLinha() const { return p_.ladoTile_ + kAlturaNome + kGap; }
    int alturaTotal() const {
        const int n = static_cast<int>(p_.visiveis_.size());
        return kGap + ((n + colunas() - 1) / colunas()) * alturaLinha();
    }
    juce::Rectangle<int> tile(int i) const {
        const int c = colunas();
        return {kGap + (i % c) * (p_.ladoTile_ + kGap), kGap + (i / c) * alturaLinha(), p_.ladoTile_, p_.ladoTile_ + kAlturaNome};
    }
    int indiceEm(juce::Point<int> pt) const {
        const int c = colunas(), h = juce::jmax(1, alturaLinha());
        const int linha = (pt.y - kGap) / h, col = (pt.x - kGap) / (p_.ladoTile_ + kGap);
        if (pt.y < kGap || pt.x < kGap || col >= c) return -1;
        const int i = linha * c + col;
        if (i < 0 || i >= static_cast<int>(p_.visiveis_.size())) return -1;
        return tile(i).contains(pt) ? i : -1;
    }
    // Faixa de índices dentro da área visível do viewport (mais uma linha de folga para cada lado).
    std::pair<int, int> faixaVisivel(juce::Rectangle<int> area) const {
        const int c = colunas(), h = juce::jmax(1, alturaLinha());
        const int l0 = juce::jmax(0, area.getY() / h - 1), l1 = area.getBottom() / h + 1;
        return {l0 * c, juce::jmin(static_cast<int>(p_.visiveis_.size()), (l1 + 1) * c)};
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        const auto [de, ate] = faixaVisivel(g.getClipBounds());
        for (int i = de; i < ate; ++i) desenharTile(g, tk, i, tile(i));
    }

    void desenharTile(juce::Graphics& g, const Tema& tk, int i, juce::Rectangle<int> r) {
        const auto* e = p_.itemVisivel(i);
        if (e == nullptr) return;
        const bool sel = p_.selecao_.count(i) > 0;
        const bool marcado = p_.estaMarcado(*e);
        auto img = r.removeFromTop(p_.ladoTile_).toFloat();
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(img, 10.0f);

        auto it = p_.miniaturas_.find(e->id);
        if (it != p_.miniaturas_.end() && it->second.isValid()) {
            juce::Graphics::ScopedSaveState salva(g);
            juce::Path recorte;
            recorte.addRoundedRectangle(img, 10.0f);
            g.reduceClipRegion(recorte);
            g.drawImage(it->second, img, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        } else {
            const auto ext = e->nome.fromLastOccurrenceOf(".", false, false).toLowerCase();
            auto& logo = p_.logos_[ext];
            if (!logo.isValid()) logo = carregarLogoDeTipo(matriz::ingest::obterLogoParaExtensao(ext));
            if (logo.isValid()) g.drawImage(logo, img.withSizeKeepingCentre(img.getWidth() * 0.42f, img.getHeight() * 0.42f),
                                            juce::RectanglePlacement::centred);
            else desenharIcone(g, IconeUi::Arquivo, img.withSizeKeepingCentre(36, 36), tk.textoSecundario);
        }
        if (categoriaDe(*e) == CategoriaMidia::Video) {  // ▶ no canto
            auto b = img.withTrimmedTop(img.getHeight() - 24).withTrimmedLeft(img.getWidth() - 30).reduced(4);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRoundedRectangle(b, 4.0f);
            juce::Path tri;
            tri.addTriangle(b.getCentreX() - 3.5f, b.getCentreY() - 5, b.getCentreX() - 3.5f, b.getCentreY() + 5, b.getCentreX() + 5, b.getCentreY());
            g.setColour(juce::Colours::white);
            g.fillPath(tri);
        }
        if (p_.jaNoProjeto(*e)) {
            auto b = juce::Rectangle<float>(img.getX() + 6, img.getBottom() - 22, 78, 16);
            g.setColour(tk.estadoQcOk);
            g.fillRoundedRectangle(b, 8.0f);
            g.setColour(juce::Colours::white);
            g.setFont(fonteUi(tk.tamanhoFonteMicro - 1.5f, juce::Font::bold));
            g.drawText(t("cel.usb.noProjeto"), b, juce::Justification::centred, true);
        }
        if (marcado) {  // selo "I", no desenho dos selos H/K/P/W do Matriz: quadrado preto com a letra branca
            auto b = juce::Rectangle<float>(img.getX() + 6, img.getY() + 6, 20, 20);
            g.setColour(juce::Colours::black);
            g.fillRoundedRectangle(b, 3.0f);
            g.setColour(juce::Colours::white);
            g.setFont(fonteUi(12.0f, juce::Font::bold));
            g.drawText("I", b, juce::Justification::centred);
        }
        if (sel) {
            g.setColour(tk.acento);
            g.drawRoundedRectangle(img.reduced(1.0f), 10.0f, 3.0f);
        }
        g.setColour(sel ? tk.textoPrimario : tk.textoSecundario);
        g.setFont(fonteUi(tk.tamanhoFontePequena, sel ? juce::Font::bold : juce::Font::plain));
        g.drawText(e->nome, r.withTrimmedTop(3), juce::Justification::centredTop, true);
    }

    void mouseDown(const juce::MouseEvent& ev) override {
        const int i = indiceEm(ev.getPosition());
        if (i < 0) { p_.selecao_.clear(); p_.cursor_ = -1; p_.atualizarTudo(); return; }
        // clique no canto do selo = marca/desmarca só aquele
        const auto r = tile(i);
        if (juce::Rectangle<int>(r.getX(), r.getY(), 30, 30).contains(ev.getPosition()) && !ev.mods.isAnyModifierKeyDown()) {
            p_.selecionar(i, false, false);
            p_.alternarMarcaDaSelecao();
            return;
        }
        p_.selecionar(i, ev.mods.isCommandDown(), ev.mods.isShiftDown());
    }
    void mouseDoubleClick(const juce::MouseEvent& ev) override {
        const int i = indiceEm(ev.getPosition());
        if (i >= 0) p_.abrirPreview(i);
    }
    void mouseWheelMove(const juce::MouseEvent& ev, const juce::MouseWheelDetails& w) override {
        if (ev.mods.isCommandDown()) {  // ⌘ + rolagem = tamanho do mosaico
            p_.sliderTamanho_.setValue(p_.sliderTamanho_.getValue() + (w.deltaY > 0 ? 12 : -12));
            return;
        }
        Component::mouseWheelMove(ev, w);
    }

private:
    PainelAndroidUsb& p_;
};

namespace {
class ViewportMosaico : public juce::Viewport {
public:
    std::function<void()> aoRolar;
    void visibleAreaChanged(const juce::Rectangle<int>&) override { if (aoRolar) aoRolar(); }
};
} // namespace

// ── Preview ─────────────────────────────────────────────────────────────────────────────────────

class PainelAndroidUsb::Preview : public juce::Component {
public:
    explicit Preview(PainelAndroidUsb& p) : p_(p) { setWantsKeyboardFocus(false); }
    ~Preview() override { if (video_) video_->parar(); }

    void mostrar(int indice) {
        indice_ = indice;
        imagem_ = {};
        if (video_) { video_->parar(); video_.reset(); }
        const auto* e = p_.itemVisivel(indice);
        if (e == nullptr) return;
        nome_ = e->nome;
        mensagem_ = t("cel.usb.preview.baixando");
        repaint();
        const auto entrada = *e;
        juce::Component::SafePointer<Preview> seguro(this);
        p_.baixarParaCache(entrada, [seguro, entrada](juce::File f) {
            if (seguro == nullptr || seguro->indice_ < 0) return;
            const auto* atual = seguro->p_.itemVisivel(seguro->indice_);
            if (atual == nullptr || atual->id != entrada.id) return;  // já passou para outro
            seguro->exibirArquivo(f, categoriaDe(entrada));
        });
    }

    void exibirArquivo(const juce::File& f, CategoriaMidia cat) {
        if (!f.existsAsFile()) { mensagem_ = t("cel.usb.preview.falhou"); repaint(); return; }
        if (cat == CategoriaMidia::Imagem || cat == CategoriaMidia::Arte) {
            // Decodifica fora da message thread (foto de 12 MP leva ~100 ms).
            juce::Component::SafePointer<Preview> seguro(this);
            const int indice = indice_;
            std::thread([seguro, f, indice] {
                auto img = juce::ImageFileFormat::loadFrom(f);
                juce::MessageManager::callAsync([seguro, img, indice] {
                    if (seguro == nullptr || seguro->indice_ != indice) return;
                    seguro->imagem_ = img;
                    seguro->mensagem_ = img.isValid() ? juce::String() : t("cel.usb.preview.semPreview");
                    seguro->repaint();
                });
            }).detach();
            return;
        }
        if (cat == CategoriaMidia::Video || cat == CategoriaMidia::Audio) {
            video_ = std::make_unique<VideoPlayerComponent>();
            addAndMakeVisible(*video_);
            resized();
            if (video_->carregar(f)) { video_->tocar(); mensagem_.clear(); }
            else { video_.reset(); mensagem_ = t("cel.usb.preview.semPreview"); }
            repaint();
            return;
        }
        mensagem_ = t("cel.usb.preview.semPreview");
        repaint();
    }

    int indice() const { return indice_; }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(juce::Colours::black.withAlpha(0.94f));
        auto r = getLocalBounds().reduced(16);
        auto topo = r.removeFromTop(28);
        g.setColour(juce::Colours::white);
        g.setFont(fonteUi(tk.fonteCorpoGrande, juce::Font::bold));
        const auto* e = p_.itemVisivel(indice_);
        const juce::String marca = e != nullptr && p_.estaMarcado(*e) ? juce::String("[I]  ") : juce::String();
        g.drawText(marca + nome_ + sep() + juce::String(indice_ + 1) + " / " + juce::String(static_cast<int>(p_.visiveis_.size())), topo,
                   juce::Justification::centredLeft, true);
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(fonteUi(tk.tamanhoFontePequena));
        g.drawText(t("cel.usb.preview.dica"), topo, juce::Justification::centredRight, true);
        const auto area = r.reduced(0, 8);
        if (imagem_.isValid()) g.drawImage(imagem_, area.toFloat(), juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        if (mensagem_.isNotEmpty()) {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.setFont(fonteUi(tk.fonteCorpoGrande));
            g.drawText(mensagem_, area, juce::Justification::centred, true);
        }
    }
    void resized() override {
        if (video_) video_->setBounds(getLocalBounds().reduced(16).withTrimmedTop(28).reduced(0, 8));
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { p_.fecharPreview(); }

private:
    PainelAndroidUsb& p_;
    int indice_ = -1;
    juce::String nome_, mensagem_;
    juce::Image imagem_;
    std::unique_ptr<VideoPlayerComponent> video_;
};

// ── Painel ──────────────────────────────────────────────────────────────────────────────────────

PainelAndroidUsb::PainelAndroidUsb(std::function<ConjuntoExistentes()> existentes, std::shared_ptr<RespostaReceberCelular> resposta,
                                   bool comVoltar)
    : sessao_(std::make_unique<SessaoAndroidUsb>()), resposta_(std::move(resposta)) {
    setComponentID("cel.usb.painel");
    const auto& tk = tema();

    const auto estilizar = [&](juce::Label& l, float tam, int estilo, juce::Colour cor) {
        l.setFont(fonteUi(tam, estilo));
        l.setColour(juce::Label::textColourId, cor);
        l.setMinimumHorizontalScale(0.7f);
        l.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(l);
    };
    estilizar(lblAparelho_, tk.fonteCorpoGrande + 2.0f, juce::Font::bold, tk.textoPrimario);
    estilizar(lblCaminho_, tk.fonteCorpoGrande, juce::Font::plain, tk.textoSecundario);
    estilizar(lblStatus_, tk.fonteCorpoGrande, juce::Font::plain, tk.textoSecundario);
    estilizar(lblRodape_, tk.fonteCorpoGrande, juce::Font::bold, tk.textoPrimario);
    estilizar(lblPastas_, tk.tamanhoFontePequena, juce::Font::bold, tk.textoTerciario);
    lblPastas_.setText(t("cel.usb.pastas"), juce::dontSendNotification);
    lblStatus_.setJustificationType(juce::Justification::centred);

    comboMemoria_.onChange = [this] { caminho_.clear(); recarregarPasta(); resolverAtalhos(); };
    comboMemoria_.setWantsKeyboardFocus(false);
    addChildComponent(comboMemoria_);
    comboAno_.onChange = [this] { anoFiltro_ = comboAno_.getSelectedId() > 1 ? comboAno_.getText().getIntValue() : 0; aplicarFiltro(); };
    comboAno_.setWantsKeyboardFocus(false);
    addAndMakeVisible(comboAno_);

    listaPastas_.setRowHeight(28);
    listaPastas_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    listaPastas_.setOutlineThickness(0);
    listaPastas_.setWantsKeyboardFocus(false);
    addAndMakeVisible(listaPastas_);

    sliderTamanho_.setSliderStyle(juce::Slider::LinearHorizontal);
    sliderTamanho_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sliderTamanho_.setRange(90, 340, 1);
    sliderTamanho_.setValue(ladoTile_, juce::dontSendNotification);
    sliderTamanho_.setWantsKeyboardFocus(false);
    sliderTamanho_.setTooltip(t("cel.usb.tamanho"));
    sliderTamanho_.onValueChange = [this] {
        ladoTile_ = static_cast<int>(sliderTamanho_.getValue());
        resized();
        grade_->repaint();
        pedirMiniaturasVisiveis();
    };
    addAndMakeVisible(sliderTamanho_);

    grade_ = std::make_unique<Grade>(*this);
    auto vp = std::make_unique<ViewportMosaico>();
    vp->aoRolar = [this] { pedirMiniaturasVisiveis(); };
    viewport_ = std::move(vp);
    viewport_->setViewedComponent(grade_.get(), false);
    viewport_->setScrollBarsShown(true, false);
    viewport_->setWantsKeyboardFocus(false);
    addAndMakeVisible(*viewport_);
    addAndMakeVisible(barra_);
    barra_.setVisible(false);

    const std::pair<Filtro, const char*> filtros[] = {{Filtro::Todos, "cel.usb.f.todos"}, {Filtro::Imagem, "cel.usb.f.imagem"},
                                                      {Filtro::Video, "cel.usb.f.video"},  {Filtro::Audio, "cel.usb.f.audio"},
                                                      {Filtro::Documento, "cel.usb.f.doc"}, {Filtro::Marcados, "cel.usb.f.marcados"}};
    for (const auto& [f, chave] : filtros) {
        auto c = std::make_unique<ChipFiltro>(t(chave), 0);
        c->setWantsKeyboardFocus(false);
        const Filtro filtro = f;
        c->onClick = [this, filtro] { filtro_ = filtro; aplicarFiltro(); };
        addAndMakeVisible(*c);
        chips_.push_back(std::move(c));
    }

    const auto botao = [this](std::unique_ptr<BotaoPilula>& b, const juce::String& texto, BotaoPilula::Estilo estilo) {
        b = std::make_unique<BotaoPilula>(texto, estilo);
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(*b);
    };
    botao(btnSubir_, juce::String::fromUTF8("\xe2\x97\x80"), BotaoPilula::Estilo::Secundario);
    btnSubir_->setTooltip(t("cel.usb.subir"));
    btnSubir_->onClick = [this] { subir(); };
    botao(btnTentar_, t("cel.usb.tentar"), BotaoPilula::Estilo::Primario);
    btnTentar_->onClick = [this] { conectar(); };
    botao(btnMarcar_, t("cel.usb.marcar"), BotaoPilula::Estilo::Secundario);
    btnMarcar_->setTooltip(t("cel.usb.marcar.dica"));
    btnMarcar_->onClick = [this] { alternarMarcaDaSelecao(); };
    botao(btnImportar_, t("cel.usb.importar"), BotaoPilula::Estilo::Primario);
    btnImportar_->onClick = [this] { importar(); };
    botao(btnFechar_, t("light.ui.common.cancel"), BotaoPilula::Estilo::Secundario);
    btnFechar_->onClick = [this] {
        if (estado_ == Estado::Copiando) { sessao_->cancelar(); return; }
        terminar(false, 0);
    };
    botao(btnMapa_, t("cel.usb.abrirMapa"), BotaoPilula::Estilo::Secundario);
    btnMapa_->onClick = [this] {
        const auto* e = itemVisivel(cursor_);
        if (e == nullptr) return;
        auto it = metadados_.find(e->id);
        if (it == metadados_.end() || !it->second.lat || !it->second.lon) return;
        geoacoes::Local l;
        l.coords = juce::String(*it->second.lat, 6) + ", " + juce::String(*it->second.lon, 6);
        geoacoes::abrirNoGoogleMaps(l);
    };
    if (comVoltar) {
        botao(btnVoltar_, t("imp.voltar"), BotaoPilula::Estilo::Secundario);
        btnVoltar_->onClick = [this] {
            if (estado_ == Estado::Copiando) return;
            if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(9);
        };
    }

    // Atalhos: os lugares onde o Android guarda o que interessa num PHONE DUMP (o primeiro caminho que existir).
    atalhosPedidos_ = {{"cel.usb.at.camera", {"DCIM", "Camera"}},
                       {"cel.usb.at.whatsapp", {"Android", "media", "com.whatsapp", "WhatsApp", "Media"}},
                       {"cel.usb.at.whatsapp", {"WhatsApp", "Media"}},
                       {"cel.usb.at.screenshots", {"DCIM", "Screenshots"}},
                       {"cel.usb.at.screenshots", {"Pictures", "Screenshots"}},
                       {"cel.usb.at.downloads", {"Download"}},
                       {"cel.usb.at.pictures", {"Pictures"}}};

    if (existentes) {  // "Já está no projeto": a mesma identidade (nome, tamanho) do QR code, lida fora da message thread
        juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
        std::thread([existentes, seguro] {
            auto conjunto = existentes();
            juce::MessageManager::callAsync([seguro, conjunto = std::move(conjunto)]() mutable {
                if (seguro == nullptr) return;
                seguro->existentes_ = std::move(conjunto);
                seguro->grade_->repaint();
            });
        }).detach();
    }

    conectar();
}

PainelAndroidUsb::~PainelAndroidUsb() {
    stopTimer();
    listaPastas_.setModel(nullptr);
    preview_.reset();
    sessao_->pedirMiniaturas({}, nullptr);
}

std::uint32_t PainelAndroidUsb::memoriaAtual() const {
    const int i = comboMemoria_.getSelectedItemIndex();
    if (i < 0 || i >= static_cast<int>(aparelho_.memorias.size())) return 0;
    return aparelho_.memorias[static_cast<size_t>(i)].id;
}

const EntradaAndroid* PainelAndroidUsb::itemVisivel(int i) const {
    if (i < 0 || i >= static_cast<int>(visiveis_.size())) return nullptr;
    return &arquivos_[static_cast<size_t>(visiveis_[static_cast<size_t>(i)])];
}

bool PainelAndroidUsb::jaNoProjeto(const EntradaAndroid& e) const {
    return !e.pasta && !existentes_.empty() &&
           existentes_.count(chaveDoArquivo(e.nome.toStdString(), static_cast<std::int64_t>(e.tamanho))) > 0;
}

juce::File PainelAndroidUsb::arquivoNoCache(const EntradaAndroid& e) const {
    return SessaoAndroidUsb::pastaCache(aparelho_.serial).getChildFile(juce::String(e.id))
        .getChildFile(matriz::nomes_seguros::sanitizarComponente(e.nome));
}

void PainelAndroidUsb::conectar() {
    estado_ = Estado::Conectando;
    erro_.clear();
    pastas_.clear();
    arquivos_.clear();
    visiveis_.clear();
    caminho_.clear();
    atualizarTudo();
    juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
    sessao_->conectar([seguro](bool ok, AparelhoAndroid info, juce::String erro) {
        if (seguro == nullptr) return;
        auto& s = *seguro;
        if (!ok) { s.estado_ = Estado::Erro; s.erro_ = erro; s.atualizarTudo(); return; }
        s.aparelho_ = std::move(info);
        s.comboMemoria_.clear(juce::dontSendNotification);
        for (size_t i = 0; i < s.aparelho_.memorias.size(); ++i) {
            const auto& m = s.aparelho_.memorias[i];
            s.comboMemoria_.addItem(m.descricao.isNotEmpty() ? m.descricao : t("cel.usb.memoria"), static_cast<int>(i) + 1);
        }
        s.comboMemoria_.setSelectedItemIndex(0, juce::dontSendNotification);
        s.comboMemoria_.setVisible(s.aparelho_.memorias.size() > 1);
        s.estado_ = Estado::Pronto;
        s.recarregarPasta();
        s.resolverAtalhos();
    });
}

void PainelAndroidUsb::resolverAtalhos() {
    for (auto& a : atalhos_) if (a.botao) removeChildComponent(a.botao.get());
    atalhos_.clear();
    resized();
    const auto memoria = memoriaAtual();
    juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
    for (const auto& [chave, partes] : atalhosPedidos_) {
        const juce::String rotulo = t(chave.toRawUTF8());
        sessao_->resolver(memoria, partes, [seguro, rotulo, memoria](std::vector<std::pair<std::uint32_t, juce::String>> cadeia) {
            if (seguro == nullptr || cadeia.empty() || seguro->memoriaAtual() != memoria) return;
            auto& s = *seguro;
            for (const auto& a : s.atalhos_) if (a.rotulo == rotulo) return;  // o primeiro caminho que existe vence
            Atalho a;
            a.rotulo = rotulo;
            a.cadeia = cadeia;
            a.botao = std::make_unique<BotaoPilula>(rotulo, BotaoPilula::Estilo::Secundario);
            a.botao->setWantsKeyboardFocus(false);
            a.botao->onClick = [seguro, cadeia] { if (seguro != nullptr) seguro->irPara(cadeia); };
            s.addAndMakeVisible(*a.botao);
            s.atalhos_.push_back(std::move(a));
            s.resized();
        });
    }
}

void PainelAndroidUsb::irPara(std::vector<std::pair<std::uint32_t, juce::String>> cadeia) {
    if (estado_ != Estado::Pronto) return;
    caminho_.clear();
    for (const auto& [id, nome] : cadeia) caminho_.push_back({id, nome});
    recarregarPasta();
}

void PainelAndroidUsb::recarregarPasta() {
    if (estado_ != Estado::Pronto) return;
    const std::uint32_t pasta = caminho_.empty() ? kRaiz : caminho_.back().id;
    listando_ = true;
    const int geracao = ++geracaoLista_;
    pastas_.clear();
    arquivos_.clear();
    listaPastas_.updateContent();
    aplicarFiltro();
    juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
    sessao_->listar(memoriaAtual(), pasta, [seguro, geracao](bool ok, std::vector<EntradaAndroid> itens, juce::String erro) {
        if (seguro == nullptr || geracao != seguro->geracaoLista_) return;  // o usuário já abriu outra pasta
        auto& s = *seguro;
        s.listando_ = false;
        if (!ok) { s.estado_ = Estado::Erro; s.erro_ = erro; }
        for (auto& e : itens) (e.pasta ? s.pastas_ : s.arquivos_).push_back(std::move(e));
        // Mosaico do mais novo para o mais antigo, como a galeria do celular.
        std::stable_sort(s.arquivos_.begin(), s.arquivos_.end(),
                         [](const EntradaAndroid& a, const EntradaAndroid& b) { return a.modificado > b.modificado; });
        // Anos presentes na pasta, para o filtro de data.
        std::set<int> anos;
        for (const auto& e : s.arquivos_) if (e.modificado.toMilliseconds() > 0) anos.insert(e.modificado.getYear());
        s.comboAno_.clear(juce::dontSendNotification);
        s.comboAno_.addItem(t("cel.usb.todosAnos"), 1);
        int id = 2;
        for (auto it = anos.rbegin(); it != anos.rend(); ++it) s.comboAno_.addItem(juce::String(*it), id++);
        s.comboAno_.setSelectedId(1, juce::dontSendNotification);
        s.anoFiltro_ = 0;
        s.listaPastas_.updateContent();
        s.listaPastas_.repaint();
        s.aplicarFiltro();
        s.viewport_->setViewPosition(0, 0);
    });
}

void PainelAndroidUsb::abrirPasta(std::uint32_t pasta, const juce::String& nome) {
    caminho_.push_back({pasta, nome});
    recarregarPasta();
}

void PainelAndroidUsb::subir() {
    if (caminho_.empty() || estado_ != Estado::Pronto) return;
    caminho_.pop_back();
    recarregarPasta();
}

void PainelAndroidUsb::aplicarFiltro() {
    visiveis_.clear();
    int contagem[6] = {};
    for (int i = 0; i < static_cast<int>(arquivos_.size()); ++i) {
        const auto& e = arquivos_[static_cast<size_t>(i)];
        if (anoFiltro_ != 0 && e.modificado.getYear() != anoFiltro_) continue;
        const auto c = categoriaDe(e);
        const bool img = c == CategoriaMidia::Imagem || c == CategoriaMidia::Arte;
        const bool vid = c == CategoriaMidia::Video;
        const bool aud = c == CategoriaMidia::Audio;
        const bool doc = c == CategoriaMidia::Documento || c == CategoriaMidia::Texto;
        const bool marc = estaMarcado(e);
        ++contagem[0];
        if (img) ++contagem[1];
        if (vid) ++contagem[2];
        if (aud) ++contagem[3];
        if (doc) ++contagem[4];
        if (marc) ++contagem[5];
        const bool passa = filtro_ == Filtro::Todos || (filtro_ == Filtro::Imagem && img) || (filtro_ == Filtro::Video && vid) ||
                           (filtro_ == Filtro::Audio && aud) || (filtro_ == Filtro::Documento && doc) || (filtro_ == Filtro::Marcados && marc);
        if (passa) visiveis_.push_back(i);
    }
    for (size_t i = 0; i < chips_.size(); ++i) {
        chips_[i]->definirContador(contagem[i]);
        chips_[i]->definirAtivo(static_cast<int>(i) == static_cast<int>(filtro_));
    }
    selecao_.clear();
    cursor_ = ancora_ = -1;
    atualizarTudo();
    pedirMiniaturasVisiveis();
}

void PainelAndroidUsb::pedirMiniaturasVisiveis() {
    if (grade_ == nullptr || viewport_ == nullptr || estado_ == Estado::Conectando) return;
    const auto [de, ate] = grade_->faixaVisivel(viewport_->getViewArea());
    std::vector<EntradaAndroid> faltam;
    for (int i = de; i < ate; ++i) {
        const auto* e = itemVisivel(i);
        if (e != nullptr && temMiniaturaProvavel(*e) && miniaturas_.count(e->id) == 0 && semMiniatura_.count(e->id) == 0) faltam.push_back(*e);
    }
    juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
    sessao_->pedirMiniaturas(std::move(faltam), [seguro](std::uint32_t id, juce::Image img) {
        if (seguro == nullptr) return;
        auto& s = *seguro;
        if (!img.isValid()) { s.semMiniatura_.insert(id); return; }
        const int maior = juce::jmax(img.getWidth(), img.getHeight());
        if (maior > 360)  // o Android às vezes manda a imagem inteira
            img = img.rescaled(juce::jmax(1, img.getWidth() * 360 / maior), juce::jmax(1, img.getHeight() * 360 / maior));
        if (static_cast<int>(s.miniaturas_.size()) >= kMaxMiniaturas) s.miniaturas_.erase(s.miniaturas_.begin());
        s.miniaturas_[id] = img;
        s.grade_->repaint();
        if (s.preview_ == nullptr) s.repaint(s.rcInfoImagem_);
    });
}

void PainelAndroidUsb::selecionar(int i, bool alternar, bool estender) {
    if (i < 0 || i >= static_cast<int>(visiveis_.size())) return;
    if (estender && ancora_ >= 0) {
        if (!alternar) selecao_.clear();
        for (int k = juce::jmin(ancora_, i); k <= juce::jmax(ancora_, i); ++k) selecao_.insert(k);
    } else if (alternar) {
        if (selecao_.erase(i) == 0) selecao_.insert(i);
        ancora_ = i;
    } else {
        selecao_ = {i};
        ancora_ = i;
    }
    cursor_ = i;
    const auto r = grade_->tile(i);
    const auto vista = viewport_->getViewArea();
    if (r.getY() < vista.getY()) viewport_->setViewPosition(0, juce::jmax(0, r.getY() - kGap));
    else if (r.getBottom() > vista.getBottom()) viewport_->setViewPosition(0, r.getBottom() - vista.getHeight() + kGap);
    startTimer(450);  // metadados (GEO) só depois de parar num item
    atualizarTudo();
}

void PainelAndroidUsb::moverCursor(int delta, bool estender) {
    if (visiveis_.empty()) return;
    const int alvo = juce::jlimit(0, static_cast<int>(visiveis_.size()) - 1, cursor_ < 0 ? 0 : cursor_ + delta);
    selecionar(alvo, false, estender);
}

void PainelAndroidUsb::alternarMarcaDaSelecao() {
    if (estado_ != Estado::Pronto || selecao_.empty()) return;
    // Tudo marcado na seleção -> desmarca; senão marca tudo.
    bool todos = true;
    for (int i : selecao_)
        if (const auto* e = itemVisivel(i); e != nullptr && !estaMarcado(*e)) { todos = false; break; }
    for (int i : selecao_) {
        const auto* e = itemVisivel(i);
        if (e == nullptr) continue;
        if (todos) marcados_.erase(chaveDe(*e));
        else marcados_[chaveDe(*e)] = *e;
    }
    if (filtro_ == Filtro::Marcados) { aplicarFiltro(); return; }
    int marc = 0;  // só o contador do chip MARCADOS muda
    for (const auto& e : arquivos_) if (estaMarcado(e)) ++marc;
    if (chips_.size() > 5) chips_[5]->definirContador(marc);
    atualizarTudo();
}

void PainelAndroidUsb::baixarParaCache(const EntradaAndroid& e, std::function<void(juce::File)> aoTerminar) {
    const juce::File alvo = arquivoNoCache(e);
    if (alvo.existsAsFile() && static_cast<std::uint64_t>(alvo.getSize()) == e.tamanho) { aoTerminar(alvo); return; }
    ItemCopiaAndroid item{e, juce::String(e.id) + "/" + e.nome};
    sessao_->copiar({item}, SessaoAndroidUsb::pastaCache(aparelho_.serial), nullptr,
                    [alvo, aoTerminar](ResultadoCopiaAndroid) { aoTerminar(alvo); });
}

void PainelAndroidUsb::abrirPreview(int i) {
    if (estado_ != Estado::Pronto || itemVisivel(i) == nullptr) return;
    selecionar(i, false, false);
    if (preview_ == nullptr) {
        preview_ = std::make_unique<Preview>(*this);
        addAndMakeVisible(*preview_);
        resized();
    }
    preview_->mostrar(i);
}

void PainelAndroidUsb::fecharPreview() {
    preview_.reset();
    atualizarTudo();
}

void PainelAndroidUsb::timerCallback() {
    stopTimer();
    lerMetadadosDoSelecionado();
}

void PainelAndroidUsb::lerMetadadosDoSelecionado() {
    const auto* e = itemVisivel(cursor_);
    if (e == nullptr || metadados_[e->id].lido) return;
    const auto c = categoriaDe(*e);
    // Fotos (até 60 MB) vêm para o cache só para ler EXIF/GEO; vídeo e áudio só se já estiverem lá (preview).
    const bool foto = (c == CategoriaMidia::Imagem || c == CategoriaMidia::Arte) && e->tamanho <= 60ull * 1024 * 1024;
    const juce::File noCache = arquivoNoCache(*e);
    const bool jaBaixado = noCache.existsAsFile() && static_cast<std::uint64_t>(noCache.getSize()) == e->tamanho;
    if (!foto && !jaBaixado) return;
    const auto id = e->id;
    juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
    baixarParaCache(*e, [seguro, id](juce::File f) {
        if (seguro == nullptr || !f.existsAsFile()) return;
        std::thread([seguro, id, f] {
            Metadados m;
            m.lido = true;
            try {
                const auto r = matriz::ingest::lerTecnica(f);
                m.largura = r.larguraPx;
                m.altura = r.alturaPx;
                m.duracao = r.duracaoSegundos;
                m.camera = juce::String::fromUTF8(r.exifCamera.value_or("").c_str());
                m.dataOriginal = juce::String::fromUTF8(r.exifDataOriginal.value_or("").c_str());
                m.lat = r.exifGpsLatitude;
                m.lon = r.exifGpsLongitude;
            } catch (...) {
            }
            juce::MessageManager::callAsync([seguro, id, m] {
                if (seguro == nullptr) return;
                seguro->metadados_[id] = m;
                seguro->atualizarTudo();
            });
        }).detach();
    });
}

void PainelAndroidUsb::importar() {
    if (estado_ != Estado::Pronto || marcados_.empty()) return;
    // Só o que é novo: o que o projeto já tem (nome + tamanho) não é copiado de novo.
    std::vector<ItemCopiaAndroid> novos;
    for (const auto& [k, e] : marcados_)
        if (!jaNoProjeto(e)) novos.push_back({e, e.nome});
    if (novos.empty()) { aviso_ = t("cel.usb.nadaNovo"); atualizarTudo(); return; }
    preview_.reset();
    estado_ = Estado::Copiando;
    progresso_ = 0.0;
    atualizarTudo();
    const juce::File destino = pastaRaizRecebidos().getChildFile(nomePastaSessao(juce::Time::getCurrentTime()) + " USB");
    juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
    sessao_->copiar(std::move(novos), destino,
        [seguro](ProgressoCopiaAndroid p) {
            if (seguro == nullptr || seguro->estado_ != Estado::Copiando) return;
            seguro->progresso_ = p.bytesTotal > 0 ? juce::jlimit(0.0, 1.0, static_cast<double>(p.bytesFeitos) / static_cast<double>(p.bytesTotal)) : 0.0;
            seguro->lblRodape_.setText(t("light.cel.xdey").replace("{x}", juce::String(p.feitos)).replace("{y}", juce::String(p.total)) + sep() +
                                           bytesLegiveis(p.bytesFeitos) + " / " + bytesLegiveis(p.bytesTotal) + sep() + p.arquivoAtual,
                                       juce::dontSendNotification);
        },
        [seguro](ResultadoCopiaAndroid r) {
            if (seguro == nullptr) return;
            auto& s = *seguro;
            const int recebidos = r.copiados + r.jaExistiam;
            if (r.erro.isNotEmpty() || (recebidos == 0 && !r.cancelado)) {
                s.estado_ = Estado::Erro;
                s.erro_ = r.erro.isNotEmpty() ? r.erro : t("cel.usb.err.copiar");
                s.atualizarTudo();
                return;
            }
            s.resposta_->pastaSessao = r.pasta;
            if (r.cancelado && recebidos == 0) { s.estado_ = Estado::Pronto; s.atualizarTudo(); return; }
            s.terminar(true, recebidos);  // cancelado no meio: o que já chegou entra no Intake mesmo assim
        });
}

void PainelAndroidUsb::terminar(bool comIngestao, int recebidos) {
    resposta_->concluiu = comIngestao;
    resposta_->recebidos = recebidos;
    if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(comIngestao ? 1 : 0);
}

void PainelAndroidUsb::atualizarTudo() {
    const auto& tk = tema();
    const bool pronto = estado_ == Estado::Pronto;
    const bool copiando = estado_ == Estado::Copiando;

    lblAparelho_.setText(aparelho_.nome.isNotEmpty() ? aparelho_.nome : t("cel.usb.titulo"), juce::dontSendNotification);
    juce::String trilha = comboMemoria_.getNumItems() > 0 ? comboMemoria_.getText() : juce::String();
    for (const auto& n : caminho_) trilha += " / " + n.nome;
    lblCaminho_.setText(trilha, juce::dontSendNotification);

    juce::String status;
    if (estado_ == Estado::Conectando) status = t("cel.usb.conectando");
    else if (estado_ == Estado::Erro) status = erro_;
    else if (pronto && listando_) status = t("cel.usb.lendo");
    else if (pronto && arquivos_.empty()) status = pastas_.empty() ? t("cel.usb.vazia") : t("cel.usb.soPastas");
    else if (pronto && visiveis_.empty()) status = t("cel.usb.nadaNoFiltro");
    lblStatus_.setText(status, juce::dontSendNotification);
    lblStatus_.setColour(juce::Label::textColourId, estado_ == Estado::Erro ? tk.perigo : tk.textoSecundario);
    lblStatus_.setVisible(status.isNotEmpty());

    if (!copiando) {
        std::uint64_t bytes = 0;
        for (const auto& [k, e] : marcados_) bytes += e.tamanho;
        juce::String r = t("cel.usb.marcados").replace("{n}", juce::String(static_cast<int>(marcados_.size())));
        if (bytes > 0) r += sep() + bytesLegiveis(bytes);
        if (aviso_.isNotEmpty()) { r += sep() + aviso_; aviso_.clear(); }
        else if (marcados_.empty()) r += sep() + t("cel.usb.dicaI");
        lblRodape_.setText(r, juce::dontSendNotification);
    }

    const auto* e = itemVisivel(cursor_);
    const Metadados* m = nullptr;
    if (e != nullptr)
        if (auto it = metadados_.find(e->id); it != metadados_.end() && it->second.lido) m = &it->second;
    btnMapa_->setVisible(m != nullptr && m->lat.has_value() && m->lon.has_value());

    barra_.setVisible(copiando);
    btnSubir_->setEnabled(pronto && !caminho_.empty());
    btnTentar_->setVisible(estado_ == Estado::Erro);
    btnMarcar_->setEnabled(pronto && !selecao_.empty());
    btnImportar_->setEnabled(pronto && !marcados_.empty());
    btnImportar_->setButtonText(marcados_.empty() ? t("cel.usb.importar")
                                                  : t("cel.usb.importarN").replace("{n}", juce::String(static_cast<int>(marcados_.size()))));
    btnFechar_->setButtonText(copiando ? t("cel.usb.parar") : t("light.ui.common.cancel"));
    if (btnVoltar_) btnVoltar_->setEnabled(!copiando);
    comboMemoria_.setEnabled(pronto);
    listaPastas_.setEnabled(pronto);
    resized();
    grade_->repaint();
    repaint();
}

void PainelAndroidUsb::paintListBoxItem(int linha, juce::Graphics& g, int largura, int altura, bool) {
    if (linha < 0 || linha >= static_cast<int>(pastas_.size())) return;
    const auto& tk = tema();
    desenharIcone(g, IconeUi::Pasta, juce::Rectangle<float>(6, (altura - 18) * 0.5f, 18, 18), tk.acento);
    g.setColour(tk.textoPrimario);
    g.setFont(fonteUi(tk.tamanhoFonteCorpo));
    g.drawText(pastas_[static_cast<size_t>(linha)].nome, juce::Rectangle<int>(32, 0, largura - 36, altura), juce::Justification::centredLeft, true);
}

void PainelAndroidUsb::listBoxItemClicked(int linha, const juce::MouseEvent&) {
    if (linha < 0 || linha >= static_cast<int>(pastas_.size()) || estado_ != Estado::Pronto) return;
    const auto p = pastas_[static_cast<size_t>(linha)];
    abrirPasta(p.id, p.nome);
}

bool PainelAndroidUsb::tratarTecla(const juce::KeyPress& k) {
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    const bool teclaI = (k.getTextCharacter() == 'i' || k.getTextCharacter() == 'I' || code == 'I') && !mods.isCommandDown();
    if (preview_ != nullptr) {
        if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::spaceKey) { fecharPreview(); return true; }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
            const int alvo = preview_->indice() + (code == juce::KeyPress::leftKey ? -1 : 1);
            if (itemVisivel(alvo) != nullptr) abrirPreview(alvo);
            return true;
        }
        if (teclaI) { alternarMarcaDaSelecao(); if (preview_) preview_->repaint(); return true; }
        return true;
    }
    if (k == juce::KeyPress::escapeKey) {
        if (estado_ == Estado::Copiando) sessao_->cancelar();
        else terminar(false, 0);
        return true;
    }
    if (estado_ != Estado::Pronto) return false;
    if (teclaI) { alternarMarcaDaSelecao(); return true; }
    if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) { if (cursor_ >= 0) abrirPreview(cursor_); return true; }
    if (code == 'A' && mods.isCommandDown()) {
        selecao_.clear();
        for (int i = 0; i < static_cast<int>(visiveis_.size()); ++i) selecao_.insert(i);
        if (cursor_ < 0 && !visiveis_.empty()) cursor_ = 0;
        atualizarTudo();
        return true;
    }
    if (k == juce::KeyPress::backspaceKey || (code == juce::KeyPress::upKey && mods.isCommandDown())) { subir(); return true; }
    const bool shift = mods.isShiftDown();
    if (code == juce::KeyPress::leftKey) { moverCursor(-1, shift); return true; }
    if (code == juce::KeyPress::rightKey) { moverCursor(1, shift); return true; }
    if (code == juce::KeyPress::upKey) { moverCursor(-grade_->colunas(), shift); return true; }
    if (code == juce::KeyPress::downKey) { moverCursor(grade_->colunas(), shift); return true; }
    return false;
}

void PainelAndroidUsb::paint(juce::Graphics& g) {
    const auto& tk = tema();
    desenharCartao(g, rcEsquerda_.toFloat(), tk.raioCartao * 0.6f, tk.cartao, true);
    desenharCartao(g, rcCentro_.toFloat(), tk.raioCartao * 0.6f, tk.cartao, true);
    desenharCartao(g, rcInfo_.toFloat(), tk.raioCartao * 0.6f, tk.cartao, true);

    // ícone do celular ao lado do nome do aparelho
    auto tile = juce::Rectangle<float>(36.0f, 36.0f).withPosition((float) rcEsquerda_.getX() + 12, (float) rcEsquerda_.getY() + 12);
    g.setColour(tk.tipoImagem.withAlpha(0.20f));
    g.fillRoundedRectangle(tile, 11.0f);
    desenharIcone(g, IconeUi::Celular, tile.reduced(8.0f), tk.tipoImagem);

    // Metadados do item selecionado
    const auto* e = itemVisivel(cursor_);
    if (e == nullptr || selecao_.size() > 1) {
        g.setColour(tk.textoTerciario);
        g.setFont(fonteUi(tk.tamanhoFonteCorpo));
        g.drawFittedText(selecao_.size() > 1 ? t("cel.usb.nSelecionados").replace("{n}", juce::String(static_cast<int>(selecao_.size())))
                                             : t("cel.usb.semSelecao"),
                         rcInfo_.reduced(16), juce::Justification::centred, 3);
        return;
    }
    auto r = rcInfoTexto_;
    g.setColour(tk.painelAlt);
    g.fillRoundedRectangle(rcInfoImagem_.toFloat(), 10.0f);
    auto it = miniaturas_.find(e->id);
    if (it != miniaturas_.end() && it->second.isValid())
        g.drawImage(it->second, rcInfoImagem_.toFloat().reduced(4), juce::RectanglePlacement::centred);

    const auto linha = [&](const juce::String& rotulo, const juce::String& valor) {
        if (valor.isEmpty() || r.getHeight() < 40) return;
        g.setColour(tk.textoTerciario);
        g.setFont(fonteUi(tk.tamanhoFonteMicro, juce::Font::bold));
        g.drawText(rotulo.toUpperCase(), r.removeFromTop(16), juce::Justification::centredLeft, true);
        g.setColour(tk.textoPrimario);
        g.setFont(fonteUi(tk.tamanhoFonteCorpo));
        g.drawFittedText(valor, r.removeFromTop(22), juce::Justification::centredLeft, 1);
        r.removeFromTop(6);
    };
    g.setColour(tk.textoPrimario);
    g.setFont(fonteUi(tk.fonteCorpoGrande, juce::Font::bold));
    g.drawFittedText(e->nome, r.removeFromTop(40), juce::Justification::topLeft, 2);
    r.removeFromTop(4);
    linha(t("cel.usb.m.tipo"), e->nome.fromLastOccurrenceOf(".", false, false).toUpperCase() + sep() + bytesLegiveis(e->tamanho));
    if (e->modificado.toMilliseconds() > 0) linha(t("cel.usb.m.modificado"), e->modificado.formatted("%Y-%m-%d %H:%M"));
    auto mt = metadados_.find(e->id);
    if (mt != metadados_.end() && mt->second.lido) {
        const auto& m = mt->second;
        if (m.dataOriginal.isNotEmpty()) linha(t("cel.usb.m.capturado"), m.dataOriginal.replaceCharacter('T', ' ').substring(0, 19));
        if (m.largura && m.altura) linha(t("cel.usb.m.dimensoes"), juce::String(*m.largura) + " x " + juce::String(*m.altura) + " px");
        if (m.duracao && *m.duracao > 0) {
            const int s = static_cast<int>(*m.duracao);
            linha(t("cel.usb.m.duracao"), juce::String::formatted("%d:%02d", s / 60, s % 60));
        }
        linha(t("cel.usb.m.camera"), m.camera);
        linha(t("cel.usb.m.geo"), m.lat && m.lon ? juce::String(*m.lat, 5) + ", " + juce::String(*m.lon, 5) : t("cel.usb.m.semGeo"));
    } else {
        const auto c = categoriaDe(*e);
        if (c == CategoriaMidia::Imagem || c == CategoriaMidia::Arte) linha(t("cel.usb.m.geo"), t("cel.usb.m.lendo"));
    }
    if (jaNoProjeto(*e)) linha(t("cel.usb.m.projeto"), t("cel.usb.noProjeto"));
}

void PainelAndroidUsb::resized() {
    const auto& tk = tema();
    auto b = getLocalBounds();

    auto nav = b.removeFromBottom(tk.alturaBotaoPrimario);
    b.removeFromBottom(10);
    btnImportar_->setBounds(nav.removeFromRight(juce::jmax(230, btnImportar_->larguraIdeal() + 24)));
    nav.removeFromRight(10);
    btnFechar_->setBounds(nav.removeFromRight(juce::jmax(140, btnFechar_->larguraIdeal() + 24)));
    if (btnVoltar_) {
        btnVoltar_->setBounds(nav.removeFromLeft(juce::jmax(120, btnVoltar_->larguraIdeal() + 24)));
        nav.removeFromLeft(10);
    }
    btnMarcar_->setBounds(nav.removeFromLeft(juce::jmax(150, btnMarcar_->larguraIdeal() + 24)));
    nav.removeFromLeft(14);
    if (barra_.isVisible()) {
        auto col = nav;
        barra_.setBounds(col.removeFromTop(16).reduced(0, 1));
        lblRodape_.setBounds(col);
    } else {
        lblRodape_.setBounds(nav);
    }

    rcEsquerda_ = b.removeFromLeft(kLarguraEsquerda);
    b.removeFromLeft(10);
    rcInfo_ = b.removeFromRight(kLarguraInfo);
    b.removeFromRight(10);
    rcCentro_ = b;

    {  // Esquerda: aparelho, memória, atalhos, subpastas
        auto e = rcEsquerda_.reduced(12);
        auto topo = e.removeFromTop(36);
        topo.removeFromLeft(46);
        lblAparelho_.setBounds(topo);
        e.removeFromTop(10);
        if (comboMemoria_.isVisible()) { comboMemoria_.setBounds(e.removeFromTop(30)); e.removeFromTop(8); }
        for (auto& a : atalhos_) {
            if (!a.botao) continue;
            a.botao->setBounds(e.removeFromTop(34));
            e.removeFromTop(6);
        }
        e.removeFromTop(8);
        lblPastas_.setBounds(e.removeFromTop(18));
        listaPastas_.setBounds(e);
    }

    {  // Centro: trilha, filtros, mosaico
        auto c = rcCentro_.reduced(10);
        auto trilha = c.removeFromTop(34);
        btnSubir_->setBounds(trilha.removeFromLeft(44));
        trilha.removeFromLeft(10);
        sliderTamanho_.setBounds(trilha.removeFromRight(150).reduced(0, 6));
        lblCaminho_.setBounds(trilha);
        c.removeFromTop(8);
        auto fil = c.removeFromTop(30);
        comboAno_.setBounds(fil.removeFromRight(120));
        fil.removeFromRight(8);
        for (auto& chip : chips_) {
            const int w = juce::jmin(chip->larguraIdeal(), fil.getWidth());
            chip->setVisible(w > 20);
            if (w <= 20) continue;
            chip->setBounds(fil.removeFromLeft(w));
            fil.removeFromLeft(6);
        }
        c.removeFromTop(8);
        viewport_->setBounds(c);
        if (grade_) grade_->setSize(juce::jmax(10, viewport_->getMaximumVisibleWidth()), grade_->alturaTotal());
        auto msg = c.withSizeKeepingCentre(juce::jmin(560, juce::jmax(100, c.getWidth() - 40)), 130);
        lblStatus_.setBounds(msg.removeFromTop(74));
        btnTentar_->setBounds(msg.removeFromTop(tk.alturaBotaoPrimario).withSizeKeepingCentre(220, tk.alturaBotaoPrimario));
        lblStatus_.toFront(false);
        btnTentar_->toFront(false);
    }

    {  // Direita: miniatura grande + metadados
        auto d = rcInfo_.reduced(14);
        rcInfoImagem_ = d.removeFromTop(juce::jmin(d.getWidth(), 220));
        d.removeFromTop(12);
        btnMapa_->setBounds(d.removeFromBottom(36).withWidth(juce::jmin(d.getWidth(), 180)));
        d.removeFromBottom(8);
        rcInfoTexto_ = d;
    }

    if (preview_) {
        preview_->setBounds(getLocalBounds());
        preview_->toFront(false);
    }
}

void mostrarAndroidUsb(PainelOverlay& overlay, std::function<ConjuntoExistentes()> existentes,
                       std::function<void(RespostaReceberCelular, bool)> aoFechar, bool comVoltar) {
    auto resposta = std::make_shared<RespostaReceberCelular>();
    auto painel = std::make_shared<PainelAndroidUsb>(std::move(existentes), resposta, comVoltar);

    PainelOverlay::Config cfg;
    cfg.titulo = t("cel.usb.titulo");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.conteudo = painel;
    cfg.larguraCartao = 1500;  // o overlay encolhe até caber na janela
    cfg.alturaConteudo = 860;
    juce::Component::SafePointer<PainelAndroidUsb> seguro(painel.get());
    cfg.aoTecla = [seguro](const juce::KeyPress& k) { return seguro != nullptr && seguro->tratarTecla(k); };
    overlay.mostrar(cfg, [resposta, aoFechar](PainelOverlay::Resultado r) {
        if (aoFechar) aoFechar(*resposta, r.botaoId == 9);
    });
}

} // namespace matriz::ui
