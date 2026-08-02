#include "FichaYaml.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace matriz::ficha_yaml {

namespace {

struct Line {
    int indent = 0;
    std::string text; // conteúdo já sem indentação, sem comentário, sem \r
    int number = 0;
};

std::string rtrim(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\r\n");
    return end == std::string::npos ? std::string() : s.substr(0, end + 1);
}

std::string trim(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return rtrim(s.substr(begin));
}

// Remove comentário "# ..." que esteja fora de aspas simples/duplas.
std::string stripComment(const std::string& s) {
    bool inSingle = false, inDouble = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\'' && !inDouble) inSingle = !inSingle;
        else if (c == '"' && !inSingle) inDouble = !inDouble;
        else if (c == '#' && !inSingle && !inDouble) {
            if (i == 0 || std::isspace(static_cast<unsigned char>(s[i - 1])))
                return s.substr(0, i);
        }
    }
    return s;
}

std::vector<Line> tokenizeLines(const std::string& yamlText) {
    std::vector<Line> lines;
    std::istringstream stream(yamlText);
    std::string raw;
    int lineNo = 0;
    while (std::getline(stream, raw)) {
        ++lineNo;
        std::string noComment = stripComment(raw);
        std::string content = rtrim(noComment);
        if (trim(content).empty())
            continue; // linha em branco ou só comentário

        size_t firstNonSpace = content.find_first_not_of(' ');
        if (firstNonSpace == std::string::npos)
            continue;
        if (content.find('\t') != std::string::npos)
            throw YamlError("tabs não são permitidos para indentação, use espaços", lineNo);

        Line l;
        l.indent = static_cast<int>(firstNonSpace);
        l.text = content.substr(firstNonSpace);
        l.number = lineNo;
        lines.push_back(std::move(l));
    }
    return lines;
}

// Remove aspas de um escalar e resolve palavras reservadas (~ / null).
std::string parseScalarText(const std::string& raw, int lineNo) {
    std::string s = trim(raw);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    if (s.find('"') != std::string::npos || s.find('\'') != std::string::npos) {
        // aspas não fecham corretamente — provavelmente erro de digitação no YAML
        if (!(s.front() == '"' || s.front() == '\''))
            return s; // aspas no meio de texto livre (ex.: alerta com aspas) é aceitável
    }
    return s;
}

// Parseia um valor de fluxo inline: "[a, b, "c d", ...]" — usado em `opcoes:`.
Node parseFlowSequence(const std::string& raw, int lineNo) {
    std::string s = trim(raw);
    if (s.size() < 2 || s.front() != '[' || s.back() != ']')
        throw YamlError("sequência inline malformada: " + raw, lineNo);
    std::string inner = s.substr(1, s.size() - 2);

    Node seq;
    seq.type = NodeType::Sequence;

    std::string current;
    bool inSingle = false, inDouble = false;
    auto flush = [&]() {
        std::string item = trim(current);
        if (!item.empty()) {
            Node n;
            n.type = NodeType::Scalar;
            n.scalar = parseScalarText(item, lineNo);
            seq.sequence.push_back(std::move(n));
        }
        current.clear();
    };
    for (char c : inner) {
        if (c == '\'' && !inDouble) { inSingle = !inSingle; current += c; }
        else if (c == '"' && !inSingle) { inDouble = !inDouble; current += c; }
        else if (c == ',' && !inSingle && !inDouble) { flush(); }
        else { current += c; }
    }
    flush();
    return seq;
}

Node parseScalarOrFlow(const std::string& raw, int lineNo) {
    std::string s = trim(raw);
    if (!s.empty() && s.front() == '[') {
        return parseFlowSequence(s, lineNo);
    }
    Node n;
    n.type = NodeType::Scalar;
    n.scalar = parseScalarText(s, lineNo);
    return n;
}

bool isDashLine(const std::string& text) {
    return text == "-" || (text.size() >= 2 && text[0] == '-' && text[1] == ' ');
}

// Encontra o primeiro ": " (ou ":" no fim da linha) fora de aspas — separa
// chave de valor num mapa em bloco.
bool splitKeyValue(const std::string& text, std::string& key, std::string& value) {
    bool inSingle = false, inDouble = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\'' && !inDouble) inSingle = !inSingle;
        else if (c == '"' && !inSingle) inDouble = !inDouble;
        else if (c == ':' && !inSingle && !inDouble) {
            if (i + 1 == text.size() || text[i + 1] == ' ') {
                key = trim(text.substr(0, i));
                value = i + 1 <= text.size() ? trim(text.substr(i + 1)) : std::string();
                return true;
            }
        }
    }
    return false;
}

Node parseBlock(const std::vector<Line>& lines, size_t& index, int indent);

// Parseia o valor associado a uma chave de mapa ou a um item de sequência
// quando o "value" na mesma linha está vazio (ou seja, o valor real está
// nas linhas seguintes, mais indentadas).
Node parseNestedValue(const std::vector<Line>& lines, size_t& index, int parentIndent) {
    if (index >= lines.size() || lines[index].indent <= parentIndent) {
        Node n;
        n.type = NodeType::Null;
        return n;
    }
    return parseBlock(lines, index, lines[index].indent);
}

Node parseMap(const std::vector<Line>& lines, size_t& index, int indent) {
    Node node;
    node.type = NodeType::Map;
    while (index < lines.size() && lines[index].indent == indent && !isDashLine(lines[index].text)) {
        const Line& line = lines[index];
        std::string key, value;
        if (!splitKeyValue(line.text, key, value))
            throw YamlError("esperava \"chave: valor\", encontrei: " + line.text, line.number);
        if (key.empty())
            throw YamlError("chave vazia", line.number);
        ++index;
        Node valueNode;
        if (value.empty()) {
            valueNode = parseNestedValue(lines, index, indent);
        } else {
            valueNode = parseScalarOrFlow(value, line.number);
        }
        node.map.emplace_back(key, std::move(valueNode));
    }
    return node;
}

Node parseSequence(const std::vector<Line>& lines, size_t& index, int indent) {
    Node node;
    node.type = NodeType::Sequence;
    while (index < lines.size() && lines[index].indent == indent && isDashLine(lines[index].text)) {
        const Line& line = lines[index];
        std::string afterDash = line.text.size() > 1 ? line.text.substr(2) : std::string();
        int contentIndent = indent + 2; // posição padrão logo após "- "

        if (trim(afterDash).empty()) {
            ++index;
            Node item = parseNestedValue(lines, index, indent);
            node.sequence.push_back(std::move(item));
            continue;
        }

        std::string key, value;
        if (splitKeyValue(afterDash, key, value)) {
            // "- chave: valor" inicia um mapa cujo próximo campo aparece em
            // linhas subsequentes alinhadas com o início do conteúdo após "- ".
            Node itemMap;
            itemMap.type = NodeType::Map;
            Node firstValue;
            size_t savedIndex = index + 1;
            if (value.empty()) {
                firstValue = parseNestedValue(lines, savedIndex, contentIndent - 1);
            } else {
                firstValue = parseScalarOrFlow(value, line.number);
            }
            itemMap.map.emplace_back(key, std::move(firstValue));
            index = savedIndex;

            while (index < lines.size() && lines[index].indent == contentIndent && !isDashLine(lines[index].text)) {
                const Line& sub = lines[index];
                std::string k2, v2;
                if (!splitKeyValue(sub.text, k2, v2))
                    throw YamlError("esperava \"chave: valor\" dentro de item de sequência: " + sub.text, sub.number);
                ++index;
                Node v2Node = v2.empty() ? parseNestedValue(lines, index, contentIndent)
                                          : parseScalarOrFlow(v2, sub.number);
                itemMap.map.emplace_back(k2, std::move(v2Node));
            }
            node.sequence.push_back(std::move(itemMap));
        } else {
            // "- valor escalar" simples (ex.: item de lista de strings sem aspas em bloco)
            node.sequence.push_back(parseScalarOrFlow(afterDash, line.number));
            ++index;
        }
    }
    return node;
}

Node parseBlock(const std::vector<Line>& lines, size_t& index, int indent) {
    if (index >= lines.size())
        throw YamlError("fim inesperado do arquivo", -1);
    if (lines[index].indent != indent)
        throw YamlError("indentação inesperada", lines[index].number);

    if (isDashLine(lines[index].text))
        return parseSequence(lines, index, indent);
    return parseMap(lines, index, indent);
}

} // namespace

Node parse(const std::string& yamlText) {
    std::vector<Line> lines = tokenizeLines(yamlText);
    if (lines.empty()) {
        Node n;
        n.type = NodeType::Map;
        return n;
    }
    size_t index = 0;
    int rootIndent = lines[0].indent;
    Node root = parseBlock(lines, index, rootIndent);
    if (index != lines.size())
        throw YamlError("conteúdo sobrando após o bloco raiz (indentação inconsistente?)", lines[index].number);
    return root;
}

} // namespace matriz::ficha_yaml
