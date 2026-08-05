#pragma once

#include <JuceHeader.h>

#include <map>
#include <string>
#include <vector>

// Máscara de nomenclatura por pasta (item 10, §11.6 do novo spec). Tokens
// suportados: {codigo} {titulo} {tipo} {original} {acervo} {pasta}
// {seq} (com padding opcional, {seq:03}) e qualquer campo de ficha pelo
// próprio id (ex.: {artista_principal}, {ano}) — resolvido contra
// ContextoMascara.camposFicha, preenchido por quem monta o contexto a
// partir de item_campo. Token que não bate com nada vira "" e é reportado,
// nunca lança e nunca trava a prévia (§11.6 — "prévia sempre visível").

namespace matriz::consolidacao {

struct ContextoMascara {
    std::string codigoAcervo;
    std::string titulo;
    std::string tipoMidia;
    std::string nomeOriginalSemExtensao;
    std::string nomeAcervo;
    std::string nomePasta;
    int seq = 0;
    std::map<std::string, std::string> camposFicha; // campo_id -> valor (nível raiz)
};

// Resolve a máscara contra o contexto e sanitiza caracteres inválidos pra
// nome de arquivo (/, \, :, etc viram "_"). Tokens não encontrados são
// acrescentados em `tokensNaoResolvidos` (se não-nulo), na ordem em que
// aparecem — quem chama decide se isso vira aviso na prévia.
std::string resolverMascara(const juce::String& mascara, const ContextoMascara& ctx,
                             std::vector<std::string>* tokensNaoResolvidos = nullptr);

} // namespace matriz::consolidacao
