#include "ArvoreComponent.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

// Diálogo mínimo de texto único (nome de pasta nova, renomeação) — mesmo
// padrão assíncrono (AlertWindow + ModalCallbackFunction) já usado em
// MainWindow.cpp pra novo projeto/erro, nunca um loop modal síncrono.
void pedirTexto(const juce::String& titulo, const juce::String& mensagem, const juce::String& valorInicial,
                 std::function<void(std::optional<juce::String>)> aoConcluir) {
    auto janela = std::make_shared<juce::AlertWindow>(titulo, mensagem, juce::MessageBoxIconType::NoIcon);
    janela->addTextEditor("valor", valorInicial);
    janela->addButton(matriz::i18n::t("comum.ok"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela, aoConcluir](int resultado) {
        if (resultado == 1) aoConcluir(janela->getTextEditorContents("valor"));
        else aoConcluir(std::nullopt);
    }));
}

// Marca um arrasto de PASTA (vindo da EXPLORER) — os arrastos vindos da
// grade chegam como array de ids de item. A subárvore em si não cabe numa
// juce::var sem serializar; fica em subarvoreArrastada_, e este marcador só
// diz "o que está sendo arrastado é aquilo".
const char* const kDescricaoPastaExplorer = "matriz:pasta-explorer";

} // namespace

ArvoreComponent::ArvoreComponent(ProjetoAberto& projeto) : projeto_(projeto) { recarregar(); }

void ArvoreComponent::definirAba(Aba aba) {
    if (aba_ == aba) return;
    aba_ = aba;
    // Trocar de aba muda o que "esta pasta" significa — manter um filtro da
    // aba anterior aplicado à grade, sem nenhuma linha destacada pra
    // explicar por quê, seria confuso. Volta pra "todos".
    if (aoSelecionarNo) aoSelecionarNo(std::nullopt);
    recarregar();
}

void ArvoreComponent::recarregar() {
    raiz_ = aba_ == Aba::Origem ? projeto_.arvoreOrigem() : projeto_.arvoreAcervo();
    selecionado_ = nullptr; // ponteiros da rodada anterior apontavam pra dentro da raiz_ agora substituída
    reconstruirLinhas();
    resized();
    repaint();
}

void ArvoreComponent::reconstruirLinhas() {
    linhas_.clear();
    LinhaAchatada pseudo{&raiz_, 0, true};
    linhas_.push_back(pseudo);
    for (auto& filho : raiz_.filhos) achatar(filho, 0);
}

void ArvoreComponent::achatar(const ProjetoAberto::NoArvore& no, int profundidade) {
    linhas_.push_back({&no, profundidade, false});
    for (auto& filho : no.filhos) achatar(filho, profundidade + 1);
}

juce::Rectangle<int> ArvoreComponent::boundsDaLinha(int indice) const {
    return {0, indice * kAlturaLinha, getWidth(), kAlturaLinha};
}

int ArvoreComponent::indiceLinhaNaPosicao(int y) const {
    if (y < 0) return -1;
    int indice = y / kAlturaLinha;
    if (indice < 0 || indice >= static_cast<int>(linhas_.size())) return -1;
    return indice;
}

void ArvoreComponent::resized() { setSize(getWidth(), static_cast<int>(linhas_.size()) * kAlturaLinha); }

void ArvoreComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
        auto& linha = linhas_[static_cast<size_t>(i)];
        auto bounds = boundsDaLinha(i);
        bool destacada = linha.ehPseudoTodos ? (selecionado_ == nullptr) : (linha.no == selecionado_);

        if (i == linhaAlvoDrop_) {
            g.setColour(tk.acento.withAlpha(0.30f));
            g.fillRect(bounds);
        } else if (destacada) {
            g.setColour(tk.painelAlt);
            g.fillRect(bounds);
        }

        auto miolo = bounds;
        miolo.removeFromLeft(tk.espacoMedio + linha.profundidade * kIndentacao);

        juce::String nome = linha.ehPseudoTodos ? matriz::i18n::t("arvore.todos_os_materiais") : linha.no->nome;

        if (!linha.ehPseudoTodos) {
            auto areaContagem = miolo.removeFromRight(36);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawText(juce::String(static_cast<int>(linha.no->itemIds.size())), areaContagem,
                       juce::Justification::centredRight);
        }

        g.setColour(destacada ? tk.textoPrimario : tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, linha.ehPseudoTodos ? juce::Font::bold : juce::Font::plain)));
        g.drawText(nome, miolo.reduced(4, 0), juce::Justification::centredLeft, true);
    }
}

void ArvoreComponent::mouseDown(const juce::MouseEvent& e) {
    int indice = indiceLinhaNaPosicao(e.getPosition().y);
    if (indice < 0) return;
    auto& linha = linhas_[static_cast<size_t>(indice)];

    // Menu de contexto (criar subpasta/renomear/apagar) só faz sentido em
    // cima de uma pasta REAL do Acervo — nem a pseudo-linha "Todos", nem o
    // nó sintético "não organizados" (id vazio, calculado por ausência,
    // ver ProjetoAberto::arvoreAcervo) têm o que renomear ou apagar.
    if (e.mods.isPopupMenu() && aba_ == Aba::Acervo && !linha.ehPseudoTodos && !linha.no->id.empty()) {
        abrirMenuContexto(indice);
        return;
    }

    selecionado_ = linha.ehPseudoTodos ? nullptr : linha.no;
    repaint();
    notificarSelecao();
}

void ArvoreComponent::notificarSelecao() {
    if (!aoSelecionarNo) return;
    if (!selecionado_) {
        aoSelecionarNo(std::nullopt);
        return;
    }
    // itemIds é recursivo (inclui subpastas); itemIdsDiretos é só o que
    // está nesta pasta — a escolha do operador decide qual dos dois filtra
    // a grade (item 2: "com opção de incluir subpastas").
    aoSelecionarNo(incluirSubpastas_ ? selecionado_->itemIds : selecionado_->itemIdsDiretos);
}

void ArvoreComponent::definirIncluirSubpastas(bool incluir) {
    if (incluirSubpastas_ == incluir) return;
    incluirSubpastas_ = incluir;
    notificarSelecao();
}

void ArvoreComponent::mouseDrag(const juce::MouseEvent& e) {
    // Só da EXPLORER: a BACKUP é o destino, e mover pasta DENTRO da BACKUP
    // é outra operação (não especificada ainda).
    if (aba_ != Aba::Origem || subarvoreArrastada_) return;
    if (e.getDistanceFromDragStart() < 8) return; // mesmo limiar do mosaico — clique simples não vira arrasto

    int indice = indiceLinhaNaPosicao(e.getMouseDownPosition().y);
    if (indice < 0) return;
    auto& linha = linhas_[static_cast<size_t>(indice)];
    if (linha.ehPseudoTodos) return;

    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (!container || container->isDragAndDropActive()) return;

    // Cópia da subárvore ANTES de qualquer recarregar() — ver nota no header.
    subarvoreArrastada_ = *linha.no;

    // Não existe alvo de drop válido na própria EXPLORER, então trocar pra
    // BACKUP na hora é o único jeito de o gesto "arrastar da EXPLORER pra
    // BACKUP" acontecer com as duas árvores sendo abas do mesmo painel.
    definirAba(Aba::Acervo);

    container->startDragging(juce::var(kDescricaoPastaExplorer), this);
}

void ArvoreComponent::abrirMenuContexto(int indiceLinha) {
    std::string pastaId = linhas_[static_cast<size_t>(indiceLinha)].no->id;

    juce::PopupMenu menu;
    enum { kNovaSub = 1, kRenomear, kApagar };
    menu.addItem(kNovaSub, matriz::i18n::t("arvore.menu_nova_subpasta"));
    menu.addItem(kRenomear, matriz::i18n::t("arvore.menu_renomear"));
    menu.addItem(kApagar, matriz::i18n::t("arvore.menu_apagar"));

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, pastaId](int resultado) {
        if (resultado == 1) {
            pedirTexto(matriz::i18n::t("arvore.nome_nova_pasta_titulo"), matriz::i18n::t("arvore.nome_nova_pasta_mensagem"),
                       "", [this, pastaId](std::optional<juce::String> nome) {
                           if (!nome || nome->trim().isEmpty()) return;
                           projeto_.criarPastaAcervo(nome->trim().toStdString(), pastaId);
                           recarregar();
                           if (aoMudarOrganizacao) aoMudarOrganizacao();
                       });
        } else if (resultado == 2) {
            pedirTexto(matriz::i18n::t("arvore.renomear_pasta_titulo"), matriz::i18n::t("arvore.renomear_pasta_mensagem"),
                       "", [this, pastaId](std::optional<juce::String> nome) {
                           if (!nome || nome->trim().isEmpty()) return;
                           projeto_.renomearPastaAcervo(pastaId, nome->trim().toStdString());
                           recarregar();
                           if (aoMudarOrganizacao) aoMudarOrganizacao();
                       });
        } else if (resultado == 3) {
            auto janela = std::make_shared<juce::AlertWindow>(matriz::i18n::t("arvore.apagar_confirmar_titulo"),
                                                                matriz::i18n::t("arvore.apagar_confirmar_mensagem"),
                                                                juce::MessageBoxIconType::WarningIcon);
            janela->addButton(matriz::i18n::t("arvore.menu_apagar"), 1);
            janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
            janela->enterModalState(true, juce::ModalCallbackFunction::create([this, janela, pastaId](int resultado) {
                if (resultado != 1) return;
                projeto_.apagarPastaAcervo(pastaId);
                recarregar();
                if (aoMudarOrganizacao) aoMudarOrganizacao();
            }));
        }
    });
}

void ArvoreComponent::criarPastaDeTopoNivel() {
    pedirTexto(matriz::i18n::t("arvore.nome_nova_pasta_titulo"), matriz::i18n::t("arvore.nome_nova_pasta_mensagem"), "",
               [this](std::optional<juce::String> nome) {
                   if (!nome || nome->trim().isEmpty()) return;
                   projeto_.criarPastaAcervo(nome->trim().toStdString(), std::nullopt);
                   recarregar();
                   if (aoMudarOrganizacao) aoMudarOrganizacao();
               });
}

bool ArvoreComponent::isInterestedInDragSource(const SourceDetails& details) {
    if (aba_ != Aba::Acervo) return false;
    // Array de ids = itens vindos da grade. Marcador = uma pasta inteira
    // vinda da EXPLORER, com a subárvore guardada em subarvoreArrastada_.
    return details.description.isArray() || details.description.toString() == kDescricaoPastaExplorer;
}

void ArvoreComponent::itemDragMove(const SourceDetails& details) {
    int indice = indiceLinhaNaPosicao(details.localPosition.y);
    bool alvoValido = indice >= 0 && !linhas_[static_cast<size_t>(indice)].ehPseudoTodos &&
                       !linhas_[static_cast<size_t>(indice)].no->id.empty();
    int novoAlvo = alvoValido ? indice : -1;
    if (novoAlvo != linhaAlvoDrop_) {
        linhaAlvoDrop_ = novoAlvo;
        repaint();
    }
}

void ArvoreComponent::itemDragExit(const SourceDetails&) {
    if (linhaAlvoDrop_ != -1) {
        linhaAlvoDrop_ = -1;
        repaint();
    }
}

void ArvoreComponent::itemDropped(const SourceDetails& details) {
    int alvo = linhaAlvoDrop_;
    linhaAlvoDrop_ = -1;
    repaint();
    if (alvo < 0) return;

    std::string pastaDestinoId = linhas_[static_cast<size_t>(alvo)].no->id;

    if (details.description.toString() == kDescricaoPastaExplorer) {
        soltarPastaDaExplorer(pastaDestinoId);
        return;
    }

    std::vector<std::string> itemIds;
    if (auto* arr = details.description.getArray())
        for (auto& v : *arr) itemIds.push_back(v.toString().toStdString());
    if (itemIds.empty()) return;

    projeto_.adicionarItensAPasta(itemIds, pastaDestinoId);
    recarregar();
    if (aoMudarOrganizacao) aoMudarOrganizacao();
}

void ArvoreComponent::soltarPastaDaExplorer(const std::string& pastaDestinoId) {
    if (!subarvoreArrastada_) return;
    auto subarvore = std::make_shared<ProjetoAberto::NoArvore>(std::move(*subarvoreArrastada_));
    subarvoreArrastada_.reset();

    // Quantos arquivos estão em SUBpastas — é o número que torna a escolha
    // concreta ("tem 812 arquivos em subpastas") em vez de abstrata.
    int emSubpastas = static_cast<int>(subarvore->itemIds.size() - subarvore->itemIdsDiretos.size());

    // Pasta sem nível nenhum abaixo: não há estrutura a preservar, então
    // perguntar seria uma pergunta sem consequência. Replica direto.
    if (emSubpastas <= 0) {
        projeto_.replicarSubarvoreNoAcervo(*subarvore, pastaDestinoId, true);
        recarregar();
        if (aoMudarOrganizacao) aoMudarOrganizacao();
        return;
    }

    auto janela = std::make_shared<juce::AlertWindow>(
        matriz::i18n::t("arvore.soltar_pasta_titulo"),
        matriz::i18n::t("arvore.soltar_pasta_mensagem")
            .replace("{pasta}", subarvore->nome)
            .replace("{n}", juce::String(emSubpastas)),
        juce::MessageBoxIconType::QuestionIcon);
    // Manter a estrutura é o padrão e por isso é o botão de Return.
    janela->addButton(matriz::i18n::t("arvore.manter_estrutura"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("arvore.achatar_estrutura"), 2);
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    janela->enterModalState(
        true, juce::ModalCallbackFunction::create([this, janela, subarvore, pastaDestinoId](int resultado) {
            if (resultado != 1 && resultado != 2) return;
            projeto_.replicarSubarvoreNoAcervo(*subarvore, pastaDestinoId, resultado == 1);
            recarregar();
            if (aoMudarOrganizacao) aoMudarOrganizacao();
        }));
}

} // namespace matriz::ui
