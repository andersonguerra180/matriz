#pragma once

#include <JuceHeader.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../Db/Database.h"
#include "Consolidacao.h"
#include "../Model/NomesCanonicos.h"

// Pacote de collection (EXPORT → INTAKE).
//
// B gera, pelo EXPORT, uma pasta:
//     <Nome do pacote>/
//       Media/               arquivos copiados do MAIN (byte a byte), pelo folder map escolhido
//       matriz-pacote.json   catálogo dos dados de ficha, um registro por arquivo, mais a
//                            seleção exportada e as pendências (o que ficou de fora e por quê)
//       matriz-pacote.parcial.json  só durante o export (ou se ele foi interrompido): o
//                            andamento, para o COMPLETE PACKAGE retomar; o INTAKE o ignora
// e A ingere essa pasta pelo INTAKE como uma ingestão normal, casando cada
// arquivo pelo SHA-256 com o registro do JSON.
//
// Mapa dos campos (Passo 0 — onde a ficha realmente lê e grava; é essa a
// fonte que o pacote carrega, senão o dado chega e não aparece):
//   SUBJECT      item.dc_subject (se vazio, item_campo 'dc_subject'); lista
//                separada por , ou ; → "subjects": [..]
//   EVENT DATE   item.ano (se vazio, item_campo 'ano'), texto livre → "event_date"
//   CONTENT      item.collection_type → "content"  (item.content_type NÃO é o CONTENT da ficha)
//   GEO LOCATION asset_geolocation (coordenadas + campos de endereço) → "geo" {coluna: valor}
//   TAGS         item_tag.tag que não é pessoa → "tags"
//   PEOPLE       item_tag.tag cujo nome está na lista PEOPLE (collection_person) → "pessoas".
//                O seletor PEOPLE grava a pessoa como tag; `entidade`/
//                `item_entidade` não são usadas por nenhuma tela e não existe
//                "lugar" como entidade — não há o que carregar ali.
//   Título       item.dc_title (TITLE da ficha; vazio = a ficha mostra o nome do arquivo) → "titulo"
//   Descrição    item.dc_description → "descricao"
//   Marcadores   item_observacao (texto + minutagem_ms — a timeline e a ficha
//                usam a MESMA linha) → "marcadores": [{"tempo_s", "texto", "titulo"}].
//                A UI só tem marcador de ponto; a tabela `marcador` (trecho,
//                tempo_fim) não é escrita por nenhuma tela. Observação sem
//                minutagem vai sem "tempo_s".
// Campos vazios são omitidos.

namespace matriz::consolidacao::pacote {

inline constexpr const char* kArquivoJson = "matriz-pacote.json";
inline constexpr const char* kArquivoParcial = "matriz-pacote.parcial.json";
inline constexpr const char* kFormato = "matriz-pacote";
inline constexpr int kVersao = 1;

struct Marcador {
    std::optional<double> tempoS;
    std::string texto;
    std::string titulo;
};

struct DadosFicha {
    std::string titulo, descricao, eventDate, content;
    std::vector<std::string> subjects, tags, pessoas;
    juce::var geo;  // objeto só com os campos preenchidos; void = sem geo
    std::vector<Marcador> marcadores;
    bool vazio() const;
};

// Lê do registro os campos do mapa acima. `chavesPessoas`: chaves
// (nomes::chave) da lista PEOPLE do projeto — separam pessoas de tags.
DadosFicha lerDadosFicha(matriz::db::Database& registro, const std::string& itemId,
                         const std::set<std::string>& chavesPessoas);
std::set<std::string> chavesDaListaPessoas(matriz::db::Database& registro);

// Soma os campos no objeto JSON (omitindo vazios) / lê de volta.
void escreverJson(const DadosFicha& dados, juce::DynamicObject& destino);
DadosFicha lerJson(const juce::var& registro);

// Item da seleção que não entrou no pacote, com o motivo em código (os kMotivo abaixo).
struct Pendencia {
    std::string itemId, codigo, motivo, detalhe;
};
inline constexpr const char* kMotivoForaDoMain = "fora_do_main";  // ainda não está no MAIN
inline constexpr const char* kMotivoConflito = "conflito_nome";   // outro arquivo já tem esse caminho
inline constexpr const char* kMotivoCopia = "falha_copia";        // a cópia falhou (disco, permissão)
inline constexpr const char* kMotivoChecksum = "checksum";        // a cópia não confere com o MAIN
inline constexpr const char* kMotivoEmbutir = "embutir";          // não deu pra embutir os metadados
inline constexpr const char* kMotivoErro = "erro";                // outro erro

struct ResultadoPacote {
    juce::File pasta;       // <destino>/<nome> (com sufixo numérico se já existia)
    int copiados = 0;
    int semPasta = 0;       // da seleção, sem pasta no folder map escolhido — não entram
    int foraDoMain = 0;     // ainda não entraram no MAIN — não entram
    std::vector<std::string> falhas;
    std::vector<Pendencia> pendencias;  // fora do MAIN + falhas, por item (gravadas no JSON)
    bool cancelado = false; // cancelado: a pasta do pacote é apagada (nada dela é aproveitável)
};

// Roda fora da message thread. Só lê o projeto (nada nele muda); os
// arquivos saem do MAIN, cópia verificada por tamanho e SHA-256.
ResultadoPacote gerarPacote(matriz::db::Database& registro, const juce::File& pastaProjeto,
                            const juce::File& destinoPai, const juce::String& nomePacote,
                            const std::string& mapaId, const juce::String& nomeMapa,
                            const juce::String& nomeColecao, const std::set<std::string>& itens,
                            const AoProgredir& aoProgredir, bool embutirMetadados = false);

// --- INTAKE (Fase 3) ---------------------------------------------------------

struct RegistroArquivo {
    std::string sha256;            // do arquivo entregue no pacote
    std::string caminho;           // relativo a Media/, com '/'
    std::vector<juce::String> pasta;  // nomes da raiz até a pasta do item no folder map
    std::string itemId, codigo;    // item de origem (vazio em pacotes anteriores)
    juce::int64 tamanho = 0;       // tamanho entregue (0 = desconhecido, pacotes anteriores)
    DadosFicha dados;
};

struct Pacote {
    juce::File pasta;              // raiz do pacote (tem Media/ e o JSON)
    juce::String colecao, folderMap, exportadoEm;
    juce::var pastas;              // árvore [{nome, pastas: [...]}]
    std::vector<RegistroArquivo> arquivos;
    std::vector<std::string> selecao;  // itens exportados (vazio em pacotes anteriores)
    std::string mapaId;                // vazio em pacotes anteriores
    std::optional<bool> embutir;       // nullopt em pacotes anteriores
    std::vector<Pendencia> pendencias;
    bool parcial = false;              // lido do JSON parcial: o export não terminou
    juce::File media() const { return pasta.getChildFile("Media"); }
    juce::String nome() const { return pasta.getFileName(); }
};

enum class StatusLeitura { NaoEPacote, Ok, VersaoDesconhecida, Invalido };
struct ResultadoLeitura {
    StatusLeitura status = StatusLeitura::NaoEPacote;
    Pacote pacote;
    juce::String erro;  // Invalido/VersaoDesconhecida: o porquê
};

// Só olha se a pasta tem o JSON (barato — chamável na message thread).
bool pareceUmPacote(const juce::File& pasta);
// Lê e valida (formato e versão conhecidos, Media/ presente). Disco: fora da
// message thread. `aceitarParcial`: sem o JSON final, lê o parcial de um export
// interrompido (pacote.parcial = true) — nunca no INTAKE.
ResultadoLeitura lerPacote(const juce::File& pasta, bool aceitarParcial = false);

// --- CHECK PACKAGE -----------------------------------------------------------
// Compara um pacote já feito com o projeto: o que a seleção pede (o mapa dá o
// caminho de cada item), o que o JSON lista e o que está de fato no disco.
// Pacote anterior à seleção gravada: a seleção é refeita do próprio pacote
// (os itens do catálogo + os que falharam no export, pelos relatórios salvos
// em Project/relatorios); só sem nada disso vale a seleção atual do card B.
// Pacote de export interrompido (só o JSON parcial): verificado do mesmo jeito.

enum class TipoProblema {
    ArquivoAusente,   // o JSON lista, o arquivo não está no disco (ou está vazio)
    ArquivoAlterado,  // está no disco com outro tamanho (cópia interrompida/truncada)
    NaoExportado,     // da seleção, mas não está no pacote (falha no export, ou ele foi interrompido)
    ForaDoMain,       // ainda não está no MAIN: nada sai antes do MAIN
    ConflitoNome      // dois itens dariam o mesmo caminho e o planejador não resolveu
};

struct Problema {
    TipoProblema tipo = TipoProblema::NaoExportado;
    std::string itemId, codigo;
    std::string caminho;  // onde o arquivo deveria estar, relativo a Media/
    std::string detalhe;  // motivo gravado pelo export, quando há
    // O COMPLETE PACKAGE resolve sozinho (copia ou recopia do MAIN).
    bool completavel() const {
        return tipo == TipoProblema::ArquivoAusente || tipo == TipoProblema::ArquivoAlterado ||
               tipo == TipoProblema::NaoExportado;
    }
};

struct ResultadoVerificacao {
    StatusLeitura status = StatusLeitura::NaoEPacote;
    juce::String erro;
    juce::File pasta;
    juce::String exportadoEm, nomeMapa;
    std::string mapaId;
    bool parcial = false;            // o export foi interrompido
    bool selecaoReconstruida = false;  // pacote anterior: seleção refeita do catálogo + relatórios
    int doCatalogo = 0, doRelatorio = 0, registrosSemItem = 0;
    bool usouSelecaoAtual = false;   // pacote anterior sem nada para refazer: a seleção é a do card B agora
    bool cancelado = false;
    int selecionados = 0, ok = 0;
    std::vector<Problema> problemas;
    int completaveis() const {
        int n = 0;
        for (const auto& p : problemas) n += p.completavel() ? 1 : 0;
        return n;
    }
};

// Roda fora da message thread; só lê. `mapas`: (id, nome) dos folder maps do
// projeto — acha o mapa do pacote pelo id gravado, senão pelo nome; sem nenhum,
// `mapaPadrao`.
ResultadoVerificacao verificarPacote(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                     const juce::File& pastaPacote, const std::set<std::string>& selecaoAtual,
                                     const std::vector<std::pair<std::string, juce::String>>& mapas,
                                     const std::string& mapaPadrao, const AoProgredir& aoProgredir = {});

// --- COMPLETE PACKAGE --------------------------------------------------------
// Resolve no MESMO pacote o que a verificação acha completável: copia do MAIN o
// que falta, recopia o que sumiu ou foi cortado e retoma um export interrompido
// (arquivo que já está lá e confere por SHA-256 com o MAIN é só catalogado).
// Nunca passa por cima de um arquivo de outro item do catálogo (vai com _N) e
// só troca um arquivo depois que a cópia nova conferiu. Grava o JSON final
// (o parcial sai) e anota no log.md. Rodar de novo não copia nada.
struct ResultadoCompletar {
    int copiados = 0, adotados = 0;
    std::vector<std::string> falhas;
    std::vector<Pendencia> pendencias;  // o que continua de fora (fora do MAIN, falhas)
    bool cancelado = false;             // o que já foi feito fica no catálogo
};

ResultadoCompletar completarPacote(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                   const juce::File& pastaPacote, const std::set<std::string>& selecaoAtual,
                                   const std::vector<std::pair<std::string, juce::String>>& mapas,
                                   const std::string& mapaPadrao, const AoProgredir& aoProgredir = {});

// Grava os dados de ficha no item (item novo do INTAKE). Nomes pela
// unificação da Fase 1 ("Show" do pacote vira o "Show" que já existe);
// pessoas entram também na lista PEOPLE. Quem chama segura a transação.
void gravarDadosFicha(matriz::db::Database& registro, const std::string& itemId, const DadosFicha& dados,
                      matriz::model::nomes::Vocabulario& vocabTags, matriz::model::nomes::Vocabulario& vocabSubjects,
                      const std::string& autor);

}  // namespace matriz::consolidacao::pacote
