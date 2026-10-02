#pragma once

#include <map>
#include <string>

namespace matriz::ui {

// Os YAMLs de fichas/ embutidos no binário (juce_add_binary_data MatrizFichasAssets;
// CMake os descobre por glob, então acrescentar um tipo continua sendo escrever um
// arquivo em fichas/). id do tipo (nome do arquivo sem ".yaml") -> texto do YAML.
// Antes o app os lia de MATRIZ_FICHAS_DIR, a pasta do código-fonte desta máquina:
// em outro Mac não havia fichas nem lista de tipos de mídia. Thread-safe.
const std::map<std::string, std::string>& fichasEmbutidas();

} // namespace matriz::ui
