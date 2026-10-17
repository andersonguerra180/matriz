#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../Db/Database.h"
#include "../Ingest/IngestaoStaging.h"
#include "../Ingest/SobreMaterial.h"
#include "../Model/Project.h"
#include "WhatsappConversa.h"

// Conversas do WhatsApp: detecção (zip, pasta descompactada ou .txt solto), extração, conversão dos áudios para
// Ogg Vorbis, ingestão no projeto de staging (UM item por conversa; as mídias ficam dentro do grupo desse item) e leitura
// do que foi gravado (tela da conversa e plano do backup).
//
// 100% offline. Nada aqui toca a message thread com disco: quem chama roda isto em background.

namespace matriz::whatsapp {

// Tags automáticas de toda conversa do WhatsApp (e "celular" em tudo que vem da página do celular).
inline const char* const kTagWhatsapp = "whatsapp";
inline const char* const kTagCelular = "celular";
// Valor gravado em CONTENT (collection_type); a tradução "Conversa de WhatsApp" está em TraducaoContent.h.
inline const char* const kContentConversa = "WhatsApp Chat";
// Pasta fixa no nível de cima do MAIN (dentro de FILES/), qualquer que seja a organização do backup.
inline const char* const kPastaRaizWhatsapp = "WhatsApp";
// Nome do .txt da exportação dentro da pasta da conversa no backup.
inline const char* const kNomeConversaTxt = "conversa.txt";

enum class OrigemConversa { Zip, Pasta, Txt };

struct ConversaDetectada {
    OrigemConversa tipo = OrigemConversa::Zip;
    juce::File zip;               // a raiz da entrada: o .zip, a pasta descompactada ou o .txt solto
    juce::File txtNoDisco;        // Pasta/Txt: o .txt da exportação
    juce::String entradaTxt;      // Zip: caminho do .txt dentro do zip
    juce::String contato;         // "Fulano" em "Conversa do WhatsApp com Fulano"
    ConversaParseada conversa;    // o .txt já lido
    juce::Time dataExportacao;    // modificação do zip/pasta/.txt (o servidor do celular preserva o lastModified)
    // Arquivos que esta conversa consome: não entram de novo como arquivos comuns no mesmo drop.
    std::vector<juce::File> abrangidos;

    juce::String periodoInicio() const { return dataCurta(conversa.primeiraChaveDia); }
    juce::String periodoFim() const { return dataCurta(conversa.ultimaChaveDia); }
    juce::String nomeDaOrigem() const { return conversa.formatoIphone ? "iPhone" : "Android"; }
};
using ConversaPtr = std::shared_ptr<const ConversaDetectada>;

// É uma conversa do WhatsApp? Precisa de um .txt cuja maioria das primeiras linhas seja cabeçalho de mensagem.
// `forcar` (recebida pelo Wi-Fi, que já entra marcada como WhatsApp): aceita o primeiro .txt sem checar a assinatura.
// Qualquer outro zip/pasta/.txt devolve nullptr e segue como arquivo comum. Faz acesso a disco (background).
ConversaPtr detectarConversa(const juce::File& entrada, const std::atomic<bool>* cancelar = nullptr, bool forcar = false);

// Varre arquivos e pastas atrás de conversas: .zip, pasta descompactada (com o .txt dentro) e .txt solto.
std::vector<ConversaPtr> detectarConversasEm(const juce::Array<juce::File>& itens, const std::atomic<bool>* cancelar = nullptr, bool forcar = false);

// "WhatsApp/<Nome da conversa>" (relativo a FILES/; nome sem caracteres inválidos).
juce::String pastaRelativaDaConversa(const juce::String& contato);

// Título do item da conversa, no idioma da interface: "Conversa com X" / "Chat with X".
juce::String tituloDaConversa(const juce::String& contato, bool portugues);
// "Conversa do WhatsApp com X — DD/MM/AAAA a DD/MM/AAAA"
juce::String descricaoDaConversa(const ConversaDetectada& c, bool portugues);

// Monta a lista de argumentos de um processo a partir de textos de tipos misturados.
inline juce::StringArray argumentos(std::initializer_list<juce::String> itens) {
    juce::StringArray a;
    for (const auto& i : itens) a.add(i);
    return a;
}

// ── Áudio ─────────────────────────────────────────────────────────────────────────────────────
struct AudioOriginal {
    bool ok = false;
    juce::String codec;
    int taxa = 0;
    int canais = 0;
    double duracao = 0.0;
};
struct TagsAudio {
    juce::String artist, date, title, album, comment;
};
// ffprobe do primeiro fluxo de áudio.
AudioOriginal sondarAudio(const juce::File& arquivo);
// ffmpeg → Ogg Vorbis (qualidade ~5, mono), com tags e data. false se falhar.
bool converterParaOgg(const juce::File& origem, const juce::File& destino, const TagsAudio& tags);

// ── Ingestão ──────────────────────────────────────────────────────────────────────────────────
struct OpcoesConversaIngestao {
    matriz::ingest::SobreMaterial sobre;   // o que o questionário respondeu (tags já incluem whatsapp e celular)
    bool portugues = true;                 // idioma dos textos gravados (título)
    juce::Time agora = juce::Time::getCurrentTime();
    std::atomic<bool>* cancelar = nullptr;
    std::function<void(int feitos, int total)> aoProgredir;
};

struct ResultadoConversa {
    matriz::ingest::ResultadoIngestao ingestao;
    std::string conversaItemId;
    int midias = 0;
    int audiosConvertidos = 0;
    int audiosNaoConvertidos = 0;
    bool duplicada = false;   // o mesmo conteúdo já estava no backup: nada foi ingerido de novo
    juce::String pastaRelativa;
};

ResultadoConversa ingerirConversa(matriz::model::Project& staging, const ConversaDetectada& conversa, const OpcoesConversaIngestao& opcoes);

// ── Leitura (tela da conversa, plano do backup) ────────────────────────────────────────────────
struct LinhaMidia {
    std::string midiaItemId;
    int ordem = 0;
    juce::String nomeOriginal, nomeArquivo, papel, subpasta, autor, dataHora, legenda;
};

struct InfoConversa {
    std::string itemId;
    std::string txtItemId;
    juce::String contato, voce, periodoInicio, periodoFim, dataExportacao, pastaRelativa, origem, idioma;
    bool voceDefinido = false;  // false = ainda não perguntado
    int nMensagens = 0;
    juce::StringArray autores;
    std::vector<LinhaMidia> midias;
};

// O item é a capa de uma conversa do WhatsApp? (leitura barata: uma linha do banco)
bool itemEhConversa(matriz::db::Database& registro, const std::string& itemId);
// Qualquer item do grupo da conversa (capa, conversa.txt, mídias): devolve o id da capa; vazio se não for.
std::string capaDaConversaDoItem(matriz::db::Database& registro, const std::string& itemId);
std::optional<InfoConversa> lerInfoConversa(matriz::db::Database& registro, const std::string& itemId);

struct DestinoConversa {
    juce::String pasta;  // relativa a FILES/ (ex.: "WhatsApp/Fulano/2024-03")
    juce::String nome;   // vazio = mantém o nome do arquivo
};
// item (a capa e cada item do grupo) -> onde vai no MAIN. Vazio se o projeto não tem conversas.
std::map<std::string, DestinoConversa> destinosDosItensDeConversa(matriz::db::Database& registro);

// Grava quem é "você" (vazio = nenhum dos participantes). Marca como já perguntado.
void gravarVoce(matriz::db::Database& registro, const std::string& itemId, const juce::String& voce);

// A tela precisa só do mapa nome original -> mídia (papel 'midia').
std::map<juce::String, LinhaMidia> mapaMidiasPorNomeOriginal(const InfoConversa& info);

} // namespace matriz::whatsapp
