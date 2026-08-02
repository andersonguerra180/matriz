#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "FichaYaml.h"

// Modelo tipado de uma definição de ficha (§6 da especificação) e o
// carregador que valida um Node de FichaYaml contra o formato descrito em
// docs/formato-ficha.md. Uma definição que viole o formato nunca produz um
// FichaDefinition parcial — o carregamento lança FichaDefinitionError.

namespace matriz::ficha {

class FichaDefinitionError : public std::runtime_error {
public:
    explicit FichaDefinitionError(const std::string& message) : std::runtime_error(message) {}
};

enum class CampoTipo {
    Texto,
    Numero,
    Inteiro,
    Data,
    Booleano,
    Opcao,
    OpcaoLivre,
    ListaPessoas,
    Tabela
};

CampoTipo campoTipoFromString(const std::string& s);
std::string campoTipoToString(CampoTipo t);

enum class VisivelSeOperador { In, Igual, Diferente };

struct VisivelSe {
    std::string campoId;
    VisivelSeOperador op;
    std::vector<std::string> valores;
};

struct Campo {
    std::string id;
    std::string rotulo;
    CampoTipo tipo = CampoTipo::Texto;
    bool obrigatorio = false;
    std::vector<std::string> opcoes;
    std::vector<std::string> colunas;             // obrigatório quando tipo == Tabela
    std::string validacao;                         // "" = ausente. Um de: isrc, ean13, soma_100
    std::optional<VisivelSe> visivelSe;
    bool herdaDoProjeto = false;
    std::string preenchidoPor;                     // "" = ausente
    std::string sugeridoPor;                        // "" = ausente
    std::vector<std::string> afeta;                 // cada item validado contra o conjunto conhecido
    std::string alertaSeTrue;                        // "" = ausente. Só faz sentido com tipo == Booleano
    bool gerarEmSequencia = false;
};

struct Grupo {
    std::string rotulo;
    std::vector<Campo> campos;
};

struct ArquivoEsperado {
    std::string papel;
    bool obrigatorio = false;
    std::string por;      // "" = não repete por nível
    std::string minimo;   // "" = sem mínimo declarado (ex.: "3000x3000")
};

struct FichaDefinition {
    std::string tipo;
    std::string rotulo;
    std::vector<std::string> niveis;                       // vazio => usa `grupos`
    std::vector<Grupo> grupos;                              // usado quando niveis está vazio
    std::vector<std::pair<std::string, std::vector<Campo>>> camposPorNivel; // usado quando niveis não está vazio, na ordem de niveis
    std::vector<ArquivoEsperado> arquivosEsperados;

    bool usaNiveis() const { return !niveis.empty(); }

    // Todos os campos de todos os grupos/níveis, achatados, na ordem de definição.
    std::vector<const Campo*> todosCampos() const;

    const std::vector<Campo>* camposDoNivel(const std::string& nivel) const;
};

// Efeitos e validações nomeadas reconhecidos por esta versão do parser
// (docs/formato-ficha.md — "Novas validações/efeitos exigem código novo").
bool isValidacaoConhecida(const std::string& nome);
bool isEfeitoConhecido(const std::string& nome);

FichaDefinition load(const matriz::ficha_yaml::Node& root);
FichaDefinition loadFromFile(const std::string& path);

} // namespace matriz::ficha
