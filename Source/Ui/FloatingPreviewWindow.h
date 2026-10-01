#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <string>
#include <optional>

#include "EventBus.h"
#include "ProjetoAberto.h"
#include "FichaPanelComponent.h"
#include "PreviewComponent.h"
#include "AudioWorkspace.h"
#include "Tokens.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::ui {

namespace {

class FloatingFichaResizerBar : public juce::Component {
public:
    FloatingFichaResizerBar() {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    std::function<void(int deltaX)> aoArrastar;

    void mouseDown(const juce::MouseEvent& e) override {
        startX_ = e.getScreenX();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        int delta = e.getScreenX() - startX_;
        startX_ = e.getScreenX();
        if (aoArrastar) aoArrastar(delta);
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = matriz::ui::tema();
        g.setColour(tk.borda);
        g.drawVerticalLine(getWidth() / 2, 0.0f, static_cast<float>(getHeight()));
    }

private:
    int startX_ = 0;
};

} // namespace

// Floating document window showing a rich media preview (left) and metadata inspector
// panel (right) with two columns of metadata by default, resizable/collapsible divider,
// large navigation arrows, and keyboard arrow key support.
class FloatingPreviewWindow : public juce::DocumentWindow {
public:
    using CallbackNavegar = std::function<std::optional<std::string>(const std::string& itemIdAtual, int direcao)>;
    using CallbackItemMudou = std::function<void(const std::string& novoItemId)>;

    FloatingPreviewWindow(ProjetoAberto& projeto,
                          const std::string& itemId,
                          std::function<void()> aoFecharCallback,
                          CallbackNavegar aoNavegarCallback = nullptr,
                          CallbackItemMudou aoItemMudouCallback = nullptr)
        : juce::DocumentWindow({}, tema().fundo, juce::DocumentWindow::allButtons),
          projeto_(projeto),
          aoFecharCallback_(std::move(aoFecharCallback)),
          aoNavegarCallback_(std::move(aoNavegarCallback)),
          aoItemMudouCallback_(std::move(aoItemMudouCallback))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);

        auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        auto displayArea = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int>(0, 0, 1920, 1080);
        int w = juce::jlimit(1150, displayArea.getWidth(), static_cast<int>(displayArea.getWidth() * 0.90f));
        int h = juce::jlimit(720, displayArea.getHeight(), static_cast<int>(displayArea.getHeight() * 0.88f));

        setResizeLimits(980, 620, displayArea.getWidth(), displayArea.getHeight());
        // Item 3 (lista nova de hoje): sem isto, nada nesta janela tem o
        // foco de teclado de verdade — nem o ContentComponent (que
        // setWantsKeyboardFocus(true) sozinho não é suficiente pra roubar
        // foco de ninguém), nem por consequência esta DocumentWindow. Os
        // atalhos E/H/K/P simplesmente não chegavam a lugar nenhum.
        setWantsKeyboardFocus(true);

        contentComp_ = new ContentComponent(projeto_, itemId,
            [this](int dir) { navegarItem(dir); },
            [this] { closeButtonPressed(); }
        );
        setContentOwned(contentComp_, true);
        // NEST: escolher outro arquivo na coluna do grupo troca o que o preview mostra (e a capa).
        contentComp_->aoMudouDeArquivo = [this](const std::string& novoId) {
            atualizarTituloJanela(novoId);
            if (aoItemMudouCallback_) aoItemMudouCallback_(novoId);
        };

        atualizarTituloJanela(itemId);

        centreWithSize(w, h);
        setVisible(true);
        toFront(true);
        if (contentComp_) contentComp_->grabKeyboardFocus();
    }

    ~FloatingPreviewWindow() override = default;

    void atualizarTituloJanela(const std::string& itemId) {
        auto info = projeto_.arquivoPrincipal(itemId);
        juce::String windowTitle;
        if (info) {
            windowTitle = juce::File(info->caminhoAbsoluto).getFileName();
        } else {
            auto it = projeto_.obterItemResumo(itemId);
            if (it) {
                windowTitle = juce::String(it->nomeOriginalArquivo.empty() ? it->titulo : it->nomeOriginalArquivo);
            }
        }
        setName(windowTitle.isEmpty() ? "Preview" : windowTitle);
    }

    void navegarItem(int direcao) {
        if (!contentComp_) return;
        std::string atual = contentComp_->itemIdAtual();
        if (atual.empty()) return;

        std::optional<std::string> proximoId;
        if (aoNavegarCallback_) {
            proximoId = aoNavegarCallback_(atual, direcao);
        }

        if (proximoId && !proximoId->empty()) {
            contentComp_->carregarAsset(*proximoId);
            atualizarTituloJanela(*proximoId);
            if (aoItemMudouCallback_) {
                aoItemMudouCallback_(*proximoId);
            }
        }
    }

    void closeButtonPressed() override {
        juce::Component::SafePointer<FloatingPreviewWindow> safe(this);
        juce::MessageManager::callAsync([safe, cb = aoFecharCallback_]() mutable {
            if (cb) cb();
        });
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::pageUpKey) {
            navegarItem(-1);
            return true;
        }
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::pageDownKey) {
            navegarItem(1);
            return true;
        }
        if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::spaceKey) {  // Espaço abre/fecha, como Esc
            closeButtonPressed();
            return true;
        }
        // Item 3 (nova lista): os atalhos de marcação (E/H/K/P/W) só
        // existiam dentro do ContentComponent, mas nada chama
        // grabKeyboardFocus() nele — então quase sempre é a DocumentWindow
        // que recebe a tecla de verdade, e essa aqui não tratava nada além
        // de navegação/Esc. Delega pro mesmo tratamento do conteúdo, que
        // marca o item aberto no preview e repinta o selo.
        if (contentComp_ && contentComp_->tratarAtalhoDeMarcacao(key)) {
            return true;
        }
        return juce::DocumentWindow::keyPressed(key);
    }

private:
    // NEST: coluna com todos os arquivos do grupo. Clicar num arquivo o escolhe como capa do nest.
    class NestColuna : public juce::Component {
    public:
        struct Linha {
            std::string id;
            juce::String nome;
            juce::Image miniatura;
            bool capa = false, atual = false;
        };
        static constexpr int kAlturaLinha = 78;
        std::function<void(const std::string&)> aoEscolher;

        void definir(std::vector<Linha> linhas) {
            linhas_ = std::move(linhas);
            setSize(getWidth(), juce::jmax(kAlturaLinha, static_cast<int>(linhas_.size()) * kAlturaLinha));
            repaint();
        }
        int numeroDeLinhas() const { return static_cast<int>(linhas_.size()); }

        void paint(juce::Graphics& g) override {
            const auto& tk = tema();
            g.fillAll(tk.painel);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
                const auto& l = linhas_[static_cast<size_t>(i)];
                juce::Rectangle<int> r(0, i * kAlturaLinha, getWidth(), kAlturaLinha);
                if (l.atual) { g.setColour(tk.acento.withAlpha(0.22f)); g.fillRect(r); }
                g.setColour(tk.borda.withAlpha(0.5f));
                g.fillRect(r.getX(), r.getBottom() - 1, r.getWidth(), 1);
                auto miniArea = r.reduced(6).removeFromTop(kAlturaLinha - 24).withWidth(getWidth() - 12);
                if (l.miniatura.isValid()) {
                    g.drawImage(l.miniatura, miniArea.toFloat(), juce::RectanglePlacement::centred);
                } else {
                    g.setColour(tk.painelAlt);
                    g.fillRoundedRectangle(miniArea.toFloat(), 3.0f);
                }
                if (l.capa) {  // selo da capa
                    juce::Rectangle<int> selo(miniArea.getX() + 3, miniArea.getY() + 3, 44, 15);
                    g.setColour(juce::Colour(0xe6111827));
                    g.fillRoundedRectangle(selo.toFloat(), 7.0f);
                    g.setColour(juce::Colour(0xff38bdf8));
                    g.drawRoundedRectangle(selo.toFloat(), 7.0f, 1.0f);
                    g.setColour(juce::Colours::white);
                    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
                    g.drawText("COVER", selo, juce::Justification::centred);
                    g.setFont(juce::Font(juce::FontOptions(11.0f)));
                }
                g.setColour(tk.textoPrimario);
                g.drawText(l.nome, r.withTop(r.getBottom() - 22).reduced(6, 0), juce::Justification::centredLeft, true);
            }
        }

        void mouseDown(const juce::MouseEvent& e) override {
            const int i = e.y / kAlturaLinha;
            if (i >= 0 && i < static_cast<int>(linhas_.size()) && aoEscolher) aoEscolher(linhas_[static_cast<size_t>(i)].id);
        }

    private:
        std::vector<Linha> linhas_;
    };

    class ContentComponent : public juce::Component, private EventBusListener {
    public:
        ContentComponent(ProjetoAberto& projeto,
                         const std::string& itemId,
                         std::function<void(int)> aoNavegar,
                         std::function<void()> aoFechar)
            : projeto_(projeto),
              itemId_(itemId),
              aoNavegar_(std::move(aoNavegar)),
              aoFechar_(std::move(aoFechar))
        {
            setWantsKeyboardFocus(true);
            // Marcas feitas no grid (E/H/K/P/W) também aparecem aqui.
            EventBus::obterInstancia().registrarListener(this);

            // Large, visible navigation arrow buttons
            btnAnterior_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x97\x80"));
            btnAnterior_->setTooltip("Previous Item in Grid (Left Arrow key)");
            btnAnterior_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
            btnAnterior_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
            btnAnterior_->onClick = [this] { if (aoNavegar_) aoNavegar_(-1); };
            addAndMakeVisible(*btnAnterior_);

            btnProximo_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x96\xb6"));
            btnProximo_->setTooltip("Next Item in Grid (Right Arrow key)");
            btnProximo_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
            btnProximo_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
            btnProximo_->onClick = [this] { if (aoNavegar_) aoNavegar_(1); };
            addAndMakeVisible(*btnProximo_);

            lblTitulo_ = std::make_unique<juce::Label>("", "");
            lblTitulo_->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            lblTitulo_->setColour(juce::Label::textColourId, tema().textoPrimario);
            addAndMakeVisible(*lblTitulo_);

            btnToggleFicha_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x96\xb6 Metadata"));
            btnToggleFicha_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
            btnToggleFicha_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
            btnToggleFicha_->setTooltip("Collapse / Expand Metadata Inspector");
            btnToggleFicha_->onClick = [this] {
                fichaColapsada_ = !fichaColapsada_;
                if (fichaColapsada_) {
                    btnToggleFicha_->setButtonText(juce::CharPointer_UTF8("\xe2\x97\x80 Metadata"));
                    btnToggleFicha_->setTooltip("Show Metadata Inspector");
                    if (fichaPanel_) fichaPanel_->setVisible(false);
                    if (resizerBar_) resizerBar_->setVisible(false);
                } else {
                    btnToggleFicha_->setButtonText(juce::CharPointer_UTF8("\xe2\x96\xb6 Metadata"));
                    btnToggleFicha_->setTooltip("Collapse Metadata Inspector");
                    if (fichaPanel_) fichaPanel_->setVisible(true);
                    if (resizerBar_) resizerBar_->setVisible(true);
                }
                resized();
            };
            addAndMakeVisible(*btnToggleFicha_);

            resizerBar_ = std::make_unique<FloatingFichaResizerBar>();
            resizerBar_->aoArrastar = [this](int deltaX) {
                if (fichaColapsada_) return;
                larguraFicha_ -= deltaX;
                larguraFicha_ = juce::jlimit(kLarguraFichaMin, kLarguraFichaMax, larguraFicha_);
                resized();
            };
            addAndMakeVisible(*resizerBar_);

            fichaPanel_ = std::make_unique<FichaPanelComponent>(projeto_);
            addAndMakeVisible(*fichaPanel_);

            nestColuna_ = std::make_unique<NestColuna>();
            nestColuna_->aoEscolher = [this](const std::string& id) { escolherArquivoDoNest(id); };
            nestViewport_ = std::make_unique<juce::Viewport>();
            nestViewport_->setViewedComponent(nestColuna_.get(), false);
            nestViewport_->setScrollBarsShown(true, false);
            nestViewport_->setVisible(false);
            addAndMakeVisible(*nestViewport_);

            carregarAsset(itemId_);
        }

        ~ContentComponent() override {
            EventBus::obterInstancia().removerListener(this);
            if (nestViewport_) nestViewport_->setViewedComponent(nullptr, false);
            if (escuta_) escuta_->descarregar();
        }

        std::function<void(const std::string&)> aoMudouDeArquivo;

        // NEST: o arquivo escolhido na coluna vira a capa e passa a ser mostrado no preview.
        void escolherArquivoDoNest(const std::string& novoId) {
            if (novoId == itemId_ || nestId_.empty()) return;
            projeto_.definirCapaDoNest(nestId_, novoId);
            carregarAsset(novoId);
            if (aoMudouDeArquivo) aoMudouDeArquivo(novoId);
        }

        void atualizarColunaNest() {
            std::vector<NestColuna::Linha> linhas;
            nestId_.clear();
            if (auto nest = projeto_.nestDoItem(itemId_)) {
                nestId_ = nest->nestId;
                for (const auto& id : projeto_.membrosDoNest(nestId_)) {
                    NestColuna::Linha l;
                    l.id = id;
                    std::string tit, tipo, cod;
                    projeto_.obterItemInfo(id, tit, tipo, cod);
                    auto info = projeto_.arquivoPrincipal(id);
                    l.nome = info ? juce::File(info->caminhoAbsoluto).getFileName() : juce::String::fromUTF8(tit.c_str());
                    if (auto caminho = projeto_.caminhoMiniaturaPrincipal(id)) l.miniatura = juce::ImageFileFormat::loadFrom(juce::File(*caminho));
                    l.capa = (id == nest->capaId);
                    l.atual = (id == itemId_);
                    linhas.push_back(std::move(l));
                }
            }
            const bool mostrar = linhas.size() > 1;
            if (nestColuna_) {
                nestColuna_->setSize(kLarguraNest - nestViewport_->getScrollBarThickness(), nestColuna_->getHeight());
                nestColuna_->definir(std::move(linhas));
            }
            if (nestViewport_) nestViewport_->setVisible(mostrar);
            if (!mostrar) nestId_.clear();
        }

        const std::string& itemIdAtual() const { return itemId_; }

        // Item 3 (nova lista): os atalhos E/H/K/P/W só existiam aqui dentro,
        // mas nada chama grabKeyboardFocus() neste componente — então a tecla
        // quase sempre chega primeiro (e só) na DocumentWindow por fora, que
        // não tinha esse tratamento. Extraído pra método público pra poder
        // ser chamado tanto daqui (se o foco algum dia estiver aqui) quanto
        // de fora, por FloatingPreviewWindow::keyPressed.
        bool tratarAtalhoDeMarcacao(const juce::KeyPress& key) {
            bool semModificadores = !key.getModifiers().isCommandDown() &&
                                    !key.getModifiers().isCtrlDown() &&
                                    !key.getModifiers().isAltDown();
            if (!semModificadores || itemId_.empty()) return false;

            auto c = juce::CharacterFunctions::toUpperCase(key.getTextCharacter());
            ProjetoAberto::TipoMarcacao tipo;
            bool temTipo = true;
            switch (c) {
                case 'H': tipo = ProjetoAberto::TipoMarcacao::Html;      break;
                case 'K': tipo = ProjetoAberto::TipoMarcacao::Zip;       break;
                // P (Send to Print) e W (Watermark) só fazem sentido pra fotos.
                case 'P': case 'W': {
                    auto resumo = projeto_.obterItemResumo(itemId_);
                    bool ehFoto = resumo && matriz::ingest::categoriaPorExtensao(juce::String(resumo->extensaoArquivo)) ==
                                                matriz::ingest::CategoriaMidia::Imagem;
                    if (!ehFoto) return false;
                    tipo = (c == 'P') ? ProjetoAberto::TipoMarcacao::Print : ProjetoAberto::TipoMarcacao::Watermark;
                    break;
                }
                default: temTipo = false; break;
            }
            if (temTipo) {
                projeto_.alternarMarcacao(tipo, {itemId_});
                if (fichaPanel_) fichaPanel_->mostrarItem(itemId_);
                repaint();
                return true;
            }
            if (c == 'E') {
                projeto_.alternarMarcadoRevisado({itemId_});
                if (fichaPanel_) fichaPanel_->mostrarItem(itemId_);
                repaint();
                return true;
            }
            return false;
        }

        void carregarAsset(const std::string& newItemId) {
            itemId_ = newItemId;

            // Update title label
            std::string tit, tipo, cod;
            projeto_.obterItemInfo(itemId_, tit, tipo, cod);
            auto info = projeto_.arquivoPrincipal(itemId_);
            juce::String fname = info ? juce::File(info->caminhoAbsoluto).getFileName() : juce::String(tit);
            lblTitulo_->setText(juce::String(cod) + "  \xe2\x80\xa2  " + fname, juce::dontSendNotification);

            // Dispose old preview/audio workspaces
            if (escuta_) {
                escuta_->descarregar();
                escuta_.reset();
            }
            preview_.reset();

            if (info) {
                juce::File arquivo(info->caminhoAbsoluto);
                auto cat = matriz::ingest::categoriaPorExtensao(arquivo);

                if (cat == matriz::ingest::CategoriaMidia::Audio) {
                    auto it = projeto_.obterItemResumo(itemId_);
                    ItemResumo snap = it ? *it : ItemResumo{};
                    escuta_ = std::make_unique<AudioWorkspace>(projeto_);
                    escuta_->aoFechar = aoFechar_;
                    escuta_->carregarAsset(snap, arquivo, nullptr);
                    addAndMakeVisible(*escuta_);
                } else {
                    preview_ = std::make_unique<PreviewComponent>(projeto_);
                    preview_->aoFechar = aoFechar_;
                    preview_->aoNavegar = [this](int dir) { if (aoNavegar_) aoNavegar_(dir); };
                    preview_->mostrarItem(itemId_);
                    // Item 2 (nova lista): esta janela já tem suas próprias
                    // setas na barra superior — as do PreviewComponent
                    // duplicariam a navegação.
                    preview_->definirBotoesNavegacaoVisiveis(false);
                    addAndMakeVisible(*preview_);
                }
            } else {
                preview_ = std::make_unique<PreviewComponent>(projeto_);
                preview_->aoFechar = aoFechar_;
                preview_->aoNavegar = [this](int dir) { if (aoNavegar_) aoNavegar_(dir); };
                preview_->mostrarItem(itemId_);
                preview_->definirBotoesNavegacaoVisiveis(false);
                addAndMakeVisible(*preview_);
            }

            if (fichaPanel_) {
                fichaPanel_->mostrarItem(itemId_);
            }

            auto refreshFicha = [this] {
                if (fichaPanel_ && !itemId_.empty()) fichaPanel_->mostrarItem(itemId_);
            };
            if (escuta_) escuta_->aoMudarMarcadores = refreshFicha;
            if (preview_) preview_->aoMudarMarcadores = refreshFicha;

            atualizarColunaNest();
            resized();
            repaint();
        }

        void paint(juce::Graphics& g) override {
            g.fillAll(tema().fundo);
            g.setColour(tema().borda);
            g.drawHorizontalLine(kAlturaTopBar - 1, 0.0f, static_cast<float>(getWidth()));
        }

        // item 2 (nova lista): a borda/selos de marcador (E.10) eram
        // desenhados em paint(), mas preview_/escuta_ são filhos que
        // ocupam exatamente previewArea_ e pintam POR CIMA logo em
        // seguida — cobrindo tudo, então nada aparecia. paintOverChildren
        // roda depois dos filhos, exatamente pra esse tipo de overlay.
        void paintOverChildren(juce::Graphics& g) override {
            // Item E.10: mesmo feedback visual do atalho no grid (letra pro
            // H/K/P/W, borda amarela pro E) — só que sobre a área de preview
            // inteira, já que aqui só existe UM item.
            if (previewArea_.isEmpty() || itemId_.empty()) return;

            // Uma query só (não obterItemResumo, que dá stat no arquivo) — isto
            // roda a cada repaint.
            bool revisado = projeto_.itemMarcadoRevisado(itemId_);
            if (revisado) {
                const juce::Colour kZebraYellow{0xffFFEE00};
                g.setColour(kZebraYellow);
                g.drawRect(previewArea_, 3);
            }

            int stampRight = previewArea_.getRight() - 4;
            auto desenharSelo = [&](const juce::String& letra, juce::Colour fundo, juce::Colour texto) {
                juce::Rectangle<int> selo(stampRight - 20, previewArea_.getY() + 4, 20, 20);
                g.setColour(fundo);
                g.fillRoundedRectangle(selo.toFloat(), 4.0f);
                g.setColour(texto);
                g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
                g.drawText(letra, selo, juce::Justification::centred);
                stampRight -= 24;
            };
            if (projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Print, itemId_))
                desenharSelo("P", juce::Colour(0xffff6b00), juce::Colours::white);
            if (projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Zip, itemId_))
                desenharSelo("K", juce::Colour(0xff0077ff), juce::Colours::white);
            if (projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Html, itemId_))
                desenharSelo("H", juce::Colour(0xff39ff14), juce::Colours::black);
            if (projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Watermark, itemId_))
                desenharSelo("W", juce::Colour(0xffffcc00), juce::Colours::black);
            // E: fundo preto e letra amarela zebra, pra não confundir com o W amarelo.
            if (revisado)
                desenharSelo("E", juce::Colours::black, juce::Colour(0xffFFEE00));
        }

        void aoItemAlterado(const EventoItemAlterado& e) override {
            if (e.tipoAlteracao == "nest") {  // capa trocada / nest desfeito em outro lugar
                juce::Component::SafePointer<ContentComponent> seguro(this);
                juce::MessageManager::callAsync([seguro] { if (seguro) { seguro->atualizarColunaNest(); seguro->resized(); } });
                return;
            }
            if (e.tipoAlteracao != "marcado_revisado" && e.tipoAlteracao != "marcacao" &&
                e.tipoAlteracao != "publicacao") return;
            if (!e.itemId.empty() && e.itemId != itemId_) return;
            if (e.itemId.empty() && !e.itemIds.empty() &&
                std::find(e.itemIds.begin(), e.itemIds.end(), itemId_) == e.itemIds.end()) return;  // lote que não inclui este item
            juce::Component::SafePointer<ContentComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis] { if (safeThis) safeThis->repaint(); });
        }

        void resized() override {
            auto area = getLocalBounds();

            // Top Bar
            auto topBar = area.removeFromTop(kAlturaTopBar).reduced(8, 4);
            if (btnAnterior_) {
                btnAnterior_->setBounds(topBar.removeFromLeft(44));
                topBar.removeFromLeft(6);
            }
            if (btnProximo_) {
                btnProximo_->setBounds(topBar.removeFromLeft(44));
                topBar.removeFromLeft(14);
            }
            if (btnToggleFicha_) {
                btnToggleFicha_->setBounds(topBar.removeFromRight(100));
                topBar.removeFromRight(8);
            }
            if (lblTitulo_) {
                lblTitulo_->setBounds(topBar);
            }

            // NEST: coluna com os arquivos do grupo, à esquerda de tudo.
            if (nestViewport_ && nestViewport_->isVisible()) {
                auto colunaArea = area.removeFromLeft(kLarguraNest);
                nestViewport_->setBounds(colunaArea);
                nestColuna_->setSize(kLarguraNest - nestViewport_->getScrollBarThickness(), nestColuna_->getHeight());
            }

            // Main Content Split: Media preview (left) | Resizer Bar | Ficha (right)
            if (!fichaColapsada_ && fichaPanel_) {
                fichaPanel_->setVisible(true);
                auto fichaArea = area.removeFromRight(larguraFicha_);
                fichaPanel_->setBounds(fichaArea);

                if (resizerBar_) {
                    resizerBar_->setVisible(true);
                    resizerBar_->setBounds(fichaArea.getX() - 4, fichaArea.getY(), 6, fichaArea.getHeight());
                }
            } else {
                if (fichaPanel_) fichaPanel_->setVisible(false);
                if (resizerBar_) resizerBar_->setVisible(false);
            }

            if (preview_) preview_->setBounds(area);
            if (escuta_) escuta_->setBounds(area);
            previewArea_ = area;
        }

        bool keyPressed(const juce::KeyPress& key) override {
            if (key == juce::KeyPress::leftKey || key == juce::KeyPress::pageUpKey) {
                if (aoNavegar_) { aoNavegar_(-1); return true; }
            }
            if (key == juce::KeyPress::rightKey || key == juce::KeyPress::pageDownKey) {
                if (aoNavegar_) { aoNavegar_(1); return true; }
            }
            if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::spaceKey) {
                if (aoFechar_) { aoFechar_(); return true; }
            }

            // Item E.10: os atalhos de marcação valem também no preview, e
            // agem sobre o item que está aberto nele — mesmas teclas, mesmo
            // efeito de toggle que MosaicoComponent::keyPressed já faz na
            // grade, só que sempre sobre um único item (o deste preview).
            return tratarAtalhoDeMarcacao(key);
        }

    private:
        static constexpr int kAlturaTopBar = 42;
        static constexpr int kLarguraNest = 176;
        std::unique_ptr<NestColuna> nestColuna_;
        std::unique_ptr<juce::Viewport> nestViewport_;
        std::string nestId_;
        static constexpr int kLarguraFichaMin = 300;
        static constexpr int kLarguraFichaMax = 950;
        int larguraFicha_ = 620; // 620px default enables 2 metadata columns
        bool fichaColapsada_ = false;
        juce::Rectangle<int> previewArea_;

        ProjetoAberto& projeto_;
        std::string itemId_;
        std::function<void(int)> aoNavegar_;
        std::function<void()> aoFechar_;

        std::unique_ptr<juce::TextButton> btnAnterior_;
        std::unique_ptr<juce::TextButton> btnProximo_;
        std::unique_ptr<juce::Label> lblTitulo_;
        std::unique_ptr<juce::TextButton> btnToggleFicha_;
        std::unique_ptr<FloatingFichaResizerBar> resizerBar_;

        std::unique_ptr<PreviewComponent> preview_;
        std::unique_ptr<AudioWorkspace> escuta_;
        std::unique_ptr<FichaPanelComponent> fichaPanel_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
    };

    ProjetoAberto& projeto_;
    ContentComponent* contentComp_ = nullptr;
    std::function<void()> aoFecharCallback_;
    CallbackNavegar aoNavegarCallback_;
    CallbackItemMudou aoItemMudouCallback_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloatingPreviewWindow)
};

} // namespace matriz::ui
