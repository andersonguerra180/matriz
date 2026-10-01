#pragma once

#include <JuceHeader.h>
#include <string>

namespace matriz::nomes_seguros {

/**
 * Caractere determinístico padrão de substituição para caracteres inválidos.
 */
constexpr juce::juce_wchar kCaractereSubstituto = '_';

/**
 * Verifica se um caractere é proibido em nomes de arquivos/pastas em qualquer
 * dos sistemas operacionais suportados (Windows e macOS).
 *
 * Proibidos: < > : " / \ | ? * e caracteres de controle ASCII (< 32).
 */
bool caractereInvalido(juce::juce_wchar c);

/**
 * Verifica se uma string de nome de arquivo ou pasta é um nome de dispositivo
 * reservado do Windows (ex: CON, PRN, AUX, NUL, COM1..COM9, LPT1..LPT9)
 * com ou sem extensão.
 */
bool ehNomeReservado(const juce::String& nome);

/**
 * Sanitiza um único componente de caminho (nome de arquivo ou pasta) de modo determinístico,
 * tornando-o seguro e válido em ambos macOS e Windows:
 * 1. Substitui caracteres inválidos (< > : " / \ | ? * e controles) por '_'.
 * 2. Remove espaços e pontos no final do componente.
 * 3. Se for um nome reservado (ex: CON, PRN, NUL), adiciona um prefixo/sufixo seguro (ex: _CON_).
 * 4. Limita o comprimento do componente a 255 bytes em UTF-8.
 * 5. Se o resultado for vazio, devolve "unnamed".
 */
juce::String sanitizarComponente(const juce::String& componenteOriginal);
std::string sanitizarComponente(const std::string& componenteOriginal);
inline juce::String sanitizarComponente(const char* componenteOriginal) {
    return sanitizarComponente(juce::String(componenteOriginal));
}

/**
 * Sanitiza um caminho relativo completo (com múltiplos componentes separados por '/').
 */
juce::String sanitizarCaminhoRelativo(const juce::String& caminhoRelativo);
std::string sanitizarCaminhoRelativo(const std::string& caminhoRelativo);
inline juce::String sanitizarCaminhoRelativo(const char* caminhoRelativo) {
    return sanitizarCaminhoRelativo(juce::String(caminhoRelativo));
}

} // namespace matriz::nomes_seguros
