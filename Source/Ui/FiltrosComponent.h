#pragma once

#include <JuceHeader.h>

#include <map>

#include "MosaicoComponent.h"
#include "ProjetoAberto.h"

// Filtros (chips) + busca + coleções inteligentes (Acréscimos §10) — fica
// embaixo da árvore no painel esquerdo (§9.1: "Árvore / Filtros / Busca"
// empilhados na mesma coluna). Opera diretamente sobre um MosaicoComponent
// (mesmo padrão de ArvoreComponent segurando ProjetoAberto&: quem lê/edita
// o estado de filtro é este Component, não um emaranhado de callbacks).
//
// Simplificação desta etapa, deliberada: as contagens de cada chip são
// GLOBAIS (contagensPorTipoMidia/Estado/Extensao, recalculadas só em
// recarregar()), não recalculadas dinamicamente em função dos OUTROS
// filtros já ativos (facetamento cruzado) — um chip mostra "quantos itens
// têm esse valor no acervo inteiro", não "quantos apareceriam se este chip
// também fosse marcado". Documentado, não escondido.

namespace matriz::ui {

class FiltrosComponent : public juce::Component, public juce::DragAndDropTarget {
public:
    FiltrosComponent(ProjetoAberto& projeto, MosaicoComponent& mosaico);

    // Reconsulta contagens e coleções salvas, e redesenha.
    //
    // As agregações que isto faz (contagem por tipo/estado/extensão/origem,
    // Vaults, e o UNION das seis views de coleção embutida) custam SEGUNDOS
    // num acervo de milhares de itens — medido em 2 s com 1.200 itens. Rodar
    // na message thread congelava a janela a cada arquivo ingerido, que é o
    // que I1 proíbe. Agora a consulta vai pra background e a message thread
    // só recebe o resultado pronto (I4).
    void recarregar();
    // Mesma coisa, na mesma pilha de chamada — só pro harness.
    void recarregarSincrono();

    // A caixa de busca mora na barra de ferramentas superior, não mais
    // aqui: duas caixas de busca na mesma tela (uma no topo, outra na
    // coluna esquerda) deixavam ambíguo qual delas manda. Este callback é
    // como este painel pede que o campo lá de cima acompanhe quando ELE
    // mexe na busca — limpar filtros, ou aplicar uma coleção salva, que
    // carrega junto o texto de busca gravado nela.
    std::function<void(const juce::String&)> aoSincronizarBusca;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    // juce::DragAndDropTarget — soltar itens da grade num Vault PLANEJA um
    // backup pra aquela mídia (§6). Coleção inteligente não aceita drop de
    // propósito: ela é uma view SQL, guarda a pergunta e não a resposta, e
    // "adicionar um item a uma pergunta" não quer dizer nada. Quem coleciona
    // item a item é a árvore BACKUP, que já recebe o mesmo arrasto.
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    // Disparado depois de planejar um backup — quem hospeda mostra o aviso
    // discreto na faixa.
    std::function<void(const juce::String& mensagem)> aoAvisar;

    // Introspecção pro harness.
    int linhaAlvoDropParaTeste() const { return linhaAlvoDrop_; }
    void soltarItensParaTeste(int indiceLinha, const std::vector<std::string>& itemIds);
    int indiceDaLinhaDeVaultParaTeste(const std::string& vaultId) const;
    int indiceDaLinhaDeColecaoEmbutidaParaTeste(const std::string& chave) const;

private:
    enum class TipoLinha {
        CabecalhoSecao,
        ChipTipoMidia,
        ChipEstado,
        ChipExtensao,
        ChipOrigem,         // Digital/Analógico (item 4.2)
        ChipContentType,    // Content filter (item 13)
        ChipCollectionType, // Collection filter (item 13)
        ChipAgrupamento,    // alterna o eixo de agrupamento da grade (item 4.3)
        Limpar,
        Vault,             // §8 — bolinha verde/cinza de conexão
        ColecaoEmbutida,   // §10 — view SQL, sempre atualizada
        Colecao,
        SalvarColecao
    };
    struct Linha {
        TipoLinha tipo;
        juce::String rotulo;
        juce::String chave;  // valor do chip (tipo_midia/estado/extensão) ou id da coleção
        int contagem = -1;   // -1 = não mostra contagem (cabeçalhos, ações)
        bool ativo = false;  // chip atualmente selecionado
    };

    // Tudo o que sai do banco pra montar as linhas. Preenchido em
    // background, consumido na message thread.
    struct DadosDoBanco {
        std::map<std::string, int> porTipoMidia, porEstado, porExtensao, porOrigem, porContentType, porCollectionType;
        std::vector<ProjetoAberto::VaultResumo> vaults;
        std::vector<ProjetoAberto::ColecaoEmbutida> embutidas;
        std::vector<ProjetoAberto::ColecaoInteligente> salvas;
    };
    static DadosDoBanco coletarDoBanco(ProjetoAberto& projeto);

    void reconstruirLinhas();
    juce::Rectangle<int> boundsDaLinha(int indice) const;
    int indiceLinhaNaPosicao(int y) const;
    void abrirMenuContextoColecao(int indiceLinha);
    void salvarColecaoAtual();

    ProjetoAberto& projeto_;
    MosaicoComponent& mosaico_;
    std::vector<Linha> linhas_;

    // Vault e coleção embutida são filtros de CONJUNTO DE ITENS (o mesmo
    // canal que a árvore usa), não chips acumuláveis — só um por vez, e
    // clicar no mesmo desliga.
    juce::String vaultFiltrado_;
    juce::String colecaoEmbutidaFiltrada_;
    int linhaAlvoDrop_ = -1;

    DadosDoBanco dados_;
    juce::ThreadPool poolConsulta_{juce::ThreadPoolOptions{}.withThreadName("MatrizFiltros")
                                   .withNumberOfThreads(1)
                                   .withDesiredThreadPriority(juce::Thread::Priority::low)};
    int geracaoConsulta_ = 0;
    bool consultaPendente_ = false;

    void planejarBackupNoVault(const std::string& vaultId, const std::vector<std::string>& itemIds);

    static constexpr int kAlturaCabecalhoSecao = 22;
    static constexpr int kAlturaLinha = 24;
};

} // namespace matriz::ui
