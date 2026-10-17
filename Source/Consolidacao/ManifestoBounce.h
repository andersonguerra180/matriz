#pragma once

#include <JuceHeader.h>

#include <string>
#include <vector>

#include "../Db/Database.h"

// Manifesto da Bounce List: arquivo JSON na RAIZ da pasta exportada com tudo o que é preciso para ela voltar idêntica
// no INTAKE (hash do original E da cópia, ficha de catálogo, marcadores P/W/K/E/B, nests e a folder structure).
// Formato aberto e portátil (JSON UTF-8, caminhos com "/"). Nada aqui toca em arquivo exportado nem em original.
namespace matriz::consolidacao {

inline constexpr const char* kNomeManifestoBounce = "matriz-bounce-manifest.json";
inline constexpr int kVersaoFormatoManifestoBounce = 1;

struct ArquivoExportadoBounce {
    std::string itemId;
    std::string arquivoId;
    juce::String caminhoRelativo;  // relativo à pasta exportada, com "/"
};

struct EstruturaManifestoBounce {
    bool porVisualEditor = false;
    std::string hierarquiaCsv;  // porVisualEditor: os níveis do Visual Editor
    std::string mapaId;         // !porVisualEditor: folder map usado ("" = sem mapa; as pastas saem dos caminhos)
};

// `copiaIdentica`: true quando a cópia é byte a byte o original (modo Original, sem embed/limpeza): o hash da cópia
// reaproveita o do original em vez de reler o arquivo. false = a cópia é relida e o hash dela é calculado.
// Devolve false (com `erro`) se não deu para gravar; o export em si já terminou nesse ponto.
bool escreverManifestoBounce(matriz::db::Database& registro, const juce::File& destino,
                             const std::vector<ArquivoExportadoBounce>& arquivos, const EstruturaManifestoBounce& estrutura,
                             bool copiaIdentica, const juce::String& versaoMatriz, std::string& erro);

}  // namespace matriz::consolidacao
