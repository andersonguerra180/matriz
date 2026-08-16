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

    // Roda as checagens de novo (ficha + disco) e redesenha.
    //
    // A checagem de disco RELÊ CADA ARQUIVO e recalcula o SHA-256
    // (verificarArquivosNoDisco). Num acervo de milhares de itens isso é
    // minutos — na message thread, era a janela congelada. Vai pra
    // background (I1); a tela recebe só a lista pronta.
    void recarregar();
    // Mesma coisa, na mesma pilha de chamada — só pro harness headless.
    void recarregarSincrono();
    bool verificacaoEmAndamentoParaTeste() const { return pool_.getNumJobs() > 0; }

    int totalInconsistencias() const { return static_cast<int>(itens_.size()); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static std::vector<matriz::ingest::Inconsistencia> coletar(ProjetoAberto& projeto);
    void aplicar(std::vector<matriz::ingest::Inconsistencia> itens);

    ProjetoAberto& projeto_;
    std::vector<matriz::ingest::Inconsistencia> itens_;

    juce::ThreadPool pool_{juce::ThreadPoolOptions{}.withThreadName("MatrizVerificacao")
                           .withNumberOfThreads(1)
                           .withDesiredThreadPriority(juce::Thread::Priority::low)};
    int geracao_ = 0;

    static constexpr int kAlturaCabecalho = 30;
    static constexpr int kAlturaLinha = 40;
};

} // namespace matriz::ui
