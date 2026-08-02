#pragma once

#include <JuceHeader.h>

#include <stdexcept>
#include <string>

// Wrapper fino sobre juce::ChildProcess pras ferramentas externas que a
// leitura técnica e a geração de miniatura/forma de onda usam (ffmpeg,
// ffprobe, sips). Centraliza timeout e tratamento de erro.

namespace matriz::ingest {

class ProcessoExternoError : public std::runtime_error {
public:
    explicit ProcessoExternoError(const std::string& message) : std::runtime_error(message) {}
};

// Roda o processo e retorna sua saída padrão como texto. Uso: ferramentas
// que devolvem texto/JSON (ffprobe, sips -g). Não é seguro para saída
// binária — para isso, faça a ferramenta escrever num arquivo e leia o
// arquivo (é o que gerarFormaDeOnda faz para o PCM decodificado).
std::string capturarSaidaTexto(const juce::StringArray& argumentos, const std::string& nomeFerramenta,
                                int timeoutMs = 30000);

// Roda o processo até terminar e lança se o código de saída não for zero.
// Uso: ferramentas cujo resultado é um arquivo gerado em disco (ffmpeg
// escrevendo miniatura/keyframe/PCM), não a saída padrão.
void rodarEsperandoSucesso(const juce::StringArray& argumentos, const std::string& nomeFerramenta,
                            int timeoutMs = 30000);

} // namespace matriz::ingest
