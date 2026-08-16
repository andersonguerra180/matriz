#pragma once

#include <JuceHeader.h>

#include <optional>
#include <stdexcept>
#include <string>

#include "../Db/Database.h"
#include "Checksum.h"
#include "LeituraTecnica.h"

// Glue de ingestão (§7.2 estágio 1): copia o arquivo de origem pra dentro da
// pasta do projeto (P5 — projeto portátil, nada fica fora dela), calcula
// checksum sobre a cópia (é ela que passa a ser o registro, não o
// original), roda a leitura técnica, e grava a linha em `arquivo`.
//
// Convenção de posicionamento em disco (não especificada na §5.4, que só
// define "caminho relativo à pasta do projeto"): <projeto>/arquivos/<id do
// arquivo>/<nome original>. Um subdiretório por id evita colisão de nome
// entre itens sem precisar de nenhuma outra regra; export/entrega
// institucional (§12.4) é empacotamento à parte, não afeta esta cópia de
// trabalho.

namespace matriz::ingest {

class IngestArquivoError : public std::runtime_error {
public:
    explicit IngestArquivoError(const std::string& message) : std::runtime_error(message) {}
};

struct ResultadoIngestArquivo {
    std::string arquivoId;
    juce::File arquivoNoProjeto;
    Checksums checksums;
    LeituraTecnicaResultado leitura;
};

// Encontra (ou cria) o Vault que corresponde ao volume físico onde `arquivo`
// mora — chave forte é o UUID de volume, com o ponto de montagem como
// fallback (§8).
std::string obterOuCriarVaultParaArquivo(matriz::db::Database& registro, const juce::File& arquivo,
                                         const std::string& projetoId);

// true quando o arquivo é um placeholder de nuvem (iCloud/Dropbox) ainda não
// baixado: tem tamanho lógico mas nenhum bloco alocado. Catalogar não pode
// forçar o download dos bytes (§5, critério 12).
bool verificarSePlaceholderNuvem(const juce::File& arquivo);

// ---------------------------------------------------------------------------
// Ingestão em duas fases (critério 3 — ingestão CONCORRENTE de 5.000
// arquivos).
//
// A fase cara — checksum do arquivo inteiro, ffprobe, Exiv2, decodificação —
// não toca no banco e roda em paralelo, uma thread por núcleo. A fase de
// gravação toca no banco e roda sob um mutex único, porque duas threads
// escrevendo no mesmo handle SQLite se serializam de qualquer jeito, e
// porque a numeração do acervo precisa de exclusão mútua pra não gastar o
// mesmo sequencial duas vezes.
//
// Separar as duas é o que transforma 5.000 arquivos de uma fila sequencial
// em trabalho de verdade paralelo: a gravação é microssegundos, a análise é
// dezenas de milissegundos.
// ---------------------------------------------------------------------------

struct AnaliseDeArquivo {
    juce::File arquivo;
    Checksums checksums;
    LeituraTecnicaResultado leitura;
    bool ehPlaceholderNuvem = false;
};

// Fase 1 — PURA: lê o arquivo, não toca no banco. Segura pra rodar em N
// threads ao mesmo tempo.
AnaliseDeArquivo analisarArquivo(const juce::File& arquivoOrigem);

// Fase 2 — grava a linha em `arquivo` a partir de uma análise já feita.
// Chamar sob o mutex de escrita do lote.
// `papel` casa com arquivo.papel (§5.4) — preservation_master, access_copy,
// capa_frente, capa_verso, encarte, documento, stem, foto_suporte, etc.
ResultadoIngestArquivo gravarArquivoAnalisado(matriz::db::Database& registro, const std::string& itemId,
                                               const AnaliseDeArquivo& analise, const std::string& papel,
                                               bool ehMaster);

// Conveniência: as duas fases juntas. É o caminho de quem ingere um arquivo
// só (capa, documento avulso) e o dos self-tests headless.
ResultadoIngestArquivo ingerirArquivo(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                      const std::string& itemId, const juce::File& arquivoOrigem,
                                      const std::string& papel, bool ehMaster);

// ---------------------------------------------------------------------------
// Continuous ingestion (item 9, Acréscimos §5/§8.2) — escopo desta etapa:
// dedup EXATO por checksum ("um asset, muitas localizações"). Near-
// duplicate (pHash/Chromaprint) e detecção automática de fonte reconectada
// não estão aqui — precisam do modelo de fingerprint/asset completo (§4/§8),
// gap já declarado no relatório do item 7.
// ---------------------------------------------------------------------------

struct AssetConhecido {
    std::string itemId;
    std::string codigoAcervo;
    std::string arquivoId;
};

// Calcula o checksum da ORIGEM (antes de copiar) e busca um `arquivo` já
// registrado com o mesmo SHA-256, em QUALQUER item do projeto. nullopt se
// não existir ou se `arquivoOrigem` não for um arquivo legível.
std::optional<AssetConhecido> buscarAssetConhecido(matriz::db::Database& registro, const juce::File& arquivoOrigem);

// Como buscarAssetConhecido, mas com o checksum JÁ calculado — evita reler o
// arquivo inteiro uma segunda vez no caminho concorrente.
std::optional<AssetConhecido> buscarAssetPorChecksum(matriz::db::Database& registro,
                                                      const std::string& sha256);


// Registra que o mesmo conteúdo de `arquivoId` também foi encontrado em
// `caminhoAbsoluto` — nunca copia nada, só amplia "quantas cópias existem e
// onde" (§8.2). Idempotente: repetir o mesmo caminho não duplica a linha.
void registrarLocalizacaoConhecida(matriz::db::Database& registro, const std::string& arquivoId,
                                    const juce::File& caminhoAbsoluto);

} // namespace matriz::ingest
