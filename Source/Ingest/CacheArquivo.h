#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../Db/Database.h"
#include "LeituraTecnica.h"

// Cache de análise por arquivo (I2 + §4 `cache_arquivo`).
//
// Tudo o que a visualização precisa é calculado UMA vez, na ingestão, e
// guardado no registro. Nada disso pode ser recalculado num clique: medir
// LUFS de um acervo de centenas de horas a cada seleção de célula é o
// travamento que I2 proíbe.
//
// E é guardado como BLOB no banco, não como arquivo solto, de propósito
// (I3): com o volume desconectado, o registro continua na mão do operador —
// é ele que faz miniatura, forma de onda e ficha abrirem offline em menos de
// 100 ms (critério 2). Um cache em disco no volume sumiria junto com o
// volume, exatamente quando é mais necessário.

namespace matriz::ingest {

// Versão do formato de análise. Subir aqui invalida o cache gravado por
// versões anteriores (cache_arquivo.versao_analise), pra uma mudança de
// algoritmo não conviver em silêncio com valores antigos.
inline constexpr int kVersaoAnalise = 1;

struct AnaliseCache {
    std::vector<uint8_t> miniatura;  // PNG; vazio quando não há prévia possível
    std::vector<uint8_t> formaOnda;  // pares [min,max] float32 (Miniaturas.h::FormaDeOnda::paraBlob)
    std::optional<double> lufsI;
    std::optional<double> lra;
    std::optional<double> truePeak;
    std::optional<double> peakAmostra;
    std::optional<double> correlacaoMedia;  // [-1, +1]; ausente em mono
};

// Calcula tudo. Roda no ThreadPool de ingestão — decodifica áudio e chama
// ffmpeg/sips, nunca pode tocar a message thread (I1). Nunca lança: um
// arquivo que não dá pra analisar devolve um AnaliseCache vazio, porque
// falhar em gerar cache não é falhar em catalogar.
AnaliseCache calcularCache(const juce::File& arquivo, CategoriaMidia categoria,
                            const juce::File& dirTemporario, std::optional<double> duracaoSegundos);

// Grava (ou substitui) a linha de `arquivoId`. Idempotente.
void gravarCache(matriz::db::Database& registro, const std::string& arquivoId, const AnaliseCache& analise);

// Conveniência: calcula e grava. Usada pelo ingest.
void calcularEGravarCache(matriz::db::Database& registro, const std::string& arquivoId,
                           const juce::File& arquivo, CategoriaMidia categoria,
                           const juce::File& dirTemporario, std::optional<double> duracaoSegundos);

// Leitura pela chave primária — a única forma de acesso a banco que I1
// permite na message thread.
std::optional<AnaliseCache> lerCache(matriz::db::Database& registro, const std::string& arquivoId);

// Reconstrói a forma de onda a partir do blob, pro desenho da timeline.
struct FormaDeOndaLida {
    std::vector<float> minimos;
    std::vector<float> maximos;
};
FormaDeOndaLida lerFormaDeOnda(const std::vector<uint8_t>& blob);

} // namespace matriz::ingest
