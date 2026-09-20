#include "BatchWatermarkDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "ProgressoGlobal.h"

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

    if (!owner_.sampleImage_.isValid()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
        g.drawText(isPt ? juce::String::fromUTF8("Nenhuma foto selecionada para pré-visualização.\nAdicione fotos ou carregue da grade.")
                        : juce::String::fromUTF8("No photo selected for preview.\nAdd photos or load from grid."),
                   getLocalBounds().reduced(20), juce::Justification::centred, true);
        return;
    }

    // Calculate aspect ratio fit of sample photo in preview canvas
    auto inner = bounds.reduced(10.0f);
    float imgW = (float)owner_.sampleImage_.getWidth();
    float imgH = (float)owner_.sampleImage_.getHeight();

    float scale = std::min(inner.getWidth() / imgW, inner.getHeight() / imgH);
    float drawW = imgW * scale;
    float drawH = imgH * scale;
    float drawX = inner.getX() + (inner.getWidth() - drawW) * 0.5f;
    float drawY = inner.getY() + (inner.getHeight() - drawH) * 0.5f;

    juce::Rectangle<float> imgDestRect(drawX, drawY, drawW, drawH);

    // Draw shadow / border behind photo
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(imgDestRect.translated(2.0f, 2.0f), 4.0f);

    // Draw sample image
    g.drawImage(owner_.sampleImage_, imgDestRect);
    g.setColour(tk.borda.withAlpha(0.7f));
    g.drawRoundedRectangle(imgDestRect, 4.0f, 1.0f);

    // Draw watermark logo overlay
    if (owner_.logoImage_.isValid()) {
        float logoW = (float)owner_.logoImage_.getWidth();
        float logoH = (float)owner_.logoImage_.getHeight();

        juce::Rectangle<float> logoFullRect = owner_.calcularPosicaoLogo(imgW, imgH, logoW, logoH);

        // Map logo position from full image coordinates to preview canvas coordinates
        float prevLogoX = drawX + (logoFullRect.getX() / imgW) * drawW;
        float prevLogoY = drawY + (logoFullRect.getY() / imgH) * drawH;
        float prevLogoW = (logoFullRect.getWidth() / imgW) * drawW;
        float prevLogoH = (logoFullRect.getHeight() / imgH) * drawH;

        juce::Rectangle<float> prevLogoRect(prevLogoX, prevLogoY, prevLogoW, prevLogoH);

        float opacity = (float)(owner_.sliderOpacity_ ? owner_.sliderOpacity_->getValue() / 100.0 : 0.8);
        g.setOpacity(opacity);
        g.drawImage(owner_.logoImage_, prevLogoRect);
        g.setOpacity(1.0f);

        // Highlight selection border around logo in preview
        g.setColour(tk.acento.withAlpha(0.7f));
        g.drawRoundedRectangle(prevLogoRect, 2.0f, 1.0f);
    }
}

void BatchWatermarkDialog::PreviewCanvas::mouseDown(const juce::MouseEvent& e) {
    if (!owner_.sampleImage_.isValid() || !owner_.logoImage_.isValid()) return;

    auto inner = getLocalBounds().toFloat().reduced(10.0f);
    float imgW = (float)owner_.sampleImage_.getWidth();
    float imgH = (float)owner_.sampleImage_.getHeight();

    float scale = std::min(inner.getWidth() / imgW, inner.getHeight() / imgH);
    float drawW = imgW * scale;
    float drawH = imgH * scale;
    float drawX = inner.getX() + (inner.getWidth() - drawW) * 0.5f;
    float drawY = inner.getY() + (inner.getHeight() - drawH) * 0.5f;

    float relX = (e.position.x - drawX) / drawW;
    float relY = (e.position.y - drawY) / drawH;

    relX = juce::jlimit(0.0f, 1.0f, relX);
    relY = juce::jlimit(0.0f, 1.0f, relY);

    owner_.customLogoPos_ = {relX, relY};
    if (owner_.comboPosition_) {
        owner_.comboPosition_->setSelectedId(8, juce::dontSendNotification); // Custom
    }
    repaint();
}

void BatchWatermarkDialog::PreviewCanvas::mouseDrag(const juce::MouseEvent& e) {
    mouseDown(e);
}

// ==============================================================================
// BatchWatermarkDialog
// ==============================================================================

BatchWatermarkDialog::BatchWatermarkDialog(std::function<std::vector<juce::File>()> obterFotosDoGrid)
    : juce::Thread("BatchWatermarkThread"),
      obterFotosDoGrid_(obterFotosDoGrid) {
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    // Header Labels
    lblTitulo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("MARCA D'ÁGUA EM LOTE") : juce::String::fromUTF8("IMAGE BATCH WATERMARK"));
    lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo + 2.0f, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    lblSubtitulo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Aplique logos e marcas d'água transparentes em lote com controle de opacidade, escala e posição.")
                                                           : juce::String::fromUTF8("Apply transparent logos and watermarks across photo batches with opacity, scale, and positioning controls."));
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo - 1.0f)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblSubtitulo_);

    // Left Column: Source Photos Controls
    btnFromGrid_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Carregar da Grade") : juce::String::fromUTF8("Load from Grid"));
    btnFromGrid_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnFromGrid_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnFromGrid_->onClick = [this] {
        if (obterFotosDoGrid_) {
            auto arquivos = obterFotosDoGrid_();
            juce::Array<juce::File> arr;
            for (const auto& f : arquivos) arr.add(f);
            addPhotosFromFiles(arr);
        }
    };
    addAndMakeVisible(*btnFromGrid_);

    btnAddPhotos_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Adicionar Fotos...") : juce::String::fromUTF8("Add Photos..."));
    btnAddPhotos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnAddPhotos_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnAddPhotos_->onClick = [this] {
        auto fc = std::make_shared<juce::FileChooser>(
            matriz::i18n::localeAtivo() == "pt_BR" ? juce::String::fromUTF8("Selecionar Fotos") : "Select Photos",
            juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
            "*.jpg;*.jpeg;*.png;*.webp;*.tiff;*.tif;*.bmp");
        fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                        [this, fc](const juce::FileChooser& chooser) {
                            auto results = chooser.getResults();
                            if (!results.isEmpty()) {
                                addPhotosFromFiles(results);
                            }
                        });
    };
    addAndMakeVisible(*btnAddPhotos_);

    btnClearPhotos_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Limpar") : juce::String::fromUTF8("Clear"));
    btnClearPhotos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnClearPhotos_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnClearPhotos_->onClick = [this] {
        photos_.clear();
        sampleImage_ = juce::Image();
        selectedPhotoIndex_ = -1;
        if (photoListBox_) photoListBox_->updateContent();
        if (lblPhotoCount_) lblPhotoCount_->setText(matriz::i18n::localeAtivo() == "pt_BR" ? juce::String::fromUTF8("0 fotos na lista") : "0 photos in list", juce::dontSendNotification);
        if (previewCanvas_) previewCanvas_->repaint();
    };
    addAndMakeVisible(*btnClearPhotos_);

    photoListBox_ = std::make_unique<juce::ListBox>("PhotoList", this);
    photoListBox_->setColour(juce::ListBox::backgroundColourId, tk.painel);
    photoListBox_->setColour(juce::ListBox::outlineColourId, tk.borda);
    photoListBox_->setRowHeight(38);
    addAndMakeVisible(*photoListBox_);

    lblPhotoCount_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("0 fotos na lista") : juce::String::fromUTF8("0 photos in list"));
    lblPhotoCount_->setFont(juce::Font(juce::FontOptions(11.5f)));
    lblPhotoCount_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblPhotoCount_);

    // Right Column: Watermark / Logo Controls
    btnChooseLogo_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Escolher Logo (PNG/JPG)...") : juce::String::fromUTF8("Choose Logo (PNG/JPG)..."));
    btnChooseLogo_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnChooseLogo_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnChooseLogo_->onClick = [this] {
        auto fc = std::make_shared<juce::FileChooser>(
            matriz::i18n::localeAtivo() == "pt_BR" ? juce::String::fromUTF8("Selecionar Arquivo da Marca d'Água / Logo") : "Select Watermark / Logo File",
            juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
            "*.png;*.jpg;*.jpeg;*.webp");
        fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                        [this, fc](const juce::FileChooser& chooser) {
                            auto result = chooser.getResult();
                            if (result.existsAsFile()) {
                                carregarLogo(result);
                            }
                        });
    };
    addAndMakeVisible(*btnChooseLogo_);

    lblLogoInfo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Nenhuma logo selecionada") : juce::String::fromUTF8("No logo selected"));
    lblLogoInfo_->setFont(juce::Font(juce::FontOptions(12.0f)));
    lblLogoInfo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblLogoInfo_);

    // Opacity
    lblOpacity_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Opacidade:") : juce::String::fromUTF8("Opacity:"));
    lblOpacity_->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblOpacity_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblOpacity_);

    sliderOpacity_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    sliderOpacity_->setRange(0.0, 100.0, 1.0);
    sliderOpacity_->setValue(80.0);
    sliderOpacity_->setTextValueSuffix("%");
    sliderOpacity_->setColour(juce::Slider::thumbColourId, tk.acento);
    sliderOpacity_->setColour(juce::Slider::trackColourId, tk.acento.withAlpha(0.6f));
    sliderOpacity_->onValueChange = [this] { if (previewCanvas_) previewCanvas_->repaint(); };
    addAndMakeVisible(*sliderOpacity_);

    // Scale
    lblScale_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Tamanho / Escala:") : juce::String::fromUTF8("Scale / Size:"));
    lblScale_->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblScale_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblScale_);

    sliderScale_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    sliderScale_->setRange(5.0, 100.0, 1.0);
    sliderScale_->setValue(22.0);
    sliderScale_->setTextValueSuffix("%");
    sliderScale_->setColour(juce::Slider::thumbColourId, tk.acento);
    sliderScale_->setColour(juce::Slider::trackColourId, tk.acento.withAlpha(0.6f));
    sliderScale_->onValueChange = [this] { if (previewCanvas_) previewCanvas_->repaint(); };
    addAndMakeVisible(*sliderScale_);

    // Position ComboBox
    lblPosition_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Posição:") : juce::String::fromUTF8("Position:"));
    lblPosition_->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblPosition_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblPosition_);

    comboPosition_ = std::make_unique<juce::ComboBox>();
    comboPosition_->addItem(isPt ? juce::String::fromUTF8("Inferior Direito (Padrão)") : juce::String::fromUTF8("Bottom Right (Default)"), 1);
    comboPosition_->addItem(isPt ? juce::String::fromUTF8("Inferior Esquerdo") : juce::String::fromUTF8("Bottom Left"), 2);
    comboPosition_->addItem(isPt ? juce::String::fromUTF8("Superior Direito") : juce::String::fromUTF8("Top Right"), 3);
    comboPosition_->addItem(isPt ? juce::String::fromUTF8("Superior Esquerdo") : juce::String::fromUTF8("Top Left"), 4);
    comboPosition_->addItem(isPt ? juce::String::fromUTF8("Centro") : juce::String::fromUTF8("Center"), 5);
    comboPosition_->addItem(isPt ? juce::String::fromUTF8("Superior Centro") : juce::String::fromUTF8("Top Center"), 6);
    comboPosition_->addItem(isPt ? juce::String::fromUTF8("Inferior Centro") : juce::String::fromUTF8("Bottom Center"), 7);
    comboPosition_->addItem(isPt ? juce::String::fromUTF8("Personalizado (Arrastar no Preview)") : juce::String::fromUTF8("Custom (Drag in Preview)"), 8);
    comboPosition_->setSelectedId(1, juce::dontSendNotification);
    comboPosition_->onChange = [this] { if (previewCanvas_) previewCanvas_->repaint(); };
    addAndMakeVisible(*comboPosition_);

    // Margin
    lblMargin_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Margem:") : juce::String::fromUTF8("Margin:"));
    lblMargin_->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    lblMargin_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblMargin_);

    sliderMargin_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    sliderMargin_->setRange(0.0, 100.0, 1.0);
    sliderMargin_->setValue(24.0);
    sliderMargin_->setTextValueSuffix("px");
    sliderMargin_->setColour(juce::Slider::thumbColourId, tk.acento);
    sliderMargin_->setColour(juce::Slider::trackColourId, tk.acento.withAlpha(0.6f));
    sliderMargin_->onValueChange = [this] { if (previewCanvas_) previewCanvas_->repaint(); };
    addAndMakeVisible(*sliderMargin_);

    // Preview Canvas
    previewCanvas_ = std::make_unique<PreviewCanvas>(*this);
    addAndMakeVisible(*previewCanvas_);

    // Destination Output Options
    radioSameFolder_ = std::make_unique<juce::ToggleButton>(isPt ? juce::String::fromUTF8("Salvar na mesma pasta de origem (adicionar sufixo '-w')")
                                                                  : juce::String::fromUTF8("Save in same source folder (append '-w' suffix)"));
    radioSameFolder_->setRadioGroupId(1001);
    radioSameFolder_->setToggleState(true, juce::dontSendNotification);
    radioSameFolder_->onClick = [this] {
        bool custom = radioCustomFolder_->getToggleState();
        editCustomFolder_->setEnabled(custom);
        btnBrowseCustomFolder_->setEnabled(custom);
    };
    addAndMakeVisible(*radioSameFolder_);

    radioCustomFolder_ = std::make_unique<juce::ToggleButton>(isPt ? juce::String::fromUTF8("Salvar em uma nova pasta de destino:")
                                                                    : juce::String::fromUTF8("Save to a new output folder:"));
    radioCustomFolder_->setRadioGroupId(1001);
    radioCustomFolder_->onClick = [this] {
        bool custom = radioCustomFolder_->getToggleState();
        editCustomFolder_->setEnabled(custom);
        btnBrowseCustomFolder_->setEnabled(custom);
    };
    addAndMakeVisible(*radioCustomFolder_);

    editCustomFolder_ = std::make_unique<juce::TextEditor>();
    editCustomFolder_->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
    editCustomFolder_->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    editCustomFolder_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    editCustomFolder_->setEnabled(false);
    addAndMakeVisible(*editCustomFolder_);

    btnBrowseCustomFolder_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Procurar...") : juce::String::fromUTF8("Browse..."));
    btnBrowseCustomFolder_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnBrowseCustomFolder_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnBrowseCustomFolder_->setEnabled(false);
    btnBrowseCustomFolder_->onClick = [this] {
        auto fc = std::make_shared<juce::FileChooser>(
            matriz::i18n::localeAtivo() == "pt_BR" ? juce::String::fromUTF8("Selecionar Pasta de Destino") : "Select Output Folder",
            juce::File::getSpecialLocation(juce::File::userPicturesDirectory));
        fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                        [this, fc](const juce::FileChooser& chooser) {
                            auto result = chooser.getResult();
                            if (result.isDirectory()) {
                                editCustomFolder_->setText(result.getFullPathName(), juce::dontSendNotification);
                            }
                        });
    };
    addAndMakeVisible(*btnBrowseCustomFolder_);

    // Action and Progress
    progressBar_ = std::make_unique<juce::ProgressBar>(progress_);
    progressBar_->setColour(juce::ProgressBar::foregroundColourId, tk.acento);
    progressBar_->setColour(juce::ProgressBar::backgroundColourId, tk.painelAlt);
    progressBar_->setVisible(false);
    addAndMakeVisible(*progressBar_);

    lblStatus_ = std::make_unique<juce::Label>("", "");
    lblStatus_->setFont(juce::Font(juce::FontOptions(12.0f)));
    lblStatus_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblStatus_);

    btnApply_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("APLICAR MARCA D'ÁGUA") : juce::String::fromUTF8("APPLY WATERMARK"));
    btnApply_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnApply_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnApply_->onClick = [this] { iniciarProcessamento(); };
    addAndMakeVisible(*btnApply_);

    btnCancel_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("FECHAR") : juce::String::fromUTF8("CLOSE"));
    btnCancel_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancel_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnCancel_->onClick = [this] {
        if (isProcessing_) {
            signalThreadShouldExit();
        } else if (aoFechar) {
            aoFechar();
        }
    };
    addAndMakeVisible(*btnCancel_);

    // Auto-load images from grid on open if available
    if (obterFotosDoGrid_) {
        auto arquivos = obterFotosDoGrid_();
        juce::Array<juce::File> arr;
        for (const auto& f : arquivos) arr.add(f);
        addPhotosFromFiles(arr);
    }

    setSize(980, 680);
}

BatchWatermarkDialog::~BatchWatermarkDialog() {
    if (isThreadRunning()) {
        signalThreadShouldExit();
        waitForThreadToExit(2000);
    }
}

void BatchWatermarkDialog::addPhotosFromFiles(const juce::Array<juce::File>& files) {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    for (const auto& f : files) {
        if (f.isDirectory()) {
            juce::Array<juce::File> subFiles;
            f.findChildFiles(subFiles, juce::File::findFiles, true, "*.jpg;*.jpeg;*.png;*.webp;*.tiff;*.tif;*.bmp");
            addPhotosFromFiles(subFiles);
        } else if (f.existsAsFile()) {
            juce::String ext = f.getFileExtension().toLowerCase().replace(".", "");
            if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" || ext == "tiff" || ext == "tif" || ext == "bmp") {
                // Check if already added
                bool alreadyExists = false;
                for (const auto& p : photos_) {
                    if (p.file == f) { alreadyExists = true; break; }
                }
                if (!alreadyExists) {
                    PhotoItem item;
                    item.file = f;
                    item.name = f.getFileName();
                    item.sizeBytes = f.getSize();
                    photos_.push_back(item);
                }
            }
        }
    }

    if (photoListBox_) photoListBox_->updateContent();
    if (lblPhotoCount_) {
        lblPhotoCount_->setText(juce::String((int)photos_.size()) + (isPt ? juce::String::fromUTF8(" fotos na lista") : " photos in list"), juce::dontSendNotification);
    }

    if (selectedPhotoIndex_ < 0 && !photos_.empty()) {
        selectedPhotoIndex_ = 0;
        if (photoListBox_) photoListBox_->selectRow(0);
        carregarFotoAmostra();
    }
}

void BatchWatermarkDialog::carregarLogo(const juce::File& file) {
    logoFile_ = file;
    logoImage_ = juce::ImageFileFormat::loadFrom(file);
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (logoImage_.isValid()) {
        juce::String info = file.getFileName() + " (" + juce::String(logoImage_.getWidth()) + "x" + juce::String(logoImage_.getHeight()) + " px)";
        lblLogoInfo_->setText(info, juce::dontSendNotification);
        lblLogoInfo_->setColour(juce::Label::textColourId, tema().acento);
    } else {
        lblLogoInfo_->setText(isPt ? juce::String::fromUTF8("Erro ao carregar formato da logo.") : "Error loading logo image.", juce::dontSendNotification);
        lblLogoInfo_->setColour(juce::Label::textColourId, juce::Colours::red);
    }
    if (previewCanvas_) previewCanvas_->repaint();
}

void BatchWatermarkDialog::carregarFotoAmostra() {
    if (selectedPhotoIndex_ >= 0 && selectedPhotoIndex_ < (int)photos_.size()) {
        const auto& f = photos_[selectedPhotoIndex_].file;
        sampleImage_ = juce::ImageFileFormat::loadFrom(f);
        if (sampleImage_.isValid()) {
            photos_[selectedPhotoIndex_].width = sampleImage_.getWidth();
            photos_[selectedPhotoIndex_].height = sampleImage_.getHeight();
            if (photoListBox_) photoListBox_->repaint();
        }
    } else {
        sampleImage_ = juce::Image();
    }
    if (previewCanvas_) previewCanvas_->repaint();
}

int BatchWatermarkDialog::getNumRows() {
    return (int)photos_.size();
}

void BatchWatermarkDialog::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= (int)photos_.size()) return;

    const auto& tk = tema();
    const auto& item = photos_[rowNumber];

    juce::Rectangle<int> bounds(0, 0, width, height);

    if (rowIsSelected) {
        g.setColour(tk.acento.withAlpha(0.18f));
        g.fillRoundedRectangle(bounds.reduced(2, 1).toFloat(), 4.0f);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(bounds.reduced(2, 1).toFloat(), 4.0f, 1.0f);
    }

    g.setFont(juce::Font(juce::FontOptions(12.5f, rowIsSelected ? juce::Font::bold : juce::Font::plain)));
    g.setColour(rowIsSelected ? tk.acento : tk.textoPrimario);
    g.drawText(item.name, 8, 4, width - 90, 16, juce::Justification::left, true);

    juce::String dimText;
    if (item.width > 0 && item.height > 0) {
        dimText = juce::String(item.width) + "x" + juce::String(item.height);
    } else {
        dimText = juce::File::descriptionOfSizeInBytes(item.sizeBytes);
    }

    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(tk.textoTerciario);
    g.drawText(dimText, width - 85, 4, 75, 16, juce::Justification::right, true);

    g.setColour(tk.textoTerciario.withAlpha(0.7f));
    g.drawText(item.file.getParentDirectory().getFileName(), 8, 20, width - 16, 14, juce::Justification::left, true);

    g.setColour(tk.borda.withAlpha(0.25f));
    g.drawLine(6.0f, (float)(height - 1), (float)(width - 6), (float)(height - 1));
}

void BatchWatermarkDialog::selectedRowsChanged(int lastRowSelected) {
    if (lastRowSelected >= 0 && lastRowSelected < (int)photos_.size()) {
        selectedPhotoIndex_ = lastRowSelected;
        carregarFotoAmostra();
    }
}

bool BatchWatermarkDialog::isInterestedInFileDrag(const juce::StringArray&) {
    return true;
}

void BatchWatermarkDialog::filesDropped(const juce::StringArray& files, int, int) {
    juce::Array<juce::File> fileList;
    for (const auto& path : files) {
        fileList.add(juce::File(path));
    }
    addPhotosFromFiles(fileList);
}

juce::Rectangle<float> BatchWatermarkDialog::calcularPosicaoLogo(float imgW, float imgH, float logoW, float logoH) const {
    float scalePercent = (float)(sliderScale_ ? sliderScale_->getValue() : 22.0) / 100.0f;
    float margin = (float)(sliderMargin_ ? sliderMargin_->getValue() : 24.0);

    // Calculate logo width relative to image width
    float targetLogoW = imgW * scalePercent;
    float targetLogoH = targetLogoW * (logoH / logoW);

    // Ensure logo doesn't exceed image dimensions
    if (targetLogoH > imgH * 0.9f) {
        targetLogoH = imgH * 0.9f;
        targetLogoW = targetLogoH * (logoW / logoH);
    }

    int posId = comboPosition_ ? comboPosition_->getSelectedId() : 1;
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
            posX = (imgW * customLogoPos_.x) - (targetLogoW * 0.5f);
            posY = (imgH * customLogoPos_.y) - (targetLogoH * 0.5f);
            break;
    }

    return juce::Rectangle<float>(posX, posY, targetLogoW, targetLogoH);
}

void BatchWatermarkDialog::iniciarProcessamento() {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (photos_.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Nenhuma foto selecionada") : "No photos selected",
            isPt ? juce::String::fromUTF8("Adicione pelo menos uma foto à lista antes de aplicar a marca d'água.")
                 : "Please add at least one photo to the list before applying watermark.");
        return;
    }

    if (!logoImage_.isValid()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Logo não selecionada") : "No logo selected",
            isPt ? juce::String::fromUTF8("Escolha um arquivo de imagem com sua logo ou marca d'água.")
                 : "Please choose a logo / watermark image file.");
        return;
    }

    if (radioCustomFolder_->getToggleState()) {
        juce::File outDir(editCustomFolder_->getText());
        if (!outDir.isDirectory() && !outDir.createDirectory()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                isPt ? juce::String::fromUTF8("Pasta de destino inválida") : "Invalid destination folder",
                isPt ? juce::String::fromUTF8("Por favor escolha uma pasta de destino válida para salvar as fotos.")
                     : "Please select a valid destination folder to save the watermarked photos.");
            return;
        }
    }

    isProcessing_ = true;
    progress_ = 0.0;
    progressBar_->setVisible(true);
    btnApply_->setEnabled(false);
    btnFromGrid_->setEnabled(false);
    btnAddPhotos_->setEnabled(false);
    btnClearPhotos_->setEnabled(false);
    btnChooseLogo_->setEnabled(false);

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "watermark", "Watermarking Photos", (int)photos_.size(),
        [this] { signalThreadShouldExit(); },
        "Processing " + juce::String((int)photos_.size()) + " photos...");

    startThread();
}

void BatchWatermarkDialog::run() {
    int total = (int)photos_.size();
    int sucessos = 0;
    int falhas = 0;

    bool isCustomFolder = radioCustomFolder_->getToggleState();
    juce::File customDir(editCustomFolder_->getText());
    float opacity = (float)(sliderOpacity_->getValue() / 100.0);

    for (int i = 0; i < total; ++i) {
        if (threadShouldExit()) break;

        const auto& item = photos_[i];
        
        juce::MessageManager::callAsync([this, i, total] {
            bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
            lblStatus_->setText((isPt ? juce::String::fromUTF8("Processando foto ") : "Processing photo ")
                                + juce::String(i + 1) + " / " + juce::String(total) + "...", juce::dontSendNotification);
            progress_ = (double)i / (double)total;
        });

        ProgressoGlobal::obterInstancia().atualizarProgresso(
            "watermark", i + 1, "Photo " + juce::String(i + 1) + " of " + juce::String(total));

        juce::Image srcImg = juce::ImageFileFormat::loadFrom(item.file);
        if (!srcImg.isValid()) {
            falhas++;
            continue;
        }

        // Create copy and draw logo
        juce::Image outImg = srcImg.createCopy();
        {
            juce::Graphics g(outImg);
            juce::Rectangle<float> logoPos = calcularPosicaoLogo(
                (float)srcImg.getWidth(), (float)srcImg.getHeight(),
                (float)logoImage_.getWidth(), (float)logoImage_.getHeight());

            g.setOpacity(opacity);
            g.drawImage(logoImage_, logoPos);
        }

        // Determine destination file path
        juce::File outFile;
        juce::String ext = item.file.getFileExtension();
        if (ext.isEmpty()) ext = ".jpg";

        if (isCustomFolder) {
            outFile = customDir.getChildFile(item.file.getFileName());
            lastOutputDir_ = customDir;
        } else {
            juce::String nomeBase = item.file.getFileNameWithoutExtension();
            outFile = item.file.getParentDirectory().getChildFile(nomeBase + "-w" + ext);
            lastOutputDir_ = item.file.getParentDirectory();
        }

        if (outFile.existsAsFile()) outFile.deleteFile();

        juce::FileOutputStream stream(outFile);
        bool gravou = false;
        if (stream.openedOk()) {
            if (ext.equalsIgnoreCase(".png")) {
                juce::PNGImageFormat pngFmt;
                gravou = pngFmt.writeImageToStream(outImg, stream);
            } else {
                juce::JPEGImageFormat jpgFmt;
                jpgFmt.setQuality(0.92f);
                gravou = jpgFmt.writeImageToStream(outImg, stream);
            }
        }

        if (gravou) sucessos++;
        else falhas++;
    }

    juce::MessageManager::callAsync([this, sucessos, falhas] {
        finalizarProcessamento(sucessos, falhas);
    });
}

void BatchWatermarkDialog::finalizarProcessamento(int sucessos, int falhas) {
    isProcessing_ = false;
    progressBar_->setVisible(false);
    btnApply_->setEnabled(true);
    btnFromGrid_->setEnabled(true);
    btnAddPhotos_->setEnabled(true);
    btnClearPhotos_->setEnabled(true);

    ProgressoGlobal::obterInstancia().concluirTarefa(
        "watermark", juce::String(sucessos) + " photos watermarked");
    btnChooseLogo_->setEnabled(true);

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    lblStatus_->setText(isPt ? juce::String::fromUTF8("Concluído: ") + juce::String(sucessos) + juce::String::fromUTF8(" fotos geradas.")
                             : "Complete: " + juce::String(sucessos) + " photos generated.", juce::dontSendNotification);

    juce::String msg = (isPt ? juce::String::fromUTF8("Processamento concluído!\n\nFotos salvas com sucesso: ")
                             : "Processing complete!\n\nSuccessfully exported photos: ")
                       + juce::String(sucessos);

    if (falhas > 0) {
        msg += (isPt ? juce::String::fromUTF8("\nFalhas: ") : "\nFailed: ") + juce::String(falhas);
    }

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::InfoIcon,
        isPt ? juce::String::fromUTF8("Marca d'Água Aplicada") : "Watermark Applied",
        msg + (isPt ? juce::String::fromUTF8("\n\nDeseja abrir a pasta no Finder?") : "\n\nDo you want to open destination in Finder?"),
        isPt ? juce::String::fromUTF8("Abrir no Finder") : "Show in Finder",
        isPt ? juce::String::fromUTF8("Fechar") : "Close",
        nullptr,
        juce::ModalCallbackFunction::create([this](int result) {
            if (result == 1 && lastOutputDir_.isDirectory()) {
                lastOutputDir_.startAsProcess();
            }
        })
    );
}

void BatchWatermarkDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Header bar
    g.setColour(tk.painel);
    g.fillRect(0, 0, getWidth(), 56);
    g.setColour(tk.borda);
    g.drawLine(0.0f, 56.0f, (float)getWidth(), 56.0f, 1.0f);

    // Bottom action bar
    g.setColour(tk.painel);
    g.fillRect(0, getHeight() - 56, getWidth(), 56);
    g.setColour(tk.borda);
    g.drawLine(0.0f, (float)(getHeight() - 56), (float)getWidth(), (float)(getHeight() - 56), 1.0f);
}

void BatchWatermarkDialog::resized() {
    auto r = getLocalBounds();
    int pad = 12;

    // Header
    auto headerArea = r.removeFromTop(56).reduced(pad, 4);
    lblTitulo_->setBounds(headerArea.removeFromTop(24));
    lblSubtitulo_->setBounds(headerArea);

    // Bottom Bar
    auto bottomArea = r.removeFromBottom(56).reduced(pad, 10);
    btnCancel_->setBounds(bottomArea.removeFromRight(100));
    bottomArea.removeFromRight(10);
    btnApply_->setBounds(bottomArea.removeFromRight(190));
    bottomArea.removeFromRight(16);
    progressBar_->setBounds(bottomArea.removeFromRight(160));
    lblStatus_->setBounds(bottomArea);

    // Main Content
    auto body = r.reduced(pad);

    // Left Column: Source Photos (Width 320px)
    int leftW = 320;
    auto leftArea = body.removeFromLeft(leftW);
    
    auto leftTopBtns = leftArea.removeFromTop(30);
    btnFromGrid_->setBounds(leftTopBtns.removeFromLeft(125));
    leftTopBtns.removeFromLeft(6);
    btnAddPhotos_->setBounds(leftTopBtns.removeFromLeft(125));
    leftTopBtns.removeFromLeft(6);
    btnClearPhotos_->setBounds(leftTopBtns);

    leftArea.removeFromTop(8);
    lblPhotoCount_->setBounds(leftArea.removeFromBottom(18));
    leftArea.removeFromBottom(4);
    photoListBox_->setBounds(leftArea);

    body.removeFromLeft(14);

    // Right Area: Settings (Top) + Preview (Middle) + Output (Bottom)
    auto rightArea = body;

    // Top Settings Panel (Logo, Opacity, Scale, Position, Margin)
    auto settingsArea = rightArea.removeFromTop(108);
    
    // Row 1: Choose Logo + Info
    auto row1 = settingsArea.removeFromTop(30);
    btnChooseLogo_->setBounds(row1.removeFromLeft(200));
    row1.removeFromLeft(10);
    lblLogoInfo_->setBounds(row1);

    settingsArea.removeFromTop(6);

    // Row 2: Sliders and Combo
    auto row2 = settingsArea.removeFromTop(32);
    int halfW = (row2.getWidth() - 20) / 2;
    
    auto row2Left = row2.removeFromLeft(halfW);
    lblOpacity_->setBounds(row2Left.removeFromLeft(75));
    sliderOpacity_->setBounds(row2Left);

    row2.removeFromLeft(20);
    auto row2Right = row2;
    lblScale_->setBounds(row2Right.removeFromLeft(110));
    sliderScale_->setBounds(row2Right);

    settingsArea.removeFromTop(6);

    // Row 3: Position and Margin
    auto row3 = settingsArea.removeFromTop(32);
    auto row3Left = row3.removeFromLeft(halfW);
    lblPosition_->setBounds(row3Left.removeFromLeft(75));
    comboPosition_->setBounds(row3Left);

    row3.removeFromLeft(20);
    auto row3Right = row3;
    lblMargin_->setBounds(row3Right.removeFromLeft(110));
    sliderMargin_->setBounds(row3Right);

    rightArea.removeFromTop(10);

    // Bottom Output Options
    auto outArea = rightArea.removeFromBottom(64);
    radioSameFolder_->setBounds(outArea.removeFromTop(24));
    
    auto rowCustom = outArea.removeFromTop(28);
    radioCustomFolder_->setBounds(rowCustom.removeFromLeft(240));
    rowCustom.removeFromLeft(8);
    btnBrowseCustomFolder_->setBounds(rowCustom.removeFromRight(90));
    rowCustom.removeFromRight(6);
    editCustomFolder_->setBounds(rowCustom);

    rightArea.removeFromBottom(8);

    // Middle Preview Canvas
    previewCanvas_->setBounds(rightArea);
}

void BatchWatermarkDialog::lookAndFeelChanged() {
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (lblTitulo_) {
        lblTitulo_->setText(isPt ? juce::String::fromUTF8("MARCA D'ÁGUA EM LOTE") : juce::String::fromUTF8("IMAGE BATCH WATERMARK"), juce::dontSendNotification);
        lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (btnApply_) {
        btnApply_->setColour(juce::TextButton::buttonColourId, tk.acento);
    }
    if (photoListBox_) photoListBox_->repaint();
    if (previewCanvas_) previewCanvas_->repaint();
    repaint();
}

void BatchWatermarkDialog::exibirModal(std::function<std::vector<juce::File>()> obterFotosDoGrid) {
    auto dlg = std::make_unique<BatchWatermarkDialog>(obterFotosDoGrid);
    dlg->setSize(980, 680);
    auto* rawDlg = dlg.get();

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dlg.release());
    opt.dialogTitle = isPt ? juce::String::fromUTF8("Marca d'Água em Lote") : "Image Batch Watermark";
    opt.dialogBackgroundColour = juce::Colours::transparentBlack;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = false;
    opt.resizable = true;

    auto* win = opt.launchAsync();
    rawDlg->aoFechar = [win] {
        if (win) win->exitModalState(0);
    };
}

} // namespace matriz::ui
