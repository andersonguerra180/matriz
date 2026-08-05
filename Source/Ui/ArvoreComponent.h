#pragma once

#include <JuceHeader.h>

#include <optional>

#include "ProjetoAberto.h"

// Árvore lateral Origem/Acervo (Reorientação completa §5, §8.1). Alterna
// entre duas visões via abas:
//   ORIGEM — espelha a estrutura real de disco de onde cada arquivo veio,
//            intocada, só pra navegar e localizar (§5.1, §4.2).
//   ACERVO — estrutura virtual que o operador monta por cima (§5.2):
//            criar/renomear/apagar pasta, arrastar material pra dentro.
//            Puro planejamento no banco — nenhuma linha aqui move, copia ou
//            renomeia nada em disco (§5.3); a consolidação (item 10) é que
//            materializa.
// Clicar numa pasta filtra a grade (§8.1) via aoSelecionarNo. Simplificação
// desta primeira versão, deliberada por tempo: a árvore inteira é sempre
// desenhada expandida (sem colapsar/expandir galho por galho) — ver nota em
// ProjetoAberto::arvoreOrigem/arvoreAcervo.

namespace matriz::ui {

class ArvoreComponent : public juce::Component, public juce::DragAndDropTarget {
public:
    explicit ArvoreComponent(ProjetoAberto& projeto);

    // Reconsulta a árvore da aba atual (Origem ou Acervo) e redesenha.
    // Chamado ao trocar de aba, após qualquer ingest, e após qualquer
    // operação de organização (criar/renomear/apagar pasta, soltar item).
    void recarregar();

    enum class Aba { Origem, Acervo };
    void definirAba(Aba aba);
    Aba abaAtual() const { return aba_; }

    // Botão "+" fica no cabeçalho fixo de MainComponent, fora do Viewport
    // que rola este Component — evita que o cabeçalho suma ao rolar uma
    // árvore grande. Só faz sentido na aba Acervo (chamado de fora).
    void criarPastaDeTopoNivel();

    // nullopt = "todos os materiais" (nenhum filtro de pasta selecionado).
    std::function<void(std::optional<std::set<std::string>> itemIdsFiltrados)> aoSelecionarNo;
    // Disparado depois de qualquer operação que mude a organização do
    // Acervo (criar/renomear/apagar pasta, soltar item) — MainComponent usa
    // isto só pra saber que pode haver algo novo pra refletir em outro
    // lugar (hoje, nada mais depende disso; existe pra não deixar a
    // integração futura sem gancho).
    std::function<void()> aoMudarOrganizacao;

    // Ligado/desligado do "incluir subpastas" da grade (item 2): desligado,
    // clicar numa pasta mostra só o que está NELA; ligado (padrão), mostra
    // também tudo o que está nas subpastas.
    void definirIncluirSubpastas(bool incluir);
    bool incluirSubpastas() const { return incluirSubpastas_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    // Arrastar uma pasta da EXPLORER pra BACKUP é o gesto principal de
    // organização — leva a subárvore inteira junto (ver itemDropped).
    void mouseDrag(const juce::MouseEvent&) override;

    // juce::DragAndDropTarget — recebe o(s) item(ns) arrastado(s) da grade
    // (MosaicoComponent::mouseDrag), só aceito soltar em cima de uma pasta
    // real do Acervo (não na raiz sintética nem em "não organizados").
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

private:
    struct LinhaAchatada {
        const ProjetoAberto::NoArvore* no;
        int profundidade;
        bool ehPseudoTodos = false; // primeira linha sintética "Todos os materiais"
    };

    void reconstruirLinhas();
    void notificarSelecao();
    void soltarPastaDaExplorer(const std::string& pastaDestinoId);
    void achatar(const ProjetoAberto::NoArvore& no, int profundidade);
    int indiceLinhaNaPosicao(int y) const;
    juce::Rectangle<int> boundsDaLinha(int indice) const;
    void abrirMenuContexto(int indiceLinha);

    ProjetoAberto& projeto_;
    Aba aba_ = Aba::Origem; // origem tem conteúdo desde o primeiro ingest; acervo começa vazio até o operador organizar
    ProjetoAberto::NoArvore raiz_;
    std::vector<LinhaAchatada> linhas_;
    const ProjetoAberto::NoArvore* selecionado_ = nullptr; // ponteiro em linhas_/raiz_, válido até o próximo recarregar()

    int linhaAlvoDrop_ = -1; // feedback visual durante o arrasto, -1 = nenhuma
    bool incluirSubpastas_ = true;

    // Subárvore da EXPLORER capturada no início do arrasto. Precisa ser uma
    // CÓPIA, não um ponteiro pra dentro de raiz_: assim que o arrasto começa
    // trocamos pra aba BACKUP (não há onde soltar na EXPLORER), e isso chama
    // recarregar(), que substitui raiz_ inteira e invalidaria o ponteiro.
    std::optional<ProjetoAberto::NoArvore> subarvoreArrastada_;

    static constexpr int kAlturaLinha = 26;
    static constexpr int kIndentacao = 16;
};

} // namespace matriz::ui
