#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <string>

#include "ProjetoAberto.h"
#include "FichaPanelComponent.h"
#include "PreviewComponent.h"
#include "AudioWorkspace.h"
#include "Tokens.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::ui {

// Floating non-modal document window showing a preview (left) and metadata
// panel (right) for a single archive asset. The main catalog stays visible
// and interactive behind this window.
class FloatingPreviewWindow : public juce::DocumentWindow {
public:
    FloatingPreviewWindow(ProjetoAberto& projeto,
                          const std::string& itemId,
                          std::function<void()> aoFecharCallback)
        : juce::DocumentWindow({}, tema().fundo, juce::DocumentWindow::allButtons),
          aoFecharCallback_(std::move(aoFecharCallback))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);

        // Build title from original filename or item title
        auto info = projeto.arquivoPrincipal(itemId);
        juce::String windowTitle;
        if (info) {
            windowTitle = juce::File(info->caminhoAbsoluto).getFileName();
        } else {
            // Fall back to ItemResumo.titulo
            auto itens = projeto.listarItens();
            for (const auto& it : itens) {
                if (it.id == itemId) {
                    windowTitle = juce::String(it.nomeOriginalArquivo.empty() ? it.titulo : it.nomeOriginalArquivo);
                    break;
                }
            }
        }
        setName(windowTitle.isEmpty() ? "Preview" : windowTitle);

        setContentOwned(new ContentComponent(projeto, itemId), true);

        centreWithSize(1100, 720);
        setVisible(true);
        toFront(true);
    }

    ~FloatingPreviewWindow() override = default;

    void closeButtonPressed() override {
        // Schedule destruction on the message thread so we don't delete
        // ourselves from within a JUCE event handler.
        juce::Component::SafePointer<FloatingPreviewWindow> safe(this);
        juce::MessageManager::callAsync([safe, cb = aoFecharCallback_]() mutable {
            if (cb) cb();
        });
    }

private:
    // -----------------------------------------------------------------------
    class ContentComponent : public juce::Component {
    public:
        ContentComponent(ProjetoAberto& projeto, const std::string& itemId)
            : projeto_(projeto), itemId_(itemId)
        {
            // Determine media type and build appropriate left-side preview
            auto info = projeto_.arquivoPrincipal(itemId_);
            if (info) {
                juce::File arquivo(info->caminhoAbsoluto);
                auto cat = matriz::ingest::categoriaPorExtensao(arquivo);

                if (cat == matriz::ingest::CategoriaMidia::Audio) {
                    auto itens = projeto_.listarItens();
                    ItemResumo snap;
                    for (const auto& it : itens) {
                        if (it.id == itemId_) { snap = it; break; }
                    }
                    escuta_ = std::make_unique<AudioWorkspace>(projeto_);
                    escuta_->carregarAsset(snap, arquivo, nullptr);
                    addAndMakeVisible(*escuta_);
                } else {
                    preview_ = std::make_unique<PreviewComponent>(projeto_);
                    preview_->mostrarItem(itemId_);
                    addAndMakeVisible(*preview_);
                }
            }

            fichaPanel_ = std::make_unique<FichaPanelComponent>(projeto_);
            fichaPanel_->mostrarItem(itemId_);
            addAndMakeVisible(*fichaPanel_);

            auto refreshFicha = [this] {
                if (fichaPanel_) fichaPanel_->mostrarItem(itemId_);
            };
            if (escuta_) escuta_->aoMudarMarcadores = refreshFicha;
            if (preview_) preview_->aoMudarMarcadores = refreshFicha;
        }

        ~ContentComponent() override {
            if (escuta_) escuta_->descarregar();
        }

        void paint(juce::Graphics& g) override {
            g.fillAll(tema().fundo);
            if (escuta_ || preview_) {
                g.setColour(tema().textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                auto hint = getLocalBounds().removeFromBottom(18).withTrimmedRight(360 + 8);
                g.drawText("Press M to add marker", hint, juce::Justification::centredRight);
            }
        }

        void resized() override {
            auto area = getLocalBounds();
            constexpr int kFichaWidth = 360;
            auto fichaArea = area.removeFromRight(kFichaWidth);
            if (fichaPanel_) fichaPanel_->setBounds(fichaArea);
            if (preview_) preview_->setBounds(area);
            if (escuta_) escuta_->setBounds(area);
        }

    private:
        ProjetoAberto& projeto_;
        std::string itemId_;
        std::unique_ptr<PreviewComponent> preview_;
        std::unique_ptr<AudioWorkspace> escuta_;
        std::unique_ptr<FichaPanelComponent> fichaPanel_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
    };
    // -----------------------------------------------------------------------

    std::function<void()> aoFecharCallback_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloatingPreviewWindow)
};

} // namespace matriz::ui
