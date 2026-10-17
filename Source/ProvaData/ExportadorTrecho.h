#pragma once

#include <JuceHeader.h>

#include <functional>
#include <optional>

#include "../Whatsapp/WhatsappConversa.h"
#include "ProvaData.h"

// "Exportar trecho" da conversa do WhatsApp: PDF (uma imagem por página, A4) com as mensagens escolhidas, o recibo de data do próprio
// PDF e o registro.json, tudo numa pasta "Trecho - <conversa> - <data>/". Roda em background (decodifica imagens, calcula hashes, usa a rede).

namespace matriz::provadata {

struct MidiaDoTrecho {
    juce::File arquivo;   // vazio = a mídia não está no backup
    juce::String sha256;  // vazio = calcula do arquivo
};

struct OpcoesTrecho {
    bool miniaturasDeFotos = true;
    bool incluirDadosDoComputador = false;  // só vale se o registro da conversa tem esses dados
    bool incluirArquivoOriginal = false;
    juce::File pastaDestino;
};

struct EntradaTrecho {
    const matriz::whatsapp::ConversaParseada* conversa = nullptr;
    int primeiraMensagem = 0, ultimaMensagem = 0;      // índices inclusive
    juce::String contato, voce;
    juce::File arquivoOriginal;                        // o zip (ou conversa.txt) guardado no backup
    juce::String nomeOriginal;                         // nome como entrou
    juce::String shaOriginal;                          // vazio = calcula
    juce::File manifestoDoOriginal;                    // Project/provas/<id>.registro.json (pode não existir)
    bool portugues = true;
    std::function<MidiaDoTrecho(const juce::String& nomeDoAnexo)> resolverMidia;
    std::function<bool()> cancelado;
};

struct ResultadoTrecho {
    bool ok = false;
    juce::File pasta;
    juce::File pdf;
    juce::String erro;
    int paginas = 0;
    EstadoProva estadoCarimboPdf;
};

// `usarRede`: tenta carimbar o PDF e o registro agora (sem internet: ficam na fila e saem sozinhos depois).
ResultadoTrecho exportarTrecho(const EntradaTrecho& entrada, const OpcoesTrecho& opcoes, bool usarRede);

// Troca emojis e símbolos que a fonte pode não ter por "[emoji]" (uma vez por sequência).
juce::String trocarEmojisPorMarcador(const juce::String& texto);

} // namespace matriz::provadata
