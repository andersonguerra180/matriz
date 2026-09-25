#pragma once

#include <string>
#include <vector>

#include "../Db/Database.h"

// Fase 4 (Autocomplete por projeto): histórico de valores já digitados,
// por campo, dentro do PROJETO atual (mesmo padrão de
// GeoFavoritosRepository — ver Source/Analytics/AssetGeolocation.h).
// Alimenta o popup de AutoCompleteTextEditor; não guarda nada em memória
// entre chamadas, sempre lê/escreve direto no banco do projeto aberto.

namespace matriz::ficha {

class AutocompleteRepository {
public:
    // Cria a tabela se não existir, depois grava/atualiza o valor pro
    // campoId dado ("dc_subject", "dc_creator", "dc_publisher",
    // "dc_contributor", "recording_device"). Valores em branco são
    // ignorados (não há o que sugerir depois).
    static void registrar(matriz::db::Database& db, const std::string& campoId, const std::string& valor);

    // Todos os valores já usados nesse projeto para o campo, em ordem
    // alfabética (case-insensitive) — filtragem por texto digitado é feita
    // por quem chama (AutoCompleteTextEditor).
    static std::vector<std::string> listar(matriz::db::Database& db, const std::string& campoId);
};

} // namespace matriz::ficha
