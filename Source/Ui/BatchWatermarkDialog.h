#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>

namespace matriz::ui {

class BatchWatermarkDialog : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             public juce::ListBoxModel,
                             private juce::Thread {
public:
    enum class Position {
        BottomRight,
        BottomLeft,
        TopRight,
        TopLeft,
        Center,
        TopCenter,
        BottomCenter,
        Custom
    };

    enum class OutputMode {
        SameFolderWithSuffix,
        CustomFolder
    };

    explicit BatchWatermarkDialog(std::function<std::vector<juce::File>()> obterFotosDoGrid = nullptr);
    ~BatchWatermarkDialog() override;

    static void exibirModal(std::function<std::vector<juce::File>()> obterFotosDoGrid = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

private:
    std::function<std::vector<juce::File>()> obterFotosDoGrid_;

    struct PhotoItem {
        juce::File file;
        juce::String name;
        juce::int64 sizeBytes = 0;
        int width = 0;
        int height = 0;
    };

    std::vector<PhotoItem> photos_;
    juce::File logoFile_;
    juce::Image logoImage_;
    juce::Image sampleImage_;
    int selectedPhotoIndex_ = -1;

    // Controls - Header
    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblSubtitulo_;

    // Controls - Left (Photos)
    std::unique_ptr<juce::TextButton> btnFromGrid_;
    std::unique_ptr<juce::TextButton> btnAddPhotos_;
    std::unique_ptr<juce::TextButton> btnClearPhotos_;
    std::unique_ptr<juce::ListBox> photoListBox_;
    std::unique_ptr<juce::Label> lblPhotoCount_;

    // Controls - Middle/Right (Watermark Settings & Preview)
    std::unique_ptr<juce::TextButton> btnChooseLogo_;
    std::unique_ptr<juce::Label> lblLogoInfo_;
    std::unique_ptr<juce::Slider> sliderOpacity_;
    std::unique_ptr<juce::Label> lblOpacity_;
    std::unique_ptr<juce::Slider> sliderScale_;
    std::unique_ptr<juce::Label> lblScale_;
    std::unique_ptr<juce::ComboBox> comboPosition_;
    std::unique_ptr<juce::Label> lblPosition_;
    std::unique_ptr<juce::Slider> sliderMargin_;
    std::unique_ptr<juce::Label> lblMargin_;

    // Preview Component
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

    // Output settings
    std::unique_ptr<juce::ToggleButton> radioSameFolder_;
    std::unique_ptr<juce::ToggleButton> radioCustomFolder_;
    std::unique_ptr<juce::TextEditor> editCustomFolder_;
    std::unique_ptr<juce::TextButton> btnBrowseCustomFolder_;

    // Action & Progress
    std::unique_ptr<juce::ProgressBar> progressBar_;
    std::unique_ptr<juce::Label> lblStatus_;
    std::unique_ptr<juce::TextButton> btnApply_;
    std::unique_ptr<juce::TextButton> btnCancel_;

    // State
    double progress_ = 0.0;
    std::atomic<bool> isProcessing_{false};
    juce::Point<float> customLogoPos_{0.5f, 0.5f};
    juce::File lastOutputDir_;

    void addPhotosFromFiles(const juce::Array<juce::File>& files);
    void carregarLogo(const juce::File& file);
    void carregarFotoAmostra();
    void iniciarProcessamento();
    void run() override; // juce::Thread
    void finalizarProcessamento(int sucessos, int falhas);

    juce::Rectangle<float> calcularPosicaoLogo(float imgW, float imgH, float logoW, float logoH) const;

    std::function<void()> aoFechar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatchWatermarkDialog)
};

} // namespace matriz::ui
