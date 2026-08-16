#include "FiltrosComponent.h"

#include "../Consolidacao/Consolidacao.h"
#include "../Ficha/FichaI18n.h"
#include "../Model/Project.h"
#include "../I18n/Strings.h"
#include "../Diag/Watchdog.h"
#include "Tokens.h"

#include <algorithm>
#include "ModalMitigacao.h"

namespace matriz::ui {

namespace {

// Mesmo diálogo de texto único usado em ArvoreComponent (criar/renomear
// pasta) — assíncrono, nunca um loop modal síncrono.
void pedirTexto(const juce::String& titulo, const juce::String& mensagem, const juce::String& valorInicial,
                 std::function<void(std::optional<juce::String>)> aoConcluir) {
    auto janela = std::make_shared<juce::AlertWindow>(titulo, mensagem, juce::MessageBoxIconType::NoIcon);
    janela->addTextEditor("valor", valorInicial);
    janela->addButton(matriz::i18n::t("comum.ok"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela, aoConcluir](int resultado) {
        // §3: tira o peer da tela ANTES de qualquer coisa. Enquanto o
        // AlertWindow tem peer, o AppKit pode repintá-lo no meio deste
        // callback — e o callback é justamente onde ele começa a ser
        // desmontado. Ler o texto vem depois, com a janela já fora da tela.
        janela->setVisible(false);
        janela->removeFromDesktop();

        if (resultado == 1) aoConcluir(janela->getTextEditorContents("valor"));
        else aoConcluir(std::nullopt);
    }));
}

juce::String rotuloEstado(const std::string& estado) { return matriz::i18n::t("mosaico.estado_" + estado); }

} // namespace

FiltrosComponent::FiltrosComponent(ProjetoAberto& projeto, MosaicoComponent& mosaico)
    : projeto_(projeto), mosaico_(mosaico) {
    // Primeira carga síncrona: o painel precisa nascer com conteúdo, e
    // neste ponto o projeto acabou de abrir (nada concorrendo pelo banco).
    recarregarSincrono();
}

FiltrosComponent::DadosDoBanco FiltrosComponent::coletarDoBanco(ProjetoAberto& projeto) {
    DadosDoBanco d;
    d.porTipoMidia = projeto.contagensPorTipoMidia();
    d.porEstado = projeto.contagensPorEstado();
    d.porExtensao = projeto.contagensPorExtensao();
    d.porOrigem = projeto.contagensPorOrigem();
    d.porContentType = projeto.contagensPorContentType();
    d.porCollectionType = projeto.contagensPorCollectionType();
    d.vaults = projeto.listarVaults();
    d.embutidas = projeto.listarColecoesEmbutidas();
    d.salvas = projeto.listarColecoes();
    return d;
}

void FiltrosComponent::recarregar() {
    if (consultaPendente_) return;  // uma agregação por vez — ver MosaicoComponent::recarregar
    consultaPendente_ = true;
    const int geracao = ++geracaoConsulta_;
    juce::Component::SafePointer<FiltrosComponent> safeThis(this);
    ProjetoAberto* projeto = &projeto_;

    poolConsulta_.addJob([safeThis, projeto, geracao]() {
        DadosDoBanco d;
        try {
            d = coletarDoBanco(*projeto);
        } catch (const std::exception&) {
            return;  // projeto fechado no meio
        }

        juce::MessageManager::callAsync([safeThis, geracao, d = std::move(d)]() mutable {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            if (geracao != self->geracaoConsulta_) return;  // consulta superada
            MATRIZ_TRACE("FiltrosComponent::aplicarConsulta");
            self->consultaPendente_ = false;
            self->dados_ = std::move(d);
            self->reconstruirLinhas();
            self->resized();
            self->repaint();
        });
    });
}

void FiltrosComponent::recarregarSincrono() {
    ++geracaoConsulta_;  // invalida qualquer consulta em voo
    consultaPendente_ = false;
    dados_ = coletarDoBanco(projeto_);
    reconstruirLinhas();
    resized();
    repaint();
}

void FiltrosComponent::reconstruirLinhas() {
    MATRIZ_TRACE("FiltrosComponent::reconstruirLinhas");
    linhas_.clear();

    const auto& contagensTipo = dados_.porTipoMidia;
    if (!contagensTipo.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("mosaico.filtrar_tipo"), "", -1, false});
        for (auto& [chave, contagem] : contagensTipo) {
            juce::String rotulo = chave.empty() ? matriz::i18n::t("grade.nao_classificado") : juce::String(chave);
            if (!chave.empty()) {
                try {
                    auto& def = projeto_.definicaoPara(chave);
                    rotulo = matriz::ficha::rotuloTipo(chave, def.rotulo.empty() ? chave : def.rotulo);
                } catch (const std::exception&) {}
            }
            linhas_.push_back({TipoLinha::ChipTipoMidia, rotulo, juce::String(chave), contagem,
                                mosaico_.filtrosTipoMidiaAtivos().count(juce::String(chave)) > 0});
        }
    }

    const auto& contagensEstado = dados_.porEstado;
    if (!contagensEstado.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("mosaico.filtrar_estado"), "", -1, false});
        for (auto& [chave, contagem] : contagensEstado) {
            linhas_.push_back({TipoLinha::ChipEstado, rotuloEstado(chave), juce::String(chave), contagem,
                                mosaico_.filtrosEstadoAtivos().count(juce::String(chave)) > 0});
        }
    }

    const auto& contagensExtensao = dados_.porExtensao;
    if (!contagensExtensao.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_extensao"), "", -1, false});
        for (auto& [chave, contagem] : contagensExtensao) {
            linhas_.push_back({TipoLinha::ChipExtensao, "." + juce::String(chave), juce::String(chave), contagem,
                                mosaico_.filtrosExtensaoAtivos().count(juce::String(chave)) > 0});
        }
    }

    // Origem digital/analógico (item 4.2 — "a origem entra como critério de
    // busca, de filtro e de organização"). O valor gravado é o texto bruto
    // do YAML ("Digital"/"Analógico"); o rótulo do chip passa pelo i18n do
    // campo pra aparecer traduzido, sem mexer no valor do banco.
    auto contagensOrigem = dados_.porOrigem;
    contagensOrigem.erase(std::string()); // "sem origem" não vira chip — é ausência, não valor
    if (!contagensOrigem.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_origem"), "", -1, false});
        for (auto& [chave, contagem] : contagensOrigem) {
            juce::String rotulo = chave == "Digital" ? matriz::i18n::t("filtros.origem_digital")
                                                     : matriz::i18n::t("filtros.origem_analogico");
            linhas_.push_back({TipoLinha::ChipOrigem, rotulo, juce::String(chave), contagem,
                                mosaico_.filtrosOrigemAtivos().count(juce::String(chave)) > 0});
        }
    }

    const auto& contagensContent = dados_.porContentType;
    if (!contagensContent.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_content"), "", -1, false});
        for (auto& [chave, contagem] : contagensContent) {
            linhas_.push_back({TipoLinha::ChipContentType, juce::String(chave), juce::String(chave), contagem,
                                mosaico_.filtrosContentTypeAtivos().count(juce::String(chave)) > 0});
        }
    }

    const auto& contagensCollection = dados_.porCollectionType;
    if (!contagensCollection.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_collection"), "", -1, false});
        for (auto& [chave, contagem] : contagensCollection) {
            linhas_.push_back({TipoLinha::ChipCollectionType, juce::String(chave), juce::String(chave), contagem,
                                mosaico_.filtrosCollectionTypeAtivos().count(juce::String(chave)) > 0});
        }
    }

    // Eixo de agrupamento da grade (item 4.3 — ano é eixo de agrupamento).
    bool porAno = mosaico_.modoAgrupamentoAtual() == MosaicoComponent::ModoAgrupamento::PorAno;
    linhas_.push_back({TipoLinha::ChipAgrupamento,
                        matriz::i18n::t(porAno ? "grade.agrupar_por_ano" : "grade.agrupar_automatico"), "", -1, porAno});

    bool algumFiltroAtivo = !mosaico_.filtrosTipoMidiaAtivos().empty() || !mosaico_.filtrosEstadoAtivos().empty() ||
                            !mosaico_.filtrosExtensaoAtivos().empty() || !mosaico_.filtrosOrigemAtivos().empty() ||
                            !mosaico_.filtrosContentTypeAtivos().empty() || !mosaico_.filtrosCollectionTypeAtivos().empty() ||
                            mosaico_.filtroFaixaAnoAtivo().has_value() || mosaico_.buscaAtual().isNotEmpty();
    if (algumFiltroAtivo) linhas_.push_back({TipoLinha::Limpar, matriz::i18n::t("filtros.limpar"), "", -1, false});

    // Vaults (§6.1): a árvore de cofres, com o indicador de conexão. Fica
    // no topo do painel de filtros porque "de qual disco isto veio" é a
    // primeira pergunta de quem abre um acervo grande.
    const auto& vaults = dados_.vaults;
    if (!vaults.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_vaults"), "", -1, false});
        for (const auto& v : vaults)
            linhas_.push_back({TipoLinha::Vault, v.nome, juce::String(v.id), v.totalItens, v.online});
    }

    // Coleções inteligentes embutidas (§10) — views SQL. Aparecem sempre,
    // inclusive zeradas: "0 ausentes" é uma informação, não uma linha vazia.
    const auto& embutidas = dados_.embutidas;
    if (!embutidas.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_colecoes_embutidas"), "", -1, false});
        for (const auto& c : embutidas)
            linhas_.push_back({TipoLinha::ColecaoEmbutida, c.rotulo, juce::String(c.chave), c.contagem,
                                colecaoEmbutidaFiltrada_ == juce::String(c.chave)});
    }

    linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_colecoes"), "", -1, false});
    for (auto& colecao : dados_.salvas)
        linhas_.push_back({TipoLinha::Colecao, colecao.nome, juce::String(colecao.id), -1, false});
    linhas_.push_back({TipoLinha::SalvarColecao, matriz::i18n::t("filtros.salvar_colecao"), "", -1, false});
}

juce::Rectangle<int> FiltrosComponent::boundsDaLinha(int indice) const {
    int y = 0;
    for (int i = 0; i < indice; ++i)
        y += linhas_[static_cast<size_t>(i)].tipo == TipoLinha::CabecalhoSecao ? kAlturaCabecalhoSecao : kAlturaLinha;
    int altura = linhas_[static_cast<size_t>(indice)].tipo == TipoLinha::CabecalhoSecao ? kAlturaCabecalhoSecao : kAlturaLinha;
    return {0, y, getWidth(), altura};
}

int FiltrosComponent::indiceLinhaNaPosicao(int y) const {
    int cursor = 0;
    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
        int altura = linhas_[static_cast<size_t>(i)].tipo == TipoLinha::CabecalhoSecao ? kAlturaCabecalhoSecao : kAlturaLinha;
        if (y >= cursor && y < cursor + altura) return i;
        cursor += altura;
    }
    return -1;
}

void FiltrosComponent::resized() {
    int altura = 0;
    for (auto& linha : linhas_) altura += linha.tipo == TipoLinha::CabecalhoSecao ? kAlturaCabecalhoSecao : kAlturaLinha;
    if (getHeight() != altura)
        setSize(getWidth(), altura);
}

void FiltrosComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
        auto& linha = linhas_[static_cast<size_t>(i)];
        auto bounds = boundsDaLinha(i);

        if (linha.tipo == TipoLinha::CabecalhoSecao) {
            g.setColour(tk.painelAlt);
            g.fillRect(bounds);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            g.drawText(linha.rotulo.toUpperCase(), bounds.reduced(tk.espacoMedio, 0), juce::Justification::centredLeft);
            continue;
        }

        // Alvo de drop: destaca a linha inteira enquanto o arrasto está por
        // cima dela, pra o operador saber em qual Vault vai soltar.
        if (i == linhaAlvoDrop_) {
            g.setColour(tk.acento.withAlpha(0.30f));
            g.fillRect(bounds);
        }

        // Em linha de Vault, `ativo` quer dizer ONLINE (a bolinha), não
        // "filtro ligado" — pintar o fundo de selecionado aqui faria todo
        // disco conectado parecer um filtro aplicado.
        if (linha.ativo && linha.tipo != TipoLinha::Vault) {
            g.setColour(tk.acento.withAlpha(0.18f));
            g.fillRect(bounds);
        }

        auto miolo = bounds.reduced(tk.espacoMedio, 0);

        if (linha.tipo == TipoLinha::Vault) {
            auto areaPonto = miolo.removeFromLeft(14);
            g.setColour(linha.ativo ? tk.estadoQcOk : tk.textoTerciario);
            g.fillEllipse(juce::Rectangle<float>(areaPonto.getX() + 2.0f, areaPonto.getCentreY() - 3.0f, 6.0f, 6.0f));
        }

        if (linha.tipo == TipoLinha::ChipTipoMidia) {
            juce::Colour corLegenda(0xff1a1a1a); // Unknown: black
            std::string chave = linha.chave.toStdString();
            if (!chave.empty()) {
                try {
                    auto& def = projeto_.definicaoPara(chave);
                    if (def.icone == "audio")       corLegenda = juce::Colour(0xff2a9d8f);
                    else if (def.icone == "video")   corLegenda = juce::Colour(0xff9d4edd);
                    else if (def.icone == "imagem")   corLegenda = juce::Colour(0xfff4a261);
                    else if (def.icone == "documento") corLegenda = juce::Colour(0xff2b9348);
                    else if (def.icone == "3d")       corLegenda = juce::Colour(0xffe76f51);
                } catch (const std::exception&) {}
            }
            auto areaPonto = miolo.removeFromLeft(14);
            g.setColour(corLegenda);
            g.fillEllipse(juce::Rectangle<float>(areaPonto.getX() + 2.0f, areaPonto.getCentreY() - 4.0f, 8.0f, 8.0f));
        }

        if (linha.contagem >= 0) {
            auto areaContagem = miolo.removeFromRight(36);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawText(juce::String(linha.contagem), areaContagem, juce::Justification::centredRight);
        }

        bool acao = linha.tipo == TipoLinha::Limpar || linha.tipo == TipoLinha::SalvarColecao;
        bool destacado = linha.ativo && linha.tipo != TipoLinha::Vault;
        g.setColour(destacado ? tk.acento : (acao ? tk.textoSecundario : tk.textoPrimario));
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, acao ? juce::Font::italic : juce::Font::plain)));
        g.drawText(linha.rotulo, miolo, juce::Justification::centredLeft, true);
    }
}

void FiltrosComponent::mouseDown(const juce::MouseEvent& e) {
    int indice = indiceLinhaNaPosicao(e.getPosition().y);
    if (indice < 0) return;
    auto& linha = linhas_[static_cast<size_t>(indice)];

    if (e.mods.isPopupMenu() && linha.tipo == TipoLinha::Colecao) {
        abrirMenuContextoColecao(indice);
        return;
    }

    switch (linha.tipo) {
        case TipoLinha::ChipTipoMidia: mosaico_.alternarFiltroTipoMidia(linha.chave); break;
        case TipoLinha::ChipEstado: mosaico_.alternarFiltroEstado(linha.chave); break;
        case TipoLinha::ChipExtensao: mosaico_.alternarFiltroExtensao(linha.chave); break;
        case TipoLinha::ChipOrigem: mosaico_.alternarFiltroOrigem(linha.chave); break;
        case TipoLinha::ChipContentType: mosaico_.alternarFiltroContentType(linha.chave); break;
        case TipoLinha::ChipCollectionType: mosaico_.alternarFiltroCollectionType(linha.chave); break;
        case TipoLinha::ChipAgrupamento:
            mosaico_.definirModoAgrupamento(mosaico_.modoAgrupamentoAtual() == MosaicoComponent::ModoAgrupamento::PorAno
                                                 ? MosaicoComponent::ModoAgrupamento::Automatico
                                                 : MosaicoComponent::ModoAgrupamento::PorAno);
            break;
        case TipoLinha::Limpar:
            mosaico_.limparFiltros();
            if (aoSincronizarBusca) aoSincronizarBusca({});
            break;
        case TipoLinha::Vault: {
            // Filtra a grade pelos itens daquele Vault. Clicar de novo no
            // mesmo tira o filtro — mesmo vocabulário dos chips.
            std::set<std::string> itens;
            auto stmt = projeto_.projeto().registro().prepare(
                "SELECT DISTINCT item_id FROM arquivo WHERE vault_id = ?");
            stmt.bind(1, matriz::db::Value::of(linha.chave.toStdString()));
            while (stmt.step()) itens.insert(stmt.columnText(0));

            if (vaultFiltrado_ == linha.chave) {
                vaultFiltrado_.clear();
                mosaico_.definirFiltroItens(std::nullopt);
            } else {
                vaultFiltrado_ = linha.chave;
                colecaoEmbutidaFiltrada_.clear();
                mosaico_.definirFiltroItens(std::move(itens));
            }
            recarregar();
            break;
        }

        case TipoLinha::ColecaoEmbutida: {
            if (colecaoEmbutidaFiltrada_ == linha.chave) {
                colecaoEmbutidaFiltrada_.clear();
                mosaico_.definirFiltroItens(std::nullopt);
            } else {
                colecaoEmbutidaFiltrada_ = linha.chave;
                vaultFiltrado_.clear();
                mosaico_.definirFiltroItens(projeto_.itensDaColecaoEmbutida(linha.chave.toStdString()));
            }
            recarregar();
            break;
        }

        case TipoLinha::Colecao: {
            auto colecoes = projeto_.listarColecoes();
            auto it = std::find_if(colecoes.begin(), colecoes.end(),
                                    [&](const auto& c) { return juce::String(c.id) == linha.chave; });
            if (it == colecoes.end()) break;
            // Reaplica a DEFINIÇÃO salva por inteiro — nunca um resultado
            // congelado (Acréscimos §10.2): limpa o que estava ativo e
            // aplica texto + cada categoria de chip da coleção.
            mosaico_.limparFiltros();
            if (aoSincronizarBusca) aoSincronizarBusca(it->buscaTexto);
            mosaico_.definirBusca(it->buscaTexto);
            for (auto& v : it->filtrosTipoMidia) mosaico_.alternarFiltroTipoMidia(v);
            for (auto& v : it->filtrosEstado) mosaico_.alternarFiltroEstado(v);
            for (auto& v : it->filtrosExtensao) mosaico_.alternarFiltroExtensao(v);
            for (auto& v : it->filtrosOrigem) mosaico_.alternarFiltroOrigem(v);
            for (auto& v : it->filtrosContentType) mosaico_.alternarFiltroContentType(v);
            for (auto& v : it->filtrosCollectionType) mosaico_.alternarFiltroCollectionType(v);
            if (it->anoDe && it->anoAte) mosaico_.definirFiltroFaixaAno(*it->anoDe, *it->anoAte);
            break;
        }
        case TipoLinha::SalvarColecao: salvarColecaoAtual(); break;
        case TipoLinha::CabecalhoSecao: break;
    }

    recarregar();
}

void FiltrosComponent::abrirMenuContextoColecao(int indiceLinha) {
    juce::String colecaoId = linhas_[static_cast<size_t>(indiceLinha)].chave;
    juce::PopupMenu menu;
    menu.addItem(1, matriz::i18n::t("arvore.menu_apagar"));
    juce::Component::SafePointer<FiltrosComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, colecaoId](int resultado) {
        if (!safeThis || resultado != 1) return;
        safeThis->projeto_.apagarColecao(colecaoId.toStdString());
        safeThis->recarregar();
    });
}

void FiltrosComponent::salvarColecaoAtual() {
    juce::Component::SafePointer<FiltrosComponent> safeThis(this);
    pedirTexto(matriz::i18n::t("filtros.nome_colecao_titulo"), matriz::i18n::t("filtros.nome_colecao_mensagem"), "",
               [safeThis](std::optional<juce::String> nome) {
                   if (!safeThis || !nome || nome->trim().isEmpty()) return;
                   ProjetoAberto::ColecaoInteligente c;
                   c.nome = nome->trim();
                   c.buscaTexto = safeThis->mosaico_.buscaAtual();
                   c.filtrosTipoMidia = safeThis->mosaico_.filtrosTipoMidiaAtivos();
                   c.filtrosEstado = safeThis->mosaico_.filtrosEstadoAtivos();
                   c.filtrosExtensao = safeThis->mosaico_.filtrosExtensaoAtivos();
                   c.filtrosOrigem = safeThis->mosaico_.filtrosOrigemAtivos();
                   c.filtrosContentType = safeThis->mosaico_.filtrosContentTypeAtivos();
                   c.filtrosCollectionType = safeThis->mosaico_.filtrosCollectionTypeAtivos();
                   if (auto faixa = safeThis->mosaico_.filtroFaixaAnoAtivo()) {
                       c.anoDe = faixa->first;
                       c.anoAte = faixa->second;
                   }
                   safeThis->projeto_.salvarColecao(c);
                   safeThis->recarregar();
               });
}



// ---------------------------------------------------------------------------
// Arrastar itens da grade pra cima de um Vault = PLANEJAR backup (§6).
//
// "Planejar" e não "gravar": criar uma publicação com status 'planejada' e
// listar os itens nela. A cópia byte a byte, a releitura e o manifesto são
// da publicação de verdade (§9) — um arrasto de mouse não pode disparar
// horas de escrita em disco sem o operador pedir.
// ---------------------------------------------------------------------------

namespace {
// item_publicacao.checksum_validado é NOT NULL, mas um item apenas PLANEJADO
// ainda não teve byte nenhum conferido. String vazia é o valor honesto:
// "existe na lista, nunca foi verificado". A publicação real preenche.
const char* kChecksumNaoVerificado = "";
} // namespace

bool FiltrosComponent::isInterestedInDragSource(const SourceDetails& details) {
    // Só o array de ids que a grade produz. Pasta vinda da EXPLORER é
    // assunto da árvore BACKUP, não daqui.
    return details.description.isArray();
}

void FiltrosComponent::itemDragMove(const SourceDetails& details) {
    int indice = indiceLinhaNaPosicao(details.localPosition.y);
    bool alvoValido = indice >= 0 && linhas_[static_cast<size_t>(indice)].tipo == TipoLinha::Vault;
    int novoAlvo = alvoValido ? indice : -1;
    if (novoAlvo != linhaAlvoDrop_) {
        linhaAlvoDrop_ = novoAlvo;
        repaint();
    }
}

void FiltrosComponent::itemDragExit(const SourceDetails&) {
    if (linhaAlvoDrop_ != -1) {
        linhaAlvoDrop_ = -1;
        repaint();
    }
}

void FiltrosComponent::itemDropped(const SourceDetails& details) {
    int alvo = linhaAlvoDrop_;
    linhaAlvoDrop_ = -1;
    repaint();
    if (alvo < 0) return;

    std::vector<std::string> itemIds;
    if (auto* arr = details.description.getArray())
        for (auto& v : *arr) itemIds.push_back(v.toString().toStdString());
    if (itemIds.empty()) return;

    planejarBackupNoVault(linhas_[static_cast<size_t>(alvo)].chave.toStdString(), itemIds);
}

int FiltrosComponent::indiceDaLinhaDeVaultParaTeste(const std::string& vaultId) const {
    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i)
        if (linhas_[static_cast<size_t>(i)].tipo == TipoLinha::Vault &&
            linhas_[static_cast<size_t>(i)].chave == juce::String(vaultId))
            return i;
    return -1;
}

int FiltrosComponent::indiceDaLinhaDeColecaoEmbutidaParaTeste(const std::string& chave) const {
    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i)
        if (linhas_[static_cast<size_t>(i)].tipo == TipoLinha::ColecaoEmbutida &&
            linhas_[static_cast<size_t>(i)].chave == juce::String(chave))
            return i;
    return -1;
}

void FiltrosComponent::soltarItensParaTeste(int indiceLinha, const std::vector<std::string>& itemIds) {
    if (indiceLinha < 0 || indiceLinha >= static_cast<int>(linhas_.size())) return;
    if (linhas_[static_cast<size_t>(indiceLinha)].tipo != TipoLinha::Vault) return;
    planejarBackupNoVault(linhas_[static_cast<size_t>(indiceLinha)].chave.toStdString(), itemIds);
}

void FiltrosComponent::planejarBackupNoVault(const std::string& vaultId,
                                              const std::vector<std::string>& itemIds) {
    auto& db = projeto_.projeto().registro();
    std::string agora = matriz::model::agoraIso8601();

    try {
        // Uma publicação planejada POR VAULT, reaproveitada: arrastar em
        // três levas pro mesmo disco monta uma lista só, não três planos
        // concorrentes pro mesmo destino.
        std::string publicacaoId;
        {
            auto stmt = db.prepare(
                "SELECT id FROM publicacao WHERE vault_id = ? AND status = 'planejada' "
                "ORDER BY criada_em DESC LIMIT 1");
            stmt.bind(1, matriz::db::Value::of(vaultId));
            if (stmt.step()) publicacaoId = stmt.columnText(0);
        }

        if (publicacaoId.empty()) {
            publicacaoId = matriz::model::novoUuid();
            db.run(
                "INSERT INTO publicacao (id, projeto_id, vault_id, autor, layout_pasta, criada_em, status, nota) "
                "VALUES (?, ?, ?, '', ?, ?, 'planejada', '')",
                {matriz::db::Value::of(publicacaoId),
                 matriz::db::Value::of(projeto_.projeto().projetoId()),
                 matriz::db::Value::of(vaultId),
                 matriz::db::Value::of(matriz::consolidacao::hierarquiaParaCsv(
                     matriz::consolidacao::hierarquiaDoProjeto(db))),
                 matriz::db::Value::of(agora)});
        }

        db.run("BEGIN", {});
        try {
            for (const auto& itemId : itemIds)
                db.run(
                    "INSERT INTO item_publicacao (item_id, publicacao_id, caminho_relativo, checksum_validado, "
                    "verificado_em) VALUES (?, ?, '', ?, ?) "
                    "ON CONFLICT(item_id, publicacao_id) DO NOTHING",
                    {matriz::db::Value::of(itemId), matriz::db::Value::of(publicacaoId),
                     matriz::db::Value::of(std::string(kChecksumNaoVerificado)),
                     matriz::db::Value::of(agora)});
            db.run("COMMIT", {});
        } catch (...) {
            db.run("ROLLBACK", {});
            throw;
        }

        int total = 0;
        {
            auto stmt = db.prepare("SELECT COUNT(*) FROM item_publicacao WHERE publicacao_id = ?");
            stmt.bind(1, matriz::db::Value::of(publicacaoId));
            stmt.step();
            total = static_cast<int>(stmt.columnInt(0));
        }

        recarregar();
        if (aoAvisar)
            aoAvisar(matriz::i18n::t("vault.backup_planejado")
                          .replace("{n}", juce::String(static_cast<int>(itemIds.size())))
                          .replace("{total}", juce::String(total)));
    } catch (const std::exception&) {
        if (aoAvisar) aoAvisar(matriz::i18n::t("vault.backup_planejado_falhou"));
    }
}

} // namespace matriz::ui
