#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Decodifica um PSD/PSB via ImageIO nativo do macOS (JUCE não tem codec pra
// isso) e grava uma miniatura JPEG já redimensionada pro lado máximo dado.
// Retorna true e preenche larguraOut/alturaOut em caso de sucesso; false se
// não conseguiu decodificar (quem chama cai pro ícone genérico da categoria).
bool gerarMiniaturaPsdNativa(const char* origemPath, const char* destinoPath,
                              int ladoMaximoPx, int* larguraOut, int* alturaOut);

#ifdef __cplusplus
}
#endif
