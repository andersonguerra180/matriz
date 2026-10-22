#include "PainelMetadadosArquivo.h"

#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../Analytics/AssetGeolocation.h"
#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"

#include <algorithm>

namespace matriz::ui {

namespace {

juce::String duracaoHumana(double segundos) {
    if (segundos < 0) return {};
    const auto total = static_cast<juce::int64>(segundos + 0.5);
    const auto h = total / 3600, m = (total / 60) % 60, s = total % 60;
    return h > 0 ? juce::String(h) + ":" + juce::String(m).paddedLeft('0', 2) + ":" + juce::String(s).paddedLeft('0', 2)
                 : juce::String(m) + ":" + juce::String(s).paddedLeft('0', 2);
}

juce::String bytesHumano(juce::int64 b) {
    if (b < 0) return {};
    return juce::File::descriptionOfSizeInBytes(b) + " (" + juce::String(b) + " bytes)";
}

juce::String txt(const std::optional<std::string>& v) {
    return v ? juce::String::fromUTF8(v->c_str()).trim() : juce::String();
}

}  // namespace

// ---------------------------------------------------------------------------
// Leitura (thread de fundo)
// ---------------------------------------------------------------------------
std::vector<PainelMetadadosArquivo::Card> PainelMetadadosArquivo::lerCards(ProjetoAberto& p, const std::string& itemId) {
    Card arquivo{"FILE", {}}, tecnico{"TECHNICAL", {}}, camera{"CAMERA / EXIF", {}},
         data{"ORIGINAL DATE", {}}, geo{"LOCATION / GPS", {}}, desc{"DESCRIPTIVE", {}};
    auto add = [](Card& c, const char* rotulo, const juce::String& valor) {
        const auto v = valor.trim();
        if (v.isNotEmpty()) c.linhas.push_back({rotulo, v});
    };
    auto campo = [&](const char* nome) -> juce::String {
        try { return txt(p.lerMetadado(itemId, nome)); } catch (...) { return {}; }
    };

    juce::File arquivoFisico;
    juce::var tecnicaJson;
    if (auto info = p.arquivoPrincipal(itemId)) {
        arquivoFisico = juce::File(info->caminhoAbsoluto);
        tecnicaJson = juce::JSON::parse(info->caracteristicasTecnicasJson);
    }
    juce::String origem;
    if (auto o = p.caminhoDeOrigem(itemId)) origem = *o;

    // ---- ARQUIVO
    {
        const juce::File ref = arquivoFisico != juce::File() ? arquivoFisico : juce::File(origem);
        add(arquivo, "Name", ref.getFileName());
        add(arquivo, "Format", ref.getFileExtension().trimCharactersAtStart(".").toUpperCase());
        if (ref.existsAsFile()) {
            add(arquivo, "Size", bytesHumano(ref.getSize()));
            add(arquivo, "Created", ref.getCreationTime().formatted("%Y-%m-%d %H:%M:%S"));
            add(arquivo, "Modified", ref.getLastModificationTime().formatted("%Y-%m-%d %H:%M:%S"));
        }
        add(arquivo, "Original path", origem);
        add(arquivo, "Asset code", campo("codigo_acervo"));
        add(arquivo, "Content type", campo("collection_type"));
    }

    // ---- TÉCNICO (características guardadas no ingest: chaves planas + "bruto" do ffprobe/Exiv2)
    juce::var exif;
    if (tecnicaJson.isObject()) {
        exif = tecnicaJson.getProperty("bruto", juce::var()).getProperty("exif", juce::var());
        auto num = [&](const char* chave) { return tecnicaJson.getProperty(chave, juce::var()); };
        const int w = static_cast<int>(num("larguraPx")), h = static_cast<int>(num("alturaPx"));
        if (w > 0 && h > 0) add(tecnico, "Dimensions", juce::String(w) + " x " + juce::String(h) + " px");
        if (tecnicaJson.hasProperty("duracaoSegundos") && static_cast<double>(num("duracaoSegundos")) > 0)
            add(tecnico, "Duration", duracaoHumana(static_cast<double>(num("duracaoSegundos"))));
        add(tecnico, "Codec", num("codec").toString());
        if (static_cast<int>(num("sampleRate")) > 0) add(tecnico, "Sample rate", juce::String(static_cast<int>(num("sampleRate"))) + " Hz");
        if (static_cast<int>(num("canais")) > 0) add(tecnico, "Channels", juce::String(static_cast<int>(num("canais"))));
        if (static_cast<int>(num("bitDepth")) > 0) add(tecnico, "Bit depth", juce::String(static_cast<int>(num("bitDepth"))) + " bit");
        if (static_cast<double>(num("fps")) > 0) add(tecnico, "Frame rate", juce::String(static_cast<double>(num("fps")), 2) + " fps");
        if (tecnicaJson.hasProperty("lufsIntegrado")) add(tecnico, "Loudness", juce::String(static_cast<double>(num("lufsIntegrado")), 1) + " LUFS");
        if (tecnicaJson.hasProperty("lra")) add(tecnico, "Loudness range", juce::String(static_cast<double>(num("lra")), 1) + " LU");
        if (tecnicaJson.hasProperty("picoDbfs")) add(tecnico, "Peak", juce::String(static_cast<double>(num("picoDbfs")), 1) + " dBFS");
    }
    try {   // LUFS & cia do cache de análise de áudio (quando não vieram no JSON)
        if (auto info = p.arquivoPrincipal(itemId)) {
            if (tecnico.linhas.empty() || !tecnicaJson.hasProperty("lufsIntegrado")) {
                auto st = p.projeto().registroLeitura().prepare(
                    "SELECT lufs_i, lra, true_peak FROM cache_arquivo WHERE arquivo_id = ?");
                st.bind(1, matriz::db::Value::of(info->id));
                if (st.step()) {
                    if (!st.columnIsNull(0)) add(tecnico, "Loudness", juce::String(st.columnReal(0), 1) + " LUFS");
                    if (!st.columnIsNull(1)) add(tecnico, "Loudness range", juce::String(st.columnReal(1), 1) + " LU");
                    if (!st.columnIsNull(2)) add(tecnico, "True peak", juce::String(st.columnReal(2), 1) + " dBTP");
                }
            }
        }
    } catch (...) {}

    // ---- CÂMERA / EXIF  e  DATA ORIGINAL
    auto exifStr = [&](const char* chave) -> juce::String {
        return exif.isObject() ? exif.getProperty(chave, juce::var()).toString() : juce::String();
    };
    add(camera, "Make", exifStr("Exif.Image.Make"));
    add(camera, "Model", exifStr("Exif.Image.Model"));
    if (camera.linhas.empty()) add(camera, "Camera", campo("camera_model"));
    add(camera, "Lens", exifStr("Exif.Photo.LensModel"));
    add(camera, "Orientation", exifStr("Exif.Image.Orientation"));
    add(camera, "Color space", exifStr("Exif.Photo.ColorSpace"));
    add(camera, "Artist", exifStr("Exif.Image.Artist"));
    add(camera, "Copyright", exifStr("Exif.Image.Copyright"));
    if (arquivoFisico.existsAsFile()) {   // EXIF completo sob demanda (sem MakerNote/binário/GPS)
        try {
            if (auto extra = matriz::ingest::lerExifCompletoParaNotas(arquivoFisico)) {
                int n = 0;
                for (const auto& linha : juce::StringArray::fromLines(juce::String::fromUTF8(extra->c_str()))) {
                    const auto rotulo = linha.upToFirstOccurrenceOf(":", false, false).trim();
                    const auto valor = linha.fromFirstOccurrenceOf(":", false, false).trim();
                    if (rotulo.isEmpty() || valor.isEmpty() || rotulo.startsWith("[")) continue;
                    if (++n > 60) break;
                    camera.linhas.push_back({rotulo, valor});
                }
            }
        } catch (...) {}
    }
    add(data, "Date taken", exifStr("Exif.Photo.DateTimeOriginal"));
    add(data, "Digitized", exifStr("Exif.Photo.DateTimeDigitized"));
    add(data, "File date (EXIF)", exifStr("Exif.Image.DateTime"));
    add(data, "Creation date", campo("data_criacao"));

    // ---- LOCALIZAÇÃO / GPS
    try {
        if (auto g = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(p.projeto().registroLeitura(), itemId)) {
            if (g->latitude && g->longitude)
                add(geo, "Coordinates", juce::String(*g->latitude, 6) + ", " + juce::String(*g->longitude, 6));
            if (g->altitude) add(geo, "Altitude", juce::String(*g->altitude, 1) + " m");
            add(geo, "Address", txt(g->formattedAddress));
            add(geo, "City", txt(g->city));
            add(geo, "State / Province", txt(g->stateProvince));
            add(geo, "Country", txt(g->country));
        }
    } catch (...) {}

    // ---- DESCRITIVO (IPTC/XMP/Dublin Core guardados no ingest)
    add(desc, "Title", campo("dc_title"));
    add(desc, "Description", campo("dc_description"));
    add(desc, "Keywords", campo("dc_subject"));
    add(desc, "List", campo("lista"));
    add(desc, "Author", campo("dc_creator"));
    add(desc, "Contributor", campo("dc_contributor"));
    add(desc, "Publisher", campo("dc_publisher"));
    add(desc, "Copyright", campo("dc_rights"));
    add(desc, "Language", campo("dc_language"));
    add(desc, "Source", campo("dc_source"));
    add(desc, "Identifier", campo("dc_identifier"));
    add(desc, "Event date", campo("ano"));
    add(desc, "Original medium", campo("source_media"));
    add(desc, "Created (metadata)", campo("dc_created"));
    add(desc, "Format (metadata)", campo("dc_format"));
    add(desc, "Notes", campo("notas_livres"));

    std::vector<Card> out;
    for (auto* c : {&arquivo, &tecnico, &camera, &data, &geo, &desc})
        if (!c->linhas.empty()) out.push_back(std::move(*c));
    return out;
}

// ---------------------------------------------------------------------------
// Conteúdo rolável: cards com rótulo em cima e valor embaixo (selecionável/copiável)
// ---------------------------------------------------------------------------
class PainelMetadadosArquivo::Conteudo : public juce::Component {
public:
    void definirCompacto(bool compacto) { compacto_ = compacto; }

    void definir(std::vector<Card> cards) {
        if (compacto_) { definirCompactoImpl(std::move(cards)); return; }
        removeAllChildren();
        linhas_.clear();
        titulos_.clear();
        cards_ = std::move(cards);
        const auto& tk = tema();
        for (const auto& c : cards_) {
            auto t = std::make_unique<juce::Label>();
            t->setText(c.titulo, juce::dontSendNotification);
            t->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
            t->setColour(juce::Label::textColourId, tk.acento);
            addAndMakeVisible(*t);
            titulos_.push_back(std::move(t));
            for (const auto& l : c.linhas) {
                Par par;
                par.rotulo = std::make_unique<juce::Label>();
                par.rotulo->setText(l.rotulo, juce::dontSendNotification);
                par.rotulo->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
                par.rotulo->setColour(juce::Label::textColourId, tk.textoTerciario);
                addAndMakeVisible(*par.rotulo);
                par.valor = std::make_unique<juce::TextEditor>();
                par.valor->setMultiLine(true, true);   // quebra de linha: nada truncado
                par.valor->setReadOnly(true);
                par.valor->setCaretVisible(false);
                par.valor->setScrollbarsShown(false);
                par.valor->setBorder({});
                par.valor->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
                par.valor->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
                par.valor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
                par.valor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
                par.valor->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
                par.valor->setText(l.valor, false);
                addAndMakeVisible(*par.valor);
                linhas_.push_back(std::move(par));
            }
        }
    }

    int alturaPara(int largura) {
        setSize(largura, 10);  // a largura primeiro: a altura dos textos depende dela
        const int altura = compacto_ ? organizarCompacto(largura) : organizar(largura);
        setSize(largura, altura);  // sem isto o conteúdo ficava com 10 px de altura e o painel parecia vazio
        return altura;
    }

    void paint(juce::Graphics& g) override {
        if (compacto_) { pintarCompacto(g); return; }
        const auto& tk = tema();
        for (const auto& r : fundosCards_) {
            g.setColour(tk.painel);
            g.fillRoundedRectangle(r.toFloat(), tk.raioMedio);
            g.setColour(tk.borda.withAlpha(0.6f));
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
        }
    }

private:

    // ── visual compacto (ficha fixa do STRUCTURE) ─────────────────────────────────────────────────────────
    enum class Icone { Nenhum, Calendario, Local, Etiqueta, Tecnico, Camera };
    struct Celula {
        std::unique_ptr<juce::Label> rotulo;
        std::unique_ptr<juce::TextEditor> valor;
        std::unique_ptr<juce::Label> extra;  // bytes exatos em cinza (só o Size)
        bool largo = false, mono = false, grande = false, linhaData = false;
    };
    struct CartaoC {
        bool arquivo = false;
        Icone icone = Icone::Nenhum;
        std::unique_ptr<juce::Label> titulo;       // cabeçalho (o card do arquivo usa o nome)
        std::unique_ptr<juce::Label> nome, tipo;   // só no card do arquivo
        juce::String selo;
        juce::Colour corSelo;
        std::vector<Celula> celulas;
    };
    struct ChipIcone { juce::Rectangle<int> r; Icone icone; };
    struct SeloDesenhado { juce::Rectangle<int> r; juce::String texto; juce::Colour cor; };

    std::unique_ptr<juce::Label> novoRotuloC(const juce::String& t) {
        auto l = std::make_unique<juce::Label>();
        l->setText(t, juce::dontSendNotification);
        l->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        l->setColour(juce::Label::textColourId, tema().textoSecundario);
        l->setBorderSize({0, 0, 0, 0});
        addAndMakeVisible(*l);
        return l;
    }

    std::unique_ptr<juce::TextEditor> novoValorC(const juce::String& t, float tamanho, bool negrito, bool mono, bool multi, bool direita) {
        auto v = std::make_unique<juce::TextEditor>();
        v->setMultiLine(multi, multi);  // multi: quebra de linha, nada truncado
        v->setReadOnly(true);
        v->setCaretVisible(false);
        v->setScrollbarsShown(false);
        v->setBorder({});
        v->setJustification(direita ? juce::Justification::right : juce::Justification::left);
        const int estilo = negrito ? juce::Font::bold : juce::Font::plain;
        v->setFont(mono ? juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), tamanho, estilo))
                        : juce::Font(juce::FontOptions(tamanho, estilo)));
        v->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        v->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        v->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        v->setColour(juce::TextEditor::textColourId, tema().textoPrimario);
        v->setText(t, false);
        addAndMakeVisible(*v);
        return v;
    }

    static juce::String comMilhares(juce::int64 n) {
        auto txt = juce::String(n);
        for (int i = txt.length() - 3; i > 0; i -= 3) txt = txt.substring(0, i) + "," + txt.substring(i);
        return txt;
    }

    void definirCompactoImpl(std::vector<Card> cards) {
        removeAllChildren();
        cartoes_.clear();
        linhas_.clear();
        titulos_.clear();
        cards_.clear();
        const auto& tk = tema();
        auto achar = [&](const char* titulo) -> const Card* {
            for (const auto& c : cards) if (c.titulo == titulo) return &c;
            return nullptr;
        };
        auto valorDe = [](const Card* c, const char* rotulo) -> juce::String {
            if (c != nullptr)
                for (const auto& l : c->linhas) if (l.rotulo == rotulo) return l.valor;
            return {};
        };
        auto celulaSimples = [&](const juce::String& rotulo, const juce::String& valor) {
            Celula ce;
            ce.rotulo = novoRotuloC(rotulo);
            ce.valor = novoValorC(valor, tk.tamanhoFonteCorpo, false, false, true, false);
            ce.largo = valor.length() > 34 || valor.containsChar('\n');
            return ce;
        };
        auto cartaoGrade = [&](const char* tituloOrigem, const juce::String& tituloNovo, Icone icone) {
            const Card* c = achar(tituloOrigem);
            if (c == nullptr || c->linhas.empty()) return;
            CartaoC cc;
            cc.icone = icone;
            cc.titulo = novoRotuloC(tituloNovo);
            cc.titulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)).withExtraKerningFactor(0.12f));
            cc.titulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            for (const auto& l : c->linhas) cc.celulas.push_back(celulaSimples(l.rotulo, l.valor));
            cartoes_.push_back(std::move(cc));
        };

        // ARQUIVO: nome em destaque, tipo de conteúdo embaixo, formato em selo, tamanho legível, caminho mono.
        if (const Card* f = achar("FILE")) {
            CartaoC cc;
            cc.arquivo = true;
            cc.nome = novoRotuloC(valorDe(f, "Name"));
            cc.nome->setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
            cc.nome->setColour(juce::Label::textColourId, tk.textoPrimario);
            const auto tipoConteudo = valorDe(f, "Content type");
            if (tipoConteudo.isNotEmpty()) {
                cc.tipo = novoRotuloC(tipoConteudo);
                cc.tipo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            }
            cc.selo = valorDe(f, "Format");
            const auto categoria = matriz::ingest::categoriaPorExtensao(cc.selo.toLowerCase());
            switch (categoria) {
                case matriz::ingest::CategoriaMidia::Video: cc.corSelo = juce::Colour(0xff7a5cc7); break;
                case matriz::ingest::CategoriaMidia::Audio: cc.corSelo = juce::Colour(0xff2f8f83); break;
                case matriz::ingest::CategoriaMidia::Imagem: cc.corSelo = juce::Colour(0xff3b7dd8); break;
                default: cc.corSelo = juce::Colour(0xffc27c2e); break;
            }
            const auto tamanho = valorDe(f, "Size");
            if (tamanho.isNotEmpty()) {
                Celula ce;
                ce.rotulo = novoRotuloC("Size");
                ce.grande = true;
                ce.valor = novoValorC(tamanho.upToFirstOccurrenceOf(" (", false, false), 17.0f, true, false, false, false);
                const auto bytes = tamanho.fromFirstOccurrenceOf("(", false, false).upToFirstOccurrenceOf(" bytes", false, false).trim();
                if (bytes.isNotEmpty()) {
                    ce.extra = novoRotuloC(comMilhares(bytes.getLargeIntValue()) + " bytes");
                }
                cc.celulas.push_back(std::move(ce));
            }
            if (tipoConteudo.isNotEmpty()) cc.celulas.push_back(celulaSimples("Content type", tipoConteudo));
            const auto codigo = valorDe(f, "Asset code");
            if (codigo.isNotEmpty()) cc.celulas.push_back(celulaSimples("Asset code", codigo));
            const auto caminho = valorDe(f, "Original path");
            if (caminho.isNotEmpty()) {
                Celula ce;
                ce.rotulo = novoRotuloC("Original path");
                ce.valor = novoValorC(caminho, tk.tamanhoFontePequena, false, true, true, false);
                ce.largo = ce.mono = true;
                cc.celulas.push_back(std::move(ce));
            }
            cartoes_.push_back(std::move(cc));
        }

        cartaoGrade("TECHNICAL", "TECHNICAL", Icone::Tecnico);

        // DATES: a data original deixa de ser card próprio; Original, Created e Modified em linhas, valor à direita.
        {
            const Card* dt = achar("ORIGINAL DATE");
            const Card* f = achar("FILE");
            std::vector<std::pair<juce::String, juce::String>> linhasDatas;
            juce::String original = valorDe(dt, "Date taken");
            const bool usouCriacao = original.isEmpty();
            if (usouCriacao) original = valorDe(dt, "Creation date");
            if (original.isNotEmpty()) linhasDatas.push_back({"Original (creation)", original});
            if (dt != nullptr)
                for (const auto& l : dt->linhas) {
                    if (l.rotulo == "Date taken") continue;
                    if (l.rotulo == "Creation date" && usouCriacao) continue;
                    linhasDatas.push_back({l.rotulo, l.valor});
                }
            const auto criado = valorDe(f, "Created");
            const auto modificado = valorDe(f, "Modified");
            if (criado.isNotEmpty()) linhasDatas.push_back({"Created", criado});
            if (modificado.isNotEmpty()) linhasDatas.push_back({"Modified", modificado});
            if (!linhasDatas.empty()) {
                CartaoC cc;
                cc.icone = Icone::Calendario;
                cc.titulo = novoRotuloC("DATES");
                cc.titulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)).withExtraKerningFactor(0.12f));
                cc.titulo->setColour(juce::Label::textColourId, tk.textoPrimario);
                for (const auto& [rot, val] : linhasDatas) {
                    Celula ce;
                    ce.linhaData = true;
                    ce.rotulo = novoRotuloC(rot);
                    ce.rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
                    ce.valor = novoValorC(val, tk.tamanhoFonteCorpo, true, false, false, true);
                    cc.celulas.push_back(std::move(ce));
                }
                cartoes_.push_back(std::move(cc));
            }
        }

        cartaoGrade("LOCATION / GPS", "LOCATION", Icone::Local);
        cartaoGrade("DESCRIPTIVE", "DESCRIPTIVE", Icone::Etiqueta);
        cartaoGrade("CAMERA / EXIF", "CAMERA / EXIF", Icone::Camera);
        (void) tk;
    }

    // Devolve a altura da célula de meia largura / largura total já posicionada em (x, y, w).
    int posicionarCelula(Celula& ce, int x, int y, int w) {
        ce.rotulo->setBounds(x, y, w, 16);
        const int yv = y + 16;
        int h;
        if (ce.grande) {
            h = 26;
            ce.valor->setBounds(x, yv, w, h);
        } else {
            ce.valor->setBounds(x, yv, w, 20);
            h = std::max(20, ce.valor->getTextHeight() + 4);
            ce.valor->setBounds(x, yv, w, h);
        }
        int total = 16 + h;
        if (ce.extra) {
            ce.extra->setBounds(x, yv + h, w, 16);
            total += 16;
        }
        return total;
    }

    int organizarCompacto(int largura) {
        constexpr int kMargem = 8, kPad = 14, kGap = 10, kColGap = 14, kEntre = 10;
        fundosC_.clear();
        chips_.clear();
        selos_.clear();
        separadores_.clear();
        int y = kMargem;
        for (auto& c : cartoes_) {
            const int topo = y;
            y += kPad;
            const int xi = kMargem + kPad;
            const int w = largura - 2 * (kMargem + kPad);
            if (c.arquivo) {
                const int larguraSelo = c.selo.isEmpty() ? 0 : 22 + juce::GlyphArrangement::getStringWidthInt(
                                            juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)), c.selo);
                c.nome->setBounds(xi, y, std::max(20, w - larguraSelo - 8), 26);
                if (larguraSelo > 0) selos_.push_back({juce::Rectangle<int>(xi + w - larguraSelo, y + 2, larguraSelo, 22), c.selo, c.corSelo});
                y += 26;
                if (c.tipo) { c.tipo->setBounds(xi, y, w, 20); y += 20; }
                y += kEntre;
            } else {
                chips_.push_back({juce::Rectangle<int>(xi, y, 26, 26), c.icone});
                c.titulo->setBounds(xi + 26 + 8, y, std::max(20, w - 34), 26);
                y += 26 + kEntre;
            }
            const int colW = (w - kColGap) / 2;
            int coluna = 0, yLinha = y, alturaLinha = 0;
            auto fechaLinha = [&] {
                if (coluna != 0) { y = yLinha + alturaLinha + kEntre; coluna = 0; alturaLinha = 0; }
            };
            for (auto& ce : c.celulas) {
                if (ce.linhaData) {
                    fechaLinha();
                    separadores_.push_back({xi, y, w, 1});
                    ce.rotulo->setBounds(xi, y + 5, w / 2, 22);
                    ce.valor->setBounds(xi + w / 2, y + 4, w - w / 2, 24);
                    y += 33;
                    continue;
                }
                if (ce.largo) {
                    fechaLinha();
                    y += posicionarCelula(ce, xi, y, w) + kEntre;
                    continue;
                }
                if (coluna == 0) yLinha = y;
                alturaLinha = std::max(alturaLinha, posicionarCelula(ce, xi + coluna * (colW + kColGap), yLinha, colW));
                if (++coluna == 2) { y = yLinha + alturaLinha + kEntre; coluna = 0; alturaLinha = 0; }
            }
            fechaLinha();
            y += kPad - kEntre;
            fundosC_.push_back({kMargem, topo, largura - 2 * kMargem, y - topo});
            y += kGap;
        }
        return y + kMargem - kGap;
    }

    static void desenharIcone(juce::Graphics& g, Icone icone, juce::Rectangle<float> chip, juce::Colour cor) {
        const float cx = chip.getCentreX(), cy = chip.getCentreY();
        g.setColour(cor);
        const juce::PathStrokeType traco(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path p;
        switch (icone) {
            case Icone::Calendario:
                g.drawRoundedRectangle(cx - 6.0f, cy - 5.0f, 12.0f, 11.0f, 2.0f, 1.3f);
                g.drawLine(cx - 6.0f, cy - 1.5f, cx + 6.0f, cy - 1.5f, 1.3f);
                g.drawLine(cx - 3.0f, cy - 7.0f, cx - 3.0f, cy - 4.0f, 1.3f);
                g.drawLine(cx + 3.0f, cy - 7.0f, cx + 3.0f, cy - 4.0f, 1.3f);
                break;
            case Icone::Local:
                p.startNewSubPath(cx, cy + 7.0f);
                p.cubicTo(cx - 7.0f, cy - 1.0f, cx - 5.5f, cy - 7.0f, cx, cy - 7.0f);
                p.cubicTo(cx + 5.5f, cy - 7.0f, cx + 7.0f, cy - 1.0f, cx, cy + 7.0f);
                g.strokePath(p, traco);
                g.fillEllipse(cx - 1.8f, cy - 4.3f, 3.6f, 3.6f);
                break;
            case Icone::Etiqueta:
                p.startNewSubPath(cx - 7.0f, cy);
                p.lineTo(cx - 1.0f, cy - 6.0f);
                p.lineTo(cx + 6.0f, cy - 6.0f);
                p.lineTo(cx + 6.0f, cy + 1.0f);
                p.lineTo(cx, cy + 7.0f);
                p.closeSubPath();
                g.strokePath(p, traco);
                g.fillEllipse(cx + 1.0f, cy - 4.0f, 2.6f, 2.6f);
                break;
            case Icone::Tecnico:
                for (int i = 0; i < 3; ++i) {
                    const float yy = cy - 5.0f + 5.0f * static_cast<float>(i);
                    g.drawLine(cx - 6.0f, yy, cx + 6.0f, yy, 1.3f);
                    g.fillEllipse(cx - 4.0f + 4.0f * static_cast<float>((i * 2) % 3), yy - 2.0f, 4.0f, 4.0f);
                }
                break;
            case Icone::Camera:
                g.drawRoundedRectangle(cx - 7.0f, cy - 4.5f, 14.0f, 11.0f, 2.0f, 1.3f);
                g.drawEllipse(cx - 3.0f, cy - 2.0f, 6.0f, 6.0f, 1.3f);
                break;
            case Icone::Nenhum: break;
        }
    }

    void pintarCompacto(juce::Graphics& g) {
        const auto& tk = tema();
        for (const auto& r : fundosC_) {
            g.setColour(tk.painel);
            g.fillRoundedRectangle(r.toFloat(), tk.raioMedio);
            g.setColour(tk.borda.withAlpha(0.6f));
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
        }
        for (const auto& sep : separadores_) {
            g.setColour(tk.borda.withAlpha(0.55f));
            g.fillRect(sep);
        }
        for (const auto& chip : chips_) {
            const bool local = chip.icone == Icone::Local;
            const auto cor = local ? juce::Colour(0xff3a7ac9) : tk.textoSecundario;
            g.setColour(local ? cor.withAlpha(0.16f) : tk.painelAlt.withAlpha(0.7f));
            g.fillRoundedRectangle(chip.r.toFloat(), 7.0f);
            desenharIcone(g, chip.icone, chip.r.toFloat(), cor);
        }
        for (const auto& selo : selos_) {
            g.setColour(selo.cor.withAlpha(0.18f));
            g.fillRoundedRectangle(selo.r.toFloat(), static_cast<float>(selo.r.getHeight()) * 0.5f);
            g.setColour(selo.cor);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            g.drawText(selo.texto, selo.r, juce::Justification::centred, false);
        }
    }

    bool compacto_ = false;
    std::vector<CartaoC> cartoes_;
    std::vector<juce::Rectangle<int>> fundosC_, separadores_;
    std::vector<ChipIcone> chips_;
    std::vector<SeloDesenhado> selos_;

    struct Par {
        std::unique_ptr<juce::Label> rotulo;
        std::unique_ptr<juce::TextEditor> valor;
    };

    int organizar(int largura) {
        constexpr int kMargem = 8, kPad = 8, kGap = 8;
        fundosCards_.clear();
        int y = kMargem;
        size_t iLinha = 0;
        for (size_t ic = 0; ic < cards_.size(); ++ic) {
            const int topo = y;
            y += kPad;
            titulos_[ic]->setBounds(kMargem + kPad, y, largura - 2 * (kMargem + kPad), 16);
            y += 18;
            for (size_t k = 0; k < cards_[ic].linhas.size(); ++k, ++iLinha) {
                auto& par = linhas_[iLinha];
                const int w = largura - 2 * (kMargem + kPad);
                par.rotulo->setBounds(kMargem + kPad, y, w, 14);
                y += 14;
                par.valor->setBounds(kMargem + kPad, y, w, 20);
                const int h = juce::jmax(18, par.valor->getTextHeight() + 4);
                par.valor->setBounds(kMargem + kPad, y, w, h);
                y += h + 4;
            }
            y += kPad - 4;
            fundosCards_.push_back({kMargem, topo, largura - 2 * kMargem, y - topo});
            y += kGap;
        }
        return y + kMargem;
    }

    std::vector<Card> cards_;
    std::vector<std::unique_ptr<juce::Label>> titulos_;
    std::vector<Par> linhas_;
    std::vector<juce::Rectangle<int>> fundosCards_;
};

// ---------------------------------------------------------------------------
PainelMetadadosArquivo::PainelMetadadosArquivo(ProjetoAberto& projeto) : projeto_(projeto) {
    const bool isPt = i18n::localeAtivo().startsWith("pt");
    lblVazio_.setText(isPt ? juce::String::fromUTF8("Selecione um arquivo para ver seus metadados.")
                           : juce::String("Select a file to see its metadata."),
                      juce::dontSendNotification);
    lblVazio_.setJustificationType(juce::Justification::centred);
    lblVazio_.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
    lblVazio_.setColour(juce::Label::textColourId, tema().textoTerciario);
    addAndMakeVisible(lblVazio_);

    conteudo_ = std::make_unique<Conteudo>();
    viewport_.setViewedComponent(conteudo_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addChildComponent(viewport_);
}

PainelMetadadosArquivo::~PainelMetadadosArquivo() {
    ++geracao_;
    viewport_.setViewedComponent(nullptr, false);
}

void PainelMetadadosArquivo::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);
}

void PainelMetadadosArquivo::resized() {
    lblVazio_.setBounds(getLocalBounds().reduced(12));
    viewport_.setBounds(getLocalBounds());
    if (viewport_.isVisible()) conteudo_->alturaPara(viewport_.getMaximumVisibleWidth());
}

void PainelMetadadosArquivo::definirLayoutCompacto(bool compacto) { conteudo_->definirCompacto(compacto); }

void PainelMetadadosArquivo::mostrarItem(const std::string& itemId) {
    const int geracao = ++geracao_;
    if (itemId.empty()) {
        viewport_.setVisible(false);
        lblVazio_.setVisible(true);
        return;
    }
    juce::Component::SafePointer<PainelMetadadosArquivo> safe(this);
    ProjetoAberto* proj = &projeto_;
    pool_.removeAllJobs(false, 0);  // descarta jobs ainda não iniciados de seleções antigas
    pool_.addJob([safe, proj, itemId, geracao]() {
        std::vector<Card> cards;
        try { cards = lerCards(*proj, itemId); } catch (...) {}
        juce::MessageManager::callAsync([safe, geracao, cards = std::move(cards)]() mutable {
            if (!safe) return;
            safe->aplicarCards(std::move(cards), geracao);
        });
    });
}

void PainelMetadadosArquivo::aplicarCards(std::vector<Card> cards, int geracao) {
    if (geracao != geracao_.load()) return;  // chegou depois de a seleção mudar
    const bool vazio = cards.empty();
    conteudo_->definir(std::move(cards));
    conteudo_->alturaPara(viewport_.getMaximumVisibleWidth());
    viewport_.setVisible(!vazio);
    lblVazio_.setVisible(vazio);
    viewport_.setViewPosition(0, 0);
}

} // namespace matriz::ui
