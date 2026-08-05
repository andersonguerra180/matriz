#include "TimelineComponent.h"

#include "../I18n/Strings.h"
#include "FormatoTempo.h"
#include "Tokens.h"

#include <algorithm>
#include <cmath>

namespace matriz::ui {

namespace {

std::string autorAtual() { return juce::SystemStats::getFullUserName().toStdString(); }

// Mesmo dispositivo compartilhado do PreviewComponent: só abre de verdade
// quando alguém aperta play, nunca só por existir (o harness headless tira
// snapshot sem precisar de placa de som).
juce::AudioDeviceManager& dispositivoCompartilhado() {
    static juce::AudioDeviceManager dm;
    return dm;
}
juce::AudioSourcePlayer& playerCompartilhado() {
    static juce::AudioSourcePlayer p;
    return p;
}
void garantirDispositivo() {
    static bool aberto = false;
    if (aberto) return;
    dispositivoCompartilhado().initialiseWithDefaultDevices(0, 2);
    dispositivoCompartilhado().addAudioCallback(&playerCompartilhado());
    aberto = true;
}

} // namespace

TimelineComponent::TimelineComponent(ProjetoAberto& projeto) : projeto_(projeto) {
    formatManager_.registerBasicFormats();
    onda_ = std::make_unique<juce::AudioThumbnail>(512, formatManager_, cacheOnda_);
    onda_->addChangeListener(nullptr);
    setWantsKeyboardFocus(true);

    auto criar = [this](std::unique_ptr<juce::TextButton>& destino, const char* chave, std::function<void()> acao) {
        destino = std::make_unique<juce::TextButton>(matriz::i18n::t(chave));
        destino->onClick = std::move(acao);
        addAndMakeVisible(*destino);
    };
    criar(botaoInicio_, "transporte.inicio", [this] { irParaInicio(); });
    criar(botaoPlayStop_, "transporte.play", [this] { alternarPlayStop(); });
    criar(botaoFim_, "transporte.fim", [this] { irParaFim(); });
    criar(botaoModoRoda_, "transporte.jog", [this] { alternarModoRoda(); });
    criar(botaoZoomTudo_, "timeline.zoom_tudo", [this] { zoomArquivoInteiro(); });
    criar(botaoTimecode_, "timeline.timecode", [this] { definirMostrarTimecode(!mostrarTimecode_); });
    criar(botaoMarcador_, "timeline.marcador", [this] { inserirMarcadorNaPosicaoAtual(); });

    startTimerHz(30); // cursor em tempo real
}

TimelineComponent::~TimelineComponent() {
    stopTimer();
    transporte_.setSource(nullptr);
    playerCompartilhado().setSource(nullptr);
}

bool TimelineComponent::carregarItem(const std::string& itemId, const juce::File& arquivo) {
    descarregar();
    itemId_ = itemId;
    arquivo_ = arquivo;

    std::unique_ptr<juce::AudioFormatReader> leitor(formatManager_.createReaderFor(arquivo));
    if (leitor == nullptr) return false;

    sampleRate_ = leitor->sampleRate > 0 ? leitor->sampleRate : 44100.0;
    duracao_ = static_cast<double>(leitor->lengthInSamples) / sampleRate_;
    onda_->setSource(new juce::FileInputSource(arquivo));

    fonte_ = std::make_unique<juce::AudioFormatReaderSource>(leitor.release(), true);
    transporte_.setSource(fonte_.get(), 0, nullptr, sampleRate_);

    zoomArquivoInteiro();
    recarregarMarcadores();
    resized();
    repaint();
    return true;
}

void TimelineComponent::descarregar() {
    parar();
    transporte_.setSource(nullptr);
    fonte_.reset();
    onda_->clear();
    itemId_.clear();
    arquivo_ = juce::File();
    duracao_ = 0.0;
    inicioVisivel_ = fimVisivel_ = 0.0;
    marcadores_.clear();
    fecharEditorInline(false);
    repaint();
}

// --- transporte ---------------------------------------------------------------

void TimelineComponent::tocar() {
    if (fonte_ == nullptr) return;
    garantirDispositivo();
    playerCompartilhado().setSource(&transporte_);
    transporte_.start();
    if (botaoPlayStop_) botaoPlayStop_->setButtonText(matriz::i18n::t("transporte.stop"));
}

void TimelineComponent::parar() {
    transporte_.stop();
    velocidadeShuttle_ = 0.0;
    if (botaoPlayStop_) botaoPlayStop_->setButtonText(matriz::i18n::t("transporte.play"));
    if (aoMedirNivel) aoMedirNivel(0.0f, 0.0f);
}

void TimelineComponent::alternarPlayStop() {
    if (estaTocando()) parar();
    else tocar();
}

void TimelineComponent::irParaInicio() {
    posicionar(0.0);
}

void TimelineComponent::irParaFim() {
    posicionar(duracao_);
}

bool TimelineComponent::estaTocando() const { return transporte_.isPlaying(); }
double TimelineComponent::posicaoSegundos() const { return transporte_.getCurrentPosition(); }

void TimelineComponent::posicionar(double segundos) {
    transporte_.setPosition(juce::jlimit(0.0, std::max(0.0, duracao_), segundos));
    repaint();
}

// --- jog e shuttle ------------------------------------------------------------

void TimelineComponent::alternarModoRoda() {
    modoRoda_ = modoRoda_ == ModoRoda::Jog ? ModoRoda::Shuttle : ModoRoda::Jog;
    velocidadeShuttle_ = 0.0;
    transporte_.setSource(fonte_.get(), 0, nullptr, sampleRate_); // volta a 1x ao trocar de modo
    if (botaoModoRoda_)
        botaoModoRoda_->setButtonText(matriz::i18n::t(modoRoda_ == ModoRoda::Jog ? "transporte.jog" : "transporte.shuttle"));
    repaint();
}

void TimelineComponent::girarRoda(float delta) {
    if (fonte_ == nullptr) return;

    if (modoRoda_ == ModoRoda::Jog) {
        // Precisão de amostra (item 7.2): um clique de roda move um número
        // fixo de AMOSTRAS, não uma fração da tela — girar devagar de fato
        // anda amostra por amostra, que é o que dá a sensação de fita na
        // cabeça. Sem áudio contínuo no scrub: reposiciona e, se estava
        // tocando, continua tocando do novo ponto (o "scrub com áudio" real
        // exigiria resampling em tempo real, o que este transporte não faz —
        // declarado, não fingido).
        const double amostrasPorClique = 512.0;
        double novaPos = posicaoSegundos() + static_cast<double>(delta) * amostrasPorClique / sampleRate_;
        posicionar(novaPos);
    } else {
        // Shuttle contínuo ±16x com detent no zero (item 7.2).
        velocidadeShuttle_ = juce::jlimit(-16.0, 16.0, velocidadeShuttle_ + static_cast<double>(delta) * 0.5);
        if (std::abs(velocidadeShuttle_) < 0.25) velocidadeShuttle_ = 0.0; // detent
        if (velocidadeShuttle_ == 0.0) {
            parar();
        } else {
            garantirDispositivo();
            playerCompartilhado().setSource(&transporte_);
            // Velocidade por reamostragem da fonte: o transporte reproduz na
            // razão sampleRate/velocidade. Negativo não é suportado pelo
            // AudioTransportSource, então shuttle pra trás avança a posição
            // manualmente no timer.
            if (velocidadeShuttle_ > 0.0) {
                transporte_.setSource(fonte_.get(), 0, nullptr, sampleRate_ / velocidadeShuttle_);
                transporte_.start();
            } else {
                transporte_.stop();
            }
        }
    }
    repaint();
}

// --- zoom e régua -------------------------------------------------------------

void TimelineComponent::zoomArquivoInteiro() {
    inicioVisivel_ = 0.0;
    fimVisivel_ = std::max(0.001, duracao_);
    repaint();
}

void TimelineComponent::aplicarZoom(double fator, double centroSegundos) {
    if (duracao_ <= 0.0) return;
    double janela = (fimVisivel_ - inicioVisivel_) / fator;
    // Piso do zoom: uma janela de 10 amostras — é o "detalhe de amostra" do
    // item 8.1, sem permitir uma janela de largura zero.
    double janelaMinima = 10.0 / sampleRate_;
    janela = juce::jlimit(janelaMinima, duracao_, janela);
    double centro = juce::jlimit(0.0, duracao_, centroSegundos);
    inicioVisivel_ = juce::jlimit(0.0, std::max(0.0, duracao_ - janela), centro - janela * 0.5);
    fimVisivel_ = inicioVisivel_ + janela;
    repaint();
}

void TimelineComponent::definirMostrarTimecode(bool sim) {
    mostrarTimecode_ = sim;
    repaint();
}

juce::String TimelineComponent::formatarPosicao(double segundos) const {
    if (!mostrarTimecode_) return matriz::ui::formatarMinutagem(static_cast<int64_t>(segundos * 1000.0));
    // Timecode HH:MM:SS:FF a 25 fps — o padrão de trabalho de arquivo em
    // material europeu/brasileiro; fps real de vídeo entra quando a timeline
    // de vídeo tiver o próprio fps declarado (item 11.5 do SPEC).
    int total = static_cast<int>(segundos);
    int frames = static_cast<int>((segundos - total) * 25.0);
    return juce::String::formatted("%02d:%02d:%02d:%02d", total / 3600, (total % 3600) / 60, total % 60, frames);
}

// --- marcadores ---------------------------------------------------------------

void TimelineComponent::recarregarMarcadores() {
    marcadores_.clear();
    if (itemId_.empty()) return;
    // A MESMA tabela que a ficha lê (item 9.2) — não há cópia nem
    // sincronização: os dois painéis leem item_observacao.
    for (auto& obs : projeto_.observacoesDoItem(itemId_)) {
        if (!obs.minutagemMs) continue; // observação sem minutagem não é marcador de timeline
        marcadores_.push_back({obs.id, static_cast<double>(*obs.minutagemMs) / 1000.0, juce::String(obs.texto)});
    }
    std::sort(marcadores_.begin(), marcadores_.end(),
               [](const Marcador& a, const Marcador& b) { return a.segundos < b.segundos; });
    repaint();
}

void TimelineComponent::inserirMarcadorNaPosicaoAtual() {
    if (itemId_.empty()) return;
    double pos = posicaoSegundos();
    // Grava já — o marcador existe a partir do instante da tecla, mesmo que
    // o operador não escreva nada (item 8.2: a tecla crava o ponto; o texto
    // vem depois, sem interromper a reprodução).
    std::string id = projeto_.adicionarObservacao(itemId_, std::string(), static_cast<int64_t>(pos * 1000.0), autorAtual());
    recarregarMarcadores();
    if (aoMudarMarcadores) aoMudarMarcadores();

    for (size_t i = 0; i < marcadores_.size(); ++i)
        if (marcadores_[i].observacaoId == id) {
            abrirEditorInline(static_cast<int>(i));
            break;
        }
}

void TimelineComponent::abrirEditorInline(int indiceMarcador) {
    if (indiceMarcador < 0 || indiceMarcador >= static_cast<int>(marcadores_.size())) return;
    indiceEditando_ = indiceMarcador;

    editorInline_ = std::make_unique<juce::TextEditor>();
    editorInline_->setText(marcadores_[static_cast<size_t>(indiceMarcador)].texto, false);
    editorInline_->setTextToShowWhenEmpty(matriz::i18n::t("timeline.marcador_dica"), tema().textoTerciario);
    // Enter fecha, sem modal e sem sair da reprodução (item 8.2).
    editorInline_->onReturnKey = [this] { fecharEditorInline(true); };
    editorInline_->onEscapeKey = [this] { fecharEditorInline(false); };
    editorInline_->onFocusLost = [this] { fecharEditorInline(true); };
    addAndMakeVisible(*editorInline_);
    resized();
    editorInline_->grabKeyboardFocus();
}

void TimelineComponent::fecharEditorInline(bool gravando) {
    if (editorInline_ == nullptr) {
        indiceEditando_ = -1;
        return;
    }
    if (gravando && indiceEditando_ >= 0 && indiceEditando_ < static_cast<int>(marcadores_.size())) {
        auto& m = marcadores_[static_cast<size_t>(indiceEditando_)];
        juce::String texto = editorInline_->getText().trim();
        projeto_.atualizarObservacao(m.observacaoId, texto.toStdString(),
                                      static_cast<int64_t>(m.segundos * 1000.0));
        m.texto = texto;
        if (aoMudarMarcadores) aoMudarMarcadores();
    }
    // Destruir de dentro de um callback do próprio editor (onFocusLost) é
    // reentrada — some depois, na fila de mensagens.
    auto morto = std::move(editorInline_);
    editorInline_.reset();
    indiceEditando_ = -1;
    juce::MessageManager::callAsync([morto = std::shared_ptr<juce::TextEditor>(morto.release())] {});
    repaint();
}

// --- geometria ----------------------------------------------------------------

juce::Rectangle<int> TimelineComponent::areaReguaLocal() const {
    return getLocalBounds().withTrimmedBottom(kAlturaTransporte).removeFromTop(kAlturaRegua);
}

juce::Rectangle<int> TimelineComponent::areaMarcadores() const {
    auto a = getLocalBounds().withTrimmedBottom(kAlturaTransporte);
    a.removeFromTop(kAlturaRegua);
    return a.removeFromTop(kAlturaFaixaMarcadores);
}

juce::Rectangle<int> TimelineComponent::areaOnda() const {
    auto a = getLocalBounds().withTrimmedBottom(kAlturaTransporte);
    a.removeFromTop(kAlturaRegua + kAlturaFaixaMarcadores);
    return a;
}

double TimelineComponent::segundosNoX(int x) const {
    auto a = areaOnda();
    if (a.getWidth() <= 0) return 0.0;
    double frac = static_cast<double>(x - a.getX()) / static_cast<double>(a.getWidth());
    return inicioVisivel_ + frac * (fimVisivel_ - inicioVisivel_);
}

int TimelineComponent::xDoSegundo(double s) const {
    auto a = areaOnda();
    double janela = fimVisivel_ - inicioVisivel_;
    if (janela <= 0.0) return a.getX();
    double frac = (s - inicioVisivel_) / janela;
    return a.getX() + static_cast<int>(frac * static_cast<double>(a.getWidth()));
}

int TimelineComponent::indiceMarcadorNoX(int x) const {
    for (size_t i = 0; i < marcadores_.size(); ++i)
        if (std::abs(xDoSegundo(marcadores_[i].segundos) - x) <= 5) return static_cast<int>(i);
    return -1;
}

// --- desenho ------------------------------------------------------------------

void TimelineComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    // Régua de tempo (item 8.1) — divisões escolhidas pra caber ~8 marcas na
    // janela visível, seja ela o arquivo inteiro ou 10 amostras.
    auto regua = areaReguaLocal();
    g.setColour(tk.painelAlt);
    g.fillRect(regua);
    g.setColour(tk.textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 3.0f)));
    double janela = fimVisivel_ - inicioVisivel_;
    if (janela > 0.0) {
        double passoBruto = janela / 8.0;
        static const double passos[] = {0.001, 0.01, 0.1, 0.5, 1, 5, 10, 30, 60, 300, 600, 1800, 3600};
        double passo = passos[0];
        for (double p : passos)
            if (p <= passoBruto) passo = p;
        for (double t = std::ceil(inicioVisivel_ / passo) * passo; t <= fimVisivel_; t += passo) {
            int x = xDoSegundo(t);
            g.drawVerticalLine(x, static_cast<float>(regua.getBottom() - 5), static_cast<float>(regua.getBottom()));
            g.drawText(formatarPosicao(t), x + 2, regua.getY(), 80, regua.getHeight() - 4,
                        juce::Justification::centredLeft, false);
        }
    }

    // Forma de onda na largura do painel (item 8.1).
    auto onda = areaOnda();
    g.setColour(tk.fundo);
    g.fillRect(onda);
    if (onda_ != nullptr && onda_->getTotalLength() > 0.0) {
        g.setColour(tk.acento.withAlpha(0.85f));
        onda_->drawChannels(g, onda.reduced(0, 2), inicioVisivel_, fimVisivel_, 1.0f);
    } else {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(matriz::i18n::t("timeline.sem_onda"), onda, juce::Justification::centred);
    }

    // Faixa de marcadores + o texto de cada um.
    auto faixa = areaMarcadores();
    g.setColour(tk.painelAlt);
    g.fillRect(faixa);
    for (size_t i = 0; i < marcadores_.size(); ++i) {
        int x = xDoSegundo(marcadores_[i].segundos);
        if (x < faixa.getX() - 20 || x > faixa.getRight() + 20) continue;
        bool arrastando = indiceArrastado_ == static_cast<int>(i);
        g.setColour(arrastando ? tk.alerta : tk.estadoAlerta);
        g.fillRect(x - 1, faixa.getY(), 3, faixa.getHeight());
        g.drawVerticalLine(x, static_cast<float>(onda.getY()), static_cast<float>(onda.getBottom()));
        if (marcadores_[i].texto.isNotEmpty()) {
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 3.0f)));
            g.drawText(marcadores_[i].texto, x + 4, faixa.getY(), 140, faixa.getHeight(),
                        juce::Justification::centredLeft, true);
        }
    }

    // Cursor de reprodução em tempo real (item 8.1).
    double pos = posicaoSegundos();
    if (pos >= inicioVisivel_ && pos <= fimVisivel_) {
        int x = xDoSegundo(pos);
        g.setColour(tk.perigo);
        g.drawVerticalLine(x, static_cast<float>(regua.getY()), static_cast<float>(onda.getBottom()));
    }

    // Posição/velocidade em texto, no canto do transporte.
    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    juce::String estado = formatarPosicao(pos) + " / " + formatarPosicao(duracao_);
    if (modoRoda_ == ModoRoda::Shuttle && velocidadeShuttle_ != 0.0)
        estado += "   " + juce::String(velocidadeShuttle_, 1) + juce::String::fromUTF8("×");
    g.drawText(estado, getLocalBounds().removeFromBottom(kAlturaTransporte).reduced(6, 0),
                juce::Justification::centredRight, false);
}

void TimelineComponent::resized() {
    auto barra = getLocalBounds().removeFromBottom(kAlturaTransporte).reduced(4, 3);
    auto botao = [&barra](juce::TextButton* b, int largura) {
        if (b) b->setBounds(barra.removeFromLeft(largura));
        barra.removeFromLeft(3);
    };
    botao(botaoInicio_.get(), 40);
    botao(botaoPlayStop_.get(), 64);
    botao(botaoFim_.get(), 40);
    botao(botaoModoRoda_.get(), 76);
    botao(botaoZoomTudo_.get(), 76);
    botao(botaoTimecode_.get(), 88);
    botao(botaoMarcador_.get(), 96);

    if (editorInline_ != nullptr && indiceEditando_ >= 0 && indiceEditando_ < static_cast<int>(marcadores_.size())) {
        int x = xDoSegundo(marcadores_[static_cast<size_t>(indiceEditando_)].segundos);
        auto faixa = areaMarcadores();
        editorInline_->setBounds(juce::jlimit(0, std::max(0, getWidth() - 180), x + 4), faixa.getY(), 176,
                                  faixa.getHeight());
    }
}

// --- interação ----------------------------------------------------------------

void TimelineComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();

    if (areaMarcadores().contains(e.getPosition())) {
        int i = indiceMarcadorNoX(e.getPosition().x);
        if (i >= 0) {
            if (e.mods.isPopupMenu()) {
                juce::PopupMenu menu;
                menu.addItem(1, matriz::i18n::t("timeline.marcador_editar"));
                menu.addItem(2, matriz::i18n::t("timeline.marcador_apagar"));
                menu.showMenuAsync(juce::PopupMenu::Options(), [this, i](int r) {
                    if (r == 1) abrirEditorInline(i);
                    else if (r == 2) apagarMarcadorParaTeste(i);
                });
                return;
            }
            indiceArrastado_ = i; // arrastável pra corrigir a posição (item 8.2)
            return;
        }
    }

    // Clique na timeline posiciona (item 8.1).
    if (areaOnda().contains(e.getPosition()) || areaReguaLocal().contains(e.getPosition()))
        posicionar(segundosNoX(e.getPosition().x));
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& e) {
    if (indiceArrastado_ >= 0 && indiceArrastado_ < static_cast<int>(marcadores_.size())) {
        marcadores_[static_cast<size_t>(indiceArrastado_)].segundos =
            juce::jlimit(0.0, std::max(0.0, duracao_), segundosNoX(e.getPosition().x));
        repaint();
        return;
    }
    if (areaOnda().contains(e.getPosition())) posicionar(segundosNoX(e.getPosition().x));
}

void TimelineComponent::mouseUp(const juce::MouseEvent&) {
    if (indiceArrastado_ < 0) return;
    auto& m = marcadores_[static_cast<size_t>(indiceArrastado_)];
    projeto_.atualizarObservacao(m.observacaoId, m.texto.toStdString(), static_cast<int64_t>(m.segundos * 1000.0));
    indiceArrastado_ = -1;
    recarregarMarcadores(); // reordena por tempo depois de mover
    if (aoMudarMarcadores) aoMudarMarcadores();
}

void TimelineComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& roda) {
    // Cmd/Ctrl + roda = zoom; roda sozinha = jog/shuttle, atrelada ao scroll
    // ring do mouse (item 7.2).
    if (e.mods.isCommandDown()) {
        aplicarZoom(roda.deltaY > 0 ? 1.25 : 0.8, segundosNoX(e.getPosition().x));
        return;
    }
    girarRoda(roda.deltaY * 10.0f);
}

bool TimelineComponent::keyPressed(const juce::KeyPress& k) {
    // Barra de espaço alterna play e stop (item 7.1).
    if (k.getKeyCode() == juce::KeyPress::spaceKey) {
        alternarPlayStop();
        return true;
    }
    // Tecla única crava o marcador no ponto atual (item 8.2).
    if (k.getKeyCode() == 'M' || k.getKeyCode() == 'm') {
        inserirMarcadorNaPosicaoAtual();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::homeKey) {
        irParaInicio();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::endKey) {
        irParaFim();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey) {
        girarRoda(k.getKeyCode() == juce::KeyPress::rightKey ? 1.0f : -1.0f);
        return true;
    }
    if (k.getTextCharacter() == 'j' || k.getTextCharacter() == 'J') {
        alternarModoRoda();
        return true;
    }
    return false;
}

void TimelineComponent::timerCallback() {
    if (fonte_ == nullptr) return;

    // Shuttle pra trás: o AudioTransportSource não toca invertido, então a
    // posição anda manualmente (sem áudio) — comportamento declarado.
    if (modoRoda_ == ModoRoda::Shuttle && velocidadeShuttle_ < 0.0)
        posicionar(posicaoSegundos() + velocidadeShuttle_ / 30.0);

    // Acompanha o cursor com a janela visível quando ele sai dela tocando —
    // senão o operador perde o cursor de vista com zoom fechado.
    double pos = posicaoSegundos();
    if (estaTocando() && (pos < inicioVisivel_ || pos > fimVisivel_)) {
        double janela = fimVisivel_ - inicioVisivel_;
        inicioVisivel_ = juce::jlimit(0.0, std::max(0.0, duracao_ - janela), pos);
        fimVisivel_ = inicioVisivel_ + janela;
    }

    if (aoMedirNivel && estaTocando()) {
        // Nível aproximado do trecho tocando, lido da própria forma de onda
        // já em cache — não abre uma segunda leitura do arquivo por frame.
        float minE = 0, maxE = 0, minD = 0, maxD = 0;
        onda_->getApproximateMinMax(pos, pos + 1.0 / 30.0, 0, minE, maxE);
        if (onda_->getNumChannels() > 1) onda_->getApproximateMinMax(pos, pos + 1.0 / 30.0, 1, minD, maxD);
        else { minD = minE; maxD = maxE; }
        aoMedirNivel(std::max(std::abs(minE), std::abs(maxE)), std::max(std::abs(minD), std::abs(maxD)));
    }

    repaint();
}

// --- introspecção pra teste ---------------------------------------------------

std::vector<TimelineComponent::MarcadorVisivel> TimelineComponent::marcadoresParaTeste() const {
    std::vector<MarcadorVisivel> out;
    for (auto& m : marcadores_) out.push_back({m.observacaoId, m.segundos, m.texto});
    return out;
}

void TimelineComponent::editarTextoDoMarcadorParaTeste(int indice, const juce::String& texto) {
    if (indice < 0 || indice >= static_cast<int>(marcadores_.size())) return;
    auto& m = marcadores_[static_cast<size_t>(indice)];
    projeto_.atualizarObservacao(m.observacaoId, texto.toStdString(), static_cast<int64_t>(m.segundos * 1000.0));
    m.texto = texto;
    if (aoMudarMarcadores) aoMudarMarcadores();
    repaint();
}

void TimelineComponent::arrastarMarcadorParaTeste(int indice, double novosSegundos) {
    if (indice < 0 || indice >= static_cast<int>(marcadores_.size())) return;
    auto& m = marcadores_[static_cast<size_t>(indice)];
    m.segundos = juce::jlimit(0.0, std::max(0.0, duracao_), novosSegundos);
    projeto_.atualizarObservacao(m.observacaoId, m.texto.toStdString(), static_cast<int64_t>(m.segundos * 1000.0));
    recarregarMarcadores();
    if (aoMudarMarcadores) aoMudarMarcadores();
}

void TimelineComponent::apagarMarcadorParaTeste(int indice) {
    if (indice < 0 || indice >= static_cast<int>(marcadores_.size())) return;
    projeto_.removerObservacao(marcadores_[static_cast<size_t>(indice)].observacaoId);
    recarregarMarcadores();
    if (aoMudarMarcadores) aoMudarMarcadores();
}

} // namespace matriz::ui
