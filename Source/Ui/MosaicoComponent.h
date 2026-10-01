#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "../Ingest/LeituraTecnica.h"
#include "EventBus.h"
#include "ProjetoAberto.h"

namespace matriz::ui {

enum class Ordenacao { Codigo, Titulo, Estado, Atualizado };

struct GrupoMosaico {
    juce::String rotulo;
    int indiceInicio = 0;
    int quantidade = 0;
    int yTopo = 0;
    int yItens = 0;
    int linhas = 0;
};

struct SubpastaInfo {
    juce::String nome;
    int quantidade = 0;
    std::set<std::string> itemIds;
};

class MosaicoComponent : public juce::Component, public EventBusListener, private juce::Timer {
public:
    explicit MosaicoComponent(ProjetoAberto& projeto);
    ~MosaicoComponent() override;

    void aoItemAlterado(const EventoItemAlterado& e) override;
    // Escondido (outra aba na frente): o evento vira "precisa atualizar" e é aplicado,
    // numa passada só, quando a grade volta a aparecer (aqui e no primeiro paint —
    // visibilityChanged() não dispara quando é um ANCESTRAL que aparece).
    void visibilityChanged() override;
    // Item da grade carregado em memória (por mapa id->índice), nullptr se não está. O
    // ponteiro vale até a próxima mudança da lista — use na hora, não guarde.
    const ItemResumo* itemEmMemoria(const std::string& itemId) { return itemEmTodos(itemId); }

    // Recarrega a lista de itens do banco e reaplica filtro/ordenação atuais.
    // Recarrega em BACKGROUND (I1/I4): a consulta e a montagem do vetor
    // rodam na Thread de Snapshot, e a grade só recebe o resultado pronto.
    // Retorna imediatamente — quem precisa do resultado na hora usa
    // recarregarSincrono().
    void recarregar();
    void recarregarSincrono();
    void atualizarItemEmMemoria(const std::string& itemId);
    // Edição em lote: entre iniciar/finalizar, atualizarItemEmMemoria só atualiza
    // o item; o refiltro/reordenação da lista inteira roda UMA vez no finalizar
    // (antes rodava por item: O(N itens x N catálogo) com seleções grandes).
    void iniciarLoteAtualizacao() { ++loteAtualizacaoProfundidade_; }
    void finalizarLoteAtualizacao();
    bool snapshotEmAndamentoParaTeste() const { return poolSnapshot_.getNumJobs() > 0; }

    // Chips de filtro (Acréscimos §10.2 — "clicáveis e combináveis"):
    // múltipla seleção DENTRO de uma categoria é OU ("tipo=vinil OU
    // tipo=cd"); combinar categorias diferentes é E. Conjunto vazio numa
    // categoria = sem filtro nela (todos passam).
    void alternarFiltroTipoMidia(const juce::String& tipo);
    void alternarFiltroEstado(const juce::String& estado);
    void alternarFiltroExtensao(const juce::String& extensao);
    // Origem (Digital/Analógico, item 4 dos acréscimos) — mesmo padrão OU-
    // dentro-da-categoria dos outros chips. "" filtra pelos ainda sem valor.
    void alternarFiltroOrigem(const juce::String& origem);
    void alternarFiltroContentType(const juce::String& contentType);
    void alternarFiltroCollectionType(const juce::String& collectionType);
    enum class FiltroDisponibilidade { All, Online, Offline };
    void alternarFiltroDisponibilidade(FiltroDisponibilidade f);
    FiltroDisponibilidade filtroDisponibilidadeAtivo() const { return filtroDisponibilidade_; }
    void limparFiltros(); // chips + busca + faixa de ano — não mexe no filtro de pasta da árvore (eixo independente)
    const std::set<juce::String>& filtrosTipoMidiaAtivos() const { return filtrosTipoMidia_; }
    const std::set<juce::String>& filtrosEstadoAtivos() const { return filtrosEstado_; }
    const std::set<juce::String>& filtrosExtensaoAtivos() const { return filtrosExtensao_; }
    const std::set<juce::String>& filtrosOrigemAtivos() const { return filtrosOrigem_; }
    const std::set<juce::String>& filtrosContentTypeAtivos() const { return filtrosContentType_; }
    const std::set<juce::String>& filtrosCollectionTypeAtivos() const { return filtrosCollectionType_; }

    // Faixa de ano ("1978-1985", item 4.3) — E com os demais filtros. Item
    // sem "ano" preenchido nunca aparece dentro de uma faixa ativa.
    void definirFiltroFaixaAno(int anoDe, int anoAte);
    void limparFiltroFaixaAno();
    std::optional<std::pair<int, int>> filtroFaixaAnoAtivo() const { return filtroFaixaAno_; }

    void definirFiltroPeriodoData(const juce::String& dataDe, const juce::String& dataAte);
    void limparFiltroPeriodoData();

    // Eixo de agrupamento do mosaico (item 4.3 — "ano é o eixo padrão de
    // agrupamento na grade"). Automatico é o comportamento já existente
    // (tipo de mídia no Archive, artista/lançamento no Catalog).
    enum class ModoAgrupamento { Automatico, PorAno };
    void definirModoAgrupamento(ModoAgrupamento modo);
    ModoAgrupamento modoAgrupamentoAtual() const { return modoAgrupamento_; }

    void definirModoQuarentena(bool modo) {
        if (modoQuarentena_ != modo) {
            modoQuarentena_ = modo;
            repaint();
        }
    }
    bool modoQuarentenaAtual() const { return modoQuarentena_; }
    std::function<void(const std::string& itemId)> aoConfirmarEntradaGrid;

    void definirDestacarEditados(bool destacar) {
        if (destacarEditados_ != destacar) {
            destacarEditados_ = destacar;
            repaint();
        }
    }
    bool destacarEditados() const { return destacarEditados_; }

    void definirOcultarEditados(bool ocultar) {
        if (ocultarEditados_ != ocultar) {
            ocultarEditados_ = ocultar;
            aplicarFiltrosEOrdenacao();
            repaint();
        }
    }
    bool ocultarEditados() const { return ocultarEditados_; }

    // Itens com CONTENT = Hidden. Padrão: visíveis (só o Catalog os esconde).
    void definirMostrarOcultos(bool mostrar) {
        if (mostrarOcultos_ != mostrar) {
            mostrarOcultos_ = mostrar;
            aplicarFiltrosEOrdenacao();
            repaint();
        }
    }

    void definirOcultarNaoSelecionados(bool ocultar) {
        if (ocultarNaoSelecionados_ != ocultar) {
            ocultarNaoSelecionados_ = ocultar;
            aplicarFiltrosEOrdenacao();
            repaint();
        }
    }
    bool ocultarNaoSelecionados() const { return ocultarNaoSelecionados_; }

    // Busca (Acréscimos §10.1): código/título, campo de ficha e assunto —
    // consulta o banco via ProjetoAberto::buscarItens a cada chamada (OCR/
    // transcrição não existem ainda, gap declarado). "" limpa a busca.
    // Compat: usada por buscas de termo único fora do catálogo (backup file
    // selector, coleções salvas/inteligentes, selftest) — substitui TODOS
    // os chips ativos por, no máximo, um único termo.
    void definirBusca(const juce::String& texto);
    // item 6: múltiplos chips de busca combinados com E — um arquivo só
    // aparece se bater com TODOS os termos ativos.
    void adicionarTermoBusca(const juce::String& texto);
    void removerTermoBusca(int indice);
    const juce::StringArray& termosBuscaAtuais() const { return buscaTermos_; }
    // Join dos termos ativos — compat com o "algum filtro ativo?" do painel
    // de filtros e com o campo único de coleções salvas/inteligentes.
    juce::String buscaAtual() const { return buscaTermos_.joinIntoString(" "); }

    void definirOrdenacao(Ordenacao ordenacao);

    // Paginação da LISTA (a grade de miniaturas rola tudo): a seleção vale
    // entre páginas; Cmd+A seleciona o filtro inteiro.
    bool paginacaoListaAtiva() const { return modoVisao_ == ModoVisao::Lista && totalFiltradoLista_ > itensPorPaginaLista_; }
    int paginaListaAtual() const { return paginaLista_; }
    int totalPaginasLista() const {
        return juce::jmax(1, (totalFiltradoLista_ + itensPorPaginaLista_ - 1) / itensPorPaginaLista_);
    }
    int totalFiltradoLista() const { return totalFiltradoLista_; }
    int itensPorPaginaLista() const { return itensPorPaginaLista_; }
    void irParaPaginaLista(int pagina);
    // Mesmo efeito de clicar no cabeçalho da coluna da LISTA (1..9).
    void ordenarListaPorColuna(int coluna, bool ascendente);
    // Ids na ordem exibida (só a página atual, quando a lista pagina).
    std::vector<std::string> idsVisiveisEmOrdem() const {
        std::vector<std::string> out;
        for (const auto& i : itensFiltrados_) out.push_back(i.id);
        return out;
    }
    void definirItensPorPaginaLista(int n);
    std::function<void()> aoMudarPaginacao;

    // Escopo da busca (seletor sob o campo de busca).
    void definirEscopoBusca(ProjetoAberto::EscopoBusca escopo);
    ProjetoAberto::EscopoBusca escopoBusca() const { return escopoBusca_; }

    // Filtro por seleção da árvore Origem/Acervo (Reorientação completa
    // §8.1 — "clicar numa pasta filtra a grade"). nullopt = sem filtro de
    // pasta (todos os itens, sujeitos aos outros filtros normalmente).
    void definirFiltroItens(std::optional<std::set<std::string>> itemIds);
    const std::optional<std::set<std::string>>& filtroItensAtual() const { return filtroItens_; }

    void selecionarItem(const std::string& itemId);
    const std::string& itemSelecionado() const { return selecionadoId_; }
    // Item com o foco do teclado (setas) e quantas colunas a grade tem agora — usados pelo selftest.
    const std::string& itemEmFoco() const { return focoId_; }
    int colunasParaTeste() const { return colunas_; }

    // Seleção múltipla (Reorientação completa §3.3 — clique, Shift,
    // Cmd/Ctrl). selecionadoId_ continua sendo a âncora/último clicado —
    // é o que a ficha lateral mostra hoje; edição em lote de verdade
    // (§7.2) é trabalho futuro, fora do ponto de parada desta etapa.
    const std::set<std::string>& itensSelecionados() const { return selecionados_; }

    // Ações da barra de seleção (BarraSelecaoComponent). "Selecionar todos"
    // pega o que está VISÍVEL — se há filtro ou busca ativa, selecionar o
    // acervo inteiro por trás do filtro seria uma armadilha silenciosa
    // numa edição em lote logo em seguida.
    void selecionarTodos();
    void limparSelecao();
    void definirSelecao(const std::set<std::string>& itemIds);

    // Disparado sempre que o conjunto selecionado muda, por qualquer
    // caminho (clique, Shift, Cmd, selecionarTodos, limparSelecao) — a
    // barra de seleção e a ficha lateral acompanham por aqui.
    std::function<void()> aoMudarSelecao;

    std::function<void(int indice, std::vector<std::string> itemIds)> aoCategorizarPorAtalho;
    std::function<void()> aoRenomearItem;
    std::function<void()> aoRemoverDoBackup;
    std::function<void(std::vector<std::string> itemIds)> aoLimparMetadados;

    void renomearSelecao();
    void removerSelecaoDoBackup();
    void limparMetadadosSelecao();

    // Disparado sempre que o CONJUNTO VISÍVEL muda — chip de filtro, busca,
    // ordenação, pasta da árvore, recarregar(). É o único funil por onde
    // todas essas mudanças passam (aplicarFiltrosEOrdenacao), então quem
    // precisa acompanhar a contagem "X de Y arquivos" escuta só aqui em vez
    // de se pendurar em cada caminho que mexe em filtro.
    std::function<void()> aoMudarConteudoVisivel;

    // Item adjacente ao atual na ordem visível da grade (§3.4 — "setas pra
    // navegar sem voltar à grade"). direcao: -1 anterior, +1 próximo.
    // nullopt se não houver vizinho (início/fim da lista) ou id não encontrado.
    std::optional<std::string> itemAdjacente(const std::string& itemIdAtual, int direcao) const;

    int totalItensCarregados() const { return static_cast<int>(itensTodos_.size()); }
    int totalItensVisiveis() const { return static_cast<int>(itensFiltrados_.size()); }
    const std::vector<ItemResumo>& todosItensEmMemoria() const { return itensTodos_; }

    void definirSubpastas(std::vector<SubpastaInfo> subpastas);
    std::function<void(const SubpastaInfo&)> aoNavegarParaSubpasta;

    // Item 9 (fix de UI): telas sem Vault/pasta pra soltar arquivo (a grade
    // do METADATA, por exemplo) chamam isto com false — clicar numa
    // miniatura e arrastar passa a criar seleção em laço em vez de tentar
    // iniciar um arrasto de arquivo sem destino, que impedia o laço de
    // funcionar quando o clique começava em cima de uma célula.
    void definirPermiteArrastarParaFora(bool permite) { permiteArrastarParaFora_ = permite; }

    enum class ModoVisao { Grade, Lista };
    void definirModoVisao(ModoVisao modo);
    ModoVisao modoVisaoAtual() const { return modoVisao_; }

    enum class TamanhoCelula { Pequeno, Medio, Grande };
    void definirTamanhoCelula(TamanhoCelula tamanho);
    TamanhoCelula tamanhoCelulaAtual() const { return tamanhoCelula_; }

    void definirTamanhoContinuo(double valor);
    double tamanhoContinuoAtual() const;

    // Introspecção pra teste (Parte 1 — agrupamento por modo, §3.5): os
    // rótulos de grupo já visíveis na tela, na ordem em que aparecem.
    std::vector<juce::String> rotulosDeGrupo() const {
        std::vector<juce::String> out;
        for (auto& g : grupos_) out.push_back(g.rotulo);
        return out;
    }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    // Realce sob o cursor: sem ele a grade parece uma imagem estática, e o
    // operador não descobre que a célula é clicável até tentar.
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    // Cmd/Ctrl+A seleciona tudo que está no filtro atual.
    bool keyPressed(const juce::KeyPress&) override;
    // Arrastar item(ns) selecionado(s) pra uma pasta do Acervo (§5.2 —
    // "arrastar da Origem pro Acervo é o gesto principal de organização").
    // O alvo (ArvoreComponent) é quem decide o que fazer com a descrição.
    void mouseDrag(const juce::MouseEvent&) override;

    // O alvo real de arrastar-e-soltar é a janela inteira (MainComponent,
    // Parte 3 da correção crítica) — antes era só o mosaico, que cobre uma
    // coluna estreita da janela; soltar em qualquer outro lugar não fazia
    // nada. O mosaico só recebe o estado (pra mostrar "solte para ingerir"
    // no lugar de "nenhum item ainda") — quem decide se está interessado e
    // quem processa o drop é o MainComponent.
    void definirArrastandoArquivo(bool arrastando) {
        if (arrastandoArquivo_ == arrastando) return;
        arrastandoArquivo_ = arrastando;
        repaint();
    }

    std::function<void(const std::string& itemId)> aoSelecionar;
    // Clique duplo abre o preview no mesmo espaço (§3.4) — clique único só
    // seleciona (permite Shift/Cmd sem entrar em preview no meio do gesto).
    std::function<void(const std::string& itemId)> aoAbrirPreview;
    // Double click on offline item opens relink dialog instead of normal preview player
    std::function<void(const std::string& itemId)> aoAbrirRelinkOffline;
    // Estado vazio inteiro é clicável, além de alvo de drop (§3.2) — abre o
    // mesmo seletor de arquivo/pasta que o botão "Add material".
    std::function<void()> aoClicarEstadoVazio;

    // Botão direito na grade. O MainComponent monta e executa o menu (é ele
    // que tem os ganchos pra ficha, filtros e recarga) — o mosaico só avisa
    // que houve o gesto e sobre QUAIS itens, já resolvida a regra de "clicou
    // fora da seleção? então a seleção passa a ser só esse item".
    std::function<void(std::vector<std::string> itemIds)> aoPedirMenuContexto;

    // true enquanto o snapshot em background disparado por recarregar()
    // ainda não voltou. Mesmo uso que ArvoreComponent::recargaPendente().
    bool snapshotPendente() const { return snapshotPendente_; }
    // Incrementa a cada snapshot COMPLETO aplicado (não em atualizarItemEmMemoria).
    int versaoSnapshot() const { return versaoSnapshot_; }
    // Grade vazia por causa de um filtro (ex.: "Show Recently Ingested" sem
    // leva registrada): mostra este aviso no lugar do "arraste aqui".
    void definirMensagemVazia(std::optional<juce::String> msg) {
        if (mensagemVazia_ != msg) { mensagemVazia_ = std::move(msg); repaint(); }
    }

    static constexpr int kAlturaCabecalhoGrupo = 26;
    // Lista (igual à do INTAKE): cabeçalho de colunas sob o título do grupo.
    static constexpr int kAlturaCabecalhoColunas = 26;
    static std::vector<std::pair<int, int>> colunasDaLista(int largura);  // x, largura por coluna
    static juce::String categoriaDaLista(const std::string& extensao);
    static juce::Colour corCategoriaDaLista(const juce::String& categoria);
    static juce::String formatarBytesDaLista(juce::int64 bytes);
    // Ordenação pelo cabeçalho da lista (coluna 1..9; 0 = a ordenação normal).
    // Vale também na grade de miniaturas.
    static int compararPorColunaDaLista(const ItemResumo& a, const ItemResumo& b, int coluna);
    int colunaOrdenacaoLista_ = 0;
    ProjetoAberto::EscopoBusca escopoBusca_ = ProjetoAberto::EscopoBusca::Todos;
    int paginaLista_ = 0;
    int itensPorPaginaLista_ = 100;
    int totalFiltradoLista_ = 0;
    std::vector<std::string> idsFiltroCompleto_;  // todas as páginas (Cmd+A)
    bool ordenacaoListaAscendente_ = true;
    static constexpr int kEspacoEntreGrupos = 6;

private:
    void aplicarFiltrosEOrdenacao();
    juce::String rotuloGrupoArchive(const ItemResumo& item) const;
    void recalcularLayout();
    juce::Rectangle<int> boundsDaCelula(int indice) const;
    int indiceNaPosicao(juce::Point<int> pos) const;
    const GrupoMosaico* grupoNaPosicaoY(int y) const;
    int alturaSecaoSubpastas() const;
    juce::Rectangle<int> boundsSubpasta(int indice) const;
    int indiceSubpastaNaPosicao(juce::Point<int> pos) const;
    void desenharSubpasta(juce::Graphics&, juce::Rectangle<int> bounds, const SubpastaInfo& sub) const;
    juce::Colour corDoEstado(const std::string& estado) const;
    juce::Colour corPorCategoria(const std::string& tipoMidia) const;
    const juce::Image* miniaturaCache(const std::string& itemId);
    void pedirCarregamentoMiniatura(const std::string& itemId);
    void desenharPlaceholderCategoria(juce::Graphics&, juce::Rectangle<int> area, const std::string& extensao) const;
    void atualizarSelecaoDoLaco(const juce::MouseEvent& e);
    // Imagem que acompanha o cursor durante o arrasto, com o contador
    // ("142 arquivos") — sem ela o operador arrasta às cegas e não sabe se
    // está levando a seleção inteira ou só o item sob o cursor.
    juce::Image imagemDeArrasto(int quantidade) const;

    ProjetoAberto& projeto_;
    // Thread de Snapshot (§2): uma só, sequencial — dois snapshots
    // concorrentes só disputariam o banco pra um deles ser descartado.
    juce::ThreadPool poolSnapshot_{juce::ThreadPoolOptions{}.withThreadName("MatrizSnapshot")
                                   .withNumberOfThreads(1)
                                   .withDesiredThreadPriority(juce::Thread::Priority::low)};
    int geracaoSnapshot_ = 0;
    int loteAtualizacaoProfundidade_ = 0;
    bool refiltroAdiado_ = false;
    int versaoSnapshot_ = 0;
    bool refiltroAgendado_ = false;
    std::optional<juce::String> mensagemVazia_;
    void agendarRefiltroCoalescido();
    bool snapshotPendente_ = false;
    // Fix de performance (item 8): buscarItens() varre FTS5 + LIKE em várias
    // tabelas — na thread principal, cada tecla digitada travava a janela.
    // Mesmo padrão de geração do snapshot acima, pra uma busca mais nova não
    // ser sobrescrita pela resposta atrasada de uma busca já superada.
    int geracaoBusca_ = 0;

    std::vector<ItemResumo> itensTodos_;

    // A lista filtrada/agrupada guarda só ÍNDICES em itensTodos_ (antes: uma cópia de cada ItemResumo,
    // com todas as strings, a cada refiltro). Mantém a sintaxe de vetor — [], size(), empty(), front(),
    // for (auto& item : itensFiltrados_) — pros pontos de uso, mas o item é o MESMO objeto de itensTodos_.
    // Agrupado: itens do mesmo grupo sempre contíguos.
    class ListaFiltrada {
    public:
        explicit ListaFiltrada(std::vector<ItemResumo>& base) : base_(&base) {}
        size_t size() const { return indices_.size(); }
        bool empty() const { return indices_.empty(); }
        ItemResumo& operator[](size_t i) { return (*base_)[indices_[i]]; }
        const ItemResumo& operator[](size_t i) const { return (*base_)[indices_[i]]; }
        ItemResumo& front() { return (*this)[0]; }
        const ItemResumo& front() const { return (*this)[0]; }
        size_t indiceNaBase(size_t i) const { return indices_[i]; }
        std::vector<uint32_t>& indices() { return indices_; }
        void limpar() { indices_.clear(); }

        template <typename Lista, typename Item>
        class Iterador {
        public:
            Iterador(Lista* lista, size_t pos) : lista_(lista), pos_(pos) {}
            Item& operator*() const { return (*lista_)[pos_]; }
            Iterador& operator++() { ++pos_; return *this; }
            bool operator!=(const Iterador& o) const { return pos_ != o.pos_; }
        private:
            Lista* lista_;
            size_t pos_;
        };
        using Mut = Iterador<ListaFiltrada, ItemResumo>;
        using Const = Iterador<const ListaFiltrada, const ItemResumo>;
        Mut begin() { return Mut(this, 0); }
        Mut end() { return Mut(this, indices_.size()); }
        Const begin() const { return Const(this, 0); }
        Const end() const { return Const(this, indices_.size()); }

    private:
        std::vector<ItemResumo>* base_;
        std::vector<uint32_t> indices_;
    };
    ListaFiltrada itensFiltrados_{itensTodos_};

    // Textos de exibição de cada item, calculados UMA vez (no job do snapshot / em atualizarItemEmMemoria)
    // em vez de a cada paint: nome, extensão, ano, resumo do ORIGINAL SOURCE MEDIUM (parse de JSON!), etc.
    // Paralelo a itensTodos_ (mesmo índice).
    struct TextosCelula {
        juce::String nome;            // título, ou o nome original do arquivo
        juce::String extensao;        // extensão em MAIÚSCULAS ("" se não tem)
        juce::String ano;             // coluna DATE CREATED da lista ("" se não tem)
        juce::String sourceMedium;    // resumo do ORIGINAL SOURCE MEDIUM ("" = nenhum)
        juce::String caminho;         // caminho de origem (ou relativo) pra coluna PATH
        juce::String tamanho;         // tamanho formatado (coluna SIZE)
        juce::String categoria;       // categoria da lista (coluna TYPE)
        juce::String categoriaMaiuscula;
        juce::String duracao;         // duração do selo da grade ("" se não tem)
        juce::String subtituloGrade;  // "EXT  |  pasta" (sem o sufixo OFFLINE)
    };
    static TextosCelula calcularTextos(const ItemResumo& item);
    std::vector<TextosCelula> textosTodos_;
    const TextosCelula& textosDoFiltrado(size_t i) const { return textosTodos_[itensFiltrados_.indiceNaBase(i)]; }

    // Zebrado de "editado": desenhado UMA vez numa imagem (por tamanho de célula e escala de pixel)
    // e reaproveitado por todas as células, em vez de dezenas de linhas com clip por célula por paint.
    juce::Image zebraGrade_, zebraBarraLista_;
    juce::Rectangle<int> zebraGradeTamanho_, zebraBarraListaTamanho_;
    float zebraGradeEscala_ = 0.0f, zebraBarraListaEscala_ = 0.0f;
    const juce::Image& imagemZebraGrade(juce::Rectangle<int> bounds, float escala);
    const juce::Image& imagemZebraBarraLista(juce::Rectangle<float> barRect, float escala);

    // Repinta só a célula (e o que vaza dela: pilha de nest, anel de foco) em vez do componente todo.
    void repintarCelula(int indice);
    int indiceFiltradoDe(const std::string& itemId);
    // id -> índice nas duas listas (antes: find_if linear por evento/item, O(N²) em lote).
    // Reconstruídos sob demanda; quem troca/refaz a lista chama invalidarIndices*().
    // Cada acesso confere o id no índice, então um mapa velho nunca devolve o item errado.
    std::unordered_map<std::string, size_t> indiceTodos_, indiceFiltrados_;
    bool indiceTodosValido_ = false, indiceFiltradosValido_ = false;
    void invalidarIndiceTodos() { indiceTodosValido_ = false; }
    void invalidarIndiceFiltrados() { indiceFiltradosValido_ = false; }
    ItemResumo* itemEmTodos(const std::string& itemId);
    ItemResumo* itemEmFiltrados(const std::string& itemId);

    // Eventos recebidos com a grade escondida, já deduplicados: tipos "amplos" (sem id)
    // e, por tipo, os ids afetados. Aplicados por aplicarEventosPendentes().
    std::set<std::string> tiposAmplosPendentes_;
    std::map<std::string, std::set<std::string>> idsPendentesPorTipo_;
    bool aplicacaoPendenteAgendada_ = false;
    bool escondido() const { return getPeer() != nullptr && !isShowing(); }
    bool temEventosPendentes() const { return !tiposAmplosPendentes_.empty() || !idsPendentesPorTipo_.empty(); }
    void guardarEventoPendente(const EventoItemAlterado& e);
    void aplicarEventosPendentes();
    void agendarAplicarPendentes();
    void aplicarEvento(const EventoItemAlterado& e);
    std::vector<GrupoMosaico> grupos_;
    std::set<juce::String> filtrosTipoMidia_, filtrosEstado_, filtrosExtensao_, filtrosOrigem_;
    std::set<juce::String> filtrosContentType_, filtrosCollectionType_;
    FiltroDisponibilidade filtroDisponibilidade_ = FiltroDisponibilidade::All;
    std::optional<std::pair<int, int>> filtroFaixaAno_;                                          // ver definirFiltroFaixaAno
    juce::String filtroDataDe_, filtroDataAte_;
    ModoAgrupamento modoAgrupamento_ = ModoAgrupamento::Automatico;
    juce::StringArray buscaTermos_;
    std::optional<std::set<std::string>> buscaResultado_; // interseção (E lógico) do resultado de cada termo em buscaTermos_
    void recomputarBuscaResultado();
    Ordenacao ordenacao_ = Ordenacao::Codigo;
    std::optional<std::set<std::string>> filtroItens_; // seleção da árvore, ver definirFiltroItens

    juce::Rectangle<int> boundsBotaoAdicionarGrid(int indice) const;
    bool modoQuarentena_ = false;
    bool recarregarAoTerminarSnapshot_ = false;
    int colunas_ = 1;
    std::string selecionadoId_;       // âncora — última célula clicada (single-click), o que a ficha mostra
    std::set<std::string> selecionados_; // seleção múltipla completa (§3.3 — clique/Shift/Cmd)
    int indiceAncoraShift_ = -1;          // início do intervalo pra Shift+clique
    int indiceHover_ = -1;                // célula sob o cursor, -1 = nenhuma

    // Navegação por setas. O FOCO é a célula que o teclado move (anel próprio, diferente do destaque
    // da seleção; só aparece depois que o teclado é usado). Seta simples = foco + seleciona só ela
    // (como clicar); Shift+seta estende a seleção a partir da âncora; Espaço abre o preview.
    std::string focoId_;
    int indiceFoco_ = -1;
    bool focoVisivel_ = false;
    int indiceDoFoco() const;
    int indiceVizinho(int indice, int dx, int dy) const;
    void moverFoco(int dx, int dy, bool estender);
    void garantirCelulaVisivel(int indice);
    // A ficha e os contadores (pesados) só se atualizam quando a tecla para de repetir.
    struct NotificadorAdiado : juce::Timer {
        std::function<void()> aoDisparar;
        void timerCallback() override { stopTimer(); if (aoDisparar) aoDisparar(); }
    } notificadorSelecao_;
    bool arrastandoArquivo_ = false;

    // Laço de seleção (retângulo com o mouse a partir de área vazia).
    // `selecaoAntesDoLaco_` guarda o que já estava selecionado quando o laço
    // começou, pra Cmd/Ctrl poder SOMAR à seleção existente em vez de
    // substituí-la, e pra arrastar o laço pra trás desmarcar de novo.
    bool lacoAtivo_ = false;
    juce::Point<int> lacoInicio_;
    juce::Rectangle<int> lacoAtual_;
    std::set<std::string> selecaoAntesDoLaco_;
    bool lacoRecomecaAoArrastar_ = false;  // clique no vazio sem Shift/Cmd: só o arrasto limpa

    bool pendingDeselect_ = false;
    std::string pendingDeselectId_;

    // Item 9: nesta tela (ver definirPermiteArrastarParaFora) não há Vault/
    // pasta pra soltar arquivo arrastado — clicar numa miniatura e arrastar
    // vira seleção em laço a partir do próprio ponto do clique, em vez de
    // tentar iniciar um drag sem destino nenhum, que bloqueava o laço.
    bool permiteArrastarParaFora_ = true;
    std::set<std::string> selecaoAntesDoClique_;

    std::vector<SubpastaInfo> subpastas_;

    ModoVisao modoVisao_ = ModoVisao::Grade;
    TamanhoCelula tamanhoCelula_ = TamanhoCelula::Medio;
    bool destacarEditados_ = false;
    bool ocultarEditados_ = false;
    bool mostrarOcultos_ = true;
    bool ocultarNaoSelecionados_ = false;
    int celulaLargura_ = 168;
    int celulaAltura_ = 148;

    juce::ThreadPool poolMiniaturas_{2};
    std::unordered_map<std::string, juce::Image> cacheMiniaturas_;
    std::unordered_map<std::string, bool> emCarregamento_;
    std::unordered_map<std::string, bool> semMiniatura_; // cache negativo — não reconsulta o índice a cada repaint
    std::deque<std::string> ordemCache_;
    static constexpr size_t kCapacidadeCache = 400;
    // Área da imagem da MAIOR célula (largura 320 no zoom máximo; ver definirTamanhoContinuo),
    // em pixels lógicos: 320-8 de largura; altura 0,88*320 = 281, menos 8 de margem e 40 de texto.
    static constexpr int kLarguraMaximaMiniatura = 312;
    static constexpr int kAlturaMaximaMiniatura = 233;
    juce::CriticalSection cacheLock_;

    // Inline rename (click on title area of selected item)
    void timerCallback() override;
    void iniciarEdicaoInline(int indice);
    void confirmarEdicaoInline();
    void cancelarEdicaoInline();
    juce::Rectangle<int> areaTituloDaCelula(int indice) const;
    std::unique_ptr<juce::TextEditor> editorInline_;
    int indiceEditando_ = -1;
    bool clicouNaTituloDeItemSelecionado_ = false;
};

} // namespace matriz::ui
