#include "PainelAndroidUsb.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

#include <thread>

namespace matriz::ui {

using matriz::i18n::t;
using namespace matriz::celular;

namespace {
constexpr std::uint32_t kRaiz = 0xffffffffu;  // LIBMTP_FILES_AND_FOLDERS_ROOT
constexpr int kAlturaLinha = 30;
constexpr int kColunaMarca = 34;

juce::String bytesLegiveis(std::uint64_t b) { return juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(b)); }
} // namespace

PainelAndroidUsb::PainelAndroidUsb(std::function<ConjuntoExistentes()> existentes, std::shared_ptr<RespostaReceberCelular> resposta,
                                   bool comVoltar)
    : sessao_(std::make_unique<SessaoAndroidUsb>()), resposta_(std::move(resposta)) {
    setComponentID("cel.usb.painel");
    const auto& tk = tema();

    const auto estilizar = [&](juce::Label& l, float tam, int estilo, juce::Colour cor) {
        l.setFont(fonteUi(tam, estilo));
        l.setColour(juce::Label::textColourId, cor);
        l.setMinimumHorizontalScale(0.7f);
        l.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(l);
    };
    estilizar(lblAparelho_, tk.fonteCorpoGrande + 3.0f, juce::Font::bold, tk.textoPrimario);
    estilizar(lblCaminho_, tk.fonteCorpoGrande, juce::Font::plain, tk.textoSecundario);
    estilizar(lblStatus_, tk.fonteCorpoGrande, juce::Font::plain, tk.textoSecundario);
    estilizar(lblRodape_, tk.fonteCorpoGrande, juce::Font::bold, tk.textoPrimario);
    lblStatus_.setJustificationType(juce::Justification::centred);

    comboMemoria_.setComponentID("cel.usb.memoria");
    comboMemoria_.onChange = [this] {
        caminho_.clear();
        recarregarPasta();
    };
    addChildComponent(comboMemoria_);

    lista_.setRowHeight(kAlturaLinha);
    lista_.setMultipleSelectionEnabled(true);
    lista_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    lista_.setOutlineThickness(0);
    addAndMakeVisible(lista_);
    addAndMakeVisible(barra_);
    barra_.setVisible(false);

    const auto botao = [this](std::unique_ptr<BotaoPilula>& b, const juce::String& texto, BotaoPilula::Estilo estilo, const char* id) {
        b = std::make_unique<BotaoPilula>(texto, estilo);
        b->setComponentID(id);
        addAndMakeVisible(*b);
    };
    botao(btnSubir_, juce::String::fromUTF8("\xe2\x97\x80"), BotaoPilula::Estilo::Secundario, "cel.usb.subir");
    btnSubir_->setTooltip(t("cel.usb.subir"));
    btnSubir_->onClick = [this] { subir(); };
    botao(btnTentar_, t("cel.usb.tentar"), BotaoPilula::Estilo::Primario, "cel.usb.tentar");
    btnTentar_->onClick = [this] { conectar(); };
    botao(btnTodos_, t("cel.usb.todos"), BotaoPilula::Estilo::Secundario, "cel.usb.todos");
    btnTodos_->onClick = [this] { marcarTodos(); };
    botao(btnImportar_, t("cel.usb.importar"), BotaoPilula::Estilo::Primario, "cel.usb.importar");
    btnImportar_->onClick = [this] { importar(); };
    botao(btnFechar_, t("light.ui.common.cancel"), BotaoPilula::Estilo::Secundario, "cel.usb.fechar");
    btnFechar_->onClick = [this] {
        if (estado_ == Estado::Copiando || estado_ == Estado::Preparando) { sessao_->cancelar(); return; }
        terminar(false, 0);
    };
    if (comVoltar) {
        botao(btnVoltar_, t("imp.voltar"), BotaoPilula::Estilo::Secundario, "cel.usb.voltar");
        btnVoltar_->onClick = [this] {
            if (estado_ == Estado::Copiando || estado_ == Estado::Preparando) return;
            if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(9);
        };
    }

    // "Já está no projeto": a mesma identidade (nome, tamanho) do QR code, lida fora da message thread.
    if (existentes) {
        juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
        std::thread([existentes, seguro] {
            auto conjunto = existentes();
            juce::MessageManager::callAsync([seguro, conjunto = std::move(conjunto)]() mutable {
                if (seguro == nullptr) return;
                seguro->existentes_ = std::move(conjunto);
                seguro->lista_.repaint();
            });
        }).detach();
    }

    conectar();
}

PainelAndroidUsb::~PainelAndroidUsb() {
    lista_.setModel(nullptr);
}

std::uint32_t PainelAndroidUsb::memoriaAtual() const {
    const int i = comboMemoria_.getSelectedItemIndex();
    if (i < 0 || i >= static_cast<int>(aparelho_.memorias.size())) return 0;
    return aparelho_.memorias[static_cast<size_t>(i)].id;
}

bool PainelAndroidUsb::jaNoProjeto(const EntradaAndroid& e) const {
    return !e.pasta && !existentes_.empty() &&
           existentes_.count(chaveDoArquivo(e.nome.toStdString(), static_cast<std::int64_t>(e.tamanho))) > 0;
}

void PainelAndroidUsb::conectar() {
    estado_ = Estado::Conectando;
    erro_.clear();
    itens_.clear();
    marcados_.clear();
    caminho_.clear();
    lista_.updateContent();
    atualizarRodape();
    juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
    sessao_->conectar([seguro](bool ok, AparelhoAndroid info, juce::String erro) {
        if (seguro == nullptr) return;
        auto& s = *seguro;
        if (!ok) {
            s.estado_ = Estado::Erro;
            s.erro_ = erro;
            s.atualizarRodape();
            return;
        }
        s.aparelho_ = std::move(info);
        s.comboMemoria_.clear(juce::dontSendNotification);
        for (size_t i = 0; i < s.aparelho_.memorias.size(); ++i) {
            const auto& m = s.aparelho_.memorias[i];
            juce::String rotulo = m.descricao.isNotEmpty() ? m.descricao : t("cel.usb.memoria");
            if (m.total > 0) rotulo += "  (" + t("cel.usb.livre").replace("{livre}", bytesLegiveis(m.livre)).replace("{total}", bytesLegiveis(m.total)) + ")";
            s.comboMemoria_.addItem(rotulo, static_cast<int>(i) + 1);
        }
        s.comboMemoria_.setSelectedItemIndex(0, juce::dontSendNotification);
        s.comboMemoria_.setVisible(s.aparelho_.memorias.size() > 1);
        s.estado_ = Estado::Pronto;
        s.recarregarPasta();
        s.resized();
    });
}

void PainelAndroidUsb::recarregarPasta() {
    if (estado_ != Estado::Pronto) return;
    const std::uint32_t pasta = caminho_.empty() ? kRaiz : caminho_.back().id;
    listando_ = true;
    const int geracao = ++geracaoLista_;
    itens_.clear();
    lista_.deselectAllRows();
    lista_.updateContent();
    atualizarRodape();
    juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
    sessao_->listar(memoriaAtual(), pasta, [seguro, geracao](bool ok, std::vector<EntradaAndroid> itens, juce::String erro) {
        if (seguro == nullptr || geracao != seguro->geracaoLista_) return;  // o usuário já abriu outra pasta
        auto& s = *seguro;
        s.listando_ = false;
        if (!ok) {
            s.estado_ = Estado::Erro;
            s.erro_ = erro;
        } else {
            s.itens_ = std::move(itens);
        }
        s.lista_.updateContent();
        s.lista_.repaint();
        s.atualizarRodape();
    });
}

void PainelAndroidUsb::abrirPasta(std::uint32_t pasta, const juce::String& nome) {
    caminho_.push_back({pasta, nome});
    recarregarPasta();
}

void PainelAndroidUsb::subir() {
    if (caminho_.empty() || estado_ != Estado::Pronto) return;
    caminho_.pop_back();
    recarregarPasta();
}

void PainelAndroidUsb::alternarMarca(int linha) {
    if (linha < 0 || linha >= static_cast<int>(itens_.size()) || estado_ != Estado::Pronto) return;
    const auto& e = itens_[static_cast<size_t>(linha)];
    const auto k = chaveDe(e);
    if (marcados_.erase(k) == 0) marcados_[k] = e;
    lista_.repaintRow(linha);
    atualizarRodape();
}

void PainelAndroidUsb::marcarTodos() {
    if (estado_ != Estado::Pronto || itens_.empty()) return;
    // Tudo marcado nesta pasta -> desmarca; senão marca o que falta (o que já está no projeto fica de fora).
    bool todos = true;
    for (const auto& e : itens_)
        if (!jaNoProjeto(e) && marcados_.count(chaveDe(e)) == 0) { todos = false; break; }
    for (const auto& e : itens_) {
        if (todos) marcados_.erase(chaveDe(e));
        else if (!jaNoProjeto(e)) marcados_[chaveDe(e)] = e;
    }
    lista_.repaint();
    atualizarRodape();
}

void PainelAndroidUsb::importar() {
    if (estado_ != Estado::Pronto || marcados_.empty()) return;
    std::vector<EntradaAndroid> marcados;
    for (const auto& [k, e] : marcados_) marcados.push_back(e);
    estado_ = Estado::Preparando;
    progresso_ = -1.0;  // barra indeterminada enquanto expande as pastas
    atualizarRodape();

    juce::Component::SafePointer<PainelAndroidUsb> seguro(this);
    sessao_->expandir(std::move(marcados), [seguro](bool ok, std::vector<ItemCopiaAndroid> itens, juce::String erro) {
        if (seguro == nullptr) return;
        auto& s = *seguro;
        if (!ok) {
            s.estado_ = erro == t("cel.usb.cancelado") ? Estado::Pronto : Estado::Erro;
            s.erro_ = erro;
            s.atualizarRodape();
            return;
        }
        // Só o que é novo: o que o projeto já tem (nome + tamanho) não é copiado de novo.
        std::vector<ItemCopiaAndroid> novos;
        s.ignoradosNoProjeto_ = 0;
        for (auto& it : itens) {
            if (s.jaNoProjeto(it.entrada)) ++s.ignoradosNoProjeto_;
            else novos.push_back(std::move(it));
        }
        if (novos.empty()) {
            s.estado_ = Estado::Pronto;
            s.erro_ = t("cel.usb.nadaNovo");
            s.atualizarRodape();
            return;
        }
        s.estado_ = Estado::Copiando;
        s.progresso_ = 0.0;
        const juce::File destino = pastaRaizRecebidos().getChildFile(nomePastaSessao(juce::Time::getCurrentTime()) + " USB");
        s.sessao_->copiar(std::move(novos), destino,
            [seguro](ProgressoCopiaAndroid p) {
                if (seguro == nullptr || seguro->estado_ != Estado::Copiando) return;
                seguro->progresso_ = p.bytesTotal > 0 ? juce::jlimit(0.0, 1.0, static_cast<double>(p.bytesFeitos) / static_cast<double>(p.bytesTotal)) : 0.0;
                const juce::String sep = juce::String::fromUTF8("  \xc2\xb7  ");
                seguro->lblRodape_.setText(t("light.cel.xdey").replace("{x}", juce::String(p.feitos)).replace("{y}", juce::String(p.total)) + sep +
                                               bytesLegiveis(p.bytesFeitos) + " / " + bytesLegiveis(p.bytesTotal) + sep + p.arquivoAtual,
                                           juce::dontSendNotification);
            },
            [seguro](ResultadoCopiaAndroid r) {
                if (seguro == nullptr) return;
                auto& s2 = *seguro;
                const int recebidos = r.copiados + r.jaExistiam;
                if (r.erro.isNotEmpty() || (recebidos == 0 && !r.cancelado)) {
                    s2.estado_ = Estado::Erro;
                    s2.erro_ = r.erro.isNotEmpty() ? r.erro : t("cel.usb.err.copiar");
                    s2.atualizarRodape();
                    return;
                }
                s2.resposta_->pastaSessao = r.pasta;
                if (r.cancelado && recebidos == 0) {
                    s2.estado_ = Estado::Pronto;
                    s2.erro_.clear();
                    s2.atualizarRodape();
                    return;
                }
                // Cancelado no meio: o que já chegou entra no Intake mesmo assim (nada fica pela metade).
                s2.terminar(true, recebidos);
            });
        s.atualizarRodape();
    });
}

void PainelAndroidUsb::terminar(bool comIngestao, int recebidos) {
    resposta_->concluiu = comIngestao;
    resposta_->recebidos = recebidos;
    if (auto* overlay = findParentComponentOfClass<PainelOverlay>()) overlay->concluirExterno(comIngestao ? 1 : 0);
}

void PainelAndroidUsb::atualizarRodape() {
    const auto& tk = tema();
    const bool pronto = estado_ == Estado::Pronto;
    const bool ocupado = estado_ == Estado::Copiando || estado_ == Estado::Preparando;

    lblAparelho_.setText(aparelho_.nome.isNotEmpty() ? aparelho_.nome : t("cel.usb.titulo"), juce::dontSendNotification);
    juce::String trilha = comboMemoria_.getNumItems() > 0 ? comboMemoria_.getText().upToFirstOccurrenceOf("  (", false, false) : juce::String();
    for (const auto& n : caminho_) trilha += " / " + n.nome;
    lblCaminho_.setText(trilha, juce::dontSendNotification);

    juce::String status;
    if (estado_ == Estado::Conectando) status = t("cel.usb.conectando");
    else if (estado_ == Estado::Erro) status = erro_;
    else if (pronto && listando_) status = t("cel.usb.lendo");
    else if (pronto && itens_.empty()) status = t("cel.usb.vazia");
    lblStatus_.setText(status, juce::dontSendNotification);
    lblStatus_.setColour(juce::Label::textColourId, estado_ == Estado::Erro ? tk.perigo : tk.textoSecundario);
    lblStatus_.setVisible(status.isNotEmpty());

    if (!ocupado) {
        std::uint64_t bytes = 0;
        int pastas = 0;
        for (const auto& [k, e] : marcados_) { bytes += e.tamanho; if (e.pasta) ++pastas; }
        const juce::String sep = juce::String::fromUTF8("  \xc2\xb7  ");
        juce::String r = t("cel.usb.marcados").replace("{n}", juce::String(static_cast<int>(marcados_.size())));
        if (bytes > 0) r += sep + bytesLegiveis(bytes);
        if (pastas > 0) r += sep + t("cel.usb.comPastas");
        if (pronto && erro_.isNotEmpty()) r += "\n" + erro_;  // ex.: "nada novo" — aviso, não erro
        lblRodape_.setText(r, juce::dontSendNotification);
    } else if (estado_ == Estado::Preparando) {
        lblRodape_.setText(t("cel.usb.preparando"), juce::dontSendNotification);
    }

    barra_.setVisible(ocupado);
    btnSubir_->setEnabled(pronto && !caminho_.empty());
    btnTentar_->setVisible(estado_ == Estado::Erro);
    btnTodos_->setEnabled(pronto && !itens_.empty());
    btnImportar_->setEnabled(pronto && !marcados_.empty());
    btnFechar_->setButtonText(ocupado ? t("cel.usb.parar") : t("light.ui.common.cancel"));
    if (btnVoltar_) btnVoltar_->setEnabled(!ocupado);
    comboMemoria_.setEnabled(pronto);
    lista_.setEnabled(pronto);
    resized();
    repaint();
}

void PainelAndroidUsb::paintListBoxItem(int linha, juce::Graphics& g, int largura, int altura, bool selecionada) {
    if (linha < 0 || linha >= static_cast<int>(itens_.size())) return;
    const auto& tk = tema();
    const auto& e = itens_[static_cast<size_t>(linha)];
    const bool marcado = marcados_.count(chaveDe(e)) > 0;
    const bool noProjeto = jaNoProjeto(e);

    if (selecionada) {
        g.setColour(tk.acento.withAlpha(0.14f));
        g.fillRoundedRectangle(juce::Rectangle<float>(0, 1, (float) largura, (float) altura - 2).reduced(2, 0), 6.0f);
    }
    // checkbox
    auto caixa = juce::Rectangle<float>(10.0f, (altura - 16) * 0.5f, 16.0f, 16.0f);
    g.setColour(marcado ? tk.acento : tk.campoFundo);
    g.fillRoundedRectangle(caixa, 4.0f);
    g.setColour(marcado ? tk.acento : tk.borda);
    g.drawRoundedRectangle(caixa, 4.0f, 1.2f);
    if (marcado) {
        juce::Path v;
        v.startNewSubPath(caixa.getX() + 3.5f, caixa.getCentreY());
        v.lineTo(caixa.getX() + 6.8f, caixa.getBottom() - 4.0f);
        v.lineTo(caixa.getRight() - 3.2f, caixa.getY() + 4.0f);
        g.setColour(tk.textoSobreAcento);
        g.strokePath(v, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto r = juce::Rectangle<int>(kColunaMarca + 4, 0, largura - kColunaMarca - 4, altura);
    desenharIcone(g, e.pasta ? IconeUi::Pasta : IconeUi::Arquivo, r.removeFromLeft(20).toFloat().withSizeKeepingCentre(18, 18),
                  e.pasta ? tk.acento : tk.textoSecundario);
    r.removeFromLeft(8);

    auto data = r.removeFromRight(140);
    auto tamanho = r.removeFromRight(96);
    auto selo = noProjeto ? r.removeFromRight(110) : juce::Rectangle<int>();

    g.setFont(fonteUi(tk.tamanhoFonteCorpo));
    g.setColour(noProjeto ? tk.textoTerciario : tk.textoPrimario);
    g.drawText(e.nome, r.reduced(0, 2), juce::Justification::centredLeft, true);
    g.setFont(fonteUi(tk.tamanhoFontePequena));
    g.setColour(tk.textoSecundario);
    g.drawText(e.pasta ? juce::String::fromUTF8("\xe2\x80\x94") : bytesLegiveis(e.tamanho), tamanho, juce::Justification::centredRight, true);
    if (e.modificado.toMilliseconds() > 0)
        g.drawText(e.modificado.formatted("%Y-%m-%d %H:%M"), data, juce::Justification::centredRight, true);
    if (noProjeto) {
        auto s = selo.toFloat().withSizeKeepingCentre(100.0f, 18.0f);
        g.setColour(tk.estadoQcOk.withAlpha(0.18f));
        g.fillRoundedRectangle(s, 9.0f);
        g.setColour(tk.estadoQcOk);
        g.setFont(fonteUi(tk.tamanhoFonteMicro, juce::Font::bold));
        g.drawText(t("cel.usb.noProjeto"), s, juce::Justification::centred, true);
    }
    g.setColour(tk.linhaSuave);
    g.drawHorizontalLine(altura - 1, (float) kColunaMarca, (float) largura - 4);
}

void PainelAndroidUsb::listBoxItemClicked(int linha, const juce::MouseEvent& e) {
    if (e.x < kColunaMarca) alternarMarca(linha);
}

void PainelAndroidUsb::abrirLinha(int linha) {
    if (linha < 0 || linha >= static_cast<int>(itens_.size()) || estado_ != Estado::Pronto) return;
    const auto e = itens_[static_cast<size_t>(linha)];
    if (e.pasta) abrirPasta(e.id, e.nome);
    else alternarMarca(linha);
}

bool PainelAndroidUsb::tratarTecla(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        if (estado_ == Estado::Copiando || estado_ == Estado::Preparando) sessao_->cancelar();
        else terminar(false, 0);
        return true;
    }
    if (k == juce::KeyPress::spaceKey) {  // marca/desmarca as linhas selecionadas
        const auto sel = lista_.getSelectedRows();
        for (int i = 0; i < sel.size(); ++i) alternarMarca(sel[i]);
        return sel.size() > 0;
    }
    if (k == juce::KeyPress::backspaceKey || k.isKeyCode(juce::KeyPress::leftKey) && k.getModifiers().isCommandDown()) {
        subir();
        return true;
    }
    return false;
}

void PainelAndroidUsb::paint(juce::Graphics& g) {
    const auto& tk = tema();
    desenharCartao(g, rcCartaoLista_.toFloat(), tk.raioCartao * 0.6f, tk.cartao, true);
    // ícone do celular ao lado do nome do aparelho
    auto tile = juce::Rectangle<float>(40.0f, 40.0f).withPosition(0.0f, 0.0f);
    g.setColour(tk.tipoImagem.withAlpha(0.20f));
    g.fillRoundedRectangle(tile, 12.0f);
    desenharIcone(g, IconeUi::Celular, tile.reduced(9.0f), tk.tipoImagem);
}

void PainelAndroidUsb::resized() {
    const auto& tk = tema();
    auto b = getLocalBounds();

    auto topo = b.removeFromTop(40);
    topo.removeFromLeft(52);
    if (comboMemoria_.isVisible()) comboMemoria_.setBounds(topo.removeFromRight(juce::jmin(320, topo.getWidth() / 2)).reduced(0, 4));
    lblAparelho_.setBounds(topo);
    b.removeFromTop(10);

    auto trilha = b.removeFromTop(34);
    btnSubir_->setBounds(trilha.removeFromLeft(44));
    trilha.removeFromLeft(10);
    lblCaminho_.setBounds(trilha);
    b.removeFromTop(8);

    auto nav = b.removeFromBottom(tk.alturaBotaoPrimario);
    b.removeFromBottom(10);
    btnImportar_->setBounds(nav.removeFromRight(juce::jmax(200, btnImportar_->larguraIdeal() + 24)));
    nav.removeFromRight(10);
    btnFechar_->setBounds(nav.removeFromRight(juce::jmax(140, btnFechar_->larguraIdeal() + 24)));
    if (btnVoltar_) {
        btnVoltar_->setBounds(nav.removeFromLeft(juce::jmax(120, btnVoltar_->larguraIdeal() + 24)));
        nav.removeFromLeft(10);
    }
    btnTodos_->setBounds(nav.removeFromLeft(juce::jmax(170, btnTodos_->larguraIdeal() + 24)));

    auto rodape = b.removeFromBottom(barra_.isVisible() ? 64 : 44);
    b.removeFromBottom(8);
    if (barra_.isVisible()) {
        barra_.setBounds(rodape.removeFromTop(18).reduced(0, 1));
        rodape.removeFromTop(4);
    }
    lblRodape_.setBounds(rodape);

    rcCartaoLista_ = b;
    lista_.setBounds(b.reduced(6, 6));
    rcMensagem_ = b.withSizeKeepingCentre(juce::jmin(560, b.getWidth() - 40), 110);
    lblStatus_.setBounds(rcMensagem_.removeFromTop(64));
    btnTentar_->setBounds(rcMensagem_.removeFromTop(tk.alturaBotaoPrimario).withSizeKeepingCentre(220, tk.alturaBotaoPrimario));
    lblStatus_.toFront(false);
    btnTentar_->toFront(false);
}

void mostrarAndroidUsb(PainelOverlay& overlay, std::function<ConjuntoExistentes()> existentes,
                       std::function<void(RespostaReceberCelular, bool)> aoFechar, bool comVoltar) {
    auto resposta = std::make_shared<RespostaReceberCelular>();
    auto painel = std::make_shared<PainelAndroidUsb>(std::move(existentes), resposta, comVoltar);

    PainelOverlay::Config cfg;
    cfg.titulo = t("cel.usb.titulo");
    cfg.trilha = t("moldura.trilha.adicionar");
    cfg.conteudo = painel;
    cfg.larguraCartao = 900;
    cfg.alturaConteudo = 560;
    juce::Component::SafePointer<PainelAndroidUsb> seguro(painel.get());
    cfg.aoTecla = [seguro](const juce::KeyPress& k) { return seguro != nullptr && seguro->tratarTecla(k); };
    overlay.mostrar(cfg, [resposta, aoFechar](PainelOverlay::Resultado r) {
        if (aoFechar) aoFechar(*resposta, r.botaoId == 9);
    });
}

} // namespace matriz::ui
