#include "ExportZipDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Vault/Resolucao.h"
#include <unordered_set>

namespace matriz::ui {

// ==============================================================================
// Métodos utilitários públicos
// ==============================================================================

juce::String ExportZipDialog::sanitizarNomeArquivoZip(const juce::String& nomeOriginal) {
    juce::String nome = nomeOriginal.replaceCharacter('\\', '/');
    auto segments = juce::StringArray::fromTokens(nome, "/", "");
    juce::StringArray sanitizedSegments;

    for (auto seg : segments) {
        seg = seg.trim();
        if (seg.isEmpty()) continue;

        // Substitui caracteres inválidos do Windows: < > : " / \ | ? *
        static const juce::String invalidChars = "<>:\"/\\|?*";
        for (int i = 0; i < invalidChars.length(); ++i) {
            seg = seg.replaceCharacter(invalidChars[i], '_');
        }

        // Trata nomes de dispositivos reservados no Windows (CON, PRN, AUX, NUL, COM1-9, LPT1-9)
        juce::String base = seg.upToLastOccurrenceOf(".", false, false);
        juce::String ext = seg.fromLastOccurrenceOf(".", true, false);
        if (base.isEmpty()) {
            base = seg;
            ext = "";
        }

        auto baseUpper = base.toUpperCase();
        bool isReserved = (baseUpper == "CON" || baseUpper == "PRN" || baseUpper == "AUX" || baseUpper == "NUL");
        if (!isReserved && (baseUpper.startsWith("COM") || baseUpper.startsWith("LPT")) && baseUpper.length() == 4) {
            auto ch = baseUpper[3];
            if (ch >= '1' && ch <= '9') isReserved = true;
        }

        if (isReserved) {
            base = "_" + base;
        }

        // Remove pontos e espaços ao final do nome base (incompatíveis com Windows)
        while (base.endsWithChar('.') || base.endsWithChar(' ')) {
            base = base.dropLastCharacters(1);
        }
        if (base.isEmpty()) base = "_";

        sanitizedSegments.add(base + ext);
    }

    if (sanitizedSegments.isEmpty()) return "arquivo";
    return sanitizedSegments.joinIntoString("/");
}

juce::String ExportZipDialog::resolverColisaoNome(const juce::String& nomeSanitizado,
                                                  std::set<std::string>& nomesUsadosLower) {
    juce::String candidate = nomeSanitizado;
    juce::String candidateLower = candidate.toLowerCase();
    if (nomesUsadosLower.find(candidateLower.toStdString()) == nomesUsadosLower.end()) {
        nomesUsadosLower.insert(candidateLower.toStdString());
        return candidate;
    }

    juce::String stem = candidate.upToLastOccurrenceOf(".", false, false);
    juce::String ext = candidate.fromLastOccurrenceOf(".", true, false);
    if (stem.isEmpty()) {
        stem = candidate;
        ext = "";
    }

    int idx = 2;
    while (true) {
        candidate = stem + "_" + juce::String(idx++) + ext;
        candidateLower = candidate.toLowerCase();
        if (nomesUsadosLower.find(candidateLower.toStdString()) == nomesUsadosLower.end()) {
            nomesUsadosLower.insert(candidateLower.toStdString());
            return candidate;
        }
    }
}

bool ExportZipDialog::ehExtensaoComprimida(const juce::String& extensao) {
    auto ext = extensao.toLowerCase();
    if (ext.startsWith(".")) ext = ext.substring(1);
    static const std::unordered_set<std::string> compExts = {
        "jpg", "jpeg", "png", "webp", "gif", "tiff", "tif", "heic", "heif",
        "mp3", "aac", "m4a", "ogg", "flac", "wav", "aif", "aiff", "alac", "wma",
        "mp4", "mov", "mkv", "avi", "wmv", "m4v", "webm",
        "zip", "gz", "bz2", "xz", "7z", "rar", "tar", "pdf"
    };
    return compExts.count(ext.toStdString()) > 0;
}

// ==============================================================================
// Thread de Exportação em Segundo Plano
// ==============================================================================

class ExportZipDialog::ExportThread : public juce::Thread {
public:
    struct Params {
        juce::File pastaDestino;
        juce::String nomeArquivoZip;
        bool incluirAssociados = false;
        juce::String metaTitulo;
        juce::String metaResponsavel;
        juce::String metaDescricao;
        juce::String metaData;
        juce::String metaDireitos;
    };

    ExportThread(ProjetoAberto& projeto,
                 Params params,
                 std::function<void(double, const juce::String&)> onProgresso,
                 std::function<void(bool, const juce::String&, const juce::File&, int, int, const juce::StringArray&)> onConcluido)
        : juce::Thread("ExportZipThread"),
          projeto_(projeto),
          params_(std::move(params)),
          onProgresso_(std::move(onProgresso)),
          onConcluido_(std::move(onConcluido)) {}

    ~ExportThread() override {
        stopThread(3000);
    }

    void run() override {
        juce::File arquivoFinal = params_.pastaDestino.getChildFile(params_.nomeArquivoZip);
        if (!arquivoFinal.getFileName().endsWithIgnoreCase(".zip")) {
            arquivoFinal = arquivoFinal.withFileExtension("zip");
        }
        juce::File arquivoPart = params_.pastaDestino.getChildFile(arquivoFinal.getFileName() + ".part");

        auto ids = projeto_.idsMarcados(ProjetoAberto::TipoMarcacao::Zip);
        if (ids.empty()) {
            notificarFim(false, matriz::i18n::t("zip.erro_sem_itens"), juce::File(), 0, 0, {});
            return;
        }

        notificarProgresso(0.05, matriz::i18n::t("zip.progresso_iniciando"));

        struct ItemExportar {
            std::string itemId;
            std::string codigoAcervo;
            std::string titulo;
            std::string tipoMidia;
            std::vector<std::pair<std::string, bool>> arquivos; // arquivoId, ehMaster
        };

        std::vector<ItemExportar> itensParaExportar;
        auto& db = projeto_.projeto().registro();

        for (const auto& itemId : ids) {
            if (threadShouldExit()) { limparTemp(arquivoPart); return; }

            ItemExportar ie;
            ie.itemId = itemId;
            try {
                auto stmt = db.prepare("SELECT codigo_acervo, titulo, tipo_midia FROM item WHERE id = ?");
                stmt.bind(1, matriz::db::Value::of(itemId));
                if (stmt.step()) {
                    ie.codigoAcervo = stmt.columnText(0);
                    ie.titulo = stmt.columnText(1);
                    ie.tipoMidia = stmt.columnText(2);
                }
            } catch (...) {}

            try {
                auto stmtArq = db.prepare(params_.incluirAssociados
                    ? "SELECT id, eh_master FROM arquivo WHERE item_id = ? ORDER BY eh_master DESC, id ASC"
                    : "SELECT id, eh_master FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
                stmtArq.bind(1, matriz::db::Value::of(itemId));
                while (stmtArq.step()) {
                    ie.arquivos.push_back({stmtArq.columnText(0), stmtArq.columnInt(1) != 0});
                }
                // Fallback se não achou master
                if (ie.arquivos.empty()) {
                    auto stmtAny = db.prepare("SELECT id, eh_master FROM arquivo WHERE item_id = ? LIMIT 1");
                    stmtAny.bind(1, matriz::db::Value::of(itemId));
                    if (stmtAny.step()) {
                        ie.arquivos.push_back({stmtAny.columnText(0), stmtAny.columnInt(1) != 0});
                    }
                }
            } catch (...) {}

            itensParaExportar.push_back(std::move(ie));
        }

        // Resolver arquivos em disco
        struct ArquivoParaZip {
            juce::File arquivoDisco;
            juce::String caminhoNoZip;
            juce::String hashSha256;
            int compressionLevel = 0;
            juce::int64 tamanhoBytes = 0;
        };

        std::vector<ArquivoParaZip> arquivosNoZip;
        juce::StringArray arquivosIgnorados;
        std::set<std::string> nomesUsadosLower;
        juce::int64 tamanhoTotalBytes = 0;

        for (const auto& ie : itensParaExportar) {
            if (threadShouldExit()) { limparTemp(arquivoPart); return; }

            for (const auto& [arqId, ehMaster] : ie.arquivos) {
                auto resolvido = matriz::vault::resolverArquivo(db, arqId, projeto_.projeto().pasta());
                if (!resolvido.has_value() || !resolvido->existsAsFile()) {
                    juce::String rotulo = ie.codigoAcervo.empty() ? juce::String(ie.itemId) : juce::String(ie.codigoAcervo);
                    if (!ie.titulo.empty()) rotulo += " (" + juce::String(ie.titulo) + ")";
                    arquivosIgnorados.add(rotulo + " [Arquivo ID: " + juce::String(arqId) + "]");
                    continue;
                }

                juce::File arq = *resolvido;
                juce::int64 sz = arq.getSize();

                // Verificação de limite de 3.5 GB por arquivo
                if (sz > ExportZipDialog::kLimiteMaximoSeguroBytes) {
                    juce::String msg = juce::String::formatted(
                        matriz::i18n::t("zip.erro_limite_arquivo").toRawUTF8(),
                        arq.getFileName().toRawUTF8(),
                        juce::File::descriptionOfSizeInBytes(sz).toRawUTF8());
                    notificarFim(false, msg, juce::File(), 0, 0, {});
                    return;
                }

                tamanhoTotalBytes += sz;
                if (tamanhoTotalBytes > ExportZipDialog::kLimiteMaximoSeguroBytes) {
                    juce::String msg = juce::String::formatted(
                        matriz::i18n::t("zip.erro_limite_tamanho").toRawUTF8(),
                        juce::File::descriptionOfSizeInBytes(tamanhoTotalBytes).toRawUTF8());
                    notificarFim(false, msg, juce::File(), 0, 0, {});
                    return;
                }

                juce::String nomeSanitizado = ExportZipDialog::sanitizarNomeArquivoZip(arq.getFileName());
                juce::String nomeFinalNoZip = ExportZipDialog::resolverColisaoNome(nomeSanitizado, nomesUsadosLower);

                // Checksum SHA-256
                juce::String hash;
                try {
                    auto stmtHash = db.prepare("SELECT checksum_sha256 FROM arquivo WHERE id = ?");
                    stmtHash.bind(1, matriz::db::Value::of(arqId));
                    if (stmtHash.step() && !stmtHash.columnIsNull(0)) {
                        hash = stmtHash.columnText(0);
                    }
                } catch (...) {}
                if (hash.isEmpty()) {
                    hash = juce::SHA256(arq).toHexString().toLowerCase();
                }

                int compLevel = ExportZipDialog::ehExtensaoComprimida(arq.getFileExtension()) ? 0 : 6;

                arquivosNoZip.push_back({arq, nomeFinalNoZip, hash, compLevel, sz});
            }
        }

        if (arquivosNoZip.empty()) {
            juce::String msg = matriz::i18n::t("zip.erro_sem_itens");
            if (!arquivosIgnorados.isEmpty()) {
                msg += " (" + juce::String(arquivosIgnorados.size()) + " arquivos offline/ignorados)";
            }
            notificarFim(false, msg, juce::File(), (int)itensParaExportar.size(), 0, arquivosIgnorados);
            return;
        }

        // Monta manifesto.txt e checksums.sha256
        juce::String manifesto;
        manifesto += "=======================================================\n";
        manifesto += " MATRIZ ARCHIVE EXPORT PACKAGE (MANIFEST)\n";
        manifesto += "=======================================================\n\n";

        manifesto += "Title: " + (params_.metaTitulo.isNotEmpty() ? params_.metaTitulo : projeto_.projeto().nome()) + "\n";
        if (params_.metaResponsavel.isNotEmpty()) manifesto += "Author/Responsible: " + params_.metaResponsavel + "\n";
        if (params_.metaData.isNotEmpty()) manifesto += "Date: " + params_.metaData + "\n";
        if (params_.metaDireitos.isNotEmpty()) manifesto += "Rights / License: " + params_.metaDireitos + "\n";
        if (params_.metaDescricao.isNotEmpty()) {
            manifesto += "Description / Notes:\n" + params_.metaDescricao + "\n";
        }
        manifesto += "Export Timestamp (UTC): " + juce::String(matriz::model::agoraIso8601()) + "\n";
        manifesto += "Total Assets Marked: " + juce::String((int)itensParaExportar.size()) + "\n";
        manifesto += "Files Exported: " + juce::String((int)arquivosNoZip.size()) + "\n";
        manifesto += "Files Skipped/Offline: " + juce::String(arquivosIgnorados.size()) + "\n\n";

        manifesto += "-------------------------------------------------------\n";
        manifesto += " ASSETS INVENTORY\n";
        manifesto += "-------------------------------------------------------\n";
        for (const auto& ie : itensParaExportar) {
            manifesto += "- Item ID: " + juce::String(ie.itemId) + "\n";
            manifesto += "  Code: " + (ie.codigoAcervo.empty() ? juce::String("N/A") : juce::String(ie.codigoAcervo)) + "\n";
            manifesto += "  Title: " + (ie.titulo.empty() ? juce::String("N/A") : juce::String(ie.titulo)) + "\n";
            manifesto += "  Type: " + juce::String(ie.tipoMidia) + "\n";
        }
        manifesto += "\n";

        if (!arquivosIgnorados.isEmpty()) {
            manifesto += "-------------------------------------------------------\n";
            manifesto += " SKIPPED / OFFLINE FILES\n";
            manifesto += "-------------------------------------------------------\n";
            for (const auto& ign : arquivosIgnorados) {
                manifesto += "- " + ign + "\n";
            }
            manifesto += "\n";
        }

        juce::String checksums;
        for (const auto& az : arquivosNoZip) {
            checksums += az.hashSha256 + "  " + az.caminhoNoZip + "\n";
        }

        // Calcula hash do manifesto e inclui no checksums
        juce::MemoryBlock mbManifesto(manifesto.toRawUTF8(), (size_t)manifesto.getNumBytesAsUTF8());
        juce::String hashManifesto = juce::SHA256(mbManifesto.getData(), mbManifesto.getSize()).toHexString().toLowerCase();
        checksums += hashManifesto + "  manifesto.txt\n";

        // Cria pasta temporária para manifesto e checksums
        juce::File pastaTemp = juce::File::createTempFile("bkr_zip_meta");
        pastaTemp.deleteFile();
        pastaTemp.createDirectory();

        juce::File arqManifestoTemp = pastaTemp.getChildFile("manifesto.txt");
        arqManifestoTemp.replaceWithText(manifesto);

        juce::File arqChecksumsTemp = pastaTemp.getChildFile("checksums.sha256");
        arqChecksumsTemp.replaceWithText(checksums);

        // Prepara juce::ZipFile::Builder
        juce::ZipFile::Builder builder;
        for (const auto& az : arquivosNoZip) {
            builder.addFile(az.arquivoDisco, az.compressionLevel, az.caminhoNoZip);
        }
        builder.addFile(arqManifestoTemp, 6, "manifesto.txt");
        builder.addFile(arqChecksumsTemp, 6, "checksums.sha256");

        // Grava no arquivo .part
        if (arquivoPart.exists()) arquivoPart.deleteFile();
        auto outStream = arquivoPart.createOutputStream();
        if (!outStream || outStream->failedToOpen()) {
            pastaTemp.deleteRecursively();
            notificarFim(false, "Falha ao criar arquivo temporário: " + arquivoPart.getFullPathName(), juce::File(), 0, 0, {});
            return;
        }

        notificarProgresso(0.15, matriz::i18n::t("zip.progresso_processando"));

        double progressFraction = 0.0;
        bool ok = false;

        // Thread monitor de cancelamento / progresso simples
        // Como writeToStream executa tudo de uma vez, passamos o ponteiro &progressFraction
        // e disparamos um timer/thread paralela para atualizar a barra de progresso.
        std::atomic<bool> escritaAtiva{true};
        std::thread monitorThread([this, &progressFraction, &escritaAtiva, totalArquivos = (int)arquivosNoZip.size()] {
            while (escritaAtiva.load()) {
                double p = 0.15 + (progressFraction * 0.80);
                juce::MessageManager::callAsync([this, p] {
                    if (onProgresso_) onProgresso_(p, matriz::i18n::t("zip.progresso_processando"));
                });
                for (int k = 0; k < 10 && escritaAtiva.load(); ++k) {
                    juce::Thread::sleep(50);
                }
            }
        });

        ok = builder.writeToStream(*outStream, &progressFraction);
        escritaAtiva.store(false);
        if (monitorThread.joinable()) monitorThread.join();

        outStream.reset(); // Fecha stream
        pastaTemp.deleteRecursively();

        if (threadShouldExit() || !ok) {
            limparTemp(arquivoPart);
            if (!threadShouldExit()) {
                notificarFim(false, "Erro ao gravar arquivo ZIP.", juce::File(), (int)itensParaExportar.size(), (int)arquivosNoZip.size(), arquivosIgnorados);
            }
            return;
        }

        notificarProgresso(0.98, matriz::i18n::t("zip.progresso_finalizando"));

        // Renomeia de .part para final
        if (arquivoFinal.exists()) arquivoFinal.deleteFile();
        if (!arquivoPart.moveFileTo(arquivoFinal)) {
            notificarFim(false, "Falha ao renomear arquivo temporário para destino final.", juce::File(), 0, 0, {});
            return;
        }

        notificarProgresso(1.0, matriz::i18n::t("zip.sucesso_titulo"));
        notificarFim(true, "", arquivoFinal, (int)itensParaExportar.size(), (int)arquivosNoZip.size(), arquivosIgnorados);
    }

private:
    void limparTemp(juce::File& f) {
        if (f.exists()) f.deleteFile();
    }

    void notificarProgresso(double p, const juce::String& msg) {
        juce::MessageManager::callAsync([this, p, msg] {
            if (onProgresso_) onProgresso_(p, msg);
        });
    }

    void notificarFim(bool sucesso, const juce::String& erro, const juce::File& arq, int totalItens, int exportados, const juce::StringArray& ignorados) {
        juce::MessageManager::callAsync([this, sucesso, erro, arq, totalItens, exportados, ignorados] {
            if (onConcluido_) onConcluido_(sucesso, erro, arq, totalItens, exportados, ignorados);
        });
    }

    ProjetoAberto& projeto_;
    Params params_;
    std::function<void(double, const juce::String&)> onProgresso_;
    std::function<void(bool, const juce::String&, const juce::File&, int, int, const juce::StringArray&)> onConcluido_;
};

// ==============================================================================
// Componente de Diálogo: ExportZipDialog
// ==============================================================================

ExportZipDialog::ExportZipDialog(ProjetoAberto& projeto)
    : projeto_(projeto) {
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    lblTitulo_ = std::make_unique<juce::Label>("lblTitulo", matriz::i18n::t("zip.titulo"));
    lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(*lblTitulo_);

    lblSubtitulo_ = std::make_unique<juce::Label>("lblSubtitulo", matriz::i18n::t("zip.subtitulo"));
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblSubtitulo_);

    // 1. Grupo Arquivo e Destino
    grpArquivo_ = std::make_unique<juce::GroupComponent>("grpArquivo", matriz::i18n::t("zip.grp_arquivo"));
    grpArquivo_->setColour(juce::GroupComponent::outlineColourId, tk.borda);
    grpArquivo_->setColour(juce::GroupComponent::textColourId, tk.acento);
    addAndMakeVisible(*grpArquivo_);

    lblNomeArquivo_ = std::make_unique<juce::Label>("lblNome", matriz::i18n::t("zip.nome_arquivo"));
    lblNomeArquivo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblNomeArquivo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblNomeArquivo_);

    juce::String nomeSugerido = projeto_.projeto().nome() + "_export.zip";
    txtNomeArquivo_ = std::make_unique<juce::TextEditor>("txtNomeArquivo");
    txtNomeArquivo_->setText(nomeSugerido);
    txtNomeArquivo_->setColour(juce::TextEditor::backgroundColourId, tk.painel);
    txtNomeArquivo_->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    txtNomeArquivo_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    addAndMakeVisible(*txtNomeArquivo_);

    lblPastaDestino_ = std::make_unique<juce::Label>("lblPasta", matriz::i18n::t("zip.pasta_destino"));
    lblPastaDestino_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblPastaDestino_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblPastaDestino_);

    pastaDestinoSelecionada_ = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    lblCaminhoPasta_ = std::make_unique<juce::Label>("lblCaminho", pastaDestinoSelecionada_.getFullPathName());
    lblCaminhoPasta_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblCaminhoPasta_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblCaminhoPasta_);

    btnEscolherPasta_ = std::make_unique<juce::TextButton>(matriz::i18n::t("zip.escolher_pasta"));
    btnEscolherPasta_->onClick = [this] { escolherPastaDestino(); };
    addAndMakeVisible(*btnEscolherPasta_);

    chkIncluirAssociados_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("zip.incluir_associados"));
    chkIncluirAssociados_->setToggleState(false, juce::dontSendNotification);
    chkIncluirAssociados_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    addAndMakeVisible(*chkIncluirAssociados_);

    // 2. Grupo Metadados
    grpMetadados_ = std::make_unique<juce::GroupComponent>("grpMetadados", matriz::i18n::t("zip.grp_metadados"));
    grpMetadados_->setColour(juce::GroupComponent::outlineColourId, tk.borda);
    grpMetadados_->setColour(juce::GroupComponent::textColourId, tk.acento);
    addAndMakeVisible(*grpMetadados_);

    auto criarCampoMeta = [this, &tk](const juce::String& rotulo, std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& txt) {
        lbl = std::make_unique<juce::Label>("", rotulo);
        lbl->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        lbl->setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(*lbl);

        txt = std::make_unique<juce::TextEditor>();
        txt->setColour(juce::TextEditor::backgroundColourId, tk.painel);
        txt->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        txt->setColour(juce::TextEditor::outlineColourId, tk.borda);
        addAndMakeVisible(*txt);
    };

    criarCampoMeta(matriz::i18n::t("zip.meta_titulo"), lblMetaTitulo_, txtMetaTitulo_);
    txtMetaTitulo_->setText(projeto_.projeto().nome());

    criarCampoMeta(matriz::i18n::t("zip.meta_responsavel"), lblMetaResponsavel_, txtMetaResponsavel_);
    criarCampoMeta(matriz::i18n::t("zip.meta_descricao"), lblMetaDescricao_, txtMetaDescricao_);

    criarCampoMeta(matriz::i18n::t("zip.meta_data"), lblMetaData_, txtMetaData_);
    txtMetaData_->setText(juce::Time::getCurrentTime().formatted("%Y-%m-%d"));

    criarCampoMeta(matriz::i18n::t("zip.meta_direitos"), lblMetaDireitos_, txtMetaDireitos_);

    // Progresso e Status
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    barraProgresso_->setColour(juce::ProgressBar::foregroundColourId, tk.acento);
    barraProgresso_->setColour(juce::ProgressBar::backgroundColourId, tk.painel);
    barraProgresso_->setVisible(false);
    addAndMakeVisible(*barraProgresso_);

    lblStatusProgresso_ = std::make_unique<juce::Label>("lblStatus", "");
    lblStatusProgresso_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    lblStatusProgresso_->setColour(juce::Label::textColourId, tk.textoSecundario);
    lblStatusProgresso_->setVisible(false);
    addAndMakeVisible(*lblStatusProgresso_);

    // Botões
    btnCancelar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("zip.btn_cancelar"));
    btnCancelar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnCancelar_->onClick = [this] {
        if (exportando_) cancelarExportacao();
        else fecharDialogo();
    };
    addAndMakeVisible(*btnCancelar_);

    btnExportar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("zip.btn_exportar"));
    btnExportar_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnExportar_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnExportar_->onClick = [this] { iniciarExportacao(); };
    addAndMakeVisible(*btnExportar_);

    setSize(640, 560);
}

ExportZipDialog::~ExportZipDialog() {
    if (threadExportacao_) {
        threadExportacao_->stopThread(2000);
    }
}

void ExportZipDialog::fecharDialogo() {
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
    }
    if (aoFechar) aoFechar();
}

bool ExportZipDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (exportando_) cancelarExportacao();
        else fecharDialogo();
        return true;
    }
    return false;
}

void ExportZipDialog::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);
}

void ExportZipDialog::resized() {
    auto area = getLocalBounds().reduced(20, 16);

    lblTitulo_->setBounds(area.removeFromTop(28));
    lblSubtitulo_->setBounds(area.removeFromTop(20));
    area.removeFromTop(10);

    // 1. Arquivo e Destino (150px)
    auto arqArea = area.removeFromTop(145);
    grpArquivo_->setBounds(arqArea);
    auto innerArq = arqArea.reduced(14, 0);
    innerArq.removeFromTop(22);

    auto r1 = innerArq.removeFromTop(26);
    lblNomeArquivo_->setBounds(r1.removeFromLeft(120));
    r1.removeFromLeft(6);
    txtNomeArquivo_->setBounds(r1);

    innerArq.removeFromTop(6);
    auto r2 = innerArq.removeFromTop(26);
    lblPastaDestino_->setBounds(r2.removeFromLeft(120));
    r2.removeFromLeft(6);
    btnEscolherPasta_->setBounds(r2.removeFromRight(130));
    r2.removeFromRight(8);
    lblCaminhoPasta_->setBounds(r2);

    innerArq.removeFromTop(8);
    chkIncluirAssociados_->setBounds(innerArq.removeFromTop(24));

    area.removeFromTop(10);

    // 2. Metadados (170px)
    auto metaArea = area.removeFromTop(170);
    grpMetadados_->setBounds(metaArea);
    auto innerMeta = metaArea.reduced(14, 0);
    innerMeta.removeFromTop(22);

    auto mRow1 = innerMeta.removeFromTop(24);
    lblMetaTitulo_->setBounds(mRow1.removeFromLeft(90));
    txtMetaTitulo_->setBounds(mRow1);

    innerMeta.removeFromTop(4);
    auto mRow2 = innerMeta.removeFromTop(24);
    lblMetaResponsavel_->setBounds(mRow2.removeFromLeft(90));
    txtMetaResponsavel_->setBounds(mRow2);

    innerMeta.removeFromTop(4);
    auto mRow3 = innerMeta.removeFromTop(24);
    lblMetaDescricao_->setBounds(mRow3.removeFromLeft(90));
    txtMetaDescricao_->setBounds(mRow3);

    innerMeta.removeFromTop(4);
    auto mRow4 = innerMeta.removeFromTop(24);
    lblMetaData_->setBounds(mRow4.removeFromLeft(90));
    txtMetaData_->setBounds(mRow4.removeFromLeft(120));
    mRow4.removeFromLeft(14);
    lblMetaDireitos_->setBounds(mRow4.removeFromLeft(90));
    txtMetaDireitos_->setBounds(mRow4);

    area.removeFromTop(10);

    // Progresso e Botões na base
    auto baseArea = area.removeFromBottom(70);
    auto btnRow = baseArea.removeFromBottom(34);
    btnCancelar_->setBounds(btnRow.removeFromLeft(110));
    btnExportar_->setBounds(btnRow.removeFromRight(160));

    baseArea.removeFromBottom(6);
    barraProgresso_->setBounds(baseArea.removeFromBottom(14));
    baseArea.removeFromBottom(2);
    lblStatusProgresso_->setBounds(baseArea);
}

void ExportZipDialog::lookAndFeelChanged() {
    repaint();
}

void ExportZipDialog::escolherPastaDestino() {
    auto chooser = std::make_shared<juce::FileChooser>(
        matriz::i18n::t("zip.escolher_pasta"),
        pastaDestinoSelecionada_.exists() ? pastaDestinoSelecionada_ : juce::File::getSpecialLocation(juce::File::userHomeDirectory));

    juce::Component::SafePointer<ExportZipDialog> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis, chooser](const juce::FileChooser& fc) {
                              if (!safeThis) return;
                              juce::File d = fc.getResult();
                              if (d.isDirectory()) {
                                  safeThis->pastaDestinoSelecionada_ = d;
                                  safeThis->lblCaminhoPasta_->setText(d.getFullPathName(), juce::dontSendNotification);
                              }
                          });
}

void ExportZipDialog::iniciarExportacao() {
    if (exportando_) return;

    juce::String nome = txtNomeArquivo_->getText().trim();
    if (nome.isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            matriz::i18n::t("zip.titulo"),
            matriz::i18n::t("zip.erro_nome_vazio"));
        return;
    }

    if (!pastaDestinoSelecionada_.isDirectory()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            matriz::i18n::t("zip.titulo"),
            matriz::i18n::t("zip.erro_sem_destino"));
        return;
    }

    if (projeto_.contarMarcacoes(ProjetoAberto::TipoMarcacao::Zip) == 0) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            matriz::i18n::t("zip.titulo"),
            matriz::i18n::t("zip.erro_sem_itens"));
        return;
    }

    exportando_ = true;
    btnExportar_->setEnabled(false);
    btnEscolherPasta_->setEnabled(false);
    txtNomeArquivo_->setEnabled(false);
    chkIncluirAssociados_->setEnabled(false);

    barraProgresso_->setVisible(true);
    lblStatusProgresso_->setVisible(true);
    progressoValor_ = 0.0;
    lblStatusProgresso_->setText(matriz::i18n::t("zip.progresso_iniciando"), juce::dontSendNotification);

    ExportThread::Params params;
    params.pastaDestino = pastaDestinoSelecionada_;
    params.nomeArquivoZip = nome;
    params.incluirAssociados = chkIncluirAssociados_->getToggleState();
    params.metaTitulo = txtMetaTitulo_->getText().trim();
    params.metaResponsavel = txtMetaResponsavel_->getText().trim();
    params.metaDescricao = txtMetaDescricao_->getText().trim();
    params.metaData = txtMetaData_->getText().trim();
    params.metaDireitos = txtMetaDireitos_->getText().trim();

    juce::Component::SafePointer<ExportZipDialog> safeThis(this);

    threadExportacao_ = std::make_unique<ExportThread>(
        projeto_,
        params,
        [safeThis](double p, const juce::String& msg) {
            if (!safeThis) return;
            safeThis->progressoValor_ = p;
            safeThis->lblStatusProgresso_->setText(msg, juce::dontSendNotification);
        },
        [safeThis](bool sucesso, const juce::String& erro, const juce::File& arqZip, int totalItens, int exportados, const juce::StringArray& ignorados) {
            if (!safeThis) return;
            safeThis->exportando_ = false;
            safeThis->btnExportar_->setEnabled(true);
            safeThis->btnEscolherPasta_->setEnabled(true);
            safeThis->txtNomeArquivo_->setEnabled(true);
            safeThis->chkIncluirAssociados_->setEnabled(true);
            safeThis->barraProgresso_->setVisible(false);
            safeThis->lblStatusProgresso_->setVisible(false);

            if (!sucesso) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    matriz::i18n::t("zip.titulo"),
                    erro.isNotEmpty() ? erro : "Falha na exportação ZIP.");
                return;
            }

            juce::String msg = juce::String::formatted(
                matriz::i18n::t("zip.sucesso_msg").toRawUTF8(),
                arqZip.getFullPathName().toRawUTF8(),
                totalItens,
                exportados,
                ignorados.size());

            if (!ignorados.isEmpty()) {
                msg += "\n\n" + (matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Itens ignorados/ausentes:\n") : "Ignored/missing assets:\n");
                for (int i = 0; i < std::min(5, ignorados.size()); ++i) {
                    msg += "• " + ignorados[i] + "\n";
                }
                if (ignorados.size() > 5) {
                    msg += "... (+" + juce::String(ignorados.size() - 5) + ")\n";
                }
            }

            auto* alert = new juce::AlertWindow(
                matriz::i18n::t("zip.sucesso_titulo"),
                msg,
                juce::AlertWindow::InfoIcon);

            alert->addButton(matriz::i18n::t("zip.btn_revelar"), 1);
            alert->addButton(matriz::i18n::t("zip.btn_fechar"), 0);

            alert->enterModalState(true, juce::ModalCallbackFunction::create([arqZip, safeThis](int result) {
                if (result == 1) {
                    arqZip.revealToUser();
                }
                if (safeThis) {
                    safeThis->fecharDialogo();
                }
            }), true);
        });

    threadExportacao_->startThread();
}

void ExportZipDialog::cancelarExportacao() {
    if (!exportando_ || !threadExportacao_) return;
    lblStatusProgresso_->setText(matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Cancelando...") : "Cancelling...", juce::dontSendNotification);
    threadExportacao_->signalThreadShouldExit();
}

void ExportZipDialog::exibirModal(ProjetoAberto& projeto) {
    auto dlg = std::make_unique<ExportZipDialog>(projeto);

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = matriz::i18n::t("zip.titulo");
    opt.content.set(dlg.release(), true);
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

} // namespace matriz::ui
