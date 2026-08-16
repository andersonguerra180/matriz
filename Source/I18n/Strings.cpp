#include "Strings.h"
#include "../Ui/Strings.h"

namespace matriz::i18n {

void carregar(const juce::String& /*locale*/) {
    // No-op: English is always the default and only language
}

juce::String t(const juce::String& chave) {
    const auto& strings = getEnglishStrings();
    auto it = strings.find(chave.toStdString());
    if (it == strings.end()) {
        std::string s = chave.toStdString();
        if (s == "novo") return "New";
        if (s == "em_analise") return "In Analysis";
        if (s == "catalogado") return "Cataloged";
        if (s == "revisado") return "Reviewed";
        if (s == "aprovado") return "Approved";
        if (s == "publicado") return "Published";
        if (s == "arquivado") return "Archived";
        if (s == "baixa") return "Low";
        if (s == "media") return "Medium";
        if (s == "alta") return "High";
        if (s == "aberto") return "Open";
        if (s == "em_progresso") return "In Progress";
        if (s == "resolvido") return "Resolved";
        if (s == "presente") return "Present";
        if (s == "ausente") return "Missing";
        if (s == "alterado") return "Changed";
        if (s == "corrompido") return "Corrupted";
        if (s == "nao_baixado") return "Not Downloaded";
        return "[" + chave + "]";
    }
    return juce::String(juce::CharPointer_UTF8(it->second.c_str()));
}

juce::String tComFallback(const juce::String& chave, const juce::String& textoOriginal) {
    const auto& strings = getEnglishStrings();
    auto it = strings.find(chave.toStdString());
    if (it == strings.end()) return textoOriginal;
    return juce::String(juce::CharPointer_UTF8(it->second.c_str()));
}

bool existe(const juce::String& chave) {
    const auto& strings = getEnglishStrings();
    return strings.find(chave.toStdString()) != strings.end();
}

} // namespace matriz::i18n
