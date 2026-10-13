#include "IngestaoStaging.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

#include "IngestArquivo.h"
#include "LeituraTecnica.h"
#include "Miniaturas.h"
#include "../Consolidacao/BridgeXmp.h"
#include "../Model/VolumeLog.h"

namespace matriz::ingest {

namespace {

std::mutex g_mutexIngestao;

// Sidecar XMP com homônimo (IMG_001.xmp ao lado de IMG_001.CR2/JPG/…): não vira item — seus dados entram no item do homônimo.
bool ehSidecarXmpDeHomonimo(const juce::File& f) {
    if (!f.hasFileExtension("xmp")) return false;
    for (const auto& irmao : f.getParentDirectory().findChildFiles(juce::File::findFiles, false, f.getFileNameWithoutExtension() + ".*"))
        if (!irmao.hasFileExtension("xmp") && irmao.getFileNameWithoutExtension() == f.getFileNameWithoutExtension()) return true;
    return false;
}

struct ArquivoComPasta {
    juce::File arquivo;
    juce::String caminhoPasta;  // "Raiz/sub/sub2" ou vazio (solto)
};

} // namespace

int maiorCodigoNumerico(matriz::model::Project& staging) {
    int maximo = 0;
    auto& db = staging.registro();
    auto stmt = db.prepare("SELECT COALESCE(MAX(CAST(SUBSTR(codigo_acervo, 5) AS INTEGER)), 0) FROM item "
                           "WHERE projeto_id = ? AND codigo_acervo LIKE 'ITM-%'");
    stmt.bind(1, matriz::db::Value::of(staging.projetoId()));
    if (stmt.step()) maximo = stmt.columnInt(0);
    return maximo;
}

int contarArquivosDaPasta(matriz::model::Project& staging, const std::string& pastaId) {
    auto& db = staging.registro();
    auto stmt = db.prepare("WITH RECURSIVE sub(id) AS ("
                           "  SELECT id FROM acervo_pasta WHERE id = ? "
                           "  UNION ALL "
                           "  SELECT p.id FROM acervo_pasta p JOIN sub s ON p.pasta_pai_id = s.id) "
                           "SELECT COUNT(DISTINCT item_id) FROM acervo_item_pasta WHERE pasta_id IN (SELECT id FROM sub)");
    stmt.bind(1, matriz::db::Value::of(pastaId));
    return stmt.step() ? stmt.columnInt(0) : 0;
}

ResultadoIngestao ingerirNoStaging(matriz::model::Project& staging, const juce::Array<juce::File>& arquivosOuPastas,
                                   const OpcoesIngestao& opcoes) {
    ResultadoIngestao res;
    std::lock_guard<std::mutex> trava(g_mutexIngestao);

    auto& db = staging.registro();
    const std::string projId = staging.projetoId();

    // Nomes de pasta raiz já existentes (para o sufixo " (2)", " (3)").
    std::set<juce::String> nomesRaiz;
    try {
        auto stmt = db.prepare("SELECT nome FROM acervo_pasta WHERE projeto_id = ? AND (pasta_pai_id IS NULL OR pasta_pai_id = '') "
                               "AND (? = '' OR mapa_id = ? OR mapa_id IS NULL OR mapa_id = '')");
        stmt.bind(1, matriz::db::Value::of(projId));
        stmt.bind(2, matriz::db::Value::of(opcoes.mapaId));
        stmt.bind(3, matriz::db::Value::of(opcoes.mapaId));
        while (stmt.step()) nomesRaiz.insert(juce::String(stmt.columnText(0)));
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("Ingestão: não foi possível ler as pastas raiz — " + juce::String(e.what()));
    }

    std::vector<ArquivoComPasta> fila;
    std::vector<juce::String> nomesRaizNovos;  // na ordem em que foram criadas
    for (const auto& f : arquivosOuPastas) {
        if (f.isDirectory()) {
            const juce::String original = f.getFileName();
            juce::String unico = original;
            int sufixo = 2;
            while (nomesRaiz.count(unico) != 0) unico = original + " (" + juce::String(sufixo++) + ")";
            nomesRaiz.insert(unico);
            nomesRaizNovos.push_back(unico);
            ++res.pastasRaiz;

            const auto lista = f.findChildFiles(juce::File::findFiles, true);
            if (lista.isEmpty()) {
                // Pasta vazia: ainda assim vira um nó (fila com arquivo inválido cria só a pasta).
                fila.push_back({ juce::File(), unico });
            }
            for (const auto& arq : lista) {
                if (arq.getFileName().startsWith(".")) continue;  // ocultos do sistema (.DS_Store…)
                if (opcoes.excluirCaminhos.count(arq.getFullPathName()) != 0) continue;
                if (ehSidecarXmpDeHomonimo(arq)) continue;
                const juce::String rel = arq.getRelativePathFrom(f).replaceCharacter('\\', '/');
                juce::String caminho = unico;
                if (rel.contains("/")) caminho += "/" + rel.upToLastOccurrenceOf("/", false, false);
                fila.push_back({ arq, caminho });
            }
        } else if (f.existsAsFile()) {
            if (opcoes.excluirCaminhos.count(f.getFullPathName()) != 0) continue;
            if (ehSidecarXmpDeHomonimo(f)) continue;
            fila.push_back({ f, {} });
            ++res.arquivosSoltos;
        } else {
            res.falhas.push_back({ f.getFullPathName(), "File or folder does not exist" });
        }
    }

    const int total = static_cast<int>(fila.size());
    int proximoCodigo = 0;
    try {
        proximoCodigo = maiorCodigoNumerico(staging);
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("Ingestão: não foi possível ler o maior código de item — " + juce::String(e.what()));
        res.falhas.push_back({ {}, "Could not read the item counter: " + juce::String(e.what()) });
        return res;
    }

    // Log por unidade de ORIGEM (SOURCE): o que foi lido de cada disco/pendrive/cartão e o que falhou.
    struct ResumoVolume { juce::File raiz; int ok = 0; juce::int64 bytes = 0; };
    std::map<juce::String, ResumoVolume> porVolume;

    std::map<juce::String, std::string> pastasCriadas;  // caminho acumulado -> id
    const auto garantirPasta = [&](const juce::String& caminho, const std::string& paiInicial) -> std::string {
        juce::StringArray partes;
        partes.addTokens(caminho, "/", "");
        std::string paiId = paiInicial;
        juce::String acumulado;
        for (int p = 0; p < partes.size(); ++p) {
            acumulado << (p > 0 ? "/" : "") << partes[p];
            auto it = pastasCriadas.find(acumulado);
            if (it != pastasCriadas.end()) {
                paiId = it->second;
                continue;
            }
            const std::string novaId = matriz::model::novoUuid();
            db.run("INSERT INTO acervo_pasta (id, projeto_id, nome, pasta_pai_id, mapa_id, criado_em, atualizado_em) VALUES (?, ?, ?, ?, ?, ?, ?)",
                   { matriz::db::Value::of(novaId), matriz::db::Value::of(projId),
                     matriz::db::Value::of(partes[p].toStdString()),
                     paiId.empty() ? matriz::db::Value::null() : matriz::db::Value::of(paiId),
                     opcoes.mapaId.empty() ? matriz::db::Value::null() : matriz::db::Value::of(opcoes.mapaId),
                     matriz::db::Value::of(matriz::model::agoraIso8601()),
                     matriz::db::Value::of(matriz::model::agoraIso8601()) });
            pastasCriadas[acumulado] = novaId;
            if (p == 0 && paiInicial.empty()) res.pastasRaizIds.push_back(novaId);
            paiId = novaId;
        }
        return paiId;
    };

    for (int i = 0; i < total; ++i) {
        if (opcoes.cancelar != nullptr && opcoes.cancelar->load()) {
            res.cancelado = true;
            break;
        }
        const auto& entrada = fila[static_cast<size_t>(i)];
        const juce::File& arq = entrada.arquivo;
        std::string itemInserido;

        try {
            std::string pastaId = opcoes.pastaDestinoId;
            if (entrada.caminhoPasta.isNotEmpty()) pastaId = garantirPasta(entrada.caminhoPasta, opcoes.pastaDestinoId);
            if (arq == juce::File()) {
                if (opcoes.aoProgredir) opcoes.aoProgredir(i + 1, total);
                continue;  // pasta vazia: só a pasta foi criada
            }

            const std::string itemId = matriz::model::novoUuid();
            const std::string titulo = arq.getFileNameWithoutExtension().toStdString();
            const auto cat = categoriaPorExtensao(arq);
            std::string tipoMidia = "documento";
            if (cat == CategoriaMidia::Audio) tipoMidia = "audio";
            else if (cat == CategoriaMidia::Video) tipoMidia = "video";
            else if (cat == CategoriaMidia::Imagem || cat == CategoriaMidia::Arte) tipoMidia = "foto";

            itemInserido = itemId;
            const std::string codigo = "ITM-" + std::to_string(++proximoCodigo);
            db.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
                   "VALUES (?, ?, ?, ?, ?, 'capturado', ?, ?)",
                   { matriz::db::Value::of(itemId), matriz::db::Value::of(projId), matriz::db::Value::of(codigo),
                     matriz::db::Value::of(titulo), matriz::db::Value::of(tipoMidia),
                     matriz::db::Value::of(matriz::model::agoraIso8601()),
                     matriz::db::Value::of(matriz::model::agoraIso8601()) });

            if (!pastaId.empty()) {
                db.run("INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, mapa_id, criado_em) VALUES (?, ?, ?, ?, ?)",
                       { matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                         matriz::db::Value::of(pastaId),
                         opcoes.mapaId.empty() ? matriz::db::Value::null() : matriz::db::Value::of(opcoes.mapaId),
                         matriz::db::Value::of(matriz::model::agoraIso8601()) });
            } else {
                res.itensSoltosIds.push_back(itemId);
            }

            const auto analise = analisarArquivo(arq);
            const auto resArq = gravarArquivoAnalisado(db, itemId, analise, "preservation_master", true);
            gerarEGravarMiniaturaPrincipal(staging.indice(), staging.pasta(), itemId, resArq.arquivoId, arq, cat,
                                           analise.leitura.duracaoSegundos);
            ++res.arquivosIngeridos;
            {
                const auto raizVol = matriz::model::volumelog::raizDoVolume(arq);
                auto& rv = porVolume[raizVol.getFullPathName()];
                rv.raiz = raizVol;
                ++rv.ok;
                rv.bytes += arq.getSize();
            }
            res.itensIngeridosIds.push_back(itemId);
            itemInserido.clear();  // item completo: o catch abaixo não deve mais removê-lo
            // XMP do arquivo (sidecar homônimo ou embutido): tags, assunto, geolocalização, título, descrição, data.
            // Roda ANTES do questionário, que soma o que o usuário digitou (assunto e tags somam; nunca substituem).
            try {
                if (const auto xmp = matriz::consolidacao::bridge::lerXmpBridge(arq))
                    matriz::consolidacao::bridge::aplicarDadosBridgeAoItem(db, itemId, *xmp);
            } catch (const std::exception& e) {
                juce::Logger::writeToLog("Ingestão: XMP de " + arq.getFullPathName() + " — " + juce::String(e.what()));
            }
            if (opcoes.aoItemIngerido) {
                try {
                    opcoes.aoItemIngerido(itemId);
                } catch (const std::exception& e) {
                    juce::Logger::writeToLog("Ingestão: aoItemIngerido falhou em " + arq.getFullPathName() + " — " + juce::String(e.what()));
                }
            }
            if (opcoes.aoArquivoIngerido) {
                try {
                    opcoes.aoArquivoIngerido(itemId, arq);
                } catch (const std::exception& e) {
                    juce::Logger::writeToLog("Ingestão: aoArquivoIngerido falhou em " + arq.getFullPathName() + " — " + juce::String(e.what()));
                }
            }
        } catch (const std::exception& e) {
            juce::Logger::writeToLog("Ingestão: falha em " + arq.getFullPathName() + " — " + juce::String(e.what()));
            res.falhas.push_back({ arq.getFullPathName(), juce::String(e.what()) });
            if (!itemInserido.empty()) {
                // Não deixa um item sem arquivo para trás.
                try {
                    db.run("DELETE FROM item WHERE id = ?", { matriz::db::Value::of(itemInserido) });
                    res.itensSoltosIds.erase(std::remove(res.itensSoltosIds.begin(), res.itensSoltosIds.end(), itemInserido),
                                             res.itensSoltosIds.end());
                } catch (const std::exception& e2) {
                    juce::Logger::writeToLog("Ingestão: não foi possível remover o item órfão — " + juce::String(e2.what()));
                }
            }
        }
        if (opcoes.aoProgredir) opcoes.aoProgredir(i + 1, total);
    }

    juce::ignoreUnused(nomesRaizNovos);
    try {
        const auto pasta = staging.pasta();
        for (const auto& [chave, rv] : porVolume)
            matriz::model::volumelog::registrar(pasta, rv.raiz, "SOURCE", res.cancelado ? "INGEST (cancelled)" : "INGEST",
                                                juce::String(rv.ok) + " files read (" + juce::File::descriptionOfSizeInBytes(rv.bytes) +
                                                    ") into the backup catalog");
        for (const auto& f : res.falhas) {
            if (f.caminho.isEmpty()) continue;
            matriz::model::volumelog::registrar(pasta, juce::File(f.caminho), "SOURCE", "ERROR", f.caminho + " - " + f.motivo);
        }
    } catch (...) {
    }
    return res;
}

} // namespace matriz::ingest
