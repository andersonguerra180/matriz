#include "RevisaoConflitosMergeComponent.h"
#include "AcoesItem.h"
#include "ContainerVisual.h"
#include "PastaPickerWindow.h"
#include "../Diag/Watchdog.h"

#include "../Consolidacao/Mascara.h"
#include "../I18n/Strings.h"
#include "SelecionarTipoMidiaDialogo.h"
#include "Tokens.h"
#include "Moldura.h"
#include "ModalMitigacao.h"
#include "EventBus.h"
#include "ProgressoGlobal.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Vault/Resolucao.h"

namespace matriz::ui::acoes {

namespace {

enum Comando {
    kCategorizar = 1,
    kRenomear,
    kRenomearEmLote,
    kAlternarPublicacao,
    kAlternarZip,
    kAlternarPrint,
    kAlternarWatermark,
    kRemoverDoBackup,
    kRemoverDaLista,
    kMostrarNaOrigem,
    kCopiarCaminho,
    kVerDuplicatas,
    kDefinirCapa,
    kRemoverCapa,
    kLimparMetadados,
    kRecarregarArquivo,
    kSubstituirArquivo,
    kObterExif,
    kRevisarConflitosMerge,
    kEditarQuestionario,
    // Ids das pastas de destino ("Enviar para pasta") começam aqui, pra
    // nunca colidirem com os comandos fixos acima por mais que a lista de
    // pastas cresça.
    kPrimeiraPasta = 1000,
};

// Achata a árvore da BACKUP numa lista plana de (id, caminho legível), pra o
// submenu "Enviar para pasta" mostrar "Turnê / 2003 / Berlim" em vez de só
// "Berlim" — com hierarquia replicada, muitos nós têm o mesmo nome curto.
void achatarPastas(const ProjetoAberto::NoArvore& no, const juce::String& prefixo,
                    std::vector<std::pair<std::string, juce::String>>& out) {
    for (auto& filho : no.filhos) {
        if (filho.id.empty()) continue; // nó sintético "ainda sem pasta"
        juce::String caminho = prefixo.isEmpty() ? filho.nome : prefixo + " / " + filho.nome;
        out.push_back({filho.id, caminho});
        achatarPastas(filho, caminho, out);
    }
}

std::vector<std::pair<std::string, juce::String>> pastasDoBackup(ProjetoAberto& projeto) {
    std::vector<std::pair<std::string, juce::String>> out;
    achatarPastas(projeto.arvoreAcervo(), {}, out);
    return out;
}

// categoriaPorExtensao() espera a extensão SEM ponto: o ItemResumo da grade já guarda
// assim, mas obterItemResumo() devolve com ponto (".jpg") — que nunca casava.
bool extensaoEhFoto(const std::string& extensao) {
    return matriz::ingest::categoriaPorExtensao(juce::String(extensao).trimCharactersAtStart(".")) ==
           matriz::ingest::CategoriaMidia::Imagem;
}

// P (Send to Print) e W (Watermark) só fazem sentido pra fotos. Com `emMemoria`, usa o
// item que a grade já tem; sem ele (ou fora da grade), cai no obterItemResumo do banco.
bool ehFoto(ProjetoAberto& projeto, const std::string& id, const ResolvedorItemEmMemoria& emMemoria = {}) {
    if (emMemoria)
        if (const ItemResumo* r = emMemoria(id)) return extensaoEhFoto(r->extensaoArquivo);
    auto resumo = projeto.obterItemResumo(id);
    if (!resumo) return false;
    return extensaoEhFoto(resumo->extensaoArquivo);
}

void confirmar(const juce::String& titulo, const juce::String& mensagem, const juce::String& rotuloConfirmar,
                std::function<void()> aoConfirmar) {
    auto janela = std::make_shared<juce::AlertWindow>(titulo, mensagem, juce::MessageBoxIconType::WarningIcon);
    janela->addButton(rotuloConfirmar, 1);
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela, aoConfirmar](int resultado) {
        retirarPeerDaTela(*janela); // §3
        if (resultado == 1) aoConfirmar();
    }));
}

void avisarErroArquivo(const juce::String& mensagem) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                      .withIconType(juce::MessageBoxIconType::WarningIcon)
                                      .withTitle(matriz::i18n::t("acoes.erro_arquivo_titulo"))
                                      .withMessage(mensagem)
                                      .withButton(matriz::i18n::t("comum.ok")),
                                  static_cast<juce::ModalComponentManager::Callback*>(nullptr));
}

// Nome final que o arquivo terá no backup, pro título dado.
//
// O operador digita um TÍTULO, mas o que vai pro disco é o resultado da
// máscara de nomenclatura ({codigo}-{seq:03}-{titulo} por padrão). Sem ver
// isso resolvido, ele digita no escuro e só descobre o resultado depois de
// gravar o backup inteiro.
juce::String previaDoNomeNoBackup(ProjetoAberto& projeto, const std::string& itemId, const juce::String& titulo) {
    matriz::consolidacao::ContextoMascara ctx;
    ctx.titulo = titulo.toStdString();
    ctx.seq = 1;

    std::string tituloStd, tipoMidia, codigoAcervo;
    if (projeto.obterItemInfo(itemId, tituloStd, tipoMidia, codigoAcervo)) {
        ctx.codigoAcervo = codigoAcervo;
        ctx.tipoMidia = tipoMidia;
    }

    auto arquivo = projeto.arquivoPrincipal(itemId);
    juce::String extensao;
    if (arquivo) {
        juce::File f(arquivo->caminhoAbsoluto);
        ctx.nomeOriginalSemExtensao = f.getFileNameWithoutExtension().toStdString();
        extensao = f.getFileExtension();
    }

    // Máscara padrão: a da pasta de destino só é conhecida na hora de gravar
    // (o item pode estar em mais de uma pasta, cada uma com a sua). A prévia
    // mostra a padrão e diz isso no rótulo — melhor que não mostrar nada.
    return juce::String(matriz::consolidacao::resolverMascara("{codigo}-{seq:03}-{titulo}", ctx)) + extensao;
}

// Renomear com prévia do nome final no backup, atualizada a cada tecla.
class DialogoRenomear : public juce::Component, private juce::TextEditor::Listener {
public:
    DialogoRenomear(ProjetoAberto& projeto, std::string itemIdParaPrevia)
        : projeto_(projeto), itemId_(std::move(itemIdParaPrevia)) {
        editor_.setText({}, false);
        editor_.addListener(this);
        addAndMakeVisible(editor_);

        previa_.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        previa_.setColour(juce::Label::textColourId, tema().textoTerciario);
        addAndMakeVisible(previa_);

        setSize(420, 56);
        atualizarPrevia();
    }

    juce::String texto() const { return editor_.getText().trim(); }
    juce::TextEditor& editor() { return editor_; }

    void resized() override {
        auto area = getLocalBounds();
        editor_.setBounds(area.removeFromTop(26));
        previa_.setBounds(area);
    }

private:
    void textEditorTextChanged(juce::TextEditor&) override { atualizarPrevia(); }

    void atualizarPrevia() {
        juce::String t = editor_.getText().trim();
        if (t.isEmpty() || itemId_.empty()) {
            previa_.setText({}, juce::dontSendNotification);
            return;
        }
        previa_.setText(matriz::i18n::t("acoes.renomear_previa")
                             .replace("{nome}", previaDoNomeNoBackup(projeto_, itemId_, t)),
                         juce::dontSendNotification);
    }

    ProjetoAberto& projeto_;
    std::string itemId_;
    juce::TextEditor editor_;
    juce::Label previa_;
};

void pedirNovoNome(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, std::function<void()> aoConcluir) {
    auto janela = std::make_shared<juce::AlertWindow>(
        matriz::i18n::t("acoes.renomear_titulo"),
        itemIds.size() == 1
            ? matriz::i18n::t("acoes.renomear_mensagem")
            : matriz::i18n::t("acoes.renomear_mensagem_lote").replace("{n}", juce::String(static_cast<int>(itemIds.size()))),
        juce::MessageBoxIconType::NoIcon);

    // Em lote, a prévia usa o primeiro item — os demais só diferem no
    // {codigo}/{seq}, que a máscara resolve por item na hora de gravar.
    auto campo = std::make_shared<DialogoRenomear>(projeto, itemIds.front());
    if (itemIds.size() == 1) {  // um item só: o nome atual já vem preenchido e selecionado
        std::string tituloAtual, tipo, cod;
        if (projeto.obterItemInfo(itemIds.front(), tituloAtual, tipo, cod))
            campo->editor().setText(juce::String::fromUTF8(tituloAtual.c_str()), true);
    }
    janela->addCustomComponent(campo.get());
    janela->addButton(matriz::i18n::t("comum.ok"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    ProjetoAberto* p = &projeto;
    auto ids = itemIds;
    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela, campo, p, ids, aoConcluir](int resultado) {
        retirarPeerDaTela(*janela); // §3
        if (resultado != 1) return;
        juce::String novo = campo->texto();
        if (novo.isEmpty()) return;
        p->renomearItens(ids, novo.toStdString());
        if (aoConcluir) aoConcluir();
    }));
    juce::Component::SafePointer<juce::TextEditor> ed(&campo->editor());
    juce::MessageManager::callAsync([ed, campo] {  // depois de a janela aparecer: foco e tudo selecionado
        if (ed == nullptr) return;
        ed->grabKeyboardFocus();
        ed->selectAll();
    });
}

} // namespace

void renomear(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
    if (itemIds.empty()) return;
    pedirNovoNome(projeto, itemIds, ganchos.aoMudarDados);
}

namespace {

// Seletor de capa a partir do PRÓPRIO catálogo: busca por código/título/nome do arquivo, lista e prévia da miniatura.
class SeletorImagemCatalogo : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    SeletorImagemCatalogo(ProjetoAberto& projeto, std::function<void(std::string)> aoEscolher)
        : projeto_(projeto), aoEscolher_(std::move(aoEscolher)) {
        const auto& tk = tema();
        busca_.setTextToShowWhenEmpty(matriz::i18n::t("acoes.capa_buscar_dica"), tk.textoTerciario);
        busca_.onTextChange = [this] { startTimer(250); };  // debounce: não consulta a cada tecla
        addAndMakeVisible(busca_);
        lista_.setModel(this);
        lista_.setRowHeight(22);
        addAndMakeVisible(lista_);
        lblContagem_.setColour(juce::Label::textColourId, tk.textoTerciario);
        lblContagem_.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        addAndMakeVisible(lblContagem_);
        btnUsar_.setButtonText(matriz::i18n::t("acoes.capa_usar"));
        btnUsar_.setColour(juce::TextButton::buttonColourId, tk.acento);
        btnUsar_.setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
        btnUsar_.setEnabled(false);
        btnUsar_.onClick = [this] { escolher(true); };
        addAndMakeVisible(btnUsar_);
        btnCancelar_.setButtonText(matriz::i18n::t("comum.cancelar"));
        btnCancelar_.onClick = [this] { escolher(false); };
        addAndMakeVisible(btnCancelar_);
        setSize(640, 420);
        consultar();
    }

    static void exibir(ProjetoAberto& projeto, std::function<void(std::string)> aoEscolher) {
        juce::DialogWindow::LaunchOptions opt;
        opt.dialogTitle = matriz::i18n::t("acoes.capa_do_catalogo");
        opt.content.setOwned(new SeletorImagemCatalogo(projeto, std::move(aoEscolher)));
        opt.dialogBackgroundColour = tema().fundo;
        opt.escapeKeyTriggersCloseButton = true;
        opt.useNativeTitleBar = true;
        opt.resizable = false;
        moldura::lancar(opt);
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.fundo);
        g.setColour(tk.painelAlt.withAlpha(0.5f));
        g.fillRoundedRectangle(areaPrevia_.toFloat(), 6.0f);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(areaPrevia_.toFloat().reduced(0.5f), 6.0f, 1.0f);
        if (previa_.isValid())
            g.drawImageWithin(previa_, areaPrevia_.getX() + 6, areaPrevia_.getY() + 6, areaPrevia_.getWidth() - 12,
                              areaPrevia_.getHeight() - 12, juce::RectanglePlacement::centred, false);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        busca_.setBounds(area.removeFromTop(28));
        area.removeFromTop(6);
        auto rodape = area.removeFromBottom(30);
        btnCancelar_.setBounds(rodape.removeFromRight(110));
        rodape.removeFromRight(8);
        btnUsar_.setBounds(rodape.removeFromRight(150));
        lblContagem_.setBounds(rodape);
        area.removeFromBottom(8);
        areaPrevia_ = area.removeFromRight(230);
        area.removeFromRight(10);
        lista_.setBounds(area);
    }

private:
    int getNumRows() override { return static_cast<int>(resultados_.size()); }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selecionada) override {
        if (row < 0 || row >= static_cast<int>(resultados_.size())) return;
        const auto& tk = tema();
        if (selecionada) g.fillAll(tk.acento.withAlpha(0.35f));
        const auto& r = resultados_[static_cast<size_t>(row)];
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
        g.setColour(tk.textoTerciario);
        g.drawText(juce::String::fromUTF8(r.codigo.c_str()), 6, 0, 78, h, juce::Justification::centredLeft, true);
        g.setColour(tk.textoPrimario);
        g.drawText(juce::String::fromUTF8(r.titulo.c_str()), 88, 0, w - 94, h, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int linha) override {
        escolhido_.clear();
        previa_ = {};
        if (linha >= 0 && linha < static_cast<int>(resultados_.size())) {
            escolhido_ = resultados_[static_cast<size_t>(linha)].id;
            if (auto caminho = projeto_.caminhoMiniaturaPrincipal(escolhido_)) {
                juce::File f(*caminho);
                if (f.existsAsFile()) previa_ = juce::ImageFileFormat::loadFrom(f);
            }
        }
        btnUsar_.setEnabled(!escolhido_.empty());
        repaint();
    }

    void listBoxItemDoubleClicked(int linha, const juce::MouseEvent&) override {
        lista_.selectRow(linha);
        if (!escolhido_.empty()) escolher(true);
    }

    void timerCallback() override { stopTimer(); consultar(); }

    void consultar() {
        resultados_ = projeto_.buscarImagensDoCatalogo(busca_.getText().trim().toStdString());
        lista_.updateContent();
        lista_.deselectAllRows();
        selectedRowsChanged(-1);
        lblContagem_.setText(matriz::i18n::t("acoes.capa_contagem").replace("{n}", juce::String(static_cast<int>(resultados_.size()))),
                             juce::dontSendNotification);
    }

    void escolher(bool usar) {
        auto cb = aoEscolher_;
        const std::string id = escolhido_;
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(usar ? 1 : 0);
        if (usar && !id.empty() && cb) juce::MessageManager::callAsync([cb, id] { cb(id); });
    }

    ProjetoAberto& projeto_;
    std::function<void(std::string)> aoEscolher_;
    juce::TextEditor busca_;
    juce::ListBox lista_;
    juce::Label lblContagem_;
    juce::TextButton btnUsar_, btnCancelar_;
    juce::Rectangle<int> areaPrevia_;
    juce::Image previa_;
    std::vector<ProjetoAberto::ImagemCatalogo> resultados_;
    std::string escolhido_;
};

} // namespace

void definirCapa(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
    if (itemIds.empty()) return;
    ProjetoAberto* p = &projeto;
    auto ids = itemIds;

    // Duas origens: uma imagem que já está no catálogo (busca no banco) ou um arquivo no disco/locações externas.
    auto doDisco = [p, ids, ganchos] {
        auto seletor = std::make_shared<juce::FileChooser>(matriz::i18n::t("acoes.definir_capa"), juce::File(),
                                                            "*.jpg;*.jpeg;*.png;*.gif;*.bmp;*.webp");
        seletor->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [seletor, p, ids, ganchos](const juce::FileChooser& fc) {
                                  juce::File imagem = fc.getResult();
                                  if (imagem == juce::File()) return;
                                  p->definirCapa(ids, imagem);
                                  if (ganchos.aoMudarDados) ganchos.aoMudarDados();
                              });
    };
    auto doCatalogo = [p, ids, ganchos] {
        SeletorImagemCatalogo::exibir(*p, [p, ids, ganchos](std::string itemEscolhido) {
            // O master do item escolhido; se está offline, cai na miniatura (melhor que nada).
            juce::File origem;
            if (auto info = p->arquivoPrincipal(itemEscolhido)) origem = juce::File(info->caminhoAbsoluto);
            if (!origem.existsAsFile())
                if (auto mini = p->caminhoMiniaturaPrincipal(itemEscolhido)) origem = juce::File(*mini);
            if (!origem.existsAsFile()) {
                avisarErroArquivo(matriz::i18n::t("acoes.capa_sem_arquivo"));
                return;
            }
            p->definirCapa(ids, origem);
            if (ganchos.aoMudarDados) ganchos.aoMudarDados();
        });
    };

    juce::PopupMenu menu;
    menu.addItem(1, matriz::i18n::t("acoes.capa_do_catalogo"));
    menu.addItem(2, matriz::i18n::t("acoes.capa_do_disco"));
    menu.showMenuAsync(juce::PopupMenu::Options(), [doCatalogo, doDisco](int resultado) {
        if (resultado == 1) doCatalogo();
        else if (resultado == 2) doDisco();
    });
}

void recarregarArquivo(ProjetoAberto& projeto, const std::string& itemId, Ganchos ganchos) {
    if (itemId.empty()) return;
    auto info = projeto.arquivoPrincipal(itemId);
    if (!info) {
        avisarErroArquivo(matriz::i18n::t("acoes.erro_sem_master"));
        return;
    }
    juce::File atual(info->caminhoAbsoluto);
    juce::String erro;
    if (!projeto.recarregarOuSubstituirArquivo(itemId, atual, erro)) {
        avisarErroArquivo(erro);
        return;
    }
    EventBus::obterInstancia().dispararItemAlterado(itemId, "miniatura");  // a grade descarta a miniatura guardada
    if (ganchos.aoMudarDados) ganchos.aoMudarDados();
}

void substituirArquivo(ProjetoAberto& projeto, const std::string& itemId, Ganchos ganchos) {
    if (itemId.empty()) return;
    auto seletor = std::make_shared<juce::FileChooser>(matriz::i18n::t("acoes.substituir_arquivo"), juce::File());
    ProjetoAberto* p = &projeto;
    std::string id = itemId;
    seletor->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [seletor, p, id, ganchos](const juce::FileChooser& fc) {
                              juce::File novo = fc.getResult();
                              if (novo == juce::File()) return;
                              juce::String erro;
                              if (!p->recarregarOuSubstituirArquivo(id, novo, erro)) {
                                  avisarErroArquivo(erro);
                                  return;
                              }
                              EventBus::obterInstancia().dispararItemAlterado(id, "miniatura");
                              if (ganchos.aoMudarDados) ganchos.aoMudarDados();
                          });
}

void mudarTipo(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
    MATRIZ_TRACE("acoes::mudarTipo");
    if (itemIds.empty()) return;
    auto tipos = listarTiposMidiaDisponiveis(projeto);
    if (tipos.empty()) return;

    juce::PopupMenu menu;
    for (int i = 0; i < static_cast<int>(tipos.size()); ++i)
        menu.addItem(i + 1, tipos[static_cast<size_t>(i)].rotulo);

    ProjetoAberto* p = &projeto;
    auto ids = itemIds;
    menu.showMenuAsync(juce::PopupMenu::Options(), [p, ids, ganchos, tipos](int resultado) {
        if (resultado <= 0 || resultado > static_cast<int>(tipos.size())) return;
        const std::string& tipo = tipos[static_cast<size_t>(resultado - 1)].id;
        if (ids.size() > 1) {
            ProgressoGlobal::obterInstancia().iniciarTarefa("batch_type", "Changing Media Type", (int)ids.size(), nullptr, "Updating " + juce::String((int)ids.size()) + " assets...");
            // Uma transação, um Undo, um evento de lote — e o mesmo efeito de classificar
            // item a item (inclui 'novo' -> 'catalogado').
            p->aplicarTipoMidiaEmLote(ids, tipo, /*promoverEstado*/ true);
            ProgressoGlobal::obterInstancia().concluirTarefa("batch_type", juce::String((int)ids.size()) + " assets updated");
        } else {
            p->atualizarTipoMidia(ids.front(), tipo);
        }
        if (ganchos.aoMudarDados) ganchos.aoMudarDados();
    });
}

void removerDoBackup(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
    if (itemIds.empty()) return;
    ProjetoAberto* p = &projeto;
    auto ids = itemIds;
    confirmar(matriz::i18n::t("acoes.remover_do_backup_titulo"),
              matriz::i18n::t("acoes.remover_do_backup_mensagem")
                  .replace("{n}", juce::String(static_cast<int>(itemIds.size()))),
              matriz::i18n::t("acoes.remover_do_backup"), [p, ids, ganchos] {
                  if (ids.size() > 1) {
                      ProgressoGlobal::obterInstancia().iniciarTarefa("batch_remove_backup", "Removing from Backup", (int)ids.size(), nullptr, "Updating " + juce::String((int)ids.size()) + " assets...");
                  }
                  p->removerItensDoBackup(ids);
                  if (ids.size() > 1) {
                      ProgressoGlobal::obterInstancia().concluirTarefa("batch_remove_backup", juce::String((int)ids.size()) + " assets updated");
                  }
                  if (ganchos.aoMudarDados) ganchos.aoMudarDados();
              });
}

void limparMetadados(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
    if (itemIds.empty()) return;
    ProjetoAberto* p = &projeto;
    auto ids = itemIds;
    confirmar(matriz::i18n::t("metadados.confirmar_limpar_titulo"),
              matriz::i18n::t("metadados.confirmar_limpar_msg"),
              matriz::i18n::t("menu.limpar_metadados"), [p, ids, ganchos] {
                  if (ids.size() > 1) {
                      ProgressoGlobal::obterInstancia().iniciarTarefa("batch_clear_metadata", "Clearing Metadata", (int)ids.size(), nullptr, "Clearing metadata for " + juce::String((int)ids.size()) + " assets...");
                  }
                  p->redefinirMetadadosItens(ids);
                  if (ids.size() > 1) {
                      ProgressoGlobal::obterInstancia().concluirTarefa("batch_clear_metadata", juce::String((int)ids.size()) + " assets metadata cleared");
                  }
                  if (ganchos.aoMudarDados) ganchos.aoMudarDados();
              });
}

void enviarParaPasta(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos,
                      juce::Component* ancora) {
    if (itemIds.empty()) return;
    auto pastas = pastasDoBackup(projeto);

    juce::PopupMenu menu;
    if (projeto.mapaAtivoEhOriginal()) {
        // ORIGINAL é somente leitura: mostra o motivo em vez de esconder a ação.
        menu.addItem(-1, matriz::i18n::t("acoes.enviar_original_bloqueado"), false);
        auto op = juce::PopupMenu::Options();
        if (ancora) op = op.withTargetComponent(ancora);
        menu.showMenuAsync(op, [](int) {});
        return;
    }
    if (pastas.empty()) {
        menu.addItem(-1, matriz::i18n::t("acoes.sem_pastas"), false);
        auto op = juce::PopupMenu::Options();
        if (ancora) op = op.withTargetComponent(ancora);
        menu.showMenuAsync(op, [](int) {});
        return;
    }

    ProjetoAberto* p = &projeto;
    auto ids = itemIds;
    PastaPickerWindow::abrir(matriz::i18n::t("acoes.enviar_para_pasta"), projeto.arvoreAcervo(),
                             [p, ids, ganchos](const std::string& pastaId) {
                                 p->adicionarItensAPasta(ids, pastaId);
                                 if (ganchos.aoMudarDados) ganchos.aoMudarDados();
                             });
}

juce::PopupMenu construirMenu(ProjetoAberto& projeto, const std::vector<std::string>& itemIds,
                              ResolvedorItemEmMemoria emMemoria, bool comEditarQuestionario) {
    juce::PopupMenu menu;
    bool umSo = itemIds.size() == 1;

    // Container de importação: o questionário inicial pode ser editado depois (as respostas voltam preenchidas).
    if (comEditarQuestionario && umSo) {
        if (const auto nest = projeto.nestDoItem(itemIds.front()); nest && ehOrigemComFicha(juce::String(nest->origem))) {
            menu.addItem(kEditarQuestionario, matriz::i18n::t("imp.editar_questionario"));
            menu.addSeparator();
        }
    }

    menu.addItem(kRenomear, matriz::i18n::t("acoes.renomear"));
    menu.addItem(kLimparMetadados, matriz::i18n::t("menu.limpar_metadados") + " (C)");
    // Item D.9/10 — só fazem sentido pra um arquivo por vez (mesma regra de
    // "Mostrar na origem"/"Ver duplicatas" logo abaixo).
    menu.addItem(kObterExif, matriz::i18n::t("acoes.obter_exif"));
    menu.addItem(kRecarregarArquivo, matriz::i18n::t("acoes.recarregar_arquivo"), umSo);
    menu.addItem(kSubstituirArquivo, matriz::i18n::t("acoes.substituir_arquivo"), umSo);

    // Estados da seleção numa passada só (antes: uma volta por rótulo, e um
    // obterItemResumo — banco + stat do arquivo — por item pra "tem foto").
    bool todosHtml = true, todosZip = true, todosPrint = true, todosWatermark = true;
    // Só fotos aceitam Print/Watermark — item desabilitado se a seleção não
    // tiver nenhuma (evita um clique que silenciosamente não faz nada).
    bool algumaFoto = false;
    for (const auto& id : itemIds) {
        if (todosHtml && !projeto.contemMarcacao(ProjetoAberto::TipoMarcacao::Html, id)) todosHtml = false;
        if (todosZip && !projeto.contemMarcacao(ProjetoAberto::TipoMarcacao::Zip, id)) todosZip = false;
        if (todosPrint && !projeto.contemMarcacao(ProjetoAberto::TipoMarcacao::Print, id)) todosPrint = false;
        if (todosWatermark && !projeto.contemMarcacao(ProjetoAberto::TipoMarcacao::Watermark, id)) todosWatermark = false;
        if (!algumaFoto && ehFoto(projeto, id, emMemoria)) algumaFoto = true;
        if (!todosHtml && !todosZip && !todosPrint && !todosWatermark && algumaFoto) break;
    }

    juce::String labelHtml = (todosHtml ? matriz::i18n::t("acoes.remover_html") : matriz::i18n::t("acoes.adicionar_html")) + " (H)";
    menu.addItem(kAlternarPublicacao, labelHtml);

    juce::String labelZip = (todosZip ? matriz::i18n::t("acoes.remover_zip") : matriz::i18n::t("acoes.adicionar_zip")) + " (K)";
    menu.addItem(kAlternarZip, labelZip);

    juce::String labelPrint = (todosPrint ? matriz::i18n::t("acoes.remover_print") : matriz::i18n::t("acoes.adicionar_print")) + " (P)";
    menu.addItem(kAlternarPrint, labelPrint, algumaFoto);

    juce::String labelWatermark = (todosWatermark ? matriz::i18n::t("acoes.remover_watermark") : matriz::i18n::t("acoes.adicionar_watermark")) + " (W)";
    menu.addItem(kAlternarWatermark, labelWatermark, algumaFoto);

    menu.addSeparator();
    menu.addItem(kDefinirCapa, matriz::i18n::t("acoes.definir_capa"));
    // Só oferece remover se ALGUM dos selecionados tem capa — item sem capa
    // com "Remover capa" habilitado é uma ação que não faz nada.
    const bool algumComCapa = projeto.algumTemCapa(itemIds);
    menu.addItem(kRemoverCapa, matriz::i18n::t("acoes.remover_capa"), algumComCapa);

    menu.addSeparator();
    menu.addItem(kRemoverDoBackup, matriz::i18n::t("acoes.remover_do_backup"));
    menu.addItem(kRemoverDaLista, matriz::i18n::t("acoes.remover_da_lista"));

    menu.addSeparator();
    // Só fazem sentido pra um arquivo — "mostrar na origem" de 300 arquivos
    // abriria 300 janelas do Finder.
    menu.addItem(kMostrarNaOrigem, matriz::i18n::t("acoes.mostrar_na_origem"), umSo);
    menu.addItem(kCopiarCaminho, matriz::i18n::t("acoes.copiar_caminho"), umSo);
    menu.addItem(kVerDuplicatas, matriz::i18n::t("acoes.ver_duplicatas"), umSo);
    // Fase 4: valores que perderam ao juntar duplicatas.
    if (std::any_of(itemIds.begin(), itemIds.end(), [&](const auto& id) { return projeto.temConflitoMergePendente(id); }))
        menu.addItem(kRevisarConflitosMerge, matriz::i18n::t(umSo ? "acoes.revisar_conflitos" : "acoes.revisar_conflitos_lote"));
    return menu;
}

void executar(int resultado, ProjetoAberto& projeto, std::vector<std::string> itemIds, Ganchos ganchos) {
    if (resultado <= 0 || itemIds.empty()) return;

    int quantidade = static_cast<int>(itemIds.size());

    switch (resultado) {
        case kCategorizar:
            mudarTipo(projeto, itemIds, ganchos);
            break;

        case kRenomear:
            renomear(projeto, itemIds, ganchos);
            break;

        case kEditarQuestionario:
            if (ganchos.aoEditarQuestionario)
                if (const auto nest = projeto.nestDoItem(itemIds.front()); nest && ehOrigemComFicha(juce::String(nest->origem)))
                    ganchos.aoEditarQuestionario(nest->nestId, nest->origem);
            break;

        case kRenomearEmLote:
            renomearEmLote(projeto, itemIds, ganchos);
            break;

        case kLimparMetadados:
            limparMetadados(projeto, itemIds, ganchos);
            break;

        case kRecarregarArquivo:
            recarregarArquivo(projeto, itemIds.front(), ganchos);
            break;

        case kSubstituirArquivo:
            substituirArquivo(projeto, itemIds.front(), ganchos);
            break;

        case kAlternarPublicacao:
            projeto.alternarMarcacao(ProjetoAberto::TipoMarcacao::Html, itemIds);
            if (ganchos.aoMudarDados) ganchos.aoMudarDados();
            break;

        case kAlternarZip:
            projeto.alternarMarcacao(ProjetoAberto::TipoMarcacao::Zip, itemIds);
            if (ganchos.aoMudarDados) ganchos.aoMudarDados();
            break;

        case kAlternarPrint: {
            std::vector<std::string> fotos;
            for (const auto& id : itemIds) if (ehFoto(projeto, id, ganchos.itemEmMemoria)) fotos.push_back(id);
            if (!fotos.empty()) projeto.alternarMarcacao(ProjetoAberto::TipoMarcacao::Print, fotos);
            if (ganchos.aoMudarDados) ganchos.aoMudarDados();
            break;
        }

        case kAlternarWatermark: {
            std::vector<std::string> fotos;
            for (const auto& id : itemIds) if (ehFoto(projeto, id, ganchos.itemEmMemoria)) fotos.push_back(id);
            if (!fotos.empty()) projeto.alternarMarcacao(ProjetoAberto::TipoMarcacao::Watermark, fotos);
            if (ganchos.aoMudarDados) ganchos.aoMudarDados();
            break;
        }

        case kDefinirCapa:
            definirCapa(projeto, itemIds, ganchos);
            break;

        case kRemoverCapa:
            projeto.removerCapa(itemIds);
            if (ganchos.aoMudarDados) ganchos.aoMudarDados();
            break;

        case kRemoverDoBackup:
            removerDoBackup(projeto, itemIds, ganchos);
            break;

        case kRemoverDaLista: {
            ProjetoAberto* p = &projeto;
            confirmar(matriz::i18n::t("acoes.remover_da_lista_titulo"),
                      matriz::i18n::t("acoes.remover_da_lista_mensagem").replace("{n}", juce::String(quantidade)),
                      matriz::i18n::t("acoes.remover_da_lista"), [p, itemIds] {
                          // Em segundo plano: a janela segue viva, o card avança e o Cancelar responde.
                          const int n = static_cast<int>(itemIds.size());
                          auto cancelar = std::make_shared<std::atomic<bool>>(false);
                          if (n > 1)
                              ProgressoGlobal::obterInstancia().iniciarTarefa(
                                  "batch_remove_list", "Removing from List", n, [cancelar] { cancelar->store(true); },
                                  "Removing " + juce::String(n) + " assets...");
                          p->removerItensDoProjetoEmSegundoPlano(
                              itemIds, cancelar,
                              [n](int feitos, int total) {
                                  if (n > 1)
                                      ProgressoGlobal::obterInstancia().atualizarProgresso(
                                          "batch_remove_list", feitos,
                                          "Removing " + juce::String(feitos) + " of " + juce::String(total) + " assets...");
                              },
                              [n](int removidos, bool cancelado, const std::string& erro) {
                                  if (n > 1) {
                                      juce::String msg = !erro.empty() ? "Removal failed after " + juce::String(removidos) + " assets"
                                                         : cancelado   ? "Cancelled: " + juce::String(removidos) + " assets removed"
                                                                       : juce::String(removidos) + " assets removed";
                                      ProgressoGlobal::obterInstancia().concluirTarefa("batch_remove_list", msg);
                                  }
                                  // A grade/árvore/contagens recarregam pelo EventBus (os ganchos de UI podem já ter
                                  // sido destruídos quando isto termina).
                                  EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");
                              });
                      });
            break;
        }

        case kMostrarNaOrigem: {
            auto caminho = projeto.caminhoDeOrigem(itemIds.front());
            if (!caminho || caminho->isEmpty()) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::InfoIcon)
                        .withTitle(matriz::i18n::t("acoes.origem_ausente_titulo"))
                        .withMessage(juce::String::fromUTF8("O arquivo de origem não possui caminho registrado."))
                        .withButton(matriz::i18n::t("comum.ok")),
                    static_cast<juce::ModalComponentManager::Callback*>(nullptr));
                break;
            }
            juce::File arquivo(*caminho);
            // Se o volume de origem não está montado, revealToUser abriria
            // uma janela vazia sem explicar nada.
            if (arquivo.existsAsFile() || arquivo.isDirectory()) arquivo.revealToUser();
            else
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::InfoIcon)
                        .withTitle(matriz::i18n::t("acoes.origem_ausente_titulo"))
                        .withMessage(matriz::i18n::t("acoes.origem_ausente_mensagem").replace("{caminho}", *caminho))
                        .withButton(matriz::i18n::t("comum.ok")),
                    static_cast<juce::ModalComponentManager::Callback*>(nullptr));
            break;
        }

        case kCopiarCaminho: {
            auto caminho = projeto.caminhoDeOrigem(itemIds.front());
            if (caminho && !caminho->isEmpty()) juce::SystemClipboard::copyTextToClipboard(*caminho);
            break;
        }

        case kObterExif:
            obterExif(projeto, itemIds);
            break;

        case kVerDuplicatas: {
            auto duplicatas = projeto.itensComMesmoConteudo(itemIds.front());
            if (duplicatas.empty()) {
                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                                  .withIconType(juce::MessageBoxIconType::InfoIcon)
                                                  .withTitle(matriz::i18n::t("acoes.ver_duplicatas"))
                                                  .withMessage(matriz::i18n::t("acoes.sem_duplicatas"))
                                                  .withButton(matriz::i18n::t("comum.ok")),
                                              static_cast<juce::ModalComponentManager::Callback*>(nullptr));
                break;
            }
            if (ganchos.aoFiltrarItens) ganchos.aoFiltrarItens(std::move(duplicatas));
            break;
        }

        case kRevisarConflitosMerge:
            // Janela no desenho da aba Duplicates; lê os conflitos em background.
            RevisaoConflitosMergeComponent::abrir(projeto, std::set<std::string>(itemIds.begin(), itemIds.end()));
            break;

        default: break;
    }
}

void renomearEmLote(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
    MATRIZ_TRACE("acoes::renomearEmLote");
    if (itemIds.empty()) return;

    auto janela = std::make_shared<juce::AlertWindow>(
        matriz::i18n::t("renomear_lote.titulo"),
        juce::String(), juce::MessageBoxIconType::NoIcon);

    janela->addComboBox("modo", {
        matriz::i18n::t("renomear_lote.adicionar_depois"),
        matriz::i18n::t("renomear_lote.adicionar_antes"),
        matriz::i18n::t("renomear_lote.substituir"),
        matriz::i18n::t("renomear_lote.inserir_entre_prefixo_e_nome")
    });
    janela->addTextEditor("texto", "", matriz::i18n::t("renomear_lote.campo_texto"));
    janela->addTextEditor("procurar", "", matriz::i18n::t("renomear_lote.campo_procurar"));
    janela->addTextEditor("substituir_por", "", matriz::i18n::t("renomear_lote.campo_substituir_por"));

    auto* combo = janela->getComboBoxComponent("modo");
    auto* campoTexto = janela->getTextEditor("texto");
    auto* campoProcurar = janela->getTextEditor("procurar");
    auto* campoSubstituirPor = janela->getTextEditor("substituir_por");

    campoTexto->setVisible(true);
    campoProcurar->setVisible(false);
    campoSubstituirPor->setVisible(false);

    combo->onChange = [campoTexto, campoProcurar, campoSubstituirPor, combo] {
        int modo = combo->getSelectedItemIndex();
        campoTexto->setVisible(modo != 2);
        campoProcurar->setVisible(modo == 2);
        campoSubstituirPor->setVisible(modo == 2);
    };

    janela->addButton(matriz::i18n::t("renomear_lote.renomear"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    ProjetoAberto* p = &projeto;
    auto ids = itemIds;
    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela, p, ids, ganchos](int resultado) {
        retirarPeerDaTela(*janela);
        if (resultado != 1) return;

        ProgressoGlobal::obterInstancia().iniciarTarefa("batch_rename", "Renaming Assets", (int)ids.size(), nullptr, "Renaming " + juce::String((int)ids.size()) + " assets...");

        int modo = janela->getComboBoxComponent("modo")->getSelectedItemIndex();

        // Um título por item, calculado aqui; a gravação é UMA chamada (uma transação, um
        // Undo, um evento de lote) em vez de renomearItens({id}) por item.
        std::vector<std::pair<std::string, std::string>> itemETitulo;
        itemETitulo.reserve(ids.size());
        for (auto& itemId : ids) {
            std::string titulo, tipoMidia, codigo;
            if (!p->obterItemInfo(itemId, titulo, tipoMidia, codigo)) continue;
            juce::String nome(titulo);

            if (modo == 0) {
                juce::String texto = janela->getTextEditorContents("texto");
                nome = nome + texto;
            } else if (modo == 1) {
                juce::String texto = janela->getTextEditorContents("texto");
                nome = texto + nome;
            } else if (modo == 2) {
                juce::String procurar = janela->getTextEditorContents("procurar");
                juce::String substituirPor = janela->getTextEditorContents("substituir_por");
                if (procurar.isNotEmpty())
                    nome = nome.replace(procurar, substituirPor);
            } else if (modo == 3) {
                juce::String texto = janela->getTextEditorContents("texto").trim();
                if (texto.isNotEmpty()) {
                    juce::String cod(codigo);
                    if (cod.isNotEmpty() && nome.startsWithIgnoreCase(cod)) {
                        juce::String resto = nome.substring(cod.length());
                        while (resto.startsWith("-") || resto.startsWith("_") || resto.startsWith(" ")) {
                            resto = resto.substring(1);
                        }
                        if (resto.isNotEmpty())
                            nome = cod + "-" + texto + "-" + resto;
                        else
                            nome = cod + "-" + texto;
                    } else if (cod.isNotEmpty()) {
                        nome = cod + "-" + texto + "-" + nome;
                    } else {
                        nome = texto + "-" + nome;
                    }
                }
            }
            itemETitulo.emplace_back(itemId, nome.toStdString());
        }
        if (!itemETitulo.empty()) p->renomearItensComTitulos(itemETitulo);
        const int processados = static_cast<int>(itemETitulo.size());
        ProgressoGlobal::obterInstancia().concluirTarefa("batch_rename", juce::String(processados) + " assets renamed");
        if (ganchos.aoMudarDados) ganchos.aoMudarDados();
    }));
}

void obterExif(ProjetoAberto& projeto, const std::vector<std::string>& itemIds,
               std::function<void(int gravados)> aoConcluir) {
    if (itemIds.empty()) return;
    // Message thread: só banco. A thread de fundo recebe cópias e nunca toca
    // no ProjetoAberto (que pode fechar no meio).
    auto alvos = projeto.alvosArquivoPrincipal(itemIds);
    auto resolvedor = projeto.criarResolvedorEmLote();
    std::weak_ptr<bool> vivo = projeto.tokenVida();
    ProjetoAberto* p = &projeto;
    const int total = static_cast<int>(itemIds.size());
    const int semArquivoNoBanco = total - static_cast<int>(alvos.size());
    if (!resolvedor) return;

    ProgressoGlobal::obterInstancia().iniciarTarefa("get_exif", matriz::i18n::t("exif.progresso"), total);
    juce::Thread::launch([alvos, resolvedor, vivo, p, total, semArquivoNoBanco, aoConcluir]() {
        auto textos = std::make_shared<std::map<std::string, std::string>>();
        int semArquivo = semArquivoNoBanco, semExif = 0, feitos = 0;
        for (const auto& a : alvos) {
            auto f = resolvedor->resolver(a.arquivoId, a.localizacaoVault, a.caminhoRelativo, a.caminhoAbsolutoOrigem);
            if (!f || !f->existsAsFile()) ++semArquivo;
            else if (auto t = matriz::ingest::lerExifCompletoParaNotas(*f)) (*textos)[a.itemId] = *t;
            else ++semExif;
            if (++feitos % 25 == 0)
                juce::MessageManager::callAsync([feitos] {
                    ProgressoGlobal::obterInstancia().atualizarProgresso("get_exif", feitos);
                });
        }
        juce::MessageManager::callAsync([textos, vivo, p, total, semArquivo, semExif, aoConcluir] {
            ProgressoGlobal::obterInstancia().concluirTarefa("get_exif");
            if (vivo.expired()) return;  // projeto fechado enquanto lia
            const int gravados = p->gravarOutraMetadataEmLote(*textos);
            EventBus::obterInstancia().dispararItemAlterado("", "metadado");
            if (aoConcluir) aoConcluir(gravados);

            juce::String msg = matriz::i18n::t("exif.resultado_ok").replace("{n}", juce::String(gravados))
                                   .replace("{total}", juce::String(total));
            if (semExif > 0) msg << "\n" << matriz::i18n::t("exif.resultado_sem_exif").replace("{n}", juce::String(semExif));
            if (semArquivo > 0) msg << "\n" << matriz::i18n::t("exif.resultado_offline").replace("{n}", juce::String(semArquivo));
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::InfoIcon)
                                             .withTitle(matriz::i18n::t("acoes.obter_exif"))
                                             .withMessage(msg)
                                             .withButton(matriz::i18n::t("comum.ok")),
                                         juce::ModalCallbackFunction::create([](int) {}));
        });
    });
}

} // namespace matriz::ui::acoes
