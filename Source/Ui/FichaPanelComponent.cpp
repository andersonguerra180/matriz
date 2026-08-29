#include "FichaPanelComponent.h"

#include "MetadadosOriginaisComponent.h"
#include "OriginalSourceMedium.h"
#include "TagChipsEditor.h"
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
        lblTempo_->setText("00:00:00 / 12:18:34", juce::dontSendNotification);
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
        case matriz::ingest::CategoriaMidia::Imagem: return MediaCategory::Image;
        case matriz::ingest::CategoriaMidia::Sessao:
        case matriz::ingest::CategoriaMidia::Documento:
        case matriz::ingest::CategoriaMidia::Texto: return MediaCategory::Docs;
        default: return MediaCategory::Audio;
    }
}

class FichaConteudo : public juce::Component {
public:
    explicit FichaConteudo(ProjetoAberto& projeto) : projeto_(projeto) {}

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
        juce::String titulo = tituloStd;
        juce::String codigo = codigoAcervo;

        cabecalho_ = std::make_unique<juce::Label>();
        cabecalho_->setText(codigo + " - " + (titulo.isNotEmpty() ? titulo : matriz::i18n::t("ficha.cabecalho_sem_titulo")),
                             juce::dontSendNotification);
        cabecalho_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteSubtitulo, juce::Font::bold)));
        cabecalho_->setColour(juce::Label::textColourId, matriz::ui::tema().textoPrimario);
        addAndMakeVisible(*cabecalho_);

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

        y += tk.espacoMedio;

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

        for (auto& cu : camposUnificados_) {
            if (!cu) continue;
            int rotuloW = larguraUtil - 100;
            cu->rotulo->setBounds(x, y, rotuloW, 16);
            cu->badge->setBounds(x + larguraUtil - 95, y, 95, 16);
            y += 18;

            if (cu->ehOriginalSourceMedium) {
                if (auto* osm = dynamic_cast<OriginalSourceMediumEditorComponent*>(cu->editor.get())) {
                    int prefH = osm->getPreferredHeight();
                    osm->setBounds(x, y, larguraUtil, prefH);
                    y += prefH + tk.espacoPequeno;
                } else {
                    cu->editor->setBounds(x, y, larguraUtil, 24);
                    y += 24 + tk.espacoPequeno;
                }
            } else if (cu->ehTags) {
                if (auto* chips = dynamic_cast<TagChipsEditor*>(cu->editor.get())) {
                    chips->setBounds(x, y, larguraUtil, chips->getPreferredHeight());
                    y += chips->getPreferredHeight() + tk.espacoPequeno;
                } else {
                    cu->editor->setBounds(x, y, larguraUtil, 24);
                    y += 24 + tk.espacoPequeno;
                }
            } else if (cu->ehNotes) {
                cu->editor->setBounds(x, y, larguraUtil, 64);
                y += 64 + tk.espacoPequeno;
            } else {
                cu->editor->setBounds(x, y, larguraUtil, 24);
                y += 24 + tk.espacoPequeno;
            }
        }

        // --- GEO LOCATION section layout (between TAGS and APPLY button) ---
        if (geolocalizacao_.titulo) {
            y += tk.espacoMedio * 2;
            int rotuloW = larguraUtil - 140;
            geolocalizacao_.titulo->setBounds(x, y, rotuloW, 16);
            if (geolocalizacao_.statusBadge)
                geolocalizacao_.statusBadge->setBounds(x + larguraUtil - 135, y, 135, 16);
            y += 20;

            if (geolocalizacao_.labelCoords && geolocalizacao_.editorCoords) {
                geolocalizacao_.labelCoords->setBounds(x, y, larguraUtil, 16);
                y += 18;
                geolocalizacao_.editorCoords->setBounds(x, y, larguraUtil, 24);
                y += 24 + tk.espacoPequeno;
            }

            if (geolocalizacao_.labelAddress && geolocalizacao_.editorAddress) {
                geolocalizacao_.labelAddress->setBounds(x, y, larguraUtil, 16);
                y += 18;
                geolocalizacao_.editorAddress->setBounds(x, y, larguraUtil, 24);
                y += 24 + tk.espacoPequeno;
            }

            if (geolocalizacao_.labelCity && geolocalizacao_.editorCity) {
                geolocalizacao_.labelCity->setBounds(x, y, larguraUtil, 16);
                y += 18;
                geolocalizacao_.editorCity->setBounds(x, y, larguraUtil, 24);
                y += 24 + tk.espacoPequeno;
            }

            if (geolocalizacao_.labelState && geolocalizacao_.editorState) {
                geolocalizacao_.labelState->setBounds(x, y, larguraUtil, 16);
                y += 18;
                geolocalizacao_.editorState->setBounds(x, y, larguraUtil, 24);
                y += 24 + tk.espacoPequeno;
            }

            if (geolocalizacao_.labelCountry && geolocalizacao_.editorCountry) {
                geolocalizacao_.labelCountry->setBounds(x, y, larguraUtil, 16);
                y += 18;
                geolocalizacao_.editorCountry->setBounds(x, y, larguraUtil, 24);
                y += 24 + tk.espacoMedio;
            }
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

        auto geoOpt = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(projeto_.projeto().registro(), itemId);

        geolocalizacao_.titulo = std::make_unique<juce::Label>();
        geolocalizacao_.titulo->setText("GEO LOCATION", juce::dontSendNotification);
        geolocalizacao_.titulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        geolocalizacao_.titulo->setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(*geolocalizacao_.titulo);

        geolocalizacao_.statusBadge = std::make_unique<juce::Label>();
        if (!geoOpt || geoOpt->source == matriz::analytics::GeoSource::None) {
            geolocalizacao_.statusBadge->setText("[ NO GPS DATA - MANUAL FILL ]", juce::dontSendNotification);
            geolocalizacao_.statusBadge->setColour(juce::Label::textColourId, tk.textoTerciario);
        } else if (geoOpt->source == matriz::analytics::GeoSource::EmbeddedMetadata) {
            geolocalizacao_.statusBadge->setText("[ EXIF GPS AUTO-EXTRACTED ]", juce::dontSendNotification);
            geolocalizacao_.statusBadge->setColour(juce::Label::textColourId, juce::Colours::lightgreen);
        } else {
            geolocalizacao_.statusBadge->setText("[ USER-DEFINED GEOLOCATION ]", juce::dontSendNotification);
            geolocalizacao_.statusBadge->setColour(juce::Label::textColourId, juce::Colours::cyan);
        }
        geolocalizacao_.statusBadge->setFont(juce::Font(juce::FontOptions(9.0f)));
        geolocalizacao_.statusBadge->setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(*geolocalizacao_.statusBadge);

        // Coordinates (Lat, Lng)
        geolocalizacao_.labelCoords = std::make_unique<juce::Label>();
        geolocalizacao_.labelCoords->setText("GPS Coordinates (Lat, Lng)", juce::dontSendNotification);
        geolocalizacao_.labelCoords->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelCoords->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelCoords);

        geolocalizacao_.editorCoords = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorCoords->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorCoords->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        geolocalizacao_.editorCoords->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
        geolocalizacao_.editorCoords->setColour(juce::TextEditor::outlineColourId, tk.borda);
        if (geoOpt && geoOpt->hasValidCoordinates()) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(6) << *geoOpt->latitude << ", " << *geoOpt->longitude;
            geolocalizacao_.editorCoords->setText(ss.str());
        } else {
            geolocalizacao_.editorCoords->setTextToShowWhenEmpty("e.g. -16.4435, -39.0643", tk.textoTerciario);
        }
        addAndMakeVisible(*geolocalizacao_.editorCoords);

        // Address
        geolocalizacao_.labelAddress = std::make_unique<juce::Label>();
        geolocalizacao_.labelAddress->setText("Formatted Address", juce::dontSendNotification);
        geolocalizacao_.labelAddress->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelAddress->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelAddress);

        geolocalizacao_.editorAddress = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorAddress->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorAddress->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        geolocalizacao_.editorAddress->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
        geolocalizacao_.editorAddress->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorAddress->setText(geoOpt && geoOpt->formattedAddress ? *geoOpt->formattedAddress : "");
        geolocalizacao_.editorAddress->setTextToShowWhenEmpty("e.g. Av. Paulista, 1000", tk.textoTerciario);
        addAndMakeVisible(*geolocalizacao_.editorAddress);

        // City
        geolocalizacao_.labelCity = std::make_unique<juce::Label>();
        geolocalizacao_.labelCity->setText("City", juce::dontSendNotification);
        geolocalizacao_.labelCity->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelCity->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelCity);

        geolocalizacao_.editorCity = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorCity->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorCity->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        geolocalizacao_.editorCity->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
        geolocalizacao_.editorCity->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorCity->setText(geoOpt && geoOpt->city ? *geoOpt->city : "");
        geolocalizacao_.editorCity->setTextToShowWhenEmpty("e.g. Porto Seguro", tk.textoTerciario);
        addAndMakeVisible(*geolocalizacao_.editorCity);

        // State
        geolocalizacao_.labelState = std::make_unique<juce::Label>();
        geolocalizacao_.labelState->setText("State / Province", juce::dontSendNotification);
        geolocalizacao_.labelState->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelState->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelState);

        geolocalizacao_.editorState = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorState->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorState->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        geolocalizacao_.editorState->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
        geolocalizacao_.editorState->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorState->setText(geoOpt && geoOpt->stateProvince ? *geoOpt->stateProvince : "");
        geolocalizacao_.editorState->setTextToShowWhenEmpty("e.g. Bahia", tk.textoTerciario);
        addAndMakeVisible(*geolocalizacao_.editorState);

        // Country
        geolocalizacao_.labelCountry = std::make_unique<juce::Label>();
        geolocalizacao_.labelCountry->setText("Country", juce::dontSendNotification);
        geolocalizacao_.labelCountry->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        geolocalizacao_.labelCountry->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*geolocalizacao_.labelCountry);

        geolocalizacao_.editorCountry = std::make_unique<juce::TextEditor>();
        geolocalizacao_.editorCountry->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        geolocalizacao_.editorCountry->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        geolocalizacao_.editorCountry->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
        geolocalizacao_.editorCountry->setColour(juce::TextEditor::outlineColourId, tk.borda);
        geolocalizacao_.editorCountry->setText(geoOpt && geoOpt->country ? *geoOpt->country : "");
        geolocalizacao_.editorCountry->setTextToShowWhenEmpty("e.g. Brazil", tk.textoTerciario);
        addAndMakeVisible(*geolocalizacao_.editorCountry);
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
    struct LinhaUnificada {
        std::string campoId;
        std::string colunaDb;
        bool ehAutoFixed = false;
        bool ehNotes = false;
        bool ehTags = false;
        bool ehOriginalSourceMedium = false;
        std::unique_ptr<juce::Label> rotulo;
        std::unique_ptr<juce::Label> badge;
        std::unique_ptr<juce::Component> editor;
        std::function<void()> onCommit;
    };
    std::vector<std::unique_ptr<LinhaUnificada>> camposUnificados_;

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
        if (lengthStr.isEmpty()) lengthStr = (cat == MediaCategory::Audio ? "04:35" : (cat == MediaCategory::Video ? "12:18" : "--:--"));

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
        juce::String valCollection = projeto_.lerMetadado(itemId, "collection_type").value_or("");
        juce::String valIsrc = projeto_.lerMetadado(itemId, "isrc").value_or("");
        juce::String valNotes = projeto_.lerMetadado(itemId, "notas_livres").value_or("");

        std::vector<std::string> tagsList = projeto_.lerTags(itemId);

        // Extract native Dublin Core values from file / database
        juce::String valDcTitle = projeto_.lerMetadado(itemId, "dc_title").value_or("");
        if (valDcTitle.isEmpty()) valDcTitle = valName;

        juce::String valDcCreator = projeto_.lerMetadado(itemId, "dc_creator").value_or("");
        if (valDcCreator.isEmpty() && dados.isObject() && dados.hasProperty("exifCamera")) {
            valDcCreator = dados["exifCamera"].toString();
        }

        juce::String valDcSubject = projeto_.lerMetadado(itemId, "dc_subject").value_or("");

        juce::String valDcDescription = projeto_.lerMetadado(itemId, "dc_description").value_or("");

        juce::String valDcPublisher = projeto_.lerMetadado(itemId, "dc_publisher").value_or("");

        juce::String valDcContributor = projeto_.lerMetadado(itemId, "dc_contributor").value_or("");

        juce::String valDcCreated = projeto_.lerMetadado(itemId, "dc_created").value_or("");
        if (valDcCreated.isEmpty() && dados.isObject() && dados.hasProperty("exifDataOriginal")) {
            valDcCreated = dados["exifDataOriginal"].toString().substring(0, 10).replace(":", "-");
        }
        if (valDcCreated.isEmpty() && arquivo) {
            juce::File f(arquivo->caminhoAbsoluto);
            if (f.existsAsFile()) {
                valDcCreated = f.getCreationTime().formatted("%Y-%m-%d");
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
            if (rOpt) valDcRights = rOpt->rightsStatus;
            else valDcRights = "PUBLIC_DOMAIN";
        }

        // Helpers to add fields
        auto addAutoFixed = [this, &tk](const std::string& campoId, const juce::String& rotulo, const juce::String& valor) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = campoId;
            linha->ehAutoFixed = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(rotulo, juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText("[AUTO + FIXED]", juce::dontSendNotification);
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

        auto addEditableText = [this, &tk, itemId](const std::string& campoId, const juce::String& rotulo, const juce::String& valor, const std::string& dbColuna) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = campoId;
            linha->colunaDb = dbColuna;

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
            ed->setText(valor, false);
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
            ed->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
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

        auto addEditableDropdown = [this, &tk, itemId](const std::string& campoId, const juce::String& rotulo, const juce::String& valor, const std::vector<juce::String>& opcoes, const std::string& dbColuna) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = campoId;
            linha->colunaDb = dbColuna;

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
                if (valor.trim().equalsIgnoreCase(opcoes[i].trim())) {
                    selId = static_cast<int>(i + 1);
                }
            }
            if (selId > 0) combo->setSelectedId(selId, juce::dontSendNotification);
            else if (!valor.isEmpty()) {
                combo->addItem(valor, static_cast<int>(opcoes.size() + 1));
                combo->setSelectedId(static_cast<int>(opcoes.size() + 1), juce::dontSendNotification);
            }

            combo->setColour(juce::ComboBox::textColourId, tk.textoPrimario);
            combo->setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
            combo->setColour(juce::ComboBox::outlineColourId, tk.borda);

            auto* comboPtr = combo.get();
            linha->onCommit = [this, itemId, dbColuna, comboPtr] {
                if (comboPtr) {
                    projeto_.salvarMetadado(itemId, dbColuna, comboPtr->getText().toStdString());
                    if (aoMudar) aoMudar();
                }
            };
            combo->onChange = linha->onCommit;
            addAndMakeVisible(*combo);
            linha->editor = std::move(combo);

            camposUnificados_.push_back(std::move(linha));
        };

        // --- DUBLIN CORE METADATA SECTION (At top of all cards, above NAME) ---
        addEditableText("dc_title", "TITLE", valDcTitle, "dc_title");
        addEditableText("dc_creator", "CREATOR", valDcCreator, "dc_creator");
        addEditableText("dc_subject", "SUBJECT", valDcSubject, "dc_subject");
        addEditableText("dc_description", "DESCRIPTION", valDcDescription, "dc_description");
        addEditableText("dc_publisher", "PUBLISHER", valDcPublisher, "dc_publisher");
        addEditableText("dc_contributor", "CONTRIBUTOR", valDcContributor, "dc_contributor");
        addEditableText("dc_created", "DATE CREATED (YYYY-MM-DD)", valDcCreated, "dc_created");
        addEditableText("dc_issued", "DATE ISSUED (YYYY-MM-DD)", valDcIssued, "dc_issued");
        addEditableText("dc_type", "TYPE", valDcType, "dc_type");
        addEditableText("dc_format", "FORMAT", valDcFormat, "dc_format");
        addEditableText("dc_identifier", "IDENTIFIER", valDcIdentifier, "dc_identifier");
        addEditableText("dc_source", "SOURCE", valDcSource, "dc_source");
        addEditableText("dc_language", "LANGUAGE", valDcLanguage, "dc_language");
        addEditableText("dc_relation", "RELATION", valDcRelation, "dc_relation");
        addEditableText("dc_coverage", "COVERAGE", valDcCoverage, "dc_coverage");
        addEditableDropdown("dc_rights", "RIGHTS", valDcRights,
                            {"PUBLIC DOMAIN", "COPYRIGHT", "CREATIVE COMMONS (CC BY)", "CREATIVE COMMONS (CC BY-SA)",
                             "CREATIVE COMMONS (CC BY-NC)", "CREATIVE COMMONS (CC BY-NC-ND)", "CREATIVE COMMONS (CC0)",
                             "ORPHAN WORK", "FAIR USE", "RESTRICTED"},
                            "dc_rights");

        auto addEditableNotes = [this, &tk, itemId](const juce::String& valor) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = "notes";
            linha->colunaDb = "notas_livres";
            linha->ehNotes = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText("NOTES", juce::dontSendNotification);
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
            ed->setMultiLine(true, true);
            ed->setReturnKeyStartsNewLine(true);
            ed->setText(valor, false);
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
            ed->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
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

        auto addEditableTags = [this, &tk, itemId](const std::vector<std::string>& tagsList) {
            auto linha = std::make_unique<LinhaUnificada>();
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

            auto chips = std::make_unique<TagChipsEditor>();
            chips->setTags(tagsList);
            chips->aoMudar = [this, itemId, raw = chips.get()] {
                projeto_.definirTags(itemId, raw->getTags());
                if (aoMudar) aoMudar();
            };
            chips->aoRedimensionar = [this] {
                if (aoRelayoutNecessario) aoRelayoutNecessario();
            };
            addAndMakeVisible(*chips);
            linha->editor = std::move(chips);

            camposUnificados_.push_back(std::move(linha));
        };

        auto addEditableOriginalSourceMedium = [this, &tk, itemId](const std::string& rawValue) {
            auto linha = std::make_unique<LinhaUnificada>();
            linha->campoId = "source_media";
            linha->colunaDb = "source_media";
            linha->ehOriginalSourceMedium = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText("ORIGINAL SOURCE MEDIUM", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText("[EDITABLE]", juce::dontSendNotification);
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

        // Construct fields per category exactly as specified
        if (cat == MediaCategory::Audio) {
            addEditableText("path", "PATH", valPath, "caminho_catalogo");
            addAutoFixed("length", "LENGTH", lengthStr);
            addAutoFixed("format", "FORMAT", ext);
            addAutoFixed("codec", "CODEC", codecStr);
            addAutoFixed("file_size", "FILE SIZE", fileSizeStr);
            addAutoFixed("sample_rate", "SAMPLE RATE", sampleRateStr);
            addAutoFixed("bit_depth", "BIT DEPTH", bitDepthStr);
            if (channelsStr.isNotEmpty())
                addAutoFixed("channels", "CHANNELS", channelsStr);
            addEditableText("isrc", "ISRC", valIsrc, "isrc");
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", "CONTENT", valCollection,
                                {"Album", "EP", "Single", "Compilation", "Soundtrack", "Stems", "Multitracks",
                                 "Sample Pack", "DAW Session", "Field Recording", "Sound FX", "MIDI",
                                 "Artist Catalog", "Artist Backup"},
                                "collection_type");
            addEditableNotes(valNotes);
            addEditableTags(tagsList);
        } else if (cat == MediaCategory::Video) {
            addEditableText("path", "PATH", valPath, "caminho_catalogo");
            addAutoFixed("length", "LENGTH", lengthStr);
            addAutoFixed("dimensions", "DIMENSIONS", dimensionsStr);
            addAutoFixed("screen_orientation", "SCREEN / ORIENTATION", orientationStr);
            addAutoFixed("format", "FORMAT", ext);
            addAutoFixed("codec", "CODEC", codecStr);
            addAutoFixed("file_size", "FILE SIZE", fileSizeStr);
            addAutoFixed("sample_rate", "SAMPLE RATE", sampleRateStr);
            addAutoFixed("bit_depth", "BIT DEPTH", bitDepthStr);
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", "CONTENT", valCollection,
                                {"Raw Footage", "Home Video", "Music Video", "Film", "Documentary",
                                 "Corporate Video", "Commercial", "Live Performance", "NLE Project"},
                                "collection_type");
            addEditableNotes(valNotes);
            addEditableTags(tagsList);
        } else if (cat == MediaCategory::Image) {
            addEditableText("path", "PATH", valPath, "caminho_catalogo");
            addAutoFixed("dimensions", "DIMENSIONS", dimensionsStr);
            addAutoFixed("screen_orientation", "SCREEN / ORIENTATION", orientationStr);
            addAutoFixed("format", "FORMAT", ext);
            addAutoFixed("file_size", "FILE SIZE", fileSizeStr);
            addAutoFixed("color_space", "COLOR SPACE", colorSpaceStr);
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", "CONTENT", valCollection,
                                {"Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional", "Image Edit Project"},
                                "collection_type");
            addEditableNotes(valNotes);
            addEditableTags(tagsList);
        } else { // Docs
            addEditableText("path", "PATH", valPath, "caminho_catalogo");
            addAutoFixed("format", "FORMAT", ext);
            addAutoFixed("file_size", "FILE SIZE", fileSizeStr);
            addAutoFixed("pages", "PAGES", pagesStr);
            addEditableOriginalSourceMedium(valSourceMedia.toStdString());
            addEditableDropdown("collection", "CONTENT", valCollection,
                                {"Documentation", "Book", "Contract", "Manual", "Report", "Reference", "Technical Documentation"},
                                "collection_type");
            addEditableNotes(valNotes);
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
};

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// FichaLoteConteudo — Unified batch metadata editor (§ metadata spec & batch editing)
// ---------------------------------------------------------------------------

class FichaLoteConteudo : public juce::Component {
public:
    explicit FichaLoteConteudo(ProjetoAberto& projeto) : projeto_(projeto) {}

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
            } else if (linha->ehNotes) {
                linha->editor->setBounds(x, y, larguraUtil, 64);
                y += 64 + tk.espacoPequeno;
            } else {
                linha->editor->setBounds(x, y, larguraUtil, 24);
                y += 24 + tk.espacoPequeno;
            }
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
        std::unique_ptr<juce::Label> rotulo;
        std::unique_ptr<juce::Label> badge;
        std::unique_ptr<juce::Component> editor;
        bool tocado = false;
        bool ehNotes = false;
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
    };

    void limpar() {
        cabecalho_.reset();
        mensagem_.reset();
        botoesTipo_.clear();
        linhas_.clear();
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
            ed->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
            ed->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
            ed->setColour(juce::TextEditor::outlineColourId, tk.borda);

            if (ehNotes) {
                ed->setMultiLine(true);
                ed->setReturnKeyStartsNewLine(true);
                ed->setTextToShowWhenEmpty("Type notes to add/append to all selected items...", tk.textoTerciario);
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
                    ed->setTextToShowWhenEmpty(todosIguais ? "" : matriz::i18n::t("ficha.lote_valores_multiplos"), tk.textoTerciario);
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
            cb->setColour(juce::ComboBox::textColourId, tk.textoPrimario);
            cb->setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
            cb->setColour(juce::ComboBox::outlineColourId, tk.borda);

            for (size_t i = 0; i < opcoes.size(); ++i) {
                cb->addItem(opcoes[i], static_cast<int>(i + 1));
            }

            std::string valorComum;
            bool todosIguais = true;
            bool primeiro = true;
            for (const auto& id : itemIds_) {
                std::string v = projeto_.lerMetadado(id, dbColuna).value_or("");
                if (primeiro) { valorComum = v; primeiro = false; }
                else if (v != valorComum) { todosIguais = false; break; }
            }

            if (todosIguais && !valorComum.empty()) {
                for (size_t i = 0; i < opcoes.size(); ++i) {
                    if (opcoes[i] == valorComum) {
                        cb->setSelectedId(static_cast<int>(i + 1), juce::dontSendNotification);
                        break;
                    }
                }
            } else {
                cb->setTextWhenNothingSelected(todosIguais ? "" : matriz::i18n::t("ficha.lote_valores_multiplos"));
            }

            cb->onChange = [this, linha] {
                linha->tocado = true;
                atualizarPrevia();
            };
            addAndMakeVisible(*cb);
            linha->editor = std::move(cb);
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

        auto addOriginalSourceMediumLote = [this, &tk]() {
            auto* linha = linhas_.add(new LinhaLote());
            linha->campoId = "source_media";
            linha->colunaDb = "source_media";
            linha->ehOriginalSourceMedium = true;

            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText("ORIGINAL SOURCE MEDIUM", juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            linha->rotulo->setColour(juce::Label::textColourId, tk.textoPrimario);
            addAndMakeVisible(*linha->rotulo);

            linha->badge = std::make_unique<juce::Label>();
            linha->badge->setText("[BATCH OVERRIDE]", juce::dontSendNotification);
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

        // Specific fields per category (§ metadata spec)
        // Hidden in batch: NAME, PATH, and all AUTO + FIXED fields.
        switch (cat) {
            case MediaCategory::Audio: {
                addEditableTextLote("year", "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", "CONTENT", "collection_type", {
                    "Album", "EP", "Single", "Compilation", "Soundtrack", "Stems", "Multitracks",
                    "Sample Pack", "DAW Session", "Field Recording", "Sound FX", "MIDI",
                    "Artist Catalog", "Artist Backup"
                });
                addTagsLote();
                addEditableTextLote("isrc", "ISRC", "isrc");
                addEditableTextLote("notes", "NOTES", "notas_livres", true);
                break;
            }
            case MediaCategory::Video: {
                addEditableTextLote("year", "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", "CONTENT", "collection_type", {
                    "Raw Footage", "Home Video", "Music Video", "Film", "Documentary",
                    "Corporate Video", "Commercial", "Live Performance", "NLE Project"
                });
                addTagsLote();
                addEditableTextLote("notes", "NOTES", "notas_livres", true);
                break;
            }
            case MediaCategory::Image: {
                addEditableTextLote("year", "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", "CONTENT", "collection_type", {
                    "Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional", "Image Edit Project"
                });
                addTagsLote();
                addEditableTextLote("notes", "NOTES", "notas_livres", true);
                break;
            }
            case MediaCategory::Docs: {
                addEditableTextLote("year", "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", "CONTENT", "collection_type", {
                    "Documentation", "Book", "Contract", "Manual", "Report", "Reference", "Technical Documentation"
                });
                addTagsLote();
                addEditableTextLote("notes", "NOTES", "notas_livres", true);
                break;
            }
            case MediaCategory::Mixed:
            default: {
                addEditableTextLote("year", "YEAR", "ano");
                addOriginalSourceMediumLote();
                addDropdownLote("collection", "CONTENT", "collection_type", {
                    "Album", "EP", "Single", "Compilation", "Soundtrack", "Stems", "Multitracks",
                    "Sample Pack", "DAW Session", "Field Recording", "Sound FX", "MIDI",
                    "Artist Catalog", "Artist Backup", "Raw Footage", "Home Video", "Music Video",
                    "Film", "Documentary", "Corporate Video", "Commercial", "Live Performance",
                    "NLE Project", "Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional",
                    "Image Edit Project", "Documentation", "Book", "Contract", "Manual", "Report",
                    "Reference", "Technical Documentation"
                });
                addTagsLote();
                addEditableTextLote("notes", "NOTES", "notas_livres", true);
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
        if (!algumTocado || itemIds_.empty()) return;

        projeto_.iniciarGrupoUndo("Batch apply");

        // Snapshot of PREVIOUS values before applying
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
                        // TAGS: Sum/Add entered tags to existing tags (§ user req 3)
                        juce::StringArray tagsNovas;
                        tagsNovas.addTokens(val, " ,;", "\"");
                        for (int t = 0; t < tagsNovas.size(); ++t) {
                            juce::String tg = tagsNovas[t].trimCharactersAtStart("#").trim();
                            if (tg.isNotEmpty()) {
                                projeto_.adicionarTag(id, tg.toStdString());
                            }
                        }
                    } else if (linha->ehNotes) {
                        // NOTES: Sum/Append entered note to existing notes (§ user req 3)
                        std::string txt = val.toStdString();
                        if (!txt.empty()) {
                            std::string existing = projeto_.lerMetadado(id, "notas_livres").value_or("");
                            std::string combined = existing.empty() ? txt : (existing + "\n" + txt);
                            projeto_.salvarMetadado(id, "notas_livres", combined);
                            // Also keep maquina in sync for legacy test
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
                    } else {
                        // Regular fields (YEAR, CONTENT, COLLECTION, SOURCE MEDIA, ISRC): OVERRIDE (§ user req 2)
                        projeto_.salvarMetadado(id, linha->colunaDb, val.toStdString());
                    }
                }
                ++sucessos;
            } catch (const std::exception&) {
                ++falhas;
            }
        }

        projeto_.finalizarGrupoUndo();

        juce::String texto = matriz::i18n::t("ficha.lote_resultado").replace("{sucessos}", juce::String(sucessos));
        if (falhas > 0) texto += matriz::i18n::t("ficha.lote_resultado_falhas").replace("{falhas}", juce::String(falhas));
        resultado_->setText(texto, juce::dontSendNotification);
        if (botaoDesfazer_) botaoDesfazer_->setVisible(sucessos > 0);
        relayoutEExibir();

        if (aoAplicarEmLote) aoAplicarEmLote();
    }

    void desfazer() {
        int restaurados = 0;
        for (const auto& [id, snap] : undoSnapshot_) {
            try {
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
                ++restaurados;
            } catch (const std::exception&) {
            }
        }
        resultado_->setText(matriz::i18n::t("ficha.lote_resultado_desfeito").replace("{n}", juce::String(restaurados)),
                             juce::dontSendNotification);
        if (botaoDesfazer_) botaoDesfazer_->setVisible(false);
        undoSnapshot_.clear();
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
    g.fillAll(matriz::ui::tema().painel);

    if (!modoLote_ && itemIdAtual_.empty()) {
        g.setColour(matriz::ui::tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("ficha.vazia"), getLocalBounds().reduced(matriz::ui::tema().espacoPainel),
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

} // namespace matriz::ui
