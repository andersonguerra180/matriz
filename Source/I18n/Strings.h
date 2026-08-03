#pragma once

#include <JuceHeader.h>

// Strings de interface externalizadas desde a primeira tela (§0.6, B.5).
// Nenhum literal de texto de UI deve viver dentro de código de Component —
// tudo passa por t("chave.pontilhada"). O texto de verdade mora em
// i18n/en.yaml e i18n/pt_BR.yaml (chave: valor, agrupado por tela),
// embutidos no binário via juce_add_binary_data, igual ao schema do banco.
//
// Inglês é o padrão (Parte 2 da correção de fluxo — abre em inglês
// independente do idioma do sistema); português fica disponível em
// Preferences, trocável em tempo real (carregar() pode ser chamado de
// novo a qualquer momento — recarrega a tabela inteira, sem precisar
// reiniciar o processo).

namespace matriz::i18n {

// Carrega o locale ("en" ou "pt_BR"). Chamado uma vez no início do
// programa e de novo sempre que o operador troca o idioma em Preferences.
void carregar(const juce::String& locale = "en");

// Busca a string traduzida pra `chave`. Se a chave não existir na tabela
// carregada, devolve "[chave]" — nunca lança, nunca trava a UI, mas deixa
// óbvio visualmente que falta tradução (fácil de grepar nos testes).
juce::String t(const juce::String& chave);

} // namespace matriz::i18n
