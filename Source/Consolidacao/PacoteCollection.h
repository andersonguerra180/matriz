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
//       matriz-pacote.json   catálogo dos dados de ficha, um registro por arquivo
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

struct ResultadoPacote {
    juce::File pasta;       // <destino>/<nome> (com sufixo numérico se já existia)
    int copiados = 0;
    int semPasta = 0;       // da seleção, sem pasta no folder map escolhido — não entram
    int foraDoMain = 0;     // ainda não entraram no MAIN — não entram
    std::vector<std::string> falhas;
    bool cancelado = false; // cancelado: a pasta do pacote é apagada (nada dela é aproveitável)
};

// Roda fora da message thread. Só lê o projeto (nada nele muda); os
// arquivos saem do MAIN, cópia verificada por tamanho e SHA-256.
ResultadoPacote gerarPacote(matriz::db::Database& registro, const juce::File& pastaProjeto,
                            const juce::File& destinoPai, const juce::String& nomePacote,
                            const std::string& mapaId, const juce::String& nomeMapa,
                            const juce::String& nomeColecao, const std::set<std::string>& itens,
                            const AoProgredir& aoProgredir);

// --- INTAKE (Fase 3) ---------------------------------------------------------

struct RegistroArquivo {
    std::string sha256;            // do arquivo entregue no pacote
    std::string caminho;           // relativo a Media/, com '/'
    std::vector<juce::String> pasta;  // nomes da raiz até a pasta do item no folder map
    DadosFicha dados;
};

struct Pacote {
    juce::File pasta;              // raiz do pacote (tem Media/ e o JSON)
    juce::String colecao, folderMap, exportadoEm;
    juce::var pastas;              // árvore [{nome, pastas: [...]}]
    std::vector<RegistroArquivo> arquivos;
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
// message thread.
ResultadoLeitura lerPacote(const juce::File& pasta);

// Grava os dados de ficha no item (item novo do INTAKE). Nomes pela
// unificação da Fase 1 ("Show" do pacote vira o "Show" que já existe);
// pessoas entram também na lista PEOPLE. Quem chama segura a transação.
void gravarDadosFicha(matriz::db::Database& registro, const std::string& itemId, const DadosFicha& dados,
                      matriz::model::nomes::Vocabulario& vocabTags, matriz::model::nomes::Vocabulario& vocabSubjects,
                      const std::string& autor);

}  // namespace matriz::consolidacao::pacote
