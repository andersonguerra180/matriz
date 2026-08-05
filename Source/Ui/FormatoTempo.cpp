#include "FormatoTempo.h"

namespace matriz::ui {

std::optional<int64_t> parsearMinutagem(const juce::String& texto) {
    juce::String limpo = texto.trim();
    if (limpo.isEmpty()) return std::nullopt;

    juce::StringArray partes;
    partes.addTokens(limpo, ":", "");
    if (partes.size() < 2 || partes.size() > 3) return std::nullopt;
    for (auto& p : partes)
        if (p.isEmpty() || !p.containsOnly("0123456789")) return std::nullopt;

    int64_t horas = 0;
    int64_t minutos, segundos;
    if (partes.size() == 3) {
        horas = static_cast<int64_t>(partes[0].getLargeIntValue());
        minutos = partes[1].getLargeIntValue();
        segundos = partes[2].getLargeIntValue();
    } else {
        minutos = partes[0].getLargeIntValue();
        segundos = partes[1].getLargeIntValue();
    }
    if (minutos >= 60 || segundos >= 60) return std::nullopt;

    int64_t totalSegundos = horas * 3600 + minutos * 60 + segundos;
    return totalSegundos * 1000;
}

juce::String formatarMinutagem(int64_t ms) {
    if (ms < 0) ms = 0;
    int64_t totalSegundos = ms / 1000;
    int64_t horas = totalSegundos / 3600;
    int64_t minutos = (totalSegundos % 3600) / 60;
    int64_t segundos = totalSegundos % 60;

    juce::String mm = juce::String(minutos).paddedLeft('0', 2);
    juce::String ss = juce::String(segundos).paddedLeft('0', 2);
    if (horas > 0) return juce::String(horas) + ":" + mm + ":" + ss;
    return mm + ":" + ss;
}

} // namespace matriz::ui
