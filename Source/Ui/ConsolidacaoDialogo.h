#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "ProjetoAberto.h"

// Diálogo de consolidação (item 10, §11.7): escolhe destino, mostra a
// prévia completa (nome original -> nome final, conflito em vermelho),
// avisos (não organizados, conflitos, espaço) e o botão "Consolidate" só
// habilita quando o plano pode ser executado (§11.6 — "conflito de nome
// bloqueia a consolidação até ser resolvido").

namespace matriz::ui {

// Devolve o AlertWindow construído — produção ignora o retorno, o harness
// de UI (Source/Ui/UiSelfTest.cpp) usa pra tirar um snapshot e verificar
// invariantes, mesmo padrão de mostrarDialogoNovoProjeto/
// mostrarDialogoSelecionarTipoMidia.
std::shared_ptr<juce::AlertWindow> mostrarDialogoConsolidacao(ProjetoAberto& projeto, std::function<void()> aoFechar);

} // namespace matriz::ui
