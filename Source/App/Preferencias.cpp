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
    opts.millisecondsBeforeSaving = -1;
    return opts;
}

} // namespace

std::unique_ptr<juce::PropertiesFile> g_arquivoPreferencias;

void inicializarPreferencias() {
    g_arquivoPreferencias = std::make_unique<juce::PropertiesFile>(opcoesPropriedades());
}

void fecharPreferencias() {
    g_arquivoPreferencias.reset();
}

juce::PropertiesFile& arquivo() {
    jassert (g_arquivoPreferencias != nullptr);
    return *g_arquivoPreferencias;
}

namespace {

constexpr int kMaxRecentes = 10;

} // namespace

juce::String lerLocale() { return arquivo().getValue("locale", "en"); }

void gravarLocale(const juce::String& locale) {
    arquivo().setValue("locale", locale);
    arquivo().saveIfNeeded();
}

juce::String lerGeminiApiKey() { return arquivo().getValue("gemini_api_key", ""); }

void gravarGeminiApiKey(const juce::String& key) {
    arquivo().setValue("gemini_api_key", key);
    arquivo().saveIfNeeded();
}

juce::String lerTema() { return arquivo().getValue("tema", "light"); }

void gravarTema(const juce::String& tema) {
    arquivo().setValue("tema", tema);
    arquivo().saveIfNeeded();
}

float lerEscalaFonte() {
    return static_cast<float>(arquivo().getDoubleValue("escala_fonte", 1.0));
}

void gravarEscalaFonte(float escala) {
    arquivo().setValue("escala_fonte", juce::var(static_cast<double>(escala)));
    arquivo().saveIfNeeded();
}

IntakeMode lerIntakeMode() {
    auto v = arquivo().getValue("recently_ingested_mode", "time");
    return v == "clear_on_start" ? IntakeMode::ClearOnStart : IntakeMode::Time;
}

void gravarIntakeMode(IntakeMode modo) {
    arquivo().setValue("recently_ingested_mode",
        modo == IntakeMode::ClearOnStart ? "clear_on_start" : "time");
    arquivo().saveIfNeeded();
}

int lerIntakeHoras() {
    return arquivo().getIntValue("recently_ingested_horas", 3);
}

void gravarIntakeHoras(int horas) {
    arquivo().setValue("recently_ingested_horas", horas);
    arquivo().saveIfNeeded();
}

const juce::Time& inicioDaSessao() {
    static const juce::Time t = juce::Time::getCurrentTime();
    return t;
}

bool ehRecemIngerido(const std::string& criadoEmIso) {
    if (criadoEmIso.empty()) return false;
    auto criado = juce::Time::fromISO8601(juce::String(criadoEmIso));
    return criado >= inicioDaSessao();
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
