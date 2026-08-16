#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../Ingest/IngestArquivo.h"
#include "../Ingest/CacheArquivo.h"
#include "../Ingest/ClassificadorFalaMusica.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::ui {

class ProjetoAberto;

class IngestWizardComponent : public juce::Component,
                               private juce::Timer {
public:
    explicit IngestWizardComponent(ProjetoAberto& projeto);
    ~IngestWizardComponent() override;

    enum class Etapa {
        Source,
        Discovering,
        Review,
        ReadyToImport,
        Importing,
        Verifying,
        Done,
        Failed
    };

    Etapa etapaAtual() const { return etapa_; }

    std::function<void()> aoFechar;
    std::function<void()> aoConcluir;
    std::function<void(const std::vector<std::string>& itemIds)> aoVerNovosAssets;
    std::function<void()> aoVerTodosAssets;
    std::function<void()> aoVerProblemas;
    std::function<void()> aoVoltarHome;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&);
    void filesDropped(const juce::StringArray& files, int x, int y);

private:
    struct ArquivoAnalisado {
        juce::File arquivo;
        matriz::ingest::AnaliseDeArquivo analise;
        matriz::ingest::AnaliseCache cache;
        matriz::ingest::CategoriaMidia categoria = matriz::ingest::CategoriaMidia::Desconhecida;
        std::optional<matriz::ingest::ResultadoFalaMusica> classificacao;
        bool ehDuplicata = false;
        std::string itemIdExistente;
        juce::String erroPorque;
        bool erro = false;
    };

    void irParaEtapa(Etapa etapa);
    void iniciarDescoberta(const juce::File& pasta);
    void iniciarImportacao();
    void timerCallback() override;

    void construirEtapaSource();
    void construirEtapaDiscovery();
    void construirEtapaReview();
    void construirEtapaReadyToImport();
    void construirEtapaImporting();
    void construirEtapaVerify();
    void construirEtapaDone();

    void limparConteudo();
    void pintarBarraEtapas(juce::Graphics& g, juce::Rectangle<int> area);

    ProjetoAberto& projeto_;
    Etapa etapa_ = Etapa::Source;

    juce::File pastaOrigem_;
    std::vector<ArquivoAnalisado> resultados_;

    // Discovery progress (shared with background threads)
    std::shared_ptr<std::atomic<int>> analisados_ = std::make_shared<std::atomic<int>>(0);
    int totalParaAnalisar_ = 0;
    bool descobertaEmAndamento_ = false;

    // Import progress
    std::shared_ptr<std::atomic<int>> importados_ = std::make_shared<std::atomic<int>>(0);
    int totalParaImportar_ = 0;

    // Verification
    int verificados_ = 0;
    int totalParaVerificar_ = 0;

    // Results
    int novosImportados_ = 0;
    int duplicatasDetectadas_ = 0;
    int errosDetectados_ = 0;
    std::vector<std::string> itemIdsImportados_;

    juce::ThreadPool pool_{
        juce::ThreadPoolOptions{}
            .withThreadName("IngestWiz")
            .withNumberOfThreads(juce::jlimit(2, 6, juce::SystemStats::getNumCpus() / 2))
            .withDesiredThreadPriority(juce::Thread::Priority::low)};
    std::shared_ptr<std::mutex> mutexResultados_ = std::make_shared<std::mutex>();

    // UI elements (rebuilt per step)
    std::unique_ptr<juce::Label> tituloEtapa_;
    std::unique_ptr<juce::Label> subtituloEtapa_;
    std::unique_ptr<juce::Label> labelProgresso_;
    std::unique_ptr<juce::ProgressBar> barraProgresso_;
    double progressoValor_ = 0.0;
    std::unique_ptr<juce::TextButton> btnVoltar_;
    std::unique_ptr<juce::TextButton> btnProximo_;
    std::unique_ptr<juce::TextButton> btnCancelar_;
    std::unique_ptr<juce::TextButton> btnBrowse_;
    std::unique_ptr<juce::TextButton> btnViewNew_;
    std::unique_ptr<juce::TextButton> btnViewAll_;
    std::unique_ptr<juce::TextButton> btnReviewIssues_;
    std::unique_ptr<juce::TextButton> btnReturnHome_;
    std::unique_ptr<juce::Label> labelResumo_;
    std::unique_ptr<juce::Label> labelResumo2_;
    std::unique_ptr<juce::Label> labelResumo3_;
    std::unique_ptr<juce::Label> labelResumo4_;

    static constexpr int kAlturaBarraEtapas = 48;
};

} // namespace matriz::ui
