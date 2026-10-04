#pragma once

#include <map>
#include <string>
#include <vector>

#include "FichaDefinition.h"

// Descoberta de tipos de ficha em tempo de execução (§6.1: "adicionar tipo
// novo é escrever um arquivo, não recompilar"). Nenhuma lista de ids fica
// hardcoded em C++ — todo consumidor (diálogo de seleção, selftests, SPEC.md)
// lê o diretório fichas/ através deste catálogo.

namespace matriz::ficha {

struct TipoFichaInfo {
    std::string id;    // nome do arquivo sem ".yaml" — chave pro item_midia.tipo_midia no banco
    FichaDefinition definicao;
};

// Varre `fichasDir` por "*.yaml", carrega cada um com loadFromFile, e ordena
// por (ordem, id). Lança FichaDefinitionError com o nome do arquivo se algum
// YAML for inválido — um tipo quebrado derruba a listagem inteira em vez de
// sumir em silêncio (falha visível é preferível a uma opção que só some).
std::vector<TipoFichaInfo> listarTodosOsTipos(const std::string& fichasDir);

// Filtra o resultado de listarTodosOsTipos por modo ("archive" ou "catalog").
// Uma ficha com `modos` vazio no YAML é incluída nos dois modos.
std::vector<TipoFichaInfo> listarTiposPorModo(const std::string& fichasDir, const std::string& modo);

// Mesmas duas funções sobre fichas já em memória (id do tipo -> texto do YAML).
// É o que o app usa: os YAMLs de fichas/ vão EMBUTIDOS no binário (Ui/FichasEmbutidas),
// pra funcionar em qualquer Mac sem a pasta do código-fonte. A versão por diretório
// fica pras ferramentas de linha de comando e testes.
std::vector<TipoFichaInfo> listarTodosOsTipos(const std::map<std::string, std::string>& yamlPorId);
std::vector<TipoFichaInfo> listarTiposPorModo(const std::map<std::string, std::string>& yamlPorId, const std::string& modo);

} // namespace matriz::ficha
