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

class FichaConteudo; // implementado em FichaPanelComponent.cpp — contém todos os widgets dinâmicos

class FichaPanelComponent : public juce::Component {
public:
    explicit FichaPanelComponent(ProjetoAberto& projeto);
    ~FichaPanelComponent() override;

    // "" = nenhum item selecionado (estado vazio).
    void mostrarItem(const std::string& itemId);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ProjetoAberto& projeto_;
    std::string itemIdAtual_;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<FichaConteudo> conteudo_;
};

} // namespace matriz::ui
