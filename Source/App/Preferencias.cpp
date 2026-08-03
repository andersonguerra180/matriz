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

std::vector<ProjetoRecente> comRecenteNoTopo(std::vector<ProjetoRecente> lista, const ProjetoRecente& novo,
                                              size_t maximo) {
    lista.erase(std::remove_if(lista.begin(), lista.end(),
                                [&](const ProjetoRecente& r) { return r.pasta == novo.pasta; }),
                lista.end());
    lista.insert(lista.begin(), novo);
    if (lista.size() > maximo) lista.resize(maximo);
    return lista;
}

void registrarRecente(const juce::String& pasta, const juce::String& nome, const juce::String& modo) {
    auto recentes = comRecenteNoTopo(lerRecentes(), {pasta, nome, modo}, static_cast<size_t>(kMaxRecentes));

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
