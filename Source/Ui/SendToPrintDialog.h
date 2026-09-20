#pragma once

#include <JuceHeader.h>
#include <vector>
#include <string>
#include <memory>
#include "ProjetoAberto.h"
#include "../Imagem/ProcessamentoImagem.h"

namespace matriz::ui {

// ==============================================================================
// Estruturas de Dados para a Fila de Impressão e Tamanhos de Papel
// ==============================================================================

struct DefinicaoPapel {
    juce::String id;
    juce::String nome;
    double larguraCm = 0.0;
    double alturaCm = 0.0;
    bool polaroid = false;

    // Retorna dimensões do papel em pixels para um determinado DPI e orientação
    int larguraPixels(double dpi, bool paisagem) const {
        double w = paisagem ? std::max(larguraCm, alturaCm) : std::min(larguraCm, alturaCm);
        return static_cast<int>(std::round((w / 2.54) * dpi));
    }

    int alturaPixels(double dpi, bool paisagem) const {
        double h = paisagem ? std::min(larguraCm, alturaCm) : std::max(larguraCm, alturaCm);
        return static_cast<int>(std::round((h / 2.54) * dpi));
    }
};

enum class OrientacaoPapel {
    Auto = 1,
    Paisagem = 2,
    Retrato = 3
};

enum class ZoomPrevia {
    AjustarJanela = 1,
    Zoom100 = 2,
    Zoom200 = 3
};

struct ItemFilaPrint {
    std::string id;
    juce::File arquivo;
    juce::String nomeArquivo;
    int larguraOriginal = 0;
    int alturaOriginal = 0;
    int orientacaoExif = 1;
    bool valido = false;
    juce::String motivoInvalido;
    juce::Image miniatura;
    juce::Image imagemPreview; // Cache da imagem carregada para o preview central

    // Parâmetros individuais de enquadramento (pan)
    float offsetX = 0.0f; // -1.0 a +1.0
    float offsetY = 0.0f; // -1.0 a +1.0

    // Ajustes de cor individuais
    float brilho = 0.0f;
    float contraste = 0.0f;
    float saturacao = 1.0f;
    float nitidez = 0.0f;

    // Calcula se a foto tem resolução nativa adequada para o papel
    int calcularDpiEfetivo(int papelPixelW, int papelPixelH) const {
        if (papelPixelW <= 0 || papelPixelH <= 0 || larguraOriginal <= 0 || alturaOriginal <= 0) return 0;
        double ratioW = static_cast<double>(larguraOriginal) / papelPixelW;
        double ratioH = static_cast<double>(alturaOriginal) / papelPixelH;
        double minRatio = std::min(ratioW, ratioH);
        return static_cast<int>(std::round(minRatio * 300.0));
    }
};

// ==============================================================================
// Componente de Prévia do Papel (Coluna Central)
// ==============================================================================

class PreviaPapelComponent : public juce::Component {
public:
    PreviaPapelComponent();
    ~PreviaPapelComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    void configurarItem(ItemFilaPrint* item, const DefinicaoPapel& papel, OrientacaoPapel orientacao,
                        matriz::imagem::ModoEnquadramento modo, ZoomPrevia zoom);

    std::function<void(float offsetX, float offsetY)> aoMudarOffset;

private:
    juce::Rectangle<float> calcularRetanguloPapel() const;
    bool orientacaoEfetivaEhPaisagem() const;

    ItemFilaPrint* itemAtivo_ = nullptr;
    DefinicaoPapel papelAtual_;
    OrientacaoPapel orientacaoConfig_ = OrientacaoPapel::Auto;
    matriz::imagem::ModoEnquadramento modoEnquadramento_ = matriz::imagem::ModoEnquadramento::Preencher;
    ZoomPrevia zoom_ = ZoomPrevia::AjustarJanela;

    // Estado do arrasto do mouse (pan)
    bool arrastando_ = false;
    juce::Point<int> pontoCliqueOrigem_;
    float offsetInicialX_ = 0.0f;
    float offsetInicialY_ = 0.0f;
    float excessoPixelW_ = 0.0f;
    float excessoPixelH_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreviaPapelComponent)
};

// ==============================================================================
// Diálogo Principal: SendToPrintDialog (3 Colunas)
// ==============================================================================

class SendToPrintDialog : public juce::Component, public juce::ListBoxModel {
public:
    explicit SendToPrintDialog(ProjetoAberto& projeto);
    ~SendToPrintDialog() override;

    static void exibirModal(ProjetoAberto& projeto);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Métodos do ListBoxModel para a lista da fila (coluna esquerda)
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    std::function<void()> aoFechar;

    // Lista estática de papéis fotográficos padrão
    static const std::vector<DefinicaoPapel>& papeisPadrao();

private:
    void carregarFila();
    void selecionarFoto(int indice);
    void atualizarDetalhesFotoAtiva();
    void escolherPastaDestino();
    void limparListaPrint();
    void fecharDialogo();
    void resetarAjustes();

    ProjetoAberto& projeto_;
    std::vector<ItemFilaPrint> fila_;
    int indiceSelecionado_ = -1;

    juce::File pastaDestinoSelecionada_;
    juce::ThreadPool poolCarregamento_{2};

    // --- Coluna 1 (Esquerda): Lista da Fila ---
    std::unique_ptr<juce::Label> lblFilaTitulo_;
    std::unique_ptr<juce::ListBox> listaFila_;

    // --- Coluna 2 (Centro): Prévia do Papel & Zoom ---
    std::unique_ptr<juce::Component> painelCentroContainer_;
    std::unique_ptr<juce::Label> lblInfoPapel_;
    std::unique_ptr<juce::TextButton> btnZoomAjustar_;
    std::unique_ptr<juce::TextButton> btnZoom100_;
    std::unique_ptr<juce::TextButton> btnZoom200_;
    std::unique_ptr<PreviaPapelComponent> previaPapel_;
    std::unique_ptr<juce::Label> lblDicaArrastar_;

    // --- Coluna 3 (Direita): Painel de Configurações ---
    std::unique_ptr<juce::GroupComponent> grpPapel_;
    std::unique_ptr<juce::Label> lblTamanhoPapel_;
    std::unique_ptr<juce::ComboBox> cboTamanhoPapel_;
    std::unique_ptr<juce::Label> lblOrientacao_;
    std::unique_ptr<juce::ComboBox> cboOrientacao_;
    std::unique_ptr<juce::Label> lblModoEnquadramento_;
    std::unique_ptr<juce::ToggleButton> radPreencher_;
    std::unique_ptr<juce::ToggleButton> radEncaixar_;
    std::unique_ptr<juce::Label> lblResolucaoInfo_;

    std::unique_ptr<juce::GroupComponent> grpDestino_;
    std::unique_ptr<juce::Label> lblCaminhoDestino_;
    std::unique_ptr<juce::TextButton> btnEscolherPasta_;

    std::unique_ptr<juce::GroupComponent> grpAjustes_;
    std::unique_ptr<juce::Label> lblBrilho_;
    std::unique_ptr<juce::Slider> sldBrilho_;
    std::unique_ptr<juce::Label> lblContraste_;
    std::unique_ptr<juce::Slider> sldContraste_;
    std::unique_ptr<juce::Label> lblSaturacao_;
    std::unique_ptr<juce::Slider> sldSaturacao_;
    std::unique_ptr<juce::Label> lblNitidez_;
    std::unique_ptr<juce::Slider> sldNitidez_;
    std::unique_ptr<juce::TextButton> btnResetarAjustes_;

    // --- Barra Inferior: Ações e Progresso ---
    std::unique_ptr<juce::TextButton> btnLimparLista_;
    std::unique_ptr<juce::ProgressBar> barraProgresso_;
    std::unique_ptr<juce::Label> lblStatusProgresso_;
    double progressoValor_ = 0.0;
    std::unique_ptr<juce::TextButton> btnCancelar_;
    std::unique_ptr<juce::TextButton> btnExportar_;

    ZoomPrevia zoomAtual_ = ZoomPrevia::AjustarJanela;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SendToPrintDialog)
};

} // namespace matriz::ui
