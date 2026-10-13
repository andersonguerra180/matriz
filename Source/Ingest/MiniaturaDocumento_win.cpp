#include "MiniaturaDocumento.h"

#if defined(_WIN32) || defined(_WIN64)

bool gerarMiniaturaDocumentoNativa(const char* /*origemPath*/, const char* /*destinoPath*/,
                                    int /*ladoMaximoPx*/, int* /*larguraOut*/, int* /*alturaOut*/) {
    return false;
}

#endif
