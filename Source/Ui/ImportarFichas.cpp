#include "ImportarMenu.h"

#include <map>

#include "../I18n/Strings.h"
#include "FichasSomDados.h"
#include "OriginalSourceMedium.h"

// Contexto de cada card da janela IMPORT: o que a escolha já implica (tabela) e as fichas que perguntam o resto
// (vídeo e áudio). Tudo vira um SobreMaterial aplicado a cada arquivo importado (ver MainComponent::ingerirArquivos).

namespace matriz::ui {

using matriz::i18n::t;
namespace fs = matriz::ui::mini::fichasom;

namespace {

// Opções do campo "formato de origem": digital nativo, os grupos do vocabulário cujo nome começa com um dos
// prefixos dados e "Outro". O texto exibido é o traduzido; canonicalMedium() devolve o nome em inglês que o catálogo guarda.
std::vector<juce::String> opcoesFormato(const std::vector<const char*>& prefixosDeGrupo) {
    const bool pt = matriz::i18n::localeAtivo().startsWith("pt");
    std::vector<juce::String> opcoes;
    opcoes.push_back(OriginalSourceMediumVocabulary::translateMedium("Native Digital", pt));
    for (const auto& grupoMedio : OriginalSourceMediumVocabulary::getMediumCategories()) {
        bool serve = false;
        for (const char* prefixo : prefixosDeGrupo) serve = serve || grupoMedio.groupName.startsWith(prefixo);
        if (!serve) continue;
        for (const auto& medio : grupoMedio.mediums) opcoes.push_back(OriginalSourceMediumVocabulary::translateMedium(medio, pt));
    }
    opcoes.push_back(OriginalSourceMediumVocabulary::translateMedium("Other", pt));
    return opcoes;
}

void gravarFormato(matriz::ingest::SobreMaterial& sobre, const juce::String& escolhido) {
    if (escolhido.isEmpty()) return;
    OriginalSourceMediumInfo info;
    info.medium = OriginalSourceMediumVocabulary::canonicalMedium(escolhido.toStdString());
    sobre.campos.push_back({ "source_media", info.serialize() });
}

std::string valorDe(const PainelOverlay::Resultado& r, const std::string& campo) {
    const auto it = r.valores.find(juce::String(campo));
    return it == r.valores.end() ? std::string() : it->second.trim().toStdString();
}

int indiceDe(const std::vector<juce::String>& opcoes, const juce::String& valor) {
    for (size_t i = 0; i < opcoes.size(); ++i)
        if (opcoes[i] == valor) return static_cast<int>(i);
    return -1;
}

// ── Áudio: tipos que o Mini já tinha (FichasSomDados.h) + os que o menu novo acrescenta ─────────────────────

const fs::Tipo& tipoAssets() {
    static const fs::Tipo tipo{ "assets", "Production assets", "Assets de produção", {
        { "asset_kind", "Kind", "Tipo", "", "", 1, false,
          { { "instrument", "Virtual instrument", "Instrumento virtual" }, { "preset", "Preset", "Preset" },
            { "ir", "Impulse response (IR)", "Resposta ao impulso (IR)" }, { "plugin", "Plugin", "Plugin" }, { "other", "Other", "Outro" } } },
        { "asset_name", "Name or vendor", "Nome ou fabricante", "", "", 0, true, {} },
    } };
    return tipo;
}

const fs::Tipo& tipoFita() {
    static const fs::Tipo tipo{ "tape_rip", "Tape / Cassette rip", "Rip de fita / cassete", {
        { "artist_title", "Artist and title", "Artista e título", "", "", 0, false, {} },
        { "condition", "Tape condition", "Estado da fita", "", "", 1, false,
          { { "good", "Good", "Boa" }, { "worn", "Worn", "Desgastada" }, { "damaged", "Damaged", "Danificada" } } },
        { "noise_reduction", "Noise reduction", "Redução de ruído", "", "", 1, true,
          { { "none", "None", "Nenhuma" }, { "dolby_b", "Dolby B", "Dolby B" }, { "dolby_c", "Dolby C", "Dolby C" }, { "dbx", "dbx", "dbx" } } },
    } };
    return tipo;
}

const fs::Tipo& tipoSomDeFilme() {
    static const fs::Tipo tipo{ "film_sound", "Film sound", "Som de filme", {
        { "project", "Film or project", "Filme ou projeto", "", "", 0, false, {} },
        { "sound_kind", "Sound type", "Tipo de som", "", "", 1, false,
          { { "dialogue", "Dialogue", "Diálogo" }, { "foley", "Foley", "Foley" }, { "ambience", "Ambience", "Ambiência" },
            { "sfx", "Sound effects", "Efeitos sonoros" }, { "adr", "ADR", "ADR" }, { "mix", "Mix / stems", "Mix / stems" } } },
        { "scene", "Scene or reel", "Cena ou rolo", "", "", 0, true, {} },
    } };
    return tipo;
}

const fs::Tipo& tipoOutroAudio() {
    static const fs::Tipo tipo{ "other_audio", "Other audio", "Outro áudio", {} };
    return tipo;
}

const fs::Tipo* achaTipoDoMini(const std::string& id) {
    for (const auto& tp : fs::tipos())
        if (id == tp.id) return &tp;
    return nullptr;
}

// Folha do menu -> tipo de ficha de áudio.
const fs::Tipo* tipoDaFolha(const std::string& folha) {
    if (folha == "audio.production.project") return achaTipoDoMini("music_production");
    if (folha == "audio.production.samples") return achaTipoDoMini("sample_pack");
    if (folha == "audio.production.midi") return achaTipoDoMini("midi");
    if (folha == "audio.production.assets") return &tipoAssets();
    if (folha == "audio.preservation.ddp") return achaTipoDoMini("ddp_master");
    if (folha == "audio.preservation.vinyl") return achaTipoDoMini("vinyl_rip");
    if (folha == "audio.preservation.tape") return &tipoFita();
    if (folha == "audio.general.soundtrack") return achaTipoDoMini("soundtrack");
    if (folha == "audio.general.film_sound") return &tipoSomDeFilme();
    if (folha == "audio.general.audiobook") return achaTipoDoMini("audiobook");
    if (folha == "audio.general.field") return achaTipoDoMini("field_recording");
    if (folha == "audio.general.voice_over") return achaTipoDoMini("voice_over");
    if (folha == "audio.general.voice_notes") return achaTipoDoMini("voice_memo");
    if (folha == "audio.general.other") return &tipoOutroAudio();
    return nullptr;
}

void mostrarFichaVideo(PainelOverlay& overlay, std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)> aoConcluir) {
    // O formato de origem é pergunta da ficha (e vira SOURCE MEDIUM de cada arquivo), não um card: o mesmo material
    // pode ter começado numa fita e estar hoje num arquivo digital ou num link.
    const auto opcoes = opcoesFormato({ "VIDEO", "OPTICAL", "FILM" });
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
        auto sobre = std::make_shared<matriz::ingest::SobreMaterial>();
        gravarFormato(*sobre, juce::String(valorDe(r, "formato")));
        sobre->descricao = valorDe(r, "notas");
        aoConcluir(true, sobre->vazio() ? nullptr : sobre);
    });
}

void mostrarFichaAudio(PainelOverlay& overlay, const std::string& folha,
                       std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)> aoConcluir) {
    const fs::Tipo* tipoPtr = tipoDaFolha(folha);
    if (tipoPtr == nullptr) { if (aoConcluir) aoConcluir(true, nullptr); return; }
    const fs::Tipo tipo = *tipoPtr;
    const bool pt = matriz::i18n::localeAtivo() == "pt_BR";
    const auto rotulo = [pt](const char* en, const char* ptb) { return juce::String::fromUTF8(pt ? ptb : en); };
    const bool producao = std::string(tipo.id) == "music_production";
    const bool ddp = std::string(tipo.id) == "ddp_master";

    // Campos do tipo; o "formato de origem" único substitui a "mídia de gravação" da produção e a "origem" comum do Mini.
    std::vector<const fs::Campo*> campos;
    for (const auto& c : tipo.campos)
        if (std::string(c.id) != "recording_medium") campos.push_back(&c);
    const auto& comuns = fs::comuns();  // [0] origem (substituída), [1] identificação do cartão/CD/fita, [2] contexto
    if (!producao && !ddp) campos.push_back(&comuns[1]);
    if (!producao) campos.push_back(&comuns[2]);  // a produção musical já tem a própria Descrição

    const auto opcoesDoFormato = opcoesFormato({ "AUDIO", "OPTICAL" });
    const std::vector<juce::String> dawsLista = { "Pro Tools", "Logic Pro", "Reaper", "Ableton Live", "FL Studio", "Audacity", "Other" };

    PainelOverlay::Config cfg;
    cfg.titulo = t("imp.ficha.audio.titulo") + juce::String::fromUTF8(" \xe2\x80\xba ") + rotulo(tipo.en, tipo.pt);
    cfg.mensagem = t("imp.ficha.audio.msg");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.larguraCartao = 620;
    if (producao) cfg.campos.push_back({ "daw", t("imp.ficha.daw"), "", "", dawsLista });
    std::vector<std::vector<juce::String>> opcoesPorCampo;
    for (const auto* c : campos) {
        std::vector<juce::String> ops;
        for (const auto& o : c->opcoes) ops.push_back(rotulo(o.en, o.pt));
        opcoesPorCampo.push_back(ops);
        cfg.campos.push_back({ juce::String("f") + juce::String(static_cast<int>(opcoesPorCampo.size() - 1)), rotulo(c->labelEn, c->labelPt),
                               "", rotulo(c->dicaEn, c->dicaPt), ops });
    }
    // Posição do formato: depois dos campos do tipo, antes de identificação/contexto.
    {
        size_t depoisDoTipo = producao ? 1 : 0;
        for (const auto* c : campos) {
            if (c == &comuns[1] || c == &comuns[2]) break;
            ++depoisDoTipo;
        }
        cfg.campos.insert(cfg.campos.begin() + static_cast<std::ptrdiff_t>(depoisDoTipo),
                          PainelOverlay::CampoTexto{ "formato", t("imp.ficha.formato"), "", t("imp.ficha.formato.dica.audio"), opcoesDoFormato });
    }
    cfg.botoes = {
        { t("light.ui.common.cancel"), 0, false, true },
        { t("imp.ficha.pular"), 2, false, false },
        { t("imp.ficha.continuar"), 1, true, false },
    };

    overlay.mostrar(cfg, [aoConcluir, tipo, campos, opcoesPorCampo, producao, dawsLista](PainelOverlay::Resultado r) {
        if (!aoConcluir) return;
        if (r.botaoId == 0) { aoConcluir(false, nullptr); return; }
        if (r.botaoId == 2) { aoConcluir(true, nullptr); return; }  // PULAR: segue só com o que o card já implica

        auto sobre = std::make_shared<matriz::ingest::SobreMaterial>();
        const std::string tipoEn = tipo.en;
        std::vector<std::string> tags;
        if (std::string(tipo.id) != "other_audio") tags.push_back(tipoEn);
        std::string desc = tipoEn.empty() ? std::string() : "Audio: " + tipoEn;
        std::string assunto;
        for (size_t k = 0; k < campos.size(); ++k) {
            const auto* c = campos[k];
            const std::string valor = valorDe(r, "f" + std::to_string(k));
            if (valor.empty()) continue;
            std::string mostrado = valor;
            std::string opcaoId;
            if (c->entrada == 1) {  // escolha: grava o rótulo em inglês
                const int o = indiceDe(opcoesPorCampo[k], juce::String::fromUTF8(valor.c_str()));
                if (o < 0) continue;
                mostrado = c->opcoes[static_cast<size_t>(o)].en;
                opcaoId = c->opcoes[static_cast<size_t>(o)].id;
                tags.push_back(mostrado);
            }
            desc += std::string(desc.empty() ? "" : "\n") + c->labelEn + ": " + mostrado;
            const std::string id = c->id;
            if (id == "stage") {  // etapa de produção -> campo de cada arquivo (REC / MIX / MSTR / FINAL)
                const char* codigo = opcaoId == "recording" ? "REC" : opcaoId == "mixing" ? "MIX" : opcaoId == "mastering" ? "MSTR" : opcaoId == "final" ? "FINAL" : "";
                if (codigo[0] != 0) sobre->campos.push_back({ "etapa_producao", codigo });
            }
            if (assunto.empty() && (id == "artist" || id == "artist_album" || id == "artist_title" || id == "pack_name" || id == "book_title" || id == "project"))
                assunto = mostrado;
        }
        if (producao) {
            const int d = indiceDe(dawsLista, juce::String(valorDe(r, "daw")));
            if (d >= 0) {
                tags.push_back(dawsLista[static_cast<size_t>(d)].toStdString());
                desc += "\nDAW: " + dawsLista[static_cast<size_t>(d)].toStdString();
            }
        }
        gravarFormato(*sobre, juce::String(valorDe(r, "formato")));
        sobre->subject = assunto;
        sobre->descricao = desc == ("Audio: " + tipoEn) ? std::string() : desc;  // só o rótulo do tipo não é informação nova
        sobre->tags = tags;
        aoConcluir(true, sobre->vazio() ? nullptr : sobre);
    });
}

} // namespace

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
        // Conjuntos que entram juntos e realizam uma ação viram CONTAINER no Intake (ver MainComponent::processarLoteEmBackground).
        if (id == "audio.production.project") sobre->containerOrigem = "production";
        if (id == "audio.preservation.ddp") sobre->containerOrigem = "ddp";
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
    if (!extra->containerOrigem.empty()) r->containerOrigem = extra->containerOrigem;
    return r;
}

bool folhaImportarTemFicha(const std::string& id) {
    if (id == "videos.digital" || id == "videos.other") return true;
    return id.rfind("audio.", 0) == 0 && tipoDaFolha(id) != nullptr;
}

void mostrarFichaImportar(PainelOverlay& overlay, const std::string& id,
                          std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)> aoConcluir) {
    if (!folhaImportarTemFicha(id)) { if (aoConcluir) aoConcluir(true, nullptr); return; }
    if (id.rfind("audio.", 0) == 0) mostrarFichaAudio(overlay, id, std::move(aoConcluir));
    else mostrarFichaVideo(overlay, std::move(aoConcluir));
}

} // namespace matriz::ui
