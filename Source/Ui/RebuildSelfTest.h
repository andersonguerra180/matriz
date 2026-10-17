#pragma once

// Cópia verificada + rebuild do MAIN de ponta a ponta, sem UI: `BKR Matriz --selftest-rebuild`.
// Confere: hash/tamanho do que entra no MAIN, recópia de arquivo truncado, cancelamento sem .mtz-tmp,
// rebuild a partir do MAIN antigo (com e sem SOURCE, com embed simulado), MAIN antigo intocado e o fim do
// modo (destino novo vira ORIGINAL, clones antigos desatualizados).

namespace matriz::ui {

int rodarRebuildSelfTest();

} // namespace matriz::ui
