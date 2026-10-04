#pragma once

#include <JuceHeader.h>
#include <functional>

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

struct MetadadoParaEmbutir {
    std::string titulo;
    std::string descricao;
    std::string artista;
    std::string codigoAcervo;
    std::string tipoMidia;
    std::optional<int> ano;
    // Resumo do AI SCAN, quando existe. Vai para Xmp.dc.subject / a descrição
    // do arquivo, de modo que o contexto detectado viaje junto com a cópia e
    // não fique preso no banco do projeto.
    std::string resumoAi;
};

enum class StatusEmbedding {
    Embedded,
    Unsupported,
    Failed,
    NoMetadata
};

MetadadoParaEmbutir coletarMetadadosDoItem(matriz::db::Database& registro, const std::string& itemId,
                                           matriz::db::Database* indice = nullptr);

StatusEmbedding embutirMetadadosNoArquivo(const juce::File& destino, const MetadadoParaEmbutir& meta);

int embutirMetadadosNoBackup(matriz::db::Database& registro, const juce::File& raizDestino);

struct ResultadoEmbedding {
    int sucesso = 0;
    int naoSuportados = 0;
    int falha = 0;
    std::vector<std::string> erros;
};
ResultadoEmbedding embutirMetadadosEmItens(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                            const std::vector<std::string>& itemIds);

// ------------------------------------------------------------ Sidecars XMP
// Etapa 8. XMP padrão (Dublin Core + xmp:CreatorTool), legível por
// Lightroom/Bridge/Resolve, com o nome completo do arquivo: ACR-001.wav.xmp.
// O banco é a verdade; o sidecar é reflexo. No MAIN há masters (selados) e
// sidecars (atualizáveis). sidecar_registro guarda o SHA-256 de cada sidecar
// que o Matriz escreveu: sidecar alterado por fora (ou que não foi escrito
// pelo Matriz, ex. do cliente) nunca é sobrescrito em silêncio.

// Pacote XMP de um item (vazio se não há nada a dizer).
std::string gerarPacoteXmp(matriz::db::Database& registro, const std::string& itemId);
// Grava o pacote ao lado de `arquivo` (arquivo.ext.xmp) — pra EXPORT, sem registro.
bool escreverSidecarAvulso(matriz::db::Database& registro, const std::string& itemId, const juce::File& arquivo);

// Resultado de gravar o sidecar de UM arquivo do MAIN.
enum class ResultadoSidecarUnico { Escrito, Igual, EditadoPorFora, SemArquivo, SemDados, Falha };
// A mesma rotina de atualizarSidecarsNoMain aplicada a um arquivo (`caminhoRelativo` = relativo a Media/):
// escreve arquivo.ext.xmp com os metadados atuais do item e o registra em sidecar_registro. Sidecar em dia é
// pulado; sidecar editado fora do Matriz nunca é sobrescrito (sobrescreverEditados = escolha explícita do operador).
ResultadoSidecarUnico gravarSidecarDoArquivo(matriz::db::Database& registro, const juce::File& media,
                                             const std::string& itemId, const std::string& arquivoId,
                                             const juce::String& caminhoRelativo, bool sobrescreverEditados = false);

struct ResultadoSidecars {
    int escritos = 0;
    int iguais = 0;
    int falhas = 0;
    std::vector<juce::String> editadosPorFora;  // caminhos relativos a Media/
};
// Um sidecar por arquivo registrado no MAIN (`media` = <raiz do MAIN>/Media,
// `destinoIdMain` = destination_id dele; registros legados sem destino contam).
// sobrescreverEditados = true só com escolha explícita do operador.
ResultadoSidecars atualizarSidecarsNoMain(matriz::db::Database& registro, const juce::File& media,
                                          const std::string& destinoIdMain, bool sobrescreverEditados = false,
                                          const std::function<bool(int, int)>& aoProgredir = {});
// "Importar": lê os sidecars editados por fora de volta pro catálogo
// (título, descrição, autor, direitos, tags) e regrava o sidecar do Matriz.
int importarSidecarsEditados(matriz::db::Database& registro, const juce::File& media, const std::string& destinoIdMain,
                             const std::vector<juce::String>& caminhosRelativos);


} // namespace matriz::consolidacao
