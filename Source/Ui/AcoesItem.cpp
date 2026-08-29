#include "AcoesItem.h"

#include "../Consolidacao/Mascara.h"
#include "../I18n/Strings.h"
#include "SelecionarTipoMidiaDialogo.h"
#include "Tokens.h"
#include "ModalMitigacao.h"

namespace matriz::ui::acoes {

namespace {

enum Comando {
    kCategorizar = 1,
    kRenomear,
    kRenomearEmLote,
    kRemoverDoBackup,
    kRemoverDaLista,
    kMostrarNaOrigem,
    kCopiarCaminho,
    kVerDuplicatas,
    kDefinirCapa,
    kRemoverCapa,
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
}

} // namespace

void renomear(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
    if (itemIds.empty()) return;
    pedirNovoNome(projeto, itemIds, ganchos.aoMudarDados);
}

void definirCapa(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
    if (itemIds.empty()) return;
    auto seletor = std::make_shared<juce::FileChooser>(matriz::i18n::t("acoes.definir_capa"), juce::File(),
                                                        "*.jpg;*.jpeg;*.png;*.gif;*.bmp;*.webp");
    ProjetoAberto* p = &projeto;
    auto ids = itemIds;
    seletor->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [seletor, p, ids, ganchos](const juce::FileChooser& fc) {
                              juce::File imagem = fc.getResult();
                              if (imagem == juce::File()) return;
                              p->definirCapa(ids, imagem);
                              if (ganchos.aoMudarDados) ganchos.aoMudarDados();
                          });
}

void mudarTipo(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
    if (itemIds.empty()) return;
    auto tipos = listarTiposMidiaDisponiveis(projeto);
    if (tipos.empty()) return;

    juce::PopupMenu menu;
    for (int i = 0; i < static_cast<int>(tipos.size()); ++i)
        menu.addItem(i + 1, tipos[static_cast<size_t>(i)].rotulo);

    ProjetoAberto* p = &projeto;
    auto ids = itemIds;
    menu.showMenuAsync(juce::PopupMenu::Options(), [p, ids, ganchos, tipos](int resultado) {
        if (resultado <= 0) return;
        const std::string& tipo = tipos[static_cast<size_t>(resultado - 1)].id;
        for (auto& itemId : ids)
            p->atualizarTipoMidia(itemId, tipo);
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
                  p->removerItensDoBackup(ids);
                  if (ganchos.aoMudarDados) ganchos.aoMudarDados();
              });
}

void enviarParaPasta(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos,
                      juce::Component* ancora) {
    if (itemIds.empty()) return;
    auto pastas = pastasDoBackup(projeto);

    juce::PopupMenu menu;
    int id = kPrimeiraPasta;
    for (auto& [pastaId, caminho] : pastas) menu.addItem(id++, caminho);
    if (pastas.empty()) menu.addItem(-1, matriz::i18n::t("acoes.sem_pastas"), false);

    auto opcoes = juce::PopupMenu::Options();
    if (ancora) opcoes = opcoes.withTargetComponent(ancora);

    ProjetoAberto* p = &projeto;
    auto ids = itemIds;
    menu.showMenuAsync(opcoes, [p, ids, ganchos](int resultado) {
        // Reaproveita o mesmo despacho do menu de contexto: os ids de pasta
        // são idênticos, então não existe uma segunda regra pra manter viva.
        executar(resultado, *p, ids, ganchos);
    });
}

juce::PopupMenu construirMenu(ProjetoAberto& projeto, const std::vector<std::string>& itemIds) {
    juce::PopupMenu menu;
    bool umSo = itemIds.size() == 1;

    menu.addItem(kCategorizar, matriz::i18n::t("acoes.categorizar"));
    menu.addItem(kRenomear, matriz::i18n::t("acoes.renomear"));
    if (!umSo) menu.addItem(kRenomearEmLote, matriz::i18n::t("renomear_lote.titulo"));

    juce::PopupMenu submenuPastas;
    auto pastas = pastasDoBackup(projeto);
    int id = kPrimeiraPasta;
    for (auto& [pastaId, caminho] : pastas) submenuPastas.addItem(id++, caminho);
    // Sem nenhuma pasta criada ainda, o submenu ficaria vazio e sem
    // explicação — uma linha desabilitada diz o que fazer antes.
    if (pastas.empty()) submenuPastas.addItem(-1, matriz::i18n::t("acoes.sem_pastas"), false);
    menu.addSubMenu(matriz::i18n::t("acoes.enviar_para_pasta"), submenuPastas);

    menu.addSeparator();
    menu.addItem(kDefinirCapa, matriz::i18n::t("acoes.definir_capa"));
    // Só oferece remover se ALGUM dos selecionados tem capa — item sem capa
    // com "Remover capa" habilitado é uma ação que não faz nada.
    bool algumComCapa = false;
    for (auto& id : itemIds)
        if (projeto.temCapa(id)) { algumComCapa = true; break; }
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
    return menu;
}

void executar(int resultado, ProjetoAberto& projeto, std::vector<std::string> itemIds, Ganchos ganchos) {
    if (resultado <= 0 || itemIds.empty()) return;

    if (resultado >= kPrimeiraPasta) {
        auto pastas = pastasDoBackup(projeto);
        size_t indice = static_cast<size_t>(resultado - kPrimeiraPasta);
        if (indice >= pastas.size()) return;
        projeto.adicionarItensAPasta(itemIds, pastas[indice].first);
        if (ganchos.aoMudarDados) ganchos.aoMudarDados();
        return;
    }

    int quantidade = static_cast<int>(itemIds.size());

    switch (resultado) {
        case kCategorizar:
            mudarTipo(projeto, itemIds, ganchos);
            break;

        case kRenomear:
            renomear(projeto, itemIds, ganchos);
            break;

        case kRenomearEmLote:
            renomearEmLote(projeto, itemIds, ganchos);
            break;

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
                      matriz::i18n::t("acoes.remover_da_lista"), [p, itemIds, ganchos] {
                          p->removerItensDoProjeto(itemIds);
                          if (ganchos.aoMudarDados) ganchos.aoMudarDados();
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
                        .withMessage("O arquivo de origem não possui caminho registrado.")
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

        default: break;
    }
}

void renomearEmLote(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos) {
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

        int modo = janela->getComboBoxComponent("modo")->getSelectedItemIndex();

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
            p->renomearItens({itemId}, nome.toStdString());
        }
        if (ganchos.aoMudarDados) ganchos.aoMudarDados();
    }));
}

} // namespace matriz::ui::acoes
