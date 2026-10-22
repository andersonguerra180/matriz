#pragma once

#include <map>
#include <memory>
#include <optional>
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

    // Campos que só valem para o arquivo de VÍDEO do lote (ex.: dc_title e dc_description do YouTube Production), gravados na
    // ficha de metadados (Dublin Core) do vídeo e não repetidos na thumbnail e no roteiro.
    std::vector<std::pair<std::string, std::string>> camposDoVideo;

    // "Single file" da ficha de importação: sufixo somado ao título de cada arquivo.
    std::string sufixoTitulo;

    // Localização padrão da importação: só vale para arquivos que não trazem geolocalização própria (as coordenadas
    // do arquivo e o que o usuário preencher à mão prevalecem).
    struct GeoPadrao {
        std::string endereco, cidade, estado, pais;
        std::optional<double> lat, lng;
        bool vazio() const { return endereco.empty() && cidade.empty() && estado.empty() && pais.empty() && !lat.has_value(); }
    } geoPadrao;

    // Conjunto que entra junto e realiza uma ação (ex.: "ddp", "production"): no fim do lote os itens viram um CONTAINER
    // no Intake com esta origem. Não é metadado do arquivo (não entra em vazio()).
    std::string containerOrigem;

    // Arquivos que entram juntos e só têm um tema em comum (Lightroom, Bridge, sample pack, gravações de campo, documentos)
    // viram um NEST — um agrupamento temático, sem origem — e não um container. Não é metadado do arquivo.
    bool agruparComoNest = false;

    // Container sem capa escolhida: a miniatura dele é um frame sorteado do meio do vídeo principal (YouTube Production).
    bool capaFrameDoVideo = false;

    // Questionário que gerou este contexto: o card do IMPORT (ex.: "audio.production.project") e as respostas da ficha
    // (id do campo -> texto). Guardados no container para o usuário poder editar o questionário inicial depois.
    // Não são metadado do arquivo (não entram em vazio()).
    std::string folhaId;
    // Nome do container (o álbum / projeto / música / vídeo da ficha): vira o nome mostrado do container e a LIST dos arquivos.
    std::string nomeContainer;
    std::map<std::string, std::string> respostas;

    // Ícone do container escolhido na ficha (nome do recurso embutido, ex.: "ci_daw_pro_tools_png" para a DAW da produção).
    // Vale quando não há capa; sem os dois, o container usa o ícone da origem.
    std::string iconeContainer;

    // Imagem escolhida na ficha (capa do disco/fita) para ser a miniatura do container. Só vale se o lote virar container.
    std::string capaContainer;

    bool vazio() const {
        return collectionType.empty() && subject.empty() && descricao.empty() && pessoas.empty() && tags.empty() &&
               campos.empty() && camposDoVideo.empty() && sufixoTitulo.empty() && geoPadrao.vazio();
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

    // Tira do item o que ESTE contexto tinha posto (tags e pessoas, partes do SUBJECT, o texto de NOTES e a LIST iguais),
    // para o questionário poder ser editado sem deixar a resposta antiga para trás. Só o que o usuário não mexeu depois.
    void desfazer(const std::string& itemId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace matriz::ingest
