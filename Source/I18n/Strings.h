#pragma once

#include <JuceHeader.h>

// Strings de interface externalizadas desde a primeira tela (§0.6, B.5).
// Nenhum literal de texto de UI deve viver dentro de código de Component —
// tudo passa por t("chave.pontilhada").
//
// IDIOMAS SUPORTADOS:
// - Inglês (EN-US): idioma padrão da interface (Source/Ui/Strings.h).
// - Português do Brasil (PT-BR): selecionável em Preferências (Source/Ui/StringsPt.h).
// Valores que vêm do banco em português (estado de item, prioridade, estado de presença)
// são traduzidos na hora pela própria t() conforme o idioma ativo.

namespace matriz::i18n {

// Carrega o locale ativo ("en" ou "pt_BR"). Padrão: "en".
void carregar(const juce::String& locale = "en");

// Retorna o identificador do locale atualmente ativo ("en" ou "pt_BR").
juce::String localeAtivo();

// Busca a string de `chave`. Se a chave não existir na tabela, devolve
// "[chave]" — nunca lança, nunca trava a UI, mas deixa óbvio visualmente que
// falta uma string (fácil de grepar nos testes).
juce::String t(const juce::String& chave);

// Igual a t(), mas devolve `textoOriginal` (não "[chave]") quando a chave
// não existe. Usado pelas 14 definições de ficha (fichas/*.yaml): os
// rótulos ali são texto fixo em português, e traduzir as ~300+ chaves
// (tipo/grupo/campo/opção × 14 fichas) é trabalho incremental — enquanto
// uma chave específica não tem tradução, mostrar o texto original do YAML
// é sempre melhor que um "[chave]" visível pro operador.
juce::String tComFallback(const juce::String& chave, const juce::String& textoOriginal);

// Diferente de t()/tComFallback(), que sempre devolvem algo (chave
// decorada ou texto de fallback), existe() diz se `chave` está de fato na
// tabela do locale carregado — usado por selftests que precisam distinguir
// "tem tradução" de "caiu no fallback".
bool existe(const juce::String& chave);

} // namespace matriz::i18n
