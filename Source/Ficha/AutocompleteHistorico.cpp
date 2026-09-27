#include "AutocompleteHistorico.h"
#include <JuceHeader.h>
#include <map>

#include "../Model/Project.h"

#include <algorithm>
#include <cctype>

namespace matriz::ficha {

namespace {

void criarTabela(matriz::db::Database& db) {
    db.run(
        "CREATE TABLE IF NOT EXISTS autocomplete_historico ("
        "  campo_id TEXT NOT NULL, "
        "  valor    TEXT NOT NULL, "
        "  atualizado_em TEXT NOT NULL, "
        "  PRIMARY KEY (campo_id, valor)"
        ")", {});
}

std::string aparar(const std::string& s) {
    auto inicio = std::find_if_not(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
    auto fim = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return (inicio < fim) ? std::string(inicio, fim) : std::string();
}

} // namespace

void AutocompleteRepository::registrar(matriz::db::Database& db, const std::string& campoId, const std::string& valor) {
    std::string valorAparado = aparar(valor);
    if (campoId.empty() || valorAparado.empty()) return;

    criarTabela(db);
    db.run(
        "INSERT INTO autocomplete_historico (campo_id, valor, atualizado_em) VALUES (?, ?, ?) "
        "ON CONFLICT(campo_id, valor) DO UPDATE SET atualizado_em = excluded.atualizado_em",
        {matriz::db::Value::of(campoId), matriz::db::Value::of(valorAparado), matriz::db::Value::of(matriz::model::agoraIso8601())});
}

std::vector<std::string> AutocompleteRepository::listar(matriz::db::Database& db, const std::string& campoId) {
    criarTabela(db);
    // Uma lista só pro projeto inteiro: o histórico da ficha MAIS os valores
    // já gravados nos itens / na geolocalização — o que foi aplicado em lote
    // no INTAKE aparece na ficha do Metadata e vice-versa.
    std::map<juce::String, std::string> porChave;  // sem distinção de caixa
    auto somar = [&](const std::string& sql, bool comCampo) {
        try {
            auto st = db.prepare(sql);
            if (comCampo) st.bind(1, matriz::db::Value::of(campoId));
            while (st.step()) {
                if (st.columnIsNull(0)) continue;
                const std::string v = aparar(st.columnText(0));
                if (!v.empty()) porChave.emplace(juce::String::fromUTF8(v.c_str()).toLowerCase(), v);
            }
        } catch (...) {}
    };
    somar("SELECT valor FROM autocomplete_historico WHERE campo_id = ?", true);
    static const std::map<std::string, std::string> kFonteGravada = {
        {"dc_creator", "SELECT DISTINCT dc_creator FROM item"},
        {"dc_subject", "SELECT DISTINCT dc_subject FROM item"},
        {"dc_publisher", "SELECT DISTINCT dc_publisher FROM item"},
        {"dc_contributor", "SELECT DISTINCT dc_contributor FROM item"},
        {"geo_address", "SELECT DISTINCT formatted_address FROM asset_geolocation"},
        {"geo_city", "SELECT DISTINCT city FROM asset_geolocation"},
        {"geo_state", "SELECT DISTINCT state_province FROM asset_geolocation"},
        {"geo_country", "SELECT DISTINCT country FROM asset_geolocation"},
    };
    if (auto it = kFonteGravada.find(campoId); it != kFonteGravada.end()) somar(it->second, false);
    std::vector<std::string> resultado;
    for (auto& [chave, valor] : porChave) resultado.push_back(valor);  // map já ordena
    return resultado;
}

} // namespace matriz::ficha
