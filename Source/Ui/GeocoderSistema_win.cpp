#include "GeocoderSistema.h"

// WINPORT: geocoder do sistema no Windows. O Windows.Services.Maps.MapLocationFinder exige uma chave do Bing Maps (sem
// plano gratuito), então aqui não há serviço do sistema e o GET de GEO LOCATION usa só o Nominatim (OpenStreetMap).
namespace matriz::ui::geoacoes {

bool geocoderDoSistemaDisponivel() { return false; }

void buscarNoSistema(const juce::String&, std::function<void(Achados)> aoConcluir) {
    if (aoConcluir) aoConcluir({});
}

void reverterNoSistema(double, double, std::function<void(Achados)> aoConcluir) {
    if (aoConcluir) aoConcluir({});
}

} // namespace matriz::ui::geoacoes
