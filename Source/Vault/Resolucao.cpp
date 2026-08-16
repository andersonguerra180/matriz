#include "Resolucao.h"

namespace matriz::vault {

namespace {

// Candidatos na ordem de preferência descrita no header. Entradas vazias são
// puladas pelo chamador.
std::vector<juce::File> candidatos(const juce::File& pastaProjeto, const std::string& localizacaoVault,
                                   const std::string& caminhoRelativo,
                                   const std::string& caminhoAbsolutoOrigem) {
    std::vector<juce::File> out;
    if (!localizacaoVault.empty() && !caminhoRelativo.empty())
        out.push_back(juce::File(juce::String(localizacaoVault)).getChildFile(juce::String(caminhoRelativo)));
    if (!caminhoAbsolutoOrigem.empty())
        out.push_back(juce::File(juce::String(caminhoAbsolutoOrigem)));
    if (!caminhoRelativo.empty() && pastaProjeto != juce::File())
        out.push_back(pastaProjeto.getChildFile(juce::String(caminhoRelativo)));
    return out;
}

} // namespace

std::optional<juce::File> resolverCaminho(const juce::File& pastaProjeto, const std::string& localizacaoVault,
                                          const std::string& caminhoRelativo,
                                          const std::string& caminhoAbsolutoOrigem) {
    for (auto& f : candidatos(pastaProjeto, localizacaoVault, caminhoRelativo, caminhoAbsolutoOrigem))
        if (f.existsAsFile()) return f;
    return std::nullopt;
}

juce::File caminhoEsperado(const juce::File& pastaProjeto, const std::string& localizacaoVault,
                           const std::string& caminhoRelativo, const std::string& caminhoAbsolutoOrigem) {
    auto lista = candidatos(pastaProjeto, localizacaoVault, caminhoRelativo, caminhoAbsolutoOrigem);
    for (auto& f : lista)
        if (f.existsAsFile()) return f;
    return lista.empty() ? juce::File() : lista.front();
}

std::optional<juce::File> resolverArquivo(matriz::db::Database& registro, const std::string& arquivoId,
                                          const juce::File& pastaProjeto) {
    auto stmt = registro.prepare(std::string("SELECT ") + colunasDeResolucao() +
                                 " FROM arquivo a " + joinDeResolucao() + " WHERE a.id = ?");
    stmt.bind(1, matriz::db::Value::of(arquivoId));
    if (!stmt.step()) return std::nullopt;
    return resolverCaminho(pastaProjeto, stmt.columnText(0), stmt.columnText(1), stmt.columnText(2));
}

} // namespace matriz::vault
