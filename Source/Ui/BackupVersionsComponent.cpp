#include "BackupVersionsComponent.h"

#include "../I18n/Strings.h"
#include "../Model/ProjectLog.h"
#include "BackupRecoveryDialog.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

class VersionRowComponent : public juce::Component {
public:
    VersionRowComponent(std::function<void()> onRenomear, std::function<void()> onDesvincular)
        : onRenomear_(std::move(onRenomear)), onDesvincular_(std::move(onDesvincular))
    {
        const auto& tk = tema();
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

        btnRenomear_.setButtonText(matriz::i18n::t("backup.renomear"));
        btnRenomear_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnRenomear_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnRenomear_.onClick = [this] { if (onRenomear_) onRenomear_(); };
        addAndMakeVisible(btnRenomear_);

        btnDesvincular_.setButtonText(matriz::i18n::t("backup.desvincular"));
        btnDesvincular_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnDesvincular_.setColour(juce::TextButton::textColourOffId, tk.perigo);
        btnDesvincular_.onClick = [this] { if (onDesvincular_) onDesvincular_(); };
        addAndMakeVisible(btnDesvincular_);
    }

    void update(const BackupVersionRef& versao, bool isEven) {
        versao_ = versao;
        isEven_ = isEven;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(isEven_ ? tk.painel : tk.painelAlt);

        auto r = getLocalBounds().reduced(10, 0);
        int h = getHeight();

        // 1. Badge com o Rótulo
        int strW = juce::GlyphArrangement::getStringWidthInt(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)), versao_.rotulo);
        int badgeW = std::min(180, std::max(120, strW + 24));
        juce::Rectangle<int> badgeRect(r.getX(), (h - 24) / 2, badgeW, 24);
        g.setColour(tk.acento.withAlpha(0.2f));
        g.fillRoundedRectangle(badgeRect.toFloat(), 4.0f);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(badgeRect.toFloat(), 4.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(versao_.rotulo, badgeRect, juce::Justification::centred, true);

        // 2. Caminho do Destino
        int pathX = badgeRect.getRight() + 16;
        int statsW = 280;
        int botoesW = 190;
        int pathW = std::max(60, r.getRight() - botoesW - statsW - pathX);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(versao_.destinoPath, pathX, 0, pathW, h, juce::Justification::centredLeft, true);

        // 3. Estatísticas: Itens e Último backup
        int statsX = pathX + pathW + 10;
        juce::String textoItens = matriz::i18n::t("backup.itens_contagem").replace("{n}", juce::String(versao_.totalItens));
        juce::String textoUltimo = versao_.ultimoBackup.isEmpty()
            ? ""
            : matriz::i18n::t("backup.ultimo_backup_em").replace("{d}", versao_.ultimoBackup.substring(0, 10));

        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(textoItens + (textoUltimo.isNotEmpty() ? "   |   " + textoUltimo : ""),
                   statsX, 0, statsW, h, juce::Justification::centredLeft, true);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10, 0);
        int h = getHeight();
        int btnH = 26;
        int btnY = (h - btnH) / 2;

        btnDesvincular_.setBounds(r.getRight() - 88, btnY, 88, btnH);
        btnRenomear_.setBounds(r.getRight() - 182, btnY, 86, btnH);
    }

private:
    BackupVersionRef versao_;
    bool isEven_ = false;
    std::function<void()> onRenomear_;
    std::function<void()> onDesvincular_;
    juce::TextButton btnRenomear_;
    juce::TextButton btnDesvincular_;
};

} // namespace

BackupVersionsComponent::BackupVersionsComponent(ProjetoAberto& projeto)
    : projeto_(projeto)
{
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    lblTitulo_.setText(matriz::i18n::t("backup.versoes_titulo"), juce::dontSendNotification);
    lblTitulo_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblTitulo_.setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(lblTitulo_);

    lblDescricao_.setText(isPt ? juce::String::fromUTF8("Pastas e unidades de destino que já receberam backups desta coleção:")
                               : "Destination drives and folders that have received backups of this collection:",
                          juce::dontSendNotification);
    lblDescricao_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    lblDescricao_.setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(lblDescricao_);

    listBox_.setModel(this);
    listBox_.setRowHeight(48);
    listBox_.setColour(juce::ListBox::backgroundColourId, tk.painel);
    listBox_.setColour(juce::ListBox::outlineColourId, tk.borda);
    addAndMakeVisible(listBox_);

    btnSincronizar_.setButtonText(matriz::i18n::t("backup.btn_sincronizar"));
    btnSincronizar_.setColour(juce::TextButton::buttonColourId, tk.acento);
    btnSincronizar_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnSincronizar_.onClick = [this] {
        BackupSyncDialog::showSyncDialog(projeto_, versoes_, [this] { recarregar(); });
    };
    addAndMakeVisible(btnSincronizar_);

    btnRecuperar_.setButtonText(matriz::i18n::t("backup.btn_recuperar"));
    btnRecuperar_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnRecuperar_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnRecuperar_.onClick = [this] {
        BackupRecoveryDialog::showRecoveryDialog(projeto_, versoes_);
    };
    addAndMakeVisible(btnRecuperar_);

    carregarVersoes();
}

void BackupVersionsComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    if (versoes_.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("backup.nenhuma_versao"),
                   listBox_.getBounds(), juce::Justification::centred);
    }
}

void BackupVersionsComponent::resized() {
    auto r = getLocalBounds().reduced(24, 20);

    lblTitulo_.setBounds(r.removeFromTop(32));
    r.removeFromTop(4);
    lblDescricao_.setBounds(r.removeFromTop(22));
    r.removeFromTop(16);

    auto linhaBotoes = r.removeFromBottom(40);
    btnSincronizar_.setBounds(linhaBotoes.removeFromLeft(220));
    linhaBotoes.removeFromLeft(16);
    btnRecuperar_.setBounds(linhaBotoes.removeFromLeft(220));

    r.removeFromBottom(16);
    listBox_.setBounds(r);
}

void BackupVersionsComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    lblTitulo_.setColour(juce::Label::textColourId, tk.textoPrimario);
    lblDescricao_.setColour(juce::Label::textColourId, tk.textoSecundario);
    listBox_.setColour(juce::ListBox::backgroundColourId, tk.painel);
    listBox_.setColour(juce::ListBox::outlineColourId, tk.borda);
    btnSincronizar_.setColour(juce::TextButton::buttonColourId, tk.acento);
    btnSincronizar_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnRecuperar_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnRecuperar_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    repaint();
}

void BackupVersionsComponent::recarregar() {
    carregarVersoes();
    listBox_.updateContent();
    listBox_.repaint();
    repaint();
}

int BackupVersionsComponent::getNumRows() {
    return static_cast<int>(versoes_.size());
}

void BackupVersionsComponent::paintListBoxItem(int, juce::Graphics&, int, int, bool) {}

juce::Component* BackupVersionsComponent::refreshComponentForRow(int rowNumber, bool, juce::Component* existingComponentToUpdate) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(versoes_.size())) {
        delete existingComponentToUpdate;
        return nullptr;
    }

    auto* rowComp = dynamic_cast<VersionRowComponent*>(existingComponentToUpdate);
    const auto& versao = versoes_[static_cast<size_t>(rowNumber)];

    if (rowComp == nullptr) {
        rowComp = new VersionRowComponent(
            [this, versao] { renomearVersao(versao); },
            [this, versao] { desvincularVersao(versao); });
    }

    rowComp->update(versao, rowNumber % 2 == 0);
    return rowComp;
}

void BackupVersionsComponent::carregarVersoes() {
    versoes_.clear();
    projeto_.sincronizarBackupDestinoDeHistorico();

    auto& db = projeto_.projeto().registro();
    try {
        auto stmt = db.prepare("SELECT id, destino_path, rotulo, ativo, criado_em FROM backup_destino WHERE ativo = 1 ORDER BY criado_em ASC");
        while (stmt.step()) {
            BackupVersionRef ref;
            ref.id = stmt.columnText(0);
            ref.destinoPath = juce::String::fromUTF8(stmt.columnText(1).c_str());
            ref.rotulo = juce::String::fromUTF8(stmt.columnText(2).c_str());

            auto stmtStats = db.prepare("SELECT COUNT(DISTINCT item_id), MAX(consolidado_em) FROM consolidacao_registro WHERE destino_path = ?");
            stmtStats.bind(1, matriz::db::Value::of(stmt.columnText(1)));
            if (stmtStats.step()) {
                ref.totalItens = stmtStats.columnInt(0);
                if (!stmtStats.columnIsNull(1)) {
                    ref.ultimoBackup = juce::String::fromUTF8(stmtStats.columnText(1).c_str());
                }
            }
            versoes_.push_back(std::move(ref));
        }
    } catch (...) {}

    btnSincronizar_.setEnabled(versoes_.size() >= 2);
    btnRecuperar_.setEnabled(!versoes_.empty());
}

void BackupVersionsComponent::renomearVersao(const BackupVersionRef& versao) {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    auto alert = std::make_shared<juce::AlertWindow>(
        isPt ? "Renomear Versão de Backup" : "Rename Backup Version",
        isPt ? "Digite o novo rótulo amigável para esta versão:" : "Enter the new friendly label for this version:",
        juce::AlertWindow::QuestionIcon);

    alert->addTextEditor("rotulo", versao.rotulo, isPt ? "Novo rótulo" : "New label");
    alert->addButton(isPt ? "Salvar" : "Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton(isPt ? "Cancelar" : "Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alert->enterModalState(true, juce::ModalCallbackFunction::create([this, versao, alert](int result) {
        if (result != 1) return;
        juce::String novoRotulo = alert->getTextEditorContents("rotulo").trim();
        if (novoRotulo.isEmpty() || novoRotulo == versao.rotulo) return;

        projeto_.projeto().registro().run(
            "UPDATE backup_destino SET rotulo = ? WHERE id = ?",
            {matriz::db::Value::of(novoRotulo.toStdString()), matriz::db::Value::of(versao.id)});

        matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
        juce::StringArray details;
        details.add("Old Label: " + versao.rotulo);
        details.add("New Label: " + novoRotulo);
        pLog.appendEntry("Backup Version Renamed", details);

        recarregar();
    }));
}

void BackupVersionsComponent::desvincularVersao(const BackupVersionRef& versao) {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        matriz::i18n::t("backup.desvincular"),
        matriz::i18n::t("backup.desvincular_confirmacao"),
        matriz::i18n::t("backup.desvincular"),
        isPt ? "Cancelar" : "Cancel",
        nullptr,
        juce::ModalCallbackFunction::create([this, versao](int result) {
            if (result != 1) return;

            projeto_.projeto().registro().run(
                "UPDATE backup_destino SET ativo = 0 WHERE id = ?",
                {matriz::db::Value::of(versao.id)});

            matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
            juce::StringArray details;
            details.add("Label: " + versao.rotulo);
            details.add("Path: " + versao.destinoPath);
            pLog.appendEntry("Backup Version Unlinked", details);

            recarregar();
        }));
}

} // namespace matriz::ui
