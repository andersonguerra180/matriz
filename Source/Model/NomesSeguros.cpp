#include "NomesSeguros.h"

namespace matriz::nomes_seguros {

bool caractereInvalido(juce::juce_wchar c) {
    if (c < 32 || c == 127) return true;
    switch (c) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '/':
        case '\\':
        case '|':
        case '?':
        case '*':
            return true;
        default:
            return false;
    }
}

bool ehNomeReservado(const juce::String& nome) {
    juce::String base = nome.trim();
    int dotIndex = base.indexOfChar('.');
    if (dotIndex != -1) {
        base = base.substring(0, dotIndex);
    }
    base = base.toUpperCase();

    static const char* const kReservados[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };

    for (const char* res : kReservados) {
        if (base == res) return true;
    }
    return false;
}

juce::String sanitizarComponente(const juce::String& componenteOriginal) {
    if (componenteOriginal.isEmpty()) return "unnamed";

    juce::String resultado;
    for (int i = 0; i < componenteOriginal.length(); ++i) {
        juce::juce_wchar c = componenteOriginal[i];
        if (caractereInvalido(c)) {
            resultado << juce::String::charToString(kCaractereSubstituto);
        } else {
            resultado << juce::String::charToString(c);
        }
    }

    // Remove espaços e pontos no final
    while (resultado.endsWithChar(' ') || resultado.endsWithChar('.')) {
        resultado = resultado.dropLastCharacters(1);
    }

    if (resultado.isEmpty()) {
        resultado = "unnamed";
    }

    // Se for nome reservado do Windows (ex: CON, AUX, CON.txt), protege o nome base
    if (ehNomeReservado(resultado)) {
        int dotIdx = resultado.indexOfChar('.');
        if (dotIdx != -1) {
            juce::String base = resultado.substring(0, dotIdx);
            juce::String ext = resultado.substring(dotIdx);
            resultado = "_" + base + "_" + ext;
        } else {
            resultado = "_" + resultado + "_";
        }
    }

    // Limitar o tamanho do componente a 255 bytes UTF-8
    juce::MemoryBlock mb;
    mb.append(resultado.toRawUTF8(), (size_t) resultado.getNumBytesAsUTF8());
    if (mb.getSize() > 255) {
        // Encurta caractere a caractere mantendo UTF-8 válido
        juce::String truncada = resultado;
        while (truncada.getNumBytesAsUTF8() > 255 && truncada.isNotEmpty()) {
            truncada = truncada.dropLastCharacters(1);
        }
        while (truncada.endsWithChar(' ') || truncada.endsWithChar('.')) {
            truncada = truncada.dropLastCharacters(1);
        }
        resultado = truncada.isEmpty() ? "unnamed" : truncada;
    }

    return resultado;
}

std::string sanitizarComponente(const std::string& componenteOriginal) {
    return sanitizarComponente(juce::String::fromUTF8(componenteOriginal.c_str())).toStdString();
}

juce::String sanitizarCaminhoRelativo(const juce::String& caminhoRelativo) {
    juce::String norm = caminhoRelativo.replaceCharacter('\\', '/');
    juce::StringArray partes;
    partes.addTokens(norm, "/", "");
    
    juce::StringArray partesSanitizadas;
    for (int i = 0; i < partes.size(); ++i) {
        juce::String p = partes[i].trim();
        if (p.isEmpty() || p == ".") continue;
        if (p == "..") {
            // Não permitir escape relativo por segurança
            continue;
        }
        partesSanitizadas.add(sanitizarComponente(p));
    }
    return partesSanitizadas.joinIntoString("/");
}

std::string sanitizarCaminhoRelativo(const std::string& caminhoRelativo) {
    return sanitizarCaminhoRelativo(juce::String::fromUTF8(caminhoRelativo.c_str())).toStdString();
}

} // namespace matriz::nomes_seguros
