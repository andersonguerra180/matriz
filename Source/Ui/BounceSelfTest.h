#pragma once

// Bounce List (tag B) de ponta a ponta, sem UI: `BKR Matriz --selftest-bounce`.
// Confere a migração do CHECK de item_marcacao em projeto antigo, a persistência da tag B e os três modos de
// metadados do EXPORT > Bounce List (Original / Embed / Clean, com e sem sidecar) com arquivos reais.

namespace matriz::ui {

int rodarBounceSelfTest();

} // namespace matriz::ui
