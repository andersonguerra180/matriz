#include "MainComponent.h"

#include "../I18n/Strings.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/LeituraTecnica.h"
#include "FichaPanelComponent.h"
#include "MosaicoComponent.h"
#include "SelecionarTipoMidiaDialogo.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

struct PapelInfo {
    std::string papel;
    bool ehMaster;
};

// Papel do arquivo (§5.4) derivado do tipo de mídia que o OPERADOR escolheu
// (nunca mais adivinhado pela extensão — era exatamente o bug reportado:
// ingerir um CD virava fita_rolo e pedia espessura de fita). A categoria do
// arquivo só desempata quando o tipo escolhido aceita mais de um papel
// (release: áudio é master, imagem é capa, o resto é encarte).
PapelInfo papelParaTipoECategoria(const std::string& tipoMidia, matriz::ingest::CategoriaMidia categoria) {
    static const std::set<std::string> kTiposAudioVideo = {"fita_rolo", "cassete", "vinil", "dat", "minidisc",
                                                             "cd", "filme", "video", "sample"};
    if (kTiposAudioVideo.count(tipoMidia)) return {"preservation_master", true};
    if (tipoMidia == "foto" || tipoMidia == "negativo" || tipoMidia == "slide") return {"foto_suporte", false};
    if (tipoMidia == "documento") return {"documento", false};
    if (tipoMidia == "release") {
        if (categoria == matriz::ingest::CategoriaMidia::Audio || categoria == matriz::ingest::CategoriaMidia::Video)
            return {"master", true};
        if (categoria == matriz::ingest::CategoriaMidia::Imagem) return {"capa_frente", false};
        return {"encarte", false};
    }
    // Fallback genérico pra qualquer tipo futuro não listado acima.
    if (categoria == matriz::ingest::CategoriaMidia::Audio || categoria == matriz::ingest::CategoriaMidia::Video)
        return {"preservation_master", true};
    return {"documento", false};
}

// Estado compartilhado de um lote de ingest em background — todo acesso
// protegido por lock porque `erros`/`sucessos` são tocados tanto pela
// thread do ThreadPool (que só processa um arquivo por vez, mas ainda é uma
// thread diferente da de mensagens) quanto pelo callback assíncrono.
struct EstadoLote {
    juce::CriticalSection lock;
    std::vector<juce::String> erros;
    int sucessos = 0;
};

} // namespace

MainComponent::MainComponent() { reconstruirTelaInicial(); }

MainComponent::~MainComponent() { ingestPool_.removeAllJobs(true, 5000); }

void MainComponent::reconstruirTelaInicial() {
    mosaicoViewport_.reset();
    mosaico_.reset();
    visualizadorPlaceholder_.reset();
    fichaPanel_.reset();

    telaInicialTitulo_ = std::make_unique<juce::Label>();
    telaInicialTitulo_->setText(matriz::i18n::t("tela_inicial.titulo"), juce::dontSendNotification);
    telaInicialTitulo_->setJustificationType(juce::Justification::centred);
    telaInicialTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteTitulo, juce::Font::bold)));
    telaInicialTitulo_->setColour(juce::Label::textColourId, tema().textoPrimario);
    addAndMakeVisible(*telaInicialTitulo_);

    telaInicialSubtitulo_ = std::make_unique<juce::Label>();
    telaInicialSubtitulo_->setText(matriz::i18n::t("tela_inicial.subtitulo"), juce::dontSendNotification);
    telaInicialSubtitulo_->setJustificationType(juce::Justification::centred);
    telaInicialSubtitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    telaInicialSubtitulo_->setColour(juce::Label::textColourId, tema().textoSecundario);
    addAndMakeVisible(*telaInicialSubtitulo_);

    telaInicialBotaoNovo_ = std::make_unique<juce::TextButton>(matriz::i18n::t("tela_inicial.botao_novo"));
    telaInicialBotaoNovo_->onClick = [this] { if (aoPedirNovoProjeto) aoPedirNovoProjeto(); };
    addAndMakeVisible(*telaInicialBotaoNovo_);

    telaInicialBotaoAbrir_ = std::make_unique<juce::TextButton>(matriz::i18n::t("tela_inicial.botao_abrir"));
    telaInicialBotaoAbrir_->onClick = [this] { if (aoPedirAbrirProjeto) aoPedirAbrirProjeto(); };
    addAndMakeVisible(*telaInicialBotaoAbrir_);

    resized();
    repaint();
}

void MainComponent::reconstruirLayoutProjeto() {
    telaInicialTitulo_.reset();
    telaInicialSubtitulo_.reset();
    telaInicialBotaoNovo_.reset();
    telaInicialBotaoAbrir_.reset();

    mosaico_ = std::make_unique<MosaicoComponent>(*projetoAberto_);
    mosaico_->aoSelecionar = [this](const std::string& itemId) { selecionarItem(itemId); };
    mosaico_->aoArquivosSoltos = [this](const juce::Array<juce::File>& arquivos) { ingerirArquivos(arquivos); };

    mosaicoViewport_ = std::make_unique<juce::Viewport>();
    mosaicoViewport_->setViewedComponent(mosaico_.get(), false);
    addAndMakeVisible(*mosaicoViewport_);

    // Visualizador (vídeo/imagem/vazio) + transporte + tira de diagnóstico
    // são B.1.4/B.1.5 — fronteira de etapa. Por enquanto, área reservada e
    // vazia no layout (§11.1 já define o espaço; o conteúdo vem depois).
    visualizadorPlaceholder_ = std::make_unique<juce::Component>();
    addAndMakeVisible(*visualizadorPlaceholder_);

    fichaPanel_ = std::make_unique<FichaPanelComponent>(*projetoAberto_);
    addAndMakeVisible(*fichaPanel_);

    mosaico_->recarregar();

    resized();
    repaint();
}

void MainComponent::abrirProjeto(std::unique_ptr<matriz::model::Project> projeto) {
    projetoAberto_ = std::make_unique<ProjetoAberto>(std::move(projeto));
    reconstruirLayoutProjeto();
}

void MainComponent::fecharProjeto() {
    // Um lote em background ainda segura uma referência a registro_ —
    // destruir o projeto agora deixaria o job com uma referência
    // pendurada. O menu já desabilita esta ação durante o ingest; esta
    // checagem é o cinto de segurança pra qualquer outro chamador (ex.:
    // testes headless).
    if (ingestEmAndamento()) return;
    projetoAberto_.reset();
    reconstruirTelaInicial();
}

std::unique_ptr<matriz::model::Project> MainComponent::destacarProjeto() {
    if (!projetoAberto_) return nullptr;
    jassert(!ingestEmAndamento());
    return projetoAberto_->destacarProjeto();
}

void MainComponent::selecionarItem(const std::string& itemId) {
    if (fichaPanel_) fichaPanel_->mostrarItem(itemId);
}

void MainComponent::ingerirArquivos(const juce::Array<juce::File>& arquivosOuPastas) {
    if (!projetoAberto_) return;

    if (ingestEmAndamento()) {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::InfoIcon)
                                          .withTitle(matriz::i18n::t("ingest.titulo"))
                                          .withMessage(matriz::i18n::t("ingest.ja_em_andamento"))
                                          .withButton(matriz::i18n::t("comum.ok")),
                                      static_cast<juce::ModalComponentManager::Callback*>(nullptr));
        return;
    }

    auto arquivos = expandirArquivos(arquivosOuPastas);
    if (arquivos.empty()) return;

    auto opcoes = listarTiposMidiaDisponiveis(*projetoAberto_);
    mostrarDialogoSelecionarTipoMidia(opcoes, static_cast<int>(arquivos.size()),
                                       [this, arquivos](std::optional<std::string> tipoEscolhido) {
                                           if (!tipoEscolhido) return;
                                           processarLoteEmBackground(arquivos, *tipoEscolhido);
                                       });
}

void MainComponent::ingerirArquivosComTipoConhecido(const juce::Array<juce::File>& arquivosOuPastas,
                                                      std::string tipoMidia) {
    if (!projetoAberto_ || ingestEmAndamento()) return;
    auto arquivos = expandirArquivos(arquivosOuPastas);
    if (arquivos.empty()) return;
    processarLoteEmBackground(arquivos, std::move(tipoMidia));
}

std::vector<juce::File> MainComponent::expandirArquivos(const juce::Array<juce::File>& arquivosOuPastas) const {
    // Pastas entram recursivamente como arquivos individuais — ainda não é
    // a inferência de estrutura real do §7.3 (release/faixa/capa por
    // convenção de nome), só o mínimo pra "arrastei uma pasta" funcionar.
    std::vector<juce::File> arquivos;
    for (auto& entrada : arquivosOuPastas) {
        if (entrada.isDirectory()) {
            for (auto& sub : juce::RangedDirectoryIterator(entrada, true, "*", juce::File::findFiles))
                arquivos.push_back(sub.getFile());
        } else {
            arquivos.push_back(entrada);
        }
    }
    return arquivos;
}

void MainComponent::processarLoteEmBackground(std::vector<juce::File> arquivos, std::string tipoMidia) {
    if (!projetoAberto_) return;

    // Ponteiro, não referência: a referência local morreria ao sair desta
    // função, mas os jobs do ThreadPool rodam depois disso — precisam de
    // algo que sobreviva. O objeto em si só é seguro enquanto o projeto
    // continuar aberto, por isso o menu (e fecharProjeto()) ficam travados
    // com ingestEmAndamento() até o último job terminar.
    matriz::db::Database* registro = &projetoAberto_->projeto().registro();
    juce::File pastaProjeto = projetoAberto_->projeto().pasta();

    auto stmtProjeto = registro->prepare("SELECT id, prefixo_nomenclatura FROM projeto LIMIT 1");
    if (!stmtProjeto.step()) return;
    std::string projetoId = stmtProjeto.columnText(0);
    juce::String prefixo = stmtProjeto.columnText(1);

    auto stmtContagem = registro->prepare("SELECT COUNT(*) FROM item");
    stmtContagem.step();
    int proximoNumero = static_cast<int>(stmtContagem.columnInt(0)) + 1;

    ingestsTotalLote_ = static_cast<int>(arquivos.size());
    ingestsPendentes_ = static_cast<int>(arquivos.size());
    ingestsErrosLote_ = 0;
    atualizarLabelProgresso();

    auto estadoLote = std::make_shared<EstadoLote>();
    juce::Component::SafePointer<MainComponent> safeThis(this);

    for (auto& arquivo : arquivos) {
        int numeroLocal = proximoNumero++;

        // A cópia + checksum + ffprobe/Exiv2 (matriz::ingest::ingerirArquivo)
        // rodava direto na thread de mensagens antes — era exatamente o
        // travamento reportado. Agora cada arquivo é um job no ThreadPool
        // (1 thread — sequencial, sem concorrência de escrita no registro).
        ingestPool_.addJob([registro, pastaProjeto, projetoId, prefixo, numeroLocal, arquivo, tipoMidia, estadoLote,
                             safeThis]() mutable {
            juce::String erro;
            bool sucesso = false;
            try {
                auto categoria = matriz::ingest::categoriaPorExtensao(arquivo);
                PapelInfo papelInfo = papelParaTipoECategoria(tipoMidia, categoria);

                std::string itemId = matriz::model::novoUuid();
                std::string agora = matriz::model::agoraIso8601();
                juce::String codigo = prefixo + "-" + juce::String(numeroLocal).paddedLeft('0', 4);

                registro->run(
                    "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
                    "VALUES (?, ?, ?, ?, ?, 'capturado', ?, ?)",
                    {matriz::db::Value::of(itemId), matriz::db::Value::of(projetoId),
                     matriz::db::Value::of(codigo.toStdString()),
                     matriz::db::Value::of(arquivo.getFileNameWithoutExtension().toStdString()),
                     matriz::db::Value::of(tipoMidia), matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

                matriz::ingest::ingerirArquivo(*registro, pastaProjeto, itemId, arquivo, papelInfo.papel,
                                                papelInfo.ehMaster);
                sucesso = true;
            } catch (const std::exception& e) {
                erro = arquivo.getFileName() + ": " + juce::String(e.what());
            }

            {
                const juce::ScopedLock sl(estadoLote->lock);
                if (sucesso) ++estadoLote->sucessos;
                else estadoLote->erros.push_back(erro);
            }

            juce::MessageManager::callAsync([safeThis, estadoLote]() {
                if (!safeThis) return;
                auto* self = safeThis.getComponent();
                self->ingestsPendentes_.fetch_sub(1);
                self->atualizarLabelProgresso();

                if (self->ingestsPendentes_.load() > 0) return;

                // Lote inteiro terminou.
                if (self->mosaico_) self->mosaico_->recarregar();

                int sucessos;
                juce::StringArray erros;
                {
                    const juce::ScopedLock sl(estadoLote->lock);
                    sucessos = estadoLote->sucessos;
                    for (auto& e : estadoLote->erros) erros.add(e);
                }

                if (self->aoConcluirLoteIngestParaTeste) {
                    self->aoConcluirLoteIngestParaTeste(sucessos, erros);
                    return;
                }

                juce::String resumo = matriz::i18n::t("ingest.resumo").replace("{n}", juce::String(sucessos));
                if (!erros.isEmpty())
                    resumo += "\n\n" + matriz::i18n::t("ingest.resumo_erros") + "\n" + erros.joinIntoString("\n");

                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(erros.isEmpty() ? juce::MessageBoxIconType::InfoIcon
                                                       : juce::MessageBoxIconType::WarningIcon)
                        .withTitle(matriz::i18n::t("ingest.titulo"))
                        .withMessage(resumo)
                        .withButton(matriz::i18n::t("comum.ok")),
                    static_cast<juce::ModalComponentManager::Callback*>(nullptr));
            });
        });
    }
}

void MainComponent::atualizarLabelProgresso() {
    int pendentes = ingestsPendentes_.load();
    if (pendentes <= 0) {
        labelProgressoIngest_.reset();
        repaint();
        return;
    }

    if (!labelProgressoIngest_) {
        labelProgressoIngest_ = std::make_unique<juce::Label>();
        labelProgressoIngest_->setJustificationType(juce::Justification::centred);
        labelProgressoIngest_->setColour(juce::Label::textColourId, tema().textoSobreAcento);
        labelProgressoIngest_->setColour(juce::Label::backgroundColourId, tema().acento.withAlpha(0.92f));
        labelProgressoIngest_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        addAndMakeVisible(*labelProgressoIngest_);
        resized();
    }

    int total = ingestsTotalLote_.load();
    int concluidos = total - pendentes;
    labelProgressoIngest_->setText(
        matriz::i18n::t("ingest.progresso").replace("{feito}", juce::String(concluidos)).replace("{total}", juce::String(total)),
        juce::dontSendNotification);
}

void MainComponent::paint(juce::Graphics& g) { g.fillAll(tema().fundo); }

void MainComponent::resized() {
    auto area = getLocalBounds();

    if (!temProjetoAberto()) {
        auto centro = area.withSizeKeepingCentre(360, 180);
        telaInicialTitulo_->setBounds(centro.removeFromTop(32));
        centro.removeFromTop(tema().espacoMedio);
        telaInicialSubtitulo_->setBounds(centro.removeFromTop(40));
        centro.removeFromTop(tema().espacoGrande);
        auto botoes = centro.removeFromTop(32);
        telaInicialBotaoNovo_->setBounds(botoes.removeFromLeft(170));
        botoes.removeFromLeft(tema().espacoMedio);
        telaInicialBotaoAbrir_->setBounds(botoes.removeFromLeft(170));
        return;
    }

    constexpr int kLarguraMosaico = 320;
    constexpr int kLarguraFicha = 340;

    mosaicoViewport_->setBounds(area.removeFromLeft(kLarguraMosaico));
    fichaPanel_->setBounds(area.removeFromRight(kLarguraFicha));
    visualizadorPlaceholder_->setBounds(area);

    if (mosaico_) {
        mosaico_->setSize(mosaicoViewport_->getWidth() - mosaicoViewport_->getScrollBarThickness(), mosaico_->getHeight());
        mosaico_->resized();
    }

    if (labelProgressoIngest_) labelProgressoIngest_->setBounds(getLocalBounds().removeFromTop(28));
}

} // namespace matriz::ui
