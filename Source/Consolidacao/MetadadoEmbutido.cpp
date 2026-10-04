#include "MetadadoEmbutido.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <map>
#include <exiv2/exiv2.hpp>
#include "../Model/Project.h"
#include "../Model/NomesCanonicos.h"
#include "../Vault/Resolucao.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::consolidacao {

using matriz::db::Value;

namespace {

void salvarMetadadoOriginalComoObservacao(matriz::db::Database& registro, const std::string& itemId,
                                          const juce::File& arquivo) {
    juce::String texto = "ORIGINAL METADATA (preserved before embed):\n";
    bool temAlgo = false;

    if (arquivo.hasFileExtension("jpg;jpeg;png;tif;tiff;dng;cr2;nef;arw;webp;heic;heif")) {
        try {
            auto image = Exiv2::ImageFactory::open(arquivo.getFullPathName().toStdString());
            image->readMetadata();
            auto& exif = image->exifData();
            auto& xmp = image->xmpData();

            auto addIfExists = [&](const char* tag, const char* label) {
                auto it = exif.findKey(Exiv2::ExifKey(tag));
                if (it != exif.end() && !it->value().toString().empty()) {
                    texto += juce::String(label) + ": " + juce::String(it->value().toString()) + "\n";
                    temAlgo = true;
                }
            };
            addIfExists("Exif.Image.ImageDescription", "Description");
            addIfExists("Exif.Image.Artist", "Artist");
            addIfExists("Exif.Image.DateTime", "Date");
            addIfExists("Exif.Photo.DateTimeOriginal", "DateTimeOriginal");

            for (auto& x : xmp) {
                juce::String key(x.key());
                if (key.startsWith("Xmp.dc.")) {
                    texto += key + ": " + juce::String(x.value().toString()) + "\n";
                    temAlgo = true;
                }
            }
        } catch (...) {}
    }

    if (!temAlgo) return;

    std::string agora = matriz::model::agoraIso8601();
    registro.run(
        "INSERT INTO item_observacao (id, item_id, texto, autor, criado_em) VALUES (?, ?, ?, ?, ?)",
        {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(texto.toStdString()),
         Value::of(std::string("system/embed")), Value::of(agora)});
}

} // namespace

namespace {

void escreverFourCC(juce::MemoryOutputStream& out, const char* cc) { out.write(cc, 4); }

// Um chunk RIFF: id + tamanho little-endian + dados + byte de padding se o
// tamanho for ímpar (a norma RIFF exige alinhamento par).
void escreverChunk(juce::MemoryOutputStream& out, const char* id, const juce::MemoryBlock& dados) {
    escreverFourCC(out, id);
    out.writeInt(static_cast<int>(dados.getSize()));
    out.write(dados.getData(), dados.getSize());
    if (dados.getSize() % 2 == 1) out.writeByte(0);
}

juce::String escaparXml(const juce::String& s) {
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\"", "&quot;");
}

// iXML com a lista de marcadores. É o formato que ferramenta de arquivo
// (BWF MetaEdit, ffprobe) sabe ler, e o que o item 8.3 pede por nome.
juce::MemoryBlock construirIxml(const std::vector<MarcadorParaEmbutir>& marcadores, double sampleRate) {
    juce::String xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<BWFXML>\n";
    xml << "  <IXML_VERSION>1.5</IXML_VERSION>\n";
    xml << "  <PROJECT>MATRIZ</PROJECT>\n";
    xml << "  <CUE_LIST>\n";
    int id = 1;
    for (auto& m : marcadores) {
        juce::int64 amostra = static_cast<juce::int64>(std::llround(m.segundos * sampleRate));
        xml << "    <CUE>\n";
        xml << "      <ID>" << id++ << "</ID>\n";
        xml << "      <SAMPLE_OFFSET>" << juce::String(amostra) << "</SAMPLE_OFFSET>\n";
        xml << "      <TIME>" << juce::String(m.segundos, 6) << "</TIME>\n";
        xml << "      <NAME>" << escaparXml(juce::String(m.texto)) << "</NAME>\n";
        xml << "    </CUE>\n";
    }
    xml << "  </CUE_LIST>\n</BWFXML>\n";

    juce::MemoryBlock bloco;
    auto utf8 = xml.toRawUTF8();
    bloco.append(utf8, std::strlen(utf8));
    return bloco;
}

} // namespace

std::vector<MarcadorParaEmbutir> marcadoresDoItem(matriz::db::Database& registro, const std::string& itemId) {
    std::vector<MarcadorParaEmbutir> out;
    auto stmt = registro.prepare(
        "SELECT minutagem_ms, texto FROM item_observacao WHERE item_id = ? AND minutagem_ms IS NOT NULL "
        "ORDER BY minutagem_ms");
    stmt.bind(1, Value::of(itemId));
    while (stmt.step())
        out.push_back({static_cast<double>(stmt.columnInt(0)) / 1000.0, stmt.columnText(1)});
    return out;
}

bool embutirMarcadoresEmWav(const juce::File& wavDestino, const std::vector<MarcadorParaEmbutir>& marcadores) {
    if (marcadores.empty() || !wavDestino.existsAsFile()) return false;

    juce::MemoryBlock original;
    if (!wavDestino.loadFileAsData(original)) return false;
    if (original.getSize() < 12) return false;

    const char* dados = static_cast<const char*>(original.getData());
    if (std::strncmp(dados, "RIFF", 4) != 0 || std::strncmp(dados + 8, "WAVE", 4) != 0) return false;

    // Descobre o sample rate a partir do chunk "fmt " — a posição de cada
    // marcador em AMOSTRAS depende dele (item 8.3 pede posição em tempo E em
    // amostras).
    double sampleRate = 44100.0;
    {
        size_t pos = 12;
        while (pos + 8 <= original.getSize()) {
            juce::uint32 tamanho = juce::ByteOrder::littleEndianInt(dados + pos + 4);
            if (std::strncmp(dados + pos, "fmt ", 4) == 0 && pos + 16 <= original.getSize()) {
                sampleRate = static_cast<double>(juce::ByteOrder::littleEndianInt(dados + pos + 12));
                break;
            }
            pos += 8 + tamanho + (tamanho % 2);
        }
        if (sampleRate <= 0.0) sampleRate = 44100.0;
    }

    // chunk "cue " — contagem + um registro de 24 bytes por ponto.
    juce::MemoryOutputStream cue;
    cue.writeInt(static_cast<int>(marcadores.size()));
    int id = 1;
    for (auto& m : marcadores) {
        juce::uint32 amostra = static_cast<juce::uint32>(std::llround(m.segundos * sampleRate));
        cue.writeInt(id++);                 // dwIdentifier
        cue.writeInt(static_cast<int>(amostra)); // dwPosition
        escreverFourCC(cue, "data");        // fccChunk
        cue.writeInt(0);                     // dwChunkStart
        cue.writeInt(0);                     // dwBlockStart
        cue.writeInt(static_cast<int>(amostra)); // dwSampleOffset
    }

    // LIST/adtl com um "labl" por ponto — é onde vive o TEXTO do marcador.
    juce::MemoryOutputStream adtl;
    escreverFourCC(adtl, "adtl");
    id = 1;
    for (auto& m : marcadores) {
        juce::String texto(m.texto);
        auto utf8 = texto.toRawUTF8();
        size_t tamanhoTexto = std::strlen(utf8) + 1; // inclui o terminador
        escreverFourCC(adtl, "labl");
        adtl.writeInt(static_cast<int>(4 + tamanhoTexto));
        adtl.writeInt(id++);
        adtl.write(utf8, tamanhoTexto);
        if ((4 + tamanhoTexto) % 2 == 1) adtl.writeByte(0);
    }

    juce::MemoryBlock ixml = construirIxml(marcadores, sampleRate);

    juce::MemoryOutputStream saida;
    escreverFourCC(saida, "RIFF");
    saida.writeInt(0); // tamanho, corrigido no fim
    escreverFourCC(saida, "WAVE");

    // Copia chunks originais pulando chunks de marcador BKR anteriores (idempotência)
    size_t pos = 12;
    while (pos + 8 <= original.getSize()) {
        const char* chunkId = dados + pos;
        juce::uint32 chunkLen = juce::ByteOrder::littleEndianInt(dados + pos + 4);
        size_t totalChunkLen = 8 + chunkLen + (chunkLen % 2);
        if (pos + totalChunkLen > original.getSize()) break;

        bool pular = false;
        if (std::strncmp(chunkId, "cue ", 4) == 0) {
            pular = true;
        } else if (std::strncmp(chunkId, "iXML", 4) == 0) {
            pular = true;
        } else if (std::strncmp(chunkId, "LIST", 4) == 0 && chunkLen >= 4) {
            if (std::strncmp(dados + pos + 8, "adtl", 4) == 0) {
                pular = true;
            }
        }

        if (!pular) {
            saida.write(dados + pos, totalChunkLen);
        }
        pos += totalChunkLen;
    }

    juce::MemoryBlock cueBloco(cue.getData(), cue.getDataSize());
    juce::MemoryBlock adtlBloco(adtl.getData(), adtl.getDataSize());
    escreverChunk(saida, "cue ", cueBloco);
    escreverChunk(saida, "LIST", adtlBloco);
    escreverChunk(saida, "iXML", ixml);

    juce::MemoryBlock resultado(saida.getData(), saida.getDataSize());
    juce::uint32 tamanhoRiff = static_cast<juce::uint32>(resultado.getSize() - 8);
    std::memcpy(static_cast<char*>(resultado.getData()) + 4, &tamanhoRiff, 4);

    juce::File temporario = wavDestino.getSiblingFile(wavDestino.getFileName() + ".marcadores.tmp");
    if (!temporario.replaceWithData(resultado.getData(), resultado.getSize())) return false;
    wavDestino.deleteFile();
    if (!temporario.moveFileTo(wavDestino)) {
        temporario.deleteFile();
        return false;
    }
    return true;
}

int embutirMarcadoresNoBackup(matriz::db::Database& registro, const juce::File& raizDestino) {
    int enriquecidos = 0;
    auto stmt = registro.prepare(
        "SELECT DISTINCT cr.item_id, cr.caminho_relativo_destino FROM consolidacao_registro cr "
        "WHERE EXISTS (SELECT 1 FROM item_observacao io WHERE io.item_id = cr.item_id AND io.minutagem_ms IS NOT NULL)");
    while (stmt.step()) {
        std::string itemId = stmt.columnText(0);
        juce::File destino = raizDestino.getChildFile(stmt.columnText(1));
        if (!destino.existsAsFile()) continue;
        if (!destino.hasFileExtension("wav")) continue;
        if (embutirMarcadoresEmWav(destino, marcadoresDoItem(registro, itemId))) ++enriquecidos;
    }
    return enriquecidos;
}

MetadadoParaEmbutir coletarMetadadosDoItem(matriz::db::Database& registro, const std::string& itemId,
                                           matriz::db::Database* indice) {
    MetadadoParaEmbutir meta;
    std::string itemAno;
    std::string itemNotas;
    auto stmt = registro.prepare("SELECT titulo, tipo_midia, codigo_acervo, ano, notas_livres FROM item WHERE id = ?");
    stmt.bind(1, Value::of(itemId));
    if (stmt.step()) {
        meta.titulo = stmt.columnText(0);
        meta.tipoMidia = stmt.columnIsNull(1) ? "" : stmt.columnText(1);
        meta.codigoAcervo = stmt.columnText(2);
        if (!stmt.columnIsNull(3)) itemAno = stmt.columnText(3);
        if (!stmt.columnIsNull(4)) itemNotas = stmt.columnText(4);
    }

    auto lerCampo = [&](const char* campoId) -> std::string {
        auto s = registro.prepare(
            "SELECT valor FROM item_campo WHERE item_id = ? AND nivel = 'raiz' AND nivel_indice = 0 AND campo_id = ?");
        s.bind(1, Value::of(itemId));
        s.bind(2, Value::of(std::string(campoId)));
        return s.step() ? s.columnText(0) : "";
    };

    meta.descricao = !itemNotas.empty() ? itemNotas : lerCampo("descricao");
    if (meta.descricao.empty()) meta.descricao = lerCampo("notas");

    if (indice != nullptr) {
        try {
            auto sAi = indice->prepare(
                "SELECT resumo FROM ai_scan_resultado WHERE item_id = ? ORDER BY analisado_em DESC LIMIT 1");
            sAi.bind(1, Value::of(itemId));
            if (sAi.step()) meta.resumoAi = sAi.columnText(0);
        } catch (...) {}
    }

    meta.artista = lerCampo("artista_principal");
    if (meta.artista.empty()) meta.artista = lerCampo("artista");
    if (meta.artista.empty()) meta.artista = lerCampo("autor");
    std::string anoStr = !itemAno.empty() ? itemAno : lerCampo("ano");
    if (!anoStr.empty()) {
        try { meta.ano = std::stoi(anoStr); } catch (...) {}
    }
    return meta;
}

StatusEmbedding embutirMetadadosNoArquivo(const juce::File& destino, const MetadadoParaEmbutir& meta) {
    if (!destino.existsAsFile()) return StatusEmbedding::Failed;
    if (meta.titulo.empty() && meta.descricao.empty() && meta.artista.empty()
        && meta.codigoAcervo.empty() && meta.resumoAi.empty()) return StatusEmbedding::NoMetadata;

    bool suportaExiv2 = destino.hasFileExtension("jpg;jpeg;png;tif;tiff;dng;cr2;nef;arw;webp;heic;heif");

    if (suportaExiv2) {
        juce::File tempCopy = destino.getSiblingFile(destino.getFileName() + ".embed.tmp");
        if (!destino.copyFileTo(tempCopy)) return StatusEmbedding::Failed;

        try {
            auto image = Exiv2::ImageFactory::open(tempCopy.getFullPathName().toStdString());
            image->readMetadata();
            auto& xmp = image->xmpData();

            if (!meta.titulo.empty())
                xmp["Xmp.dc.title"] = meta.titulo;
            if (!meta.descricao.empty())
                xmp["Xmp.dc.description"] = meta.descricao;
            if (!meta.artista.empty())
                xmp["Xmp.dc.creator"] = meta.artista;
            if (!meta.codigoAcervo.empty())
                xmp["Xmp.dc.identifier"] = meta.codigoAcervo;
            if (meta.ano)
                xmp["Xmp.dc.date"] = std::to_string(*meta.ano);
            if (!meta.resumoAi.empty())
                xmp["Xmp.dc.subject"] = meta.resumoAi;

            auto& exif = image->exifData();
            std::string descricaoExif = meta.descricao;
            if (!meta.resumoAi.empty())
                descricaoExif = descricaoExif.empty() ? meta.resumoAi
                                                      : descricaoExif + "\n\n[AI] " + meta.resumoAi;
            if (!descricaoExif.empty())
                exif["Exif.Image.ImageDescription"] = descricaoExif;
            if (!meta.artista.empty())
                exif["Exif.Image.Artist"] = meta.artista;

            image->writeMetadata();
            destino.deleteFile();
            if (!tempCopy.moveFileTo(destino)) {
                tempCopy.deleteFile();
                return StatusEmbedding::Failed;
            }
            return StatusEmbedding::Embedded;
        } catch (...) {
            tempCopy.deleteFile();
            return StatusEmbedding::Failed;
        }
    }

    // Normal backup MUST NOT generate automatic XMP sidecars for unsupported formats (Rule 5 & 10).
    return StatusEmbedding::Unsupported;
}

int embutirMetadadosNoBackup(matriz::db::Database& registro, const juce::File& raizDestino) {
    int enriquecidos = 0;
    auto stmt = registro.prepare(
        "SELECT DISTINCT cr.item_id, cr.caminho_relativo_destino FROM consolidacao_registro cr");
    while (stmt.step()) {
        std::string itemId = stmt.columnText(0);
        juce::File destino = raizDestino.getChildFile(stmt.columnText(1));
        auto meta = coletarMetadadosDoItem(registro, itemId);
        salvarMetadadoOriginalComoObservacao(registro, itemId, destino);
        if (embutirMetadadosNoArquivo(destino, meta) == StatusEmbedding::Embedded) ++enriquecidos;
    }
    return enriquecidos;
}

ResultadoEmbedding embutirMetadadosEmItens(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                            const std::vector<std::string>& itemIds) {
    ResultadoEmbedding resultado;
    for (const auto& itemId : itemIds) {
        auto meta = coletarMetadadosDoItem(registro, itemId);
        if (meta.titulo.empty() && meta.descricao.empty() && meta.artista.empty()) {
            resultado.erros.push_back(itemId + ": no metadata to embed");
            ++resultado.falha;
            continue;
        }

        auto stmtArq = registro.prepare(
            std::string("SELECT a.id, ") + matriz::vault::colunasDeResolucao() +
            " FROM arquivo a " + matriz::vault::joinDeResolucao() +
            " WHERE a.item_id = ? ORDER BY a.eh_master DESC, a.id LIMIT 1");
        stmtArq.bind(1, Value::of(itemId));
        if (!stmtArq.step()) {
            resultado.erros.push_back(itemId + ": no file found");
            ++resultado.falha;
            continue;
        }

        juce::File arquivo = matriz::vault::caminhoEsperado(
            pastaProjeto, stmtArq.columnText(1), stmtArq.columnText(2), stmtArq.columnText(3));

        if (!arquivo.existsAsFile()) {
            resultado.erros.push_back(meta.codigoAcervo + ": file not found");
            ++resultado.falha;
            continue;
        }

        if (arquivo.hasFileExtension("wav")) {
            auto marcadores = marcadoresDoItem(registro, itemId);
            if (!marcadores.empty()) {
                embutirMarcadoresEmWav(arquivo, marcadores);
                ++resultado.sucesso;
                continue;
            }
        }

        auto status = embutirMetadadosNoArquivo(arquivo, meta);
        if (status == StatusEmbedding::Embedded)
            ++resultado.sucesso;
        else if (status == StatusEmbedding::Unsupported) {
            ++resultado.naoSuportados;
        } else {
            resultado.erros.push_back((meta.codigoAcervo.empty() ? itemId : meta.codigoAcervo) + ": embedding failed");
            ++resultado.falha;
        }
    }
    return resultado;
}

// ------------------------------------------------------------ Sidecars XMP

namespace {

void garantirTabelaSidecar(matriz::db::Database& registro) {
    registro.exec("CREATE TABLE IF NOT EXISTS sidecar_registro ("
                  "  caminho TEXT PRIMARY KEY, arquivo_id TEXT NOT NULL, sha256 TEXT NOT NULL, escrito_em TEXT NOT NULL)");
}

std::string sha256DeTexto(const std::string& t) {
    return juce::SHA256(t.data(), t.size()).toHexString().toStdString();
}

std::string sha256DeArquivo(const juce::File& f) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) return {};
    return juce::SHA256(mb.getData(), mb.getSize()).toHexString().toStdString();
}

bool gravarTextoAtomico(const juce::File& destino, const std::string& texto) {
    juce::File tmp = destino.getSiblingFile(destino.getFileName() + ".tmp");
    if (!tmp.replaceWithData(texto.data(), texto.size())) return false;
    destino.deleteFile();
    if (!tmp.moveFileTo(destino)) {
        tmp.deleteFile();
        return false;
    }
    return true;
}

struct ArquivoNoMain {
    std::string arquivoId, itemId, relativo;
};

std::vector<ArquivoNoMain> arquivosNoMain(matriz::db::Database& registro, const std::string& destinoIdMain) {
    std::vector<ArquivoNoMain> out;
    std::set<std::string> vistos;
    auto st = registro.prepare("SELECT arquivo_id, item_id, caminho_relativo_destino FROM consolidacao_registro "
                               "WHERE COALESCE(destino_id, '') = '' OR destino_id = ? ORDER BY consolidado_em");
    st.bind(1, Value::of(destinoIdMain));
    while (st.step()) {
        const std::string rel = st.columnText(2);
        if (rel.empty() || !vistos.insert(st.columnText(0)).second) continue;
        out.push_back({st.columnText(0), st.columnText(1), rel});
    }
    return out;
}

} // namespace

std::string gerarPacoteXmp(matriz::db::Database& registro, const std::string& itemId) {
    // XMP padrão (RDF/XML, namespaces oficiais dc/xmp) escrito à mão: o
    // Exiv2 desta build não tem o toolkit XMP (EXIV2_ENABLE_XMP OFF).
    const auto meta = coletarMetadadosDoItem(registro, itemId);
    std::string direitos;
    std::vector<std::string> tags;
    {
        auto st = registro.prepare("SELECT COALESCE(dc_rights, '') FROM item WHERE id = ?");
        st.bind(1, Value::of(itemId));
        if (st.step()) direitos = st.columnText(0);
        auto stt = registro.prepare("SELECT tag FROM item_tag WHERE item_id = ? ORDER BY tag");
        stt.bind(1, Value::of(itemId));
        while (stt.step()) tags.push_back(stt.columnText(0));
    }
    if (meta.titulo.empty() && meta.descricao.empty() && meta.artista.empty() && meta.codigoAcervo.empty() &&
        direitos.empty() && tags.empty())
        return {};
    auto esc = [](const std::string& v) {
        return juce::String::fromUTF8(v.c_str()).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace("\"", "&quot;");
    };
    auto alt = [&](const char* prop, const std::string& v) {
        return v.empty() ? juce::String()
                         : juce::String("   <") + prop + "><rdf:Alt><rdf:li xml:lang=\"x-default\">" + esc(v) +
                               "</rdf:li></rdf:Alt></" + prop + ">\n";
    };
    auto lista = [&](const char* prop, const char* tipo, const std::vector<std::string>& vs) {
        if (vs.empty()) return juce::String();
        juce::String out = juce::String("   <") + prop + "><rdf:" + tipo + ">";
        for (const auto& v : vs) out << "<rdf:li>" << esc(v) << "</rdf:li>";
        return out + "</rdf:" + tipo + "></" + prop + ">\n";
    };
    juce::String x;
    x << "<?xpacket begin=\"" << juce::String::fromUTF8("\xef\xbb\xbf") << "\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
      << "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"BKR Matriz\">\n"
      << " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
      << "  <rdf:Description rdf:about=\"\"\n"
      << "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
      << "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
      << "    xmp:CreatorTool=\"BKR Matriz\">\n"
      << alt("dc:title", meta.titulo) << alt("dc:description", meta.descricao)
      << lista("dc:creator", "Seq", meta.artista.empty() ? std::vector<std::string>{} : std::vector<std::string>{meta.artista})
      << alt("dc:rights", direitos)
      << (meta.codigoAcervo.empty() ? juce::String() : "   <dc:identifier>" + esc(meta.codigoAcervo) + "</dc:identifier>\n")
      << lista("dc:date", "Seq", meta.ano ? std::vector<std::string>{std::to_string(*meta.ano)} : std::vector<std::string>{})
      << lista("dc:subject", "Bag", tags)
      << "  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>\n";
    return x.toStdString();
}

bool escreverSidecarAvulso(matriz::db::Database& registro, const std::string& itemId, const juce::File& arquivo) {
    const auto pacote = gerarPacoteXmp(registro, itemId);
    if (pacote.empty()) return false;
    return gravarTextoAtomico(juce::File(arquivo.getFullPathName() + ".xmp"), pacote);
}

namespace {
// Corpo por arquivo, sem garantir a tabela (o laço em lote garante uma vez só).
ResultadoSidecarUnico gravarSidecarInterno(matriz::db::Database& registro, const juce::File& media,
                                           const std::string& itemId, const std::string& arquivoId,
                                           const juce::String& caminhoRelativo, bool sobrescreverEditados) {
    const juce::File master = media.getChildFile(caminhoRelativo);
    if (!master.existsAsFile()) return ResultadoSidecarUnico::SemArquivo;
    const juce::String relSidecar = caminhoRelativo + ".xmp";
    const juce::File sidecar = media.getChildFile(relSidecar);
    try {
        const std::string pacote = gerarPacoteXmp(registro, itemId);
        if (pacote.empty()) return ResultadoSidecarUnico::SemDados;
        if (sidecar.existsAsFile()) {
            std::string shaRegistrado;
            auto st = registro.prepare("SELECT sha256 FROM sidecar_registro WHERE caminho = ?");
            st.bind(1, Value::of(relSidecar.toStdString()));
            if (st.step()) shaRegistrado = st.columnText(0);
            const std::string shaAtual = sha256DeArquivo(sidecar);
            if ((shaRegistrado.empty() || shaAtual != shaRegistrado) && !sobrescreverEditados)
                return ResultadoSidecarUnico::EditadoPorFora;
            if (shaAtual == sha256DeTexto(pacote)) return ResultadoSidecarUnico::Igual;
        }
        if (!gravarTextoAtomico(sidecar, pacote)) return ResultadoSidecarUnico::Falha;
        registro.run("INSERT INTO sidecar_registro (caminho, arquivo_id, sha256, escrito_em) VALUES (?, ?, ?, ?) "
                     "ON CONFLICT(caminho) DO UPDATE SET sha256 = excluded.sha256, escrito_em = excluded.escrito_em",
                     {Value::of(relSidecar.toStdString()), Value::of(arquivoId), Value::of(sha256DeTexto(pacote)),
                      Value::of(matriz::model::agoraIso8601())});
        return ResultadoSidecarUnico::Escrito;
    } catch (...) {
        return ResultadoSidecarUnico::Falha;
    }
}
} // namespace

ResultadoSidecarUnico gravarSidecarDoArquivo(matriz::db::Database& registro, const juce::File& media,
                                             const std::string& itemId, const std::string& arquivoId,
                                             const juce::String& caminhoRelativo, bool sobrescreverEditados) {
    try {
        garantirTabelaSidecar(registro);
    } catch (...) {
        return ResultadoSidecarUnico::Falha;
    }
    return gravarSidecarInterno(registro, media, itemId, arquivoId, caminhoRelativo, sobrescreverEditados);
}

ResultadoSidecars atualizarSidecarsNoMain(matriz::db::Database& registro, const juce::File& media,
                                          const std::string& destinoIdMain, bool sobrescreverEditados,
                                          const std::function<bool(int, int)>& aoProgredir) {
    ResultadoSidecars r;
    garantirTabelaSidecar(registro);
    const auto arquivos = arquivosNoMain(registro, destinoIdMain);
    const int total = static_cast<int>(arquivos.size());
    for (int i = 0; i < total; ++i) {
        if (aoProgredir && !aoProgredir(i, total)) break;
        const auto& a = arquivos[(size_t) i];
        const juce::String relativo = juce::String::fromUTF8(a.relativo.c_str());
        switch (gravarSidecarInterno(registro, media, a.itemId, a.arquivoId, relativo, sobrescreverEditados)) {
            case ResultadoSidecarUnico::Escrito:        ++r.escritos; break;
            case ResultadoSidecarUnico::Igual:          ++r.iguais; break;
            case ResultadoSidecarUnico::EditadoPorFora: r.editadosPorFora.push_back(relativo + ".xmp"); break;
            case ResultadoSidecarUnico::Falha:          ++r.falhas; break;
            case ResultadoSidecarUnico::SemArquivo:
            case ResultadoSidecarUnico::SemDados:       break;
        }
    }
    return r;
}

int importarSidecarsEditados(matriz::db::Database& registro, const juce::File& media, const std::string& destinoIdMain,
                             const std::vector<juce::String>& caminhosRelativos) {
    int importados = 0;
    std::map<std::string, ArquivoNoMain> porSidecar;
    for (const auto& a : arquivosNoMain(registro, destinoIdMain)) porSidecar[a.relativo + ".xmp"] = a;
    const std::string agora = matriz::model::agoraIso8601();
    auto vocabTags = matriz::model::nomes::Vocabulario::carregar(registro, matriz::model::nomes::Vocabulario::Tipo::Tags);
    for (const auto& rel : caminhosRelativos) {
        auto it = porSidecar.find(rel.toStdString());
        if (it == porSidecar.end()) continue;
        auto d = matriz::ingest::lerArquivoXmp(media.getChildFile(rel));
        if (!d) continue;
        const std::string& itemId = it->second.itemId;
        if (!d->titulo.empty())
            registro.run("UPDATE item SET titulo = ?, dc_title = ? WHERE id = ?",
                         {Value::of(d->titulo), Value::of(d->titulo), Value::of(itemId)});
        if (!d->descricao.empty())
            registro.run("UPDATE item SET dc_description = ? WHERE id = ?", {Value::of(d->descricao), Value::of(itemId)});
        if (!d->autor.empty())
            registro.run("UPDATE item SET dc_creator = ? WHERE id = ?", {Value::of(d->autor), Value::of(itemId)});
        if (!d->direitos.empty())
            registro.run("UPDATE item SET dc_rights = ? WHERE id = ?", {Value::of(d->direitos), Value::of(itemId)});
        for (const auto& t : d->tags) {
            const std::string canon = vocabTags.canonico(t);
            if (canon.empty()) continue;
            registro.run("INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                         {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(canon)});
        }
        registro.run("UPDATE item SET metadados_editados = 1, atualizado_em = ?, notas_livres = "
                     "CASE WHEN TRIM(COALESCE(notas_livres, '')) = '' THEN ? ELSE notas_livres || char(10) || ? END "
                     "WHERE id = ?",
                     {Value::of(agora), Value::of("Imported from sidecar edited outside BKR Matriz: " + rel.toStdString()),
                      Value::of("Imported from sidecar edited outside BKR Matriz: " + rel.toStdString()), Value::of(itemId)});
        ++importados;
    }
    // O sidecar passa a refletir o catálogo de novo (e fica registrado).
    atualizarSidecarsNoMain(registro, media, destinoIdMain, /*sobrescreverEditados*/ true);
    return importados;
}

} // namespace matriz::consolidacao
