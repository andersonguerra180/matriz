#include "TiposContainer.h"

#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Ingest/SobreMaterial.h"
#include "EventBus.h"
#include "ImportarMenu.h"
#include "JanelaPlayerDdp.h"
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

juce::String nomeDaPastaEmComum(ProjetoAberto& projeto, const std::vector<std::string>& itemIds) {
    const auto arquivos = arquivosPrincipais(projeto, itemIds);
    if (arquivos.empty()) return {};
    juce::File comum = arquivos.front().getParentDirectory();
    for (const auto& f : arquivos) {
        while (comum != juce::File() && !f.isAChildOf(comum)) comum = comum.getParentDirectory();
        if (comum == juce::File()) return {};
    }
    // Raiz de volume nao e nome de projeto.
    if (comum.getParentDirectory() == comum || comum.getFileName().isEmpty() || comum.getParentDirectory().getFileName().isEmpty()) return {};
    return comum.getFileName();
}

juce::String nomePadraoDoContainer(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, const juce::String& rotuloDoTipo) {
    const auto pasta = nomeDaPastaEmComum(projeto, itemIds);
    if (pasta.isNotEmpty()) return pasta;
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


namespace {

constexpr int kBotaoCancelar = 0;
constexpr int kBotaoConfirmar = 1;

// Ordem das categorias no dialogo: a dominante na selecao primeiro, as outras logo abaixo.
std::vector<CategoriaContainer> ordemDasCategorias(CategoriaContainer dominante) {
    std::vector<CategoriaContainer> ordem{ dominante };
    for (auto c : { CategoriaContainer::Video, CategoriaContainer::Audio, CategoriaContainer::Image, CategoriaContainer::Docs, CategoriaContainer::Outro })
        if (c != dominante) ordem.push_back(c);
    return ordem;
}

// Dialogo de tipo no mesmo visual do SelecionarTipoMidia: PainelOverlay com cabecalhos por categoria. `nomeInicial` so aparece
// ao agrupar (trocar o tipo nao renomeia). aoConcluir(nullptr, "") = cancelou.
void mostrarEscolherTipo(PainelOverlay& overlay, CategoriaContainer dominante, int quantidade, const juce::String& nomeInicial,
                         const TipoContainer* atual, bool trocar, std::function<void(const TipoContainer*, juce::String)> aoConcluir) {
    PainelOverlay::Config cfg;
    cfg.titulo = matriz::i18n::t(trocar ? "tipoc.dialogo.titulo_trocar" : "tipoc.dialogo.titulo_agrupar");
    cfg.mensagem = trocar ? matriz::i18n::t("tipoc.dialogo.msg_trocar") : matriz::i18n::t("tipoc.dialogo.msg").replace("{n}", juce::String(quantidade));
    cfg.larguraCartao = 620;
    std::vector<const TipoContainer*> ordenados;
    for (auto cat : ordemDasCategorias(dominante)) {
        bool primeiro = true;
        for (const auto& t : tiposDeContainer()) {
            if (t.categoria != cat) continue;
            if (primeiro) { cfg.cabecalhos.push_back({ static_cast<int>(ordenados.size()), rotuloDaCategoriaContainer(cat) }); primeiro = false; }
            cfg.opcoes.push_back((t.temPlayer() ? matriz::i18n::t("tipoc.player_marca") : juce::String()) + rotuloDoTipoContainer(t));
            ordenados.push_back(&t);
        }
    }
    cfg.opcaoSelecionada = 0;
    for (size_t i = 0; i < ordenados.size(); ++i) if (ordenados[i] == atual) cfg.opcaoSelecionada = static_cast<int>(i);
    if (!trocar) {
        cfg.comCampoTexto = true;
        cfg.textoInicial = nomeInicial;
        cfg.dicaCampoTexto = matriz::i18n::t("tipoc.dialogo.nome");
    }
    cfg.botoes = {
        { matriz::i18n::t("comum.cancelar"), kBotaoCancelar, false, true },
        { matriz::i18n::t(trocar ? "tipoc.dialogo.confirmar_trocar" : "tipoc.dialogo.confirmar"), kBotaoConfirmar, true, false },
    };
    overlay.mostrar(cfg, [ordenados, aoConcluir](PainelOverlay::Resultado r) {
        if (!aoConcluir) return;
        if (r.botaoId != kBotaoConfirmar || r.opcaoSelecionada < 0 || r.opcaoSelecionada >= static_cast<int>(ordenados.size())) { aoConcluir(nullptr, {}); return; }
        aoConcluir(ordenados[static_cast<size_t>(r.opcaoSelecionada)], r.texto.trim());
    });
}

// O que os membros oferecem aos tipos com player: o video principal (o maior), uma imagem (vira a thumbnail/capa), um roteiro em
// texto simples e se ha uma pasta DDP.
struct AnaliseMembros {
    std::string videoId;
    juce::File imagem, roteiro;
    bool temDdp = false;
};

AnaliseMembros analisarMembros(ProjetoAberto& projeto, const std::vector<std::string>& ids) {
    AnaliseMembros a;
    std::vector<juce::File> arquivos;
    juce::int64 maior = -1;
    for (const auto& id : ids) {
        const auto info = projeto.arquivoPrincipal(id);
        if (!info) continue;
        const juce::File f(info->caminhoAbsoluto);
        arquivos.push_back(f);
        switch (categoriaDoArquivo(f)) {
            case CategoriaConteudo::Video:
                if (f.getSize() > maior) { maior = f.getSize(); a.videoId = id; }
                break;
            case CategoriaConteudo::Image:
                if (a.imagem == juce::File() && f.existsAsFile()) a.imagem = f;
                break;
            default:
                if (a.roteiro == juce::File() && f.existsAsFile() && (f.hasFileExtension("txt") || f.hasFileExtension("md"))) a.roteiro = f;
                break;
        }
    }
    a.temDdp = JanelaPlayerDdp::acharPastaDdp(arquivos) != juce::File();
    return a;
}

// O que o tipo exige e a selecao nao tem: a mesma mensagem que o IMPORT mostra (YouTube sem video, DDP sem a pasta DDP).
juce::String avisoDoTipo(const TipoContainer& t, const AnaliseMembros& a) {
    const std::string origem = t.origemPlayer;
    if (origem == "youtube" && a.videoId.empty()) return matriz::i18n::t("imp.ficha.youtube.sem_video");
    if (origem == "ddp" && !a.temDdp) return matriz::i18n::t("ddp.err.notFound");
    return {};
}

void aoTerminarFluxo(const HospedeOverlay& h, bool mudou) {
    if (h.fechar) h.fechar();
    if (mudou && h.aoMudarDados) h.aoMudarDados();
}

// Grava o container (e o CONTENT do tipo) fora da message thread; a tela recarrega ao fim.
void criarEGravar(ProjetoAberto& projeto, std::vector<std::string> ids, const TipoContainer& tipo, juce::String nome, HospedeOverlay h) {
    ProjetoAberto* proj = &projeto;
    const auto vivo = projeto.tokenVida();
    const std::string tipoId = tipo.id;
    juce::Thread::launch([proj, vivo, ids, tipoId, nome, h] {
        if (vivo.expired()) return;
        const TipoContainer* t = tipoDeContainer(tipoId);
        if (t == nullptr) return;
        const juce::String nomeFinal = nome.isNotEmpty() ? nome : nomePadraoDoContainer(*proj, ids, rotuloDoTipoContainer(*t));
        const std::string nestId = proj->criarContainerDeTipo(ids, t->id, origemDoTipoContainer(*t), nomeFinal.toStdString());
        if (!nestId.empty()) aplicarContentDoTipo(*proj, ids, *t);
        juce::MessageManager::callAsync([vivo, h, nestId] {
            if (vivo.expired()) return;
            if (!nestId.empty()) EventBus::obterInstancia().dispararItemAlterado("", "nest");
            aoTerminarFluxo(h, !nestId.empty());
        });
    });
}

// Tipo com player: grava o container EXATAMENTE como o IMPORT (CONTENT da ficha, tags, ícone, capa, nome, questionário guardado).
// `nestExistente` vazio = cria o container; senão troca o tipo do que já existe.
void gravarComPlayer(ProjetoAberto& projeto, std::vector<std::string> ids, std::string nestExistente, const TipoContainer& tipo, juce::String nomeDigitado,
                     std::shared_ptr<const matriz::ingest::SobreMaterial> contexto, AnaliseMembros analise, HospedeOverlay h) {
    ProjetoAberto* proj = &projeto;
    const auto vivo = projeto.tokenVida();
    const std::string tipoId = tipo.id;
    juce::Thread::launch([proj, vivo, ids, nestExistente, tipoId, nomeDigitado, contexto, analise, h] {
        if (vivo.expired()) return;
        const TipoContainer* t = tipoDeContainer(tipoId);
        if (t == nullptr) return;
        const matriz::ingest::SobreMaterial ctx = contexto != nullptr ? *contexto : matriz::ingest::SobreMaterial{};
        std::string nestId;
        if (nestExistente.empty()) {
            const juce::String nome = ctx.nomeContainer.empty() ? (nomeDigitado.isNotEmpty() ? nomeDigitado : nomePadraoDoContainer(*proj, ids, rotuloDoTipoContainer(*t)))
                                                               : juce::String::fromUTF8(ctx.nomeContainer.c_str());
            nestId = proj->criarContainerDeTipo(ids, t->id, origemDoTipoContainer(*t), nome.toStdString());
        } else if (proj->trocarTipoDoContainer(nestExistente, t->id, origemDoTipoContainer(*t))) {
            nestId = nestExistente;
            if (!ctx.nomeContainer.empty()) proj->definirNomeDoContainer(nestId, ctx.nomeContainer);
        }
        if (!nestId.empty()) {
            try {
                matriz::ingest::SobreMaterial aplicar = ctx;
                // YouTube: um roteiro em texto simples preenche a descrição do vídeo quando ela não foi digitada (igual ao IMPORT).
                if (std::string(t->origemPlayer) == "youtube" && analise.roteiro != juce::File()) {
                    bool temDescricao = false;
                    for (const auto& c : aplicar.camposDoVideo) if (c.first == "dc_description" && !c.second.empty()) temDescricao = true;
                    const juce::String texto = analise.roteiro.loadFileAsString().trim().substring(0, 4000);
                    if (!temDescricao && texto.isNotEmpty()) aplicar.camposDoVideo.push_back({ "dc_description", texto.toStdString() });
                }
                proj->aplicarContextoDeImportacao(ids, aplicar, analise.videoId);
                aplicarContentDoTipo(*proj, ids, *t, ctx.collectionType);
                if (!ctx.iconeContainer.empty()) proj->definirIconeDoContainer(nestId, ctx.iconeContainer);
                if (!ctx.folhaId.empty()) proj->definirQuestionarioDoContainer(nestId, questionarioParaJson(ctx));  // para o Edit Project Info
                std::string capa = ctx.capaContainer;
                const bool automatica = nestExistente.empty() && std::string(t->origemPlayer) == "youtube";  // trocar o tipo não troca a capa que já existe
                if (capa.empty() && automatica && analise.imagem != juce::File())
                    capa = analise.imagem.getFullPathName().toStdString();  // a thumbnail da seleção é a capa
                if (!capa.empty()) proj->definirCapaDoContainer(nestId, juce::File(juce::String::fromUTF8(capa.c_str())));
                else if (automatica) proj->definirCapaDoContainerComFrameDeVideo(nestId, true);
            } catch (...) {}
        }
        const bool ok = !nestId.empty();
        juce::MessageManager::callAsync([vivo, h, ok] {
            if (vivo.expired()) return;
            if (ok) EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");
            aoTerminarFluxo(h, ok);
        });
    });
}

// Tipo com player: confere o que a seleção tem, mostra a ficha do card do IMPORT (sem a etapa de escolher arquivos) e grava.
void fluxoComPlayer(ProjetoAberto& projeto, std::vector<std::string> ids, std::string nestExistente, const TipoContainer& tipo, juce::String nome,
                    juce::Component::SafePointer<PainelOverlay> overlay, HospedeOverlay h) {
    ProjetoAberto* proj = &projeto;
    const auto vivo = projeto.tokenVida();
    const std::string tipoId = tipo.id;
    juce::Thread::launch([proj, vivo, ids, nestExistente, tipoId, nome, overlay, h] {
        if (vivo.expired()) return;
        const auto analise = analisarMembros(*proj, ids);
        juce::MessageManager::callAsync([proj, vivo, ids, nestExistente, tipoId, nome, overlay, h, analise] {
            if (vivo.expired()) return;
            const TipoContainer* t = tipoDeContainer(tipoId);
            if (t == nullptr || overlay == nullptr) { aoTerminarFluxo(h, false); return; }
            if (const auto aviso = avisoDoTipo(*t, analise); aviso.isNotEmpty()) {
                aoTerminarFluxo(h, false);
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, rotuloDoTipoContainer(*t), aviso);
                return;
            }
            mostrarFichaImportar(*overlay, t->id, [proj, vivo, ids, nestExistente, tipoId, nome, h, analise](bool continuar, std::shared_ptr<const matriz::ingest::SobreMaterial> daFicha) {
                juce::MessageManager::callAsync([proj, vivo, ids, nestExistente, tipoId, nome, h, analise, continuar, daFicha] {
                    if (vivo.expired()) return;
                    const TipoContainer* t2 = tipoDeContainer(tipoId);
                    if (!continuar || t2 == nullptr) { aoTerminarFluxo(h, false); return; }
                    if (h.fechar) h.fechar();
                    const auto contexto = mesclarContextosImportar(contextoDaFolhaImportar(t2->id), daFicha);
                    gravarComPlayer(*proj, ids, nestExistente, *t2, nome, contexto, analise, h);
                });
            });
        });
    });
}

}  // namespace

void agruparComoProjectContainer(ProjetoAberto& projeto, std::vector<std::string> itemIds, HospedeOverlay h) {
    if (itemIds.size() < 2 || projeto.somenteLeitura() || !h.abrir) return;
    for (const auto& id : itemIds)
        if (projeto.nestDoItem(id).has_value()) return;  // o menu ja avisa; defesa
    ProjetoAberto* proj = &projeto;
    const auto vivo = projeto.tokenVida();
    juce::Thread::launch([proj, vivo, itemIds, h] {
        if (vivo.expired()) return;
        const auto dominante = categoriaDominanteDosItens(*proj, itemIds);
        const auto pasta = nomeDaPastaEmComum(*proj, itemIds);
        juce::MessageManager::callAsync([proj, vivo, itemIds, h, dominante, pasta] {
            if (vivo.expired()) return;
            PainelOverlay* overlay = h.abrir();
            if (overlay == nullptr) return;
            const juce::Component::SafePointer<PainelOverlay> ov(overlay);
            mostrarEscolherTipo(*overlay, dominante, static_cast<int>(itemIds.size()), pasta, nullptr, false,
                [proj, vivo, itemIds, h, ov](const TipoContainer* tipo, juce::String nome) {
                    juce::MessageManager::callAsync([proj, vivo, itemIds, h, tipo, nome, ov] {
                        if (vivo.expired()) return;
                        if (tipo == nullptr) { aoTerminarFluxo(h, false); return; }
                        if (tipo->temPlayer()) { fluxoComPlayer(*proj, itemIds, {}, *tipo, nome, ov, h); return; }  // a ficha reusa o mesmo overlay
                        if (h.fechar) h.fechar();
                        criarEGravar(*proj, itemIds, *tipo, nome, h);
                    });
                });
        });
    });
}

void mudarTipoDoContainer(ProjetoAberto& projeto, const std::string& nestId, HospedeOverlay h) {
    if (nestId.empty() || projeto.somenteLeitura() || !h.abrir) return;
    PainelOverlay* overlay = h.abrir();
    if (overlay == nullptr) return;
    const auto membros = projeto.membrosDoNest(nestId);
    const TipoContainer* atual = tipoDeContainerDaOrigem(projeto.tipoDoContainer(nestId), projeto.origemDoContainer(nestId));
    ProjetoAberto* proj = &projeto;
    const auto vivo = projeto.tokenVida();
    const juce::Component::SafePointer<PainelOverlay> ov(overlay);
    mostrarEscolherTipo(*overlay, atual != nullptr ? atual->categoria : CategoriaContainer::Outro, static_cast<int>(membros.size()), {}, atual, true,
        [proj, vivo, nestId, membros, h, ov](const TipoContainer* tipo, juce::String) {
            juce::MessageManager::callAsync([proj, vivo, nestId, membros, h, tipo, ov] {
                if (vivo.expired()) return;
                if (tipo == nullptr) { aoTerminarFluxo(h, false); return; }
                if (tipo->temPlayer()) { fluxoComPlayer(*proj, membros, nestId, *tipo, {}, ov, h); return; }
                if (h.fechar) h.fechar();
                const std::string tipoId = tipo->id;
                juce::Thread::launch([proj, vivo, nestId, membros, h, tipoId] {
                    if (vivo.expired()) return;
                    const TipoContainer* t = tipoDeContainer(tipoId);
                    bool ok = false;
                    if (t != nullptr && proj->trocarTipoDoContainer(nestId, t->id, origemDoTipoContainer(*t))) {
                        aplicarContentDoTipo(*proj, membros, *t);  // nunca apaga nem sobrescreve o que ja esta preenchido
                        ok = true;
                    }
                    juce::MessageManager::callAsync([vivo, h, ok] {
                        if (vivo.expired()) return;
                        if (ok) EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");
                        aoTerminarFluxo(h, ok);
                    });
                });
            });
        });
}

void desagruparContainer(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, HospedeOverlay h) {
    std::vector<std::string> deContainer;
    for (const auto& id : itemIds)
        if (const auto n = projeto.nestDoItem(id); n && !n->origem.empty()) deContainer.push_back(id);
    if (deContainer.empty()) return;
    projeto.desfazerNest(deContainer);
    if (h.aoMudarDados) h.aoMudarDados();
}

void adicionarMenuProjectContainer(juce::PopupMenu& menu, ProjetoAberto& projeto, const std::vector<std::string>& itemIds, HospedeOverlay h) {
    bool algumEmGrupo = false, algumContainer = false, todosNoMesmoContainer = true;
    std::string nestDoContainer;
    for (const auto& id : itemIds) {
        const auto n = projeto.nestDoItem(id);
        if (!n) { todosNoMesmoContainer = false; continue; }
        algumEmGrupo = true;
        if (n->origem.empty()) { todosNoMesmoContainer = false; continue; }
        if (!algumContainer) { algumContainer = true; nestDoContainer = n->nestId; }
        else if (nestDoContainer != n->nestId) todosNoMesmoContainer = false;
    }
    ProjetoAberto* proj = &projeto;
    if (itemIds.size() > 1) {
        menu.addItem(matriz::i18n::t("tipoc.menu.agrupar"), !algumEmGrupo, false, [proj, itemIds, h] { agruparComoProjectContainer(*proj, itemIds, h); });
        if (algumEmGrupo) menu.addItem(matriz::i18n::t("tipoc.aviso.ja_agrupado"), false, false, nullptr);
    }
    if (algumContainer) {
        menu.addItem(matriz::i18n::t("tipoc.menu.trocar"), todosNoMesmoContainer, false, [proj, nestDoContainer, h] { mudarTipoDoContainer(*proj, nestDoContainer, h); });
        menu.addItem(matriz::i18n::t("tipoc.menu.desagrupar"), true, false, [proj, itemIds, h] { desagruparContainer(*proj, itemIds, h); });
    }
}

} // namespace matriz::ui
