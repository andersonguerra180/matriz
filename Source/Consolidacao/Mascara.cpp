#include "Mascara.h"

namespace matriz::consolidacao {

namespace {

juce::String valorToken(const juce::String& nomeToken, const juce::String& especificador, const ContextoMascara& ctx,
                         bool& encontrado) {
    encontrado = true;
    if (nomeToken == "codigo") return ctx.codigoAcervo;
    if (nomeToken == "titulo") return ctx.titulo;
    if (nomeToken == "tipo") return ctx.tipoMidia;
    if (nomeToken == "original") return ctx.nomeOriginalSemExtensao;
    if (nomeToken == "acervo") return ctx.nomeAcervo;
    if (nomeToken == "pasta") return ctx.nomePasta;
    if (nomeToken == "seq") {
        int largura = especificador.isNotEmpty() ? especificador.getIntValue() : 0;
        return juce::String(ctx.seq).paddedLeft('0', largura);
    }
    auto it = ctx.camposFicha.find(nomeToken.toStdString());
    if (it != ctx.camposFicha.end()) return juce::String(it->second);
    encontrado = false;
    return {};
}

juce::String sanitizarNomeArquivo(const juce::String& s) {
    juce::String out = s;
    static const juce::String invalidos = "/\\:*?\"<>|";
    for (auto c : invalidos) out = out.replaceCharacter(c, '_');
    return out;
}

} // namespace

std::string resolverMascara(const juce::String& mascara, const ContextoMascara& ctx,
                             std::vector<std::string>* tokensNaoResolvidos) {
    juce::String out;
    int i = 0;
    while (i < mascara.length()) {
        if (mascara[i] == '{') {
            int fim = mascara.indexOfChar(i, '}');
            if (fim < 0) {
                out += mascara.substring(i); // "{" sem fechar — copia o resto literal, nunca lança
                break;
            }
            juce::String dentro = mascara.substring(i + 1, fim);
            juce::String nomeToken = dentro.upToFirstOccurrenceOf(":", false, false).trim();
            juce::String especificador = dentro.fromFirstOccurrenceOf(":", false, false).trim();
            bool encontrado = false;
            juce::String valor = valorToken(nomeToken, especificador, ctx, encontrado);
            if (!encontrado && tokensNaoResolvidos) tokensNaoResolvidos->push_back(nomeToken.toStdString());
            out += valor;
            i = fim + 1;
        } else {
            out += juce::String::charToString(mascara[i]);
            ++i;
        }
    }
    return sanitizarNomeArquivo(out).toStdString();
}

} // namespace matriz::consolidacao
