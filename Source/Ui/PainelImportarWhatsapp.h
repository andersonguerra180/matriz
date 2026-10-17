#pragma once

#include <JuceHeader.h>

#include <memory>

#include "OverlayComponent.h"
#include "Tablet.h"

// IMPORTAR CONVERSAS DO WHATSAPP (pareamento): QR code grande com o endereço do app web do celular + IP:PORTA do
// computador, e o aviso "✓ Celular conectado" quando o celular faz o primeiro ping. O servidor é o do app
// (ServidorWhatsappApp), que fica ativo o tempo todo: fechar esta tela não o desliga.
namespace matriz::ui {

class PainelImportarWhatsapp : public juce::Component, private juce::Timer {
public:
    PainelImportarWhatsapp();
    ~PainelImportarWhatsapp() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool tratarTecla(const juce::KeyPress&);

    juce::String enderecoDoQrParaTeste() const { return urlQr_; }
    bool conectadoParaTeste() const { return conectado_; }

private:
    void timerCallback() override;
    void montar();

    juce::String urlQr_, rodape_, erro_;
    juce::Image qr_;
    bool conectado_ = false;
    std::unique_ptr<BotaoPilula> btnFechar_;
    juce::Rectangle<int> rcQr_, rcTexto_, rcCartaoQr_, rcCartaoTexto_, rcRodape_, rcStatus_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PainelImportarWhatsapp)
};

std::shared_ptr<PainelImportarWhatsapp> mostrarImportarWhatsapp(PainelOverlay& overlay, std::function<void()> aoFechar);

} // namespace matriz::ui
