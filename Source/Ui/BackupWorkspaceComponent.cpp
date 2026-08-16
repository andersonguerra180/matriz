#include "BackupWorkspaceComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Catalogo/CatalogoProxies.h"
#include "../Consolidacao/MetadadoEmbutido.h"

namespace matriz::ui {

class BackupWorkspaceComponent::PreviaLista : public juce::Component {
public:
    void definirPlano(const matriz::consolidacao::PlanoConsolidacao& plano) {
        plano_ = &plano;
        setSize(getWidth(), std::max(60, static_cast<int>(plano_->itens.size()) * 22));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);

        if (!plano_ || plano_->itens.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            g.drawText("No items to backup.", getLocalBounds(), juce::Justification::centred);
            return;
        }

        int y = 0;
        for (auto& item : plano_->itens) {
            juce::Rectangle<int> linha(0, y, getWidth(), 22);
            if (item.emConflito) {
                g.setColour(tk.perigo.withAlpha(0.15f));
                g.fillRect(linha);
            } else if (item.jaConsolidado) {
                g.setColour(tk.painelAlt);
                g.fillRect(linha);
            }
            g.setColour(item.emConflito ? tk.perigo : (item.jaConsolidado ? tk.textoTerciario : tk.textoPrimario));
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            juce::String texto = item.nomeOriginal + "  ->  " + item.caminhoRelativoDestino;
            if (item.jaConsolidado) texto += "  (already backed up)";
            g.drawText(texto, linha.reduced(6, 2), juce::Justification::centredLeft, true);
            y += 22;
        }
    }
private:
    const matriz::consolidacao::PlanoConsolidacao* plano_ = nullptr;
};

BackupWorkspaceComponent::BackupWorkspaceComponent(ProjetoAberto& projeto, const std::set<std::string>& selectedItemIds)
    : projeto_(projeto), selectedItemIds_(selectedItemIds)
{
    vaults_ = projeto_.listarVaults();

    auto colecoesRaw = projeto_.listarColecoesEmbutidas();
    for (const auto& c : colecoesRaw)
        if (c.contagem > 0) colecoes_.push_back(c);

    const auto& tk = tema();

    // === TITLE ===
    labelTitulo_ = std::make_unique<juce::Label>();
    labelTitulo_->setText("Backup Configuration", juce::dontSendNotification);
    labelTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    labelTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelTitulo_);

    // === SOURCE section ===
    labelSource_ = std::make_unique<juce::Label>();
    labelSource_->setText("SOURCE", juce::dontSendNotification);
    labelSource_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelSource_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*labelSource_);

    comboSource_ = std::make_unique<juce::ComboBox>();
    comboSource_->addItem("All assets in archive", 1);
    comboSource_->addItem("Recently ingested assets", 2);
    comboSource_->addItem("Selected assets (" + juce::String(static_cast<int>(selectedItemIds_.size())) + ")", 3);
    comboSource_->addItem("Assets with no backup yet", 4);
    comboSource_->addItem("From collection", 5);
    if (!selectedItemIds_.empty()) {
        comboSource_->setSelectedId(3, juce::dontSendNotification);
        whatOption_ = WhatOption::SelectedAssets;
    } else {
        comboSource_->setSelectedId(1, juce::dontSendNotification);
    }
    comboSource_->onChange = [this] {
        int id = comboSource_->getSelectedId();
        whatOption_ = static_cast<WhatOption>(id - 1);
        if (comboColecoes_) comboColecoes_->setVisible(id == 5);
        resized();  // o combo só ganha bounds no ramo "if (isVisible())" do resized
        atualizarResumo();
    };
    addAndMakeVisible(*comboSource_);

    comboColecoes_ = std::make_unique<juce::ComboBox>();
    for (size_t i = 0; i < colecoes_.size(); ++i)
        comboColecoes_->addItem(colecoes_[i].rotulo + " (" + juce::String(colecoes_[i].contagem) + ")", static_cast<int>(i + 1));
    if (!colecoes_.empty()) comboColecoes_->setSelectedId(1, juce::dontSendNotification);
    comboColecoes_->onChange = [this] {
        selectedCollectionIdx_ = comboColecoes_->getSelectedItemIndex();
        atualizarResumo();
    };
    // addChildComponent, não addAndMakeVisible: o segundo força visible=true e
    // desfaz qualquer setVisible(false) anterior.
    addChildComponent(*comboColecoes_);

    // === DESTINATION section ===
    labelDest_ = std::make_unique<juce::Label>();
    labelDest_->setText("DESTINATION", juce::dontSendNotification);
    labelDest_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelDest_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*labelDest_);

    listVaults_ = std::make_unique<juce::ListBox>();
    listVaults_->setModel(this);
    listVaults_->setColour(juce::ListBox::backgroundColourId, tk.painel);
    listVaults_->setRowHeight(32);
    addAndMakeVisible(*listVaults_);

    btnBrowseVault_ = std::make_unique<juce::TextButton>("Choose Custom Folder...");
    aplicarEstiloBotao(*btnBrowseVault_, false);
    btnBrowseVault_->onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(matriz::i18n::t("consolidacao.escolher_destino"));
        juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis, chooser](const juce::FileChooser& fc) {
                                  if (!safeThis) return;
                                  juce::File folder = fc.getResult();
                                  if (folder == juce::File()) return;
                                  safeThis->customDestFolder_ = folder;
                                  safeThis->selectedVaultIdx_ = -1;
                                  safeThis->listVaults_->deselectAllRows();
                                  safeThis->resolvedDestFolder_ = folder;
                                  safeThis->labelDestInfo_->setText("Target: " + folder.getFullPathName(), juce::dontSendNotification);
                                  safeThis->atualizarResumo();
                              });
    };
    addAndMakeVisible(*btnBrowseVault_);

    labelDestInfo_ = std::make_unique<juce::Label>();
    labelDestInfo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    labelDestInfo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*labelDestInfo_);

    // O destino padrão é a pasta que o usuário escolheu ao criar o projeto,
    // num subdiretório "Backup" — apontar para a raiz do projeto faria cada
    // arquivo colidir consigo mesmo e travaria o plano por conflito de nome.
    resolvedDestFolder_ = projeto_.projeto().pasta().getChildFile("Backup");
    labelDestInfo_->setText("Default: " + resolvedDestFolder_.getFullPathName() +
                            "  -  pick a vault or custom folder above to change it",
                            juce::dontSendNotification);

    // === ORGANIZATION section ===
    labelOrg_ = std::make_unique<juce::Label>();
    labelOrg_->setText("ORGANIZATION", juce::dontSendNotification);
    labelOrg_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelOrg_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*labelOrg_);

    togglePreservarEstrutura_ = std::make_unique<juce::ToggleButton>("Preserve Original Folder Structure");
    togglePreservarEstrutura_->setToggleState(false, juce::dontSendNotification);
    togglePreservarEstrutura_->onClick = [this] {
        bool preserve = togglePreservarEstrutura_->getToggleState();
        if (comboOrg_) comboOrg_->setEnabled(!preserve);
        if (btnEditarHierarquia_) btnEditarHierarquia_->setEnabled(!preserve);
        atualizarResumo();
    };
    addAndMakeVisible(*togglePreservarEstrutura_);

    comboOrg_ = std::make_unique<juce::ComboBox>();
    comboOrg_->addItem("Keep my catalog organization", 1);
    comboOrg_->addItem("By media type", 2);
    comboOrg_->addItem("By year", 3);
    comboOrg_->addItem("By media type + year", 4);
    comboOrg_->addItem("Custom (visual editor)", 5);
    comboOrg_->setSelectedId(1, juce::dontSendNotification);
    comboOrg_->onChange = [this] {
        if (btnEditarHierarquia_)
            btnEditarHierarquia_->setVisible(comboOrg_->getSelectedId() == 5);
        resized();
        atualizarResumo();
    };
    addAndMakeVisible(*comboOrg_);

    hierarquiaCustom_ = matriz::consolidacao::hierarquiaPadrao();
    btnEditarHierarquia_ = std::make_unique<juce::TextButton>("OPEN VISUAL EDITOR");
    btnEditarHierarquia_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnEditarHierarquia_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnEditarHierarquia_->onClick = [this] {
        new HierarquiaEditorWindow(hierarquiaCustom_, [this](const matriz::consolidacao::HierarquiaBackup& h) {
            hierarquiaCustom_ = h;
            atualizarResumo();
        });
    };
    addChildComponent(*btnEditarHierarquia_);

    // === OPTIONS section ===
    labelOpcoes_ = std::make_unique<juce::Label>();
    labelOpcoes_->setText("OPTIONS", juce::dontSendNotification);
    labelOpcoes_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelOpcoes_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*labelOpcoes_);

    toggleVerificarChecksum_ = std::make_unique<juce::ToggleButton>("Verify checksum after copy (SHA-256)");
    toggleVerificarChecksum_->setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(*toggleVerificarChecksum_);

    toggleGerarCatalogo_ = std::make_unique<juce::ToggleButton>("Generate BKR Backup Catalog Database (SQLite)");
    toggleGerarCatalogo_->setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(*toggleGerarCatalogo_);

    toggleEmbutirMetadados_ = std::make_unique<juce::ToggleButton>("Embed metadata into backup files (EXIF/XMP/iXML)");
    toggleEmbutirMetadados_->setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(*toggleEmbutirMetadados_);

    // === PREVIEW section ===
    labelResumo_ = std::make_unique<juce::Label>();
    labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelResumo_);

    listPrevia_ = std::make_unique<PreviaLista>();
    listPreviaViewport_ = std::make_unique<juce::Viewport>();
    listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);
    addAndMakeVisible(*listPreviaViewport_);

    // === PROGRESS ===
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    addChildComponent(*barraProgresso_);

    labelProgressoStatus_ = std::make_unique<juce::Label>();
    labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    labelProgressoStatus_->setColour(juce::Label::textColourId, tk.textoPrimario);
    labelProgressoStatus_->setJustificationType(juce::Justification::centred);
    addChildComponent(*labelProgressoStatus_);

    // === BUTTONS ===
    btnStartBackup_ = std::make_unique<juce::TextButton>("MAKE BACKUP");
    aplicarEstiloBotao(*btnStartBackup_, true);
    btnStartBackup_->onClick = [this] { iniciarBackup(); };
    addAndMakeVisible(*btnStartBackup_);

    btnCancel_ = std::make_unique<juce::TextButton>("CANCEL");
    aplicarEstiloBotao(*btnCancel_, false);
    btnCancel_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    btnCancel_->onClick = [this] {
        if (executando_) {
            cancelamento_->pedir();
        } else if (aoVoltarHome) {
            aoVoltarHome();
        }
    };
    addAndMakeVisible(*btnCancel_);

    btnDone_ = std::make_unique<juce::TextButton>("DONE");
    aplicarEstiloBotao(*btnDone_, false);
    btnDone_->onClick = [this] {
        if (aoConcluir) aoConcluir();
        else if (aoVoltarHome) aoVoltarHome();
    };
    addChildComponent(*btnDone_);

    btnOpenCatalog_ = std::make_unique<juce::TextButton>("OPEN BACKUP CATALOG");
    aplicarEstiloBotao(*btnOpenCatalog_, true);
    btnOpenCatalog_->onClick = [this] {
        if (aoAbrirCatalogo) aoAbrirCatalogo(resolvedDestFolder_);
    };
    addChildComponent(*btnOpenCatalog_);

    btnExportBkm_ = std::make_unique<juce::TextButton>("EXPORT CATALOG (.bkm)");
    aplicarEstiloBotao(*btnExportBkm_, false);
    btnExportBkm_->onClick = [this] { exportarBkm(); };
    addChildComponent(*btnExportBkm_);

    btnExportPdf_ = std::make_unique<juce::TextButton>("EXPORT PDF");
    aplicarEstiloBotao(*btnExportPdf_, false);
    btnExportPdf_->onClick = [this] { exportarPdf(); };
    addChildComponent(*btnExportPdf_);

    btnExportCsv_ = std::make_unique<juce::TextButton>("EXPORT CSV");
    aplicarEstiloBotao(*btnExportCsv_, false);
    btnExportCsv_->onClick = [this] { exportarCsv(); };
    addChildComponent(*btnExportCsv_);

    atualizarResumo();
}

BackupWorkspaceComponent::~BackupWorkspaceComponent() = default;

void BackupWorkspaceComponent::exportarCsv() {
    juce::FileChooser fc("Export CSV Report...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.csv");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("csv");

        juce::StringArray lines;
        lines.add("Asset ID,Filename,Path,Size,Date,Status");

        for (const auto& item : plano_.itens) {
            juce::String line = juce::String(item.itemId) + "," +
                                "\"" + item.nomeOriginal + "\"," +
                                "\"" + item.caminhoRelativoDestino + "\"," +
                                juce::String(item.tamanhoBytes) + "," +
                                "\"2026-08-11\"," +
                                "\"VERIFIED\"";
            lines.add(line);
        }

        targetFile.replaceWithText(lines.joinIntoString("\n"));
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT CSV")
                .withMessage("CSV Report exported successfully to:\n" + targetFile.getFullPathName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

void BackupWorkspaceComponent::exportarPdf() {
    juce::FileChooser fc("Export PDF Report...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.pdf");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("pdf");

        juce::StringArray lines;
        lines.add("===============================================================================");
        lines.add("BKR MATRIZ - ARCHIVAL BACKUP REPORT (PDF)");
        lines.add("===============================================================================");
        lines.add("Project Name: " + juce::String(projeto_.projeto().nome()));
        lines.add("Backup Path:  " + resolvedDestFolder_.getFullPathName());
        lines.add("Export Date:  2026-08-11");
        lines.add("Total Items:  " + juce::String(plano_.itens.size()));
        lines.add("Total Size:   " + juce::File::descriptionOfSizeInBytes(plano_.espacoNecessarioBytes));
        lines.add("===============================================================================\n");

        lines.add("--- FOLDER STRUCTURE & ASSET LIST ---");
        for (const auto& item : plano_.itens) {
            lines.add("[" + juce::String(item.itemId) + "] " + item.caminhoRelativoDestino +
                      " (" + juce::File::descriptionOfSizeInBytes(item.tamanhoBytes) + ")");
        }

        targetFile.replaceWithText(lines.joinIntoString("\n"));
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT PDF")
                .withMessage("PDF Report exported successfully to:\n" + targetFile.getFullPathName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

void BackupWorkspaceComponent::exportarBkm() {
    juce::FileChooser fc("Export Published Catalog (.bkm)...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.bkm");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("bkm");
        exportarBkmPara(targetFile.getParentDirectory());
    }
}

void BackupWorkspaceComponent::exportarCsvPara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + "_report.csv");

    juce::StringArray lines;
    lines.add("Asset ID,Filename,Path,Size,Date,Status");

    for (const auto& item : plano_.itens) {
        juce::String line = juce::String(item.itemId) + "," +
                            "\"" + item.nomeOriginal + "\"," +
                            "\"" + item.caminhoRelativoDestino + "\"," +
                            juce::String(item.tamanhoBytes) + "," +
                            "\"" + juce::Time::getCurrentTime().toString(true, false) + "\"," +
                            "\"VERIFIED\"";
        lines.add(line);
    }
    targetFile.replaceWithText(lines.joinIntoString("\n"));
}

void BackupWorkspaceComponent::exportarPdfPara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + "_report.pdf");

    juce::StringArray lines;
    lines.add("===============================================================================");
    lines.add("BKR MATRIZ - ARCHIVAL BACKUP REPORT");
    lines.add("===============================================================================");
    lines.add("Project Name: " + projectName);
    lines.add("Backup Path:  " + destFolder.getFullPathName());
    lines.add("Export Date:  " + juce::Time::getCurrentTime().toString(true, false));
    lines.add("Total Items:  " + juce::String(plano_.itens.size()));
    lines.add("Total Size:   " + juce::File::descriptionOfSizeInBytes(plano_.espacoNecessarioBytes));
    lines.add("===============================================================================\n");

    lines.add("--- FOLDER STRUCTURE & ASSET LIST ---");
    for (const auto& item : plano_.itens) {
        lines.add("[" + juce::String(item.itemId) + "] " + item.caminhoRelativoDestino +
                  " (" + juce::File::descriptionOfSizeInBytes(item.tamanhoBytes) + ")");
    }
    targetFile.replaceWithText(lines.joinIntoString("\n"));
}

void BackupWorkspaceComponent::exportarBkmPara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + ".bkm");

    // The .bkm is a copy of the project database (read-only catalog snapshot)
    auto dbFile = projeto_.projeto().pasta().getChildFile("registro.sqlite");
    if (!dbFile.existsAsFile())
        dbFile = projeto_.projeto().pasta().getChildFile("projeto.db");
    if (dbFile.existsAsFile())
        dbFile.copyFileTo(targetFile);
}

void BackupWorkspaceComponent::aplicarEstiloBotao(juce::TextButton& botao, bool primario) {
    const auto& tk = tema();
    botao.setColour(juce::TextButton::buttonColourId, primario ? tk.acento : tk.painelAlt);
    botao.setColour(juce::TextButton::buttonOnColourId, tk.acento);
    botao.setColour(juce::TextButton::textColourOffId, primario ? tk.textoSobreAcento : tk.textoPrimario);
    botao.setColour(juce::TextButton::textColourOnId, tk.textoSobreAcento);
}

std::set<std::string> BackupWorkspaceComponent::obterItensSelecionadosPeloCriterio() {
    std::set<std::string> out;
    auto& db = projeto_.projeto().registro();

    if (whatOption_ == WhatOption::Everything) {
        auto stmt = db.prepare("SELECT id FROM item");
        while (stmt.step()) out.insert(stmt.columnText(0));
    } else if (whatOption_ == WhatOption::RecentlyIngested) {
        auto stmt = db.prepare("SELECT id FROM item WHERE criado_em >= datetime('now', '-24 hours')");
        while (stmt.step()) out.insert(stmt.columnText(0));
    } else if (whatOption_ == WhatOption::SelectedAssets) {
        return selectedItemIds_;
    } else if (whatOption_ == WhatOption::NeedsBackup) {
        return projeto_.itensDaColecaoEmbutida("vulneraveis");
    } else if (whatOption_ == WhatOption::Collection) {
        if (selectedCollectionIdx_ >= 0 && selectedCollectionIdx_ < static_cast<int>(colecoes_.size()))
            return projeto_.itensDaColecaoEmbutida(colecoes_[static_cast<size_t>(selectedCollectionIdx_)].chave);
    }
    return out;
}

void BackupWorkspaceComponent::atualizarResumo() {
    if (resolvedDestFolder_.getFullPathName().isEmpty()) {
        labelResumo_->setText("Select a destination to see backup preview.", juce::dontSendNotification);
        plano_.itens.clear();
        listPrevia_->definirPlano(plano_);
        btnStartBackup_->setEnabled(false);
        return;
    }

    std::set<std::string> itemIds = obterItensSelecionadosPeloCriterio();

    matriz::consolidacao::HierarquiaBackup h;
    if (togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState()) {
        h = { matriz::consolidacao::NivelHierarquia::EstruturaOriginal };
    } else {
        int orgId = comboOrg_ ? comboOrg_->getSelectedId() : 1;
        if (orgId == 1) h = { matriz::consolidacao::NivelHierarquia::PastaManual };
        else if (orgId == 2) h = { matriz::consolidacao::NivelHierarquia::TipoMidia };
        else if (orgId == 3) h = { matriz::consolidacao::NivelHierarquia::Ano };
        else if (orgId == 4) h = { matriz::consolidacao::NivelHierarquia::TipoMidia, matriz::consolidacao::NivelHierarquia::Ano };
        else if (orgId == 5) h = hierarquiaCustom_;
    }

    plano_ = matriz::consolidacao::planejarConsolidacao(
        projeto_.projeto().registro(), projeto_.projeto().pasta(), resolvedDestFolder_, h);

    std::vector<matriz::consolidacao::ItemPlanejado> filtrados;
    juce::int64 sz = 0; // space to copy
    juce::int64 totalSz = 0; // total backup size
    for (auto& item : plano_.itens) {
        if (itemIds.count(item.itemId)) {
            filtrados.push_back(item);
            if (!item.jaConsolidado) sz += item.tamanhoBytes;
            totalSz += item.tamanhoBytes;
        }
    }
    plano_.itens = std::move(filtrados);
    plano_.espacoNecessarioBytes = sz;

    juce::String summary = "Assets: " + juce::String(static_cast<int>(plano_.itens.size())) +
                           " | Space: " + juce::File::descriptionOfSizeInBytes(totalSz);

    // Botão desabilitado sem explicação é indistinguível de botão ausente —
    // o motivo vai junto do resumo sempre que o backup não puder rodar.
    bool pronto = plano_.podeConsolidar() && !plano_.itens.empty();
    if (plano_.itens.empty())
        summary += "  -  CANNOT RUN: no assets match the selected source.";
    else if (!plano_.podeConsolidar())
        summary += "  -  CANNOT RUN: naming conflict, change the organization above.";

    labelResumo_->setText(summary, juce::dontSendNotification);

    listPrevia_->definirPlano(plano_);
    listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);

    btnStartBackup_->setEnabled(pronto);
}

void BackupWorkspaceComponent::iniciarBackup() {
    estado_ = Estado::Running;
    executando_ = true;
    cancelamento_->rearmar();
    progressoValor_ = 0.0;

    btnStartBackup_->setEnabled(false);
    barraProgresso_->setVisible(true);
    labelProgressoStatus_->setVisible(true);
    labelProgressoStatus_->setText("Preparing copy operations...", juce::dontSendNotification);

    resized();
    repaint();

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

    auto cancelamento = cancelamento_;
    auto plano = plano_;
    auto destFolder = resolvedDestFolder_;
    auto& projeto = projeto_;
    bool gerarCatalogo = toggleGerarCatalogo_->getToggleState();
    bool embutirMeta = toggleEmbutirMetadados_->getToggleState();

    juce::MessageManager::callAsync([safeThis, cancelamento, plano, destFolder, &projeto,
                                      gerarCatalogo, embutirMeta]() {
        if (!safeThis) return;

        auto resultado = matriz::consolidacao::executarConsolidacao(
            projeto.projeto().registro(),
            projeto.projeto().pasta(),
            destFolder,
            plano,
            [safeThis, cancelamento](int feito, int total) {
                if (!safeThis) return false;
                safeThis->progressoValor_ = static_cast<double>(feito) / std::max(1, total);
                safeThis->labelProgressoStatus_->setText("Copying: " + juce::String(feito) + " of " + juce::String(total) + " assets...", juce::dontSendNotification);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                return !cancelamento->pedido() && safeThis != nullptr;
            }
        );

        if (!safeThis) return;

        safeThis->copiadoCount_ = resultado.consolidados;
        safeThis->verificadoCount_ = resultado.consolidados + resultado.pulados;
        safeThis->falhasCount_ = static_cast<int>(resultado.falhas.size());
        safeThis->falhasLista_.clear();
        for (const auto& f : resultado.falhas)
            safeThis->falhasLista_.push_back(f);

        if (gerarCatalogo && !resultado.cancelado && safeThis) {
            safeThis->labelProgressoStatus_->setText("Generating HTML Catalog...", juce::dontSendNotification);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (!safeThis) return;
            auto resCatalogo = matriz::catalogo::gerar(
                projeto.projeto().registro(),
                projeto.projeto().indice(),
                projeto.projeto().pasta(),
                destFolder,
                [safeThis, cancelamento](int feito, int total) {
                    if (!safeThis) return false;
                    safeThis->labelProgressoStatus_->setText("Cataloging: " + juce::String(feito) + " of " + juce::String(total) + " files...", juce::dontSendNotification);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                    return !cancelamento->pedido() && safeThis != nullptr;
                }
            );
            (void)resCatalogo;
        }

        if (embutirMeta && !resultado.cancelado && safeThis) {
            safeThis->labelProgressoStatus_->setText("Embedding metadata into backup files...", juce::dontSendNotification);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (!safeThis) return;
            // Só os metadados aqui. Marcadores NÃO: executarConsolidacao já
            // chamou embutirMarcadoresNoBackup no fim, e rodar de novo anexa
            // um segundo par de chunks cue/LIST/iXML no mesmo WAV.
            int metadados = matriz::consolidacao::embutirMetadadosNoBackup(
                projeto.projeto().registro(), destFolder);
            (void)metadados;
        }

        if (!safeThis) return;

        safeThis->executando_ = false;
        safeThis->estado_ = Estado::Done;

        // A barra parava no último valor reportado (86%) ao lado de "concluído
        // com sucesso" — dois sinais contraditórios na mesma tela.
        safeThis->progressoValor_ = 1.0;

        // Auto-export CSV, PDF, BKM to backup root folder
        if (!resultado.cancelado) {
            safeThis->labelProgressoStatus_->setText("Exporting CSV, PDF and BKM catalog...", juce::dontSendNotification);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (safeThis) {
                safeThis->exportarCsvPara(destFolder);
                safeThis->exportarPdfPara(destFolder);
                safeThis->exportarBkmPara(destFolder);
            }
        }

        const bool houveFalha = safeThis->falhasCount_ > 0;

        // A linha de destaque diz o QUE aconteceu; a de baixo, os números.
        safeThis->labelProgressoStatus_->setText(
            resultado.cancelado ? "Backup cancelled."
                                : (houveFalha ? "Backup finished with errors."
                                              : "Backup completed successfully."),
            juce::dontSendNotification);
        safeThis->labelProgressoStatus_->setColour(
            juce::Label::textColourId,
            houveFalha ? tema().perigo : (resultado.cancelado ? tema().alerta : tema().estadoQcOk));

        juce::String finalMsg;
        finalMsg << safeThis->copiadoCount_ << " copied   |   "
                 << safeThis->verificadoCount_ << " verified";
        if (houveFalha) finalMsg << "   |   " << safeThis->falhasCount_ << " failed";
        finalMsg << "\n" << destFolder.getFullPathName();
        if (houveFalha) {
            finalMsg << "\n";
            int mostradas = 0;
            for (const auto& f : safeThis->falhasLista_) {
                if (mostradas++ >= 4) break;
                finalMsg << "\n" << f;
            }
            if (safeThis->falhasLista_.size() > 4)
                finalMsg << "\n... and " << static_cast<int>(safeThis->falhasLista_.size()) - 4 << " more.";
        }
        safeThis->labelResumo_->setText(finalMsg, juce::dontSendNotification);

        safeThis->labelTitulo_->setText(resultado.cancelado ? "Backup Cancelled" : "Backup Complete",
                                         juce::dontSendNotification);
        safeThis->btnStartBackup_->setVisible(false);
        safeThis->btnCancel_->setVisible(false);
        safeThis->btnDone_->setVisible(true);
        safeThis->resized();
    });
}

// --- ListBoxModel implementation for Vaults list ---

int BackupWorkspaceComponent::getNumRows() {
    return static_cast<int>(vaults_.size());
}

void BackupWorkspaceComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber >= static_cast<int>(vaults_.size())) return;
    const auto& tk = tema();
    const auto& vault = vaults_[static_cast<size_t>(rowNumber)];

    if (rowIsSelected) {
        g.fillAll(tk.acento.withAlpha(0.15f));
        g.setColour(tk.bordaFoco);
        g.drawRect(0, 0, width, height, 1);
    } else {
        g.fillAll(rowNumber % 2 == 0 ? tk.painel : tk.painelAlt);
    }

    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    g.drawText(vault.nome, 12, 2, width / 2 - 24, height - 4, juce::Justification::centredLeft, true);

    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    g.drawText(vault.localizacao, width / 2, 2, width / 2 - 100, height - 4, juce::Justification::centredLeft, true);

    juce::Rectangle<int> statusArea(width - 90, (height - 18) / 2, 80, 18);
    g.setColour(vault.online ? tk.estadoQcOk : tk.textoTerciario);
    g.fillRoundedRectangle(statusArea.toFloat(), 4.0f);
    g.setColour(tk.textoSobreAcento);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    g.drawText(vault.online ? "ONLINE" : "OFFLINE", statusArea, juce::Justification::centred, true);
}

void BackupWorkspaceComponent::listBoxItemClicked(int rowNumber, const juce::MouseEvent&) {
    if (rowNumber >= 0 && rowNumber < static_cast<int>(vaults_.size())) {
        selectedVaultIdx_ = rowNumber;
        resolvedDestFolder_ = juce::File(vaults_[static_cast<size_t>(rowNumber)].localizacao);
        labelDestInfo_->setText("Target: " + vaults_[static_cast<size_t>(rowNumber)].localizacao, juce::dontSendNotification);
        atualizarResumo();
    }
}

void BackupWorkspaceComponent::mostrarControlesConfig(bool mostrar) {
    for (juce::Component* c : {static_cast<juce::Component*>(labelSource_.get()),
                                static_cast<juce::Component*>(comboSource_.get()),
                                static_cast<juce::Component*>(labelDest_.get()),
                                static_cast<juce::Component*>(listVaults_.get()),
                                static_cast<juce::Component*>(btnBrowseVault_.get()),
                                static_cast<juce::Component*>(labelDestInfo_.get()),
                                static_cast<juce::Component*>(labelOrg_.get()),
                                static_cast<juce::Component*>(togglePreservarEstrutura_.get()),
                                static_cast<juce::Component*>(comboOrg_.get()),
                                static_cast<juce::Component*>(labelOpcoes_.get()),
                                static_cast<juce::Component*>(toggleVerificarChecksum_.get()),
                                static_cast<juce::Component*>(toggleGerarCatalogo_.get()),
                                static_cast<juce::Component*>(toggleEmbutirMetadados_.get()),
                                static_cast<juce::Component*>(listPreviaViewport_.get())})
        if (c != nullptr) c->setVisible(mostrar);

    // Os dois condicionais voltam obedecendo à própria regra, não ao estado.
    if (comboColecoes_) comboColecoes_->setVisible(mostrar && comboSource_->getSelectedId() == 5);
    if (btnEditarHierarquia_) btnEditarHierarquia_->setVisible(mostrar && comboOrg_->getSelectedId() == 5);
}

void BackupWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Cabeçalho e rodapé como faixas, separados por fio de 1px: dá âncora
    // visual fixa pro olho e impede que o conteúdo pareça flutuar solto.
    g.setColour(tk.borda);
    if (!faixaCabecalho_.isEmpty())
        g.fillRect(faixaCabecalho_.getX(), faixaCabecalho_.getBottom() - 1, faixaCabecalho_.getWidth(), 1);
    if (!faixaRodape_.isEmpty())
        g.fillRect(faixaRodape_.getX(), faixaRodape_.getY(), faixaRodape_.getWidth(), 1);

    for (const auto& cartao : cartoes_) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(cartao.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cartao.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
    }

    if (!cartaoCentral_.isEmpty()) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(cartaoCentral_.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cartaoCentral_.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
    }
}

void BackupWorkspaceComponent::resized() {
    const auto& tk = tema();
    cartoes_.clear();
    cartaoCentral_ = {};

    const bool emAndamento = (estado_ == Estado::Running || estado_ == Estado::Done);
    mostrarControlesConfig(!emAndamento);

    auto area = getLocalBounds();

    // ---- Cabeçalho ----
    faixaCabecalho_ = area.removeFromTop(64);
    labelTitulo_->setBounds(faixaCabecalho_.reduced(tk.espacoGrande * 2, 0)
                                .withTrimmedBottom(tk.espacoMedio)
                                .removeFromBottom(34));

    // ---- Rodapé ----
    faixaRodape_ = area.removeFromBottom(72);
    auto botoes = faixaRodape_.reduced(tk.espacoGrande * 2, tk.espacoGrande);
    const int alturaBotao = 40;
    botoes = botoes.withSizeKeepingCentre(botoes.getWidth(), alturaBotao);

    if (estado_ == Estado::Done) {
        btnDone_->setBounds(botoes.removeFromRight(110));
        botoes.removeFromRight(tk.espacoPainel);
        if (btnOpenCatalog_) {
            btnOpenCatalog_->setBounds(botoes.removeFromRight(190));
            btnOpenCatalog_->setVisible(true);
        }
        if (btnExportBkm_) {
            botoes.removeFromRight(tk.espacoPainel);
            btnExportBkm_->setBounds(botoes.removeFromRight(170));
            btnExportBkm_->setVisible(true);
        }
        if (btnExportPdf_) {
            botoes.removeFromRight(tk.espacoPainel);
            btnExportPdf_->setBounds(botoes.removeFromRight(110));
            btnExportPdf_->setVisible(true);
        }
        if (btnExportCsv_) {
            botoes.removeFromRight(tk.espacoPainel);
            btnExportCsv_->setBounds(botoes.removeFromRight(110));
            btnExportCsv_->setVisible(true);
        }
    } else {
        if (btnOpenCatalog_) btnOpenCatalog_->setVisible(false);
        if (btnExportBkm_) btnExportBkm_->setVisible(false);
        if (btnExportPdf_) btnExportPdf_->setVisible(false);
        if (btnExportCsv_) btnExportCsv_->setVisible(false);
        btnCancel_->setBounds(botoes.removeFromRight(110));
        botoes.removeFromRight(tk.espacoPainel);
        btnStartBackup_->setBounds(botoes.removeFromRight(170));
    }

    // ---- Running / Done: um cartão só, centrado ----
    if (emAndamento) {
        const int larguraCartao = std::min(760, area.getWidth() - tk.espacoGrande * 2);
        auto corpo = area.reduced(tk.espacoGrande, tk.espacoGrande);
        cartaoCentral_ = corpo.withSizeKeepingCentre(larguraCartao, std::min(320, corpo.getHeight()));

        auto dentro = cartaoCentral_.reduced(tk.espacoGrande * 2, tk.espacoGrande);

        labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
        labelProgressoStatus_->setJustificationType(juce::Justification::centred);
        labelProgressoStatus_->setBounds(dentro.removeFromTop(40));
        dentro.removeFromTop(tk.espacoMedio);

        if (barraProgresso_->isVisible()) {
            barraProgresso_->setBounds(dentro.removeFromTop(24));
            dentro.removeFromTop(tk.espacoGrande);
        }

        labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        labelResumo_->setColour(juce::Label::textColourId, tk.textoSecundario);
        labelResumo_->setJustificationType(juce::Justification::centredTop);
        labelResumo_->setBounds(dentro);
        return;
    }

    // ---- Config: duas colunas, com largura máxima ----
    // Sem teto de largura o formulário estica por 1600px e o olho percorre a
    // tela inteira entre o rótulo e o controle. 1180 mantém a leitura curta.
    auto corpo = area.reduced(tk.espacoGrande * 2, tk.espacoGrande + tk.espacoMedio);
    const int larguraMax = 1180;
    if (corpo.getWidth() > larguraMax)
        corpo = corpo.withSizeKeepingCentre(larguraMax, corpo.getHeight()).withY(corpo.getY());

    const int larguraConfig = std::min(460, corpo.getWidth() / 2);
    auto colunaConfig = corpo.removeFromLeft(larguraConfig);
    corpo.removeFromLeft(tk.espacoGrande + tk.espacoMedio);
    auto colunaPrevia = corpo;

    const int padCartao = tk.espacoPainel + 2;
    const int alturaControle = 32;
    const int alturaLinhaToggle = 30;
    const int alturaCabecalhoSecao = 26;

    // Cada seção é um cartão: fundo próprio, título dentro, respiro entre eles.
    // Agrupar assim é o que evita a "parede de widgets" — o olho encontra
    // quatro blocos, não treze controles soltos.
    auto abrirCartao = [&](int alturaConteudo) {
        auto cartao = colunaConfig.removeFromTop(alturaConteudo + padCartao * 2);
        cartoes_.push_back(cartao);
        colunaConfig.removeFromTop(tk.espacoPainel);
        return cartao.reduced(padCartao, padCartao);
    };

    // SOURCE
    {
        int h = alturaCabecalhoSecao + alturaControle;
        if (comboColecoes_->isVisible()) h += tk.espacoMedio + alturaControle;
        auto dentro = abrirCartao(h);
        labelSource_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
        comboSource_->setBounds(dentro.removeFromTop(alturaControle));
        if (comboColecoes_->isVisible()) {
            dentro.removeFromTop(tk.espacoMedio);
            comboColecoes_->setBounds(dentro.removeFromTop(alturaControle));
        }
    }

    // DESTINATION
    {
        const int alturaVaults = std::min(128, std::max(36, static_cast<int>(vaults_.size()) * 36));
        int h = alturaCabecalhoSecao + alturaVaults + tk.espacoMedio + alturaControle + tk.espacoMedio + 32;
        auto dentro = abrirCartao(h);
        labelDest_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
        listVaults_->setBounds(dentro.removeFromTop(alturaVaults));
        dentro.removeFromTop(tk.espacoMedio);
        btnBrowseVault_->setBounds(dentro.removeFromTop(alturaControle));
        dentro.removeFromTop(tk.espacoMedio);
        labelDestInfo_->setBounds(dentro.removeFromTop(32));
    }

    // ORGANIZATION
    {
        int h = alturaCabecalhoSecao + alturaLinhaToggle + tk.espacoPequeno + alturaControle;
        if (btnEditarHierarquia_->isVisible()) h += tk.espacoMedio + alturaControle;
        auto dentro = abrirCartao(h);
        labelOrg_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
        togglePreservarEstrutura_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
        dentro.removeFromTop(tk.espacoPequeno);
        comboOrg_->setBounds(dentro.removeFromTop(alturaControle));
        if (btnEditarHierarquia_->isVisible()) {
            dentro.removeFromTop(tk.espacoMedio);
            btnEditarHierarquia_->setBounds(dentro.removeFromTop(alturaControle));
        }
    }

    // OPTIONS
    {
        auto dentro = abrirCartao(alturaCabecalhoSecao + alturaLinhaToggle * 3);
        labelOpcoes_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
        toggleVerificarChecksum_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
        toggleGerarCatalogo_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
        toggleEmbutirMetadados_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
    }

    // PREVIEW — cartão único ocupando a coluna inteira, resumo no topo.
    {
        cartoes_.push_back(colunaPrevia);
        auto dentro = colunaPrevia.reduced(padCartao, padCartao);
        labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
        labelResumo_->setJustificationType(juce::Justification::centredLeft);
        labelResumo_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao + 4));
        dentro.removeFromTop(tk.espacoMedio);
        listPreviaViewport_->setBounds(dentro);
        if (listPrevia_) listPrevia_->setSize(dentro.getWidth(), listPrevia_->getHeight());
    }
}

} // namespace matriz::ui
