#pragma once

#include <JuceHeader.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ColunaCardsLayout.h"
#include "../Model/MergeFichas.h"

namespace matriz::ui {

class ProjetoAberto;

// Revisão em lote dos conflitos de merge (valores que perderam ao juntar
// duplicatas / pacotes), no desenho da aba Duplicates: coluna de cards à
// esquerda (CONFLICTS, FILTERS, SELECTION, BATCH ACTIONS) e, à direita, um
// card por item × campo com miniatura, tag colorida do campo e os valores lado
// a lado. Leitura e gravação no poolMerge_ do ProjetoAberto (uma transação por
// aplicação, com Undo); miniaturas num pool próprio, só das que estão na tela.
class RevisaoConflitosMergeComponent : public juce::Component {
public:
    // escopo vazio = todos os conflitos pendentes do projeto.
    RevisaoConflitosMergeComponent(ProjetoAberto& projeto, std::set<std::string> escopo);
    ~RevisaoConflitosMergeComponent() override;

    // Abre a janela (moldura, modal) com os conflitos do escopo.
    static void abrir(ProjetoAberto& projeto, std::set<std::string> escopo = {});

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Cor da tag de cada tipo de campo (datas, GEO, classificação, texto).
    static juce::Colour corDoCampo(const std::string& campo);

    // Self-test (--selftest-lote).
    bool ocupadoParaTeste() const { return carregando_ || ocupado_; }
    size_t cardsParaTeste() const { return entradas_.size(); }
    size_t visiveisParaTeste() const { return visiveis_.size(); }
    int opcoesDeCampoParaTeste() const { return cbCampo_.getNumItems(); }
    void filtrarParaTeste(int idValores, int idOrigem) {
        cbValores_.setSelectedId(idValores, juce::sendNotificationSync);
        cbOrigem_.setSelectedId(idOrigem, juce::sendNotificationSync);
    }
    void selecionarVisiveisParaTeste() { btnSelecionarTodos_.triggerClick(); }
    size_t selecionadosParaTeste() const { return selecionadosVisiveis().size(); }
    bool loteHabilitadoParaTeste() const { return btnUsarOutroSel_.isEnabled(); }
    void usarOutroParaTeste() { aplicarSelecionados(2); }

private:
    struct Entrada {
        matriz::model::merge::GrupoConflito grupo;
        int categoria = 7;      // id do filtro de tipo de mídia (2 imagem ... 7 outros)
        bool selecionado = false;
        int escolha = 0;        // 0 = valor atual, k = alternativas[k - 1]
    };

    class CardComponent;
    class ListaComponent;
    class ViewportLista;

    void recarregar();
    void aoCarregar(std::vector<matriz::model::merge::GrupoConflito> grupos, std::map<std::string, juce::String> miniaturas);
    void reconstruirFiltros();
    void aplicarFiltros();
    void atualizarBotoes();
    void pedirMiniaturas(juce::Rectangle<int> areaVisivel);

    // Cada par = (índice em entradas_, valor que fica). Pergunta antes de um lote.
    void aplicar(std::vector<std::pair<size_t, std::string>> escolhas, bool perguntar);
    void aplicarSelecionados(int modo);  // 1 manter atual, 2 usar o outro, 3 usar o escolhido em cada card
    void abrirMenuUsarValor();
    std::vector<size_t> selecionadosVisiveis() const;

    ProjetoAberto& projeto_;
    std::weak_ptr<bool> vivo_;
    std::set<std::string> escopo_;
    bool carregando_ = true;
    bool ocupado_ = false;  // gravação em curso: botões desligados

    std::vector<Entrada> entradas_;
    std::vector<size_t> visiveis_;  // índices de entradas_ que passam nos filtros, na ordem da lista
    std::map<std::string, juce::String> caminhosMiniatura_;
    std::map<std::string, juce::Image> miniaturas_;
    std::set<std::string> miniaturasPedidas_;
    juce::ThreadPool poolMiniaturas_{1};

    ColunaCardsLayout coluna_;
    juce::Label lblStatus_;
    juce::TextButton btnRecarregar_;
    juce::Label lblCampo_, lblMidia_, lblPasta_, lblValores_, lblOrigem_;
    juce::ComboBox cbCampo_, cbMidia_, cbPasta_, cbValores_, cbOrigem_;
    std::vector<std::string> opcoesCampo_, opcoesPasta_;  // valor de cada item dos combos (índice = id - 2)
    juce::TextButton btnSelecionarTodos_, btnSelecionarNenhum_;
    juce::Label lblSelecionados_;
    juce::TextButton btnManterSel_, btnUsarOutroSel_, btnUsarEscolhaSel_, btnUsarValor_;

    std::unique_ptr<ViewportLista> viewport_;
    std::unique_ptr<ListaComponent> lista_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RevisaoConflitosMergeComponent)
};

}  // namespace matriz::ui
