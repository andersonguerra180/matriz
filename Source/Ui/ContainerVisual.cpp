#include "ContainerVisual.h"

#include <AssetsBinaryData.h>

#include <map>

#include "../I18n/Strings.h"
#include "ImportarMenu.h"
#include "JanelaPlayerDdp.h"
#include "ProjetoAberto.h"

namespace matriz::ui {

const juce::Image& logoDaOrigem(const juce::String& origem) {
    static const juce::Image galeria = juce::ImageFileFormat::loadFrom(AssetsBinaryData::gallery_png, AssetsBinaryData::gallery_pngSize);
    static const juce::Image drive = juce::ImageFileFormat::loadFrom(AssetsBinaryData::googledrive_png, AssetsBinaryData::googledrive_pngSize);
    static const juce::Image bridge = juce::ImageFileFormat::loadFrom(AssetsBinaryData::bridgelogo_png, AssetsBinaryData::bridgelogo_pngSize);
    static const juce::Image lightroom = juce::ImageFileFormat::loadFrom(AssetsBinaryData::lrlogo_png, AssetsBinaryData::lrlogo_pngSize);
    static const juce::Image whatsapp = juce::ImageFileFormat::loadFrom(AssetsBinaryData::whatsapp_png, AssetsBinaryData::whatsapp_pngSize);
    static const juce::Image ddp = tirarFundoBrancoDasBordas(juce::ImageFileFormat::loadFrom(AssetsBinaryData::DDP_png, AssetsBinaryData::DDP_pngSize));
    static const juce::Image producao = juce::ImageFileFormat::loadFrom(AssetsBinaryData::ci_aud_music_production_png, AssetsBinaryData::ci_aud_music_production_pngSize);
    static const juce::Image vinil = juce::ImageFileFormat::loadFrom(AssetsBinaryData::ci_aud_vinyl_rip_png, AssetsBinaryData::ci_aud_vinyl_rip_pngSize);
    static const juce::Image fita = juce::ImageFileFormat::loadFrom(AssetsBinaryData::Tape_Cassette_Rip_png, AssetsBinaryData::Tape_Cassette_Rip_pngSize);
    static const juce::Image youtube = juce::ImageFileFormat::loadFrom(AssetsBinaryData::youtube_png, AssetsBinaryData::youtube_pngSize);
    static const juce::Image vazio;
    if (origem == "youtube") return youtube;
    if (origem == "vinyl") return vinil;
    if (origem == "tape") return fita;
    if (origem == "ddp") return ddp;
    if (origem == "production") return producao;
    if (origem == "gallery") return galeria;
    if (origem == "drive") return drive;
    if (origem == "bridge") return bridge;
    if (origem == "lightroom") return lightroom;
    if (origem == "whatsapp") return whatsapp;
    return vazio;
}

bool ehContainerGrafico(const juce::String& origem) {
    return origem == "ddp" || origem == "production" || origem == "vinyl" || origem == "tape" || origem == "youtube";
}

const juce::Image& iconeDoContainer(const juce::String& origem, const juce::String& nomeDoRecurso) {
    if (nomeDoRecurso.isNotEmpty()) {
        static std::map<std::string, juce::Image> cache;
        const std::string chave = nomeDoRecurso.toStdString();
        auto it = cache.find(chave);
        if (it == cache.end()) {
            int tamanho = 0;
            const char* dados = AssetsBinaryData::getNamedResource(chave.c_str(), tamanho);
            it = cache.emplace(chave, dados != nullptr ? juce::ImageFileFormat::loadFrom(dados, static_cast<size_t>(tamanho)) : juce::Image()).first;
        }
        if (it->second.isValid()) return it->second;
    }
    return logoDaOrigem(origem);
}

const juce::Image& imagemDaCapaDoContainer(const juce::String& caminho) {
    static std::map<std::string, juce::Image> cache;
    static const juce::Image vazio;
    if (caminho.isEmpty()) return vazio;
    const std::string chave = caminho.toStdString();
    auto it = cache.find(chave);
    if (it == cache.end()) {
        const juce::File arquivo(caminho);
        it = cache.emplace(chave, arquivo.existsAsFile() ? juce::ImageFileFormat::loadFrom(arquivo) : juce::Image()).first;
    }
    return it->second;
}

void desenharContainerGrafico(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& origem,
                              const juce::String& nomeDoRecurso, const juce::String& caminhoDaCapa) {
    juce::Graphics::ScopedSaveState salvo(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    const auto& capa = imagemDaCapaDoContainer(caminhoDaCapa);
    const auto& icone = capa.isValid() ? capa : iconeDoContainer(origem, nomeDoRecurso);
    if (icone.isValid()) g.drawImage(icone, area.reduced(10.0f), juce::RectanglePlacement::centred);
}

void abrirContainerDdp(ProjetoAberto& projeto, const std::string& nestId) {
    std::vector<juce::File> origens;
    for (const auto& id : projeto.membrosDoNest(nestId))
        if (auto c = projeto.caminhoDeOrigem(id)) origens.emplace_back(*c);
    juce::String erro;
    const auto pastaDdp = JanelaPlayerDdp::acharPastaDdp(origens);
    if (pastaDdp == juce::File() || !JanelaPlayerDdp::abrir(pastaDdp, erro))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, i18n::t("ddp.title"),
                                               erro.isNotEmpty() ? erro : i18n::t("ddp.err.notFound"));
}

} // namespace matriz::ui
