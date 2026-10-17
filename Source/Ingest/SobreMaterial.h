#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../Db/Database.h"

// Respostas do questionário "Sobre este material" aplicadas a cada arquivo de um
// drop. Nunca substitui nada que o arquivo já tenha: tags e pessoas SOMAM às
// existentes, SUBJECT soma ao assunto lido do arquivo, e o texto vai para uma
// seção de NOTES (item.notas_livres). Os metadados originais (item_campo com
// fonte 'leitura_tecnica': datas, EXIF, GPS, técnicos) não são tocados.

namespace matriz::ingest {

struct SobreMaterial {
    std::string collectionType;   // valor gravado em CONTENT (collection_type), em inglês canônico quando existir
    std::string subject;          // SUBJECT (dc_subject)
    std::string descricao;        // texto completo da Tela 3 -> NOTES
    std::vector<std::string> pessoas;
    std::vector<std::string> tags;

    // Campos de ficha extras: "source_media" (JSON de OriginalSourceMediumInfo, vai na coluna item.source_media),
    // "ano" (só preenche se o arquivo não trouxe data) e qualquer outro campo_id (item_campo, fonte 'humano').
    std::vector<std::pair<std::string, std::string>> campos;

    // "Single file" da ficha de importação: sufixo somado ao título de cada arquivo.
    std::string sufixoTitulo;

    // Conjunto que entra junto e realiza uma ação (ex.: "ddp", "production"): no fim do lote os itens viram um CONTAINER
    // no Intake com esta origem. Não é metadado do arquivo (não entra em vazio()).
    std::string containerOrigem;

    bool vazio() const {
        return collectionType.empty() && subject.empty() && descricao.empty() && pessoas.empty() && tags.empty() &&
               campos.empty() && sufixoTitulo.empty();
    }
};

// Soma uma tag automática (ex.: "celular") ao que o questionário respondeu, sem diferenciar maiúsculas e sem repetir.
// `sobre` nulo (questionário pulado) vira um SobreMaterial só com a tag: a tag vale mesmo sem questionário.
std::shared_ptr<const SobreMaterial> comTagAutomatica(std::shared_ptr<const SobreMaterial> sobre, const std::string& tag);

class AplicadorSobreMaterial {
public:
    // Prepara tudo UMA vez (vocabulário case-insensitive, lista de pessoas da coleção,
    // histórico de SUBJECT). Chamar na thread que ingere.
    AplicadorSobreMaterial(matriz::db::Database& registro, SobreMaterial sobre);
    ~AplicadorSobreMaterial();

    // Aplica ao item recém-ingerido. Falhas são registradas no log e nunca derrubam a ingestão.
    // `descricaoDoItem` (opcional) troca o texto da descrição só para este item — a legenda de uma mídia do WhatsApp.
    void aplicar(const std::string& itemId, const std::string* descricaoDoItem = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace matriz::ingest
