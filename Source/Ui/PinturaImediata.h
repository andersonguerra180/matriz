#pragma once

#include <JuceHeader.h>

namespace matriz::ui {

// Pinta `c` AGORA, sem passar pelo loop de mensagens: mostra progresso enquanto a message thread está
// presa num trabalho síncrono (ex.: edição em lote da ficha, que roda numa transação só). Não despacha
// mensagens — nada de runDispatchLoopUntil (reentrância, ver AGENTS.md) —, só redesenha o retângulo do
// componente. Só na message thread.
void pintarAgora(juce::Component& c);

}  // namespace matriz::ui
