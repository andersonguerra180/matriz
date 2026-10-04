#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

// Visualizador de PDF do Windows (o Mac usa o PDFView do PDFKit dentro de DocumentPreviewBridge.mm). Renderiza UMA
// página por vez em thread de fundo (Windows.Data.Pdf, ver PdfRenderer_win.h) e mostra num Viewport: zoom,
// "Fit" (largura da janela) e navegação de páginas, com os mesmos botões do Mac. A implementação só existe no Windows
// (PdfPageView_win.cpp); nos outros sistemas a classe nunca é instanciada.

namespace matriz::ui {

// Componente que só pinta a imagem da página (esticada ao próprio tamanho) ou uma mensagem.
class PaginaPdfImagem : public juce::Component {
public:
    void definirImagem(const juce::Image& img, const juce::String& mensagem);
    void paint(juce::Graphics&) override;

private:
    juce::Image imagem_;
    juce::String mensagem_;
};

class PdfPageView : public juce::Component, private juce::Timer {
public:
    // `aoMudar(pagina, total)` é chamado na message thread quando a página atual ou o total mudam.
    PdfPageView(const juce::File& pdf, std::function<void(int pagina, int total)> aoMudar);
    ~PdfPageView() override;

    void zoomIn();
    void zoomOut();
    void zoomReset();
    void proximaPagina();
    void paginaAnterior();
    void primeiraPagina();
    void ultimaPagina();

    int paginaAtual() const { return pagina_; }
    int totalPaginas() const { return paginas_; }

    void resized() override;

private:
    void timerCallback() override;
    void irPara(int pagina);
    void renderizar();
    void aplicar(const juce::Image& img, const juce::String& erro);
    void ajustarTamanhoDoConteudo();

    std::wstring caminho_;
    std::function<void(int, int)> aoMudar_;
    juce::Viewport viewport_;
    PaginaPdfImagem conteudo_;
    juce::ThreadPool pool_{1};
    // Geração compartilhada com os jobs: um render que chega depois de outro mais novo é descartado.
    std::shared_ptr<std::atomic<int>> geracao_ = std::make_shared<std::atomic<int>>(0);
    int paginas_ = 0;
    int pagina_ = 1;
    float zoom_ = 1.0f;  // 1.0 = largura da janela
    int larguraRenderizada_ = 0;
    double proporcaoPagina_ = 1.4142;  // altura / largura, até a primeira página chegar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PdfPageView)
};

} // namespace matriz::ui
