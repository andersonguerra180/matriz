#pragma once

#include <JuceHeader.h>

#include <optional>
#include <stdexcept>
#include <string>

// Estágio 1 da ingestão (§7.2): leitura automática do que já está no
// arquivo — nunca indexação de conteúdo.
//
// Áudio/vídeo/imagem (dimensão, formato, codec, sample rate, bit depth,
// fps, tags embutidas incluindo BWF/bext, ID3, Vorbis comment quando o
// container expõe) via ffprobe — a mesma ferramenta multiplataforma pros
// três, imagem estática é só "vídeo de um frame" do ponto de vista do
// ffprobe. EXIF completo de imagem (câmera, lente, orientação, GPS, data
// original) via Exiv2, linkado direto no binário via FetchContent — nem
// ffprobe nem Exiv2 dependem de ferramenta de sistema (ao contrário do
// `sips`/`mdls` usados numa versão anterior deste arquivo, exclusivos de
// macOS e removidos por quebrar o build no Windows).
//
// PDF: só fileSizeBytes por enquanto — ver a nota longa em
// LeituraTecnica.cpp sobre por que PDFium e MuPDF (as opções avaliadas)
// não têm caminho de FetchContent+CMake multiplataforma viável, e por que a
// contagem de páginas ficou explicitamente ausente em vez de usar o parser
// artesanal antigo (que "funcionava" só porque nunca foi testado contra um
// PDF real).

namespace matriz::ingest {

class LeituraTecnicaError : public std::runtime_error {
public:
    explicit LeituraTecnicaError(const std::string& message) : std::runtime_error(message) {}
};

enum class CategoriaMidia { Audio, Video, Imagem, Arte, Documento, Texto, Sessao, Desconhecida };

CategoriaMidia categoriaPorExtensao(const juce::File& arquivo);
CategoriaMidia categoriaPorExtensao(const juce::String& extensaoSemPonto);
juce::String detectarExtensaoPorAssinatura(const juce::File& arquivo);
juce::String obterLogoSessaoPorExtensao(const juce::String& extensaoSemPonto);
juce::String obterLogoParaExtensao(const juce::String& extensaoSemPonto);

struct LeituraTecnicaResultado {
    juce::var bruto; // objeto completo retornado por ffprobe (+ "exif" quando Exiv2 encontra dados), vira arquivo.caracteristicas_tecnicas_json

    std::optional<double> duracaoSegundos;
    std::optional<int> sampleRate;
    std::optional<int> bitDepth;
    std::optional<int> canais;
    std::string codec;
    std::optional<int> larguraPx;
    std::optional<int> alturaPx;
    std::optional<double> fps;

    // Codec do container já é um formato lossy conhecido (mp3, aac, atrac...).
    bool codecLossyDeclarado = false;

    // EXIF (só populado pra CategoriaMidia::Imagem)
    std::optional<std::string> exifDataOriginal;  // ISO 8601, de Exif.Photo.DateTimeOriginal
    std::optional<std::string> exifCamera;         // "Marca Modelo"
    std::optional<std::string> exifLente;
    std::optional<int> exifOrientacao;              // 1-8, convenção EXIF
    std::optional<double> exifGpsLatitude;
    std::optional<double> exifGpsLongitude;

    // Loudness EBU R128
    std::optional<double> lufsIntegrado;
    std::optional<double> lra;
    std::optional<double> picoDbfs;

    // Dublin Core / Embedded Metadata Extraídos no Ingest
    std::optional<std::string> metaTitle;
    std::optional<std::string> metaCreator;
    std::optional<std::string> metaContributor;
    std::optional<std::string> metaSubject;
    std::optional<std::string> metaDescription;
    std::optional<std::string> metaPublisher;
    std::optional<std::string> metaDate;
    std::optional<std::string> metaType;
    std::optional<std::string> metaFormat;
    std::optional<std::string> metaIdentifier;
    std::optional<std::string> metaRights;
    std::optional<std::string> metaLanguage;
    std::optional<std::string> metaSource;
    std::optional<std::string> metaCoverage;
    std::optional<int> pageCount;
};

// Roda ffprobe (áudio/vídeo/imagem) ou o leitor de PDF conforme a categoria
// do arquivo, e Exiv2 adicionalmente pra imagem. Lança LeituraTecnicaError
// se a ferramenta não estiver disponível ou retornar erro — nunca retorna
// um resultado parcial em silêncio (EXIF ausente é uma exceção deliberada:
// ver nota em LeituraTecnica.cpp).
LeituraTecnicaResultado lerTecnica(const juce::File& arquivo);

// Serializa `bruto` como texto JSON compacto, pronto para
// arquivo.caracteristicas_tecnicas_json.
std::string paraJson(const LeituraTecnicaResultado& r);

} // namespace matriz::ingest
