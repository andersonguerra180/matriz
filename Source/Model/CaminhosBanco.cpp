#include "CaminhosBanco.h"

namespace matriz::caminhos {

juce::String paraBanco(const juce::String& caminhoRelativo) {
    if (caminhoRelativo.isEmpty()) return {};
    
    juce::String r = caminhoRelativo.replaceCharacter('\\', '/').trim();
    
    // Remove barras iniciais e finais redundantes
    while (r.startsWithChar('/')) r = r.substring(1);
    while (r.endsWithChar('/')) r = r.dropLastCharacters(1);
    
    // Remove ocorrências de "//" duplicadas
    while (r.contains("//")) {
        r = r.replace("//", "/");
    }
    
    return r;
}

std::string paraBanco(const std::string& caminhoRelativo) {
    if (caminhoRelativo.empty()) return {};
    return paraBanco(juce::String::fromUTF8(caminhoRelativo.c_str())).toStdString();
}

juce::String doBanco(const juce::String& caminhoRelativo) {
    return paraBanco(caminhoRelativo);
}

std::string doBanco(const std::string& caminhoRelativo) {
    return paraBanco(caminhoRelativo);
}

std::string relativoParaBanco(const juce::File& arquivo, const juce::File& pastaBase) {
    juce::String rel = arquivo.getRelativePathFrom(pastaBase);
    return paraBanco(rel).toStdString();
}

juce::File resolverDoBanco(const juce::File& pastaBase, const std::string& caminhoRelativoDoBanco) {
    juce::String rel = doBanco(juce::String::fromUTF8(caminhoRelativoDoBanco.c_str()));
    return pastaBase.getChildFile(rel);
}

} // namespace matriz::caminhos
