#include "ImportarMenu.h"
#include "GeoAcoes.h"
#include "ProjetoAberto.h"

#include <cmath>
#include <map>
#include <set>

#include "../I18n/Strings.h"
#include "FichasSomDados.h"
#include "OriginalSourceMedium.h"

// Contexto de cada card da janela IMPORT: o que a escolha já implica (tabela) e as fichas que perguntam o resto.
// Tudo vira um SobreMaterial aplicado a cada arquivo importado (ver MainComponent::ingerirArquivos).
//
// Cada campo da ficha carrega a própria regra de gravação (struct Campo): o callback do overlay só roda as regras dos
// campos, sem ponteiros para dados que já tenham sido destruídos.

namespace matriz::ui {

using matriz::i18n::t;
using matriz::ingest::SobreMaterial;
namespace fs = matriz::ui::mini::fichasom;

namespace {

using Resultado = PainelOverlay::Resultado;
using Aplica = std::function<void(const Resultado&, SobreMaterial&)>;
using AoConcluirFicha = std::function<void(bool, std::shared_ptr<const SobreMaterial>)>;

struct Campo {
    PainelOverlay::CampoTexto def;
    Aplica aplica;
};
using Campos = std::vector<Campo>;

struct Ficha {
    Campos campos;
    std::vector<PainelOverlay::BotaoGrupo> botoesGrupo;
    Aplica depois;  // roda depois de todos os campos (regras que cruzam campos)
};

struct Op { std::string id, en, pt; };
struct FormatoOp { std::string en, pt, medium, nota; };

// ── utilidades ────────────────────────────────────────────────────────────────────────────────────────────

juce::String rot(const std::string& en, const std::string& pt) {
    return juce::String::fromUTF8((matriz::i18n::localeAtivo() == "pt_BR" ? pt : en).c_str());
}

std::string valorDe(const Resultado& r, const std::string& campo) {
    const auto it = r.valores.find(juce::String(campo));
    return it == r.valores.end() ? std::string() : it->second.trim().toStdString();
}

int indiceDe(const std::vector<juce::String>& opcoes, const juce::String& valor) {
    for (size_t i = 0; i < opcoes.size(); ++i)
        if (opcoes[i] == valor) return static_cast<int>(i);
    return -1;
}

// Linha "Rótulo: valor" nas notas (NOTES) de cada arquivo.
void linhaDeNota(SobreMaterial& sobre, const std::string& rotulo, const std::string& valor) {
    if (valor.empty()) return;
    sobre.descricao += (sobre.descricao.empty() ? std::string() : std::string("\n")) + rotulo + ": " + valor;
}

void somarAssunto(SobreMaterial& s, const std::string& valor) {
    if (valor.empty()) return;
    s.subject = s.subject.empty() ? valor : valor + ", " + s.subject;
}

// LIST do Matriz (campo "lista" de cada arquivo) e TAGS a partir de uma lista separada por vírgula.
void definirLista(SobreMaterial& s, const std::string& v) { s.campos.push_back({ "lista", v }); }

void somarTags(SobreMaterial& s, const std::string& v) {
    juce::StringArray partes;
    partes.addTokens(juce::String::fromUTF8(v.c_str()), ",;", "");
    for (auto& p : partes) if (p.trim().isNotEmpty()) s.tags.push_back(p.trim().toStdString());
}

// Texto livre direto em NOTES (sem o rótulo "Campo: ").
void somarNotas(SobreMaterial& s, const std::string& v) { s.descricao += (s.descricao.empty() ? std::string() : std::string("\n")) + v; }

// No máximo `n` palavras (campo curto que diz do que se trata).
std::string primeirasPalavras(const std::string& v, int n) {
    juce::StringArray palavras;
    palavras.addTokens(juce::String::fromUTF8(v.c_str()), " \t\n", "");
    palavras.removeEmptyStrings();
    while (palavras.size() > n) palavras.remove(palavras.size() - 1);
    return palavras.joinIntoString(" ").toStdString();
}

// LIST + nome do container (álbum, título, projeto ou música): o container passa a mostrar este nome.
void definirListaENome(SobreMaterial& s, const std::string& v) {
    definirLista(s, v);
    s.nomeContainer = v;
}

// ── construtores de campo ─────────────────────────────────────────────────────────────────────────────────

Campo cTexto(const std::string& id, const std::string& en, const std::string& pt, const std::string& dicaEn = "",
             const std::string& dicaPt = "", std::function<void(SobreMaterial&, const std::string&)> extra = nullptr,
             bool comoNota = true, int linhas = 1) {
    Campo c;
    c.def = { juce::String(id), rot(en, pt), "", rot(dicaEn, dicaPt), {} };
    c.def.linhas = linhas;
    c.aplica = [id, en, extra, comoNota](const Resultado& r, SobreMaterial& s) {
        const std::string v = valorDe(r, id);
        if (v.empty()) return;
        if (comoNota) linhaDeNota(s, en, v);
        if (extra) extra(s, v);
    };
    return c;
}

Campo cAssunto(const std::string& id, const std::string& en, const std::string& pt, const std::string& dicaEn = "",
               const std::string& dicaPt = "") {
    return cTexto(id, en, pt, dicaEn, dicaPt, [](SobreMaterial& s, const std::string& v) { somarAssunto(s, v); }, false);
}

Campo cEscolha(const std::string& id, const std::string& en, const std::string& pt, const std::vector<Op>& ops, bool comTag = true,
               std::function<void(SobreMaterial&, const Op&)> extra = nullptr, const std::string& dicaEn = "",
               const std::string& dicaPt = "") {
    std::vector<juce::String> rotulos;
    for (const auto& op : ops) rotulos.push_back(rot(op.en, op.pt));
    Campo c;
    c.def = { juce::String(id), rot(en, pt), "", rot(dicaEn, dicaPt), rotulos };
    c.aplica = [id, en, ops, rotulos, comTag, extra](const Resultado& r, SobreMaterial& s) {
        const std::string v = valorDe(r, id);
        if (v.empty()) return;
        const int i = indiceDe(rotulos, juce::String::fromUTF8(v.c_str()));
        if (i < 0) return;
        const Op& op = ops[static_cast<size_t>(i)];
        linhaDeNota(s, en, op.en);
        if (comTag) s.tags.push_back(op.en);
        if (extra) extra(s, op);
    };
    return c;
}

// "Original source format": o que sai escolhido vira SOURCE MEDIUM (JSON de OriginalSourceMediumInfo) de cada arquivo.
Campo cFormato(const std::vector<FormatoOp>& ops, const std::string& id = "formato", const std::string& en = "Original source format",
               const std::string& pt = "Formato de origem", const std::string& dicaEn = "Where the material originally lived. Leave empty if unknown.",
               const std::string& dicaPt = "Onde o material vivia originalmente. Deixe vazio se não souber.") {
    std::vector<juce::String> rotulos;
    for (const auto& op : ops) rotulos.push_back(rot(op.en, op.pt));
    Campo c;
    c.def = { juce::String(id), rot(en, pt), "", rot(dicaEn, dicaPt), rotulos };
    c.aplica = [id, ops, rotulos](const Resultado& r, SobreMaterial& s) {
        const std::string v = valorDe(r, id);
        if (v.empty()) return;
        const int i = indiceDe(rotulos, juce::String::fromUTF8(v.c_str()));
        if (i < 0) return;
        const FormatoOp& op = ops[static_cast<size_t>(i)];
        OriginalSourceMediumInfo info;
        info.medium = op.medium;
        info.customNote = op.nota;
        s.campos.push_back({ "source_media", info.serialize() });
    };
    return c;
}

Campo cInterruptor(const std::string& id, const std::string& en, const std::string& pt, const juce::String& grupo) {
    Campo c;
    c.def = { juce::String(id), rot(en, pt), "", "", {}, true, "", "", "", "", grupo };
    c.aplica = [id, en](const Resultado& r, SobreMaterial& s) {
        if (valorDe(r, id) == "1") s.tags.push_back(en);
    };
    return c;
}

// ── formatos de origem a partir do vocabulário do Matriz ──────────────────────────────────────────────────

// Opções dos grupos do vocabulário cujo nome começa com um dos prefixos. `excluir` tira entradas pelo nome canônico.
std::vector<FormatoOp> formatosDoVocabulario(const std::vector<const char*>& prefixosDeGrupo, bool comNativo = true,
                                             const std::set<std::string>& excluir = {}) {
    std::vector<FormatoOp> ops;
    const auto add = [&](const std::string& nome) {
        if (excluir.count(nome)) return;
        ops.push_back({ nome, OriginalSourceMediumVocabulary::translateMedium(juce::String(nome), true).toStdString(), nome, "" });
    };
    if (comNativo) add("Native Digital");
    for (const auto& grupoMedio : OriginalSourceMediumVocabulary::getMediumCategories()) {
        bool serve = false;
        for (const char* prefixo : prefixosDeGrupo) serve = serve || grupoMedio.groupName.startsWith(prefixo);
        if (!serve) continue;
        for (const auto& medio : grupoMedio.mediums) add(medio.toStdString());
    }
    add("Other");
    return ops;
}

// Formatos por nome canônico, na ordem dada.
std::vector<FormatoOp> formatosPorNome(const std::vector<std::string>& nomes) {
    std::vector<FormatoOp> ops;
    for (const auto& nome : nomes)
        ops.push_back({ nome, OriginalSourceMediumVocabulary::translateMedium(juce::String(nome), true).toStdString(), nome, "" });
    return ops;
}

// "CD-R" e "DVD-R": o vocabulário guarda CD e DVD num nome só; a lista mostra só o que o usuário usa.
void simplificarOpticos(std::vector<FormatoOp>& ops) {
    for (auto& op : ops) {
        if (op.medium == "Audio CD / CD-R / CD-RW") { op.en = "CD-R"; op.pt = "CD-R"; }
        if (op.medium == "DVD / DVD-R / DVD-Video") { op.en = "DVD-R"; op.pt = "DVD-R"; }
    }
}

const std::set<std::string>& semOpticosRaros() {
    static const std::set<std::string> v = { "MiniDisc (MD)", "LaserDisc", "Blu-ray Disc (BD / BD-R)", "Internal Storage / Smartphone" };
    return v;
}

// ── geolocalização (cidade, estado, país, coordenadas) e botão GET ───────────────────────────────────────

void obterLocalizacao(PainelOverlay& overlay) {
    // Local aproximado pelo IP, preenchido sem apagar nada. Só roda quando o usuário clica em GET (GeoAcoes).
    juce::Component::SafePointer<PainelOverlay> seguro(&overlay);
    geoacoes::obterLocalAtual([seguro](geoacoes::Local local) {
        if (seguro == nullptr) return;
        seguro->definirValorCampo("geo_cidade", local.cidade);
        seguro->definirValorCampo("geo_estado", local.estado);
        seguro->definirValorCampo("geo_pais", local.pais);
        seguro->definirValorCampo("geo_coord", local.coords);
    });
}

void abrirLocalizacao(PainelOverlay& overlay) {
    geoacoes::abrirNoGoogleMaps({ overlay.valorDoCampo("geo_coord"), overlay.valorDoCampo("geo_endereco"), overlay.valorDoCampo("geo_cidade"),
                                  overlay.valorDoCampo("geo_estado"), overlay.valorDoCampo("geo_pais") });
}

Campos cGeo() {
    const juce::String grupo = t("imp.ficha.geo");
    const auto campo = [&](const char* id, const char* chave, const char* chaveDica, Aplica aplica) {
        Campo c;
        c.def = { id, t(chave), "", chaveDica != nullptr ? t(chaveDica) : juce::String(), {}, false, "", "", "", "", grupo };
        c.aplica = std::move(aplica);
        return c;
    };
    return {
        campo("geo_endereco", "imp.ficha.endereco", nullptr, [](const Resultado& r, SobreMaterial& s) { s.geoPadrao.endereco = valorDe(r, "geo_endereco"); }),
        campo("geo_cidade", "imp.ficha.cidade", nullptr, [](const Resultado& r, SobreMaterial& s) { s.geoPadrao.cidade = valorDe(r, "geo_cidade"); }),
        campo("geo_estado", "imp.ficha.estado", nullptr, [](const Resultado& r, SobreMaterial& s) { s.geoPadrao.estado = valorDe(r, "geo_estado"); }),
        campo("geo_pais", "imp.ficha.pais", nullptr, [](const Resultado& r, SobreMaterial& s) { s.geoPadrao.pais = valorDe(r, "geo_pais"); }),
        campo("geo_coord", "imp.ficha.coord", "imp.ficha.coord.dica", [](const Resultado& r, SobreMaterial& s) {
            juce::StringArray partes;
            partes.addTokens(juce::String::fromUTF8(valorDe(r, "geo_coord").c_str()).replaceCharacters(";", ","), ", ", "");
            partes.removeEmptyStrings();
            if (partes.size() < 2) return;
            const double lat = partes[0].getDoubleValue(), lng = partes[1].getDoubleValue();
            if (std::abs(lat) <= 90.0 && std::abs(lng) <= 180.0 && (lat != 0.0 || lng != 0.0)) { s.geoPadrao.lat = lat; s.geoPadrao.lng = lng; }
        }),
    };
}

PainelOverlay::BotaoGrupo botaoGet() {
    return { t("imp.ficha.geo"), t("imp.ficha.geo.get"), [](PainelOverlay& o) { obterLocalizacao(o); } };
}

PainelOverlay::BotaoGrupo botaoOpen() {
    return { t("imp.ficha.geo"), t("imp.ficha.geo.open"), [](PainelOverlay& o) { abrirLocalizacao(o); } };
}

void juntarGeo(Ficha& f) {
    for (auto& c : cGeo()) f.campos.push_back(std::move(c));
    f.botoesGrupo.push_back(botaoGet());
    f.botoesGrupo.push_back(botaoOpen());
}

// ── capa do container (vinil, fita) ───────────────────────────────────────────────────────────────────────

void juntarCapa(Ficha& f, const char* chaveGrupo = "imp.ficha.capa", const char* chaveArquivo = "imp.ficha.capa.arquivo",
                const char* chaveDica = "imp.ficha.capa.dica", const char* chaveBotao = "imp.ficha.capa.botao") {
    const juce::String grupo = t(chaveGrupo);
    Campo c;
    c.def = { "capa", t(chaveArquivo), "", t(chaveDica), {}, false, "", "", "", "", grupo };
    c.aplica = [](const Resultado& r, SobreMaterial& s) { s.capaContainer = valorDe(r, "capa"); };
    f.campos.push_back(std::move(c));
    f.botoesGrupo.push_back({ grupo, t(chaveBotao), [](PainelOverlay& overlay) {
        auto seletor = std::make_shared<juce::FileChooser>(t("imp.ficha.capa.titulo"), juce::File(), "*.jpg;*.jpeg;*.png;*.tif;*.tiff;*.bmp;*.gif;*.webp");
        juce::Component::SafePointer<PainelOverlay> seguro(&overlay);
        seletor->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [seguro, seletor](const juce::FileChooser& fc) {
                                 const auto escolhido = fc.getResult();
                                 if (seguro != nullptr && escolhido.existsAsFile()) seguro->definirValorCampo("capa", escolhido.getFullPathName());
                             });
    } });
}

// ── mostrar uma ficha ─────────────────────────────────────────────────────────────────────────────────────

void mostrarFicha(PainelOverlay& overlay, const juce::String& titulo, const juce::String& mensagem, Ficha ficha, AoConcluirFicha aoConcluir,
                  std::function<void()> aoVoltar, std::string folhaId = {}, const std::map<std::string, std::string>* iniciais = nullptr) {
    PainelOverlay::Config cfg;
    cfg.titulo = titulo;
    cfg.mensagem = mensagem;
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.larguraCartao = 620;
    for (const auto& c : ficha.campos) {
        cfg.campos.push_back(c.def);
        if (iniciais != nullptr) {  // editar o questionário: as respostas guardadas voltam preenchidas
            const auto it = iniciais->find(c.def.id.toStdString());
            if (it != iniciais->end()) cfg.campos.back().valorInicial = juce::String::fromUTF8(it->second.c_str());
        }
    }
    cfg.botoesGrupo = ficha.botoesGrupo;
    if (aoVoltar) cfg.botoes.push_back({ t("imp.voltar"), 3, false, false });
    cfg.botoes.push_back({ t("light.ui.common.cancel"), 0, false, true });
    cfg.botoes.push_back({ t("imp.ficha.pular"), 2, false, false });
    cfg.botoes.push_back({ t("imp.ficha.continuar"), 1, true, false });
    // A ficha inteira (campos e regras) vive dentro do callback: nada aponta para fora dele.
    auto dono = std::make_shared<Ficha>(std::move(ficha));
    overlay.mostrar(cfg, [aoConcluir, dono, aoVoltar, folhaId](Resultado r) {
        if (r.botaoId == 3) { if (aoVoltar) aoVoltar(); return; }  // VOLTAR: reabre o card anterior, sem importar nada
        if (!aoConcluir) return;
        if (r.botaoId == 0) { aoConcluir(false, nullptr); return; }
        if (r.botaoId == 2) { aoConcluir(true, nullptr); return; }  // PULAR: segue só com o que o card já implica
        auto sobre = std::make_shared<SobreMaterial>();
        for (const auto& c : dono->campos)
            if (c.aplica) c.aplica(r, *sobre);
        if (dono->depois) dono->depois(r, *sobre);
        sobre->folhaId = folhaId;
        for (const auto& [k, v] : r.valores) if (v.trim().isNotEmpty()) sobre->respostas[k.toStdString()] = v.trim().toStdString();
        aoConcluir(true, sobre->vazio() && sobre->capaContainer.empty() && sobre->iconeContainer.empty() ? nullptr : sobre);
    });
}

Campo cNotas() { return cTexto("notas", "Notes", "Observações", "Anything worth remembering about this material", "Qualquer coisa que valha lembrar sobre este material"); }
Campo cContexto() { return cTexto("context", "Context / Additional Details", "Contexto / Detalhes adicionais"); }
Campo cIdentificacao() {
    return cTexto("media_id", "Card, CD or tape ID", "ID do cartão, CD ou fita", "Label, number or name written on it", "Etiqueta, número ou nome escrito nele");
}

// "Outro" e "Personalizado": o usuário diz do que se trata (vira o SUBJECT dos arquivos e entra nas notas).
bool ehOutro(const std::string& id) {
    return id == "other.custom" || (id.size() > 6 && id.compare(id.size() - 6, 6, ".other") == 0);
}

Campo cOutro() {
    return cTexto("outro", "What is this about?", "Do que se trata?", "Describe what you are importing — it becomes the SUBJECT",
                  "Descreva o que você está importando — vira o ASSUNTO (SUBJECT)",
                  [](SobreMaterial& s, const std::string& v) { somarAssunto(s, v); });
}

// ── VÍDEO ─────────────────────────────────────────────────────────────────────────────────────────────────

Ficha fichaVideo(bool comOutro) {
    // O formato de origem é pergunta da ficha, não um card: o mesmo material pode ter começado numa fita e estar hoje
    // num arquivo digital ou num link.
    auto formatos = formatosDoVocabulario({ "VIDEO", "OPTICAL", "FILM" }, true, { "MiniDisc (MD)", "Audio CD / CD-R / CD-RW" });
    Ficha f;
    if (comOutro) f.campos.push_back(cOutro());
    else f.campos.push_back(cAssunto("assunto", "What is in this video?", "O que há neste vídeo?"));
    f.campos.push_back(cFormato(formatos));
    f.campos.push_back(cNotas());
    return f;
}

// ── ÁUDIO ─────────────────────────────────────────────────────────────────────────────────────────────────

// Campos de um tipo do Mini (FichasSomDados.h), copiados para dados próprios da ficha.
Campos camposDoTipoMini(const std::string& tipoId, const std::set<std::string>& ignorar = {}) {
    static const std::set<std::string> idsDeAssunto = { "artist", "artist_album", "pack_name", "book_title", "project" };
    Campos out;
    for (const auto& tp : fs::tipos()) {
        if (tipoId != tp.id) continue;
        for (const auto& c : tp.campos) {
            const std::string id = c.id;
            if (ignorar.count(id)) continue;
            if (c.entrada == 1) {
                std::vector<Op> ops;
                for (const auto& o : c.opcoes) ops.push_back({ o.id, o.en, o.pt });
                std::function<void(SobreMaterial&, const Op&)> extra;
                if (id == "stage")  // etapa de produção -> campo de cada arquivo (REC / MIX / MSTR / FINAL)
                    extra = [](SobreMaterial& s, const Op& op) {
                        const char* codigo = op.id == "recording" ? "REC" : op.id == "mixing" ? "MIX" : op.id == "mastering" ? "MSTR" : op.id == "final" ? "FINAL" : "";
                        if (codigo[0] != 0) s.campos.push_back({ "etapa_producao", codigo });
                    };
                out.push_back(cEscolha(id, c.labelEn, c.labelPt, ops, true, extra, c.dicaEn, c.dicaPt));
            } else {
                // Metadata pós-questionário: produção musical e sample pack caem direto no SUBJECT / LIST / TAGS / NOTES.
                if (tipoId == "music_production" && id == "artist") {
                    out.push_back(cTexto(id, c.labelEn, c.labelPt, c.dicaEn, c.dicaPt, [](SobreMaterial& s, const std::string& v) { somarAssunto(s, v); }, false));
                    continue;
                }
                if (tipoId == "music_production" && id == "project") {
                    out.push_back(cTexto(id, c.labelEn, c.labelPt, c.dicaEn, c.dicaPt, definirListaENome, false));
                    continue;
                }
                if (tipoId == "sample_pack" && id == "pack_name") {
                    out.push_back(cTexto(id, c.labelEn, c.labelPt, c.dicaEn, c.dicaPt, definirLista, false));
                    continue;
                }
                if (tipoId == "music_production" && id == "description") {
                    out.push_back(cTexto(id, c.labelEn, c.labelPt, c.dicaEn, c.dicaPt, somarNotas, false, 3));
                    continue;
                }
                if (tipoId == "sample_pack" && id == "sound_type") {
                    out.push_back(cTexto(id, c.labelEn, c.labelPt, c.dicaEn, c.dicaPt, somarTags, false));
                    continue;
                }
                std::function<void(SobreMaterial&, const std::string&)> extra;
                if (idsDeAssunto.count(id))
                    extra = [](SobreMaterial& s, const std::string& v) { if (s.subject.empty()) s.subject = v; };
                out.push_back(cTexto(id, c.labelEn, c.labelPt, c.dicaEn, c.dicaPt, extra));
            }
        }
    }
    return out;
}

void juntar(Ficha& f, Campos cs) {
    for (auto& c : cs) f.campos.push_back(std::move(c));
}

const std::vector<std::string>& microfones() {
    static const std::vector<std::string> v = {
        "Sennheiser MKH 416", "Sennheiser MKH 418-S", "Binaural", "Contact Microphone", "Sennheiser MKH 8060", "Schoeps CMIT 5U",
        "DPA 4017B", "Sanken CS-3e", "Rode NTG3", "Neumann KMR 81i", "Sennheiser MKH 8040", "Sennheiser MKH 8020", "Schoeps CMC6 MK41",
        "Sennheiser MKH 30", "Sennheiser MKH 50", "DPA 4060", "LOM Usi Pro", "Sony PCM-D100 (integrated capsules)", "Audio-Technica BP4025",
        "Sanken CO-100K", "Sennheiser Ambeo VR Mic", "Jez Riley French JrF C-Series (hydrophone / contact)", "Line Audio CM4", "Other" };
    return v;
}

const std::vector<std::string>& gravadores() {
    static const std::vector<std::string> v = {
        "Sound Devices MixPre-3 II", "Sound Devices MixPre-6 II", "Sound Devices MixPre-10 II", "Sound Devices 833", "Sound Devices 888",
        "Sound Devices Scorpio", "Sound Devices 702T", "Sound Devices 744T", "Sound Devices 633", "Zoom F8n Pro", "Zoom F6", "Zoom F3",
        "Zoom H4n Pro", "Zoom H5", "Zoom H6", "Sony PCM-D100", "Sony PCM-A10", "Tascam Portacapture X8", "Tascam DR-100mkIII", "Nagra Seven", "Other" };
    return v;
}

std::vector<Op> opsDe(const std::vector<std::string>& nomes) {
    std::vector<Op> ops;
    for (const auto& n : nomes) ops.push_back({ n, n, n });
    return ops;
}

// Formatos de origem de uma gravação de campo (a lista é fixa; o que o vocabulário não tem vai como "Other" + nota).
std::vector<FormatoOp> formatosDeCampo() {
    return {
        { "Native Digital", "Digital nativo", "Native Digital", "" },
        { "PCM", "PCM", "Other", "PCM" },
        { "Tape", "Fita", "Other", "Tape" },
        { "Digital Tape", "Fita digital", "Other", "Digital Tape" },
        { "CD", "CD", "Audio CD / CD-R / CD-RW", "" },
        { "MD - Mini Disc", "MD - Mini Disc", "MiniDisc (MD)", "" },
        { "Smartphone", "Smartphone", "Internal Storage / Smartphone", "" },
        { "Camera", "Câmera", "Other", "Camera" },
    };
}

Ficha fichaAudio(const std::string& folha) {
    Ficha f;
    const auto formatoPadrao = [] { return cFormato(formatosDoVocabulario({ "AUDIO", "OPTICAL" })); };

    if (folha == "audio.production.project") {
        const std::vector<std::string> daws = { "Pro Tools", "Logic Pro", "Reaper", "Ableton Live", "FL Studio", "Audacity", "Other" };
        // A DAW usada vira o ícone do container (se não houver capa); "Other" não tem ícone próprio.
        f.campos.push_back(cEscolha("daw", "DAW used", "DAW usada", opsDe(daws), true, [](SobreMaterial& s, const Op& op) {
            static const std::map<std::string, std::string> icones = {
                { "Pro Tools", "ci_daw_pro_tools_png" }, { "Logic Pro", "ci_daw_logic_pro_png" }, { "Reaper", "ci_daw_reaper_png" },
                { "Ableton Live", "ci_daw_ableton_live_png" }, { "FL Studio", "ci_daw_fl_studio_png" }, { "Audacity", "ci_daw_audacity_png" } };
            const auto it = icones.find(op.en);
            if (it != icones.end()) s.iconeContainer = it->second;
        }));
        juntar(f, camposDoTipoMini("music_production", { "recording_medium" }));
        f.campos.push_back(formatoPadrao());
        juntarCapa(f);
    } else if (folha == "audio.production.samples") {
        juntar(f, camposDoTipoMini("sample_pack"));
        f.campos.push_back(formatoPadrao());
        f.campos.push_back(cIdentificacao());
        f.campos.push_back(cContexto());
        f.depois = [](const Resultado&, SobreMaterial& s) { somarAssunto(s, "Sample Pack"); };  // SUBJECT = Sample Pack
    } else if (folha == "audio.production.midi") {
        // Lote / pasta de MIDIs: coleção, categoria do conteúdo, ecossistema de destino, origem física e notas de roteamento.
        f.campos.push_back(cTexto("colecao", "Collection / Project Name", "Coleção / Nome do projeto",
                                  "e.g., Album Demos 2024, Drum Grooves Pack, Project X Stems", "ex.: Demos do Álbum 2024, Pacote de Grooves de Bateria, Stems do Projeto X",
                                  definirLista, false));
        f.campos.push_back(cEscolha("categoria", "Content Category", "Categoria de conteúdo",
                                    { { "full", "Full Song Arrangements (Multi-track)", "Arranjos completos de música (multipista)" },
                                      { "drums", "Drum Grooves / Percussive Loops", "Grooves de bateria / loops percussivos" },
                                      { "chords", "Chord Progressions / Harmonies", "Progressões de acordes / harmonias" },
                                      { "melodies", "Melodies / Basslines / Riffs", "Melodias / linhas de baixo / riffs" },
                                      { "controller", "Controller / Automation / SysEx Dumps", "Controlador / automação / dumps SysEx" },
                                      { "mixed", "Mixed Session Files (Various parts)", "Arquivos de sessão mistos (várias partes)" } }));
        f.campos.push_back(cTexto("ecossistema", "Target Ecosystem / Sound Sources", "Ecossistema de destino / Fontes sonoras",
                                  "e.g., Logic Pro Session, Superior Drummer 3, Roland Juno-106, General MIDI",
                                  "ex.: Sessão do Logic Pro, Superior Drummer 3, Roland Juno-106, General MIDI", somarTags, false));
        f.campos.push_back(cTexto("origem_hw", "Origin / Hardware Dump (optional)", "Origem / Dump de hardware (opcional)",
                                  "e.g., Akai MPC2000 Floppy #04, Old Mac Mini HD, Splice Pack",
                                  "ex.: Disquete #04 do Akai MPC2000, HD do Mac Mini antigo, pacote do Splice", somarTags, false));
        f.campos.push_back(cTexto("context", "Context / Routing Notes", "Contexto / Notas de roteamento",
                                  "e.g., Unquantized live takes, mapped for Addictive Drums, exported at 120 BPM.",
                                  "ex.: Takes ao vivo sem quantizar, mapeado para Addictive Drums, exportado a 120 BPM.", nullptr, true, 3));
        f.depois = [](const Resultado&, SobreMaterial& s) { somarAssunto(s, "Midi"); };  // SUBJECT = Midi
    } else if (folha == "audio.production.tools") {
        // Ferramentas: SUBJECT = tipo (Kind), LIST = nome/fabricante, função da ferramenta = tags; as notas de contexto são o campo grande.
        f.campos.push_back(cEscolha("asset_kind", "Kind", "Tipo",
                                    { { "instrument", "Virtual instrument", "Instrumento virtual" }, { "preset", "Preset", "Preset" },
                                      { "ir", "Impulse response (IR)", "Resposta ao impulso (IR)" }, { "plugin", "Plugin", "Plugin" }, { "other", "Other", "Outro" } },
                                    false, [](SobreMaterial& s, const Op& op) { somarAssunto(s, op.en); }));
        f.campos.push_back(cTexto("tool_function", "Tool Function", "Função da ferramenta", "e.g., Reverb, Synth, Sampler, EQ", "ex.: Reverb, Synth, Sampler, EQ",
                                  somarTags, false));
        f.campos.push_back(cTexto("asset_name", "Name or vendor", "Nome ou fabricante", "", "", definirLista, false));
        f.campos.push_back(cTexto("context", "Context Notes", "Notas de contexto", "", "", nullptr, true, 5));
    } else if (folha == "audio.preservation.ddp") {
        // DDP é a master que fabrica o CD industrial: a origem é sempre essa, então não se pergunta o formato.
        f.campos.push_back(cTexto("artist", "Artist", "Artista", "", "", [](SobreMaterial& s, const std::string& v) { somarAssunto(s, v); }, false));
        f.campos.push_back(cTexto("album", "Album", "Álbum", "Becomes the name of the container", "Vira o nome do container", definirListaENome, false));
        f.campos.push_back(cContexto());
        juntarCapa(f);  // capa do CD, igual Vinyl e Tape
    } else if (folha == "audio.preservation.vinyl") {
        f.campos.push_back(cTexto("artist", "Artist", "Artista", "", "", [](SobreMaterial& s, const std::string& v) { somarAssunto(s, v); }, false));
        f.campos.push_back(cTexto("album", "Album", "Álbum", "Becomes the name of the container", "Vira o nome do container", definirListaENome, false));
        f.campos.push_back(cEscolha("side", "Side", "Lado", { { "both", "Both", "Ambos" }, { "a", "Side A", "Lado A" }, { "b", "Side B", "Lado B" } }));
        const std::vector<Op> velocidades = { { "33", "33 1/3 RPM", "33 1/3 RPM" }, { "45", "45 RPM", "45 RPM" }, { "78", "78 RPM", "78 RPM" } };
        const std::vector<Op> tamanhos = { { "7", "7\"", "7\"" }, { "10", "10\"", "10\"" }, { "12", "12\"", "12\"" }, { "16", "16\"", "16\"" } };
        f.campos.push_back(cEscolha("speed", "Speed (RPM)", "Rotação (RPM)", velocidades, false));
        f.campos.push_back(cEscolha("condition", "Record condition", "Estado do disco", { { "good", "Good", "Bom" }, { "noisy", "Noisy", "Chiado" }, { "scratched", "Scratched", "Riscado" } }));
        f.campos.push_back(cEscolha("tamanho", "Disc size", "Tamanho do disco", tamanhos, false));
        f.campos.push_back(cContexto());
        // Tamanho e rotação formam o SOURCE MEDIUM do vinil (ex.: 12" Vinyl, 33 1/3 RPM).
        f.depois = [velocidades, tamanhos](const Resultado& r, SobreMaterial& s) {
            const auto escolhido = [&r](const std::string& campo, const std::vector<Op>& ops) -> const Op* {
                const std::string v = valorDe(r, campo);
                for (const auto& op : ops)
                    if (v == rot(op.en, op.pt).toStdString()) return &op;
                return nullptr;
            };
            const Op* tamanho = escolhido("tamanho", tamanhos);
            const Op* velocidade = escolhido("speed", velocidades);
            if (tamanho == nullptr && velocidade == nullptr) return;
            OriginalSourceMediumInfo info;
            info.medium = tamanho != nullptr ? tamanho->id + "\" Vinyl" : "Other";
            if (tamanho == nullptr) info.customNote = "Vinyl";
            if (velocidade != nullptr) info.speed = velocidade->en;
            s.campos.push_back({ "source_media", info.serialize() });
        };
        juntarCapa(f);
    } else if (folha == "audio.preservation.tape") {
        f.campos.push_back(cTexto("artist", "Artist", "Artista", "", "", [](SobreMaterial& s, const std::string& v) { somarAssunto(s, v); }, false));
        f.campos.push_back(cTexto("album", "Album or title", "Álbum ou título", "Becomes the name of the container", "Vira o nome do container", definirListaENome, false));
        f.campos.push_back(cEscolha("condition", "Tape condition", "Estado da fita", { { "good", "Good", "Boa" }, { "worn", "Worn", "Desgastada" }, { "damaged", "Damaged", "Danificada" } }));
        f.campos.push_back(cEscolha("noise_reduction", "Noise reduction", "Redução de ruído",
                                    { { "none", "None", "Nenhuma" }, { "dolby_b", "Dolby B", "Dolby B" }, { "dolby_c", "Dolby C", "Dolby C" }, { "dbx", "dbx", "dbx" } }));
        f.campos.push_back(cFormato(formatosPorNome({ "1\" Tape", "2\" Tape", "1/4\" Tape", "1/2\" Tape", "Cassette Tape", "8-Track Cartridge" })));
        f.campos.push_back(cTexto("media_id", "Tape Label", "Etiqueta da fita", "Label, number or name written on it", "Etiqueta, número ou nome escrito nela"));
        f.campos.push_back(cContexto());
        juntarCapa(f);
    } else if (folha == "audio.general.soundtrack") {
        juntar(f, camposDoTipoMini("soundtrack"));
        f.campos.push_back(formatoPadrao());
        f.campos.push_back(cIdentificacao());
        f.campos.push_back(cContexto());
    } else if (folha == "audio.general.film_sound") {
        f.campos.push_back(cTexto("project", "Film or project", "Filme ou projeto", "", "", [](SobreMaterial& s, const std::string& v) { if (s.subject.empty()) s.subject = v; }));
        f.campos.push_back(cEscolha("sound_kind", "Sound type", "Tipo de som",
                                    { { "dialogue", "Dialogue", "Diálogo" }, { "foley", "Foley", "Foley" }, { "ambience", "Ambience", "Ambiência" },
                                      { "sfx", "Sound effects", "Efeitos sonoros" }, { "adr", "ADR", "ADR" }, { "mix", "Mix / stems", "Mix / stems" } }));
        f.campos.push_back(cTexto("scene", "Scene or reel", "Cena ou rolo"));
        juntarGeo(f);
        f.campos.push_back(formatoPadrao());
        f.campos.push_back(cIdentificacao());
        f.campos.push_back(cContexto());
    } else if (folha == "audio.general.audiobook") {
        juntar(f, camposDoTipoMini("audiobook"));
        f.campos.push_back(formatoPadrao());
        f.campos.push_back(cIdentificacao());
        f.campos.push_back(cContexto());
    } else if (folha == "audio.general.field") {
        // Local, logo abaixo a geolocalização; depois data, o que foi gravado, equipamento (microfone e gravador), canais e formato.
        f.campos.push_back(cTexto("place", "Place", "Local"));
        juntarGeo(f);
        f.campos.push_back(cTexto("date_time", "Date and time", "Data e hora"));
        f.campos.push_back(cTexto("what_recorded", "What was recorded", "O que foi gravado"));
        f.campos.push_back(cEscolha("microfone", "Microphone setup", "Setup de microfone", opsDe(microfones()), false));
        f.campos.push_back(cEscolha("gravador", "Recorder", "Gravador", opsDe(gravadores()), false, [](SobreMaterial& s, const Op& op) {
            s.campos.push_back({ "recording_device", op.en });
        }));
        const juce::String grupoCanais = t("imp.ficha.canais");
        const std::vector<std::pair<std::string, std::string>> canais = { { "Mono", "Mono" }, { "Stereo", "Estéreo" }, { "Binaural", "Binaural" }, { "M/S", "M/S" }, { "Multitracks", "Multipistas" } };
        for (const auto& [en, pt] : canais) f.campos.push_back(cInterruptor("canal_" + en, en, pt, grupoCanais));
        f.campos.push_back(cFormato(formatosDeCampo()));
        f.campos.push_back(cTexto("log_notes", "Log Notes", "Notas de registro"));
        f.campos.push_back(cIdentificacao());
        f.campos.push_back(cContexto());
        f.depois = [canais](const Resultado& r, SobreMaterial& s) {
            std::string marcados;
            for (const auto& [en, pt] : canais)
                if (valorDe(r, "canal_" + en) == "1") marcados += (marcados.empty() ? "" : ", ") + en;
            linhaDeNota(s, "Channel configuration", marcados);
        };
    } else if (folha == "audio.general.voice_over") {
        juntar(f, camposDoTipoMini("voice_over"));
        // Locução: no lugar do formato de origem, a descrição do setup de microfone.
        f.campos.push_back(cTexto("mic_desc", "Microphone setup description", "Descrição do setup de microfone"));
        f.campos.push_back(cIdentificacao());
        f.campos.push_back(cContexto());
    } else if (folha == "audio.general.voice_notes") {
        juntar(f, camposDoTipoMini("voice_memo"));
        f.campos.push_back(formatoPadrao());
        f.campos.push_back(cIdentificacao());
        f.campos.push_back(cContexto());
    } else if (folha == "audio.general.other") {
        f.campos.push_back(cOutro());
        f.campos.push_back(formatoPadrao());
        f.campos.push_back(cIdentificacao());
        f.campos.push_back(cContexto());
    }
    return f;
}

bool folhaDeAudioTemFicha(const std::string& id) {
    static const std::set<std::string> folhas = {
        "audio.production.project", "audio.production.samples", "audio.production.midi", "audio.production.tools",
        "audio.preservation.ddp", "audio.preservation.vinyl", "audio.preservation.tape",
        "audio.general.soundtrack", "audio.general.film_sound", "audio.general.audiobook", "audio.general.field",
        "audio.general.voice_over", "audio.general.voice_notes", "audio.general.other" };
    return folhas.count(id) > 0;
}

// ── FOTOS, ARQUIVOS, DOCUMENTOS, MULTIMÍDIA, REDES, DESIGN ────────────────────────────────────────────────

Ficha fichaFotos(bool cartaoDeCamera) {
    Ficha f;
    f.campos.push_back(cAssunto("assunto", "What is in these photos?", "O que há nestas fotos?"));
    f.campos.push_back(cFormato(formatosDoVocabulario({ "IMAGE" })));
    if (cartaoDeCamera)
        f.campos.push_back(cTexto("cartao", "Card name / number", "Nome / número do cartão", "Written on the card, or the label you gave it",
                                  "Escrito no cartão, ou a etiqueta que você deu"));
    f.campos.push_back(cTexto("ano", "Year", "Ano", "Only fills files that carry no date of their own", "Só preenche arquivos que não têm data própria",
                              [](SobreMaterial& s, const std::string& v) { s.campos.push_back({ "ano", v }); }, false));
    f.campos.push_back(cNotas());
    juntarGeo(f);
    return f;
}

Ficha fichaArquivos(const std::string& folha) {
    auto formatos = folha == "files.optical" ? formatosDoVocabulario({ "OPTICAL" }, false, semOpticosRaros())
                                             : formatosDoVocabulario({ "SOLID STATE", "OPTICAL" }, true, semOpticosRaros());
    simplificarOpticos(formatos);
    Ficha f;
    f.campos.push_back(cAssunto("assunto", "What is inside?", "O que há dentro?"));
    f.campos.push_back(cFormato(formatos));
    if (folha != "files.computer_folder")
        f.campos.push_back(cTexto("rotulo", "Name or label on the drive / disc", "Nome ou etiqueta no drive / disco", "Written on it, or the name of the volume",
                                  "Escrito nele, ou o nome do volume"));
    f.campos.push_back(cNotas());
    return f;
}

Ficha fichaDocumentos(const std::string& folha) {
    const std::vector<Op> formatosDeDocumento = opsDe({ "PDF", "Word (.doc)", "Word (.docx)", "Text (.txt)", "RTF", "Excel (.xls)", "Excel (.xlsx)",
                                                        "PowerPoint (.ppt / .pptx)", "OpenDocument (.odt / .ods)", "Pages / Numbers / Keynote",
                                                        "Markdown", "HTML", "CSV", "EPUB", "Other" });
    Ficha f;
    if (folha == "documents.web")
        f.campos.push_back(cTexto("url", "Address (URL)", "Endereço (URL)", "Where the page or site lived, if known", "Onde a página ou o site ficava, se souber",
                                  [](SobreMaterial& s, const std::string& v) { s.campos.push_back({ "dc_source", v }); }));
    f.campos.push_back(cAssunto("assunto", "What are these documents?", "O que são estes documentos?"));
    f.campos.push_back(cEscolha("formato_doc", "Format", "Formato", formatosDeDocumento, true, [](SobreMaterial& s, const Op& op) {
        s.campos.push_back({ "dc_format", op.en });  // não sobrescreve o formato que o arquivo já traz
    }));
    if (folha == "documents.scanned") f.campos.push_back(cFormato(formatosDoVocabulario({ "DOCUMENT", "IMAGE" }, false)));
    f.campos.push_back(cNotas());
    return f;
}

Ficha fichaMultimidia(const std::string& folha) {
    Ficha f;
    f.campos.push_back(cAssunto("titulo", "Title / name", "Título / nome"));
    if (folha == "other.multimedia.interactive")
        f.campos.push_back(cEscolha("tipo", "Kind", "Tipo",
                                    { { "game", "Game", "Jogo" }, { "presentation", "Presentation", "Apresentação" }, { "elearning", "E-learning", "E-learning" },
                                      { "kiosk", "Kiosk or installation", "Quiosque ou instalação" }, { "other", "Other", "Outro" } }));
    f.campos.push_back(cTexto("plataforma", "Platform / system", "Plataforma / sistema", "e.g. Windows 98, Mac OS 9, DOS", "ex.: Windows 98, Mac OS 9, DOS"));
    f.campos.push_back(cFormato(formatosDoVocabulario({ "OPTICAL", "SOLID STATE" })));
    f.campos.push_back(cNotas());
    return f;
}

Ficha fichaRedeSocial(const std::string& folha) {
    static const std::map<std::string, std::string> redes = { { "other.social.instagram", "Instagram" }, { "other.social.facebook", "Facebook" },
                                                              { "other.social.tiktok", "TikTok" }, { "other.social.x", "Twitter / X" } };
    const auto rede = redes.find(folha);
    const std::string nomeDaRede = rede != redes.end() ? rede->second : std::string();
    Ficha f;
    if (nomeDaRede.empty())  // "Outra rede": o usuário diz qual
        f.campos.push_back(cTexto("rede", "Network name", "Nome da rede", "", "", [](SobreMaterial& s, const std::string& v) { somarAssunto(s, v); s.tags.push_back(v); }));
    f.campos.push_back(cTexto("usuario", "Account / username", "Conta / usuário", "", "", [](SobreMaterial& s, const std::string& v) { s.tags.push_back(v); }));
    f.campos.push_back(cTexto("link", "Link", "Link", "", "", [](SobreMaterial& s, const std::string& v) { s.campos.push_back({ "dc_source", v }); }));
    f.campos.push_back(cNotas());
    if (!nomeDaRede.empty()) f.depois = [nomeDaRede](const Resultado&, SobreMaterial& s) { somarAssunto(s, nomeDaRede); };
    return f;
}

Ficha fichaDesign() {
    Ficha f;
    f.campos.push_back(cAssunto("projeto", "Project name", "Nome do projeto"));
    f.campos.push_back(cEscolha("software", "Software", "Software",
                                opsDe({ "CorelDRAW", "Adobe InDesign", "Adobe Illustrator", "Adobe Photoshop", "AutoCAD", "3ds Max", "Maya", "Blender",
                                        "Cinema 4D", "SketchUp", "SolidWorks", "Other" })));
    f.campos.push_back(cEscolha("tipo", "Kind", "Tipo",
                                { { "2d", "2D graphic", "Gráfico 2D" }, { "layout", "Layout / print", "Diagramação / impressão" }, { "3d", "3D model", "Modelo 3D" },
                                  { "cad", "CAD / technical drawing", "CAD / desenho técnico" }, { "animation", "Animation", "Animação" }, { "other", "Other", "Outro" } }));
    f.campos.push_back(cNotas());
    return f;
}

// YouTube Production: os dados de UM vídeo (nome, categoria, descrição, miniatura, tags); a pasta de produção vem depois.
Ficha fichaYoutube() {
    Ficha f;
    // Nome e descrição vão para a ficha Dublin Core do arquivo de vídeo (dc_title, dc_description), não para as notas.
    // SUBJECT = categoria do vídeo; LIST = do que se trata (até 4 palavras).
    f.campos.push_back(cTexto("nome", "Video name", "Nome do vídeo", "", "",
                              [](SobreMaterial& s, const std::string& v) { s.camposDoVideo.push_back({ "dc_title", v }); s.nomeContainer = v; }, false));
    f.campos.push_back(cEscolha("categoria", "Category", "Categoria",
                                { { "vlog", "Vlog", "Vlog" }, { "short", "Short", "Short" }, { "tutorial", "Tutorial", "Tutorial" }, { "review", "Review", "Review" },
                                  { "interview", "Interview", "Entrevista" }, { "music", "Music video", "Videoclipe" }, { "live", "Livestream", "Transmissão ao vivo" },
                                  { "documentary", "Documentary", "Documentário" }, { "other", "Other", "Outro" } },
                                false, [](SobreMaterial& s, const Op& op) { somarAssunto(s, op.en); }));
    f.campos.push_back(cTexto("sobre", "What is this video about? (4 words max)", "Do que se trata o vídeo? (no máximo 4 palavras)", "", "",
                              [](SobreMaterial& s, const std::string& v) { definirLista(s, primeirasPalavras(v, 4)); }, false));
    f.campos.push_back(cTexto("descricao", "Description", "Descrição", "", "",
                              [](SobreMaterial& s, const std::string& v) { s.camposDoVideo.push_back({ "dc_description", v }); }, false, 4));
    f.campos.push_back(cTexto("tags", "Tags", "Tags", "Separated by commas", "Separadas por vírgula",
                              [](SobreMaterial& s, const std::string& v) {
                                  juce::StringArray partes;
                                  partes.addTokens(juce::String::fromUTF8(v.c_str()), ",;", "");
                                  for (auto& p : partes) if (p.trim().isNotEmpty()) s.tags.push_back(p.trim().toStdString());
                              }, false));
    return f;
}

Ficha fichaOutro() {
    Ficha f;
    f.campos.push_back(cOutro());
    f.campos.push_back(cNotas());
    return f;
}

} // namespace

std::shared_ptr<const SobreMaterial> contextoDaFolhaImportar(const std::string& id) {
    // Valores no vocabulário canônico (em inglês) do CONTENT e do SOURCE MEDIUM do Matriz. Só entra o que o card
    // afirma com certeza; o resto (formato, tamanho do vinil...) vem das fichas de cada categoria.
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
        { "other.design", "", "", { "Design project" } },
        { "videos.youtube", "YouTube Video", "", { "YouTube" } },
        { "other.social.instagram", "", "", { "Social media", "Instagram" } },
        { "other.social.facebook", "", "", { "Social media", "Facebook" } },
        { "other.social.tiktok", "", "", { "Social media", "TikTok" } },
        { "other.social.x", "", "", { "Social media", "Twitter / X" } },
        { "other.social.other", "", "", { "Social media" } },
    };
    for (const auto& l : tabela) {
        if (id != l.id) continue;
        auto sobre = std::make_shared<SobreMaterial>();
        sobre->folhaId = id;
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
        if (id == "videos.youtube") { sobre->containerOrigem = "youtube"; sobre->capaFrameDoVideo = true; }
        // NEST (tema em comum), não container: sample pack, gravações de campo e documentos importados juntos.
        if (id == "audio.production.samples" || id == "audio.general.field" || id.rfind("documents.", 0) == 0) sobre->agruparComoNest = true;
        if (id == "audio.preservation.vinyl") sobre->containerOrigem = "vinyl";
        if (id == "audio.preservation.tape") sobre->containerOrigem = "tape";
        return sobre;
    }
    return nullptr;
}

std::shared_ptr<const SobreMaterial> mesclarContextosImportar(std::shared_ptr<const SobreMaterial> base, std::shared_ptr<const SobreMaterial> extra) {
    if (extra == nullptr) return base;
    if (base == nullptr) return extra;
    auto r = std::make_shared<SobreMaterial>(*base);
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
    if (extra->agruparComoNest) r->agruparComoNest = true;
    if (extra->capaFrameDoVideo) r->capaFrameDoVideo = true;
    if (!extra->capaContainer.empty()) r->capaContainer = extra->capaContainer;
    if (!extra->iconeContainer.empty()) r->iconeContainer = extra->iconeContainer;
    if (!extra->geoPadrao.vazio()) r->geoPadrao = extra->geoPadrao;
    if (!extra->folhaId.empty()) r->folhaId = extra->folhaId;
    if (!extra->nomeContainer.empty()) r->nomeContainer = extra->nomeContainer;
    if (!extra->respostas.empty()) r->respostas = extra->respostas;
    return r;
}

void mostrarArquivosYoutube(PainelOverlay& overlay, const juce::String& aviso, std::function<void(int, ArquivosYoutube)> aoConcluir) {
    PainelOverlay::Config cfg;
    cfg.titulo = t("imp.ficha.youtube.arquivos.titulo");
    cfg.mensagem = aviso.isNotEmpty() ? aviso : t("imp.ficha.youtube.arquivos.msg");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.larguraCartao = 620;
    struct Slot { const char* id; const char* chaveGrupo; const char* chaveArquivo; const char* chaveBotao; const char* chaveTitulo; const char* filtro; };
    static const Slot slots[] = {
        { "video", "imp.ficha.youtube.video", "imp.ficha.youtube.video.arquivo", "imp.ficha.youtube.video.botao", "imp.ficha.youtube.video.titulo",
          "*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.mpg;*.mpeg;*.wmv;*.mts;*.m2ts;*.mxf;*.flv;*.3gp;*.ogv;*.vob" },
        { "thumb", "imp.ficha.thumb", "imp.ficha.thumb.arquivo", "imp.ficha.thumb.botao", "imp.ficha.thumb.titulo",
          "*.jpg;*.jpeg;*.png;*.tif;*.tiff;*.bmp;*.gif;*.webp;*.heic" },
        { "roteiro", "imp.ficha.youtube.roteiro", "imp.ficha.youtube.roteiro.arquivo", "imp.ficha.youtube.roteiro.botao", "imp.ficha.youtube.roteiro.titulo",
          "*.txt;*.md;*.rtf;*.doc;*.docx;*.odt;*.pdf;*.pages;*.xls;*.xlsx;*.csv" },
    };
    for (const auto& s : slots) {
        const juce::String grupo = t(s.chaveGrupo);
        cfg.campos.push_back({ s.id, t(s.chaveArquivo), "", "", {}, false, "", "", "", "", grupo });
        const juce::String idCampo = s.id, tituloSeletor = t(s.chaveTitulo), filtro = s.filtro;
        cfg.botoesGrupo.push_back({ grupo, t(s.chaveBotao), [idCampo, tituloSeletor, filtro](PainelOverlay& o) {
            auto seletor = std::make_shared<juce::FileChooser>(tituloSeletor, juce::File(), filtro);
            juce::Component::SafePointer<PainelOverlay> seguro(&o);
            seletor->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [seguro, seletor, idCampo](const juce::FileChooser& fc) {
                                     const auto escolhido = fc.getResult();
                                     if (seguro != nullptr && escolhido.existsAsFile()) seguro->definirValorCampo(idCampo, escolhido.getFullPathName());
                                 });
        } });
    }
    cfg.botoes = {
        { t("imp.voltar"), 3, false, false },
        { t("light.ui.common.cancel"), 0, false, true },
        { t("imp.ficha.continuar"), 1, true, false },
    };
    overlay.mostrar(cfg, [aoConcluir](PainelOverlay::Resultado r) {
        if (!aoConcluir) return;
        ArquivosYoutube a;
        const auto arquivo = [&r](const char* id) {
            const auto it = r.valores.find(id);
            return it == r.valores.end() || it->second.trim().isEmpty() ? juce::File() : juce::File(it->second.trim());
        };
        a.video = arquivo("video");
        a.thumbnail = arquivo("thumb");
        a.roteiro = arquivo("roteiro");
        aoConcluir(r.botaoId, a);
    });
}

void mostrarPerguntaOutroYoutube(PainelOverlay& overlay, const juce::String& nomeDoVideo, int totalCadastrados, std::function<void(int)> aoEscolher) {
    PainelOverlay::Config cfg;
    cfg.tom = PainelOverlay::Tom::Pergunta;
    cfg.titulo = t("imp.ficha.youtube.titulo");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.mensagem = t("imp.ficha.youtube.outro.msg").replace("{nome}", nomeDoVideo.isNotEmpty() ? nomeDoVideo : t("imp.ficha.youtube.sem_nome"))
                                                   .replace("{n}", juce::String(totalCadastrados));
    cfg.larguraCartao = 620;
    cfg.botoes = {
        { t("light.ui.common.cancel"), 0, false, true },
        { t("imp.ficha.youtube.outro"), 2, false, false },
        { t("imp.ficha.youtube.continuar"), 1, true, false },
    };
    overlay.mostrar(cfg, [aoEscolher](PainelOverlay::Resultado r) { if (aoEscolher) aoEscolher(r.botaoId); });
}

bool folhaImportarTemFicha(const std::string& id) {
    if (id == "videos.digital" || id == "videos.other" || id == "videos.youtube") return true;
    if (id.rfind("other.social.", 0) == 0 || id.rfind("other.multimedia.", 0) == 0) return id != "other.multimedia.cdrom";
    if (id == "other.design") return true;
    if (id == "files.computer_folder" || id == "files.external_drive" || id == "files.optical") return true;
    if (id == "photos.folder" || id == "photos.camera_card") return true;
    if (id == "documents.native" || id == "documents.scanned" || id == "documents.web") return true;
    if (folhaDeAudioTemFicha(id)) return true;
    return ehOutro(id);
}

namespace {

// A ficha de uma folha do IMPORT (campos, regras, título e mensagem). Usada ao mostrar a ficha e ao reconstruir o contexto de
// respostas antigas (editar o questionário).
Ficha montarFichaDaFolha(const std::string& id, juce::String& titulo, juce::String& mensagem) {
    mensagem = t("imp.ficha.msg.opcional");
    Ficha ficha;
    if (id.rfind("audio.", 0) == 0) {
        titulo = t("imp.ficha.audio.titulo") + juce::String::fromUTF8(" \xe2\x80\xba ") + t("imp.node." + id);
        if (id == "audio.preservation.ddp") mensagem = t("imp.ficha.ddp.msg");
        if (id == "audio.production.midi") mensagem = t("imp.ficha.midi.msg");
        ficha = fichaAudio(id);
    } else if (id == "videos.youtube") {
        titulo = t("imp.ficha.youtube.titulo");
        mensagem = t("imp.ficha.youtube.msg");
        ficha = fichaYoutube();
    } else if (id.rfind("videos.", 0) == 0) {
        titulo = t("imp.ficha.videos.titulo");
        mensagem = t("imp.ficha.videos.msg");
        ficha = fichaVideo(id == "videos.other");
    } else if (id.rfind("photos.", 0) == 0) {
        titulo = t("imp.ficha.fotos.titulo");
        ficha = fichaFotos(id == "photos.camera_card");
    } else if (id.rfind("documents.", 0) == 0) {
        titulo = t("imp.ficha.docs.titulo");
        ficha = fichaDocumentos(id);
    } else if (id.rfind("other.social.", 0) == 0) {
        titulo = t("imp.ficha.rede.titulo");
        ficha = fichaRedeSocial(id);
    } else if (id.rfind("other.multimedia.", 0) == 0) {
        titulo = t("imp.ficha.multimidia.titulo");
        ficha = fichaMultimidia(id);
    } else if (id == "other.design") {
        titulo = t("imp.ficha.design.titulo");
        ficha = fichaDesign();
    } else if (id.rfind("files.", 0) == 0 && id != "files.other") {
        titulo = t("imp.ficha.arquivos.titulo");
        ficha = fichaArquivos(id);
    } else {  // files.other, other.custom
        titulo = t("imp.ficha.outro.titulo");
        mensagem = t("imp.ficha.outro.msg");
        ficha = fichaOutro();
    }
    return ficha;
}

}  // namespace

void mostrarFichaImportar(PainelOverlay& overlay, const std::string& id, std::function<void(bool, std::shared_ptr<const SobreMaterial>)> aoConcluir,
                          std::function<void()> aoVoltar, const std::map<std::string, std::string>* respostasIniciais) {
    if (!folhaImportarTemFicha(id)) { if (aoConcluir) aoConcluir(true, nullptr); return; }
    juce::String titulo, mensagem;
    Ficha ficha = montarFichaDaFolha(id, titulo, mensagem);
    mostrarFicha(overlay, titulo, mensagem, std::move(ficha), std::move(aoConcluir), std::move(aoVoltar), id, respostasIniciais);
}

std::shared_ptr<const SobreMaterial> contextoDeRespostasImportar(const std::string& folha, const std::map<std::string, std::string>& respostas) {
    if (!folhaImportarTemFicha(folha)) return nullptr;
    juce::String titulo, mensagem;
    const Ficha ficha = montarFichaDaFolha(folha, titulo, mensagem);
    Resultado r;
    for (const auto& [k, v] : respostas) r.valores[juce::String(k)] = juce::String::fromUTF8(v.c_str());
    auto sobre = std::make_shared<SobreMaterial>();
    for (const auto& c : ficha.campos)
        if (c.aplica) c.aplica(r, *sobre);
    if (ficha.depois) ficha.depois(r, *sobre);
    sobre->folhaId = folha;
    sobre->respostas = respostas;
    return sobre;
}

std::string questionarioParaJson(const SobreMaterial& sobre) {
    if (sobre.folhaId.empty()) return {};
    auto* respostas = new juce::DynamicObject();
    for (const auto& [k, v] : sobre.respostas) respostas->setProperty(juce::Identifier(k.c_str()), juce::String::fromUTF8(v.c_str()));
    auto* raiz = new juce::DynamicObject();
    raiz->setProperty("folha", juce::String(sobre.folhaId));
    raiz->setProperty("respostas", juce::var(respostas));
    return juce::JSON::toString(juce::var(raiz), true).toStdString();
}

bool questionarioDeJson(const std::string& json, std::string& folha, std::map<std::string, std::string>& respostas) {
    const auto v = juce::JSON::parse(juce::String::fromUTF8(json.c_str()));
    if (!v.isObject()) return false;
    folha = v["folha"].toString().toStdString();
    if (auto* obj = v["respostas"].getDynamicObject())
        for (const auto& p : obj->getProperties()) respostas[p.name.toString().toStdString()] = p.value.toString().toStdString();
    return !folha.empty();
}

std::string folhaDaOrigemDoContainer(const std::string& origem) {
    if (origem == "production") return "audio.production.project";
    if (origem == "ddp") return "audio.preservation.ddp";
    if (origem == "vinyl") return "audio.preservation.vinyl";
    if (origem == "tape") return "audio.preservation.tape";
    if (origem == "youtube") return "videos.youtube";
    return {};
}

void editarQuestionarioDoContainer(PainelOverlay& overlay, ProjetoAberto& projeto, const std::string& nestId, const std::string& origem,
                                   std::function<void(bool)> aoFechar) {
    std::string folha;
    std::map<std::string, std::string> respostas;
    const std::string json = projeto.questionarioDoContainer(nestId);
    if (json.empty() || !questionarioDeJson(json, folha, respostas)) {  // container de antes desta função: ficha em branco da origem
        folha = folhaDaOrigemDoContainer(origem);
        respostas.clear();
    }
    if (folha.empty() || !folhaImportarTemFicha(folha)) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, t("imp.editar_questionario"), t("imp.editar_questionario.sem"));
        if (aoFechar) aoFechar(false);
        return;
    }
    // O que o card já implica (tags dele) não é resposta do usuário: não sai ao trocar as respostas.
    auto antigo = contextoDeRespostasImportar(folha, respostas);
    juce::Component::SafePointer<juce::Component> seguro(&overlay);
    ProjetoAberto* proj = &projeto;
    mostrarFichaImportar(overlay, folha,
        [proj, nestId, folha, antigo, aoFechar](bool continuar, std::shared_ptr<const SobreMaterial> novo) {
            if (!continuar || novo == nullptr) { if (aoFechar) aoFechar(false); return; }
            SobreMaterial tirar = antigo != nullptr ? *antigo : SobreMaterial{};
            if (const auto card = contextoDaFolhaImportar(folha)) {
                for (const auto& tag : card->tags)
                    tirar.tags.erase(std::remove_if(tirar.tags.begin(), tirar.tags.end(),
                                                    [&](const std::string& x) { return juce::String(x).equalsIgnoreCase(juce::String(tag)); }),
                                     tirar.tags.end());
            }
            const bool ok = proj->reaplicarQuestionarioDoContainer(nestId, tirar, *novo, questionarioParaJson(*novo));
            if (aoFechar) aoFechar(ok);
        },
        nullptr, &respostas);
}

} // namespace matriz::ui
