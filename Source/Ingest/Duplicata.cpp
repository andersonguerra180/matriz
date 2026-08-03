#include "Duplicata.h"

#include "LeituraTecnica.h"
#include "Pcm.h"
#include "ProcessoExterno.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace matriz::ingest {

namespace {

// --- pHash ------------------------------------------------------------------

constexpr int kLadoPHash = 32;
constexpr int kBlocoPHash = 8;

std::array<std::array<double, kLadoPHash>, kLadoPHash> dct2D(
    const std::array<std::array<double, kLadoPHash>, kLadoPHash>& in) {
    std::array<std::array<double, kLadoPHash>, kLadoPHash> temp{};
    std::array<std::array<double, kLadoPHash>, kLadoPHash> out{};

    for (int y = 0; y < kLadoPHash; ++y) {
        for (int k = 0; k < kLadoPHash; ++k) {
            double soma = 0.0;
            for (int n = 0; n < kLadoPHash; ++n)
                soma += in[static_cast<size_t>(y)][static_cast<size_t>(n)] *
                        std::cos(juce::MathConstants<double>::pi / kLadoPHash * (n + 0.5) * k);
            temp[static_cast<size_t>(y)][static_cast<size_t>(k)] = soma;
        }
    }
    for (int k = 0; k < kLadoPHash; ++k) {
        for (int u = 0; u < kLadoPHash; ++u) {
            double soma = 0.0;
            for (int y = 0; y < kLadoPHash; ++y)
                soma += temp[static_cast<size_t>(y)][static_cast<size_t>(k)] *
                        std::cos(juce::MathConstants<double>::pi / kLadoPHash * (y + 0.5) * u);
            out[static_cast<size_t>(u)][static_cast<size_t>(k)] = soma;
        }
    }
    return out;
}

} // namespace

uint64_t calcularPHashImagem(const juce::File& imagem, const juce::File& dirTemporario) {
    dirTemporario.createDirectory();
    juce::File tmp = dirTemporario.getChildFile("matriz_phash_" + juce::Uuid().toDashedString() + ".raw");

    rodarEsperandoSucesso("ffmpeg",
                          {"-y", "-hide_banner", "-loglevel", "error", "-i", imagem.getFullPathName(),
                           "-vf", "scale=" + juce::String(kLadoPHash) + ":" + juce::String(kLadoPHash) +
                                      ":flags=lanczos,format=gray",
                           "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "gray", tmp.getFullPathName()});

    if (!tmp.existsAsFile())
        throw DuplicataError("ffmpeg não gerou a matriz de pixels para pHash: " + imagem.getFullPathName().toStdString());

    juce::MemoryBlock bloco;
    tmp.loadFileAsData(bloco);
    tmp.deleteFile();

    if (bloco.getSize() != static_cast<size_t>(kLadoPHash * kLadoPHash))
        throw DuplicataError("pHash: tamanho de pixel inesperado para " + imagem.getFullPathName().toStdString());

    const auto* pixels = static_cast<const uint8_t*>(bloco.getData());
    std::array<std::array<double, kLadoPHash>, kLadoPHash> matriz{};
    for (int y = 0; y < kLadoPHash; ++y)
        for (int x = 0; x < kLadoPHash; ++x)
            matriz[static_cast<size_t>(y)][static_cast<size_t>(x)] =
                pixels[static_cast<size_t>(y * kLadoPHash + x)];

    auto dct = dct2D(matriz);

    std::array<double, kBlocoPHash * kBlocoPHash> valores{};
    int idx = 0;
    for (int u = 0; u < kBlocoPHash; ++u)
        for (int v = 0; v < kBlocoPHash; ++v)
            valores[static_cast<size_t>(idx++)] = dct[static_cast<size_t>(u)][static_cast<size_t>(v)];

    // Mediana das componentes AC (exclui o índice 0, que é o termo DC / brilho médio).
    std::array<double, kBlocoPHash * kBlocoPHash - 1> ac{};
    std::copy(valores.begin() + 1, valores.end(), ac.begin());
    std::array<double, kBlocoPHash * kBlocoPHash - 1> acOrdenado = ac;
    std::nth_element(acOrdenado.begin(), acOrdenado.begin() + acOrdenado.size() / 2, acOrdenado.end());
    double mediana = acOrdenado[acOrdenado.size() / 2];

    uint64_t hash = 0;
    for (int i = 0; i < kBlocoPHash * kBlocoPHash; ++i)
        if (valores[static_cast<size_t>(i)] > mediana)
            hash |= (uint64_t(1) << i);

    return hash;
}

int distanciaHamming(uint64_t a, uint64_t b) {
    return std::popcount(a ^ b);
}

bool provavelmenteDuplicadaPHash(uint64_t a, uint64_t b, int limiar) {
    return distanciaHamming(a, b) <= limiar;
}

FingerprintAudio calcularFingerprintAudio(const juce::File& audio, const juce::File& dirTemporario,
                                           int numSegmentos) {
    constexpr int sampleRate = 11025;
    constexpr int fftOrder = 12; // 4096
    constexpr int fftSize = 1 << fftOrder;
    constexpr double duracaoExcerto = static_cast<double>(fftSize) / sampleRate; // ~0,371s

    auto leitura = lerTecnica(audio);
    if (!leitura.duracaoSegundos || *leitura.duracaoSegundos <= 0.0)
        throw DuplicataError("duração desconhecida, não é possível gerar fingerprint: " +
                              audio.getFullPathName().toStdString());
    double duracaoTotal = *leitura.duracaoSegundos;

    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> janela(static_cast<size_t>(fftSize),
                                                juce::dsp::WindowingFunction<float>::hann);

    FingerprintAudio resultado;
    resultado.duracaoSegundos = duracaoTotal;
    resultado.picosPorSegmento.reserve(static_cast<size_t>(numSegmentos));

    for (int i = 0; i < numSegmentos; ++i) {
        double centro = duracaoTotal * (i + 0.5) / numSegmentos;
        double offset = juce::jmax(0.0, centro - duracaoExcerto / 2.0);

        std::vector<float> amostras = decodificarExcertoPcmMono(audio, dirTemporario, sampleRate, offset, duracaoExcerto);

        std::vector<float> buffer(static_cast<size_t>(fftSize) * 2, 0.0f);
        size_t nCopiar = std::min(amostras.size(), static_cast<size_t>(fftSize));
        std::copy(amostras.begin(), amostras.begin() + static_cast<long>(nCopiar), buffer.begin());
        janela.multiplyWithWindowingTable(buffer.data(), static_cast<size_t>(fftSize));

        fft.performFrequencyOnlyForwardTransform(buffer.data(), true);

        // Bin 0 é DC — procura o pico a partir do bin 1.
        int melhorBin = 1;
        float melhorMagnitude = buffer[1];
        for (int b = 2; b <= fftSize / 2; ++b) {
            if (buffer[static_cast<size_t>(b)] > melhorMagnitude) {
                melhorMagnitude = buffer[static_cast<size_t>(b)];
                melhorBin = b;
            }
        }
        resultado.picosPorSegmento.push_back(static_cast<uint16_t>(melhorBin));
    }

    return resultado;
}

double similaridadeFingerprint(const FingerprintAudio& a, const FingerprintAudio& b, int toleranciaBins) {
    if (a.picosPorSegmento.size() != b.picosPorSegmento.size() || a.picosPorSegmento.empty())
        return 0.0;

    int combinam = 0;
    for (size_t i = 0; i < a.picosPorSegmento.size(); ++i) {
        int diff = std::abs(static_cast<int>(a.picosPorSegmento[i]) - static_cast<int>(b.picosPorSegmento[i]));
        if (diff <= toleranciaBins) ++combinam;
    }
    return static_cast<double>(combinam) / static_cast<double>(a.picosPorSegmento.size());
}

AnaliseCorteBanda detectarCorteDeBanda(const juce::File& audio, const juce::File& dirTemporario) {
    constexpr int sampleRate = 44100;
    constexpr int fftOrder = 12; // 4096
    constexpr int fftSize = 1 << fftOrder;
    constexpr int hop = fftSize / 2;

    auto leitura = lerTecnica(audio);
    double duracaoTotal = leitura.duracaoSegundos.value_or(0.0);
    if (duracaoTotal <= 0.0)
        throw DuplicataError("duração desconhecida, não é possível analisar corte de banda: " +
                              audio.getFullPathName().toStdString());

    double duracaoAnalise = std::min(15.0, duracaoTotal);
    double offset = duracaoTotal > duracaoAnalise ? (duracaoTotal - duracaoAnalise) / 2.0 : 0.0;

    std::vector<float> amostras = decodificarExcertoPcmMono(audio, dirTemporario, sampleRate, offset, duracaoAnalise);
    if (amostras.size() < static_cast<size_t>(fftSize)) {
        AnaliseCorteBanda vazio;
        return vazio; // trecho curto demais pra analisar com segurança — não determina nada
    }

    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> janela(static_cast<size_t>(fftSize),
                                                juce::dsp::WindowingFunction<float>::hann);

    constexpr int numBins = fftSize / 2 + 1;
    std::vector<double> somaMagnitude(static_cast<size_t>(numBins), 0.0);
    int numJanelas = 0;

    std::vector<float> buffer(static_cast<size_t>(fftSize) * 2);
    for (size_t inicio = 0; inicio + static_cast<size_t>(fftSize) <= amostras.size(); inicio += hop) {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        std::copy(amostras.begin() + static_cast<long>(inicio),
                  amostras.begin() + static_cast<long>(inicio) + fftSize, buffer.begin());
        janela.multiplyWithWindowingTable(buffer.data(), static_cast<size_t>(fftSize));
        fft.performFrequencyOnlyForwardTransform(buffer.data(), true);
        for (int b = 0; b < numBins; ++b) somaMagnitude[static_cast<size_t>(b)] += buffer[static_cast<size_t>(b)];
        ++numJanelas;
    }

    if (numJanelas == 0) return AnaliseCorteBanda{};

    // Energia média por banda de 1kHz, de 0 a 22kHz.
    constexpr double larguraBandaHz = 1000.0;
    int numBandas = static_cast<int>(sampleRate / 2 / larguraBandaHz);
    std::vector<double> energiaBanda(static_cast<size_t>(numBandas), 0.0);
    for (int banda = 0; banda < numBandas; ++banda) {
        double freqIni = banda * larguraBandaHz;
        double freqFim = freqIni + larguraBandaHz;
        int binIni = static_cast<int>(freqIni * fftSize / sampleRate);
        int binFim = std::min(numBins - 1, static_cast<int>(freqFim * fftSize / sampleRate));
        double soma = 0.0;
        int n = 0;
        for (int b = binIni; b <= binFim; ++b) {
            soma += somaMagnitude[static_cast<size_t>(b)];
            ++n;
        }
        energiaBanda[static_cast<size_t>(banda)] = n > 0 ? soma / n / numJanelas : 0.0;
    }

    // Referência: energia média nas bandas de 2 a 9kHz (onde praticamente todo
    // material de acervo tem conteúdo real).
    double referencia = 0.0;
    int nRef = 0;
    for (int banda = 2; banda <= 8 && banda < numBandas; ++banda) {
        referencia += energiaBanda[static_cast<size_t>(banda)];
        ++nRef;
    }
    referencia = nRef > 0 ? referencia / nRef : 0.0;
    if (referencia <= 0.0) return AnaliseCorteBanda{}; // sem conteúdo de referência, não dá pra julgar

    constexpr double limiarQuedaDb = -40.0; // banda com energia 40dB abaixo da referência
    AnaliseCorteBanda resultado;
    int bandasAbaixoSeguidas = 0;
    for (int banda = 9; banda < numBandas; ++banda) {
        double db = 20.0 * std::log10(std::max(1e-9, energiaBanda[static_cast<size_t>(banda)] / referencia));
        if (db < limiarQuedaDb) {
            ++bandasAbaixoSeguidas;
            if (bandasAbaixoSeguidas >= 2) {
                double freqCorte = (banda - bandasAbaixoSeguidas + 1) * larguraBandaHz;
                // Corte relevante só se acontece nitidamente antes do Nyquist —
                // senão é só o limite natural da análise, não um artefato de encoder.
                if (freqCorte < sampleRate / 2.0 - 1500.0) {
                    resultado.corteDetectado = true;
                    resultado.freqCorteEstimadaHz = freqCorte;
                }
                break;
            }
        } else {
            bandasAbaixoSeguidas = 0;
        }
    }

    return resultado;
}

} // namespace matriz::ingest
