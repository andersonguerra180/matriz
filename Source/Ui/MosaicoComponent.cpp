#include "MosaicoComponent.h"

#include "../Ficha/FichaI18n.h"
#include "../I18n/Strings.h"
#include "Tokens.h"

#include <algorithm>
#include <map>

namespace matriz::ui {

MosaicoComponent::MosaicoComponent(ProjetoAberto& projeto) : projeto_(projeto) { setWantsKeyboardFocus(true); }

MosaicoComponent::~MosaicoComponent() { poolMiniaturas_.removeAllJobs(true, 2000); }

void MosaicoComponent::recarregar() {
    {
        const juce::ScopedLock sl(cacheLock_);
        semMiniatura_.clear(); // um novo ingest pode ter gerado miniatura pra itens antes sem
    }
    itensTodos_ = projeto_.listarItens();
    aplicarFiltrosEOrdenacao();
}

namespace {
void alternar(std::set<juce::String>& conjunto, const juce::String& valor) {
    if (!conjunto.insert(valor).second) conjunto.erase(valor);
}

// Faixa de ano digitada na busca (item 4.3 — "entra na busca com faixas
// (1978–1985) além do valor exato"). Aceita hífen comum e travessão (o
// operador copia e cola de texto corrido). nullopt = não é uma faixa, o
// termo segue como busca textual normal.
std::optional<std::pair<int, int>> parsearFaixaAno(const juce::String& texto) {
    juce::String t = texto.trim().replace(juce::String::fromUTF8("–"), "-").replace(juce::String::fromUTF8("—"), "-");
    if (!t.contains("-")) return std::nullopt;
    juce::String de = t.upToFirstOccurrenceOf("-", false, false).trim();
    juce::String ate = t.fromFirstOccurrenceOf("-", false, false).trim();
    if (de.length() != 4 || ate.length() != 4) return std::nullopt;
    if (!de.containsOnly("0123456789") || !ate.containsOnly("0123456789")) return std::nullopt;
    int anoDe = de.getIntValue();
    int anoAte = ate.getIntValue();
    if (anoDe > anoAte) std::swap(anoDe, anoAte);
    return std::make_pair(anoDe, anoAte);
}
} // namespace

void MosaicoComponent::alternarFiltroTipoMidia(const juce::String& tipo) {
    alternar(filtrosTipoMidia_, tipo);
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::alternarFiltroEstado(const juce::String& estado) {
    alternar(filtrosEstado_, estado);
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::alternarFiltroExtensao(const juce::String& extensao) {
    alternar(filtrosExtensao_, extensao);
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::alternarFiltroOrigem(const juce::String& origem) {
    alternar(filtrosOrigem_, origem);
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirFiltroFaixaAno(int anoDe, int anoAte) {
    if (anoDe > anoAte) std::swap(anoDe, anoAte);
    filtroFaixaAno_ = std::make_pair(anoDe, anoAte);
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::limparFiltroFaixaAno() {
    filtroFaixaAno_.reset();
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirModoAgrupamento(ModoAgrupamento modo) {
    if (modoAgrupamento_ == modo) return;
    modoAgrupamento_ = modo;
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::limparFiltros() {
    filtrosTipoMidia_.clear();
    filtrosEstado_.clear();
    filtrosExtensao_.clear();
    filtrosOrigem_.clear();
    filtroFaixaAno_.reset();
    buscaTexto_.clear();
    buscaResultado_.reset();
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirBusca(const juce::String& texto) {
    buscaTexto_ = texto;
    // Consulta o banco a cada chamada (código/título/campo de ficha/
    // assunto — ver ProjetoAberto::buscarItens) em vez de um substring
    // check em memória: é o único jeito de a busca alcançar valor de ficha
    // e assunto, que não vivem em ItemResumo. "" limpa (nullopt = sem
    // filtro de busca, nunca "nenhum resultado").
    //
    // "1978-1985" é reconhecido como faixa de ano (item 4.3) e resolvido
    // por itensPorFaixaAno em vez de LIKE textual — um LIKE por "1978-1985"
    // não acharia nada, já que o valor gravado em cada item é só "1981".
    if (auto faixa = parsearFaixaAno(texto)) {
        buscaResultado_ = projeto_.itensPorFaixaAno(faixa->first, faixa->second);
    } else {
        buscaResultado_ = texto.trim().isEmpty() ? std::nullopt : std::optional(projeto_.buscarItens(texto));
    }
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirOrdenacao(Ordenacao ordenacao) {
    ordenacao_ = ordenacao;
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirFiltroItens(std::optional<std::set<std::string>> itemIds) {
    filtroItens_ = std::move(itemIds);
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirTamanhoCelula(TamanhoCelula tamanho) {
    if (tamanhoCelula_ == tamanho) return;
    tamanhoCelula_ = tamanho;
    switch (tamanho) {
        case TamanhoCelula::Pequeno: celulaLargura_ = 112; celulaAltura_ = 100; break;
        case TamanhoCelula::Grande: celulaLargura_ = 240; celulaAltura_ = 210; break;
        case TamanhoCelula::Medio:
        default: celulaLargura_ = 168; celulaAltura_ = 148; break;
    }
    recalcularLayout();
    repaint();
}

void MosaicoComponent::aplicarFiltrosEOrdenacao() {
    itensFiltrados_.clear();

    for (auto& item : itensTodos_) {
        // Eixos combinados com E: pasta da árvore, busca de texto, cada
        // categoria de chip. DENTRO de uma categoria de chip, múltipla
        // seleção é OU (Acréscimos §10.2 — "clicáveis e combináveis").
        if (filtroItens_ && !filtroItens_->count(item.id)) continue;
        if (buscaResultado_ && !buscaResultado_->count(item.id)) continue;
        if (!filtrosEstado_.empty() && !filtrosEstado_.count(juce::String(item.estado))) continue;
        if (!filtrosTipoMidia_.empty() && !filtrosTipoMidia_.count(juce::String(item.tipoMidia))) continue;
        if (!filtrosExtensao_.empty() && !filtrosExtensao_.count(juce::String(item.extensaoArquivo))) continue;
        if (!filtrosOrigem_.empty() && !filtrosOrigem_.count(juce::String(item.origem.value_or(std::string())))) continue;
        // Faixa de ano: item sem ano preenchido nunca entra numa faixa —
        // faixa é sobre o que se sabe, não sobre o que falta.
        if (filtroFaixaAno_ && (!item.ano || *item.ano < filtroFaixaAno_->first || *item.ano > filtroFaixaAno_->second))
            continue;
        itensFiltrados_.push_back(item);
    }

    auto comparador = [this](const ItemResumo& a, const ItemResumo& b) {
        switch (ordenacao_) {
            case Ordenacao::Titulo: return a.titulo < b.titulo;
            case Ordenacao::Estado: return a.estado < b.estado;
            case Ordenacao::Atualizado: return a.atualizadoEm > b.atualizadoEm;
            case Ordenacao::Codigo:
            default: return a.codigoAcervo < b.codigoAcervo;
        }
    };

    // Mosaico agrupado (Parte 1 da correção de fluxo, §3.5): por tipo de
    // mídia no Archive, por artista/lançamento no Catalog. Item sem tipo
    // de mídia ainda (Reorientação completa §7.1 — catalogar é trabalho de
    // depois) cai num grupo "Não classificado" próprio, nunca escondido.
    // A ordenação escolhida pelo operador vale DENTRO de cada grupo, não
    // entre grupos.
    bool catalog = projeto_.projeto().modo() == matriz::model::Modo::Catalogo;
    std::map<juce::String, std::vector<ItemResumo>> baldes;
    for (auto& item : itensFiltrados_) {
        juce::String chave;
        if (modoAgrupamento_ == ModoAgrupamento::PorAno) {
            // Ano como eixo de agrupamento (item 4.3). Material sem ano vai
            // pra um grupo "No year" no fim, nunca desaparece. O prefixo
            // numérico ordena os baldes (std::map ordena por string), então
            // o ano vai zero-padded pra 4 dígitos ordenar de verdade.
            chave = item.ano ? ("0:" + juce::String(*item.ano).paddedLeft('0', 4))
                             : ("1:" + matriz::i18n::t("grade.sem_ano"));
        } else if (item.tipoMidia.empty()) {
            chave = "2:" + matriz::i18n::t("grade.nao_classificado");
        } else if (catalog && item.artistaLancamento && item.tituloLancamento) {
            chave = "0:" + *item.artistaLancamento + " — " + *item.tituloLancamento;
        } else if (catalog) {
            chave = "1:" + rotuloGrupoArchive(item); // sample/suporte sem lançamento — bucket pelo próprio tipo
        } else {
            chave = "0:" + rotuloGrupoArchive(item);
        }
        baldes[chave].push_back(item);
    }

    itensFiltrados_.clear();
    grupos_.clear();
    // O índice de hover aponta pra uma posição da lista ANTERIOR — depois
    // de refiltrar/reagrupar ele passaria a realçar uma célula qualquer.
    indiceHover_ = -1;
    for (auto& [chave, itensDoGrupo] : baldes) {
        std::vector<ItemResumo> ordenados = itensDoGrupo;
        std::sort(ordenados.begin(), ordenados.end(), comparador);

        GrupoMosaico g;
        g.rotulo = chave.fromFirstOccurrenceOf(":", false, false) + " — " +
                   juce::String(static_cast<int>(ordenados.size()));
        g.indiceInicio = static_cast<int>(itensFiltrados_.size());
        g.quantidade = static_cast<int>(ordenados.size());
        grupos_.push_back(g);

        for (auto& item : ordenados) itensFiltrados_.push_back(std::move(item));
    }

    recalcularLayout();
    repaint();
    if (aoMudarConteudoVisivel) aoMudarConteudoVisivel();
}

juce::String MosaicoComponent::rotuloGrupoArchive(const ItemResumo& item) const {
    try {
        const auto& def = projeto_.definicaoPara(item.tipoMidia);
        return matriz::ficha::rotuloTipo(item.tipoMidia, def.rotulo.empty() ? item.tipoMidia : def.rotulo);
    } catch (const std::exception&) {
        return juce::String(item.tipoMidia);
    }
}

std::optional<std::string> MosaicoComponent::itemAdjacente(const std::string& itemIdAtual, int direcao) const {
    for (size_t i = 0; i < itensFiltrados_.size(); ++i) {
        if (itensFiltrados_[i].id != itemIdAtual) continue;
        long alvo = static_cast<long>(i) + direcao;
        if (alvo < 0 || alvo >= static_cast<long>(itensFiltrados_.size())) return std::nullopt;
        return itensFiltrados_[static_cast<size_t>(alvo)].id;
    }
    return std::nullopt;
}

void MosaicoComponent::selecionarItem(const std::string& itemId) {
    selecionadoId_ = itemId;
    selecionados_ = {itemId};
    repaint();
    if (aoMudarSelecao) aoMudarSelecao();
}

void MosaicoComponent::selecionarTodos() {
    // Só o que está visível sob os filtros atuais — ver nota no header.
    selecionados_.clear();
    for (auto& item : itensFiltrados_) selecionados_.insert(item.id);
    if (!itensFiltrados_.empty()) {
        selecionadoId_ = itensFiltrados_.front().id;
        indiceAncoraShift_ = 0;
    }
    repaint();
    if (aoMudarSelecao) aoMudarSelecao();
}

void MosaicoComponent::limparSelecao() {
    selecionados_.clear();
    selecionadoId_.clear();
    indiceAncoraShift_ = -1;
    repaint();
    if (aoMudarSelecao) aoMudarSelecao();
}

void MosaicoComponent::mouseMove(const juce::MouseEvent& e) {
    int indice = indiceNaPosicao(e.getPosition());
    if (indice == indiceHover_) return;
    indiceHover_ = indice;
    repaint();
}

void MosaicoComponent::mouseExit(const juce::MouseEvent&) {
    if (indiceHover_ < 0) return;
    indiceHover_ = -1;
    repaint();
}

void MosaicoComponent::recalcularLayout() {
    int larguraDisponivel = getWidth();
    colunas_ = juce::jmax(1, larguraDisponivel / celulaLargura_);

    // Grupos são O(quantidade de grupos), não O(total de itens) — mesmo
    // com 10 mil itens num único grupo (ver stress test B.2), este laço
    // roda uma vez só por grupo, cada um O(1).
    int cursor = matriz::ui::tema().espacoPainel;
    for (auto& g : grupos_) {
        g.yTopo = cursor;
        g.yItens = cursor + kAlturaCabecalhoGrupo;
        g.linhas = (g.quantidade + colunas_ - 1) / colunas_;
        cursor = g.yItens + g.linhas * celulaAltura_ + kEspacoEntreGrupos;
    }

    setSize(getWidth(), juce::jmax(getParentHeight(), cursor));
}

void MosaicoComponent::resized() { recalcularLayout(); }

const GrupoMosaico* MosaicoComponent::grupoNaPosicaoY(int y) const {
    for (auto& g : grupos_) {
        int yFim = g.yItens + g.linhas * celulaAltura_;
        if (y >= g.yTopo && y < yFim) return &g;
    }
    return nullptr;
}

juce::Rectangle<int> MosaicoComponent::boundsDaCelula(int indice) const {
    for (auto& g : grupos_) {
        if (indice < g.indiceInicio || indice >= g.indiceInicio + g.quantidade) continue;
        int localIndice = indice - g.indiceInicio;
        int coluna = localIndice % colunas_;
        int linha = localIndice / colunas_;
        return {coluna * celulaLargura_, g.yItens + linha * celulaAltura_, celulaLargura_, celulaAltura_};
    }
    return {};
}

int MosaicoComponent::indiceNaPosicao(juce::Point<int> pos) const {
    if (colunas_ <= 0) return -1;
    const GrupoMosaico* g = grupoNaPosicaoY(pos.y);
    if (!g || pos.y < g->yItens) return -1; // fora de um grupo, ou em cima do cabeçalho (não clicável)

    int coluna = pos.x / celulaLargura_;
    if (coluna < 0 || coluna >= colunas_) return -1;
    int linha = (pos.y - g->yItens) / celulaAltura_;
    int localIndice = linha * colunas_ + coluna;
    if (localIndice < 0 || localIndice >= g->quantidade) return -1;
    return g->indiceInicio + localIndice;
}

juce::Colour MosaicoComponent::corDoEstado(const std::string& estado) const {
    const auto& tk = matriz::ui::tema();
    if (estado == "capturado") return tk.estadoCapturado;
    if (estado == "qc_ok") return tk.estadoQcOk;
    if (estado == "alerta") return tk.estadoAlerta;
    if (estado == "publicado") return tk.estadoQcOk;
    // 'duplicata' (item 9) cai aqui de propósito — mesmo cinza neutro de
    // "não digitalizado". Não é um erro (por isso não é estadoAlerta), só
    // um fato: conteúdo já conhecido, reconhecido em vez de reimportado.
    return tk.estadoNaoDigitalizado;
}

void MosaicoComponent::mouseDown(const juce::MouseEvent& e) {
    if (itensFiltrados_.empty()) {
        if (aoClicarEstadoVazio) aoClicarEstadoVazio();
        return;
    }

    int indice = indiceNaPosicao(e.getPosition());

    // Botão direito: age sobre a seleção inteira quando o clique cai DENTRO
    // dela (é o que permite "selecionei 300, agora clico com o direito e
    // mando todos pra uma pasta"), e troca a seleção quando cai fora — que é
    // o comportamento de qualquer gerenciador de arquivos.
    if (e.mods.isPopupMenu() && indice >= 0) {
        const std::string& idClicado = itensFiltrados_[static_cast<size_t>(indice)].id;
        if (!selecionados_.count(idClicado)) {
            selecionados_ = {idClicado};
            selecionadoId_ = idClicado;
            indiceAncoraShift_ = indice;
            repaint();
            if (aoMudarSelecao) aoMudarSelecao();
            if (aoSelecionar) aoSelecionar(selecionadoId_);
        }
        if (aoPedirMenuContexto)
            aoPedirMenuContexto(std::vector<std::string>(selecionados_.begin(), selecionados_.end()));
        return;
    }

    if (indice < 0) {
        // Clique em área vazia (entre grupos, ou depois da última célula)
        // começa um laço de seleção. Sem Shift/Cmd, esvazia a seleção — é o
        // que "clicar no vazio" significa em qualquer gerenciador de arquivo.
        lacoAtivo_ = true;
        lacoInicio_ = e.getPosition();
        lacoAtual_ = juce::Rectangle<int>(lacoInicio_, lacoInicio_);
        bool somando = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
        if (!somando) selecionados_.clear();
        selecaoAntesDoLaco_ = selecionados_;
        repaint();
        if (aoMudarSelecao) aoMudarSelecao();
        return;
    }
    const std::string& id = itensFiltrados_[static_cast<size_t>(indice)].id;

    // Seleção múltipla (Reorientação completa §3.3): clique simples troca
    // a seleção inteira; Cmd/Ctrl alterna o item clicado sem mexer nos
    // outros; Shift seleciona o intervalo contíguo desde a última âncora.
    if (e.mods.isShiftDown() && indiceAncoraShift_ >= 0) {
        int de = juce::jmin(indiceAncoraShift_, indice);
        int ate = juce::jmax(indiceAncoraShift_, indice);
        for (int i = de; i <= ate; ++i) selecionados_.insert(itensFiltrados_[static_cast<size_t>(i)].id);
    } else if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
        if (!selecionados_.insert(id).second) selecionados_.erase(id); // já estava — tira
        indiceAncoraShift_ = indice;
    } else {
        selecionados_ = {id};
        indiceAncoraShift_ = indice;
    }

    selecionadoId_ = id;
    repaint();
    if (aoMudarSelecao) aoMudarSelecao();
    if (aoSelecionar) aoSelecionar(selecionadoId_);
}

void MosaicoComponent::mouseDrag(const juce::MouseEvent& e) {
    if (lacoAtivo_) {
        atualizarSelecaoDoLaco(e);
        return;
    }

    // Limiar de distância: sem isso, todo clique simples (que sempre gera
    // um mouseDrag de deslocamento ~0) tentaria iniciar uma sessão de
    // arrastar-e-soltar, brigando com a seleção por clique.
    if (e.getDistanceFromDragStart() < 8 || selecionados_.empty()) return;

    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (!container || container->isDragAndDropActive()) return;

    juce::var listaIds;
    for (auto& id : selecionados_) listaIds.append(juce::String(id));

    // Contador junto ao cursor — arrastar 142 arquivos precisa parecer que
    // são 142 arquivos.
    juce::Image etiqueta = imagemDeArrasto(static_cast<int>(selecionados_.size()));
    juce::Point<int> deslocamento(-etiqueta.getWidth() / 2, -etiqueta.getHeight() - 6);
    container->startDragging(listaIds, this, juce::ScaledImage(etiqueta), true, &deslocamento);
}

void MosaicoComponent::atualizarSelecaoDoLaco(const juce::MouseEvent& e) {
    lacoAtual_ = juce::Rectangle<int>(lacoInicio_, e.getPosition());

    // Recomeça sempre da seleção que existia quando o laço começou: assim
    // encolher o retângulo desmarca o que saiu de dentro dele, em vez de
    // acumular tudo o que o laço um dia tocou.
    selecionados_ = selecaoAntesDoLaco_;
    bool removendo = e.mods.isCommandDown() || e.mods.isCtrlDown();

    for (size_t i = 0; i < itensFiltrados_.size(); ++i) {
        if (!boundsDaCelula(static_cast<int>(i)).intersects(lacoAtual_)) continue;
        const std::string& id = itensFiltrados_[i].id;
        // Cmd/Ctrl com o laço TIRA da seleção o que for tocado — é o
        // complemento natural do Cmd+clique, que já alterna item a item.
        if (removendo && selecaoAntesDoLaco_.count(id)) selecionados_.erase(id);
        else selecionados_.insert(id);
    }

    if (!selecionados_.empty() && selecionadoId_.empty()) selecionadoId_ = *selecionados_.begin();
    repaint();
    if (aoMudarSelecao) aoMudarSelecao();
    if (aoSelecionar && !selecionadoId_.empty()) aoSelecionar(selecionadoId_);
}

void MosaicoComponent::mouseUp(const juce::MouseEvent&) {
    if (!lacoAtivo_) return;
    lacoAtivo_ = false;
    selecaoAntesDoLaco_.clear();
    repaint();
}

bool MosaicoComponent::keyPressed(const juce::KeyPress& tecla) {
    if (tecla.getKeyCode() == 'A' && (tecla.getModifiers().isCommandDown() || tecla.getModifiers().isCtrlDown())) {
        selecionarTodos(); // "tudo que está no filtro atual" — ver nota em selecionarTodos()
        if (aoSelecionar && !selecionadoId_.empty()) aoSelecionar(selecionadoId_);
        return true;
    }
    return false;
}

juce::Image MosaicoComponent::imagemDeArrasto(int quantidade) const {
    const auto& tk = matriz::ui::tema();
    juce::String texto = quantidade == 1
                              ? matriz::i18n::t("selecao.contagem_um")
                              : matriz::i18n::t("selecao.contagem").replace("{n}", juce::String(quantidade));

    juce::Font fonte(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold));
    int largura = juce::jlimit(90, 260, juce::GlyphArrangement::getStringWidthInt(fonte, texto) + 24);
    constexpr int kAltura = 26;

    juce::Image imagem(juce::Image::ARGB, largura, kAltura, true);
    juce::Graphics g(imagem);
    g.setColour(tk.acento);
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(largura),
                                                   static_cast<float>(kAltura)),
                            tk.raioMedio);
    g.setColour(tk.textoSobreAcento);
    g.setFont(fonte);
    g.drawText(texto, juce::Rectangle<int>(0, 0, largura, kAltura), juce::Justification::centred);
    return imagem;
}

void MosaicoComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    int indice = indiceNaPosicao(e.getPosition());
    if (indice < 0) return;
    const std::string& id = itensFiltrados_[static_cast<size_t>(indice)].id;
    if (aoAbrirPreview) aoAbrirPreview(id);
}

const juce::Image* MosaicoComponent::miniaturaCache(const std::string& itemId) {
    const juce::ScopedLock sl(cacheLock_);
    auto it = cacheMiniaturas_.find(itemId);
    if (it != cacheMiniaturas_.end()) return &it->second;
    return nullptr;
}

void MosaicoComponent::pedirCarregamentoMiniatura(const std::string& itemId) {
    {
        const juce::ScopedLock sl(cacheLock_);
        if (emCarregamento_.count(itemId) || cacheMiniaturas_.count(itemId) || semMiniatura_.count(itemId)) return;
        emCarregamento_[itemId] = true;
    }

    // A consulta ao índice também vai pro pool — antes rodava síncrona na
    // thread de paint(), o que reconsultava o banco a cada repaint pra cada
    // célula sem miniatura ainda (achado no stress test de 10 mil itens,
    // B.2). Cache negativo (semMiniatura_) evita repetir a consulta depois
    // que já se sabe que o item não tem miniatura.
    juce::Component::SafePointer<MosaicoComponent> ponteiroSeguro(this);
    ProjetoAberto* projeto = &projeto_;

    poolMiniaturas_.addJob([ponteiroSeguro, projeto, itemId]() mutable {
        auto caminho = projeto->caminhoMiniaturaPrincipal(itemId);
        juce::Image imagem;
        if (caminho) imagem = juce::ImageFileFormat::loadFrom(juce::File(*caminho));
        bool temCaminho = caminho.has_value();

        juce::MessageManager::callAsync([ponteiroSeguro, itemId, imagem, temCaminho]() mutable {
            if (!ponteiroSeguro) return;
            auto* self = ponteiroSeguro.getComponent();
            const juce::ScopedLock sl(self->cacheLock_);
            self->emCarregamento_.erase(itemId);
            if (imagem.isValid()) {
                self->cacheMiniaturas_[itemId] = imagem;
                self->ordemCache_.push_back(itemId);
                while (self->ordemCache_.size() > kCapacidadeCache) {
                    self->cacheMiniaturas_.erase(self->ordemCache_.front());
                    self->ordemCache_.pop_front();
                }
            } else if (!temCaminho) {
                self->semMiniatura_[itemId] = true;
            }
            self->repaint();
        });
    });
}

// Ícone por categoria — nunca bloco cinza vazio (§3.3), mesmo pra tipos
// sem miniatura real ainda (PDF, documento, desconhecido — sem leitor de
// PDF/texto nesta etapa, ver nota em Source/Ingest/LeituraTecnica.cpp).
// Vetorial, não depende de fonte de ícone nenhuma.
void MosaicoComponent::desenharPlaceholderCategoria(juce::Graphics& g, juce::Rectangle<int> area,
                                                       const std::string& extensao) const {
    const auto& tk = matriz::ui::tema();
    auto categoria = matriz::ingest::categoriaPorExtensao(juce::File("x." + extensao));
    auto miolo = area.reduced(area.getWidth() / 4, area.getHeight() / 4).toFloat();
    g.setColour(tk.textoTerciario);

    switch (categoria) {
        case matriz::ingest::CategoriaMidia::Audio: {
            // Três barras de altura desigual — ícone de forma de onda.
            float larguraBarra = miolo.getWidth() / 5.0f;
            for (int i = 0; i < 3; ++i) {
                float altura = miolo.getHeight() * (i == 1 ? 1.0f : 0.55f);
                juce::Rectangle<float> barra(miolo.getX() + (i * 2 + 1) * larguraBarra,
                                               miolo.getCentreY() - altura / 2.0f, larguraBarra, altura);
                g.fillRoundedRectangle(barra, 1.5f);
            }
            break;
        }
        case matriz::ingest::CategoriaMidia::Video: {
            juce::Path triangulo;
            triangulo.addTriangle(miolo.getX(), miolo.getY(), miolo.getX(), miolo.getBottom(), miolo.getRight(),
                                    miolo.getCentreY());
            g.fillPath(triangulo);
            break;
        }
        case matriz::ingest::CategoriaMidia::Imagem: {
            g.drawRoundedRectangle(miolo, 3.0f, 1.5f);
            g.fillEllipse(miolo.getX() + miolo.getWidth() * 0.15f, miolo.getY() + miolo.getHeight() * 0.15f,
                           miolo.getWidth() * 0.22f, miolo.getWidth() * 0.22f);
            juce::Path montanha;
            montanha.startNewSubPath(miolo.getX(), miolo.getBottom());
            montanha.lineTo(miolo.getX() + miolo.getWidth() * 0.4f, miolo.getY() + miolo.getHeight() * 0.4f);
            montanha.lineTo(miolo.getX() + miolo.getWidth() * 0.65f, miolo.getY() + miolo.getHeight() * 0.65f);
            montanha.lineTo(miolo.getRight(), miolo.getY() + miolo.getHeight() * 0.3f);
            montanha.lineTo(miolo.getRight(), miolo.getBottom());
            montanha.closeSubPath();
            g.fillPath(montanha);
            break;
        }
        case matriz::ingest::CategoriaMidia::Documento: {
            juce::Path pagina;
            float dobra = miolo.getWidth() * 0.3f;
            pagina.startNewSubPath(miolo.getX(), miolo.getY());
            pagina.lineTo(miolo.getRight() - dobra, miolo.getY());
            pagina.lineTo(miolo.getRight(), miolo.getY() + dobra);
            pagina.lineTo(miolo.getRight(), miolo.getBottom());
            pagina.lineTo(miolo.getX(), miolo.getBottom());
            pagina.closeSubPath();
            g.strokePath(pagina, juce::PathStrokeType(1.5f));
            for (int i = 0; i < 3; ++i) {
                float y = miolo.getY() + miolo.getHeight() * (0.45f + i * 0.15f);
                g.drawLine(miolo.getX() + 4, y, miolo.getRight() - 4, y, 1.0f);
            }
            break;
        }
        default: {
            g.drawEllipse(miolo, 1.5f);
            g.setFont(juce::Font(juce::FontOptions(miolo.getHeight() * 0.5f, juce::Font::bold)));
            g.drawText("?", miolo.toNearestInt(), juce::Justification::centred);
            break;
        }
    }
}

void MosaicoComponent::paint(juce::Graphics& g) {
    const auto& tk = matriz::ui::tema();
    g.fillAll(tk.fundo);

    if (arrastandoArquivo_) {
        g.setColour(tk.acento.withAlpha(0.12f));
        g.fillRect(g.getClipBounds());
        g.setColour(tk.acento);
        g.drawRect(getLocalBounds(), 2);
    }

    if (itensFiltrados_.empty()) {
        // Estado vazio explicativo (Reorientação completa §3.2) — nunca
        // tela preta. Borda tracejada + duas linhas de instrução + nota
        // de rodapé; a área inteira já é clicável (mouseDown) e alvo de
        // drop (MainComponent, que também controla arrastandoArquivo_).
        auto areaCaixa = getLocalBounds().withSizeKeepingCentre(
            juce::jmin(420, getWidth() - 40), juce::jmin(220, getHeight() - 40));

        juce::Path tracejado;
        tracejado.addRoundedRectangle(areaCaixa.toFloat(), tk.raioMedio);
        juce::PathStrokeType tipoTraco(1.5f);
        float dashes[] = {6.0f, 5.0f};
        juce::Path tracejadoPontilhado;
        tipoTraco.createDashedStroke(tracejadoPontilhado, tracejado, dashes, 2);
        g.setColour(arrastandoArquivo_ ? tk.acento : tk.borda);
        g.fillPath(tracejadoPontilhado);

        auto miolo = areaCaixa.reduced(24);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
        g.drawText(matriz::i18n::t(arrastandoArquivo_ ? "mosaico.solte_para_ingerir" : "estado_vazio.titulo"),
                   miolo.removeFromTop(30), juce::Justification::centred);
        miolo.removeFromTop(tk.espacoPequeno);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("estado_vazio.subtitulo"), miolo.removeFromTop(24), juce::Justification::centred);
        miolo.removeFromBottom(4);
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawFittedText(matriz::i18n::t("estado_vazio.rodape"), miolo.removeFromBottom(32),
                          juce::Justification::centredBottom, 2);
        return;
    }

    juce::Rectangle<int> clip = g.getClipBounds();
    if (colunas_ <= 0) return;

    // Só os grupos cujo intervalo vertical cruza o clip entram no laço —
    // O(grupos), nunca O(total de itens). Dentro de cada grupo, só as
    // linhas visíveis (mesma lógica de antes, agora relativa a g.yItens).
    for (auto& grupo : grupos_) {
        int yFimGrupo = grupo.yItens + grupo.linhas * celulaAltura_;
        if (yFimGrupo < clip.getY() || grupo.yTopo > clip.getBottom()) continue;

        if (grupo.yTopo < clip.getBottom() && yFimGrupo > clip.getY()) {
            juce::Rectangle<int> areaCabecalho(0, grupo.yTopo, getWidth(), kAlturaCabecalhoGrupo);
            g.setColour(tk.painelAlt);
            g.fillRect(areaCabecalho);
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            g.drawText(grupo.rotulo, areaCabecalho.reduced(tema().espacoMedio, 0), juce::Justification::centredLeft);
        }

        int primeiraLinha = juce::jmax(0, (clip.getY() - grupo.yItens) / celulaAltura_);
        int ultimaLinha = juce::jmax(0, (clip.getBottom() - grupo.yItens) / celulaAltura_ + 1);
        int primeiroLocal = juce::jmax(0, primeiraLinha * colunas_);
        int ultimoLocal = juce::jmin(grupo.quantidade, (ultimaLinha + 1) * colunas_);

        for (int local = primeiroLocal; local < ultimoLocal; ++local) {
            int i = grupo.indiceInicio + local;
            const ItemResumo& item = itensFiltrados_[static_cast<size_t>(i)];
            juce::Rectangle<int> bounds = boundsDaCelula(i).reduced(4);

            juce::Colour corEstado = corDoEstado(item.estado);
            bool selecionado = selecionados_.count(item.id) > 0;
            bool sobHover = i == indiceHover_;

            // Cartão sob a célula: some no estado normal, aparece sob o
            // cursor e fica tingido de acento quando selecionado. É o que
            // faz a grade responder ao mouse em vez de parecer um pôster.
            if (sobHover || selecionado) {
                g.setColour(selecionado ? tk.acento.withAlpha(0.16f) : tk.painelAlt);
                g.fillRoundedRectangle(bounds.toFloat().expanded(3.0f), tk.raioMedio);
            }

            // Área de miniatura — sempre um ícone por categoria enquanto
            // não há miniatura real, nunca um bloco cinza sem sentido.
            juce::Rectangle<int> areaImagem = bounds.withHeight(bounds.getHeight() - 34);
            const juce::Image* img = miniaturaCache(item.id);
            if (img) {
                g.setColour(tk.painelAlt);
                g.fillRoundedRectangle(areaImagem.toFloat(), tk.raioPequeno);
                g.drawImage(*img, areaImagem.toFloat(), juce::RectanglePlacement::centred);
            } else {
                pedirCarregamentoMiniatura(item.id);
                g.setColour(tk.painelAlt);
                g.fillRoundedRectangle(areaImagem.toFloat(), tk.raioPequeno);
                desenharPlaceholderCategoria(g, areaImagem, item.extensaoArquivo);
            }

            // Borda de estado (cinza/azul/verde/âmbar)
            g.setColour(corEstado);
            g.drawRoundedRectangle(areaImagem.toFloat(), tk.raioPequeno, 2.0f);

            // Halo de sincronizado — anel adicional por fora, sobreposto a qualquer estado
            if (item.sincronizado) {
                g.setColour(tk.haloSincronizado);
                g.drawRoundedRectangle(bounds.toFloat(), tk.raioMedio, 1.5f);
            }

            // Seleção — todos os itens selecionados (§3.3), não só o último
            // clicado. Além da borda, um disco com "✓" na quina: numa
            // seleção de centenas de células a borda azul sozinha se
            // confunde com a borda de estado "capturado", que também é azul.
            if (selecionado) {
                g.setColour(tk.bordaFoco);
                g.drawRoundedRectangle(bounds.toFloat().expanded(2.0f), tk.raioMedio, 2.0f);

                constexpr int kDiametroMarca = 18;
                juce::Rectangle<float> marca(static_cast<float>(areaImagem.getRight() - kDiametroMarca - 4),
                                              static_cast<float>(areaImagem.getY() + 4),
                                              static_cast<float>(kDiametroMarca),
                                              static_cast<float>(kDiametroMarca));
                g.setColour(tk.acento);
                g.fillEllipse(marca);
                g.setColour(tk.textoSobreAcento);
                juce::Path tique;
                tique.startNewSubPath(marca.getX() + kDiametroMarca * 0.26f, marca.getCentreY());
                tique.lineTo(marca.getX() + kDiametroMarca * 0.44f, marca.getY() + kDiametroMarca * 0.68f);
                tique.lineTo(marca.getX() + kDiametroMarca * 0.76f, marca.getY() + kDiametroMarca * 0.32f);
                g.strokePath(tique, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }

            // Código + título
            juce::Rectangle<int> areaTexto = bounds.removeFromBottom(30);
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            g.drawText(item.codigoAcervo, areaTexto.removeFromTop(15), juce::Justification::centredLeft, true);
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawText(item.titulo, areaTexto, juce::Justification::centredLeft, true);
        }
    }

    // Retângulo do laço por último, por cima das células que ele cruza.
    if (lacoAtivo_ && !lacoAtual_.isEmpty()) {
        g.setColour(tk.acento.withAlpha(0.18f));
        g.fillRect(lacoAtual_);
        g.setColour(tk.acento);
        g.drawRect(lacoAtual_, 1);
    }
}

} // namespace matriz::ui
