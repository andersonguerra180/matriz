#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ProjetoAberto.h"

// Timeline de editor em tempo real (item 8) — não um player simples.
// Forma de onda desenhada na largura do painel, cursor de reprodução em
// tempo real, clique posiciona, zoom do arquivo inteiro até detalhe de
// amostra, régua de tempo com opção de timecode, e marcadores.
//
// Transporte (item 7): PLAY/STOP/START/END, barra de espaço alternando play
// e stop. Jog wheel (item 7.2) com scrub de áudio, atrelada ao scroll do
// mouse, precisão de amostra; shuttle contínuo como segundo modo.
//
// Marcadores (item 8.2/8.3): tecla única crava no ponto atual e abre o campo
// inline; Enter fecha sem sair da reprodução; arrastável pra corrigir;
// editável e apagável depois. Cada marcador é gravado como observação do
// item (item_observacao, com minutagem_ms) — marcadores e observações são a
// MESMA lista, vista de dois lugares (item 9.2), então editar num reflete no
// outro sem sincronização própria: os dois leem a mesma tabela.
//
// NUNCA DESTRUTIVO (item 8.3): nada aqui escreve no arquivo. Marcador é
// registro no banco; a gravação em metadado acontece na cópia de backup.

namespace matriz::ui {

class TimelineComponent : public juce::Component, private juce::Timer {
public:
    explicit TimelineComponent(ProjetoAberto& projeto);
    ~TimelineComponent() override;

    // Carrega o arquivo de áudio/vídeo de um item. Devolve false quando o
    // formato não é decodificável aqui (vídeo sem trilha extraível) — o
    // chamador mostra o que sabe em vez de uma timeline vazia mentindo.
    bool carregarItem(const std::string& itemId, const juce::File& arquivo);
    void descarregar();

    // --- Transporte (item 7.1) ---
    void tocar();
    void parar();
    void alternarPlayStop();
    void irParaInicio();
    void irParaFim();
    bool estaTocando() const;
    double posicaoSegundos() const;
    double duracaoSegundos() const { return duracao_; }
    void posicionar(double segundos);

    // --- Jog e shuttle (item 7.2) ---
    enum class ModoRoda { Jog, Shuttle };
    void alternarModoRoda();
    ModoRoda modoRoda() const { return modoRoda_; }
    // Delta em "cliques" de roda; jog move por amostras, shuttle muda a
    // velocidade contínua (±16x, com detent no zero).
    void girarRoda(float delta);
    double velocidadeShuttle() const { return velocidadeShuttle_; }

    // --- Zoom (item 8.1) ---
    void aplicarZoom(double fator, double centroSegundos);
    void zoomArquivoInteiro();
    double janelaVisivelSegundos() const { return fimVisivel_ - inicioVisivel_; }

    // Régua em timecode em vez de mm:ss (item 8.1).
    void definirMostrarTimecode(bool sim);
    bool mostrandoTimecode() const { return mostrarTimecode_; }

    // --- Marcadores (item 8.2) ---
    // Crava no ponto atual e abre o campo inline pro operador escrever o que
    // aquele marcador indica. Não para a reprodução (item 8.2).
    void inserirMarcadorNaPosicaoAtual();
    // Recarrega da tabela item_observacao — chamado quando a ficha edita uma
    // observação, pra timeline refletir (item 9.2, "editar em um reflete no
    // outro").
    void recarregarMarcadores();
    int totalMarcadores() const { return static_cast<int>(marcadores_.size()); }

    // Disparado quando um marcador é criado/movido/apagado — a ficha escuta
    // pra atualizar a lista de observações.
    std::function<void()> aoMudarMarcadores;
    // Nível de pico do bloco tocando agora (0..1 por canal) — a barra de
    // métricas usa pro VU.
    std::function<void(float esquerda, float direita)> aoMedirNivel;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Introspecção pra teste.
    struct MarcadorVisivel {
        std::string observacaoId;
        double segundos = 0.0;
        juce::String texto;
    };
    std::vector<MarcadorVisivel> marcadoresParaTeste() const;
    void editarTextoDoMarcadorParaTeste(int indice, const juce::String& texto);
    void arrastarMarcadorParaTeste(int indice, double novosSegundos);
    void apagarMarcadorParaTeste(int indice);

    static constexpr int kAlturaRegua = 18;
    static constexpr int kAlturaFaixaMarcadores = 20;
    static constexpr int kAlturaTransporte = 30;

private:
    void timerCallback() override;
    double segundosNoX(int x) const;
    int xDoSegundo(double s) const;
    juce::Rectangle<int> areaOnda() const;
    juce::Rectangle<int> areaMarcadores() const;
    juce::Rectangle<int> areaReguaLocal() const;
    int indiceMarcadorNoX(int x) const;
    void abrirEditorInline(int indiceMarcador);
    void fecharEditorInline(bool gravando);
    juce::String formatarPosicao(double segundos) const;

    ProjetoAberto& projeto_;
    std::string itemId_;
    juce::File arquivo_;
    double duracao_ = 0.0;
    double sampleRate_ = 44100.0;

    juce::AudioFormatManager formatManager_;
    juce::AudioThumbnailCache cacheOnda_{8};
    std::unique_ptr<juce::AudioThumbnail> onda_;
    juce::AudioTransportSource transporte_;
    std::unique_ptr<juce::AudioFormatReaderSource> fonte_;

    double inicioVisivel_ = 0.0, fimVisivel_ = 0.0;
    bool mostrarTimecode_ = false;

    ModoRoda modoRoda_ = ModoRoda::Jog;
    double velocidadeShuttle_ = 0.0;

    struct Marcador {
        std::string observacaoId;
        double segundos = 0.0;
        juce::String texto;
    };
    std::vector<Marcador> marcadores_;
    int indiceArrastado_ = -1;
    int indiceEditando_ = -1;
    std::unique_ptr<juce::TextEditor> editorInline_;

    std::unique_ptr<juce::TextButton> botaoPlayStop_, botaoInicio_, botaoFim_, botaoModoRoda_, botaoZoomTudo_,
        botaoTimecode_, botaoMarcador_;
};

} // namespace matriz::ui
