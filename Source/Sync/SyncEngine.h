#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <optional>

#include "../Model/Project.h"
#include "../App/Cancelamento.h"

namespace matriz::sync {

enum class ClasseSync {
    Igual,
    Novo,
    Modificado,
    Movido,
    Removido
};

enum class CategoriaSync {
    Media,
    Project
};

struct ItemSync {
    juce::String caminhoRelativo; // Relativo à pasta de categoria (Media/ ou Project/)
    CategoriaSync categoria = CategoriaSync::Media;
    ClasseSync classe = ClasseSync::Igual;
    juce::int64 tamanhoBytes = 0;
    std::string sha256Ref;
    std::string sha256Alvo;
    juce::String caminhoOrigemMovido; // Caminho antigo no alvo para itens Movido
};

struct PlanoSync {
    std::vector<ItemSync> itens;
    int totalIguais = 0;
    int totalNovos = 0;
    int totalModificados = 0;
    int totalMovidos = 0;
    int totalRemovidos = 0;
    juce::int64 bytesParaCopiar = 0;
    juce::int64 bytesParaLixeira = 0;
    bool referenciaMaisAntiga = false;
    int64_t revisaoRef = 0;
    int64_t revisaoAlvo = 0;
    std::string rotuloRef;
    std::string rotuloAlvo;
    std::vector<std::string> errosValidacao;

    bool podeAplicar() const { return errosValidacao.empty(); }
};

struct ResultadoSync {
    bool sucesso = false;
    bool cancelado = false;
    int itensCopiados = 0;
    int itensMovidos = 0;
    int itensLixeira = 0;
    std::vector<std::string> falhas;
    juce::File pastaLixeiraCriada;
};

using CallbackProgressoSync = std::function<bool(int atual, int total, const juce::String& mensagem)>;

class SyncEngine {
public:
    // Escaneia e compara dois DESTINATIONs (somente leitura).
    // modoCompletoSha256: calcula SHA-256 de todos os arquivos nos dois lados.
    static PlanoSync escanearEComparar(const juce::File& referenciaRaiz,
                                       const juce::File& alvoRaiz,
                                       bool modoCompletoSha256 = true,
                                       const CallbackProgressoSync& progresso = nullptr,
                                       matriz::app::CancelamentoPtr cancelamento = nullptr);

    // Aplica o plano de sincronização no alvo, gerando a pasta _lixeira/<timestamp>_sync.
    static ResultadoSync aplicarSync(const juce::File& referenciaRaiz,
                                     const juce::File& alvoRaiz,
                                     const PlanoSync& plano,
                                     const CallbackProgressoSync& progresso = nullptr,
                                     matriz::app::CancelamentoPtr cancelamento = nullptr);

    // Espelhamento automático para clones conectados que não divergiram.
    // Retorna lista de relatórios de execução para cada clone.
    struct StatusEspelhamento {
        std::string destinationId;
        juce::String rotulo;
        juce::String caminho;
        enum class Estado { Aplicado, PendenteOffline, Divergente, Falha } estado;
        juce::String mensagem;
    };

    // ignorarIds: destinos (backup_destino.id) desmarcados pelo operador
    // para este envio — não recebem o espelhamento.
    // aplicarRemocoes=false: só adições/atualizações — o que falta no MAIN NÃO
    // vai pra _lixeira do clone (remoção só com confirmação explícita).
    static std::vector<StatusEspelhamento> executarEspelhamentoAutomatico(matriz::model::Project& projeto,
                                                                         const std::set<std::string>& ignorarIds = {},
                                                                         bool aplicarRemocoes = true);

    // ---------------------------------------------------------------- Etapa 7
    // CLONE = espelho integral. Nunca copia de CLONE pra MAIN; remoções no
    // clone só com aplicarRemocoes = true (confirmação explícita na UI).
    struct ResultadoClone {
        bool sucesso = false;
        bool cancelado = false;
        int copiados = 0;
        std::vector<std::string> falhas;
        std::string id;      // backup_destino.id (clone do MAIN) ou source_clone.id
        juce::File raiz;
    };
    // Clone do MAIN: registra um destino CLONE (destination.json + backup_destino)
    // em `pastaEscolhida` (ou numa subpasta "<projeto> CLONE" se ela já tem
    // outra coisa) e copia Media + Project (banco incluso).
    static ResultadoClone clonarMain(matriz::model::Project& projeto, const juce::File& pastaEscolhida,
                                     const CallbackProgressoSync& progresso = nullptr,
                                     matriz::app::CancelamentoPtr cancelamento = nullptr);
    // Compara MAIN -> clone (só leitura). As remoções (arquivo só no clone)
    // vêm no plano como ClasseSync::Removido — a UI mostra separado.
    static PlanoSync compararCloneDoMain(matriz::model::Project& projeto, const std::string& cloneId);
    static ResultadoSync sincronizarCloneDoMain(matriz::model::Project& projeto, const std::string& cloneId,
                                                bool aplicarRemocoes, const CallbackProgressoSync& progresso = nullptr,
                                                matriz::app::CancelamentoPtr cancelamento = nullptr);

    // Clone de SOURCE ("Clone Source"): cópia bruta, nomes e estrutura exatamente
    // como estão no volume, com checksums.sha256 junto; registrado em
    // source_clone. Não passa pelo catálogo.
    static ResultadoClone clonarSource(matriz::model::Project& projeto, const std::string& vaultId,
                                       const juce::File& pastaEscolhida, const CallbackProgressoSync& progresso = nullptr,
                                       matriz::app::CancelamentoPtr cancelamento = nullptr);
    struct PlanoCloneSource {
        juce::File origem, clone;
        std::vector<juce::String> novos;      // na origem, não no clone (ou tamanho diferente)
        std::vector<juce::String> removidos;  // só no clone
        std::string erro;
    };
    static PlanoCloneSource compararCloneDeSource(matriz::model::Project& projeto, const std::string& cloneId);
    static ResultadoSync sincronizarCloneDeSource(matriz::model::Project& projeto, const std::string& cloneId,
                                                  bool aplicarRemocoes, const CallbackProgressoSync& progresso = nullptr,
                                                  matriz::app::CancelamentoPtr cancelamento = nullptr);

    // Promover a MAIN (o MAIN morreu): o clone vira ORIGINAL, os outros CLONE;
    // consolidacao_registro passa a apontar pro clone (é espelho: mesmos
    // caminhos) — no banco aberto E no banco copiado no clone; destination.json
    // dos dois lados. Nada é copiado nem apagado. Depois, abrir o projeto a
    // partir do clone.
    static bool promoverAMain(matriz::model::Project& projeto, const std::string& cloneId, juce::String& erro);

    // Verifica se há marcador de sync incompleto no destino
    static bool temMarcadorSyncIncompleto(const juce::File& destinoRaiz);
    static void criarMarcadorSync(const juce::File& destinoRaiz, const juce::String& refInfo);
    static void removerMarcadorSync(const juce::File& destinoRaiz);
};

} // namespace matriz::sync
