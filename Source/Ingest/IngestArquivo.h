#pragma once

#include <JuceHeader.h>

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

} // namespace matriz::ingest
