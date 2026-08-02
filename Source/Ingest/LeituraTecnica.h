#pragma once

#include <JuceHeader.h>

#include <optional>
#include <stdexcept>
#include <string>

// Estágio 1 da ingestão (§7.2): leitura automática do que já está no
// arquivo — nunca indexação de conteúdo. Áudio/vídeo via ffprobe (duração,
// sample rate, bit depth, codec, fps, tags embutidas incluindo BWF/bext,
// ID3, Vorbis comment quando o container expõe). Imagem via `sips` nativo do
// macOS (dimensões, perfil de cor, bits por amostra) e PDF via contagem de
// páginas por varredura do próprio arquivo — troca deliberada em relação ao
// ExifTool declarado em §13: este ambiente de desenvolvimento (macOS 13, sem
// bottle) força o Homebrew a recompilar CMake do zero só pra instalar o
// ExifTool (dependência transitiva), o que não é uma troca aceitável de
// tempo por uma leitura de metadado. `sips` é nativo, sem instalação, e
// cobre o que a leitura técnica de estágio 1 precisa (dimensão, perfil,
// bit depth). EXIF completo (câmera, GPS, data original) fica pendente de
// uma biblioteca EXIF real — não está implementado, e não deve ser
// confundido com o que `sips` retorna.

namespace matriz::ingest {

class LeituraTecnicaError : public std::runtime_error {
public:
    explicit LeituraTecnicaError(const std::string& message) : std::runtime_error(message) {}
};

enum class CategoriaMidia { Audio, Video, Imagem, Documento, Desconhecida };

CategoriaMidia categoriaPorExtensao(const juce::File& arquivo);

struct LeituraTecnicaResultado {
    juce::var bruto; // objeto completo retornado por ffprobe/exiftool, vira arquivo.caracteristicas_tecnicas_json

    std::optional<double> duracaoSegundos;
    std::optional<int> sampleRate;
    std::optional<int> bitDepth;
    std::optional<int> canais;
    std::string codec;
    std::optional<int> larguraPx;
    std::optional<int> alturaPx;
    std::optional<double> fps;

    // Codec do container já é um formato lossy conhecido (mp3, aac, atrac...).
    // Distinto da detecção espectral (Source/Ingest/Duplicata.h), que pega o
    // caso mais difícil: fonte lossy reencodada pra PCM, escondendo o codec.
    bool codecLossyDeclarado = false;
};

// Roda ffprobe (áudio/vídeo) ou exiftool (imagem/documento) conforme a
// categoria do arquivo. Lança LeituraTecnicaError se a ferramenta externa
// não estiver disponível ou retornar erro — nunca retorna um resultado
// parcial em silêncio.
LeituraTecnicaResultado lerTecnica(const juce::File& arquivo);

// Serializa `bruto` como texto JSON compacto, pronto para
// arquivo.caracteristicas_tecnicas_json.
std::string paraJson(const LeituraTecnicaResultado& r);

} // namespace matriz::ingest
