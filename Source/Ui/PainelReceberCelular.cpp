#include "PainelReceberCelular.h"

#include "../Celular/QrCodeImagem.h"
#include "../I18n/Strings.h"
#include "LogosTipoArquivo.h"
#include "Tokens.h"

namespace matriz::ui {

using matriz::i18n::t;

namespace {
constexpr int kLado = 270;

juce::String bytesLegiveis(juce::int64 b) { return juce::File::descriptionOfSizeInBytes(b); }
} // namespace

PainelReceberCelular::PainelReceberCelular(matriz::celular::OpcoesServidor opcoes, std::shared_ptr<RespostaReceberCelular> resposta,
                                           ModoImportacaoCelular modo)
    : resposta_(std::move(resposta)), modo_(modo) {
    setComponentID("cel.painel");
    setWantsKeyboardFocus(false);

    opcoes.idioma = matriz::i18n::t("auto.painelrecebercelular.a60c9f");
    juce::String erro;
    if (!servidor_.iniciar(opcoes, erro)) erro_ = t("light.cel.err.servidor");

    ips_ = matriz::celular::enderecosLocaisUtilizaveis();
    if ((opcoes.aceitarLoopback || matriz::celular::ServidorCelular::loopbackPermitidoParaTeste()) && ips_.empty()) ips_.push_back("127.0.0.1");

    const auto& tk = tema();
    const auto estilizar = [&](juce::Label& l, float tam, int estilo, juce::Colour cor) {
        l.setFont(fonteUi(tam, estilo));
        l.setColour(juce::Label::textColourId, cor);
        l.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(l);
    };

    estilizar(lblUrl_, tk.fonteCorpoGrande + 3.0f, juce::Font::bold, tk.textoPrimario);
    lblUrl_.setComponentID("cel.url");
    lblUrl_.setJustificationType(juce::Justification::centredLeft);
    lblUrl_.setInterceptsMouseClicks(true, false);

    estilizar(lblArquivo_, tk.fonteCorpoGrande, juce::Font::plain, tk.textoSecundario);
    lblArquivo_.setComponentID("cel.arquivo");
    estilizar(lblProgresso_, tk.fonteCorpoGrande + 1.0f, juce::Font::bold, tk.textoPrimario);
    lblProgresso_.setComponentID("cel.progresso");
    estilizar(lblDica_, tk.fonteCorpoGrande + 1.0f, juce::Font::plain, tk.textoSecundario);
    lblDica_.setComponentID("cel.dica");
    lblDica_.setJustificationType(juce::Justification::topLeft);
    estilizar(lblSemIp_, tk.fonteCorpoGrande, juce::Font::bold, tk.perigo);
    lblSemIp_.setComponentID("cel.semIp");
    lblSemIp_.setJustificationType(juce::Justification::topLeft);

    comboIp_.setComponentID("cel.comboIp");
    comboIp_.setTooltip(t("light.cel.escolherIp"));
    for (size_t i = 0; i < ips_.size(); ++i) comboIp_.addItem(juce::String(ips_[i]), static_cast<int>(i) + 1);
    if (!ips_.empty()) comboIp_.setSelectedId(1, juce::dontSendNotification);
    comboIp_.onChange = [this] { montarEndereco(); resized(); repaint(); };
    addChildComponent(comboIp_);
    comboIp_.setVisible(ips_.size() > 1);

    addAndMakeVisible(barra_);
    barra_.setComponentID("cel.barra");

    btnConcluir_ = std::make_unique<BotaoPilula>(t("light.cel.concluir"), BotaoPilula::Estilo::Primario);
    btnConcluir_->setComponentID("cel.btnConcluir");
    btnConcluir_->onClick = [this] { concluir(true); };
    addAndMakeVisible(*btnConcluir_);
    btnFechar_ = std::make_unique<BotaoPilula>(t("light.cel.fechar"), BotaoPilula::Estilo::Secundario);
    btnFechar_->setComponentID("cel.btnFechar");
    btnFechar_->onClick = [this] { concluir(false); };
    addAndMakeVisible(*btnFechar_);

    montarEndereco();
    atualizar();
    startTimerHz(4);
}

PainelReceberCelular::~PainelReceberCelular() {
    stopTimer();
    servidor_.pararEmSegundoPlano();  // fechou o painel, fechou o servidor (a porta já fica livre)
}

void PainelReceberCelular::montarEndereco() {
    url_.clear();
    qr_ = {};
    if (!servidor_.ativo()) return;
    const int sel = comboIp_.getSelectedId();
    const std::string ip = (sel >= 1 && sel <= static_cast<int>(ips_.size())) ? ips_[static_cast<size_t>(sel - 1)]
                                                                                 : (ips_.empty() ? std::string() : ips_.front());
    if (ip.empty()) return;
    const auto url = matriz::celular::montarUrl(ip, servidor_.porta(), servidor_.token().toStdString());
    url_ = juce::String(url);
    qr_ = matriz::celular::criarImagemQr(url, 6);
    lblUrl_.setText(url_, juce::dontSendNotification);
}

juce::String PainelReceberCelular::dicaFirewall(bool destacada) const {
#if JUCE_LINUX
    auto s = t("light.cel.firewall.linux").replace("{porta}", juce::String(servidor_.porta()));
#else
    auto s = t("light.cel.firewall.macwin");
#endif
    if (destacada) s = t("light.cel.semConexao") + "\n" + s;
    return s;
}

void PainelReceberCelular::atualizar() {
    const auto e = servidor_.estado();
    const bool semConexaoHa60s = servidor_.ativo() && !e.algumCelularConectou && (e.msDesdeInicio >= 60000 || forcarDica_);
    dicaDestacada_ = semConexaoHa60s;

    lblDica_.setText(erro_.isNotEmpty() ? erro_ : dicaFirewall(dicaDestacada_), juce::dontSendNotification);
    lblDica_.setColour(juce::Label::textColourId, dicaDestacada_ ? tema().alerta : tema().textoSecundario);
    lblDica_.setFont(fonteUi(tema().fonteCorpoGrande + 1.0f, dicaDestacada_ ? juce::Font::bold : juce::Font::plain));

    lblSemIp_.setVisible(ips_.empty());
    lblSemIp_.setText(t("light.cel.semRede"), juce::dontSendNotification);

    lblArquivo_.setText(e.arquivoAtual.isNotEmpty() ? t("light.cel.recebendo").replace("{nome}", e.arquivoAtual)
                                                    : (e.algumCelularConectou ? t("light.cel.conectado") : t("light.cel.aguardando")),
                        juce::dontSendNotification);

    const juce::String sepUtf8 = juce::String::fromUTF8("  \xc2\xb7  ");
    juce::String linha = t("light.cel.xdey").replace("{x}", juce::String(e.recebidos)).replace("{y}", juce::String(e.totalArquivos));
    if (e.bytesTotal > 0) linha += sepUtf8 + bytesLegiveis(e.bytesRecebidos) + " / " + bytesLegiveis(e.bytesTotal);
    if (e.bytesPorSegundo > 1024.0) linha += sepUtf8 + bytesLegiveis(static_cast<juce::int64>(e.bytesPorSegundo)) + "/s";
    if (e.jaExistiam > 0) linha += "\n" + t("light.cel.ignorados").replace("{n}", juce::String(e.jaExistiam));
    if (e.falhas > 0) linha += "\n" + t("light.cel.falhas").replace("{n}", juce::String(e.falhas));
    lblProgresso_.setText(linha, juce::dontSendNotification);

    progresso_ = e.bytesTotal > 0 ? juce::jlimit(0.0, 1.0, static_cast<double>(e.bytesRecebidos) / static_cast<double>(e.bytesTotal)) : 0.0;
    repaint();
}

void PainelReceberCelular::concluir(bool comIngestao) {
    const auto e = servidor_.estado();
    resposta_->concluiu = comIngestao;
    resposta_->recebidos = e.recebidos;
    resposta_->pastaSessao = servidor_.pastaSessao();
    stopTimer();
    if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(comIngestao ? 1 : 0);
}

bool PainelReceberCelular::tratarTecla(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        concluir(false);
        return true;
    }
    return false;
}

void PainelReceberCelular::paint(juce::Graphics& g) {
    const auto& tk = tema();
    // Cartão do QR (esquerda) e cartão das instruções (direita), no mesmo estilo dos cartões do Home.
    desenharCartao(g, rcCartaoQr_.toFloat(), tk.raioCartao, tk.cartao, true);
    desenharCartao(g, rcCartaoInfo_.toFloat(), tk.raioCartao, tk.cartao, true);
    desenharCartao(g, rcCartaoProgresso_.toFloat(), tk.raioCartao * 0.8f, tk.cartao, true);

    // ladrilho com ícone de celular + instruções
    auto tile = juce::Rectangle<float>(44.0f, 44.0f).withPosition((float) rcInstrucoes_.getX(), (float) rcInstrucoes_.getY());
    g.setColour(tk.tipoImagem.withAlpha(0.20f));
    g.fillRoundedRectangle(tile, 13.0f);
    desenharIcone(g, IconeUi::Celular, tile.reduced(10.0f), tk.tipoImagem);
    g.setColour(tk.textoPrimario);
    g.setFont(fonteUi(tk.fonteCorpoGrande + 3.0f, juce::Font::bold));
    g.drawFittedText(t(modo_ == ModoImportacaoCelular::WhatsApp ? "light.cel.instrucoesWa" : "light.cel.instrucoes"), rcInstrucoes_.withTrimmedLeft(58), juce::Justification::topLeft, 6);

    if (qr_.isValid()) {
        g.setColour(juce::Colours::white);
        g.fillRoundedRectangle(rcQr_.toFloat().expanded(6), 10.0f);
        g.drawImage(qr_, rcQr_.toFloat(), juce::RectanglePlacement::centred);
    } else {
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(rcQr_.toFloat(), 10.0f);
    }
    if (dicaDestacada_) {
        g.setColour(tk.alerta);
        g.drawRoundedRectangle(lblDica_.getBounds().toFloat().expanded(6, 4), 8.0f, 2.0f);
    }
}

void PainelReceberCelular::resized() {
    const auto& tk = tema();
    auto b = getLocalBounds();
    auto nav = b.removeFromBottom(tk.alturaBotaoPrimario);
    b.removeFromBottom(12);
    btnConcluir_->setBounds(nav.removeFromRight(juce::jmax(220, btnConcluir_->larguraIdeal() + 24)));
    nav.removeFromRight(10);
    btnFechar_->setBounds(nav.removeFromRight(juce::jmax(150, btnFechar_->larguraIdeal() + 24)));

    auto baixo = b.removeFromBottom(112);
    b.removeFromBottom(8);
    rcCartaoProgresso_ = baixo.reduced(0, 0);
    baixo = baixo.reduced(16, 8);

    // Coluna esquerda: QR. Direita: instruções, endereço, IP, dica.
    rcCartaoInfo_ = b.withTrimmedLeft(kLado + 24 + 8);
    rcCartaoQr_ = b.withWidth(kLado + 24);
    auto esquerda = b.removeFromLeft(kLado + 24);
    const int lado = juce::jmin(kLado, esquerda.getWidth() - 24, b.getHeight() - 8);
    rcQr_ = juce::Rectangle<int>(juce::jmax(60, lado), juce::jmax(60, lado)).withPosition(esquerda.getX() + 8, esquerda.getY() + 8);
    b.removeFromLeft(12);
    b = b.reduced(12, 12);

    rcInstrucoes_ = b.removeFromTop(112);
    lblUrl_.setBounds(b.removeFromTop(34));
    if (comboIp_.isVisible()) {
        b.removeFromTop(4);
        comboIp_.setBounds(b.removeFromTop(32).withWidth(juce::jmin(260, b.getWidth())));
    }
    lblSemIp_.setBounds(ips_.empty() ? b.removeFromTop(48) : juce::Rectangle<int>());
    b.removeFromTop(10);
    lblDica_.setBounds(b.removeFromTop(78).reduced(6, 2));

    // Progresso: ocupa a largura toda, abaixo do QR
    lblArquivo_.setBounds(baixo.removeFromTop(24));
    barra_.setBounds(baixo.removeFromTop(20).reduced(0, 1));
    baixo.removeFromTop(4);
    lblProgresso_.setBounds(baixo);
}

std::shared_ptr<PainelReceberCelular> mostrarReceberDoCelular(PainelOverlay& overlay, matriz::celular::OpcoesServidor opcoes,
                                                              std::function<void(RespostaReceberCelular)> aoFechar,
                                                              ModoImportacaoCelular modo) {
    auto resposta = std::make_shared<RespostaReceberCelular>();
    auto painel = std::make_shared<PainelReceberCelular>(std::move(opcoes), resposta, modo);

    PainelOverlay::Config cfg;
    cfg.titulo = t(modo == ModoImportacaoCelular::WhatsApp ? "light.cel.tituloWa" : "light.cel.titulo");
    cfg.conteudo = painel;
    cfg.larguraCartao = 860;
    cfg.alturaConteudo = 520;
    juce::Component::SafePointer<PainelReceberCelular> seguro(painel.get());
    cfg.aoTecla = [seguro](const juce::KeyPress& k) { return seguro != nullptr && seguro->tratarTecla(k); };

    overlay.mostrar(cfg, [resposta, aoFechar](PainelOverlay::Resultado) {
        if (aoFechar) aoFechar(*resposta);
    });
    return painel;
}

// ── Card "Importar do celular": GALLERY ou WHATSAPP ──────────────────────────────────────────────

EscolhaImportacaoCelular::EscolhaImportacaoCelular(bool sistema) : sistema_(sistema) {
    setComponentID("cel.escolha");
    setWantsKeyboardFocus(false);
    iconeGaleria_ = carregarLogoDeTipo("gallery.png");
    iconeWhatsapp_ = carregarLogoDeTipo("whatsapp.png");
    iconeAndroid_ = carregarLogoDeTipo("android.png");
    iconeApple_ = carregarLogoDeTipo("apple.png");
}

void EscolhaImportacaoCelular::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(44);  // dica
    const int gap = 24;
    const int larg = (r.getWidth() - gap) / 2;
    tiles_[0] = r.removeFromLeft(larg);
    r.removeFromLeft(gap);
    tiles_[1] = r;
}

int EscolhaImportacaoCelular::tileEm(juce::Point<int> p) const {
    for (int i = 0; i < 2; ++i)
        if (tiles_[i].contains(p)) return i;
    return -1;
}

void EscolhaImportacaoCelular::mouseMove(const juce::MouseEvent& e) {
    const int novo = tileEm(e.getPosition());
    if (novo != sobre_) {
        sobre_ = novo;
        setMouseCursor(novo >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EscolhaImportacaoCelular::mouseExit(const juce::MouseEvent&) {
    if (sobre_ != -1) { sobre_ = -1; repaint(); }
}

void EscolhaImportacaoCelular::mouseUp(const juce::MouseEvent& e) {
    const int i = tileEm(e.getPosition());
    if (i >= 0 && !e.mouseWasDraggedSinceMouseDown()) escolher(i == 0 ? ModoImportacaoCelular::Galeria : ModoImportacaoCelular::WhatsApp);
}

void EscolhaImportacaoCelular::escolher(ModoImportacaoCelular m) {
    if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(m == ModoImportacaoCelular::Galeria ? 1 : 2);
}

bool EscolhaImportacaoCelular::tratarTecla(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(0);
        return true;
    }
    return false;
}

void EscolhaImportacaoCelular::paint(juce::Graphics& g) {
    const auto p = moldura::paleta();
    g.setColour(p.tinta);
    g.setFont(fonteUi(14.0f));
    g.drawText(t(sistema_ ? "light.cel.escolha.dicaSistema" : "light.cel.escolha.dica"), getLocalBounds().removeFromTop(36),
               juce::Justification::centredLeft, true);

    for (int i = 0; i < 2; ++i) {
        auto r = tiles_[i].toFloat().reduced(2.0f);
        const bool ativo = sobre_ == i;
        g.setColour(p.campo);
        g.fillRoundedRectangle(r, 16.0f);
        g.setColour(ativo ? p.grafite : p.linha);
        g.drawRoundedRectangle(r.reduced(0.5f), 16.0f, ativo ? 2.0f : 1.2f);
        auto interno = r.reduced(16.0f);
        auto sub = interno.removeFromBottom(24.0f);
        auto rotulo = interno.removeFromBottom(30.0f);
        const float lado = juce::jmin(interno.getHeight(), 80.0f);
        const auto azulejo = interno.withSizeKeepingCentre(lado, lado);
        g.setColour(p.poco);
        g.fillRoundedRectangle(azulejo, 20.0f);
        const auto area = azulejo.reduced(lado * 0.24f);
        const auto& img = sistema_ ? (i == 0 ? iconeAndroid_ : iconeApple_) : (i == 0 ? iconeGaleria_ : iconeWhatsapp_);
        if (sistema_) {  // ANDROID/IPHONE: símbolo isolado já em #1B1917 e transparente; 40x40 pt centralizado no quadrado de 80x80
            if (img.isValid())
                g.drawImage(img, azulejo.withSizeKeepingCentre(40.0f, 40.0f), juce::RectanglePlacement::centred);
        } else if (img.isValid()) {
            if (i == 0) {  // o ícone da galeria é de um tom só: pinta com a cor de texto
                g.setColour(p.tinta);
                g.drawImage(img, area, juce::RectanglePlacement::centred, true);
            } else {
                g.setColour(juce::Colours::black);
                g.drawImage(img, area, juce::RectanglePlacement::centred);
            }
        }
        g.setColour(p.tinta);
        g.setFont(fonteUi(15.0f, juce::Font::bold).withExtraKerningFactor(0.06f));
        g.drawText(t(sistema_ ? (i == 0 ? "light.cel.android" : "light.cel.iphone") : (i == 0 ? "light.cel.gallery" : "light.cel.whatsapp")),
                   rotulo.toNearestInt(), juce::Justification::centred, true);
        g.setColour(p.mudo);
        g.setFont(fonteUi(13.0f));
        g.drawText(t(sistema_ ? (i == 0 ? "light.cel.escolha.subAndroid" : "light.cel.escolha.subIphone")
                              : (i == 0 ? "light.cel.escolha.subGaleria" : "light.cel.escolha.subWhatsapp")),
                   sub.toNearestInt(), juce::Justification::centred, true);
    }
}

void mostrarEscolhaImportacaoCelular(PainelOverlay& overlay, std::function<void(std::optional<ModoImportacaoCelular>)> aoConcluir,
                                     std::weak_ptr<EscolhaImportacaoCelular>* devolver) {
    auto conteudo = std::make_shared<EscolhaImportacaoCelular>();
    if (devolver != nullptr) *devolver = conteudo;

    PainelOverlay::Config cfg;
    cfg.titulo = t("light.cel.escolha.titulo");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.conteudo = conteudo;
    cfg.larguraCartao = 620;
    cfg.alturaConteudo = 300;
    cfg.botoes = {{t("light.ui.common.cancel"), 0, false, true}};
    juce::Component::SafePointer<EscolhaImportacaoCelular> seguro(conteudo.get());
    cfg.aoTecla = [seguro](const juce::KeyPress& k) { return seguro != nullptr && seguro->tratarTecla(k); };
    juce::Component::SafePointer<PainelOverlay> pOverlay(&overlay);
    overlay.mostrar(cfg, [aoConcluir, pOverlay](PainelOverlay::Resultado r) {
        if (!aoConcluir) return;
        if (r.botaoId == 1) aoConcluir(ModoImportacaoCelular::Galeria);
        else if (r.botaoId == 2) {
            // WHATSAPP abre a segunda tela: ANDROID = fluxo do WhatsApp de hoje; IPHONE = QR code da GALLERY.
            // Adiado: este callback roda de dentro do mouseUp do card que o overlay vai substituir.
            juce::MessageManager::callAsync([aoConcluir, pOverlay] {
                if (pOverlay == nullptr) { aoConcluir(std::nullopt); return; }
                mostrarEscolhaSistemaCelular(*pOverlay, [aoConcluir](std::optional<SistemaCelular> s) {
                    if (!s.has_value()) aoConcluir(std::nullopt);
                    else aoConcluir(*s == SistemaCelular::Android ? ModoImportacaoCelular::WhatsApp : ModoImportacaoCelular::Galeria);
                });
            });
        } else aoConcluir(std::nullopt);
    });
}

void mostrarEscolhaSistemaCelular(PainelOverlay& overlay, std::function<void(std::optional<SistemaCelular>)> aoConcluir) {
    auto conteudo = std::make_shared<EscolhaImportacaoCelular>(true);

    PainelOverlay::Config cfg;
    cfg.titulo = t("light.cel.escolha.titulo");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.conteudo = conteudo;
    cfg.larguraCartao = 620;
    cfg.alturaConteudo = 300;
    cfg.botoes = {{t("light.ui.common.cancel"), 0, false, true}};
    juce::Component::SafePointer<EscolhaImportacaoCelular> seguro(conteudo.get());
    cfg.aoTecla = [seguro](const juce::KeyPress& k) { return seguro != nullptr && seguro->tratarTecla(k); };
    overlay.mostrar(cfg, [aoConcluir](PainelOverlay::Resultado r) {
        if (!aoConcluir) return;
        if (r.botaoId == 1) aoConcluir(SistemaCelular::Android);
        else if (r.botaoId == 2) aoConcluir(SistemaCelular::IPhone);
        else aoConcluir(std::nullopt);
    });
}

} // namespace matriz::ui
