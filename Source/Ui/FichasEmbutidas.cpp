#include "FichasEmbutidas.h"

#include <FichasBinaryData.h>

namespace matriz::ui {

const std::map<std::string, std::string>& fichasEmbutidas() {
    static const auto fichas = [] {
        std::map<std::string, std::string> m;
        for (int i = 0; i < FichasBinaryData::namedResourceListSize; ++i) {
            const char* nome = FichasBinaryData::namedResourceList[i];
            const char* original = FichasBinaryData::getNamedResourceOriginalFilename(nome);
            int tamanho = 0;
            const char* dados = FichasBinaryData::getNamedResource(nome, tamanho);
            if (original == nullptr || dados == nullptr) continue;
            std::string id(original);
            if (id.size() > 5 && id.compare(id.size() - 5, 5, ".yaml") == 0) id.resize(id.size() - 5);
            m.emplace(std::move(id), std::string(dados, static_cast<size_t>(tamanho)));
        }
        return m;
    }();
    return fichas;
}

} // namespace matriz::ui
