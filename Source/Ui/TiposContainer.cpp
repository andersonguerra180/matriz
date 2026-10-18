#include "TiposContainer.h"

#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"
#include "ProjetoAberto.h"

#include <map>

namespace matriz::ui {

const std::vector<TipoContainer>& tiposDeContainer() {
    static const std::vector<TipoContainer> tabela = {
    { "video.video", CategoriaContainer::Video, "tipoc.video.video", "", "", CategoriaConteudo::Nenhuma, "ci_vid_native_digital_png" },
    { "videos.youtube", CategoriaContainer::Video, "tipoc.videos.youtube", "youtube", "", CategoriaConteudo::Nenhuma, "ci_vid_native_digital_png" },
    { "video.live_performance", CategoriaContainer::Video, "tipoc.video.live_performance", "", "Live Performance", CategoriaConteudo::Video, "ci_vid_native_digital_png" },
    { "video.tutorial", CategoriaContainer::Video, "tipoc.video.tutorial", "", "Tutorial", CategoriaConteudo::Video, "ci_vid_native_digital_png" },
    { "video.film_project", CategoriaContainer::Video, "tipoc.video.film_project", "", "Film", CategoriaConteudo::Video, "ci_vid_film_16_mm_8_mm_png" },
    { "video.raw_footage", CategoriaContainer::Video, "tipoc.video.raw_footage", "", "Raw Footage", CategoriaConteudo::Video, "ci_vid_native_digital_png" },
    { "audio.production.project", CategoriaContainer::Audio, "tipoc.audio.production.project", "production", "", CategoriaConteudo::Nenhuma, "ci_aud_music_production_png" },
    { "audio.preservation.ddp", CategoriaContainer::Audio, "tipoc.audio.preservation.ddp", "ddp", "", CategoriaConteudo::Nenhuma, "ci_aud_ddp_master_png" },
    { "audio.preservation.vinyl", CategoriaContainer::Audio, "tipoc.audio.preservation.vinyl", "vinyl", "", CategoriaConteudo::Nenhuma, "ci_aud_vinyl_rip_png" },
    { "audio.preservation.tape", CategoriaContainer::Audio, "tipoc.audio.preservation.tape", "tape", "", CategoriaConteudo::Nenhuma, "ci_aud_music_png" },
    { "audio.album", CategoriaContainer::Audio, "tipoc.audio.album", "", "Album", CategoriaConteudo::Audio, "ci_aud_music_png" },
    { "audio.ep", CategoriaContainer::Audio, "tipoc.audio.ep", "", "EP", CategoriaConteudo::Audio, "ci_aud_music_png" },
    { "audio.demo", CategoriaContainer::Audio, "tipoc.audio.demo", "", "Demo", CategoriaConteudo::Audio, "ci_aud_music_png" },
    { "audio.soundtrack", CategoriaContainer::Audio, "tipoc.audio.soundtrack", "", "Soundtrack", CategoriaConteudo::Audio, "ci_aud_soundtrack_png" },
    { "audio.stems", CategoriaContainer::Audio, "tipoc.audio.stems", "", "Stems", CategoriaConteudo::Audio, "ci_aud_music_production_png" },
    { "audio.multitracks", CategoriaContainer::Audio, "tipoc.audio.multitracks", "", "Multitracks", CategoriaConteudo::Audio, "ci_aud_music_production_png" },
    { "audio.sample_pack", CategoriaContainer::Audio, "tipoc.audio.sample_pack", "", "Sample Pack", CategoriaConteudo::Audio, "ci_aud_sample_pack_png" },
    { "audio.field_recording", CategoriaContainer::Audio, "tipoc.audio.field_recording", "", "Field Recording", CategoriaConteudo::Audio, "ci_aud_field_recording_png" },
    { "audio.sound_fx", CategoriaContainer::Audio, "tipoc.audio.sound_fx", "", "Sound FX", CategoriaConteudo::Audio, "ci_aud_music_png" },
    { "audio.raw", CategoriaContainer::Audio, "tipoc.audio.raw", "", "Raw Audio", CategoriaConteudo::Audio, "ci_aud_music_png" },
    { "audio.impulse_response", CategoriaContainer::Audio, "tipoc.audio.impulse_response", "", "Impulse Response", CategoriaConteudo::Audio, "ci_aud_music_png" },
    { "image.photo_collection", CategoriaContainer::Image, "tipoc.image.photo_collection", "", "Photo", CategoriaConteudo::Image, "ci_foto_computer_folder_png" },
    { "image.exhibition", CategoriaContainer::Image, "tipoc.image.exhibition", "", "Exhibition", CategoriaConteudo::Image, "ci_foto_computer_folder_png" },
    { "image.raw_photos", CategoriaContainer::Image, "tipoc.image.raw_photos", "", "Raw Photos", CategoriaConteudo::Image, "ci_foto_computer_folder_png" },
    { "image.photo_project", CategoriaContainer::Image, "tipoc.image.photo_project", "", "Photo", CategoriaConteudo::Image, "ci_foto_computer_folder_png" },
    { "image.technical_photography", CategoriaContainer::Image, "tipoc.image.technical_photography", "", "Technical Photography", CategoriaConteudo::Image, "ci_foto_computer_folder_png" },
    { "doc.personal", CategoriaContainer::Docs, "tipoc.doc.personal", "", "Personal Document", CategoriaConteudo::Docs, "ci_doc_digital_native_png" },
    { "doc.contract", CategoriaContainer::Docs, "tipoc.doc.contract", "", "Contract", CategoriaConteudo::Docs, "ci_doc_digital_native_png" },
    { "doc.technical", CategoriaContainer::Docs, "tipoc.doc.technical", "", "Technical Documentation", CategoriaConteudo::Docs, "ci_doc_digital_native_png" },
    { "doc.project_documentation", CategoriaContainer::Docs, "tipoc.doc.project_documentation", "", "Documentation", CategoriaConteudo::Docs, "ci_doc_digital_native_png" },
    { "other.branding", CategoriaContainer::Outro, "tipoc.other.branding", "", "", CategoriaConteudo::Nenhuma, "ci_doc_other_png" },
    { "other.marketing", CategoriaContainer::Outro, "tipoc.other.marketing", "", "", CategoriaConteudo::Nenhuma, "ci_doc_other_png" },
    };
    return tabela;
}

const TipoContainer* tipoDeContainer(const std::string& id) {
    for (const auto& t : tiposDeContainer())
        if (id == t.id) return &t;
    return nullptr;
}

const TipoContainer* tipoDeContainerDaOrigem(const std::string& tipoGuardado, const std::string& origem) {
    if (!tipoGuardado.empty()) return tipoDeContainer(tipoGuardado);
    for (const auto& t : tiposDeContainer())
        if (t.temPlayer() && origem == t.origemPlayer) return &t;
    return nullptr;
}

std::string origemDoTipoContainer(const TipoContainer& tipo) {
    return tipo.temPlayer() ? std::string(tipo.origemPlayer) : std::string(kOrigemContainerGenerico);
}

juce::String rotuloDoTipoContainer(const TipoContainer& tipo) { return matriz::i18n::t(tipo.chaveRotulo); }

juce::String rotuloDaCategoriaContainer(CategoriaContainer c) {
    switch (c) {
        case CategoriaContainer::Video: return matriz::i18n::t("tipoc.cat.video");
        case CategoriaContainer::Audio: return matriz::i18n::t("tipoc.cat.audio");
        case CategoriaContainer::Image: return matriz::i18n::t("tipoc.cat.image");
        case CategoriaContainer::Docs:  return matriz::i18n::t("tipoc.cat.docs");
        case CategoriaContainer::Outro: break;
    }
    return matriz::i18n::t("tipoc.cat.outro");
}

namespace {

// Mesma divisao que a ficha usa para escolher a lista de CONTENT (FichaPanelComponent).
CategoriaConteudo categoriaDoArquivo(const juce::File& arquivo) {
    switch (matriz::ingest::categoriaPorExtensao(arquivo)) {
        case matriz::ingest::CategoriaMidia::Audio: return CategoriaConteudo::Audio;
        case matriz::ingest::CategoriaMidia::Video: return CategoriaConteudo::Video;
        case matriz::ingest::CategoriaMidia::Arte:
        case matriz::ingest::CategoriaMidia::Imagem: return CategoriaConteudo::Image;
        default: return CategoriaConteudo::Docs;
    }
}

std::vector<juce::File> arquivosPrincipais(ProjetoAberto& projeto, const std::vector<std::string>& itemIds) {
    std::vector<juce::File> out;
    for (const auto& id : itemIds)
        if (const auto info = projeto.arquivoPrincipal(id)) out.emplace_back(info->caminhoAbsoluto);
    return out;
}

}  // namespace

CategoriaContainer categoriaDominanteDosItens(ProjetoAberto& projeto, const std::vector<std::string>& itemIds) {
    std::map<CategoriaConteudo, int> contagem;
    for (const auto& f : arquivosPrincipais(projeto, itemIds)) ++contagem[categoriaDoArquivo(f)];
    CategoriaConteudo melhor = CategoriaConteudo::Video;
    int maior = 0;
    for (auto c : { CategoriaConteudo::Video, CategoriaConteudo::Audio, CategoriaConteudo::Image, CategoriaConteudo::Docs }) {
        if (contagem[c] > maior) { maior = contagem[c]; melhor = c; }
    }
    if (maior == 0) return CategoriaContainer::Video;
    switch (melhor) {
        case CategoriaConteudo::Audio: return CategoriaContainer::Audio;
        case CategoriaConteudo::Image: return CategoriaContainer::Image;
        case CategoriaConteudo::Docs:  return CategoriaContainer::Docs;
        default: return CategoriaContainer::Video;
    }
}

juce::String nomePadraoDoContainer(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, const juce::String& rotuloDoTipo) {
    const auto arquivos = arquivosPrincipais(projeto, itemIds);
    if (!arquivos.empty()) {
        juce::File comum = arquivos.front().getParentDirectory();
        for (const auto& f : arquivos) {
            while (comum != juce::File() && !f.isAChildOf(comum)) comum = comum.getParentDirectory();
            if (comum == juce::File()) break;
        }
        // Raiz de volume nao e nome de projeto.
        if (comum != juce::File() && comum.getParentDirectory() != comum && comum.getFileName().isNotEmpty() && comum.getParentDirectory().getFileName().isNotEmpty())
            return comum.getFileName();
    }
    return matriz::i18n::t("tipoc.nome_padrao").replace("{tipo}", rotuloDoTipo).replace("{data}", juce::Time::getCurrentTime().formatted("%Y-%m-%d"));
}

int aplicarContentDoTipo(ProjetoAberto& projeto, const std::vector<std::string>& membros, const TipoContainer& tipo, const std::string& contentDaFicha) {
    const std::string valor = contentDaFicha.empty() ? std::string(tipo.content) : contentDaFicha;
    if (valor.empty()) return 0;
    std::vector<std::string> aceitos;
    if (contentDaFicha.empty() && tipo.categoriaConteudo != CategoriaConteudo::Nenhuma) {
        for (const auto& id : membros) {
            const auto info = projeto.arquivoPrincipal(id);
            if (info && categoriaDoArquivo(juce::File(info->caminhoAbsoluto)) == tipo.categoriaConteudo) aceitos.push_back(id);
        }
    } else {
        // CONTENT vindo da ficha do card: vale para os arquivos cuja categoria o card implica (a ficha do IMPORT nao filtra por
        // categoria), entao todos os membros — o IMPORT tambem gravava em todos.
        aceitos = membros;
    }
    return projeto.definirContentSeVazio(aceitos, valor);
}

} // namespace matriz::ui
