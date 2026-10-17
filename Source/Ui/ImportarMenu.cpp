#include "ImportarMenu.h"

#include <AssetsBinaryData.h>

#include "../I18n/Strings.h"
#include "Moldura.h"
#include "Tablet.h"
#include "Tokens.h"

namespace matriz::ui {

using matriz::i18n::t;

namespace {

enum class Logo { Nenhum, Drive, Lightroom, Bridge };

// Um card da janela: folha (conclui com o id) ou grupo (abre o nível seguinte).
struct No {
    std::string id;
    IconeUi icone = IconeUi::Other;
    Logo logo = Logo::Nenhum;
    bool emBreve = false;   // card já no lugar, ainda sem fluxo: aparece apagado com selo "em breve"
    bool comSub = false;    // mostra a linha de subtítulo (só os cards do nível raiz)
    juce::Colour cor;
    std::vector<No> filhos;
};

No folha(const std::string& id, IconeUi icone, bool emBreve = false, Logo logo = Logo::Nenhum) {
    No n;
    n.id = id;
    n.icone = icone;
    n.emBreve = emBreve;
    n.logo = logo;
    return n;
}

No grupo(const std::string& id, IconeUi icone, std::vector<No> filhos) {
    No n;
    n.id = id;
    n.icone = icone;
    n.filhos = std::move(filhos);
    return n;
}

void colorir(No& n, juce::Colour cor) {
    n.cor = cor;
    for (auto& f : n.filhos) colorir(f, cor);
}

// A árvore inteira. Os ids das folhas são o contrato com IntakeWorkspaceComponent::tratarEscolhaImportar.
std::vector<No> montarArvore() {
    const auto& tk = tema();

    std::vector<No> raiz;

    auto files = grupo("files", IconeUi::StorageUnit,
                       { folha("files.computer_folder", IconeUi::Pasta),
                         folha("files.external_hdd", IconeUi::StorageUnit),
                         folha("files.sd_card", IconeUi::StorageUnit),
                         folha("files.usb", IconeUi::StorageUnit),
                         folha("files.cd", IconeUi::Disco),
                         folha("files.dvd", IconeUi::Disco),
                         folha("files.other", IconeUi::Other) });
    colorir(files, juce::Colour(0xff0284c7));
    raiz.push_back(std::move(files));

    auto drive = folha("drive", IconeUi::StorageUnit, false, Logo::Drive);
    colorir(drive, juce::Colour(0xff1a73e8));
    raiz.push_back(std::move(drive));

    auto phone = folha("phone", IconeUi::PhoneGallery);
    colorir(phone, juce::Colour(0xff3b82f6));
    raiz.push_back(std::move(phone));

    auto photos = grupo("photos", IconeUi::Photography,
                        { folha("photos.folder", IconeUi::Pasta),
                          folha("photos.camera_card", IconeUi::Photography),
                          folha("photos.lightroom", IconeUi::Photography, false, Logo::Lightroom),
                          folha("photos.bridge", IconeUi::Photography, false, Logo::Bridge) });
    colorir(photos, tk.tipoImagem);
    raiz.push_back(std::move(photos));

    auto videos = grupo("videos", IconeUi::Videos,
                        { folha("videos.native", IconeUi::Videos),
                          folha("videos.vhs", IconeUi::Videos),
                          folha("videos.minidv", IconeUi::Videos),
                          folha("videos.betacam", IconeUi::Videos),
                          folha("videos.film", IconeUi::Videos),
                          folha("videos.dvd_rip", IconeUi::Disco, true),
                          folha("videos.link", IconeUi::Link, true),
                          folha("videos.other", IconeUi::Other) });
    colorir(videos, tk.tipoVideo);
    raiz.push_back(std::move(videos));

    auto audio = grupo("audio", IconeUi::Sound,
                       { folha("audio.music", IconeUi::Sound),
                         folha("audio.production", IconeUi::Sound),
                         folha("audio.samples", IconeUi::Sound),
                         folha("audio.field", IconeUi::Sound),
                         folha("audio.voice_memo", IconeUi::Sound),
                         folha("audio.voice_over", IconeUi::Sound),
                         folha("audio.audiobook", IconeUi::Documents),
                         folha("audio.soundtrack", IconeUi::Videos),
                         folha("audio.midi", IconeUi::Sound),
                         folha("audio.vinyl_rip", IconeUi::Disco),
                         folha("audio.tape_rip", IconeUi::Cassete),
                         folha("audio.ddp", IconeUi::Disco),
                         folha("audio.cd_rip", IconeUi::Disco, true) });
    colorir(audio, tk.tipoAudio);
    raiz.push_back(std::move(audio));

    auto documents = grupo("documents", IconeUi::Documents,
                           { folha("documents.digital", IconeUi::Documents),
                             folha("documents.scanned", IconeUi::Documents),
                             folha("documents.photocopy", IconeUi::Documents),
                             folha("documents.other", IconeUi::Other) });
    colorir(documents, tk.tipoDocumento);
    raiz.push_back(std::move(documents));

    // OTHER fica por último: Web, Multimedia, Social Media e Custom.
    auto outros = grupo("other", IconeUi::Other,
                        { folha("other.web", IconeUi::Web, true),
                          grupo("other.multimedia", IconeUi::Multimedia,
                                { folha("other.multimedia.cdrom", IconeUi::CdRom, true),
                                  folha("other.multimedia.apps", IconeUi::Multimedia),
                                  folha("other.multimedia.interactive", IconeUi::Multimedia) }),
                          grupo("other.social", IconeUi::SocialMedia,
                                { folha("other.social.instagram", IconeUi::SocialMedia),
                                  folha("other.social.facebook", IconeUi::SocialMedia),
                                  folha("other.social.tiktok", IconeUi::SocialMedia),
                                  folha("other.social.x", IconeUi::SocialMedia),
                                  folha("other.social.other", IconeUi::SocialMedia) }),
                          folha("other.custom", IconeUi::Other) });
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

const juce::Image& imagemDoLogo(Logo logo) {
    static const juce::Image drive = juce::ImageFileFormat::loadFrom(AssetsBinaryData::googledrive_png, AssetsBinaryData::googledrive_pngSize);
    static const juce::Image lightroom = juce::ImageFileFormat::loadFrom(AssetsBinaryData::lrlogo_png, AssetsBinaryData::lrlogo_pngSize);
    static const juce::Image bridge = juce::ImageFileFormat::loadFrom(AssetsBinaryData::bridgelogo_png, AssetsBinaryData::bridgelogo_pngSize);
    static const juce::Image vazio;
    switch (logo) {
        case Logo::Drive: return drive;
        case Logo::Lightroom: return lightroom;
        case Logo::Bridge: return bridge;
        default: return vazio;
    }
}

constexpr int kLarguraCard = 136;
constexpr int kAlturaCard = 148;
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

            auto interno = r.reduced(10.0f, 12.0f);
            auto azulejo = interno.removeFromTop(56.0f).withSizeKeepingCentre(56.0f, 56.0f);
            g.setColour(p.poco.withMultipliedAlpha(alfa));
            g.fillRoundedRectangle(azulejo, 16.0f);
            const auto& img = imagemDoLogo(no.logo);
            if (img.isValid()) {
                g.setOpacity(alfa);
                g.drawImage(img, azulejo.reduced(10.0f), juce::RectanglePlacement::centred);
            } else {
                desenharIcone(g, no.icone, azulejo.reduced(13.0f), no.cor.withMultipliedAlpha(alfa));
            }

            interno.removeFromTop(6.0f);
            const bool temSub = no.comSub;
            auto areaSub = temSub ? interno.removeFromBottom(30.0f) : juce::Rectangle<float>();
            g.setColour(p.tinta.withMultipliedAlpha(alfa));
            g.setFont(fonteUi(13.0f, juce::Font::bold).withExtraKerningFactor(0.03f));
            g.drawFittedText(t("imp.node." + no.id), interno.toNearestInt(),
                             temSub ? juce::Justification::centredTop : juce::Justification::centred, 2);
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

void mostrarNivel(PainelOverlay& overlay, std::vector<No> nos, juce::String titulo, bool ehSub,
                  std::function<void()> voltar, AoConcluir aoConcluir) {
    auto conteudo = std::make_shared<GradeImportar>(nos, t(ehSub ? "imp.dica.sub" : "imp.dica.raiz"));
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
    overlay.mostrar(cfg, [seguroOverlay, nos, titulo, ehSub, voltar, aoConcluir](PainelOverlay::Resultado r) {
        if (r.botaoId >= 100) {
            const size_t idx = static_cast<size_t>(r.botaoId - 100);
            if (idx >= nos.size()) { aoConcluir(std::nullopt); return; }
            const No no = nos[idx];
            if (no.filhos.empty()) { aoConcluir(no.id); return; }
            // Adiado: este callback roda de dentro do mouseUp do card que o overlay vai substituir.
            juce::MessageManager::callAsync([seguroOverlay, nos, titulo, ehSub, voltar, aoConcluir, no] {
                if (seguroOverlay == nullptr) { aoConcluir(std::nullopt); return; }
                const juce::String tituloSub = titulo + juce::String::fromUTF8(" \xe2\x80\xba ") + t("imp.node." + no.id);
                mostrarNivel(*seguroOverlay, no.filhos, tituloSub, true,
                             [seguroOverlay, nos, titulo, ehSub, voltar, aoConcluir] {
                                 mostrarNivel(*seguroOverlay, nos, titulo, ehSub, voltar, aoConcluir);
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

} // namespace

void mostrarMenuImportar(PainelOverlay& overlay, std::function<void(std::optional<std::string>)> aoConcluir) {
    mostrarNivel(overlay, montarArvore(), t("imp.titulo"), false, nullptr, std::move(aoConcluir));
}

std::vector<std::string> idsDasFolhasDoMenuImportar() {
    std::vector<std::string> ids;
    coletarFolhas(montarArvore(), ids);
    return ids;
}

} // namespace matriz::ui
