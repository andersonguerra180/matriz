#include "SaidaBounce.h"

#include <exiv2/exiv2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "../Ingest/ProcessoExterno.h"
#include "LimpezaImagem.h"
#include "PacoteCollection.h"

namespace matriz::consolidacao {

namespace {

using Bytes = std::vector<std::uint8_t>;

juce::String extensaoMinuscula(const juce::File& f) { return f.getFileExtension().trimCharactersAtStart(".").toLowerCase(); }

bool estaEm(const juce::String& ext, std::initializer_list<const char*> lista) {
    for (auto* e : lista)
        if (ext == e) return true;
    return false;
}

bool ehImagemJpeg(const juce::String& e) { return estaEm(e, {"jpg", "jpeg", "jpe", "jfif"}); }
bool ehImagemPng(const juce::String& e) { return e == "png"; }
bool ehImagemExiv2(const juce::String& e) { return estaEm(e, {"webp", "tif", "tiff"}); }
bool ehAudioVideoLimpavel(const juce::String& e) {
    return estaEm(e, {"mp4", "m4v", "mov", "mkv", "webm", "avi", "mxf", "mts", "m2ts", "ts", "wmv", "flv", "mpg", "mpeg", "3gp",
                      "wav", "mp3", "flac", "aif", "aiff", "m4a", "aac", "ogg", "oga", "opus", "wma"});
}

Bytes lerBytes(const juce::File& f) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) return {};
    const auto* p = static_cast<const std::uint8_t*>(mb.getData());
    return Bytes(p, p + mb.getSize());
}

bool gravarBytes(const juce::File& destino, const Bytes& dados) {
    return destino.replaceWithData(dados.data(), dados.size());  // via arquivo temporário: nunca deixa pela metade
}

bool contem(const Bytes& b, const char* agulha) {
    const std::size_t n = std::strlen(agulha);
    if (b.size() < n) return false;
    return std::search(b.begin(), b.end(), agulha, agulha + n) != b.end();
}

// Depois de limpar uma imagem, confere (nunca se supõe): sem XMP, sem APP13/IPTC, sem EXIF GPS.
bool imagemSemMetadados(const Bytes& b) {
    if (contem(b, "http://ns.adobe.com/xap/1.0/") || contem(b, "x:xmpmeta") || contem(b, "XML:com.adobe.xmp") ||
        contem(b, "Photoshop 3.0"))
        return false;
    try {
        auto image = Exiv2::ImageFactory::open(b.data(), b.size());
        if (!image) return true;
        image->readMetadata();
        if (!image->iptcData().empty()) return false;
        for (const auto& d : image->exifData())
            if (d.groupName() == "GPSInfo") return false;
    } catch (...) {
        // Não decodificável pelo Exiv2 não prova nada contra a limpeza por bytes (JPEG/PNG já reescritos).
    }
    return true;
}

// WebP (EXIF/XMP/ICC em chunks próprios) e TIFF (metadado dentro do IFD, junto da estrutura da imagem) via Exiv2.
bool limparViaExiv2(const juce::File& copia, bool tiff, std::string& erro) {
    const juce::File tmp = copia.getSiblingFile(copia.getFileName() + ".bounce.tmp");
    if (!copia.copyFileTo(tmp)) { erro = "could not create the temporary copy"; return false; }
    try {
        {
            auto image = Exiv2::ImageFactory::open(tmp.getFullPathName().toStdString());
            image->readMetadata();
            auto& exif = image->exifData();
            auto inteiro = [&](const char* chave) -> long {
                auto it = exif.findKey(Exiv2::ExifKey(chave));
                return it == exif.end() ? -1 : static_cast<long>(it->toInt64());
            };
            const long orient = inteiro("Exif.Image.Orientation");
            const long espaco = inteiro("Exif.Photo.ColorSpace");

            if (!tiff) {
                exif.clear();  // WebP: o EXIF é um chunk à parte, sem estrutura de imagem dentro
            } else {
                // TIFF: tags de estrutura (offsets, compressão, ICC) vivem no mesmo IFD — só sai o que identifica.
                static const std::set<std::string> idImage = {
                    "Make", "Model", "Software", "DateTime", "Artist", "Copyright", "ImageDescription", "HostComputer",
                    "XPTitle", "XPComment", "XPAuthor", "XPKeywords", "XPSubject", "CameraSerialNumber", "UniqueCameraModel",
                    "LocalizedCameraModel", "XMLPacket", "IPTCNAA", "ImageResources", "ImageID", "DocumentName", "PageName"};
                std::vector<std::string> apagar;
                for (const auto& d : exif) {
                    const std::string grupo = d.groupName();
                    if (grupo == "Image") {
                        if (idImage.count(d.tagName())) apagar.push_back(d.key());
                    } else if (!(grupo == "Photo" && d.tagName() == "ColorSpace") && grupo.rfind("Thumbnail", 0) != 0) {
                        apagar.push_back(d.key());  // Photo, GPSInfo, Iop, MakerNote (Canon, Nikon...) e afins
                    }
                }
                for (const auto& k : apagar) {
                    auto it = exif.findKey(Exiv2::ExifKey(k));
                    if (it != exif.end()) exif.erase(it);
                }
            }
            if (!tiff) {
                if (orient >= 1 && orient <= 8) exif["Exif.Image.Orientation"] = static_cast<uint16_t>(orient);
                if (espaco >= 0) exif["Exif.Photo.ColorSpace"] = static_cast<uint16_t>(espaco);
            }
            image->clearIptcData();
            image->clearXmpData();
            image->clearXmpPacket();
            image->clearComment();
            image->writeMetadata();
        }
        // Confere o resultado relendo o arquivo gravado.
        const Bytes depois = lerBytes(tmp);
        if (depois.empty() || !imagemSemMetadados(depois)) {
            tmp.deleteFile();
            erro = "metadata still present after cleaning";
            return false;
        }
    } catch (const std::exception& e) {
        tmp.deleteFile();
        erro = e.what();
        return false;
    }
    if (!copia.deleteFile() || !tmp.moveFileTo(copia)) {
        tmp.deleteFile();
        erro = "could not replace the cleaned file";
        return false;
    }
    return true;
}

// Remuxa sem recodificar (-c copy) e sem nenhum metadado de container/stream/capítulo.
bool limparViaFfmpeg(const juce::File& copia, const juce::String& ext, std::string& erro) {
    const juce::File tmp = copia.getSiblingFile(copia.getFileNameWithoutExtension() + ".bounce.tmp." + ext);
    tmp.deleteFile();
    juce::StringArray args{"-y", "-nostdin", "-loglevel", "error", "-i", copia.getFullPathName(),
                           "-map", "0:v?", "-map", "0:a?",
                           "-map_metadata", "-1", "-map_metadata:s:v", "-1", "-map_metadata:s:a", "-1", "-map_chapters", "-1",
                           "-c", "copy", "-fflags", "+bitexact", "-flags:v", "+bitexact", "-flags:a", "+bitexact"};
    if (ext == "mp3") { args.add("-write_id3v2"); args.add("0"); args.add("-write_id3v1"); args.add("0"); }
    args.add(tmp.getFullPathName());
    try {
        matriz::ingest::rodarEsperandoSucesso("ffmpeg", args, 300000);
    } catch (const std::exception& e) {
        tmp.deleteFile();
        erro = std::string("ffmpeg: ") + e.what();
        return false;
    }
    if (!tmp.existsAsFile() || tmp.getSize() <= 0) { tmp.deleteFile(); erro = "ffmpeg produced no output"; return false; }
    if (!copia.deleteFile() || !tmp.moveFileTo(copia)) { tmp.deleteFile(); erro = "could not replace the cleaned file"; return false; }
    return true;
}

// ---- ficha do Matriz (Embed e sidecar) -------------------------------------------------------------------------------

std::string geoTexto(const juce::var& geo, const char* chave) {
    if (!geo.isObject()) return {};
    return geo.getProperty(chave, {}).toString().trim().toStdString();
}

bool geoNumero(const juce::var& geo, const char* chave, double& out) {
    if (!geo.isObject()) return false;
    const auto v = geo.getProperty(chave, {});
    if (v.isVoid()) return false;
    out = static_cast<double>(v);
    return std::isfinite(out);
}

bool dataCompleta(const std::string& texto, int& y, int& m, int& d) {
    const juce::String t = juce::String::fromUTF8(texto.c_str()).trim();
    juce::StringArray p;
    p.addTokens(t.replaceCharacter('/', '-').replaceCharacter('.', '-'), "-", "");
    p.removeEmptyStrings();
    if (p.size() != 3) return false;
    for (auto& s : p) if (!s.containsOnly("0123456789")) return false;
    int a = p[0].getIntValue(), b = p[1].getIntValue(), c = p[2].getIntValue();
    if (p[0].length() == 4) { y = a; m = b; d = c; }
    else if (p[2].length() == 4) { y = c; m = b; d = a; }
    else return false;
    return y >= 1000 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

std::string racional(double v) {  // graus decimais -> "g/1 m/1 s*10000/10000"
    const double a = std::fabs(v);
    const int g = static_cast<int>(a);
    const double mm = (a - g) * 60.0;
    const int m = static_cast<int>(mm);
    const long s = std::lround((mm - m) * 60.0 * 10000.0);
    return std::to_string(g) + "/1 " + std::to_string(m) + "/1 " + std::to_string(s) + "/10000";
}

std::string truncarUtf8(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;  // não corta no meio de um caractere
    return s.substr(0, n);
}

void apagarChaveIptc(Exiv2::IptcData& iptc, const char* chave) {
    Exiv2::IptcKey k(chave);
    for (auto it = iptc.findKey(k); it != iptc.end(); it = iptc.findKey(k)) iptc.erase(it);
}

void addIptc(Exiv2::IptcData& iptc, const char* chave, const std::string& valor, std::size_t max) {
    if (valor.empty()) return;
    Exiv2::Iptcdatum d{Exiv2::IptcKey(chave)};
    d.setValue(truncarUtf8(valor, max));
    iptc.add(d);
}

bool embutirImagem(const juce::File& copia, const pacote::DadosFicha& d, std::string& erro) {
    const juce::File tmp = copia.getSiblingFile(copia.getFileName() + ".bounce.tmp");
    if (!copia.copyFileTo(tmp)) { erro = "could not create the temporary copy"; return false; }
    try {
        {
            auto image = Exiv2::ImageFactory::open(tmp.getFullPathName().toStdString());
            image->readMetadata();
            auto& exif = image->exifData();
            auto& iptc = image->iptcData();

            {   // UTF-8 nos campos IPTC
                Exiv2::Iptcdatum cs{Exiv2::IptcKey("Iptc.Envelope.CharacterSet")};
                cs.setValue(std::string("\x1b%G"));
                apagarChaveIptc(iptc, "Iptc.Envelope.CharacterSet");
                iptc.add(cs);
            }
            for (const char* k : {"Iptc.Application2.ObjectName", "Iptc.Application2.Caption", "Iptc.Application2.Keywords",
                                  "Iptc.Application2.SuppCategory", "Iptc.Application2.ObjectAttribute",
                                  "Iptc.Application2.City", "Iptc.Application2.ProvinceState", "Iptc.Application2.CountryName",
                                  "Iptc.Application2.SubLocation", "Iptc.Application2.DateCreated"})
                apagarChaveIptc(iptc, k);

            addIptc(iptc, "Iptc.Application2.ObjectName", d.titulo, 64);
            addIptc(iptc, "Iptc.Application2.Caption", d.descricao, 2000);
            if (!d.descricao.empty()) exif["Exif.Image.ImageDescription"] = d.descricao;
            for (const auto& s : d.subjects) addIptc(iptc, "Iptc.Application2.SuppCategory", s, 32);
            addIptc(iptc, "Iptc.Application2.ObjectAttribute", d.content, 68);
            for (const auto& t : d.tags) addIptc(iptc, "Iptc.Application2.Keywords", t, 64);
            for (const auto& p : d.pessoas) addIptc(iptc, "Iptc.Application2.Keywords", p, 64);

            int y = 0, m = 0, dia = 0;
            if (dataCompleta(d.eventDate, y, m, dia)) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%04d:%02d:%02d 00:00:00", y, m, dia);
                exif["Exif.Photo.DateTimeOriginal"] = std::string(buf);
                std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, dia);
                Exiv2::Iptcdatum dc{Exiv2::IptcKey("Iptc.Application2.DateCreated")};
                dc.setValue(std::string(buf));
                iptc.add(dc);
            }

            double lat = 0, lon = 0, alt = 0;
            if (geoNumero(d.geo, "latitude", lat) && geoNumero(d.geo, "longitude", lon)) {
                exif["Exif.GPSInfo.GPSVersionID"] = "2 3 0 0";
                exif["Exif.GPSInfo.GPSLatitudeRef"] = lat >= 0 ? "N" : "S";
                exif["Exif.GPSInfo.GPSLatitude"] = racional(lat);
                exif["Exif.GPSInfo.GPSLongitudeRef"] = lon >= 0 ? "E" : "W";
                exif["Exif.GPSInfo.GPSLongitude"] = racional(lon);
                if (geoNumero(d.geo, "altitude", alt)) {
                    exif["Exif.GPSInfo.GPSAltitudeRef"] = static_cast<uint8_t>(alt < 0 ? 1 : 0);
                    exif["Exif.GPSInfo.GPSAltitude"] = std::to_string(std::lround(std::fabs(alt) * 100)) + "/100";
                }
            }
            addIptc(iptc, "Iptc.Application2.City", geoTexto(d.geo, "city"), 32);
            addIptc(iptc, "Iptc.Application2.ProvinceState", geoTexto(d.geo, "state_province"), 32);
            addIptc(iptc, "Iptc.Application2.CountryName", geoTexto(d.geo, "country"), 64);
            {
                std::string sub = geoTexto(d.geo, "neighborhood");
                if (sub.empty()) sub = geoTexto(d.geo, "street");
                addIptc(iptc, "Iptc.Application2.SubLocation", sub, 32);
            }
            image->writeMetadata();
        }
    } catch (const std::exception& e) {
        tmp.deleteFile();
        erro = e.what();
        return false;
    }
    if (!copia.deleteFile() || !tmp.moveFileTo(copia)) { tmp.deleteFile(); erro = "could not replace the file"; return false; }
    return true;
}

bool embutirAudioVideo(const juce::File& copia, const juce::String& ext, const pacote::DadosFicha& d, std::string& erro) {
    const juce::File tmp = copia.getSiblingFile(copia.getFileNameWithoutExtension() + ".bounce.tmp." + ext);
    juce::StringArray tags;
    auto meta = [&](const char* chave, const std::string& v) {
        if (v.empty()) return;
        tags.add("-metadata");
        tags.add(juce::String(chave) + "=" + juce::String::fromUTF8(v.c_str()));
    };
    meta("title", d.titulo);
    meta("comment", d.descricao);
    meta("description", d.descricao);
    meta("genre", d.content);
    meta("date", d.eventDate);
    {
        std::string palavras;
        for (const auto* lista : {&d.subjects, &d.tags, &d.pessoas})
            for (const auto& s : *lista) palavras += (palavras.empty() ? "" : "; ") + s;
        meta("keywords", palavras);
    }
    double lat = 0, lon = 0;
    if (estaEm(ext, {"mp4", "m4v", "mov"}) && geoNumero(d.geo, "latitude", lat) && geoNumero(d.geo, "longitude", lon))
        meta("location", juce::String::formatted("%+08.4f%+09.4f/", lat, lon).toStdString());
    if (tags.isEmpty()) return true;

    for (int tentativa = 0; tentativa < 2; ++tentativa) {
        tmp.deleteFile();
        juce::StringArray args{"-y", "-nostdin", "-loglevel", "error", "-i", copia.getFullPathName()};
        if (tentativa == 0) { args.add("-map"); args.add("0"); }
        else { args.add("-map"); args.add("0:v?"); args.add("-map"); args.add("0:a?"); }
        args.add("-map_metadata"); args.add("0");
        args.add("-c"); args.add("copy");
        args.addArray(tags);
        args.add(tmp.getFullPathName());
        try {
            matriz::ingest::rodarEsperandoSucesso("ffmpeg", args, 300000);
            if (tmp.existsAsFile() && tmp.getSize() > 0) {
                if (!copia.deleteFile() || !tmp.moveFileTo(copia)) { tmp.deleteFile(); erro = "could not replace the file"; return false; }
                return true;
            }
        } catch (const std::exception& e) {
            erro = std::string("ffmpeg: ") + e.what();
        }
    }
    tmp.deleteFile();
    return false;
}

juce::String xmlEsc(const std::string& v) {
    return juce::String::fromUTF8(v.c_str()).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\"", "&quot;");
}

juce::String gpsXmp(double v, bool latitude) {  // "DD,MM.mmmmN"
    const double a = std::fabs(v);
    const int g = static_cast<int>(a);
    const double m = (a - g) * 60.0;
    const char ref = latitude ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
    return juce::String(g) + "," + juce::String(m, 4) + juce::String::charToString(ref);
}

}  // namespace

bool ehArquivoRaw(const juce::File& arquivo) {
    return estaEm(extensaoMinuscula(arquivo), {"cr2", "cr3", "crw", "nef", "nrw", "arw", "srf", "sr2", "dng", "orf", "raf", "rw2",
                                               "rwl", "pef", "srw", "x3f", "3fr", "fff", "erf", "kdc", "dcr", "mrw", "mef", "mos",
                                               "iiq", "raw"});
}

bool ehArquivoVideo(const juce::File& arquivo) {
    return estaEm(extensaoMinuscula(arquivo), {"mp4", "m4v", "mov", "mkv", "webm"});
}

StatusLimpeza limparMetadadosDaCopia(const juce::File& copia, std::string& erro) {
    const juce::String ext = extensaoMinuscula(copia);
    if (!copia.existsAsFile()) { erro = "file not found"; return StatusLimpeza::Falhou; }
    if (ehArquivoRaw(copia)) return StatusLimpeza::RawSemLimpeza;

    if (ehImagemJpeg(ext) || ehImagemPng(ext)) {
        const Bytes entrada = lerBytes(copia);
        if (entrada.empty()) { erro = "could not read the file"; return StatusLimpeza::Falhou; }
        const int orient = limpeza::lerOrientacao(entrada);
        Bytes saida;
        const bool ok = ehImagemJpeg(ext) ? limpeza::limparJpeg(entrada, orient, saida) : limpeza::limparPng(entrada, orient, saida);
        if (!ok) { erro = "not a valid " + std::string(ehImagemJpeg(ext) ? "JPEG" : "PNG") + " (could not clean)"; return StatusLimpeza::Falhou; }
        if (!imagemSemMetadados(saida)) { erro = "metadata still present after cleaning"; return StatusLimpeza::Falhou; }
        if (!gravarBytes(copia, saida)) { erro = "could not write the cleaned file"; return StatusLimpeza::Falhou; }
        return StatusLimpeza::Limpo;
    }
    if (ehImagemExiv2(ext))
        return limparViaExiv2(copia, ext != "webp", erro) ? StatusLimpeza::Limpo : StatusLimpeza::Falhou;
    if (ehAudioVideoLimpavel(ext))
        return limparViaFfmpeg(copia, ext, erro) ? StatusLimpeza::Limpo : StatusLimpeza::Falhou;
    return StatusLimpeza::TipoSemSuporte;  // PDF, HEIC, GIF, documentos, sessões...
}

StatusEmbedFicha embutirFichaMatrizNaCopia(matriz::db::Database& registro, const std::string& itemId,
                                           const juce::File& copia, std::string& erro) {
    const auto dados = pacote::lerDadosFicha(registro, itemId, pacote::chavesDaListaPessoas(registro));
    if (dados.vazio()) return StatusEmbedFicha::SemDados;
    const juce::String ext = extensaoMinuscula(copia);
    if (ehArquivoRaw(copia)) return StatusEmbedFicha::NaoSuportado;  // RAW não é reescrito
    if (ehImagemJpeg(ext) || ehImagemPng(ext) || ehImagemExiv2(ext))
        return embutirImagem(copia, dados, erro) ? StatusEmbedFicha::Embutido : StatusEmbedFicha::Falhou;
    if (estaEm(ext, {"mp3", "flac", "m4a", "mp4", "m4v", "mov", "mkv", "webm", "ogg", "oga", "opus"}))
        return embutirAudioVideo(copia, ext, dados, erro) ? StatusEmbedFicha::Embutido : StatusEmbedFicha::Falhou;
    return StatusEmbedFicha::NaoSuportado;
}

bool escreverSidecarFichaMatriz(matriz::db::Database& registro, const std::string& itemId, const juce::File& copia) {
    const auto d = pacote::lerDadosFicha(registro, itemId, pacote::chavesDaListaPessoas(registro));
    if (d.titulo.empty() && d.descricao.empty() && d.eventDate.empty() && d.content.empty() && d.subjects.empty() &&
        d.tags.empty() && d.pessoas.empty() && !d.geo.isObject())
        return false;

    auto alt = [&](const char* prop, const std::string& v) {
        return v.empty() ? juce::String()
                         : juce::String("   <") + prop + "><rdf:Alt><rdf:li xml:lang=\"x-default\">" + xmlEsc(v) + "</rdf:li></rdf:Alt></" + prop + ">\n";
    };
    auto lista = [&](const char* prop, const char* tipo, const std::vector<std::string>& vs) {
        if (vs.empty()) return juce::String();
        juce::String out = juce::String("   <") + prop + "><rdf:" + tipo + ">";
        for (const auto& v : vs) out << "<rdf:li>" << xmlEsc(v) << "</rdf:li>";
        return out + "</rdf:" + tipo + "></" + prop + ">\n";
    };
    std::vector<std::string> palavras = d.subjects;
    palavras.insert(palavras.end(), d.tags.begin(), d.tags.end());

    juce::String atributos;  // propriedades simples como atributos de rdf:Description
    auto attr = [&](const char* nome, const std::string& v) { if (!v.empty()) atributos << "\n    " << nome << "=\"" << xmlEsc(v) << "\""; };
    attr("photoshop:City", geoTexto(d.geo, "city"));
    attr("photoshop:State", geoTexto(d.geo, "state_province"));
    attr("photoshop:Country", geoTexto(d.geo, "country"));
    attr("bkr:Content", d.content);
    double lat = 0, lon = 0;
    if (geoNumero(d.geo, "latitude", lat) && geoNumero(d.geo, "longitude", lon)) {
        atributos << "\n    exif:GPSLatitude=\"" << gpsXmp(lat, true) << "\"\n    exif:GPSLongitude=\"" << gpsXmp(lon, false) << "\"";
    }

    juce::String x;
    x << "<?xpacket begin=\"" << juce::String::fromUTF8("\xef\xbb\xbf") << "\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
      << "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"BKR Matriz\">\n"
      << " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
      << "  <rdf:Description rdf:about=\"\"\n"
      << "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
      << "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
      << "    xmlns:photoshop=\"http://ns.adobe.com/photoshop/1.0/\"\n"
      << "    xmlns:exif=\"http://ns.adobe.com/exif/1.0/\"\n"
      << "    xmlns:Iptc4xmpExt=\"http://iptc.org/std/Iptc4xmpExt/2008-02-29/\"\n"
      << "    xmlns:bkr=\"http://bkr.com.br/matriz/1.0/\"\n"
      << "    xmp:CreatorTool=\"BKR Matriz\"" << atributos << ">\n"
      << alt("dc:title", d.titulo) << alt("dc:description", d.descricao)
      << lista("dc:subject", "Bag", palavras)
      << lista("dc:date", "Seq", d.eventDate.empty() ? std::vector<std::string>{} : std::vector<std::string>{d.eventDate})
      << lista("Iptc4xmpExt:PersonInImage", "Bag", d.pessoas)
      << "  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>\n";
    return juce::File(copia.getFullPathName() + ".xmp").replaceWithText(x, false, false, "\n");
}

} // namespace matriz::consolidacao

namespace matriz::consolidacao {

namespace {
juce::File arquivoConfiguracaoBounce(const juce::File& pastaProjeto) { return pastaProjeto.getChildFile("bounce_list.json"); }
}  // namespace

ConfiguracaoBounce carregarConfiguracaoBounce(const juce::File& pastaProjeto) {
    ConfiguracaoBounce cfg;
    const auto v = juce::JSON::parse(arquivoConfiguracaoBounce(pastaProjeto));
    if (!v.isObject()) return cfg;
    cfg.limpar = static_cast<bool>(v.getProperty("limpar", false));
    cfg.embutir = static_cast<bool>(v.getProperty("embutir", false)) && !cfg.limpar;
    cfg.sidecar = static_cast<bool>(v.getProperty("sidecar", true));
    cfg.manifesto = static_cast<bool>(v.getProperty("manifesto", true));
    cfg.embedVideo = static_cast<bool>(v.getProperty("embed_video", true));
    return cfg;
}

bool salvarConfiguracaoBounce(const juce::File& pastaProjeto, const ConfiguracaoBounce& cfg) {
    auto* o = new juce::DynamicObject();
    o->setProperty("limpar", cfg.limpar);
    o->setProperty("embutir", cfg.embutir);
    o->setProperty("sidecar", cfg.sidecar);
    o->setProperty("manifesto", cfg.manifesto);
    o->setProperty("embed_video", cfg.embedVideo);
    return arquivoConfiguracaoBounce(pastaProjeto).replaceWithText(juce::JSON::toString(juce::var(o)));
}

}  // namespace matriz::consolidacao
