#include "FichaI18n.h"

#include "../I18n/Strings.h"

namespace matriz::ficha {

juce::String rotuloTipo(const std::string& tipo, const std::string& rotuloOriginal) {
    return matriz::i18n::tComFallback("ficha_tipos." + juce::String(tipo), rotuloOriginal);
}

juce::String rotuloGrupo(const std::string& tipo, const std::string& chaveGrupo, const std::string& rotuloOriginal) {
    return matriz::i18n::tComFallback("ficha_grupos." + juce::String(tipo) + "." + juce::String(chaveGrupo),
                                       rotuloOriginal);
}

juce::String rotuloCampo(const std::string& tipo, const Campo& campo) {
    return matriz::i18n::tComFallback("ficha_campos." + juce::String(tipo) + "." + juce::String(campo.id),
                                       campo.rotulo);
}

juce::String rotuloOpcao(const std::string& tipo, const Campo& campo, int indiceOpcao, const std::string& valorBruto) {
    return matriz::i18n::tComFallback(
        "ficha_opcoes." + juce::String(tipo) + "." + juce::String(campo.id) + "." + juce::String(indiceOpcao),
        valorBruto);
}

juce::String rotuloAlerta(const std::string& tipo, const Campo& campo) {
    return matriz::i18n::tComFallback("ficha_alertas." + juce::String(tipo) + "." + juce::String(campo.id),
                                       campo.alertaSeTrue);
}

} // namespace matriz::ficha
