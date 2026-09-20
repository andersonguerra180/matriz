#include "PublishHtmlDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

class BadgeHComponent : public juce::Component {
public:
    std::function<void()> onClick;

    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(juce::Colour(0xff22c55e)); // Emerald green
        g.drawRoundedRectangle(b, 3.0f, 1.5f);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("H", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (onClick) onClick();
    }
};

PublishHtmlDialog::PublishHtmlDialog(ProjetoAberto& projeto)
    : projeto_(projeto) {
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    // Header
    lblTitulo_ = std::make_unique<juce::Label>("lblTitulo", isPt ? juce::String::fromUTF8("PUBLICAR HTML") : "PUBLISH HTML");
    lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(*lblTitulo_);

    lblSubtitulo_ = std::make_unique<juce::Label>(
        "lblSubtitulo",
        isPt ? juce::String::fromUTF8("Gere um portal web estático, responsivo e autocontido com player de áudio e visualizador de mídia.")
             : "Generate a self-contained, responsive static HTML site with audio player and media gallery.");
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblSubtitulo_);

    // 1. Escopo de Publicação
    grpEscopo_ = std::make_unique<juce::GroupComponent>("grpEscopo", isPt ? juce::String::fromUTF8("1. Escopo de Arquivos para Publicação") : "1. Publication Scope");
    grpEscopo_->setColour(juce::GroupComponent::outlineColourId, tk.borda);
    grpEscopo_->setColour(juce::GroupComponent::textColourId, tk.acento);
    addAndMakeVisible(*grpEscopo_);

    lblDicaPublicacao_ = std::make_unique<juce::Label>(
        "lblDica",
        isPt ? juce::String::fromUTF8("Nota: Apenas os arquivos com status ativo de publicação serão incluídos na opção de itens marcados.")
             : "Note: Only assets with active publication status are included in the marked assets option.");
    lblDicaPublicacao_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblDicaPublicacao_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblDicaPublicacao_);

    rbApenasMarcadosH_ = std::make_unique<juce::ToggleButton>(
        isPt ? juce::String::fromUTF8("Apenas arquivos marcados com")
             : "Only assets marked with");
    rbApenasMarcadosH_->setRadioGroupId(1001);
    rbApenasMarcadosH_->setToggleState(true, juce::dontSendNotification);
    rbApenasMarcadosH_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    addAndMakeVisible(*rbApenasMarcadosH_);

    auto badge = std::make_unique<BadgeHComponent>();
    badge->onClick = [this] { rbApenasMarcadosH_->setToggleState(true, juce::sendNotification); };
    badgeH_ = std::move(badge);
    addAndMakeVisible(*badgeH_);

    lblSufixoH_ = std::make_unique<juce::Label>(
        "lblSufixoH",
        isPt ? juce::String::fromUTF8("(borda verde no grid)")
             : "(green border in grid)");
    lblSufixoH_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblSufixoH_->setColour(juce::Label::textColourId, tk.textoSecundario);
    lblSufixoH_->addMouseListener(this, false);
    addAndMakeVisible(*lblSufixoH_);

    rbTodosAssets_ = std::make_unique<juce::ToggleButton>(
        isPt ? juce::String::fromUTF8("Todos os arquivos do projeto")
             : "All assets in project");
    rbTodosAssets_->setRadioGroupId(1001);
    rbTodosAssets_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    addAndMakeVisible(*rbTodosAssets_);

    // 2. Estrutura e Organização
    grpEstrutura_ = std::make_unique<juce::GroupComponent>("grpEstrutura", isPt ? juce::String::fromUTF8("2. Estrutura e Organização do HTML") : "2. HTML Structure & Organization");
    grpEstrutura_->setColour(juce::GroupComponent::outlineColourId, tk.borda);
    grpEstrutura_->setColour(juce::GroupComponent::textColourId, tk.acento);
    addAndMakeVisible(*grpEstrutura_);

    lblEstruturaInfo_ = std::make_unique<juce::Label>(
        "lblEstruturaInfo",
        isPt ? juce::String::fromUTF8("Escolha como os arquivos serão agrupados nas coleções do portal:")
             : "Choose how assets will be grouped into catalog collections:");
    lblEstruturaInfo_->setFont(juce::Font(juce::FontOptions(11.5f)));
    lblEstruturaInfo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblEstruturaInfo_);

    comboEstrutura_ = std::make_unique<juce::ComboBox>("comboEstrutura");
    comboEstrutura_->addItem(isPt ? juce::String::fromUTF8("Estrutura de Pastas Originais / Coleções") : "Original Folder Structure / Collections", 1);
    comboEstrutura_->addItem(isPt ? juce::String::fromUTF8("Por Tipo de Mídia (Áudio, Vídeo, Imagens, Documentos...)") : "By Media Type (Audio, Video, Images, Documents...)", 2);
    comboEstrutura_->addItem(isPt ? juce::String::fromUTF8("Por Ano de Lançamento / Criação") : "By Release / Creation Year", 3);
    comboEstrutura_->addItem(isPt ? juce::String::fromUTF8("Por Tipo de Conteúdo (Content Type)") : "By Content Type", 4);
    comboEstrutura_->setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(*comboEstrutura_);

    // 3. Branding e Identidade Visual
    grpBranding_ = std::make_unique<juce::GroupComponent>("grpBranding", isPt ? juce::String::fromUTF8("3. Identidade Visual (Opcional)") : "3. Visual Branding (Optional)");
    grpBranding_->setColour(juce::GroupComponent::outlineColourId, tk.borda);
    grpBranding_->setColour(juce::GroupComponent::textColourId, tk.acento);
    addAndMakeVisible(*grpBranding_);

    btnEscolherFundo_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Imagem de Fundo...") : "Background Image...");
    btnEscolherFundo_->onClick = [this] { escolherImagemFundo(); };
    addAndMakeVisible(*btnEscolherFundo_);

    lblCaminhoFundo_ = std::make_unique<juce::Label>("lblCaminhoFundo", isPt ? juce::String::fromUTF8("Nenhuma imagem selecionada") : "No image selected");
    lblCaminhoFundo_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblCaminhoFundo_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblCaminhoFundo_);

    btnLimparFundo_ = std::make_unique<juce::TextButton>("X");
    btnLimparFundo_->onClick = [this] {
        arquivoFundo_ = juce::File();
        lblCaminhoFundo_->setText(matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Nenhuma imagem selecionada") : "No image selected", juce::dontSendNotification);
        btnLimparFundo_->setVisible(false);
    };
    btnLimparFundo_->setVisible(false);
    addAndMakeVisible(*btnLimparFundo_);

    btnEscolherLogo_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Logo da Coleção...") : "Collection Logo...");
    btnEscolherLogo_->onClick = [this] { escolherImagemLogo(); };
    addAndMakeVisible(*btnEscolherLogo_);

    lblCaminhoLogo_ = std::make_unique<juce::Label>("lblCaminhoLogo", isPt ? juce::String::fromUTF8("Nenhuma logo selecionada") : "No logo selected");
    lblCaminhoLogo_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblCaminhoLogo_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblCaminhoLogo_);

    btnLimparLogo_ = std::make_unique<juce::TextButton>("X");
    btnLimparLogo_->onClick = [this] {
        arquivoLogo_ = juce::File();
        lblCaminhoLogo_->setText(matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Nenhuma logo selecionada") : "No logo selected", juce::dontSendNotification);
        btnLimparLogo_->setVisible(false);
    };
    btnLimparLogo_->setVisible(false);
    addAndMakeVisible(*btnLimparLogo_);

    // 4. Texto Customizado
    grpTextoCustom_ = std::make_unique<juce::GroupComponent>("grpTextoCustom", isPt ? juce::String::fromUTF8("4. Texto Customizado para Apresentação / Header (Opcional)") : "4. Custom Header / Presentation Text (Optional)");
    grpTextoCustom_->setColour(juce::GroupComponent::outlineColourId, tk.borda);
    grpTextoCustom_->setColour(juce::GroupComponent::textColourId, tk.acento);
    addAndMakeVisible(*grpTextoCustom_);

    editorTextoCustom_ = std::make_unique<juce::TextEditor>("editorTextoCustom");
    editorTextoCustom_->setMultiLine(true, true);
    editorTextoCustom_->setReturnKeyStartsNewLine(true);
    editorTextoCustom_->setTextToShowWhenEmpty(
        isPt ? juce::String::fromUTF8("Cole aqui notas sobre o catálogo, introdução, créditos ou informações que aparecerão no cabeçalho...")
             : "Paste optional notes, intro, credits, or reader text to display in the website header...",
        tk.textoTerciario);
    editorTextoCustom_->setColour(juce::TextEditor::backgroundColourId, tk.painel);
    editorTextoCustom_->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    editorTextoCustom_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    addAndMakeVisible(*editorTextoCustom_);

    // Botões Inferiores
    btnCancelar_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Cancelar") : "Cancel");
    btnCancelar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnCancelar_->onClick = [this] { closeDialog(); };
    addAndMakeVisible(*btnCancelar_);

    btnPublicar_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("SELECIONAR DESTINO E PUBLICAR...") : "SELECT DESTINATION & PUBLISH...");
    btnPublicar_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnPublicar_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnPublicar_->onClick = [this] { iniciarPublicacao(); };
    addAndMakeVisible(*btnPublicar_);

    setSize(780, 560);
}

PublishHtmlDialog::~PublishHtmlDialog() = default;

void PublishHtmlDialog::closeDialog() {
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
    }
    if (aoFechar) aoFechar();
}

bool PublishHtmlDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        closeDialog();
        return true;
    }
    return false;
}

void PublishHtmlDialog::escolherImagemFundo() {
    auto chooser = std::make_shared<juce::FileChooser>(
        matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Selecionar Imagem de Fundo") : "Select Background Image",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.jpg;*.jpeg;*.png;*.webp");
    juce::Component::SafePointer<PublishHtmlDialog> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis, chooser](const juce::FileChooser& fc) {
                              if (!safeThis) return;
                              juce::File f = fc.getResult();
                              if (f.existsAsFile()) {
                                  safeThis->arquivoFundo_ = f;
                                  safeThis->lblCaminhoFundo_->setText(f.getFileName(), juce::dontSendNotification);
                                  safeThis->btnLimparFundo_->setVisible(true);
                              }
                          });
}

void PublishHtmlDialog::escolherImagemLogo() {
    auto chooser = std::make_shared<juce::FileChooser>(
        matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Selecionar Imagem do Logo") : "Select Logo Image",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.jpg;*.jpeg;*.png;*.webp;*.svg");
    juce::Component::SafePointer<PublishHtmlDialog> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis, chooser](const juce::FileChooser& fc) {
                              if (!safeThis) return;
                              juce::File f = fc.getResult();
                              if (f.existsAsFile()) {
                                  safeThis->arquivoLogo_ = f;
                                  safeThis->lblCaminhoLogo_->setText(f.getFileName(), juce::dontSendNotification);
                                  safeThis->btnLimparLogo_->setVisible(true);
                              }
                          });
}

void PublishHtmlDialog::iniciarPublicacao() {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    bool apenasMarcadosH = rbApenasMarcadosH_->getToggleState();

    std::vector<std::string> itemIdsFiltro;
    if (apenasMarcadosH) {
        itemIdsFiltro = projeto_.idsMarcados(ProjetoAberto::TipoMarcacao::Html);
        if (itemIdsFiltro.empty()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                isPt ? juce::String::fromUTF8("Nenhum Arquivo Marcado") : "No Marked Assets",
                isPt ? juce::String::fromUTF8("Nenhum arquivo está marcado para publicação com o atalho [H].\n\nMarque os arquivos desejados no grid pressionando [H] ou selecione a opção 'Todos os arquivos do projeto'.")
                     : "No assets are marked for publication with [H].\n\nMark assets in the grid using [H] or select 'All assets in project'.");
            return;
        }
    }

    matriz::catalogo::ParamsExportSite params;
    params.itemIdsFiltro = itemIdsFiltro;
    params.backgroundImageFile = arquivoFundo_;
    params.logoFile = arquivoLogo_;
    params.textoCabecalhoCustom = editorTextoCustom_->getText().trim();

    int estruturaId = comboEstrutura_->getSelectedId();
    if (estruturaId == 2) params.estrutura = matriz::catalogo::ParamsExportSite::EstruturaHtml::PorTipoMidia;
    else if (estruturaId == 3) params.estrutura = matriz::catalogo::ParamsExportSite::EstruturaHtml::PorAno;
    else if (estruturaId == 4) params.estrutura = matriz::catalogo::ParamsExportSite::EstruturaHtml::PorConteudo;
    else params.estrutura = matriz::catalogo::ParamsExportSite::EstruturaHtml::PastasOriginais;

    auto chooser = std::make_shared<juce::FileChooser>(
        isPt ? juce::String::fromUTF8("Selecione a Pasta de Destino da Publicação") : "Select Output Folder for HTML Export",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory));

    juce::Component::SafePointer<PublishHtmlDialog> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis, chooser, params, isPt](const juce::FileChooser& fc) {
                              if (!safeThis) return;
                              juce::File folder = fc.getResult();
                              if (folder == juce::File() || !folder.exists()) return;

                              struct ProgressThread : public juce::ThreadWithProgressWindow {
                                  ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                                      : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
                                  void run() override { if (work_) work_(this); }
                                  std::function<void(ProgressThread*)> work_;
                              };

                              matriz::catalogo::ResultadoExportSite resultado;
                              ProgressThread thread(isPt ? "PUBLICAR HTML" : "PUBLISH HTML", [safeThis, folder, params, &resultado](ProgressThread* t) {
                                  resultado = matriz::catalogo::exportarHtmlBrowser(
                                      safeThis->projeto_, folder, params,
                                      [t](int feito, int total) -> bool {
                                          if (t->threadShouldExit()) return false;
                                          if (total > 0) {
                                              t->setStatusMessage("Generating static web catalog: " + juce::String(feito) + " / " + juce::String(total));
                                              t->setProgress(static_cast<double>(feito) / total);
                                          }
                                          return true;
                                      });
                              });
                              thread.runThread();

                              if (!resultado.cancelled && resultado.errors.empty()) {
                                  juce::File indexFile = folder.getChildFile("index.html");
                                  juce::AlertWindow::showOkCancelBox(
                                      juce::MessageBoxIconType::InfoIcon,
                                      isPt ? "PUBLICAR HTML" : "PUBLISH HTML",
                                      matriz::i18n::t("hub.dialog_publicar_sucesso").replace("{p}", folder.getFullPathName()) +
                                      "\n\nTotal collections: " + juce::String(resultado.totalCollections) +
                                      "\nTotal assets: " + juce::String(resultado.totalAssets),
                                      "OPEN BROWSER",
                                      "OK",
                                      nullptr,
                                      juce::ModalCallbackFunction::create([indexFile](int res) {
                                          if (res == 1 && indexFile.existsAsFile()) {
                                              juce::URL(indexFile).launchInDefaultBrowser();
                                          }
                                      }));
                                  safeThis->closeDialog();
                              } else if (!resultado.cancelled && !resultado.errors.empty()) {
                                  juce::String errStr;
                                  for (const auto& e : resultado.errors) errStr += e + "\n";
                                  juce::AlertWindow::showAsync(
                                      juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::WarningIcon)
                                          .withTitle(isPt ? "PUBLICAR HTML FALHOU" : "PUBLISH HTML FAILED")
                                          .withMessage((isPt ? "A geração do HTML falhou:\n\n" : "HTML generation failed:\n\n") + errStr)
                                          .withButton("OK"),
                                      static_cast<juce::ModalComponentManager::Callback*>(nullptr));
                              }
                          });
}

void PublishHtmlDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);
    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);
}

void PublishHtmlDialog::resized() {
    if (!btnPublicar_) return;
    auto area = getLocalBounds().reduced(24, 18);
    const auto& tk = tema();

    // Header
    lblTitulo_->setBounds(area.removeFromTop(26));
    lblSubtitulo_->setBounds(area.removeFromTop(18));
    area.removeFromTop(10);

    // Botões Inferiores
    auto btmArea = area.removeFromBottom(34);
    btnCancelar_->setBounds(btmArea.removeFromLeft(110));
    btnPublicar_->setBounds(btmArea.removeFromRight(270));
    area.removeFromBottom(12);

    // 1. Escopo (Height 86px)
    auto escopoArea = area.removeFromTop(86);
    grpEscopo_->setBounds(escopoArea);
    escopoArea.reduce(12, 14);
    lblDicaPublicacao_->setBounds(escopoArea.removeFromTop(16));
    escopoArea.removeFromTop(2);

    auto rowH = escopoArea.removeFromTop(24);
    auto fontCorpo = juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo));
    int textW = static_cast<int>(juce::GlyphArrangement::getStringWidth(fontCorpo, rbApenasMarcadosH_->getButtonText())) + 26;
    rbApenasMarcadosH_->setBounds(rowH.removeFromLeft(textW));
    rowH.removeFromLeft(6);
    badgeH_->setBounds(rowH.removeFromLeft(20).withSizeKeepingCentre(20, 18));
    rowH.removeFromLeft(6);
    lblSufixoH_->setBounds(rowH.removeFromLeft(170));
    rowH.removeFromLeft(14);
    rbTodosAssets_->setBounds(rowH);

    area.removeFromTop(10);

    // 2. Estrutura (Height 76px)
    auto structArea = area.removeFromTop(76);
    grpEstrutura_->setBounds(structArea);
    structArea.reduce(12, 14);
    lblEstruturaInfo_->setBounds(structArea.removeFromTop(16));
    structArea.removeFromTop(4);
    comboEstrutura_->setBounds(structArea.removeFromTop(26).reduced(2, 0));

    area.removeFromTop(10);

    // 3. Branding (Height 90px)
    auto brandArea = area.removeFromTop(90);
    grpBranding_->setBounds(brandArea);

    auto innerBrand = brandArea.reduced(14, 0);
    innerBrand.removeFromTop(23); // Margem superior adequada abaixo do título do grupo

    auto row1 = innerBrand.removeFromTop(26);
    btnEscolherFundo_->setBounds(row1.removeFromLeft(160));
    row1.removeFromLeft(8);
    btnLimparFundo_->setBounds(row1.removeFromRight(26));
    row1.removeFromRight(6);
    lblCaminhoFundo_->setBounds(row1);

    innerBrand.removeFromTop(6);

    auto row2 = innerBrand.removeFromTop(26);
    btnEscolherLogo_->setBounds(row2.removeFromLeft(160));
    row2.removeFromLeft(8);
    btnLimparLogo_->setBounds(row2.removeFromRight(26));
    row2.removeFromRight(6);
    lblCaminhoLogo_->setBounds(row2);

    area.removeFromTop(10);

    // 4. Texto Customizado (fills remaining space)
    grpTextoCustom_->setBounds(area);
    area.reduce(12, 16);
    editorTextoCustom_->setBounds(area);
}

void PublishHtmlDialog::lookAndFeelChanged() {
    repaint();
}

void PublishHtmlDialog::exibirModal(ProjetoAberto& projeto) {
    auto dlg = std::make_unique<PublishHtmlDialog>(projeto);

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = isPt ? juce::String::fromUTF8("PUBLICAR HTML") : "PUBLISH HTML";
    opt.content.set(dlg.release(), true);
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

} // namespace matriz::ui
