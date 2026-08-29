#include "DocumentPreviewComponent.h"
#include "DocumentPreviewBridge.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

DocumentPreviewComponent::DocumentPreviewComponent() {
    bridge_ = docCreate();

    btnPrimeiraPagina_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x8f\xae")); // ⏮
    btnPrimeiraPagina_->setTooltip("First Page");
    btnPrimeiraPagina_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnPrimeiraPagina_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnPrimeiraPagina_->onClick = [this] { primeiraPagina(); };
    addAndMakeVisible(*btnPrimeiraPagina_);

    btnPaginaAnterior_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x97\x80")); // ◀
    btnPaginaAnterior_->setTooltip("Previous Page");
    btnPaginaAnterior_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnPaginaAnterior_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnPaginaAnterior_->onClick = [this] { paginaAnterior(); };
    addAndMakeVisible(*btnPaginaAnterior_);

    lblPagina_ = std::make_unique<juce::Label>("", "1 / 1");
    lblPagina_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    lblPagina_->setColour(juce::Label::textColourId, tema().textoPrimario);
    lblPagina_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblPagina_);

    btnProximaPagina_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x96\xb6")); // ▶
    btnProximaPagina_->setTooltip("Next Page");
    btnProximaPagina_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnProximaPagina_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnProximaPagina_->onClick = [this] { proximaPagina(); };
    addAndMakeVisible(*btnProximaPagina_);

    btnUltimaPagina_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x8f\xad")); // ⏭
    btnUltimaPagina_->setTooltip("Last Page");
    btnUltimaPagina_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnUltimaPagina_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnUltimaPagina_->onClick = [this] { ultimaPagina(); };
    addAndMakeVisible(*btnUltimaPagina_);

    btnZoomOut_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x88\x92")); // −
    btnZoomOut_->setTooltip("Zoom Out");
    btnZoomOut_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnZoomOut_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnZoomOut_->onClick = [this] { zoomOut(); };
    addAndMakeVisible(*btnZoomOut_);

    btnZoomReset_ = std::make_unique<juce::TextButton>("Fit");
    btnZoomReset_->setTooltip("Reset Zoom / Fit to Window");
    btnZoomReset_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnZoomReset_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnZoomReset_->onClick = [this] { zoomReset(); };
    addAndMakeVisible(*btnZoomReset_);

    btnZoomIn_ = std::make_unique<juce::TextButton>("+");
    btnZoomIn_->setTooltip("Zoom In");
    btnZoomIn_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnZoomIn_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnZoomIn_->onClick = [this] { zoomIn(); };
    addAndMakeVisible(*btnZoomIn_);
}

DocumentPreviewComponent::~DocumentPreviewComponent() {
    stopTimer();
    viewComponent_.reset();
    textViewer_.reset();
    if (bridge_) {
        docDestroy(static_cast<DocHandle>(bridge_));
        bridge_ = nullptr;
    }
}

bool DocumentPreviewComponent::carregar(const juce::File& arquivo) {
    stopTimer();
    viewComponent_.reset();
    textViewer_.reset();
    carregadoNativo_ = false;
    carregadoTexto_ = false;
    paginaAtual_ = 1;
    totalPaginas_ = 1;

    if (!arquivo.existsAsFile()) return false;

    auto ext = arquivo.getFileExtension().toLowerCase().replace(".", "");

    if (bridge_ && docLoad(static_cast<DocHandle>(bridge_), arquivo.getFullPathName().toRawUTF8())) {
        void* nsView = docGetNSView(static_cast<DocHandle>(bridge_));
        if (nsView) {
            viewComponent_ = std::make_unique<juce::NSViewComponent>();
            viewComponent_->setView(nsView);
            addAndMakeVisible(*viewComponent_);
            carregadoNativo_ = true;
            totalPaginas_ = docGetTotalPages(static_cast<DocHandle>(bridge_));
            paginaAtual_ = docGetCurrentPage(static_cast<DocHandle>(bridge_));
            startTimerHz(10);
            resized();
            atualizarBarraNavegacao();
            return true;
        }
    }

    // Fallback for text / markdown / rtf or when native bridge is unavailable
    juce::String conteudo = arquivo.loadFileAsString();
    if (conteudo.isNotEmpty() || ext == "txt" || ext == "md" || ext == "rtf" || ext == "csv" || ext == "tsv") {
        textViewer_ = std::make_unique<juce::TextEditor>();
        textViewer_->setMultiLine(true, true);
        textViewer_->setReadOnly(true);
        textViewer_->setScrollbarsShown(true);
        textViewer_->setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), tamanhoFonteTexto_, juce::Font::plain)));
        textViewer_->setColour(juce::TextEditor::backgroundColourId, tema().painel);
        textViewer_->setColour(juce::TextEditor::textColourId, tema().textoPrimario);
        textViewer_->setColour(juce::TextEditor::outlineColourId, tema().borda);
        textViewer_->setText(conteudo, false);
        addAndMakeVisible(*textViewer_);
        carregadoTexto_ = true;
        totalPaginas_ = 1;
        paginaAtual_ = 1;
        resized();
        atualizarBarraNavegacao();
        return true;
    }

    return false;
}

void DocumentPreviewComponent::zoomIn() {
    if (carregadoNativo_ && bridge_) {
        docZoomIn(static_cast<DocHandle>(bridge_));
    } else if (textViewer_) {
        tamanhoFonteTexto_ = juce::jmin(32.0f, tamanhoFonteTexto_ + 2.0f);
        textViewer_->setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), tamanhoFonteTexto_, juce::Font::plain)));
    }
}

void DocumentPreviewComponent::zoomOut() {
    if (carregadoNativo_ && bridge_) {
        docZoomOut(static_cast<DocHandle>(bridge_));
    } else if (textViewer_) {
        tamanhoFonteTexto_ = juce::jmax(8.0f, tamanhoFonteTexto_ - 2.0f);
        textViewer_->setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), tamanhoFonteTexto_, juce::Font::plain)));
    }
}

void DocumentPreviewComponent::zoomReset() {
    if (carregadoNativo_ && bridge_) {
        docZoomReset(static_cast<DocHandle>(bridge_));
    } else if (textViewer_) {
        tamanhoFonteTexto_ = 13.0f;
        textViewer_->setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), tamanhoFonteTexto_, juce::Font::plain)));
    }
}

void DocumentPreviewComponent::proximaPagina() {
    if (carregadoNativo_ && bridge_) {
        docGoToNextPage(static_cast<DocHandle>(bridge_));
        paginaAtual_ = docGetCurrentPage(static_cast<DocHandle>(bridge_));
        atualizarBarraNavegacao();
    } else if (textViewer_) {
        textViewer_->pageDown(false);
    }
}

void DocumentPreviewComponent::paginaAnterior() {
    if (carregadoNativo_ && bridge_) {
        docGoToPreviousPage(static_cast<DocHandle>(bridge_));
        paginaAtual_ = docGetCurrentPage(static_cast<DocHandle>(bridge_));
        atualizarBarraNavegacao();
    } else if (textViewer_) {
        textViewer_->pageUp(false);
    }
}

void DocumentPreviewComponent::primeiraPagina() {
    if (carregadoNativo_ && bridge_) {
        docGoToFirstPage(static_cast<DocHandle>(bridge_));
        paginaAtual_ = docGetCurrentPage(static_cast<DocHandle>(bridge_));
        atualizarBarraNavegacao();
    } else if (textViewer_) {
        textViewer_->moveCaretToTop(false);
    }
}

void DocumentPreviewComponent::ultimaPagina() {
    if (carregadoNativo_ && bridge_) {
        docGoToLastPage(static_cast<DocHandle>(bridge_));
        paginaAtual_ = docGetCurrentPage(static_cast<DocHandle>(bridge_));
        atualizarBarraNavegacao();
    } else if (textViewer_) {
        textViewer_->moveCaretToEnd(false);
    }
}

void DocumentPreviewComponent::timerCallback() {
    if (carregadoNativo_ && bridge_) {
        int cur = docGetCurrentPage(static_cast<DocHandle>(bridge_));
        int tot = docGetTotalPages(static_cast<DocHandle>(bridge_));
        if (cur != paginaAtual_ || tot != totalPaginas_) {
            paginaAtual_ = cur;
            totalPaginas_ = tot;
            atualizarBarraNavegacao();
        }
    }
}

void DocumentPreviewComponent::atualizarBarraNavegacao() {
    if (lblPagina_) {
        lblPagina_->setText(juce::String(paginaAtual_) + " / " + juce::String(totalPaginas_), juce::dontSendNotification);
    }
}

void DocumentPreviewComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);

    auto topo = getLocalBounds().removeFromTop(36);
    g.setColour(tema().painel);
    g.fillRect(topo);
    g.setColour(tema().borda);
    g.drawHorizontalLine(topo.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    if (!carregadoNativo_ && !carregadoTexto_) {
        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
        g.drawText("No document preview available", getLocalBounds(), juce::Justification::centred);
    }
}

void DocumentPreviewComponent::resized() {
    auto area = getLocalBounds();
    auto topo = area.removeFromTop(36).reduced(8, 4);

    // Left side: Page Navigation Controls
    btnPrimeiraPagina_->setBounds(topo.removeFromLeft(28));
    topo.removeFromLeft(4);
    btnPaginaAnterior_->setBounds(topo.removeFromLeft(28));
    topo.removeFromLeft(4);
    lblPagina_->setBounds(topo.removeFromLeft(64));
    topo.removeFromLeft(4);
    btnProximaPagina_->setBounds(topo.removeFromLeft(28));
    topo.removeFromLeft(4);
    btnUltimaPagina_->setBounds(topo.removeFromLeft(28));

    // Right side: Zoom Controls
    btnZoomIn_->setBounds(topo.removeFromRight(28));
    topo.removeFromRight(4);
    btnZoomReset_->setBounds(topo.removeFromRight(36));
    topo.removeFromRight(4);
    btnZoomOut_->setBounds(topo.removeFromRight(28));

    auto viewArea = area.reduced(4);
    if (viewComponent_) {
        viewComponent_->setBounds(viewArea);
        if (bridge_) docResize(static_cast<DocHandle>(bridge_), viewArea.getWidth(), viewArea.getHeight());
    }
    if (textViewer_) {
        textViewer_->setBounds(viewArea);
    }
}

} // namespace matriz::ui
