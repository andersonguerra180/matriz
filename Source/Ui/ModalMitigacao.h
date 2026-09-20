#pragma once

#include <JuceHeader.h>
#include <functional>
#include <optional>
#include <memory>
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

inline void retirarPeerDaTela(juce::Component& janela) {
    janela.setVisible(false);
    janela.removeFromDesktop();
}

class ModalTextoDialog : public juce::Component {
public:
    ModalTextoDialog(const juce::String& titulo, const juce::String& mensagem, const juce::String& valorInicial,
                     std::function<void(std::optional<juce::String>)> aoConcluir,
                     std::shared_ptr<juce::DialogWindow> janelaHolder)
        : aoConcluir_(std::move(aoConcluir)), janelaHolder_(janelaHolder) {
        
        const auto& tk = tema();
        lblTitulo_ = std::make_unique<juce::Label>("", titulo);
        lblTitulo_->setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
        addAndMakeVisible(*lblTitulo_);

        if (mensagem.isNotEmpty()) {
            lblMensagem_ = std::make_unique<juce::Label>("", mensagem);
            lblMensagem_->setFont(juce::Font(juce::FontOptions(12.5f)));
            lblMensagem_->setColour(juce::Label::textColourId, tk.textoSecundario);
            addAndMakeVisible(*lblMensagem_);
        }

        editor_ = std::make_unique<juce::TextEditor>();
        editor_->setFont(juce::Font(juce::FontOptions(13.5f)));
        editor_->setText(valorInicial);
        editor_->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
        editor_->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        editor_->setColour(juce::TextEditor::outlineColourId, tk.borda);
        editor_->setColour(juce::TextEditor::focusedOutlineColourId, tk.acento);
        editor_->setHighlightedRegion(juce::Range<int>(0, valorInicial.length()));
        editor_->onReturnKey = [this] { confirmar(); };
        editor_->onEscapeKey = [this] { cancelar(); };
        addAndMakeVisible(*editor_);

        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
        btnOk_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("OK") : "OK");
        btnOk_->setColour(juce::TextButton::buttonColourId, tk.acento);
        btnOk_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btnOk_->onClick = [this] { confirmar(); };
        addAndMakeVisible(*btnOk_);

        btnCancelar_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
        btnCancelar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnCancelar_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
        btnCancelar_->onClick = [this] { cancelar(); };
        addAndMakeVisible(*btnCancelar_);

        setSize(420, 180);
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.fundo);
        g.setColour(tk.borda);
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(16);
        lblTitulo_->setBounds(r.removeFromTop(24));
        r.removeFromTop(4);
        if (lblMensagem_) {
            lblMensagem_->setBounds(r.removeFromTop(20));
            r.removeFromTop(6);
        }
        editor_->setBounds(r.removeFromTop(32));
        
        auto bottomRow = r.removeFromBottom(34);
        btnCancelar_->setBounds(bottomRow.removeFromRight(100));
        bottomRow.removeFromRight(10);
        btnOk_->setBounds(bottomRow.removeFromRight(100));
    }

    void confirmar() {
        juce::String texto = editor_ ? editor_->getText() : "";
        if (aoConcluir_) aoConcluir_(texto);
        fechar();
    }

    void cancelar() {
        if (aoConcluir_) aoConcluir_(std::nullopt);
        fechar();
    }

    void fechar() {
        if (janelaHolder_) {
            janelaHolder_->exitModalState(0);
            janelaHolder_->setVisible(false);
        }
    }

    static void exibir(const juce::String& titulo, const juce::String& mensagem, const juce::String& valorInicial,
                       std::function<void(std::optional<juce::String>)> aoConcluir) {
        juce::MessageManager::callAsync([titulo, mensagem, valorInicial, aoConcluir]() {
            auto janela = std::make_shared<juce::DialogWindow>(
                titulo, tema().fundo, true, true);
            janela->setUsingNativeTitleBar(false);
            janela->setAlwaysOnTop(true);
            auto painel = std::make_unique<ModalTextoDialog>(titulo, mensagem, valorInicial, aoConcluir, janela);
            auto* ed = painel->editor_.get();
            janela->setContentOwned(painel.release(), true);
            janela->centreWithSize(420, 180);
            janela->setVisible(true);
            if (ed) {
                ed->grabKeyboardFocus();
                ed->setHighlightedRegion(juce::Range<int>(0, valorInicial.length()));
            }
            janela->enterModalState(true, juce::ModalCallbackFunction::create([janela](int) {
                janela->setVisible(false);
            }));
        });
    }

private:
    std::function<void(std::optional<juce::String>)> aoConcluir_;
    std::shared_ptr<juce::DialogWindow> janelaHolder_;
    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblMensagem_;
    std::unique_ptr<juce::TextEditor> editor_;
    std::unique_ptr<juce::TextButton> btnOk_;
    std::unique_ptr<juce::TextButton> btnCancelar_;
};

} // namespace matriz::ui
