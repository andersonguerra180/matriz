#pragma once

#include <JuceHeader.h>

#include "Moldura.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Overlay interno — o substituto dos modais nativos (§3).
//
// O crash reprodutível relatado (SIGABRT dentro de juce::AlertWindow::paint()
// -> Component::getName()) vem do modelo de janela do AlertWindow: ele cria um
// PEER nativo próprio, entra num loop modal do sistema operacional e continua
// sendo repintado pelo AppKit depois que o callback já começou a destruir o
// objeto. Nenhuma ordem de destruição do lado do JUCE fecha essa janela por
// completo.
//
// PainelOverlay não tem nada disso: é um Component filho comum do
// MainComponent, ocupando a área inteira dele. Sem peer, sem loop modal, sem
// janela nativa — pintar é a mesma pintura de qualquer painel, na message
// thread, e fechar é remover um filho. A "modalidade" é visual (scrim por
// cima) e de entrada (intercepta clique e teclado enquanto visível).

namespace matriz::ui {

class PainelOverlay : public juce::Component {
public:
    // Um botão do rodapé. `id` volta pro callback de conclusão.
    struct Botao {
        juce::String rotulo;
        int id = 0;
        bool ehPadrao = false;   // aciona com Return
        bool ehCancelar = false; // aciona com Escape
    };

    struct CampoTexto {
        juce::String id;
        juce::String rotulo;
        juce::String valorInicial;
        juce::String dica;
        std::vector<juce::String> opcoes;
    };

    // Tom do aviso: cor do destaque, ícone ao lado do título e cor do botão principal.
    enum class Tom { Info, Pergunta, Aviso, Perigo, Sucesso };

    struct Config {
        Tom tom = Tom::Pergunta;
        juce::String titulo;
        juce::String mensagem;
        std::vector<Botao> botoes;
        std::vector<juce::String> opcoes;
        int opcaoSelecionada = 0;
        std::vector<std::pair<int, juce::String>> cabecalhos;
        bool comCampoTexto = false;
        juce::String textoInicial;
        juce::String dicaCampoTexto;
        std::vector<CampoTexto> campos;
        // Conteúdo próprio (ex.: questionário "Sobre este material", painel "Receber do celular"),
        // posicionado entre a mensagem e os botões. O overlay assume a posse (shared_ptr mantém a
        // Config copiável). Sem botões, o rodapé some e o conteúdo comanda a conclusão
        // (concluirExterno).
        // Moldura (sistema de janelas): seção (cor do ícone), trilha ("ATUALIZAR"), linha de progresso e contador do cabeçalho.
        moldura::Secao secao = moldura::Secao::Auto;
        juce::String trilha;
        float progresso = -1.0f;
        juce::String contador;
        juce::String dicaRodape;   // texto discreto à esquerda do rodapé
        std::shared_ptr<juce::Component> conteudo;
        int larguraCartao = 0;    // 0 = kLarguraCartao
        int alturaConteudo = 0;   // altura desejada do conteúdo
        // Teclas que nem o conteúdo nem os botões trataram (Esc, Return…). true = tratada.
        std::function<bool(const juce::KeyPress&)> aoTecla;
    };

    struct Resultado {
        int botaoId = 0;
        int opcaoSelecionada = -1;
        juce::String texto;
        std::map<juce::String, juce::String> valores;
    };

    PainelOverlay();
    ~PainelOverlay() override;

    // Mostra o overlay. Substitui o que estiver aberto (só um por vez — é o
    // que "modal" significa aqui). O callback roda UMA vez, sempre na message
    // thread, e o overlay já está escondido quando ele começa: nenhum código
    // do callback pode ser reentrado por uma repintura do painel.
    void mostrar(Config config, std::function<void(Resultado)> aoConcluir);

    // Fecha sem acionar botão nenhum (equivale a cancelar). Seguro de chamar
    // com nada aberto.
    void fechar();

    bool estaAberto() const { return aberto_; }

    // Para o conteúdo próprio concluir o overlay (mesmo caminho de um botão: o callback roda uma vez,
    // já com o overlay escondido). Seguro de chamar de dentro de um clique do próprio conteúdo.
    void concluirExterno(int botaoId) { concluir(botaoId); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Introspecção pra teste — o harness aciona sem mouse.
    int totalBotoesParaTeste() const { return static_cast<int>(botoes_.size()); }
    void acionarBotaoParaTeste(int indice);
    void selecionarOpcaoParaTeste(int indice);
    void definirTextoParaTeste(const juce::String& texto);
    juce::String tituloParaTeste() const { return config_.titulo; }

    static constexpr int kLarguraCartao = 560;
    static constexpr int kMargem = 24;
    static constexpr int kAlturaLinha = 36;

private:
    void concluir(int botaoId);
    juce::Rectangle<int> areaDoCartao() const;
    int alturaDoCartao() const;
    int larguraDoCartao() const { return config_.larguraCartao > 0 ? config_.larguraCartao : kLarguraCartao; }
    void soltarConteudo();
    bool temRodape() const { return !config_.botoes.empty(); }

    bool aberto_ = false;
    Config config_;
    std::function<void(Resultado)> aoConcluir_;

    std::vector<std::unique_ptr<juce::TextButton>> botoes_;
    std::unique_ptr<juce::ComboBox> combo_;
    std::unique_ptr<juce::TextEditor> campo_;
    struct CampoWidget {
        juce::String id;
        std::unique_ptr<juce::TextEditor> editor;
        std::unique_ptr<juce::ComboBox> combo;
        juce::String valor() const;
    };
    std::vector<CampoWidget> camposExtras_;
    std::vector<std::unique_ptr<juce::Label>> rotulosCampos_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PainelOverlay)
};

} // namespace matriz::ui
