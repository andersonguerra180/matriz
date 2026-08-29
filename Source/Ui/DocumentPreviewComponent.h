#pragma once

#include <JuceHeader.h>

namespace matriz::ui {

class DocumentPreviewComponent : public juce::Component, private juce::Timer {
public:
    DocumentPreviewComponent();
    ~DocumentPreviewComponent() override;

    bool carregar(const juce::File& arquivo);

    void zoomIn();
    void zoomOut();
    void zoomReset();
    void proximaPagina();
    void paginaAnterior();
    void primeiraPagina();
    void ultimaPagina();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void atualizarBarraNavegacao();

    void* bridge_ = nullptr;
    bool carregadoNativo_ = false;
    bool carregadoTexto_ = false;

    std::unique_ptr<juce::NSViewComponent> viewComponent_;
    std::unique_ptr<juce::TextEditor> textViewer_;

    std::unique_ptr<juce::TextButton> btnPrimeiraPagina_;
    std::unique_ptr<juce::TextButton> btnPaginaAnterior_;
    std::unique_ptr<juce::Label> lblPagina_;
    std::unique_ptr<juce::TextButton> btnProximaPagina_;
    std::unique_ptr<juce::TextButton> btnUltimaPagina_;

    std::unique_ptr<juce::TextButton> btnZoomOut_;
    std::unique_ptr<juce::TextButton> btnZoomReset_;
    std::unique_ptr<juce::TextButton> btnZoomIn_;

    float tamanhoFonteTexto_ = 13.0f;
    int paginaAtual_ = 1;
    int totalPaginas_ = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DocumentPreviewComponent)
};

} // namespace matriz::ui
