#pragma once

#include <JuceHeader.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../Db/Database.h"

// Junção de fichas ao resolver uma duplicata (pacote de collection, Fase 4).
//
// Ao manter um item e descartar o outro, o mantido recebe os dados do
// descartado:
//   - listas (tags/pessoas, subjects, marcadores/observações, assuntos):
//     somam; nome igual ignorando maiúsculas é o mesmo; marcador idêntico
//     (mesmo tempo e mesmo texto) não duplica;
//   - valor único (EVENT DATE, CONTENT, GEO LOCATION, título, descrição e os
//     demais campos da ficha): lado vazio → o preenchido entra; datas
//     compatíveis ("2015" × "12/03/2015") → fica a mais precisa; diferentes
//     de verdade → vale o do mantido (ou o que o operador escolheu na tela) e
//     o perdedor vai para item_historico, recuperável.
// Leituras técnicas (fonte 'leitura_tecnica') só preenchem vazio — nunca
// viram conflito.
//
// Conflito no item_historico: tipo_evento 'edicao_campo', modelo_origem =
// 'merge' (marca da origem do registro), valor_anterior = perdedor,
// valor_novo = vencedor, autor = origem ("duplicate resolved" / nome do
// pacote), modelo_origem_versao = id do item descartado. confianca_origem
// NULL = ainda não revisado (filtro "Merge conflicts"); 1 = revisado.
// campo_id: coluna do item, "geo" (valores em JSON da linha de
// asset_geolocation) ou o campo_id de item_campo.

namespace matriz::model::merge {

inline constexpr const char* kModeloMerge = "merge";
inline constexpr const char* kCampoGeo = "geo";

struct Conflito {
    std::string campo;
    std::string valorMantido;    // como está no item mantido
    std::string valorDescartado; // do descartado
};

// Datas: "2015", "03/2015", "12/03/2015", "2015-03", "2015-03-12",
// "2015:03:12 10:00:00"... Compatíveis = mesmo ano e, onde os dois têm, mesmo
// mês/dia. `maisPrecisa` recebe a de mais componentes (empate: `a`).
bool datasCompativeis(const std::string& a, const std::string& b, std::string* maisPrecisa = nullptr);

struct ResultadoJuncao {
    int camposSomados = 0;
    std::vector<Conflito> conflitos;
};

// simular = true: não grava nada, só diz quais conflitos haveria (tela de
// resolução manual). usarDescartado: campos de conflito em que o operador
// trocou pro valor do descartado. Quem chama segura a transação.
ResultadoJuncao juntarFichas(matriz::db::Database& registro, const std::string& manterId,
                             const std::string& descartarId, const std::set<std::string>& usarDescartado,
                             const std::string& origem, bool simular = false);

// Origem pro histórico: nome do pacote de onde o descartado veio, se veio de
// um (item_campo 'pacote_origem'), senão "duplicate resolved".
std::string origemDoDescartado(matriz::db::Database& registro, const std::string& descartarId);

// Resumo legível de um valor de conflito (JSON de geo vira "lat, lng · cidade ...").
juce::String resumoDoValor(const std::string& campo, const std::string& valor);

// --- Revisão (filtro "Merge conflicts") -------------------------------------
struct ConflitoPendente {
    std::string historicoId, campo, valorAtual, valorAlternativo, origem;
};
std::vector<ConflitoPendente> conflitosPendentes(matriz::db::Database& registro, const std::string& itemId);
std::set<std::string> itensComConflitoPendente(matriz::db::Database& registro);
// Troca pelo valor alternativo os conflitos escolhidos (o atual vira o
// alternativo no histórico) e marca TODOS os pendentes do item como
// revisados. Quem chama segura a transação.
void revisarConflitos(matriz::db::Database& registro, const std::string& itemId,
                      const std::set<std::string>& trocarHistoricoIds);

// --- Undo -------------------------------------------------------------------
// Retrato das fichas (item, item_campo, tags, observações, assuntos, geo,
// histórico de merge) de um conjunto de itens; restaurar() volta tudo como
// estava. Opaco pra quem chama.
struct Retrato;
std::shared_ptr<Retrato> retratar(matriz::db::Database& registro, const std::vector<std::string>& itemIds);
void restaurar(matriz::db::Database& registro, const Retrato& retrato);  // quem chama segura a transação

}  // namespace matriz::model::merge
