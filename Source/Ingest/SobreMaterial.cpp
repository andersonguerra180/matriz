#include "SobreMaterial.h"

#include <JuceHeader.h>

#include <algorithm>
#include <set>

#include "../Ficha/AutocompleteHistorico.h"
#include "../Model/NomesCanonicos.h"
#include "../Model/NotasEstruturadas.h"
#include "../Model/Project.h"

namespace matriz::ingest {

using matriz::db::Value;

std::shared_ptr<const SobreMaterial> comTagAutomatica(std::shared_ptr<const SobreMaterial> sobre, const std::string& tag) {
    auto copia = sobre != nullptr ? std::make_shared<SobreMaterial>(*sobre) : std::make_shared<SobreMaterial>();
    const bool tem = std::any_of(copia->tags.begin(), copia->tags.end(),
                                 [&](const std::string& x) { return juce::String(x).trim().equalsIgnoreCase(juce::String(tag)); });
    if (!tem) copia->tags.push_back(tag);
    return copia;
}

struct AplicadorSobreMaterial::Impl {
    matriz::db::Database& db;
    SobreMaterial sobre;
    matriz::model::nomes::Vocabulario tagsVocab;
    std::vector<std::string> etiquetas;      // pessoas + tags, já na grafia do projeto, sem repetir
    std::vector<std::string> subjectsNovos;  // dividido e canônico

    Impl(matriz::db::Database& d, SobreMaterial s) : db(d), sobre(std::move(s)) {}
};

AplicadorSobreMaterial::AplicadorSobreMaterial(matriz::db::Database& registro, SobreMaterial sobre)
    : impl_(std::make_unique<Impl>(registro, std::move(sobre))) {
    auto& im = *impl_;
    using namespace matriz::model::nomes;
    try {
        im.tagsVocab = Vocabulario::carregar(im.db, Vocabulario::Tipo::Tags);
        auto subjects = Vocabulario::carregar(im.db, Vocabulario::Tipo::Subjects);

        // Pessoas entram na lista da coleção (o seletor PEOPLE oferece) E viram tag no item.
        im.db.execScript("CREATE TABLE IF NOT EXISTS collection_person (id TEXT PRIMARY KEY, nome TEXT NOT NULL UNIQUE, criado_em TEXT NOT NULL);");
        std::set<std::string> vistos;
        const auto juntar = [&](const std::string& bruto, bool pessoa) {
            const std::string aparado = juce::String(bruto).trim().trimCharactersAtStart("#").trim().toStdString();
            if (aparado.empty()) return;
            const std::string canon = im.tagsVocab.canonico(aparado);
            if (!vistos.insert(chave(canon)).second) return;
            im.etiquetas.push_back(canon);
            if (pessoa) {
                try {
                    im.db.run("INSERT OR IGNORE INTO collection_person (id, nome, criado_em) VALUES (?, ?, ?)",
                              {Value::of(matriz::model::novoUuid()), Value::of(canon), Value::of(matriz::model::agoraIso8601())});
                } catch (const std::exception& e) {
                    juce::Logger::writeToLog("SobreMaterial: pessoa não registrada — " + juce::String(e.what()));
                }
            }
        };
        for (const auto& p : im.sobre.pessoas) juntar(p, true);
        for (const auto& t : im.sobre.tags) juntar(t, false);

        for (const auto& s : dividirSubjects(im.sobre.subject)) {
            const std::string canon = subjects.canonico(s);
            if (!canon.empty()) im.subjectsNovos.push_back(canon);
        }
        for (const auto& s : im.subjectsNovos)
            matriz::ficha::AutocompleteRepository::registrar(im.db, "dc_subject", s);
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("SobreMaterial: preparo falhou — " + juce::String(e.what()));
    }
}

AplicadorSobreMaterial::~AplicadorSobreMaterial() = default;

namespace {

void upsertCampoHumano(matriz::db::Database& db, const std::string& itemId, const std::string& campo, const std::string& valor,
                       bool sobrescrever) {
    // sobrescrever=false: se o arquivo já tinha o campo (fonte leitura_tecnica) o original fica como está.
    db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
           "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) " +
               std::string(sobrescrever ? "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, "
                                          "fonte = 'humano', atualizado_em = excluded.atualizado_em"
                                        : "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO NOTHING"),
           {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(campo), Value::of(valor),
            Value::of(matriz::model::agoraIso8601())});
}

std::string lerColuna(matriz::db::Database& db, const std::string& itemId, const std::string& coluna) {
    auto st = db.prepare("SELECT " + coluna + " FROM item WHERE id = ?");
    st.bind(1, Value::of(itemId));
    if (st.step() && !st.columnIsNull(0)) return st.columnText(0);
    return {};
}

std::string lerCampo(matriz::db::Database& db, const std::string& itemId, const std::string& campo) {
    auto st = db.prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
    st.bind(1, Value::of(itemId));
    st.bind(2, Value::of(campo));
    if (st.step() && !st.columnIsNull(0)) return st.columnText(0);
    return {};
}

} // namespace

void AplicadorSobreMaterial::aplicar(const std::string& itemId, const std::string* descricaoDoItem) {
    auto& im = *impl_;
    auto& db = im.db;
    const auto& sm = im.sobre;
    const std::string& descricao = descricaoDoItem != nullptr ? *descricaoDoItem : sm.descricao;
    if ((sm.vazio() && descricao.empty()) || itemId.empty()) return;
    using namespace matriz::model::nomes;
    const std::string agora = matriz::model::agoraIso8601();

    // Um SAVEPOINT por item: todas as escritas deste item entram ou saem juntas (e funciona dentro de uma
    // transação já aberta pelo chamador).
    bool savepointAberto = false;
    try {
        db.run("SAVEPOINT sobre_material", {});
        savepointAberto = true;

        if (!sm.sufixoTitulo.empty()) {
            const std::string titulo = lerColuna(db, itemId, "titulo");
            const std::string fim = " - " + sm.sufixoTitulo;
            if (titulo.size() < fim.size() || titulo.compare(titulo.size() - fim.size(), fim.size(), fim) != 0)
                db.run("UPDATE item SET titulo = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                       {Value::of(titulo.empty() ? sm.sufixoTitulo : titulo + fim), Value::of(agora), Value::of(itemId)});
        }

        if (!sm.collectionType.empty()) {
            db.run("UPDATE item SET collection_type = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                   {Value::of(sm.collectionType), Value::of(agora), Value::of(itemId)});
            upsertCampoHumano(db, itemId, "collection_type", sm.collectionType, true);
        }

        if (!sm.camposDoVideo.empty() && lerColuna(db, itemId, "tipo_midia") == "digital_video") {
            for (const auto& [campo, valor] : sm.camposDoVideo) {
                if (valor.empty()) continue;
                if (campo == "dc_title" || campo == "dc_description")  // Dublin Core do próprio item
                    db.run("UPDATE item SET " + campo + " = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                           {Value::of(valor), Value::of(agora), Value::of(itemId)});
                else
                    upsertCampoHumano(db, itemId, campo, valor, true);
            }
        }

        for (const auto& [campo, valor] : sm.campos) {
            if (campo.empty() || valor.empty()) continue;
            if (campo == "source_media") {  // mídia de origem: coluna do próprio item (JSON)
                db.run("UPDATE item SET source_media = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                       {Value::of(valor), Value::of(agora), Value::of(itemId)});
            } else if (campo == "lista") {  // LIST: coluna do próprio item
                db.run("UPDATE item SET lista = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                       {Value::of(valor), Value::of(agora), Value::of(itemId)});
            } else if (campo == "ano") {    // ano: só preenche se o arquivo ainda não tem (a data do arquivo prevalece)
                db.run("UPDATE item SET ano = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ? AND (ano IS NULL OR ano = 0)",
                       {Value::of(juce::String(valor).getIntValue()), Value::of(agora), Value::of(itemId)});
            } else {
                // O que o arquivo já traz (formato, origem, aparelho de gravação) fica; a ficha só preenche o que falta.
                const bool respeitaArquivo = campo == "dc_format" || campo == "dc_source" || campo == "recording_device";
                upsertCampoHumano(db, itemId, campo, valor, !respeitaArquivo);
            }
        }

        // Localização padrão: só entra se o item ainda não tem geolocalização (a do arquivo e a do usuário prevalecem;
        // quem grava depois usa upsert e substitui esta).
        if (!sm.geoPadrao.vazio()) {
            auto st = db.prepare("SELECT 1 FROM asset_geolocation WHERE asset_id = ?");
            st.bind(1, Value::of(itemId));
            if (!st.step()) {
                const bool temCoord = sm.geoPadrao.lat.has_value() && sm.geoPadrao.lng.has_value();
                const auto texto = [](const std::string& v) { return v.empty() ? Value::null() : Value::of(v); };
                db.run("INSERT OR IGNORE INTO asset_geolocation (asset_id, latitude, longitude, country, state_province, city, formatted_address, source) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                       {Value::of(itemId), temCoord ? Value::of(*sm.geoPadrao.lat) : Value::null(), temCoord ? Value::of(*sm.geoPadrao.lng) : Value::null(),
                        texto(sm.geoPadrao.pais), texto(sm.geoPadrao.estado), texto(sm.geoPadrao.cidade), texto(sm.geoPadrao.endereco),
                        Value::of(std::string(temCoord ? "USER_COORDINATES" : sm.geoPadrao.cidade.empty() ? (sm.geoPadrao.estado.empty() ? "USER_COUNTRY" : "USER_STATE") : "USER_CITY"))});
            }
        }

        if (!im.subjectsNovos.empty()) {
            // O assunto que o arquivo já traz (item.dc_subject ou leitura técnica) fica; o novo soma.
            std::string atual = lerColuna(db, itemId, "dc_subject");
            if (atual.empty()) atual = lerCampo(db, itemId, "dc_subject");
            std::vector<std::string> todos = dividirSubjects(atual);
            std::set<std::string> chaves;
            for (const auto& s : todos) chaves.insert(chave(s));
            for (const auto& s : im.subjectsNovos)
                if (chaves.insert(chave(s)).second) todos.push_back(s);
            std::string junto;
            for (size_t i = 0; i < todos.size(); ++i) junto += (i ? ", " : "") + todos[i];
            db.run("UPDATE item SET dc_subject = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                   {Value::of(junto), Value::of(agora), Value::of(itemId)});
            upsertCampoHumano(db, itemId, "dc_subject", junto, false);
        }

        if (!descricao.empty()) {
            auto secoes = matriz::model::parseNotasEstruturadas(lerColuna(db, itemId, "notas_livres"));
            bool juntou = false;
            for (auto& s : secoes) {
                if (s.titulo == "NOTES" && !s.automatica) {
                    s.conteudo = s.conteudo.empty() ? descricao : s.conteudo + "\n\n" + descricao;
                    juntou = true;
                    break;
                }
            }
            if (!juntou) secoes.insert(secoes.begin(), matriz::model::SecaoNota{"NOTES", descricao, false});
            db.run("UPDATE item SET notas_livres = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                   {Value::of(matriz::model::serializarNotasEstruturadas(secoes)), Value::of(agora), Value::of(itemId)});
        }

        bool mudouTags = false;
        for (const auto& tag : im.etiquetas) {
            auto st = db.prepare("SELECT 1 FROM item_tag WHERE item_id = ? AND tag = ?");
            st.bind(1, Value::of(itemId));
            st.bind(2, Value::of(tag));
            if (st.step()) continue;  // o arquivo já tinha: nada a fazer
            // O índice de busca (busca_fts + busca_fts_map) é mantido pelo trigger trg_item_tag_busca_insert:
            // inserir em busca_fts aqui duplicava as linhas e deixava as cópias fora do mapa.
            db.run("INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                   {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(tag)});
            mudouTags = true;
        }
        if (mudouTags)
            db.run("UPDATE item SET metadados_editados = 1, atualizado_em = ? WHERE id = ?", {Value::of(agora), Value::of(itemId)});
        db.run("RELEASE sobre_material", {});
        savepointAberto = false;
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("SobreMaterial: falha ao aplicar em " + juce::String(itemId) + " — " + juce::String(e.what()));
        if (savepointAberto) {
            try {
                db.run("ROLLBACK TO sobre_material", {});
                db.run("RELEASE sobre_material", {});
            } catch (...) {}
        }
    }
}

void AplicadorSobreMaterial::desfazer(const std::string& itemId) {
    auto& im = *impl_;
    auto& db = im.db;
    const auto& sm = im.sobre;
    if (itemId.empty()) return;
    using namespace matriz::model::nomes;
    const std::string agora = matriz::model::agoraIso8601();
    bool savepointAberto = false;
    try {
        db.run("SAVEPOINT desfazer_sobre", {});
        savepointAberto = true;
        bool mudou = false;

        for (const auto& tag : im.etiquetas) {
            auto st = db.prepare("SELECT id FROM item_tag WHERE item_id = ? AND lower(tag) = lower(?)");
            st.bind(1, Value::of(itemId));
            st.bind(2, Value::of(tag));
            std::vector<std::string> ids;
            while (st.step()) ids.push_back(st.columnText(0));
            for (const auto& id : ids) { db.run("DELETE FROM item_tag WHERE id = ?", {Value::of(id)}); mudou = true; }
        }

        if (!im.subjectsNovos.empty()) {
            std::string atual = lerColuna(db, itemId, "dc_subject");
            if (atual.empty()) atual = lerCampo(db, itemId, "dc_subject");
            std::set<std::string> tirar;
            for (const auto& s : im.subjectsNovos) tirar.insert(chave(s));
            std::string junto;
            for (const auto& s : dividirSubjects(atual)) {
                if (tirar.count(chave(s))) continue;
                junto += (junto.empty() ? "" : ", ") + s;
            }
            db.run("UPDATE item SET dc_subject = ? WHERE id = ?", {Value::of(junto), Value::of(itemId)});
            upsertCampoHumano(db, itemId, "dc_subject", junto, true);
            mudou = true;
        }

        if (!sm.descricao.empty()) {
            auto secoes = matriz::model::parseNotasEstruturadas(lerColuna(db, itemId, "notas_livres"));
            for (auto it = secoes.begin(); it != secoes.end(); ++it) {
                if (it->titulo != "NOTES" || it->automatica) continue;
                const auto pos = it->conteudo.find(sm.descricao);
                if (pos == std::string::npos) break;
                it->conteudo.erase(pos, sm.descricao.size());
                const auto aparar = [](std::string& t) {
                    t = juce::String(t).trim().toStdString();
                };
                // "A\n\n\n\nB" -> "A\n\nB": o separador que sobrou entre os dois trechos some junto.
                while (it->conteudo.find("\n\n\n\n") != std::string::npos) it->conteudo.replace(it->conteudo.find("\n\n\n\n"), 4, "\n\n");
                aparar(it->conteudo);
                if (it->conteudo.empty()) secoes.erase(it);
                db.run("UPDATE item SET notas_livres = ? WHERE id = ?", {Value::of(matriz::model::serializarNotasEstruturadas(secoes)), Value::of(itemId)});
                mudou = true;
                break;
            }
        }

        for (const auto& [campo, valor] : sm.campos) {
            if (campo != "lista" || valor.empty()) continue;
            db.run("UPDATE item SET lista = '' WHERE id = ? AND lista = ?", {Value::of(itemId), Value::of(valor)});
            mudou = true;
        }

        if (mudou) db.run("UPDATE item SET metadados_editados = 1, atualizado_em = ? WHERE id = ?", {Value::of(agora), Value::of(itemId)});
        db.run("RELEASE desfazer_sobre", {});
        savepointAberto = false;
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("SobreMaterial: falha ao desfazer em " + juce::String(itemId) + " — " + juce::String(e.what()));
        if (savepointAberto) {
            try {
                db.run("ROLLBACK TO desfazer_sobre", {});
                db.run("RELEASE desfazer_sobre", {});
            } catch (...) {}
        }
    }
}

} // namespace matriz::ingest
