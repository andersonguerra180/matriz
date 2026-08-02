#pragma once

#include <JuceHeader.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "../Db/Database.h"

// Projeto portátil (P5): uma pasta no disco contendo os arquivos e o banco.
// Copiou pro HD externo, abriu em outra máquina, funciona. Esta classe é o
// único ponto de entrada para criar ou abrir essa pasta — garante que a
// estrutura (registro.sqlite, indice.sqlite, subpastas) sempre existe antes
// de qualquer outra camada tocar o projeto.

namespace matriz::model {

class ProjectError : public std::runtime_error {
public:
    explicit ProjectError(const std::string& message) : std::runtime_error(message) {}
};

enum class Modo { Preservacao, Catalogo };

std::string modoToString(Modo m);
Modo modoFromString(const std::string& s);

struct NovoProjetoParams {
    std::string nome;
    Modo modo = Modo::Preservacao;
    std::string prefixoNomenclatura;
    std::string instituicaoOuSelo;
    std::string responsavel;
    std::string isrcRegistrante; // só modo catálogo
};

class Project {
public:
    // Cria a estrutura de pastas + registro.sqlite + indice.sqlite em
    // `pastaProjeto` (que deve estar vazia ou não existir ainda) e grava a
    // linha única de `projeto`. Lança ProjectError se a pasta já contiver um
    // projeto ou não puder ser criada.
    static std::unique_ptr<Project> criar(const juce::File& pastaProjeto, const NovoProjetoParams& params);

    // Abre uma pasta de projeto existente, validando a estrutura mínima
    // (registro.sqlite e indice.sqlite presentes com schema aplicado).
    static std::unique_ptr<Project> abrir(const juce::File& pastaProjeto);

    const juce::File& pasta() const { return pastaProjeto_; }
    matriz::db::Database& registro() { return *registro_; }
    matriz::db::Database& indice() { return *indice_; }

    std::string projetoId() const { return projetoId_; }

private:
    Project(juce::File pastaProjeto, std::unique_ptr<matriz::db::Database> registro,
            std::unique_ptr<matriz::db::Database> indice, std::string projetoId);

    juce::File pastaProjeto_;
    std::unique_ptr<matriz::db::Database> registro_;
    std::unique_ptr<matriz::db::Database> indice_;
    std::string projetoId_;
};

// Timestamp ISO-8601 UTC ("YYYY-MM-DDTHH:MM:SSZ"), a convenção de todo
// timestamp gravado em registro.sqlite/indice.sqlite.
std::string agoraIso8601();

// UUID v4 em minúsculas, a convenção de todo id gravado no banco.
std::string novoUuid();

} // namespace matriz::model
