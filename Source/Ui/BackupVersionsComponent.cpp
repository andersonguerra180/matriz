#include "BackupVersionsComponent.h"
#include "../Consolidacao/Consolidacao.h"

#include "../I18n/Strings.h"
#include "../Model/ProjectLog.h"
#include "BackupRecoveryDialog.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

class VersionRowComponent : public juce::Component {
public:
    // Callbacks recebem a linha ATUAL: o ListBox reaproveita o componente
    // entre linhas ao rolar/recarregar.
    VersionRowComponent(std::function<void(int)> onRenomear, std::function<void(int)> onDesvincular)
        : onRenomear_(std::move(onRenomear)), onDesvincular_(std::move(onDesvincular))
    {
        const auto& tk = tema();

        btnRenomear_.setButtonText(matriz::i18n::t("backup.renomear"));
        btnRenomear_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnRenomear_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnRenomear_.onClick = [this] { if (onRenomear_) onRenomear_(linha_); };
        addAndMakeVisible(btnRenomear_);

        btnDesvincular_.setButtonText(matriz::i18n::t("backup.desvincular"));
        btnDesvincular_.setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnDesvincular_.setColour(juce::TextButton::textColourOffId, tk.perigo);
        btnDesvincular_.onClick = [this] { if (onDesvincular_) onDesvincular_(linha_); };
        addAndMakeVisible(btnDesvincular_);
    }

    void update(const ProjetoAberto::VersaoResumo& versao, bool isEven, int linha) {
        versao_ = versao;
        linha_ = linha;
        isEven_ = isEven;
        // SOURCE não se desvincula (é proveniência); o MAIN mostra o botão,
        // mas o clique só avisa (ver desvincularVersao).
        btnDesvincular_.setVisible(versao_.papel != ProjetoAberto::VersaoResumo::Papel::Source);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        using Papel = ProjetoAberto::VersaoResumo::Papel;
        const auto& tk = tema();
        g.fillAll(isEven_ ? tk.painel : tk.painelAlt);

        auto r = getLocalBounds().reduced(10, 0);
        int h = getHeight();

        // 1. Selo de papel
        juce::String papelTxt;
        juce::Colour corPapel;
        switch (versao_.papel) {
            case Papel::Main:   papelTxt = matriz::i18n::t("backup.papel_main");   corPapel = juce::Colour(0xff2563eb); break;
            case Papel::Clone:  papelTxt = matriz::i18n::t("backup.papel_clone");  corPapel = juce::Colour(0xff0d9488); break;
            case Papel::Source: papelTxt = matriz::i18n::t("backup.papel_source"); corPapel = juce::Colour(0xff9a6b1f); break;
        }
        juce::Rectangle<int> selo(r.getX(), (h - 22) / 2, 74, 22);
        g.setColour(corPapel);
        g.fillRoundedRectangle(selo.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(papelTxt, selo, juce::Justification::centred, true);

        // 2. Rótulo (linha de cima) e caminho (linha de baixo)
        int textoX = selo.getRight() + 12;
        int statusW = 300;
        int botoesW = 190;
        int textoW = std::max(60, r.getRight() - botoesW - statusW - textoX);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(versao_.codigo.isNotEmpty() ? versao_.codigo + "  " + versao_.rotulo : versao_.rotulo,
                   textoX, 4, textoW, h / 2 - 2, juce::Justification::bottomLeft, true);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(versao_.caminho, textoX, h / 2, textoW, h / 2 - 4, juce::Justification::topLeft, true);

        // 3. Status (linha de cima) e números (linha de baixo)
        int statusX = textoX + textoW + 10;
        juce::String status;
        juce::Colour corStatus = tk.textoSecundario;
        juce::String numeros;
        auto dataCurta = [](const juce::String& iso) {
            // "YYYY-MM-DD..." -> "DD/MM"
            return iso.length() >= 10 ? iso.substring(8, 10) + "/" + iso.substring(5, 7) : iso;
        };
        if (versao_.papel == Papel::Source) {
            if (!versao_.online) {
                status = matriz::i18n::t("backup.status_guardado");
            } else if (versao_.dependentes == 0 && versao_.totalItens > 0) {
                status = matriz::i18n::t("backup.status_liberado");
                corStatus = tk.estadoQcOk;
            } else {
                status = matriz::i18n::t("backup.status_conectado");
            }
            numeros = matriz::i18n::t("backup.itens_fonte").replace("{n}", juce::String(versao_.totalItens));
            if (versao_.ingestoes > 0)
                numeros += "   |   " + matriz::i18n::t("backup.ingestoes_fonte")
                                           .replace("{n}", juce::String(versao_.ingestoes))
                                           .replace("{d}", dataCurta(versao_.ultimaData));
            if (versao_.dependentes > 0) {
                numeros += "   |   " + matriz::i18n::t("backup.status_dependentes")
                                           .replace("{n}", juce::String(versao_.dependentes));
                corStatus = versao_.online ? tk.alerta : corStatus;
            }
        } else {
            if (versao_.papel == Papel::Clone) {
                if (versao_.desatualizado) {
                    status = matriz::i18n::t("backup.status_desatualizado").replace("{d}", dataCurta(versao_.ultimaData));
                    corStatus = tk.alerta;
                } else {
                    status = matriz::i18n::t("backup.status_em_dia");
                    corStatus = tk.estadoQcOk;
                }
            } else {
                status = versao_.online ? matriz::i18n::t("backup.status_online") : matriz::i18n::t("backup.status_offline");
                corStatus = versao_.online ? tk.estadoQcOk : tk.perigo;
            }
            if (!versao_.online && versao_.papel == Papel::Clone) status += " (offline)";
            // CLONE é espelho do MAIN: o que importa é quando sincronizou.
            if (versao_.papel == Papel::Clone)
                numeros = versao_.ultimaData.isNotEmpty()
                              ? matriz::i18n::t("backup.ultima_sync_em").replace("{d}", dataCurta(versao_.ultimaData))
                              : juce::String();
            else
                numeros = matriz::i18n::t("backup.itens_contagem").replace("{n}", juce::String(versao_.totalItens));
            if (versao_.papel == Papel::Main && versao_.ultimaData.isNotEmpty())
                numeros += "   |   " + matriz::i18n::t("backup.ultimo_backup_em").replace("{d}", versao_.ultimaData.substring(0, 10));
        }
        g.setColour(corStatus);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText(status, statusX, 4, statusW, h / 2 - 2, juce::Justification::bottomLeft, true);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(numeros, statusX, h / 2, statusW, h / 2 - 4, juce::Justification::topLeft, true);
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
    ProjetoAberto::VersaoResumo versao_;
    bool isEven_ = false;
    int linha_ = -1;
    std::function<void(int)> onRenomear_;
    std::function<void(int)> onDesvincular_;
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

    lblDescricao_.setText(isPt ? juce::String::fromUTF8("O MAIN (fonte da verdade), seus CLONEs e os SOURCEs de onde o material veio:")
                               : "The MAIN (source of truth), its CLONEs and the SOURCEs the material came from:",
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

    if (linhas_.empty()) {
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
    carregarVersoes();  // aplicarVersoes() atualiza a lista quando a carga volta
}

int BackupVersionsComponent::getNumRows() {
    return static_cast<int>(linhas_.size());
}

void BackupVersionsComponent::paintListBoxItem(int, juce::Graphics&, int, int, bool) {}

juce::Component* BackupVersionsComponent::refreshComponentForRow(int rowNumber, bool, juce::Component* existingComponentToUpdate) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(linhas_.size())) {
        delete existingComponentToUpdate;
        return nullptr;
    }

    auto* rowComp = dynamic_cast<VersionRowComponent*>(existingComponentToUpdate);
    const auto& versao = linhas_[static_cast<size_t>(rowNumber)];

    if (rowComp == nullptr) {
        rowComp = new VersionRowComponent(
            [this](int linha) {
                if (linha >= 0 && linha < static_cast<int>(linhas_.size())) renomearVersao(linhas_[static_cast<size_t>(linha)]);
            },
            [this](int linha) {
                if (linha >= 0 && linha < static_cast<int>(linhas_.size())) desvincularVersao(linhas_[static_cast<size_t>(linha)]);
            });
    }

    rowComp->update(versao, rowNumber % 2 == 0, rowNumber);
    return rowComp;
}

void BackupVersionsComponent::carregarVersoes() {
    // Banco + existência das pastas (discos que podem estar dormindo): fora
    // da message thread. A geração descarta respostas de cargas antigas.
    const int geracao = ++geracaoCarga_;
    juce::Component::SafePointer<BackupVersionsComponent> safeThis(this);
    ProjetoAberto* projeto = &projeto_;
    poolCarga_.addJob([safeThis, projeto, geracao] {
        std::vector<ProjetoAberto::VersaoResumo> linhas;
        try { linhas = projeto->listarVersoes(); } catch (...) {}
        juce::MessageManager::callAsync([safeThis, geracao, linhas = std::move(linhas)]() mutable {
            if (safeThis == nullptr || geracao != safeThis->geracaoCarga_) return;
            safeThis->aplicarVersoes(std::move(linhas));
        });
    });
}

void BackupVersionsComponent::aplicarVersoes(std::vector<ProjetoAberto::VersaoResumo> linhas) {
    linhas_ = std::move(linhas);
    versoes_.clear();
    for (const auto& l : linhas_) {
        if (l.papel == ProjetoAberto::VersaoResumo::Papel::Source) continue;
        BackupVersionRef ref;
        ref.id = l.id;
        ref.rotulo = l.rotulo;
        ref.destinoPath = l.caminho;
        ref.totalItens = l.totalItens;
        ref.ultimoBackup = l.ultimaData;
        versoes_.push_back(std::move(ref));
    }
    btnSincronizar_.setEnabled(versoes_.size() >= 2);
    btnRecuperar_.setEnabled(!versoes_.empty());
    listBox_.updateContent();
    listBox_.repaint();
    repaint();
}

void BackupVersionsComponent::renomearVersao(const ProjetoAberto::VersaoResumo& versao) {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    auto alert = std::make_shared<juce::AlertWindow>(
        isPt ? "Renomear Versão de Backup" : "Rename Backup Version",
        isPt ? "Digite o novo rótulo amigável para esta versão:" : "Enter the new friendly label for this version:",
        juce::AlertWindow::QuestionIcon);

    alert->addTextEditor("rotulo", versao.rotulo, isPt ? "Novo rótulo" : "New label");
    // SOURCE: o código (sufixo _S01 / pasta raiz) é editável até o primeiro
    // arquivo dele entrar no MAIN; depois fica fixo.
    const bool editaCodigo = versao.papel == ProjetoAberto::VersaoResumo::Papel::Source && versao.codigoEditavel;
    if (editaCodigo) alert->addTextEditor("codigo", versao.codigo, matriz::i18n::t("backup.codigo_source"));
    alert->addButton(isPt ? "Salvar" : "Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton(isPt ? "Cancelar" : "Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alert->enterModalState(true, juce::ModalCallbackFunction::create([this, versao, alert](int result) {
        if (result != 1) return;
        juce::String novoRotulo = alert->getTextEditorContents("rotulo").trim();
        if (versao.papel == ProjetoAberto::VersaoResumo::Papel::Source && versao.codigoEditavel) {
            const juce::String novoCodigo = alert->getTextEditorContents("codigo").trim();
            if (novoCodigo.isNotEmpty() && novoCodigo != versao.codigo) {
                bool emUso = false;
                for (const auto& [id, cod] : matriz::consolidacao::codigosDeSource(projeto_.projeto().registro()))
                    if (id != versao.id && juce::String(cod).equalsIgnoreCase(novoCodigo)) emUso = true;
                if (!matriz::consolidacao::codigoDeSourceValido(novoCodigo) || emUso) {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           matriz::i18n::t("backup.codigo_source"),
                                                           matriz::i18n::t(emUso ? "backup.codigo_source_em_uso"
                                                                                 : "backup.codigo_source_invalido"));
                    return;
                }
                projeto_.projeto().registro().run("UPDATE vault SET codigo = ? WHERE id = ?",
                                                  {matriz::db::Value::of(novoCodigo.toStdString()),
                                                   matriz::db::Value::of(versao.id)});
                if (novoRotulo.isEmpty() || novoRotulo == versao.rotulo) {
                    recarregar();
                    return;
                }
            }
        }
        if (novoRotulo.isEmpty() || novoRotulo == versao.rotulo) return;

        // Só o rótulo (apelido do disco). Num SOURCE a pasta no MAIN nunca muda.
        const bool ehSource = versao.papel == ProjetoAberto::VersaoResumo::Papel::Source;
        projeto_.projeto().registro().run(
            ehSource ? "UPDATE vault SET nome = ? WHERE id = ?" : "UPDATE backup_destino SET rotulo = ? WHERE id = ?",
            {matriz::db::Value::of(novoRotulo.toStdString()), matriz::db::Value::of(versao.id)});

        matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
        juce::StringArray details;
        details.add("Old Label: " + versao.rotulo);
        details.add("New Label: " + novoRotulo);
        pLog.appendEntry("Backup Version Renamed", details);

        recarregar();
    }));
}

void BackupVersionsComponent::desvincularVersao(const ProjetoAberto::VersaoResumo& versao) {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    if (versao.papel == ProjetoAberto::VersaoResumo::Papel::Source) return;
    if (versao.papel == ProjetoAberto::VersaoResumo::Papel::Main) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("backup.desvincular"),
                                               matriz::i18n::t("backup.main_desvincular_bloqueado"));
        return;
    }

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
            details.add("Path: " + versao.caminho);
            pLog.appendEntry("Backup Version Unlinked", details);

            recarregar();
        }));
}

} // namespace matriz::ui
