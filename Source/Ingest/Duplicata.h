#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

// Detecção de duplicata (pHash de imagem, fingerprint de áudio) e de corte
// de banda denunciando origem lossy (§7.2 estágio 1, §9). Grava em
// phash_imagem/fingerprint_audio/duplicata (indice.sqlite) — ver
// Source/Db/Database.h para a camada de persistência; este módulo só
// calcula.
//
// O fingerprint de áudio AQUI NÃO É Chromaprint/AcoustID. É um algoritmo
// espectral simples, próprio deste projeto (registrado como
// "matriz_spectral_v1" em fingerprint_audio.algoritmo), pensado pra achar
// duplicata exata/quase-exata (mesma gravação exportada duas vezes) sem
// decodificar o arquivo inteiro — amostra N trechos curtos espalhados pela
// duração, então escala para os áudios de 400 horas citados em §9.3 sem
// travar o estágio 1 ("automático, imediato").

namespace matriz::ingest {

class DuplicataError : public std::runtime_error {
public:
    explicit DuplicataError(const std::string& message) : std::runtime_error(message) {}
};

// --- Imagem: pHash (DCT 32x32, bloco 8x8 de baixa frequência, 64 bits) -----

uint64_t calcularPHashImagem(const juce::File& imagem, const juce::File& dirTemporario);
int distanciaHamming(uint64_t a, uint64_t b);
// Limiar clássico de pHash: distância <= 10 (de 64 bits) é "muito parecida".
bool provavelmenteDuplicadaPHash(uint64_t a, uint64_t b, int limiar = 10);

// --- Áudio: fingerprint espectral próprio ----------------------------------

struct FingerprintAudio {
    double duracaoSegundos = 0.0;
    std::vector<uint16_t> picosPorSegmento; // índice do bin de FFT com mais energia, por segmento amostrado
};

FingerprintAudio calcularFingerprintAudio(const juce::File& audio, const juce::File& dirTemporario,
                                           int numSegmentos = 32);

// 0.0 (nada em comum) a 1.0 (idêntico). Exige mesmo numSegmentos nos dois
// fingerprints (senão retorna 0.0 — não são comparáveis).
double similaridadeFingerprint(const FingerprintAudio& a, const FingerprintAudio& b, int toleranciaBins = 2);

// --- Áudio: corte de banda denunciando origem lossy (§7.2, §9.3) ----------

struct AnaliseCorteBanda {
    bool corteDetectado = false;
    std::optional<double> freqCorteEstimadaHz;
};

// Decodifica um trecho representativo (até 15s, do meio do arquivo quando
// possível) e procura uma queda abrupta de energia acima de ~10kHz — a
// assinatura de um encoder lossy (mp3/ATRAC) que já rodou antes deste
// arquivo existir. Não distingue de um rolloff natural muito suave; por
// isso é sugestão (sugestao_campo), nunca decisão automática.
AnaliseCorteBanda detectarCorteDeBanda(const juce::File& audio, const juce::File& dirTemporario);

} // namespace matriz::ingest
