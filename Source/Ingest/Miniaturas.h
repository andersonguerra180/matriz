#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Miniaturas, keyframes e forma de onda (§7.2 estágio 1, §11.3). Gera os
// arquivos de cache visual — a leitura/renderização propriamente dita (tira
// de diagnóstico) é Etapa 3. Imagem via `sips` nativo, vídeo/áudio via
// ffmpeg.

namespace matriz::ingest {

class MiniaturaError : public std::runtime_error {
public:
    explicit MiniaturaError(const std::string& message) : std::runtime_error(message) {}
};

struct DimensaoImagem {
    int largura = 0;
    int altura = 0;
};

// Lê largura/altura de uma imagem via `sips` (rápido — só duas chaves).
DimensaoImagem dimensoesImagem(const juce::File& imagem);

// Gera uma cópia reduzida de `origem` (imagem) em `destino`, com o maior
// lado limitado a `ladoMaximoPx`, preservando proporção. Retorna as
// dimensões finais.
DimensaoImagem gerarMiniaturaImagem(const juce::File& origem, const juce::File& destino, int ladoMaximoPx = 512);

struct KeyframeGerado {
    double tempoSegundos = 0.0;
    juce::File arquivo;
    DimensaoImagem dimensao;
};

// Extrai `quantidade` frames igualmente espaçados ao longo de
// `duracaoSegundos` do vídeo `origem`, salvando em `dirDestino` com o
// prefixo dado. Usado tanto para a miniatura única (quantidade=1) quanto
// para a faixa de keyframes da tira de diagnóstico (§11.3).
std::vector<KeyframeGerado> gerarKeyframesVideo(const juce::File& origem, double duracaoSegundos, int quantidade,
                                                 const juce::File& dirDestino, const juce::String& prefixo,
                                                 int larguraPx = 320);

struct FormaDeOnda {
    double duracaoSegundos = 0.0;
    double bucketsPorSegundo = 0.0;
    std::vector<float> minimos; // um valor por bucket
    std::vector<float> maximos; // um valor por bucket, mesmo tamanho de minimos

    // Serializa como pares [min,max] float32 intercalados — o formato gravado em forma_onda.peaks.
    std::vector<uint8_t> paraBlob() const;
};

// Decodifica `origemAudio` para PCM mono via ffmpeg (arquivo temporário em
// `dirTemporario`, apagado ao final) e calcula peaks min/max por bucket.
FormaDeOnda calcularFormaDeOnda(const juce::File& origemAudio, const juce::File& dirTemporario,
                                 double bucketsPorSegundo = 20.0);

} // namespace matriz::ingest
