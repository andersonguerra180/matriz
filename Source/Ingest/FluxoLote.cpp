#include "FluxoLote.h"

#include "../Model/Project.h"

namespace matriz::ingest {

using matriz::db::Value;

namespace {

std::string lerValorAtual(matriz::db::Database& registro, const std::string& itemId, const std::string& nivel,
                           int nivelIndice, const std::string& campoId) {
    auto stmt = registro.prepare(
        "SELECT valor FROM item_campo WHERE item_id = ? AND nivel = ? AND nivel_indice = ? AND campo_id = ?");
    stmt.bind(1, Value::of(itemId));
    stmt.bind(2, Value::of(nivel));
    stmt.bind(3, Value::of(nivelIndice));
    stmt.bind(4, Value::of(campoId));
    if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
    return {};
}

} // namespace

void aplicarFichaEmLote(matriz::db::Database& registro, const std::vector<std::string>& itemIds,
                         const std::string& nivel, int nivelIndice, const std::map<std::string, std::string>& valores,
                         const std::string& autor) {
    std::string agora = matriz::model::agoraIso8601();

    registro.exec("BEGIN");
    try {
        for (auto& itemId : itemIds) {
            for (auto& [campoId, valor] : valores) {
                std::string valorAnterior = lerValorAtual(registro, itemId, nivel, nivelIndice, campoId);

                registro.run(
                    "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                    "VALUES (?, ?, ?, ?, ?, ?, 'humano', ?) "
                    "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) "
                    "DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em "
                    "WHERE item_campo.fonte != 'leitura_tecnica'",
                    {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(nivel), Value::of(nivelIndice),
                     Value::of(campoId), Value::of(valor), Value::of(agora)});

                if (valorAnterior != valor) {
                    registro.run(
                        "INSERT INTO item_historico (id, item_id, tipo_evento, campo_id, valor_anterior, valor_novo, "
                        "autor, criado_em) VALUES (?, ?, 'edicao_campo', ?, ?, ?, ?, ?)",
                        {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(campoId),
                         valorAnterior.empty() ? Value::null() : Value::of(valorAnterior), Value::of(valor),
                         Value::of(autor), Value::of(agora)});
                }
            }

            registro.run("UPDATE item SET atualizado_em = ? WHERE id = ?", {Value::of(agora), Value::of(itemId)});
        }
        registro.exec("COMMIT");
    } catch (...) {
        registro.exec("ROLLBACK");
        throw;
    }
}

} // namespace matriz::ingest
