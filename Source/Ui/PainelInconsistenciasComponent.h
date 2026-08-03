#pragma once

#include <JuceHeader.h>

#include "../Ingest/PainelInconsistencias.h"
#include "ProjetoAberto.h"

// Painel de inconsistências (§7.4) com posição fixa na tela principal no
// modo Catalog (Parte 1 da correção de fluxo, §1.3) — "o coração do modo
// Catalog: faixa sem ISRC, lançamento sem capa, master em lossy... nunca
// escondido em menu". No Archive não tem posição fixa nesta etapa; o
// operador não precisa dele com a mesma urgência (§1.2).
//
// Só relata — nunca corrige sozinho, mesma regra do motor de ingestão que
// consome (Source/Ingest/PainelInconsistencias.h).

namespace matriz::ui {

class PainelInconsistenciasComponent : public juce::Component {
public:
    explicit PainelInconsistenciasComponent(ProjetoAberto& projeto);

    // Roda as checagens de novo (ficha + disco) e redesenha. Chamado ao
    // abrir o painel e depois de qualquer ingest/edição de ficha que possa
    // ter mudado o quadro.
    void recarregar();

    int totalInconsistencias() const { return static_cast<int>(itens_.size()); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ProjetoAberto& projeto_;
    std::vector<matriz::ingest::Inconsistencia> itens_;

    static constexpr int kAlturaCabecalho = 30;
    static constexpr int kAlturaLinha = 40;
};

} // namespace matriz::ui
