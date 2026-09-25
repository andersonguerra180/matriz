#include "AutocompleteHistorico.h"

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
    std::vector<std::string> resultado;
    auto stmt = db.prepare("SELECT valor FROM autocomplete_historico WHERE campo_id = ? ORDER BY valor COLLATE NOCASE ASC");
    stmt.bind(1, matriz::db::Value::of(campoId));
    while (stmt.step()) {
        if (!stmt.columnIsNull(0)) resultado.push_back(stmt.columnText(0));
    }
    return resultado;
}

} // namespace matriz::ficha
