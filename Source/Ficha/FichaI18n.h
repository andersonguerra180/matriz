#pragma once

#include <JuceHeader.h>

#include <string>

#include "FichaDefinition.h"

// Camada de tradução por cima das 14 definições de ficha (fichas/*.yaml).
// Os "rotulo:" ali são texto fixo em português — nunca passaram pelo
// sistema de i18n (achado durante a correção crítica pós-teste do
// usuário: mesmo com locale=en, a ficha inteira aparecia em português).
//
// Grupo usa a chave estável Grupo::chave (slug derivado do rótulo, ver
// FichaDefinition.h) — não mais a posição na lista: chave por posição
// desincroniza em silêncio quando um grupo é inserido/reordenado no meio
// de um YAML existente (i18n/en.yaml tinha um aviso sobre isso, corrigido
// junto com a descoberta de fichas em runtime). Opção continua por índice
// (não tem id próprio no formato). campo.id já é estável, então campo usa
// a chave por id.
//
// Sempre com fallback pro texto original do YAML (tComFallback) — uma
// chave sem tradução ainda não é erro nem quebra nada, só mostra o texto
// em português até alguém traduzir. Ver i18n/en.yaml, seções
// ficha_tipos/ficha_grupos/ficha_campos/ficha_opcoes.

namespace matriz::ficha {

// Rótulo de exibição do tipo de mídia (usado como nome do tipo no diálogo
// de escolha, cabeçalho de grupo do mosaico Archive, e como título da
// seção raiz de fichas com níveis).
juce::String rotuloTipo(const std::string& tipo, const std::string& rotuloOriginal);

// Rótulo de um grupo (fichas sem níveis — grupos são só organização
// visual, §6.2), pela chave estável do grupo (Grupo::chave).
juce::String rotuloGrupo(const std::string& tipo, const std::string& chaveGrupo, const std::string& rotuloOriginal);

// Rótulo de um campo, pelo id estável do campo.
juce::String rotuloCampo(const std::string& tipo, const Campo& campo);

// Texto de exibição de uma opção (tipo Opcao/OpcaoLivre), pela posição na
// lista `opcoes` do campo. O valor BRUTO gravado no banco nunca muda —
// isto só afeta o que aparece na tela.
juce::String rotuloOpcao(const std::string& tipo, const Campo& campo, int indiceOpcao, const std::string& valorBruto);

// Texto do aviso (alerta_se_true) de um campo booleano — outro texto fixo
// em português no YAML original, mesma lógica de fallback.
juce::String rotuloAlerta(const std::string& tipo, const Campo& campo);

} // namespace matriz::ficha
