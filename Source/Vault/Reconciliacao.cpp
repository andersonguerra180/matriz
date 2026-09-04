#include "Reconciliacao.h"

#include <map>
#include <set>

#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "Volume.h"

namespace matriz::vault {

using matriz::db::Value;

namespace {

// Autor dos eventos que o próprio sistema gera. A proveniência distingue
// decisão humana de constatação de máquina — misturar as duas tiraria o
// valor da trilha.
const char* kAutorSistema = "system:vault-sync";

struct ArquivoRegistrado {
    std::string arquivoId;
    std::string itemId;
    std::string caminhoRelativo;
    std::string sha256;
    long long tamanhoBytes = 0;
    std::string estadoPresenca;
    std::string modificadoEm;  // ISO-8601 do último visto; vazio se nunca registrado
};

std::vector<ArquivoRegistrado> lerArquivosDoVault(matriz::db::Database& registro, const std::string& vaultId) {
    std::vector<ArquivoRegistrado> out;
    auto stmt = registro.prepare(
        "SELECT id, item_id, caminho_relativo, IFNULL(checksum_sha256, ''), IFNULL(tamanho_bytes, 0), "
        "estado_presenca, IFNULL(visto_pela_ultima_vez, '') FROM arquivo WHERE vault_id = ?");
    stmt.bind(1, Value::of(vaultId));
    while (stmt.step()) {
        ArquivoRegistrado a;
        a.arquivoId = stmt.columnText(0);
        a.itemId = stmt.columnText(1);
        a.caminhoRelativo = stmt.columnText(2);
        a.sha256 = stmt.columnText(3);
        a.tamanhoBytes = stmt.columnInt(4);
        a.estadoPresenca = stmt.columnText(5);
        a.modificadoEm = stmt.columnText(6);
        out.push_back(std::move(a));
    }
    return out;
}

std::string localizacaoDoVault(matriz::db::Database& registro, const std::string& vaultId) {
    auto stmt = registro.prepare("SELECT localizacao FROM vault WHERE id = ?");
    stmt.bind(1, Value::of(vaultId));
    return stmt.step() ? stmt.columnText(0) : std::string();
}

void definirEstadoPresenca(matriz::db::Database& registro, const std::string& arquivoId,
                            const std::string& estado, bool marcarVisto) {
    if (marcarVisto) {
        registro.run("UPDATE arquivo SET estado_presenca = ?, visto_pela_ultima_vez = ? WHERE id = ?",
                     {Value::of(estado), Value::of(matriz::model::agoraIso8601()), Value::of(arquivoId)});
    } else {
        registro.run("UPDATE arquivo SET estado_presenca = ? WHERE id = ?",
                     {Value::of(estado), Value::of(arquivoId)});
    }
}

// Abre e fecha a janela em que a trava de master do schema aceita UPDATE de
// caminho/checksum (ver `reconciliacao_em_curso` em schema/registro.sql).
// RAII porque uma saída por exceção ou por cancelamento não pode deixar a
// trava aberta — seria uma brecha permanente na garantia P1.
struct JanelaDeReconciliacao {
    explicit JanelaDeReconciliacao(matriz::db::Database& db) : db_(db) {
        db_.run("INSERT OR IGNORE INTO reconciliacao_em_curso (marca) VALUES (1)", {});
    }
    ~JanelaDeReconciliacao() {
        try {
            db_.run("DELETE FROM reconciliacao_em_curso", {});
        } catch (...) {
            // Destrutor não propaga. Se nem o DELETE passou, o banco já está
            // num estado em que a próxima abertura reaplica o schema.
        }
    }
    JanelaDeReconciliacao(const JanelaDeReconciliacao&) = delete;
    JanelaDeReconciliacao& operator=(const JanelaDeReconciliacao&) = delete;

    matriz::db::Database& db_;
};

// Arquivos que o ingest ignora por regra (§5) não podem virar "novo" na
// varredura — senão todo volume aparece com centenas de novidades que
// ninguém quer catalogar.
bool ehIgnoravel(const juce::File& f) {
    juce::String nome = f.getFileName();
    if (nome.startsWith(".")) return true;  // dotfiles e resource forks (._*)
    juce::String ext = f.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    if (ext == "sfk" || ext == "reapeaks" || ext == "asd") return true;
    return f.getSize() == 0;
}

} // namespace

juce::String ResumoReconciliacao::comoTexto() const {
    juce::StringArray partes;
    if (novos > 0) partes.add(juce::String(novos) + " new");
    if (movidos > 0) partes.add(juce::String(movidos) + " moved");
    if (ausentes > 0) partes.add(juce::String(ausentes) + " missing");
    if (alterados > 0) partes.add(juce::String(alterados) + " changed");
    if (corrompidos > 0) partes.add(juce::String(corrompidos) + " corrupted");
    if (partes.isEmpty()) return "No changes";
    return partes.joinIntoString(" | ");
}

void registrarProveniencia(matriz::db::Database& registro, const std::string& itemId, const std::string& evento,
                            const std::string& autor, const std::string& detalhes) {
    // Append-only por contrato (§4): nunca há UPDATE nem DELETE aqui. Uma
    // trilha que pode ser reescrita não serve de trilha.
    registro.run(
        "INSERT INTO proveniencia (id, item_id, evento, autor, detalhes, criado_em) VALUES (?, ?, ?, ?, ?, ?)",
        {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(evento), Value::of(autor),
         Value::of(detalhes), Value::of(matriz::model::agoraIso8601())});
}

bool atualizarPresencaDoVault(matriz::db::Database& registro, const std::string& vaultId) {
    std::string localizacao, uuidVolume, statusAtual;
    {
        auto stmt = registro.prepare(
            "SELECT localizacao, IFNULL(uuid_volume, ''), status FROM vault WHERE id = ?");
        stmt.bind(1, Value::of(vaultId));
        if (!stmt.step()) return false;
        localizacao = stmt.columnText(0);
        uuidVolume = stmt.columnText(1);
        statusAtual = stmt.columnText(2);
    }

    juce::File raiz{juce::String(localizacao)};
    bool online = raiz.isDirectory();

    // O caminho pode ter sido reusado por OUTRO volume (o macOS monta o
    // segundo disco de mesmo nome em "/Volumes/Nome 1", mas o primeiro
    // caminho pode ser reciclado). Quando o Vault tem UUID, é ele que
    // decide — sem isso a reconciliação compararia o catálogo com o disco
    // errado e marcaria tudo como ausente.
    if (online && !uuidVolume.empty()) online = (uuidDoVolume(raiz) == uuidVolume);

    std::string novoStatus = online ? "online" : "offline";
    if (novoStatus == statusAtual) return false;

    if (online) {
        registro.run("UPDATE vault SET status = 'online', visto_em = ? WHERE id = ?",
                     {Value::of(matriz::model::agoraIso8601()), Value::of(vaultId)});
    } else {
        registro.run("UPDATE vault SET status = 'offline' WHERE id = ?", {Value::of(vaultId)});
    }
    return true;
}

std::vector<std::string> reavaliarVaults(matriz::db::Database& registro) {
    sincronizarDrivesDoProjeto(registro, "");
    std::vector<std::string> ficaramOnline;
    std::vector<std::pair<std::string, std::string>> vaults;  // id, status anterior
    {
        auto stmt = registro.prepare("SELECT id, status FROM vault");
        while (stmt.step()) vaults.emplace_back(stmt.columnText(0), stmt.columnText(1));
    }

    for (auto& [id, statusAnterior] : vaults) {
        if (!atualizarPresencaDoVault(registro, id)) continue;
        // Só interessa a transição offline -> online: é ela que significa
        // "o disco acabou de ser conectado", o gatilho da varredura.
        if (statusAnterior == "offline") ficaramOnline.push_back(id);
    }
    return ficaramOnline;
}

ResumoReconciliacao varreduraRapida(matriz::db::Database& registro, const std::string& vaultId,
                                     const std::shared_ptr<matriz::app::Cancelamento>& cancelamento,
                                     std::function<void(int, int)> aoProgredir) {
    ResumoReconciliacao resumo;

    std::string localizacao = localizacaoDoVault(registro, vaultId);
    juce::File raiz{juce::String(localizacao)};
    if (localizacao.empty() || !raiz.isDirectory()) return resumo;

    JanelaDeReconciliacao janela(registro);
    auto registrados = lerArquivosDoVault(registro, vaultId);

    // Índice do que está EM DISCO agora, por caminho relativo. Uma passada
    // só pelo volume — reabrir o diretório por arquivo registrado seria
    // O(n·m) e é o que impediria os 100.000 itens do critério 17.
    std::map<std::string, juce::File> emDisco;
    for (const auto& entrada : juce::RangedDirectoryIterator(raiz, true, "*", juce::File::findFiles)) {
        if (cancelamento && cancelamento->pedido()) {
            resumo.cancelada = true;
            return resumo;
        }
        juce::File f = entrada.getFile();
        if (ehIgnoravel(f)) continue;
        emDisco[f.getRelativePathFrom(raiz).toStdString()] = f;
    }

    const int total = static_cast<int>(registrados.size());
    std::set<std::string> caminhosCasados;

    // Passe 1 — quem continua no lugar de sempre.
    std::vector<ArquivoRegistrado> semCaminho;
    for (const auto& reg : registrados) {
        if (cancelamento && cancelamento->pedido()) {
            resumo.cancelada = true;
            return resumo;
        }
        ++resumo.verificados;
        if (aoProgredir) aoProgredir(resumo.verificados, total);

        auto it = emDisco.find(reg.caminhoRelativo);
        if (it == emDisco.end()) {
            semCaminho.push_back(reg);
            continue;
        }
        caminhosCasados.insert(reg.caminhoRelativo);

        // A comparação barata: tamanho. Igual => quase certamente o mesmo
        // conteúdo, e reler os bytes de um acervo inteiro pra confirmar é
        // exatamente o "recálculo inútil de hashes" que o critério 17
        // proíbe. Quem quiser a certeza byte a byte roda a verificação
        // completa, que existe pra isso.
        if (it->second.getSize() == reg.tamanhoBytes) {
            if (reg.estadoPresenca != "presente") definirEstadoPresenca(registro, reg.arquivoId, "presente", true);
            else
                registro.run("UPDATE arquivo SET visto_pela_ultima_vez = ? WHERE id = ?",
                             {Value::of(matriz::model::agoraIso8601()), Value::of(reg.arquivoId)});
            continue;
        }

        // Tamanho mudou: aí sim vale reler e registrar a mudança.
        auto novo = matriz::ingest::calcularChecksums(it->second);
        if (novo.sha256 == reg.sha256) {
            definirEstadoPresenca(registro, reg.arquivoId, "presente", true);
            continue;
        }

        registrarProveniencia(registro, reg.itemId, "arquivo_alterado", kAutorSistema,
                              "sha256 " + (reg.sha256.empty() ? std::string("(unknown)") : reg.sha256) +
                                  " -> " + novo.sha256 + " em " + reg.caminhoRelativo);
        registro.run(
            "UPDATE arquivo SET checksum_sha256 = ?, checksum_md5 = ?, checksum_gerado_em = ?, tamanho_bytes = ?, "
            "estado_presenca = 'alterado', visto_pela_ultima_vez = ? WHERE id = ?",
            {Value::of(novo.sha256), Value::of(novo.md5), Value::of(matriz::model::agoraIso8601()),
             Value::of(static_cast<long long>(it->second.getSize())),
             Value::of(matriz::model::agoraIso8601()), Value::of(reg.arquivoId)});
        ++resumo.alterados;
    }

    // Passe 2 — quem sumiu do caminho registrado pode ter sido MOVIDO. Só
    // aqui vale gastar hash: o candidato é um arquivo não casado com o mesmo
    // tamanho, e são poucos.
    std::map<long long, std::vector<std::string>> naoCasadosPorTamanho;
    for (const auto& [caminho, f] : emDisco) {
        if (caminhosCasados.count(caminho) > 0) continue;
        naoCasadosPorTamanho[f.getSize()].push_back(caminho);
    }

    for (const auto& reg : semCaminho) {
        if (cancelamento && cancelamento->pedido()) {
            resumo.cancelada = true;
            return resumo;
        }

        bool reencontrado = false;
        if (!reg.sha256.empty()) {
            auto candidatos = naoCasadosPorTamanho.find(reg.tamanhoBytes);
            if (candidatos != naoCasadosPorTamanho.end()) {
                for (const auto& caminho : candidatos->second) {
                    if (caminhosCasados.count(caminho) > 0) continue;
                    auto hash = matriz::ingest::calcularChecksums(emDisco[caminho]);
                    if (hash.sha256 != reg.sha256) continue;

                    // Mesmo conteúdo, caminho novo: atualiza ONDE ele está e
                    // não toca em mais nada. Ficha, marcadores e histórico
                    // continuam ligados ao mesmo arquivo_id — que é o ponto
                    // inteiro do §8.
                    registro.run(
                        "UPDATE arquivo SET caminho_relativo = ?, caminho_absoluto_origem = ?, "
                        "estado_presenca = 'presente', visto_pela_ultima_vez = ? WHERE id = ?",
                        {Value::of(caminho), Value::of(emDisco[caminho].getFullPathName().toStdString()),
                         Value::of(matriz::model::agoraIso8601()), Value::of(reg.arquivoId)});
                    registrarProveniencia(registro, reg.itemId, "arquivo_movido", kAutorSistema,
                                          reg.caminhoRelativo + " -> " + caminho);
                    caminhosCasados.insert(caminho);
                    ++resumo.movidos;
                    reencontrado = true;
                    break;
                }
            }
        }
        if (reencontrado) continue;

        // Não está em lugar nenhum do volume: AUSENTE, nunca apagado. O
        // registro é o que sobrou de conhecimento sobre o arquivo, e é
        // justamente quando o arquivo some que ele vale mais (critério 16).
        if (reg.estadoPresenca != "ausente") {
            definirEstadoPresenca(registro, reg.arquivoId, "ausente", false);
            registrarProveniencia(registro, reg.itemId, "arquivo_ausente", kAutorSistema,
                                  "not found at " + reg.caminhoRelativo);
        }
        ++resumo.ausentes;
    }

    // Passe 3 — o que está no volume sem registro nenhum. Não é catalogado
    // aqui (catalogar é decisão do operador, com escolha de tipo de mídia),
    // só contado, pra o resumo dizer quanto material novo apareceu.
    for (const auto& [caminho, f] : emDisco) {
        juce::ignoreUnused(f);
        if (caminhosCasados.count(caminho) == 0) ++resumo.novos;
    }

    registro.run("UPDATE vault SET visto_em = ? WHERE id = ?",
                 {Value::of(matriz::model::agoraIso8601()), Value::of(vaultId)});
    return resumo;
}

ResumoReconciliacao verificacaoCompleta(matriz::db::Database& registro, const std::string& vaultId,
                                         const std::shared_ptr<matriz::app::Cancelamento>& cancelamento,
                                         std::function<void(int, int)> aoProgredir) {
    ResumoReconciliacao resumo;

    std::string localizacao = localizacaoDoVault(registro, vaultId);
    juce::File raiz{juce::String(localizacao)};
    if (localizacao.empty() || !raiz.isDirectory()) return resumo;

    JanelaDeReconciliacao janela(registro);
    auto registrados = lerArquivosDoVault(registro, vaultId);
    const int total = static_cast<int>(registrados.size());

    for (const auto& reg : registrados) {
        if (cancelamento && cancelamento->pedido()) {
            resumo.cancelada = true;
            return resumo;
        }
        ++resumo.verificados;
        if (aoProgredir) aoProgredir(resumo.verificados, total);

        // Placeholder de nuvem não tem bytes locais: conferir forçaria o
        // download do acervo inteiro (critério 12).
        if (reg.estadoPresenca == "nao_baixado") continue;

        juce::File arquivo = raiz.getChildFile(juce::String(reg.caminhoRelativo));
        if (!arquivo.existsAsFile()) {
            if (reg.estadoPresenca != "ausente") {
                definirEstadoPresenca(registro, reg.arquivoId, "ausente", false);
                registrarProveniencia(registro, reg.itemId, "arquivo_ausente", kAutorSistema,
                                      "not found at " + reg.caminhoRelativo);
            }
            ++resumo.ausentes;
            continue;
        }

        // Sem hash registrado não há o que auditar — grava o de agora como
        // linha de base, em vez de fingir que passou ou que falhou.
        auto atual = matriz::ingest::calcularChecksums(arquivo);
        if (reg.sha256.empty()) {
            registro.run(
                "UPDATE arquivo SET checksum_sha256 = ?, checksum_md5 = ?, checksum_gerado_em = ?, "
                "checksum_verificado_em = ?, estado_presenca = 'presente', visto_pela_ultima_vez = ? WHERE id = ?",
                {Value::of(atual.sha256), Value::of(atual.md5), Value::of(matriz::model::agoraIso8601()),
                 Value::of(matriz::model::agoraIso8601()), Value::of(matriz::model::agoraIso8601()),
                 Value::of(reg.arquivoId)});
            continue;
        }

        if (atual.sha256 == reg.sha256) {
            registro.run(
                "UPDATE arquivo SET checksum_verificado_em = ?, estado_presenca = 'presente', "
                "visto_pela_ultima_vez = ? WHERE id = ?",
                {Value::of(matriz::model::agoraIso8601()), Value::of(matriz::model::agoraIso8601()),
                 Value::of(reg.arquivoId)});
            continue;
        }

        // Bit rot (ou edição por fora): o conteúdo mudou sem que ninguém
        // tenha pedido. O hash antigo vai pra proveniência ANTES de ser
        // substituído — é a única prova de que o arquivo já foi outro.
        auto copias = vaultsComCopiaSa(registro, reg.sha256, vaultId);
        std::string detalhes = "sha256 " + reg.sha256 + " -> " + atual.sha256 + " em " + reg.caminhoRelativo;
        if (!copias.empty()) {
            juce::StringArray nomes;
            for (auto& n : copias) nomes.add(juce::String(n));
            detalhes += " | intact copy at: " + nomes.joinIntoString(", ").toStdString();
        }
        registrarProveniencia(registro, reg.itemId, "arquivo_corrompido", kAutorSistema, detalhes);

        // O checksum REGISTRADO não é sobrescrito: ele é a referência do que
        // o arquivo deveria ser. Sobrescrever aqui apagaria a evidência e
        // faria a próxima verificação passar limpa em cima da corrupção.
        registro.run(
            "UPDATE arquivo SET estado_presenca = 'corrompido', checksum_verificado_em = ?, "
            "visto_pela_ultima_vez = ? WHERE id = ?",
            {Value::of(matriz::model::agoraIso8601()), Value::of(matriz::model::agoraIso8601()),
             Value::of(reg.arquivoId)});
        ++resumo.corrompidos;
    }

    registro.run("UPDATE vault SET verificado_em = ?, visto_em = ? WHERE id = ?",
                 {Value::of(matriz::model::agoraIso8601()), Value::of(matriz::model::agoraIso8601()),
                  Value::of(vaultId)});
    return resumo;
}

std::vector<std::string> vaultsComCopiaSa(matriz::db::Database& registro, const std::string& checksumSha256,
                                            const std::string& vaultIdExcluir) {
    std::vector<std::string> out;
    if (checksumSha256.empty()) return out;

    auto stmt = registro.prepare(
        "SELECT DISTINCT v.nome FROM arquivo a JOIN vault v ON v.id = a.vault_id "
        "WHERE a.checksum_sha256 = ? AND a.vault_id <> ? AND a.estado_presenca = 'presente'");
    stmt.bind(1, Value::of(checksumSha256));
    stmt.bind(2, Value::of(vaultIdExcluir));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

} // namespace matriz::vault
