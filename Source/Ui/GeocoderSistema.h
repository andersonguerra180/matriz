#pragma once

#include <JuceHeader.h>
#include <functional>
#include <utility>
#include <vector>

#include "GeoAcoes.h"

// Serviço de endereços do próprio sistema operacional, usado pelo GET de GEO LOCATION antes do Nominatim:
// gratuito, sem chave nem conta e mantido pelo fabricante do sistema (macOS: CLGeocoder, CoreLocation).
// Windows: sem equivalente gratuito (o MapLocationFinder exige chave do Bing Maps) — disponível() devolve false e o
// GET vai direto ao Nominatim (OpenStreetMap). WINPORT: geocoder do sistema no Windows.
namespace matriz::ui::geoacoes {

using Achados = std::vector<std::pair<Local, juce::String>>;  // lugar + rótulo para o menu de escolha

bool geocoderDoSistemaDisponivel();

// `aoConcluir` volta na message thread; vazio = nada achado (ou sem rede). Chamar da message thread.
void buscarNoSistema(const juce::String& consulta, std::function<void(Achados)> aoConcluir);
void reverterNoSistema(double lat, double lng, std::function<void(Achados)> aoConcluir);

} // namespace matriz::ui::geoacoes
