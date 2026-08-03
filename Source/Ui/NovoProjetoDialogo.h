#pragma once

#include <JuceHeader.h>

#include <functional>
#include <optional>

#include "../Model/Project.h"

// Diálogo de criação de projeto (B.1.1). Assíncrono (nunca bloqueia a
// thread de mensagens) — chama `aoConcluir` com nullopt se cancelado.
//
// O modo (Archive/Catalog) não é mais escolhido aqui — é a primeira decisão
// do operador, na tela inicial (dois cartões, Parte 1 da correção de
// fluxo), antes deste diálogo sequer abrir. Aqui só pedimos o que é comum
// aos dois modos: nome, pasta, e os campos de identificação do projeto.

namespace matriz::ui {

struct NovoProjetoResultado {
    juce::File pasta;
    matriz::model::NovoProjetoParams params;
};

void mostrarDialogoNovoProjeto(matriz::model::Modo modo,
                                std::function<void(std::optional<NovoProjetoResultado>)> aoConcluir);

} // namespace matriz::ui
