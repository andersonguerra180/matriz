#include "Strings.h"
#include "../Ui/Strings.h"
#include "../Ui/StringsPt.h"

namespace matriz::i18n {

namespace {
std::string g_activeLocale = "en";
}

void carregar(const juce::String& locale) {
    if (locale.equalsIgnoreCase("pt_BR") || locale.equalsIgnoreCase("pt-BR") || locale.equalsIgnoreCase("pt")) {
        g_activeLocale = "pt_BR";
    } else {
        g_activeLocale = "en";
    }
}

juce::String localeAtivo() {
    return juce::String(g_activeLocale);
}

juce::String t(const juce::String& chave) {
    const std::string key = chave.toStdString();

    if (g_activeLocale == "pt_BR") {
        const auto& ptStrings = getPortugueseStrings();
        auto it = ptStrings.find(key);
        if (it != ptStrings.end()) {
            return juce::String(juce::CharPointer_UTF8(it->second.c_str()));
        }
        const auto& enStrings = getEnglishStrings();
        auto itEn = enStrings.find(key);
        if (itEn != enStrings.end()) {
            return juce::String(juce::CharPointer_UTF8(itEn->second.c_str()));
        }

        if (key == "novo") return juce::String::fromUTF8("Novo");
        if (key == "em_analise") return juce::String::fromUTF8("Em Análise");
        if (key == "catalogado") return juce::String::fromUTF8("Catalogado");
        if (key == "revisado") return juce::String::fromUTF8("Revisado");
        if (key == "aprovado") return juce::String::fromUTF8("Aprovado");
        if (key == "publicado") return juce::String::fromUTF8("Publicado");
        if (key == "arquivado") return juce::String::fromUTF8("Arquivado");
        if (key == "baixa") return juce::String::fromUTF8("Baixa");
        if (key == "media") return juce::String::fromUTF8("Média");
        if (key == "alta") return juce::String::fromUTF8("Alta");
        if (key == "aberto") return juce::String::fromUTF8("Aberto");
        if (key == "em_progresso") return juce::String::fromUTF8("Em Progresso");
        if (key == "resolvido") return juce::String::fromUTF8("Resolvido");
        if (key == "presente") return juce::String::fromUTF8("Presente");
        if (key == "ausente") return juce::String::fromUTF8("Ausente");
        if (key == "alterado") return juce::String::fromUTF8("Alterado");
        if (key == "corrompido") return juce::String::fromUTF8("Corrompido");
        if (key == "nao_baixado") return juce::String::fromUTF8("Não Baixado");
        return "[" + chave + "]";
    }

    const auto& strings = getEnglishStrings();
    auto it = strings.find(key);
    if (it == strings.end()) {
        if (key == "novo") return "New";
        if (key == "em_analise") return "In Analysis";
        if (key == "catalogado") return "Cataloged";
        if (key == "revisado") return "Reviewed";
        if (key == "aprovado") return "Approved";
        if (key == "publicado") return "Published";
        if (key == "arquivado") return "Archived";
        if (key == "baixa") return "Low";
        if (key == "media") return "Medium";
        if (key == "alta") return "High";
        if (key == "aberto") return "Open";
        if (key == "em_progresso") return "In Progress";
        if (key == "resolvido") return "Resolved";
        if (key == "presente") return "Present";
        if (key == "ausente") return "Missing";
        if (key == "alterado") return "Changed";
        if (key == "corrompido") return "Corrupted";
        if (key == "nao_baixado") return "Not Downloaded";
        return "[" + chave + "]";
    }
    return juce::String(juce::CharPointer_UTF8(it->second.c_str()));
}

juce::String tComFallback(const juce::String& chave, const juce::String& textoOriginal) {
    const std::string key = chave.toStdString();
    if (g_activeLocale == "pt_BR") {
        const auto& ptStrings = getPortugueseStrings();
        auto it = ptStrings.find(key);
        if (it != ptStrings.end()) {
            return juce::String(juce::CharPointer_UTF8(it->second.c_str()));
        }
        if (textoOriginal.isNotEmpty()) {
            return textoOriginal;
        }
        const auto& enStrings = getEnglishStrings();
        auto itEn = enStrings.find(key);
        if (itEn != enStrings.end()) {
            return juce::String(juce::CharPointer_UTF8(itEn->second.c_str()));
        }
        return textoOriginal;
    }

    const auto& strings = getEnglishStrings();
    auto it = strings.find(key);
    if (it == strings.end()) return textoOriginal;
    return juce::String(juce::CharPointer_UTF8(it->second.c_str()));
}

bool existe(const juce::String& chave) {
    const std::string key = chave.toStdString();
    if (g_activeLocale == "pt_BR") {
        const auto& ptStrings = getPortugueseStrings();
        if (ptStrings.find(key) != ptStrings.end()) return true;
    }
    const auto& strings = getEnglishStrings();
    return strings.find(key) != strings.end();
}

} // namespace matriz::i18n
