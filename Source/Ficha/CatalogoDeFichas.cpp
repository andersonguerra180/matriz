#include "CatalogoDeFichas.h"

#include <JuceHeader.h>

#include <algorithm>

namespace matriz::ficha {

std::vector<TipoFichaInfo> listarTodosOsTipos(const std::string& fichasDir) {
    juce::File dir(fichasDir);
    if (!dir.isDirectory())
        throw FichaDefinitionError("diretório de fichas não encontrado: " + fichasDir);

    std::vector<TipoFichaInfo> out;
    for (const auto& arquivo : dir.findChildFiles(juce::File::findFiles, false, "*.yaml")) {
        std::string id = arquivo.getFileNameWithoutExtension().toStdString();
        TipoFichaInfo info{id, loadFromFile(arquivo.getFullPathName().toStdString())};
        out.push_back(std::move(info));
    }

    std::sort(out.begin(), out.end(), [](const TipoFichaInfo& a, const TipoFichaInfo& b) {
        if (a.definicao.ordem != b.definicao.ordem) return a.definicao.ordem < b.definicao.ordem;
        return a.id < b.id;
    });

    return out;
}

std::vector<TipoFichaInfo> listarTiposPorModo(const std::string& fichasDir, const std::string& modo) {
    std::vector<TipoFichaInfo> out;
    for (auto& info : listarTodosOsTipos(fichasDir)) {
        const auto& modos = info.definicao.modos;
        bool disponivel = modos.empty() || std::find(modos.begin(), modos.end(), modo) != modos.end();
        if (disponivel) out.push_back(std::move(info));
    }
    return out;
}

} // namespace matriz::ficha
