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
    static std::vector<StatusEspelhamento> executarEspelhamentoAutomatico(matriz::model::Project& projeto,
                                                                         const std::set<std::string>& ignorarIds = {});

    // Verifica se há marcador de sync incompleto no destino
    static bool temMarcadorSyncIncompleto(const juce::File& destinoRaiz);
    static void criarMarcadorSync(const juce::File& destinoRaiz, const juce::String& refInfo);
    static void removerMarcadorSync(const juce::File& destinoRaiz);
};

} // namespace matriz::sync
