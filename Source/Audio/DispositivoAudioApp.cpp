#include "DispositivoAudioApp.h"

#include "../App/Preferencias.h"

namespace matriz::audio {

juce::AudioDeviceManager& dispositivoAudioDoApp() {
    static juce::AudioDeviceManager dm;
    return dm;
}

void garantirDispositivoAudioDoApp() {
    static bool aberto = false;
    if (aberto) return;
    aberto = true;
    auto& dm = dispositivoAudioDoApp();
    std::unique_ptr<juce::XmlElement> salvo;
    const auto xml = matriz::app::lerEstadoDispositivoAudio();
    if (xml.isNotEmpty()) salvo = juce::parseXML(xml);
    dm.initialise(0, 2, salvo.get(), true);  // sem entrada - só reprodução
    if (salvo == nullptr) {
        auto setup = dm.getAudioDeviceSetup();
        setup.bufferSize = 1024;
        dm.setAudioDeviceSetup(setup, true);
    }
}

void gravarEscolhaDispositivoAudio() {
    if (auto estado = dispositivoAudioDoApp().createStateXml())
        matriz::app::gravarEstadoDispositivoAudio(estado->toString());
}

} // namespace matriz::audio
