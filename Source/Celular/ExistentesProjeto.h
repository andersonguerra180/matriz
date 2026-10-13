#pragma once

#include "RecebimentoCelular.h"
#include "../Model/Project.h"

namespace matriz::celular {

// (nome, tamanho) de todos os arquivos que o projeto/backup aberto já tem. Lê pela conexão
// SOMENTE-LEITURA da thread que chama (Project::registroLeitura): chamar de thread de fundo.
ConjuntoExistentes existentesDoProjeto(matriz::model::Project& projeto);

} // namespace matriz::celular
