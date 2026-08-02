#pragma once

#include <JuceHeader.h>

#include <vector>

// Decodificação de um trecho limitado de áudio para PCM mono float32, via
// ffmpeg. Usado por qualquer análise que não precisa (e não deve, por causa
// de arquivos de centenas de horas — §9.3) decodificar o arquivo inteiro:
// fingerprint, detecção de corte de banda, classificador fala x música.

namespace matriz::ingest {

std::vector<float> decodificarExcertoPcmMono(const juce::File& audio, const juce::File& dirTemporario,
                                              int sampleRate, double offsetSegundos, double duracaoSegundos);

} // namespace matriz::ingest
