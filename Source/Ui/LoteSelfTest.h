#pragma once

// Batch assignment de ponta a ponta (regressão "aplico Source Medium em
// lote e só 1 item muda"). Roda como `BKR Matriz --selftest-lote`: abre um
// MainComponent real, seleciona 12 itens no Catalog e 12 no Intake e aplica
// Source Medium, Creator, Content, Subject, Event Date e Geo Location pelos
// mesmos caminhos da UI; confere no banco que os N itens foram gravados,
// na memória da tela sem reabrir, e o desfazer do lote.

namespace matriz::ui {

int rodarLoteSelfTest();

} // namespace matriz::ui
