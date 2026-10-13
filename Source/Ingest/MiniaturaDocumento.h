#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Gera uma miniatura (capa) de um documento (.pdf, .doc, .docx, .pages, .rtf, etc.)
// gravando uma imagem JPEG redimensionada para ladoMaximoPx em destinoPath.
// Retorna true e preenche larguraOut/alturaOut se teve sucesso;
// false se não foi possível gerar (cai pro ícone genérico da extensão).
bool gerarMiniaturaDocumentoNativa(const char* origemPath, const char* destinoPath,
                                    int ladoMaximoPx, int* larguraOut, int* alturaOut);

#ifdef __cplusplus
}
#endif
