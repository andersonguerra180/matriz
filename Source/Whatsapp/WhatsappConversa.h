#pragma once

#include <JuceHeader.h>

#include <vector>

// Leitor do .txt da exportação de conversa do WhatsApp (Android e iPhone, PT/EN/ES/FR/DE…).
//
// Lógica pura, sem disco e sem interface: é o que o self-test exercita. Nunca lança por causa do
// formato: toda linha que não é reconhecida vira continuação da mensagem anterior.
//
// A ORDEM DO ARQUIVO É PRESERVADA. O WhatsApp às vezes exporta horários fora de ordem; aqui nada é reordenado.

namespace matriz::whatsapp {

enum class TipoMensagem {
    Texto,            // mensagem comum
    Sistema,          // aviso do sistema (criptografia, código de segurança mudou, …): linha com data mas sem "Autor:"
    Anexo,            // mídia incluída no zip (nome em `anexo`; legenda em `legenda`)
    MidiaOculta,      // <Mídia oculta> / <Media omitted>: a mídia não veio no export
    LigacaoVoz,       // ligação de voz perdida
    LigacaoVideo,     // ligação de vídeo perdida
    SemConteudo,      // autor com texto vazio (figurinha, visualização única ou apagada, no export)
    Apagada           // "Mensagem apagada" / "This message was deleted"
};

struct Mensagem {
    int ano = 0, mes = 0, dia = 0, hora = 0, minuto = 0, segundo = 0;
    juce::String autor;    // vazio em aviso do sistema
    juce::String texto;    // texto, como veio (marcação *negrito* _itálico_ <b> é só aplicada na tela)
    TipoMensagem tipo = TipoMensagem::Texto;
    juce::String anexo;    // só em Anexo
    juce::String legenda;  // só em Anexo: as linhas de continuação

    // "AAAA-MM-DD HH:MM:SS"
    juce::String dataHoraTexto() const;
    // "AAAA-MM-DD"
    juce::String dataTexto() const;
    // Chave comparável (AAAAMMDDHHMM) — o agrupamento de álbum usa "mesmo minuto".
    long long chaveMinuto() const;
    long long chaveDia() const;
};

struct ConversaParseada {
    std::vector<Mensagem> mensagens;
    juce::StringArray autores;      // na ordem em que aparecem, sem repetir (sem diferenciar maiúsculas)
    bool formatoIphone = false;     // "[DD/MM/AAAA, HH:MM:SS] Autor: …"
    juce::String idioma = "pt";     // "pt" ou "en" (pelas palavras dos avisos e anexos)
    bool grupo = false;             // 3 ou mais participantes
    bool diaPrimeiro = true;        // DD/MM (true) ou MM/DD (false)
    // Primeira e última data (menor e maior; não depende da ordem do arquivo). 0 = conversa vazia.
    long long primeiraChaveDia = 0, ultimaChaveDia = 0;
    int cabecalhosReconhecidos = 0;

    bool valida() const { return cabecalhosReconhecidos > 0; }
};

// Lê o texto completo (UTF-8 já decodificado).
ConversaParseada lerConversa(const juce::String& textoCompleto);

// Assinatura de conversa do WhatsApp: a MAIORIA das primeiras linhas (até 15 não vazias) é cabeçalho de mensagem
// (Android "12/03/2024 14:32 - Fulano: …" ou iPhone "[12/03/24 14:32:10] Fulano: …"). Linhas de continuação de
// mensagens de várias linhas não derrubam a regra (basta 40% de cabeçalhos, e a primeira linha útil tem que ser um).
// Barato: serve de checagem antes de ler o arquivo inteiro.
bool primeirasLinhasSaoDeWhatsapp(const juce::String& inicioDoTexto);

// Sinais que reforçam (não obrigatórios): nome de arquivo de mídia do WhatsApp (IMG-/VID-/AUD-/PTT-/DOC-/STK- + data + -WA…)
bool nomeDeMidiaDoWhatsapp(const juce::String& nomeDoArquivo);

// Subpasta de mês de uma mídia pela data no nome do arquivo: "2024-03" ou "sem-data".
// IMG-20240312-WA0004.jpg, 00000012-PHOTO-2024-03-12-14-03-25.jpg, …
juce::String subpastaDeMes(const juce::String& nomeDoArquivo);

// O nome do .txt dentro do zip segue o padrão de exportação do WhatsApp?
// "Conversa do WhatsApp com X.txt", "WhatsApp Chat with X.txt", "_chat.txt", e equivalentes de outros idiomas.
bool nomeDeArquivoEhDeExportacao(const juce::String& nomeDoTxt);

// X em "Conversa do WhatsApp com X.txt" / "WhatsApp Chat - X.txt". Vazio quando o padrão não traz o nome (_chat.txt).
juce::String nomeDoContatoNoArquivo(const juce::String& nomeDoTxt);

// "12 de julho de 2022" / "July 12, 2022"
juce::String nomeDoDia(int ano, int mes, int dia, bool portugues);
// "DD/MM/AAAA" a partir de AAAAMMDD
juce::String dataCurta(long long chaveDia);

// Extensões: classificação dos anexos para a tela da conversa.
enum class ClasseAnexo { Foto, Video, Audio, Figurinha, Documento };
ClasseAnexo classeDoAnexo(const juce::String& nomeDoArquivo);
// Áudios que são convertidos para Ogg Vorbis na ingestão (.opus, .m4a, .aac, .amr).
bool ehAudioParaConverter(const juce::String& nomeDoArquivo);

// Aplica o que a tela mostra formatado (*negrito*, _itálico_, <b>…</b>): devolve o texto sem marcação e os
// intervalos em negrito/itálico (índices no texto devolvido). O texto guardado nunca muda.
struct TrechoFormatado {
    int inicio = 0, fim = 0;
    bool negrito = false, italico = false;
};
juce::String removerMarcacao(const juce::String& texto, std::vector<TrechoFormatado>& trechos);

// Mesmo nome para comparação (sem invisíveis, sem diferenciar maiúsculas).
juce::String chaveDeNome(const juce::String& nome);

} // namespace matriz::whatsapp
