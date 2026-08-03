#include "Preferencias.h"

#include <algorithm>

namespace matriz::app {

namespace {

juce::PropertiesFile::Options opcoesPropriedades() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "MATRIZ";
    opts.filenameSuffix = "settings";
    opts.folderName = "MATRIZ";
    opts.osxLibrarySubFolder = "Application Support";
    return opts;
}

juce::PropertiesFile& arquivo() {
    static juce::PropertiesFile instancia(opcoesPropriedades());
    return instancia;
}

constexpr int kMaxRecentes = 10;

} // namespace

juce::String lerLocale() { return arquivo().getValue("locale", "en"); }

void gravarLocale(const juce::String& locale) {
    arquivo().setValue("locale", locale);
    arquivo().saveIfNeeded();
}

std::vector<ProjetoRecente> lerRecentes() {
    std::vector<ProjetoRecente> out;
    auto xml = arquivo().getXmlValue("recentes");
    if (!xml) return out;

    for (auto* filho : xml->getChildIterator()) {
        juce::String pasta = filho->getStringAttribute("pasta");
        if (!juce::File(pasta).isDirectory()) continue; // projeto movido/apagado — some da lista
        out.push_back({pasta, filho->getStringAttribute("nome"), filho->getStringAttribute("modo")});
    }
    return out;
}

void registrarRecente(const juce::String& pasta, const juce::String& nome, const juce::String& modo) {
    auto recentes = lerRecentes();
    recentes.erase(std::remove_if(recentes.begin(), recentes.end(),
                                   [&](const ProjetoRecente& r) { return r.pasta == pasta; }),
                   recentes.end());
    recentes.insert(recentes.begin(), {pasta, nome, modo});
    if (recentes.size() > static_cast<size_t>(kMaxRecentes)) recentes.resize(static_cast<size_t>(kMaxRecentes));

    juce::XmlElement raiz("recentes");
    for (auto& r : recentes) {
        auto* item = raiz.createNewChildElement("item");
        item->setAttribute("pasta", r.pasta);
        item->setAttribute("nome", r.nome);
        item->setAttribute("modo", r.modo);
    }
    arquivo().setValue("recentes", &raiz);
    arquivo().saveIfNeeded();
}

} // namespace matriz::app
