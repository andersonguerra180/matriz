#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include "../Model/Project.h"

namespace matriz::ui {

// Estado compartilhado de UMA tentativa de abertura (worker + diálogo).
struct EstadoAberturaProjeto;

class ProjectLoadingModalDialog : public juce::Component,
                                  private juce::Timer {
public:
    // `aguardandoApenas`: reabre o diálogo de uma abertura anterior ainda finalizando (sem botões).
    ProjectLoadingModalDialog(const juce::File& pasta, std::shared_ptr<EstadoAberturaProjeto> estado,
                              bool aguardandoApenas = false);
    ~ProjectLoadingModalDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setStatus(const juce::String& text, double progress);
    void closeDialog();

    using OnCompleteCallback = std::function<void(
        std::unique_ptr<matriz::model::Project> proj,
        std::string rotuloMaisRecente,
        int64_t revisaoMaisRecente,
        bool isCatalog,
        std::string erroMsg)>;

    // Mostra o diálogo de espera da abertura que ainda está finalizando (se houver).
    static void mostrarEsperaDaAnterior(juce::Component* parentComp);

    static void launch(const juce::File& pasta,
                       juce::Component* parentComp,
                       OnCompleteCallback onComplete);

private:
    void timerCallback() override;
    void cliqueCancelar();
    juce::String textoAindaAbrindo(const char* verboPt, const char* verboEn) const;

    juce::File pasta_;
    juce::Label lblHeader_;
    juce::Label lblSubHeader_;
    juce::ProgressBar progressBar_;
    juce::Label lblStatus_;
    juce::TextButton btnContinuar_;
    juce::TextButton btnCancelar_;
    std::shared_ptr<EstadoAberturaProjeto> estado_;
    bool aguardandoApenas_ = false;
    bool avisoMostrado_ = false;
    bool continuouEsperando_ = false;
    bool cancelando_ = false;
    juce::int64 cancelouEmMs_ = 0;

    double targetProgress_ = 0.05;
    double currentProgress_ = 0.05;
    juce::String currentStatusText_;
    std::mutex stateMutex_;
    bool isClosed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectLoadingModalDialog)
};

} // namespace matriz::ui
