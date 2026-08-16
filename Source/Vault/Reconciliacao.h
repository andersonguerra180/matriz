#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>
#include <vector>

#include "../App/Cancelamento.h"
#include "../Db/Database.h"

// Reconciliação e auditoria de Vault (§8).
//
// A premissa do sistema é que o conhecimento sobrevive ao arquivo: o arquivo
// pode ser movido, renomeado, apagado ou corrompido, e a ficha, os
// marcadores e o histórico continuam válidos e ligados ao mesmo asset. Este
// módulo é onde isso acontece — ele nunca apaga registro por não achar
// arquivo, só atualiza onde o arquivo está e em que estado.
//
// Duas operações, com custos muito diferentes:
//
//  - Varredura rápida: automática quando um volume é montado. Compara
//    caminho + tamanho + data de modificação. Só recalcula hash quando algo
//    de fato mudou. É o que permite passar por 100.000 itens em minutos
//    (critério 17) em vez de reler terabytes.
//
//  - Verificação completa (Verify Vault): manual, relê cada byte e recalcula
//    o SHA-256. É a auditoria de bit rot — cara por definição, nunca
//    automática.

namespace matriz::vault {

// Resumo da varredura, no formato que a barra discreta mostra:
// "12 new · 3 moved · 1 missing · 2 changed" (§8).
struct ResumoReconciliacao {
    int novos = 0;      // arquivos no volume ainda sem registro
    int movidos = 0;    // mesmo conteúdo em caminho novo — ficha preservada
    int ausentes = 0;   // registrado, não está mais lá
    int alterados = 0;  // mesmo caminho, conteúdo diferente
    int corrompidos = 0;  // só a verificação completa produz este
    int verificados = 0;  // quantos foram efetivamente conferidos
    bool cancelada = false;

    juce::String comoTexto() const;
    bool semMudancas() const { return novos == 0 && movidos == 0 && ausentes == 0 && alterados == 0 && corrompidos == 0; }
};

// Marca o Vault como online/offline conforme o ponto de montagem existir
// agora. Devolve true se o estado mudou (ou seja, se vale disparar a
// reconciliação).
bool atualizarPresencaDoVault(matriz::db::Database& registro, const std::string& vaultId);

// Percorre todos os Vaults do projeto atualizando `status`. Devolve os ids
// dos que acabaram de ficar online — os que merecem varredura.
std::vector<std::string> reavaliarVaults(matriz::db::Database& registro);

// Varredura rápida de um Vault. NUNCA na message thread: percorre o volume
// inteiro (I1). `aoProgredir` é chamado da thread de trabalho.
ResumoReconciliacao varreduraRapida(matriz::db::Database& registro, const std::string& vaultId,
                                     const std::shared_ptr<matriz::app::Cancelamento>& cancelamento = nullptr,
                                     std::function<void(int verificados, int total)> aoProgredir = nullptr);

// Verificação completa: relê os bytes e recalcula o SHA-256 de cada arquivo
// do Vault. Arquivo cujo hash não bate vira `corrompido` e o hash antigo vai
// pra `proveniencia` — o registro de que o conteúdo mudou embaixo do
// catálogo é justamente o que uma auditoria precisa provar depois.
ResumoReconciliacao verificacaoCompleta(matriz::db::Database& registro, const std::string& vaultId,
                                         const std::shared_ptr<matriz::app::Cancelamento>& cancelamento = nullptr,
                                         std::function<void(int verificados, int total)> aoProgredir = nullptr);

// Onde mais existe uma cópia íntegra deste conteúdo — a resposta que o
// operador precisa quando um arquivo aparece corrompido (§8). Devolve os
// nomes dos Vaults com o mesmo SHA-256 em estado saudável.
std::vector<std::string> vaultsComCopiaSa(matriz::db::Database& registro, const std::string& checksumSha256,
                                            const std::string& vaultIdExcluir);

// Registra um evento na trilha append-only de proveniência (§4).
void registrarProveniencia(matriz::db::Database& registro, const std::string& itemId, const std::string& evento,
                            const std::string& autor, const std::string& detalhes);

} // namespace matriz::vault
