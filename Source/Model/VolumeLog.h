#pragma once

#include <JuceHeader.h>

// Log por UNIDADE (disco externo/interno, Time Machine, pendrive, cartão SD, CD/DVD, nuvem…) usada neste backup como
// SOURCE ou DESTINATION. Um arquivo por unidade, dentro da pasta de logs do projeto:
//   <projeto>/logs/volumes/<unidade>.log
// Cada linha:  AAAA-MM-DD HH:MM:SS | SOURCE/DESTINATION | AÇÃO | detalhe
// Tudo o que acontece com a unidade entra aqui (leituras da origem, gravações do backup, erros, mudanças de
// online/offline e de espaço livre). O card da unidade em Statistics abre este histórico + características + saúde.
namespace matriz::model::volumelog {

// Raiz da unidade que contém `caminho`: /Volumes/X, pasta de nuvem (CloudStorage / iCloud) ou a raiz do sistema.
juce::File raizDoVolume(const juce::File& caminho, bool* nuvem = nullptr);

juce::File pastaDeLogs(const juce::File& pastaProjeto);
juce::File arquivoDeLog(const juce::File& pastaProjeto, const juce::File& raizVolume);

// Acrescenta uma linha ao log da unidade que contém `caminhoNoVolume`. `papel`: "SOURCE" ou "DESTINATION".
// Thread-safe; nunca lança.
void registrar(const juce::File& pastaProjeto, const juce::File& caminhoNoVolume, const juce::String& papel,
               const juce::String& acao, const juce::String& detalhe = {});

// Foto do estado da unidade (online/offline, espaço livre): só grava quando algo mudou desde a última foto
// (ou se a última tem mais de 24 h), para o log contar a história sem encher de repetição.
void observar(const juce::File& pastaProjeto, const juce::File& raizVolume, const juce::String& papel);

// Texto do relatório da unidade: características, saúde e histórico (o mais recente primeiro). Pode demorar
// (consulta SMART): chamar fora da message thread.
juce::String relatorioDaUnidade(const juce::File& pastaProjeto, const juce::File& raizVolume);

} // namespace matriz::model::volumelog
