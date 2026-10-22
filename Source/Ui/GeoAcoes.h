#pragma once

#include <JuceHeader.h>
#include <functional>

// Ações comuns de todas as fichas de GEO LOCATION (questionários de entrada, ficha de metadados, lote, popup do Intake):
// GET puxa o local atual e OPEN abre o Google Maps naquelas coordenadas (ou no endereço).
namespace matriz::ui::geoacoes {

struct Local {
    juce::String coords, endereco, cidade, estado, pais;  // coords = "lat, lng"
};

// GET: local aproximado pelo IP (sem GPS no desktop). A consulta sai da thread de interface e só roda quando o usuário
// clica; `aoConcluir` volta na message thread (vazio = sem rede/sem resposta). Quem chama protege `this` com SafePointer.
void obterLocalAtual(std::function<void(Local)> aoConcluir);

// OPEN: abre o navegador no Google Maps — coordenadas válidas primeiro; sem elas, o endereço (+ cidade, estado, país).
// Devolve false (e não abre nada) quando não há nada preenchido.
bool abrirNoGoogleMaps(const Local& local);

// URL que abrirNoGoogleMaps usa ("" = nada preenchido). Separada para o self-test.
juce::String urlDoGoogleMaps(const Local& local);

} // namespace matriz::ui::geoacoes
