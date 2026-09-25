#include "BatchWatermarkDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Vault/Resolucao.h"
#include "../Imagem/ProcessamentoImagem.h"

namespace matriz::ui {

// ==============================================================================
// PreviewCanvas
// ==============================================================================

BatchWatermarkDialog::PreviewCanvas::PreviewCanvas(BatchWatermarkDialog& owner)
    : owner_(owner) {
}

void BatchWatermarkDialog::PreviewCanvas::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto bounds = getLocalBounds().toFloat();

    // Canvas background
    g.setColour(tk.painel);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(tk.borda);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    const juce::Image& sample = (owner_.orientacaoAtual_ == OrientacaoPreview::Horizontal)
                                    ? owner_.dummyLandscape_
                                    : owner_.dummyPortrait_;

    if (!sample.isValid()) return;

    auto inner = bounds.reduced(16.0f);
    float imgW = (float)sample.getWidth();
    float imgH = (float)sample.getHeight();

    float scale = std::min(inner.getWidth() / imgW, inner.getHeight() / imgH);
    float drawW = imgW * scale;
    float drawH = imgH * scale;
    float drawX = inner.getX() + (inner.getWidth() - drawW) * 0.5f;
    float drawY = inner.getY() + (inner.getHeight() - drawH) * 0.5f;

    juce::Rectangle<float> imgDestRect(drawX, drawY, drawW, drawH);

    // Drop shadow behind dummy photo
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(imgDestRect.translated(2.0f, 3.0f), 6.0f);

    // Draw dummy photo
    g.drawImage(sample, imgDestRect);
    g.setColour(tk.borda.withAlpha(0.8f));
    g.drawRoundedRectangle(imgDestRect, 6.0f, 1.2f);

    // Draw watermark overlay if logo loaded
    if (owner_.logoImage_.isValid()) {
        float logoW = (float)owner_.logoImage_.getWidth();
        float logoH = (float)owner_.logoImage_.getHeight();

        juce::Rectangle<float> logoFullRect = BatchWatermarkDialog::calcularPosicaoLogo(
            imgW, imgH, logoW, logoH, owner_.cfg_);

        float prevLogoX = drawX + (logoFullRect.getX() / imgW) * drawW;
        float prevLogoY = drawY + (logoFullRect.getY() / imgH) * drawH;
        float prevLogoW = (logoFullRect.getWidth() / imgW) * drawW;
        float prevLogoH = (logoFullRect.getHeight() / imgH) * drawH;

        juce::Rectangle<float> prevLogoRect(prevLogoX, prevLogoY, prevLogoW, prevLogoH);

        g.setOpacity(juce::jlimit(0.0f, 1.0f, owner_.cfg_.opacidade));
        g.drawImage(owner_.logoImage_, prevLogoRect);
        g.setOpacity(1.0f);

        // Selection highlight ring around logo in preview
        g.setColour(juce::Colour(0xffffcc00).withAlpha(0.9f));
        g.drawRoundedRectangle(prevLogoRect.expanded(1.5f), 3.0f, 1.5f);
    } else {
        // Subtle hint when no logo is selected yet
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::italic)));
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
        g.drawText(isPt ? juce::String::fromUTF8("Selecione um arquivo de logo (PNG ou JPG) para visualizar a marca d'água.")
                        : "Select a logo file (PNG or JPG) to preview the watermark overlay.",
                   imgDestRect, juce::Justification::centred, true);
    }
}

void BatchWatermarkDialog::PreviewCanvas::mouseDown(const juce::MouseEvent& e) {
    const juce::Image& sample = (owner_.orientacaoAtual_ == OrientacaoPreview::Horizontal)
                                    ? owner_.dummyLandscape_
                                    : owner_.dummyPortrait_;
    if (!sample.isValid() || !owner_.logoImage_.isValid()) return;

    auto inner = getLocalBounds().toFloat().reduced(16.0f);
    float imgW = (float)sample.getWidth();
    float imgH = (float)sample.getHeight();

    float scale = std::min(inner.getWidth() / imgW, inner.getHeight() / imgH);
    float drawW = imgW * scale;
    float drawH = imgH * scale;
    float drawX = inner.getX() + (inner.getWidth() - drawW) * 0.5f;
    float drawY = inner.getY() + (inner.getHeight() - drawH) * 0.5f;

    float relX = (e.position.x - drawX) / drawW;
    float relY = (e.position.y - drawY) / drawH;

    relX = juce::jlimit(0.05f, 0.95f, relX);
    relY = juce::jlimit(0.05f, 0.95f, relY);

    if (owner_.orientacaoAtual_ == OrientacaoPreview::Horizontal) {
        owner_.cfg_.customPosX_H = relX;
        owner_.cfg_.customPosY_H = relY;
        owner_.cfg_.posicaoIdH = 8; // Custom
    } else {
        owner_.cfg_.customPosX_V = relX;
        owner_.cfg_.customPosY_V = relY;
        owner_.cfg_.posicaoIdV = 8; // Custom
    }

    if (owner_.cboPosicao_) {
        owner_.cboPosicao_->setSelectedId(8, juce::dontSendNotification);
    }
    repaint();
}

void BatchWatermarkDialog::PreviewCanvas::mouseDrag(const juce::MouseEvent& e) {
    mouseDown(e);
}

// ==============================================================================
// Thread de Exportação em Segundo Plano
// ==============================================================================

class BatchWatermarkDialog::ExportWatermarkThread : public juce::Thread {
public:
    struct Params {
        juce::File pastaDestino;
        bool exportarZip = false;
        juce::String nomeProjeto;
        ConfiguracaoWatermark configWatermark;
        std::vector<std::pair<std::string, juce::File>> itens;
    };

    ExportWatermarkThread(Params params,
                          std::function<void(double, const juce::String&)> onProgresso,
                          std::function<void(bool, int, const juce::File&, const juce::StringArray&)> onConcluido)
        : juce::Thread("ExportWatermarkThread"),
          params_(std::move(params)),
          onProgresso_(std::move(onProgresso)),
          onConcluido_(std::move(onConcluido)) {}

    ~ExportWatermarkThread() override {
        stopThread(4000);
    }

    void run() override {
        int total = static_cast<int>(params_.itens.size());
        int exportados = 0;
        juce::StringArray erros;

        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
        notificarProgresso(0.0, isPt ? juce::String::fromUTF8("Iniciando exportação...") : "Starting export...");

        juce::File pastaDestinoEfetiva = params_.pastaDestino;
        juce::File pastaTemp;
        if (params_.exportarZip) {
            pastaTemp = juce::File::createTempFile("bkr_wm_temp");
            pastaTemp.deleteFile();
            pastaTemp.createDirectory();
            pastaDestinoEfetiva = pastaTemp;
        }

        std::vector<juce::File> arquivosExportados;

        for (int i = 0; i < total; ++i) {
            if (threadShouldExit()) break;

            const auto& item = params_.itens[static_cast<size_t>(i)];
            juce::String nomeArq = item.second.getFileName();
            double p = static_cast<double>(i) / std::max(1, total);

            juce::String msg = (isPt
                ? ("Processando " + juce::String(i + 1) + " de " + juce::String(total) + ": " + nomeArq)
                : ("Processing " + juce::String(i + 1) + " of " + juce::String(total) + ": " + nomeArq));
            notificarProgresso(p, msg);

            juce::String nomeBase = item.second.getFileNameWithoutExtension();
            juce::File arquivoDestino = resolverColisaoArquivo(pastaDestinoEfetiva, nomeBase, "_w", "jpg");

            bool ok = BatchWatermarkDialog::aplicarMarcaDaguaEmArquivo(item.second, arquivoDestino, params_.configWatermark);
            if (ok && arquivoDestino.existsAsFile() && arquivoDestino.getSize() > 0) {
                exportados++;
                arquivosExportados.push_back(arquivoDestino);
            } else {
                erros.add(nomeArq + ": " + (isPt ? juce::String::fromUTF8("falha ao processar ou gravar foto") : "failed to process or save photo"));
            }
        }

        juce::File resultadoFinal = params_.pastaDestino;

        if (params_.exportarZip && exportados > 0 && !threadShouldExit()) {
            notificarProgresso(0.95, (isPt ? juce::String::fromUTF8("Finalizando arquivo ZIP...") : "Finalizing ZIP package..."));

            juce::String dataHora = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
            juce::String nomeBaseZip = "Watermark_" + (params_.nomeProjeto.isNotEmpty() ? params_.nomeProjeto + "_" : "") + dataHora;
            juce::File arquivoZip = resolverColisaoArquivo(params_.pastaDestino, nomeBaseZip, "", "zip");

            juce::ZipFile::Builder builder;
            for (const auto& f : arquivosExportados) {
                builder.addFile(f, 0, f.getFileName());
            }

            arquivoZip.deleteFile();
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(arquivoZip.createOutputStream())) {
                builder.writeToStream(*stream, nullptr);
                resultadoFinal = arquivoZip;
            } else {
                erros.add(isPt ? juce::String::fromUTF8("Falha ao gravar arquivo ZIP.") : "Failed to write ZIP file.");
            }

            if (pastaTemp.isDirectory()) {
                pastaTemp.deleteRecursively();
            }
        }

        bool sucesso = (exportados > 0 && !threadShouldExit());

        juce::MessageManager::callAsync([this, sucesso, exportados, resultadoFinal, erros]() {
            if (onConcluido_) {
                onConcluido_(sucesso, exportados, resultadoFinal, erros);
            }
        });
    }

private:
    void notificarProgresso(double valor, const juce::String& msg) {
        juce::MessageManager::callAsync([this, valor, msg]() {
            if (onProgresso_) {
                onProgresso_(valor, msg);
            }
        });
    }

    Params params_;
    std::function<void(double, const juce::String&)> onProgresso_;
    std::function<void(bool, int, const juce::File&, const juce::StringArray&)> onConcluido_;
};

// ==============================================================================
// BatchWatermarkDialog
// ==============================================================================

BatchWatermarkDialog::BatchWatermarkDialog(ProjetoAberto* projeto)
    : projeto_(projeto) {
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (projeto_) {
        cfg_ = projeto_->obterConfiguracaoWatermark();
        pastaDestinoSelecionada_ = projeto_->projeto().pasta().getChildFile("Export_Watermark");
    }
    if (!pastaDestinoSelecionada_.isDirectory() && !pastaDestinoSelecionada_.exists()) {
        pastaDestinoSelecionada_ = juce::File::getSpecialLocation(juce::File::userPicturesDirectory);
    }

    criarImagensDummy();

    // Load existing logo if configured
    if (cfg_.caminhoLogo.isNotEmpty()) {
        juce::File lf(cfg_.caminhoLogo);
        if (lf.existsAsFile()) {
            carregarLogo(lf);
        }
    }

    // Header Labels
    lblTitulo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("EXPORTAR COM MARCA D'ÁGUA (W)") : "EXPORT WATERMARKED (W)");
    lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo + 2.0f, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    lblSubtitulo_ = std::make_unique<juce::Label>(
        "", isPt ? juce::String::fromUTF8("Configure a marca d'água, escolha a pasta de destino e exporte as fotos marcadas.")
                 : "Configure watermark settings, choose destination folder, and export marked photos.");
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo - 1.0f)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblSubtitulo_);

    // Orientation toggle buttons
    btnOrientacaoH_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Horizontal (3:2)") : "Landscape (3:2)");
    btnOrientacaoH_->setClickingTogglesState(false);
    btnOrientacaoH_->onClick = [this] {
        orientacaoAtual_ = OrientacaoPreview::Horizontal;
        atualizarControlesParaOrientacao();
        if (previewCanvas_) previewCanvas_->repaint();
        repaint();
    };
    addAndMakeVisible(*btnOrientacaoH_);

    btnOrientacaoV_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Vertical (2:3)") : "Portrait (2:3)");
    btnOrientacaoV_->setClickingTogglesState(false);
    btnOrientacaoV_->onClick = [this] {
        orientacaoAtual_ = OrientacaoPreview::Vertical;
        atualizarControlesParaOrientacao();
        if (previewCanvas_) previewCanvas_->repaint();
        repaint();
    };
    addAndMakeVisible(*btnOrientacaoV_);

    // Section 1: Logo
    lblSecaoLogo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("ARQUIVO DE LOGO") : "LOGO FILE");
    lblSecaoLogo_->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    lblSecaoLogo_->setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(*lblSecaoLogo_);

    btnEscolherLogo_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Escolher Logo (PNG / JPG)...") : "Choose Logo (PNG / JPG)...");
    btnEscolherLogo_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnEscolherLogo_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnEscolherLogo_->onClick = [this] {
        bool pt = (matriz::i18n::localeAtivo() == "pt_BR");
        auto chooser = std::make_shared<juce::FileChooser>(
            pt ? juce::String::fromUTF8("Selecione a Imagem do Logo") : "Select Logo Image",
            juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
            "*.png;*.jpg;*.jpeg;*.webp");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file.existsAsFile()) {
                carregarLogo(file);
                salvarConfiguracaoAtual();
            }
        });
    };
    addAndMakeVisible(*btnEscolherLogo_);

    lblLogoInfo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("(Nenhum logo selecionado)") : "(No logo selected)");
    lblLogoInfo_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblLogoInfo_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblLogoInfo_);

    // Section 2: Watermark Settings
    lblSecaoAjustes_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("AJUSTES DE MARCA D'ÁGUA") : "WATERMARK SETTINGS");
    lblSecaoAjustes_->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    lblSecaoAjustes_->setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(*lblSecaoAjustes_);

    // Opacity
    lblOpacidade_ = std::make_unique<juce::Label>("", isPt ? "Opacidade:" : "Opacity:");
    lblOpacidade_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblOpacidade_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblOpacidade_);

    sldOpacidade_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    sldOpacidade_->setRange(0.0, 100.0, 1.0);
    sldOpacidade_->setValue(cfg_.opacidade * 100.0, juce::dontSendNotification);
    sldOpacidade_->setTextValueSuffix("%");
    sldOpacidade_->onValueChange = [this] {
        cfg_.opacidade = (float)(sldOpacidade_->getValue() / 100.0);
        salvarConfiguracaoAtual();
        if (previewCanvas_) previewCanvas_->repaint();
    };
    addAndMakeVisible(*sldOpacidade_);

    // Scale
    lblEscala_ = std::make_unique<juce::Label>("", isPt ? "Tamanho (Escala):" : "Size (Scale):");
    lblEscala_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblEscala_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblEscala_);

    sldEscala_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    sldEscala_->setRange(5.0, 80.0, 1.0);
    sldEscala_->setValue(cfg_.escala * 100.0, juce::dontSendNotification);
    sldEscala_->setTextValueSuffix("%");
    sldEscala_->onValueChange = [this] {
        cfg_.escala = (float)(sldEscala_->getValue() / 100.0);
        salvarConfiguracaoAtual();
        if (previewCanvas_) previewCanvas_->repaint();
    };
    addAndMakeVisible(*sldEscala_);

    // Margin
    lblMargem_ = std::make_unique<juce::Label>("", isPt ? "Margem da Borda:" : "Border Margin:");
    lblMargem_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblMargem_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblMargem_);

    sldMargem_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    sldMargem_->setRange(0.0, 120.0, 1.0);
    sldMargem_->setValue(cfg_.margem, juce::dontSendNotification);
    sldMargem_->setTextValueSuffix(" px");
    sldMargem_->onValueChange = [this] {
        cfg_.margem = (float)sldMargem_->getValue();
        salvarConfiguracaoAtual();
        if (previewCanvas_) previewCanvas_->repaint();
    };
    addAndMakeVisible(*sldMargem_);

    // Position preset
    lblPosicao_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Posição:") : "Position:");
    lblPosicao_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblPosicao_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblPosicao_);

    cboPosicao_ = std::make_unique<juce::ComboBox>();
    cboPosicao_->addItem(isPt ? "Inferior Direito" : "Bottom Right", 1);
    cboPosicao_->addItem(isPt ? "Inferior Esquerdo" : "Bottom Left", 2);
    cboPosicao_->addItem(isPt ? "Superior Direito" : "Top Right", 3);
    cboPosicao_->addItem(isPt ? "Superior Esquerdo" : "Top Left", 4);
    cboPosicao_->addItem(isPt ? "Centro" : "Center", 5);
    cboPosicao_->addItem(isPt ? "Superior Centro" : "Top Center", 6);
    cboPosicao_->addItem(isPt ? "Inferior Centro" : "Bottom Center", 7);
    cboPosicao_->addItem(isPt ? "Personalizado (Arrastar na Foto)" : "Custom (Drag on Photo)", 8);

    cboPosicao_->onChange = [this] {
        int id = cboPosicao_->getSelectedId();
        if (orientacaoAtual_ == OrientacaoPreview::Horizontal) {
            cfg_.posicaoIdH = id;
        } else {
            cfg_.posicaoIdV = id;
        }
        salvarConfiguracaoAtual();
        if (previewCanvas_) previewCanvas_->repaint();
    };
    addAndMakeVisible(*cboPosicao_);

    lblDicaArrastar_ = std::make_unique<juce::Label>(
        "", isPt ? juce::String::fromUTF8("💡 Arraste o logo diretamente na foto para posicionar.")
                 : "💡 Drag the logo directly on the photo to position freely.");
    lblDicaArrastar_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblDicaArrastar_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblDicaArrastar_);

    // Section 3: Destination & Options
    lblSecaoDestino_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("DESTINO E PACOTE") : "DESTINATION & PACKAGE");
    lblSecaoDestino_->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    lblSecaoDestino_->setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(*lblSecaoDestino_);

    lblCaminhoDestino_ = std::make_unique<juce::Label>("", pastaDestinoSelecionada_.getFullPathName());
    lblCaminhoDestino_->setFont(juce::Font(juce::FontOptions(11.5f)));
    lblCaminhoDestino_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblCaminhoDestino_);

    btnEscolherPasta_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Escolher Pasta...") : "Choose Folder...");
    btnEscolherPasta_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnEscolherPasta_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnEscolherPasta_->onClick = [this] { escolherPastaDestino(); };
    addAndMakeVisible(*btnEscolherPasta_);

    chkExportarZip_ = std::make_unique<juce::ToggleButton>(isPt ? juce::String::fromUTF8("Exportar como arquivo ZIP") : "Export as ZIP package");
    chkExportarZip_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    addAndMakeVisible(*chkExportarZip_);

    lblItensMarcados_ = std::make_unique<juce::Label>();
    lblItensMarcados_->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblItensMarcados_->setColour(juce::Label::textColourId, juce::Colour(0xffffcc00));
    auto idsMarcados = projeto_ ? projeto_->idsMarcados(ProjetoAberto::TipoMarcacao::Watermark) : std::vector<std::string>();
    lblItensMarcados_->setText(isPt ? (juce::String((int)idsMarcados.size()) + " foto(s) marcada(s) com 'W'")
                                    : (juce::String((int)idsMarcados.size()) + " photo(s) marked with 'W'"),
                               juce::dontSendNotification);
    addAndMakeVisible(*lblItensMarcados_);

    // Preview Canvas
    previewCanvas_ = std::make_unique<PreviewCanvas>(*this);
    addAndMakeVisible(*previewCanvas_);

    // Progress Controls
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    barraProgresso_->setVisible(false);
    addAndMakeVisible(*barraProgresso_);

    lblStatusProgresso_ = std::make_unique<juce::Label>();
    lblStatusProgresso_->setFont(juce::Font(juce::FontOptions(11.5f)));
    lblStatusProgresso_->setColour(juce::Label::textColourId, tk.textoPrimario);
    lblStatusProgresso_->setVisible(false);
    addAndMakeVisible(*lblStatusProgresso_);

    // Bottom Action Buttons
    btnCancelar_ = std::make_unique<juce::TextButton>(isPt ? "Cancelar" : "Cancel");
    btnCancelar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnCancelar_->onClick = [this] {
        if (exportando_) cancelarExportacao();
        else fecharDialogo();
    };
    addAndMakeVisible(*btnCancelar_);

    btnExportar_ = std::make_unique<juce::TextButton>(isPt ? "EXPORTAR" : "EXPORT");
    btnExportar_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnExportar_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnExportar_->setEnabled(!idsMarcados.empty());
    btnExportar_->onClick = [this] { iniciarExportacao(); };
    addAndMakeVisible(*btnExportar_);

    atualizarControlesParaOrientacao();
}

BatchWatermarkDialog::~BatchWatermarkDialog() {
    if (threadExportacao_) {
        threadExportacao_->stopThread(2000);
    }
    salvarConfiguracaoAtual();
}

void BatchWatermarkDialog::fecharDialogo() {
    salvarConfiguracaoAtual();
    if (aoFechar) aoFechar();
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
    }
}

bool BatchWatermarkDialog::keyPressed(const juce::KeyPress& key) {
    if (key.getKeyCode() == juce::KeyPress::escapeKey) {
        if (exportando_) cancelarExportacao();
        else fecharDialogo();
        return true;
    }
    return false;
}

void BatchWatermarkDialog::escolherPastaDestino() {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    auto chooser = std::make_shared<juce::FileChooser>(
        isPt ? juce::String::fromUTF8("Escolha a Pasta de Destino") : "Choose Destination Folder",
        pastaDestinoSelecionada_.isDirectory() ? pastaDestinoSelecionada_ : juce::File::getSpecialLocation(juce::File::userPicturesDirectory));

    juce::Component::SafePointer<BatchWatermarkDialog> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [safeThis, chooser](const juce::FileChooser& fc) {
                             if (!safeThis) return;
                             juce::File d = fc.getResult();
                             if (d.isDirectory()) {
                                 safeThis->pastaDestinoSelecionada_ = d;
                                 safeThis->lblCaminhoDestino_->setText(d.getFullPathName(), juce::dontSendNotification);
                             }
                         });
}

void BatchWatermarkDialog::salvarConfiguracaoAtual() {
    if (projeto_) {
        projeto_->salvarConfiguracaoWatermark(cfg_);
    }
}

void BatchWatermarkDialog::iniciarExportacao() {
    if (exportando_) return;

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    if (!pastaDestinoSelecionada_.isDirectory()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Exportação") : "Export",
            isPt ? juce::String::fromUTF8("Selecione uma pasta de destino válida.") : "Please select a valid destination folder.");
        return;
    }

    if (!projeto_) return;
    salvarConfiguracaoAtual();

    auto ids = projeto_->idsMarcados(ProjetoAberto::TipoMarcacao::Watermark);
    if (ids.empty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Exportação") : "Export",
            isPt ? juce::String::fromUTF8("Nenhuma foto marcada com 'W' para exportação.") : "No photos marked with 'W' for export.");
        return;
    }

    if (!cfg_.valida()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Exportação") : "Export",
            isPt ? juce::String::fromUTF8("Por favor, selecione um arquivo de logo válido antes de exportar.")
                 : "Please select a valid logo image before exporting.");
        return;
    }

    ExportWatermarkThread::Params params;
    params.pastaDestino = pastaDestinoSelecionada_;
    params.exportarZip = chkExportarZip_->getToggleState();
    params.nomeProjeto = projeto_->projeto().nome();
    params.configWatermark = cfg_;

    for (const auto& id : ids) {
        auto arqInfo = projeto_->arquivoPrincipal(id);
        if (arqInfo) {
            juce::File f(arqInfo->caminhoAbsoluto);
            if (!f.existsAsFile()) {
                auto resolvido = projeto_->resolverArquivoComMemoria(arqInfo->id);
                if (resolvido.has_value() && resolvido->existsAsFile()) {
                    f = *resolvido;
                }
            }
            if (f.existsAsFile()) {
                params.itens.push_back({ id, f });
            }
        }
    }

    if (params.itens.empty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Exportação") : "Export",
            isPt ? juce::String::fromUTF8("Nenhum arquivo físico encontrado para as fotos marcadas.")
                 : "No physical files found for marked photos.");
        return;
    }

    exportando_ = true;
    btnExportar_->setEnabled(false);
    btnEscolherLogo_->setEnabled(false);
    btnOrientacaoH_->setEnabled(false);
    btnOrientacaoV_->setEnabled(false);
    sldOpacidade_->setEnabled(false);
    sldEscala_->setEnabled(false);
    sldMargem_->setEnabled(false);
    cboPosicao_->setEnabled(false);
    btnEscolherPasta_->setEnabled(false);
    chkExportarZip_->setEnabled(false);

    barraProgresso_->setVisible(true);
    lblStatusProgresso_->setVisible(true);
    progressoValor_ = 0.0;
    lblStatusProgresso_->setText(isPt ? juce::String::fromUTF8("Iniciando exportação...") : "Starting export...", juce::dontSendNotification);

    juce::Component::SafePointer<BatchWatermarkDialog> safeThis(this);

    threadExportacao_ = std::make_unique<ExportWatermarkThread>(
        std::move(params),
        [safeThis](double p, const juce::String& msg) {
            if (!safeThis) return;
            safeThis->progressoValor_ = p;
            safeThis->lblStatusProgresso_->setText(msg, juce::dontSendNotification);
        },
        [safeThis, isPt](bool sucesso, int totalExportados, const juce::File& resultado, const juce::StringArray& erros) {
            if (!safeThis) return;
            safeThis->exportando_ = false;
            safeThis->btnExportar_->setEnabled(true);
            safeThis->btnEscolherLogo_->setEnabled(true);
            safeThis->btnOrientacaoH_->setEnabled(true);
            safeThis->btnOrientacaoV_->setEnabled(true);
            safeThis->sldOpacidade_->setEnabled(true);
            safeThis->sldEscala_->setEnabled(true);
            safeThis->sldMargem_->setEnabled(true);
            safeThis->cboPosicao_->setEnabled(true);
            safeThis->btnEscolherPasta_->setEnabled(true);
            safeThis->chkExportarZip_->setEnabled(true);
            safeThis->barraProgresso_->setVisible(false);
            safeThis->lblStatusProgresso_->setVisible(false);

            if (sucesso) {
                juce::String msg;
                if (resultado.existsAsFile()) {
                    msg = (isPt ? juce::String::fromUTF8("{n} foto(s) exportada(s) e empacotada(s) em ZIP com sucesso:\n{caminho}")
                                : "{n} photo(s) exported and packaged to ZIP successfully:\n{caminho}")
                        .replace("{n}", juce::String(totalExportados))
                        .replace("{caminho}", resultado.getFullPathName());
                } else {
                    msg = (isPt ? juce::String::fromUTF8("{n} foto(s) exportada(s) com marca d'água com sucesso na pasta:\n{pasta}")
                                : "{n} photo(s) exported with watermark successfully to folder:\n{pasta}")
                        .replace("{n}", juce::String(totalExportados))
                        .replace("{pasta}", resultado.getFullPathName());
                }

                if (!erros.isEmpty()) {
                    msg += "\n\nErros:\n" + erros.joinIntoString("\n");
                }

                auto* alert = new juce::AlertWindow(
                    isPt ? juce::String::fromUTF8("Exportação Concluída") : "Export Completed",
                    msg,
                    juce::AlertWindow::InfoIcon);

                alert->addButton(isPt ? juce::String::fromUTF8("Revelar no Finder") : "Reveal in Finder", 1);
                alert->addButton(isPt ? "Fechar" : "Close", 0);

                alert->enterModalState(true, juce::ModalCallbackFunction::create([resultado, safeThis](int result) {
                    if (result == 1) {
                        resultado.revealToUser();
                    }
                    if (safeThis) {
                        safeThis->fecharDialogo();
                    }
                }), true);
            } else if (!erros.isEmpty()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    isPt ? juce::String::fromUTF8("Exportação") : "Export",
                    erros.joinIntoString("\n"));
            }
        });

    threadExportacao_->startThread();
}

void BatchWatermarkDialog::cancelarExportacao() {
    if (!exportando_ || !threadExportacao_) return;
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    lblStatusProgresso_->setText(isPt ? juce::String::fromUTF8("Cancelando exportação...") : "Cancelling export...", juce::dontSendNotification);
    threadExportacao_->signalThreadShouldExit();
}

void BatchWatermarkDialog::criarImagensDummy() {
    // 1. Horizontal Dummy (1200 x 800 - 3:2)
    {
        dummyLandscape_ = juce::Image(juce::Image::RGB, 1200, 800, false);
        juce::Graphics g(dummyLandscape_);

        juce::ColourGradient grad(
            juce::Colour(0xff2d3139), 0.0f, 0.0f,
            juce::Colour(0xff181a1f), 1200.0f, 800.0f, false);
        g.setGradientFill(grad);
        g.fillAll();

        juce::Path p;
        p.startNewSubPath(0, 800);
        p.lineTo(260, 480);
        p.lineTo(540, 620);
        p.lineTo(820, 390);
        p.lineTo(1200, 680);
        p.lineTo(1200, 800);
        p.closeSubPath();

        g.setColour(juce::Colour(0x18ffffff));
        g.fillPath(p);

        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawVerticalLine(400, 0.0f, 800.0f);
        g.drawVerticalLine(800, 0.0f, 800.0f);
        g.drawHorizontalLine(266, 0.0f, 1200.0f);
        g.drawHorizontalLine(533, 0.0f, 1200.0f);

        juce::Rectangle<int> badge(450, 370, 300, 60);
        g.setColour(juce::Colour(0x66000000));
        g.fillRoundedRectangle(badge.toFloat(), 6.0f);
        g.setColour(juce::Colour(0x44ffffff));
        g.drawRoundedRectangle(badge.toFloat(), 6.0f, 1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.drawText("PREVIEW HORIZONTAL (3:2)", badge, juce::Justification::centred, true);
    }

    // 2. Vertical Dummy (800 x 1200 - 2:3)
    {
        dummyPortrait_ = juce::Image(juce::Image::RGB, 800, 1200, false);
        juce::Graphics g(dummyPortrait_);

        juce::ColourGradient grad(
            juce::Colour(0xff2d3139), 0.0f, 0.0f,
            juce::Colour(0xff181a1f), 800.0f, 1200.0f, false);
        g.setGradientFill(grad);
        g.fillAll();

        g.setColour(juce::Colour(0x15ffffff));
        g.fillEllipse(300.0f, 320.0f, 200.0f, 250.0f);
        juce::Path shoulders;
        shoulders.startNewSubPath(160, 1200);
        shoulders.cubicTo(200, 750, 300, 620, 400, 620);
        shoulders.cubicTo(500, 620, 600, 750, 640, 1200);
        shoulders.closeSubPath();
        g.fillPath(shoulders);

        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawVerticalLine(266, 0.0f, 1200.0f);
        g.drawVerticalLine(533, 0.0f, 1200.0f);
        g.drawHorizontalLine(400, 0.0f, 800.0f);
        g.drawHorizontalLine(800, 0.0f, 800.0f);

        juce::Rectangle<int> badge(250, 570, 300, 60);
        g.setColour(juce::Colour(0x66000000));
        g.fillRoundedRectangle(badge.toFloat(), 6.0f);
        g.setColour(juce::Colour(0x44ffffff));
        g.drawRoundedRectangle(badge.toFloat(), 6.0f, 1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.drawText("PREVIEW VERTICAL (2:3)", badge, juce::Justification::centred, true);
    }
}

void BatchWatermarkDialog::carregarLogo(const juce::File& file) {
    logoImage_ = juce::ImageFileFormat::loadFrom(file);
    if (logoImage_.isValid()) {
        cfg_.caminhoLogo = file.getFullPathName();
        if (lblLogoInfo_) {
            lblLogoInfo_->setText(file.getFileName() + " (" + juce::String(logoImage_.getWidth()) + "x" + juce::String(logoImage_.getHeight()) + " px)",
                                  juce::dontSendNotification);
            lblLogoInfo_->setColour(juce::Label::textColourId, tema().textoPrimario);
        }
    } else {
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
        if (lblLogoInfo_) {
            lblLogoInfo_->setText(isPt ? juce::String::fromUTF8("Erro ao carregar formato de imagem.") : "Failed to load image format.",
                                  juce::dontSendNotification);
            lblLogoInfo_->setColour(juce::Label::textColourId, juce::Colours::red);
        }
    }
    if (previewCanvas_) previewCanvas_->repaint();
}

void BatchWatermarkDialog::atualizarControlesParaOrientacao() {
    const auto& tk = tema();
    bool isH = (orientacaoAtual_ == OrientacaoPreview::Horizontal);

    if (btnOrientacaoH_) {
        btnOrientacaoH_->setColour(juce::TextButton::buttonColourId, isH ? tk.acento : tk.painelAlt);
        btnOrientacaoH_->setColour(juce::TextButton::textColourOffId, isH ? tk.textoSobreAcento : tk.textoPrimario);
    }
    if (btnOrientacaoV_) {
        btnOrientacaoV_->setColour(juce::TextButton::buttonColourId, !isH ? tk.acento : tk.painelAlt);
        btnOrientacaoV_->setColour(juce::TextButton::textColourOffId, !isH ? tk.textoSobreAcento : tk.textoPrimario);
    }

    int posId = isH ? cfg_.posicaoIdH : cfg_.posicaoIdV;
    if (cboPosicao_) {
        cboPosicao_->setSelectedId(posId, juce::dontSendNotification);
    }
}

bool BatchWatermarkDialog::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& p : files) {
        juce::String ext = juce::File(p).getFileExtension().toLowerCase();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp") return true;
    }
    return false;
}

void BatchWatermarkDialog::filesDropped(const juce::StringArray& files, int, int) {
    for (const auto& p : files) {
        juce::File f(p);
        juce::String ext = f.getFileExtension().toLowerCase();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp") {
            carregarLogo(f);
            salvarConfiguracaoAtual();
            break;
        }
    }
}

juce::Rectangle<float> BatchWatermarkDialog::calcularPosicaoLogo(
    float imgW, float imgH, float logoW, float logoH, const ConfiguracaoWatermark& cfg) {
    bool isLandscape = (imgW >= imgH);
    int posId = isLandscape ? cfg.posicaoIdH : cfg.posicaoIdV;
    float customX = isLandscape ? cfg.customPosX_H : cfg.customPosX_V;
    float customY = isLandscape ? cfg.customPosY_H : cfg.customPosY_V;

    float scalePercent = cfg.escala;
    float margin = cfg.margem;

    float targetLogoW = imgW * scalePercent;
    float targetLogoH = targetLogoW * (logoH / logoW);

    if (targetLogoH > imgH * 0.9f) {
        targetLogoH = imgH * 0.9f;
        targetLogoW = targetLogoH * (logoW / logoH);
    }

    float posX = 0.0f;
    float posY = 0.0f;

    switch (posId) {
        case 1: // Bottom Right
            posX = imgW - targetLogoW - margin;
            posY = imgH - targetLogoH - margin;
            break;
        case 2: // Bottom Left
            posX = margin;
            posY = imgH - targetLogoH - margin;
            break;
        case 3: // Top Right
            posX = imgW - targetLogoW - margin;
            posY = margin;
            break;
        case 4: // Top Left
            posX = margin;
            posY = margin;
            break;
        case 5: // Center
            posX = (imgW - targetLogoW) * 0.5f;
            posY = (imgH - targetLogoH) * 0.5f;
            break;
        case 6: // Top Center
            posX = (imgW - targetLogoW) * 0.5f;
            posY = margin;
            break;
        case 7: // Bottom Center
            posX = (imgW - targetLogoW) * 0.5f;
            posY = imgH - targetLogoH - margin;
            break;
        case 8: // Custom
        default:
            posX = (imgW * customX) - (targetLogoW * 0.5f);
            posY = (imgH * customY) - (targetLogoH * 0.5f);
            break;
    }

    return { posX, posY, targetLogoW, targetLogoH };
}

bool BatchWatermarkDialog::aplicarMarcaDaguaEmImagem(juce::Image& img, const ConfiguracaoWatermark& cfg) {
    if (!img.isValid() || !cfg.valida()) return false;
    juce::Image logo = juce::ImageFileFormat::loadFrom(juce::File(cfg.caminhoLogo));
    if (!logo.isValid()) return false;

    juce::Graphics g(img);
    auto rect = calcularPosicaoLogo((float)img.getWidth(), (float)img.getHeight(),
                                   (float)logo.getWidth(), (float)logo.getHeight(), cfg);
    g.setOpacity(juce::jlimit(0.0f, 1.0f, cfg.opacidade));
    g.drawImage(logo, rect);
    return true;
}

bool BatchWatermarkDialog::aplicarMarcaDaguaEmBuffer(matriz::imagem::ImagemBuffer& buf, const ConfiguracaoWatermark& cfg) {
    if (!buf.valido() || !cfg.valida()) return false;
    juce::Image jImg(juce::Image::RGB, buf.largura, buf.altura, false);
    for (int y = 0; y < buf.altura; ++y) {
        for (int x = 0; x < buf.largura; ++x) {
            const uint8_t* p = buf.pixel(x, y);
            jImg.setPixelAt(x, y, juce::Colour(p[0], p[1], p[2]));
        }
    }
    if (!aplicarMarcaDaguaEmImagem(jImg, cfg)) return false;

    for (int y = 0; y < buf.altura; ++y) {
        for (int x = 0; x < buf.largura; ++x) {
            auto c = jImg.getPixelAt(x, y);
            buf.definirPixel(x, y, c.getRed(), c.getGreen(), c.getBlue(), 255);
        }
    }
    return true;
}

bool BatchWatermarkDialog::aplicarMarcaDaguaEmArquivo(const juce::File& srcFile, const juce::File& dstFile, const ConfiguracaoWatermark& cfg) {
    if (!srcFile.existsAsFile() || !cfg.valida()) return false;

    // 1. Carrega imagem (suporta JPEG, PNG, TIFF e RAW nativo via ImageIO / CoreGraphics)
    juce::Image img = juce::ImageFileFormat::loadFrom(srcFile);
    if (!img.isValid()) {
        auto res = matriz::imagem::lerImagem(srcFile);
        if (res.sucesso && res.buffer.valido()) {
            img = juce::Image(juce::Image::RGB, res.buffer.largura, res.buffer.altura, false);
            for (int y = 0; y < res.buffer.altura; ++y) {
                for (int x = 0; x < res.buffer.largura; ++x) {
                    const uint8_t* p = res.buffer.pixel(x, y);
                    img.setPixelAt(x, y, juce::Colour(p[0], p[1], p[2]));
                }
            }
        }
    }
    if (!img.isValid()) return false;

    // 2. Correção de orientação EXIF
    int orient = 1;
    try {
        juce::FileInputStream fis(srcFile);
        if (fis.openedOk()) {
            juce::MemoryBlock mb;
            fis.readIntoMemoryBlock(mb, 65536);
            orient = matriz::imagem::orientacaoExif(static_cast<const uint8_t*>(mb.getData()), mb.getSize());
        }
    } catch (...) {}

    if (orient > 1) {
        if (orient == 6) { // 90 CW
            juce::Image dst(img.getFormat(), img.getHeight(), img.getWidth(), true);
            juce::Graphics g(dst);
            g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi, 0, 0)
                                                  .translated(static_cast<float>(img.getHeight()), 0));
            g.drawImageAt(img, 0, 0);
            img = dst;
        } else if (orient == 8) { // 270 CW
            juce::Image dst(img.getFormat(), img.getHeight(), img.getWidth(), true);
            juce::Graphics g(dst);
            g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, 0, 0)
                                                  .translated(0, static_cast<float>(img.getWidth())));
            g.drawImageAt(img, 0, 0);
            img = dst;
        } else if (orient == 3) { // 180
            juce::Image dst(img.getFormat(), img.getWidth(), img.getHeight(), true);
            juce::Graphics g(dst);
            g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi,
                                                           img.getWidth() * 0.5f, img.getHeight() * 0.5f));
            g.drawImageAt(img, 0, 0);
            img = dst;
        }
    }

    // 3. Aplica marca d'água de acordo com a proporção horizontal / vertical
    if (!aplicarMarcaDaguaEmImagem(img, cfg)) return false;

    // 4. Grava sempre como JPEG qualidade 90 com sRGB
    if (dstFile.existsAsFile()) dstFile.deleteFile();
    dstFile.getParentDirectory().createDirectory();

    juce::FileOutputStream stream(dstFile);
    if (!stream.openedOk()) return false;

    juce::JPEGImageFormat jpgFmt;
    jpgFmt.setQuality(0.90f);
    return jpgFmt.writeImageToStream(img, stream);
}

juce::File BatchWatermarkDialog::resolverColisaoArquivo(const juce::File& pasta,
                                                       const juce::String& nomeBase,
                                                       const juce::String& sufixo,
                                                       const juce::String& extensao) {
    juce::String nomeArquivo = nomeBase + sufixo + "." + extensao;
    juce::File cand = pasta.getChildFile(nomeArquivo);
    if (!cand.exists()) return cand;

    int counter = 2;
    while (true) {
        juce::String nomeComNum = nomeBase + sufixo + "_" + juce::String(counter) + "." + extensao;
        juce::File candNum = pasta.getChildFile(nomeComNum);
        if (!candNum.exists()) return candNum;
        counter++;
    }
}

void BatchWatermarkDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Separator line above bottom action buttons
    auto bottomBar = getLocalBounds().removeFromBottom(56);
    g.setColour(tk.borda);
    g.drawLine(0.0f, (float)bottomBar.getY(), (float)getWidth(), (float)bottomBar.getY(), 1.0f);
}

void BatchWatermarkDialog::resized() {
    auto area = getLocalBounds().reduced(20, 16);

    // Top: Header (54px)
    auto header = area.removeFromTop(50);
    lblTitulo_->setBounds(header.removeFromTop(28));
    lblSubtitulo_->setBounds(header.removeFromTop(20));

    area.removeFromTop(10);

    // Bottom Action Bar (50px)
    auto bottomBar = area.removeFromBottom(46);
    btnCancelar_->setBounds(bottomBar.removeFromRight(110).withSizeKeepingCentre(100, 34));
    bottomBar.removeFromRight(10);
    btnExportar_->setBounds(bottomBar.removeFromRight(150).withSizeKeepingCentre(140, 34));

    if (barraProgresso_->isVisible()) {
        auto progArea = bottomBar.removeFromLeft(480);
        lblStatusProgresso_->setBounds(progArea.removeFromTop(20));
        progArea.removeFromTop(4);
        barraProgresso_->setBounds(progArea.removeFromTop(18));
    }

    area.removeFromBottom(12);

    // Split area into Left Column (380px) and Right Column (Preview Canvas)
    auto leftArea = area.removeFromLeft(380);
    area.removeFromLeft(20);
    auto rightArea = area;

    // Left Column:
    // 1. Orientation toggles (32px)
    auto barOrientacao = leftArea.removeFromTop(32);
    btnOrientacaoH_->setBounds(barOrientacao.removeFromLeft(180));
    barOrientacao.removeFromLeft(10);
    btnOrientacaoV_->setBounds(barOrientacao);

    leftArea.removeFromTop(10);

    // 2. Logo Section
    lblSecaoLogo_->setBounds(leftArea.removeFromTop(20));
    leftArea.removeFromTop(4);
    btnEscolherLogo_->setBounds(leftArea.removeFromTop(30));
    leftArea.removeFromTop(4);
    lblLogoInfo_->setBounds(leftArea.removeFromTop(18));

    leftArea.removeFromTop(10);

    // 3. Watermark Settings Section
    lblSecaoAjustes_->setBounds(leftArea.removeFromTop(20));
    leftArea.removeFromTop(6);

    // Opacity row
    auto rowOp = leftArea.removeFromTop(26);
    lblOpacidade_->setBounds(rowOp.removeFromLeft(120));
    sldOpacidade_->setBounds(rowOp);

    leftArea.removeFromTop(4);

    // Scale row
    auto rowEsc = leftArea.removeFromTop(26);
    lblEscala_->setBounds(rowEsc.removeFromLeft(120));
    sldEscala_->setBounds(rowEsc);

    leftArea.removeFromTop(4);

    // Margin row
    auto rowMarg = leftArea.removeFromTop(26);
    lblMargem_->setBounds(rowMarg.removeFromLeft(120));
    sldMargem_->setBounds(rowMarg);

    leftArea.removeFromTop(4);

    // Position preset row
    auto rowPos = leftArea.removeFromTop(28);
    lblPosicao_->setBounds(rowPos.removeFromLeft(120));
    cboPosicao_->setBounds(rowPos);

    leftArea.removeFromTop(4);
    lblDicaArrastar_->setBounds(leftArea.removeFromTop(20));

    leftArea.removeFromTop(10);

    // 4. Destination & Options Section
    lblSecaoDestino_->setBounds(leftArea.removeFromTop(20));
    leftArea.removeFromTop(4);

    auto rowDest = leftArea.removeFromTop(28);
    btnEscolherPasta_->setBounds(rowDest.removeFromRight(130));
    rowDest.removeFromRight(8);
    lblCaminhoDestino_->setBounds(rowDest);

    leftArea.removeFromTop(6);
    chkExportarZip_->setBounds(leftArea.removeFromTop(24));

    leftArea.removeFromTop(6);
    lblItensMarcados_->setBounds(leftArea.removeFromTop(22));

    // Right Column: Preview Canvas
    previewCanvas_->setBounds(rightArea);
}

void BatchWatermarkDialog::lookAndFeelChanged() {
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (lblTitulo_) {
        lblTitulo_->setText(isPt ? juce::String::fromUTF8("EXPORTAR COM MARCA D'ÁGUA (W)") : "EXPORT WATERMARKED (W)", juce::dontSendNotification);
        lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (btnExportar_) {
        btnExportar_->setColour(juce::TextButton::buttonColourId, tk.acento);
        btnExportar_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    }
    atualizarControlesParaOrientacao();
    if (previewCanvas_) previewCanvas_->repaint();
    repaint();
}

void BatchWatermarkDialog::exibirModal(ProjetoAberto* projeto) {
    auto dlg = std::make_unique<BatchWatermarkDialog>(projeto);
    dlg->setSize(1020, 680);
    auto* rawDlg = dlg.get();

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dlg.release());
    opt.dialogTitle = isPt ? juce::String::fromUTF8("Exportar com Marca D'Água (W)") : "Export Watermarked (W)";
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;

    auto* win = opt.launchAsync();
    rawDlg->aoFechar = [win] {
        if (win) win->exitModalState(0);
    };
}

void BatchWatermarkDialog::exibirModal(std::function<std::vector<juce::File>()>) {
    exibirModal(static_cast<ProjetoAberto*>(nullptr));
}

} // namespace matriz::ui
