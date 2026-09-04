#pragma once

#include <JuceHeader.h>

#include <functional>

// Barra de ferramentas superior — as ações reais do app, sempre visíveis.
//
// Antes desta barra, TUDO o que o operador precisa fazer (adicionar
// arquivos, abrir os detalhes, gravar o backup) só existia no menu do
// sistema. Quem abria o app pela primeira vez via uma grade vazia e nenhum
// botão: o app não se explicava sozinho. A regra aqui é que nenhuma ação
// do fluxo principal pode morar só num menu — o menu continua existindo,
// mas como atalho pra quem já sabe, nunca como único caminho.
//
// Só desenha e emite callbacks; não conhece ProjetoAberto nem
// MosaicoComponent. Quem liga os fios é o MainComponent.

namespace matriz::ui {

class BarraFerramentasComponent : public juce::Component {
public:
    BarraFerramentasComponent();

    // Ações — todas opcionais; um callback vazio simplesmente não faz nada.
    std::function<void()> aoAdicionarArquivos;
    std::function<void()> aoAbrirNavegador;
    std::function<void()> aoAlternarDetalhes;
    std::function<void()> aoFazerBackup;
    std::function<void(const juce::String&)> aoBuscar;
    std::function<void(int)> aoMudarTamanho; // 0 = pequeno, 1 = médio, 2 = grande
    std::function<void(bool)> aoAlternarModoVisao; // true = lista, false = grade
    std::function<void(bool)> aoAlternarEstruturaOrigem;
    std::function<void(bool)> aoAlternarEstruturaBackup;
    std::function<void(const std::string&)> aoMudarFiltroHorizontal;
    std::function<void(const std::string&)> aoMudarFiltroStatus;
    std::function<void(bool)> aoAlternarDestacarEditados;

    // Toggle zebra marking on/off
    void definirDestacarEditados(bool ativo);
    bool destacarEditados() const { return destacarEditados_; }

    // Contagem "X de Y arquivos" no meio da barra — é o feedback de que o
    // filtro/busca fez alguma coisa, sem o operador ter que contar célula.
    void definirContagem(int visiveis, int total, bool filtroAtivo);

    // Reflete na barra o estado da ficha lateral (aberta/fechada), pra o
    // botão "Detalhes" mostrar que está ligado em vez de parecer inerte.
    void definirDetalhesAbertos(bool abertos);

    // Escreve no campo de busca SEM disparar aoBuscar — usado quando outra
    // parte da UI mexe na busca (limpar filtros, aplicar uma coleção
    // salva) e o campo precisa acompanhar sem realimentar o mosaico.
    void definirTextoBuscaSemNotificar(const juce::String& texto);

    // Actively select one of the horizontal filters from the code
    void definirFiltroHorizontalAtivo(const std::string& chave);
    void definirFiltroStatusAtivo(const std::string& status);

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kAltura = 88;

private:
    void aplicarEstiloBotao(juce::TextButton& botao, bool primario) const;
    void marcarTamanhoAtivo(int indice);
    void atualizarBotoesFiltroHorizontal();
    void atualizarBotoesFiltroStatus();

    std::unique_ptr<juce::TextButton> botaoAdicionar_;
    std::unique_ptr<juce::TextButton> botaoNavegar_;
    std::unique_ptr<juce::TextEditor> campoBusca_;
    std::unique_ptr<juce::TextButton> btnLimparBusca_;
    std::unique_ptr<juce::Label> labelContagem_;
    std::unique_ptr<juce::TextButton> botaoTamanhoP_, botaoTamanhoM_, botaoTamanhoG_;
    std::unique_ptr<juce::TextButton> botaoModoVisao_;
    std::unique_ptr<juce::TextButton> botaoDetalhes_;
    std::unique_ptr<juce::TextButton> botaoBackup_;
    std::unique_ptr<juce::TextButton> botaoEstruturaOrigem_;
    std::unique_ptr<juce::TextButton> botaoEstruturaBackup_;
    std::unique_ptr<juce::TextButton> botaoAdvanced_;

    // Horizontal filters
    std::unique_ptr<juce::TextButton> btnAllAssets_;
    std::unique_ptr<juce::TextButton> btnAudio_;
    std::unique_ptr<juce::TextButton> btnVideo_;
    std::unique_ptr<juce::TextButton> btnImage_;
    std::unique_ptr<juce::TextButton> btnDocument_;
    std::string filtroHorizontalAtivo_ = "all";

    // Status filters (ALL, ONLINE, OFFLINE)
    std::unique_ptr<juce::Label> lblStatusFilter_;
    std::unique_ptr<juce::TextButton> btnStatusAll_;
    std::unique_ptr<juce::TextButton> btnStatusOnline_;
    std::unique_ptr<juce::TextButton> btnStatusOffline_;
    std::string filtroStatusAtivo_ = "all";

    // Mark edited items toggle button
    std::unique_ptr<juce::TextButton> btnDestacarEditados_;
    bool destacarEditados_ = true;

    int tamanhoAtivo_ = 1; // médio, mesmo padrão de MosaicoComponent
    bool detalhesAbertos_ = false;
    bool modoLista_ = false;
    bool mostrarEstruturaOrigem_ = false;
    bool mostrarEstruturaBackup_ = false;
    bool mostrarAdvanced_ = false;
};

} // namespace matriz::ui
