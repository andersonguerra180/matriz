#pragma once

#include <JuceHeader.h>
#include <string>

namespace matriz::caminhos {

/**
 * Normaliza caminhos relativos para armazenamento consistente no banco SQLite.
 *
 * REGRA INVIOLÁVEL: Todo caminho relativo gravado no banco usa "/" sempre,
 * em ambos os sistemas operacionais (macOS e Windows).
 *
 * Converte barras invertidas '\' em '/', remove barras duplicadas,
 * remove barras no início/fim e assegura codificação UTF-8 consistente.
 */
juce::String paraBanco(const juce::String& caminhoRelativo);
std::string paraBanco(const std::string& caminhoRelativo);
inline juce::String paraBanco(const char* caminhoRelativo) {
    return paraBanco(juce::String(caminhoRelativo));
}

/**
 * Lê um caminho relativo do banco de dados para uso na aplicação.
 * Aceita tanto '/' quanto '\' (por tolerância a bancos legados) e
 * devolve uma representação limpa.
 */
juce::String doBanco(const juce::String& caminhoRelativo);
std::string doBanco(const std::string& caminhoRelativo);
inline juce::String doBanco(const char* caminhoRelativo) {
    return doBanco(juce::String(caminhoRelativo));
}

/**
 * Obtém o caminho relativo de um arquivo em relação a uma pasta base,
 * garantindo formatação normalizada com "/" para persistência no banco.
 */
std::string relativoParaBanco(const juce::File& arquivo, const juce::File& pastaBase);

/**
 * Resolve um caminho relativo do banco a partir de uma pasta base local.
 */
juce::File resolverDoBanco(const juce::File& pastaBase, const std::string& caminhoRelativoDoBanco);

} // namespace matriz::caminhos
