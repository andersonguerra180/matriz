#include "GeoAcoes.h"

#include <cmath>
#include <vector>

#include "../I18n/Strings.h"
#include "GeocoderSistema.h"

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

namespace {

bool coordsValidas(const juce::String& coords, double& lat, double& lng) {
    juce::StringArray partes;
    partes.addTokens(coords.replaceCharacters(";", ","), ", ", "");
    partes.removeEmptyStrings();
    if (partes.size() < 2) return false;
    lat = partes[0].getDoubleValue();
    lng = partes[1].getDoubleValue();
    return std::abs(lat) <= 90.0 && std::abs(lng) <= 180.0 && (lat != 0.0 || lng != 0.0);
}

// Um resultado do Nominatim (jsonv2 com addressdetails) como Local; `rotulo` = display_name, para o menu de escolha.
Local localDoNominatim(const juce::var& r, juce::String& rotulo) {
    Local l;
    const double lat = r["lat"].toString().getDoubleValue(), lng = r["lon"].toString().getDoubleValue();
    if (lat != 0.0 || lng != 0.0) l.coords = juce::String(lat, 6) + ", " + juce::String(lng, 6);
    const auto a = r["address"];
    auto primeiro = [&a](std::initializer_list<const char*> chaves) {
        for (auto* k : chaves)
            if (a.hasProperty(k) && a[k].toString().isNotEmpty()) return a[k].toString();
        return juce::String();
    };
    const auto rua = primeiro({ "road", "pedestrian", "street", "square", "neighbourhood", "suburb" });
    const auto numero = primeiro({ "house_number" });
    l.endereco = rua.isNotEmpty() && numero.isNotEmpty() ? rua + ", " + numero : rua;
    l.cidade = primeiro({ "city", "town", "village", "municipality", "hamlet", "county" });
    l.estado = primeiro({ "state", "region", "state_district", "province" });
    l.pais = primeiro({ "country" });
    rotulo = r["display_name"].toString();
    return l;
}

// Consulta o Nominatim (fora da message thread). Resposta de busca = array; de reverse = um objeto.
Achados consultarNominatim(const juce::String& url) {
    Achados out;
    try {
        juce::URL u(url);
        // A política de uso do Nominatim exige identificar o aplicativo; uma consulta por clique em GET.
        auto opcoes = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                          .withConnectionTimeoutMs(8000)
                          .withExtraHeaders("User-Agent: BKR Matriz (desktop catalog app)\r\n");
        if (auto in = u.createInputStream(opcoes)) {
            const auto json = juce::JSON::parse(in->readEntireStreamAsString());
            auto somar = [&out](const juce::var& r) {
                if (!r.isObject() || !r.hasProperty("lat")) return;
                juce::String rotulo;
                auto l = localDoNominatim(r, rotulo);
                // A cidade e o município costumam vir com o mesmo nome: um só no menu.
                for (const auto& [_, r2] : out)
                    if (r2 == rotulo) return;
                if (l.coords.isNotEmpty()) out.emplace_back(std::move(l), std::move(rotulo));
            };
            if (auto* arr = json.getArray()) for (const auto& r : *arr) somar(r);
            else somar(json);
        }
    } catch (...) {}
    return out;
}

}  // namespace

ModoGet modoDoGet(const Local& p) {
    const bool algumTexto = p.endereco.trim().isNotEmpty() || p.cidade.trim().isNotEmpty() || p.estado.trim().isNotEmpty() ||
                            p.pais.trim().isNotEmpty();
    if (algumTexto) return ModoGet::PorEndereco;
    double lat = 0, lng = 0;
    if (coordsValidas(p.coords, lat, lng)) return ModoGet::PorCoordenadas;
    return ModoGet::LocalAtual;  // tudo vazio (ou coordenada inválida e nada mais)
}

Local mesclarAchado(const Local& p, const Local& achado, ModoGet modo) {
    auto manter = [](const juce::String& digitado, const juce::String& novo) { return digitado.trim().isNotEmpty() ? digitado : novo; };
    Local out;
    out.coords = modo == ModoGet::PorCoordenadas ? p.coords : (achado.coords.isNotEmpty() ? achado.coords : p.coords);
    out.endereco = manter(p.endereco, achado.endereco);
    out.cidade = manter(p.cidade, achado.cidade);
    out.estado = manter(p.estado, achado.estado);
    out.pais = manter(p.pais, achado.pais);
    return out;
}

void obterLocal(const Local& preenchido, juce::Component* alvo, std::function<void(Local)> aoConcluir) {
    const auto modo = modoDoGet(preenchido);
    if (modo == ModoGet::LocalAtual) { obterLocalAtual(std::move(aoConcluir)); return; }

    const juce::String idioma = matriz::i18n::localeAtivo() == "pt_BR" ? "pt-BR,pt,en" : "en";
    juce::String url, consulta;
    if (modo == ModoGet::PorCoordenadas) {
        double lat = 0, lng = 0;
        coordsValidas(preenchido.coords, lat, lng);
        consulta = juce::String(lat, 6) + ", " + juce::String(lng, 6);
        url = "https://nominatim.openstreetmap.org/reverse?format=jsonv2&addressdetails=1&zoom=18&lat=" + juce::String(lat, 6) +
              "&lon=" + juce::String(lng, 6) + "&accept-language=" + juce::URL::addEscapeChars(idioma, true);
    } else {
        juce::StringArray partes;
        for (const auto& t : { preenchido.endereco, preenchido.cidade, preenchido.estado, preenchido.pais })
            if (t.trim().isNotEmpty()) partes.add(t.trim());
        consulta = partes.joinIntoString(", ");
        url = "https://nominatim.openstreetmap.org/search?format=jsonv2&addressdetails=1&limit=6&q=" +
              juce::URL::addEscapeChars(consulta, true) + "&accept-language=" + juce::URL::addEscapeChars(idioma, true);
    }

    // 1º o serviço do sistema (macOS: Apple, grátis e sem chave); sem resultado — ou no Windows, que não tem —, o
    // Nominatim (OpenStreetMap). Os dois só rodam quando o usuário clica em GET.
    juce::Component::SafePointer<juce::Component> seguro(alvo);
    auto apresentar = [consulta, preenchido, modo, seguro, aoConcluir](Achados achados) {
        if (achados.empty()) {
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::InfoIcon)
                                             .withTitle(matriz::i18n::t("imp.ficha.geo.get"))
                                             .withMessage(matriz::i18n::t("imp.ficha.geo.nada_achado").replace("{q}", consulta))
                                             .withButton(matriz::i18n::t("dialogo.ok")),
                                         nullptr);
            return;
        }
        if (achados.size() == 1 || modo == ModoGet::PorCoordenadas) {
            if (aoConcluir) aoConcluir(mesclarAchado(preenchido, achados.front().first, modo));
            return;
        }
        if (seguro == nullptr) return;  // a ficha que pediu já fechou
        juce::PopupMenu menu;
        menu.addSectionHeader(matriz::i18n::t("imp.ficha.geo.escolher"));
        for (size_t i = 0; i < achados.size(); ++i) menu.addItem(static_cast<int>(i) + 1, achados[i].second);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(seguro.getComponent()),
                           [achados, preenchido, modo, aoConcluir](int escolha) {
                               if (escolha < 1 || escolha > static_cast<int>(achados.size()) || !aoConcluir) return;
                               aoConcluir(mesclarAchado(preenchido, achados[static_cast<size_t>(escolha - 1)].first, modo));
                           });
    };
    auto noNominatim = [url, apresentar] {
        juce::Thread::launch([url, apresentar] {
            auto achados = consultarNominatim(url);
            juce::MessageManager::callAsync([achados = std::move(achados), apresentar]() mutable { apresentar(std::move(achados)); });
        });
    };
    if (!geocoderDoSistemaDisponivel()) { noNominatim(); return; }
    auto depoisDoSistema = [apresentar, noNominatim](Achados achados) {
        if (achados.empty()) noNominatim();
        else apresentar(std::move(achados));
    };
    if (modo == ModoGet::PorCoordenadas) {
        double lat = 0, lng = 0;
        coordsValidas(preenchido.coords, lat, lng);
        reverterNoSistema(lat, lng, depoisDoSistema);
    } else {
        buscarNoSistema(consulta, depoisDoSistema);
    }
}

namespace {
juce::String urlReverso(double lat, double lng) {
    const juce::String idioma = matriz::i18n::localeAtivo() == "pt_BR" ? "pt-BR,pt,en" : "en";
    return "https://nominatim.openstreetmap.org/reverse?format=jsonv2&addressdetails=1&zoom=18&lat=" + juce::String(lat, 6) +
           "&lon=" + juce::String(lng, 6) + "&accept-language=" + juce::URL::addEscapeChars(idioma, true);
}

struct EstadoLoteReverso {
    std::vector<std::pair<double, double>> pontos;
    std::vector<std::optional<Local>> resultados;
    size_t proximo = 0;
    std::shared_ptr<std::atomic<bool>> cancelado;
    std::function<void(int, int)> aoProgredir;
    std::function<void(std::vector<std::optional<Local>>)> aoConcluir;
};

void passoLoteReverso(std::shared_ptr<EstadoLoteReverso> e) {
    if ((e->cancelado && e->cancelado->load()) || e->proximo >= e->pontos.size()) {
        if (e->aoConcluir) e->aoConcluir(std::move(e->resultados));
        return;
    }
    const auto [lat, lng] = e->pontos[e->proximo];
    auto terminar = [e](Achados achados) {
        if (!achados.empty()) e->resultados[e->proximo] = achados.front().first;
        ++e->proximo;
        if (e->aoProgredir) e->aoProgredir(static_cast<int>(e->proximo), static_cast<int>(e->pontos.size()));
        const int pausa = e->proximo < e->pontos.size() ? 1200 : 0;
        juce::Timer::callAfterDelay(pausa, [e] { passoLoteReverso(e); });
    };
    auto noNominatim = [lat, lng, terminar] {
        juce::Thread::launch([lat, lng, terminar] {
            auto achados = consultarNominatim(urlReverso(lat, lng));
            juce::MessageManager::callAsync([achados = std::move(achados), terminar]() mutable { terminar(std::move(achados)); });
        });
    };
    if (!geocoderDoSistemaDisponivel()) { noNominatim(); return; }
    reverterNoSistema(lat, lng, [terminar, noNominatim](Achados achados) {
        if (achados.empty()) noNominatim();
        else terminar(std::move(achados));
    });
}
}  // namespace

void reverterEmLote(std::vector<std::pair<double, double>> pontos, std::shared_ptr<std::atomic<bool>> cancelado,
                    std::function<void(int, int)> aoProgredir, std::function<void(std::vector<std::optional<Local>>)> aoConcluir) {
    auto e = std::make_shared<EstadoLoteReverso>();
    e->resultados.resize(pontos.size());
    e->pontos = std::move(pontos);
    e->cancelado = std::move(cancelado);
    e->aoProgredir = std::move(aoProgredir);
    e->aoConcluir = std::move(aoConcluir);
    passoLoteReverso(e);
}

void confirmarTrocaDoGpsDoArquivo(int quantos, const juce::String& novo, std::function<void()> aoConfirmar,
                                  std::function<void()> aoCancelar) {
    const auto msg = matriz::i18n::t("geo.trocar_gps_msg")
                         .replace("{n}", juce::String(quantos))
                         .replace("{novo}", novo.isNotEmpty() ? novo : matriz::i18n::t("geo.trocar_gps_sem_coords"));
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(matriz::i18n::t("geo.trocar_gps_titulo"))
                                     .withMessage(msg)
                                     .withButton(matriz::i18n::t("geo.trocar_gps_sim"))
                                     .withButton(matriz::i18n::t("comum.cancelar")),
                                 [aoConfirmar, aoCancelar](int r) {
                                     if (r == 1) { if (aoConfirmar) aoConfirmar(); }
                                     else if (aoCancelar) aoCancelar();
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
