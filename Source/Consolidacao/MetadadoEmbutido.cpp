#include "MetadadoEmbutido.h"

#include <algorithm>
#include <cmath>

namespace matriz::consolidacao {

using matriz::db::Value;

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

    // Monta o arquivo novo: cabeçalho RIFF + todo o conteúdo original a
    // partir do byte 12, e os chunks novos no fim. Nenhum chunk existente é
    // reescrito nem reordenado — o áudio em si sai idêntico.
    juce::MemoryOutputStream saida;
    escreverFourCC(saida, "RIFF");
    saida.writeInt(0); // tamanho, corrigido no fim
    escreverFourCC(saida, "WAVE");
    saida.write(dados + 12, original.getSize() - 12);

    juce::MemoryBlock cueBloco(cue.getData(), cue.getDataSize());
    juce::MemoryBlock adtlBloco(adtl.getData(), adtl.getDataSize());
    escreverChunk(saida, "cue ", cueBloco);
    escreverChunk(saida, "LIST", adtlBloco);
    escreverChunk(saida, "iXML", ixml);

    juce::MemoryBlock resultado(saida.getData(), saida.getDataSize());
    juce::ByteOrder::littleEndianInt(resultado.getData()); // no-op explícito: o campo é corrigido abaixo
    juce::uint32 tamanhoRiff = static_cast<juce::uint32>(resultado.getSize() - 8);
    std::memcpy(static_cast<char*>(resultado.getData()) + 4, &tamanhoRiff, 4);

    // Escreve num temporário e só então substitui — se faltar espaço ou o
    // processo morrer no meio, a cópia verificada continua intacta.
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
        if (!destino.hasFileExtension("wav")) continue; // ver nota de escopo no header
        if (embutirMarcadoresEmWav(destino, marcadoresDoItem(registro, itemId))) ++enriquecidos;
    }
    return enriquecidos;
}

} // namespace matriz::consolidacao
