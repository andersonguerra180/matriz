#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <string>
#include <optional>

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

        contentComp_ = new ContentComponent(projeto_, itemId,
            [this](int dir) { navegarItem(dir); },
            [this] { closeButtonPressed(); }
        );
        setContentOwned(contentComp_, true);

        atualizarTituloJanela(itemId);

        centreWithSize(w, h);
        setVisible(true);
        toFront(true);
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
        if (key == juce::KeyPress::escapeKey) {
            closeButtonPressed();
            return true;
        }
        return juce::DocumentWindow::keyPressed(key);
    }

private:
    class ContentComponent : public juce::Component {
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

            carregarAsset(itemId_);
        }

        ~ContentComponent() override {
            if (escuta_) escuta_->descarregar();
        }

        const std::string& itemIdAtual() const { return itemId_; }

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
                    addAndMakeVisible(*preview_);
                }
            } else {
                preview_ = std::make_unique<PreviewComponent>(projeto_);
                preview_->aoFechar = aoFechar_;
                preview_->aoNavegar = [this](int dir) { if (aoNavegar_) aoNavegar_(dir); };
                preview_->mostrarItem(itemId_);
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

            resized();
            repaint();
        }

        void paint(juce::Graphics& g) override {
            g.fillAll(tema().fundo);
            g.setColour(tema().borda);
            g.drawHorizontalLine(kAlturaTopBar - 1, 0.0f, static_cast<float>(getWidth()));
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
        }

        bool keyPressed(const juce::KeyPress& key) override {
            if (key == juce::KeyPress::leftKey || key == juce::KeyPress::pageUpKey) {
                if (aoNavegar_) { aoNavegar_(-1); return true; }
            }
            if (key == juce::KeyPress::rightKey || key == juce::KeyPress::pageDownKey) {
                if (aoNavegar_) { aoNavegar_(1); return true; }
            }
            if (key == juce::KeyPress::escapeKey) {
                if (aoFechar_) { aoFechar_(); return true; }
            }
            return false;
        }

    private:
        static constexpr int kAlturaTopBar = 42;
        static constexpr int kLarguraFichaMin = 300;
        static constexpr int kLarguraFichaMax = 950;
        int larguraFicha_ = 620; // 620px default enables 2 metadata columns
        bool fichaColapsada_ = false;

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
