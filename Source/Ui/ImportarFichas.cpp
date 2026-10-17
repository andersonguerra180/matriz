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
std::vector<juce::String> opcoesFormato(const std::vector<const char*>& prefixosDeGrupo, bool comNativoDigital = true) {
    const bool pt = matriz::i18n::localeAtivo().startsWith("pt");
    std::vector<juce::String> opcoes;
    if (comNativoDigital) opcoes.push_back(OriginalSourceMediumVocabulary::translateMedium("Native Digital", pt));
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

const fs::Tipo& tipoFerramentas() {
    static const fs::Tipo tipo{ "tools", "Production tools", "Ferramentas de produção", {
        { "asset_kind", "Kind", "Tipo", "", "", 1, false,
          { { "instrument", "Virtual instrument", "Instrumento virtual" }, { "preset", "Preset", "Preset" },
            { "ir", "Impulse response (IR)", "Resposta ao impulso (IR)" }, { "plugin", "Plugin", "Plugin" }, { "other", "Other", "Outro" } } },
        { "asset_name", "Name or vendor", "Nome ou fabricante", "", "", 0, true, {} },
    } };
    return tipo;
}

const fs::Tipo& tipoFita() {
    static const fs::Tipo tipo{ "tape_rip", "Tape / Cassette transcription", "Transcrição de fita / cassete", {
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
    if (folha == "audio.production.tools") return &tipoFerramentas();
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

// "Outro" e "Personalizado": o usuário diz do que se trata (vira o SUBJECT dos arquivos e entra nas notas).
bool ehOutro(const std::string& id) {
    return id == "other.custom" || (id.size() > 6 && id.compare(id.size() - 6, 6, ".other") == 0);
}

PainelOverlay::CampoTexto campoOutro() {
    return { "outro", t("imp.ficha.outro"), "", t("imp.ficha.outro.dica"), {} };
}

void aplicarOutro(matriz::ingest::SobreMaterial& sobre, const std::string& oQue) {
    if (oQue.empty()) return;
    sobre.subject = sobre.subject.empty() ? oQue : oQue + ", " + sobre.subject;
    sobre.descricao = (sobre.descricao.empty() ? std::string() : sobre.descricao + "\n") + "What it is: " + oQue;
}

void mostrarFichaOutro(PainelOverlay& overlay, std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)> aoConcluir) {
    PainelOverlay::Config cfg;
    cfg.titulo = t("imp.ficha.outro.titulo");
    cfg.mensagem = t("imp.ficha.outro.msg");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.larguraCartao = 620;
    cfg.campos = { campoOutro(), { "notas", t("imp.ficha.notas"), "", t("imp.ficha.notas.dica"), {} } };
    cfg.botoes = {
        { t("light.ui.common.cancel"), 0, false, true },
        { t("imp.ficha.pular"), 2, false, false },
        { t("imp.ficha.continuar"), 1, true, false },
    };
    overlay.mostrar(cfg, [aoConcluir](PainelOverlay::Resultado r) {
        if (!aoConcluir) return;
        if (r.botaoId == 0) { aoConcluir(false, nullptr); return; }
        if (r.botaoId == 2) { aoConcluir(true, nullptr); return; }
        auto sobre = std::make_shared<matriz::ingest::SobreMaterial>();
        sobre->descricao = valorDe(r, "notas");
        aplicarOutro(*sobre, valorDe(r, "outro"));
        aoConcluir(true, sobre->vazio() ? nullptr : sobre);
    });
}

void mostrarFichaVideo(PainelOverlay& overlay, bool comOutro,
                       std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)> aoConcluir) {
    // O formato de origem é pergunta da ficha (e vira SOURCE MEDIUM de cada arquivo), não um card: o mesmo material
    // pode ter começado numa fita e estar hoje num arquivo digital ou num link.
    const auto opcoes = opcoesFormato({ "VIDEO", "OPTICAL", "FILM" });
    PainelOverlay::Config cfg;
    cfg.titulo = t("imp.ficha.videos.titulo");
    cfg.mensagem = t("imp.ficha.videos.msg");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.larguraCartao = 620;
    if (comOutro) cfg.campos.push_back(campoOutro());
    cfg.campos.push_back({ "formato", t("imp.ficha.formato"), "", t("imp.ficha.formato.dica"), opcoes });
    cfg.campos.push_back({ "notas", t("imp.ficha.notas"), "", t("imp.ficha.notas.dica"), {} });
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
        aplicarOutro(*sobre, valorDe(r, "outro"));
        aoConcluir(true, sobre->vazio() ? nullptr : sobre);
    });
}

void mostrarFichaAudio(PainelOverlay& overlay, const std::string& folha,
                       std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)> aoConcluir) {
    const fs::Tipo* tipoPtr = tipoDaFolha(folha);
    if (tipoPtr == nullptr) { if (aoConcluir) aoConcluir(true, nullptr); return; }
    fs::Tipo tipo = *tipoPtr;
    // "Rip" só vale para CD e DVD: vinil e fita são transcrições.
    if (std::string(tipo.id) == "vinyl_rip") { tipo.en = "Vinyl transcription"; tipo.pt = "Transcrição de vinil"; }
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
    cfg.mensagem = t(ddp ? "imp.ficha.ddp.msg" : "imp.ficha.audio.msg");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.larguraCartao = 620;
    const bool comOutro = ehOutro(folha);
    if (comOutro) cfg.campos.push_back(campoOutro());
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
        size_t depoisDoTipo = (producao ? 1 : 0) + (comOutro ? 1 : 0);
        for (const auto* c : campos) {
            if (c == &comuns[1] || c == &comuns[2]) break;
            ++depoisDoTipo;
        }
        // DDP é a master que fabrica o CD industrial: a origem é sempre essa, então não se pergunta o formato.
        if (!ddp)
            cfg.campos.insert(cfg.campos.begin() + static_cast<std::ptrdiff_t>(depoisDoTipo),
                              PainelOverlay::CampoTexto{ "formato", t("imp.ficha.formato"), "", t("imp.ficha.formato.dica.audio"), opcoesDoFormato });
    }
    cfg.botoes = {
        { t("light.ui.common.cancel"), 0, false, true },
        { t("imp.ficha.pular"), 2, false, false },
        { t("imp.ficha.continuar"), 1, true, false },
    };

    overlay.mostrar(cfg, [aoConcluir, tipo, campos, opcoesPorCampo, producao, dawsLista, comOutro, ddp](PainelOverlay::Resultado r) {
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
        if (!ddp) gravarFormato(*sobre, juce::String(valorDe(r, "formato")));
        sobre->subject = assunto;
        sobre->descricao = desc == ("Audio: " + tipoEn) ? std::string() : desc;  // só o rótulo do tipo não é informação nova
        sobre->tags = tags;
        if (comOutro) aplicarOutro(*sobre, valorDe(r, "outro"));
        aoConcluir(true, sobre->vazio() ? nullptr : sobre);
    });
}

// ── Fichas simples: fotos, arquivos, documentos, multimídia e redes sociais ──────────────────────────────────

juce::String porLocale(const char* en, const char* ptb) {
    return juce::String::fromUTF8(matriz::i18n::localeAtivo() == "pt_BR" ? ptb : en);
}

using AoConcluirFicha = std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)>;
using AplicarFicha = std::function<void(const PainelOverlay::Resultado&, matriz::ingest::SobreMaterial&)>;

// Linha "Rótulo: valor" nas notas (NOTES) de cada arquivo.
void linhaDeNota(matriz::ingest::SobreMaterial& sobre, const char* rotulo, const std::string& valor) {
    if (valor.empty()) return;
    sobre.descricao += (sobre.descricao.empty() ? std::string() : std::string("\n")) + rotulo + ": " + valor;
}

PainelOverlay::CampoTexto campoTexto(const char* id, const char* chave, const char* chaveDica = nullptr) {
    return { id, t(chave), "", chaveDica != nullptr ? t(chaveDica) : juce::String(), {} };
}

PainelOverlay::CampoTexto campoEscolha(const char* id, const char* chave, const std::vector<juce::String>& opcoes, const char* chaveDica = nullptr) {
    return { id, t(chave), "", chaveDica != nullptr ? t(chaveDica) : juce::String(), opcoes };
}

// Campo dentro do cartão "GEO LOCATION" (cidade, estado e país ficam lado a lado).
PainelOverlay::CampoTexto campoGeo(const char* id, const char* chave) {
    return { id, t(chave), "", "", {}, false, "", "", "", "", t("imp.ficha.geo") };
}

void mostrarFichaSimples(PainelOverlay& overlay, const char* chaveTitulo, std::vector<PainelOverlay::CampoTexto> campos,
                         AplicarFicha aplicar, AoConcluirFicha aoConcluir) {
    PainelOverlay::Config cfg;
    cfg.titulo = t(chaveTitulo);
    cfg.mensagem = t("imp.ficha.msg.opcional");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.larguraCartao = 620;
    cfg.campos = std::move(campos);
    cfg.botoes = {
        { t("light.ui.common.cancel"), 0, false, true },
        { t("imp.ficha.pular"), 2, false, false },
        { t("imp.ficha.continuar"), 1, true, false },
    };
    overlay.mostrar(cfg, [aoConcluir, aplicar](PainelOverlay::Resultado r) {
        if (!aoConcluir) return;
        if (r.botaoId == 0) { aoConcluir(false, nullptr); return; }
        if (r.botaoId == 2) { aoConcluir(true, nullptr); return; }
        auto sobre = std::make_shared<matriz::ingest::SobreMaterial>();
        aplicar(r, *sobre);
        aoConcluir(true, sobre->vazio() ? nullptr : sobre);
    });
}

void fichaFotos(PainelOverlay& overlay, AoConcluirFicha aoConcluir) {
    mostrarFichaSimples(overlay, "imp.ficha.fotos.titulo", {
        campoTexto("assunto", "imp.ficha.assunto.fotos"),
        campoEscolha("formato", "imp.ficha.formato", opcoesFormato({ "IMAGE" }), "imp.ficha.formato.dica.generico"),
        campoTexto("ano", "imp.ficha.ano", "imp.ficha.ano.dica"),
        campoTexto("notas", "imp.ficha.notas", "imp.ficha.notas.dica"),
        campoGeo("cidade", "imp.ficha.cidade"), campoGeo("estado", "imp.ficha.estado"), campoGeo("pais", "imp.ficha.pais"),
    }, [](const PainelOverlay::Resultado& r, matriz::ingest::SobreMaterial& s) {
        s.subject = valorDe(r, "assunto");
        gravarFormato(s, juce::String(valorDe(r, "formato")));
        const std::string ano = valorDe(r, "ano");
        if (!ano.empty()) s.campos.push_back({ "ano", ano });
        s.descricao = valorDe(r, "notas");
        s.geoPadrao.cidade = valorDe(r, "cidade");
        s.geoPadrao.estado = valorDe(r, "estado");
        s.geoPadrao.pais = valorDe(r, "pais");
    }, std::move(aoConcluir));
}

void fichaArquivos(PainelOverlay& overlay, const std::string& folha, AoConcluirFicha aoConcluir) {
    const auto formatos = folha == "files.optical" ? opcoesFormato({ "OPTICAL" }) : opcoesFormato({ "SOLID STATE", "OPTICAL" });
    std::vector<PainelOverlay::CampoTexto> campos = {
        campoTexto("assunto", "imp.ficha.assunto.arquivos"),
        campoEscolha("formato", "imp.ficha.formato", formatos, "imp.ficha.formato.dica.generico"),
    };
    if (folha != "files.computer_folder") campos.push_back(campoTexto("rotulo", "imp.ficha.rotulo", "imp.ficha.rotulo.dica"));
    campos.push_back(campoTexto("notas", "imp.ficha.notas", "imp.ficha.notas.dica"));
    mostrarFichaSimples(overlay, "imp.ficha.arquivos.titulo", std::move(campos),
                        [](const PainelOverlay::Resultado& r, matriz::ingest::SobreMaterial& s) {
        s.subject = valorDe(r, "assunto");
        gravarFormato(s, juce::String(valorDe(r, "formato")));
        s.descricao = valorDe(r, "notas");
        linhaDeNota(s, "Label", valorDe(r, "rotulo"));
    }, std::move(aoConcluir));
}

void fichaDocumentos(PainelOverlay& overlay, const std::string& folha, AoConcluirFicha aoConcluir) {
    std::vector<PainelOverlay::CampoTexto> campos;
    if (folha == "documents.web") campos.push_back(campoTexto("url", "imp.ficha.url", "imp.ficha.url.dica"));
    campos.push_back(campoTexto("assunto", "imp.ficha.assunto.docs"));
    if (folha == "documents.scanned")
        campos.push_back(campoEscolha("formato", "imp.ficha.formato", opcoesFormato({ "DOCUMENT", "IMAGE" }, false), "imp.ficha.formato.dica.generico"));
    campos.push_back(campoTexto("notas", "imp.ficha.notas", "imp.ficha.notas.dica"));
    mostrarFichaSimples(overlay, "imp.ficha.docs.titulo", std::move(campos),
                        [](const PainelOverlay::Resultado& r, matriz::ingest::SobreMaterial& s) {
        s.subject = valorDe(r, "assunto");
        gravarFormato(s, juce::String(valorDe(r, "formato")));
        s.descricao = valorDe(r, "notas");
        const std::string url = valorDe(r, "url");
        if (!url.empty()) {
            s.campos.push_back({ "dc_source", url });  // endereço de onde a página/o site vivia
            linhaDeNota(s, "URL", url);
        }
    }, std::move(aoConcluir));
}

void fichaMultimidia(PainelOverlay& overlay, const std::string& folha, AoConcluirFicha aoConcluir) {
    std::vector<juce::String> tipos = { porLocale("Game", "Jogo"), porLocale("Presentation", "Apresentação"), porLocale("E-learning", "E-learning"),
                                        porLocale("Kiosk or installation", "Quiosque ou instalação"), porLocale("Other", "Outro") };
    std::vector<PainelOverlay::CampoTexto> campos = { campoTexto("titulo", "imp.ficha.titulo.app") };
    if (folha == "other.multimedia.interactive") campos.push_back(campoEscolha("tipo", "imp.ficha.tipo.interativo", tipos));
    campos.push_back(campoTexto("plataforma", "imp.ficha.plataforma", "imp.ficha.plataforma.dica"));
    campos.push_back(campoEscolha("formato", "imp.ficha.formato", opcoesFormato({ "OPTICAL", "SOLID STATE" }), "imp.ficha.formato.dica.generico"));
    campos.push_back(campoTexto("notas", "imp.ficha.notas", "imp.ficha.notas.dica"));
    mostrarFichaSimples(overlay, "imp.ficha.multimidia.titulo", std::move(campos),
                        [tipos](const PainelOverlay::Resultado& r, matriz::ingest::SobreMaterial& s) {
        s.subject = valorDe(r, "titulo");
        gravarFormato(s, juce::String(valorDe(r, "formato")));
        s.descricao = valorDe(r, "notas");
        linhaDeNota(s, "Platform", valorDe(r, "plataforma"));
        const std::string tipo = valorDe(r, "tipo");
        if (!tipo.empty() && indiceDe(tipos, juce::String::fromUTF8(tipo.c_str())) >= 0) {
            s.tags.push_back(tipo);
            linhaDeNota(s, "Kind", tipo);
        }
    }, std::move(aoConcluir));
}

void fichaRedeSocial(PainelOverlay& overlay, const std::string& folha, AoConcluirFicha aoConcluir) {
    static const std::map<std::string, std::string> redes = { { "other.social.instagram", "Instagram" }, { "other.social.facebook", "Facebook" },
                                                              { "other.social.tiktok", "TikTok" }, { "other.social.x", "Twitter / X" } };
    const auto rede = redes.find(folha);
    const std::string nomeDaRede = rede != redes.end() ? rede->second : std::string();
    std::vector<PainelOverlay::CampoTexto> campos;
    if (nomeDaRede.empty()) campos.push_back(campoTexto("rede", "imp.ficha.rede"));  // "Outra rede": o usuário diz qual
    campos.push_back(campoTexto("usuario", "imp.ficha.usuario"));
    campos.push_back(campoTexto("link", "imp.ficha.link"));
    campos.push_back(campoTexto("notas", "imp.ficha.notas", "imp.ficha.notas.dica"));
    mostrarFichaSimples(overlay, "imp.ficha.rede.titulo", std::move(campos),
                        [nomeDaRede](const PainelOverlay::Resultado& r, matriz::ingest::SobreMaterial& s) {
        const std::string nome = nomeDaRede.empty() ? valorDe(r, "rede") : nomeDaRede;
        s.subject = nome;
        if (nomeDaRede.empty() && !nome.empty()) s.tags.push_back(nome);
        s.descricao = valorDe(r, "notas");
        linhaDeNota(s, "Network", nome);
        const std::string usuario = valorDe(r, "usuario"), link = valorDe(r, "link");
        if (!usuario.empty()) s.tags.push_back(usuario);
        linhaDeNota(s, "User", usuario);
        if (!link.empty()) s.campos.push_back({ "dc_source", link });
        linhaDeNota(s, "Link", link);
    }, std::move(aoConcluir));
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
        { "audio.production.tools", "", "", { "Production tools" } },
        { "audio.preservation.ddp", "Master", "", { "DDP master" } },
        { "audio.preservation.vinyl", "", "", { "Vinyl transcription" } },
        { "audio.preservation.tape", "", "", { "Tape transcription" } },
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
    if (!extra->geoPadrao.vazio()) r->geoPadrao = extra->geoPadrao;
    return r;
}

bool folhaImportarTemFicha(const std::string& id) {
    if (id == "videos.digital" || id == "videos.other") return true;
    if (id.rfind("other.social.", 0) == 0 || id.rfind("other.multimedia.", 0) == 0) return id != "other.multimedia.cdrom";
    if (id == "files.computer_folder" || id == "files.external_drive" || id == "files.optical") return true;
    if (id == "photos.folder" || id == "photos.camera_card") return true;
    if (id == "documents.native" || id == "documents.scanned" || id == "documents.web") return true;
    if (ehOutro(id)) return true;
    return id.rfind("audio.", 0) == 0 && tipoDaFolha(id) != nullptr;
}

void mostrarFichaImportar(PainelOverlay& overlay, const std::string& id,
                          std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)> aoConcluir) {
    if (!folhaImportarTemFicha(id)) { if (aoConcluir) aoConcluir(true, nullptr); return; }
    if (id.rfind("audio.", 0) == 0) mostrarFichaAudio(overlay, id, std::move(aoConcluir));
    else if (id.rfind("videos.", 0) == 0) mostrarFichaVideo(overlay, id == "videos.other", std::move(aoConcluir));
    else if (id.rfind("photos.", 0) == 0) fichaFotos(overlay, std::move(aoConcluir));
    else if (id.rfind("documents.", 0) == 0) fichaDocumentos(overlay, id, std::move(aoConcluir));
    else if (id.rfind("other.social.", 0) == 0) fichaRedeSocial(overlay, id, std::move(aoConcluir));
    else if (id.rfind("other.multimedia.", 0) == 0) fichaMultimidia(overlay, id, std::move(aoConcluir));
    else if (id == "files.computer_folder" || id == "files.external_drive" || id == "files.optical") fichaArquivos(overlay, id, std::move(aoConcluir));
    else mostrarFichaOutro(overlay, std::move(aoConcluir));  // files.other, other.custom
}

} // namespace matriz::ui
