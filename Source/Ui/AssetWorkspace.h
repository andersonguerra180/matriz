#pragma once

#include <JuceHeader.h>

#include <functional>
#include <optional>

#include "ProjetoAberto.h"

// Workspace polimórfico (§7) — a área central troca de ferramenta conforme
// o tipo do asset selecionado, em vez de uma tela genérica que serve mal
// pra tudo.
//
// `arquivo` é opcional de propósito: com o Vault desconectado (I3) o
// workspace ainda carrega — forma de onda, marcadores e ficha vêm do cache
// no registro. O que fica desabilitado é só o transporte, com o aviso de
// onde o Vault está.

namespace matriz::ui {

class AssetWorkspace : public juce::Component {
public:
    ~AssetWorkspace() override = default;

    virtual void carregarAsset(const ItemResumo& snap, std::optional<juce::File> arquivo,
                               std::function<void()> aoConcluir) = 0;
    virtual void descarregar() = 0;
};

} // namespace matriz::ui
