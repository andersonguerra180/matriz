#include "BackupWorkspaceComponent.h"
#include <unordered_map>
#include "../App/SemSono.h"
#include "MainEditPanel.h"
#include "GoogleDriveContas.h"
#include "BackupFileSelectorDialog.h"
#include "BackupSyncDialog.h"
#include "BackupScanProgressDialog.h"
#include "SyncDestinationDialog.h"
#include "PublishHtmlDialog.h"
#include "ExportZipDialog.h"
#include "SendToPrintDialog.h"
#include "BatchWatermarkDialog.h"
#include "MainEditPanel.h"
#include "../Sync/SyncEngine.h"
#include <AssetsBinaryData.h>
#include "Tokens.h"
#include "Moldura.h"
#include "../I18n/Strings.h"
#include "../Catalogo/CatalogoProxies.h"
#include "../Catalogo/CatalogSiteExport.h"
#include "../Diag/Watchdog.h"
#include "../Consolidacao/MetadadoEmbutido.h"
#include "../Consolidacao/PacoteCollection.h"
#include "../Vault/Resolucao.h"
#include "../Vault/Volume.h"
#include "../Preservation/Preservation.h"
#include "ProgressoGlobal.h"
#include "../Model/ProjectLog.h"
#include "../Vault/DeviceUsageLog.h"

namespace matriz::ui {

namespace {

// Janela de status do backup/Rebuild: o que falhou (agrupado por causa), o que ainda falta e, principalmente,
// os botões para resolver ali mesmo (remover do catálogo, mostrar no Grid, concluir o Rebuild) em vez de mandar
// o usuário procurar o problema pelo app. O relatório fica gravado em Project/relatorios/.
struct DadosStatusBackup {
    juce::String titulo, texto;
    juce::File relatorio;
    bool bloqueado = false;       // há itens que impedem o fim
    int nRemoviveis = 0;          // ausentes / alterados: o conserto é tirá-los do catálogo
    int nBloqueados = 0;
    bool rebuild = false;
    bool podeFinalizar = false;
    std::function<void()> aoRemover, aoMostrarNoGrid, aoFinalizar;
};

class ErrosBackupDialog : public juce::Component {
public:
    explicit ErrosBackupDialog(DadosStatusBackup d) : d_(std::move(d)) {
        const auto& tk = tema();
        lblTitulo_.setText(d_.titulo, juce::dontSendNotification);
        lblTitulo_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        lblTitulo_.setColour(juce::Label::textColourId, d_.bloqueado ? tk.perigo : tk.estadoQcOk);
        addAndMakeVisible(lblTitulo_);

        editor_.setMultiLine(true, true);
        editor_.setReadOnly(true);
        editor_.setScrollbarsShown(true);
        editor_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain)));
        editor_.setColour(juce::TextEditor::backgroundColourId, tk.fundo);
        editor_.setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        editor_.setColour(juce::TextEditor::outlineColourId, tk.borda);
        editor_.setText(d_.texto, false);
        addAndMakeVisible(editor_);

        auto botao = [&](juce::TextButton& b, const juce::String& t, std::function<void()> fn, bool visivel = true, bool ativo = true) {
            b.setButtonText(t);
            b.setEnabled(ativo);
            b.onClick = std::move(fn);
            addChildComponent(b);
            b.setVisible(visivel);
        };
        // Ações que resolvem: fecha a janela e só então age (o callback pode reabrir uma janela atualizada).
        auto fechando = [this](const std::function<void()>& cb) {
            return [this, cb] {
                auto copia = cb;
                fechar();
                if (copia) copia();
            };
        };
        botao(btnRemover_, matriz::i18n::t("rebuild.btn_remover").replace("{n}", juce::String(d_.nRemoviveis)),
              fechando(d_.aoRemover), d_.nRemoviveis > 0);
        botao(btnMostrar_, matriz::i18n::t("rebuild.btn_mostrar"), fechando(d_.aoMostrarNoGrid), d_.nBloqueados > 0);
        botao(btnFinalizar_, matriz::i18n::t("rebuild.btn_finalizar"), fechando(d_.aoFinalizar), d_.rebuild, d_.podeFinalizar);
        botao(btnCopiar_, matriz::i18n::t("backup.erros_copiar"), [this] { juce::SystemClipboard::copyTextToClipboard(d_.texto); });
        botao(btnAbrir_, matriz::i18n::t("backup.erros_abrir"), [this] {
            if (!d_.relatorio.startAsProcess()) d_.relatorio.revealToUser();
        }, true, d_.relatorio.existsAsFile());
        botao(btnFechar_, matriz::i18n::t("backup.erros_fechar"), [this] { fechar(); });
        if (d_.podeFinalizar) btnFinalizar_.setColour(juce::TextButton::buttonColourId, tk.estadoQcOk);
        else if (d_.nRemoviveis > 0) btnRemover_.setColour(juce::TextButton::buttonColourId, tk.perigo);
        setSize(1000, 600);
    }

    void paint(juce::Graphics& g) override { g.fillAll(tema().painel); }

    void resized() override {
        auto r = getLocalBounds().reduced(20);
        lblTitulo_.setBounds(r.removeFromTop(26));
        r.removeFromTop(8);
        auto baixo = r.removeFromBottom(34);
        auto coloca = [&](juce::TextButton& b, int largura) {
            if (!b.isVisible()) return;
            b.setBounds(baixo.removeFromRight(largura));
            baixo.removeFromRight(8);
        };
        coloca(btnFechar_, 90);
        coloca(btnFinalizar_, 160);
        coloca(btnAbrir_, 130);
        coloca(btnCopiar_, 110);
        coloca(btnMostrar_, 130);
        coloca(btnRemover_, 270);
        r.removeFromBottom(10);
        editor_.setBounds(r);
    }

    static void mostrar(DadosStatusBackup d, juce::Component* centrarEm) {
        const juce::String titulo = d.titulo;
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(new ErrosBackupDialog(std::move(d)));
        opts.dialogTitle = titulo;
        opts.dialogBackgroundColour = tema().painel;
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.resizable = true;
        opts.componentToCentreAround = centrarEm;
        moldura::lancar(opts);
    }

private:
    void fechar() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
    }

    DadosStatusBackup d_;
    juce::Label lblTitulo_;
    juce::TextEditor editor_;
    juce::TextButton btnRemover_, btnMostrar_, btnFinalizar_, btnCopiar_, btnAbrir_, btnFechar_;
};


// EXPORT (etapa 6): opções livres — não afetam nada no MAIN.
struct OpcoesExport {
    juce::File destino;
    matriz::consolidacao::HierarquiaBackup hierarquia;
    matriz::consolidacao::ModoPrefixoArquivo modoPrefixo = matriz::consolidacao::ModoPrefixoArquivo::Nenhum;
    juce::String prefixo;
    bool embutir = true;
    bool marcaDagua = false;
    std::string mapaId;  // Fase 2: folder map do usuário que estrutura o export ("" = mapa padrão)
    bool porMapa = false;
    matriz::consolidacao::OpcoesMetadadosSaida metaSaida;  // só Bounce List: Original / Embed / Clean + sidecar
};

class ConteudoExport : public juce::Component {
public:
    ConteudoExport(int marcadosW, bool marcaDaguaConfigurada, const juce::String& prefixoPadrao,
                   std::vector<std::pair<std::string, juce::String>> mapas,
                   std::function<void(OpcoesExport)> aoExportar, bool formatoBridge = false, bool bounce = false)
        : aoExportar_(std::move(aoExportar)), mapas_(std::move(mapas)), bounce_(bounce) {
        const auto& tk = tema();
        auto rotulo = [&](juce::Label& l, const juce::String& t) {
            l.setText(t, juce::dontSendNotification);
            l.setColour(juce::Label::textColourId, tk.textoSecundario);
            l.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            addAndMakeVisible(l);
        };
        rotulo(lblIntro_, matriz::i18n::t(bounce ? "export.bounce_intro" : "export.intro"));
        lblIntro_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        rotulo(lblPasta_, matriz::i18n::t("export.pasta"));
        lblCaminho_.setColour(juce::Label::textColourId, tk.textoPrimario);
        lblCaminho_.setText(matriz::i18n::t("export.pasta_nenhuma"), juce::dontSendNotification);
        addAndMakeVisible(lblCaminho_);
        btnEscolher_.setButtonText(matriz::i18n::t("export.escolher"));
        btnEscolher_.onClick = [this] { escolherPasta(); };
        addAndMakeVisible(btnEscolher_);

        rotulo(lblEstrutura_, matriz::i18n::t("backup.secao_organizacao"));
        comboEstrutura_.addItem(matriz::i18n::t("backup.manter_organizacao"), 1);
        comboEstrutura_.addItem(matriz::i18n::t("export.por_tipo"), 2);
        comboEstrutura_.addItem(matriz::i18n::t("export.por_ano"), 3);
        comboEstrutura_.addItem(matriz::i18n::t("export.por_tipo_ano"), 4);
        comboEstrutura_.addItem(matriz::i18n::t("backup.preservar_estrutura"), 6);
        if (!mapas_.empty()) comboEstrutura_.addItem(matriz::i18n::t("export.por_mapa"), 7);
        if (bounce) comboEstrutura_.addItem(matriz::i18n::t("export.por_editor_visual"), 8);  // monta a hierarquia no Visual Editor
        comboEstrutura_.setSelectedId(1, juce::dontSendNotification);
        comboEstrutura_.onChange = [this] {
            comboMapa_.setVisible(comboEstrutura_.getSelectedId() == 7);
            btnEditorVisual_.setVisible(bounce_ && comboEstrutura_.getSelectedId() == 8);
            resized();
        };
        btnEditorVisual_.setButtonText(matriz::i18n::t("export.abrir_editor_visual"));
        btnEditorVisual_.setColour(juce::TextButton::buttonColourId, tk.botaoSecundario);  // OPEN VISUAL EDITOR: secundário
        btnEditorVisual_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnEditorVisual_.onClick = [this] { abrirEditorVisual(); };
        addChildComponent(btnEditorVisual_);
        addAndMakeVisible(comboEstrutura_);
        for (size_t i = 0; i < mapas_.size(); ++i) comboMapa_.addItem(mapas_[i].second, static_cast<int>(i) + 1);
        if (!mapas_.empty()) comboMapa_.setSelectedId(1, juce::dontSendNotification);
        addChildComponent(comboMapa_);

        rotulo(lblNomes_, matriz::i18n::t("backup.prefixo_arquivos"));
        comboNomes_.addItem(matriz::i18n::t("backup.prefixo_modo_nenhum"), 1);
        comboNomes_.addItem(matriz::i18n::t("backup.prefixo_modo_auto").replace("{prefix}", prefixoPadrao), 2);
        comboNomes_.addItem(matriz::i18n::t("backup.prefixo_modo_custom"), 3);
        comboNomes_.setSelectedId(1, juce::dontSendNotification);
        comboNomes_.onChange = [this] { editPrefixo_.setVisible(comboNomes_.getSelectedId() == 3); };
        addAndMakeVisible(comboNomes_);
        editPrefixo_.setText(prefixoPadrao, juce::dontSendNotification);
        addChildComponent(editPrefixo_);

        toggleEmbutir_.setButtonText(matriz::i18n::t(formatoBridge ? "export.embutir_bridge" : "export.embutir"));
        toggleEmbutir_.setToggleState(true, juce::dontSendNotification);
        toggleEmbutir_.setEnabled(!formatoBridge);  // no Adobe Bridge o XMP é o próprio objetivo do export
        toggleMarca_.setButtonText(matriz::i18n::t("export.marca_dagua").replace("{n}", juce::String(marcadosW)));
        toggleMarca_.setEnabled(marcadosW > 0 && marcaDaguaConfigurada);
        toggleMarca_.setToggleState(marcadosW > 0 && marcaDaguaConfigurada, juce::dontSendNotification);
        if (bounce_) {
            // Bounce List: três modos exclusivos de metadados na saída (a marca d'água sairia do "Original": não existe aqui).
            rotulo(lblModoMeta_, matriz::i18n::t("export.bounce_metadados"));
            const char* chaves[3] = {"export.bounce_original", "export.bounce_embed", "export.bounce_clean"};
            juce::ToggleButton* modos[3] = {&modoOriginal_, &modoEmbed_, &modoClean_};
            for (int i = 0; i < 3; ++i) {
                modos[i]->setButtonText(matriz::i18n::t(chaves[i]));
                modos[i]->setRadioGroupId(7421);
                modos[i]->onClick = [this] { atualizarSidecarHabilitado(); };
                addAndMakeVisible(*modos[i]);
            }
            modoOriginal_.setToggleState(true, juce::dontSendNotification);  // padrão: cópia idêntica à MAIN
            toggleSidecar_.setButtonText(matriz::i18n::t("export.bounce_sidecar"));
            addAndMakeVisible(toggleSidecar_);
            toggleEmbedVideo_.setButtonText(matriz::i18n::t("export.bounce_embed_video"));
            toggleEmbedVideo_.setToggleState(true, juce::dontSendNotification);  // padrão = comportamento anterior
            addAndMakeVisible(toggleEmbedVideo_);
            atualizarSidecarHabilitado();  // estado inicial (Original): toggle de vídeo desabilitado
            lblModoDica_.setText(matriz::i18n::t("export.bounce_clean_dica"), juce::dontSendNotification);
            lblModoDica_.setColour(juce::Label::textColourId, tk.textoTerciario);
            lblModoDica_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            addAndMakeVisible(lblModoDica_);
        } else {
            addAndMakeVisible(toggleEmbutir_);
            addAndMakeVisible(toggleMarca_);
        }

        btnExportar_.setButtonText(matriz::i18n::t("export.btn"));
        btnExportar_.setEnabled(false);
        btnExportar_.onClick = [this] { confirmar(); };
        addAndMakeVisible(btnExportar_);
        btnCancelar_.setButtonText(matriz::i18n::t("dialogo.cancelar"));
        btnCancelar_.onClick = [this] { fechar(); };
        addAndMakeVisible(btnCancelar_);
        setSize(560, bounce ? 632 : 440);
    }

    void paint(juce::Graphics& g) override { g.fillAll(tema().painel); }

    void resized() override {
        auto r = getLocalBounds().reduced(16);
        lblIntro_.setBounds(r.removeFromTop(bounce_ ? 56 : 40));
        r.removeFromTop(6);
        lblPasta_.setBounds(r.removeFromTop(20));
        auto linha = r.removeFromTop(28);
        btnEscolher_.setBounds(linha.removeFromRight(110));
        lblCaminho_.setBounds(linha.withTrimmedRight(8));
        r.removeFromTop(10);
        lblEstrutura_.setBounds(r.removeFromTop(20));
        comboEstrutura_.setBounds(r.removeFromTop(28));
        if (comboMapa_.isVisible()) {
            r.removeFromTop(4);
            comboMapa_.setBounds(r.removeFromTop(28));
        }
        if (btnEditorVisual_.isVisible()) {
            r.removeFromTop(4);
            btnEditorVisual_.setBounds(r.removeFromTop(28));
        }
        r.removeFromTop(10);
        lblNomes_.setBounds(r.removeFromTop(20));
        linha = r.removeFromTop(28);
        comboNomes_.setBounds(linha.removeFromLeft(linha.getWidth() * 2 / 3));
        editPrefixo_.setBounds(linha.withTrimmedLeft(8));
        r.removeFromTop(10);
        if (bounce_) {
            lblModoMeta_.setBounds(r.removeFromTop(20));
            modoOriginal_.setBounds(r.removeFromTop(26));
            modoEmbed_.setBounds(r.removeFromTop(26));
            modoClean_.setBounds(r.removeFromTop(26));
            r.removeFromTop(4);
            toggleSidecar_.setBounds(r.removeFromTop(26));
            toggleEmbedVideo_.setBounds(r.removeFromTop(26));
            lblModoDica_.setBounds(r.removeFromTop(48));
        } else {
            toggleEmbutir_.setBounds(r.removeFromTop(26));
            toggleMarca_.setBounds(r.removeFromTop(26));
        }
        auto botoes = r.removeFromBottom(32);
        btnExportar_.setBounds(botoes.removeFromRight(130));
        botoes.removeFromRight(8);
        btnCancelar_.setBounds(botoes.removeFromRight(110));
    }

private:
    // Mesmo Visual Editor do BACKUP ("ABRIR EDITOR VISUAL"); modal pra não ficar bloqueado atrás deste diálogo.
    void abrirEditorVisual() {
        juce::Component::SafePointer<ConteudoExport> safeThis(this);
        HierarquiaEditorOpcoes op;
        op.titulo = matriz::i18n::t("export.editor_titulo");
        op.raizPreview = "/Export";
        // DialogWindow (não DocumentWindow): mesmo nível de janela do diálogo de EXPORT, então abre NA FRENTE dele.
        HierarquiaEditorWindow::abrirComoDialogo(hierarquiaCustom_, [safeThis](const matriz::consolidacao::HierarquiaBackup& h) {
            if (safeThis != nullptr) safeThis->hierarquiaCustom_ = h;
        }, op, this);
    }

    // Sidecar (.xmp) só em Original e Embed; em Clean fica desligado e desabilitado.
    void atualizarSidecarHabilitado() {
        const bool clean = modoClean_.getToggleState();
        if (clean) toggleSidecar_.setToggleState(false, juce::dontSendNotification);
        toggleSidecar_.setEnabled(!clean);
        toggleEmbedVideo_.setEnabled(modoEmbed_.getToggleState());  // só faz sentido em Embed
    }

    void escolherPasta() {
        chooser_ = std::make_unique<juce::FileChooser>(matriz::i18n::t("export.pasta"), juce::File(), "");
        juce::Component::SafePointer<ConteudoExport> safeThis(this);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis](const juce::FileChooser& fc) {
                                  if (safeThis == nullptr || fc.getResult() == juce::File()) return;
                                  safeThis->destino_ = fc.getResult();
                                  safeThis->lblCaminho_.setText(safeThis->destino_.getFullPathName(), juce::dontSendNotification);
                                  safeThis->btnExportar_.setEnabled(true);
                              });
    }

    void confirmar() {
        using matriz::consolidacao::NivelHierarquia;
        OpcoesExport o;
        o.destino = destino_;
        switch (comboEstrutura_.getSelectedId()) {
            case 2: o.hierarquia = {NivelHierarquia::TipoMidia}; break;
            case 3: o.hierarquia = {NivelHierarquia::Ano}; break;
            case 4: o.hierarquia = {NivelHierarquia::TipoMidia, NivelHierarquia::Ano}; break;
            case 6: o.hierarquia = {NivelHierarquia::EstruturaOriginal}; break;
            case 7: {
                o.hierarquia = {NivelHierarquia::PastaManual};
                o.porMapa = true;
                const int idx = comboMapa_.getSelectedId() - 1;
                if (idx >= 0 && idx < static_cast<int>(mapas_.size())) o.mapaId = mapas_[static_cast<size_t>(idx)].first;
                break;
            }
            case 8:  // Personalizado (editor visual): a hierarquia montada no Visual Editor (vazia = mantém a organização)
                o.hierarquia = hierarquiaCustom_.empty() ? matriz::consolidacao::HierarquiaBackup{NivelHierarquia::PastaManual}
                                                         : hierarquiaCustom_;
                break;
            default: o.hierarquia = {NivelHierarquia::PastaManual}; break;
        }
        const int n = comboNomes_.getSelectedId();
        o.modoPrefixo = n == 2 ? matriz::consolidacao::ModoPrefixoArquivo::Auto
                      : n == 3 ? matriz::consolidacao::ModoPrefixoArquivo::Custom
                               : matriz::consolidacao::ModoPrefixoArquivo::Nenhum;
        o.prefixo = editPrefixo_.getText().trim();
        o.embutir = toggleEmbutir_.getToggleState();
        o.marcaDagua = toggleMarca_.isEnabled() && toggleMarca_.getToggleState();
        if (bounce_) {
            using matriz::consolidacao::ModoMetadadosSaida;
            o.embutir = false;
            o.marcaDagua = false;
            o.metaSaida.modo = modoClean_.getToggleState() ? ModoMetadadosSaida::Clean
                             : modoEmbed_.getToggleState() ? ModoMetadadosSaida::Embed
                                                           : ModoMetadadosSaida::Original;
            o.metaSaida.sidecar = o.metaSaida.modo != ModoMetadadosSaida::Clean && toggleSidecar_.getToggleState();
            o.metaSaida.embedVideo = toggleEmbedVideo_.getToggleState();
        }
        auto cb = aoExportar_;
        fechar();
        if (cb) cb(o);
    }

    void fechar() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
    }

    std::function<void(OpcoesExport)> aoExportar_;
    juce::File destino_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Label lblIntro_, lblPasta_, lblCaminho_, lblEstrutura_, lblNomes_;
    juce::TextButton btnEscolher_, btnExportar_, btnCancelar_;
    juce::ComboBox comboEstrutura_, comboNomes_, comboMapa_;
    std::vector<std::pair<std::string, juce::String>> mapas_;
    juce::TextEditor editPrefixo_;
    juce::ToggleButton toggleEmbutir_, toggleMarca_;
    bool bounce_ = false;
    matriz::consolidacao::HierarquiaBackup hierarquiaCustom_ = matriz::consolidacao::hierarquiaPadrao();
    juce::TextButton btnEditorVisual_;
    juce::Label lblModoMeta_, lblModoDica_;
    juce::ToggleButton modoOriginal_, modoEmbed_, modoClean_, toggleSidecar_, toggleEmbedVideo_;
};

// Janela SETTINGS da Bounce List. Cada mudança é gravada no projeto na hora (bounce_list.json).
class ConteudoBounceAjustes : public juce::Component {
public:
    ConteudoBounceAjustes(const juce::File& pastaProjeto, std::vector<std::pair<std::string, juce::String>> mapas)
        : pasta_(pastaProjeto), mapas_(std::move(mapas)), cfg_(matriz::consolidacao::carregarConfiguracaoBounce(pastaProjeto)) {
        const auto& tk = tema();
        lblEstrutura_.setText(matriz::i18n::t("bounce.estrutura"), juce::dontSendNotification);
        lblEstrutura_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        lblEstrutura_.setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(lblEstrutura_);
        comboEstrutura_.addItem(matriz::i18n::t("bounce.estrutura_mapa"), 1);
        comboEstrutura_.addItem(matriz::i18n::t("bounce.estrutura_editor"), 2);
        comboEstrutura_.setSelectedId(cfg_.porVisualEditor ? 2 : 1, juce::dontSendNotification);
        comboEstrutura_.onChange = [this] { salvar(); atualizarEstrutura(); };
        addAndMakeVisible(comboEstrutura_);
        for (size_t i = 0; i < mapas_.size(); ++i) {
            comboMapa_.addItem(mapas_[i].second, static_cast<int>(i) + 1);
            if (mapas_[i].first == cfg_.mapaId) comboMapa_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
        if (comboMapa_.getSelectedId() == 0 && !mapas_.empty()) comboMapa_.setSelectedId(1, juce::dontSendNotification);
        comboMapa_.onChange = [this] { salvar(); };
        addChildComponent(comboMapa_);
        btnEditorVisual_.setButtonText(matriz::i18n::t("export.abrir_editor_visual"));
        btnEditorVisual_.setColour(juce::TextButton::buttonColourId, tk.botaoSecundario);  // OPEN VISUAL EDITOR: secundário
        btnEditorVisual_.setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnEditorVisual_.onClick = [this] { abrirEditorVisual(); };
        addChildComponent(btnEditorVisual_);

        auto toggle = [&](juce::ToggleButton& t, const char* chave, bool estado) {
            t.setButtonText(matriz::i18n::t(chave));
            t.setToggleState(estado, juce::dontSendNotification);
            addAndMakeVisible(t);
        };
        toggle(tLimpar_, "bounce.cfg_limpar", cfg_.limpar);
        toggle(tEmbutir_, "bounce.cfg_embutir", cfg_.embutir);
        toggle(tSidecar_, "bounce.cfg_sidecar", cfg_.sidecar);
        toggle(tManifesto_, "bounce.cfg_manifesto", cfg_.manifesto);
        tLimpar_.onClick = [this] { if (tLimpar_.getToggleState()) tEmbutir_.setToggleState(false, juce::dontSendNotification); salvar(); };
        tEmbutir_.onClick = [this] { if (tEmbutir_.getToggleState()) tLimpar_.setToggleState(false, juce::dontSendNotification); salvar(); };
        tSidecar_.onClick = [this] { salvar(); };
        tManifesto_.onClick = [this] {
            salvar();
            if (!tManifesto_.getToggleState())
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("bounce.settings_titulo"),
                                                       matriz::i18n::t("bounce.cfg_manifesto_aviso"));
        };
        dica_.setText(matriz::i18n::t("bounce.cfg_dica"), juce::dontSendNotification);
        dica_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        dica_.setColour(juce::Label::textColourId, tk.textoTerciario);
        addAndMakeVisible(dica_);
        btnFechar_.setButtonText(matriz::i18n::t("dialogo.fechar"));
        btnFechar_.onClick = [this] {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
        };
        addAndMakeVisible(btnFechar_);
        atualizarEstrutura();
        setSize(480, 360);
    }
    void paint(juce::Graphics& g) override { g.fillAll(tema().painel); }
    void resized() override {
        auto r = getLocalBounds().reduced(16);
        lblEstrutura_.setBounds(r.removeFromTop(20));
        comboEstrutura_.setBounds(r.removeFromTop(28));
        r.removeFromTop(4);
        if (comboMapa_.isVisible()) comboMapa_.setBounds(r.removeFromTop(28));
        if (btnEditorVisual_.isVisible()) btnEditorVisual_.setBounds(r.removeFromTop(30));
        r.removeFromTop(12);
        for (auto* t : {&tLimpar_, &tEmbutir_, &tSidecar_, &tManifesto_}) t->setBounds(r.removeFromTop(28));
        r.removeFromTop(6);
        dica_.setBounds(r.removeFromTop(36));
        btnFechar_.setBounds(r.removeFromBottom(30).removeFromRight(110));
    }

private:
    void atualizarEstrutura() {
        const bool editor = comboEstrutura_.getSelectedId() == 2;
        comboMapa_.setVisible(!editor && !mapas_.empty());
        btnEditorVisual_.setVisible(editor);
        resized();
    }
    // Mesmo Visual Editor do BACKUP; modal, na frente deste diálogo.
    void abrirEditorVisual() {
        juce::Component::SafePointer<ConteudoBounceAjustes> safeThis(this);
        const auto csv = matriz::consolidacao::carregarConfiguracaoBounce(pasta_).hierarquiaCsv;
        HierarquiaEditorOpcoes op;
        op.titulo = matriz::i18n::t("export.editor_titulo");
        op.raizPreview = "/Export";
        HierarquiaEditorWindow::abrirComoDialogo(csv.empty() ? matriz::consolidacao::hierarquiaPadrao()
                                                             : matriz::consolidacao::hierarquiaDeCsv(csv),
                                                 [safeThis](const matriz::consolidacao::HierarquiaBackup& h) {
            if (safeThis == nullptr) return;
            safeThis->hierarquiaCsv_ = matriz::consolidacao::hierarquiaParaCsv(h);
            safeThis->salvar();
        }, op, this);
    }
    void salvar() {
        cfg_ = matriz::consolidacao::carregarConfiguracaoBounce(pasta_);
        if (hierarquiaCsv_.isNotEmpty()) cfg_.hierarquiaCsv = hierarquiaCsv_.toStdString();
        cfg_.porVisualEditor = comboEstrutura_.getSelectedId() == 2;
        const int im = comboMapa_.getSelectedId() - 1;
        cfg_.mapaId = im >= 0 && im < static_cast<int>(mapas_.size()) ? mapas_[static_cast<size_t>(im)].first : std::string();
        cfg_.limpar = tLimpar_.getToggleState();
        cfg_.embutir = tEmbutir_.getToggleState();
        cfg_.sidecar = tSidecar_.getToggleState();
        cfg_.manifesto = tManifesto_.getToggleState();
        matriz::consolidacao::salvarConfiguracaoBounce(pasta_, cfg_);
    }
    juce::File pasta_;
    std::vector<std::pair<std::string, juce::String>> mapas_;
    juce::String hierarquiaCsv_;
    matriz::consolidacao::ConfiguracaoBounce cfg_;
    juce::Label lblEstrutura_;
    juce::ComboBox comboEstrutura_, comboMapa_;
    juce::TextButton btnEditorVisual_;
    juce::ToggleButton tLimpar_, tEmbutir_, tSidecar_, tManifesto_;
    juce::Label dica_;
    juce::TextButton btnFechar_;
};

// Pacote de collection: nome, pasta de destino e embutir metadados. O folder map NÃO se escolhe aqui: é o do card C.
class ConteudoPacote : public juce::Component {
public:
    using AoGerar = std::function<void(juce::File destino, juce::String nome, bool embutir)>;
    ConteudoPacote(const juce::String& nomePadrao, const juce::String& nomeMapa, AoGerar aoGerar)
        : aoGerar_(std::move(aoGerar)) {
        const auto& tk = tema();
        auto rotulo = [&](juce::Label& l, const juce::String& t) {
            l.setText(t, juce::dontSendNotification);
            l.setColour(juce::Label::textColourId, tk.textoSecundario);
            l.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            addAndMakeVisible(l);
        };
        rotulo(lblIntro_, matriz::i18n::t("export.pacote_intro"));
        lblIntro_.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        rotulo(lblNome_, matriz::i18n::t("export.pacote_nome"));
        editNome_.setText(nomePadrao, juce::dontSendNotification);
        editNome_.onTextChange = [this] { atualizarBotao(); };
        addAndMakeVisible(editNome_);
        rotulo(lblMapa_, matriz::i18n::t("export.pacote_mapa_ativo").replace("{mapa}", nomeMapa));
        toggleEmbutir_.setButtonText(matriz::i18n::t("export.pacote_embutir"));
        addAndMakeVisible(toggleEmbutir_);
        rotulo(lblPasta_, matriz::i18n::t("export.pasta"));
        lblCaminho_.setColour(juce::Label::textColourId, tk.textoPrimario);
        lblCaminho_.setText(matriz::i18n::t("export.pasta_nenhuma"), juce::dontSendNotification);
        addAndMakeVisible(lblCaminho_);
        btnEscolher_.setButtonText(matriz::i18n::t("export.escolher"));
        btnEscolher_.onClick = [this] { escolherPasta(); };
        addAndMakeVisible(btnEscolher_);
        btnGerar_.setButtonText(matriz::i18n::t("export.pacote_btn"));
        btnGerar_.setEnabled(false);
        btnGerar_.onClick = [this] { confirmar(); };
        addAndMakeVisible(btnGerar_);
        btnCancelar_.setButtonText(matriz::i18n::t("dialogo.cancelar"));
        btnCancelar_.onClick = [this] { fechar(); };
        addAndMakeVisible(btnCancelar_);
        setSize(560, 380);
    }

    void paint(juce::Graphics& g) override { g.fillAll(tema().painel); }

    void resized() override {
        auto r = getLocalBounds().reduced(16);
        lblIntro_.setBounds(r.removeFromTop(64));
        r.removeFromTop(6);
        lblNome_.setBounds(r.removeFromTop(20));
        editNome_.setBounds(r.removeFromTop(28));
        r.removeFromTop(10);
        lblMapa_.setBounds(r.removeFromTop(20));
        toggleEmbutir_.setBounds(r.removeFromTop(28));
        r.removeFromTop(10);
        lblPasta_.setBounds(r.removeFromTop(20));
        auto linha = r.removeFromTop(28);
        btnEscolher_.setBounds(linha.removeFromRight(110));
        lblCaminho_.setBounds(linha.withTrimmedRight(8));
        auto botoes = r.removeFromBottom(32);
        btnGerar_.setBounds(botoes.removeFromRight(150));
        botoes.removeFromRight(8);
        btnCancelar_.setBounds(botoes.removeFromRight(110));
    }

private:
    void atualizarBotao() {
        btnGerar_.setEnabled(destino_ != juce::File() && editNome_.getText().trim().isNotEmpty());
    }

    void escolherPasta() {
        chooser_ = std::make_unique<juce::FileChooser>(matriz::i18n::t("export.pasta"), juce::File(), "");
        juce::Component::SafePointer<ConteudoPacote> safeThis(this);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis](const juce::FileChooser& fc) {
                                  if (safeThis == nullptr || fc.getResult() == juce::File()) return;
                                  safeThis->destino_ = fc.getResult();
                                  safeThis->lblCaminho_.setText(safeThis->destino_.getFullPathName(), juce::dontSendNotification);
                                  safeThis->atualizarBotao();
                              });
    }

    void confirmar() {
        auto cb = aoGerar_;
        auto destino = destino_;
        auto nome = editNome_.getText().trim();
        const bool embutir = toggleEmbutir_.getToggleState();
        fechar();
        if (cb) cb(destino, nome, embutir);
    }

    void fechar() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
    }

    AoGerar aoGerar_;
    juce::File destino_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Label lblIntro_, lblNome_, lblMapa_, lblPasta_, lblCaminho_;
    juce::TextEditor editNome_;
    juce::ToggleButton toggleEmbutir_;
    juce::TextButton btnEscolher_, btnGerar_, btnCancelar_;
};

class GoogleDriveIconButton : public juce::Button {
public:
    GoogleDriveIconButton() : juce::Button("GoogleDrive") {
        img_ = juce::ImageFileFormat::loadFrom(AssetsBinaryData::googledrive_png, AssetsBinaryData::googledrive_pngSize);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat();
        
        g.setColour(shouldDrawButtonAsDown ? tk.painelAlt.darker(0.1f)
                    : (shouldDrawButtonAsHighlighted ? tk.painelAlt.brighter(0.15f) : tk.painelAlt));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(shouldDrawButtonAsHighlighted ? juce::Colour(0xff1a73e8) : tk.borda);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);

        if (img_.isValid()) {
            auto iconArea = r.reduced(4.0f);
            g.drawImageWithin(img_, (int)iconArea.getX(), (int)iconArea.getY(),
                              (int)iconArea.getWidth(), (int)iconArea.getHeight(),
                              juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, false);
        } else {
            g.setColour(juce::Colour(0xff1a73e8));
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
            g.drawText("GD", getLocalBounds(), juce::Justification::centred);
        }
    }
private:
    juce::Image img_;
};

class MarkedActionButton : public juce::TextButton {
public:
    MarkedActionButton(const juce::String& prefix, const juce::String& letter, juce::Colour letterColour)
        : prefix_(prefix), letter_(letter), letterColour_(letterColour) {
        setButtonText(prefix_ + " [" + letter_ + "] (0)");
    }

    void setCount(int c) {
        if (count_ != c) {
            count_ = c;
            setButtonText(prefix_ + " [" + letter_ + "] (" + juce::String(count_) + ")");
            repaint();
        }
    }

    int getCount() const { return count_; }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        getLookAndFeel().drawButtonBackground(g, *this,
            findColour(juce::TextButton::buttonColourId),
            shouldDrawButtonAsHighlighted,
            shouldDrawButtonAsDown);

        const auto& tk = tema();
        bool enabled = isEnabled();

        // Tipografia idêntica e unificada nos 3 botões
        juce::Font font(juce::FontOptions(tk.tamanhoFonteCorpo - 0.5f, juce::Font::bold));
        g.setFont(font);

        juce::String strPrefix = prefix_ + " ";
        juce::String strCount = " (" + juce::String(count_) + ")";

        float wPrefix = juce::GlyphArrangement::getStringWidth(font, strPrefix);
        float circleSize = 17.0f;
        float circleGap = 4.0f;
        float wCount = juce::GlyphArrangement::getStringWidth(font, strCount);
        float totalW = wPrefix + circleSize + circleGap + wCount;

        auto r = getLocalBounds().toFloat();
        float startX = (r.getWidth() - totalW) * 0.5f;
        float y = r.getY();
        float h = r.getHeight();

        juce::Colour textCol = enabled ? findColour(juce::TextButton::textColourOffId)
                                       : findColour(juce::TextButton::textColourOffId).withAlpha(0.4f);

        // 1. Prefixo de texto
        g.setColour(textCol);
        g.drawText(strPrefix, (int)std::round(startX), (int)y, (int)std::ceil(wPrefix), (int)h, juce::Justification::centredLeft, false);

        // 2. Círculo preto com letra colorida centralizada
        float circleX = startX + wPrefix;
        float circleY = y + (h - circleSize) * 0.5f;
        juce::Rectangle<float> circleRect(circleX, circleY, circleSize, circleSize);

        g.setColour(juce::Colours::black.withAlpha(enabled ? 0.95f : 0.45f));
        g.fillEllipse(circleRect);

        juce::Colour badgeCol = enabled ? letterColour_ : letterColour_.withAlpha(0.4f);
        g.setColour(badgeCol);
        juce::Font letterFont(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold));
        g.setFont(letterFont);
        g.drawText(letter_, circleRect, juce::Justification::centred, false);

        // 3. Contador numérico
        g.setFont(font);
        g.setColour(textCol);
        g.drawText(strCount, (int)std::round(circleX + circleSize + circleGap), (int)y, (int)std::ceil(wCount), (int)h, juce::Justification::centredLeft, false);
    }

private:
    juce::String prefix_;
    juce::String letter_;
    juce::Colour letterColour_;
    int count_ = 0;
};

void desenharIconeNuvem(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour cor) {
    juce::Path cloud;
    float x = r.getX();
    float y = r.getY();
    float w = r.getWidth();
    float h = r.getHeight();

    cloud.startNewSubPath(x + w * 0.20f, y + h * 0.82f);
    cloud.lineTo(x + w * 0.80f, y + h * 0.82f);
    cloud.cubicTo(x + w * 0.94f, y + h * 0.82f, x + w * 1.00f, y + h * 0.65f, x + w * 0.92f, y + h * 0.52f);
    cloud.cubicTo(x + w * 0.94f, y + h * 0.38f, x + w * 0.80f, y + h * 0.32f, x + w * 0.72f, y + h * 0.36f);
    cloud.cubicTo(x + w * 0.66f, y + h * 0.12f, x + w * 0.36f, y + h * 0.12f, x + w * 0.30f, y + h * 0.34f);
    cloud.cubicTo(x + w * 0.20f, y + h * 0.30f, x + w * 0.06f, y + h * 0.38f, x + w * 0.06f, y + h * 0.56f);
    cloud.cubicTo(x + w * 0.04f, y + h * 0.72f, x + w * 0.12f, y + h * 0.82f, x + w * 0.20f, y + h * 0.82f);
    cloud.closeSubPath();

    g.setColour(cor);
    g.fillPath(cloud);
}

bool ehDestinoNuvem(const juce::String& caminho, const juce::String& rotulo, const std::string& papel) {
    if (papel == "ORIGINAL") return false;
    const juce::String& p = caminho;
    const juce::String& r = rotulo;
    return (p.contains("CloudStorage") ||
            p.containsIgnoreCase("Google Drive") || p.containsIgnoreCase("GoogleDrive") ||
            r.containsIgnoreCase("Google Drive") || r.containsIgnoreCase("GoogleDrive") ||
            p.containsIgnoreCase("Dropbox") || r.containsIgnoreCase("Dropbox") ||
            p.containsIgnoreCase("OneDrive") || r.containsIgnoreCase("OneDrive") ||
            p.containsIgnoreCase("iCloud") || p.containsIgnoreCase("Mobile Documents") || r.containsIgnoreCase("iCloud") ||
            p.containsIgnoreCase("pCloud") || r.containsIgnoreCase("pCloud") ||
            p.containsIgnoreCase("Box Sync") || r.containsIgnoreCase("Box") ||
            r.containsIgnoreCase("Cloud") || r.containsIgnoreCase("Nuvem"));
}

} // namespace

class BackupWorkspaceComponent::PreviaLista : public juce::Component {
public:
    enum class ModoPrioridade {
        Nenhum = 0,
        PriorizarIdenticos = 1,  // 🟢
        PriorizarPendentes = 2,  // 🔴
        PriorizarOrfaos = 3      // ⚪
    };

    struct LinhaExibicao {
        enum class Tipo { Normal, Orfao };
        Tipo tipo = Tipo::Normal;

        // Item Normal
        std::string itemId;
        std::string arquivoId;
        juce::String nomeOriginal;
        juce::String caminhoRelativoDestino;
        bool emConflito = false;
        bool jaConsolidado = false;
        matriz::consolidacao::StatusArquivoBackup status = matriz::consolidacao::StatusArquivoBackup::Vermelho;

        // Item Órfão
        juce::String caminhoRelativoOrfao;
        juce::int64 tamanhoBytesOrfao = 0;
    };

    void definirPlano(const matriz::consolidacao::PlanoConsolidacao& plano) {
        plano_ = &plano;
        isCatalogMode_ = false;
        colecoes_.clear();
        reconstruirLinhas();
    }

    void definirScan(const matriz::consolidacao::ResultadoScanBackup& scan) {
        statusPorItem_.clear();
        for (const auto& isb : scan.itensGrid) {
            statusPorItem_[isb.arquivoId] = isb.status;
            if (!isb.itemId.empty()) statusPorItem_[isb.itemId] = isb.status;
        }
        reconstruirLinhas();
    }

    void definirOrfaos(const std::vector<matriz::consolidacao::ArquivoOrfao>& orfaos,
                       const juce::File& destFolder) {
        orfaos_ = orfaos;
        destFolder_ = destFolder;
        reconstruirLinhas();
    }

    void definirColecoesCatalogo(const std::vector<ProjetoAberto::ColecaoLink>& colecoes,
                                 const juce::File& destFolder) {
        colecoes_ = colecoes;
        destFolder_ = destFolder;
        isCatalogMode_ = true;
        plano_ = nullptr;
        orfaos_.clear();
        reconstruirLinhas();
    }

    void definirModoPrioridade(ModoPrioridade modo) {
        modoPrioridade_ = modo;
        reconstruirLinhas();
    }

    void reconstruirLinhas() {
        linhasExibicao_.clear();
        if (isCatalogMode_) {
            setSize(getWidth(), std::max(60, static_cast<int>(colecoes_.size()) * 36 + 10));
            repaint();
            return;
        }

        if (plano_) {
            for (const auto& item : plano_->itens) {
                LinhaExibicao l;
                l.tipo = LinhaExibicao::Tipo::Normal;
                l.itemId = item.itemId;
                l.arquivoId = item.arquivoId;
                l.nomeOriginal = item.nomeOriginal;
                l.caminhoRelativoDestino = item.caminhoRelativoDestino;
                l.emConflito = item.emConflito;
                l.jaConsolidado = item.jaConsolidado;

                auto itStatus = statusPorItem_.find(item.arquivoId);
                if (itStatus != statusPorItem_.end()) {
                    l.status = itStatus->second;
                } else {
                    itStatus = statusPorItem_.find(item.itemId);
                    if (itStatus != statusPorItem_.end()) {
                        l.status = itStatus->second;
                    } else {
                        l.status = item.jaConsolidado ? matriz::consolidacao::StatusArquivoBackup::Verde : matriz::consolidacao::StatusArquivoBackup::Vermelho;
                    }
                }
                linhasExibicao_.push_back(std::move(l));
            }
        }

        for (const auto& orf : orfaos_) {
            LinhaExibicao l;
            l.tipo = LinhaExibicao::Tipo::Orfao;
            l.caminhoRelativoOrfao = orf.caminhoRelativo;
            l.tamanhoBytesOrfao = orf.tamanhoBytes;
            linhasExibicao_.push_back(std::move(l));
        }

        if (modoPrioridade_ == ModoPrioridade::PriorizarPendentes) {
            std::stable_sort(linhasExibicao_.begin(), linhasExibicao_.end(), [](const LinhaExibicao& a, const LinhaExibicao& b) {
                int pA = (a.tipo == LinhaExibicao::Tipo::Normal && (a.status == matriz::consolidacao::StatusArquivoBackup::Vermelho || a.emConflito)) ? 0 : 1;
                int pB = (b.tipo == LinhaExibicao::Tipo::Normal && (b.status == matriz::consolidacao::StatusArquivoBackup::Vermelho || b.emConflito)) ? 0 : 1;
                return pA < pB;
            });
        } else if (modoPrioridade_ == ModoPrioridade::PriorizarIdenticos) {
            std::stable_sort(linhasExibicao_.begin(), linhasExibicao_.end(), [](const LinhaExibicao& a, const LinhaExibicao& b) {
                int pA = (a.tipo == LinhaExibicao::Tipo::Normal && a.status == matriz::consolidacao::StatusArquivoBackup::Verde && !a.emConflito) ? 0 : 1;
                int pB = (b.tipo == LinhaExibicao::Tipo::Normal && b.status == matriz::consolidacao::StatusArquivoBackup::Verde && !b.emConflito) ? 0 : 1;
                return pA < pB;
            });
        } else if (modoPrioridade_ == ModoPrioridade::PriorizarOrfaos) {
            std::stable_sort(linhasExibicao_.begin(), linhasExibicao_.end(), [](const LinhaExibicao& a, const LinhaExibicao& b) {
                int pA = (a.tipo == LinhaExibicao::Tipo::Orfao) ? 0 : 1;
                int pB = (b.tipo == LinhaExibicao::Tipo::Orfao) ? 0 : 1;
                return pA < pB;
            });
        }

        setSize(getWidth(), std::max(60, static_cast<int>(linhasExibicao_.size()) * 26));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

        if (isCatalogMode_) {
            if (colecoes_.empty()) {
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
                g.drawText(isPt ? juce::String::fromUTF8("Nenhuma coleção vinculada a este catálogo.") : "No collections linked to this catalog.", getLocalBounds(), juce::Justification::centred);
                return;
            }

            int y = 4;
            for (const auto& c : colecoes_) {
                auto linha = juce::Rectangle<int>(8, y, getWidth() - 16, 30);
                g.setColour(tk.painelAlt);
                g.fillRoundedRectangle(linha.toFloat(), 4.0f);
                g.setColour(tk.borda);
                g.drawRoundedRectangle(linha.toFloat(), 4.0f, 1.0f);

                auto r = linha.reduced(8, 0);

                // Badge
                auto badge = juce::Rectangle<int>(r.getX(), r.getY() + 6, 56, 18);
                g.setColour(c.valido ? tk.estadoQcOk : juce::Colour(0xfff97316));
                g.fillRoundedRectangle(badge.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
                g.drawText(c.valido ? "ONLINE" : "OFFLINE", badge, juce::Justification::centred);
                r.removeFromLeft(64);

                // Collection Name
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
                g.drawText(c.nome, r.removeFromLeft(200), juce::Justification::centredLeft, true);

                // Assets & Size
                g.setColour(tk.textoSecundario);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
                juce::String details = juce::String(c.totalAssets) + (isPt ? juce::String::fromUTF8(" itens  |  ") : " assets  |  ") +
                                       juce::File::descriptionOfSizeInBytes(c.totalBytes);
                g.drawText(details, r.removeFromLeft(160), juce::Justification::centredLeft);

                // Target Destination
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
                juce::File raiz = matriz::model::normalizarParaRaizDestino(destFolder_);
                juce::String target = "->  " + raiz.getChildFile("Media").getChildFile(c.nome).getFullPathName();
                g.drawText(target, r, juce::Justification::centredLeft, true);

                y += 34;
            }
            return;
        }

        if (linhasExibicao_.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            g.drawText(isPt ? juce::String::fromUTF8("Nenhum item para backup.") : "No items to backup.", getLocalBounds(), juce::Justification::centred);
            return;
        }

        int y = 0;
        const int itemH = 26;
        for (const auto& linha : linhasExibicao_) {
            juce::Rectangle<int> rLinha(0, y, getWidth(), itemH);

            if (linha.tipo == LinhaExibicao::Tipo::Normal) {
                if (linha.emConflito) {
                    g.setColour(tk.perigo.withAlpha(0.12f));
                    g.fillRect(rLinha);
                } else if (linha.status == matriz::consolidacao::StatusArquivoBackup::Verde) {
                    g.setColour(tk.painelAlt.withAlpha(0.35f));
                    g.fillRect(rLinha);
                }

                auto r = rLinha.reduced(6, 2);
                bool isVerde = (linha.status == matriz::consolidacao::StatusArquivoBackup::Verde);

                // Dot
                auto dotArea = r.removeFromLeft(16).withSizeKeepingCentre(8, 8);
                g.setColour(isVerde ? juce::Colour(0xff22c55e) : juce::Colour(0xffef4444));
                g.fillEllipse(dotArea.toFloat());
                r.removeFromLeft(4);

                // Status badge on the right (with comfortable margin from scrollbar)
                juce::String statusTxt;
                juce::Colour statusCol;
                if (linha.emConflito) {
                    statusTxt = isPt ? juce::String::fromUTF8("CONFLITO") : "CONFLICT";
                    statusCol = tk.perigo;
                } else if (isVerde) {
                    statusTxt = isPt ? juce::String::fromUTF8("Idêntico") : "Up to date";
                    statusCol = juce::Colour(0xff22c55e);
                } else {
                    statusTxt = isPt ? juce::String::fromUTF8("Pendente") : "Pending";
                    statusCol = juce::Colour(0xffef4444);
                }

                r.removeFromRight(20); // Margem para não colidir com scrollbar
                int statusW = 95;
                auto statusArea = r.removeFromRight(statusW);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 0.5f, juce::Font::bold)));
                g.setColour(statusCol);
                g.drawText(statusTxt, statusArea, juce::Justification::centredRight, false);
                r.removeFromRight(12);

                // Main Text: "nomeOriginal  →  caminhoRelativoDestino"
                g.setColour(linha.emConflito ? tk.perigo : (isVerde ? tk.textoSecundario : tk.textoPrimario));
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                juce::String texto = linha.nomeOriginal + juce::String::fromUTF8("  →  ") + linha.caminhoRelativoDestino;
                g.drawText(texto, r, juce::Justification::centredLeft, true);

            } else {
                // Linha Órfão
                auto r = rLinha.reduced(6, 2);

                // Dot ⚪
                auto dotArea = r.removeFromLeft(16).withSizeKeepingCentre(8, 8);
                g.setColour(juce::Colour(0xff9ca3af));
                g.fillEllipse(dotArea.toFloat());
                r.removeFromLeft(4);

                // Size & Status badge on the right
                r.removeFromRight(20);
                juce::String sizeTxt = juce::File::descriptionOfSizeInBytes(linha.tamanhoBytesOrfao);
                int statusW = 95;
                auto statusArea = r.removeFromRight(statusW);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 0.5f)));
                g.setColour(tk.textoTerciario);
                g.drawText(sizeTxt, statusArea, juce::Justification::centredRight, false);
                r.removeFromRight(12);

                // Main text: caminhoRelativo
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                juce::String texto = linha.caminhoRelativoOrfao + (isPt ? juce::String::fromUTF8("  [Órfão no destino]") : "  [Orphan in destination]");
                g.drawText(texto, r, juce::Justification::centredLeft, true);
            }

            y += itemH;
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (isCatalogMode_) return;
        const int itemH = 26;
        int idx = e.y / itemH;
        if (idx >= 0 && idx < static_cast<int>(linhasExibicao_.size())) {
            const auto& linha = linhasExibicao_[static_cast<size_t>(idx)];
            if (linha.tipo == LinhaExibicao::Tipo::Normal) {
                if (!linha.itemId.empty() && aoAbrirNoGrid) {
                    aoAbrirNoGrid({linha.itemId});
                }
            } else if (linha.tipo == LinhaExibicao::Tipo::Orfao) {
                juce::File targetFile;
                if (destFolder_.isDirectory() && linha.caminhoRelativoOrfao.isNotEmpty()) {
                    targetFile = destFolder_.getChildFile(linha.caminhoRelativoOrfao);
                }
                if (targetFile.existsAsFile()) {
                    targetFile.revealToUser();
                } else if (destFolder_.isDirectory()) {
                    destFolder_.revealToUser();
                }
            }
        }
    }

    std::function<void(const std::set<std::string>&)> aoAbrirNoGrid;

private:
    const matriz::consolidacao::PlanoConsolidacao* plano_ = nullptr;
    std::unordered_map<std::string, matriz::consolidacao::StatusArquivoBackup> statusPorItem_;
    std::vector<matriz::consolidacao::ArquivoOrfao> orfaos_;
    std::vector<LinhaExibicao> linhasExibicao_;
    ModoPrioridade modoPrioridade_ = ModoPrioridade::Nenhum;
    std::vector<ProjetoAberto::ColecaoLink> colecoes_;
    juce::File destFolder_;
    bool isCatalogMode_ = false;
};

class BackupWorkspaceComponent::LegendaStatusComponent : public juce::Component {
public:
    LegendaStatusComponent() {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    std::function<void(PreviaLista::ModoPrioridade)> aoMudarFiltro;

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

        auto area = getLocalBounds();
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 0.5f)));

        struct ItemLegenda {
            PreviaLista::ModoPrioridade modo;
            juce::Colour cor;
            juce::String texto;
        };

        std::vector<ItemLegenda> itens = {
            { PreviaLista::ModoPrioridade::PriorizarIdenticos, juce::Colour(0xff22c55e), isPt ? juce::String::fromUTF8("Idêntico (SHA-256)") : "Up to date (SHA-256)" },
            { PreviaLista::ModoPrioridade::PriorizarPendentes, juce::Colour(0xffef4444), isPt ? juce::String::fromUTF8("Pendente / Divergente") : "Pending / Modified" },
            { PreviaLista::ModoPrioridade::PriorizarOrfaos, juce::Colour(0xff9ca3af), isPt ? juce::String::fromUTF8("Órfão no destino") : "Orphan in destination" }
        };

        pillRects_.clear();
        int x = 0;
        for (const auto& it : itens) {
            int textW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), it.texto);
            int pillW = 8 + 8 + 5 + textW + 8;
            auto pillRect = juce::Rectangle<int>(x, 0, pillW, area.getHeight());
            pillRects_.push_back({ pillRect, it.modo });

            bool isSelected = (modoAtivo_ == it.modo);

            if (isSelected) {
                g.setColour(tk.acento.withAlpha(0.20f));
                g.fillRoundedRectangle(pillRect.toFloat(), 4.0f);
                g.setColour(tk.acento);
                g.drawRoundedRectangle(pillRect.toFloat().reduced(0.5f), 4.0f, 1.5f);
            } else {
                g.setColour(tk.painelAlt.withAlpha(0.6f));
                g.fillRoundedRectangle(pillRect.toFloat(), 4.0f);
                g.setColour(tk.borda.withAlpha(0.6f));
                g.drawRoundedRectangle(pillRect.toFloat().reduced(0.5f), 4.0f, 1.0f);
            }

            auto dotRect = juce::Rectangle<float>((float)x + 7.0f, ((float)area.getHeight() - 7.0f) / 2.0f, 7.0f, 7.0f);
            g.setColour(it.cor);
            g.fillEllipse(dotRect);

            g.setColour(isSelected ? tk.textoPrimario : tk.textoSecundario);
            auto textRect = juce::Rectangle<int>(x + 18, 0, textW + 4, area.getHeight());
            g.drawText(it.texto, textRect, juce::Justification::centredLeft, false);

            x += pillW + 8;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        for (const auto& pr : pillRects_) {
            if (pr.rect.contains(e.getPosition())) {
                if (modoAtivo_ == pr.modo) {
                    modoAtivo_ = PreviaLista::ModoPrioridade::Nenhum;
                } else {
                    modoAtivo_ = pr.modo;
                }
                repaint();
                if (aoMudarFiltro) aoMudarFiltro(modoAtivo_);
                break;
            }
        }
    }

    void resetarFiltro() {
        modoAtivo_ = PreviaLista::ModoPrioridade::Nenhum;
        repaint();
    }

private:
    struct PillInfo {
        juce::Rectangle<int> rect;
        PreviaLista::ModoPrioridade modo;
    };
    std::vector<PillInfo> pillRects_;
    PreviaLista::ModoPrioridade modoAtivo_ = PreviaLista::ModoPrioridade::Nenhum;
};

namespace {
// Setas e números do fluxo da aba BACKUP (ASSETS 1 → SELECTION 2 → VERSIONS 3 → STRUCTURE 4 → OPTIONS 5 → botão 6).
// Só desenho: ficam nos vãos entre os cards, nunca por cima de controle.
constexpr int kLarguraNumeroFluxo = 22;  // espaço reservado à esquerda do título de cada etapa

// Estilo "contorno": cards e setas com o mesmo traço; a seta nasce do contorno do card (sem emenda) e é vazada.
juce::Colour corContornoFluxo() {
    // Claro: contorno #BFB7AA (setas "embutidas" nos cards, preenchimento igual ao card); escuro: como antes.
    return tema().fundo.getBrightness() > 0.5f ? juce::Colour(0xffbfb7aa) : tema().textoSecundario.withAlpha(0.6f);
}
constexpr float kTracoFluxo = 1.5f;

void desenharCartaoFluxo(juce::Graphics& g, juce::Rectangle<int> cartao) {
    const auto& tk = tema();
    g.setColour(tk.painel);
    g.fillRoundedRectangle(cartao.toFloat(), tk.raioCartao);
    g.setColour(corContornoFluxo());
    g.drawRoundedRectangle(cartao.toFloat().reduced(kTracoFluxo * 0.5f), tk.raioCartao, kTracoFluxo);
}

// Seta vazada horizontal saindo da borda direita de `cartao` (o contorno do card se abre na haste) até xPonta.
void desenharSetaVazadaSaindoDaDireita(juce::Graphics& g, juce::Rectangle<int> cartao, float y, float xPonta) {
    const float x0 = static_cast<float>(cartao.getRight()) - kTracoFluxo;
    const float haste = 6.0f, cabeca = 13.0f, compCabeca = std::min(16.0f, (xPonta - x0) * 0.5f);
    if (xPonta - x0 < 12.0f) return;
    juce::Path contorno;  // aberto do lado do card
    contorno.startNewSubPath(x0, y - haste);
    contorno.lineTo(xPonta - compCabeca, y - haste);
    contorno.lineTo(xPonta - compCabeca, y - cabeca);
    contorno.lineTo(xPonta, y);
    contorno.lineTo(xPonta - compCabeca, y + cabeca);
    contorno.lineTo(xPonta - compCabeca, y + haste);
    contorno.lineTo(x0, y + haste);
    juce::Path preenchimento(contorno);
    preenchimento.closeSubPath();
    g.setColour(tema().painel);
    g.fillPath(preenchimento);  // cobre também o trecho do contorno do card onde a haste se abre
    g.setColour(corContornoFluxo());
    g.strokePath(contorno, juce::PathStrokeType(kTracoFluxo, juce::PathStrokeType::mitered));
}

// A mesma seta do ASSETS, saindo da borda ESQUERDA de `cartao` e apontando para a esquerda até xPonta.
void desenharSetaVazadaSaindoDaEsquerda(juce::Graphics& g, juce::Rectangle<int> cartao, float y, float xPonta) {
    const float x0 = static_cast<float>(cartao.getX()) + kTracoFluxo;
    if (x0 - xPonta < 12.0f) return;
    const float haste = 6.0f, cabeca = 13.0f, compCabeca = std::min(16.0f, (x0 - xPonta) * 0.5f);
    juce::Path contorno;  // aberto do lado do card
    contorno.startNewSubPath(x0, y - haste);
    contorno.lineTo(xPonta + compCabeca, y - haste);
    contorno.lineTo(xPonta + compCabeca, y - cabeca);
    contorno.lineTo(xPonta, y);
    contorno.lineTo(xPonta + compCabeca, y + cabeca);
    contorno.lineTo(xPonta + compCabeca, y + haste);
    contorno.lineTo(x0, y + haste);
    juce::Path preenchimento(contorno);
    preenchimento.closeSubPath();
    g.setColour(tema().painel);
    g.fillPath(preenchimento);
    g.setColour(corContornoFluxo());
    g.strokePath(contorno, juce::PathStrokeType(kTracoFluxo, juce::PathStrokeType::mitered));
}

// A mesma seta do ASSETS, na vertical: nasce da borda de baixo de `cartao` (o contorno se abre na haste) e aponta
// para baixo até yPonta.
void desenharSetaVazadaSaindoDeBaixo(juce::Graphics& g, juce::Rectangle<int> cartao, float x, float yPonta) {
    const float y0 = static_cast<float>(cartao.getBottom()) - kTracoFluxo;
    if (yPonta - y0 < 12.0f) return;
    const float haste = 6.0f, cabeca = 13.0f, compCabeca = std::min(16.0f, (yPonta - y0) * 0.5f);
    juce::Path contorno;  // aberto do lado do card
    contorno.startNewSubPath(x - haste, y0);
    contorno.lineTo(x - haste, yPonta - compCabeca);
    contorno.lineTo(x - cabeca, yPonta - compCabeca);
    contorno.lineTo(x, yPonta);
    contorno.lineTo(x + cabeca, yPonta - compCabeca);
    contorno.lineTo(x + haste, yPonta - compCabeca);
    contorno.lineTo(x + haste, y0);
    juce::Path preenchimento(contorno);
    preenchimento.closeSubPath();
    g.setColour(tema().painel);
    g.fillPath(preenchimento);  // cobre também o trecho do contorno do card onde a haste se abre
    g.setColour(corContornoFluxo());
    g.strokePath(contorno, juce::PathStrokeType(kTracoFluxo, juce::PathStrokeType::mitered));
}

// Etapa do fluxo identificada por letra: 1 -> A, 2 -> B, ...
void desenharNumeroFluxo(juce::Graphics& g, juce::Rectangle<int> area, int numero) {
    if (area.isEmpty()) return;
    const auto c = area.toFloat().withSizeKeepingCentre(18.0f, 18.0f);
    g.setColour(tema().acento);
    g.fillEllipse(c);
    g.setColour(tema().textoSobreAcento);
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(L'A' + numero - 1)), c, juce::Justification::centred, false);
}
}  // namespace

// Uma etapa do Make Backup (COPY ou CATALOG), cada uma num card próprio com título, fase, contador "n / total" e a
// barra no mesmo desenho da barra do rodapé (trilho painelAlt, preenchimento de acento, % no centro).
class EtapaProgressoComponent : public juce::Component, private juce::Timer {
public:
    explicit EtapaProgressoComponent(juce::String titulo) : titulo_(std::move(titulo)) {}
    ~EtapaProgressoComponent() override { stopTimer(); }

    // Aguardando a etapa anterior: barra vazia, sem animação.
    void esperar(const juce::String& fase) { definirEstado(Estado::Espera, 0.0, fase, 0, 0); }
    // fracao < 0 = sem contagem (animação); total > 0 mostra o contador.
    void definir(double fracao, const juce::String& fase, int feito = 0, int total = 0) {
        definirEstado(Estado::Andando, fracao, fase, feito, total);
    }
    void concluir() { definirEstado(Estado::Concluido, 1.0, juce::String(), 0, 0); }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat();
        g.setColour(tk.painelAlt.withAlpha(0.25f));
        g.fillRoundedRectangle(r, tk.raioPequeno);
        g.setColour(tk.borda.withAlpha(0.7f));
        g.drawRoundedRectangle(r.reduced(0.5f), tk.raioPequeno, 1.0f);

        auto area = getLocalBounds().reduced(12, 8);
        auto linha = area.removeFromTop(18);
        const bool concluido = estado_ == Estado::Concluido;
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
        g.setColour(concluido ? juce::Colour(0xff22c55e) : (estado_ == Estado::Espera ? tk.textoTerciario : tk.acento));
        const int larguraTitulo = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), titulo_) + 12;
        g.drawText(titulo_, linha.removeFromLeft(larguraTitulo), juce::Justification::centredLeft);
        if (total_ > 0) {
            g.setColour(tk.textoPrimario);
            g.drawText(juce::String(feito_) + " / " + juce::String(total_), linha.removeFromRight(110), juce::Justification::centredRight);
        }
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        g.setColour(tk.textoSecundario);
        g.drawText(concluido ? matriz::i18n::t("backup.etapa_concluida") : fase_, linha, juce::Justification::centredLeft, true);

        area.removeFromTop(6);
        const auto barra = area.removeFromTop(14).toFloat();
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(barra, 4.0f);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(barra, 4.0f, 1.0f);
        const juce::Colour preenchimento = concluido ? juce::Colour(0xff22c55e) : tk.acento;
        if (estado_ == Estado::Andando && fracao_ < 0.0) {
            const float largura = 40.0f;
            const float x = barra.getX() + (barra.getWidth() - largura) * brilho_;
            g.setColour(tk.acento.withAlpha(0.7f));
            g.fillRoundedRectangle(juce::Rectangle<float>(x, barra.getY(), largura, barra.getHeight()), 4.0f);
        } else if (estado_ != Estado::Espera) {
            const float w = barra.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, fracao_));
            if (w > 0.5f) {
                g.setColour(preenchimento);
                g.fillRoundedRectangle(juce::Rectangle<float>(barra.getX(), barra.getY(), w, barra.getHeight()), 4.0f);
            }
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
            g.setColour(tk.textoPrimario);
            g.drawText(juce::String(static_cast<int>(std::round(juce::jlimit(0.0, 1.0, fracao_) * 100.0))) + "%", barra,
                       juce::Justification::centred, true);
        }
    }

private:
    enum class Estado { Espera, Andando, Concluido };

    void definirEstado(Estado e, double fracao, const juce::String& fase, int feito, int total) {
        estado_ = e;
        fracao_ = fracao;
        fase_ = fase;
        feito_ = feito;
        total_ = total;
        if (e == Estado::Andando && fracao < 0.0) { if (!isTimerRunning()) startTimerHz(25); }
        else stopTimer();
        repaint();
    }
    void timerCallback() override {
        brilho_ += 0.04f;
        if (brilho_ > 1.0f) brilho_ -= 1.0f;
        repaint();
    }

    juce::String titulo_, fase_;
    Estado estado_ = Estado::Espera;
    double fracao_ = 0.0;
    int feito_ = 0, total_ = 0;
    float brilho_ = 0.0f;
};

class BackupWorkspaceComponent::ConfigContainerComponent : public juce::Component {
public:
    explicit ConfigContainerComponent(BackupWorkspaceComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        if (!fluxo_) {
            for (const auto& cartao : cartoes_) {
                g.setColour(tk.painel);
                g.fillRoundedRectangle(cartao.toFloat(), tk.raioMedio);
                g.setColour(tk.borda);
                g.drawRoundedRectangle(cartao.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
            }
            return;
        }
        // Fluxo: cards em contorno; de um para o outro, a mesma seta vazada do ASSETS saindo da borda de baixo,
        // perto da borda direita (rolam junto). Números 2-5 antes dos títulos.
        for (const auto& cartao : cartoes_) desenharCartaoFluxo(g, cartao);
        const float x = static_cast<float>(getWidth() - 34);
        for (size_t i = 0; i + 1 < cartoes_.size(); ++i)
            desenharSetaVazadaSaindoDeBaixo(g, cartoes_[i], x, static_cast<float>(cartoes_[i + 1].getY()) - 2.0f);
        for (size_t i = 0; i < numeros_.size(); ++i) desenharNumeroFluxo(g, numeros_[i], static_cast<int>(i) + 2);
    }

    // As setas ASSETS -> SELECTION e OPTIONS -> botão são do dono: acompanham a rolagem da coluna.
    void moved() override { owner_.repaint(); }
    const std::vector<juce::Rectangle<int>>& cartoes() const { return cartoes_; }

    // Altura que a coluna precisa para mostrar TODOS os controles de todos os cards (nada é cortado: se a janela for
    // menor, a coluna rola). Mesmas medidas do resized().
    int alturaNecessaria() const {
        const bool isCatalogMode = (owner_.projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
        const int padCartaoY = 6, alturaControle = 32, alturaLinhaToggle = 24, alturaCabecalhoSecao = 22, alturaRotuloPrefixo = 20;
        const int numCartoes = isCatalogMode ? 2 : 4;
        const int gap = isCatalogMode ? 8 : 14;
        int soma = 0;
        if (isCatalogMode) {
            soma += alturaCabecalhoSecao + 2 + 36 + 4 + alturaControle + 2 + 20 + padCartaoY * 2;
            soma += alturaCabecalhoSecao + 2 + (alturaLinhaToggle * 2) + padCartaoY * 2;
        } else {
            const bool temColecoes = (owner_.comboColecoes_ && owner_.comboColecoes_->isVisible());
            const bool temEditarSelecao = (owner_.btnEditarSelecao_ && owner_.btnEditarSelecao_->isVisible());
            const bool temEditorHierarquia = (owner_.btnEditarHierarquia_ && owner_.btnEditarHierarquia_->isVisible());
            const bool temEditCustomPrefixo = (owner_.editPrefixo_ && owner_.editPrefixo_->isVisible());
            const int extraH1 = (temColecoes || temEditarSelecao) ? (4 + alturaControle) : 0;
            soma += alturaCabecalhoSecao + 2 + alturaControle + extraH1 + padCartaoY * 2;
            soma += alturaCabecalhoSecao + 2 + (alturaLinhaToggle * 2) + 3 + alturaControle + (3 + alturaControle) +
                    (temEditorHierarquia ? (3 + alturaControle) : 0) + (4 + alturaRotuloPrefixo + alturaControle) +
                    (temEditCustomPrefixo ? (3 + alturaControle) : 0) + padCartaoY * 2;
            soma += alturaCabecalhoSecao + 2 + (alturaLinhaToggle * 5) + padCartaoY * 2;  // OPTIONS: até 5 linhas (nenhuma é cortada)
            soma += alturaCabecalhoSecao + 4 + 40 + padCartaoY * 2;                        // RUN
        }
        return soma + (numCartoes - 1) * gap;
    }

    void resized() override {
        const auto& tk = tema();
        cartoes_.clear();

        int largura = getWidth();
        int totalH = getHeight();
        if (largura <= 0 || totalH <= 0) return;

        bool isCatalogMode = (owner_.projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

        const int padCartaoX = 10;
        const int padCartaoY = 6;
        const int alturaControle = 32;       // fonte 14 px: campos/combos/botões mais altos (antes 26)
        const int alturaLinhaToggle = 24;    // (antes 22)
        const int alturaCabecalhoSecao = 22;  // (antes 20)
        const int alturaRotuloPrefixo = 20;   // "FILE NAMING / PREFIX": rótulo em linha própria, combo com a largura toda
        const int numCartoes = isCatalogMode ? 2 : 4;
        fluxo_ = !isCatalogMode;  // setas/números do fluxo só na Coleção (os 4 cards)
        numeros_.clear();
        const int gapCartoes = fluxo_ ? 14 : 8;  // vão curto: seta vazada encurtada (cabe os 4 cards sem rolar)
        const int espacosTotais = (numCartoes - 1) * gapCartoes;
        const int availableForCards = totalH - espacosTotais;

        int card1H = 0, card2H = 0, card3H = 0, card4H = 0;

        if (isCatalogMode) {
            int minH2 = alturaCabecalhoSecao + 2 + 36 + 4 + alturaControle + 2 + 20 + padCartaoY * 2;
            int minH4 = alturaCabecalhoSecao + 2 + (alturaLinhaToggle * 2) + padCartaoY * 2;
            int minTotal = minH2 + minH4;

            if (availableForCards >= minTotal) {
                int extra = availableForCards - minTotal;
                int extra2 = static_cast<int>(extra * 0.55f);
                int extra4 = extra - extra2;
                card2H = minH2 + extra2;
                card4H = minH4 + extra4;
            } else {
                float ratio = static_cast<float>(availableForCards) / static_cast<float>(std::max(1, minTotal));
                card2H = std::max(60, static_cast<int>(minH2 * ratio));
                card4H = std::max(40, availableForCards - card2H);
            }
        } else {
            bool temColecoes = (owner_.comboColecoes_ && owner_.comboColecoes_->isVisible());
            bool temEditarSelecao = (owner_.btnEditarSelecao_ && owner_.btnEditarSelecao_->isVisible());
            bool temEditorHierarquia = (owner_.btnEditarHierarquia_ && owner_.btnEditarHierarquia_->isVisible());
            bool temEditCustomPrefixo = (owner_.editPrefixo_ && owner_.editPrefixo_->isVisible());
            int extraH1 = (temColecoes || temEditarSelecao) ? (4 + alturaControle) : 0;
            // Coleção: SELECTION, FOLDER STRUCTURE, OPTIONS e RUN (o BACKUP VERSIONS fica no rodapé, card 6).
            int minH1 = alturaCabecalhoSecao + 2 + alturaControle + extraH1 + padCartaoY * 2;
            int minH3 = alturaCabecalhoSecao + 2 + (alturaLinhaToggle * 2) + 3 + alturaControle + (3 + alturaControle) + (temEditorHierarquia ? (3 + alturaControle) : 0) + (4 + alturaRotuloPrefixo + alturaControle) + (temEditCustomPrefixo ? (3 + alturaControle) : 0) + padCartaoY * 2;
            int minH4 = alturaCabecalhoSecao + 2 + (alturaLinhaToggle * 5) + padCartaoY * 2;
            const int hRun = alturaCabecalhoSecao + 4 + 40 + padCartaoY * 2;  // título + botão principal
            int minTotal = minH1 + minH3 + minH4 + hRun;

            if (availableForCards >= minTotal) {
                int extra = availableForCards - minTotal;
                card1H = minH1 + static_cast<int>(extra * 0.05f);
                card3H = minH3 + static_cast<int>(extra * 0.10f);
                card4H = availableForCards - card1H - card3H - hRun;  // OPTIONS fica com a sobra
            } else {
                float ratio = static_cast<float>(std::max(1, availableForCards - hRun)) / static_cast<float>(std::max(1, minTotal - hRun));
                card1H = std::max(46, static_cast<int>(minH1 * ratio));
                card3H = minH3;  // nunca encolhe abaixo do conteúdo (antes o combo de prefixo era cortado)
                card4H = std::max(60, availableForCards - hRun - card1H - card3H);
            }
        }

        // Linha do título: com o fluxo, o número da etapa ocupa o começo e o título vem logo depois.
        auto linhaTitulo = [&](juce::Rectangle<int> linha) {
            if (fluxo_) numeros_.push_back(linha.removeFromLeft(kLarguraNumeroFluxo));
            return linha;
        };

        int currY = 0;
        auto abrirCartao = [&](int altura) {
            auto cartao = juce::Rectangle<int>(0, currY, largura, altura);
            cartoes_.push_back(cartao);
            currY += altura + gapCartoes;
            return cartao.reduced(padCartaoX, padCartaoY);
        };

        // 1. SOURCE (Collection Mode only)
        if (!isCatalogMode && owner_.comboSource_) {
            auto dentro = abrirCartao(card1H);
            if (owner_.labelSource_) owner_.labelSource_->setBounds(linhaTitulo(dentro.removeFromTop(alturaCabecalhoSecao)));
            dentro.removeFromTop(2);
            owner_.comboSource_->setBounds(dentro.removeFromTop(alturaControle));
            if (owner_.comboColecoes_ && owner_.comboColecoes_->isVisible()) {
                dentro.removeFromTop(4);
                owner_.comboColecoes_->setBounds(dentro.removeFromTop(alturaControle));
            }
            if (owner_.btnEditarSelecao_ && owner_.btnEditarSelecao_->isVisible()) {
                dentro.removeFromTop(4);
                owner_.btnEditarSelecao_->setBounds(dentro.removeFromTop(alturaControle));
            }
        }

        // 3. ORGANIZATION (Collection Mode only)
        if (!isCatalogMode && owner_.comboOrg_) {
            auto dentro = abrirCartao(card3H);
            if (owner_.labelOrg_) owner_.labelOrg_->setBounds(linhaTitulo(dentro.removeFromTop(alturaCabecalhoSecao)));
            dentro.removeFromTop(2);
            if (owner_.togglePreservarEstrutura_) owner_.togglePreservarEstrutura_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            if (owner_.toggleUsarEstruturaMapa_) owner_.toggleUsarEstruturaMapa_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            if (owner_.comboMapaMain_) {
                dentro.removeFromTop(3);
                owner_.comboMapaMain_->setBounds(dentro.removeFromTop(alturaControle));
            }
            dentro.removeFromTop(3);
            owner_.comboOrg_->setBounds(dentro.removeFromTop(alturaControle));
            if (owner_.btnEditarHierarquia_ && owner_.btnEditarHierarquia_->isVisible()) {
                dentro.removeFromTop(3);
                owner_.btnEditarHierarquia_->setBounds(dentro.removeFromTop(alturaControle));
            }
            if (owner_.comboModoPrefixo_) {
                dentro.removeFromTop(4);
                if (owner_.labelPrefixo_) owner_.labelPrefixo_->setBounds(dentro.removeFromTop(alturaRotuloPrefixo));
                auto linhaPrefixo = dentro.removeFromTop(alturaControle);
                owner_.comboModoPrefixo_->setBounds(linhaPrefixo);
                if (owner_.editPrefixo_ && owner_.editPrefixo_->isVisible()) {
                    dentro.removeFromTop(3);
                    owner_.editPrefixo_->setBounds(dentro.removeFromTop(alturaControle));
                }
            }
        }

        // 4. OPTIONS (ambos os modos) — antes de BACKUP VERSIONS na ordem do fluxo
        if (owner_.toggleVerificarChecksum_) {
            auto dentro = abrirCartao(card4H);
            if (owner_.labelOpcoes_) owner_.labelOpcoes_->setBounds(linhaTitulo(dentro.removeFromTop(alturaCabecalhoSecao)));
            dentro.removeFromTop(2);
            owner_.toggleVerificarChecksum_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            if (owner_.toggleGerarCatalogo_) owner_.toggleGerarCatalogo_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            if (!isCatalogMode) {
                if (owner_.btnAtualizarSidecars_ && owner_.btnAtualizarSidecars_->isVisible()) {
                    owner_.btnAtualizarSidecars_->setBounds(
                        dentro.removeFromTop(alturaLinhaToggle).reduced(0, 3).removeFromLeft(220));
                }
                if (owner_.toggleAutoResolverConflitos_) {
                    owner_.toggleAutoResolverConflitos_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
                }
                if (owner_.toggleForcarRebackup_ && owner_.toggleForcarRebackup_->isVisible()) {
                    owner_.toggleForcarRebackup_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
                }
            }
        }

        // BACKUP VERSIONS (DESTINATION) no Catálogo — último da coluna, com o resto da altura. Na Coleção ele é o
        // card (6), no rodapé (ver BackupWorkspaceComponent::resized).
        if (owner_.listVaults_ && !fluxo_) {
            auto dentro = abrirCartao(std::max(60, totalH - currY));
            if (owner_.labelDest_) owner_.labelDest_->setBounds(linhaTitulo(dentro.removeFromTop(alturaCabecalhoSecao)));
            dentro.removeFromTop(2);

            int alturaDestInfo = 20;
            int reservedBottom = alturaControle + 4 + alturaDestInfo;
            int listH = std::max(24, dentro.getHeight() - reservedBottom - 4);
            owner_.listVaults_->setBounds(dentro.removeFromTop(listH));
            dentro.removeFromTop(4);
            // (o MAIN EDIT MODE mora no menu Tools — ver MainComponent::alternarModoEdicaoMain)

            // Browse and Google Drive buttons side by side
            {
                auto linhaBrowse = dentro.removeFromTop(alturaControle);
                int btnGdW = 32;
                if (owner_.btnGoogleDriveDest_) {
                    owner_.btnGoogleDriveDest_->setBounds(linhaBrowse.removeFromRight(btnGdW));
                    linhaBrowse.removeFromRight(tk.espacoPequeno);
                }
                if (owner_.btnBrowseVault_) owner_.btnBrowseVault_->setBounds(linhaBrowse);
            }
            dentro.removeFromTop(2);
            if (owner_.labelDestInfo_) owner_.labelDestInfo_->setBounds(dentro.removeFromTop(std::min(alturaDestInfo, dentro.getHeight())));
        }


        // 5. RUN (Coleção): título + botão principal (e o REBUILD STATUS, quando houver), depois de OPTIONS.
        if (fluxo_ && owner_.btnStartBackup_) {
            auto dentro = abrirCartao(std::max(60, totalH - currY));
            if (owner_.labelAcao_) owner_.labelAcao_->setBounds(linhaTitulo(dentro.removeFromTop(alturaCabecalhoSecao)));
            dentro.removeFromTop(4);
            auto linha = dentro.removeFromTop(std::min(40, dentro.getHeight()));
            if (owner_.btnRebuildStatus_ && owner_.btnRebuildStatus_->isVisible()) {
                owner_.btnRebuildStatus_->setBounds(linha.removeFromLeft(std::min(190, linha.getWidth() / 2)));
                linha.removeFromLeft(tk.espacoPainel);
            }
            owner_.btnStartBackup_->setBounds(linha);
        }

        alturaCalculada_ = cartoes_.empty() ? totalH : cartoes_.back().getBottom();
        repaint();
    }

    int calcularAlturaNecessaria() const {
        return alturaCalculada_;
    }

private:
    BackupWorkspaceComponent& owner_;
    std::vector<juce::Rectangle<int>> cartoes_;
    std::vector<juce::Rectangle<int>> numeros_;  // número de cada etapa, na ordem dos cards
    bool fluxo_ = false;
    int alturaCalculada_ = 500;
};

class BackupWorkspaceComponent::CatalogBackupContainerComponent : public juce::Component {
public:
    CatalogBackupContainerComponent(BackupWorkspaceComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        bool isLight = (tk.fundo.getBrightness() > 0.5f);
        juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
        g.fillAll(bg);
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

        int y = 20;
        int w = getWidth() - 32;

        // 1. Global Backup Summary (Top 4 KPI Cards)
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
        g.drawText(isPt ? juce::String::fromUTF8("RESUMO GLOBAL DO BACKUP") : "GLOBAL BACKUP SUMMARY", 16, y, w, 22, juce::Justification::left);
        y += 28;

        int cardW = (w - 36) / 4;
        int cardH = 70;
        int cx = 16;

        auto drawCard = [&](const juce::String& title, const juce::String& val, const juce::String& sub, juce::Colour color) {
            auto r = juce::Rectangle<int>(cx, y, cardW, cardH);
            g.setColour(tk.painel);
            g.fillRoundedRectangle(r.toFloat(), 6.0f);
            auto strip = r.removeFromLeft(4);
            g.setColour(color);
            g.fillRoundedRectangle(strip.toFloat(), 3.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.0f);

            auto inner = r.reduced(10, 6);
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
            g.drawText(title, inner.removeFromTop(14), juce::Justification::left);

            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
            g.drawText(val, inner.removeFromTop(20), juce::Justification::left);

            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
            g.drawText(sub, inner, juce::Justification::left);

            cx += cardW + 12;
        };

        drawCard(isPt ? juce::String::fromUTF8("DADOS TOTAIS") : "TOTAL DATA",
                 juce::File::descriptionOfSizeInBytes(owner_.catalogBackupTotal_.sizeBytes),
                 juce::String(owner_.catalogBackupTotal_.totalAssets) + (isPt ? juce::String::fromUTF8(" itens") : " assets"), juce::Colour(0xff3b82f6));
        drawCard(isPt ? juce::String::fromUTF8("COM BACKUP") : "BACKED UP",
                 juce::File::descriptionOfSizeInBytes(owner_.catalogBackupTotal_.sizeBytes),
                 isPt ? juce::String::fromUTF8("100% protegido") : "100% protected", juce::Colour(0xff10b981));
        drawCard(isPt ? juce::String::fromUTF8("AUSENTES") : "MISSING",
                 "0 B", isPt ? juce::String::fromUTF8("0 itens ausentes") : "0 assets missing", juce::Colour(0xff64748b));
        drawCard(isPt ? juce::String::fromUTF8("REQUER ATENÇÃO") : "NEEDS ATTENTION",
                 juce::String(owner_.catalogBackupTotal_.needsAttention),
                 isPt ? juce::String::fromUTF8("itens para revisar") : "items need review",
                 owner_.catalogBackupTotal_.needsAttention > 0 ? juce::Colour(0xfff97316) : tk.textoSecundario);

        y += cardH + 24;

        // 2. Collections Overview Section Header
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
        g.drawText(isPt ? juce::String::fromUTF8("STATUS DO BACKUP DAS COLEÇÕES") : "COLLECTIONS BACKUP STATUS", 16, y, w, 22, juce::Justification::left);
        y += 28;

        // Table Header
        auto headerRect = juce::Rectangle<int>(16, y, w, 28);
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(headerRect.toFloat(), 4.0f);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));

        int col1W = 260;
        int col2W = 120;
        int col3W = 110;
        int col4W = 120;
        
        auto hInner = headerRect.reduced(12, 0);
        g.drawText(isPt ? juce::String::fromUTF8("COLEÇÃO") : "COLLECTION", hInner.removeFromLeft(col1W), juce::Justification::centredLeft);
        g.drawText(isPt ? juce::String::fromUTF8("TAMANHO EM DISCO") : "STORAGE SIZE", hInner.removeFromLeft(col2W), juce::Justification::centredLeft);
        g.drawText(isPt ? juce::String::fromUTF8("ITENS") : "ASSETS", hInner.removeFromLeft(col3W), juce::Justification::centredLeft);
        g.drawText("STATUS", hInner.removeFromLeft(col4W), juce::Justification::centredLeft);
        g.drawText(isPt ? juce::String::fromUTF8("LOCALIZAÇÃO") : "LOCATION", hInner, juce::Justification::centredLeft);

        y += 32;

        if (owner_.catalogBackupItems_.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
            g.drawText(isPt ? juce::String::fromUTF8("Nenhuma coleção vinculada a este catálogo ainda.") : "No collections linked to this catalog yet.", 16, y, w, 30, juce::Justification::left);
            return;
        }

        // Collection Rows
        for (const auto& item : owner_.catalogBackupItems_) {
            auto rowRect = juce::Rectangle<int>(16, y, w, 36);
            g.setColour(tk.painel);
            g.fillRoundedRectangle(rowRect.toFloat(), 4.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(rowRect.toFloat(), 4.0f, 1.0f);

            auto rInner = rowRect.reduced(12, 0);
            
            // Name
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
            g.drawText(item.name, rInner.removeFromLeft(col1W), juce::Justification::centredLeft, true);

            // Size
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
            g.drawText(juce::File::descriptionOfSizeInBytes(item.sizeBytes), rInner.removeFromLeft(col2W), juce::Justification::centredLeft);

            // Assets
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
            g.drawText(juce::String(item.totalAssets), rInner.removeFromLeft(col3W), juce::Justification::centredLeft);

            // Status Badge
            auto badgeArea = rInner.removeFromLeft(col4W);
            auto badge = juce::Rectangle<int>(badgeArea.getX(), badgeArea.getY() + 8, 70, 20);
            juce::Colour badgeCol = (item.status == "READY") ? tk.estadoQcOk : (item.status == "WARNING" ? tk.alerta : tk.perigo);
            g.setColour(badgeCol);
            g.fillRoundedRectangle(badge.toFloat(), 3.0f);
            g.setColour(tk.textoSobreAcento);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
            juce::String statusTxt = (item.status == "READY") ? (isPt ? juce::String::fromUTF8("PRONTO") : "READY") : item.status;
            g.drawText(statusTxt, badge, juce::Justification::centred);

            // Path
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
            g.drawText(item.path, rInner, juce::Justification::centredLeft, true);

            y += 40;
        }

        // 3. Catalog Total Row
        y += 8;
        auto totalRow = juce::Rectangle<int>(16, y, w, 40);
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(totalRow.toFloat(), 4.0f);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(totalRow.toFloat(), 4.0f, 1.5f);

        auto tInner = totalRow.reduced(12, 0);
        g.setColour(tk.acento);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(isPt ? juce::String::fromUTF8("TOTAL DO CATÁLOGO") : "CATALOG TOTAL", tInner.removeFromLeft(col1W), juce::Justification::centredLeft);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(juce::File::descriptionOfSizeInBytes(owner_.catalogBackupTotal_.sizeBytes), tInner.removeFromLeft(col2W), juce::Justification::centredLeft);
        g.drawText(juce::String(owner_.catalogBackupTotal_.totalAssets), tInner.removeFromLeft(col3W), juce::Justification::centredLeft);

        auto badgeArea = tInner.removeFromLeft(col4W);
        auto badge = juce::Rectangle<int>(badgeArea.getX(), badgeArea.getY() + 9, 70, 22);
        g.setColour(tk.estadoQcOk);
        g.fillRoundedRectangle(badge.toFloat(), 3.0f);
        g.setColour(tk.textoSobreAcento);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
        g.drawText(isPt ? juce::String::fromUTF8("PRONTO") : "READY", badge, juce::Justification::centred);
    }

    void recalculateHeight() {
        int h = 260 + static_cast<int>(owner_.catalogBackupItems_.size()) * 40 + 80;
        setSize(getParentWidth(), std::max(h, getParentHeight()));
    }

private:
    BackupWorkspaceComponent& owner_;
};

void BackupWorkspaceComponent::carregarColecoesBackupCatalogo() {
    // item 3: abrir um banco SQLite por coleção vinculada + COUNT (I/O de
    // disco real) não pode rodar na message thread — vai pro pool e só
    // aplica o resultado quando pronto. geracaoCatalogoBackup_ descarta
    // resposta atrasada de uma recarga já superada por uma mais nova.
    const int geracao = ++geracaoCatalogoBackup_;
    ProjetoAberto* projeto = &projeto_;
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

    poolCatalogoBackup_.addJob([safeThis, projeto, geracao]() {
        matriz::diag::LogOperacao logOp("carregarColecoesBackupCatalogo");
        std::vector<CatalogBackupItem> itens;
        CatalogBackupItem total;
        total.name = "CATALOG TOTAL";

        auto colecoes = projeto->listarColecoesLinkadas();
        for (const auto& c : colecoes) {
            CatalogBackupItem item;
            item.name = c.nome;
            item.path = c.caminhoProjeto;

            if (!c.valido) {
                item.status = "OFFLINE";
            } else {
                item.sizeBytes = c.totalBytes;
                item.totalAssets = c.totalAssets;
                item.status = "READY";

                juce::File colDir(c.caminhoProjeto);
                juce::File dbFile = matriz::model::Project::resolverPastaProjeto(colDir).getChildFile("registro.sqlite");
                if (dbFile.existsAsFile()) {
                    try {
                        matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                        auto stmtRev = colDb.prepare(
                            "SELECT COUNT(id) FROM item "
                            "WHERE COALESCE(estado, '') != 'duplicata' AND ((ano IS NULL OR ano = 0) "
                            "   OR (source_media IS NULL OR TRIM(source_media) = '') "
                            "   OR (collection_type IS NULL OR TRIM(collection_type) = ''))");
                        if (stmtRev.step()) {
                            item.needsAttention = static_cast<uint64_t>(stmtRev.columnInt(0));
                            if (item.needsAttention > 0 && item.status == "READY") {
                                item.status = "WARNING";
                            }
                        }
                    } catch (...) {}
                }
            }

            total.sizeBytes += item.sizeBytes;
            total.totalAssets += item.totalAssets;
            total.needsAttention += item.needsAttention;
            itens.push_back(item);
        }

        juce::MessageManager::callAsync([safeThis, geracao, itens = std::move(itens), total]() mutable {
            if (!safeThis) return;
            if (geracao != safeThis->geracaoCatalogoBackup_) return; // superado por uma recarga mais nova
            safeThis->catalogBackupItems_ = std::move(itens);
            safeThis->catalogBackupTotal_ = total;
            safeThis->repaint();
        });
    });
}

void BackupWorkspaceComponent::carregarOpcoesContent() {
    opcoesContent_.clear();

    // Standard CONTENT taxonomy from metadata sheets across all categories
    static const std::vector<std::string> kPadroesContent = {
        // Audio
        "Album", "EP", "Single", "Compilation", "Soundtrack", "Stems", "Multitracks",
        "Sample Pack", "Sample", "Preset", "DAW Session", "Field Recording", "Ambience Recording", "Voiceover", "Speech", "Sound FX", "MIDI",
        "Artist Catalog", "Artist Backup", "Demo", "Live Concert", "VS", "Impulse Response", "Mix", "Master", "Raw Audio",
        // Video
        "Raw Footage", "Home Video", "Music Video", "Film", "Documentary",
        "Corporate Video", "Commercial", "Live Performance", "NLE Project", "Social Media Video",
        "WhatsApp Video", "TV Video", "YouTube Video", "360 Video", "Making Of", "Tutorial", "CCTV (Security Camera)",
        // Image
        "Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional", "Image Edit Project",
        "Graphics", "Logo", "3D", "YouTube Thumbnail", "Exhibition", "Raw Photos", "360 Photo", "Technical Photography",
        // Docs
        "Documentation", "Release", "Article", "Press", "Book", "Text", "Journal", "Contract", "Personal Document", "Manual", "Report", "Reference", "Technical Documentation",
        "Sheet", "Zip", "Rar", "PDF", "Doc/Docx", "Markdown",
        "Website", "DAW", "NLE", "CSV", "MTZ Collection", "BKM Catalog"
    };

    std::map<std::string, int> contagens;
    int semConteudo = 0;

    try {
        auto stmt = projeto_.projeto().registro().prepare(
            "SELECT COALESCE(NULLIF(TRIM(i.collection_type), ''), "
            "       (SELECT TRIM(valor) FROM item_campo WHERE item_id = i.id AND campo_id = 'collection_type' AND valor IS NOT NULL AND TRIM(valor) <> '' LIMIT 1), "
            "       '') AS ctype, COUNT(DISTINCT i.id) "
            "FROM item i "
            "WHERE i.projeto_id = ? AND COALESCE(i.estado, '') != 'duplicata' "
            "GROUP BY ctype");
        stmt.bind(1, matriz::db::Value::of(projeto_.projeto().projetoId()));
        while (stmt.step()) {
            std::string ct = stmt.columnText(0);
            int cnt = stmt.columnInt(1);
            if (ct.empty()) {
                semConteudo += cnt;
            } else {
                contagens[ct] += cnt;
            }
        }
    } catch (...) {}

    // 1. Add all standard options from metadata sheets
    std::set<std::string> jaAdicionados;
    for (const auto& opt : kPadroesContent) {
        int cnt = 0;
        auto it = contagens.find(opt);
        if (it != contagens.end()) cnt = it->second;
        opcoesContent_.push_back({opt, juce::String(opt), cnt});
        jaAdicionados.insert(opt);
    }

    // 2. Add any custom options that exist in the database but aren't in the standard list
    for (const auto& [ct, cnt] : contagens) {
        if (jaAdicionados.find(ct) == jaAdicionados.end()) {
            opcoesContent_.push_back({ct, juce::String(ct), cnt});
        }
    }

    // 3. Add "No content defined" / "Sem conteúdo definido"
    juce::String rotuloSem = matriz::i18n::t("backup.sem_conteudo");
    opcoesContent_.push_back({"__empty__", rotuloSem, semConteudo});
}

juce::String BackupWorkspaceComponent::rotuloOpcaoSelecionados() const {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    if (selectedItemIds_.empty()) {
        return isPt ? juce::String::fromUTF8("Arquivos Selecionados") : "Selected Files";
    }
    return (isPt ? juce::String::fromUTF8("Arquivos Selecionados (") : "Selected Files (")
           + juce::String(static_cast<int>(selectedItemIds_.size())) + ")";
}

void BackupWorkspaceComponent::atualizarSelecaoDoGridSeNecessario(bool recalcular) {
    if (whatOption_ != WhatOption::SelectedAssets || !obterSelecaoAtualDoGrid) return;
    selectedItemIds_ = obterSelecaoAtualDoGrid();
    if (comboSource_) comboSource_->changeItemText(3, rotuloOpcaoSelecionados());
    if (btnEditarSelecao_) btnEditarSelecao_->setVisible(true);
    if (recalcular) atualizarResumo();
}

void BackupWorkspaceComponent::abrirJanelaSelecionarArquivos() {
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    BackupFileSelectorDialog::exibirModal(projeto_, selectedItemIds_, this, [safeThis](const std::set<std::string>& novosIds) {
        if (!safeThis) return;
        safeThis->selectedItemIds_ = novosIds;
        safeThis->whatOption_ = WhatOption::SelectedAssets;
        if (safeThis->comboSource_) {
            safeThis->comboSource_->changeItemText(3, safeThis->rotuloOpcaoSelecionados());
            safeThis->comboSource_->setSelectedId(3, juce::dontSendNotification);
        }
        if (safeThis->btnEditarSelecao_) {
            safeThis->btnEditarSelecao_->setVisible(true);
        }
        safeThis->atualizarResumo();
        safeThis->resized();
    });
}

BackupWorkspaceComponent::BackupWorkspaceComponent(ProjetoAberto& projeto, const std::set<std::string>& selectedItemIds)
    : projeto_(projeto), selectedItemIds_(selectedItemIds)
{
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    carregarDestinosBackup();
    carregarOpcoesContent();

    const auto& tk = tema();

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    // === TITLE ===
    labelTitulo_ = std::make_unique<juce::Label>();
    labelTitulo_->setText(isCatalogMode ? matriz::i18n::t("backup.titulo_catalogo") : matriz::i18n::t("backup.titulo_configuracao"), juce::dontSendNotification);
    labelTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    labelTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelTitulo_);

    // === CONFIG CONTAINER & VIEWPORT ===
    configViewport_ = std::make_unique<juce::Viewport>();
    configViewport_->setScrollBarsShown(true, false);
    configContainer_ = std::make_unique<ConfigContainerComponent>(*this);
    configViewport_->setViewedComponent(configContainer_.get(), false);
    addAndMakeVisible(*configViewport_);

    // === SELECTION section ===
    labelSource_ = std::make_unique<juce::Label>();
    labelSource_->setText(matriz::i18n::t("backup.secao_origem"), juce::dontSendNotification);
    labelSource_->setTooltip(isPt ? juce::String::fromUTF8("Seleção dos itens para o backup") : "Selection of assets to backup");
    labelSource_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelSource_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelSource_);

    comboSource_ = std::make_unique<juce::ComboBox>();
    comboSource_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboSource_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboSource_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboSource_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    comboSource_->setTooltip(isPt ? juce::String::fromUTF8("Escolha quais itens incluir nesta execução do backup") : "Choose which assets to include in this backup run");
    comboSource_->addItem(isCatalogMode ? matriz::i18n::t("backup.origem_todos_catalogo") : matriz::i18n::t("backup.origem_todos_projeto"), 1);
    comboSource_->addItem(matriz::i18n::t("backup.origem_intake"), 2);
    comboSource_->addItem(rotuloOpcaoSelecionados(), 3);
    comboSource_->addItem(matriz::i18n::t("backup.origem_sem_backup"), 4);
    comboSource_->addItem(matriz::i18n::t("backup.origem_de_conteudo"), 5);
    comboSource_->setSelectedId(1, juce::dontSendNotification);
    whatOption_ = WhatOption::Everything;
    comboSource_->onChange = [this] {
        int id = comboSource_->getSelectedId();
        if (id == 3) {
            whatOption_ = WhatOption::SelectedAssets;
            if (comboColecoes_) comboColecoes_->setVisible(false);
            if (btnEditarSelecao_) btnEditarSelecao_->setVisible(true);
            // Popula automaticamente com a seleção atual do grid de metadados —
            // não abre mais o diálogo (esse é o papel exclusivo do botão
            // "SELECT FILES..." logo abaixo, ver item 4).
            if (obterSelecaoAtualDoGrid) {
                selectedItemIds_ = obterSelecaoAtualDoGrid();
                comboSource_->changeItemText(3, rotuloOpcaoSelecionados());
            }
            resized();
            atualizarResumo();
            return;
        }
        whatOption_ = static_cast<WhatOption>(id - 1);
        if (comboColecoes_) comboColecoes_->setVisible(id == 5);
        if (btnEditarSelecao_) btnEditarSelecao_->setVisible(id == 3);
        resized();
        atualizarResumo();
    };
    configContainer_->addAndMakeVisible(*comboSource_);

    btnEditarSelecao_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("SELECIONAR ARQUIVOS...") : "SELECT FILES...");
    btnEditarSelecao_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnEditarSelecao_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnEditarSelecao_->setTooltip(isPt ? juce::String::fromUTF8("Abrir janela para selecionar arquivos do backup") : "Open window to choose backup files");
    btnEditarSelecao_->onClick = [this] {
        abrirJanelaSelecionarArquivos();
    };
    configContainer_->addChildComponent(*btnEditarSelecao_);
    if (!selectedItemIds_.empty()) {
        btnEditarSelecao_->setVisible(true);
    }

    comboColecoes_ = std::make_unique<juce::ComboBox>();
    comboColecoes_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboColecoes_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboColecoes_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboColecoes_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    for (size_t i = 0; i < opcoesContent_.size(); ++i)
        comboColecoes_->addItem(opcoesContent_[i].rotulo + " (" + juce::String(opcoesContent_[i].contagem) + ")", static_cast<int>(i + 1));
    if (!opcoesContent_.empty()) comboColecoes_->setSelectedId(1, juce::dontSendNotification);
    comboColecoes_->onChange = [this] {
        selectedContentIdx_ = comboColecoes_->getSelectedItemIndex();
        atualizarResumo();
    };
    configContainer_->addChildComponent(*comboColecoes_);

    // === DESTINATION section ===
    labelDest_ = std::make_unique<juce::Label>();
    labelDest_->setText(matriz::i18n::t("backup.secao_destino"), juce::dontSendNotification);
    labelDest_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelDest_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelDest_);

    listVaults_ = std::make_unique<juce::ListBox>();
    listVaults_->setModel(this);
    listVaults_->setColour(juce::ListBox::backgroundColourId, tk.painelAlt);
    listVaults_->setColour(juce::ListBox::outlineColourId, tk.borda);
    listVaults_->setRowHeight(32);
    configContainer_->addAndMakeVisible(*listVaults_);

    btnBrowseVault_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("+ Adicionar Clone...") : "+ Add Clone...");
    btnBrowseVault_->setTooltip(isPt ? juce::String::fromUTF8("Criar ou vincular um clone (versão de backup)") : "Create or link a clone (backup version) folder");
    aplicarEstiloBotao(*btnBrowseVault_, false);
    btnBrowseVault_->onClick = [this, isPt] {
        auto chooser = std::make_shared<juce::FileChooser>(
            isPt ? juce::String::fromUTF8("Selecione a pasta para o novo CLONE")
                 : "Select folder for the new CLONE");
        juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis, chooser](const juce::FileChooser& fc) {
                                  if (!safeThis) return;
                                  juce::File folder = fc.getResult();
                                  if (folder == juce::File()) return;
                                  safeThis->criarNovoClone(folder);
                              });
    };
    configContainer_->addAndMakeVisible(*btnBrowseVault_);

    // Botão Google Drive como destino
    bool isPtGDest = isPt;
    btnGoogleDriveDest_ = std::make_unique<GoogleDriveIconButton>();
    btnGoogleDriveDest_->setTooltip(isPtGDest
        ? juce::String::fromUTF8("Criar CLONE no Google Drive (requer Google Drive para Desktop)")
        : "Create a CLONE in Google Drive (requires Google Drive for Desktop)");
    btnGoogleDriveDest_->onClick = [this, isPtGDest] {
        // Várias contas do Drive neste Mac: pergunta qual (antes abria a primeira).
        juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
        escolherContaGoogleDrive(btnGoogleDriveDest_.get(), [safeThis, isPtGDest](const juce::File& gdFolder) {
            if (!safeThis) return;
            auto chooser = std::make_shared<juce::FileChooser>(
                isPtGDest ? juce::String::fromUTF8("Selecione ou crie uma subpasta no Google Drive para o CLONE")
                          : "Select or create a subfolder in Google Drive for the CLONE",
                gdFolder);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [safeThis, chooser](const juce::FileChooser& fc) {
                                      if (!safeThis) return;
                                      juce::File subpasta = fc.getResult();
                                      if (subpasta == juce::File()) return;
                                      safeThis->criarNovoClone(subpasta);
                                  });
        });
    };
    configContainer_->addAndMakeVisible(*btnGoogleDriveDest_);

    labelDestInfo_ = std::make_unique<juce::Label>();
    labelDestInfo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    labelDestInfo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    labelDestInfo_->setMinimumHorizontalScale(0.85f);
    configContainer_->addAndMakeVisible(*labelDestInfo_);

    // === FOLDER STRUCTURE section ===
    labelOrg_ = std::make_unique<juce::Label>();
    labelOrg_->setText(matriz::i18n::t("backup.secao_organizacao"), juce::dontSendNotification);
    labelOrg_->setTooltip(isPt ? juce::String::fromUTF8("Defina como as pastas e arquivos serão estruturados no backup") : "Define how folders and files will be structured in the backup");
    labelOrg_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelOrg_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelOrg_);

    togglePreservarEstrutura_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.preservar_estrutura"));
    togglePreservarEstrutura_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    togglePreservarEstrutura_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    togglePreservarEstrutura_->setTooltip(matriz::i18n::t("backup.preservar_estrutura_dica"));
    togglePreservarEstrutura_->setToggleState(false, juce::dontSendNotification);

    toggleUsarEstruturaMapa_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.usar_estrutura_mapa"));
    toggleUsarEstruturaMapa_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleUsarEstruturaMapa_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleUsarEstruturaMapa_->setTooltip(matriz::i18n::t("backup.usar_estrutura_mapa_dica"));
    toggleUsarEstruturaMapa_->setToggleState(true, juce::dontSendNotification);

    auto atualizarEstadoOrganizacao = [this] {
        bool usaOriginal = togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState();
        if (comboOrg_) comboOrg_->setEnabled(!usaOriginal);
        if (btnEditarHierarquia_) {
            bool isCustom = (comboOrg_ && comboOrg_->getSelectedId() == 5);
            btnEditarHierarquia_->setVisible(isCustom && !usaOriginal);
            btnEditarHierarquia_->setEnabled(isCustom && !usaOriginal);
        }
        resized();
        gravarRascunhoOrganizacao();
        atualizarResumo();
    };

    togglePreservarEstrutura_->onClick = [this, atualizarEstadoOrganizacao] {
        if (togglePreservarEstrutura_->getToggleState()) {
            if (toggleUsarEstruturaMapa_) toggleUsarEstruturaMapa_->setToggleState(false, juce::dontSendNotification);
        }
        // O plano novo chega em background: o aviso de conflito espera por ele (concluirPlano()).
        if (togglePreservarEstrutura_->getToggleState()) popupConflitoAposPlano_ = true;
        atualizarEstadoOrganizacao();
    };

    toggleUsarEstruturaMapa_->onClick = [this, atualizarEstadoOrganizacao] {
        if (toggleUsarEstruturaMapa_->getToggleState()) {
            if (togglePreservarEstrutura_) togglePreservarEstrutura_->setToggleState(false, juce::dontSendNotification);
            if (comboOrg_) comboOrg_->setSelectedId(1, juce::dontSendNotification);
        }
        atualizarEstadoOrganizacao();
    };

    configContainer_->addAndMakeVisible(*togglePreservarEstrutura_);
    configContainer_->addAndMakeVisible(*toggleUsarEstruturaMapa_);

    comboMapaMain_ = std::make_unique<juce::ComboBox>();
    comboMapaMain_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboMapaMain_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboMapaMain_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboMapaMain_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    comboMapaMain_->setTooltip(matriz::i18n::t("backup.mapa_main_dica"));
    comboMapaMain_->onChange = [this] { gravarRascunhoOrganizacao(); atualizarResumo(); };
    configContainer_->addAndMakeVisible(*comboMapaMain_);
    recarregarComboMapaMain();

    comboOrg_ = std::make_unique<juce::ComboBox>();
    comboOrg_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboOrg_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboOrg_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboOrg_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    comboOrg_->addItem(matriz::i18n::t("backup.manter_organizacao"), 1);
    comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por tipo de mídia") : "By media type", 2);
    comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por ano") : "By year", 3);
    comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por tipo de mídia + ano") : "By media type + year", 4);
    comboOrg_->addItem(isPt ? juce::String::fromUTF8("Personalizado (editor visual)") : "Custom (visual editor)", 5);
    comboOrg_->setSelectedId(1, juce::dontSendNotification);
    comboOrg_->setEnabled(true);
    comboOrg_->setTooltip("Choose directory naming/organization structure pattern");
    comboOrg_->onChange = [this, atualizarEstadoOrganizacao] {
        int id = comboOrg_->getSelectedId();
        if (id == 1) {
            if (toggleUsarEstruturaMapa_) toggleUsarEstruturaMapa_->setToggleState(true, juce::dontSendNotification);
        } else {
            if (toggleUsarEstruturaMapa_) toggleUsarEstruturaMapa_->setToggleState(false, juce::dontSendNotification);
        }
        if (togglePreservarEstrutura_) togglePreservarEstrutura_->setToggleState(false, juce::dontSendNotification);
        atualizarEstadoOrganizacao();
    };
    configContainer_->addAndMakeVisible(*comboOrg_);

    hierarquiaCustom_ = matriz::consolidacao::hierarquiaPadrao();
    btnEditarHierarquia_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("ABRIR EDITOR VISUAL") : "OPEN VISUAL EDITOR");
    btnEditarHierarquia_->setColour(juce::TextButton::buttonColourId, tk.botaoSecundario);  // OPEN VISUAL EDITOR: secundário
    btnEditarHierarquia_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnEditarHierarquia_->setTooltip("Open visual interactive editor to design folder naming tree");
    btnEditarHierarquia_->onClick = [this] {
        new HierarquiaEditorWindow(hierarquiaCustom_, [this](const matriz::consolidacao::HierarquiaBackup& h) {
            hierarquiaCustom_ = h;
            gravarRascunhoOrganizacao();
            atualizarResumo();
        });
    };
    configContainer_->addChildComponent(*btnEditarHierarquia_);

    prefixoAuto_ = "BKR";
    {
        auto stmtPref = projeto_.projeto().registro().prepare("SELECT prefixo_nomenclatura FROM projeto LIMIT 1");
        if (stmtPref.step() && !stmtPref.columnIsNull(0)) {
            juce::String val = stmtPref.columnText(0);
            if (val.isNotEmpty())
                prefixoAuto_ = val;
        }
    }
    prefixoCustomizado_ = prefixoAuto_;

    labelPrefixo_ = std::make_unique<juce::Label>();
    labelPrefixo_->setText(matriz::i18n::t("backup.prefixo_arquivos"), juce::dontSendNotification);
    labelPrefixo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    labelPrefixo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelPrefixo_);

    comboModoPrefixo_ = std::make_unique<juce::ComboBox>();
    comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_nenhum"), 1);
    comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_auto").replace("{prefix}", prefixoAuto_), 2);
    comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_custom"), 3);
    comboModoPrefixo_->setSelectedId(1, juce::dontSendNotification); // Default: NO PREFIX (1)
    modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Nenhum;
    comboModoPrefixo_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboModoPrefixo_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboModoPrefixo_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboModoPrefixo_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    comboModoPrefixo_->setTooltip(matriz::i18n::t("backup.prefixo_modo_nenhum_dica"));
    comboModoPrefixo_->onChange = [this] {
        int id = comboModoPrefixo_->getSelectedId();
        if (id == 1) {
            modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Nenhum;
            if (editPrefixo_) editPrefixo_->setVisible(false);
        } else if (id == 2) {
            modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Auto;
            if (editPrefixo_) editPrefixo_->setVisible(false);
        } else if (id == 3) {
            modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Custom;
            if (editPrefixo_) editPrefixo_->setVisible(true);
        }
        if (configContainer_) configContainer_->resized();
        atualizarResumo();
    };
    configContainer_->addAndMakeVisible(*comboModoPrefixo_);

    editPrefixo_ = std::make_unique<juce::TextEditor>();
    editPrefixo_->setText(prefixoCustomizado_, juce::dontSendNotification);
    editPrefixo_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    editPrefixo_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
    editPrefixo_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    editPrefixo_->setColour(juce::TextEditor::focusedOutlineColourId, tk.acento);
    editPrefixo_->setTooltip(matriz::i18n::t("backup.prefixo_arquivos_dica"));
    editPrefixo_->setVisible(false);
    editPrefixo_->onTextChange = [this] {
        prefixoCustomizado_ = editPrefixo_->getText().trim();
        atualizarResumo();
    };
    configContainer_->addChildComponent(*editPrefixo_);

    // === OPTIONS section ===
    labelOpcoes_ = std::make_unique<juce::Label>();
    labelOpcoes_->setText(matriz::i18n::t("backup.secao_opcoes"), juce::dontSendNotification);
    labelOpcoes_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelOpcoes_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelOpcoes_);

    toggleVerificarChecksum_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.verificar_checksum"));
    toggleVerificarChecksum_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleVerificarChecksum_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleVerificarChecksum_->setTooltip("Enable reading back copied files to verify SHA-256 integrity");
    toggleVerificarChecksum_->setToggleState(true, juce::dontSendNotification);
    configContainer_->addAndMakeVisible(*toggleVerificarChecksum_);

    toggleGerarCatalogo_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.gerar_banco_sqlite"));
    toggleGerarCatalogo_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleGerarCatalogo_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleGerarCatalogo_->setTooltip("Enable SQLite database summary file generation in target folder");
    toggleGerarCatalogo_->setToggleState(true, juce::dontSendNotification);
    configContainer_->addAndMakeVisible(*toggleGerarCatalogo_);

    toggleAutoResolverConflitos_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.auto_resolver_conflitos"));
    toggleAutoResolverConflitos_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleAutoResolverConflitos_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleAutoResolverConflitos_->setTooltip(matriz::i18n::t("backup.auto_resolver_conflitos"));
    toggleAutoResolverConflitos_->setToggleState(true, juce::dontSendNotification);
    toggleAutoResolverConflitos_->onClick = [this] { atualizarResumo(); };
    configContainer_->addAndMakeVisible(*toggleAutoResolverConflitos_);

    btnAtualizarSidecars_ = std::make_unique<juce::TextButton>(matriz::i18n::t("sidecar.btn"));
    aplicarEstiloBotao(*btnAtualizarSidecars_, false);
    btnAtualizarSidecars_->setTooltip(matriz::i18n::t("sidecar.dica"));
    btnAtualizarSidecars_->onClick = [this] { atualizarSidecars(false); };
    configContainer_->addChildComponent(*btnAtualizarSidecars_);

    toggleForcarRebackup_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.forcar_rebackup"));
    toggleForcarRebackup_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleForcarRebackup_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleForcarRebackup_->setTooltip(matriz::i18n::t("backup.forcar_rebackup_dica"));
    toggleForcarRebackup_->setToggleState(false, juce::dontSendNotification);
    toggleForcarRebackup_->onClick = [this] { atualizarResumo(); };
    configContainer_->addAndMakeVisible(*toggleForcarRebackup_);

    // === PREVIEW section ===
    labelResumo_ = std::make_unique<juce::Label>();
    labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelResumo_);

    labelAssets_ = std::make_unique<juce::Label>();  // mesmo estilo dos títulos dos cards da coluna
    labelAssets_->setText(matriz::i18n::t("backup.secao_assets"), juce::dontSendNotification);
    labelAssets_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelAssets_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*labelAssets_);

    // Títulos dos cards do rodapé: (6) RUN e (7) OUTPUTS FROM THE MAIN (texto do (7) definido no resized: Catálogo
    // usa outro rótulo).
    labelAcao_ = std::make_unique<juce::Label>();
    labelAcao_->setText(matriz::i18n::t("backup.secao_executar"), juce::dontSendNotification);
    labelSaidas_ = std::make_unique<juce::Label>();
    for (auto* l : {labelAcao_.get(), labelSaidas_.get()}) {
        l->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        l->setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(*l);
    }

    barraLegenda_ = std::make_unique<LegendaStatusComponent>();
    barraLegenda_->aoMudarFiltro = [this](PreviaLista::ModoPrioridade modo) {
        if (listPrevia_) listPrevia_->definirModoPrioridade(modo);
    };
    addAndMakeVisible(*barraLegenda_);

    listPrevia_ = std::make_unique<PreviaLista>();
    listPrevia_->aoAbrirNoGrid = [this](const std::set<std::string>& ids) {
        if (aoAbrirNoGrid) aoAbrirNoGrid(ids);
    };
    listPreviaViewport_ = std::make_unique<juce::Viewport>();
    listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);
    addAndMakeVisible(*listPreviaViewport_);

    // === PROGRESS ===
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    addChildComponent(*barraProgresso_);
    // Make Backup de coleção: duas etapas, cada uma no seu card (COPY e CATALOG).
    etapaCopy_ = std::make_unique<EtapaProgressoComponent>(matriz::i18n::t("backup.etapa_copy"));
    etapaCatalog_ = std::make_unique<EtapaProgressoComponent>(matriz::i18n::t("backup.etapa_catalog"));
    addChildComponent(*etapaCopy_);
    addChildComponent(*etapaCatalog_);

    labelProgressoStatus_ = std::make_unique<juce::Label>();
    labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    labelProgressoStatus_->setColour(juce::Label::textColourId, tk.textoPrimario);
    labelProgressoStatus_->setJustificationType(juce::Justification::centred);
    addChildComponent(*labelProgressoStatus_);

    // === BUTTONS ===
    btnStartBackup_ = std::make_unique<juce::TextButton>(matriz::i18n::t("backup.btn_fazer_backup"));
    aplicarEstiloBotao(*btnStartBackup_, true);
    btnStartBackup_->setTooltip("Begin incremental backup process (sends only pending/modified files)");
    btnStartBackup_->onClick = [this] {
        if (togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState() &&
            !plano_.podeConsolidar() && !plano_.nomesEmConflito.empty()) {
            mostrarPopupConflitoPreservacao();
            return;
        }
        executarBackupAcao(false);
    };
    addAndMakeVisible(*btnStartBackup_);

    // EXPORT (etapa 6): recorte volátil do MAIN pra um destino novo.
    btnExportar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("export.btn_abrir"));
    aplicarEstiloBotao(*btnExportar_, false);
    btnExportar_->setTooltip(matriz::i18n::t("export.dica"));
    btnExportar_->onClick = [this] { abrirExport(); };
    addChildComponent(*btnExportar_);

    btnSyncDestino_ = std::make_unique<juce::TextButton>("BACKUP SYNC...");
    aplicarEstiloBotao(*btnSyncDestino_, false);
    btnSyncDestino_->setColour(juce::TextButton::textColourOffId, tk.acento);
    btnSyncDestino_->setTooltip(isPt ? juce::String::fromUTF8("Comparar a versão ativa com outro disco/pasta para sincronização manual")
                                     : "Compare the active version with another drive/folder for manual sync review");
    btnSyncDestino_->onClick = [this] { iniciarSyncComOutroDestino(); };
    addAndMakeVisible(*btnSyncDestino_);

    btnPublishHtml_ = std::make_unique<MarkedActionButton>("PUBLISH TO HTML", "H", juce::Colour(0xff39ff14));
    aplicarEstiloBotao(*btnPublishHtml_, false);
    btnPublishHtml_->setTooltip("Publish static HTML website preview / catalog");
    btnPublishHtml_->onClick = [this] { publicarHtml(); };
    addChildComponent(*btnPublishHtml_);

    btnExportZip_ = std::make_unique<MarkedActionButton>("EXPORT ZIP", "K", juce::Colour(0xff0077ff));
    aplicarEstiloBotao(*btnExportZip_, false);
    btnExportZip_->setTooltip(isPt ? juce::String::fromUTF8("Exportar itens marcados com K como arquivo ZIP")
                                   : "Export assets marked with K as ZIP package");
    btnExportZip_->onClick = [this] {
        ExportZipDialog::exibirModal(projeto_);
    };
    addChildComponent(*btnExportZip_);

    btnLimparZip_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("×"));
    aplicarEstiloBotao(*btnLimparZip_, false);
    btnLimparZip_->setTooltip(matriz::i18n::t("backup.limpar_zip_dica"));
    btnLimparZip_->onClick = [this] {
        projeto_.limparMarcacoes(ProjetoAberto::TipoMarcacao::Zip);
        atualizarBotoesListas();
    };
    addChildComponent(*btnLimparZip_);

    btnSendToPrint_ = std::make_unique<MarkedActionButton>("SEND TO PRINT", "P", juce::Colour(0xffff6b00));
    aplicarEstiloBotao(*btnSendToPrint_, false);
    btnSendToPrint_->setTooltip(isPt ? juce::String::fromUTF8("Enviar fotos marcadas com P para impressão")
                                     : "Send photos marked with P to print");
    btnSendToPrint_->onClick = [this] {
        SendToPrintDialog::exibirModal(projeto_);
    };
    addChildComponent(*btnSendToPrint_);

    btnLimparPrint_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("×"));
    aplicarEstiloBotao(*btnLimparPrint_, false);
    btnLimparPrint_->setTooltip(matriz::i18n::t("backup.limpar_print_dica"));
    btnLimparPrint_->onClick = [this] {
        projeto_.limparMarcacoes(ProjetoAberto::TipoMarcacao::Print);
        atualizarBotoesListas();
    };
    addChildComponent(*btnLimparPrint_);

    btnExportWatermark_ = std::make_unique<MarkedActionButton>("EXPORT WATERMARKED", "W", juce::Colour(0xffffcc00));
    aplicarEstiloBotao(*btnExportWatermark_, false);
    btnExportWatermark_->setTooltip(isPt ? juce::String::fromUTF8("Exportar fotos marcadas com W com marca d'água")
                                         : "Export photos marked with W with watermark");
    btnExportWatermark_->onClick = [this] {
        BatchWatermarkDialog::exibirModal(&projeto_);
    };
    addChildComponent(*btnExportWatermark_);

    btnLimparWatermark_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("×"));
    aplicarEstiloBotao(*btnLimparWatermark_, false);
    btnLimparWatermark_->setTooltip(isPt ? juce::String::fromUTF8("Limpar seleção de fotos para marca d'água")
                                         : "Clear watermark selection");
    btnLimparWatermark_->onClick = [this] {
        projeto_.limparMarcacoes(ProjetoAberto::TipoMarcacao::Watermark);
        atualizarBotoesListas();
    };
    addChildComponent(*btnLimparWatermark_);

    btnCancelarExecucao_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
    aplicarEstiloBotao(*btnCancelarExecucao_, false);
    btnCancelarExecucao_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    btnCancelarExecucao_->setTooltip("Abort current backup task safely");
    btnCancelarExecucao_->onClick = [this] {
        if (cancelamento_) cancelamento_->pedir();
    };
    btnCancelarExecucao_->setVisible(false);
    addAndMakeVisible(*btnCancelarExecucao_);

    btnDone_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("CONCLUÍDO") : "DONE");
    aplicarEstiloBotao(*btnDone_, false);
    btnDone_->setTooltip("Return to Home screen");
    btnDone_->onClick = [this] {
        voltarParaConfiguracao();
        if (aoConcluir) aoConcluir();
        else if (aoVoltarHome) aoVoltarHome();
    };
    addChildComponent(*btnDone_);

    btnRebuildStatus_ = std::make_unique<juce::TextButton>(matriz::i18n::t("rebuild.btn_status"));
    aplicarEstiloBotao(*btnRebuildStatus_, false);
    btnRebuildStatus_->onClick = [this] {
        prepararRelatorioErros(projeto_.emRebuild());
        mostrarErrosBackup();
    };
    addChildComponent(*btnRebuildStatus_);

    btnVerErros_ = std::make_unique<juce::TextButton>(matriz::i18n::t("backup.erros_btn"));
    aplicarEstiloBotao(*btnVerErros_, false);
    btnVerErros_->onClick = [this] { mostrarErrosBackup(); };
    addChildComponent(*btnVerErros_);

    btnOpenCatalog_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("ABRIR PASTA DO BACKUP") : "OPEN BACKUP FOLDER");
    aplicarEstiloBotao(*btnOpenCatalog_, true);
    btnOpenCatalog_->setTooltip(isPt ? juce::String::fromUTF8("Abrir pasta do backup no Finder") : "Open backup folder in Finder");
    btnOpenCatalog_->onClick = [this] {
        juce::File pastaAlvo = matriz::model::normalizarParaRaizDestino(resolvedDestFolder_);
        if (!pastaAlvo.exists()) pastaAlvo = projeto_.projeto().raiz();
        if (!pastaAlvo.exists()) pastaAlvo = projeto_.projeto().pastaMedia();
        if (!pastaAlvo.exists()) pastaAlvo = projeto_.projeto().pasta();
        if (pastaAlvo.exists()) {
            if (!pastaAlvo.startAsProcess()) {
                pastaAlvo.revealToUser();
            }
        }
    };
    addChildComponent(*btnOpenCatalog_);

    btnExportJanela_ = std::make_unique<juce::TextButton>(matriz::i18n::t("backup.btn_exportar_metadados"));
    aplicarEstiloBotao(*btnExportJanela_, false);
    btnExportJanela_->setTooltip(isPt ? juce::String::fromUTF8("Exportar catálogo de metadados como CSV/XLS") : "Export metadata catalog as CSV/XLS");
    btnExportJanela_->onClick = [this] { mostrarJanelaExportar(); };
    addChildComponent(*btnExportJanela_);

    // EXPORT unificado (Fase 6)
    {
        const bool catalogo = projeto_.projeto().modo() == matriz::model::Modo::Catalogo;
        comboExportOrigem_ = std::make_unique<juce::ComboBox>();
        comboExportOrigem_->setLookAndFeel(&comboExportLf_);
        comboExportOrigem_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboExportOrigem_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboExportOrigem_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboExportOrigem_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
        comboExportOrigem_->setTooltip(matriz::i18n::t("export.unificado_dica"));
        for (int id = kExpSelecionados; id <= kExpBridge; ++id) comboExportOrigem_->addItem("-", id);
        // Catálogo nunca teve "Selected Files" (o botão antigo não existia lá);
        // o pacote sai do MAIN, que o Catálogo não tem.
        comboExportOrigem_->setItemEnabled(kExpSelecionados, !catalogo);
        comboExportOrigem_->setItemEnabled(kExpPacote, !catalogo);
        comboExportOrigem_->setItemEnabled(kExpBridge, !catalogo);
        // Coleção sempre abre em SELECTED FILES; só o Catálogo (sem essa opção)
        // lembra a última escolha.
        int inicial = catalogo ? kExpZip : kExpSelecionados;
        const auto arq = arquivoExportOrigem();
        if (catalogo && arq.existsAsFile()) {
            const int lembrado = arq.loadFileAsString().trim().getIntValue();
            if (lembrado >= kExpSelecionados && lembrado <= kExpBridge &&
                lembrado != kExpSelecionados && lembrado != kExpPacote && lembrado != kExpBridge)
                inicial = lembrado;
        }
        comboExportOrigem_->setSelectedId(inicial, juce::dontSendNotification);
        comboExportOrigem_->onChange = [this] {
            // Lembra a última escolha, por projeto (arquivo na pasta do projeto).
            if (!projeto_.somenteLeitura())
                arquivoExportOrigem().replaceWithText(juce::String(comboExportOrigem_->getSelectedId()));
            atualizarExportUnificado();
            resized();
        };
        addChildComponent(*comboExportOrigem_);

        btnExportUnificado_ = std::make_unique<juce::TextButton>(matriz::i18n::t("export.unificado_btn"));
        aplicarEstiloBotao(*btnExportUnificado_, false);
        btnExportUnificado_->setTooltip(matriz::i18n::t("export.unificado_dica"));
        btnExportUnificado_->onClick = [this] { executarExportUnificado(); };
        addChildComponent(*btnExportUnificado_);

        btnBounceAjustes_ = std::make_unique<juce::TextButton>(matriz::i18n::t("bounce.btn_settings"));
        aplicarEstiloBotao(*btnBounceAjustes_, false);
        btnBounceAjustes_->onClick = [this] { abrirAjustesBounce(); };
        addChildComponent(*btnBounceAjustes_);
        btnBounceExport_ = std::make_unique<juce::TextButton>(matriz::i18n::t("bounce.btn_export"));
        aplicarEstiloBotao(*btnBounceExport_, true);
        btnBounceExport_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6b46c1));
        btnBounceExport_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btnBounceExport_->setTooltip(matriz::i18n::t("bounce.btn_export_vazia"));
        btnBounceExport_->onClick = [this] { exportarBounceList(); };
        addChildComponent(*btnBounceExport_);
        setWantsKeyboardFocus(true);
    }

    addChildComponent(overlay_);

    EventBus::obterInstancia().registrarListener(this);
    {
        AdiaResumo umaVez(*this);  // prévia calculada uma vez só, não uma por chamada abaixo
        aplicarRascunhoOrganizacao();  // última escolha da seção ESTRUTURA DE PASTAS (antes do 1º backup)
        carregarDestinoAtivoInicial();
        resumoPendente_ = true;
    }
    atualizarBotoesListas();
}

BackupWorkspaceComponent::~BackupWorkspaceComponent() {
    if (comboExportOrigem_) comboExportOrigem_->setLookAndFeel(nullptr);
    EventBus::obterInstancia().removerListener(this);
    poolCatalogoBackup_.removeAllJobs(true, 2000);
    ++*geracaoPlano_;  // job de plano ainda na fila nem começa
    poolPlano_.removeAllJobs(true, 30000);
    // A cópia do backup usa o Project: cancela entre os blocos e espera o arquivo em curso.
    cancelamento_->pedir();
    poolBackup_.removeAllJobs(true, 60000);
    ++geracaoDestinos_;
    poolDestinos_.removeAllJobs(true, 2000);
    // O job de EXPORT usa o Project: cancela entre arquivos e espera o
    // arquivo em cópia terminar antes de o projeto poder ser fechado.
    cancelarExport_->store(true);
    if (cancelamento_) cancelamento_->pedir();  // a fase final do backup (relatórios) também para entre etapas
    poolExport_.removeAllJobs(true, 60000);
}

void BackupWorkspaceComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    configurarLookAndFeel(comboExportLf_);
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    if (labelTitulo_) {
        labelTitulo_->setText(isCatalogMode ? matriz::i18n::t("backup.titulo_catalogo") : matriz::i18n::t("backup.titulo_configuracao"), juce::dontSendNotification);
        labelTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
        labelTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (btnSyncDestino_) {
        btnSyncDestino_->setButtonText("BACKUP SYNC...");
    }
    if (labelSource_) {
        labelSource_->setText(matriz::i18n::t("backup.secao_origem"), juce::dontSendNotification);
        labelSource_->setTooltip(isPt ? juce::String::fromUTF8("Seleção dos itens para o backup") : "Selection of assets to backup");
        labelSource_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelSource_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (labelAssets_) {
        labelAssets_->setText(matriz::i18n::t("backup.secao_assets"), juce::dontSendNotification);
        labelAssets_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelAssets_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (labelAcao_) labelAcao_->setText(matriz::i18n::t("backup.secao_executar"), juce::dontSendNotification);
    for (auto* l : {labelAcao_.get(), labelSaidas_.get()}) {
        if (!l) continue;
        l->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        l->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (comboSource_) {
        comboSource_->setTooltip(isPt ? juce::String::fromUTF8("Escolha quais itens incluir nesta execução do backup") : "Choose which assets to include in this backup run");
        int sel = comboSource_->getSelectedId();
        comboSource_->clear(juce::dontSendNotification);
        comboSource_->addItem(isCatalogMode ? matriz::i18n::t("backup.origem_todos_catalogo") : matriz::i18n::t("backup.origem_todos_projeto"), 1);
        comboSource_->addItem(matriz::i18n::t("backup.origem_intake"), 2);
        comboSource_->addItem(rotuloOpcaoSelecionados(), 3);
        comboSource_->addItem(matriz::i18n::t("backup.origem_sem_backup"), 4);
        comboSource_->addItem(matriz::i18n::t("backup.origem_de_conteudo"), 5);
        if (sel > 0) comboSource_->setSelectedId(sel, juce::dontSendNotification);

        comboSource_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboSource_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboSource_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboSource_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (btnEditarSelecao_) {
        btnEditarSelecao_->setButtonText(isPt ? juce::String::fromUTF8("SELECIONAR ARQUIVOS...") : "SELECT FILES...");
        btnEditarSelecao_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnEditarSelecao_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (comboColecoes_) {
        int sel = comboColecoes_->getSelectedId();
        carregarOpcoesContent();
        comboColecoes_->clear(juce::dontSendNotification);
        for (size_t i = 0; i < opcoesContent_.size(); ++i)
            comboColecoes_->addItem(opcoesContent_[i].rotulo + " (" + juce::String(opcoesContent_[i].contagem) + ")", static_cast<int>(i + 1));
        if (sel > 0 && sel <= static_cast<int>(opcoesContent_.size()))
            comboColecoes_->setSelectedId(sel, juce::dontSendNotification);
        else if (!opcoesContent_.empty())
            comboColecoes_->setSelectedId(1, juce::dontSendNotification);

        comboColecoes_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboColecoes_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboColecoes_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboColecoes_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (labelDest_) {
        labelDest_->setText(matriz::i18n::t("backup.secao_destino"), juce::dontSendNotification);
        labelDest_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelDest_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (btnBrowseVault_) {
        btnBrowseVault_->setButtonText(isPt ? juce::String::fromUTF8("+ Adicionar Clone...") : "+ Add Clone...");
        btnBrowseVault_->setTooltip(isPt ? juce::String::fromUTF8("Criar ou vincular um clone (versão de backup)") : "Create or link a clone (backup version) folder");
    }
    if (btnGoogleDriveDest_) {
        btnGoogleDriveDest_->setTooltip(isPt
            ? juce::String::fromUTF8("Criar CLONE no Google Drive (requer Google Drive para Desktop)")
            : "Create a CLONE in Google Drive (requires Google Drive for Desktop)");
    }
    if (listVaults_) {
        listVaults_->setColour(juce::ListBox::backgroundColourId, tk.painelAlt);
        listVaults_->setColour(juce::ListBox::outlineColourId, tk.borda);
        listVaults_->repaint();
    }
    if (labelDestInfo_) {
        labelDestInfo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        labelDestInfo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (labelOrg_) {
        labelOrg_->setText(matriz::i18n::t("backup.secao_organizacao"), juce::dontSendNotification);
        labelOrg_->setTooltip(isPt ? juce::String::fromUTF8("Defina como as pastas e arquivos serão estruturados no backup") : "Define how folders and files will be structured in the backup");
        labelOrg_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelOrg_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (togglePreservarEstrutura_) {
        togglePreservarEstrutura_->setButtonText(matriz::i18n::t("backup.preservar_estrutura"));
        togglePreservarEstrutura_->setTooltip(matriz::i18n::t("backup.preservar_estrutura_dica"));
        togglePreservarEstrutura_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        togglePreservarEstrutura_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (toggleUsarEstruturaMapa_) {
        toggleUsarEstruturaMapa_->setButtonText(matriz::i18n::t("backup.usar_estrutura_mapa"));
        toggleUsarEstruturaMapa_->setTooltip(matriz::i18n::t("backup.usar_estrutura_mapa_dica"));
        toggleUsarEstruturaMapa_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleUsarEstruturaMapa_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (comboOrg_) {
        int sel = comboOrg_->getSelectedId();
        comboOrg_->clear(juce::dontSendNotification);
        comboOrg_->addItem(matriz::i18n::t("backup.manter_organizacao"), 1);
        comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por tipo de mídia") : "By media type", 2);
        comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por ano") : "By year", 3);
        comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por tipo de mídia + ano") : "By media type + year", 4);
        comboOrg_->addItem(isPt ? juce::String::fromUTF8("Personalizado (editor visual)") : "Custom (visual editor)", 5);
        if (sel > 0) comboOrg_->setSelectedId(sel, juce::dontSendNotification);

        comboOrg_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboOrg_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboOrg_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboOrg_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (btnEditarHierarquia_) {
        btnEditarHierarquia_->setButtonText(isPt ? juce::String::fromUTF8("ABRIR EDITOR VISUAL") : "OPEN VISUAL EDITOR");
        btnEditarHierarquia_->setColour(juce::TextButton::buttonColourId, tk.botaoSecundario);  // OPEN VISUAL EDITOR: secundário
        btnEditarHierarquia_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (labelPrefixo_) {
        labelPrefixo_->setText(matriz::i18n::t("backup.prefixo_arquivos"), juce::dontSendNotification);
        labelPrefixo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        labelPrefixo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (comboModoPrefixo_) {
        int sel = comboModoPrefixo_->getSelectedId();
        comboModoPrefixo_->clear(juce::dontSendNotification);
        comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_nenhum"), 1);
        comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_auto").replace("{prefix}", prefixoAuto_), 2);
        comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_custom"), 3);
        comboModoPrefixo_->setSelectedId(sel > 0 ? sel : 1, juce::dontSendNotification);
        comboModoPrefixo_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboModoPrefixo_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboModoPrefixo_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboModoPrefixo_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
        comboModoPrefixo_->setTooltip(matriz::i18n::t("backup.prefixo_modo_nenhum_dica"));
    }
    if (editPrefixo_) {
        editPrefixo_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        editPrefixo_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        editPrefixo_->setColour(juce::TextEditor::outlineColourId, tk.borda);
        editPrefixo_->setColour(juce::TextEditor::focusedOutlineColourId, tk.acento);
        editPrefixo_->setTooltip(matriz::i18n::t("backup.prefixo_arquivos_dica"));
    }
    if (labelOpcoes_) {
        labelOpcoes_->setText(matriz::i18n::t("backup.secao_opcoes"), juce::dontSendNotification);
        labelOpcoes_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelOpcoes_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (toggleVerificarChecksum_) {
        toggleVerificarChecksum_->setButtonText(matriz::i18n::t("backup.verificar_checksum"));
        toggleVerificarChecksum_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleVerificarChecksum_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (toggleGerarCatalogo_) {
        toggleGerarCatalogo_->setButtonText(matriz::i18n::t("backup.gerar_banco_sqlite"));
        toggleGerarCatalogo_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleGerarCatalogo_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (toggleAutoResolverConflitos_) {
        toggleAutoResolverConflitos_->setButtonText(matriz::i18n::t("backup.auto_resolver_conflitos"));
        toggleAutoResolverConflitos_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleAutoResolverConflitos_->setColour(juce::ToggleButton::tickColourId, tk.acento);
        toggleAutoResolverConflitos_->setTooltip(matriz::i18n::t("backup.auto_resolver_conflitos"));
    }
    if (toggleForcarRebackup_) {
        toggleForcarRebackup_->setButtonText(matriz::i18n::t("backup.forcar_rebackup"));
        toggleForcarRebackup_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleForcarRebackup_->setColour(juce::ToggleButton::tickColourId, tk.acento);
        toggleForcarRebackup_->setTooltip(matriz::i18n::t("backup.forcar_rebackup_dica"));
    }
    if (labelResumo_) {
        labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (labelProgressoStatus_) {
        labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        labelProgressoStatus_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (btnStartBackup_) {
        btnStartBackup_->setButtonText(matriz::i18n::t(mainSelado_ ? "backup.btn_adicionar_ao_main" : "backup.btn_fazer_backup"));
        aplicarEstiloBotao(*btnStartBackup_, true);
    }
    if (btnPublishHtml_) {
        aplicarEstiloBotao(*btnPublishHtml_, false);
    }
    if (btnCancelarExecucao_) {
        btnCancelarExecucao_->setButtonText(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
        aplicarEstiloBotao(*btnCancelarExecucao_, false);
        btnCancelarExecucao_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    }
    if (btnDone_) {
        btnDone_->setButtonText(isPt ? juce::String::fromUTF8("CONCLUÍDO") : "DONE");
        aplicarEstiloBotao(*btnDone_, false);
    }
    if (btnRebuildStatus_) btnRebuildStatus_->setButtonText(matriz::i18n::t("rebuild.btn_status"));
    if (btnVerErros_) {
        btnVerErros_->setButtonText(matriz::i18n::t("backup.erros_btn"));
        aplicarEstiloBotao(*btnVerErros_, false);
    }
    if (btnOpenCatalog_) {
        btnOpenCatalog_->setButtonText(isPt ? juce::String::fromUTF8("ABRIR PASTA DO BACKUP") : "OPEN BACKUP FOLDER");
        btnOpenCatalog_->setTooltip(isPt ? juce::String::fromUTF8("Abrir pasta do backup no Finder") : "Open backup folder in Finder");
        aplicarEstiloBotao(*btnOpenCatalog_, true);
    }
    if (btnExportJanela_) {
        btnExportJanela_->setButtonText(matriz::i18n::t("backup.btn_exportar_metadados"));
        aplicarEstiloBotao(*btnExportJanela_, false);
    }
    if (btnExportZip_) {
        aplicarEstiloBotao(*btnExportZip_, false);
    }
    if (btnLimparZip_) {
        aplicarEstiloBotao(*btnLimparZip_, false);
    }
    if (btnSendToPrint_) {
        aplicarEstiloBotao(*btnSendToPrint_, false);
    }
    if (btnLimparPrint_) {
        aplicarEstiloBotao(*btnLimparPrint_, false);
    }
    if (btnExportWatermark_) {
        aplicarEstiloBotao(*btnExportWatermark_, false);
    }
    if (btnLimparWatermark_) {
        aplicarEstiloBotao(*btnLimparWatermark_, false);
    }
    if (btnBrowseVault_) {
        btnBrowseVault_->setButtonText(matriz::i18n::t("backup.escolher_pasta"));
        aplicarEstiloBotao(*btnBrowseVault_, false);
    }

    if (listPrevia_) listPrevia_->repaint();
    atualizarResumo();
    atualizarBotoesListas();
    repaint();
}

void BackupWorkspaceComponent::mostrarJanelaExportar() {
    if (saidaBloqueadaSemMain()) return;  // nada sai do projeto antes do MAIN
    struct JanelaExportarMetadata : public juce::DialogWindow {
        JanelaExportarMetadata(const juce::String& title, juce::Colour bg)
            : juce::DialogWindow(title, bg, true) {}

        void closeButtonPressed() override {
            setVisible(false);
            exitModalState(0);
        }
    };

    int modalH = 300;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    auto janela = std::make_shared<JanelaExportarMetadata>(matriz::i18n::t("backup.btn_exportar_metadados"), tema().painel);

    struct PainelExportar : public juce::Component {
        PainelExportar(BackupWorkspaceComponent& parent, std::shared_ptr<juce::DialogWindow> win)
            : win_(std::move(win)) {
            const auto& tk = tema();
            bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

            lblTitulo_ = std::make_unique<juce::Label>("", matriz::i18n::t("backup.btn_exportar_metadados"));
            lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
            lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
            lblTitulo_->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*lblTitulo_);

            auto criarBotao = [&](const juce::String& text, std::function<void()> cb) {
                auto b = std::make_unique<juce::TextButton>(text);
                parent.aplicarEstiloBotao(*b, false);
                b->onClick = [this, cb] {
                    if (win_) win_->exitModalState(0);
                    if (cb) cb();
                };
                addAndMakeVisible(*b);
                return b;
            };

            btnCsvFull_ = criarBotao(isPt ? juce::String::fromUTF8("CSV (COMPLETO)") : "CSV (FULL)", [&parent] { parent.exportarCsv(); });
            btnXls_ = criarBotao("XLS", [&parent] { parent.exportarXls(); });
            btnDublinCore_ = criarBotao(isPt ? juce::String::fromUTF8("CSV (DUBLIN CORE)") : "CSV (DUBLIN CORE)", [&parent] { parent.exportarDublinCore(); });
            btnChecksums_ = criarBotao(isPt ? juce::String::fromUTF8("HASHES / CHECKSUMS") : "CHECKSUMS", [&parent] { parent.exportarChecksums(); });

            setSize(460, 300);
        }

        void resized() override {
            auto area = getLocalBounds().reduced(24, 20);
            lblTitulo_->setBounds(area.removeFromTop(32));
            area.removeFromTop(14);

            int btnH = 38;
            btnCsvFull_->setBounds(area.removeFromTop(btnH));
            area.removeFromTop(10);
            btnXls_->setBounds(area.removeFromTop(btnH));
            area.removeFromTop(10);
            btnDublinCore_->setBounds(area.removeFromTop(btnH));
            area.removeFromTop(10);
            btnChecksums_->setBounds(area.removeFromTop(btnH));
        }

    private:
        std::shared_ptr<juce::DialogWindow> win_;
        std::unique_ptr<juce::Label> lblTitulo_;
        std::unique_ptr<juce::TextButton> btnCsvFull_;
        std::unique_ptr<juce::TextButton> btnXls_;
        std::unique_ptr<juce::TextButton> btnDublinCore_;
        std::unique_ptr<juce::TextButton> btnChecksums_;
    };

    auto painel = std::make_unique<PainelExportar>(*this, janela);

    janela->setContentOwned(painel.release(), true);
    janela->setResizable(false, false);
    janela->centreWithSize(460, modalH);
    janela->setVisible(true);
    janela->enterModalState(true);
}

void BackupWorkspaceComponent::publicarHtml() {
    if (saidaBloqueadaSemMain()) return;  // nada sai do projeto antes do MAIN
    PublishHtmlDialog::exibirModal(projeto_);
}

void BackupWorkspaceComponent::aoItemAlterado(const EventoItemAlterado& e) {
    bool tituloMudou = (e.tipoAlteracao == "titulo");
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<BackupWorkspaceComponent>(this), tituloMudou] {
        if (safe != nullptr) {
            if (safe->escondido()) {  // outra aba na frente: atualiza quando voltar a aparecer
                safe->botoesPendentes_ = true;
                if (tituloMudou) safe->resumoDeTituloPendente_ = true;
                return;
            }
            safe->atualizarBotoesListas();
            // O nome final no backup vem da máscara "{codigo}-{seq:03}-{titulo}" —
            // um título novo muda o nome planejado, então a prévia/plano precisa
            // ser recalculada (não só os contadores de botões).
            if (tituloMudou) safe->atualizarResumo();
        }
    });
}

juce::File BackupWorkspaceComponent::arquivoExportOrigem() const {
    return projeto_.projeto().pasta().getChildFile("export_origem.txt");
}

// Contadores nas opções do dropdown, botão EXPORT e botões de limpar. As regras de
// habilitar são as dos botões antigos: "só depois do MAIN" para arquivos
// selecionados e planilha (modo Coleção); listas ZIP/Print/Watermark só precisam de itens.
void BackupWorkspaceComponent::atualizarExportUnificado() {
    if (!comboExportOrigem_ || !btnExportUnificado_) return;
    const bool catalogo = projeto_.projeto().modo() == matriz::model::Modo::Catalogo;
    const int nZip = static_cast<int>(projeto_.contarMarcacoes(ProjetoAberto::TipoMarcacao::Zip));
    const int nPrint = static_cast<int>(projeto_.contarMarcacoes(ProjetoAberto::TipoMarcacao::Print));
    const int nWm = static_cast<int>(projeto_.idsMarcados(ProjetoAberto::TipoMarcacao::Watermark).size());
    const int nBounce = static_cast<int>(projeto_.contarMarcacoes(ProjetoAberto::TipoMarcacao::Bounce));
    auto opcao = [&](int id, const char* chave, int n) {
        comboExportOrigem_->changeItemText(id, matriz::i18n::t(chave).replace("{n}", juce::String(n)));
    };
    opcao(kExpSelecionados, "export.op_selecionados", contagemSelecionados_);
    opcao(kExpZip, "export.op_zip", nZip);
    opcao(kExpPrint, "export.op_print", nPrint);
    opcao(kExpWatermark, "export.op_watermark", nWm);
    comboExportOrigem_->changeItemText(kExpPlanilha, matriz::i18n::t("export.op_planilha"));
    opcao(kExpPacote, "export.op_pacote", contagemSelecionados_);
    opcao(kExpBridge, "export.op_bridge", contagemSelecionados_);
    if (nBounce_ != nBounce) { nBounce_ = nBounce; repaint(cartaoBounce_); }
    if (btnBounceExport_) btnBounceExport_->setEnabled(!catalogo && !exportando_ && estado_ == Estado::Config && nBounce > 0);

    // changeItemText() não atualiza o texto já exibido no combo (ficava "-"):
    // reselecionar o mesmo id força o rótulo a acompanhar o item.
    {
        const int atual = comboExportOrigem_->getSelectedId();
        comboExportOrigem_->setSelectedId(atual != 0 ? atual : (catalogo ? kExpZip : kExpSelecionados),
                                          juce::dontSendNotification);
    }
    const int id = comboExportOrigem_->getSelectedId();
    bool habilitado = false;
    switch (id) {
        case kExpSelecionados: habilitado = !catalogo && mainSelado_ && !exportando_ && contagemSelecionados_ > 0; break;
        case kExpZip: habilitado = nZip > 0; break;
        case kExpPrint: habilitado = nPrint > 0; break;
        case kExpWatermark: habilitado = nWm > 0; break;
        case kExpPlanilha: habilitado = catalogo ? !plano_.itens.empty() || projeto_.listarColecoesLinkadas().size() > 0
                                                 : mainSelado_; break;
        case kExpPacote: habilitado = !catalogo && mainSelado_ && !exportando_ && contagemSelecionados_ > 0; break;
        case kExpBridge: habilitado = !catalogo && mainSelado_ && !exportando_ && contagemSelecionados_ > 0; break;
        default: break;
    }
    btnExportUnificado_->setEnabled(habilitado);
    btnExportUnificado_->setTooltip(!habilitado && !catalogo && mainSelado_ == false && (id == kExpSelecionados || id == kExpPlanilha || id == kExpPacote || id == kExpBridge)
                                        ? matriz::i18n::t("backup.saida_sem_main")
                                        : matriz::i18n::t("export.unificado_dica"));

    // Limpar só aparece ao lado do dropdown quando a lista correspondente está escolhida.
    auto mostrarLimpar = [&](juce::TextButton* b, bool ativo) { if (b && b->isVisible() != ativo) b->setVisible(ativo); };
    mostrarLimpar(btnLimparZip_.get(), id == kExpZip && estado_ == Estado::Config);
    mostrarLimpar(btnLimparPrint_.get(), id == kExpPrint && estado_ == Estado::Config);
    mostrarLimpar(btnLimparWatermark_.get(), id == kExpWatermark && estado_ == Estado::Config);
}

// "B" roxo ao lado de "Bounce List" no dropdown do EXPORT (o ComboBox não tem ícone por item: pega o item do menu).
void BackupWorkspaceComponent::aplicarIconeBounceNoCombo() {
    if (!comboExportOrigem_) return;
    juce::Image img(juce::Image::ARGB, 32, 32, true);
    {
        juce::Graphics g(img);
        g.setColour(juce::Colour(0xff9333ea));
        g.fillRoundedRectangle(2.0f, 2.0f, 28.0f, 28.0f, 6.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
        g.drawText("B", 0, 0, 32, 32, juce::Justification::centred);
    }
    auto* menu = comboExportOrigem_->getRootMenu();
    juce::PopupMenu::MenuItemIterator it(*menu, true);
    while (it.next()) {
        auto& item = it.getItem();
        if (item.itemID == kExpBounce) {
            auto d = std::make_unique<juce::DrawableImage>();
            d->setImage(img);
            item.setImage(std::move(d));
        }
    }
}

void BackupWorkspaceComponent::executarExportUnificado() {
    if (!comboExportOrigem_ || !btnExportUnificado_ || !btnExportUnificado_->isEnabled()) return;
    juce::TextButton* alvo = nullptr;
    switch (comboExportOrigem_->getSelectedId()) {
        case kExpSelecionados: alvo = btnExportar_.get(); break;
        case kExpZip: alvo = btnExportZip_.get(); break;
        case kExpPrint: alvo = btnSendToPrint_.get(); break;
        case kExpWatermark: alvo = btnExportWatermark_.get(); break;
        case kExpPlanilha: alvo = btnExportJanela_.get(); break;
        case kExpPacote: abrirPacote(); return;
        case kExpBridge: abrirExport(true); return;  // mesmo recorte do EXPORT de selecionados, com XMP do Bridge
        default: break;
    }
    // Exatamente a ação e o diálogo do botão antigo correspondente.
    if (alvo && alvo->onClick) alvo->onClick();
}

// Atalho W na aba BACKUP: escolhe "Watermark List" e dispara o export (se houver itens marcados).
bool BackupWorkspaceComponent::keyPressed(const juce::KeyPress& tecla) {
    const auto c = tecla.getTextCharacter();
    if ((c == 'w' || c == 'W') && !tecla.getModifiers().isAnyModifierKeyDown() && comboExportOrigem_ && btnExportUnificado_
        && comboExportOrigem_->isVisible()) {
        comboExportOrigem_->setSelectedId(kExpWatermark, juce::sendNotificationSync);
        executarExportUnificado();
        return true;
    }
    return false;
}

void BackupWorkspaceComponent::atualizarBotoesListas() {
    const size_t countZip = projeto_.contarMarcacoes(ProjetoAberto::TipoMarcacao::Zip);
    const size_t countPrint = projeto_.contarMarcacoes(ProjetoAberto::TipoMarcacao::Print);
    const size_t countHtml = projeto_.contarMarcacoes(ProjetoAberto::TipoMarcacao::Html);

    if (btnExportZip_) {
        if (auto* mb = dynamic_cast<MarkedActionButton*>(btnExportZip_.get())) {
            mb->setCount((int)countZip);
        } else {
            btnExportZip_->setButtonText("EXPORT ZIP (K) (" + juce::String((int)countZip) + ")");
        }
        btnExportZip_->setEnabled(countZip > 0);
    }
    if (btnLimparZip_) {
        btnLimparZip_->setEnabled(countZip > 0);
        btnLimparZip_->setTooltip(matriz::i18n::t("backup.limpar_zip_dica"));
    }
    if (btnSendToPrint_) {
        if (auto* mb = dynamic_cast<MarkedActionButton*>(btnSendToPrint_.get())) {
            mb->setCount((int)countPrint);
        } else {
            btnSendToPrint_->setButtonText("SEND TO PRINT (P) (" + juce::String((int)countPrint) + ")");
        }
        btnSendToPrint_->setEnabled(countPrint > 0);
    }
    if (btnLimparPrint_) {
        btnLimparPrint_->setEnabled(countPrint > 0);
        btnLimparPrint_->setTooltip(matriz::i18n::t("backup.limpar_print_dica"));
    }
    auto countWatermark = projeto_.idsMarcados(ProjetoAberto::TipoMarcacao::Watermark).size();
    if (btnExportWatermark_) {
        if (auto* mb = dynamic_cast<MarkedActionButton*>(btnExportWatermark_.get())) {
            mb->setCount((int)countWatermark);
        } else {
            btnExportWatermark_->setButtonText("EXPORT WATERMARKED (W) (" + juce::String((int)countWatermark) + ")");
        }
        btnExportWatermark_->setEnabled(countWatermark > 0);
    }
    if (btnLimparWatermark_) {
        btnLimparWatermark_->setEnabled(countWatermark > 0);
    }
    if (btnPublishHtml_) {
        if (auto* mb = dynamic_cast<MarkedActionButton*>(btnPublishHtml_.get())) {
            mb->setCount((int)countHtml);
        }
        // Coleção: a liberação é só do MAIN (a escolha "só marcados H / todos"
        // fica na janela de publicação). Catálogo: regra de antes.
        if (projeto_.projeto().modo() == matriz::model::Modo::Catalogo) btnPublishHtml_->setEnabled(countHtml > 0);
        else atualizarBotoesDependentesDoMain();
    }
    atualizarExportUnificado();
}

void BackupWorkspaceComponent::exportarCsv() {
    juce::FileChooser fc("Export BKR Full CSV Package...", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    if (fc.browseForDirectory()) {
        juce::File targetDir = fc.getResult();

        std::vector<std::string> ids;
        for (const auto& item : plano_.itens) ids.push_back(item.itemId);
        if (ids.empty()) {
            try {
                auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item WHERE COALESCE(estado, '') != 'duplicata'");
                while (stmt.step()) ids.push_back(stmt.columnText(0));
            } catch (...) {}
        }

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String err;
        bool ok = false;
        ProgressThread thread("EXPORT METADATA - CSV (FULL)", [this, &ids, &targetDir, &ok, &err](ProgressThread* t) {
            t->setStatusMessage("Generating BKR Full CSV Package...");
            t->setProgress(0.4);
            ok = projeto_.exportarFullCsvPacote(ids, targetDir, err);
            t->setProgress(1.0);
        });
        thread.runThread();

        if (ok) {
            juce::File pkgDir = targetDir.getFileName() == "BKR_Full_Export" ? targetDir : targetDir.getChildFile("BKR_Full_Export");
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("EXPORT FULL CSV")
                    .withMessage("BKR Full CSV Package exported and validated successfully!\n\nPackage location:\n" + pkgDir.getFullPathName() + "\n\nContents:\n- BKR_FULL.csv\n- BKR_FULL.schema.json\n- manifest.json")
                    .withButton("OK"),
                static_cast<juce::ModalComponentManager::Callback*>(nullptr));
        } else {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("EXPORT FULL CSV FAILED")
                    .withMessage("BKR Full CSV Export failed validation:\n" + err)
                    .withButton("OK"),
                static_cast<juce::ModalComponentManager::Callback*>(nullptr));
        }
    }
}

void BackupWorkspaceComponent::exportarXls() {
    juce::FileChooser fc("Export XLS Spreadsheet...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.xls");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("xls");

        std::vector<std::string> ids;
        for (const auto& item : plano_.itens) ids.push_back(item.itemId);
        if (ids.empty()) {
            try {
                auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item WHERE COALESCE(estado, '') != 'duplicata'");
                while (stmt.step()) ids.push_back(stmt.columnText(0));
            } catch (...) {}
        }

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String xls;
        ProgressThread thread("EXPORT METADATA - XLS", [this, &ids, &xls](ProgressThread* t) {
            t->setStatusMessage("Generating XLS Spreadsheet (" + juce::String(static_cast<int>(ids.size())) + " items)...");
            t->setProgress(0.5);
            xls = projeto_.exportarXlsXml(ids);
            t->setProgress(1.0);
        });
        thread.runThread();

        targetFile.replaceWithText(xls);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT XLS")
                .withMessage("XLS Spreadsheet exported successfully to:\n" + targetFile.getFullPathName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

namespace {
// Relatórios do fim do backup: gravados em .tmp e renomeados só no fim — um CSV/XLS/manifesto nunca existe pela
// metade (cancelar ou cair no meio não deixa arquivo parcial). `fimDeLinha` padrão = o de sempre do JUCE (CRLF) pra
// não mudar o formato dos relatórios; o .sha256 usa LF (com CRLF o shasum -c lê "nome\r").
bool escreverRelatorioAtomico(const juce::File& alvo, const juce::String& texto, const char* fimDeLinha = "\r\n") {
    juce::File tmp = alvo.getSiblingFile(alvo.getFileName() + ".tmp");
    if (!tmp.replaceWithText(texto, false, false, fimDeLinha) || !tmp.moveFileTo(alvo)) {
        tmp.deleteFile();
        return false;
    }
    return true;
}

std::vector<std::string> idsDoPlano(const std::vector<matriz::consolidacao::ItemPlanejado>& itens) {
    std::vector<std::string> ids;
    ids.reserve(itens.size());
    for (const auto& item : itens) ids.push_back(item.itemId);
    return ids;
}

// Plano vazio (sem backup planejado): todos os itens do catálogo.
std::vector<std::string> idsDoPlanoOuTodos(ProjetoAberto& projeto, const std::vector<matriz::consolidacao::ItemPlanejado>& itens) {
    auto ids = idsDoPlano(itens);
    if (ids.empty()) {
        try {
            auto stmt = projeto.projeto().registro().prepare("SELECT id FROM item WHERE COALESCE(estado, '') != 'duplicata'");
            while (stmt.step()) ids.push_back(stmt.columnText(0));
        } catch (...) {}
    }
    return ids;
}

void gravarRelatorioCsv(ProjetoAberto& projeto, const std::vector<matriz::consolidacao::ItemPlanejado>& itens, const juce::File& destFolder) {
    // WINPORT: NomesSeguros — nome de arquivo gerado a partir do nome do projeto (caracteres inválidos no Windows).
    juce::File alvo = destFolder.getChildFile(juce::String(projeto.projeto().nome()) + "_full_report.csv");
    escreverRelatorioAtomico(alvo, projeto.exportarFullCsv(idsDoPlanoOuTodos(projeto, itens)));
}

void gravarRelatorioXls(ProjetoAberto& projeto, const std::vector<matriz::consolidacao::ItemPlanejado>& itens, const juce::File& destFolder) {
    // WINPORT: NomesSeguros — idem.
    juce::File alvo = destFolder.getChildFile(juce::String(projeto.projeto().nome()) + "_catalog.xls");
    escreverRelatorioAtomico(alvo, projeto.exportarXlsXml(idsDoPlanoOuTodos(projeto, itens)));
}

void gravarRelatorioDublinCore(ProjetoAberto& projeto, const std::vector<matriz::consolidacao::ItemPlanejado>& itens, const juce::File& destFolder) {
    // WINPORT: NomesSeguros — idem.
    juce::File alvo = destFolder.getChildFile(juce::String(projeto.projeto().nome()) + "_dublin_core.csv");
    escreverRelatorioAtomico(alvo, projeto.exportarDublinCoreCsv(idsDoPlano(itens)));
}

// .sha256 do backup. Cancelado: some o do backup anterior (não pode parecer o deste). Devolve se gravou.
bool gravarManifestoChecksums(const juce::File& destFolder, const juce::String& nomeProjeto, const juce::String& manifesto, bool cancelado) {
    // WINPORT: NomesSeguros — nome de arquivo gerado a partir do nome do projeto (caracteres inválidos no Windows).
    juce::File alvo = destFolder.getChildFile(nomeProjeto + ".sha256");
    if (cancelado) {
        alvo.getSiblingFile(alvo.getFileName() + ".tmp").deleteFile();
        alvo.deleteFile();
        return false;
    }
    return escreverRelatorioAtomico(alvo, manifesto, "\n");
}

void registrarManifestoSemRegistroNoLog(const juce::File& pastaProjeto, int semRegistro, int total) {
    if (semRegistro <= 0) return;
    matriz::model::ProjectLog pLog(pastaProjeto);
    pLog.appendEntry("Checksum manifest: items not consolidated",
                     {juce::String(semRegistro) + " of " + juce::String(total) +
                          " items have no consolidation record in this destination and were listed as comments (# not consolidated).",
                      "No file is read to compute a hash here; consolidate them (backup / add to MAIN) to include them."});
}
} // namespace

void BackupWorkspaceComponent::exportarCsvPara(const juce::File& destFolder) {
    gravarRelatorioCsv(projeto_, plano_.itens, destFolder);
}

void BackupWorkspaceComponent::exportarXlsPara(const juce::File& destFolder) {
    gravarRelatorioXls(projeto_, plano_.itens, destFolder);
}

void BackupWorkspaceComponent::exportarDublinCore() {
    juce::FileChooser fc("Export DC-CSV Report...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.csv");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("csv");

        std::vector<std::string> ids;
        for (const auto& item : plano_.itens) ids.push_back(item.itemId);
        if (ids.empty()) {
            try {
                auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item WHERE COALESCE(estado, '') != 'duplicata'");
                while (stmt.step()) ids.push_back(stmt.columnText(0));
            } catch (...) {}
        }

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String csv;
        ProgressThread thread("EXPORT METADATA - CSV (DUBLIN CORE)", [this, &ids, &csv](ProgressThread* t) {
            t->setStatusMessage("Generating Dublin Core CSV (" + juce::String(static_cast<int>(ids.size())) + " items)...");
            t->setProgress(0.5);
            csv = projeto_.exportarDublinCoreCsv(ids);
            t->setProgress(1.0);
        });
        thread.runThread();

        targetFile.replaceWithText(csv);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT DC-CSV")
                .withMessage("DC-CSV Report exported to:\n" + targetFile.getFullPathName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

juce::String BackupWorkspaceComponent::gerarManifestChecksumsBackup(const std::function<bool(int, int)>& onProgress,
                                                                    bool* cancelado,
                                                                    const juce::File& destinoMedia) {
    if (cancelado) *cancelado = false;
    auto r = matriz::consolidacao::gerarManifestChecksums(projeto_.projeto().registro(), destinoMedia, plano_.itens, onProgress);
    if (r.cancelado) {
        if (cancelado) *cancelado = true;
        return {};
    }
    registrarManifestoSemRegistroNoLog(projeto_.projeto().pasta(), r.semRegistro, static_cast<int>(plano_.itens.size()));
    return juce::String::fromUTF8(r.texto.c_str());
}

void BackupWorkspaceComponent::exportarChecksums() {
    juce::FileChooser fc("Export Checksum Manifest...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.sha256");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("sha256");

        bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        // Destino selecionado (lido aqui, na message thread): é nele que o manifesto
        // hasheia; sem destino válido cai no checksum da origem.
        const juce::File destinoMedia = matriz::model::normalizarParaRaizDestino(
            resolvedDestFolder_.isDirectory() ? resolvedDestFolder_ : projeto_.projeto().raiz()).getChildFile("Media");

        juce::String manifest;
        ProgressThread thread("EXPORT METADATA - CHECKSUMS", [this, &manifest, isCatalogMode, destinoMedia](ProgressThread* t) {
            if (isCatalogMode || plano_.itens.empty()) {
                t->setStatusMessage("Generating SHA-256 Checksum Manifest...");
                t->setProgress(0.5);
                manifest = projeto_.exportarFixityManifest({}, "SHA-256");
                t->setProgress(1.0);
            } else {
                manifest = gerarManifestChecksumsBackup([t](int feito, int total) {
                    if (total > 0) {
                        t->setStatusMessage("Generating SHA-256 Checksum: " + juce::String(feito + 1) + " of " + juce::String(total) + "...");
                        t->setProgress(static_cast<double>(feito) / total);
                    }
                    return true;
                }, nullptr, destinoMedia);
            }
        });
        thread.runThread();

        targetFile.replaceWithText(manifest, false, false, "\n");  // LF: com CRLF o shasum -c lê "nome\r"
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT CHECKSUMS")
                .withMessage("Checksum manifest exported to:\n" + targetFile.getFullPathName()
                             + "\n\nVerify with:  sha256sum -c " + targetFile.getFileName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

void BackupWorkspaceComponent::exportarDublinCorePara(const juce::File& destFolder) {
    gravarRelatorioDublinCore(projeto_, plano_.itens, destFolder);
}

bool BackupWorkspaceComponent::exportarChecksumsPara(const juce::File& destFolder, const juce::File& destinoMedia,
                                                     const std::function<bool(int, int)>& onProgress) {
    bool cancelado = false;
    juce::String manifest = gerarManifestChecksumsBackup(onProgress, &cancelado, destinoMedia);
    return gravarManifestoChecksums(destFolder, juce::String(projeto_.projeto().nome()), manifest, cancelado);
}

void BackupWorkspaceComponent::aplicarEstiloBotao(juce::TextButton& botao, bool primario) {
    const auto& tk = tema();
    botao.setColour(juce::TextButton::buttonColourId, primario ? tk.acento : tk.botaoSecundario);
    botao.setColour(juce::TextButton::buttonOnColourId, tk.acento);
    botao.setColour(juce::TextButton::textColourOffId, primario ? tk.textoSobreAcento : tk.textoPrimario);
    botao.setColour(juce::TextButton::textColourOnId, tk.textoSobreAcento);
}

// Item 'duplicata' (já resolvido na aba Duplicates) nunca entra na seleção do backup/export: sai da contagem também.
std::set<std::string> BackupWorkspaceComponent::obterItensSelecionadosPeloCriterio() {
    auto ids = obterItensSelecionadosPeloCriterioBruto();
    try {
        auto st = projeto_.projeto().registro().prepare("SELECT id FROM item WHERE estado = 'duplicata'");
        while (st.step()) ids.erase(st.columnText(0));
    } catch (...) {}
    return ids;
}

std::set<std::string> BackupWorkspaceComponent::obterItensSelecionadosPeloCriterioBruto() {
    std::set<std::string> out;
    auto& db = projeto_.projeto().registro();

    if (whatOption_ == WhatOption::Everything) {
        auto stmt = db.prepare("SELECT id FROM item WHERE COALESCE(estado, '') != 'duplicata'");
        while (stmt.step()) out.insert(stmt.columnText(0));
    } else if (whatOption_ == WhatOption::Intake) {
        auto stmt = db.prepare("SELECT id FROM item WHERE criado_em >= datetime('now', '-24 hours')");
        while (stmt.step()) out.insert(stmt.columnText(0));
    } else if (whatOption_ == WhatOption::SelectedAssets) {
        return selectedItemIds_;
    } else if (whatOption_ == WhatOption::NeedsBackup) {
        auto stmt = db.prepare(
            "SELECT i.id FROM item i "
            "WHERE NOT EXISTS (SELECT 1 FROM consolidacao_registro cr WHERE cr.item_id = i.id)");
        while (stmt.step()) out.insert(stmt.columnText(0));
        return out;
    } else if (whatOption_ == WhatOption::Collection) {
        if (selectedContentIdx_ >= 0 && selectedContentIdx_ < static_cast<int>(opcoesContent_.size())) {
            const auto& opt = opcoesContent_[static_cast<size_t>(selectedContentIdx_)];
            if (opt.chave == "__empty__") {
                auto stmt = db.prepare(
                    "SELECT i.id FROM item i "
                    "WHERE (i.collection_type IS NULL OR TRIM(i.collection_type) = '') "
                    "  AND NOT EXISTS (SELECT 1 FROM item_campo ic WHERE ic.item_id = i.id AND ic.campo_id = 'collection_type' AND ic.valor IS NOT NULL AND TRIM(ic.valor) <> '')");
                while (stmt.step()) out.insert(stmt.columnText(0));
            } else {
                auto stmt = db.prepare(
                    "SELECT i.id FROM item i "
                    "WHERE i.collection_type = ? "
                    "   OR ( (i.collection_type IS NULL OR TRIM(i.collection_type) = '') "
                    "        AND EXISTS (SELECT 1 FROM item_campo ic WHERE ic.item_id = i.id AND ic.campo_id = 'collection_type' AND ic.valor = ?) )");
                stmt.bind(1, matriz::db::Value::of(opt.chave));
                stmt.bind(2, matriz::db::Value::of(opt.chave));
                while (stmt.step()) out.insert(stmt.columnText(0));
            }
        }
    }
    return out;
}

void BackupWorkspaceComponent::atualizarResumo() {
    // Dentro de recarregar()/construtor: só marca; a prévia sai uma vez no fim.
    if (resumoAdiado_ > 0) { resumoPendente_ = true; return; }
    MATRIZ_TRACE("BackupWorkspaceComponent::atualizarResumo");
    // Cada pedido invalida o cálculo em background anterior ainda em voo.
    const int geracao = ++*geracaoPlano_;
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (isCatalogMode) {
        auto colecoes = projeto_.listarColecoesLinkadas();
        uint64_t totalAssets = 0;
        juce::int64 totalBytes = 0;
        for (const auto& c : colecoes) {
            if (c.valido) {
                totalAssets += c.totalAssets;
                totalBytes += c.totalBytes;
            }
        }

        juce::String summary = (isPt ? juce::String::fromUTF8("COLEÇÕES (") : "COLLECTIONS (") + juce::String(static_cast<int>(colecoes.size())) + ")" +
                               (isPt ? juce::String::fromUTF8("  |  Itens Totais: ") : "  |  Total Assets: ") + juce::String(totalAssets) +
                               (isPt ? juce::String::fromUTF8("  |  Espaço: ") : "  |  Space: ") + juce::File::descriptionOfSizeInBytes(totalBytes);

        if (resolvedDestFolder_.getFullPathName().isEmpty()) {
            summary += isPt ? juce::String::fromUTF8("  -  (Selecione uma pasta de destino acima)") : "  -  (Select a destination folder above)";
            btnStartBackup_->setEnabled(false);
        } else if (colecoes.empty()) {
            summary += isPt ? juce::String::fromUTF8("  -  (Nenhuma coleção vinculada a este catálogo)") : "  -  (No collections linked to this catalog)";
            btnStartBackup_->setEnabled(false);
        } else {
            btnStartBackup_->setEnabled(true);
        }

        labelResumo_->setText(summary, juce::dontSendNotification);
        listPrevia_->definirColecoesCatalogo(colecoes, resolvedDestFolder_);
        listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);
        concluirPlano();
        return;
    }

    // O estado do MAIN não depende de destino destacado: recalcula sempre,
    // senão os botões de saída ficavam presos ao abrir um projeto com MAIN.
    atualizarTravasDoMain();

    if (resolvedDestFolder_.getFullPathName().isEmpty()) {
        labelResumo_->setText(isPt ? juce::String::fromUTF8("Selecione um destino para ver a prévia do backup.") : "Select a destination to see backup preview.", juce::dontSendNotification);
        plano_.itens.clear();
        listPrevia_->definirPlano(plano_);
        btnStartBackup_->setEnabled(false);
        concluirPlano();
        return;
    }

    std::set<std::string> itemIds = obterItensSelecionadosPeloCriterio();
    contagemSelecionados_ = static_cast<int>(itemIds.size());
    // atualizarTravasDoMain() já rodou acima: com MAIN, os controles têm as
    // escolhas do 1º backup antes de serem lidos aqui.

    matriz::consolidacao::HierarquiaBackup h;
    if (togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState()) {
        h = { matriz::consolidacao::NivelHierarquia::EstruturaOriginal };
    } else if (toggleUsarEstruturaMapa_ && toggleUsarEstruturaMapa_->getToggleState()) {
        h = { matriz::consolidacao::NivelHierarquia::PastaManual };
    } else {
        int orgId = comboOrg_ ? comboOrg_->getSelectedId() : 1;
        if (orgId == 1) h = { matriz::consolidacao::NivelHierarquia::PastaManual };
        else if (orgId == 2) h = { matriz::consolidacao::NivelHierarquia::TipoMidia };
        else if (orgId == 3) h = { matriz::consolidacao::NivelHierarquia::Ano };
        else if (orgId == 4) h = { matriz::consolidacao::NivelHierarquia::TipoMidia, matriz::consolidacao::NivelHierarquia::Ano };
        else if (orgId == 5) h = hierarquiaCustom_;
    }

    bool autoResolver = toggleAutoResolverConflitos_ ? toggleAutoResolverConflitos_->getToggleState() : true;
    bool forcarRebackup = toggleForcarRebackup_ ? toggleForcarRebackup_->getToggleState() : false;

    // planejarConsolidacao (SQL por item + existsAsFile no MAIN e na origem,
    // catálogo inteiro) roda em background; até o resultado chegar a interface
    // mostra "calculando" e o botão de backup fica desabilitado. Os controles
    // são lidos aqui, na message thread, e viajam por valor pro job.
    planoPronto_ = false;
    labelResumo_->setText(isPt ? juce::String::fromUTF8("Calculando prévia do backup...") : "Calculating backup preview...",
                          juce::dontSendNotification);
    btnStartBackup_->setEnabled(false);

    auto* registro = &projeto_.projeto().registro();
    const juce::File pastaProjeto = projeto_.projeto().pasta();
    const juce::File raizProjeto = projeto_.projeto().raiz();
    // Rebuild: o plano é sempre do destino novo (nunca de um destino clicado na lista).
    const juce::File destinoEscolhido = projeto_.emRebuild() ? projeto_.rebuildDestino() : resolvedDestFolder_;
    const auto modoPrefixo = modoPrefixo_;
    const juce::String prefixo = prefixoCustomizado_;
    const bool organizarPorSource = organizarPorSource_;
    const std::string mapaId = mapaParaBackup();
    const bool soPendentes = (whatOption_ == WhatOption::NeedsBackup);
    const bool rebuild = projeto_.emRebuild();
    auto geracaoAtomica = geracaoPlano_;
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

    poolPlano_.addJob([safeThis, geracao, geracaoAtomica, registro, pastaProjeto, raizProjeto, destinoEscolhido, h,
                       modoPrefixo, prefixo, autoResolver, forcarRebackup, organizarPorSource, mapaId, soPendentes, rebuild,
                       itemIds = std::move(itemIds)]() {
        if (geracaoAtomica->load() != geracao) return;  // superado antes de começar

        ResultadoPlano r;
        // planejarConsolidacao faz SQL cru contra colunas de metadado (dc_*,
        // collection_type) — um banco aberto antes de uma migração aditiva
        // rodar (ou qualquer outro erro de SQL) não pode derrubar o app inteiro
        // só por abrir a aba BACKUP; melhor mostrar o motivo no resumo.
        try {
            // Rebuild: o plano é do destino NOVO (ainda não escolhido = pasta fictícia que não existe):
            // nada conta como "já consolidado" a não ser o que já foi copiado pra lá.
            const juce::File destinoRaiz = rebuild
                ? (destinoEscolhido != juce::File() ? destinoEscolhido
                                                    : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bkr_rebuild_novo_main"))
                : matriz::model::normalizarParaRaizDestino(destinoEscolhido.isDirectory() ? destinoEscolhido : raizProjeto);
            const juce::File destinoMedia = destinoRaiz.getChildFile("Media");
            r.plano = matriz::consolidacao::planejarConsolidacao(
                *registro, pastaProjeto, destinoMedia, h, {}, modoPrefixo, prefixo,
                autoResolver, forcarRebackup, organizarPorSource, /*paraExport*/ false, mapaId, /*estritoAoDestino*/ rebuild);

            std::vector<matriz::consolidacao::ItemPlanejado> mv;
            // A reorganização vale pro MAIN inteiro: uma pasta renomeada não pode ficar meio no lugar antigo.
            for (auto& m : r.plano.movimentosSemPasta)
                if (m.reorganizacao || itemIds.count(m.itemId)) mv.push_back(std::move(m));
            r.plano.movimentosSemPasta = std::move(mv);
            std::vector<matriz::consolidacao::ItemPlanejado> filtrados;
            for (auto& item : r.plano.itens) {
                if (soPendentes && item.jaConsolidado) continue;
                if (itemIds.count(item.itemId)) {
                    filtrados.push_back(item);
                    if (!item.jaConsolidado) r.espacoACopiar += item.tamanhoBytes;
                    r.tamanhoTotal += item.tamanhoBytes;
                }
            }
            r.plano.itens = std::move(filtrados);
            r.plano.espacoNecessarioBytes = r.espacoACopiar;
            r.totalIdsSelecionados = itemIds.size();
        } catch (const std::exception& e) {
            r = {};
            r.erro = true;
            r.mensagemErro = juce::String(e.what());
        } catch (...) {
            r = {};
            r.erro = true;
        }

        juce::MessageManager::callAsync([safeThis, geracao, forcarRebackup, r = std::move(r)]() mutable {
            if (safeThis == nullptr) return;
            if (geracao != safeThis->geracaoPlano_->load()) return;  // superado por um cálculo mais novo
            safeThis->aplicarPlanoCalculado(std::move(r), forcarRebackup);
        });
    });
}

void BackupWorkspaceComponent::aplicarPlanoCalculado(ResultadoPlano r, bool forcarRebackup) {
    MATRIZ_TRACE("BackupWorkspaceComponent::aplicarPlanoCalculado");
    const bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (r.erro) {
        plano_ = {};
        labelResumo_->setText(
            (isPt ? juce::String::fromUTF8("Não foi possível calcular a prévia do backup: ")
                  : juce::String("Could not calculate the backup preview: ")) + r.mensagemErro,
            juce::dontSendNotification);
        listPrevia_->definirPlano(plano_);
        listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);
        btnStartBackup_->setEnabled(false);
        concluirPlano();
        return;
    }

    plano_ = std::move(r.plano);
    const juce::int64 sz = r.espacoACopiar;       // space to copy
    const juce::int64 totalSz = r.tamanhoTotal;   // total backup size
    const size_t totalIds = r.totalIdsSelecionados;

    juce::String summary = (isPt ? juce::String::fromUTF8("Itens: ") : "Assets: ") + juce::String(static_cast<int>(plano_.itens.size()));
    if (totalIds > plano_.itens.size()) {
        summary += isPt ? (juce::String::fromUTF8(" (de ") + juce::String(static_cast<int>(totalIds)) + juce::String::fromUTF8(" no catálogo)"))
                        : (" (of " + juce::String(static_cast<int>(totalIds)) + " in catalog)");
    }
    summary += (isPt ? juce::String::fromUTF8(" | Espaço: ") : " | Space: ") + juce::File::descriptionOfSizeInBytes(totalSz);

    if (plano_.conflitosAutoResolvidos > 0) {
        summary += (isPt ? juce::String::fromUTF8(" | ") : " | ") +
                   matriz::i18n::t("backup.conflitos_resolvidos").replace("{n}", juce::String(plano_.conflitosAutoResolvidos));
    }

    int semArquivo = static_cast<int>(totalIds - plano_.itens.size());
    if (semArquivo > 0) {
        summary += isPt ? (juce::String::fromUTF8("  -  (") + juce::String(semArquivo) + juce::String::fromUTF8(" itens de metadado não têm arquivos físicos)"))
                        : ("  -  (" + juce::String(semArquivo) + " metadata items have no physical files)");
    }

    // Botão desabilitado sem explicação é indistinguível de botão ausente —
    // o motivo vai junto do resumo sempre que o backup não puder rodar.
    const bool soMovimentos = plano_.itens.empty() && !plano_.movimentosSemPasta.empty();
    bool pronto = plano_.podeConsolidar() && (!plano_.itens.empty() || soMovimentos);
    if (!plano_.movimentosSemPasta.empty()) {
        const bool reorganiza = std::any_of(plano_.movimentosSemPasta.begin(), plano_.movimentosSemPasta.end(),
                                            [](const auto& m) { return m.reorganizacao; });
        summary += "  -  " + matriz::i18n::t(reorganiza ? "backup.reorganizar_resumo" : "backup.mover_sem_pasta_resumo")
                                 .replace("{n}", juce::String(static_cast<int>(plano_.movimentosSemPasta.size())));
    }
    if (plano_.itens.empty() && !soMovimentos)
        summary += isPt ? juce::String::fromUTF8("  -  NÃO É POSSÍVEL EXECUTAR: nenhum item corresponde à seleção.")
                        : "  -  CANNOT RUN: no assets match the selection.";
    else if (!plano_.podeConsolidar()) {
        bool preservando = togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState();
        if (preservando) {
            summary += isPt ? juce::String::fromUTF8("  -  (Conflito de nomes detectado na estrutura original)")
                            : "  -  (Naming conflict detected in original structure)";
        } else {
            int qtd = static_cast<int>(plano_.nomesEmConflito.size());
            summary += isPt ? (juce::String::fromUTF8("  -  NÃO É POSSÍVEL EXECUTAR: ") + juce::String(qtd) +
                               juce::String::fromUTF8(" arquivo(s) com conflito de nomes no destino. Resolva as duplicatas ou altere a estrutura de pastas."))
                            : ("  -  CANNOT RUN: " + juce::String(qtd) +
                               " file(s) with naming conflicts at destination. Resolve duplicates or change folder structure.");
        }
    } else if (sz == 0 && !plano_.itens.empty() && !forcarRebackup) {
        summary += isPt ? juce::String::fromUTF8("  (Destino 100% atualizado)")
                        : "  (Destination 100% up to date)";
    }

    labelResumo_->setText(summary, juce::dontSendNotification);

    listPrevia_->definirPlano(plano_);
    listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);

    // Backup só vai pro MAIN: com outra versão destacada o botão fica
    // desabilitado e o resumo diz por quê.
    if (!destacadoEhMain()) {
        summary += isPt ? juce::String::fromUTF8("  -  Backups vão só para o MAIN (a pasta do projeto). Selecione a linha MAIN; "
                                                 "outras versões são CLONE ou EXPORT.")
                        : "  -  Backups only go to the MAIN (the project folder). Select the MAIN row; "
                          "other versions are CLONE or EXPORT.";
        labelResumo_->setText(summary, juce::dontSendNotification);
    }
    // FAZER BACKUP antes do primeiro backup; depois, ADICIONAR AO MAIN
    // (mainSelado_ calculado em atualizarTravasDoMain).
    btnStartBackup_->setButtonText(matriz::i18n::t(mainSelado_ ? "backup.btn_adicionar_ao_main" : "backup.btn_fazer_backup"));
    btnStartBackup_->setEnabled(pronto && destacadoEhMain() && !projeto_.somenteLeitura());
    concluirPlano();
}

void BackupWorkspaceComponent::concluirPlano() {
    planoPronto_ = true;
    // Continuações que esperavam o plano (ex.: scan do destino); uma por vez,
    // porque cada uma pode pedir um plano novo e voltar a deixá-lo pendente.
    while (planoPronto_ && !aoPlanoPronto_.empty()) {
        auto fn = std::move(aoPlanoPronto_.front());
        aoPlanoPronto_.erase(aoPlanoPronto_.begin());
        fn();
    }
    if (planoPronto_ && popupConflitoAposPlano_) {
        popupConflitoAposPlano_ = false;
        if (togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState() && !plano_.podeConsolidar() &&
            !plano_.nomesEmConflito.empty())
            mostrarPopupConflitoPreservacao();
    }
}

void BackupWorkspaceComponent::aoPlanoPronto(std::function<void()> fn) {
    if (planoPronto_) fn();
    else aoPlanoPronto_.push_back(std::move(fn));
}

void BackupWorkspaceComponent::visibilityChanged() {
    if (!escondido()) agendarAplicarPendentes();
}

void BackupWorkspaceComponent::agendarAplicarPendentes() {
    if (aplicacaoPendenteAgendada_ || (!botoesPendentes_ && !resumoDeTituloPendente_)) return;
    aplicacaoPendenteAgendada_ = true;
    // Adiado um passo: quem torna a tela visível costuma chamar recarregar() logo em
    // seguida (que já refaz tudo e zera as pendências) — assim não se calcula duas vezes.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<BackupWorkspaceComponent>(this)] {
        if (safe == nullptr) return;
        safe->aplicacaoPendenteAgendada_ = false;
        if (safe->escondido()) return;
        const bool botoes = std::exchange(safe->botoesPendentes_, false);
        const bool resumo = std::exchange(safe->resumoDeTituloPendente_, false);
        if (botoes) safe->atualizarBotoesListas();
        if (resumo) safe->atualizarResumo();
    });
}

void BackupWorkspaceComponent::atualizarTravasDoMain() {
    // FAZER BACKUP antes do primeiro backup; depois, ADICIONAR AO MAIN.
    mainSelado_ = false;
    juce::var cfg;
    try {
        auto st = projeto_.projeto().registro().prepare(
            "SELECT 1 FROM consolidacao_registro WHERE COALESCE(destino_id, '') = '' OR destino_id = ? LIMIT 1");
        st.bind(1, matriz::db::Value::of(destinoIdDoMain()));
        mainSelado_ = st.step() && !projeto_.emRebuild();  // rebuild: projeto sem MAIN (o antigo só é fonte)
        if (mainSelado_) {
            auto sc = projeto_.projeto().registro().prepare("SELECT COALESCE(backup_config_main, '') FROM projeto LIMIT 1");
            if (sc.step()) cfg = juce::JSON::parse(juce::String::fromUTF8(sc.columnText(0).c_str()));
        }
    } catch (...) {}
    configTravada_ = mainSelado_ && cfg.isObject();
    // MAIN antigo (sem config gravada ou gravada sem a flag) segue a regra que já tinha.
    organizarPorSource_ = configTravada_ ? static_cast<bool>(cfg.getProperty("por_source", false)) : !mainSelado_;

    if (configTravada_) {
        const bool preservar = cfg.getProperty("preservar", false);
        const bool mapa = cfg.getProperty("mapa", false);
        const int org = cfg.getProperty("org", 1);
        const int modo = cfg.getProperty("modo_prefixo", 1);
        if (togglePreservarEstrutura_) togglePreservarEstrutura_->setToggleState(preservar, juce::dontSendNotification);
        if (toggleUsarEstruturaMapa_) toggleUsarEstruturaMapa_->setToggleState(mapa, juce::dontSendNotification);
        if (comboOrg_) comboOrg_->setSelectedId(org, juce::dontSendNotification);
        if (org == 5)
            hierarquiaCustom_ = matriz::consolidacao::hierarquiaDeCsv(cfg.getProperty("hierarquia", "").toString().toStdString());
        prefixoCustomizado_ = cfg.getProperty("prefixo", prefixoCustomizado_).toString();
        if (editPrefixo_) editPrefixo_->setText(prefixoCustomizado_, juce::dontSendNotification);
        if (comboModoPrefixo_) comboModoPrefixo_->setSelectedId(modo, juce::dontSendNotification);
        modoPrefixo_ = modo == 2 ? matriz::consolidacao::ModoPrefixoArquivo::Auto
                     : modo == 3 ? matriz::consolidacao::ModoPrefixoArquivo::Custom
                                 : matriz::consolidacao::ModoPrefixoArquivo::Nenhum;
        if (editPrefixo_) editPrefixo_->setVisible(modo == 3);
    }

    recarregarComboMapaMain();
    const bool livre = !configTravada_;
    const bool usaOriginal = togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState();
    if (togglePreservarEstrutura_) togglePreservarEstrutura_->setEnabled(livre);
    if (toggleUsarEstruturaMapa_) toggleUsarEstruturaMapa_->setEnabled(livre);
    if (comboMapaMain_)
        comboMapaMain_->setEnabled(livre && toggleUsarEstruturaMapa_ && toggleUsarEstruturaMapa_->getToggleState());
    if (comboOrg_) comboOrg_->setEnabled(livre && !usaOriginal);
    if (btnEditarHierarquia_) btnEditarHierarquia_->setEnabled(livre && !usaOriginal);
    if (comboModoPrefixo_) comboModoPrefixo_->setEnabled(livre);
    if (editPrefixo_) editPrefixo_->setEnabled(livre);
    const juce::String sufixo = configTravada_ ? "  -  " + matriz::i18n::t("backup.definido_primeiro_backup") : juce::String();
    if (labelOrg_) labelOrg_->setText(matriz::i18n::t("backup.secao_organizacao") + sufixo, juce::dontSendNotification);
    // O aviso fica no título da seção; no rótulo estreito do prefixo, só na dica.
    if (comboModoPrefixo_)
        comboModoPrefixo_->setTooltip(configTravada_ ? matriz::i18n::t("backup.definido_primeiro_backup")
                                                     : matriz::i18n::t("backup.prefixo_modo_nenhum_dica"));

    // Embed e "forçar backup completo" só existem antes do MAIN: depois o
    // MAIN só recebe arquivos novos, byte a byte (conjunto novo = CLONE).
    const bool mostrarOpcoesPrimeiroBackup = !mainSelado_;
    if (btnAtualizarSidecars_ && btnAtualizarSidecars_->isVisible() != mainSelado_) {
        btnAtualizarSidecars_->setVisible(mainSelado_);
        if (configContainer_) configContainer_->resized();
    }
    for (auto* t : {toggleForcarRebackup_.get()}) {
        if (!t) continue;
        if (!mostrarOpcoesPrimeiroBackup) t->setToggleState(false, juce::dontSendNotification);
        if (t->isVisible() != mostrarOpcoesPrimeiroBackup) {
            t->setVisible(mostrarOpcoesPrimeiroBackup);
            if (configContainer_) configContainer_->resized();
        }
    }
    atualizarBotoesDependentesDoMain();
}

bool BackupWorkspaceComponent::saidaBloqueadaSemMain() const {
    return projeto_.projeto().modo() != matriz::model::Modo::Catalogo && !mainSelado_;
}

void BackupWorkspaceComponent::atualizarBotoesDependentesDoMain() {
    atualizarExportUnificado();
    // Modo Catálogo não tem MAIN próprio (cada coleção tem o seu): lá esses
    // botões seguem as regras de antes (resized / atualizarBotoesListas).
    if (projeto_.projeto().modo() == matriz::model::Modo::Catalogo) return;
    const bool isPt = matriz::i18n::localeAtivo() == "pt_BR";
    const bool liberado = mainSelado_;
    const juce::String dicaTravada = matriz::i18n::t("backup.saida_sem_main");
    auto aplicar = [&](juce::Button* b, bool extra, const juce::String& dicaNormal) {
        if (b == nullptr) return;
        b->setEnabled(liberado && extra);
        b->setTooltip(liberado ? dicaNormal : dicaTravada);
    };
    aplicar(btnPublishHtml_.get(), true, "Publish static HTML website preview / catalog");
    aplicar(btnExportar_.get(), !exportando_, matriz::i18n::t("export.dica"));
    aplicar(btnExportJanela_.get(), true,
            isPt ? juce::String::fromUTF8("Exportar catálogo de metadados como CSV/XLS") : "Export metadata catalog as CSV/XLS");
    aplicar(btnSyncDestino_.get(), true,
            isPt ? juce::String::fromUTF8("Comparar a versão ativa com outro disco/pasta para sincronização manual")
                 : "Compare the active version with another drive/folder for manual sync review");
}

bool BackupWorkspaceComponent::hierarquiaUsaMapa() const {
    if (togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState()) return false;
    if (toggleUsarEstruturaMapa_ && toggleUsarEstruturaMapa_->getToggleState()) return true;
    const int orgId = comboOrg_ ? comboOrg_->getSelectedId() : 1;
    if (orgId == 1) return true;
    return orgId == 5 && std::find(hierarquiaCustom_.begin(), hierarquiaCustom_.end(),
                                   matriz::consolidacao::NivelHierarquia::PastaManual) != hierarquiaCustom_.end();
}

std::string BackupWorkspaceComponent::mapaParaBackup() const {
    if (!hierarquiaUsaMapa()) return {};
    if (configTravada_) return projeto_.mapaDoMainId();  // "" = MAIN por regra: sem mapa
    if (!comboMapaMain_) return {};
    const int idx = comboMapaMain_->getSelectedId() - 1;
    return idx >= 0 && idx < static_cast<int>(idsComboMapaMain_.size()) ? idsComboMapaMain_[static_cast<size_t>(idx)]
                                                                         : std::string();
}

// Só mapas do usuário. Travado (MAIN com config): mostra só o mapa do MAIN,
// pelo nome ATUAL (o id é o que fica gravado).
void BackupWorkspaceComponent::recarregarComboMapaMain() {
    if (!comboMapaMain_) return;
    std::string selecionadoAntes;
    {
        const int idx = comboMapaMain_->getSelectedId() - 1;
        if (idx >= 0 && idx < static_cast<int>(idsComboMapaMain_.size()))
            selecionadoAntes = idsComboMapaMain_[static_cast<size_t>(idx)];
    }
    comboMapaMain_->clear(juce::dontSendNotification);
    idsComboMapaMain_.clear();
    if (configTravada_) {
        const auto idMain = projeto_.mapaDoMainId();
        if (!idMain.empty()) {
            idsComboMapaMain_.push_back(idMain);
            comboMapaMain_->addItem(projeto_.nomeDoMapaDoMain(), 1);
            comboMapaMain_->setSelectedId(1, juce::dontSendNotification);
        } else {
            comboMapaMain_->setText(matriz::i18n::t("backup.mapa_main_nenhum"), juce::dontSendNotification);
        }
        return;
    }
    int n = 0;
    for (auto& m : projeto_.listarFolderMaps()) {
        if (m.original) continue;
        idsComboMapaMain_.push_back(m.id);
        comboMapaMain_->addItem(m.nome, ++n);
    }
    // Padrão: MAIN legado sem config = primeiro mapa (o antigo mapa único);
    // senão o mapa aberto no Folder Map.
    std::string alvo = selecionadoAntes;
    if (alvo.empty()) alvo = mainSelado_ && !idsComboMapaMain_.empty() ? idsComboMapaMain_.front() : projeto_.mapaAtivoPadrao();
    int selecionar = 1;
    for (size_t i = 0; i < idsComboMapaMain_.size(); ++i)
        if (idsComboMapaMain_[i] == alvo) selecionar = static_cast<int>(i) + 1;
    if (!idsComboMapaMain_.empty()) comboMapaMain_->setSelectedId(selecionar, juce::dontSendNotification);
}

void BackupWorkspaceComponent::gravarConfigDoMain() {
    if (configTravada_) return;  // já definido no primeiro backup
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("preservar", togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState());
    o->setProperty("mapa", toggleUsarEstruturaMapa_ && toggleUsarEstruturaMapa_->getToggleState());
    o->setProperty("org", comboOrg_ ? comboOrg_->getSelectedId() : 1);
    o->setProperty("hierarquia", juce::String(matriz::consolidacao::hierarquiaParaCsv(hierarquiaCustom_)));
    o->setProperty("modo_prefixo", comboModoPrefixo_ ? comboModoPrefixo_->getSelectedId() : 1);
    o->setProperty("prefixo", prefixoCustomizado_);
    o->setProperty("por_source", organizarPorSource_);
    // Fase 2: guarda o ID do mapa (nunca o nome — o mapa pode ser renomeado).
    if (hierarquiaUsaMapa()) o->setProperty("mapa_id", juce::String(mapaParaBackup()));
    try {
        projeto_.projeto().registro().run("UPDATE projeto SET backup_config_main = ?",
                                          {matriz::db::Value::of(juce::JSON::toString(juce::var(o.get()), true).toStdString())});
    } catch (...) {}
}

void BackupWorkspaceComponent::gravarRascunhoOrganizacao() {
    if (configTravada_ || aplicandoRascunho_) return;
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("preservar", togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState());
    o->setProperty("mapa", toggleUsarEstruturaMapa_ && toggleUsarEstruturaMapa_->getToggleState());
    o->setProperty("org", comboOrg_ ? comboOrg_->getSelectedId() : 1);
    o->setProperty("hierarquia", juce::String(matriz::consolidacao::hierarquiaParaCsv(hierarquiaCustom_)));
    // Guarda o mapa escolhido mesmo com a hierarquia atual não usando mapa
    // (o usuário pode voltar a ela); mapaParaBackup() devolveria vazio.
    {
        const int idx = comboMapaMain_ ? comboMapaMain_->getSelectedId() - 1 : -1;
        const bool valido = idx >= 0 && idx < static_cast<int>(idsComboMapaMain_.size());
        o->setProperty("mapa_id", juce::String(valido ? idsComboMapaMain_[static_cast<size_t>(idx)] : std::string()));
    }
    try {
        // Undo (a aba relê o rascunho em recarregar(), que o executarUndo chama).
        projeto_.registrarUndoDeColunas("Backup Options", "projeto", "id", projeto_.projeto().projetoId(), {"backup_config_rascunho"});
        projeto_.projeto().registro().run("UPDATE projeto SET backup_config_rascunho = ?",
                                          {matriz::db::Value::of(juce::JSON::toString(juce::var(o.get()), true).toStdString())});
    } catch (...) {}
}

void BackupWorkspaceComponent::aplicarRascunhoOrganizacao() {
    if (configTravada_) return;  // depois do 1º backup vale backup_config_main
    if (projeto_.projeto().modo() == matriz::model::Modo::Catalogo) return;  // sem seção de estrutura
    juce::var cfg;
    try {
        auto st = projeto_.projeto().registro().prepare("SELECT COALESCE(backup_config_rascunho, '') FROM projeto LIMIT 1");
        if (st.step()) cfg = juce::JSON::parse(juce::String::fromUTF8(st.columnText(0).c_str()));
    } catch (...) {}
    if (!cfg.isObject()) return;

    const bool preservar = cfg.getProperty("preservar", false);
    const bool mapa = cfg.getProperty("mapa", false);
    const int org = juce::jlimit(1, 5, static_cast<int>(cfg.getProperty("org", 1)));
    const auto hierarquia = cfg.getProperty("hierarquia", "").toString().toStdString();
    const std::string mapaId = cfg.getProperty("mapa_id", "").toString().toStdString();

    aplicandoRascunho_ = true;
    if (togglePreservarEstrutura_) togglePreservarEstrutura_->setToggleState(preservar, juce::dontSendNotification);
    if (toggleUsarEstruturaMapa_) toggleUsarEstruturaMapa_->setToggleState(mapa, juce::dontSendNotification);
    if (comboOrg_) comboOrg_->setSelectedId(org, juce::dontSendNotification);
    if (org == 5 && !hierarquia.empty()) hierarquiaCustom_ = matriz::consolidacao::hierarquiaDeCsv(hierarquia);
    if (comboMapaMain_ && !mapaId.empty()) {
        for (size_t i = 0; i < idsComboMapaMain_.size(); ++i)
            if (idsComboMapaMain_[i] == mapaId) comboMapaMain_->setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    }
    aplicandoRascunho_ = false;

    // Mesmo efeito visual do atualizarEstadoOrganizacao: combo/botão do editor visual.
    const bool usaOriginal = preservar;
    if (comboOrg_) comboOrg_->setEnabled(!usaOriginal);
    if (comboMapaMain_) comboMapaMain_->setEnabled(mapa);
    if (btnEditarHierarquia_) {
        const bool isCustom = org == 5;
        btnEditarHierarquia_->setVisible(isCustom && !usaOriginal);
        btnEditarHierarquia_->setEnabled(isCustom && !usaOriginal);
    }
    if (configContainer_) configContainer_->resized();
    resized();
}

void BackupWorkspaceComponent::mostrarPopupConflitoPreservacao() {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    int qtd = static_cast<int>(plano_.nomesEmConflito.size());

    PainelOverlay::Config cfg;
    cfg.titulo = isPt ? juce::String::fromUTF8("CONFLITO DE ARQUIVOS NO DESTINO")
                      : "DESTINATION FILE CONFLICT";

    juce::String msg;
    if (isPt) {
        msg = juce::String::fromUTF8(
            "Ao optar por 'Preservar estrutura original de pastas', foram identificados ")
            + juce::String(qtd) + juce::String::fromUTF8(" arquivo(s) cujos nomes geram colisão no destino.\n\n"
            "O processo de backup não pode sobrescrever arquivos com o mesmo nome.\n\n"
            "Deseja ir para a aba de Duplicatas para escanear, validar e resolver os arquivos conflitantes?");
    } else {
        msg = juce::String(
            "When choosing 'Preserve original folder structure', ")
            + juce::String(qtd) + " file(s) collide at the destination with identical names and paths.\n\n"
            "The backup process cannot overwrite destination files.\n\n"
            "Would you like to go to the Duplicates tab to scan, inspect, and resolve these conflicting files?";
    }
    cfg.mensagem = msg;
    cfg.botoes = {
        { isPt ? "IR PARA DUPLICATAS" : "GO TO DUPLICATES", 1, true, false },
        { isPt ? "VOLTAR" : "RETURN", 2, false, true }
    };

    overlay_.mostrar(cfg, [this](PainelOverlay::Resultado res) {
        if (res.botaoId == 1) {
            if (aoPedirIrParaDuplicatas) {
                aoPedirIrParaDuplicatas();
            }
        }
    });
}

void BackupWorkspaceComponent::iniciarBackup() {
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    if (projeto_.somenteLeitura()) { ProjetoAberto::avisarSomenteLeitura(); return; }  // clone: sem backup
    if (!planoPronto_) return;  // plano ainda calculando em background: nada de backup com plano velho

    // Backup só vai pro MAIN (a pasta do projeto) — outros destinos são CLONE
    // (sincronização) ou EXPORT. O botão já fica desabilitado fora do MAIN.
    if (!isCatalogMode && !destacadoEhMain()) return;

    // REBUILD DO MAIN: a pasta nova é escolhida (e validada) aqui, uma vez; retomar reaproveita a mesma.
    const bool rebuild = !isCatalogMode && projeto_.emRebuild();
    std::optional<matriz::consolidacao::OpcoesRebuild> opcoesRebuild;
    if (rebuild) {
        const juce::File novo = projeto_.rebuildDestino();
        if (novo == juce::File()) { pedirDestinoRebuild(); return; }
        const auto fonte = projeto_.fonteRebuild();
        juce::String motivo;
        if (!fonte || !validarDestinoRebuild(novo, motivo)) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("rebuild.titulo"),
                                                   fonte ? motivo : juce::String("old MAIN (FONTE_REBUILD) not found"));
            return;
        }
        novo.createDirectory();
        // O acervo inteiro (o que ainda falta copiar) precisa caber no destino novo.
        const juce::int64 livre = novo.getBytesFreeOnVolume();
        if (!novo.isDirectory() || (livre > 0 && livre < plano_.espacoNecessarioBytes)) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, matriz::i18n::t("rebuild.titulo"),
                matriz::i18n::t("rebuild.sem_espaco")
                    .replace("{preciso}", juce::File::descriptionOfSizeInBytes(plano_.espacoNecessarioBytes))
                    .replace("{livre}", juce::File::descriptionOfSizeInBytes(std::max<juce::int64>(0, livre))));
            return;
        }
        resolvedDestFolder_ = novo;
        customDestFolder_ = novo;
        matriz::consolidacao::OpcoesRebuild o;
        o.fonteMedia = fonte->raiz.getChildFile("Media");
        o.fonteDestinoId = fonte->destinoId;
        o.descricaoFonte = fonte->raiz.getFullPathName().toStdString();
        opcoesRebuild = o;
        relatorioRebuild_.clear();
    }

    // Fase 2: itens de _SEM_PASTA que ganharam pasta no mapa do MAIN — oferece
    // mover (lista + confirmação) antes de qualquer cópia.
    if (!isCatalogMode && !movimentosDecididos_ && !plano_.movimentosSemPasta.empty()) {
        perguntarMoverSemPasta();
        return;
    }
    const bool moverAgora = !isCatalogMode && moverSemPasta_;
    movimentosDecididos_ = false;
    moverSemPasta_ = false;

    estado_ = Estado::Running;
    executando_ = true;
    cancelamento_->rearmar();
    progressoValor_ = 0.0;

    btnStartBackup_->setEnabled(false);
    barraProgresso_->setVisible(isCatalogMode);  // coleção usa os cards COPY/CATALOG; catálogo mantém a barra única
    etapaCopy_->setVisible(!isCatalogMode);
    etapaCatalog_->setVisible(!isCatalogMode);
    etapaCopy_->definir(-1.0, matriz::i18n::t("backup.progresso_preparando"));
    etapaCatalog_->esperar(matriz::i18n::t("backup.etapa_aguardando"));
    labelProgressoStatus_->setVisible(true);
    labelProgressoStatus_->setText(matriz::i18n::t("backup.progresso_preparando"), juce::dontSendNotification);

    auto cancelamento = cancelamento_;
    auto plano = plano_;
    auto& projeto = projeto_;
    bool gerarCatalogo = toggleGerarCatalogo_->getToggleState();
    // O MAIN é fiel à SOURCE: o backup nunca embute metadados/marcadores (isso é só do EXPORT).
    const bool embutirMeta = false;
    if (!isCatalogMode) gravarConfigDoMain();  // estas escolhas passam a valer pra todo arquivo novo

    juce::File destinoRaiz = matriz::model::normalizarParaRaizDestino(
        resolvedDestFolder_.isDirectory() ? resolvedDestFolder_ : (isCatalogMode ? resolvedDestFolder_ : projeto.projeto().raiz()));
    juce::File destinoMedia = destinoRaiz.getChildFile("Media");
    destinoRaiz.createDirectory();
    destinoMedia.createDirectory();
    matriz::model::sanitizarEstruturaDestino(destinoRaiz);

    // Coloca os arquivos do projeto (.mtz ou .bkm) DENTRO da pasta principal do backup (Tarefa 11)
    juce::File pastaBackupRaiz = destinoRaiz;
    pastaBackupRaiz.createDirectory();

    juce::String extProj = isCatalogMode ? ".bkm" : ".mtz";
    juce::String nomeDoProjeto = juce::String::fromUTF8(projeto.projeto().nome().c_str());
    juce::File arqProjBackup = pastaBackupRaiz.getChildFile(juce::File::createLegalFileName(nomeDoProjeto) + extProj);
    // Rename Project (item 5, 2026-09-28): reconstruir o nome do arquivo
    // original a partir do nome AO VIVO falha depois de um rename (o
    // arquivo real ainda tem o nome antigo) — cai no synthetic abaixo, que
    // reinventa criado_em/ids em vez de copiar o de verdade. Acha por
    // extensão na raiz do projeto, igual já faz a cópia pro CLONE logo mais
    // abaixo nesta função.
    juce::Array<juce::File> arqsProjOrig;
    projeto.projeto().raiz().findChildFiles(arqsProjOrig, juce::File::findFiles, false, "*" + extProj);
    juce::File arqProjOrig = arqsProjOrig.isEmpty() ? juce::File() : arqsProjOrig.getFirst();
    if (arqProjOrig.existsAsFile()) {
        arqProjOrig.copyFileTo(arqProjBackup);
    } else {
        juce::DynamicObject::Ptr projObj = new juce::DynamicObject();
        projObj->setProperty("formato", 1);
        projObj->setProperty("modo", isCatalogMode ? "catalogo" : "preservacao");
        projObj->setProperty("nome", nomeDoProjeto);
        projObj->setProperty("projeto_id", juce::String(projeto.projeto().projetoId()));
        projObj->setProperty("destination_id", juce::String(projeto.projeto().destinationId()));
        projObj->setProperty("criado_em", juce::Time::getCurrentTime().toISO8601(true));
        arqProjBackup.replaceWithText(juce::JSON::toString(juce::var(projObj.get()), false));
    }
    // Garante destination.json e Project/ com SQLite no backup
    juce::File destJsonBackup = pastaBackupRaiz.getChildFile("destination.json");
    juce::File destJsonOrig = projeto.projeto().raiz().getChildFile("destination.json");
    if (rebuild) {
        // MAIN novo = destino NOVO (identidade própria; nunca copia o destination.json do MAIN antigo).
        auto existente = matriz::model::DestinationInfo::lerDeArquivo(destJsonBackup);
        if (!existente || existente->projetoId != projeto.projeto().projetoId() || existente->papel != "ORIGINAL") {
            matriz::model::DestinationInfo dInfo;
            dInfo.formato = 1;
            dInfo.destinationId = matriz::model::novoUuid();
            dInfo.projetoId = projeto.projeto().projetoId();
            dInfo.papel = "ORIGINAL";
            dInfo.rotulo = pastaBackupRaiz.getFileName().toStdString();
            dInfo.revisao = projeto.projeto().revisao();
            dInfo.ultimaEdicaoUtc = matriz::model::agoraIso8601();
            dInfo.criadoEm = matriz::model::agoraIso8601();
            dInfo.gravarEmArquivo(destJsonBackup);
        }
    } else if (destJsonOrig.existsAsFile()) {
        destJsonOrig.copyFileTo(destJsonBackup);
    } else {
        matriz::model::DestinationInfo dInfo;
        dInfo.formato = 1;
        dInfo.destinationId = matriz::model::novoUuid();
        dInfo.projetoId = projeto.projeto().projetoId();
        dInfo.papel = "CLONE";
        dInfo.rotulo = pastaBackupRaiz.getFileName().toStdString();
        dInfo.revisao = projeto.projeto().revisao();
        dInfo.ultimaEdicaoUtc = matriz::model::agoraIso8601();
        dInfo.criadoEm = matriz::model::agoraIso8601();
        dInfo.gravarEmArquivo(destJsonBackup);
    }

    juce::File projectSubBackup = pastaBackupRaiz.getChildFile("Project");
    if (!projectSubBackup.exists()) projectSubBackup.createDirectory();
    juce::File regBackup = projectSubBackup.getChildFile("registro.sqlite");
    juce::File indBackup = projectSubBackup.getChildFile("indice.sqlite");
    juce::File regOrig = projeto.projeto().pasta().getChildFile("registro.sqlite");
    juce::File indOrig = projeto.projeto().pasta().getChildFile("indice.sqlite");
    if (regOrig.existsAsFile()) regOrig.copyFileTo(regBackup);
    if (indOrig.existsAsFile()) indOrig.copyFileTo(indBackup);

    if (isCatalogMode) {
        auto colecoes = projeto.listarColecoesLinkadas();
        ProgressoGlobal::obterInstancia().iniciarTarefa(
            "backup",
            matriz::i18n::t("backup.progresso_titulo_catalogo"),
            static_cast<int>(colecoes.size()),
            [cancelamento] { cancelamento->pedir(); },
            matriz::i18n::t("backup.progresso_preparando_catalogo"),
            /*temModalProprio*/ true);

        resized();
        repaint();

        juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

        // Cópia das pastas de coleção num job do poolExport_ (o destrutor do workspace espera por ele): copiar uma
        // coleção inteira pode levar minutos e antes travava a message thread. A UI só volta via callAsync.
        ProjetoAberto* pProjeto = &projeto;
        auto guarda = projeto.guardaConsolidacao();  // fechar o projeto fica travado até o job terminar
        BackupWorkspaceComponent* self = this;
        cancelarExport_->store(false);

        poolExport_.addJob([self, safeThis, cancelamento, colecoes, destinoRaiz, destinoMedia, pProjeto, guarda]() {
            int copiado = 0;
            int falhas = 0;
            std::vector<juce::String> falhasLista;
            auto parar = [cancelamento, self] { return cancelamento->pedido() || self->cancelarExport_->load(); };

            destinoMedia.createDirectory();

            for (size_t i = 0; i < colecoes.size(); ++i) {
                if (parar()) break;
                const auto& c = colecoes[i];
                if (!c.valido) continue;

                juce::File colOrigem(c.caminhoProjeto);
                juce::File colDestino = destinoMedia.getChildFile(c.nome);

                juce::MessageManager::callAsync([safeThis, i, n = colecoes.size(), nome = c.nome] {
                    if (!safeThis) return;
                    safeThis->progressoValor_ = static_cast<double>(i) / std::max<size_t>(1, n);
                    safeThis->labelProgressoStatus_->setText("Backing up collection " + juce::String(i + 1) + " of " + juce::String(n) + ": " + nome + "...", juce::dontSendNotification);
                    ProgressoGlobal::obterInstancia().atualizarProgresso("backup", static_cast<int>(i), "Backing up: " + nome);
                });

                if (colOrigem.isDirectory()) {
                    if (colDestino.exists()) colDestino.deleteRecursively();
                    if (!colOrigem.copyDirectoryTo(colDestino)) {
                        falhas++;
                        falhasLista.push_back("Failed to copy collection folder: " + c.nome);
                    } else {
                        copiado++;
                    }
                }
            }

            // Copy catalog database as well into Project/
            juce::File catOrigemDb = pProjeto->projeto().pasta().getChildFile("registro.sqlite");
            if (catOrigemDb.existsAsFile()) {
                juce::File projectSubBackup = destinoRaiz.getChildFile("Project");
                projectSubBackup.createDirectory();
                catOrigemDb.copyFileTo(projectSubBackup.getChildFile("registro.sqlite"));
            }
            const bool cancelado = parar();

            juce::MessageManager::callAsync([safeThis, copiado, falhas, falhasLista, destinoRaiz, cancelado] {
                if (!safeThis) return;
                safeThis->copiadoCount_ = copiado;
                safeThis->verificadoCount_ = copiado;
                safeThis->falhasCount_ = falhas;
                safeThis->falhasLista_ = falhasLista;

                safeThis->registrarDestinoBackup(destinoRaiz, "Catalog Backup", copiado, 0, falhas, cancelado);

                safeThis->executando_ = false;
                safeThis->estado_ = Estado::Done;
                safeThis->progressoValor_ = 1.0;

                const bool houveFalha = (falhas > 0);
                juce::String msgFinal = cancelado ? "Backup cancelled."
                                                  : (houveFalha ? "Catalog backup completed with errors."
                                                                : "Catalog backup completed successfully (" + juce::String(copiado) + " collections).");
                ProgressoGlobal::obterInstancia().concluirTarefa("backup", msgFinal);

                safeThis->labelProgressoStatus_->setText(msgFinal, juce::dontSendNotification);
                safeThis->labelProgressoStatus_->setColour(
                    juce::Label::textColourId,
                    houveFalha ? tema().perigo : (cancelado ? tema().alerta : tema().estadoQcOk));

                safeThis->mostrarControlesConfig(false);
                safeThis->resized();
                safeThis->repaint();
            });
        });
        return;
    }

    // temModalProprio: esta aba já mostra a própria barra (e o CANCEL); o modal global duplicava a barra e
    // ficava parado em 0% por cima do "Backup completed".
    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "backup",
        matriz::i18n::t("backup.progresso_titulo"),
        static_cast<int>(plano.itens.size()),
        [cancelamento] { cancelamento->pedir(); },
        matriz::i18n::t("backup.progresso_preparando"),
        /*temModalProprio*/ true);

    resized();
    repaint();

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

    // Cópia, catálogo e relatórios rodam num job do poolExport_ (o destrutor do workspace espera por ele, cancelando
    // entre arquivos). Antes isto rodava na message thread com runDispatchLoopUntil(1) entre arquivos: o Windows marca
    // o app como "Não está respondendo" após ~5 s. A UI só é tocada via callAsync com SafePointer; `safeThis` nunca
    // é dereferenciado dentro do job. Fechar o projeto fica travado (guardaConsolidacao) até o job terminar.
    BackupWorkspaceComponent* self = this;
    ProjetoAberto* pProjeto = &projeto;
    std::weak_ptr<bool> vivoProjeto = projeto.tokenVida();
    auto guarda = projeto.guardaConsolidacao();
    cancelarExport_->store(false);

    poolExport_.addJob([self, safeThis, cancelamento, plano, destinoRaiz, destinoMedia, pProjeto, vivoProjeto, guarda,
                        gerarCatalogo, embutirMeta, moverAgora, opcoesRebuild]() {
        matriz::app::SemSono semSono("BKR Matriz: Make Backup");
        matriz::consolidacao::limparTemporariosDeCopia(destinoMedia);

        // modo: 0 = só o detalhe da tarefa; 1 = progresso + barra (COPY); 2 = progresso + barra (CATALOG);
        // 3 = detalhe + barra CATALOG indeterminada.
        // No máximo a cada 100 ms, para não inundar a fila de eventos em backups com milhares de arquivos.
        auto ultimoAviso = std::make_shared<juce::uint32>(0);  // só a thread do job lê/escreve
        auto aviso = [safeThis, ultimoAviso](const juce::String& texto, int feito, int total, int modo) {
            const auto agora = juce::Time::getMillisecondCounter();
            if (feito < total && agora - *ultimoAviso < 100) return;
            *ultimoAviso = agora;
            juce::MessageManager::callAsync([safeThis, texto, feito, total, modo] {
                if (!safeThis) return;
                if (modo == 1) safeThis->progressoValor_ = static_cast<double>(feito) / std::max(1, total);
                safeThis->labelProgressoStatus_->setText(texto, juce::dontSendNotification);
                if (modo == 1) safeThis->etapaCopy_->definir(safeThis->progressoValor_, "Copying", feito, total);
                else if (modo == 2)
                    safeThis->etapaCatalog_->definir(static_cast<double>(feito) / std::max(1, total), "Cataloging", feito, total);
                else if (modo == 3) safeThis->etapaCatalog_->definir(-1.0, texto.trimCharactersAtEnd("."));
                if (modo == 0 || modo == 3) ProgressoGlobal::obterInstancia().atualizarDetalhe("backup", texto);
                else ProgressoGlobal::obterInstancia().atualizarProgresso("backup", feito, texto);
            });
        };

        // Fase 2: moves de _SEM_PASTA primeiro (rename no mesmo volume, sem recópia).
        matriz::consolidacao::ResultadoMovimentos movs;
        if (moverAgora) {
            movs = matriz::consolidacao::executarMovimentosSemPasta(
                pProjeto->projeto().registro(), pProjeto->projeto().pasta(), destinoMedia, plano.movimentosSemPasta,
                [aviso, cancelamento, self](int feito, int total) {
                    aviso("Moving " + juce::String(feito) + " of " + juce::String(total) + " inside the MAIN...", feito, total, 0);
                    return !cancelamento->pedido() && !self->cancelarExport_->load();
                },
                &pProjeto->projeto().writeMutex());
        }

        // Marca d'água nunca vai pro MAIN (só EXPORT) e o MAIN é fiel à SOURCE: sem embed.
        auto resultado = matriz::consolidacao::executarConsolidacao(
            pProjeto->projeto().registro(),
            pProjeto->projeto().pasta(),
            destinoMedia,
            plano,
            [aviso, cancelamento, self](int feito, int total) {
                aviso("Copying: " + juce::String(feito) + " of " + juce::String(total) + " assets...", feito, total, 1);
                return !cancelamento->pedido() && !self->cancelarExport_->load();
            },
            {}, embutirMeta,
            [cancelamento, self]() { return !cancelamento->pedido() && !self->cancelarExport_->load(); },
            opcoesRebuild ? &*opcoesRebuild : nullptr,
            &pProjeto->projeto().writeMutex()
        );

        // Rebuild: o projeto já usava sidecars XMP (o MAIN antigo tinha) => o MAIN novo os recebe, refletindo
        // os metadados ATUAIS do projeto.
        if (opcoesRebuild && !resultado.cancelado && resultado.falhas.empty()) {
            try {
                bool usavaSidecars = false;
                {
                    auto st = pProjeto->projeto().registro().prepare("SELECT 1 FROM sidecar_registro LIMIT 1");
                    usavaSidecars = st.step();
                }
                if (usavaSidecars)
                    matriz::consolidacao::atualizarSidecarsNoMain(
                        pProjeto->projeto().registro(), destinoMedia, matriz::vault::destinationIdDaRaiz(destinoRaiz),
                        /*sobrescreverEditados*/ true, [cancelamento](int, int) { return !cancelamento->pedido(); });
            } catch (...) {}
        }

        // Contagens e lista de falhas (membros do workspace) voltam pela message thread, antes do `concluir`
        // (a fila do callAsync é FIFO).
        juce::MessageManager::callAsync([safeThis, resultado, movs] {
            if (!safeThis) return;
            if (!resultado.cancelado) safeThis->etapaCopy_->concluir();  // a barra COPY fecha em 100%; o CATALOG começa
            safeThis->copiadoCount_ = resultado.consolidados;
            safeThis->verificadoCount_ = resultado.consolidados + resultado.pulados;
            safeThis->falhasCount_ = static_cast<int>(resultado.falhas.size());
            safeThis->falhasLista_.clear();
            safeThis->relatorioErros_.clear();
            for (const auto& f : movs.falhas) safeThis->falhasLista_.push_back("move: " + f);
            safeThis->falhasCount_ += static_cast<int>(movs.falhas.size());
            for (const auto& f : resultado.falhas)
                safeThis->falhasLista_.push_back(f);
        });

        if (gerarCatalogo && !resultado.cancelado) {
            aviso("Generating HTML Catalog...", 0, 0, 3);
            juce::File catDestDir = pProjeto->projeto().pasta().getChildFile("catalogo");
            catDestDir.createDirectory();
            auto resCatalogo = matriz::catalogo::gerar(
                pProjeto->projeto().registro(),
                pProjeto->projeto().indice(),
                pProjeto->projeto().pasta(),
                catDestDir,
                [aviso, cancelamento, self](int feito, int total) {
                    aviso("Cataloging: " + juce::String(feito) + " of " + juce::String(total) + " files...", feito, total, 2);
                    return !cancelamento->pedido() && !self->cancelarExport_->load();
                }
            );
            (void)resCatalogo;
        }

        // O embed agora acontece na própria cópia (executarConsolidacao com
        // embutirMeta) — o passe antigo embutirMetadadosNoBackup reescrevia
        // TODOS os arquivos já registrados no destino.

        // Relatórios + manifesto do fim do backup: continuam neste mesmo job, fora da message thread. Só a UI volta
        // pela message thread (callAsync).

        // Fechamento (message thread): revisão, destino registrado, mensagem final. A cópia já terminou e foi
        // verificada: cancelar só os relatórios não invalida o backup — registrar e confirmar rodam mesmo assim.
        const bool rebuild = opcoesRebuild.has_value();
        auto concluir = [safeThis, pProjeto, vivoProjeto, resultado, destinoRaiz, rebuild](bool relatoriosPulados) {
            if (!safeThis) {
                // O workspace foi destruído (troca de idioma/tema): a revisão do projeto confirma mesmo assim.
                if (!resultado.cancelado && !vivoProjeto.expired()) pProjeto->projeto().confirmarRevisao();
                return;
            }
            // Increment and confirm revision on active project.
            if (!resultado.cancelado) safeThis->projeto_.projeto().confirmarRevisao();

            // Clones: nada automático e silencioso (modelo SOURCE/MAIN/CLONE) —
            // no fim, se houver clone conectado, pergunta (perguntarSincronizarClones).

            safeThis->executando_ = false;
            safeThis->estado_ = Estado::Done;

            // A barra parava no último valor reportado (86%) ao lado de "concluído
            // com sucesso" — dois sinais contraditórios na mesma tela.
            safeThis->progressoValor_ = 1.0;
            if (!resultado.cancelado) safeThis->etapaCatalog_->concluir();

            const bool houveFalha = safeThis->falhasCount_ > 0;
            juce::String rotuloSugerido = safeThis->selectedDestinoIdx_ >= 0 && safeThis->selectedDestinoIdx_ < static_cast<int>(safeThis->destinosBackup_.size())
                ? safeThis->destinosBackup_[static_cast<size_t>(safeThis->selectedDestinoIdx_)].rotulo
                : (safeThis->googleDriveComoDestino_ ? juce::String("Google Drive") : destinoRaiz.getFileName());
            if (!rebuild)
                safeThis->registrarDestinoBackup(destinoRaiz, rotuloSugerido, safeThis->copiadoCount_, resultado.pulados, safeThis->falhasCount_, resultado.cancelado);

            // REBUILD: relatório próprio e, sem falhas e sem cancelar, o fim do modo (o destino novo vira o MAIN).
            juce::String linhasRebuild;
            const bool rebuildOk = rebuild && !resultado.cancelado && safeThis->falhasCount_ == 0;
            if (rebuild) {
                linhasRebuild << "\n" << matriz::i18n::t("rebuild.rel_copiados").replace("{n}", juce::String(safeThis->verificadoCount_));
                if (resultado.copiadosDaSourcePorDivergencia > 0)
                    linhasRebuild << "\n" << matriz::i18n::t("rebuild.rel_da_source").replace("{n}", juce::String(resultado.copiadosDaSourcePorDivergencia));
                if (!resultado.mantidosDoMainAntigo.empty()) {
                    linhasRebuild << "\n" << matriz::i18n::t("rebuild.rel_mantidos").replace("{n}", juce::String((int) resultado.mantidosDoMainAntigo.size()));
                    for (size_t i = 0; i < resultado.mantidosDoMainAntigo.size() && i < 6; ++i)
                        linhasRebuild << "\n  - " << juce::String::fromUTF8(resultado.mantidosDoMainAntigo[i].c_str());
                }
                try {
                    juce::StringArray det;
                    det.add("Copied and verified: " + juce::String(safeThis->verificadoCount_));
                    det.add("Copied from SOURCE (old MAIN differed): " + juce::String(resultado.copiadosDaSourcePorDivergencia));
                    det.add("Kept from old MAIN, differs from SOURCE (SOURCE unavailable): " + juce::String((int) resultado.mantidosDoMainAntigo.size()));
                    det.add("Failures: " + juce::String(safeThis->falhasCount_));
                    for (const auto& m : resultado.mantidosDoMainAntigo) det.add("  " + juce::String::fromUTF8(m.c_str()));
                    for (const auto& f : safeThis->falhasLista_) det.add("  failed: " + f);
                    matriz::model::ProjectLog(safeThis->projeto_.projeto().pasta()).appendEntry("REBUILD DO MAIN: Make Backup finished", det);
                } catch (...) {}
                if (rebuildOk)
                    safeThis->finalizarRebuild(destinoRaiz, matriz::vault::destinationIdDaRaiz(destinoRaiz), safeThis->copiadoCount_);
            }

            juce::String msgFinal = resultado.cancelado ? "Backup cancelled"
                                                       : (houveFalha ? "Backup finished with " + juce::String(safeThis->falhasCount_) + " errors"
                                                                     : "Backup completed: " + juce::String(safeThis->copiadoCount_) + " assets consolidated");
            if (relatoriosPulados && !resultado.cancelado) msgFinal << " (reports skipped)";
            ProgressoGlobal::obterInstancia().concluirTarefa("backup", msgFinal);

            // A linha de destaque diz o QUE aconteceu; a de baixo, os números.
            safeThis->labelProgressoStatus_->setText(
                resultado.cancelado ? juce::String("Backup cancelled.")
                                    : (houveFalha ? juce::String("Backup finished with errors.")
                                                  : (relatoriosPulados ? juce::String::fromUTF8("Backup completed \xe2\x80\x94 reports skipped (cancelled)")
                                                                      : juce::String("Backup completed successfully."))),
                juce::dontSendNotification);
            safeThis->labelProgressoStatus_->setColour(
                juce::Label::textColourId,
                houveFalha ? tema().perigo : ((resultado.cancelado || relatoriosPulados) ? tema().alerta : tema().estadoQcOk));

            juce::String finalMsg;
            finalMsg << safeThis->copiadoCount_ << " copied   |   "
                     << safeThis->verificadoCount_ << " verified";
            if (houveFalha) finalMsg << "   |   " << safeThis->falhasCount_ << " failed";
            finalMsg << "\n" << destinoRaiz.getFullPathName() << linhasRebuild;
            if (rebuild && !rebuildOk && !resultado.cancelado) finalMsg << "\n" << matriz::i18n::t("rebuild.rerodar");
            // SOURCE: liberado quando todo arquivo já tem cópia verificada no MAIN.
            if (!resultado.cancelado) {
                const int dependem = safeThis->projeto_.arquivosQueDependemDoSource();
                finalMsg << "\n" << (dependem == 0 ? matriz::i18n::t("backup.aviso_source_liberado")
                                                    : matriz::i18n::t("backup.aviso_source_dependem")
                                                          .replace("{n}", juce::String(dependem)));
            }
            if (houveFalha)
                finalMsg << "\n\n" << matriz::i18n::t("backup.erros_pointer").replace("{n}", juce::String(safeThis->falhasCount_));
            safeThis->labelResumo_->setText(finalMsg, juce::dontSendNotification);
            // Backup com erros: abre já o resultado detalhado (o que falhou, por quê e onde resolver) em vez de
            // deixar o usuário procurar no log.md.
            if (houveFalha && !resultado.cancelado && !safeThis->falhasLista_.empty()) {
                safeThis->prepararRelatorioErros(rebuild);
                safeThis->mostrarErrosBackup();
            }

            safeThis->labelTitulo_->setText(resultado.cancelado ? "Backup Cancelled" : "Backup Complete",
                                             juce::dontSendNotification);
            safeThis->btnStartBackup_->setVisible(false);
            if (safeThis->btnCancelarExecucao_) safeThis->btnCancelarExecucao_->setVisible(false);
            safeThis->btnDone_->setVisible(true);
            safeThis->resized();
            if (!resultado.cancelado && !rebuild) safeThis->perguntarSincronizarClones();
        };

        if (resultado.cancelado) {  // cópia cancelada: sem relatórios, sem revisão
            juce::MessageManager::callAsync([concluir] { concluir(false); });
            return;
        }

        const juce::File relatoriosDir = pProjeto->projeto().pasta().getChildFile("relatorios");
        relatoriosDir.createDirectory();
        const std::vector<matriz::consolidacao::ItemPlanejado>& itensDoPlano = plano.itens;
        {
            ProjetoAberto& proj = *pProjeto;
            auto parar = [self, cancelamento] { return cancelamento->pedido() || self->cancelarExport_->load(); };
            // Progresso: a UI só é tocada via callAsync com SafePointer.
            auto atualizar = [safeThis](const juce::String& texto, double fracao) {
                juce::MessageManager::callAsync([safeThis, texto, fracao] {
                    if (!safeThis) return;
                    safeThis->progressoValor_ = fracao;
                    safeThis->labelProgressoStatus_->setText(texto, juce::dontSendNotification);
                    safeThis->etapaCatalog_->definir(fracao, texto.upToFirstOccurrenceOf(":", false, false).trimCharactersAtEnd("."));
                    ProgressoGlobal::obterInstancia().atualizarFracao("backup", fracao, texto);
                });
            };

            bool puladas = false;
            try {
                atualizar("Report 1/4 - CSV...", 0.0);
                if (parar()) puladas = true; else gravarRelatorioCsv(proj, itensDoPlano, relatoriosDir);
                if (!puladas) {
                    atualizar("Report 2/4 - XLS...", 0.25);
                    if (parar()) puladas = true; else gravarRelatorioXls(proj, itensDoPlano, relatoriosDir);
                }
                if (!puladas) {
                    atualizar("Report 3/4 - Dublin Core...", 0.50);
                    if (parar()) puladas = true; else gravarRelatorioDublinCore(proj, itensDoPlano, relatoriosDir);
                }
                if (!puladas) {
                    const int total = static_cast<int>(itensDoPlano.size());
                    atualizar("Report 4/4 - Checksums: 0 of " + juce::String(total), 0.75);
                    auto r = matriz::consolidacao::gerarManifestChecksums(
                        proj.projeto().registro(), destinoMedia, itensDoPlano, [&](int feito, int tot) {
                            if (feito % 250 == 0 && feito < tot)
                                atualizar("Report 4/4 - Checksums: " + juce::String(feito) + " of " + juce::String(tot),
                                          0.75 + 0.25 * feito / std::max(1, tot));
                            return !parar();
                        });
                    gravarManifestoChecksums(relatoriosDir, juce::String(proj.projeto().nome()),
                                             juce::String::fromUTF8(r.texto.c_str()), r.cancelado);
                    if (r.cancelado) puladas = true;
                    else registrarManifestoSemRegistroNoLog(proj.projeto().pasta(), r.semRegistro, total);
                }
            } catch (...) {
                puladas = true;  // relatório que falhou não derruba o backup já copiado e verificado
            }
            juce::MessageManager::callAsync([concluir, puladas] { concluir(puladas); });
        }
    });
}

void BackupWorkspaceComponent::prepararRelatorioErros(bool rebuild) {
    struct ItemFalha {
        juce::String codigo;
        juce::String caminho;
        juce::String detalheErro;
    };
    struct Grupo {
        juce::String chave, motivo, diag, acao;
        std::vector<ItemFalha> itens;
    };
    std::vector<Grupo> grupos;
    auto obter = [&](const juce::String& chave, const char* kMotivo, const char* kDiag, const char* kAcao) -> Grupo& {
        for (auto& g : grupos) if (g.chave == chave) return g;
        grupos.push_back({chave, matriz::i18n::t(kMotivo), matriz::i18n::t(kDiag), matriz::i18n::t(kAcao), {}});
        return grupos.back();
    };

    idsRemoviveis_.clear();
    idsBloqueados_.clear();
    constexpr int kMaxLinhasComCaminho = 300;  // depois disso só o código; o botão REMOVE continua pegando todos
    int comCaminho = 0;
    auto& registro = projeto_.projeto().registro();
    for (const auto& f : falhasLista_) {
        juce::String resto = f;
        const bool mover = resto.startsWith("move: ");
        if (mover) resto = resto.substring(6);
        juce::String codigo, motivo = resto;
        const int sep = resto.indexOf(": ");
        if (sep > 0 && sep <= 40) {
            codigo = resto.substring(0, sep);
            motivo = resto.substring(sep + 2);
            if (motivo.endsWith(": " + codigo)) motivo = motivo.dropLastCharacters(codigo.length() + 2);
        }

        // Item que já saiu do catálogo (o usuário removeu depois do backup) não é mais problema.
        std::string itemId;
        juce::String caminho;
        if (codigo.isNotEmpty()) {
            try {
                auto st = registro.prepare(
                    "SELECT i.id, COALESCE(NULLIF(a.caminho_absoluto_origem, ''), a.caminho_relativo) FROM item i "
                    "LEFT JOIN arquivo a ON a.item_id = i.id WHERE i.codigo_acervo = ? ORDER BY a.eh_master DESC LIMIT 1");
                st.bind(1, matriz::db::Value::of(codigo.toStdString()));
                if (!st.step()) continue;
                itemId = st.columnText(0);
                if (!st.columnIsNull(1)) caminho = juce::String::fromUTF8(st.columnText(1).c_str());
            } catch (...) {}
        }

        const bool ausente = motivo.contains("ausente no MAIN antigo");
        const bool hash = motivo.contains("difere do hash");
        Grupo* g = nullptr;
        if (ausente) g = &obter("ausente", "backup.erro_ausente", "backup.erro_ausente_diag", "backup.erro_ausente_acao");
        else if (hash) g = &obter("hash", "backup.erro_hash", "backup.erro_hash_diag", "backup.erro_hash_acao");
        else if (motivo.contains("name conflict")) g = &obter("nome", "backup.erro_nome", "backup.erro_nome_diag", "backup.erro_nome_acao");
        else if (mover) g = &obter("move", "backup.erro_move", "backup.erro_move_diag", "backup.erro_move_acao");
        else g = &obter("outro", "backup.erro_outro", "backup.erro_outro_diag", "backup.erro_outro_acao");

        if (!itemId.empty()) {
            idsBloqueados_.push_back(itemId);
            if (ausente || hash) idsRemoviveis_.push_back(itemId);
        }
        ItemFalha item;
        item.codigo = codigo;
        if (comCaminho++ < kMaxLinhasComCaminho) item.caminho = caminho;
        if (g->chave == "outro" || g->chave == "move") item.detalheErro = motivo;
        g->itens.push_back(std::move(item));
    }

    // Situação do plano atual: o que já está no destino novo e o que ainda falta.
    int noPlano = static_cast<int>(plano_.itens.size()), prontos = 0;
    for (const auto& it : plano_.itens) if (it.jaConsolidado) ++prontos;
    int duplicatas = 0;
    try {
        auto st = registro.prepare("SELECT COUNT(*) FROM item WHERE estado = 'duplicata'");
        if (st.step()) duplicatas = static_cast<int>(st.columnInt(0));
    } catch (...) {}

    const int nBloq = static_cast<int>(idsBloqueados_.size());
    juce::String texto;
    texto << "================================================================================\n";
    texto << "  " << matriz::i18n::t("backup.erros_header_titulo") << "\n";
    texto << "================================================================================\n\n";

    if (rebuild && noPlano > 0)
        texto << "• " << matriz::i18n::t("rebuild.status_situacao").replace("{c}", juce::String(prontos)).replace("{t}", juce::String(noPlano))
                                                           .replace("{p}", juce::String(noPlano - prontos)) << "\n\n";
    if (duplicatas > 0)
        texto << "• " << matriz::i18n::t("rebuild.status_duplicatas").replace("{n}", juce::String(duplicatas)) << "\n\n";
    if (whatOption_ != WhatOption::Everything)
        texto << "• " << matriz::i18n::t("rebuild.status_selecao") << "\n\n";
    if (!grupos.empty()) {
        texto << "• " << matriz::i18n::t("backup.erros_intro") << "\n\n";
        if (rebuild) texto << "• " << matriz::i18n::t("backup.erros_rebuild") << "\n\n";
    } else if (rebuild) {
        texto << "• " << matriz::i18n::t("rebuild.status_pronto") << "\n\n";
    }

    // Ordem fixa: o que mais bloqueia o usuário primeiro.
    const char* ordem[] = {"ausente", "hash", "nome", "move", "outro"};
    for (const char* chave : ordem) {
        for (const auto& g : grupos) {
            if (g.chave != chave) continue;
            texto << "--------------------------------------------------------------------------------\n";
            texto << "[!] " << matriz::i18n::t("backup.erros_grupo_header").replace("{n}", juce::String((int) g.itens.size())).replace("{motivo}", g.motivo) << "\n";
            texto << "--------------------------------------------------------------------------------\n\n";

            texto << "• " << matriz::i18n::t("backup.erros_lbl_diag") << "\n";
            texto << "  " << g.diag << "\n\n";

            texto << "• " << matriz::i18n::t("backup.erros_lbl_solucao") << "\n";
            texto << g.acao << "\n\n";

            texto << "• " << matriz::i18n::t("backup.erros_lbl_itens").replace("{n}", juce::String((int) g.itens.size())) << "\n";
            for (const auto& item : g.itens) {
                texto << "  • [" << item.codigo << "] " << (item.caminho.isNotEmpty() ? item.caminho : juce::String("-"));
                if (item.detalheErro.isNotEmpty())
                    texto << "\n    ↳ " << matriz::i18n::t("backup.erros_lbl_detalhe") << ": " << item.detalheErro;
                texto << "\n";
            }
            texto << "\n";
        }
    }

    statusBloqueado_ = !grupos.empty();
    statusPodeFinalizar_ = rebuild && grupos.empty() && planoPronto_ && noPlano > 0;
    tituloStatus_ = statusBloqueado_ ? matriz::i18n::t("backup.erros_titulo").replace("{n}", juce::String(nBloq > 0 ? nBloq : (int) falhasLista_.size()))
                                     : matriz::i18n::t("rebuild.status_titulo_ok");

    arquivoRelatorioErros_ = juce::File();
    try {
        const juce::File dir = projeto_.projeto().pasta().getChildFile("relatorios");
        dir.createDirectory();
        const juce::File arq = dir.getChildFile("backup-erros-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".txt");
        if (arq.replaceWithText(tituloStatus_ + "\n\n" + texto)) arquivoRelatorioErros_ = arq;
    } catch (...) {}
    if (arquivoRelatorioErros_ != juce::File()) {
        texto << "--------------------------------------------------------------------------------\n";
        texto << "• " << matriz::i18n::t("backup.erros_relatorio") << "\n";
        texto << "  " << arquivoRelatorioErros_.getFullPathName() << "\n\n";
    }
    relatorioErros_ = texto;
}

void BackupWorkspaceComponent::mostrarErrosBackup() {
    if (relatorioErros_.isEmpty()) return;
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

    DadosStatusBackup d;
    d.titulo = tituloStatus_;
    d.texto = relatorioErros_;
    d.relatorio = arquivoRelatorioErros_;
    d.bloqueado = statusBloqueado_;
    d.nRemoviveis = static_cast<int>(idsRemoviveis_.size());
    d.nBloqueados = static_cast<int>(idsBloqueados_.size());
    d.rebuild = projeto_.emRebuild();
    d.podeFinalizar = statusPodeFinalizar_;

    d.aoMostrarNoGrid = [safeThis] {
        if (!safeThis || !safeThis->aoAbrirNoGrid) return;
        safeThis->aoAbrirNoGrid(std::set<std::string>(safeThis->idsBloqueados_.begin(), safeThis->idsBloqueados_.end()));
    };
    d.aoRemover = [safeThis] {
        if (!safeThis) return;
        const auto ids = safeThis->idsRemoviveis_;
        const int n = static_cast<int>(ids.size());
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, matriz::i18n::t("rebuild.remover_titulo"),
            matriz::i18n::t("rebuild.remover_msg").replace("{n}", juce::String(n)), matriz::i18n::t("rebuild.remover_ok"),
            matriz::i18n::t("backup.erros_fechar"), nullptr,
            juce::ModalCallbackFunction::create([safeThis, ids](int ok) {
                if (!safeThis) return;
                if (ok == 0) { safeThis->mostrarErrosBackup(); return; }  // cancelou: volta pra janela
                safeThis->projeto_.removerItensDoProjeto(ids);
                // Recalcula o plano e reabre a janela já atualizada (sem os itens removidos).
                safeThis->recarregar();
                safeThis->aoPlanoPronto([safeThis] {
                    if (!safeThis) return;
                    safeThis->prepararRelatorioErros(safeThis->projeto_.emRebuild());
                    safeThis->mostrarErrosBackup();
                });
            }));
    };
    d.aoFinalizar = [safeThis] {
        if (safeThis) safeThis->executarBackupAcao(false);
    };
    ErrosBackupDialog::mostrar(std::move(d), this);
}

// ---------------------------------------------------------------- REBUILD DO MAIN

bool BackupWorkspaceComponent::validarDestinoRebuild(const juce::File& novo, juce::String& motivo) const {
    const auto fonte = projeto_.fonteRebuild();
    const juce::File antiga = fonte ? fonte->raiz : projeto_.projeto().raiz();
    for (const juce::File& ref : {antiga, projeto_.projeto().raiz()}) {
        if (ref == juce::File()) continue;
        // Recusa o MAIN antigo, qualquer pasta dentro dele e qualquer pasta que o contenha.
        if (novo == ref || novo.isAChildOf(ref) || ref.isAChildOf(novo)) {
            motivo = matriz::i18n::t("rebuild.destino_conflita");
            return false;
        }
    }
    if (novo.exists() && !novo.isDirectory()) {
        motivo = matriz::i18n::t("rebuild.destino_conflita");
        return false;
    }
    if (novo.isDirectory() && novo.getNumberOfChildFiles(juce::File::findFilesAndDirectories) > 0) {
        // Só vale pasta vazia — ou uma que ESTE rebuild já começou (retomar).
        auto info = matriz::model::DestinationInfo::lerDeArquivo(novo.getChildFile("destination.json"));
        if (!info || info->projetoId != projeto_.projeto().projetoId()) {
            motivo = matriz::i18n::t("rebuild.destino_nao_vazio");
            return false;
        }
    }
    return true;
}

void BackupWorkspaceComponent::pedirDestinoRebuild() {
    chooserRebuild_ = std::make_unique<juce::FileChooser>(matriz::i18n::t("rebuild.escolher_titulo"),
                                                          projeto_.projeto().raiz().getParentDirectory(), "");
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    chooserRebuild_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                 [safeThis](const juce::FileChooser& fc) {
        if (!safeThis) return;
        const juce::File escolhida = fc.getResult();
        if (escolhida == juce::File()) return;
        juce::String motivo;
        if (!safeThis->validarDestinoRebuild(escolhida, motivo)) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("rebuild.titulo"), motivo);
            return;
        }
        escolhida.createDirectory();
        if (!escolhida.isDirectory()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("rebuild.titulo"),
                                                   matriz::i18n::t("rebuild.destino_conflita"));
            return;
        }
        safeThis->projeto_.definirRebuildDestino(escolhida);
        safeThis->resolvedDestFolder_ = escolhida;
        safeThis->customDestFolder_ = escolhida;
        safeThis->atualizarResumo();  // o plano passa a ser o do destino novo (espaço necessário incluso)
        safeThis->aoPlanoPronto([safeThis] { if (safeThis) safeThis->iniciarBackup(); });
    });
}

void BackupWorkspaceComponent::finalizarRebuild(const juce::File& novaRaiz, const std::string& novoDestinationId, int) {
    const auto fonte = projeto_.fonteRebuild();
    const juce::File raizAntiga = fonte ? fonte->raiz : juce::File();
    const juce::String erro = projeto_.concluirRebuild(novaRaiz, novoDestinationId);
    if (erro.isNotEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("rebuild.titulo"), erro);
        return;
    }
    // O MAIN novo precisa abrir sozinho: snapshot consistente do banco (VACUUM INTO) e do resto de Project/.
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    auto* proj = &projeto_.projeto();
    const juce::File pastaProjeto = proj->pasta();
    poolBackup_.addJob([safeThis, proj, pastaProjeto, novaRaiz, raizAntiga]() {
        juce::String falha;
        try {
            const juce::File destProject = novaRaiz.getChildFile("Project");
            destProject.createDirectory();
            auto snapshot = [&](matriz::db::Database& db, const juce::String& nome) {
                const juce::File alvo = destProject.getChildFile(nome);
                alvo.deleteFile();
                db.exec("VACUUM INTO '" + alvo.getFullPathName().replace("'", "''").toStdString() + "'");
            };
            // Conexões SÓ DE LEITURA (uma por thread): o VACUUM INTO na conexão de escrita falhava com
            // "cannot VACUUM - SQL statements in progress" sempre que a UI (plano do backup recalculado ao
            // concluir) tinha uma consulta aberta nela. Leitor em WAL tira o snapshot sem esbarrar nisso.
            snapshot(proj->registroLeitura(), "registro.sqlite");
            snapshot(proj->indiceLeitura(), "indice.sqlite");
            for (const auto& c : pastaProjeto.findChildFiles(juce::File::findFilesAndDirectories, false)) {
                const auto nome = c.getFileName();
                if (nome.startsWith("registro.sqlite") || nome.startsWith("indice.sqlite")) continue;
                const juce::File d = destProject.getChildFile(nome);
                if (c.isDirectory()) { d.deleteRecursively(); c.copyDirectoryTo(d); }
                else c.copyFileTo(d);
            }
        } catch (const std::exception& e) {
            falha = juce::String::fromUTF8(e.what());
        }
        juce::MessageManager::callAsync([safeThis, falha, raizAntiga, novaRaiz]() {
            if (!safeThis) return;
            if (falha.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, matriz::i18n::t("rebuild.titulo"),
                                                       matriz::i18n::t("rebuild.erro_copiar_projeto") + "\n" + falha);
            safeThis->perguntarApagarMainAntigo(raizAntiga, novaRaiz);
        });
    });
}

void BackupWorkspaceComponent::perguntarApagarMainAntigo(const juce::File& raizAntiga, const juce::File& novaRaiz) {
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    // Fim: o projeto vivo ainda aponta pra pasta antiga — reabrir pelo MAIN novo.
    auto avisoFinal = [safeThis, novaRaiz]() {
        if (!safeThis) return;
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(matriz::i18n::t("rebuild.titulo"))
                .withMessage(matriz::i18n::t("rebuild.fim").replace("{novo}", novaRaiz.getFullPathName()))
                .withButton(matriz::i18n::t("rebuild.reabrir"))
                .withButton(matriz::i18n::t("rebuild.depois")),
            juce::ModalCallbackFunction::create([safeThis, novaRaiz](int r) {
                if (safeThis && r == 1 && safeThis->aoPedirReabrirPasta) safeThis->aoPedirReabrirPasta(novaRaiz);
            }));
    };
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle(matriz::i18n::t("rebuild.titulo"))
            .withMessage(matriz::i18n::t("rebuild.apagar_msg").replace("{antigo}", raizAntiga.getFullPathName()))
            .withButton(matriz::i18n::t("rebuild.manter"))
            .withButton(matriz::i18n::t("rebuild.apagar")),
        juce::ModalCallbackFunction::create([safeThis, raizAntiga, avisoFinal](int r) {
            if (!safeThis) return;
            if (r != 2) { avisoFinal(); return; }  // MANTER (padrão): a pasta fica no disco, desvinculada
            // APAGAR: barreira do nome do projeto. Só Media/ e _QUARENTENA/ — Project/ (o projeto vivo) nunca.
            MainEditPanel::pedirNomeDoProjeto(
                safeThis->projeto_, matriz::i18n::t("rebuild.titulo"), matriz::i18n::t("rebuild.apagar_barreira"),
                matriz::i18n::t("rebuild.apagar"), [safeThis, raizAntiga, avisoFinal](const juce::String& digitado) {
                    if (!safeThis || !safeThis->projeto_.nomeConfereComProjeto(digitado)) return;
                    ProgressoGlobal::obterInstancia().iniciarTarefa("rebuild_apagar", "Deleting old MAIN", 0);
                    safeThis->poolBackup_.addJob([safeThis, raizAntiga, avisoFinal]() {
                        const bool ok = raizAntiga.getChildFile("Media").deleteRecursively() &&
                                        raizAntiga.getChildFile("_QUARENTENA").deleteRecursively();
                        juce::MessageManager::callAsync([safeThis, ok, raizAntiga, avisoFinal]() {
                            ProgressoGlobal::obterInstancia().concluirTarefa("rebuild_apagar");
                            if (!safeThis) return;
                            try {
                                matriz::model::ProjectLog(safeThis->projeto_.projeto().pasta()).appendEntry(
                                    "REBUILD DO MAIN: old MAIN deleted", {"Path: " + raizAntiga.getFullPathName(),
                                                                          juce::String("Media/ and _QUARENTENA/ removed; Project/ kept"),
                                                                          ok ? "Result: ok" : "Result: some files could not be deleted"});
                            } catch (...) {}
                            avisoFinal();
                        });
                    });
                });
        }));
}

void BackupWorkspaceComponent::perguntarMoverSemPasta() {
    // Agrupado por pasta (de -> para, com a contagem): uma pasta renomeada é uma linha, não mil.
    auto pastaDe = [](const juce::String& rel) {
        return rel.containsChar('/') ? rel.upToLastOccurrenceOf("/", false, false) : juce::String("Media");
    };
    std::vector<std::pair<std::pair<juce::String, juce::String>, int>> grupos;
    std::map<std::pair<juce::String, juce::String>, size_t> indiceGrupo;
    bool reorganiza = false;
    for (const auto& m : plano_.movimentosSemPasta) {
        reorganiza = reorganiza || m.reorganizacao;
        const auto chave = std::make_pair(pastaDe(m.moverDe), pastaDe(m.caminhoRelativoDestino));
        auto it = indiceGrupo.find(chave);
        if (it == indiceGrupo.end()) {
            indiceGrupo[chave] = grupos.size();
            grupos.push_back({chave, 1});
        } else {
            ++grupos[it->second].second;
        }
    }
    constexpr int kMaxLinhas = 15;
    juce::String lista;
    for (size_t g = 0; g < grupos.size() && g < static_cast<size_t>(kMaxLinhas); ++g)
        lista << "\n" << grupos[g].first.first << "/  ->  " << grupos[g].first.second << "/   ("
              << matriz::i18n::t("backup.reorganizar_n_arquivos").replace("{n}", juce::String(grupos[g].second)) << ")";
    if (grupos.size() > static_cast<size_t>(kMaxLinhas))
        lista << "\n" << matriz::i18n::t("backup.reorganizar_mais_pastas")
                             .replace("{n}", juce::String(static_cast<int>(grupos.size()) - kMaxLinhas));
    const int total = static_cast<int>(plano_.movimentosSemPasta.size());

    PainelOverlay::Config cfg;
    cfg.titulo = matriz::i18n::t(reorganiza ? "backup.reorganizar_titulo" : "backup.mover_sem_pasta_titulo");
    cfg.mensagem = matriz::i18n::t(reorganiza ? "backup.reorganizar_msg" : "backup.mover_sem_pasta_msg")
                       .replace("{n}", juce::String(total)) + "\n" + lista;
    cfg.botoes = {
        { matriz::i18n::t("backup.mover_sem_pasta_mover"), 1, true, false },
        { matriz::i18n::t("backup.mover_sem_pasta_pular"), 2, false, false },
        { matriz::i18n::t("dialogo.cancelar"), 3, false, true }
    };
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    overlay_.mostrar(cfg, [safeThis](PainelOverlay::Resultado res) {
        if (!safeThis || res.botaoId == 3 || res.botaoId == 0) return;
        safeThis->movimentosDecididos_ = true;
        safeThis->moverSemPasta_ = (res.botaoId == 1);
        safeThis->iniciarBackup();
    });
}

std::string BackupWorkspaceComponent::destinoIdDoMain() {
    try {
        auto st = projeto_.projeto().registro().prepare(
            "SELECT COALESCE(destination_id, id) FROM backup_destino WHERE ativo = 1 AND papel = 'ORIGINAL' LIMIT 1");
        if (st.step()) return st.columnText(0);
    } catch (...) {}
    return projeto_.projeto().destinationId();
}

void BackupWorkspaceComponent::abrirAjustesBounce() {
    juce::DialogWindow::LaunchOptions opts;
    std::vector<std::pair<std::string, juce::String>> mapas;
    for (auto& m : projeto_.listarFolderMaps())
        if (!m.original) mapas.emplace_back(m.id, m.nome);
    opts.content.setOwned(new ConteudoBounceAjustes(projeto_.projeto().pasta(), std::move(mapas)));
    opts.dialogTitle = matriz::i18n::t("bounce.settings_titulo");
    opts.dialogBackgroundColour = tema().painel;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.componentToCentreAround = this;
    moldura::lancar(opts);
}

// Bounce List: pergunta só a pasta de destino; estrutura e metadados vêm do que ficou salvo nos ajustes.
void BackupWorkspaceComponent::exportarBounceList() {
    if (exportando_ || nBounce_ <= 0 || estado_ != Estado::Config) return;
    chooserBounce_ = std::make_unique<juce::FileChooser>(matriz::i18n::t("bounce.escolher_destino"), juce::File(), "");
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    chooserBounce_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                [safeThis](const juce::FileChooser& fc) {
        if (safeThis == nullptr || fc.getResult() == juce::File()) return;
        const auto cfg = matriz::consolidacao::carregarConfiguracaoBounce(safeThis->projeto_.projeto().pasta());
        // Estrutura: hierarquia do Visual Editor, ou o Folder Map escolhido (arquivo sem pasta vai para _SEM_PASTA).
        matriz::consolidacao::HierarquiaBackup hierarquia{matriz::consolidacao::NivelHierarquia::PastaManual};
        if (cfg.porVisualEditor)
            hierarquia = cfg.hierarquiaCsv.empty() ? matriz::consolidacao::hierarquiaPadrao()
                                                   : matriz::consolidacao::hierarquiaDeCsv(cfg.hierarquiaCsv);
        safeThis->iniciarExport(fc.getResult(), hierarquia, matriz::consolidacao::ModoPrefixoArquivo::Nenhum, {}, false, false,
                                cfg.porVisualEditor ? std::string() : cfg.mapaId, false, true, cfg.paraExport());
    });
}

void BackupWorkspaceComponent::abrirExport(bool formatoBridge, bool bounce) {
    if (!bounce && saidaBloqueadaSemMain()) return;  // nada sai do projeto antes do MAIN (a Bounce List é liberada sem ele)
    if (exportando_) return;
    const int marcadosW = static_cast<int>(projeto_.idsMarcados(ProjetoAberto::TipoMarcacao::Watermark).size());
    const bool wmOk = ProjetoAberto::carregarConfiguracaoWatermarkDePasta(projeto_.projeto().pasta()).valida();
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    std::vector<std::pair<std::string, juce::String>> mapas;
    for (auto& m : projeto_.listarFolderMaps())
        if (!m.original) mapas.emplace_back(m.id, m.nome);
    auto* conteudo = new ConteudoExport(marcadosW, wmOk, prefixoAuto_, std::move(mapas), [safeThis, formatoBridge, bounce](OpcoesExport o) {
        if (safeThis != nullptr) safeThis->iniciarExport(o.destino, o.hierarquia, o.modoPrefixo, o.prefixo, o.embutir,
                                                          o.marcaDagua, o.porMapa ? o.mapaId : std::string(), formatoBridge,
                                                          bounce, o.metaSaida);
    }, formatoBridge, bounce);
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(conteudo);
    opts.dialogTitle = matriz::i18n::t(bounce ? "export.titulo_bounce" : formatoBridge ? "export.titulo_bridge" : "export.titulo");
    opts.dialogBackgroundColour = tema().painel;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.componentToCentreAround = this;
    moldura::lancar(opts);
}

void BackupWorkspaceComponent::iniciarExport(const juce::File& destino,
                                             const matriz::consolidacao::HierarquiaBackup& hierarquia,
                                             matriz::consolidacao::ModoPrefixoArquivo modo, const juce::String& prefixo,
                                             bool embutir, bool marcaDagua, const std::string& mapaIdPedido,
                                             bool formatoBridge, bool bounce,
                                             matriz::consolidacao::OpcoesMetadadosSaida metaSaida) {
    if (exportando_ || !destino.isDirectory()) return;
    // "Manter organização" (PastaManual sem mapa escolhido) usa o mapa do MAIN
    // ou o mapa aberto — sem isso um item em pastas de dois mapas sairia duas vezes.
    std::string mapaId = mapaIdPedido;
    if (mapaId.empty() && std::find(hierarquia.begin(), hierarquia.end(), matriz::consolidacao::NivelHierarquia::PastaManual) != hierarquia.end()) {
        mapaId = projeto_.mapaDoMainId();
        if (mapaId.empty()) mapaId = projeto_.mapaAtivoPadrao();
    }
    exportando_ = true;
    resized();
    cancelarExport_->store(false);
    auto cancelado = cancelarExport_;
    ProgressoGlobal::obterInstancia().iniciarTarefa("export", matriz::i18n::t(bounce ? "export.titulo_bounce" : "export.titulo"), 0,
                                                    [cancelado] { cancelado->store(true); });
    // Tudo que toca banco/disco roda fora da message thread.
    // Bounce List: a fonte são os arquivos com tag B (a seleção do grid não entra); a tag continua depois do export.
    std::set<std::string> itens;
    if (bounce) {
        for (const auto& id : projeto_.idsMarcados(ProjetoAberto::TipoMarcacao::Bounce)) itens.insert(id);
    } else {
        itens = obterItensSelecionadosPeloCriterio();
    }
    std::set<std::string> comW;
    if (marcaDagua && !bounce)
        for (const auto& id : projeto_.idsMarcados(ProjetoAberto::TipoMarcacao::Watermark)) comW.insert(id);
    auto* projeto = &projeto_.projeto();
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    poolExport_.addJob([safeThis, projeto, destino, hierarquia, modo, prefixo, embutir, comW, itens, cancelado, mapaId, formatoBridge, bounce, metaSaida] {
        int semPasta = 0;
        int duplicatas = 0;  // marcados que são duplicata: o plano os exclui de propósito, mas o usuário precisa saber
        try {
            auto st = projeto->registro().prepare("SELECT id FROM item WHERE estado = 'duplicata'");
            while (st.step()) if (itens.count(st.columnText(0))) ++duplicatas;
        } catch (...) {}
        matriz::consolidacao::ResultadoExport r;
        try {
            auto plano = matriz::consolidacao::planejarConsolidacao(projeto->registro(), projeto->pasta(), destino,
                                                                    hierarquia, {}, modo, prefixo,
                                                                    /*autoResolver*/ true, false, false,
                                                                    /*paraExport*/ true, mapaId, false,
                                                                    /*semPastaNaRaiz*/ true);
            for (const auto& id : plano.semPastaExcluidos) if (itens.count(id)) ++semPasta;
            std::vector<matriz::consolidacao::ItemPlanejado> doRecorte;
            for (auto& ip : plano.itens)
                if (itens.count(ip.itemId)) doRecorte.push_back(std::move(ip));
            plano.itens = std::move(doRecorte);
            r = matriz::consolidacao::executarExport(
                projeto->registro(), projeto->pasta(), destino, plano,
                [cancelado](int feito, int total) {
                    juce::MessageManager::callAsync([feito, total] {
                        ProgressoGlobal::obterInstancia().atualizarFracao(
                            "export", static_cast<double>(feito) / std::max(1, total),
                            juce::String(feito) + " / " + juce::String(total));
                    });
                    return !cancelado->load();
                },
                comW, embutir, formatoBridge, metaSaida, /*fonteOrigem*/ bounce);
        } catch (const std::exception& e) {
            r.falhas.push_back(e.what());
        }
        juce::MessageManager::callAsync([safeThis, r, destino, semPasta, duplicatas, bounce, metaSaida] {
            ProgressoGlobal::obterInstancia().concluirTarefa("export");
            if (safeThis == nullptr) return;
            safeThis->exportando_ = false;
            safeThis->resized();
            const juce::String resumo = matriz::i18n::t("export.fim")
                                            .replace("{n}", juce::String(r.copiados))
                                            .replace("{pasta}", destino.getFullPathName());
            juce::String msg;
            if (bounce) {
                using matriz::consolidacao::ModoMetadadosSaida;
                if (metaSaida.modo == ModoMetadadosSaida::Clean) {
                    if (r.rawSemLimpeza > 0)
                        msg << "\n\n" << matriz::i18n::t("export.bounce_raw").replace("{n}", juce::String(r.rawSemLimpeza));
                    if (r.tiposSemLimpeza > 0)
                        msg << "\n\n" << matriz::i18n::t("export.bounce_tipos").replace("{n}", juce::String(r.tiposSemLimpeza));
                } else if (metaSaida.modo == ModoMetadadosSaida::Embed && r.embedNaoSuportado > 0) {
                    msg << "\n\n" << matriz::i18n::t("export.bounce_embed_nao").replace("{n}", juce::String(r.embedNaoSuportado));
                }
            }
            if (r.bridgeEmbutidos > 0 || r.bridgeSidecars > 0 || !r.avisos.empty()) {
                if (msg.isNotEmpty()) msg << "\n\n";
                msg << matriz::i18n::t("export.bridge_resumo")
                           .replace("{e}", juce::String(r.bridgeEmbutidos)).replace("{s}", juce::String(r.bridgeSidecars));
                for (size_t i = 0; i < r.avisos.size() && i < 5; ++i) msg << "\n- " << juce::String::fromUTF8(r.avisos[i].c_str());
                if (r.avisos.size() > 5) msg << "\n... +" << (int) (r.avisos.size() - 5);
            }
            if (bounce) {
                if (r.conflitosResolvidos > 0)
                    msg << "\n\n" << matriz::i18n::t("export.bounce_conflitos").replace("{n}", juce::String(r.conflitosResolvidos));
                if (!r.pulados.empty()) {
                    msg << "\n\n" << matriz::i18n::t("export.bounce_pulados").replace("{n}", juce::String((int) r.pulados.size()));
                    for (size_t i = 0; i < r.pulados.size() && i < 50; ++i) msg << "\n- " << juce::String::fromUTF8(r.pulados[i].c_str());
                    if (r.pulados.size() > 50) msg << "\n... +" << (int) (r.pulados.size() - 50);
                }
            }
            safeThis->mostrarResultadoExport(bounce ? "export.titulo_bounce" : "export.titulo", resumo, r.jaExistiam, r.foraDoMain, semPasta,
                                             bounce ? "export.bounce_sem_pasta" : "export.mapa_sem_pasta_aviso", duplicatas, r.falhas,
                                             r.jaExistiamDiferentes, msg, r.cancelado);
        });
    });
}

void BackupWorkspaceComponent::mostrarResultadoExport(const juce::String& tituloKey, const juce::String& linhaResumo, int jaExistiam,
                                                      int foraDoMain, int semPasta, const juce::String& semPastaKey, int duplicatas,
                                                      const std::vector<std::string>& falhas, const std::vector<std::string>& jaDiferentes,
                                                      const juce::String& extras, bool cancelado, const juce::String& nomeMapa) {
    const bool algoParaEntender = !falhas.empty() || !jaDiferentes.empty() || jaExistiam > 0 || duplicatas > 0 || foraDoMain > 0 ||
                                  semPasta > 0;
    if (!algoParaEntender) {
        juce::String msg = linhaResumo;
        if (extras.trim().isNotEmpty()) msg << "\n\n" << extras.trim();
        if (cancelado) msg << "\n\n" << matriz::i18n::t("export.cancelado");
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, matriz::i18n::t(tituloKey), msg, {}, nullptr,
                                               juce::ModalCallbackFunction::create([](int) {}));
        return;
    }

    auto& registro = projeto_.projeto().registro();
    struct ItemFalha {
        juce::String codigo;
        juce::String caminho;
        juce::String detalheErro;
    };
    struct Grupo { juce::String motivo, diag, acao; std::vector<ItemFalha> itens; };
    Grupo conflito{matriz::i18n::t("export.res_f_conflito"), matriz::i18n::t("export.res_f_conflito_diag"), matriz::i18n::t("export.res_f_conflito_solucao"), {}};
    Grupo outro{matriz::i18n::t("export.res_f_outro"), matriz::i18n::t("export.res_f_outro_diag"), matriz::i18n::t("export.res_f_outro_solucao"), {}};
    std::vector<std::string> idsComFalha;
    int comCaminho = 0;
    for (const auto& f : falhas) {
        const juce::String s = juce::String::fromUTF8(f.c_str());
        juce::String codigo, motivo = s;
        const int sep = s.indexOf(": ");
        if (sep > 0 && sep <= 40) { codigo = s.substring(0, sep); motivo = s.substring(sep + 2); }
        juce::String caminho;
        if (codigo.isNotEmpty()) {
            try {
                auto st = registro.prepare(
                    "SELECT i.id, COALESCE(NULLIF(a.caminho_absoluto_origem, ''), a.caminho_relativo) FROM item i "
                    "LEFT JOIN arquivo a ON a.item_id = i.id WHERE i.codigo_acervo = ? ORDER BY a.eh_master DESC LIMIT 1");
                st.bind(1, matriz::db::Value::of(codigo.toStdString()));
                if (st.step()) {
                    idsComFalha.push_back(st.columnText(0));
                    if (comCaminho++ < 300 && !st.columnIsNull(1)) caminho = juce::String::fromUTF8(st.columnText(1).c_str());
                }
            } catch (...) {}
        }
        const bool ehConflito = motivo.contains("name conflict");
        ItemFalha item;
        item.codigo = codigo;
        item.caminho = caminho;
        if (!ehConflito) item.detalheErro = motivo;
        (ehConflito ? conflito : outro).itens.push_back(std::move(item));
    }

    juce::String texto;
    texto << "================================================================================\n";
    texto << "  " << matriz::i18n::t("backup.erros_header_titulo") << "\n";
    texto << "================================================================================\n\n";
    texto << "• " << linhaResumo << "\n\n";

    if (cancelado) texto << "• " << matriz::i18n::t("export.cancelado") << "\n\n";
    if (jaExistiam > 0) texto << "• " << matriz::i18n::t("export.res_ja").replace("{n}", juce::String(jaExistiam)) << "\n\n";
    if (duplicatas > 0) texto << "• " << matriz::i18n::t("export.res_dup").replace("{n}", juce::String(duplicatas)) << "\n\n";
    if (foraDoMain > 0) texto << "• " << matriz::i18n::t("export.fora_do_main").replace("{n}", juce::String(foraDoMain)) << "\n\n";
    if (semPasta > 0) texto << "• " << matriz::i18n::t(semPastaKey).replace("{n}", juce::String(semPasta)).replace("{mapa}", nomeMapa) << "\n\n";
    if (extras.trim().isNotEmpty()) texto << "• " << extras.trim() << "\n\n";

    if (!jaDiferentes.empty()) {
        texto << "--------------------------------------------------------------------------------\n";
        texto << "[!] " << matriz::i18n::t("export.res_diferentes").replace("{n}", juce::String((int) jaDiferentes.size())) << "\n";
        texto << "--------------------------------------------------------------------------------\n\n";
        texto << "• " << matriz::i18n::t("backup.erros_lbl_diag") << "\n";
        texto << "  " << matriz::i18n::t("export.res_diferentes_diag") << "\n\n";
        texto << "• " << matriz::i18n::t("backup.erros_lbl_solucao") << "\n";
        texto << matriz::i18n::t("export.res_diferentes_solucao") << "\n\n";
        texto << "• " << matriz::i18n::t("backup.erros_lbl_itens").replace("{n}", juce::String((int) jaDiferentes.size())) << "\n";
        for (size_t i = 0; i < jaDiferentes.size() && i < 300; ++i)
            texto << "  • " << juce::String::fromUTF8(jaDiferentes[i].c_str()) << "\n";
        texto << "\n";
    }

    for (const Grupo* g : {&conflito, &outro}) {
        if (g->itens.empty()) continue;
        texto << "--------------------------------------------------------------------------------\n";
        texto << "[!] " << matriz::i18n::t("backup.erros_grupo_header").replace("{n}", juce::String((int) g->itens.size())).replace("{motivo}", g->motivo) << "\n";
        texto << "--------------------------------------------------------------------------------\n\n";
        texto << "• " << matriz::i18n::t("backup.erros_lbl_diag") << "\n";
        texto << "  " << g->diag << "\n\n";
        texto << "• " << matriz::i18n::t("backup.erros_lbl_solucao") << "\n";
        texto << g->acao << "\n\n";
        texto << "• " << matriz::i18n::t("backup.erros_lbl_itens").replace("{n}", juce::String((int) g->itens.size())) << "\n";
        for (const auto& item : g->itens) {
            texto << "  • [" << item.codigo << "] " << (item.caminho.isNotEmpty() ? item.caminho : juce::String("-"));
            if (item.detalheErro.isNotEmpty())
                texto << "\n    ↳ " << matriz::i18n::t("backup.erros_lbl_detalhe") << ": " << item.detalheErro;
            texto << "\n";
        }
        texto << "\n";
    }

    const juce::String titulo = matriz::i18n::t(tituloKey) + " - " +
        (falhas.empty() ? matriz::i18n::t("export.res_titulo_ok")
                        : matriz::i18n::t("export.res_titulo_falhas").replace("{n}", juce::String((int) falhas.size())));
    juce::File relatorio;
    try {
        const juce::File dir = projeto_.projeto().pasta().getChildFile("relatorios");
        dir.createDirectory();
        const juce::File arq = dir.getChildFile("export-resultado-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".txt");
        if (arq.replaceWithText(titulo + "\n\n" + texto)) relatorio = arq;
    } catch (...) {}
    if (relatorio != juce::File()) {
        texto << "--------------------------------------------------------------------------------\n";
        texto << "• " << matriz::i18n::t("backup.erros_relatorio") << "\n";
        texto << "  " << relatorio.getFullPathName() << "\n\n";
    }

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    DadosStatusBackup d;
    d.titulo = titulo;
    d.texto = texto;
    d.relatorio = relatorio;
    d.bloqueado = !falhas.empty();
    d.nBloqueados = static_cast<int>(idsComFalha.size());
    d.rebuild = false;
    d.aoMostrarNoGrid = [safeThis, idsComFalha] {
        if (safeThis && safeThis->aoAbrirNoGrid) safeThis->aoAbrirNoGrid(std::set<std::string>(idsComFalha.begin(), idsComFalha.end()));
    };
    ErrosBackupDialog::mostrar(std::move(d), this);
}

void BackupWorkspaceComponent::abrirPacote() {
    if (saidaBloqueadaSemMain()) return;  // nada sai do projeto antes do MAIN
    if (exportando_) return;
    // O folder map do pacote é o do card C (FOLDER STRUCTURE) — nada de dropdown próprio.
    // MAIN sem mapa (card C travado): mesma regra do EXPORT, mapa do MAIN ou o mapa aberto;
    // o que não tem pasta nele sai em _NO FOLDER.
    std::string mapaId = mapaParaBackup();
    if (mapaId.empty()) mapaId = projeto_.mapaDoMainId();
    if (mapaId.empty()) mapaId = projeto_.mapaAtivoPadrao();
    juce::String nomeMapa;
    for (auto& m : projeto_.listarFolderMaps())
        if (m.id == mapaId) nomeMapa = m.nome;
    if (mapaId.empty() || nomeMapa.isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, matriz::i18n::t("export.pacote_titulo"),
                                               matriz::i18n::t("export.pacote_sem_mapa_c"), {}, nullptr,
                                               juce::ModalCallbackFunction::create([](int) {}));
        return;
    }
    const juce::String nomePadrao = juce::String::fromUTF8(projeto_.projeto().nome().c_str()) + " - " +
                                    juce::Time::getCurrentTime().formatted("%Y-%m-%d");
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    auto* conteudo = new ConteudoPacote(nomePadrao, nomeMapa,
                                        [safeThis, mapaId, nomeMapa](juce::File destino, juce::String nome, bool embutir) {
                                            if (safeThis != nullptr) safeThis->iniciarPacote(destino, nome, mapaId, nomeMapa, embutir);
                                        });
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(conteudo);
    opts.dialogTitle = matriz::i18n::t("export.pacote_titulo");
    opts.dialogBackgroundColour = tema().painel;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.componentToCentreAround = this;
    moldura::lancar(opts);
}

void BackupWorkspaceComponent::iniciarPacote(const juce::File& destino, const juce::String& nome, const std::string& mapaId,
                                             const juce::String& nomeMapa, bool embutir) {
    if (exportando_ || !destino.isDirectory() || mapaId.empty()) return;
    exportando_ = true;
    resized();
    cancelarExport_->store(false);
    auto cancelado = cancelarExport_;
    ProgressoGlobal::obterInstancia().iniciarTarefa("export", matriz::i18n::t("export.pacote_titulo"), 0,
                                                    [cancelado] { cancelado->store(true); });
    const std::set<std::string> itens = obterItensSelecionadosPeloCriterio();
    const juce::String nomeColecao = juce::String::fromUTF8(projeto_.projeto().nome().c_str());
    auto* projeto = &projeto_.projeto();
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    // Tudo que toca banco/disco roda fora da message thread; o projeto só é lido.
    poolExport_.addJob([safeThis, projeto, destino, nome, mapaId, nomeMapa, nomeColecao, itens, cancelado, embutir] {
        int duplicatas = 0;  // marcados que são duplicata: ficam de fora de propósito, o usuário precisa saber
        try {
            auto st = projeto->registro().prepare("SELECT id FROM item WHERE estado = 'duplicata'");
            while (st.step()) if (itens.count(st.columnText(0))) ++duplicatas;
        } catch (...) {}
        matriz::consolidacao::pacote::ResultadoPacote r;
        try {
            r = matriz::consolidacao::pacote::gerarPacote(
                projeto->registro(), projeto->pasta(), destino, nome, mapaId, nomeMapa, nomeColecao, itens,
                [cancelado](int feito, int total) {
                    juce::MessageManager::callAsync([feito, total] {
                        ProgressoGlobal::obterInstancia().atualizarFracao(
                            "export", static_cast<double>(feito) / std::max(1, total),
                            juce::String(feito) + " / " + juce::String(total));
                    });
                    return !cancelado->load();
                }, embutir);
        } catch (const std::exception& e) {
            r.falhas.push_back(e.what());
        }
        juce::MessageManager::callAsync([safeThis, r, duplicatas, nomeMapa] {
            ProgressoGlobal::obterInstancia().concluirTarefa("export");
            if (safeThis == nullptr) return;
            safeThis->exportando_ = false;
            safeThis->resized();
            const juce::String resumo = r.cancelado ? matriz::i18n::t("export.cancelado")
                                      : r.copiados == 0 ? matriz::i18n::t("export.pacote_vazio")
                                                        : matriz::i18n::t("export.pacote_fim")
                                                              .replace("{n}", juce::String(r.copiados))
                                                              .replace("{pasta}", r.pasta.getFullPathName());
            safeThis->mostrarResultadoExport("export.pacote_titulo", resumo, 0, r.foraDoMain, r.semPasta, "export.pacote_sem_pasta_aviso",
                                             duplicatas, r.falhas, {}, juce::String(), false, nomeMapa);
        });
    });
}

void BackupWorkspaceComponent::atualizarSidecars(bool sobrescreverEditados, std::vector<juce::String> importar) {
    if (projeto_.somenteLeitura()) { ProjetoAberto::avisarSomenteLeitura(); return; }
    if (exportando_) return;
    exportando_ = true;  // um trabalho de disco por vez nesta tela
    if (btnAtualizarSidecars_) btnAtualizarSidecars_->setEnabled(false);
    ProgressoGlobal::obterInstancia().iniciarTarefa("sidecars", matriz::i18n::t("sidecar.btn"), 0);
    auto* projeto = &projeto_.projeto();
    const juce::File media = projeto->pastaMedia();
    const std::string destinoId = destinoIdDoMain();
    auto cancelado = cancelarExport_;
    cancelado->store(false);
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    poolExport_.addJob([safeThis, projeto, media, destinoId, sobrescreverEditados, importar, cancelado] {
        matriz::consolidacao::ResultadoSidecars r;
        int importados = 0;
        try {
            if (!importar.empty())
                importados = matriz::consolidacao::importarSidecarsEditados(projeto->registro(), media, destinoId, importar);
            else
                r = matriz::consolidacao::atualizarSidecarsNoMain(
                    projeto->registro(), media, destinoId, sobrescreverEditados, [cancelado](int feito, int total) {
                        juce::MessageManager::callAsync([feito, total] {
                            ProgressoGlobal::obterInstancia().atualizarFracao("sidecars", (double) feito / std::max(1, total),
                                                                            juce::String(feito) + " / " + juce::String(total));
                        });
                        return !cancelado->load();
                    });
        } catch (...) {
            ++r.falhas;
        }
        juce::MessageManager::callAsync([safeThis, r, importados, importando = !importar.empty()] {
            ProgressoGlobal::obterInstancia().concluirTarefa("sidecars");
            if (safeThis == nullptr) return;
            safeThis->exportando_ = false;
            if (safeThis->btnAtualizarSidecars_) safeThis->btnAtualizarSidecars_->setEnabled(true);
            if (importando) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, matriz::i18n::t("sidecar.btn"),
                    matriz::i18n::t("sidecar.importados").replace("{n}", juce::String(importados)), {}, nullptr,
                    juce::ModalCallbackFunction::create([](int) {}));
                return;
            }
            juce::String msg = matriz::i18n::t("sidecar.fim")
                                   .replace("{n}", juce::String(r.escritos))
                                   .replace("{i}", juce::String(r.iguais));
            if (r.falhas > 0) msg << "\n" << matriz::i18n::t("export.falhas").replace("{n}", juce::String(r.falhas));
            if (r.editadosPorFora.empty()) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, matriz::i18n::t("sidecar.btn"), msg, {},
                                                       nullptr, juce::ModalCallbackFunction::create([](int) {}));
                return;
            }
            // Editados por fora (ou que não são do Matriz): nunca em silêncio.
            msg << "\n\n" << matriz::i18n::t("sidecar.editados").replace("{n}", juce::String((int) r.editadosPorFora.size()));
            for (size_t i = 0; i < r.editadosPorFora.size() && i < 8; ++i) msg << "\n- " << r.editadosPorFora[i];
            auto* aw = new juce::AlertWindow(matriz::i18n::t("sidecar.btn"), msg, juce::MessageBoxIconType::WarningIcon);
            aw->addButton(matriz::i18n::t("sidecar.importar"), 2);
            aw->addButton(matriz::i18n::t("sidecar.sobrescrever"), 1);
            aw->addButton(matriz::i18n::t("sidecar.deixar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
            auto editados = r.editadosPorFora;
            aw->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, editados](int escolha) {
                if (safeThis == nullptr || escolha == 0) return;
                if (escolha == 1) safeThis->atualizarSidecars(true);
                else safeThis->atualizarSidecars(false, editados);
            }), true);
        });
    });
}

void BackupWorkspaceComponent::perguntarSincronizarClones() {
    // Decisão do modelo SOURCE/MAIN/CLONE: nada automático e silencioso. Se
    // houver CLONE conectado, pergunta; só adições/atualizações entram —
    // remoções nunca acontecem aqui (ficam pra "Sincronizar clone", com
    // confirmação).
    int conectados = 0;
    for (const auto& d : destinosBackup_)
        if (d.papel != "ORIGINAL" && d.online) ++conectados;
    if (conectados == 0) return;

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon, matriz::i18n::t("backup.sync_clones_titulo"),
        matriz::i18n::t("backup.sync_clones_msg").replace("{n}", juce::String(conectados)),
        matriz::i18n::t("backup.sync_clones_sim"), matriz::i18n::t("backup.sync_clones_nao"), nullptr,
        juce::ModalCallbackFunction::create([safeThis](int resultado) {
            if (resultado != 1 || safeThis == nullptr) return;
            safeThis->labelProgressoStatus_->setText(matriz::i18n::t("backup.sync_clones_andamento"),
                                                     juce::dontSendNotification);
            auto* projeto = &safeThis->projeto_.projeto();
            safeThis->poolSyncClones_.addJob([safeThis, projeto] {
                auto status = matriz::sync::SyncEngine::executarEspelhamentoAutomatico(*projeto, {},
                                                                                       /*aplicarRemocoes*/ false);
                int ok = 0, falhas = 0;
                for (const auto& st : status) {
                    if (st.estado == matriz::sync::SyncEngine::StatusEspelhamento::Estado::Aplicado) ++ok;
                    else if (st.estado != matriz::sync::SyncEngine::StatusEspelhamento::Estado::PendenteOffline) ++falhas;
                }
                juce::MessageManager::callAsync([safeThis, ok, falhas] {
                    if (safeThis == nullptr) return;
                    safeThis->labelProgressoStatus_->setText(
                        matriz::i18n::t("backup.sync_clones_fim").replace("{ok}", juce::String(ok))
                            .replace("{falhas}", juce::String(falhas)),
                        juce::dontSendNotification);
                });
            });
        }));
}

void BackupWorkspaceComponent::carregarDestinosBackup() {
    // ONLINE/OFFLINE vem de isDirectory() — num volume de rede desconectado
    // isso trava a message thread. Aqui só se reaproveita o que a carga
    // anterior já sabia (a raiz do projeto aberto conta como online); o
    // estado real chega do job em background no fim da função.
    std::map<juce::String, bool> onlineConhecido;
    for (const auto& d : destinosBackup_) onlineConhecido[d.caminho] = d.online;
    destinosBackup_.clear();
    projeto_.sincronizarBackupDestinoDeHistorico();
    const juce::File raizAberta = projeto_.projeto().raiz();

    auto& db = projeto_.projeto().registro();
    try {
        auto stmt = db.prepare("SELECT id, rotulo, destino_path, papel, ativo FROM backup_destino WHERE ativo = 1 ORDER BY criado_em ASC");
        while (stmt.step()) {
            DestinoBackupItem d;
            d.id = stmt.columnText(0);
            d.rotulo = juce::String::fromUTF8(stmt.columnText(1).c_str());
            d.caminho = juce::String::fromUTF8(stmt.columnText(2).c_str());
            d.papel = stmt.columnIsNull(3) ? "CLONE" : stmt.columnText(3);
            const auto conhecido = onlineConhecido.find(d.caminho);
            d.online = juce::File(d.caminho) == raizAberta ? true
                       : conhecido != onlineConhecido.end() ? conhecido->second : false;
            d.ativo = (juce::File(d.caminho) == projeto_.projeto().raiz());
            destinosBackup_.push_back(std::move(d));
        }
    } catch (...) {}

    // Garante que a pasta oficial criada no início do projeto (DESTINATION Original) esteja sempre listada
    juce::File raizProjeto = projeto_.projeto().raiz();
    bool encontrouRaiz = false;
    for (auto& d : destinosBackup_) {
        if (juce::File(d.caminho) == raizProjeto) {
            encontrouRaiz = true;
            d.ativo = true;
            if (projeto_.emRebuild()) {
                d.papel = "FONTE_REBUILD";  // MAIN antigo: só leitura até o rebuild terminar
            } else if (projeto_.projeto().papel() == "ORIGINAL" || d.papel != "CLONE") {
                d.papel = "ORIGINAL";
            }
            break;
        }
    }

    if (!encontrouRaiz && raizProjeto.isDirectory()) {
        DestinoBackupItem d;
        d.id = projeto_.projeto().destinationId();
        d.rotulo = raizProjeto.getFileName();
        d.caminho = raizProjeto.getFullPathName();
        d.papel = "ORIGINAL";
        d.online = true;
        d.ativo = true;
        destinosBackup_.push_back(std::move(d));
    }

    // Remove entradas que são subpastas da raiz do projeto — ex: Media/ auto-registrada como clone.
    destinosBackup_.erase(
        std::remove_if(destinosBackup_.begin(), destinosBackup_.end(),
            [&raizProjeto](const DestinoBackupItem& d) {
                return juce::File(d.caminho).isAChildOf(raizProjeto);
            }),
        destinosBackup_.end());

    // MAIN ORIGINAL aparece sempre em 1º lugar no topo da lista
    std::stable_partition(destinosBackup_.begin(), destinosBackup_.end(),
                          [](const DestinoBackupItem& d) { return d.papel == "ORIGINAL"; });

    // sanitizarEstruturaDestino NÃO roda mais aqui (varria todos os destinos
    // online a cada visita à aba, com I/O de disco na message thread): fica na
    // abertura do projeto (Project::abrir) e imediatamente antes de executar
    // backup/clone (iniciarBackup, criarNovoClone, SyncEngine).

    if (listVaults_) {
        listVaults_->updateContent();
        listVaults_->repaint();
    }

    std::vector<juce::String> caminhos;
    for (const auto& d : destinosBackup_) caminhos.push_back(d.caminho);
    const int geracao = ++geracaoDestinos_;
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    poolDestinos_.addJob([safeThis, geracao, caminhos = std::move(caminhos)]() {
        std::vector<std::pair<juce::String, bool>> resultados;
        for (const auto& c : caminhos) resultados.emplace_back(c, juce::File(c).isDirectory());
        juce::MessageManager::callAsync([safeThis, geracao, resultados = std::move(resultados)]() {
            if (safeThis == nullptr || geracao != safeThis->geracaoDestinos_) return;  // superado por uma carga mais nova
            bool mudou = false;
            for (auto& d : safeThis->destinosBackup_)
                for (const auto& res : resultados)
                    if (d.caminho == res.first && d.online != res.second) { d.online = res.second; mudou = true; }
            if (mudou && safeThis->listVaults_) safeThis->listVaults_->repaint();
        });
    });
}

void BackupWorkspaceComponent::adicionarOuAtivarDestino(const juce::File& pasta, const juce::String& rotuloSugerido) {
    if (pasta == juce::File()) return;
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    auto dentroDeMediaOuProject = [](const juce::File& f) {
        juce::File cur = f;
        while (cur != juce::File() && cur != cur.getParentDirectory()) {
            if (cur.getFileName().equalsIgnoreCase("Media") || cur.getFileName().equalsIgnoreCase("Project"))
                return true;
            cur = cur.getParentDirectory();
        }
        return false;
    };

    if (dentroDeMediaOuProject(pasta) || pasta.isAChildOf(projeto_.projeto().raiz()) || pasta.isAChildOf(projeto_.projeto().pasta())) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Pasta de Clone Inválida") : juce::String("Invalid Clone Folder"),
            isPt ? juce::String::fromUTF8("A pasta do clone não pode se chamar 'Media' ou 'Project', nem estar dentro de uma pasta Media ou Project.")
                 : "The clone folder cannot be named 'Media' or 'Project', nor be inside a Media or Project folder.");
        return;
    }

    auto& db = projeto_.projeto().registro();
    std::string destPath = pasta.getFullPathName().toStdString();
    std::string rotulo = rotuloSugerido.trim().isNotEmpty() ? rotuloSugerido.toStdString() : pasta.getFileName().toStdString();
    if (rotulo.empty()) rotulo = "Backup";

    try {
        projeto_.registrarUndoDeColunas("Add Backup Destination", "backup_destino", "destino_path", destPath, {"ativo"});
        auto stmtCheck = db.prepare("SELECT id, rotulo, ativo FROM backup_destino WHERE destino_path = ?");
        stmtCheck.bind(1, matriz::db::Value::of(destPath));
        if (stmtCheck.step()) {
            int ativo = stmtCheck.columnInt(2);
            if (ativo == 0) {
                db.run("UPDATE backup_destino SET ativo = 1 WHERE destino_path = ?", {matriz::db::Value::of(destPath)});
            }
        } else {
            std::string novoId = matriz::model::novoUuid();
            std::string agora = matriz::model::agoraIso8601();
            db.run("INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, ultima_revisao_conhecida) VALUES (?, ?, ?, ?, 'CLONE', 1, ?, ?)",
                   {matriz::db::Value::of(novoId), matriz::db::Value::of(novoId), matriz::db::Value::of(destPath),
                    matriz::db::Value::of(rotulo), matriz::db::Value::of(agora),
                    matriz::db::Value::of(static_cast<int64_t>(projeto_.projeto().revisao()))});

            // Garante que destination.json exista no destino registrado
            juce::File destJson = pasta.getChildFile("destination.json");
            if (!destJson.existsAsFile()) {
                matriz::model::DestinationInfo dInfo;
                dInfo.formato = 1;
                dInfo.destinationId = novoId;
                dInfo.projetoId = projeto_.projeto().projetoId();
                dInfo.papel = "CLONE";
                dInfo.rotulo = rotulo;
                dInfo.revisao = projeto_.projeto().revisao();
                dInfo.ultimaEdicaoUtc = agora;
                dInfo.criadoEm = agora;
                dInfo.gravarEmArquivo(destJson);
            }

            // Log: Backup Version Created
            matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
            juce::StringArray details;
            details.add("Label: " + juce::String::fromUTF8(rotulo.c_str()));
            details.add("Path: " + pasta.getFullPathName());
            pLog.appendEntry("Backup Version Created", details);
        }
    } catch (...) {}
}

void BackupWorkspaceComponent::criarNovoClone(const juce::File& folder) {
    if (folder == juce::File()) return;
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    auto dentroDeMediaOuProject = [](const juce::File& f) {
        juce::File cur = f;
        while (cur != juce::File() && cur != cur.getParentDirectory()) {
            if (cur.getFileName().equalsIgnoreCase("Media") || cur.getFileName().equalsIgnoreCase("Project"))
                return true;
            cur = cur.getParentDirectory();
        }
        return false;
    };

    if (dentroDeMediaOuProject(folder) || folder.isAChildOf(projeto_.projeto().raiz()) || folder.isAChildOf(projeto_.projeto().pasta())) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Pasta de Clone Inválida") : juce::String("Invalid Clone Folder"),
            isPt ? juce::String::fromUTF8("A pasta do clone não pode se chamar 'Media' ou 'Project', nem estar dentro de uma pasta Media ou Project.")
                 : "The clone folder cannot be named 'Media' or 'Project', nor be inside a Media or Project folder.");
        return;
    }

    if (folder == projeto_.projeto().raiz() || folder == projeto_.projeto().pasta()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Pasta de Clone Inválida") : juce::String("Invalid Clone Folder"),
            isPt ? juce::String::fromUTF8("A pasta do clone não pode ser a própria pasta do projeto atual.")
                 : "The clone folder cannot be the active project's own folder.");
        return;
    }

    juce::File destJsonFile = folder.getChildFile("destination.json");
    if (destJsonFile.existsAsFile()) {
        // Already a destination
        auto destInfoOpt = matriz::model::DestinationInfo::lerDeArquivo(destJsonFile);
        if (destInfoOpt.has_value()) {
            if (destInfoOpt->destinationId == projeto_.projeto().destinationId()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    isPt ? juce::String::fromUTF8("Versão Existente") : juce::String("Existing Version"),
                    isPt ? juce::String::fromUTF8("Esta pasta já é a própria versão ativa.")
                         : "This folder is already the active version.");
                return;
            }
            // Link existing destination
            auto& db = projeto_.projeto().registro();
            try {
                projeto_.registrarUndoDeColunas("Link Backup Destination", "backup_destino", "destino_path", folder.getFullPathName().toStdString(),
                                                {"ativo", "destination_id", "papel", "ultima_revisao_conhecida", "ultimo_visto_em", "ultima_edicao_conhecida"});
                auto stmtCheck = db.prepare("SELECT id FROM backup_destino WHERE destino_path = ?");
                stmtCheck.bind(1, matriz::db::Value::of(folder.getFullPathName().toStdString()));
                if (stmtCheck.step()) {
                    db.run(
                        "UPDATE backup_destino SET ativo = 1, destination_id = ?, papel = ?, "
                        "ultima_revisao_conhecida = ?, ultimo_visto_em = ?, ultima_edicao_conhecida = ? WHERE destino_path = ?",
                        {matriz::db::Value::of(destInfoOpt->destinationId),
                         matriz::db::Value::of(destInfoOpt->papel.empty() ? "CLONE" : destInfoOpt->papel),
                         matriz::db::Value::of(static_cast<int64_t>(destInfoOpt->revisao)),
                         matriz::db::Value::of(matriz::model::agoraIso8601()),
                         matriz::db::Value::of(destInfoOpt->ultimaEdicaoUtc),
                         matriz::db::Value::of(folder.getFullPathName().toStdString())});
                } else {
                    db.run(
                        "INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, "
                        "ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
                        "VALUES (?, ?, ?, ?, ?, 1, ?, ?, ?, ?)",
                        {matriz::db::Value::of(matriz::model::novoUuid()),
                         matriz::db::Value::of(destInfoOpt->destinationId),
                         matriz::db::Value::of(folder.getFullPathName().toStdString()),
                         matriz::db::Value::of(folder.getFileName().toStdString()),
                         matriz::db::Value::of(destInfoOpt->papel.empty() ? "CLONE" : destInfoOpt->papel),
                         matriz::db::Value::of(matriz::model::agoraIso8601()),
                         matriz::db::Value::of(static_cast<int64_t>(destInfoOpt->revisao)),
                         matriz::db::Value::of(matriz::model::agoraIso8601()),
                         matriz::db::Value::of(destInfoOpt->ultimaEdicaoUtc)});
                }
            } catch (...) {}

            carregarDestinosBackup();
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                isPt ? juce::String::fromUTF8("Clone Vinculado") : juce::String("Clone Linked"),
                isPt ? juce::String::fromUTF8("Clone existente vinculado com sucesso ao projeto.")
                     : "Existing clone linked successfully to the project.");
            return;
        }
    }

    if (folder.exists() && !folder.findChildFiles(juce::File::findFilesAndDirectories, false).isEmpty()) {
        bool ok = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Pasta Não Vazia") : juce::String("Folder Not Empty"),
            isPt ? juce::String::fromUTF8("A pasta selecionada não está vazia. Deseja criar o clone nesta pasta?")
                 : "The selected folder is not empty. Do you want to create the clone here?",
            isPt ? juce::String::fromUTF8("Continuar") : juce::String("Proceed"),
            isPt ? juce::String::fromUTF8("Cancelar") : juce::String("Cancel"));
        if (!ok) return;
    }

    // Check disk space
    juce::int64 espacoNecessario = 0;
    for (auto& f : projeto_.projeto().pasta().findChildFiles(juce::File::findFiles, true)) {
        espacoNecessario += f.getSize();
    }
    for (auto& f : projeto_.projeto().pastaMedia().findChildFiles(juce::File::findFiles, true)) {
        espacoNecessario += f.getSize();
    }
    juce::int64 espacoLivre = folder.getBytesFreeOnVolume();
    if (espacoLivre > 0 && espacoLivre < espacoNecessario) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Espaço Insuficiente") : juce::String("Insufficient Disk Space"),
            isPt ? juce::String::fromUTF8("Espaço livre em disco insuficiente para criar o clone.\nNecessário: ") +
                   juce::File::descriptionOfSizeInBytes(espacoNecessario) + juce::String::fromUTF8("\nDisponível: ") +
                   juce::File::descriptionOfSizeInBytes(espacoLivre)
                 : "Insufficient disk space to create the clone.\nRequired: " +
                   juce::File::descriptionOfSizeInBytes(espacoNecessario) + "\nAvailable: " +
                   juce::File::descriptionOfSizeInBytes(espacoLivre));
        return;
    }

    // Perform backup creation
    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "clone",
        isPt ? "Criando Clone" : "Creating Clone",
        100,
        nullptr,
        isPt ? "Copiando banco de dados e arquivos..." : "Copying database and project files...");

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    juce::File pastaProjeto = projeto_.projeto().pasta();
    juce::File pastaMedia = projeto_.projeto().pastaMedia();
    juce::File raizProjeto = projeto_.projeto().raiz();
    std::string projId = projeto_.projeto().projetoId();
    std::string destId = projeto_.projeto().destinationId();
    int64_t projRev = static_cast<int64_t>(projeto_.projeto().revisao());

    // Assegura que o projeto de origem esteja perfeitamente higienizado antes do clone
    matriz::model::sanitizarEstruturaDestino(raizProjeto);

    juce::Thread::launch([safeThis, folder, isPt, pastaProjeto, pastaMedia, raizProjeto, projId, destId, projRev]() {
        try {
            folder.createDirectory();
            juce::File cloneProject = folder.getChildFile("Project");
            cloneProject.createDirectory();
            juce::File cloneMedia = folder.getChildFile("Media");
            cloneMedia.createDirectory();

            // Safe copy of databases
            juce::File dbReg = cloneProject.getChildFile("registro.sqlite");
            dbReg.deleteFile();
            juce::File srcReg = pastaProjeto.getChildFile("registro.sqlite");
            if (srcReg.existsAsFile()) {
                matriz::db::Database srcDb(srcReg.getFullPathName().toStdString());
                srcDb.copiarSeguroPara(dbReg.getFullPathName().toStdString());
            }

            juce::File dbInd = cloneProject.getChildFile("indice.sqlite");
            dbInd.deleteFile();
            juce::File srcInd = pastaProjeto.getChildFile("indice.sqlite");
            if (srcInd.existsAsFile()) {
                matriz::db::Database srcDbInd(srcInd.getFullPathName().toStdString());
                srcDbInd.copiarSeguroPara(dbInd.getFullPathName().toStdString());
            }

            // Copy other project files
            for (auto& child : pastaProjeto.findChildFiles(juce::File::findFilesAndDirectories, false)) {
                juce::String name = child.getFileName();
                if (name == "registro.sqlite" || name == "indice.sqlite" ||
                    name.startsWith("registro.sqlite-") || name.startsWith("indice.sqlite-") ||
                    name == ".sync_em_andamento") {
                    continue;
                }
                if (child.isDirectory()) {
                    child.copyDirectoryTo(cloneProject.getChildFile(name));
                } else {
                    child.copyFileTo(cloneProject.getChildFile(name));
                }
            }

            // Copy media files (only genuine media, never system/project folders)
            if (pastaMedia.isDirectory()) {
                for (auto& child : pastaMedia.findChildFiles(juce::File::findFilesAndDirectories, false)) {
                    juce::String name = child.getFileName();
                    if (name == ".DS_Store" || name == "_lixeira" || name == "destination.json" ||
                        name == "Project" || name == "log" || name == "relatorios" ||
                        name == "catalogo" || name == "Media") {
                        continue;
                    }
                    if (child.isDirectory()) {
                        child.copyDirectoryTo(cloneMedia.getChildFile(name));
                    } else {
                        child.copyFileTo(cloneMedia.getChildFile(name));
                    }
                }
            }

            // Copy project file (.mtz / .bkm) to root and Project/ of clone
            for (auto& fProj : raizProjeto.findChildFiles(juce::File::findFiles, false, "*.mtz;*.bkm")) {
                fProj.copyFileTo(folder.getChildFile(fProj.getFileName()));
                fProj.copyFileTo(cloneProject.getChildFile(fProj.getFileName()));
            }

            // Write destination.json in clone
            matriz::model::DestinationInfo cloneInfo;
            cloneInfo.destinationId = matriz::model::novoUuid();
            cloneInfo.projetoId = projId;
            cloneInfo.papel = "CLONE";
            cloneInfo.rotulo = folder.getFileName().toStdString();
            cloneInfo.revisao = projRev;
            cloneInfo.ultimaEdicaoUtc = matriz::model::agoraIso8601();
            cloneInfo.criadoEm = matriz::model::agoraIso8601();
            cloneInfo.gravarEmArquivo(folder.getChildFile("destination.json"));

            // Rigorously sanitize clone structure
            matriz::model::sanitizarEstruturaDestino(folder);

            // Register clone in active project's backup_destino
            {
                matriz::db::Database activeDb(pastaProjeto.getChildFile("registro.sqlite").getFullPathName().toStdString());
                auto stmtCheck = activeDb.prepare("SELECT id FROM backup_destino WHERE destino_path = ?");
                stmtCheck.bind(1, matriz::db::Value::of(folder.getFullPathName().toStdString()));
                if (stmtCheck.step()) {
                    activeDb.run(
                        "UPDATE backup_destino SET ativo = 1, destination_id = ?, papel = 'CLONE', "
                        "ultima_revisao_conhecida = ?, ultimo_visto_em = ?, ultima_edicao_conhecida = ? WHERE destino_path = ?",
                        {matriz::db::Value::of(cloneInfo.destinationId),
                         matriz::db::Value::of(static_cast<int64_t>(cloneInfo.revisao)),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(folder.getFullPathName().toStdString())});
                } else {
                    activeDb.run(
                        "INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, "
                        "ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
                        "VALUES (?, ?, ?, ?, 'CLONE', 1, ?, ?, ?, ?)",
                        {matriz::db::Value::of(matriz::model::novoUuid()),
                         matriz::db::Value::of(cloneInfo.destinationId),
                         matriz::db::Value::of(folder.getFullPathName().toStdString()),
                         matriz::db::Value::of(folder.getFileName().toStdString()),
                         matriz::db::Value::of(cloneInfo.criadoEm),
                         matriz::db::Value::of(static_cast<int64_t>(cloneInfo.revisao)),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc)});
                }
            }

            // Register active destination inside clone's backup_destino
            try {
                matriz::db::Database cloneDb(dbReg.getFullPathName().toStdString());
                auto stmtCheck = cloneDb.prepare("SELECT id FROM backup_destino WHERE destino_path = ?");
                stmtCheck.bind(1, matriz::db::Value::of(raizProjeto.getFullPathName().toStdString()));
                if (stmtCheck.step()) {
                    cloneDb.run(
                        "UPDATE backup_destino SET ativo = 1, destination_id = ?, papel = 'ORIGINAL', "
                        "ultima_revisao_conhecida = ?, ultimo_visto_em = ?, ultima_edicao_conhecida = ? WHERE destino_path = ?",
                        {matriz::db::Value::of(destId),
                         matriz::db::Value::of(static_cast<int64_t>(projRev)),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(raizProjeto.getFullPathName().toStdString())});
                } else {
                    cloneDb.run(
                        "INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, "
                        "ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
                        "VALUES (?, ?, ?, ?, ?, 1, ?, ?, ?, ?)",
                        {matriz::db::Value::of(matriz::model::novoUuid()),
                         matriz::db::Value::of(destId),
                         matriz::db::Value::of(raizProjeto.getFullPathName().toStdString()),
                         matriz::db::Value::of(raizProjeto.getFileName().toStdString()),
                         matriz::db::Value::of(std::string("ORIGINAL")),
                         matriz::db::Value::of(cloneInfo.criadoEm),
                         matriz::db::Value::of(static_cast<int64_t>(projRev)),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc)});
                }
            } catch (...) {}

            // Logs
            matriz::model::ProjectLog activeLog(pastaProjeto);
            activeLog.appendEntry("Clone Created", {"Path: " + folder.getFullPathName(), "Destination ID: " + juce::String(cloneInfo.destinationId)});

            matriz::model::ProjectLog cloneLog(cloneProject);
            cloneLog.appendEntry("Clone Initialized", {"Origin: " + raizProjeto.getFullPathName()});

            juce::MessageManager::callAsync([safeThis, folderPath = folder.getFullPathName(), isPt]() {
                ProgressoGlobal::obterInstancia().concluirTarefa("clone", isPt ? "Clone criado com sucesso." : "Clone created successfully.");
                if (safeThis) {
                    safeThis->carregarDestinosBackup();
                    for (int i = 0; i < static_cast<int>(safeThis->destinosBackup_.size()); ++i) {
                        if (safeThis->destinosBackup_[static_cast<size_t>(i)].caminho == folderPath) {
                            safeThis->selectedDestinoIdx_ = i;
                            if (safeThis->listVaults_) safeThis->listVaults_->selectRow(i);
                            safeThis->resolvedDestFolder_ = juce::File(folderPath);
                            safeThis->customDestFolder_ = safeThis->resolvedDestFolder_;
                            safeThis->dispararScanDestino(true);
                            break;
                        }
                    }
                }

                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    isPt ? "Clone Criado" : "Clone Created",
                    isPt ? juce::String::fromUTF8("Novo clone criado com sucesso em:\n") + folderPath
                         : "New clone created successfully at:\n" + folderPath);
            });
        } catch (const std::exception& e) {
            juce::String erroMsg(e.what());
            juce::MessageManager::callAsync([isPt, erroMsg]() {
                ProgressoGlobal::obterInstancia().concluirTarefa("clone", isPt ? "Erro ao criar clone." : "Error creating clone.");
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    isPt ? "Erro ao Criar Clone" : "Clone Creation Error",
                    erroMsg);
            });
        }
    });
}

void BackupWorkspaceComponent::desvincularDestino(const DestinoBackupItem& dest) {
    if (dest.papel == "ORIGINAL") return;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        matriz::i18n::t("backup.desvincular"),
        matriz::i18n::t("backup.desvincular_confirmacao"),
        matriz::i18n::t("backup.desvincular"),
        isPt ? "Cancelar" : "Cancel",
        nullptr,
        juce::ModalCallbackFunction::create([this, dest](int result) {
            if (result != 1) return;

            auto& db = projeto_.projeto().registro();
            if (dest.id.empty())
                projeto_.registrarUndoDeColunas("Unlink Backup Destination", "backup_destino", "destino_path", dest.caminho.toStdString(), {"ativo"});
            else
                projeto_.registrarUndoDeColunas("Unlink Backup Destination", "backup_destino", "id", dest.id, {"ativo"});
            if (dest.id.empty()) {
                db.run("UPDATE backup_destino SET ativo = 0 WHERE destino_path = ?",
                       {matriz::db::Value::of(dest.caminho.toStdString())});
            } else {
                db.run("UPDATE backup_destino SET ativo = 0 WHERE id = ?",
                       {matriz::db::Value::of(dest.id)});
            }

            matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
            juce::StringArray details;
            details.add("Label: " + dest.rotulo);
            details.add("Path: " + dest.caminho);
            pLog.appendEntry("Backup Version Unlinked", details);

            carregarDestinosBackup();
            carregarDestinoAtivoInicial();
        }));
}

// --- ListBoxModel implementation for Destinations list ---

int BackupWorkspaceComponent::getNumRows() {
    return static_cast<int>(destinosBackup_.size());
}

void BackupWorkspaceComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber >= static_cast<int>(destinosBackup_.size())) return;
    const auto& tk = tema();
    const auto& dest = destinosBackup_[static_cast<size_t>(rowNumber)];
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (rowIsSelected) {
        g.fillAll(tk.acento.withAlpha(0.18f));
        g.setColour(tk.bordaFoco);
        g.drawRect(0, 0, width, height, 1);
    } else {
        g.fillAll(rowNumber % 2 == 0 ? tk.painel : tk.painelAlt);
    }

    int curX = 8;
    bool isOriginal = (dest.papel == "ORIGINAL");


    // Selo de papel: MAIN (fonte da verdade) ou CLONE.
    int tagW = 64;
    juce::Rectangle<int> papelArea(curX, (height - 20) / 2, tagW, 20);
    if (isOriginal) {
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(papelArea.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.drawRoundedRectangle(papelArea.toFloat().reduced(0.5f), 4.0f, 1.2f);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        g.drawText(matriz::i18n::t("backup.papel_main"), papelArea, juce::Justification::centred, true);
    } else {
        g.setColour(tk.painel.withAlpha(0.6f));
        g.fillRoundedRectangle(papelArea.toFloat(), 4.0f);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(papelArea.toFloat().reduced(0.5f), 4.0f, 1.0f);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        g.drawText(matriz::i18n::t("backup.papel_clone"), papelArea, juce::Justification::centred, true);
    }
    curX += tagW + 10;

    // Online/Offline status badge on the far right
    // No MAIN (só modo Coleção) o selo diz também se o MAIN já foi criado: até o 1º backup a pasta
    // existe, mas está vazia — "ONLINE" sozinho dava a entender que já havia backup.
    const bool mostrarSeloMain = isOriginal && projeto_.projeto().modo() != matriz::model::Modo::Catalogo;
    const bool mainAindaNaoCriado = mostrarSeloMain && !mainSelado_;
    int statusW = mostrarSeloMain ? 118 : 68;
    juce::Rectangle<int> statusArea(width - statusW - 8, (height - 18) / 2, statusW, 18);
    juce::String textoStatus = dest.online ? "ONLINE" : "OFFLINE";
    juce::Colour corStatus = dest.online ? tk.estadoQcOk.withAlpha(0.85f) : tk.textoTerciario.withAlpha(0.5f);
    if (mostrarSeloMain && dest.online) {
        textoStatus = matriz::i18n::t(mainAindaNaoCriado ? "backup.main_nao_criado" : "backup.main_criado");
        corStatus = mainAindaNaoCriado ? tk.alerta : tk.estadoQcOk.withAlpha(0.85f);
    }
    g.setColour(corStatus);
    g.fillRoundedRectangle(statusArea.toFloat(), 4.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
    g.drawText(textoStatus, statusArea, juce::Justification::centred, true);

    // Cloud icon on right side before status area, if cloud destination
    bool ehNuvem = ehDestinoNuvem(dest.caminho, dest.rotulo, dest.papel);
    int labelRight = statusArea.getX() - 10;
    if (ehNuvem) {
        labelRight -= 24;
        juce::Rectangle<float> cloudRect(static_cast<float>(labelRight + 4), static_cast<float>(height - 12) / 2.0f, 16.0f, 12.0f);
        juce::Colour cloudCol = (tk.fundo.getBrightness() > 0.5f) ? juce::Colour(0xff0284c7) : juce::Colour(0xff38bdf8);
        desenharIconeNuvem(g, cloudRect, cloudCol);
    }

    // Volume name gets all the breathing room between tag and status badge
    int labelW = std::max(20, labelRight - curX);
    g.setColour(isOriginal ? tk.textoPrimario : tk.textoPrimario.withAlpha(0.9f));
    auto fontRotulo = juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo - 0.5f, juce::Font::bold));
    g.setFont(fontRotulo);
    g.drawText(dest.rotulo, curX, 0, labelW, height, juce::Justification::centredLeft, true);
}

void BackupWorkspaceComponent::listBoxItemClicked(int rowNumber, const juce::MouseEvent& e) {
    if (rowNumber >= 0 && rowNumber < static_cast<int>(destinosBackup_.size())) {
        selectedDestinoIdx_ = rowNumber;
        if (listVaults_) listVaults_->selectRow(rowNumber);

        const auto& dest = destinosBackup_[static_cast<size_t>(rowNumber)];
        juce::String loc = dest.caminho;
        resolvedDestFolder_ = matriz::model::normalizarParaRaizDestino(juce::File(loc));
        customDestFolder_ = resolvedDestFolder_;
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        juce::String tipoStr = (dest.papel == "ORIGINAL")
            ? (isPt ? juce::String::fromUTF8("[MAIN - Versão Oficial]") : "[MAIN - Official Version]")
            : (isPt ? juce::String::fromUTF8("[CLONE - Cópia de Backup]") : "[CLONE - Backup Copy]");
        if (dest.papel == "ORIGINAL" && projeto_.projeto().modo() != matriz::model::Modo::Catalogo)
            tipoStr += "  " + matriz::i18n::t(mainSelado_ ? "backup.main_criado_info" : "backup.main_nao_criado_info");
        juce::String textoInfo = (isPt ? juce::String::fromUTF8("Selecionado: ") : "Selected: ") + loc + "  " + tipoStr;
        labelDestInfo_->setText(textoInfo, juce::dontSendNotification);
        labelDestInfo_->setTooltip(textoInfo);
        dispararScanDestino(true);

        if (e.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            menu.addSectionHeader(dest.caminho);
            juce::String itemTxt = isPt ? juce::String::fromUTF8("Revelar no Finder") : "Reveal in Finder";
            juce::File f(dest.caminho);
            menu.addItem(1, itemTxt, dest.online && f.exists());

            juce::String copyTxt = isPt ? juce::String::fromUTF8("Copiar Caminho") : "Copy Path";
            menu.addItem(3, copyTxt, true);

            if (dest.papel != "ORIGINAL") {
                menu.addSeparator();
                juce::String unlinkTxt = isPt ? juce::String::fromUTF8("Desvincular do Backup...")
                                              : "Unlink from Backup...";
                menu.addItem(2, unlinkTxt, true);
            }

            juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
            menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, dest](int res) {
                if (!safeThis) return;
                if (res == 1) {
                    juce::File alvo(dest.caminho);
                    if (alvo.exists()) {
                        alvo.revealToUser();
                    }
                } else if (res == 2) {
                    safeThis->desvincularDestino(dest);
                } else if (res == 3) {
                    juce::SystemClipboard::copyTextToClipboard(dest.caminho);
                }
            });
        }
    }
}

static juce::String formatarEspacoLivre(juce::int64 bytes) {
    if (bytes <= 0) return "0 B";
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024.0, 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return juce::String(bytes / (1024.0 * 1024.0), 1) + " MB";
    if (bytes < 1024LL * 1024LL * 1024LL * 1024LL) return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0), 2) + " TB";
}

juce::String BackupWorkspaceComponent::getTooltipForRow(int rowNumber) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(destinosBackup_.size())) return {};
    const auto& dest = destinosBackup_[static_cast<size_t>(rowNumber)];
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    juce::File caminhoF(dest.caminho);
    auto infoVol = matriz::vault::descreverVolume(caminhoF);
    juce::String nomeVolume = infoVol.nome.empty()
        ? (infoVol.hardware.volumeLabel.empty() ? caminhoF.getFileName() : juce::String(infoVol.hardware.volumeLabel))
        : juce::String(infoVol.nome);
    if (nomeVolume.isEmpty()) nomeVolume = "Macintosh HD";

    juce::String caminhoCompleto = dest.caminho;
    if (caminhoCompleto.isEmpty()) caminhoCompleto = caminhoF.getFullPathName();

    juce::int64 freeBytes = caminhoF.getBytesFreeOnVolume();
    juce::String freeSpaceStr = formatarEspacoLivre(freeBytes);

    if (isPt) {
        return juce::String::fromUTF8("Nome do Volume: ") + nomeVolume + "\n" +
               juce::String::fromUTF8("Espaço Livre: ") + freeSpaceStr + "\n" +
               juce::String::fromUTF8("Caminho Completo: ") + caminhoCompleto;
    }
    return "Volume Name: " + nomeVolume + "\n" +
           "Free Space: " + freeSpaceStr + "\n" +
           "Full Path: " + caminhoCompleto;
}

void BackupWorkspaceComponent::carregarDestinoAtivoInicial() {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    resolvedDestFolder_ = projeto_.projeto().raiz();
    if (projeto_.emRebuild()) resolvedDestFolder_ = projeto_.rebuildDestino();  // vazio até a pasta nova ser escolhida
    customDestFolder_ = resolvedDestFolder_;
    selectedDestinoIdx_ = -1;

    for (size_t i = 0; i < destinosBackup_.size(); ++i) {
        if (destinosBackup_[i].caminho == projeto_.projeto().raiz().getFullPathName() ||
            (selectedDestinoIdx_ < 0 && destinosBackup_[i].papel == "ORIGINAL")) {
            selectedDestinoIdx_ = static_cast<int>(i);
            if (destinosBackup_[i].caminho == projeto_.projeto().raiz().getFullPathName()) break;
        }
    }
    if (selectedDestinoIdx_ < 0 && !destinosBackup_.empty()) {
        selectedDestinoIdx_ = 0;
    }

    if (selectedDestinoIdx_ >= 0 && selectedDestinoIdx_ < static_cast<int>(destinosBackup_.size())) {
        if (listVaults_) listVaults_->selectRow(selectedDestinoIdx_);
        const auto& dest = destinosBackup_[static_cast<size_t>(selectedDestinoIdx_)];
        resolvedDestFolder_ = matriz::model::normalizarParaRaizDestino(juce::File(dest.caminho));
        customDestFolder_ = resolvedDestFolder_;
        if (labelDestInfo_) {
            juce::String tipoStr = (dest.papel == "ORIGINAL")
                ? (isPt ? juce::String::fromUTF8("[MAIN - Versão Oficial]") : "[MAIN - Official Version]")
                : (isPt ? juce::String::fromUTF8("[CLONE - Cópia de Backup]") : "[CLONE - Backup Copy]");
            juce::String activeInfo = (isPt ? juce::String::fromUTF8("Versão Ativa: ") : "Active Version: ")
                                    + dest.caminho + "  " + tipoStr;
            labelDestInfo_->setText(activeInfo, juce::dontSendNotification);
            labelDestInfo_->setTooltip(activeInfo);
        }
    }

    atualizarResumo();
}

void BackupWorkspaceComponent::dispararScanDestino(bool forcado) {
    (void)forcado;
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    if (isCatalogMode) {
        atualizarResumo();
        return;
    }

    // Plano (jaConsolidado por destino_path) do destino RECÉM destacado
    // antes do scan — sem isto o scan/lista usavam o plano do anterior. O
    // plano é calculado em background: o resto roda quando ele chegar. Se já
    // há um scan agendado, ele pega o destino atual na hora de rodar.
    atualizarResumo();
    if (scanAposPlano_) return;
    scanAposPlano_ = true;
    juce::Component::SafePointer<BackupWorkspaceComponent> aguardaPlano(this);
    aoPlanoPronto([aguardaPlano] {
        if (aguardaPlano == nullptr) return;
        aguardaPlano->scanAposPlano_ = false;
        aguardaPlano->continuarScanDestino();
    });
}

void BackupWorkspaceComponent::continuarScanDestino() {
    juce::File destinoRaiz = matriz::model::normalizarParaRaizDestino(resolvedDestFolder_);
    juce::File targetDir = destinoRaiz.getChildFile("Media");
    if (!targetDir.isDirectory() || plano_.itens.empty()) {
        scanResult_.itensGrid.clear();
        scanResult_.orfaos.clear();
        scanResult_.totalVerdes = 0;
        scanResult_.totalVermelhos = static_cast<int>(plano_.itens.size());
        scanResult_.totalOrfaos = 0;
        for (const auto& ip : plano_.itens) {
            matriz::consolidacao::ItemStatusBackup isb;
            isb.itemId = ip.itemId;
            isb.arquivoId = ip.arquivoId;
            isb.codigoAcervo = ip.codigoAcervo;
            isb.nomeArquivo = ip.nomeOriginal;
            isb.caminhoRelativoDestino = ip.caminhoRelativoDestino;
            isb.tamanhoBytes = ip.tamanhoBytes;
            isb.status = matriz::consolidacao::StatusArquivoBackup::Vermelho;
            scanResult_.itensGrid.push_back(std::move(isb));
        }
        if (listPrevia_) {
            listPrevia_->definirScan(scanResult_);
            listPrevia_->definirOrfaos(scanResult_.orfaos, targetDir);
        }
        atualizarResumo();
        return;
    }

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    BackupScanProgressDialog::showModal(
        projeto_.projeto().registro(),
        projeto_.projeto().pasta(),
        targetDir,
        plano_,
        [safeThis, targetDir](const matriz::consolidacao::ResultadoScanBackup& result) {
            if (!safeThis) return;
            safeThis->scanResult_ = result;
            safeThis->scanRealizado_ = true;
            if (safeThis->listPrevia_) {
                safeThis->listPrevia_->definirScan(result);
                safeThis->listPrevia_->definirOrfaos(result.orfaos, targetDir);
            }
            safeThis->atualizarResumo();
        }
    );
}

void BackupWorkspaceComponent::executarBackupAcao(bool forcarOverride) {
    if (toggleForcarRebackup_)
        toggleForcarRebackup_->setToggleState(forcarOverride, juce::dontSendNotification);
    iniciarBackup();
}

void BackupWorkspaceComponent::iniciarSyncComOutroDestino() {
    if (saidaBloqueadaSemMain()) return;  // nada sai do projeto antes do MAIN
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    ProjetoAberto* p = &projeto_;
    SyncDestinationDialog::abrirModal(
        projeto_.projeto(), [safeThis] { if (safeThis) safeThis->recarregar(); },
        [p](const std::string& destinoId, const std::vector<std::string>& colunas) {
            p->registrarUndoDeColunas("Backup Destination", "backup_destino", "id", destinoId, colunas);
        });
}

void BackupWorkspaceComponent::registrarDestinoBackup(const juce::File& destFolder, const juce::String& rotuloSugerido,
                                                      int copiado, int pulados, int falhas, bool cancelado)
{
    if (cancelado) return;

    adicionarOuAtivarDestino(destFolder, rotuloSugerido);

    std::string rotulo = rotuloSugerido.trim().isNotEmpty() ? rotuloSugerido.toStdString() : destFolder.getFileName().toStdString();
    if (rotulo.empty()) rotulo = "Backup";

    // Log: Backup Completed
    matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
    juce::StringArray details;
    details.add("Destination: " + juce::String::fromUTF8(rotulo.c_str()) + " (" + destFolder.getFullPathName() + ")");
    details.add(toggleForcarRebackup_ && toggleForcarRebackup_->getToggleState() ? "Mode: Force Override" : "Mode: Incremental");
    details.add("Copied: " + juce::String(copiado));
    details.add("Skipped: " + juce::String(pulados));
    details.add("Failures: " + juce::String(falhas));
    pLog.appendEntry("Backup Completed", details);

    // Register in Device Usage Log (persists to DB and mirrors to destination external drive)
    try {
        auto& db = projeto_.projeto().registro();
        std::string vId = matriz::vault::obterOuCriarVaultParaDestino(db, destFolder, projeto_.projeto().projetoId());

        juce::StringArray arquivosCopiados;
        juce::int64 bytesCopiados = 0;
        for (const auto& item : scanResult_.itensGrid) {
            arquivosCopiados.add(item.caminhoRelativoDestino);
            bytesCopiados += item.tamanhoBytes;
        }

        juce::String detalhesUso = "Backup: " + juce::String(copiado) + " copied, "
                                 + juce::String(pulados) + " skipped, "
                                 + juce::String(falhas) + " failed";

        matriz::vault::registrarUsoDoDispositivo(
            db,
            projeto_.projeto().pasta(),
            vId,
            "BACKUP",
            copiado,
            bytesCopiados,
            arquivosCopiados,
            detalhesUso.toStdString());
    } catch (...) {}

    // Update scanResult_ in memory
    for (auto& isb : scanResult_.itensGrid) {
        isb.status = matriz::consolidacao::StatusArquivoBackup::Verde;
    }
    scanResult_.totalVerdes = static_cast<int>(scanResult_.itensGrid.size());
    scanResult_.totalVermelhos = 0;
    if (listPrevia_) listPrevia_->definirScan(scanResult_);
}

void BackupWorkspaceComponent::mostrarControlesConfig(bool mostrar) {
    if (configViewport_) configViewport_->setVisible(mostrar);
    if (listPreviaViewport_) listPreviaViewport_->setVisible(mostrar);
    if (labelResumo_) labelResumo_->setVisible(mostrar);
    if (labelAssets_) labelAssets_->setVisible(mostrar);
    if (projeto_.projeto().modo() != matriz::model::Modo::Catalogo) {  // (6) BACKUP VERSIONS fica fora da coluna
        for (juce::Component* c : {static_cast<juce::Component*>(labelDest_.get()), static_cast<juce::Component*>(listVaults_.get()),
                                   static_cast<juce::Component*>(btnBrowseVault_.get()), static_cast<juce::Component*>(btnGoogleDriveDest_.get()),
                                   static_cast<juce::Component*>(labelDestInfo_.get())})
            if (c) c->setVisible(mostrar);
    }
    if (barraLegenda_) barraLegenda_->setVisible(mostrar && projeto_.projeto().modo() != matriz::model::Modo::Catalogo);
    if (comboColecoes_) comboColecoes_->setVisible(mostrar && comboSource_->getSelectedId() == 5);
    if (btnEditarHierarquia_) btnEditarHierarquia_->setVisible(mostrar && comboOrg_->getSelectedId() == 5);
}

void BackupWorkspaceComponent::paint(juce::Graphics& g) {
    if (botoesPendentes_ || resumoDeTituloPendente_) agendarAplicarPendentes();  // voltou a aparecer por um ancestral
    const auto& tk = tema();
    bool isLight = (tk.fundo.getBrightness() > 0.5f);
    juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
    g.fillAll(bg);

    // Cabeçalho como faixa, separado por fio de 1px; o rodapé virou um card (EXPORT + botão principal).
    g.setColour(tk.borda);
    if (!faixaCabecalho_.isEmpty())
        g.fillRect(faixaCabecalho_.getX(), faixaCabecalho_.getBottom() - 1, faixaCabecalho_.getWidth(), 1);
    const bool emFluxo = estado_ == Estado::Config && !numeroAssets_.isEmpty();
    auto cartaoSimples = [&](juce::Rectangle<int> r) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(r.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
    };
    if (!cartaoRodape_.isEmpty() && estado_ == Estado::Done) cartaoSimples(cartaoRodape_);
    for (const auto& r : {cartaoAcao_, cartaoSaidas_, cartaoBounce_}) {
        if (r.isEmpty()) continue;
        if (emFluxo) desenharCartaoFluxo(g, r);
        else cartaoSimples(r);
    }
    if (!cartaoBounce_.isEmpty()) {
        const juce::Colour roxo(0xff9333ea);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(matriz::i18n::t("bounce.card_titulo"), bounceTituloR_, juce::Justification::centredLeft);
        auto tituloLinha = bounceTituloR_;  // cópia: cortar o membro a cada paint empurrava o selo para a esquerda
        const auto selo = tituloLinha.removeFromRight(110).withSizeKeepingCentre(110, 18);
        g.setColour(roxo.withAlpha(0.18f));
        g.fillRoundedRectangle(selo.toFloat(), 4.0f);
        g.setColour(roxo);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 1.0f, juce::Font::bold)));
        g.drawText(matriz::i18n::t("bounce.selo"), selo, juce::Justification::centred);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawFittedText(matriz::i18n::t("bounce.desc"), bounceDescR_, juce::Justification::topLeft, 3, 1.0f);
        g.setColour(tk.fundo);
        g.fillRoundedRectangle(bounceContagemR_.toFloat(), 4.0f);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(bounceContagemR_.toFloat().reduced(0.5f), 4.0f, 1.0f);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("bounce.contagem").replace("{n}", juce::String(nBounce_)),
                   bounceContagemR_.reduced(8, 0), juce::Justification::centredLeft);
    }
    if (emFluxo && !cartaoAcao_.isEmpty() && !cartaoSaidas_.isEmpty()) {  // (6) -> (7): sai do lado esquerdo do RUN
        desenharSetaVazadaSaindoDaEsquerda(g, cartaoAcao_, static_cast<float>(numeroAcao_.getCentreY()),
                                           static_cast<float>(cartaoSaidas_.getRight()) + 2.0f);
    }

    if (!cartaoPrevia_.isEmpty() && estado_ == Estado::Config) {
        if (emFluxo) {
            desenharCartaoFluxo(g, cartaoPrevia_);
        } else {
            g.setColour(tk.painel);
            g.fillRoundedRectangle(cartaoPrevia_.toFloat(), tk.raioMedio);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(cartaoPrevia_.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
        }
    }

    // Fluxo: 1 (ASSETS) -> 2 (SELECTION) ... 5 (OPTIONS) -> 6 (botão principal). As setas entre os cards da coluna
    // ficam no ConfigContainerComponent (rolam junto); aqui, as que ligam a coluna ao ASSETS e ao botão.
    if (estado_ == Estado::Config && !numeroAssets_.isEmpty()) {
        desenharNumeroFluxo(g, numeroAssets_, 1);
        desenharNumeroFluxo(g, numeroAcao_, 6);
        desenharNumeroFluxo(g, numeroSaidas_, 7);
        if (configViewport_ && configContainer_ && configViewport_->isVisible()) {
            const auto vista = configViewport_->getBounds();
            const auto deslocamento = configViewport_->getPosition() - configViewport_->getViewPosition();
            const auto& cartoesColuna = configContainer_->cartoes();
            if (!cartoesColuna.empty()) {  // ASSETS -> SELECTION, na altura do título do ASSETS
                const auto selecao = cartoesColuna.front() + deslocamento;
                const int y = numeroAssets_.getCentreY();
                if (y >= std::max(selecao.getY(), vista.getY()) + 14 && y <= std::min(selecao.getBottom(), vista.getBottom()) - 14)
                    desenharSetaVazadaSaindoDaDireita(g, cartaoPrevia_, static_cast<float>(y),
                                                      static_cast<float>(selecao.getX()) - 2.0f);
            }
        }
    }

    if (!cartaoCentral_.isEmpty()) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(cartaoCentral_.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cartaoCentral_.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
    }
}

// BACKUP VERSIONS (último da coluna) -> card (6) RUN: a seta nasce da borda de baixo do último card, que é de um filho (a coluna
// no viewport) — por isso por cima dos filhos. Só quando o OPTIONS está à vista; a área coberta é o respiro do card.
void BackupWorkspaceComponent::paintOverChildren(juce::Graphics& g) {
    if (estado_ != Estado::Config || numeroAssets_.isEmpty() || !configViewport_ || !configContainer_) return;
    if (!configViewport_->isVisible()) return;
    const auto& cartoesColuna = configContainer_->cartoes();
    if (cartoesColuna.size() < 4) return;
    const auto vista = configViewport_->getBounds();
    const auto opcoes = cartoesColuna.back() + (configViewport_->getPosition() - configViewport_->getViewPosition());
    if (opcoes.getBottom() > vista.getBottom() || opcoes.getBottom() <= vista.getY()) return;
    if (cartaoAcao_.isEmpty()) return;
    const float x = static_cast<float>(opcoes.getRight() - 34);  // mesma posição das setas entre os cards da coluna
    desenharSetaVazadaSaindoDeBaixo(g, opcoes, x, static_cast<float>(cartaoAcao_.getY()) - 2.0f);
}

void BackupWorkspaceComponent::resized() {
    const auto& tk = tema();
    // Coleção: o RUN (5) mora na coluna de configuração e o BACKUP VERSIONS (6) no rodapé; Catálogo: o contrário,
    // como sempre foi. Só muda o pai dos controles (a visibilidade de cada um é mantida).
    if (configContainer_) {
        const bool coluna5Run = projeto_.projeto().modo() != matriz::model::Modo::Catalogo;
        juce::Component* paraColuna = coluna5Run ? static_cast<juce::Component*>(configContainer_.get()) : this;
        juce::Component* paraRodape = coluna5Run ? static_cast<juce::Component*>(this) : configContainer_.get();
        for (juce::Component* c : {static_cast<juce::Component*>(btnStartBackup_.get()), static_cast<juce::Component*>(btnRebuildStatus_.get()),
                                   static_cast<juce::Component*>(labelAcao_.get())})
            if (c && c->getParentComponent() != paraColuna) paraColuna->addChildComponent(c);
        for (juce::Component* c : {static_cast<juce::Component*>(labelDest_.get()), static_cast<juce::Component*>(listVaults_.get()),
                                   static_cast<juce::Component*>(btnBrowseVault_.get()), static_cast<juce::Component*>(btnGoogleDriveDest_.get()),
                                   static_cast<juce::Component*>(labelDestInfo_.get())})
            if (c && c->getParentComponent() != paraRodape) paraRodape->addChildComponent(c);
    }
    cartaoPrevia_ = {};
    cartaoCentral_ = {};

    const bool emAndamento = (estado_ == Estado::Running || estado_ == Estado::Done);
    mostrarControlesConfig(!emAndamento);

    auto area = getLocalBounds();

    // ---- Cabeçalho: só no backup em andamento/concluído ("Backup Complete"...). Na configuração o título
    // "Backup Configuration" saiu: o espaço vai para os vãos entre os cards.
    faixaCabecalho_ = {};
    if (labelTitulo_) labelTitulo_->setVisible(emAndamento);
    if (emAndamento) faixaCabecalho_ = area.removeFromTop(64);
    if (labelTitulo_ && emAndamento) {
        auto rCabecalho = faixaCabecalho_.reduced(tk.espacoGrande * 2, 0)
                                         .withTrimmedBottom(tk.espacoMedio)
                                         .removeFromBottom(34);
        labelTitulo_->setBounds(rCabecalho);
    }

    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    const bool fluxo = !isCatalogMode;  // setas e números do fluxo (Coleção)
    bool temItens = !plano_.itens.empty();
    if (isCatalogMode) {
        auto colecoes = projeto_.listarColecoesLinkadas();
        for (const auto& c : colecoes) {
            if (c.valido && c.totalAssets > 0) {
                temItens = true;
                break;
            }
        }
    }
    numeroAssets_ = {};
    numeroAcao_ = {};
    numeroSaidas_ = {};
    cartaoRodape_ = {};
    cartaoAcao_ = {};
    cartaoSaidas_ = {};
    const int alturaBotao = 40;

    if (estado_ == Estado::Done) {
        // ---- Rodapé do resultado: card único na largura do conteúdo ----
        faixaRodape_ = area.removeFromBottom(72);
        cartaoRodape_ = faixaRodape_.reduced(tk.espacoGrande, tk.espacoMedio);
        auto botoes = cartaoRodape_.reduced(tk.espacoPainel + 2, 0);
        botoes = botoes.withSizeKeepingCentre(botoes.getWidth(), alturaBotao);
        for (auto* l : {labelAcao_.get(), labelSaidas_.get()}) if (l) l->setVisible(false);
        btnDone_->setBounds(botoes.removeFromRight(110));
        btnDone_->setVisible(true);
        btnStartBackup_->setVisible(false);
        if (btnExportar_) btnExportar_->setVisible(false);
        if (comboExportOrigem_) comboExportOrigem_->setVisible(false);
        if (btnExportUnificado_) btnExportUnificado_->setVisible(false);
        cartaoBounce_ = {};
        for (auto* c : {btnBounceAjustes_.get(), btnBounceExport_.get()}) if (c) c->setVisible(false);
        if (btnExportJanela_) btnExportJanela_->setVisible(false);
        if (btnSyncDestino_) btnSyncDestino_->setVisible(false);
        if (btnPublishHtml_) btnPublishHtml_->setVisible(false);
        if (btnExportZip_) btnExportZip_->setVisible(false);
        if (btnLimparZip_) btnLimparZip_->setVisible(false);
        if (btnSendToPrint_) btnSendToPrint_->setVisible(false);
        if (btnLimparPrint_) btnLimparPrint_->setVisible(false);
        if (btnExportWatermark_) btnExportWatermark_->setVisible(false);
        if (btnLimparWatermark_) btnLimparWatermark_->setVisible(false);
        if (btnCancelarExecucao_) btnCancelarExecucao_->setVisible(false);
        botoes.removeFromRight(tk.espacoPainel);
        if (btnRebuildStatus_) btnRebuildStatus_->setVisible(false);
        if (btnVerErros_) {
            const bool comErros = falhasCount_ > 0 && relatorioErros_.isNotEmpty();
            btnVerErros_->setVisible(comErros);
            if (comErros) {
                btnVerErros_->setBounds(botoes.removeFromRight(150));
                botoes.removeFromRight(tk.espacoPainel);
            }
        }
        if (btnOpenCatalog_) {
            btnOpenCatalog_->setBounds(botoes.removeFromRight(200));
            btnOpenCatalog_->setVisible(true);
        }
    } else {
        // ---- Rodapé em dois cards, nas mesmas colunas do corpo. À direita, na largura da coluna de configuração:
        // Coleção = (6) BACKUP VERSIONS (o RUN é o card 5, na coluna); Catálogo = RUN. À esquerda o (7) OUTPUTS,
        // na largura do ASSETS, com PUBLISH · dropdown [×] · EXPORT. Seta do lado esquerdo do (6) para o (7).
        faixaRodape_ = area.removeFromBottom(fluxo ? 188 : 96);
        auto linha = faixaRodape_.reduced(tk.espacoGrande, tk.espacoMedio);
        const int larguraConfigRodape = std::min(420, std::max(340, static_cast<int>(linha.getWidth() * 0.32f)));
        cartaoAcao_ = linha.removeFromRight(larguraConfigRodape);
        linha.removeFromRight(fluxo ? 52 : 28);
        cartaoBounce_ = {};
        if (!isCatalogMode) {
            // OUTPUTS FROM THE MAIN ocupa só a largura dos seus controles (PUBLISH 175 · dropdown 230 · [×] 26 · EXPORT 100
            // e os vãos); a sobra vai para o card BOUNCE LIST, à esquerda, abaixo do ASSETS.
            const int larguraControlesSaidas = 175 + tk.espacoPainel + 230 + tk.espacoPequeno + 26 + tk.espacoPainel + 100;
            const int larguraSaidas = larguraControlesSaidas + 2 * (tk.espacoPainel + 2);
            const int minBounce = 250;
            const int larguraFinal = std::min(larguraSaidas, std::max(0, linha.getWidth() - minBounce - tk.espacoGrande));
            cartaoSaidas_ = linha.removeFromRight(larguraFinal);
            linha.removeFromRight(tk.espacoGrande);
            cartaoBounce_ = linha;
        } else {
            cartaoSaidas_ = linha;
        }

        if (btnVerErros_) btnVerErros_->setVisible(false);
        if (btnOpenCatalog_) btnOpenCatalog_->setVisible(false);
        btnDone_->setVisible(false);
        if (btnCancelarExecucao_) btnCancelarExecucao_->setVisible(false);

        // BACKUP SYNC... mora na ponta direita da linha de título do card de baixo/direita.
        auto posicionarSyncNoTitulo = [this](juce::Rectangle<int>& linhaTitulo) {
            if (!btnSyncDestino_) return;
            auto area = linhaTitulo.removeFromRight(124);
            linhaTitulo.removeFromRight(4);
            btnSyncDestino_->setBounds(area.withSizeKeepingCentre(area.getWidth(), 22));
            btnSyncDestino_->setVisible(true);
        };
        auto dentroAcao = cartaoAcao_.reduced(10, 8);  // mesmo recuo dos cards da coluna (10 x 8)
        if (btnRebuildStatus_) btnRebuildStatus_->setVisible(projeto_.emRebuild());
        btnStartBackup_->setVisible(true);
        if (fluxo) {
            // (6) BACKUP VERSIONS — mesmos controles e arrumação de quando ficava na coluna. O RUN (5) é posicionado
            // pelo ConfigContainerComponent.
            if (labelAcao_) labelAcao_->setVisible(true);
            auto linhaTitulo = dentroAcao.removeFromTop(20);
            posicionarSyncNoTitulo(linhaTitulo);
            numeroAcao_ = linhaTitulo.removeFromLeft(kLarguraNumeroFluxo);
            if (labelDest_) labelDest_->setBounds(linhaTitulo);
            dentroAcao.removeFromTop(2);
            const int alturaDestInfo = 20, alturaControleDest = 26;
            const int reservado = alturaControleDest + 4 + alturaDestInfo;
            if (listVaults_) listVaults_->setBounds(dentroAcao.removeFromTop(std::max(24, dentroAcao.getHeight() - reservado - 4)));
            dentroAcao.removeFromTop(4);
            {
                auto linhaBrowse = dentroAcao.removeFromTop(alturaControleDest);
                if (btnGoogleDriveDest_) {
                    btnGoogleDriveDest_->setBounds(linhaBrowse.removeFromRight(32));
                    linhaBrowse.removeFromRight(tk.espacoPequeno);
                }
                if (btnBrowseVault_) btnBrowseVault_->setBounds(linhaBrowse);
            }
            dentroAcao.removeFromTop(2);
            if (labelDestInfo_) labelDestInfo_->setBounds(dentroAcao.removeFromTop(std::min(alturaDestInfo, dentroAcao.getHeight())));
        } else {
            // RUN (Catálogo) — título + botão principal
            {
                auto linhaTitulo = dentroAcao.removeFromTop(20);
                posicionarSyncNoTitulo(linhaTitulo);
                if (labelAcao_) { labelAcao_->setBounds(linhaTitulo); labelAcao_->setVisible(true); }
                dentroAcao.removeFromTop(4);
            }
            auto linhaAcao = dentroAcao.removeFromTop(alturaBotao);
            if (btnRebuildStatus_ && btnRebuildStatus_->isVisible()) {
                btnRebuildStatus_->setBounds(linhaAcao.removeFromLeft(std::min(190, linhaAcao.getWidth() / 2)));
                linhaAcao.removeFromLeft(tk.espacoPainel);
            }
            btnStartBackup_->setBounds(linhaAcao);
        }

        {
            const bool mostrarBounce = !cartaoBounce_.isEmpty();
            for (auto* c : {btnBounceAjustes_.get(), btnBounceExport_.get()}) if (c) c->setVisible(mostrarBounce);
            if (mostrarBounce) {
                auto dentro = cartaoBounce_.reduced(tk.espacoPainel + 2, 8);
                bounceTituloR_ = dentro.removeFromTop(20);
                dentro.removeFromTop(4);
                bounceDescR_ = dentro.removeFromTop(34);
                dentro.removeFromTop(6);
                auto linhaContagem = dentro.removeFromTop(28);
                if (btnBounceAjustes_) btnBounceAjustes_->setBounds(linhaContagem.removeFromRight(100));
                linhaContagem.removeFromRight(tk.espacoPequeno);
                bounceContagemR_ = linhaContagem;
                dentro.removeFromTop(6);
                if (btnBounceExport_) btnBounceExport_->setBounds(dentro.removeFromTop(alturaBotao));
            }
        }

        // (7) OUTPUTS — mesmo recuo horizontal do card ASSETS
        auto dentroSaidas = cartaoSaidas_.reduced(tk.espacoPainel + 2, 8);
        {
            auto linhaTitulo = dentroSaidas.removeFromTop(20);
            if (fluxo) numeroSaidas_ = linhaTitulo.removeFromLeft(kLarguraNumeroFluxo);
            if (labelSaidas_) {
                labelSaidas_->setText(isCatalogMode ? matriz::i18n::t("backup.secao_saidas_catalogo")
                                                    : matriz::i18n::t("backup.secao_saidas"), juce::dontSendNotification);
                labelSaidas_->setBounds(linhaTitulo);
                labelSaidas_->setVisible(true);
            }
            dentroSaidas.removeFromTop(4);
        }
        auto botoes = dentroSaidas.removeFromTop(alturaBotao);
        // Grupo de export, nesta ordem: PUBLISH TO HTML · dropdown [×] · EXPORT (o BACKUP SYNC... mora no título do card de baixo/direita).
        // EXPORT unificado: [dropdown][×][EXPORT] no lugar dos 4 botões de saída. Os antigos
        // seguem existindo (escondidos) e são disparados pelas opções do dropdown.
        for (auto* antigo : {btnExportar_.get(), btnExportZip_.get(), btnSendToPrint_.get(), btnExportWatermark_.get(), btnExportJanela_.get()})
            if (antigo) antigo->setVisible(false);
        if (btnPublishHtml_) {
            btnPublishHtml_->setBounds(botoes.removeFromLeft(175));
            btnPublishHtml_->setVisible(true);
            if (isCatalogMode) btnPublishHtml_->setEnabled(temItens);  // Catálogo: regra de antes
        }
        botoes.removeFromLeft(tk.espacoPainel);
        if (btnExportUnificado_ && comboExportOrigem_) {
            comboExportOrigem_->setBounds(botoes.removeFromLeft(230));
            comboExportOrigem_->setVisible(true);
            const int opcaoExport = comboExportOrigem_->getSelectedId();
            juce::TextButton* limpar = opcaoExport == kExpZip ? btnLimparZip_.get()
                                     : opcaoExport == kExpPrint ? btnLimparPrint_.get()
                                     : opcaoExport == kExpWatermark ? btnLimparWatermark_.get() : nullptr;
            for (auto* l : {btnLimparZip_.get(), btnLimparPrint_.get(), btnLimparWatermark_.get()})
                if (l && l != limpar) l->setVisible(false);
            if (limpar) {
                botoes.removeFromLeft(tk.espacoPequeno);
                limpar->setBounds(botoes.removeFromLeft(26));
                limpar->setVisible(true);
            }
            botoes.removeFromLeft(tk.espacoPainel);
            btnExportUnificado_->setBounds(botoes.removeFromLeft(100));
            btnExportUnificado_->setVisible(true);
        }
    }

    atualizarBotoesDependentesDoMain();  // Coleção: só o MAIN decide (4 botões de saída)

    // ---- Running / Done: um cartão só, centrado ----
    if (emAndamento) {
        const int larguraCartao = std::min(760, area.getWidth() - tk.espacoGrande * 2);
        auto corpo = area.reduced(tk.espacoGrande, tk.espacoGrande);
        cartaoCentral_ = corpo.withSizeKeepingCentre(larguraCartao, std::min((etapaCopy_ && etapaCopy_->isVisible()) ? 440 : 320, corpo.getHeight()));

        auto dentro = cartaoCentral_.reduced(tk.espacoGrande * 2, tk.espacoGrande);

        labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
        labelProgressoStatus_->setJustificationType(juce::Justification::centred);
        labelProgressoStatus_->setBounds(dentro.removeFromTop(40));
        dentro.removeFromTop(tk.espacoMedio);

        if (barraProgresso_->isVisible()) {
            barraProgresso_->setBounds(dentro.removeFromTop(24));
            dentro.removeFromTop(tk.espacoMedio);
        }
        if (etapaCopy_ && etapaCopy_->isVisible()) {
            etapaCopy_->setBounds(dentro.removeFromTop(64));
            dentro.removeFromTop(tk.espacoMedio);
            etapaCatalog_->setBounds(dentro.removeFromTop(64));
            dentro.removeFromTop(tk.espacoMedio);
        }

        if (estado_ == Estado::Running && btnCancelarExecucao_) {
            btnCancelarExecucao_->setBounds(dentro.removeFromBottom(28).withSizeKeepingCentre(120, 28));
            btnCancelarExecucao_->setVisible(true);
            dentro.removeFromBottom(tk.espacoPequeno);
        } else if (btnCancelarExecucao_) {
            btnCancelarExecucao_->setVisible(false);
        }

        labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        labelResumo_->setColour(juce::Label::textColourId, tk.textoSecundario);
        labelResumo_->setJustificationType(juce::Justification::centredTop);
        labelResumo_->setBounds(dentro);
        return;
    }

    // ---- Config: ASSETS (prévia) à esquerda ocupando o resto; coluna de configuração à direita, mesma largura ----
    auto corpo = area.reduced(tk.espacoGrande, tk.espacoGrande);

    const int larguraConfig = std::min(420, std::max(340, static_cast<int>(corpo.getWidth() * 0.32f)));
    auto colunaConfig = corpo.removeFromRight(larguraConfig);
    corpo.removeFromRight(fluxo ? 52 : 28);  // vão entre ASSETS e a coluna (seta vazada ASSETS -> SELECTION)
    auto colunaPrevia = corpo;

    if (configViewport_ && configContainer_) {
        configViewport_->setBounds(colunaConfig);
        // REGRA: a coluna NUNCA rola. Os vãos entre os cards (comprimento do corpo das setas) é que cedem espaço.
        configViewport_->setScrollBarsShown(false, false);
        configContainer_->setBounds(0, 0, colunaConfig.getWidth(), colunaConfig.getHeight());
        configContainer_->resized();
    }

    // PREVIEW — cartão único ocupando a coluna inteira com lista unificada de arquivos
    cartaoPrevia_ = colunaPrevia;
    const int padCartao = tk.espacoPainel + 2;
    const int alturaCabecalhoSecao = 26;
    auto dentroPrevia = colunaPrevia.reduced(padCartao, padCartao);

    if (labelAssets_) {  // título ASSETS, mesma altura dos títulos dos cards da coluna
        auto linhaTitulo = dentroPrevia.removeFromTop(20);
        if (fluxo) numeroAssets_ = linhaTitulo.removeFromLeft(kLarguraNumeroFluxo);
        labelAssets_->setBounds(linhaTitulo);
        labelAssets_->setVisible(true);
        dentroPrevia.removeFromTop(2);
    }

    labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    labelResumo_->setJustificationType(juce::Justification::centredLeft);
    labelResumo_->setBounds(dentroPrevia.removeFromTop(alturaCabecalhoSecao));

    dentroPrevia.removeFromTop(4);

    if (barraLegenda_) {
        barraLegenda_->setBounds(dentroPrevia.removeFromTop(20));
        barraLegenda_->setVisible(!isCatalogMode);
    }
    dentroPrevia.removeFromTop(tk.espacoPequeno);

    listPreviaViewport_->setBounds(dentroPrevia);
    if (listPrevia_) listPrevia_->setSize(dentroPrevia.getWidth(), listPrevia_->getHeight());

    overlay_.setBounds(getLocalBounds());
}

void BackupWorkspaceComponent::voltarParaConfiguracao() {
    if (estado_ != Estado::Done) return;
    estado_ = Estado::Config;
    executando_ = false;
    progressoValor_ = 0.0;
    copiadoCount_ = 0;
    verificadoCount_ = 0;
    falhasCount_ = 0;
    falhasLista_.clear();
    relatorioErros_.clear();
    if (btnVerErros_) btnVerErros_->setVisible(false);
    barraProgresso_->setVisible(false);
    etapaCopy_->setVisible(false);
    etapaCatalog_->setVisible(false);
    labelProgressoStatus_->setVisible(false);
    labelProgressoStatus_->setText({}, juce::dontSendNotification);
    labelProgressoStatus_->setColour(juce::Label::textColourId, tema().textoPrimario);
    const bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    labelTitulo_->setText(isCatalogMode ? matriz::i18n::t("backup.titulo_catalogo") : matriz::i18n::t("backup.titulo_configuracao"),
                          juce::dontSendNotification);
    btnDone_->setVisible(false);
    if (btnOpenCatalog_) btnOpenCatalog_->setVisible(false);
    recarregar();
    resized();
}

void BackupWorkspaceComponent::recarregar() {
    MATRIZ_TRACE("BackupWorkspaceComponent::recarregar");
    {
        // atualizarResumo() só roda UMA vez, no fim do bloco (carregarDestinoAtivoInicial
        // e quem mais pedir dentro daqui só marcam pendente).
        AdiaResumo umaVez(*this);
        aplicarRascunhoOrganizacao();
        carregarOpcoesContent();
        carregarColecoesBackupCatalogo();
        carregarDestinosBackup();
        carregarDestinoAtivoInicial();
        resumoPendente_ = true;
    }
    atualizarBotoesListas();
    // recarregar() já refez botões e prévia: nada mais a aplicar do que chegou escondida.
    botoesPendentes_ = false;
    resumoDeTituloPendente_ = false;
    repaint();
}

} // namespace matriz::ui
