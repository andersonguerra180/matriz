#include "ExportadorTrecho.h"

#include <algorithm>

#include "../I18n/Strings.h"
#include "../Ingest/Checksum.h"
#include "../Whatsapp/WhatsappIngestao.h"
#include "PdfRaster.h"

namespace matriz::provadata {

using namespace matriz::whatsapp;
using matriz::i18n::t;

namespace {

constexpr int W = kPaginaLarguraPx, H = kPaginaAlturaPx;
constexpr int kMargem = 90, kTopo = 110, kRodape = 80;
constexpr float kCorpo = 25.0f, kPequeno = 20.0f, kMini = 17.0f, kAltLinha = 33.0f;
constexpr float kPad = 16.0f, kEntreBaloes = 12.0f;

juce::Font fonte(float altura, bool negrito = false, bool italico = false) {
    int estilo = juce::Font::plain;
    if (negrito) estilo |= juce::Font::bold;
    if (italico) estilo |= juce::Font::italic;
    return juce::Font(juce::FontOptions(altura, estilo));
}

float largura(const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth(f, s); }

bool ehEmojiOuModificador(juce::juce_wchar c) {
    return (c >= 0x1F000 && c <= 0x1FAFF) || (c >= 0x2600 && c <= 0x27BF) || (c >= 0x2B00 && c <= 0x2BFF) || (c >= 0x2300 && c <= 0x23FF) ||
           c == 0xFE0F || c == 0x200D || c == 0x20E3 || (c >= 0xE0020 && c <= 0xE007F) || c == 0x00A9 || c == 0x00AE || c == 0x203C || c == 0x2049 ||
           (c >= 0x2190 && c <= 0x21FF && false);
}

// Quebra em linhas que cabem em `max` pixels (respeita \n; palavra maior que a linha é cortada).
std::vector<juce::String> quebrar(const juce::String& texto, const juce::Font& f, float max) {
    std::vector<juce::String> saida;
    juce::StringArray paragrafos;
    paragrafos.addLines(texto);
    if (paragrafos.isEmpty()) paragrafos.add({});
    for (const auto& par : paragrafos) {
        juce::StringArray palavras;
        palavras.addTokens(par, " ", "");
        juce::String linha;
        for (auto palavra : palavras) {
            while (largura(f, palavra) > max && palavra.length() > 1) {  // palavra gigante
                int n = palavra.length();
                while (n > 1 && largura(f, palavra.substring(0, n)) > max) --n;
                if (linha.isNotEmpty()) { saida.push_back(linha); linha.clear(); }
                saida.push_back(palavra.substring(0, n));
                palavra = palavra.substring(n);
            }
            const auto teste = linha.isEmpty() ? palavra : linha + " " + palavra;
            if (linha.isNotEmpty() && largura(f, teste) > max) { saida.push_back(linha); linha = palavra; }
            else linha = teste;
        }
        saida.push_back(linha);
    }
    return saida;
}

juce::Colour corDoAutor(const juce::String& autor) {
    static const juce::uint32 paleta[] = {0xff128c7e, 0xff1d6fa5, 0xffb45309, 0xff6d28d9, 0xffbe185d, 0xff047857, 0xffc2410c, 0xff1d4ed8};
    return juce::Colour(paleta[static_cast<size_t>(std::abs(autor.toLowerCase().hashCode())) % 8]);
}

juce::String dataBr(const Mensagem& m) {
    return juce::String::formatted("%02d/%02d/%04d", m.dia, m.mes, m.ano);
}
juce::String dataHoraBr(const Mensagem& m) { return dataBr(m) + juce::String::formatted(" %02d:%02d", m.hora, m.minuto); }
juce::String agoraBr() {
    const auto n = juce::Time::getCurrentTime();
    return n.formatted("%d/%m/%Y %H:%M");
}

struct Peca {
    enum class Tipo { Texto, Imagem } tipo = Tipo::Texto;
    std::vector<juce::String> linhas;
    juce::Font f = fonte(kCorpo);
    juce::Colour cor = juce::Colour(0xff111111);
    juce::Image img;
    float altura() const { return tipo == Tipo::Imagem ? static_cast<float>(img.getHeight()) + 6.0f : static_cast<float>(linhas.size()) * (f.getHeight() * 1.32f); }
    float alturaLinha() const { return f.getHeight() * 1.32f; }
};

class Pintor {
public:
    Pintor() { novaPagina(); }
    void novaPagina() {
        paginas.emplace_back(juce::Image::RGB, W, H, true);
        g = std::make_unique<juce::Graphics>(paginas.back());
        g->fillAll(juce::Colours::white);
        y = static_cast<float>(kTopo);
    }
    float restante() const { return static_cast<float>(H - kRodape) - y; }
    bool paginaNova() const { return y <= static_cast<float>(kTopo) + 0.5f; }
    std::vector<juce::Image> paginas;
    std::unique_ptr<juce::Graphics> g;
    float y = 0;
};

struct Ctx {
    const EntradaTrecho& e;
    const OpcoesTrecho& o;
    Pintor p;
    juce::String sha;
    juce::String exportadoEm;
    EstadoProva estadoOriginal;
    bool temManifesto = false;
    juce::var manifesto;
    int primeiraPaginaDeMensagens = 1;
    Ctx(const EntradaTrecho& en, const OpcoesTrecho& op) : e(en), o(op) {}
};

void linhaCentralizada(Ctx& c, const juce::String& texto, const juce::Font& f, juce::Colour cor, float espacoDepois = 6.0f) {
    for (const auto& l : quebrar(texto, f, static_cast<float>(W - 2 * kMargem))) {
        if (c.p.restante() < f.getHeight() * 1.4f) c.p.novaPagina();
        c.p.g->setFont(f);
        c.p.g->setColour(cor);
        c.p.g->drawText(l, kMargem, juce::roundToInt(c.p.y), W - 2 * kMargem, juce::roundToInt(f.getHeight() * 1.4f), juce::Justification::centred, false);
        c.p.y += f.getHeight() * 1.4f;
    }
    c.p.y += espacoDepois;
}

void paragrafo(Ctx& c, const juce::String& texto, const juce::Font& f, juce::Colour cor, float recuo = 0.0f, float espacoDepois = 8.0f) {
    for (const auto& l : quebrar(texto, f, static_cast<float>(W - 2 * kMargem) - recuo)) {
        if (c.p.restante() < f.getHeight() * 1.4f) c.p.novaPagina();
        c.p.g->setFont(f);
        c.p.g->setColour(cor);
        c.p.g->drawText(l, kMargem + juce::roundToInt(recuo), juce::roundToInt(c.p.y), W - 2 * kMargem - juce::roundToInt(recuo), juce::roundToInt(f.getHeight() * 1.4f),
                        juce::Justification::centredLeft, false);
        c.p.y += f.getHeight() * 1.4f;
    }
    c.p.y += espacoDepois;
}

void desenharBalao(Ctx& c, bool meu, const juce::String& autor, std::vector<Peca> pecas, const juce::String& hora) {
    const float larguraMaxBalao = static_cast<float>(W - 2 * kMargem) * 0.72f;
    const float larguraMaxTexto = larguraMaxBalao - 2 * kPad;
    const auto fHora = fonte(kMini);
    const auto fAutor = fonte(kPequeno, true);
    bool primeiro = true;
    while (!pecas.empty()) {
        const float cabecalho = (primeiro && autor.isNotEmpty()) ? kPequeno * 1.5f : 0.0f;
        const float espacoHora = fHora.getHeight() * 1.5f;
        const float disp = c.p.restante() - 2 * kPad - kEntreBaloes;
        std::vector<Peca> nesta;
        float usado = cabecalho;
        while (!pecas.empty()) {
            auto& pc = pecas.front();
            const float h = pc.altura();
            const bool ehUltimaPeca = pecas.size() == 1;
            if (usado + h + (ehUltimaPeca ? espacoHora : 0.0f) <= disp) {
                nesta.push_back(pc);
                usado += h;
                pecas.erase(pecas.begin());
                continue;
            }
            if (pc.tipo == Peca::Tipo::Texto && pc.linhas.size() > 1) {
                const int cabem = static_cast<int>((disp - usado) / pc.alturaLinha());
                if (cabem >= 1 && cabem < static_cast<int>(pc.linhas.size())) {
                    Peca parte = pc;
                    parte.linhas.assign(pc.linhas.begin(), pc.linhas.begin() + cabem);
                    pc.linhas.erase(pc.linhas.begin(), pc.linhas.begin() + cabem);
                    usado += parte.altura();
                    nesta.push_back(std::move(parte));
                }
            }
            break;
        }
        if (nesta.empty()) {
            if (c.p.paginaNova()) {  // nada cabe nem numa página vazia: descarta a peça para não travar
                pecas.erase(pecas.begin());
                continue;
            }
            c.p.novaPagina();
            continue;
        }
        const bool ultimo = pecas.empty();
        if (ultimo) usado += espacoHora;

        float larguraConteudo = hora.isNotEmpty() ? largura(fHora, hora) : 0.0f;
        if (cabecalho > 0) larguraConteudo = juce::jmax(larguraConteudo, largura(fAutor, autor));
        for (const auto& pc : nesta) {
            if (pc.tipo == Peca::Tipo::Imagem) larguraConteudo = juce::jmax(larguraConteudo, static_cast<float>(pc.img.getWidth()));
            else for (const auto& l : pc.linhas) larguraConteudo = juce::jmax(larguraConteudo, largura(pc.f, l));
        }
        const float lb = juce::jlimit(150.0f, larguraMaxBalao, larguraConteudo + 2 * kPad + 4.0f);
        const float hb = usado + 2 * kPad;
        const float x = meu ? static_cast<float>(W - kMargem) - lb : static_cast<float>(kMargem);
        auto& g = *c.p.g;
        juce::Rectangle<float> caixa(x, c.p.y, lb, hb);
        g.setColour(meu ? juce::Colour(0xffdcf8c6) : juce::Colour(0xfff0f0f0));
        g.fillRoundedRectangle(caixa, 14.0f);
        g.setColour(juce::Colour(0xffd0d0d0));
        g.drawRoundedRectangle(caixa, 14.0f, 1.0f);
        float yy = c.p.y + kPad;
        if (cabecalho > 0) {
            g.setFont(fAutor);
            g.setColour(corDoAutor(autor));
            g.drawText(autor, juce::roundToInt(x + kPad), juce::roundToInt(yy), juce::roundToInt(lb - 2 * kPad), juce::roundToInt(kPequeno * 1.4f), juce::Justification::centredLeft, true);
            yy += cabecalho;
        }
        for (const auto& pc : nesta) {
            if (pc.tipo == Peca::Tipo::Imagem) {
                g.drawImageAt(pc.img, juce::roundToInt(x + kPad), juce::roundToInt(yy));
                yy += pc.altura();
            } else {
                g.setFont(pc.f);
                g.setColour(pc.cor);
                for (const auto& l : pc.linhas) {
                    g.drawText(l, juce::roundToInt(x + kPad), juce::roundToInt(yy), juce::roundToInt(lb - 2 * kPad) + 4, juce::roundToInt(pc.alturaLinha()), juce::Justification::centredLeft, false);
                    yy += pc.alturaLinha();
                }
            }
        }
        if (ultimo && hora.isNotEmpty()) {
            g.setFont(fHora);
            g.setColour(juce::Colour(0xff666666));
            g.drawText(hora, juce::roundToInt(x + kPad), juce::roundToInt(c.p.y + hb - kPad - espacoHora), juce::roundToInt(lb - 2 * kPad), juce::roundToInt(espacoHora), juce::Justification::centredRight, false);
        }
        c.p.y += hb + kEntreBaloes;
        primeiro = false;
        if (!ultimo) c.p.novaPagina();
    }
}

juce::Image miniaturaDe(const juce::File& arquivo, int larguraMax, int alturaMax) {
    auto img = juce::ImageFileFormat::loadFrom(arquivo);
    if (!img.isValid()) return {};
    const double k = juce::jmin(1.0, juce::jmin(static_cast<double>(larguraMax) / img.getWidth(), static_cast<double>(alturaMax) / img.getHeight()));
    if (k >= 1.0) return img;
    return img.rescaled(juce::jmax(1, juce::roundToInt(img.getWidth() * k)), juce::jmax(1, juce::roundToInt(img.getHeight() * k)), juce::Graphics::highResamplingQuality);
}

void capa(Ctx& c) {
    auto& e = c.e;
    c.p.y = 250;
    linhaCentralizada(c, tituloDaConversa(e.contato, e.portugues), fonte(54.0f, true), juce::Colour(0xff111111), 12);
    linhaCentralizada(c, t("light.trecho.cover.excerpt"), fonte(30.0f), juce::Colour(0xff555555), 40);
    const auto& primeira = e.conversa->mensagens[static_cast<size_t>(e.primeiraMensagem)];
    const auto& ultima = e.conversa->mensagens[static_cast<size_t>(e.ultimaMensagem)];
    const int n = e.ultimaMensagem - e.primeiraMensagem + 1;
    linhaCentralizada(c, t("light.trecho.cover.period").replace("{a}", dataHoraBr(primeira)).replace("{b}", dataHoraBr(ultima)), fonte(kCorpo + 3), juce::Colour(0xff111111), 6);
    linhaCentralizada(c, t("light.trecho.cover.count").replace("{n}", juce::String(n)), fonte(kCorpo + 3), juce::Colour(0xff111111), 6);
    juce::StringArray quem;
    for (const auto& a : e.conversa->autores) quem.add(a);
    if (quem.size() > 0) linhaCentralizada(c, t("light.trecho.cover.participants").replace("{list}", quem.joinIntoString(", ")), fonte(kCorpo), juce::Colour(0xff333333), 40);
    linhaCentralizada(c, t("light.trecho.cover.original").replace("{name}", e.nomeOriginal), fonte(kCorpo), juce::Colour(0xff333333), 6);
    linhaCentralizada(c, t("light.trecho.cover.hash"), fonte(kPequeno, true), juce::Colour(0xff333333), 2);
    linhaCentralizada(c, c.sha.substring(0, 32), fonte(kPequeno + 1, false), juce::Colour(0xff333333), 0);
    linhaCentralizada(c, c.sha.substring(32), fonte(kPequeno + 1, false), juce::Colour(0xff333333), 30);
    linhaCentralizada(c, t("light.trecho.cover.exported").replace("{dt}", c.exportadoEm), fonte(kPequeno), juce::Colour(0xff555555), 6);
    if (c.estadoOriginal.estado != Estado::Nenhum)
        linhaCentralizada(c, t("light.prova.details.line").replace("{state}", textoEstado(c.estadoOriginal)), fonte(kPequeno), juce::Colour(0xff555555), 6);
}

juce::String nomeDaClasse(ClasseAnexo k) {
    switch (k) {
        case ClasseAnexo::Foto: return t("light.trecho.kind.photo");
        case ClasseAnexo::Video: return t("light.trecho.kind.video");
        case ClasseAnexo::Audio: return t("light.trecho.kind.audio");
        case ClasseAnexo::Figurinha: return t("light.trecho.kind.sticker");
        default: return t("light.trecho.kind.document");
    }
}

void mensagens(Ctx& c) {
    auto& e = c.e;
    c.p.novaPagina();
    int diaAnterior = 0;
    const float larguraMaxTexto = static_cast<float>(W - 2 * kMargem) * 0.72f - 2 * kPad;
    for (int i = e.primeiraMensagem; i <= e.ultimaMensagem; ++i) {
        if (e.cancelado && e.cancelado()) return;
        const auto& m = e.conversa->mensagens[static_cast<size_t>(i)];
        const int dia = m.ano * 10000 + m.mes * 100 + m.dia;
        if (dia != diaAnterior) {
            diaAnterior = dia;
            if (c.p.restante() < 90) c.p.novaPagina();
            auto& g = *c.p.g;
            const auto rotulo = nomeDoDia(m.ano, m.mes, m.dia, e.portugues);
            const auto f = fonte(kPequeno, true);
            const float w = largura(f, rotulo) + 44.0f;
            juce::Rectangle<float> pil(0, c.p.y + 8, w, 36.0f);
            pil = pil.withCentre({static_cast<float>(W) / 2.0f, pil.getCentreY()});
            g.setColour(juce::Colour(0xffe5e7eb));
            g.fillRoundedRectangle(pil, 18.0f);
            g.setColour(juce::Colour(0xff374151));
            g.setFont(f);
            g.drawText(rotulo, pil.toNearestInt(), juce::Justification::centred);
            c.p.y += 62;
        }
        const bool meu = e.voce.isNotEmpty() && m.autor == e.voce;
        const auto hora = juce::String::formatted("%02d:%02d", m.hora, m.minuto);
        const auto texto = trocarEmojisPorMarcador(m.texto);

        const auto textoPeca = [&](const juce::String& s, bool italico = false, juce::Colour cor = juce::Colour(0xff111111), float tam = kCorpo) {
            Peca p;
            p.f = fonte(tam, false, italico);
            p.cor = cor;
            p.linhas = quebrar(s, p.f, larguraMaxTexto);
            return p;
        };

        switch (m.tipo) {
            case TipoMensagem::Sistema:
                linhaCentralizada(c, texto, fonte(kPequeno, false, true), juce::Colour(0xff6b7280), 10);
                break;
            case TipoMensagem::Texto:
                desenharBalao(c, meu, m.autor, {textoPeca(texto)}, hora);
                break;
            case TipoMensagem::MidiaOculta:
                desenharBalao(c, meu, m.autor, {textoPeca(t("light.trecho.hidden"), true, juce::Colour(0xff6b7280))}, hora);
                break;
            case TipoMensagem::LigacaoVoz:
                desenharBalao(c, meu, m.autor, {textoPeca(t("light.trecho.voiceCall"), true, juce::Colour(0xff6b7280))}, hora);
                break;
            case TipoMensagem::LigacaoVideo:
                desenharBalao(c, meu, m.autor, {textoPeca(t("light.trecho.videoCall"), true, juce::Colour(0xff6b7280))}, hora);
                break;
            case TipoMensagem::SemConteudo:
                desenharBalao(c, meu, m.autor, {textoPeca(t("light.trecho.empty"), true, juce::Colour(0xff6b7280))}, hora);
                break;
            case TipoMensagem::Apagada:
                desenharBalao(c, meu, m.autor, {textoPeca(t("light.trecho.deleted"), true, juce::Colour(0xff6b7280))}, hora);
                break;
            case TipoMensagem::Anexo: {
                std::vector<Peca> pecas;
                const auto classe = classeDoAnexo(m.anexo);
                MidiaDoTrecho md;
                if (e.resolverMidia) md = e.resolverMidia(m.anexo);
                if (classe == ClasseAnexo::Foto && c.o.miniaturasDeFotos && md.arquivo.existsAsFile()) {
                    Peca im;
                    im.tipo = Peca::Tipo::Imagem;
                    im.img = miniaturaDe(md.arquivo, 420, 360);
                    if (im.img.isValid()) pecas.push_back(std::move(im));
                }
                pecas.push_back(textoPeca(nomeDaClasse(classe) + ": " + m.anexo, false, juce::Colour(0xff111111), kPequeno + 1));
                if (md.arquivo.existsAsFile() || md.sha256.isNotEmpty()) {
                    const auto sha = md.sha256.isNotEmpty() ? md.sha256 : sha256Hexa(md.arquivo);
                    pecas.push_back(textoPeca(t("light.trecho.sha").replace("{h}", sha), false, juce::Colour(0xff374151), kMini));
                } else {
                    pecas.push_back(textoPeca(t("light.trecho.media.missing"), true, juce::Colour(0xff6b7280), kMini));
                }
                const bool comMiniatura = !pecas.empty() && pecas.front().tipo == Peca::Tipo::Imagem;
                if (!comMiniatura) pecas.push_back(textoPeca(t("light.trecho.media.notIncluded"), true, juce::Colour(0xff6b7280), kMini));
                if (m.legenda.isNotEmpty()) pecas.push_back(textoPeca(trocarEmojisPorMarcador(m.legenda)));
                desenharBalao(c, meu, m.autor, std::move(pecas), hora);
                break;
            }
        }
    }
}

void provaFinal(Ctx& c) {
    auto& e = c.e;
    c.p.novaPagina();
    paragrafo(c, t("light.trecho.proof.title"), fonte(40.0f, true), juce::Colour(0xff111111), 0, 16);
    paragrafo(c, t("light.trecho.cover.original").replace("{name}", e.nomeOriginal), fonte(kCorpo), juce::Colour(0xff111111));
    paragrafo(c, t("light.trecho.cover.hash") + " " + c.sha, fonte(kPequeno + 1), juce::Colour(0xff111111), 0, 16);
    if (c.estadoOriginal.estado != Estado::Nenhum)
        paragrafo(c, t("light.trecho.proof.original").replace("{state}", textoEstado(c.estadoOriginal)), fonte(kCorpo), juce::Colour(0xff111111), 0, 16);
    else
        paragrafo(c, t("light.trecho.proof.none"), fonte(kCorpo), juce::Colour(0xff555555), 0, 16);
    if (c.o.incluirDadosDoComputador && c.manifesto.isObject() && c.manifesto.hasProperty("computador")) {
        paragrafo(c, t("light.trecho.proof.computer"), fonte(kCorpo, true), juce::Colour(0xff111111), 0, 4);
        if (auto* o = c.manifesto["computador"].getDynamicObject()) {
            for (const auto& p : o->getProperties()) {
                if (p.name.toString() == "nota") continue;
                paragrafo(c, p.name.toString() + ": " + p.value.toString(), fonte(kPequeno + 1), juce::Colour(0xff111111), 24, 2);
            }
            paragrafo(c, o->getProperty("nota").toString(), fonte(kMini, false, true), juce::Colour(0xff555555), 24, 16);
        }
    }
    paragrafo(c, t("light.trecho.proof.how"), fonte(kCorpo, true), juce::Colour(0xff111111), 0, 6);
    paragrafo(c, t("light.trecho.howToVerify.pdf"), fonte(kPequeno + 1), juce::Colour(0xff222222), 0, 4);
}

void rodapes(Ctx& c) {
    const int total = static_cast<int>(c.p.paginas.size());
    for (int i = 0; i < total; ++i) {
        juce::Graphics g(c.p.paginas[static_cast<size_t>(i)]);
        g.setColour(juce::Colour(0xffd1d5db));
        g.fillRect(kMargem, H - kRodape + 6, W - 2 * kMargem, 1);
        g.setColour(juce::Colour(0xff4b5563));
        g.setFont(fonte(kMini));
        auto texto = t("light.trecho.footer").replace("{p}", juce::String(i + 1)).replace("{n}", juce::String(total)).replace("{dt}", c.exportadoEm).replace("{h}", c.sha.substring(0, 16));
        g.drawText(texto, kMargem, H - kRodape + 14, W - 2 * kMargem, 40, juce::Justification::centred, true);
    }
}

} // namespace

juce::String trocarEmojisPorMarcador(const juce::String& texto) {
    juce::String saida;
    bool emSequencia = false;
    for (auto p = texto.getCharPointer(); !p.isEmpty();) {
        const juce::juce_wchar c = p.getAndAdvance();
        if (ehEmojiOuModificador(c) || (c >= 0x1F3FB && c <= 0x1F3FF)) {
            if (!emSequencia) saida += "[emoji]";
            emSequencia = true;
        } else {
            saida += juce::String::charToString(c);
            emSequencia = false;
        }
    }
    return saida;
}

ResultadoTrecho exportarTrecho(const EntradaTrecho& entrada, const OpcoesTrecho& opcoes, bool usarRede) {
    ResultadoTrecho r;
    if (entrada.conversa == nullptr || entrada.primeiraMensagem < 0 || entrada.ultimaMensagem < entrada.primeiraMensagem ||
        entrada.ultimaMensagem >= static_cast<int>(entrada.conversa->mensagens.size())) {
        r.erro = t("light.trecho.err.range");
        return r;
    }
    try {
        Ctx c(entrada, opcoes);
        c.sha = entrada.shaOriginal.isNotEmpty() ? entrada.shaOriginal : sha256Hexa(entrada.arquivoOriginal);
        c.exportadoEm = agoraBr();
        if (entrada.manifestoDoOriginal.existsAsFile()) {
            c.temManifesto = true;
            c.manifesto = juce::JSON::parse(entrada.manifestoDoOriginal);
            c.estadoOriginal = estadoDoCarimbo(entrada.manifestoDoOriginal);
        }

        capa(c);
        mensagens(c);
        if (entrada.cancelado && entrada.cancelado()) { r.erro = t("light.trecho.err.cancelled"); return r; }
        provaFinal(c);
        rodapes(c);

        const auto nomePasta = juce::File::createLegalFileName(t("light.trecho.folder").replace("{name}", entrada.contato).replace("{dt}", juce::Time::getCurrentTime().formatted("%Y-%m-%d %H.%M")));
        auto pasta = opcoes.pastaDestino.getChildFile(nomePasta);
        for (int i = 2; pasta.exists(); ++i) pasta = opcoes.pastaDestino.getChildFile(nomePasta + " (" + juce::String(i) + ")");
        if (!pasta.createDirectory()) { r.erro = t("light.prova.err.write"); return r; }
        r.pasta = pasta;
        r.pdf = pasta.getChildFile("Trecho.pdf");
        r.paginas = static_cast<int>(c.p.paginas.size());
        if (!escreverPdfDePaginas(c.p.paginas, r.pdf, tituloDaConversa(entrada.contato, entrada.portugues), "BKR Matriz Mini")) {
            r.erro = t("light.prova.err.write");
            return r;
        }

        // Registro do trecho: o PDF e o original, com o hash de cada um.
        std::vector<EntradaManifesto> arquivos;
        arquivos.push_back({"Trecho.pdf", sha256Hexa(r.pdf)});
        arquivos.push_back({entrada.nomeOriginal, c.sha});
        const auto& primeira = entrada.conversa->mensagens[static_cast<size_t>(entrada.primeiraMensagem)];
        const auto& ultima = entrada.conversa->mensagens[static_cast<size_t>(entrada.ultimaMensagem)];
        auto manifesto = montarManifesto("whatsapp-trecho", arquivos, OpcoesComputador{}, false,
                                         t("light.trecho.note").replace("{n}", juce::String(entrada.ultimaMensagem - entrada.primeiraMensagem + 1))
                                             .replace("{a}", dataHoraBr(primeira)).replace("{b}", dataHoraBr(ultima)));
        if (opcoes.incluirDadosDoComputador && c.manifesto.isObject() && c.manifesto.hasProperty("computador"))
            if (auto* o = manifesto.getDynamicObject()) o->setProperty("computador", c.manifesto["computador"]);
        if (!gravarManifestoECarimbar(pasta.getChildFile("registro.json"), manifesto, usarRede)) { r.erro = t("light.prova.err.write"); return r; }
        carimbarArquivo(r.pdf, usarRede);
        r.estadoCarimboPdf = estadoDoCarimbo(r.pdf);
        if (opcoes.incluirArquivoOriginal && entrada.arquivoOriginal.existsAsFile())
            entrada.arquivoOriginal.copyFileTo(pasta.getChildFile(entrada.nomeOriginal.isNotEmpty() ? entrada.nomeOriginal : entrada.arquivoOriginal.getFileName()));
        pasta.getChildFile(t("light.prova.howToVerify.fileName")).replaceWithText(t("light.trecho.howToVerify.file"));
        registrarPastaParaAtualizacaoExtra(pasta);
        r.ok = true;
    } catch (const std::exception& ex) {
        r.erro = ex.what();
    }
    return r;
}

} // namespace matriz::provadata
