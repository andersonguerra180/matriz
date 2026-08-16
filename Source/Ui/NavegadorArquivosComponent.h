#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Navegador de arquivos estilo Finder do macOS, dentro do aplicativo
// (item 3). Coexiste com arrastar direto do Finder pra janela — é uma
// segunda porta de entrada, não substituta: quem prefere navegar dentro do
// app usa este, quem prefere arrastar continua arrastando.
//
// O navegador NÃO MODIFICA NADA (item 3.4): não move, não renomeia, não
// apaga. Só lê o disco e devolve a seleção pra quem chamou.
//
// Visão em colunas é o padrão (item 3.2), com alternativa em lista e em
// ícones. Cada coluna é o conteúdo de uma pasta; selecionar uma pasta abre
// a coluna seguinte à direita, exatamente como o Finder.

namespace matriz::ui {

class NavegadorArquivosComponent : public juce::Component {
public:
    NavegadorArquivosComponent();
    ~NavegadorArquivosComponent() override;

    enum class Visao { Colunas, Lista, Icones };
    void definirVisao(Visao v);
    Visao visaoAtual() const { return visao_; }

    // Navega até `pasta`, deixando as colunas ancestrais visíveis à esquerda.
    void irPara(const juce::File& pasta);
    juce::File pastaAtual() const { return caminho_.empty() ? juce::File() : caminho_.back(); }

    void voltar();
    void avancar();
    void subirUmNivel();
    bool podeVoltar() const { return indiceHistorico_ > 0; }
    bool podeAvancar() const { return indiceHistorico_ + 1 < static_cast<int>(historico_.size()); }

    // Busca dentro do navegador (item 3.2) — filtra a pasta atual E as
    // subpastas. "" limpa.
    void definirBusca(const juce::String& texto);
    const juce::String& buscaAtual() const { return busca_; }

    // Filtro por tipo de arquivo (item 3.2 — "achar só áudio ou só imagem
    // numa pasta bagunçada"). Vazio = todos.
    enum class FiltroTipo { Todos, Audio, Video, Imagem, Documento };
    void definirFiltroTipo(FiltroTipo f);
    FiltroTipo filtroTipoAtual() const { return filtroTipo_; }

    enum class Ordenacao { Nome, Data, Tamanho, Tipo };
    void definirOrdenacao(Ordenacao o);

    // Seleção múltipla (item 3.2): clique, Shift, Cmd/Ctrl e retângulo de
    // arrasto — o mesmo vocabulário de gesto do mosaico.
    const std::vector<juce::File>& selecao() const { return selecao_; }
    void limparSelecao();

    // Contagem e tamanho da seleção, já formatados ("142 files, 8.4 GB") —
    // visíveis ANTES de adicionar (item 3.2). Pasta selecionada conta
    // recursivamente o que há dentro dela.
    juce::String resumoSelecao() const;
    int totalArquivosNaSelecao() const;
    juce::int64 tamanhoTotalDaSelecao() const;

    // Disparado quando a seleção muda — quem hospeda o navegador usa pra
    // habilitar/desabilitar o botão ADD TO BACKUP e atualizar o resumo.
    std::function<void()> aoMudarSelecao;
    // Disparado por clique duplo num arquivo (atalho pra "adicionar isto").
    std::function<void()> aoConfirmarSelecao;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Introspecção pra teste — o harness de UI dirige o navegador sem mouse.
    int totalColunasParaTeste() const { return static_cast<int>(colunas_.size()); }
    // A busca é assíncrona (I1) — o harness bombeia mensagens até isto virar
    // false antes de conferir o resultado.
    bool buscaEmAndamentoParaTeste() const { return buscaEmAndamento_; }
    std::vector<juce::File> entradasVisiveisParaTeste(int coluna) const;
    void selecionarParaTeste(const juce::File& arquivo, bool somarASelecao = false);

    static constexpr int kLarguraColuna = 200;
    static constexpr int kAlturaLinha = 22;
    static constexpr int kAlturaToolbar = 26;

private:
    struct Entrada {
        juce::File arquivo;
        bool ehPasta = false;
        juce::int64 tamanho = 0;
        juce::Time modificado;
    };
    struct Coluna {
        juce::File pasta;
        std::vector<Entrada> entradas;
        int scrollY = 0;
    };

    void reconstruirColunas();
    // Listagem de UM nível — o que o Finder faz a cada clique. Rápida e
    // limitada, roda na message thread sem problema.
    std::vector<Entrada> lerPasta(const juce::File& pasta) const;
    // Busca recursiva na pasta atual (item 3.2). NUNCA na message thread:
    // uma subárvore grande trava a interface por minutos (I1). Dispara um job
    // e entrega o resultado por callAsync.
    void dispararBuscaRecursiva(const juce::File& pasta, const juce::String& termo);
    bool passaFiltros(const juce::File& arquivo, bool ehPasta) const;
    // Estáticas porque o job de busca roda fora da message thread e não pode
    // tocar em estado do componente — recebe filtro e ordenação por cópia.
    static bool tipoCombina(const juce::File& arquivo, FiltroTipo filtro);
    static void ordenarEntradas(std::vector<Entrada>& entradas, Ordenacao ordenacao);
    void registrarNoHistorico(const juce::File& pasta);
    int colunaNaPosicao(juce::Point<int> p) const;
    int indiceEntradaNaPosicao(int coluna, juce::Point<int> p) const;
    void atualizarSelecaoDoLaco(const juce::MouseEvent& e);
    void notificarSelecao();
    void dispararMedidaDePasta(const juce::String& caminho);

    std::vector<juce::File> caminho_;  // raiz .. pasta atual
    std::vector<Coluna> colunas_;
    std::vector<juce::File> selecao_;
    std::vector<juce::File> historico_;
    int indiceHistorico_ = -1;

    Visao visao_ = Visao::Colunas;
    FiltroTipo filtroTipo_ = FiltroTipo::Todos;
    Ordenacao ordenacao_ = Ordenacao::Nome;
    juce::String busca_;

    bool lacoAtivo_ = false;
    juce::Point<int> lacoInicio_;
    juce::Rectangle<int> lacoAtual_;
    std::vector<juce::File> selecaoAntesDoLaco_;
    juce::File ancoraShift_;

    // Cache de tamanho recursivo de pasta — calcular na hora numa pasta com
    // 100 mil arquivos travaria a UI a cada clique.
    mutable std::map<juce::String, std::pair<int, juce::int64>> cacheTamanhoPasta_;
    std::set<juce::String> pastasEmAndamento_;
    juce::ThreadPool threadPool_{1};

    // Cada busca recebe um número; resultado que chega com número velho é
    // descartado. Sem isso, digitar rápido faz a resposta de um termo antigo
    // sobrescrever a do termo atual.
    int geracaoBusca_ = 0;
    bool buscaEmAndamento_ = false;

    juce::TextButton btnVoltar_{"<"}, btnAvancar_{">"}, btnSubir_{"^"};
    void atualizarBotoesNavegacao();

    // Teto de resultados de busca: acima disso a coluna vira uma lista que
    // ninguém lê e a varredura vira custo puro.
    static constexpr int kMaxResultadosBusca = 2000;
};

} // namespace matriz::ui
