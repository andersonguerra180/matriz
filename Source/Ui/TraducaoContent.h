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
        {"Preset", juce::String::fromUTF8("Preset / Predefinição")},
        {"DAW Session", juce::String::fromUTF8("Sessão de DAW")},
        {"Field Recording", juce::String::fromUTF8("Gravação de Campo")},
        {"Ambience Recording", juce::String::fromUTF8("Gravação de Ambiente")},
        {"Voiceover", juce::String::fromUTF8("Locução")},
        {"Speech", "Depoimento"},
        {"Sound FX", "Efeitos Sonoros (SFX)"},
        {"MIDI", "Arquivo MIDI"},
        {"Artist Catalog", juce::String::fromUTF8("Catálogo de Artista")},
        {"Artist Backup", "Backup do Artista"},
        {"Demo", "Demo"},
        {"Demo Tape", "Demo"},  // valor antigo: lido como Demo
        {"Raw Audio", juce::String::fromUTF8("Áudio Bruto")},
        {"Live Concert", "Concerto ao Vivo"},
        {"VS", "VS"},
        {"Impulse Response", "Resposta ao Impulso (IR)"},
        {"Mix", "Mixagem"},
        {"Master", juce::String::fromUTF8("Masterização")},
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
        {"Tutorial", "Tutorial"},
        {"CCTV (Security Camera)", juce::String::fromUTF8("CFTV (Câmera de Segurança)")},
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
        {"YouTube Thumbnail", juce::String::fromUTF8("Miniatura do YouTube")},
        {"Exhibition", juce::String::fromUTF8("Exposição")},
        {"Raw Photos", "Fotos RAW"},
        {"360 Photo", "Foto 360"},
        {"Technical Photography", juce::String::fromUTF8("Fotografia Técnica")},
        // Docs
        {"Documentation", juce::String::fromUTF8("Documentação")},
        {"Book", "Livro"},
        {"Release", "Release"},
        {"Article", "Artigo"},
        {"Press", juce::String::fromUTF8("Matéria")},
        {"Text", "Texto"},
        {"Journal", juce::String::fromUTF8("Diário")},
        {"Contract", "Contrato"},
        {"Personal Document", "Documento Pessoal"},
        {"Manual", "Manual"},
        {"Report", juce::String::fromUTF8("Relatório")},
        {"Reference", juce::String::fromUTF8("Referência / Pesquisa")},
        {"Technical Documentation", juce::String::fromUTF8("Documentação Técnica")},
        {"Sheet", "Planilha"},
        {"Spreadsheet", "Planilha"},  // valor antigo: lido como Sheet
        {"Zip", "Zip"},
        {"Rar", "Rar"},
        {"PDF", "PDF"},
        {"Doc/Docx", "Doc/Docx"},
        {"Markdown", "Markdown"},
        {"Website", "Website"},
        {"DAW", "DAW"},
        {"NLE", "NLE"},
        {"CSV", "CSV"},
        {"MTZ Collection", juce::String::fromUTF8("Coleção MTZ")},
        {"BKM Catalog", juce::String::fromUTF8("Catálogo BKM")}
    };
    return mapa;
}

inline juce::String traduzirContent(const juce::String& val, bool paraPt) {
    if (val.isEmpty()) return {};
    if (!paraPt && val.equalsIgnoreCase("Spreadsheet")) return "Sheet";  // nome antigo
    if (!paraPt && val.equalsIgnoreCase("Demo Tape")) return "Demo";  // nome antigo
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
