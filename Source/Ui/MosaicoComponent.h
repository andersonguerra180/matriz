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

private:
    void aplicarFiltrosEOrdenacao();
    void recalcularLayout();
    juce::Rectangle<int> boundsDaCelula(int indice) const;
    int indiceNaPosicao(juce::Point<int> pos) const;
    juce::Colour corDoEstado(const std::string& estado) const;
    const juce::Image* miniaturaCache(const std::string& itemId);
    void pedirCarregamentoMiniatura(const std::string& itemId);

    ProjetoAberto& projeto_;
    std::vector<ItemResumo> itensTodos_;
    std::vector<ItemResumo> itensFiltrados_;
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
