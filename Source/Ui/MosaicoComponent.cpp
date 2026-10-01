#include "MosaicoComponent.h"
#include "../Diag/Watchdog.h"
#include "../Ingest/LeituraTecnica.h"

#include "../Ficha/FichaI18n.h"
#include "../I18n/Strings.h"
#include "Tokens.h"
#include "OriginalSourceMedium.h"
#include "TraducaoContent.h"
#include "ProgressoGlobal.h"
#include "SendToPrintDialog.h"

#include <algorithm>
#include <iterator>
#include <map>

namespace matriz::ui {

MosaicoComponent::MosaicoComponent(ProjetoAberto& projeto) : projeto_(projeto) {
    setWantsKeyboardFocus(true);
    notificadorSelecao_.aoDisparar = [this] {
        if (aoMudarSelecao) aoMudarSelecao();
        if (aoSelecionar && !selecionadoId_.empty()) aoSelecionar(selecionadoId_);
    };
    EventBus::obterInstancia().registrarListener(this);
}

MosaicoComponent::~MosaicoComponent() {
    EventBus::obterInstancia().removerListener(this);
    poolSnapshot_.removeAllJobs(true, 2000);
    poolMiniaturas_.removeAllJobs(true, 2000);
}

void MosaicoComponent::aoItemAlterado(const EventoItemAlterado& e) {
    // Desregistrar no destrutor não basta: um evento recebido um instante
    // antes já postou esta mensagem, que só é entregue depois.
    juce::Component::SafePointer<MosaicoComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis, e]() {
        if (safeThis == nullptr) return;
        // Escondida (outra aba na frente): só anota "precisa atualizar"; a grade volta
        // a aparecer e aplica tudo de uma vez (visibilityChanged / primeiro paint).
        if (safeThis->escondido()) {
            safeThis->guardarEventoPendente(e);
            return;
        }
        safeThis->aplicarEventosPendentes();  // anteriores primeiro
        safeThis->aplicarEvento(e);
    });
}

void MosaicoComponent::guardarEventoPendente(const EventoItemAlterado& e) {
    if (e.itemId.empty() && e.itemIds.empty()) {
        tiposAmplosPendentes_.insert(e.tipoAlteracao);
        return;
    }
    auto& ids = idsPendentesPorTipo_[e.tipoAlteracao];
    if (!e.itemIds.empty()) ids.insert(e.itemIds.begin(), e.itemIds.end());
    else ids.insert(e.itemId);
    // Escondida por muito tempo com lotes enormes: uma recarga completa sai mais barata
    // (e mais simples) que dezenas de milhares de atualizações por item.
    constexpr size_t kMaxIdsPendentes = 20000;
    size_t total = 0;
    for (const auto& par : idsPendentesPorTipo_) total += par.second.size();
    if (total > kMaxIdsPendentes) {
        tiposAmplosPendentes_.insert("recarregar_tudo");
        idsPendentesPorTipo_.clear();
    }
}

void MosaicoComponent::agendarAplicarPendentes() {
    if (aplicacaoPendenteAgendada_ || !temEventosPendentes()) return;
    aplicacaoPendenteAgendada_ = true;
    juce::Component::SafePointer<MosaicoComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr) return;
        safeThis->aplicacaoPendenteAgendada_ = false;
        if (!safeThis->escondido()) safeThis->aplicarEventosPendentes();
    });
}

void MosaicoComponent::visibilityChanged() {
    if (!escondido()) agendarAplicarPendentes();
}

void MosaicoComponent::aplicarEventosPendentes() {
    if (!temEventosPendentes()) return;
    auto amplos = std::move(tiposAmplosPendentes_);
    auto porTipo = std::move(idsPendentesPorTipo_);
    tiposAmplosPendentes_.clear();
    idsPendentesPorTipo_.clear();

    const bool recargaCompleta = amplos.count("recarregar_tudo") > 0;
    if (recargaCompleta) {
        amplos.erase("recarregar_tudo");
        aplicarEvento({std::string(), "recarregar_tudo", {}});
    }
    for (const auto& tipo : amplos) aplicarEvento({std::string(), tipo, {}});
    for (const auto& [tipo, ids] : porTipo) {
        // A recarga completa já relê do banco o que os eventos "genéricos" (título, tipo,
        // metadado...) releriam item a item; marcações/E/nest têm tratamento próprio.
        const bool generico = tipo != "nest" && tipo != "marcacao" && tipo != "publicacao" && tipo != "marcado_revisado";
        if (recargaCompleta && generico) continue;
        if (ids.size() == 1) aplicarEvento({*ids.begin(), tipo, {}});
        else aplicarEvento({std::string(), tipo, std::vector<std::string>(ids.begin(), ids.end())});
    }
}

void MosaicoComponent::aplicarEvento(const EventoItemAlterado& e) {
    // Evento de LOTE: itemId vazio e os ids em e.itemIds (um evento por operação em lote).
    const bool lote = !e.itemIds.empty();

    // Mudança em muitos itens de uma vez (ex. Duplicates > Validate All):
    // uma recarga completa em background, não N atualizações por item.
    if (e.tipoAlteracao == "recarregar_tudo") {
        recarregar();
        return;
    }
    // NEST criado / desfeito / capa trocada: relê só o mapa de nests (uma consulta) e refiltra.
    if (e.tipoAlteracao == "nest") {
        // Um snapshot completo que começou ANTES deste nest (leu o estado antigo) não pode sobrescrever o
        // resultado quando chegar: descarta-o e pede um novo logo depois (mesmo padrão de atualizarItemEmMemoria).
        if (snapshotPendente_) recarregarAoTerminarSnapshot_ = true;
        ++geracaoSnapshot_;
        snapshotPendente_ = false;
        const auto nests = ProjetoAberto::mapaDeNests(projeto_.projeto().registro());
        auto aplicar = [&](std::vector<ItemResumo>& lista) {
            for (auto& item : lista) {
                auto it = nests.find(item.id);
                if (it == nests.end()) { item.nestId.clear(); item.nestCapaId.clear(); item.nestTotal = 0; item.nestCapa = false; continue; }
                item.nestId = it->second.nestId;
                item.nestCapaId = it->second.capaId;
                item.nestTotal = it->second.total;
                item.nestCapa = (item.id == it->second.capaId);
            }
        };
        aplicar(itensTodos_);
        aplicarFiltrosEOrdenacao();
        return;
    }
    if (e.tipoAlteracao == "marcacao" || e.tipoAlteracao == "publicacao") {
        auto atualizar = [this](const std::string& id) {
            if (id.empty()) return;
            bool marcadoH = projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Html, id);
            bool marcadoK = projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Zip, id);
            bool marcadoP = projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Print, id);
            // item 7: watermark (W) dispara o mesmo tipo de evento "marcacao"
            // que H/K/P, então precisa do mesmo tratamento aqui — sem isso
            // ficava sem sync ao vivo entre a janela de Preview e a grade.
            bool marcadoW = projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Watermark, id);
            for (ItemResumo* item : {itemEmTodos(id), itemEmFiltrados(id)}) {
                if (item == nullptr) continue;
                item->marcadoPublicacao = marcadoH;
                item->marcadoZip = marcadoK;
                item->marcadoPrint = marcadoP;
                item->marcadoWatermark = marcadoW;
            }
        };
        if (lote) for (const auto& id : e.itemIds) atualizar(id);
        else atualizar(e.itemId);
        repaint();
        return;
    }
    // Tecla E: só a flag muda — nada de atualizarItemEmMemoria (que dá
    // stat no arquivo, marca metadadosEditados e refiltra por item).
    if (e.tipoAlteracao == "marcado_revisado") {
        // itemId vazio (sem lote) = limparTodosMarcadosRevisado(): todos desligados.
        if (e.itemId.empty() && !lote) {
            for (auto& item : itensTodos_) item.marcadoRevisado = false;
            for (auto& item : itensFiltrados_) item.marcadoRevisado = false;
        } else {
            auto atualizar = [this](const std::string& id) {
                const bool valor = projeto_.itemMarcadoRevisado(id);
                if (auto* item = itemEmTodos(id)) item->marcadoRevisado = valor;
                if (auto* item = itemEmFiltrados(id)) item->marcadoRevisado = valor;
            };
            if (lote) for (const auto& id : e.itemIds) atualizar(id);
            else atualizar(e.itemId);
        }
        repaint();
        if (ocultarEditados_) agendarRefiltroCoalescido();
        // Snapshot em voo foi lido ANTES do E: ao chegar, traria a flag
        // antiga por cima. Pede um recarregar logo depois dele.
        if (snapshotPendente_) recarregarAoTerminarSnapshot_ = true;
        return;
    }
    // Lote (renomear, tipo...): cada item atualizado em memória como no evento por item,
    // mas o refiltro/reordenação da lista inteira roda UMA vez no fim.
    if (lote) {
        iniciarLoteAtualizacao();
        for (const auto& id : e.itemIds) atualizarItemEmMemoria(id);
        finalizarLoteAtualizacao();
        return;
    }
    // EDIT METADATA != REMOVE FROM THIS LIST (correção METADATA): um
    // evento de UM item (tags, título, etc.) só precisa atualizar ESSE
    // item em memória — chamar recarregar() aqui refazia o snapshot
    // inteiro do catálogo por cima de qualquer edição de campo (cada
    // ProjetoAberto::definirTags/salvarMetadado dispara este evento),
    // disputando com o filtro "Selected"/"Hide Unselected" corrente e
    // arriscando derrubar o item da lista no meio do caminho. Só um
    // evento sem itemId (mudança ampla, não de um item específico)
    // ainda pede o recarregar completo.
    if (e.itemId.empty()) {
        recarregar();
    } else {
        atualizarItemEmMemoria(e.itemId);
    }
}

ItemResumo* MosaicoComponent::itemEmTodos(const std::string& itemId) {
    if (itemId.empty()) return nullptr;
    if (!indiceTodosValido_) {
        indiceTodos_.clear();
        indiceTodos_.reserve(itensTodos_.size());
        for (size_t i = 0; i < itensTodos_.size(); ++i) indiceTodos_.emplace(itensTodos_[i].id, i);  // emplace: o 1º vence, como o find_if
        indiceTodosValido_ = true;
    }
    auto it = indiceTodos_.find(itemId);
    if (it != indiceTodos_.end()) {
        if (it->second < itensTodos_.size() && itensTodos_[it->second].id == itemId) return &itensTodos_[it->second];
        indiceTodosValido_ = false;  // mapa velho (lista mudou sem avisar): refaz na próxima e cai na busca linear
        auto lin = std::find_if(itensTodos_.begin(), itensTodos_.end(), [&](const ItemResumo& r) { return r.id == itemId; });
        return lin == itensTodos_.end() ? nullptr : &*lin;
    }
    return nullptr;
}

ItemResumo* MosaicoComponent::itemEmFiltrados(const std::string& itemId) {
    const int i = indiceFiltradoDe(itemId);
    return i >= 0 ? &itensFiltrados_[static_cast<size_t>(i)] : nullptr;
}

int MosaicoComponent::indiceFiltradoDe(const std::string& itemId) {
    if (itemId.empty()) return -1;
    if (!indiceFiltradosValido_) {
        indiceFiltrados_.clear();
        indiceFiltrados_.reserve(itensFiltrados_.size());
        for (size_t i = 0; i < itensFiltrados_.size(); ++i) indiceFiltrados_.emplace(itensFiltrados_[i].id, i);
        indiceFiltradosValido_ = true;
    }
    auto it = indiceFiltrados_.find(itemId);
    if (it == indiceFiltrados_.end()) return -1;
    if (it->second < itensFiltrados_.size() && itensFiltrados_[it->second].id == itemId) return static_cast<int>(it->second);
    indiceFiltradosValido_ = false;  // mapa velho (lista mudou sem avisar): refaz na próxima e cai na busca linear
    for (size_t i = 0; i < itensFiltrados_.size(); ++i)
        if (itensFiltrados_[i].id == itemId) return static_cast<int>(i);
    return -1;
}

void MosaicoComponent::agendarRefiltroCoalescido() {
    // Uma rajada de E (N eventos, um por item) vira um refiltro só.
    if (refiltroAgendado_) return;
    refiltroAgendado_ = true;
    juce::Component::SafePointer<MosaicoComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr) return;
        safeThis->refiltroAgendado_ = false;
        safeThis->aplicarFiltrosEOrdenacao();
    });
}

void MosaicoComponent::recarregar() {
    MATRIZ_TRACE("MosaicoComponent::recarregar");
    if (editorInline_) cancelarEdicaoInline();
    // Um snapshot por vez. Sem isto, um lote de 5.000 arquivos enfileirava um
    // job a cada 700 ms enquanto o anterior ainda rodava, e cada job carrega
    // um vetor de 5.000 ItemResumo — dezenas de cópias vivas ao mesmo tempo.
    if (snapshotPendente_) {
        recarregarAoTerminarSnapshot_ = true;
        return;
    }
    recarregarAoTerminarSnapshot_ = false;
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
    bool isQuarentena = modoQuarentena_;

    ProgressoGlobal::obterInstancia().iniciarTarefa("catalog_assets", "Loading Catalog", 100, nullptr, "Reading catalog index...",
                                                   /*temModalProprio*/ false, /*somenteBarra*/ true);

    poolSnapshot_.addJob([safeThis, projeto, geracao, isQuarentena]() {
        std::vector<ItemResumo> itens;
        std::vector<TextosCelula> textos;
        try {
            itens = isQuarentena ? projeto->listarItensEmQuarentena() : projeto->listarItens();
            // Textos de exibição aqui, no job (não no paint, por célula, a cada quadro).
            textos.reserve(itens.size());
            for (const auto& item : itens) textos.push_back(calcularTextos(item));
        } catch (const std::exception&) {
            ProgressoGlobal::obterInstancia().concluirTarefa("catalog_assets", "");
            return;  // projeto fechado no meio: nada a entregar
        }

        juce::MessageManager::callAsync([safeThis, geracao, itens = std::move(itens), textos = std::move(textos)]() mutable {
            if (!safeThis) {
                ProgressoGlobal::obterInstancia().concluirTarefa("catalog_assets", "");
                return;
            }
            auto* self = safeThis.getComponent();
            if (geracao != self->geracaoSnapshot_) {
                // snapshot superado (ex.: atualizarItemEmMemoria() bumped a
                // geração enquanto este job estava em voo) — a tarefa global
                // precisa fechar mesmo assim, senão o modal "Loading Catalog"
                // fica preso em 0% pra sempre.
                ProgressoGlobal::obterInstancia().concluirTarefa("catalog_assets", "");
                // Quem pediu ESTE snapshot (ex.: "Show Recently Ingested"
                // esperando o lote promovido do Intake) nunca seria avisado —
                // o resultado foi descartado e aoMudarConteudoVisivel não
                // dispara para uma geração superada. Se atualizarItemEmMemoria
                // pediu um recarregar de reposição, honra agora.
                if (self->recarregarAoTerminarSnapshot_) {
                    self->recarregarAoTerminarSnapshot_ = false;
                    self->recarregar();
                }
                return;
            }
            MATRIZ_TRACE("MosaicoComponent::aplicarSnapshot");
            self->snapshotPendente_ = false;
            self->itensFiltrados_.limpar();  // os índices apontavam pra lista antiga
            self->itensTodos_ = std::move(itens);
            self->textosTodos_ = std::move(textos);
            self->invalidarIndiceTodos();
            ++self->versaoSnapshot_;
            self->aplicarFiltrosEOrdenacao();
            if (self->aoMudarConteudoVisivel) self->aoMudarConteudoVisivel();
            ProgressoGlobal::obterInstancia().concluirTarefa("catalog_assets", juce::String(self->itensTodos_.size()) + " items loaded");
            // A reload was requested while this snapshot was in flight — honour it now.
            if (self->recarregarAoTerminarSnapshot_) {
                self->recarregarAoTerminarSnapshot_ = false;
                self->recarregar();
            }
        });
    });
}

void MosaicoComponent::atualizarItemEmMemoria(const std::string& itemId) {
    if (itemId.empty()) return;

    // Um recarregar() em voo (ex.: um filtro esperando o catálogo completo)
    // fica órfão quando a geração muda aqui: sua resposta chega descartada
    // (branch de geração divergente em recarregar()). Sem isto, quem estava
    // esperando aquele snapshot nunca é avisado do desfecho.
    if (snapshotPendente_) recarregarAoTerminarSnapshot_ = true;
    ++geracaoSnapshot_;
    snapshotPendente_ = false;

    ItemResumo* it = itemEmTodos(itemId);
    if (it == nullptr) return;

    auto tit = projeto_.lerMetadado(itemId, "titulo");
    if (tit.has_value()) {
        it->titulo = *tit;
        if (!tit->empty()) it->nomeOriginalArquivo = *tit;
    }
    // Só o EVENT DATE (item.ano, o que a ficha mostra).
    it->ano = projeto_.anoDoEventDate(itemId);
    auto ct = projeto_.lerMetadado(itemId, "content_type");
    it->contentType = ct.has_value() && !ct->empty() ? ct : std::nullopt;
    auto col = projeto_.lerMetadado(itemId, "collection_type");
    it->collectionType = col.has_value() && !col->empty() ? col : std::nullopt;
    auto subj = projeto_.lerMetadado(itemId, "dc_subject");
    it->subject = subj.has_value() && !subj->empty() ? subj : std::nullopt;
    it->marcadoPublicacao = projeto_.itemMarcadoPublicacao(itemId);
    it->tags = projeto_.lerTags(itemId);
    it->metadadosEditados = true;
    // D3/item 7: marcado_revisado (atalho E) e watermark (W) não tinham
    // sync ao vivo entre a janela de Preview e a grade — H/K/P já eram
    // tratados à parte no branco "marcacao"/"publicacao" acima em
    // aoItemAlterado, mas E e W caíam só neste caminho genérico, que nunca
    // relia essas duas flags do banco.
    if (auto resumo = projeto_.obterItemResumo(itemId)) {
        it->marcadoRevisado = resumo->marcadoRevisado;
        it->marcadoWatermark = resumo->marcadoWatermark;
    }

    // Nome/ano/etc. mudaram: refaz os textos de exibição deste item.
    textosTodos_[static_cast<size_t>(it - itensTodos_.data())] = calcularTextos(*it);

    if (loteAtualizacaoProfundidade_ > 0) refiltroAdiado_ = true;
    else aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::finalizarLoteAtualizacao() {
    if (loteAtualizacaoProfundidade_ > 0) --loteAtualizacaoProfundidade_;
    if (loteAtualizacaoProfundidade_ == 0 && refiltroAdiado_) {
        refiltroAdiado_ = false;
        aplicarFiltrosEOrdenacao();
    }
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
    itensFiltrados_.limpar();  // os índices apontavam pra lista antiga
    itensTodos_ = modoQuarentena_ ? projeto_.listarItensEmQuarentena() : projeto_.listarItens();
    textosTodos_.clear();
    textosTodos_.reserve(itensTodos_.size());
    for (const auto& item : itensTodos_) textosTodos_.push_back(calcularTextos(item));
    invalidarIndiceTodos();
    ++versaoSnapshot_;
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

// Fallback em memória por termo (item 6): usado quando o item não está no
// conjunto vindo do FTS5 (buscaResultado_) — checa campos básicos já
// carregados no ItemResumo. Com múltiplos chips, cada termo passa por aqui
// separadamente e o item só sobrevive se bater em TODOS (E lógico).
bool itemBateComTermoEmMemoria(const ItemResumo& item, const juce::String& termo) {
    juce::String q = termo.toLowerCase().trim();
    if (q.isEmpty()) return true;
    if (juce::String(item.titulo).toLowerCase().contains(q) ||
        juce::String(item.codigoAcervo).toLowerCase().contains(q) ||
        juce::String(item.nomeOriginalArquivo).toLowerCase().contains(q) ||
        juce::String(item.pastaNome).toLowerCase().contains(q) ||
        juce::String(item.isrc).toLowerCase().contains(q)) {
        return true;
    }
    for (const auto& tg : item.tags) {
        if (juce::String(tg).toLowerCase().contains(q.trimCharactersAtStart("#"))) return true;
    }
    return false;
}

// Interseção (E lógico) dos conjuntos de ids vindos de cada termo — um item
// só passa se aparecer em TODOS os conjuntos.
std::set<std::string> intersectarConjuntos(const std::vector<std::set<std::string>>& conjuntos) {
    if (conjuntos.empty()) return {};
    std::set<std::string> resultado = conjuntos.front();
    for (size_t i = 1; i < conjuntos.size() && !resultado.empty(); ++i) {
        std::set<std::string> intersecao;
        std::set_intersection(resultado.begin(), resultado.end(),
                               conjuntos[i].begin(), conjuntos[i].end(),
                               std::inserter(intersecao, intersecao.begin()));
        resultado = std::move(intersecao);
    }
    return resultado;
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

void MosaicoComponent::definirFiltroPeriodoData(const juce::String& dataDe, const juce::String& dataAte) {
    filtroDataDe_ = dataDe.trim();
    filtroDataAte_ = dataAte.trim();
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::limparFiltroPeriodoData() {
    filtroDataDe_.clear();
    filtroDataAte_.clear();
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirModoAgrupamento(ModoAgrupamento modo) {
    if (modoAgrupamento_ == modo) return;
    modoAgrupamento_ = modo;
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::alternarFiltroDisponibilidade(FiltroDisponibilidade f) {
    if (filtroDisponibilidade_ == f) return;
    filtroDisponibilidade_ = f;
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::limparFiltros() {
    filtrosTipoMidia_.clear();
    filtrosEstado_.clear();
    filtrosExtensao_.clear();
    filtrosOrigem_.clear();
    filtrosContentType_.clear();
    filtrosCollectionType_.clear();
    filtroDisponibilidade_ = FiltroDisponibilidade::All;
    filtroFaixaAno_.reset();
    buscaTermos_.clear();
    buscaResultado_.reset();
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirBusca(const juce::String& texto) {
    // Compat: chamada por quem só conhece um termo único (backup file
    // selector, coleção salva/inteligente, selftest) — substitui TODOS os
    // chips ativos por, no máximo, um.
    buscaTermos_.clear();
    juce::String t = texto.trim();
    if (t.isNotEmpty()) buscaTermos_.add(t);
    recomputarBuscaResultado();
}

void MosaicoComponent::adicionarTermoBusca(const juce::String& texto) {
    juce::String t = texto.trim();
    if (t.isEmpty() || buscaTermos_.contains(t)) return;
    buscaTermos_.add(t);
    recomputarBuscaResultado();
}

void MosaicoComponent::definirEscopoBusca(ProjetoAberto::EscopoBusca escopo) {
    if (escopo == escopoBusca_) return;
    escopoBusca_ = escopo;
    recomputarBuscaResultado();
}

void MosaicoComponent::removerTermoBusca(int indice) {
    if (indice < 0 || indice >= buscaTermos_.size()) return;
    buscaTermos_.remove(indice);
    recomputarBuscaResultado();
}

void MosaicoComponent::recomputarBuscaResultado() {
    // Consulta o banco (código/título/campo de ficha/assunto — ver
    // ProjetoAberto::buscarItens) em vez de um substring check em memória:
    // é o único jeito de a busca alcançar valor de ficha e assunto, que não
    // vivem em ItemResumo. Sem termos = nullopt = sem filtro de busca,
    // nunca "nenhum resultado".
    //
    // item 6: cada termo (chip) tem seu próprio conjunto de ids; o
    // resultado final é a INTERSEÇÃO de todos — E lógico entre chips.
    //
    // "1978-1985" é reconhecido como faixa de ano (item 4.3) e resolvido
    // por itensPorFaixaAno em vez de LIKE textual — um LIKE por "1978-1985"
    // não acharia nada, já que o valor gravado em cada item é só "1981".
    const int geracao = ++geracaoBusca_; // invalida qualquer busca em voo

    if (buscaTermos_.isEmpty()) {
        buscaResultado_ = std::nullopt;
        aplicarFiltrosEOrdenacao();
        return;
    }

    auto resultadosParciais = std::make_shared<std::vector<std::set<std::string>>>();
    std::vector<juce::String> termosPendentes;
    for (auto& t : buscaTermos_) {
        if (auto faixa = parsearFaixaAno(t))
            resultadosParciais->push_back(projeto_.itensPorFaixaAno(faixa->first, faixa->second));
        else
            termosPendentes.push_back(t);
    }

    if (termosPendentes.empty()) {
        // Só faixas de ano — nada assíncrono, intersecta na hora.
        buscaResultado_ = intersectarConjuntos(*resultadosParciais);
        aplicarFiltrosEOrdenacao();
        return;
    }

    // Fix de performance (item 8): buscarItens() faz FTS5 + LIKE em várias
    // tabelas — rodar na message thread travava a janela a cada tecla. Vai
    // pro pool de miniaturas (já existe, leve o bastante pra não competir
    // com o snapshot) e a UI só aplica quando TODOS os termos pendentes
    // responderam, com a mesma geração usada em recarregar() pra descartar
    // resposta atrasada de uma busca já superada por uma mais nova.
    auto restantes = std::make_shared<int>(static_cast<int>(termosPendentes.size()));
    juce::Component::SafePointer<MosaicoComponent> safeThis(this);
    ProjetoAberto* projeto = &projeto_;

    for (auto& termo : termosPendentes) {
        const auto escopo = escopoBusca_;
        poolMiniaturas_.addJob([safeThis, projeto, geracao, termo, resultadosParciais, restantes, escopo]() {
            matriz::diag::LogOperacao logOp("buscaTermo:" + termo.toStdString());
            std::set<std::string> resultado;
            try {
                resultado = projeto->buscarItens(termo, escopo);
            } catch (const std::exception&) {
                // projeto fechado no meio: entrega vazio, só pra não travar
                // os outros termos esperando pra sempre.
            }

            juce::MessageManager::callAsync([safeThis, geracao, resultado = std::move(resultado), resultadosParciais, restantes]() mutable {
                if (!safeThis) return;
                auto* self = safeThis.getComponent();
                if (geracao != self->geracaoBusca_) return; // busca superada por uma mais nova
                // callAsync sempre serializado na message thread — sem
                // corrida entre os pushes de termos diferentes.
                resultadosParciais->push_back(std::move(resultado));
                if (--(*restantes) == 0) {
                    self->buscaResultado_ = intersectarConjuntos(*resultadosParciais);
                    self->aplicarFiltrosEOrdenacao();
                }
            });
        });
    }
}

void MosaicoComponent::irParaPaginaLista(int pagina) {
    const int alvo = juce::jlimit(0, totalPaginasLista() - 1, pagina);
    if (alvo == paginaLista_) return;
    paginaLista_ = alvo;
    aplicarFiltrosEOrdenacao();
    if (auto* vp = findParentComponentOfClass<juce::Viewport>()) vp->setViewPosition(0, 0);  // página nova começa do topo
}

void MosaicoComponent::definirItensPorPaginaLista(int n) {
    if (n <= 0 || n == itensPorPaginaLista_) return;
    const int primeiro = paginaLista_ * itensPorPaginaLista_;  // mantém o primeiro item à vista
    itensPorPaginaLista_ = n;
    paginaLista_ = primeiro / n;
    aplicarFiltrosEOrdenacao();
    if (auto* vp = findParentComponentOfClass<juce::Viewport>()) vp->setViewPosition(0, 0);
}

void MosaicoComponent::definirOrdenacao(Ordenacao ordenacao) {
    ordenacao_ = ordenacao;
    colunaOrdenacaoLista_ = 0;  // escolher no menu desliga a ordenação por coluna da lista
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
    if (indice < 0 || indice >= static_cast<int>(subpastas_.size()) || colunas_ <= 0) return {};
    int coluna = indice % colunas_;
    int linha = indice / colunas_;
    int yBase = matriz::ui::tema().espacoPainel;
    int w = getWidth();
    int x0 = (w > 0) ? (coluna * w) / colunas_ : coluna * celulaLargura_;
    int x1 = (w > 0) ? ((coluna + 1) * w) / colunas_ : (coluna + 1) * celulaLargura_;
    return {x0, yBase + linha * celulaAltura_, x1 - x0, celulaAltura_};
}

int MosaicoComponent::indiceSubpastaNaPosicao(juce::Point<int> pos) const {
    if (subpastas_.empty() || colunas_ <= 0) return -1;
    int yBase = matriz::ui::tema().espacoPainel;
    int yRelativo = pos.y - yBase;
    if (yRelativo < 0) return -1;
    int linha = yRelativo / celulaAltura_;
    int totalLinhas = (static_cast<int>(subpastas_.size()) + colunas_ - 1) / colunas_;
    if (linha >= totalLinhas) return -1;
    int w = getWidth();
    int coluna = (w > 0) ? (pos.x * colunas_) / w : pos.x / celulaLargura_;
    if (coluna < 0 || coluna >= colunas_) return -1;
    int indice = linha * colunas_ + coluna;
    if (indice >= static_cast<int>(subpastas_.size())) return -1;
    return indice;
}

int MosaicoComponent::compararPorColunaDaLista(const ItemResumo& a, const ItemResumo& b, int coluna) {
    auto texto = [](const std::string& x, const std::string& y) {
        return juce::String::fromUTF8(x.c_str()).compareNatural(juce::String::fromUTF8(y.c_str()));
    };
    auto nome = [](const ItemResumo& i) { return i.titulo.empty() ? i.nomeOriginalArquivo : i.titulo; };
    auto data = [](const ItemResumo& i) { return i.dataCriacao.empty() ? i.criadoEm : i.dataCriacao; };
    auto marcas = [](const ItemResumo& i) {
        return int(i.marcadoPublicacao) + int(i.marcadoZip) + int(i.marcadoPrint) + int(i.marcadoWatermark);
    };
    switch (coluna) {
        case 1: return categoriaDaLista(a.extensaoArquivo).compare(categoriaDaLista(b.extensaoArquivo));
        case 2: return texto(nome(a), nome(b));
        case 3: return texto(a.extensaoArquivo, b.extensaoArquivo);
        case 4: return texto(data(a), data(b));
        case 5: return a.tamanhoBytes < b.tamanhoBytes ? -1 : (a.tamanhoBytes > b.tamanhoBytes ? 1 : 0);
        case 6: return texto(a.caminhoAbsolutoOrigem.empty() ? a.caminhoRelativoArquivo : a.caminhoAbsolutoOrigem,
                             b.caminhoAbsolutoOrigem.empty() ? b.caminhoRelativoArquivo : b.caminhoAbsolutoOrigem);
        case 7: return texto(a.collectionType.value_or(""), b.collectionType.value_or(""));
        case 8: return texto(a.sourceMedia, b.sourceMedia);
        case 9: return marcas(a) - marcas(b);
        default: return 0;
    }
}

std::vector<std::pair<int, int>> MosaicoComponent::colunasDaLista(int largura) {
    // Larguras padrão das colunas da tabela do INTAKE; PATH absorve a sobra.
    static const int kLarg[] = {36, 80, 220, 110, 150, 80, 250, 150, 200, 80};
    int fixas = 0;
    for (int c = 0; c < 10; ++c) if (c != 6) fixas += kLarg[c];
    const int path = juce::jmax(120, largura - fixas);
    std::vector<std::pair<int, int>> out;
    int x = 0;
    for (int c = 0; c < 10; ++c) {
        const int w = c == 6 ? path : kLarg[c];
        out.push_back({x, w});
        x += w;
    }
    return out;
}

juce::String MosaicoComponent::categoriaDaLista(const std::string& extensao) {
    switch (matriz::ingest::categoriaPorExtensao(juce::String::fromUTF8(extensao.c_str()))) {
        case matriz::ingest::CategoriaMidia::Audio: return "Audio";
        case matriz::ingest::CategoriaMidia::Video: return "Video";
        case matriz::ingest::CategoriaMidia::Imagem: return "Image";
        case matriz::ingest::CategoriaMidia::Documento:
        case matriz::ingest::CategoriaMidia::Texto: return "Document";
        case matriz::ingest::CategoriaMidia::Sessao: return "Project";
        default: return "Other";
    }
}

juce::Colour MosaicoComponent::corCategoriaDaLista(const juce::String& cat) {
    // Mesmas cores das etiquetas TYPE do INTAKE.
    if (cat == "Audio") return juce::Colour(0xff38bdf8);
    if (cat == "Video") return juce::Colour(0xffa855f7);
    if (cat == "Image") return juce::Colour(0xfff59e0b);
    if (cat == "Document") return juce::Colour(0xff10b981);
    if (cat == "Project") return juce::Colour(0xffec4899);
    return juce::Colour(0xff94a3b8);
}

juce::String MosaicoComponent::formatarBytesDaLista(juce::int64 bytes) {
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024.0, 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return juce::String(bytes / (1024.0 * 1024.0), 1) + " MB";
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

// Textos de exibição de UM item. Mesmas regras que o paint() aplicava por célula, a cada quadro.
MosaicoComponent::TextosCelula MosaicoComponent::calcularTextos(const ItemResumo& item) {
    TextosCelula t;
    t.nome = item.titulo.empty() ? juce::String::fromUTF8(item.nomeOriginalArquivo.c_str())
                                 : juce::String::fromUTF8(item.titulo.c_str());
    if (!item.extensaoArquivo.empty()) t.extensao = juce::String::fromUTF8(item.extensaoArquivo.c_str()).toUpperCase();

    // DATE CREATED (só o ano): data do metadado; sem ela, a de entrada; sem ela, o ano do EVENT DATE.
    {
        juce::String data = juce::String::fromUTF8((item.dataCriacao.empty() ? item.criadoEm : item.dataCriacao).c_str());
        t.ano = data.length() >= 4 ? data.substring(0, 4) : juce::String();
        if (t.ano.isEmpty() && item.ano) t.ano = juce::String(*item.ano);
    }

    if (!item.sourceMedia.empty()) {
        t.sourceMedium = juce::String::fromUTF8(OriginalSourceMediumInfo::deserialize(item.sourceMedia).toDisplaySummary().c_str());
        if (t.sourceMedium == "None / Unknown") t.sourceMedium = juce::String();
    }

    const std::string& caminho = !item.caminhoAbsolutoOrigem.empty() ? item.caminhoAbsolutoOrigem : item.caminhoRelativoArquivo;
    if (!caminho.empty()) t.caminho = juce::String::fromUTF8(caminho.c_str());
    t.tamanho = formatarBytesDaLista(item.tamanhoBytes);
    t.categoria = categoriaDaLista(item.extensaoArquivo);
    t.categoriaMaiuscula = t.categoria.toUpperCase();

    if (item.duracaoSegundos.has_value() && *item.duracaoSegundos > 0.0) {
        int total = static_cast<int>(*item.duracaoSegundos + 0.5);
        int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
        if (h > 0)
            t.duracao = juce::String(h) + ":" + juce::String(m).paddedLeft('0', 2) + ":" + juce::String(s).paddedLeft('0', 2);
        else
            t.duracao = juce::String(m).paddedLeft('0', 2) + ":" + juce::String(s).paddedLeft('0', 2);
    }

    t.subtituloGrade = item.extensaoArquivo.empty() ? juce::String("FILE") : t.extensao;
    if (!item.pastaNome.empty()) t.subtituloGrade += "  |  " + juce::String::fromUTF8(item.pastaNome.c_str());
    return t;
}

// Zebrado de "editado" (barra da lista / anel da grade): desenhado UMA vez, com a mesma geometria do
// desenho direto, numa imagem na escala de pixel do paint (Retina) — e reaproveitado por toda célula
// editada. Margem em volta: o contorno do anel vaza ~0,6 px pra fora do retângulo da célula.
namespace {
constexpr int kMargemZebra = 4;
const juce::Colour kZebraAmarelo{0xffFFEE00};
const juce::Colour kZebraRisco{0xdd000000};
}

const juce::Image& MosaicoComponent::imagemZebraBarraLista(juce::Rectangle<float> barRect, float escala) {
    const juce::Rectangle<int> tamanho(0, 0, static_cast<int>(barRect.getWidth()), static_cast<int>(barRect.getHeight()));
    if (zebraBarraLista_.isValid() && zebraBarraListaTamanho_ == tamanho && zebraBarraListaEscala_ == escala) return zebraBarraLista_;
    const int w = tamanho.getWidth() + 2 * kMargemZebra, h = tamanho.getHeight() + 2 * kMargemZebra;
    juce::Image img(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(w * escala)), juce::jmax(1, juce::roundToInt(h * escala)), true);
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(escala));
        const juce::Rectangle<float> r(static_cast<float>(kMargemZebra), static_cast<float>(kMargemZebra),
                                       barRect.getWidth(), barRect.getHeight());
        juce::Path barPath;
        barPath.addRoundedRectangle(r, 2.0f);
        g.reduceClipRegion(barPath);
        g.setColour(kZebraAmarelo);
        g.fillRect(r);
        g.setColour(kZebraRisco);
        for (float y = r.getY() - r.getWidth() * 2; y <= r.getBottom() + r.getWidth() * 2; y += 8.0f)
            g.drawLine(r.getX() - 3.0f, y, r.getRight() + 3.0f, y + r.getWidth(), 3.5f);
    }
    zebraBarraLista_ = img;
    zebraBarraListaTamanho_ = tamanho;
    zebraBarraListaEscala_ = escala;
    return zebraBarraLista_;
}

const juce::Image& MosaicoComponent::imagemZebraGrade(juce::Rectangle<int> bounds, float escala) {
    const juce::Rectangle<int> tamanho(0, 0, bounds.getWidth(), bounds.getHeight());
    if (zebraGrade_.isValid() && zebraGradeTamanho_ == tamanho && zebraGradeEscala_ == escala) return zebraGrade_;
    const auto& tk = matriz::ui::tema();
    const int w = tamanho.getWidth() + 2 * kMargemZebra, h = tamanho.getHeight() + 2 * kMargemZebra;
    juce::Image img(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(w * escala)), juce::jmax(1, juce::roundToInt(h * escala)), true);
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(escala));
        const juce::Rectangle<int> zebraBounds(kMargemZebra, kMargemZebra, tamanho.getWidth(), tamanho.getHeight());
        const float zebraR = juce::jmax(1.0f, tk.raioMedio);
        {
            juce::Graphics::ScopedSaveState saveState(g);
            juce::Path ringPath;
            ringPath.addRoundedRectangle(zebraBounds.toFloat(), zebraR);
            ringPath.addRoundedRectangle(zebraBounds.reduced(5).toFloat(), juce::jmax(1.0f, zebraR - 3.0f));
            ringPath.setUsingNonZeroWinding(false);  // Even-odd hollow ring
            g.reduceClipRegion(ringPath);

            g.setColour(kZebraAmarelo);  // base amarela
            g.fillRect(zebraBounds);

            g.setColour(kZebraRisco);  // riscas escuras transversais
            const float stripePitch = 12.0f, stripeWidth = 5.0f;
            const float minCoord = static_cast<float>(zebraBounds.getX() - zebraBounds.getHeight() - 10);
            const float maxCoord = static_cast<float>(zebraBounds.getRight() + zebraBounds.getHeight() + 10);
            for (float x = minCoord; x <= maxCoord; x += stripePitch)
                g.drawLine(x, static_cast<float>(zebraBounds.getY() - 5), x + static_cast<float>(zebraBounds.getHeight() + 10),
                           static_cast<float>(zebraBounds.getBottom() + 5), stripeWidth);
        }
        g.setColour(kZebraAmarelo);  // contorno externo nítido
        g.drawRoundedRectangle(zebraBounds.toFloat(), zebraR, 1.2f);
    }
    zebraGrade_ = img;
    zebraGradeTamanho_ = tamanho;
    zebraGradeEscala_ = escala;
    return zebraGrade_;
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
        celulaAltura_ = 32;  // mesma altura de linha da lista do INTAKE
    } else {
        switch (tamanhoCelula_) {
            case TamanhoCelula::Pequeno: celulaLargura_ = 112; celulaAltura_ = 100; break;
            case TamanhoCelula::Grande: celulaLargura_ = 240; celulaAltura_ = 210; break;
            case TamanhoCelula::Medio:
            default: celulaLargura_ = 168; celulaAltura_ = 148; break;
        }
    }
    aplicarFiltrosEOrdenacao();  // liga/desliga a paginação da lista (recalcula o layout)
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

void MosaicoComponent::ordenarListaPorColuna(int coluna, bool ascendente) {
    colunaOrdenacaoLista_ = coluna;
    ordenacaoListaAscendente_ = ascendente;
    paginaLista_ = 0;
    aplicarFiltrosEOrdenacao();
    repaint();
}

void MosaicoComponent::aplicarFiltrosEOrdenacao() {
    MATRIZ_TRACE("MosaicoComponent::aplicarFiltrosEOrdenacao");
    invalidarIndiceFiltrados();
    // Trabalha com ÍNDICES em itensTodos_ (antes: copiava cada ItemResumo — com todas as
    // strings — até 3 vezes por refiltro: no filtro, no agrupamento e na ordenação).
    auto& indices = itensFiltrados_.indices();
    indices.clear();

    for (size_t pos = 0; pos < itensTodos_.size(); ++pos) {
        const ItemResumo& item = itensTodos_[pos];
        if (!item.pastaAtiva) continue;
        if (ocultarEditados_ && item.marcadoRevisado) continue;
        if (ocultarNaoSelecionados_ && !selecionados_.count(item.id)) continue;

        // Eixos combinados com E: pasta da árvore, busca de texto, cada
        // categoria de chip. DENTRO de uma categoria de chip, múltipla
        // seleção é OU (Acréscimos §10.2 — "clicáveis e combináveis").
        if (filtroItens_ && !filtroItens_->count(item.id)) continue;
        if (buscaResultado_ && !buscaResultado_->count(item.id) && escopoBusca_ != ProjetoAberto::EscopoBusca::Todos)
            continue;  // busca com escopo: só o que o banco achou naquele campo
        if (buscaResultado_ && !buscaResultado_->count(item.id)) {
            // Fallback em memória (item 6): item não veio no conjunto do
            // FTS5 — ainda pode sobreviver se bater em TODOS os termos
            // ativos via campos básicos já carregados (E lógico entre chips,
            // mesmo critério do conjunto vindo do banco).
            bool todosOsTermosBatem = true;
            for (const auto& termo : buscaTermos_) {
                if (!itemBateComTermoEmMemoria(item, termo)) { todosOsTermosBatem = false; break; }
            }
            if (!todosOsTermosBatem) continue;
        }
        if (!filtrosEstado_.empty() && !filtrosEstado_.count(juce::String(item.estado))) continue;
        if (!filtrosTipoMidia_.empty() && !filtrosTipoMidia_.count(juce::String(item.tipoMidia))) continue;
        if (!filtrosExtensao_.empty() && !filtrosExtensao_.count(juce::String(item.extensaoArquivo))) continue;
        if (!filtrosOrigem_.empty() && !filtrosOrigem_.count(juce::String(item.origem.value_or(std::string())))) continue;
        if (!filtrosContentType_.empty() && !filtrosContentType_.count(juce::String(item.contentType.value_or(std::string())))) continue;
        if (!filtrosCollectionType_.empty() && !filtrosCollectionType_.count(juce::String(item.collectionType.value_or(std::string())))) continue;
        if (filtroDisponibilidade_ == FiltroDisponibilidade::Online && item.offline) continue;
        if (filtroDisponibilidade_ == FiltroDisponibilidade::Offline && !item.offline) continue;
        // Faixa de ano: item sem ano preenchido nunca entra numa faixa —
        // faixa é sobre o que se sabe, não sobre o que falta.
        if (filtroFaixaAno_ && (!item.ano || *item.ano < filtroFaixaAno_->first || *item.ano > filtroFaixaAno_->second))
            continue;

        // Filtro customizado FROM {date} TO {date}
        if (filtroDataDe_.isNotEmpty() || filtroDataAte_.isNotEmpty()) {
            juce::String itemData = juce::String(item.criadoEm).trim();
            if (itemData.isEmpty() && item.ano)
                itemData = juce::String(*item.ano);

            if (itemData.isEmpty()) continue;

            if (filtroDataDe_.isNotEmpty()) {
                if (filtroDataDe_.length() == 4 && itemData.length() >= 4) {
                    if (itemData.substring(0, 4) < filtroDataDe_) continue;
                } else if (itemData < filtroDataDe_) {
                    continue;
                }
            }
            if (filtroDataAte_.isNotEmpty()) {
                if (filtroDataAte_.length() == 4 && itemData.length() >= 4) {
                    if (itemData.substring(0, 4) > filtroDataAte_) continue;
                } else if (itemData.substring(0, filtroDataAte_.length()) > filtroDataAte_) {
                    continue;
                }
            }
        }
        indices.push_back(static_cast<uint32_t>(pos));
    }

    // NEST: cada nest vira UMA célula — a capa. Se a busca/filtro só achou outro arquivo do nest, a célula é
    // esse arquivo (o nest "aberto" nele): o arquivo nunca some da busca. Os demais membros ficam escondidos.
    {
        std::map<std::string, size_t> representante;  // nestId -> posição em `saida`
        std::vector<uint32_t> saida;
        saida.reserve(indices.size());
        for (uint32_t pos : indices) {
            const ItemResumo& item = itensTodos_[pos];
            if (item.nestId.empty()) { saida.push_back(pos); continue; }
            auto it = representante.find(item.nestId);
            if (it == representante.end()) {
                representante[item.nestId] = saida.size();
                saida.push_back(pos);
            } else if (item.nestCapa) {
                saida[it->second] = pos;  // a capa vale mais que um membro que só casou com a busca
            }
        }
        indices = std::move(saida);
    }

    auto comparador = [this](const ItemResumo& a, const ItemResumo& b) {
        // Ordenação escolhida clicando no cabeçalho da LISTA (igual ao INTAKE);
        // vale também na grade de miniaturas.
        if (colunaOrdenacaoLista_ > 0) {
            const int c = compararPorColunaDaLista(a, b, colunaOrdenacaoLista_);
            if (c != 0) return ordenacaoListaAscendente_ ? c < 0 : c > 0;
            return a.criadoEm > b.criadoEm;
        }
        switch (ordenacao_) {
            case Ordenacao::Titulo: return a.titulo < b.titulo;
            case Ordenacao::Estado: return a.estado < b.estado;
            case Ordenacao::Atualizado: return a.atualizadoEm > b.atualizadoEm;
            case Ordenacao::Codigo:
            default: return a.criadoEm > b.criadoEm; // Últimas ingestões acima, mais antigas abaixo
        }
    };

    // Mosaico agrupado por tipo de arquivo.
    std::map<juce::String, std::vector<uint32_t>> baldes;
    for (uint32_t pos : indices) {
        const ItemResumo& item = itensTodos_[pos];
        juce::String chave;
        if (colunaOrdenacaoLista_ > 0) {
            // Ordenação por coluna vale pra lista INTEIRA (todas as páginas):
            // um grupo só, senão cada grupo de tipo/ano ordenaria à parte e a
            // página 1 mostraria só o começo do primeiro grupo.
            chave = "0:" + matriz::i18n::t("grade.todos_ordenados");
        } else if (modoAgrupamento_ == ModoAgrupamento::PorAno) {
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
        baldes[chave].push_back(pos);
    }

    indices.clear();
    grupos_.clear();
    // O índice de hover aponta pra uma posição da lista ANTERIOR — depois
    // de refiltrar/reagrupar ele passaria a realçar uma célula qualquer.
    indiceHover_ = -1;
    for (auto& [chave, itensDoGrupo] : baldes) {
        std::vector<uint32_t> ordenados = itensDoGrupo;
        std::sort(ordenados.begin(), ordenados.end(),
                  [&](uint32_t a, uint32_t b) { return comparador(itensTodos_[a], itensTodos_[b]); });

        GrupoMosaico g;
        g.rotulo = chave.fromFirstOccurrenceOf(":", false, false) + " - " +
                   juce::String(static_cast<int>(ordenados.size()));
        g.indiceInicio = static_cast<int>(indices.size());
        g.quantidade = static_cast<int>(ordenados.size());
        grupos_.push_back(g);

        indices.insert(indices.end(), ordenados.begin(), ordenados.end());
    }

    std::set<std::string> idsVisiveis;
    for (auto& item : itensFiltrados_) idsVisiveis.insert(item.id);
    for (auto it = selecionados_.begin(); it != selecionados_.end();) {
        if (!idsVisiveis.count(*it)) it = selecionados_.erase(it);
        else ++it;
    }
    if (!selecionadoId_.empty() && !idsVisiveis.count(selecionadoId_))
        selecionadoId_.clear();

    // Paginação da LISTA: fatia depois de ordenar e de podar a seleção pelo
    // filtro INTEIRO (a seleção sobrevive à troca de página).
    idsFiltroCompleto_.clear();
    for (auto& item : itensFiltrados_) idsFiltroCompleto_.push_back(item.id);
    totalFiltradoLista_ = static_cast<int>(itensFiltrados_.size());
    if (modoVisao_ == ModoVisao::Lista && totalFiltradoLista_ > itensPorPaginaLista_) {
        paginaLista_ = juce::jlimit(0, totalPaginasLista() - 1, paginaLista_);
        const int ini = paginaLista_ * itensPorPaginaLista_;
        const int fim = juce::jmin(totalFiltradoLista_, ini + itensPorPaginaLista_);
        std::vector<GrupoMosaico> gruposDaPagina;
        for (auto grupo : grupos_) {
            const int gi = juce::jmax(grupo.indiceInicio, ini);
            const int gf = juce::jmin(grupo.indiceInicio + grupo.quantidade, fim);
            if (gi >= gf) continue;
            grupo.indiceInicio = gi - ini;
            grupo.quantidade = gf - gi;
            gruposDaPagina.push_back(grupo);
        }
        indices = std::vector<uint32_t>(indices.begin() + ini, indices.begin() + fim);
        grupos_ = std::move(gruposDaPagina);
    } else {
        paginaLista_ = 0;
    }
    invalidarIndiceFiltrados();  // a lista foi refeita (agrupada/paginada) acima
    if (aoMudarPaginacao) aoMudarPaginacao();

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
    focoId_ = itemId;  // (o preview navega por aqui: o foco acompanha)
    indiceFoco_ = -1;
    repaint();
    if (aoMudarSelecao) aoMudarSelecao();
}

void MosaicoComponent::selecionarTodos() {
    // Tudo o que passa pelos filtros atuais — todas as páginas da lista.
    selecionados_.clear();
    for (auto& id : idsFiltroCompleto_) selecionados_.insert(id);
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

void MosaicoComponent::definirSelecao(const std::set<std::string>& itemIds) {
    selecionados_ = itemIds;
    if (!selecionados_.empty()) {
        selecionadoId_ = *selecionados_.begin();
        indiceAncoraShift_ = 0;
    } else {
        selecionadoId_.clear();
        indiceAncoraShift_ = -1;
    }
    repaint();
    if (aoMudarSelecao) aoMudarSelecao();
}

void MosaicoComponent::mouseMove(const juce::MouseEvent& e) {
    int indice = indiceNaPosicao(e.getPosition());
    if (indice == indiceHover_) return;
    const int anterior = indiceHover_;
    indiceHover_ = indice;
    repintarCelula(anterior);  // só as duas células afetadas, não o componente inteiro
    repintarCelula(indice);
}

void MosaicoComponent::mouseExit(const juce::MouseEvent&) {
    if (indiceHover_ < 0) return;
    const int anterior = indiceHover_;
    indiceHover_ = -1;
    repintarCelula(anterior);
}

void MosaicoComponent::repintarCelula(int indice) {
    if (indice < 0 || indice >= static_cast<int>(itensFiltrados_.size())) return;
    // Folga pro que vaza da célula: pilha de nest (+6 px), anel de foco e contornos.
    repaint(boundsDaCelula(indice).expanded(10));
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
        g.yItens = cursor + kAlturaCabecalhoGrupo + (modoVisao_ == ModoVisao::Lista ? kAlturaCabecalhoColunas : 0);
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
    if (colunas_ <= 0) return {};
    for (auto& g : grupos_) {
        if (indice < g.indiceInicio || indice >= g.indiceInicio + g.quantidade) continue;
        int localIndice = indice - g.indiceInicio;
        int coluna = localIndice % colunas_;
        int linha = localIndice / colunas_;
        int w = getWidth();
        int x0 = (w > 0) ? (coluna * w) / colunas_ : coluna * celulaLargura_;
        int x1 = (w > 0) ? ((coluna + 1) * w) / colunas_ : (coluna + 1) * celulaLargura_;
        return {x0, g.yItens + linha * celulaAltura_, x1 - x0, celulaAltura_};
    }
    return {};
}

int MosaicoComponent::indiceNaPosicao(juce::Point<int> pos) const {
    if (colunas_ <= 0) return -1;
    const GrupoMosaico* g = grupoNaPosicaoY(pos.y);
    if (!g || pos.y < g->yItens) return -1; // fora de um grupo, ou em cima do cabeçalho (não clicável)

    int w = getWidth();
    int coluna = (w > 0) ? (pos.x * colunas_) / w : pos.x / celulaLargura_;
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

    // Clique no cabeçalho de colunas da LISTA: ordena por aquela coluna
    // (de novo na mesma coluna inverte), como na tabela do INTAKE.
    if (modoVisao_ == ModoVisao::Lista && !e.mods.isPopupMenu()) {
        for (const auto& grupo : grupos_) {
            const int y0 = grupo.yTopo + kAlturaCabecalhoGrupo;
            if (e.y < y0 || e.y >= y0 + kAlturaCabecalhoColunas) continue;
            const auto cols = colunasDaLista(getWidth());
            for (size_t c = 1; c < cols.size(); ++c) {
                if (e.x < cols[c].first || e.x >= cols[c].first + cols[c].second) continue;
                const int col = static_cast<int>(c);
                ordenarListaPorColuna(col, colunaOrdenacaoLista_ == col ? !ordenacaoListaAscendente_ : true);
                return;
            }
            return;
        }
    }

    if (itensFiltrados_.empty()) {
        if (aoClicarEstadoVazio) aoClicarEstadoVazio();
        return;
    }

    // Item 9: se este clique virar arrasto e não houver destino de drop
    // nesta tela, o laço recomeça do que já estava selecionado ANTES deste
    // clique (não do que o clique-simples acabou de selecionar) — mesma
    // semântica de começar o laço no vazio.
    selecaoAntesDoClique_ = selecionados_;

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
        // Clique simples no vazio NÃO esvazia a seleção (pedido do operador:
        // perdia centenas de itens selecionados por um clique fora). Só um
        // laço de verdade, sem Shift/Cmd, recomeça do zero — decidido no
        // primeiro arrasto (mouseDrag). Esc / Clear Selection limpam.
        bool somando = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
        lacoRecomecaAoArrastar_ = !somando;
        selecaoAntesDoLaco_ = selecionados_;
        repaint();
        return;
    }
    if (editorInline_) cancelarEdicaoInline();

    const std::string& id = itensFiltrados_[static_cast<size_t>(indice)].id;

    if (modoQuarentena_) {
        auto btnMais = boundsBotaoAdicionarGrid(indice);
        if (btnMais.contains(e.getPosition())) {
            if (aoConfirmarEntradaGrid) aoConfirmarEntradaGrid(id);
            return;
        }
    }

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
            naCheckbox = e.getPosition().x < celula.getX() + 36;  // coluna de seleção (igual ao INTAKE)
        } else {
            auto celulaReduzida = celula.reduced(4);
            auto areaImagem = celulaReduzida.withHeight(celulaReduzida.getHeight() - 34);
            constexpr int kDiam = 18;
            juce::Rectangle<int> areaCheck;
            if (modoQuarentena_) {
                areaCheck = juce::Rectangle<int>(areaImagem.getRight() - kDiam - 5,
                                                 areaImagem.getBottom() - kDiam - 5, kDiam + 8, kDiam + 8);
            } else {
                areaCheck = juce::Rectangle<int>(areaImagem.getRight() - kDiam - 4,
                                                 areaImagem.getY() + 4, kDiam + 8, kDiam + 8);
            }
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
        // Clique sem Cmd num item JÁ selecionado: pode ser o começo de um
        // arrasto do grupo — só reduz a seleção a ele no mouseUp sem arrasto.
        pendingDeselect_ = true;
        pendingDeselectId_ = id;
        indiceAncoraShift_ = indice;
    } else {
        // Clique sem Cmd troca a seleção pelo item; somar é só com Cmd
        // (clique ou arrasto).
        selecionados_ = {id};
        indiceAncoraShift_ = indice;
    }

    selecionadoId_ = id;
    focoId_ = id;
    indiceFoco_ = indice;
    focoVisivel_ = false;  // o anel só aparece quando o teclado é usado
    repaint();
    if (aoMudarSelecao) aoMudarSelecao();
    if (aoSelecionar) aoSelecionar(selecionadoId_);
}

void MosaicoComponent::mouseDrag(const juce::MouseEvent& e) {
    if (lacoAtivo_) {
        if (e.getDistanceFromDragStart() < 5) return;  // tremida do clique não é laço
        if (lacoRecomecaAoArrastar_) {
            lacoRecomecaAoArrastar_ = false;
            selecaoAntesDoLaco_.clear();
        }
        atualizarSelecaoDoLaco(e);
        return;
    }

    if (e.getDistanceFromDragStart() >= 8) {
        stopTimer();
        clicouNaTituloDeItemSelecionado_ = false;
    }

    if (e.getDistanceFromDragStart() < 8) return;

    pendingDeselect_ = false;

    if (!permiteArrastarParaFora_) {
        // Sem alvo de drop nesta tela (item 9): clicar numa miniatura e
        // arrastar cria seleção em laço a partir do ponto do clique, em vez
        // de tentar um arrasto de arquivo sem destino nenhum.
        lacoAtivo_ = true;
        lacoInicio_ = e.getMouseDownPosition();
        selecaoAntesDoLaco_ = selecaoAntesDoClique_;
        atualizarSelecaoDoLaco(e);
        return;
    }

    if (selecionados_.empty()) return;

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

    // Cmd+arrasto SOMA o que o laço tocar à seleção anterior (pedido do
    // operador); sem Cmd/Shift o laço recomeça do zero (ver mouseDrag).
    for (size_t i = 0; i < itensFiltrados_.size(); ++i) {
        if (!boundsDaCelula(static_cast<int>(i)).intersects(lacoAtual_)) continue;
        selecionados_.insert(itensFiltrados_[i].id);
    }

    if (!selecionados_.empty() && selecionadoId_.empty()) selecionadoId_ = *selecionados_.begin();
    repaint();
    if (aoMudarSelecao) aoMudarSelecao();
    // aoSelecionar dispara a reconstrução pesada da ficha de metadados
    // (leitura de EXIF/imagem) — chamar isso a cada frame de mouseDrag
    // travava a navegação. A ficha só precisa atualizar quando o laço
    // termina (mouseUp), não a cada pixel arrastado.
}

void MosaicoComponent::mouseUp(const juce::MouseEvent&) {
    if (lacoAtivo_) {
        lacoAtivo_ = false;
        selecaoAntesDoLaco_.clear();
        repaint();
        if (aoSelecionar && !selecionadoId_.empty()) aoSelecionar(selecionadoId_);
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

int MosaicoComponent::indiceDoFoco() const {
    if (focoId_.empty()) return -1;
    const int total = static_cast<int>(itensFiltrados_.size());
    if (indiceFoco_ >= 0 && indiceFoco_ < total && itensFiltrados_[static_cast<size_t>(indiceFoco_)].id == focoId_)
        return indiceFoco_;
    for (int i = 0; i < total; ++i)
        if (itensFiltrados_[static_cast<size_t>(i)].id == focoId_) return i;
    return -1;
}

// Vizinho de uma célula. Esquerda/direita seguem a ordem dos itens (fim da linha -> começo da
// seguinte, e o inverso); cima/baixo andam uma linha na mesma coluna, atravessando os grupos.
int MosaicoComponent::indiceVizinho(int indice, int dx, int dy) const {
    const int total = static_cast<int>(itensFiltrados_.size());
    if (dx != 0) return juce::jlimit(0, juce::jmax(0, total - 1), indice + dx);
    if (dy == 0 || colunas_ <= 0) return indice;

    const GrupoMosaico* atual = nullptr;
    const GrupoMosaico* anterior = nullptr;
    const GrupoMosaico* proximo = nullptr;
    for (size_t k = 0; k < grupos_.size(); ++k) {
        const auto& g = grupos_[k];
        if (indice >= g.indiceInicio && indice < g.indiceInicio + g.quantidade) {
            atual = &g;
            if (k > 0) anterior = &grupos_[k - 1];
            if (k + 1 < grupos_.size()) proximo = &grupos_[k + 1];
            break;
        }
    }
    if (!atual) return indice;

    const int local = indice - atual->indiceInicio;
    const int coluna = local % colunas_;
    const int linha = local / colunas_;
    if (dy > 0) {
        if (local + colunas_ < atual->quantidade) return indice + colunas_;
        if (linha < atual->linhas - 1) return atual->indiceInicio + atual->quantidade - 1;  // última linha, mais curta
        if (proximo && proximo->quantidade > 0) return proximo->indiceInicio + juce::jmin(coluna, proximo->quantidade - 1);
        return indice;
    }
    if (linha > 0) return indice - colunas_;
    if (anterior && anterior->quantidade > 0) {
        const int ultimaLinha = anterior->linhas - 1;
        const int naUltima = anterior->quantidade - ultimaLinha * colunas_;
        return anterior->indiceInicio + ultimaLinha * colunas_ + juce::jmin(coluna, naUltima - 1);
    }
    return indice;
}

void MosaicoComponent::garantirCelulaVisivel(int indice) {
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    if (!vp) return;
    const auto celula = boundsDaCelula(indice);
    if (celula.isEmpty()) return;
    int topo = celula.getY();
    for (const auto& g : grupos_)  // primeira linha do grupo: mostra o cabeçalho junto
        if (indice >= g.indiceInicio && indice < g.indiceInicio + juce::jmin(colunas_, g.quantidade)) {
            topo = g.yTopo;
            break;
        }
    const auto vista = vp->getViewArea();
    int y = vista.getY();
    if (topo < vista.getY()) y = topo;
    else if (celula.getBottom() > vista.getBottom()) y = celula.getBottom() - vista.getHeight();
    if (y != vista.getY()) vp->setViewPosition(vista.getX(), juce::jmax(0, y));
}

void MosaicoComponent::moverFoco(int dx, int dy, bool estender) {
    const int total = static_cast<int>(itensFiltrados_.size());
    if (total == 0) return;

    int atual = indiceDoFoco();
    int novo;
    if (atual < 0) {  // primeira seta: parte do item selecionado (ou do primeiro), sem andar ainda
        novo = 0;
        for (int i = 0; i < total && !selecionadoId_.empty(); ++i)
            if (itensFiltrados_[static_cast<size_t>(i)].id == selecionadoId_) { novo = i; break; }
        atual = novo;
    } else {
        novo = indiceVizinho(atual, dx, dy);
    }
    focoVisivel_ = true;

    const std::string& id = itensFiltrados_[static_cast<size_t>(novo)].id;
    if (estender) {
        const int ancora = (indiceAncoraShift_ >= 0 && indiceAncoraShift_ < total) ? indiceAncoraShift_ : atual;
        const int antesDe = juce::jmin(ancora, atual), antesAte = juce::jmax(ancora, atual);
        const int depoisDe = juce::jmin(ancora, novo), depoisAte = juce::jmax(ancora, novo);
        for (int i = antesDe; i <= antesAte; ++i)  // saiu do intervalo: desmarca; o resto da seleção fica
            if (i < depoisDe || i > depoisAte) selecionados_.erase(itensFiltrados_[static_cast<size_t>(i)].id);
        for (int i = depoisDe; i <= depoisAte; ++i) selecionados_.insert(itensFiltrados_[static_cast<size_t>(i)].id);
        indiceAncoraShift_ = ancora;
    } else {
        selecionados_ = {id};
        indiceAncoraShift_ = novo;
    }
    selecionadoId_ = id;
    focoId_ = id;
    indiceFoco_ = novo;
    garantirCelulaVisivel(novo);
    repaint();
    notificadorSelecao_.startTimer(90);
}

bool MosaicoComponent::keyPressed(const juce::KeyPress& tecla) {
    {
        const auto mods = tecla.getModifiers();
        if (!editorInline_ && !mods.isCommandDown() && !mods.isCtrlDown() && !mods.isAltDown()) {
            const int codigo = tecla.getKeyCode();
            int dx = 0, dy = 0;
            if (codigo == juce::KeyPress::leftKey) dx = -1;
            else if (codigo == juce::KeyPress::rightKey) dx = 1;
            else if (codigo == juce::KeyPress::upKey) dy = -1;
            else if (codigo == juce::KeyPress::downKey) dy = 1;
            if (dx != 0 || dy != 0) {
                moverFoco(dx, dy, mods.isShiftDown());
                return true;  // sempre consome: a seta nunca deve rolar o Viewport por conta própria
            }
            if (codigo == juce::KeyPress::spaceKey && !mods.isShiftDown()) {
                int i = indiceDoFoco();
                if (i < 0 && !selecionadoId_.empty())
                    for (int k = 0; k < static_cast<int>(itensFiltrados_.size()); ++k)
                        if (itensFiltrados_[static_cast<size_t>(k)].id == selecionadoId_) { i = k; break; }
                if (i >= 0) {
                    const auto& item = itensFiltrados_[static_cast<size_t>(i)];
                    if (!item.offline && aoAbrirPreview) aoAbrirPreview(item.id);
                }
                return true;
            }
        }
    }
    if (tecla == juce::KeyPress::escapeKey && !selecionados_.empty()) {
        selecionados_.clear();
        repaint();
        if (aoMudarSelecao) aoMudarSelecao();
        return true;
    }
    if (tecla.getKeyCode() == 'A' && (tecla.getModifiers().isCommandDown() || tecla.getModifiers().isCtrlDown())) {
        selecionarTodos(); // "tudo que está no filtro atual" — ver nota em selecionarTodos()
        if (aoSelecionar && !selecionadoId_.empty()) aoSelecionar(selecionadoId_);
        return true;
    }

    if (tecla.getKeyCode() == 'P' && (tecla.getModifiers().isCommandDown() || tecla.getModifiers().isCtrlDown())) {
        SendToPrintDialog::exibirModal(projeto_);
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

    // N = renomear (era R; o R agora é o Reject do INTAKE e não deve se confundir com ele).
    if ((tecla.getKeyCode() == 'N' || c == 'n' || c == 'N') &&
        !tecla.getModifiers().isCommandDown() && !tecla.getModifiers().isCtrlDown()) {
        renomearSelecao();
        return true;
    }

    bool semModificadores = !tecla.getModifiers().isCommandDown() &&
                            !tecla.getModifiers().isCtrlDown() &&
                            !tecla.getModifiers().isAltDown();

    if ((tecla.getKeyCode() == 'H' || c == 'h' || c == 'H') && semModificadores) {
        std::vector<std::string> alvos(selecionados_.begin(), selecionados_.end());
        if (alvos.empty() && !selecionadoId_.empty()) alvos.push_back(selecionadoId_);
        if (!alvos.empty()) {
            projeto_.alternarMarcacao(ProjetoAberto::TipoMarcacao::Html, alvos);
            bool novoEstado = projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Html, alvos.front());
            std::unordered_set<std::string> alvosSet(alvos.begin(), alvos.end());
            for (auto& item : itensTodos_) {
                if (alvosSet.count(item.id)) {
                    item.marcadoPublicacao = novoEstado;
                }
            }
            for (auto& item : itensFiltrados_) {
                if (alvosSet.count(item.id)) {
                    item.marcadoPublicacao = novoEstado;
                }
            }
            repaint();
            return true;
        }
    }

    if ((tecla.getKeyCode() == 'K' || c == 'k' || c == 'K') && semModificadores) {
        std::vector<std::string> alvos(selecionados_.begin(), selecionados_.end());
        if (alvos.empty() && !selecionadoId_.empty()) alvos.push_back(selecionadoId_);
        if (!alvos.empty()) {
            projeto_.alternarMarcacao(ProjetoAberto::TipoMarcacao::Zip, alvos);
            bool novoEstado = projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Zip, alvos.front());
            std::unordered_set<std::string> alvosSet(alvos.begin(), alvos.end());
            for (auto& item : itensTodos_) {
                if (alvosSet.count(item.id)) {
                    item.marcadoZip = novoEstado;
                }
            }
            for (auto& item : itensFiltrados_) {
                if (alvosSet.count(item.id)) {
                    item.marcadoZip = novoEstado;
                }
            }
            repaint();
            return true;
        }
    }

    // P (Send to Print) e W (Watermark) só fazem sentido pra fotos — filtra
    // a seleção antes de marcar, ignorando silenciosamente áudio/vídeo/
    // documentos/sessões que estejam junto. Baseado em extensaoArquivo,
    // mesma categoriaPorExtensao já usada pelo filtro MEDIA TYPE.
    auto ehFoto = [this](const std::string& id) {
        const ItemResumo* it = itemEmTodos(id);
        if (it == nullptr) return false;
        return matriz::ingest::categoriaPorExtensao(juce::String(it->extensaoArquivo)) ==
               matriz::ingest::CategoriaMidia::Imagem;
    };

    if ((tecla.getKeyCode() == 'P' || c == 'p' || c == 'P') && semModificadores) {
        std::vector<std::string> alvos(selecionados_.begin(), selecionados_.end());
        if (alvos.empty() && !selecionadoId_.empty()) alvos.push_back(selecionadoId_);
        alvos.erase(std::remove_if(alvos.begin(), alvos.end(), [&](const std::string& id) { return !ehFoto(id); }), alvos.end());
        if (!alvos.empty()) {
            projeto_.alternarMarcacao(ProjetoAberto::TipoMarcacao::Print, alvos);
            bool novoEstado = projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Print, alvos.front());
            std::unordered_set<std::string> alvosSet(alvos.begin(), alvos.end());
            for (auto& item : itensTodos_) {
                if (alvosSet.count(item.id)) {
                    item.marcadoPrint = novoEstado;
                }
            }
            for (auto& item : itensFiltrados_) {
                if (alvosSet.count(item.id)) {
                    item.marcadoPrint = novoEstado;
                }
            }
            repaint();
            return true;
        }
    }

    if ((tecla.getKeyCode() == 'W' || c == 'w' || c == 'W') && semModificadores) {
        std::vector<std::string> alvos(selecionados_.begin(), selecionados_.end());
        if (alvos.empty() && !selecionadoId_.empty()) alvos.push_back(selecionadoId_);
        alvos.erase(std::remove_if(alvos.begin(), alvos.end(), [&](const std::string& id) { return !ehFoto(id); }), alvos.end());
        if (!alvos.empty()) {
            projeto_.alternarMarcacao(ProjetoAberto::TipoMarcacao::Watermark, alvos);
            bool novoEstado = projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Watermark, alvos.front());
            std::unordered_set<std::string> alvosSet(alvos.begin(), alvos.end());
            for (auto& item : itensTodos_) {
                if (alvosSet.count(item.id)) {
                    item.marcadoWatermark = novoEstado;
                }
            }
            for (auto& item : itensFiltrados_) {
                if (alvosSet.count(item.id)) {
                    item.marcadoWatermark = novoEstado;
                }
            }
            repaint();
            return true;
        }
    }

    if ((tecla.getKeyCode() == 'E' || c == 'e' || c == 'E') && semModificadores) {
        std::vector<std::string> alvos(selecionados_.begin(), selecionados_.end());
        if (alvos.empty() && !selecionadoId_.empty()) alvos.push_back(selecionadoId_);
        if (!alvos.empty()) {
            bool todosMarcados = std::all_of(alvos.begin(), alvos.end(), [this](const std::string& id) {
                const ItemResumo* it = itemEmTodos(id);
                return it != nullptr && it->marcadoRevisado;
            });
            bool novoEstado = !todosMarcados;
            projeto_.alternarMarcadoRevisado(alvos);
            std::unordered_set<std::string> alvosSet(alvos.begin(), alvos.end());
            for (auto& item : itensTodos_) {
                if (alvosSet.count(item.id)) item.marcadoRevisado = novoEstado;
            }
            for (auto& item : itensFiltrados_) {
                if (alvosSet.count(item.id)) item.marcadoRevisado = novoEstado;
            }
            repaint();
            return true;
        }
    }

    if ((tecla.getKeyCode() == 'C' || c == 'c' || c == 'C') &&
        !tecla.getModifiers().isCommandDown() && !tecla.getModifiers().isCtrlDown()) {
        limparMetadadosSelecao();
        return true;
    }

    return false;
}

namespace {
void renomearItemNoProjeto(ProjetoAberto& projetoAberto, const std::string& itemId, const juce::String& novoTitulo) {
    projetoAberto.renomearItens({itemId}, novoTitulo.toStdString());
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

void MosaicoComponent::limparMetadadosSelecao() {
    std::vector<std::string> alvos(selecionados_.begin(), selecionados_.end());
    if (alvos.empty() && !selecionadoId_.empty()) alvos.push_back(selecionadoId_);
    if (alvos.empty()) return;
    if (aoLimparMetadados) {
        aoLimparMetadados(std::move(alvos));
    }
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

    const auto& item = itensFiltrados_[static_cast<size_t>(indice)];
    if (item.offline) {
        if (aoAbrirRelinkOffline) {
            aoAbrirRelinkOffline(item.id);
            return;
        }
    }

    if (aoAbrirPreview) aoAbrirPreview(item.id);
}

const juce::Image* MosaicoComponent::miniaturaCache(const std::string& itemId) {
    const juce::ScopedLock sl(cacheLock_);
    auto it = cacheMiniaturas_.find(itemId);
    if (it != cacheMiniaturas_.end()) return &it->second;
    return nullptr;
}

namespace {
// Reduz (nunca amplia) preservando a proporção, pra caber em maxLargura x maxAltura.
juce::Image reduzirParaCelula(const juce::Image& imagem, int maxLargura, int maxAltura) {
    if (!imagem.isValid() || maxLargura <= 0 || maxAltura <= 0) return imagem;
    if (imagem.getWidth() <= maxLargura && imagem.getHeight() <= maxAltura) return imagem;
    const double fator = juce::jmin(static_cast<double>(maxLargura) / imagem.getWidth(),
                                    static_cast<double>(maxAltura) / imagem.getHeight());
    return imagem.rescaled(juce::jmax(1, juce::roundToInt(imagem.getWidth() * fator)),
                           juce::jmax(1, juce::roundToInt(imagem.getHeight() * fator)), juce::Graphics::highResamplingQuality);
}
} // namespace

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

    // Miniatura reduzida UMA vez, aqui no job, ao tamanho da MAIOR célula (zoom máximo) em pixels físicos
    // (Retina): o paint desenha a imagem já pequena em vez de reamostrar a original a cada quadro, e o
    // cache (400 entradas) deixa de guardar bitmaps enormes. Nunca amplia; o zoom nunca invalida o cache.
    double escalaTela = 1.0;
    for (const auto& d : juce::Desktop::getInstance().getDisplays().displays) escalaTela = juce::jmax(escalaTela, d.scale);
    const int maxLargura = juce::roundToInt(kLarguraMaximaMiniatura * escalaTela);
    const int maxAltura = juce::roundToInt(kAlturaMaximaMiniatura * escalaTela);

    poolMiniaturas_.addJob([ponteiroSeguro, projeto, itemId, maxLargura, maxAltura]() mutable {
        auto caminho = projeto->caminhoMiniaturaPrincipal(itemId);
        juce::Image imagem;
        if (caminho) imagem = reduzirParaCelula(juce::ImageFileFormat::loadFrom(juce::File(*caminho)), maxLargura, maxAltura);
        bool temCaminho = caminho.has_value();

        if (!imagem.isValid()) {
            if (auto arq = projeto->arquivoPrincipal(itemId)) {
                juce::File f(arq->caminhoAbsoluto);
                juce::String ext = f.getFileExtension().toLowerCase().replace(".", "");
                juce::String logoFile = matriz::ingest::obterLogoParaExtensao(ext);
                if (logoFile.isEmpty() && ext == "pd") logoFile = "puredata.png";

                if (logoFile.isNotEmpty()) {
                    juce::File assetsFolder = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("Assets");
                    juce::File logoImgFile = assetsFolder.getChildFile(logoFile);
                    if (logoImgFile.existsAsFile()) {
                        imagem = reduzirParaCelula(juce::ImageFileFormat::loadFrom(logoImgFile), maxLargura, maxAltura);
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
            self->repintarCelula(self->indiceFiltradoDe(itemId));  // só a célula que ganhou a miniatura
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
    // Voltou a aparecer por um ANCESTRAL (visibilityChanged não dispara nesse caso):
    // eventos que chegaram escondida são aplicados logo depois deste quadro.
    if (temEventosPendentes()) agendarAplicarPendentes();
    const auto& tk = matriz::ui::tema();
    g.fillAll(tk.fundo);

    static const juce::Font font14Bold(juce::FontOptions(14.0f, juce::Font::bold));
    static const juce::Font font13Bold(juce::FontOptions(13.0f, juce::Font::bold));
    static const juce::Font font11Bold(juce::FontOptions(11.0f, juce::Font::bold));
    static const juce::Font font11Normal(juce::FontOptions(11.0f));
    static const juce::Font font10Bold(juce::FontOptions(10.0f, juce::Font::bold));
    static const juce::Font font95Normal(juce::FontOptions(9.5f));
    static const juce::Font font9Bold(juce::FontOptions(9.0f, juce::Font::bold));

    static const juce::Path checkmarkPath = []() {
        juce::Path p;
        p.startNewSubPath(0.24f, 0.50f);
        p.lineTo(0.42f, 0.70f);
        p.lineTo(0.78f, 0.30f);
        return p;
    }();

    if (arrastandoArquivo_) {
        g.setColour(tk.acento.withAlpha(0.12f));
        g.fillRect(g.getClipBounds());
        g.setColour(tk.acento);
        g.drawRect(getLocalBounds(), 2);
    }

    if (itensFiltrados_.empty() && subpastas_.empty() && mensagemVazia_.has_value() && !arrastandoArquivo_) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
        g.drawFittedText(*mensagemVazia_, getLocalBounds().reduced(24), juce::Justification::centred, 2);
        return;
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
    // Escala de pixel deste paint (2 na Retina): o zebrado pré-desenhado precisa estar nela pra ficar nítido.
    const float escalaPixel = static_cast<float>(g.getInternalContext().getPhysicalPixelScaleFactor());

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
            g.setFont(font11Bold);
            g.drawText(grupo.rotulo, areaCabecalho.reduced(tema().espacoMedio, 0), juce::Justification::centredLeft);
            if (modoVisao_ == ModoVisao::Lista) {
                // Cabeçalho de colunas igual ao da tabela do INTAKE.
                juce::Rectangle<int> areaCols(0, grupo.yTopo + kAlturaCabecalhoGrupo, getWidth(), kAlturaCabecalhoColunas);
                g.setColour(tk.painelAlt);
                g.fillRect(areaCols);
                g.setColour(tk.borda);
                g.fillRect(areaCols.getX(), areaCols.getBottom() - 1, areaCols.getWidth(), 1);
                static const char* const kRotulos[] = {"intake.col_select", "intake.col_type", "intake.col_name",
                                                       "intake.col_origin", "intake.col_date", "intake.col_size",
                                                       "intake.col_path", "intake.col_collection",
                                                       "intake.col_source_media", "intake.col_action"};
                const auto cols = colunasDaLista(getWidth());
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
                for (size_t c = 0; c < cols.size(); ++c) {
                    juce::Rectangle<int> r(cols[c].first, areaCols.getY(), cols[c].second, areaCols.getHeight());
                    juce::String rotulo = matriz::i18n::t(kRotulos[c]);
                    if (static_cast<int>(c) == colunaOrdenacaoLista_)
                        rotulo << (ordenacaoListaAscendente_ ? juce::String::fromUTF8("  \xe2\x96\xb2") : juce::String::fromUTF8("  \xe2\x96\xbc"));
                    g.drawText(rotulo, r.reduced(6, 0), juce::Justification::centredLeft, true);
                    if (c > 0) g.fillRect(r.getX(), r.getY() + 6, 1, r.getHeight() - 12);
                }
            }
        }

        int primeiraLinha = juce::jmax(0, (clip.getY() - grupo.yItens) / celulaAltura_);
        int ultimaLinha = juce::jmax(0, (clip.getBottom() - grupo.yItens) / celulaAltura_ + 1);
        int primeiroLocal = juce::jmax(0, primeiraLinha * colunas_);
        int ultimoLocal = juce::jmin(grupo.quantidade, (ultimaLinha + 1) * colunas_);

        for (int local = primeiroLocal; local < ultimoLocal; ++local) {
            int i = grupo.indiceInicio + local;
            const ItemResumo& item = itensFiltrados_[static_cast<size_t>(i)];
            const TextosCelula& tx = textosDoFiltrado(static_cast<size_t>(i));  // calculados no snapshot
            juce::Rectangle<int> bounds = boundsDaCelula(i).reduced(modoVisao_ == ModoVisao::Lista ? 1 : 4);

            juce::Colour corEstado = corDoEstado(item.estado);
            bool selecionado = selecionados_.count(item.id) > 0;
            bool sobHover = i == indiceHover_;

            if (modoVisao_ == ModoVisao::Lista) {
                juce::Colour corCat = corPorCategoria(item.tipoMidia);

                bool marcadoH = item.marcadoPublicacao;
                bool marcadoK = item.marcadoZip;
                bool marcadoP = item.marcadoPrint;
                bool temMarcacao = marcadoH || marcadoK || marcadoP;

                // Fundo zebrado igual ao da tabela do INTAKE.
                g.setColour(local % 2 == 1 ? tk.painelAlt.withAlpha(0.35f) : tk.painel);
                g.fillRect(bounds.expanded(1));

                if (marcadoH) {
                    g.setColour(juce::Colour(0xff39ff14).withAlpha(0.18f));
                    g.fillRect(bounds);
                } else if (marcadoK) {
                    g.setColour(juce::Colour(0xff0077ff).withAlpha(0.15f));
                    g.fillRect(bounds);
                } else if (marcadoP) {
                    g.setColour(juce::Colour(0xffff6b00).withAlpha(0.15f));
                    g.fillRect(bounds);
                } else if (selecionado) {
                    g.setColour(tk.acento.withAlpha(0.25f));  // mesmo destaque de linha do INTAKE
                    g.fillRect(bounds);
                } else if (sobHover) {
                    g.setColour(tk.painelAlt.withAlpha(0.6f));
                    g.fillRect(bounds);
                }

                // Anéis concêntricos na visualização em lista
                float listOffset = 0.0f;
                if (marcadoH) {
                    g.setColour(juce::Colour(0xff39ff14));
                    g.drawRoundedRectangle(bounds.reduced(static_cast<int>(listOffset)).toFloat(), 3.0f, 2.0f);
                    listOffset += 2.0f;
                }
                if (marcadoK) {
                    g.setColour(juce::Colour(0xff0077ff));
                    g.drawRoundedRectangle(bounds.reduced(static_cast<int>(listOffset)).toFloat(), 3.0f, 2.5f);
                    listOffset += 2.5f;
                }
                if (marcadoP) {
                    g.setColour(juce::Colour(0xffff6b00));
                    g.drawRoundedRectangle(bounds.reduced(static_cast<int>(listOffset)).toFloat(), 3.0f, 2.0f);
                    listOffset += 2.0f;
                }

                // Category color bar (left edge) — zebra striped for edited items if enabled
                const juce::Colour kZebraYellowList{0xffFFEE00}; // vivid yellow
                const juce::Colour kZebraStripeList{0xdd000000}; // near-black stripe
                float barW = (destacarEditados_ && item.metadadosEditados) ? 9.0f : 4.0f;
                juce::Rectangle<float> barRect(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY() + 2),
                                               barW, static_cast<float>(bounds.getHeight() - 4));
                if (destacarEditados_ && item.metadadosEditados) {
                    // Zebrado desenhado uma vez numa imagem (imagemZebraBarraLista) e reaproveitado.
                    // drawImage multiplica pela opacidade da cor corrente: opaco aqui, e o estado volta no fim do bloco.
                    juce::Graphics::ScopedSaveState estadoZebra(g);
                    g.setOpacity(1.0f);
                    g.drawImage(imagemZebraBarraLista(barRect, escalaPixel),
                                juce::Rectangle<float>(barRect.getX() - static_cast<float>(kMargemZebra),
                                                       barRect.getY() - static_cast<float>(kMargemZebra),
                                                       barRect.getWidth() + 2.0f * kMargemZebra,
                                                       barRect.getHeight() + 2.0f * kMargemZebra));
                }
                (void) corCat;  // lista igual à do INTAKE: sem barra de categoria (o TYPE é a etiqueta)

                if (destacarEditados_ && item.metadadosEditados && !marcadoP && !selecionado) {
                    g.setColour(kZebraYellowList.withAlpha(0.45f));
                    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.5f);
                }

                g.setColour(tk.borda.withAlpha(0.2f));
                g.fillRect(bounds.getX(), bounds.getBottom() - 1, bounds.getWidth(), 1);

                // Mesmas colunas, larguras e badges da lista do INTAKE (pedido do
                // operador): sem miniatura, TYPE como etiqueta colorida.
                const auto cols = colunasDaLista(bounds.getWidth());
                auto celulaCol = [&](int c) {
                    return juce::Rectangle<int>(bounds.getX() + cols[static_cast<size_t>(c)].first, bounds.getY(),
                                                cols[static_cast<size_t>(c)].second, bounds.getHeight());
                };
                const bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
                auto semValor = [&](juce::Rectangle<int> r) {
                    g.setColour(tk.textoTerciario);
                    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
                    g.drawText(isPt ? juce::String::fromUTF8("Nenhum") : juce::String("None"), r.reduced(6, 0),
                               juce::Justification::centredLeft, true);
                };

                // Seleção
                constexpr int kCheckDiam = 16;
                auto areaCheck = celulaCol(0);
                juce::Rectangle<float> checkRect(static_cast<float>(areaCheck.getCentreX() - kCheckDiam / 2),
                                                 static_cast<float>(areaCheck.getCentreY() - kCheckDiam / 2),
                                                 static_cast<float>(kCheckDiam), static_cast<float>(kCheckDiam));
                if (selecionado) {
                    g.setColour(tk.acento);
                    g.fillRoundedRectangle(checkRect, 3.0f);
                    g.setColour(tk.textoSobreAcento);
                    g.strokePath(checkmarkPath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                                       juce::PathStrokeType::rounded),
                                 juce::AffineTransform::scale(static_cast<float>(kCheckDiam)).translated(checkRect.getX(), checkRect.getY()));
                } else {
                    g.setColour(sobHover ? tk.textoTerciario : tk.borda);
                    g.drawRoundedRectangle(checkRect, 3.0f, 1.2f);
                }

                // TYPE
                const juce::String& categoria = tx.categoria;
                {
                    auto badge = celulaCol(1).reduced(4, 5);
                    g.setColour(corCategoriaDaLista(categoria));
                    g.fillRoundedRectangle(badge.toFloat(), 3.0f);
                    g.setColour(juce::Colours::white);
                    g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
                    g.drawText(tx.categoriaMaiuscula, badge, juce::Justification::centred, true);
                }

                // ASSET / FILENAME (+ OFFLINE)
                {
                    auto r = celulaCol(2);
                    juce::String nome = tx.nome;
                    g.setColour(tk.textoPrimario);
                    g.setFont(juce::Font(juce::FontOptions(12.5f)));
                    if (item.nestTotal > 1) {  // NEST: o nome ganha o número de arquivos do grupo
                        const juce::String tag = "  [NEST " + juce::String(item.nestTotal) + "]";
                        nome += tag;
                    }
                    if (item.offline) {
                        auto off = juce::Rectangle<int>(r.getRight() - 64, r.getCentreY() - 9, 58, 18);
                        g.drawText(nome, r.withRight(off.getX() - 4).reduced(6, 0), juce::Justification::centredLeft, true);
                        g.setColour(juce::Colour(0xffef4444));
                        g.fillRoundedRectangle(off.toFloat(), 3.0f);
                        g.setColour(juce::Colours::white);
                        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
                        g.drawText("OFFLINE", off, juce::Justification::centred);
                    } else {
                        g.drawText(nome, r.reduced(6, 0), juce::Justification::centredLeft, true);
                    }
                }

                // EXTENSION
                g.setColour(tk.textoSecundario);
                g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
                g.drawText(tx.extensao.isEmpty() ? juce::String("-") : tx.extensao,
                           celulaCol(3).reduced(6, 0), juce::Justification::centredLeft, true);

                // DATE CREATED (só o ano, como no INTAKE)
                {
                    // Mesma regra do INTAKE: data do metadado; sem ela, a de entrada (calculada em calcularTextos).
                    g.setFont(juce::Font(juce::FontOptions(12.0f)));
                    g.drawText(tx.ano.isNotEmpty() ? tx.ano : "-", celulaCol(4).reduced(6, 0), juce::Justification::centredLeft, true);
                }

                // SIZE
                g.setColour(tk.textoPrimario);
                g.drawText(tx.tamanho, celulaCol(5).reduced(4, 0),
                           juce::Justification::centredLeft, true);

                // PATH
                g.setColour(tk.textoSecundario);
                g.drawText(tx.caminho.isEmpty() ? juce::String("-") : tx.caminho,
                           celulaCol(6).reduced(6, 0), juce::Justification::centredLeft, true);

                // CONTENT
                if (item.collectionType && !item.collectionType->empty()) {
                    auto badge = celulaCol(7).reduced(4, 5);
                    g.setColour(tk.acento);
                    g.fillRoundedRectangle(badge.toFloat(), 3.0f);
                    g.setColour(tk.textoSobreAcento);
                    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
                    g.drawText(traduzirContent(juce::String::fromUTF8(item.collectionType->c_str()), isPt), badge,
                               juce::Justification::centred, true);
                } else {
                    semValor(celulaCol(7));
                }

                // ORIGINAL SOURCE MEDIUM
                {
                    const juce::String& texto = tx.sourceMedium;  // resumo calculado no snapshot (era um parse de JSON por célula)
                    if (texto.isEmpty()) {
                        semValor(celulaCol(8));
                    } else {
                        auto badge = celulaCol(8).reduced(4, 5);
                        g.setColour(juce::Colour(0xff0d9488));
                        g.fillRoundedRectangle(badge.toFloat(), 3.0f);
                        g.setColour(juce::Colours::white);
                        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
                        g.drawText(texto, badge.reduced(6, 0), juce::Justification::centredLeft, true);
                    }
                }

                // ACTION: no Metadata, as marcas H / K / P / W
                {
                    auto r = celulaCol(9).reduced(4, 0);
                    auto selo = [&](bool on, const char* letra, juce::Colour cor, juce::Colour corTexto) {
                        if (!on) return;
                        auto b = r.removeFromLeft(18).withSizeKeepingCentre(16, 16);
                        r.removeFromLeft(2);
                        g.setColour(cor);
                        g.fillRoundedRectangle(b.toFloat(), 3.0f);
                        g.setColour(corTexto);
                        g.setFont(font9Bold);
                        g.drawText(letra, b, juce::Justification::centred);
                    };
                    selo(item.marcadoPublicacao, "H", juce::Colour(0xff39ff14), juce::Colours::black);
                    selo(item.marcadoZip, "K", juce::Colour(0xff0077ff), juce::Colours::white);
                    selo(item.marcadoPrint, "P", juce::Colour(0xffff6b00), juce::Colours::white);
                    selo(item.marcadoWatermark, "W", juce::Colour(0xffffcc00), juce::Colours::black);
                }

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

            // OFFLINE badge on thumbnail
            if (item.offline) {
                int offBadgeW = 68;
                int offBadgeH = 18;
                juce::Rectangle<int> offBadge(areaImagem.getCentreX() - offBadgeW / 2, areaImagem.getCentreY() - offBadgeH / 2, offBadgeW, offBadgeH);
                g.setColour(juce::Colour(0xd0000000));
                g.fillRoundedRectangle(offBadge.toFloat(), 4.0f);
                g.setColour(juce::Colour(0xfff97316));
                g.drawRoundedRectangle(offBadge.toFloat(), 4.0f, 1.2f);
                g.setFont(font9Bold);
                g.drawText("OFFLINE", offBadge, juce::Justification::centred);
            }

            // NEST: duas linhas "empilhadas" saindo do cartão (direita/baixo) e o selo com o número de arquivos.
            if (item.nestTotal > 1) {
                g.setColour(tk.borda.withAlpha(0.9f));
                for (int off : {3, 6}) {
                    g.drawLine((float) bounds.getRight() + off, (float) bounds.getY() + off + 2,
                               (float) bounds.getRight() + off, (float) bounds.getBottom() + off - 2, 1.5f);
                    g.drawLine((float) bounds.getX() + off + 2, (float) bounds.getBottom() + off,
                               (float) bounds.getRight() + off - 2, (float) bounds.getBottom() + off, 1.5f);
                }
                const juce::String txt = "NEST " + juce::String(item.nestTotal);
                const int w = juce::GlyphArrangement::getStringWidthInt(font9Bold, txt) + 12;
                juce::Rectangle<int> pilula(areaImagem.getRight() - w - 4, areaImagem.getBottom() - 20, w, 16);
                g.setColour(juce::Colour(0xe6111827));
                g.fillRoundedRectangle(pilula.toFloat(), 8.0f);
                g.setColour(juce::Colour(0xff38bdf8));
                g.drawRoundedRectangle(pilula.toFloat(), 8.0f, 1.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font9Bold);
                g.drawText(txt, pilula, juce::Justification::centred);
            }

            // Extension badge (top-left)
            const juce::String& ext = tx.extensao;
            if (ext.isNotEmpty()) {
                int badgeW = juce::jmax(28, static_cast<int>(ext.length()) * 7 + 10);
                juce::Rectangle<int> badge(areaImagem.getX() + 4, areaImagem.getY() + 4, badgeW, 16);
                g.setColour(juce::Colour(0xcc000000));
                g.fillRoundedRectangle(badge.toFloat(), 8.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font9Bold);
                g.drawText(ext, badge, juce::Justification::centred);
            }

            // Duration badge (bottom-right of thumbnail, or bottom-left if in quarantine mode)
            const juce::String& durText = tx.duracao;
            if (durText.isNotEmpty()) {
                int badgeW = juce::jmax(36, static_cast<int>(durText.length()) * 7 + 8);
                int badgeX = modoQuarentena_ ? (areaImagem.getX() + 4) : (areaImagem.getRight() - badgeW - 4);
                juce::Rectangle<int> durBadge(badgeX, areaImagem.getBottom() - 20, badgeW, 16);
                g.setColour(juce::Colour(0xcc000000));
                g.fillRoundedRectangle(durBadge.toFloat(), 4.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font9Bold);
                g.drawText(durText, durBadge, juce::Justification::centred);
            }

            // Corner stamps H / K / P (top-right of areaImagem)
            int stampRight = (selecionado && !modoQuarentena_) ? (areaImagem.getRight() - 28) : (areaImagem.getRight() - 4);
            if (item.marcadoPrint) {
                juce::Rectangle<int> stampP(stampRight - 16, areaImagem.getY() + 4, 16, 16);
                g.setColour(juce::Colour(0xffff6b00));
                g.fillRoundedRectangle(stampP.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font9Bold);
                g.drawText("P", stampP, juce::Justification::centred);
                stampRight -= 19;
            }
            if (item.marcadoZip) {
                juce::Rectangle<int> stampK(stampRight - 16, areaImagem.getY() + 4, 16, 16);
                g.setColour(juce::Colour(0xff0077ff));
                g.fillRoundedRectangle(stampK.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font9Bold);
                g.drawText("K", stampK, juce::Justification::centred);
                stampRight -= 19;
            }
            if (item.marcadoPublicacao) {
                juce::Rectangle<int> stampH(stampRight - 16, areaImagem.getY() + 4, 16, 16);
                g.setColour(juce::Colour(0xff39ff14));
                g.fillRoundedRectangle(stampH.toFloat(), 3.0f);
                g.setColour(juce::Colours::black);
                g.setFont(font9Bold);
                g.drawText("H", stampH, juce::Justification::centred);
                stampRight -= 19;
            }
            if (item.marcadoWatermark) {
                juce::Rectangle<int> stampW(stampRight - 16, areaImagem.getY() + 4, 16, 16);
                g.setColour(juce::Colour(0xffffcc00));
                g.fillRoundedRectangle(stampW.toFloat(), 3.0f);
                g.setColour(juce::Colours::black);
                g.setFont(font9Bold);
                g.drawText("W", stampW, juce::Justification::centred);
                stampRight -= 19;
            }

            float offsetRing = 0.0f;

            // marcadoRevisado é o atalho manual "E" (Tag as Edited) — mostra
            // o zebrado amarelo sempre que marcado, sem depender do toggle
            // "destacar editados" (que é sobre metadadosEditados, automático).
            bool mostrarZebraRevisado = (destacarEditados_ && item.metadadosEditados) || item.marcadoRevisado;
            if (mostrarZebraRevisado) {
                // Zebrado de editado: anel amarelo com riscas, desenhado uma vez numa imagem
                // (imagemZebraGrade) e reaproveitado por todas as células.
                {
                    juce::Graphics::ScopedSaveState estadoZebra(g);
                    g.setOpacity(1.0f);
                    g.drawImage(imagemZebraGrade(bounds, escalaPixel),
                                juce::Rectangle<float>(static_cast<float>(bounds.getX() - kMargemZebra),
                                                       static_cast<float>(bounds.getY() - kMargemZebra),
                                                       static_cast<float>(bounds.getWidth() + 2 * kMargemZebra),
                                                       static_cast<float>(bounds.getHeight() + 2 * kMargemZebra)));
                }
                offsetRing += 5.0f;
            }

            if (selecionado) {
                g.setColour(tk.acento);
                auto selBounds = bounds.reduced(static_cast<int>(offsetRing));
                float selR = juce::jmax(1.0f, tk.raioMedio - offsetRing);
                g.drawRoundedRectangle(selBounds.toFloat(), selR, 3.5f);
            } else if (!mostrarZebraRevisado) {
                g.setColour(corCat.withAlpha(0.65f));
                g.drawRoundedRectangle(bounds.toFloat(), tk.raioMedio, 1.8f);
            }

            if (item.sincronizado) {
                g.setColour(tk.haloSincronizado);
                g.drawRoundedRectangle(bounds.reduced(3).toFloat(), tk.raioMedio, 1.5f);
            }

            // Checkmark on selected items (drawn at bottom-right in quarantine mode to avoid +, otherwise top-right)
            if (selecionado) {
                constexpr int kDiametroMarca = 20;
                float marcaY = modoQuarentena_ ? static_cast<float>(areaImagem.getBottom() - kDiametroMarca - 5)
                                               : static_cast<float>(areaImagem.getY() + 5);
                juce::Rectangle<float> marca(static_cast<float>(areaImagem.getRight() - kDiametroMarca - 5),
                                              marcaY,
                                              static_cast<float>(kDiametroMarca),
                                              static_cast<float>(kDiametroMarca));
                g.setColour(tk.acento);
                g.fillRoundedRectangle(marca, 4.0f);
                g.setColour(tk.textoSobreAcento);
                g.strokePath(checkmarkPath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                                   juce::PathStrokeType::rounded),
                             juce::AffineTransform::scale(static_cast<float>(kDiametroMarca)).translated(marca.getX(), marca.getY()));
            }

            // Button "+" in top-right for confirming quarantine item into GRID (fixed position)
            if (modoQuarentena_) {
                constexpr int kTamMais = 22;
                juce::Rectangle<float> btnMais(static_cast<float>(areaImagem.getRight() - kTamMais - 6),
                                               static_cast<float>(areaImagem.getY() + 6),
                                               static_cast<float>(kTamMais),
                                               static_cast<float>(kTamMais));
                g.setColour(juce::Colour(0xff22c55e)); // Green badge
                g.fillRoundedRectangle(btnMais, 11.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font14Bold);
                g.drawText("+", btnMais, juce::Justification::centred);
            }

            const juce::String& nomeExibicaoGrid = tx.nome;
            auto areaTexto = bounds.withTop(areaImagem.getBottom() + 2).reduced(6, 0);
            g.setColour(tk.textoPrimario);
            g.setFont(font11Bold);
            g.drawText(nomeExibicaoGrid, areaTexto.removeFromTop(textAreaH / 2),
                       juce::Justification::centredLeft, true);

            // Subtitle: file type + folder
            juce::String info2 = tx.subtituloGrade;
            if (item.offline) info2 += "  |  OFFLINE";
            g.setColour(item.offline ? juce::Colour(0xfff97316) : tk.textoTerciario);
            g.setFont(font95Normal);
            g.drawText(info2, areaTexto, juce::Justification::centredLeft, true);
        }
    }

    // Anel do FOCO do teclado (diferente do destaque de seleção).
    if (focoVisivel_) {
        const int i = indiceDoFoco();
        if (i >= 0) {
            const auto celula = boundsDaCelula(i).reduced(modoVisao_ == ModoVisao::Lista ? 1 : 3);
            if (celula.intersects(g.getClipBounds())) {
                // Azul vivo + fio branco: nenhum outro estado usa essa cor (a seleção é cinza escuro).
                g.setColour(juce::Colour(0xff0a84ff));
                g.drawRoundedRectangle(celula.toFloat(), 5.0f, 4.0f);
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.drawRoundedRectangle(celula.toFloat().reduced(2.5f), 3.5f, 1.2f);
            }
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
    if (aoRenomearItem) aoRenomearItem();
}

void MosaicoComponent::cancelarEdicaoInline() {
    if (!editorInline_) return;
    indiceEditando_ = -1;
    editorInline_->onFocusLost = nullptr;
    removeChildComponent(editorInline_.get());
    editorInline_.reset();
}

juce::Rectangle<int> MosaicoComponent::boundsBotaoAdicionarGrid(int indice) const {
    auto bounds = boundsDaCelula(indice);
    int textAreaH = juce::jmin(40, bounds.getHeight() / 4 + 10);
    juce::Rectangle<int> areaImagem = bounds.withHeight(bounds.getHeight() - textAreaH);
    
    constexpr int kTamMais = 22;
    return juce::Rectangle<int>(areaImagem.getRight() - kTamMais - 6, areaImagem.getY() + 6, kTamMais, kTamMais);
}

} // namespace matriz::ui
