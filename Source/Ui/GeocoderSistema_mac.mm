#import <CoreLocation/CoreLocation.h>
#import <Foundation/Foundation.h>

#include "GeocoderSistema.h"
#include "../I18n/Strings.h"

namespace matriz::ui::geoacoes {

namespace {

juce::String texto(NSString* s) { return s != nil ? juce::String::fromUTF8([s UTF8String]) : juce::String(); }

// CLPlacemark -> Local (+ rótulo do menu). Endereço = rua, número; sem rua, o nome do lugar (ex.: "Arena Independência").
std::pair<Local, juce::String> doPlacemark(CLPlacemark* p) {
    Local l;
    const auto coord = p.location.coordinate;
    l.coords = juce::String(coord.latitude, 6) + ", " + juce::String(coord.longitude, 6);
    const auto rua = texto(p.thoroughfare), numero = texto(p.subThoroughfare), nome = texto(p.name);
    l.cidade = texto(p.locality);
    l.estado = texto(p.administrativeArea);
    l.pais = texto(p.country);
    if (rua.isNotEmpty()) l.endereco = numero.isNotEmpty() ? rua + ", " + numero : rua;
    else if (nome.isNotEmpty() && nome != l.cidade && nome != l.estado && nome != l.pais) l.endereco = nome;

    juce::StringArray partes;
    for (const auto& t : { nome, l.endereco, texto(p.subLocality), l.cidade, l.estado, l.pais })
        if (t.isNotEmpty() && !partes.contains(t)) partes.add(t);
    return { l, partes.joinIntoString(", ") };
}

void entregar(NSArray<CLPlacemark*>* marcas, std::function<void(Achados)> aoConcluir) {
    Achados achados;
    for (CLPlacemark* p in marcas)
        if (p.location != nil) achados.push_back(doPlacemark(p));
    if (aoConcluir) aoConcluir(std::move(achados));
}

NSLocale* localeDoApp() {
    return [NSLocale localeWithLocaleIdentifier:(matriz::i18n::localeAtivo() == "pt_BR" ? @"pt_BR" : @"en_US")];
}

}  // namespace

bool geocoderDoSistemaDisponivel() { return true; }

// O CLGeocoder chama o bloco na main queue (= message thread do JUCE). Sem ARC: o bloco copiado segura o geocoder até
// a resposta, então dá para soltá-lo logo depois de pedir.
void buscarNoSistema(const juce::String& consulta, std::function<void(Achados)> aoConcluir) {
    CLGeocoder* geocoder = [[CLGeocoder alloc] init];
    auto cb = std::make_shared<std::function<void(Achados)>>(std::move(aoConcluir));
    [geocoder geocodeAddressString:[NSString stringWithUTF8String:consulta.toRawUTF8()]
                          inRegion:nil
                   preferredLocale:localeDoApp()
                 completionHandler:^(NSArray<CLPlacemark*>* marcas, NSError*) { entregar(marcas, *cb); }];
    [geocoder release];
}

void reverterNoSistema(double lat, double lng, std::function<void(Achados)> aoConcluir) {
    CLGeocoder* geocoder = [[CLGeocoder alloc] init];
    CLLocation* local = [[CLLocation alloc] initWithLatitude:lat longitude:lng];
    auto cb = std::make_shared<std::function<void(Achados)>>(std::move(aoConcluir));
    [geocoder reverseGeocodeLocation:local
                     preferredLocale:localeDoApp()
                   completionHandler:^(NSArray<CLPlacemark*>* marcas, NSError*) { entregar(marcas, *cb); }];
    [local release];
    [geocoder release];
}

} // namespace matriz::ui::geoacoes
