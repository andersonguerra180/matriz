#pragma once

// Benchmark headless da virtualização do mosaico (B.2): prova que
// paint()/resized() não escalam com o total de itens — só as células
// visíveis custam trabalho. Roda via `MATRIZ --selftest-mosaico-10k`
// (ver Main.cpp), sem precisar de tela nem de janela visível — só um
// juce::Image + juce::Graphics off-screen, dentro do ambiente JUCE já
// inicializado pela própria JUCEApplication.

namespace matriz::ui {

// Imprime o relatório no stdout e retorna 0 se tudo ficou dentro do
// esperado, 1 caso contrário.
int rodarStressTestMosaico10k();

} // namespace matriz::ui
