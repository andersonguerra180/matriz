#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Parser de um subconjunto de YAML — não é um parser YAML completo.
// Cobre exatamente o que os arquivos em fichas/*.yaml usam: mapas em bloco,
// sequências em bloco ("- item"), sequências em bloco de mapas
// ("- chave: valor" seguido de linhas mais indentadas), sequências inline
// ("[a, b, c]"), escalares com e sem aspas, comentários com "#" fora de
// aspas. Não suporta âncoras, tags, escalares multilinha (| ou >), nem
// mapas/sequências inline aninhados dentro de fluxo.
//
// Ver docs/formato-ficha.md para o formato que este parser precisa aceitar.

namespace matriz::ficha_yaml {

class YamlError : public std::runtime_error {
public:
    explicit YamlError(const std::string& message, int lineNumber = -1)
        : std::runtime_error(lineNumber >= 0
                                  ? ("linha " + std::to_string(lineNumber) + ": " + message)
                                  : message),
          line(lineNumber) {}

    int line;
};

enum class NodeType { Null, Scalar, Sequence, Map };

class Node {
public:
    NodeType type = NodeType::Null;
    std::string scalar;                       // válido quando type == Scalar
    std::vector<Node> sequence;                // válido quando type == Sequence
    std::vector<std::pair<std::string, Node>> map; // válido quando type == Map (preserva ordem)

    bool isNull() const { return type == NodeType::Null; }
    bool isScalar() const { return type == NodeType::Scalar; }
    bool isSequence() const { return type == NodeType::Sequence; }
    bool isMap() const { return type == NodeType::Map; }

    // Acesso a chave de mapa. Retorna nó Null se ausente.
    const Node& get(const std::string& key) const {
        static const Node nullNode{};
        for (auto& kv : map)
            if (kv.first == key)
                return kv.second;
        return nullNode;
    }

    bool has(const std::string& key) const {
        for (auto& kv : map)
            if (kv.first == key)
                return true;
        return false;
    }

    std::string asString(const std::string& fallback = "") const {
        return type == NodeType::Scalar ? scalar : fallback;
    }

    bool asBool(bool fallback = false) const {
        if (type != NodeType::Scalar) return fallback;
        if (scalar == "true") return true;
        if (scalar == "false") return false;
        return fallback;
    }

    std::vector<std::string> asStringList() const {
        std::vector<std::string> out;
        if (type == NodeType::Sequence)
            for (auto& n : sequence)
                out.push_back(n.asString());
        return out;
    }
};

// Ponto de entrada: parseia o texto YAML inteiro e retorna o nó raiz
// (sempre um Map para os arquivos de definição de ficha). Lança YamlError
// em qualquer texto malformado — nunca retorna um resultado parcial.
Node parse(const std::string& yamlText);

} // namespace matriz::ficha_yaml
