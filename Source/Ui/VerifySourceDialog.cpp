#include "VerifySourceDialog.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

constexpr int kColCheck = 34;
constexpr int kColStatus = 130;
constexpr int kColTamanho = 90;

juce::String tr(const char* chave) { return matriz::i18n::t(chave); }

void colunas(int largura, int& xNome, int& larguraNome, int& xPasta, int& larguraPasta, int& xTamanho) {
    const int resto = juce::jmax(100, largura - kColCheck - kColStatus - kColTamanho);
    xNome = kColCheck + kColStatus;
    larguraNome = resto * 4 / 10;
    xPasta = xNome + larguraNome;
    larguraPasta = resto - larguraNome;
    xTamanho = xPasta + larguraPasta;
}

void estilizar(juce::TextButton& b, bool primario) {
    const auto& tk = tema();
    b.setColour(juce::TextButton::buttonColourId, primario ? tk.acento : tk.painelAlt);
    b.setColour(juce::TextButton::buttonOnColourId, tk.acento);
    b.setColour(juce::TextButton::textColourOffId, primario ? tk.textoSobreAcento : tk.textoPrimario);
    b.setColour(juce::TextButton::textColourOnId, tk.textoSobreAcento);
}

} // namespace

class VerifySourceDialog::Cabecalho : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painelAlt);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        int xN, wN, xP, wP, xT;
        colunas(getWidth(), xN, wN, xP, wP, xT);
        g.drawText(tr("verify.col_status"), kColCheck, 0, kColStatus, getHeight(), juce::Justification::centredLeft);
        g.drawText(tr("verify.col_arquivo"), xN, 0, wN, getHeight(), juce::Justification::centredLeft);
        g.drawText(tr("verify.col_pasta"), xP, 0, wP, getHeight(), juce::Justification::centredLeft);
        g.drawText(tr("verify.col_tamanho"), xT, 0, kColTamanho - 12, getHeight(), juce::Justification::centredRight);
    }
};

VerifySourceDialog::VerifySourceDialog(Ganchos ganchos)
    : ganchos_(std::move(ganchos)), barra_(progresso_) {
    const auto& tk = tema();

    lblInfo_.setJustificationType(juce::Justification::topLeft);
    lblInfo_.setMinimumHorizontalScale(1.0f);
    lblInfo_.setFont(juce::Font(juce::FontOptions(14.0f)));
    lblInfo_.setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(lblInfo_);
    addChildComponent(barra_);

    cabecalho_ = std::make_unique<Cabecalho>();
    lista_.setModel(this);
    lista_.setRowHeight(26);
    lista_.setHeaderComponent(std::move(cabecalho_));
    lista_.getHeaderComponent()->setSize(100, 24);
    lista_.setColour(juce::ListBox::backgroundColourId, tk.painel);
    lista_.setColour(juce::ListBox::outlineColourId, tk.borda);
    lista_.setOutlineThickness(1);
    addChildComponent(lista_);

    filtroTodos_.setButtonText(tr("verify.filtro_todos"));
    filtroFaltando_.setButtonText(tr("verify.filtro_faltando"));
    filtroRejeitado_.setButtonText(tr("verify.filtro_rejeitado"));
    int idx = 0;
    for (auto* b : {&filtroTodos_, &filtroFaltando_, &filtroRejeitado_}) {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(4711);
        const int alvo = idx++;
        b->onClick = [this, alvo] {
            filtro_ = alvo;
            reconstruirVisiveis();
        };
        addChildComponent(*b);
    }
    filtroTodos_.setToggleState(true, juce::dontSendNotification);

    btnMarcarFalt_.setButtonText(tr("verify.marcar_faltando"));
    btnMarcarFalt_.onClick = [this] { marcar(true, true); };
    btnMarcarTodos_.setButtonText(tr("verify.marcar_todos"));
    btnMarcarTodos_.onClick = [this] { marcar(false, true); };
    btnDesmarcar_.setButtonText(tr("verify.desmarcar"));
    btnDesmarcar_.onClick = [this] { marcar(false, false); };
    for (auto* b : {&btnMarcarFalt_, &btnMarcarTodos_, &btnDesmarcar_}) addChildComponent(*b);

    btnA_.onClick = [this] {
        switch (estado_) {
            case Estado::Pedir: {
                if (ganchos_.volumes.empty() || indiceDisco_ >= ganchos_.volumes.size()) return;
                const auto& nome = ganchos_.volumes[indiceDisco_];
                const juce::File raiz("/Volumes/" + nome);
                if (!raiz.isDirectory()) {
                    avisoPedir_ = tr("verify.pasta_invalida");
                    atualizarTextos();
                    return;
                }
                iniciarScan(raiz, nome);
                break;
            }
            case Estado::Resultado: contarMarcados() > 0 ? ingerirMarcados() : proximoDisco(); break;
            case Estado::Ingerido: proximoDisco(); break;
            default: break;
        }
    };
    btnB_.onClick = [this] { escolherPasta(); };
    btnC_.onClick = [this] {
        if (estado_ == Estado::Pedir) {
            ++indiceDisco_;
            if (indiceDisco_ >= ganchos_.volumes.size()) finalizar();
            else pedirProximoDisco();
        } else if (estado_ == Estado::Resultado) {
            proximoDisco();
        }
    };
    btnFechar_.onClick = [this] {
        if (estado_ == Estado::Escaneando) cancelarScan();
        else fechar();
    };
    for (auto* b : {&btnA_, &btnB_, &btnC_, &btnFechar_}) addChildComponent(*b);

    pedirProximoDisco();
    setSize(900, 580);
}

VerifySourceDialog::~VerifySourceDialog() {
    stopTimer();
    if (scan_) scan_->cancelar = true;
    lista_.setModel(nullptr);
}

void VerifySourceDialog::pedirProximoDisco() {
    avisoPedir_.clear();
    aplicarEstado(Estado::Pedir);
}

void VerifySourceDialog::aplicarEstado(Estado e) {
    estado_ = e;
    atualizarTextos();
    resized();
    repaint();
}

void VerifySourceDialog::atualizarTextos() {
    const bool temVolumes = !ganchos_.volumes.empty() && indiceDisco_ < ganchos_.volumes.size();
    const bool mostraGrid = estado_ == Estado::Resultado || estado_ == Estado::Ingerindo || estado_ == Estado::Ingerido;
    lista_.setVisible(mostraGrid);
    barra_.setVisible(estado_ == Estado::Escaneando || estado_ == Estado::Ingerindo);
    const bool editavel = estado_ == Estado::Resultado;
    for (auto* b : {&filtroTodos_, &filtroFaltando_, &filtroRejeitado_, &btnMarcarFalt_, &btnMarcarTodos_, &btnDesmarcar_})
        b->setVisible(mostraGrid && !linhas_.empty());
    for (auto* b : {&btnMarcarFalt_, &btnMarcarTodos_, &btnDesmarcar_}) b->setEnabled(editavel);

    for (auto* b : {&btnA_, &btnB_, &btnC_}) b->setVisible(false);
    btnFechar_.setVisible(true);
    btnFechar_.setEnabled(true);
    btnFechar_.setButtonText(tr("verify.btn_fechar"));
    estilizar(btnA_, true);
    estilizar(btnB_, false);
    estilizar(btnC_, false);
    estilizar(btnFechar_, false);

    switch (estado_) {
        case Estado::Pedir:
            if (temVolumes) {
                lblInfo_.setText(tr("verify.pedir")
                                     .replace("{n}", juce::String(static_cast<int>(indiceDisco_) + 1))
                                     .replace("{total}", juce::String(static_cast<int>(ganchos_.volumes.size())))
                                     .replace("{nome}", ganchos_.volumes[indiceDisco_]) +
                                     (avisoPedir_.isNotEmpty() ? "\n\n" + avisoPedir_ : juce::String()),
                                 juce::dontSendNotification);
                btnA_.setButtonText(tr("verify.btn_scan"));
                btnA_.setVisible(true);
                btnC_.setButtonText(tr("verify.btn_pular"));
                btnC_.setVisible(true);
            } else {
                lblInfo_.setText(tr("verify.pedir_pasta"), juce::dontSendNotification);
                estilizar(btnB_, true);
            }
            btnB_.setButtonText(tr("verify.btn_pasta"));
            btnB_.setVisible(true);
            break;
        case Estado::Escaneando:
            lblInfo_.setText(tr("verify.escaneando").replace("{nome}", nomeAtual_), juce::dontSendNotification);
            btnFechar_.setButtonText(tr("verify.btn_cancelar"));
            break;
        case Estado::Resultado: {
            const int marcados = contarMarcados();
            if (linhas_.empty()) {
                lblInfo_.setText(tr("verify.nada").replace("{nome}", nomeAtual_), juce::dontSendNotification);
            } else {
                int faltando = 0, rej = 0;
                for (const auto& l : linhas_) (l.f.rejeitadoAntes ? rej : faltando)++;
                lblInfo_.setText(tr("verify.resultado")
                                     .replace("{nome}", nomeAtual_)
                                     .replace("{lidos}", juce::String(lidosDoDisco_))
                                     .replace("{main}", juce::String(noMainDoDisco_))
                                     .replace("{faltando}", juce::String(faltando))
                                     .replace("{rej}", juce::String(rej))
                                     .replace("{ilegiveis}",
                                              ilegiveisDoDisco_ > 0 ? tr("verify.ilegiveis").replace("{n}", juce::String(ilegiveisDoDisco_))
                                                                    : juce::String()),
                                 juce::dontSendNotification);
            }
            if (marcados > 0) {
                btnA_.setButtonText(tr("verify.btn_ingest").replace("{n}", juce::String(marcados)));
                btnC_.setButtonText(tr("verify.btn_proximo"));
                btnC_.setVisible(true);
            } else {
                btnA_.setButtonText(tr("verify.btn_proximo"));
            }
            btnA_.setVisible(true);
            break;
        }
        case Estado::Ingerindo:
            lblInfo_.setText(tr("verify.ingerindo").replace("{n}", juce::String(ingeridosDoDisco_)).replace("{nome}", nomeAtual_),
                             juce::dontSendNotification);
            break;
        case Estado::Ingerido:
            lblInfo_.setText(tr("verify.ingerido").replace("{nome}", nomeAtual_), juce::dontSendNotification);
            btnA_.setButtonText(tr("verify.btn_proximo"));
            btnA_.setVisible(true);
            break;
        case Estado::Fim: {
            const auto base = arquivoRelatorio_ == juce::File() ? tr("verify.fim_sem_relatorio") : tr("verify.fim");
            lblInfo_.setText(base.replace("{discos}", juce::String(discosVerificados_))
                                 .replace("{faltando}", juce::String(faltandoTotal_))
                                 .replace("{ingeridos}", juce::String(ingeridosTotal_))
                                 .replace("{arquivo}", arquivoRelatorio_.getFullPathName()),
                             juce::dontSendNotification);
            break;
        }
    }
}

void VerifySourceDialog::escolherPasta() {
    chooser_ = std::make_unique<juce::FileChooser>(tr("verify.pedir_pasta"), juce::File("/Volumes"));
    juce::Component::SafePointer<VerifySourceDialog> self(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [self](const juce::FileChooser& fc) {
                              if (!self) return;
                              const auto pasta = fc.getResult();
                              if (pasta == juce::File()) return;
                              if (!pasta.isDirectory()) {
                                  self->avisoPedir_ = tr("verify.pasta_invalida");
                                  self->atualizarTextos();
                                  return;
                              }
                              self->iniciarScan(pasta, pasta.getFileName());
                          });
}

void VerifySourceDialog::iniciarScan(const juce::File& raiz, const juce::String& nome) {
    if (!ganchos_.carregarHashes) return;
    nomeAtual_ = nome;
    linhas_.clear();
    visiveis_.clear();
    auto s = std::make_shared<EstadoScan>();
    s->hashes = ganchos_.carregarHashes();  // message thread: o worker nunca toca no banco
    s->raiz = raiz;
    scan_ = s;
    progresso_ = -1.0;  // indeterminada até saber o total
    aplicarEstado(Estado::Escaneando);
    startTimer(100);
    juce::Thread::launch([s] {
        auto r = matriz::vault::VerifySource::verificar(
            s->raiz, s->hashes, s->cancelar, [s](int feitos, int total, const juce::String& arq) {
                s->feitos = feitos;
                s->total = total;
                const juce::ScopedLock sl(s->lock);
                s->arquivoAtual = arq;
            });
        {
            const juce::ScopedLock sl(s->lock);
            s->resultado = std::move(r);
        }
        s->pronto = true;
    });
}

void VerifySourceDialog::cancelarScan() {
    if (scan_) scan_->cancelar = true;
    scan_.reset();
    stopTimer();
    pedirProximoDisco();
}

void VerifySourceDialog::timerCallback() {
    if (estado_ == Estado::Escaneando && scan_) {
        const int total = scan_->total.load();
        progresso_ = total > 0 ? static_cast<double>(scan_->feitos.load()) / total : -1.0;
        if (total > 0) {
            juce::String arq;
            {
                const juce::ScopedLock sl(scan_->lock);
                arq = scan_->arquivoAtual;
            }
            lblInfo_.setText(tr("verify.escaneando").replace("{nome}", nomeAtual_) + "\n" +
                                 tr("verify.progresso")
                                     .replace("{feitos}", juce::String(scan_->feitos.load()))
                                     .replace("{total}", juce::String(total))
                                     .replace("{arquivo}", arq),
                             juce::dontSendNotification);
        }
        if (scan_->pronto.load()) {
            stopTimer();
            concluirScan();
        }
    } else if (estado_ == Estado::Ingerindo) {
        const bool ocupado = ganchos_.ingestOcupado && ganchos_.ingestOcupado();
        ociosos_ = ocupado ? 0 : ociosos_ + 1;
        if (ociosos_ >= 2) {  // duas leituras livres seguidas: o lote e a resolução de duplicatas acabaram de verdade
            stopTimer();
            aplicarEstado(Estado::Ingerido);
        }
    }
}

void VerifySourceDialog::concluirScan() {
    matriz::vault::ResultadoVerificacaoFonte r;
    {
        const juce::ScopedLock sl(scan_->lock);
        r = std::move(scan_->resultado);
    }
    if (r.cancelado) {
        pedirProximoDisco();
        return;
    }
    lidosDoDisco_ = r.totalLidos;
    noMainDoDisco_ = r.totalNoMain;
    ilegiveisDoDisco_ = r.ilegiveis;
    ingeridosDoDisco_ = 0;
    linhas_.clear();
    for (auto& f : r.faltantes) {
        Linha l;
        l.marcado = !f.rejeitadoAntes;  // faltando vem marcado; rejeitado antes o usuário decide
        l.f = std::move(f);
        if (!l.f.rejeitadoAntes) ++faltandoTotal_;
        linhas_.push_back(std::move(l));
    }
    ++discosVerificados_;
    discoPendente_ = true;
    filtro_ = 0;
    filtroTodos_.setToggleState(true, juce::dontSendNotification);
    reconstruirVisiveis();
    aplicarEstado(Estado::Resultado);
}

void VerifySourceDialog::reconstruirVisiveis() {
    visiveis_.clear();
    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
        const bool rej = linhas_[static_cast<size_t>(i)].f.rejeitadoAntes;
        if (filtro_ == 0 || (filtro_ == 1 && !rej) || (filtro_ == 2 && rej)) visiveis_.push_back(i);
    }
    lista_.updateContent();
    lista_.repaint();
}

int VerifySourceDialog::contarMarcados() const {
    int n = 0;
    for (const auto& l : linhas_) n += (l.marcado && !l.ingerido) ? 1 : 0;
    return n;
}

void VerifySourceDialog::marcar(bool soFaltando, bool valor) {
    for (auto& l : linhas_) {
        if (soFaltando) l.marcado = !l.f.rejeitadoAntes;
        else l.marcado = valor;
    }
    atualizarTextos();
    lista_.repaint();
}

int VerifySourceDialog::getNumRows() { return static_cast<int>(visiveis_.size()); }

void VerifySourceDialog::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) {
    if (row < 0 || row >= static_cast<int>(visiveis_.size())) return;
    const auto& tk = tema();
    const auto& l = linhas_[static_cast<size_t>(visiveis_[static_cast<size_t>(row)])];
    if (row % 2 == 1) g.fillAll(tk.painelAlt.withAlpha(0.35f));
    g.setColour(tk.borda.withAlpha(0.4f));
    g.drawHorizontalLine(h - 1, 0.0f, static_cast<float>(w));

    const auto caixa = juce::Rectangle<int>(10, h / 2 - 7, 14, 14);
    g.setColour(tk.textoSecundario);
    g.drawRect(caixa, 1);
    if (l.marcado) {
        g.setColour(l.ingerido ? tk.textoSecundario : tk.acento);
        g.fillRect(caixa.reduced(3));
    }

    int xN, wN, xP, wP, xT;
    colunas(w, xN, wN, xP, wP, xT);
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.setColour(l.f.rejeitadoAntes ? tk.textoSecundario : tk.acento);
    g.drawText(l.f.rejeitadoAntes ? tr("verify.status_rejeitado") : tr("verify.status_faltando"), kColCheck, 0, kColStatus, h,
               juce::Justification::centredLeft, true);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.setColour(tk.textoPrimario);
    g.drawText(l.f.arquivo.getFileName(), xN, 0, wN - 8, h, juce::Justification::centredLeft, true);
    g.setColour(tk.textoSecundario);
    const auto raiz = scan_ ? scan_->raiz : juce::File();
    g.drawText(l.f.arquivo.getParentDirectory().getRelativePathFrom(raiz), xP, 0, wP - 8, h, juce::Justification::centredLeft, true);
    g.drawText(juce::File::descriptionOfSizeInBytes(l.f.tamanhoBytes), xT, 0, kColTamanho - 12, h, juce::Justification::centredRight, true);
}

void VerifySourceDialog::listBoxItemClicked(int row, const juce::MouseEvent&) {
    if (estado_ != Estado::Resultado) return;
    if (row < 0 || row >= static_cast<int>(visiveis_.size())) return;
    auto& l = linhas_[static_cast<size_t>(visiveis_[static_cast<size_t>(row)])];
    l.marcado = !l.marcado;
    atualizarTextos();
    lista_.repaint();
}

void VerifySourceDialog::ingerirMarcados() {
    juce::Array<juce::File> arquivos;
    for (auto& l : linhas_)
        if (l.marcado && !l.ingerido && l.f.arquivo.existsAsFile()) arquivos.add(l.f.arquivo);
    if (arquivos.isEmpty()) {
        lblInfo_.setText(tr("verify.pasta_invalida"), juce::dontSendNotification);
        return;
    }
    for (auto& l : linhas_)
        if (l.marcado && !l.ingerido && l.f.arquivo.existsAsFile()) l.ingerido = true;
    ingeridosDoDisco_ = arquivos.size();
    ingeridosTotal_ += arquivos.size();
    ociosos_ = 0;
    progresso_ = -1.0;
    if (ganchos_.ingerir) ganchos_.ingerir(arquivos);
    aplicarEstado(Estado::Ingerindo);
    startTimer(500);
}

void VerifySourceDialog::anexarRelatorioDoDisco() {
    if (!discoPendente_) return;
    discoPendente_ = false;
    relatorio_ << "== " << nomeAtual_ << " (" << (scan_ ? scan_->raiz.getFullPathName() : juce::String()) << ")\n"
               << "read: " << lidosDoDisco_ << ", already in the MAIN: " << noMainDoDisco_ << ", unreadable: " << ilegiveisDoDisco_
               << ", listed: " << static_cast<int>(linhas_.size()) << "\n";
    for (const auto& l : linhas_)
        relatorio_ << (l.f.rejeitadoAntes ? "REJECTED BEFORE" : "MISSING        ") << (l.ingerido ? "  [sent to Intake]  " : "  [not sent]  ")
                   << l.f.arquivo.getFullPathName() << "  " << juce::File::descriptionOfSizeInBytes(l.f.tamanhoBytes) << "  "
                   << juce::String(l.f.sha256) << "\n";
    relatorio_ << "\n";
}

juce::File VerifySourceDialog::salvarRelatorio() {
    if (relatorioSalvo_ || discosVerificados_ == 0 || !ganchos_.pastaRelatorios) return arquivoRelatorio_;
    const auto pasta = ganchos_.pastaRelatorios();
    if (pasta == juce::File()) return {};
    relatorioSalvo_ = true;
    pasta.createDirectory();
    const auto agora = juce::Time::getCurrentTime();
    const auto f = pasta.getChildFile("verify-source-" + agora.formatted("%Y%m%d-%H%M%S") + ".txt");
    const juce::String txt = "Verify Source Disks - " + agora.formatted("%Y-%m-%d %H:%M:%S") + "\n" +
                             "disks checked: " + juce::String(discosVerificados_) + ", missing: " + juce::String(faltandoTotal_) +
                             ", sent to Intake: " + juce::String(ingeridosTotal_) + "\n\n" + relatorio_;
    if (f.replaceWithText(txt)) arquivoRelatorio_ = f;
    return arquivoRelatorio_;
}

void VerifySourceDialog::proximoDisco() {
    anexarRelatorioDoDisco();
    linhas_.clear();
    visiveis_.clear();
    ++indiceDisco_;
    if (indiceDisco_ >= std::max<size_t>(1, ganchos_.volumes.size())) finalizar();
    else pedirProximoDisco();
}

void VerifySourceDialog::finalizar() {
    anexarRelatorioDoDisco();
    salvarRelatorio();
    aplicarEstado(Estado::Fim);
}

void VerifySourceDialog::fechar() {
    stopTimer();
    if (scan_) scan_->cancelar = true;
    anexarRelatorioDoDisco();
    salvarRelatorio();
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
}

void VerifySourceDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);
    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);
}

void VerifySourceDialog::resized() {
    auto area = getLocalBounds().reduced(20, 16);
    auto botoes = area.removeFromBottom(34);
    area.removeFromBottom(10);
    btnFechar_.setBounds(botoes.removeFromRight(110));
    btnA_.setBounds(botoes.removeFromLeft(170));
    botoes.removeFromLeft(10);
    btnB_.setBounds(botoes.removeFromLeft(170));
    btnC_.setBounds(botoes.removeFromLeft(170));

    const bool mostraGrid = estado_ == Estado::Resultado || estado_ == Estado::Ingerindo || estado_ == Estado::Ingerido;
    lblInfo_.setBounds(area.removeFromTop(mostraGrid ? 44 : juce::jmax(60, area.getHeight() - 40)));
    if (mostraGrid) {
        area.removeFromTop(6);
        auto barraFiltros = area.removeFromTop(28);
        filtroTodos_.setBounds(barraFiltros.removeFromLeft(80));
        barraFiltros.removeFromLeft(4);
        filtroFaltando_.setBounds(barraFiltros.removeFromLeft(90));
        barraFiltros.removeFromLeft(4);
        filtroRejeitado_.setBounds(barraFiltros.removeFromLeft(130));
        btnDesmarcar_.setBounds(barraFiltros.removeFromRight(130));
        barraFiltros.removeFromRight(6);
        btnMarcarTodos_.setBounds(barraFiltros.removeFromRight(120));
        barraFiltros.removeFromRight(6);
        btnMarcarFalt_.setBounds(barraFiltros.removeFromRight(130));
        area.removeFromTop(6);
        if (estado_ == Estado::Ingerindo) barra_.setBounds(area.removeFromBottom(22));
        lista_.setBounds(area);
    } else {
        barra_.setBounds(area.removeFromTop(22));
    }
}

void VerifySourceDialog::lookAndFeelChanged() {
    lblInfo_.setColour(juce::Label::textColourId, tema().textoPrimario);
    atualizarTextos();
    repaint();
}

void VerifySourceDialog::exibirModal(Ganchos ganchos) {
    auto* dlg = new VerifySourceDialog(std::move(ganchos));
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dlg);
    options.dialogTitle = tr("verify.titulo");
    options.dialogBackgroundColour = tema().painel;
    options.escapeKeyTriggersCloseButton = false;  // Esc no meio do scan/ingest fechava sem relatório; use Close/Cancel
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.launchAsync();
}

} // namespace matriz::ui
