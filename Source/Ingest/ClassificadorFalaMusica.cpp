#include "ClassificadorFalaMusica.h"

#include "LeituraTecnica.h"
#include "Pcm.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace matriz::ingest {

namespace {

constexpr int kSampleRate = 22050;
constexpr int kFrameLen = 441;         // 20ms @ 22050Hz
constexpr int kHop = 220;              // 10ms — 50% overlap, frame rate resultante = 100Hz
constexpr int kEnvelopeFftOrder = 9;   // 512 frames de envelope = 5,12s de modulação
constexpr int kEnvelopeFftSize = 1 << kEnvelopeFftOrder;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

struct FramesCalculados {
    std::vector<float> energia; // RMS por frame
    std::vector<float> zcr;     // taxa de cruzamento por zero por frame, 0..1
};

FramesCalculados calcularFrames(const std::vector<float>& amostras) {
    FramesCalculados f;
    for (size_t inicio = 0; inicio + kFrameLen <= amostras.size(); inicio += kHop) {
        double somaQuadrados = 0.0;
        int cruzamentos = 0;
        for (int i = 0; i < kFrameLen; ++i) {
            float a = amostras[inicio + static_cast<size_t>(i)];
            somaQuadrados += static_cast<double>(a) * a;
            if (i > 0) {
                float anterior = amostras[inicio + static_cast<size_t>(i) - 1];
                if ((anterior >= 0.0f) != (a >= 0.0f)) ++cruzamentos;
            }
        }
        f.energia.push_back(static_cast<float>(std::sqrt(somaQuadrados / kFrameLen)));
        f.zcr.push_back(static_cast<float>(cruzamentos) / kFrameLen);
    }
    return f;
}

// Proporção da energia de modulação do envelope concentrada em 2-6Hz — a
// faixa da taxa silábica da fala (Scheirer & Slaney 1997).
double energiaModulacao4Hz(const std::vector<float>& envelope) {
    int n = std::min(static_cast<int>(envelope.size()), kEnvelopeFftSize);
    if (n < kEnvelopeFftSize / 4)
        throw ClassificadorFalaMusicaError("trecho curto demais para analisar modulação de envelope");

    double media = std::accumulate(envelope.begin(), envelope.begin() + n, 0.0) / n;

    std::vector<float> buffer(static_cast<size_t>(kEnvelopeFftSize) * 2, 0.0f);
    juce::dsp::WindowingFunction<float> janela(static_cast<size_t>(kEnvelopeFftSize),
                                                juce::dsp::WindowingFunction<float>::hann);
    for (int i = 0; i < n; ++i) buffer[static_cast<size_t>(i)] = envelope[static_cast<size_t>(i)] - static_cast<float>(media);
    janela.multiplyWithWindowingTable(buffer.data(), static_cast<size_t>(kEnvelopeFftSize));

    juce::dsp::FFT fft(kEnvelopeFftOrder);
    fft.performFrequencyOnlyForwardTransform(buffer.data(), true);

    double freqPorBin = 100.0 / kEnvelopeFftSize; // frame rate do envelope é 100Hz (hop de 10ms)
    double energiaFala = 0.0, energiaTotal = 0.0;
    for (int b = 1; b <= kEnvelopeFftSize / 2; ++b) { // pula bin 0 (DC, já removido)
        double freq = b * freqPorBin;
        if (freq > 16.0) break;
        double e = static_cast<double>(buffer[static_cast<size_t>(b)]);
        energiaTotal += e;
        if (freq >= 2.0 && freq <= 6.0) energiaFala += e;
    }
    return energiaTotal > 0.0 ? clamp01(energiaFala / energiaTotal) : 0.0;
}

double variabilidadeZcr(const std::vector<float>& zcr) {
    if (zcr.size() < 2) return 0.0;
    double media = std::accumulate(zcr.begin(), zcr.end(), 0.0) / zcr.size();
    if (media <= 1e-6) return 0.0;
    double variancia = 0.0;
    for (float v : zcr) variancia += (v - media) * (v - media);
    variancia /= zcr.size();
    double desvio = std::sqrt(variancia);
    double coefVariacao = desvio / media; // dispersão relativa da taxa de cruzamento por zero
    return clamp01(coefVariacao / 1.5);   // normalização empírica pra faixa 0..1
}

} // namespace

ResultadoFalaMusica classificarFalaMusica(const juce::File& audio, const juce::File& dirTemporario) {
    auto leitura = lerTecnica(audio);
    if (!leitura.duracaoSegundos || *leitura.duracaoSegundos <= 0.0)
        throw ClassificadorFalaMusicaError("duração desconhecida, não é possível classificar: " +
                                            audio.getFullPathName().toStdString());
    double duracaoTotal = *leitura.duracaoSegundos;

    double duracaoAnalise = std::min(30.0, duracaoTotal);
    double offset = duracaoTotal > duracaoAnalise ? (duracaoTotal - duracaoAnalise) / 2.0 : 0.0;

    std::vector<float> amostras = decodificarExcertoPcmMono(audio, dirTemporario, kSampleRate, offset, duracaoAnalise);
    if (amostras.size() < static_cast<size_t>(kFrameLen) * 10)
        throw ClassificadorFalaMusicaError("trecho de áudio curto demais para classificar: " +
                                            audio.getFullPathName().toStdString());

    FramesCalculados frames = calcularFrames(amostras);

    ResultadoFalaMusica r;
    r.energiaModulacao4HzRatio = energiaModulacao4Hz(frames.energia);
    r.variabilidadeZcr = variabilidadeZcr(frames.zcr);

    double score = 0.65 * r.energiaModulacao4HzRatio + 0.35 * r.variabilidadeZcr;

    constexpr double kLimiarFala = 0.55;
    constexpr double kLimiarMusica = 0.45;
    if (score >= kLimiarFala) {
        r.rotulo = "fala";
        r.confianca = clamp01((score - kLimiarFala) / (1.0 - kLimiarFala));
    } else if (score <= kLimiarMusica) {
        r.rotulo = "musica";
        r.confianca = clamp01((kLimiarMusica - score) / kLimiarMusica);
    } else {
        r.rotulo = std::nullopt; // zona cinzenta — nenhuma sugestão, evita empurrar palpite fraco (P3)
        r.confianca = 0.0;
    }

    return r;
}

} // namespace matriz::ingest
