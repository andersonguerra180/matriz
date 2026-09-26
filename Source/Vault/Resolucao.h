#pragma once

#include <JuceHeader.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../Db/Database.h"

// Resolução de caminho físico de um `arquivo`.
//
// `arquivo` é PROVENIÊNCIA: o que foi ingerido e de onde (vault + caminho
// relativo à raiz do volume, ou caminho absoluto de origem). Nunca é
// reescrito pra apontar pro backup (trigger arquivo_master_travado). Onde o
// arquivo mora no MAIN/CLONE vem de `consolidacao_registro`
// (caminho_relativo_destino, relativo a <raiz do destino>/Media) e do papel
// de cada destino em `backup_destino`.
//
// Todo consumidor que abre/toca/gera miniatura ou forma de onda tem que
// passar por aqui — concatenar `pastaProjeto + caminho_relativo` na mão não
// acha o arquivo quando o backup aplicou máscara/hierarquia.
//
// Ordem de LEITURA (Preferencia::MainPrimeiro, o padrão):
//   1. MAIN   — destino com papel ORIGINAL em backup_destino, identificado pelo
//               destination_id (nunca pelo caminho): se a raiz do projeto
//               aberto é o próprio MAIN, vale a montagem atual; senão o
//               destino_path registrado. + caminho_relativo_destino.
//   2. CLONEs — os demais destinos ativos, mesma lógica (espelho do MAIN).
//   3. SOURCE — vault.localizacao + arquivo.caminho_relativo.
//   4. arquivo.caminho_absoluto_origem.
//   5. Legado — pasta do projeto (+ Media/) + caminho_relativo, pra projetos
//               antigos do modelo "ingest copiava pra dentro do projeto".
// Preferencia::Origem pula 1 e 2: pra quem precisa do ARQUIVO ORIGINAL (nome
// original pra planejar máscara, data de modificação/tamanho da origem).
// Devolve nullopt quando nenhum candidato existe em disco — o chamador
// distingue "não sei onde está" de "sei onde está e sumiu" olhando
// `arquivo.estado_presenca`.

namespace matriz::vault {

enum class Preferencia { MainPrimeiro, Origem };

// Só com as colunas de proveniência (sem id do arquivo): ordem 3 → 4 → 5.
// Use quando a ORIGEM é o que se quer; pra leitura, prefira resolverArquivo /
// ResolvedorEmLote, que conhecem o MAIN.
std::optional<juce::File> resolverCaminho(const juce::File& pastaProjeto,
                                          const std::string& localizacaoVault,
                                          const std::string& caminhoRelativo,
                                          const std::string& caminhoAbsolutoOrigem);

// Resolução completa pelo id do arquivo (lê proveniência, destinos e
// consolidacao_registro). Algumas queries por chamada — em laço, use
// ResolvedorEmLote.
std::optional<juce::File> resolverArquivo(matriz::db::Database& registro, const std::string& arquivoId,
                                          const juce::File& pastaProjeto,
                                          Preferencia preferencia = Preferencia::MainPrimeiro);

// Caminho "esperado" mesmo que o arquivo não exista em disco — pra mensagens
// de erro e pra exibir onde o operador deve procurar (§8, item ausente).
// Só proveniência (ordem 3 → 4 → 5).
juce::File caminhoEsperado(const juce::File& pastaProjeto, const std::string& localizacaoVault,
                           const std::string& caminhoRelativo, const std::string& caminhoAbsolutoOrigem);

// Resolvido em modo leitura se existir; senão o esperado na origem.
juce::File caminhoEsperadoArquivo(matriz::db::Database& registro, const std::string& arquivoId,
                                  const juce::File& pastaProjeto);

// Destinos de backup conhecidos, MAIN primeiro. `raiz` é a pasta que contém
// Media/ e Project/.
struct DestinoDeBackup {
    std::string destinationId;
    juce::File raiz;
    bool ehMain = false;
};
std::vector<DestinoDeBackup> destinosDeBackup(matriz::db::Database& registro, const juce::File& pastaProjeto);

// destination_id do destination.json de uma raiz de destino (vazio se não há).
std::string destinationIdDaRaiz(const juce::File& raiz);

// Pra laços (listagem, verificação de presença, painel de inconsistências):
// carrega destinos e TODO o consolidacao_registro uma vez só.
class ResolvedorEmLote {
public:
    ResolvedorEmLote(matriz::db::Database& registro, const juce::File& pastaProjeto);

    std::optional<juce::File> resolver(const std::string& arquivoId, const std::string& localizacaoVault,
                                       const std::string& caminhoRelativo,
                                       const std::string& caminhoAbsolutoOrigem) const;

private:
    struct Registro {
        std::string caminhoRelativoDestino;
        std::string destinoId;
    };
    juce::File pastaProjeto_;
    std::vector<DestinoDeBackup> destinos_;
    std::map<std::string, std::vector<Registro>> registrosPorArquivo_;
};

// Fragmento de SELECT com as colunas que `resolverCaminho` consome, na ordem
// (localizacao, caminho_relativo, caminho_absoluto_origem). Usado pra manter
// as consultas espalhadas em sincronia com a assinatura acima.
inline const char* colunasDeResolucao() {
    return "COALESCE(v.localizacao, ''), a.caminho_relativo, COALESCE(a.caminho_absoluto_origem, '')";
}

// JOIN correspondente. `a` é o alias de `arquivo`.
inline const char* joinDeResolucao() { return "LEFT JOIN vault v ON v.id = a.vault_id"; }

} // namespace matriz::vault
