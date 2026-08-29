#include "CatalogHubComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

CatalogHubComponent::CatalogHubComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    const auto& tk = tema();

    lblTitulo_ = std::make_unique<juce::Label>("", "COLLECTIONS");
    lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    lblSubtitulo_ = std::make_unique<juce::Label>("", "Collections belonging to this catalog. Open any collection to manage its assets, or import new collections.");
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblSubtitulo_);

    lblResumo_ = std::make_unique<juce::Label>();
    lblResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblResumo_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblResumo_);

    btnImportar_ = std::make_unique<juce::TextButton>("+ IMPORT COLLECTION...");
    btnImportar_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnImportar_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnImportar_->onClick = [this] { importarColecaoDialogo(); };
    addAndMakeVisible(*btnImportar_);

    btnAbrir_ = std::make_unique<juce::TextButton>("OPEN COLLECTION");
    btnAbrir_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnAbrir_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnAbrir_->onClick = [this] { abrirSelecionada(); };
    addAndMakeVisible(*btnAbrir_);

    btnRelocar_ = std::make_unique<juce::TextButton>("LOCATE COLLECTION...");
    btnRelocar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnRelocar_->setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff97316));
    btnRelocar_->onClick = [this] { relocarSelecionada(); };
    addAndMakeVisible(*btnRelocar_);

    btnDesvincular_ = std::make_unique<juce::TextButton>("UNLINK");
    btnDesvincular_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnDesvincular_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    btnDesvincular_->onClick = [this] { desvincularSelecionada(); };
    addAndMakeVisible(*btnDesvincular_);

    btnBackup_ = std::make_unique<juce::TextButton>("CONSOLIDATED BACKUP");
    btnBackup_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnBackup_->setColour(juce::TextButton::textColourOffId, tk.acento);
    btnBackup_->onClick = [this] { if (aoBackupConsolidado) aoBackupConsolidado(); };
    addAndMakeVisible(*btnBackup_);

    tabela_ = std::make_unique<juce::TableListBox>("CatalogTable", this);
    tabela_->setColour(juce::TableListBox::backgroundColourId, tk.painel);
    tabela_->setColour(juce::ListBox::outlineColourId, tk.borda);
    tabela_->setRowHeight(36);
    tabela_->setMultipleSelectionEnabled(false);

    auto& hdr = tabela_->getHeader();
    hdr.addColumn("STATUS", kColStatus, 80, 60, 100, juce::TableHeaderComponent::notSortable);
    hdr.addColumn("COLLECTION NAME", kColNome, 220, 150, 400, juce::TableHeaderComponent::notSortable);
    hdr.addColumn("GROUP", kColGrupo, 140, 100, 250, juce::TableHeaderComponent::notSortable);
    hdr.addColumn("TOTAL ASSETS", kColAssets, 110, 90, 150, juce::TableHeaderComponent::notSortable);
    hdr.addColumn("TOTAL SIZE", kColTamanho, 110, 90, 150, juce::TableHeaderComponent::notSortable);
    hdr.addColumn("LOCATION / PATH", kColCaminho, 350, 200, 800, juce::TableHeaderComponent::notSortable);
    addAndMakeVisible(*tabela_);

    recarregar();
}

CatalogHubComponent::~CatalogHubComponent() = default;

void CatalogHubComponent::recarregar() {
    colecoes_ = projeto_.listarColecoesLinkadas();
    if (tabela_) tabela_->updateContent();

    uint64_t totalAssets = 0;
    juce::int64 totalBytes = 0;
    for (const auto& c : colecoes_) {
        if (c.valido) {
            totalAssets += c.totalAssets;
            totalBytes += c.totalBytes;
        }
    }

    juce::String resumo = juce::String(static_cast<int>(colecoes_.size())) + " linked collection(s)  |  " +
                          juce::String(totalAssets) + " total assets  |  " +
                          juce::File::descriptionOfSizeInBytes(totalBytes);
    if (lblResumo_) lblResumo_->setText(resumo, juce::dontSendNotification);

    bool temSel = tabela_ && tabela_->getSelectedRow() >= 0;
    btnAbrir_->setEnabled(temSel);
    btnRelocar_->setEnabled(temSel);
    btnDesvincular_->setEnabled(temSel);
    btnBackup_->setEnabled(!colecoes_.empty());
}

void CatalogHubComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);
}

void CatalogHubComponent::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds().reduced(24, 20);

    auto topo = area.removeFromTop(60);
    lblTitulo_->setBounds(topo.removeFromTop(28));
    lblSubtitulo_->setBounds(topo.removeFromTop(22));

    area.removeFromTop(12);

    auto barraAcoes = area.removeFromTop(36);
    btnImportar_->setBounds(barraAcoes.removeFromLeft(180));
    barraAcoes.removeFromLeft(12);
    btnAbrir_->setBounds(barraAcoes.removeFromLeft(150));
    barraAcoes.removeFromLeft(12);
    btnRelocar_->setBounds(barraAcoes.removeFromLeft(170));
    barraAcoes.removeFromLeft(12);
    btnDesvincular_->setBounds(barraAcoes.removeFromLeft(90));
    barraAcoes.removeFromLeft(16);
    btnBackup_->setBounds(barraAcoes.removeFromLeft(180));

    lblResumo_->setBounds(barraAcoes);

    area.removeFromTop(16);
    tabela_->setBounds(area);
}

int CatalogHubComponent::getNumRows() {
    return static_cast<int>(colecoes_.size());
}

void CatalogHubComponent::paintRowBackground(juce::Graphics& g, int rowNumber, int, int, bool rowIsSelected) {
    const auto& tk = tema();
    if (rowIsSelected) {
        g.fillAll(tk.acento.withAlpha(0.15f));
    } else if (rowNumber % 2 == 1) {
        g.fillAll(juce::Colour(0x06ffffff));
    }
}

void CatalogHubComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(colecoes_.size())) return;
    const auto& c = colecoes_[static_cast<size_t>(rowNumber)];
    const auto& tk = tema();

    if (columnId == kColStatus) {
        auto badge = juce::Rectangle<int>(10, (height - 18) / 2, 60, 18);
        g.setColour(c.valido ? tk.estadoQcOk : juce::Colour(0xfff97316));
        g.fillRoundedRectangle(badge.toFloat(), 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(c.valido ? "ONLINE" : "OFFLINE", badge, juce::Justification::centred, true);
    } else if (columnId == kColNome) {
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(c.nome, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColGrupo) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(c.grupo.isEmpty() ? "-" : c.grupo, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColAssets) {
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(c.valido ? juce::String(c.totalAssets) : "-", 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColTamanho) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(c.valido ? juce::File::descriptionOfSizeInBytes(c.totalBytes) : "-", 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColCaminho) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(c.caminhoProjeto, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    }
}

void CatalogHubComponent::cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&) {
    if (rowNumber >= 0 && rowNumber < static_cast<int>(colecoes_.size())) {
        const auto& c = colecoes_[static_cast<size_t>(rowNumber)];
        if (c.valido && aoAbrirColecao) {
            aoAbrirColecao(juce::File(c.caminhoProjeto));
        } else if (!c.valido) {
            relocarSelecionada();
        }
    }
}

void CatalogHubComponent::importarColecaoDialogo() {
    auto chooser = std::make_shared<juce::FileChooser>("Select Collection Project Folder to Import into Catalog");
    juce::Component::SafePointer<CatalogHubComponent> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis, chooser](const juce::FileChooser& fc) {
                              if (!safeThis) return;
                              juce::File folder = fc.getResult();
                              if (folder == juce::File() || !folder.exists()) return;

                              juce::File dbFile = folder.getChildFile("registro.sqlite");
                              if (!dbFile.existsAsFile()) {
                                  juce::AlertWindow::showAsync(
                                      juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::WarningIcon)
                                          .withTitle("Invalid Collection Folder")
                                          .withMessage("The selected folder is not a valid BKR project directory (missing registro.sqlite).")
                                          .withButton("OK"),
                                      nullptr);
                                  return;
                              }

                              safeThis->projeto_.linkarColecao(folder);
                              safeThis->recarregar();
                          });
}

void CatalogHubComponent::abrirSelecionada() {
    if (!tabela_) return;
    int selRow = tabela_->getSelectedRow();
    if (selRow >= 0 && selRow < static_cast<int>(colecoes_.size())) {
        const auto& c = colecoes_[static_cast<size_t>(selRow)];
        if (c.valido && aoAbrirColecao) {
            aoAbrirColecao(juce::File(c.caminhoProjeto));
        } else if (!c.valido) {
            relocarSelecionada();
        }
    }
}

void CatalogHubComponent::relocarSelecionada() {
    if (!tabela_) return;
    int selRow = tabela_->getSelectedRow();
    if (selRow < 0 || selRow >= static_cast<int>(colecoes_.size())) return;

    const auto& c = colecoes_[static_cast<size_t>(selRow)];
    std::string linkId = c.id;

    auto chooser = std::make_shared<juce::FileChooser>(
        "Locate Collection Folder for: " + c.nome,
        juce::File::getSpecialLocation(juce::File::userHomeDirectory));

    juce::Component::SafePointer<CatalogHubComponent> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis, chooser, linkId](const juce::FileChooser& fc) {
                              if (!safeThis) return;
                              juce::File folder = fc.getResult();
                              if (folder == juce::File() || !folder.exists()) return;

                              if (!safeThis->projeto_.relocarColecaoLink(linkId, folder)) {
                                  juce::AlertWindow::showAsync(
                                      juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::WarningIcon)
                                          .withTitle("Invalid Collection Folder")
                                          .withMessage("The selected folder does not contain a valid collection database (registro.sqlite).")
                                          .withButton("OK"),
                                      nullptr);
                                  return;
                              }

                              safeThis->recarregar();
                          });
}

void CatalogHubComponent::desvincularSelecionada() {
    if (!tabela_) return;
    int selRow = tabela_->getSelectedRow();
    if (selRow >= 0 && selRow < static_cast<int>(colecoes_.size())) {
        const auto& c = colecoes_[static_cast<size_t>(selRow)];
        projeto_.desvincularColecao(c.id);
        recarregar();
    }
}

} // namespace matriz::ui
