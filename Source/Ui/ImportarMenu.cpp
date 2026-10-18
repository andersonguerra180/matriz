#include "ImportarMenu.h"

#include <AssetsBinaryData.h>

#include <algorithm>
#include <map>

#include "../I18n/Strings.h"
#include "GoogleDriveContas.h"
#include "Moldura.h"
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
    bool filhosEmLista = false;   // os filhos aparecem como lista (linhas), não como cards (ex.: contas do Google Drive)
    juce::String detalhe;         // segunda linha da linha da lista (ex.: pasta da conta); não passa por i18n
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
    drive.filhosEmLista = true;
    for (size_t i = 0; i < contas.size(); ++i) {
        No c;
        c.id = "drive." + std::to_string(i);
        c.icone = IconeUi::StorageUnit;
        c.arquivoIcone = "googledrive_png";
        c.rotuloLiteral = contas[i].copiaAntiga ? contas[i].rotulo + " (" + t("gdrive.copia_antiga") + ")" : contas[i].rotulo;
        c.detalhe = contas[i].pasta.getFullPathName();
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
                          // Logos originais da Adobe (Assets/Icons), com cantos arredondados no desenho do card.
                          folha("photos.lightroom", IconeUi::Photography, "lrlogo_original_png"),
                          folha("photos.bridge", IconeUi::Photography, "bridgelogo_png") });
    colorir(photos, tk.tipoImagem);
    raiz.push_back(std::move(photos));

    // VIDEOS: o formato de origem (VHS, MiniDV, filme...) NÃO é um card — é um campo da ficha, porque o mesmo
    // material pode ter vindo de uma fita e estar hoje num arquivo digital ou num link.
    auto videos = grupo("videos", IconeUi::Videos, "ci_vid_native_digital_png",
                        { folha("videos.digital", IconeUi::Videos, "ci_vid_native_digital_png"),
                          folha("videos.link", IconeUi::Link, "youtube_png", true),
                          folha("videos.dvd_rip", IconeUi::Disco, "ci_vid_dvd_blu_ray_png", true),
                          folha("videos.other", IconeUi::Other, "ci_vid_other_png") });
    colorir(videos, tk.tipoVideo);
    raiz.push_back(std::move(videos));

    auto audio = grupo("audio", IconeUi::Sound, "ci_aud_music_production_png",
        { grupo("audio.production", IconeUi::Sound, "ci_aud_music_production_png",
                { folha("audio.production.project", IconeUi::Sound, "ci_aud_music_production_png"),
                  folha("audio.production.samples", IconeUi::Sound, "ci_aud_sample_pack_png"),
                  folha("audio.production.midi", IconeUi::Sound, "ci_aud_midi_png"),
                  folha("audio.production.tools", IconeUi::Sound, "Tools_png") }),
          grupo("audio.preservation", IconeUi::Disco, "ci_aud_vinyl_rip_png",
                { folha("audio.preservation.cd_rip", IconeUi::Disco, "ci_aud_cd_rip_png", true),
                  folha("audio.preservation.ddp", IconeUi::Disco, "DDP_png"),
                  folha("audio.preservation.vinyl", IconeUi::Disco, "ci_aud_vinyl_rip_png"),
                  folha("audio.preservation.tape", IconeUi::Cassete, "Tape_Cassette_Rip_png") }),
          grupo("audio.general", IconeUi::Sound, "ci_aud_field_recording_png",
                { folha("audio.general.soundtrack", IconeUi::Videos, "ci_aud_soundtrack_png"),
                  folha("audio.general.film_sound", IconeUi::Videos, "Film_Sound_png"),
                  folha("audio.general.audiobook", IconeUi::Documents, "ci_aud_audiobook_png"),
                  folha("audio.general.field", IconeUi::Sound, "ci_aud_field_recording_png"),
                  folha("audio.general.voice_over", IconeUi::Sound, "ci_aud_voice_over_png"),
                  folha("audio.general.voice_notes", IconeUi::Sound, "ci_aud_voice_memo_png"),
                  folha("audio.general.other", IconeUi::Other, "Other_png") }) });
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
          folha("other.design", IconeUi::Multimedia, "3d_jpg"),
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
        juce::Image img = dados != nullptr ? juce::ImageFileFormat::loadFrom(dados, static_cast<size_t>(tamanho)) : juce::Image();
        if (img.isValid()) img = tirarFundoBrancoDasBordas(img);
        it = cache.emplace(recurso, img).first;
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
                g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
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

// Lista de linhas (logo, nome e detalhe) para quando a escolha é entre poucos itens com texto longo (contas do Drive).
class ListaImportar : public juce::Component {
public:
    static constexpr int kAlturaLinha = 60;

    ListaImportar(std::vector<No> nos, juce::String dica) : nos_(std::move(nos)), dica_(std::move(dica)) {
        setComponentID("imp.lista");
        setWantsKeyboardFocus(false);
    }

    static int alturaIdeal(int n) { return kAlturaDica + n * kAlturaLinha + juce::jmax(0, n - 1) * 8; }

    void paint(juce::Graphics& g) override {
        const auto p = moldura::paleta();
        g.setColour(p.tinta);
        g.setFont(fonteUi(14.0f));
        g.drawText(dica_, getLocalBounds().removeFromTop(kAlturaDica - 4), juce::Justification::centredLeft, true);
        for (size_t i = 0; i < nos_.size(); ++i) {
            const auto& no = nos_[i];
            auto r = linha(static_cast<int>(i)).toFloat().reduced(1.0f);
            const bool ativo = sobre_ == static_cast<int>(i);
            g.setColour(p.campo);
            g.fillRoundedRectangle(r, 14.0f);
            g.setColour(ativo ? p.grafite : p.linha);
            g.drawRoundedRectangle(r.reduced(0.5f), 14.0f, ativo ? 2.0f : 1.2f);
            auto interno = r.reduced(12.0f, 8.0f);
            const auto& img = imagemDoIcone(no.arquivoIcone);
            auto areaLogo = interno.removeFromLeft(44.0f);
            if (img.isValid()) {
                g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                g.drawImage(img, areaLogo, juce::RectanglePlacement::centred);
            }
            interno.removeFromLeft(12.0f);
            auto linhaNome = interno.removeFromTop(interno.getHeight() * 0.5f);
            g.setColour(p.tinta);
            g.setFont(fonteUi(14.0f, juce::Font::bold));
            g.drawText(no.rotuloLiteral.isNotEmpty() ? no.rotuloLiteral : t("imp.node." + no.id), linhaNome.toNearestInt(), juce::Justification::bottomLeft, true);
            g.setColour(p.mudo);
            g.setFont(fonteUi(11.5f));
            g.drawText(no.detalhe, interno.toNearestInt(), juce::Justification::topLeft, true);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int novo = linhaEm(e.getPosition());
        if (novo != sobre_) {
            sobre_ = novo;
            setMouseCursor(novo >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent&) override {
        if (sobre_ != -1) { sobre_ = -1; repaint(); }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        const int i = linhaEm(e.getPosition());
        if (i < 0 || e.mouseWasDraggedSinceMouseDown()) return;
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
    juce::Rectangle<int> linha(int i) const { return { 0, kAlturaDica + i * (kAlturaLinha + 8), getWidth(), kAlturaLinha }; }
    int linhaEm(juce::Point<int> pt) const {
        for (int i = 0; i < static_cast<int>(nos_.size()); ++i)
            if (linha(i).contains(pt)) return i;
        return -1;
    }

    std::vector<No> nos_;
    juce::String dica_;
    int sobre_ = -1;
};

using AoConcluir = std::function<void(std::optional<std::string>, std::vector<std::string>)>;

// Um nível da janela (a raiz ou um grupo aberto) e o estado da navegação: o botão VOLTAR de cada nível reabre o anterior,
// e a escolha de uma folha devolve também o caminho de grupos até ela (para a ficha poder voltar ao mesmo lugar).
struct NivelDescr {
    std::vector<No> nos;
    juce::String titulo;
    std::string chaveDica;
    bool emLista = false;
    std::string idGrupo;
};

struct EstadoMenu {
    juce::Component::SafePointer<PainelOverlay> overlay;
    std::vector<NivelDescr> niveis;
    AoConcluir aoConcluir;
};

void mostrarNivelEm(std::shared_ptr<EstadoMenu> st, size_t indice) {
    if (st->overlay == nullptr) { st->aoConcluir(std::nullopt, {}); return; }
    const NivelDescr nivel = st->niveis[indice];
    const bool ehSub = indice > 0;
    const juce::String dica = t(nivel.chaveDica.empty() ? (ehSub ? "imp.dica.sub" : "imp.dica.raiz") : nivel.chaveDica);

    PainelOverlay::Config cfg;
    cfg.titulo = nivel.titulo;
    cfg.trilha = t("moldura.trilha.adicionar");
    if (ehSub) cfg.botoes.push_back({ t("imp.voltar"), 1, false, false });
    cfg.botoes.push_back({ t("light.ui.common.cancel"), 0, false, true });
    if (nivel.emLista) {
        auto lista = std::make_shared<ListaImportar>(nivel.nos, dica);
        cfg.conteudo = lista;
        cfg.larguraCartao = 620;
        cfg.alturaConteudo = ListaImportar::alturaIdeal(static_cast<int>(nivel.nos.size()));
        juce::Component::SafePointer<ListaImportar> seguroLista(lista.get());
        cfg.aoTecla = [seguroLista](const juce::KeyPress& k) { return seguroLista != nullptr && seguroLista->tratarTecla(k); };
    } else {
        auto conteudo = std::make_shared<GradeImportar>(nivel.nos, dica);
        const auto tam = GradeImportar::tamanhoIdeal(static_cast<int>(nivel.nos.size()));
        cfg.conteudo = conteudo;
        cfg.larguraCartao = tam.x + 2 * PainelOverlay::kMargem + 8;
        cfg.alturaConteudo = tam.y;
        juce::Component::SafePointer<GradeImportar> seguroGrade(conteudo.get());
        cfg.aoTecla = [seguroGrade](const juce::KeyPress& k) { return seguroGrade != nullptr && seguroGrade->tratarTecla(k); };
    }

    st->overlay->mostrar(cfg, [st, indice](PainelOverlay::Resultado r) {
        if (r.botaoId >= 100) {
            const std::vector<No>& nos = st->niveis[indice].nos;
            const size_t idx = static_cast<size_t>(r.botaoId - 100);
            if (idx >= nos.size()) { st->aoConcluir(std::nullopt, {}); return; }
            const No no = nos[idx];
            if (no.filhos.empty()) {
                std::vector<std::string> caminho;
                for (size_t k = 1; k <= indice; ++k) caminho.push_back(st->niveis[k].idGrupo);
                st->aoConcluir(no.id, caminho);
                return;
            }
            // Adiado: este callback roda de dentro do mouseUp do card que o overlay vai substituir.
            juce::MessageManager::callAsync([st, indice, no] {
                if (st->overlay == nullptr) { st->aoConcluir(std::nullopt, {}); return; }
                NivelDescr novo;
                novo.nos = no.filhos;
                novo.titulo = st->niveis[indice].titulo + juce::String::fromUTF8(" \xe2\x80\xba ") +
                              (no.rotuloLiteral.isNotEmpty() ? no.rotuloLiteral : t("imp.node." + no.id));
                novo.chaveDica = no.chaveDica;
                novo.emLista = no.filhosEmLista;
                novo.idGrupo = no.id;
                st->niveis.resize(indice + 1);
                st->niveis.push_back(std::move(novo));
                mostrarNivelEm(st, indice + 1);
            });
        } else if (r.botaoId == 1 && indice > 0) {
            juce::MessageManager::callAsync([st, indice] { mostrarNivelEm(st, indice - 1); });
        } else {
            st->aoConcluir(std::nullopt, {});
        }
    });
}

} // namespace

// Alguns ícones de Assets/Icons vêm com fundo branco opaco (os demais são transparentes): o branco ligado às bordas
// vira transparente (preenchimento a partir das bordas, então o branco de dentro do desenho fica como está).
juce::Image tirarFundoBrancoDasBordas(const juce::Image& original) {
    juce::Image img = original.convertedToFormat(juce::Image::ARGB);
    const int w = img.getWidth(), h = img.getHeight();
    juce::Image::BitmapData px(img, juce::Image::BitmapData::readWrite);
    const auto brancoOpaco = [&](int x, int y) {
        const auto c = px.getPixelColour(x, y);
        return c.getAlpha() > 250 && c.getRed() >= 240 && c.getGreen() >= 240 && c.getBlue() >= 240;
    };
    if (!brancoOpaco(0, 0)) return original;  // já transparente (ou sem fundo branco): nada a fazer
    std::vector<juce::Point<int>> pilha;
    std::vector<char> visto(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    const auto empurrar = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        if (visto[i] || !brancoOpaco(x, y)) return;
        visto[i] = 1;
        pilha.push_back({ x, y });
    };
    for (int x = 0; x < w; ++x) { empurrar(x, 0); empurrar(x, h - 1); }
    for (int y = 0; y < h; ++y) { empurrar(0, y); empurrar(w - 1, y); }
    while (!pilha.empty()) {
        const auto p = pilha.back();
        pilha.pop_back();
        px.setPixelColour(p.x, p.y, juce::Colours::transparentBlack);
        empurrar(p.x + 1, p.y); empurrar(p.x - 1, p.y); empurrar(p.x, p.y + 1); empurrar(p.x, p.y - 1);
    }
    return img;
}

void mostrarMenuImportar(PainelOverlay& overlay,
                         std::function<void(std::optional<std::string>, std::vector<std::string>)> aoConcluir,
                         std::vector<std::string> caminhoInicial) {
    auto st = std::make_shared<EstadoMenu>();
    st->overlay = &overlay;
    st->aoConcluir = std::move(aoConcluir);
    NivelDescr raiz;
    raiz.nos = montarArvore();
    raiz.titulo = t("imp.titulo");
    st->niveis.push_back(std::move(raiz));
    // Abre direto no grupo pedido (volta de uma ficha): desce pelos ids do caminho enquanto eles existirem.
    for (const auto& idGrupo : caminhoInicial) {
        const NivelDescr atual = st->niveis.back();
        const auto it = std::find_if(atual.nos.begin(), atual.nos.end(), [&](const No& n) { return n.id == idGrupo && !n.filhos.empty(); });
        if (it == atual.nos.end()) break;
        NivelDescr novo;
        novo.nos = it->filhos;
        novo.titulo = atual.titulo + juce::String::fromUTF8(" \xe2\x80\xba ") + (it->rotuloLiteral.isNotEmpty() ? it->rotuloLiteral : t("imp.node." + it->id));
        novo.chaveDica = it->chaveDica;
        novo.emLista = it->filhosEmLista;
        novo.idGrupo = it->id;
        st->niveis.push_back(std::move(novo));
    }
    mostrarNivelEm(st, st->niveis.size() - 1);
}

void mostrarListaDeContasParaTeste(PainelOverlay& overlay) {
    auto st = std::make_shared<EstadoMenu>();
    st->overlay = &overlay;
    st->aoConcluir = [](std::optional<std::string>, std::vector<std::string>) {};
    NivelDescr raiz;
    raiz.titulo = t("imp.titulo");
    st->niveis.push_back(raiz);
    NivelDescr lista;
    for (const char* email : { "anderson@exemplo.com", "estudio@exemplo.com.br", "arquivo-antigo@exemplo.com (old copy)" }) {
        No c;
        c.id = "drive." + std::to_string(lista.nos.size());
        c.arquivoIcone = "googledrive_png";
        c.rotuloLiteral = email;
        c.detalhe = juce::String("/Users/exemplo/Library/CloudStorage/GoogleDrive-") + email + "/My Drive";
        lista.nos.push_back(std::move(c));
    }
    lista.titulo = t("imp.titulo") + juce::String::fromUTF8(" \xe2\x80\xba Google Drive");
    lista.chaveDica = "imp.dica.drive";
    lista.emLista = true;
    lista.idGrupo = "drive";
    st->niveis.push_back(std::move(lista));
    mostrarNivelEm(st, 1);
}

std::vector<std::string> idsDasFolhasDoMenuImportar() {
    std::vector<std::string> ids;
    coletarFolhas(montarArvore(), ids);
    return ids;
}

} // namespace matriz::ui
