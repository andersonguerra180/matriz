#include "ExistentesProjeto.h"

namespace matriz::celular {

ConjuntoExistentes existentesDoProjeto(matriz::model::Project& projeto) {
    ConjuntoExistentes out;
    auto& db = projeto.registroLeitura();
    auto st = db.prepare("SELECT caminho_absoluto_origem, caminho_relativo, tamanho_bytes FROM arquivo WHERE tamanho_bytes > 0");
    while (st.step()) {
        juce::String caminho = st.columnIsNull(0) ? juce::String() : juce::String::fromUTF8(st.columnText(0).c_str());
        if (caminho.trim().isEmpty()) caminho = juce::String::fromUTF8(st.columnText(1).c_str());
        const juce::String nome = caminho.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false);
        if (nome.isEmpty()) continue;
        out.insert(chaveDoArquivo(nome.toStdString(), static_cast<std::int64_t>(st.columnInt(2))));
    }
    return out;
}

} // namespace matriz::celular
