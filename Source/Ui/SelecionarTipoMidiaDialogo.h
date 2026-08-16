#pragma once

#include <JuceHeader.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "OverlayComponent.h"
#include "ProjetoAberto.h"

// Antes de ingerir, o operador escolhe o tipo de mídia do lote — nunca
// adivinhado pela extensão do arquivo (§6: cada tipo de mídia tem uma ficha
// própria; assumir errado obriga o operador a responder campos que não
// existem pra aquele formato, ex.: espessura de fita pra um CD).
//
// Este era o diálogo do crash reproduzível em juce::AlertWindow::paint() ->
// Component::getName(). Não usa mais AlertWindow: vira um PainelOverlay
// interno da janela (§3), sem peer nativo e sem loop modal do sistema.

namespace matriz::ui {

struct TipoMidiaOpcao {
    std::string id;
    juce::String rotulo;
    std::string categoria;
};

// Lê o rótulo real de cada definição em fichas/*.yaml (via ProjetoAberto,
// que já cacheia FichaDefinition) — nunca um rótulo hard-coded que possa
// divergir do YAML.
std::vector<TipoMidiaOpcao> listarTiposMidiaDisponiveis(ProjetoAberto& projeto);
std::vector<TipoMidiaOpcao> listarTiposMidiaDisponiveisParaExtensoes(ProjetoAberto& projeto, const std::vector<std::string>& extensoes);

// Config pronta pro overlay, exposta à parte pra o harness poder montar e
// descartar o painel 500 vezes (--selftest-modal-loop) sem precisar de um
// projeto aberto por trás.
PainelOverlay::Config configSelecionarTipoMidia(const std::vector<TipoMidiaOpcao>& opcoes,
                                                 int quantidadeArquivos);

// Mostra o seletor em `overlay`. `aoConcluir` recebe nullopt se o operador
// cancelou. Sempre chamado na message thread, com o overlay já fechado.
void mostrarSelecionarTipoMidia(PainelOverlay& overlay, std::vector<TipoMidiaOpcao> opcoes,
                                 int quantidadeArquivos,
                                 std::function<void(std::optional<std::string>)> aoConcluir);

} // namespace matriz::ui
