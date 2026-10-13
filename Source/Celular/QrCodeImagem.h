#pragma once

#include <JuceHeader.h>

#include <string>
#include <vector>

// QR code 100% local (biblioteca qrcodegen da Nayuki, MIT, compilada dentro do app).
namespace matriz::celular {

// Matriz de módulos (true = escuro), sem a borda silenciosa. Vazia se o texto não couber.
std::vector<std::vector<bool>> gerarModulosQr(const std::string& texto);

// Imagem preto-sobre-branco com a borda silenciosa de 4 módulos que os leitores exigem.
juce::Image criarImagemQr(const std::string& texto, int pixelsPorModulo);

} // namespace matriz::celular
