#include "IngestWizardComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Ingest/Miniaturas.h"
#include "../Model/Project.h"
#include "../Vault/Volume.h"

namespace {

struct PapelInfo {
    std::string papel;
    bool ehMaster;
};

PapelInfo papelPorCategoria(matriz::ingest::CategoriaMidia categoria) {
    if (categoria == matriz::ingest::CategoriaMidia::Audio || categoria == matriz::ingest::CategoriaMidia::Video)
        return {"preservation_master", true};
    if (categoria == matriz::ingest::CategoriaMidia::Imagem) return {"foto_suporte", true};
    return {"documento", true};
}

std::vector<juce::File> expandirPasta(const juce::File& pasta) {
    std::vector<juce::File> arquivos;
    for (auto& sub : juce::RangedDirectoryIterator(pasta, true, "*", juce::File::findFiles)) {
        juce::File f = sub.getFile();
        juce::String name = f.getFileName();
        juce::String ext = f.getFileExtension().trimCharactersAtStart(".").toLowerCase();
        if (name.startsWith(".") || f.getSize() == 0) continue;
        if (ext == "sfk" || ext == "reapeaks" || ext == "asd") continue;
        arquivos.push_back(f);
    }
    return arquivos;
}

} // namespace

namespace matriz::ui {

IngestWizardComponent::IngestWizardComponent(ProjetoAberto& projeto)
    : projeto_(projeto)
{
    construirEtapaSource();
}

IngestWizardComponent::~IngestWizardComponent() {
    stopTimer();
    pool_.removeAllJobs(true, 5000);
}

void IngestWizardComponent::limparConteudo() {
    tituloEtapa_.reset();
    subtituloEtapa_.reset();
    labelProgresso_.reset();
    barraProgresso_.reset();
    btnVoltar_.reset();
    btnProximo_.reset();
    btnCancelar_.reset();
    btnBrowse_.reset();
    labelResumo_.reset();
    labelResumo2_.reset();
    labelResumo3_.reset();
    labelResumo4_.reset();

    btnViewNew_.reset();
    btnViewAll_.reset();
    btnReviewIssues_.reset();
    btnReturnHome_.reset();
}

void IngestWizardComponent::irParaEtapa(Etapa etapa) {
    etapa_ = etapa;
    limparConteudo();

    switch (etapa) {
        case Etapa::Source:         construirEtapaSource(); break;
        case Etapa::Discovering:    construirEtapaDiscovery(); break;
        case Etapa::Review:         construirEtapaReview(); break;
        case Etapa::ReadyToImport:  construirEtapaReadyToImport(); break;
        case Etapa::Importing:      construirEtapaImporting(); break;
        case Etapa::Verifying:      construirEtapaVerify(); break;
        case Etapa::Done:           construirEtapaDone(); break;
        case Etapa::Failed:         break;
    }

    resized();
    repaint();
}

void IngestWizardComponent::construirEtapaSource() {
    tituloEtapa_ = std::make_unique<juce::Label>();
    tituloEtapa_->setText(matriz::i18n::t("ingest.source_titulo"), juce::dontSendNotification);
    tituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    tituloEtapa_->setColour(juce::Label::textColourId, tema().textoPrimario);
    tituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*tituloEtapa_);

    subtituloEtapa_ = std::make_unique<juce::Label>();
    subtituloEtapa_->setText(matriz::i18n::t("ingest.source_arrastar"), juce::dontSendNotification);
    subtituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    subtituloEtapa_->setColour(juce::Label::textColourId, tema().textoSecundario);
    subtituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*subtituloEtapa_);

    btnBrowse_ = std::make_unique<juce::TextButton>("Browse...");
    btnBrowse_->setColour(juce::TextButton::buttonColourId, tema().acento);
    btnBrowse_->setColour(juce::TextButton::textColourOffId, tema().textoSobreAcento);
    btnBrowse_->onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>("Choose source folder");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.existsAsFile() || result.isDirectory())
                    iniciarDescoberta(result);
            });
    };
    addAndMakeVisible(*btnBrowse_);

    btnCancelar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.cancelar"));
    btnCancelar_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnCancelar_->onClick = [this] { if (aoFechar) aoFechar(); };
    addAndMakeVisible(*btnCancelar_);
}

void IngestWizardComponent::construirEtapaDiscovery() {
    tituloEtapa_ = std::make_unique<juce::Label>();
    tituloEtapa_->setText(matriz::i18n::t("ingest.descobre_titulo"), juce::dontSendNotification);
    tituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    tituloEtapa_->setColour(juce::Label::textColourId, tema().textoPrimario);
    tituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*tituloEtapa_);

    labelProgresso_ = std::make_unique<juce::Label>();
    labelProgresso_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    labelProgresso_->setColour(juce::Label::textColourId, tema().textoSecundario);
    labelProgresso_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*labelProgresso_);

    progressoValor_ = 0.0;
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    barraProgresso_->setColour(juce::ProgressBar::foregroundColourId, tema().acento);
    barraProgresso_->setColour(juce::ProgressBar::backgroundColourId, tema().painelAlt);
    addAndMakeVisible(*barraProgresso_);

    btnCancelar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.cancelar"));
    btnCancelar_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnCancelar_->onClick = [this] {
        stopTimer();
        pool_.removeAllJobs(true, 2000);
        descobertaEmAndamento_ = false;
        irParaEtapa(Etapa::Source);
    };
    addAndMakeVisible(*btnCancelar_);
}

void IngestWizardComponent::construirEtapaReview() {
    tituloEtapa_ = std::make_unique<juce::Label>();
    tituloEtapa_->setText(matriz::i18n::t("ingest.review_titulo"), juce::dontSendNotification);
    tituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    tituloEtapa_->setColour(juce::Label::textColourId, tema().textoPrimario);
    tituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*tituloEtapa_);

    int nErros = 0, nDuplicatas = 0, nDesconhecidos = 0;
    for (const auto& r : resultados_) {
        if (r.erro) ++nErros;
        if (r.ehDuplicata) ++nDuplicatas;
        if (!r.erro && !r.ehDuplicata && r.categoria == matriz::ingest::CategoriaMidia::Desconhecida)
            ++nDesconhecidos;
    }
    // DEBUG: dump category breakdown to log file
    {
        std::map<int, int> catCount;
        int totalRes = 0;
        juce::String debugExts;
        for (const auto& r : resultados_) {
            catCount[static_cast<int>(r.categoria)]++;
            totalRes++;
            if (r.categoria == matriz::ingest::CategoriaMidia::Desconhecida && debugExts.length() < 2000) {
                debugExts += r.arquivo.getFileExtension() + " ";
            }
        }
        juce::String dbg = "INGEST DEBUG: total=" + juce::String(totalRes)
            + " erros=" + juce::String(nErros)
            + " dup=" + juce::String(nDuplicatas)
            + " desconhecidos=" + juce::String(nDesconhecidos)
            + " | cats: ";
        for (auto& [k, v] : catCount) dbg += juce::String(k) + "=" + juce::String(v) + " ";
        dbg += "| unknown_exts: " + debugExts;
        juce::Logger::writeToLog(dbg);
        juce::File("/tmp/bkr_ingest_debug.txt").replaceWithText(dbg);
    }

    bool temProblemas = nErros > 0 || nDuplicatas > 0 || nDesconhecidos > 0;

    subtituloEtapa_ = std::make_unique<juce::Label>();
    if (!temProblemas) {
        subtituloEtapa_->setText(matriz::i18n::t("ingest.review_nenhum"), juce::dontSendNotification);
    } else {
        juce::String texto;
        if (nDuplicatas > 0) {
            auto t = matriz::i18n::t("ingest.resumo_duplicatas");
            texto += t.replace("{0}", juce::String(nDuplicatas)) + "\n";
        }
        if (nErros > 0) {
            auto t = matriz::i18n::t("ingest.resumo_erros");
            texto += t.replace("{0}", juce::String(nErros)) + "\n";
        }
        if (nDesconhecidos > 0)
            texto += juce::String(nDesconhecidos) + " files with unknown type\n";
        subtituloEtapa_->setText(texto.trimEnd(), juce::dontSendNotification);
    }
    subtituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    subtituloEtapa_->setColour(juce::Label::textColourId, tema().textoSecundario);
    subtituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*subtituloEtapa_);

    btnProximo_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.btn_proximo"));
    btnProximo_->setColour(juce::TextButton::buttonColourId, tema().acento);
    btnProximo_->setColour(juce::TextButton::textColourOffId, tema().textoSobreAcento);
    btnProximo_->onClick = [this] { irParaEtapa(Etapa::ReadyToImport); };
    addAndMakeVisible(*btnProximo_);

    btnVoltar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.btn_voltar"));
    btnVoltar_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnVoltar_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnVoltar_->onClick = [this] { irParaEtapa(Etapa::Source); };
    addAndMakeVisible(*btnVoltar_);
}

void IngestWizardComponent::construirEtapaReadyToImport() {
    tituloEtapa_ = std::make_unique<juce::Label>();
    tituloEtapa_->setText(matriz::i18n::t("ingest.step_importar"), juce::dontSendNotification);
    tituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    tituloEtapa_->setColour(juce::Label::textColourId, tema().textoPrimario);
    tituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*tituloEtapa_);

    int novos = 0, audio = 0, video = 0, img = 0, doc = 0, outro = 0;
    for (const auto& r : resultados_) {
        if (r.erro || r.ehDuplicata) continue;
        ++novos;
        switch (r.categoria) {
            case matriz::ingest::CategoriaMidia::Audio: ++audio; break;
            case matriz::ingest::CategoriaMidia::Video: ++video; break;
            case matriz::ingest::CategoriaMidia::Imagem: ++img; break;
            case matriz::ingest::CategoriaMidia::Documento: ++doc; break;
            default: ++outro; break;
        }
    }

    labelResumo_ = std::make_unique<juce::Label>();
    auto textoNovos = matriz::i18n::t("ingest.resumo_novos").replace("{0}", juce::String(novos));
    labelResumo_->setText(textoNovos, juce::dontSendNotification);
    labelResumo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    labelResumo_->setColour(juce::Label::textColourId, tema().textoPrimario);
    labelResumo_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*labelResumo_);

    labelResumo2_ = std::make_unique<juce::Label>();
    auto textoCat = matriz::i18n::t("ingest.resumo_categorias")
                        .replace("{0}", juce::String(audio))
                        .replace("{1}", juce::String(video))
                        .replace("{2}", juce::String(img))
                        .replace("{3}", juce::String(doc))
                        .replace("{4}", juce::String(outro));
    labelResumo2_->setText(textoCat, juce::dontSendNotification);
    labelResumo2_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    labelResumo2_->setColour(juce::Label::textColourId, tema().textoSecundario);
    labelResumo2_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*labelResumo2_);

    subtituloEtapa_ = std::make_unique<juce::Label>();
    subtituloEtapa_->setText("No files will be modified on the original source.", juce::dontSendNotification);
    subtituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    subtituloEtapa_->setColour(juce::Label::textColourId, tema().textoTerciario);
    subtituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*subtituloEtapa_);

    btnProximo_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.btn_importar"));
    btnProximo_->setColour(juce::TextButton::buttonColourId, tema().acento);
    btnProximo_->setColour(juce::TextButton::textColourOffId, tema().textoSobreAcento);
    btnProximo_->onClick = [this] { iniciarImportacao(); };
    addAndMakeVisible(*btnProximo_);

    btnVoltar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.btn_voltar"));
    btnVoltar_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnVoltar_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnVoltar_->onClick = [this] { irParaEtapa(Etapa::Review); };
    addAndMakeVisible(*btnVoltar_);
}

void IngestWizardComponent::construirEtapaImporting() {
    tituloEtapa_ = std::make_unique<juce::Label>();
    tituloEtapa_->setText(matriz::i18n::t("ingest.importando_titulo"), juce::dontSendNotification);
    tituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    tituloEtapa_->setColour(juce::Label::textColourId, tema().textoPrimario);
    tituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*tituloEtapa_);

    labelProgresso_ = std::make_unique<juce::Label>();
    labelProgresso_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    labelProgresso_->setColour(juce::Label::textColourId, tema().textoSecundario);
    labelProgresso_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*labelProgresso_);

    progressoValor_ = 0.0;
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    barraProgresso_->setColour(juce::ProgressBar::foregroundColourId, tema().acento);
    barraProgresso_->setColour(juce::ProgressBar::backgroundColourId, tema().painelAlt);
    addAndMakeVisible(*barraProgresso_);
}

void IngestWizardComponent::construirEtapaVerify() {
    tituloEtapa_ = std::make_unique<juce::Label>();
    tituloEtapa_->setText(matriz::i18n::t("ingest.verificando_titulo"), juce::dontSendNotification);
    tituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    tituloEtapa_->setColour(juce::Label::textColourId, tema().textoPrimario);
    tituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*tituloEtapa_);

    labelProgresso_ = std::make_unique<juce::Label>();
    labelProgresso_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    labelProgresso_->setColour(juce::Label::textColourId, tema().textoSecundario);
    labelProgresso_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*labelProgresso_);

    progressoValor_ = 0.0;
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    barraProgresso_->setColour(juce::ProgressBar::foregroundColourId, tema().estadoQcOk);
    barraProgresso_->setColour(juce::ProgressBar::backgroundColourId, tema().painelAlt);
    addAndMakeVisible(*barraProgresso_);
}

void IngestWizardComponent::construirEtapaDone() {
    tituloEtapa_ = std::make_unique<juce::Label>();
    tituloEtapa_->setText(matriz::i18n::t("ingest.concluido_titulo"), juce::dontSendNotification);
    tituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteTitulo, juce::Font::bold)));
    tituloEtapa_->setColour(juce::Label::textColourId, tema().textoPrimario);
    tituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*tituloEtapa_);

    subtituloEtapa_ = std::make_unique<juce::Label>();
    auto texto = matriz::i18n::t("ingest.concluido_assets").replace("{0}", juce::String(novosImportados_));
    subtituloEtapa_->setText(texto, juce::dontSendNotification);
    subtituloEtapa_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    subtituloEtapa_->setColour(juce::Label::textColourId, tema().textoSecundario);
    subtituloEtapa_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*subtituloEtapa_);

    if (duplicatasDetectadas_ > 0) {
        labelResumo_ = std::make_unique<juce::Label>();
        auto t = matriz::i18n::t("ingest.resumo_duplicatas").replace("{0}", juce::String(duplicatasDetectadas_));
        labelResumo_->setText(t, juce::dontSendNotification);
        labelResumo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        labelResumo_->setColour(juce::Label::textColourId, tema().textoTerciario);
        labelResumo_->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*labelResumo_);
    }

    if (errosDetectados_ > 0) {
        labelResumo2_ = std::make_unique<juce::Label>();
        auto t = matriz::i18n::t("ingest.resumo_erros").replace("{0}", juce::String(errosDetectados_));
        labelResumo2_->setText(t, juce::dontSendNotification);
        labelResumo2_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        labelResumo2_->setColour(juce::Label::textColourId, tema().alerta);
        labelResumo2_->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*labelResumo2_);
    }

    // 1. VIEW NEW ASSETS
    btnViewNew_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.concluido_ver_novos"));
    btnViewNew_->setColour(juce::TextButton::buttonColourId, tema().acento);
    btnViewNew_->setColour(juce::TextButton::textColourOffId, tema().textoSobreAcento);
    btnViewNew_->onClick = [this] { if (aoVerNovosAssets) aoVerNovosAssets(itemIdsImportados_); };
    btnViewNew_->setEnabled(novosImportados_ > 0);
    addAndMakeVisible(*btnViewNew_);

    // 2. VIEW ALL ASSETS
    btnViewAll_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.concluido_ver_todos"));
    btnViewAll_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnViewAll_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnViewAll_->onClick = [this] { if (aoVerTodosAssets) aoVerTodosAssets(); };
    addAndMakeVisible(*btnViewAll_);

    // 3. REVIEW ISSUES
    btnReviewIssues_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.concluido_ver_problemas"));
    btnReviewIssues_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnReviewIssues_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnReviewIssues_->onClick = [this] { if (aoVerProblemas) aoVerProblemas(); };
    btnReviewIssues_->setEnabled(errosDetectados_ > 0);
    addAndMakeVisible(*btnReviewIssues_);

    // 4. RETURN HOME
    btnReturnHome_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ingest.concluido_voltar_home"));
    btnReturnHome_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnReturnHome_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnReturnHome_->onClick = [this] { if (aoVoltarHome) aoVoltarHome(); };
    addAndMakeVisible(*btnReturnHome_);
}

// -- Discovery: scan + analyze in background, NO DB writes --

void IngestWizardComponent::iniciarDescoberta(const juce::File& pasta) {
    pastaOrigem_ = pasta;
    resultados_.clear();
    itemIdsImportados_.clear();
    *analisados_ = 0;
    descobertaEmAndamento_ = true;
    irParaEtapa(Etapa::Discovering);

    juce::File pastaProjeto = projeto_.projeto().pasta();
    auto mutexRes = mutexResultados_;
    auto analisadosPtr = analisados_;
    juce::Component::SafePointer<IngestWizardComponent> safeThis(this);
    auto registro = &projeto_.projeto().registro();

    pool_.addJob([pasta, pastaProjeto, mutexRes, analisadosPtr, safeThis, registro]() {
        auto arquivos = expandirPasta(pasta);
        int total = static_cast<int>(arquivos.size());

        juce::MessageManager::callAsync([safeThis, total]() {
            if (!safeThis) return;
            safeThis.getComponent()->totalParaAnalisar_ = total;
            if (total == 0) {
                safeThis.getComponent()->descobertaEmAndamento_ = false;
                safeThis.getComponent()->stopTimer();
                // No files found
                safeThis.getComponent()->irParaEtapa(Etapa::Review);
            }
        });

        if (arquivos.empty()) return;

        // Pre-allocate result vector on main thread, then fill from workers
        juce::MessageManager::callAsync([safeThis, total]() {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            self->resultados_.resize(static_cast<size_t>(total));
            self->startTimer(100);
        });

        auto pool = std::make_shared<juce::ThreadPool>(
            juce::ThreadPoolOptions{}
                .withThreadName("IngestAnalyze")
                .withNumberOfThreads(juce::jlimit(2, 6, juce::SystemStats::getNumCpus() / 2))
                .withDesiredThreadPriority(juce::Thread::Priority::low));

        for (int i = 0; i < total; ++i) {
            juce::File arquivo = arquivos[static_cast<size_t>(i)];
            pool->addJob([i, arquivo, pastaProjeto, mutexRes, analisadosPtr, safeThis, registro]() {
                ArquivoAnalisado resultado;
                resultado.arquivo = arquivo;
                resultado.categoria = matriz::ingest::categoriaPorExtensao(arquivo);
                {
                    juce::String dbgLine = "ANALYZE i=" + juce::String(i)
                        + " ext=" + arquivo.getFileExtension()
                        + " cat=" + juce::String(static_cast<int>(resultado.categoria))
                        + " file=" + arquivo.getFileName() + "\n";
                    juce::File("/tmp/bkr_analyze_debug.txt").appendText(dbgLine);
                }

                try {
                    resultado.analise = matriz::ingest::analisarArquivo(arquivo);

                    if (!resultado.analise.ehPlaceholderNuvem)
                        resultado.cache = matriz::ingest::calcularCache(
                            arquivo, resultado.categoria, pastaProjeto,
                            resultado.analise.leitura.duracaoSegundos);

                    // Speech/music classification for audio files
                    if (resultado.categoria == matriz::ingest::CategoriaMidia::Audio &&
                        resultado.analise.leitura.duracaoSegundos &&
                        *resultado.analise.leitura.duracaoSegundos > 0.5) {
                        try {
                            resultado.classificacao = matriz::ingest::classificarFalaMusica(
                                arquivo, pastaProjeto,
                                *resultado.analise.leitura.duracaoSegundos);
                        } catch (...) {}
                    }

                    auto conhecido = matriz::ingest::buscarAssetPorChecksum(
                        *registro, resultado.analise.checksums.sha256);
                    if (!conhecido) {
                        double dur = resultado.analise.leitura.duracaoSegundos.value_or(0.0);
                        int w = resultado.analise.leitura.larguraPx.value_or(0);
                        int h = resultado.analise.leitura.alturaPx.value_or(0);
                        std::string tit = arquivo.getFileNameWithoutExtension().toStdString();
                        std::string ext = arquivo.getFileExtension().replaceCharacter('.', ' ').trim().toStdString();
                        std::string relPath = matriz::vault::caminhoRelativoAoVolume(arquivo);
                        conhecido = matriz::ingest::buscarAssetPorMetadados(*registro, tit, ext, dur, w, h, arquivo.getSize(), relPath, "", pastaProjeto);
                    }
                    if (conhecido) {
                        resultado.ehDuplicata = true;
                        resultado.itemIdExistente = conhecido->codigoAcervo;
                    }
                } catch (const std::exception& e) {
                    resultado.erro = true;
                    resultado.erroPorque = e.what();
                }

                {
                    const std::lock_guard<std::mutex> lock(*mutexRes);
                    juce::MessageManager::callAsync([safeThis, i, resultado = std::move(resultado)]() mutable {
                        if (!safeThis) return;
                        auto* self = safeThis.getComponent();
                        if (i < static_cast<int>(self->resultados_.size())) {
                            self->resultados_[static_cast<size_t>(i)] = std::move(resultado);
                        } else {
                            juce::Logger::writeToLog("DROP result i=" + juce::String(i)
                                + " size=" + juce::String((int)self->resultados_.size()));
                        }
                    });
                }

                analisadosPtr->fetch_add(1);
            });
        }

        // Wait for all analysis jobs to complete. Do NOT use
        // removeAllJobs - it drops queued jobs that haven't started yet.
        while (analisadosPtr->load() < total) {
            juce::Thread::sleep(50);
            if (!safeThis) break;
        }

        juce::MessageManager::callAsync([safeThis]() {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            self->descobertaEmAndamento_ = false;
            self->stopTimer();
            if (self->etapa_ == Etapa::Discovering)
                self->irParaEtapa(Etapa::Review);
        });
    });
}

// -- Import: write analyzed results to DB --

void IngestWizardComponent::iniciarImportacao() {
    irParaEtapa(Etapa::Importing);

    *importados_ = 0;
    novosImportados_ = 0;
    duplicatasDetectadas_ = 0;
    errosDetectados_ = 0;

    std::vector<size_t> indices;
    for (size_t i = 0; i < resultados_.size(); ++i) {
        if (!resultados_[i].erro) indices.push_back(i);
    }
    totalParaImportar_ = static_cast<int>(indices.size());
    startTimer(100);

    auto registro = &projeto_.projeto().registro();
    auto indice = &projeto_.projeto().indice();
    juce::File pastaProjeto = projeto_.projeto().pasta();

    auto stmtProjeto = registro->prepare("SELECT id, prefixo_nomenclatura FROM projeto LIMIT 1");
    if (!stmtProjeto.step()) return;
    std::string projetoId = stmtProjeto.columnText(0);
    juce::String prefixo = stmtProjeto.columnText(1);

    auto importadosPtr = importados_;
    auto escritaRegistro = std::make_shared<std::mutex>();
    juce::Component::SafePointer<IngestWizardComponent> safeThis(this);

    // Create item rows synchronously (fast)
    std::vector<std::string> itemIds;
    itemIds.reserve(indices.size());
    registro->run("BEGIN", {});
    try {
        for (size_t idx : indices) {
            std::string itemId = matriz::model::novoUuid();
            std::string agora = matriz::model::agoraIso8601();

            auto ext = resultados_[idx].arquivo.getFileExtension();
            auto cat = matriz::ingest::categoriaPorExtensao(ext);
            std::string tipoMidia;
            switch (cat) {
                case matriz::ingest::CategoriaMidia::Audio:     tipoMidia = "digital_audio"; break;
                case matriz::ingest::CategoriaMidia::Video:     tipoMidia = "digital_video"; break;
                case matriz::ingest::CategoriaMidia::Imagem:    tipoMidia = "foto"; break;
                case matriz::ingest::CategoriaMidia::Documento: tipoMidia = "documento"; break;
                case matriz::ingest::CategoriaMidia::Texto:     tipoMidia = "documento"; break;
                case matriz::ingest::CategoriaMidia::Sessao:    tipoMidia = "sessao"; break;
                default: break;
            }

            std::string estado = tipoMidia.empty() ? "capturado" : "catalogado";
            auto tipoVal = tipoMidia.empty() ? matriz::db::Value::null() : matriz::db::Value::of(tipoMidia);

            registro->run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, em_quarentena, criado_em, atualizado_em) "
                "VALUES (?, ?, NULL, ?, ?, ?, 1, ?, ?)",
                {matriz::db::Value::of(itemId), matriz::db::Value::of(projetoId),
                 matriz::db::Value::of(resultados_[idx].arquivo.getFileName().toStdString()),
                 tipoVal, matriz::db::Value::of(estado),
                 matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
            itemIds.push_back(itemId);
        }
        registro->run("COMMIT", {});
    } catch (...) {
        registro->run("ROLLBACK", {});
        irParaEtapa(Etapa::Failed);
        return;
    }

    // Background: write analysis results using the SAME AnaliseDeArquivo from discovery
    for (size_t j = 0; j < indices.size(); ++j) {
        size_t idx = indices[j];
        std::string itemId = itemIds[j];
        auto& res = resultados_[idx];

        pool_.addJob([this, registro, indice, pastaProjeto, itemId, idx, prefixo,
                      escritaRegistro, importadosPtr, safeThis]() {
            auto& res = resultados_[idx];
            bool sucesso = false;
            bool duplicata = false;

            try {
                const std::lock_guard<std::mutex> lock(*escritaRegistro);

                if (res.ehDuplicata) {
                    bool duplicadoPorChecksum = false;
                    auto conhecido = matriz::ingest::buscarAssetPorChecksum(*registro, res.analise.checksums.sha256);
                    if (conhecido) {
                        duplicadoPorChecksum = true;
                    } else {
                        double dur = res.analise.leitura.duracaoSegundos.value_or(0.0);
                        int w = res.analise.leitura.larguraPx.value_or(0);
                        int h = res.analise.leitura.alturaPx.value_or(0);
                        std::string tit = res.arquivo.getFileNameWithoutExtension().toStdString();
                        std::string ext = res.arquivo.getFileExtension().replaceCharacter('.', ' ').trim().toStdString();
                        std::string relPath = matriz::vault::caminhoRelativoAoVolume(res.arquivo);
                        conhecido = matriz::ingest::buscarAssetPorMetadados(*registro, tit, ext, dur, w, h, res.arquivo.getSize(), relPath, "", pastaProjeto);
                    }

                    if (conhecido) {
                        juce::String nota;
                        if (duplicadoPorChecksum) {
                            matriz::ingest::registrarLocalizacaoConhecida(
                                *registro, conhecido->arquivoId, res.arquivo);
                            nota = "Same content as " + juce::String(conhecido->codigoAcervo) +
                                   " - already in the archive.";
                        } else {
                            nota = "Possible duplicate of " + juce::String(conhecido->codigoAcervo) +
                                   " (matched name, format, duration/dimensions). Flagged as duplicate.";
                        }

                        PapelInfo papelInfo = papelPorCategoria(res.categoria);
                        auto resultado = matriz::ingest::gravarArquivoAnalisado(
                            *registro, itemId, res.analise, papelInfo.papel, papelInfo.ehMaster);

                        int proximoNumero = 1;
                        auto stmtContagem = registro->prepare(
                            "SELECT COALESCE(MAX(CAST(SUBSTR(codigo_acervo, INSTR(codigo_acervo, '-') + 1) AS INTEGER)), 0) "
                            "FROM item WHERE codigo_acervo LIKE ?");
                        stmtContagem.bind(1, matriz::db::Value::of(prefixo.toStdString() + "-%"));
                        if (stmtContagem.step())
                            proximoNumero = static_cast<int>(stmtContagem.columnInt(0)) + 1;
                        juce::String codigo = prefixo + "-" + juce::String(proximoNumero).paddedLeft('0', 5);

                        std::string tipoMidia = "";
                        if (res.categoria == matriz::ingest::CategoriaMidia::Audio) tipoMidia = "digital_audio";
                        else if (res.categoria == matriz::ingest::CategoriaMidia::Video) tipoMidia = "digital_video";
                        else if (res.categoria == matriz::ingest::CategoriaMidia::Imagem) tipoMidia = "foto";
                        else if (res.categoria == matriz::ingest::CategoriaMidia::Documento) tipoMidia = "documento";
                        else if (res.categoria == matriz::ingest::CategoriaMidia::Texto) tipoMidia = "documento";
                        else if (res.categoria == matriz::ingest::CategoriaMidia::Sessao) tipoMidia = "sessao";

                        registro->run("UPDATE item SET codigo_acervo = ?, estado = 'duplicata', tipo_midia = ?, notas_livres = ? WHERE id = ?",
                                      {matriz::db::Value::of(codigo.toStdString()),
                                       tipoMidia.empty() ? matriz::db::Value::null() : matriz::db::Value::of(tipoMidia),
                                       matriz::db::Value::of(nota.toStdString()),
                                       matriz::db::Value::of(itemId)});

                        matriz::ingest::gravarCache(*registro, resultado.arquivoId, res.cache);

                        matriz::ingest::gerarEGravarMiniaturaPrincipal(
                            *indice, pastaProjeto, itemId, resultado.arquivoId,
                            res.arquivo, res.categoria, res.analise.leitura.duracaoSegundos);

                        duplicata = true;
                        sucesso = true;
                    }
                }

                if (!duplicata) {
                    PapelInfo papelInfo = papelPorCategoria(res.categoria);
                    auto resultado = matriz::ingest::gravarArquivoAnalisado(
                        *registro, itemId, res.analise, papelInfo.papel, papelInfo.ehMaster);

                    // Sequencial permanente SÓ no sucesso (critério 6).
                    // Busca o maior sufixo numérico existente para este prefixo no banco,
                    // evitando conflitos causados por itens excluídos ou lacunas de id.
                    int proximoNumero = 1;
                    auto stmtContagem = registro->prepare(
                        "SELECT COALESCE(MAX(CAST(SUBSTR(codigo_acervo, INSTR(codigo_acervo, '-') + 1) AS INTEGER)), 0) "
                        "FROM item WHERE codigo_acervo LIKE ?");
                    stmtContagem.bind(1, matriz::db::Value::of(prefixo.toStdString() + "-%"));
                    if (stmtContagem.step())
                        proximoNumero = static_cast<int>(stmtContagem.columnInt(0)) + 1;
                    juce::String codigo = prefixo + "-" + juce::String(proximoNumero).paddedLeft('0', 5);

                    std::string tipoMidia = "";
                    if (res.categoria == matriz::ingest::CategoriaMidia::Audio) tipoMidia = "digital_audio";
                    else if (res.categoria == matriz::ingest::CategoriaMidia::Video) tipoMidia = "digital_video";
                    else if (res.categoria == matriz::ingest::CategoriaMidia::Imagem) tipoMidia = "foto";
                    else if (res.categoria == matriz::ingest::CategoriaMidia::Documento) tipoMidia = "documento";
                    else if (res.categoria == matriz::ingest::CategoriaMidia::Texto) tipoMidia = "documento";
                    else if (res.categoria == matriz::ingest::CategoriaMidia::Sessao) tipoMidia = "sessao";

                    registro->run("UPDATE item SET codigo_acervo = ?, estado = 'novo', tipo_midia = ? WHERE id = ?",
                                  {matriz::db::Value::of(codigo.toStdString()),
                                   tipoMidia.empty() ? matriz::db::Value::null() : matriz::db::Value::of(tipoMidia),
                                   matriz::db::Value::of(itemId)});

                    if (!tipoMidia.empty()) {
                        std::string agoraStr = matriz::model::agoraIso8601();
                        if (tipoMidia == "digital_audio" || tipoMidia == "digital_video" || tipoMidia == "3d_file" || tipoMidia == "sample" || tipoMidia == "release") {
                            registro->run(
                                "INSERT OR IGNORE INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                                "VALUES (?, ?, 'raiz', 0, 'origem', 'Digital', 'leitura_tecnica', ?)",
                                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                                 matriz::db::Value::of(agoraStr)});
                        } else if (tipoMidia == "cassete" || tipoMidia == "fita_rolo" || tipoMidia == "vinil" || tipoMidia == "dat" || tipoMidia == "minidisc") {
                            registro->run(
                                "INSERT OR IGNORE INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                                "VALUES (?, ?, 'raiz', 0, 'origem', 'Analógico', 'leitura_tecnica', ?)",
                                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                                 matriz::db::Value::of(agoraStr)});
                        }
                    }

                    matriz::ingest::gravarCache(*registro, resultado.arquivoId, res.cache);

                    matriz::ingest::gerarEGravarMiniaturaPrincipal(
                        *indice, pastaProjeto, itemId, resultado.arquivoId,
                        res.arquivo, res.categoria, res.analise.leitura.duracaoSegundos);

                    if (res.classificacao && res.classificacao->rotulo) {
                        std::string val = *res.classificacao->rotulo;
                        if (val == "musica") val = "música"; // Map to accented option in Ficha YAML

                        std::string sugId = matriz::model::novoUuid();
                        std::string agora = matriz::model::agoraIso8601();
                        indice->run(
                            "INSERT INTO sugestao_campo (id, item_id, nivel, nivel_indice, campo_id, valor, confianca, modelo, modelo_versao, processado_em, confirmado) "
                            "VALUES (?, ?, 'raiz', 0, 'natureza', ?, ?, ?, ?, ?, 0)",
                            {matriz::db::Value::of(sugId), matriz::db::Value::of(itemId),
                             matriz::db::Value::of(val), matriz::db::Value::of(res.classificacao->confianca),
                             matriz::db::Value::of("classificador_fala_musica"), matriz::db::Value::of("v1"),
                             matriz::db::Value::of(agora)});
                    }

                    sucesso = true;
                }
            } catch (const std::exception&) {
                try {
                    const std::lock_guard<std::mutex> lock(*escritaRegistro);
                    registro->run("DELETE FROM item WHERE id = ?",
                                  {matriz::db::Value::of(itemId)});
                } catch (...) {}
            }

            juce::MessageManager::callAsync([safeThis, sucesso, duplicata, itemId]() {
                if (!safeThis) return;
                auto* self = safeThis.getComponent();
                if (sucesso) {
                    if (duplicata) {
                        ++self->duplicatasDetectadas_;
                    } else {
                        ++self->novosImportados_;
                        self->itemIdsImportados_.push_back(itemId);
                    }
                } else {
                    ++self->errosDetectados_;
                }
            });

            importadosPtr->fetch_add(1);
        });
    }
}

// -- Timer: update progress UI during discovery and import --

void IngestWizardComponent::timerCallback() {
    if (etapa_ == Etapa::Discovering && totalParaAnalisar_ > 0) {
        int done = analisados_->load();
        progressoValor_ = static_cast<double>(done) / totalParaAnalisar_;
        if (labelProgresso_) {
            auto texto = matriz::i18n::t("ingest.descobre_analisando")
                             .replace("{0}", juce::String(done))
                             .replace("{1}", juce::String(totalParaAnalisar_));
            labelProgresso_->setText(texto, juce::dontSendNotification);
        }
        repaint();
    }

    if (etapa_ == Etapa::Importing && totalParaImportar_ > 0) {
        int done = importados_->load();
        progressoValor_ = static_cast<double>(done) / totalParaImportar_;
        if (labelProgresso_) {
            auto texto = matriz::i18n::t("ingest.importando_progresso")
                             .replace("{0}", juce::String(done))
                             .replace("{1}", juce::String(totalParaImportar_));
            labelProgresso_->setText(texto, juce::dontSendNotification);
        }

        if (done >= totalParaImportar_) {
            stopTimer();
            irParaEtapa(Etapa::Done);
        }
        repaint();
    }
}

// -- Drag and drop support for source step --

bool IngestWizardComponent::isInterestedInFileDrag(const juce::StringArray&) {
    return etapa_ == Etapa::Source;
}

void IngestWizardComponent::filesDropped(const juce::StringArray& files, int, int) {
    if (etapa_ != Etapa::Source || files.isEmpty()) return;
    juce::File first(files[0]);
    if (first.isDirectory())
        iniciarDescoberta(first);
    else
        iniciarDescoberta(first.getParentDirectory());
}

// -- Paint --

void IngestWizardComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);

    auto area = getLocalBounds();
    pintarBarraEtapas(g, area.removeFromTop(kAlturaBarraEtapas));
}

void IngestWizardComponent::pintarBarraEtapas(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(tema().painel);
    g.fillRect(area);
    g.setColour(tema().borda);
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    struct EtapaLabel {
        const char* chave;
        Etapa etapa;
    };
    EtapaLabel etapas[] = {
        {"ingest.step_source",   Etapa::Source},
        {"ingest.step_descobre", Etapa::Discovering},
        {"ingest.step_review",   Etapa::Review},
        {"ingest.step_importar", Etapa::ReadyToImport},
        {"ingest.step_verificar", Etapa::Verifying},
        {"ingest.step_concluido", Etapa::Done},
    };
    int nEtapas = 6;
    int larguraEtapa = area.getWidth() / nEtapas;

    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));

    for (int i = 0; i < nEtapas; ++i) {
        auto r = area.withX(area.getX() + i * larguraEtapa).withWidth(larguraEtapa);
        bool atual = (etapa_ == etapas[i].etapa) ||
                     (etapa_ == Etapa::Importing && etapas[i].etapa == Etapa::ReadyToImport);
        bool passada = static_cast<int>(etapa_) > static_cast<int>(etapas[i].etapa);

        if (atual) {
            g.setColour(tema().acento.withAlpha(0.2f));
            g.fillRect(r);
            g.setColour(tema().acento);
            g.fillRect(r.getX(), r.getBottom() - 3, r.getWidth(), 3);
        }

        g.setColour(atual ? tema().textoPrimario : (passada ? tema().textoSecundario : tema().textoTerciario));
        g.drawText(matriz::i18n::t(etapas[i].chave), r, juce::Justification::centred, true);
    }
}

// -- Layout --

void IngestWizardComponent::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(kAlturaBarraEtapas);

    constexpr int kLargura = 720;
    constexpr int kMargemBotao = 32;

    auto centro = area.withSizeKeepingCentre(kLargura, area.getHeight());

    int y = centro.getY() + centro.getHeight() / 4;

    if (tituloEtapa_) {
        tituloEtapa_->setBounds(centro.getX(), y, kLargura, 30);
        y += 36;
    }
    if (subtituloEtapa_) {
        int h = (etapa_ == Etapa::Review) ? 60 : 24;
        subtituloEtapa_->setBounds(centro.getX(), y, kLargura, h);
        y += h + 8;
    }
    if (labelResumo_) {
        labelResumo_->setBounds(centro.getX(), y, kLargura, 24);
        y += 28;
    }
    if (labelResumo2_) {
        labelResumo2_->setBounds(centro.getX(), y, kLargura, 20);
        y += 24;
    }
    if (labelResumo3_) {
        labelResumo3_->setBounds(centro.getX(), y, kLargura, 20);
        y += 24;
    }
    if (labelResumo4_) {
        labelResumo4_->setBounds(centro.getX(), y, kLargura, 20);
        y += 24;
    }
    if (labelProgresso_) {
        labelProgresso_->setBounds(centro.getX(), y, kLargura, 28);
        y += 32;
    }
    if (barraProgresso_) {
        barraProgresso_->setBounds(centro.getX() + 20, y, kLargura - 40, 20);
        y += 32;
    }

    y += kMargemBotao;

    int btnW = 160;
    int btnH = 32;
    int btnX = centro.getCentreX() - btnW / 2;

    if (etapa_ == Etapa::Done) {
        int doneBtnW = 240;
        int doneBtnX = centro.getCentreX() - doneBtnW / 2;

        if (btnViewNew_) { btnViewNew_->setBounds(doneBtnX, y, doneBtnW, btnH); y += btnH + 8; }
        if (btnViewAll_) { btnViewAll_->setBounds(doneBtnX, y, doneBtnW, btnH); y += btnH + 8; }
        if (btnReviewIssues_) { btnReviewIssues_->setBounds(doneBtnX, y, doneBtnW, btnH); y += btnH + 8; }
        if (btnReturnHome_) { btnReturnHome_->setBounds(doneBtnX, y, doneBtnW, btnH); y += btnH + 8; }
    } else {
        if (btnBrowse_) {
            btnBrowse_->setBounds(btnX, y, btnW, btnH);
            y += btnH + 8;
        }
        if (btnProximo_ && btnVoltar_) {
            int gap = 12;
            int totalW = btnW * 2 + gap;
            int startX = centro.getCentreX() - totalW / 2;
            btnVoltar_->setBounds(startX, y, btnW, btnH);
            btnProximo_->setBounds(startX + btnW + gap, y, btnW, btnH);
            y += btnH + 8;
        } else if (btnProximo_) {
            btnProximo_->setBounds(btnX, y, btnW, btnH);
            y += btnH + 8;
        }
        if (btnCancelar_) {
            btnCancelar_->setBounds(btnX, y, btnW, btnH);
        }
    }
}

} // namespace matriz::ui
