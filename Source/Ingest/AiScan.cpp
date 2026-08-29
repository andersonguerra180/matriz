#include "AiScan.h"

#include "../Model/Project.h"
#include "../Vault/Resolucao.h"
#include "LeituraTecnica.h"
#include "Miniaturas.h"
#include "ProcessoExterno.h"

#include <algorithm>
#include <thread>

namespace matriz::ingest {

namespace {

using matriz::db::Value;

// Limite prático de inlineData numa requisição. O teto documentado é ~20MB
// para a requisição inteira; 14MB de payload deixa margem pro base64 (+33%)
// e pro resto do JSON.
constexpr juce::int64 kMaxBytesInline = 14 * 1024 * 1024;

struct ParteInline {
    juce::MemoryBlock dados;
    juce::String mime;
};

struct RespostaApi {
    juce::String texto;
    juce::String erro;
    int httpStatus = 0;
    bool fatal = false;        // 400/401/403/404 — repetir em 300 itens não ajuda
    bool transitorio = false;  // 429/5xx — vale retry com backoff
};

juce::String mimeDeImagem(const juce::File& f) {
    auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".png") return "image/png";
    if (ext == ".webp") return "image/webp";
    if (ext == ".heic") return "image/heic";
    if (ext == ".heif") return "image/heif";
    return "image/jpeg";
}

// Mime de áudio que a API aceita direto, sem transcodificar.
juce::String mimeDeAudioSuportado(const juce::File& f) {
    auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".mp3") return "audio/mp3";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".flac") return "audio/flac";
    if (ext == ".aac") return "audio/aac";
    if (ext == ".m4a") return "audio/aac";
    if (ext == ".ogg" || ext == ".opus") return "audio/ogg";
    if (ext == ".aif" || ext == ".aiff") return "audio/aiff";
    return {};
}

juce::String instrucaoPalavrasChave() {
    return "\n\nFinish with a single line starting exactly with 'KEYWORDS:' listing search terms in "
           "BOTH English and Portuguese, comma separated (e.g. 'clown, palhaco, circus, circo'). "
           "Base every statement only on what you actually perceive in the provided content; if "
           "something is not determinable, omit it rather than guessing.";
}

// ---------------------------------------------------------------------------
// Chamada da API
// ---------------------------------------------------------------------------

RespostaApi chamarGemini(const juce::String& apiKey, const juce::String& modelo,
                          const juce::String& prompt, const std::vector<ParteInline>& partes) {
    RespostaApi out;

    juce::Array<juce::var> partsArr;
    for (const auto& p : partes) {
        if (p.dados.getSize() == 0) continue;
        auto* inlineData = new juce::DynamicObject();
        inlineData->setProperty("mime_type", p.mime);
        // Base64::toBase64, NÃO MemoryBlock::toBase64Encoding(): o segundo é um
        // formato proprietário do JUCE (tamanho embutido, outro alfabeto) que a
        // API recebe como binário corrompido.
        inlineData->setProperty("data", juce::Base64::toBase64(p.dados.getData(), p.dados.getSize()));
        auto* parte = new juce::DynamicObject();
        parte->setProperty("inline_data", juce::var(inlineData));
        partsArr.add(juce::var(parte));
    }

    auto* partText = new juce::DynamicObject();
    partText->setProperty("text", prompt);
    partsArr.add(juce::var(partText));

    auto* content = new juce::DynamicObject();
    content->setProperty("parts", juce::var(partsArr));

    juce::Array<juce::var> contentsArr;
    contentsArr.add(juce::var(content));

    auto* body = new juce::DynamicObject();
    body->setProperty("contents", juce::var(contentsArr));

    juce::String jsonBody = juce::JSON::toString(juce::var(body));

    // Chave no header, não na query string: URL vaza em log de proxy e em
    // relatório de crash.
    juce::URL url("https://generativelanguage.googleapis.com/v1beta/models/" + modelo + ":generateContent");
    url = url.withPOSTData(jsonBody);

    int statusCode = 0;
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withExtraHeaders("Content-Type: application/json\r\nx-goog-api-key: " + apiKey)
                       .withConnectionTimeoutMs(120000)
                       .withStatusCode(&statusCode);

    std::unique_ptr<juce::InputStream> stream;
    try {
        stream = url.createInputStream(options);
    } catch (...) {
        out.erro = "Network call threw an exception.";
        out.transitorio = true;
        return out;
    }

    if (!stream) {
        out.erro = "Could not reach generativelanguage.googleapis.com.";
        out.transitorio = true;
        return out;
    }

    juce::String resposta = stream->readEntireStreamAsString();
    out.httpStatus = statusCode;
    juce::var parsed = juce::JSON::parse(resposta);

    if (auto erro = parsed["error"]; erro.isObject()) {
        int code = static_cast<int>(erro["code"]);
        if (code != 0) out.httpStatus = code;
        out.erro = erro["message"].toString();
    }

    if (out.httpStatus >= 400) {
        if (out.erro.isEmpty()) out.erro = "HTTP " + juce::String(out.httpStatus) + ": " + resposta.substring(0, 300);
        out.fatal = (out.httpStatus == 400 || out.httpStatus == 401
                     || out.httpStatus == 403 || out.httpStatus == 404);
        out.transitorio = (out.httpStatus == 429 || out.httpStatus >= 500);
        return out;
    }

    if (!parsed.isObject()) {
        out.erro = "Unexpected (non-JSON) reply: " + resposta.substring(0, 300);
        return out;
    }

    // Filtro de segurança é caso legítimo num acervo com material sensível:
    // vira motivo legível, não falha muda.
    if (auto fb = parsed["promptFeedback"]; fb.isObject()) {
        juce::String bloqueio = fb["blockReason"].toString();
        if (bloqueio.isNotEmpty()) {
            out.erro = "Blocked by the API safety filter (" + bloqueio + ").";
            return out;
        }
    }

    auto candidates = parsed["candidates"];
    if (!candidates.isArray() || candidates.getArray()->isEmpty()) {
        out.erro = "API returned no candidates: " + resposta.substring(0, 300);
        return out;
    }

    auto primeiro = (*candidates.getArray())[0];
    auto partsResp = primeiro["content"]["parts"];
    if (!partsResp.isArray() || partsResp.getArray()->isEmpty()) {
        juce::String motivo = primeiro["finishReason"].toString();
        out.erro = "API reply had no text" + (motivo.isEmpty() ? juce::String() : " (finishReason: " + motivo + ")");
        return out;
    }

    juce::String texto;
    for (auto& p : *partsResp.getArray()) texto += p["text"].toString();
    out.texto = texto.trim();
    if (out.texto.isEmpty()) out.erro = "API returned an empty text part.";
    return out;
}

// Descobre qual modelo da lista responde. Um 404 significa modelo inexistente
// ou desligado — tenta o próximo. Qualquer outro erro é do lote, não do modelo.
RespostaApi resolverModelo(const juce::String& apiKey, const std::vector<juce::String>& modelos,
                            juce::String& modeloEscolhido) {
    RespostaApi ultima;
    for (const auto& m : modelos) {
        ultima = chamarGemini(apiKey, m, "Reply with the single word: ok", {});
        if (ultima.httpStatus == 404) continue;
        modeloEscolhido = m;
        return ultima;
    }
    return ultima;
}

// ---------------------------------------------------------------------------
// Preparação de mídia (sempre em cópia temporária; original é read-only)
// ---------------------------------------------------------------------------

bool lerArquivoInteiro(const juce::File& f, juce::MemoryBlock& destino) {
    return f.existsAsFile() && f.getSize() > 0 && f.loadFileAsData(destino) && destino.getSize() > 0;
}

double duracaoDoItem(matriz::db::Database& registro, const std::string& itemId) {
    try {
        auto stmt = registro.prepare(
            "SELECT caracteristicas_tecnicas_json FROM arquivo WHERE item_id = ? "
            "ORDER BY eh_master DESC, id LIMIT 1");
        stmt.bind(1, Value::of(itemId));
        if (!stmt.step()) return 0.0;
        auto v = juce::JSON::parse(juce::String(stmt.columnText(0)));
        double d = static_cast<double>(v["format"]["duration"]);
        if (d > 0.0) return d;
        return juce::String(v["format"]["duration"].toString()).getDoubleValue();
    } catch (...) {
        return 0.0;
    }
}

// Proxy de áudio via ffmpeg. Mono 16 kHz é o que a API usa internamente de
// qualquer forma — subir um WAV 96/24 é desperdício de upload, não qualidade.
// Tenta os encoders em ordem de disponibilidade num build genérico de ffmpeg.
bool gerarProxyAudio(const juce::File& origem, const juce::File& destino, double segundosMax,
                      juce::String& mimeOut, juce::String& erroOut) {
    struct Perfil { const char* codec; const char* formato; const char* extensao; const char* mime; };
    const Perfil perfis[] = {
        {"libopus", "ogg", ".ogg", "audio/ogg"},
        {"aac", "adts", ".aac", "audio/aac"},
        {"libmp3lame", "mp3", ".mp3", "audio/mp3"},
    };

    for (const auto& perfil : perfis) {
        juce::File saida = destino.withFileExtension(perfil.extensao);
        juce::StringArray args;
        args.add("-nostdin");
        args.add("-y");
        args.add("-i");
        args.add(origem.getFullPathName());
        args.add("-vn");
        args.add("-ac");
        args.add("1");
        args.add("-ar");
        args.add("16000");
        args.add("-t");
        args.add(juce::String(segundosMax, 3));
        args.add("-c:a");
        args.add(perfil.codec);
        args.add("-b:a");
        args.add("16k");
        args.add("-f");
        args.add(perfil.formato);
        args.add(saida.getFullPathName());

        try {
            rodarEsperandoSucesso("ffmpeg", args, 300000);
        } catch (const std::exception& e) {
            erroOut = e.what();
            saida.deleteFile();
            continue;
        }

        if (saida.existsAsFile() && saida.getSize() > 0) {
            mimeOut = perfil.mime;
            if (saida != destino) {
                destino.deleteFile();
                if (!saida.moveFileTo(destino)) {
                    mimeOut = perfil.mime;
                    erroOut.clear();
                    // Usa o próprio arquivo gerado quando o rename falha.
                    const_cast<juce::File&>(destino) = saida;
                }
            }
            erroOut.clear();
            return true;
        }
        saida.deleteFile();
    }

    if (erroOut.isEmpty()) erroOut = "ffmpeg produced no audio proxy.";
    return false;
}

} // namespace

// ---------------------------------------------------------------------------

int reindexarAiScanNaBusca(matriz::db::Database& indice, matriz::db::Database& registro) {
    int indexados = 0;
    std::vector<std::pair<std::string, std::string>> linhas;

    auto stmt = indice.prepare(
        "SELECT item_id, resumo FROM ai_scan_resultado WHERE resumo IS NOT NULL AND resumo <> ''");
    while (stmt.step()) linhas.emplace_back(stmt.columnText(0), stmt.columnText(1));

    if (linhas.empty()) return 0;

    registro.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& [itemId, resumo] : linhas) {
            registro.run("DELETE FROM busca_fts WHERE item_id = ? AND conteudo = ?",
                          {Value::of(itemId), Value::of(resumo)});
            registro.run("INSERT INTO busca_fts (item_id, conteudo) VALUES (?, ?)",
                          {Value::of(itemId), Value::of(resumo)});
            ++indexados;
        }
        registro.run("COMMIT", {});
    } catch (...) {
        registro.run("ROLLBACK", {});
        throw;
    }
    return indexados;
}

AiScanRelatorio executarAiScan(matriz::db::Database& indice,
                                matriz::db::Database& registro,
                                const juce::String& apiKey,
                                const std::vector<std::string>& itemIds,
                                const juce::File& pastaProjeto,
                                const AiScanOpcoes& opcoes,
                                const AoProgressoScan& aoProgresso,
                                matriz::app::CancelamentoPtr cancelamento) {
    AiScanRelatorio rel;
    rel.solicitados = static_cast<int>(itemIds.size());

    if (apiKey.isEmpty()) {
        rel.erroFatal = "No Gemini API key configured.";
        return rel;
    }
    if (itemIds.empty()) return rel;

    // Um 404 de modelo desligado repetido em 300 itens é ruído: resolve o
    // modelo uma vez, antes do lote.
    juce::String modelo;
    RespostaApi sonda = resolverModelo(apiKey, opcoes.modelos, modelo);
    if (modelo.isEmpty()) {
        rel.erroFatal = "None of the configured models are available (last error: "
                        + sonda.erro.toStdString() + ").";
        return rel;
    }
    if (sonda.fatal) {
        rel.erroFatal = sonda.erro.toStdString();
        return rel;
    }
    rel.modeloUsado = modelo;

    juce::File dirTmp = pastaProjeto.getChildFile("cache").getChildFile("aiscan_tmp");
    dirTmp.createDirectory();

    const int total = static_cast<int>(itemIds.size());
    int feito = 0;

    for (const auto& itemId : itemIds) {
        if (cancelamento && cancelamento->pedido()) { rel.cancelado = true; break; }

        juce::File arquivo;
        juce::File itemTmp = dirTmp.getChildFile(juce::Uuid().toDashedString());

        try {
            auto stmt = registro.prepare(
                std::string("SELECT ") + matriz::vault::colunasDeResolucao() +
                " FROM arquivo a " + matriz::vault::joinDeResolucao() +
                " WHERE a.item_id = ? ORDER BY a.eh_master DESC, a.id LIMIT 1");
            stmt.bind(1, Value::of(itemId));
            if (!stmt.step()) {
                rel.falhas.push_back({itemId, "", "No file row for this item.", 0});
                ++feito;
                if (aoProgresso) aoProgresso(feito, total, {});
                continue;
            }

            auto resolvido = matriz::vault::resolverCaminho(
                pastaProjeto, stmt.columnText(0), stmt.columnText(1), stmt.columnText(2));
            if (!resolvido || !resolvido->existsAsFile()) {
                rel.falhas.push_back({itemId, stmt.columnText(1), "File not found on disk.", 0});
                ++feito;
                if (aoProgresso) aoProgresso(feito, total, {});
                continue;
            }
            arquivo = *resolvido;
        } catch (const std::exception& e) {
            rel.falhas.push_back({itemId, "", std::string("Database error: ") + e.what(), 0});
            ++feito;
            if (aoProgresso) aoProgresso(feito, total, {});
            continue;
        }

        if (aoProgresso) aoProgresso(feito, total, arquivo.getFileName());

        std::string tipoAnalise;
        std::vector<ParteInline> partes;
        juce::String prompt;
        juce::String motivoIgnorar;

        itemTmp.createDirectory();

        switch (categoriaPorExtensao(arquivo)) {
            case CategoriaMidia::Imagem: {
                tipoAnalise = "visual";
                ParteInline p;
                juce::Image img = juce::ImageFileFormat::loadFrom(arquivo);
                if (img.isValid()) {
                    const int alvo = opcoes.ladoMaximoImagem;
                    if (img.getWidth() > alvo || img.getHeight() > alvo) {
                        double escala = static_cast<double>(alvo) / std::max(img.getWidth(), img.getHeight());
                        img = img.rescaled(std::max(1, static_cast<int>(img.getWidth() * escala)),
                                            std::max(1, static_cast<int>(img.getHeight() * escala)),
                                            juce::Graphics::highResamplingQuality);
                    }
                    juce::MemoryOutputStream ms(p.dados, false);
                    juce::JPEGImageFormat fmt;
                    fmt.setQuality(0.85f);
                    fmt.writeImageToStream(img, ms);
                    ms.flush();
                    p.mime = "image/jpeg";
                } else if (arquivo.getSize() <= kMaxBytesInline && lerArquivoInteiro(arquivo, p.dados)) {
                    // JUCE não decodifica HEIC nem vários RAW: sobe o original
                    // e deixa a API decodificar.
                    p.mime = mimeDeImagem(arquivo);
                }
                if (p.dados.getSize() > 0) partes.push_back(std::move(p));
                else motivoIgnorar = "Could not decode this image format.";

                prompt = "Describe this image for an archival catalog: subject matter, people and objects, "
                         "setting, notable features, estimated era, and transcribe any text visible in the "
                         "image." + instrucaoPalavrasChave();
                break;
            }

            case CategoriaMidia::Audio: {
                tipoAnalise = "audio";
                ParteInline p;
                juce::String mimeDireto = mimeDeAudioSuportado(arquivo);

                if (mimeDireto.isNotEmpty() && arquivo.getSize() <= kMaxBytesInline
                    && lerArquivoInteiro(arquivo, p.dados)) {
                    p.mime = mimeDireto;
                } else {
                    juce::File proxy = itemTmp.getChildFile("proxy");
                    juce::String mimeProxy, erroProxy;
                    if (gerarProxyAudio(arquivo, proxy, opcoes.segundosMaxAudio, mimeProxy, erroProxy)) {
                        juce::File real = proxy.existsAsFile() ? proxy : proxy.withFileExtension("ogg");
                        if (lerArquivoInteiro(real, p.dados)) p.mime = mimeProxy;
                        else motivoIgnorar = "Audio proxy could not be read back.";
                    } else {
                        motivoIgnorar = "Could not transcode audio: " + erroProxy.toStdString();
                    }
                }

                if (p.dados.getSize() > 0) partes.push_back(std::move(p));
                prompt = "Listen to this audio and describe it for an archival catalog. State whether it is "
                         "speech, music, ambience or noise. If there is speech, give the language and "
                         "transcribe or summarise what is said. If it is music, give instrumentation, style, "
                         "tempo, language of any vocals, and whether it sounds like a studio recording, a "
                         "live performance, a rehearsal or a field recording. Note audible defects (hiss, "
                         "hum, dropouts, wow/flutter, distortion, clicks)." + instrucaoPalavrasChave();
                break;
            }

            case CategoriaMidia::Video: {
                tipoAnalise = "video";
                double duracao = duracaoDoItem(registro, itemId);
                juce::String tempos;

                if (duracao > 0.0) {
                    try {
                        auto frames = gerarKeyframesVideo(arquivo, duracao, opcoes.keyframesPorVideo,
                                                           itemTmp, "kf", 768);
                        for (const auto& f : frames) {
                            ParteInline p;
                            if (!lerArquivoInteiro(f.arquivo, p.dados)) continue;
                            p.mime = "image/jpeg";
                            partes.push_back(std::move(p));
                            tempos += (tempos.isEmpty() ? "" : ", ")
                                      + juce::String(f.tempoSegundos, 1) + "s";
                        }
                    } catch (const std::exception& e) {
                        motivoIgnorar = std::string("Keyframe extraction failed: ") + e.what();
                    }
                } else {
                    motivoIgnorar = "Unknown video duration (no technical metadata).";
                }

                // Trilha de áudio junto: o que é dito costuma valer mais que o
                // que aparece em quatro frames.
                {
                    juce::File proxy = itemTmp.getChildFile("audio");
                    juce::String mimeProxy, erroProxy;
                    if (gerarProxyAudio(arquivo, proxy, opcoes.segundosMaxAudio, mimeProxy, erroProxy)) {
                        ParteInline p;
                        juce::File real = proxy.existsAsFile() ? proxy : proxy.withFileExtension("ogg");
                        if (lerArquivoInteiro(real, p.dados)) {
                            p.mime = mimeProxy;
                            partes.push_back(std::move(p));
                        }
                    }
                }

                if (!partes.empty()) motivoIgnorar.clear();

                prompt = "These are keyframes sampled from a single video"
                         + (tempos.isEmpty() ? juce::String() : " at " + tempos)
                         + ", plus its audio track. Describe the video for an archival catalog: subject "
                           "matter, people and objects, setting, spoken content, and estimated era. "
                           "Describe only what is visible in the sampled frames and audible in the audio — "
                           "do NOT invent what happens between the sampled frames."
                         + instrucaoPalavrasChave();
                break;
            }

            case CategoriaMidia::Sessao:
            case CategoriaMidia::Documento:
            case CategoriaMidia::Texto: {
                tipoAnalise = "documento";

                if (arquivo.hasFileExtension("pdf")) {
                    ParteInline p;
                    if (arquivo.getSize() > kMaxBytesInline) {
                        motivoIgnorar = "PDF is larger than the inline upload limit ("
                                        + juce::File::descriptionOfSizeInBytes(kMaxBytesInline).toStdString()
                                        + "); the Files API would be required.";
                    } else if (lerArquivoInteiro(arquivo, p.dados)) {
                        p.mime = "application/pdf";
                        partes.push_back(std::move(p));
                    } else {
                        motivoIgnorar = "PDF could not be read.";
                    }
                    prompt = "Summarise this document for an archival catalog: main topic, key content, "
                             "document type, and any dates or people mentioned." + instrucaoPalavrasChave();
                } else if (arquivo.hasFileExtension("doc;docx;odt;rtf;pages")) {
                    // Ler bytes binários como texto e mandar pra API produz
                    // resumo inventado. Campo vazio é melhor que campo falso.
                    motivoIgnorar = "Binary word-processor formats are not readable directly — "
                                    "convert to PDF to scan this file.";
                } else {
                    juce::String texto = arquivo.loadFileAsString();
                    if (texto.length() > 20000) texto = texto.substring(0, 20000) + "\n[truncated]";
                    if (texto.containsNonWhitespaceChars()) {
                        prompt = "Summarise this document for an archival catalog:\n\n" + texto +
                                 "\n\nGive main topic, key content, document type, and any dates or people "
                                 "mentioned." + instrucaoPalavrasChave();
                    } else {
                        motivoIgnorar = "File contains no readable text.";
                    }
                }
                break;
            }

            default:
                motivoIgnorar = "Unsupported media type for content analysis.";
                break;
        }

        if (motivoIgnorar.isNotEmpty()) {
            rel.falhas.push_back({itemId, arquivo.getFileName().toStdString(),
                                   motivoIgnorar.toStdString(), 0});
            ++rel.ignorados;
            itemTmp.deleteRecursively();
            ++feito;
            if (aoProgresso) aoProgresso(feito, total, arquivo.getFileName());
            continue;
        }

        RespostaApi resposta;
        for (int tentativa = 0; tentativa < std::max(1, opcoes.tentativasPorItem); ++tentativa) {
            if (cancelamento && cancelamento->pedido()) { rel.cancelado = true; break; }

            resposta = chamarGemini(apiKey, modelo, prompt, partes);
            if (resposta.texto.isNotEmpty() || resposta.fatal || !resposta.transitorio) break;

            // Backoff 2s/6s/14s, interrompível: cancelar não pode ficar preso
            // esperando um sleep longo.
            int esperaMs = (tentativa == 0 ? 2000 : tentativa == 1 ? 6000 : 14000);
            for (int passo = 0; passo < esperaMs / 200; ++passo) {
                if (cancelamento && cancelamento->pedido()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }

        itemTmp.deleteRecursively();

        if (rel.cancelado) break;

        if (resposta.fatal) {
            rel.erroFatal = resposta.erro.toStdString();
            break;
        }

        if (resposta.texto.isEmpty()) {
            rel.falhas.push_back({itemId, arquivo.getFileName().toStdString(),
                                   resposta.erro.toStdString(), resposta.httpStatus});
            ++feito;
            if (aoProgresso) aoProgresso(feito, total, arquivo.getFileName());
            continue;
        }

        try {
            // Id determinístico: com UUID novo a cada rodada o OR REPLACE nunca
            // substituía nada e rescan acumulava duplicata indefinidamente.
            std::string scanId = itemId + ":" + tipoAnalise;
            std::string resumo = resposta.texto.toStdString();

            auto* contexto = new juce::DynamicObject();
            contexto->setProperty("filename", arquivo.getFileName());
            contexto->setProperty("extension", arquivo.getFileExtension().removeCharacters(".").toLowerCase());
            contexto->setProperty("category", juce::String(tipoAnalise));
            contexto->setProperty("model", modelo);
            juce::String contextoJson = juce::JSON::toString(juce::var(contexto));

            // O resumo anterior sai da FTS antes do novo entrar, senão rescan
            // duplica a linha de busca.
            std::string resumoAnterior;
            {
                auto sAnt = indice.prepare("SELECT resumo FROM ai_scan_resultado WHERE id = ?");
                sAnt.bind(1, Value::of(scanId));
                if (sAnt.step()) resumoAnterior = sAnt.columnText(0);
            }
            if (!resumoAnterior.empty())
                registro.run("DELETE FROM busca_fts WHERE item_id = ? AND conteudo = ?",
                              {Value::of(itemId), Value::of(resumoAnterior)});

            indice.run(
                "INSERT OR REPLACE INTO ai_scan_resultado "
                "(id, item_id, modelo, tipo_analise, contexto_json, resumo, confianca, analisado_em) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                {Value::of(scanId), Value::of(itemId), Value::of(modelo.toStdString()),
                 Value::of(tipoAnalise), Value::of(contextoJson.toStdString()), Value::of(resumo),
                 Value::of(0.8), Value::of(matriz::model::agoraIso8601())});

            if (opcoes.indexarNaBusca) {
                registro.run("DELETE FROM busca_fts WHERE item_id = ? AND conteudo = ?",
                              {Value::of(itemId), Value::of(resumo)});
                registro.run("INSERT INTO busca_fts (item_id, conteudo) VALUES (?, ?)",
                              {Value::of(itemId), Value::of(resumo)});
            }

            ++rel.sucessos;
        } catch (const std::exception& e) {
            rel.falhas.push_back({itemId, arquivo.getFileName().toStdString(),
                                   std::string("Could not save result: ") + e.what(), 0});
        }

        ++feito;
        if (aoProgresso) aoProgresso(feito, total, arquivo.getFileName());
    }

    dirTmp.deleteRecursively();
    return rel;
}

std::vector<AiScanResult> resultadosDoItem(matriz::db::Database& indice, const std::string& itemId) {
    std::vector<AiScanResult> out;
    auto stmt = indice.prepare(
        "SELECT id, item_id, modelo, tipo_analise, contexto_json, resumo, confianca, analisado_em "
        "FROM ai_scan_resultado WHERE item_id = ? ORDER BY analisado_em DESC");
    stmt.bind(1, Value::of(itemId));
    while (stmt.step()) {
        AiScanResult r;
        r.id = stmt.columnText(0);
        r.itemId = stmt.columnText(1);
        r.modelo = stmt.columnText(2);
        r.tipoAnalise = stmt.columnText(3);
        r.contextoJson = stmt.columnText(4);
        r.resumo = stmt.columnText(5);
        r.confianca = stmt.columnReal(6);
        r.analisadoEm = stmt.columnText(7);
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace matriz::ingest
