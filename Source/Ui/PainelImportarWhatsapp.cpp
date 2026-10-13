#include "PainelImportarWhatsapp.h"

#include "../Celular/QrCodeImagem.h"
#include "../I18n/Strings.h"
#include "../Whatsapp/ServidorWhatsappApp.h"
#include "LogosTipoArquivo.h"
#include "Tokens.h"

namespace matriz::ui {

using matriz::i18n::t;

PainelImportarWhatsapp::PainelImportarWhatsapp() {
    setComponentID("wapp.painel");
    setWantsKeyboardFocus(false);
    btnFechar_ = std::make_unique<BotaoPilula>(t("light.cel.fechar"), BotaoPilula::Estilo::Secundario);
    btnFechar_->setComponentID("wapp.btnFechar");
    btnFechar_->onClick = [this] {
        if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(0);
    };
    addAndMakeVisible(*btnFechar_);
    montar();
    startTimerHz(2);
}

PainelImportarWhatsapp::~PainelImportarWhatsapp() { stopTimer(); }

void PainelImportarWhatsapp::montar() {
    auto& srv = matriz::whatsapp::ServidorWhatsappApp::instancia();
    if (!srv.ativo()) srv.iniciar();  // tenta de novo (ex.: a porta estava ocupada e foi liberada)
    erro_.clear();
    qr_ = {};
    urlQr_.clear();
    rodape_.clear();
    if (!srv.ativo()) {
        erro_ = t("light.wapp.erro").replace("{erro}", srv.erro().isEmpty() ? juce::String("?") : srv.erro());
    } else if (srv.ip().isEmpty()) {
        erro_ = t("light.wapp.semRede");
    } else {
        urlQr_ = srv.urlDePareamento();
        qr_ = matriz::celular::criarImagemQr(urlQr_.toStdString(), 6);
        rodape_ = srv.ip() + ":" + juce::String(srv.porta()) + juce::String::fromUTF8("   \xc2\xb7   ") + srv.nomeComputador();
    }
    conectado_ = srv.ativo() && srv.pareado();
}

void PainelImportarWhatsapp::timerCallback() {
    auto& srv = matriz::whatsapp::ServidorWhatsappApp::instancia();
    const bool novoConectado = srv.ativo() && srv.pareado();
    const bool precisaRemontar = (urlQr_.isEmpty() && erro_.isNotEmpty()) || (srv.ativo() && srv.urlDePareamento() != urlQr_);
    if (precisaRemontar) montar();
    if (novoConectado != conectado_) conectado_ = novoConectado;
    repaint();
}

bool PainelImportarWhatsapp::tratarTecla(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(0);
        return true;
    }
    return false;
}

void PainelImportarWhatsapp::paint(juce::Graphics& g) {
    const auto& tk = tema();
    desenharCartao(g, rcCartaoQr_.toFloat(), tk.raioCartao, tk.cartao, true);
    desenharCartao(g, rcCartaoTexto_.toFloat(), tk.raioCartao, tk.cartao, true);

    if (qr_.isValid()) {
        g.setColour(juce::Colours::white);
        g.fillRoundedRectangle(rcQr_.toFloat().expanded(6), 10.0f);
        g.drawImage(qr_, rcQr_.toFloat(), juce::RectanglePlacement::centred);
    } else {
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(rcQr_.toFloat(), 10.0f);
    }
    if (rodape_.isNotEmpty()) {
        g.setColour(tk.textoSecundario);
        g.setFont(fonteUi(tk.tamanhoFontePequena + 1.0f));
        g.drawText(rodape_, rcRodape_, juce::Justification::centred, true);
    }

    auto r = rcTexto_;
    const auto logo = carregarLogoDeTipo("whatsapp.png");
    auto ic = r.removeFromTop(44);
    if (logo.isValid()) g.drawImage(logo, ic.removeFromLeft(44).toFloat(), juce::RectanglePlacement::centred);
    r.removeFromTop(10);
    g.setColour(tk.textoPrimario);
    g.setFont(fonteUi(tk.fonteCorpoGrande + 2.0f, juce::Font::bold));
    g.drawFittedText(t("light.wapp.l1"), r.removeFromTop(78), juce::Justification::topLeft, 4);
    r.removeFromTop(6);
    g.setColour(tk.textoSecundario);
    g.setFont(fonteUi(tk.fonteCorpoGrande + 1.0f));
    g.drawFittedText(t("light.wapp.l2"), r.removeFromTop(68), juce::Justification::topLeft, 4);
    r.removeFromTop(8);

    if (erro_.isNotEmpty()) {
        g.setColour(tk.perigo);
        g.setFont(fonteUi(tk.fonteCorpoGrande, juce::Font::bold));
        g.drawFittedText(erro_, r.removeFromTop(56), juce::Justification::topLeft, 3);
    }
    if (conectado_) {
        g.setColour(juce::Colour(0xff16a34a));
        g.setFont(fonteUi(tk.fonteCorpoGrande + 3.0f, juce::Font::bold));
        g.drawText(t("light.wapp.conectado"), r.removeFromTop(30), juce::Justification::centredLeft, true);
    }
    g.setColour(tk.textoTerciario);
    g.setFont(fonteUi(tk.tamanhoFontePequena + 1.0f));
    g.drawFittedText(t("light.wapp.firewall"), rcStatus_, juce::Justification::bottomLeft, 3);
}

void PainelImportarWhatsapp::resized() {
    auto b = getLocalBounds();
    auto nav = b.removeFromBottom(44);
    btnFechar_->setBounds(nav.removeFromRight(160));
    b.removeFromBottom(10);
    auto esquerda = b.removeFromLeft(300);
    rcCartaoQr_ = esquerda;
    auto interno = esquerda.reduced(16);
    rcRodape_ = interno.removeFromBottom(24);
    interno.removeFromBottom(6);
    const int lado = juce::jmin(interno.getWidth(), interno.getHeight());
    rcQr_ = juce::Rectangle<int>(lado, lado).withCentre(interno.getCentre());
    b.removeFromLeft(10);
    rcCartaoTexto_ = b;
    rcTexto_ = b.reduced(18);
    rcStatus_ = rcTexto_.removeFromBottom(54);
}

std::shared_ptr<PainelImportarWhatsapp> mostrarImportarWhatsapp(PainelOverlay& overlay, std::function<void()> aoFechar) {
    auto painel = std::make_shared<PainelImportarWhatsapp>();
    PainelOverlay::Config cfg;
    cfg.titulo = t("light.wapp.titulo");
    cfg.conteudo = painel;
    cfg.larguraCartao = 860;
    cfg.alturaConteudo = 420;
    juce::Component::SafePointer<PainelImportarWhatsapp> seguro(painel.get());
    cfg.aoTecla = [seguro](const juce::KeyPress& k) { return seguro != nullptr && seguro->tratarTecla(k); };
    overlay.mostrar(cfg, [aoFechar](PainelOverlay::Resultado) { if (aoFechar) aoFechar(); });
    return painel;
}

} // namespace matriz::ui
