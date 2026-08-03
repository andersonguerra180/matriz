#pragma once

#include <functional>

#include "ProjetoAberto.h"

// Configurações do projeto (B.4, P1). O toggle "permitir sobrescrever
// master" mostra o aviso de consequência uma única vez — a confirmação fica
// gravada em projeto.aviso_sobrescrita_confirmado_em e nunca mais aparece
// depois disso, mesmo desligando e religando a opção.

namespace matriz::ui {

void mostrarDialogoConfiguracoesProjeto(ProjetoAberto& projeto, std::function<void()> aoFechar);

} // namespace matriz::ui
