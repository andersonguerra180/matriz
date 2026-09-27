#pragma once

// Tradução dos valores de CONTENT (collection_type) entre inglês (gravado no
// banco) e português (exibição) — usada pelo INTAKE e pela lista do Metadata.

#include <JuceHeader.h>
#include <utility>
#include <vector>

namespace matriz::ui {

inline const std::vector<std::pair<juce::String, juce::String>>& mapaTraducoesContent() {
    static const std::vector<std::pair<juce::String, juce::String>> mapa = {
        // Audio
        {"Album", juce::String::fromUTF8("Álbum")},
        {"EP", "EP"},
        {"Single", "Single"},
        {"Compilation", juce::String::fromUTF8("Coletânea")},
        {"Soundtrack", "Trilha Sonora"},
        {"Stems", "Stems / Pistas Separadas"},
        {"Multitracks", "Multitracks"},
        {"Sample Pack", "Pacote de Samples"},
        {"Sample", "Sample"},
        {"Preset", "Preset / Predefinição"},
        {"DAW Session", juce::String::fromUTF8("Sessão de DAW")},
        {"Field Recording", juce::String::fromUTF8("Gravação de Campo")},
        {"Sound FX", "Efeitos Sonoros (SFX)"},
        {"MIDI", "Arquivo MIDI"},
        {"Artist Catalog", juce::String::fromUTF8("Catálogo de Artista")},
        {"Artist Backup", "Backup do Artista"},
        // Video
        {"Raw Footage", juce::String::fromUTF8("Gravação Bruta")},
        {"Home Video", juce::String::fromUTF8("Vídeo Caseiro")},
        {"Music Video", "Videoclipe"},
        {"Film", "Filme / Curta"},
        {"Documentary", juce::String::fromUTF8("Documentário")},
        {"Corporate Video", juce::String::fromUTF8("Vídeo Institucional")},
        {"Commercial", "Comercial / Publicidade"},
        {"Live Performance", "Show / Ao Vivo"},
        {"NLE Project", juce::String::fromUTF8("Projeto de Edição (NLE)")},
        {"Social Media Video", juce::String::fromUTF8("Vídeos para Redes Sociais")},
        {"WhatsApp Video", juce::String::fromUTF8("Vídeo do WhatsApp")},
        {"TV Video", juce::String::fromUTF8("Vídeo de TV")},
        {"YouTube Video", juce::String::fromUTF8("Vídeo do YouTube")},
        {"360 Video", juce::String::fromUTF8("Vídeo 360°")},
        {"Making Of", juce::String::fromUTF8("Making Of")},
        // Image
        {"Photo", "Foto"},
        {"Artwork", juce::String::fromUTF8("Arte / Ilustração")},
        {"Album Cover", juce::String::fromUTF8("Capa de Álbum")},
        {"Poster", juce::String::fromUTF8("Pôster / Cartaz")},
        {"Press / Promotional", juce::String::fromUTF8("Material de Divulgação")},
        {"Image Edit Project", "Projeto de Imagem"},
        {"Graphics", juce::String::fromUTF8("Gráficos / Design")},
        {"Logo", "Logotipo"},
        {"3D", juce::String::fromUTF8("Modelagem 3D")},
        // Docs
        {"Documentation", juce::String::fromUTF8("Documentação")},
        {"Book", "Livro"},
        {"Contract", "Contrato"},
        {"Manual", "Manual"},
        {"Report", juce::String::fromUTF8("Relatório")},
        {"Reference", juce::String::fromUTF8("Referência / Pesquisa")},
        {"Technical Documentation", juce::String::fromUTF8("Documentação Técnica")},
        {"Spreadsheet", "Planilha"},
        {"Planilha", "Planilha"}
    };
    return mapa;
}

inline juce::String traduzirContent(const juce::String& val, bool paraPt) {
    if (val.isEmpty()) return {};
    for (const auto& par : mapaTraducoesContent()) {
        if (paraPt) {
            if (val.equalsIgnoreCase(par.first)) return par.second;
        } else {
            if (val.equalsIgnoreCase(par.second)) return par.first;
        }
    }
    return val;
}

} // namespace matriz::ui
