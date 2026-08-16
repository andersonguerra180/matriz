#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>
#include <vector>

#include "../Consolidacao/Consolidacao.h"

namespace matriz::ui {

class HierarquiaEditorComponent : public juce::Component {
public:
    explicit HierarquiaEditorComponent(const matriz::consolidacao::HierarquiaBackup& hierarquiaAtual);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    matriz::consolidacao::HierarquiaBackup hierarquiaResultante() const;

    std::function<void(const matriz::consolidacao::HierarquiaBackup&)> aoConfirmar;
    std::function<void()> aoCancelar;

private:
    struct Bloco {
        matriz::consolidacao::NivelHierarquia nivel;
        juce::String rotulo;
        juce::Colour cor;
        bool ativo = false;
        juce::Rectangle<float> bounds;
    };

    void recalcularLayout();
    int indiceBlocoNaPosicao(juce::Point<int> pos) const;
    int indiceDropNaPosicao(int y) const;
    juce::String previewCaminho() const;

    std::vector<Bloco> blocos_;
    int arrastando_ = -1;
    juce::Point<int> offsetArrasto_;
    juce::Point<float> posicaoArrasto_;
    int indiceDropAlvo_ = -1;

    std::unique_ptr<juce::TextButton> btnOk_;
    std::unique_ptr<juce::TextButton> btnCancelar_;
    std::unique_ptr<juce::Label> labelPreview_;
    std::unique_ptr<juce::Label> labelTitulo_;
    std::unique_ptr<juce::Label> labelAtivos_;
    std::unique_ptr<juce::Label> labelDisponiveis_;

    static constexpr float kBlocoLargura = 180.0f;
    static constexpr float kBlocoAltura = 44.0f;
    static constexpr float kEspaco = 12.0f;
    static constexpr float kRaio = 10.0f;
    static constexpr int kAreaAtivosX = 80;
    static constexpr int kAreaDisponiveisX = 340;
    static constexpr int kTopoArea = 100;
};

class HierarquiaEditorWindow : public juce::DocumentWindow {
public:
    HierarquiaEditorWindow(const matriz::consolidacao::HierarquiaBackup& hierarquiaAtual,
                            std::function<void(const matriz::consolidacao::HierarquiaBackup&)> aoConfirmar);

    void closeButtonPressed() override { delete this; }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HierarquiaEditorWindow)
};

} // namespace matriz::ui
