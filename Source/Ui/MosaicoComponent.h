#pragma once

#include <JuceHeader.h>

#include <deque>
#include <optional>
#include <set>
#include <unordered_map>

#include "../Ingest/LeituraTecnica.h"
#include "ProjetoAberto.h"

// Mosaico do acervo (§11.2, B.1.2/B.2). Virtualizado desde o início: o
// Component inteiro é UM objeto independente da quantidade de itens —
// paint() só desenha o que g.getClipBounds() pede (o Viewport que o contém
// só pede a área visível), e miniatura só é carregada sob demanda pras
// células realmente pintadas, em background, com placeholder enquanto
// carrega. Testado com 10 mil itens sintéticos (ver
// tools/ui_selftest/main.cpp) — não existe nenhum trabalho O(total de
// itens) por frame, só O(células visíveis).

namespace matriz::ui {

enum class Ordenacao { Codigo, Titulo, Estado, Atualizado };

// Um cabeçalho de seção do mosaico (Parte 1 da correção de fluxo, §3.5):
// Archive agrupa por tipo de mídia, Catalog por artista/lançamento. Um
// grupo é sempre um intervalo CONTÍGUO de itensFiltrados_ — nunca precisa
// tocar os itens de dentro pra desenhar o cabeçalho ou localizar uma
// célula, o que mantém paint() em O(células visíveis), não O(total de
// itens), mesmo com muitos grupos.
struct GrupoMosaico {
    juce::String rotulo; // já inclui a contagem, ex.: "Reel tapes — 12"
    int indiceInicio = 0;
    int quantidade = 0;
    int yTopo = 0;  // topo do cabeçalho
    int yItens = 0; // topo da primeira linha de células
    int linhas = 0;
};

class MosaicoComponent : public juce::Component {
public:
    explicit MosaicoComponent(ProjetoAberto& projeto);
    ~MosaicoComponent() override;

    // Recarrega a lista de itens do banco e reaplica filtro/ordenação atuais.
    void recarregar();

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
    void limparFiltros(); // chips + busca + faixa de ano — não mexe no filtro de pasta da árvore (eixo independente)
    const std::set<juce::String>& filtrosTipoMidiaAtivos() const { return filtrosTipoMidia_; }
    const std::set<juce::String>& filtrosEstadoAtivos() const { return filtrosEstado_; }
    const std::set<juce::String>& filtrosExtensaoAtivos() const { return filtrosExtensao_; }
    const std::set<juce::String>& filtrosOrigemAtivos() const { return filtrosOrigem_; }

    // Faixa de ano ("1978-1985", item 4.3) — E com os demais filtros. Item
    // sem "ano" preenchido nunca aparece dentro de uma faixa ativa.
    void definirFiltroFaixaAno(int anoDe, int anoAte);
    void limparFiltroFaixaAno();
    std::optional<std::pair<int, int>> filtroFaixaAnoAtivo() const { return filtroFaixaAno_; }

    // Eixo de agrupamento do mosaico (item 4.3 — "ano é o eixo padrão de
    // agrupamento na grade"). Automatico é o comportamento já existente
    // (tipo de mídia no Archive, artista/lançamento no Catalog).
    enum class ModoAgrupamento { Automatico, PorAno };
    void definirModoAgrupamento(ModoAgrupamento modo);
    ModoAgrupamento modoAgrupamentoAtual() const { return modoAgrupamento_; }

    // Busca (Acréscimos §10.1): código/título, campo de ficha e assunto —
    // consulta o banco via ProjetoAberto::buscarItens a cada chamada (OCR/
    // transcrição não existem ainda, gap declarado). "" limpa a busca.
    void definirBusca(const juce::String& texto);
    const juce::String& buscaAtual() const { return buscaTexto_; }

    void definirOrdenacao(Ordenacao ordenacao);

    // Filtro por seleção da árvore Origem/Acervo (Reorientação completa
    // §8.1 — "clicar numa pasta filtra a grade"). nullopt = sem filtro de
    // pasta (todos os itens, sujeitos aos outros filtros normalmente).
    void definirFiltroItens(std::optional<std::set<std::string>> itemIds);

    void selecionarItem(const std::string& itemId);
    const std::string& itemSelecionado() const { return selecionadoId_; }

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

    // Disparado sempre que o conjunto selecionado muda, por qualquer
    // caminho (clique, Shift, Cmd, selecionarTodos, limparSelecao) — a
    // barra de seleção e a ficha lateral acompanham por aqui.
    std::function<void()> aoMudarSelecao;

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

    // Tamanho de célula ajustável (§3.3 — "controle de tamanho no
    // rodapé"). Só os três tamanhos de grade nesta etapa; visão em lista
    // (layout de linha única, bem diferente de uma grade) fica pra depois
    // do ponto de parada — não declarado aqui pra não sugerir que existe.
    enum class TamanhoCelula { Pequeno, Medio, Grande };
    void definirTamanhoCelula(TamanhoCelula tamanho);
    TamanhoCelula tamanhoCelulaAtual() const { return tamanhoCelula_; }

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
    // Estado vazio inteiro é clicável, além de alvo de drop (§3.2) — abre o
    // mesmo seletor de arquivo/pasta que o botão "Add material".
    std::function<void()> aoClicarEstadoVazio;

    // Botão direito na grade. O MainComponent monta e executa o menu (é ele
    // que tem os ganchos pra ficha, filtros e recarga) — o mosaico só avisa
    // que houve o gesto e sobre QUAIS itens, já resolvida a regra de "clicou
    // fora da seleção? então a seleção passa a ser só esse item".
    std::function<void(std::vector<std::string> itemIds)> aoPedirMenuContexto;

    static constexpr int kAlturaCabecalhoGrupo = 26;
    static constexpr int kEspacoEntreGrupos = 6;

private:
    void aplicarFiltrosEOrdenacao();
    juce::String rotuloGrupoArchive(const ItemResumo& item) const;
    void recalcularLayout();
    juce::Rectangle<int> boundsDaCelula(int indice) const;
    int indiceNaPosicao(juce::Point<int> pos) const;
    const GrupoMosaico* grupoNaPosicaoY(int y) const;
    juce::Colour corDoEstado(const std::string& estado) const;
    const juce::Image* miniaturaCache(const std::string& itemId);
    void pedirCarregamentoMiniatura(const std::string& itemId);
    void desenharPlaceholderCategoria(juce::Graphics&, juce::Rectangle<int> area, const std::string& extensao) const;
    void atualizarSelecaoDoLaco(const juce::MouseEvent& e);
    // Imagem que acompanha o cursor durante o arrasto, com o contador
    // ("142 arquivos") — sem ela o operador arrasta às cegas e não sabe se
    // está levando a seleção inteira ou só o item sob o cursor.
    juce::Image imagemDeArrasto(int quantidade) const;

    ProjetoAberto& projeto_;
    std::vector<ItemResumo> itensTodos_;
    std::vector<ItemResumo> itensFiltrados_; // agrupado — itens do mesmo grupo sempre contíguos
    std::vector<GrupoMosaico> grupos_;
    std::set<juce::String> filtrosTipoMidia_, filtrosEstado_, filtrosExtensao_, filtrosOrigem_; // chips, ver alternarFiltro*
    std::optional<std::pair<int, int>> filtroFaixaAno_;                                          // ver definirFiltroFaixaAno
    ModoAgrupamento modoAgrupamento_ = ModoAgrupamento::Automatico;
    juce::String buscaTexto_;
    std::optional<std::set<std::string>> buscaResultado_; // resultado de projeto_.buscarItens(buscaTexto_), recalculado a cada definirBusca
    Ordenacao ordenacao_ = Ordenacao::Codigo;
    std::optional<std::set<std::string>> filtroItens_; // seleção da árvore, ver definirFiltroItens

    int colunas_ = 1;
    std::string selecionadoId_;       // âncora — última célula clicada (single-click), o que a ficha mostra
    std::set<std::string> selecionados_; // seleção múltipla completa (§3.3 — clique/Shift/Cmd)
    int indiceAncoraShift_ = -1;          // início do intervalo pra Shift+clique
    int indiceHover_ = -1;                // célula sob o cursor, -1 = nenhuma
    bool arrastandoArquivo_ = false;

    // Laço de seleção (retângulo com o mouse a partir de área vazia).
    // `selecaoAntesDoLaco_` guarda o que já estava selecionado quando o laço
    // começou, pra Cmd/Ctrl poder SOMAR à seleção existente em vez de
    // substituí-la, e pra arrastar o laço pra trás desmarcar de novo.
    bool lacoAtivo_ = false;
    juce::Point<int> lacoInicio_;
    juce::Rectangle<int> lacoAtual_;
    std::set<std::string> selecaoAntesDoLaco_;

    TamanhoCelula tamanhoCelula_ = TamanhoCelula::Medio;
    int celulaLargura_ = 168;
    int celulaAltura_ = 148;

    juce::ThreadPool poolMiniaturas_{2};
    std::unordered_map<std::string, juce::Image> cacheMiniaturas_;
    std::unordered_map<std::string, bool> emCarregamento_;
    std::unordered_map<std::string, bool> semMiniatura_; // cache negativo — não reconsulta o índice a cada repaint
    std::deque<std::string> ordemCache_;
    static constexpr size_t kCapacidadeCache = 400;
    juce::CriticalSection cacheLock_;
};

} // namespace matriz::ui
