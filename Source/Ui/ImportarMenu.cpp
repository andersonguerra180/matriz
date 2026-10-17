#include "ImportarMenu.h"

#include <AssetsBinaryData.h>

#include <map>

#include "../I18n/Strings.h"
#include "GoogleDriveContas.h"
#include "Moldura.h"
#include "OriginalSourceMedium.h"
#include "Tablet.h"
#include "Tokens.h"

namespace matriz::ui {

using matriz::i18n::t;

namespace {

// Um card da janela: folha (conclui com o id) ou grupo (abre o nível seguinte).
struct No {
    std::string id;
    IconeUi icone = IconeUi::Other;
    std::string arquivoIcone;     // nome do recurso embutido (ex.: "ci_aud_midi_png", de Assets/Icons); vazio = ícone vetorial
    juce::String rotuloLiteral;   // texto que não passa por i18n (ex.: e-mail da conta do Drive); vazio = t("imp.node.<id>")
    std::string chaveDica;        // dica do nível que este grupo abre; vazio = "imp.dica.sub"
    bool emBreve = false;         // card já no lugar, ainda sem fluxo: aparece apagado com selo "em breve"
    bool comSub = false;          // mostra a linha de subtítulo ("imp.sub.<id>")
    juce::Colour cor;
    std::vector<No> filhos;
};

No folha(const std::string& id, IconeUi icone, const char* png = "", bool emBreve = false) {
    No n;
    n.id = id;
    n.icone = icone;
    n.arquivoIcone = png;
    n.emBreve = emBreve;
    return n;
}

No grupo(const std::string& id, IconeUi icone, const char* png, std::vector<No> filhos, bool comSub = true) {
    No n;
    n.id = id;
    n.icone = icone;
    n.arquivoIcone = png;
    n.filhos = std::move(filhos);
    n.comSub = comSub;
    return n;
}

void colorir(No& n, juce::Colour cor) {
    n.cor = cor;
    for (auto& f : n.filhos) colorir(f, cor);
}

// Contas do Google Drive para Desktop: com 2 ou mais, o card vira um grupo e a escolha da conta é uma sub-janela
// (ids "drive.0", "drive.1"...). Com 0 ou 1 conta fica folha "drive" (uma conta vai direto; nenhuma avisa).
No montarDrive() {
    const auto contas = contasGoogleDrive();
    No drive;
    drive.id = "drive";
    drive.icone = IconeUi::StorageUnit;
    drive.arquivoIcone = "googledrive_png";
    drive.cor = juce::Colour(0xff1a73e8);
    if (contas.size() < 2) return drive;
    drive.chaveDica = "imp.dica.drive";
    drive.comSub = true;
    for (size_t i = 0; i < contas.size(); ++i) {
        No c;
        c.id = "drive." + std::to_string(i);
        c.icone = IconeUi::StorageUnit;
        c.arquivoIcone = "googledrive_png";
        c.rotuloLiteral = contas[i].copiaAntiga ? contas[i].rotulo + " (" + t("gdrive.copia_antiga") + ")" : contas[i].rotulo;
        c.cor = drive.cor;
        drive.filhos.push_back(std::move(c));
    }
    return drive;
}

// A árvore inteira. Os ids das folhas são o contrato com IntakeWorkspaceComponent::tratarEscolhaImportar.
std::vector<No> montarArvore() {
    const auto& tk = tema();

    std::vector<No> raiz;

    auto files = grupo("files", IconeUi::StorageUnit, "ci_arm_computer_folder_png",
                       { folha("files.computer_folder", IconeUi::Pasta, "ci_arm_computer_folder_png"),
                         folha("files.external_drive", IconeUi::StorageUnit, "ci_arm_external_hdd_png"),
                         folha("files.optical", IconeUi::Disco, "ci_arm_cd_png"),
                         folha("files.other", IconeUi::Other, "ci_arm_other_png") });
    colorir(files, juce::Colour(0xff0284c7));
    raiz.push_back(std::move(files));

    auto drive = montarDrive();
    raiz.push_back(std::move(drive));

    auto phone = folha("phone", IconeUi::PhoneGallery);
    colorir(phone, juce::Colour(0xff3b82f6));
    raiz.push_back(std::move(phone));

    auto photos = grupo("photos", IconeUi::Photography, "ci_foto_camera_memory_card_sd_cf_png",
                        { folha("photos.folder", IconeUi::Pasta, "ci_foto_computer_folder_png"),
                          folha("photos.camera_card", IconeUi::Photography, "ci_foto_camera_memory_card_sd_cf_png"),
                          folha("photos.lightroom", IconeUi::Photography, "ci_foto_adobe_lightroom_catalog_png"),
                          folha("photos.bridge", IconeUi::Photography, "ci_foto_adobe_bridge_png") });
    colorir(photos, tk.tipoImagem);
    raiz.push_back(std::move(photos));

    // VIDEOS: o formato de origem (VHS, MiniDV, filme...) NÃO é um card — é um campo da ficha, porque o mesmo
    // material pode ter vindo de uma fita e estar hoje num arquivo digital ou num link.
    auto videos = grupo("videos", IconeUi::Videos, "ci_vid_native_digital_png",
                        { folha("videos.digital", IconeUi::Videos, "ci_vid_native_digital_png"),
                          folha("videos.link", IconeUi::Link, "", true),
                          folha("videos.dvd_rip", IconeUi::Disco, "ci_vid_dvd_blu_ray_png", true),
                          folha("videos.other", IconeUi::Other, "ci_vid_other_png") });
    colorir(videos, tk.tipoVideo);
    raiz.push_back(std::move(videos));

    auto audio = grupo("audio", IconeUi::Sound, "ci_aud_music_production_png",
        { grupo("audio.production", IconeUi::Sound, "ci_aud_music_production_png",
                { folha("audio.production.project", IconeUi::Sound, "ci_aud_music_production_png"),
                  folha("audio.production.samples", IconeUi::Sound, "ci_aud_sample_pack_png"),
                  folha("audio.production.midi", IconeUi::Sound, "ci_aud_midi_png"),
                  folha("audio.production.assets", IconeUi::Sound) }),
          grupo("audio.preservation", IconeUi::Disco, "ci_aud_vinyl_rip_png",
                { folha("audio.preservation.cd_rip", IconeUi::Disco, "ci_aud_cd_rip_png", true),
                  folha("audio.preservation.ddp", IconeUi::Disco, "ci_aud_ddp_master_png"),
                  folha("audio.preservation.vinyl", IconeUi::Disco, "ci_aud_vinyl_rip_png"),
                  folha("audio.preservation.tape", IconeUi::Cassete) }),
          grupo("audio.general", IconeUi::Sound, "ci_aud_field_recording_png",
                { folha("audio.general.soundtrack", IconeUi::Videos, "ci_aud_soundtrack_png"),
                  folha("audio.general.film_sound", IconeUi::Videos),
                  folha("audio.general.audiobook", IconeUi::Documents, "ci_aud_audiobook_png"),
                  folha("audio.general.field", IconeUi::Sound, "ci_aud_field_recording_png"),
                  folha("audio.general.voice_over", IconeUi::Sound, "ci_aud_voice_over_png"),
                  folha("audio.general.voice_notes", IconeUi::Sound, "ci_aud_voice_memo_png"),
                  folha("audio.general.other", IconeUi::Other) }) });
    colorir(audio, tk.tipoAudio);
    raiz.push_back(std::move(audio));

    auto documents = grupo("documents", IconeUi::Documents, "ci_doc_digital_native_png",
                           { folha("documents.native", IconeUi::Documents, "ci_doc_digital_native_png"),
                             folha("documents.scanned", IconeUi::Documents, "ci_doc_scanned_png"),
                             folha("documents.web", IconeUi::Web) });
    colorir(documents, tk.tipoDocumento);
    raiz.push_back(std::move(documents));

    // OTHER fica por último: Web, Multimedia, Social Media e Custom.
    auto outros = grupo("other", IconeUi::Other, "",
        { folha("other.web", IconeUi::Web, "", true),
          grupo("other.multimedia", IconeUi::Multimedia, "",
                { folha("other.multimedia.cdrom", IconeUi::CdRom, "", true),
                  folha("other.multimedia.apps", IconeUi::Multimedia),
                  folha("other.multimedia.interactive", IconeUi::Multimedia) }),
          grupo("other.social", IconeUi::SocialMedia, "",
                { folha("other.social.instagram", IconeUi::SocialMedia, "instagram_jpeg"),
                  folha("other.social.facebook", IconeUi::SocialMedia, "meta_png"),
                  folha("other.social.tiktok", IconeUi::SocialMedia, "tiktok_jpeg"),
                  folha("other.social.x", IconeUi::SocialMedia, "x_png"),
                  folha("other.social.other", IconeUi::SocialMedia) }),
          folha("other.custom", IconeUi::Other, "ci_doc_other_png") });
    colorir(outros, tk.tipoOutros);
    raiz.push_back(std::move(outros));

    for (auto& n : raiz) n.comSub = true;
    return raiz;
}

void coletarFolhas(const std::vector<No>& nos, std::vector<std::string>& saida) {
    for (const auto& n : nos) {
        if (n.filhos.empty()) saida.push_back(n.id);
        else coletarFolhas(n.filhos, saida);
    }
}

// Ícone embutido (Assets/Icons): vazio se o recurso não existe.
const juce::Image& imagemDoIcone(const std::string& recurso) {
    static std::map<std::string, juce::Image> cache;
    static const juce::Image vazio;
    if (recurso.empty()) return vazio;
    auto it = cache.find(recurso);
    if (it == cache.end()) {
        int tamanho = 0;
        const char* dados = AssetsBinaryData::getNamedResource(recurso.c_str(), tamanho);
        it = cache.emplace(recurso, dados != nullptr ? juce::ImageFileFormat::loadFrom(dados, static_cast<size_t>(tamanho)) : juce::Image()).first;
    }
    return it->second;
}

constexpr int kLarguraCard = 136;
constexpr int kAlturaCard = 156;
constexpr int kEspaco = 14;
constexpr int kAlturaDica = 40;

int colunasPara(int n) {
    if (n <= 4) return juce::jmax(2, n);
    if (n <= 8) return 4;
    return 5;
}

// Grade de cards. O clique conclui o overlay com 100 + posição do card.
class GradeImportar : public juce::Component {
public:
    GradeImportar(std::vector<No> nos, juce::String dica) : nos_(std::move(nos)), dica_(std::move(dica)) {
        setComponentID("imp.grade");
        setWantsKeyboardFocus(false);
        cards_.resize(nos_.size());
    }

    static juce::Point<int> tamanhoIdeal(int n) {
        const int cols = colunasPara(n);
        const int linhas = (n + cols - 1) / cols;
        return { cols * kLarguraCard + (cols - 1) * kEspaco, kAlturaDica + linhas * kAlturaCard + (linhas - 1) * kEspaco };
    }

    void resized() override {
        const int n = static_cast<int>(nos_.size());
        const int cols = colunasPara(n);
        const int linhas = (n + cols - 1) / cols;
        const int larguraGrade = cols * kLarguraCard + (cols - 1) * kEspaco;
        const int x0 = (getWidth() - larguraGrade) / 2;
        for (int i = 0; i < n; ++i) {
            const int c = i % cols, l = i / cols;
            // A última linha, se incompleta, fica centralizada.
            const int naLinha = (l == linhas - 1) ? n - l * cols : cols;
            const int desloc = ((cols - naLinha) * (kLarguraCard + kEspaco)) / 2;
            cards_[static_cast<size_t>(i)] = { x0 + desloc + c * (kLarguraCard + kEspaco), kAlturaDica + l * (kAlturaCard + kEspaco),
                                               kLarguraCard, kAlturaCard };
        }
    }

    void paint(juce::Graphics& g) override {
        const auto p = moldura::paleta();
        g.setColour(p.tinta);
        g.setFont(fonteUi(14.0f));
        g.drawText(dica_, getLocalBounds().removeFromTop(kAlturaDica - 4), juce::Justification::centredLeft, true);

        for (size_t i = 0; i < nos_.size(); ++i) {
            const auto& no = nos_[i];
            const bool ativo = sobre_ == static_cast<int>(i) && !no.emBreve;
            auto r = cards_[i].toFloat().reduced(2.0f);
            const float alfa = no.emBreve ? 0.5f : 1.0f;

            g.setColour(p.campo.withMultipliedAlpha(alfa));
            g.fillRoundedRectangle(r, 16.0f);
            g.setColour((ativo ? p.grafite : p.linha).withMultipliedAlpha(alfa));
            g.drawRoundedRectangle(r.reduced(0.5f), 16.0f, ativo ? 2.0f : 1.2f);

            auto interno = r.reduced(10.0f, 10.0f);
            auto areaIcone = interno.removeFromTop(68.0f);
            const auto& img = imagemDoIcone(no.arquivoIcone);
            if (img.isValid()) {
                // Os ícones de Assets/Icons já trazem o próprio fundo: desenhados direto, sem azulejo.
                g.setOpacity(alfa);
                const auto alvo = areaIcone.withSizeKeepingCentre(66.0f, 66.0f);
                // Logos de marca (Instagram, TikTok, X...) são quadrados cheios: cantos arredondados como os demais cards.
                const bool logoDeMarca = no.arquivoIcone.rfind("ci_", 0) != 0 && no.arquivoIcone != "googledrive_png";
                juce::Graphics::ScopedSaveState salvo(g);
                if (logoDeMarca) {
                    juce::Path recorte;
                    recorte.addRoundedRectangle(alvo, 15.0f);
                    g.reduceClipRegion(recorte);
                }
                g.drawImage(img, alvo, juce::RectanglePlacement::centred);
            } else {
                auto azulejo = areaIcone.withSizeKeepingCentre(56.0f, 56.0f);
                g.setColour(p.poco.withMultipliedAlpha(alfa));
                g.fillRoundedRectangle(azulejo, 16.0f);
                desenharIcone(g, no.icone, azulejo.reduced(13.0f), no.cor.withMultipliedAlpha(alfa));
            }

            interno.removeFromTop(4.0f);
            const bool temSub = no.comSub;
            auto areaSub = temSub ? interno.removeFromBottom(26.0f) : juce::Rectangle<float>();
            g.setColour(p.tinta.withMultipliedAlpha(alfa));
            g.setFont(fonteUi(13.0f, juce::Font::bold).withExtraKerningFactor(0.03f));
            const juce::String titulo = no.rotuloLiteral.isNotEmpty() ? no.rotuloLiteral : t("imp.node." + no.id);
            g.drawFittedText(titulo, interno.toNearestInt(), temSub ? juce::Justification::centredTop : juce::Justification::centred, 2);
            if (temSub) {
                g.setColour(p.mudo.withMultipliedAlpha(alfa));
                g.setFont(fonteUi(11.0f));
                g.drawFittedText(t("imp.sub." + no.id), areaSub.toNearestInt(), juce::Justification::centredTop, 2);
            }

            if (no.emBreve) {
                const juce::String selo = t("imp.embreve");
                g.setFont(fonteUi(9.5f, juce::Font::bold).withExtraKerningFactor(0.08f));
                const float w = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), selo)) + 12.0f;
                auto pilula = juce::Rectangle<float>(w, 16.0f).withRightX(r.getRight() - 8.0f).withY(r.getY() + 8.0f);
                g.setColour(p.grafite);
                g.fillRoundedRectangle(pilula, 8.0f);
                g.setColour(p.branco);
                g.drawText(selo, pilula, juce::Justification::centred, false);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int novo = cardEm(e.getPosition());
        if (novo != sobre_) {
            sobre_ = novo;
            const bool clicavel = novo >= 0 && !nos_[static_cast<size_t>(novo)].emBreve;
            setMouseCursor(clicavel ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (sobre_ != -1) { sobre_ = -1; repaint(); }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        const int i = cardEm(e.getPosition());
        if (i < 0 || e.mouseWasDraggedSinceMouseDown() || nos_[static_cast<size_t>(i)].emBreve) return;
        if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(100 + i);
    }

    bool tratarTecla(const juce::KeyPress& k) {
        if (k == juce::KeyPress::escapeKey) {
            if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(0);
            return true;
        }
        return false;
    }

private:
    int cardEm(juce::Point<int> pt) const {
        for (size_t i = 0; i < cards_.size(); ++i)
            if (cards_[i].contains(pt)) return static_cast<int>(i);
        return -1;
    }

    std::vector<No> nos_;
    juce::String dica_;
    std::vector<juce::Rectangle<int>> cards_;
    int sobre_ = -1;
};

using AoConcluir = std::function<void(std::optional<std::string>)>;

void mostrarNivel(PainelOverlay& overlay, std::vector<No> nos, juce::String titulo, const std::string& chaveDica,
                  std::function<void()> voltar, AoConcluir aoConcluir) {
    const bool ehSub = voltar != nullptr;
    auto conteudo = std::make_shared<GradeImportar>(nos, t(chaveDica.empty() ? (ehSub ? "imp.dica.sub" : "imp.dica.raiz") : chaveDica));
    const auto tam = GradeImportar::tamanhoIdeal(static_cast<int>(nos.size()));

    PainelOverlay::Config cfg;
    cfg.titulo = titulo;
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.conteudo = conteudo;
    cfg.larguraCartao = tam.x + 2 * PainelOverlay::kMargem + 8;
    cfg.alturaConteudo = tam.y;
    if (ehSub) cfg.botoes.push_back({ t("imp.voltar"), 1, false, false });
    cfg.botoes.push_back({ t("light.ui.common.cancel"), 0, false, true });
    juce::Component::SafePointer<GradeImportar> seguroGrade(conteudo.get());
    cfg.aoTecla = [seguroGrade](const juce::KeyPress& k) { return seguroGrade != nullptr && seguroGrade->tratarTecla(k); };

    juce::Component::SafePointer<PainelOverlay> seguroOverlay(&overlay);
    overlay.mostrar(cfg, [seguroOverlay, nos, titulo, chaveDica, voltar, aoConcluir](PainelOverlay::Resultado r) {
        if (r.botaoId >= 100) {
            const size_t idx = static_cast<size_t>(r.botaoId - 100);
            if (idx >= nos.size()) { aoConcluir(std::nullopt); return; }
            const No no = nos[idx];
            if (no.filhos.empty()) { aoConcluir(no.id); return; }
            // Adiado: este callback roda de dentro do mouseUp do card que o overlay vai substituir.
            juce::MessageManager::callAsync([seguroOverlay, nos, titulo, chaveDica, voltar, aoConcluir, no] {
                if (seguroOverlay == nullptr) { aoConcluir(std::nullopt); return; }
                const juce::String nomeNo = no.rotuloLiteral.isNotEmpty() ? no.rotuloLiteral : t("imp.node." + no.id);
                const juce::String tituloSub = titulo + juce::String::fromUTF8(" \xe2\x80\xba ") + nomeNo;
                mostrarNivel(*seguroOverlay, no.filhos, tituloSub, no.chaveDica,
                             [seguroOverlay, nos, titulo, chaveDica, voltar, aoConcluir] {
                                 mostrarNivel(*seguroOverlay, nos, titulo, chaveDica, voltar, aoConcluir);
                             },
                             aoConcluir);
            });
        } else if (r.botaoId == 1 && voltar) {
            juce::MessageManager::callAsync([seguroOverlay, voltar, aoConcluir] {
                if (seguroOverlay == nullptr) { aoConcluir(std::nullopt); return; }
                voltar();
            });
        } else {
            aoConcluir(std::nullopt);
        }
    });
}

// Opções do campo "formato de origem" da ficha de vídeo: digital nativo, fitas, discos óticos e filme.
// O texto exibido é o traduzido; canonicalMedium() devolve o nome em inglês que o catálogo guarda.
std::vector<juce::String> opcoesFormatoDeVideo() {
    const bool pt = matriz::i18n::localeAtivo().startsWith("pt");
    std::vector<juce::String> opcoes;
    opcoes.push_back(OriginalSourceMediumVocabulary::translateMedium("Native Digital", pt));
    for (const auto& grupoMedio : OriginalSourceMediumVocabulary::getMediumCategories()) {
        const auto nome = grupoMedio.groupName;
        if (!(nome.startsWith("VIDEO") || nome.startsWith("OPTICAL") || nome.startsWith("FILM"))) continue;
        for (const auto& medio : grupoMedio.mediums) opcoes.push_back(OriginalSourceMediumVocabulary::translateMedium(medio, pt));
    }
    opcoes.push_back(OriginalSourceMediumVocabulary::translateMedium("Other", pt));
    return opcoes;
}

} // namespace

void mostrarMenuImportar(PainelOverlay& overlay, std::function<void(std::optional<std::string>)> aoConcluir) {
    mostrarNivel(overlay, montarArvore(), t("imp.titulo"), "", nullptr, std::move(aoConcluir));
}

std::shared_ptr<const matriz::ingest::SobreMaterial> contextoDaFolhaImportar(const std::string& id) {
    // Valores no vocabulário canônico (em inglês) do CONTENT e do SOURCE MEDIUM do Matriz. Só entra o que o card
    // afirma com certeza; o resto (tamanho do vinil, formato do vídeo...) vem das fichas de cada categoria.
    struct Linha { const char* id; const char* content; const char* medium; std::vector<const char*> tags; };
    static const std::vector<Linha> tabela = {
        { "files.external_drive", "", "External Hard Drive (HDD)", {} },
        { "files.optical", "", "", { "Optical media" } },
        { "photos.folder", "Photo", "", {} },
        { "photos.camera_card", "Photo", "", { "Camera card" } },
        { "audio.production.project", "", "", { "Music Production" } },
        { "audio.production.samples", "Sample Pack", "", {} },
        { "audio.production.midi", "MIDI", "", {} },
        { "audio.production.assets", "", "", { "Production assets" } },
        { "audio.preservation.ddp", "Master", "", { "DDP master" } },
        { "audio.preservation.vinyl", "", "", { "Vinyl rip" } },
        { "audio.preservation.tape", "", "", { "Tape rip" } },
        { "audio.general.soundtrack", "Soundtrack", "", {} },
        { "audio.general.film_sound", "", "", { "Film sound" } },
        { "audio.general.audiobook", "", "", { "Audiobook" } },
        { "audio.general.field", "Field Recording", "", {} },
        { "audio.general.voice_over", "Voiceover", "", {} },
        { "audio.general.voice_notes", "", "", { "Voice notes" } },
        { "documents.native", "", "Native Digital", {} },
        { "documents.scanned", "", "", { "Scanned" } },
        { "documents.web", "", "", { "Web" } },
        { "other.multimedia.apps", "", "", { "App / software" } },
        { "other.multimedia.interactive", "", "", { "Interactive multimedia" } },
        { "other.social.instagram", "", "", { "Social media", "Instagram" } },
        { "other.social.facebook", "", "", { "Social media", "Facebook" } },
        { "other.social.tiktok", "", "", { "Social media", "TikTok" } },
        { "other.social.x", "", "", { "Social media", "Twitter / X" } },
        { "other.social.other", "", "", { "Social media" } },
    };
    for (const auto& l : tabela) {
        if (id != l.id) continue;
        auto sobre = std::make_shared<matriz::ingest::SobreMaterial>();
        sobre->collectionType = l.content;
        for (const char* tag : l.tags) sobre->tags.push_back(tag);
        if (std::string(l.medium).size() > 0) {
            OriginalSourceMediumInfo info;
            info.medium = l.medium;
            sobre->campos.push_back({ "source_media", info.serialize() });
        }
        return sobre;
    }
    return nullptr;
}

std::shared_ptr<const matriz::ingest::SobreMaterial> mesclarContextosImportar(
    std::shared_ptr<const matriz::ingest::SobreMaterial> base, std::shared_ptr<const matriz::ingest::SobreMaterial> extra) {
    if (extra == nullptr) return base;
    if (base == nullptr) return extra;
    auto r = std::make_shared<matriz::ingest::SobreMaterial>(*base);
    if (!extra->collectionType.empty()) r->collectionType = extra->collectionType;
    if (!extra->subject.empty()) r->subject = r->subject.empty() ? extra->subject : r->subject + ", " + extra->subject;
    if (!extra->descricao.empty()) r->descricao = r->descricao.empty() ? extra->descricao : r->descricao + "\n\n" + extra->descricao;
    for (const auto& p : extra->pessoas) r->pessoas.push_back(p);
    for (const auto& tag : extra->tags) r->tags.push_back(tag);
    for (const auto& c : extra->campos) {
        bool achou = false;
        for (auto& existente : r->campos)
            if (existente.first == c.first) { existente.second = c.second; achou = true; break; }
        if (!achou) r->campos.push_back(c);
    }
    if (!extra->sufixoTitulo.empty()) r->sufixoTitulo = extra->sufixoTitulo;
    return r;
}

bool folhaImportarTemFicha(const std::string& id) {
    return id == "videos.digital" || id == "videos.other";
}

void mostrarFichaImportar(PainelOverlay& overlay, const std::string& id,
                          std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)> aoConcluir) {
    if (!folhaImportarTemFicha(id)) { if (aoConcluir) aoConcluir(true, nullptr); return; }

    // Ficha de VÍDEO: o formato de origem é pergunta da ficha (e vira SOURCE MEDIUM de cada arquivo), não um card.
    const auto opcoes = opcoesFormatoDeVideo();
    PainelOverlay::Config cfg;
    cfg.titulo = t("imp.ficha.videos.titulo");
    cfg.mensagem = t("imp.ficha.videos.msg");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.larguraCartao = 620;
    cfg.campos = {
        { "formato", t("imp.ficha.formato"), "", t("imp.ficha.formato.dica"), opcoes },
        { "notas", t("imp.ficha.notas"), "", t("imp.ficha.notas.dica"), {} },
    };
    cfg.botoes = {
        { t("light.ui.common.cancel"), 0, false, true },
        { t("imp.ficha.pular"), 2, false, false },
        { t("imp.ficha.continuar"), 1, true, false },
    };
    overlay.mostrar(cfg, [aoConcluir](PainelOverlay::Resultado r) {
        if (!aoConcluir) return;
        if (r.botaoId == 0) { aoConcluir(false, nullptr); return; }
        if (r.botaoId == 2) { aoConcluir(true, nullptr); return; }  // PULAR: segue sem o que a ficha perguntaria
        const auto valor = [&r](const char* campo) {
            const auto it = r.valores.find(campo);
            return it == r.valores.end() ? juce::String() : it->second.trim();
        };
        auto sobre = std::make_shared<matriz::ingest::SobreMaterial>();
        if (valor("formato").isNotEmpty()) {
            OriginalSourceMediumInfo info;
            info.medium = OriginalSourceMediumVocabulary::canonicalMedium(valor("formato").toStdString());
            sobre->campos.push_back({ "source_media", info.serialize() });
        }
        sobre->descricao = valor("notas").toStdString();
        aoConcluir(true, sobre->vazio() ? nullptr : sobre);
    });
}

std::vector<std::string> idsDasFolhasDoMenuImportar() {
    std::vector<std::string> ids;
    coletarFolhas(montarArvore(), ids);
    return ids;
}

} // namespace matriz::ui
