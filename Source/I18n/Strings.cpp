#include "Strings.h"

#include "I18nBinaryData.h"

#include <yaml-cpp/yaml.h>

#include <unordered_map>

namespace matriz::i18n {

namespace {

std::unordered_map<std::string, std::string>& tabela() {
    static std::unordered_map<std::string, std::string> t;
    return t;
}

void achatar(const YAML::Node& node, const std::string& prefixo, std::unordered_map<std::string, std::string>& out) {
    if (node.IsScalar()) {
        out[prefixo] = node.as<std::string>();
        return;
    }
    if (node.IsMap()) {
        for (const auto& par : node) {
            std::string chave = par.first.as<std::string>();
            std::string novoPrefixo = prefixo.empty() ? chave : prefixo + "." + chave;
            achatar(par.second, novoPrefixo, out);
        }
    }
}

const char* dadosParaLocale(const juce::String& locale, int& tamanho) {
    if (locale == "en") {
        tamanho = I18nBinaryData::en_yamlSize;
        return I18nBinaryData::en_yaml;
    }
    if (locale == "pt_BR") {
        tamanho = I18nBinaryData::pt_BR_yamlSize;
        return I18nBinaryData::pt_BR_yaml;
    }
    tamanho = 0;
    return nullptr;
}

} // namespace

void carregar(const juce::String& locale) {
    int tamanho = 0;
    const char* dados = dadosParaLocale(locale, tamanho);
    tabela().clear();
    if (!dados || tamanho == 0) return;

    std::string texto(dados, static_cast<size_t>(tamanho));
    YAML::Node raiz = YAML::Load(texto);
    achatar(raiz, "", tabela());
}

juce::String t(const juce::String& chave) {
    auto it = tabela().find(chave.toStdString());
    if (it == tabela().end()) return "[" + chave + "]";
    return juce::String(juce::CharPointer_UTF8(it->second.c_str()));
}

} // namespace matriz::i18n
