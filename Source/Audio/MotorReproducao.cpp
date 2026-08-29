#include "MotorReproducao.h"

#include <cmath>
#include "../Diag/NSExceptionGuard.h"

namespace matriz::audio {

namespace {

// Constante de suavização do jog/shuttle: 50 ms (§7). Em vez de saltar pra
// a velocidade nova, a velocidade efetiva persegue o alvo — é o que
// transforma uma sequência de saltos de posição num scrub contínuo, sem
// clique a cada movimento do mouse.
constexpr double kSuavizacaoMs = 50.0;

float interpolar(const juce::AudioBuffer<float>& buffer, int canal, double posicao) {
    const int n = buffer.getNumSamples();
    if (n <= 0) return 0.0f;

    int i0 = static_cast<int>(std::floor(posicao));
    if (i0 < 0 || i0 >= n) return 0.0f;
    int i1 = juce::jmin(i0 + 1, n - 1);

    // Linear: o custo por amostra importa aqui (é audio callback), e a
    // diferença audível pra uma interpolação de ordem maior só apareceria em
    // razões de reamostragem extremas, que o shuttle não usa.
    float fracao = static_cast<float>(posicao - i0);
    const float* dados = buffer.getReadPointer(canal);
    return dados[i0] + fracao * (dados[i1] - dados[i0]);
}

} // namespace

// Job de carga cancelável de verdade. Um addJob(lambda) vira LambdaJobWrapper,
// que não tem como consultar shouldExit(): `interruptRunningJobs` só sinaliza,
// e um read() único de 45 minutos de áudio ignora o sinal até terminar. Daí a
// leitura em blocos, com checagem entre eles.
class MotorReproducao::JobCarga : public juce::ThreadPoolJob {
public:
    JobCarga(MotorReproducao& motor, juce::File arquivo, std::function<void(bool)> aoConcluir,
             uint64_t geracao)
        : juce::ThreadPoolJob("MatrizCargaAudio"),
          motor_(motor), ref_(&motor), arquivo_(std::move(arquivo)),
          aoConcluir_(std::move(aoConcluir)), geracao_(geracao) {}

    JobStatus runJob() override {
        BufferAudio::Ptr novo = carregar();

        // Abandonada no meio (motor morrendo, ou clique mais novo já em curso):
        // não posta nada. Postar aqui é justamente o que produzia o crash.
        if (shouldExit() || motor_.geracao_.load() != geracao_) return jobHasFinished;

        auto ref = ref_;
        auto aoConcluir = aoConcluir_;
        const uint64_t geracao = geracao_;

        juce::MessageManager::callAsync([ref, novo, aoConcluir, geracao]() {
            auto* motor = ref.get();
            if (motor == nullptr) return;                       // motor já morreu
            if (motor->geracao_.load() != geracao) return;      // carga obsoleta
            motor->aplicarBufferCarregado(novo);
            if (aoConcluir) aoConcluir(novo != nullptr);
        });
        return jobHasFinished;
    }

private:
    BufferAudio::Ptr carregar() {
        std::unique_ptr<juce::AudioFormatReader> leitor(motor_.formatos_.createReaderFor(arquivo_));
        if (leitor == nullptr || leitor->lengthInSamples <= 0 || leitor->numChannels == 0)
            return nullptr;

        const juce::int64 maxAmostras =
            static_cast<juce::int64>(leitor->sampleRate * kMaxSegundosEmMemoria);
        const int amostras = static_cast<int>(std::min(leitor->lengthInSamples, maxAmostras));

        BufferAudio::Ptr novo = new BufferAudio(static_cast<int>(leitor->numChannels), amostras,
                                                 leitor->sampleRate);
        novo->truncado = leitor->lengthInSamples > maxAmostras;

        constexpr int kBloco = 65536;
        for (int offset = 0; offset < amostras; offset += kBloco) {
            if (shouldExit() || motor_.geracao_.load() != geracao_) return nullptr;
            const int n = std::min(kBloco, amostras - offset);
            if (!leitor->read(&novo->audio, offset, n, offset, true, true)) return nullptr;
        }
        return novo;
    }

    MotorReproducao& motor_;                      // vivo enquanto o job roda: o destrutor espera
    juce::WeakReference<MotorReproducao> ref_;    // para o callAsync, que sobrevive ao job
    juce::File arquivo_;
    std::function<void(bool)> aoConcluir_;
    uint64_t geracao_;
};

MotorReproducao::MotorReproducao() { formatos_.registerBasicFormats(); }

MotorReproducao::~MotorReproducao() {
    // Primeira linha, antes de qualquer membro morrer: mensagens já
    // enfileiradas passam a encontrar referência nula em vez de um objeto
    // meio-destruído.
    masterReference.clear();

    ++geracao_;  // invalida qualquer job em voo

    // Aqui a espera é obrigatória: o job usa formatos_ e geracao_.
    if (!poolCarga_.removeAllJobs(true, 4000)) {
        // Job preso muito além do razoável — com a leitura em blocos isto não
        // deveria acontecer; se acontecer, é bug a investigar, não a ignorar.
        jassertfalse;
    }
    bufferParaAudio_.store(nullptr);
    bufferEmUso_ = nullptr;
}

void MotorReproducao::aplicarBufferCarregado(const BufferAudio::Ptr& novo) {
    if (novo == nullptr) return;
    buffersVivos_.add(novo);
    bufferAtual_ = novo;
    posicaoAmostras_.store(0.0);
    bufferParaAudio_.store(novo.get());
    // Só agora dá pra soltar os buffers antigos: a thread de áudio já foi
    // apontada pro novo. Guardamos o atual e o anterior — um bloco de folga é
    // suficiente e evita ter que sincronizar com o callback.
    while (buffersVivos_.size() > 2) buffersVivos_.remove(0);
}

void MotorReproducao::carregarAsync(const juce::File& arquivo, std::function<void(bool)> aoConcluir) {
    // Carga nova invalida a anterior pela geração, não por espera: bloquear a
    // message thread por até 2 s a cada clique num item era o que travava a
    // interface em disco lento — e não funcionava, porque o lambda antigo não
    // tinha como observar o pedido de interrupção.
    const uint64_t minhaGeracao = ++geracao_;
    poolCarga_.removeAllJobs(true, 0);
    descarregar();

    poolCarga_.addJob(new JobCarga(*this, arquivo, std::move(aoConcluir), minhaGeracao), true);
}

void MotorReproducao::descarregar() {
    tocando_.store(false);
    bufferParaAudio_.store(nullptr);
    bufferAtual_ = nullptr;
    posicaoAmostras_.store(0.0);
    limparLoop();
    picoE_.store(0.0f);
    picoD_.store(0.0f);
    rmsE_.store(0.0f);
    rmsD_.store(0.0f);
    correlacao_.store(0.0f);
}

double MotorReproducao::duracaoSegundos() const {
    if (bufferAtual_ == nullptr || bufferAtual_->taxaAmostragem <= 0.0) return 0.0;
    return bufferAtual_->audio.getNumSamples() / bufferAtual_->taxaAmostragem;
}

double MotorReproducao::posicaoSegundos() const {
    if (bufferAtual_ == nullptr || bufferAtual_->taxaAmostragem <= 0.0) return 0.0;
    return posicaoAmostras_.load() / bufferAtual_->taxaAmostragem;
}

void MotorReproducao::tocar() {
    if (bufferAtual_ == nullptr) return;
    degrauShuttle_.store(2);  // 1.0x
    reverso_.store(false);
    velocidade_.store(1.0);
    tocando_.store(true);
}

void MotorReproducao::pausar() { tocando_.store(false); }

void MotorReproducao::parar() {
    tocando_.store(false);
    velocidade_.store(1.0);
    degrauShuttle_.store(2);
    reverso_.store(false);
    irPara(loopAtivo_.load() ? loopInicio_.load() : 0.0);
}

void MotorReproducao::irPara(double segundos) {
    if (bufferAtual_ == nullptr) return;
    double amostras = juce::jlimit(0.0, static_cast<double>(bufferAtual_->audio.getNumSamples() - 1),
                                    segundos * bufferAtual_->taxaAmostragem);
    posicaoAmostras_.store(amostras);
    jogAlvoAmostras_.store(amostras);
}

void MotorReproducao::definirLoopInicio(double segundos) {
    loopInicio_.store(juce::jmax(0.0, segundos));
    // Marcar In depois do Out ainda não é um loop — só passa a valer quando
    // os dois existem e estão na ordem certa.
    if (loopFim_.load() > loopInicio_.load()) loopAtivo_.store(true);
}

void MotorReproducao::definirLoopFim(double segundos) {
    loopFim_.store(juce::jmax(0.0, segundos));
    if (loopFim_.load() > loopInicio_.load()) loopAtivo_.store(true);
}

void MotorReproducao::limparLoop() {
    loopAtivo_.store(false);
    loopInicio_.store(0.0);
    loopFim_.store(0.0);
}

void MotorReproducao::aplicarVelocidadeDoDegrau() {
    double v = kDegrausShuttle[juce::jlimit(0, kTotalDegrausShuttle - 1, degrauShuttle_.load())];
    velocidade_.store(reverso_.load() ? -v : v);
    tocando_.store(true);
}

void MotorReproducao::shuttleAvancar() {
    if (bufferAtual_ == nullptr) return;
    // L vindo do reverso primeiro CANCELA o reverso (volta pra 1x pra
    // frente) em vez de subir degrau no sentido errado — é o que a mão
    // espera de um JKL de verdade.
    if (reverso_.load()) {
        reverso_.store(false);
        degrauShuttle_.store(2);
    } else {
        degrauShuttle_.store(juce::jmin(kTotalDegrausShuttle - 1, degrauShuttle_.load() + 1));
    }
    aplicarVelocidadeDoDegrau();
}

void MotorReproducao::shuttleRetroceder() {
    if (bufferAtual_ == nullptr) return;
    if (!reverso_.load()) {
        reverso_.store(true);
        degrauShuttle_.store(2);
    } else {
        degrauShuttle_.store(juce::jmin(kTotalDegrausShuttle - 1, degrauShuttle_.load() + 1));
    }
    aplicarVelocidadeDoDegrau();
}

void MotorReproducao::shuttleParar() {
    tocando_.store(false);
    velocidade_.store(0.0);
    degrauShuttle_.store(2);
    reverso_.store(false);
}

void MotorReproducao::jog(double deltaSegundos) {
    if (bufferAtual_ == nullptr) return;
    double alvo = jogAlvoAmostras_.load() + deltaSegundos * bufferAtual_->taxaAmostragem;
    jogAlvoAmostras_.store(
        juce::jlimit(0.0, static_cast<double>(bufferAtual_->audio.getNumSamples() - 1), alvo));
    jogAtivo_.store(true);
    tocando_.store(true);
}

void MotorReproducao::encerrarJog() {
    jogAtivo_.store(false);
    tocando_.store(false);
    posicaoAmostras_.store(jogAlvoAmostras_.load());
}

MedidasAoVivo MotorReproducao::medidas() const {
    MedidasAoVivo m;
    m.picoEsquerda = picoE_.load();
    m.picoDireita = picoD_.load();
    m.rmsEsquerda = rmsE_.load();
    m.rmsDireita = rmsD_.load();
    m.correlacao = correlacao_.load();
    m.posicaoSegundos = posicaoSegundos();
    m.velocidade = velocidade_.load();
    m.tocando = tocando_.load();
    return m;
}

int MotorReproducao::lerAmostrasRecentes(std::vector<float>& esquerda, std::vector<float>& direita, int maximo) {
    int disponivel = juce::jmin(maximo, fifo_.getNumReady());
    if (disponivel <= 0) return 0;

    esquerda.resize(static_cast<size_t>(disponivel));
    direita.resize(static_cast<size_t>(disponivel));

    int inicio1, tam1, inicio2, tam2;
    fifo_.prepareToRead(disponivel, inicio1, tam1, inicio2, tam2);
    for (int i = 0; i < tam1; ++i) {
        esquerda[static_cast<size_t>(i)] = fifoEsquerda_[static_cast<size_t>(inicio1 + i)];
        direita[static_cast<size_t>(i)] = fifoDireita_[static_cast<size_t>(inicio1 + i)];
    }
    for (int i = 0; i < tam2; ++i) {
        esquerda[static_cast<size_t>(tam1 + i)] = fifoEsquerda_[static_cast<size_t>(inicio2 + i)];
        direita[static_cast<size_t>(tam1 + i)] = fifoDireita_[static_cast<size_t>(inicio2 + i)];
    }
    fifo_.finishedRead(tam1 + tam2);
    return tam1 + tam2;
}

void MotorReproducao::audioDeviceAboutToStart(juce::AudioIODevice* dispositivo) {
    matriz::diag::breadcrumb("audioDeviceAboutToStart");
    taxaSaida_ = dispositivo != nullptr ? dispositivo->getCurrentSampleRate() : 48000.0;
}

void MotorReproducao::audioDeviceStopped() {
    matriz::diag::breadcrumb("audioDeviceStopped");
    tocando_.store(false);
    bufferEmUso_ = nullptr;
}

void MotorReproducao::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* saida,
                                                        int canaisSaida, int amostras,
                                                        const juce::AudioIODeviceCallbackContext&) {
    // Silêncio primeiro: qualquer caminho de saída antecipada abaixo deixa a
    // placa com zeros, nunca com lixo do buffer anterior.
    for (int c = 0; c < canaisSaida; ++c)
        if (saida[c] != nullptr) juce::FloatVectorOperations::clear(saida[c], amostras);

    BufferAudio* raw = bufferParaAudio_.load();
    if (raw != bufferEmUso_.get())
        bufferEmUso_ = raw;

    if (bufferEmUso_ == nullptr || !tocando_.load()) {
        picoE_.store(0.0f);
        picoD_.store(0.0f);
        return;
    }

    const auto& audio = bufferEmUso_->audio;
    const int totalAmostras = audio.getNumSamples();
    const int canaisFonte = audio.getNumChannels();
    if (totalAmostras <= 0 || canaisFonte <= 0) return;

    // Razão entre a taxa do arquivo e a da placa: sem isso um arquivo de
    // 44,1 kHz numa placa a 48 kHz tocaria mais agudo e mais rápido.
    const double razaoTaxa = bufferEmUso_->taxaAmostragem / (taxaSaida_ > 0.0 ? taxaSaida_ : 48000.0);

    double posicao = posicaoAmostras_.load();
    double velocidadeAlvo = velocidade_.load();

    // Jog: em vez de saltar direto pro alvo, converte a distância restante
    // numa velocidade — é isso que faz o scrub soar como fita, e não como
    // uma sucessão de cortes.
    const bool emJog = jogAtivo_.load();
    if (emJog) {
        double distancia = jogAlvoAmostras_.load() - posicao;
        double janela = kSuavizacaoMs * 0.001 * bufferEmUso_->taxaAmostragem;
        velocidadeAlvo = juce::jlimit(-16.0, 16.0, janela > 0.0 ? distancia / janela : 0.0);
    }

    // Rampa exponencial de 50 ms na velocidade efetiva (§7): mudar de degrau
    // no JKL não pode estalar.
    double suave = velocidadeSuave_.load();
    const double coef = std::exp(-1.0 / (kSuavizacaoMs * 0.001 * (taxaSaida_ > 0.0 ? taxaSaida_ : 48000.0)));

    const double loopIni = loopInicio_.load() * bufferEmUso_->taxaAmostragem;
    const double loopFim = loopFim_.load() * bufferEmUso_->taxaAmostragem;
    const bool comLoop = loopAtivo_.load() && loopFim > loopIni;

    float picoE = 0.0f, picoD = 0.0f;
    double somaE = 0.0, somaD = 0.0, somaLR = 0.0, somaLL = 0.0, somaRR = 0.0;

    int escritaFifo1 = 0, tamFifo1 = 0, escritaFifo2 = 0, tamFifo2 = 0;
    const int espacoFifo = juce::jmin(amostras, fifo_.getFreeSpace());
    if (espacoFifo > 0) fifo_.prepareToWrite(espacoFifo, escritaFifo1, tamFifo1, escritaFifo2, tamFifo2);
    int escritos = 0;

    for (int i = 0; i < amostras; ++i) {
        suave = velocidadeAlvo + (suave - velocidadeAlvo) * coef;

        float e = interpolar(audio, 0, posicao);
        float d = canaisFonte > 1 ? interpolar(audio, 1, posicao) : e;

        if (canaisSaida > 0 && saida[0] != nullptr) saida[0][i] = e;
        if (canaisSaida > 1 && saida[1] != nullptr) saida[1][i] = d;

        picoE = juce::jmax(picoE, std::abs(e));
        picoD = juce::jmax(picoD, std::abs(d));
        somaE += static_cast<double>(e) * e;
        somaD += static_cast<double>(d) * d;
        somaLR += static_cast<double>(e) * d;
        somaLL += static_cast<double>(e) * e;
        somaRR += static_cast<double>(d) * d;

        if (escritos < tamFifo1) {
            fifoEsquerda_[static_cast<size_t>(escritaFifo1 + escritos)] = e;
            fifoDireita_[static_cast<size_t>(escritaFifo1 + escritos)] = d;
            ++escritos;
        } else if (escritos - tamFifo1 < tamFifo2) {
            int j = escritos - tamFifo1;
            fifoEsquerda_[static_cast<size_t>(escritaFifo2 + j)] = e;
            fifoDireita_[static_cast<size_t>(escritaFifo2 + j)] = d;
            ++escritos;
        }

        posicao += suave * razaoTaxa;

        if (comLoop) {
            if (posicao >= loopFim) posicao = loopIni + (posicao - loopFim);
            if (posicao < loopIni) posicao = loopFim - (loopIni - posicao);
        }

        // Fim do arquivo (ou começo, no reverso): para em vez de continuar
        // lendo fora do buffer.
        if (posicao >= totalAmostras - 1) {
            posicao = totalAmostras - 1;
            tocando_.store(false);
            break;
        }
        if (posicao <= 0.0) {
            posicao = 0.0;
            tocando_.store(false);
            break;
        }
    }

    if (espacoFifo > 0) fifo_.finishedWrite(escritos);

    posicaoAmostras_.store(posicao);
    if (!emJog) jogAlvoAmostras_.store(posicao);
    velocidadeSuave_.store(suave);

    picoE_.store(picoE);
    picoD_.store(picoD);
    rmsE_.store(static_cast<float>(std::sqrt(somaE / juce::jmax(1, amostras))));
    rmsD_.store(static_cast<float>(std::sqrt(somaD / juce::jmax(1, amostras))));

    double denom = std::sqrt(somaLL * somaRR);
    correlacao_.store(denom > 1.0e-12 ? static_cast<float>(juce::jlimit(-1.0, 1.0, somaLR / denom)) : 0.0f);
}

} // namespace matriz::audio
