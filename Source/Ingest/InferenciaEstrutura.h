#pragma once

#include <JuceHeader.h>

#include <optional>
#include <string>
#include <vector>

#include "LeituraTecnica.h"
#include "Miniaturas.h"

// Inferência de estrutura de pasta de disco e agrupamento por timestamp
// (§7.3, §7.2). Funções puras — não tocam disco além do que já foi lido
// pelas outras peças do ingest (LeituraTecnica, Miniaturas). Usadas quando
// um conjunto de pastas inteiro é arrastado pro MATRIZ de uma vez.

namespace matriz::ingest {

// "Artista - Álbum (Ano)" -> release, artista, ano (§7.3).
struct InferenciaPastaRelease {
    std::optional<std::string> artista;
    std::optional<std::string> album;
    std::optional<int> ano;
};
InferenciaPastaRelease inferirDePastaRelease(const juce::String& nomePasta);

// "01 - Faixa.wav" -> ordem e título (§7.3).
struct InferenciaNomeFaixa {
    std::optional<int> numero;
    std::optional<std::string> titulo;
};
InferenciaNomeFaixa inferirDeNomeArquivoFaixa(const juce::String& nomeArquivo);

// ID3 / Vorbis / BWF embutido -> título, ISRC, compositor (§7.3). Busca
// case-insensitive nas tags cruas devolvidas por ffprobe, porque containers
// diferentes capitalizam diferente (ID3 costuma vir minúsculo, BWF/iXML às
// vezes preserva caixa alta como "ISRC").
struct TagsEmbutidasComuns {
    std::optional<std::string> titulo;
    std::optional<std::string> artista;
    std::optional<std::string> isrc;
    std::optional<std::string> compositor;
};
TagsEmbutidasComuns extrairTagsComuns(const LeituraTecnicaResultado& leitura);

// Imagem quadrada grande -> provável capa frente (§7.3). Tolerância de
// proporção em fração (0.05 = até 5% de diferença entre largura e altura).
bool provavelCapaFrente(const DimensaoImagem& dimensao, int ladoMinimoPx = 800, double toleranciaProporcao = 0.05);

// Duração parecida + fingerprint parecido -> provável mesma gravação em
// pastas diferentes (§7.3). Recebe a similaridade já calculada
// (Source/Ingest/Duplicata.h) para não acoplar este módulo à decodificação
// de áudio.
bool provavelmenteMesmaGravacao(double duracaoASegundos, double duracaoBSegundos, double similaridadeFingerprint,
                                 double toleranciaDuracaoSegundos = 1.0, double limiarSimilaridade = 0.8);

// --- Agrupamento por timestamp (§7.2) --------------------------------------
//
// Pasta com milhares de fotos vira dezenas de grupos por salto de
// timestamp, respondidos de uma vez (fluxo de ficha em lote, §7.2). O
// refinamento por similaridade visual (CLIP) fica pra Etapa 4 — aqui o
// agrupamento é só por proximidade temporal, um critério que já funciona
// sem nenhuma camada de IA em pé.

struct ItemParaAgrupar {
    std::string id;
    double timestampUnixSegundos = 0.0;
};

struct GrupoTemporal {
    std::vector<std::string> idsOrdenados;
    double inicioUnixSegundos = 0.0;
    double fimUnixSegundos = 0.0;
};

std::vector<GrupoTemporal> agruparPorSaltoDeTimestamp(std::vector<ItemParaAgrupar> itens,
                                                       double saltoMaximoSegundos = 1800.0);

} // namespace matriz::ingest
