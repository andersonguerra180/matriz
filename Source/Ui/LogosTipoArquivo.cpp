#include "LogosTipoArquivo.h"

#include <AssetsBinaryData.h>

#include <map>
#include <utility>

namespace matriz::ui {

namespace {

// nome original do arquivo -> (dados, tamanho), montado uma vez a partir da lista que o JUCE gera.
const std::map<juce::String, std::pair<const char*, int>>& indiceDeLogos() {
    static const auto indice = [] {
        std::map<juce::String, std::pair<const char*, int>> m;
        for (int i = 0; i < AssetsBinaryData::namedResourceListSize; ++i) {
            const char* nome = AssetsBinaryData::namedResourceList[i];
            int tamanho = 0;
            const char* dados = AssetsBinaryData::getNamedResource(nome, tamanho);
            if (const char* original = AssetsBinaryData::getNamedResourceOriginalFilename(nome))
                if (dados != nullptr) m.emplace(juce::String::fromUTF8(original), std::make_pair(dados, tamanho));
        }
        return m;
    }();
    return indice;
}

} // namespace

juce::Image carregarLogoDeTipo(const juce::String& nomeArquivo) {
    const auto& indice = indiceDeLogos();
    auto it = indice.find(nomeArquivo);
    if (it == indice.end()) return {};
    return juce::ImageFileFormat::loadFrom(it->second.first, static_cast<size_t>(it->second.second));
}

juce::Image logoConversaWhatsapp(int lado) {
    auto logo = carregarLogoDeTipo("whatsapp.png");
    if (!logo.isValid()) logo = carregarLogoDeTipo("whatsapp.jpg");
    if (logo.isValid()) return logo;
    // ícone genérico: balão de conversa
    juce::Image img(juce::Image::ARGB, lado, lado, true);
    juce::Graphics g(img);
    const float l = static_cast<float>(lado);
    g.setColour(juce::Colour(0xff94a3b8));
    juce::Path balao;
    balao.addRoundedRectangle(l * 0.14f, l * 0.18f, l * 0.72f, l * 0.50f, l * 0.12f);
    balao.startNewSubPath(l * 0.30f, l * 0.66f);
    balao.lineTo(l * 0.26f, l * 0.84f);
    balao.lineTo(l * 0.48f, l * 0.68f);
    balao.closeSubPath();
    g.fillPath(balao);
    g.setColour(juce::Colours::white);
    for (int i = 0; i < 3; ++i) g.fillEllipse(l * (0.34f + 0.16f * static_cast<float>(i)), l * 0.39f, l * 0.08f, l * 0.08f);
    return img;
}

juce::Image miniaturaConversaWhatsapp(int largura) {
    const int altura = largura * 9 / 16;
    juce::Image img(juce::Image::RGB, largura, altura, true);
    juce::Graphics g(img);
    g.fillAll(juce::Colours::black);
    const auto bolha = logoConversaWhatsapp(256);
    const float lado = static_cast<float>(altura) * 0.42f;
    g.setOpacity(1.0f);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(bolha, juce::Rectangle<float>(0, 0, lado, lado).withCentre({ largura * 0.5f, altura * 0.5f }), juce::RectanglePlacement::centred);
    return img;
}

} // namespace matriz::ui
