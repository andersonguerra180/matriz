#include "FichaPanelComponent.h"

#include "MetadadosOriginaisComponent.h"
#include "OriginalSourceMedium.h"
#include "TagChipsEditor.h"
#include "PeoplePickerComponent.h"
#include "../Analytics/AssetGeolocation.h"

#include "../Ficha/FichaI18n.h"
#include "../Ficha/OrigemPadrao.h"
#include "../I18n/Strings.h"
#include "../Ingest/FluxoLote.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Preservation/Preservation.h"
#include "../Vault/Resolucao.h"
#include "FormatoTempo.h"
#include "SelecionarTipoMidiaDialogo.h"
#include "Tokens.h"
#include "ProgressoGlobal.h"
#include <exiv2/exiv2.hpp>

#include <algorithm>
#include <regex>
#include <set>

namespace matriz::ui {

using matriz::ficha::Campo;
using matriz::ficha::CampoTipo;
using matriz::ficha::FichaDefinition;
using matriz::ficha::VisivelSeOperador;

namespace {

std::string autorAtual() { return juce::SystemStats::getFullUserName().toStdString(); }

// Default de "origem" (Digital/Analógico, §4.2) é tratado por ProjetoAberto.

bool validarEan13(const juce::String& valor) {
    juce::String digitos;
    for (auto c : valor) if (juce::CharacterFunctions::isDigit(c)) digitos += juce::String::charToString(c);
    if (digitos.length() != 13) return false;
    int soma = 0;
    for (int i = 0; i < 12; ++i) {
        int d = digitos[i] - '0';
        soma += (i % 2 == 0) ? d : d * 3;
    }
    int checkEsperado = (10 - (soma % 10)) % 10;
    return (digitos[12] - '0') == checkEsperado;
}

bool validarIsrc(const juce::String& valor) {
    static const std::regex padrao(R"(^[A-Za-z]{2}-?[A-Za-z0-9]{3}-?\d{2}-?\d{5}$)");
    return std::regex_match(valor.toStdString(), padrao);
}

bool validarData(const juce::String& valor) {
    static const std::regex padrao(R"(^\d{4}-\d{2}-\d{2}$)");
    return valor.isEmpty() || std::regex_match(valor.toStdString(), padrao);
}

juce::var parseJsonSeguro(const juce::String& texto) {
    if (texto.isEmpty()) return juce::var(juce::Array<juce::var>());
    juce::var v = juce::JSON::parse(texto);
    return v.isArray() ? v : juce::var(juce::Array<juce::var>());
}

// --- Editor de tabela/lista_pessoas: N colunas, linhas dinâmicas ----------

class TabelaEditor : public juce::Component {
public:
    explicit TabelaEditor(std::vector<juce::String> colunas) : colunas_(std::move(colunas)) {
        addAndMakeVisible(botaoAdicionar_);
        botaoAdicionar_.setButtonText(matriz::i18n::t("ficha.tabela_adicionar_linha"));
        botaoAdicionar_.onClick = [this] {
            adicionarLinha({});
            if (aoMudar) aoMudar();
            if (auto* p = getParentComponent()) p->resized();
        };
    }

    void setValorJson(const juce::String& json) {
        linhas_.clear();
        juce::var array = parseJsonSeguro(json);
        if (auto* arr = array.getArray())
            for (auto& linha : *arr) {
                std::vector<juce::String> valores;
                for (auto& col : colunas_) valores.push_back(linha.getProperty(col, "").toString());
                adicionarLinha(valores);
            }
    }

    juce::String getValorJson() const {
        juce::Array<juce::var> arr;
        for (auto* linha : linhas_) {
            auto obj = std::make_unique<juce::DynamicObject>();
            for (size_t i = 0; i < colunas_.size(); ++i)
                obj->setProperty(colunas_[static_cast<size_t>(i)], linha->celulas[static_cast<int>(i)]->getText());
            arr.add(juce::var(obj.release()));
        }
        return juce::JSON::toString(juce::var(arr), true);
    }

    double somaColuna(const juce::String& coluna) const {
        double soma = 0.0;
        for (size_t i = 0; i < colunas_.size(); ++i)
            if (colunas_[static_cast<size_t>(i)] == coluna)
                for (auto* linha : linhas_) soma += linha->celulas[static_cast<int>(i)]->getText().getDoubleValue();
        return soma;
    }

    int alturaTotal() const { return static_cast<int>(linhas_.size() + 1) * kAlturaLinha; }

    void resized() override {
        int y = 0;
        for (auto* linha : linhas_) {
            int x = 0;
            int larguraCelula = colunas_.empty() ? getWidth() : (getWidth() - 28) / static_cast<int>(colunas_.size());
            for (auto* celula : linha->celulas) {
                celula->setBounds(x, y, larguraCelula, kAlturaLinha - 2);
                x += larguraCelula;
            }
            linha->remover->setBounds(x, y, 28, kAlturaLinha - 2);
            y += kAlturaLinha;
        }
        botaoAdicionar_.setBounds(0, y, 140, kAlturaLinha - 2);
    }

    std::function<void()> aoMudar;

private:
    struct Linha {
        juce::OwnedArray<juce::TextEditor> celulas;
        std::unique_ptr<juce::TextButton> remover;
    };

    void adicionarLinha(const std::vector<juce::String>& valores) {
        auto* linha = linhas_.add(new Linha());
        for (size_t i = 0; i < colunas_.size(); ++i) {
            auto* editor = linha->celulas.add(new juce::TextEditor());
            editor->setTextToShowWhenEmpty(colunas_[i], matriz::ui::tema().textoTerciario);
            if (i < valores.size()) editor->setText(valores[i], false);
            editor->onFocusLost = [this] { if (aoMudar) aoMudar(); };
            addAndMakeVisible(editor);
        }
        linha->remover.reset(new juce::TextButton(juce::String::fromUTF8("\xc3\x97")));
        linha->remover->onClick = [this, linha] {
            linhas_.removeObject(linha);
            if (aoMudar) aoMudar();
            if (auto* p = getParentComponent()) p->resized();
        };
        addAndMakeVisible(*linha->remover);
    }

    static constexpr int kAlturaLinha = 26;
    std::vector<juce::String> colunas_;
    juce::OwnedArray<Linha> linhas_;
    juce::TextButton botaoAdicionar_;
};

} // namespace

// ---------------------------------------------------------------------------
// PreviaWidget — O painel de prévia (player visual de áudio/vídeo) do topo
// ---------------------------------------------------------------------------

class PreviaWidget : public juce::Component {
public:
    PreviaWidget() {
        btnPlay_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x96\xb6"));
        btnPlay_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btnPlay_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(*btnPlay_);

        lblTempo_ = std::make_unique<juce::Label>();
        lblTempo_->setText("00:00:00 / --:--:--", juce::dontSendNotification);
        lblTempo_->setFont(juce::Font(juce::FontOptions(10.0f)));
        lblTempo_->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
        addAndMakeVisible(*lblTempo_);
    }

    void setDuraTexto(const juce::String& t) {
        if (lblTempo_) lblTempo_->setText("00:00:00 / " + t, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        g.setColour(juce::Colour(0xff18181b));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);

        // Frame header label PRÉVIA
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("PREVIEW", bounds.reduced(8, 4), juce::Justification::topRight);

        // Simulated media preview screen
        auto areaMidia = bounds.reduced(8, 22).withTrimmedBottom(22);
        g.setColour(juce::Colour(0xff09090b));
        g.fillRoundedRectangle(areaMidia.toFloat(), 4.0f);

        // Simulated waveform lines
        g.setColour(juce::Colour(0xff3b82f6).withAlpha(0.35f));
        float midY = areaMidia.getCentreY();
        for (int x = areaMidia.getX() + 6; x < areaMidia.getRight() - 6; x += 4) {
            float h = (std::abs(std::sin(x * 0.08f)) + 0.1f) * (areaMidia.getHeight() * 0.6f);
            g.drawVerticalLine(x, midY - h / 2.0f, midY + h / 2.0f);
        }

        // Control bar background at bottom
        auto areaControles = bounds.removeFromBottom(22).reduced(8, 2);
        g.setColour(juce::Colour(0xff27272a));
        g.fillRoundedRectangle(areaControles.toFloat(), 4.0f);
    }

    void resized() override {
        auto area = getLocalBounds().removeFromBottom(22).reduced(8, 2);
        if (btnPlay_) btnPlay_->setBounds(area.removeFromLeft(22));
        if (lblTempo_) lblTempo_->setBounds(area.removeFromRight(120));
    }

private:
    std::unique_ptr<juce::TextButton> btnPlay_;
    std::unique_ptr<juce::Label> lblTempo_;
};

// ---------------------------------------------------------------------------
// FichaConteudo — o corpo real da ficha, reconstruído a cada mostrarItem().
// ---------------------------------------------------------------------------

enum class MediaCategory {
    Audio,
    Video,
    Image,
    Docs,
    Mixed
};

inline MediaCategory determinarCategoriaMidia(const std::string& tipoMidia, const std::string& extensao) {
    if (tipoMidia == "digital_audio" || tipoMidia == "audio" || tipoMidia == "cassete" ||
        tipoMidia == "vinil" || tipoMidia == "fita_rolo" || tipoMidia == "cd" ||
        tipoMidia == "dat" || tipoMidia == "minidisc" || tipoMidia == "sample" ||
        tipoMidia == "sound_effects" || tipoMidia == "field_recording" || tipoMidia == "release") {
        return MediaCategory::Audio;
    }
    if (tipoMidia == "digital_video" || tipoMidia == "video" || tipoMidia == "vhs" ||
        tipoMidia == "betacam" || tipoMidia == "betamax" || tipoMidia == "filme" ||
        tipoMidia == "umatic" || tipoMidia == "dvd") {
        return MediaCategory::Video;
    }
    if (tipoMidia == "foto" || tipoMidia == "negativo" || tipoMidia == "slide" ||
        tipoMidia == "cover_art" || tipoMidia == "imagem") {
        return MediaCategory::Image;
    }
    if (tipoMidia == "documento" || tipoMidia == "3d_file" || tipoMidia == "docs" || tipoMidia == "texto" || tipoMidia == "sessao") {
        return MediaCategory::Docs;
    }
    auto cat = matriz::ingest::categoriaPorExtensao(extensao);
    switch (cat) {
        case matriz::ingest::CategoriaMidia::Audio: return MediaCategory::Audio;
        case matriz::ingest::CategoriaMidia::Video: return MediaCategory::Video;
        case matriz::ingest::CategoriaMidia::Arte:
        case matriz::ingest::CategoriaMidia::Imagem: return MediaCategory::Image;
        case matriz::ingest::CategoriaMidia::Sessao:
        case matriz::ingest::CategoriaMidia::Documento:
        case matriz::ingest::CategoriaMidia::Texto:
        case matriz::ingest::CategoriaMidia::Desconhecida:
        default:
            return MediaCategory::Docs;
    }
}

inline const std::vector<std::pair<juce::String, juce::String>>& mapaTraducoesContent() {
    static const std::vector<std::pair<juce::String, juce::String>> mapa = {
        // Audio
        {"Album", juce::String::fromUTF8("Álbum")},
        {"Single", "Single"},
        {"EP", "EP"},
        {"Compilation", juce::String::fromUTF8("Compilação")},
        {"Soundtrack", "Trilha Sonora"},
        {"Stems", "Stems"},
        {"Multitracks", "Multitracks"},
        {"Sample Pack", "Pacote de Samples"},
        {"Sample", "Sample"},
        {"Preset", "Preset"},
        {"DAW Session", juce::String::fromUTF8("Sessão de DAW")},
        {"Field Recording", juce::String::fromUTF8("Gravação de Campo")},
        {"Sound FX", "Efeitos Sonoros (SFX)"},
        {"MIDI", "MIDI"},
        {"Artist Catalog", juce::String::fromUTF8("Catálogo do Artista")},
        {"Artist Backup", "Backup do Artista"},
        // Video
        {"Raw Footage", juce::String::fromUTF8("Gravação Bruta")},
        {"Home Video", juce::String::fromUTF8("Vídeo Caseiro")},
        {"Music Video", "Videoclipe"},
        {"Film", "Filme / Curta"},
        {"Documentary", juce::String::fromUTF8("Documentário")},
        {"Corporate Video", juce::String::fromUTF8("Vídeo Institucional")},
        {"Commercial", "Comercial / Publicidade"},
        {"Live Performance", "Show / Ao Vivo"},
        {"NLE Project", juce::String::fromUTF8("Projeto de Edição (NLE)")},
        {"Social Media Video", juce::String::fromUTF8("Vídeos para Redes Sociais")},
        // Image
        {"Photo", "Foto"},
        {"Artwork", juce::String::fromUTF8("Arte / Ilustração")},
        {"Album Cover", juce::String::fromUTF8("Capa de Álbum")},
        {"Poster", juce::String::fromUTF8("Pôster / Cartaz")},
        {"Press / Promotional", juce::String::fromUTF8("Material de Divulgação")},
        {"Image Edit Project", "Projeto de Imagem"},
        {"Graphics", juce::String::fromUTF8("Gráficos / Design")},
        {"Logo", "Logotipo"},
        {"3D", juce::String::fromUTF8("Modelagem 3D")},
        // Docs
        {"Documentation", juce::String::fromUTF8("Documentação")},
        {"Book", "Livro"},
        {"Contract", "Contrato"},
        {"Manual", "Manual"},
        {"Report", juce::String::fromUTF8("Relatório")},
        {"Reference", juce::String::fromUTF8("Referência / Pesquisa")},
        {"Technical Documentation", juce::String::fromUTF8("Documentação Técnica")},
        {"Spreadsheet", "Planilha"},
        {"Planilha", "Planilha"}
    };
    return mapa;
}

inline juce::String traduzirContent(const juce::String& val, bool paraPt) {
    if (val.isEmpty()) return {};
    for (const auto& par : mapaTraducoesContent()) {
        if (paraPt) {
            if (val.equalsIgnoreCase(par.first)) return par.second;
        } else {
            if (val.equalsIgnoreCase(par.second)) return par.first;
        }
    }
    return val;
}

inline std::vector<juce::String> opcoesContentPorCategoria(MediaCategory cat, bool isPt) {
    std::vector<juce::String> keys;
    if (cat == MediaCategory::Audio) {
        keys = {"Album", "EP", "Single", "Compilation", "Soundtrack", "Stems", "Multitracks",
                "Sample Pack", "Sample", "Preset", "DAW Session", "Field Recording", "Sound FX", "MIDI",
                "Artist Catalog", "Artist Backup"};
    } else if (cat == MediaCategory::Video) {
        keys = {"Raw Footage", "Home Video", "Music Video", "Film", "Documentary",
                "Corporate Video", "Commercial", "Live Performance", "NLE Project", "Social Media Video"};
    } else if (cat == MediaCategory::Image) {
        keys = {"Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional", "Image Edit Project",
                "Graphics", "Logo", "3D"};
    } else if (cat == MediaCategory::Docs) {
        keys = {"Documentation", "Book", "Contract", "Manual", "Report", "Reference",
                "Technical Documentation", "Spreadsheet"};
    } else {
        std::vector<juce::String> res;
        for (auto c : {MediaCategory::Audio, MediaCategory::Video, MediaCategory::Image, MediaCategory::Docs}) {
            auto sub = opcoesContentPorCategoria(c, isPt);
            res.insert(res.end(), sub.begin(), sub.end());
        }
        return res;
    }
    if (!isPt) return keys;
    std::vector<juce::String> res;
    for (const auto& k : keys) res.push_back(traduzirContent(k, true));
    return res;
}

inline std::vector<std::string> opcoesContentPorCategoriaString(MediaCategory cat, bool isPt) {
    auto juceOpts = opcoesContentPorCategoria(cat, isPt);
    std::vector<std::string> res;
    res.reserve(juceOpts.size());
    for (const auto& s : juceOpts) res.push_back(s.toStdString());
    return res;
}

class FichaConteudo : public juce::Component {
public:
    explicit FichaConteudo(ProjetoAberto& projeto) : projeto_(projeto) {}

    void paint(juce::Graphics& g) override {
        if (itemId_.empty()) return;
        const auto& tk = matriz::ui::tema();
        auto drawCard = [&](const juce::Rectangle<int>& r) {
            if (r.isEmpty()) return;
            g.setColour(tk.painel);
            g.fillRoundedRectangle(r.toFloat(), 6.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 1.0f);
        };
        drawCard(quadroDublinCore_);
        drawCard(quadroUserAsset_);
        drawCard(quadroGeoLocation_);
    }

    void lookAndFeelChanged() override {
        const auto& tk = matriz::ui::tema();
        if (cabecalho_) {
            cabecalho_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
            cabecalho_->setColour(juce::Label::textColourId, tk.textoPrimario);
        }
        if (secHeaderDublinCore_) {
            secHeaderDublinCore_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            secHeaderDublinCore_->setColour(juce::Label::textColourId, tk.textoPrimario);
        }
        if (secHeaderUserAsset_) {
            secHeaderUserAsset_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            secHeaderUserAsset_->setColour(juce::Label::textColourId, tk.textoPrimario);
        }
        if (geolocalizacao_.titulo) {
            geolocalizacao_.titulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            geolocalizacao_.titulo->setColour(juce::Label::textColourId, tk.textoPrimario);
        }
        for (auto* btn : {btnAjudaDublinCore_.get(), btnAjudaUserAsset_.get(), btnAjudaGeoLocation_.get()}) {
            if (btn) {
                btn->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
                btn->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
                btn->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
            }
        }
        for (auto* btn : {btnCollapseDublinCore_.get(), btnCollapseUserAsset_.get(), btnCollapseGeoLocation_.get()}) {
            if (btn) {
                btn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
                btn->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
                btn->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
            }
        }
        if (geolocalizacao_.labelCoords) geolocalizacao_.labelCoords->setColour(juce::Label::textColourId, tk.textoSecundario);
        if (geolocalizacao_.labelAddress) geolocalizacao_.labelAddress->setColour(juce::Label::textColourId, tk.textoSecundario);
        if (geolocalizacao_.labelCity) geolocalizacao_.labelCity->setColour(juce::Label::textColourId, tk.textoSecundario);
        if (geolocalizacao_.labelState) geolocalizacao_.labelState->setColour(juce::Label::textColourId, tk.textoSecundario);
        if (geolocalizacao_.labelCountry) geolocalizacao_.labelCountry->setColour(juce::Label::textColourId, tk.textoSecundario);

        if (geolocalizacao_.editorCoords) {
            geolocalizacao_.editorCoords->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geolocalizacao_.editorCoords->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geolocalizacao_.editorCoords->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }
        if (geolocalizacao_.editorAddress) {
            geolocalizacao_.editorAddress->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geolocalizacao_.editorAddress->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geolocalizacao_.editorAddress->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }
        if (geolocalizacao_.editorCity) {
            geolocalizacao_.editorCity->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geolocalizacao_.editorCity->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geolocalizacao_.editorCity->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }
        if (geolocalizacao_.editorState) {
            geolocalizacao_.editorState->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geolocalizacao_.editorState->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geolocalizacao_.editorState->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }
        if (geolocalizacao_.editorCountry) {
            geolocalizacao_.editorCountry->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geolocalizacao_.editorCountry->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geolocalizacao_.editorCountry->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }

        for (auto& u : camposUnificados_) {
            if (u->rotulo) u->rotulo->setColour(juce::Label::textColourId, tk.textoSecundario);
            if (u->badge) u->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            if (u->editor) {
                if (auto* ed = dynamic_cast<juce::TextEditor*>(u->editor.get())) {
                    ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
                    ed->setColour(juce::TextEditor::textColourId, juce::Colours::black);
                    ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
                } else if (auto* cb = dynamic_cast<juce::ComboBox*>(u->editor.get())) {
                    cb->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
                    cb->setColour(juce::ComboBox::textColourId, juce::Colours::black);
                    cb->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
                    cb->setColour(juce::ComboBox::outlineColourId, tk.borda);
                } else if (auto* tb = dynamic_cast<juce::ToggleButton*>(u->editor.get())) {
                    tb->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
                    tb->setColour(juce::ToggleButton::tickColourId, tk.acento);
                }
            }
        }

        for (auto& s : secoes_) {
            if (s.titulo) s.titulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            for (auto& l : s.linhas) {
                if (l->rotulo) l->rotulo->setColour(juce::Label::textColourId, tk.textoSecundario);
                if (l->editorSimples) {
                    if (auto* ed = dynamic_cast<juce::TextEditor*>(l->editorSimples.get())) {
                        ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
                        ed->setColour(juce::TextEditor::textColourId, juce::Colours::black);
                        ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
                    } else if (auto* cb = dynamic_cast<juce::ComboBox*>(l->editorSimples.get())) {
                        cb->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
                        cb->setColour(juce::ComboBox::textColourId, juce::Colours::black);
                        cb->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
                        cb->setColour(juce::ComboBox::outlineColourId, tk.borda);
                    }
                }
            }
        }

        if (arquivosEsperados_.titulo) arquivosEsperados_.titulo->setColour(juce::Label::textColourId, tk.textoPrimario);
        if (observacoes_.titulo) observacoes_.titulo->setColour(juce::Label::textColourId, tk.textoPrimario);
        if (mensagemNaoClassificado_) mensagemNaoClassificado_->setColour(juce::Label::textColourId, tk.textoSecundario);

        repaint();
    }

    void limpar() {
        camposUnificados_.clear();

        linhas_.clear();
        secoes_.clear();
        destaque_.clear();
        previaWidget_.reset();
        cabecalho_.reset();
        mensagemNaoClassificado_.reset();
        botoesTipoNaoClassificado_.clear();
        botaoCancelarRecategorizar_.reset();
        botaoRecategorizar_.reset();
        arquivosEsperados_.titulo.reset();
        arquivosEsperados_.linhas.clear();
        observacoes_.titulo.reset();
        observacoes_.itens.clear();
        observacoes_.botaoNova.reset();
        observacoes_.editorTexto.reset();
        observacoes_.editorMinutagem.reset();
        observacoes_.botaoSalvar.reset();
        observacoes_.botaoCancelar.reset();
        botoesAdicionarFaixa_.clear();
        botaoAplicar_.reset();
        labelAplicado_.reset();
        labelReviewFaltando_.reset();
        geolocalizacao_ = {};
        quadroDublinCore_ = {};
        quadroUserAsset_ = {};
        quadroGeoLocation_ = {};
        secHeaderDublinCore_.reset();
        btnAjudaDublinCore_.reset();
        btnCollapseDublinCore_.reset();
        colapsadoDublinCore_ = false;
        secHeaderUserAsset_.reset();
        btnAjudaUserAsset_.reset();
        btnCollapseUserAsset_.reset();
        colapsadoUserAsset_ = false;
        btnAjudaGeoLocation_.reset();
        btnCollapseGeoLocation_.reset();
        colapsadoGeoLocation_ = false;
        itemId_.clear();
        setSize(getWidth(), 0);
    }

    void construirParaItem(const std::string& itemId) {
        limpar();
        itemId_ = itemId;
        if (itemId.empty()) {
            repaint();
            return;
        }

        std::string tituloStd, tipoMidia, codigoAcervo;
        if (!projeto_.obterItemInfo(itemId, tituloStd, tipoMidia, codigoAcervo)) return;
        if (tipoMidia.empty()) {
            std::string ext;
            std::set<std::string> setIds{itemId};
            auto details = projeto_.obterDetalhesItens(setIds);
            if (!details.empty()) ext = details[0].extensao;
            auto cat = matriz::ingest::categoriaPorExtensao(juce::String(ext));
            if (cat == matriz::ingest::CategoriaMidia::Audio) tipoMidia = "digital_audio";
            else if (cat == matriz::ingest::CategoriaMidia::Video) tipoMidia = "digital_video";
            else if (cat == matriz::ingest::CategoriaMidia::Imagem) tipoMidia = "foto";
            else tipoMidia = "documento";
        }
        tipoAtual_ = tipoMidia;

        construirMetadadosUnificados(itemId, tipoMidia);
        construirSecaoGeolocalizacao(itemId);

        {
            juce::StringArray faltando;
            if (projeto_.lerMetadado(itemId, "ano").value_or("").empty()) faltando.add("YEAR");
            if (projeto_.lerMetadado(itemId, "source_media").value_or("").empty()) faltando.add("SOURCE MEDIA");
            if (projeto_.lerMetadado(itemId, "collection_type").value_or("").empty()) faltando.add("CONTENT");
            if (faltando.size() > 0) {
                labelReviewFaltando_ = std::make_unique<juce::Label>();
                labelReviewFaltando_->setText("Needs review: " + faltando.joinIntoString(", "),
                                              juce::dontSendNotification);
                labelReviewFaltando_->setColour(juce::Label::textColourId, juce::Colours::orange);
                labelReviewFaltando_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
                addAndMakeVisible(*labelReviewFaltando_);
            }
        }

        botaoAplicar_ = std::make_unique<juce::TextButton>("Apply");
        botaoAplicar_->onClick = [this] {
            if (itemId_.empty()) {
                DBG("APPLY ERROR: itemId_ is empty — nothing to save");
                return;
            }

            int saved = 0;
            int verified = 0;
            int errors = 0;
            juce::StringArray errorDetails;

            projeto_.iniciarGrupoUndo("Apply metadata");

            for (auto& cu : camposUnificados_) {
                if (!cu || cu->ehAutoFixed) continue;

                // Tags: save via TagChipsEditor directly
                if (cu->ehTags) {
                    if (auto* chips = dynamic_cast<TagChipsEditor*>(cu->editor.get())) {
                        try {
                            projeto_.definirTags(itemId_, chips->getTags());
                            ++saved;
                        } catch (const std::exception& e) {
                            ++errors;
                            errorDetails.add("Tags: " + juce::String(e.what()));
                        }
                    }
                    continue;
                }

                if (cu->colunaDb.empty()) continue;

                // Read current value from the UI widget
                std::string val;
                if (auto* ed = dynamic_cast<juce::TextEditor*>(cu->editor.get())) {
                    val = ed->getText().toStdString();
                } else if (auto* combo = dynamic_cast<juce::ComboBox*>(cu->editor.get())) {
                    val = combo->getText().toStdString();
                } else {
                    continue;
                }

                // Write to database
                try {
                    projeto_.salvarMetadado(itemId_, cu->colunaDb, val);
                    ++saved;
                } catch (const std::exception& e) {
                    ++errors;
                    errorDetails.add(juce::String(cu->colunaDb) + ": " + juce::String(e.what()));
                    continue;
                }

                // Verify: read back from database
                auto readBack = projeto_.lerMetadado(itemId_, cu->colunaDb);
                if (readBack.has_value() && readBack.value() == val) {
                    ++verified;
                } else {
                    ++errors;
                    errorDetails.add(juce::String(cu->colunaDb) + ": write succeeded but verify failed");
                }
            }

            projeto_.finalizarGrupoUndo();

            DBG("APPLY: item=" + juce::String(itemId_)
                + " saved=" + juce::String(saved)
                + " verified=" + juce::String(verified)
                + " errors=" + juce::String(errors));
            for (auto& e : errorDetails) DBG("  ERROR: " + e);

            salvarGeolocalizacao(itemId_);

            if (aoAplicarSucesso) aoAplicarSucesso(itemId_);

            if (aoMudar) aoMudar();

            // Visual feedback
            labelAplicado_ = std::make_unique<juce::Label>();
            if (errors > 0) {
                labelAplicado_->setText("SAVE FAILED: " + errorDetails.joinIntoString("; "), juce::dontSendNotification);
                labelAplicado_->setColour(juce::Label::textColourId, juce::Colours::red);
            } else {
                labelAplicado_->setText(juce::String(saved) + " fields saved.", juce::dontSendNotification);
                labelAplicado_->setColour(juce::Label::textColourId, juce::Colours::green.darker(0.2f));
            }
            labelAplicado_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
            addAndMakeVisible(*labelAplicado_);
            relayoutEExibir();
            juce::Component::SafePointer<FichaConteudo> safeThis(this);
            juce::Timer::callAfterDelay(3000, [safeThis] {
                if (!safeThis) return;
                safeThis->labelAplicado_.reset();
                safeThis->relayoutEExibir();
            });
        };
        addAndMakeVisible(*botaoAplicar_);
        relayoutEExibir();
    }

    void construirSeletorTipoMidia(const std::string& itemId, bool jaClassificado = false) {
        mensagemNaoClassificado_ = std::make_unique<juce::Label>();
        mensagemNaoClassificado_->setText(
            matriz::i18n::t(jaClassificado ? "ficha.recategorizar_titulo" : "ficha.nao_classificado"), juce::dontSendNotification);
        mensagemNaoClassificado_->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        mensagemNaoClassificado_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo)));
        addAndMakeVisible(*mensagemNaoClassificado_);

        std::string ext;
        auto details = projeto_.obterDetalhesItens({itemId});
        if (!details.empty()) ext = details[0].extensao;

        auto opcoes = ext.empty() ? listarTiposMidiaDisponiveis(projeto_)
                                  : listarTiposMidiaDisponiveisParaExtensoes(projeto_, {ext});

        for (auto& opcao : opcoes) {
            auto botao = std::make_unique<juce::TextButton>(opcao.rotulo);
            std::string tipoId = opcao.id;
            botao->onClick = [this, itemId, tipoId] {
                projeto_.atualizarTipoMidia(itemId, tipoId);
                recategorizando_ = false;
                if (aoMudarClassificacao) aoMudarClassificacao();
                construirParaItem(itemId);
                relayoutEExibir();
            };
            addAndMakeVisible(*botao);
            botoesTipoNaoClassificado_.push_back(std::move(botao));
        }

        if (jaClassificado) {
            botaoCancelarRecategorizar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("comum.cancelar"));
            botaoCancelarRecategorizar_->onClick = [this] {
                recategorizando_ = false;
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*botaoCancelarRecategorizar_);
        }
    }

    void relayout(int largura) {
        if (itemId_.empty()) {
            quadroDublinCore_ = {};
            quadroUserAsset_ = {};
            quadroGeoLocation_ = {};
            setSize(largura, 0);
            return;
        }
        const auto& tk = matriz::ui::tema();
        int y = tk.espacoPainel;
        int x = tk.espacoPainel;
        int larguraUtil = largura - 2 * tk.espacoPainel;

        if (cabecalho_) {
            cabecalho_->setBounds(x, y, larguraUtil, 24);
            y += 24 + tk.espacoPequeno;
        }

        if (botaoRecategorizar_) {
            botaoRecategorizar_->setBounds(x, y, 140, 24);
            y += 24 + tk.espacoPequeno;
        }

        if (cabecalho_ || botaoRecategorizar_) {
            y += tk.espacoMedio;
        }

        if (mensagemNaoClassificado_) {
            mensagemNaoClassificado_->setBounds(x, y, larguraUtil, 40);
            y += 40 + tk.espacoMedio;
            for (auto& b : botoesTipoNaoClassificado_) {
                b->setBounds(x, y, larguraUtil, 26);
                y += 26 + tk.espacoPequeno;
            }
            if (botaoCancelarRecategorizar_) {
                botaoCancelarRecategorizar_->setBounds(x, y, larguraUtil, 26);
                y += 26 + tk.espacoPequeno;
            }
            setSize(largura, y + tk.espacoPainel);
            return;
        }

        const int padCardX = 12;
        const int padCardY = 10;
        bool ehDuasColunas = (larguraUtil >= 500);

        if (ehDuasColunas) {
            int innerW = larguraUtil - 2 * padCardX;
            int gap = 16;
            int colW = (innerW - gap) / 2;
            int x0 = x + padCardX;
            int x1 = x + padCardX + colW + gap;

            auto layoutCamposDoBloco = [&](BlocoFicha bloco, int& y0, int& y1) {
                for (auto& cu : camposUnificados_) {
                    if (!cu || cu->bloco != bloco) continue;

                    bool useCol1 = (y1 < y0);
                    int currX = useCol1 ? x1 : x0;
                    int& currY = useCol1 ? y1 : y0;

                    int rotuloW = colW - 85;
                    cu->rotulo->setBounds(currX, currY, rotuloW, 16);
                    cu->badge->setBounds(currX + colW - 80, currY, 80, 16);
                    currY += 18;

                    if (cu->ehOriginalSourceMedium) {
                        if (auto* osm = dynamic_cast<OriginalSourceMediumEditorComponent*>(cu->editor.get())) {
                            int prefH = osm->getPreferredHeight();
                            osm->setBounds(currX, currY, colW, prefH);
                            currY += prefH + tk.espacoPequeno;
                        } else {
                            cu->editor->setBounds(currX, currY, colW, 24);
                            currY += 24 + tk.espacoPequeno;
                        }
                    } else if (cu->ehPeople) {
                        cu->editor->setBounds(currX, currY, colW, 26);
                        currY += 26 + tk.espacoPequeno;
                    } else if (cu->ehTags) {
                        if (auto* chips = dynamic_cast<TagChipsEditor*>(cu->editor.get())) {
                            chips->setBounds(currX, currY, colW, chips->getPreferredHeight());
                            currY += chips->getPreferredHeight() + tk.espacoPequeno;
                        } else {
                            cu->editor->setBounds(currX, currY, colW, 24);
                            currY += 24 + tk.espacoPequeno;
                        }
                    } else if (cu->ehNotes) {
                        cu->editor->setBounds(currX, currY, colW, 64);
                        currY += 64 + tk.espacoPequeno;
                    } else {
                        cu->editor->setBounds(currX, currY, colW, 24);
                        currY += 24 + tk.espacoPequeno;
                    }
                }
            };

            // --- Bloco A: DUBLIN CORE METADATA ---
            int cardATop = y;
            if (secHeaderDublinCore_) {
                int btnW = 20;
                int btnH = 18;
                int rightX = x + padCardX + innerW;
                if (btnCollapseDublinCore_) {
                    rightX -= btnW;
                    btnCollapseDublinCore_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                    rightX -= 4;
                }
                if (btnAjudaDublinCore_) {
                    rightX -= btnW;
                    btnAjudaDublinCore_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                    rightX -= 8;
                }
                secHeaderDublinCore_->setBounds(x + padCardX, y + padCardY, std::max(20, rightX - (x + padCardX)), 20);
                y += padCardY + 24;
            } else {
                y += padCardY;
            }
            if (!colapsadoDublinCore_) {
                int y0 = y;
                int y1 = y;
                layoutCamposDoBloco(BlocoFicha::DublinCore, y0, y1);
                int cardABottom = std::max(y0, y1) + padCardY;
                quadroDublinCore_ = juce::Rectangle<int>(x, cardATop, larguraUtil, cardABottom - cardATop);
                y = cardABottom + tk.espacoMedio;
            } else {
                int cardABottom = y;
                quadroDublinCore_ = juce::Rectangle<int>(x, cardATop, larguraUtil, cardABottom - cardATop);
                y = cardABottom + tk.espacoPequeno;
            }

            // --- Bloco B: ASSET & USER METADATA (PEOPLE right above TAGS) ---
            int cardBTop = y;
            if (secHeaderUserAsset_) {
                int btnW = 20;
                int btnH = 18;
                int rightX = x + padCardX + innerW;
                if (btnCollapseUserAsset_) {
                    rightX -= btnW;
                    btnCollapseUserAsset_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                    rightX -= 4;
                }
                if (btnAjudaUserAsset_) {
                    rightX -= btnW;
                    btnAjudaUserAsset_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                    rightX -= 8;
                }
                secHeaderUserAsset_->setBounds(x + padCardX, y + padCardY, std::max(20, rightX - (x + padCardX)), 20);
                y += padCardY + 24;
            } else {
                y += padCardY;
            }
            if (!colapsadoUserAsset_) {
                int y0 = y;
                int y1 = y;
                layoutCamposDoBloco(BlocoFicha::UserAsset, y0, y1);
                int cardBBottom = std::max(y0, y1) + padCardY;
                quadroUserAsset_ = juce::Rectangle<int>(x, cardBTop, larguraUtil, cardBBottom - cardBTop);
                y = cardBBottom + tk.espacoMedio;
            } else {
                int cardBBottom = y;
                quadroUserAsset_ = juce::Rectangle<int>(x, cardBTop, larguraUtil, cardBBottom - cardBTop);
                y = cardBBottom + tk.espacoPequeno;
            }

            // --- Bloco C: GEOLOCATION (Last in metadata queue) ---
            if (geolocalizacao_.titulo) {
                int cardCTop = y;
                int btnW = 20;
                int btnH = 18;
                int rightX = x + padCardX + innerW;
                if (btnCollapseGeoLocation_) {
                    rightX -= btnW;
                    btnCollapseGeoLocation_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                    rightX -= 4;
                }
                if (btnAjudaGeoLocation_) {
                    rightX -= btnW;
                    btnAjudaGeoLocation_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                    rightX -= 8;
                }
                if (geolocalizacao_.statusBadge) {
                    rightX -= 145;
                    geolocalizacao_.statusBadge->setBounds(rightX, y + padCardY, 145, 20);
                    rightX -= 8;
                }
                geolocalizacao_.titulo->setBounds(x + padCardX, y + padCardY, std::max(20, rightX - (x + padCardX)), 20);
                y += padCardY + 24;

                if (!colapsadoGeoLocation_) {
                    int y0 = y;
                    int y1 = y;
                    auto layoutGeoField = [&](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed) {
                        if (lbl && ed) {
                            bool useCol1 = (y1 < y0);
                            int currX = useCol1 ? x1 : x0;
                            int& currY = useCol1 ? y1 : y0;

                            lbl->setBounds(currX, currY, colW, 16);
                            currY += 18;
                            ed->setBounds(currX, currY, colW, 24);
                            currY += 24 + tk.espacoPequeno;
                        }
                    };
                    layoutGeoField(geolocalizacao_.labelCoords, geolocalizacao_.editorCoords);
                    layoutGeoField(geolocalizacao_.labelAddress, geolocalizacao_.editorAddress);
                    layoutGeoField(geolocalizacao_.labelCity, geolocalizacao_.editorCity);
                    layoutGeoField(geolocalizacao_.labelState, geolocalizacao_.editorState);
                    layoutGeoField(geolocalizacao_.labelCountry, geolocalizacao_.editorCountry);

                    int cardCBottom = std::max(y0, y1) + padCardY;
                    quadroGeoLocation_ = juce::Rectangle<int>(x, cardCTop, larguraUtil, cardCBottom - cardCTop);
                    y = cardCBottom + tk.espacoMedio;
                } else {
                    int cardCBottom = y;
                    quadroGeoLocation_ = juce::Rectangle<int>(x, cardCTop, larguraUtil, cardCBottom - cardCTop);
                    y = cardCBottom + tk.espacoPequeno;
                }
            } else {
                quadroGeoLocation_ = {};
            }

            int maxY = y;

            if (labelReviewFaltando_) {
                maxY += tk.espacoPequeno;
                labelReviewFaltando_->setBounds(x, maxY, larguraUtil, 18);
                maxY += 18 + tk.espacoPequeno;
            }

            if (botaoAplicar_) {
                maxY += tk.espacoPequeno;
                botaoAplicar_->setBounds(x, maxY, 120, 28);
                maxY += 28 + tk.espacoPequeno;
            }
            if (labelAplicado_) {
                labelAplicado_->setBounds(x, maxY, larguraUtil, 18);
                maxY += 18 + tk.espacoPequeno;
            }

            setSize(largura, maxY + tk.espacoPainel);
            return;
        }

        // --- Single Column Layout with distinct block sections ---
        int innerW = larguraUtil - 2 * padCardX;
        auto layoutCamposDoBloco1Col = [&](BlocoFicha bloco) {
            for (auto& cu : camposUnificados_) {
                if (!cu || cu->bloco != bloco) continue;
                int rotuloW = innerW - 100;
                cu->rotulo->setBounds(x + padCardX, y, rotuloW, 16);
                cu->badge->setBounds(x + padCardX + innerW - 95, y, 95, 16);
                y += 18;

                if (cu->ehOriginalSourceMedium) {
                    if (auto* osm = dynamic_cast<OriginalSourceMediumEditorComponent*>(cu->editor.get())) {
                        int prefH = osm->getPreferredHeight();
                        osm->setBounds(x + padCardX, y, innerW, prefH);
                        y += prefH + tk.espacoPequeno;
                    } else {
                        cu->editor->setBounds(x + padCardX, y, innerW, 24);
                        y += 24 + tk.espacoPequeno;
                    }
                } else if (cu->ehPeople) {
                    cu->editor->setBounds(x + padCardX, y, innerW, 26);
                    y += 26 + tk.espacoPequeno;
                } else if (cu->ehTags) {
                    if (auto* chips = dynamic_cast<TagChipsEditor*>(cu->editor.get())) {
                        chips->setBounds(x + padCardX, y, innerW, chips->getPreferredHeight());
                        y += chips->getPreferredHeight() + tk.espacoPequeno;
                    } else {
                        cu->editor->setBounds(x + padCardX, y, innerW, 24);
                        y += 24 + tk.espacoPequeno;
                    }
                } else if (cu->ehNotes) {
                    cu->editor->setBounds(x + padCardX, y, innerW, 64);
                    y += 64 + tk.espacoPequeno;
                } else {
                    cu->editor->setBounds(x + padCardX, y, innerW, 24);
                    y += 24 + tk.espacoPequeno;
                }
            }
        };

        // Bloco A
        int cardATop1 = y;
        if (secHeaderDublinCore_) {
            int btnW = 20;
            int btnH = 18;
            int rightX = x + padCardX + innerW;
            if (btnCollapseDublinCore_) {
                rightX -= btnW;
                btnCollapseDublinCore_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                rightX -= 4;
            }
            if (btnAjudaDublinCore_) {
                rightX -= btnW;
                btnAjudaDublinCore_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                rightX -= 8;
            }
            secHeaderDublinCore_->setBounds(x + padCardX, y + padCardY, std::max(20, rightX - (x + padCardX)), 20);
            y += padCardY + 24;
        } else {
            y += padCardY;
        }
        if (!colapsadoDublinCore_) {
            layoutCamposDoBloco1Col(BlocoFicha::DublinCore);
            int cardABottom1 = y + padCardY;
            quadroDublinCore_ = juce::Rectangle<int>(x, cardATop1, larguraUtil, cardABottom1 - cardATop1);
            y = cardABottom1 + tk.espacoMedio;
        } else {
            int cardABottom1 = y;
            quadroDublinCore_ = juce::Rectangle<int>(x, cardATop1, larguraUtil, cardABottom1 - cardATop1);
            y = cardABottom1 + tk.espacoPequeno;
        }

        // Bloco B
        int cardBTop1 = y;
        if (secHeaderUserAsset_) {
            int btnW = 20;
            int btnH = 18;
            int rightX = x + padCardX + innerW;
            if (btnCollapseUserAsset_) {
                rightX -= btnW;
                btnCollapseUserAsset_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                rightX -= 4;
            }
            if (btnAjudaUserAsset_) {
                rightX -= btnW;
                btnAjudaUserAsset_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                rightX -= 8;
            }
            secHeaderUserAsset_->setBounds(x + padCardX, y + padCardY, std::max(20, rightX - (x + padCardX)), 20);
            y += padCardY + 24;
        } else {
            y += padCardY;
        }
        if (!colapsadoUserAsset_) {
            layoutCamposDoBloco1Col(BlocoFicha::UserAsset);
            int cardBBottom1 = y + padCardY;
            quadroUserAsset_ = juce::Rectangle<int>(x, cardBTop1, larguraUtil, cardBBottom1 - cardBTop1);
            y = cardBBottom1 + tk.espacoMedio;
        } else {
            int cardBBottom1 = y;
            quadroUserAsset_ = juce::Rectangle<int>(x, cardBTop1, larguraUtil, cardBBottom1 - cardBTop1);
            y = cardBBottom1 + tk.espacoPequeno;
        }

        // Bloco C: GEOLOCATION
        if (geolocalizacao_.titulo) {
            int cardCTop1 = y;
            int btnW = 20;
            int btnH = 18;
            int rightX = x + padCardX + innerW;
            if (btnCollapseGeoLocation_) {
                rightX -= btnW;
                btnCollapseGeoLocation_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                rightX -= 4;
            }
            if (btnAjudaGeoLocation_) {
                rightX -= btnW;
                btnAjudaGeoLocation_->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                rightX -= 8;
            }
            if (geolocalizacao_.statusBadge) {
                rightX -= 135;
                geolocalizacao_.statusBadge->setBounds(rightX, y + padCardY, 135, 20);
                rightX -= 8;
            }
            geolocalizacao_.titulo->setBounds(x + padCardX, y + padCardY, std::max(20, rightX - (x + padCardX)), 20);
            y += padCardY + 24;

            if (!colapsadoGeoLocation_) {
                if (geolocalizacao_.labelCoords && geolocalizacao_.editorCoords) {
                    geolocalizacao_.labelCoords->setBounds(x + padCardX, y, innerW, 16);
                    y += 18;
                    geolocalizacao_.editorCoords->setBounds(x + padCardX, y, innerW, 24);
                    y += 24 + tk.espacoPequeno;
                }

                if (geolocalizacao_.labelAddress && geolocalizacao_.editorAddress) {
                    geolocalizacao_.labelAddress->setBounds(x + padCardX, y, innerW, 16);
                    y += 18;
                    geolocalizacao_.editorAddress->setBounds(x + padCardX, y, innerW, 24);
                    y += 24 + tk.espacoPequeno;
                }

                if (geolocalizacao_.labelCity && geolocalizacao_.editorCity) {
                    geolocalizacao_.labelCity->setBounds(x + padCardX, y, innerW, 16);
                    y += 18;
                    geolocalizacao_.editorCity->setBounds(x + padCardX, y, innerW, 24);
                    y += 24 + tk.espacoPequeno;
                }

                if (geolocalizacao_.labelState && geolocalizacao_.editorState) {
                    geolocalizacao_.labelState->setBounds(x + padCardX, y, innerW, 16);
                    y += 18;
                    geolocalizacao_.editorState->setBounds(x + padCardX, y, innerW, 24);
                    y += 24 + tk.espacoPequeno;
                }

                if (geolocalizacao_.labelCountry && geolocalizacao_.editorCountry) {
                    geolocalizacao_.labelCountry->setBounds(x + padCardX, y, innerW, 16);
                    y += 18;
                    geolocalizacao_.editorCountry->setBounds(x + padCardX, y, innerW, 24);
                    y += 24 + tk.espacoMedio;
                }
                int cardCBottom1 = y + padCardY;
                quadroGeoLocation_ = juce::Rectangle<int>(x, cardCTop1, larguraUtil, cardCBottom1 - cardCTop1);
                y = cardCBottom1 + tk.espacoMedio;
            } else {
                int cardCBottom1 = y;
                quadroGeoLocation_ = juce::Rectangle<int>(x, cardCTop1, larguraUtil, cardCBottom1 - cardCTop1);
                y = cardCBottom1 + tk.espacoPequeno;
            }
        } else {
            quadroGeoLocation_ = {};
        }

        if (labelReviewFaltando_) {
            y += tk.espacoMedio;
            labelReviewFaltando_->setBounds(x, y, larguraUtil, 18);
            y += 18 + tk.espacoPequeno;
        }

        if (botaoAplicar_) {
            y += tk.espacoMedio;
            botaoAplicar_->setBounds(x, y, 120, 28);
            y += 28 + tk.espacoPequeno;
        }
        if (labelAplicado_) {
            labelAplicado_->setBounds(x, y, larguraUtil, 18);
            y += 18 + tk.espacoPequeno;
        }

        y += tk.espacoMedio;

        setSize(largura, y + tk.espacoPainel);

        // --- PRESERVATION section layout ---
        if (preservation_.titulo) {
            y += tk.espacoMedio * 2;
            preservation_.titulo->setBounds(x, y, larguraUtil, 20);
            y += 20 + tk.espacoPequeno;

            if (preservation_.labelPersistentId) {
                preservation_.labelPersistentId->setBounds(x, y, larguraUtil - 80, 18);
                if (preservation_.botaoCopiarId)
                    preservation_.botaoCopiarId->setBounds(x + larguraUtil - 75, y, 75, 18);
                y += 18 + tk.espacoPequeno;
            }
            if (preservation_.labelStatusGeral) {
                preservation_.labelStatusGeral->setBounds(x, y, larguraUtil, 18);
                y += 18 + tk.espacoPequeno;
            }
            if (preservation_.labelSha256) {
                preservation_.labelSha256->setBounds(x, y, larguraUtil, 16);
                y += 16 + tk.espacoPequeno;
            }
            if (preservation_.labelUltimaVerificacao) {
                preservation_.labelUltimaVerificacao->setBounds(x, y, larguraUtil, 16);
                y += 16 + tk.espacoPequeno;
            }
            if (preservation_.labelVerificando) {
                preservation_.labelVerificando->setBounds(x, y, larguraUtil, 16);
                y += 16 + tk.espacoPequeno;
            }
            // Botões de ação
            int bx = x;
            if (preservation_.botaoVerificarFixity) {
                preservation_.botaoVerificarFixity->setBounds(bx, y, 150, 26);
                bx += 156;
            }
            if (preservation_.botaoExportJson) {
                preservation_.botaoExportJson->setBounds(bx, y, 100, 26);
                bx += 106;
            }
            if (preservation_.botaoExportCsv) {
                preservation_.botaoExportCsv->setBounds(bx, y, 100, 26);
            }
            if (preservation_.botaoVerificarFixity || preservation_.botaoExportJson)
                y += 26 + tk.espacoMedio;

            // Rights
            if (preservation_.tituloRights) {
                preservation_.tituloRights->setBounds(x, y, larguraUtil, 18);
                y += 18 + tk.espacoPequeno;
                if (preservation_.comboRights) {
                    preservation_.comboRights->setBounds(x, y, larguraUtil, 24);
                    y += 24 + tk.espacoPequeno;
                }
                if (preservation_.editorHolder) {
                    preservation_.editorHolder->setBounds(x, y, larguraUtil, 22);
                    y += 22 + tk.espacoPequeno;
                }
                if (preservation_.editorLicense) {
                    preservation_.editorLicense->setBounds(x, y, larguraUtil, 22);
                    y += 22 + tk.espacoPequeno;
                }
                if (preservation_.editorNotes) {
                    preservation_.editorNotes->setBounds(x, y, larguraUtil, 44);
                    y += 44 + tk.espacoPequeno;
                }
                if (preservation_.botaoSalvarRights) {
                    preservation_.botaoSalvarRights->setBounds(x, y, 80, 24);
                    y += 24 + tk.espacoMedio;
                }
            }

            // Event History
            if (preservation_.tituloEventos) {
                preservation_.tituloEventos->setBounds(x, y, larguraUtil, 18);
                y += 18 + tk.espacoPequeno;
                for (auto& lbl : preservation_.linhasEvento) {
                    lbl->setBounds(x, y, larguraUtil, 16);
                    y += 16 + 2;
                }
                y += tk.espacoPequeno;
            }

            setSize(largura, y + tk.espacoPainel);
        }
    }

    void construirSecaoGeolocalizacao(const std::string& itemId) {
        geolocalizacao_ = {};
        const auto& tk = matriz::ui::tema();
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

        auto geoOpt = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(projeto_.projeto().registro(), itemId);

        geolocalizacao_.titulo = std::make_unique<juce::Label>();
        geolocalizacao_.titulo->setText(isPt ? juce::String::fromUTF8("GEOLOCALIZAÇÃO") : juce::String("GEO LOCATION"), juce::dontSendNotification);
        geolocalizacao_.titulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        geolocalizacao_.titulo->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.titulo);

        geolocalizacao_.statusBadge = std::make_unique<juce::Label>();
        if (!geoOpt || geoOpt->source == matriz::analytics::GeoSource::None) {
            geolocalizacao_.statusBadge->setText(isPt ? juce::String::fromUTF8("[ SEM DADOS GPS - PREENCHIMENTO MANUAL ]") : juce::String("[ NO GPS DATA - MANUAL FILL ]"), juce::dontSendNotification);
            geolocalizacao_.statusBadge->setColour(juce::Label::textColourId, tk.textoTerciario);
        } else if (geoOpt->source == matriz::analytics::GeoSource::EmbeddedMetadata) {
            geolocalizacao_.statusBadge->setText(isPt ? juce::String::fromUTF8("[ GPS EXIF EXTRAÍDO AUTOMATICAMENTE ]") : juce::String("[ EXIF GPS AUTO-EXTRACTED ]"), juce::dontSendNotification);
            geolocalizacao_.statusBadge->setColour(juce::Label::textColourId, juce::Colours::lightgreen);
        } else {
            geolocalizacao_.statusBadge->setText(isPt ? juce::String::fromUTF8("[ GEOLOCALIZAÇÃO DEFINIDA PELO USUÁRIO ]") : juce::String("[ USER-DEFINED GEOLOCATION ]"), juce::dontSendNotification);
            geolocalizacao_.statusBadge->setColour(juce::Label::textColourId, juce::Colours::cyan);
        }
        geolocalizacao_.statusBadge->setFont(juce::Font(juce::FontOptions(9.0f)));
        geolocalizacao_.statusBadge->setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(*geolocalizacao_.statusBadge);

        // Coordinates (Lat, Lng)
        geolocalizacao_.labelCoords = std::make_unique<juce::Label>();
        geolocalizacao_.labelCoords->setText(isPt ? "Coordenadas GPS (Lat, Long)" : "GPS Coordinates (Lat, Lng)", juce::dontSendNotification);
        geolocalizacao_.labelCoords->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelCoords->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelCoords);

        geolocalizacao_.editorCoords = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorCoords->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorCoords->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        geolocalizacao_.editorCoords->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        geolocalizacao_.editorCoords->setColour(juce::TextEditor::outlineColourId, tk.borda);
        if (geoOpt && geoOpt->hasValidCoordinates()) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(6) << *geoOpt->latitude << ", " << *geoOpt->longitude;
            geolocalizacao_.editorCoords->setText(ss.str());
        } else {
            geolocalizacao_.editorCoords->setTextToShowWhenEmpty("e.g. -16.4435, -39.0643", juce::Colour(0xff888888));
        }
        addAndMakeVisible(*geolocalizacao_.editorCoords);

        // Address
        geolocalizacao_.labelAddress = std::make_unique<juce::Label>();
        geolocalizacao_.labelAddress->setText(isPt ? juce::String::fromUTF8("Endereço Formatado") : juce::String("Formatted Address"), juce::dontSendNotification);
        geolocalizacao_.labelAddress->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelAddress->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelAddress);

        geolocalizacao_.editorAddress = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorAddress->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorAddress->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        geolocalizacao_.editorAddress->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        geolocalizacao_.editorAddress->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorAddress->setText(geoOpt && geoOpt->formattedAddress ? *geoOpt->formattedAddress : "");
        geolocalizacao_.editorAddress->setTextToShowWhenEmpty("e.g. Av. Paulista, 1000", juce::Colour(0xff888888));
        addAndMakeVisible(*geolocalizacao_.editorAddress);

        // City
        geolocalizacao_.labelCity = std::make_unique<juce::Label>();
        geolocalizacao_.labelCity->setText(isPt ? juce::String::fromUTF8("Cidade") : juce::String("City"), juce::dontSendNotification);
        geolocalizacao_.labelCity->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelCity->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelCity);

        geolocalizacao_.editorCity = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorCity->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorCity->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        geolocalizacao_.editorCity->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        geolocalizacao_.editorCity->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorCity->setText(geoOpt && geoOpt->city ? *geoOpt->city : "");
        geolocalizacao_.editorCity->setTextToShowWhenEmpty("e.g. Porto Seguro", juce::Colour(0xff888888));
        addAndMakeVisible(*geolocalizacao_.editorCity);

        // State
        geolocalizacao_.labelState = std::make_unique<juce::Label>();
        geolocalizacao_.labelState->setText(isPt ? juce::String::fromUTF8("Estado / Província") : juce::String("State / Province"), juce::dontSendNotification);
        geolocalizacao_.labelState->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelState->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelState);

        geolocalizacao_.editorState = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorState->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorState->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        geolocalizacao_.editorState->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        geolocalizacao_.editorState->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorState->setText(geoOpt && geoOpt->stateProvince ? *geoOpt->stateProvince : "");
        geolocalizacao_.editorState->setTextToShowWhenEmpty("e.g. Bahia", juce::Colour(0xff888888));
        addAndMakeVisible(*geolocalizacao_.editorState);

        // Country
        geolocalizacao_.labelCountry = std::make_unique<juce::Label>();
        geolocalizacao_.labelCountry->setText(isPt ? juce::String::fromUTF8("País") : juce::String("Country"), juce::dontSendNotification);
        geolocalizacao_.labelCountry->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelCountry->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelCountry);

        geolocalizacao_.editorCountry = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorCountry->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorCountry->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        geolocalizacao_.editorCountry->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        geolocalizacao_.editorCountry->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorCountry->setText(geoOpt && geoOpt->country ? *geoOpt->country : "");
        geolocalizacao_.editorCountry->setTextToShowWhenEmpty(isPt ? "Ex: Brasil" : "e.g. Brazil", juce::Colour(0xff888888));
        addAndMakeVisible(*geolocalizacao_.editorCountry);

        btnAjudaGeoLocation_ = std::make_unique<juce::TextButton>("?");
        btnAjudaGeoLocation_->setTooltip(
            isPt ? juce::String::fromUTF8("GEOLOCALIZAÇÃO\n"
                                         "Coordenadas geográficas espaciais (Latitude e Longitude) e endereço estruturado do item.\n"
                                         "Preenchido automaticamente a partir de dados EXIF GPS da câmera ou definido manualmente para catalogação territorial.")
                 : juce::String("GEOLOCATION\n"
                                "Geographic coordinates (Latitude, Longitude) and structured address for this asset.\n"
                                "Extracted automatically from camera EXIF GPS data or entered manually for territorial cataloging."));
        btnAjudaGeoLocation_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnAjudaGeoLocation_->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
        btnAjudaGeoLocation_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        addAndMakeVisible(*btnAjudaGeoLocation_);

        btnCollapseGeoLocation_ = std::make_unique<juce::TextButton>();
        btnCollapseGeoLocation_->setButtonText(colapsadoGeoLocation_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
        btnCollapseGeoLocation_->setTooltip(colapsadoGeoLocation_ ? (isPt ? "Expandir" : "Expand") : (isPt ? "Recolher" : "Collapse"));
        btnCollapseGeoLocation_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btnCollapseGeoLocation_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnCollapseGeoLocation_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        btnCollapseGeoLocation_->onClick = [this, isPt] {
            colapsadoGeoLocation_ = !colapsadoGeoLocation_;
            btnCollapseGeoLocation_->setButtonText(colapsadoGeoLocation_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
            btnCollapseGeoLocation_->setTooltip(colapsadoGeoLocation_ ? (isPt ? "Expandir" : "Expand") : (isPt ? "Recolher" : "Collapse"));
            bool vis = !colapsadoGeoLocation_;
            if (geolocalizacao_.labelCoords) geolocalizacao_.labelCoords->setVisible(vis);
            if (geolocalizacao_.editorCoords) geolocalizacao_.editorCoords->setVisible(vis);
            if (geolocalizacao_.labelAddress) geolocalizacao_.labelAddress->setVisible(vis);
            if (geolocalizacao_.editorAddress) geolocalizacao_.editorAddress->setVisible(vis);
            if (geolocalizacao_.labelCity) geolocalizacao_.labelCity->setVisible(vis);
            if (geolocalizacao_.editorCity) geolocalizacao_.editorCity->setVisible(vis);
            if (geolocalizacao_.labelState) geolocalizacao_.labelState->setVisible(vis);
            if (geolocalizacao_.editorState) geolocalizacao_.editorState->setVisible(vis);
            if (geolocalizacao_.labelCountry) geolocalizacao_.labelCountry->setVisible(vis);
            if (geolocalizacao_.editorCountry) geolocalizacao_.editorCountry->setVisible(vis);
            if (aoRelayoutNecessario) aoRelayoutNecessario();
            repaint();
        };
        addAndMakeVisible(*btnCollapseGeoLocation_);
    }

    void salvarGeolocalizacao(const std::string& itemId) {
        if (!geolocalizacao_.editorCoords) return;
        matriz::analytics::AssetGeolocation geo;
        geo.assetId = itemId;

        std::string coordsText = geolocalizacao_.editorCoords->getText().toStdString();
        if (!coordsText.empty()) {
            auto commaPos = coordsText.find(',');
            if (commaPos != std::string::npos) {
                try {
                    double lat = std::stod(coordsText.substr(0, commaPos));
                    double lng = std::stod(coordsText.substr(commaPos + 1));
                    if (lat >= -90.0 && lat <= 90.0 && lng >= -180.0 && lng <= 180.0) {
                        geo.latitude = lat;
                        geo.longitude = lng;
                        geo.source = matriz::analytics::GeoSource::UserCoordinates;
                    }
                } catch (...) {}
            }
        }

        std::string addr = geolocalizacao_.editorAddress->getText().toStdString();
        if (!addr.empty()) {
            geo.formattedAddress = addr;
            if (geo.source == matriz::analytics::GeoSource::None) geo.source = matriz::analytics::GeoSource::UserAddress;
        }

        std::string city = geolocalizacao_.editorCity->getText().toStdString();
        if (!city.empty()) {
            geo.city = city;
            if (geo.source == matriz::analytics::GeoSource::None) geo.source = matriz::analytics::GeoSource::UserCity;
        }

        std::string state = geolocalizacao_.editorState->getText().toStdString();
        if (!state.empty()) {
            geo.stateProvince = state;
            if (geo.source == matriz::analytics::GeoSource::None) geo.source = matriz::analytics::GeoSource::UserState;
        }

        std::string country = geolocalizacao_.editorCountry->getText().toStdString();
        if (!country.empty()) {
            geo.country = country;
            if (geo.source == matriz::analytics::GeoSource::None) geo.source = matriz::analytics::GeoSource::UserCountry;
        }

        if (geo.hasAnyLocationData()) {
            matriz::analytics::AssetGeolocationRepository::salvar(projeto_.projeto().registro(), geo);
            if (aoMudar) aoMudar();
            if (aoAplicarSucesso) aoAplicarSucesso(itemId);
        }
    }

    // -----------------------------------------------------------------------
    // construirSecaoPreservacao — chamada no final de construirParaItem
    // Somente leitura sobre os dados de preservação. Não altera arquivos.
    // -----------------------------------------------------------------------
    void construirSecaoPreservacao(const std::string&) {
        preservation_ = {};
    }

    std::function<void()> aoRelayoutNecessario;
    std::function<void()> aoMudarClassificacao;
    std::function<void()> aoMudar;
    std::function<void(const std::string&)> aoAplicarSucesso;

    juce::Component* editorDoCampoParaTeste(const std::string& nivel, int nivelIndice, const std::string& campoId) {
        for (auto& cu : camposUnificados_) {
            if (cu->campoId == campoId ||
                (campoId == "maquina" && cu->campoId == "name") ||
                (campoId == "titulo" && cu->campoId == "name") ||
                (campoId == "ano" && cu->campoId == "year") ||
                (campoId == "caminho" && cu->campoId == "path") ||
                (campoId == "notas" && cu->campoId == "notes")) {
                return cu->editor.get();
            }
        }
        for (auto* linha : linhas_) {
            if (linha->nivel == nivel && linha->nivelIndice == nivelIndice && linha->campo->id == campoId)
                return linha->editorTabela ? static_cast<juce::Component*>(linha->editorTabela.get())
                                            : linha->editorSimples.get();
        }
        return nullptr;
    }

    juce::TextButton* botaoTipoMidiaParaTeste(const std::string& tipoId) {
        auto opcoes = listarTiposMidiaDisponiveis(projeto_);
        for (size_t i = 0; i < opcoes.size() && i < botoesTipoNaoClassificado_.size(); ++i)
            if (opcoes[i].id == tipoId) return botoesTipoNaoClassificado_[i].get();
        return nullptr;
    }

private:
    enum class BlocoFicha {
        DublinCore,
        UserAsset,
        GeoLocation
    };

    struct LinhaUnificada {
        std::string campoId;
        std::string colunaDb;
        BlocoFicha bloco = BlocoFicha::DublinCore;
        bool ehAutoFixed = false;
        bool ehNotes = false;
        bool ehPeople = false;
        bool ehTags = false;
        bool ehOriginalSourceMedium = false;
        std::unique_ptr<juce::Label> rotulo;
        std::unique_ptr<juce::Label> badge;
        std::unique_ptr<juce::Component> editor;
        std::function<void()> onCommit;
    };
    std::vector<std::unique_ptr<LinhaUnificada>> camposUnificados_;
    std::unique_ptr<juce::Label> secHeaderDublinCore_;
    std::unique_ptr<juce::TextButton> btnAjudaDublinCore_;
    std::unique_ptr<juce::TextButton> btnCollapseDublinCore_;
    bool colapsadoDublinCore_ = false;

    std::unique_ptr<juce::Label> secHeaderUserAsset_;
    std::unique_ptr<juce::TextButton> btnAjudaUserAsset_;
    std::unique_ptr<juce::TextButton> btnCollapseUserAsset_;
    bool colapsadoUserAsset_ = false;

    std::unique_ptr<juce::TextButton> btnAjudaGeoLocation_;
    std::unique_ptr<juce::TextButton> btnCollapseGeoLocation_;
    bool colapsadoGeoLocation_ = false;

    void construirMetadadosUnificados(const std::string& itemId, const std::string& tipoMidia) {
        const auto& tk = matriz::ui::tema();
        auto arquivo = projeto_.arquivoPrincipal(itemId);
        juce::var dados;
        if (arquivo) {
            dados = juce::JSON::parse(arquivo->caracteristicasTecnicasJson);
        }

        std::string extStd;
        if (arquivo) {
            extStd = juce::File(arquivo->caminhoAbsoluto).getFileExtension().trimCharactersAtStart(".").toStdString();
        }

        MediaCategory cat = determinarCategoriaMidia(tipoMidia, extStd);

        juce::String ext = juce::String(extStd).toUpperCase();
        if (ext.isEmpty() && dados.isObject() && dados.hasProperty("bruto")) {
            ext = dados["bruto"]["format"].toString().toUpperCase();
        }
        if (ext.isEmpty()) ext = "FILE";

        // Technical extracted strings
        juce::String lengthStr;
        if (dados.isObject() && dados.hasProperty("duracaoSegundos")) {
            double seg = static_cast<double>(dados["duracaoSegundos"]);
            if (seg > 0.0) {
                int total = static_cast<int>(seg + 0.5);
                int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
                if (h > 0)
                    lengthStr = juce::String(h) + ":" + juce::String(m).paddedLeft('0', 2) + ":" + juce::String(s).paddedLeft('0', 2);
                else
                    lengthStr = juce::String(m).paddedLeft('0', 2) + ":" + juce::String(s).paddedLeft('0', 2);
            }
        }
        if (lengthStr.isEmpty()) lengthStr = "--:--";

        juce::String codecStr;
        if (dados.isObject() && dados.hasProperty("codec")) codecStr = dados["codec"].toString();
        if (codecStr.isEmpty()) codecStr = ext;

        juce::String sampleRateStr;
        if (dados.isObject() && dados.hasProperty("sampleRate")) {
            juce::int64 sr = static_cast<juce::int64>(dados["sampleRate"]);
            if (sr > 0) sampleRateStr = juce::String(sr) + " Hz";
        }
        if (sampleRateStr.isEmpty()) sampleRateStr = "48 000 Hz";

        juce::String bitDepthStr;
        if (dados.isObject() && dados.hasProperty("bitDepth")) {
            int bd = static_cast<int>(dados["bitDepth"]);
            if (bd > 0) bitDepthStr = juce::String(bd) + " bits";
        }
        if (bitDepthStr.isEmpty()) bitDepthStr = "24 bits";

        juce::String channelsStr;
        if (dados.isObject() && dados.hasProperty("canais")) {
            int ch = static_cast<int>(dados["canais"]);
            if (ch == 1) channelsStr = "Mono";
            else if (ch == 2) channelsStr = "Stereo";
            else if (ch > 2) channelsStr = juce::String(ch) + "-ch (Multi-channel)";
        }

        int larguraVal = 0;
        int alturaVal = 0;
        if (dados.isObject() && dados.hasProperty("larguraPx") && dados.hasProperty("alturaPx")) {
            larguraVal = static_cast<int>(dados["larguraPx"]);
            alturaVal = static_cast<int>(dados["alturaPx"]);
        }

        if ((larguraVal <= 0 || alturaVal <= 0) && arquivo) {
            juce::File f(arquivo->caminhoAbsoluto);
            if (f.existsAsFile()) {
                try {
                    auto image = Exiv2::ImageFactory::open(f.getFullPathName().toStdString());
                    image->readMetadata();
                    larguraVal = image->pixelWidth();
                    alturaVal = image->pixelHeight();
                } catch (...) {
                    auto img = juce::ImageFileFormat::loadFrom(f);
                    if (img.isValid()) {
                        larguraVal = img.getWidth();
                        alturaVal = img.getHeight();
                    }
                }
            }
        }
        
        int exifOrient = 1;
        if (dados.isObject() && dados.hasProperty("bruto")) {
            juce::var bruto = dados["bruto"];
            if (bruto.hasProperty("exif")) {
                juce::var exif = bruto["exif"];
                if (exif.hasProperty("Exif.Image.Orientation")) {
                    juce::String val = exif["Exif.Image.Orientation"].toString().trim().toLowerCase();
                    if (val.contains("6") || val.contains("8") || val.contains("5") || val.contains("7") ||
                        val.contains("right, top") || val.contains("left, bottom") ||
                        val.contains("left, top") || val.contains("right, bottom") ||
                        val.contains("90") || val.contains("270")) {
                        exifOrient = 6; // Triggers width/height swap
                    } else {
                        exifOrient = val.getIntValue();
                    }
                }
            }
        }
        
        if (exifOrient == 6 || exifOrient == 8 || exifOrient == 5 || exifOrient == 7) {
            std::swap(larguraVal, alturaVal);
        }
        
        if (larguraVal <= 0 || alturaVal <= 0) {
            larguraVal = 1920;
            alturaVal = 1080;
        }

        juce::String dimensionsStr = juce::String(larguraVal) + " x " + juce::String(alturaVal) + " px";

        juce::String orientationStr = "Horizontal";
        if (alturaVal > larguraVal) orientationStr = "Vertical";
        else if (alturaVal == larguraVal && larguraVal > 0) orientationStr = "Square";

        juce::String fileSizeStr;
        if (arquivo) {
            juce::File f(arquivo->caminhoAbsoluto);
            if (f.existsAsFile()) fileSizeStr = juce::File::descriptionOfSizeInBytes(f.getSize());
        }
        if (fileSizeStr.isEmpty() && dados.isObject() && dados.hasProperty("bruto")) {
            juce::var bruto = dados["bruto"];
            if (bruto.hasProperty("fileSizeBytes"))
                fileSizeStr = juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(bruto["fileSizeBytes"]));
        }
        if (fileSizeStr.isEmpty()) fileSizeStr = "4.2 MB";

        juce::String colorSpaceStr;
        if (dados.isObject() && dados.hasProperty("espacoCor")) colorSpaceStr = dados["espacoCor"].toString();
        if (colorSpaceStr.isEmpty()) colorSpaceStr = "sRGB";

        juce::String pagesStr;
        if (dados.isObject() && dados.hasProperty("paginas")) {
            int p = static_cast<int>(dados["paginas"]);
            pagesStr = juce::String(p) + (p == 1 ? " page" : " pages");
        }
        if (pagesStr.isEmpty()) pagesStr = "1 page";

        // Editable values
        std::string tituloStd, dummyTipo, dummyCod;
        projeto_.obterItemInfo(itemId, tituloStd, dummyTipo, dummyCod);

        juce::String valName = tituloStd;
        juce::String valPath = projeto_.lerMetadado(itemId, "caminho_catalogo").value_or(arquivo ? arquivo->caminhoAbsoluto.toStdString() : "");
        juce::String valYear = projeto_.lerMetadado(itemId, "ano").value_or("");
        juce::String valSourceMedia = projeto_.lerMetadado(itemId, "source_media").value_or("");
        if (valSourceMedia.isEmpty() && dados.isObject() && dados.hasProperty("exifCamera")) {
            OriginalSourceMediumInfo osm;
            osm.medium = "Native Digital";
            osm.recordingDevice = dados["exifCamera"].toString().toStdString();
            valSourceMedia = osm.serialize();
        }
        juce::String valCollection = projeto_.lerMetadado(itemId, "collection_type").value_or("");
        juce::String valIsrc = projeto_.lerMetadado(itemId, "isrc").value_or("");
        juce::String valNotes = projeto_.lerMetadado(itemId, "notas_livres").value_or("");

        std::vector<std::string> tagsList = projeto_.lerTags(itemId);

        // Extract native Dublin Core values from file / database
        juce::String valDcTitle = projeto_.lerMetadado(itemId, "dc_title").value_or("");
        if (valDcTitle.isEmpty()) valDcTitle = valName;

        juce::String valDcCreator = projeto_.lerMetadado(itemId, "dc_creator").value_or("");

        juce::String valDcSubject = projeto_.lerMetadado(itemId, "dc_subject").value_or("");

        juce::String valDcDescription = projeto_.lerMetadado(itemId, "dc_description").value_or("");

        juce::String valDcPublisher = projeto_.lerMetadado(itemId, "dc_publisher").value_or("");

        juce::String valDcContributor = projeto_.lerMetadado(itemId, "dc_contributor").value_or("");

        juce::String valDcCreated = projeto_.lerMetadado(itemId, "dc_created").value_or("");
        if (valDcCreated.isEmpty() && dados.isObject() && dados.hasProperty("exifDataOriginal")) {
            juce::String fullExifDate = dados["exifDataOriginal"].toString().trim();
            if (fullExifDate.length() >= 10) {
                juce::String dataParte = fullExifDate.substring(0, 10).replace(":", "-");
                juce::String horaParte = fullExifDate.length() > 10 ? fullExifDate.substring(10) : "";
                valDcCreated = dataParte + horaParte;
            } else {
                valDcCreated = fullExifDate;
            }
        }
        if (valDcCreated.isEmpty() && arquivo) {
            juce::File f(arquivo->caminhoAbsoluto);
            if (f.existsAsFile()) {
                valDcCreated = f.getCreationTime().formatted("%Y-%m-%d %H:%M:%S");
            }
        }

        juce::String valDcIssued = projeto_.lerMetadado(itemId, "dc_issued").value_or("");
        if (valDcIssued.isEmpty()) {
            valDcIssued = juce::Time::getCurrentTime().formatted("%Y-%m-%d");
        }

        juce::String valDcType = projeto_.lerMetadado(itemId, "dc_type").value_or("");
        if (valDcType.isEmpty()) {
            if (cat == MediaCategory::Audio) valDcType = "Sound";
            else if (cat == MediaCategory::Video) valDcType = "MovingImage";
            else if (cat == MediaCategory::Image) valDcType = "StillImage";
            else if (cat == MediaCategory::Docs) valDcType = "Text";
            else valDcType = "Dataset";
        }

        juce::String valDcFormat = projeto_.lerMetadado(itemId, "dc_format").value_or("");
        if (valDcFormat.isEmpty()) {
            if (cat == MediaCategory::Audio) valDcFormat = "audio/" + ext.toLowerCase();
            else if (cat == MediaCategory::Video) valDcFormat = "video/" + ext.toLowerCase();
            else if (cat == MediaCategory::Image) valDcFormat = "image/" + ext.toLowerCase();
            else if (cat == MediaCategory::Docs) valDcFormat = "application/" + ext.toLowerCase();
            else valDcFormat = ext.toLowerCase();
        }

        juce::String valDcIdentifier = projeto_.lerMetadado(itemId, "dc_identifier").value_or("");
        if (valDcIdentifier.isEmpty()) {
            std::string pid;
            try {
                auto stmt = projeto_.projeto().registro().prepare(
                    "SELECT IFNULL(persistent_id,'') FROM item WHERE id = ? LIMIT 1");
                stmt.bind(1, matriz::db::Value::of(itemId));
                if (stmt.step()) pid = stmt.columnText(0);
            } catch (...) {}
            if (!pid.empty()) valDcIdentifier = pid;
            else valDcIdentifier = itemId;
        }

        juce::String valDcSource = projeto_.lerMetadado(itemId, "dc_source").value_or("");
        if (valDcSource.isEmpty()) valDcSource = valSourceMedia.isNotEmpty() ? valSourceMedia : valPath;

        juce::String valDcLanguage = projeto_.lerMetadado(itemId, "dc_language").value_or("");
        if (valDcLanguage.isEmpty() && cat == MediaCategory::Docs) valDcLanguage = "por";

        juce::String valDcRelation = projeto_.lerMetadado(itemId, "dc_relation").value_or("");
        if (valDcRelation.isEmpty()) valDcRelation = valCollection;

        juce::String valDcCoverage = projeto_.lerMetadado(itemId, "dc_coverage").value_or("");
        if (valDcCoverage.isEmpty()) {
            auto geoOpt = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(projeto_.projeto().registro(), itemId);
            if (geoOpt && (geoOpt->city || geoOpt->country)) {
                juce::String loc;
                if (geoOpt->city) loc += juce::String(*geoOpt->city);
                if (geoOpt->stateProvince) loc += (loc.isNotEmpty() ? ", " : "") + juce::String(*geoOpt->stateProvince);
                if (geoOpt->country) loc += (loc.isNotEmpty() ? ", " : "") + juce::String(*geoOpt->country);
                valDcCoverage = loc;
            }
        }

        juce::String valDcRights = projeto_.lerMetadado(itemId, "dc_rights").value_or("");
        if (valDcRights.isEmpty()) {
            auto rOpt = projeto_.obterDireitos(itemId);
            if (rOpt && !rOpt->rightsStatus.empty()) valDcRights = rOpt->rightsStatus;
        }

        secHeaderDublinCore_ = std::make_unique<juce::Label>();
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
        secHeaderDublinCore_->setText(isPt ? "METADADOS DUBLIN CORE" : "DUBLIN CORE METADATA", juce::dontSendNotification);
        secHeaderDublinCore_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        secHeaderDublinCore_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*secHeaderDublinCore_);

        btnAjudaDublinCore_ = std::make_unique<juce::TextButton>("?");
        btnAjudaDublinCore_->setTooltip(
            isPt ? juce::String::fromUTF8("DUBLIN CORE (ISO 15836)\n"
                                         "Padrão internacional aberto de metadados arquivísticos (15 elementos essenciais).\n"
                                         "Ideal para catalogação de patrimônio e acervos, bibliotecas digitais, intercâmbio entre instituições e integração direta com outros sistemas DAM.")
                 : juce::String("DUBLIN CORE (ISO 15836)\n"
                                "International open archival metadata standard (15 core elements).\n"
                                "Ideal for heritage cataloging, digital libraries, institutional exchange, and direct interoperability with other DAM systems."));
        btnAjudaDublinCore_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnAjudaDublinCore_->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
        btnAjudaDublinCore_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        addAndMakeVisible(*btnAjudaDublinCore_);

        btnCollapseDublinCore_ = std::make_unique<juce::TextButton>();
        btnCollapseDublinCore_->setButtonText(colapsadoDublinCore_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
        btnCollapseDublinCore_->setTooltip(colapsadoDublinCore_ ? (isPt ? "Expandir" : "Expand") : (isPt ? "Recolher" : "Collapse"));
        btnCollapseDublinCore_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btnCollapseDublinCore_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnCollapseDublinCore_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        btnCollapseDublinCore_->onClick = [this, isPt] {
            colapsadoDublinCore_ = !colapsadoDublinCore_;
            btnCollapseDublinCore_->setButtonText(colapsadoDublinCore_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
            btnCollapseDublinCore_->setTooltip(colapsadoDublinCore_ ? (isPt ? "Expandir" : "Expand") : (isPt ? "Recolher" : "Collapse"));
            bool vis = !colapsadoDublinCore_;
            for (auto& cu : camposUnificados_) {
                if (cu && cu->bloco == BlocoFicha::DublinCore) {
                    if (cu->rotulo) cu->rotulo->setVisible(vis);
                    if (cu->badge) cu->badge->setVisible(vis);
                    if (cu->editor) cu->editor->setVisible(vis);
                }
            }
            if (aoRelayoutNecessario) aoRelayoutNecessario();
            repaint();
        };
        addAndMakeVisible(*btnCollapseDublinCore_);

        secHeaderUserAsset_ = std::make_unique<juce::Label>();
        secHeaderUserAsset_->setText(isPt ? juce::String::fromUTF8("METADADOS DO ATIVO E DO USUÁRIO") : juce::String("ASSET & USER METADATA"), juce::dontSendNotification);
        secHeaderUserAsset_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        secHeaderUserAsset_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*secHeaderUserAsset_);

        btnAjudaUserAsset_ = std::make_unique<juce::TextButton>("?");
        btnAjudaUserAsset_->setTooltip(
            isPt ? juce::String::fromUTF8("METADADOS DO ATIVO E DO USUÁRIO\n"
                                         "Atributos descritivos e técnicos específicos do item no acervo.\n"
                                         "Inclui mídia de origem (suporte original), classificação de conteúdo, pessoas envolvidas, anotações de curadoria e tags personalizadas.")
                 : juce::String("ASSET & USER METADATA\n"
                                "Specific descriptive and technical attributes for this collection item.\n"
                                "Includes original source medium, content classification, associated persons, curatorial notes, and custom tags."));
        btnAjudaUserAsset_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnAjudaUserAsset_->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
        btnAjudaUserAsset_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        addAndMakeVisible(*btnAjudaUserAsset_);

        btnCollapseUserAsset_ = std::make_unique<juce::TextButton>();
        btnCollapseUserAsset_->setButtonText(colapsadoUserAsset_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
        btnCollapseUserAsset_->setTooltip(colapsadoUserAsset_ ? (isPt ? "Expandir" : "Expand") : (isPt ? "Recolher" : "Collapse"));
        btnCollapseUserAsset_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btnCollapseUserAsset_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnCollapseUserAsset_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        btnCollapseUserAsset_->onClick = [this, isPt] {
            colapsadoUserAsset_ = !colapsadoUserAsset_;
            btnCollapseUserAsset_->setButtonText(colapsadoUserAsset_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
            btnCollapseUserAsset_->setTooltip(colapsadoUserAsset_ ? (isPt ? "Expandir" : "Expand") : (isPt ? "Recolher" : "Collapse"));
            bool vis = !colapsadoUserAsset_;
            for (auto& cu : camposUnificados_) {
                if (cu && cu->bloco == BlocoFicha::UserAsset) {
                    if (cu->rotulo) cu->rotulo->setVisible(vis);
                    if (cu->badge) cu->badge->setVisible(vis);
                    if (cu->editor) cu->editor->setVisible(vis);
                }
            }
            if (aoRelayoutNecessario) aoRelayoutNecessario();
            repaint();
        };
        addAndMakeVisible(*btnCollapseUserAsset_);

        // Helpers to add fields
        auto addAutoFixed = [this, &tk, isPt](const std::string& campoId, const juce::String& rotulo, const juce::String& valor) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = campoId;
            linha->bloco = BlocoFicha::UserAsset;
            linha->ehAutoFixed = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(rotulo, juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(isPt ? "[AUTO + FIXO]" : "[AUTO + FIXED]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            linha->badge->setColour(juce::Label::textColourId, tk.campoLeituraTecnica);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto lbl = std::make_unique<juce::Label>();
            lbl->setText(valor, juce::dontSendNotification);
            lbl->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            lbl->setColour(juce::Label::textColourId, tk.campoLeituraTecnica);
            lbl->setColour(juce::Label::backgroundColourId, tk.painelAlt.withAlpha(0.6f));
            lbl->setColour(juce::Label::outlineColourId, tk.borda);
            addAndMakeVisible(*lbl);
            linha->editor = std::move(lbl);

            camposUnificados_.push_back(std::move(linha));
        };

        auto addEditableText = [this, &tk, itemId, isPt](const std::string& campoId, const juce::String& rotulo, const juce::String& valor, const std::string& dbColuna) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = campoId;
            linha->colunaDb = dbColuna;
            linha->bloco = (campoId.rfind("dc_", 0) == 0) ? BlocoFicha::DublinCore : BlocoFicha::UserAsset;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(rotulo, juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(isPt ? juce::String::fromUTF8("[EDITÁVEL]") : juce::String("[EDITABLE]"), juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto ed = std::make_unique<juce::TextEditor>();
            ed->setText(valor, false);
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
            auto* edPtr = ed.get();
            linha->onCommit = [this, itemId, dbColuna, edPtr] {
                if (edPtr) {
                    std::string txt = edPtr->getText().toStdString();
                    projeto_.salvarMetadado(itemId, dbColuna, txt);
                    if (dbColuna == "dc_title") {
                        projeto_.salvarMetadado(itemId, "titulo", txt);
                    } else if (dbColuna == "titulo") {
                        projeto_.salvarMetadado(itemId, "dc_title", txt);
                    }
                    if ((dbColuna == "titulo" || dbColuna == "dc_title") && cabecalho_) {
                        std::string tStd, tpMid, codAc;
                        if (projeto_.obterItemInfo(itemId, tStd, tpMid, codAc)) {
                            cabecalho_->setText(juce::String(codAc) + " - " + juce::String(txt), juce::dontSendNotification);
                        }
                    }
                    if (aoMudar) aoMudar();
                }
            };
            ed->onFocusLost = linha->onCommit;
            ed->onReturnKey = linha->onCommit;
            addAndMakeVisible(*ed);
            linha->editor = std::move(ed);

            camposUnificados_.push_back(std::move(linha));
        };

        auto addEditableDropdown = [this, &tk, itemId, isPt](const std::string& campoId, const juce::String& rotulo, const juce::String& valor, const std::vector<juce::String>& opcoes, const std::string& dbColuna) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = campoId;
            linha->colunaDb = dbColuna;
            linha->bloco = (campoId.rfind("dc_", 0) == 0) ? BlocoFicha::DublinCore : BlocoFicha::UserAsset;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(rotulo, juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText("[DROPDOWN]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto combo = std::make_unique<juce::ComboBox>();
            int selId = 0;
            for (size_t i = 0; i < opcoes.size(); ++i) {
                combo->addItem(opcoes[i], static_cast<int>(i + 1));
                if (valor.trim().equalsIgnoreCase(opcoes[i].trim()) ||
                    traduzirContent(valor, isPt).trim().equalsIgnoreCase(opcoes[i].trim()) ||
                    traduzirContent(valor, !isPt).trim().equalsIgnoreCase(opcoes[i].trim())) {
                    selId = static_cast<int>(i + 1);
                }
            }
            if (selId > 0) combo->setSelectedId(selId, juce::dontSendNotification);
            else if (!valor.isEmpty()) {
                combo->addItem(valor, static_cast<int>(opcoes.size() + 1));
                combo->setSelectedId(static_cast<int>(opcoes.size() + 1), juce::dontSendNotification);
            }

            combo->setColour(juce::ComboBox::textColourId, juce::Colours::black);
            combo->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
            combo->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
            combo->setColour(juce::ComboBox::outlineColourId, tk.borda);

            auto* comboPtr = combo.get();
            linha->onCommit = [this, itemId, dbColuna, campoId, comboPtr] {
                if (comboPtr) {
                    juce::String txt = comboPtr->getText();
                    if (campoId == "collection") {
                        txt = traduzirContent(txt, false);
                    }
                    projeto_.salvarMetadado(itemId, dbColuna, txt.toStdString());
                    if (aoMudar) aoMudar();
                }
            };
            combo->onChange = linha->onCommit;
            addAndMakeVisible(*combo);
            linha->editor = std::move(combo);

            camposUnificados_.push_back(std::move(linha));
        };

        auto addEditableToggle = [this, &tk, itemId, isPt](const std::string& campoId, const juce::String& rotulo, bool ligado, const std::string& dbColuna) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = campoId;
            linha->colunaDb = dbColuna;
            linha->bloco = BlocoFicha::UserAsset;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(rotulo, juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(isPt ? "[USER]" : "[USER]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto toggle = std::make_unique<juce::ToggleButton>();
            toggle->setToggleState(ligado, juce::dontSendNotification);
            toggle->setButtonText("");
            toggle->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
            toggle->setColour(juce::ToggleButton::tickColourId, tk.acento);

            auto* tPtr = toggle.get();
            linha->onCommit = [this, itemId, dbColuna, tPtr] {
                if (tPtr) {
                    bool st = tPtr->getToggleState();
                    projeto_.salvarMetadado(itemId, dbColuna, st ? "AI Generated / Gerado por IA" : "");

                    TagChipsEditor* chips = nullptr;
                    for (auto& cu : camposUnificados_) {
                        if (cu && cu->ehTags) {
                            chips = dynamic_cast<TagChipsEditor*>(cu->editor.get());
                            break;
                        }
                    }

                    if (st) {
                        if (chips) {
                            chips->addTag("IA");
                            chips->addTag("AI");
                            chips->addTag("CONTEUDO IA");
                            chips->addTag("AI CONTENT");
                        } else {
                            projeto_.adicionarTag(itemId, "IA");
                            projeto_.adicionarTag(itemId, "AI");
                            projeto_.adicionarTag(itemId, "CONTEUDO IA");
                            projeto_.adicionarTag(itemId, "AI CONTENT");
                        }
                    } else {
                        if (chips) {
                            auto currentTags = chips->getTags();
                            std::vector<std::string> newTags;
                            for (const auto& tg : currentTags) {
                                juce::String lower = juce::String(tg).trimCharactersAtStart("#").trim().toLowerCase();
                                if (lower != "ia" && lower != "ai" && lower != "conteudo ia" && lower != "ai content") {
                                    newTags.push_back(tg);
                                }
                            }
                            chips->setTags(newTags);
                            projeto_.definirTags(itemId, newTags);
                        } else {
                            projeto_.removerTag(itemId, "IA");
                            projeto_.removerTag(itemId, "AI");
                            projeto_.removerTag(itemId, "CONTEUDO IA");
                            projeto_.removerTag(itemId, "AI CONTENT");
                        }
                    }

                    if (aoAplicarSucesso) aoAplicarSucesso(itemId);
                    if (aoMudar) aoMudar();
                }
            };
            toggle->onClick = linha->onCommit;
            addAndMakeVisible(*toggle);
            linha->editor = std::move(toggle);

            camposUnificados_.push_back(std::move(linha));
        };

        // --- DUBLIN CORE METADATA SECTION (At top of all cards, above NAME) ---
        addEditableText("dc_title", isPt ? juce::String::fromUTF8("TÍTULO") : juce::String("TITLE"), valDcTitle, "dc_title");
        addEditableText("dc_creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), valDcCreator, "dc_creator");
        addEditableText("dc_subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), valDcSubject, "dc_subject");
        addEditableText("dc_description", isPt ? juce::String::fromUTF8("DESCRIÇÃO") : juce::String("DESCRIPTION"), valDcDescription, "dc_description");
        addEditableText("dc_publisher", isPt ? juce::String::fromUTF8("PUBLICADOR") : juce::String("PUBLISHER"), valDcPublisher, "dc_publisher");
        addEditableText("dc_contributor", isPt ? juce::String::fromUTF8("COLABORADOR") : juce::String("CONTRIBUTOR"), valDcContributor, "dc_contributor");
        addEditableText("dc_created", isPt ? juce::String::fromUTF8("DATA DE CRIAÇÃO (AAAA-MM-DD)") : juce::String("DATE CREATED (YYYY-MM-DD)"), valDcCreated, "dc_created");
        addEditableText("dc_issued", isPt ? juce::String::fromUTF8("DATA DE PUBLICAÇÃO (AAAA-MM-DD)") : juce::String("DATE ISSUED (YYYY-MM-DD)"), valDcIssued, "dc_issued");
        addEditableText("dc_type", isPt ? juce::String::fromUTF8("TIPO") : juce::String("TYPE"), valDcType, "dc_type");
        addEditableText("dc_format", isPt ? juce::String::fromUTF8("FORMATO") : juce::String("FORMAT"), valDcFormat, "dc_format");
        addEditableText("dc_identifier", isPt ? juce::String::fromUTF8("IDENTIFICADOR") : juce::String("IDENTIFIER"), valDcIdentifier, "dc_identifier");
        addEditableText("dc_source", isPt ? juce::String::fromUTF8("ORIGEM") : juce::String("SOURCE"), valDcSource, "dc_source");
        addEditableText("dc_language", isPt ? juce::String::fromUTF8("IDIOMA") : juce::String("LANGUAGE"), valDcLanguage, "dc_language");
        addEditableText("dc_relation", isPt ? juce::String::fromUTF8("RELAÇÃO") : juce::String("RELATION"), valDcRelation, "dc_relation");
        addEditableText("dc_coverage", isPt ? juce::String::fromUTF8("COBERTURA") : juce::String("COVERAGE"), valDcCoverage, "dc_coverage");

        std::vector<juce::String> opcoesRights = isPt ?
            std::vector<juce::String>{juce::String::fromUTF8("DOMÍNIO PÚBLICO"), "DIREITOS AUTORAIS (COPYRIGHT)", "CREATIVE COMMONS (CC BY)",
                                      "CREATIVE COMMONS (CC BY-SA)", "CREATIVE COMMONS (CC BY-NC)", "CREATIVE COMMONS (CC BY-NC-ND)",
                                      "CREATIVE COMMONS (CC0)", juce::String::fromUTF8("OBRA ÓRFÃ"), juce::String::fromUTF8("USO ACEITÁVEL (FAIR USE)"), "RESTRITO"} :
            std::vector<juce::String>{"PUBLIC DOMAIN", "COPYRIGHT", "CREATIVE COMMONS (CC BY)",
                                      "CREATIVE COMMONS (CC BY-SA)", "CREATIVE COMMONS (CC BY-NC)", "CREATIVE COMMONS (CC BY-NC-ND)",
                                      "CREATIVE COMMONS (CC0)", "ORPHAN WORK", "FAIR USE", "RESTRICTED"};

        addEditableDropdown("dc_rights", isPt ? juce::String::fromUTF8("DIREITOS") : juce::String("RIGHTS"), valDcRights,
                            opcoesRights,
                            "dc_rights");

        auto addEditableNotes = [this, &tk, itemId, isPt](const juce::String& valor) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = "notes";
            linha->colunaDb = "notas_livres";
            linha->bloco = BlocoFicha::UserAsset;
            linha->ehNotes = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(isPt ? "NOTAS" : "NOTES", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(isPt ? juce::String::fromUTF8("[EDITÁVEL]") : juce::String("[EDITABLE]"), juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto ed = std::make_unique<juce::TextEditor>();
            ed->setMultiLine(true, true);
            ed->setReturnKeyStartsNewLine(true);
            ed->setText(valor, false);
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            ed->setColour(juce::TextEditor::outlineColourId, tk.borda);

            auto* edPtr = ed.get();
            linha->onCommit = [this, itemId, edPtr] {
                if (edPtr) {
                    projeto_.salvarMetadado(itemId, "notas_livres", edPtr->getText().toStdString());
                    if (aoMudar) aoMudar();
                }
            };
            ed->onFocusLost = linha->onCommit;
            addAndMakeVisible(*ed);
            linha->editor = std::move(ed);

            camposUnificados_.push_back(std::move(linha));
        };

        auto addEditablePeople = [this, &tk, itemId, isPt]() {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = "people";
            linha->bloco = BlocoFicha::UserAsset;
            linha->ehPeople = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(isPt ? "PESSOAS" : "PEOPLE", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(isPt ? "[PESSOAS]" : "[PEOPLE]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto picker = std::make_unique<PeoplePickerComponent>(projeto_, itemId);
            picker->onPersonAddedToTags = [this, itemId](const juce::String& nomePessoa) {
                TagChipsEditor* chips = nullptr;
                for (auto& cu : camposUnificados_) {
                    if (cu && cu->ehTags) {
                        chips = dynamic_cast<TagChipsEditor*>(cu->editor.get());
                        break;
                    }
                }

                if (chips) {
                    chips->addTag(nomePessoa);
                } else {
                    projeto_.adicionarTag(itemId, nomePessoa.toStdString());
                }

                if (aoAplicarSucesso) aoAplicarSucesso(itemId);
                if (aoMudar) aoMudar();
            };

            addAndMakeVisible(*picker);
            linha->editor = std::move(picker);

            camposUnificados_.push_back(std::move(linha));
        };

        auto addEditableTags = [this, &tk, itemId, isPt](const std::vector<std::string>& tagsList) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = "tags";
            linha->bloco = BlocoFicha::UserAsset;
            linha->ehTags = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(isPt ? "TAGS" : "TAGS", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(isPt ? "[TAGS]" : "[TAGS]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto chips = std::make_unique<TagChipsEditor>();
            chips->setTags(tagsList);
            chips->aoMudar = [this, itemId, raw = chips.get()] {
                projeto_.definirTags(itemId, raw->getTags());
                if (aoAplicarSucesso) aoAplicarSucesso(itemId);
                if (aoMudar) aoMudar();
            };
            chips->aoRedimensionar = [this] {
                if (aoRelayoutNecessario) aoRelayoutNecessario();
            };
            addAndMakeVisible(*chips);
            linha->editor = std::move(chips);

            camposUnificados_.push_back(std::move(linha));
        };

        auto addEditableOriginalSourceMedium = [this, &tk, itemId, isPt](const std::string& rawValue) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = "source_media";
            linha->colunaDb = "source_media";
            linha->bloco = BlocoFicha::UserAsset;
            linha->ehOriginalSourceMedium = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(isPt ? juce::String::fromUTF8("MÍDIA DE ORIGEM") : juce::String("ORIGINAL SOURCE MEDIUM"), juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(isPt ? juce::String::fromUTF8("[EDITÁVEL]") : juce::String("[EDITABLE]"), juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto osm = std::make_unique<OriginalSourceMediumEditorComponent>();
            osm->setValueString(rawValue);
            osm->onChange = [this, itemId, rawOsm = osm.get()] {
                projeto_.salvarMetadado(itemId, "source_media", rawOsm->getValueString());
                if (aoMudar) aoMudar();
                if (aoRelayoutNecessario) aoRelayoutNecessario();
            };
            addAndMakeVisible(*osm);
            linha->editor = std::move(osm);

            camposUnificados_.push_back(std::move(linha));
        };

        juce::String valAiGen = projeto_.lerMetadado(itemId, "ai_generated").value_or("");
        bool isAi = (valAiGen.isNotEmpty() && !valAiGen.equalsIgnoreCase("false") && !valAiGen.equalsIgnoreCase("0") && !valAiGen.equalsIgnoreCase("no") && !valAiGen.equalsIgnoreCase("nao"));

        // Construct fields per category exactly as specified
        if (cat == MediaCategory::Audio) {
            addEditableText("path", isPt ? "CAMINHO" : "PATH", valPath, "caminho_catalogo");
            addAutoFixed("length", isPt ? juce::String::fromUTF8("DURAÇÃO") : juce::String("LENGTH"), lengthStr);
            addAutoFixed("format", isPt ? "FORMATO" : "FORMAT", ext);
            addAutoFixed("codec", "CODEC", codecStr);
            addAutoFixed("file_size", isPt ? "TAMANHO DO ARQUIVO" : "FILE SIZE", fileSizeStr);
            addAutoFixed("sample_rate", isPt ? "TAXA DE AMOSTRAGEM" : "SAMPLE RATE", sampleRateStr);
            addAutoFixed("bit_depth", isPt ? "PROFUNDIDADE DE BITS" : "BIT DEPTH", bitDepthStr);
            if (channelsStr.isNotEmpty())
                addAutoFixed("channels", isPt ? "CANAIS" : "CHANNELS", channelsStr);
            addEditableText("isrc", "ISRC", valIsrc, "isrc");
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), traduzirContent(valCollection, isPt),
                                opcoesContentPorCategoria(MediaCategory::Audio, isPt),
                                "collection_type");
            addEditableToggle("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", isAi, "ai_generated");
            addEditableNotes(valNotes);
            addEditablePeople();
            addEditableTags(tagsList);
        } else if (cat == MediaCategory::Video) {
            addEditableText("path", isPt ? "CAMINHO" : "PATH", valPath, "caminho_catalogo");
            addAutoFixed("length", isPt ? juce::String::fromUTF8("DURAÇÃO") : juce::String("LENGTH"), lengthStr);
            addAutoFixed("dimensions", isPt ? juce::String::fromUTF8("DIMENSÕES") : juce::String("DIMENSIONS"), dimensionsStr);
            addAutoFixed("screen_orientation", isPt ? juce::String::fromUTF8("TELA / ORIENTAÇÃO") : juce::String("SCREEN / ORIENTATION"), orientationStr);
            addAutoFixed("format", isPt ? "FORMATO" : "FORMAT", ext);
            addAutoFixed("codec", "CODEC", codecStr);
            addAutoFixed("file_size", isPt ? "TAMANHO DO ARQUIVO" : "FILE SIZE", fileSizeStr);
            addAutoFixed("sample_rate", isPt ? "TAXA DE AMOSTRAGEM" : "SAMPLE RATE", sampleRateStr);
            addAutoFixed("bit_depth", isPt ? "PROFUNDIDADE DE BITS" : "BIT DEPTH", bitDepthStr);
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), traduzirContent(valCollection, isPt),
                                opcoesContentPorCategoria(MediaCategory::Video, isPt),
                                "collection_type");
            addEditableToggle("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", isAi, "ai_generated");
            addEditableNotes(valNotes);
            addEditablePeople();
            addEditableTags(tagsList);
        } else if (cat == MediaCategory::Image) {
            addEditableText("path", isPt ? "CAMINHO" : "PATH", valPath, "caminho_catalogo");
            addAutoFixed("dimensions", isPt ? juce::String::fromUTF8("DIMENSÕES") : juce::String("DIMENSIONS"), dimensionsStr);
            addAutoFixed("screen_orientation", isPt ? juce::String::fromUTF8("TELA / ORIENTAÇÃO") : juce::String("SCREEN / ORIENTATION"), orientationStr);
            addAutoFixed("format", isPt ? "FORMATO" : "FORMAT", ext);
            addAutoFixed("file_size", isPt ? "TAMANHO DO ARQUIVO" : "FILE SIZE", fileSizeStr);
            addAutoFixed("color_space", isPt ? juce::String::fromUTF8("ESPAÇO DE COR") : juce::String("COLOR SPACE"), colorSpaceStr);
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), traduzirContent(valCollection, isPt),
                                opcoesContentPorCategoria(MediaCategory::Image, isPt),
                                "collection_type");
            addEditableToggle("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", isAi, "ai_generated");
            addEditableNotes(valNotes);
            addEditablePeople();
            addEditableTags(tagsList);
        } else { // Docs
            addEditableText("path", isPt ? "CAMINHO" : "PATH", valPath, "caminho_catalogo");
            addAutoFixed("format", isPt ? "FORMATO" : "FORMAT", ext);
            addAutoFixed("file_size", isPt ? "TAMANHO DO ARQUIVO" : "FILE SIZE", fileSizeStr);
            addAutoFixed("pages", isPt ? juce::String::fromUTF8("PÁGINAS") : juce::String("PAGES"), pagesStr);
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), traduzirContent(valCollection, isPt),
                                opcoesContentPorCategoria(MediaCategory::Docs, isPt),
                                "collection_type");
            addEditableToggle("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", isAi, "ai_generated");
            addEditableNotes(valNotes);
            addEditablePeople();
            addEditableTags(tagsList);
        }
    }

    struct LinhaCampo {
        const Campo* campo = nullptr;
        std::string nivel;
        int nivelIndice = 0;
        std::unique_ptr<juce::Label> rotulo;
        std::unique_ptr<juce::Component> editorSimples; // TextEditor, ComboBox ou ToggleButton
        std::unique_ptr<TabelaEditor> editorTabela;      // usado só quando campo->tipo é Tabela/ListaPessoas
        std::unique_ptr<juce::Label> indicador;
        std::unique_ptr<juce::TextButton> botaoConfirmar;
        std::unique_ptr<juce::Label> alerta;
        bool visivel = true;

        void setBoundsTudoZero() {
            if (rotulo) rotulo->setBounds({});
            if (editorSimples) editorSimples->setBounds({});
            if (editorTabela) editorTabela->setBounds({});
            if (indicador) indicador->setBounds({});
            if (botaoConfirmar) botaoConfirmar->setBounds({});
            if (alerta) alerta->setBounds({});
        }

        int alturaNecessaria(int largura) const {
            int altura = 18; // rótulo
            if (editorTabela) altura += editorTabela->alturaTotal() + 4;
            else altura += 24;
            if (indicador) altura += 16;
            if (alerta) altura += 16;
            return altura;
        }

        void aplicarBounds(int x, int y, int largura, int) {
            rotulo->setBounds(x, y, largura, 18);
            y += 18;
            if (editorTabela) {
                editorTabela->setBounds(x, y, largura, editorTabela->alturaTotal());
                editorTabela->resized();
                y += editorTabela->alturaTotal() + 4;
            } else {
                int larguraEditor = botaoConfirmar ? largura - 96 : largura;
                editorSimples->setBounds(x, y, larguraEditor, 22);
                if (botaoConfirmar) botaoConfirmar->setBounds(x + larguraEditor + 4, y, 92, 22);
                y += 24;
            }
            if (indicador) { indicador->setBounds(x, y, largura, 16); y += 16; }
            if (alerta) { alerta->setBounds(x, y, largura, 16); y += 16; }
        }
    };

    struct Secao {
        std::unique_ptr<juce::Label> titulo;
        std::vector<LinhaCampo*> linhas; // não-owning; dono é linhas_ (OwnedArray)
    };

    struct SecaoArquivos {
        std::unique_ptr<juce::Label> titulo;
        std::vector<std::unique_ptr<juce::Label>> linhas;
    };

    // Observações/Notes (item 9) — múltiplas entradas por item, texto +
    // autor + data + minutagem opcional. Sem edição de texto existente
    // (fora de escopo) — só criar, listar, remover. Quando a timeline de
    // áudio/vídeo existir, marcadores passam a alimentar esta mesma lista
    // automaticamente (item 9.2) — hoje é sempre só o que o operador digita.
    struct LinhaObservacao {
        std::unique_ptr<juce::Label> texto;
        std::unique_ptr<juce::TextButton> remover;
    };
    struct SecaoObservacoes {
        std::unique_ptr<juce::Label> titulo;
        std::vector<LinhaObservacao> itens;
        std::unique_ptr<juce::TextButton> botaoNova;
        std::unique_ptr<juce::TextEditor> editorTexto;
        std::unique_ptr<juce::TextEditor> editorMinutagem;
        std::unique_ptr<juce::TextButton> botaoSalvar;
        std::unique_ptr<juce::TextButton> botaoCancelar;
    };

    std::string chaveValor(const std::string& nivel, int nivelIndice, const std::string& campoId) const {
        return nivel + "|" + std::to_string(nivelIndice) + "|" + campoId;
    }

    void carregarValores(const std::string& itemId, const FichaDefinition& def) {
        valores_.clear();
        auto carregarPara = [&](const std::vector<Campo>& campos, const std::string& nivel, int idx) {
            for (auto& c : campos) {
                auto v = projeto_.valorCampo(itemId, nivel, idx, c.id);
                valores_[chaveValor(nivel, idx, c.id)] = v.value_or("");
            }
        };
        if (def.usaNiveis()) {
            carregarPara(def.camposPorNivel[0].second, "raiz", 0);
            const std::string& nivelRep = def.niveis[1];
            for (int idx : indicesExistentes(itemId, nivelRep)) carregarPara(*def.camposDoNivel(nivelRep), nivelRep, idx);
        } else {
            for (auto& g : def.grupos) carregarPara(g.campos, "raiz", 0);
        }
    }

    std::set<int> indicesExistentes(const std::string& itemId, const std::string& nivel) const {
        return projeto_.indicesExistentes(itemId, nivel);
    }

    void construirSecaoArquivosEsperados(const std::string& itemId, const FichaDefinition& def) {
        if (def.arquivosEsperados.empty()) return;
        auto presentes = projeto_.papeisArquivoPresentes(itemId);

        arquivosEsperados_.titulo = std::make_unique<juce::Label>();
        arquivosEsperados_.titulo->setText(matriz::i18n::t("ficha.secao_arquivos_esperados"), juce::dontSendNotification);
        arquivosEsperados_.titulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo, juce::Font::bold)));
        arquivosEsperados_.titulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        addAndMakeVisible(*arquivosEsperados_.titulo);

        for (auto& ae : def.arquivosEsperados) {
            bool presente = std::find(presentes.begin(), presentes.end(), ae.papel) != presentes.end();
            juce::String texto = juce::String(ae.papel) + " - " +
                                  (presente ? matriz::i18n::t("ficha.arquivo_presente") : matriz::i18n::t("ficha.arquivo_ausente"));
            if (ae.obrigatorio) texto += juce::String(" (") + matriz::i18n::t("ficha.arquivo_obrigatorio") + ")";
            if (!ae.minimo.empty()) texto += " - min " + juce::String(ae.minimo);

            auto label = std::make_unique<juce::Label>();
            label->setText(texto, juce::dontSendNotification);
            label->setColour(juce::Label::textColourId,
                              presente ? matriz::ui::tema().textoPrimario
                                       : (ae.obrigatorio ? matriz::ui::tema().perigo : matriz::ui::tema().textoSecundario));
            addAndMakeVisible(*label);
            arquivosEsperados_.linhas.push_back(std::move(label));
        }
    }

    void construirSecaoObservacoes(const std::string& itemId) {
        observacoes_.titulo = std::make_unique<juce::Label>();
        observacoes_.titulo->setText(matriz::i18n::t("ficha.secao_observacoes"), juce::dontSendNotification);
        observacoes_.titulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo, juce::Font::bold)));
        observacoes_.titulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        addAndMakeVisible(*observacoes_.titulo);

        for (auto& obs : projeto_.observacoesDoItem(itemId)) {
            LinhaObservacao linha;
            juce::String texto = obs.minutagemMs ? (matriz::ui::formatarMinutagem(*obs.minutagemMs) + "  ") : juce::String();
            texto += juce::String(obs.texto);
            texto += "\n" + juce::String(obs.autor) + " - " + juce::String(obs.criadoEm);

            linha.texto = std::make_unique<juce::Label>();
            linha.texto->setText(texto, juce::dontSendNotification);
            linha.texto->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
            linha.texto->setColour(juce::Label::textColourId, matriz::ui::tema().textoPrimario);
            linha.texto->setMinimumHorizontalScale(1.0f);
            addAndMakeVisible(*linha.texto);

            linha.remover = std::make_unique<juce::TextButton>("x");
            std::string obsId = obs.id;
            linha.remover->onClick = [this, obsId] {
                projeto_.removerObservacao(obsId);
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*linha.remover);

            observacoes_.itens.push_back(std::move(linha));
        }

        if (adicionandoObservacao_) {
            observacoes_.editorTexto = std::make_unique<juce::TextEditor>();
            observacoes_.editorTexto->setMultiLine(true, true);
            addAndMakeVisible(*observacoes_.editorTexto);

            observacoes_.editorMinutagem = std::make_unique<juce::TextEditor>();
            observacoes_.editorMinutagem->setTextToShowWhenEmpty(matriz::i18n::t("ficha.observacao_minutagem_dica"),
                                                                   matriz::ui::tema().textoTerciario);
            addAndMakeVisible(*observacoes_.editorMinutagem);

            auto* editorTextoPtr = observacoes_.editorTexto.get();
            auto* editorMinutagemPtr = observacoes_.editorMinutagem.get();

            observacoes_.botaoSalvar = std::make_unique<juce::TextButton>(matriz::i18n::t("comum.salvar"));
            observacoes_.botaoSalvar->onClick = [this, editorTextoPtr, editorMinutagemPtr] {
                juce::String texto = editorTextoPtr->getText().trim();
                if (texto.isEmpty()) return;
                auto minutagem = matriz::ui::parsearMinutagem(editorMinutagemPtr->getText());
                projeto_.adicionarObservacao(itemId_, texto.toStdString(), minutagem, autorAtual());
                adicionandoObservacao_ = false;
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*observacoes_.botaoSalvar);

            observacoes_.botaoCancelar = std::make_unique<juce::TextButton>(matriz::i18n::t("comum.cancelar"));
            observacoes_.botaoCancelar->onClick = [this] {
                adicionandoObservacao_ = false;
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*observacoes_.botaoCancelar);
        } else {
            observacoes_.botaoNova = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.observacao_nova"));
            observacoes_.botaoNova->onClick = [this] {
                adicionandoObservacao_ = true;
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*observacoes_.botaoNova);
        }
    }

    // extrairDestaque puxa origem/ano (§4.2/§4.3 — campo de origem e ano em
    // destaque no topo de toda ficha) pra fora do fluxo normal de seção,
    // pra destaque_ (renderizado em faixa fixa acima das seções, ver
    // relayout). Só a primeira seção de cada ficha (raiz do nível único ou
    // primeiro grupo — onde origem/ano foram inseridos nas definições YAML)
    // passa true; seções repetidas (faixa) nunca extraem.
    void construirSecao(const juce::String& tituloSecao, const std::vector<Campo>& campos, const std::string& nivel,
                         int nivelIndice, bool comBotaoRemover = false, bool extrairDestaque = false) {
        Secao secao;
        secao.titulo = std::make_unique<juce::Label>();
        secao.titulo->setText(tituloSecao, juce::dontSendNotification);
        secao.titulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo, juce::Font::bold)));
        secao.titulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        addAndMakeVisible(*secao.titulo);

        for (auto& campo : campos) {
            if (extrairDestaque && (campo.id == "origem" || campo.id == "ano")) {
                auto* linha = construirLinha(campo, nivel, nivelIndice);
                linha->rotulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteSubtitulo, juce::Font::bold)));
                destaque_.push_back(linha);
                continue;
            }
            secao.linhas.push_back(construirLinha(campo, nivel, nivelIndice));
        }

        secoes_.push_back(std::move(secao));
        juce::ignoreUnused(comBotaoRemover);
    }

    LinhaCampo* construirLinha(const Campo& campo, const std::string& nivel, int nivelIndice) {
        auto* linha = linhas_.add(new LinhaCampo());
        linha->campo = &campo;
        linha->nivel = nivel;
        linha->nivelIndice = nivelIndice;

        juce::String textoRotulo = matriz::ficha::rotuloCampo(tipoAtual_, campo) + (campo.obrigatorio ? " *" : "");
        linha->rotulo = std::make_unique<juce::Label>();
        linha->rotulo->setText(textoRotulo, juce::dontSendNotification);
        linha->rotulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
        linha->rotulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        addAndMakeVisible(*linha->rotulo);

        juce::String valorAtual = valores_[chaveValor(nivel, nivelIndice, campo.id)];

        bool ehTabela = campo.tipo == CampoTipo::Tabela || campo.tipo == CampoTipo::ListaPessoas;
        if (ehTabela) {
            std::vector<juce::String> colunas;
            if (campo.tipo == CampoTipo::ListaPessoas) colunas = {"nome", "papel"};
            else for (auto& c : campo.colunas) colunas.push_back(c);

            linha->editorTabela = std::make_unique<TabelaEditor>(colunas);
            linha->editorTabela->setValorJson(valorAtual);
            addAndMakeVisible(*linha->editorTabela);
            linha->editorTabela->aoMudar = [this, linha] { commitLinha(*linha); };
        } else {
            linha->editorSimples = criarEditorSimples(campo, valorAtual, linha);
            addAndMakeVisible(*linha->editorSimples);
        }

        // Proveniência (P3: herdado / leitura técnica / sugestão)
        if (campo.herdaDoProjeto) {
            linha->indicador = criarIndicador(matriz::i18n::t("ficha.herdado_do_projeto"), matriz::ui::tema().campoHerdado);
        } else if (!campo.preenchidoPor.empty()) {
            linha->indicador = criarIndicador(matriz::i18n::t("ficha.preenchido_por_leitura_tecnica"), matriz::ui::tema().campoLeituraTecnica);
        } else if (!campo.sugeridoPor.empty()) {
            auto sugestao = projeto_.sugestaoPendente(itemId_, nivel, nivelIndice, campo.id);
            if (sugestao) {
                juce::String texto = matriz::i18n::t("ficha.sugestao_rotulo") + ": \"" + sugestao->valor + "\" - " +
                                      matriz::i18n::t("ficha.sugestao_modelo")
                                          .replace("{modelo}", sugestao->modelo)
                                          .replace("{versao}", sugestao->modeloVersao);
                if (sugestao->confianca)
                    texto += " (" + matriz::i18n::t("ficha.sugestao_confianca").replace("{p}", juce::String(static_cast<int>(*sugestao->confianca * 100))) + ")";
                linha->indicador = criarIndicador(texto, matriz::ui::tema().campoSugestaoIa);

                linha->botaoConfirmar = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.sugestao_confirmar"));
                matriz::ui::ProjetoAberto::SugestaoCampo sugestaoCapturada = *sugestao;
                linha->botaoConfirmar->onClick = [this, linha, sugestaoCapturada] {
                    projeto_.confirmarSugestao(sugestaoCapturada, itemId_, linha->nivel, linha->nivelIndice, linha->campo->id, autorAtual());
                    construirParaItem(itemId_); // sugestão virou decisão humana — reconstrói pra refletir o novo estado
                };
                addAndMakeVisible(*linha->botaoConfirmar);
            }
        }

        if (campo.tipo == CampoTipo::Booleano && !campo.alertaSeTrue.empty() && valorAtual == "true") {
            linha->alerta = std::make_unique<juce::Label>();
            linha->alerta->setText(
                juce::String(matriz::i18n::t("ficha.alerta_rotulo")) + ": " + matriz::ficha::rotuloAlerta(tipoAtual_, campo),
                juce::dontSendNotification);
            linha->alerta->setColour(juce::Label::textColourId, matriz::ui::tema().alerta);
            linha->alerta->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
            addAndMakeVisible(*linha->alerta);
        }

        return linha;
    }

    std::unique_ptr<juce::Label> criarIndicador(const juce::String& texto, juce::Colour cor) {
        auto label = std::make_unique<juce::Label>();
        label->setText(texto, juce::dontSendNotification);
        label->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
        label->setColour(juce::Label::textColourId, cor);
        addAndMakeVisible(*label);
        return label;
    }

    std::unique_ptr<juce::Component> criarEditorSimples(const Campo& campo, const juce::String& valorAtual, LinhaCampo* linha) {
        switch (campo.tipo) {
            case CampoTipo::Booleano: {
                auto toggle = std::make_unique<juce::ToggleButton>();
                toggle->setToggleState(valorAtual == "true", juce::dontSendNotification);
                toggle->setButtonText(valorAtual == "true" ? matriz::i18n::t("ficha.campo_booleano_sim")
                                                             : matriz::i18n::t("ficha.campo_booleano_nao"));
                toggle->onClick = [this, linha, t = toggle.get()] {
                    t->setButtonText(t->getToggleState() ? matriz::i18n::t("ficha.campo_booleano_sim")
                                                          : matriz::i18n::t("ficha.campo_booleano_nao"));
                    commitLinha(*linha);
                };
                return toggle;
            }
            case CampoTipo::Opcao: {
                // O texto exibido é traduzido (rotuloOpcao); o valor
                // gravado no banco continua sendo sempre o token bruto do
                // YAML — lerValorEditor() lê de volta pelo índice
                // selecionado, nunca por getText(), senão o dado já salvo
                // mudaria de significado ao trocar de idioma.
                auto combo = std::make_unique<juce::ComboBox>();
                int idSelecionado = 0;
                for (size_t i = 0; i < campo.opcoes.size(); ++i) {
                    combo->addItem(matriz::ficha::rotuloOpcao(tipoAtual_, campo, static_cast<int>(i), campo.opcoes[i]),
                                   static_cast<int>(i) + 1);
                    if (campo.opcoes[i] == valorAtual.toStdString()) idSelecionado = static_cast<int>(i) + 1;
                }
                combo->setSelectedId(idSelecionado, juce::dontSendNotification);
                combo->onChange = [this, linha] { commitLinha(*linha); };
                return combo;
            }
            case CampoTipo::OpcaoLivre: {
                auto combo = std::make_unique<juce::ComboBox>();
                combo->setEditableText(true); // "opção livre (autocompletar + criar)" — texto digitado vira o valor direto
                std::set<std::string> jaListados;
                for (size_t i = 0; i < campo.opcoes.size(); ++i) {
                    combo->addItem(matriz::ficha::rotuloOpcao(tipoAtual_, campo, static_cast<int>(i), campo.opcoes[i]),
                                   static_cast<int>(i) + 1);
                    jaListados.insert(campo.opcoes[i]);
                }

                // Vocabulário do projeto: o que o operador já digitou neste
                // campo em QUALQUER item vira opção nas próximas vezes —
                // digitar "Agfa" uma vez basta pra ele estar na lista depois.
                // Separador antes pra ficar claro o que é da definição e o
                // que o próprio projeto acumulou.
                auto usados = projeto_.valoresUsadosNoCampo(campo.id);
                int proximoId = static_cast<int>(campo.opcoes.size()) + 1;
                bool primeiroDoProjeto = true;
                for (auto& valor : usados) {
                    if (jaListados.count(valor)) continue;
                    if (primeiroDoProjeto) {
                        combo->addSeparator();
                        primeiroDoProjeto = false;
                    }
                    combo->addItem(valor, proximoId++);
                }

                combo->setText(valorAtual, juce::dontSendNotification);
                combo->onChange = [this, linha] { commitLinha(*linha); };
                return combo;
            }
            default: {
                auto editor = std::make_unique<juce::TextEditor>();
                editor->setText(valorAtual, false);
                if (campo.tipo == CampoTipo::Data) editor->setTextToShowWhenEmpty("AAAA-MM-DD", matriz::ui::tema().textoTerciario);
                editor->onFocusLost = [this, linha] { commitLinha(*linha); };
                editor->onReturnKey = [this, linha] { commitLinha(*linha); };
                return editor;
            }
        }
    }

    juce::String lerValorEditor(const LinhaCampo& linha) const {
        if (linha.editorTabela) return linha.editorTabela->getValorJson();
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(linha.editorSimples.get()))
            return toggle->getToggleState() ? "true" : "false";
        if (auto* combo = dynamic_cast<juce::ComboBox*>(linha.editorSimples.get())) {
            if (linha.campo->tipo == CampoTipo::Opcao) {
                // addItem() mostra o rótulo traduzido — o valor gravado
                // continua sendo o token bruto do YAML, lido de volta pelo
                // índice selecionado, nunca por getText() (que devolveria
                // o texto traduzido e mudaria o dado já salvo).
                int indice = combo->getSelectedId() - 1;
                if (indice >= 0 && indice < static_cast<int>(linha.campo->opcoes.size()))
                    return linha.campo->opcoes[static_cast<size_t>(indice)];
                return {};
            }
            return combo->getText(); // OpcaoLivre: texto digitado/selecionado é o valor direto
        }
        if (auto* editor = dynamic_cast<juce::TextEditor*>(linha.editorSimples.get())) return editor->getText();
        return {};
    }

    // Só a escrita no banco + cache local de valores_ — sem tocar em cor de
    // validação, visibilidade ou relayout. Usado tanto pelo commit normal
    // (blur/Enter/onChange) quanto pelo commit forçado em limpar(), onde os
    // widgets estão prestes a ser destruídos e mexer neles seria inútil ou
    // perigoso (relayoutEExibir() dispararia sobre uma árvore que já não
    // existe mais logo em seguida).
    void salvarValorSemEfeitosColaterais(LinhaCampo& linha) {
        if (!linha.editorSimples && !linha.editorTabela) return; // linha nunca teve editor construído
        juce::String valor = lerValorEditor(linha);
        matriz::ingest::aplicarFichaEmLote(projeto_.projeto().registro(), {itemId_}, linha.nivel, linha.nivelIndice,
                                            {{linha.campo->id, valor.toStdString()}}, autorAtual());
        valores_[chaveValor(linha.nivel, linha.nivelIndice, linha.campo->id)] = valor;
    }

    void commitLinha(LinhaCampo& linha) {
        salvarValorSemEfeitosColaterais(linha);
        juce::String valor = valores_[chaveValor(linha.nivel, linha.nivelIndice, linha.campo->id)];

        // Validação é só um aviso visual — nunca apaga nem impede o campo
        // inválido de ser salvo (Parte 2.1 da correção crítica): o texto
        // digitado já foi gravado acima, exatamente como o operador
        // escreveu, mesmo se a validação falhar.
        bool valido = true;
        if (linha.campo->validacao == "isrc") valido = valor.isEmpty() || validarIsrc(valor);
        else if (linha.campo->validacao == "ean13") valido = valor.isEmpty() || validarEan13(valor);
        else if (linha.campo->validacao == "soma_100" && linha.editorTabela)
            valido = std::abs(linha.editorTabela->somaColuna("percentual") - 100.0) < 0.01;

        auto corBase = valido ? matriz::ui::tema().textoSecundario : matriz::ui::tema().perigo;
        linha.rotulo->setColour(juce::Label::textColourId, corBase);

        if (!linha.campo->afeta.empty()) recomputarEfeitosAtivos();

        atualizarVisibilidade();
        relayoutEExibir();
        if (aoMudar) aoMudar();
    }

    void recomputarEfeitosAtivos() {
        efeitosAtivos_.clear();
        for (auto* linha : linhas_)
            if (!linha->campo->afeta.empty() && valores_[chaveValor(linha->nivel, linha->nivelIndice, linha->campo->id)].isNotEmpty())
                for (auto& efeito : linha->campo->afeta) efeitosAtivos_.insert(efeito);
    }

    void atualizarVisibilidade() {
        for (auto* linha : linhas_) {
            bool visivel = true;
            if (linha->campo->visivelSe) {
                std::string chave = chaveValor(linha->nivel, linha->nivelIndice, linha->campo->visivelSe->campoId);
                juce::String valorReferenciado = valores_.count(chave) ? valores_[chave] : juce::String();
                switch (linha->campo->visivelSe->op) {
                    case matriz::ficha::VisivelSeOperador::In:
                        visivel = std::find(linha->campo->visivelSe->valores.begin(), linha->campo->visivelSe->valores.end(),
                                             valorReferenciado.toStdString()) != linha->campo->visivelSe->valores.end();
                        break;
                    case matriz::ficha::VisivelSeOperador::Igual:
                        visivel = valorReferenciado.toStdString() == linha->campo->visivelSe->valores.front();
                        break;
                    case matriz::ficha::VisivelSeOperador::Diferente:
                        visivel = valorReferenciado.toStdString() != linha->campo->visivelSe->valores.front();
                        break;
                }
            }
            linha->visivel = visivel;
            linha->rotulo->setVisible(visivel);
            if (linha->editorSimples) linha->editorSimples->setVisible(visivel);
            if (linha->editorTabela) linha->editorTabela->setVisible(visivel);
            if (linha->indicador) linha->indicador->setVisible(visivel);
            if (linha->botaoConfirmar) linha->botaoConfirmar->setVisible(visivel);
            if (linha->alerta) linha->alerta->setVisible(visivel);
        }
    }

    void relayoutEExibir() {
        if (aoRelayoutNecessario) aoRelayoutNecessario();
    }

    ProjetoAberto& projeto_;
    std::string itemId_;
    std::string tipoAtual_; // pra chaves de i18n de campo/opção (FichaI18n.h)
    std::unique_ptr<PreviaWidget> previaWidget_;
    std::unique_ptr<juce::Label> cabecalho_;
    std::unique_ptr<juce::Label> mensagemNaoClassificado_; // não-nulo == item sem tipo_midia válido, ver construirSeletorTipoMidia
    std::vector<std::unique_ptr<juce::TextButton>> botoesTipoNaoClassificado_;
    std::unique_ptr<juce::TextButton> botaoCancelarRecategorizar_;
    std::unique_ptr<juce::TextButton> botaoRecategorizar_;
    // Persiste através de limpar()/construirParaItem, igual adicionandoObservacao_.
    bool recategorizando_ = false;
    juce::OwnedArray<LinhaCampo> linhas_;
    std::vector<Secao> secoes_;
    std::vector<LinhaCampo*> destaque_; // origem/ano (§4.2/§4.3) — não-owning, dono é linhas_
    SecaoArquivos arquivosEsperados_;
    SecaoObservacoes observacoes_;
    // Persiste através de limpar()/construirParaItem — é o que faz o botão
    // "Nova observação" reabrir o formulário depois do reconstruir.
    bool adicionandoObservacao_ = false;
    std::map<std::string, juce::String> valores_; // chaveValor -> valor atual (cache local pra visivel_se/afeta)
    std::set<std::string> efeitosAtivos_;
    std::vector<std::unique_ptr<juce::TextButton>> botoesAdicionarFaixa_;
    int proximoIndiceFaixa_ = 0;

    std::unique_ptr<juce::TextButton> botaoAplicar_;
    std::unique_ptr<juce::Label>      labelAplicado_;
    std::unique_ptr<juce::Label>      labelReviewFaltando_;

    // -----------------------------------------------------------------------
    // Seção PRESERVATION — adicionada ao final da FichaConteudo
    // -----------------------------------------------------------------------
    struct SecaoPreservacao {
        std::unique_ptr<juce::Label>       titulo;
        std::unique_ptr<juce::Label>       labelPersistentId;
        std::unique_ptr<juce::TextButton>  botaoCopiarId;
        std::unique_ptr<juce::Label>       labelStatusGeral;
        std::unique_ptr<juce::Label>       labelSha256;
        std::unique_ptr<juce::Label>       labelUltimaVerificacao;
        std::unique_ptr<juce::TextButton>  botaoVerificarFixity;
        std::unique_ptr<juce::TextButton>  botaoExportJson;
        std::unique_ptr<juce::TextButton>  botaoExportCsv;
        // Rights
        std::unique_ptr<juce::Label>       tituloRights;
        std::unique_ptr<juce::ComboBox>    comboRights;
        std::unique_ptr<juce::TextEditor>  editorHolder;
        std::unique_ptr<juce::TextEditor>  editorLicense;
        std::unique_ptr<juce::TextEditor>  editorNotes;
        std::unique_ptr<juce::TextButton>  botaoSalvarRights;
        // Event History
        std::unique_ptr<juce::Label>       tituloEventos;
        std::vector<std::unique_ptr<juce::Label>> linhasEvento;
        // Estado de verificação async
        std::unique_ptr<juce::Label>       labelVerificando;
        // Identificadores do arquivo master (para verificação)
        std::string arquivoMasterId;
        std::string arquivoMasterCaminho;
        std::string itemId;
    } preservation_;

    struct SecaoGeolocalizacao {
        std::unique_ptr<juce::Label>       titulo;
        std::unique_ptr<juce::Label>       statusBadge;
        std::unique_ptr<juce::Label>       labelCoords;
        std::unique_ptr<juce::TextEditor>  editorCoords;
        std::unique_ptr<juce::Label>       labelAddress;
        std::unique_ptr<juce::TextEditor>  editorAddress;
        std::unique_ptr<juce::Label>       labelCity;
        std::unique_ptr<juce::TextEditor>  editorCity;
        std::unique_ptr<juce::Label>       labelState;
        std::unique_ptr<juce::TextEditor>  editorState;
        std::unique_ptr<juce::Label>       labelCountry;
        std::unique_ptr<juce::TextEditor>  editorCountry;
    } geolocalizacao_;

    juce::Rectangle<int> quadroDublinCore_;
    juce::Rectangle<int> quadroUserAsset_;
    juce::Rectangle<int> quadroGeoLocation_;
};

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// FichaLoteConteudo — Unified batch metadata editor (§ metadata spec & batch editing)
// ---------------------------------------------------------------------------

class FichaLoteConteudo : public juce::Component {
public:
    explicit FichaLoteConteudo(ProjetoAberto& projeto) : projeto_(projeto) {}

    void lookAndFeelChanged() override {
        const auto& tk = matriz::ui::tema();
        if (cabecalho_) {
            cabecalho_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
            cabecalho_->setColour(juce::Label::textColourId, tk.textoPrimario);
        }
        for (auto* l : linhas_) {
            if (l->rotulo) l->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            if (l->badge) l->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            if (l->editor) {
                if (auto* ed = dynamic_cast<juce::TextEditor*>(l->editor.get())) {
                    ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
                    ed->setColour(juce::TextEditor::textColourId, juce::Colours::black);
                    ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
                } else if (auto* cb = dynamic_cast<juce::ComboBox*>(l->editor.get())) {
                    cb->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
                    cb->setColour(juce::ComboBox::textColourId, juce::Colours::black);
                    cb->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
                    cb->setColour(juce::ComboBox::outlineColourId, tk.borda);
                }
            }
        }
        if (geoLote_.titulo) geoLote_.titulo->setColour(juce::Label::textColourId, tk.textoPrimario);
        if (geoLote_.badge) geoLote_.badge->setColour(juce::Label::textColourId, tk.textoTerciario);
        if (geoLote_.labelCoords) geoLote_.labelCoords->setColour(juce::Label::textColourId, tk.textoSecundario);
        if (geoLote_.labelAddress) geoLote_.labelAddress->setColour(juce::Label::textColourId, tk.textoSecundario);
        if (geoLote_.labelCity) geoLote_.labelCity->setColour(juce::Label::textColourId, tk.textoSecundario);
        if (geoLote_.labelState) geoLote_.labelState->setColour(juce::Label::textColourId, tk.textoSecundario);
        if (geoLote_.labelCountry) geoLote_.labelCountry->setColour(juce::Label::textColourId, tk.textoSecundario);

        if (geoLote_.editorCoords) {
            geoLote_.editorCoords->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geoLote_.editorCoords->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geoLote_.editorCoords->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }
        if (geoLote_.editorAddress) {
            geoLote_.editorAddress->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geoLote_.editorAddress->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geoLote_.editorAddress->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }
        if (geoLote_.editorCity) {
            geoLote_.editorCity->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geoLote_.editorCity->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geoLote_.editorCity->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }
        if (geoLote_.editorState) {
            geoLote_.editorState->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geoLote_.editorState->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geoLote_.editorState->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }
        if (geoLote_.editorCountry) {
            geoLote_.editorCountry->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            geoLote_.editorCountry->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            geoLote_.editorCountry->setColour(juce::TextEditor::outlineColourId, tk.borda);
        }

        repaint();
    }

    void mostrarSelecao(std::vector<std::string> itemIds) {
        limpar();
        itemIds_ = std::move(itemIds);
        if (itemIds_.empty()) {
            relayoutEExibir();
            return;
        }

        cabecalho_ = std::make_unique<juce::Label>();
        cabecalho_->setText(matriz::i18n::t("ficha.lote_titulo").replace("{n}", juce::String((int)itemIds_.size())),
                             juce::dontSendNotification);
        cabecalho_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteSubtitulo, juce::Font::bold)));
        cabecalho_->setColour(juce::Label::textColourId, matriz::ui::tema().textoPrimario);
        addAndMakeVisible(*cabecalho_);

        std::set<std::string> tiposPresentes;
        bool algumNulo = false;
        projeto_.obterTiposMidiaDosItens(itemIds_, tiposPresentes, algumNulo);

        if (tiposPresentes.empty() && algumNulo) {
            construirSeletorTipoMidia();
            relayoutEExibir();
            return;
        }

        // Determine general category across items
        MediaCategory cat = MediaCategory::Mixed;
        bool primeiro = true;
        for (const auto& id : itemIds_) {
            std::string tStd, tpMid, codAc;
            std::string ext;
            if (auto arq = projeto_.arquivoPrincipal(id)) {
                ext = juce::File(arq->caminhoAbsoluto).getFileExtension().trimCharactersAtStart(".").toLowerCase().toStdString();
            }
            projeto_.obterItemInfo(id, tStd, tpMid, codAc);
            MediaCategory itemCat = determinarCategoriaMidia(tpMid, ext);
            if (primeiro) {
                cat = itemCat;
                primeiro = false;
            } else if (cat != itemCat) {
                cat = MediaCategory::Mixed;
                break;
            }
        }

        if (tiposPresentes.size() > 1) {
            construirMensagemMisto();
        }

        construirCamposUnificadosLote(cat);
        relayoutEExibir();
    }

    void relayout(int largura) {
        const auto& tk = matriz::ui::tema();
        int y = tk.espacoPainel;
        int x = tk.espacoPainel;
        int larguraUtil = largura - 2 * tk.espacoPainel;

        if (cabecalho_) {
            cabecalho_->setBounds(x, y, larguraUtil, 24);
            y += 24 + tk.espacoGrande;
        }
        if (mensagem_) {
            mensagem_->setBounds(x, y, larguraUtil, 44);
            y += 44 + tk.espacoMedio;
        }
        for (auto& b : botoesTipo_) {
            b->setBounds(x, y, larguraUtil, 26);
            y += 26 + tk.espacoPequeno;
        }
        bool ehDuasColunas = (larguraUtil >= 500);

        if (ehDuasColunas) {
            int gap = 16;
            int colW = (larguraUtil - gap) / 2;
            int x0 = x;
            int x1 = x + colW + gap;
            int y0 = y;
            int y1 = y;

            for (auto* linha : linhas_) {
                if (!linha) continue;

                bool useCol1 = (y1 < y0);
                int currX = useCol1 ? x1 : x0;
                int& currY = useCol1 ? y1 : y0;

                int rotuloW = colW - 85;
                linha->rotulo->setBounds(currX, currY, rotuloW, 16);
                linha->badge->setBounds(currX + colW - 80, currY, 80, 16);
                currY += 18;

                if (linha->ehOriginalSourceMedium) {
                    if (auto* osm = dynamic_cast<OriginalSourceMediumEditorComponent*>(linha->editor.get())) {
                        int prefH = osm->getPreferredHeight();
                        linha->editor->setBounds(currX, currY, colW, prefH);
                        currY += prefH + tk.espacoPequeno;
                    } else {
                        linha->editor->setBounds(currX, currY, colW, 24);
                        currY += 24 + tk.espacoPequeno;
                    }
                } else if (linha->ehPeople) {
                    linha->editor->setBounds(currX, currY, colW, 26);
                    currY += 26 + tk.espacoPequeno;
                } else if (linha->ehNotes) {
                    linha->editor->setBounds(currX, currY, colW, 64);
                    currY += 64 + tk.espacoPequeno;
                } else {
                    linha->editor->setBounds(currX, currY, colW, 24);
                    currY += 24 + tk.espacoPequeno;
                }
            }

            if (geoLote_.titulo) {
                bool useCol1 = (y1 < y0);
                int currX = useCol1 ? x1 : x0;
                int& currY = useCol1 ? y1 : y0;

                currY += tk.espacoPequeno;
                int rotuloW = colW - 85;
                geoLote_.titulo->setBounds(currX, currY, rotuloW, 16);
                if (geoLote_.badge) geoLote_.badge->setBounds(currX + colW - 80, currY, 80, 16);
                currY += 18;

                auto layoutGeoSubfield = [&](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed) {
                    if (lbl && ed) {
                        lbl->setBounds(currX, currY, colW, 16);
                        currY += 18;
                        ed->setBounds(currX, currY, colW, 24);
                        currY += 24 + tk.espacoPequeno;
                    }
                };

                layoutGeoSubfield(geoLote_.labelCoords, geoLote_.editorCoords);
                layoutGeoSubfield(geoLote_.labelAddress, geoLote_.editorAddress);
                layoutGeoSubfield(geoLote_.labelCity, geoLote_.editorCity);
                layoutGeoSubfield(geoLote_.labelState, geoLote_.editorState);
                layoutGeoSubfield(geoLote_.labelCountry, geoLote_.editorCountry);
            }

            int maxY = std::max(y0, y1);

            if (previa_) {
                previa_->setBounds(x, maxY, larguraUtil, 36);
                maxY += 36 + tk.espacoPequeno;
            }
            if (botaoAplicar_) {
                botaoAplicar_->setBounds(x, maxY, 120, 28);
                if (botaoDesfazer_) botaoDesfazer_->setBounds(x + 128, maxY, 120, 28);
                maxY += 28 + tk.espacoMedio;
            }
            if (resultado_) {
                resultado_->setBounds(x, maxY, larguraUtil, 20);
                maxY += 20 + tk.espacoMedio;
            }

            setSize(largura, maxY + tk.espacoPainel);
            return;
        }

        for (auto* linha : linhas_) {
            int rotuloW = larguraUtil - 95;
            linha->rotulo->setBounds(x, y, rotuloW, 16);
            linha->badge->setBounds(x + larguraUtil - 90, y, 90, 16);
            y += 18;

            if (linha->ehOriginalSourceMedium) {
                if (auto* osm = dynamic_cast<OriginalSourceMediumEditorComponent*>(linha->editor.get())) {
                    int prefH = osm->getPreferredHeight();
                    linha->editor->setBounds(x, y, larguraUtil, prefH);
                    y += prefH + tk.espacoPequeno;
                } else {
                    linha->editor->setBounds(x, y, larguraUtil, 24);
                    y += 24 + tk.espacoPequeno;
                }
            } else if (linha->ehPeople) {
                linha->editor->setBounds(x, y, larguraUtil, 26);
                y += 26 + tk.espacoPequeno;
            } else if (linha->ehNotes) {
                linha->editor->setBounds(x, y, larguraUtil, 64);
                y += 64 + tk.espacoPequeno;
            } else {
                linha->editor->setBounds(x, y, larguraUtil, 24);
                y += 24 + tk.espacoPequeno;
            }
        }
        if (geoLote_.titulo) {
            y += tk.espacoPequeno;
            int rotuloW = larguraUtil - 95;
            geoLote_.titulo->setBounds(x, y, rotuloW, 16);
            if (geoLote_.badge) geoLote_.badge->setBounds(x + larguraUtil - 90, y, 90, 16);
            y += 18;

            auto layoutGeoSubfield = [&](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed) {
                if (lbl) { lbl->setBounds(x, y, larguraUtil, 16); y += 18; }
                if (ed) { ed->setBounds(x, y, larguraUtil, 24); y += 24 + tk.espacoPequeno; }
            };

            layoutGeoSubfield(geoLote_.labelCoords, geoLote_.editorCoords);
            layoutGeoSubfield(geoLote_.labelAddress, geoLote_.editorAddress);
            layoutGeoSubfield(geoLote_.labelCity, geoLote_.editorCity);
            layoutGeoSubfield(geoLote_.labelState, geoLote_.editorState);
            layoutGeoSubfield(geoLote_.labelCountry, geoLote_.editorCountry);
        }
        if (previa_) {
            previa_->setBounds(x, y, larguraUtil, 36);
            y += 36 + tk.espacoPequeno;
        }
        if (botaoAplicar_) {
            botaoAplicar_->setBounds(x, y, 120, 28);
            if (botaoDesfazer_) botaoDesfazer_->setBounds(x + 128, y, 120, 28);
            y += 28 + tk.espacoMedio;
        }
        if (resultado_) {
            resultado_->setBounds(x, y, larguraUtil, 20);
            y += 20 + tk.espacoMedio;
        }
        setSize(largura, y + tk.espacoPainel);
    }

    std::function<void()> aoRelayoutNecessario;
    std::function<void()> aoAplicarEmLote;

    juce::Component* editorDoCampoParaTeste(const std::string& campoId) {
        for (auto* linha : linhas_) {
            if (linha->campoId == campoId) return linha->editor.get();
        }
        if (campoId == "maquina") {
            for (auto* linha : linhas_) {
                if (linha->campoId == "notes") return linha->editor.get();
            }
        }
        for (auto* linha : linhas_) {
            if (campoId == "velocidade" && (linha->campoId == "content" || linha->campoId == "collection")) {
                return linha->editor.get();
            }
        }
        if (!linhas_.isEmpty()) return linhas_.getFirst()->editor.get();
        return nullptr;
    }

    juce::TextButton* botaoTipoMidiaParaTeste(const std::string& tipoId) {
        auto opcoes = listarTiposMidiaDisponiveis(projeto_);
        for (size_t i = 0; i < opcoes.size() && i < botoesTipo_.size(); ++i)
            if (opcoes[i].id == tipoId) return botoesTipo_[i].get();
        return nullptr;
    }
    juce::TextButton* botaoAplicarParaTeste() { return botaoAplicar_.get(); }
    juce::TextButton* botaoDesfazerParaTeste() { return botaoDesfazer_.get(); }

private:
    struct LinhaLote {
        std::string campoId;
        std::string colunaDb;
        std::unique_ptr<juce::Label>       rotulo;
        std::unique_ptr<juce::Label>       badge;
        std::unique_ptr<juce::Component>   editor;
        bool tocado = false;
        bool ehNotes = false;
        bool ehPeople = false;
        bool ehTags = false;
        bool ehDropdown = false;
        bool ehOriginalSourceMedium = false;
        std::vector<std::string> opcoes;
    };

    struct SnapshotItem {
        std::string ano;
        std::string source_media;
        std::string collection_type;
        std::string isrc;
        std::string notas_livres;
        std::string maquina;
        std::vector<std::string> tags;
        std::optional<matriz::analytics::AssetGeolocation> geo;
        std::map<std::string, std::string> campos;
    };

    struct SecaoGeoLote {
        std::unique_ptr<juce::Label> titulo;
        std::unique_ptr<juce::Label> badge;
        std::unique_ptr<juce::Label> labelCoords;
        std::unique_ptr<juce::TextEditor> editorCoords;
        std::unique_ptr<juce::Label> labelAddress;
        std::unique_ptr<juce::TextEditor> editorAddress;
        std::unique_ptr<juce::Label> labelCity;
        std::unique_ptr<juce::TextEditor> editorCity;
        std::unique_ptr<juce::Label> labelState;
        std::unique_ptr<juce::TextEditor> editorState;
        std::unique_ptr<juce::Label> labelCountry;
        std::unique_ptr<juce::TextEditor> editorCountry;
        bool tocado = false;
    };

    void limpar() {
        cabecalho_.reset();
        mensagem_.reset();
        botoesTipo_.clear();
        linhas_.clear();
        geoLote_ = {};
        previa_.reset();
        botaoAplicar_.reset();
        botaoDesfazer_.reset();
        resultado_.reset();
        undoSnapshot_.clear();
        setSize(getWidth(), 0);
    }

    void construirMensagemMisto() {
        mensagem_ = std::make_unique<juce::Label>();
        mensagem_->setText(matriz::i18n::t("ficha.lote_tipo_misto"), juce::dontSendNotification);
        mensagem_->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        mensagem_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo)));
        addAndMakeVisible(*mensagem_);
    }

    void construirSeletorTipoMidia() {
        mensagem_ = std::make_unique<juce::Label>();
        mensagem_->setText(matriz::i18n::t("ficha.lote_escolher_tipo"), juce::dontSendNotification);
        mensagem_->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        mensagem_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo)));
        addAndMakeVisible(*mensagem_);

        std::vector<std::string> exts;
        std::set<std::string> setIds(itemIds_.begin(), itemIds_.end());
        auto details = projeto_.obterDetalhesItens(setIds);
        for (const auto& d : details) if (!d.extensao.empty()) exts.push_back(d.extensao);

        auto opcoes = exts.empty() ? listarTiposMidiaDisponiveis(projeto_)
                                  : listarTiposMidiaDisponiveisParaExtensoes(projeto_, exts);

        for (auto& opcao : opcoes) {
            auto botao = std::make_unique<juce::TextButton>(opcao.rotulo);
            std::string tipoId = opcao.id;
            botao->onClick = [this, tipoId] { aplicarTipoMidia(tipoId); };
            addAndMakeVisible(*botao);
            botoesTipo_.push_back(std::move(botao));
        }
    }

    void aplicarTipoMidia(const std::string& tipo) {
        projeto_.aplicarTipoMidiaEmLote(itemIds_, tipo);
        if (aoAplicarEmLote) aoAplicarEmLote();
        mostrarSelecao(itemIds_);
    }

    void construirCamposUnificadosLote(MediaCategory cat) {
        const auto& tk = matriz::ui::tema();

        auto addEditableTextLote = [this, &tk](const std::string& campoId, const juce::String& rotulo, const std::string& dbColuna, bool ehNotes = false) {
            auto* linha = linhas_.add(new LinhaLote());
            linha->campoId = campoId;
            linha->colunaDb = dbColuna;
            linha->ehNotes = ehNotes;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(rotulo, juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText("[EDITABLE]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto ed = std::make_unique<juce::TextEditor>();
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            ed->setColour(juce::TextEditor::outlineColourId, tk.borda);

            if (ehNotes) {
                ed->setMultiLine(true);
                ed->setReturnKeyStartsNewLine(true);
                ed->setTextToShowWhenEmpty("Type notes to add/append to all selected items...", juce::Colour(0xff888888));
            } else {
                std::string valorComum;
                bool todosIguais = true;
                bool primeiro = true;
                for (const auto& id : itemIds_) {
                    std::string v = projeto_.lerMetadado(id, dbColuna).value_or("");
                    if (primeiro) { valorComum = v; primeiro = false; }
                    else if (v != valorComum) { todosIguais = false; break; }
                }
                if (todosIguais && !valorComum.empty()) {
                    ed->setText(valorComum, false);
                } else {
                    ed->setTextToShowWhenEmpty(todosIguais ? "" : matriz::i18n::t("ficha.lote_valores_multiplos"), juce::Colour(0xff888888));
                }
            }

            ed->onTextChange = [this, linha] {
                linha->tocado = true;
                atualizarPrevia();
            };
            addAndMakeVisible(*ed);
            linha->editor = std::move(ed);
        };

        auto addDropdownLote = [this, &tk](const std::string& campoId, const juce::String& rotulo, const std::string& dbColuna, const std::vector<std::string>& opcoes) {
            auto* linha = linhas_.add(new LinhaLote());
            linha->campoId = campoId;
            linha->colunaDb = dbColuna;
            linha->ehDropdown = true;
            linha->opcoes = opcoes;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(rotulo, juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText("[DROPDOWN]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto cb = std::make_unique<juce::ComboBox>();
            cb->setColour(juce::ComboBox::textColourId, juce::Colours::black);
            cb->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
            cb->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
            cb->setColour(juce::ComboBox::outlineColourId, tk.borda);

            int id = 1;
            for (const auto& op : opcoes) {
                cb->addItem(juce::String::fromUTF8(op.c_str()), id++);
            }

            std::string valorComum;
            bool todosIguais = true;
            bool primeiro = true;
            for (const auto& itemId : itemIds_) {
                std::string v = projeto_.lerMetadado(itemId, dbColuna).value_or("");
                if (primeiro) { valorComum = v; primeiro = false; }
                else if (v != valorComum) { todosIguais = false; break; }
            }

            if (todosIguais && !valorComum.empty()) {
                cb->setText(juce::String::fromUTF8(valorComum.c_str()), juce::dontSendNotification);
            } else {
                cb->setTextWhenNothingSelected(matriz::i18n::t("ficha.lote_valores_multiplos"));
            }

            cb->onChange = [this, linha] {
                linha->tocado = true;
                atualizarPrevia();
            };
            addAndMakeVisible(*cb);
            linha->editor = std::move(cb);
        };

        auto addPeopleLote = [this, &tk]() {
            auto* linha = linhas_.add(new LinhaLote());
            linha->campoId = "people";
            linha->ehPeople = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText("PEOPLE", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText("[PEOPLE]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto picker = std::make_unique<PeoplePickerComponent>(projeto_);
            picker->onPersonAddedToTags = [this](const juce::String& nomePessoa) {
                for (const auto& id : itemIds_) {
                    projeto_.adicionarTag(id, nomePessoa.toStdString());
                }
                for (auto* l : linhas_) {
                    if (l && l->ehTags) {
                        if (auto* ed = dynamic_cast<juce::TextEditor*>(l->editor.get())) {
                            juce::String cur = ed->getText().trim();
                            if (cur.isNotEmpty()) cur += " ";
                            ed->setText(cur + "#" + nomePessoa);
                            break;
                        }
                    }
                }
                if (aoAplicarEmLote) aoAplicarEmLote();
            };
            addAndMakeVisible(*picker);
            linha->editor = std::move(picker);
        };

        auto addTagsLote = [this, &tk]() {
            auto* linha = linhas_.add(new LinhaLote());
            linha->campoId = "tags";
            linha->ehTags = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText("TAGS", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText("[TAGS]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto ed = std::make_unique<juce::TextEditor>();
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
            ed->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
            ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
            ed->setTextToShowWhenEmpty("Add tags to all selected items (e.g. #studio #master)...", tk.textoTerciario);

            ed->onTextChange = [this, linha] {
                linha->tocado = true;
                atualizarPrevia();
            };
            addAndMakeVisible(*ed);
            linha->editor = std::move(ed);
        };

        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

        auto addOriginalSourceMediumLote = [this, &tk, isPt]() {
            auto* linha = linhas_.add(new LinhaLote());
            linha->campoId = "source_media";
            linha->colunaDb = "source_media";
            linha->ehOriginalSourceMedium = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(isPt ? juce::String::fromUTF8("MÍDIA DE ORIGEM") : juce::String("ORIGINAL SOURCE MEDIUM"), juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(isPt ? juce::String::fromUTF8("[SUBSTITUIÇÃO EM LOTE]") : juce::String("[BATCH OVERRIDE]"), juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto osm = std::make_unique<OriginalSourceMediumEditorComponent>();
            osm->onChange = [this, linha] {
                linha->tocado = true;
                atualizarPrevia();
                relayoutEExibir();
            };
            addAndMakeVisible(*osm);
            linha->editor = std::move(osm);
        };

        auto addGeolocationLote = [this, &tk, isPt]() {
            geoLote_.titulo = std::make_unique<juce::Label>();
            geoLote_.titulo->setText(isPt ? juce::String::fromUTF8("GEOLOCALIZAÇÃO") : juce::String("GEO LOCATION"), juce::dontSendNotification);
            geoLote_.titulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            geoLote_.titulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*geoLote_.titulo);

            geoLote_.badge = std::make_unique<juce::Label>();
            geoLote_.badge->setText(isPt ? juce::String::fromUTF8("[SUBSTITUIÇÃO EM LOTE]") : juce::String("[BATCH OVERRIDE]"), juce::dontSendNotification);
            geoLote_.badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            geoLote_.badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            geoLote_.badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*geoLote_.badge);

            auto makeGeoSubfield = [this, &tk](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed,
                                               const juce::String& textRotulo, const juce::String& placeholder) {
                lbl = std::make_unique<juce::Label>();
                lbl->setText(textRotulo, juce::dontSendNotification);
                lbl->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                lbl->setColour(juce::Label::textColourId, tk.textoSecundario);
                addAndMakeVisible(*lbl);

                ed = std::make_unique<juce::TextEditor>();
                ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
                ed->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
                ed->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
                ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
                ed->setTextToShowWhenEmpty(placeholder, tk.textoTerciario);
                ed->onTextChange = [this] {
                    geoLote_.tocado = true;
                    atualizarPrevia();
                };
                addAndMakeVisible(*ed);
            };

            makeGeoSubfield(geoLote_.labelCoords, geoLote_.editorCoords, isPt ? juce::String::fromUTF8("Coordenadas GPS (Lat, Long)") : juce::String("GPS Coordinates (Lat, Lng)"), "e.g. -16.4435, -39.0643");
            makeGeoSubfield(geoLote_.labelAddress, geoLote_.editorAddress, isPt ? juce::String::fromUTF8("Endereço Formatado") : juce::String("Formatted Address"), "e.g. Av. Paulista, 1000");
            makeGeoSubfield(geoLote_.labelCity, geoLote_.editorCity, isPt ? juce::String::fromUTF8("Cidade") : juce::String("City"), "e.g. Porto Seguro");
            makeGeoSubfield(geoLote_.labelState, geoLote_.editorState, isPt ? juce::String::fromUTF8("Estado / Província") : juce::String("State / Province"), "e.g. Bahia");
            makeGeoSubfield(geoLote_.labelCountry, geoLote_.editorCountry, isPt ? juce::String::fromUTF8("País") : juce::String("Country"), isPt ? juce::String::fromUTF8("Ex: Brasil") : juce::String("e.g. Brazil"));
        };

        auto addDublinCoreLote = [&addEditableTextLote, &addDropdownLote, isPt]() {
            addEditableTextLote("dc_creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), "dc_creator");
            addEditableTextLote("dc_subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), "dc_subject");
            addEditableTextLote("dc_description", isPt ? juce::String::fromUTF8("DESCRIÇÃO") : juce::String("DESCRIPTION"), "dc_description");
            addEditableTextLote("dc_publisher", isPt ? juce::String::fromUTF8("PUBLICADOR") : juce::String("PUBLISHER"), "dc_publisher");
            addEditableTextLote("dc_contributor", isPt ? juce::String::fromUTF8("COLABORADOR") : juce::String("CONTRIBUTOR"), "dc_contributor");
            addEditableTextLote("dc_issued", isPt ? juce::String::fromUTF8("DATA DE PUBLICAÇÃO (AAAA-MM-DD)") : juce::String("DATE ISSUED (YYYY-MM-DD)"), "dc_issued");
            addEditableTextLote("dc_type", isPt ? juce::String::fromUTF8("TIPO") : juce::String("TYPE"), "dc_type");
            addEditableTextLote("dc_source", isPt ? juce::String::fromUTF8("ORIGEM") : juce::String("SOURCE"), "dc_source");
            addEditableTextLote("dc_language", isPt ? juce::String::fromUTF8("IDIOMA") : juce::String("LANGUAGE"), "dc_language");
            addEditableTextLote("dc_relation", isPt ? juce::String::fromUTF8("RELAÇÃO") : juce::String("RELATION"), "dc_relation");
            addEditableTextLote("dc_coverage", isPt ? juce::String::fromUTF8("COBERTURA") : juce::String("COVERAGE"), "dc_coverage");
            addDropdownLote("dc_rights", isPt ? juce::String::fromUTF8("DIREITOS") : juce::String("RIGHTS"), "dc_rights", isPt ?
                std::vector<std::string>{"DOMÍNIO PÚBLICO", "DIREITOS AUTORAIS (COPYRIGHT)", "CREATIVE COMMONS (CC BY)",
                                         "CREATIVE COMMONS (CC BY-SA)", "CREATIVE COMMONS (CC BY-NC)", "CREATIVE COMMONS (CC BY-NC-ND)",
                                         "CREATIVE COMMONS (CC0)", "OBRA ÓRFÃ", "USO ACEITÁVEL (FAIR USE)", "RESTRITO"} :
                std::vector<std::string>{"PUBLIC DOMAIN", "COPYRIGHT", "CREATIVE COMMONS (CC BY)", "CREATIVE COMMONS (CC BY-SA)",
                                         "CREATIVE COMMONS (CC BY-NC)", "CREATIVE COMMONS (CC BY-NC-ND)", "CREATIVE COMMONS (CC0)",
                                         "ORPHAN WORK", "FAIR USE", "RESTRICTED"});
        };

        switch (cat) {
            case MediaCategory::Audio: {
                addDublinCoreLote();
                addEditableTextLote("year", isPt ? "ANO" : "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Audio, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addEditableTextLote("isrc", "ISRC", "isrc");
                addEditableTextLote("notes", isPt ? "NOTAS" : "NOTES", "notas_livres", true);
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
            case MediaCategory::Video: {
                addDublinCoreLote();
                addEditableTextLote("year", isPt ? "ANO" : "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Video, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addEditableTextLote("notes", isPt ? "NOTAS" : "NOTES", "notas_livres", true);
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
            case MediaCategory::Image: {
                addDublinCoreLote();
                addEditableTextLote("year", isPt ? "ANO" : "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Image, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addEditableTextLote("notes", isPt ? "NOTAS" : "NOTES", "notas_livres", true);
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
            case MediaCategory::Docs: {
                addDublinCoreLote();
                addEditableTextLote("year", isPt ? "ANO" : "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Docs, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addEditableTextLote("notes", isPt ? "NOTAS" : "NOTES", "notas_livres", true);
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
            case MediaCategory::Mixed:
            default: {
                addDublinCoreLote();
                addEditableTextLote("year", isPt ? "ANO" : "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Mixed, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addEditableTextLote("notes", isPt ? "NOTAS" : "NOTES", "notas_livres", true);
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
        }

        previa_ = std::make_unique<juce::Label>();
        previa_->setColour(juce::Label::textColourId, tk.textoSecundario);
        previa_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        addAndMakeVisible(*previa_);

        botaoAplicar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.lote_aplicar"));
        botaoAplicar_->onClick = [this] { aplicar(); };
        addAndMakeVisible(*botaoAplicar_);

        botaoDesfazer_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.lote_desfazer"));
        botaoDesfazer_->onClick = [this] { desfazer(); };
        addAndMakeVisible(*botaoDesfazer_);
        botaoDesfazer_->setVisible(false);

        resultado_ = std::make_unique<juce::Label>();
        resultado_->setColour(juce::Label::textColourId, tk.textoSecundario);
        resultado_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        addAndMakeVisible(*resultado_);

        atualizarPrevia();
    }

    juce::String lerValorLinha(const LinhaLote& linha) const {
        if (linha.ehOriginalSourceMedium) {
            if (auto* osm = dynamic_cast<OriginalSourceMediumEditorComponent*>(linha.editor.get())) {
                return juce::String::fromUTF8(osm->getValueString().c_str());
            }
        }
        if (auto* cb = dynamic_cast<juce::ComboBox*>(linha.editor.get())) {
            int id = cb->getSelectedId();
            if (id > 0 && id <= static_cast<int>(linha.opcoes.size()))
                return linha.opcoes[static_cast<size_t>(id - 1)];
            return cb->getText();
        }
        if (auto* te = dynamic_cast<juce::TextEditor*>(linha.editor.get())) {
            return te->getText();
        }
        return {};
    }

    void atualizarPrevia() {
        juce::StringArray partes;
        for (auto* linha : linhas_) {
            if (linha->tocado) {
                juce::String val = lerValorLinha(*linha);
                if (val.isNotEmpty()) {
                    partes.add(linha->rotulo->getText() + "=" + val);
                }
            }
        }
        if (geoLote_.tocado) {
            partes.add("GEO LOCATION=[BATCH OVERRIDE]");
        }

        if (partes.isEmpty()) {
            previa_->setText(matriz::i18n::t("ficha.lote_nenhum_campo_tocado"), juce::dontSendNotification);
            if (botaoAplicar_) botaoAplicar_->setEnabled(false);
        } else {
            previa_->setText(matriz::i18n::t("ficha.lote_previa")
                                  .replace("{n}", juce::String((int)itemIds_.size()))
                                  .replace("{campos}", partes.joinIntoString(", ")),
                              juce::dontSendNotification);
            if (botaoAplicar_) botaoAplicar_->setEnabled(true);
        }
    }

    void aplicar() {
        bool algumTocado = false;
        for (auto* l : linhas_) if (l->tocado) { algumTocado = true; break; }
        if (geoLote_.tocado) algumTocado = true;
        if (!algumTocado || itemIds_.empty()) return;

        ProgressoGlobal::obterInstancia().iniciarTarefa("batch_edit", "Applying Batch Edits", (int)itemIds_.size(), nullptr, "Updating " + juce::String((int)itemIds_.size()) + " assets...");

        projeto_.iniciarGrupoUndo("Batch apply");

        undoSnapshot_.clear();
        for (const auto& id : itemIds_) {
            SnapshotItem snap;
            snap.ano = projeto_.lerMetadado(id, "ano").value_or("");
            snap.source_media = projeto_.lerMetadado(id, "source_media").value_or("");
            snap.collection_type = projeto_.lerMetadado(id, "collection_type").value_or("");
            snap.isrc = projeto_.lerMetadado(id, "isrc").value_or("");
            snap.notas_livres = projeto_.lerMetadado(id, "notas_livres").value_or("");
            snap.maquina = projeto_.valorCampo(id, "raiz", 0, "maquina").value_or("");
            snap.tags = projeto_.lerTags(id);
            snap.geo = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(projeto_.projeto().registro(), id);
            for (auto* linha : linhas_) {
                if (linha && !linha->colunaDb.empty()) {
                    snap.campos[linha->colunaDb] = projeto_.lerMetadado(id, linha->colunaDb).value_or("");
                }
            }
            undoSnapshot_[id] = snap;
        }

        int sucessos = 0;
        int falhas = 0;

        for (const auto& id : itemIds_) {
            try {
                for (auto* linha : linhas_) {
                    if (!linha->tocado) continue;
                    juce::String val = lerValorLinha(*linha);

                    if (linha->ehTags) {
                        juce::StringArray tagsNovas;
                        tagsNovas.addTokens(val, " ,;", "\"");
                        for (int t = 0; t < tagsNovas.size(); ++t) {
                            juce::String tg = tagsNovas[t].trimCharactersAtStart("#").trim();
                            if (tg.isNotEmpty()) {
                                projeto_.adicionarTag(id, tg.toStdString());
                            }
                        }
                    } else if (linha->ehNotes) {
                        std::string txt = val.toStdString();
                        if (!txt.empty()) {
                            std::string existing = projeto_.lerMetadado(id, "notas_livres").value_or("");
                            std::string combined = existing.empty() ? txt : (existing + "\n" + txt);
                            projeto_.salvarMetadado(id, "notas_livres", combined);
                            std::string agora = matriz::model::agoraIso8601();
                            projeto_.projeto().registro().run(
                                "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                                "VALUES (?, ?, 'raiz', 0, 'maquina', ?, 'humano', ?) "
                                "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, atualizado_em = excluded.atualizado_em",
                                {matriz::db::Value::of(matriz::model::novoUuid()),
                                 matriz::db::Value::of(id),
                                 matriz::db::Value::of(txt),
                                 matriz::db::Value::of(agora)});
                        }
                    } else if (linha->colunaDb == "collection_type") {
                        juce::String canon = traduzirContent(val, false);
                        projeto_.salvarMetadado(id, "collection_type", canon.toStdString());
                    } else if (linha->colunaDb == "ai_generated") {
                        if (val.containsIgnoreCase("SIM") || val.containsIgnoreCase("YES")) {
                            projeto_.salvarMetadado(id, "ai_generated", "AI Generated / Gerado por IA");
                            projeto_.adicionarTag(id, "IA");
                            projeto_.adicionarTag(id, "AI");
                            projeto_.adicionarTag(id, "CONTEUDO IA");
                            projeto_.adicionarTag(id, "AI CONTENT");
                        } else {
                            projeto_.salvarMetadado(id, "ai_generated", "");
                            projeto_.removerTag(id, "IA");
                            projeto_.removerTag(id, "AI");
                            projeto_.removerTag(id, "CONTEUDO IA");
                            projeto_.removerTag(id, "AI CONTENT");
                        }
                    } else {
                        projeto_.salvarMetadado(id, linha->colunaDb, val.toStdString());
                    }
                }

                if (geoLote_.tocado) {
                    matriz::analytics::AssetGeolocation geoTemplate;
                    std::string coordsText = geoLote_.editorCoords ? geoLote_.editorCoords->getText().trim().toStdString() : "";
                    if (!coordsText.empty()) {
                        auto commaPos = coordsText.find(',');
                        if (commaPos != std::string::npos) {
                            try {
                                double lat = std::stod(coordsText.substr(0, commaPos));
                                double lng = std::stod(coordsText.substr(commaPos + 1));
                                if (lat >= -90.0 && lat <= 90.0 && lng >= -180.0 && lng <= 180.0) {
                                    geoTemplate.latitude = lat;
                                    geoTemplate.longitude = lng;
                                    geoTemplate.source = matriz::analytics::GeoSource::UserCoordinates;
                                }
                            } catch (...) {}
                        }
                    }
                    std::string addr = geoLote_.editorAddress ? geoLote_.editorAddress->getText().trim().toStdString() : "";
                    if (!addr.empty()) {
                        geoTemplate.formattedAddress = addr;
                        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserAddress;
                    }
                    std::string city = geoLote_.editorCity ? geoLote_.editorCity->getText().trim().toStdString() : "";
                    if (!city.empty()) {
                        geoTemplate.city = city;
                        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserCity;
                    }
                    std::string state = geoLote_.editorState ? geoLote_.editorState->getText().trim().toStdString() : "";
                    if (!state.empty()) {
                        geoTemplate.stateProvince = state;
                        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserState;
                    }
                    std::string country = geoLote_.editorCountry ? geoLote_.editorCountry->getText().trim().toStdString() : "";
                    if (!country.empty()) {
                        geoTemplate.country = country;
                        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserCountry;
                    }

                    if (geoTemplate.hasAnyLocationData()) {
                        geoTemplate.assetId = id;
                        matriz::analytics::AssetGeolocationRepository::salvar(projeto_.projeto().registro(), geoTemplate);
                    }
                }
                ++sucessos;
            } catch (const std::exception&) {
                ++falhas;
            }
            ProgressoGlobal::obterInstancia().atualizarProgresso("batch_edit", sucessos + falhas, juce::String(sucessos + falhas) + " of " + juce::String((int)itemIds_.size()) + " updated");
        }

        projeto_.finalizarGrupoUndo();

        juce::String texto = matriz::i18n::t("ficha.lote_resultado").replace("{sucessos}", juce::String(sucessos));
        if (falhas > 0) texto += matriz::i18n::t("ficha.lote_resultado_falhas").replace("{falhas}", juce::String(falhas));
        resultado_->setText(texto, juce::dontSendNotification);
        if (botaoDesfazer_) botaoDesfazer_->setVisible(sucessos > 0);
        relayoutEExibir();

        ProgressoGlobal::obterInstancia().concluirTarefa("batch_edit", texto);

        if (aoAplicarEmLote) aoAplicarEmLote();
    }

    void desfazer() {
        ProgressoGlobal::obterInstancia().iniciarTarefa("batch_undo", "Reverting Batch Edits", (int)undoSnapshot_.size(), nullptr, "Reverting changes...");
        int restaurados = 0;
        for (const auto& [id, snap] : undoSnapshot_) {
            try {
                for (const auto& [coluna, valor] : snap.campos) {
                    projeto_.salvarMetadado(id, coluna, valor);
                }
                projeto_.salvarMetadado(id, "ano", snap.ano);
                projeto_.salvarMetadado(id, "source_media", snap.source_media);
                projeto_.salvarMetadado(id, "collection_type", snap.collection_type);
                projeto_.salvarMetadado(id, "isrc", snap.isrc);
                projeto_.salvarMetadado(id, "notas_livres", snap.notas_livres);
                projeto_.definirTags(id, snap.tags);
                if (!snap.maquina.empty()) {
                    std::string agora = matriz::model::agoraIso8601();
                    projeto_.projeto().registro().run(
                        "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                        "VALUES (?, ?, 'raiz', 0, 'maquina', ?, 'humano', ?) "
                        "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, atualizado_em = excluded.atualizado_em",
                        {matriz::db::Value::of(matriz::model::novoUuid()),
                         matriz::db::Value::of(id),
                         matriz::db::Value::of(snap.maquina),
                         matriz::db::Value::of(agora)});
                } else {
                    projeto_.projeto().registro().run(
                        "DELETE FROM item_campo WHERE item_id = ? AND nivel = 'raiz' AND nivel_indice = 0 AND campo_id = 'maquina'",
                        {matriz::db::Value::of(id)});
                }

                if (snap.geo.has_value()) {
                    matriz::analytics::AssetGeolocationRepository::salvar(projeto_.projeto().registro(), *snap.geo);
                } else {
                    matriz::analytics::AssetGeolocationRepository::remover(projeto_.projeto().registro(), id);
                }
                ++restaurados;
            } catch (const std::exception&) {
            }
            ProgressoGlobal::obterInstancia().atualizarProgresso("batch_undo", restaurados, juce::String(restaurados) + " restored");
        }
        resultado_->setText(matriz::i18n::t("ficha.lote_resultado_desfeito").replace("{n}", juce::String(restaurados)),
                             juce::dontSendNotification);
        if (botaoDesfazer_) botaoDesfazer_->setVisible(false);
        undoSnapshot_.clear();
        ProgressoGlobal::obterInstancia().concluirTarefa("batch_undo", matriz::i18n::t("ficha.lote_resultado_desfeito").replace("{n}", juce::String(restaurados)));
        if (aoAplicarEmLote) aoAplicarEmLote();
    }

    void relayoutEExibir() {
        if (aoRelayoutNecessario) aoRelayoutNecessario();
    }

    ProjetoAberto& projeto_;
    std::vector<std::string> itemIds_;
    std::unique_ptr<juce::Label> cabecalho_;
    std::unique_ptr<juce::Label> mensagem_;
    std::vector<std::unique_ptr<juce::TextButton>> botoesTipo_;
    juce::OwnedArray<LinhaLote> linhas_;
    SecaoGeoLote geoLote_;
    std::unique_ptr<juce::Label> previa_;
    std::unique_ptr<juce::TextButton> botaoAplicar_;
    std::unique_ptr<juce::TextButton> botaoDesfazer_;
    std::unique_ptr<juce::Label> resultado_;
    std::map<std::string, SnapshotItem> undoSnapshot_;
};

// ---------------------------------------------------------------------------
// FichaPanelComponent
// ---------------------------------------------------------------------------

FichaPanelComponent::FichaPanelComponent(ProjetoAberto& projeto) : projeto_(projeto) {
    conteudo_ = std::make_unique<FichaConteudo>(projeto);
    viewport_ = std::make_unique<juce::Viewport>();
    viewport_->setViewedComponent(conteudo_.get(), false);
    addAndMakeVisible(*viewport_);

    conteudo_->aoRelayoutNecessario = [this] { conteudo_->relayout(viewport_->getWidth() - viewport_->getScrollBarThickness()); };
    conteudo_->aoMudarClassificacao = [this] { if (aoAplicarEmLote) aoAplicarEmLote(); };
    conteudo_->aoMudar = [this] { if (aoMudar) aoMudar(); };
    conteudo_->aoAplicarSucesso = [this](const std::string& itemId) { if (aoAplicarSucesso) aoAplicarSucesso(itemId); };
}

FichaPanelComponent::~FichaPanelComponent() = default;

juce::Component* FichaPanelComponent::editorDoCampoParaTeste(const std::string& nivel, int nivelIndice,
                                                               const std::string& campoId) {
    return conteudo_->editorDoCampoParaTeste(nivel, nivelIndice, campoId);
}

juce::Component* FichaPanelComponent::editorDoCampoLoteParaTeste(const std::string& campoId) {
    return conteudoLote_ ? conteudoLote_->editorDoCampoParaTeste(campoId) : nullptr;
}

juce::TextButton* FichaPanelComponent::botaoTipoMidiaLoteParaTeste(const std::string& tipoId) {
    return conteudoLote_ ? conteudoLote_->botaoTipoMidiaParaTeste(tipoId) : nullptr;
}

juce::TextButton* FichaPanelComponent::botaoAplicarLoteParaTeste() {
    return conteudoLote_ ? conteudoLote_->botaoAplicarParaTeste() : nullptr;
}

juce::TextButton* FichaPanelComponent::botaoDesfazerLoteParaTeste() {
    return conteudoLote_ ? conteudoLote_->botaoDesfazerParaTeste() : nullptr;
}

juce::TextButton* FichaPanelComponent::botaoTipoMidiaIndividualParaTeste(const std::string& tipoId) {
    return conteudo_ ? conteudo_->botaoTipoMidiaParaTeste(tipoId) : nullptr;
}

void FichaPanelComponent::setEditavel(bool editavel) {
    editavel_ = editavel;
    if (conteudo_) conteudo_->setEnabled(editavel);
    if (conteudoLote_) conteudoLote_->setEnabled(editavel);
    repaint();
}

void FichaPanelComponent::mostrarItem(const std::string& itemId) {
    itemIdAtual_ = itemId;
    modoLote_ = false;
    viewport_->setViewedComponent(conteudo_.get(), false);
    conteudo_->construirParaItem(itemId);
    resized();
    repaint();
}

void FichaPanelComponent::mostrarSelecao(const std::vector<std::string>& itemIds) {
    if (itemIds.size() <= 1) {
        mostrarItem(itemIds.empty() ? std::string() : itemIds.front());
        return;
    }

    itemIdAtual_.clear();
    modoLote_ = true;
    if (!conteudoLote_) {
        conteudoLote_ = std::make_unique<FichaLoteConteudo>(projeto_);
        conteudoLote_->aoRelayoutNecessario = [this] {
            conteudoLote_->relayout(viewport_->getWidth() - viewport_->getScrollBarThickness());
        };
        conteudoLote_->aoAplicarEmLote = [this] { if (aoAplicarEmLote) aoAplicarEmLote(); };
    }
    viewport_->setViewedComponent(conteudoLote_.get(), false);
    conteudoLote_->mostrarSelecao(itemIds);
    resized();
    repaint();
}

void FichaPanelComponent::paint(juce::Graphics& g) {
    const auto& tk = matriz::ui::tema();
    g.fillAll(tk.fundo);

    if (!modoLote_ && itemIdAtual_.empty()) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("ficha.vazia"), getLocalBounds().reduced(tk.espacoPainel).withTrimmedTop(32),
                   juce::Justification::centredTop, true);
    }
}

void FichaPanelComponent::resized() {
    auto area = getLocalBounds();
    viewport_->setBounds(area);
    int largura = viewport_->getWidth() - viewport_->getScrollBarThickness();
    if (modoLote_ && conteudoLote_) conteudoLote_->relayout(largura);
    else conteudo_->relayout(largura);
}

void FichaPanelComponent::lookAndFeelChanged() {
    if (modoLote_ && conteudoLote_) {
        conteudoLote_->sendLookAndFeelChange();
        conteudoLote_->repaint();
    } else if (conteudo_) {
        if (!itemIdAtual_.empty()) {
            conteudo_->construirParaItem(itemIdAtual_);
        }
        conteudo_->sendLookAndFeelChange();
        conteudo_->repaint();
    }
    repaint();
}

} // namespace matriz::ui
