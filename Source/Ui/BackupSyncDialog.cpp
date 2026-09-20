#include "BackupSyncDialog.h"

#include <atomic>
#include <thread>
#include <chrono>

#include "../Ingest/Checksum.h"
#include "../Model/ProjectLog.h"
#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

#include <iomanip>
#include <sstream>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#endif

namespace {

static std::string calcularSha256Chunked(
    const juce::File& file,
    const std::function<void(juce::int64 bytesLidos, juce::int64 totalBytes)>& onProgress,
    std::atomic<bool>* cancelamento)
{
    if (!file.existsAsFile()) return "";
    juce::FileInputStream stream(file);
    if (stream.failedToOpen()) return "";

    juce::int64 total = stream.getTotalLength();
    juce::int64 lidos = 0;

#ifdef __APPLE__
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    constexpr int bufSize = 131072; // 128 KB
    char buffer[bufSize];

    while (!stream.isExhausted()) {
        if (cancelamento && cancelamento->load()) return "";
        int r = stream.read(buffer, bufSize);
        if (r <= 0) break;
        CC_SHA256_Update(&ctx, buffer, static_cast<CC_LONG>(r));
        lidos += r;
        if (onProgress) onProgress(lidos, total);
    }

    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);

    std::stringstream ss;
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return ss.str();
#else
    return juce::SHA256(file).toHexString().toLowerCase().toStdString();
#endif
}

// ==============================================================================
// 1. Modal de Seleção de Versões (Origem -> Destino)
// ==============================================================================
class SyncSelectComponent : public juce::Component {
public:
    SyncSelectComponent(ProjetoAberto& projeto,
                        const std::vector<BackupVersionRef>& versoes,
                        std::function<void(const BackupVersionRef& fonte, const BackupVersionRef& alvo)> onComparar,
                        std::function<void()> onCancelar)
        : projeto_(projeto), versoes_(versoes), onComparar_(std::move(onComparar)), onCancelar_(std::move(onCancelar))
    {
        const auto& tk = tema();
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

        lblTitulo_.setText(matriz::i18n::t("backup.sincronizar_titulo"), juce::dontSendNotification);
        lblTitulo_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
        lblTitulo_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblTitulo_);

        lblDescricao_.setText(isPt ? juce::String::fromUTF8("Selecione explicitamente a direção da cópia:")
                                   : "Select copy direction explicitly:", juce::dontSendNotification);
        lblDescricao_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        lblDescricao_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(lblDescricao_);

        lblFonte_.setText(matriz::i18n::t("backup.copiar_de"), juce::dontSendNotification);
        lblFonte_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        lblFonte_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblFonte_);

        comboFonte_.setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
        comboFonte_.setColour(juce::ComboBox::textColourId, tk.textoPrimario);
        comboFonte_.setColour(juce::ComboBox::outlineColourId, tk.borda);
        for (size_t i = 0; i < versoes_.size(); ++i) {
            comboFonte_.addItem(versoes_[i].rotulo + " (" + versoes_[i].destinoPath + ")", static_cast<int>(i + 1));
        }
        comboFonte_.setSelectedId(1, juce::dontSendNotification);
        comboFonte_.onChange = [this] { atualizarAlvos(); };
        addAndMakeVisible(comboFonte_);

        lblAlvo_.setText(matriz::i18n::t("backup.copiar_para"), juce::dontSendNotification);
        lblAlvo_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        lblAlvo_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblAlvo_);

        comboAlvo_.setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
        comboAlvo_.setColour(juce::ComboBox::textColourId, tk.textoPrimario);
        comboAlvo_.setColour(juce::ComboBox::outlineColourId, tk.borda);
        addAndMakeVisible(comboAlvo_);
        atualizarAlvos();

        btnComparar_.setButtonText(matriz::i18n::t("backup.comparar"));
        btnComparar_.setColour(juce::TextButton::buttonColourId, tk.acento);
        btnComparar_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
        btnComparar_.onClick = [this] {
            int idxFonte = comboFonte_.getSelectedId() - 1;
            int idAlvo = comboAlvo_.getSelectedId();
            if (idxFonte >= 0 && idxFonte < static_cast<int>(versoes_.size()) && idAlvo > 0) {
                int idxAlvo = idAlvo - 1;
                if (idxAlvo >= 0 && idxAlvo < static_cast<int>(versoes_.size())) {
                    if (onComparar_) onComparar_(versoes_[static_cast<size_t>(idxFonte)], versoes_[static_cast<size_t>(idxAlvo)]);
                }
            }
        };
        addAndMakeVisible(btnComparar_);

        btnCancelar_.setButtonText(isPt ? "Cancelar" : "Cancel");
        btnCancelar_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnCancelar_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnCancelar_.onClick = [this] { if (onCancelar_) onCancelar_(); };
        addAndMakeVisible(btnCancelar_);

        setSize(520, 260);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(20);

        lblTitulo_.setBounds(r.removeFromTop(28));
        r.removeFromTop(4);
        lblDescricao_.setBounds(r.removeFromTop(20));
        r.removeFromTop(16);

        auto linhaFonte = r.removeFromTop(32);
        lblFonte_.setBounds(linhaFonte.removeFromLeft(100));
        comboFonte_.setBounds(linhaFonte);
        r.removeFromTop(10);

        auto linhaAlvo = r.removeFromTop(32);
        lblAlvo_.setBounds(linhaAlvo.removeFromLeft(100));
        comboAlvo_.setBounds(linhaAlvo);

        r.removeFromTop(20);
        auto linhaBotoes = r.removeFromBottom(34);
        btnCancelar_.setBounds(linhaBotoes.removeFromLeft(110));
        btnComparar_.setBounds(linhaBotoes.removeFromRight(130));
    }

private:
    void atualizarAlvos() {
        int fonteSel = comboFonte_.getSelectedId() - 1;
        comboAlvo_.clear(juce::dontSendNotification);
        int primeiroValido = -1;
        for (size_t i = 0; i < versoes_.size(); ++i) {
            if (static_cast<int>(i) != fonteSel) {
                comboAlvo_.addItem(versoes_[i].rotulo + " (" + versoes_[i].destinoPath + ")", static_cast<int>(i + 1));
                if (primeiroValido == -1) primeiroValido = static_cast<int>(i + 1);
            }
        }
        if (primeiroValido != -1) comboAlvo_.setSelectedId(primeiroValido, juce::dontSendNotification);
    }

    ProjetoAberto& projeto_;
    std::vector<BackupVersionRef> versoes_;
    std::function<void(const BackupVersionRef&, const BackupVersionRef&)> onComparar_;
    std::function<void()> onCancelar_;

    juce::Label lblTitulo_;
    juce::Label lblDescricao_;
    juce::Label lblFonte_;
    juce::ComboBox comboFonte_;
    juce::Label lblAlvo_;
    juce::ComboBox comboAlvo_;
    juce::TextButton btnComparar_;
    juce::TextButton btnCancelar_;
};

// ==============================================================================
// 2. Modal Bloqueante de Progresso de Varredura (SHA-256 em tempo real)
// ==============================================================================
class SyncScanModalComponent : public juce::Component, private juce::Timer {
public:
    SyncScanModalComponent(ProjetoAberto& projeto,
                           const BackupVersionRef& fonte,
                           const BackupVersionRef& alvo,
                           std::function<void(bool sucesso, std::vector<SyncDivergenceItem> divergencias)> onConcluido)
        : projeto_(projeto), fonte_(fonte), alvo_(alvo), onConcluido_(std::move(onConcluido))
    {
        const auto& tk = tema();
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

        lblTitulo_.setText(isPt ? juce::String::fromUTF8("Comparando Cópias de Backup...") : "Comparing Backup Copies...", juce::dontSendNotification);
        lblTitulo_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
        lblTitulo_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblTitulo_);

        juce::String destInfo = "📁 " + (isPt ? juce::String::fromUTF8("Fonte: ") : "Source: ") + fonte_.rotulo + " (" + fonte_.destinoPath + ")"
                              + "   →   " + "📁 " + (isPt ? juce::String::fromUTF8("Alvo: ") : "Target: ") + alvo_.rotulo + " (" + alvo_.destinoPath + ")";
        lblDestinos_.setText(destInfo, juce::dontSendNotification);
        lblDestinos_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        lblDestinos_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(lblDestinos_);

        lblFase_.setText(isPt ? juce::String::fromUTF8("Fase 1: Mapeando arquivos e preparando varredura...")
                              : "Phase 1: Mapping files and preparing scan...", juce::dontSendNotification);
        lblFase_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        lblFase_.setColour(juce::Label::textColourId, tk.acento);
        addAndMakeVisible(lblFase_);

        lblDetalheGeral_.setText("", juce::dontSendNotification);
        lblDetalheGeral_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        lblDetalheGeral_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblDetalheGeral_);

        progressBarGeral_.setColour(juce::ProgressBar::foregroundColourId, tk.acento);
        progressBarGeral_.setColour(juce::ProgressBar::backgroundColourId, tk.painelAlt);
        addAndMakeVisible(progressBarGeral_);

        lblDetalheArquivo_.setText("", juce::dontSendNotification);
        lblDetalheArquivo_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 0.5f)));
        lblDetalheArquivo_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(lblDetalheArquivo_);

        progressBarArquivo_.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(0xff3b82f6));
        progressBarArquivo_.setColour(juce::ProgressBar::backgroundColourId, tk.painelAlt);
        addAndMakeVisible(progressBarArquivo_);

        lblLog_.setText("", juce::dontSendNotification);
        lblLog_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 0.5f)));
        lblLog_.setColour(juce::Label::textColourId, tk.textoTerciario);
        addAndMakeVisible(lblLog_);

        btnCancelar_.setButtonText(isPt ? "Cancelar" : "Cancel");
        btnCancelar_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnCancelar_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnCancelar_.onClick = [this] {
            cancelRequested_ = true;
            btnCancelar_.setEnabled(false);
            btnCancelar_.setButtonText(matriz::i18n::localeAtivo().startsWith("pt") ? "Cancelando..." : "Cancelling...");
        };
        addAndMakeVisible(btnCancelar_);

        setSize(660, 370);
        iniciarThreadVarredura();
        startTimer(50);
    }

    ~SyncScanModalComponent() override {
        cancelRequested_ = true;
        if (workerThread_.joinable()) workerThread_.join();
    }

    void resized() override {
        auto r = getLocalBounds().reduced(20, 16);
        lblTitulo_.setBounds(r.removeFromTop(26));
        r.removeFromTop(4);
        lblDestinos_.setBounds(r.removeFromTop(18));
        r.removeFromTop(8);
        lblFase_.setBounds(r.removeFromTop(18));
        r.removeFromTop(8);

        // Overall progress section
        lblDetalheGeral_.setBounds(r.removeFromTop(18));
        r.removeFromTop(3);
        progressBarGeral_.setBounds(r.removeFromTop(18));
        r.removeFromTop(12);

        // Sub progress section
        lblDetalheArquivo_.setBounds(r.removeFromTop(18));
        r.removeFromTop(3);
        progressBarArquivo_.setBounds(r.removeFromTop(16));
        r.removeFromTop(10);

        // Activity log box
        lblLog_.setBounds(r.removeFromTop(26));

        r.removeFromTop(8);
        btnCancelar_.setBounds(r.removeFromBottom(32).withSizeKeepingCentre(130, 32));
    }

private:
    void timerCallback() override {
        int proc = processedFiles_.load();
        int totFiles = totalFiles_.load();
        juce::int64 scanned = bytesScanned_.load();
        juce::int64 totBytes = totalBytes_.load();
        juce::int64 curBytes = currentFileBytes_.load();
        juce::int64 curTot = currentFileTotal_.load();
        int divs = divergencesCount_.load();

        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

        // Calculate progress fractions
        if (totBytes > 0) {
            progressoGeral_ = static_cast<double>(scanned) / static_cast<double>(totBytes);
        } else if (totFiles > 0) {
            progressoGeral_ = static_cast<double>(proc) / static_cast<double>(totFiles);
        } else {
            progressoGeral_ = -1.0;
        }

        if (curTot > 0) {
            progressoArquivo_ = static_cast<double>(curBytes) / static_cast<double>(curTot);
        } else {
            progressoArquivo_ = 0.0;
        }

        // Calculate Speed & ETA
        auto now = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration_cast<std::chrono::duration<double>>(now - startTime_).count();
        double speedBytesPerSec = (elapsedSec > 0.5) ? (static_cast<double>(scanned) / elapsedSec) : 0.0;
        juce::int64 remainingBytes = std::max<juce::int64>(0, totBytes - scanned);
        int etaSec = (speedBytesPerSec > 1024.0) ? static_cast<int>(static_cast<double>(remainingBytes) / speedBytesPerSec) : 0;

        juce::String speedStr = (speedBytesPerSec > 0.0) ? (juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(speedBytesPerSec)) + "/s") : "-- MB/s";
        juce::String etaStr = (speedBytesPerSec > 1024.0 && totBytes > 0) ? juce::String::formatted("%02d:%02d", etaSec / 60, etaSec % 60) : "--:--";

        // Labels
        juce::String geralTxt = (isPt ? "Varredura Geral: " : "Overall Scan: ")
            + juce::String(proc) + " / " + juce::String(totFiles) + (isPt ? " arquivos" : " files")
            + "  |  " + juce::File::descriptionOfSizeInBytes(scanned) + " / " + juce::File::descriptionOfSizeInBytes(totBytes)
            + "  |  " + speedStr
            + (isPt ? "  |  Restante: " : "  |  ETA: ") + etaStr;
        lblDetalheGeral_.setText(geralTxt, juce::dontSendNotification);

        juce::String stage, file, activity;
        {
            juce::ScopedLock sl(lock_);
            stage = currentStage_;
            file = currentFile_;
            activity = currentActivity_;
        }

        lblFase_.setText(stage, juce::dontSendNotification);

        juce::String arqTxt = (isPt ? "Arquivo atual: " : "Current file: ") + file;
        if (curTot > 0) {
            arqTxt += " (" + juce::File::descriptionOfSizeInBytes(curBytes) + " / " + juce::File::descriptionOfSizeInBytes(curTot)
                   + " - " + juce::String(static_cast<int>(progressoArquivo_ * 100.0)) + "%)";
        }
        lblDetalheArquivo_.setText(arqTxt, juce::dontSendNotification);

        juce::String logMsg = activity;
        if (divs > 0) {
            logMsg += (isPt ? "  •  Divergências encontradas: " : "  •  Divergences found: ") + juce::String(divs);
        }
        lblLog_.setText(logMsg, juce::dontSendNotification);

        if (isFinished_.load()) {
            stopTimer();
            if (workerThread_.joinable()) workerThread_.join();
            if (onConcluido_) {
                onConcluido_(!cancelRequested_.load(), std::move(divergencias_));
            }
        }
    }

    void iniciarThreadVarredura() {
        startTime_ = std::chrono::steady_clock::now();
        workerThread_ = std::thread([this] {
            executarVarredura();
        });
    }

    void executarVarredura() {
        juce::File pastaFonte(fonte_.destinoPath);
        juce::File pastaAlvo(alvo_.destinoPath);

        if (!pastaFonte.isDirectory() || !pastaAlvo.isDirectory()) {
            isFinished_ = true;
            return;
        }

        {
            juce::ScopedLock sl(lock_);
            currentStage_ = (matriz::i18n::localeAtivo().startsWith("pt"))
                ? juce::String::fromUTF8("Fase 1 de 2: Mapeando estrutura e descobrindo arquivos...")
                : "Phase 1 of 2: Mapping folder structure and discovering files...";
        }

        // 1. Mapear consolidacao_registro para fonte e alvo
        struct RecFonte { std::string itemId; std::string pastaId; std::string arquivoId; };
        std::map<std::string, RecFonte> mapaFonte;
        std::map<std::string, std::string> mapaAlvo;

        try {
            auto stmtF = projeto_.projeto().registro().prepare(
                "SELECT item_id, pasta_id, arquivo_id, caminho_relativo_destino FROM consolidacao_registro "
                "WHERE (destino_path = ? OR destino_path = '')");
            stmtF.bind(1, matriz::db::Value::of(fonte_.destinoPath.toStdString()));
            while (stmtF.step()) {
                mapaFonte[stmtF.columnText(3)] = {stmtF.columnText(0), stmtF.columnText(1), stmtF.columnText(2)};
            }

            auto stmtA = projeto_.projeto().registro().prepare(
                "SELECT item_id, caminho_relativo_destino FROM consolidacao_registro "
                "WHERE (destino_path = ? OR destino_path = '')");
            stmtA.bind(1, matriz::db::Value::of(alvo_.destinoPath.toStdString()));
            while (stmtA.step()) {
                mapaAlvo[stmtA.columnText(0)] = stmtA.columnText(1);
            }
        } catch (...) {}

        // 2. Coletar arquivos físicos da fonte
        auto arquivos = pastaFonte.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, true);
        totalFiles_ = arquivos.size();

        juce::int64 totalBytes = 0;
        for (const auto& a : arquivos) {
            totalBytes += a.getSize();
        }
        totalBytes_ = totalBytes;

        {
            juce::ScopedLock sl(lock_);
            currentStage_ = (matriz::i18n::localeAtivo().startsWith("pt"))
                ? juce::String::fromUTF8("Fase 2 de 2: Analisando integridade SHA-256 e comparando destinos...")
                : "Phase 2 of 2: Analyzing SHA-256 integrity and comparing destinations...";
        }

        startTime_ = std::chrono::steady_clock::now();
        std::vector<SyncDivergenceItem> divList;

        for (int i = 0; i < arquivos.size(); ++i) {
            if (cancelRequested_.load()) break;

            const auto& arqFonte = arquivos[i];
            juce::String relFonte = arqFonte.getRelativePathFrom(pastaFonte);
            juce::int64 tamFonte = arqFonte.getSize();

            currentFileTotal_ = tamFonte;
            currentFileBytes_ = 0;

            {
                juce::ScopedLock sl(lock_);
                currentFile_ = relFonte;
                currentActivity_ = (matriz::i18n::localeAtivo().startsWith("pt"))
                    ? juce::String::fromUTF8("Calculando hash SHA-256 da fonte...")
                    : "Computing source SHA-256 hash...";
            }

            // Chunked SHA-256 of source file
            juce::int64 bytesLidosAntesFonte = bytesScanned_.load();
            auto shaFonte = calcularSha256Chunked(arqFonte, [this, bytesLidosAntesFonte](juce::int64 lidos, juce::int64) {
                currentFileBytes_ = lidos;
                bytesScanned_ = bytesLidosAntesFonte + lidos;
            }, &cancelRequested_);

            if (cancelRequested_.load()) break;

            // Encontrar correspondência no alvo
            std::string itemId, pastaId, arquivoId;
            auto itF = mapaFonte.find(relFonte.toStdString());
            if (itF != mapaFonte.end()) {
                itemId = itF->second.itemId;
                pastaId = itF->second.pastaId;
                arquivoId = itF->second.arquivoId;
            }

            juce::String relAlvo = relFonte;
            if (!itemId.empty()) {
                auto itA = mapaAlvo.find(itemId);
                if (itA != mapaAlvo.end() && !itA->second.empty()) {
                    relAlvo = juce::String::fromUTF8(itA->second.c_str());
                }
            }

            juce::File arqAlvo = pastaAlvo.getChildFile(relAlvo);

            if (!arqAlvo.existsAsFile()) {
                SyncDivergenceItem item;
                item.tipo = SyncDivergenceItem::Tipo::Ausente;
                item.arquivoFonte = arqFonte;
                item.arquivoAlvo = arqAlvo;
                item.caminhoRelativoFonte = relFonte;
                item.caminhoRelativoAlvo = relAlvo;
                item.nomeExibicao = arqFonte.getFileName();
                item.itemId = itemId;
                item.pastaId = pastaId;
                item.arquivoId = arquivoId;
                item.sha256Fonte = shaFonte;
                item.selecionado = false;
                divList.push_back(std::move(item));
                divergencesCount_++;

                juce::ScopedLock sl(lock_);
                currentActivity_ = (matriz::i18n::localeAtivo().startsWith("pt"))
                    ? (juce::String::fromUTF8("⚠ Ausente no alvo: ") + relAlvo)
                    : ("⚠ Missing in target: " + relAlvo);
            } else {
                {
                    juce::ScopedLock sl(lock_);
                    currentActivity_ = (matriz::i18n::localeAtivo().startsWith("pt"))
                        ? juce::String::fromUTF8("Verificando hash do arquivo correspondente no alvo...")
                        : "Verifying hash of matching file in target...";
                }

                currentFileTotal_ = arqAlvo.getSize();
                currentFileBytes_ = 0;
                auto shaAlvo = calcularSha256Chunked(arqAlvo, [this](juce::int64 lidos, juce::int64) {
                    currentFileBytes_ = lidos;
                }, &cancelRequested_);

                if (cancelRequested_.load()) break;

                if (shaFonte != shaAlvo) {
                    SyncDivergenceItem item;
                    item.tipo = SyncDivergenceItem::Tipo::Divergente;
                    item.arquivoFonte = arqFonte;
                    item.arquivoAlvo = arqAlvo;
                    item.caminhoRelativoFonte = relFonte;
                    item.caminhoRelativoAlvo = relAlvo;
                    item.nomeExibicao = arqFonte.getFileName();
                    item.itemId = itemId;
                    item.pastaId = pastaId;
                    item.arquivoId = arquivoId;
                    item.sha256Fonte = shaFonte;
                    item.sha256Alvo = shaAlvo;
                    item.selecionado = false;
                    divList.push_back(std::move(item));
                    divergencesCount_++;

                    juce::ScopedLock sl(lock_);
                    currentActivity_ = (matriz::i18n::localeAtivo().startsWith("pt"))
                        ? (juce::String::fromUTF8("⚠ Hash divergente detectado: ") + relFonte)
                        : ("⚠ Divergent hash detected: " + relFonte);
                } else {
                    juce::ScopedLock sl(lock_);
                    currentActivity_ = (matriz::i18n::localeAtivo().startsWith("pt"))
                        ? (juce::String::fromUTF8("✓ Arquivo idêntico: ") + relFonte)
                        : ("✓ Identical file: " + relFonte);
                }
            }

            processedFiles_++;
        }

        divergencias_ = std::move(divList);
        isFinished_ = true;
    }

    ProjetoAberto& projeto_;
    BackupVersionRef fonte_;
    BackupVersionRef alvo_;
    std::function<void(bool, std::vector<SyncDivergenceItem>)> onConcluido_;

    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> isFinished_{false};
    std::atomic<int> processedFiles_{0};
    std::atomic<int> totalFiles_{0};
    std::atomic<juce::int64> bytesScanned_{0};
    std::atomic<juce::int64> totalBytes_{0};
    std::atomic<juce::int64> currentFileBytes_{0};
    std::atomic<juce::int64> currentFileTotal_{0};
    std::atomic<int> divergencesCount_{0};

    std::chrono::steady_clock::time_point startTime_;

    juce::CriticalSection lock_;
    juce::String currentStage_;
    juce::String currentFile_;
    juce::String currentActivity_;
    std::vector<SyncDivergenceItem> divergencias_;
    std::thread workerThread_;

    juce::Label lblTitulo_;
    juce::Label lblDestinos_;
    juce::Label lblFase_;
    juce::Label lblDetalheGeral_;
    double progressoGeral_ = 0.0;
    juce::ProgressBar progressBarGeral_{progressoGeral_};

    juce::Label lblDetalheArquivo_;
    double progressoArquivo_ = 0.0;
    juce::ProgressBar progressBarArquivo_{progressoArquivo_};

    juce::Label lblLog_;
    juce::TextButton btnCancelar_;
};

// ==============================================================================
// 3. Modal de Revisão de Divergências com Checkboxes (desmarcados por padrão)
// ==============================================================================
class SyncReviewComponent : public juce::Component, private juce::ListBoxModel {
public:
    SyncReviewComponent(ProjetoAberto& projeto,
                        const BackupVersionRef& fonte,
                        const BackupVersionRef& alvo,
                        std::vector<SyncDivergenceItem> divergencias,
                        std::function<void()> onConcluido,
                        std::function<void()> onCancelar)
        : projeto_(projeto), fonte_(fonte), alvo_(alvo),
          divergencias_(std::move(divergencias)),
          onConcluido_(std::move(onConcluido)),
          onCancelar_(std::move(onCancelar))
    {
        const auto& tk = tema();
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

        juce::String tit = matriz::i18n::t("backup.divergencias_encontradas")
                               .replace("{n}", juce::String(static_cast<int>(divergencias_.size())));
        lblTitulo_.setText(tit, juce::dontSendNotification);
        lblTitulo_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
        lblTitulo_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblTitulo_);

        juce::String sub = (isPt ? "Copiar de: " : "Copy from: ") + fonte_.rotulo + "  ->  " +
                           (isPt ? "Para: " : "To: ") + alvo_.rotulo;
        lblSubtitulo_.setText(sub, juce::dontSendNotification);
        lblSubtitulo_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        lblSubtitulo_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(lblSubtitulo_);

        listBox_.setModel(this);
        listBox_.setRowHeight(32);
        listBox_.setColour(juce::ListBox::backgroundColourId, tk.painel);
        listBox_.setColour(juce::ListBox::outlineColourId, tk.borda);
        addAndMakeVisible(listBox_);

        btnCopiar_.setButtonText(matriz::i18n::t("backup.copiar_selecionados"));
        btnCopiar_.setColour(juce::TextButton::buttonColourId, tk.acento);
        btnCopiar_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
        btnCopiar_.onClick = [this] { executarCopia(); };
        addAndMakeVisible(btnCopiar_);

        btnCancelar_.setButtonText(isPt ? "Cancelar" : "Cancel");
        btnCancelar_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnCancelar_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnCancelar_.onClick = [this] { if (onCancelar_) onCancelar_(); };
        addAndMakeVisible(btnCancelar_);

        setSize(640, 480);
    }

    int getNumRows() override {
        return static_cast<int>(divergencias_.size());
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool /*rowIsSelected*/) override {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(divergencias_.size())) return;
        const auto& tk = tema();
        const auto& item = divergencias_[static_cast<size_t>(rowNumber)];

        g.fillAll(rowNumber % 2 == 0 ? tk.painel : tk.painelAlt);

        // Checkbox square
        juce::Rectangle<int> cbRect(12, (height - 18) / 2, 18, 18);
        g.setColour(tk.borda);
        g.drawRect(cbRect, 1);
        if (item.selecionado) {
            g.setColour(tk.acento);
            g.fillRect(cbRect.reduced(3));
        }

        // Text: File name and status
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.setColour(tk.textoPrimario);
        int textX = 38;
        int badgeW = 200;
        int textW = width - textX - badgeW - 10;
        g.drawText(item.nomeExibicao, textX, 0, textW, height, juce::Justification::centredLeft, true);

        // Badge
        bool isAusente = (item.tipo == SyncDivergenceItem::Tipo::Ausente);
        juce::String rotuloTipo = isAusente ? matriz::i18n::t("backup.ausente_alvo")
                                            : matriz::i18n::t("backup.divergente_hash");
        juce::Colour badgeCol = isAusente ? juce::Colour(0xfff97316) : tk.perigo;

        juce::Rectangle<int> bRect(width - badgeW - 8, (height - 20) / 2, badgeW, 20);
        g.setColour(badgeCol.withAlpha(0.2f));
        g.fillRoundedRectangle(bRect.toFloat(), 3.0f);
        g.setColour(badgeCol);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText(rotuloTipo, bRect, juce::Justification::centred, true);
    }

    void listBoxItemClicked(int rowNumber, const juce::MouseEvent&) override {
        if (rowNumber >= 0 && rowNumber < static_cast<int>(divergencias_.size())) {
            divergencias_[static_cast<size_t>(rowNumber)].selecionado = !divergencias_[static_cast<size_t>(rowNumber)].selecionado;
            listBox_.repaintRow(rowNumber);
        }
    }

    void resized() override {
        auto r = getLocalBounds().reduced(20);
        lblTitulo_.setBounds(r.removeFromTop(28));
        r.removeFromTop(4);
        lblSubtitulo_.setBounds(r.removeFromTop(20));
        r.removeFromTop(12);

        auto linhaBotoes = r.removeFromBottom(34);
        btnCancelar_.setBounds(linhaBotoes.removeFromLeft(110));
        btnCopiar_.setBounds(linhaBotoes.removeFromRight(230));

        r.removeFromBottom(12);
        listBox_.setBounds(r);
    }

private:
    void executarCopia() {
        int selecionados = 0;
        for (const auto& item : divergencias_) if (item.selecionado) selecionados++;

        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        if (selecionados == 0) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                isPt ? "Nenhum arquivo selecionado" : "No files selected",
                isPt ? juce::String::fromUTF8("Marque as caixas de seleção dos arquivos que deseja copiar para o alvo.")
                     : "Check the boxes for the files you want to copy to the target.");
            return;
        }

        struct SyncCopyThread : public juce::ThreadWithProgressWindow {
            SyncCopyThread(const juce::String& title,
                           ProjetoAberto& proj,
                           const BackupVersionRef& f,
                           const BackupVersionRef& a,
                           const std::vector<SyncDivergenceItem>& divs)
                : juce::ThreadWithProgressWindow(title, true, true),
                  projeto_(proj), fonte_(f), alvo_(a), divergencias_(divs) {}

            void run() override {
                bool isPtLocale = matriz::i18n::localeAtivo().startsWith("pt");
                int totalSel = 0;
                juce::int64 totalBytes = 0;
                for (const auto& item : divergencias_) {
                    if (item.selecionado) {
                        totalSel++;
                        totalBytes += item.arquivoFonte.getSize();
                    }
                }

                int feito = 0;
                juce::int64 bytesCopiados = 0;
                std::string agora = matriz::model::agoraIso8601();
                std::string targetPathStr = alvo_.destinoPath.toStdString();

                auto startTime = std::chrono::steady_clock::now();

                for (const auto& item : divergencias_) {
                    if (threadShouldExit()) break;
                    if (!item.selecionado) continue;

                    auto now = std::chrono::steady_clock::now();
                    double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - startTime).count();
                    double speed = (elapsed > 0.5) ? (static_cast<double>(bytesCopiados) / elapsed) : 0.0;
                    juce::int64 restBytes = std::max<juce::int64>(0, totalBytes - bytesCopiados);
                    int etaSec = (speed > 1024.0) ? static_cast<int>(static_cast<double>(restBytes) / speed) : 0;

                    juce::String speedStr = (speed > 0.0) ? (juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(speed)) + "/s") : "-- MB/s";
                    juce::String etaStr = (speed > 1024.0 && totalBytes > 0) ? juce::String::formatted("%02d:%02d", etaSec / 60, etaSec % 60) : "--:--";

                    juce::String msg = (isPtLocale ? juce::String::fromUTF8("Copiando: ") : "Copying: ") + item.nomeExibicao + "\n("
                                     + juce::String(feito + 1) + " / " + juce::String(totalSel) + " - "
                                     + juce::File::descriptionOfSizeInBytes(bytesCopiados) + " / " + juce::File::descriptionOfSizeInBytes(totalBytes)
                                     + "  |  " + speedStr + "  |  " + (isPtLocale ? juce::String::fromUTF8("Restante: ") : "ETA: ") + etaStr + ")";
                    setStatusMessage(msg);
                    if (totalBytes > 0) {
                        setProgress(static_cast<double>(bytesCopiados) / static_cast<double>(totalBytes));
                    } else {
                        setProgress(static_cast<double>(feito) / std::max(1, totalSel));
                    }

                    item.arquivoAlvo.getParentDirectory().createDirectory();
                    if (item.arquivoAlvo.existsAsFile()) item.arquivoAlvo.deleteFile();

                    if (item.arquivoFonte.copyFileTo(item.arquivoAlvo)) {
                        copiados++;
                        bytesCopiados += item.arquivoFonte.getSize();
                        auto novoSha = calcularSha256Chunked(item.arquivoAlvo, nullptr, nullptr);

                        if (!item.itemId.empty() && !item.arquivoId.empty()) {
                            try {
                                projeto_.projeto().registro().run(
                                    "INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                                    "checksum_sha256, consolidado_em, destino_path) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                                    "ON CONFLICT(item_id, pasta_id, arquivo_id, destino_path) DO UPDATE SET "
                                    "caminho_relativo_destino = excluded.caminho_relativo_destino, "
                                    "checksum_sha256 = excluded.checksum_sha256, consolidado_em = excluded.consolidado_em",
                                    {matriz::db::Value::of(matriz::model::novoUuid()),
                                     matriz::db::Value::of(item.itemId),
                                     matriz::db::Value::of(item.pastaId),
                                     matriz::db::Value::of(item.arquivoId),
                                     matriz::db::Value::of(item.caminhoRelativoAlvo.toStdString()),
                                     matriz::db::Value::of(novoSha),
                                     matriz::db::Value::of(agora),
                                     matriz::db::Value::of(targetPathStr)});
                            } catch (...) {}
                        }
                    }
                    feito++;
                }
            }

            ProjetoAberto& projeto_;
            BackupVersionRef fonte_;
            BackupVersionRef alvo_;
            const std::vector<SyncDivergenceItem>& divergencias_;
            int copiados = 0;
        };

        SyncCopyThread copyThread(isPt ? juce::String::fromUTF8("Sincronizando Cópias de Backup...") : "Synchronizing Backup Copies...",
                                  projeto_, fonte_, alvo_, divergencias_);
        copyThread.runThread();

        int copiados = copyThread.copiados;

        int ignorados = static_cast<int>(divergencias_.size()) - copiados;

        // Log do projeto
        matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
        juce::StringArray details;
        details.add("Source Version: " + fonte_.rotulo + " (" + fonte_.destinoPath + ")");
        details.add("Target Version: " + alvo_.rotulo + " (" + alvo_.destinoPath + ")");
        details.add("Files copied: " + juce::String(copiados));
        details.add("Divergences ignored: " + juce::String(ignorados));
        pLog.appendEntry("Backup Versions Synced", details);

        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            isPt ? "Sincronização Concluída" : "Sync Completed",
            (isPt ? juce::String::fromUTF8("Arquivos copiados com sucesso para o alvo: ") : "Files successfully copied to target: ") + juce::String(copiados));

        if (onConcluido_) onConcluido_();
    }

    ProjetoAberto& projeto_;
    BackupVersionRef fonte_;
    BackupVersionRef alvo_;
    std::vector<SyncDivergenceItem> divergencias_;
    std::function<void()> onConcluido_;
    std::function<void()> onCancelar_;

    juce::Label lblTitulo_;
    juce::Label lblSubtitulo_;
    juce::ListBox listBox_;
    juce::TextButton btnCopiar_;
    juce::TextButton btnCancelar_;
};

} // namespace

// ==============================================================================
// Entry point
// ==============================================================================
void BackupSyncDialog::showSyncDialog(ProjetoAberto& projeto,
                                     const std::vector<BackupVersionRef>& versoes,
                                     std::function<void()> onConcluido)
{
    if (versoes.size() < 2) {
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            matriz::i18n::t("backup.sincronizar_titulo"),
            isPt ? juce::String::fromUTF8("É necessário ter pelo menos duas Versões de Backup cadastradas para sincronizar.")
                 : "At least two registered Backup Versions are required to synchronize.");
        return;
    }

    auto janelaHolder = std::make_shared<std::unique_ptr<juce::DialogWindow>>();

    auto onCancelar = [janelaHolder] {
        juce::MessageManager::callAsync([janelaHolder] {
            if (*janelaHolder) {
                (*janelaHolder)->exitModalState(0);
                janelaHolder->reset();
            }
        });
    };

    auto onComparar = [&projeto, janelaHolder, onConcluido](const BackupVersionRef& fonte, const BackupVersionRef& alvo) {
        juce::MessageManager::callAsync([janelaHolder] {
            if (*janelaHolder) {
                (*janelaHolder)->exitModalState(0);
                janelaHolder->reset();
            }
        });

        // Abre modal de varredura
        auto modalHolder = std::make_shared<std::unique_ptr<juce::DialogWindow>>();
        auto scanComp = std::make_unique<SyncScanModalComponent>(
            projeto, fonte, alvo,
            [&projeto, fonte, alvo, modalHolder, onConcluido](bool sucesso, std::vector<SyncDivergenceItem> divergencias) {
                juce::MessageManager::callAsync([&projeto, fonte, alvo, modalHolder, onConcluido, sucesso, divergencias = std::move(divergencias)]() mutable {
                    if (*modalHolder) {
                        (*modalHolder)->exitModalState(0);
                        modalHolder->reset();
                    }

                    if (!sucesso) return; // Cancelado

                    if (divergencias.empty()) {
                        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::InfoIcon,
                            matriz::i18n::t("backup.sincronizar_titulo"),
                            isPt ? juce::String::fromUTF8("Todas as cópias estão perfeitamente sincronizadas (0 divergências).")
                                 : "All backup copies are perfectly synchronized (0 divergences).");
                        return;
                    }

                    // Abre modal de revisão
                    auto reviewHolder = std::make_shared<std::unique_ptr<juce::DialogWindow>>();
                    auto reviewComp = std::make_unique<SyncReviewComponent>(
                        projeto, fonte, alvo, std::move(divergencias),
                        [reviewHolder, onConcluido] {
                            juce::MessageManager::callAsync([reviewHolder, onConcluido] {
                                if (*reviewHolder) {
                                    (*reviewHolder)->exitModalState(1);
                                    reviewHolder->reset();
                                }
                                if (onConcluido) onConcluido();
                            });
                        },
                        [reviewHolder] {
                            juce::MessageManager::callAsync([reviewHolder] {
                                if (*reviewHolder) {
                                    (*reviewHolder)->exitModalState(0);
                                    reviewHolder->reset();
                                }
                            });
                        });

                    juce::DialogWindow::LaunchOptions optReview;
                    optReview.content.set(reviewComp.release(), true);
                    optReview.dialogTitle = matriz::i18n::t("backup.sincronizar_titulo");
                    optReview.dialogBackgroundColour = tema().painel;
                    optReview.escapeKeyTriggersCloseButton = true;
                    optReview.useNativeTitleBar = true;
                    optReview.resizable = true;
                    *reviewHolder = std::unique_ptr<juce::DialogWindow>(optReview.launchAsync());
                });
            });

        juce::DialogWindow::LaunchOptions optScan;
        optScan.content.set(scanComp.release(), true);
        optScan.dialogTitle = matriz::i18n::t("backup.sincronizar_titulo");
        optScan.dialogBackgroundColour = tema().painel;
        optScan.escapeKeyTriggersCloseButton = false;
        optScan.useNativeTitleBar = true;
        optScan.resizable = false;
        *modalHolder = std::unique_ptr<juce::DialogWindow>(optScan.launchAsync());
    };

    auto selectComp = std::make_unique<SyncSelectComponent>(projeto, versoes, std::move(onComparar), std::move(onCancelar));

    juce::DialogWindow::LaunchOptions optSelect;
    optSelect.content.set(selectComp.release(), true);
    optSelect.dialogTitle = matriz::i18n::t("backup.sincronizar_titulo");
    optSelect.dialogBackgroundColour = tema().painel;
    optSelect.escapeKeyTriggersCloseButton = true;
    optSelect.useNativeTitleBar = true;
    optSelect.resizable = false;
    *janelaHolder = std::unique_ptr<juce::DialogWindow>(optSelect.launchAsync());
}

void BackupSyncDialog::showSyncWithDestinationDialog(ProjetoAberto& projeto,
                                                      const juce::File& destinoAtivo,
                                                      const juce::File& segundoDestino,
                                                      std::function<void()> onConcluido)
{
    BackupVersionRef fonte;
    fonte.rotulo = "Destino Ativo";
    fonte.destinoPath = destinoAtivo.getFullPathName();

    BackupVersionRef alvo;
    alvo.rotulo = segundoDestino.getFileName();
    alvo.destinoPath = segundoDestino.getFullPathName();

    auto modalHolder = std::make_shared<std::unique_ptr<juce::DialogWindow>>();
    auto scanComp = std::make_unique<SyncScanModalComponent>(
        projeto, fonte, alvo,
        [&projeto, fonte, alvo, modalHolder, onConcluido](bool sucesso, std::vector<SyncDivergenceItem> divergencias) {
            juce::MessageManager::callAsync([&projeto, fonte, alvo, modalHolder, onConcluido, sucesso, divergencias = std::move(divergencias)]() mutable {
                if (*modalHolder) {
                    (*modalHolder)->exitModalState(0);
                    modalHolder->reset();
                }

                if (!sucesso) return; // Cancelado

                if (divergencias.empty()) {
                    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::InfoIcon,
                        matriz::i18n::t("backup.sincronizar_titulo"),
                        isPt ? juce::String::fromUTF8("Todas as cópias estão perfeitamente sincronizadas (0 divergências).")
                             : "All backup copies are perfectly synchronized (0 divergences).");
                    return;
                }

                // Abre modal de revisão
                auto reviewHolder = std::make_shared<std::unique_ptr<juce::DialogWindow>>();
                auto reviewComp = std::make_unique<SyncReviewComponent>(
                    projeto, fonte, alvo, std::move(divergencias),
                    [reviewHolder, onConcluido] {
                        juce::MessageManager::callAsync([reviewHolder, onConcluido] {
                            if (*reviewHolder) {
                                (*reviewHolder)->exitModalState(1);
                                reviewHolder->reset();
                            }
                            if (onConcluido) onConcluido();
                        });
                    },
                    [reviewHolder] {
                        juce::MessageManager::callAsync([reviewHolder] {
                            if (*reviewHolder) {
                                (*reviewHolder)->exitModalState(0);
                                reviewHolder->reset();
                            }
                        });
                    });

                juce::DialogWindow::LaunchOptions optReview;
                optReview.content.set(reviewComp.release(), true);
                optReview.dialogTitle = matriz::i18n::t("backup.sincronizar_titulo");
                optReview.dialogBackgroundColour = tema().painel;
                optReview.escapeKeyTriggersCloseButton = true;
                optReview.useNativeTitleBar = true;
                optReview.resizable = true;
                *reviewHolder = std::unique_ptr<juce::DialogWindow>(optReview.launchAsync());
            });
        });

    juce::DialogWindow::LaunchOptions optScan;
    optScan.content.set(scanComp.release(), true);
    optScan.dialogTitle = matriz::i18n::t("backup.sincronizar_titulo");
    optScan.dialogBackgroundColour = tema().painel;
    optScan.escapeKeyTriggersCloseButton = false;
    optScan.useNativeTitleBar = true;
    optScan.resizable = false;
    *modalHolder = std::unique_ptr<juce::DialogWindow>(optScan.launchAsync());
}

} // namespace matriz::ui
