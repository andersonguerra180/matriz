#pragma once

#include <JuceHeader.h>

#include <optional>
#include <string>

#include "../Db/Database.h"

// Resolução de caminho físico de um `arquivo` (I5 — Preservação In-Place).
//
// Desde a ingestão in-place, `arquivo.caminho_relativo` é relativo à RAIZ DO
// VAULT (ponto de montagem do volume), não mais à pasta do projeto. Todo
// consumidor que precisa abrir o arquivo de verdade tem que passar por aqui —
// concatenar `pastaProjeto + caminho_relativo` na mão só funcionava no modelo
// antigo, em que o ingest copiava tudo pra dentro do projeto.

namespace matriz::vault {

// Resolve a partir de colunas já lidas, pra loops que fazem um SELECT só.
// Ordem de tentativa:
//   1. vault.localizacao + arquivo.caminho_relativo  (caso normal)
//   2. arquivo.caminho_absoluto_origem               (vault nulo / desconhecido)
//   3. pastaProjeto + arquivo.caminho_relativo       (projeto legado, §4)
// Devolve nullopt quando nenhum candidato existe em disco — o chamador
// distingue "não sei onde está" de "sei onde está e sumiu" olhando
// `arquivo.estado_presenca`.
std::optional<juce::File> resolverCaminho(const juce::File& pastaProjeto,
                                          const std::string& localizacaoVault,
                                          const std::string& caminhoRelativo,
                                          const std::string& caminhoAbsolutoOrigem);

// Mesma resolução, buscando as colunas pelo id do arquivo.
std::optional<juce::File> resolverArquivo(matriz::db::Database& registro, const std::string& arquivoId,
                                          const juce::File& pastaProjeto);

// Caminho "esperado" mesmo que o arquivo não exista em disco — pra mensagens
// de erro e pra exibir onde o operador deve procurar (§8, item ausente).
juce::File caminhoEsperado(const juce::File& pastaProjeto, const std::string& localizacaoVault,
                           const std::string& caminhoRelativo, const std::string& caminhoAbsolutoOrigem);

// Fragmento de SELECT com as colunas que `resolverCaminho` consome, na ordem
// (localizacao, caminho_relativo, caminho_absoluto_origem). Usado pra manter
// as consultas espalhadas em sincronia com a assinatura acima.
inline const char* colunasDeResolucao() {
    return "COALESCE(v.localizacao, ''), a.caminho_relativo, COALESCE(a.caminho_absoluto_origem, '')";
}

// JOIN correspondente. `a` é o alias de `arquivo`.
inline const char* joinDeResolucao() { return "LEFT JOIN vault v ON v.id = a.vault_id"; }

} // namespace matriz::vault
