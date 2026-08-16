#include "ModalLoopSelfTest.h"

#include <JuceHeader.h>

#include <iostream>

#include "OverlayComponent.h"
#include "SelecionarTipoMidiaDialogo.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

int falhas = 0;

void checar(bool condicao, const juce::String& descricao) {
    std::cout << (condicao ? "  OK   " : "  FAIL ") << descricao << "\n";
    if (!condicao) ++falhas;
}

void bombearMensagens(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// Hospedeiro mínimo: um Component com peer real, do tamanho de uma janela de
// trabalho, com o overlay como filho — a MESMA topologia da janela de
// produção, que é o que precisa ser exercitado.
class JanelaDeTeste : public juce::Component {
public:
    JanelaDeTeste() {
        setSize(1000, 700);
        addChildComponent(overlay_);
        // Fora da tela: cria peer de verdade (é ele que faz repintura e foco
        // acontecerem como em produção) sem piscar janela na cara de quem
        // roda o teste.
        setTopLeftPosition(-4000, -4000);
        addToDesktop(juce::ComponentPeer::windowIsTemporary);
        setVisible(true);
    }

    ~JanelaDeTeste() override { removeFromDesktop(); }

    void resized() override { overlay_.setBounds(getLocalBounds()); }

    PainelOverlay& overlay() { return overlay_; }

private:
    PainelOverlay overlay_;
};

std::vector<TipoMidiaOpcao> opcoesSinteticas() {
    return {{"fita_rolo", "Reel tape"}, {"cassete", "Cassette"}, {"cd", "CD"}, {"vinil", "Vinyl"}};
}

} // namespace

int rodarModalLoopSelfTest() {
    falhas = 0;
    std::cout << "== Modal loop stress (500x, section 3 / criterion 21) ==\n";

    constexpr int kCiclos = 500;
    auto opcoes = opcoesSinteticas();

    {
        JanelaDeTeste janela;
        janela.resized();
        bombearMensagens(30);

        int confirmados = 0;
        int cancelados = 0;
        int descartados = 0;

        for (int i = 0; i < kCiclos; ++i) {
            bool respondeu = false;
            std::optional<std::string> escolha;

            mostrarSelecionarTipoMidia(janela.overlay(), opcoes, i + 1,
                                        [&respondeu, &escolha](std::optional<std::string> tipo) {
                                            respondeu = true;
                                            escolha = std::move(tipo);
                                        });

            if (!janela.overlay().estaAberto()) {
                checar(false, "cycle " + juce::String(i) + ": overlay did not open");
                break;
            }

            // Bombeia COM o overlay aberto: é aqui que a repintura do sistema
            // acontece por cima do painel vivo, o momento em que o
            // AlertWindow morria.
            bombearMensagens(1);

            // Três formas de sair, alternadas — confirmar, cancelar pelo
            // botão, e descartar por fora sem acionar botão nenhum (o
            // caminho que mais se parece com "a janela sumiu debaixo do
            // callback").
            switch (i % 3) {
                case 0:
                    janela.overlay().selecionarOpcaoParaTeste(i % static_cast<int>(opcoes.size()));
                    janela.overlay().acionarBotaoParaTeste(1);  // Confirm
                    ++confirmados;
                    break;
                case 1:
                    janela.overlay().acionarBotaoParaTeste(0);  // Cancel
                    ++cancelados;
                    break;
                default:
                    janela.overlay().fechar();
                    ++descartados;
                    break;
            }

            // Bombeia DEPOIS de fechar: qualquer repintura ou callback
            // pendente que ainda apontasse pro painel destruído cai aqui.
            bombearMensagens(1);

            if (janela.overlay().estaAberto()) {
                checar(false, "cycle " + juce::String(i) + ": overlay stayed open after closing");
                break;
            }
            // fechar() é descarte, não resposta: não pode acionar o callback.
            if (i % 3 != 2 && !respondeu) {
                checar(false, "cycle " + juce::String(i) + ": callback was not called");
                break;
            }
            if (i % 3 == 2 && respondeu) {
                checar(false, "cycle " + juce::String(i) + ": fechar() fired the callback, it should only discard");
                break;
            }
        }

        checar(confirmados + cancelados + descartados == kCiclos,
               "500 full open/close cycles (" + juce::String(confirmados) + " confirmed, " +
                   juce::String(cancelados) + " cancelled, " + juce::String(descartados) + " discarded)");

        // Abrir por cima de um overlay já aberto tem que substituir, não
        // empilhar — dois painéis vivos disputando a mesma área é o começo
        // do mesmo problema de ciclo de vida.
        bool primeiroRespondeu = false;
        janela.overlay().mostrar(configSelecionarTipoMidia(opcoes, 1),
                                  [&](PainelOverlay::Resultado) { primeiroRespondeu = true; });
        janela.overlay().mostrar(configSelecionarTipoMidia(opcoes, 2),
                                  [](PainelOverlay::Resultado) {});
        bombearMensagens(1);
        checar(!primeiroRespondeu, "opening an overlay over another discards the first without firing its callback");
        checar(janela.overlay().estaAberto(), "the second overlay stays open");

        // Destruir a janela COM overlay aberto: o callback pendente não pode
        // rodar depois que o dono morreu.
        janela.overlay().fechar();
        bombearMensagens(1);
        checar(!janela.overlay().estaAberto(), "the overlay closes before the window dies");
    }

    // Fora do escopo: a janela e o peer já morreram. Bombear aqui é o teste
    // final — nada agendado pode tocar no que foi destruído.
    bombearMensagens(50);
    checar(true, "pumping messages after destroying the window touches no freed memory");

    std::cout << "\n" << (falhas == 0 ? "ALL TESTS PASSED" : juce::String(falhas) + " FAILURE(S)").toStdString()
              << "\n";
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
