#pragma once

#include <JuceHeader.h>

#include "../Ingest/LeituraTecnica.h"
#include "ProjetoAberto.h"
#include "TimelineComponent.h"

// Preview no painel central (Reorientação completa §3.4) — troca a grade
// pelo conteúdo do arquivo selecionado, no mesmo espaço. Imagem mostra a
// imagem real; áudio mostra a forma de onda com transporte e play
// imediato; vídeo mostra o keyframe já gerado no ingest; PDF/documento/
// desconhecido mostram metadado técnico e um ícone — não existe leitor de
// PDF nem de texto nesta etapa (mesma decisão documentada em
// Source/Ingest/LeituraTecnica.cpp sobre PDFium/MuPDF: sem caminho
// FetchContent+CMake+Windows viável).

namespace matriz::ui {

class PreviewComponent : public juce::Component, private juce::Timer {
public:
    explicit PreviewComponent(ProjetoAberto& projeto);
    ~PreviewComponent() override;

    void mostrarItem(const std::string& itemId);
    const std::string& itemAtual() const { return itemId_; }

    // -1 = anterior, +1 = próximo (§3.4 — "setas pra navegar sem voltar à grade").
    std::function<void(int direcao)> aoNavegar;
    std::function<void()> aoFechar; // Esc ou botão fechar — volta pra grade

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void construirParaItem();
    void timerCallback() override;

    ProjetoAberto& projeto_;
    std::string itemId_;
    matriz::ingest::CategoriaMidia categoriaAtual_ = matriz::ingest::CategoriaMidia::Desconhecida;

    std::unique_ptr<juce::Label> cabecalhoTitulo_;
    std::unique_ptr<juce::TextButton> botaoAnterior_;
    std::unique_ptr<juce::TextButton> botaoProximo_;
    std::unique_ptr<juce::TextButton> botaoFechar_;

    juce::Image imagemPrincipal_; // imagem real, keyframe de vídeo, ou miniatura de forma de onda
    std::unique_ptr<juce::Label> labelMetadados_;
    std::unique_ptr<juce::Label> labelSemPreview_; // PDF/documento/desconhecido — nota honesta, não esconde a lacuna

    // Timeline de editor (item 8) — só construída quando categoriaAtual_ ==
    // Audio e o arquivo é decodificável. Ela é dona do transporte, do
    // jog/shuttle, do zoom e dos marcadores.
    std::unique_ptr<TimelineComponent> timeline_;

public:
    // Repassados pra fora: a ficha escuta marcadores (item 9.2) e a barra de
    // métricas escuta o nível pro VU (item 6.1).
    std::function<void()> aoMudarMarcadores;
    std::function<void(float esquerda, float direita)> aoMedirNivel;

    // Introspecção pra teste — nullptr quando o item não é áudio.
    TimelineComponent* timelineParaTeste() { return timeline_.get(); }
};

} // namespace matriz::ui
