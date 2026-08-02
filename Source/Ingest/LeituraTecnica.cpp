#include "LeituraTecnica.h"

#include "ProcessoExterno.h"

#include <memory>
#include <set>
#include <string_view>

namespace matriz::ingest {

namespace {

const std::set<juce::String>& extensoesAudio() {
    static const std::set<juce::String> s = {"wav", "aiff", "aif", "flac", "mp3", "m4a", "aac", "ogg", "wma", "dsf"};
    return s;
}
const std::set<juce::String>& extensoesVideo() {
    static const std::set<juce::String> s = {"mov", "mp4", "avi", "mkv", "mxf", "m4v", "wmv"};
    return s;
}
const std::set<juce::String>& extensoesImagem() {
    static const std::set<juce::String> s = {"jpg", "jpeg", "png", "tif", "tiff", "bmp", "heic", "dng", "cr2", "nef"};
    return s;
}
const std::set<juce::String>& extensoesDocumento() {
    static const std::set<juce::String> s = {"pdf"};
    return s;
}

const std::set<juce::String>& codecsAudioLossy() {
    static const std::set<juce::String> s = {
        "mp3", "aac", "ac3", "eac3", "vorbis", "opus", "wmav1", "wmav2",
        "atrac3", "atrac3p", "atrac9", "amr_nb", "amr_wb", "gsm", "mp2"};
    return s;
}

std::string runProcess(const juce::StringArray& args, const std::string& nomeFerramenta) {
    try {
        return capturarSaidaTexto(args, nomeFerramenta);
    } catch (const ProcessoExternoError& e) {
        throw LeituraTecnicaError(e.what());
    }
}

std::optional<double> parseFraction(const juce::String& s) {
    int slash = s.indexOfChar('/');
    if (slash < 0) return s.getDoubleValue();
    double num = s.substring(0, slash).getDoubleValue();
    double den = s.substring(slash + 1).getDoubleValue();
    if (den == 0.0) return std::nullopt;
    return num / den;
}

LeituraTecnicaResultado lerViaFfprobe(const juce::File& arquivo) {
    juce::StringArray args{"ffprobe", "-v", "quiet", "-print_format", "json",
                            "-show_format", "-show_streams", arquivo.getFullPathName()};
    std::string output = runProcess(args, "ffprobe");
    if (output.empty())
        throw LeituraTecnicaError("ffprobe não retornou dados para: " + arquivo.getFullPathName().toStdString());

    juce::var root = juce::JSON::parse(juce::String(output));
    if (!root.isObject())
        throw LeituraTecnicaError("ffprobe: saída JSON inválida para: " + arquivo.getFullPathName().toStdString());

    LeituraTecnicaResultado r;
    r.bruto = root;

    juce::var format = root["format"];
    if (format.isObject() && format.hasProperty("duration"))
        r.duracaoSegundos = juce::String(format["duration"].toString()).getDoubleValue();

    juce::var streams = root["streams"];
    if (streams.isArray()) {
        for (auto& streamVar : *streams.getArray()) {
            juce::String tipo = streamVar["codec_type"].toString();
            if (tipo == "audio" && r.sampleRate == std::nullopt) {
                r.sampleRate = streamVar["sample_rate"].toString().getIntValue();
                juce::String bits = streamVar["bits_per_raw_sample"].toString();
                if (bits.isEmpty()) bits = streamVar["bits_per_sample"].toString();
                if (bits.isNotEmpty() && bits.getIntValue() > 0) r.bitDepth = bits.getIntValue();
                r.canais = streamVar["channels"].toString().getIntValue();
                r.codec = streamVar["codec_name"].toString().toStdString();
                if (!r.duracaoSegundos && streamVar.hasProperty("duration"))
                    r.duracaoSegundos = juce::String(streamVar["duration"].toString()).getDoubleValue();
                if (codecsAudioLossy().count(juce::String(r.codec)) > 0)
                    r.codecLossyDeclarado = true;
            } else if (tipo == "video" && r.larguraPx == std::nullopt) {
                r.larguraPx = streamVar["width"].toString().getIntValue();
                r.alturaPx = streamVar["height"].toString().getIntValue();
                if (r.codec.empty()) r.codec = streamVar["codec_name"].toString().toStdString();
                juce::String frameRate = streamVar["r_frame_rate"].toString();
                if (frameRate.isNotEmpty()) r.fps = parseFraction(frameRate);
                if (!r.duracaoSegundos && streamVar.hasProperty("duration"))
                    r.duracaoSegundos = juce::String(streamVar["duration"].toString()).getDoubleValue();
            }
        }
    }

    return r;
}

// `sips -g all <arquivo>` imprime uma linha de caminho seguida de
// "  chave: valor" por linha. Sem JSON — parseamos o texto na mão.
juce::var parseSaidaSips(const std::string& output) {
    auto obj = std::make_unique<juce::DynamicObject>();
    juce::StringArray linhas;
    linhas.addLines(juce::String(output));
    for (auto& linha : linhas) {
        int doisPontos = linha.indexOfChar(':');
        if (doisPontos < 0) continue; // primeira linha é o caminho do arquivo, sem ':'
        juce::String chave = linha.substring(0, doisPontos).trim();
        juce::String valor = linha.substring(doisPontos + 1).trim();
        if (chave.isEmpty()) continue;
        if (valor.containsOnly("0123456789.-"))
            obj->setProperty(chave, valor.getDoubleValue());
        else
            obj->setProperty(chave, valor);
    }
    return juce::var(obj.release());
}

LeituraTecnicaResultado lerViaSips(const juce::File& arquivo) {
    juce::StringArray args{"sips", "-g", "all", arquivo.getFullPathName()};
    std::string output = runProcess(args, "sips");
    if (output.empty())
        throw LeituraTecnicaError("sips não retornou dados para: " + arquivo.getFullPathName().toStdString());

    LeituraTecnicaResultado r;
    r.bruto = parseSaidaSips(output);

    if (r.bruto.hasProperty("pixelWidth")) r.larguraPx = static_cast<int>(static_cast<double>(r.bruto["pixelWidth"]));
    if (r.bruto.hasProperty("pixelHeight")) r.alturaPx = static_cast<int>(static_cast<double>(r.bruto["pixelHeight"]));
    if (r.bruto.hasProperty("format")) r.codec = r.bruto["format"].toString().toStdString();
    if (r.bruto.hasProperty("bitsPerSample")) r.bitDepth = static_cast<int>(static_cast<double>(r.bruto["bitsPerSample"]));

    return r;
}

// Contagem de páginas de PDF por varredura do arquivo bruto: conta
// ocorrências de "/Type /Page" que não são "/Type /Pages" (o nó pai da
// árvore). Cobre o caso comum (PDF sem cross-reference stream comprimido).
// PDFs com árvore de páginas dentro de object streams comprimidos (comuns em
// exportações mais recentes) não são cobertos por este método — nesse caso o
// resultado fica ausente (nulo), nunca um número inventado.
std::optional<int> contarPaginasPdf(const juce::File& arquivo) {
    juce::MemoryBlock bloco;
    if (!arquivo.loadFileAsData(bloco) || bloco.getSize() == 0)
        return std::nullopt;

    std::string_view texto(static_cast<const char*>(bloco.getData()), bloco.getSize());
    int contagem = 0;
    size_t pos = 0;
    const std::string_view alvo = "/Type";
    while ((pos = texto.find(alvo, pos)) != std::string_view::npos) {
        size_t depoisTipo = pos + alvo.size();
        size_t inicioValor = texto.find_first_not_of(" \t\r\n", depoisTipo);
        if (inicioValor != std::string_view::npos && texto.compare(inicioValor, 6, "/Pages") != 0 &&
            texto.compare(inicioValor, 5, "/Page") == 0) {
            ++contagem;
        }
        pos = depoisTipo;
    }
    return contagem > 0 ? std::optional<int>(contagem) : std::nullopt;
}

LeituraTecnicaResultado lerDocumentoPdf(const juce::File& arquivo) {
    LeituraTecnicaResultado r;
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("fileSizeBytes", static_cast<juce::int64>(arquivo.getSize()));

    std::optional<int> paginas = contarPaginasPdf(arquivo);
    if (paginas) obj->setProperty("pageCountEstimado", *paginas);

    r.bruto = juce::var(obj.release());
    return r;
}

} // namespace

CategoriaMidia categoriaPorExtensao(const juce::File& arquivo) {
    juce::String ext = arquivo.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    if (extensoesAudio().count(ext) > 0) return CategoriaMidia::Audio;
    if (extensoesVideo().count(ext) > 0) return CategoriaMidia::Video;
    if (extensoesImagem().count(ext) > 0) return CategoriaMidia::Imagem;
    if (extensoesDocumento().count(ext) > 0) return CategoriaMidia::Documento;
    return CategoriaMidia::Desconhecida;
}

LeituraTecnicaResultado lerTecnica(const juce::File& arquivo) {
    if (!arquivo.existsAsFile())
        throw LeituraTecnicaError("arquivo não encontrado: " + arquivo.getFullPathName().toStdString());

    switch (categoriaPorExtensao(arquivo)) {
        case CategoriaMidia::Audio:
        case CategoriaMidia::Video:
            return lerViaFfprobe(arquivo);
        case CategoriaMidia::Imagem:
            return lerViaSips(arquivo);
        case CategoriaMidia::Documento:
            return lerDocumentoPdf(arquivo);
        case CategoriaMidia::Desconhecida:
            throw LeituraTecnicaError("extensão não reconhecida para leitura técnica: " + arquivo.getFullPathName().toStdString());
    }
    throw LeituraTecnicaError("categoria de mídia inesperada");
}

std::string paraJson(const LeituraTecnicaResultado& r) {
    return juce::JSON::toString(r.bruto, true).toStdString();
}

} // namespace matriz::ingest
