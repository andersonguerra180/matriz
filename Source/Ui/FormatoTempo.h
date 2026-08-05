#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <optional>

// Parsing/formatação de minutagem digitada à mão em observações (item 9).
// Guardado como inteiro em milissegundos no banco (schema/registro.sql,
// item_observacao.minutagem_ms) — nunca texto — pra não exigir migração de
// dado quando a timeline de áudio/vídeo (fatia futura) passar a gravar
// minutagem de verdade em vez de digitada.

namespace matriz::ui {

// Aceita "MM:SS" ou "HH:MM:SS". nullopt se o texto não bate o formato
// (tokens não numéricos, minutos/segundos >= 60, contagem de partes
// diferente de 2 ou 3) — entrada de formulário, tolerante: nunca lança.
std::optional<int64_t> parsearMinutagem(const juce::String& texto);

// Inverso de parsearMinutagem — "MM:SS" se cabe em menos de uma hora,
// "H:MM:SS" caso contrário.
juce::String formatarMinutagem(int64_t ms);

} // namespace matriz::ui
