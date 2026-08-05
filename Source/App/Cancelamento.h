#pragma once

#include <atomic>
#include <memory>

// Cancelamento de operação longa (item 10 das correções de operação).
//
// Contrato, igual pra toda operação longa do software:
//
//   1. O cancelamento é IMEDIATO do ponto de vista do operador — no máximo
//      um arquivo em curso termina. Por isso a checagem fica ANTES de cada
//      unidade de trabalho, nunca só no fim do laço.
//   2. O que já foi processado permanece válido: nunca é revertido nem
//      descartado. Cancelar não é rollback.
//   3. Retomar continua de onde parou, sem refazer o que já estava pronto.
//   4. Fechar o aplicativo durante a operação equivale a cancelar, com a
//      mesma garantia.
//
// Deliberadamente burro: um átomo e nada mais. Quem cancela chama pedir();
// quem trabalha checa pedido() entre unidades. Compartilhado por
// shared_ptr porque os jobs em background sobrevivem à função que os criou.

namespace matriz::app {

class Cancelamento {
public:
    void pedir() { pedido_.store(true); }
    bool pedido() const { return pedido_.load(); }

    // Um lote novo reusa o mesmo objeto; sem isto, cancelar uma vez
    // deixaria toda operação seguinte nascendo já cancelada.
    void rearmar() { pedido_.store(false); }

private:
    std::atomic<bool> pedido_{false};
};

using CancelamentoPtr = std::shared_ptr<Cancelamento>;

inline CancelamentoPtr novoCancelamento() { return std::make_shared<Cancelamento>(); }

} // namespace matriz::app
