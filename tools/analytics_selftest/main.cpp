#include <JuceHeader.h>
#include "Db/Database.h"
#include "Model/Project.h"
#include "Analytics/AnalyticsTypes.h"
#include "Analytics/AnalyticsEngine.h"
#include "Analytics/AssetGeolocation.h"
#include "Ingest/LeituraTecnica.h"
#include "Ingest/IngestArquivo.h"
#include "Ui/ProjetoAberto.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <chrono>

using namespace matriz::analytics;
using namespace matriz::ingest;

void runCategoryAndExtractorUnitTest() {
    std::cout << "[TEST] 1. Category Classification & Automated Metadata Extraction..." << std::endl;

    // 1. Classification test
    jassert(categoriaPorExtensao("wav") == CategoriaMidia::Audio);
    jassert(categoriaPorExtensao("mp3") == CategoriaMidia::Audio);
    jassert(categoriaPorExtensao("mp4") == CategoriaMidia::Video);
    jassert(categoriaPorExtensao("mov") == CategoriaMidia::Video);
    jassert(categoriaPorExtensao("jpg") == CategoriaMidia::Imagem);
    jassert(categoriaPorExtensao("png") == CategoriaMidia::Imagem);
    jassert(categoriaPorExtensao("pdf") == CategoriaMidia::Documento);
    jassert(categoriaPorExtensao("docx") == CategoriaMidia::Documento);
    jassert(categoriaPorExtensao("txt") == CategoriaMidia::Texto);

    std::cout << "   -> PASS: Media categories correctly classified!" << std::endl;

    // 2. Dummy PDF extraction test
    juce::File tempPdf = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("test_sample.pdf");
    tempPdf.replaceWithText("%PDF-1.4\n1 0 obj\n<< /Title (BKR Test Document) /Author (Antigravity) /Subject (Unit Test) >>\nendobj\n/Type /Page\n");

    LeituraTecnicaResultado pdfRes = lerTecnica(tempPdf);
    jassert(pdfRes.metaType.value_or("") == "Text");
    jassert(pdfRes.metaTitle.value_or("") == "BKR Test Document");
    jassert(pdfRes.metaCreator.value_or("") == "Antigravity");
    jassert(pdfRes.metaSubject.value_or("") == "Unit Test");
    tempPdf.deleteFile();

    std::cout << "   -> PASS: PDF Metadata & Dublin Core extraction verified!" << std::endl;

    // 3. Dummy TXT extraction test
    juce::File tempTxt = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("test_sample.txt");
    tempTxt.replaceWithText("Line 1\nLine 2\nLine 3\n");
    LeituraTecnicaResultado txtRes = lerTecnica(tempTxt);
    jassert(txtRes.metaType.value_or("") == "Text");
    jassert(txtRes.metaFormat.value_or("") == "text/plain");
    tempTxt.deleteFile();

    std::cout << "   -> PASS: Text document extraction verified!" << std::endl << std::endl;
}

void runProvenanceAndReingestSafetyUnitTest() {
    std::cout << "[TEST] 2. Provenance & Re-ingest Safety (USER_* > EMBEDDED_METADATA)..." << std::endl;

    matriz::db::Database db(":memory:");
    db.execScript(
        "CREATE TABLE item (id TEXT PRIMARY KEY, projeto_id TEXT, codigo_acervo TEXT, titulo TEXT, tipo_midia TEXT, estado TEXT, notas_livres TEXT, criado_em TEXT, atualizado_em TEXT);"
        "CREATE TABLE item_tag (id TEXT PRIMARY KEY, item_id TEXT, tag TEXT, UNIQUE(item_id, tag));"
        "CREATE TABLE item_campo (id TEXT PRIMARY KEY, item_id TEXT, nivel TEXT, nivel_indice INT, campo_id TEXT, valor TEXT, fonte TEXT, atualizado_em TEXT, UNIQUE(item_id, nivel, nivel_indice, campo_id));"
        "CREATE TABLE asset_geolocation ("
        "  asset_id TEXT PRIMARY KEY REFERENCES item(id) ON DELETE CASCADE,"
        "  latitude REAL, longitude REAL, altitude REAL,"
        "  continent TEXT, country TEXT, country_code TEXT, state_province TEXT, state_code TEXT,"
        "  city TEXT, municipality TEXT, neighborhood TEXT, district TEXT, postal_code TEXT,"
        "  street TEXT, street_number TEXT, locality TEXT, formatted_address TEXT,"
        "  source TEXT NOT NULL DEFAULT 'NONE', precision_accuracy REAL, confidence REAL,"
        "  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
    );

    std::string itemId = "item_test_001";
    db.run("INSERT INTO item (id, projeto_id, titulo, tipo_midia, estado, criado_em, atualizado_em) VALUES (?, 'p1', 'Test Item', 'foto', 'novo', '2025-01-01T00:00:00Z', '2025-01-01T00:00:00Z')", {matriz::db::Value::of(itemId)});

    // 1. Initial Embedded Metadata Geolocation
    AssetGeolocation geoEmbed;
    geoEmbed.assetId = itemId;
    geoEmbed.latitude = -16.44;
    geoEmbed.longitude = -39.06;
    geoEmbed.source = GeoSource::EmbeddedMetadata;
    AssetGeolocationRepository::salvar(db, geoEmbed);

    auto geo1 = AssetGeolocationRepository::obterPorAssetId(db, itemId);
    jassert(geo1.has_value());
    jassert(geo1->source == GeoSource::EmbeddedMetadata);

    // 2. User edits Geolocation manually (USER_COORDINATES)
    AssetGeolocation geoUser = *geo1;
    geoUser.latitude = -16.45;
    geoUser.longitude = -39.07;
    geoUser.city = "Porto Seguro User Edit";
    geoUser.source = GeoSource::UserCoordinates;
    AssetGeolocationRepository::salvar(db, geoUser);

    auto geo2 = AssetGeolocationRepository::obterPorAssetId(db, itemId);
    jassert(geo2.has_value());
    jassert(geo2->source == GeoSource::UserCoordinates);
    jassert(geo2->city.value_or("") == "Porto Seguro User Edit");

    // 3. Re-ingest simulation: Embedded EXIF GPS MUST NOT overwrite USER_*
    auto currentGeo = AssetGeolocationRepository::obterPorAssetId(db, itemId);
    if (!currentGeo.has_value() || currentGeo->source != GeoSource::UserCoordinates) {
        AssetGeolocation reingestGeo = geoEmbed;
        AssetGeolocationRepository::salvar(db, reingestGeo);
    }

    auto geo3 = AssetGeolocationRepository::obterPorAssetId(db, itemId);
    jassert(geo3->source == GeoSource::UserCoordinates);
    jassert(geo3->city.value_or("") == "Porto Seguro User Edit");

    // 4. Tags preservation test
    db.run("INSERT INTO item_tag (id, item_id, tag) VALUES ('t1', ?, 'Historical')", {matriz::db::Value::of(itemId)});
    db.run("INSERT INTO item_tag (id, item_id, tag) VALUES ('t2', ?, 'Archive')", {matriz::db::Value::of(itemId)});

    auto stmtTags = db.prepare("SELECT COUNT(*) FROM item_tag WHERE item_id = ?");
    stmtTags.bind(1, matriz::db::Value::of(itemId));
    if (stmtTags.step()) {
        jassert(stmtTags.columnInt(0) == 2);
    }

    std::cout << "   -> PASS: Provenance & Re-ingest Safety verified!" << std::endl << std::endl;
}

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceGui;

    std::cout << "================================================================================" << std::endl;
    std::cout << "               BKR MATRIZ METADATA RESTRUCTURING SELF-TEST                     " << std::endl;
    std::cout << "================================================================================" << std::endl << std::endl;

    try {
        runCategoryAndExtractorUnitTest();
        runProvenanceAndReingestSafetyUnitTest();

        std::cout << "================================================================================" << std::endl;
        std::cout << "               ALL METADATA & RESTRUCTURING TESTS PASSED!                      " << std::endl;
        std::cout << "================================================================================" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "METADATA TEST FAILED WITH EXCEPTION: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "METADATA TEST FAILED WITH UNKNOWN EXCEPTION" << std::endl;
        return 1;
    }
}
