#pragma once

#include <JuceHeader.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../Consolidacao/Consolidacao.h"
#include "../Db/Database.h"

// Catálogo de proxies (item 11 das correções de operação).
//
// É o que transforma o backup de "um monte de arquivos" numa coisa
// consultável: gravado DENTRO do destino do backup, descreve tudo o que foi
// indexado — miniatura, ficha, metadado técnico, e onde cada original está
// (caminho completo, volume de origem, checksum).
//
// A propriedade que justifica o subsistema: o catálogo é AUTÔNOMO. Não
// referencia o projeto que o gerou, não guarda caminho pra dentro dele, e
// abre numa máquina que nunca viu o projeto original. O operador navega o
// acervo com o HD desconectado, acha o arquivo visualmente, e o software diz
// exatamente onde ele está.
//
// ESCOPO DESTA ETAPA — declarado, não fingido:
//   - Entram os proxies que a ingestão JÁ produz: miniatura (imagem/vídeo) e
//     forma de onda (áudio), copiados pro catálogo.
//   - NÃO entram: proxy de áudio em baixa taxa, storyboard de vídeo (vários
//     keyframes) e texto extraído de documento. Os três exigem geração nova
//     (ffmpeg com perfil de proxy, extração multi-keyframe, e um extrator de
//     texto que não existe — ver a nota sobre PDFium/MuPDF em
//     LeituraTecnica.cpp). A ausência é explícita: nenhum campo é preenchido
//     com aproximação.

namespace matriz::catalogo {

// Nome da pasta do catálogo dentro do destino de backup. Prefixo "_" pra
// ordenar antes do conteúdo e deixar claro que é infraestrutura, não acervo.
inline const char* kNomePastaCatalogo = "_catalogo_matriz";

struct EstimativaCatalogo {
    int itens = 0;
    juce::int64 tamanhoBytes = 0; // soma real das miniaturas + formas de onda que serão copiadas
};

// Quanto o catálogo vai ocupar, ANTES de gerar (§11: "o painel mostra o
// tamanho antes de gerar"). Soma o tamanho em disco dos proxies que já
// existem — não é estimativa por heurística, é o número real.
EstimativaCatalogo estimar(matriz::db::Database& registro, matriz::db::Database& indice,
                            const juce::File& pastaProjeto);

struct ResultadoGeracao {
    int gravados = 0;
    std::vector<std::string> falhas;
    bool cancelado = false;
    int totalPlanejado = 0;
};

// Gera (ou regenera) o catálogo dentro de `destinoBackup`. Idempotente:
// rodar de novo reescreve por completo, então é "regenerável a qualquer
// momento" sem acumular lixo de execuções anteriores.
//
// Usa o mesmo AoProgredir da consolidação — toda operação longa do software
// oferece cancelamento pela mesma porta (item 10).
ResultadoGeracao gerar(matriz::db::Database& registro, matriz::db::Database& indice,
                        const juce::File& pastaProjeto, const juce::File& destinoBackup,
                        const matriz::consolidacao::AoProgredir& aoProgredir = {});

// --- Leitura: daqui pra baixo, NADA depende do projeto original ---

struct EntradaCatalogo {
    std::string itemId;
    std::string codigoAcervo;
    std::string titulo;
    std::string tipoMidia;
    juce::String caminhoOrigem;    // caminho absoluto de onde o arquivo veio
    juce::String volumeOrigem;     // "HD Samsung T7" — extraído do caminho
    juce::String caminhoNoBackup;  // relativo à raiz do backup
    std::string checksumSha256;
    juce::int64 tamanhoBytes = 0;
    juce::String miniaturaRelativa;    // relativo à pasta do catálogo ("" se não houver)
    juce::String metadadoTecnicoJson;
    juce::String fichaJson;
    bool temFormaOnda = false;
};

bool ehPastaDeCatalogo(const juce::File& pasta);

// Abre um catálogo gravado por gerar(). `pasta` pode ser a pasta do catálogo
// ou a raiz do backup que a contém — as duas funcionam, porque o operador
// não tem por que saber qual das duas apontar.
std::vector<EntradaCatalogo> abrir(const juce::File& pasta);

// Resolve a pasta do catálogo a partir de qualquer uma das duas. Vazio se
// não houver catálogo ali.
juce::File resolverPastaCatalogo(const juce::File& pasta);

struct Localizacao {
    bool fonteConectada = false;
    juce::File arquivoReal;        // só válido quando fonteConectada
    juce::String descricaoHumana;   // "HD Samsung T7 / 2003 / Turnê Europa / Berlim"
};

// Onde o arquivo está AGORA. Procura em duas frentes, nesta ordem:
//   1. A cópia dentro do próprio backup (o backup também é uma fonte, e é a
//      que está mais perto — se o catálogo abriu, ela costuma estar junto).
//   2. O caminho de origem registrado, se aquele volume estiver montado.
// Não achando nenhuma das duas, devolve a descrição legível de onde procurar.
Localizacao localizar(const EntradaCatalogo& entrada, const juce::File& pastaCatalogo);

// Descrição legível de um caminho absoluto ("/Volumes/HD/2003/Berlim/x.wav"
// vira "HD / 2003 / Berlim"). Exposta porque é o texto que a UI mostra
// quando a fonte está ausente.
juce::String descreverLocalizacao(const juce::String& caminhoAbsoluto);

} // namespace matriz::catalogo
