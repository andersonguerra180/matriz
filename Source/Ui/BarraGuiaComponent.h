#pragma once

#include <JuceHeader.h>

#include <functional>

// Faixa de orientação — uma frase dizendo o que fazer AGORA.
//
// Resposta direta a "o software não se explica": em vez de um manual ou um
// tour de boas-vindas (que o operador fecha e nunca mais vê), o app mantém
// permanentemente à vista uma linha que muda com o estado do projeto e
// sempre aponta o próximo passo concreto. Dispensável ("Entendi") e
// recuperável pelo botão "Dica" da própria faixa colapsada — nunca uma
// decisão irreversível de UI.

namespace matriz::ui {

class BarraGuiaComponent : public juce::Component {
public:
    // A ordem é a do fluxo real: chegar arquivo → dar categoria → montar as
    // pastas do backup → gravar. Quem decide em qual estágio o projeto está
    // é o MainComponent (é ele que tem as contagens).
    enum class Etapa { Vazio, Classificar, Organizar, Backup };

    BarraGuiaComponent();

    // `quantidade` só é usada por Etapa::Classificar ("{n} arquivos ainda
    // sem categoria"); as outras etapas ignoram.
    void definirEtapa(Etapa etapa, int quantidade = 0);
    Etapa etapaAtual() const { return etapa_; }

    // Colapsada, a faixa vira só o botão "Dica" — continua ocupando uma
    // linha fina em vez de sumir, senão o operador que dispensou por
    // engano não tem como trazer de volta.
    bool estaColapsada() const { return colapsada_; }
    void definirColapsada(bool colapsada);

    // Disparado quando a altura desejada muda (colapsar/expandir), pra o
    // MainComponent refazer o layout.
    std::function<void()> aoMudarAltura;

    int alturaDesejada() const { return colapsada_ ? kAlturaColapsada : kAlturaExpandida; }

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kAlturaExpandida = 62;
    static constexpr int kAlturaColapsada = 26;

private:
    void atualizarTextos();

    Etapa etapa_ = Etapa::Vazio;
    int quantidade_ = 0;
    bool colapsada_ = false;
    juce::String titulo_, corpo_;
    std::unique_ptr<juce::TextButton> botaoAlternar_;
};

} // namespace matriz::ui
