#pragma once

#include <JuceHeader.h>

#include <stdexcept>
#include <string>

// Wrapper fino sobre juce::ChildProcess pras únicas duas ferramentas
// externas que o MATRIZ ainda invoca como subprocesso: ffmpeg e ffprobe
// (decodificação de mídia — não há substituto in-process razoável). Tudo o
// que antes rodava via `sips`/`mdls` (macOS only) ou parser artesanal virou
// biblioteca linkada direto (Exiv2, PDF — ver LeituraTecnica.h), então essas
// duas são de fato as únicas chamadas de processo externo no projeto.
//
// Resolução de caminho (A.4): nunca depende do PATH em build de produção.
// `resolverCaminhoExecutavel` procura o binário ao lado do executável do
// app (convenção de empacotamento da Etapa 10: ffmpeg/ffprobe embarcados
// junto do instalador). Só cai pro PATH quando compilado com
// MATRIZ_DEV_BUILD — as duas metas de self-test definem essa macro; a build
// de produção da Etapa 10 não deve defini-la, e nesse caso a ausência do
// binário embarcado vira erro explícito, nunca uma tentativa silenciosa via
// PATH.

namespace matriz::ingest {

class ProcessoExternoError : public std::runtime_error {
public:
    explicit ProcessoExternoError(const std::string& message) : std::runtime_error(message) {}
};

// Resolve o caminho absoluto de `nomeFerramenta` ("ffmpeg" ou "ffprobe", sem
// extensão) ao lado do executável do app. Em build de desenvolvimento
// (MATRIZ_DEV_BUILD definido), se não encontrar, devolve o nome nu pra
// deixar o sistema operacional resolver via PATH. Em build de produção,
// lança ProcessoExternoError se o binário embarcado não existir.
juce::String resolverCaminhoExecutavel(const std::string& nomeFerramenta);

// Roda `nomeFerramenta` (resolvido via resolverCaminhoExecutavel) com
// `argumentos` e retorna sua saída padrão como texto. Uso: ffprobe. Não é
// seguro para saída binária — para isso, faça a ferramenta escrever num
// arquivo e leia o arquivo (é o que calcularFormaDeOnda faz com o PCM
// decodificado).
std::string capturarSaidaTexto(const std::string& nomeFerramenta, const juce::StringArray& argumentos,
                                int timeoutMs = 30000);

// Roda `nomeFerramenta` até terminar e lança se o código de saída não for
// zero. Uso: ffmpeg escrevendo miniatura/keyframe/PCM em disco.
void rodarEsperandoSucesso(const std::string& nomeFerramenta, const juce::StringArray& argumentos,
                            int timeoutMs = 30000);

} // namespace matriz::ingest
