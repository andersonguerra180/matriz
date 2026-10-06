#pragma once

#include <cstdint>
#include <vector>

// Limpeza de metadados de imagem para a CÓPIA exportada (EXPORT > Bounce List, modo Clean). C++ puro (sem JUCE, sem
// banco), operando em memória: o chamador lê a cópia, limpa e regrava. Nada aqui toca no arquivo original/MAIN.
//
// JPEG e PNG são limpos por reescrita de segmentos/chunks, SEM recodificar os pixels: remove EXIF (inclui GPS e
// miniatura embutida), IPTC/Photoshop (APP13), XMP (inclusive ExtendedXMP), comentários (COM), tIME, textos do PNG, e
// qualquer imagem/dado depois do EOI. Preserva o perfil de cor (ICC), o marcador Adobe (APP14, necessário em CMYK/YCCK)
// e a ORIENTAÇÃO, regravada num EXIF mínimo que só contém a tag de rotação.
//
// A lib Exiv2 desta build não tem o toolkit XMP (EXIV2_ENABLE_XMP OFF), por isso ela NÃO é usada para limpar XMP: só
// para ler a orientação.
namespace matriz::consolidacao::limpeza {

// Orientação EXIF (1..8) do conteúdo, ou 0 se não houver. Não lança.
int lerOrientacao(const std::vector<std::uint8_t>& bytes);

// Devolvem false (e deixam `saida` vazio) se o conteúdo não for um JPEG/PNG íntegro.
bool limparJpeg(const std::vector<std::uint8_t>& entrada, int orientacao, std::vector<std::uint8_t>& saida);
bool limparPng(const std::vector<std::uint8_t>& entrada, int orientacao, std::vector<std::uint8_t>& saida);

} // namespace matriz::consolidacao::limpeza
