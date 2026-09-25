#pragma once

#include <JuceHeader.h>
#include "ImagemBuffer.h"

namespace matriz::imagem {

enum class ModoEnquadramento {
    Preencher, // Corta sobras mantendo proporção travada
    Encaixar   // Foto inteira com margem branca mantendo proporção
};

enum class FormatoSaida {
    Jpeg,
    Png
};

struct ResultadoLeitura {
    bool sucesso = false;
    juce::String erro;
    ImagemBuffer buffer;
    int orientacaoExif = 1;
    double dpiX = 72.0;
    double dpiY = 72.0;
    juce::MemoryBlock perfilIcc;
};

// 1. Leitura de imagem (aceita somente JPEG e PNG válidos)
ResultadoLeitura lerImagem(const juce::File& arquivo);

// 2. Parser próprio do APP1/EXIF para orientação (valores 1 a 8)
int orientacaoExif(const uint8_t* dados, size_t tamanho);

// 3. Aplicação pura de orientação em pixels (os 8 casos padrão EXIF)
ImagemBuffer aplicarOrientacao(const ImagemBuffer& src, int orientacao);

// 4. Redimensionamento de alta qualidade por média de área para redução limpa
ImagemBuffer redimensionar(const ImagemBuffer& src, int novoW, int novoH);

// 5. Enquadramento no papel (Preencher ou Encaixar com proporção travada)
ImagemBuffer enquadrar(const ImagemBuffer& src, int papelW, int papelH,
                       ModoEnquadramento modo, float offsetX = 0.0f, float offsetY = 0.0f);

// 6. Ajustes via LUT por canal (brilho, contraste, saturação, levels, gamma)
void aplicarAjustes(ImagemBuffer& img,
                    float brilho = 0.0f,
                    float contraste = 0.0f,
                    float saturacao = 1.0f,
                    int levelMin = 0,
                    int levelMax = 255,
                    float gamma = 1.0f,
                    float temperatura = 0.0f);

// 7. Nitidez leve (unsharp mask)
void nitidez(ImagemBuffer& img, float forca = 0.5f);

// 8. Gravação com injeção de DPI e sRGB em nível de bytes
bool gravar(const ImagemBuffer& img,
            const juce::File& destino,
            FormatoSaida formato = FormatoSaida::Jpeg,
            int qualidade = 95,
            double dpi = 300.0);

// Utilitário para conferência e testes: lê DPI diretamente dos bytes gravados
bool lerDpiDosBytes(const juce::File& arquivo, double& outDpiX, double& outDpiY);

} // namespace matriz::imagem
