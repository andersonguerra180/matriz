#pragma once

#include <JuceHeader.h>

// Strings de interface externalizadas desde a primeira tela (§0.6, B.5).
// Nenhum literal de texto de UI deve viver dentro de código de Component —
// tudo passa por t("chave.pontilhada"). O texto de verdade mora em
// i18n/pt_BR.yaml (chave: valor, agrupado por tela), embutido no binário
// via juce_add_binary_data, igual ao schema do banco.
//
// Só pt_BR está populado agora — é o único idioma da interface que existe
// hoje. A arquitetura já é multi-locale (carregar() recebe o nome do
// locale, t() nunca quebra se a chave não existir), pronta pra quando um
// segundo idioma for de fato decidido; não escrevemos inglês incompleto só
// pra "existir" (isso violaria a regra de nunca deixar conteúdo pela
// metade — §0 regra 1).

namespace matriz::i18n {

// Carrega o locale (hoje só "pt_BR" tem conteúdo real). Chamar uma vez, no
// início do programa, antes de qualquer Component ser construído.
void carregar(const juce::String& locale = "pt_BR");

// Busca a string traduzida pra `chave`. Se a chave não existir na tabela
// carregada, devolve "[chave]" — nunca lança, nunca trava a UI, mas deixa
// óbvio visualmente que falta tradução (fácil de grepar nos testes).
juce::String t(const juce::String& chave);

} // namespace matriz::i18n
