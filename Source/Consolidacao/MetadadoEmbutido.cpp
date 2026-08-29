#include "MetadadoEmbutido.h"

#include <algorithm>
#include <cmath>
#include <exiv2/exiv2.hpp>
#include "../Model/Project.h"
#include "../Vault/Resolucao.h"

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

} // namespace matriz::consolidacao
