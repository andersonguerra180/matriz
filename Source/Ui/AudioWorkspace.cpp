#include "AudioWorkspace.h"

#include <cmath>

#include "../Diag/NSExceptionGuard.h"
#include "../I18n/Strings.h"
#include "../Ingest/CacheArquivo.h"
#include "../Vault/Reconciliacao.h"
#include "../Vault/Resolucao.h"
#include "FormatoTempo.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

// Marcador lido do banco, já com a cor do tipo resolvida. Cor vem de
// `tipo_marcador`, não de um switch no código: acrescentar um tipo de
// marcador é inserir uma linha, não recompilar (§7).
struct MarcadorNaRegua {
    std::string id;             // pra gravar a edição de volta no banco
    double inicio = 0.0;
    std::optional<double> fim;  // trecho quando presente, ponto quando não
    juce::String titulo;
    juce::Colour cor;
};

juce::Colour corDeTexto(const std::string& hex, juce::Colour padrao) {
    juce::String s(hex);
    if (!s.startsWith("#") || s.length() < 7) return padrao;
    return juce::Colour::fromString("ff" + s.substring(1));
}

juce::String dbComoTexto(double db) {
    if (db <= -144.0) return "-inf";
    return juce::String(db, 1);
}

} // namespace

// ---------------------------------------------------------------------------
// Forma de onda + régua de marcadores + região de loop + agulha
// ---------------------------------------------------------------------------
class VisualizadorFormaOnda : public juce::Component {
public:
    explicit VisualizadorFormaOnda(matriz::audio::MotorReproducao& motor) : motor_(motor) {}

    void definirFormaDeOnda(std::vector<float> minimos, std::vector<float> maximos, double duracao) {
        minimos_ = std::move(minimos);
        maximos_ = std::move(maximos);
        duracao_ = duracao;
        repaint();
    }

    void definirMarcadores(std::vector<MarcadorNaRegua> marcadores) {
        marcadores_ = std::move(marcadores);
        repaint();
    }

    void limpar() {
        minimos_.clear();
        maximos_.clear();
        marcadores_.clear();
        duracao_ = 0.0;
        repaint();
    }

    bool temFormaDeOnda() const { return !minimos_.empty(); }
    int totalMarcadores() const { return static_cast<int>(marcadores_.size()); }
    double duracao() const { return duracao_; }

    static constexpr int kAlturaFaixaMarcadores = 20;

    juce::Rectangle<int> areaFaixaMarcadores() const {
        auto a = getLocalBounds();
        a.removeFromTop(AudioWorkspace::kAlturaRegua);
        return a.removeFromTop(kAlturaFaixaMarcadores);
    }

    juce::Rectangle<int> areaOnda() const {
        auto a = getLocalBounds();
        a.removeFromTop(AudioWorkspace::kAlturaRegua + kAlturaFaixaMarcadores);
        return a;
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        auto area = getLocalBounds();
        auto regua = area.removeFromTop(AudioWorkspace::kAlturaRegua);
        auto faixa = area.removeFromTop(kAlturaFaixaMarcadores);

        g.setColour(tk.painel);
        g.fillRect(regua);
        g.setColour(tk.painelAlt);
        g.fillRect(faixa);
        g.setColour(tk.painelAlt);
        g.fillRect(area);

        if (duracao_ <= 0.0) {
            g.setColour(tk.textoTerciario);
            g.setFont(tk.tamanhoFonteCorpo);
            g.drawText(matriz::i18n::t("escuta.sem_forma_onda"), area, juce::Justification::centred, true);
            return;
        }

        auto xDe = [&](double segundos) {
            return area.getX() + static_cast<float>(segundos / duracao_) * area.getWidth();
        };

        // Região de loop primeiro: é fundo, não pode cobrir a onda.
        if (motor_.loopAtivo()) {
            float x1 = xDe(motor_.loopInicio());
            float x2 = xDe(motor_.loopFim());
            g.setColour(tk.acento.withAlpha(0.18f));
            g.fillRect(juce::Rectangle<float>(x1, static_cast<float>(area.getY()), x2 - x1,
                                               static_cast<float>(area.getHeight())));
        }

        // Onda a partir do BLOB do cache.
        g.setColour(tk.acento);
        const int largura = area.getWidth();
        const float meio = area.getCentreY();
        const float escala = area.getHeight() * 0.5f;
        for (int x = 0; x < largura; ++x) {
            size_t idx = static_cast<size_t>(static_cast<double>(x) / largura * minimos_.size());
            if (idx >= minimos_.size()) break;
            float minimo = minimos_[idx];
            float maximo = maximos_[idx];
            g.drawVerticalLine(area.getX() + x, meio - maximo * escala, meio - minimo * escala);
        }

        // Marcadores: 3px na faixa de markers + linha fina pela onda + texto na faixa.
        for (size_t i = 0; i < marcadores_.size(); ++i) {
            const auto& m = marcadores_[i];
            float x1 = xDe(m.inicio);
            if (m.fim) {
                float x2 = xDe(*m.fim);
                g.setColour(m.cor.withAlpha(0.25f));
                g.fillRect(juce::Rectangle<float>(x1, static_cast<float>(area.getY()), juce::jmax(1.0f, x2 - x1),
                                                   static_cast<float>(area.getHeight())));
            }
            bool arrastando = indiceArrastado_ == static_cast<int>(i);
            g.setColour(arrastando ? tk.alerta : m.cor);
            g.fillRect(juce::Rectangle<float>(x1 - 1.0f, static_cast<float>(faixa.getY()), 3.0f,
                                               static_cast<float>(faixa.getHeight())));
            g.drawVerticalLine(static_cast<int>(x1), static_cast<float>(area.getY()),
                               static_cast<float>(area.getBottom()));
            if (m.titulo.isNotEmpty()) {
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 3.0f)));
                g.drawText(m.titulo, static_cast<int>(x1) + 4, faixa.getY(), 140, faixa.getHeight(),
                           juce::Justification::centredLeft, true);
            }
            if (m.fim) {
                float x2 = xDe(*m.fim);
                g.setColour(m.cor);
                g.fillRect(juce::Rectangle<float>(x2 - 2.0f, static_cast<float>(faixa.getY()), 2.0f,
                                                   static_cast<float>(faixa.getHeight())));
                for (float xa : {x1, x2}) {
                    g.fillRect(juce::Rectangle<float>(xa - 3.0f, static_cast<float>(faixa.getCentreY()) - 3.0f,
                                                       6.0f, 6.0f));
                }
            }
        }

        // Agulha por último: tem que ficar sobre tudo.
        float xAgulha = xDe(motor_.posicaoSegundos());
        g.setColour(tk.textoPrimario);
        g.fillRect(juce::Rectangle<float>(xAgulha, static_cast<float>(getY()), 1.5f,
                                           static_cast<float>(getHeight())));
    }

    // Gravar a edição de trecho. Ligado pelo AudioWorkspace, que é quem tem
    // o projeto — o visualizador não fala com banco.
    std::function<void(const std::string& marcadorId, double inicio, std::optional<double> fim)> aoEditarMarcador;
    std::function<void(const std::string& marcadorId)> aoRemoverMarcador;
    std::function<void(const std::string& marcadorId, double posSegundos)> aoPedirEditorInline;
    std::function<void()> aoReordenarMarcadores;

    void mouseDown(const juce::MouseEvent& e) override {
        if (duracao_ <= 0.0) return;

        auto faixa = areaFaixaMarcadores();
        bool naFaixaMarcadores = faixa.toFloat().contains(e.position);

        if (e.mods.isPopupMenu()) {
            int idx = indiceMarcadorNoX(e.position.x);
            if (idx >= 0) {
                const auto& m = marcadores_[static_cast<size_t>(idx)];
                juce::PopupMenu menu;
                menu.addItem(1, matriz::i18n::t("timeline.marcador_editar"));
                menu.addItem(2, matriz::i18n::t("timeline.marcador_apagar"));

                juce::Component::SafePointer<VisualizadorFormaOnda> safeThis(this);
                menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, m](int r) {
                    if (!safeThis) return;
                    if (r == 1) {
                        if (safeThis->aoPedirEditorInline)
                            safeThis->aoPedirEditorInline(m.id, m.inicio);
                    } else if (r == 2) {
                        if (safeThis->aoRemoverMarcador)
                            safeThis->aoRemoverMarcador(m.id);
                    }
                });
                return;
            }
        }

        if (naFaixaMarcadores) {
            // Range marker border dragging.
            if (agarrarBordaDeMarcador(e.position.x)) {
                repaint();
                return;
            }
            // Point marker dragging (like video timeline).
            int idx = indiceMarcadorNoX(e.position.x);
            if (idx >= 0) {
                indiceArrastado_ = idx;
                repaint();
                return;
            }
        }

        arrastando_ = true;
        ultimoX_ = e.position.x;
        motor_.irPara(segundosNoX(e.position.x));
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (duracao_ <= 0.0) return;

        if (marcadorEmEdicao_ >= 0) {
            auto& m = marcadores_[static_cast<size_t>(marcadorEmEdicao_)];
            double t = segundosNoX(e.position.x);
            if (editandoInicio_) {
                m.inicio = m.fim ? juce::jmin(t, *m.fim) : t;
            } else {
                m.fim = juce::jmax(t, m.inicio);
            }
            repaint();
            return;
        }

        if (indiceArrastado_ >= 0 && indiceArrastado_ < static_cast<int>(marcadores_.size())) {
            marcadores_[static_cast<size_t>(indiceArrastado_)].inicio =
                juce::jlimit(0.0, std::max(0.0, duracao_), segundosNoX(e.position.x));
            repaint();
            return;
        }

        if (!arrastando_) return;
        double deltaSegundos = (e.position.x - ultimoX_) / juce::jmax(1, getWidth()) * duracao_;
        ultimoX_ = e.position.x;
        motor_.jog(deltaSegundos);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (marcadorEmEdicao_ >= 0) {
            const auto& m = marcadores_[static_cast<size_t>(marcadorEmEdicao_)];
            if (aoEditarMarcador) aoEditarMarcador(m.id, m.inicio, m.fim);
            marcadorEmEdicao_ = -1;
            repaint();
            return;
        }
        if (indiceArrastado_ >= 0 && indiceArrastado_ < static_cast<int>(marcadores_.size())) {
            const auto& m = marcadores_[static_cast<size_t>(indiceArrastado_)];
            if (aoEditarMarcador) aoEditarMarcador(m.id, m.inicio, m.fim);
            indiceArrastado_ = -1;
            if (aoReordenarMarcadores) aoReordenarMarcadores();
            repaint();
            return;
        }
        if (!arrastando_) return;
        arrastando_ = false;
        motor_.encerrarJog();
        repaint();
    }

    void mouseMove(const juce::MouseEvent& e) override {
        auto faixa = areaFaixaMarcadores();
        bool naFaixa = faixa.toFloat().contains(e.position);
        bool sobreBorda = duracao_ > 0.0 && naFaixa &&
                          indiceDaBordaEm(e.position.x, nullptr) >= 0;
        setMouseCursor(sobreBorda ? juce::MouseCursor::LeftRightResizeCursor
                                   : juce::MouseCursor::NormalCursor);
    }

    int marcadorEmEdicaoParaTeste() const { return marcadorEmEdicao_; }

    // Introspecção pro harness: dirigir o arrasto sem mouse real.
    void editarBordaParaTeste(int indiceMarcador, bool inicio, double segundos) {
        if (indiceMarcador < 0 || indiceMarcador >= static_cast<int>(marcadores_.size())) return;
        marcadorEmEdicao_ = indiceMarcador;
        editandoInicio_ = inicio;
        auto& m = marcadores_[static_cast<size_t>(indiceMarcador)];
        if (inicio) m.inicio = m.fim ? juce::jmin(segundos, *m.fim) : segundos;
        else m.fim = juce::jmax(segundos, m.inicio);
        if (aoEditarMarcador) aoEditarMarcador(m.id, m.inicio, m.fim);
        marcadorEmEdicao_ = -1;
        repaint();
    }

    std::optional<double> fimDoMarcadorParaTeste(int indice) const {
        if (indice < 0 || indice >= static_cast<int>(marcadores_.size())) return std::nullopt;
        return marcadores_[static_cast<size_t>(indice)].fim;
    }

private:
    // Tolerância de agarre, em pixels. Uma borda de 2 px seria impossível de
    // pegar com o mouse; 6 px é o mínimo confortável sem roubar o clique de
    // navegação da onda.
    static constexpr float kToleranciaAgarrePx = 6.0f;

    double segundosNoX(float x) const {
        return juce::jlimit(0.0, duracao_, static_cast<double>(x) / juce::jmax(1, getWidth()) * duracao_);
    }

    float xDeSegundos(double segundos) const {
        if (duracao_ <= 0.0) return 0.0f;
        return static_cast<float>(segundos / duracao_) * getWidth();
    }

    int indiceMarcadorNoX(float x) const {
        for (int i = 0; i < static_cast<int>(marcadores_.size()); ++i) {
            const auto& m = marcadores_[static_cast<size_t>(i)];
            if (std::abs(xDeSegundos(m.inicio) - x) <= 8.0f) {
                return i;
            }
            if (m.fim && std::abs(xDeSegundos(*m.fim) - x) <= 8.0f) {
                return i;
            }
        }
        return -1;
    }

    // Índice do marcador cuja borda está a menos de kToleranciaAgarrePx de
    // `x`. `ehInicio` (opcional) diz qual das duas bordas foi encontrada.
    int indiceDaBordaEm(float x, bool* ehInicio) const {
        for (int i = 0; i < static_cast<int>(marcadores_.size()); ++i) {
            const auto& m = marcadores_[static_cast<size_t>(i)];
            // Só trecho tem borda pra arrastar: marcador de instante é um
            // ponto, e "esticar um ponto" não quer dizer nada.
            if (!m.fim) continue;
            if (std::abs(x - xDeSegundos(m.inicio)) <= kToleranciaAgarrePx) {
                if (ehInicio) *ehInicio = true;
                return i;
            }
            if (std::abs(x - xDeSegundos(*m.fim)) <= kToleranciaAgarrePx) {
                if (ehInicio) *ehInicio = false;
                return i;
            }
        }
        return -1;
    }

    bool agarrarBordaDeMarcador(float x) {
        bool inicio = false;
        int idx = indiceDaBordaEm(x, &inicio);
        if (idx < 0) return false;
        marcadorEmEdicao_ = idx;
        editandoInicio_ = inicio;
        return true;
    }

    matriz::audio::MotorReproducao& motor_;
    std::vector<float> minimos_, maximos_;
    std::vector<MarcadorNaRegua> marcadores_;
    double duracao_ = 0.0;
    bool arrastando_ = false;
    float ultimoX_ = 0.0f;
    int marcadorEmEdicao_ = -1;
    bool editandoInicio_ = false;
    int indiceArrastado_ = -1;
};

// ---------------------------------------------------------------------------
// Suíte DSP: VU/pico, correlação, vetorscópio, espectrograma
// ---------------------------------------------------------------------------
class MedidoresComponent : public juce::Component {
public:
    static constexpr int kOrdemFft = 10;              // 1024 pontos
    static constexpr int kTamanhoFft = 1 << kOrdemFft;

    MedidoresComponent() : fft_(kOrdemFft), janela_(kTamanhoFft, juce::dsp::WindowingFunction<float>::hann) {
        espectro_.assign(kTamanhoFft / 2, 0.0f);
        buffer_.assign(2 * kTamanhoFft, 0.0f);
    }

    // Chamado pelo Timer de 30 Hz da UI — nunca pelo audio callback.
    void atualizar(const matriz::audio::MedidasAoVivo& m, const std::vector<float>& e, const std::vector<float>& d) {
        medidas_ = m;

        // Vetorscópio: subamostra pra ~512 pontos. Desenhar milhares de
        // pontos a 30 Hz custaria mais que a análise inteira.
        pontos_.clear();
        const int passo = juce::jmax(1, static_cast<int>(e.size()) / 512);
        for (size_t i = 0; i < e.size(); i += static_cast<size_t>(passo))
            pontos_.push_back({e[i], i < d.size() ? d[i] : e[i]});

        if (!e.empty()) {
            for (size_t i = 0; i < e.size() && i < static_cast<size_t>(kTamanhoFft); ++i)
                buffer_[i] = 0.5f * (e[i] + (i < d.size() ? d[i] : e[i]));

            janela_.multiplyWithWindowingTable(buffer_.data(), kTamanhoFft);
            fft_.performFrequencyOnlyForwardTransform(buffer_.data());

            // Decaimento: sem ele o espectrograma pisca a cada quadro e não
            // dá pra ler nada.
            for (int i = 0; i < kTamanhoFft / 2; ++i) {
                float novo = buffer_[static_cast<size_t>(i)];
                espectro_[static_cast<size_t>(i)] = juce::jmax(novo, espectro_[static_cast<size_t>(i)] * 0.82f);
            }
        }
        repaint();
    }

    void limpar() {
        medidas_ = {};
        pontos_.clear();
        std::fill(espectro_.begin(), espectro_.end(), 0.0f);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        auto area = getLocalBounds().reduced(tk.espacoPequeno);
        g.setColour(tk.painelAlt);
        g.fillRect(getLocalBounds());

        int largura = area.getWidth() / 4;
        desenharVu(g, area.removeFromLeft(largura).reduced(4));
        desenharCorrelacao(g, area.removeFromLeft(largura).reduced(4));
        desenharVetorscopio(g, area.removeFromLeft(largura).reduced(4));
        desenharEspectro(g, area.reduced(4));
    }

private:
    void rotular(juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& chave) {
        const auto& tk = tema();
        g.setColour(tk.textoTerciario);
        g.setFont(tk.tamanhoFontePequena);
        g.drawText(matriz::i18n::t(chave), area.removeFromTop(14), juce::Justification::centredLeft, false);
    }

    void desenharVu(juce::Graphics& g, juce::Rectangle<int> area) {
        const auto& tk = tema();
        rotular(g, area, "escuta.vu");

        auto barra = [&](juce::Rectangle<int> r, float pico, float rms) {
            g.setColour(tk.fundo);
            g.fillRect(r);
            // RMS cheio, pico como traço: o traço é o que denuncia um
            // transiente que o RMS esconde.
            int alturaRms = static_cast<int>(juce::jlimit(0.0f, 1.0f, rms * 1.4f) * r.getHeight());
            g.setColour(pico > 0.98f ? tk.perigo : tk.acento);
            g.fillRect(r.withTop(r.getBottom() - alturaRms));
            int yPico = r.getBottom() - static_cast<int>(juce::jlimit(0.0f, 1.0f, pico) * r.getHeight());
            g.setColour(pico > 0.98f ? tk.perigo : tk.textoPrimario);
            g.fillRect(r.getX(), yPico, r.getWidth(), 2);
        };

        int meio = area.getWidth() / 2;
        barra(area.removeFromLeft(meio).reduced(2, 0), medidas_.picoEsquerda, medidas_.rmsEsquerda);
        barra(area.reduced(2, 0), medidas_.picoDireita, medidas_.rmsDireita);
    }

    void desenharCorrelacao(juce::Graphics& g, juce::Rectangle<int> area) {
        const auto& tk = tema();
        rotular(g, area, "escuta.correlacao");

        auto faixa = area.removeFromTop(18);
        g.setColour(tk.fundo);
        g.fillRect(faixa);

        // -1 à esquerda, +1 à direita, 0 no centro. Correlação negativa é o
        // sinal de cancelamento em mono — o que um arquivista precisa ver
        // antes de aprovar um master.
        float x = faixa.getX() + (medidas_.correlacao + 1.0f) * 0.5f * faixa.getWidth();
        g.setColour(medidas_.correlacao < 0.0f ? tk.perigo : tk.estadoQcOk);
        g.fillRect(juce::Rectangle<float>(x - 2.0f, static_cast<float>(faixa.getY()), 4.0f,
                                           static_cast<float>(faixa.getHeight())));
        g.setColour(tk.borda);
        g.drawVerticalLine(faixa.getCentreX(), static_cast<float>(faixa.getY()),
                           static_cast<float>(faixa.getBottom()));

        g.setColour(tk.textoSecundario);
        g.setFont(tk.tamanhoFontePequena);
        g.drawText(juce::String(medidas_.correlacao, 2), area.removeFromTop(14),
                   juce::Justification::centred, false);
    }

    void desenharVetorscopio(juce::Graphics& g, juce::Rectangle<int> area) {
        const auto& tk = tema();
        rotular(g, area, "escuta.vetorscopio");

        g.setColour(tk.fundo);
        g.fillRect(area);
        g.setColour(tk.borda);
        g.drawRect(area);

        auto centro = area.getCentre().toFloat();
        float escala = juce::jmin(area.getWidth(), area.getHeight()) * 0.45f;
        g.setColour(tk.acento.withAlpha(0.7f));
        for (const auto& p : pontos_) {
            // Rotação de 45°: em modo Lissajous, mono em fase vira uma linha
            // VERTICAL, que é a convenção que todo operador de áudio lê sem
            // pensar.
            float x = centro.x + (p.first - p.second) * escala;
            float y = centro.y - (p.first + p.second) * escala * 0.5f;
            g.fillRect(x, y, 1.5f, 1.5f);
        }
    }

    void desenharEspectro(juce::Graphics& g, juce::Rectangle<int> area) {
        const auto& tk = tema();
        rotular(g, area, "escuta.espectro");

        g.setColour(tk.fundo);
        g.fillRect(area);

        const int barras = juce::jmin(area.getWidth(), 128);
        for (int i = 0; i < barras; ++i) {
            // Eixo logarítmico: distribuição linear de bins joga quase tudo
            // no agudo e não mostra nada de útil no grave.
            float prop = static_cast<float>(i) / barras;
            int bin = static_cast<int>(std::pow(static_cast<float>(kTamanhoFft / 2), prop));
            bin = juce::jlimit(0, kTamanhoFft / 2 - 1, bin);

            float db = juce::Decibels::gainToDecibels(espectro_[static_cast<size_t>(bin)] / kTamanhoFft, -80.0f);
            float altura = juce::jlimit(0.0f, 1.0f, (db + 80.0f) / 80.0f);

            int x = area.getX() + i * area.getWidth() / barras;
            int larguraBarra = juce::jmax(1, area.getWidth() / barras);
            int h = static_cast<int>(altura * area.getHeight());
            g.setColour(tk.acento.withAlpha(0.35f + 0.65f * altura));
            g.fillRect(x, area.getBottom() - h, larguraBarra, h);
        }
    }

    matriz::audio::MedidasAoVivo medidas_;
    std::vector<std::pair<float, float>> pontos_;
    std::vector<float> espectro_;
    std::vector<float> buffer_;
    juce::dsp::FFT fft_;
    juce::dsp::WindowingFunction<float> janela_;
};

// ---------------------------------------------------------------------------
// AudioWorkspace
// ---------------------------------------------------------------------------

AudioWorkspace::AudioWorkspace(ProjetoAberto& projeto) : projeto_(projeto) {
    setWantsKeyboardFocus(true);

    forma_ = std::make_unique<VisualizadorFormaOnda>(motor_);
    forma_->aoEditarMarcador = [this](const std::string& marcadorId, double inicio,
                                       std::optional<double> /*fim*/) {
        if (marcadorId.empty()) return;
        try {
            auto obs = projeto_.observacoesDoItem(itemId_);
            for (auto& o : obs) {
                if (o.id == marcadorId) {
                    projeto_.atualizarObservacao(o.id, o.texto, static_cast<int64_t>(inicio * 1000.0));
                    break;
                }
            }
            recarregarMarcadores();
            sincronizarMarcadoresParaNotas();
            if (aoMudarMarcadores) aoMudarMarcadores();
        } catch (const std::exception&) {}
    };
    forma_->aoPedirEditorInline = [this](const std::string& id, double pos) {
        abrirEditorInlineMarcador(id, pos);
    };
    forma_->aoRemoverMarcador = [this](const std::string& id) {
        try {
            projeto_.removerObservacao(id);
            recarregarMarcadores();
            sincronizarMarcadoresParaNotas();
            if (aoMudarMarcadores) aoMudarMarcadores();
        } catch (const std::exception&) {}
    };
    forma_->aoReordenarMarcadores = [this] {
        recarregarMarcadores();
        sincronizarMarcadoresParaNotas();
        if (aoMudarMarcadores) aoMudarMarcadores();
    };
    addAndMakeVisible(*forma_);

    medidores_ = std::make_unique<MedidoresComponent>();
    addAndMakeVisible(*medidores_);

    auto botao = [this](std::unique_ptr<juce::TextButton>& destino, const char* chave, std::function<void()> acao) {
        destino = std::make_unique<juce::TextButton>(matriz::i18n::t(chave));
        destino->onClick = std::move(acao);
        addAndMakeVisible(*destino);
    };

    botao(botaoTocar_, "escuta.tocar", [this] {
        if (motor_.estaTocando()) motor_.pausar();
        else motor_.tocar();
    });
    botao(botaoParar_, "escuta.parar", [this] { motor_.parar(); });
    botao(botaoLoopIn_, "escuta.loop_in", [this] { motor_.definirLoopInicio(motor_.posicaoSegundos()); });
    botao(botaoLoopOut_, "escuta.loop_out", [this] { motor_.definirLoopFim(motor_.posicaoSegundos()); });
    botao(botaoLimparLoop_, "escuta.loop_limpar", [this] { motor_.limparLoop(); });
    botao(botaoFechar_, "escuta.fechar", [this] { if (aoFechar) aoFechar(); });
    botao(botaoAdicionarMarcador_, "escuta.adicionar_marcador", [this] { adicionarMarcadorNaPosicaoAtual(); });

    // 30 Hz (§7): rápido o bastante pra o VU parecer contínuo, devagar o
    // bastante pra não competir com a pintura do resto da janela.
    startTimerHz(30);
}

AudioWorkspace::~AudioWorkspace() {
    stopTimer();
    if (dispositivoAberto_) dispositivos_.removeAudioCallback(&motor_);
    motor_.descarregar();
}

void AudioWorkspace::abrirDispositivoDeAudio() {
    matriz::diag::breadcrumb("abrirDispositivoDeAudio");
    if (dispositivoAberto_) return;
    // 0 entradas: a estação só REPRODUZ. Pedir entrada dispararia o aviso de
    // permissão de microfone do macOS por nada — e captura ao vivo está
    // explicitamente fora de escopo (§1).
    juce::String erro = dispositivos_.initialiseWithDefaultDevices(0, 2);
    if (erro.isNotEmpty()) return;  // sem placa: o resto da tela continua
    dispositivos_.addAudioCallback(&motor_);
    dispositivoAberto_ = true;
}

bool AudioWorkspace::temFormaOndaParaTeste() const { return forma_ && forma_->temFormaDeOnda(); }
int AudioWorkspace::totalMarcadoresParaTeste() const { return forma_ ? forma_->totalMarcadores() : 0; }

void AudioWorkspace::editarBordaDeMarcadorParaTeste(int indiceMarcador, bool inicio, double segundos) {
    if (forma_) forma_->editarBordaParaTeste(indiceMarcador, inicio, segundos);
}

std::optional<double> AudioWorkspace::fimDoMarcadorParaTeste(int indiceMarcador) const {
    return forma_ ? forma_->fimDoMarcadorParaTeste(indiceMarcador) : std::nullopt;
}

void AudioWorkspace::carregarAsset(const ItemResumo& snap, std::optional<juce::File> arquivo,
                                    std::function<void()> aoConcluir) {
    descarregar();
    itemId_ = snap.id;

    auto info = projeto_.arquivoPrincipal(itemId_);
    if (!info) {
        textoRodape_ = matriz::i18n::t("escuta.sem_arquivo");
        if (aoConcluir) aoConcluir();
        repaint();
        return;
    }
    arquivoId_ = info->id;

    // 1) O que vem do cache — sempre, mesmo offline (I3). Este é o caminho
    //    que o critério 2 mede: forma de onda e ficha em menos de 100 ms com
    //    o volume desconectado, porque não há disco nenhum sendo tocado.
    auto analise = matriz::ingest::lerCache(projeto_.projeto().registro(), arquivoId_);
    if (analise && !analise->formaOnda.empty()) {
        auto onda = matriz::ingest::lerFormaDeOnda(analise->formaOnda);
        // 20 baldes por segundo é a resolução gravada (CacheArquivo.cpp).
        double duracao = onda.minimos.size() / 20.0;
        forma_->definirFormaDeOnda(std::move(onda.minimos), std::move(onda.maximos), duracao);
    }

    // 2) Marcadores, com a cor do tipo vinda do banco.
    recarregarMarcadores();

    // 3) Rodapé com os valores JÁ MEDIDOS na ingestão (I2). Nunca calculados
    //    aqui — é o rodapé que o critério 9 confere contra o Youlean.
    if (analise) {
        juce::StringArray partes;
        if (analise->lufsI) partes.add("LUFS-I " + juce::String(*analise->lufsI, 1));
        if (analise->lra) partes.add("LRA " + juce::String(*analise->lra, 1) + " LU");
        if (analise->truePeak) partes.add("TP " + dbComoTexto(*analise->truePeak) + " dBTP");
        if (analise->correlacaoMedia) partes.add("Corr " + juce::String(*analise->correlacaoMedia, 2));
        textoRodape_ = partes.joinIntoString("   ");
    }

    // 4) Só agora o transporte, que é a única parte que depende do arquivo
    //    estar acessível de verdade.
    juce::File alvo = arquivo.value_or(juce::File(info->caminhoAbsoluto));
    if (!alvo.existsAsFile()) {
        transporteHabilitado_ = false;
        avisoVault_ = matriz::i18n::t("escuta.vault_offline").replace("{local}", alvo.getFullPathName());
        if (aoConcluir) aoConcluir();
        resized();
        repaint();
        return;
    }

    avisoVault_.clear();
    abrirDispositivoDeAudio();
    // O callback vem de uma carga em background e é entregue depois, na
    // message thread: a esta altura a workspace pode ter sido destruída
    // (MainComponent e FloatingPreviewWindow recriam a estação de escuta).
    juce::Component::SafePointer<AudioWorkspace> safeThis(this);
    motor_.carregarAsync(alvo, [safeThis, aoConcluir](bool ok) {
        if (safeThis == nullptr) return;
        safeThis->transporteHabilitado_ = ok;
        if (!ok) safeThis->avisoVault_ = matriz::i18n::t("escuta.formato_nao_suportado");
        safeThis->resized();
        safeThis->repaint();
        if (aoConcluir) aoConcluir();
    });
}

void AudioWorkspace::descarregar() {
    motor_.descarregar();
    transporteHabilitado_ = false;
    itemId_.clear();
    arquivoId_.clear();
    textoRodape_.clear();
    avisoVault_.clear();
    if (forma_) forma_->limpar();
    if (medidores_) medidores_->limpar();
    repaint();
}

void AudioWorkspace::abrirEditorInlineMarcador(const std::string& observacaoId, double posSegundos) {
    observacaoIdEditando_ = observacaoId;
    posEditando_ = posSegundos;

    juce::String texto;
    for (auto& obs : projeto_.observacoesDoItem(itemId_)) {
        if (obs.id == observacaoId) {
            texto = juce::String(obs.texto);
            break;
        }
    }

    editorInlineMarcador_ = std::make_unique<juce::TextEditor>();
    editorInlineMarcador_->setText(texto, false);
    editorInlineMarcador_->setTextToShowWhenEmpty(matriz::i18n::t("timeline.marcador_dica"), tema().textoTerciario);
    editorInlineMarcador_->onReturnKey = [this] { fecharEditorInlineMarcador(true); };
    editorInlineMarcador_->onEscapeKey = [this] { fecharEditorInlineMarcador(false); };
    editorInlineMarcador_->onFocusLost = [this] { fecharEditorInlineMarcador(true); };
    addAndMakeVisible(*editorInlineMarcador_);

    resized();
    editorInlineMarcador_->grabKeyboardFocus();
}

void AudioWorkspace::fecharEditorInlineMarcador(bool gravar) {
    if (editorInlineMarcador_ == nullptr) return;

    if (gravar && !observacaoIdEditando_.empty()) {
        juce::String texto = editorInlineMarcador_->getText().trim();
        try {
            projeto_.atualizarObservacao(observacaoIdEditando_, texto.toStdString(),
                                          static_cast<int64_t>(posEditando_ * 1000.0));
            recarregarMarcadores();
            sincronizarMarcadoresParaNotas();
            if (aoMudarMarcadores) aoMudarMarcadores();
        } catch (...) {}
    }

    auto morto = std::move(editorInlineMarcador_);
    editorInlineMarcador_.reset();
    observacaoIdEditando_.clear();

    juce::MessageManager::callAsync([morto = std::shared_ptr<juce::TextEditor>(morto.release())] {});
    repaint();
}

void AudioWorkspace::timerCallback() {
    if (!transporteHabilitado_) return;

    std::vector<float> e, d;
    motor_.lerAmostrasRecentes(e, d, MedidoresComponent::kTamanhoFft);
    medidores_->atualizar(motor_.medidas(), e, d);
    if (forma_) forma_->repaint();
}

void AudioWorkspace::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds();

    area.removeFromBottom(kAlturaRodape);

    // Coloca o transporte na parte inferior!
    auto transporte = area.removeFromBottom(kAlturaTransporte).reduced(tk.espacoPequeno, 4);
    
    if (botaoFechar_) {
        botaoFechar_->setBounds(transporte.removeFromRight(84));
        botaoFechar_->setEnabled(true);
        transporte.removeFromRight(tk.espacoPequeno);
    }
    for (auto* b : {botaoTocar_.get(), botaoParar_.get(), botaoLoopIn_.get(), botaoLoopOut_.get(),
                     botaoLimparLoop_.get(), botaoAdicionarMarcador_.get()}) {
        if (b == nullptr) continue;
        b->setBounds(transporte.removeFromLeft(84));
        transporte.removeFromLeft(tk.espacoPequeno);
        b->setEnabled(transporteHabilitado_);
    }

    if (medidores_) medidores_->setBounds(area.removeFromBottom(kAlturaMedidores));
    if (forma_) {
        forma_->setBounds(area);
        if (editorInlineMarcador_ && forma_->duracao() > 0.0) {
            float x = static_cast<float>(posEditando_ / forma_->duracao()) * forma_->getWidth();
            auto faixa = forma_->areaFaixaMarcadores();
            editorInlineMarcador_->setBounds(
                juce::jlimit(0, std::max(0, getWidth() - 180), static_cast<int>(x) + 4),
                forma_->getY() + faixa.getY(),
                176,
                faixa.getHeight()
            );
        }
    }
}

void AudioWorkspace::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    auto rodape = getLocalBounds().removeFromBottom(kAlturaRodape).reduced(tk.espacoPequeno, 2);

    // Aviso de Vault offline no lugar das métricas, e não sobre a tela: I3
    // manda o resto continuar utilizável, então o aviso ocupa uma linha e
    // some — nunca um modal, nunca uma faixa fixa comendo altura útil.
    if (!avisoVault_.isEmpty()) {
        g.setColour(tk.alerta);
        g.setFont(tk.tamanhoFontePequena);
        g.drawText(avisoVault_, rodape, juce::Justification::centredLeft, true);
        return;
    }

    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena).withStyle("Bold")));
    g.drawText(textoRodape_, rodape, juce::Justification::centredLeft, true);

    if (motor_.foiTruncado()) {
        g.setColour(tk.alerta);
        g.drawText(matriz::i18n::t("escuta.arquivo_truncado"), rodape, juce::Justification::centredRight, true);
    }
}

bool AudioWorkspace::keyPressed(const juce::KeyPress& tecla) {
    // Escape sai mesmo sem transporte — ver a nota do botão Close.
    if (tecla == juce::KeyPress::escapeKey) {
        if (aoFechar) aoFechar();
        return true;
    }
    if (tecla == juce::KeyPress::leftKey || tecla == juce::KeyPress::pageUpKey) {
        if (aoNavegar) { aoNavegar(-1); return true; }
    }
    if (tecla == juce::KeyPress::rightKey || tecla == juce::KeyPress::pageDownKey) {
        if (aoNavegar) { aoNavegar(1); return true; }
    }
    if (!transporteHabilitado_) return false;

    // JKL de mesa de corte: L acelera pra frente em degraus, J acelera pra
    // trás nos mesmos degraus, K para na hora (§7).
    auto c = tecla.getTextCharacter();
    if (c == 'l' || c == 'L') { motor_.shuttleAvancar(); return true; }
    if (c == 'j' || c == 'J') { motor_.shuttleRetroceder(); return true; }
    if (c == 'k' || c == 'K') { motor_.shuttleParar(); return true; }
    if (c == 'i' || c == 'I') { motor_.definirLoopInicio(motor_.posicaoSegundos()); repaint(); return true; }
    if (c == 'o' || c == 'O') { motor_.definirLoopFim(motor_.posicaoSegundos()); repaint(); return true; }
    if (c == 'm' || c == 'M') { adicionarMarcadorNaPosicaoAtual(); return true; }

    if (tecla == juce::KeyPress::spaceKey) {
        if (motor_.estaTocando()) motor_.pausar();
        else motor_.tocar();
        return true;
    }
    return false;
}

void AudioWorkspace::simularTeclaParaTeste(int codigoTecla) {
    keyPressed(juce::KeyPress(codigoTecla, juce::ModifierKeys(), static_cast<juce::juce_wchar>(codigoTecla)));
}

void AudioWorkspace::recarregarMarcadores() {
    if (itemId_.empty()) return;
    std::vector<MarcadorNaRegua> marcadores;
    for (auto& obs : projeto_.observacoesDoItem(itemId_)) {
        if (!obs.minutagemMs) continue;
        MarcadorNaRegua m;
        m.inicio = static_cast<double>(*obs.minutagemMs) / 1000.0;
        m.titulo = juce::String(obs.texto);
        m.cor = tema().estadoAlerta;
        m.id = obs.id;
        marcadores.push_back(std::move(m));
    }
    std::sort(marcadores.begin(), marcadores.end(),
              [](const MarcadorNaRegua& a, const MarcadorNaRegua& b) { return a.inicio < b.inicio; });
    forma_->definirMarcadores(std::move(marcadores));
}

void AudioWorkspace::sincronizarMarcadoresParaNotas() {
    if (itemId_.empty()) return;

    std::string notas = projeto_.lerMetadado(itemId_, "notas_livres").value_or("");
    juce::StringArray linhas;
    linhas.addLines(notas);

    juce::StringArray novasLinhas;
    for (auto& linha : linhas) {
        juce::String l = linha.trim();
        if (ehLinhaDeMinutagemNotas(l)) continue;
        int primeiroColon = l.indexOfChar(':');
        if (l.startsWithChar('(') && primeiroColon > 0 && primeiroColon < 15 && l.endsWithChar(')'))
            continue;
        novasLinhas.add(linha);
    }

    for (auto& obs : projeto_.observacoesDoItem(itemId_)) {
        if (!obs.minutagemMs) continue;
        juce::String minutagem = formatarMinutagemNotas(static_cast<double>(*obs.minutagemMs) / 1000.0);
        juce::String textoMarcador = juce::String(obs.texto).trim();
        novasLinhas.add(minutagem + " - " + textoMarcador);
    }

    std::string resultado = novasLinhas.joinIntoString("\n").toStdString();
    projeto_.salvarMetadado(itemId_, "notas_livres", resultado);
}

void AudioWorkspace::adicionarMarcadorNaPosicaoAtual() {
    if (itemId_.empty()) return;
    double pos = motor_.posicaoSegundos();
    std::string autor = juce::SystemStats::getFullUserName().toStdString();
    std::string id = projeto_.adicionarObservacao(itemId_, std::string(), static_cast<int64_t>(pos * 1000.0), autor);
    recarregarMarcadores();
    sincronizarMarcadoresParaNotas();
    if (aoMudarMarcadores) aoMudarMarcadores();
    abrirEditorInlineMarcador(id, pos);
}

} // namespace matriz::ui
