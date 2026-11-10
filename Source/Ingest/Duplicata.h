#pragma once

#include <JuceHeader.h>

#include <array>
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

// Confirmação de um par que o pHash aproximou. Num acervo inteiro (milhões de pares) o d <= 10 junta muitas fotos
// diferentes por acaso — sobretudo escuras ou de cor chapada, em que os bits do pHash viram ruído. A assinatura é a
// luma 32x32 normalizada (média 0, desvio 1) + a proporção da imagem; o par só fica se a correlação de pixels for
// alta (>= 0,80; >= 0,90 quando as proporções diferem mais de 12%, i.e. recorte). Calibrado num acervo real de
// 12 mil fotos: duplicatas reais ficam em ~1,0; fotos diferentes que o pHash juntava, abaixo de 0,8.
struct AssinaturaVisual {
    std::array<float, 32 * 32> luma{};
    float proporcao = 0.0f;  // largura / altura
    bool valida = false;     // false: não decodificou ou imagem chapada (sem estrutura para comparar)
};
AssinaturaVisual calcularAssinaturaVisual(const juce::File& imagem);
bool confirmaSemelhancaVisual(const AssinaturaVisual& a, const AssinaturaVisual& b);

// --- Conteúdo: conferência barata de par achado só por metadados ----------

// Dois arquivos de MESMO tamanho são o mesmo conteúdo? Compara 4 blocos (até 64 KB) no miolo do arquivo (20/40/60/80%),
// longe do cabeçalho/fim onde ficam tags (BWF, ID3, EXIF): cópia com tag editada continua "mesma", tracks diferentes de
// uma sessão multitrack (mesma duração => mesmo tamanho em PCM) dão "diferente". nullopt = não deu para decidir
// (tamanhos diferentes, arquivo ilegível).
std::optional<bool> mesmoConteudoNoMiolo(const juce::File& a, const juce::File& b);

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
