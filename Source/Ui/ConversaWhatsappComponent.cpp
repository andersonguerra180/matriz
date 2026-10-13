#include "ConversaWhatsappComponent.h"

#include <algorithm>
#include <cmath>

#include "../App/AreaTransferenciaImagem.h"
#include "../App/Preferencias.h"
#include "../Audio/DispositivoAudioApp.h"
#include "../Audio/FormatoAudioQuickTime.h"
#include "../I18n/Strings.h"
#include "../ProvaData/ProvaData.h"
#include "../Whatsapp/WhatsappConversa.h"
#include "ProgressoGlobal.h"
#include "Tokens.h"

namespace matriz::ui {

using matriz::i18n::t;
using namespace matriz::whatsapp;

namespace {

constexpr float kMargemLado = 14.0f;
constexpr float kPadH = 11.0f, kPadV = 7.0f;
constexpr float kLinhaH = 19.0f, kNomeH = 18.0f, kHoraH = 15.0f;
constexpr float kFotoW = 272.0f, kFotoH = 204.0f, kSticker = 140.0f;
constexpr float kAudioW = 276.0f, kAudioH = 46.0f, kDocW = 290.0f, kDocH = 54.0f, kLigW = 252.0f;
constexpr float kRaio = 11.0f;
constexpr float kAlturaDia = 48.0f;
constexpr int kAlturaCabecalho = 64, kAlturaBusca = 48;

juce::Colour corDoAutor(const juce::String& autor) {
    static const juce::uint32 paleta[] = {0xff25d366, 0xff38bdf8, 0xfff59e0b, 0xffa78bfa, 0xfff472b6, 0xff34d399, 0xfffb923c, 0xff60a5fa};
    return juce::Colour(paleta[static_cast<size_t>(std::abs(autor.toLowerCase().hashCode())) % 8]);
}

juce::String iniciais(const juce::String& nome) {
    juce::StringArray p;
    p.addTokens(nome.trim(), " ", "");
    p.removeEmptyStrings();
    juce::String r;
    for (int i = 0; i < p.size() && r.length() < 2; ++i) r += p[i].substring(0, 1).toUpperCase();
    return r.isEmpty() ? "?" : r;
}

juce::String horaDe(const Mensagem& m) { return juce::String::formatted("%02d:%02d", m.hora, m.minuto); }

juce::String duracaoTexto(double s) {
    const int total = static_cast<int>(s + 0.5);
    return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}

float larguraTxt(const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth(f, s); }

juce::String paraJuce(const std::u32string& t, size_t a, size_t b) {
    juce::String s;
    for (size_t i = a; i < b && i < t.size(); ++i) s += juce::String::charToString(static_cast<juce::juce_wchar>(t[i]));
    return s;
}

std::u32string paraU32(const juce::String& s) {
    std::u32string out;
    for (auto p = s.getCharPointer(); !p.isEmpty();) out.push_back(static_cast<char32_t>(p.getAndAdvance()));
    return out;
}

struct Quebra {
    std::vector<std::pair<int, int>> linhas;
    float maior = 0;
};

// Quebra de linha por palavras, medindo cada trecho com a fonte do seu estilo (negrito/itálico).
Quebra quebrarTexto(const std::u32string& t, const std::vector<uint8_t>& est, float maxW, const juce::Font& base) {
    Quebra q;
    juce::Font fontes[4] = {base, base.boldened(), base.italicised(), base.boldened().italicised()};
    const auto estiloEm = [&](size_t i) -> int { return i < est.size() ? (est[i] & 3) : 0; };
    const auto largura = [&](size_t a, size_t b) -> float {
        float w = 0;
        size_t i = a;
        while (i < b) {
            size_t j = i + 1;
            const int e = estiloEm(i);
            while (j < b && estiloEm(j) == e) ++j;
            w += larguraTxt(fontes[e], paraJuce(t, i, j));
            i = j;
        }
        return w;
    };
    const auto ehEspaco = [&](size_t i) { return t[i] == U' ' || t[i] == U'\t'; };
    const size_t n = t.size();
    size_t pos = 0;
    while (true) {
        size_t pe = pos;
        while (pe < n && t[pe] != U'\n') ++pe;
        size_t iniLinha = pos, i = pos;
        float cur = 0;
        if (pe == pos) q.linhas.emplace_back(static_cast<int>(pos), static_cast<int>(pos));
        while (i < pe) {
            size_t j = i;
            while (j < pe && !ehEspaco(j)) ++j;
            const size_t fimPalavra = j;
            while (j < pe && ehEspaco(j)) ++j;
            const float wPalavra = largura(i, fimPalavra);
            const float wComEspaco = largura(i, j);
            if (cur > 0 && cur + wPalavra > maxW) {
                q.maior = juce::jmax(q.maior, cur);
                q.linhas.emplace_back(static_cast<int>(iniLinha), static_cast<int>(i));
                iniLinha = i;
                cur = 0;
            }
            if (wPalavra > maxW) {  // palavra maior que a linha: quebra por caractere
                size_t k = i;
                float w = 0;
                while (k < fimPalavra) {
                    const float wc = largura(k, k + 1);
                    if (w > 0 && w + wc > maxW) {
                        q.maior = juce::jmax(q.maior, w);
                        q.linhas.emplace_back(static_cast<int>(iniLinha), static_cast<int>(k));
                        iniLinha = k;
                        w = 0;
                    }
                    w += wc;
                    ++k;
                }
                cur = w + (wComEspaco - wPalavra);
            } else {
                cur += wComEspaco;
            }
            i = j;
        }
        if (pe > pos) {
            q.maior = juce::jmax(q.maior, juce::jmin(cur, maxW));
            q.linhas.emplace_back(static_cast<int>(iniLinha), static_cast<int>(pe));
        }
        if (pe >= n) break;
        pos = pe + 1;
    }
    if (q.linhas.empty()) q.linhas.emplace_back(0, 0);
    return q;
}

// ── Reprodutor do balão ─────────────────────────────────────────────────────────────────────
// Toca o FLAC dentro do balão, um por vez. Só CONSOME o dispositivo de áudio do app (não mexe no motor de reprodução).
class ReprodutorBalao : private juce::Timer {
public:
    std::function<void()> aoMudar;

    ReprodutorBalao() { matriz::audio::registrarFormatosDeAudio(formatos_); }
    ~ReprodutorBalao() override {
        stopTimer();
        parar();
        desanexar();
        player_.setSource(nullptr);
        transport_.setSource(nullptr);
    }

    const std::string& atual() const { return atual_; }
    bool tocando(const std::string& id) const { return id == atual_ && transport_.isPlaying(); }
    double posicao() const { return transport_.getCurrentPosition(); }

    void alternar(const std::string& id, const juce::File& arquivo) {
        if (id == atual_ && transport_.isPlaying()) {
            parar();
            return;
        }
        parar();
        if (!arquivo.existsAsFile() || arquivo.getSize() > 96LL * 1024 * 1024) return;
        auto bloco = std::make_unique<juce::MemoryBlock>();
        if (!arquivo.loadFileAsData(*bloco)) return;
        auto leitor = std::unique_ptr<juce::AudioFormatReader>(
            formatos_.createReaderFor(std::make_unique<juce::MemoryInputStream>(bloco->getData(), bloco->getSize(), false)));
        if (leitor == nullptr) return;
        const double taxa = leitor->sampleRate;
        transport_.setSource(nullptr);
        fonte_.reset();
        dados_ = std::move(bloco);
        fonte_ = std::make_unique<juce::AudioFormatReaderSource>(leitor.release(), true);
        transport_.setSource(fonte_.get(), 0, nullptr, taxa);
        player_.setSource(&transport_);
        anexar();
        atual_ = id;
        transport_.setPosition(0.0);
        transport_.start();
        startTimerHz(30);
        if (aoMudar) aoMudar();
    }

    void parar() {
        stopTimer();
        transport_.stop();
        if (fonte_ != nullptr) transport_.setPosition(0.0);
        const bool tinha = !atual_.empty();
        atual_.clear();
        if (tinha && aoMudar) aoMudar();
    }

private:
    void anexar() {
        if (anexado_) return;
        matriz::audio::garantirDispositivoAudioDoApp();
        matriz::audio::dispositivoAudioDoApp().addAudioCallback(&player_);
        anexado_ = true;
    }
    void desanexar() {
        if (!anexado_) return;
        matriz::audio::dispositivoAudioDoApp().removeAudioCallback(&player_);
        anexado_ = false;
    }
    void timerCallback() override {
        if (!transport_.isPlaying() || transport_.hasStreamFinished()) {
            parar();
            return;
        }
        if (aoMudar) aoMudar();
    }

    juce::AudioFormatManager formatos_;
    juce::AudioTransportSource transport_;
    juce::AudioSourcePlayer player_;
    std::unique_ptr<juce::MemoryBlock> dados_;
    std::unique_ptr<juce::AudioFormatReaderSource> fonte_;
    std::string atual_;
    bool anexado_ = false;
};

} // namespace

// ── Tipos privados ───────────────────────────────────────────────────────────────────────────

struct ConversaWhatsappComponent::Medida {
    float larg = 0, alt = 0;
    juce::String plain;
    std::vector<uint8_t> estilo;
    std::vector<std::pair<int, int>> linhas;
    float larguraTexto = 0;
    bool temTexto = false;
    bool italico = false;  // textos de sistema/vazio
};

struct ConversaWhatsappComponent::Acerto {
    enum class Tipo { Midia, Play, Album } tipo = Tipo::Midia;
    juce::Rectangle<int> r;
    int iMsg = -1;
    int sub = 0;
};

// Seleção da área do print: escurece, cursor em cruz, arrasta o retângulo; Esc cancela.
class ConversaWhatsappComponent::SobreposicaoPrint : public juce::Component {
public:
    std::function<void(juce::Rectangle<int>)> aoSelecionar;
    std::function<void()> aoCancelar;

    SobreposicaoPrint() {
        setComponentID("wa.print.overlay");
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }
    void paint(juce::Graphics& g) override {
        juce::Path p;
        p.addRectangle(getLocalBounds().toFloat());
        if (arrastando_) p.addRectangle(sel_.toFloat());
        p.setUsingNonZeroWinding(false);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillPath(p);
        if (arrastando_) {
            g.setColour(juce::Colours::white);
            g.drawRect(sel_.expanded(1), 2);
        }
        g.setColour(juce::Colours::white);
        g.setFont(fonteUi(15.0f, juce::Font::bold));
        g.drawText(t("light.wa.print.hint"), getLocalBounds().removeFromTop(48), juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        inicio_ = e.getPosition();
        sel_ = juce::Rectangle<int>(inicio_, inicio_);
        arrastando_ = true;
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const juce::Point<int> q(juce::jlimit(0, getWidth(), e.x), juce::jlimit(0, getHeight(), e.y));
        sel_ = juce::Rectangle<int>::leftTopRightBottom(juce::jmin(inicio_.x, q.x), juce::jmin(inicio_.y, q.y), juce::jmax(inicio_.x, q.x), juce::jmax(inicio_.y, q.y));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (!arrastando_) return;
        arrastando_ = false;
        if (sel_.getWidth() >= 8 && sel_.getHeight() >= 8 && aoSelecionar) aoSelecionar(sel_);
        else repaint();
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress::escapeKey) {
            if (aoCancelar) aoCancelar();
            return true;
        }
        return false;
    }

private:
    bool arrastando_ = false;
    juce::Point<int> inicio_;
    juce::Rectangle<int> sel_;
};

namespace {

// Conteúdo do "Salvar print como…": nome, pasta e a caixa de prova de data.
class ConteudoSalvarPrint : public juce::Component {
public:
    ConteudoSalvarPrint(const juce::String& nome, const juce::File& pasta) : pasta_(pasta) {
        setComponentID("wa.print.salvar");
        const auto& tk = tema();
        lblNome_.setText(t("light.wa.print.fileName"), juce::dontSendNotification);
        lblNome_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(lblNome_);
        campo_.setText(nome, false);
        campo_.setComponentID("wa.print.nome");
        campo_.setColour(juce::TextEditor::backgroundColourId, tk.campoFundo);
        campo_.setColour(juce::TextEditor::textColourId, tk.campoTexto);
        campo_.setColour(juce::TextEditor::outlineColourId, tk.borda);
        addAndMakeVisible(campo_);
        lblPasta_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblPasta_);
        escolher_.setButtonText(t("light.wa.print.choose"));
        escolher_.onClick = [this] {
            seletor_ = std::make_shared<juce::FileChooser>(t("light.wa.print.folder"), pasta_);
            seletor_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (f.isDirectory()) { pasta_ = f; atualizarPasta(); }
            });
        };
        addAndMakeVisible(escolher_);
        registrar_.setButtonText(t("light.wa.print.register"));
        registrar_.setComponentID("wa.print.registrar");
        registrar_.setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        addAndMakeVisible(registrar_);
        atualizarPasta();
    }
    void resized() override {
        auto r = getLocalBounds();
        lblNome_.setBounds(r.removeFromTop(24));
        campo_.setBounds(r.removeFromTop(38));
        r.removeFromTop(12);
        auto linha = r.removeFromTop(34);
        escolher_.setBounds(linha.removeFromRight(110));
        linha.removeFromRight(8);
        lblPasta_.setBounds(linha);
        r.removeFromTop(12);
        registrar_.setBounds(r.removeFromTop(30));
    }
    juce::String nome() const { return campo_.getText(); }
    juce::File pasta() const { return pasta_; }
    bool registrar() const { return registrar_.getToggleState(); }

private:
    void atualizarPasta() { lblPasta_.setText(t("light.wa.print.folder") + " " + pasta_.getFullPathName(), juce::dontSendNotification); }
    juce::File pasta_;
    juce::Label lblNome_, lblPasta_;
    juce::TextEditor campo_;
    juce::TextButton escolher_;
    juce::ToggleButton registrar_;
    std::shared_ptr<juce::FileChooser> seletor_;
};

// Conteúdo das opções do "Exportar PDF".
class ConteudoOpcoesTrecho : public juce::Component {
public:
    ConteudoOpcoesTrecho(bool temDadosDoComputador, const juce::File& pasta) : pasta_(pasta) {
        setComponentID("wa.trecho.opcoes");
        const auto& tk = tema();
        const auto cfg = [&](juce::ToggleButton& b, const char* id, const juce::String& texto, bool valor, bool habilitado) {
            b.setButtonText(texto);
            b.setComponentID(id);
            b.setToggleState(valor, juce::dontSendNotification);
            b.setEnabled(habilitado);
            b.setColour(juce::ToggleButton::textColourId, habilitado ? tk.textoPrimario : tk.textoTerciario);
            addAndMakeVisible(b);
        };
        cfg(fotos_, "wa.trecho.fotos", t("light.wa.trecho.opt.photos"), true, true);
        cfg(computador_, "wa.trecho.computador", t("light.wa.trecho.opt.computer"), false, temDadosDoComputador);
        cfg(original_, "wa.trecho.original", t("light.wa.trecho.opt.original"), false, true);
        lblPasta_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblPasta_);
        escolher_.setButtonText(t("light.wa.trecho.opt.choose"));
        escolher_.onClick = [this] {
            seletor_ = std::make_shared<juce::FileChooser>(t("light.wa.trecho.opt.dest"), pasta_);
            seletor_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (f.isDirectory()) { pasta_ = f; atualizarPasta(); }
            });
        };
        addAndMakeVisible(escolher_);
        atualizarPasta();
    }
    void resized() override {
        auto r = getLocalBounds();
        fotos_.setBounds(r.removeFromTop(32));
        computador_.setBounds(r.removeFromTop(32));
        original_.setBounds(r.removeFromTop(32));
        r.removeFromTop(14);
        auto linha = r.removeFromTop(34);
        escolher_.setBounds(linha.removeFromRight(110));
        linha.removeFromRight(8);
        lblPasta_.setBounds(linha);
    }
    matriz::provadata::OpcoesTrecho opcoes() const {
        matriz::provadata::OpcoesTrecho o;
        o.miniaturasDeFotos = fotos_.getToggleState();
        o.incluirDadosDoComputador = computador_.getToggleState() && computador_.isEnabled();
        o.incluirArquivoOriginal = original_.getToggleState();
        o.pastaDestino = pasta_;
        return o;
    }

private:
    void atualizarPasta() { lblPasta_.setText(t("light.wa.trecho.opt.dest") + " " + pasta_.getFullPathName(), juce::dontSendNotification); }
    juce::File pasta_;
    juce::ToggleButton fotos_, computador_, original_;
    juce::Label lblPasta_;
    juce::TextButton escolher_;
    std::shared_ptr<juce::FileChooser> seletor_;
};

// Data digitada (dd/mm/aaaa ou aaaa-mm-dd) → AAAAMMDD; 0 se inválida.
long long chaveDeDataDigitada(const juce::String& texto) {
    const auto t0 = texto.trim();
    juce::StringArray p;
    p.addTokens(t0, "/-.", "");
    p.removeEmptyStrings();
    if (p.size() != 3) return 0;
    int d = p[0].getIntValue(), m = p[1].getIntValue(), a = p[2].getIntValue();
    if (p[0].length() == 4) { a = p[0].getIntValue(); m = p[1].getIntValue(); d = p[2].getIntValue(); }
    if (a < 100) a += 2000;
    if (a < 1900 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    return static_cast<long long>(a) * 10000 + m * 100 + d;
}

} // namespace

struct ConversaWhatsappComponent::Impl {
    juce::ThreadPool poolCarga{1};
    juce::ThreadPool poolMidia{2};
    struct Mini {
        juce::Image img;
        double duracao = 0;
    };
    std::map<std::string, Mini> minis;
    std::set<std::string> pendentesMini;
    std::map<std::string, std::vector<float>> ondas;
    std::map<std::string, double> duracoes;
    std::set<std::string> pendentesOnda;
    std::vector<Acerto> acertos;
    std::map<int, Medida> cacheMedidas;
    ReprodutorBalao reprodutor;
};

// ── Construção ───────────────────────────────────────────────────────────────────────────────

ConversaWhatsappComponent::ConversaWhatsappComponent(ProjetoAberto& projeto, const std::string& conversaItemId)
    : projeto_(projeto), conversaItemId_(conversaItemId), impl_(std::make_unique<Impl>()) {
    setComponentID("wa.root");
    setWantsKeyboardFocus(true);
    portugues_ = matriz::i18n::localeAtivo() == "pt_BR";

    campoBusca_.setComponentID("wa.search");
    campoBusca_.setIndents(14, 0);
    campoBusca_.setJustification(juce::Justification::centredLeft);
    campoBusca_.setTextToShowWhenEmpty(t("light.wa.buscar"), tema().textoTerciario);
    campoBusca_.setEscapeAndReturnKeysConsumed(false);
    campoBusca_.onTextChange = [this] { definirBusca(campoBusca_.getText()); };
    campoBusca_.onReturnKey = [this] {
        if (!resultados_.empty()) irParaResultado((resultadoAtual_ + 1) % static_cast<int>(resultados_.size()));
    };
    addAndMakeVisible(campoBusca_);

    lblContagem_.setComponentID("wa.count");
    lblContagem_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(lblContagem_);

    btnAnterior_ = std::make_unique<BotaoPilula>(juce::String::fromUTF8("\xe2\x96\xb2"), BotaoPilula::Estilo::Redondo);
    btnAnterior_->setComponentID("wa.prev");
    btnAnterior_->setTooltip(t("light.wa.anterior"));
    btnAnterior_->onClick = [this] {
        if (!resultados_.empty())
            irParaResultado((resultadoAtual_ - 1 + static_cast<int>(resultados_.size())) % static_cast<int>(resultados_.size()));
    };
    addAndMakeVisible(*btnAnterior_);
    btnProximo_ = std::make_unique<BotaoPilula>(juce::String::fromUTF8("\xe2\x96\xbc"), BotaoPilula::Estilo::Redondo);
    btnProximo_->setComponentID("wa.next");
    btnProximo_->setTooltip(t("light.wa.proximo"));
    btnProximo_->onClick = [this] {
        if (!resultados_.empty()) irParaResultado((resultadoAtual_ + 1) % static_cast<int>(resultados_.size()));
    };
    addAndMakeVisible(*btnProximo_);

    btnVoce_ = std::make_unique<BotaoPilula>(juce::String(), BotaoPilula::Estilo::Secundario);
    btnVoce_->setComponentID("wa.voce");
    btnVoce_->onClick = [this] { mostrarMenuDeVoce(); };
    btnVoce_->setVisible(false);
    addAndMakeVisible(*btnVoce_);

    btnPrint_ = std::make_unique<BotaoPilula>(t("light.wa.print.button"), BotaoPilula::Estilo::Secundario);
    btnPrint_->setComponentID("wa.print");
    btnPrint_->onClick = [this] { iniciarPrint(); };
    addAndMakeVisible(*btnPrint_);
    btnTrecho_ = std::make_unique<BotaoPilula>(t("light.wa.trecho.button"), BotaoPilula::Estilo::Secundario);
    btnTrecho_->setComponentID("wa.trecho");
    btnTrecho_->onClick = [this] { if (modoSelecao_) sairSelecao(); else entrarSelecao(); };
    addAndMakeVisible(*btnTrecho_);

    // Barra do "Exportar trecho" (só aparece no modo seleção)
    lblSelecionadas_.setComponentID("wa.trecho.count");
    lblDicaSel_.setComponentID("wa.trecho.hint");
    lblDicaSel_.setText(t("light.wa.trecho.hint"), juce::dontSendNotification);
    lblDe_.setText(t("light.wa.trecho.from"), juce::dontSendNotification);
    lblAte_.setText(t("light.wa.trecho.to"), juce::dontSendNotification);
    campoDe_.setComponentID("wa.trecho.de");
    campoAte_.setComponentID("wa.trecho.ate");
    campoDe_.setTextToShowWhenEmpty("dd/mm/aaaa", tema().textoTerciario);
    campoAte_.setTextToShowWhenEmpty("dd/mm/aaaa", tema().textoTerciario);
    btnPeriodo_ = std::make_unique<BotaoPilula>(t("light.wa.trecho.apply"), BotaoPilula::Estilo::Secundario);
    btnPeriodo_->setComponentID("wa.trecho.periodo");
    btnPeriodo_->onClick = [this] { selecionarPeriodo(); };
    btnTodas_ = std::make_unique<BotaoPilula>(t("light.wa.trecho.all"), BotaoPilula::Estilo::Secundario);
    btnTodas_->setComponentID("wa.trecho.todas");
    btnTodas_->onClick = [this] { selecionarTudo(); };
    btnExportar_ = std::make_unique<BotaoPilula>(t("light.wa.trecho.exportPdf"), BotaoPilula::Estilo::Primario);
    btnExportar_->setComponentID("wa.trecho.exportar");
    btnExportar_->onClick = [this] { abrirOpcoesDoTrecho(); };
    btnSairSel_ = std::make_unique<BotaoPilula>(t("light.wa.trecho.exit"), BotaoPilula::Estilo::Fantasma);
    btnSairSel_->setComponentID("wa.trecho.sair");
    btnSairSel_->onClick = [this] { sairSelecao(); };
    for (auto* c : std::initializer_list<juce::Component*>{&lblSelecionadas_, &lblDicaSel_, &lblDe_, &lblAte_, &campoDe_, &campoAte_, btnPeriodo_.get(), btnTodas_.get(), btnExportar_.get(), btnSairSel_.get()})
        addChildComponent(*c);

    barra_.setAutoHide(false);
    barra_.addListener(this);
    addAndMakeVisible(barra_);

    impl_->reprodutor.aoMudar = [this] { repaint(); };
    lookAndFeelChanged();
    iniciarCarga();
}

ConversaWhatsappComponent::~ConversaWhatsappComponent() {
    stopTimer();
    barra_.removeListener(this);
    impl_->poolCarga.removeAllJobs(true, 8000);
    impl_->poolMidia.removeAllJobs(true, 8000);
}

void ConversaWhatsappComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    fonteTexto_ = fonteUi(tk.tamanhoFonteCorpo + 2.0f);
    campoBusca_.setFont(fonteUi(tk.fonteCorpoGrande));
    campoBusca_.setColour(juce::TextEditor::backgroundColourId, tk.campoFundo);
    campoBusca_.setColour(juce::TextEditor::textColourId, tk.campoTexto);
    campoBusca_.setColour(juce::TextEditor::outlineColourId, tk.borda);
    campoBusca_.setTextToShowWhenEmpty(t("light.wa.buscar"), tk.textoTerciario);
    lblContagem_.setFont(fonteUi(tk.tamanhoFontePequena + 1.0f));
    lblContagem_.setColour(juce::Label::textColourId, tk.textoSecundario);
    for (auto* l : {&lblSelecionadas_, &lblDicaSel_, &lblDe_, &lblAte_}) {
        l->setFont(fonteUi(tk.tamanhoFontePequena + 1.0f));
        l->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    lblSelecionadas_.setFont(fonteUi(tk.tamanhoFontePequena + 2.0f, juce::Font::bold));
    lblSelecionadas_.setColour(juce::Label::textColourId, tk.textoPrimario);
    for (auto* ed : {&campoDe_, &campoAte_}) {
        ed->setFont(fonteUi(tk.tamanhoFontePequena + 2.0f));
        ed->setColour(juce::TextEditor::backgroundColourId, tk.campoFundo);
        ed->setColour(juce::TextEditor::textColourId, tk.campoTexto);
        ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
    }
    impl_->cacheMedidas.clear();
    repaint();
}

// ── Carga (background) ───────────────────────────────────────────────────────────────────────

void ConversaWhatsappComponent::iniciarCarga() {
    auto* projeto = &projeto_;
    const std::string id = conversaItemId_;
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);

    impl_->poolCarga.addJob([seguro, projeto, id] {
        auto infoConversa = lerInfoConversa(projeto->projeto().registroLeitura(), id);
        std::shared_ptr<const ConversaParseada> conversa;
        std::vector<Linha> linhas;
        juce::String erro;
        if (!infoConversa) {
            erro = t("light.wa.erroLeitura");
            infoConversa = InfoConversa{};
        } else {
            // o texto vem do conversa.txt guardado no backup (um item do grupo da conversa); nada é gravado de volta
            juce::String txt;
            if (const auto arq = projeto->arquivoPrincipal(infoConversa->txtItemId)) {
                juce::File f(arq->caminhoAbsoluto);
                if (!f.existsAsFile()) {
                    // ainda no staging (antes do backup): o caminho de origem era temporário; vale a cópia dentro do projeto
                    auto st = projeto->projeto().registroLeitura().prepare("SELECT caminho_relativo FROM arquivo WHERE item_id = ? ORDER BY eh_master DESC LIMIT 1");
                    st.bind(1, matriz::db::Value::of(infoConversa->txtItemId));
                    if (st.step()) f = projeto->projeto().pasta().getChildFile(juce::String::fromUTF8(st.columnText(0).c_str()));
                }
                if (f.existsAsFile()) txt = f.loadFileAsString();
            }
            if (txt.isEmpty()) {
                erro = t("light.wa.erroLeitura");
            } else {
                auto parseada = std::make_shared<ConversaParseada>(lerConversa(txt));
                conversa = parseada;
                if (parseada->mensagens.empty()) erro = t("light.wa.erroLeitura");
                const auto mapa = mapaMidiasPorNomeOriginal(*infoConversa);
                const auto& msgs = parseada->mensagens;
                long long diaAtual = -1;
                bool anteriorEhBalao = false;
                juce::String autorAnterior;
                for (size_t i = 0; i < msgs.size(); ++i) {
                    const auto& m = msgs[i];
                    if (m.chaveDia() != diaAtual) {
                        diaAtual = m.chaveDia();
                        Linha d;
                        d.tipo = Tipo::Dia;
                        d.iMsg = static_cast<int>(i);
                        linhas.push_back(d);
                        anteriorEhBalao = false;
                    }
                    Linha l;
                    l.iMsg = static_cast<int>(i);
                    l.meu = infoConversa->voce.isNotEmpty() && m.autor.equalsIgnoreCase(infoConversa->voce);
                    bool balao = true;
                    switch (m.tipo) {
                        case TipoMensagem::Sistema: l.tipo = Tipo::Sistema; balao = false; break;
                        case TipoMensagem::Texto: l.tipo = Tipo::Texto; break;
                        case TipoMensagem::MidiaOculta: l.tipo = Tipo::Oculta; break;
                        case TipoMensagem::SemConteudo: l.tipo = Tipo::Vazia; break;
                        case TipoMensagem::Apagada: l.tipo = Tipo::Apagada; break;
                        case TipoMensagem::LigacaoVoz: l.tipo = Tipo::LigVoz; balao = !m.autor.isEmpty(); break;
                        case TipoMensagem::LigacaoVideo: l.tipo = Tipo::LigVideo; balao = !m.autor.isEmpty(); break;
                        case TipoMensagem::Anexo: {
                            const auto achada = mapa.find(chaveDeNome(m.anexo));
                            const bool presente = achada != mapa.end();
                            l.ausente = !presente;
                            if (!presente) {  // anexo citado e ausente (exportação sem mídia): "Mídia não incluída na exportação"
                                l.tipo = Tipo::Oculta;
                                break;
                            }
                            switch (classeDoAnexo(m.anexo)) {
                                case ClasseAnexo::Foto: l.tipo = Tipo::Foto; break;
                                case ClasseAnexo::Video: l.tipo = Tipo::Video; break;
                                case ClasseAnexo::Audio: l.tipo = Tipo::Audio; break;
                                case ClasseAnexo::Figurinha: l.tipo = Tipo::Figurinha; break;
                                case ClasseAnexo::Documento: l.tipo = Tipo::Documento; break;
                            }
                            // álbum: 3 ou mais fotos/vídeos seguidos, do mesmo autor e no mesmo minuto
                            if (presente && (l.tipo == Tipo::Foto || l.tipo == Tipo::Video) && m.legenda.isEmpty()) {
                                size_t j = i + 1;
                                while (j < msgs.size()) {
                                    const auto& n = msgs[j];
                                    if (n.tipo != TipoMensagem::Anexo || !n.autor.equalsIgnoreCase(m.autor) || n.chaveMinuto() != m.chaveMinuto() ||
                                        !n.legenda.isEmpty())
                                        break;
                                    const auto classe = classeDoAnexo(n.anexo);
                                    if ((classe != ClasseAnexo::Foto && classe != ClasseAnexo::Video) || mapa.find(chaveDeNome(n.anexo)) == mapa.end())
                                        break;
                                    ++j;
                                }
                                if (j - i >= 3) {
                                    l.tipo = Tipo::Album;
                                    l.nMsgs = static_cast<int>(j - i);
                                    i = j - 1;
                                }
                            }
                            break;
                        }
                    }
                    l.inicioSequencia = !(balao && anteriorEhBalao && autorAnterior.equalsIgnoreCase(m.autor));
                    if (balao) {
                        anteriorEhBalao = true;
                        autorAnterior = m.autor;
                    } else {
                        anteriorEhBalao = false;
                    }
                    linhas.push_back(l);
                }
            }
        }
        juce::MessageManager::callAsync([seguro, conversa, info = *infoConversa, linhas = std::move(linhas), erro]() mutable {
            if (seguro == nullptr) return;
            seguro->aoCarregar(conversa, std::move(info), std::move(linhas), erro);
        });
    });
}

void ConversaWhatsappComponent::aoCarregar(std::shared_ptr<const ConversaParseada> conversa, InfoConversa info, std::vector<Linha> linhas,
                                           juce::String erro) {
    conversa_ = std::move(conversa);
    info_ = std::move(info);
    mapaMidias_ = mapaMidiasPorNomeOriginal(info_);
    voce_ = info_.voce;
    perguntarVoce_ = !info_.voceDefinido && info_.autores.size() > 0;
    linhas_ = std::move(linhas);
    erro_ = erro;
    carregada_ = erro.isEmpty() && conversa_ != nullptr;
    mensagemParaLinha_.assign(conversa_ ? conversa_->mensagens.size() : 0, -1);
    for (size_t i = 0; i < linhas_.size(); ++i) {
        const auto& l = linhas_[i];
        if (l.iMsg < 0) continue;
        for (int k = 0; k < l.nMsgs; ++k)
            if (static_cast<size_t>(l.iMsg + k) < mensagemParaLinha_.size() && l.tipo != Tipo::Dia)
                mensagemParaLinha_[static_cast<size_t>(l.iMsg + k)] = static_cast<int>(i);
    }
    larguraMedida_ = 0;
    ys_.clear();
    alturaTotal_ = 0;
    if (perguntarVoce_) montarSeletorDeVoce();
    if (btnVoce_) {
        btnVoce_->setButtonText(t("light.wa.voce").replace("{nome}", voce_.isEmpty() ? juce::String::fromUTF8("\xe2\x80\x94") : voce_));
        btnVoce_->setVisible(carregada_);
    }
    resized();  // mede as linhas para a largura atual
    repaint();
}

float ConversaWhatsappComponent::larguraMaximaBalao() const {
    const float w = static_cast<float>(areaLista_.getWidth()) - 2.0f * kMargemLado - 16.0f;
    return juce::jlimit(240.0f, 580.0f, w * 0.72f);
}

// ── Medição ──────────────────────────────────────────────────────────────────────────────────

ConversaWhatsappComponent::Medida ConversaWhatsappComponent::medirLinha(const Linha& l, float maxBalao) const {
    Medida m;
    if (conversa_ == nullptr) return m;
    const auto& tk = tema();
    const juce::Font base = fonteTexto_;
    const auto& msg = conversa_->mensagens[static_cast<size_t>(juce::jlimit(0, static_cast<int>(conversa_->mensagens.size()) - 1, l.iMsg))];
    const float gap = l.inicioSequencia ? 10.0f : 2.0f;
    const bool mostrarNome = l.inicioSequencia && !l.meu && msg.autor.isNotEmpty();

    const auto layoutTexto = [&](const juce::String& raw, float larguraDisponivel, bool italico) {
        m.temTexto = raw.isNotEmpty();
        m.italico = italico;
        if (raw.isEmpty()) { m.linhas.clear(); return; }
        const bool simples = !raw.containsAnyOf("\n*_<");
        if (simples) {
            const float w = larguraTxt(base, raw);
            if (w <= larguraDisponivel) {
                m.plain = raw;
                m.linhas = {{0, raw.length()}};
                m.larguraTexto = w;
                return;
            }
        }
        std::vector<TrechoFormatado> trechos;
        m.plain = removerMarcacao(raw, trechos);
        const auto u = paraU32(m.plain);
        m.estilo.assign(u.size(), 0);
        for (const auto& tr : trechos)
            for (int i = tr.inicio; i < tr.fim && i < static_cast<int>(m.estilo.size()); ++i)
                m.estilo[static_cast<size_t>(i)] |= static_cast<uint8_t>((tr.negrito ? 1 : 0) | (tr.italico ? 2 : 0));
        const auto q = quebrarTexto(u, m.estilo, larguraDisponivel, base);
        m.linhas = q.linhas;
        m.larguraTexto = q.maior;
    };

    switch (l.tipo) {
        case Tipo::Dia:
            m.alt = kAlturaDia;
            return m;
        case Tipo::Sistema: {
            const float w = juce::jmin(maxBalao * 1.25f, 620.0f) - 28.0f;
            layoutTexto(msg.texto, w, false);
            m.larg = m.larguraTexto + 28.0f;
            m.alt = 8.0f + static_cast<float>(m.linhas.size()) * 17.0f + 14.0f;
            return m;
        }
        case Tipo::Texto: {
            layoutTexto(msg.texto, maxBalao - 2 * kPadH, false);
            m.larg = juce::jmax(m.larguraTexto + 2 * kPadH, 86.0f);
            if (mostrarNome) m.larg = juce::jmax(m.larg, larguraTxt(base, msg.autor) + 2 * kPadH);
            m.alt = gap + kPadV + (mostrarNome ? kNomeH : 0.0f) + static_cast<float>(m.linhas.size()) * kLinhaH + kHoraH + kPadV - 2.0f;
            return m;
        }
        case Tipo::Oculta:
        case Tipo::Vazia:
        case Tipo::Apagada: {
            const juce::String chave = l.tipo == Tipo::Oculta ? "light.wa.midiaOculta" : (l.tipo == Tipo::Vazia ? "light.wa.semConteudo" : "light.wa.apagada");
            layoutTexto(t(chave), maxBalao - 2 * kPadH, true);
            m.larg = juce::jmax(m.larguraTexto + 2 * kPadH, 120.0f);
            float extra = 0;
            if (l.tipo == Tipo::Oculta && msg.legenda.isNotEmpty()) extra = kLinhaH * 1.0f;
            m.alt = gap + kPadV + (mostrarNome ? kNomeH : 0.0f) + static_cast<float>(m.linhas.size()) * kLinhaH + extra + kHoraH + kPadV - 2.0f;
            return m;
        }
        case Tipo::LigVoz:
        case Tipo::LigVideo:
            m.larg = kLigW;
            m.alt = gap + kPadV + (mostrarNome ? kNomeH : 0.0f) + 24.0f + kHoraH + kPadV - 2.0f;
            return m;
        case Tipo::Foto:
        case Tipo::Video: {
            float alt = gap + 4.0f + (mostrarNome ? kNomeH : 0.0f) + kFotoH;
            m.larg = kFotoW + 8.0f;
            if (msg.legenda.isNotEmpty()) {
                layoutTexto(msg.legenda, kFotoW - 2 * 3.0f, false);
                alt += 4.0f + static_cast<float>(m.linhas.size()) * kLinhaH + kHoraH + 2.0f;
            } else {
                alt += 4.0f;
            }
            m.alt = alt;
            return m;
        }
        case Tipo::Album:
            m.larg = kFotoW + 8.0f;
            m.alt = gap + 4.0f + (mostrarNome ? kNomeH : 0.0f) + kFotoW + 4.0f;
            return m;
        case Tipo::Figurinha:
            m.larg = kSticker + 8.0f;
            m.alt = gap + 2.0f + (mostrarNome ? kNomeH : 0.0f) + kSticker + kHoraH;
            return m;
        case Tipo::Audio:
            m.larg = kAudioW;
            m.alt = gap + kPadV + (mostrarNome ? kNomeH : 0.0f) + kAudioH + kHoraH + kPadV - 4.0f;
            return m;
        case Tipo::Documento: {
            m.larg = kDocW;
            float alt = gap + kPadV + (mostrarNome ? kNomeH : 0.0f) + kDocH;
            if (msg.legenda.isNotEmpty()) {
                layoutTexto(msg.legenda, kDocW - 2 * kPadH, false);
                alt += 4.0f + static_cast<float>(m.linhas.size()) * kLinhaH;
            }
            m.alt = alt + kHoraH + kPadV - 4.0f;
            return m;
        }
    }
    return m;
}

const ConversaWhatsappComponent::Medida& ConversaWhatsappComponent::medidaEmCache(int indice) {
    auto& cache = impl_->cacheMedidas;
    auto it = cache.find(indice);
    if (it != cache.end()) return it->second;
    if (cache.size() > 200) cache.clear();
    return cache.emplace(indice, medirLinha(linhas_[static_cast<size_t>(indice)], larguraMaximaBalao())).first->second;
}

void ConversaWhatsappComponent::remedirEmBackground() {
    if (!carregada_ || linhas_.empty()) return;
    const uint64_t geracao = ++geracaoMedida_;
    const float maxBalao = larguraMaximaBalao();
    larguraMedida_ = maxBalao;
    auto copia = linhas_;  // as medidas voltam como alturas novas das mesmas linhas
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    impl_->poolCarga.removeAllJobs(false, 0);
    impl_->poolCarga.addJob([seguro, copia = std::move(copia), maxBalao, geracao, this]() mutable {
        for (auto& l : copia) {
            if (seguro == nullptr) return;
            const auto m = medirLinha(l, maxBalao);
            l.largura = m.larg;
            l.altura = m.alt;
        }
        juce::MessageManager::callAsync([seguro, copia = std::move(copia), geracao]() mutable {
            if (seguro == nullptr) return;
            seguro->aplicarMedidas(geracao, std::move(copia));
        });
    });
}

void ConversaWhatsappComponent::aplicarMedidas(uint64_t geracao, std::vector<Linha> linhas) {
    if (geracao != geracaoMedida_ || linhas.size() != linhas_.size()) return;
    // mantém a linha que o usuário está vendo no topo
    const int topoAntes = linhaEm(rolagem_);
    const float deslocamento = (topoAntes >= 0 && topoAntes < static_cast<int>(ys_.size())) ? rolagem_ - ys_[static_cast<size_t>(topoAntes)] : 0.0f;
    linhas_ = std::move(linhas);
    ys_.resize(linhas_.size());
    float y = 8.0f;
    for (size_t i = 0; i < linhas_.size(); ++i) {
        ys_[i] = y;
        y += linhas_[i].altura;
    }
    alturaTotal_ = y + 12.0f;
    impl_->cacheMedidas.clear();
    if (topoAntes > 0 && topoAntes < static_cast<int>(ys_.size())) rolagem_ = ys_[static_cast<size_t>(topoAntes)] + deslocamento;
    atualizarBarra();
    if (!consulta_.isEmpty()) definirBusca(consulta_);
    repaint();
}

void ConversaWhatsappComponent::resized() {
    auto b = getLocalBounds();
    areaCabecalho_ = b.removeFromTop(kAlturaCabecalho);
    {
        // Cabeçalho, da direita para a esquerda: [Você ▾] [Exportar trecho] [Print]
        auto cab = areaCabecalho_.reduced(10, 12);
        if (btnVoce_) { btnVoce_->setBounds(cab.removeFromRight(juce::jlimit(140, 220, cab.getWidth() / 4))); cab.removeFromRight(8); }
        if (btnTrecho_) { btnTrecho_->setBounds(cab.removeFromRight(juce::jlimit(130, 180, cab.getWidth() / 4))); cab.removeFromRight(8); }
        if (btnPrint_) btnPrint_->setBounds(cab.removeFromRight(juce::jlimit(75, 110, cab.getWidth() / 5)));
    }
    auto busca = b.removeFromTop(kAlturaBusca).reduced(14, 6);
    btnProximo_->setBounds(busca.removeFromRight(36).withSizeKeepingCentre(36, 34));
    busca.removeFromRight(4);
    btnAnterior_->setBounds(busca.removeFromRight(36).withSizeKeepingCentre(36, 34));
    busca.removeFromRight(8);
    lblContagem_.setBounds(busca.removeFromRight(juce::jmin(150, busca.getWidth() / 3)));
    campoBusca_.setBounds(busca);
    if (modoSelecao_) {
        areaBarraSel_ = b.removeFromTop(56);
        auto r = areaBarraSel_.reduced(14, 8);
        btnSairSel_->setBounds(r.removeFromRight(100));
        r.removeFromRight(6);
        btnExportar_->setBounds(r.removeFromRight(130));
        r.removeFromRight(6);
        btnTodas_->setBounds(r.removeFromRight(140));
        r.removeFromRight(12);
        btnPeriodo_->setBounds(r.removeFromRight(150));
        r.removeFromRight(6);
        campoAte_.setBounds(r.removeFromRight(110));
        lblAte_.setBounds(r.removeFromRight(36));
        campoDe_.setBounds(r.removeFromRight(110));
        lblDe_.setBounds(r.removeFromRight(34));
        r.removeFromRight(10);
        lblSelecionadas_.setBounds(r.removeFromTop(r.getHeight() / 2 + 2));
        lblDicaSel_.setBounds(r);
    } else {
        areaBarraSel_ = {};
    }
    areaLista_ = b;
    barra_.setBounds(b.removeFromRight(12));
    areaLista_ = juce::Rectangle<int>(areaLista_.getX(), areaLista_.getY(), areaLista_.getWidth() - 12, areaLista_.getHeight());

    if (perguntarVoce_) {
        const int n = static_cast<int>(botoesVoce_.size());
        const int alturaCartao = 120 + n * 52;
        rcSeletorVoce_ = juce::Rectangle<int>(juce::jmin(460, getWidth() - 40), alturaCartao).withCentre(getLocalBounds().getCentre());
        auto r = rcSeletorVoce_.reduced(24, 0).withTrimmedTop(88).withTrimmedBottom(12);
        for (auto& bt : botoesVoce_) {
            bt->setBounds(r.removeFromTop(44));
            r.removeFromTop(8);
        }
    }
    if (overlay_) overlay_->setBounds(getLocalBounds());
    if (sobreposicaoPrint_) sobreposicaoPrint_->setBounds(getLocalBounds());
    if (carregada_ && std::abs(larguraMaximaBalao() - larguraMedida_) > 0.5f) {
        // debounce: a medição pesada só roda quando o redimensionamento assenta
        remedirPendente_ = true;
        startTimer(120);
    }
    atualizarBarra();
}

void ConversaWhatsappComponent::timerCallback() {
    if (remedirPendente_) {
        remedirPendente_ = false;
        stopTimer();
        remedirEmBackground();
    }
}

void ConversaWhatsappComponent::atualizarBarra() {
    barra_.setRangeLimits(0.0, juce::jmax<double>(alturaTotal_, areaLista_.getHeight()));
    rolagem_ = juce::jlimit(0.0f, juce::jmax(0.0f, alturaTotal_ - static_cast<float>(areaLista_.getHeight())), rolagem_);
    barra_.setCurrentRange(rolagem_, areaLista_.getHeight(), juce::dontSendNotification);
    barra_.setVisible(alturaTotal_ > static_cast<float>(areaLista_.getHeight()));
}

void ConversaWhatsappComponent::scrollBarMoved(juce::ScrollBar*, double novoInicio) {
    rolagem_ = static_cast<float>(novoInicio);
    repaint(areaLista_);
}

void ConversaWhatsappComponent::rolarPara(float y) {
    rolagem_ = juce::jlimit(0.0f, juce::jmax(0.0f, alturaTotal_ - static_cast<float>(areaLista_.getHeight())), y);
    barra_.setCurrentRange(rolagem_, areaLista_.getHeight(), juce::dontSendNotification);
    repaint(areaLista_);
}

int ConversaWhatsappComponent::linhaEm(float y) const {
    if (ys_.empty()) return -1;
    auto it = std::upper_bound(ys_.begin(), ys_.end(), y);
    return juce::jmax(0, static_cast<int>(it - ys_.begin()) - 1);
}

void ConversaWhatsappComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    barra_.mouseWheelMove(e.getEventRelativeTo(&barra_), w);
    rolagem_ = static_cast<float>(barra_.getCurrentRangeStart());
    repaint(areaLista_);
}

bool ConversaWhatsappComponent::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey && overlay_ != nullptr) { fecharOverlay(); return true; }
    if (k == juce::KeyPress::escapeKey && modoSelecao_) { sairSelecao(); return true; }
    const float pagina = static_cast<float>(areaLista_.getHeight()) * 0.85f;
    if (k == juce::KeyPress::pageDownKey) { rolarPara(rolagem_ + pagina); return true; }
    if (k == juce::KeyPress::pageUpKey) { rolarPara(rolagem_ - pagina); return true; }
    if (k == juce::KeyPress::homeKey) { rolarPara(0); return true; }
    if (k == juce::KeyPress::endKey) { rolarPara(alturaTotal_); return true; }
    if (k == juce::KeyPress::downKey) { rolarPara(rolagem_ + 60); return true; }
    if (k == juce::KeyPress::upKey) { rolarPara(rolagem_ - 60); return true; }
    return false;
}

// ── Print ────────────────────────────────────────────────────────────────────────────────────

void ConversaWhatsappComponent::avisar(const juce::String& texto) {
    ProgressoGlobal::obterInstancia().iniciarTarefa("wa_aviso", "WhatsApp", 0, nullptr, "", false, true);
    ProgressoGlobal::obterInstancia().concluirTarefa("wa_aviso", texto);
}

void ConversaWhatsappComponent::fecharOverlay() {
    if (overlay_ == nullptr) return;
    overlay_->fechar();
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    juce::MessageManager::callAsync([seguro] {
        if (seguro != nullptr) { seguro->overlay_.reset(); seguro->grabKeyboardFocus(); }
    });
}

void ConversaWhatsappComponent::iniciarPrint() {
    if (!carregada_ || sobreposicaoPrint_ != nullptr || perguntarVoce_) return;
    auto s = std::make_unique<SobreposicaoPrint>();
    s->aoSelecionar = [this](juce::Rectangle<int> r) { aoSelecionarAreaDoPrint(r); };
    s->aoCancelar = [this] { cancelarPrint(); };
    sobreposicaoPrint_ = std::move(s);
    addAndMakeVisible(*sobreposicaoPrint_);
    sobreposicaoPrint_->setBounds(getLocalBounds());
    sobreposicaoPrint_->toFront(true);
    sobreposicaoPrint_->grabKeyboardFocus();
}

void ConversaWhatsappComponent::cancelarPrint() {
    if (sobreposicaoPrint_ == nullptr) return;
    sobreposicaoPrint_->setVisible(false);
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    juce::MessageManager::callAsync([seguro] {
        if (seguro != nullptr) { seguro->sobreposicaoPrint_.reset(); seguro->grabKeyboardFocus(); }
    });
}

// Captura o próprio componente (não a tela) em 2x e cola a faixa de rodapé com a identificação do print.
juce::Image ConversaWhatsappComponent::comporPrint(juce::Rectangle<int> area) {
    area = area.getIntersection(getLocalBounds());
    if (area.isEmpty()) return {};
    const bool eraVisivel = sobreposicaoPrint_ != nullptr && sobreposicaoPrint_->isVisible();
    if (sobreposicaoPrint_ != nullptr) sobreposicaoPrint_->setVisible(false);
    juce::Image foto = createComponentSnapshot(area, true, 2.0f);
    if (sobreposicaoPrint_ != nullptr) sobreposicaoPrint_->setVisible(eraVisivel);
    if (!foto.isValid()) return {};

    // Rodapé: <conversa> · print gerado em <data/hora> · SHA-256 do original: <16>… [· prova de data …]
    juce::String sha16;
    if (const auto arq = matriz::provadata::infoArquivoDoItem(projeto_.projeto(), conversaItemId_)) sha16 = matriz::provadata::hashCurto(arq->sha256);
    const auto estado = matriz::provadata::estadoDoItem(projeto_.projeto().pasta(), conversaItemId_);
    juce::String texto = t("light.wa.print.footer")
                             .replace("{name}", info_.contato)
                             .replace("{dt}", juce::Time::getCurrentTime().formatted("%d/%m/%Y %H:%M:%S"))
                             .replace("{h}", sha16);
    if (estado.estado == matriz::provadata::Estado::Confirmada)
        texto += t("light.wa.print.footerConfirmed").replace("{d}", matriz::provadata::dataCurtaDoBloco(estado.dataBloco));
    else if (estado.estado != matriz::provadata::Estado::Nenhum)
        texto += t("light.wa.print.footerPending");

    const juce::Font f(juce::FontOptions(22.0f));
    juce::AttributedString as;
    as.append(texto, f, juce::Colours::white);
    as.setWordWrap(juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout(as, static_cast<float>(foto.getWidth() - 32));
    const int banda = juce::jmax(52, static_cast<int>(std::ceil(layout.getHeight())) + 24);
    juce::Image saida(juce::Image::RGB, foto.getWidth(), foto.getHeight() + banda, true);
    juce::Graphics g(saida);
    g.drawImageAt(foto, 0, 0);
    g.setColour(juce::Colour(0xff1f2937));
    g.fillRect(0, foto.getHeight(), saida.getWidth(), banda);
    layout.draw(g, juce::Rectangle<float>(16.0f, static_cast<float>(foto.getHeight() + 12), static_cast<float>(foto.getWidth() - 32), static_cast<float>(banda - 16)));
    return saida;
}

void ConversaWhatsappComponent::aoSelecionarAreaDoPrint(juce::Rectangle<int> area) {
    auto imagem = comporPrint(area);
    cancelarPrint();
    if (!imagem.isValid()) return;
    if (matriz::app::copiarImagemParaAreaDeTransferencia(imagem)) avisar(t("light.wa.print.copied"));
    mostrarSalvarPrint(imagem);
}

void ConversaWhatsappComponent::mostrarSalvarPrint(juce::Image imagem) {
    const auto nome = juce::File::createLegalFileName(t("light.wa.print.defaultName").replace("{name}", info_.contato).replace("{dt}", juce::Time::getCurrentTime().formatted("%Y-%m-%d %H.%M.%S")));
    auto conteudo = std::make_shared<ConteudoSalvarPrint>(nome, juce::File::getSpecialLocation(juce::File::userPicturesDirectory));
    PainelOverlay::Config cfg;
    cfg.titulo = t("light.wa.print.saveTitle");
    cfg.conteudo = conteudo;
    cfg.larguraCartao = 600;
    cfg.alturaConteudo = 190;
    cfg.botoes = { { t("light.wa.print.save"), 1, true, false }, { t("light.ui.common.cancel"), 0, false, true } };
    overlay_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlay_);
    overlay_->setBounds(getLocalBounds());
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    overlay_->mostrar(cfg, [seguro, conteudo, imagem](PainelOverlay::Resultado r) {
        const bool salvar = r.botaoId == 1;
        const auto nomeArq = conteudo->nome();
        const auto pasta = conteudo->pasta();
        const bool registrar = conteudo->registrar();
        juce::MessageManager::callAsync([seguro, salvar, nomeArq, pasta, registrar, imagem] {
            if (seguro == nullptr) return;
            seguro->overlay_.reset();
            if (!salvar) return;
            auto limpo = juce::File::createLegalFileName(nomeArq.trim());
            if (limpo.isEmpty()) limpo = "Print";
            if (!limpo.endsWithIgnoreCase(".png")) limpo += ".png";
            auto destino = pasta.getChildFile(limpo).getNonexistentSibling();
            pasta.createDirectory();
            {
                juce::FileOutputStream saida(destino);
                juce::PNGImageFormat png;
                if (saida.failedToOpen() || !png.writeImageToStream(imagem, saida)) return;
            }
            seguro->avisar(t("light.wa.print.saved").replace("{path}", destino.getFullPathName()));
            if (registrar) {
                // Prova de data do próprio PNG: o recibo fica ao lado (<nome>.png.ots); sem internet entra na fila.
                matriz::provadata::registrarPastaParaAtualizacaoExtra(destino);
                seguro->projeto_.executarProvaEmFundo([destino] { matriz::provadata::carimbarArquivo(destino, true); });
            }
        });
    });
    overlay_->toFront(true);
}

// ── Exportar trecho ──────────────────────────────────────────────────────────────────────────

void ConversaWhatsappComponent::entrarSelecao() {
    if (!carregada_ || perguntarVoce_) return;
    modoSelecao_ = true;
    ancoraSel_ = selIni_ = selFim_ = -1;
    for (auto* c : std::initializer_list<juce::Component*>{&lblSelecionadas_, &lblDicaSel_, &lblDe_, &lblAte_, &campoDe_, &campoAte_, btnPeriodo_.get(), btnTodas_.get(), btnExportar_.get(), btnSairSel_.get()})
        c->setVisible(true);
    btnTrecho_->setToggleState(true, juce::dontSendNotification);
    atualizarBarraSelecao();
    resized();
    repaint();
}

void ConversaWhatsappComponent::sairSelecao() {
    modoSelecao_ = false;
    ancoraSel_ = selIni_ = selFim_ = -1;
    for (auto* c : std::initializer_list<juce::Component*>{&lblSelecionadas_, &lblDicaSel_, &lblDe_, &lblAte_, &campoDe_, &campoAte_, btnPeriodo_.get(), btnTodas_.get(), btnExportar_.get(), btnSairSel_.get()})
        c->setVisible(false);
    btnTrecho_->setToggleState(false, juce::dontSendNotification);
    resized();
    repaint();
}

int ConversaWhatsappComponent::mensagensSelecionadas() const {
    if (selIni_ < 0 || selFim_ < selIni_ || selFim_ >= static_cast<int>(linhas_.size())) return 0;
    const int a = linhas_[static_cast<size_t>(selIni_)].iMsg;
    const auto& fim = linhas_[static_cast<size_t>(selFim_)];
    return fim.iMsg + fim.nMsgs - a;
}

void ConversaWhatsappComponent::atualizarBarraSelecao() {
    const int n = mensagensSelecionadas();
    lblSelecionadas_.setText(n > 0 ? t("light.wa.trecho.count").replace("{n}", juce::String(n)) : t("light.wa.trecho.none"), juce::dontSendNotification);
    btnExportar_->setEnabled(n > 0);
}

void ConversaWhatsappComponent::selecionarIntervalo(int a, int b) {
    if (linhas_.empty()) return;
    a = juce::jlimit(0, static_cast<int>(linhas_.size()) - 1, a);
    b = juce::jlimit(0, static_cast<int>(linhas_.size()) - 1, b);
    selIni_ = juce::jmin(a, b);
    selFim_ = juce::jmax(a, b);
    atualizarBarraSelecao();
    repaint();
}

void ConversaWhatsappComponent::selecionarTudo() {
    if (linhas_.empty()) return;
    ancoraSel_ = 0;
    selecionarIntervalo(0, static_cast<int>(linhas_.size()) - 1);
}

void ConversaWhatsappComponent::selecionarPeriodo() {
    if (conversa_ == nullptr || linhas_.empty()) return;
    const long long de = chaveDeDataDigitada(campoDe_.getText());
    long long ate = chaveDeDataDigitada(campoAte_.getText());
    if (de == 0 && ate == 0) return;
    if (ate == 0) ate = 99991231;
    int primeira = -1, ultima = -1;
    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
        const auto& m = conversa_->mensagens[static_cast<size_t>(linhas_[static_cast<size_t>(i)].iMsg)];
        const long long dia = m.chaveDia();
        if (dia >= de && dia <= ate) {
            if (primeira < 0) primeira = i;
            ultima = i;
        }
    }
    if (primeira < 0) {
        selIni_ = selFim_ = -1;
        atualizarBarraSelecao();
        repaint();
        return;
    }
    ancoraSel_ = primeira;
    selecionarIntervalo(primeira, ultima);
    rolarPara(ys_[static_cast<size_t>(primeira)]);
}

void ConversaWhatsappComponent::clicarSelecao(const juce::MouseEvent& e) {
    if (!areaLista_.contains(e.getPosition()) || ys_.empty()) return;
    const int li = linhaEm(static_cast<float>(e.y - areaLista_.getY()) + rolagem_);
    if (li < 0 || li >= static_cast<int>(linhas_.size())) return;
    if (e.mods.isShiftDown() && ancoraSel_ >= 0) {
        selecionarIntervalo(ancoraSel_, li);
    } else {
        ancoraSel_ = li;
        selecionarIntervalo(li, li);
    }
}

void ConversaWhatsappComponent::desenharMarcaSelecao(juce::Graphics& g, int indice, float yTopo) {
    if (selIni_ < 0 || indice < selIni_ || indice > selFim_) return;
    const auto& tk = tema();
    const float yBase = indice + 1 < static_cast<int>(ys_.size()) ? ys_[static_cast<size_t>(indice) + 1] - ys_[static_cast<size_t>(indice)] : alturaTotal_ - ys_[static_cast<size_t>(indice)];
    g.setColour(tk.acento.withAlpha(0.16f));
    g.fillRect(0.0f, yTopo, static_cast<float>(areaLista_.getWidth()), yBase);
    const auto disco = juce::Rectangle<float>(18.0f, 18.0f).withCentre({18.0f, yTopo + juce::jmin(24.0f, yBase / 2.0f)});
    g.setColour(tk.acento);
    g.fillEllipse(disco);
    g.setColour(tk.textoSobreAcento);
    juce::Path v;
    v.startNewSubPath(disco.getX() + 4.5f, disco.getCentreY());
    v.lineTo(disco.getCentreX() - 1.0f, disco.getBottom() - 5.5f);
    v.lineTo(disco.getRight() - 4.0f, disco.getY() + 5.0f);
    g.strokePath(v, juce::PathStrokeType(2.0f));
}

void ConversaWhatsappComponent::abrirOpcoesDoTrecho() {
    if (mensagensSelecionadas() <= 0 || overlay_ != nullptr) return;
    // O registro da conversa tem dados do computador? Só então a opção fica disponível.
    bool temComputador = false;
    {
        const auto m = matriz::provadata::manifestoDoItem(projeto_.projeto().pasta(), conversaItemId_);
        if (m.existsAsFile()) temComputador = juce::JSON::parse(m).hasProperty("computador");
    }
    juce::File pasta(matriz::app::lerPreferenciaTexto("trecho_pasta", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getFullPathName()));
    if (!pasta.isDirectory()) pasta = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto conteudo = std::make_shared<ConteudoOpcoesTrecho>(temComputador, pasta);
    PainelOverlay::Config cfg;
    cfg.titulo = t("light.wa.trecho.opt.title");
    cfg.conteudo = conteudo;
    cfg.larguraCartao = 620;
    cfg.alturaConteudo = 190;
    cfg.botoes = { { t("light.wa.trecho.opt.go"), 1, true, false }, { t("light.ui.common.cancel"), 0, false, true } };
    overlay_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlay_);
    overlay_->setBounds(getLocalBounds());
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    overlay_->mostrar(cfg, [seguro, conteudo](PainelOverlay::Resultado r) {
        const bool ok = r.botaoId == 1;
        const auto opcoes = conteudo->opcoes();
        juce::MessageManager::callAsync([seguro, ok, opcoes] {
            if (seguro == nullptr) return;
            seguro->overlay_.reset();
            if (!ok) return;
            matriz::app::gravarPreferenciaTexto("trecho_pasta", opcoes.pastaDestino.getFullPathName());
            seguro->exportarComAviso(opcoes);
        });
    });
    overlay_->toFront(true);
}

// Prova de data da conversa ainda pendente? Avisa antes (Exportar assim mesmo / Esperar).
void ConversaWhatsappComponent::exportarComAviso(matriz::provadata::OpcoesTrecho opcoes) {
    const auto estado = matriz::provadata::estadoDoItem(projeto_.projeto().pasta(), conversaItemId_);
    const bool pendente = estado.estado == matriz::provadata::Estado::Pendente || estado.estado == matriz::provadata::Estado::NaFila;
    const auto seguir = [this, opcoes] {
        avisar(t("light.wa.trecho.working"));
        juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
        executarExportacaoDeTrecho(opcoes, true, [seguro](matriz::provadata::ResultadoTrecho r) {
            if (seguro == nullptr) return;
            if (r.ok) {
                r.pasta.revealToUser();
                seguro->avisar(t("light.wa.trecho.done").replace("{path}", r.pasta.getFullPathName()));
            } else {
                seguro->avisar(t("light.wa.trecho.failed").replace("{reason}", r.erro));
            }
        });
    };
    if (!pendente) { seguir(); return; }
    PainelOverlay::Config cfg;
    cfg.titulo = t("light.wa.trecho.pending.title");
    cfg.mensagem = t("light.wa.trecho.pending.body");
    cfg.botoes = { { t("light.wa.trecho.pending.go"), 1, true, false }, { t("light.wa.trecho.pending.wait"), 0, false, true } };
    overlay_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlay_);
    overlay_->setBounds(getLocalBounds());
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    overlay_->mostrar(cfg, [seguro, seguir](PainelOverlay::Resultado r) {
        const bool ok = r.botaoId == 1;
        juce::MessageManager::callAsync([seguro, ok, seguir] {
            if (seguro == nullptr) return;
            seguro->overlay_.reset();
            if (ok) seguir();
        });
    });
    overlay_->toFront(true);
}

void ConversaWhatsappComponent::executarExportacaoDeTrecho(matriz::provadata::OpcoesTrecho opcoes, bool usarRede, std::function<void(matriz::provadata::ResultadoTrecho)> aoTerminar) {
    const int n = mensagensSelecionadas();
    if (n <= 0 || conversa_ == nullptr) {
        matriz::provadata::ResultadoTrecho vazio;
        vazio.erro = t("light.trecho.err.range");
        if (aoTerminar) aoTerminar(vazio);
        return;
    }
    const int primeira = linhas_[static_cast<size_t>(selIni_)].iMsg;
    const int ultima = linhas_[static_cast<size_t>(selFim_)].iMsg + linhas_[static_cast<size_t>(selFim_)].nMsgs - 1;
    const auto original = matriz::provadata::infoArquivoDoItem(projeto_.projeto(), conversaItemId_);
    if (!original) {
        matriz::provadata::ResultadoTrecho r;
        r.erro = t("light.prova.err.noFile");
        if (aoTerminar) aoTerminar(r);
        return;
    }
    auto conversa = conversa_;
    auto mapa = mapaMidias_;
    auto* projeto = &projeto_;
    const auto pasta = projeto_.projeto().pasta();
    const auto id = conversaItemId_;
    const auto contato = info_.contato;
    const auto voce = voce_;
    const bool pt = portugues_;
    auto vivo = projeto_.tokenVivo();
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    projeto_.executarProvaEmFundo([=] {
        matriz::provadata::EntradaTrecho e;
        e.conversa = conversa.get();
        e.primeiraMensagem = primeira;
        e.ultimaMensagem = ultima;
        e.contato = contato;
        e.voce = voce;
        e.arquivoOriginal = original->arquivo;
        e.nomeOriginal = original->nomeOriginal;
        e.shaOriginal = original->sha256;
        e.manifestoDoOriginal = matriz::provadata::manifestoDoItem(pasta, id);
        e.portugues = pt;
        e.resolverMidia = [projeto, mapa](const juce::String& nome) {
            matriz::provadata::MidiaDoTrecho m;
            const auto it = mapa.find(chaveDeNome(nome));
            if (it == mapa.end() || it->second.midiaItemId.empty()) return m;
            if (const auto info = projeto->arquivoPrincipal(it->second.midiaItemId)) m.arquivo = juce::File(info->caminhoAbsoluto);
            return m;
        };
        auto resultado = matriz::provadata::exportarTrecho(e, opcoes, usarRede);
        juce::MessageManager::callAsync([vivo, seguro, aoTerminar, resultado] {
            if (vivo.expired() || seguro == nullptr) return;
            if (aoTerminar) aoTerminar(resultado);
        });
    });
}

// ── Quem é você ──────────────────────────────────────────────────────────────────────────────

void ConversaWhatsappComponent::montarSeletorDeVoce() {
    for (auto& b : botoesVoce_) removeChildComponent(b.get());
    botoesVoce_.clear();
    const auto lembrado = matriz::app::lerVoceWhatsapp();
    const auto criar = [&](const juce::String& rotulo, const juce::String& autor, bool destaque) {
        auto b = std::make_unique<BotaoPilula>(rotulo, destaque ? BotaoPilula::Estilo::Primario : BotaoPilula::Estilo::Secundario);
        b->setComponentID("wa.voce." + juce::String(static_cast<int>(botoesVoce_.size())));
        b->onClick = [this, autor] { definirVoce(autor); };
        addAndMakeVisible(*b);
        botoesVoce_.push_back(std::move(b));
    };
    for (const auto& a : info_.autores) criar(a, a, lembrado.isNotEmpty() && a.equalsIgnoreCase(lembrado));
    criar(t("light.sm.t5.nenhum"), {}, false);
}

void ConversaWhatsappComponent::definirVoce(const juce::String& autor) {
    try {
        matriz::whatsapp::gravarVoce(projeto_.projeto().registro(), conversaItemId_, autor);  // guarda no item da conversa
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("WhatsApp: não foi possível gravar quem é você — " + juce::String(e.what()));
    }
    if (autor.isNotEmpty()) matriz::app::gravarVoceWhatsapp(autor);  // pré-marca nas próximas conversas
    voce_ = autor;
    info_.voce = autor;
    info_.voceDefinido = true;
    perguntarVoce_ = false;
    for (auto& b : botoesVoce_) removeChildComponent(b.get());
    botoesVoce_.clear();
    if (conversa_ != nullptr)
        for (auto& l : linhas_)
            if (l.iMsg >= 0 && l.tipo != Tipo::Dia)
                l.meu = voce_.isNotEmpty() && conversa_->mensagens[static_cast<size_t>(l.iMsg)].autor.equalsIgnoreCase(voce_);
    if (btnVoce_) btnVoce_->setButtonText(t("light.wa.voce").replace("{nome}", voce_.isEmpty() ? juce::String::fromUTF8("\xe2\x80\x94") : voce_));
    impl_->cacheMedidas.clear();
    larguraMedida_ = 0;  // a largura dos balões muda (o nome do autor some/aparece): mede de novo
    resized();
    repaint();
}

void ConversaWhatsappComponent::mostrarMenuDeVoce() {
    juce::PopupMenu m;
    int id = 1;
    for (const auto& a : info_.autores) m.addItem(id++, a, true, voce_.equalsIgnoreCase(a) && voce_.isNotEmpty());
    m.addSeparator();
    m.addItem(id, t("light.sm.t5.nenhum"), true, voce_.isEmpty());
    const int total = info_.autores.size();
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(btnVoce_.get()), [seguro, total](int r) {
        if (seguro == nullptr || r <= 0) return;
        seguro->definirVoce(r <= total ? seguro->info_.autores[r - 1] : juce::String());
    });
}

// ── Busca ────────────────────────────────────────────────────────────────────────────────────

void ConversaWhatsappComponent::definirBusca(const juce::String& texto) {
    consulta_ = texto.trim();
    resultados_.clear();
    resultadoAtual_ = -1;
    if (consulta_.isNotEmpty() && conversa_ != nullptr) {
        const auto& msgs = conversa_->mensagens;
        int ultimaLinha = -1;
        for (size_t i = 0; i < msgs.size(); ++i) {
            const auto& m = msgs[i];
            if (m.tipo == TipoMensagem::Sistema) continue;
            if (m.texto.containsIgnoreCase(consulta_) || m.legenda.containsIgnoreCase(consulta_)) {
                const int linha = i < mensagemParaLinha_.size() ? mensagemParaLinha_[i] : -1;
                if (linha >= 0 && linha != ultimaLinha) {
                    resultados_.push_back(linha);
                    ultimaLinha = linha;
                }
            }
        }
    }
    if (consulta_.isEmpty()) lblContagem_.setText({}, juce::dontSendNotification);
    else if (resultados_.empty()) lblContagem_.setText(t("light.wa.nenhumResultado"), juce::dontSendNotification);
    else if (resultados_.size() == 1) lblContagem_.setText(t("light.wa.umResultado"), juce::dontSendNotification);
    else lblContagem_.setText(t("light.wa.resultados").replace("{n}", juce::String(static_cast<int>(resultados_.size()))), juce::dontSendNotification);
    impl_->cacheMedidas.clear();
    if (!resultados_.empty()) irParaResultado(0);
    else repaint(areaLista_);
}

void ConversaWhatsappComponent::irParaResultado(int indice) {
    if (resultados_.empty()) return;
    resultadoAtual_ = juce::jlimit(0, static_cast<int>(resultados_.size()) - 1, indice);
    const int linha = resultados_[static_cast<size_t>(resultadoAtual_)];
    if (linha >= 0 && linha < static_cast<int>(ys_.size()))
        rolarPara(ys_[static_cast<size_t>(linha)] - static_cast<float>(areaLista_.getHeight()) * 0.3f);
}

void ConversaWhatsappComponent::buscarParaTeste(const juce::String& texto) {
    campoBusca_.setText(texto, false);
    definirBusca(texto);
}

int ConversaWhatsappComponent::mensagensParaTeste() const { return conversa_ ? static_cast<int>(conversa_->mensagens.size()) : 0; }

int ConversaWhatsappComponent::albunsParaTeste() const {
    int n = 0;
    for (const auto& l : linhas_) n += (l.tipo == Tipo::Album) ? 1 : 0;
    return n;
}

// ── Mídias em background ─────────────────────────────────────────────────────────────────────

std::string ConversaWhatsappComponent::idDaMidia(int iMsg) const {
    if (conversa_ == nullptr || iMsg < 0 || iMsg >= static_cast<int>(conversa_->mensagens.size())) return {};
    const auto it = mapaMidias_.find(chaveDeNome(conversa_->mensagens[static_cast<size_t>(iMsg)].anexo));
    return it == mapaMidias_.end() ? std::string() : it->second.midiaItemId;
}

void ConversaWhatsappComponent::pedirMiniatura(const std::string& midiaId, bool ehVideo) {
    if (midiaId.empty() || impl_->minis.count(midiaId) || impl_->pendentesMini.count(midiaId)) return;
    impl_->pendentesMini.insert(midiaId);
    auto* projeto = &projeto_;
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    impl_->poolMidia.addJob([seguro, projeto, midiaId, ehVideo] {
        Impl::Mini mini;
        try {
            const auto reduzir = [](juce::Image img) {
                if (!img.isValid() || img.getWidth() <= 640) return img;
                const int h = juce::roundToInt(img.getHeight() * 640.0 / img.getWidth());
                return img.rescaled(640, juce::jmax(1, h), juce::Graphics::mediumResamplingQuality);
            };
            if (auto caminho = projeto->caminhoMiniaturaPrincipal(midiaId))
                mini.img = reduzir(juce::ImageFileFormat::loadFrom(juce::File(*caminho)));
            const auto info = projeto->arquivoPrincipal(midiaId);
            if (info) {
                if (!mini.img.isValid() && !ehVideo) mini.img = reduzir(juce::ImageFileFormat::loadFrom(juce::File(info->caminhoAbsoluto)));
                const auto json = juce::JSON::parse(info->caracteristicasTecnicasJson);
                if (json.hasProperty("duracaoSegundos")) mini.duracao = static_cast<double>(json["duracaoSegundos"]);
            }
        } catch (...) {
        }
        juce::MessageManager::callAsync([seguro, midiaId, mini]() mutable {
            if (seguro == nullptr) return;
            seguro->impl_->pendentesMini.erase(midiaId);
            seguro->impl_->minis[midiaId] = std::move(mini);
            seguro->repaint(seguro->areaLista_);
        });
    });
}

void ConversaWhatsappComponent::pedirOnda(const std::string& midiaId) {
    if (midiaId.empty() || impl_->ondas.count(midiaId) || impl_->pendentesOnda.count(midiaId)) return;
    impl_->pendentesOnda.insert(midiaId);
    auto* projeto = &projeto_;
    juce::Component::SafePointer<ConversaWhatsappComponent> seguro(this);
    impl_->poolMidia.addJob([seguro, projeto, midiaId] {
        std::vector<float> picos;
        double duracao = 0.0;
        try {
            const auto info = projeto->arquivoPrincipal(midiaId);
            if (info) {
                juce::AudioFormatManager fm;
                matriz::audio::registrarFormatosDeAudio(fm);
                std::unique_ptr<juce::AudioFormatReader> leitor(fm.createReaderFor(juce::File(info->caminhoAbsoluto)));
                if (leitor != nullptr && leitor->lengthInSamples > 0) {
                    duracao = static_cast<double>(leitor->lengthInSamples) / leitor->sampleRate;
                    constexpr int barras = 44;
                    picos.assign(barras, 0.0f);
                    const juce::int64 total = leitor->lengthInSamples;
                    const int canais = static_cast<int>(juce::jmin<unsigned int>(leitor->numChannels, 2));
                    juce::AudioBuffer<float> buf(canais, 4096);
                    for (int b = 0; b < barras; ++b) {
                        const juce::int64 ini = total * b / barras;
                        const juce::int64 fim = total * (b + 1) / barras;
                        const juce::int64 passo = juce::jmax<juce::int64>(1, (fim - ini) / 4096);
                        float pico = 0;
                        // amostra alguns blocos pequenos de cada fatia (rápido mesmo em áudios longos)
                        for (juce::int64 pos = ini; pos < fim; pos += juce::jmax<juce::int64>(4096, passo * 4096)) {
                            const int n = static_cast<int>(juce::jmin<juce::int64>(1024, fim - pos));
                            leitor->read(&buf, 0, n, pos, true, canais > 1);
                            for (int c = 0; c < canais; ++c) pico = juce::jmax(pico, buf.getMagnitude(c, 0, n));
                        }
                        picos[static_cast<size_t>(b)] = pico;
                    }
                    float mx = 0.001f;
                    for (auto p : picos) mx = juce::jmax(mx, p);
                    for (auto& p : picos) p = juce::jlimit(0.06f, 1.0f, p / mx);
                }
            }
        } catch (...) {
        }
        juce::MessageManager::callAsync([seguro, midiaId, picos = std::move(picos), duracao]() mutable {
            if (seguro == nullptr) return;
            seguro->impl_->pendentesOnda.erase(midiaId);
            seguro->impl_->ondas[midiaId] = std::move(picos);
            seguro->impl_->duracoes[midiaId] = duracao;
            seguro->repaint(seguro->areaLista_);
        });
    });
}

void ConversaWhatsappComponent::alternarAudio(const std::string& midiaId) {
    if (midiaId.empty()) return;
    const auto info = projeto_.arquivoPrincipal(midiaId);
    if (!info) return;
    impl_->reprodutor.alternar(midiaId, juce::File(info->caminhoAbsoluto));
    repaint(areaLista_);
}

void ConversaWhatsappComponent::abrirMidia(const std::string& inicial, std::vector<std::string> sequencia) {
    if (aoAbrirMidia && !inicial.empty()) aoAbrirMidia(inicial, sequencia);
}

void ConversaWhatsappComponent::clicarMidiaParaTeste(int indiceDaLinha) {
    if (indiceDaLinha < 0 || indiceDaLinha >= static_cast<int>(linhas_.size()) || conversa_ == nullptr) return;
    const auto& l = linhas_[static_cast<size_t>(indiceDaLinha)];
    std::vector<std::string> seq;
    for (int k = 0; k < l.nMsgs; ++k) seq.push_back(idDaMidia(l.iMsg + k));
    if (!seq.empty()) abrirMidia(seq.front(), seq);
}

// ── Cliques ──────────────────────────────────────────────────────────────────────────────────

void ConversaWhatsappComponent::mouseMove(const juce::MouseEvent& e) {
    bool sobre = modoSelecao_ && areaLista_.contains(e.getPosition());
    if (modoSelecao_) { setMouseCursor(sobre ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); return; }
    for (const auto& a : impl_->acertos)
        if (a.r.contains(e.getPosition())) { sobre = true; break; }
    setMouseCursor(sobre ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ConversaWhatsappComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (perguntarVoce_) return;  // primeiro responde "Quem é você?"
    if (modoSelecao_) {  // exportar trecho: o clique escolhe mensagens, não abre mídia nem toca áudio
        clicarSelecao(e);
        return;
    }
    for (const auto& a : impl_->acertos) {
        if (!a.r.contains(e.getPosition())) continue;
        if (a.tipo == Acerto::Tipo::Play) {
            alternarAudio(idDaMidia(a.iMsg));
        } else if (a.tipo == Acerto::Tipo::Midia) {
            const auto id = idDaMidia(a.iMsg);
            abrirMidia(id, {id});
        } else {
            // álbum: a sequência é o álbum todo, começando pela célula clicada
            const int li = mensagemParaLinha_[static_cast<size_t>(a.iMsg)];
            if (li < 0) return;
            const auto& l = linhas_[static_cast<size_t>(li)];
            std::vector<std::string> seq;
            for (int k = 0; k < l.nMsgs; ++k) seq.push_back(idDaMidia(l.iMsg + k));
            const int ini = juce::jlimit(0, static_cast<int>(seq.size()) - 1, a.sub);
            abrirMidia(seq[static_cast<size_t>(ini)], seq);
        }
        return;
    }
}

// ── Pintura ──────────────────────────────────────────────────────────────────────────────────

void ConversaWhatsappComponent::desenharCabecalho(juce::Graphics& g, juce::Rectangle<int> area) {
    const auto& tk = tema();
    g.setColour(tk.cartao);
    g.fillRect(area);
    g.setColour(tk.linhaSuave);
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    auto r = area.reduced(16, 10);
    r.removeFromLeft(150); // Espaço reservado para o botão '◀ BACK TO MAP'
    auto circulo = r.removeFromLeft(44).toFloat();
    g.setColour(juce::Colour(0xff25d366));
    g.fillEllipse(circulo);
    g.setColour(juce::Colours::white);
    g.setFont(fonteUi(tk.fonteCorpoGrande + 1.0f, juce::Font::bold));
    g.drawText(iniciais(info_.contato), circulo.toNearestInt(), juce::Justification::centred);
    r.removeFromLeft(12);
    g.setColour(tk.textoPrimario);
    g.setFont(fonteUi(tk.fonteCorpoGrande + 3.0f, juce::Font::bold));
    g.drawText(info_.contato, r.removeFromTop(24), juce::Justification::centredLeft, true);
    g.setColour(tk.textoSecundario);
    g.setFont(fonteUi(tk.tamanhoFontePequena + 1.0f));
    const auto ini = conversa_ ? dataCurta(conversa_->primeiraChaveDia) : juce::String();
    const auto fim = conversa_ ? dataCurta(conversa_->ultimaChaveDia) : juce::String();
    juce::String sub = t("light.wa.subtitulo").replace("{a}", ini).replace("{b}", fim);
    if (info_.nMensagens > 0)
        sub += juce::String::fromUTF8("  \xc2\xb7  ") + t("light.wa.detalhes").replace("{n}", juce::String(info_.nMensagens)).replace("{origem}", info_.origem);
    g.drawText(sub, r.withTrimmedRight(470), juce::Justification::centredLeft, true);
}

void ConversaWhatsappComponent::desenharTexto(juce::Graphics& g, const Medida& m, float x, float y, juce::Colour cor, int /*iMsg*/,
                                              const juce::Font& base) {
    if (!m.temTexto) return;
    juce::Font fontes[4] = {base, base.boldened(), base.italicised(), base.boldened().italicised()};
    if (m.italico)
        for (auto& f : fontes) f = f.italicised();
    const auto estiloEm = [&](int i) -> int { return i >= 0 && i < static_cast<int>(m.estilo.size()) ? (m.estilo[static_cast<size_t>(i)] & 3) : 0; };

    // trechos que casam com a busca (índices no texto sem marcação)
    std::vector<std::pair<int, int>> casas;
    if (consulta_.isNotEmpty()) {
        int p = 0;
        while ((p = m.plain.indexOfIgnoreCase(p, consulta_)) >= 0) {
            casas.emplace_back(p, p + consulta_.length());
            p += juce::jmax(1, consulta_.length());
        }
    }
    float yy = y;
    for (const auto& [a, b] : m.linhas) {
        float xx = x;
        // pontos de corte: mudança de estilo ou de destaque
        int i = a;
        while (i < b) {
            const int e = estiloEm(i);
            bool dest = false;
            for (const auto& c : casas)
                if (i >= c.first && i < c.second) { dest = true; break; }
            int j = i + 1;
            while (j < b) {
                bool d2 = false;
                for (const auto& c : casas)
                    if (j >= c.first && j < c.second) { d2 = true; break; }
                if (estiloEm(j) != e || d2 != dest) break;
                ++j;
            }
            const juce::String seg = m.plain.substring(i, j);
            const float w = larguraTxt(fontes[e], seg);
            if (dest) {
                g.setColour(juce::Colour(0xfffacc15).withAlpha(0.85f));
                g.fillRoundedRectangle(xx - 1.0f, yy + 1.0f, w + 2.0f, kLinhaH - 3.0f, 3.0f);
            }
            g.setColour(dest ? juce::Colours::black : cor);
            g.setFont(fontes[e]);
            g.drawText(seg, juce::Rectangle<float>(xx, yy, w + 4.0f, kLinhaH), juce::Justification::centredLeft, false);
            xx += w;
            i = j;
        }
        yy += kLinhaH;
    }
}

void ConversaWhatsappComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);
    desenharCabecalho(g, areaCabecalho_);

    // faixa da busca
    g.setColour(tk.fundo);
    g.fillRect(juce::Rectangle<int>(0, kAlturaCabecalho, getWidth(), kAlturaBusca));
    g.setColour(tk.linhaSuave);
    g.fillRect(0, kAlturaCabecalho + kAlturaBusca - 1, getWidth(), 1);

    if (modoSelecao_ && !areaBarraSel_.isEmpty()) {
        g.setColour(tk.cartao);
        g.fillRect(areaBarraSel_);
        g.setColour(tk.linhaSuave);
        g.fillRect(areaBarraSel_.getX(), areaBarraSel_.getBottom() - 1, areaBarraSel_.getWidth(), 1);
    }

    impl_->acertos.clear();
    ultimasDesenhadas_ = 0;
    if (!carregada_) {
        g.setColour(tk.textoSecundario);
        g.setFont(fonteUi(tk.fonteCorpoGrande));
        g.drawText(erro_.isNotEmpty() ? erro_ : t("light.wa.carregando"), areaLista_, juce::Justification::centred);
        return;
    }
    if (ys_.empty()) {
        g.setColour(tk.textoSecundario);
        g.setFont(fonteUi(tk.fonteCorpoGrande));
        g.drawText(t("light.wa.carregando"), areaLista_, juce::Justification::centred);
        return;
    }

    if (perguntarVoce_) {  // "Quem é você nesta conversa?" — primeira vez que a conversa é aberta
        g.setColour(tk.scrim);
        g.fillRect(areaLista_);
        desenharCartao(g, rcSeletorVoce_.toFloat(), tk.raioCartao, tk.cartao, true);
        g.setColour(tk.textoPrimario);
        g.setFont(fonteUi(tk.fonteCorpoGrande + 4.0f, juce::Font::bold));
        g.drawText(t("light.sm.t5.titulo"), rcSeletorVoce_.reduced(24, 0).withTrimmedTop(18).withHeight(34), juce::Justification::centredLeft, true);
        g.setColour(tk.textoSecundario);
        g.setFont(fonteUi(tk.tamanhoFonteCorpo));
        g.drawFittedText(t("light.sm.t5.dica"), rcSeletorVoce_.reduced(24, 0).withTrimmedTop(52).withHeight(34), juce::Justification::topLeft, 2);
        return;
    }
    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(areaLista_);
    g.setOrigin(areaLista_.getPosition());
    // linhas visíveis (busca binária; nada fora da janela é desenhado)
    const float topo = rolagem_, base = rolagem_ + static_cast<float>(areaLista_.getHeight());
    int i = linhaEm(topo);
    if (i < 0) i = 0;
    for (; i < static_cast<int>(linhas_.size()); ++i) {
        const float y = ys_[static_cast<size_t>(i)] - rolagem_;
        if (ys_[static_cast<size_t>(i)] > base) break;
        desenharLinha(g, i, y);
        if (modoSelecao_) desenharMarcaSelecao(g, i, y);
        ++ultimasDesenhadas_;
    }
    // os acertos foram guardados em coordenadas locais da lista: converte para o componente
    for (auto& a : impl_->acertos) a.r.translate(areaLista_.getX(), areaLista_.getY());
}

void ConversaWhatsappComponent::desenharLinha(juce::Graphics& g, int indice, float yTopo) {
    const auto& tk = tema();
    const auto& l = linhas_[static_cast<size_t>(indice)];
    const auto& msg = conversa_->mensagens[static_cast<size_t>(l.iMsg)];
    const Medida& m = medidaEmCache(indice);
    const float larguraLista = static_cast<float>(areaLista_.getWidth());
    const juce::Font base = fonteUi(tk.tamanhoFonteCorpo + 2.0f);
    const bool pt = portugues_;
    const bool resultadoAtual = resultadoAtual_ >= 0 && resultados_[static_cast<size_t>(resultadoAtual_)] == indice;

    if (l.tipo == Tipo::Dia) {
        const auto rotulo = nomeDoDia(msg.ano, msg.mes, msg.dia, pt);
        g.setFont(fonteUi(tk.tamanhoFontePequena + 1.0f, juce::Font::bold));
        const float w = larguraTxt(g.getCurrentFont(), rotulo) + 28.0f;
        auto pilula = juce::Rectangle<float>(w, 26.0f).withCentre({larguraLista / 2.0f, yTopo + kAlturaDia / 2.0f});
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(pilula, 13.0f);
        g.setColour(tk.textoSecundario);
        g.drawText(rotulo, pilula.toNearestInt(), juce::Justification::centred);
        return;
    }
    if (l.tipo == Tipo::Sistema) {
        const float w = juce::jmax(m.larg, 60.0f);
        auto caixa = juce::Rectangle<float>(w, m.alt - 8.0f).withCentre({larguraLista / 2.0f, yTopo + m.alt / 2.0f});
        g.setColour(tk.painelAlt.withAlpha(0.65f));
        g.fillRoundedRectangle(caixa, 9.0f);
        desenharTexto(g, m, caixa.getX() + 14.0f, caixa.getY() + 6.0f, tk.textoTerciario, l.iMsg, base.withHeight(base.getHeight() - 1.5f));
        return;
    }

    // ── balão ──
    const bool meu = l.meu;
    const float gap = l.inicioSequencia ? 10.0f : 2.0f;
    const bool mostrarNome = l.inicioSequencia && !meu && msg.autor.isNotEmpty();
    const float x = meu ? larguraLista - kMargemLado - m.larg : kMargemLado;
    const float y = yTopo + gap;
    const float h = m.alt - gap;
    auto caixa = juce::Rectangle<float>(x, y, m.larg, h);

    juce::Colour fundoBalao = meu ? tk.acento.darker(0.4f) : tk.painelAlt;
    if (l.tipo == Tipo::LigVoz || l.tipo == Tipo::LigVideo) fundoBalao = tk.painelAlt.overlaidWith(tk.perigo.withAlpha(0.28f));
    juce::Colour corTexto = meu ? fundoBalao.contrasting(1.0f) : tk.textoPrimario;
    const juce::Colour corHora = meu ? corTexto.withAlpha(0.7f) : tk.textoTerciario;

    if (l.tipo != Tipo::Figurinha) {
        g.setColour(fundoBalao);
        g.fillRoundedRectangle(caixa, kRaio);
        if (resultadoAtual) {
            g.setColour(juce::Colour(0xfffacc15));
            g.drawRoundedRectangle(caixa.reduced(0.5f), kRaio, 2.0f);
        }
    }
    float cy = y + (l.tipo == Tipo::Foto || l.tipo == Tipo::Video || l.tipo == Tipo::Album ? 4.0f : (l.tipo == Tipo::Figurinha ? 2.0f : kPadV));
    if (mostrarNome) {
        g.setColour(corDoAutor(msg.autor));
        g.setFont(fonteUi(tk.tamanhoFonteCorpo + 0.5f, juce::Font::bold));
        g.drawText(msg.autor, juce::Rectangle<float>(x + kPadH, cy - (l.tipo == Tipo::Foto || l.tipo == Tipo::Video || l.tipo == Tipo::Album ? 0.0f : 0.0f), m.larg - 2 * kPadH, kNomeH),
                   juce::Justification::centredLeft, true);
        cy += kNomeH;
    }
    const auto horaTexto = horaDe(msg);
    const auto desenharHora = [&](float xDir, float yBaixo, juce::Colour cor) {
        g.setColour(cor);
        g.setFont(fonteUi(tk.tamanhoFonteMicro + 1.0f));
        g.drawText(horaTexto, juce::Rectangle<float>(xDir - 44.0f, yBaixo - kHoraH, 44.0f, kHoraH), juce::Justification::centredRight, false);
    };

    switch (l.tipo) {
        case Tipo::Texto:
            desenharTexto(g, m, x + kPadH, cy, corTexto, l.iMsg, base);
            desenharHora(x + m.larg - kPadH + 2.0f, y + h - 2.0f, corHora);
            break;
        case Tipo::Oculta:
        case Tipo::Vazia:
        case Tipo::Apagada:
            desenharTexto(g, m, x + kPadH, cy, meu ? corTexto.withAlpha(0.8f) : tk.textoTerciario, l.iMsg, base);
            if (l.tipo == Tipo::Oculta && msg.legenda.isNotEmpty()) {
                g.setColour(corTexto);
                g.setFont(base);
                g.drawText(msg.legenda.upToFirstOccurrenceOf("\n", false, false), juce::Rectangle<float>(x + kPadH, cy + static_cast<float>(m.linhas.size()) * kLinhaH, m.larg - 2 * kPadH, kLinhaH),
                           juce::Justification::centredLeft, true);
            }
            desenharHora(x + m.larg - kPadH + 2.0f, y + h - 2.0f, corHora);
            break;
        case Tipo::LigVoz:
        case Tipo::LigVideo: {
            const auto ic = juce::Rectangle<float>(x + kPadH, cy, 24.0f, 24.0f);
            g.setColour(tk.perigo);
            g.fillEllipse(ic);
            juce::Path fone;  // fone de telefone estilizado
            fone.startNewSubPath(ic.getX() + 6.0f, ic.getBottom() - 8.0f);
            fone.quadraticTo(ic.getCentreX(), ic.getY() + 4.0f, ic.getRight() - 6.0f, ic.getBottom() - 8.0f);
            g.setColour(juce::Colours::white);
            g.strokePath(fone, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(corTexto);
            g.setFont(base);
            g.drawText(t(l.tipo == Tipo::LigVoz ? "light.wa.ligacaoVoz" : "light.wa.ligacaoVideo"),
                       juce::Rectangle<float>(ic.getRight() + 8.0f, cy, m.larg - 2 * kPadH - 32.0f, 24.0f), juce::Justification::centredLeft, true);
            desenharHora(x + m.larg - kPadH + 2.0f, y + h - 2.0f, corHora);
            break;
        }
        case Tipo::Foto:
        case Tipo::Video: {
            const auto midiaId = idDaMidia(l.iMsg);
            auto area = juce::Rectangle<float>(x + 4.0f, cy, kFotoW, kFotoH);
            if (!l.ausente) pedirMiniatura(midiaId, l.tipo == Tipo::Video);
            {
                juce::Graphics::ScopedSaveState s2(g);
                juce::Path p;
                p.addRoundedRectangle(area, kRaio - 3.0f);
                g.reduceClipRegion(p);
                g.setColour(tk.painel);
                g.fillRect(area);
                const auto it = impl_->minis.find(midiaId);
                if (it != impl_->minis.end() && it->second.img.isValid()) {
                    const auto& img = it->second.img;
                    g.drawImage(img, area, juce::RectanglePlacement::fillDestination);  // preenche e corta
                } else {
                    g.setColour(tk.textoTerciario);
                    g.setFont(fonteUi(tk.tamanhoFontePequena));
                    g.drawText(l.ausente ? t("light.wa.arquivoAusente") : msg.anexo, area.toNearestInt(), juce::Justification::centred, true);
                }
                if (l.tipo == Tipo::Video) {
                    g.setColour(juce::Colours::black.withAlpha(0.55f));
                    g.fillEllipse(juce::Rectangle<float>(52.0f, 52.0f).withCentre(area.getCentre()));
                    juce::Path tri;
                    const auto c = area.getCentre();
                    tri.addTriangle(c.x - 8.0f, c.y - 14.0f, c.x - 8.0f, c.y + 14.0f, c.x + 15.0f, c.y);
                    g.setColour(juce::Colours::white);
                    g.fillPath(tri);
                    if (it != impl_->minis.end() && it->second.duracao > 0.0) {
                        g.setColour(juce::Colours::black.withAlpha(0.55f));
                        auto chip = juce::Rectangle<float>(48.0f, 18.0f).withPosition(area.getX() + 8.0f, area.getBottom() - 26.0f);
                        g.fillRoundedRectangle(chip, 9.0f);
                        g.setColour(juce::Colours::white);
                        g.setFont(fonteUi(tk.tamanhoFonteMicro + 1.0f, juce::Font::bold));
                        g.drawText(duracaoTexto(it->second.duracao), chip.toNearestInt(), juce::Justification::centred);
                    }
                }
                if (msg.legenda.isEmpty()) {
                    g.setColour(juce::Colours::black.withAlpha(0.45f));
                    g.fillRoundedRectangle(juce::Rectangle<float>(46.0f, 18.0f).withPosition(area.getRight() - 54.0f, area.getBottom() - 26.0f), 9.0f);
                    desenharHora(area.getRight() - 8.0f, area.getBottom() - 6.0f, juce::Colours::white);
                }
            }
            impl_->acertos.push_back({Acerto::Tipo::Midia, area.toNearestInt(), l.iMsg, 0});
            if (msg.legenda.isNotEmpty()) {
                desenharTexto(g, m, x + 4.0f + 3.0f, area.getBottom() + 4.0f, corTexto, l.iMsg, base);
                desenharHora(x + m.larg - 6.0f, y + h - 2.0f, corHora);
            }
            break;
        }
        case Tipo::Album: {
            const float lado = kFotoW, cel = (lado - 4.0f) / 2.0f;
            const float ax = x + 4.0f;
            for (int k = 0; k < 4 && k < l.nMsgs; ++k) {
                const int iMsgK = l.iMsg + k;
                const auto idK = idDaMidia(iMsgK);
                const bool video = classeDoAnexo(conversa_->mensagens[static_cast<size_t>(iMsgK)].anexo) == ClasseAnexo::Video;
                pedirMiniatura(idK, video);
                const auto celula = juce::Rectangle<float>(ax + (k % 2) * (cel + 4.0f), cy + (k / 2) * (cel + 4.0f), cel, cel);
                juce::Graphics::ScopedSaveState s2(g);
                juce::Path p;
                p.addRoundedRectangle(celula, 7.0f);
                g.reduceClipRegion(p);
                g.setColour(tk.painel);
                g.fillRect(celula);
                const auto it = impl_->minis.find(idK);
                if (it != impl_->minis.end() && it->second.img.isValid()) {
                    const auto& img = it->second.img;
                    g.drawImage(img, celula, juce::RectanglePlacement::fillDestination);
                }
                if (video) {
                    g.setColour(juce::Colours::black.withAlpha(0.5f));
                    g.fillEllipse(juce::Rectangle<float>(30.0f, 30.0f).withCentre(celula.getCentre()));
                    juce::Path tri;
                    const auto c = celula.getCentre();
                    tri.addTriangle(c.x - 4.0f, c.y - 8.0f, c.x - 4.0f, c.y + 8.0f, c.x + 8.0f, c.y);
                    g.setColour(juce::Colours::white);
                    g.fillPath(tri);
                }
                if (k == 3 && l.nMsgs > 4) {
                    g.setColour(juce::Colours::black.withAlpha(0.55f));
                    g.fillRect(celula);
                    g.setColour(juce::Colours::white);
                    g.setFont(fonteUi(tk.fonteCorpoGrande + 8.0f, juce::Font::bold));
                    g.drawText("+" + juce::String(l.nMsgs - 3), celula.toNearestInt(), juce::Justification::centred);
                }
                impl_->acertos.push_back({Acerto::Tipo::Album, celula.toNearestInt(), iMsgK, k});
            }
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.fillRoundedRectangle(juce::Rectangle<float>(46.0f, 18.0f).withPosition(ax + lado - 54.0f, cy + lado - 26.0f), 9.0f);
            desenharHora(ax + lado - 8.0f, cy + lado - 6.0f, juce::Colours::white);
            break;
        }
        case Tipo::Figurinha: {
            const auto midiaId = idDaMidia(l.iMsg);
            auto area = juce::Rectangle<float>(x + 4.0f, cy, kSticker, kSticker);
            if (!l.ausente) pedirMiniatura(midiaId, false);
            const auto it = impl_->minis.find(midiaId);
            if (it != impl_->minis.end() && it->second.img.isValid())
                g.drawImage(it->second.img, area, juce::RectanglePlacement::centred);
            else {
                g.setColour(tk.painelAlt);
                g.fillRoundedRectangle(area, 14.0f);
            }
            desenharHora(area.getRight(), y + h, tk.textoTerciario);
            impl_->acertos.push_back({Acerto::Tipo::Midia, area.toNearestInt(), l.iMsg, 0});
            break;
        }
        case Tipo::Audio: {
            const auto midiaId = idDaMidia(l.iMsg);
            if (!l.ausente) pedirOnda(midiaId);
            const bool tocando = impl_->reprodutor.tocando(midiaId);
            const auto botao = juce::Rectangle<float>(38.0f, 38.0f).withPosition(x + kPadH, cy + (kAudioH - 38.0f) / 2.0f);
            g.setColour(meu ? corTexto.withAlpha(0.22f) : juce::Colour(0xff25d366));
            g.fillEllipse(botao);
            g.setColour(meu ? corTexto : juce::Colours::white);
            const auto c = botao.getCentre();
            if (tocando) {
                g.fillRect(c.x - 6.0f, c.y - 7.0f, 4.0f, 14.0f);
                g.fillRect(c.x + 2.0f, c.y - 7.0f, 4.0f, 14.0f);
            } else {
                juce::Path tri;
                tri.addTriangle(c.x - 5.0f, c.y - 8.0f, c.x - 5.0f, c.y + 8.0f, c.x + 8.0f, c.y);
                g.fillPath(tri);
            }
            impl_->acertos.push_back({Acerto::Tipo::Play, botao.expanded(4.0f).toNearestInt(), l.iMsg, 0});

            // forma de onda + duração
            const auto onda = juce::Rectangle<float>(botao.getRight() + 10.0f, cy + 6.0f, m.larg - (botao.getRight() - x) - 10.0f - kPadH - 36.0f, kAudioH - 12.0f);
            const auto itOnda = impl_->ondas.find(midiaId);
            const double dur = impl_->duracoes.count(midiaId) ? impl_->duracoes[midiaId] : 0.0;
            const double pos = tocando ? impl_->reprodutor.posicao() : 0.0;
            const float prog = dur > 0 ? static_cast<float>(juce::jlimit(0.0, 1.0, pos / dur)) : 0.0f;
            const int barras = itOnda != impl_->ondas.end() && !itOnda->second.empty() ? static_cast<int>(itOnda->second.size()) : 44;
            const float largBarra = onda.getWidth() / static_cast<float>(barras);
            for (int b = 0; b < barras; ++b) {
                const float v = itOnda != impl_->ondas.end() && !itOnda->second.empty() ? itOnda->second[static_cast<size_t>(b)] : 0.12f;
                const float altB = juce::jmax(3.0f, v * onda.getHeight());
                const bool tocada = (static_cast<float>(b) / static_cast<float>(barras)) < prog;
                g.setColour((tocada ? (meu ? corTexto : juce::Colour(0xff25d366)) : corTexto.withAlpha(0.35f)));
                g.fillRoundedRectangle(onda.getX() + b * largBarra + 0.5f, onda.getCentreY() - altB / 2.0f, juce::jmax(1.5f, largBarra - 1.5f), altB, 1.2f);
            }
            g.setColour(corTexto.withAlpha(0.85f));
            g.setFont(fonteUi(tk.tamanhoFontePequena));
            g.drawText(dur > 0 ? duracaoTexto(tocando ? pos : dur) : "", juce::Rectangle<float>(onda.getRight() + 4.0f, cy, 36.0f, kAudioH), juce::Justification::centredLeft, false);
            desenharHora(x + m.larg - kPadH + 2.0f, y + h - 2.0f, corHora);
            break;
        }
        case Tipo::Documento: {
            const auto area = juce::Rectangle<float>(x + kPadH - 4.0f, cy, m.larg - 2 * (kPadH - 4.0f), kDocH);
            g.setColour(corTexto.withAlpha(0.10f));
            g.fillRoundedRectangle(area, 8.0f);
            const auto ic = juce::Rectangle<float>(26.0f, 32.0f).withCentre({area.getX() + 26.0f, area.getCentreY()});
            g.setColour(tk.tipoDocumento);
            g.fillRoundedRectangle(ic, 4.0f);
            g.setColour(juce::Colours::white);
            g.setFont(fonteUi(tk.tamanhoFonteMicro, juce::Font::bold));
            g.drawText(msg.anexo.fromLastOccurrenceOf(".", false, false).toUpperCase().substring(0, 4), ic.toNearestInt(), juce::Justification::centred);
            g.setColour(l.ausente ? tk.textoTerciario : corTexto);
            g.setFont(fonteUi(tk.tamanhoFonteCorpo + 0.5f, juce::Font::bold));
            g.drawText(msg.anexo, area.withTrimmedLeft(52.0f).withTrimmedRight(8.0f).removeFromTop(kDocH * 0.55f), juce::Justification::bottomLeft, true);
            g.setColour(corHora);
            g.setFont(fonteUi(tk.tamanhoFontePequena));
            g.drawText(l.ausente ? t("light.wa.arquivoAusente") : juce::String(), area.withTrimmedLeft(52.0f).withTrimmedTop(kDocH * 0.55f), juce::Justification::topLeft, true);
            if (!l.ausente) impl_->acertos.push_back({Acerto::Tipo::Midia, area.toNearestInt(), l.iMsg, 0});
            if (msg.legenda.isNotEmpty()) desenharTexto(g, m, x + kPadH, area.getBottom() + 4.0f, corTexto, l.iMsg, base);
            desenharHora(x + m.larg - kPadH + 2.0f, y + h - 2.0f, corHora);
            break;
        }
        default: break;
    }
}

} // namespace matriz::ui
