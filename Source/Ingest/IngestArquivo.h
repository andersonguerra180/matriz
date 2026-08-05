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

// `papel` casa com arquivo.papel (§5.4) — preservation_master, access_copy,
// capa_frente, capa_verso, encarte, documento, stem, foto_suporte, etc.
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

// Registra que o mesmo conteúdo de `arquivoId` também foi encontrado em
// `caminhoAbsoluto` — nunca copia nada, só amplia "quantas cópias existem e
// onde" (§8.2). Idempotente: repetir o mesmo caminho não duplica a linha.
void registrarLocalizacaoConhecida(matriz::db::Database& registro, const std::string& arquivoId,
                                    const juce::File& caminhoAbsoluto);

} // namespace matriz::ingest
