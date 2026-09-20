#include "BackupRecoveryDialog.h"

#include "../I18n/Strings.h"
#include "../Model/ProjectLog.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

// ==============================================================================
// Componente de Navegação e Recuperação restrito à pasta do Backup
// ==============================================================================
class RecoveryBrowserComponent : public juce::Component,
                                 private juce::FileBrowserListener {
public:
    RecoveryBrowserComponent(ProjetoAberto& projeto, const BackupVersionRef& versao)
        : projeto_(projeto), versao_(versao)
    {
        const auto& tk = tema();
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

        juce::File pastaRaiz(versao_.destinoPath);

        lblHeader_.setText((isPt ? "Navegador de Recuperação: [" : "Recovery Browser: [") +
                           versao_.rotulo + "]  —  " + versao_.destinoPath, juce::dontSendNotification);
        lblHeader_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        lblHeader_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblHeader_);

        int browserFlags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::canSelectDirectories;

        fileBrowser_ = std::make_unique<juce::FileBrowserComponent>(browserFlags, pastaRaiz, nullptr, nullptr);
        fileBrowser_->addListener(this);
        addAndMakeVisible(*fileBrowser_);

        lblSelecao_.setText(isPt ? "Nenhum arquivo selecionado" : "No file selected", juce::dontSendNotification);
        lblSelecao_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        lblSelecao_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(lblSelecao_);

        btnCopiarPara_.setButtonText(matriz::i18n::t("backup.copiar_para_btn"));
        btnCopiarPara_.setColour(juce::TextButton::buttonColourId, tk.acento);
        btnCopiarPara_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
        btnCopiarPara_.setEnabled(false);
        btnCopiarPara_.onClick = [this] { executarCopiaPara(); };
        addAndMakeVisible(btnCopiarPara_);

        setSize(850, 580);
    }

    ~RecoveryBrowserComponent() override {
        if (fileBrowser_) fileBrowser_->removeListener(this);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(16);
        lblHeader_.setBounds(r.removeFromTop(28));
        r.removeFromTop(8);

        auto linhaInferior = r.removeFromBottom(36);
        btnCopiarPara_.setBounds(linhaInferior.removeFromRight(150));
        linhaInferior.removeFromRight(12);
        lblSelecao_.setBounds(linhaInferior);

        r.removeFromBottom(10);
        if (fileBrowser_) fileBrowser_->setBounds(r);
    }

    void selectionChanged() override {
        atualizarSelecao();
    }

    void fileClicked(const juce::File&, const juce::MouseEvent&) override {
        atualizarSelecao();
    }

    void fileDoubleClicked(const juce::File& f) override {
        if (f.existsAsFile()) {
            executarCopiaPara();
        }
    }

    void browserRootChanged(const juce::File&) override {}

private:
    void atualizarSelecao() {
        if (!fileBrowser_) return;
        juce::File sel = fileBrowser_->getSelectedFile(0);
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

        if (sel.existsAsFile()) {
            lblSelecao_.setText((isPt ? "Arquivo: " : "File: ") + sel.getFileName() + " (" +
                                juce::File::descriptionOfSizeInBytes(sel.getSize()) + ")",
                                juce::dontSendNotification);
            btnCopiarPara_.setEnabled(true);
        } else if (sel.isDirectory()) {
            lblSelecao_.setText((isPt ? "Pasta: " : "Folder: ") + sel.getFileName(), juce::dontSendNotification);
            btnCopiarPara_.setEnabled(false);
        } else {
            lblSelecao_.setText(isPt ? "Nenhum arquivo selecionado" : "No file selected", juce::dontSendNotification);
            btnCopiarPara_.setEnabled(false);
        }
    }

    void executarCopiaPara() {
        if (!fileBrowser_) return;
        juce::File arqOrigem = fileBrowser_->getSelectedFile(0);
        if (!arqOrigem.existsAsFile()) return;

        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

        fileChooser_ = std::make_unique<juce::FileChooser>(
            isPt ? "Escolha a pasta de destino para copiar o arquivo recuperado:"
                 : "Choose destination folder to copy recovered file:",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*");

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
        juce::Component::SafePointer<RecoveryBrowserComponent> safeThis(this);

        fileChooser_->launchAsync(flags, [safeThis, arqOrigem, isPt](const juce::FileChooser& fc) {
            if (!safeThis) return;
            auto destFolder = fc.getResult();
            if (!destFolder.isDirectory()) return;

            juce::File arqDestino = destFolder.getChildFile(arqOrigem.getFileName());

            // Se já existe no destino, confirma sobrescrita
            if (arqDestino.existsAsFile()) {
                bool sobrescrever = juce::AlertWindow::showOkCancelBox(
                    juce::AlertWindow::WarningIcon,
                    isPt ? "Arquivo já existe" : "File already exists",
                    isPt ? "O arquivo já existe na pasta escolhida. Deseja substituí-lo?"
                         : "The file already exists in the destination folder. Do you want to replace it?",
                    isPt ? "Substituir" : "Replace",
                    isPt ? "Cancelar" : "Cancel");
                if (!sobrescrever) return;
                arqDestino.deleteFile();
            }

            if (arqOrigem.copyFileTo(arqDestino)) {
                // Registrar log do projeto conforme Seção 4
                matriz::model::ProjectLog pLog(safeThis->projeto_.projeto().pasta());
                juce::StringArray details;
                details.add("File: " + arqOrigem.getFileName());
                details.add("Source Version: " + safeThis->versao_.rotulo + " (" + safeThis->versao_.destinoPath + ")");
                details.add("Destination: " + destFolder.getFullPathName());
                pLog.appendEntry("File Recovered from Backup", details);

                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    isPt ? "Arquivo Recuperado" : "File Recovered",
                    (isPt ? juce::String::fromUTF8("Arquivo copiado com sucesso para:\n")
                          : "File successfully copied to:\n") + arqDestino.getFullPathName());
            } else {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    isPt ? "Erro na Cópia" : "Copy Error",
                    isPt ? juce::String::fromUTF8("Não foi possível copiar o arquivo para a pasta de destino.")
                         : "Could not copy file to destination folder.");
            }
        });
    }

    ProjetoAberto& projeto_;
    BackupVersionRef versao_;
    juce::Label lblHeader_;
    std::unique_ptr<juce::FileBrowserComponent> fileBrowser_;
    juce::Label lblSelecao_;
    juce::TextButton btnCopiarPara_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
};

// Diálogo intermediário de seleção de versão para recuperar
class SelectVersionToRecoverDialog : public juce::Component {
public:
    SelectVersionToRecoverDialog(const std::vector<BackupVersionRef>& versoes,
                                 std::function<void(const BackupVersionRef&)> onSelecionada,
                                 std::function<void()> onCancelar)
        : versoes_(versoes), onSelecionada_(std::move(onSelecionada)), onCancelar_(std::move(onCancelar))
    {
        const auto& tk = tema();
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

        lblTitulo_.setText(matriz::i18n::t("backup.recuperar_titulo"), juce::dontSendNotification);
        lblTitulo_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
        lblTitulo_.setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(lblTitulo_);

        lblDescricao_.setText(isPt ? juce::String::fromUTF8("Selecione qual Versão de Backup deseja navegar:")
                                   : "Select which Backup Version to browse:", juce::dontSendNotification);
        lblDescricao_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        lblDescricao_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(lblDescricao_);

        comboVersao_.setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
        comboVersao_.setColour(juce::ComboBox::textColourId, tk.textoPrimario);
        comboVersao_.setColour(juce::ComboBox::outlineColourId, tk.borda);
        for (size_t i = 0; i < versoes_.size(); ++i) {
            comboVersao_.addItem(versoes_[i].rotulo + " (" + versoes_[i].destinoPath + ")", static_cast<int>(i + 1));
        }
        comboVersao_.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(comboVersao_);

        btnAbrir_.setButtonText(isPt ? "Abrir Navegador" : "Open Browser");
        btnAbrir_.setColour(juce::TextButton::buttonColourId, tk.acento);
        btnAbrir_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
        btnAbrir_.onClick = [this] {
            int sel = comboVersao_.getSelectedId() - 1;
            if (sel >= 0 && sel < static_cast<int>(versoes_.size())) {
                auto v = versoes_[static_cast<size_t>(sel)];
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
                    dw->exitModalState(1);
                }
                if (onSelecionada_) onSelecionada_(v);
            }
        };
        addAndMakeVisible(btnAbrir_);

        btnCancelar_.setButtonText(isPt ? "Cancelar" : "Cancel");
        btnCancelar_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnCancelar_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnCancelar_.onClick = [this] {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
                dw->exitModalState(0);
            }
            if (onCancelar_) onCancelar_();
        };
        addAndMakeVisible(btnCancelar_);

        setSize(480, 190);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(20);
        lblTitulo_.setBounds(r.removeFromTop(28));
        r.removeFromTop(4);
        lblDescricao_.setBounds(r.removeFromTop(20));
        r.removeFromTop(12);
        comboVersao_.setBounds(r.removeFromTop(32));

        r.removeFromTop(16);
        auto linhaBotoes = r.removeFromBottom(32);
        btnCancelar_.setBounds(linhaBotoes.removeFromLeft(110));
        btnAbrir_.setBounds(linhaBotoes.removeFromRight(150));
    }

private:
    std::vector<BackupVersionRef> versoes_;
    std::function<void(const BackupVersionRef&)> onSelecionada_;
    std::function<void()> onCancelar_;

    juce::Label lblTitulo_;
    juce::Label lblDescricao_;
    juce::ComboBox comboVersao_;
    juce::TextButton btnAbrir_;
    juce::TextButton btnCancelar_;
};

void abrirJanelaNavegador(ProjetoAberto& projeto, const BackupVersionRef& versao) {
    juce::File pasta(versao.destinoPath);
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    if (!pasta.isDirectory()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            matriz::i18n::t("backup.recuperar_titulo"),
            isPt ? juce::String::fromUTF8("A pasta desta versão de backup não está acessível no momento:\n") + versao.destinoPath
                 : "The folder for this backup version is currently inaccessible:\n" + versao.destinoPath);
        return;
    }

    auto browserComp = std::make_unique<RecoveryBrowserComponent>(projeto, versao);

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(browserComp.release());
    opt.dialogTitle = matriz::i18n::t("backup.recuperar_titulo") + " — " + versao.rotulo;
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;
    opt.launchAsync();
}

} // namespace

void BackupRecoveryDialog::showRecoveryDialog(ProjetoAberto& projeto,
                                             const std::vector<BackupVersionRef>& versoes)
{
    if (versoes.empty()) {
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            matriz::i18n::t("backup.recuperar_titulo"),
            isPt ? juce::String::fromUTF8("Nenhuma Versão de Backup cadastrada para navegar.")
                 : "No registered Backup Versions to browse.");
        return;
    }

    if (versoes.size() == 1) {
        abrirJanelaNavegador(projeto, versoes[0]);
        return;
    }

    auto onSelecionada = [&projeto](const BackupVersionRef& versao) {
        juce::MessageManager::callAsync([&projeto, versao] {
            abrirJanelaNavegador(projeto, versao);
        });
    };

    auto comp = std::make_unique<SelectVersionToRecoverDialog>(versoes, std::move(onSelecionada), nullptr);

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(comp.release());
    opt.dialogTitle = matriz::i18n::t("backup.recuperar_titulo");
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

} // namespace matriz::ui
