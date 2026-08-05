#pragma once

#include <JuceHeader.h>

#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

#include "../Catalogo/CatalogoProxies.h"

// Visualizador do catálogo de proxies (item 11).
//
// É a metade que justifica gerar o catálogo: abre uma pasta de backup numa
// máquina que nunca viu o projeto original, mostra o acervo inteiro em
// miniatura, e diz onde cada arquivo está — mesmo com o material
// desconectado. Clicar num item com a fonte montada abre o arquivo real;
// com a fonte ausente, mostra onde procurar.
//
// Somente leitura por construção: não existe caminho daqui pro banco do
// projeto (que pode nem estar nesta máquina). Nenhuma ação de edição.

namespace matriz::ui {

class CatalogoComponent : public juce::Component {
public:
    CatalogoComponent();

    // `pasta` pode ser a raiz do backup ou a pasta do catálogo dentro dela.
    // Devolve false se não houver catálogo ali.
    bool abrir(const juce::File& pasta);

    void definirBusca(const juce::String& texto);

    int totalEntradas() const { return static_cast<int>(entradas_.size()); }
    int totalVisiveis() const { return static_cast<int>(visiveis_.size()); }

    // Introspecção pra teste: a linha de status da entrada visível `indice`
    // — é o texto que responde "onde está este arquivo?".
    juce::String descricaoDaEntradaParaTeste(int indice) const;
    bool fonteConectadaParaTeste(int indice) const;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    static constexpr int kAlturaLinha = 64;

private:
    void aplicarBusca();
    int indiceNaPosicao(int y) const;
    const juce::Image* miniatura(int indiceVisivel);

    juce::File pastaCatalogo_;
    std::vector<matriz::catalogo::EntradaCatalogo> entradas_;
    std::vector<int> visiveis_; // índices em entradas_
    juce::String busca_;
    int hover_ = -1;

    // Cache de miniatura por índice de entrada. Catálogo de acervo grande
    // tem dezenas de milhares de linhas; carregar tudo de uma vez estouraria
    // a memória, então só o que aparece na tela é lido — mesma disciplina do
    // mosaico do projeto.
    std::unordered_map<int, juce::Image> cache_;
    std::deque<int> ordemCache_;
    static constexpr size_t kCapacidadeCache = 300;
};

} // namespace matriz::ui
