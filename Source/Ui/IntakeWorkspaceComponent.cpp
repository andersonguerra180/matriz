// ==============================================================================
// INTAKE WORKSPACE COMPONENT
// STRICT APP-WIDE RULE: 100% ENGLISH UI. ZERO PORTUGUESE TEXT IN USER INTERFACE.
// ==============================================================================

#include "IntakeWorkspaceComponent.h"
#include "LegendaAtalhosIntake.h"
#include "GeoAcoes.h"
#include "GeoFavoritosGerenciar.h"
#include "FloatingPreviewWindow.h"
#include "PainelMetadadosArquivo.h"
#include "GoogleDriveContas.h"
#include "TraducaoContent.h"
#include "TiposContainer.h"
#include "../Ficha/AutocompleteHistorico.h"

#include <AssetsBinaryData.h>
#include "OriginalSourceMedium.h"
#include "ProjetoAberto.h"
#include "ProgressoGlobal.h"
#include "Tokens.h"
#include "Moldura.h"
#include "Tablet.h"
#include "HierarquiaEditorComponent.h"
#include "../Analytics/AssetGeolocation.h"
#include "../Ingest/LeituraTecnica.h"
#include "LogosTipoArquivo.h"
#include "PainelAndroidUsb.h"
#include "../Celular/DispositivoAndroidUsb.h"
#include "PainelImportarWhatsapp.h"
#include "ImportarMenu.h"
#include "ContainerVisual.h"
#include "JanelaPlayerDdp.h"
#include "../Celular/ExistentesProjeto.h"
#include "../Diag/Watchdog.h"
#include "../I18n/Strings.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace matriz::ui {

namespace {

enum ColumnId {
    kColSelect = 1,
    kColType = 2,
    kColName = 3,
    kColOrigin = 10,
    kColDateCreated = 4,
    kColSize = 5,
    kColPath = 6,
    kColCollection = 7,
    kColSourceMedia = 8,
    kColAction = 9
};

juce::String formatarBytes(juce::int64 bytes) {
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024.0, 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return juce::String(bytes / (1024.0 * 1024.0), 1) + " MB";
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

juce::Colour corParaCategoria(const juce::String& cat) {
    if (cat == "Audio") return juce::Colour(0xff38bdf8); // Sky blue
    if (cat == "Video") return juce::Colour(0xffa855f7); // Purple
    if (cat == "Image") return juce::Colour(0xfff59e0b); // Amber
    if (cat == "Document") return juce::Colour(0xff10b981); // Emerald
    if (cat == "Project") return juce::Colour(0xffec4899); // Pink
    return juce::Colour(0xff94a3b8); // Slate gray
}

juce::String rotuloDaOrigem(const juce::String& origem) {
    return i18n::t("intake.container_" + origem);
}

// Ladrilho com o logo (e, se `total` > 0, a contagem de arquivos) — selo do container.
void desenharSeloContainer(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& origem, int total) {
    const auto& tk = tema();
    g.setColour(tk.painel);
    g.fillRoundedRectangle(area.toFloat(), 5.0f);
    g.setColour(tk.borda);
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 5.0f, 1.0f);
    const auto& img = logoDaOrigem(origem);
    auto interno = area.reduced(3);
    if (total > 0) interno = interno.withTrimmedRight(0);
    if (img.isValid()) g.drawImageWithin(img, interno.getX(), interno.getY(), interno.getWidth(), interno.getHeight(), juce::RectanglePlacement::centred, false);
}

// Grid de miniaturas de um container de importação: o usuário desmarca o que NÃO quer aprovar. "Aplicar" tira os
// desmarcados do container (continuam no Intake como arquivos individuais).
class ContainerGridComponent : public juce::Component {
public:
    ContainerGridComponent(ProjetoAberto& projeto, std::vector<std::string> ids, std::function<void(std::vector<std::string>)> aoAplicar,
                           std::function<void()> aoFechar)
        : projeto_(projeto), ids_(std::move(ids)), aoAplicar_(std::move(aoAplicar)), aoFechar_(std::move(aoFechar)) {
        marcado_.assign(ids_.size(), true);
        miniaturas_.resize(ids_.size());
        nomes_.resize(ids_.size());
        for (size_t i = 0; i < ids_.size(); ++i)
            if (auto r = projeto_.obterItemResumo(ids_[i]))
                nomes_[i] = juce::String::fromUTF8(r->titulo.c_str());
        hint_.setText(i18n::t("intake.container_grid_dica"), juce::dontSendNotification);
        hint_.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        hint_.setColour(juce::Label::textColourId, tema().textoSecundario);
        addAndMakeVisible(hint_);
        contagem_.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        contagem_.setColour(juce::Label::textColourId, tema().textoPrimario);
        addAndMakeVisible(contagem_);
        grade_ = std::make_unique<Grade>(*this);
        viewport_.setViewedComponent(grade_.get(), false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
        moldura::estilizarBotao(btnAplicar_, true);
        moldura::estilizarBotao(btnCancelar_, false);
        btnAplicar_.setButtonText(i18n::t("intake.container_grid_aplicar"));
        btnCancelar_.setButtonText(i18n::t("intake.container_grid_cancelar"));
        btnAplicar_.onClick = [this] {
            std::vector<std::string> desmarcados;
            for (size_t i = 0; i < ids_.size(); ++i) if (!marcado_[i]) desmarcados.push_back(ids_[i]);
            if (aoAplicar_) aoAplicar_(std::move(desmarcados));
            if (aoFechar_) aoFechar_();
        };
        btnCancelar_.onClick = [this] { if (aoFechar_) aoFechar_(); };
        addAndMakeVisible(btnAplicar_);
        addAndMakeVisible(btnCancelar_);
        setSize(840, 600);
        atualizarContagem();
        carregarMiniaturas();
    }
    ~ContainerGridComponent() override { pool_.removeAllJobs(true, 2000); }

    void paint(juce::Graphics& g) override {
        const auto rodape = getLocalBounds().removeFromBottom(moldura::kAlturaRodape);
        g.setColour(moldura::paleta().poco);
        g.fillRect(rodape);
        g.setColour(moldura::paleta().linha);
        g.fillRect(rodape.withHeight(1));
    }
    void resized() override {
        auto area = getLocalBounds();
        auto rodape = area.removeFromBottom(moldura::kAlturaRodape).reduced(moldura::kMargemCorpo, 0);
        auto linhaBotoes = rodape.withSizeKeepingCentre(rodape.getWidth(), 40);
        btnAplicar_.setBounds(linhaBotoes.removeFromRight(130));
        linhaBotoes.removeFromRight(10);
        btnCancelar_.setBounds(linhaBotoes.removeFromRight(130));
        contagem_.setBounds(linhaBotoes);
        auto corpo = area.reduced(moldura::kMargemCorpo, 12);
        hint_.setBounds(corpo.removeFromTop(24));
        corpo.removeFromTop(6);
        viewport_.setBounds(corpo);
        grade_->ajustar(viewport_.getMaximumVisibleWidth());
    }

private:
    class Grade : public juce::Component {
    public:
        explicit Grade(ContainerGridComponent& dono) : dono_(dono) {}
        void ajustar(int largura) {
            colunas_ = juce::jmax(1, largura / (kCelula + kGap));
            const int linhas = (static_cast<int>(dono_.ids_.size()) + colunas_ - 1) / colunas_;
            setSize(largura, juce::jmax(10, linhas * (kCelula + kGap + kTexto)));
        }
        juce::Rectangle<int> celula(int i) const {
            return { (i % colunas_) * (kCelula + kGap), (i / colunas_) * (kCelula + kGap + kTexto), kCelula, kCelula + kTexto };
        }
        void paint(juce::Graphics& g) override {
            const auto& tk = tema();
            const auto clip = g.getClipBounds();
            for (int i = 0; i < static_cast<int>(dono_.ids_.size()); ++i) {
                const auto c = celula(i);
                if (!c.intersects(clip)) continue;
                const auto thumb = c.withHeight(kCelula);
                g.setColour(tk.painel);
                g.fillRoundedRectangle(thumb.toFloat(), 10.0f);
                const auto& img = dono_.miniaturas_[static_cast<size_t>(i)];
                if (img.isValid()) {
                    juce::Graphics::ScopedSaveState ss(g);
                    juce::Path recorte; recorte.addRoundedRectangle(thumb.toFloat(), 10.0f);
                    g.reduceClipRegion(recorte);
                    g.drawImageWithin(img, thumb.getX(), thumb.getY(), thumb.getWidth(), thumb.getHeight(),
                                      juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination, false);
                }
                const bool on = dono_.marcado_[static_cast<size_t>(i)];
                if (!on) { g.setColour(tk.fundo.withAlpha(0.65f)); g.fillRoundedRectangle(thumb.toFloat(), 10.0f); }
                g.setColour(on ? tk.acento : tk.borda);
                g.drawRoundedRectangle(thumb.toFloat().reduced(1.0f), 10.0f, on ? 2.0f : 1.0f);
                const juce::Rectangle<float> caixa(static_cast<float>(thumb.getX() + 8), static_cast<float>(thumb.getY() + 8), 20.0f, 20.0f);
                g.setColour(on ? tk.acento : tk.painel);
                g.fillEllipse(caixa);
                g.setColour(tk.borda);
                if (!on) g.drawEllipse(caixa.reduced(0.5f), 1.0f);
                if (on) {
                    g.setColour(tk.textoSobreAcento);
                    juce::Path v; v.startNewSubPath(caixa.getX() + 5, caixa.getCentreY()); v.lineTo(caixa.getX() + 9, caixa.getBottom() - 6); v.lineTo(caixa.getRight() - 5, caixa.getY() + 6);
                    g.strokePath(v, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                g.setColour(on ? tk.textoPrimario : tk.textoSecundario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteMicro)));
                g.drawText(dono_.nomes_[static_cast<size_t>(i)], c.withTrimmedTop(kCelula + 2), juce::Justification::centredLeft, true);
            }
        }
        void mouseDown(const juce::MouseEvent& e) override {
            for (int i = 0; i < static_cast<int>(dono_.ids_.size()); ++i)
                if (celula(i).withHeight(kCelula).contains(e.getPosition())) {
                    dono_.marcado_[static_cast<size_t>(i)] = !dono_.marcado_[static_cast<size_t>(i)];
                    dono_.atualizarContagem();
                    repaint(celula(i));
                    return;
                }
        }
        static constexpr int kCelula = 130, kGap = 12, kTexto = 20;
    private:
        ContainerGridComponent& dono_;
        int colunas_ = 1;
    };

    void atualizarContagem() {
        int on = 0;
        for (bool b : marcado_) on += b ? 1 : 0;
        contagem_.setText(i18n::t("intake.container_grid_contagem").replace("{a}", juce::String(on)).replace("{n}", juce::String(static_cast<int>(ids_.size()))),
                          juce::dontSendNotification);
    }
    void carregarMiniaturas() {
        juce::Component::SafePointer<ContainerGridComponent> seguro(this);
        ProjetoAberto* proj = &projeto_;
        for (size_t i = 0; i < ids_.size(); ++i) {
            const std::string id = ids_[i];
            pool_.addJob([seguro, proj, id, i] {
                if (seguro == nullptr) return;
                juce::Image img;
                if (auto caminho = proj->caminhoMiniaturaPrincipal(id)) img = juce::ImageFileFormat::loadFrom(juce::File(*caminho));
                if (!img.isValid()) return;
                juce::MessageManager::callAsync([seguro, i, img] {
                    if (seguro == nullptr || i >= seguro->miniaturas_.size()) return;
                    seguro->miniaturas_[i] = img;
                    if (seguro->grade_) seguro->grade_->repaint(seguro->grade_->celula(static_cast<int>(i)));
                });
            });
        }
    }

    ProjetoAberto& projeto_;
    std::vector<std::string> ids_;
    std::vector<bool> marcado_;
    std::vector<juce::Image> miniaturas_;
    std::vector<juce::String> nomes_;
    std::function<void(std::vector<std::string>)> aoAplicar_;
    std::function<void()> aoFechar_;
    juce::Label hint_, contagem_;
    juce::Viewport viewport_;
    std::unique_ptr<Grade> grade_;
    juce::TextButton btnAplicar_, btnCancelar_;
    juce::ThreadPool pool_{2};
};

// Botão do card IMPORT: pasta com "+" bem visível. Abre a janela de cards de importação.
class ImportarIconButton : public juce::Button {
public:
    explicit ImportarIconButton(const juce::String& texto) : juce::Button("Import") {
        setButtonText(texto);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool destacado, bool pressionado) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        juce::Colour fundo = tk.acento;
        if (pressionado) fundo = fundo.darker(0.15f);
        else if (destacado) fundo = fundo.brighter(0.12f);
        g.setColour(fundo);
        g.fillRoundedRectangle(r, 12.0f);

        // Mapa "BARRA LATERAL DO INTAKE": botão de 48 px com o ícone ao lado do texto, os dois centralizados.
        const auto fonte = fonteUi(14.0f, juce::Font::bold).withExtraKerningFactor(0.16f);
        const float lado = juce::jmin(r.getHeight() - 16.0f, 22.0f);
        const float wTexto = juce::GlyphArrangement::getStringWidth(fonte, getButtonText());
        float x = r.getCentreX() - (lado + 12.0f + wTexto) * 0.5f;
        desenharIcone(g, IconeUi::PastaMais, juce::Rectangle<float>(x, r.getCentreY() - lado * 0.5f, lado, lado), tk.textoSobreAcento);
        x += lado + 12.0f;
        g.setColour(tk.textoSobreAcento);
        g.setFont(fonte);
        g.drawText(getButtonText(), juce::Rectangle<float>(x, r.getY(), wTexto + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
    }
};

// Cell component for Selection Checkbox
class CheckboxCellComponent : public juce::Component {
public:
    std::function<void(bool)> onToggle;

    CheckboxCellComponent() {
        toggle_.onClick = [this] {
            if (onToggle) onToggle(toggle_.getToggleState());
        };
        addAndMakeVisible(toggle_);
    }

    void setChecked(bool checked) {
        toggle_.setToggleState(checked, juce::dontSendNotification);
    }

    void resized() override {
        toggle_.setBounds(getLocalBounds().reduced(2));
    }

private:
    juce::ToggleButton toggle_;
};

// Cell component for Quick Collection Change button
class ActionCellComponent : public juce::Component {
public:
    std::function<void(juce::Rectangle<int>)> onClick;

    ActionCellComponent() {
        btn_.setButtonText(i18n::t("intake.definir"));
        btn_.onClick = [this] {
            if (onClick) onClick(getScreenBounds());
        };
        addAndMakeVisible(btn_);
    }

    void resized() override {
        btn_.setBounds(getLocalBounds().reduced(4, 3));
    }

private:
    juce::TextButton btn_;
};

class PillButton : public juce::TextButton {
public:
    juce::Colour corPonto = juce::Colours::transparentBlack;
    juce::Colour corBordaCustom = juce::Colours::transparentBlack;
    juce::Colour corTextoCustom = juce::Colours::transparentBlack;
    juce::Colour corFundoCustom = juce::Colours::transparentBlack;
    float tamanhoFonte = 13.0f;
    bool ativo = false;
    bool desenharPin = false;
    bool desenharCelular = false;

    bool principal = false;  // ação principal do painel: pílula grafite, texto branco (no tema claro)
    // Barra lateral (mapa "BARRA LATERAL DO INTAKE"): chip de cantos 7 px com ponto/quadradinho/pino, nome à esquerda e
    // contagem à direita; tipo com 0 arquivos fica cinza, sem número; ativo = grafite.
    bool estiloChip = false;
    bool semQuadradinho = false;  // ACTIONS da barra (mapa): pílula só com o texto, sem o quadradinho da cor antiga

    PillButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void paintChip(juce::Graphics& g, bool destacado, bool pressionado) {
        const auto& tk = tema();
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        juce::Colour fundo = ativo ? tk.acento : tk.painelAlt;
        if (pressionado) fundo = fundo.darker(0.08f);
        else if (destacado) fundo = ativo ? fundo.brighter(0.1f) : fundo.darker(0.03f);
        g.setColour(fundo);
        g.fillRoundedRectangle(r, barra::kRaioChip);
        if (!ativo) {
            g.setColour(tk.borda.withAlpha(0.6f));
            g.drawRoundedRectangle(r, barra::kRaioChip, 1.0f);
        }
        const auto [nome, n] = barra::separarContagem(getButtonText());
        const bool vazio = !ativo && corPonto.isOpaque() && n == "0";
        auto c = getLocalBounds().reduced(barra::kPadChip, 0);
        if (desenharPin) {
            const float p = 11.0f, px = static_cast<float>(c.getX()), py = c.getCentreY() - p * 0.6f;
            juce::Path pino;
            pino.addEllipse(px, py, p, p * 0.85f);
            pino.startNewSubPath(px + 1.0f, py + p * 0.45f);
            pino.lineTo(px + p * 0.5f, py + p + 2.0f);
            pino.lineTo(px + p - 1.0f, py + p * 0.45f);
            pino.closeSubPath();
            g.setColour(corFundoCustom.isOpaque() ? corFundoCustom : tk.acento);
            g.fillPath(pino);
            g.setColour(tk.painelAlt);
            g.fillEllipse(px + p * 0.32f, py + p * 0.2f, p * 0.36f, p * 0.36f);
            c.removeFromLeft(18);
        } else if (corFundoCustom.isOpaque()) {
            g.setColour(isEnabled() ? corFundoCustom : corFundoCustom.withAlpha(0.4f));
            g.fillRoundedRectangle(static_cast<float>(c.getX()), c.getCentreY() - 4.5f, 9.0f, 9.0f, 2.0f);
            c.removeFromLeft(16);
        } else if (corPonto.isOpaque()) {
            g.setColour(vazio ? corPonto.withAlpha(0.45f) : (ativo ? tk.textoSobreAcento : corPonto));
            g.fillEllipse(static_cast<float>(c.getX()), c.getCentreY() - 4.0f, 8.0f, 8.0f);
            c.removeFromLeft(14);
        }
        if (n.isNotEmpty() && !vazio) {
            g.setFont(barra::fonteContagem());
            g.setColour(ativo ? tk.textoSobreAcento.withAlpha(0.85f) : tk.textoTerciario);
            const int w = juce::GlyphArrangement::getStringWidthInt(barra::fonteContagem(), n) + 2;
            g.drawText(n, c.removeFromRight(w), juce::Justification::centredRight, false);
            c.removeFromRight(6);
        }
        g.setFont(barra::fonteChip());
        g.setColour(ativo ? tk.textoSobreAcento : ((vazio || !isEnabled()) ? tk.textoDesativado : tk.textoPrimario));
        g.drawText(nome, c, juce::Justification::centredLeft, true);
    }

    // Tema claro (estilo Mini): pílula neutra; a cor do botão antigo vira um quadradinho antes do texto.
    // Grafite só para a ação principal e para o filtro ativo; vermelho só para quem rejeita/apaga.
    void paintClaro(juce::Graphics& g, bool destacado, bool pressionado) {
        const auto& tk = tema();
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        const float raio = bounds.getHeight() * 0.5f;
        const bool temCor = corFundoCustom.isOpaque();
        const bool vermelho = temCor && corFundoCustom.getSaturation() > 0.5f && (corFundoCustom.getHue() < 0.04f || corFundoCustom.getHue() > 0.96f);
        const bool grafite = principal || ativo;

        juce::Colour fundo = vermelho ? tk.perigo : (grafite ? tk.acento : tk.botaoSecundario);
        juce::Colour texto = (vermelho || grafite) ? tk.textoSobreAcento : tk.textoPrimario;
        if (pressionado) fundo = fundo.darker(0.12f);
        else if (destacado) fundo = fundo.brighter(0.06f);

        if (!isEnabled()) {
            g.setColour(tk.linhaSuave);
            g.drawRoundedRectangle(bounds, raio, 1.0f);
            texto = tk.textoDesativado;
        } else {
            g.setColour(fundo);
            g.fillRoundedRectangle(bounds, raio);
            g.setColour(tk.linhaSuave);
            g.drawRoundedRectangle(bounds, raio, 1.0f);
        }

        auto conteudo = getLocalBounds().reduced(10, 0);
        const bool quadradinho = temCor && !vermelho && !grafite && !semQuadradinho;
        if (desenharPin) {
            const float pSize = 12.0f;
            const float px = static_cast<float>(conteudo.getX() + 2);
            const float py = static_cast<float>(conteudo.getCentreY()) - pSize * 0.6f;
            juce::Path p;
            p.addEllipse(px, py, pSize, pSize * 0.85f);
            p.startNewSubPath(px + 1.0f, py + pSize * 0.45f);
            p.lineTo(px + pSize * 0.5f, py + pSize + 2.5f);
            p.lineTo(px + pSize - 1.0f, py + pSize * 0.45f);
            p.closeSubPath();
            g.setColour(isEnabled() ? corFundoCustom : tk.textoDesativado);
            g.fillPath(p);
            g.setColour(tk.botaoSecundario);
            g.fillEllipse(px + pSize * 0.32f, py + pSize * 0.20f, pSize * 0.36f, pSize * 0.36f);
            conteudo.removeFromLeft(static_cast<int>(pSize + 8.0f));
        } else if (desenharCelular) {
            auto rCel = conteudo.removeFromLeft(18).toFloat().withSizeKeepingCentre(12.0f, 16.0f);
            desenharIcone(g, IconeUi::Celular, rCel, texto);
            conteudo.removeFromLeft(4);
        } else if (quadradinho) {
            const float d = 8.0f;
            g.setColour(isEnabled() ? corFundoCustom : corFundoCustom.withAlpha(0.4f));
            g.fillRoundedRectangle(static_cast<float>(conteudo.getX()), static_cast<float>(conteudo.getCentreY()) - d * 0.5f, d, d, 2.0f);
            conteudo.removeFromLeft(static_cast<int>(d + 6.0f));
        } else if (!corPonto.isTransparent()) {
            const float d = 7.0f;
            g.setColour(isEnabled() ? (ativo ? tk.textoSobreAcento : corPonto) : corPonto.withAlpha(0.40f));
            g.fillEllipse(static_cast<float>(conteudo.getX()), static_cast<float>(conteudo.getCentreY()) - d * 0.5f, d, d);
            conteudo.removeFromLeft(static_cast<int>(d + 6.0f));
        }

        g.setColour(texto);
        g.setFont(fonteUi(tamanhoFonte, juce::Font::bold));
        g.drawText(getButtonText(), conteudo, juce::Justification::centred, true);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        const auto& tk = tema();
        if (estiloChip) {
            paintChip(g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
            return;
        }
        if (tk.fundo.getBrightness() > 0.5f) {
            paintClaro(g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
            return;
        }
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        float cornerRadius = 4.0f;

        // Background
        if (corFundoCustom.isOpaque()) {
            juce::Colour bg = isEnabled() ? corFundoCustom : corFundoCustom.withAlpha(0.35f);
            if (shouldDrawButtonAsDown) bg = bg.darker(0.15f);
            else if (shouldDrawButtonAsHighlighted) bg = bg.brighter(0.15f);
            g.setColour(bg);
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else if (ativo) {
            g.setColour(tk.acento.withAlpha(0.20f));
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else if (shouldDrawButtonAsDown) {
            g.setColour(tk.painelAlt.darker(0.15f));
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else if (shouldDrawButtonAsHighlighted) {
            g.setColour(tk.painelAlt.brighter(0.12f));
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else {
            g.setColour(tk.painelAlt);
            g.fillRoundedRectangle(bounds, cornerRadius);
        }

        // Outline (Fine border)
        juce::Colour borderCol = corBordaCustom.isOpaque() ? corBordaCustom 
                               : (ativo ? tk.acento : tk.borda);
        if (!isEnabled()) {
            borderCol = borderCol.withAlpha(0.40f);
        }
        g.setColour(borderCol);
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

        // Content (Dot + Text)
        auto contentBounds = getLocalBounds().reduced(6, 0);
        if (desenharPin) {
            float pSize = 12.0f;
            float px = static_cast<float>(contentBounds.getX() + 6);
            float py = static_cast<float>(contentBounds.getCentreY()) - pSize * 0.6f;
            juce::Path p;
            p.addEllipse(px, py, pSize, pSize * 0.85f);
            p.startNewSubPath(px + 1.0f, py + pSize * 0.45f);
            p.lineTo(px + pSize * 0.5f, py + pSize + 2.5f);
            p.lineTo(px + pSize - 1.0f, py + pSize * 0.45f);
            p.closeSubPath();
            g.setColour(juce::Colours::white);
            g.fillPath(p);
            g.setColour(corFundoCustom.isOpaque() ? corFundoCustom : tk.acento);
            g.fillEllipse(px + pSize * 0.32f, py + pSize * 0.20f, pSize * 0.36f, pSize * 0.36f);
            contentBounds.removeFromLeft(static_cast<int>(pSize + 10.0f));
        } else if (desenharCelular) {
            auto rCel = contentBounds.removeFromLeft(18).toFloat().withSizeKeepingCentre(12.0f, 16.0f);
            desenharIcone(g, IconeUi::Celular, rCel, corTextoCustom.isOpaque() ? corTextoCustom : tk.textoPrimario);
            contentBounds.removeFromLeft(4);
        } else if (!corPonto.isTransparent()) {
            float dotSize = 7.0f;
            float dotX = static_cast<float>(contentBounds.getX() + 2);
            float dotY = static_cast<float>(contentBounds.getCentreY()) - dotSize * 0.5f;
            g.setColour(isEnabled() ? corPonto : corPonto.withAlpha(0.40f));
            g.fillEllipse(dotX, dotY, dotSize, dotSize);
            contentBounds.removeFromLeft(static_cast<int>(dotSize + 6.0f));
        }

        juce::Colour textCol = corTextoCustom.isOpaque() ? corTextoCustom
                             : (ativo ? tk.textoPrimario : (isEnabled() ? tk.textoPrimario : tk.textoTerciario));
        g.setColour(textCol);
        bool ehNegativo = corFundoCustom.isOpaque();
        g.setFont(juce::Font(juce::FontOptions(tamanhoFonte, (ativo || isMouseOver() || ehNegativo) ? juce::Font::bold : juce::Font::plain)));
        g.drawText(getButtonText(), contentBounds, juce::Justification::centred, true);
    }
};

class ActionButton : public juce::TextButton {
public:
    explicit ActionButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        const auto& tk = tema();
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        float radius = 5.0f;

        juce::Colour bg = tk.painelAlt;
        if (shouldDrawButtonAsDown) bg = tk.painelAlt.darker(0.15f);
        else if (shouldDrawButtonAsHighlighted) bg = tk.painelAlt.brighter(0.10f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, radius);

        g.setColour(tk.borda);
        g.drawRoundedRectangle(bounds, radius, 1.0f);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
        g.drawText(getButtonText(), bounds, juce::Justification::centred, true);
    }
};

class ActionMenuComponent : public juce::Component {
public:
    ActionMenuComponent(int rowNumber, std::function<void(int)> onOpen, std::function<void(int)> onProperties)
        : rowNumber_(rowNumber), onOpen_(std::move(onOpen)), onProperties_(std::move(onProperties)) {
        btnOpen_ = std::make_unique<ActionButton>("Open");
        btnOpen_->onClick = [this] { if (onOpen_) onOpen_(rowNumber_); };
        addAndMakeVisible(*btnOpen_);

        btnMenu_ = std::make_unique<ActionButton>("...");
        btnMenu_->onClick = [this] { if (onProperties_) onProperties_(rowNumber_); };
        addAndMakeVisible(*btnMenu_);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(2, 3);
        btnMenu_->setBounds(area.removeFromRight(26));
        area.removeFromRight(4);
        btnOpen_->setBounds(area);
    }

    void setRow(int row) { rowNumber_ = row; }

private:
    int rowNumber_;
    std::unique_ptr<ActionButton> btnOpen_;
    std::unique_ptr<ActionButton> btnMenu_;
    std::function<void(int)> onOpen_;
    std::function<void(int)> onProperties_;
};

class VerticalDividerComponent : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        g.setColour(tema().borda);
        g.fillRect(0, 0, getWidth(), getHeight());
    }
};

// Etapa 10 — popups de Batch Assignment do INTAKE: Enter = APPLY, Esc fecha
// sem aplicar, campo vazio + Enter não faz nada. Liga em todo TextEditor do
// popup que ainda não tem tratamento próprio (o campo com autocomplete tem:
// o 1º Enter confirma a sugestão destacada, o 2º aplica).
void fecharCallOutDe(juce::Component& c) {
    if (auto* callout = c.findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
}
void ligarTeclasDoPopup(juce::Component& raiz, std::function<void()> aplicar) {
    std::function<void(juce::Component&)> visitar = [&](juce::Component& c) {
        if (auto* te = dynamic_cast<juce::TextEditor*>(&c)) {
            if (!te->onReturnKey)
                te->onReturnKey = [aplicar, te] { if (te->getText().trim().isNotEmpty()) aplicar(); };
            if (!te->onEscapeKey) te->onEscapeKey = [te] { fecharCallOutDe(*te); };
        }
        for (auto* filho : c.getChildren()) visitar(*filho);
    };
    visitar(raiz);
}

class OriginalSourceMediumPopupContent : public juce::Component {
public:
    OriginalSourceMediumPopupContent(const std::string& initialVal, bool isBatch, std::function<void(const std::string&)> onApply)
        : onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", isBatch ? i18n::t("intake.popup_osm_batch") : i18n::t("intake.popup_osm"));
        lblTitle_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitle_);

        editor_ = std::make_unique<OriginalSourceMediumEditorComponent>(/*menuNativo*/ true);  // padrão dos outros do Intake
        editor_->setValueString(initialVal);
        editor_->onChange = [this] {
            if (editor_) {
                editor_->setBounds(0, 0, 360, editor_->getPreferredHeight());
            }
        };

        viewport_ = std::make_unique<juce::Viewport>();
        viewport_->setViewedComponent(editor_.get(), false);
        viewport_->setScrollBarsShown(true, false);
        addAndMakeVisible(*viewport_);

        btnApply_ = std::make_unique<PillButton>(isBatch ? i18n::t("intake.btn_aplicar_selecionados") : i18n::t("intake.btn_aplicar"));
        btnApply_->corTextoCustom = juce::Colour(0xff2a9d8f);
        btnApply_->corBordaCustom = juce::Colour(0xff2a9d8f);
        btnApply_->tamanhoFonte = 13.0f;
        btnApply_->onClick = [this] {
            if (onApply_) onApply_(editor_->getValueString());
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) {
                callout->dismiss();
            }
        };
        addAndMakeVisible(*btnApply_);
        ligarTeclasDoPopup(*editor_, [this] { btnApply_->onClick(); });
        setWantsKeyboardFocus(true);

        setSize(400, 380);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        lblTitle_->setBounds(area.removeFromTop(24));
        area.removeFromTop(8);

        auto bottomArea = area.removeFromBottom(30);
        btnApply_->setBounds(bottomArea.removeFromRight(150));

        area.removeFromBottom(8);
        viewport_->setBounds(area);
        if (editor_) {
            editor_->setBounds(0, 0, area.getWidth() - 10, editor_->getPreferredHeight());
        }
    }

    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress::returnKey && !editor_->getValueString().empty()) { btnApply_->onClick(); return true; }
        if (k == juce::KeyPress::escapeKey) { fecharCallOutDe(*this); return true; }
        return false;
    }

private:
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<OriginalSourceMediumEditorComponent> editor_;
    std::unique_ptr<PillButton> btnApply_;
    std::function<void(const std::string&)> onApply_;
};

// Campo de texto com autocomplete (item 4 da correção "BACKUP e INTAKE"):
// mostra os valores já usados nessa mesma coluna em qualquer lugar do
// projeto (CREATOR/SUBJECT), filtra por prefixo conforme digita, sempre em
// ordem alfabética. Não cria uma base de valores paralela — os valores vêm
// prontos de fora (query direta na tabela item, ver
// IntakeWorkspaceComponent::valoresExistentesParaColuna).
// Sugestões como CHIPS (mesmo visual dos chips do TagChipsEditor), com área de clique generosa: o chip inteiro e até o
// espaço entre chips aceitam o clique. Não pede foco de teclado — clicar nunca tira o foco do campo de texto (era o que
// fazia o ListBox perder o clique na linha pré-selecionada).
class ChipsSugestoes : public juce::Component {
public:
    ChipsSugestoes() {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    std::function<void(int)> aoEscolher;

    // Devolve a altura necessária para a largura dada.
    int definir(const std::vector<juce::String>& valores, int largura) {
        valores_ = valores;
        if (valores_.size() > 24) valores_.resize(24);
        destaque_ = -1;
        hover_ = -1;
        const auto fonte = juce::Font(juce::FontOptions(tema().tamanhoFontePequena));
        constexpr int kAlturaChip = 26, kPad = 12, kGap = 6, kMargem = 6;
        const int util = juce::jmax(60, largura - 2 * kMargem);
        chips_.clear();
        int x = kMargem, y = kMargem;
        for (const auto& v : valores_) {
            int w = juce::jmin(util, kPad * 2 + juce::GlyphArrangement::getStringWidthInt(fonte, v) + 2);
            if (x + w > kMargem + util && x > kMargem) { x = kMargem; y += kAlturaChip + kGap; }
            chips_.push_back({x, y, w, kAlturaChip});
            x += w + kGap;
        }
        const int altura = chips_.empty() ? 0 : y + kAlturaChip + kMargem;
        setSize(largura, altura);
        repaint();
        return altura;
    }
    int destaque() const { return destaque_; }
    juce::Rectangle<int> retanguloDoDestaque() const {
        return (destaque_ >= 0 && destaque_ < static_cast<int>(chips_.size())) ? chips_[static_cast<size_t>(destaque_)] : juce::Rectangle<int>();
    }
    void destacar(int i) {
        destaque_ = (i >= 0 && i < static_cast<int>(valores_.size())) ? i : -1;
        repaint();
    }
    int quantos() const { return static_cast<int>(valores_.size()); }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.setColour(juce::Colours::white);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), tk.raioPequeno);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), tk.raioPequeno, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
            const auto r = chips_[static_cast<size_t>(i)].toFloat();
            const bool ativo = (i == hover_ || i == destaque_);
            g.setColour(ativo ? tk.acento.brighter(0.3f) : tk.acento.withAlpha(0.25f));
            g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
            g.setColour(ativo ? tk.textoSobreAcento : tk.textoPrimario);
            g.drawText(valores_[static_cast<size_t>(i)], chips_[static_cast<size_t>(i)].reduced(12, 0), juce::Justification::centredLeft, true);
        }
    }
    void mouseMove(const juce::MouseEvent& e) override { const int i = maisProximo(e.getPosition()); if (i != hover_) { hover_ = i; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { if (hover_ != -1) { hover_ = -1; repaint(); } }
    void mouseDown(const juce::MouseEvent& e) override {
        const int i = maisProximo(e.getPosition());
        if (i >= 0 && aoEscolher) {
            auto cb = aoEscolher;  // o callback pode destruir este componente
            cb(i);
        }
    }

private:
    int maisProximo(juce::Point<int> p) const {
        int melhor = -1;
        float menor = 1.0e9f;
        for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
            const auto& r = chips_[static_cast<size_t>(i)];
            const float dx = static_cast<float>(juce::jmax(r.getX() - p.x, 0, p.x - r.getRight()));
            const float dy = static_cast<float>(juce::jmax(r.getY() - p.y, 0, p.y - r.getBottom()));
            const float d = dx * dx + dy * dy;
            if (d < menor) { menor = d; melhor = i; }
        }
        return (melhor >= 0 && menor <= 36.0f) ? melhor : -1;  // chip sob o ponto ou a até 6 px
    }
    std::vector<juce::String> valores_;
    std::vector<juce::Rectangle<int>> chips_;
    int hover_ = -1, destaque_ = -1;
};

class AutocompleteAssistedField : public juce::Component {
public:
    AutocompleteAssistedField(std::vector<juce::String> valoresExistentes, const juce::String& placeholder) {
        valores_ = std::move(valoresExistentes);
        std::sort(valores_.begin(), valores_.end(),
                  [](const juce::String& a, const juce::String& b) { return a.compareIgnoreCase(b) < 0; });

        const auto& tk = tema();
        editor_ = std::make_unique<FocusAwareTextEditor>();
        editor_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        editor_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        editor_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        editor_->setColour(juce::TextEditor::outlineColourId, tk.borda);
        editor_->setTextToShowWhenEmpty(placeholder, juce::Colours::grey);
        editor_->onTextChange = [this] { atualizarSugestoes(); };
        editor_->onFocus = [this] { mostrarTodasSugestoes(); };
        editor_->onReturnKey = [this] { tratarEnter(); };
        editor_->onEscapeKey = [this] { fecharCallOutDe(*this); };
        editor_->aoSetaVertical = [this](int delta) { moverDestaque(delta); };
        editor_->onFocusLost = [this] {
            juce::Component::SafePointer<AutocompleteAssistedField> safe(this);
            juce::Timer::callAfterDelay(150, [safe] { if (safe && safe->viewport_) safe->viewport_->setVisible(false); });
        };
        addAndMakeVisible(*editor_);

        lista_ = std::make_unique<ChipsSugestoes>();
        lista_->aoEscolher = [this](int i) { escolher(i); };
        // Os chips ficam num Viewport com rolagem vertical (muitas sugestões): roda do mouse, barra e setas.
        viewport_ = std::make_unique<juce::Viewport>();
        viewport_->setViewedComponent(lista_.get(), false);
        viewport_->setScrollBarsShown(true, false);
        viewport_->setMouseClickGrabsKeyboardFocus(false);  // clicar/rolar nunca tira o foco do campo de texto
        viewport_->setWantsKeyboardFocus(false);
        addChildComponent(*viewport_);  // abre com foco/digitação (addAndMakeVisible a deixava visível e vazia)
        viewport_->toFront(false);
    }

    juce::String getText() const { return editor_->getText().trim(); }
    // Enter com a lista fechada (valor já confirmado) — aplicar.
    std::function<void()> aoEnter;
    void setText(const juce::String& t) { editor_->setText(t, false); }
    // Opcional: chamado quando o operador escolhe uma sugestão da lista.
    std::function<void(const juce::String&)> aoEscolher;
    // Altura com a lista de sugestões aberta/fechada — pra quem quiser que
    // a lista flutue por cima do que vem embaixo em vez de reservar espaço.
    void abrirSugestoes() { mostrarTodasSugestoes(); }
    int alturaDesejada() const {
        return 26 + ((viewport_ && viewport_->isVisible() && !sugestoes_.empty()) ? 2 + alturaVisivel_ : 0);
    }

    void resized() override {
        auto area = getLocalBounds();
        editor_->setBounds(area.removeFromTop(26));
        area.removeFromTop(2);
        if (sugestoes_.empty()) { viewport_->setBounds(area.removeFromTop(0)); return; }
        constexpr int kAlturaMaxima = 150;  // acima disso rola
        const int largura = area.getWidth() - viewport_->getScrollBarThickness();
        alturaLista_ = lista_->definir(sugestoes_, juce::jmax(60, largura));  // quebra em linhas conforme a largura
        alturaVisivel_ = juce::jmin(alturaLista_, kAlturaMaxima);
        viewport_->setBounds(area.removeFromTop(alturaVisivel_));
        lista_->destacar(destaqueAtual_);
    }

private:

    void escolher(int row) {
        if (row < 0 || row >= static_cast<int>(sugestoes_.size())) return;
        juce::String escolhido = sugestoes_[static_cast<size_t>(row)];
        editor_->setText(escolhido, false);
        viewport_->setVisible(false);
        if (aoEscolher) aoEscolher(escolhido);
    }

    // 1º Enter com sugestão destacada: confirma a sugestão (nunca aplica um
    // valor parcial por engano). Lista fechada: Enter aplica. Vazio: nada.
    void tratarEnter() {
        const int destacada = viewport_->isVisible() ? lista_->destaque() : -1;
        if (destacada >= 0 && destacada < static_cast<int>(sugestoes_.size())) {
            escolher(destacada);
            return;
        }
        if (getText().isEmpty()) return;
        viewport_->setVisible(false);
        if (aoEnter) aoEnter();
    }

    void moverDestaque(int delta) {
        if (!viewport_->isVisible() || sugestoes_.empty()) return;
        const int n = static_cast<int>(sugestoes_.size());
        destaqueAtual_ = juce::jlimit(0, n - 1, lista_->destaque() + delta);
        lista_->destacar(destaqueAtual_);
        const auto r = lista_->retanguloDoDestaque();
        if (!r.isEmpty()) {  // mantém o chip destacado à vista ao navegar pelas setas
            auto pos = viewport_->getViewPosition();
            if (r.getY() < pos.y) pos.y = r.getY() - 6;
            else if (r.getBottom() > pos.y + viewport_->getViewHeight()) pos.y = r.getBottom() - viewport_->getViewHeight() + 6;
            viewport_->setViewPosition(pos);
        }
    }

    void mostrarTodasSugestoes() {
        sugestoes_ = valores_;
        destaqueAtual_ = -1;
        viewport_->setVisible(!sugestoes_.empty());
        resized();
        if (auto* parent = getParentComponent()) parent->resized();
    }

    void atualizarSugestoes() {
        juce::String q = editor_->getText().trim();
        if (q.isEmpty()) { mostrarTodasSugestoes(); return; }
        sugestoes_.clear();
        for (auto& v : valores_)
            if (v.startsWithIgnoreCase(q)) sugestoes_.push_back(v);
        // Digitando: a 1ª sugestão fica destacada (é ela que o Enter confirma).
        destaqueAtual_ = sugestoes_.empty() ? -1 : 0;
        viewport_->setViewPosition(0, 0);  // lista nova: volta ao topo
        viewport_->setVisible(!sugestoes_.empty());
        resized();
        if (auto* parent = getParentComponent()) parent->resized();
    }

    class FocusAwareTextEditor : public juce::TextEditor {
    public:
        std::function<void()> onFocus;
        std::function<void(int)> aoSetaVertical;
        void focusGained(FocusChangeType) override { if (onFocus) onFocus(); }
        bool keyPressed(const juce::KeyPress& k) override {
            if (aoSetaVertical && (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)) {
                aoSetaVertical(k == juce::KeyPress::downKey ? 1 : -1);
                return true;
            }
            return juce::TextEditor::keyPressed(k);
        }
    };

    std::vector<juce::String> valores_;
    std::vector<juce::String> sugestoes_;
    std::unique_ptr<FocusAwareTextEditor> editor_;
    std::unique_ptr<ChipsSugestoes> lista_;
    std::unique_ptr<juce::Viewport> viewport_;
    int alturaLista_ = 0;
    int alturaVisivel_ = 0;
    int destaqueAtual_ = -1;
};

class GeoLocationPopupContent : public juce::Component {
public:
    GeoLocationPopupContent(matriz::db::Database& registro, bool isBatch,
                             std::function<void(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&)> onApply)
        : registro_(registro), onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", isBatch ? i18n::t("intake.popup_geo_batch") : i18n::t("intake.popup_geo"));
        lblTitle_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitle_);

        // Favoritos (item 3 da correção "BACKUP e INTAKE"): mesmo
        // GeoFavoritosRepository que a ficha usa — sem lista paralela.
        btnFavoritos_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x96\xbe Favorites"));
        btnFavoritos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnFavoritos_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnFavoritos_->onClick = [this] { mostrarMenuFavoritos(); };
        addAndMakeVisible(*btnFavoritos_);

        auto makeField = [this, &tk](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed,
                                     const juce::String& labelText, const juce::String& placeholder) {
            lbl = std::make_unique<juce::Label>("", labelText);
            lbl->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            lbl->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lbl);

            ed = std::make_unique<juce::TextEditor>();
            ed->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            ed->setColour(juce::TextEditor::textColourId, juce::Colours::black);
            ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
            ed->setColour(juce::TextEditor::outlineColourId, tk.borda);
            ed->setTextToShowWhenEmpty(placeholder, juce::Colours::grey);
            addAndMakeVisible(*ed);
        };

        // Autocomplete com as localizações já gravadas no projeto: escolher
        // uma preenche os 5 campos de uma vez.
        carregarLocalizacoesUsadas();
        std::vector<juce::String> rotulos;
        for (const auto& loc : localizacoesUsadas_) rotulos.push_back(loc.rotulo);
        lblUsadas_ = std::make_unique<juce::Label>("", "Used locations in this project");
        lblUsadas_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        lblUsadas_->setColour(juce::Label::textColourId, tk.textoSecundario);
        addAndMakeVisible(*lblUsadas_);
        campoUsadas_ = std::make_unique<AutocompleteAssistedField>(
            std::move(rotulos), localizacoesUsadas_.empty() ? "No locations used yet" : "Type a city, state, country...");
        campoUsadas_->aoEscolher = [this](const juce::String& rotulo) { preencherComLocalizacaoUsada(rotulo); };
        addAndMakeVisible(*campoUsadas_);

        makeField(lblCoords_, edCoords_, "GPS Coordinates (Lat, Lng)", "e.g. -16.4435, -39.0643");
        makeField(lblAddress_, edAddress_, "Formatted Address", "e.g. Av. Paulista, 1000");
        makeField(lblCity_, edCity_, "City", "e.g. Porto Seguro");
        makeField(lblState_, edState_, "State / Province", "e.g. Bahia");
        makeField(lblCountry_, edCountry_, "Country", "e.g. Brazil");

        btnApply_ = std::make_unique<PillButton>(isBatch ? i18n::t("intake.btn_aplicar_selecionados") : i18n::t("intake.btn_aplicar"));
        btnApply_->corTextoCustom = juce::Colour(0xff2a9d8f);
        btnApply_->corBordaCustom = juce::Colour(0xff2a9d8f);
        btnApply_->tamanhoFonte = 13.0f;
        btnApply_->onClick = [this] {
            if (onApply_) {
                onApply_(edCoords_->getText().toStdString(),
                         edAddress_->getText().toStdString(),
                         edCity_->getText().toStdString(),
                         edState_->getText().toStdString(),
                         edCountry_->getText().toStdString());
            }
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) {
                callout->dismiss();
            }
        };
        addAndMakeVisible(*btnApply_);
        campoUsadas_->aoEnter = [this] { btnApply_->onClick(); };
        ligarTeclasDoPopup(*this, [this] { btnApply_->onClick(); });

        btnSalvarFavorito_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x98\x85 Add to Favorites"));
        btnSalvarFavorito_->setTooltip("Save this place to the project favorites list");
        btnSalvarFavorito_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnSalvarFavorito_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnSalvarFavorito_->onClick = [this] { salvarComoFavorito(); };
        addAndMakeVisible(*btnSalvarFavorito_);

        // GET (local atual pelo IP) e OPEN (Google Maps), como em todas as fichas de GEO LOCATION.
        btnGet_ = std::make_unique<juce::TextButton>(i18n::t("imp.ficha.geo.get"));
        btnGet_->setTooltip(i18n::t("imp.ficha.geo.get.dica"));
        btnGet_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnGet_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnGet_->onClick = [this] {
            juce::Component::SafePointer<juce::Component> seguro(this);
            matriz::ui::geoacoes::obterLocal({ edCoords_->getText(), edAddress_->getText(), edCity_->getText(), edState_->getText(), edCountry_->getText() },
                                             btnGet_.get(), [this, seguro](matriz::ui::geoacoes::Local l) {
                if (seguro == nullptr) return;
                if (l.coords.isNotEmpty()) edCoords_->setText(l.coords);
                if (l.endereco.isNotEmpty()) edAddress_->setText(l.endereco);
                if (l.cidade.isNotEmpty()) edCity_->setText(l.cidade);
                if (l.estado.isNotEmpty()) edState_->setText(l.estado);
                if (l.pais.isNotEmpty()) edCountry_->setText(l.pais);
            });
        };
        addAndMakeVisible(*btnGet_);
        btnOpen_ = std::make_unique<juce::TextButton>(i18n::t("imp.ficha.geo.open"));
        btnOpen_->setTooltip(i18n::t("imp.ficha.geo.open.dica"));
        btnOpen_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnOpen_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
        btnOpen_->onClick = [this] {
            matriz::ui::geoacoes::abrirNoGoogleMaps({ edCoords_->getText(), edAddress_->getText(), edCity_->getText(), edState_->getText(), edCountry_->getText() });
        };
        addAndMakeVisible(*btnOpen_);

        setSize(380, 482);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        auto topRow = area.removeFromTop(24);
        lblTitle_->setBounds(topRow.removeFromLeft(topRow.getWidth() - 110));
        btnFavoritos_->setBounds(topRow);
        area.removeFromTop(6);

        // O campo ocupa uma linha; aberta, a lista de sugestões flutua por
        // cima dos campos de baixo (o campo chama este resized ao abrir/fechar).
        lblUsadas_->setBounds(area.removeFromTop(16));
        area.removeFromTop(2);
        auto linhaUsadas = area.removeFromTop(26);
        campoUsadas_->setBounds(linhaUsadas.withHeight(campoUsadas_->alturaDesejada()));
        campoUsadas_->toFront(false);
        area.removeFromTop(6);

        auto layoutSubfield = [&](std::unique_ptr<juce::Label>& lbl, std::unique_ptr<juce::TextEditor>& ed) {
            if (lbl) { lbl->setBounds(area.removeFromTop(16)); area.removeFromTop(2); }
            if (ed) { ed->setBounds(area.removeFromTop(26)); area.removeFromTop(6); }
        };

        layoutSubfield(lblCoords_, edCoords_);
        layoutSubfield(lblAddress_, edAddress_);
        layoutSubfield(lblCity_, edCity_);
        layoutSubfield(lblState_, edState_);
        layoutSubfield(lblCountry_, edCountry_);

        {
            auto linhaGet = area.removeFromTop(26);
            btnGet_->setBounds(linhaGet.removeFromLeft((linhaGet.getWidth() - 8) / 2));
            linhaGet.removeFromLeft(8);
            btnOpen_->setBounds(linhaGet);
        }
        area.removeFromTop(4);
        auto rodape = area.removeFromBottom(28);
        btnApply_->setBounds(rodape.removeFromRight(150));
        rodape.removeFromRight(8);
        btnSalvarFavorito_->setBounds(rodape.removeFromLeft(150));
    }

    // Só pra --selftest-lote.
    std::vector<juce::String> rotulosUsadosParaTeste() const {
        std::vector<juce::String> out;
        for (const auto& l : localizacoesUsadas_) out.push_back(l.rotulo);
        return out;
    }
    void abrirSugestoesParaTeste() { campoUsadas_->abrirSugestoes(); }
    void escolherUsadaParaTeste(const juce::String& rotulo) { if (campoUsadas_->aoEscolher) campoUsadas_->aoEscolher(rotulo); }
    juce::String campoParaTeste(int i) const {
        const juce::TextEditor* eds[] = {edCoords_.get(), edAddress_.get(), edCity_.get(), edState_.get(), edCountry_.get()};
        return eds[i]->getText();
    }
    void salvarFavoritoParaTeste() { salvarComoFavorito(); }

private:
    struct LocalizacaoUsada {
        juce::String rotulo;  // "Cidade, Estado, País — endereço" (o que o autocomplete mostra)
        juce::String coords, endereco, cidade, estado, pais;
    };

    void carregarLocalizacoesUsadas() {
        // Combinações distintas já gravadas, as mais usadas primeiro.
        try {
            auto st = registro_.prepare(
                "SELECT latitude, longitude, COALESCE(formatted_address,''), COALESCE(city,''), "
                "COALESCE(state_province,''), COALESCE(country,''), COUNT(*) AS n FROM asset_geolocation "
                "GROUP BY 1, 2, 3, 4, 5, 6 ORDER BY n DESC LIMIT 200");
            std::set<juce::String> vistos;
            while (st.step()) {
                LocalizacaoUsada loc;
                if (!st.columnIsNull(0) && !st.columnIsNull(1)) {
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(6) << st.columnReal(0) << ", " << st.columnReal(1);
                    loc.coords = ss.str();
                }
                loc.endereco = juce::String::fromUTF8(st.columnText(2).c_str());
                loc.cidade = juce::String::fromUTF8(st.columnText(3).c_str());
                loc.estado = juce::String::fromUTF8(st.columnText(4).c_str());
                loc.pais = juce::String::fromUTF8(st.columnText(5).c_str());
                juce::StringArray partes;
                for (auto* p : {&loc.cidade, &loc.estado, &loc.pais}) if (p->isNotEmpty()) partes.add(*p);
                loc.rotulo = partes.joinIntoString(", ");
                if (loc.endereco.isNotEmpty())
                    loc.rotulo = loc.rotulo.isEmpty() ? loc.endereco : loc.rotulo + juce::String::fromUTF8(" \xe2\x80\x94 ") + loc.endereco;
                if (loc.rotulo.isEmpty()) loc.rotulo = loc.coords;
                if (loc.rotulo.isEmpty() || !vistos.insert(loc.rotulo).second) continue;
                localizacoesUsadas_.push_back(std::move(loc));
            }
        } catch (...) {}
    }

    void preencherComLocalizacaoUsada(const juce::String& rotulo) {
        for (const auto& loc : localizacoesUsadas_) {
            if (loc.rotulo != rotulo) continue;
            edCoords_->setText(loc.coords);
            edAddress_->setText(loc.endereco);
            edCity_->setText(loc.cidade);
            edState_->setText(loc.estado);
            edCountry_->setText(loc.pais);
            return;
        }
    }

    void salvarComoFavorito() {
        // Copia os campos ANTES do diálogo: o CallOutBox pode se fechar
        // quando o diálogo pega o foco, e o callback não pode tocar em this.
        matriz::analytics::GeoFavorito fav;
        std::string coords = edCoords_->getText().toStdString();
        auto virgula = coords.find(',');
        if (virgula != std::string::npos) {
            try {
                fav.latitude = std::stod(coords.substr(0, virgula));
                fav.longitude = std::stod(coords.substr(virgula + 1));
            } catch (...) {}
        }
        auto opcional = [](const juce::TextEditor& ed) -> std::optional<std::string> {
            auto t = ed.getText().trim();
            return t.isEmpty() ? std::nullopt : std::optional<std::string>(t.toStdString());
        };
        fav.formattedAddress = opcional(*edAddress_);
        fav.city = opcional(*edCity_);
        fav.stateProvince = opcional(*edState_);
        fav.country = opcional(*edCountry_);
        if (!fav.latitude && !fav.formattedAddress && !fav.city && !fav.stateProvince && !fav.country) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Favorites",
                                                    "Fill in at least one location field before saving a favorite.");
            return;
        }

        juce::String sugestao = fav.city ? juce::String(*fav.city)
                              : (fav.formattedAddress ? juce::String(*fav.formattedAddress).substring(0, 40) : juce::String());
        auto* dlg = new juce::AlertWindow("Save Favorite Place", "Name for this place:", juce::MessageBoxIconType::NoIcon);
        dlg->addTextEditor("nome", sugestao, "");
        dlg->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dlg->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        matriz::db::Database* registro = &registro_;
        dlg->enterModalState(true, juce::ModalCallbackFunction::create([dlg, fav, registro](int resultado) mutable {
            if (resultado == 1) {
                juce::String nome = dlg->getTextEditorContents("nome").trim();
                if (nome.isNotEmpty()) {
                    fav.nome = nome.toStdString();
                    try { matriz::analytics::GeoFavoritosRepository::salvar(*registro, fav); } catch (...) {}
                }
            }
        }), true);  // deleteWhenDismissed
    }

    void mostrarMenuFavoritos() {
        auto favs = matriz::analytics::GeoFavoritosRepository::listar(registro_);
        if (favs.empty()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon, "Favorites",
                juce::String::fromUTF8("No favorite places saved yet. Fill in a location and use \xe2\x98\x85 Add to Favorites."));
            return;
        }
        juce::PopupMenu menu;
        for (int i = 0; i < static_cast<int>(favs.size()); ++i) {
            juce::String label = juce::String(favs[static_cast<size_t>(i)].nome);
            if (favs[static_cast<size_t>(i)].city)
                label += juce::String::fromUTF8(" \xe2\x80\x93 ") + juce::String(*favs[static_cast<size_t>(i)].city);
            menu.addItem(i + 1, label);
        }
        geofav::adicionarGerenciamento(menu, favs);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(btnFavoritos_.get()),
            [this, favs](int result) {
                if (geofav::tratarResultado(result, favs, registro_)) return;
                if (result < 1 || result > static_cast<int>(favs.size())) return;
                const auto& fav = favs[static_cast<size_t>(result - 1)];
                if (edCoords_ && fav.latitude && fav.longitude) {
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(6) << *fav.latitude << ", " << *fav.longitude;
                    edCoords_->setText(ss.str());
                }
                if (edAddress_ && fav.formattedAddress) edAddress_->setText(juce::String(*fav.formattedAddress));
                if (edCity_ && fav.city) edCity_->setText(juce::String(*fav.city));
                if (edState_ && fav.stateProvince) edState_->setText(juce::String(*fav.stateProvince));
                if (edCountry_ && fav.country) edCountry_->setText(juce::String(*fav.country));
            });
    }

    matriz::db::Database& registro_;
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<juce::TextButton> btnFavoritos_;
    std::unique_ptr<juce::TextButton> btnGet_, btnOpen_;
    std::unique_ptr<juce::Label> lblCoords_;
    std::unique_ptr<juce::TextEditor> edCoords_;
    std::unique_ptr<juce::Label> lblAddress_;
    std::unique_ptr<juce::TextEditor> edAddress_;
    std::unique_ptr<juce::Label> lblCity_;
    std::unique_ptr<juce::TextEditor> edCity_;
    std::unique_ptr<juce::Label> lblState_;
    std::unique_ptr<juce::TextEditor> edState_;
    std::unique_ptr<juce::Label> lblCountry_;
    std::unique_ptr<juce::TextEditor> edCountry_;
    std::unique_ptr<PillButton> btnApply_;
    std::unique_ptr<juce::TextButton> btnSalvarFavorito_;
    std::unique_ptr<juce::Label> lblUsadas_;
    std::unique_ptr<AutocompleteAssistedField> campoUsadas_;
    std::vector<LocalizacaoUsada> localizacoesUsadas_;
    std::function<void(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&)> onApply_;
};

class AutocompleteLotePopupContent : public juce::Component {
public:
    AutocompleteLotePopupContent(const juce::String& title, const juce::String& placeholder,
                                  std::vector<juce::String> valoresExistentes, juce::Colour corDestaque,
                                  std::function<void(const juce::String&)> onApply)
        : onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", title);
        lblTitle_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitle_);

        campo_ = std::make_unique<AutocompleteAssistedField>(std::move(valoresExistentes), placeholder);
        addAndMakeVisible(*campo_);

        btnApply_ = std::make_unique<PillButton>(i18n::t("intake.btn_aplicar_selecionados"));
        btnApply_->corTextoCustom = corDestaque;
        btnApply_->corBordaCustom = corDestaque;
        btnApply_->tamanhoFonte = 13.0f;
        btnApply_->onClick = [this] {
            if (onApply_) onApply_(campo_->getText());
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
        };
        addAndMakeVisible(*btnApply_);
        campo_->aoEnter = [this] { btnApply_->onClick(); };

        setSize(340, 240);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        lblTitle_->setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
        campo_->setBounds(area.removeFromTop(140));
        area.removeFromTop(8);
        btnApply_->setBounds(area.removeFromBottom(30).removeFromRight(170));
    }

private:
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<AutocompleteAssistedField> campo_;
    std::unique_ptr<PillButton> btnApply_;
    std::function<void(const juce::String&)> onApply_;
};

class ContentLotePopupContent : public juce::Component {
    friend class matriz::ui::IntakeWorkspaceComponent;  // autoteste da etapa 10
public:
    ContentLotePopupContent(juce::Colour corDestaque, std::function<void(const juce::String&)> onApply)
        : onApply_(std::move(onApply)) {
        const auto& tk = tema();

        lblTitle_ = std::make_unique<juce::Label>("", "Set CONTENT for Selected");
        lblTitle_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        lblTitle_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitle_);

        combo_ = std::make_unique<juce::ComboBox>();
        combo_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        combo_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        combo_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        combo_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
        IntakeWorkspaceComponent::popularComboColecoes(*combo_, true);
        combo_->setWantsKeyboardFocus(false);  // Enter aplica (não reabre o menu)
        // O combo abre em "None": Enter só aplica depois de uma escolha — senão
        // um Enter por engano limparia o CONTENT de todos os selecionados.
        combo_->onChange = [this] { escolheu_ = true; };
        addAndMakeVisible(*combo_);
        setWantsKeyboardFocus(true);

        btnApply_ = std::make_unique<PillButton>(i18n::t("intake.btn_aplicar_selecionados"));
        btnApply_->corTextoCustom = corDestaque;
        btnApply_->corBordaCustom = corDestaque;
        btnApply_->tamanhoFonte = 13.0f;
        btnApply_->onClick = [this] {
            juce::String chosen = combo_->getText();
            if (onApply_) {
                if (combo_->getSelectedId() == 1 || chosen.equalsIgnoreCase("None")) onApply_("");
                else onApply_(chosen);
            }
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
        };
        addAndMakeVisible(*btnApply_);

        setSize(320, 150);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        lblTitle_->setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
        combo_->setBounds(area.removeFromTop(28));
        area.removeFromTop(10);
        btnApply_->setBounds(area.removeFromBottom(30).removeFromRight(170));
    }

    void parentHierarchyChanged() override {
        juce::Component::SafePointer<ContentLotePopupContent> safe(this);
        juce::MessageManager::callAsync([safe] { if (safe && safe->isShowing()) safe->grabKeyboardFocus(); });
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress::returnKey) {
            if (escolheu_ && combo_->getSelectedId() > 0) btnApply_->onClick();
            return true;
        }
        if (k == juce::KeyPress::escapeKey) { fecharCallOutDe(*this); return true; }
        return false;
    }

private:
    bool escolheu_ = false;
    std::unique_ptr<juce::Label> lblTitle_;
    std::unique_ptr<juce::ComboBox> combo_;
    std::unique_ptr<PillButton> btnApply_;
    std::function<void(const juce::String&)> onApply_;
};

} // namespace


class IntakeWorkspaceComponent::ThumbnailsGridComponent : public juce::Component {
public:
    explicit ThumbnailsGridComponent(IntakeWorkspaceComponent& owner) : owner_(owner) {
        setWantsKeyboardFocus(false);
    }

    ~ThumbnailsGridComponent() override {
        poolThumbnails_.removeAllJobs(true, 2000);
    }

    void recalcularLayout() {
        int w = owner_.gridViewport_ ? owner_.gridViewport_->getViewWidth() : getWidth();
        if (w <= 0) w = owner_.getWidth();
        if (w <= 0) return;

        int total = static_cast<int>(owner_.indicesFiltrados_.size());
        if (total == 0) {
            setSize(w, 200);
            repaint();
            return;
        }

        int padding = 14;
        // Slider de tamanho (mesma escala da aba METADATA: 80..320 px de largura mínima do card).
        int cardMinW = static_cast<int>(80.0 + owner_.tamanhoMiniaturas_ * (320.0 - 80.0));
        int cardH = 0;  // proporcional: miniatura (cardH - kAlturaTextoCard) + área de texto fixa
        int colunas = std::max(1, (w - padding) / (cardMinW + padding));
        int cardW = (w - padding * (colunas + 1)) / colunas;
        cardH = std::max(150, static_cast<int>(cardW * 1.1));
        int linhas = (total + colunas - 1) / colunas;
        int totalH = padding + linhas * (cardH + padding);

        colunas_ = colunas;
        cardW_ = cardW;
        cardH_ = cardH;
        padding_ = padding;

        setSize(w, std::max(totalH, 200));
        repaint();
    }

    int colunas() const { return colunas_; }

    juce::Rectangle<int> boundsDoCard(int indice) const {
        if (colunas_ <= 0) return {};
        int col = indice % colunas_;
        int row = indice / colunas_;
        int x = padding_ + col * (cardW_ + padding_);
        int y = padding_ + row * (cardH_ + padding_);
        return { x, y, cardW_, cardH_ };
    }

    int indiceNaPosicao(juce::Point<int> pos) const {
        int total = static_cast<int>(owner_.indicesFiltrados_.size());
        for (int i = 0; i < total; ++i) {
            if (boundsDoCard(i).contains(pos)) return i;
        }
        return -1;
    }

    // "Sem miniatura" só vale até o próximo snapshot: o card entra na grade
    // durante o ingest, ANTES de a miniatura ser gerada — sem esquecer isto,
    // o JPG recém-ingerido ficava com o placeholder pra sempre. Mesmo
    // critério de MosaicoComponent::recarregar (semMiniatura_.clear()).
    void esquecerSemMiniatura() {
        const juce::ScopedLock sl(lock_);
        noThumbnail_.clear();
    }

    // Capa de container (vinil, fita) chega depois do item: a miniatura já guardada (forma de onda da 1ª faixa) é refeita.
    void esquecerMiniaturaDoItem(const std::string& itemId) {
        const juce::ScopedLock sl(lock_);
        cache_.erase(itemId);
        noThumbnail_.erase(itemId);
    }

    bool temMiniaturaEmCache(const std::string& itemId) const {
        const juce::ScopedLock sl(lock_);
        return cache_.count(itemId) > 0;
    }

    void pedirMiniatura(const std::string& itemId, const juce::String& capaContainer,
                        const juce::String& caminhoMiniatura, const juce::String& caminhoOrigem,
                        const juce::String& extensao) {
        {
            const juce::ScopedLock sl(lock_);
            if (loading_.count(itemId) || cache_.count(itemId) || noThumbnail_.count(itemId)) return;
            loading_[itemId] = true;
        }

        juce::Component::SafePointer<ThumbnailsGridComponent> safeThis(this);
        poolThumbnails_.addJob([safeThis, itemId, capaContainer, caminhoMiniatura, caminhoOrigem, extensao]() mutable {
            // Container com capa própria (vinil, fita): a capa escolhida na ficha vale como miniatura dele.
            std::optional<juce::String> caminho;
            if (capaContainer.isNotEmpty() && juce::File(capaContainer).existsAsFile()) caminho = capaContainer;
            if (!caminho && caminhoMiniatura.isNotEmpty()) caminho = caminhoMiniatura;
            juce::Image img;
            if (caminho) img = juce::ImageFileFormat::loadFrom(juce::File(*caminho));
            bool temCaminho = caminho.has_value();

            // Imports antigos e arquivos que chegaram como placeholder de nuvem
            // podem não ter uma linha em indice.sqlite. O Intake ainda precisa
            // mostrar a foto: carrega somente o original visível, no pool, e o
            // mantém no cache da grade sem consultar o banco por cartão.
            if (!img.isValid() && caminhoOrigem.isNotEmpty()) {
                const juce::File origem(caminhoOrigem);
                if (origem.existsAsFile()) {
                    img = juce::ImageFileFormat::loadFrom(origem);
                    if (img.isValid() && (img.getWidth() > 512 || img.getHeight() > 512)) {
                        const double escala = juce::jmin(512.0 / img.getWidth(), 512.0 / img.getHeight());
                        img = img.rescaled(juce::jmax(1, juce::roundToInt(img.getWidth() * escala)),
                                           juce::jmax(1, juce::roundToInt(img.getHeight() * escala)),
                                           juce::Graphics::ResamplingQuality::mediumResamplingQuality);
                    }
                    if (img.isValid()) temCaminho = true;
                }
            }

            if (!img.isValid()) {
                const auto ext = extensao.toLowerCase().replace(".", "");
                juce::String logoFile = matriz::ingest::obterLogoParaExtensao(ext);
                if (logoFile.isEmpty() && ext == "pd") logoFile = "puredata.png";
                if (logoFile.isNotEmpty()) {
                    img = matriz::ui::carregarLogoDeTipo(logoFile);
                    if (img.isValid()) temCaminho = true;
                }
            }

            juce::MessageManager::callAsync([safeThis, itemId, img, temCaminho]() mutable {
                if (!safeThis) return;
                const juce::ScopedLock sl(safeThis->lock_);
                safeThis->loading_.erase(itemId);
                if (img.isValid()) {
                    safeThis->cache_[itemId] = img;
                } else if (!temCaminho) {
                    safeThis->noThumbnail_[itemId] = true;
                }
                safeThis->repaint();
            });
        });
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.fundo);

        int total = static_cast<int>(owner_.indicesFiltrados_.size());
        if (total == 0) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
            g.drawText(i18n::t("intake.sem_itens_filtro"), getLocalBounds(), juce::Justification::centred);
            return;
        }

        static const juce::Font font12Bold(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold));
        static const juce::Font font11Normal(juce::FontOptions(tema().tamanhoFontePequena));
        static const juce::Font font10Bold(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold));
        static const juce::Font font9Bold(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold));

        static const juce::Path checkmarkPath = []() {
            juce::Path p;
            p.startNewSubPath(0.24f, 0.50f);
            p.lineTo(0.42f, 0.70f);
            p.lineTo(0.78f, 0.30f);
            return p;
        }();

        auto clip = g.getClipBounds();

        for (int i = 0; i < total; ++i) {
            auto bounds = boundsDoCard(i);
            if (!bounds.intersects(clip)) continue;

            int realIdx = owner_.indicesFiltrados_[static_cast<size_t>(i)];
            if (realIdx < 0 || realIdx >= static_cast<int>(owner_.todosItens_.size())) continue;
            const auto& item = owner_.todosItens_[static_cast<size_t>(realIdx)];

            bool selecionado = item.selecionado;
            bool hover = (hoverIndex_ == i);
            juce::Colour corCat = corParaCategoria(item.categoria);

            // Card Background
            g.setColour(tk.painel);
            g.fillRoundedRectangle(bounds.toFloat(), tk.raioMedio);

            // Thumbnail Area (~115px height)
            auto thumbArea = bounds.reduced(6, 6).removeFromTop(juce::jmax(60, cardH_ - 95));
            {
                juce::Image img;
                {
                    const juce::ScopedLock sl(lock_);
                    auto it = cache_.find(item.id);
                    if (it != cache_.end()) img = it->second;
                }

                if (ehContainerGrafico(item.containerOrigem)) {
                    // Container de verdade: ícone do tipo (DDP, produção, vinil, fita) ou a capa escolhida na ficha.
                    g.setColour(tk.painelAlt);
                    g.fillRoundedRectangle(thumbArea.toFloat(), tk.raioPequeno);
                    desenharContainerGrafico(g, thumbArea.toFloat(), item.containerOrigem, item.containerIcone, item.containerCapaCaminho);
                } else if (img.isValid()) {
                    g.setColour(tk.painelAlt);
                    g.fillRoundedRectangle(thumbArea.toFloat(), tk.raioPequeno);
                    g.drawImage(img, thumbArea.toFloat(), juce::RectanglePlacement::centred);
                } else {
                    pedirMiniatura(item.id, item.containerCapaCaminho, item.caminhoMiniatura,
                                   item.caminhoOrigem, item.extensao);
                    g.setColour(corCat.withAlpha(0.12f));
                    g.fillRoundedRectangle(thumbArea.toFloat(), tk.raioPequeno);
                    g.setColour(corCat);
                    g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
                    g.drawText(item.extensao.isNotEmpty() ? item.extensao : item.categoria.substring(0, 3).toUpperCase(),
                               thumbArea, juce::Justification::centred);
                }
            }

            // Thumbnail Border
            g.setColour(corCat.withAlpha(0.45f));
            g.drawRoundedRectangle(thumbArea.toFloat(), tk.raioPequeno, 1.0f);

            // Top-Left Checkbox on thumbnail
            juce::Rectangle<int> checkRect(thumbArea.getX() + 6, thumbArea.getY() + 6, 18, 18);
            if (selecionado) {
                g.setColour(tk.acento);
                g.fillRoundedRectangle(checkRect.toFloat(), 3.0f);
                g.setColour(tk.textoSobreAcento);
                g.strokePath(checkmarkPath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                             juce::AffineTransform::scale(18.0f).translated(static_cast<float>(checkRect.getX()), static_cast<float>(checkRect.getY())));
            } else {
                g.setColour(juce::Colour(0xd0000000));
                g.fillRoundedRectangle(checkRect.toFloat(), 3.0f);
                g.setColour(tk.borda);
                g.drawRoundedRectangle(checkRect.toFloat(), 3.0f, 1.2f);
            }

            // Top-Left Type / Category Badge (adjacent to checkbox)
            // Container PROJECT: a tag PROJECT ocupa o lugar do selo de categoria.
            const bool ehProjeto = ehOrigemProject(item.containerOrigem);
            juce::String catBadgeText = ehProjeto ? i18n::t("projeto.tag") : item.categoria.toUpperCase();
            int catBadgeW = juce::jmax(36, static_cast<int>(catBadgeText.length()) * 7 + 8);
            juce::Rectangle<int> catBadgeRect(thumbArea.getX() + 28, thumbArea.getY() + 6, catBadgeW, 18);
            g.setColour((ehProjeto ? juce::Colour(0xffb8860b) : corCat).withAlpha(0.85f));
            g.fillRoundedRectangle(catBadgeRect.toFloat(), 4.0f);
            g.setColour(juce::Colours::white);
            g.setFont(font9Bold);
            g.drawText(catBadgeText, catBadgeRect, juce::Justification::centred);

            // Container de importação / conversa de WhatsApp: logo da origem (+ contagem) no canto inferior direito.
            if (item.containerOrigem.isNotEmpty()) {
                juce::Rectangle<int> selo(thumbArea.getRight() - 32, thumbArea.getBottom() - 32, 28, 28);
                // O ícone grande do container já diz o que ele é; o selo só aparece quando a capa é uma imagem qualquer.
                if (!ehContainerGrafico(item.containerOrigem) || item.containerCapaCaminho.isNotEmpty())
                    desenharSeloContainer(g, selo, item.containerOrigem, 0);
                if (item.containerTotal > 0) {
                    const juce::String n(item.containerTotal);
                    const int w = juce::jmax(22, juce::GlyphArrangement::getStringWidthInt(font10Bold, n) + 10);
                    juce::Rectangle<int> contagem(selo.getRight() - w, selo.getY() - 20, w, 18);
                    g.setColour(tk.acento);
                    g.fillRoundedRectangle(contagem.toFloat(), 9.0f);
                    g.setColour(tk.textoSobreAcento);
                    g.setFont(font10Bold);
                    g.drawText(n, contagem, juce::Justification::centred);
                }
            }

            // Top-Right Rescan Origin Badge (Section 5.2)
            if (item.rescanOrigem == RescanOrigem::Novo) {
                int rescanBadgeW = 46;
                juce::Rectangle<int> rescanBadgeRect(thumbArea.getRight() - rescanBadgeW - 6, thumbArea.getY() + 6, rescanBadgeW, 18);
                g.setColour(juce::Colour(0xff10b981)); // Green pill
                g.fillRoundedRectangle(rescanBadgeRect.toFloat(), 4.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font9Bold);
                g.drawText("NOVO", rescanBadgeRect, juce::Justification::centred);
            } else if (item.rescanOrigem == RescanOrigem::Modificado) {
                int rescanBadgeW = 76;
                juce::Rectangle<int> rescanBadgeRect(thumbArea.getRight() - rescanBadgeW - 6, thumbArea.getY() + 6, rescanBadgeW, 18);
                g.setColour(juce::Colour(0xfff59e0b)); // Amber pill
                g.fillRoundedRectangle(rescanBadgeRect.toFloat(), 4.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font9Bold);
                g.drawText("MODIFICADO", rescanBadgeRect, juce::Justification::centred);
            }

            // OFFLINE Badge
            if (item.offline) {
                juce::Rectangle<int> offBadge(thumbArea.getCentreX() - 28, thumbArea.getCentreY() - 9, 56, 18);
                g.setColour(juce::Colour(0xd0000000));
                g.fillRoundedRectangle(offBadge.toFloat(), 4.0f);
                g.setColour(juce::Colour(0xfff97316));
                g.drawRoundedRectangle(offBadge.toFloat(), 4.0f, 1.0f);
                g.setFont(font9Bold);
                g.drawText("OFFLINE", offBadge, juce::Justification::centred);
            }

            // Duração (áudio/vídeo): selo no canto inferior esquerdo da miniatura, desenhado aqui (nunca embutido na
            // imagem). Sem duração lida: nada (nunca "0:00").
            if (item.duracaoSegundos && (item.categoria == "Audio" || item.categoria == "Video")) {
                const auto total = static_cast<juce::int64>(*item.duracaoSegundos + 0.5);
                const auto hh = total / 3600, mm = (total / 60) % 60, ss = total % 60;
                const juce::String txt = hh > 0 ? juce::String(hh) + ":" + juce::String(mm).paddedLeft('0', 2) + ":" + juce::String(ss).paddedLeft('0', 2)
                                                : juce::String(mm) + ":" + juce::String(ss).paddedLeft('0', 2);
                const int w = juce::GlyphArrangement::getStringWidthInt(font10Bold, txt) + 12;
                juce::Rectangle<int> durBadge(thumbArea.getX() + 6, thumbArea.getBottom() - 22, w, 16);
                g.setColour(juce::Colour(0xcc000000));
                g.fillRoundedRectangle(durBadge.toFloat(), 4.0f);
                g.setColour(juce::Colours::white);
                g.setFont(font10Bold);
                g.drawText(txt, durBadge, juce::Justification::centred);
            }

            // R (Reject): véu vermelho na miniatura, selo "R" e moldura vermelha no card.
            if (owner_.itemMarcadoR(item.id)) {
                g.setColour(tk.perigo.withAlpha(0.22f));
                g.fillRoundedRectangle(thumbArea.toFloat(), tk.raioPequeno);
                juce::Rectangle<int> rBadge(thumbArea.getRight() - 26, thumbArea.getBottom() - 26, 22, 22);
                g.setColour(tk.perigo);
                g.fillRoundedRectangle(rBadge.toFloat(), 4.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
                g.drawText("R", rBadge, juce::Justification::centred);
                g.setColour(tk.perigo);
                g.drawRoundedRectangle(bounds.toFloat().reduced(1.0f), tk.raioMedio, 2.0f);
            }

            // Anel do FOCO do teclado (diferente do destaque de seleção).
            if (owner_.focoVisivel_ && item.id == owner_.focoId_) {
                g.setColour(juce::Colour(0xff0a84ff));  // azul vivo: a seleção é cinza escuro
                g.drawRoundedRectangle(bounds.toFloat().reduced(1.0f), tk.raioMedio, 4.0f);
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.drawRoundedRectangle(bounds.toFloat().reduced(3.5f), tk.raioMedio, 1.2f);
            }

            // Text Info Section below thumbnail
            auto infoArea = bounds.reduced(8, 0).withTop(thumbArea.getBottom() + 6);

            // Filename / Title (Line 1)
            g.setColour(tk.textoPrimario);
            g.setFont(font12Bold);
            g.drawText(item.nomeArquivo, infoArea.removeFromTop(18), juce::Justification::centredLeft, true);

            // Size & Date (Line 2)
            juce::String sizeDate = formatarBytes(item.tamanhoBytes);
            if (item.dataCriacao.isNotEmpty()) {
                sizeDate += "  |  " + item.dataCriacao.substring(0, 10);
            }
            g.setColour(tk.textoSecundario);
            g.setFont(font11Normal);
            g.drawText(sizeDate, infoArea.removeFromTop(16), juce::Justification::centredLeft, true);

            // Content / Collection Badge (Line 3 if present)
            if (item.collection.isNotEmpty()) {
                auto collBadge = infoArea.removeFromTop(18).reduced(0, 1);
                g.setColour(tk.acento.withAlpha(0.20f));
                g.fillRoundedRectangle(collBadge.toFloat(), 3.0f);
                g.setColour(tk.acento);
                g.drawRoundedRectangle(collBadge.toFloat(), 3.0f, 1.0f);
                g.setFont(font10Bold);
                g.drawText(item.collection, collBadge, juce::Justification::centred, true);
            }

            // Outer Card Border
            if (selecionado) {
                g.setColour(tk.acento);
                g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), tk.raioMedio, 2.0f);
            } else if (hover) {
                g.setColour(tk.bordaFoco);
                g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), tk.raioMedio, 1.5f);
            } else {
                g.setColour(tk.borda);
                g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
            }
        }

        // Marquee selection overlay
        if (lacoAtivo_ && !lacoAtual_.isEmpty()) {
            g.setColour(tk.acento.withAlpha(0.18f));
            g.fillRect(lacoAtual_);
            g.setColour(tk.acento);
            g.drawRect(lacoAtual_, 1);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int idx = indiceNaPosicao(e.getPosition());
        matriz::diag::WatchdogLogger::getInstance().log(
            "[IntakeGrid] mouseDown pos=" + e.getPosition().toString() + " idx=" + juce::String(idx) +
            " colunas_=" + juce::String(colunas_) + " cardW_=" + juce::String(cardW_) + " cardH_=" + juce::String(cardH_));

        owner_.grabKeyboardFocus();  // a tecla R (e as próximas) vale também no modo miniaturas

        if (e.mods.isPopupMenu()) {
            if (idx >= 0 && idx < static_cast<int>(owner_.indicesFiltrados_.size())) {
                int realIdx = owner_.indicesFiltrados_[static_cast<size_t>(idx)];
                if (realIdx >= 0 && realIdx < static_cast<int>(owner_.todosItens_.size())) {
                    owner_.mostrarMenuContexto(realIdx, e.getScreenPosition());
                }
            }
            return;
        }

        if (e.getNumberOfClicks() >= 2) {
            if (idx >= 0 && idx < static_cast<int>(owner_.indicesFiltrados_.size())) {
                const int real = owner_.indicesFiltrados_[static_cast<size_t>(idx)];
                if (real >= 0 && real < static_cast<int>(owner_.todosItens_.size()))
                    owner_.abrirPreviewDoItem(owner_.todosItens_[static_cast<size_t>(real)].id);  // "Mostrar na origem" fica no menu de contexto
            }
            clickedCardIdx_ = -1;
            return;
        }

        lacoInicio_ = e.getPosition();
        lacoAtual_ = {};
        lacoAtivo_ = false;
        clickedCardIdx_ = idx;
        if (idx >= 0 && idx < static_cast<int>(owner_.indicesFiltrados_.size())) {  // o teclado continua daqui
            const int real = owner_.indicesFiltrados_[static_cast<size_t>(idx)];
            if (real >= 0 && real < static_cast<int>(owner_.todosItens_.size())) {
                owner_.focoId_ = owner_.todosItens_[static_cast<size_t>(real)].id;
                owner_.focoVisivel_ = false;
            }
        }

        selecaoAntesDoLaco_.resize(owner_.todosItens_.size());
        for (size_t k = 0; k < owner_.todosItens_.size(); ++k) {
            selecaoAntesDoLaco_[k] = owner_.todosItens_[k].selecionado;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        static int contadorLog = 0;
        if (++contadorLog % 5 == 1) { // não loga TODO evento de drag (spam) — só 1 em 5
            matriz::diag::WatchdogLogger::getInstance().log(
                "[IntakeGrid] mouseDrag pos=" + e.getPosition().toString() +
                " dist=" + juce::String(e.getDistanceFromDragStart()) + " lacoAtivo_=" + juce::String((int)lacoAtivo_));
        }
        if (e.getDistanceFromDragStart() >= 5) {
            lacoAtivo_ = true;
            lacoAtual_ = juce::Rectangle<int>(lacoInicio_, e.getPosition());

            bool invertendo = e.mods.isCommandDown() || e.mods.isCtrlDown();
            bool acumulando = e.mods.isShiftDown();

            int total = static_cast<int>(owner_.indicesFiltrados_.size());
            for (int i = 0; i < total; ++i) {
                int realIdx = owner_.indicesFiltrados_[static_cast<size_t>(i)];
                if (realIdx < 0 || realIdx >= static_cast<int>(owner_.todosItens_.size())) continue;

                bool touched = boundsDoCard(i).intersects(lacoAtual_);
                bool original = (realIdx < static_cast<int>(selecaoAntesDoLaco_.size())) ? selecaoAntesDoLaco_[static_cast<size_t>(realIdx)] : false;

                if (touched) {
                    if (invertendo) {
                        owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = !original;
                    } else {
                        owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = true;
                    }
                } else {
                    if (acumulando || invertendo) {
                        owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = original;
                    } else {
                        owner_.todosItens_[static_cast<size_t>(realIdx)].selecionado = original;
                    }
                }
            }
            owner_.atualizarContagens();
            if (owner_.tabela_) owner_.tabela_->repaint();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (lacoAtivo_) {
            lacoAtivo_ = false;
            lacoAtual_ = {};
            selecaoAntesDoLaco_.clear();
            owner_.atualizarContagens();
            if (owner_.tabela_) owner_.tabela_->repaint();
            repaint();
            return;
        }

        // Single click (not dragged)
        if (clickedCardIdx_ >= 0 && clickedCardIdx_ < static_cast<int>(owner_.indicesFiltrados_.size())) {
            int realIdx = owner_.indicesFiltrados_[static_cast<size_t>(clickedCardIdx_)];
            if (realIdx >= 0 && realIdx < static_cast<int>(owner_.todosItens_.size())) {
                // Seleção estilo Finder: clique simples = só este; Cmd/Ctrl = liga/desliga; Shift = intervalo
                // (âncora compartilhada com a visão em lista).
                owner_.aplicarCliqueSelecao(clickedCardIdx_, e.mods);
                owner_.atualizarContagens();
                if (owner_.tabela_) owner_.tabela_->repaint();
                repaint();
            }
        } else if (clickedCardIdx_ < 0 && !e.mods.isPopupMenu()) {
            // Clique no vazio (nenhum card sob o cursor) deseleciona tudo —
            // mesmo comportamento do Folder Map.
            bool haviaSelecao = std::any_of(owner_.todosItens_.begin(), owner_.todosItens_.end(),
                                             [](const auto& it) { return it.selecionado; });
            if (haviaSelecao) {
                owner_.selecionarTodos(false);
                repaint();
            }
        }
        clickedCardIdx_ = -1;
    }

    void mouseMove(const juce::MouseEvent& e) override {
        int idx = indiceNaPosicao(e.getPosition());
        if (hoverIndex_ != idx) {
            hoverIndex_ = idx;
            setMouseCursor(hoverIndex_ >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoverIndex_ != -1) {
            hoverIndex_ = -1;
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

private:
    IntakeWorkspaceComponent& owner_;
    juce::ThreadPool poolThumbnails_{2};
    std::unordered_map<std::string, juce::Image> cache_;
    std::unordered_map<std::string, bool> loading_;
    std::unordered_map<std::string, bool> noThumbnail_;
    mutable juce::CriticalSection lock_;
    int hoverIndex_ = -1;
    int colunas_ = 1;
    int cardW_ = 200;
    int cardH_ = 210;
    int padding_ = 14;

    bool lacoAtivo_ = false;
    juce::Point<int> lacoInicio_;
    juce::Rectangle<int> lacoAtual_;
    std::vector<bool> selecaoAntesDoLaco_;
    int clickedCardIdx_ = -1;
};

const std::vector<IntakeWorkspaceComponent::CategoriaColecao>& IntakeWorkspaceComponent::vocabularioColecoes() {
    static const std::vector<CategoriaColecao> kVocab = {
        { "AUDIO", {
            "Album", "EP", "Single", "Compilation", "Soundtrack",
            "Stems", "Multitracks", "Sample Pack", "Sample", "Preset", "DAW Session",
            "Field Recording", "Ambience Recording", "Voiceover", "Speech", "Sound FX", "MIDI",
            "Artist Catalog", "Artist Backup", "Demo", "Live Concert", "VS", "Impulse Response", "Mix", "Master", "Raw Audio"
        }},
        { "VIDEO", {
            "Raw Footage", "Home Video", "Music Video", "Film",
            "Documentary", "Corporate Video", "Commercial", "Live Performance",
            "NLE Project", "Social Media Video",
            "WhatsApp Video", "TV Video", "YouTube Video", "360 Video", "Making Of", "Tutorial", "CCTV (Security Camera)"
        }},
        { "IMAGE", {
            "Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional",
            "Image Edit Project", "Graphics", "Logo", "3D", "YouTube Thumbnail",
            "Exhibition", "Raw Photos", "Technical Photography"
        }},
        { "DOCUMENT", {
            "Documentation", "Release", "Article", "Press", "Book", "Text", "Journal", "Contract", "Personal Document", "Manual", "Report",
            "Reference", "Technical Documentation", "Sheet", "Zip", "Rar", "PDF", "Doc/Docx", "Markdown",
            "Website", "DAW", "NLE", "CSV", "MTZ Collection", "BKM Catalog"
        }}
    };
    return kVocab;
}

void IntakeWorkspaceComponent::popularComboColecoes(juce::ComboBox& combo, bool incluirNone) {
    combo.clear(juce::dontSendNotification);
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    int id = 1;
    if (incluirNone) {
        combo.addItem(i18n::t("intake.nenhuma_colecao"), id++);
        combo.addSeparator();
    }
    for (const auto& cat : vocabularioColecoes()) {
        juce::String grupoNome = cat.grupo;
        if (isPt) {
            if (grupoNome == "AUDIO") grupoNome = juce::String::fromUTF8("ÁUDIO");
            else if (grupoNome == "VIDEO") grupoNome = juce::String::fromUTF8("VÍDEO");
            else if (grupoNome == "IMAGE") grupoNome = juce::String::fromUTF8("IMAGEM");
            else if (grupoNome == "DOCUMENT") grupoNome = juce::String::fromUTF8("DOCUMENTO");
        }
        combo.addSectionHeading(grupoNome);
        for (const auto& item : cat.itens) {
            combo.addItem(isPt ? traduzirContent(item, true) : item, id++);
        }
    }
    if (incluirNone) {
        combo.setSelectedId(1, juce::dontSendNotification);
    }
}

class IntakeWorkspaceComponent::IntakeDragDropEmptyState : public juce::Component,
                                                           public juce::FileDragAndDropTarget {
public:
    IntakeDragDropEmptyState(IntakeWorkspaceComponent& owner) : owner_(owner) {
        setInterceptsMouseClicks(true, true);
    }

    bool isInterestedInFileDrag(const juce::StringArray&) override {
        return true;
    }

    void fileDragEnter(const juce::StringArray&, int, int) override {
        dragOver_ = true;
        repaint();
    }

    void fileDragExit(const juce::StringArray&) override {
        dragOver_ = false;
        repaint();
    }

    void filesDropped(const juce::StringArray& files, int x, int y) override {
        dragOver_ = false;
        repaint();
        owner_.filesDropped(files, x, y);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        hover_ = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override {
        hover_ = false;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (!e.mouseWasDraggedSinceMouseDown()) {
            owner_.abrirMenuImportar();
        }
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
        auto area = getLocalBounds();

        int cardW = juce::jmin(640, getWidth() - 60);
        int cardH = juce::jmin(340, getHeight() - 40);
        if (cardW < 200 || cardH < 150) return;

        auto boxRect = area.withSizeKeepingCentre(cardW, cardH);

        juce::Colour fillCol = dragOver_ ? tk.acento.withAlpha(0.12f)
                             : (hover_ ? tk.painelAlt.withAlpha(0.6f) : tk.painel.withAlpha(0.35f));
        g.setColour(fillCol);
        g.fillRoundedRectangle(boxRect.toFloat(), 14.0f);

        juce::Colour borderCol = dragOver_ ? tk.acento
                               : (hover_ ? tk.bordaFoco : tk.borda.withAlpha(0.85f));
        g.setColour(borderCol);
        float dashLengths[2] = { 8.0f, 6.0f };
        juce::Path p;
        p.addRoundedRectangle(boxRect.toFloat().reduced(1.0f), 14.0f);
        juce::Path dashed;
        juce::PathStrokeType(2.0f).createDashedStroke(dashed, p, dashLengths, 2);
        g.fillPath(dashed);

        auto contentArea = boxRect.reduced(24, 20);

        int iconSize = 72;
        auto iconBox = contentArea.removeFromTop(iconSize + 16).withSizeKeepingCentre(iconSize, iconSize).toFloat();

        juce::Colour iconCol = dragOver_ ? tk.acento : (hover_ ? tk.acento.withAlpha(0.95f) : tk.textoTerciario.interpolatedWith(tk.acento, 0.45f));
        g.setColour(iconCol);

        float ix = iconBox.getX();
        float iy = iconBox.getY();
        float iw = iconBox.getWidth();
        float ih = iconBox.getHeight();

        juce::Path tray;
        float trayTop = iy + ih * 0.44f;
        float trayBottom = iy + ih * 0.96f;
        float trayLeft = ix + iw * 0.12f;
        float trayRight = ix + iw * 0.88f;
        float cr = 10.0f;

        tray.startNewSubPath(trayLeft, trayTop);
        tray.lineTo(trayLeft, trayBottom - cr);
        tray.quadraticTo(trayLeft, trayBottom, trayLeft + cr, trayBottom);
        tray.lineTo(trayRight - cr, trayBottom);
        tray.quadraticTo(trayRight, trayBottom, trayRight, trayBottom - cr);
        tray.lineTo(trayRight, trayTop);
        tray.lineTo(trayRight - iw * 0.22f, trayTop);
        tray.lineTo(trayRight - iw * 0.28f, trayTop + ih * 0.14f);
        tray.lineTo(trayLeft + iw * 0.28f, trayTop + ih * 0.14f);
        tray.lineTo(trayLeft + iw * 0.22f, trayTop);
        tray.closeSubPath();

        g.strokePath(tray, juce::PathStrokeType(2.5f));

        juce::Path arrow;
        float midX = ix + iw * 0.5f;
        float arrowTop = iy + ih * 0.06f;
        float arrowTip = iy + ih * 0.60f;
        float hw = iw * 0.22f;
        float hh = ih * 0.22f;

        arrow.startNewSubPath(midX, arrowTop);
        arrow.lineTo(midX, arrowTip);
        arrow.startNewSubPath(midX - hw, arrowTip - hh);
        arrow.lineTo(midX, arrowTip);
        arrow.lineTo(midX + hw, arrowTip - hh);

        g.strokePath(arrow, juce::PathStrokeType(3.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

        contentArea.removeFromTop(8);
        g.setColour(dragOver_ ? tk.acento : tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
        juce::String titulo = isPt ? juce::String::fromUTF8("ARRASTE E SOLTE ARQUIVOS OU PASTAS AQUI")
                                   : "DRAG & DROP FILES OR FOLDERS HERE";
        g.drawText(titulo, contentArea.removeFromTop(26), juce::Justification::centred);

        contentArea.removeFromTop(6);
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
        juce::String subtitulo = isPt ? juce::String::fromUTF8("Suporta áudio, vídeo, imagens, documentos e sessões multitrack\nou clique em qualquer lugar desta área para selecionar")
                                      : "Supports audio, video, images, documents and multitrack sessions\nor click anywhere in this area to browse";
        g.drawFittedText(subtitulo, contentArea.removeFromTop(36), juce::Justification::centred, 2);
    }

private:
    IntakeWorkspaceComponent& owner_;
    bool hover_ = false;
    bool dragOver_ = false;
};

IntakeWorkspaceComponent::IntakeWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {
    const auto& tk = tema();

    // Ícone GEO LOCATION — o PNG vem com fundo branco chapado; aqui ele vira
    // transparente para o ícone assentar sobre o fundo do card.
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

    // 1. LINHA 1 (BatchHeaderBar)
    lblTitulo_ = std::make_unique<juce::Label>("", i18n::t("intake.titulo"));
    lblTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    lblContadorTotal_ = std::make_unique<juce::Label>("", "");
    lblContadorTotal_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    lblContadorTotal_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblContadorTotal_);

    btnVisaoLista_ = std::make_unique<ViewModeIconButton>(ViewModeIconButton::IconType::List);
    btnVisaoLista_->setAtivo(true);
    btnVisaoLista_->setTooltip(i18n::t("intake.tooltip_lista"));
    btnVisaoLista_->onClick = [this] { definirModoVisao(ModoVisao::Lista); };
    btnVisaoLista_->setSegmentado(true);
    addAndMakeVisible(*btnVisaoLista_);

    btnVisaoIcones_ = std::make_unique<ViewModeIconButton>(ViewModeIconButton::IconType::Grid);
    btnVisaoIcones_->setAtivo(false);
    btnVisaoIcones_->setTooltip(i18n::t("intake.tooltip_icones"));
    btnVisaoIcones_->onClick = [this] { definirModoVisao(ModoVisao::Icones); };
    btnVisaoIcones_->setSegmentado(true);
    addAndMakeVisible(*btnVisaoIcones_);

    painelMetadados_ = std::make_unique<PainelMetadadosArquivo>(projeto_);
    addAndMakeVisible(*painelMetadados_);

    // Mesmo slider de tamanho da aba METADATA (mesmo visual e limites); só vale na visão em miniaturas.
    sliderTamanho_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    sliderTamanho_->setRange(0.0, 1.0, 0.01);
    sliderTamanho_->setValue(tamanhoMiniaturas_, juce::dontSendNotification);
    sliderTamanho_->setDoubleClickReturnValue(true, tamanhoMiniaturas_);
    sliderTamanho_->setColour(juce::Slider::trackColourId, tema().borda);
    sliderTamanho_->setColour(juce::Slider::thumbColourId, tema().acento);
    sliderTamanho_->setColour(juce::Slider::backgroundColourId, tema().painelAlt);
    sliderTamanho_->onValueChange = [this] {
        tamanhoMiniaturas_ = sliderTamanho_->getValue();
        if (gridComponent_) gridComponent_->recalcularLayout();
    };
    sliderTamanho_->setTooltip("Adjust thumbnail display size");
    sliderTamanho_->setEnabled(false);  // habilita ao entrar na visão em miniaturas
    addAndMakeVisible(*sliderTamanho_);
    lblTamanho_ = std::make_unique<juce::Label>("", "SIZE");
    lblTamanho_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
    lblTamanho_->setColour(juce::Label::textColourId, tema().textoTerciario);
    lblTamanho_->setJustificationType(juce::Justification::centred);
    addChildComponent(*lblTamanho_);  // o mapa da barra não tem o rótulo SIZE

    // IMPORT: um botão só. Arquivos/pastas, Google Drive, celular, Lightroom, Bridge e o resto vivem na janela de cards.
    auto btnImportar = std::make_unique<ImportarIconButton>(i18n::t("imp.botao"));
    btnImportar->setTooltip(i18n::t("imp.botao.dica"));
    btnImportar->onClick = [this] { abrirMenuImportar(); };
    btnIngerir_ = std::move(btnImportar);
    addAndMakeVisible(*btnIngerir_);

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    // CLOSE PROJECT / RETURN TO CATALOG saíram da barra desta aba (e de todas
    // as outras): ambos vivem agora só no menu File.

    btnAjuda_ = std::make_unique<juce::TextButton>("?");
    btnAjuda_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnAjuda_->setColour(juce::TextButton::textColourOffId, tk.acento);
    btnAjuda_->setColour(juce::TextButton::textColourOnId, tk.acento);
    btnAjuda_->setTooltip(isPt ? juce::String::fromUTF8("Gerenciar arquivos recém-ingeridos aguardando verificação para METADADOS") : "Manage recently ingested files awaiting verification to METADATA");
    btnAjuda_->onClick = [this] { if (aoPedirAjuda) aoPedirAjuda(); };
    addAndMakeVisible(*btnAjuda_);

    // 2. LINHA 2 (FilterBar)
    auto setupPill = [this](std::unique_ptr<juce::TextButton>& btn, const juce::String& text, const juce::String& cat, juce::Colour dotCol) {
        auto pill = std::make_unique<PillButton>(text);
        pill->tamanhoFonte = 13.0f;
        pill->corPonto = dotCol;
        pill->estiloChip = true;
        pill->onClick = [this, cat] {
            filtroCategoriaAtual_ = cat;
            atualizarFiltragem();
        };
        btn = std::move(pill);
        addAndMakeVisible(*btn);
    };

    comboAno_ = std::make_unique<juce::ComboBox>();
    comboAno_->setTooltip(i18n::t("intake.filtro_ano_dica"));
    comboAno_->onChange = [this] {
        const int id = comboAno_->getSelectedId();
        filtroAnoAtual_ = id <= 1 ? 0 : (id == 2 ? -1 : id);  // ids: 1 todos, 2 sem data, ano = o próprio ano
        filtroMesAtual_ = filtroDiaAtual_ = 0;
        atualizarComboAno();
        atualizarFiltragem();
    };
    addAndMakeVisible(*comboAno_);
    comboMes_ = std::make_unique<juce::ComboBox>();
    comboMes_->setTooltip(i18n::t("intake.filtro_mes"));
    comboMes_->onChange = [this] {
        const int id = comboMes_->getSelectedId();
        filtroMesAtual_ = id >= 100 ? id - 100 : 0;  // ids: 1 todos, 100 + mês
        filtroDiaAtual_ = 0;
        atualizarComboAno();
        atualizarFiltragem();
    };
    addAndMakeVisible(*comboMes_);
    comboDia_ = std::make_unique<juce::ComboBox>();
    comboDia_->setTooltip(i18n::t("intake.filtro_dia"));
    comboDia_->onChange = [this] {
        const int id = comboDia_->getSelectedId();
        filtroDiaAtual_ = id >= 100 ? id - 100 : 0;
        atualizarFiltragem();
    };
    addAndMakeVisible(*comboDia_);

    setupPill(btnFiltroAll_, "All (0)", "ALL", juce::Colours::transparentBlack);
    setupPill(btnFiltroAudio_, "Audio (0)", "Audio", juce::Colour(0xff2a9d8f));
    setupPill(btnFiltroVideo_, "Video (0)", "Video", juce::Colour(0xff9d4edd));
    setupPill(btnFiltroImage_, "Images (0)", "Image", juce::Colour(0xfff4a261));
    setupPill(btnFiltroDoc_, "Documents (0)", "Document", juce::Colour(0xff2b9348));
    setupPill(btnFiltroOther_, "Other (0)", "Other", juce::Colour(0xff7d766c));
    setupPill(btnFiltroPhoneDump_, i18n::t("intake.filtro_phone_dump").replace("{n}", "0"), "PhoneDump", juce::Colour(0xff06b6d4));

    // 3. LINHA 3 (SelectionActionBar)
    // Cluster A (Left)
    lblSubtitulo_ = std::make_unique<juce::Label>("", "");
    lblSubtitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addChildComponent(*lblSubtitulo_);  // "N selected" é pintado na linha do título FILTER (paint)

    auto btnSelTodos = std::make_unique<PillButton>(i18n::t("intake.selecionar_todos"));
    btnSelTodos->tamanhoFonte = 12.5f;
    btnSelTodos->onClick = [this] { selecionarTodos(true); };
    btnSelecionarTodos_ = std::move(btnSelTodos);
    addAndMakeVisible(*btnSelecionarTodos_);

    auto btnLimpar = std::make_unique<PillButton>(i18n::t("intake.limpar_selecao"));
    btnLimpar->tamanhoFonte = 12.5f;
    btnLimpar->onClick = [this] { selecionarTodos(false); };
    btnLimparSelecao_ = std::move(btnLimpar);
    addAndMakeVisible(*btnLimparSelecao_);

    // Cluster B (Center) — Batch Assignment: um botão colorido por campo,
    // mesma cor do bloco correspondente no Visual Editor da aba BACKUP
    // (item 2 da correção "BACKUP e INTAKE").
    using NH = matriz::consolidacao::NivelHierarquia;
    auto criarBotaoLote = [this](const juce::String& texto, juce::Colour cor, std::function<void()> aoClicar) {
        auto btn = std::make_unique<PillButton>(texto);
        btn->tamanhoFonte = 12.5f;
        btn->estiloChip = true;  // quadradinho da cor + nome, em duas colunas
        btn->corFundoCustom = cor;
        btn->corTextoCustom = juce::Colours::white;
        btn->corBordaCustom = cor.darker(0.2f);
        btn->onClick = std::move(aoClicar);
        return btn;
    };

    auto btnOrigMed = criarBotaoLote(i18n::t("intake.btn_source_medium_lote"), corDoNivelHierarquia(NH::Origem), [this] {
        if (btnSourceMediumLote_) mostrarEditorOriginalSourceMediumLote(btnSourceMediumLote_->getScreenBounds());
    });
    btnSourceMediumLote_ = std::move(btnOrigMed);
    addAndMakeVisible(*btnSourceMediumLote_);

    auto btnCreator = criarBotaoLote(i18n::t("intake.btn_creator_lote"), corDoNivelHierarquia(NH::Artista), [this] {
        if (btnCreatorLote_) mostrarEditorCreatorLote(btnCreatorLote_->getScreenBounds());
    });
    btnCreatorLote_ = std::move(btnCreator);
    addAndMakeVisible(*btnCreatorLote_);

    auto btnContent = criarBotaoLote(i18n::t("intake.btn_content_lote"), corDoNivelHierarquia(NH::ContentType), [this] {
        if (btnContentLote_) mostrarEditorContentLote(btnContentLote_->getScreenBounds());
    });
    btnContentLote_ = std::move(btnContent);
    addAndMakeVisible(*btnContentLote_);

    auto btnSubject = criarBotaoLote(i18n::t("intake.btn_subject_lote"), corDoNivelHierarquia(NH::Subject), [this] {
        if (btnSubjectLote_) mostrarEditorSubjectLote(btnSubjectLote_->getScreenBounds());
    });
    btnSubjectLote_ = std::move(btnSubject);
    addAndMakeVisible(*btnSubjectLote_);

    // Item 2 (nova lista) — o default (herdar o ano de DATE CREATED) já
    // existe em FichaPanelComponent; este botão só dá ao operador um jeito
    // rápido de sobrescrever em lote no INTAKE, antes de mandar pra GRID.
    // Cor fixa em roxo clarinho (item 2, lista nova de hoje) só neste
    // botão — corDoNivelHierarquia(Ano) é o mesmo verde de CONTENT
    // (ContentType) e ficava fácil de confundir um com o outro aqui; o
    // Visual Editor do BACKUP continua usando o verde original pro nível
    // Ano, não mexi nisso.
    auto btnEventDate = criarBotaoLote(i18n::t("intake.btn_event_date_lote"), juce::Colour(0xff9575cd), [this] {
        if (btnEventDateLote_) mostrarEditorEventDateLote(btnEventDateLote_->getScreenBounds());
    });
    btnEventDateLote_ = std::move(btnEventDate);
    addAndMakeVisible(*btnEventDateLote_);

    // GEO LOCATION no card Batch Assignment abaixo de Event Date (item 9A)
    auto btnGeo = std::make_unique<PillButton>(i18n::t("intake.btn_geo_location_lote"));
    btnGeo->tamanhoFonte = 12.0f;
    btnGeo->desenharPin = true;
    btnGeo->estiloChip = true;
    btnGeo->corFundoCustom = juce::Colour(0xff0284c7); // azul celeste
    btnGeo->corTextoCustom = juce::Colours::white;
    btnGeo->corBordaCustom = juce::Colour(0xff0369a1);
    btnGeo->onClick = [this] {
        if (btnGeolocationLote_) mostrarEditorGeolocationLote(btnGeolocationLote_->getScreenBounds());
    };
    btnGeolocationLote_ = std::move(btnGeo);
    addAndMakeVisible(*btnGeolocationLote_);

    // Cluster C (Right)
    auto btnConfSel = std::make_unique<PillButton>(i18n::t("intake.btn_enviar_selecionados"));
    btnConfSel->tamanhoFonte = 13.0f;
    btnConfSel->semQuadradinho = true;
    btnConfSel->corFundoCustom = juce::Colour(0xff16a34a);
    btnConfSel->corTextoCustom = juce::Colours::white;
    btnConfSel->corBordaCustom = juce::Colour(0xff15803d);
    btnConfSel->onClick = [this] { confirmarSelecaoParaGrid(); };
    btnConfSel->setTooltip("Send selected intake items to GRID");
    btnConfSel->setEnabled(false);
    btnConfirmarSelecao_ = std::move(btnConfSel);
    addAndMakeVisible(*btnConfirmarSelecao_);

    auto btnConfTodos = std::make_unique<PillButton>(i18n::t("intake.btn_enviar_todos"));
    btnConfTodos->tamanhoFonte = 13.0f;
    btnConfTodos->principal = true;  // "Approve all": a ação principal do painel (grafite)
    btnConfTodos->corFundoCustom = juce::Colour(0xff16a34a);
    btnConfTodos->corTextoCustom = juce::Colours::white;
    btnConfTodos->corBordaCustom = juce::Colour(0xff15803d);
    btnConfTodos->onClick = [this] { confirmarTodosParaGrid(); };
    btnConfTodos->setTooltip("Send all items in this batch to GRID");
    btnConfirmarTodos_ = std::move(btnConfTodos);
    addAndMakeVisible(*btnConfirmarTodos_);

    // Divisors for SelectionActionBar
    divisor1_ = std::make_unique<VerticalDividerComponent>();
    addAndMakeVisible(*divisor1_);

    divisor2_ = std::make_unique<VerticalDividerComponent>();
    addAndMakeVisible(*divisor2_);

    auto btnRemover = std::make_unique<PillButton>(i18n::t("intake.btn_rejeitar_selecionados"));
    btnRemover->tamanhoFonte = 13.0f;
    btnRemover->corFundoCustom = tk.perigo;
    btnRemover->corTextoCustom = juce::Colours::white;
    btnRemover->corBordaCustom = tk.perigo.darker(0.2f);
    btnRemover->onClick = [this] { rejeitarMarcados(); };
    btnRemover->setAlpha(1.0f);
    btnRemover->setEnabled(true);
    btnRemoverSelecao_ = std::move(btnRemover);
    addAndMakeVisible(*btnRemoverSelecao_);

    {
        // A frase longa sobre o REJECT MARKED vai para o "?" (aparece ao passar o mouse), como no mapa.
        auto legenda = std::make_unique<LegendaAtalhosIntake>(i18n::t("intake.legenda_r_dica") + "\n" + i18n::t("intake.nota_reject"));
        legenda->interrogacaoNaDireita = true;
        legendaAtalhos_ = std::move(legenda);
    }
    addAndMakeVisible(*legendaAtalhos_);
    lblNotaReject_ = std::make_unique<juce::Label>("", i18n::t("intake.nota_reject"));
    lblNotaReject_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
    lblNotaReject_->setColour(juce::Label::textColourId, tk.textoSecundario);
    lblNotaReject_->setMinimumHorizontalScale(1.0f);  // quebra em 2 linhas, nunca "..."
    lblNotaReject_->setInterceptsMouseClicks(false, false);
    addChildComponent(*lblNotaReject_);  // o texto vive no tooltip do "?" da legenda
    setWantsKeyboardFocus(true);
    atualizarBotaoRejeitar();

    // 4. Main Table List
    tabela_ = std::make_unique<juce::TableListBox>("IntakeTable", this);
    tabela_->setColour(juce::ListBox::backgroundColourId, tk.painel);
    tabela_->setColour(juce::ListBox::outlineColourId, tk.borda);
    tabela_->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, tk.painelAlt);
    tabela_->getHeader().setColour(juce::TableHeaderComponent::textColourId, tk.textoPrimario);
    tabela_->getHeader().setColour(juce::TableHeaderComponent::outlineColourId, tk.borda);
    tabela_->setHeaderHeight(28);
    tabela_->setRowHeight(32);
    tabela_->setMultipleSelectionEnabled(true);

    auto& hdr = tabela_->getHeader();
    hdr.addColumn("", kColSelect, 36, 36, 36, juce::TableHeaderComponent::notSortable);
    hdr.addColumn(i18n::t("intake.col_type"), kColType, 80, 70, 110, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_name"), kColName, 220, 140, 500, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_origin"), kColOrigin, 110, 90, 140, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_date"), kColDateCreated, 150, 110, 200, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_size"), kColSize, 80, 60, 120, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_path"), kColPath, 250, 120, 600, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_collection"), kColCollection, 150, 100, 240, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_source_media"), kColSourceMedia, 200, 130, 350, juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn(i18n::t("intake.col_action"), kColAction, 80, 70, 100, juce::TableHeaderComponent::notSortable);
    addAndMakeVisible(*tabela_);
    tabela_->addMouseListener(this, true);

    chkSelectAllHeader_ = std::make_unique<juce::ToggleButton>();
    chkSelectAllHeader_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    chkSelectAllHeader_->setTooltip(matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Selecionar / Desmarcar Todos") : "Select / Deselect All");
    chkSelectAllHeader_->onClick = [this] {
        bool checked = chkSelectAllHeader_->getToggleState();
        selecionarTodos(checked);
    };
    tabela_->getHeader().addAndMakeVisible(*chkSelectAllHeader_);

    gridComponent_ = std::make_unique<ThumbnailsGridComponent>(*this);
    gridViewport_ = std::make_unique<juce::Viewport>();
    gridViewport_->setViewedComponent(gridComponent_.get(), false);
    gridViewport_->setScrollBarsShown(true, false);
    gridViewport_->setVisible(false);
    addChildComponent(*gridViewport_);

    emptyState_ = std::make_unique<IntakeDragDropEmptyState>(*this);
    addChildComponent(*emptyState_);

    recarregar();
}

IntakeWorkspaceComponent::~IntakeWorkspaceComponent() {
    janelaPreview_.reset();
    poolSnapshot_.removeAllJobs(true, 2000);
}

void IntakeWorkspaceComponent::atualizarVisibilidadeEmptyState() {
    bool vazio = todosItens_.empty();
    if (emptyState_) {
        emptyState_->setVisible(vazio);
        if (vazio) emptyState_->toFront(false);
    }
    // A área de drop nunca fica por cima de uma janela aberta (ela engole os cliques e a janela parece travada).
    if (overlayAtual_ && overlayAtual_->isVisible()) overlayAtual_->toFront(true);
    if (vazio) {
        if (tabela_) tabela_->setVisible(false);
        if (gridViewport_) gridViewport_->setVisible(false);
    } else {
        if (tabela_) tabela_->setVisible(modoVisao_ == ModoVisao::Lista);
        if (gridViewport_) {
            gridViewport_->setVisible(modoVisao_ == ModoVisao::Icones);
            if (gridComponent_) gridComponent_->recalcularLayout();
        }
    }
}

void IntakeWorkspaceComponent::registrarItensRescan(const std::vector<std::pair<std::string, RescanOrigem>>& itensRescan) {
    for (const auto& par : itensRescan) {
        badgesRescanSessao_[par.first] = par.second;
    }
    carregarItens();
    for (auto& it : todosItens_) {
        if (badgesRescanSessao_.count(it.id)) {
            it.selecionado = true;
        }
    }
    atualizarContagens();
    if (tabela_) tabela_->updateContent();
    if (gridComponent_) gridComponent_->repaint();
    repaint();
}

void IntakeWorkspaceComponent::carregarItens() {
    recarregar();
}

void IntakeWorkspaceComponent::aplicarItensQuarentena(std::vector<ItemResumo> quarentena) {
    // Preserve selection of existing items
    std::set<std::string> selecionadosAnteriores;
    for (const auto& it : todosItens_) {
        if (it.selecionado) selecionadosAnteriores.insert(it.id);
    }

    std::vector<ItemIntake> novosItens;
    novosItens.reserve(quarentena.size());
    std::vector<std::string> idsCelularSemMedium;
    // Registro explícito de importação pelo fluxo PHONE DUMP (container gallery/whatsapp ou conversa de WhatsApp);
    // nada de caminho ou tag.
    auto ehImportadoPeloPhoneDump = [](const std::string& containerOrigem, const std::string& collectionType) {
        return containerOrigem == "gallery" || containerOrigem == "whatsapp" || collectionType == "WhatsApp Chat";
    };

    for (const auto& item : quarentena) {
        // CONTAINER: só a capa aparece no Intake (representa a importação inteira); os demais membros ficam escondidos.
        if (!item.nestOrigem.empty() && !item.nestCapa) {
            // Os membros escondidos também vieram pelo Phone Dump: recebem o Original Source Medium (a cover é tratada abaixo).
            const bool origemCelular = ehImportadoPeloPhoneDump(item.nestOrigem, item.collectionType.value_or(std::string()));
            if (origemCelular && (item.sourceMedia.empty() ||
                                  OriginalSourceMediumInfo::deserialize(item.sourceMedia).isNoneOrUnknown()))
                idsCelularSemMedium.push_back(item.id);
            continue;
        }
        ItemIntake it;
        it.id = item.id;
        it.titulo = juce::String::fromUTF8(item.titulo.c_str());
        it.nomeArquivo = item.nomeOriginalArquivo.empty() ? it.titulo : juce::String::fromUTF8(item.nomeOriginalArquivo.c_str());
        it.extensao = juce::String(item.extensaoArquivo).toUpperCase();
        it.tamanhoBytes = item.tamanhoBytes;
        if (item.duracaoSegundos && *item.duracaoSegundos > 0.0) it.duracaoSegundos = item.duracaoSegundos;

        auto cat = matriz::ingest::categoriaPorExtensao(juce::String(item.extensaoArquivo));
        switch (cat) {
            case matriz::ingest::CategoriaMidia::Audio:      it.categoria = "Audio"; break;
            case matriz::ingest::CategoriaMidia::Video:      it.categoria = "Video"; break;
            case matriz::ingest::CategoriaMidia::Imagem:     it.categoria = "Image"; break;
            case matriz::ingest::CategoriaMidia::Documento:  it.categoria = "Document"; break;
            case matriz::ingest::CategoriaMidia::Texto:      it.categoria = "Document"; break;
            case matriz::ingest::CategoriaMidia::Sessao:     it.categoria = "Project"; break;
            default:                                         it.categoria = "Other"; break;
        }

        it.collection = item.collectionType.has_value() ? juce::String::fromUTF8(item.collectionType->c_str()) : juce::String();
        if (!item.nestOrigem.empty()) {
            it.containerOrigem = juce::String::fromUTF8(item.nestOrigem.c_str());
            it.containerTotal = item.nestTotal;
            it.containerNestId = item.nestId;
            it.containerCapaCaminho = juce::String::fromUTF8(item.nestCapaImagem.c_str());
            it.containerIcone = juce::String::fromUTF8(item.nestIcone.c_str());
            if (item.nestOrigem != "whatsapp") it.nomeArquivo = (item.nestNome.empty() ? rotuloDaOrigem(it.containerOrigem) : juce::String::fromUTF8(item.nestNome.c_str())) + juce::String::fromUTF8(" \xe2\x80\x94 ") +
                             i18n::t("intake.container_arquivos").replace("{n}", juce::String(item.nestTotal));
            it.titulo = it.nomeArquivo;
        } else if (it.collection == "WhatsApp Chat") {
            it.containerOrigem = "whatsapp";
        }
        it.sourceMedia = juce::String::fromUTF8(item.sourceMedia.c_str());
        it.offline = item.offline;

        auto bIt = badgesRescanSessao_.find(item.id);
        if (bIt != badgesRescanSessao_.end()) {
            it.rescanOrigem = bIt->second;
            it.selecionado = true;
        } else {
            it.rescanOrigem = RescanOrigem::Nenhum;
            it.selecionado = (selecionadosAnteriores.count(item.id) > 0);
        }

        it.dataCriacao = juce::String::fromUTF8(item.dataCriacao.c_str());
        it.ano = item.ano;
        it.dataCriacaoDoMetadado = it.dataCriacao.isNotEmpty();
        if (it.dataCriacao.isEmpty()) {
            it.dataCriacao = juce::String::fromUTF8(item.criadoEm.c_str());
        }

        it.caminhoOrigem = !item.caminhoAbsolutoOrigem.empty() ? juce::String::fromUTF8(item.caminhoAbsolutoOrigem.c_str())
                                                              : juce::String::fromUTF8(item.caminhoRelativoArquivo.c_str());
        if (!item.miniaturaCaminhoRelativo.empty())
            it.caminhoMiniatura = projeto_.projeto().pasta().getChildFile(
                juce::String::fromUTF8(item.miniaturaCaminhoRelativo.c_str())).getFullPathName();

        // IMPORT FROM PHONE (galeria/WhatsApp): o registro da importação garante Original Source Medium = Phone Dump,
        // também em itens antigos que ficaram sem medium. Só preenche quando ainda não há medium definido.
        if (ehImportadoPeloPhoneDump(item.nestOrigem, item.collectionType.value_or(std::string())) &&
            (it.sourceMedia.isEmpty() || OriginalSourceMediumInfo::deserialize(it.sourceMedia.toStdString()).isNoneOrUnknown())) {
            OriginalSourceMediumInfo phoneDump;
            phoneDump.medium = kMediumPhoneDump;
            it.sourceMedia = juce::String::fromUTF8(phoneDump.serialize().c_str());
            idsCelularSemMedium.push_back(it.id);
        }

        novosItens.push_back(std::move(it));
    }

    if (!idsCelularSemMedium.empty()) {
        ProjetoAberto* projeto = &projeto_;
        poolMetadadoLote_.addJob([projeto, ids = std::move(idsCelularSemMedium)] {
            projeto->definirOrigemPhoneDump(ids);  // uma transação só, silencioso (não vira "editado")
        });
    }

    todosItens_ = std::move(novosItens);
    marcadosR_ = projeto_.idsMarcadosR();

    atualizarComboAno();
    if (ultimoSortColumnId_ > 0) {
        sortOrderChanged(ultimoSortColumnId_, sortAscendente_);
    } else {
        atualizarFiltragem();
    }
    atualizarContagens();
    atualizarVisibilidadeEmptyState();
}

void IntakeWorkspaceComponent::definirModoVisao(ModoVisao modo) {
    if (modoVisao_ != modo) {
        modoVisao_ = modo;
        const auto& tk = tema();
        if (btnVisaoLista_) {
            btnVisaoLista_->setAtivo(modoVisao_ == ModoVisao::Lista);
        }
        if (btnVisaoIcones_) {
            btnVisaoIcones_->setAtivo(modoVisao_ == ModoVisao::Icones);
        }
        if (sliderTamanho_) sliderTamanho_->setEnabled(modoVisao_ == ModoVisao::Icones);
        idNoPainel_.clear();             // ao voltar para as miniaturas o painel relê o item atual
        atualizarPainelMetadados();
        atualizarVisibilidadeEmptyState();
        resized();
        repaint();
    }
}

bool IntakeWorkspaceComponent::partesDaData(const juce::String& data, int& ano, int& mes, int& dia) {
    // "AAAA", "AAAA-MM-DD..." ou "AAAA:MM:DD ..." (EXIF). Mês/dia 0 = ausentes.
    ano = mes = dia = 0;
    if (data.length() < 4 || !data.substring(0, 4).containsOnly("0123456789")) return false;
    ano = data.substring(0, 4).getIntValue();
    if (data.length() >= 7) mes = data.substring(5, 7).getIntValue();
    if (data.length() >= 10) dia = data.substring(8, 10).getIntValue();
    if (mes < 1 || mes > 12) mes = dia = 0;
    if (dia < 1 || dia > 31) dia = 0;
    return ano > 0;
}

void IntakeWorkspaceComponent::atualizarComboAno() {
    if (!comboAno_) return;
    std::map<int, int> porAno, porMes, porDia;
    int semData = 0;
    for (const auto& it : todosItens_) {
        int a = 0, m = 0, d = 0;
        if (!partesDaData(it.dataCriacao, a, m, d)) { ++semData; continue; }
        ++porAno[a];
        if (a == filtroAnoAtual_ && m > 0) {
            ++porMes[m];
            if (m == filtroMesAtual_ && d > 0) ++porDia[d];
        }
    }
    comboAno_->clear(juce::dontSendNotification);
    comboAno_->addItem(i18n::t("intake.filtro_ano_todos") + " (" + juce::String((int) todosItens_.size()) + ")", 1);
    if (semData > 0) comboAno_->addItem(i18n::t("intake.filtro_ano_sem") + " (" + juce::String(semData) + ")", 2);
    for (auto it = porAno.rbegin(); it != porAno.rend(); ++it)
        if (it->first > 2) comboAno_->addItem(juce::String(it->first) + " (" + juce::String(it->second) + ")", it->first);
    // Mantém o ano escolhido se ele ainda existe; senão volta pra "todos".
    const int idAtual = filtroAnoAtual_ == 0 ? 1 : (filtroAnoAtual_ == -1 ? 2 : filtroAnoAtual_);
    if (comboAno_->indexOfItemId(idAtual) >= 0) comboAno_->setSelectedId(idAtual, juce::dontSendNotification);
    else {
        filtroAnoAtual_ = filtroMesAtual_ = filtroDiaAtual_ = 0;
        comboAno_->setSelectedId(1, juce::dontSendNotification);
    }
    // Mês e dia: só com um ano escolhido (e o dia, com um mês).
    if (comboMes_ && comboDia_) {
        comboMes_->clear(juce::dontSendNotification);
        comboDia_->clear(juce::dontSendNotification);
        comboMes_->addItem(i18n::t("intake.filtro_mes_todos"), 1);
        comboDia_->addItem(i18n::t("intake.filtro_dia_todos"), 1);
        const juce::StringArray nomesMes = juce::StringArray::fromTokens(i18n::t("intake.meses"), ",", "");
        for (auto& [m, n] : porMes)
            comboMes_->addItem((m <= nomesMes.size() ? nomesMes[m - 1] : juce::String(m)) + " (" + juce::String(n) + ")", 100 + m);
        for (auto& [d, n] : porDia) comboDia_->addItem(juce::String(d) + " (" + juce::String(n) + ")", 100 + d);
        if (comboMes_->indexOfItemId(100 + filtroMesAtual_) < 0) filtroMesAtual_ = filtroDiaAtual_ = 0;
        if (comboDia_->indexOfItemId(100 + filtroDiaAtual_) < 0) filtroDiaAtual_ = 0;
        comboMes_->setSelectedId(filtroMesAtual_ ? 100 + filtroMesAtual_ : 1, juce::dontSendNotification);
        comboDia_->setSelectedId(filtroDiaAtual_ ? 100 + filtroDiaAtual_ : 1, juce::dontSendNotification);
        comboMes_->setEnabled(filtroAnoAtual_ > 0 && !porMes.empty());
        comboDia_->setEnabled(filtroMesAtual_ > 0 && !porDia.empty());
    }
}

void IntakeWorkspaceComponent::atualizarFiltragem() {
    indicesFiltrados_.clear();

    // PHONE DUMP: só Original Source Medium = Phone Dump ou o registro explícito da importação pelo fluxo
    // PHONE DUMP (container gallery/whatsapp, conversa de WhatsApp). Sem heurística de tag, caminho ou texto do medium.
    auto ehDump = [](const ItemIntake& it) -> bool {
        return OriginalSourceMediumVocabulary::itemIsPhoneDump(it.sourceMedia.toStdString(), it.containerOrigem.toStdString(), it.collection.toStdString());
    };

    for (size_t i = 0; i < todosItens_.size(); ++i) {
        const auto& it = todosItens_[i];
        int a = 0, m = 0, d = 0;
        const bool temData = partesDaData(it.dataCriacao, a, m, d);
        const bool dataOk = filtroAnoAtual_ == 0 ? true
                          : filtroAnoAtual_ == -1 ? !temData
                          : (temData && a == filtroAnoAtual_ && (filtroMesAtual_ == 0 || m == filtroMesAtual_) &&
                             (filtroDiaAtual_ == 0 || d == filtroDiaAtual_));
        if (!dataOk) continue;

        const bool dump = ehDump(it);
        bool catOk = false;
        if (filtroCategoriaAtual_ == "ALL") {
            catOk = true;
        } else if (filtroCategoriaAtual_ == "PhoneDump") {
            catOk = dump;
        } else {
            catOk = (!dump && it.categoria == filtroCategoriaAtual_);
        }
        if (catOk) {
            indicesFiltrados_.push_back(static_cast<int>(i));
        }
    }

    auto updatePill = [](juce::TextButton* btn, bool active) {
        if (auto* pill = dynamic_cast<PillButton*>(btn)) {
            pill->ativo = active;
            pill->repaint();
        }
    };

    updatePill(btnFiltroAll_.get(), filtroCategoriaAtual_ == "ALL");
    updatePill(btnFiltroAudio_.get(), filtroCategoriaAtual_ == "Audio");
    updatePill(btnFiltroVideo_.get(), filtroCategoriaAtual_ == "Video");
    updatePill(btnFiltroImage_.get(), filtroCategoriaAtual_ == "Image");
    updatePill(btnFiltroDoc_.get(), filtroCategoriaAtual_ == "Document");
    updatePill(btnFiltroOther_.get(), filtroCategoriaAtual_ == "Other");
    updatePill(btnFiltroPhoneDump_.get(), filtroCategoriaAtual_ == "PhoneDump");

    if (tabela_) tabela_->updateContent();
    if (gridComponent_) {
        gridComponent_->recalcularLayout();
        gridComponent_->repaint();
    }
    repaint();
}

void IntakeWorkspaceComponent::atualizarContagens() {
    atualizarPainelMetadados();  // toda mudança de seleção passa por aqui
    contagemAudio_ = 0;
    contagemVideo_ = 0;
    contagemImage_ = 0;
    contagemDoc_ = 0;
    contagemOther_ = 0;
    contagemPhoneDump_ = 0;

    // PHONE DUMP: só Original Source Medium = Phone Dump ou o registro explícito da importação pelo fluxo
    // PHONE DUMP (container gallery/whatsapp, conversa de WhatsApp). Sem heurística de tag, caminho ou texto do medium.
    auto ehDump = [](const ItemIntake& it) -> bool {
        return OriginalSourceMediumVocabulary::itemIsPhoneDump(it.sourceMedia.toStdString(), it.containerOrigem.toStdString(), it.collection.toStdString());
    };

    for (const auto& item : todosItens_) {
        if (ehDump(item)) {
            contagemPhoneDump_++;
        } else {
            if (item.categoria == "Audio") contagemAudio_++;
            else if (item.categoria == "Video") contagemVideo_++;
            else if (item.categoria == "Image") contagemImage_++;
            else if (item.categoria == "Document") contagemDoc_++;
            else contagemOther_++;
        }
    }

    // Item 1 (lista nova de hoje): a contagem/estado dos botões de lote tem
    // que bater com o que a ação de verdade vai atingir — só o que está
    // presente sob o filtro atual, mesmo que esteja marcado "selecionado"
    // por baixo do pano.
    int totalSelecionados = 0;
    for (int idx : indicesFiltrados_) {
        if (idx >= 0 && idx < static_cast<int>(todosItens_.size()) && todosItens_[static_cast<size_t>(idx)].selecionado)
            totalSelecionados++;
    }

    if (btnFiltroAll_) btnFiltroAll_->setButtonText(i18n::t("intake.filtro_all").replace("{n}", juce::String(todosItens_.size())));
    if (btnFiltroAudio_) btnFiltroAudio_->setButtonText(i18n::t("intake.filtro_audio").replace("{n}", juce::String(contagemAudio_)));
    if (btnFiltroVideo_) btnFiltroVideo_->setButtonText(i18n::t("intake.filtro_video").replace("{n}", juce::String(contagemVideo_)));
    if (btnFiltroImage_) btnFiltroImage_->setButtonText(i18n::t("intake.filtro_image").replace("{n}", juce::String(contagemImage_)));
    if (btnFiltroDoc_) btnFiltroDoc_->setButtonText(i18n::t("intake.filtro_doc").replace("{n}", juce::String(contagemDoc_)));
    if (btnFiltroOther_) btnFiltroOther_->setButtonText(i18n::t("intake.filtro_other").replace("{n}", juce::String(contagemOther_)));
    if (btnFiltroPhoneDump_) btnFiltroPhoneDump_->setButtonText(i18n::t("intake.filtro_phone_dump").replace("{n}", juce::String(contagemPhoneDump_)));

    if (lblContadorTotal_) {
        lblContadorTotal_->setText(i18n::t("intake.ativos_contador").replace("{n}", juce::String(todosItens_.size())), juce::dontSendNotification);
    }

    if (lblSubtitulo_) {
        lblSubtitulo_->setText(i18n::t("intake.selecionados").replace("{n}", juce::String(totalSelecionados)), juce::dontSendNotification);
        repaint(tituloFiltro_);
    }

    if (btnConfirmarSelecao_) {
        btnConfirmarSelecao_->setEnabled(totalSelecionados > 0);
        btnConfirmarSelecao_->setAlpha(totalSelecionados > 0 ? 1.0f : 0.4f);
    }

    if (btnConfirmarTodos_) {
        // "Send All" agora significa "todos os visíveis sob o filtro atual".
        btnConfirmarTodos_->setEnabled(!indicesFiltrados_.empty());
        btnConfirmarTodos_->setAlpha(!indicesFiltrados_.empty() ? 1.0f : 0.4f);
    }

    atualizarBotaoRejeitar();

    if (chkSelectAllHeader_) {
        bool allSel = !indicesFiltrados_.empty();
        for (int idx : indicesFiltrados_) {
            if (idx >= 0 && idx < static_cast<int>(todosItens_.size())) {
                if (!todosItens_[static_cast<size_t>(idx)].selecionado) {
                    allSel = false;
                    break;
                }
            }
        }
        chkSelectAllHeader_->setToggleState(allSel, juce::dontSendNotification);
    }
}

void IntakeWorkspaceComponent::recarregar() {
    MATRIZ_TRACE("IntakeWorkspaceComponent::recarregar");
    if (snapshotPendente_) {
        recarregarAoTerminarSnapshot_ = true;
        return;
    }
    recarregarAoTerminarSnapshot_ = false;
    snapshotPendente_ = true;

    const int geracao = ++geracaoSnapshot_;
    juce::Component::SafePointer<IntakeWorkspaceComponent> safeThis(this);
    ProjetoAberto* proj = &projeto_;

    poolSnapshot_.addJob([safeThis, proj, geracao]() {
        std::vector<ItemResumo> quarentena;
        try {
            quarentena = proj->listarItensEmQuarentena();
            proj->carregarMarcasRDoBanco();  // as marcas R chegam junto com a lista (nunca consultadas na message thread)
        } catch (const std::exception&) {
            return;
        }

        juce::MessageManager::callAsync([safeThis, geracao, quarentena = std::move(quarentena)]() mutable {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            if (geracao != self->geracaoSnapshot_) return;
            MATRIZ_TRACE("IntakeWorkspaceComponent::aplicarSnapshot");
            self->snapshotPendente_ = false;
            if (self->gridComponent_) self->gridComponent_->esquecerSemMiniatura();
            self->aplicarItensQuarentena(std::move(quarentena));
            if (self->gridComponent_)
                for (const auto& it : self->todosItens_)
                    if (self->itemEhContainer(it) && ehContainerGrafico(it.containerOrigem))
                        self->gridComponent_->esquecerMiniaturaDoItem(it.id);
            if (self->recarregarAoTerminarSnapshot_) {
                self->recarregarAoTerminarSnapshot_ = false;
                self->recarregar();
            }
        });
    });
}

std::set<std::string> IntakeWorkspaceComponent::itensSelecionados() const {
    // Item 1 (lista nova de hoje): só os que estão presentes sob o filtro
    // ativo — os escondidos por um filtro (media type etc.) não entram,
    // mesmo que continuem marcados "selecionado" por baixo do pano.
    std::set<std::string> ids;
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        const auto& item = todosItens_[static_cast<size_t>(idx)];
        if (!item.selecionado) continue;
        ids.insert(item.id);
        // Container de importação: agir sobre ele = agir sobre todos os arquivos dele (metadado em lote, R, aprovar).
        if (itemEhContainer(item))
            for (const auto& m : projeto_.membrosDoNest(item.containerNestId)) ids.insert(m);
    }
    return ids;
}

void IntakeWorkspaceComponent::selecionarTodos(bool selecionar) {
    for (int idx : indicesFiltrados_) {
        if (idx >= 0 && idx < static_cast<int>(todosItens_.size())) {
            todosItens_[static_cast<size_t>(idx)].selecionado = selecionar;
        }
    }
    if (tabela_) {
        tabela_->updateContent();
        tabela_->repaint();
    }
    if (gridComponent_) {
        gridComponent_->repaint();
    }
    atualizarContagens();
}

void IntakeWorkspaceComponent::selecionarPorCategoria(const juce::String& categoria) {
    for (auto& item : todosItens_) {
        if (categoria == "ALL" || item.categoria == categoria) {
            item.selecionado = true;
        } else {
            item.selecionado = false;
        }
    }
    if (tabela_) {
        tabela_->updateContent();
        tabela_->repaint();
    }
    if (gridComponent_) {
        gridComponent_->repaint();
    }
    atualizarContagens();
}

// Fase 2b (freeze de edição em lote): mesmo tratamento de
// aplicarCreatorAosSelecionados/aplicarSubjectAosSelecionados/
// aplicarEventDateAosSelecionados abaixo -- a gravação em si sai da
// message thread via poolMetadadoLote_ e vira UMA transação com UM
// evento amplo, em vez de N chamadas síncronas de salvarMetadado. Content
// (collection_type) e Source Medium tinham ficado de fora da primeira
// passada desta correção -- Source Medium em especial é o campo do
// freeze originalmente diagnosticado nesta sessão.
void IntakeWorkspaceComponent::aplicarColecaoAosSelecionados(const juce::String& colecao) {
    std::vector<std::string> ids;
    // Item 1 (lista nova de hoje): só os selecionados que também estão
    // visíveis sob o filtro ativo.
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        auto& item = todosItens_[static_cast<size_t>(idx)];
        if (item.selecionado) {
            ids.push_back(item.id);
            item.collection = colecao;
        }
    }
    if (!ids.empty()) {
        ProjetoAberto* projeto = &projeto_;
        std::string colecaoStd = colecao.toStdString();
        poolMetadadoLote_.addJob([projeto, ids, colecaoStd] {
            projeto->salvarMetadadoEmLote(ids, {{"collection_type", colecaoStd}});
        });
    }

    if (tabela_) tabela_->repaint();
    if (gridComponent_) gridComponent_->repaint();
    atualizarContagens();
}

void IntakeWorkspaceComponent::definirColecaoItem(const std::string& itemId, const juce::String& colecao) {
    projeto_.salvarMetadado(itemId, "collection_type", colecao.toStdString());
}

void IntakeWorkspaceComponent::aplicarOriginalSourceMediumAosSelecionados(const std::string& sourceMediaJson) {
    std::vector<std::string> ids;
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        auto& item = todosItens_[static_cast<size_t>(idx)];
        if (item.selecionado) {
            ids.push_back(item.id);
            // Mesmo DEVICE que salvarMetadadoEmLote mantém no banco (o lote troca só o meio).
            const auto device = OriginalSourceMediumInfo::deserialize(item.sourceMedia.toStdString()).recordingDevice;
            item.sourceMedia = juce::String::fromUTF8(manterDeviceDoItem(sourceMediaJson, device).c_str());
        }
    }
    if (!ids.empty()) {
        ProjetoAberto* projeto = &projeto_;
        std::string valorStd = sourceMediaJson;
        poolMetadadoLote_.addJob([projeto, ids, valorStd] {
            projeto->salvarMetadadoEmLote(ids, {{"source_media", valorStd}});
        });
    }
    if (tabela_) tabela_->repaint();
    if (gridComponent_) gridComponent_->repaint();
}

void IntakeWorkspaceComponent::definirSourceMediaItem(const std::string& itemId, const std::string& sourceMediaJson) {
    projeto_.salvarMetadado(itemId, "source_media", sourceMediaJson);
}

// CREATOR e SUBJECT (item 2 da correção "BACKUP e INTAKE"): mesma coluna
// que a ficha grava (dc_creator/dc_subject via ProjetoAberto::salvarMetadado)
// — o valor aparece direto no ASSET & USER METADATA e no DUBLIN CORE do
// item assim que ele chega no Grid, sem estrutura própria de metadado.
// Fase 2b (freeze de edição em lote): a gravação em si (N itens) sai da
// message thread via poolMetadadoLote_ e vira UMA transação com UM evento
// amplo no fim, em vez de N chamadas síncronas de salvarMetadado — cada
// uma com sua própria transação implícita e seu próprio evento.
void IntakeWorkspaceComponent::aplicarCreatorAosSelecionados(const juce::String& valor) {
    std::vector<std::string> ids;
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        if (todosItens_[static_cast<size_t>(idx)].selecionado)
            ids.push_back(todosItens_[static_cast<size_t>(idx)].id);
    }
    if (ids.empty()) return;
    ProjetoAberto* projeto = &projeto_;
    std::string valorStd = valor.toStdString();
    poolMetadadoLote_.addJob([projeto, ids, valorStd] {
        projeto->salvarMetadadoEmLote(ids, {{"dc_creator", valorStd}});
    });
}

void IntakeWorkspaceComponent::aplicarSubjectAosSelecionados(const juce::String& valor) {
    std::vector<std::string> ids;
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        if (todosItens_[static_cast<size_t>(idx)].selecionado)
            ids.push_back(todosItens_[static_cast<size_t>(idx)].id);
    }
    if (ids.empty()) return;
    ProjetoAberto* projeto = &projeto_;
    std::string valorStd = valor.toStdString();
    poolMetadadoLote_.addJob([projeto, ids, valorStd] {
        projeto->salvarMetadadoEmLote(ids, {{"dc_subject", valorStd}});
    });
}

void IntakeWorkspaceComponent::aplicarEventDateAosSelecionados(const juce::String& valor) {
    juce::String v = valor.trim();
    if (v.isEmpty()) return;
    // Item 1 (lista nova de hoje): só os selecionados visíveis sob o
    // filtro ativo. Item 3: EVENT DATE passa a alimentar também DATE
    // CREATED (dc_created, a mesma coluna que a lista exibe) — a partir de
    // agora o arquivo assume esta data como data de criação. A atualização
    // do modelo em memória (item.dataCriacao) e o refresh da tabela
    // continuam síncronos (baratos, só tocam o vetor local); só a gravação
    // no banco sai da message thread.
    //
    // Exceção: informado SÓ o ano e ele é igual ao ano do DATE CREATED
    // original (metadado do arquivo, não a data do ingest), o DATE CREATED
    // fica como estava (ex.: 2015-04-17 10:30:24 não vira "2015"). Decidido
    // item a item.
    const bool soAno = v.length() == 4 && v.containsOnly("0123456789");
    std::vector<std::string> ids;
    std::set<std::pair<std::string, std::string>> manterDateCreated;
    for (int idx : indicesFiltrados_) {
        if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
        auto& item = todosItens_[static_cast<size_t>(idx)];
        if (!item.selecionado) continue;
        ids.push_back(item.id);
        if (const int anoNovo = v.substring(0, 4).getIntValue(); anoNovo > 0) item.ano = anoNovo;  // filtro por ano
        if (soAno && item.dataCriacaoDoMetadado && item.dataCriacao.substring(0, 4) == v) {
            manterDateCreated.insert({item.id, "dc_created"});
            continue;
        }
        item.dataCriacao = v;
        item.dataCriacaoDoMetadado = true;
    }
    if (ids.empty()) return;
    ProjetoAberto* projeto = &projeto_;
    std::string vStd = v.toStdString();
    poolMetadadoLote_.addJob([projeto, ids, vStd, manterDateCreated] {
        projeto->salvarMetadadoEmLote(ids, {{"ano", vStd}, {"dc_created", vStd}}, manterDateCreated);
    });
    if (tabela_) {
        tabela_->updateContent();
        tabela_->repaint();
    }
    atualizarContagens();
    atualizarComboAno();
}

std::vector<juce::String> IntakeWorkspaceComponent::valoresExistentesParaColuna(const std::string& coluna) const {
    std::vector<juce::String> out;
    // CREATOR/SUBJECT: a mesma lista da ficha do Metadata (histórico + valores
    // gravados) — os dois lados sugerem o que o outro já usou.
    if (coluna == "dc_creator" || coluna == "dc_subject") {
        try {
            for (const auto& v : matriz::ficha::AutocompleteRepository::listar(projeto_.projeto().registro(), coluna))
                out.push_back(juce::String::fromUTF8(v.c_str()));
        } catch (...) {}
        return out;
    }
    try {
        auto stmt = projeto_.projeto().registro().prepare(
            "SELECT DISTINCT " + coluna + " FROM item WHERE " + coluna + " IS NOT NULL AND TRIM(" + coluna + ") <> '' "
            "ORDER BY " + coluna + " COLLATE NOCASE ASC");
        while (stmt.step()) out.push_back(juce::String(stmt.columnText(0)));
    } catch (...) {}
    return out;
}

void IntakeWorkspaceComponent::mostrarEditorCreatorLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<AutocompleteLotePopupContent>(
        "Set CREATOR for Selected", "Creator name...",
        valoresExistentesParaColuna("dc_creator"),
        corDoNivelHierarquia(matriz::consolidacao::NivelHierarquia::Artista),
        [this](const juce::String& valor) { aplicarCreatorAosSelecionados(valor); });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorSubjectLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<AutocompleteLotePopupContent>(
        "Set SUBJECT for Selected", "Subject...",
        valoresExistentesParaColuna("dc_subject"),
        corDoNivelHierarquia(matriz::consolidacao::NivelHierarquia::Subject),
        [this](const juce::String& valor) { aplicarSubjectAosSelecionados(valor); });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorEventDateLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<AutocompleteLotePopupContent>(
        "Set EVENT DATE for Selected", "YYYY",
        valoresExistentesParaColuna("ano"),
        corDoNivelHierarquia(matriz::consolidacao::NivelHierarquia::Ano),
        [this](const juce::String& valor) { aplicarEventDateAosSelecionados(valor); });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorContentLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<ContentLotePopupContent>(
        corDoNivelHierarquia(matriz::consolidacao::NivelHierarquia::ContentType),
        [this](const juce::String& valor) { aplicarColecaoAosSelecionados(traduzirContent(valor, false)); });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorOriginalSourceMediumLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<OriginalSourceMediumPopupContent>(
        "", true, [this](const std::string& val) {
            aplicarOriginalSourceMediumAosSelecionados(val);
        });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorOriginalSourceMedium(int itemIndex, juce::Rectangle<int> screenBounds) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;
    std::string itemId = todosItens_[static_cast<size_t>(itemIndex)].id;
    std::string currentVal = todosItens_[static_cast<size_t>(itemIndex)].sourceMedia.toStdString();

    auto content = std::make_unique<OriginalSourceMediumPopupContent>(
        currentVal, false, [this, itemId, itemIndex](const std::string& val) {
            auto selecionados = itensSelecionados();
            std::vector<std::string> idsParaAplicar;
            if (selecionados.count(itemId) > 0 && selecionados.size() > 1) {
                idsParaAplicar.assign(selecionados.begin(), selecionados.end());
            } else {
                idsParaAplicar.push_back(itemId);
            }

            for (const auto& idStr : idsParaAplicar) {
                definirSourceMediaItem(idStr, val);
                for (auto& it : todosItens_) {
                    if (it.id == idStr) {
                        it.sourceMedia = juce::String::fromUTF8(val.c_str());
                        break;
                    }
                }
            }
            if (tabela_) tabela_->repaint();
            if (gridComponent_) gridComponent_->repaint();
        });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::mostrarEditorGeolocationLote(juce::Rectangle<int> screenBounds) {
    auto content = std::make_unique<GeoLocationPopupContent>(
        projeto_.projeto().registro(), true,
        [this](const std::string& coords, const std::string& addr, const std::string& city, const std::string& state, const std::string& country) {
            aplicarGeolocationAosSelecionados(coords, addr, city, state, country);
        });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void IntakeWorkspaceComponent::aplicarGeolocationAosSelecionados(const std::string& coords, const std::string& addr, const std::string& city, const std::string& state, const std::string& country) {
    matriz::analytics::AssetGeolocation geoTemplate;
    if (!coords.empty()) {
        auto commaPos = coords.find(',');
        if (commaPos != std::string::npos) {
            try {
                double lat = std::stod(coords.substr(0, commaPos));
                double lng = std::stod(coords.substr(commaPos + 1));
                if (lat >= -90.0 && lat <= 90.0 && lng >= -180.0 && lng <= 180.0) {
                    geoTemplate.latitude = lat;
                    geoTemplate.longitude = lng;
                    geoTemplate.source = matriz::analytics::GeoSource::UserCoordinates;
                }
            } catch (...) {}
        }
    }
    if (!addr.empty()) {
        geoTemplate.formattedAddress = addr;
        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserAddress;
    }
    if (!city.empty()) {
        geoTemplate.city = city;
        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserCity;
    }
    if (!state.empty()) {
        geoTemplate.stateProvince = state;
        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserState;
    }
    if (!country.empty()) {
        geoTemplate.country = country;
        if (geoTemplate.source == matriz::analytics::GeoSource::None) geoTemplate.source = matriz::analytics::GeoSource::UserCountry;
    }

    if (!geoTemplate.hasAnyLocationData()) return;

    // Item 1 (lista nova de hoje): itensSelecionados() já respeita o filtro
    // ativo — o que estiver escondido não entra, mesmo marcado.
    auto selecionados = itensSelecionados();
    std::vector<std::string> ids(selecionados.begin(), selecionados.end());
    if (ids.empty()) return;

    // Fase 2b: mesmo motivo dos outros aplicarXAosSelecionados -- sai da message thread, uma transação só.
    // Por item, só muda o que foi digitado (salvarEmLote gravava a linha inteira: digitar só a cidade APAGAVA o GPS
    // do iPhone), com retrato de antes para o Undo.
    ProjetoAberto* projeto = &projeto_;
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    auto gravar = [projeto, ids, geoTemplate, seguro] {
        if (seguro == nullptr) return;
        seguro->poolMetadadoLote_.addJob([projeto, ids, geoTemplate] {
            using matriz::analytics::GeoSource;
            auto& db = projeto->projeto().registro();
            std::unique_lock<std::recursive_mutex> lock(projeto->writeMutex());
            projeto->registrarUndoGeo(ids.size() == 1 ? "Edit geo location" : "Batch edit: geo location", ids);  // antes de gravar
            auto antes = projeto->retratoGeo(ids);
            try {
                db.exec("BEGIN IMMEDIATE");
                for (const auto& [id, existente] : antes) {
                    auto g = existente.value_or(matriz::analytics::AssetGeolocation{});
                    g.assetId = id;
                    const bool deUsuario = geoTemplate.hasValidCoordinates() || g.source == GeoSource::None ||
                                           g.source == GeoSource::UserCoordinates || g.source == GeoSource::UserAddress ||
                                           g.source == GeoSource::UserCity || g.source == GeoSource::UserState ||
                                           g.source == GeoSource::UserCountry;
                    if (geoTemplate.hasValidCoordinates()) { g.latitude = geoTemplate.latitude; g.longitude = geoTemplate.longitude; }
                    if (geoTemplate.formattedAddress) g.formattedAddress = geoTemplate.formattedAddress;
                    if (geoTemplate.city) g.city = geoTemplate.city;
                    if (geoTemplate.stateProvince) g.stateProvince = geoTemplate.stateProvince;
                    if (geoTemplate.country) g.country = geoTemplate.country;
                    if (deUsuario) {
                        if (g.hasValidCoordinates())   g.source = GeoSource::UserCoordinates;
                        else if (g.formattedAddress)   g.source = GeoSource::UserAddress;
                        else if (g.city)               g.source = GeoSource::UserCity;
                        else if (g.stateProvince)      g.source = GeoSource::UserState;
                        else if (g.country)            g.source = GeoSource::UserCountry;
                    }
                    matriz::analytics::AssetGeolocationRepository::salvar(db, g);
                }
                db.exec("COMMIT");
            } catch (...) {
                try { db.exec("ROLLBACK"); } catch (...) {}
                return;
            }
        });
        if (seguro->tabela_) seguro->tabela_->repaint();
    };

    // Coordenadas novas em arquivos com GPS da câmera: pergunta antes, dizendo quantos.
    if (geoTemplate.hasValidCoordinates()) {
        int comGpsDoArquivo = 0;
        for (const auto& id : ids) {
            auto g = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(projeto_.projeto().registro(), id);
            if (g && g->source == matriz::analytics::GeoSource::EmbeddedMetadata && g->hasValidCoordinates()) ++comGpsDoArquivo;
        }
        if (comGpsDoArquivo > 0) {
            matriz::ui::geoacoes::confirmarTrocaDoGpsDoArquivo(comGpsDoArquivo, juce::String(coords), gravar);
            return;
        }
    }
    gravar();
}

void IntakeWorkspaceComponent::confirmarSelecaoParaGrid() {
    // Item 1 (lista nova de hoje): idem — só o que está selecionado E
    // visível sob o filtro atual.
    auto selecionados = itensSelecionados();
    std::vector<std::string> ids = projeto_.semMarcadosR(std::vector<std::string>(selecionados.begin(), selecionados.end()));
    if (ids.empty()) return;

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "intake_confirm", "Promoting to GRID", static_cast<int>(ids.size()), nullptr,
        "Promoting " + juce::String(ids.size()) + " selected items to GRID...");

    projeto_.confirmarLoteGrid(ids);
    recarregar();

    ProgressoGlobal::obterInstancia().concluirTarefa(
        "intake_confirm", juce::String(ids.size()) + " items confirmed to GRID");

    if (aoConfirmarParaGrid) aoConfirmarParaGrid();
}

void IntakeWorkspaceComponent::confirmarTodosParaGrid() {
    // Item 1 (lista nova de hoje): "ALL" passa a significar "todos os
    // visíveis sob o filtro atual" — não literalmente cada item do INTAKE.
    if (indicesFiltrados_.empty()) return;
    std::vector<std::string> ids;
    ids.reserve(indicesFiltrados_.size());
    for (int idx : indicesFiltrados_) {
        if (idx >= 0 && idx < static_cast<int>(todosItens_.size()))
            ids.push_back(todosItens_[static_cast<size_t>(idx)].id);
    }
    ids = projeto_.semMarcadosR(ids);  // os marcados com R ficam no INTAKE
    if (ids.empty()) return;

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "intake_confirm_all", "Promoting to GRID", static_cast<int>(ids.size()), nullptr,
        "Promoting all " + juce::String(ids.size()) + " intake items to GRID...");

    projeto_.confirmarLoteGrid(ids);
    recarregar();

    ProgressoGlobal::obterInstancia().concluirTarefa(
        "intake_confirm_all", "All " + juce::String(ids.size()) + " items confirmed to GRID");

    if (aoConfirmarParaGrid) aoConfirmarParaGrid();
}

void IntakeWorkspaceComponent::atualizarBotaoRejeitar() {
    if (!btnRemoverSelecao_) return;
    const int n = static_cast<int>(marcadosR_.size());
    juce::String texto = i18n::t("intake.btn_rejeitar_selecionados");
    if (n > 0) texto << " (" << n << ")";
    btnRemoverSelecao_->setButtonText(texto);
    btnRemoverSelecao_->setTooltip(i18n::t("intake.reject_dica").replace("{n}", juce::String(n)));
    btnRemoverSelecao_->setEnabled(n > 0);
    btnRemoverSelecao_->setAlpha(n > 0 ? 1.0f : 0.4f);
}

void IntakeWorkspaceComponent::alternarMarcaRDosSelecionados() {
    // O R vale ONDE A SETA ESTÁ: o item em foco, marcado ou não (não precisa estar com o tique). Só quando o
    // foco está dentro de uma seleção (ex.: Shift+seta) o R vale para a seleção inteira; sem navegação pelo
    // teclado, vale a seleção de sempre (tiques/laço).
    auto selecionados = itensSelecionados();
    std::string idFoco;
    if (const int pos = posicaoDoFoco(); pos >= 0)
        idFoco = todosItens_[static_cast<size_t>(indicesFiltrados_[static_cast<size_t>(pos)])].id;
    std::set<std::string> alvo;
    if (focoVisivel_ && !idFoco.empty() && selecionados.count(idFoco) == 0) alvo.insert(idFoco);
    else if (!selecionados.empty()) alvo = selecionados;
    else if (!idFoco.empty()) alvo.insert(idFoco);
    if (alvo.empty()) return;
    projeto_.aoMarcasRRevertidas = [safeThis = juce::Component::SafePointer<IntakeWorkspaceComponent>(this)]() {
        if (!safeThis) return;
        safeThis->marcadosR_ = safeThis->projeto_.idsMarcadosR();
        safeThis->atualizarBotaoRejeitar();
        safeThis->repaint();
    };
    projeto_.alternarMarcaR(std::vector<std::string>(alvo.begin(), alvo.end()));
    marcadosR_ = projeto_.idsMarcadosR();
    atualizarBotaoRejeitar();
    if (tabela_) tabela_->repaint();
    if (gridComponent_) gridComponent_->repaint();
}

void IntakeWorkspaceComponent::alternarMarcaRDeItem(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;
    const std::string itemId = todosItens_[static_cast<size_t>(itemIndex)].id;
    // Menu de contexto sobre um item dentro de uma seleção múltipla vale pra seleção inteira.
    auto selecionados = itensSelecionados();
    std::vector<std::string> ids;
    if (selecionados.count(itemId) > 0 && selecionados.size() > 1) ids.assign(selecionados.begin(), selecionados.end());
    else ids.push_back(itemId);
    projeto_.aoMarcasRRevertidas = [safeThis = juce::Component::SafePointer<IntakeWorkspaceComponent>(this)]() {
        if (!safeThis) return;
        safeThis->marcadosR_ = safeThis->projeto_.idsMarcadosR();
        safeThis->atualizarBotaoRejeitar();
        safeThis->repaint();
    };
    projeto_.alternarMarcaR(ids);
    marcadosR_ = projeto_.idsMarcadosR();
    atualizarBotaoRejeitar();
    if (tabela_) tabela_->repaint();
    if (gridComponent_) gridComponent_->repaint();
}

void IntakeWorkspaceComponent::rejeitarMarcados() {
    // Só o que tem R. A seleção atual é ignorada, sempre (camada de segurança contra seleção mal feita).
    if (marcadosR_.empty()) return;
    projeto_.rejeitarMarcadosR();
    recarregar();
}

int IntakeWorkspaceComponent::posicaoDoFoco() const {
    if (focoId_.empty()) return -1;
    auto idNaPosicao = [this](int p) -> const std::string* {
        if (p < 0 || p >= static_cast<int>(indicesFiltrados_.size())) return nullptr;
        const int real = indicesFiltrados_[static_cast<size_t>(p)];
        return (real >= 0 && real < static_cast<int>(todosItens_.size())) ? &todosItens_[static_cast<size_t>(real)].id : nullptr;
    };
    if (const auto* id = idNaPosicao(posicaoFocoCache_); id && *id == focoId_) return posicaoFocoCache_;
    for (int p = 0; p < static_cast<int>(indicesFiltrados_.size()); ++p) {
        const int real = indicesFiltrados_[static_cast<size_t>(p)];
        if (real >= 0 && real < static_cast<int>(todosItens_.size()) && todosItens_[static_cast<size_t>(real)].id == focoId_)
            return posicaoFocoCache_ = p;
    }
    return -1;
}

void IntakeWorkspaceComponent::garantirCardVisivel(int posicao) {
    if (!gridViewport_ || !gridComponent_) return;
    const auto card = gridComponent_->boundsDoCard(posicao);
    if (card.isEmpty()) return;
    const auto vista = gridViewport_->getViewArea();
    const int folga = 14;
    int y = vista.getY();
    if (card.getY() - folga < vista.getY()) y = card.getY() - folga;
    else if (card.getBottom() + folga > vista.getBottom()) y = card.getBottom() + folga - vista.getHeight();
    if (y != vista.getY()) gridViewport_->setViewPosition(vista.getX(), juce::jmax(0, y));
}

void IntakeWorkspaceComponent::moverFocoGrade(int dx, int dy, bool estender) {
    const int total = static_cast<int>(indicesFiltrados_.size());
    const int colunas = gridComponent_ ? gridComponent_->colunas() : 0;
    if (total == 0 || colunas <= 0) return;

    int atual = posicaoDoFoco();
    int novo;
    if (atual < 0) {  // primeira seta: parte do primeiro selecionado (ou do primeiro card), sem andar ainda
        novo = 0;
        for (int p = 0; p < total; ++p) {
            const int real = indicesFiltrados_[static_cast<size_t>(p)];
            if (real >= 0 && real < static_cast<int>(todosItens_.size()) && todosItens_[static_cast<size_t>(real)].selecionado) {
                novo = p;
                break;
            }
        }
        atual = novo;
    } else if (dx != 0) {
        novo = juce::jlimit(0, total - 1, atual + dx);  // fim da linha -> começo da seguinte, e o inverso
    } else if (dy > 0) {
        novo = (atual + colunas < total) ? atual + colunas
             : (atual / colunas < (total - 1) / colunas ? total - 1 : atual);  // última linha, mais curta
    } else {
        novo = (atual - colunas >= 0) ? atual - colunas : atual;
    }
    focoVisivel_ = true;

    auto doItem = [this](int pos) -> ItemIntake* {
        const int real = indicesFiltrados_[static_cast<size_t>(pos)];
        return (real >= 0 && real < static_cast<int>(todosItens_.size())) ? &todosItens_[static_cast<size_t>(real)] : nullptr;
    };

    bool selecaoMudou = false;
    if (estender) {
        // Âncora = a do último clique/seta; o que sai do intervalo é desmarcado, o que entra é marcado.
        int ancora = ultimaPosicaoClicadaParaSelecao_;
        if (ancora < 0 || ancora >= total) ancora = atual;
        const int antesDe = std::min(ancora, atual), antesAte = std::max(ancora, atual);
        const int depoisDe = std::min(ancora, novo), depoisAte = std::max(ancora, novo);
        for (int p = antesDe; p <= antesAte; ++p)
            if (p < depoisDe || p > depoisAte)
                if (auto* it = doItem(p)) it->selecionado = false;
        for (int p = depoisDe; p <= depoisAte; ++p)
            if (auto* it = doItem(p)) it->selecionado = true;
        ultimaPosicaoClicadaParaSelecao_ = ancora;
        selecaoMudou = true;
    } else {
        ultimaPosicaoClicadaParaSelecao_ = novo;  // a próxima Shift+seta parte daqui
    }

    if (auto* it = doItem(novo)) focoId_ = it->id;
    if (selecaoMudou) ultimoClicadoId_ = focoId_;  // Shift+seta: o painel acompanha o item do foco
    posicaoFocoCache_ = novo;
    garantirCardVisivel(novo);
    if (selecaoMudou) {
        atualizarContagens();
        if (tabela_) tabela_->repaint();
    }
    if (gridComponent_) gridComponent_->repaint();
}

void IntakeWorkspaceComponent::alternarPreviewDoFoco() {
    if (janelaPreview_) {  // Espaço de novo fecha
        janelaPreview_.reset();
        return;
    }
    int pos = posicaoDoFoco();
    if (pos < 0) return;
    const int real = indicesFiltrados_[static_cast<size_t>(pos)];
    if (real < 0 || real >= static_cast<int>(todosItens_.size())) return;
    abrirPreviewDoItem(todosItens_[static_cast<size_t>(real)].id);
}

void IntakeWorkspaceComponent::abrirGridDoContainer(const std::string& itemId) {
    std::string nestId;
    for (const auto& it : todosItens_) if (it.id == itemId) { nestId = it.containerNestId; break; }
    if (nestId.empty()) return;
    auto membros = projeto_.membrosDoNest(nestId);
    if (membros.empty()) return;
    for (const auto& it : todosItens_) {
        if (it.id != itemId || it.containerOrigem != "ddp") continue;
        // Master DDP: o duplo clique abre o player de CD (disco, faixas e reprodução), não a grade de miniaturas.
        abrirContainerDdp(projeto_, nestId);
        return;
    }
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    auto* conteudo = new ContainerGridComponent(
        projeto_, std::move(membros),
        [seguro](std::vector<std::string> desmarcados) {
            if (seguro == nullptr || desmarcados.empty()) return;
            seguro->projeto_.removerDoContainer(desmarcados);
            seguro->recarregar();
        },
        [] {
            juce::MessageManager::callAsync([] {
                for (int i = juce::Desktop::getInstance().getNumComponents(); --i >= 0;)
                    if (auto* dw = dynamic_cast<juce::DialogWindow*>(juce::Desktop::getInstance().getComponent(i)))
                        if (dw->getName() == "container-grid") { dw->exitModalState(0); dw->setVisible(false); return; }
            });
        });
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(conteudo);
    opt.dialogTitle = "container-grid";
    opt.escapeKeyTriggersCloseButton = true;
    opt.resizable = false;
    moldura::lancar(opt, moldura::Secao::Explorador, i18n::t("moldura.trilha.ingest"));
}

void IntakeWorkspaceComponent::abrirPreviewDoItem(const std::string& itemId) {
    if (itemId.empty()) return;
    for (const auto& it : todosItens_)  // container de importação: duplo clique abre o grid de miniaturas (não o preview)
        if (it.id == itemId && itemEhContainer(it) && it.containerOrigem != "whatsapp" && it.containerOrigem != "youtube" &&
            (!ehOrigemProject(it.containerOrigem) || it.containerOrigem == "ddp")) { abrirGridDoContainer(itemId); return; }  // WhatsApp abre a conversa; YouTube, vinil, fita e produção abrem a janela do projeto (preview com a capa); DDP, o player de CD
    janelaPreview_.reset();  // duplo clique em outro arquivo troca o preview
    juce::Component::SafePointer<IntakeWorkspaceComponent> safe(this);
    auto aoFechar = [safe] {
        juce::MessageManager::callAsync([safe] {
            if (safe) safe->janelaPreview_.reset();
        });
    };
    auto aoNavegar = [safe](const std::string& atualId, int direcao) -> std::optional<std::string> {
        if (!safe) return std::nullopt;
        const int p = [&] {
            for (int i = 0; i < static_cast<int>(safe->indicesFiltrados_.size()); ++i) {
                const int r = safe->indicesFiltrados_[static_cast<size_t>(i)];
                if (r >= 0 && r < static_cast<int>(safe->todosItens_.size()) && safe->todosItens_[static_cast<size_t>(r)].id == atualId) return i;
            }
            return -1;
        }();
        const int alvo = p + direcao;
        if (p < 0 || alvo < 0 || alvo >= static_cast<int>(safe->indicesFiltrados_.size())) return std::nullopt;
        const int r = safe->indicesFiltrados_[static_cast<size_t>(alvo)];
        if (r < 0 || r >= static_cast<int>(safe->todosItens_.size())) return std::nullopt;
        return safe->todosItens_[static_cast<size_t>(r)].id;
    };
    auto aoItemMudou = [safe](const std::string& novoId) {
        if (!safe) return;
        safe->focoId_ = novoId;  // o foco acompanha o que o preview mostra
        const int p = safe->posicaoDoFoco();
        if (p >= 0) safe->garantirCardVisivel(p);
        if (safe->gridComponent_) safe->gridComponent_->repaint();
    };
    janelaPreview_ = std::make_unique<FloatingPreviewWindow>(projeto_, itemId, aoFechar, aoNavegar, aoItemMudou, /*somenteLeitura=*/true);
}

bool IntakeWorkspaceComponent::keyPressed(const juce::KeyPress& k) {
    // Campo de texto em foco: a tecla é dele (R digita a letra).
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;
    if (modoVisao_ == ModoVisao::Icones && !indicesFiltrados_.empty()) {
        const int codigo = k.getKeyCode();
        int dx = 0, dy = 0;
        if (codigo == juce::KeyPress::leftKey) dx = -1;
        else if (codigo == juce::KeyPress::rightKey) dx = 1;
        else if (codigo == juce::KeyPress::upKey) dy = -1;
        else if (codigo == juce::KeyPress::downKey) dy = 1;
        if (dx != 0 || dy != 0) {
            moverFocoGrade(dx, dy, mods.isShiftDown());
            return true;
        }
    }
    if (k.getKeyCode() == juce::KeyPress::spaceKey && !mods.isShiftDown()) {  // preview também na visão em lista
        alternarPreviewDoFoco();
        return true;
    }
    const auto c = k.getTextCharacter();
    if (c == 'r' || c == 'R') {
        alternarMarcaRDosSelecionados();
        return true;
    }
    return false;
}

void IntakeWorkspaceComponent::mostrarMenuColecaoParaItem(int itemIndex, juce::Rectangle<int> screenBounds) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;

    juce::PopupMenu menu;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    menu.addItem(1, isPt ? juce::String::fromUTF8("Nenhum (Limpar)") : "None (Clear)");
    menu.addSeparator();

    int id = 2;
    std::map<int, juce::String> idParaNome;
    for (const auto& cat : vocabularioColecoes()) {
        juce::PopupMenu sub;
        for (const auto& item : cat.itens) {
            sub.addItem(id, isPt ? traduzirContent(item, true) : item);
            idParaNome[id] = item;
            id++;
        }
        juce::String grupoNome = cat.grupo;
        if (isPt) {
            if (grupoNome == "AUDIO") grupoNome = juce::String::fromUTF8("ÁUDIO");
            else if (grupoNome == "VIDEO") grupoNome = juce::String::fromUTF8("VÍDEO");
            else if (grupoNome == "IMAGE") grupoNome = juce::String::fromUTF8("IMAGEM");
            else if (grupoNome == "DOCUMENT") grupoNome = juce::String::fromUTF8("DOCUMENTO");
        }
        menu.addSubMenu(grupoNome, sub);
    }

    juce::Component::SafePointer<IntakeWorkspaceComponent> safeThis(this);
    std::string itemId = todosItens_[static_cast<size_t>(itemIndex)].id;

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenBounds),
                       [safeThis, itemId, idParaNome, itemIndex](int result) {
        if (!safeThis || result == 0) return;
        juce::String chosen;
        if (result > 1) {
            auto it = idParaNome.find(result);
            if (it != idParaNome.end()) chosen = it->second;
        }

        auto selecionados = safeThis->itensSelecionados();
        std::vector<std::string> idsParaAplicar;
        if (selecionados.count(itemId) > 0 && selecionados.size() > 1) {
            idsParaAplicar.assign(selecionados.begin(), selecionados.end());
        } else {
            idsParaAplicar.push_back(itemId);
        }

        for (const auto& idStr : idsParaAplicar) {
            safeThis->definirColecaoItem(idStr, chosen);
            for (auto& it : safeThis->todosItens_) {
                if (it.id == idStr) {
                    it.collection = chosen;
                    break;
                }
            }
        }
        if (safeThis->tabela_) safeThis->tabela_->repaint();
        if (safeThis->gridComponent_) safeThis->gridComponent_->repaint();
    });
}

// TableListBoxModel implementation
int IntakeWorkspaceComponent::getNumRows() {
    return static_cast<int>(indicesFiltrados_.size());
}

void IntakeWorkspaceComponent::paintRowBackground(juce::Graphics& g, int rowNumber, int, int, bool rowIsSelected) {
    const auto& tk = tema();
    if (rowIsSelected) {
        g.fillAll(tk.acento.withAlpha(0.25f));
    } else {
        g.fillAll(rowNumber % 2 == 1 ? tk.painelAlt.withAlpha(0.35f) : tk.painel);
    }
    if (rowNumber >= 0 && rowNumber < static_cast<int>(indicesFiltrados_.size())) {
        const int real = indicesFiltrados_[static_cast<size_t>(rowNumber)];
        if (real >= 0 && real < static_cast<int>(todosItens_.size()) && itemMarcadoR(todosItens_[static_cast<size_t>(real)].id))
            g.fillAll(tk.perigo.withAlpha(0.16f));  // R (Reject)
    }
}

void IntakeWorkspaceComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(indicesFiltrados_.size())) return;
    int realIndex = indicesFiltrados_[static_cast<size_t>(rowNumber)];
    if (realIndex < 0 || realIndex >= static_cast<int>(todosItens_.size())) return;

    const auto& item = todosItens_[static_cast<size_t>(realIndex)];
    const auto& tk = tema();

    if (columnId == kColType) {
        juce::Colour corCat = corParaCategoria(item.categoria);
        auto badgeArea = juce::Rectangle<int>(4, 5, width - 8, height - 10);
        g.setColour(corCat);
        g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
        g.drawText(item.categoria.toUpperCase(), badgeArea, juce::Justification::centred, true);
    } else if (columnId == kColName) {
        if (item.containerOrigem.isNotEmpty()) {  // logo da origem antes do nome; o resto desenha como sempre, deslocado
            const int lado = juce::jmin(height - 6, 24);
            desenharSeloContainer(g, juce::Rectangle<int>(4, (height - lado) / 2, lado, lado), item.containerOrigem, 0);
            g.setOrigin(lado + 8, 0);
            width -= lado + 8;
        }
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        int recuoR = 0;
        if (itemMarcadoR(item.id)) {  // selo R (Reject) antes do nome
            juce::Rectangle<int> rBadge(4, (height - 16) / 2, 16, 16);
            g.setColour(tk.perigo);
            g.fillRoundedRectangle(rBadge.toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
            g.drawText("R", rBadge, juce::Justification::centred);
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
            recuoR = 20;
        }
        if (recuoR > 0) {
            g.drawText(item.nomeArquivo, 6 + recuoR, 0, width - 12 - recuoR - (item.offline ? 64 : 0), height, juce::Justification::centredLeft, true);
            if (item.offline) {
                juce::Rectangle<int> offBadge(width - 58 - 6, (height - 18) / 2, 58, 18);
                g.setColour(juce::Colour(0xffef4444));
                g.fillRoundedRectangle(offBadge.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
                g.drawText("OFFLINE", offBadge, juce::Justification::centred);
            }
        } else if (item.offline) {
            int offBadgeW = 58;
            auto offBadge = juce::Rectangle<int>(width - offBadgeW - 6, (height - 18) / 2, offBadgeW, 18);
            g.drawText(item.nomeArquivo, 6, 0, width - offBadgeW - 16, height, juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xffef4444));
            g.fillRoundedRectangle(offBadge.toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
            g.drawText("OFFLINE", offBadge, juce::Justification::centred);
        } else {
            g.drawText(item.nomeArquivo, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
    } else if (columnId == kColOrigin) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
        juce::String extStr = item.extensao.isNotEmpty() ? item.extensao.toUpperCase() : "-";
        g.drawText(extStr, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColDateCreated) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
        // Item 3 (nova lista): a coluna mostra só o ano (AAAA) — a data
        // completa continua guiando a ordenação por esta coluna (ver
        // compareElements, kColDateCreated), só a exibição encolheu.
        juce::String ano = item.dataCriacao.length() >= 4 ? item.dataCriacao.substring(0, 4) : juce::String();
        g.drawText(ano.isNotEmpty() ? ano : "-", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColSize) {
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
        g.drawText(formatarBytes(item.tamanhoBytes), 4, 0, width - 8, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColPath) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
        g.drawText(item.caminhoOrigem.isNotEmpty() ? item.caminhoOrigem : "-", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    } else if (columnId == kColCollection) {
        if (!item.collection.isEmpty()) {
            auto badgeArea = juce::Rectangle<int>(4, 5, width - 8, height - 10);
            g.setColour(tk.acento);
            g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
            g.setColour(tk.textoSobreAcento);
            bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
            g.drawText(traduzirContent(item.collection, isPt), badgeArea, juce::Justification::centred, true);
        } else {
            bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::italic)));
            g.drawText(isPt ? juce::String::fromUTF8("Nenhum") : "None", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
    } else if (columnId == kColSourceMedia) {
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        if (!item.sourceMedia.isEmpty()) {
            auto info = OriginalSourceMediumInfo::deserialize(item.sourceMedia.toStdString());
            juce::String text = juce::String::fromUTF8(info.toDisplaySummary().c_str());
            if (text.isEmpty() || text == "None / Unknown") {
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::italic)));
                g.drawText(isPt ? juce::String::fromUTF8("Nenhum") : "None", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
            } else {
                auto badgeArea = juce::Rectangle<int>(4, 5, width - 8, height - 10);
                g.setColour(juce::Colour(0xff0d9488));
                g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
                g.drawText(text, badgeArea.reduced(6, 0), juce::Justification::centredLeft, true);
            }
        } else {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::italic)));
            g.drawText(isPt ? juce::String::fromUTF8("Nenhum") : "None", 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
    }
}

juce::Component* IntakeWorkspaceComponent::refreshComponentForCell(int rowNumber, int columnId, bool, juce::Component* existingComponentToUpdate) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(indicesFiltrados_.size())) {
        delete existingComponentToUpdate;
        return nullptr;
    }
    int realIndex = indicesFiltrados_[static_cast<size_t>(rowNumber)];
    if (realIndex < 0 || realIndex >= static_cast<int>(todosItens_.size())) {
        delete existingComponentToUpdate;
        return nullptr;
    }

    if (columnId == kColSelect) {
        auto* cell = dynamic_cast<CheckboxCellComponent*>(existingComponentToUpdate);
        if (!cell) {
            delete existingComponentToUpdate;
            cell = new CheckboxCellComponent();
        }
        cell->setChecked(todosItens_[static_cast<size_t>(realIndex)].selecionado);
        cell->onToggle = [this, realIndex](bool checked) {
            todosItens_[static_cast<size_t>(realIndex)].selecionado = checked;
            atualizarContagens();
        };
        return cell;
    } else if (columnId == kColAction) {
        auto* cell = dynamic_cast<ActionCellComponent*>(existingComponentToUpdate);
        if (!cell) {
            delete existingComponentToUpdate;
            cell = new ActionCellComponent();
        }
        cell->onClick = [this, realIndex](juce::Rectangle<int> bounds) {
            mostrarMenuColecaoParaItem(realIndex, bounds);
        };
        return cell;
    }

    delete existingComponentToUpdate;
    return nullptr;
}

void IntakeWorkspaceComponent::selectedRowsChanged(int) {
    // Avoid double toggle since row click is handled in cellClicked
}

void IntakeWorkspaceComponent::sortOrderChanged(int newSortColumnId, bool isForwards) {
    ultimoSortColumnId_ = newSortColumnId;
    sortAscendente_ = isForwards;

    std::stable_sort(todosItens_.begin(), todosItens_.end(), [newSortColumnId, isForwards](const ItemIntake& a, const ItemIntake& b) {
        int cmp = 0;
        switch (newSortColumnId) {
            case kColType:
                cmp = a.categoria.compareIgnoreCase(b.categoria);
                break;
            case kColName:
                cmp = a.nomeArquivo.compareIgnoreCase(b.nomeArquivo);
                if (cmp == 0) cmp = a.titulo.compareIgnoreCase(b.titulo);
                break;
            case kColOrigin:
                cmp = a.extensao.compareIgnoreCase(b.extensao);
                break;
            case kColDateCreated:
                cmp = a.dataCriacao.compareIgnoreCase(b.dataCriacao);
                break;
            case kColSize:
                if (a.tamanhoBytes < b.tamanhoBytes) cmp = -1;
                else if (a.tamanhoBytes > b.tamanhoBytes) cmp = 1;
                break;
            case kColPath:
                cmp = a.caminhoOrigem.compareIgnoreCase(b.caminhoOrigem);
                break;
            case kColCollection:
                cmp = a.collection.compareIgnoreCase(b.collection);
                break;
            case kColSourceMedia:
                cmp = a.sourceMedia.compareIgnoreCase(b.sourceMedia);
                break;
            default:
                cmp = 0;
                break;
        }
        return isForwards ? (cmp < 0) : (cmp > 0);
    });

    atualizarFiltragem();
}

void IntakeWorkspaceComponent::cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e) {
    focoVisivel_ = false;  // clique na lista: o R volta a valer para a seleção
    grabKeyboardFocus();   // R e barra de espaço (preview) valem também na lista
    if (rowNumber < 0 || rowNumber >= static_cast<int>(indicesFiltrados_.size())) return;
    int realIndex = indicesFiltrados_[static_cast<size_t>(rowNumber)];
    if (realIndex < 0 || realIndex >= static_cast<int>(todosItens_.size())) return;

    if (e.mods.isPopupMenu()) {
        mostrarMenuContexto(realIndex, e.getScreenPosition());
        return;
    }

    if (e.getNumberOfClicks() >= 2) {
        abrirPreviewDoItem(todosItens_[static_cast<size_t>(realIndex)].id);  // "Mostrar na origem" fica no menu de contexto
        return;
    }

    if (columnId == kColSourceMedia) {
        mostrarEditorOriginalSourceMedium(realIndex, juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1));
        return;
    }
    if (columnId == kColCollection) {
        mostrarMenuColecaoParaItem(realIndex, juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1));
        return;
    }

    if (columnId != kColSelect && columnId != kColAction) {
        aplicarCliqueSelecao(rowNumber, e.mods);
        if (tabela_) {
            tabela_->updateContent();
            tabela_->repaint();
        }
        atualizarContagens();
    }
}

void IntakeWorkspaceComponent::cellDoubleClicked(int, int, const juce::MouseEvent&) {
    // o duplo clique é tratado em cellClicked (getNumberOfClicks() >= 2): abre o preview
}

void IntakeWorkspaceComponent::aplicarCliqueSelecao(int pos, const juce::ModifierKeys& mods) {
    if (pos < 0 || pos >= static_cast<int>(indicesFiltrados_.size())) return;
    auto item = [this](int p) -> ItemIntake* {
        const int ri = indicesFiltrados_[static_cast<size_t>(p)];
        return (ri >= 0 && ri < static_cast<int>(todosItens_.size())) ? &todosItens_[static_cast<size_t>(ri)] : nullptr;
    };
    auto* clicado = item(pos);
    if (clicado == nullptr) return;

    if (mods.isShiftDown() && ultimaPosicaoClicadaParaSelecao_ >= 0 &&
        ultimaPosicaoClicadaParaSelecao_ < static_cast<int>(indicesFiltrados_.size())) {
        // Shift: intervalo entre a âncora (último clique sem Shift) e o clicado; a âncora não muda.
        const int de = std::min(ultimaPosicaoClicadaParaSelecao_, pos);
        const int ate = std::max(ultimaPosicaoClicadaParaSelecao_, pos);
        for (int p = de; p <= ate; ++p)
            if (auto* it = item(p)) it->selecionado = true;
    } else if (mods.isCommandDown() || mods.isCtrlDown()) {
        clicado->selecionado = !clicado->selecionado;  // liga/desliga só este; vira âncora
        ultimaPosicaoClicadaParaSelecao_ = pos;
    } else {
        for (auto& it : todosItens_) it.selecionado = false;  // clique simples: só este; vira âncora
        clicado->selecionado = true;
        ultimaPosicaoClicadaParaSelecao_ = pos;
    }
    ultimoClicadoId_ = clicado->id;
    focoId_ = clicado->id;   // a barra de espaço (preview) e as setas continuam daqui
    posicaoFocoCache_ = pos;
    if (tabela_) {
        tabela_->updateContent();
        tabela_->repaint();
    }
    if (gridComponent_) gridComponent_->repaint();
    atualizarContagens();
}

void IntakeWorkspaceComponent::atualizarPainelMetadados() {
    if (!painelMetadados_ || modoVisao_ != ModoVisao::Icones) return;  // na lista a coluna não existe
    std::string alvo;
    if (!ultimoClicadoId_.empty()) {
        for (int idx : indicesFiltrados_) {
            if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
            const auto& it = todosItens_[static_cast<size_t>(idx)];
            if (it.id == ultimoClicadoId_ && it.selecionado) { alvo = it.id; break; }
        }
    }
    if (alvo.empty()) {  // o último clicado saiu da seleção: usa qualquer selecionado visível
        for (int idx : indicesFiltrados_) {
            if (idx < 0 || idx >= static_cast<int>(todosItens_.size())) continue;
            const auto& it = todosItens_[static_cast<size_t>(idx)];
            if (it.selecionado) { alvo = it.id; }
        }
    }
    if (alvo == idNoPainel_) return;
    idNoPainel_ = alvo;
    painelMetadados_->mostrarItem(alvo);
}

void IntakeWorkspaceComponent::mostrarMenuContexto(int itemIndex, juce::Point<int> screenPos) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    juce::PopupMenu m;
    m.addItem(1, itemMarcadoR(todosItens_[static_cast<size_t>(itemIndex)].id) ? i18n::t("intake.menu_desmarcar_r")
                                                                              : i18n::t("intake.menu_marcar_r"));
    m.addItem(2, isPt ? juce::String::fromUTF8("Mostrar Origem") : "Show at Source");
    m.addSeparator();
    m.addItem(3, isPt ? juce::String::fromUTF8("Obter Informações...") : "Get Info...");
    const auto& itemClicado = todosItens_[static_cast<size_t>(itemIndex)];
    const bool ehContainer = itemEhContainer(itemClicado) && ehOrigemComFicha(itemClicado.containerOrigem);
    if (ehContainer) {
        m.addSeparator();
        m.addItem(4, i18n::t("imp.editar_questionario"));
    }
    const std::string nestDoClique = itemClicado.containerNestId, origemDoClique = itemClicado.containerOrigem.toStdString();

    juce::Component::SafePointer<IntakeWorkspaceComponent> safeThis(this);

    // Group as Project Container / Change Container Type / Ungroup Container: sobre a selecao (ou o item clicado, se ele nao esta nela).
    {
        std::vector<std::string> idsDoMenu;
        if (itemClicado.selecionado) {
            for (const auto& it : todosItens_) if (it.selecionado && it.containerOrigem != "whatsapp") idsDoMenu.push_back(it.id);
        } else if (itemClicado.containerOrigem != "whatsapp") {
            idsDoMenu.push_back(itemClicado.id);
        }
        matriz::ui::HospedeOverlay hospede;
        hospede.abrir = [safeThis]() -> PainelOverlay* {
            if (safeThis == nullptr || safeThis->overlayAtual_ != nullptr) return nullptr;
            safeThis->overlayAtual_ = std::make_unique<PainelOverlay>();
            safeThis->addAndMakeVisible(*safeThis->overlayAtual_);
            safeThis->resized();
            return safeThis->overlayAtual_.get();
        };
        hospede.fechar = [safeThis] {
            if (safeThis == nullptr) return;
            safeThis->overlayAtual_.reset();
            safeThis->resized();
        };
        hospede.aoMudarDados = [safeThis] { if (safeThis != nullptr) safeThis->recarregar(); };
        juce::PopupMenu grupo;
        matriz::ui::adicionarMenuProjectContainer(grupo, projeto_, idsDoMenu, hospede);
        if (grupo.getNumItems() > 0) {
            m.addSeparator();
            for (juce::PopupMenu::MenuItemIterator it(grupo); it.next();) m.addItem(it.getItem());
        }
    }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [safeThis, itemIndex, nestDoClique, origemDoClique](int result) {
            if (!safeThis) return;
            if (result == 4) {
                safeThis->editarQuestionarioDoContainerDoItem(nestDoClique, origemDoClique);
            } else if (result == 1) {
                safeThis->alternarMarcaRDeItem(itemIndex);
            } else if (result == 2) {
                safeThis->abrirArquivoOrigem(itemIndex);
            } else if (result == 3) {
                safeThis->mostrarDialogoGetInfo(itemIndex);
            }
        });
}

void IntakeWorkspaceComponent::mostrarDialogoGetInfo(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(todosItens_.size())) return;
    const auto& item = todosItens_[static_cast<size_t>(itemIndex)];

    juce::String msg;
    msg << "ASSET & FILE INFORMATION\n";
    msg << "-----------------------------------------\n";
    msg << "Filename: " << item.nomeArquivo << "\n";
    msg << "Title: " << item.titulo << "\n";
    msg << "Asset ID: " << item.id << "\n";
    msg << "Category: " << item.categoria << " (" << item.extensao << ")\n";
    msg << "Size: " << formatarBytes(item.tamanhoBytes) << " (" << juce::String(item.tamanhoBytes) << " bytes)\n";
    msg << "Date Created: " << (item.dataCriacao.isNotEmpty() ? item.dataCriacao : "Unknown") << "\n";
    msg << "Original Path: " << (item.caminhoOrigem.isNotEmpty() ? item.caminhoOrigem : "Unknown") << "\n\n";

    msg << "METADATA ASSIGNMENTS\n";
    msg << "-----------------------------------------\n";
    msg << "Content: " << (item.collection.isNotEmpty() ? item.collection : "None") << "\n";
    if (item.sourceMedia.isNotEmpty()) {
        auto info = OriginalSourceMediumInfo::deserialize(item.sourceMedia.toStdString());
        msg << "Original Medium: " << juce::String::fromUTF8(info.toDisplaySummary().c_str()) << "\n";
        if (!info.recordingDevice.empty()) {
            msg << "Device: " << juce::String::fromUTF8(info.recordingDevice.c_str()) << "\n";
        }
    } else {
        msg << "Original Medium: None\n";
    }

    auto notes = projeto_.lerMetadado(item.id, "notas_livres").value_or("");
    if (!notes.empty()) {
        msg << "Notes: " << juce::String::fromUTF8(notes.c_str()) << "\n";
    }

    auto creator = projeto_.lerMetadado(item.id, "dc_creator").value_or("");
    if (!creator.empty()) {
        msg << "Creator: " << juce::String::fromUTF8(creator.c_str()) << "\n";
    }

    auto rights = projeto_.lerMetadado(item.id, "dc_rights").value_or("");
    if (!rights.empty()) {
        msg << "Rights: " << juce::String::fromUTF8(rights.c_str()) << "\n";
    }

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::InfoIcon,
        "File Info - " + item.nomeArquivo,
        msg,
        "Reveal in Finder",
        "Close",
        this,
        juce::ModalCallbackFunction::create([this, itemIndex](int result) {
            if (result == 1) {
                abrirArquivoOrigem(itemIndex);
            }
        })
    );
}

void IntakeWorkspaceComponent::abrirArquivoOrigem(int rowNumber) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(indicesFiltrados_.size())) return;
    int realIndex = indicesFiltrados_[static_cast<size_t>(rowNumber)];
    if (realIndex < 0 || realIndex >= static_cast<int>(todosItens_.size())) return;

    const auto& item = todosItens_[static_cast<size_t>(realIndex)];
    auto caminhoOpt = projeto_.caminhoDeOrigem(item.id);
    if (caminhoOpt && !caminhoOpt->isEmpty()) {
        juce::File f(*caminhoOpt);
        if (f.existsAsFile() || f.isDirectory()) {
            f.revealToUser();
            return;
        }
    }

    auto arqPrinc = projeto_.arquivoPrincipal(item.id);
    if (arqPrinc && !arqPrinc->caminhoAbsoluto.isEmpty()) {
        juce::File f(arqPrinc->caminhoAbsoluto);
        if (f.existsAsFile() || f.isDirectory()) {
            f.revealToUser();
            return;
        }
    }
}

// FileDragAndDropTarget implementation
bool IntakeWorkspaceComponent::isInterestedInFileDrag(const juce::StringArray&) {
    return true;
}

void IntakeWorkspaceComponent::filesDropped(const juce::StringArray& files, int, int) {
    juce::Array<juce::File> arquivos;
    for (const auto& caminho : files) {
        arquivos.add(juce::File(caminho));
    }
    if (aoIngerirArquivosDireto) {
        aoIngerirArquivosDireto(arquivos);
    } else if (aoPedirIngerirArquivos) {
        aoPedirIngerirArquivos();
    }
}

void IntakeWorkspaceComponent::mouseDown(const juce::MouseEvent& e) {
    if (!tabela_ || modoVisao_ != ModoVisao::Lista) return;

    auto rel = e.getEventRelativeTo(tabela_.get());
    if (rel.x < 0 || rel.y < 0 || rel.x >= tabela_->getWidth() || rel.y >= tabela_->getHeight()) {
        // Clique no vazio (fora da tabela) deseleciona tudo — mesmo
        // comportamento do Folder Map.
        if (e.mods.isLeftButtonDown() && !itensSelecionados().empty()) selecionarTodos(false);
        return;
    }

    // Check click in header area of column kColSelect
    if (rel.y < tabela_->getHeaderHeight()) {
        int colIdx = tabela_->getHeader().getIndexOfColumnId(kColSelect, true);
        auto pos = tabela_->getHeader().getColumnPosition(colIdx >= 0 ? colIdx : 0);
        if (rel.x >= pos.getX() && rel.x < pos.getRight()) {
            if (e.eventComponent != chkSelectAllHeader_.get()) {
                if (chkSelectAllHeader_) {
                    chkSelectAllHeader_->setToggleState(!chkSelectAllHeader_->getToggleState(), juce::sendNotification);
                }
            }
        }
        return;
    }

    if (!e.mods.isLeftButtonDown()) return;

    int row = tabela_->getRowContainingPosition(rel.x, rel.y);
    if (row >= 0 && row < static_cast<int>(indicesFiltrados_.size())) {
        isDraggingRows_ = true;
        dragStartRow_ = row;
        dragSelectState_ = true;
    } else {
        // Dentro da área da tabela, mas abaixo da última linha (lista mais
        // curta que a viewport) — também é "vazio".
        if (!itensSelecionados().empty()) selecionarTodos(false);
    }
}

void IntakeWorkspaceComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDraggingRows_ || !tabela_ || modoVisao_ != ModoVisao::Lista) return;

    auto rel = e.getEventRelativeTo(tabela_.get());
    int row = tabela_->getRowContainingPosition(rel.x, rel.y);
    if (row >= 0 && row < static_cast<int>(indicesFiltrados_.size())) {
        int rStart = std::min(dragStartRow_, row);
        int rEnd = std::max(dragStartRow_, row);
        bool changed = false;
        for (int r = rStart; r <= rEnd; ++r) {
            int realIdx = indicesFiltrados_[static_cast<size_t>(r)];
            if (realIdx >= 0 && realIdx < static_cast<int>(todosItens_.size())) {
                if (todosItens_[static_cast<size_t>(realIdx)].selecionado != dragSelectState_) {
                    todosItens_[static_cast<size_t>(realIdx)].selecionado = dragSelectState_;
                    changed = true;
                }
            }
        }
        if (changed) {
            tabela_->updateContent();
            tabela_->repaint();
            atualizarContagens();
        }
    }
}

void IntakeWorkspaceComponent::mouseUp(const juce::MouseEvent&) {
    isDraggingRows_ = false;
    dragStartRow_ = -1;
}

void IntakeWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();

    g.fillAll(tk.fundo);

    // A faixa superior de 44px saiu (itens 2 e 3 do ajuste de layout do
    // INTAKE): INGEST/GOOGLE DRIVE/LIGHTROOM e HELP/CLOSE PROJECT subiram
    // para a linha das tabs, e a linha que sobrou foi eliminada — o
    // conteúdo começa direto no topo do workspace.

    // Barra lateral no desenho do mapa "BARRA LATERAL DO INTAKE": cards, títulos, trilho lista/grade e "N selected".
    barra_.paint(g);
    if (!trilhoVisao_.isEmpty()) barra::pintarTrilhoSegmentado(g, trilhoVisao_);
    if (!tituloFiltro_.isEmpty() && lblSubtitulo_) {
        g.setColour(tk.textoTerciario);
        g.setFont(barra::fonteContagem());
        g.drawText(lblSubtitulo_->getText(), tituloFiltro_, juce::Justification::centredRight, true);
    }
}

void IntakeWorkspaceComponent::setHasParentCatalog(bool hasParent) {
    hasParentCatalog_ = hasParent;
    resized();
}

void IntakeWorkspaceComponent::componentesBarraSuperior(
        std::vector<std::pair<juce::Component*, int>>& esquerda,
        std::vector<std::pair<juce::Component*, int>>& direita) {
    esquerda.clear();
    direita.clear();
    // INGEST FILES / GOOGLE DRIVE / LIGHTROOM saíram da linha das tabs e
    // moraram no card IMPORT, no topo da coluna esquerda (acima do FILTER)
    // — ver resized(). A barra de navegação continua hospedando só o HELP.
    if (btnAjuda_)        direita.push_back({btnAjuda_.get(), 28});
}

void IntakeWorkspaceComponent::resized() {
    auto area = getLocalBounds();

    // Hide old labels
    if (lblTitulo_) lblTitulo_->setVisible(false);
    if (lblContadorTotal_) lblContadorTotal_->setVisible(false);

    // A barra superior de 44px não existe mais aqui: os cinco comandos
    // (INGEST FILES / GOOGLE DRIVE / LIGHTROOM à esquerda, HELP / CLOSE
    // PROJECT à direita) são posicionados pela barra de navegação, que os
    // hospeda na mesma linha das tabs — ver componentesBarraSuperior().

    // Barra lateral no desenho do mapa "BARRA LATERAL DO INTAKE" (240 px): mesmo padrão da barra do METADATA.
    using namespace barra;
    barra_.comecar(area.removeFromLeft(kLargura));
    auto& corpo = barra_.corpo();
    if (divisor1_) divisor1_->setVisible(false);
    if (divisor2_) divisor2_->setVisible(false);
    auto linhaDupla = [&](juce::Component* a, juce::Component* b, int altura, float fracaoA = 0.5f) {
        auto linha = corpo.removeFromTop(altura);
        const int wa = juce::roundToInt((linha.getWidth() - kGap) * fracaoA);
        if (a) a->setBounds(linha.removeFromLeft(wa));
        linha.removeFromLeft(kGap);
        if (b) b->setBounds(linha);
    };

    // 1. IMPORT: o botão principal (grafite), com o ícone ao lado do texto, em 48 px.
    if (btnIngerir_) {
        if (btnIngerir_->getParentComponent() != this) addAndMakeVisible(*btnIngerir_);
        btnIngerir_->setBounds(corpo.removeFromTop(48));
        corpo.removeFromTop(kEntreCards);
    }

    // 2. VIEW numa linha: lista/grade e o tamanho das miniaturas lado a lado.
    {
        auto linha = barra_.iniciarCardEmLinha(i18n::t("catwork.secao_view"), 28);
        trilhoVisao_ = linha.removeFromLeft(60);
        auto seg = trilhoVisao_;
        if (btnVisaoLista_) btnVisaoLista_->setBounds(seg.removeFromLeft(30));
        if (btnVisaoIcones_) btnVisaoIcones_->setBounds(seg);
        linha.removeFromLeft(10);
        if (sliderTamanho_) sliderTamanho_->setBounds(linha);
        barra_.finalizarCard();
    }

    // 3. FILTER: "N selected" na linha do título; Select all | Clear; All na linha inteira e os tipos em duas colunas;
    //    All years, e All months | All days lado a lado (desativados até escolher o ano).
    tituloFiltro_ = barra_.iniciarCard(i18n::t("intake.secao_filter"));
    linhaDupla(btnSelecionarTodos_.get(), btnLimparSelecao_.get(), kChipH);
    corpo.removeFromTop(6);
    btnFiltroAll_->setBounds(corpo.removeFromTop(kChipH));
    corpo.removeFromTop(kGap);
    linhaDupla(btnFiltroAudio_.get(), btnFiltroVideo_.get(), kChipH);
    corpo.removeFromTop(kGap);
    linhaDupla(btnFiltroImage_.get(), btnFiltroDoc_.get(), kChipH);
    corpo.removeFromTop(kGap);
    linhaDupla(btnFiltroOther_.get(), btnFiltroPhoneDump_.get(), kChipH);
    corpo.removeFromTop(8);
    if (comboAno_) comboAno_->setBounds(corpo.removeFromTop(28));
    if (comboMes_ && comboDia_) {
        corpo.removeFromTop(6);
        linhaDupla(comboMes_.get(), comboDia_.get(), 28);
    }
    barra_.finalizarCard();

    // 4. BATCH ASSIGNMENT em duas colunas, com o quadradinho de cor; Source medium e Geo location na linha inteira.
    barra_.iniciarCard(i18n::t("intake.secao_batch"));
    btnSourceMediumLote_->setBounds(corpo.removeFromTop(kChipH));
    corpo.removeFromTop(kGap);
    linhaDupla(btnCreatorLote_.get(), btnContentLote_.get(), kChipH);
    corpo.removeFromTop(kGap);
    linhaDupla(btnSubjectLote_.get(), btnEventDateLote_.get(), kChipH);
    corpo.removeFromTop(kGap);
    btnGeolocationLote_->setBounds(corpo.removeFromTop(kChipH));
    barra_.finalizarCard();

    // 5. ACTIONS: Approve selected | Approve all lado a lado, Reject marked embaixo; atalho R com o "?" (a frase longa
    //    sobre o REJECT MARKED está no tooltip dele).
    barra_.iniciarCard(i18n::t("intake.secao_actions"));
    linhaDupla(btnConfirmarSelecao_.get(), btnConfirmarTodos_.get(), 28, 0.56f);
    corpo.removeFromTop(6);
    btnRemoverSelecao_->setBounds(corpo.removeFromTop(28));
    corpo.removeFromTop(6);
    if (legendaAtalhos_) legendaAtalhos_->setBounds(corpo.removeFromTop(24));
    if (lblNotaReject_) lblNotaReject_->setBounds({});
    barra_.finalizarCard();

    // Coluna da direita: metadados do arquivo selecionado (somente leitura), estreita (~280 px). Some em janelas estreitas.
    if (painelMetadados_) {
        // Só no modo MINIATURAS: na LISTA os metadados aparecem apenas na janela de preview.
        const bool cabe = area.getWidth() >= 760 && modoVisao_ == ModoVisao::Icones;
        painelMetadados_->setVisible(cabe);
        if (cabe) painelMetadados_->setBounds(area.removeFromRight(280));
    }

    // Right Area fills the remaining workspace (x = 230 to width, full height)
    if (tabela_) {
        tabela_->setBounds(area);
        if (chkSelectAllHeader_) {
            int colIdx = tabela_->getHeader().getIndexOfColumnId(kColSelect, true);
            auto pos = tabela_->getHeader().getColumnPosition(colIdx >= 0 ? colIdx : 0);
            chkSelectAllHeader_->setBounds(pos.getX() + 2, 2, pos.getWidth() - 4, tabela_->getHeaderHeight() - 4);
        }
    }
    if (gridViewport_) {
        gridViewport_->setBounds(area);
        if (gridComponent_) gridComponent_->recalcularLayout();
    }
    if (emptyState_) {
        emptyState_->setBounds(area);
    }
    if (overlayAtual_ && overlayAtual_->isVisible()) {
        overlayAtual_->setBounds(getLocalBounds());
        overlayAtual_->toFront(true);
    }
}

// `caminho`: grupos até onde a janela deve abrir (o VOLTAR de uma ficha reabre o menu no mesmo lugar).
void IntakeWorkspaceComponent::abrirMenuImportar(std::vector<std::string> caminho) {
    if (overlayAtual_ != nullptr) return;
    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    mostrarMenuImportar(*overlayAtual_, [seguro](std::optional<std::string> id, std::vector<std::string> caminhoAteAqui) {
        juce::MessageManager::callAsync([seguro, id, caminhoAteAqui] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            if (id.has_value()) seguro->tratarEscolhaImportar(*id, caminhoAteAqui);
        });
    }, std::move(caminho));
    resized();
}

// O que cada card da janela IMPORT faz. Os fluxos que já existiam (celular, Drive, Lightroom, Bridge) seguem iguais;
// as demais folhas ainda usam o seletor de arquivos e pastas de sempre (as fichas de contexto entram por categoria).
void IntakeWorkspaceComponent::tratarEscolhaImportar(const std::string& id, const std::vector<std::string>& caminho) {
    if (id == "phone") {
        juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
        abrirReceberDoCelular([seguro, caminho] {  // VOLTAR do card do celular: reabre a janela IMPORT no mesmo lugar
            juce::MessageManager::callAsync([seguro, caminho] {
                if (seguro == nullptr) return;
                seguro->overlayAtual_.reset();
                seguro->escolhaImportacaoAtual_.reset();
                seguro->resized();
                seguro->abrirMenuImportar(caminho);
            });
        });
        return;
    }
    if (id == "drive") {
        // Várias contas do Drive neste Mac: pergunta qual (antes abria a primeira).
        juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
        escolherContaGoogleDrive(btnIngerir_.get(), [seguro](const juce::File& gdFolder) {
            if (!seguro) return;
            if (seguro->aoIngerirDeGoogleDrive) seguro->aoIngerirDeGoogleDrive(gdFolder);
            else if (seguro->aoPedirIngerirArquivos) seguro->aoPedirIngerirArquivos();
        });
        return;
    }
    if (id.rfind("drive.", 0) == 0) {
        // Conta escolhida na sub-janela do card Google Drive.
        const auto contas = contasGoogleDrive();
        const size_t n = static_cast<size_t>(juce::String(id.substr(6)).getIntValue());
        if (n < contas.size()) {
            matriz::app::gravarUltimaContaGoogleDrive(contas[n].rotulo);
            if (aoIngerirDeGoogleDrive) aoIngerirDeGoogleDrive(contas[n].pasta);
            else if (aoPedirIngerirArquivos) aoPedirIngerirArquivos();
        }
        return;
    }
    if (id == "photos.lightroom") { if (aoIngerirDeLightroom) aoIngerirDeLightroom(); return; }
    if (id == "photos.bridge") { if (aoIngerirDeBridge) aoIngerirDeBridge(); return; }
    if (id == "videos.youtube") { abrirFluxoYoutube(caminho); return; }
    if (folhaImportarTemFicha(id)) { abrirFichaDeImportacao(id, caminho); return; }
    abrirSeletorDeImportacao(id);
}

// Ficha de contexto da folha (ex.: formato de origem do vídeo) antes do seletor de arquivos.
void IntakeWorkspaceComponent::abrirFichaDeImportacao(const std::string& id, const std::vector<std::string>& caminho) {
    if (overlayAtual_ != nullptr) return;
    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    mostrarFichaImportar(*overlayAtual_, id, [seguro, id](bool continuar, std::shared_ptr<const matriz::ingest::SobreMaterial> daFicha) {
        juce::MessageManager::callAsync([seguro, id, continuar, daFicha] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            if (continuar) seguro->abrirSeletorDeImportacao(id, daFicha);
        });
    }, [seguro, caminho] {  // VOLTAR: reabre a janela IMPORT no grupo de onde a ficha veio
        juce::MessageManager::callAsync([seguro, caminho] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            seguro->abrirMenuImportar(caminho);
        });
    });
    resized();
}

// ── YouTube Production ───────────────────────────────────────────────────────────────────────────────────
// Botão direito num container > Editar questionário inicial: a ficha reabre com as respostas guardadas.
void IntakeWorkspaceComponent::editarQuestionarioDoContainerDoItem(const std::string& nestId, const std::string& origem) {
    if (overlayAtual_ != nullptr || nestId.empty()) return;
    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    editarQuestionarioDoContainer(*overlayAtual_, projeto_, nestId, origem, [seguro](bool aplicou) {
        juce::MessageManager::callAsync([seguro, aplicou] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            if (aplicou) seguro->recarregar();
        });
    });
    resized();
}

void IntakeWorkspaceComponent::abrirFluxoYoutube(const std::vector<std::string>& caminho) {
    if (overlayAtual_ != nullptr) return;
    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    mostrarFichaImportar(*overlayAtual_, "videos.youtube", [seguro](bool continuar, std::shared_ptr<const matriz::ingest::SobreMaterial> daFicha) {
        juce::MessageManager::callAsync([seguro, continuar, daFicha] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            if (continuar) seguro->escolherArquivosYoutube(daFicha, {});
            else seguro->youtubePendentes_.clear();
        });
    }, [seguro, caminho] {  // VOLTAR: abandona o que foi cadastrado e reabre o card anterior
        juce::MessageManager::callAsync([seguro, caminho] {
            if (seguro == nullptr) return;
            seguro->youtubePendentes_.clear();
            seguro->overlayAtual_.reset();
            seguro->resized();
            seguro->abrirMenuImportar(caminho);
        });
    });
    resized();
}

// Os arquivos de UM vídeo: o vídeo (obrigatório), a thumbnail e o roteiro. Os três entram juntos e viram um container.
void IntakeWorkspaceComponent::escolherArquivosYoutube(std::shared_ptr<const matriz::ingest::SobreMaterial> daFicha, const juce::String& aviso) {
    if (overlayAtual_ != nullptr) return;
    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    mostrarArquivosYoutube(*overlayAtual_, aviso, [seguro, daFicha](int botao, ArquivosYoutube arquivos) {
        juce::MessageManager::callAsync([seguro, daFicha, botao, arquivos] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            if (botao == 3) { seguro->abrirFluxoYoutube({ "videos" }); return; }  // VOLTAR: refaz a ficha do vídeo
            if (botao != 1) { seguro->youtubePendentes_.clear(); return; }
            if (!arquivos.video.existsAsFile()) { seguro->escolherArquivosYoutube(daFicha, i18n::t("imp.ficha.youtube.sem_video")); return; }

            auto contexto = std::make_shared<matriz::ingest::SobreMaterial>(*mesclarContextosImportar(contextoDaFolhaImportar("videos.youtube"), daFicha));
            juce::Array<juce::File> arquivosDoLote;
            arquivosDoLote.add(arquivos.video);
            if (arquivos.thumbnail.existsAsFile()) {
                arquivosDoLote.add(arquivos.thumbnail);
                contexto->capaContainer = arquivos.thumbnail.getFullPathName().toStdString();  // a thumbnail cadastrada é a capa
            }
            if (arquivos.roteiro.existsAsFile()) {
                arquivosDoLote.add(arquivos.roteiro);
                // Sem descrição digitada, um roteiro em texto simples preenche a descrição do vídeo.
                bool temDescricao = false;
                for (const auto& c : contexto->camposDoVideo) if (c.first == "dc_description" && !c.second.empty()) temDescricao = true;
                const auto ext = arquivos.roteiro.getFileExtension().toLowerCase();
                if (!temDescricao && (ext == ".txt" || ext == ".md")) {
                    const juce::String texto = arquivos.roteiro.loadFileAsString().trim().substring(0, 4000);
                    if (texto.isNotEmpty()) contexto->camposDoVideo.push_back({ "dc_description", texto.toStdString() });
                }
            }
            seguro->youtubePendentes_.push_back({ arquivosDoLote, contexto });
            juce::String nome = arquivos.video.getFileNameWithoutExtension();
            for (const auto& c : contexto->camposDoVideo) if (c.first == "dc_title" && !c.second.empty()) nome = juce::String::fromUTF8(c.second.c_str());
            seguro->perguntarOutroYoutube(nome);
        });
    });
    resized();
}

void IntakeWorkspaceComponent::perguntarOutroYoutube(const juce::String& nomeDoVideo) {
    if (overlayAtual_ != nullptr) return;
    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    mostrarPerguntaOutroYoutube(*overlayAtual_, nomeDoVideo, static_cast<int>(youtubePendentes_.size()), [seguro](int escolha) {
        juce::MessageManager::callAsync([seguro, escolha] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            if (escolha == 1) seguro->importarYoutubePendentes();
            else if (escolha == 2) seguro->abrirFluxoYoutube({ "videos" });
            else seguro->youtubePendentes_.clear();
        });
    });
    resized();
}

// Cada vídeo vira um lote próprio (e um container próprio): vídeo + thumbnail + roteiro + o contexto da ficha dele.
void IntakeWorkspaceComponent::importarYoutubePendentes() {
    auto pendentes = std::move(youtubePendentes_);
    youtubePendentes_.clear();
    for (const auto& [arquivos, contexto] : pendentes) {
        if (aoIngerirComContexto) aoIngerirComContexto(arquivos, contexto);
        else if (aoIngerirArquivosDireto) aoIngerirArquivosDireto(arquivos);
    }
}

// Seletor de arquivos e pastas do card escolhido; o que o card implica (ver contextoDaFolhaImportar) vai junto.
void IntakeWorkspaceComponent::abrirSeletorDeImportacao(const std::string& id, std::shared_ptr<const matriz::ingest::SobreMaterial> daFicha) {
    if (!aoIngerirComContexto && !aoIngerirArquivosDireto) {
        if (aoPedirIngerirArquivos) aoPedirIngerirArquivos();
        return;
    }
    const auto contexto = mesclarContextosImportar(contextoDaFolhaImportar(id), std::move(daFicha));
    auto seletor = std::make_shared<juce::FileChooser>(i18n::t("imp.titulo") + " - " + i18n::t("imp.node." + id));
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    seletor->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                         [seguro, seletor, contexto](const juce::FileChooser& fc) {
                             const auto escolhidos = fc.getResults();
                             if (seguro == nullptr || escolhidos.isEmpty()) return;
                             if (seguro->aoIngerirComContexto) seguro->aoIngerirComContexto(escolhidos, contexto);
                             else if (seguro->aoIngerirArquivosDireto) seguro->aoIngerirArquivosDireto(escolhidos);
                         });
}

void IntakeWorkspaceComponent::abrirReceberDoCelular(std::function<void()> aoVoltar) {
    if (overlayAtual_ != nullptr) return;
    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    mostrarEscolhaImportacaoCelular(*overlayAtual_, [seguro](std::optional<ModoImportacaoCelular> modo) {
        juce::MessageManager::callAsync([seguro, modo] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->escolhaImportacaoAtual_.reset();
            seguro->resized();
            if (modo.has_value()) seguro->iniciarReceberDoCelular(*modo);
        });
    }, &escolhaImportacaoAtual_, aoVoltar, matriz::celular::SessaoAndroidUsb::suportado());
    resized();
}

void IntakeWorkspaceComponent::abrirAndroidUsbAoPlugar() {
    if (!matriz::celular::SessaoAndroidUsb::suportado()) return;
    if (janelaAndroidUsb_ != nullptr) { janelaAndroidUsb_->toFront(true); return; }
    if (overlayAtual_ != nullptr) return;
    abrirAndroidUsb();
}

// PHONE DUMP > ANDROID USB: o celular numa janela de pasta (mesmo formato da janela do Folder Map). Os arquivos copiados
// entram como a galeria do QR code (container "gallery", Original Source Medium = Phone Dump).
void IntakeWorkspaceComponent::abrirAndroidUsb() {
    if (janelaAndroidUsb_ != nullptr) { janelaAndroidUsb_->toFront(true); return; }
    auto* pProj = &projeto_.projeto();
    auto existentes = [pProj]() -> matriz::celular::ConjuntoExistentes {
        if (pProj == nullptr) return {};
        return matriz::celular::existentesDoProjeto(*pProj);
    };
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    janelaAndroidUsb_ = std::make_unique<JanelaAndroidUsb>(existentes,
        [seguro](juce::File pasta, int recebidos) {
            juce::MessageManager::callAsync([seguro, pasta, recebidos] {
                if (seguro == nullptr) return;
                seguro->janelaAndroidUsb_.reset();
                if (recebidos > 0 && pasta.isDirectory() && seguro->aoIngerirArquivosDireto) {
                    if (seguro->aoDefinirOrigemContainer) seguro->aoDefinirOrigemContainer("gallery");  // galeria do celular = container
                    seguro->aoIngerirArquivosDireto({pasta});
                }
            });
        },
        [seguro] {
            juce::MessageManager::callAsync([seguro] { if (seguro != nullptr) seguro->janelaAndroidUsb_.reset(); });
        });
}

void IntakeWorkspaceComponent::iniciarReceberDoCelular(ModoImportacaoCelular modo) {
    if (overlayAtual_ != nullptr) return;
    if (modo == ModoImportacaoCelular::AndroidUsb) {
        abrirAndroidUsb();
        return;
    }
    if (modo == ModoImportacaoCelular::WhatsApp) {
        overlayAtual_ = std::make_unique<PainelOverlay>();
        addAndMakeVisible(*overlayAtual_);
        juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
        mostrarImportarWhatsapp(*overlayAtual_, [seguro] {
            juce::MessageManager::callAsync([seguro] {
                if (seguro == nullptr) return;
                seguro->overlayAtual_.reset();
                seguro->resized();
                seguro->recarregar();
            });
        });
        resized();
        return;
    }

    matriz::celular::OpcoesServidor opcoes;
    auto* pProj = &projeto_.projeto();
    opcoes.existentesDoProjeto = [pProj]() -> matriz::celular::ConjuntoExistentes {
        if (pProj == nullptr) return {};
        return matriz::celular::existentesDoProjeto(*pProj);
    };

    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<IntakeWorkspaceComponent> seguro(this);
    painelCelularAtual_ = mostrarReceberDoCelular(*overlayAtual_, opcoes, [seguro](RespostaReceberCelular r) {
        juce::MessageManager::callAsync([seguro, r] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            if (r.concluiu && r.recebidos > 0 && r.pastaSessao.isDirectory()) {
                if (seguro->aoIngerirArquivosDireto) {
                    if (seguro->aoDefinirOrigemContainer) seguro->aoDefinirOrigemContainer("gallery");  // galeria do celular = container
                    seguro->aoIngerirArquivosDireto({r.pastaSessao});
                }
            }
        });
    }, modo);
    resized();
}

void IntakeWorkspaceComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    if (lblTitulo_) {
        lblTitulo_->setText(i18n::t("intake.titulo"), juce::dontSendNotification);
    }
    if (lblSubtitulo_) {
        lblSubtitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
        lblSubtitulo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (btnVisaoLista_) {
        btnVisaoLista_->setAtivo(modoVisao_ == ModoVisao::Lista);
        btnVisaoLista_->setTooltip(i18n::t("intake.tooltip_lista"));
    }
    if (btnVisaoIcones_) {
        btnVisaoIcones_->setAtivo(modoVisao_ == ModoVisao::Icones);
        btnVisaoIcones_->setTooltip(i18n::t("intake.tooltip_icones"));
    }
    if (btnIngerir_) {
        btnIngerir_->setButtonText(i18n::t("imp.botao"));
        btnIngerir_->setTooltip(i18n::t("imp.botao.dica"));
        btnIngerir_->repaint();
    }
    if (chkSelectAllHeader_) {
        chkSelectAllHeader_->setTooltip(matriz::i18n::localeAtivo().startsWith("pt") ? juce::String::fromUTF8("Selecionar / Desmarcar Todos") : "Select / Deselect All");
        chkSelectAllHeader_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    auto updatePill = [](juce::TextButton* btn, juce::Colour textCol, juce::Colour borderCol) {
        if (auto* p = dynamic_cast<PillButton*>(btn)) {
            p->corTextoCustom = textCol;
            p->corBordaCustom = borderCol;
            p->repaint();
        }
    };

    if (btnSelecionarTodos_) {
        btnSelecionarTodos_->setButtonText(i18n::t("intake.selecionar_todos"));
        updatePill(btnSelecionarTodos_.get(), juce::Colours::transparentBlack, juce::Colours::transparentBlack);
    }
    if (btnLimparSelecao_) {
        btnLimparSelecao_->setButtonText(i18n::t("intake.limpar_selecao"));
        updatePill(btnLimparSelecao_.get(), juce::Colours::transparentBlack, juce::Colours::transparentBlack);
    }
    // SOURCE MEDIUM/CREATOR/CONTENT/SUBJECT mantêm a cor fixa do bloco
    // correspondente no Visual Editor (setada no construtor) — não passam
    // por updatePill, que zeraria essa cor a cada mudança de tema/locale.
    if (btnGeolocationLote_) {
        btnGeolocationLote_->setButtonText("GEO LOCATION");
    }
    if (btnConfirmarSelecao_) {
        btnConfirmarSelecao_->setButtonText(i18n::t("intake.btn_enviar_selecionados"));
        updatePill(btnConfirmarSelecao_.get(), juce::Colours::white, juce::Colour(0xff22c55e));
    }
    if (btnConfirmarTodos_) {
        btnConfirmarTodos_->setButtonText(i18n::t("intake.btn_enviar_todos"));
        updatePill(btnConfirmarTodos_.get(), juce::Colours::white, juce::Colour(0xff22c55e));
    }
    if (btnRemoverSelecao_) {
        updatePill(btnRemoverSelecao_.get(), juce::Colours::white, tk.perigo);
        atualizarBotaoRejeitar();
    }
    if (legendaAtalhos_) {
        if (auto* dica = dynamic_cast<juce::SettableTooltipClient*>(legendaAtalhos_.get()))
            dica->setTooltip(i18n::t("intake.legenda_r_dica"));
        legendaAtalhos_->repaint();
    }
    if (lblNotaReject_) {
        lblNotaReject_->setText(i18n::t("intake.nota_reject"), juce::dontSendNotification);
        lblNotaReject_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (btnAjuda_) {
        btnAjuda_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnAjuda_->setColour(juce::TextButton::textColourOffId, tk.acento);
        btnAjuda_->setColour(juce::TextButton::textColourOnId, tk.acento);
    }
    if (tabela_) {
        auto& hdr = tabela_->getHeader();
        hdr.setColumnName(kColType, i18n::t("intake.col_type"));
        hdr.setColumnName(kColName, i18n::t("intake.col_name"));
        hdr.setColumnName(kColOrigin, i18n::t("intake.col_origin"));
        hdr.setColumnName(kColDateCreated, i18n::t("intake.col_date"));
        hdr.setColumnName(kColSize, i18n::t("intake.col_size"));
        hdr.setColumnName(kColPath, i18n::t("intake.col_path"));
        hdr.setColumnName(kColCollection, i18n::t("intake.col_collection"));
        hdr.setColumnName(kColSourceMedia, i18n::t("intake.col_source_media"));
        hdr.setColumnName(kColAction, i18n::t("intake.col_action"));
        tabela_->setColour(juce::ListBox::backgroundColourId, tk.painel);
        tabela_->setColour(juce::ListBox::outlineColourId, tk.borda);
        tabela_->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, tk.painelAlt);
        tabela_->getHeader().setColour(juce::TableHeaderComponent::textColourId, tk.textoPrimario);
        tabela_->getHeader().setColour(juce::TableHeaderComponent::outlineColourId, tk.borda);
        tabela_->updateContent();
        tabela_->repaint();
    }
    if (gridComponent_) {
        gridComponent_->repaint();
    }
    if (emptyState_) {
        emptyState_->repaint();
    }
    atualizarContagens();
    repaint();
}

// Só pra --selftest-lote: força o paint do grid (que é quem pede as
// miniaturas) e consulta o cache de miniaturas carregadas.
void IntakeWorkspaceComponent::pintarGridParaTeste() {
    if (!gridComponent_) return;
    if (gridComponent_->getWidth() <= 0) gridComponent_->setSize(1200, 800);
    gridComponent_->createComponentSnapshot(gridComponent_->getLocalBounds());
}

bool IntakeWorkspaceComponent::miniaturaEmCacheParaTeste(const std::string& itemId) const {
    return gridComponent_ && gridComponent_->temMiniaturaEmCache(itemId);
}

// Só pra --selftest-lote (etapa 10): Enter = APPLY nos popups de lote.
int IntakeWorkspaceComponent::autotesteTeclasPopupsParaTeste(const std::function<void(bool, const juce::String&)>& checar) {
    int falhas = 0;
    auto ok = [&](bool c, const juce::String& d) { checar(c, d); if (!c) ++falhas; };
    auto editorDe = [](juce::Component& raiz) {
        std::function<juce::TextEditor*(juce::Component&)> achar = [&](juce::Component& c) -> juce::TextEditor* {
            if (auto* te = dynamic_cast<juce::TextEditor*>(&c)) return te;
            for (auto* f : c.getChildren()) if (auto* r = achar(*f)) return r;
            return nullptr;
        };
        return achar(raiz);
    };
    const juce::KeyPress enter(juce::KeyPress::returnKey);
    // O JUCE entrega Return/Esc/mudança de texto por mensagem assíncrona: o
    // teste chama os mesmos callbacks direto (o mapeamento tecla -> callback
    // é do JUCE; o que se testa aqui é a nossa lógica).
    auto digitar = [](juce::TextEditor& te, const juce::String& t) {
        te.setText(t, false);
        if (te.onTextChange) te.onTextChange();
    };
    auto tecla = [](juce::TextEditor& te, bool enterTecla) {
        if (enterTecla) { if (te.onReturnKey) te.onReturnKey(); }
        else if (te.onEscapeKey) te.onEscapeKey();
    };
    {
        std::vector<juce::String> aplicados;
        AutocompleteLotePopupContent popup("t", "p", {"Creator Alfa", "Creator Beta"}, juce::Colours::red,
                                          [&](const juce::String& v) { aplicados.push_back(v); });
        popup.setSize(340, 240);
        auto* te = editorDe(popup);
        ok(te != nullptr, "popup has a text field");
        if (te == nullptr) return falhas;
        tecla(*te, true);
        ok(aplicados.empty(), "empty field + Enter does nothing");
        digitar(*te, "Creator A");  // autocomplete aberto, "Creator Alfa" destacada
        tecla(*te, true);
        ok(aplicados.empty() && te->getText() == "Creator Alfa", "1st Enter confirms the highlighted suggestion, applies nothing");
        tecla(*te, true);
        ok(aplicados.size() == 1 && aplicados.front() == "Creator Alfa", "2nd Enter = APPLY");
        digitar(*te, "Outro");
        tecla(*te, false);
        ok(aplicados.size() == 1, "Esc closes without applying");
        digitar(*te, "Texto livre");  // nenhuma sugestão casa: Enter aplica direto
        tecla(*te, true);
        ok(aplicados.size() == 2 && aplicados.back() == "Texto livre", "value with no matching suggestion: Enter applies");
    }
    {
        int aplicados = 0;
        ContentLotePopupContent popup(juce::Colours::red, [&](const juce::String&) { ++aplicados; });
        popup.keyPressed(enter);
        ok(aplicados == 0, "CONTENT: Enter with nothing chosen does nothing");
        popup.combo_->setSelectedItemIndex(1, juce::sendNotificationSync);
        popup.keyPressed(enter);
        ok(aplicados == 1, "CONTENT: Enter = APPLY after choosing");
    }
    {
        int aplicados = 0;
        OriginalSourceMediumPopupContent popup("", true, [&](const std::string&) { ++aplicados; });
        popup.setSize(400, 380);
        auto* te = editorDe(popup);
        if (te != nullptr) {
            digitar(*te, "");
            tecla(*te, true);
            ok(aplicados == 0, "SOURCE MEDIUM: Enter on an empty field does nothing");
            digitar(*te, "abc");
            tecla(*te, true);
            ok(aplicados == 1, "SOURCE MEDIUM: Enter in a filled field = APPLY");
        }
    }
    return falhas;
}

// Só pra --selftest-lote: popup GEO LOCATION do Intake — autocomplete das
// localizações usadas e ★ Add to Favorites. Devolve o número de falhas.
int IntakeWorkspaceComponent::autotestePopupGeoParaTeste(matriz::db::Database& registro,
                                                        const std::function<void(bool, const juce::String&)>& checar) {
    int falhas = 0;
    auto ok = [&](bool c, const juce::String& d) { checar(c, d); if (!c) ++falhas; };
    GeoLocationPopupContent popup(registro, true, [](const std::string&, const std::string&, const std::string&,
                                                    const std::string&, const std::string&) {});
    auto rotulos = popup.rotulosUsadosParaTeste();
    const juce::String esperado = juce::String::fromUTF8("Porto Seguro, Bahia, Brazil");
    bool temPorto = std::find(rotulos.begin(), rotulos.end(), esperado) != rotulos.end();
    ok(temPorto && !rotulos.empty() && rotulos.front() == esperado,
       "used locations list the project's places, most used first (" + juce::String((int) rotulos.size()) + ")");
    if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
        juce::PNGImageFormat png;
        if (auto out = std::unique_ptr<juce::FileOutputStream>(dir.getChildFile("intake_geo_popup.png").createOutputStream())) {
            out->setPosition(0); out->truncate();
            png.writeImageToStream(popup.createComponentSnapshot(popup.getLocalBounds()), *out);
        }
        popup.abrirSugestoesParaTeste();
        if (auto out = std::unique_ptr<juce::FileOutputStream>(dir.getChildFile("intake_geo_popup_sugestoes.png").createOutputStream())) {
            out->setPosition(0); out->truncate();
            png.writeImageToStream(popup.createComponentSnapshot(popup.getLocalBounds()), *out);
        }
    }
    popup.escolherUsadaParaTeste(esperado);
    ok(popup.campoParaTeste(2) == "Porto Seguro" && popup.campoParaTeste(3) == "Bahia" &&
           popup.campoParaTeste(4) == "Brazil" && popup.campoParaTeste(0).startsWith("-16.44"),
       "picking a used location fills coordinates, city, state and country");

    auto antes = matriz::analytics::GeoFavoritosRepository::listar(registro).size();
    popup.salvarFavoritoParaTeste();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    auto* dlg = dynamic_cast<juce::AlertWindow*>(juce::ModalComponentManager::getInstance()->getModalComponent(0));
    ok(dlg != nullptr, "Add to Favorites asks for a name");
    if (dlg) {
        if (auto* ed = dlg->getTextEditor("nome")) {
            ok(ed->getText() == "Porto Seguro", "the suggested name is the city");
            ed->setText("Studio Porto");
        }
        dlg->exitModalState(1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    }
    auto favs = matriz::analytics::GeoFavoritosRepository::listar(registro);
    bool salvo = false;
    for (const auto& f : favs)
        if (f.nome == "Studio Porto" && f.city && *f.city == "Porto Seguro" && f.latitude) salvo = true;
    ok(favs.size() == antes + 1 && salvo, "the favorite is saved with its fields (Studio Porto)");
    return falhas;
}

} // namespace matriz::ui

