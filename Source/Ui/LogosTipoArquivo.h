#pragma once

#include <JuceHeader.h>

namespace matriz::ui {

// Logo de um tipo de arquivo (o nome que matriz::ingest::obterLogoParaExtensao
// devolve, ex.: "excel.jpeg", "pro tools.png") lido dos Assets EMBUTIDOS no
// binário (juce_add_binary_data MatrizAssets) — antes vinha de
// MATRIZ_FICHAS_DIR/../Assets, a pasta do código-fonte desta máquina, que não
// existe em outro Mac. Imagem inválida quando o logo não existe (ex.: os que
// ainda não foram fornecidos): o chamador cai no ícone genérico. Thread-safe.
juce::Image carregarLogoDeTipo(const juce::String& nomeArquivo);

} // namespace matriz::ui
