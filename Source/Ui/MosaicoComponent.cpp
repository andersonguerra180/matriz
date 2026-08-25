#include "MosaicoComponent.h"
#include "../Diag/Watchdog.h"

#include "../Ficha/FichaI18n.h"
#include "../I18n/Strings.h"
#include "Tokens.h"

#include <algorithm>
#include <map>

namespace matriz::ui {

MosaicoComponent::MosaicoComponent(ProjetoAberto& projeto) : projeto_(projeto) {
    setWantsKeyboardFocus(true);
    EventBus::obterInstancia().registrarListener(this);
}

MosaicoComponent::~MosaicoComponent() {
    EventBus::obterInstancia().removerListener(this);
    poolMiniaturas_.removeAllJobs(true, 2000);
}

void MosaicoComponent::aoItemAlterado(const EventoItemAlterado&) {
    // Desregistrar no destrutor não basta: um evento recebido um instante
    // antes já postou esta mensagem, que só é entregue depois.
    juce::Component::SafePointer<MosaicoComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (safeThis == nullptr) return;
        safeThis->recarregar();
    });
}

void MosaicoComponent::recarregar() {
    MATRIZ_TRACE("MosaicoComponent::recarregar");
    if (editorInline_) cancelarEdicaoInline();
    // Um snapshot por vez. Sem isto, um lote de 5.000 arquivos enfileirava um
    // job a cada 700 ms enquanto o anterior ainda rodava, e cada job carrega
    // um vetor de 5.000 ItemResumo — dezenas de cópias vivas ao mesmo tempo.
    if (snapshotPendente_) return;
    snapshotPendente_ = true;
    {
        const juce::ScopedLock sl(cacheLock_);
        semMiniatura_.clear(); // um novo ingest pode ter gerado miniatura pra itens antes sem
    }

    // I1/I4: listarItens() é uma consulta completa com subqueries por item —
    // com 100 mil itens são vários SEGUNDOS. Rodar isso aqui congelava a
    // janela inteira. A Thread de Snapshot monta o vetor em background e a
    // message thread só recebe o resultado pronto.
    //
    // Geração: um recarregar novo invalida o anterior. Sem isso, um ingest
    // que dispara vários recarregamentos deixaria a resposta mais lenta
    // sobrescrever a mais recente.
    const int geracao = ++geracaoSnapshot_;
    juce::Component::SafePointer<MosaicoComponent> safeThis(this);
    ProjetoAberto* projeto = &projeto_;

    poolSnapshot_.addJob([safeThis, projeto, geracao]() {
        std::vector<ItemResumo> itens;
        try {
            itens = projeto->listarItens();
        } catch (const std::exception&) {
            return;  // projeto fechado no meio: nada a entregar
        }

        juce::MessageManager::callAsync([safeThis, geracao, itens = std::move(itens)]() mutable {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            if (geracao != self->geracaoSnapshot_) return;  // snapshot superado
            MATRIZ_TRACE("MosaicoComponent::aplicarSnapshot");
            self->snapshotPendente_ = false;
            self->itensTodos_ = std::move(itens);
            self->aplicarFiltrosEOrdenacao();
            if (self->aoMudarConteudoVisivel) self->aoMudarConteudoVisivel();
        });
    });
}

void MosaicoComponent::atualizarItemEmMemoria(const std::string& itemId) {
    if (itemId.empty()) return;

    ++geracaoSnapshot_;
    snapshotPendente_ = false;

    auto it = std::find_if(itensTodos_.begin(), itensTodos_.end(),
                           [&](const ItemResumo& r) { return r.id == itemId; });
    if (it == itensTodos_.end()) return;

    auto tit = projeto_.lerMetadado(itemId, "titulo");
    if (tit.has_value()) {
        it->titulo = *tit;
        if (!tit->empty()) it->nomeOriginalArquivo = *tit;
    }
    auto anoStr = projeto_.lerMetadado(itemId, "ano");
    if (anoStr.has_value() && !anoStr->empty()) {
        juce::String s(*anoStr);
        if (s.containsOnly("0123456789")) it->ano = s.getIntValue();
    } else {
        it->ano.reset();
    }
    auto ct = projeto_.lerMetadado(itemId, "content_type");
    it->contentType = ct.has_value() && !ct->empty() ? ct : std::nullopt;
    auto col = projeto_.lerMetadado(itemId, "collection_type");
    it->collectionType = col.has_value() && !col->empty() ? col : std::nullopt;

    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::recarregarSincrono() {
    // Só pro harness headless e pro stress test, que precisam do resultado
    // na mesma pilha de chamada. Em produção nada chama isto — é justamente
    // o caminho que trava a janela com acervo grande.
    {
        const juce::ScopedLock sl(cacheLock_);
        semMiniatura_.clear();
    }
    ++geracaoSnapshot_;  // invalida qualquer snapshot em voo
    snapshotPendente_ = false;
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

void MosaicoComponent::alternarFiltroContentType(const juce::String& contentType) {
    alternar(filtrosContentType_, contentType);
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::alternarFiltroCollectionType(const juce::String& collectionType) {
    alternar(filtrosCollectionType_, collectionType);
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
    filtrosContentType_.clear();
    filtrosCollectionType_.clear();
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

void MosaicoComponent::definirSubpastas(std::vector<SubpastaInfo> subpastas) {
    subpastas_ = std::move(subpastas);
    recalcularLayout();
    repaint();
}

int MosaicoComponent::alturaSecaoSubpastas() const {
    if (subpastas_.empty() || colunas_ <= 0) return 0;
    int linhas = (static_cast<int>(subpastas_.size()) + colunas_ - 1) / colunas_;
    return linhas * celulaAltura_;
}

juce::Rectangle<int> MosaicoComponent::boundsSubpasta(int indice) const {
    if (indice < 0 || indice >= static_cast<int>(subpastas_.size())) return {};
    int coluna = indice % colunas_;
    int linha = indice / colunas_;
    int yBase = matriz::ui::tema().espacoPainel;
    return {coluna * celulaLargura_, yBase + linha * celulaAltura_, celulaLargura_, celulaAltura_};
}

int MosaicoComponent::indiceSubpastaNaPosicao(juce::Point<int> pos) const {
    if (subpastas_.empty() || colunas_ <= 0) return -1;
    int yBase = matriz::ui::tema().espacoPainel;
    int yRelativo = pos.y - yBase;
    if (yRelativo < 0) return -1;
    int linha = yRelativo / celulaAltura_;
    int totalLinhas = (static_cast<int>(subpastas_.size()) + colunas_ - 1) / colunas_;
    if (linha >= totalLinhas) return -1;
    int coluna = pos.x / celulaLargura_;
    if (coluna < 0 || coluna >= colunas_) return -1;
    int indice = linha * colunas_ + coluna;
    if (indice >= static_cast<int>(subpastas_.size())) return -1;
    return indice;
}

void MosaicoComponent::desenharSubpasta(juce::Graphics& g, juce::Rectangle<int> bounds, const SubpastaInfo& sub) const {
    const auto& tk = matriz::ui::tema();
    auto area = bounds.reduced(modoVisao_ == ModoVisao::Lista ? 1 : 4);

    if (modoVisao_ == ModoVisao::Lista) {
        g.setColour(tk.painelAlt.brighter(0.1f));
        g.fillRect(area);
        g.setColour(tk.borda.withAlpha(0.3f));
        g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);
        auto linha = area.reduced(4, 0);
        auto areaIcone = linha.removeFromLeft(22);
        g.setColour(tk.acento);
        juce::Path pasta;
        float fx = static_cast<float>(areaIcone.getCentreX() - 7);
        float fy = static_cast<float>(areaIcone.getCentreY() - 5);
        pasta.addRoundedRectangle(fx, fy + 3.0f, 14.0f, 8.0f, 1.5f);
        pasta.addRoundedRectangle(fx, fy, 6.0f, 4.0f, 1.0f, 1.0f, true, true, false, false);
        g.fillPath(pasta);
        linha.removeFromLeft(10);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText(sub.nome, linha.removeFromLeft(linha.getWidth() * 2 / 3), juce::Justification::centredLeft, true);
        g.setColour(tk.textoTerciario);
        g.drawText(juce::String(sub.quantidade), linha, juce::Justification::centredRight, true);
        return;
    }

    g.setColour(tk.painelAlt.brighter(0.05f));
    g.fillRoundedRectangle(area.toFloat(), tk.raioMedio);
    g.setColour(tk.borda);
    g.drawRoundedRectangle(area.toFloat(), tk.raioMedio, 1.0f);

    auto areaIcone = area.withHeight(area.getHeight() - 34).reduced(area.getWidth() / 4, area.getHeight() / 6);
    g.setColour(tk.acento.withAlpha(0.7f));
    juce::Path pasta;
    float fx = static_cast<float>(areaIcone.getCentreX() - areaIcone.getWidth() / 2);
    float fy = static_cast<float>(areaIcone.getCentreY() - areaIcone.getHeight() / 2);
    float fw = static_cast<float>(areaIcone.getWidth());
    float fh = static_cast<float>(areaIcone.getHeight());
    pasta.addRoundedRectangle(fx, fy + fh * 0.25f, fw, fh * 0.75f, 3.0f);
    pasta.addRoundedRectangle(fx, fy, fw * 0.45f, fh * 0.3f, 2.0f, 2.0f, true, true, false, false);
    g.fillPath(pasta);

    auto areaTexto = area.removeFromBottom(30);
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    g.drawText(sub.nome, areaTexto.removeFromTop(15), juce::Justification::centredLeft, true);
    g.setColour(tk.textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    g.drawText(juce::String(sub.quantidade) + " items", areaTexto, juce::Justification::centredLeft, true);
}

void MosaicoComponent::definirModoVisao(ModoVisao modo) {
    if (modoVisao_ == modo) return;
    modoVisao_ = modo;
    if (modo == ModoVisao::Lista) {
        celulaLargura_ = getWidth() > 0 ? getWidth() : 600;
        celulaAltura_ = 44;
    } else {
        definirTamanhoCelula(tamanhoCelula_);
        return;
    }
    recalcularLayout();
    repaint();
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

void MosaicoComponent::definirTamanhoContinuo(double valor) {
    if (modoVisao_ == ModoVisao::Lista) return;
    int novaLargura = static_cast<int>(80.0 + valor * (320.0 - 80.0));
    int novaAltura = static_cast<int>(novaLargura * 0.88);
    if (novaLargura == celulaLargura_) return;
    celulaLargura_ = novaLargura;
    celulaAltura_ = novaAltura;
    recalcularLayout();
    repaint();
}

double MosaicoComponent::tamanhoContinuoAtual() const {
    return (celulaLargura_ - 80.0) / (320.0 - 80.0);
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
        if (!filtrosContentType_.empty() && !filtrosContentType_.count(juce::String(item.contentType.value_or(std::string())))) continue;
        if (!filtrosCollectionType_.empty() && !filtrosCollectionType_.count(juce::String(item.collectionType.value_or(std::string())))) continue;
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
            default: return a.criadoEm > b.criadoEm; // Últimas ingestões acima, mais antigas abaixo
        }
    };

    // Mosaico agrupado por tipo de arquivo.
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
        g.rotulo = chave.fromFirstOccurrenceOf(":", false, false) + " - " +
                   juce::String(static_cast<int>(ordenados.size()));
        g.indiceInicio = static_cast<int>(itensFiltrados_.size());
        g.quantidade = static_cast<int>(ordenados.size());
        grupos_.push_back(g);

        for (auto& item : ordenados) itensFiltrados_.push_back(std::move(item));
    }

    std::set<std::string> idsVisiveis;
    for (auto& item : itensFiltrados_) idsVisiveis.insert(item.id);
    for (auto it = selecionados_.begin(); it != selecionados_.end();) {
        if (!idsVisiveis.count(*it)) it = selecionados_.erase(it);
        else ++it;
    }
    if (!selecionadoId_.empty() && !idsVisiveis.count(selecionadoId_))
        selecionadoId_.clear();

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
    if (modoVisao_ == ModoVisao::Lista) {
        colunas_ = 1;
        celulaLargura_ = juce::jmax(200, larguraDisponivel);
    } else {
        colunas_ = juce::jmax(1, larguraDisponivel / celulaLargura_);
    }

    // Grupos são O(quantidade de grupos), não O(total de itens) — mesmo
    // com 10 mil itens num único grupo (ver stress test B.2), este laço
    // roda uma vez só por grupo, cada um O(1).
    int cursor = matriz::ui::tema().espacoPainel + alturaSecaoSubpastas();
    for (auto& g : grupos_) {
        g.yTopo = cursor;
        g.yItens = cursor + kAlturaCabecalhoGrupo;
        g.linhas = (g.quantidade + colunas_ - 1) / colunas_;
        cursor = g.yItens + g.linhas * celulaAltura_ + kEspacoEntreGrupos;
    }

    int targetH = juce::jmax(getParentHeight(), cursor);
    if (getHeight() != targetH)
        setSize(getWidth(), targetH);
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

    // Estados de workflow do §4 ("a borda do card muda de cor conforme o
    // status", §6). A leitura é de progresso: cinza = ainda não olhado,
    // azul = em trabalho, verde = aprovado/preservado, âmbar = precisa de
    // atenção.
    if (estado == "novo") return tk.estadoNaoDigitalizado;
    if (estado == "em_analise") return tk.estadoCapturado;
    if (estado == "catalogado") return tk.estadoCapturado;
    if (estado == "revisado") return tk.estadoQcOk;
    if (estado == "aprovado") return tk.estadoQcOk;
    if (estado == "publicado") return tk.estadoQcOk;
    if (estado == "arquivado") return tk.estadoQcOk;

    // Estados legados, de projetos criados antes do §4.
    if (estado == "capturado") return tk.estadoCapturado;
    if (estado == "qc_ok") return tk.estadoQcOk;
    if (estado == "alerta") return tk.estadoAlerta;

    // 'duplicata' (item 9) cai aqui de propósito — mesmo cinza neutro de
    // "não digitalizado". Não é um erro (por isso não é estadoAlerta), só
    // um fato: conteúdo já conhecido, reconhecido em vez de reimportado.
    return tk.estadoNaoDigitalizado;
}

juce::Colour MosaicoComponent::corPorCategoria(const std::string& tipoMidia) const {
    if (!tipoMidia.empty()) {
        try {
            const auto& def = projeto_.definicaoPara(tipoMidia);
            if (def.icone == "audio")       return juce::Colour(0xff2a9d8f);
            if (def.icone == "video")       return juce::Colour(0xff9d4edd);
            if (def.icone == "imagem")      return juce::Colour(0xfff4a261);
            if (def.icone == "documento")   return juce::Colour(0xff2b9348);
            if (def.icone == "3d")          return juce::Colour(0xffe76f51);
        } catch (const std::exception&) {}
    }
    return juce::Colour(0xff1a1a1a);
}

void MosaicoComponent::mouseDown(const juce::MouseEvent& e) {
    if (indiceSubpastaNaPosicao(e.getPosition()) >= 0)
        return;

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
    if (editorInline_) cancelarEdicaoInline();

    const std::string& id = itensFiltrados_[static_cast<size_t>(indice)].id;

    clicouNaTituloDeItemSelecionado_ = false;
    stopTimer();
    if (modoVisao_ == ModoVisao::Grade && selecionados_.size() == 1 && selecionados_.count(id)
        && !e.mods.isPopupMenu() && !e.mods.isShiftDown() && !e.mods.isCommandDown() && !e.mods.isCtrlDown()) {
        auto tituloArea = areaTituloDaCelula(indice);
        if (tituloArea.contains(e.getPosition())) {
            clicouNaTituloDeItemSelecionado_ = true;
            indiceEditando_ = indice;
            startTimer(500);
            return;
        }
    }

    // Detect click on checkbox area — toggle without modifier keys.
    bool naCheckbox = false;
    {
        auto celula = boundsDaCelula(indice);
        if (modoVisao_ == ModoVisao::Lista) {
            naCheckbox = e.getPosition().x < celula.getX() + 22;
        } else {
            auto celulaReduzida = celula.reduced(4);
            auto areaImagem = celulaReduzida.withHeight(celulaReduzida.getHeight() - 34);
            constexpr int kDiam = 18;
            juce::Rectangle<int> areaCheck(areaImagem.getRight() - kDiam - 4,
                                            areaImagem.getY() + 4, kDiam + 8, kDiam + 8);
            naCheckbox = areaCheck.contains(e.getPosition());
        }
    }

    pendingDeselect_ = false;
    if (naCheckbox) {
        if (!selecionados_.insert(id).second) selecionados_.erase(id);
        indiceAncoraShift_ = indice;
    } else if (e.mods.isShiftDown() && indiceAncoraShift_ >= 0) {
        int de = juce::jmin(indiceAncoraShift_, indice);
        int ate = juce::jmax(indiceAncoraShift_, indice);
        for (int i = de; i <= ate; ++i) selecionados_.insert(itensFiltrados_[static_cast<size_t>(i)].id);
    } else if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
        if (!selecionados_.insert(id).second) selecionados_.erase(id);
        indiceAncoraShift_ = indice;
    } else if (selecionados_.count(id) && selecionados_.size() > 1) {
        pendingDeselect_ = true;
        pendingDeselectId_ = id;
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

    if (e.getDistanceFromDragStart() >= 8) {
        stopTimer();
        clicouNaTituloDeItemSelecionado_ = false;
    }

    if (e.getDistanceFromDragStart() < 8 || selecionados_.empty()) return;

    pendingDeselect_ = false;

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
    if (lacoAtivo_) {
        lacoAtivo_ = false;
        selecaoAntesDoLaco_.clear();
        repaint();
        return;
    }
    if (pendingDeselect_) {
        pendingDeselect_ = false;
        selecionados_ = {pendingDeselectId_};
        selecionadoId_ = pendingDeselectId_;
        repaint();
        if (aoMudarSelecao) aoMudarSelecao();
        if (aoSelecionar) aoSelecionar(selecionadoId_);
    }
}

bool MosaicoComponent::keyPressed(const juce::KeyPress& tecla) {
    if (tecla.getKeyCode() == 'A' && (tecla.getModifiers().isCommandDown() || tecla.getModifiers().isCtrlDown())) {
        selecionarTodos(); // "tudo que está no filtro atual" — ver nota em selecionarTodos()
        if (aoSelecionar && !selecionadoId_.empty()) aoSelecionar(selecionadoId_);
        return true;
    }

    // 1-9 categorizam a seleção inteira de uma vez (critério 14: 200 itens
    // em menos de 2 minutos). Sem modificador: com Cmd/Ctrl as mesmas teclas
    // são atalhos do sistema, e roubá-las seria pior que não ter o atalho.
    auto c = tecla.getTextCharacter();
    if (c >= '1' && c <= '9' && !tecla.getModifiers().isCommandDown() && !tecla.getModifiers().isCtrlDown()) {
        if (!aoCategorizarPorAtalho) return false;

        std::vector<std::string> alvos(selecionados_.begin(), selecionados_.end());
        // Sem seleção múltipla, vale o item sob o cursor — é o fluxo de
        // quem vai passando item a item classificando.
        if (alvos.empty() && !selecionadoId_.empty()) alvos.push_back(selecionadoId_);
        if (alvos.empty()) return false;

        aoCategorizarPorAtalho(static_cast<int>(c - '1'), std::move(alvos));
        return true;
    }

    if ((tecla.getKeyCode() == 'R' || c == 'r' || c == 'R') &&
        !tecla.getModifiers().isCommandDown() && !tecla.getModifiers().isCtrlDown()) {
        renomearSelecao();
        return true;
    }

    if ((tecla.getKeyCode() == 'C' || c == 'c' || c == 'C') &&
        !tecla.getModifiers().isCommandDown() && !tecla.getModifiers().isCtrlDown()) {
        removerSelecaoDoBackup();
        return true;
    }

    return false;
}

namespace {
void renomearItemNoProjeto(ProjetoAberto& projetoAberto, const std::string& itemId, const juce::String& novoTitulo) {
    auto& db = projetoAberto.projeto().registro();
    const auto& pastaProjeto = projetoAberto.projeto().pasta();

    db.run("UPDATE item SET titulo = ? WHERE id = ?",
           {matriz::db::Value::of(novoTitulo.toStdString()), matriz::db::Value::of(itemId)});

    try {
        auto stmt = db.prepare("SELECT id, caminho_relativo FROM arquivo WHERE item_id = ? ORDER BY eh_master DESC, id LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) {
            std::string arqId = stmt.columnText(0);
            juce::String relPath = stmt.columnText(1);

            juce::File arqAtual = pastaProjeto.getChildFile(relPath);
            if (arqAtual.existsAsFile()) {
                juce::File pastaPai = arqAtual.getParentDirectory();
                juce::String ext = arqAtual.getFileExtension();

                juce::String nomeSanitizado = novoTitulo;
                static const juce::String invalidos = "/\\:*?\"<>|";
                for (auto c : invalidos) nomeSanitizado = nomeSanitizado.replaceCharacter(c, '_');

                juce::File novoArq = pastaPai.getChildFile(nomeSanitizado + ext);
                if (novoArq != arqAtual) {
                    int counter = 1;
                    while (novoArq.existsAsFile()) {
                        novoArq = pastaPai.getChildFile(nomeSanitizado + "_" + juce::String(counter++) + ext);
                    }

                    if (arqAtual.moveFileTo(novoArq)) {
                        juce::String novoRelPath = novoArq.getRelativePathFrom(pastaProjeto);
                        db.run("UPDATE arquivo SET caminho_relativo = ? WHERE id = ?",
                               {matriz::db::Value::of(novoRelPath.toStdString()), matriz::db::Value::of(arqId)});
                    }
                }
            }
        }
    } catch (...) {}
}
} // namespace

void MosaicoComponent::renomearSelecao() {
    std::vector<std::string> alvos(selecionados_.begin(), selecionados_.end());
    if (alvos.empty() && !selecionadoId_.empty()) alvos.push_back(selecionadoId_);
    if (alvos.empty()) return;

    if (alvos.size() == 1) {
        std::string itemId = alvos[0];
        std::string titulo, tipoMidia, codigoAcervo;
        projeto_.obterItemInfo(itemId, titulo, tipoMidia, codigoAcervo);

        auto win = std::make_shared<juce::AlertWindow>(
            "Rename Asset",
            "Enter new title for asset " + juce::String(codigoAcervo) + ":",
            juce::AlertWindow::QuestionIcon);
        win->addTextEditor("titulo", titulo.empty() ? codigoAcervo : titulo, "New Title:");
        win->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey, 0, 0));
        win->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey, 0, 0));

        juce::Component::SafePointer<MosaicoComponent> safeThis(this);
        win->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, win, itemId](int result) {
            if (result == 1 && safeThis) {
                juce::String novoTitulo = win->getTextEditorContents("titulo").trim();
                if (novoTitulo.isNotEmpty()) {
                    renomearItemNoProjeto(safeThis->projeto_, itemId, novoTitulo);
                    safeThis->recarregar();
                    if (safeThis->aoRenomearItem) safeThis->aoRenomearItem();
                }
            }
        }));
    } else {
        auto win = std::make_shared<juce::AlertWindow>(
            "Batch Rename Assets",
            "Enter base title for " + juce::String(static_cast<int>(alvos.size())) + " selected assets:\n(Sequences _001, _002... will be added automatically)",
            juce::AlertWindow::QuestionIcon);
        win->addTextEditor("titulo", "Asset", "Base Title:");
        win->addButton("Batch Rename", 1, juce::KeyPress(juce::KeyPress::returnKey, 0, 0));
        win->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey, 0, 0));

        juce::Component::SafePointer<MosaicoComponent> safeThis(this);
        win->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, win, alvos](int result) {
            if (result == 1 && safeThis) {
                juce::String baseTitulo = win->getTextEditorContents("titulo").trim();
                if (baseTitulo.isNotEmpty()) {
                    for (size_t idx = 0; idx < alvos.size(); ++idx) {
                        juce::String sufixo = "_" + juce::String(static_cast<int>(idx + 1)).paddedLeft('0', 3);
                        juce::String finalTitulo = baseTitulo + sufixo;
                        renomearItemNoProjeto(safeThis->projeto_, alvos[idx], finalTitulo);
                    }
                    safeThis->recarregar();
                    if (safeThis->aoRenomearItem) safeThis->aoRenomearItem();
                }
            }
        }));
    }
}

void MosaicoComponent::removerSelecaoDoBackup() {
    limparSelecao();
    if (aoRemoverDoBackup) aoRemoverDoBackup();
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
    stopTimer();
    clicouNaTituloDeItemSelecionado_ = false;

    int indiceSub = indiceSubpastaNaPosicao(e.getPosition());
    if (indiceSub >= 0) {
        if (aoNavegarParaSubpasta)
            aoNavegarParaSubpasta(subpastas_[static_cast<size_t>(indiceSub)]);
        return;
    }

    int indice = indiceNaPosicao(e.getPosition());
    if (indice < 0) return;

    if (modoVisao_ == ModoVisao::Grade) {
        auto tituloArea = areaTituloDaCelula(indice);
        if (tituloArea.contains(e.getPosition())) {
            iniciarEdicaoInline(indice);
            return;
        }
    }

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

        if (!imagem.isValid()) {
            if (auto arq = projeto->arquivoPrincipal(itemId)) {
                juce::File f(arq->caminhoAbsoluto);
                juce::String ext = f.getFileExtension().toLowerCase().replace(".", "");
                juce::String logoFile;
                if (ext == "rpp") logoFile = "reaper.png";
                else if (ext == "ptx" || ext == "ptf") logoFile = "pro tools.png";
                else if (ext == "logic" || ext == "logicx") logoFile = "logic.png";
                else if (ext == "als") logoFile = "ableton live.png";
                else if (ext == "rxdoc") logoFile = "izotope.png";
                else if (ext == "pd") logoFile = "puredata.png";

                if (logoFile.isNotEmpty()) {
                    juce::File assetsFolder = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("Assets");
                    juce::File logoImgFile = assetsFolder.getChildFile(logoFile);
                    if (logoImgFile.existsAsFile()) {
                        imagem = juce::ImageFileFormat::loadFrom(logoImgFile);
                        if (imagem.isValid()) {
                            temCaminho = true;
                        }
                    }
                }
            }
        }

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
    auto categoria = matriz::ingest::categoriaPorExtensao(juce::String(extensao));
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
    MATRIZ_TRACE("MosaicoComponent::paint");
    const auto& tk = matriz::ui::tema();
    g.fillAll(tk.fundo);

    if (arrastandoArquivo_) {
        g.setColour(tk.acento.withAlpha(0.12f));
        g.fillRect(g.getClipBounds());
        g.setColour(tk.acento);
        g.drawRect(getLocalBounds(), 2);
    }

    if (itensFiltrados_.empty() && subpastas_.empty()) {
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

    for (int si = 0; si < static_cast<int>(subpastas_.size()); ++si) {
        auto bounds = boundsSubpasta(si);
        if (bounds.getBottom() < clip.getY() || bounds.getY() > clip.getBottom()) continue;
        desenharSubpasta(g, bounds, subpastas_[static_cast<size_t>(si)]);
    }

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
            juce::Rectangle<int> bounds = boundsDaCelula(i).reduced(modoVisao_ == ModoVisao::Lista ? 1 : 4);

            juce::Colour corEstado = corDoEstado(item.estado);
            bool selecionado = selecionados_.count(item.id) > 0;
            bool sobHover = i == indiceHover_;

            if (modoVisao_ == ModoVisao::Lista) {
                juce::Colour corCat = corPorCategoria(item.tipoMidia);

                if (selecionado) {
                    g.setColour(tk.acento.withAlpha(0.12f));
                    g.fillRect(bounds);
                } else if (sobHover) {
                    g.setColour(tk.painelAlt.withAlpha(0.6f));
                    g.fillRect(bounds);
                }

                // Category color bar (left edge)
                g.setColour(corCat);
                g.fillRoundedRectangle(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY() + 2),
                                       4.0f, static_cast<float>(bounds.getHeight() - 4), 2.0f);

                g.setColour(tk.borda.withAlpha(0.2f));
                g.fillRect(bounds.getX(), bounds.getBottom() - 1, bounds.getWidth(), 1);

                auto linha = bounds.reduced(0, 2);
                linha.removeFromLeft(10);

                // Checkbox
                constexpr int kCheckDiam = 16;
                auto areaCheck = linha.removeFromLeft(kCheckDiam + 6);
                float cx = static_cast<float>(areaCheck.getCentreX() - kCheckDiam / 2);
                float cy = static_cast<float>(areaCheck.getCentreY() - kCheckDiam / 2);
                juce::Rectangle<float> checkRect(cx, cy, static_cast<float>(kCheckDiam), static_cast<float>(kCheckDiam));
                if (selecionado) {
                    g.setColour(tk.acento);
                    g.fillRoundedRectangle(checkRect, 3.0f);
                    g.setColour(tk.textoSobreAcento);
                    juce::Path tique;
                    tique.startNewSubPath(checkRect.getX() + kCheckDiam * 0.24f, checkRect.getCentreY());
                    tique.lineTo(checkRect.getX() + kCheckDiam * 0.42f, checkRect.getY() + kCheckDiam * 0.70f);
                    tique.lineTo(checkRect.getX() + kCheckDiam * 0.78f, checkRect.getY() + kCheckDiam * 0.30f);
                    g.strokePath(tique, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
                } else {
                    g.setColour(sobHover ? tk.textoTerciario : tk.borda);
                    g.drawRoundedRectangle(checkRect, 3.0f, 1.2f);
                }

                // Thumbnail (square)
                int thumbSize = bounds.getHeight() - 6;
                auto areaThumb = linha.removeFromLeft(thumbSize + 4);
                auto thumbRect = areaThumb.withSizeKeepingCentre(thumbSize, thumbSize);
                const juce::Image* img = miniaturaCache(item.id);
                if (img) {
                    g.setColour(tk.painelAlt);
                    g.fillRoundedRectangle(thumbRect.toFloat(), 3.0f);
                    g.drawImage(*img, thumbRect.toFloat(), juce::RectanglePlacement::centred);
                } else {
                    pedirCarregamentoMiniatura(item.id);
                    g.setColour(corCat.withAlpha(0.15f));
                    g.fillRoundedRectangle(thumbRect.toFloat(), 3.0f);
                    desenharPlaceholderCategoria(g, thumbRect, item.extensaoArquivo);
                }
                g.setColour(corCat);
                g.drawRoundedRectangle(thumbRect.toFloat(), 3.0f, 1.0f);
                linha.removeFromLeft(8);

                // Extension badge (right side)
                juce::String ext = juce::String(item.extensaoArquivo).toUpperCase();
                int extW = juce::jmax(36, static_cast<int>(ext.length()) * 8 + 12);
                auto areaExt = linha.removeFromRight(extW + 8);
                g.setColour(corCat.withAlpha(0.18f));
                auto extBadge = areaExt.withSizeKeepingCentre(extW, 20);
                g.fillRoundedRectangle(extBadge.toFloat(), 10.0f);
                g.setColour(corCat);
                g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
                g.drawText(ext, extBadge, juce::Justification::centred);

                auto areaTexto = linha.reduced(4, 0);
                int metadeAltura = areaTexto.getHeight() / 2;

                juce::String nomeExibicaoList = item.titulo.empty() ? juce::String(item.nomeOriginalArquivo) : juce::String(item.titulo);
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
                g.drawText(nomeExibicaoList, areaTexto.removeFromTop(metadeAltura),
                           juce::Justification::centredLeft, true);

                juce::String info2 = item.extensaoArquivo.empty() ? juce::String("FILE") : juce::String(item.extensaoArquivo).toUpperCase();
                if (!item.pastaNome.empty()) info2 += "  |  " + juce::String(item.pastaNome);
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(11.0f)));
                g.drawText(info2, areaTexto, juce::Justification::centredLeft, true);

                continue;
            }

            juce::Colour corCat = corPorCategoria(item.tipoMidia);

            g.setColour(tk.painel);
            g.fillRoundedRectangle(bounds.toFloat(), tk.raioMedio);

            int textAreaH = juce::jmin(40, bounds.getHeight() / 4 + 10);
            juce::Rectangle<int> areaImagem = bounds.withHeight(bounds.getHeight() - textAreaH);
            const juce::Image* img = miniaturaCache(item.id);
            if (img) {
                g.saveState();
                g.reduceClipRegion(areaImagem.reduced(1));
                g.drawImage(*img, areaImagem.toFloat(), juce::RectanglePlacement::centred);
                g.restoreState();
            } else {
                pedirCarregamentoMiniatura(item.id);
                g.setColour(corCat.withAlpha(0.10f));
                g.fillRoundedRectangle(areaImagem.toFloat(), tk.raioPequeno);
                desenharPlaceholderCategoria(g, areaImagem, item.extensaoArquivo);
            }

            // Extension badge (top-left)
            juce::String ext = juce::String(item.extensaoArquivo).toUpperCase();
            if (ext.isNotEmpty()) {
                int badgeW = juce::jmax(28, static_cast<int>(ext.length()) * 7 + 10);
                juce::Rectangle<int> badge(areaImagem.getX() + 4, areaImagem.getY() + 4, badgeW, 16);
                g.setColour(juce::Colour(0xcc000000));
                g.fillRoundedRectangle(badge.toFloat(), 8.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
                g.drawText(ext, badge, juce::Justification::centred);
            }

            // Duration badge (bottom-right of thumbnail like 04:35, 12:18)
            juce::String durText = (item.extensaoArquivo == "mp4" || item.extensaoArquivo == "mov" || item.extensaoArquivo == "avi" || item.extensaoArquivo == "mkv") ? "12:18"
                                 : (item.extensaoArquivo == "wav" || item.extensaoArquivo == "mp3" || item.extensaoArquivo == "flac") ? "04:35" : "";
            if (durText.isNotEmpty()) {
                juce::Rectangle<int> durBadge(areaImagem.getRight() - 44, areaImagem.getBottom() - 20, 40, 16);
                g.setColour(juce::Colour(0xcc000000));
                g.fillRoundedRectangle(durBadge.toFloat(), 4.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
                g.drawText(durText, durBadge, juce::Justification::centred);
            }

            // Card border: thick colored by media type, thicker + accent when selected
            if (selecionado) {
                g.setColour(tk.acento);
                g.drawRoundedRectangle(bounds.toFloat(), tk.raioMedio, 3.5f);
            } else {
                g.setColour(corCat);
                g.drawRoundedRectangle(bounds.toFloat(), tk.raioMedio, 2.5f);
            }

            if (item.sincronizado) {
                g.setColour(tk.haloSincronizado);
                g.drawRoundedRectangle(bounds.reduced(3).toFloat(), tk.raioMedio, 1.5f);
            }

            // Checkmark on selected items
            if (selecionado) {
                constexpr int kDiametroMarca = 20;
                juce::Rectangle<float> marca(static_cast<float>(areaImagem.getRight() - kDiametroMarca - 5),
                                              static_cast<float>(areaImagem.getY() + 5),
                                              static_cast<float>(kDiametroMarca),
                                              static_cast<float>(kDiametroMarca));
                g.setColour(tk.acento);
                g.fillRoundedRectangle(marca, 4.0f);
                g.setColour(tk.textoSobreAcento);
                juce::Path tique;
                tique.startNewSubPath(marca.getX() + kDiametroMarca * 0.24f, marca.getCentreY());
                tique.lineTo(marca.getX() + kDiametroMarca * 0.42f, marca.getY() + kDiametroMarca * 0.70f);
                tique.lineTo(marca.getX() + kDiametroMarca * 0.78f, marca.getY() + kDiametroMarca * 0.30f);
                g.strokePath(tique, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }

            juce::String nomeExibicaoGrid = item.titulo.empty() ? juce::String(item.nomeOriginalArquivo) : juce::String(item.titulo);
            auto areaTexto = bounds.withTop(areaImagem.getBottom() + 2).reduced(6, 0);
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(nomeExibicaoGrid, areaTexto.removeFromTop(textAreaH / 2),
                       juce::Justification::centredLeft, true);

            // Subtitle: file type + folder
            juce::String info2 = item.extensaoArquivo.empty() ? "FILE" : juce::String(item.extensaoArquivo).toUpperCase();
            if (!item.pastaNome.empty()) info2 += "  |  " + juce::String(item.pastaNome);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(9.5f)));
            g.drawText(info2, areaTexto, juce::Justification::centredLeft, true);
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

juce::Rectangle<int> MosaicoComponent::areaTituloDaCelula(int indice) const {
    if (modoVisao_ != ModoVisao::Grade) return {};
    auto bounds = boundsDaCelula(indice).reduced(4);
    int textAreaH = juce::jmin(40, bounds.getHeight() / 4 + 10);
    auto areaImagem = bounds.withHeight(bounds.getHeight() - textAreaH);
    return bounds.withTop(areaImagem.getBottom() + 2).reduced(4, 0).withHeight(textAreaH / 2);
}

void MosaicoComponent::timerCallback() {
    stopTimer();
    if (!clicouNaTituloDeItemSelecionado_ || indiceEditando_ < 0) return;
    iniciarEdicaoInline(indiceEditando_);
}

void MosaicoComponent::iniciarEdicaoInline(int indice) {
    if (indice < 0 || indice >= static_cast<int>(itensFiltrados_.size())) return;
    auto area = areaTituloDaCelula(indice);
    if (area.isEmpty()) return;

    const auto& item = itensFiltrados_[static_cast<size_t>(indice)];
    juce::String textoAtual = item.titulo.empty() ? juce::String(item.nomeOriginalArquivo) : juce::String(item.titulo);

    editorInline_ = std::make_unique<juce::TextEditor>();
    editorInline_->setBounds(area);
    editorInline_->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    editorInline_->setText(textoAtual, false);
    editorInline_->selectAll();
    editorInline_->setColour(juce::TextEditor::backgroundColourId, tema().painel);
    editorInline_->setColour(juce::TextEditor::textColourId, tema().textoPrimario);
    editorInline_->setColour(juce::TextEditor::outlineColourId, tema().acento);
    editorInline_->setColour(juce::TextEditor::focusedOutlineColourId, tema().acento);

    editorInline_->onReturnKey = [this] { confirmarEdicaoInline(); };
    editorInline_->onEscapeKey = [this] { cancelarEdicaoInline(); };
    editorInline_->onFocusLost = [this] { confirmarEdicaoInline(); };

    addAndMakeVisible(*editorInline_);
    editorInline_->grabKeyboardFocus();
    indiceEditando_ = indice;
}

void MosaicoComponent::confirmarEdicaoInline() {
    if (!editorInline_ || indiceEditando_ < 0) return;
    auto novoTitulo = editorInline_->getText().trim();
    int idx = indiceEditando_;
    indiceEditando_ = -1;
    removeChildComponent(editorInline_.get());
    editorInline_.reset();

    if (novoTitulo.isEmpty() || idx >= static_cast<int>(itensFiltrados_.size())) return;
    const auto& item = itensFiltrados_[static_cast<size_t>(idx)];
    juce::String tituloAtual = item.titulo.empty() ? juce::String(item.nomeOriginalArquivo) : juce::String(item.titulo);
    if (novoTitulo == tituloAtual) return;

    projeto_.renomearItens({item.id}, novoTitulo.toStdString());
    recarregar();
}

void MosaicoComponent::cancelarEdicaoInline() {
    if (!editorInline_) return;
    indiceEditando_ = -1;
    editorInline_->onFocusLost = nullptr;
    removeChildComponent(editorInline_.get());
    editorInline_.reset();
}

} // namespace matriz::ui
