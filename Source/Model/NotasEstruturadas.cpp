#include "NotasEstruturadas.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace matriz::model {

const char* const kOutraMetadataTitulo = "OTHER METADATA";

namespace {

// Prefixo usado antes desta estrutura existir (ver IngestArquivo.cpp) —
// reconhecido só para migrar itens já ingeridos, nunca mais escrito.
const char* const kPrefixoLegado = "Others (metadata):\n";

std::string aparar(const std::string& s) {
    size_t inicio = s.find_first_not_of(" \t\r\n");
    if (inicio == std::string::npos) return "";
    size_t fim = s.find_last_not_of(" \t\r\n");
    return s.substr(inicio, fim - inicio + 1);
}

std::string maiusculo(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::toupper(c); });
    return r;
}

// Uma linha "cabeçalho de seção" é a linha inteira (já aparada) entre
// colchetes, ex.: "[USER NOTES]". Retorna o título sem colchetes, ou string
// vazia se a linha não é um cabeçalho.
bool ehLinhaCabecalho(const std::string& linhaAparada, std::string& tituloOut) {
    if (linhaAparada.size() < 3) return false;
    if (linhaAparada.front() != '[' || linhaAparada.back() != ']') return false;
    tituloOut = aparar(linhaAparada.substr(1, linhaAparada.size() - 2));
    return !tituloOut.empty();
}

} // namespace

std::vector<SecaoNota> parseNotasEstruturadas(const std::string& texto) {
    std::vector<SecaoNota> secoes;
    if (aparar(texto).empty()) return secoes;

    std::vector<std::string> preambulo;
    SecaoNota* atual = nullptr;
    std::vector<std::string> conteudoAtual;

    auto flush = [&]() {
        if (atual) {
            atual->conteudo = aparar([&] {
                std::ostringstream oss;
                for (size_t i = 0; i < conteudoAtual.size(); ++i) {
                    if (i) oss << '\n';
                    oss << conteudoAtual[i];
                }
                return oss.str();
            }());
            conteudoAtual.clear();
        }
    };

    std::istringstream stream(texto);
    std::string linha;
    while (std::getline(stream, linha)) {
        if (!linha.empty() && linha.back() == '\r') linha.pop_back();
        std::string titulo;
        if (ehLinhaCabecalho(aparar(linha), titulo)) {
            flush();
            secoes.push_back(SecaoNota{titulo, "", false});
            atual = &secoes.back();
        } else if (atual) {
            conteudoAtual.push_back(linha);
        } else {
            preambulo.push_back(linha);
        }
    }
    flush();

    std::ostringstream preOss;
    for (size_t i = 0; i < preambulo.size(); ++i) {
        if (i) preOss << '\n';
        preOss << preambulo[i];
    }
    std::string textoPreambulo = aparar(preOss.str());

    if (!textoPreambulo.empty()) {
        if (textoPreambulo.rfind(kPrefixoLegado, 0) == 0) {
            SecaoNota s;
            s.titulo = kOutraMetadataTitulo;
            s.conteudo = aparar(textoPreambulo.substr(std::string(kPrefixoLegado).size()));
            s.automatica = true;
            secoes.insert(secoes.begin(), std::move(s));
        } else {
            SecaoNota s;
            s.titulo = "NOTES";
            s.conteudo = textoPreambulo;
            s.automatica = false;
            secoes.insert(secoes.begin(), std::move(s));
        }
    }

    for (auto& s : secoes) {
        if (maiusculo(s.titulo) == kOutraMetadataTitulo) {
            s.titulo = kOutraMetadataTitulo;
            s.automatica = true;
        }
    }

    return secoes;
}

std::string serializarNotasEstruturadas(const std::vector<SecaoNota>& secoes) {
    std::ostringstream out;
    bool primeira = true;
    for (const auto& s : secoes) {
        std::string titulo = aparar(s.titulo);
        if (titulo.empty()) continue;
        if (!primeira) out << "\n\n";
        primeira = false;
        out << "[" << maiusculo(titulo) << "]\n" << aparar(s.conteudo);
    }
    return out.str();
}

} // namespace matriz::model
