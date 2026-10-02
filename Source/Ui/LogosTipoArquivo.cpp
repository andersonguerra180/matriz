#include "LogosTipoArquivo.h"

#include <AssetsBinaryData.h>

#include <map>
#include <utility>

namespace matriz::ui {

namespace {

// nome original do arquivo -> (dados, tamanho), montado uma vez a partir da lista que o JUCE gera.
const std::map<juce::String, std::pair<const char*, int>>& indiceDeLogos() {
    static const auto indice = [] {
        std::map<juce::String, std::pair<const char*, int>> m;
        for (int i = 0; i < AssetsBinaryData::namedResourceListSize; ++i) {
            const char* nome = AssetsBinaryData::namedResourceList[i];
            int tamanho = 0;
            const char* dados = AssetsBinaryData::getNamedResource(nome, tamanho);
            if (const char* original = AssetsBinaryData::getNamedResourceOriginalFilename(nome))
                if (dados != nullptr) m.emplace(juce::String::fromUTF8(original), std::make_pair(dados, tamanho));
        }
        return m;
    }();
    return indice;
}

} // namespace

juce::Image carregarLogoDeTipo(const juce::String& nomeArquivo) {
    const auto& indice = indiceDeLogos();
    auto it = indice.find(nomeArquivo);
    if (it == indice.end()) return {};
    return juce::ImageFileFormat::loadFrom(it->second.first, static_cast<size_t>(it->second.second));
}

} // namespace matriz::ui
