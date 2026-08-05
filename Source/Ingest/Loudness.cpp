#include "Loudness.h"

#include <algorithm>
#include <cmath>

namespace matriz::ingest {

namespace {

// Filtro K de BS.1770-4: um shelf de agudos ("head effect") em cascata com
// um passa-altas ("RLB"). Os coeficientes da norma são dados a 48 kHz; para
// outras taxas, são recalculados pelas mesmas fórmulas analógicas.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    double processar(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Shelf de agudos (estágio 1). Fórmula de BS.1770-4 anexo 1, generalizada
// pra qualquer sampleRate.
Biquad shelfAgudos(double sampleRate) {
    const double f0 = 1681.974450955533;
    const double G = 3.999843853973347;   // dB
    const double Q = 0.7071752369554196;

    double K = std::tan(juce::MathConstants<double>::pi * f0 / sampleRate);
    double Vh = std::pow(10.0, G / 20.0);
    double Vb = std::pow(Vh, 0.4996667741545416);
    double a0 = 1.0 + K / Q + K * K;

    Biquad f;
    f.b0 = (Vh + Vb * K / Q + K * K) / a0;
    f.b1 = 2.0 * (K * K - Vh) / a0;
    f.b2 = (Vh - Vb * K / Q + K * K) / a0;
    f.a1 = 2.0 * (K * K - 1.0) / a0;
    f.a2 = (1.0 - K / Q + K * K) / a0;
    return f;
}

// Passa-altas RLB (estágio 2).
Biquad passaAltas(double sampleRate) {
    const double f0 = 38.13547087602444;
    const double Q = 0.5003270373238773;

    double K = std::tan(juce::MathConstants<double>::pi * f0 / sampleRate);
    Biquad f;
    f.a1 = 2.0 * (K * K - 1.0) / (1.0 + K / Q + K * K);
    f.a2 = (1.0 - K / Q + K * K) / (1.0 + K / Q + K * K);
    f.b0 = 1.0;
    f.b1 = -2.0;
    f.b2 = 1.0;
    return f;
}

// Pesos de canal G_i (BS.1770-4 tabela 3): L/R/C = 1.0, surround = +1.5 dB.
double pesoCanal(int indice, int totalCanais) {
    if (totalCanais <= 2) return 1.0;
    if (indice <= 2) return 1.0;                 // L, R, C
    if (indice == 3 || indice == 4) return 1.41; // Ls, Rs (+1.5 dB)
    return 1.0;                                   // a norma não define além disso; ignorar seria pior
}

double loudnessDeBlocos(const std::vector<double>& somasPorBloco) {
    if (somasPorBloco.empty()) return -70.0;
    double soma = 0.0;
    for (double s : somasPorBloco) soma += s;
    double media = soma / static_cast<double>(somasPorBloco.size());
    if (media <= 0.0) return -70.0;
    return -0.691 + 10.0 * std::log10(media);
}

} // namespace

Loudness medirLoudness(const juce::AudioBuffer<float>& audio, double sampleRate) {
    Loudness out;
    const int canais = audio.getNumChannels();
    const int amostras = audio.getNumSamples();
    if (canais <= 0 || amostras <= 0 || sampleRate <= 0.0) return out;

    // Pico de amostra — não é true peak com oversampling (a norma pede 4x;
    // isso fica declarado no header em vez de chamar de true peak sem ser).
    float pico = 0.0f;
    for (int c = 0; c < canais; ++c) pico = std::max(pico, audio.getMagnitude(c, 0, amostras));
    out.truePeakDbfs = pico > 0.0f ? 20.0f * std::log10(pico) : -144.0;

    // Filtro K por canal, mantendo o estado ao longo de todo o arquivo.
    std::vector<Biquad> shelf(static_cast<size_t>(canais), shelfAgudos(sampleRate));
    std::vector<Biquad> hp(static_cast<size_t>(canais), passaAltas(sampleRate));

    // Blocos de 400 ms com 75% de sobreposição (passo de 100 ms) — a
    // "sliding window" de BS.1770.
    const int tamanhoBloco = static_cast<int>(std::llround(0.4 * sampleRate));
    const int passo = static_cast<int>(std::llround(0.1 * sampleRate));
    if (tamanhoBloco <= 0 || passo <= 0 || amostras < tamanhoBloco) return out;

    // Soma de quadrados filtrados por canal, acumulada amostra a amostra;
    // cada bloco lê a diferença de somas acumuladas (integral parcial), o
    // que evita refiltrar as mesmas amostras 4 vezes por causa da
    // sobreposição.
    std::vector<std::vector<double>> acumulado(static_cast<size_t>(canais),
                                                std::vector<double>(static_cast<size_t>(amostras) + 1, 0.0));
    for (int c = 0; c < canais; ++c) {
        const float* dados = audio.getReadPointer(c);
        auto& shelfC = shelf[static_cast<size_t>(c)];
        auto& hpC = hp[static_cast<size_t>(c)];
        auto& accC = acumulado[static_cast<size_t>(c)];
        for (int i = 0; i < amostras; ++i) {
            double x = hpC.processar(shelfC.processar(static_cast<double>(dados[i])));
            accC[static_cast<size_t>(i) + 1] = accC[static_cast<size_t>(i)] + x * x;
        }
    }

    // Loudness de cada bloco (soma ponderada por canal) — guardada junto com
    // a soma de potência, porque o gating precisa dos dois.
    struct Bloco {
        double soma = 0.0; // sum_i G_i * mean_square_i
        double lufs = -70.0;
    };
    std::vector<Bloco> blocos;
    for (int inicio = 0; inicio + tamanhoBloco <= amostras; inicio += passo) {
        Bloco b;
        for (int c = 0; c < canais; ++c) {
            auto& accC = acumulado[static_cast<size_t>(c)];
            double somaQuadrados = accC[static_cast<size_t>(inicio + tamanhoBloco)] - accC[static_cast<size_t>(inicio)];
            double mediaQuadratica = somaQuadrados / static_cast<double>(tamanhoBloco);
            b.soma += pesoCanal(c, canais) * mediaQuadratica;
        }
        b.lufs = b.soma > 0.0 ? -0.691 + 10.0 * std::log10(b.soma) : -70.0;
        blocos.push_back(b);
    }
    if (blocos.empty()) return out;

    // --- LUFS integrado: gating absoluto em -70 LUFS, depois relativo em
    // (loudness dos que passaram) - 10 LU.
    std::vector<double> passaramAbsoluto;
    for (auto& b : blocos)
        if (b.lufs > -70.0) passaramAbsoluto.push_back(b.soma);
    if (passaramAbsoluto.empty()) return out;

    double limiarRelativo = loudnessDeBlocos(passaramAbsoluto) - 10.0;
    std::vector<double> passaramRelativo;
    for (auto& b : blocos)
        if (b.lufs > -70.0 && b.lufs > limiarRelativo) passaramRelativo.push_back(b.soma);
    out.lufsIntegrado = passaramRelativo.empty() ? loudnessDeBlocos(passaramAbsoluto) : loudnessDeBlocos(passaramRelativo);

    // --- LRA (EBU Tech 3342): blocos de 3 s com passo de 1 s, gating
    // absoluto em -70 e relativo em -20 LU, e a faixa é do percentil 10 ao 95.
    const int blocoLongo = static_cast<int>(std::llround(3.0 * sampleRate));
    const int passoLongo = static_cast<int>(std::llround(1.0 * sampleRate));
    if (blocoLongo > 0 && passoLongo > 0 && amostras >= blocoLongo) {
        std::vector<double> somasLongas;
        std::vector<double> lufsLongos;
        for (int inicio = 0; inicio + blocoLongo <= amostras; inicio += passoLongo) {
            double soma = 0.0;
            for (int c = 0; c < canais; ++c) {
                auto& accC = acumulado[static_cast<size_t>(c)];
                double somaQuadrados = accC[static_cast<size_t>(inicio + blocoLongo)] - accC[static_cast<size_t>(inicio)];
                soma += pesoCanal(c, canais) * (somaQuadrados / static_cast<double>(blocoLongo));
            }
            double lufs = soma > 0.0 ? -0.691 + 10.0 * std::log10(soma) : -70.0;
            if (lufs <= -70.0) continue;
            somasLongas.push_back(soma);
            lufsLongos.push_back(lufs);
        }
        if (!lufsLongos.empty()) {
            double limiar = loudnessDeBlocos(somasLongas) - 20.0;
            std::vector<double> acimaDoLimiar;
            for (double l : lufsLongos)
                if (l > limiar) acimaDoLimiar.push_back(l);
            if (acimaDoLimiar.size() >= 2) {
                std::sort(acimaDoLimiar.begin(), acimaDoLimiar.end());
                auto percentil = [&](double p) {
                    double pos = p * static_cast<double>(acimaDoLimiar.size() - 1);
                    size_t i = static_cast<size_t>(pos);
                    double frac = pos - static_cast<double>(i);
                    if (i + 1 >= acimaDoLimiar.size()) return acimaDoLimiar.back();
                    return acimaDoLimiar[i] * (1.0 - frac) + acimaDoLimiar[i + 1] * frac;
                };
                out.lra = percentil(0.95) - percentil(0.10);
            }
        }
    }

    return out;
}

std::optional<Loudness> medirLoudnessDoArquivo(const juce::File& arquivo) {
    juce::AudioFormatManager formatos;
    formatos.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> leitor(formatos.createReaderFor(arquivo));
    if (leitor == nullptr) return std::nullopt;
    if (leitor->lengthInSamples <= 0 || leitor->numChannels == 0) return std::nullopt;

    // Teto de leitura pra não carregar um arquivo de horas inteiro na
    // memória: 10 minutos cobrem o material típico e dão uma medida
    // estatisticamente estável; acima disso, mede os primeiros 10 minutos.
    // Declarado aqui em vez de silenciosamente truncado.
    const juce::int64 maxAmostras = static_cast<juce::int64>(leitor->sampleRate * 600.0);
    const int amostras = static_cast<int>(std::min(leitor->lengthInSamples, maxAmostras));

    juce::AudioBuffer<float> buffer(static_cast<int>(leitor->numChannels), amostras);
    if (!leitor->read(&buffer, 0, amostras, 0, true, true)) return std::nullopt;
    return medirLoudness(buffer, leitor->sampleRate);
}

} // namespace matriz::ingest
