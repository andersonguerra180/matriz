#pragma once

// Harness de teste de UI headless (Parte 1 da correção crítica). JUCE
// renderiza qualquer Component pra uma imagem em memória
// (createComponentSnapshot) sem passar pelo compositor do sistema — não
// precisa de permissão de Gravação de Tela, que é limitação só do
// `screencapture`. Interação real (foco de teclado, clique) usa um peer
// off-screen de verdade (Component::addToDesktop, posicionado fora da
// tela) — cria uma janela real do próprio processo, não inspeciona nem
// controla outro app, então também não precisa de permissão de Acessibilidade.
//
// Roda como `MATRIZ --selftest-uitest`, igual aos outros modos ocultos
// (--selftest-mosaico-10k, --selftest-ingerir-arquivos) — não um alvo
// CMake separado, pra reusar o link completo de GUI do binário `matriz`
// em vez de duplicar ~15 bibliotecas linkadas só pra isto.

namespace matriz::ui {

int rodarUiSelfTest();

} // namespace matriz::ui
