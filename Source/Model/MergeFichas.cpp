#include "MergeFichas.h"

#include <cmath>
#include <map>
#include <regex>

#include "NomesCanonicos.h"
#include "Project.h"

namespace matriz::model::merge {

using matriz::db::Value;

namespace {

// Colunas de valor único do item (a de SUBJECT é lista, tratada à parte).
const char* const kColunasValorUnico[] = {
    "tipo_midia", "ano", "content_type", "source_media", "collection_type", "isrc",
    "dc_title", "dc_creator", "dc_description", "dc_publisher", "dc_contributor",
    "dc_created", "dc_issued", "dc_type", "dc_format", "dc_identifier", "dc_source",
    "dc_language", "dc_relation", "dc_coverage", "dc_rights"};

bool ehColunaDoItem(const std::string& campo) {
    if (campo == "dc_subject" || campo == "notas_livres" || campo == "titulo") return true;
    for (auto* c : kColunasValorUnico)
        if (campo == c) return true;
    return false;
}

bool ehData(const std::string& campo) { return campo == "ano" || campo == "dc_created" || campo == "dc_issued"; }

std::string aparar(const std::string& s) { return juce::String::fromUTF8(s.c_str()).trim().toStdString(); }

// Valor efetivo, como a ficha mostra (ProjetoAberto::lerMetadado): coluna do
// item, senão item_campo.
std::string lerEfetivo(matriz::db::Database& db, const std::string& itemId, const std::string& coluna) {
    {
        auto st = db.prepare("SELECT COALESCE(" + coluna + ", '') FROM item WHERE id = ?");
        st.bind(1, Value::of(itemId));
        if (st.step()) {
            auto v = aparar(st.columnText(0));
            if (!v.empty()) return v;
        }
    }
    auto st = db.prepare("SELECT COALESCE(valor, '') FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
    st.bind(1, Value::of(itemId));
    st.bind(2, Value::of(coluna));
    return st.step() ? aparar(st.columnText(0)) : std::string();
}

// Mesmo par de escritas de ProjetoAberto::salvarMetadado.
void gravarColuna(matriz::db::Database& db, const std::string& itemId, const std::string& coluna, const std::string& valor) {
    const std::string agora = agoraIso8601();
    db.run("UPDATE item SET " + coluna + " = ?, atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
           {Value::of(valor), Value::of(agora), Value::of(itemId)});
    db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
           "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
           "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, "
           "fonte = 'humano', atualizado_em = excluded.atualizado_em",
           {Value::of(novoUuid()), Value::of(itemId), Value::of(coluna), Value::of(valor), Value::of(agora)});
}

void gravarCampoFicha(matriz::db::Database& db, const std::string& itemId, const std::string& campo, const std::string& valor) {
    db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
           "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
           "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, "
           "fonte = 'humano', atualizado_em = excluded.atualizado_em",
           {Value::of(novoUuid()), Value::of(itemId), Value::of(campo), Value::of(valor), Value::of(agoraIso8601())});
}

// --- Geo -----------------------------------------------------------------
struct ColunaTipada { const char* nome; char tipo; };  // 'T' texto, 'I' inteiro, 'R' real
const ColunaTipada kColunasGeo[] = {
    {"latitude", 'R'}, {"longitude", 'R'}, {"altitude", 'R'}, {"continent", 'T'}, {"country", 'T'},
    {"country_code", 'T'}, {"state_province", 'T'}, {"state_code", 'T'}, {"city", 'T'}, {"municipality", 'T'},
    {"neighborhood", 'T'}, {"district", 'T'}, {"postal_code", 'T'}, {"street", 'T'}, {"street_number", 'T'},
    {"locality", 'T'}, {"formatted_address", 'T'}, {"source", 'T'}, {"precision_accuracy", 'R'}, {"confidence", 'R'}};

juce::var lerGeo(matriz::db::Database& db, const std::string& itemId) {
    std::string cols;
    for (auto& c : kColunasGeo) cols += (cols.empty() ? "" : ", ") + std::string(c.nome);
    auto st = db.prepare("SELECT " + cols + " FROM asset_geolocation WHERE asset_id = ?");
    st.bind(1, Value::of(itemId));
    if (!st.step()) return {};
    auto* o = new juce::DynamicObject();
    for (int i = 0; i < static_cast<int>(std::size(kColunasGeo)); ++i) {
        if (st.columnIsNull(i)) continue;
        if (kColunasGeo[i].tipo == 'R') o->setProperty(kColunasGeo[i].nome, st.columnReal(i));
        else if (auto v = juce::String::fromUTF8(st.columnText(i).c_str()).trim(); v.isNotEmpty())
            o->setProperty(kColunasGeo[i].nome, v);
    }
    return juce::var(o);
}

// O que a ficha mostra (coordenadas, endereço, cidade, estado, país): vazio = sem geo.
std::string assinaturaGeo(const juce::var& g) {
    if (!g.isObject()) return {};
    auto num = [&](const char* k) {
        return g.hasProperty(k) ? juce::String(static_cast<double>(g.getProperty(k, 0.0)), 6) : juce::String();
    };
    juce::String s = num("latitude") + "|" + num("longitude");
    for (auto* k : {"formatted_address", "city", "state_province", "country"})
        s << "|" << g.getProperty(k, {}).toString().trim();
    return s.removeCharacters("|").isEmpty() ? std::string() : s.toStdString();
}

void gravarGeo(matriz::db::Database& db, const std::string& itemId, const juce::var& g) {
    db.run("DELETE FROM asset_geolocation WHERE asset_id = ?", {Value::of(itemId)});
    auto* o = g.getDynamicObject();
    if (!o) return;
    std::string cols = "asset_id", marcas = "?";
    std::vector<Value> vals{Value::of(itemId)};
    for (auto& c : kColunasGeo) {
        if (!o->hasProperty(c.nome)) continue;
        const auto v = o->getProperty(c.nome);
        cols += std::string(", ") + c.nome;
        marcas += ", ?";
        vals.push_back(c.tipo == 'R' ? Value::of(static_cast<double>(v)) : Value::of(v.toString().toStdString()));
    }
    const std::string agora = agoraIso8601();
    db.run("INSERT INTO asset_geolocation (" + cols + ", created_at, updated_at) VALUES (" + marcas + ", ?, ?)",
           [&] { vals.push_back(Value::of(agora)); vals.push_back(Value::of(agora)); return vals; }());
}

std::string geoJson(const juce::var& g) { return g.isObject() ? juce::JSON::toString(g, true).toStdString() : std::string(); }

void registrarConflito(matriz::db::Database& db, const std::string& manterId, const std::string& descartarId,
                       const std::string& campo, const std::string& perdedor, const std::string& vencedor,
                       const std::string& origem) {
    db.run("INSERT INTO item_historico (id, item_id, tipo_evento, campo_id, valor_anterior, valor_novo, modelo_origem, "
           "modelo_origem_versao, autor, criado_em) VALUES (?, ?, 'edicao_campo', ?, ?, ?, ?, ?, ?, ?)",
           {Value::of(novoUuid()), Value::of(manterId), Value::of(campo), Value::of(perdedor), Value::of(vencedor),
            Value::of(std::string(kModeloMerge)), Value::of(descartarId), Value::of(origem), Value::of(agoraIso8601())});
}

// Escreve um valor de conflito de volta no lugar certo (revisão).
void gravarValor(matriz::db::Database& db, const std::string& itemId, const std::string& campo, const std::string& valor) {
    if (campo == kCampoGeo) {
        juce::var g;
        if (!valor.empty()) g = juce::JSON::parse(juce::String::fromUTF8(valor.c_str()));
        gravarGeo(db, itemId, g);
    } else if (ehColunaDoItem(campo)) {
        gravarColuna(db, itemId, campo, valor);
    } else {
        gravarCampoFicha(db, itemId, campo, valor);
    }
}

struct DataParcial { int ano = 0, mes = 0, dia = 0, precisao = 0; };

std::optional<DataParcial> lerData(const std::string& texto) {
    juce::String s = juce::String::fromUTF8(texto.c_str()).trim();
    bool temHora = false;
    if (auto i = s.indexOfAnyOf(" T"); i > 0) {
        temHora = s.substring(i + 1).trim().isNotEmpty();
        s = s.substring(0, i);
    }
    const std::string d = s.toStdString();
    std::smatch m;
    DataParcial p;
    static const std::regex ano(R"(^(\d{4})$)"), mesAno(R"(^(\d{1,2})[/.-](\d{4})$)"),
        diaMesAno(R"(^(\d{1,2})[/.-](\d{1,2})[/.-](\d{4})$)"), anoMes(R"(^(\d{4})[-/:](\d{1,2})$)"),
        anoMesDia(R"(^(\d{4})[-/:](\d{1,2})[-/:](\d{1,2})$)");
    if (std::regex_match(d, m, ano)) p = {std::stoi(m[1]), 0, 0, 1};
    else if (std::regex_match(d, m, mesAno)) p = {std::stoi(m[2]), std::stoi(m[1]), 0, 2};
    else if (std::regex_match(d, m, anoMes)) p = {std::stoi(m[1]), std::stoi(m[2]), 0, 2};
    else if (std::regex_match(d, m, diaMesAno)) p = {std::stoi(m[3]), std::stoi(m[2]), std::stoi(m[1]), 3};
    else if (std::regex_match(d, m, anoMesDia)) p = {std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]), 3};
    else return std::nullopt;
    if (p.mes > 12 || p.dia > 31 || (p.precisao >= 2 && p.mes < 1) || (p.precisao >= 3 && p.dia < 1)) return std::nullopt;
    if (temHora && p.precisao == 3) p.precisao = 4;
    return p;
}

}  // namespace

bool datasCompativeis(const std::string& a, const std::string& b, std::string* maisPrecisa) {
    auto da = lerData(a), db = lerData(b);
    if (!da || !db || da->ano != db->ano) return false;
    if (da->mes && db->mes && da->mes != db->mes) return false;
    if (da->dia && db->dia && da->dia != db->dia) return false;
    if (maisPrecisa) *maisPrecisa = db->precisao > da->precisao ? b : a;
    return true;
}

std::string origemDoDescartado(matriz::db::Database& registro, const std::string& descartarId) {
    auto st = registro.prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = 'pacote_origem' LIMIT 1");
    st.bind(1, Value::of(descartarId));
    if (st.step() && !st.columnText(0).empty()) return "package " + st.columnText(0);
    return "duplicate resolved";
}

juce::String resumoDoValor(const std::string& campo, const std::string& valor) {
    if (campo != kCampoGeo) return juce::String::fromUTF8(valor.c_str());
    const auto g = juce::JSON::parse(juce::String::fromUTF8(valor.c_str()));
    if (!g.isObject()) return {};
    juce::StringArray partes;
    if (g.hasProperty("latitude") && g.hasProperty("longitude"))
        partes.add(juce::String(static_cast<double>(g.getProperty("latitude", 0.0)), 6) + ", " +
                   juce::String(static_cast<double>(g.getProperty("longitude", 0.0)), 6));
    for (auto* k : {"formatted_address", "city", "state_province", "country"})
        if (auto v = g.getProperty(k, {}).toString().trim(); v.isNotEmpty()) partes.add(v);
    return partes.joinIntoString(juce::String::fromUTF8(" \xc2\xb7 "));
}

ResultadoJuncao juntarFichas(matriz::db::Database& db, const std::string& manterId, const std::string& descartarId,
                             const std::set<std::string>& usarDescartado, const std::string& origem, bool simular) {
    ResultadoJuncao r;
    auto conflito = [&](const std::string& campo, const std::string& vk, const std::string& vd,
                        const std::function<void(const std::string&)>& gravar) {
        r.conflitos.push_back({campo, vk, vd});
        if (simular) return;
        const bool trocar = usarDescartado.count(campo) > 0;
        if (trocar) gravar(vd);
        registrarConflito(db, manterId, descartarId, campo, trocar ? vk : vd, trocar ? vd : vk, origem);
    };

    // 1. Colunas de valor único (valor efetivo, como a ficha mostra).
    for (auto* coluna : kColunasValorUnico) {
        const std::string vk = lerEfetivo(db, manterId, coluna), vd = lerEfetivo(db, descartarId, coluna);
        if (vd.empty() || vd == vk) continue;
        auto gravar = [&](const std::string& v) { gravarColuna(db, manterId, coluna, v); };
        if (vk.empty()) {
            if (!simular) gravar(vd);
            ++r.camposSomados;
            continue;
        }
        std::string maisPrecisa;
        if (ehData(coluna) && datasCompativeis(vk, vd, &maisPrecisa)) {
            if (maisPrecisa == vd) {
                if (!simular) gravar(vd);
                ++r.camposSomados;
            }
            continue;
        }
        conflito(coluna, vk, vd, gravar);
    }

    // 2. SUBJECT é lista: soma, sem repetir nome (maiúsculas não contam).
    {
        const std::string sk = lerEfetivo(db, manterId, "dc_subject"), sd = lerEfetivo(db, descartarId, "dc_subject");
        std::set<std::string> chaves;
        for (auto& t : nomes::dividirSubjects(sk)) chaves.insert(nomes::chave(t));
        std::string novos;
        for (auto& t : nomes::dividirSubjects(sd))
            if (chaves.insert(nomes::chave(t)).second) novos += (novos.empty() ? "" : ", ") + t;
        if (!novos.empty()) {
            if (!simular) gravarColuna(db, manterId, "dc_subject", sk.empty() ? novos : sk + ", " + novos);
            ++r.camposSomados;
        }
    }

    // 3. Demais campos da ficha (item_campo que não espelham coluna do item).
    {
        struct Campo { std::string nivel; long long idx; std::string campo, valor, fonte, idK, valorK, fonteK; };
        std::vector<Campo> campos;
        auto st = db.prepare(
            "SELECT d.nivel, d.nivel_indice, d.campo_id, d.valor, d.fonte, COALESCE(k.id, ''), COALESCE(k.valor, ''), "
            "COALESCE(k.fonte, '') FROM item_campo d LEFT JOIN item_campo k ON k.item_id = ? AND k.nivel = d.nivel "
            " AND k.nivel_indice = d.nivel_indice AND k.campo_id = d.campo_id "
            "WHERE d.item_id = ? AND TRIM(COALESCE(d.valor, '')) <> '' AND d.campo_id <> 'pacote_origem'");
        st.bind(1, Value::of(manterId));
        st.bind(2, Value::of(descartarId));
        while (st.step())
            campos.push_back({st.columnText(0), st.columnInt(1), st.columnText(2), st.columnText(3), st.columnText(4),
                              st.columnText(5), st.columnText(6), st.columnText(7)});
        for (const auto& c : campos) {
            if (ehColunaDoItem(c.campo) || aparar(c.valor) == aparar(c.valorK)) continue;
            const std::string agora = agoraIso8601();
            if (c.idK.empty()) {
                if (!simular)
                    db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                           {Value::of(novoUuid()), Value::of(manterId), Value::of(c.nivel), Value::of(c.idx),
                            Value::of(c.campo), Value::of(c.valor), Value::of(c.fonte), Value::of(agora)});
                ++r.camposSomados;
            } else if (aparar(c.valorK).empty()) {
                if (!simular)
                    db.run("UPDATE item_campo SET valor = ?, atualizado_em = ? WHERE id = ?",
                           {Value::of(c.valor), Value::of(agora), Value::of(c.idK)});
                ++r.camposSomados;
            } else if (c.fonte != "leitura_tecnica" && c.fonteK != "leitura_tecnica" && c.nivel == "raiz" && c.idx == 0) {
                conflito(c.campo, aparar(c.valorK), aparar(c.valor),
                         [&](const std::string& v) { gravarCampoFicha(db, manterId, c.campo, v); });
            }
        }
    }

    // 4. GEO LOCATION: valor único, comparado pelo que a ficha mostra.
    {
        const auto gk = lerGeo(db, manterId), gd = lerGeo(db, descartarId);
        const auto ak = assinaturaGeo(gk), ad = assinaturaGeo(gd);
        if (!ad.empty() && ad != ak) {
            if (ak.empty()) {
                if (!simular) gravarGeo(db, manterId, gd);
                ++r.camposSomados;
            } else {
                conflito(kCampoGeo, geoJson(gk), geoJson(gd), [&](const std::string&) { gravarGeo(db, manterId, gd); });
            }
        }
    }

    if (simular) return r;

    // 5. Tags e pessoas: união ("show" do descartado = "Show" do mantido).
    {
        std::set<std::string> chavesMantido;
        {
            auto st = db.prepare("SELECT tag FROM item_tag WHERE item_id = ?");
            st.bind(1, Value::of(manterId));
            while (st.step()) chavesMantido.insert(nomes::chave(st.columnText(0)));
        }
        std::vector<std::string> tags;
        auto st = db.prepare("SELECT tag FROM item_tag WHERE item_id = ? ORDER BY rowid");
        st.bind(1, Value::of(descartarId));
        while (st.step()) {
            std::string t = st.columnText(0);
            if (chavesMantido.insert(nomes::chave(t)).second) tags.push_back(nomes::tagCanonica(db, t));
        }
        for (const auto& t : tags) {
            db.run("INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                   {Value::of(novoUuid()), Value::of(manterId), Value::of(t)});
            ++r.camposSomados;
        }
    }
    db.run("INSERT OR IGNORE INTO item_assunto (item_id, assunto_id, autor, criado_em) "
           "SELECT ?, assunto_id, autor, criado_em FROM item_assunto WHERE item_id = ?",
           {Value::of(manterId), Value::of(descartarId)});

    // 6. Marcadores/observações: somam; mesmo tempo e mesmo texto não duplica.
    {
        struct Obs { std::string texto, autor, criadoEm, titulo; bool temMin; long long min; };
        std::vector<Obs> obs;
        auto st = db.prepare("SELECT texto, autor, criado_em, minutagem_ms, titulo FROM item_observacao o "
                             "WHERE item_id = ? AND NOT EXISTS (SELECT 1 FROM item_observacao k "
                             " WHERE k.item_id = ? AND k.texto = o.texto AND k.minutagem_ms IS o.minutagem_ms)");
        st.bind(1, Value::of(descartarId));
        st.bind(2, Value::of(manterId));
        while (st.step())
            obs.push_back({st.columnText(0), st.columnText(1), st.columnText(2), st.columnText(4), !st.columnIsNull(3),
                           st.columnInt(3)});
        for (const auto& o : obs) {
            db.run("INSERT INTO item_observacao (id, item_id, texto, autor, criado_em, minutagem_ms, titulo) "
                   "VALUES (?, ?, ?, ?, ?, ?, ?)",
                   {Value::of(novoUuid()), Value::of(manterId), Value::of(o.texto), Value::of(o.autor),
                    Value::of(o.criadoEm), o.temMin ? Value::of(o.min) : Value::null(),
                    o.titulo.empty() ? Value::null() : Value::of(o.titulo)});
            ++r.camposSomados;
        }
    }
    return r;
}

std::vector<ConflitoPendente> conflitosPendentes(matriz::db::Database& registro, const std::string& itemId) {
    std::vector<ConflitoPendente> out;
    auto st = registro.prepare("SELECT id, COALESCE(campo_id, ''), COALESCE(valor_novo, ''), COALESCE(valor_anterior, ''), "
                               "autor FROM item_historico WHERE item_id = ? AND modelo_origem = ? "
                               "AND confianca_origem IS NULL ORDER BY criado_em, rowid");
    st.bind(1, Value::of(itemId));
    st.bind(2, Value::of(std::string(kModeloMerge)));
    while (st.step()) out.push_back({st.columnText(0), st.columnText(1), st.columnText(2), st.columnText(3), st.columnText(4)});
    return out;
}

std::set<std::string> itensComConflitoPendente(matriz::db::Database& registro) {
    std::set<std::string> out;
    try {
        auto st = registro.prepare("SELECT DISTINCT item_id FROM item_historico WHERE modelo_origem = ? "
                                   "AND confianca_origem IS NULL");
        st.bind(1, Value::of(std::string(kModeloMerge)));
        while (st.step()) out.insert(st.columnText(0));
    } catch (...) {}
    return out;
}

void revisarConflitos(matriz::db::Database& registro, const std::string& itemId,
                      const std::set<std::string>& trocarHistoricoIds) {
    for (const auto& c : conflitosPendentes(registro, itemId)) {
        if (!trocarHistoricoIds.count(c.historicoId)) continue;
        gravarValor(registro, itemId, c.campo, c.valorAlternativo);
        registro.run("UPDATE item_historico SET valor_anterior = ?, valor_novo = ? WHERE id = ?",
                     {Value::of(c.valorAtual), Value::of(c.valorAlternativo), Value::of(c.historicoId)});
    }
    registro.run("UPDATE item_historico SET confianca_origem = 1 WHERE item_id = ? AND modelo_origem = ? "
                 "AND confianca_origem IS NULL",
                 {Value::of(itemId), Value::of(std::string(kModeloMerge))});
}

// ---------------------------------------------------------------------------
// Retrato (Undo)
// ---------------------------------------------------------------------------

namespace {

struct TabelaRetrato {
    const char* nome;
    const char* chave;
    std::vector<ColunaTipada> colunas;
};

const std::vector<TabelaRetrato>& tabelasRetrato() {
    static const std::vector<TabelaRetrato> t = {
        {"item_campo", "item_id",
         {{"id", 'T'}, {"item_id", 'T'}, {"nivel", 'T'}, {"nivel_indice", 'I'}, {"campo_id", 'T'}, {"valor", 'T'},
          {"fonte", 'T'}, {"atualizado_em", 'T'}}},
        {"item_tag", "item_id", {{"id", 'T'}, {"item_id", 'T'}, {"tag", 'T'}}},
        {"item_observacao", "item_id",
         {{"id", 'T'}, {"item_id", 'T'}, {"texto", 'T'}, {"autor", 'T'}, {"criado_em", 'T'}, {"minutagem_ms", 'I'},
          {"marcador_id", 'T'}, {"titulo", 'T'}, {"categoria", 'T'}, {"prioridade", 'T'}, {"checklist", 'T'},
          {"anexos", 'T'}}},
        {"item_assunto", "item_id", {{"item_id", 'T'}, {"assunto_id", 'T'}, {"autor", 'T'}, {"criado_em", 'T'}}},
        {"asset_geolocation", "asset_id",
         [] {
             std::vector<ColunaTipada> c{{"asset_id", 'T'}};
             for (auto& g : kColunasGeo) c.push_back(g);
             c.push_back({"created_at", 'T'});
             c.push_back({"updated_at", 'T'});
             return c;
         }()},
    };
    return t;
}

std::vector<ColunaTipada> colunasItemRetrato() {
    std::vector<ColunaTipada> c;
    for (auto* n : kColunasValorUnico) c.push_back({n, 'T'});
    c.push_back({"dc_subject", 'T'});
    c.push_back({"notas_livres", 'T'});
    c.push_back({"estado", 'T'});
    c.push_back({"metadados_editados", 'I'});
    c.push_back({"atualizado_em", 'T'});
    return c;
}

Value lerTipado(matriz::db::Statement& st, int i, char tipo) {
    if (st.columnIsNull(i)) return Value::null();
    if (tipo == 'I') return Value::of(st.columnInt(i));
    if (tipo == 'R') return Value::of(st.columnReal(i));
    return Value::of(st.columnText(i));
}

std::string listaColunas(const std::vector<ColunaTipada>& cols) {
    std::string s;
    for (auto& c : cols) s += (s.empty() ? "" : ", ") + std::string(c.nome);
    return s;
}

}  // namespace

struct Retrato {
    struct DoItem {
        std::string id;
        std::vector<Value> colunasItem;
        std::vector<std::vector<std::vector<Value>>> linhasPorTabela;  // na ordem de tabelasRetrato()
        std::set<std::string> historicoMerge;                          // ids que já existiam
    };
    std::vector<DoItem> itens;
};

std::shared_ptr<Retrato> retratar(matriz::db::Database& db, const std::vector<std::string>& itemIds) {
    auto r = std::make_shared<Retrato>();
    const auto colsItem = colunasItemRetrato();
    std::set<std::string> vistos;
    for (const auto& id : itemIds) {
        if (!vistos.insert(id).second) continue;
        Retrato::DoItem d;
        d.id = id;
        {
            auto st = db.prepare("SELECT " + listaColunas(colsItem) + " FROM item WHERE id = ?");
            st.bind(1, Value::of(id));
            if (!st.step()) continue;
            for (int i = 0; i < static_cast<int>(colsItem.size()); ++i) d.colunasItem.push_back(lerTipado(st, i, colsItem[i].tipo));
        }
        for (const auto& t : tabelasRetrato()) {
            std::vector<std::vector<Value>> linhas;
            auto st = db.prepare("SELECT " + listaColunas(t.colunas) + " FROM " + t.nome + " WHERE " + t.chave + " = ?");
            st.bind(1, Value::of(id));
            while (st.step()) {
                std::vector<Value> linha;
                for (int i = 0; i < static_cast<int>(t.colunas.size()); ++i) linha.push_back(lerTipado(st, i, t.colunas[i].tipo));
                linhas.push_back(std::move(linha));
            }
            d.linhasPorTabela.push_back(std::move(linhas));
        }
        {
            auto st = db.prepare("SELECT id FROM item_historico WHERE item_id = ? AND modelo_origem = ?");
            st.bind(1, Value::of(id));
            st.bind(2, Value::of(std::string(kModeloMerge)));
            while (st.step()) d.historicoMerge.insert(st.columnText(0));
        }
        r->itens.push_back(std::move(d));
    }
    return r;
}

void restaurar(matriz::db::Database& db, const Retrato& retrato) {
    const auto colsItem = colunasItemRetrato();
    std::string sets;
    for (auto& c : colsItem) sets += (sets.empty() ? "" : ", ") + std::string(c.nome) + " = ?";
    for (const auto& d : retrato.itens) {
        auto vals = d.colunasItem;
        vals.push_back(Value::of(d.id));
        db.run("UPDATE item SET " + sets + " WHERE id = ?", vals);
        const auto& tabelas = tabelasRetrato();
        for (size_t t = 0; t < tabelas.size() && t < d.linhasPorTabela.size(); ++t) {
            // DELETE + INSERT (não UPDATE): os gatilhos mantêm busca_fts/_map.
            db.run(std::string("DELETE FROM ") + tabelas[t].nome + " WHERE " + tabelas[t].chave + " = ?", {Value::of(d.id)});
            std::string marcas;
            for (size_t i = 0; i < tabelas[t].colunas.size(); ++i) marcas += i ? ", ?" : "?";
            for (const auto& linha : d.linhasPorTabela[t])
                db.run(std::string("INSERT INTO ") + tabelas[t].nome + " (" + listaColunas(tabelas[t].colunas) +
                           ") VALUES (" + marcas + ")",
                       linha);
        }
        std::vector<std::string> novos;
        {
            auto st = db.prepare("SELECT id FROM item_historico WHERE item_id = ? AND modelo_origem = ?");
            st.bind(1, Value::of(d.id));
            st.bind(2, Value::of(std::string(kModeloMerge)));
            while (st.step())
                if (!d.historicoMerge.count(st.columnText(0))) novos.push_back(st.columnText(0));
        }
        for (const auto& h : novos) db.run("DELETE FROM item_historico WHERE id = ?", {Value::of(h)});
    }
}

}  // namespace matriz::model::merge
