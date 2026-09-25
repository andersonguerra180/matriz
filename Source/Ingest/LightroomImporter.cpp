#include "LightroomImporter.h"

#include <sqlite3.h>
#include <cmath>
#include <algorithm>
#include "../Model/Project.h"
#include "../Model/ProjectLog.h"
#include "../Vault/AssetRelinkEngine.h"
#include "../Vault/Volume.h"
#include "../Analytics/AssetGeolocation.h"
#include "LeituraTecnica.h"
#include "IngestArquivo.h"
#include "Miniaturas.h"
#include "CacheArquivo.h"
#include "../Consolidacao/MetadadoEmbutido.h"

namespace matriz::ingest {

using matriz::db::Value;

namespace {

// RAII helper to close sqlite3 handles
struct SqliteCloser {
    sqlite3* db = nullptr;
    ~SqliteCloser() {
        if (db) sqlite3_close(db);
    }
};

// RAII helper to finalize sqlite3_stmt handles
struct StmtFinalizer {
    sqlite3_stmt* stmt = nullptr;
    ~StmtFinalizer() {
        if (stmt) sqlite3_finalize(stmt);
    }
};

// Check if a table exists in sqlite3 database
bool tabelaExiste(sqlite3* db, const char* nomeTabela) {
    const char* sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    StmtFinalizer fin{stmt};
    sqlite3_bind_text(stmt, 1, nomeTabela, -1, SQLITE_STATIC);
    return (sqlite3_step(stmt) == SQLITE_ROW);
}

// Convert APEX shutter speed value (-log2(exposureTime)) to human-readable string
juce::String formatarVelocidade(double apexShutterSpeed) {
    if (apexShutterSpeed == 0.0) return "1s";
    double expTime = std::pow(2.0, -apexShutterSpeed);
    if (expTime >= 1.0) {
        return juce::String(expTime, 1) + "s";
    }
    double denom = 1.0 / expTime;
    return "1/" + juce::String(juce::roundToInt(denom)) + "s";
}

// Convert APEX aperture value (2*log2(fNumber)) to standard f-number
juce::String formatarAbertura(double apexAperture) {
    if (apexAperture <= 0.0) return "";
    double fNum = std::pow(2.0, apexAperture / 2.0);
    return "f/" + juce::String(fNum, 1);
}

} // namespace

juce::String LightroomImporter::formatarNotasTecnicas(const LightroomFotoMetadados& foto) {
    juce::StringArray partes;
    if (foto.camera.isNotEmpty()) partes.add("Câmera: " + foto.camera);
    if (foto.lente.isNotEmpty()) partes.add("Lente: " + foto.lente);
    if (foto.distanciaFocal.isNotEmpty()) partes.add("Distância Focal: " + foto.distanciaFocal);
    if (foto.abertura.isNotEmpty()) partes.add("Abertura: " + foto.abertura);
    if (foto.velocidade.isNotEmpty()) partes.add("Velocidade: " + foto.velocidade);
    if (foto.iso.isNotEmpty()) partes.add("ISO: " + foto.iso);
    if (foto.copiaVirtual) partes.add("Cópia Virtual: Sim");
    if (foto.editada) partes.add("Editada no Lightroom: Sim");
    if (foto.pilha.isNotEmpty()) partes.add("Pilha: " + foto.pilha);
    return partes.joinIntoString("; ");
}

bool LightroomImporter::validarCatalogoClassic(const juce::File& lrcatArquivo, juce::String& outErro) {
    if (!lrcatArquivo.existsAsFile()) {
        outErro = "Arquivo de catálogo não encontrado.";
        return false;
    }

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(lrcatArquivo.getFullPathName().toUTF8(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || !db) {
        outErro = "Não foi possível abrir o arquivo como banco SQLite.";
        if (db) sqlite3_close(db);
        return false;
    }
    SqliteCloser closer{db};

    // Check mandatory Lightroom Classic tables
    if (!tabelaExiste(db, "Adobe_images") ||
        !tabelaExiste(db, "AgLibraryFile") ||
        !tabelaExiste(db, "AgLibraryFolder") ||
        !tabelaExiste(db, "AgLibraryRootFolder")) {
        outErro = "O arquivo selecionado não é um catálogo reconhecido do Lightroom Classic.";
        return false;
    }

    return true;
}

bool LightroomImporter::lerCatalogo(const juce::File& lrcatArquivo,
                                   std::vector<LightroomFotoMetadados>& outFotos,
                                   std::vector<juce::File>& outArquivosSessao,
                                   bool& outEstavaBloqueado,
                                   juce::String& outErro) {
    outFotos.clear();
    outArquivosSessao.clear();
    outEstavaBloqueado = false;
    outErro = "";

    if (!lrcatArquivo.existsAsFile()) {
        outErro = "Arquivo de catálogo não encontrado.";
        return false;
    }

    // 1. Verificar lock
    juce::File lockFile = lrcatArquivo.getSiblingFile(lrcatArquivo.getFileName() + ".lock");
    if (lockFile.existsAsFile()) {
        outEstavaBloqueado = true;
    }

    // 2. Checagem de espaço livre antes de copiar para pasta temporária
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::int64 espacoLivre = tempDir.getBytesFreeOnVolume();
    juce::int64 tamanhoCat = lrcatArquivo.getSize();
    if (espacoLivre > 0 && espacoLivre < tamanhoCat * 2) {
        outErro = "Espaço livre insuficiente na pasta temporária para copiar o catálogo.";
        return false;
    }

    // 3. Criar cópia temporária do .lrcat com RAII guard para limpeza garantida
    juce::String rndSuffix = juce::String(juce::Random::getSystemRandom().nextInt(9999999));
    juce::File tempDbFile = tempDir.getChildFile("matriz_lr_tmp_" + rndSuffix + ".lrcat");
    if (!lrcatArquivo.copyFileTo(tempDbFile)) {
        outErro = "Falha ao criar cópia temporária do catálogo.";
        return false;
    }

    struct TempCleanupGuard {
        juce::File db;
        juce::File wal;
        juce::File journal;
        ~TempCleanupGuard() {
            if (db.existsAsFile()) db.deleteFile();
            if (wal.existsAsFile()) wal.deleteFile();
            if (journal.existsAsFile()) journal.deleteFile();
        }
    } tempGuard{tempDbFile,
                tempDbFile.getSiblingFile(tempDbFile.getFileName() + "-wal"),
                tempDbFile.getSiblingFile(tempDbFile.getFileName() + "-journal")};

    // Copiar WAL / journal se existirem junto com o .lrcat original
    juce::File walOrig = lrcatArquivo.getSiblingFile(lrcatArquivo.getFileName() + "-wal");
    if (walOrig.existsAsFile()) walOrig.copyFileTo(tempGuard.wal);
    juce::File journalOrig = lrcatArquivo.getSiblingFile(lrcatArquivo.getFileName() + "-journal");
    if (journalOrig.existsAsFile()) journalOrig.copyFileTo(tempGuard.journal);

    // 4. Abrir cópia temporária em modo somente leitura
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(tempDbFile.getFullPathName().toUTF8(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || !db) {
        outErro = "Não foi possível abrir a cópia do catálogo com SQLite.";
        if (db) sqlite3_close(db);
        return false;
    }
    SqliteCloser closer{db};

    // 5. Validar tabelas do Lightroom Classic
    if (!tabelaExiste(db, "Adobe_images") ||
        !tabelaExiste(db, "AgLibraryFile") ||
        !tabelaExiste(db, "AgLibraryFolder") ||
        !tabelaExiste(db, "AgLibraryRootFolder")) {
        outErro = "O arquivo selecionado não é um catálogo reconhecido do Lightroom Classic.";
        return false;
    }

    // 6. Coletar arquivos de sessão associados (mesma pasta, ignorando .lrdata e fotos)
    outArquivosSessao.push_back(lrcatArquivo);
    auto pastaCatalogo = lrcatArquivo.getParentDirectory();
    for (const auto& arq : pastaCatalogo.findChildFiles(juce::File::findFiles, false)) {
        if (arq == lrcatArquivo) continue;
        juce::String ext = arq.getFileExtension().trimCharactersAtStart(".").toLowerCase();
        if (ext == "lrcat-journal" || ext == "lrcat-wal" || ext == "lrcat-shm" ||
            ext == "lrtemplate" || ext == "lrsmcol") {
            outArquivosSessao.push_back(arq);
        }
    }

    // 7. Tabelas opcionais
    bool temIptc = tabelaExiste(db, "AgLibraryIPTC");
    bool temHarvestedIptc = tabelaExiste(db, "AgHarvestedIptcMetadata");
    bool temInternedCreator = tabelaExiste(db, "AgInternedIptcCreator");
    bool temExif = tabelaExiste(db, "AgHarvestedExifMetadata");
    bool temKeywords = tabelaExiste(db, "AgLibraryKeywordImage") && tabelaExiste(db, "AgLibraryKeyword");
    bool temCollections = tabelaExiste(db, "AgLibraryCollectionImage") && tabelaExiste(db, "AgLibraryCollection");

    // 8. Consultar imagens
    const char* sqlImagens =
        "SELECT "
        "  img.id_local, "
        "  COALESCE(rf.absolutePath, ''), "
        "  COALESCE(fo.pathFromRoot, ''), "
        "  COALESCE(fi.baseName, ''), "
        "  COALESCE(fi.extension, ''), "
        "  COALESCE(fi.sidecarExtensions, ''), "
        "  COALESCE(img.captureTime, ''), "
        "  COALESCE(img.rating, 0), "
        "  COALESCE(img.colorLabels, ''), "
        "  COALESCE(img.pick, 0), "
        "  img.masterImage, "
        "  COALESCE(img.touchCount, 0) "
        "FROM Adobe_images img "
        "JOIN AgLibraryFile fi ON fi.id_local = img.rootFile "
        "JOIN AgLibraryFolder fo ON fo.id_local = fi.folder "
        "JOIN AgLibraryRootFolder rf ON rf.id_local = fo.rootFolder;";

    sqlite3_stmt* stmtImg = nullptr;
    if (sqlite3_prepare_v2(db, sqlImagens, -1, &stmtImg, nullptr) != SQLITE_OK) {
        outErro = "Falha ao consultar imagens no catálogo.";
        return false;
    }
    StmtFinalizer finImg{stmtImg};

    while (sqlite3_step(stmtImg) == SQLITE_ROW) {
        LightroomFotoMetadados foto;
        foto.localId = sqlite3_column_int64(stmtImg, 0);
        juce::String rootAbs = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(stmtImg, 1)));
        juce::String pathFromRoot = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(stmtImg, 2)));
        juce::String baseName = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(stmtImg, 3)));
        juce::String ext = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(stmtImg, 4)));
        juce::String sidecars = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(stmtImg, 5)));

        foto.dataCaptura = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(stmtImg, 6)));
        foto.rating = sqlite3_column_int(stmtImg, 7);
        foto.colorLabel = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(stmtImg, 8))).trim();
        foto.pick = sqlite3_column_int(stmtImg, 9);
        foto.copiaVirtual = (sqlite3_column_type(stmtImg, 10) != SQLITE_NULL);
        foto.editada = (sqlite3_column_int(stmtImg, 11) > 0);

        // Montar caminho absoluto do arquivo
        juce::String caminhoCompleto = rootAbs;
        if (!caminhoCompleto.endsWithChar('/') && !caminhoCompleto.endsWithChar('\\') &&
            !pathFromRoot.startsWithChar('/') && !pathFromRoot.startsWithChar('\\')) {
            caminhoCompleto << "/";
        }
        caminhoCompleto << pathFromRoot;
        if (!caminhoCompleto.endsWithChar('/') && !caminhoCompleto.endsWithChar('\\')) {
            caminhoCompleto << "/";
        }
        caminhoCompleto << baseName << "." << ext;

        foto.arquivoFoto = juce::File(caminhoCompleto);
        foto.existe = foto.arquivoFoto.existsAsFile();

        // Verificar arquivo acompanhante .xmp
        juce::File arqXmp = foto.arquivoFoto.withFileExtension("xmp");
        if (arqXmp.existsAsFile()) {
            foto.arquivoXmpAcompanhante = arqXmp;
        } else if (sidecars.containsIgnoreCase("xmp")) {
            foto.arquivoXmpAcompanhante = arqXmp;
        }

        // 9. IPTC Metadata
        if (temIptc) {
            const char* sqlIptc = "SELECT COALESCE(caption, ''), COALESCE(copyright, ''), COALESCE(title, '') FROM AgLibraryIPTC WHERE image=? LIMIT 1;";
            sqlite3_stmt* sIptc = nullptr;
            if (sqlite3_prepare_v2(db, sqlIptc, -1, &sIptc, nullptr) == SQLITE_OK) {
                StmtFinalizer finIptc{sIptc};
                sqlite3_bind_int64(sIptc, 1, foto.localId);
                if (sqlite3_step(sIptc) == SQLITE_ROW) {
                    foto.descricao = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(sIptc, 0))).trim();
                    foto.direitosAutorais = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(sIptc, 1))).trim();
                    foto.titulo = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(sIptc, 2))).trim();
                }
            }
        }

        if (temHarvestedIptc && temInternedCreator) {
            const char* sqlCreator =
                "SELECT c.value FROM AgHarvestedIptcMetadata iptc "
                "JOIN AgInternedIptcCreator c ON c.id_local = iptc.creatorRef "
                "WHERE iptc.image=? LIMIT 1;";
            sqlite3_stmt* sCreator = nullptr;
            if (sqlite3_prepare_v2(db, sqlCreator, -1, &sCreator, nullptr) == SQLITE_OK) {
                StmtFinalizer finCreator{sCreator};
                sqlite3_bind_int64(sCreator, 1, foto.localId);
                if (sqlite3_step(sCreator) == SQLITE_ROW) {
                    foto.criador = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(sCreator, 0))).trim();
                }
            }
        }

        // 10. EXIF Metadata
        if (temExif) {
            const char* sqlExif =
                "SELECT e.focalLength, e.aperture, e.shutterSpeed, e.isoSpeedRating, e.hasGPS, "
                "       e.gpsLatitude, e.gpsLongitude, e.gpsAltitude, "
                "       cam.value, lens.value "
                "FROM AgHarvestedExifMetadata e "
                "LEFT JOIN AgInternedExifCameraModel cam ON cam.id_local = e.cameraModelRef "
                "LEFT JOIN AgInternedExifLens lens ON lens.id_local = e.lensRef "
                "WHERE e.image=? LIMIT 1;";
            sqlite3_stmt* sExif = nullptr;
            if (sqlite3_prepare_v2(db, sqlExif, -1, &sExif, nullptr) == SQLITE_OK) {
                StmtFinalizer finExif{sExif};
                sqlite3_bind_int64(sExif, 1, foto.localId);
                if (sqlite3_step(sExif) == SQLITE_ROW) {
                    if (sqlite3_column_type(sExif, 0) != SQLITE_NULL) {
                        double fl = sqlite3_column_double(sExif, 0);
                        if (fl > 0.0) foto.distanciaFocal = juce::String(fl, 1) + "mm";
                    }
                    if (sqlite3_column_type(sExif, 1) != SQLITE_NULL) {
                        foto.abertura = formatarAbertura(sqlite3_column_double(sExif, 1));
                    }
                    if (sqlite3_column_type(sExif, 2) != SQLITE_NULL) {
                        foto.velocidade = formatarVelocidade(sqlite3_column_double(sExif, 2));
                    }
                    if (sqlite3_column_type(sExif, 3) != SQLITE_NULL) {
                        foto.iso = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(sExif, 3))).trim();
                    }
                    int hasGps = sqlite3_column_int(sExif, 4);
                    if (hasGps != 0) {
                        if (sqlite3_column_type(sExif, 5) != SQLITE_NULL) {
                            double lat = sqlite3_column_double(sExif, 5);
                            if (lat >= -90.0 && lat <= 90.0) foto.latitude = lat;
                        }
                        if (sqlite3_column_type(sExif, 6) != SQLITE_NULL) {
                            double lng = sqlite3_column_double(sExif, 6);
                            if (lng >= -180.0 && lng <= 180.0) foto.longitude = lng;
                        }
                        if (sqlite3_column_type(sExif, 7) != SQLITE_NULL) {
                            foto.altitude = sqlite3_column_double(sExif, 7);
                        }
                    }
                    if (sqlite3_column_type(sExif, 8) != SQLITE_NULL) {
                        foto.camera = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(sExif, 8))).trim();
                    }
                    if (sqlite3_column_type(sExif, 9) != SQLITE_NULL) {
                        foto.lente = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(sExif, 9))).trim();
                    }
                }
            }
        }

        // 11. Palavras-chave (Keywords)
        if (temKeywords) {
            const char* sqlKw =
                "SELECT k.name FROM AgLibraryKeywordImage ki "
                "JOIN AgLibraryKeyword k ON k.id_local = ki.tag "
                "WHERE ki.image=?;";
            sqlite3_stmt* sKw = nullptr;
            if (sqlite3_prepare_v2(db, sqlKw, -1, &sKw, nullptr) == SQLITE_OK) {
                StmtFinalizer finKw{sKw};
                sqlite3_bind_int64(sKw, 1, foto.localId);
                while (sqlite3_step(sKw) == SQLITE_ROW) {
                    juce::String kw = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(sKw, 0))).trim();
                    if (kw.isNotEmpty()) foto.palavrasChave.add(kw);
                }
            }
        }

        // 12. Coleções (Collections)
        if (temCollections) {
            const char* sqlCol =
                "SELECT c.name FROM AgLibraryCollectionImage ci "
                "JOIN AgLibraryCollection c ON c.id_local = ci.collection "
                "WHERE ci.image=?;";
            sqlite3_stmt* sCol = nullptr;
            if (sqlite3_prepare_v2(db, sqlCol, -1, &sCol, nullptr) == SQLITE_OK) {
                StmtFinalizer finCol{sCol};
                sqlite3_bind_int64(sCol, 1, foto.localId);
                while (sqlite3_step(sCol) == SQLITE_ROW) {
                    juce::String colName = juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(sCol, 0))).trim();
                    if (colName.isNotEmpty()) foto.colecoes.add(colName);
                }
            }
        }

        outFotos.push_back(std::move(foto));
    }

    return true;
}

LightroomImportResultado LightroomImporter::importarCatalogo(
    const juce::File& lrcatArquivo,
    matriz::db::Database& registro,
    matriz::db::Database& indice,
    const juce::File& pastaProjeto,
    const std::string& projetoId,
    const juce::String& prefixoAcervo,
    std::shared_ptr<matriz::app::Cancelamento> cancelamento,
    std::function<void(int atual, int total, const juce::String& status)> onProgresso,
    std::function<juce::File(const juce::String& exemploEsperado, const juce::String& nomeFoto)> onPedirNovaRaiz) {

    LightroomImportResultado res;
    matriz::model::ProjectLog pLog(pastaProjeto);

    std::vector<LightroomFotoMetadados> fotos;
    std::vector<juce::File> arquivosSessao;
    bool estavaBloqueado = false;
    juce::String erro;

    if (onProgresso) onProgresso(0, 100, "Lendo catálogo do Lightroom...");

    if (!lerCatalogo(lrcatArquivo, fotos, arquivosSessao, estavaBloqueado, erro)) {
        res.erro = erro;
        pLog.appendEntry("Lightroom Import Failed", {erro});
        return res;
    }
    res.catalogoEstavaBloqueado = estavaBloqueado;

    // Verificar se a pasta .lrdata existe e registrar no log
    juce::File lrdataDir = lrcatArquivo.getSiblingFile(lrcatArquivo.getFileNameWithoutExtension() + " Previews.lrdata");
    if (!lrdataDir.isDirectory()) {
        lrdataDir = lrcatArquivo.getSiblingFile(lrcatArquivo.getFileName() + " Previews.lrdata");
    }
    if (lrdataDir.isDirectory()) {
        res.detalhesLog.add("Pasta de miniaturas (.lrdata) detectada e ignorada: " + lrdataDir.getFileName());
    }

    // 1. Relink de arquivos não encontrados
    std::vector<size_t> indicesAusentes;
    for (size_t i = 0; i < fotos.size(); ++i) {
        if (!fotos[i].existe) {
            indicesAusentes.push_back(i);
        }
    }

    if (!indicesAusentes.empty() && onPedirNovaRaiz) {
        auto& fotoExemplo = fotos[indicesAusentes.front()];
        juce::File novaRaiz = onPedirNovaRaiz(fotoExemplo.arquivoFoto.getFullPathName(), fotoExemplo.arquivoFoto.getFileName());
        if (novaRaiz.isDirectory()) {
            juce::String oldRoot, newRoot;
            if (matriz::vault::AssetRelinkEngine::inferirNovaRaiz(
                    fotoExemplo.arquivoFoto.getFullPathName(),
                    novaRaiz.getChildFile(fotoExemplo.arquivoFoto.getFileName()).getFullPathName(),
                    oldRoot, newRoot)) {
                for (size_t idx : indicesAusentes) {
                    auto& f = fotos[idx];
                    juce::String path = f.arquivoFoto.getFullPathName();
                    if (path.startsWith(oldRoot)) {
                        juce::String rebased = newRoot + path.substring(oldRoot.length());
                        juce::File rebasedFile(rebased);
                        if (rebasedFile.existsAsFile()) {
                            f.arquivoFoto = rebasedFile;
                            f.existe = true;
                            // Rebase companion .xmp if present
                            if (f.arquivoXmpAcompanhante != juce::File{}) {
                                f.arquivoXmpAcompanhante = rebasedFile.withFileExtension("xmp");
                            }
                        }
                    }
                }
            }
        }
    }

    // Contabilizar fotos encontradas vs ausentes
    std::vector<LightroomFotoMetadados*> fotosParaIngerir;
    for (auto& f : fotos) {
        if (f.existe) {
            fotosParaIngerir.push_back(&f);
        } else {
            res.fotosNaoEncontradas++;
            res.detalhesLog.add("Arquivo ausente não encontrado: " + f.arquivoFoto.getFullPathName());
        }
    }

    int totalTarefas = static_cast<int>(fotosParaIngerir.size() + arquivosSessao.size());
    int feitos = 0;

    // D1: número sequencial do acervo lido UMA vez pra esta importação (era
    // um SELECT MAX(...) por foto/arquivo de sessão, sem índice em
    // codigo_acervo — custo O(n²) no catálogo inteiro). As duas seções
    // abaixo (fotos e arquivos de sessão) usam o mesmo prefixoAcervo e
    // rodam em sequência nesta função, então um único contador cobre as duas.
    int proximoNumeroAcervo = 0;
    {
        auto stmtMax = registro.prepare(
            "SELECT COALESCE(MAX(CAST(SUBSTR(codigo_acervo, INSTR(codigo_acervo, '-') + 1) AS INTEGER)), 0) "
            "FROM item WHERE codigo_acervo LIKE ?");
        stmtMax.bind(1, Value::of(prefixoAcervo.toStdString() + "-%"));
        if (stmtMax.step()) proximoNumeroAcervo = static_cast<int>(stmtMax.columnInt(0));
    }

    // 2. Ingerir Fotos
    for (auto* fotoPtr : fotosParaIngerir) {
        if (cancelamento && cancelamento->pedido()) break;

        const auto& foto = *fotoPtr;
        if (onProgresso) onProgresso(++feitos, totalTarefas, "Ingerindo: " + foto.arquivoFoto.getFileName());

        try {
            auto analise = matriz::ingest::analisarArquivo(foto.arquivoFoto);
            auto categoria = matriz::ingest::categoriaPorExtensao(foto.arquivoFoto);

            matriz::ingest::AnaliseCache cache;
            if (!analise.ehPlaceholderNuvem) {
                cache = matriz::ingest::calcularCache(foto.arquivoFoto, categoria, pastaProjeto, analise.leitura.duracaoSegundos);
            }

            std::string itemId = matriz::model::novoUuid();
            std::string agora = matriz::model::agoraIso8601();
            std::string tipoMidia = "foto";
            std::string estado = (foto.pick == 1) ? "aprovado" : "novo";

            std::string tituloFinal = foto.titulo.isNotEmpty() ? foto.titulo.toStdString() : foto.arquivoFoto.getFileNameWithoutExtension().toStdString();

            registro.run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, em_quarentena, criado_em, atualizado_em) "
                "VALUES (?, ?, NULL, ?, ?, ?, 0, ?, ?)",
                {Value::of(itemId), Value::of(projetoId), Value::of(tituloFinal),
                 Value::of(tipoMidia), Value::of(estado), Value::of(agora), Value::of(agora)});

            // Gerar código de acervo (D1: contador cacheado, ver acima)
            int proximoNumero = ++proximoNumeroAcervo;
            juce::String codigo = prefixoAcervo + "-" + juce::String(proximoNumero).paddedLeft('0', 5);

            registro.run("UPDATE item SET codigo_acervo = ? WHERE id = ?",
                         {Value::of(codigo.toStdString()), Value::of(itemId)});

            // Gravar arquivo analisado
            auto resArq = matriz::ingest::gravarArquivoAnalisado(registro, itemId, analise, "imagem_principal", true);
            if (!resArq.arquivoId.empty()) {
                matriz::ingest::gravarCache(registro, resArq.arquivoId, cache);
                matriz::ingest::gerarEGravarMiniaturaPrincipal(indice, pastaProjeto, itemId, resArq.arquivoId, foto.arquivoFoto, categoria, analise.leitura.duracaoSegundos);
            }

            // Gravação de metadados no registro.sqlite com regra de não-sobrescrita
            auto gravarCampoSeVazio = [&](const std::string& campoId, const juce::String& valor, const std::string& fonte = "humano") {
                if (valor.isEmpty()) return;
                auto sCheck = registro.prepare(
                    "SELECT valor FROM item_campo WHERE item_id = ? AND nivel = 'raiz' AND nivel_indice = 0 AND campo_id = ?");
                sCheck.bind(1, Value::of(itemId));
                sCheck.bind(2, Value::of(campoId));
                if (sCheck.step() && !sCheck.columnIsNull(0) && !sCheck.columnText(0).empty()) {
                    res.detalhesLog.add("Campo " + juce::String(campoId) + " já preenchido em " + codigo + "; preservado.");
                    return;
                }
                registro.run(
                    "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                    "VALUES (?, ?, 'raiz', 0, ?, ?, ?, ?) "
                    "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor=excluded.valor, atualizado_em=excluded.atualizado_em",
                    {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(campoId),
                     Value::of(valor.toStdString()), Value::of(fonte), Value::of(agora)});
                res.metadadosAplicados++;
            };

            // Dublin Core
            if (foto.titulo.isNotEmpty()) gravarCampoSeVazio("dc_title", foto.titulo);
            if (foto.descricao.isNotEmpty()) {
                gravarCampoSeVazio("dc_description", foto.descricao);
                gravarCampoSeVazio("descricao", foto.descricao);
            }
            if (foto.criador.isNotEmpty()) {
                gravarCampoSeVazio("dc_creator", foto.criador);
                gravarCampoSeVazio("artista_principal", foto.criador);
            }
            if (foto.direitosAutorais.isNotEmpty()) gravarCampoSeVazio("dc_rights", foto.direitosAutorais);
            if (foto.palavrasChave.size() > 0) {
                gravarCampoSeVazio("dc_subject", foto.palavrasChave.joinIntoString("; "));
                for (const auto& tag : foto.palavrasChave) {
                    registro.run("INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                                 {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(tag.toStdString())});
                }
            }

            // Data
            if (foto.dataCaptura.isNotEmpty()) {
                juce::String dt = foto.dataCaptura;
                if (dt.length() >= 10) {
                    juce::String dParte = dt.substring(0, 10).replace(":", "-");
                    juce::String hParte = dt.length() > 10 ? dt.substring(10) : "";
                    dt = dParte + hParte;
                }
                gravarCampoSeVazio("dc_created", dt);
                gravarCampoSeVazio("data_foto", dt);
                gravarCampoSeVazio("data_criacao", dt);
                if (dt.length() >= 4) {
                    registro.run("UPDATE item SET ano = ? WHERE id = ? AND (ano IS NULL OR ano = '')",
                                 {Value::of(dt.substring(0, 4).toStdString()), Value::of(itemId)});
                }
            }

            // Geolocalização
            if (foto.latitude.has_value() && foto.longitude.has_value()) {
                matriz::analytics::AssetGeolocation geo;
                geo.assetId = itemId;
                geo.latitude = *foto.latitude;
                geo.longitude = *foto.longitude;
                if (foto.altitude.has_value()) geo.altitude = *foto.altitude;
                geo.source = matriz::analytics::GeoSource::Imported;
                matriz::analytics::AssetGeolocationRepository::salvar(registro, geo);
                res.metadadosAplicados++;
            }

            // User Asset (Rating, Color Label, Pick, Collection)
            if (foto.rating > 0) {
                gravarCampoSeVazio("user_rating", juce::String(foto.rating));
                gravarCampoSeVazio("rating", juce::String(foto.rating));
            }
            if (foto.colorLabel.isNotEmpty()) {
                gravarCampoSeVazio("color_label", foto.colorLabel);
            }
            if (foto.pick != 0) {
                gravarCampoSeVazio("pick_status", foto.pick == 1 ? "pick" : "rejected");
            }
            if (foto.colecoes.size() > 0) {
                juce::String colecaoStr = foto.colecoes.joinIntoString("; ");
                gravarCampoSeVazio("collection_type", colecaoStr);
                registro.run("UPDATE item SET collection_type = ? WHERE id = ? AND (collection_type IS NULL OR collection_type = '')",
                             {Value::of(colecaoStr.toStdString()), Value::of(itemId)});
            }

            // Notas técnicas (câmera, lente, abertura, etc.) acrescentadas ao final
            juce::String notasAdicionais = formatarNotasTecnicas(foto);
            if (notasAdicionais.isNotEmpty()) {
                auto sNotas = registro.prepare("SELECT notas_livres FROM item WHERE id = ?");
                sNotas.bind(1, Value::of(itemId));
                juce::String notasAtuais;
                if (sNotas.step() && !sNotas.columnIsNull(0)) {
                    notasAtuais = juce::String::fromUTF8(sNotas.columnText(0).c_str());
                }
                juce::String notasCombinadas = notasAtuais;
                if (notasCombinadas.isNotEmpty()) notasCombinadas << "\n\n";
                notasCombinadas << "[Lightroom] " << notasAdicionais;
                registro.run("UPDATE item SET notas_livres = ? WHERE id = ?",
                             {Value::of(notasCombinadas.toStdString()), Value::of(itemId)});
                res.metadadosAplicados++;
            }

            // Embutir metadados nas CÓPIAS IMPORTADAS do projeto (nunca nos originais do Lightroom!)
            // Se o arquivo estiver dentro da pasta do projeto (ou cópia de preservação), embutir via Exiv2
            if (foto.arquivoFoto.isAChildOf(pastaProjeto)) {
                matriz::consolidacao::MetadadoParaEmbutir metaParaCopia;
                metaParaCopia.titulo = tituloFinal;
                metaParaCopia.descricao = foto.descricao.isNotEmpty() ? foto.descricao.toStdString() : notasAdicionais.toStdString();
                metaParaCopia.artista = foto.criador.toStdString();
                metaParaCopia.codigoAcervo = codigo.toStdString();
                metaParaCopia.tipoMidia = "foto";
                if (foto.dataCaptura.length() >= 4) {
                    metaParaCopia.ano = foto.dataCaptura.substring(0, 4).getIntValue();
                }
                matriz::consolidacao::embutirMetadadosNoArquivo(foto.arquivoFoto, metaParaCopia);
            }

            // Se houver arquivo acompanhante .xmp ao lado da foto e a foto estiver na pasta do projeto,
            // copiar o .xmp junto
            if (foto.arquivoXmpAcompanhante.existsAsFile() && foto.arquivoFoto.isAChildOf(pastaProjeto)) {
                juce::File dstXmp = foto.arquivoFoto.withFileExtension("xmp");
                if (dstXmp != foto.arquivoXmpAcompanhante && !dstXmp.existsAsFile()) {
                    foto.arquivoXmpAcompanhante.copyFileTo(dstXmp);
                }
            }

            res.fotosImportadas++;
        } catch (const std::exception& e) {
            res.detalhesLog.add("Erro ao importar foto " + foto.arquivoFoto.getFileName() + ": " + juce::String(e.what()));
        }
    }

    // 3. Ingerir Arquivos de Sessão (.lrcat, lrcat-wal, lrtemplate, etc.)
    juce::File pastaMedia = pastaProjeto.getParentDirectory().getChildFile("Media");
    juce::File pastaSessoes = pastaMedia.isDirectory() ? pastaMedia.getChildFile("Sessões") : pastaProjeto.getChildFile("Sessões");
    pastaSessoes.createDirectory();
    juce::File pastaSessaoLr = pastaSessoes.getChildFile(lrcatArquivo.getFileNameWithoutExtension());
    pastaSessaoLr.createDirectory();

    for (const auto& arqSessao : arquivosSessao) {
        if (cancelamento && cancelamento->pedido()) break;
        if (!arqSessao.existsAsFile()) continue;

        if (onProgresso) onProgresso(++feitos, totalTarefas, "Ingerindo arquivo de sessão: " + arqSessao.getFileName());

        try {
            juce::File arqDestino = pastaSessaoLr.getChildFile(arqSessao.getFileName());
            if (arqSessao != arqDestino && !arqDestino.existsAsFile()) {
                arqSessao.copyFileTo(arqDestino);
            }
            auto arquivoParaIngerir = arqDestino.existsAsFile() ? arqDestino : arqSessao;

            auto analise = matriz::ingest::analisarArquivo(arquivoParaIngerir);
            std::string itemId = matriz::model::novoUuid();
            std::string agora = matriz::model::agoraIso8601();

            registro.run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, em_quarentena, criado_em, atualizado_em) "
                "VALUES (?, ?, NULL, ?, 'sessao', 'novo', 0, ?, ?)",
                {Value::of(itemId), Value::of(projetoId),
                 Value::of(arqSessao.getFileNameWithoutExtension().toStdString()),
                 Value::of(agora), Value::of(agora)});

            // Gerar código de acervo (D1: contador cacheado, ver acima)
            int proximoNumero = ++proximoNumeroAcervo;
            juce::String codigo = prefixoAcervo + "-" + juce::String(proximoNumero).paddedLeft('0', 5);

            registro.run("UPDATE item SET codigo_acervo = ? WHERE id = ?",
                         {Value::of(codigo.toStdString()), Value::of(itemId)});

            matriz::ingest::gravarArquivoAnalisado(registro, itemId, analise, "sessao_projeto", true);
            res.arquivosSessaoImportados++;
        } catch (const std::exception& e) {
            res.detalhesLog.add("Erro ao importar arquivo de sessão " + arqSessao.getFileName() + ": " + juce::String(e.what()));
        }
    }

    // Registrar resumo no ProjectLog
    juce::StringArray logDetails;
    logDetails.add("Catalog: " + lrcatArquivo.getFullPathName());
    logDetails.add("Photos imported: " + juce::String(res.fotosImportadas));
    logDetails.add("Missing photos: " + juce::String(res.fotosNaoEncontradas));
    logDetails.add("Metadata records applied: " + juce::String(res.metadadosAplicados));
    logDetails.add("Session files imported: " + juce::String(res.arquivosSessaoImportados));
    for (const auto& d : res.detalhesLog) logDetails.add("Detail: " + d);

    pLog.appendEntry("Lightroom Classic Catalog Ingested", logDetails);

    res.sucesso = res.erro.isEmpty();
    return res;
}

} // namespace matriz::ingest
