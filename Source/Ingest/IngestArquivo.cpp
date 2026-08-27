#include "IngestArquivo.h"
#include "../Analytics/AssetGeolocation.h"

#include <sys/stat.h>

#include "../Model/Project.h"
#include "../Preservation/Preservation.h"
#include "../Vault/Volume.h"
#include "../Vault/Resolucao.h"
#include "LeituraTecnica.h"

namespace matriz::ingest {

using matriz::db::Value;

namespace {

juce::var construirCaracteristicasJson(const LeituraTecnicaResultado& leitura) {
    auto obj = std::make_unique<juce::DynamicObject>();
    if (leitura.duracaoSegundos) obj->setProperty("duracaoSegundos", *leitura.duracaoSegundos);
    if (leitura.sampleRate) obj->setProperty("sampleRate", *leitura.sampleRate);
    if (leitura.bitDepth) obj->setProperty("bitDepth", *leitura.bitDepth);
    if (leitura.canais) obj->setProperty("canais", *leitura.canais);
    if (!leitura.codec.empty()) obj->setProperty("codec", juce::String(leitura.codec));
    if (leitura.larguraPx) obj->setProperty("larguraPx", *leitura.larguraPx);
    if (leitura.alturaPx) obj->setProperty("alturaPx", *leitura.alturaPx);
    if (leitura.fps) obj->setProperty("fps", *leitura.fps);
    if (leitura.lufsIntegrado) obj->setProperty("lufsIntegrado", *leitura.lufsIntegrado);
    if (leitura.lra) obj->setProperty("lra", *leitura.lra);
    if (leitura.picoDbfs) obj->setProperty("picoDbfs", *leitura.picoDbfs);
    obj->setProperty("codecLossyDeclarado", leitura.codecLossyDeclarado);
    obj->setProperty("bruto", leitura.bruto);
    return juce::var(obj.release());
}

} // namespace

std::string obterOuCriarVaultParaArquivo(matriz::db::Database& registro, const juce::File& arquivo, const std::string& projetoId) {
    auto volume = matriz::vault::descreverVolume(arquivo);
    std::string caminhoMontagem = volume.pontoMontagem.getFullPathName().toStdString();

    // O UUID é a chave forte (sobrevive a remontagem em outro caminho, §8);
    // o ponto de montagem é o fallback pra volumes que não expõem UUID. Sem
    // o guard de `uuid_volume <> ''` um volume sem UUID casaria com qualquer
    // outro volume sem UUID.
    auto stmt = registro.prepare(
        "SELECT id FROM vault WHERE (uuid_volume <> '' AND uuid_volume = ?) OR localizacao = ? LIMIT 1");
    stmt.bind(1, Value::of(volume.uuid));
    stmt.bind(2, Value::of(caminhoMontagem));
    if (stmt.step()) {
        return stmt.columnText(0);
    }

    std::string vaultId = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();
    registro.run(
        "INSERT INTO vault (id, projeto_id, nome, tipo, uuid_volume, raiz_relativa, localizacao, status, visto_em, criado_em) "
        "VALUES (?, ?, ?, 'local', ?, '', ?, 'online', ?, ?)",
        {Value::of(vaultId), Value::of(projetoId), Value::of(volume.nome),
         Value::of(volume.uuid), Value::of(caminhoMontagem), Value::of(agora), Value::of(agora)});
    return vaultId;
}

bool verificarSePlaceholderNuvem(const juce::File& f) {
    struct stat st;
    if (::stat(f.getFullPathName().toRawUTF8(), &st) == 0) {
        if (st.st_blocks == 0 && st.st_size > 0) {
            return true;
        }
    }
    return false;
}

AnaliseDeArquivo analisarArquivo(const juce::File& arquivoOrigem) {
    if (!arquivoOrigem.existsAsFile())
        throw IngestArquivoError("source file not found: " + arquivoOrigem.getFullPathName().toStdString());

    AnaliseDeArquivo a;
    a.arquivo = arquivoOrigem;
    a.ehPlaceholderNuvem = verificarSePlaceholderNuvem(arquivoOrigem);

    if (!a.ehPlaceholderNuvem) {
        a.checksums = calcularChecksums(arquivoOrigem);
        a.leitura = lerTecnica(arquivoOrigem);
    } else {
        // Placeholder de nuvem: registra o que se sabe SEM puxar os bytes
        // (critério 12). Ler o arquivo aqui dispararia o download do acervo
        // inteiro só por ter aberto o programa.
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("fileSizeBytes", static_cast<juce::int64>(arquivoOrigem.getSize()));
        a.leitura.bruto = juce::var(obj.release());
    }
    return a;
}

ResultadoIngestArquivo gravarArquivoAnalisado(matriz::db::Database& registro, const std::string& itemId,
                                               const AnaliseDeArquivo& analise, const std::string& papel,
                                               bool ehMaster) {
    std::string projetoId;
    auto stmtProj = registro.prepare("SELECT id FROM projeto LIMIT 1");
    if (stmtProj.step()) projetoId = stmtProj.columnText(0);

    std::string vaultId = obterOuCriarVaultParaArquivo(registro, analise.arquivo, projetoId);

    std::string caminhoRelativo = matriz::vault::caminhoRelativoAoVolume(analise.arquivo);

    ResultadoIngestArquivo resultado;
    resultado.arquivoId = matriz::model::novoUuid();
    resultado.arquivoNoProjeto = analise.arquivo;  // In-place (I5): a referência é o original
    resultado.checksums = analise.checksums;
    resultado.leitura = analise.leitura;

    std::string estadoPresenca = analise.ehPlaceholderNuvem ? "nao_baixado" : "presente";
    std::string caracteristicasJson =
        juce::JSON::toString(construirCaracteristicasJson(analise.leitura), true).toStdString();
    std::string agora = matriz::model::agoraIso8601();

    registro.run(
        "INSERT INTO arquivo (id, item_id, vault_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, "
        "checksum_md5, checksum_sha256, checksum_gerado_em, caracteristicas_tecnicas_json, estado_presenca, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {Value::of(resultado.arquivoId), Value::of(itemId), Value::of(vaultId),
         Value::of(caminhoRelativo),
         Value::of(analise.arquivo.getFullPathName().toStdString()), Value::of(papel), Value::of(ehMaster),
         Value::of(static_cast<long long>(analise.arquivo.getSize())),
         analise.ehPlaceholderNuvem ? Value::null() : Value::of(analise.checksums.md5),
         analise.ehPlaceholderNuvem ? Value::null() : Value::of(analise.checksums.sha256),
         analise.ehPlaceholderNuvem ? Value::null() : Value::of(agora),
         Value::of(caracteristicasJson), Value::of(estadoPresenca), Value::of(agora), Value::of(agora)});

    // -----------------------------------------------------------------------
    // Preservation events — PREMIS hooks pós-ingest.
    // Erros são silenciados: falha aqui nunca deve bloquear o ingest.
    // -----------------------------------------------------------------------
    try {
        // Garante persistent_id (idempotente — retorna existente sem alterar)
        preservation::garantirPersistentId(registro, itemId);

        // Evento INGEST
        preservation::registrarEvento(
            registro, itemId, resultado.arquivoId,
            preservation::EventType::Ingest,
            "File ingested: " + analise.arquivo.getFileName().toStdString(),
            preservation::Outcome::Success, {},
            "bkr-agent-sistema");

        // FIXITY_CALCULATED — apenas quando checksum foi de fato calculado
        if (!analise.ehPlaceholderNuvem && !analise.checksums.sha256.empty()) {
            preservation::registrarEvento(
                registro, itemId, resultado.arquivoId,
                preservation::EventType::FixityCalculated,
                "SHA-256: " + analise.checksums.sha256,
                preservation::Outcome::Success, {},
                "bkr-agent-sistema");
        }

        // FORMAT_IDENTIFIED — quando ffprobe retornou dados (codec ou container)
        bool temFormatoTecnico = !analise.leitura.codec.empty()
                              || analise.leitura.duracaoSegundos.has_value()
                              || analise.leitura.sampleRate.has_value()
                              || analise.leitura.larguraPx.has_value();
        if (!analise.ehPlaceholderNuvem && temFormatoTecnico) {
            std::string detail = "codec: " + analise.leitura.codec;
            if (analise.leitura.sampleRate)
                detail += " | sampleRate: " + std::to_string(*analise.leitura.sampleRate);
            if (analise.leitura.larguraPx)
                detail += " | " + std::to_string(*analise.leitura.larguraPx)
                        + "x" + std::to_string(analise.leitura.alturaPx.value_or(0));
            preservation::registrarEvento(
                registro, itemId, resultado.arquivoId,
                preservation::EventType::FormatIdentified,
                detail, preservation::Outcome::Success, {},
                "bkr-agent-ffprobe");
        }

        // METADATA_EXTRACTED — quando Exiv2 retornou dados EXIF
        if (!analise.ehPlaceholderNuvem && analise.leitura.exifCamera.has_value()) {
            std::string detail = "EXIF extracted";
            if (analise.leitura.exifCamera)
                detail += " | camera: " + *analise.leitura.exifCamera;
            if (analise.leitura.exifDataOriginal)
                detail += " | date: " + *analise.leitura.exifDataOriginal;
            preservation::registrarEvento(
                registro, itemId, resultado.arquivoId,
                preservation::EventType::MetadataExtracted,
                detail, preservation::Outcome::Success, {},
                "bkr-agent-exiv2");
        }

        // GEOLOCATION_EXTRACTED — quando Exiv2 retornou coordenadas GPS válidas
        if (!analise.ehPlaceholderNuvem && analise.leitura.exifGpsLatitude.has_value() && analise.leitura.exifGpsLongitude.has_value()) {
            double lat = *analise.leitura.exifGpsLatitude;
            double lng = *analise.leitura.exifGpsLongitude;
            if (lat >= -90.0 && lat <= 90.0 && lng >= -180.0 && lng <= 180.0) {
                matriz::analytics::AssetGeolocation geo;
                geo.assetId = itemId;
                geo.latitude = lat;
                geo.longitude = lng;
                geo.source = matriz::analytics::GeoSource::EmbeddedMetadata;
                matriz::analytics::AssetGeolocationRepository::salvar(registro, geo);
            }
        }
    } catch (...) {
        // Preservation hooks são best-effort — ingest não pode falhar por eles
    }

    return resultado;
}

ResultadoIngestArquivo ingerirArquivo(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                      const std::string& itemId, const juce::File& arquivoOrigem,
                                      const std::string& papel, bool ehMaster) {
    juce::ignoreUnused(pastaProjeto);  // I5: nada é copiado pra dentro do projeto
    return gravarArquivoAnalisado(registro, itemId, analisarArquivo(arquivoOrigem), papel, ehMaster);
}

std::optional<AssetConhecido> buscarAssetPorChecksum(matriz::db::Database& registro,
                                                      const std::string& sha256) {
    if (sha256.empty()) return std::nullopt;

    auto stmt = registro.prepare(
        "SELECT a.id, i.id, i.codigo_acervo FROM arquivo a JOIN item i ON i.id = a.item_id "
        "WHERE a.checksum_sha256 = ? LIMIT 1");
    stmt.bind(1, Value::of(sha256));
    if (!stmt.step()) return std::nullopt;

    AssetConhecido conhecido;
    conhecido.arquivoId = stmt.columnText(0);
    conhecido.itemId = stmt.columnText(1);
    conhecido.codigoAcervo = stmt.columnText(2);
    return conhecido;
}

std::optional<AssetConhecido> buscarAssetConhecido(matriz::db::Database& registro, const juce::File& arquivoOrigem) {
    if (!arquivoOrigem.existsAsFile()) return std::nullopt;

    Checksums cs = calcularChecksums(arquivoOrigem);

    auto stmt = registro.prepare(
        "SELECT a.id, i.id, i.codigo_acervo FROM arquivo a JOIN item i ON i.id = a.item_id "
        "WHERE a.checksum_sha256 = ? LIMIT 1");
    stmt.bind(1, Value::of(cs.sha256));
    if (!stmt.step()) return std::nullopt;

    AssetConhecido a;
    a.arquivoId = stmt.columnText(0);
    a.itemId = stmt.columnText(1);
    a.codigoAcervo = stmt.columnText(2);
    return a;
}

void registrarLocalizacaoConhecida(matriz::db::Database& registro, const std::string& arquivoId,
                                    const juce::File& caminhoAbsoluto) {
    registro.run(
        "INSERT OR IGNORE INTO localizacao_conhecida (id, arquivo_id, caminho_absoluto, criado_em) VALUES (?, ?, ?, ?)",
        {Value::of(matriz::model::novoUuid()), Value::of(arquivoId),
         Value::of(caminhoAbsoluto.getFullPathName().toStdString()), Value::of(matriz::model::agoraIso8601())});
}

std::optional<AssetConhecido> buscarAssetPorMetadados(matriz::db::Database& registro,
                                                       const std::string& titulo,
                                                       const std::string& ext,
                                                       double duracao,
                                                       int largura,
                                                       int altura,
                                                       juce::int64 tamanhoBytes,
                                                       const std::string& caminhoRelativo,
                                                       const std::string& excludeItemId,
                                                       const juce::File& pastaProjeto) {
    auto stmt = registro.prepare(
        "SELECT a.id, i.id, i.codigo_acervo, i.titulo, a.caminho_relativo, a.caracteristicas_tecnicas_json, a.tamanho_bytes "
        "FROM item i "
        "JOIN arquivo a ON a.item_id = i.id "
        "WHERE a.eh_master = 1 "
        "  AND (? = '' OR i.id != ?) "
        "  AND (i.notas_livres IS NULL OR i.notas_livres NOT LIKE '%[USER_VERIFIED_NOT_DUPLICATE]%') "
        "  AND ("
        "    LOWER(i.titulo) = ? "
        "    OR a.tamanho_bytes = ? "
        "    OR (? > 0.0 AND ABS(CAST(json_extract(a.caracteristicas_tecnicas_json, '$.duracaoSegundos') AS REAL) - ?) < 0.1) "
        "    OR (? > 0 AND ? > 0 AND CAST(json_extract(a.caracteristicas_tecnicas_json, '$.larguraPx') AS INTEGER) = ? AND CAST(json_extract(a.caracteristicas_tecnicas_json, '$.alturaPx') AS INTEGER) = ?)"
        "  )");

    std::string tituloLower = juce::String(titulo).toLowerCase().toStdString();
    
    double origLufs = 0.0;
    double origPeak = 0.0;
    int origSampleRate = 0;
    int origChannels = 0;
    juce::String origOrientationStr;
    juce::String origColorSpaceStr;
    if (!excludeItemId.empty()) {
        try {
            auto stmtOrig = registro.prepare(
                "SELECT a.caracteristicas_tecnicas_json FROM arquivo a WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
            stmtOrig.bind(1, Value::of(excludeItemId));
            if (stmtOrig.step()) {
                auto jsonVar = juce::JSON::parse(stmtOrig.columnText(0));
                if (auto* obj = jsonVar.getDynamicObject()) {
                    if (obj->hasProperty("lufsIntegrado")) origLufs = obj->getProperty("lufsIntegrado");
                    if (obj->hasProperty("picoDbfs")) origPeak = obj->getProperty("picoDbfs");
                    if (obj->hasProperty("sampleRate")) origSampleRate = obj->getProperty("sampleRate");
                    if (obj->hasProperty("canais")) origChannels = obj->getProperty("canais");
                    
                    if (auto* bruto = obj->getProperty("bruto").getDynamicObject()) {
                        if (auto* exif = bruto->getProperty("exif").getDynamicObject()) {
                            if (exif->hasProperty("Exif.Image.Orientation")) {
                                origOrientationStr = exif->getProperty("Exif.Image.Orientation").toString();
                            }
                            if (exif->hasProperty("Exif.Photo.ColorSpace")) {
                                origColorSpaceStr = exif->getProperty("Exif.Photo.ColorSpace").toString();
                            }
                        }
                    }
                }
            }
        } catch (...) {}
    }

    stmt.bind(1, Value::of(excludeItemId));
    stmt.bind(2, Value::of(excludeItemId));
    stmt.bind(3, Value::of(tituloLower));
    stmt.bind(4, Value::of(static_cast<long long>(tamanhoBytes)));
    stmt.bind(5, Value::of(duracao));
    stmt.bind(6, Value::of(duracao));
    stmt.bind(7, Value::of(largura));
    stmt.bind(8, Value::of(altura));
    stmt.bind(9, Value::of(largura));
    stmt.bind(10, Value::of(altura));

    while (stmt.step()) {
        int coincidences = 0;

        // Coincidência 1: Nome (case-insensitive)
        std::string candTitulo = stmt.columnText(3);
        if (juce::String(candTitulo).equalsIgnoreCase(juce::String(titulo))) {
            coincidences++;
        }

        // Coincidência 2: Extensão/Formato (case-insensitive)
        std::string candCaminho = stmt.columnText(4);
        
        // Heuristic: If they are in the same folder (same parent directory), they are excluded
        // from duplicate file matching (either they are different files in the same folder,
        // or they are the exact same physical file registered twice in the database).
        if (!caminhoRelativo.empty()) {
            juce::File fileA(caminhoRelativo);
            juce::File fileB(candCaminho);
            if (fileA.getParentDirectory().getFullPathName() == fileB.getParentDirectory().getFullPathName()) {
                continue;
            }
        }

        juce::String candExt = juce::File(candCaminho).getFileExtension().replaceCharacter('.', ' ').trim().toLowerCase();
        juce::String queryExt = juce::String(ext).toLowerCase();
        bool extMatches = (candExt == queryExt);
        if (extMatches) {
            coincidences++;
        }

        // Coincidência 3: Tamanho (bytes)
        juce::int64 candTamanho = stmt.columnInt(6);
        bool sizeMatches = (candTamanho == tamanhoBytes);
        if (sizeMatches) {
            coincidences++;
        }

        // Strict Duplicate Rule for compressed media files:
        // If format and exact size in bytes match, they are 100% duplicates (excluding uncompressed PCM wav/aif/aiff)
        if (extMatches && sizeMatches) {
            bool isCompressedMedia = (queryExt == "mp3" || queryExt == "mp4" || queryExt == "mov" || queryExt == "m4a" || queryExt == "flac" || queryExt == "mkv" || queryExt == "ogg");
            if (isCompressedMedia) {
                coincidences = 3; // Force match!
            }
        }

        // Coincidências 4 e 5: Duração e Dimensões (lidos do JSON)
        std::string jsonStr = stmt.columnText(5);
        if ((jsonStr == "{}" || jsonStr.empty()) && pastaProjeto != juce::File()) {
            std::string candArqId = stmt.columnText(0);
            auto resolvido = matriz::vault::resolverArquivo(registro, candArqId, pastaProjeto);
            if (resolvido && resolvido->existsAsFile()) {
                try {
                    auto categoria = categoriaPorExtensao(*resolvido);
                    auto analise = analisarArquivo(*resolvido);
                    juce::var varJson = construirCaracteristicasJson(analise.leitura);
                    std::string novoJson = juce::JSON::toString(varJson, true).toStdString();
                    
                    registro.run("UPDATE arquivo SET caracteristicas_tecnicas_json = ? WHERE id = ?",
                                 {matriz::db::Value::of(novoJson),
                                  matriz::db::Value::of(candArqId)});
                    jsonStr = novoJson;
                } catch (...) {}
            }
        }
        auto jsonVar = juce::JSON::parse(jsonStr);
        if (auto* obj = jsonVar.getDynamicObject()) {
            juce::String queryExt = juce::String(ext).toLowerCase();
            bool isImage = (queryExt == "jpg" || queryExt == "jpeg" || queryExt == "png" || queryExt == "gif" || queryExt == "tiff" || queryExt == "bmp" || queryExt == "webp" || queryExt == "psd");
            bool isAudio = (queryExt == "wav" || queryExt == "mp3" || queryExt == "flac" || queryExt == "aif" || queryExt == "aiff" || queryExt == "m4a");

            if (isImage) {
                // Strict duplicate rules for images: size, dimensions, screen orientation, and color space must ALL match
                bool sizeMatches = (candTamanho == tamanhoBytes);
                
                int candW = obj->hasProperty("larguraPx") ? static_cast<int>(obj->getProperty("larguraPx")) : 0;
                int candH = obj->hasProperty("alturaPx") ? static_cast<int>(obj->getProperty("alturaPx")) : 0;
                bool dimensionsMatch = (candW == largura && candH == altura && largura > 0 && altura > 0);
                
                juce::String candOrientation;
                juce::String candColorSpace;
                if (auto* bruto = obj->getProperty("bruto").getDynamicObject()) {
                    if (auto* exif = bruto->getProperty("exif").getDynamicObject()) {
                        if (exif->hasProperty("Exif.Image.Orientation")) {
                            candOrientation = exif->getProperty("Exif.Image.Orientation").toString();
                        }
                        if (exif->hasProperty("Exif.Photo.ColorSpace")) {
                            candColorSpace = exif->getProperty("Exif.Photo.ColorSpace").toString();
                        }
                    }
                }
                
                bool orientationMatches = (candOrientation == origOrientationStr);
                bool colorSpaceMatches = (candColorSpace == origColorSpaceStr);
                
                if (sizeMatches && dimensionsMatch && orientationMatches && colorSpaceMatches) {
                    coincidences = 3; // Force match!
                } else {
                    coincidences = 0; // Forced reject!
                }
            } else {
                // Duração (só comparamos para arquivos de áudio ou vídeo, imagens estáticas nunca comparam duração)
                if (duracao > 0.0 && obj->hasProperty("duracaoSegundos")) {
                    double candDur = obj->getProperty("duracaoSegundos");
                    if (std::abs(candDur - duracao) < 0.1) {
                        coincidences++;
                    }
                }
                // Dimensões (só comparamos para vídeos)
                if (largura > 0 && altura > 0 && obj->hasProperty("larguraPx") && obj->hasProperty("alturaPx")) {
                    int candW = obj->getProperty("larguraPx");
                    int candH = obj->getProperty("alturaPx");
                    if (candW == largura && candH == altura) {
                        coincidences++;
                    }
                }

                // Para arquivos de áudio, se as propriedades técnicas detalhadas ou a assinatura de áudio forem diferentes,
                // forçamos a rejeição (coincidences = 0) para evitar falsos positivos
                if (isAudio) {
                    if (origSampleRate > 0 && obj->hasProperty("sampleRate")) {
                        int candSR = obj->getProperty("sampleRate");
                        if (candSR != origSampleRate) {
                            coincidences = 0;
                        }
                    }
                    if (origChannels > 0 && obj->hasProperty("canais")) {
                        int candCh = obj->getProperty("canais");
                        if (candCh != origChannels) {
                            coincidences = 0;
                        }
                    }
                    if (std::abs(origLufs) > 0.0 && obj->hasProperty("lufsIntegrado")) {
                        double candLufs = obj->getProperty("lufsIntegrado");
                        if (std::abs(candLufs - origLufs) > 0.05) {
                            coincidences = 0;
                        }
                    }
                    if (std::abs(origPeak) > 0.0 && obj->hasProperty("picoDbfs")) {
                        double candPeak = obj->getProperty("picoDbfs");
                        if (std::abs(candPeak - origPeak) > 0.05) {
                            coincidences = 0;
                        }
                    }
                }
            }
        }

        if (coincidences >= 3) {
            AssetConhecido conhecido;
            conhecido.arquivoId = stmt.columnText(0);
            conhecido.itemId = stmt.columnText(1);
            conhecido.codigoAcervo = stmt.columnText(2);
            return conhecido;
        }
    }

    return std::nullopt;
}

} // namespace matriz::ingest
