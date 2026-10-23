#include "PainelAndroidUsb.h"

#include "../Celular/DispositivoAndroidUsb.h"
#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Model/NomesSeguros.h"
#include "GeoAcoes.h"
#include "LogosTipoArquivo.h"
#include "Moldura.h"
#include "OverlayComponent.h"
#include "Tablet.h"
#include "Tokens.h"
#include "VideoPlayerComponent.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <thread>

namespace matriz::ui {

using matriz::i18n::t;
using namespace matriz::celular;
using matriz::ingest::CategoriaMidia;

namespace {
// Mesmas medidas da janela de pasta do Folder Map (JanelaPastaFlutuante).
constexpr int kLarguraFiltros = 290;
constexpr int kLarguraFicha = 330;
constexpr int kAlturaCabecalho = 46;
constexpr std::uint32_t kRaiz = 0xffffffffu;  // LIBMTP_FILES_AND_FOLDERS_ROOT
constexpr int kAlturaNome = 22;
constexpr int kGap = 10;
constexpr int kMaxMiniaturas = 2500;
constexpr int kMenuMarcar = 1, kMenuPreview = 2, kMenuImportar = 3, kMenuMarcarPasta = 4, kMenuImportarPasta = 5, kMenuMarcarR = 6;

juce::String bytesLegiveis(std::uint64_t b) { return juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(b)); }
juce::String sep() { return juce::String::fromUTF8("  \xc2\xb7  "); }

CategoriaMidia categoriaDe(const EntradaAndroid& e) {
    return matriz::ingest::categoriaPorExtensao(e.nome.fromLastOccurrenceOf(".", false, false).toLowerCase());
}
bool ehImagem(CategoriaMidia c) { return c == CategoriaMidia::Imagem || c == CategoriaMidia::Arte; }
bool temMiniaturaProvavel(const EntradaAndroid& e) {
    const auto c = categoriaDe(e);
    return ehImagem(c) || c == CategoriaMidia::Video;
}

// Selo "R" (rejeitado: sai do celular), igual ao R do INTAKE: quadrado vermelho com a letra branca.
void desenharSeloR(juce::Graphics& g, juce::Rectangle<float> b) {
    g.setColour(tema().perigo);
    g.fillRoundedRectangle(b, 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(fonteUi(b.getHeight() * 0.62f, juce::Font::bold));
    g.drawText("R", b, juce::Justification::centred);
}

// Selo "I" (marcado para importar), no desenho dos selos H/K/P/W do Matriz: quadrado preto com a letra branca.
void desenharSeloI(juce::Graphics& g, juce::Rectangle<float> b) {
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(b, 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(fonteUi(b.getHeight() * 0.62f, juce::Font::bold));
    g.drawText("I", b, juce::Justification::centred);
}
// Viewport do mosaico que avisa quando a área visível muda (as miniaturas são pedidas só para o que aparece).
class ViewportComRolagem : public juce::Viewport {
public:
    std::function<void()> aoRolar;
    void visibleAreaChanged(const juce::Rectangle<int>&) override { if (aoRolar) aoRolar(); }
};
} // namespace

// ═════════════════════════════════════════════════════════════════════════════════════════════════
class JanelaAndroidUsb::Conteudo : public juce::Component, private juce::Timer {
public:
    enum class Estado { Conectando, Pronto, Erro, Preparando, Copiando };
    enum class Filtro { Todos, Imagem, Video, Audio, Documento };
    using Chave = std::pair<std::uint32_t, std::uint32_t>;  // (memória, id)
    static Chave chaveDe(const EntradaAndroid& e) { return {e.memoria, e.id}; }

    struct Metadados {
        bool lido = false;
        std::optional<int> largura, altura;
        std::optional<double> duracao;
        juce::String camera, dataOriginal;
        std::optional<double> lat, lon;
    };

    class Grade;
    class Filtros;
    class ItemPasta;
    class Preview;
    class JanelaPreview;

    Conteudo(std::function<ConjuntoExistentes()> existentes, std::function<void(juce::File, int)> aoImportar, std::function<void()> aoFechar);
    ~Conteudo() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool tratarTecla(const juce::KeyPress&);
    bool keyPressed(const juce::KeyPress& k) override { return tratarTecla(k); }
    void fechar();

    // ── estado (Grade, Filtros e ItemPasta leem direto) ──
    std::unique_ptr<SessaoAndroidUsb> sessao_;
    AparelhoAndroid aparelho_;
    Estado estado_ = Estado::Conectando;
    juce::String erro_, aviso_;
    std::function<void(juce::File, int)> aoImportar_;
    std::function<void()> aoFechar_;
    bool terminou_ = false;

    struct Nivel { std::uint32_t memoria, id; juce::String nome; };
    std::vector<Nivel> caminho_;                     // pasta aberta no mosaico (da raiz da memória até ela)
    std::vector<std::vector<Nivel>> historico_;      // para VOLTAR
    std::map<Chave, std::vector<EntradaAndroid>> cacheListas_;  // conteúdo de cada pasta já listada
    std::vector<EntradaAndroid> arquivos_;           // arquivos da pasta aberta
    std::vector<int> visiveis_;                      // índices em arquivos_ depois de filtro e busca
    std::map<Chave, EntradaAndroid> marcados_;       // arquivos e pastas marcados (pasta = tudo dentro)
    std::map<Chave, EntradaAndroid> rejeitados_;     // R: arquivos que vão ser APAGADOS do celular (só com confirmação)
    bool soRejeitados_ = false;
    bool apagarAposImportar_ = false;                // "mover": apaga do celular o que chegou inteiro no Mac
    std::unique_ptr<PainelOverlay> overlay_;
    std::set<int> selecao_;
    int cursor_ = -1, ancora_ = -1;
    bool focoNaArvore_ = false;
    ConjuntoExistentes existentes_;
    std::map<std::uint32_t, juce::Image> miniaturas_;
    std::set<std::uint32_t> semMiniatura_;
    std::map<std::uint32_t, Metadados> metadados_;
    std::map<juce::String, juce::Image> logos_;
    Filtro filtro_ = Filtro::Todos;
    bool soMarcados_ = false;
    int anoFiltro_ = 0;
    std::vector<int> anos_;
    struct Atalho { juce::String rotulo; std::vector<Nivel> cadeia; };
    std::vector<Atalho> atalhos_;
    bool listando_ = false;
    int geracaoLista_ = 0;
    double progresso_ = 0.0;
    int ladoTile_ = 150;
    std::map<Chave, ItemPasta*> itensArvore_;

    const EntradaAndroid* itemVisivel(int i) const {
        if (i < 0 || i >= static_cast<int>(visiveis_.size())) return nullptr;
        return &arquivos_[static_cast<size_t>(visiveis_[static_cast<size_t>(i)])];
    }
    bool estaMarcado(const EntradaAndroid& e) const { return marcados_.count(chaveDe(e)) > 0; }
    bool estaRejeitado(const EntradaAndroid& e) const { return rejeitados_.count(chaveDe(e)) > 0; }
    bool jaNoProjeto(const EntradaAndroid& e) const {
        return !e.pasta && !existentes_.empty() &&
               existentes_.count(chaveDoArquivo(e.nome.toStdString(), static_cast<std::int64_t>(e.tamanho))) > 0;
    }
    juce::File arquivoNoCache(const EntradaAndroid& e) const {
        return SessaoAndroidUsb::pastaCache(aparelho_.serial).getChildFile(juce::String(e.id))
            .getChildFile(matriz::nomes_seguros::sanitizarComponente(e.nome));
    }

    void conectar();
    void resolverAtalhos();
    void montarArvore();
    void listarPasta(std::uint32_t memoria, std::uint32_t id, std::function<void(bool, const std::vector<EntradaAndroid>&)> aoTerminar);
    void abrirPasta(std::vector<Nivel> cadeia, bool guardarHistorico = true);
    void voltar();
    void aplicarFiltro();
    void pedirMiniaturasVisiveis();
    void selecionar(int i, bool alternar, bool estender);
    void moverCursor(int delta, bool estender);
    void alternarMarca(std::vector<EntradaAndroid> itens);
    void alternarMarcaDaSelecao();
    void alternarRDaSelecao();
    void apagarRejeitados();
    void confirmarApagar();
    void abrirPreview(int i);
    void lerMetadadosDoSelecionado();
    void baixarParaCache(const EntradaAndroid& e, std::function<void(juce::File)> aoTerminar);
    void abrirMenuGrade();
    void abrirMenuPasta(const EntradaAndroid& pasta);
    void importar();
    void copiarParaIntake(std::vector<ItemCopiaAndroid> itens, bool confirmado = false);
    void pedirConfirmacao(const juce::String& titulo, const juce::String& msg, const juce::String& ok, std::function<void()> aoConfirmar);
    void atualizarTudo();
    void timerCallback() override { stopTimer(); lerMetadadosDoSelecionado(); }

    std::unique_ptr<Grade> grade_;
    std::unique_ptr<Filtros> filtros_;
    std::unique_ptr<JanelaPreview> janelaPreview_;
    ViewportComRolagem viewportGrade_;
    juce::Viewport viewportFiltros_;
    juce::TreeView arvore_;
    std::unique_ptr<juce::TreeViewItem> raizArvore_;
    juce::TextButton btnVoltar_;
    std::unique_ptr<BotaoPilula> btnImportar_, btnTentar_, btnMapa_, btnRejeitar_;
    juce::Label lblTitulo_, lblContagem_, lblStatus_;
    juce::TextEditor campoBusca_;
    juce::Slider sliderTamanho_;
    juce::ProgressBar barra_{progresso_};
    juce::Rectangle<int> rotuloArvore_, rcFicha_, rcFichaImagem_, rcFichaTexto_;
};

// ── Mosaico ─────────────────────────────────────────────────────────────────────────────────────

class JanelaAndroidUsb::Conteudo::Grade : public juce::Component {
public:
    explicit Grade(Conteudo& c) : c_(c) {}

    int colunas() const { return juce::jmax(1, (getWidth() - kGap) / (c_.ladoTile_ + kGap)); }
    int alturaLinha() const { return c_.ladoTile_ + kAlturaNome + kGap; }
    int alturaTotal() const {
        const int n = static_cast<int>(c_.visiveis_.size());
        return kGap + ((n + colunas() - 1) / colunas()) * alturaLinha();
    }
    juce::Rectangle<int> tile(int i) const {
        const int c = colunas();
        return {kGap + (i % c) * (c_.ladoTile_ + kGap), kGap + (i / c) * alturaLinha(), c_.ladoTile_, c_.ladoTile_ + kAlturaNome};
    }
    int indiceEm(juce::Point<int> pt) const {
        const int c = colunas(), h = juce::jmax(1, alturaLinha());
        if (pt.y < kGap || pt.x < kGap) return -1;
        const int col = (pt.x - kGap) / (c_.ladoTile_ + kGap);
        if (col >= c) return -1;
        const int i = ((pt.y - kGap) / h) * c + col;
        if (i < 0 || i >= static_cast<int>(c_.visiveis_.size())) return -1;
        return tile(i).contains(pt) ? i : -1;
    }
    std::pair<int, int> faixaVisivel(juce::Rectangle<int> area) const {
        const int c = colunas(), h = juce::jmax(1, alturaLinha());
        const int l0 = juce::jmax(0, area.getY() / h - 1), l1 = area.getBottom() / h + 1;
        return {l0 * c, juce::jmin(static_cast<int>(c_.visiveis_.size()), (l1 + 1) * c)};
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        const auto [de, ate] = faixaVisivel(g.getClipBounds());
        for (int i = de; i < ate; ++i) desenharTile(g, tk, i, tile(i));
    }

    void desenharTile(juce::Graphics& g, const Tema& tk, int i, juce::Rectangle<int> r) {
        const auto* e = c_.itemVisivel(i);
        if (e == nullptr) return;
        const bool sel = c_.selecao_.count(i) > 0;
        auto img = r.removeFromTop(c_.ladoTile_).toFloat();
        g.setColour(tk.painel);
        g.fillRoundedRectangle(img, 12.0f);
        auto it = c_.miniaturas_.find(e->id);
        if (it != c_.miniaturas_.end() && it->second.isValid()) {
            juce::Graphics::ScopedSaveState salva(g);
            juce::Path recorte;
            recorte.addRoundedRectangle(img, 12.0f);
            g.reduceClipRegion(recorte);
            g.drawImage(it->second, img, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        } else {
            const auto ext = e->nome.fromLastOccurrenceOf(".", false, false).toLowerCase();
            auto& logo = c_.logos_[ext];
            if (!logo.isValid()) logo = carregarLogoDeTipo(matriz::ingest::obterLogoParaExtensao(ext));
            if (logo.isValid()) g.drawImage(logo, img.withSizeKeepingCentre(img.getWidth() * 0.42f, img.getHeight() * 0.42f), juce::RectanglePlacement::centred);
            else desenharIcone(g, IconeUi::Arquivo, img.withSizeKeepingCentre(36, 36), tk.textoSecundario);
        }
        if (categoriaDe(*e) == CategoriaMidia::Video) {
            auto b = img.withTrimmedTop(img.getHeight() - 24).withTrimmedLeft(img.getWidth() - 30).reduced(4);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRoundedRectangle(b, 4.0f);
            juce::Path tri;
            tri.addTriangle(b.getCentreX() - 3.5f, b.getCentreY() - 5, b.getCentreX() - 3.5f, b.getCentreY() + 5, b.getCentreX() + 5, b.getCentreY());
            g.setColour(juce::Colours::white);
            g.fillPath(tri);
        }
        if (c_.jaNoProjeto(*e)) {
            auto b = juce::Rectangle<float>(img.getX() + 6, img.getBottom() - 22, 78, 16);
            g.setColour(tk.estadoQcOk);
            g.fillRoundedRectangle(b, 8.0f);
            g.setColour(juce::Colours::white);
            g.setFont(fonteUi(tk.tamanhoFonteMicro - 1.5f, juce::Font::bold));
            g.drawText(t("cel.usb.noProjeto"), b, juce::Justification::centred, true);
        }
        if (c_.estaMarcado(*e)) desenharSeloI(g, {img.getX() + 6, img.getY() + 6, 20, 20});
        if (c_.estaRejeitado(*e)) {  // R: o card escurece, como um arquivo que vai sair
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRoundedRectangle(img, 12.0f);
            desenharSeloR(g, {img.getX() + 6, img.getY() + 6, 20, 20});
        }
        if (sel) {
            g.setColour(tk.acento);
            g.drawRoundedRectangle(img.reduced(1.0f), 12.0f, 3.0f);
        }
        g.setColour(sel ? tk.textoPrimario : tk.textoSecundario);
        g.setFont(fonteUi(tk.tamanhoFontePequena, sel ? juce::Font::bold : juce::Font::plain));
        g.drawText(e->nome, r.withTrimmedTop(3), juce::Justification::centredTop, true);
    }

    void mouseDown(const juce::MouseEvent& ev) override {
        c_.focoNaArvore_ = false;
        c_.grabKeyboardFocus();
        const int i = indiceEm(ev.getPosition());
        if (ev.mods.isPopupMenu()) {
            if (i >= 0 && c_.selecao_.count(i) == 0) c_.selecionar(i, false, false);
            c_.abrirMenuGrade();
            return;
        }
        if (i < 0) { c_.selecao_.clear(); c_.cursor_ = -1; c_.atualizarTudo(); return; }
        const auto r = tile(i);
        if (juce::Rectangle<int>(r.getX(), r.getY(), 30, 30).contains(ev.getPosition()) && !ev.mods.isAnyModifierKeyDown()) {
            if (const auto* e = c_.itemVisivel(i)) c_.alternarMarca({*e});  // clique no canto do selo
            return;
        }
        c_.selecionar(i, ev.mods.isCommandDown(), ev.mods.isShiftDown());
    }
    void mouseDoubleClick(const juce::MouseEvent& ev) override {
        const int i = indiceEm(ev.getPosition());
        if (i >= 0) c_.abrirPreview(i);
    }
    void mouseWheelMove(const juce::MouseEvent& ev, const juce::MouseWheelDetails& w) override {
        if (ev.mods.isCommandDown()) { c_.sliderTamanho_.setValue(c_.sliderTamanho_.getValue() + (w.deltaY > 0 ? 12 : -12)); return; }
        Component::mouseWheelMove(ev, w);
    }

private:
    Conteudo& c_;
};

// ── Coluna de filtros (mesmo desenho do FiltrosComponent: cabeçalhos de seção + pílulas com contagem) ─────────

class JanelaAndroidUsb::Conteudo::Filtros : public juce::Component {
public:
    explicit Filtros(Conteudo& c) : c_(c) {}

    struct Linha { int tipo = 0; juce::String rotulo; int contagem = -1; bool ativo = false; int valor = 0; juce::Colour ponto; };
    enum { kCabecalho, kTipo, kAno, kMarcados, kAtalho, kRejeitados, kApagarApos };
    static constexpr int kAlturaSecao = 22, kAlturaLinha = 24;

    void reconstruir() {
        linhas_.clear();
        int cont[5] = {}, marc = 0;
        std::map<int, int> porAno;
        for (const auto& e : c_.arquivos_) {
            const auto cat = categoriaDe(e);
            ++cont[0];
            if (ehImagem(cat)) ++cont[1];
            if (cat == CategoriaMidia::Video) ++cont[2];
            if (cat == CategoriaMidia::Audio) ++cont[3];
            if (cat == CategoriaMidia::Documento || cat == CategoriaMidia::Texto) ++cont[4];
            if (e.modificado.toMilliseconds() > 0) ++porAno[e.modificado.getYear()];
        }
        marc = static_cast<int>(c_.marcados_.size());
        linhas_.push_back({kCabecalho, t("cel.usb.sec.tipo")});
        const std::pair<const char*, juce::Colour> tipos[] = {{"cel.usb.f.todos", juce::Colour(0xff1a1a1a)},
                                                             {"cel.usb.f.imagem", juce::Colour(0xfff4a261)},
                                                             {"cel.usb.f.video", juce::Colour(0xff9d4edd)},
                                                             {"cel.usb.f.audio", juce::Colour(0xff2a9d8f)},
                                                             {"cel.usb.f.doc", juce::Colour(0xff2b9348)}};
        for (int i = 0; i < 5; ++i)
            linhas_.push_back({kTipo, t(tipos[i].first), cont[i], static_cast<int>(c_.filtro_) == i, i, tipos[i].second});
        linhas_.push_back({kCabecalho, t("cel.usb.sec.importar")});
        linhas_.push_back({kMarcados, t("cel.usb.f.marcados"), marc, c_.soMarcados_});
        linhas_.push_back({kRejeitados, t("cel.usb.f.rejeitados"), static_cast<int>(c_.rejeitados_.size()), c_.soRejeitados_});
        linhas_.push_back({kApagarApos, t("cel.usb.apagarApos"), -1, c_.apagarAposImportar_});
        if (!porAno.empty()) {
            linhas_.push_back({kCabecalho, t("cel.usb.sec.data")});
            for (auto it = porAno.rbegin(); it != porAno.rend(); ++it)
                linhas_.push_back({kAno, juce::String(it->first), it->second, c_.anoFiltro_ == it->first, it->first});
        }
        if (!c_.atalhos_.empty()) {
            linhas_.push_back({kCabecalho, t("cel.usb.sec.atalhos")});
            for (int i = 0; i < static_cast<int>(c_.atalhos_.size()); ++i)
                linhas_.push_back({kAtalho, c_.atalhos_[static_cast<size_t>(i)].rotulo, -1, false, i});
        }
        int h = 0;
        for (const auto& l : linhas_) h += l.tipo == kCabecalho ? kAlturaSecao : kAlturaLinha;
        setSize(getWidth(), juce::jmax(h + 8, 40));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);
        int y = 0;
        for (const auto& l : linhas_) {
            const int h = l.tipo == kCabecalho ? kAlturaSecao : kAlturaLinha;
            auto b = juce::Rectangle<int>(0, y, getWidth(), h);
            y += h;
            if (l.tipo == kCabecalho) {
                g.setColour(tk.painelAlt);
                g.fillRect(b);
                g.setColour(tk.textoSecundario);
                g.setFont(fonteUi(10.5f, juce::Font::bold).withExtraKerningFactor(0.06f));
                g.drawText(moldura::caixaAlta(l.rotulo), b.reduced(tk.espacoMedio, 0), juce::Justification::centredLeft);
                continue;
            }
            if (l.tipo == kApagarApos) {  // interruptor (caixa de marcar), não filtro
                auto miolo = b.reduced(tk.espacoMedio, 0);
                auto caixa = miolo.removeFromLeft(18).toFloat().withSizeKeepingCentre(14, 14);
                g.setColour(l.ativo ? tk.perigo : tk.campoFundo);
                g.fillRoundedRectangle(caixa, 3.0f);
                g.setColour(l.ativo ? tk.perigo : tk.borda);
                g.drawRoundedRectangle(caixa, 3.0f, 1.2f);
                if (l.ativo) {
                    juce::Path v;
                    v.startNewSubPath(caixa.getX() + 3.0f, caixa.getCentreY());
                    v.lineTo(caixa.getX() + 6.0f, caixa.getBottom() - 3.5f);
                    v.lineTo(caixa.getRight() - 2.5f, caixa.getY() + 3.5f);
                    g.setColour(juce::Colours::white);
                    g.strokePath(v, juce::PathStrokeType(1.8f));
                }
                miolo.removeFromLeft(6);
                g.setColour(l.ativo ? tk.perigo : tk.textoPrimario);
                g.setFont(fonteUi(tk.tamanhoFonteCorpo, l.ativo ? juce::Font::bold : juce::Font::plain));
                g.drawText(l.rotulo, miolo, juce::Justification::centredLeft, true);
                continue;
            }
            const bool chip = l.tipo != kAtalho;
            if (chip) {
                auto pilula = b.toFloat().reduced(static_cast<float>(tk.espacoPequeno), 2.0f);
                g.setColour(l.ativo ? tk.acento : tk.painelAlt);
                g.fillRoundedRectangle(pilula, pilula.getHeight() * 0.5f);
            }
            auto miolo = b.reduced(tk.espacoMedio + (chip ? tk.espacoMedio : 0), 0);
            if (l.tipo == kTipo) {
                auto ponto = miolo.removeFromLeft(14);
                g.setColour(l.ponto);
                g.fillEllipse(juce::Rectangle<float>(ponto.getX() + 2.0f, ponto.getCentreY() - 4.0f, 8.0f, 8.0f));
            } else if (l.tipo == kMarcados || l.tipo == kRejeitados) {
                const auto b = juce::Rectangle<float>(static_cast<float>(miolo.getX()), miolo.getCentreY() - 7.0f, 14, 14);
                if (l.tipo == kMarcados) desenharSeloI(g, b);
                else desenharSeloR(g, b);
                miolo.removeFromLeft(20);
            } else if (l.tipo == kAtalho) {
                desenharIcone(g, IconeUi::Pasta, miolo.removeFromLeft(18).toFloat().withSizeKeepingCentre(15, 15), tk.acento);
                miolo.removeFromLeft(6);
            }
            if (l.contagem >= 0) {
                g.setColour(l.ativo ? tk.textoSobreAcento.withAlpha(0.8f) : tk.textoTerciario);
                g.setFont(fonteUi(tk.tamanhoFontePequena));
                g.drawText(juce::String(l.contagem), miolo.removeFromRight(36), juce::Justification::centredRight);
            }
            g.setColour(l.ativo ? tk.textoSobreAcento : tk.textoPrimario);
            g.setFont(fonteUi(tk.tamanhoFonteCorpo));
            g.drawText(l.rotulo, miolo, juce::Justification::centredLeft, true);
        }
    }

    void mouseUp(const juce::MouseEvent& ev) override {
        int y = 0;
        for (const auto& l : linhas_) {
            const int h = l.tipo == kCabecalho ? kAlturaSecao : kAlturaLinha;
            if (ev.y >= y && ev.y < y + h) { clicar(l); return; }
            y += h;
        }
    }

private:
    void clicar(const Linha& l) {
        if (l.tipo == kTipo) c_.filtro_ = static_cast<Filtro>(l.valor);
        else if (l.tipo == kMarcados) { c_.soMarcados_ = !c_.soMarcados_; c_.soRejeitados_ = false; }
        else if (l.tipo == kRejeitados) { c_.soRejeitados_ = !c_.soRejeitados_; c_.soMarcados_ = false; }
        else if (l.tipo == kApagarApos) { c_.apagarAposImportar_ = !c_.apagarAposImportar_; reconstruir(); c_.atualizarTudo(); return; }
        else if (l.tipo == kAno) c_.anoFiltro_ = c_.anoFiltro_ == l.valor ? 0 : l.valor;
        else if (l.tipo == kAtalho) { c_.abrirPasta(c_.atalhos_[static_cast<size_t>(l.valor)].cadeia); return; }
        else return;
        c_.aplicarFiltro();
    }

    Conteudo& c_;
    std::vector<Linha> linhas_;
};

// ── Árvore das pastas do celular (como a de SUBFOLDERS da janela de pasta): carrega cada nível ao abrir ──────────

class JanelaAndroidUsb::Conteudo::ItemPasta : public juce::TreeViewItem {
public:
    ItemPasta(Conteudo& c, EntradaAndroid pasta, std::vector<Nivel> cadeia, bool ehMemoria)
        : c_(c), pasta_(std::move(pasta)), cadeia_(std::move(cadeia)), ehMemoria_(ehMemoria) {
        c_.itensArvore_[chave()] = this;
    }
    ~ItemPasta() override {
        auto it = c_.itensArvore_.find(chave());
        if (it != c_.itensArvore_.end() && it->second == this) c_.itensArvore_.erase(it);
    }
    Chave chave() const { return {pasta_.memoria, ehMemoria_ ? kRaiz : pasta_.id}; }
    const EntradaAndroid& pasta() const { return pasta_; }
    const std::vector<Nivel>& cadeia() const { return cadeia_; }

    bool mightContainSubItems() override { return !carregado_ || getNumSubItems() > 0; }
    int getItemHeight() const override { return 22; }
    juce::String getUniqueName() const override { return juce::String(pasta_.memoria) + ":" + juce::String(chave().second); }

    void itemOpennessChanged(bool aberto) override {
        if (!aberto || carregado_ || pedindo_) return;
        pedindo_ = true;
        c_.listarPasta(pasta_.memoria, chave().second, {});  // o retorno chega por preencher()
    }
    void preencher(const std::vector<EntradaAndroid>& itens) {
        pedindo_ = false;
        if (carregado_) return;
        carregado_ = true;
        clearSubItems();
        for (const auto& e : itens) {
            if (!e.pasta) continue;
            auto cad = cadeia_;
            cad.push_back({e.memoria, e.id, e.nome});
            addSubItem(new ItemPasta(c_, e, std::move(cad), false));
        }
        treeHasChanged();
    }

    void paintItem(juce::Graphics& g, int w, int h) override {
        const auto& tk = tema();
        if (isSelected()) {
            g.setColour(tk.acento.withAlpha(0.18f));
            g.fillRoundedRectangle(2.0f, 1.0f, static_cast<float>(w - 4), static_cast<float>(h - 2), 4.0f);
        }
        float x = 4.0f;
        if (c_.estaMarcado(pasta_) && !ehMemoria_) {
            desenharSeloI(g, {x, (h - 14) * 0.5f, 14, 14});
            x += 20.0f;
        }
        g.setColour(tk.textoPrimario);
        g.setFont(fonteUi(tk.tamanhoFontePequena + 1.0f, ehMemoria_ ? juce::Font::bold : juce::Font::plain));
        g.drawText(pasta_.nome, static_cast<int>(x), 0, w - static_cast<int>(x) - 4, h, juce::Justification::centredLeft, true);
    }
    void itemClicked(const juce::MouseEvent& ev) override {
        c_.focoNaArvore_ = !ehMemoria_;
        c_.grabKeyboardFocus();
        if (ev.mods.isPopupMenu()) { if (!ehMemoria_) c_.abrirMenuPasta(pasta_); return; }
        c_.abrirPasta(cadeia_);
        setOpen(true);
    }

private:
    Conteudo& c_;
    EntradaAndroid pasta_;
    std::vector<Nivel> cadeia_;
    bool ehMemoria_;
    bool carregado_ = false, pedindo_ = false;
};

// ── Preview numa janela própria (como o FloatingPreviewWindow): imagem, vídeo ou áudio do cache ───────────────

class JanelaAndroidUsb::Conteudo::Preview : public juce::Component {
public:
    explicit Preview(Conteudo& c) : c_(c) { setSize(1100, 760); }
    ~Preview() override { if (video_) video_->parar(); }

    void mostrar(int indice) {
        indice_ = indice;
        imagem_ = {};
        if (video_) { video_->parar(); video_.reset(); }
        const auto* e = c_.itemVisivel(indice);
        if (e == nullptr) return;
        nome_ = e->nome;
        mensagem_ = t("cel.usb.preview.baixando");
        repaint();
        const auto entrada = *e;
        juce::Component::SafePointer<Preview> seguro(this);
        c_.baixarParaCache(entrada, [seguro, entrada](juce::File f) {
            if (seguro == nullptr) return;
            const auto* atual = seguro->c_.itemVisivel(seguro->indice_);
            if (atual == nullptr || atual->id != entrada.id) return;
            seguro->exibir(f, categoriaDe(entrada));
        });
    }
    int indice() const { return indice_; }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(juce::Colours::black);
        auto r = getLocalBounds().reduced(14);
        auto topo = r.removeFromTop(26);
        g.setColour(juce::Colours::white);
        g.setFont(fonteUi(tk.fonteCorpoGrande, juce::Font::bold));
        const auto* e = c_.itemVisivel(indice_);
        auto tit = topo;
        if (e != nullptr && c_.estaMarcado(*e)) { desenharSeloI(g, tit.removeFromLeft(20).toFloat().withSizeKeepingCentre(18, 18)); tit.removeFromLeft(8); }
        if (e != nullptr && c_.estaRejeitado(*e)) { desenharSeloR(g, tit.removeFromLeft(20).toFloat().withSizeKeepingCentre(18, 18)); tit.removeFromLeft(8); }
        g.drawText(nome_ + sep() + juce::String(indice_ + 1) + " / " + juce::String(static_cast<int>(c_.visiveis_.size())), tit,
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
    void resized() override { if (video_) video_->setBounds(getLocalBounds().reduced(14).withTrimmedTop(26).reduced(0, 8)); }

private:
    void exibir(const juce::File& f, CategoriaMidia cat) {
        if (!f.existsAsFile()) { mensagem_ = t("cel.usb.preview.falhou"); repaint(); return; }
        if (ehImagem(cat)) {  // decodifica fora da message thread (foto de 12 MP leva ~100 ms)
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

    Conteudo& c_;
    int indice_ = -1;
    juce::String nome_, mensagem_;
    juce::Image imagem_;
    std::unique_ptr<VideoPlayerComponent> video_;
};

class JanelaAndroidUsb::Conteudo::JanelaPreview : public juce::DocumentWindow {
public:
    explicit JanelaPreview(Conteudo& c)
        : juce::DocumentWindow("Preview", moldura::paleta().fundo, juce::DocumentWindow::closeButton), c_(c) {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setResizable(true, false);
        preview_ = new Preview(c);
        setContentOwned(new moldura::JanelaMoldura(preview_, moldura::Secao::Explorador, "Phone", t("cel.usb.preview.titulo")), true);
        setResizeLimits(640, 420, 4000, 3000);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
        toFront(true);
    }
    Preview& preview() { return *preview_; }
    void closeButtonPressed() override { fecharDepois(); }
    bool keyPressed(const juce::KeyPress& k) override {
        const int code = k.getKeyCode();
        if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::spaceKey) { fecharDepois(); return true; }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
            const int alvo = preview_->indice() + (code == juce::KeyPress::leftKey ? -1 : 1);
            if (c_.itemVisivel(alvo) != nullptr) { c_.selecionar(alvo, false, false); preview_->mostrar(alvo); }
            return true;
        }
        if (k.getTextCharacter() == 'i' || k.getTextCharacter() == 'I') {
            if (const auto* e = c_.itemVisivel(preview_->indice())) c_.alternarMarca({*e});
            preview_->repaint();
            return true;
        }
        if (k.getTextCharacter() == 'r' || k.getTextCharacter() == 'R') {
            c_.alternarRDaSelecao();
            preview_->repaint();
            return true;
        }
        return DocumentWindow::keyPressed(k);
    }

private:
    void fecharDepois() {
        juce::Component::SafePointer<Conteudo> seguro(&c_);
        juce::MessageManager::callAsync([seguro] { if (seguro != nullptr) seguro->janelaPreview_.reset(); });
    }
    Conteudo& c_;
    Preview* preview_ = nullptr;
};

// ── Conteúdo ────────────────────────────────────────────────────────────────────────────────────

JanelaAndroidUsb::Conteudo::Conteudo(std::function<ConjuntoExistentes()> existentes, std::function<void(juce::File, int)> aoImportar,
                                     std::function<void()> aoFechar)
    : sessao_(std::make_unique<SessaoAndroidUsb>()), aoImportar_(std::move(aoImportar)), aoFechar_(std::move(aoFechar)) {
    setWantsKeyboardFocus(true);
    const auto& tk = tema();

    grade_ = std::make_unique<Grade>(*this);
    viewportGrade_.setViewedComponent(grade_.get(), false);
    viewportGrade_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewportGrade_);
    viewportGrade_.aoRolar = [this] { pedirMiniaturasVisiveis(); };

    filtros_ = std::make_unique<Filtros>(*this);
    viewportFiltros_.setViewedComponent(filtros_.get(), false);
    viewportFiltros_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewportFiltros_);

    arvore_.setRootItemVisible(false);
    arvore_.setIndentSize(14);
    arvore_.setColour(juce::TreeView::backgroundColourId, tk.painel);
    arvore_.setWantsKeyboardFocus(false);
    addAndMakeVisible(arvore_);

    btnVoltar_.setButtonText(t("janela_pasta.voltar"));
    btnVoltar_.setWantsKeyboardFocus(false);
    btnVoltar_.onClick = [this] { voltar(); };
    addAndMakeVisible(btnVoltar_);

    lblTitulo_.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    lblTitulo_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblTitulo_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(lblTitulo_);

    campoBusca_.setTextToShowWhenEmpty(t("janela_pasta.busca"), tk.textoTerciario);
    campoBusca_.setIndents(12, 0);
    campoBusca_.onTextChange = [this] { aplicarFiltro(); };
    campoBusca_.onEscapeKey = [this] { campoBusca_.clear(); grabKeyboardFocus(); aplicarFiltro(); };
    addAndMakeVisible(campoBusca_);

    btnImportar_ = std::make_unique<BotaoPilula>(t("cel.usb.importar"), BotaoPilula::Estilo::Primario);
    btnImportar_->setWantsKeyboardFocus(false);
    btnImportar_->onClick = [this] {
        if (estado_ == Estado::Copiando || estado_ == Estado::Preparando) sessao_->cancelar();
        else importar();
    };
    addAndMakeVisible(*btnImportar_);
    btnRejeitar_ = std::make_unique<BotaoPilula>(t("cel.usb.rejeitar"), BotaoPilula::Estilo::Perigo);
    btnRejeitar_->setWantsKeyboardFocus(false);
    btnRejeitar_->setTooltip(t("cel.usb.rejeitar.dica"));
    btnRejeitar_->onClick = [this] { confirmarApagar(); };
    addAndMakeVisible(*btnRejeitar_);
    btnTentar_ = std::make_unique<BotaoPilula>(t("cel.usb.tentar"), BotaoPilula::Estilo::Primario);
    btnTentar_->setWantsKeyboardFocus(false);
    btnTentar_->onClick = [this] { conectar(); };
    addChildComponent(*btnTentar_);
    btnMapa_ = std::make_unique<BotaoPilula>(t("cel.usb.abrirMapa"), BotaoPilula::Estilo::Secundario);
    btnMapa_->setWantsKeyboardFocus(false);
    btnMapa_->onClick = [this] {
        const auto* e = itemVisivel(cursor_);
        if (e == nullptr) return;
        auto it = metadados_.find(e->id);
        if (it == metadados_.end() || !it->second.lat || !it->second.lon) return;
        geoacoes::Local l;
        l.coords = juce::String(*it->second.lat, 6) + ", " + juce::String(*it->second.lon, 6);
        geoacoes::abrirNoGoogleMaps(l);
    };
    addChildComponent(*btnMapa_);

    for (auto* l : {&lblContagem_, &lblStatus_}) {
        l->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        l->setColour(juce::Label::textColourId, tk.textoSecundario);
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
    }
    lblStatus_.setJustificationType(juce::Justification::centred);
    addChildComponent(barra_);

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

    if (existentes) {  // "Já está no projeto": a mesma identidade (nome, tamanho) do QR code, lida fora da message thread
        juce::Component::SafePointer<Conteudo> seguro(this);
        std::thread([existentes, seguro] {
            auto conjunto = existentes();
            juce::MessageManager::callAsync([seguro, conjunto = std::move(conjunto)]() mutable {
                if (seguro == nullptr) return;
                seguro->existentes_ = std::move(conjunto);
                seguro->grade_->repaint();
            });
        }).detach();
    }

    // Do tamanho da tela (a janela nunca corta nada); o usuário pode redimensionar.
    auto tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    setSize(juce::jlimit(980, 1600, tela.getWidth() * 88 / 100), juce::jlimit(600, 1000, tela.getHeight() * 80 / 100));
    conectar();
}

JanelaAndroidUsb::Conteudo::~Conteudo() {
    stopTimer();
    overlay_.reset();
    janelaPreview_.reset();
    arvore_.setRootItem(nullptr);
    raizArvore_.reset();
    sessao_->pedirMiniaturas({}, nullptr);
}

void JanelaAndroidUsb::Conteudo::fechar() {
    if (terminou_) return;
    terminou_ = true;
    if (aoFechar_) aoFechar_();
}

void JanelaAndroidUsb::Conteudo::conectar() {
    estado_ = Estado::Conectando;
    erro_.clear();
    arquivos_.clear();
    visiveis_.clear();
    caminho_.clear();
    historico_.clear();
    cacheListas_.clear();
    arvore_.setRootItem(nullptr);
    raizArvore_.reset();
    atualizarTudo();
    juce::Component::SafePointer<Conteudo> seguro(this);
    sessao_->conectar([seguro](bool ok, AparelhoAndroid info, juce::String erro) {
        if (seguro == nullptr) return;
        auto& s = *seguro;
        if (!ok) { s.estado_ = Estado::Erro; s.erro_ = erro; s.atualizarTudo(); return; }
        s.aparelho_ = std::move(info);
        s.estado_ = Estado::Pronto;
        s.montarArvore();
        if (!s.aparelho_.memorias.empty()) {
            const auto& m = s.aparelho_.memorias.front();
            s.abrirPasta({{m.id, kRaiz, m.descricao.isNotEmpty() ? m.descricao : t("cel.usb.memoria")}}, false);
        }
        s.resolverAtalhos();
    });
}

void JanelaAndroidUsb::Conteudo::montarArvore() {
    arvore_.setRootItem(nullptr);
    struct Raiz : juce::TreeViewItem {
        bool mightContainSubItems() override { return true; }
    };
    raizArvore_ = std::make_unique<Raiz>();
    for (const auto& m : aparelho_.memorias) {
        EntradaAndroid e;
        e.memoria = m.id;
        e.id = kRaiz;
        e.pasta = true;
        e.nome = m.descricao.isNotEmpty() ? m.descricao : t("cel.usb.memoria");
        raizArvore_->addSubItem(new ItemPasta(*this, e, {{m.id, kRaiz, e.nome}}, true));
    }
    arvore_.setRootItem(raizArvore_.get());
    raizArvore_->setOpen(true);
    if (raizArvore_->getNumSubItems() > 0) raizArvore_->getSubItem(0)->setOpen(true);
}

void JanelaAndroidUsb::Conteudo::resolverAtalhos() {
    atalhos_.clear();
    const std::vector<std::pair<const char*, std::vector<juce::String>>> pedidos = {
        {"cel.usb.at.camera", {"DCIM", "Camera"}},
        {"cel.usb.at.whatsapp", {"Android", "media", "com.whatsapp", "WhatsApp", "Media"}},
        {"cel.usb.at.whatsapp", {"WhatsApp", "Media"}},
        {"cel.usb.at.screenshots", {"DCIM", "Screenshots"}},
        {"cel.usb.at.screenshots", {"Pictures", "Screenshots"}},
        {"cel.usb.at.downloads", {"Download"}}};
    if (aparelho_.memorias.empty()) return;
    const auto memoria = aparelho_.memorias.front().id;
    juce::Component::SafePointer<Conteudo> seguro(this);
    for (const auto& [chave, partes] : pedidos) {
        const juce::String rotulo = t(chave);
        sessao_->resolver(memoria, partes, [seguro, rotulo, memoria](std::vector<std::pair<std::uint32_t, juce::String>> cadeia) {
            if (seguro == nullptr || cadeia.empty()) return;
            auto& s = *seguro;
            for (const auto& a : s.atalhos_) if (a.rotulo == rotulo) return;  // o primeiro caminho que existe vence
            Atalho a;
            a.rotulo = rotulo;
            a.cadeia.push_back({memoria, kRaiz, s.aparelho_.memorias.front().descricao});
            for (const auto& [id, nome] : cadeia) a.cadeia.push_back({memoria, id, nome});
            s.atalhos_.push_back(std::move(a));
            s.filtros_->reconstruir();
            s.resized();
        });
    }
}

// Lista uma pasta (com cache) e entrega o resultado também ao item da árvore daquela pasta, se existir.
void JanelaAndroidUsb::Conteudo::listarPasta(std::uint32_t memoria, std::uint32_t id,
                                             std::function<void(bool, const std::vector<EntradaAndroid>&)> aoTerminar) {
    const Chave k{memoria, id};
    if (auto it = cacheListas_.find(k); it != cacheListas_.end()) {
        if (auto ia = itensArvore_.find(k); ia != itensArvore_.end()) ia->second->preencher(it->second);
        if (aoTerminar) aoTerminar(true, it->second);
        return;
    }
    juce::Component::SafePointer<Conteudo> seguro(this);
    sessao_->listar(memoria, id, [seguro, k, aoTerminar](bool ok, std::vector<EntradaAndroid> itens, juce::String erro) {
        if (seguro == nullptr) return;
        auto& s = *seguro;
        if (ok) s.cacheListas_[k] = itens;
        else { s.estado_ = Estado::Erro; s.erro_ = erro; }
        if (auto ia = s.itensArvore_.find(k); ia != s.itensArvore_.end()) ia->second->preencher(itens);
        if (aoTerminar) aoTerminar(ok, itens);
        if (!ok) s.atualizarTudo();
    });
}

void JanelaAndroidUsb::Conteudo::abrirPasta(std::vector<Nivel> cadeia, bool guardarHistorico) {
    if (estado_ != Estado::Pronto || cadeia.empty()) return;
    if (guardarHistorico && !caminho_.empty()) historico_.push_back(caminho_);
    caminho_ = std::move(cadeia);
    listando_ = true;
    const int geracao = ++geracaoLista_;
    arquivos_.clear();
    aplicarFiltro();
    juce::Component::SafePointer<Conteudo> seguro(this);
    const auto& alvo = caminho_.back();
    listarPasta(alvo.memoria, alvo.id, [seguro, geracao](bool, const std::vector<EntradaAndroid>& itens) {
        if (seguro == nullptr || geracao != seguro->geracaoLista_) return;  // o usuário já abriu outra pasta
        auto& s = *seguro;
        s.listando_ = false;
        s.arquivos_.clear();
        for (const auto& e : itens) if (!e.pasta) s.arquivos_.push_back(e);
        // do mais novo para o mais antigo, como a galeria do celular
        std::stable_sort(s.arquivos_.begin(), s.arquivos_.end(),
                         [](const EntradaAndroid& a, const EntradaAndroid& b) { return a.modificado > b.modificado; });
        s.anoFiltro_ = 0;
        s.aplicarFiltro();
        s.viewportGrade_.setViewPosition(0, 0);
    });
}

void JanelaAndroidUsb::Conteudo::voltar() {
    if (historico_.empty()) {  // sem histórico: sobe uma pasta
        if (caminho_.size() <= 1) return;
        auto acima = caminho_;
        acima.pop_back();
        abrirPasta(acima, false);
        return;
    }
    auto anterior = historico_.back();
    historico_.pop_back();
    abrirPasta(anterior, false);
}

void JanelaAndroidUsb::Conteudo::aplicarFiltro() {
    visiveis_.clear();
    const auto busca = campoBusca_.getText().trim();
    for (int i = 0; i < static_cast<int>(arquivos_.size()); ++i) {
        const auto& e = arquivos_[static_cast<size_t>(i)];
        if (anoFiltro_ != 0 && e.modificado.getYear() != anoFiltro_) continue;
        if (soMarcados_ && !estaMarcado(e)) continue;
        if (soRejeitados_ && !estaRejeitado(e)) continue;
        if (busca.isNotEmpty() && !e.nome.containsIgnoreCase(busca)) continue;
        const auto c = categoriaDe(e);
        const bool passa = filtro_ == Filtro::Todos || (filtro_ == Filtro::Imagem && ehImagem(c)) ||
                           (filtro_ == Filtro::Video && c == CategoriaMidia::Video) || (filtro_ == Filtro::Audio && c == CategoriaMidia::Audio) ||
                           (filtro_ == Filtro::Documento && (c == CategoriaMidia::Documento || c == CategoriaMidia::Texto));
        if (passa) visiveis_.push_back(i);
    }
    selecao_.clear();
    cursor_ = ancora_ = -1;
    filtros_->reconstruir();
    atualizarTudo();
    pedirMiniaturasVisiveis();
}

void JanelaAndroidUsb::Conteudo::pedirMiniaturasVisiveis() {
    if (estado_ == Estado::Conectando) return;
    const auto [de, ate] = grade_->faixaVisivel(viewportGrade_.getViewArea());
    std::vector<EntradaAndroid> faltam;
    for (int i = de; i < ate; ++i) {
        const auto* e = itemVisivel(i);
        if (e != nullptr && temMiniaturaProvavel(*e) && miniaturas_.count(e->id) == 0 && semMiniatura_.count(e->id) == 0) faltam.push_back(*e);
    }
    juce::Component::SafePointer<Conteudo> seguro(this);
    sessao_->pedirMiniaturas(std::move(faltam), [seguro](std::uint32_t id, juce::Image img) {
        if (seguro == nullptr) return;
        auto& s = *seguro;
        if (!img.isValid()) { s.semMiniatura_.insert(id); return; }
        const int maior = juce::jmax(img.getWidth(), img.getHeight());
        if (maior > 360) img = img.rescaled(juce::jmax(1, img.getWidth() * 360 / maior), juce::jmax(1, img.getHeight() * 360 / maior));
        if (static_cast<int>(s.miniaturas_.size()) >= kMaxMiniaturas) s.miniaturas_.erase(s.miniaturas_.begin());
        s.miniaturas_[id] = img;
        s.grade_->repaint();
        s.repaint(s.rcFichaImagem_);
    });
}

void JanelaAndroidUsb::Conteudo::selecionar(int i, bool alternar, bool estender) {
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
    const auto vista = viewportGrade_.getViewArea();
    if (r.getY() < vista.getY()) viewportGrade_.setViewPosition(0, juce::jmax(0, r.getY() - kGap));
    else if (r.getBottom() > vista.getBottom()) viewportGrade_.setViewPosition(0, r.getBottom() - vista.getHeight() + kGap);
    startTimer(450);  // metadados (GEO) só depois de parar num item
    atualizarTudo();
}

void JanelaAndroidUsb::Conteudo::moverCursor(int delta, bool estender) {
    if (visiveis_.empty()) return;
    selecionar(juce::jlimit(0, static_cast<int>(visiveis_.size()) - 1, cursor_ < 0 ? 0 : cursor_ + delta), false, estender);
}

// Tudo marcado -> desmarca; senão marca tudo.
void JanelaAndroidUsb::Conteudo::alternarMarca(std::vector<EntradaAndroid> itens) {
    if (itens.empty() || estado_ != Estado::Pronto) return;
    bool todos = true;
    for (const auto& e : itens) if (!estaMarcado(e)) { todos = false; break; }
    for (const auto& e : itens) {
        if (todos) marcados_.erase(chaveDe(e));
        else { marcados_[chaveDe(e)] = e; rejeitados_.erase(chaveDe(e)); }  // I e R não convivem
    }
    if (soMarcados_ || soRejeitados_) { aplicarFiltro(); return; }
    filtros_->reconstruir();
    arvore_.repaint();
    atualizarTudo();
}

void JanelaAndroidUsb::Conteudo::alternarMarcaDaSelecao() {
    if (focoNaArvore_) {
        if (auto* sel = dynamic_cast<ItemPasta*>(arvore_.getSelectedItem(0))) alternarMarca({sel->pasta()});
        return;
    }
    std::vector<EntradaAndroid> itens;
    for (int i : selecao_) if (const auto* e = itemVisivel(i)) itens.push_back(*e);
    alternarMarca(std::move(itens));
}

// R vale como no INTAKE: o item em foco ou a seleção; só arquivos (pasta não vai para o R).
void JanelaAndroidUsb::Conteudo::alternarRDaSelecao() {
    if (estado_ != Estado::Pronto || focoNaArvore_) return;
    std::vector<EntradaAndroid> itens;
    for (int i : selecao_) if (const auto* e = itemVisivel(i); e != nullptr && !e->pasta) itens.push_back(*e);
    if (itens.empty()) if (const auto* e = itemVisivel(cursor_)) itens.push_back(*e);
    if (itens.empty()) return;
    bool todos = true;
    for (const auto& e : itens) if (!estaRejeitado(e)) { todos = false; break; }
    for (const auto& e : itens) {
        if (todos) rejeitados_.erase(chaveDe(e));
        else { rejeitados_[chaveDe(e)] = e; marcados_.erase(chaveDe(e)); }  // I e R não convivem
    }
    if (soMarcados_ || soRejeitados_) { aplicarFiltro(); return; }
    filtros_->reconstruir();
    atualizarTudo();
}

// Só o que tem R, nunca a seleção (como o REJECT MARKED do INTAKE). Apagar do celular não tem volta: pergunta antes.
void JanelaAndroidUsb::Conteudo::confirmarApagar() {
    if (estado_ != Estado::Pronto || rejeitados_.empty() || overlay_ != nullptr) return;
    std::uint64_t bytes = 0;
    for (const auto& [k, e] : rejeitados_) bytes += e.tamanho;
    const int n = static_cast<int>(rejeitados_.size());
    juce::Component::SafePointer<Conteudo> seguro(this);
    pedirConfirmacao(t("cel.usb.apagar.titulo").replace("{n}", juce::String(n)),
                     t("cel.usb.apagar.msg").replace("{n}", juce::String(n)).replace("{tam}", bytesLegiveis(bytes)).replace("{aparelho}", aparelho_.nome),
                     t("cel.usb.apagar.ok").replace("{n}", juce::String(n)),
                     [seguro] { if (seguro != nullptr) seguro->apagarRejeitados(); });
}

// Confirmação de ação que apaga do celular (overlay do app, tom de perigo; Return não confirma).
void JanelaAndroidUsb::Conteudo::pedirConfirmacao(const juce::String& titulo, const juce::String& msg, const juce::String& ok,
                                                  std::function<void()> aoConfirmar) {
    if (overlay_ != nullptr) return;
    overlay_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlay_);
    overlay_->setBounds(getLocalBounds());
    PainelOverlay::Config cfg;
    cfg.tom = PainelOverlay::Tom::Perigo;
    cfg.titulo = titulo;
    cfg.mensagem = msg;
    cfg.botoes.push_back({t("light.ui.common.cancel"), 0, false, true});
    cfg.botoes.push_back({ok, 1, false, false});
    juce::Component::SafePointer<Conteudo> seguro(this);
    overlay_->mostrar(cfg, [seguro, aoConfirmar](PainelOverlay::Resultado r) {
        juce::MessageManager::callAsync([seguro, aoConfirmar, sim = r.botaoId == 1] {
            if (seguro == nullptr) return;
            seguro->overlay_.reset();
            seguro->grabKeyboardFocus();
            if (sim && aoConfirmar) aoConfirmar();
        });
    });
}

void JanelaAndroidUsb::Conteudo::apagarRejeitados() {
    if (estado_ != Estado::Pronto || rejeitados_.empty()) return;
    std::vector<EntradaAndroid> itens;
    for (const auto& [k, e] : rejeitados_) itens.push_back(e);
    estado_ = Estado::Preparando;
    progresso_ = -1.0;
    lblContagem_.setText(t("cel.usb.apagando").replace("{n}", juce::String(static_cast<int>(itens.size()))), juce::dontSendNotification);
    atualizarTudo();
    juce::Component::SafePointer<Conteudo> seguro(this);
    sessao_->apagar(std::move(itens), [seguro](std::vector<std::uint32_t> apagados, int falhas) {
        if (seguro == nullptr) return;
        auto& s = *seguro;
        const std::set<std::uint32_t> ids(apagados.begin(), apagados.end());
        const auto saiu = [&ids](const EntradaAndroid& e) { return ids.count(e.id) > 0; };
        for (auto it = s.rejeitados_.begin(); it != s.rejeitados_.end();) {
            if (saiu(it->second)) {
                s.arquivoNoCache(it->second).getParentDirectory().deleteRecursively();
                it = s.rejeitados_.erase(it);
            } else ++it;
        }
        for (auto& [k, lista] : s.cacheListas_) lista.erase(std::remove_if(lista.begin(), lista.end(), saiu), lista.end());
        s.arquivos_.erase(std::remove_if(s.arquivos_.begin(), s.arquivos_.end(), saiu), s.arquivos_.end());
        for (auto id : ids) { s.miniaturas_.erase(id); s.metadados_.erase(id); }
        s.estado_ = Estado::Pronto;
        s.aviso_ = t("cel.usb.apagados").replace("{n}", juce::String(static_cast<int>(apagados.size())));
        if (falhas > 0) s.aviso_ += sep() + t("cel.usb.apagarFalhas").replace("{n}", juce::String(falhas));
        s.aplicarFiltro();
    });
}

void JanelaAndroidUsb::Conteudo::baixarParaCache(const EntradaAndroid& e, std::function<void(juce::File)> aoTerminar) {
    const juce::File alvo = arquivoNoCache(e);
    if (alvo.existsAsFile() && static_cast<std::uint64_t>(alvo.getSize()) == e.tamanho) { aoTerminar(alvo); return; }
    ItemCopiaAndroid item{e, juce::String(e.id) + "/" + e.nome};
    sessao_->copiar({item}, SessaoAndroidUsb::pastaCache(aparelho_.serial), nullptr,
                    [alvo, aoTerminar](ResultadoCopiaAndroid) { aoTerminar(alvo); });
}

void JanelaAndroidUsb::Conteudo::abrirPreview(int i) {
    if (estado_ != Estado::Pronto || itemVisivel(i) == nullptr) return;
    selecionar(i, false, false);
    if (janelaPreview_ == nullptr) janelaPreview_ = std::make_unique<JanelaPreview>(*this);
    janelaPreview_->preview().mostrar(i);
    janelaPreview_->toFront(true);
}

void JanelaAndroidUsb::Conteudo::lerMetadadosDoSelecionado() {
    const auto* e = itemVisivel(cursor_);
    if (e == nullptr || metadados_[e->id].lido) return;
    // Fotos (até 60 MB) vêm para o cache só para ler EXIF/GEO; vídeo e áudio só se já estiverem lá (preview).
    const bool foto = ehImagem(categoriaDe(*e)) && e->tamanho <= 60ull * 1024 * 1024;
    const juce::File noCache = arquivoNoCache(*e);
    const bool jaBaixado = noCache.existsAsFile() && static_cast<std::uint64_t>(noCache.getSize()) == e->tamanho;
    if (!foto && !jaBaixado) return;
    const auto id = e->id;
    juce::Component::SafePointer<Conteudo> seguro(this);
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

void JanelaAndroidUsb::Conteudo::abrirMenuGrade() {
    if (estado_ != Estado::Pronto || selecao_.empty()) return;
    bool todos = true;
    for (int i : selecao_) if (const auto* e = itemVisivel(i); e != nullptr && !estaMarcado(*e)) { todos = false; break; }
    juce::PopupMenu menu;
    menu.addItem(kMenuMarcar, todos ? t("cel.usb.menu.desmarcar") : t("cel.usb.menu.marcar"));
    bool todosR = true;
    for (int i : selecao_) if (const auto* e = itemVisivel(i); e != nullptr && !estaRejeitado(*e)) { todosR = false; break; }
    menu.addItem(kMenuMarcarR, todosR ? t("cel.usb.menu.desmarcarR") : t("cel.usb.menu.marcarR"));
    menu.addItem(kMenuPreview, t("cel.usb.menu.preview"), selecao_.size() == 1);
    menu.addSeparator();
    menu.addItem(kMenuImportar, t("cel.usb.menu.importarSel").replace("{n}", juce::String(static_cast<int>(selecao_.size()))));
    juce::Component::SafePointer<Conteudo> seguro(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [seguro](int r) {
        if (seguro == nullptr || r == 0) return;
        auto& s = *seguro;
        if (r == kMenuMarcar) s.alternarMarcaDaSelecao();
        else if (r == kMenuMarcarR) s.alternarRDaSelecao();
        else if (r == kMenuPreview) s.abrirPreview(s.cursor_);
        else if (r == kMenuImportar) {
            for (int i : s.selecao_) if (const auto* e = s.itemVisivel(i)) s.marcados_[chaveDe(*e)] = *e;
            s.importar();
        }
    });
}

void JanelaAndroidUsb::Conteudo::abrirMenuPasta(const EntradaAndroid& pasta) {
    if (estado_ != Estado::Pronto) return;
    juce::PopupMenu menu;
    menu.addItem(kMenuMarcarPasta, estaMarcado(pasta) ? t("cel.usb.menu.desmarcarPasta") : t("cel.usb.menu.marcarPasta"));
    menu.addItem(kMenuImportarPasta, t("cel.usb.menu.importarPasta"));
    juce::Component::SafePointer<Conteudo> seguro(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [seguro, pasta](int r) {
        if (seguro == nullptr || r == 0) return;
        if (r == kMenuMarcarPasta) seguro->alternarMarca({pasta});
        else if (r == kMenuImportarPasta) { seguro->marcados_[chaveDe(pasta)] = pasta; seguro->importar(); }
    });
}

void JanelaAndroidUsb::Conteudo::importar() {
    if (estado_ != Estado::Pronto || marcados_.empty()) return;
    std::vector<EntradaAndroid> pastas;
    std::vector<ItemCopiaAndroid> itens;
    for (const auto& [k, e] : marcados_) {
        if (e.pasta) pastas.push_back(e);
        else itens.push_back({e, e.nome});
    }
    if (pastas.empty()) { copiarParaIntake(std::move(itens)); return; }
    // Pastas marcadas: tudo dentro delas, mantendo as subpastas (pode levar um tempo numa pasta grande).
    estado_ = Estado::Preparando;
    progresso_ = -1.0;
    atualizarTudo();
    juce::Component::SafePointer<Conteudo> seguro(this);
    sessao_->expandir(std::move(pastas),
        [seguro](int arquivos, juce::String pasta) {
            if (seguro == nullptr || seguro->estado_ != Estado::Preparando) return;
            seguro->lblContagem_.setText(t("cel.usb.preparandoN").replace("{n}", juce::String(arquivos)).replace("{pasta}", pasta)
                                             .replace("\n", sep()), juce::dontSendNotification);
        },
        [seguro, itens](bool ok, std::vector<ItemCopiaAndroid> dasPastas, juce::String erro) mutable {
            if (seguro == nullptr) return;
            auto& s = *seguro;
            if (!ok) {
                s.estado_ = erro == t("cel.usb.cancelado") ? Estado::Pronto : Estado::Erro;
                s.erro_ = erro;
                s.atualizarTudo();
                return;
            }
            std::set<Chave> ja;
            for (const auto& it : itens) ja.insert(chaveDe(it.entrada));
            for (auto& it : dasPastas) if (ja.insert(chaveDe(it.entrada)).second) itens.push_back(std::move(it));
            s.estado_ = Estado::Pronto;
            s.copiarParaIntake(std::move(itens));
        });
}

void JanelaAndroidUsb::Conteudo::copiarParaIntake(std::vector<ItemCopiaAndroid> itens, bool confirmado) {
    // Só o que é novo: o que o projeto já tem (nome + tamanho) não é copiado de novo.
    std::vector<ItemCopiaAndroid> novos;
    for (auto& it : itens) if (!jaNoProjeto(it.entrada)) novos.push_back(std::move(it));
    if (novos.empty()) { aviso_ = t("cel.usb.nadaNovo"); atualizarTudo(); return; }
    if (apagarAposImportar_ && !confirmado) {  // "mover": pergunta antes, porque apaga do celular
        const int n = static_cast<int>(novos.size());
        juce::Component::SafePointer<Conteudo> seguro(this);
        pedirConfirmacao(t("cel.usb.mover.titulo").replace("{n}", juce::String(n)),
                         t("cel.usb.mover.msg").replace("{n}", juce::String(n)).replace("{aparelho}", aparelho_.nome),
                         t("cel.usb.mover.ok").replace("{n}", juce::String(n)),
                         [seguro, novos] { if (seguro != nullptr) seguro->copiarParaIntake(novos, true); });
        return;
    }
    const auto copiados = novos;  // para conferir no fim o que chegou inteiro (só isso sai do celular)
    janelaPreview_.reset();
    estado_ = Estado::Copiando;
    progresso_ = 0.0;
    atualizarTudo();
    const juce::File destino = pastaRaizRecebidos().getChildFile(nomePastaSessao(juce::Time::getCurrentTime()) + " USB");
    juce::Component::SafePointer<Conteudo> seguro(this);
    sessao_->copiar(std::move(novos), destino,
        [seguro](ProgressoCopiaAndroid p) {
            if (seguro == nullptr || seguro->estado_ != Estado::Copiando) return;
            seguro->progresso_ = p.bytesTotal > 0 ? juce::jlimit(0.0, 1.0, static_cast<double>(p.bytesFeitos) / static_cast<double>(p.bytesTotal)) : 0.0;
            seguro->lblContagem_.setText(t("light.cel.xdey").replace("{x}", juce::String(p.feitos)).replace("{y}", juce::String(p.total)) + sep() +
                                             bytesLegiveis(p.bytesFeitos) + " / " + bytesLegiveis(p.bytesTotal) + sep() + p.arquivoAtual,
                                         juce::dontSendNotification);
        },
        [seguro, copiados](ResultadoCopiaAndroid r) {
            if (seguro == nullptr) return;
            auto& s = *seguro;
            const int recebidos = r.copiados + r.jaExistiam;
            if (r.erro.isNotEmpty() || (recebidos == 0 && !r.cancelado)) {
                s.estado_ = Estado::Erro;
                s.erro_ = r.erro.isNotEmpty() ? r.erro : t("cel.usb.err.copiar");
                s.atualizarTudo();
                return;
            }
            if (r.cancelado && recebidos == 0) { s.estado_ = Estado::Pronto; s.atualizarTudo(); return; }
            // cancelado no meio: o que já chegou entra no Intake mesmo assim
            if (!s.apagarAposImportar_) {
                s.terminou_ = true;
                if (s.aoImportar_) s.aoImportar_(r.pasta, recebidos);
                return;
            }
            // "Mover": só sai do celular o que está no Mac com o tamanho exato (mesmo caminho que a cópia gravou).
            std::vector<EntradaAndroid> conferidos;
            for (const auto& it : copiados) {
                juce::File alvo = r.pasta;
                for (const auto& parte : juce::StringArray::fromTokens(it.caminhoRelativo, "/", ""))
                    if (parte.isNotEmpty()) alvo = alvo.getChildFile(matriz::nomes_seguros::sanitizarComponente(parte));
                if (alvo.existsAsFile() && static_cast<std::uint64_t>(alvo.getSize()) == it.entrada.tamanho) conferidos.push_back(it.entrada);
            }
            s.estado_ = Estado::Preparando;
            s.lblContagem_.setText(t("cel.usb.apagando").replace("{n}", juce::String(static_cast<int>(conferidos.size()))), juce::dontSendNotification);
            const juce::File pasta = r.pasta;
            s.sessao_->apagar(std::move(conferidos), [seguro, pasta, recebidos](std::vector<std::uint32_t>, int) {
                if (seguro == nullptr) return;
                seguro->terminou_ = true;
                if (seguro->aoImportar_) seguro->aoImportar_(pasta, recebidos);
            });
        });
}

void JanelaAndroidUsb::Conteudo::atualizarTudo() {
    const auto& tk = tema();
    const bool pronto = estado_ == Estado::Pronto;
    const bool ocupado = estado_ == Estado::Copiando || estado_ == Estado::Preparando;

    juce::String titulo = aparelho_.nome.isNotEmpty() ? aparelho_.nome : t("cel.usb.titulo");
    lblTitulo_.setText(titulo, juce::dontSendNotification);

    if (!ocupado) {
        juce::String trilha;
        for (size_t i = 0; i < caminho_.size(); ++i) trilha += (i == 0 ? juce::String() : juce::String(" / ")) + caminho_[i].nome;
        juce::String linha = trilha.isNotEmpty() ? trilha + sep() : juce::String();
        linha += t("janela_pasta.contagem").replace("{n}", juce::String(static_cast<int>(visiveis_.size())));
        if (selecao_.size() > 0) linha += sep() + t("janela_pasta.selecionados").replace("{n}", juce::String(static_cast<int>(selecao_.size())));
        std::uint64_t bytes = 0;
        for (const auto& [k, e] : marcados_) bytes += e.tamanho;
        if (!marcados_.empty()) linha += sep() + t("cel.usb.marcados").replace("{n}", juce::String(static_cast<int>(marcados_.size()))) +
                                         (bytes > 0 ? " (" + bytesLegiveis(bytes) + ")" : juce::String());
        if (!rejeitados_.empty()) linha += sep() + t("cel.usb.rejeitadosN").replace("{n}", juce::String(static_cast<int>(rejeitados_.size())));
        if (aviso_.isNotEmpty()) { linha += sep() + aviso_; aviso_.clear(); }
        lblContagem_.setText(linha, juce::dontSendNotification);
    } else if (estado_ == Estado::Preparando && lblContagem_.getText().isEmpty()) {
        lblContagem_.setText(t("cel.usb.preparando"), juce::dontSendNotification);
    }

    juce::String status;
    if (estado_ == Estado::Conectando) status = t("cel.usb.conectando");
    else if (estado_ == Estado::Erro) status = erro_;
    else if (pronto && listando_) status = t("cel.usb.lendo");
    else if (pronto && arquivos_.empty()) status = t("cel.usb.soPastas");
    else if (pronto && visiveis_.empty()) status = t("cel.usb.nadaNoFiltro");
    lblStatus_.setText(status, juce::dontSendNotification);
    lblStatus_.setColour(juce::Label::textColourId, estado_ == Estado::Erro ? tk.perigo : tk.textoSecundario);
    lblStatus_.setVisible(status.isNotEmpty());
    btnTentar_->setVisible(estado_ == Estado::Erro);

    const auto* e = itemVisivel(cursor_);
    const Metadados* m = nullptr;
    if (e != nullptr && selecao_.size() <= 1)
        if (auto it = metadados_.find(e->id); it != metadados_.end() && it->second.lido) m = &it->second;
    btnMapa_->setVisible(m != nullptr && m->lat.has_value() && m->lon.has_value());

    barra_.setVisible(ocupado);
    btnVoltar_.setEnabled(pronto && (!historico_.empty() || caminho_.size() > 1));
    btnImportar_->setEnabled(ocupado || (pronto && !marcados_.empty()));
    btnRejeitar_->setVisible(!rejeitados_.empty());
    btnRejeitar_->setEnabled(pronto && !rejeitados_.empty());
    btnRejeitar_->setButtonText(t("cel.usb.rejeitar") + " (" + juce::String(static_cast<int>(rejeitados_.size())) + ")");
    btnImportar_->setButtonText(ocupado ? t("cel.usb.parar")
                                        : (marcados_.empty() ? t("cel.usb.importar")
                                                             : t("cel.usb.importarN").replace("{n}", juce::String(static_cast<int>(marcados_.size())))));
    grade_->setSize(juce::jmax(10, viewportGrade_.getMaximumVisibleWidth()), juce::jmax(viewportGrade_.getHeight(), grade_->alturaTotal()));
    resized();
    grade_->repaint();
    repaint();
}

bool JanelaAndroidUsb::Conteudo::tratarTecla(const juce::KeyPress& k) {
    if (overlay_ != nullptr) return false;  // a confirmação trata as próprias teclas
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    if (k == juce::KeyPress::escapeKey) {
        if (estado_ == Estado::Copiando || estado_ == Estado::Preparando) sessao_->cancelar();
        else fechar();
        return true;
    }
    if (estado_ != Estado::Pronto) return false;
    if ((k.getTextCharacter() == 'i' || k.getTextCharacter() == 'I') && !mods.isCommandDown()) { alternarMarcaDaSelecao(); return true; }
    if ((k.getTextCharacter() == 'r' || k.getTextCharacter() == 'R') && !mods.isCommandDown()) { alternarRDaSelecao(); return true; }
    if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) { if (cursor_ >= 0) abrirPreview(cursor_); return true; }
    if (code == 'A' && mods.isCommandDown()) {
        focoNaArvore_ = false;
        selecao_.clear();
        for (int i = 0; i < static_cast<int>(visiveis_.size()); ++i) selecao_.insert(i);
        if (cursor_ < 0 && !visiveis_.empty()) cursor_ = 0;
        atualizarTudo();
        return true;
    }
    if (k == juce::KeyPress::backspaceKey || (code == juce::KeyPress::upKey && mods.isCommandDown())) { voltar(); return true; }
    const bool shift = mods.isShiftDown();
    if (code == juce::KeyPress::leftKey) { moverCursor(-1, shift); return true; }
    if (code == juce::KeyPress::rightKey) { moverCursor(1, shift); return true; }
    if (code == juce::KeyPress::upKey) { moverCursor(-grade_->colunas(), shift); return true; }
    if (code == juce::KeyPress::downKey) { moverCursor(grade_->colunas(), shift); return true; }
    return false;
}

void JanelaAndroidUsb::Conteudo::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);
    g.setColour(tk.borda);
    g.fillRect(kLarguraFiltros, kAlturaCabecalho, 1, getHeight() - kAlturaCabecalho);
    g.fillRect(0, kAlturaCabecalho - 1, getWidth(), 1);
    g.fillRect(rcFicha_.getX() - 1, kAlturaCabecalho, 1, getHeight() - kAlturaCabecalho);
    g.setColour(tk.textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteMicro, juce::Font::bold)));
    g.drawText(t("cel.usb.pastas"), rotuloArvore_, juce::Justification::centredLeft, false);

    // Ficha do arquivo selecionado (como a ficha compacta da janela de pasta: nome, formato, tamanho, datas, grade).
    g.setColour(tk.painel);
    g.fillRect(rcFicha_);
    const auto* e = itemVisivel(cursor_);
    if (e == nullptr || selecao_.size() > 1) {
        g.setColour(tk.textoTerciario);
        g.setFont(fonteUi(tk.tamanhoFonteCorpo));
        g.drawFittedText(selecao_.size() > 1 ? t("cel.usb.nSelecionados").replace("{n}", juce::String(static_cast<int>(selecao_.size())))
                                             : t("cel.usb.semSelecao"),
                         rcFicha_.reduced(20), juce::Justification::centred, 4);
        return;
    }
    g.setColour(tk.painelAlt);
    g.fillRoundedRectangle(rcFichaImagem_.toFloat(), 10.0f);
    if (auto it = miniaturas_.find(e->id); it != miniaturas_.end() && it->second.isValid())
        g.drawImage(it->second, rcFichaImagem_.toFloat().reduced(4), juce::RectanglePlacement::centred);

    auto r = rcFichaTexto_;
    const auto ext = e->nome.fromLastOccurrenceOf(".", false, false).toUpperCase();
    auto selo = r.removeFromTop(20);
    g.setFont(fonteUi(tk.tamanhoFonteMicro, juce::Font::bold));
    const int ls = juce::jmax(40, juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), ext) + 14);
    auto rs = selo.removeFromLeft(ls).toFloat();
    g.setColour(tk.acento);
    g.fillRoundedRectangle(rs, 4.0f);
    g.setColour(tk.textoSobreAcento);
    g.drawText(ext, rs, juce::Justification::centred);
    if (estaMarcado(*e)) desenharSeloI(g, selo.removeFromLeft(28).toFloat().withSizeKeepingCentre(18, 18).translated(6, 0));
    if (estaRejeitado(*e)) desenharSeloR(g, selo.removeFromLeft(28).toFloat().withSizeKeepingCentre(18, 18).translated(6, 0));
    r.removeFromTop(6);
    g.setColour(tk.textoPrimario);
    g.setFont(fonteUi(tk.fonteCorpoGrande, juce::Font::bold));
    g.drawFittedText(e->nome, r.removeFromTop(40), juce::Justification::topLeft, 2);
    g.setColour(tk.textoSecundario);
    g.setFont(fonteUi(tk.tamanhoFonteCorpo));
    g.drawText(bytesLegiveis(e->tamanho), r.removeFromTop(20), juce::Justification::centredLeft);
    juce::String trilha;
    for (const auto& n : caminho_) trilha += "/" + n.nome;
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), tk.tamanhoFontePequena, juce::Font::plain)));
    g.drawFittedText(trilha, r.removeFromTop(32), juce::Justification::topLeft, 2);
    r.removeFromTop(8);

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
    if (e->modificado.toMilliseconds() > 0) linha(t("cel.usb.m.modificado"), e->modificado.formatted("%Y-%m-%d %H:%M"));
    if (auto mt = metadados_.find(e->id); mt != metadados_.end() && mt->second.lido) {
        const auto& m = mt->second;
        if (m.dataOriginal.isNotEmpty()) linha(t("cel.usb.m.capturado"), m.dataOriginal.replaceCharacter('T', ' ').substring(0, 19));
        if (m.largura && m.altura) linha(t("cel.usb.m.dimensoes"), juce::String(*m.largura) + " x " + juce::String(*m.altura) + " px");
        if (m.duracao && *m.duracao > 0) {
            const int s = static_cast<int>(*m.duracao);
            linha(t("cel.usb.m.duracao"), juce::String::formatted("%d:%02d", s / 60, s % 60));
        }
        linha(t("cel.usb.m.camera"), m.camera);
        linha(t("cel.usb.m.geo"), m.lat && m.lon ? juce::String(*m.lat, 5) + ", " + juce::String(*m.lon, 5) : t("cel.usb.m.semGeo"));
    } else if (ehImagem(categoriaDe(*e))) {
        linha(t("cel.usb.m.geo"), t("cel.usb.m.lendo"));
    }
    if (jaNoProjeto(*e)) linha(t("cel.usb.m.projeto"), t("cel.usb.noProjeto"));
}

void JanelaAndroidUsb::Conteudo::resized() {
    auto area = getLocalBounds();
    auto cab = area.removeFromTop(kAlturaCabecalho).reduced(12, 6);
    btnVoltar_.setBounds(cab.removeFromLeft(86));
    cab.removeFromLeft(10);
    btnImportar_->setBounds(cab.removeFromRight(juce::jmax(240, btnImportar_->larguraIdeal() + 24)));
    cab.removeFromRight(12);
    if (btnRejeitar_->isVisible()) {
        btnRejeitar_->setBounds(cab.removeFromRight(juce::jmax(200, btnRejeitar_->larguraIdeal() + 24)));
        cab.removeFromRight(12);
    }
    campoBusca_.setBounds(cab.removeFromRight(juce::jmin(300, cab.getWidth() / 3)));
    cab.removeFromRight(12);
    lblTitulo_.setBounds(cab);

    auto esquerda = area.removeFromLeft(kLarguraFiltros);
    area.removeFromLeft(1);
    const int alturaFiltros = juce::jmin(esquerda.getHeight() / 2, juce::jmax(120, filtros_->getHeight()));
    viewportFiltros_.setBounds(esquerda.removeFromTop(alturaFiltros));
    filtros_->setSize(viewportFiltros_.getMaximumVisibleWidth(), filtros_->getHeight());
    rotuloArvore_ = esquerda.removeFromTop(22).reduced(10, 0);
    arvore_.setBounds(esquerda);

    rcFicha_ = area.removeFromRight(kLarguraFicha);
    area.removeFromRight(1);
    {
        auto d = rcFicha_.reduced(16);
        rcFichaImagem_ = d.removeFromTop(juce::jmin(d.getWidth(), 240));
        d.removeFromTop(12);
        btnMapa_->setBounds(d.removeFromBottom(36).withWidth(juce::jmin(d.getWidth(), 180)));
        d.removeFromBottom(8);
        rcFichaTexto_ = d;
    }

    auto topo = area.removeFromTop(30).reduced(12, 4);
    sliderTamanho_.setBounds(topo.removeFromRight(140));
    topo.removeFromRight(10);
    if (barra_.isVisible()) {
        barra_.setBounds(topo.removeFromRight(juce::jmin(260, topo.getWidth() / 3)).reduced(0, 3));
        topo.removeFromRight(10);
    }
    lblContagem_.setBounds(topo);
    viewportGrade_.setBounds(area);
    grade_->setSize(juce::jmax(10, viewportGrade_.getMaximumVisibleWidth()), juce::jmax(viewportGrade_.getHeight(), grade_->alturaTotal()));
    auto msg = area.withSizeKeepingCentre(juce::jmin(560, juce::jmax(100, area.getWidth() - 40)), 140);
    lblStatus_.setBounds(msg.removeFromTop(80));
    btnTentar_->setBounds(msg.removeFromTop(44).withSizeKeepingCentre(220, 44));
    lblStatus_.toFront(false);
    btnTentar_->toFront(false);
    if (overlay_ != nullptr) { overlay_->setBounds(getLocalBounds()); overlay_->toFront(true); }
}

// ── Janela ──────────────────────────────────────────────────────────────────────────────────────

JanelaAndroidUsb::JanelaAndroidUsb(std::function<ConjuntoExistentes()> existentes, std::function<void(juce::File, int)> aoImportar,
                                   std::function<void()> aoFechar)
    : juce::DocumentWindow("ANDROID USB", moldura::paleta().fundo, juce::DocumentWindow::closeButton) {
    setUsingNativeTitleBar(false);
    setTitleBarHeight(0);
    setResizable(true, false);
    auto* conteudo = new Conteudo(std::move(existentes), std::move(aoImportar), std::move(aoFechar));
    conteudo_ = conteudo;
    setContentOwned(new moldura::JanelaMoldura(conteudo, moldura::Secao::Explorador, "Phone Dump", t("cel.usb.titulo")), true);
    setResizeLimits(900, 560, 4000, 3000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    toFront(true);
    conteudo->grabKeyboardFocus();
}

JanelaAndroidUsb::~JanelaAndroidUsb() { clearContentComponent(); }

void JanelaAndroidUsb::closeButtonPressed() {
    juce::Component::SafePointer<Conteudo> seguro(conteudo_);
    juce::MessageManager::callAsync([seguro] { if (seguro != nullptr) seguro->fechar(); });
}

bool JanelaAndroidUsb::keyPressed(const juce::KeyPress& key) {
    if (conteudo_ != nullptr && conteudo_->tratarTecla(key)) return true;
    return DocumentWindow::keyPressed(key);
}

} // namespace matriz::ui
