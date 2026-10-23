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

// GET de todas as fichas: com Coordinates, Address, City, State e Country TODOS vazios, o local atual (obterLocalAtual).
// Com algo preenchido, procura o lugar pelo que foi digitado (OpenStreetMap/Nominatim, só quando o usuário clica):
// endereço/cidade/estado/país -> coordenadas; só coordenadas -> endereço. Vários lugares possíveis abrem um menu em
// `alvo` para escolher; nenhum mostra um aviso e não chama `aoConcluir`. O Local devolvido mantém o que foi digitado e só
// preenche os campos vazios (as coordenadas vêm da busca). `aoConcluir` volta na message thread.
void obterLocal(const Local& preenchido, juce::Component* alvo, std::function<void(Local)> aoConcluir);

// O que obterLocal faz com `preenchido` (separado para o self-test).
enum class ModoGet { LocalAtual, PorEndereco, PorCoordenadas };
ModoGet modoDoGet(const Local& preenchido);

// Junta o digitado com o achado: campos digitados ficam, vazios recebem o achado; as coordenadas são as do achado, exceto
// em PorCoordenadas (ficam as digitadas).
Local mesclarAchado(const Local& preenchido, const Local& achado, ModoGet modo);

// OPEN: abre o navegador no Google Maps — coordenadas válidas primeiro; sem elas, o endereço (+ cidade, estado, país).
// Devolve false (e não abre nada) quando não há nada preenchido.
bool abrirNoGoogleMaps(const Local& local);

// URL que abrirNoGoogleMaps usa ("" = nada preenchido). Separada para o self-test.
juce::String urlDoGoogleMaps(const Local& local);

} // namespace matriz::ui::geoacoes
