#include "GeoAcoes.h"

#include <cmath>

namespace matriz::ui::geoacoes {

void obterLocalAtual(std::function<void(Local)> aoConcluir) {
    juce::Thread::launch([aoConcluir = std::move(aoConcluir)] {
        Local local;
        try {
            juce::URL u("https://ipapi.co/json/");
            if (auto in = u.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(6000))) {
                const auto json = juce::JSON::parse(in->readEntireStreamAsString());
                if (json.isObject() && json.hasProperty("country_name")) {
                    local.cidade = json["city"].toString();
                    local.estado = json["region"].toString();
                    local.pais = json["country_name"].toString();
                    if (json.hasProperty("latitude") && json.hasProperty("longitude"))
                        local.coords = juce::String(static_cast<double>(json["latitude"]), 5) + ", " + juce::String(static_cast<double>(json["longitude"]), 5);
                }
            }
        } catch (...) {}
        juce::MessageManager::callAsync([aoConcluir, local] { if (aoConcluir) aoConcluir(local); });
    });
}

juce::String urlDoGoogleMaps(const Local& local) {
    juce::StringArray partes;
    partes.addTokens(local.coords.replaceCharacters(";", ","), ", ", "");
    partes.removeEmptyStrings();
    juce::String consulta;
    if (partes.size() >= 2) {
        const double lat = partes[0].getDoubleValue(), lng = partes[1].getDoubleValue();
        if (std::abs(lat) <= 90.0 && std::abs(lng) <= 180.0 && (lat != 0.0 || lng != 0.0))
            consulta = juce::String(lat, 6) + "," + juce::String(lng, 6);
    }
    if (consulta.isEmpty()) {
        juce::StringArray endereco;
        for (const auto& p : { local.endereco, local.cidade, local.estado, local.pais })
            if (p.trim().isNotEmpty()) endereco.add(p.trim());
        consulta = endereco.joinIntoString(", ");
    }
    if (consulta.isEmpty()) return {};
    return "https://www.google.com/maps/search/?api=1&query=" + juce::URL::addEscapeChars(consulta, true);
}

bool abrirNoGoogleMaps(const Local& local) {
    const auto url = urlDoGoogleMaps(local);
    if (url.isEmpty()) return false;
    return juce::URL(url).launchInDefaultBrowser();
}

} // namespace matriz::ui::geoacoes
