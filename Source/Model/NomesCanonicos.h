#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Db/Database.h"

// Nomes case-insensitive (pacote de collection, Fase 1).
//
// Tags, pessoas e subjects: o mesmo nome ignorando maiúsculas/minúsculas e
// espaços nas pontas é o MESMO registro. Acentos continuam distinguindo
// ("São" ≠ "Sao"). O texto que fica é o da primeira ocorrência gravada no
// projeto — toda gravação passa por aqui e troca o que o operador digitou
// pela grafia que já existe, então filtros, contagens e autocomplete veem
// uma entrada só sem precisar mudar a leitura.
//
// Onde cada um mora (é o que a ficha lê e grava):
//   - TAGS    → item_tag.tag
//   - PEOPLE  → também item_tag (o seletor PEOPLE põe o nome como tag);
//               collection_person.nome é só a lista de nomes do projeto
//               que o seletor oferece. Tags e pessoas dividem o mesmo
//               vocabulário. `entidade`/`item_entidade` não são usadas
//               por nenhuma tela, e não existe "lugar" como entidade.
//   - SUBJECT → item.dc_subject (espelhado em item_campo 'dc_subject'),
//               texto livre com vários subjects separados por , ou ;
//               (a tabela `assunto`/`item_assunto` não é escrita pela UI).
//
// Minúsculas via juce::String::toLowerCase (Unicode: "SÃO" → "são"); o
// lower() do SQLite só conhece ASCII, por isso a comparação final é sempre
// em C++. Nas consultas pontuais o LIKE só pré-filtra (cada caractere
// não-ASCII da chave vira '_').

namespace matriz::model::nomes {

// trim + minúsculas. Acentos preservados.
std::string chave(const std::string& nome);

// "Show, Backstage; Tour" → {"Show", "Backstage", "Tour"} (aparados, sem vazios;
// vírgula/ponto e vírgula entre aspas não separam — mesma regra do filtro
// SUBJECT do Catalog).
std::vector<std::string> dividirSubjects(const std::string& valor);

// Troca cada subject da lista pela forma dada por `canonico` e tira os
// repetidos (pela chave). Sem nenhuma mudança devolve `valor` intacto
// (separadores e espaços como estavam).
std::string canonizarListaSubjects(const std::string& valor,
                                   const std::function<std::string(const std::string&)>& canonico);

// Consulta pontual (uma gravação): a grafia já gravada no projeto, ou o
// próprio nome aparado se ainda não existir.
std::string tagCanonica(matriz::db::Database& registro, const std::string& nome);  // tags e pessoas
std::string subjectCanonico(matriz::db::Database& registro, const std::string& nome);
// Lista inteira de dc_subject pronta pra gravar.
std::string subjectsCanonicos(matriz::db::Database& registro, const std::string& valor);

// Vocabulário carregado uma vez — pra lotes (pacote, merge, migração).
// canonico() registra nomes novos, então o 2º "show" de um mesmo lote vira
// o "Show" do 1º.
class Vocabulario {
public:
    enum class Tipo { Tags, Subjects };
    static Vocabulario carregar(matriz::db::Database& registro, Tipo tipo);
    std::string canonico(const std::string& nome);
    std::string listaSubjects(const std::string& valor);

private:
    std::unordered_map<std::string, std::string> porChave_;
};

// --- Migração de projetos existentes ---------------------------------------

struct GrupoUnificacao {
    enum class Tipo { Tags, Subjects } tipo = Tipo::Tags;
    std::string canonico;
    std::vector<std::string> variantes;  // grafias que viram `canonico` (sem ela)
    int itens = 0;                       // itens com alguma das variantes
};

// Só leitura: o que seria unificado. Vazio = nada a fazer.
std::vector<GrupoUnificacao> levantarUnificacao(matriz::db::Database& registro);

struct ResultadoUnificacao {
    bool ok = false;
    juce::String erro;
    juce::File backup;
    int grupos = 0;
    int itensAlterados = 0;
};

// Copia o registro (VACUUM INTO) pra `pastaProjeto`, aplica numa transação
// só e registra no log do projeto. Roda fora da message thread.
ResultadoUnificacao aplicarUnificacao(matriz::db::Database& registro, const juce::File& pastaProjeto);

}  // namespace matriz::model::nomes
