#include "RescanBackupSourcesDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

RescanBackupSourcesDialog::RescanBackupSourcesDialog(
    std::vector<matriz::vault::RescanPair> pares,
    std::function<void(std::vector<matriz::vault::RescanPair>)> onConfirmar)
    : onConfirmar_(std::move(onConfirmar)) {

    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    lblTitulo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("Rescanear Fontes de Backup") : "Rescan Backup Sources");
    lblTitulo_->setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    containerLinhas_ = std::make_unique<juce::Component>();
    viewport_ = std::make_unique<juce::Viewport>();
    viewport_->setViewedComponent(containerLinhas_.get(), false);
    viewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*viewport_);

    for (auto& par : pares) {
        ItemLinha item;
        item.par = par;

        juce::String labelTexto = par.sourceDriveName + juce::String::fromUTF8("  →  ") + par.backupDriveName;
        item.toggle = std::make_unique<juce::ToggleButton>(labelTexto);
        item.toggle->setToggleState(false, juce::dontSendNotification); // Default: unchecked
        item.toggle->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        item.toggle->setColour(juce::ToggleButton::tickColourId, tk.acento);
        item.toggle->onClick = [this] { atualizarEstadoBotoes(); };

        containerLinhas_->addAndMakeVisible(*item.toggle);
        linhas_.push_back(std::move(item));
    }

    btnRescanSelecionados_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Rescanear selecionados") : "Rescan Selected");
    btnRescanSelecionados_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnRescanSelecionados_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnRescanSelecionados_->onClick = [this] {
        std::vector<matriz::vault::RescanPair> selecionados;
        for (auto& linha : linhas_) {
            if (linha.toggle && linha.toggle->getToggleState()) {
                selecionados.push_back(linha.par);
            }
        }
        if (!selecionados.empty() && onConfirmar_) {
            auto cb = onConfirmar_;
            fecharDialogo();
            cb(std::move(selecionados));
        }
    };
    addAndMakeVisible(*btnRescanSelecionados_);

    btnRescanTodos_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Rescanear todos") : "Rescan All");
    btnRescanTodos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnRescanTodos_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnRescanTodos_->onClick = [this] {
        std::vector<matriz::vault::RescanPair> todos;
        for (auto& linha : linhas_) {
            todos.push_back(linha.par);
        }
        if (onConfirmar_) {
            auto cb = onConfirmar_;
            fecharDialogo();
            cb(std::move(todos));
        }
    };
    addAndMakeVisible(*btnRescanTodos_);

    btnCancelar_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Cancelar") : "Cancel");
    btnCancelar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnCancelar_->onClick = [this] { fecharDialogo(); };
    addAndMakeVisible(*btnCancelar_);

    atualizarEstadoBotoes();
    setSize(520, std::max(220, std::min(450, 140 + static_cast<int>(linhas_.size()) * 36)));
}

RescanBackupSourcesDialog::~RescanBackupSourcesDialog() = default;

void RescanBackupSourcesDialog::atualizarEstadoBotoes() {
    int marcados = 0;
    for (auto& linha : linhas_) {
        if (linha.toggle && linha.toggle->getToggleState()) {
            marcados++;
        }
    }
    if (btnRescanSelecionados_) {
        btnRescanSelecionados_->setEnabled(marcados > 0);
    }
}

void RescanBackupSourcesDialog::fecharDialogo() {
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
    }
}

void RescanBackupSourcesDialog::lookAndFeelChanged() {
    const auto& tk = tema();
    if (lblTitulo_) lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    for (auto& l : linhas_) {
        if (l.toggle) {
            l.toggle->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
            l.toggle->setColour(juce::ToggleButton::tickColourId, tk.acento);
        }
    }
    if (btnRescanSelecionados_) {
        btnRescanSelecionados_->setColour(juce::TextButton::buttonColourId, tk.acento);
        btnRescanSelecionados_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    }
    if (btnRescanTodos_) {
        btnRescanTodos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnRescanTodos_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (btnCancelar_) {
        btnCancelar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnCancelar_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    repaint();
}

void RescanBackupSourcesDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);
}

void RescanBackupSourcesDialog::resized() {
    auto area = getLocalBounds().reduced(20, 16);

    lblTitulo_->setBounds(area.removeFromTop(28));
    area.removeFromTop(12);

    auto btnArea = area.removeFromBottom(34);
    area.removeFromBottom(12);

    int btnW = 150;
    btnRescanSelecionados_->setBounds(btnArea.removeFromLeft(btnW));
    btnArea.removeFromLeft(10);
    btnRescanTodos_->setBounds(btnArea.removeFromLeft(130));
    btnCancelar_->setBounds(btnArea.removeFromRight(100));

    viewport_->setBounds(area);

    int linhaH = 34;
    int totalH = static_cast<int>(linhas_.size()) * linhaH;
    containerLinhas_->setSize(viewport_->getWidth() - 12, std::max(totalH, area.getHeight()));

    int y = 0;
    for (auto& l : linhas_) {
        if (l.toggle) {
            l.toggle->setBounds(8, y, containerLinhas_->getWidth() - 16, linhaH - 4);
            y += linhaH;
        }
    }
}

void RescanBackupSourcesDialog::showDialog(
    std::vector<matriz::vault::RescanPair> pares,
    std::function<void(std::vector<matriz::vault::RescanPair>)> onConfirmar) {

    auto* dlg = new RescanBackupSourcesDialog(std::move(pares), std::move(onConfirmar));
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dlg);
    options.dialogTitle = isPt ? juce::String::fromUTF8("Rescanear Fontes de Backup") : "Rescan Backup Sources";
    options.dialogBackgroundColour = tema().painel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;

    if (auto* tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        auto area = tela->userBounds;
        dlg->setCentrePosition(area.getCentreX(), area.getCentreY());
    }

    options.launchAsync();
}

} // namespace matriz::ui
