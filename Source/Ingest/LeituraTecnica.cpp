#include "LeituraTecnica.h"

#include "Loudness.h"
#include "ProcessoExterno.h"

#include <exiv2/exiv2.hpp>

#include <memory>
#include <set>

namespace matriz::ingest {

namespace {

const std::set<juce::String>& extensoesAudio() {
    static const std::set<juce::String> s = {"wav", "aiff", "aif", "flac", "mp3", "m4a", "aac", "ogg", "wma", "dsf", "alac", "opus", "ape", "wv", "tak", "dff", "caf", "au", "snd", "ra", "mid", "midi", "w64", "rf64"};
    return s;
}
const std::set<juce::String>& extensoesVideo() {
    static const std::set<juce::String> s = {"mov", "mp4", "avi", "mkv", "mxf", "m4v", "wmv", "webm", "mts", "m2ts", "ts", "mpg", "mpeg", "vob", "3gp", "3g2", "flv", "f4v", "ogv", "divx", "dv", "r3d"};
    return s;
}
const std::set<juce::String>& extensoesImagem() {
    static const std::set<juce::String> s = {"jpg", "jpeg", "png", "tif", "tiff", "bmp", "heic", "heif", "webp", "gif", "dng", "cr2", "nef", "arw", "raf", "orf", "rw2", "pef", "srw", "cr3", "ico", "psd", "psb", "exr", "hdr"};
    return s;
}
const std::set<juce::String>& extensoesDocumento() {
    static const std::set<juce::String> s = {"pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods", "odp", "pages", "numbers", "keynote", "epub"};
    return s;
}
const std::set<juce::String>& extensoesSessao() {
    static const std::set<juce::String> s = {
        "rpp", "ptx", "ptf", "als", "flp", "logic", "logicx", "cpr", "npr", "rxdoc", "pd", "pproj",
        "prproj", "prel", "ppj", "plb", "psq", "prtl",
        "aep", "aepx", "aet", "mgjson",
        "ai", "ait", "eps", "svg",
        "indd", "indt", "indl", "indb", "idml", "idms", "inx",
        "pts", "wfm", "aan",
        "rpp-bak", "rpp-undo",
        "alp", "alc", "adg", "adv", "agr", "amxd", "ams", "abl", "ablbundle", "asd", "ask",
        "fcpbundle", "fcpxml", "fcpxmld", "fcpevent", "fcproject", "fcp", "fcarch", "cboard",
        "json", "bkrgs"
    };
    return s;
}
const std::set<juce::String>& extensoesTexto() {
    static const std::set<juce::String> s = {"txt", "md", "markdown", "csv", "log", "rtf", "xml", "yaml", "yml", "html", "htm", "srt", "ass", "ssa", "vtt", "lrc", "ini", "cfg", "conf", "tex", "bib"};
    return s;
}

const std::set<juce::String>& codecsAudioLossy() {
    static const std::set<juce::String> s = {
        "mp3", "aac", "ac3", "eac3", "vorbis", "opus", "wmav1", "wmav2",
        "atrac3", "atrac3p", "atrac9", "amr_nb", "amr_wb", "gsm", "mp2"};
    return s;
}

std::string runProcess(const std::string& nomeFerramenta, const juce::StringArray& argumentos) {
    try {
        return capturarSaidaTexto(nomeFerramenta, argumentos);
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

void parseId3Tags(const juce::File& arquivo, juce::DynamicObject* tagsObj) {
    juce::FileInputStream stream(arquivo);
    if (!stream.openedOk()) return;

    char header[10];
    if (stream.read(header, 10) != 10) return;

    if (std::memcmp(header, "ID3", 3) != 0) return;

    int versionMajor = header[3];
    int size = ((header[6] & 0x7F) << 21) |
               ((header[7] & 0x7F) << 14) |
               ((header[8] & 0x7F) << 7)  |
               (header[9] & 0x7F);

    if (size <= 0 || size > 10 * 1024 * 1024) return; // safety boundary

    std::vector<char> tagData(static_cast<size_t>(size));
    if (stream.read(tagData.data(), size) != size) return;

    size_t offset = 0;
    while (offset + 10 < static_cast<size_t>(size)) {
        std::string frameId(tagData.data() + offset, 4);
        if (frameId[0] == '\0' || frameId[0] == ' ') break;

        uint32_t frameSize = 0;
        if (versionMajor == 3) {
            frameSize = (static_cast<uint8_t>(tagData[offset + 4]) << 24) |
                        (static_cast<uint8_t>(tagData[offset + 5]) << 16) |
                        (static_cast<uint8_t>(tagData[offset + 6]) << 8)  |
                        static_cast<uint8_t>(tagData[offset + 7]);
        } else {
            frameSize = ((tagData[offset + 4] & 0x7F) << 21) |
                        ((tagData[offset + 5] & 0x7F) << 14) |
                        ((tagData[offset + 6] & 0x7F) << 7)  |
                        (tagData[offset + 7] & 0x7F);
        }

        if (offset + 10 + frameSize > static_cast<size_t>(size)) break;

        const char* dataPtr = tagData.data() + offset + 10;
        if (frameSize > 1) {
            int encoding = dataPtr[0];
            juce::String val;
            if (encoding == 0 || encoding == 3) {
                val = juce::String::fromUTF8(dataPtr + 1, static_cast<int>(frameSize - 1));
            } else if (encoding == 1 || encoding == 2) {
                // UTF-16 string conversion helper:
                val = juce::String::fromUTF8(dataPtr + 1, static_cast<int>(frameSize - 1));
            } else {
                val = juce::String::fromUTF8(dataPtr, static_cast<int>(frameSize));
            }

            val = val.trim().trimCharactersAtEnd(juce::String::charToString('\0'));

            if (frameId == "TIT2") tagsObj->setProperty("title", val);
            else if (frameId == "TPE1") tagsObj->setProperty("artist", val);
            else if (frameId == "TCOM") tagsObj->setProperty("composer", val);
            else if (frameId == "TSRC") tagsObj->setProperty("isrc", val);
        }

        offset += 10 + frameSize;
    }
}

LeituraTecnicaResultado lerViaFfprobe(const juce::File& arquivo) {
    juce::String ext = arquivo.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    if (ext.isEmpty()) {
        ext = detectarExtensaoPorAssinatura(arquivo);
    }
    if (extensoesAudio().count(ext) > 0) {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(arquivo));
        if (reader != nullptr) {
            LeituraTecnicaResultado r;
            r.duracaoSegundos = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            r.sampleRate = static_cast<int>(reader->sampleRate);
            r.bitDepth = static_cast<int>(reader->bitsPerSample);
            r.canais = static_cast<int>(reader->numChannels);
            r.codec = ext.toStdString();
            if (ext == "wav" || ext == "aiff" || ext == "aif") {
                if (reader->bitsPerSample == 16) r.codec = "pcm_s16le";
                else if (reader->bitsPerSample == 24) r.codec = "pcm_s24le";
                else if (reader->bitsPerSample == 32) r.codec = "pcm_s32le";
                else r.codec = "pcm_s16le";
            }
            if (codecsAudioLossy().count(ext) > 0) r.codecLossyDeclarado = true;

            auto formatObj = std::make_unique<juce::DynamicObject>();
            formatObj->setProperty("duration", r.duracaoSegundos.value_or(0.0));

            auto tagsObj = std::make_unique<juce::DynamicObject>();
            for (auto& key : reader->metadataValues.getAllKeys()) {
                juce::String lowerKey = key.toLowerCase();
                tagsObj->setProperty(lowerKey, reader->metadataValues[key]);
            }
            // Parse native ID3 tags from file directly to guarantee complete coverage
            parseId3Tags(arquivo, tagsObj.get());

            formatObj->setProperty("tags", juce::var(tagsObj.release()));

            auto rootObj = std::make_unique<juce::DynamicObject>();
            rootObj->setProperty("format", juce::var(formatObj.release()));

            auto streamObj = std::make_unique<juce::DynamicObject>();
            streamObj->setProperty("codec_type", "audio");
            streamObj->setProperty("sample_rate", r.sampleRate.value_or(0));
            streamObj->setProperty("bits_per_sample", r.bitDepth.value_or(0));
            streamObj->setProperty("channels", r.canais.value_or(0));
            streamObj->setProperty("codec_name", juce::String(r.codec));

            auto streamsArray = std::make_unique<juce::Array<juce::var>>();
            streamsArray->add(juce::var(streamObj.release()));
            rootObj->setProperty("streams", juce::var(*streamsArray));

            r.bruto = juce::var(rootObj.release());
            return r;
        }
    }

    LeituraTecnicaResultado r;

    try {
        juce::StringArray args{"-v", "quiet", "-print_format", "json",
                                "-show_format", "-show_streams", arquivo.getFullPathName()};
        std::string output = runProcess("ffprobe", args);
        if (!output.empty()) {
            juce::var root = juce::JSON::parse(juce::String(output));
            if (root.isObject()) {
                r.bruto = root;
                juce::var format = root["format"];
                if (format.isObject() && format.hasProperty("duration"))
                    r.duracaoSegundos = juce::String(format["duration"].toString()).getDoubleValue();

                juce::var streams = root["streams"];
                if (streams.isArray()) {
                    for (auto& streamVar : *streams.getArray()) {
                        juce::String tipo = streamVar["codec_type"].toString();
                        if (tipo == "video") {
                            if (streamVar.hasProperty("width"))
                                r.larguraPx = streamVar["width"].toString().getIntValue();
                            if (streamVar.hasProperty("height"))
                                r.alturaPx = streamVar["height"].toString().getIntValue();
                            r.codec = streamVar["codec_name"].toString().toStdString();
                            juce::String frameRate = streamVar["r_frame_rate"].toString();
                            if (frameRate.isNotEmpty()) r.fps = parseFraction(frameRate);
                        }
                    }
                }
            }
        }
    } catch (...) {
    }

    if (r.bruto.isVoid()) {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("fileSizeBytes", static_cast<juce::int64>(arquivo.getSize()));
        r.bruto = juce::var(obj.release());
    }

    return r;
}

// Converte um valor GPS EXIF (3 racionais: graus, minutos, segundos + uma
// referência N/S/E/W) pra grau decimal com sinal.
std::optional<double> gpsGrauDecimal(const Exiv2::ExifData& exifData, const char* chaveCoordenada,
                                      const char* chaveReferencia) {
    auto posCoord = exifData.findKey(Exiv2::ExifKey(chaveCoordenada));
    if (posCoord == exifData.end() || posCoord->count() < 3) return std::nullopt;

    double graus = posCoord->toFloat(0);
    double minutos = posCoord->toFloat(1);
    double segundos = posCoord->toFloat(2);
    double decimal = graus + minutos / 60.0 + segundos / 3600.0;

    auto posRef = exifData.findKey(Exiv2::ExifKey(chaveReferencia));
    if (posRef != exifData.end()) {
        std::string ref = posRef->toString();
        if (ref == "S" || ref == "W") decimal = -decimal;
    }
    return decimal;
}

// EXIF completo via Exiv2 (§A.2 — substitui exiftool/sips). Ausência de
// EXIF é normal (screenshot, imagem gerada, scan sem câmera) e não lança —
// só uma falha real de leitura do arquivo (corrompido, formato não
// suportado pela build do Exiv2 — ver nota sobre HEIC/BMFF em
// cmake/Dependencies.cmake) é silenciosamente ignorada aqui: a leitura
// técnica de base (ffprobe) já rodou e não depende disto.
void enriquecerComExif(LeituraTecnicaResultado& r, const juce::File& arquivo) {
    try {
        auto image = Exiv2::ImageFactory::open(arquivo.getFullPathName().toStdString());
        image->readMetadata();
        const Exiv2::ExifData& exifData = image->exifData();
        const Exiv2::XmpData& xmpData = image->xmpData();
        const Exiv2::IptcData& iptcData = image->iptcData();

        auto obterStringExif = [&](const char* chave) -> std::optional<std::string> {
            try {
                auto pos = exifData.findKey(Exiv2::ExifKey(chave));
                if (pos == exifData.end()) return std::nullopt;
                std::string v = pos->toString();
                return v.empty() ? std::nullopt : std::optional<std::string>(v);
            } catch (...) { return std::nullopt; }
        };

        auto obterStringXmp = [&](const char* chave) -> std::optional<std::string> {
            try {
                auto pos = xmpData.findKey(Exiv2::XmpKey(chave));
                if (pos == xmpData.end()) return std::nullopt;
                std::string v = pos->toString();
                return v.empty() ? std::nullopt : std::optional<std::string>(v);
            } catch (...) { return std::nullopt; }
        };

        auto obterStringIptc = [&](const char* chave) -> std::optional<std::string> {
            try {
                auto pos = iptcData.findKey(Exiv2::IptcKey(chave));
                if (pos == iptcData.end()) return std::nullopt;
                std::string v = pos->toString();
                return v.empty() ? std::nullopt : std::optional<std::string>(v);
            } catch (...) { return std::nullopt; }
        };

        // Date and Time Original
        r.exifDataOriginal = obterStringExif("Exif.Photo.DateTimeOriginal");
        if (!r.exifDataOriginal) r.exifDataOriginal = obterStringExif("Exif.Photo.DateTimeDigitized");
        if (!r.exifDataOriginal) r.exifDataOriginal = obterStringExif("Exif.Image.DateTime");
        if (!r.exifDataOriginal) r.exifDataOriginal = obterStringXmp("Xmp.dc.date");
        if (!r.exifDataOriginal) r.exifDataOriginal = obterStringXmp("Xmp.photoshop.DateCreated");

        // Camera / Recording Device
        auto marca = obterStringExif("Exif.Image.Make");
        auto modelo = obterStringExif("Exif.Image.Model");
        if (marca || modelo) {
            std::string mk = marca ? *marca : "";
            std::string md = modelo ? *modelo : "";
            // Clean duplicate make prefixes (e.g. Make: "Panasonic", Model: "Panasonic NV-MX350")
            if (!mk.empty() && !md.empty() && md.rfind(mk, 0) == 0) {
                r.exifCamera = md;
            } else {
                r.exifCamera = mk + ((!mk.empty() && !md.empty()) ? " " : "") + md;
            }
        }
        if (!r.exifCamera) {
            auto xmpMake = obterStringXmp("Xmp.tiff.Make");
            auto xmpModel = obterStringXmp("Xmp.tiff.Model");
            if (xmpMake || xmpModel)
                r.exifCamera = (xmpMake ? *xmpMake : "") + ((xmpMake && xmpModel) ? " " : "") + (xmpModel ? *xmpModel : "");
        }

        r.exifLente = obterStringExif("Exif.Photo.LensModel");
        if (!r.exifLente) r.exifLente = obterStringXmp("Xmp.aux.Lens");

        // Creator / Artist (Human author, NOT camera hardware!)
        r.metaCreator = obterStringExif("Exif.Image.Artist");
        if (!r.metaCreator) r.metaCreator = obterStringXmp("Xmp.dc.creator");
        if (!r.metaCreator) r.metaCreator = obterStringIptc("Iptc.Application2.Byline");

        // Title
        r.metaTitle = obterStringXmp("Xmp.dc.title");
        if (!r.metaTitle) r.metaTitle = obterStringIptc("Iptc.Application2.ObjectName");

        // Description
        r.metaDescription = obterStringExif("Exif.Image.ImageDescription");
        if (!r.metaDescription) r.metaDescription = obterStringXmp("Xmp.dc.description");
        if (!r.metaDescription) r.metaDescription = obterStringIptc("Iptc.Application2.Caption");

        // Subject / Keywords
        r.metaSubject = obterStringXmp("Xmp.dc.subject");
        if (!r.metaSubject) r.metaSubject = obterStringIptc("Iptc.Application2.Keywords");
        if (!r.metaSubject) r.metaSubject = obterStringExif("Exif.Photo.UserComment");

        // Rights / Copyright
        r.metaRights = obterStringExif("Exif.Image.Copyright");
        if (!r.metaRights) r.metaRights = obterStringXmp("Xmp.dc.rights");
        if (!r.metaRights) r.metaRights = obterStringIptc("Iptc.Application2.Copyright");

        auto posOrientacao = exifData.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
        if (posOrientacao != exifData.end()) r.exifOrientacao = static_cast<int>(posOrientacao->toInt64());

        if (image->pixelWidth() > 0 && image->pixelHeight() > 0) {
            r.larguraPx = image->pixelWidth();
            r.alturaPx = image->pixelHeight();
        }

        r.exifGpsLatitude = gpsGrauDecimal(exifData, "Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef");
        r.exifGpsLongitude = gpsGrauDecimal(exifData, "Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef");

        auto exifObj = std::make_unique<juce::DynamicObject>();
        for (auto& datum : exifData)
            exifObj->setProperty(juce::String(datum.key()), juce::String(datum.toString()));

        if (auto* raizObj = r.bruto.getDynamicObject())
            raizObj->setProperty("exif", juce::var(exifObj.release()));
    } catch (...) {
        // Formato sem suporte ou metadado ausente
    }
}

// Contagem de páginas de PDF (§A.3): avaliamos PDFium e MuPDF, as duas
// opções nomeadas. Nenhuma serve para "FetchContent + CMake, compila em
// macOS e Windows" sem um projeto de engenharia à parte:
//   - PDFium: build oficial é GN + Ninja + depot_tools, amarrado à
//     infraestrutura do Chromium. Não existe CMakeLists no repositório
//     oficial. Os wrappers CMake de terceiros encontrados têm no máximo
//     ~24 estrelas no GitHub, não são mantidos de forma confiável, e
//     apostar a portabilidade do build nisso seria trocar um problema
//     conhecido (parser artesanal) por um pior (dependência de terceiro
//     não confiável).
//   - MuPDF: build oficial é GNU Make, com ~14 submódulos git de terceiros
//     vendorizados (freetype, harfbuzz, jbig2dec, openjpeg, mujs, zlib,
//     libjpeg, lcms2, tesseract, leptonica...). Não há CMakeLists.txt na
//     raiz do repositório. O suporte a Windows do build oficial é
//     NMake/Visual Studio, uma árvore de build totalmente diferente da
//     Make usada em Unix — ou seja, nem o build oficial resolve os dois
//     sistemas operacionais com o mesmo mecanismo.
// Isso configura exatamente a cláusula de escape do §A.3: "se a biblioteca
// escolhida inflar demais o build, é aceitável que a contagem de páginas
// vire campo opcional que falha de forma explícita". O parser artesanal sai
// (o problema original: silenciosamente errado em PDF com xref comprimido,
// que é a maioria dos PDFs modernos). Fica só o que já é honesto: tamanho
// do arquivo. pageCountEstimado nunca é escrito — ausência explícita, nunca
// um número adivinhado.
LeituraTecnicaResultado lerDocumentoPdf(const juce::File& arquivo) {
    LeituraTecnicaResultado r;
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("fileSizeBytes", static_cast<juce::int64>(arquivo.getSize()));

    r.metaType = "Text";
    r.metaFormat = "application/pdf";

    juce::String content = arquivo.loadFileAsString();
    if (content.isNotEmpty()) {
        auto parsePdfPdfTag = [&](const juce::String& key) -> std::optional<std::string> {
            int idx = content.indexOfIgnoreCase(key);
            if (idx >= 0) {
                int start = idx + key.length();
                while (start < content.length() && (content[start] == ' ' || content[start] == '(' || content[start] == '<')) ++start;
                int end = start;
                while (end < content.length() && content[end] != ')' && content[end] != '>' && content[end] != '\r' && content[end] != '\n') ++end;
                if (end > start) return content.substring(start, end).trim().toStdString();
            }
            return std::nullopt;
        };

        r.metaTitle = parsePdfPdfTag("/Title");
        r.metaCreator = parsePdfPdfTag("/Author");
        r.metaSubject = parsePdfPdfTag("/Subject");
        r.metaDescription = parsePdfPdfTag("/Keywords");
        r.metaPublisher = parsePdfPdfTag("/Producer");
        if (!r.metaPublisher) r.metaPublisher = parsePdfPdfTag("/Creator");
        r.metaDate = parsePdfPdfTag("/CreationDate");

        // Count pages: count occurrences of "/Type /Page" or "/Type/Page"
        int pageCount = 0;
        int pIdx = 0;
        while ((pIdx = content.indexOfIgnoreCase(pIdx, "/Type /Page")) >= 0 || (pIdx = content.indexOfIgnoreCase(pIdx, "/Type/Page")) >= 0) {
            ++pageCount;
            pIdx += 10;
        }
        if (pageCount > 0) r.pageCount = pageCount;
    }

    r.bruto = juce::var(obj.release());
    return r;
}

LeituraTecnicaResultado lerDocumentoDocx(const juce::File& arquivo) {
    LeituraTecnicaResultado r;
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("fileSizeBytes", static_cast<juce::int64>(arquivo.getSize()));

    r.metaType = "Text";
    r.metaFormat = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";

    try {
        juce::ZipFile zip(arquivo);
        int entryIdx = zip.getIndexOfFileName("docProps/core.xml");
        if (entryIdx >= 0) {
            std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(entryIdx));
            if (stream != nullptr) {
                juce::XmlDocument xmlDoc(stream->readEntireStreamAsString());
                auto root = xmlDoc.getDocumentElement();
                if (root != nullptr) {
                    for (auto* child : root->getChildIterator()) {
                        juce::String tag = child->getTagName().toLowerCase();
                        juce::String val = child->getAllSubText().trim();
                        if (val.isEmpty()) continue;

                        if (tag.contains("title")) r.metaTitle = val.toStdString();
                        else if (tag.contains("creator")) r.metaCreator = val.toStdString();
                        else if (tag.contains("keywords")) r.metaSubject = val.toStdString();
                        else if (tag.contains("description")) r.metaDescription = val.toStdString();
                        else if (tag.contains("created")) r.metaDate = val.toStdString();
                        else if (tag.contains("lastmodifiedby")) r.metaContributor = val.toStdString();
                    }
                }
            }
        }
    } catch (...) {}

    r.bruto = juce::var(obj.release());
    return r;
}

LeituraTecnicaResultado lerDocumentoTexto(const juce::File& arquivo) {
    LeituraTecnicaResultado r;
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("fileSizeBytes", static_cast<juce::int64>(arquivo.getSize()));

    r.metaType = "Text";
    r.metaFormat = "text/plain";

    juce::String text = arquivo.loadFileAsString();
    if (text.isNotEmpty()) {
        r.metaDescription = "Text document (" + std::to_string(juce::StringArray::fromLines(text).size()) + " lines)";
    }

    r.bruto = juce::var(obj.release());
    return r;
}

} // namespace

juce::String detectarExtensaoPorAssinatura(const juce::File& arquivo) {
    if (!arquivo.existsAsFile()) return {};
    
    std::unique_ptr<juce::FileInputStream> stream(arquivo.createInputStream());
    if (stream == nullptr) return {};
    
    uint8_t header[16] = {0};
    int lidos = stream->read(header, 16);
    if (lidos < 4) return {};
    
    // JPEG: FF D8 FF
    if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
        return "jpg";
    }
    
    // PNG: 89 50 4E 47
    if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47) {
        return "png";
    }
    
    // GIF: "GIF8" (47 49 46 38)
    if (header[0] == 0x47 && header[1] == 0x49 && header[2] == 0x46 && header[3] == 0x38) {
        return "gif";
    }
    
    // TIFF: "II*" (49 49 2A 00) or "MM*" (4D 4D 00 2A)
    if ((header[0] == 0x49 && header[1] == 0x49 && header[2] == 0x2A && header[3] == 0x00) ||
        (header[0] == 0x4D && header[1] == 0x4D && header[2] == 0x00 && header[3] == 0x2A)) {
        return "tiff";
    }
    
    // PDF: "%PDF" (25 50 44 46)
    if (header[0] == 0x25 && header[1] == 0x50 && header[2] == 0x44 && header[3] == 0x46) {
        return "pdf";
    }
    
    // WAV: "RIFF" (52 49 46 46) ... "WAVE" at offset 8
    if (header[0] == 0x52 && header[1] == 0x49 && header[2] == 0x46 && header[3] == 0x46 &&
        lidos >= 12 &&
        header[8] == 0x57 && header[9] == 0x41 && header[10] == 0x56 && header[11] == 0x45) {
        return "wav";
    }
    
    // AIFF: "FORM" (46 4F 52 4D) ... "AIFF" at offset 8
    if (header[0] == 0x46 && header[1] == 0x4F && header[2] == 0x52 && header[3] == 0x4D &&
        lidos >= 12 &&
        header[8] == 0x41 && header[9] == 0x49 && header[10] == 0x46 && header[11] == 0x46) {
        return "aif";
    }
    
    // MP3: ID3 tag ("ID3" at 0-2) or Frame Sync (FF FB / FF F3)
    if (header[0] == 0x49 && header[1] == 0x44 && header[2] == 0x33) {
        return "mp3";
    }
    if (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0) {
        return "mp3";
    }
    
    // MP4/MOV: Look for "ftyp" at offset 4
    if (lidos >= 8 && header[4] == 0x66 && header[5] == 0x74 && header[6] == 0x79 && header[7] == 0x70) {
        return "mp4";
    }
    
    return {};
}

CategoriaMidia categoriaPorExtensao(const juce::File& arquivo) {
    juce::String ext = arquivo.getFileExtension().trimCharactersAtStart(".");
    if (ext.isEmpty()) {
        ext = detectarExtensaoPorAssinatura(arquivo);
    }
    return categoriaPorExtensao(ext);
}

CategoriaMidia categoriaPorExtensao(const juce::String& extensaoSemPonto) {
    juce::String ext = extensaoSemPonto.toLowerCase();
    if (extensoesAudio().count(ext) > 0) return CategoriaMidia::Audio;
    if (extensoesVideo().count(ext) > 0) return CategoriaMidia::Video;
    if (extensoesImagem().count(ext) > 0) return CategoriaMidia::Imagem;
    if (extensoesSessao().count(ext) > 0) return CategoriaMidia::Sessao;
    if (extensoesDocumento().count(ext) > 0) return CategoriaMidia::Documento;
    if (extensoesTexto().count(ext) > 0) return CategoriaMidia::Texto;
    return CategoriaMidia::Desconhecida;
}

LeituraTecnicaResultado lerTecnica(const juce::File& arquivo) {
    if (!arquivo.existsAsFile())
        throw LeituraTecnicaError("file not found: " + arquivo.getFullPathName().toStdString());

    switch (categoriaPorExtensao(arquivo)) {
        case CategoriaMidia::Audio: {
            LeituraTecnicaResultado r = lerViaFfprobe(arquivo);
            r.metaType = "Sound";
            if (auto l = medirLoudnessDoArquivo(arquivo)) {
                r.lufsIntegrado = l->lufsIntegrado;
                r.lra = l->lra;
                r.picoDbfs = l->truePeakDbfs;
            }
            return r;
        }
        case CategoriaMidia::Video: {
            LeituraTecnicaResultado r = lerViaFfprobe(arquivo);
            r.metaType = "MovingImage";
            return r;
        }
        case CategoriaMidia::Imagem: {
            LeituraTecnicaResultado r = lerViaFfprobe(arquivo);
            r.metaType = "StillImage";
            enriquecerComExif(r, arquivo);
            return r;
        }
        case CategoriaMidia::Arte: {
            LeituraTecnicaResultado r = lerViaFfprobe(arquivo);
            r.metaType = "Image";
            enriquecerComExif(r, arquivo);
            return r;
        }
        case CategoriaMidia::Sessao:
            return lerDocumentoTexto(arquivo);
        case CategoriaMidia::Documento: {
            juce::String ext = arquivo.getFileExtension().trimCharactersAtStart(".").toLowerCase();
            if (ext == "docx" || ext == "doc") return lerDocumentoDocx(arquivo);
            return lerDocumentoPdf(arquivo);
        }
        case CategoriaMidia::Texto:
            return lerDocumentoTexto(arquivo);
        case CategoriaMidia::Desconhecida: {
            LeituraTecnicaResultado r;
            auto obj = std::make_unique<juce::DynamicObject>();
            obj->setProperty("fileSizeBytes", static_cast<juce::int64>(arquivo.getSize()));
            r.bruto = juce::var(obj.release());
            return r;
        }
    }
    throw LeituraTecnicaError("unexpected media category");
}

std::string paraJson(const LeituraTecnicaResultado& r) {
    return juce::JSON::toString(r.bruto, true).toStdString();
}

juce::String obterLogoSessaoPorExtensao(const juce::String& extensaoSemPonto) {
    juce::String ext = extensaoSemPonto.toLowerCase().trim();
    if (ext == "als" || ext == "alp" || ext == "alc" || ext == "adg" || ext == "adv" || ext == "agr" || ext == "amxd" || ext == "ams" || ext == "abl" || ext == "ablbundle" || ext == "asd" || ext == "ask")
        return "ableton live.png";
    if (ext == "prproj" || ext == "pproj" || ext == "prel" || ext == "ppj" || ext == "plb" || ext == "psq" || ext == "prtl")
        return "adobepremiere.png";
    if (ext == "aep" || ext == "aepx" || ext == "aet" || ext == "mgjson")
        return "adobeaftereffects.png";
    if (ext == "ai" || ext == "ait" || ext == "eps" || ext == "svg")
        return "adobeillustrator.png";
    if (ext == "indd" || ext == "indt" || ext == "indl" || ext == "indb" || ext == "idml" || ext == "idms" || ext == "inx")
        return "adobeindesign.png";
    if (ext == "ptx" || ext == "ptf" || ext == "pts" || ext == "wfm" || ext == "aan")
        return "pro tools.png";
    if (ext == "rpp" || ext == "rpp-bak" || ext == "rpp-undo")
        return "reaper.png";
    if (ext == "rxdoc")
        return "izotope.png";
    if (ext == "fcpbundle" || ext == "fcpxml" || ext == "fcpxmld" || ext == "fcpevent" || ext == "fcproject" || ext == "fcp" || ext == "fcarch" || ext == "cboard")
        return "finalcut.png";
    if (ext == "json")
        return "capcut.png";
    if (ext == "bkrgs")
        return "groovesculptor.png";
    if (ext == "logic" || ext == "logicx")
        return "logic.png";
    return "";
}

juce::String obterLogoParaExtensao(const juce::String& extensaoSemPonto) {
    juce::String ext = extensaoSemPonto.toLowerCase().trim();
    if (ext == "pdf")
        return "pdf.jpeg";
    return obterLogoSessaoPorExtensao(ext);
}

} // namespace matriz::ingest
