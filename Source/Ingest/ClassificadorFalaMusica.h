#pragma once

#include <JuceHeader.h>

#include <optional>
#include <stdexcept>
#include <string>

// Classificador fala x música (§7.2 estágio 1, §9). Alimenta o campo
// "natureza" das fichas de áudio (sugerido_por: classificador_fala_musica,
// §6.3).
//
// DECISÃO DE ARQUITETURA: a §13 desenha a camada de IA inteira como sidecar
// Python via IPC, e a §16 escala esse sidecar na Etapa 4 junto com
// CLIP/Whisper/OCR. Este classificador é exigido já na Etapa 2 (§16 lista
// "detecção de... fala × música" explicitamente no ingest). Levantar o
// sidecar Python inteiro só para esta função, antes da infraestrutura de
// IA pesada existir, adiantaria trabalho de Etapa 4 sem necessidade.
// Em vez disso, isto é um classificador heurístico de DSP puro em C++,
// inspirado na literatura clássica de discriminação fala/música (energia de
// modulação em ~4Hz do envelope de energia — Scheirer & Slaney 1997 — mais
// variância da taxa de cruzamento por zero). Não é aprendizado de máquina.
// O contrato com o resto do sistema é idêntico ao de um modelo real: entra
// em sugestao_campo com modelo e versão próprios, nunca escreve direto no
// registro (P2/P3), e vira decisão humana só quando o operador confirma.
// Se/quando a Etapa 4 levantar o sidecar Python, este classificador pode
// ser trocado por um de verdade sem mudar nada fora deste arquivo.

namespace matriz::ingest {

class ClassificadorFalaMusicaError : public std::runtime_error {
public:
    explicit ClassificadorFalaMusicaError(const std::string& message) : std::runtime_error(message) {}
};

inline constexpr const char* kModeloClassificadorFalaMusica = "classificador_fala_musica_heuristico_dsp";
inline constexpr const char* kVersaoClassificadorFalaMusica = "v1";

struct ResultadoFalaMusica {
    // "fala" | "musica" | ausente quando o sinal é ambíguo demais pra sugerir
    // (zona cinzenta) — nesse caso o software não empurra um palpite fraco
    // pro operador, natureza fica em branco pra preenchimento manual.
    std::optional<std::string> rotulo;
    double confianca = 0.0; // 0..1, distância do score até o limiar de decisão

    double energiaModulacao4HzRatio = 0.0; // 0..1, quanto da energia de modulação está em 2-6Hz (indício de fala)
    double variabilidadeZcr = 0.0;         // 0..1, normalizado
};

ResultadoFalaMusica classificarFalaMusica(const juce::File& audio, const juce::File& dirTemporario);

} // namespace matriz::ingest
