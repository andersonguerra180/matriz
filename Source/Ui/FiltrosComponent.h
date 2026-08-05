#pragma once

#include <JuceHeader.h>

#include "MosaicoComponent.h"
#include "ProjetoAberto.h"

// Filtros (chips) + busca + coleções inteligentes (Acréscimos §10) — fica
// embaixo da árvore no painel esquerdo (§9.1: "Árvore / Filtros / Busca"
// empilhados na mesma coluna). Opera diretamente sobre um MosaicoComponent
// (mesmo padrão de ArvoreComponent segurando ProjetoAberto&: quem lê/edita
// o estado de filtro é este Component, não um emaranhado de callbacks).
//
// Simplificação desta etapa, deliberada: as contagens de cada chip são
// GLOBAIS (contagensPorTipoMidia/Estado/Extensao, recalculadas só em
// recarregar()), não recalculadas dinamicamente em função dos OUTROS
// filtros já ativos (facetamento cruzado) — um chip mostra "quantos itens
// têm esse valor no acervo inteiro", não "quantos apareceriam se este chip
// também fosse marcado". Documentado, não escondido.

namespace matriz::ui {

class FiltrosComponent : public juce::Component {
public:
    FiltrosComponent(ProjetoAberto& projeto, MosaicoComponent& mosaico);

    // Reconsulta contagens e coleções salvas, e redesenha. Chamado ao abrir
    // o projeto e depois de qualquer ingest/edição que possa ter mudado o
    // quadro (mesmo padrão de MosaicoComponent::recarregar()).
    void recarregar();

    // A caixa de busca mora na barra de ferramentas superior, não mais
    // aqui: duas caixas de busca na mesma tela (uma no topo, outra na
    // coluna esquerda) deixavam ambíguo qual delas manda. Este callback é
    // como este painel pede que o campo lá de cima acompanhe quando ELE
    // mexe na busca — limpar filtros, ou aplicar uma coleção salva, que
    // carrega junto o texto de busca gravado nela.
    std::function<void(const juce::String&)> aoSincronizarBusca;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    enum class TipoLinha {
        CabecalhoSecao,
        ChipTipoMidia,
        ChipEstado,
        ChipExtensao,
        ChipOrigem,     // Digital/Analógico (item 4.2)
        ChipAgrupamento, // alterna o eixo de agrupamento da grade (item 4.3)
        Limpar,
        Colecao,
        SalvarColecao
    };
    struct Linha {
        TipoLinha tipo;
        juce::String rotulo;
        juce::String chave;  // valor do chip (tipo_midia/estado/extensão) ou id da coleção
        int contagem = -1;   // -1 = não mostra contagem (cabeçalhos, ações)
        bool ativo = false;  // chip atualmente selecionado
    };

    void reconstruirLinhas();
    juce::Rectangle<int> boundsDaLinha(int indice) const;
    int indiceLinhaNaPosicao(int y) const;
    void abrirMenuContextoColecao(int indiceLinha);
    void salvarColecaoAtual();

    ProjetoAberto& projeto_;
    MosaicoComponent& mosaico_;
    std::vector<Linha> linhas_;

    static constexpr int kAlturaCabecalhoSecao = 22;
    static constexpr int kAlturaLinha = 24;
};

} // namespace matriz::ui
