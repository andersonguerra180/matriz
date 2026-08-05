#pragma once

#include <JuceHeader.h>

#include <optional>
#include <string>
#include <vector>

#include "ProjetoAberto.h"

// Barra de métricas inferior (item 6), fixa no rodapé — mesma estética e
// comportamento da do BKR Groove Sculptor.
//
// LUFS-I e LRA são PRÉ-CALCULADOS na leitura técnica (item 6.1: "não em
// tempo real"), lidos de arquivo.caracteristicas_tecnicas_json. FPS vem dos
// metadados. O medidor VU é o único elemento em tempo real — balística de
// 300 ms sobre o áudio que está tocando agora.
//
// A barra se adapta ao tipo do arquivo (item 6.2): áudio mostra métricas de
// áudio; vídeo mostra as de áudio e de vídeo; imagem mostra dimensão,
// profundidade e espaço de cor; documento mostra páginas e tamanho. Sem
// nada selecionado, mostra o resumo do que está na grade.

namespace matriz::ui {

class BarraMetricasComponent : public juce::Component, private juce::Timer {
public:
    BarraMetricasComponent();
    ~BarraMetricasComponent() override;

    // Item selecionado — lê a leitura técnica do arquivo principal dele.
    // "" volta pro resumo da grade.
    void mostrarItem(ProjetoAberto& projeto, const std::string& itemId);

    // Resumo do que está visível na grade (nenhum item selecionado).
    void mostrarResumoDaGrade(int totalItens, juce::int64 tamanhoTotalBytes);

    // Alimenta o medidor VU com o nível do áudio tocando agora. Chamado pelo
    // preview/transporte a cada bloco; a balística de 300 ms é aplicada aqui.
    void alimentarVu(float picoEsquerda, float picoDireita);
    void zerarVu();

    // Introspecção pra teste: os pares rótulo/valor atualmente exibidos.
    std::vector<std::pair<juce::String, juce::String>> camposParaTeste() const { return campos_; }

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kAlturaPreferida = 44;

private:
    void timerCallback() override;
    void reconstruirCampos(const juce::var& tecnica, const std::string& extensao);

    std::vector<std::pair<juce::String, juce::String>> campos_;
    juce::String titulo_;

    // Balística VU (300 ms de integração, item 6.1). Dois canais; queda
    // exponencial no timer, subida imediata — é o que dá a sensação de
    // agulha de VU em vez de um pico que pisca.
    float vuEsquerda_ = 0.0f, vuDireita_ = 0.0f;
    float vuAlvoEsquerda_ = 0.0f, vuAlvoDireita_ = 0.0f;
    bool vuAtivo_ = false;
};

} // namespace matriz::ui
