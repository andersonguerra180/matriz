#include "BounceSelfTest.h"

#include <JuceHeader.h>
#include <exiv2/exiv2.hpp>

#include <iostream>
#include <set>

#include "../Consolidacao/Consolidacao.h"
#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "ProjetoAberto.h"

namespace matriz::ui {

namespace {

using matriz::db::Value;

std::string sha(const juce::File& f) { return matriz::ingest::calcularChecksums(f).sha256; }

std::string inserirItem(matriz::db::Database& reg, const std::string& projetoId, const std::string& codigo, const juce::File& arq) {
    const std::string id = matriz::model::novoUuid(), agora = matriz::model::agoraIso8601();
    reg.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
            "VALUES (?, ?, ?, ?, 'foto', 'novo', ?, ?)",
            {Value::of(id), Value::of(projetoId), Value::of(codigo), Value::of(arq.getFileNameWithoutExtension().toStdString()),
             Value::of(agora), Value::of(agora)});
    reg.run("INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, "
            "checksum_sha256, criado_em, atualizado_em) VALUES (?, ?, ?, ?, 'preservation_master', 1, ?, ?, ?, ?)",
            {Value::of(matriz::model::novoUuid()), Value::of(id), Value::of(arq.getFileName().toStdString()),
             Value::of(arq.getFullPathName().toStdString()), Value::of(static_cast<long long>(arq.getSize())), Value::of(sha(arq)),
             Value::of(agora), Value::of(agora)});
    return id;
}

void escreverWav(const juce::File& destino) {
    juce::MemoryOutputStream out;
    const int amostras = 800, taxa = 8000;
    out.write("RIFF", 4); out.writeInt(36 + amostras * 2); out.write("WAVE", 4); out.write("fmt ", 4);
    out.writeInt(16); out.writeShort(1); out.writeShort(1); out.writeInt(taxa); out.writeInt(taxa * 2); out.writeShort(2); out.writeShort(16);
    out.write("data", 4); out.writeInt(amostras * 2);
    for (int i = 0; i < amostras; ++i) out.writeShort(static_cast<short>((i * 13) % 4000 - 2000));
    destino.getParentDirectory().createDirectory();
    destino.replaceWithData(out.getData(), out.getDataSize());
}

// Insere segmentos logo depois do SOI (COM e XMP) pra simular um JPEG "sujo" de verdade.
void injetarSegmentos(const juce::File& jpg) {
    juce::MemoryBlock mb;
    jpg.loadFileAsData(mb);
    juce::MemoryOutputStream out;
    const auto* b = static_cast<const juce::uint8*>(mb.getData());
    out.write(b, 2);  // SOI
    auto seg = [&](juce::uint8 marcador, const juce::String& payload) {
        out.writeByte(static_cast<char>(0xFF)); out.writeByte(static_cast<char>(marcador));
        out.writeShortBigEndian(static_cast<short>(payload.getNumBytesAsUTF8() + 2 + (marcador == 0xE1 ? 1 : 0)));
        out.write(payload.toRawUTF8(), payload.getNumBytesAsUTF8());
        if (marcador == 0xE1) out.writeByte(0);
    };
    seg(0xFE, "COMENTARIO SECRETO");
    seg(0xE1, "http://ns.adobe.com/xap/1.0/");  // sem o NUL final = ajustado abaixo
    out.write(b + 2, mb.getSize() - 2);
    jpg.replaceWithData(out.getData(), out.getDataSize());
}

}  // namespace

int rodarBounceSelfTest() {
    std::cout << "== Bounce List (tag B + export Original / Embed / Clean) ==\n";
    int falhas = 0;
    auto checar = [&](bool ok, const juce::String& d) {
        std::cout << (ok ? "  OK   " : "  FAIL ") << d << "\n";
        if (!ok) ++falhas;
    };
    juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("matriz_bounce_selftest_" + juce::Uuid().toDashedString());
    try {
        const juce::File src = raiz.getChildFile("SOURCE");
        src.createDirectory();

        // ---- fixtures
        const auto jpg = src.getChildFile("foto.jpg");
        {
            juce::Image im(juce::Image::RGB, 32, 24, true);
            juce::FileOutputStream o(jpg);
            juce::JPEGImageFormat fmt;
            fmt.writeImageToStream(im, o);
        }
        {
            auto image = Exiv2::ImageFactory::open(jpg.getFullPathName().toStdString());
            image->readMetadata();
            auto& exif = image->exifData();
            exif["Exif.Image.Make"] = "CanonTeste";
            exif["Exif.Image.Orientation"] = static_cast<uint16_t>(6);
            exif["Exif.GPSInfo.GPSLatitudeRef"] = "S";
            exif["Exif.GPSInfo.GPSLatitude"] = "23/1 32/1 51/1";
            exif["Exif.GPSInfo.GPSLongitudeRef"] = "W";
            exif["Exif.GPSInfo.GPSLongitude"] = "46/1 38/1 10/1";
            image->iptcData()["Iptc.Application2.Keywords"] = "SEGREDO";
            image->writeMetadata();
        }
        injetarSegmentos(jpg);
        const auto cr2 = src.getChildFile("raw.cr2");
        {
            juce::MemoryOutputStream m;  // TIFF mínimo
            m.write("II", 2); m.writeShort(42); m.writeInt(8); m.writeShort(1);
            m.writeShort(256); m.writeShort(3); m.writeInt(1); m.writeShort(4); m.writeShort(0); m.writeInt(0);
            cr2.replaceWithData(m.getData(), m.getDataSize());
        }
        const auto psd = src.getChildFile("arte.psd");
        psd.replaceWithText("8BPS-conteudo-qualquer");
        const auto wav = src.getChildFile("som.wav");
        escreverWav(wav);
        const auto extra = src.getChildFile("fora.wav");  // item SEM tag B
        escreverWav(extra);
        const auto soltoWav = src.getChildFile("solto.wav");  // com tag B, mas sem pasta no mapa
        escreverWav(soltoWav);

        matriz::model::NovoProjetoParams params;
        params.nome = "Bounce";
        params.prefixoNomenclatura = "BNC";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("P"), params);
        const std::string projetoId = projeto->projetoId();

        // ---- 1) migração: projeto ANTIGO (CHECK só com 4 tipos) passa a aceitar 'bounce' e não perde as marcas
        {
            auto& reg = projeto->registro();
            const auto idMig = inserirItem(reg, projetoId, "BNC-M", extra);
            reg.exec("DROP TABLE item_marcacao");
            reg.exec("CREATE TABLE item_marcacao (item_id TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE, "
                     "tipo TEXT NOT NULL CHECK (tipo IN ('html', 'zip', 'print', 'watermark')), PRIMARY KEY (item_id, tipo))");
            reg.run("INSERT INTO item_marcacao (item_id, tipo) VALUES (?, 'html')", {Value::of(idMig)});
            bool recusava = false;
            try { reg.run("INSERT INTO item_marcacao (item_id, tipo) VALUES (?, 'bounce')", {Value::of(idMig)}); } catch (...) { recusava = true; }
            checar(recusava, "setup: the old table refuses 'bounce'");
        }
        const juce::File pastaProjeto = projeto->pasta();
        projeto.reset();
        projeto = matriz::model::Project::abrir(pastaProjeto);
        {
            auto& reg = projeto->registro();
            bool aceita = true;
            try { reg.run("INSERT INTO item_marcacao (item_id, tipo) SELECT id, 'bounce' FROM item LIMIT 1", {}); } catch (...) { aceita = false; }
            auto st = reg.prepare("SELECT COUNT(*) FROM item_marcacao WHERE tipo = 'html'");
            st.step();
            checar(aceita && st.columnInt(0) == 1, "migration: reopening an old project accepts 'bounce' and keeps the old H mark");
            reg.exec("DELETE FROM item_marcacao");
            reg.exec("DELETE FROM item");
        }

        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        const auto idJpg = inserirItem(reg, projetoId, "BNC-1", jpg);
        const auto idCr2 = inserirItem(reg, projetoId, "BNC-2", cr2);
        const auto idPsd = inserirItem(reg, projetoId, "BNC-3", psd);
        const auto idWav = inserirItem(reg, projetoId, "BNC-4", wav);
        const auto idFora = inserirItem(reg, projetoId, "BNC-5", extra);
        const auto idSolto = inserirItem(reg, projetoId, "BNC-6", soltoWav);

        const std::string mapa = pa.criarFolderMap("Mapa", std::nullopt);
        const std::string pasta = pa.criarPastaAcervo("Entrega", std::nullopt, mapa);
        pa.adicionarItensAPasta({idJpg, idCr2, idPsd, idWav, idFora}, pasta);  // idSolto fica SEM pasta
        pa.adicionarPessoa("Maria");
        pa.salvarMetadado(idJpg, "dc_title", "Show no Rio");
        pa.salvarMetadado(idJpg, "dc_description", "Primeira noite");
        pa.salvarMetadado(idJpg, "ano", "2013-05-04");
        pa.salvarMetadado(idJpg, "dc_subject", "Turne");
        pa.salvarMetadado(idJpg, "collection_type", "Album");
        pa.definirTags(idJpg, {"Show", "Maria"});
        reg.run("INSERT INTO asset_geolocation (asset_id, latitude, longitude, city, state_province, country, source) "
                "VALUES (?, -23.5505, -46.6333, 'Sao Paulo', 'SP', 'Brasil', 'USER_ADDRESS')", {Value::of(idJpg)});

        const juce::File media = pa.projeto().pastaMedia();
        auto planoMain = matriz::consolidacao::planejarConsolidacao(reg, pa.projeto().pasta(), media,
                                                                    {matriz::consolidacao::NivelHierarquia::PastaManual}, {},
                                                                    matriz::consolidacao::ModoPrefixoArquivo::Nenhum, {}, true, false, false, false, mapa);
        auto resMain = matriz::consolidacao::executarConsolidacao(reg, pa.projeto().pasta(), media, planoMain);
        checar(resMain.consolidados == 6, "setup: 6 files in the MAIN (" + juce::String(resMain.consolidados) + ")");
        std::map<std::string, std::string> hashMain;
        for (const auto& f : media.findChildFiles(juce::File::findFiles, true, "*")) hashMain[f.getFullPathName().toStdString()] = sha(f);

        // ---- 2) tag B: liga, persiste e fica fora de qualquer metadado
        pa.definirMarcacao(ProjetoAberto::TipoMarcacao::Bounce, {idJpg, idCr2, idPsd, idWav, idSolto}, true);
        pa.aguardarGravacoesMarcas();
        auto contarB = [&] {
            auto st = reg.prepare("SELECT COUNT(*) FROM item_marcacao WHERE tipo = 'bounce'");
            st.step();
            return static_cast<int>(st.columnInt(0));
        };
        checar(contarB() == 5 && pa.contarMarcacoes(ProjetoAberto::TipoMarcacao::Bounce) == 5, "tag B: 5 files marked and saved in the project database");
        pa.alternarMarcacao(ProjetoAberto::TipoMarcacao::Bounce, {idFora});
        pa.alternarMarcacao(ProjetoAberto::TipoMarcacao::Bounce, {idFora});  // liga e desliga: volta ao estado anterior
        pa.aguardarGravacoesMarcas();
        checar(contarB() == 5 && !pa.contemMarcacao(ProjetoAberto::TipoMarcacao::Bounce, idFora), "tag B: pressing B twice toggles it back off");

        auto exportar = [&](const juce::String& nome, matriz::consolidacao::ModoMetadadosSaida modo, bool sidecar, int& semPasta) {
            const juce::File saida = raiz.getChildFile(nome);
            saida.createDirectory();
            auto plano = matriz::consolidacao::planejarConsolidacao(reg, pa.projeto().pasta(), saida,
                                                                    {matriz::consolidacao::NivelHierarquia::PastaManual}, {},
                                                                    matriz::consolidacao::ModoPrefixoArquivo::Nenhum, {}, true, false, false, true, mapa);
            std::set<std::string> b;
            for (const auto& id : pa.idsMarcados(ProjetoAberto::TipoMarcacao::Bounce)) b.insert(id);
            semPasta = 0;
            for (const auto& id : plano.semPastaExcluidos) if (b.count(id)) ++semPasta;
            std::vector<matriz::consolidacao::ItemPlanejado> recorte;
            for (auto& ip : plano.itens) if (b.count(ip.itemId)) recorte.push_back(std::move(ip));
            plano.itens = std::move(recorte);
            matriz::consolidacao::OpcoesMetadadosSaida meta;
            meta.modo = modo;
            meta.sidecar = sidecar;
            return std::make_pair(saida, matriz::consolidacao::executarExport(reg, pa.projeto().pasta(), saida, plano, {}, {}, false, false, meta));
        };
        using Modo = matriz::consolidacao::ModoMetadadosSaida;
        auto nomeJpg = [](const juce::File& s) { return s.getChildFile("Entrega/foto.jpg"); };

        // ---- 3) Original: cópia idêntica; sem pasta fica de fora; sidecar opcional
        int semPasta = 0;
        {
            auto [saida, r] = exportar("saida_original", Modo::Original, false, semPasta);
            checar(r.copiados == 4 && r.falhas.empty(), "Original: 4 files copied (" + juce::String(r.copiados) + ")");
            checar(semPasta == 1, "Original: the B file without a folder in the chosen map is left out and counted (" + juce::String(semPasta) + ")");
            bool identicos = true;
            for (const auto& f : saida.findChildFiles(juce::File::findFiles, true, "*")) {
                const auto m = media.getChildFile("Entrega/" + f.getFileName());
                identicos = identicos && m.existsAsFile() && sha(f) == sha(m);
            }
            checar(identicos, "Original: every copy is byte-identical to the MAIN file");
            checar(saida.findChildFiles(juce::File::findFiles, true, "*.xmp").isEmpty(), "Original without sidecar: no .xmp");
        }
        {
            auto [saida, r] = exportar("saida_original_sidecar", Modo::Original, true, semPasta);
            const auto sc = juce::File(nomeJpg(saida).getFullPathName() + ".xmp");
            const auto txt = sc.loadFileAsString();
            checar(sc.existsAsFile() && txt.contains("Show no Rio") && txt.contains("Turne") && txt.contains("Maria"),
                   "Original + sidecar: .xmp has title, subject and people");
            checar(!txt.containsIgnoreCase("bounce") && !txt.containsIgnoreCase("tag B"), "the B tag never goes into the sidecar");
            checar(sha(nomeJpg(saida)) == hashMain[media.getChildFile("Entrega/foto.jpg").getFullPathName().toStdString()],
                   "Original + sidecar: the image itself is still identical");
        }

        // ---- 4) Embed
        {
            auto [saida, r] = exportar("saida_embed", Modo::Embed, false, semPasta);
            checar(r.copiados == 4 && r.falhas.empty() && r.embutidos >= 1, "Embed: copied and embedded (" + juce::String(r.embutidos) + ")");
            const auto c = nomeJpg(saida);
            checar(juce::ImageFileFormat::loadFrom(c).isValid(), "Embed: the JPEG still decodes");
            auto image = Exiv2::ImageFactory::open(c.getFullPathName().toStdString());
            image->readMetadata();
            std::set<std::string> kw;
            for (const auto& d : image->iptcData()) if (d.key() == "Iptc.Application2.Keywords") kw.insert(d.toString());
            checar(kw.count("Show") && kw.count("Maria"), "Embed: tags and people in the IPTC keywords");
            auto texto = [&](const char* k) { auto it = image->iptcData().findKey(Exiv2::IptcKey(k)); return it == image->iptcData().end() ? std::string() : it->toString(); };
            checar(texto("Iptc.Application2.ObjectName") == "Show no Rio" && texto("Iptc.Application2.Caption") == "Primeira noite", "Embed: title and description");
            checar(texto("Iptc.Application2.City") == "Sao Paulo" && texto("Iptc.Application2.CountryName") == "Brasil", "Embed: geo location (city, country)");
            checar(texto("Iptc.Application2.SuppCategory") == "Turne" && texto("Iptc.Application2.ObjectAttribute") == "Album", "Embed: subject and content");
            auto it = image->exifData().findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
            checar(it != image->exifData().end() && it->toString().rfind("2013:05:04", 0) == 0, "Embed: event date");
            checar(image->exifData().findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitude")) != image->exifData().end(), "Embed: GPS coordinates");
            checar(sha(media.getChildFile("Entrega/foto.jpg")) == hashMain[media.getChildFile("Entrega/foto.jpg").getFullPathName().toStdString()],
                   "Embed: the MAIN file did not change");
        }

        // ---- 5) Clean
        {
            auto [saida, r] = exportar("saida_clean", Modo::Clean, true /* sidecar pedido: deve ser ignorado */, semPasta);
            checar(r.copiados == 4 && r.falhas.empty(), "Clean: 4 files exported, no failure (" + juce::String((int) r.falhas.size()) + ")");
            checar(r.limpos == 2 && r.rawSemLimpeza == 1 && r.tiposSemLimpeza == 1,
                   "Clean: 2 cleaned (JPEG, WAV), 1 RAW without cleaning, 1 unsupported type (" + juce::String(r.limpos) + "/" + juce::String(r.rawSemLimpeza) + "/" + juce::String(r.tiposSemLimpeza) + ")");
            checar(saida.findChildFiles(juce::File::findFiles, true, "*.xmp").isEmpty(), "Clean: no sidecar even when requested");
            const auto c = nomeJpg(saida);
            juce::MemoryBlock mb;
            c.loadFileAsData(mb);
            const juce::String bruto = juce::String::createStringFromData(mb.getData(), (int) mb.getSize());
            checar(!bruto.contains("COMENTARIO") && !bruto.contains("xap/1.0") && !bruto.contains("CanonTeste") && !bruto.contains("SEGREDO"),
                   "Clean: no comment, XMP, camera make or IPTC left in the JPEG bytes");
            auto image = Exiv2::ImageFactory::open(c.getFullPathName().toStdString());
            image->readMetadata();
            bool gps = false;
            for (const auto& d : image->exifData()) gps = gps || d.groupName() == "GPSInfo";
            auto ori = image->exifData().findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
            checar(!gps && image->iptcData().empty(), "Clean: no GPS and no IPTC");
            checar(ori != image->exifData().end() && ori->toInt64() == 6, "Clean: the orientation is preserved");
            const auto im = juce::ImageFileFormat::loadFrom(c);
            checar(im.isValid() && im.getWidth() == 32 && im.getHeight() == 24, "Clean: the image still decodes with the same size");
            checar(sha(saida.getChildFile("Entrega/raw.cr2")) == hashMain[media.getChildFile("Entrega/raw.cr2").getFullPathName().toStdString()],
                   "Clean: the RAW is copied untouched");
            checar(saida.getChildFile("Entrega/som.wav").getSize() > 44, "Clean: the cleaned WAV is still a file with audio");
        }

        // ---- 6) nada mudou onde não podia
        bool mainIntacto = true;
        for (const auto& f : media.findChildFiles(juce::File::findFiles, true, "*")) mainIntacto = mainIntacto && hashMain[f.getFullPathName().toStdString()] == sha(f);
        checar(mainIntacto && media.findChildFiles(juce::File::findFiles, true, "*").size() == (int) hashMain.size(), "the MAIN has the same files and hashes after all exports");
        checar(contarB() == 5, "the B tag stays on the files after the export (not cleared)");
    } catch (const std::exception& e) {
        std::cout << "  FAIL exception: " << e.what() << "\n";
        ++falhas;
    }
    raiz.deleteRecursively();
    std::cout << (falhas == 0 ? "== Bounce List: ALL OK ==\n" : "== Bounce List: FAILURES: " + std::to_string(falhas) + " ==\n");
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
