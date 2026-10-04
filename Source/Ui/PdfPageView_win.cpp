#include "PdfPageView.h"

#if JUCE_WINDOWS

#include "PdfRenderer_win.h"
#include "Tokens.h"

namespace matriz::ui {

void PaginaPdfImagem::definirImagem(const juce::Image& img, const juce::String& mensagem) {
    imagem_ = img;
    mensagem_ = mensagem;
    repaint();
}

void PaginaPdfImagem::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);
    if (imagem_.isValid()) {
        g.drawImage(imagem_, getLocalBounds().toFloat());
        return;
    }
    g.setColour(tema().textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    g.drawFittedText(mensagem_, getLocalBounds().reduced(16), juce::Justification::centred, 4);
}

PdfPageView::PdfPageView(const juce::File& pdf, std::function<void(int, int)> aoMudar)
    : caminho_(pdf.getFullPathName().toWideCharPointer()), aoMudar_(std::move(aoMudar)) {
    viewport_.setViewedComponent(&conteudo_, false);
    viewport_.setScrollBarsShown(true, true);
    addAndMakeVisible(viewport_);
    conteudo_.definirImagem({}, "Loading PDF...");

    // Abre e conta as páginas fora da message thread; só então renderiza a primeira.
    const int geracao = geracao_->load();
    pool_.addJob([caminho = caminho_, geracao_ = geracao_, geracao, safe = juce::Component::SafePointer<PdfPageView>(this)] {
        pdfwin::InfoPdf info;
        std::string erro;
        const bool ok = pdfwin::abrir(caminho, info, erro);
        juce::MessageManager::callAsync([safe, geracao_, geracao, ok, info, erro] {
            if (!safe || geracao_->load() != geracao) return;
            if (!ok) {
                safe->conteudo_.definirImagem({}, "This PDF could not be opened (password-protected or damaged).\n" +
                                                      juce::String::fromUTF8(erro.c_str()));
                return;
            }
            safe->paginas_ = info.paginas;
            safe->pagina_ = 1;
            if (info.larguraPagina1 > 0.0) safe->proporcaoPagina_ = info.alturaPagina1 / info.larguraPagina1;
            if (safe->aoMudar_) safe->aoMudar_(safe->pagina_, safe->paginas_);
            safe->ajustarTamanhoDoConteudo();
            safe->renderizar();
        });
    });
}

PdfPageView::~PdfPageView() {
    stopTimer();
    ++*geracao_;
    pool_.removeAllJobs(true, 10000);
    viewport_.setViewedComponent(nullptr, false);
}

void PdfPageView::irPara(int pagina) {
    if (paginas_ <= 0) return;
    pagina = juce::jlimit(1, paginas_, pagina);
    if (pagina == pagina_) return;
    pagina_ = pagina;
    if (aoMudar_) aoMudar_(pagina_, paginas_);
    viewport_.setViewPosition(0, 0);
    renderizar();
}

void PdfPageView::zoomIn() { zoom_ = juce::jmin(4.0f, zoom_ * 1.25f); ajustarTamanhoDoConteudo(); renderizar(); }
void PdfPageView::zoomOut() { zoom_ = juce::jmax(0.25f, zoom_ / 1.25f); ajustarTamanhoDoConteudo(); renderizar(); }
void PdfPageView::zoomReset() { zoom_ = 1.0f; ajustarTamanhoDoConteudo(); renderizar(); }
void PdfPageView::proximaPagina() { irPara(pagina_ + 1); }
void PdfPageView::paginaAnterior() { irPara(pagina_ - 1); }
void PdfPageView::primeiraPagina() { irPara(1); }
void PdfPageView::ultimaPagina() { irPara(paginas_); }

void PdfPageView::resized() {
    viewport_.setBounds(getLocalBounds());
    ajustarTamanhoDoConteudo();
    startTimer(200);  // re-renderiza na largura nova só quando o usuário para de redimensionar
}

void PdfPageView::timerCallback() {
    stopTimer();
    if (paginas_ > 0 && viewport_.getMaximumVisibleWidth() != larguraRenderizada_) renderizar();
}

void PdfPageView::ajustarTamanhoDoConteudo() {
    const int largura = juce::jmax(100, juce::roundToInt(static_cast<float>(viewport_.getMaximumVisibleWidth()) * zoom_));
    conteudo_.setSize(largura, juce::roundToInt(static_cast<double>(largura) * proporcaoPagina_));
}

void PdfPageView::renderizar() {
    if (paginas_ <= 0) return;
    const int geracao = ++*geracao_;
    const int largura = juce::jlimit(200, 4096, juce::roundToInt(static_cast<float>(viewport_.getMaximumVisibleWidth()) * zoom_));
    larguraRenderizada_ = viewport_.getMaximumVisibleWidth();
    pool_.addJob([caminho = caminho_, pagina = pagina_, largura, geracao_ = geracao_, geracao,
                  safe = juce::Component::SafePointer<PdfPageView>(this)] {
        if (geracao_->load() != geracao) return;  // já há um pedido mais novo
        std::vector<std::uint8_t> png;
        int w = 0, h = 0;
        std::string erro;
        const bool ok = pdfwin::renderizarPagina(caminho, pagina, largura, png, w, h, erro);
        juce::Image img;
        if (ok) img = juce::ImageFileFormat::loadFrom(png.data(), png.size());
        juce::MessageManager::callAsync([safe, geracao_, geracao, img, ok, erro] {
            if (!safe || geracao_->load() != geracao) return;
            safe->aplicar(img, ok ? juce::String() : juce::String::fromUTF8(erro.c_str()));
        });
    });
}

void PdfPageView::aplicar(const juce::Image& img, const juce::String& erro) {
    if (img.isValid() && img.getWidth() > 0) proporcaoPagina_ = static_cast<double>(img.getHeight()) / img.getWidth();
    ajustarTamanhoDoConteudo();
    conteudo_.definirImagem(img, erro.isNotEmpty() ? "Could not render this page.\n" + erro : juce::String());
}

} // namespace matriz::ui

#endif // JUCE_WINDOWS
