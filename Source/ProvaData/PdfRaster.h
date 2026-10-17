#pragma once

#include <JuceHeader.h>

#include <vector>

// Escritor mínimo de PDF: uma imagem (JPEG) por página A4. Sem dependências além do JUCE.
// O texto não é selecionável (cada página é uma imagem), mas o arquivo abre em qualquer leitor e o hash dele é estável.

namespace matriz::provadata {

// Tamanho de página usado pelas telas desenhadas aqui: A4 a 150 dpi.
constexpr int kPaginaLarguraPx = 1240;
constexpr int kPaginaAlturaPx = 1754;

// Escreve `paginas` (imagens RGB do tamanho acima) em `saida`. `titulo` e `autor` vão nos metadados. false se falhar.
bool escreverPdfDePaginas(const std::vector<juce::Image>& paginas, const juce::File& saida, const juce::String& titulo, const juce::String& autor,
                          float qualidadeJpeg = 0.86f);

} // namespace matriz::provadata
