#pragma once

#include <JuceHeader.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ProjetoAberto.h"

// Antes de ingerir, o operador escolhe o tipo de mídia do lote — nunca
// adivinhado pela extensão do arquivo (§6: cada tipo de mídia tem uma ficha
// própria; assumir errado obriga o operador a responder campos que não
// existem pra aquele formato, ex.: espessura de fita pra um CD).

namespace matriz::ui {

struct TipoMidiaOpcao {
    std::string id;
    juce::String rotulo;
};

// Lê o rótulo real de cada definição em fichas/*.yaml (via ProjetoAberto,
// que já cacheia FichaDefinition) — nunca um rótulo hard-coded que possa
// divergir do YAML.
std::vector<TipoMidiaOpcao> listarTiposMidiaDisponiveis(ProjetoAberto& projeto);

// nullopt se o operador cancelou.
void mostrarDialogoSelecionarTipoMidia(std::vector<TipoMidiaOpcao> opcoes, int quantidadeArquivos,
                                        std::function<void(std::optional<std::string> tipoEscolhido)> aoConcluir);

} // namespace matriz::ui
