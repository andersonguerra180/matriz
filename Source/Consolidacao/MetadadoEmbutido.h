#pragma once

#include <JuceHeader.h>

#include <string>
#include <vector>

#include "../Db/Database.h"

// Gravação de marcadores como metadado embutido NA CÓPIA DE BACKUP (item
// 8.3). O arquivo original permanece byte a byte idêntico — esta função só
// toca no arquivo de destino, DEPOIS de ele ter sido copiado e verificado.
//
// Formatos cobertos nesta etapa:
// - WAV: chunk "cue " (pontos de marcação) + "adtl"/"labl" (o texto de cada
//   um), que é como Sound Forge/Audition/Reaper leem marcador em WAV, mais
//   um chunk "iXML" com a lista completa em XML (BWF/iXML). Escrito à mão:
//   é RIFF, e as bibliotecas que fariam isso (BWF MetaEdit) são ferramenta
//   de linha de comando, não biblioteca embutível.
// - MP4/MOV (capítulos) e outros formatos NÃO são cobertos: exigiriam
//   remuxar o container com ffmpeg, e reescrever um container de vídeo é
//   operação com risco de perda que não cabe sem verificação byte a byte
//   dedicada. Declarado, não fingido — o marcador continua na ficha e no
//   relatório, que é onde a informação nunca se perde.

namespace matriz::consolidacao {

struct MarcadorParaEmbutir {
    double segundos = 0.0;
    std::string texto;
};

// Lê os marcadores de um item (item_observacao com minutagem_ms).
std::vector<MarcadorParaEmbutir> marcadoresDoItem(matriz::db::Database& registro, const std::string& itemId);

// Acrescenta os chunks de marcador a um WAV já existente no destino.
// Devolve false quando o arquivo não é um WAV válido ou não há marcador —
// nunca lança, e nunca deixa o arquivo pela metade (escreve num temporário e
// só então substitui).
bool embutirMarcadoresEmWav(const juce::File& wavDestino, const std::vector<MarcadorParaEmbutir>& marcadores);

// Aplica a todos os itens do backup que tenham marcador e cuja cópia seja
// WAV. Devolve quantos arquivos foram enriquecidos.
int embutirMarcadoresNoBackup(matriz::db::Database& registro, const juce::File& raizDestino);

} // namespace matriz::consolidacao
