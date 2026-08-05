#pragma once

#include <JuceHeader.h>

#include <optional>
#include <vector>

// Loudness EBU R128 / ITU-R BS.1770: LUFS integrado e LRA (item 6.1).
//
// Pré-calculado na leitura técnica, NUNCA em tempo real (item 6.1 é
// explícito) — medir 400 horas de acervo a cada seleção de célula seria
// inviável. O que roda em tempo real é só o medidor VU do arquivo tocando,
// que é outra coisa (balística de 300 ms sobre o áudio que está saindo).
//
// Implementado direto aqui em vez de por biblioteca (libebur128 não tem
// caminho de FetchContent viável pros dois sistemas, e a regra de
// dependência do projeto proíbe gerenciador de pacote do sistema): o
// algoritmo é o filtro K + gating de BS.1770-4, curto o suficiente pra ser
// escrito e testado contra os valores de referência da própria norma.

namespace matriz::ingest {

struct Loudness {
    double lufsIntegrado = -70.0; // LUFS; -70 é o piso da norma (silêncio absoluto)
    double lra = 0.0;              // Loudness Range, em LU
    double truePeakDbfs = -144.0;  // pico de amostra em dBFS (não oversampled — declarado, não fingido)
};

// Mede um buffer já decodificado. `sampleRate` em Hz. Canais > 2 são
// tratados com os pesos de BS.1770 até 5 canais; acima disso, os extras
// entram com peso 1.0 (a norma não define, e ignorá-los seria pior).
Loudness medirLoudness(const juce::AudioBuffer<float>& audio, double sampleRate);

// Abre o arquivo com os formatos que o JUCE conhece (WAV/AIFF/FLAC/OGG/MP3)
// e mede. nullopt quando o formato não é legível por aqui — vídeo, por
// exemplo, precisaria de ffmpeg pra extrair a trilha, o que não acontece
// nesta função (gap declarado, não silencioso).
std::optional<Loudness> medirLoudnessDoArquivo(const juce::File& arquivo);

} // namespace matriz::ingest
