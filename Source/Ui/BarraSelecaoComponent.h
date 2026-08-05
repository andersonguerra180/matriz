#pragma once

#include <JuceHeader.h>

#include <functional>

// Barra de seleção — aparece sobre o rodapé da grade assim que existe
// algum arquivo marcado, e some quando a seleção esvazia.
//
// Existe por um motivo específico: a edição em lote JÁ funcionava, mas
// morava dentro do painel lateral de detalhes, que nasce fechado. Ou seja,
// o recurso mais importante pra quem tem um HD cheio de arquivo — mudar
// mil coisas de uma vez — era invisível até o operador descobrir sozinho
// que precisava (1) selecionar vários, (2) abrir um painel que não dava
// sinal de conter isso. Agora a própria seleção anuncia o que dá pra fazer
// com ela.

namespace matriz::ui {

class BarraSelecaoComponent : public juce::Component {
public:
    BarraSelecaoComponent();

    std::function<void()> aoEditarEmLote;
    std::function<void()> aoSelecionarTodos;
    std::function<void()> aoLimparSelecao;

    // 0 esconde a barra; 1+ mostra com a contagem no plural correto.
    void definirQuantidade(int quantidade);
    int quantidade() const { return quantidade_; }

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kAltura = 44;

private:
    int quantidade_ = 0;
    juce::String texto_;
    std::unique_ptr<juce::TextButton> botaoEditarLote_, botaoSelecionarTodos_, botaoLimpar_;
};

} // namespace matriz::ui
