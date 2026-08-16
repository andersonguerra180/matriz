#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

// Motor de reprodução da Estação de Escuta (§7).
//
// Divisão de responsabilidade, que é o que mantém a interface fluida:
//
//   - Thread de carga (background): decodifica o arquivo pra um
//     AudioBuffer. Nunca a message thread, nunca a de áudio.
//   - Audio callback: só lê do buffer já pronto, interpola e escreve as
//     medidas em variáveis atômicas. Não aloca, não trava, não abre arquivo.
//   - UI: um Timer de 30 Hz lê as atômicas. Nunca é chamada pelo áudio.
//
// Reprodução a partir de um buffer em memória (e não streaming do disco) é
// uma escolha, não um atalho: shuttle reverso e jog com precisão de amostra
// exigem saltar pra qualquer posição a qualquer momento, e fazer isso com
// leitura de disco dentro do audio callback é impossível sem estouro. O
// custo é um teto de duração, declarado em kMaxSegundosEmMemoria — acima
// dele, o começo do arquivo é carregado e o resto fica de fora, avisado, em
// vez de silenciosamente truncado.

namespace matriz::audio {

// 45 minutos: cobre lado de fita, lado de LP, show inteiro na maioria dos
// casos. A 48 kHz estéreo float são ~1 GB — o teto existe pra não estourar
// a memória num arquivo de dezenas de horas.
inline constexpr double kMaxSegundosEmMemoria = 45.0 * 60.0;

// Degraus de shuttle do JKL (§7). L sobe, J desce/inverte, K para.
inline constexpr double kDegrausShuttle[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
inline constexpr int kTotalDegrausShuttle = 6;

// Buffer com contagem de referência: a message thread troca o ponteiro, a
// thread de áudio segue lendo o antigo até o próximo bloco. Sem isso, trocar
// de faixa enquanto toca liberaria memória debaixo do audio callback.
class BufferAudio : public juce::ReferenceCountedObject {
public:
    using Ptr = juce::ReferenceCountedObjectPtr<BufferAudio>;

    BufferAudio(int canais, int amostras, double sampleRate)
        : audio(canais, amostras), taxaAmostragem(sampleRate) {}

    juce::AudioBuffer<float> audio;
    double taxaAmostragem = 44100.0;
    bool truncado = false;  // arquivo maior que kMaxSegundosEmMemoria
};

// Medidas do audio callback pra UI. Lidas por um Timer de 30 Hz — nunca por
// callback do áudio chamando a interface.
struct MedidasAoVivo {
    float picoEsquerda = 0.0f;
    float picoDireita = 0.0f;
    float rmsEsquerda = 0.0f;
    float rmsDireita = 0.0f;
    float correlacao = 0.0f;   // [-1, +1]
    double posicaoSegundos = 0.0;
    double velocidade = 0.0;   // negativa = reverso; 0 = parado
    bool tocando = false;
};

class MotorReproducao : public juce::AudioIODeviceCallback {
public:
    MotorReproducao();
    ~MotorReproducao() override;

    // Decodifica em background e devolve o resultado na message thread.
    // `aoConcluir(false)` quando o formato não é legível — o transporte fica
    // desabilitado e o resto da tela continua funcionando (I3).
    void carregarAsync(const juce::File& arquivo, std::function<void(bool)> aoConcluir);
    void descarregar();

    bool temAudio() const { return bufferAtual_ != nullptr; }
    double duracaoSegundos() const;
    bool foiTruncado() const { return bufferAtual_ != nullptr && bufferAtual_->truncado; }

    // ---- Transporte ----
    void tocar();
    void pausar();
    void parar();  // pausa e volta pro início do loop (ou pro zero)
    bool estaTocando() const { return tocando_.load(); }

    void irPara(double segundos);
    double posicaoSegundos() const;

    // ---- Loop (teclas I / O) ----
    void definirLoopInicio(double segundos);
    void definirLoopFim(double segundos);
    void limparLoop();
    bool loopAtivo() const { return loopAtivo_.load(); }
    double loopInicio() const { return loopInicio_.load(); }
    double loopFim() const { return loopFim_.load(); }

    // ---- Shuttle JKL (§7), em degraus ----
    void shuttleAvancar();   // L
    void shuttleRetroceder();  // J
    void shuttleParar();     // K
    double velocidadeAtual() const { return velocidade_.load(); }

    // ---- Jog / scrub ----
    // `delta` em segundos. Um scrub é audível e com suavização angular de
    // 50 ms: sem ela, cada movimento do mouse vira um salto de posição e o
    // resultado é uma sequência de cliques, não um scrub.
    void jog(double deltaSegundos);
    void encerrarJog();

    MedidasAoVivo medidas() const;

    // Amostras recentes pra FFT e vetorscópio, copiadas do FIFO lock-free.
    // Devolve quantos frames saíram.
    int lerAmostrasRecentes(std::vector<float>& esquerda, std::vector<float>& direita, int maximo);

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* entrada, int canaisEntrada, float* const* saida,
                                           int canaisSaida, int amostras,
                                           const juce::AudioIODeviceCallbackContext& contexto) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* dispositivo) override;
    void audioDeviceStopped() override;

private:
    class JobCarga;
    friend class JobCarga;

    void aplicarVelocidadeDoDegrau();
    // Chamado só na message thread, pelo job de carga que ainda for atual.
    void aplicarBufferCarregado(const BufferAudio::Ptr& novo);

    juce::AudioFormatManager formatos_;
    juce::ThreadPool poolCarga_{1};

    // Cada carregarAsync incrementa. Job e callback carregam a geração em que
    // nasceram e se descartam quando ficam obsoletos — é o que faz dois
    // cliques rápidos terminarem no áudio do último item clicado, sem
    // bloquear a message thread esperando o job anterior morrer.
    std::atomic<uint64_t> geracao_{0};

    // Mantém vivos os buffers já trocados até a thread de áudio ter passado
    // por eles. Só a message thread mexe nesta lista.
    juce::ReferenceCountedArray<BufferAudio> buffersVivos_;
    BufferAudio::Ptr bufferAtual_;
    std::atomic<BufferAudio*> bufferParaAudio_{nullptr};

    std::atomic<bool> tocando_{false};
    std::atomic<double> posicaoAmostras_{0.0};
    std::atomic<double> velocidade_{1.0};      // alvo
    std::atomic<double> velocidadeSuave_{1.0}; // seguida com rampa, evita clique
    std::atomic<int> degrauShuttle_{2};        // índice em kDegrausShuttle; 2 == 1.0x
    std::atomic<bool> reverso_{false};

    std::atomic<bool> loopAtivo_{false};
    std::atomic<double> loopInicio_{0.0};
    std::atomic<double> loopFim_{0.0};

    std::atomic<bool> jogAtivo_{false};
    std::atomic<double> jogAlvoAmostras_{0.0};

    std::atomic<float> picoE_{0.0f}, picoD_{0.0f}, rmsE_{0.0f}, rmsD_{0.0f}, correlacao_{0.0f};

    // FIFO lock-free pro espectrograma e o vetorscópio. O audio callback só
    // escreve; a UI só lê, no seu próprio Timer.
    static constexpr int kTamanhoFifo = 8192;
    juce::AbstractFifo fifo_{kTamanhoFifo};
    std::vector<float> fifoEsquerda_ = std::vector<float>(kTamanhoFifo, 0.0f);
    std::vector<float> fifoDireita_ = std::vector<float>(kTamanhoFifo, 0.0f);

    double taxaSaida_ = 48000.0;

    // O callAsync do job de carga pode ser entregue DEPOIS de o motor morrer
    // (a mensagem já está na fila e removeAllJobs não a cancela). A referência
    // fraca é o que transforma isso em no-op em vez de escrita em memória
    // liberada — ver o clear() na primeira linha do destrutor.
    JUCE_DECLARE_WEAK_REFERENCEABLE(MotorReproducao)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MotorReproducao)
};

} // namespace matriz::audio
