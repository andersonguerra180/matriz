#pragma once

#include <JuceHeader.h>

#include <functional>
#include <optional>

#include "../Model/Project.h"

// Diálogo de criação de projeto (B.1.1). Assíncrono (nunca bloqueia a
// thread de mensagens) — chama `aoConcluir` com nullopt se cancelado.

namespace matriz::ui {

struct NovoProjetoResultado {
    juce::File pasta;
    matriz::model::NovoProjetoParams params;
};

void mostrarDialogoNovoProjeto(std::function<void(std::optional<NovoProjetoResultado>)> aoConcluir);

} // namespace matriz::ui
