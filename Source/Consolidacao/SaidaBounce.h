#pragma once

#include <JuceHeader.h>

#include <string>

#include "../Db/Database.h"

// Metadados na saída do EXPORT > Bounce List. Só a CÓPIA exportada é tocada: a MAIN e os originais nunca.
//
//   Original  cópia idêntica ao arquivo da MAIN.
//   Embed     grava dentro da cópia a ficha do Matriz (title, description, subject, event date, content, geo, tags,
//             people), onde o formato aceita.
//   Clean     remove EXIF, IPTC, XMP e GPS da cópia (preserva orientação e perfil ICC; RAW sai sem limpeza; vídeo/áudio
//             têm o container limpo sem recodificar).
//
// "Include sidecar (.xmp)" vale em Original e Embed. A tag B é interna: nunca entra em nenhum metadado.
namespace matriz::consolidacao {

enum class ModoMetadadosSaida { Padrao, Original, Embed, Clean };

struct OpcoesMetadadosSaida {
    ModoMetadadosSaida modo = ModoMetadadosSaida::Padrao;  // Padrao = fluxo antigo (embutir/Bridge), sem mexer
    bool sidecar = false;                                  // só Original/Embed
    bool embedVideo = true;                                // só Embed: false = vídeo não é reescrito (ffmpeg); sai com sidecar .xmp
};

// Escolhas da janela SETTINGS da Bounce List, salvas no projeto (`bounce_list.json` na pasta do projeto).
// Limpar e Embutir são exclusivos (nenhum = cópia idêntica ao original).
struct ConfiguracaoBounce {
    bool limpar = false;
    bool embutir = false;
    bool sidecar = true;
    bool manifesto = true;
    bool embedVideo = true;
    OpcoesMetadadosSaida paraExport() const {
        OpcoesMetadadosSaida o;
        o.modo = limpar ? ModoMetadadosSaida::Clean : embutir ? ModoMetadadosSaida::Embed : ModoMetadadosSaida::Original;
        o.sidecar = o.modo != ModoMetadadosSaida::Clean && sidecar;
        o.embedVideo = embedVideo;
        return o;
    }
};
ConfiguracaoBounce carregarConfiguracaoBounce(const juce::File& pastaProjeto);
bool salvarConfiguracaoBounce(const juce::File& pastaProjeto, const ConfiguracaoBounce& cfg);

enum class StatusLimpeza {
    Limpo,           // metadados removidos
    RawSemLimpeza,   // RAW: copiado sem limpeza (contado e avisado no fim)
    TipoSemSuporte,  // formato sem limpeza implementada (PDF, HEIC, ...): copiado sem alteração (contado e avisado)
    Falhou           // não deu pra limpar: quem chama DEVE apagar a cópia (nunca deixar arquivo "sujo" passar por limpo)
};

// Limpa `copia` no lugar. `erro` é preenchido quando Falhou.
StatusLimpeza limparMetadadosDaCopia(const juce::File& copia, std::string& erro);

enum class StatusEmbedFicha {
    Embutido,
    NaoSuportado,  // o formato não aceita metadado embutido (use o sidecar)
    SemDados,      // a ficha não tem nada a gravar
    Falhou
};

// Grava a ficha do item na cópia (imagem via EXIF/IPTC; áudio/vídeo via tags do container, sem recodificar).
StatusEmbedFicha embutirFichaMatrizNaCopia(matriz::db::Database& registro, const std::string& itemId,
                                           const juce::File& copia, std::string& erro);

// "<arquivo>.xmp" ao lado da cópia, com a ficha completa. false se não há nada a escrever ou falhou.
bool escreverSidecarFichaMatriz(matriz::db::Database& registro, const std::string& itemId, const juce::File& copia);

bool ehArquivoRaw(const juce::File& arquivo);

// Container de vídeo (o embed reescreve o arquivo inteiro via ffmpeg: lento em arquivo grande).
bool ehArquivoVideo(const juce::File& arquivo);

} // namespace matriz::consolidacao
