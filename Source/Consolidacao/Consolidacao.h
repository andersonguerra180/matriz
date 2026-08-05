#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>
#include <vector>

#include "../Db/Database.h"

// Consolidação (item 10, §11.7) — copia byte a byte da master JÁ DENTRO DO
// PROJETO (não do arquivo original de origem — este nunca é reaberto aqui;
// P5 garante que a cópia de trabalho já é idêntica) pra dentro da estrutura
// do Acervo, renomeada pela máscara de cada pasta, e verifica depois.
//
// Escopo desta etapa, declarado — não fingido:
// - Metadado embutido (BWF/bext, iXML, EXIF write, ID3, XMP) NÃO é gravado
//   na cópia consolidada. Precisaria de escritores por formato que não
//   existem ainda (Exiv2, já uma dependência, só foi usado pra LEITURA até
//   aqui — escrever EXIF/XMP com ele é viável depois, os demais formatos
//   não têm biblioteca nenhuma no projeto). O checksum é da cópia tal como
//   sai, sem nenhum metadado a mais.
// - Destino é sempre uma pasta local. NAS/FTP/S3/nuvem (já modelados na
//   tabela `destino`) não têm cliente nenhum implementado pra consolidação
//   escrever neles.
// - "Metadado lido de volta corretamente" (§11.7 verificação) não se aplica
//   por não haver nada embutido — a verificação aqui é existência, tamanho
//   e checksum.

namespace matriz::consolidacao {

// Organização de pastas do backup (item 5). A estrutura padrão é
// Projeto → Ano → Tipo de Mídia → Tipo de Arquivo, e o operador reordena,
// remove ou acrescenta níveis. `PastaManual` é a estrutura que ele montou à
// mão na árvore BACKUP — é o que existia como único comportamento antes
// deste item, e continua disponível como um nível entre os outros (sozinha,
// reproduz exatamente o comportamento anterior).
//
// Material sem o campo de um nível (ano desconhecido, tipo não classificado)
// vai pra uma pasta "sem <campo>" em vez de desaparecer ou travar o backup
// (§5.2 — "nunca desaparece nem trava").
enum class NivelHierarquia { Projeto, Ano, TipoMidia, TipoArquivo, Origem, Artista, PastaManual };

std::string nivelHierarquiaToString(NivelHierarquia n);
NivelHierarquia nivelHierarquiaFromString(const std::string& s); // lança std::runtime_error se desconhecido

using HierarquiaBackup = std::vector<NivelHierarquia>;

// Projeto → Ano → Tipo de Mídia → Tipo de Arquivo (item 5.1).
HierarquiaBackup hierarquiaPadrao();

// Serialização pra projeto.hierarquia_backup (CSV). Vazio/inválido volta
// pro padrão — nunca lança na leitura, pra um valor corrompido não impedir
// de abrir o projeto.
std::string hierarquiaParaCsv(const HierarquiaBackup& h);
HierarquiaBackup hierarquiaDeCsv(const std::string& csv);

struct ItemPlanejado {
    std::string itemId;
    std::string codigoAcervo;
    std::string pastaId;
    std::string arquivoId;
    juce::String nomeOriginal;             // só pro display na prévia (original vs final)
    juce::String caminhoRelativoDestino;   // relativo à raiz de destino escolhida
    juce::int64 tamanhoBytes = 0;
    bool jaConsolidado = false;            // incremental — já tem registro com o mesmo checksum, não precisa copiar de novo
    bool emConflito = false;               // caminhoRelativoDestino colide com outro item do plano
};

struct PlanoConsolidacao {
    std::vector<ItemPlanejado> itens;
    std::vector<juce::String> nomesEmConflito; // caminhos que aparecem em mais de um item — bloqueiam consolidar
    int itensNaoOrganizados = 0;               // §5.5 — fora do plano, só contados pro aviso
    juce::int64 espacoNecessarioBytes = 0;     // soma dos itens que NÃO estão jaConsolidado
    juce::int64 espacoDisponivelBytes = 0;

    bool podeConsolidar() const { return nomesEmConflito.empty(); }
};

// Nunca toca em disco — só lê o banco e calcula. Sempre reexecutável (é a
// "prévia sempre visível", §11.6), inclusive pra atualizar depois de o
// operador resolver um conflito renomeando/movendo na árvore.
// Nome de pasta pra um tipo de mídia. O default devolve o próprio id
// (`fita_rolo`), que é estável mas cru; a UI passa um resolvedor que devolve
// o rótulo de exibição traduzido ("Reel tape"), como no exemplo do §5.1.
// Existe como callback, e não como include de FichaI18n aqui, porque este
// módulo não é de UI e é compilado no selftest headless, que não linka i18n.
using RotuloTipoMidia = std::function<juce::String(const std::string& tipoMidia)>;

// `hierarquia` vazia = usa o que estiver gravado em projeto.hierarquia_backup
// (ou o padrão, se não houver nada gravado).
PlanoConsolidacao planejarConsolidacao(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                        const juce::File& destino, const HierarquiaBackup& hierarquia = {},
                                        const RotuloTipoMidia& rotuloTipoMidia = {});

// Lê/grava a hierarquia escolhida pelo operador em projeto.hierarquia_backup.
HierarquiaBackup hierarquiaDoProjeto(matriz::db::Database& registro);
void gravarHierarquiaDoProjeto(matriz::db::Database& registro, const HierarquiaBackup& hierarquia);

struct ResultadoConsolidacao {
    int consolidados = 0;
    int pulados = 0; // incremental — já estavam lá, checksum batia, não foram tocados de novo
    std::vector<std::string> falhas; // "<código>: <motivo>" — nunca silencioso (§11.7 "falha vira alerta, nunca sucesso silencioso")

    // Item 10 — o operador interrompeu. O que já foi copiado E VERIFICADO
    // continua válido e registrado; retomar depois pula esses (jaConsolidado
    // no plano seguinte), sem refazer nada.
    bool cancelado = false;
    int totalPlanejado = 0; // pro resumo "X de Y processados"

    // Item 8.3 — quantas cópias receberam os marcadores como metadado
    // embutido (cue/adtl/iXML). Só WAV nesta etapa; ver MetadadoEmbutido.h.
    int arquivosComMarcadorEmbutido = 0;
};

// Chamado ANTES de cada arquivo, com quantos já foram processados e o total.
// Devolver false interrompe: o arquivo em curso é o último, e nada do que já
// foi feito é revertido.
//
// É também o gancho de progresso — não existe versão "só progresso" ou "só
// cancelamento", justamente pra não haver operação longa que reporte
// andamento sem oferecer saída (item 10: cancelar SEM EXCEÇÃO).
using AoProgredir = std::function<bool(int feito, int total)>;

// Copia cada item do plano (que não esteja em conflito nem já consolidado)
// pro destino, verifica depois (existe, tamanho bate, checksum bate), e
// registra em consolidacao_registro. Falha num item não aborta os outros —
// mesma resiliência por-item já usada na ficha em lote (item 8).
ResultadoConsolidacao executarConsolidacao(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                            const juce::File& destino, const PlanoConsolidacao& plano,
                                            const AoProgredir& aoProgredir = {});

} // namespace matriz::consolidacao
