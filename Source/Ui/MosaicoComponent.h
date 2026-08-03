#pragma once

#include <JuceHeader.h>

#include <deque>
#include <unordered_map>

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

class MosaicoComponent : public juce::Component, public juce::FileDragAndDropTarget {
public:
    explicit MosaicoComponent(ProjetoAberto& projeto);
    ~MosaicoComponent() override;

    // Recarrega a lista de itens do banco e reaplica filtro/ordenação atuais.
    void recarregar();

    void definirFiltroEstado(const juce::String& estado); // "" = todos
    void definirFiltroTipo(const juce::String& tipo);       // "" = todos
    void definirBusca(const juce::String& texto);
    void definirOrdenacao(Ordenacao ordenacao);

    void selecionarItem(const std::string& itemId);
    const std::string& itemSelecionado() const { return selecionadoId_; }

    int totalItensCarregados() const { return static_cast<int>(itensTodos_.size()); }
    int totalItensVisiveis() const { return static_cast<int>(itensFiltrados_.size()); }

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

    // juce::FileDragAndDropTarget — arrastar arquivos do Finder direto pro
    // mosaico ingere cada um como item novo (§7.1: não trava esperando ficha
    // completa, o arquivo entra e a leitura técnica roda na hora).
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { arrastandoArquivo_ = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { arrastandoArquivo_ = false; repaint(); }

    std::function<void(const std::string& itemId)> aoSelecionar;
    std::function<void(const juce::Array<juce::File>&)> aoArquivosSoltos;

    static constexpr int kCelulaLargura = 168;
    static constexpr int kCelulaAltura = 148;
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

    ProjetoAberto& projeto_;
    std::vector<ItemResumo> itensTodos_;
    std::vector<ItemResumo> itensFiltrados_; // agrupado — itens do mesmo grupo sempre contíguos
    std::vector<GrupoMosaico> grupos_;
    juce::String filtroEstado_, filtroTipo_, buscaTexto_;
    Ordenacao ordenacao_ = Ordenacao::Codigo;

    int colunas_ = 1;
    std::string selecionadoId_;
    bool arrastandoArquivo_ = false;

    juce::ThreadPool poolMiniaturas_{2};
    std::unordered_map<std::string, juce::Image> cacheMiniaturas_;
    std::unordered_map<std::string, bool> emCarregamento_;
    std::unordered_map<std::string, bool> semMiniatura_; // cache negativo — não reconsulta o índice a cada repaint
    std::deque<std::string> ordemCache_;
    static constexpr size_t kCapacidadeCache = 400;
    juce::CriticalSection cacheLock_;
};

} // namespace matriz::ui
