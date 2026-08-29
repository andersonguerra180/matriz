#pragma once

#include <JuceHeader.h>
#include <functional>
#include <string>
#include <vector>

#include "../App/Cancelamento.h"
#include "../Db/Database.h"

// Análise de conteúdo por API (Gemini). O que interessa aqui é o que está
// DENTRO do arquivo — o áudio, a imagem, os frames do vídeo, o texto do PDF —
// nunca o que o nome do arquivo sugere. Quando o conteúdo não pode ser lido, o
// item vira uma falha declarada, não um resumo inventado.
//
// REGRA INVIOLÁVEL: o arquivo original nunca é aberto para escrita, movido ou
// renomeado. Proxies de áudio/keyframes são gerados em diretório temporário e
// apagados ao fim de cada item.

namespace matriz::ingest {

struct AiScanResult {
    std::string id;
    std::string itemId;
    std::string modelo;
    std::string tipoAnalise;
    std::string contextoJson;
    std::string resumo;
    double confianca = 0.0;
    std::string analisadoEm;
};

struct AiScanOpcoes {
    // Modelos tentados em ordem. O primeiro que não devolver 404 é usado no
    // lote inteiro. Lista em vez de nome fixo porque modelo hospedado é
    // desligado sem aviso, e um 404 no meio de um acervo grande não pode
    // exigir recompilar o app.
    std::vector<juce::String> modelos{"gemini-3.6-flash", "gemini-2.5-flash", "gemini-2.0-flash"};
    int keyframesPorVideo = 4;
    double segundosMaxAudio = 1800.0;
    int ladoMaximoImagem = 1024;
    int tentativasPorItem = 3;
    bool indexarNaBusca = true;
};

struct AiScanFalha {
    std::string itemId;
    std::string arquivo;
    std::string motivo;
    int httpStatus = 0;
};

struct AiScanRelatorio {
    int solicitados = 0;
    int sucessos = 0;
    int ignorados = 0;
    std::vector<AiScanFalha> falhas;
    bool cancelado = false;
    std::string erroFatal;   // aborta o lote: chave inválida, sem billing, modelo inexistente
    juce::String modeloUsado;
};

// feito/total para a barra; arquivoAtual para o operador saber onde está.
using AoProgressoScan = std::function<void(int feito, int total, const juce::String& arquivoAtual)>;

// NÃO CHAME ISTO DA MESSAGE THREAD. Faz I/O de rede bloqueante por item e
// roda ffmpeg; a UI congela. Use um Thread/ThreadPoolJob e devolva o
// resultado por MessageManager::callAsync.
AiScanRelatorio executarAiScan(matriz::db::Database& indice,
                               matriz::db::Database& registro,
                               const juce::String& apiKey,
                               const std::vector<std::string>& itemIds,
                               const juce::File& pastaProjeto,
                               const AiScanOpcoes& opcoes = {},
                               const AoProgressoScan& aoProgresso = {},
                               matriz::app::CancelamentoPtr cancelamento = nullptr);

// Reindexa em busca_fts tudo que já está em ai_scan_resultado. Recupera scans
// gravados antes da indexação existir. Devolve quantas linhas indexou.
int reindexarAiScanNaBusca(matriz::db::Database& indice, matriz::db::Database& registro);

std::vector<AiScanResult> resultadosDoItem(matriz::db::Database& indice, const std::string& itemId);

} // namespace matriz::ingest
