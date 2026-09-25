#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>
#include <string>
#include "ProjetoAberto.h"
#include "../Imagem/ImagemBuffer.h"

namespace matriz::ui {

class BatchWatermarkDialog : public juce::Component,
                             public juce::FileDragAndDropTarget {
public:
    explicit BatchWatermarkDialog(ProjetoAberto* projeto = nullptr);
    ~BatchWatermarkDialog() override;

    static void exibirModal(ProjetoAberto* projeto = nullptr);
    static void exibirModal(std::function<std::vector<juce::File>()> obterFotosDoGrid);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    std::function<void()> aoFechar;

    // Static watermark application helpers
    static juce::Rectangle<float> calcularPosicaoLogo(float imgW, float imgH, float logoW, float logoH, const ConfiguracaoWatermark& cfg);
    static bool aplicarMarcaDaguaEmImagem(juce::Image& img, const ConfiguracaoWatermark& cfg);
    static bool aplicarMarcaDaguaEmBuffer(matriz::imagem::ImagemBuffer& buf, const ConfiguracaoWatermark& cfg);
    static bool aplicarMarcaDaguaEmArquivo(const juce::File& srcFile, const juce::File& dstFile, const ConfiguracaoWatermark& cfg);
    static juce::File resolverColisaoArquivo(const juce::File& pasta, const juce::String& nomeBase, const juce::String& sufixo, const juce::String& extensao);

private:
    void fecharDialogo();
    void escolherPastaDestino();
    void iniciarExportacao();
    void cancelarExportacao();

    ProjetoAberto* projeto_ = nullptr;
    ConfiguracaoWatermark cfg_;
    juce::File pastaDestinoSelecionada_;

    class ExportWatermarkThread;
    std::unique_ptr<ExportWatermarkThread> threadExportacao_;
    bool exportando_ = false;
    double progressoValor_ = 0.0;

    enum class OrientacaoPreview {
        Horizontal, // 3:2
        Vertical    // 2:3
    };
    OrientacaoPreview orientacaoAtual_ = OrientacaoPreview::Horizontal;

    juce::Image logoImage_;
    juce::Image dummyLandscape_;
    juce::Image dummyPortrait_;

    // Header controls
    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblSubtitulo_;

    // Orientation toggle buttons
    std::unique_ptr<juce::TextButton> btnOrientacaoH_;
    std::unique_ptr<juce::TextButton> btnOrientacaoV_;

    // Logo picker
    std::unique_ptr<juce::Label> lblSecaoLogo_;
    std::unique_ptr<juce::TextButton> btnEscolherLogo_;
    std::unique_ptr<juce::Label> lblLogoInfo_;

    // Settings
    std::unique_ptr<juce::Label> lblSecaoAjustes_;
    std::unique_ptr<juce::Label> lblOpacidade_;
    std::unique_ptr<juce::Slider> sldOpacidade_;
    std::unique_ptr<juce::Label> lblEscala_;
    std::unique_ptr<juce::Slider> sldEscala_;
    std::unique_ptr<juce::Label> lblMargem_;
    std::unique_ptr<juce::Slider> sldMargem_;
    std::unique_ptr<juce::Label> lblPosicao_;
    std::unique_ptr<juce::ComboBox> cboPosicao_;
    std::unique_ptr<juce::Label> lblDicaArrastar_;

    // Destination and Options
    std::unique_ptr<juce::Label> lblSecaoDestino_;
    std::unique_ptr<juce::Label> lblCaminhoDestino_;
    std::unique_ptr<juce::TextButton> btnEscolherPasta_;
    std::unique_ptr<juce::ToggleButton> chkExportarZip_;
    std::unique_ptr<juce::Label> lblItensMarcados_;

    // Preview Canvas
    class PreviewCanvas : public juce::Component {
    public:
        PreviewCanvas(BatchWatermarkDialog& owner);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
    private:
        BatchWatermarkDialog& owner_;
    };
    std::unique_ptr<PreviewCanvas> previewCanvas_;

    // Progress
    std::unique_ptr<juce::ProgressBar> barraProgresso_;
    std::unique_ptr<juce::Label> lblStatusProgresso_;

    // Bottom Action Buttons
    std::unique_ptr<juce::TextButton> btnCancelar_;
    std::unique_ptr<juce::TextButton> btnExportar_;

    void carregarLogo(const juce::File& file);
    void criarImagensDummy();
    void atualizarControlesParaOrientacao();
    void salvarConfiguracaoAtual();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatchWatermarkDialog)
};

} // namespace matriz::ui
