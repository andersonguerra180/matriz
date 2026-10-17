#include "PainelMetadadosArquivo.h"

#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../Analytics/AssetGeolocation.h"
#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"

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
    void definir(std::vector<Card> cards) {
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
        const int altura = organizar(largura);
        setSize(largura, altura);  // sem isto o conteúdo ficava com 10 px de altura e o painel parecia vazio
        return altura;
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        for (const auto& r : fundosCards_) {
            g.setColour(tk.painel);
            g.fillRoundedRectangle(r.toFloat(), tk.raioMedio);
            g.setColour(tk.borda.withAlpha(0.6f));
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
        }
    }

private:
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
