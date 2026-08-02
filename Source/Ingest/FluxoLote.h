#pragma once

#include <map>
#include <string>
#include <vector>

#include "../Db/Database.h"

// Fluxo de ficha em lote (§7.2 estágio 2): a ficha é sempre respondida por
// grupo, nunca por arquivo. Aplica o mesmo conjunto de valores confirmados
// a todos os itens de um grupo de uma vez, gravando fonte='humano' e um
// evento de histórico por campo alterado (P3 — vira decisão humana com
// autor e data).

namespace matriz::ingest {

// `nivel`/`nivelIndice` seguem a convenção de item_campo: 'raiz'/0 para
// ficha de nível único, ou o nome do nível repetido ('faixa') com o índice
// da ocorrência para tipos com niveis aninhados (§6.3).
void aplicarFichaEmLote(matriz::db::Database& registro, const std::vector<std::string>& itemIds,
                         const std::string& nivel, int nivelIndice, const std::map<std::string, std::string>& valores,
                         const std::string& autor);

} // namespace matriz::ingest
