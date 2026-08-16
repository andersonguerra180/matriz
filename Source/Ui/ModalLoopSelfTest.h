#pragma once

// Teste de estresse de diálogo (§3, critério 21).
//
// Roda como `BKR Matriz --selftest-modal-loop`: instancia, exibe e descarta
// os diálogos 500 vezes de forma sequencial na message thread, bombeando
// mensagens entre uma e outra. Existe pra rodar com AddressSanitizer ou
// Guard Malloc ligado — é sob eles que um acesso a memória liberada vira
// falha imediata em vez de um crash intermitente em produção.
//
// O que ele cobre não é "o diálogo abre": é que fechar um diálogo enquanto o
// sistema ainda pode repintá-lo não toca em nada já destruído — o modo exato
// do crash em juce::AlertWindow::paint() -> Component::getName() que motivou
// a troca por overlays internos.

namespace matriz::ui {

int rodarModalLoopSelfTest();

} // namespace matriz::ui
