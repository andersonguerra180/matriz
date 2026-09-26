#include "FichaPanelComponent.h"

#include "AutoCompleteTextEditor.h"
#include "MetadadosOriginaisComponent.h"
#include "OriginalSourceMedium.h"
#include "TagChipsEditor.h"
#include "PeoplePickerComponent.h"
#include "NotesEstruturadasComponent.h"
#include "../Analytics/AssetGeolocation.h"

#include "../Ficha/AutocompleteHistorico.h"
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
#include <AssetsBinaryData.h>
#include <exiv2/exiv2.hpp>

#include <algorithm>
#include <iterator>
#include <regex>
#include <set>

namespace matriz::ui {

using matriz::ficha::Campo;
using matriz::ficha::CampoTipo;
using matriz::ficha::FichaDefinition;
using matriz::ficha::VisivelSeOperador;

namespace {

// Altura mínima do editor de NOTES no card ASSET & USER METADATA — usada no
// modo de item único e no modo de seleção múltipla (lote), pra manter as
// duas telas com o mesmo tamanho mínimo de caixa (item 1 da 3ª correção de UI).
constexpr int kAlturaMinimaNotas = 110;

std::string autorAtual() { return juce::SystemStats::getFullUserName().toStdString(); }

// Feedback visual simples ao salvar (Correção realtime, Bug 1, item 3): pisca
// a borda do campo em destaque por um instante. Não afeta o texto nem o
// foco — só uma pista visual de que o valor foi gravado.
void piscarBordaSalvo(juce::TextEditor* ed) {
    if (!ed) return;
    // Sem o botão APPLY, este flash é a única confirmação de que o Enter (ou
    // o blur) gravou. Só a borda era discreto demais — o fundo também pisca,
    // e por mais tempo, para o retorno ser inequívoco.
    juce::Colour bordaOriginal = ed->findColour(juce::TextEditor::outlineColourId);
    juce::Colour fundoOriginal = ed->findColour(juce::TextEditor::backgroundColourId);
    juce::Colour destaque = matriz::ui::tema().acento;
    ed->setColour(juce::TextEditor::outlineColourId, destaque);
    ed->setColour(juce::TextEditor::backgroundColourId, destaque.withAlpha(0.22f));
    ed->repaint();
    juce::Component::SafePointer<juce::TextEditor> safe(ed);
    juce::Timer::callAfterDelay(450, [safe, bordaOriginal, fundoOriginal]() {
        if (safe) {
            safe->setColour(juce::TextEditor::outlineColourId, bordaOriginal);
            safe->setColour(juce::TextEditor::backgroundColourId, fundoOriginal);
            safe->repaint();
        }
    });
}

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
        {"WhatsApp Video", juce::String::fromUTF8("Vídeo do WhatsApp")},
        {"TV Video", juce::String::fromUTF8("Vídeo de TV")},
        {"YouTube Video", juce::String::fromUTF8("Vídeo do YouTube")},
        {"360 Video", juce::String::fromUTF8("Vídeo 360°")},
        {"Making Of", juce::String::fromUTF8("Making Of")},
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
        // Item "CONTENT — imagens também podem ser documentos": uma imagem
        // (JPG/PNG/TIFF/etc.) pode ser um documento digitalizado — isso é
        // classificação semântica de CONTENT, não altera o tipo técnico
        // nativo do arquivo (que continua "imagem" em toda a detecção).
        {"Document", "Documento"},
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
                "Corporate Video", "Commercial", "Live Performance", "NLE Project", "Social Media Video",
                "WhatsApp Video", "TV Video", "YouTube Video", "360 Video", "Making Of"};
    } else if (cat == MediaCategory::Image) {
        keys = {"Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional", "Image Edit Project",
                "Graphics", "Logo", "3D", "Document"};
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
    explicit FichaConteudo(ProjetoAberto& projeto) : projeto_(projeto) {
        // Ícone GEO LOCATION — o PNG vem com fundo branco chapado; aqui ele
        // vira transparente para o ícone assentar sobre o fundo do card.
        iconeGeo_ = juce::ImageFileFormat::loadFrom(AssetsBinaryData::geo_png, AssetsBinaryData::geo_pngSize);
        if (iconeGeo_.isValid()) {
            iconeGeo_ = iconeGeo_.convertedToFormat(juce::Image::ARGB);
            juce::Image::BitmapData bmp(iconeGeo_, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < bmp.height; ++y) {
                for (int x = 0; x < bmp.width; ++x) {
                    auto cor = bmp.getPixelColour(x, y);
                    if (cor.getRed() >= 240 && cor.getGreen() >= 240 && cor.getBlue() >= 240)
                        bmp.setPixelColour(x, y, juce::Colours::transparentBlack);
                }
            }
        }
    }

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
        if (iconeGeo_.isValid() && !iconeGeoBounds_.isEmpty())
            g.drawImage(iconeGeo_, iconeGeoBounds_.toFloat(), juce::RectanglePlacement::centred);
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

    // Correção realtime (Bug 1): commita qualquer texto digitado e ainda não
    // salvo antes que os editores sejam destruídos — chamado no início de
    // limpar() (todo ponto que reconstrói a ficha passa por aqui: trocar de
    // item, entrar/sair de lote, mudar tema/idioma) e também exposto via
    // FichaPanelComponent::salvarPendencias() pros pontos de saída que não
    // reconstroem a tela (fechar projeto, fechar o app, trocar de aba).
    void comitarPendencias() {
        for (auto& linha : camposUnificados_) {
            if (!linha || !linha->onCommit) continue;
            if (linha->ehNotes) {
                if (auto* notes = dynamic_cast<NotesEstruturadasComponent*>(linha->editor.get())) {
                    if (juce::String(notes->getTexto()) != linha->valorSeed) linha->onCommit();
                }
            } else if (auto* ed = dynamic_cast<juce::TextEditor*>(linha->editor.get())) {
                if (ed->getText() != linha->valorSeed) linha->onCommit();
            }
        }
    }

    void limpar() {
        comitarPendencias();
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
        colapsadoDublinCore_ = true;
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
                // Ajuste de layout METADATA: "Needs review" em texto preto.
                labelReviewFaltando_->setColour(juce::Label::textColourId, juce::Colours::black);
                labelReviewFaltando_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
                addAndMakeVisible(*labelReviewFaltando_);
            }
        }

        // Real-time (correção METADATA, item 5): SEM botão APPLY — cada
        // campo unificado já commita sozinho (onFocusLost/onReturnKey/
        // onChange de cada um, ver addEditableText/addEditableDropdown/
        // addEditableOriginalSourceMedium/addEditableTags/addEditableToggle
        // acima, e os 5 campos de GEO LOCATION em
        // construirSecaoGeolocalizacao). botaoAplicar_/labelAplicado_ nunca
        // são construídos — ficam null pra sempre; os "if (botaoAplicar_)"
        // de layout mais abaixo continuam de pé mas não desenham nada.
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

    // alturaDisponivel > 0: depois do layout normal (extraNotas = 0), se
    // sobrar espaço até a altura visível do viewport, refaz o layout uma
    // segunda vez esticando NOTES por esse tanto — item 1 da 3ª correção de
    // UI ("aumentar até o conjunto de cards ocupar a coluna inteira").
    // Duas passadas em vez de uma conta analítica: o layout em cascata tem
    // gente demais dependendo de y (GEO LOCATION, DUBLIN CORE, botões,
    // PRESERVATION) pra vale a pena recalcular tudo à mão.
    void relayout(int largura, int alturaDisponivel = 0) {
        relayoutInterno(largura, 0);
        if (alturaDisponivel > 0) {
            int deficit = alturaDisponivel - getHeight();
            if (deficit > 0) relayoutInterno(largura, deficit);
        }
    }

    void relayoutInterno(int largura, int extraNotas) {
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

            // PATH e AI GENERATED seguem a mesma coluna que CONTENT (item da
            // 5ª correção de UI: "PATH entre CONTENT e AI GENERATED" —
            // precisam ficar juntos, não cada um correndo pra coluna mais
            // curta na hora que aparece). Substituiu o antigo agrupamento
            // com ORIGINAL SOURCE MEDIUM (colOsm), que deixava PATH grudado
            // em DEVICE do outro lado e um vão vazio embaixo de AI GENERATED.
            int colContentGroup = 0;
            auto layoutCamposDoBloco = [&](BlocoFicha bloco, int& y0, int& y1, LinhaUnificada*& notasFora) {
                LinhaUnificada* cuPeople = nullptr;
                LinhaUnificada* cuTags = nullptr;

                for (auto& cu : camposUnificados_) {
                    if (!cu || cu->bloco != bloco) continue;

                    if (bloco == BlocoFicha::UserAsset) {
                        if (cu->ehNotes)  { notasFora = cu.get(); continue; }
                        if (cu->ehPeople) { cuPeople = cu.get(); continue; }
                        if (cu->ehTags)   { cuTags = cu.get();   continue; }
                        // item: SUBJECT sempre à esquerda, CREATOR sempre à
                        // direita, os dois sempre na mesma linha — o
                        // empacotamento "coluna mais curta primeiro" abaixo
                        // não garante isso sozinho. SUBJECT é pulado aqui
                        // (tratado junto quando o loop chega no CREATOR,
                        // que já vem logo depois dele na ordem de inserção).
                        if (cu->campoId == "subject") continue;
                        if (cu->campoId == "creator") {
                            LinhaUnificada* cuSubject = nullptr;
                            for (auto& outro : camposUnificados_) {
                                if (outro && outro->bloco == bloco && outro->campoId == "subject") {
                                    cuSubject = outro.get();
                                    break;
                                }
                            }
                            int yLinha = std::max(y0, y1);
                            y0 = yLinha;
                            y1 = yLinha;
                            int rotuloW = colW - 85;
                            int alturaLinha = 18 + tk.espacoPequeno;
                            if (cuSubject) {
                                cuSubject->rotulo->setBounds(x0, yLinha, rotuloW, 16);
                                cuSubject->badge->setBounds(x0 + colW - 80, yLinha, 80, 16);
                                cuSubject->editor->setBounds(x0, yLinha + 18, colW, 24);
                            }
                            cu->rotulo->setBounds(x1, yLinha, rotuloW, 16);
                            cu->badge->setBounds(x1 + colW - 80, yLinha, 80, 16);
                            cu->editor->setBounds(x1, yLinha + 18, colW, 24);
                            y0 += alturaLinha + 24;
                            y1 += alturaLinha + 24;
                            continue;
                        }
                    }

                    bool useCol1 = (y1 < y0);
                    if (bloco == BlocoFicha::UserAsset && (cu->campoId == "path" || cu->campoId == "ai_generated")) {
                        useCol1 = (colContentGroup == 1);
                    }
                    if (bloco == BlocoFicha::UserAsset && cu->campoId == "collection") {
                        colContentGroup = useCol1 ? 1 : 0;
                    }

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

                if (bloco == BlocoFicha::UserAsset && (cuPeople || cuTags)) {
                    // PEOPLE/TAGS seguem juntas, sempre na coluna direita —
                    // já não precisam mais disputar lado com NOTES.
                    int px = x1;
                    int& py = y1;

                    if (cuPeople) {
                        cuPeople->rotulo->setBounds(px, py, colW - 85, 16);
                        cuPeople->badge->setBounds(px + colW - 80, py, 80, 16);
                        py += 18;
                        cuPeople->editor->setBounds(px, py, colW, 26);
                        py += 26 + tk.espacoPequeno;
                    }

                    if (cuTags) {
                        cuTags->rotulo->setBounds(px, py, colW - 85, 16);
                        cuTags->badge->setBounds(px + colW - 80, py, 80, 16);
                        py += 18;
                        int chipH = 26;
                        if (auto* chips = dynamic_cast<TagChipsEditor*>(cuTags->editor.get())) {
                            chipH = chips->getPreferredHeight();
                        }
                        cuTags->editor->setBounds(px, py, colW, chipH);
                        py += chipH + tk.espacoPequeno;
                    }
                }
            };

            // --- 1. ASSET & USER METADATA (PEOPLE right above TAGS) ---
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
                LinhaUnificada* notasFora = nullptr;
                layoutCamposDoBloco(BlocoFicha::UserAsset, y0, y1, notasFora);
                int yPosColunas = std::max(y0, y1);
                int cardBBottom = yPosColunas + padCardY;

                // NOTES: última linha do card, ocupando as DUAS colunas
                // (item 1 da 3ª correção de UI) — estica com extraNotas até
                // o conjunto de cards preencher a coluna inteira da ficha.
                if (notasFora) {
                    int nx = x + padCardX;
                    notasFora->rotulo->setBounds(nx, yPosColunas, innerW - 85, 16);
                    notasFora->badge->setBounds(nx + innerW - 80, yPosColunas, 80, 16);
                    int ny = yPosColunas + 18;
                    int alturaNotas = kAlturaMinimaNotas + extraNotas;
                    notasFora->editor->setBounds(nx, ny, innerW, alturaNotas);
                    cardBBottom = ny + alturaNotas + padCardY;
                }

                quadroUserAsset_ = juce::Rectangle<int>(x, cardBTop, larguraUtil, cardBBottom - cardBTop);
                y = cardBBottom + tk.espacoMedio;
            } else {
                int cardBBottom = y;
                quadroUserAsset_ = juce::Rectangle<int>(x, cardBTop, larguraUtil, cardBBottom - cardBTop);
                y = cardBBottom + tk.espacoPequeno;
            }

            // --- 2. GEOLOCATION ---
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
                int tituloX = x + padCardX;
                if (iconeGeo_.isValid()) {
                    const int iconeW = 16;
                    iconeGeoBounds_ = juce::Rectangle<int>(tituloX, y + padCardY, iconeW, 20);
                    tituloX += iconeW + 4;
                } else {
                    iconeGeoBounds_ = {};
                }
                geolocalizacao_.titulo->setBounds(tituloX, y + padCardY, std::max(20, rightX - tituloX), 20);
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

                    // Favorite buttons row (side by side below all fields)
                    int favY = std::max(y0, y1) + tk.espacoPequeno;
                    int halfW = (innerW - tk.espacoPequeno) / 2;
                    if (geolocalizacao_.btnSalvarFavorito)
                        geolocalizacao_.btnSalvarFavorito->setBounds(x + padCardX, favY, halfW, 22);
                    if (geolocalizacao_.btnCarregarFavorito)
                        geolocalizacao_.btnCarregarFavorito->setBounds(x + padCardX + halfW + tk.espacoPequeno, favY, halfW, 22);
                    y0 = y1 = favY + 22 + tk.espacoPequeno;

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

            // --- 3. DUBLIN CORE METADATA ---
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
                LinhaUnificada* notasForaDc = nullptr; // Dublin Core não tem NOTES
                layoutCamposDoBloco(BlocoFicha::DublinCore, y0, y1, notasForaDc);
                int cardABottom = std::max(y0, y1) + padCardY;
                quadroDublinCore_ = juce::Rectangle<int>(x, cardATop, larguraUtil, cardABottom - cardATop);
                y = cardABottom + tk.espacoMedio;
            } else {
                int cardABottom = y;
                quadroDublinCore_ = juce::Rectangle<int>(x, cardATop, larguraUtil, cardABottom - cardATop);
                y = cardABottom + tk.espacoPequeno;
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
                    int alturaNotas1Col = kAlturaMinimaNotas + extraNotas;
                    cu->editor->setBounds(x + padCardX, y, innerW, alturaNotas1Col);
                    y += alturaNotas1Col + tk.espacoPequeno;
                } else {
                    cu->editor->setBounds(x + padCardX, y, innerW, 24);
                    y += 24 + tk.espacoPequeno;
                }
            }
        };

        // --- 1. ASSET & USER METADATA ---
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

        // --- 2. GEOLOCATION ---
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

                // Favorite buttons row
                {
                    int halfW = (innerW - tk.espacoPequeno) / 2;
                    if (geolocalizacao_.btnSalvarFavorito)
                        geolocalizacao_.btnSalvarFavorito->setBounds(x + padCardX, y, halfW, 22);
                    if (geolocalizacao_.btnCarregarFavorito)
                        geolocalizacao_.btnCarregarFavorito->setBounds(x + padCardX + halfW + tk.espacoPequeno, y, halfW, 22);
                    y += 22 + tk.espacoPequeno;
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

        // --- 3. DUBLIN CORE METADATA ---
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

        // Real-time (correção METADATA, item 9): sem isto, GEO LOCATION só
        // persistia quando o botão APPLY existia e era clicado — nenhum dos
        // 5 campos tinha commit próprio. Sai do foco (ou Enter) em
        // qualquer um deles já salva os 5 juntos (salvarGeolocalizacao lê
        // o texto atual de todos) e atualiza o item em memória.
        auto commitGeo = [this, itemId] {
            salvarGeolocalizacao(itemId);
            // item: autocomplete pros campos de GEO LOCATION, mesmo padrão
            // de CREATOR/SUBJECT/PUBLISHER — alimenta o histórico a cada
            // commit pra próximas digitações sugerirem valores já usados.
            auto& db = projeto_.projeto().registro();
            if (geolocalizacao_.editorAddress) {
                auto t = geolocalizacao_.editorAddress->getText().trim();
                if (t.isNotEmpty()) matriz::ficha::AutocompleteRepository::registrar(db, "geo_address", t.toStdString());
            }
            if (geolocalizacao_.editorCity) {
                auto t = geolocalizacao_.editorCity->getText().trim();
                if (t.isNotEmpty()) matriz::ficha::AutocompleteRepository::registrar(db, "geo_city", t.toStdString());
            }
            if (geolocalizacao_.editorState) {
                auto t = geolocalizacao_.editorState->getText().trim();
                if (t.isNotEmpty()) matriz::ficha::AutocompleteRepository::registrar(db, "geo_state", t.toStdString());
            }
            if (geolocalizacao_.editorCountry) {
                auto t = geolocalizacao_.editorCountry->getText().trim();
                if (t.isNotEmpty()) matriz::ficha::AutocompleteRepository::registrar(db, "geo_country", t.toStdString());
            }
            if (aoAplicarSucesso) aoAplicarSucesso(itemId);
        };

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
            // Ajuste de layout METADATA: geolocalização definida pelo
            // usuário em texto preto.
            geolocalizacao_.statusBadge->setColour(juce::Label::textColourId, juce::Colours::black);
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
        geolocalizacao_.editorCoords->onFocusLost = commitGeo;
        geolocalizacao_.editorCoords->onReturnKey = commitGeo;
        addAndMakeVisible(*geolocalizacao_.editorCoords);

        // Address
        geolocalizacao_.labelAddress = std::make_unique<juce::Label>();
        geolocalizacao_.labelAddress->setText(isPt ? juce::String::fromUTF8("Endereço Formatado") : juce::String("Formatted Address"), juce::dontSendNotification);
        geolocalizacao_.labelAddress->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelAddress->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelAddress);

        {
            ProjetoAberto* projPtr = &projeto_;
            geolocalizacao_.editorAddress = std::make_unique<AutoCompleteTextEditor>([projPtr] {
                std::vector<juce::String> valores;
                for (const auto& v : matriz::ficha::AutocompleteRepository::listar(projPtr->projeto().registro(), "geo_address"))
                    valores.push_back(juce::String(v));
                return valores;
            });
        }
        geolocalizacao_.editorAddress->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorAddress->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        geolocalizacao_.editorAddress->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        geolocalizacao_.editorAddress->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorAddress->setText(geoOpt && geoOpt->formattedAddress ? *geoOpt->formattedAddress : "");
        geolocalizacao_.editorAddress->setTextToShowWhenEmpty("e.g. Av. Paulista, 1000", juce::Colour(0xff888888));
        geolocalizacao_.editorAddress->onFocusLost = commitGeo;
        geolocalizacao_.editorAddress->onReturnKey = commitGeo;
        addAndMakeVisible(*geolocalizacao_.editorAddress);

        // City
        geolocalizacao_.labelCity = std::make_unique<juce::Label>();
        geolocalizacao_.labelCity->setText(isPt ? juce::String::fromUTF8("Cidade") : juce::String("City"), juce::dontSendNotification);
        geolocalizacao_.labelCity->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelCity->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelCity);

        {
            ProjetoAberto* projPtr = &projeto_;
            geolocalizacao_.editorCity = std::make_unique<AutoCompleteTextEditor>([projPtr] {
                std::vector<juce::String> valores;
                for (const auto& v : matriz::ficha::AutocompleteRepository::listar(projPtr->projeto().registro(), "geo_city"))
                    valores.push_back(juce::String(v));
                return valores;
            });
        }
        geolocalizacao_.editorCity->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorCity->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        geolocalizacao_.editorCity->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        geolocalizacao_.editorCity->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorCity->setText(geoOpt && geoOpt->city ? *geoOpt->city : "");
        geolocalizacao_.editorCity->setTextToShowWhenEmpty("e.g. Porto Seguro", juce::Colour(0xff888888));
        geolocalizacao_.editorCity->onFocusLost = commitGeo;
        geolocalizacao_.editorCity->onReturnKey = commitGeo;
        addAndMakeVisible(*geolocalizacao_.editorCity);

        // State
        geolocalizacao_.labelState = std::make_unique<juce::Label>();
        geolocalizacao_.labelState->setText(isPt ? juce::String::fromUTF8("Estado / Província") : juce::String("State / Province"), juce::dontSendNotification);
        geolocalizacao_.labelState->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelState->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelState);

        {
            ProjetoAberto* projPtr = &projeto_;
            geolocalizacao_.editorState = std::make_unique<AutoCompleteTextEditor>([projPtr] {
                std::vector<juce::String> valores;
                for (const auto& v : matriz::ficha::AutocompleteRepository::listar(projPtr->projeto().registro(), "geo_state"))
                    valores.push_back(juce::String(v));
                return valores;
            });
        }
        geolocalizacao_.editorState->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorState->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        geolocalizacao_.editorState->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        geolocalizacao_.editorState->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorState->setText(geoOpt && geoOpt->stateProvince ? *geoOpt->stateProvince : "");
        geolocalizacao_.editorState->setTextToShowWhenEmpty("e.g. Bahia", juce::Colour(0xff888888));
        geolocalizacao_.editorState->onFocusLost = commitGeo;
        geolocalizacao_.editorState->onReturnKey = commitGeo;
        addAndMakeVisible(*geolocalizacao_.editorState);

        // Country
        geolocalizacao_.labelCountry = std::make_unique<juce::Label>();
        geolocalizacao_.labelCountry->setText(isPt ? juce::String::fromUTF8("País") : juce::String("Country"), juce::dontSendNotification);
        geolocalizacao_.labelCountry->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelCountry->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelCountry);

        {
            ProjetoAberto* projPtr = &projeto_;
            geolocalizacao_.editorCountry = std::make_unique<AutoCompleteTextEditor>([projPtr] {
                std::vector<juce::String> valores;
                for (const auto& v : matriz::ficha::AutocompleteRepository::listar(projPtr->projeto().registro(), "geo_country"))
                    valores.push_back(juce::String(v));
                return valores;
            });
        }
        geolocalizacao_.editorCountry->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorCountry->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        geolocalizacao_.editorCountry->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        geolocalizacao_.editorCountry->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorCountry->setText(geoOpt && geoOpt->country ? *geoOpt->country : "");
        geolocalizacao_.editorCountry->setTextToShowWhenEmpty(isPt ? "Ex: Brasil" : "e.g. Brazil", juce::Colour(0xff888888));
        geolocalizacao_.editorCountry->onFocusLost = commitGeo;
        geolocalizacao_.editorCountry->onReturnKey = commitGeo;
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
            btnCollapseGeoLocation_->setButtonText(colapsadoGeoLocation_ ? juce::String::fromUTF8("\xe2\x96\xb6") : juce::String::fromUTF8("\xe2\x96\xbc"));
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
            if (geolocalizacao_.btnSalvarFavorito) geolocalizacao_.btnSalvarFavorito->setVisible(vis);
            if (geolocalizacao_.btnCarregarFavorito) geolocalizacao_.btnCarregarFavorito->setVisible(vis);
            if (aoRelayoutNecessario) aoRelayoutNecessario();
            repaint();
        };
        addAndMakeVisible(*btnCollapseGeoLocation_);

        // ★ Add to Favorites
        geolocalizacao_.btnSalvarFavorito = std::make_unique<juce::TextButton>(juce::String::fromUTF8(isPt ? "\xe2\x98\x85 Favorito" : "\xe2\x98\x85 Add to Favorites"));
        geolocalizacao_.btnSalvarFavorito->setTooltip(isPt ? "Salvar este lugar na lista de favoritos do projeto" : "Save this place to the project favorites list");
        geolocalizacao_.btnSalvarFavorito->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        geolocalizacao_.btnSalvarFavorito->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        geolocalizacao_.btnSalvarFavorito->onClick = [this, isPt] {
            // Build a label from filled fields
            juce::String sugestao;
            if (geolocalizacao_.editorCity && geolocalizacao_.editorCity->getText().isNotEmpty())
                sugestao = geolocalizacao_.editorCity->getText();
            else if (geolocalizacao_.editorAddress && geolocalizacao_.editorAddress->getText().isNotEmpty())
                sugestao = geolocalizacao_.editorAddress->getText().substring(0, 40);

            juce::AlertWindow dlg(isPt ? "Salvar Lugar Favorito" : "Save Favorite Place",
                                  isPt ? "Nome para este lugar:" : "Name for this place:",
                                  juce::MessageBoxIconType::NoIcon);
            dlg.addTextEditor("nome", sugestao.isEmpty() ? "" : sugestao, "");
            dlg.addButton(isPt ? "Salvar" : "Save", 1);
            dlg.addButton(isPt ? "Cancelar" : "Cancel", 0);

            if (dlg.runModalLoop() == 1) {
                juce::String nome = dlg.getTextEditorContents("nome").trim();
                if (nome.isEmpty()) return;

                matriz::analytics::GeoFavorito fav;
                fav.nome = nome.toStdString();
                if (geolocalizacao_.editorCoords && geolocalizacao_.editorCoords->getText().isNotEmpty()) {
                    std::string txt = geolocalizacao_.editorCoords->getText().toStdString();
                    auto comma = txt.find(',');
                    if (comma != std::string::npos) {
                        try {
                            fav.latitude  = std::stod(txt.substr(0, comma));
                            fav.longitude = std::stod(txt.substr(comma + 1));
                        } catch (...) {}
                    }
                }
                if (geolocalizacao_.editorAddress && geolocalizacao_.editorAddress->getText().isNotEmpty())
                    fav.formattedAddress = geolocalizacao_.editorAddress->getText().toStdString();
                if (geolocalizacao_.editorCity && geolocalizacao_.editorCity->getText().isNotEmpty())
                    fav.city = geolocalizacao_.editorCity->getText().toStdString();
                if (geolocalizacao_.editorState && geolocalizacao_.editorState->getText().isNotEmpty())
                    fav.stateProvince = geolocalizacao_.editorState->getText().toStdString();
                if (geolocalizacao_.editorCountry && geolocalizacao_.editorCountry->getText().isNotEmpty())
                    fav.country = geolocalizacao_.editorCountry->getText().toStdString();

                matriz::analytics::GeoFavoritosRepository::salvar(projeto_.projeto().registro(), fav);
            }
        };
        addAndMakeVisible(*geolocalizacao_.btnSalvarFavorito);

        // ▾ Load Favorite
        geolocalizacao_.btnCarregarFavorito = std::make_unique<juce::TextButton>(juce::String::fromUTF8(isPt ? "\xe2\x96\xbe Favoritos" : "\xe2\x96\xbe Favorites"));
        geolocalizacao_.btnCarregarFavorito->setTooltip(isPt ? "Carregar um lugar salvo nos favoritos" : "Load a saved place from favorites");
        geolocalizacao_.btnCarregarFavorito->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        geolocalizacao_.btnCarregarFavorito->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        geolocalizacao_.btnCarregarFavorito->onClick = [this, isPt, commitGeo] {
            auto favs = matriz::analytics::GeoFavoritosRepository::listar(projeto_.projeto().registro());
            if (favs.empty()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    isPt ? "Favoritos" : "Favorites",
                    juce::String::fromUTF8(isPt ? "Nenhum lugar favorito salvo ainda.\nUse o bot\xc3\xa3o \xe2\x98\x85 para salvar um lugar." :
                           "No favorite places saved yet.\nUse the \xe2\x98\x85 button to save a place."));
                return;
            }

            juce::PopupMenu menu;
            for (int i = 0; i < static_cast<int>(favs.size()); ++i) {
                juce::String label = juce::String(favs[static_cast<size_t>(i)].nome);
                if (favs[static_cast<size_t>(i)].city)
                    label += juce::String::fromUTF8(" \xe2\x80\x93 ") + juce::String(*favs[static_cast<size_t>(i)].city);
                menu.addItem(i + 1, label);
            }

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(geolocalizacao_.btnCarregarFavorito.get()),
                [this, favs, commitGeo](int result) {
                    if (result < 1 || result > static_cast<int>(favs.size())) return;
                    const auto& fav = favs[static_cast<size_t>(result - 1)];

                    if (geolocalizacao_.editorCoords && fav.latitude && fav.longitude) {
                        std::ostringstream ss;
                        ss << std::fixed << std::setprecision(6) << *fav.latitude << ", " << *fav.longitude;
                        geolocalizacao_.editorCoords->setText(ss.str());
                    }
                    if (geolocalizacao_.editorAddress && fav.formattedAddress)
                        geolocalizacao_.editorAddress->setText(juce::String(*fav.formattedAddress));
                    if (geolocalizacao_.editorCity && fav.city)
                        geolocalizacao_.editorCity->setText(juce::String(*fav.city));
                    if (geolocalizacao_.editorState && fav.stateProvince)
                        geolocalizacao_.editorState->setText(juce::String(*fav.stateProvince));
                    if (geolocalizacao_.editorCountry && fav.country)
                        geolocalizacao_.editorCountry->setText(juce::String(*fav.country));
                    // Real-time (item 9): escolher o favorito já aplica —
                    // sem isso ficava esperando um Apply que não existe mais.
                    commitGeo();
                });
        };
        addAndMakeVisible(*geolocalizacao_.btnCarregarFavorito);
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
        // Valor com que o editor foi semeado (ou o último valor salvo) —
        // permite ao flush (comitarPendencias) saber se há texto digitado
        // e nunca commitado, sem precisar de um onTextChange por campo.
        juce::String valorSeed;
    };
    std::vector<std::unique_ptr<LinhaUnificada>> camposUnificados_;
    std::unique_ptr<juce::Label> secHeaderDublinCore_;
    std::unique_ptr<juce::TextButton> btnAjudaDublinCore_;
    std::unique_ptr<juce::TextButton> btnCollapseDublinCore_;
    bool colapsadoDublinCore_ = true;

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

                // Decodificar imagem/Exiv2 na thread de UI trava a navegação
                // (trocar de foto vira um spinner) — cacheia o resultado no
                // JSON do arquivo pra nunca mais precisar reabrir o arquivo
                // só pra ler largura/altura.
                if (larguraVal > 0 && alturaVal > 0) {
                    juce::var dadosCache = dados.isObject() ? dados : juce::var(new juce::DynamicObject());
                    dadosCache.getDynamicObject()->setProperty("larguraPx", larguraVal);
                    dadosCache.getDynamicObject()->setProperty("alturaPx", alturaVal);
                    std::string novoJson = juce::JSON::toString(dadosCache, true).toStdString();
                    try {
                        projeto_.projeto().registro().run(
                            "UPDATE arquivo SET caracteristicas_tecnicas_json = ? WHERE id = ?",
                            {matriz::db::Value::of(novoJson), matriz::db::Value::of(arquivo->id)});
                        dados = dadosCache;
                    } catch (...) {}
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
        juce::String valNomeArquivo = valName;
        if (valNomeArquivo.isEmpty() && arquivo) {
            valNomeArquivo = juce::File(arquivo->caminhoAbsoluto).getFileName();
        }
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

        // "As 3 datas do sistema": só EVENT DATE (item.ano) aparece na ficha
        // ASSET & USER METADATA — é o único campo usado para indexação/
        // busca temporal (DATE CREATED/DATE ISSUED foram removidas desta
        // ficha por não deverem estar aqui; o Dublin Core acima fica
        // intocado).
        juce::String valEventDate = projeto_.lerMetadado(itemId, "ano").value_or("");
        // EVENT DATE nunca deve nascer vazio: se o item foi ingerido antes do
        // fallback de data de criação existir (ou se nada pôde ser lido lá),
        // herda o ano de DATE CREATED, que logo acima já foi resolvido em
        // cascata (dc_created → EXIF → data de criação em disco).
        //
        // O valor entra como TEXTO EDITÁVEL de verdade no campo (addEditableText
        // faz setText), não placeholder — dá pra apagar e sobrescrever. E é
        // gravado de forma silenciosa (ver preencherAnoPadraoSeVazio) pra o
        // mesmo ano valer na grade, nos filtros e na próxima sessão, sem
        // marcar o item como editado e sem entrar no Undo.
        if (valEventDate.isEmpty() && valDcCreated.length() >= 4) {
            juce::String anoDerivado = valDcCreated.substring(0, 4);
            if (anoDerivado.containsOnly("0123456789")) {
                valEventDate = anoDerivado;
                projeto_.preencherAnoPadraoSeVazio(itemId, anoDerivado.toStdString());
            }
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

        auto addEditableText = [this, &tk, itemId, isPt](const std::string& campoId, const juce::String& rotulo, const juce::String& valor, const std::string& dbColuna, bool comAutocomplete = false) {
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

            std::unique_ptr<juce::TextEditor> ed;
            if (comAutocomplete) {
                ProjetoAberto* projPtr = &projeto_;
                std::string campoAuto = dbColuna;
                ed = std::make_unique<AutoCompleteTextEditor>([projPtr, campoAuto] {
                    std::vector<juce::String> valores;
                    for (const auto& v : matriz::ficha::AutocompleteRepository::listar(projPtr->projeto().registro(), campoAuto))
                        valores.push_back(juce::String(v));
                    return valores;
                });
            } else {
                ed = std::make_unique<juce::TextEditor>();
            }
            ed->setText(valor, false);
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
            auto* edPtr = ed.get();
            linha->valorSeed = valor;
            auto* linhaRaw = linha.get();
            linha->onCommit = [this, itemId, dbColuna, edPtr, linhaRaw] {
                if (edPtr) {
                    std::string txt = edPtr->getText().toStdString();
                    bool ok = true;
                    try {
                        if (dbColuna == "dc_title") {
                            projeto_.salvarMetadado(itemId, "dc_title", txt);
                            projeto_.renomearItens({itemId}, txt);
                        } else if (dbColuna == "titulo") {
                            projeto_.renomearItens({itemId}, txt);
                            projeto_.salvarMetadado(itemId, "dc_title", txt);
                        } else {
                            projeto_.salvarMetadado(itemId, dbColuna, txt);
                        }
                    } catch (const std::exception& e) {
                        ok = false;
                        juce::Logger::writeToLog("[ficha] FALHA ao salvar campo='" + juce::String(dbColuna) +
                                                  "' item=" + juce::String(itemId) + " em " +
                                                  juce::Time::getCurrentTime().toISO8601(true) +
                                                  " erro=" + juce::String(e.what()));
                    }
                    if (ok) {
                        juce::Logger::writeToLog("[ficha] salvo campo='" + juce::String(dbColuna) +
                                                  "' item=" + juce::String(itemId) + " em " +
                                                  juce::Time::getCurrentTime().toISO8601(true));
                        linhaRaw->valorSeed = edPtr->getText();
                        piscarBordaSalvo(edPtr);
                        // Fase 4: SUBJECT/CREATOR/PUBLISHER/CONTRIBUTOR
                        // alimentam o autocomplete do projeto — "creator"/
                        // "subject" (bloco UserAsset) usam a mesma coluna
                        // dc_creator/dc_subject, então caem no mesmo histórico.
                        if (dbColuna == "dc_subject" || dbColuna == "dc_creator" ||
                            dbColuna == "dc_publisher" || dbColuna == "dc_contributor") {
                            matriz::ficha::AutocompleteRepository::registrar(projeto_.projeto().registro(), dbColuna, txt);
                        }
                    }
                    if (dbColuna == "titulo" || dbColuna == "dc_title") {
                        for (auto& other : camposUnificados_) {
                            if (!other) continue;
                            if (dbColuna == "titulo" && other->campoId == "dc_title") {
                                if (auto* edOth = dynamic_cast<juce::TextEditor*>(other->editor.get()))
                                    edOth->setText(juce::String::fromUTF8(txt.c_str()), false);
                            } else if (dbColuna == "dc_title" && other->campoId == "filename") {
                                if (auto* edOth = dynamic_cast<juce::TextEditor*>(other->editor.get()))
                                    edOth->setText(juce::String::fromUTF8(txt.c_str()), false);
                            }
                        }
                        if (cabecalho_) {
                            std::string tStd, tpMid, codAc;
                            if (projeto_.obterItemInfo(itemId, tStd, tpMid, codAc)) {
                                cabecalho_->setText(juce::String(codAc) + " - " + juce::String::fromUTF8(txt.c_str()), juce::dontSendNotification);
                            }
                        }
                    }
                    // "As 3 datas do sistema": DATE CREATED (data_criacao)
                    // não é mais sincronizada com o dc_created do Dublin
                    // Core — são campos independentes a partir de agora
                    // (item 10). Este bloco só sincroniza múltiplos widgets
                    // do PRÓPRIO dc_created (Dublin Core), se algum dia
                    // houver mais de um na tela.
                    if (dbColuna == "dc_created") {
                        for (auto& other : camposUnificados_) {
                            if (!other) continue;
                            if (other->campoId == "dc_created") {
                                if (auto* edOth = dynamic_cast<juce::TextEditor*>(other->editor.get())) {
                                    if (edOth != edPtr)
                                        edOth->setText(juce::String::fromUTF8(txt.c_str()), false);
                                }
                            }
                        }
                    }
                    // CREATOR (ASSET & USER) e CREATOR do Dublin Core (item 3):
                    // mesmo campo dc_creator por baixo, dois lugares na tela —
                    // preencher um copia pro outro na hora, sem precisar salvar
                    // e reabrir a ficha.
                    if (dbColuna == "dc_creator") {
                        for (auto& other : camposUnificados_) {
                            if (!other) continue;
                            if (other->campoId == "dc_creator" || other->campoId == "creator") {
                                if (auto* edOth = dynamic_cast<juce::TextEditor*>(other->editor.get())) {
                                    if (edOth != edPtr)
                                        edOth->setText(juce::String::fromUTF8(txt.c_str()), false);
                                }
                            }
                        }
                    }
                    // SUBJECT (ASSET & USER) e ASSUNTO/SUBJECT do Dublin Core:
                    // mesmo esquema do CREATOR acima — mesmo campo dc_subject
                    // por baixo, sincronizado ao vivo entre os dois lugares.
                    if (dbColuna == "dc_subject") {
                        for (auto& other : camposUnificados_) {
                            if (!other) continue;
                            if (other->campoId == "dc_subject" || other->campoId == "subject") {
                                if (auto* edOth = dynamic_cast<juce::TextEditor*>(other->editor.get())) {
                                    if (edOth != edPtr)
                                        edOth->setText(juce::String::fromUTF8(txt.c_str()), false);
                                }
                            }
                        }
                    }
                    // Migrado do antigo botão APPLY (removido — correção
                    // METADATA item 5): título novo também renomeia o
                    // caminho de catálogo, não só o registro no banco.
                    if (dbColuna == "titulo" || dbColuna == "filename") {
                        juce::String novoTitulo = juce::String::fromUTF8(txt.c_str()).trim();
                        if (novoTitulo.isNotEmpty()) {
                            auto pathOpt = projeto_.lerMetadado(itemId, "caminho_catalogo");
                            if (pathOpt && !pathOpt->empty()) {
                                juce::File fPath(juce::String::fromUTF8(pathOpt->c_str()));
                                if (fPath.getParentDirectory().exists() ||
                                    juce::String::fromUTF8(pathOpt->c_str()).containsChar('/') ||
                                    juce::String::fromUTF8(pathOpt->c_str()).containsChar('\\')) {
                                    juce::File novoPath = fPath.getParentDirectory().getChildFile(novoTitulo);
                                    try {
                                        projeto_.salvarMetadado(itemId, "caminho_catalogo", novoPath.getFullPathName().toStdString());
                                    } catch (...) {}
                                }
                            }
                        }
                    }
                    // Real-time (item 4/6): mantém o card do item em
                    // memória (título, etc.) sincronizado sem recarregar a
                    // lista inteira — aoMudar aqui só atualiza contadores.
                    if (aoAplicarSucesso) aoAplicarSucesso(itemId);
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
                    if (aoAplicarSucesso) aoAplicarSucesso(itemId);
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
        addEditableText("dc_creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), valDcCreator, "dc_creator", true);
        addEditableText("dc_subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), valDcSubject, "dc_subject", true);
        addEditableText("dc_description", isPt ? juce::String::fromUTF8("DESCRIÇÃO") : juce::String("DESCRIPTION"), valDcDescription, "dc_description");
        addEditableText("dc_publisher", isPt ? juce::String::fromUTF8("PUBLICADOR") : juce::String("PUBLISHER"), valDcPublisher, "dc_publisher", true);
        addEditableText("dc_contributor", isPt ? juce::String::fromUTF8("COLABORADOR") : juce::String("CONTRIBUTOR"), valDcContributor, "dc_contributor", true);
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

            // NOTES estruturado (item "NOTES — estrutura de metadados e
            // notas"): continua sendo um único texto em notas_livres por
            // baixo (ver Source/Model/NotasEstruturadas.h), mas a interface
            // organiza em seções colapsáveis — OTHER METADATA (automática,
            // somente leitura) e as seções que o usuário cria via +ADD NOTE.
            auto ed = std::make_unique<NotesEstruturadasComponent>(isPt);
            ed->setTexto(valor.toStdString());
            auto* edPtr = ed.get();
            linha->valorSeed = valor;
            auto* linhaRaw = linha.get();
            linha->onCommit = [this, itemId, edPtr, linhaRaw] {
                if (edPtr) {
                    try {
                        projeto_.salvarMetadado(itemId, "notas_livres", edPtr->getTexto());
                        linhaRaw->valorSeed = juce::String(edPtr->getTexto());
                    } catch (const std::exception& e) {
                        juce::Logger::writeToLog("[ficha] FALHA ao salvar campo='notas_livres' item=" +
                                                  juce::String(itemId) + " em " +
                                                  juce::Time::getCurrentTime().toISO8601(true) +
                                                  " erro=" + juce::String(e.what()));
                    }
                    if (aoAplicarSucesso) aoAplicarSucesso(itemId);
                    if (aoMudar) aoMudar();
                }
            };
            ed->onCommit = linha->onCommit;
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
            linha->rotulo->setText(isPt ? "PESSOAS / TAGS" : "PEOPLE / TAGS", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(isPt ? "[PESSOAS / TAGS]" : "[PEOPLE / TAGS]", juce::dontSendNotification);
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
            // Fase 4 (autocomplete por projeto): DEVICE é o único subcampo
            // do OSM com histórico — os demais são dropdowns fechados.
            osm->provedorHistoricoDevice = [this] {
                std::vector<juce::String> valores;
                for (const auto& v : matriz::ficha::AutocompleteRepository::listar(projeto_.projeto().registro(), "recording_device"))
                    valores.push_back(juce::String(v));
                return valores;
            };
            osm->setValueString(rawValue);
            osm->onChange = [this, itemId, rawOsm = osm.get()] {
                projeto_.salvarMetadado(itemId, "source_media", rawOsm->getValueString());
                matriz::ficha::AutocompleteRepository::registrar(projeto_.projeto().registro(), "recording_device",
                                                                   rawOsm->getValue().recordingDevice);
                if (aoAplicarSucesso) aoAplicarSucesso(itemId);
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
            addEditableText("filename", isPt ? juce::String::fromUTF8("NOME DO ARQUIVO") : juce::String("FILE NAME"), valNomeArquivo, "titulo");
            // "As 3 datas do sistema": FILE DATE virou EVENT DATE (data/ano
            // do EVENTO retratado pelo asset — único campo usado pra
            // indexação/busca temporal, ver item 9). Correção seguinte:
            // DATE CREATED e DATE ISSUED não deveriam ter sido expostas
            // nesta ficha — removidas daqui; EVENT DATE é o único campo de
            // data do card ASSET & USER METADATA.
            addEditableText("event_date", isPt ? juce::String::fromUTF8("DATA DO EVENTO") : juce::String("EVENT DATE"), valEventDate, "ano");
            addEditableText("creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), valDcCreator, "dc_creator", true);
            addEditableText("subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), valDcSubject, "dc_subject", true);
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
            addEditableText("path", isPt ? "CAMINHO" : "PATH", valPath, "caminho_catalogo");
            addEditableToggle("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", isAi, "ai_generated");
            addEditableNotes(valNotes);
            addEditablePeople();
            addEditableTags(tagsList);
        } else if (cat == MediaCategory::Video) {
            addEditableText("filename", isPt ? juce::String::fromUTF8("NOME DO ARQUIVO") : juce::String("FILE NAME"), valNomeArquivo, "titulo");
            // "As 3 datas do sistema": FILE DATE virou EVENT DATE (data/ano
            // do EVENTO retratado pelo asset — único campo usado pra
            // indexação/busca temporal, ver item 9). Correção seguinte:
            // DATE CREATED e DATE ISSUED não deveriam ter sido expostas
            // nesta ficha — removidas daqui; EVENT DATE é o único campo de
            // data do card ASSET & USER METADATA.
            addEditableText("event_date", isPt ? juce::String::fromUTF8("DATA DO EVENTO") : juce::String("EVENT DATE"), valEventDate, "ano");
            addEditableText("creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), valDcCreator, "dc_creator", true);
            addEditableText("subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), valDcSubject, "dc_subject", true);
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
            addEditableText("path", isPt ? "CAMINHO" : "PATH", valPath, "caminho_catalogo");
            addEditableToggle("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", isAi, "ai_generated");
            addEditableNotes(valNotes);
            addEditablePeople();
            addEditableTags(tagsList);
        } else if (cat == MediaCategory::Image) {
            addEditableText("filename", isPt ? juce::String::fromUTF8("NOME DO ARQUIVO") : juce::String("FILE NAME"), valNomeArquivo, "titulo");
            // "As 3 datas do sistema": FILE DATE virou EVENT DATE (data/ano
            // do EVENTO retratado pelo asset — único campo usado pra
            // indexação/busca temporal, ver item 9). Correção seguinte:
            // DATE CREATED e DATE ISSUED não deveriam ter sido expostas
            // nesta ficha — removidas daqui; EVENT DATE é o único campo de
            // data do card ASSET & USER METADATA.
            addEditableText("event_date", isPt ? juce::String::fromUTF8("DATA DO EVENTO") : juce::String("EVENT DATE"), valEventDate, "ano");
            addEditableText("creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), valDcCreator, "dc_creator", true);
            addEditableText("subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), valDcSubject, "dc_subject", true);
            addAutoFixed("dimensions", isPt ? juce::String::fromUTF8("DIMENSÕES") : juce::String("DIMENSIONS"), dimensionsStr);
            addAutoFixed("screen_orientation", isPt ? juce::String::fromUTF8("TELA / ORIENTAÇÃO") : juce::String("SCREEN / ORIENTATION"), orientationStr);
            addAutoFixed("format", isPt ? "FORMATO" : "FORMAT", ext);
            addAutoFixed("file_size", isPt ? "TAMANHO DO ARQUIVO" : "FILE SIZE", fileSizeStr);
            addAutoFixed("color_space", isPt ? juce::String::fromUTF8("ESPAÇO DE COR") : juce::String("COLOR SPACE"), colorSpaceStr);
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), traduzirContent(valCollection, isPt),
                                opcoesContentPorCategoria(MediaCategory::Image, isPt),
                                "collection_type");
            addEditableText("path", isPt ? "CAMINHO" : "PATH", valPath, "caminho_catalogo");
            addEditableToggle("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", isAi, "ai_generated");
            addEditableNotes(valNotes);
            addEditablePeople();
            addEditableTags(tagsList);
        } else { // Docs
            addEditableText("filename", isPt ? juce::String::fromUTF8("NOME DO ARQUIVO") : juce::String("FILE NAME"), valNomeArquivo, "titulo");
            // "As 3 datas do sistema": FILE DATE virou EVENT DATE (data/ano
            // do EVENTO retratado pelo asset — único campo usado pra
            // indexação/busca temporal, ver item 9). Correção seguinte:
            // DATE CREATED e DATE ISSUED não deveriam ter sido expostas
            // nesta ficha — removidas daqui; EVENT DATE é o único campo de
            // data do card ASSET & USER METADATA.
            addEditableText("event_date", isPt ? juce::String::fromUTF8("DATA DO EVENTO") : juce::String("EVENT DATE"), valEventDate, "ano");
            addEditableText("creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), valDcCreator, "dc_creator", true);
            addEditableText("subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), valDcSubject, "dc_subject", true);
            addAutoFixed("format", isPt ? "FORMATO" : "FORMAT", ext);
            addAutoFixed("file_size", isPt ? "TAMANHO DO ARQUIVO" : "FILE SIZE", fileSizeStr);
            addAutoFixed("pages", isPt ? juce::String::fromUTF8("PÁGINAS") : juce::String("PAGES"), pagesStr);
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), traduzirContent(valCollection, isPt),
                                opcoesContentPorCategoria(MediaCategory::Docs, isPt),
                                "collection_type");
            addEditableText("path", isPt ? "CAMINHO" : "PATH", valPath, "caminho_catalogo");
            addEditableToggle("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", isAi, "ai_generated");
            addEditableNotes(valNotes);
            addEditablePeople();
            addEditableTags(tagsList);
        }

        if (colapsadoDublinCore_) {
            for (auto& cu : camposUnificados_) {
                if (cu && cu->bloco == BlocoFicha::DublinCore) {
                    if (cu->rotulo) cu->rotulo->setVisible(false);
                    if (cu->badge) cu->badge->setVisible(false);
                    if (cu->editor) cu->editor->setVisible(false);
                }
            }
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
        std::unique_ptr<juce::TextButton>  btnSalvarFavorito;   // ★ Add to Favorites
        std::unique_ptr<juce::TextButton>  btnCarregarFavorito; // ▾ Load Favorite
    } geolocalizacao_;
    juce::Image iconeGeo_;
    juce::Rectangle<int> iconeGeoBounds_;

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

    void paint(juce::Graphics& g) override {
        if (itemIds_.empty()) return;
        const auto& tk = matriz::ui::tema();
        auto drawCard = [&](const juce::Rectangle<int>& r) {
            if (r.isEmpty()) return;
            g.setColour(tk.painel);
            g.fillRoundedRectangle(r.toFloat(), 6.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 1.0f);
        };
        drawCard(quadroUserAssetLote_);
        drawCard(quadroGeoLocationLote_);
        drawCard(quadroDublinCoreLote_);
    }

    void lookAndFeelChanged() override {
        const auto& tk = matriz::ui::tema();
        if (cabecalho_) {
            cabecalho_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
            cabecalho_->setColour(juce::Label::textColourId, tk.textoPrimario);
        }
        if (secHeaderDublinCoreLote_) {
            secHeaderDublinCoreLote_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            secHeaderDublinCoreLote_->setColour(juce::Label::textColourId, tk.textoPrimario);
        }
        if (secHeaderUserAssetLote_) {
            secHeaderUserAssetLote_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            secHeaderUserAssetLote_->setColour(juce::Label::textColourId, tk.textoPrimario);
        }
        for (auto* btn : {btnAjudaDublinCoreLote_.get(), btnAjudaUserAssetLote_.get()}) {
            if (btn) {
                btn->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
                btn->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
                btn->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
            }
        }
        for (auto* btn : {btnCollapseDublinCoreLote_.get(), btnCollapseUserAssetLote_.get()}) {
            if (btn) {
                btn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
                btn->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
                btn->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
            }
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

    // Mesma ideia de duas passadas do modo de item único — ver comentário
    // lá em cima de FichaConteudo::relayout.
    void relayout(int largura, int alturaDisponivel = 0) {
        relayoutInterno(largura, 0);
        if (alturaDisponivel > 0) {
            int deficit = alturaDisponivel - getHeight();
            if (deficit > 0) relayoutInterno(largura, deficit);
        }
    }

    void relayoutInterno(int largura, int extraNotas) {
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

        // Telas sem cartões de campo (tipo misto / escolher tipo): sem
        // cabeçalhos de card pra desenhar.
        if (linhas_.isEmpty() && !geoLote_.titulo) {
            quadroDublinCoreLote_ = {};
            quadroUserAssetLote_ = {};
            quadroGeoLocationLote_ = {};
            if (previa_) { previa_->setBounds(x, y, larguraUtil, 36); y += 36 + tk.espacoPequeno; }
            if (botaoAplicar_) {
                botaoAplicar_->setBounds(x, y, 120, 28);
                if (botaoDesfazer_) botaoDesfazer_->setBounds(x + 128, y, 120, 28);
                y += 28 + tk.espacoMedio;
            }
            if (resultado_) { resultado_->setBounds(x, y, larguraUtil, 20); y += 20 + tk.espacoMedio; }
            setSize(largura, y + tk.espacoPainel);
            return;
        }

        // Mesma diagramação em 3 cartões do modo de item único (item 1):
        // cartões com fundo/borda (ver paint()), na mesma ordem visual
        // (ASSET & USER, GEO LOCATION, DUBLIN CORE), separando os campos
        // "dc_*" (Dublin Core) do restante (User Asset).
        const int padCardX = 12;
        const int padCardY = 10;
        bool ehDuasColunas = (larguraUtil >= 500);

        std::vector<LinhaLote*> linhasDublinCore, linhasUserAsset;
        for (auto* linha : linhas_) {
            if (!linha) continue;
            if (ehCampoDublinCoreLote(*linha)) linhasDublinCore.push_back(linha);
            else linhasUserAsset.push_back(linha);
        }

        auto layoutLinhaEmColuna = [&tk, extraNotas](LinhaLote* linha, int currX, int& currY, int colW, int badgeW) {
            int rotuloW = colW - badgeW - 5;
            linha->rotulo->setBounds(currX, currY, rotuloW, 16);
            linha->badge->setBounds(currX + colW - badgeW, currY, badgeW, 16);
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
            } else if (linha->ehTags) {
                int chipH = 26;
                if (auto* chips = dynamic_cast<TagChipsEditor*>(linha->editor.get())) chipH = chips->getPreferredHeight();
                linha->editor->setBounds(currX, currY, colW, chipH);
                currY += chipH + tk.espacoPequeno;
            } else if (linha->ehNotes) {
                int alturaNotas = kAlturaMinimaNotas + extraNotas;
                linha->editor->setBounds(currX, currY, colW, alturaNotas);
                currY += alturaNotas + tk.espacoPequeno;
            } else {
                linha->editor->setBounds(currX, currY, colW, 24);
                currY += 24 + tk.espacoPequeno;
            }
        };

        if (ehDuasColunas) {
            int innerW = larguraUtil - 2 * padCardX;
            int gap = 16;
            int colW = (innerW - gap) / 2;
            int x0 = x + padCardX;
            int x1 = x + padCardX + colW + gap;

            auto layoutHeaderCard = [&](std::unique_ptr<juce::Label>& header, std::unique_ptr<juce::TextButton>& btnAjuda,
                                         std::unique_ptr<juce::TextButton>& btnCollapse) {
                int btnW = 20, btnH = 18;
                int rightX = x + padCardX + innerW;
                if (btnCollapse) {
                    rightX -= btnW;
                    btnCollapse->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                    rightX -= 4;
                }
                if (btnAjuda) {
                    rightX -= btnW;
                    btnAjuda->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                    rightX -= 8;
                }
                if (header) header->setBounds(x + padCardX, y + padCardY, std::max(20, rightX - (x + padCardX)), 20);
                y += padCardY + 24;
            };

            // --- 1. ASSET & USER METADATA ---
            int cardBTop = y;
            layoutHeaderCard(secHeaderUserAssetLote_, btnAjudaUserAssetLote_, btnCollapseUserAssetLote_);
            if (!colapsadoUserAssetLote_) {
                int y0 = y, y1 = y;
                // Mesma regra do modo de item único: NOTES sai da disputa de
                // coluna, vai por baixo das duas ocupando a largura inteira
                // do card (item 1 da 3ª correção de UI); PEOPLE+TAGS seguem
                // juntas, sempre na coluna direita.
                LinhaLote* linhaNotes = nullptr;
                LinhaLote* linhaPeople = nullptr;
                LinhaLote* linhaTags = nullptr;
                for (auto* linha : linhasUserAsset) {
                    if (!linha) continue;
                    if (linha->ehNotes)  { linhaNotes = linha;  continue; }
                    if (linha->ehPeople) { linhaPeople = linha; continue; }
                    if (linha->ehTags)   { linhaTags = linha;   continue; }
                    bool useCol1 = (y1 < y0);
                    layoutLinhaEmColuna(linha, useCol1 ? x1 : x0, useCol1 ? y1 : y0, colW, 80);
                }

                if (linhaPeople || linhaTags) {
                    int px = x1;
                    int& py = y1;
                    if (linhaPeople) layoutLinhaEmColuna(linhaPeople, px, py, colW, 80);
                    if (linhaTags) layoutLinhaEmColuna(linhaTags, px, py, colW, 80);
                }

                int yPosColunas = std::max(y0, y1);
                int cardBBottom = yPosColunas + padCardY;

                if (linhaNotes) {
                    int nx = x + padCardX;
                    linhaNotes->rotulo->setBounds(nx, yPosColunas, innerW - 85, 16);
                    linhaNotes->badge->setBounds(nx + innerW - 80, yPosColunas, 80, 16);
                    int ny = yPosColunas + 18;
                    int alturaNotas = kAlturaMinimaNotas + extraNotas;
                    linhaNotes->editor->setBounds(nx, ny, innerW, alturaNotas);
                    cardBBottom = ny + alturaNotas + padCardY;
                }

                quadroUserAssetLote_ = juce::Rectangle<int>(x, cardBTop, larguraUtil, cardBBottom - cardBTop);
                y = cardBBottom + tk.espacoMedio;
            } else {
                int cardBBottom = y;
                quadroUserAssetLote_ = juce::Rectangle<int>(x, cardBTop, larguraUtil, cardBBottom - cardBTop);
                y = cardBBottom + tk.espacoPequeno;
            }

            // --- 2. GEOLOCATION ---
            if (geoLote_.titulo) {
                int cardCTop = y;

                // Cabeçalho do card em largura inteira (título à esquerda,
                // badge encostado à direita), idêntico ao da ficha de item
                // único. Antes o título entrava na disputa de colunas com os
                // campos e empurrava todos eles uma coluna para o lado — era
                // isso que deixava COORDS/CITY/COUNTRY à direita no lote e à
                // esquerda na seleção única.
                int rightXg = x + padCardX + innerW;
                if (geoLote_.badge) {
                    rightXg -= 145;
                    geoLote_.badge->setBounds(rightXg, y + padCardY, 145, 20);
                    rightXg -= 8;
                }
                geoLote_.titulo->setBounds(x + padCardX, y + padCardY,
                                           std::max(20, rightXg - (x + padCardX)), 20);
                y += padCardY + 24;

                int y0 = y, y1 = y;

                auto layoutGeoSubfield = [&](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed) {
                    if (lbl && ed) {
                        bool uc1 = (y1 < y0);
                        int cx = uc1 ? x1 : x0;
                        int& cy = uc1 ? y1 : y0;
                        lbl->setBounds(cx, cy, colW, 16);
                        cy += 18;
                        ed->setBounds(cx, cy, colW, 24);
                        cy += 24 + tk.espacoPequeno;
                    }
                };
                layoutGeoSubfield(geoLote_.labelCoords, geoLote_.editorCoords);
                layoutGeoSubfield(geoLote_.labelAddress, geoLote_.editorAddress);
                layoutGeoSubfield(geoLote_.labelCity, geoLote_.editorCity);
                layoutGeoSubfield(geoLote_.labelState, geoLote_.editorState);
                layoutGeoSubfield(geoLote_.labelCountry, geoLote_.editorCountry);

                if (geoLote_.botaoSalvarFavoritos || geoLote_.botaoFavoritos) {
                    // Botões lado a lado, abaixo das duas colunas (mesmo
                    // arranjo do single-item — ver favY/halfW em ~944-950).
                    int favY = std::max(y0, y1) + tk.espacoPequeno;
                    int halfW = (innerW - tk.espacoPequeno) / 2;
                    if (geoLote_.botaoSalvarFavoritos)
                        geoLote_.botaoSalvarFavoritos->setBounds(x + padCardX, favY, halfW, 22);
                    if (geoLote_.botaoFavoritos)
                        geoLote_.botaoFavoritos->setBounds(x + padCardX + halfW + tk.espacoPequeno, favY, halfW, 22);
                    y0 = y1 = favY + 22 + tk.espacoPequeno;
                }

                int cardCBottom = std::max(y0, y1) + padCardY;
                quadroGeoLocationLote_ = juce::Rectangle<int>(x, cardCTop, larguraUtil, cardCBottom - cardCTop);
                y = cardCBottom + tk.espacoMedio;
            } else {
                quadroGeoLocationLote_ = {};
            }

            // --- 3. DUBLIN CORE METADATA ---
            int cardATop = y;
            layoutHeaderCard(secHeaderDublinCoreLote_, btnAjudaDublinCoreLote_, btnCollapseDublinCoreLote_);
            if (!colapsadoDublinCoreLote_) {
                int y0 = y, y1 = y;
                for (auto* linha : linhasDublinCore) {
                    bool useCol1 = (y1 < y0);
                    layoutLinhaEmColuna(linha, useCol1 ? x1 : x0, useCol1 ? y1 : y0, colW, 80);
                }
                int cardABottom = std::max(y0, y1) + padCardY;
                quadroDublinCoreLote_ = juce::Rectangle<int>(x, cardATop, larguraUtil, cardABottom - cardATop);
                y = cardABottom + tk.espacoMedio;
            } else {
                int cardABottom = y;
                quadroDublinCoreLote_ = juce::Rectangle<int>(x, cardATop, larguraUtil, cardABottom - cardATop);
                y = cardABottom + tk.espacoPequeno;
            }

            if (previa_) { previa_->setBounds(x, y, larguraUtil, 36); y += 36 + tk.espacoPequeno; }
            if (botaoAplicar_) {
                botaoAplicar_->setBounds(x, y, 120, 28);
                if (botaoDesfazer_) botaoDesfazer_->setBounds(x + 128, y, 120, 28);
                y += 28 + tk.espacoMedio;
            }
            if (resultado_) { resultado_->setBounds(x, y, larguraUtil, 20); y += 20 + tk.espacoMedio; }

            setSize(largura, y + tk.espacoPainel);
            return;
        }

        // --- Single column layout, same 3 cards stacked ---
        int innerW = larguraUtil - 2 * padCardX;

        auto layoutHeaderCard1Col = [&](std::unique_ptr<juce::Label>& header, std::unique_ptr<juce::TextButton>& btnAjuda,
                                         std::unique_ptr<juce::TextButton>& btnCollapse) {
            int btnW = 20, btnH = 18;
            int rightX = x + padCardX + innerW;
            if (btnCollapse) {
                rightX -= btnW;
                btnCollapse->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                rightX -= 4;
            }
            if (btnAjuda) {
                rightX -= btnW;
                btnAjuda->setBounds(rightX, y + padCardY + 1, btnW, btnH);
                rightX -= 8;
            }
            if (header) header->setBounds(x + padCardX, y + padCardY, std::max(20, rightX - (x + padCardX)), 20);
            y += padCardY + 24;
        };

        auto layoutLinha1Col = [&tk, x, padCardX, innerW, extraNotas](LinhaLote* linha, int& y) {
            int rotuloW = innerW - 100;
            linha->rotulo->setBounds(x + padCardX, y, rotuloW, 16);
            linha->badge->setBounds(x + padCardX + innerW - 95, y, 95, 16);
            y += 18;

            if (linha->ehOriginalSourceMedium) {
                if (auto* osm = dynamic_cast<OriginalSourceMediumEditorComponent*>(linha->editor.get())) {
                    int prefH = osm->getPreferredHeight();
                    linha->editor->setBounds(x + padCardX, y, innerW, prefH);
                    y += prefH + tk.espacoPequeno;
                } else {
                    linha->editor->setBounds(x + padCardX, y, innerW, 24);
                    y += 24 + tk.espacoPequeno;
                }
            } else if (linha->ehPeople) {
                linha->editor->setBounds(x + padCardX, y, innerW, 26);
                y += 26 + tk.espacoPequeno;
            } else if (linha->ehTags) {
                int chipH = 26;
                if (auto* chips = dynamic_cast<TagChipsEditor*>(linha->editor.get())) chipH = chips->getPreferredHeight();
                linha->editor->setBounds(x + padCardX, y, innerW, chipH);
                y += chipH + tk.espacoPequeno;
            } else if (linha->ehNotes) {
                int alturaNotas1Col = kAlturaMinimaNotas + extraNotas;
                linha->editor->setBounds(x + padCardX, y, innerW, alturaNotas1Col);
                y += alturaNotas1Col + tk.espacoPequeno;
            } else {
                linha->editor->setBounds(x + padCardX, y, innerW, 24);
                y += 24 + tk.espacoPequeno;
            }
        };

        // --- 1. ASSET & USER METADATA ---
        int cardBTop1 = y;
        layoutHeaderCard1Col(secHeaderUserAssetLote_, btnAjudaUserAssetLote_, btnCollapseUserAssetLote_);
        if (!colapsadoUserAssetLote_) {
            for (auto* linha : linhasUserAsset) layoutLinha1Col(linha, y);
            int cardBBottom1 = y + padCardY;
            quadroUserAssetLote_ = juce::Rectangle<int>(x, cardBTop1, larguraUtil, cardBBottom1 - cardBTop1);
            y = cardBBottom1 + tk.espacoMedio;
        } else {
            int cardBBottom1 = y;
            quadroUserAssetLote_ = juce::Rectangle<int>(x, cardBTop1, larguraUtil, cardBBottom1 - cardBTop1);
            y = cardBBottom1 + tk.espacoPequeno;
        }

        // --- 2. GEOLOCATION ---
        if (geoLote_.titulo) {
            int cardCTop1 = y;
            int rightXg = x + padCardX + innerW;
            if (geoLote_.badge) {
                rightXg -= 135;
                geoLote_.badge->setBounds(rightXg, y + padCardY, 135, 20);
                rightXg -= 8;
            }
            geoLote_.titulo->setBounds(x + padCardX, y + padCardY,
                                       std::max(20, rightXg - (x + padCardX)), 20);
            y += padCardY + 24;

            auto layoutGeoSubfield1 = [&](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed) {
                if (lbl) { lbl->setBounds(x + padCardX, y, innerW, 16); y += 18; }
                if (ed) { ed->setBounds(x + padCardX, y, innerW, 24); y += 24 + tk.espacoPequeno; }
            };
            layoutGeoSubfield1(geoLote_.labelCoords, geoLote_.editorCoords);
            layoutGeoSubfield1(geoLote_.labelAddress, geoLote_.editorAddress);
            layoutGeoSubfield1(geoLote_.labelCity, geoLote_.editorCity);
            layoutGeoSubfield1(geoLote_.labelState, geoLote_.editorState);
            layoutGeoSubfield1(geoLote_.labelCountry, geoLote_.editorCountry);
            if (geoLote_.botaoSalvarFavoritos || geoLote_.botaoFavoritos) {
                int halfW = (innerW - tk.espacoPequeno) / 2;
                if (geoLote_.botaoSalvarFavoritos)
                    geoLote_.botaoSalvarFavoritos->setBounds(x + padCardX, y, halfW, 22);
                if (geoLote_.botaoFavoritos)
                    geoLote_.botaoFavoritos->setBounds(x + padCardX + halfW + tk.espacoPequeno, y, halfW, 22);
                y += 22 + tk.espacoPequeno;
            }

            int cardCBottom1 = y + padCardY;
            quadroGeoLocationLote_ = juce::Rectangle<int>(x, cardCTop1, larguraUtil, cardCBottom1 - cardCTop1);
            y = cardCBottom1 + tk.espacoMedio;
        } else {
            quadroGeoLocationLote_ = {};
        }

        // --- 3. DUBLIN CORE METADATA ---
        int cardATop1 = y;
        layoutHeaderCard1Col(secHeaderDublinCoreLote_, btnAjudaDublinCoreLote_, btnCollapseDublinCoreLote_);
        if (!colapsadoDublinCoreLote_) {
            for (auto* linha : linhasDublinCore) layoutLinha1Col(linha, y);
            int cardABottom1 = y + padCardY;
            quadroDublinCoreLote_ = juce::Rectangle<int>(x, cardATop1, larguraUtil, cardABottom1 - cardATop1);
            y = cardABottom1 + tk.espacoMedio;
        } else {
            int cardABottom1 = y;
            quadroDublinCoreLote_ = juce::Rectangle<int>(x, cardATop1, larguraUtil, cardABottom1 - cardATop1);
            y = cardABottom1 + tk.espacoPequeno;
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
    // Real-time (correção METADATA): disparado por item, a cada campo
    // aplicado — mesma função que o single-item usa (aoAplicarSucesso) pra
    // manter o card do item em memória atualizado sem recarregar a lista
    // inteira nem mexer no filtro/seleção corrente.
    std::function<void(const std::string& itemId)> aoAplicarSucessoItem;

    // Correção realtime (Bug 1), modo lote: mesmo papel do
    // FichaConteudo::comitarPendencias() — commita texto digitado e ainda
    // não aplicado antes que os editores sejam destruídos.
    void comitarPendencias() {
        for (auto* linha : linhas_) {
            if (!linha) continue;
            if (linha->ehNotes) {
                if (auto* notes = dynamic_cast<NotesEstruturadasComponent*>(linha->editor.get())) {
                    if (notes->onCommit) notes->onCommit();
                }
            } else if (auto* ed = dynamic_cast<juce::TextEditor*>(linha->editor.get())) {
                if (ed->getText() != linha->valorSeed) aplicarCampoAgora(linha);
            }
        }
    }

    juce::Component* editorDoCampoParaTeste(const std::string& campoId) {
        if (campoId == "geo_city") return geoLote_.editorCity.get();
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
        // Último valor semeado/aplicado — usado pelo flush (comitarPendencias)
        // pra saber se há texto digitado e ainda não aplicado nesta linha.
        juce::String valorSeed;
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
        std::unique_ptr<juce::TextButton> botaoFavoritos;
        std::unique_ptr<juce::TextButton> botaoSalvarFavoritos;   // ★ Add to Favorites (Fase 2)
        bool tocado = false;
    };

    void limpar() {
        comitarPendencias();
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
        quadroDublinCoreLote_ = {};
        quadroUserAssetLote_ = {};
        quadroGeoLocationLote_ = {};
        secHeaderDublinCoreLote_.reset();
        btnAjudaDublinCoreLote_.reset();
        btnCollapseDublinCoreLote_.reset();
        colapsadoDublinCoreLote_ = true;
        secHeaderUserAssetLote_.reset();
        btnAjudaUserAssetLote_.reset();
        btnCollapseUserAssetLote_.reset();
        colapsadoUserAssetLote_ = false;
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
        bool isPtHeaders = (matriz::i18n::localeAtivo() == "pt_BR");

        // Cabeçalhos dos cartões DUBLIN CORE e ASSET & USER METADATA — mesmo
        // texto, ajuda e recolher/expandir do modo de item único (item 1).
        secHeaderUserAssetLote_ = std::make_unique<juce::Label>();
        secHeaderUserAssetLote_->setText(isPtHeaders ? juce::String::fromUTF8("METADADOS DO ATIVO E DO USUÁRIO") : juce::String("ASSET & USER METADATA"), juce::dontSendNotification);
        secHeaderUserAssetLote_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        secHeaderUserAssetLote_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*secHeaderUserAssetLote_);

        btnAjudaUserAssetLote_ = std::make_unique<juce::TextButton>("?");
        btnAjudaUserAssetLote_->setTooltip(
            isPtHeaders ? juce::String::fromUTF8("METADADOS DO ATIVO E DO USUÁRIO\n"
                                         "Atributos descritivos e técnicos específicos do item no acervo.\n"
                                         "Inclui mídia de origem (suporte original), classificação de conteúdo, pessoas envolvidas, anotações de curadoria e tags personalizadas.")
                 : juce::String("ASSET & USER METADATA\n"
                                "Specific descriptive and technical attributes for this collection item.\n"
                                "Includes original source medium, content classification, associated persons, curatorial notes, and custom tags."));
        btnAjudaUserAssetLote_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnAjudaUserAssetLote_->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
        btnAjudaUserAssetLote_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        addAndMakeVisible(*btnAjudaUserAssetLote_);

        btnCollapseUserAssetLote_ = std::make_unique<juce::TextButton>();
        btnCollapseUserAssetLote_->setButtonText(colapsadoUserAssetLote_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
        btnCollapseUserAssetLote_->setTooltip(colapsadoUserAssetLote_ ? (isPtHeaders ? "Expandir" : "Expand") : (isPtHeaders ? "Recolher" : "Collapse"));
        btnCollapseUserAssetLote_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btnCollapseUserAssetLote_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnCollapseUserAssetLote_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        btnCollapseUserAssetLote_->onClick = [this, isPtHeaders] {
            colapsadoUserAssetLote_ = !colapsadoUserAssetLote_;
            btnCollapseUserAssetLote_->setButtonText(colapsadoUserAssetLote_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
            btnCollapseUserAssetLote_->setTooltip(colapsadoUserAssetLote_ ? (isPtHeaders ? "Expandir" : "Expand") : (isPtHeaders ? "Recolher" : "Collapse"));
            bool vis = !colapsadoUserAssetLote_;
            for (auto* l : linhas_) {
                if (l && !ehCampoDublinCoreLote(*l)) {
                    if (l->rotulo) l->rotulo->setVisible(vis);
                    if (l->badge) l->badge->setVisible(vis);
                    if (l->editor) l->editor->setVisible(vis);
                }
            }
            if (aoRelayoutNecessario) aoRelayoutNecessario();
            repaint();
        };
        addAndMakeVisible(*btnCollapseUserAssetLote_);

        secHeaderDublinCoreLote_ = std::make_unique<juce::Label>();
        secHeaderDublinCoreLote_->setText(isPtHeaders ? "METADADOS DUBLIN CORE" : "DUBLIN CORE METADATA", juce::dontSendNotification);
        secHeaderDublinCoreLote_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        secHeaderDublinCoreLote_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*secHeaderDublinCoreLote_);

        btnAjudaDublinCoreLote_ = std::make_unique<juce::TextButton>("?");
        btnAjudaDublinCoreLote_->setTooltip(
            isPtHeaders ? juce::String::fromUTF8("DUBLIN CORE (ISO 15836)\n"
                                         "Padrão internacional aberto de metadados arquivísticos (15 elementos essenciais).\n"
                                         "Ideal para catalogação de patrimônio e acervos, bibliotecas digitais, intercâmbio entre instituições e integração direta com outros sistemas DAM.")
                 : juce::String("DUBLIN CORE (ISO 15836)\n"
                                "International open archival metadata standard (15 core elements).\n"
                                "Ideal for heritage cataloging, digital libraries, institutional exchange, and direct interoperability with other DAM systems."));
        btnAjudaDublinCoreLote_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnAjudaDublinCoreLote_->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
        btnAjudaDublinCoreLote_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        addAndMakeVisible(*btnAjudaDublinCoreLote_);

        btnCollapseDublinCoreLote_ = std::make_unique<juce::TextButton>();
        btnCollapseDublinCoreLote_->setButtonText(colapsadoDublinCoreLote_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
        btnCollapseDublinCoreLote_->setTooltip(colapsadoDublinCoreLote_ ? (isPtHeaders ? "Expandir" : "Expand") : (isPtHeaders ? "Recolher" : "Collapse"));
        btnCollapseDublinCoreLote_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btnCollapseDublinCoreLote_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnCollapseDublinCoreLote_->setColour(juce::TextButton::textColourOnId, tk.textoPrimario);
        btnCollapseDublinCoreLote_->onClick = [this, isPtHeaders] {
            colapsadoDublinCoreLote_ = !colapsadoDublinCoreLote_;
            btnCollapseDublinCoreLote_->setButtonText(colapsadoDublinCoreLote_ ? juce::String::fromUTF8("▶") : juce::String::fromUTF8("▼"));
            btnCollapseDublinCoreLote_->setTooltip(colapsadoDublinCoreLote_ ? (isPtHeaders ? "Expandir" : "Expand") : (isPtHeaders ? "Recolher" : "Collapse"));
            bool vis = !colapsadoDublinCoreLote_;
            for (auto* l : linhas_) {
                if (l && ehCampoDublinCoreLote(*l)) {
                    if (l->rotulo) l->rotulo->setVisible(vis);
                    if (l->badge) l->badge->setVisible(vis);
                    if (l->editor) l->editor->setVisible(vis);
                }
            }
            if (aoRelayoutNecessario) aoRelayoutNecessario();
            repaint();
        };
        addAndMakeVisible(*btnCollapseDublinCoreLote_);

        auto addEditableTextLote = [this, &tk](const std::string& campoId, const juce::String& rotulo, const std::string& dbColuna, bool ehNotes = false, bool comAutocomplete = false) {
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

            std::unique_ptr<juce::TextEditor> ed;
            if (comAutocomplete) {
                ProjetoAberto* projPtr = &projeto_;
                std::string campoAuto = dbColuna;
                ed = std::make_unique<AutoCompleteTextEditor>([projPtr, campoAuto] {
                    std::vector<juce::String> valores;
                    for (const auto& v : matriz::ficha::AutocompleteRepository::listar(projPtr->projeto().registro(), campoAuto))
                        valores.push_back(juce::String(v));
                    return valores;
                });
            } else {
                ed = std::make_unique<juce::TextEditor>();
            }
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
            linha->valorSeed = ed->getText();

            // Real-time (item 6/7 da correção METADATA): commit ao sair do
            // campo (ou Enter, exceto em NOTES — Enter ali é quebra de
            // linha), não mais "marca tocado e espera o botão Apply".
            auto commitTexto = [this, linha] { aplicarCampoAgora(linha); };
            ed->onFocusLost = commitTexto;
            if (!ehNotes) ed->onReturnKey = commitTexto;
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

            cb->onChange = [this, linha] { aplicarCampoAgora(linha); };
            addAndMakeVisible(*cb);
            linha->editor = std::move(cb);
        };

        auto addPeopleLote = [this, &tk]() {
            auto* linha = linhas_.add(new LinhaLote());
            linha->campoId = "people";
            linha->ehPeople = true;
            bool loteIsPt = (matriz::i18n::localeAtivo() == "pt_BR");

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(loteIsPt ? "PESSOAS / TAGS" : "PEOPLE / TAGS", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(loteIsPt ? "[PESSOAS / TAGS]" : "[PEOPLE / TAGS]", juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto picker = std::make_unique<PeoplePickerComponent>(projeto_);
            picker->onPersonAddedToTags = [this](const juce::String& nomePessoa) {
                // TAGS em lote agora é o mesmo TagChipsEditor da ficha
                // individual (item "TAGS — ficha em lote") — adicionar o
                // chip já dispara o aoMudar dele, que aplica a tag a todos
                // os itens selecionados; não duplicar a escrita aqui.
                for (auto* l : linhas_) {
                    if (l && l->ehTags) {
                        if (auto* chips = dynamic_cast<TagChipsEditor*>(l->editor.get())) {
                            chips->addTag(nomePessoa);
                        }
                        break;
                    }
                }
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

            // Item "TAGS — ficha de metadata em lote": mesmo componente e
            // comportamento (chips) da ficha individual (addEditableTags),
            // não um campo de texto livre à parte. Mostra apenas as tags
            // COMUNS a todos os itens selecionados (interseção, não união):
            // um chip aqui significa "todos os selecionados têm esta tag".
            // Adicionar/remover um chip aplica em tempo real a todos os
            // selecionados — como TagChipsEditor só avisa "mudou" (sem
            // dizer o quê), o commit compara contra o snapshot anterior pra
            // saber o que adicionar/remover em cada item. Quem já tem a tag
            // adicionada é ignorado (adicionarTag é idempotente).
            std::set<std::string> comuns;
            bool primeiroItem = true;
            for (const auto& id : itemIds_) {
                auto doItem = projeto_.lerTags(id);
                std::set<std::string> setItem(doItem.begin(), doItem.end());
                if (primeiroItem) {
                    comuns = std::move(setItem);
                    primeiroItem = false;
                } else {
                    std::set<std::string> interseccao;
                    std::set_intersection(comuns.begin(), comuns.end(),
                                          setItem.begin(), setItem.end(),
                                          std::inserter(interseccao, interseccao.end()));
                    comuns = std::move(interseccao);
                }
                if (comuns.empty()) break;
            }
            std::vector<std::string> tagsIniciais(comuns.begin(), comuns.end());

            auto chips = std::make_unique<TagChipsEditor>();
            chips->setTags(tagsIniciais);
            auto anterior = std::make_shared<std::vector<std::string>>(tagsIniciais);
            chips->aoMudar = [this, anterior, raw = chips.get()] {
                std::vector<std::string> novos = raw->getTags();
                std::set<std::string> setAntigo(anterior->begin(), anterior->end());
                std::set<std::string> setNovo(novos.begin(), novos.end());
                std::vector<std::string> adicionadas, removidas;
                for (const auto& t : setNovo) if (!setAntigo.count(t)) adicionadas.push_back(t);
                for (const auto& t : setAntigo) if (!setNovo.count(t)) removidas.push_back(t);
                *anterior = novos;
                if (adicionadas.empty() && removidas.empty()) return;

                projeto_.iniciarGrupoUndo("Batch edit: tags");
                for (const auto& id : itemIds_) {
                    for (const auto& t : adicionadas) projeto_.adicionarTag(id, t);
                    for (const auto& t : removidas) projeto_.removerTag(id, t);
                    if (aoAplicarSucessoItem) aoAplicarSucessoItem(id);
                }
                projeto_.finalizarGrupoUndo();
                if (aoAplicarEmLote) aoAplicarEmLote();
            };
            chips->aoRedimensionar = [this] { relayoutEExibir(); };
            addAndMakeVisible(*chips);
            linha->editor = std::move(chips);
        };

        // Item "Ficha ASSET & USER METADATA em seleção múltipla": NOTES em
        // lote passa a usar o mesmo NotesEstruturadasComponent (com +ADD
        // NOTE) da ficha individual, em vez de uma caixa de texto livre.
        // Começa vazio (não existe uma única "OTHER METADATA" comum a
        // vários arquivos diferentes) — cada seção criada/editada aqui é
        // aplicada por título a TODOS os itens selecionados: se o item já
        // tem uma seção com aquele título, o conteúdo é atualizado; senão, a
        // seção é adicionada, preservando o resto das notas de cada item
        // (incluindo a própria OTHER METADATA de cada um).
        auto addNotesLote = [this, &tk]() {
            bool notesIsPt = (matriz::i18n::localeAtivo() == "pt_BR");
            auto* linha = linhas_.add(new LinhaLote());
            linha->campoId = "notes";
            linha->ehNotes = true;
            linha->colunaDb = "notas_livres";

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(notesIsPt ? "NOTAS" : "NOTES", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText(notesIsPt ? juce::String::fromUTF8("[EDITÁVEL]") : juce::String("[EDITABLE]"), juce::dontSendNotification);
            linha->badge->setFont(juce::Font(juce::FontOptions(9.0f)));
            linha->badge->setColour(juce::Label::textColourId, tk.textoTerciario);
            linha->badge->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*linha->badge);

            auto notes = std::make_unique<NotesEstruturadasComponent>(notesIsPt);
            notes->setTexto("");
            auto ultimaComposicao = std::make_shared<std::vector<matriz::model::SecaoNota>>();
            notes->onCommit = [this, ultimaComposicao, raw = notes.get()] {
                auto atual = matriz::model::parseNotasEstruturadas(raw->getTexto());
                std::vector<matriz::model::SecaoNota> mudou;
                for (const auto& s : atual) {
                    bool igual = false;
                    for (const auto& prev : *ultimaComposicao) {
                        if (prev.titulo == s.titulo && prev.conteudo == s.conteudo) { igual = true; break; }
                    }
                    if (!igual) mudou.push_back(s);
                }
                *ultimaComposicao = atual;
                if (mudou.empty()) return;

                projeto_.iniciarGrupoUndo("Batch edit: notes");
                for (const auto& id : itemIds_) {
                    try {
                        auto secoesItem = matriz::model::parseNotasEstruturadas(projeto_.lerMetadado(id, "notas_livres").value_or(""));
                        for (const auto& nova : mudou) {
                            bool achou = false;
                            for (auto& existente : secoesItem) {
                                if (existente.titulo == nova.titulo) { existente.conteudo = nova.conteudo; achou = true; break; }
                            }
                            if (!achou) secoesItem.push_back(nova);
                        }
                        projeto_.salvarMetadado(id, "notas_livres", matriz::model::serializarNotasEstruturadas(secoesItem));
                        if (aoAplicarSucessoItem) aoAplicarSucessoItem(id);
                    } catch (...) {}
                }
                projeto_.finalizarGrupoUndo();
                if (aoAplicarEmLote) aoAplicarEmLote();
            };
            addAndMakeVisible(*notes);
            linha->editor = std::move(notes);
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
            osm->provedorHistoricoDevice = [this] {
                std::vector<juce::String> valores;
                for (const auto& v : matriz::ficha::AutocompleteRepository::listar(projeto_.projeto().registro(), "recording_device"))
                    valores.push_back(juce::String(v));
                return valores;
            };
            auto* rawOsm = osm.get();
            osm->onChange = [this, linha, rawOsm] {
                matriz::ficha::AutocompleteRepository::registrar(projeto_.projeto().registro(), "recording_device",
                                                                   rawOsm->getValue().recordingDevice);
                aplicarCampoAgora(linha);
                relayoutEExibir();
            };
            addAndMakeVisible(*osm);
            linha->editor = std::move(osm);
        };

        auto addGeolocationLote = [this, &tk, isPt]() {
            geoLote_.titulo = std::make_unique<juce::Label>();
            geoLote_.titulo->setText(isPt ? juce::String::fromUTF8("GEOLOCALIZAÇÃO") : juce::String("GEO LOCATION"), juce::dontSendNotification);
            // Mesma fonte do título do card na ficha de item único.
            geoLote_.titulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
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
                // Bold, como os rótulos de geo da ficha de item único.
                lbl->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
                lbl->setColour(juce::Label::textColourId, tk.textoSecundario);
                addAndMakeVisible(*lbl);

                ed = std::make_unique<juce::TextEditor>();
                ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
                ed->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
                ed->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
                ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
                ed->setTextToShowWhenEmpty(placeholder, tk.textoTerciario);
                auto commitGeo = [this] { aplicarGeoAgora(); };
                ed->onFocusLost = commitGeo;
                ed->onReturnKey = commitGeo;
                addAndMakeVisible(*ed);
            };

            makeGeoSubfield(geoLote_.labelCoords, geoLote_.editorCoords, isPt ? juce::String::fromUTF8("Coordenadas GPS (Lat, Long)") : juce::String("GPS Coordinates (Lat, Lng)"), "e.g. -16.4435, -39.0643");
            makeGeoSubfield(geoLote_.labelAddress, geoLote_.editorAddress, isPt ? juce::String::fromUTF8("Endereço Formatado") : juce::String("Formatted Address"), "e.g. Av. Paulista, 1000");
            makeGeoSubfield(geoLote_.labelCity, geoLote_.editorCity, isPt ? juce::String::fromUTF8("Cidade") : juce::String("City"), "e.g. Porto Seguro");
            makeGeoSubfield(geoLote_.labelState, geoLote_.editorState, isPt ? juce::String::fromUTF8("Estado / Província") : juce::String("State / Province"), "e.g. Bahia");
            makeGeoSubfield(geoLote_.labelCountry, geoLote_.editorCountry, isPt ? juce::String::fromUTF8("País") : juce::String("Country"), isPt ? juce::String::fromUTF8("Ex: Brasil") : juce::String("e.g. Brazil"));

            // ★ Add to Favorites (Fase 2): mesmo fluxo de diálogo do
            // single-item (btnSalvarFavorito), salvando os campos
            // preenchidos no card de geo lote como novo favorito do
            // projeto — via o mesmo GeoFavoritosRepository::salvar.
            geoLote_.botaoSalvarFavoritos = std::make_unique<juce::TextButton>(juce::String::fromUTF8(isPt ? "\xe2\x98\x85 Favorito" : "\xe2\x98\x85 Add to Favorites"));
            geoLote_.botaoSalvarFavoritos->setTooltip(isPt ? "Salvar este lugar na lista de favoritos do projeto" : "Save this place to the project favorites list");
            geoLote_.botaoSalvarFavoritos->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
            geoLote_.botaoSalvarFavoritos->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
            geoLote_.botaoSalvarFavoritos->onClick = [this, isPt] {
                juce::String sugestao;
                if (geoLote_.editorCity && geoLote_.editorCity->getText().isNotEmpty())
                    sugestao = geoLote_.editorCity->getText();
                else if (geoLote_.editorAddress && geoLote_.editorAddress->getText().isNotEmpty())
                    sugestao = geoLote_.editorAddress->getText().substring(0, 40);

                juce::AlertWindow dlg(isPt ? "Salvar Lugar Favorito" : "Save Favorite Place",
                                      isPt ? "Nome para este lugar:" : "Name for this place:",
                                      juce::MessageBoxIconType::NoIcon);
                dlg.addTextEditor("nome", sugestao.isEmpty() ? "" : sugestao, "");
                dlg.addButton(isPt ? "Salvar" : "Save", 1);
                dlg.addButton(isPt ? "Cancelar" : "Cancel", 0);

                if (dlg.runModalLoop() == 1) {
                    juce::String nome = dlg.getTextEditorContents("nome").trim();
                    if (nome.isEmpty()) return;

                    matriz::analytics::GeoFavorito fav;
                    fav.nome = nome.toStdString();
                    if (geoLote_.editorCoords && geoLote_.editorCoords->getText().isNotEmpty()) {
                        std::string txt = geoLote_.editorCoords->getText().toStdString();
                        auto comma = txt.find(',');
                        if (comma != std::string::npos) {
                            try {
                                fav.latitude  = std::stod(txt.substr(0, comma));
                                fav.longitude = std::stod(txt.substr(comma + 1));
                            } catch (...) {}
                        }
                    }
                    if (geoLote_.editorAddress && geoLote_.editorAddress->getText().isNotEmpty())
                        fav.formattedAddress = geoLote_.editorAddress->getText().toStdString();
                    if (geoLote_.editorCity && geoLote_.editorCity->getText().isNotEmpty())
                        fav.city = geoLote_.editorCity->getText().toStdString();
                    if (geoLote_.editorState && geoLote_.editorState->getText().isNotEmpty())
                        fav.stateProvince = geoLote_.editorState->getText().toStdString();
                    if (geoLote_.editorCountry && geoLote_.editorCountry->getText().isNotEmpty())
                        fav.country = geoLote_.editorCountry->getText().toStdString();

                    matriz::analytics::GeoFavoritosRepository::salvar(projeto_.projeto().registro(), fav);
                }
            };
            addAndMakeVisible(*geoLote_.botaoSalvarFavoritos);

            // Favoritos (item 1 da correção METADATA): mesmo
            // GeoFavoritosRepository que a ficha de item único usa — sem
            // lista paralela. Escolher um favorito preenche os campos E já
            // aplica em tempo real a todos os itens selecionados.
            geoLote_.botaoFavoritos = std::make_unique<juce::TextButton>(juce::String::fromUTF8(isPt ? "\xe2\x96\xbe Favoritos" : "\xe2\x96\xbe Favorites"));
            geoLote_.botaoFavoritos->setTooltip(isPt ? "Carregar um lugar salvo nos favoritos e aplicar a todos os selecionados"
                                                      : "Load a saved place from favorites and apply to all selected");
            geoLote_.botaoFavoritos->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
            geoLote_.botaoFavoritos->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
            geoLote_.botaoFavoritos->onClick = [this, isPt] {
                auto favs = matriz::analytics::GeoFavoritosRepository::listar(projeto_.projeto().registro());
                if (favs.empty()) {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        isPt ? "Favoritos" : "Favorites",
                        juce::String::fromUTF8(isPt ? "Nenhum lugar favorito salvo ainda." : "No favorite places saved yet."));
                    return;
                }
                juce::PopupMenu menu;
                for (int i = 0; i < static_cast<int>(favs.size()); ++i) {
                    juce::String label = juce::String(favs[static_cast<size_t>(i)].nome);
                    if (favs[static_cast<size_t>(i)].city)
                        label += juce::String::fromUTF8(" \xe2\x80\x93 ") + juce::String(*favs[static_cast<size_t>(i)].city);
                    menu.addItem(i + 1, label);
                }
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(geoLote_.botaoFavoritos.get()),
                    [this, favs](int result) {
                        if (result < 1 || result > static_cast<int>(favs.size())) return;
                        const auto& fav = favs[static_cast<size_t>(result - 1)];
                        if (geoLote_.editorCoords && fav.latitude && fav.longitude) {
                            std::ostringstream ss;
                            ss << std::fixed << std::setprecision(6) << *fav.latitude << ", " << *fav.longitude;
                            geoLote_.editorCoords->setText(ss.str());
                        }
                        if (geoLote_.editorAddress && fav.formattedAddress) geoLote_.editorAddress->setText(juce::String(*fav.formattedAddress));
                        if (geoLote_.editorCity && fav.city) geoLote_.editorCity->setText(juce::String(*fav.city));
                        if (geoLote_.editorState && fav.stateProvince) geoLote_.editorState->setText(juce::String(*fav.stateProvince));
                        if (geoLote_.editorCountry && fav.country) geoLote_.editorCountry->setText(juce::String(*fav.country));
                        aplicarGeoAgora();
                    });
            };
            addAndMakeVisible(*geoLote_.botaoFavoritos);
        };

        auto addDublinCoreLote = [&addEditableTextLote, &addDropdownLote, isPt]() {
            addEditableTextLote("dc_creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), "dc_creator", false, true);
            addEditableTextLote("dc_subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), "dc_subject", false, true);
            addEditableTextLote("dc_description", isPt ? juce::String::fromUTF8("DESCRIÇÃO") : juce::String("DESCRIPTION"), "dc_description");
            addEditableTextLote("dc_publisher", isPt ? juce::String::fromUTF8("PUBLICADOR") : juce::String("PUBLISHER"), "dc_publisher", false, true);
            addEditableTextLote("dc_contributor", isPt ? juce::String::fromUTF8("COLABORADOR") : juce::String("CONTRIBUTOR"), "dc_contributor", false, true);
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
                // Item "Ficha ASSET & USER METADATA em seleção múltipla":
                // CREATOR/SUBJECT também aparecem aqui (bloco UserAsset),
                // não só no card DUBLIN CORE — mesmo campo dc_creator/
                // dc_subject por baixo (campoId sem prefixo "dc_" já cai no
                // bloco certo via ehCampoDublinCoreLote). CREATOR e EVENT
                // DATE trocaram de posição entre si (ajuste de layout da
                // ficha de seleção múltipla).
                addEditableTextLote("creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), "dc_creator", false, true);
                // "As 3 datas do sistema" item 8: YEAR virou EVENT DATE
                // (mesmo campo nativo "ano" por baixo — só o rótulo muda).
                addEditableTextLote("year", isPt ? juce::String::fromUTF8("DATA DO EVENTO") : juce::String("EVENT DATE"), "ano");
                addEditableTextLote("subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), "dc_subject", false, true);
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Audio, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addEditableTextLote("isrc", "ISRC", "isrc");
                addNotesLote();
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
            case MediaCategory::Video: {
                addDublinCoreLote();
                // Item "Ficha ASSET & USER METADATA em seleção múltipla":
                // CREATOR/SUBJECT também aparecem aqui (bloco UserAsset),
                // não só no card DUBLIN CORE — mesmo campo dc_creator/
                // dc_subject por baixo (campoId sem prefixo "dc_" já cai no
                // bloco certo via ehCampoDublinCoreLote). CREATOR e EVENT
                // DATE trocaram de posição entre si (ajuste de layout da
                // ficha de seleção múltipla).
                addEditableTextLote("creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), "dc_creator", false, true);
                // "As 3 datas do sistema" item 8: YEAR virou EVENT DATE
                // (mesmo campo nativo "ano" por baixo — só o rótulo muda).
                addEditableTextLote("year", isPt ? juce::String::fromUTF8("DATA DO EVENTO") : juce::String("EVENT DATE"), "ano");
                addEditableTextLote("subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), "dc_subject", false, true);
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Video, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addNotesLote();
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
            case MediaCategory::Image: {
                addDublinCoreLote();
                // Item "Ficha ASSET & USER METADATA em seleção múltipla":
                // CREATOR/SUBJECT também aparecem aqui (bloco UserAsset),
                // não só no card DUBLIN CORE — mesmo campo dc_creator/
                // dc_subject por baixo (campoId sem prefixo "dc_" já cai no
                // bloco certo via ehCampoDublinCoreLote). CREATOR e EVENT
                // DATE trocaram de posição entre si (ajuste de layout da
                // ficha de seleção múltipla).
                addEditableTextLote("creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), "dc_creator", false, true);
                // "As 3 datas do sistema" item 8: YEAR virou EVENT DATE
                // (mesmo campo nativo "ano" por baixo — só o rótulo muda).
                addEditableTextLote("year", isPt ? juce::String::fromUTF8("DATA DO EVENTO") : juce::String("EVENT DATE"), "ano");
                addEditableTextLote("subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), "dc_subject", false, true);
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Image, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addNotesLote();
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
            case MediaCategory::Docs: {
                addDublinCoreLote();
                // Item "Ficha ASSET & USER METADATA em seleção múltipla":
                // CREATOR/SUBJECT também aparecem aqui (bloco UserAsset),
                // não só no card DUBLIN CORE — mesmo campo dc_creator/
                // dc_subject por baixo (campoId sem prefixo "dc_" já cai no
                // bloco certo via ehCampoDublinCoreLote). CREATOR e EVENT
                // DATE trocaram de posição entre si (ajuste de layout da
                // ficha de seleção múltipla).
                addEditableTextLote("creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), "dc_creator", false, true);
                // "As 3 datas do sistema" item 8: YEAR virou EVENT DATE
                // (mesmo campo nativo "ano" por baixo — só o rótulo muda).
                addEditableTextLote("year", isPt ? juce::String::fromUTF8("DATA DO EVENTO") : juce::String("EVENT DATE"), "ano");
                addEditableTextLote("subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), "dc_subject", false, true);
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Docs, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addNotesLote();
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
            case MediaCategory::Mixed:
            default: {
                addDublinCoreLote();
                // Item "Ficha ASSET & USER METADATA em seleção múltipla":
                // CREATOR/SUBJECT também aparecem aqui (bloco UserAsset),
                // não só no card DUBLIN CORE — mesmo campo dc_creator/
                // dc_subject por baixo (campoId sem prefixo "dc_" já cai no
                // bloco certo via ehCampoDublinCoreLote). CREATOR e EVENT
                // DATE trocaram de posição entre si (ajuste de layout da
                // ficha de seleção múltipla).
                addEditableTextLote("creator", isPt ? juce::String::fromUTF8("CRIADOR") : juce::String("CREATOR"), "dc_creator", false, true);
                // "As 3 datas do sistema" item 8: YEAR virou EVENT DATE
                // (mesmo campo nativo "ano" por baixo — só o rótulo muda).
                addEditableTextLote("year", isPt ? juce::String::fromUTF8("DATA DO EVENTO") : juce::String("EVENT DATE"), "ano");
                addEditableTextLote("subject", isPt ? juce::String::fromUTF8("ASSUNTO") : juce::String("SUBJECT"), "dc_subject", false, true);
                addOriginalSourceMediumLote();
                addDropdownLote("collection", isPt ? juce::String::fromUTF8("CONTEÚDO") : juce::String("CONTENT"), "collection_type",
                                opcoesContentPorCategoriaString(MediaCategory::Mixed, isPt));
                addDropdownLote("ai_generated", isPt ? "GERADO POR IA" : "AI GENERATED", "ai_generated",
                                isPt ? std::vector<std::string>{"SIM (Gerado por IA)", "NÃO"} : std::vector<std::string>{"YES (AI Generated)", "NO"});
                addNotesLote();
                addPeopleLote();
                addTagsLote();
                addGeolocationLote();
                break;
            }
        }

        // Real-time (correção METADATA, item 5): SEM botão APPLY — cada
        // campo já aplica sozinho no commit (ver aplicarCampoAgora/
        // aplicarGeoAgora, chamadas pelos onChange/onFocusLost de cada
        // campo acima). previa_/botaoAplicar_/botaoDesfazer_ nunca são
        // construídos — ficam null pra sempre, os "if (botaoAplicar_)" de
        // layout mais abaixo continuam de pé mas não desenham nada.
        // resultado_ vira o feedback do ÚLTIMO campo aplicado.
        resultado_ = std::make_unique<juce::Label>();
        resultado_->setColour(juce::Label::textColourId, tk.textoSecundario);
        resultado_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        addAndMakeVisible(*resultado_);
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

    // Real-time (correção METADATA, itens 5-9): cada campo aplica sozinho,
    // assim que muda — sem botão APPLY, sem etapa de confirmação, sem
    // "tocado" esperando um clique. aplicarCampoAgora() é a MESMA lógica
    // por tipo de campo que existia dentro do antigo aplicar(), só que
    // chamada uma linha por vez, na hora do commit daquele campo.
    void aplicarCampoAgora(LinhaLote* linha) {
        if (!linha || itemIds_.empty()) return;
        juce::String val = lerValorLinha(*linha);

        // Bug 5 (correção METADATA): quando os itens selecionados têm
        // valores diferentes pra este campo, o editor fica com texto real
        // vazio (só mostra um placeholder cinza "valores múltiplos") —
        // Enter ou perda de foco SEM o usuário ter digitado nada não pode
        // gravar esse vazio em cima do valor de todo mundo (ex.: apagava
        // o SUBJECT de todos os itens selecionados). Mesmo critério que
        // comitarPendencias() já usa pra decidir se há algo pendente.
        if (!linha->ehTags && !linha->ehNotes && val == linha->valorSeed) return;

        projeto_.iniciarGrupoUndo("Batch edit: " + linha->campoId);
        int sucessos = 0, falhas = 0;
        // Fase 2b (freeze de edição em lote): uma transação só pros N itens
        // em vez de uma implícita por INSERT/UPDATE (salvarMetadado/
        // adicionarTag/removerTag chamados por item, dentro do loop
        // abaixo). Continua na message thread -- os efeitos por campo
        // (tags, notas concatenadas, toggles de AI GENERATED) têm lógica
        // condicional demais por item pra mover com segurança sem uma
        // revisão própria; isto já elimina o custo de N transações
        // separadas, que era o grosso do problema.
        auto& dbLote = projeto_.projeto().registro();
        std::unique_lock<std::recursive_mutex> writeLock(projeto_.writeMutex());
        bool emTransacao = false;
        try { dbLote.exec("BEGIN IMMEDIATE"); emTransacao = true; } catch (...) {}
        for (const auto& id : itemIds_) {
            try {
                if (linha->ehTags) {
                    juce::StringArray tagsNovas;
                    tagsNovas.addTokens(val, " ,;", "\"");
                    for (int t = 0; t < tagsNovas.size(); ++t) {
                        juce::String tg = tagsNovas[t].trimCharactersAtStart("#").trim();
                        if (tg.isNotEmpty()) projeto_.adicionarTag(id, tg.toStdString());
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
                } else if (!linha->colunaDb.empty()) {
                    projeto_.salvarMetadado(id, linha->colunaDb, val.toStdString());
                }
                ++sucessos;
                if (aoAplicarSucessoItem) aoAplicarSucessoItem(id);
            } catch (const std::exception& e) {
                ++falhas;
                juce::Logger::writeToLog("[ficha-lote] FALHA ao salvar campo='" + juce::String(linha->campoId) +
                                          "' item=" + juce::String(id) + " em " +
                                          juce::Time::getCurrentTime().toISO8601(true) +
                                          " erro=" + juce::String(e.what()));
            }
        }
        if (emTransacao && !commitOuReverter(dbLote, itemIds_)) {
            sucessos = 0;
            falhas = static_cast<int>(itemIds_.size());
        }
        projeto_.finalizarGrupoUndo();
        juce::Logger::writeToLog("[ficha-lote] campo='" + juce::String(linha->campoId) + "' aplicado em " +
                                  juce::String(sucessos) + " item(ns), " + juce::String(falhas) +
                                  " falha(s), em " + juce::Time::getCurrentTime().toISO8601(true));
        linha->valorSeed = val;

        // Fase 4: mesmos 4 campos do modo single alimentam o autocomplete
        // do projeto (uma vez por aplicação, não por item — é o mesmo
        // valor pros itens selecionados).
        if (sucessos > 0 && (linha->colunaDb == "dc_subject" || linha->colunaDb == "dc_creator" ||
                              linha->colunaDb == "dc_publisher" || linha->colunaDb == "dc_contributor")) {
            matriz::ficha::AutocompleteRepository::registrar(projeto_.projeto().registro(), linha->colunaDb, val.toStdString());
        }

        // CREATOR (ASSET & USER) e CREATOR do Dublin Core, idem SUBJECT:
        // mesmo dc_creator/dc_subject por baixo, dois lugares na tela — como
        // no modo item único, preencher um copia pro outro na hora.
        if (linha->colunaDb == "dc_creator" || linha->colunaDb == "dc_subject") {
            for (auto* other : linhas_) {
                if (!other || other == linha || other->colunaDb != linha->colunaDb) continue;
                if (auto* edOth = dynamic_cast<juce::TextEditor*>(other->editor.get())) {
                    edOth->setText(val, false);
                    other->valorSeed = val;
                }
            }
        }

        juce::String texto = matriz::i18n::t("ficha.lote_resultado").replace("{sucessos}", juce::String(sucessos));
        if (falhas > 0) texto += matriz::i18n::t("ficha.lote_resultado_falhas").replace("{falhas}", juce::String(falhas));
        if (resultado_) resultado_->setText(texto, juce::dontSendNotification);

        if (sucessos > 0) piscarBordaSalvo(dynamic_cast<juce::TextEditor*>(linha->editor.get()));
        if (aoAplicarEmLote) aoAplicarEmLote();
    }

    void aplicarGeoAgora() {
        if (itemIds_.empty()) return;
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
        if (!geoTemplate.hasAnyLocationData()) return;

        // Item "Progress bar para operações de metadata" (exemplo
        // obrigatório: GEO LOCATION em lote) — mesma ProgressoGlobal já
        // usada por desfazer() nesta classe, progresso real por item
        // concluído, não uma animação solta.
        bool ehLote = itemIds_.size() > 1;
        const juce::String kIdTarefa = "batch_geo_location";
        if (ehLote) {
            ProgressoGlobal::obterInstancia().iniciarTarefa(
                kIdTarefa, "Applying GEO Location", (int) itemIds_.size(), nullptr, "Starting...");
        }

        projeto_.iniciarGrupoUndo("Batch edit: geo location");
        int concluidos = 0;
        // Fase 2b: uma transação só pros N itens em vez de uma por INSERT
        // (AssetGeolocationRepository::salvar chamado por item).
        auto& dbGeoLote = projeto_.projeto().registro();
        std::unique_lock<std::recursive_mutex> writeLock(projeto_.writeMutex());
        bool emTransacaoGeo = false;
        try { dbGeoLote.exec("BEGIN IMMEDIATE"); emTransacaoGeo = true; } catch (...) {}
        for (const auto& id : itemIds_) {
            geoTemplate.assetId = id;
            try {
                matriz::analytics::AssetGeolocationRepository::salvar(dbGeoLote, geoTemplate);
                if (aoAplicarSucessoItem) aoAplicarSucessoItem(id);
            } catch (...) {}
            ++concluidos;
            if (ehLote) {
                ProgressoGlobal::obterInstancia().atualizarProgresso(
                    kIdTarefa, concluidos, juce::String(concluidos) + " / " + juce::String((int) itemIds_.size()));
            }
        }
        if (emTransacaoGeo) commitOuReverter(dbGeoLote, itemIds_);
        projeto_.finalizarGrupoUndo();
        if (ehLote) {
            ProgressoGlobal::obterInstancia().concluirTarefa(
                kIdTarefa, juce::String(concluidos) + " item(s) updated");
        }
        if (aoAplicarEmLote) aoAplicarEmLote();
    }

    void desfazer() {
        ProgressoGlobal::obterInstancia().iniciarTarefa("batch_undo", "Reverting Batch Edits", (int)undoSnapshot_.size(), nullptr, "Reverting changes...");
        int restaurados = 0;
        // Fase 2b: uma transação só pro lote de restauração inteiro (cada
        // item já dispara até 6 salvarMetadado + escritas cruas + geo, sem
        // isto viravam dezenas de transações implícitas separadas).
        auto& dbUndo = projeto_.projeto().registro();
        std::unique_lock<std::recursive_mutex> writeLock(projeto_.writeMutex());
        bool emTransacaoUndo = false;
        try { dbUndo.exec("BEGIN IMMEDIATE"); emTransacaoUndo = true; } catch (...) {}
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
        if (emTransacaoUndo) {
            std::vector<std::string> idsUndo;
            for (const auto& [id, snap] : undoSnapshot_) idsUndo.push_back(id);
            if (!commitOuReverter(dbUndo, idsUndo)) {
                // Nada foi desfeito: mantém o snapshot e o botão pra tentar de novo.
                ProgressoGlobal::obterInstancia().concluirTarefa("batch_undo", "Undo failed (rolled back)");
                if (aoAplicarEmLote) aoAplicarEmLote();
                return;
            }
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

    // COMMIT do lote; se falhar, ROLLBACK e relê os itens afetados — a grade
    // já tinha sido atualizada item a item (aoAplicarSucessoItem) com valores
    // que o ROLLBACK acabou de desfazer.
    bool commitOuReverter(matriz::db::Database& db, const std::vector<std::string>& ids) {
        try {
            db.exec("COMMIT");
            return true;
        } catch (...) {
            try { db.exec("ROLLBACK"); } catch (...) {}
        }
        juce::Logger::writeToLog("[ficha-lote] COMMIT falhou, ROLLBACK de " + juce::String((int) ids.size()) + " item(ns)");
        if (aoAplicarSucessoItem)
            for (const auto& id : ids) aoAplicarSucessoItem(id);
        return false;
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

    // Item 1 — mesma diagramação em 3 cartões com bordas do modo de item
    // único (FichaConteudo): DUBLIN CORE / ASSET & USER METADATA / GEO
    // LOCATION, com os mesmos cabeçalhos, botões de ajuda/recolher e a
    // mesma ordem visual (User Asset, GeoLocation, Dublin Core).
    std::unique_ptr<juce::Label> secHeaderDublinCoreLote_;
    std::unique_ptr<juce::TextButton> btnAjudaDublinCoreLote_;
    std::unique_ptr<juce::TextButton> btnCollapseDublinCoreLote_;
    bool colapsadoDublinCoreLote_ = true;

    std::unique_ptr<juce::Label> secHeaderUserAssetLote_;
    std::unique_ptr<juce::TextButton> btnAjudaUserAssetLote_;
    std::unique_ptr<juce::TextButton> btnCollapseUserAssetLote_;
    bool colapsadoUserAssetLote_ = false;

    juce::Rectangle<int> quadroDublinCoreLote_;
    juce::Rectangle<int> quadroUserAssetLote_;
    juce::Rectangle<int> quadroGeoLocationLote_;

    static bool ehCampoDublinCoreLote(const LinhaLote& linha) {
        return linha.campoId.rfind("dc_", 0) == 0;
    }
};

// ---------------------------------------------------------------------------
// FichaPanelComponent
// ---------------------------------------------------------------------------

FichaPanelComponent::FichaPanelComponent(ProjetoAberto& projeto) : projeto_(projeto) {
    conteudo_ = std::make_unique<FichaConteudo>(projeto);
    viewport_ = std::make_unique<juce::Viewport>();
    viewport_->setViewedComponent(conteudo_.get(), false);
    addAndMakeVisible(*viewport_);

    conteudo_->aoRelayoutNecessario = [this] {
        conteudo_->relayout(viewport_->getWidth() - viewport_->getScrollBarThickness(), viewport_->getHeight());
    };
    conteudo_->aoMudarClassificacao = [this] { if (aoAplicarEmLote) aoAplicarEmLote(); };
    conteudo_->aoMudar = [this] { if (aoMudar) aoMudar(); };
    conteudo_->aoAplicarSucesso = [this](const std::string& itemId) { if (aoAplicarSucesso) aoAplicarSucesso(itemId); };
}

FichaPanelComponent::~FichaPanelComponent() {
    // Cobre fechar projeto e fechar o app: nesses dois casos o destrutor
    // roda com projeto_/mosaico_/arvoreAcervo_ ainda vivos (ordem de
    // destruição em MainComponent), então o flush ainda alcança o banco e
    // a árvore antes de tudo ser desmontado.
    salvarPendencias();
}

void FichaPanelComponent::salvarPendencias() {
    if (conteudo_) conteudo_->comitarPendencias();
    if (conteudoLote_) conteudoLote_->comitarPendencias();
}

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
    // Trocar de item é o ponto de perda mais comum (Bug 1): sem isto, texto
    // digitado no card de lote (se estava em modo lote) é descartado quando
    // conteudoLote_ é escondido, e construirParaItem() só flusha o próprio
    // conteudo_, não o outro.
    salvarPendencias();
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

    salvarPendencias();
    itemIdAtual_.clear();
    modoLote_ = true;
    if (!conteudoLote_) {
        conteudoLote_ = std::make_unique<FichaLoteConteudo>(projeto_);
        conteudoLote_->aoRelayoutNecessario = [this] {
            conteudoLote_->relayout(viewport_->getWidth() - viewport_->getScrollBarThickness(), viewport_->getHeight());
        };
        conteudoLote_->aoAplicarEmLote = [this] { if (aoAplicarEmLote) aoAplicarEmLote(); };
        conteudoLote_->aoAplicarSucessoItem = [this](const std::string& itemId) {
            if (aoAplicarSucesso) aoAplicarSucesso(itemId);
        };
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
    int altura = viewport_->getHeight();
    if (modoLote_ && conteudoLote_) conteudoLote_->relayout(largura, altura);
    else conteudo_->relayout(largura, altura);
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
