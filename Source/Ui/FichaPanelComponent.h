#pragma once

#include <JuceHeader.h>

#include "ProjetoAberto.h"

// Ficha lateral (§11.1, §6.2, B.1.3/B.3). UMA tela genérica que consome
// FichaDefinition e monta os widgets em runtime — nunca um arquivo por tipo
// de mídia. Suporta: grupos, todos os tipos de campo, obrigatoriedade,
// validação nomeada, visivel_se reagindo em tempo real, herança do
// projeto, preenchido_por leitura técnica, sugerido_por modelo (P3 —
// visualmente distinto, confirmação explícita), afeta (efeito colateral),
// alerta_se_true, níveis aninhados (release → faixa) e arquivos esperados.

namespace matriz::ui {

class FichaConteudo;     // implementado em FichaPanelComponent.cpp — ficha de UM item
class FichaLoteConteudo; // implementado em FichaPanelComponent.cpp — modo lote (§12.2, item 8)
class MetadadosOriginaisComponent;

class FichaPanelComponent : public juce::Component {
public:
    explicit FichaPanelComponent(ProjetoAberto& projeto);
    ~FichaPanelComponent() override;

    // "" = nenhum item selecionado (estado vazio).
    void mostrarItem(const std::string& itemId);

    // Seleção múltipla (§12.2 — "a ficha entra em modo lote"). 0 itens =
    // estado vazio, 1 item cai de volta em mostrarItem() (ficha individual
    // normal), 2+ entra em modo lote de verdade.
    void mostrarSelecao(const std::vector<std::string>& itemIds);

    // Disparado depois de aplicar ficha em lote ou reclassificar a seleção
    // (tipo de mídia muda contagens/agrupamento em todo lugar — grade,
    // árvore, chips de filtro). MainComponent usa isto pra recarregar tudo.
    std::function<void()> aoAplicarEmLote;

    // Introspecção pra teste (Parte 2 da correção crítica — perda de
    // dado): acesso ao editor de um campo específico pra simular digitação
    // sem depender de foco/blur real. nullptr se o campo não está visível
    // na ficha atual.
    juce::Component* editorDoCampoParaTeste(const std::string& nivel, int nivelIndice, const std::string& campoId);

    // Idem, pro modo lote (item 8, §12.2) — nullptr se não estiver em modo
    // lote ou o campo não existir nesta tela.
    juce::Component* editorDoCampoLoteParaTeste(const std::string& campoId);
    juce::TextButton* botaoTipoMidiaLoteParaTeste(const std::string& tipoId);
    juce::TextButton* botaoAplicarLoteParaTeste();
    juce::TextButton* botaoDesfazerLoteParaTeste();

    // Idem, pro seletor de tipo na ficha INDIVIDUAL de um item ainda não
    // classificado (crash fix — ver FichaConteudo::construirSeletorTipoMidia).
    juce::TextButton* botaoTipoMidiaIndividualParaTeste(const std::string& tipoId);

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kAlturaCabecalhoFicha = 22;

private:
    // Item 4 das correções de operação: o painel tem DUAS seções separadas
    // e rotuladas — o que veio dentro do arquivo (imutável) e o que o
    // operador preenche (a ficha). Antes era tudo campo editável junto, e
    // não dava pra saber o que era leitura de máquina e o que era decisão
    // humana.
    std::unique_ptr<MetadadosOriginaisComponent> metadadosOriginais_;

    ProjetoAberto& projeto_;
    std::string itemIdAtual_;
    bool modoLote_ = false;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<FichaConteudo> conteudo_;
    std::unique_ptr<FichaLoteConteudo> conteudoLote_;
};

} // namespace matriz::ui
