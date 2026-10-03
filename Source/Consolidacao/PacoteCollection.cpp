#include "PacoteCollection.h"

#include <cmath>
#include <map>

#include "../Ingest/Checksum.h"
#include "../Model/NomesCanonicos.h"
#include "../Model/Project.h"
#include "../Model/ProjectLog.h"
#include "../Vault/Resolucao.h"

namespace matriz::consolidacao::pacote {

using matriz::db::Value;

namespace {

// Colunas de asset_geolocation que viajam (as de controle ficam).
const char* const kColunasGeo[] = {
    "latitude", "longitude", "altitude", "continent", "country", "country_code", "state_province", "state_code",
    "city", "municipality", "neighborhood", "district", "postal_code", "street", "street_number", "locality",
    "formatted_address", "source", "precision_accuracy", "confidence"};

// Mesma regra de ProjetoAberto::lerMetadado: coluna do item, senão item_campo.
std::string lerCampo(matriz::db::Database& registro, const std::string& itemId, const std::string& coluna) {
    {
        auto st = registro.prepare("SELECT COALESCE(" + coluna + ", '') FROM item WHERE id = ?");
        st.bind(1, Value::of(itemId));
        if (st.step()) {
            std::string v = st.columnText(0);
            if (!juce::String::fromUTF8(v.c_str()).trim().isEmpty()) return v;
        }
    }
    auto st = registro.prepare("SELECT COALESCE(valor, '') FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
    st.bind(1, Value::of(itemId));
    st.bind(2, Value::of(coluna));
    if (st.step()) {
        std::string v = st.columnText(0);
        if (!juce::String::fromUTF8(v.c_str()).trim().isEmpty()) return v;
    }
    return {};
}

juce::var listaJson(const std::vector<std::string>& v) {
    juce::Array<juce::var> arr;
    for (const auto& s : v) arr.add(juce::String::fromUTF8(s.c_str()));
    return arr;
}

std::vector<std::string> listaDeJson(const juce::var& v) {
    std::vector<std::string> out;
    if (auto* arr = v.getArray())
        for (const auto& e : *arr) {
            auto s = e.toString().trim();
            if (s.isNotEmpty()) out.push_back(s.toStdString());
        }
    return out;
}

std::string texto(const juce::var& obj, const char* chave) {
    return obj.getProperty(chave, {}).toString().trim().toStdString();
}

std::string sha256Registrado(matriz::db::Database& registro, const std::string& arquivoId) {
    auto st = registro.prepare("SELECT COALESCE(checksum_sha256, '') FROM arquivo WHERE id = ?");
    st.bind(1, Value::of(arquivoId));
    return st.step() ? juce::String(st.columnText(0)).toLowerCase().toStdString() : std::string();
}

}  // namespace

bool DadosFicha::vazio() const {
    return titulo.empty() && descricao.empty() && eventDate.empty() && content.empty() && subjects.empty() &&
           tags.empty() && pessoas.empty() && !geo.isObject() && marcadores.empty();
}

std::set<std::string> chavesDaListaPessoas(matriz::db::Database& registro) {
    std::set<std::string> out;
    try {
        auto st = registro.prepare("SELECT nome FROM collection_person");
        while (st.step()) out.insert(matriz::model::nomes::chave(st.columnText(0)));
    } catch (...) {}  // projeto sem a tabela: ninguém é pessoa
    return out;
}

DadosFicha lerDadosFicha(matriz::db::Database& registro, const std::string& itemId,
                         const std::set<std::string>& chavesPessoas) {
    DadosFicha d;
    d.titulo = lerCampo(registro, itemId, "dc_title");
    d.descricao = lerCampo(registro, itemId, "dc_description");
    d.eventDate = lerCampo(registro, itemId, "ano");
    d.content = lerCampo(registro, itemId, "collection_type");
    d.subjects = matriz::model::nomes::dividirSubjects(lerCampo(registro, itemId, "dc_subject"));
    {
        auto st = registro.prepare("SELECT tag FROM item_tag WHERE item_id = ? ORDER BY rowid");
        st.bind(1, Value::of(itemId));
        while (st.step()) {
            std::string t = juce::String::fromUTF8(st.columnText(0).c_str()).trim().toStdString();
            if (t.empty()) continue;
            (chavesPessoas.count(matriz::model::nomes::chave(t)) ? d.pessoas : d.tags).push_back(t);
        }
    }
    {
        std::string cols;
        for (auto* c : kColunasGeo) cols += (cols.empty() ? "" : ", ") + std::string(c);
        auto st = registro.prepare("SELECT " + cols + " FROM asset_geolocation WHERE asset_id = ?");
        st.bind(1, Value::of(itemId));
        if (st.step()) {
            auto* obj = new juce::DynamicObject();
            for (int i = 0; i < static_cast<int>(std::size(kColunasGeo)); ++i) {
                if (st.columnIsNull(i)) continue;
                const juce::String nome(kColunasGeo[i]);
                if (nome == "latitude" || nome == "longitude" || nome == "altitude" || nome == "precision_accuracy" ||
                    nome == "confidence") {
                    obj->setProperty(nome, st.columnReal(i));
                } else {
                    auto v = juce::String::fromUTF8(st.columnText(i).c_str()).trim();
                    if (v.isEmpty() || (nome == "source" && v == "NONE")) continue;
                    obj->setProperty(nome, v);
                }
            }
            if (obj->getProperties().size() > 0) d.geo = juce::var(obj);
            else delete obj;
        }
    }
    {
        auto st = registro.prepare("SELECT COALESCE(texto, ''), minutagem_ms, COALESCE(titulo, '') FROM item_observacao "
                                   "WHERE item_id = ? ORDER BY criado_em, rowid");
        st.bind(1, Value::of(itemId));
        while (st.step()) {
            Marcador m;
            m.texto = st.columnText(0);
            if (!st.columnIsNull(1)) m.tempoS = static_cast<double>(st.columnInt(1)) / 1000.0;
            m.titulo = st.columnText(2);
            if (m.texto.empty() && m.titulo.empty() && !m.tempoS) continue;
            d.marcadores.push_back(std::move(m));
        }
    }
    return d;
}

void escreverJson(const DadosFicha& d, juce::DynamicObject& o) {
    auto str = [&](const char* chave, const std::string& v) {
        if (!v.empty()) o.setProperty(chave, juce::String::fromUTF8(v.c_str()));
    };
    str("titulo", d.titulo);
    str("descricao", d.descricao);
    str("event_date", d.eventDate);
    str("content", d.content);
    if (!d.subjects.empty()) o.setProperty("subjects", listaJson(d.subjects));
    if (!d.tags.empty()) o.setProperty("tags", listaJson(d.tags));
    if (!d.pessoas.empty()) o.setProperty("pessoas", listaJson(d.pessoas));
    if (d.geo.isObject()) o.setProperty("geo", d.geo);
    if (!d.marcadores.empty()) {
        juce::Array<juce::var> arr;
        for (const auto& m : d.marcadores) {
            auto* mo = new juce::DynamicObject();
            if (m.tempoS) mo->setProperty("tempo_s", *m.tempoS);
            if (!m.texto.empty()) mo->setProperty("texto", juce::String::fromUTF8(m.texto.c_str()));
            if (!m.titulo.empty()) mo->setProperty("titulo", juce::String::fromUTF8(m.titulo.c_str()));
            arr.add(juce::var(mo));
        }
        o.setProperty("marcadores", arr);
    }
}

DadosFicha lerJson(const juce::var& r) {
    DadosFicha d;
    d.titulo = texto(r, "titulo");
    d.descricao = texto(r, "descricao");
    d.eventDate = texto(r, "event_date");
    d.content = texto(r, "content");
    d.subjects = listaDeJson(r.getProperty("subjects", {}));
    d.tags = listaDeJson(r.getProperty("tags", {}));
    d.pessoas = listaDeJson(r.getProperty("pessoas", {}));
    if (auto g = r.getProperty("geo", {}); g.isObject() && g.getDynamicObject()->getProperties().size() > 0) d.geo = g;
    if (auto* arr = r.getProperty("marcadores", {}).getArray())
        for (const auto& e : *arr) {
            if (!e.isObject()) continue;
            Marcador m;
            if (e.hasProperty("tempo_s")) m.tempoS = static_cast<double>(e.getProperty("tempo_s", 0.0));
            m.texto = e.getProperty("texto", {}).toString().toStdString();
            m.titulo = e.getProperty("titulo", {}).toString().toStdString();
            if (m.texto.empty() && m.titulo.empty() && !m.tempoS) continue;
            d.marcadores.push_back(std::move(m));
        }
    return d;
}

ResultadoPacote gerarPacote(matriz::db::Database& registro, const juce::File& pastaProjeto,
                            const juce::File& destinoPai, const juce::String& nomePacote,
                            const std::string& mapaId, const juce::String& nomeMapa,
                            const juce::String& nomeColecao, const std::set<std::string>& itens,
                            const AoProgredir& aoProgredir) {
    ResultadoPacote r;
    juce::String nomeSeguro = juce::File::createLegalFileName(nomePacote.trim());
    if (nomeSeguro.isEmpty()) nomeSeguro = "Matriz package";
    // Nome já usado: "Nome (2)", "Nome (3)"... (mesma regra do folder map no INTAKE).
    r.pasta = destinoPai.getChildFile(nomeSeguro);
    for (int n = 2; r.pasta.exists(); ++n) r.pasta = destinoPai.getChildFile(nomeSeguro + " (" + juce::String(n) + ")");
    const juce::File media = r.pasta.getChildFile("Media");

    // Mesmo planner do EXPORT por folder map: estrutura = pastas do mapa, sem
    // prefixo (nomes como no MAIN), sem _SEM_PASTA — sem pasta fica de fora.
    auto plano = planejarConsolidacao(registro, pastaProjeto, media, {NivelHierarquia::PastaManual}, {},
                                      ModoPrefixoArquivo::Nenhum, {}, /*autoResolver*/ true, false, false,
                                      /*paraExport*/ true, mapaId);
    for (const auto& id : plano.semPastaExcluidos)
        if (itens.count(id)) ++r.semPasta;
    std::vector<ItemPlanejado> doRecorte;
    for (auto& ip : plano.itens)
        if (itens.count(ip.itemId)) doRecorte.push_back(std::move(ip));
    if (doRecorte.empty()) return r;

    // Pastas do mapa: id -> (nome, pai, ordem).
    struct Pasta { juce::String nome; std::string pai; long long ordem = 0; };
    std::map<std::string, Pasta> pastas;
    {
        auto st = registro.prepare("SELECT id, nome, COALESCE(pasta_pai_id, ''), ordem FROM acervo_pasta WHERE mapa_id = ?");
        st.bind(1, Value::of(mapaId));
        while (st.step())
            pastas[st.columnText(0)] = {juce::String::fromUTF8(st.columnText(1).c_str()), st.columnText(2), st.columnInt(3)};
    }
    auto caminhoDaPasta = [&](std::string id) {
        juce::StringArray nomes;
        for (int guarda = 0; !id.empty() && guarda < 256; ++guarda) {
            auto it = pastas.find(id);
            if (it == pastas.end()) break;
            nomes.insert(0, it->second.nome);
            id = it->second.pai;
        }
        return nomes;
    };

    const auto chavesPessoas = chavesDaListaPessoas(registro);
    std::set<std::string> pastasUsadas;
    juce::Array<juce::var> arquivos;
    const int total = static_cast<int>(doRecorte.size());
    int feito = 0;
    for (const auto& ip : doRecorte) {
        if (aoProgredir && !aoProgredir(feito, total)) {
            r.cancelado = true;
            break;
        }
        ++feito;
        if (ip.emConflito) {
            r.falhas.push_back(ip.codigoAcervo + ": name conflict at destination");
            continue;
        }
        juce::File destino;
        try {
            // Sempre do MAIN: item que só existe no SOURCE fica de fora.
            auto noMain = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto);
            auto naOrigem = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto,
                                                           matriz::vault::Preferencia::Origem);
            if (!noMain || (naOrigem && *naOrigem == *noMain)) {
                ++r.foraDoMain;
                continue;
            }
            destino = media.getChildFile(ip.caminhoRelativoDestino);
            if (destino.existsAsFile())
                throw std::runtime_error("already exists in the package: " + destino.getFullPathName().toStdString());
            destino.getParentDirectory().createDirectory();
            if (!noMain->copyFileTo(destino) || destino.getSize() != noMain->getSize())
                throw std::runtime_error("copy failed: " + destino.getFullPathName().toStdString());
            // Cópia verificada: o SHA-256 do arquivo entregue é o que vai no
            // JSON, e tem que bater com o registrado (ou com o do MAIN).
            const std::string sha = juce::String(matriz::ingest::calcularChecksums(destino).sha256).toLowerCase().toStdString();
            std::string esperado = sha256Registrado(registro, ip.arquivoId);
            if (esperado.empty())
                esperado = juce::String(matriz::ingest::calcularChecksums(*noMain).sha256).toLowerCase().toStdString();
            if (sha.empty() || sha != esperado)
                throw std::runtime_error("checksum mismatch after copy: " + destino.getFullPathName().toStdString());

            auto* reg = new juce::DynamicObject();
            reg->setProperty("sha256", juce::String(sha));
            reg->setProperty("caminho", juce::String(ip.caminhoRelativoDestino).replaceCharacter('\\', '/'));
            const auto nomesPasta = caminhoDaPasta(ip.pastaId);
            if (!nomesPasta.isEmpty()) {
                juce::Array<juce::var> arr;
                for (const auto& n : nomesPasta) arr.add(n);
                reg->setProperty("pasta", arr);
                for (std::string id = ip.pastaId; !id.empty() && pastas.count(id); id = pastas[id].pai)
                    if (!pastasUsadas.insert(id).second) break;
            }
            escreverJson(lerDadosFicha(registro, ip.itemId, chavesPessoas), *reg);
            arquivos.add(juce::var(reg));
            ++r.copiados;
        } catch (const std::exception& e) {
            if (destino != juce::File() && destino.existsAsFile()) destino.deleteFile();
            r.falhas.push_back(ip.codigoAcervo + ": " + e.what());
        }
    }

    // Cancelado ou sem nenhum arquivo entregue: o pacote não serve pra nada
    // (e não tem JSON) — a pasta, criada agora por este export, sai.
    if (r.cancelado || r.copiados == 0) {
        r.pasta.deleteRecursively();
        return r;
    }

    // Árvore de pastas: só as que têm itens do pacote (e seus ancestrais).
    std::function<juce::Array<juce::var>(const std::string&)> filhosDe = [&](const std::string& pai) {
        std::vector<std::pair<const std::string*, const Pasta*>> filhos;
        for (const auto& [id, p] : pastas)
            if (p.pai == pai && pastasUsadas.count(id)) filhos.push_back({&id, &p});
        std::sort(filhos.begin(), filhos.end(), [](const auto& a, const auto& b) {
            return a.second->ordem != b.second->ordem ? a.second->ordem < b.second->ordem
                                                      : a.second->nome.compareNatural(b.second->nome) < 0;
        });
        juce::Array<juce::var> out;
        for (const auto& [id, p] : filhos) {
            auto* no = new juce::DynamicObject();
            no->setProperty("nome", p->nome);
            auto sub = filhosDe(*id);
            if (!sub.isEmpty()) no->setProperty("pastas", sub);
            out.add(juce::var(no));
        }
        return out;
    };

    auto* raiz = new juce::DynamicObject();
    raiz->setProperty("formato", kFormato);
    raiz->setProperty("versao", kVersao);
    raiz->setProperty("colecao", nomeColecao);
    raiz->setProperty("exportado_em", juce::String(matriz::model::agoraIso8601()));
    raiz->setProperty("folder_map", nomeMapa);
    raiz->setProperty("pastas", filhosDe({}));
    raiz->setProperty("arquivos", arquivos);
    const juce::var json(raiz);
    const juce::File tmp = r.pasta.getChildFile(juce::String(kArquivoJson) + ".tmp");
    if (!tmp.replaceWithText(juce::JSON::toString(json, false)) ||
        !tmp.moveFileTo(r.pasta.getChildFile(kArquivoJson))) {
        r.falhas.push_back("could not write " + std::string(kArquivoJson));
    }

    try {
        juce::StringArray linhas;
        linhas.add("Package: " + r.pasta.getFullPathName());
        linhas.add("Folder map: " + nomeMapa);
        linhas.add("Files: " + juce::String(r.copiados) + " (without a folder in the map, skipped: " +
                   juce::String(r.semPasta) + "; not yet in MAIN, skipped: " + juce::String(r.foraDoMain) +
                   "; failures: " + juce::String((int) r.falhas.size()) + ")");
        matriz::model::ProjectLog(pastaProjeto).appendEntry("Collection Package Exported", linhas);
    } catch (...) {}
    return r;
}

// ---------------------------------------------------------------------------
// INTAKE (Fase 3)
// ---------------------------------------------------------------------------

bool pareceUmPacote(const juce::File& pasta) {
    return pasta.isDirectory() && pasta.getChildFile(kArquivoJson).existsAsFile();
}

ResultadoLeitura lerPacote(const juce::File& pasta) {
    ResultadoLeitura r;
    if (!pareceUmPacote(pasta)) return r;
    r.status = StatusLeitura::Invalido;
    juce::var json;
    const auto parse = juce::JSON::parse(pasta.getChildFile(kArquivoJson).loadFileAsString(), json);
    if (parse.failed() || !json.isObject()) {
        r.erro = parse.failed() ? parse.getErrorMessage() : juce::String("not a JSON object");
        return r;
    }
    if (json.getProperty("formato", {}).toString() != kFormato) {
        r.erro = "\"formato\" is not \"" + juce::String(kFormato) + "\"";
        return r;
    }
    const auto versao = json.getProperty("versao", {});
    if (!(versao.isInt() || versao.isInt64() || versao.isDouble()) || static_cast<int>(versao) != kVersao) {
        r.status = StatusLeitura::VersaoDesconhecida;
        r.erro = versao.toString();
        return r;
    }
    Pacote& p = r.pacote;
    p.pasta = pasta;
    if (!p.media().isDirectory()) {
        r.erro = "Media/ folder is missing";
        return r;
    }
    p.colecao = json.getProperty("colecao", {}).toString();
    p.folderMap = json.getProperty("folder_map", {}).toString();
    p.exportadoEm = json.getProperty("exportado_em", {}).toString();
    p.pastas = json.getProperty("pastas", {});
    if (auto* arr = json.getProperty("arquivos", {}).getArray()) {
        for (const auto& e : *arr) {
            if (!e.isObject()) continue;
            RegistroArquivo ra;
            ra.sha256 = e.getProperty("sha256", {}).toString().trim().toLowerCase().toStdString();
            ra.caminho = e.getProperty("caminho", {}).toString().replaceCharacter('\\', '/').toStdString();
            if (auto* pa = e.getProperty("pasta", {}).getArray())
                for (const auto& n : *pa)
                    if (n.toString().trim().isNotEmpty()) ra.pasta.push_back(n.toString().trim());
            ra.dados = lerJson(e);
            if (!ra.sha256.empty()) p.arquivos.push_back(std::move(ra));
        }
    }
    r.status = StatusLeitura::Ok;
    return r;
}

void gravarDadosFicha(matriz::db::Database& registro, const std::string& itemId, const DadosFicha& d,
                      matriz::model::nomes::Vocabulario& vocabTags, matriz::model::nomes::Vocabulario& vocabSubjects,
                      const std::string& autor) {
    const std::string agora = matriz::model::agoraIso8601();
    // Mesmo par de escritas de ProjetoAberto::salvarMetadado: coluna do item
    // + espelho em item_campo.
    auto gravar = [&](const char* coluna, const std::string& valor) {
        if (valor.empty()) return;
        try {
            if (std::string(coluna) == "dc_title") {
                // O título do pacote vai nas DUAS colunas (como em importarSidecarsEditados): item.titulo é o que a grade
                // mostra e item.dc_title é o campo Title da ficha. Só titulo deixava a ficha sem título após o INTAKE.
                registro.run("UPDATE item SET titulo = ?, dc_title = ?, atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                             {Value::of(valor), Value::of(valor), Value::of(agora), Value::of(itemId)});
            } else {
                registro.run(std::string("UPDATE item SET ") + coluna + " = ?, atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                             {Value::of(valor), Value::of(agora), Value::of(itemId)});
            }
        } catch (...) {
            try {
                registro.run("UPDATE item SET atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                             {Value::of(agora), Value::of(itemId)});
            } catch (...) {}
        }
        try {
            registro.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                         "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
                         "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, "
                         "fonte = 'humano', atualizado_em = excluded.atualizado_em",
                         {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(std::string(coluna)),
                          Value::of(valor), Value::of(agora)});
        } catch (...) {}
    };
    gravar("dc_title", d.titulo);
    gravar("dc_description", d.descricao);
    gravar("ano", d.eventDate);
    gravar("collection_type", d.content);
    if (!d.subjects.empty()) {
        std::string lista;
        for (const auto& s : d.subjects) lista += (lista.empty() ? "" : ", ") + s;
        gravar("dc_subject", vocabSubjects.listaSubjects(lista));
    }

    auto tag = [&](const std::string& t) {
        const std::string canon = vocabTags.canonico(t);
        if (canon.empty()) return std::string();
        registro.run("INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                     {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(canon)});
        return canon;
    };
    for (const auto& t : d.tags) tag(t);
    if (!d.pessoas.empty()) {
        registro.exec("CREATE TABLE IF NOT EXISTS collection_person (id TEXT PRIMARY KEY, nome TEXT NOT NULL UNIQUE, "
                      "criado_em TEXT NOT NULL)");
        for (const auto& p : d.pessoas) {
            const std::string canon = tag(p);
            if (canon.empty()) continue;
            registro.run("INSERT OR IGNORE INTO collection_person (id, nome, criado_em) VALUES (?, ?, ?)",
                         {Value::of(matriz::model::novoUuid()), Value::of(canon), Value::of(agora)});
        }
    }

    // Geo: os campos do pacote valem por cima de uma leitura automática (GPS
    // do EXIF) do item recém-ingerido; o que o pacote não traz fica.
    if (auto* g = d.geo.getDynamicObject()) {
        std::string cols = "asset_id", marcas = "?", atualiza;
        std::vector<Value> vals{Value::of(itemId)};
        for (auto* c : kColunasGeo) {
            if (!g->hasProperty(c)) continue;
            const auto v = g->getProperty(c);
            cols += std::string(", ") + c;
            marcas += ", ?";
            atualiza += std::string(atualiza.empty() ? "" : ", ") + c + " = excluded." + c;
            if (v.isDouble() || v.isInt() || v.isInt64()) vals.push_back(Value::of(static_cast<double>(v)));
            else vals.push_back(Value::of(v.toString().toStdString()));
        }
        if (!atualiza.empty()) {
            cols += ", created_at, updated_at";
            marcas += ", ?, ?";
            vals.push_back(Value::of(agora));
            vals.push_back(Value::of(agora));
            registro.run("INSERT INTO asset_geolocation (" + cols + ") VALUES (" + marcas + ") "
                         "ON CONFLICT(asset_id) DO UPDATE SET " + atualiza + ", updated_at = excluded.updated_at",
                         vals);
        }
    }

    for (const auto& m : d.marcadores) {
        registro.run("INSERT INTO item_observacao (id, item_id, texto, autor, criado_em, minutagem_ms, titulo) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?)",
                     {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(m.texto), Value::of(autor),
                      Value::of(agora),
                      m.tempoS ? Value::of(static_cast<long long>(std::llround(*m.tempoS * 1000.0))) : Value::null(),
                      m.titulo.empty() ? Value::null() : Value::of(m.titulo)});
    }
}

}  // namespace matriz::consolidacao::pacote
