#include "SyncDestinationDialog.h"
#include "ModalMitigacao.h"
#include "../I18n/Strings.h"
#include "../Vault/Volume.h"

namespace matriz::ui {

class SyncDestinationDialog::ListaRevisaoSync : public juce::Component {
public:
    void setItens(const std::vector<matriz::sync::ItemSync>& todos, int filtroClasse) {
        itensExibidos_.clear();
        for (const auto& it : todos) {
            if (filtroClasse == 0) { // Todos
                itensExibidos_.push_back(it);
            } else if (filtroClasse == 1 && it.classe == matriz::sync::ClasseSync::Novo) {
                itensExibidos_.push_back(it);
            } else if (filtroClasse == 2 && it.classe == matriz::sync::ClasseSync::Modificado) {
                itensExibidos_.push_back(it);
            } else if (filtroClasse == 3 && it.classe == matriz::sync::ClasseSync::Movido) {
                itensExibidos_.push_back(it);
            } else if (filtroClasse == 4 && it.classe == matriz::sync::ClasseSync::Removido) {
                itensExibidos_.push_back(it);
            } else if (filtroClasse == 5 && it.classe == matriz::sync::ClasseSync::Igual) {
                itensExibidos_.push_back(it);
            }
        }
        setSize(getWidth(), std::max(200, static_cast<int>(itensExibidos_.size()) * 28 + 10));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);

        int y = 5;
        int rowH = 26;
        int w = getWidth();

        for (size_t i = 0; i < itensExibidos_.size(); ++i) {
            const auto& item = itensExibidos_[i];
            juce::Rectangle<int> r(6, y, w - 12, rowH);

            if (i % 2 == 1) {
                g.setColour(tk.painelAlt.withAlpha(0.35f));
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
            }

            // Badge da classe
            bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
            juce::String classeStr;
            juce::Colour corBadge = tk.textoSecundario;
            switch (item.classe) {
                case matriz::sync::ClasseSync::Novo:
                    classeStr = isPt ? juce::String::fromUTF8("[ NOVO ]") : "[ NEW ]";
                    corBadge = juce::Colours::lightgreen;
                    break;
                case matriz::sync::ClasseSync::Modificado:
                    classeStr = isPt ? juce::String::fromUTF8("[ MODIFICADO ]") : "[ MODIFIED ]";
                    corBadge = juce::Colours::orange;
                    break;
                case matriz::sync::ClasseSync::Movido:
                    classeStr = isPt ? juce::String::fromUTF8("[ MOVIDO ]") : "[ MOVED ]";
                    corBadge = juce::Colours::cyan;
                    break;
                case matriz::sync::ClasseSync::Removido:
                    classeStr = isPt ? juce::String::fromUTF8("[ P/ LIXEIRA ]") : "[ TO TRASH ]";
                    corBadge = tk.perigo;
                    break;
                case matriz::sync::ClasseSync::Igual:
                    classeStr = isPt ? juce::String::fromUTF8("[ INALTERADO ]") : "[ UNCHANGED ]";
                    corBadge = tk.textoTerciario;
                    break;
            }

            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            g.setColour(corBadge);
            g.drawText(classeStr, r.removeFromLeft(100), juce::Justification::centredLeft, true);

            // Categoria (Media / Project)
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.setColour(tk.textoSecundario);
            juce::String catStr = (item.categoria == matriz::sync::CategoriaSync::Media)
                ? (isPt ? juce::String::fromUTF8("Mídia/") : "Media/")
                : (isPt ? juce::String::fromUTF8("Projeto/") : "Project/");
            g.drawText(catStr, r.removeFromLeft(65), juce::Justification::centredLeft, true);

            // Tamanho
            g.setColour(tk.textoTerciario);
            juce::String sizeStr = juce::File::descriptionOfSizeInBytes(item.tamanhoBytes);
            g.drawText(sizeStr, r.removeFromRight(80), juce::Justification::centredRight, true);

            // Caminho relativo e info de origem se movido
            g.setColour(tk.textoPrimario);
            juce::String pathStr = item.caminhoRelativo;
            if (item.classe == matriz::sync::ClasseSync::Movido && item.caminhoOrigemMovido.isNotEmpty()) {
                pathStr << (isPt ? juce::String::fromUTF8("  (de: ") : "  (from: ") << item.caminhoOrigemMovido << ")";
            }
            g.drawText(pathStr, r.reduced(4, 0), juce::Justification::centredLeft, true);

            y += rowH + 2;
        }
    }

private:
    std::vector<matriz::sync::ItemSync> itensExibidos_;
};

SyncDestinationDialog::SyncDestinationDialog(matriz::model::Project& projeto, std::function<void()> aoFechar)
    : projeto_(projeto), aoFechar_(std::move(aoFechar)) {
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    labelTitulo_ = std::make_unique<juce::Label>();
    labelTitulo_->setText("BACKUP SYNC", juce::dontSendNotification);
    labelTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    labelTitulo_->setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(*labelTitulo_);

    listDestinos_ = std::make_unique<juce::ListBox>("listDestinos", this);
    listDestinos_->setRowHeight(32);
    listDestinos_->setColour(juce::ListBox::backgroundColourId, tk.painelAlt);
    listDestinos_->setColour(juce::ListBox::outlineColourId, tk.borda);
    addAndMakeVisible(*listDestinos_);

    btnLocalizarDestino_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Relocalizar...") : "Relocate...");
    btnLocalizarDestino_->onClick = [this] {
        int row = listDestinos_->getSelectedRow();
        if (row >= 0 && row < static_cast<int>(destinos_.size())) {
            localizarDestino(row);
        }
    };
    addAndMakeVisible(*btnLocalizarDestino_);

    btnAdicionarExistente_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Adicionar Clone Existente...") : "Add Existing Clone...");
    btnAdicionarExistente_->onClick = [this] { adicionarDestinoExistente(); };
    addAndMakeVisible(*btnAdicionarExistente_);

    labelRef_ = std::make_unique<juce::Label>();
    labelRef_->setText(isPt ? juce::String::fromUTF8("1. REFERÊNCIA (Fonte da Verdade):") : "1. REFERENCE (Source of Truth):", juce::dontSendNotification);
    labelRef_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelRef_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelRef_);

    comboRef_ = std::make_unique<juce::ComboBox>();
    comboRef_->setTextWhenNothingSelected(isPt ? juce::String::fromUTF8("Selecionar DESTINO de Referência...") : "Select Reference DESTINATION...");
    comboRef_->onChange = [this] {
        selectedRefIdx_ = comboRef_->getSelectedId() - 1;
        atualizarValidacaoSelecao();
    };
    addAndMakeVisible(*comboRef_);

    labelAlvo_ = std::make_unique<juce::Label>();
    labelAlvo_->setText(isPt ? juce::String::fromUTF8("2. ALVO (Destino a Atualizar):") : "2. TARGET (Destination to Update):", juce::dontSendNotification);
    labelAlvo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelAlvo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelAlvo_);

    comboAlvo_ = std::make_unique<juce::ComboBox>();
    comboAlvo_->setTextWhenNothingSelected(isPt ? juce::String::fromUTF8("Selecionar DESTINO Alvo...") : "Select Target DESTINATION...");
    comboAlvo_->onChange = [this] {
        selectedAlvoIdx_ = comboAlvo_->getSelectedId() - 1;
        atualizarValidacaoSelecao();
    };
    addAndMakeVisible(*comboAlvo_);

    labelAvisoTempo_ = std::make_unique<juce::Label>();
    labelAvisoTempo_->setText(isPt ? juce::String::fromUTF8("AVISO: A Referência é MAIS ANTIGA que o Alvo. Alterações mais recentes no Alvo serão enviadas para a lixeira.")
                                  : "WARNING: The Reference is OLDER than the Target. Newer changes on Target will be moved to its trash.", juce::dontSendNotification);
    labelAvisoTempo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    labelAvisoTempo_->setColour(juce::Label::textColourId, tk.perigo);
    labelAvisoTempo_->setVisible(false);
    addAndMakeVisible(*labelAvisoTempo_);

    toggleConfirmarReferenciaAntiga_ = std::make_unique<juce::ToggleButton>(isPt ? juce::String::fromUTF8("Compreendo e desejo restaurar/reverter a partir desta referência mais antiga")
                                                                                : "I understand and want to rollback/restore from this older reference");
    toggleConfirmarReferenciaAntiga_->onClick = [this] { atualizarValidacaoSelecao(); };
    toggleConfirmarReferenciaAntiga_->setVisible(false);
    addAndMakeVisible(*toggleConfirmarReferenciaAntiga_);

    labelErroValidacao_ = std::make_unique<juce::Label>();
    labelErroValidacao_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    labelErroValidacao_->setColour(juce::Label::textColourId, tk.perigo);
    addAndMakeVisible(*labelErroValidacao_);

    btnEscanear_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Escanear e Comparar (SHA-256)...") : "Scan & Compare (SHA-256)...");
    btnEscanear_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnEscanear_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnEscanear_->onClick = [this] { iniciarEscaneamento(); };
    addAndMakeVisible(*btnEscanear_);

    btnCancelarGeral_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Cancelar") : "Cancel");
    btnCancelarGeral_->onClick = [this] { closeDialog(); };
    addAndMakeVisible(*btnCancelarGeral_);

    // Progress
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    barraProgresso_->setVisible(false);
    addAndMakeVisible(*barraProgresso_);

    labelProgressoMensagem_ = std::make_unique<juce::Label>();
    labelProgressoMensagem_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    labelProgressoMensagem_->setColour(juce::Label::textColourId, tk.textoSecundario);
    labelProgressoMensagem_->setVisible(false);
    addAndMakeVisible(*labelProgressoMensagem_);

    btnCancelarOperacao_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Abortar") : "Abort");
    btnCancelarOperacao_->onClick = [this] {
        if (cancelamento_) cancelamento_->pedir();
    };
    btnCancelarOperacao_->setVisible(false);
    addAndMakeVisible(*btnCancelarOperacao_);

    // Review
    labelResumoRevisao_ = std::make_unique<juce::Label>();
    labelResumoRevisao_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    labelResumoRevisao_->setColour(juce::Label::textColourId, tk.textoSecundario);
    labelResumoRevisao_->setVisible(false);
    addAndMakeVisible(*labelResumoRevisao_);

    comboFiltroClasse_ = std::make_unique<juce::ComboBox>();
    comboFiltroClasse_->addItem(isPt ? juce::String::fromUTF8("Todos os Itens") : "All Items", 1);
    comboFiltroClasse_->addItem(isPt ? juce::String::fromUTF8("Novos") : "New", 2);
    comboFiltroClasse_->addItem(isPt ? juce::String::fromUTF8("Modificados") : "Modified", 3);
    comboFiltroClasse_->addItem(isPt ? juce::String::fromUTF8("Movidos") : "Moved", 4);
    comboFiltroClasse_->addItem(isPt ? juce::String::fromUTF8("Para a Lixeira") : "To Trash", 5);
    comboFiltroClasse_->addItem(isPt ? juce::String::fromUTF8("Inalterados") : "Unchanged", 6);
    comboFiltroClasse_->setSelectedId(1, juce::dontSendNotification);
    comboFiltroClasse_->onChange = [this] {
        if (listaRevisao_) {
            listaRevisao_->setItens(planoAtual_.itens, comboFiltroClasse_->getSelectedId() - 1);
        }
    };
    comboFiltroClasse_->setVisible(false);
    addAndMakeVisible(*comboFiltroClasse_);

    viewportRevisao_ = std::make_unique<juce::Viewport>();
    listaRevisao_ = std::make_unique<ListaRevisaoSync>();
    viewportRevisao_->setViewedComponent(listaRevisao_.get(), false);
    viewportRevisao_->setVisible(false);
    addAndMakeVisible(*viewportRevisao_);

    btnAplicarSync_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Aplicar Sincronização") : "Apply Sync");
    btnAplicarSync_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnAplicarSync_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnAplicarSync_->onClick = [this] { aplicarSincronizacao(); };
    btnAplicarSync_->setVisible(false);
    addAndMakeVisible(*btnAplicarSync_);

    btnVoltarSelecao_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Voltar à Seleção") : "Back to Selection");
    btnVoltarSelecao_->onClick = [this] {
        fase_ = Fase::Selecao;
        resized();
        repaint();
    };
    btnVoltarSelecao_->setVisible(false);
    addAndMakeVisible(*btnVoltarSelecao_);

    // Completion
    labelResumoConcluido_ = std::make_unique<juce::Label>();
    labelResumoConcluido_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    labelResumoConcluido_->setColour(juce::Label::textColourId, tk.textoPrimario);
    labelResumoConcluido_->setVisible(false);
    addAndMakeVisible(*labelResumoConcluido_);

    btnAbrirLixeira_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Abrir Pasta da Lixeira no Finder") : "Open Trash Folder in Finder");
    btnAbrirLixeira_->onClick = [this] {
        if (resultadoAtual_.pastaLixeiraCriada.isDirectory()) {
            resultadoAtual_.pastaLixeiraCriada.revealToUser();
        }
    };
    btnAbrirLixeira_->setVisible(false);
    addAndMakeVisible(*btnAbrirLixeira_);

    btnConcluirFinal_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Concluir") : "Done");
    btnConcluirFinal_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnConcluirFinal_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnConcluirFinal_->onClick = [this] {
        closeDialog();
    };
    btnConcluirFinal_->setVisible(false);
    addAndMakeVisible(*btnConcluirFinal_);

    setSize(780, 560);
    setWantsKeyboardFocus(true);
    carregarDestinos();
}

SyncDestinationDialog::~SyncDestinationDialog() {
    if (cancelamento_) cancelamento_->pedir();
}

void SyncDestinationDialog::closeDialog() {
    if (cancelamento_) cancelamento_->pedir();
    if (aoFechar_) aoFechar_();
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
        dw->setVisible(false);
        juce::Component::SafePointer<juce::DialogWindow> safeDw(dw);
        juce::MessageManager::callAsync([safeDw] {
            if (safeDw != nullptr) {
                safeDw->removeFromDesktop();
                delete safeDw.getComponent();
            }
        });
    } else {
        setVisible(false);
    }
}

bool SyncDestinationDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        closeDialog();
        return true;
    }
    return false;
}

void SyncDestinationDialog::carregarDestinos() {
    destinos_.clear();
    comboRef_->clear();
    comboAlvo_->clear();

    auto& db = projeto_.registro();
    try {
        auto stmt = db.prepare(
            "SELECT id, destino_path, rotulo, destination_id, papel, ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida "
            "FROM backup_destino WHERE ativo = 1 ORDER BY criado_em ASC");

        while (stmt.step()) {
            DestinoInfoUI d;
            d.id = stmt.columnText(0);
            d.caminho = stmt.columnText(1);
            d.rotulo = stmt.columnText(2);
            d.destinationId = stmt.columnText(3);
            d.papel = stmt.columnText(4);
            d.revisao = stmt.columnInt(5);
            d.ultimaEdicao = stmt.columnText(7);

            juce::File pasta(d.caminho);
            if (pasta.isDirectory()) {
                d.online = true;
                auto destOpt = matriz::model::DestinationInfo::lerDeArquivo(pasta.getChildFile("destination.json"));
                if (destOpt) {
                    d.revisao = destOpt->revisao;
                    d.ultimaEdicao = destOpt->ultimaEdicaoUtc;
                    d.temMarcadorIncompleto = matriz::sync::SyncEngine::temMarcadorSyncIncompleto(pasta);
                }
            }

            destinos_.push_back(std::move(d));
        }
    } catch (...) {}

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    for (size_t i = 0; i < destinos_.size(); ++i) {
        const auto& d = destinos_[i];
        juce::String desc = d.rotulo + " (" + (d.online ? "ONLINE" : "OFFLINE") + " - Rev " + juce::String(d.revisao) + ")";
        if (d.temMarcadorIncompleto) desc << (isPt ? juce::String::fromUTF8(" [SYNC INCOMPLETO]") : " [INCOMPLETE SYNC]");

        comboRef_->addItem(desc, static_cast<int>(i + 1));
        comboAlvo_->addItem(desc, static_cast<int>(i + 1));
    }

    listDestinos_->updateContent();
    calcularTamanhosBackground();
    atualizarValidacaoSelecao();
}

void SyncDestinationDialog::calcularTamanhosBackground() {
    // Copia os caminhos ANTES de lançar a thread — o loop roda em background
    // e ~SyncDestinationDialog() só pede cancelamento_->pedir(), nunca junta
    // essa thread; sem essa cópia, ler destinos_ (membro do diálogo) de
    // dentro dela era um use-after-free em potencial assim que o diálogo
    // fechasse no meio do cálculo. safeThis (mesmo padrão usado em
    // MosaicoComponent::recarregar) garante que só o resultado final toca o
    // diálogo, e só se ele ainda existir, de volta na message thread.
    std::vector<std::pair<size_t, juce::File>> pastas;
    for (size_t i = 0; i < destinos_.size(); ++i) {
        if (destinos_[i].online) pastas.emplace_back(i, juce::File(destinos_[i].caminho));
    }

    juce::Component::SafePointer<SyncDestinationDialog> safeThis(this);
    juce::Thread::launch([safeThis, pastas] {
        for (const auto& par : pastas) {
            size_t i = par.first;
            const juce::File& pasta = par.second;
            juce::int64 total = 0;

            auto calcPasta = [&](const juce::File& dir) {
                if (!dir.isDirectory()) return;
                juce::Array<juce::File> files;
                dir.findChildFiles(files, juce::File::findFiles, true);
                for (const auto& f : files) total += f.getSize();
            };

            calcPasta(pasta.getChildFile("Media"));
            calcPasta(pasta.getChildFile("Project"));

            juce::MessageManager::callAsync([safeThis, i, total] {
                if (!safeThis) return;
                if (i < safeThis->destinos_.size()) {
                    safeThis->destinos_[i].tamanhoBytes = total;
                    safeThis->listDestinos_->repaint();
                }
            });
        }
    });
}

int SyncDestinationDialog::getNumRows() {
    return static_cast<int>(destinos_.size());
}

void SyncDestinationDialog::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(destinos_.size())) return;
    const auto& d = destinos_[rowNumber];
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    juce::Rectangle<int> r(0, 0, width, height);
    if (rowIsSelected) {
        g.setColour(tk.acento.withAlpha(0.15f));
        g.fillRect(r);
    }

    g.setColour(tk.borda.withAlpha(0.3f));
    g.fillRect(r.getX(), r.getBottom() - 1, r.getWidth(), 1);

    auto linha = r.reduced(8, 0);

    // Online/Offline status badge — estilo idêntico ao card DESTINATION
    {
        juce::String statusTexto = d.online
            ? (d.temMarcadorIncompleto ? (isPt ? "INCOMPLETO" : "INCOMPLETE") : "ONLINE")
            : "OFFLINE";
        juce::Colour statusCor = d.online
            ? (d.temMarcadorIncompleto ? tk.alerta : tk.estadoQcOk)
            : tk.textoTerciario.withAlpha(0.6f);
        int badgeW = 82;
        juce::Rectangle<int> badgeArea(linha.getX(), (height - 18) / 2, badgeW, 18);
        g.setColour(statusCor);
        g.fillRoundedRectangle(badgeArea.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText(statusTexto, badgeArea, juce::Justification::centred, true);
        linha.removeFromLeft(badgeW + 6);
    }

    // Papel (ORIGINAL / CLONE)
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    g.setColour(d.papel == "ORIGINAL" ? tk.acento : tk.textoSecundario);
    g.drawText("[" + d.papel + "]", linha.removeFromLeft(85), juce::Justification::centredLeft, true);

    // Tamanho
    g.setColour(tk.textoTerciario);
    juce::String sizeStr = (d.tamanhoBytes >= 0) ? juce::File::descriptionOfSizeInBytes(d.tamanhoBytes) : (isPt ? "calculando..." : "calculating...");
    g.drawText(sizeStr, linha.removeFromRight(85), juce::Justification::centredRight, true);

    // Revisão
    juce::String revStr = (isPt ? "Rev " : "Rev ") + juce::String(d.revisao);
    g.setColour(tk.textoSecundario);
    g.drawText(revStr, linha.removeFromRight(55), juce::Justification::centredRight, true);

    // Data da última revisão (extraída do ISO 8601: primeiros 10 chars = YYYY-MM-DD)
    juce::String dataStr = d.ultimaEdicao.isNotEmpty() ? d.ultimaEdicao.substring(0, 10) : juce::String();
    g.setColour(tk.textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 0.5f)));
    g.drawText(dataStr, linha.removeFromRight(90), juce::Justification::centredRight, true);

    // Rótulo e caminho
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    g.drawText(d.rotulo, linha.removeFromLeft(150), juce::Justification::centredLeft, true);

    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    g.drawText(d.caminho, linha, juce::Justification::centredLeft, true);
}

juce::String SyncDestinationDialog::getTooltipForRow(int rowNumber) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(destinos_.size())) return {};
    const auto& dest = destinos_[static_cast<size_t>(rowNumber)];
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    juce::File caminhoF(dest.caminho);
    auto infoVol = matriz::vault::descreverVolume(caminhoF);
    juce::String nomeVolume = infoVol.nome.empty()
        ? (infoVol.hardware.volumeLabel.empty() ? caminhoF.getFileName() : juce::String(infoVol.hardware.volumeLabel))
        : juce::String(infoVol.nome);
    if (nomeVolume.isEmpty()) nomeVolume = "Macintosh HD";

    juce::String caminhoCompleto = dest.caminho;
    if (caminhoCompleto.isEmpty()) caminhoCompleto = caminhoF.getFullPathName();

    if (isPt) {
        return juce::String::fromUTF8("Nome do Volume: ") + nomeVolume + "\n" +
               juce::String::fromUTF8("Caminho Completo: ") + caminhoCompleto;
    }
    return "Volume Name: " + nomeVolume + "\n" +
           "Full Path: " + caminhoCompleto;
}

void SyncDestinationDialog::atualizarValidacaoSelecao() {
    juce::String erro;
    bool refMaisAntiga = false;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    if (selectedRefIdx_ < 0 || selectedAlvoIdx_ < 0) {
        erro = isPt ? juce::String::fromUTF8("Selecione tanto o DESTINO de Referência quanto o Alvo para continuar.")
                    : "Select both a Reference and a Target DESTINATION to proceed.";
    } else if (selectedRefIdx_ == selectedAlvoIdx_) {
        erro = isPt ? juce::String::fromUTF8("A Referência e o Alvo não podem ser o mesmo DESTINO.")
                    : "Reference and Target cannot be the same DESTINATION.";
    } else {
        const auto& dRef = destinos_[selectedRefIdx_];
        const auto& dAlvo = destinos_[selectedAlvoIdx_];

        if (!dRef.online) erro = isPt ? juce::String::fromUTF8("O DESTINO de Referência está offline. Reconecte o disco.")
                                      : "Reference DESTINATION is offline. Reconnect the drive.";
        else if (!dAlvo.online) erro = isPt ? juce::String::fromUTF8("O DESTINO Alvo está offline. Reconecte o disco.")
                                            : "Target DESTINATION is offline. Reconnect the drive.";
        else if (dRef.temMarcadorIncompleto) erro = isPt ? juce::String::fromUTF8("O DESTINO de Referência possui uma sincronização interrompida. Não pode ser usado como referência.")
                                                         : "Reference DESTINATION has an interrupted sync. It cannot be used as reference.";
        else if (dRef.revisao < dAlvo.revisao) {
            refMaisAntiga = true;
            if (!toggleConfirmarReferenciaAntiga_->getToggleState()) {
                erro = isPt ? juce::String::fromUTF8("Confirmação necessária: A revisão da Referência é mais antiga que a do Alvo.")
                            : "Confirmation required: Reference revision is older than Target revision.";
            }
        }
    }

    labelAvisoTempo_->setVisible(refMaisAntiga);
    toggleConfirmarReferenciaAntiga_->setVisible(refMaisAntiga);
    labelErroValidacao_->setText(erro, juce::dontSendNotification);
    btnEscanear_->setEnabled(erro.isEmpty());
}

void SyncDestinationDialog::iniciarEscaneamento() {
    if (selectedRefIdx_ < 0 || selectedAlvoIdx_ < 0) return;

    juce::File refRaiz(destinos_[selectedRefIdx_].caminho);
    juce::File alvoRaiz(destinos_[selectedAlvoIdx_].caminho);

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    fase_ = Fase::Escaneando;
    progressoValor_ = 0.0;
    progressoTexto_ = isPt ? juce::String::fromUTF8("Escaneando arquivos e calculando somas de verificação SHA-256...")
                           : "Scanning files and calculating SHA-256 checksums...";
    cancelamento_ = std::make_shared<matriz::app::Cancelamento>();
    auto cancelamento = cancelamento_; // cópia local — lida só por esta operação, ver nota abaixo

    resized();
    repaint();

    // safeThis em vez de `this` bruto: esta lambda roda numa juce::Thread
    // detached (sem handle guardado, sem join no destrutor — só
    // cancelamento_->pedir()) e chama callAsync de dentro dela mesma. Se o
    // diálogo fechar enquanto o escaneamento ainda roda, `this` bruto vira
    // um ponteiro pendurado tanto na thread quanto no callAsync.
    // `cancelamento` (cópia local do shared_ptr) evita precisar ler
    // safeThis->cancelamento_ fora da message thread.
    juce::Component::SafePointer<SyncDestinationDialog> safeThis(this);
    juce::Thread::launch([safeThis, refRaiz, alvoRaiz, cancelamento] {
        auto plano = matriz::sync::SyncEngine::escanearEComparar(
            refRaiz, alvoRaiz, true,
            [safeThis](int atual, int total, const juce::String& msg) {
                juce::MessageManager::callAsync([safeThis, atual, total, msg] {
                    if (!safeThis) return;
                    safeThis->progressoValor_ = (total > 0) ? static_cast<double>(atual) / total : 0.0;
                    safeThis->progressoTexto_ = msg;
                    safeThis->labelProgressoMensagem_->setText(msg, juce::dontSendNotification);
                });
                return true;
            },
            cancelamento);

        juce::MessageManager::callAsync([safeThis, plano, cancelamento] {
            if (!safeThis) return;
            if (cancelamento && cancelamento->pedido()) {
                safeThis->fase_ = Fase::Selecao;
                safeThis->resized();
                safeThis->repaint();
                return;
            }

            bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
            safeThis->planoAtual_ = plano;
            auto& planoAtual = safeThis->planoAtual_; // alias local — safeThis já confirmado vivo acima
            if (!planoAtual.podeAplicar()) {
                juce::String msgErro;
                if (!planoAtual.errosValidacao.empty()) {
                    msgErro = juce::String(planoAtual.errosValidacao.front());
                    if (isPt) {
                        if (msgErro.startsWith("Insufficient free disk space")) {
                            msgErro = msgErro.replace("Insufficient free disk space on target destination", juce::String::fromUTF8("Espaço em disco insuficiente no destino alvo"))
                                             .replace("Required", juce::String::fromUTF8("Necessário"))
                                             .replace("Available", juce::String::fromUTF8("Disponível"));
                        } else if (msgErro.startsWith("Reference destination folder")) {
                            msgErro = msgErro.replace("Reference destination folder not found or inaccessible", juce::String::fromUTF8("Pasta de referência não encontrada ou inacessível"))
                                             .replace("Reference destination folder not accessible", juce::String::fromUTF8("Pasta de referência inacessível"));
                        } else if (msgErro.startsWith("Target destination folder")) {
                            msgErro = msgErro.replace("Target destination folder not found or inaccessible", juce::String::fromUTF8("Pasta alvo não encontrada ou inacessível"))
                                             .replace("Target destination folder not accessible", juce::String::fromUTF8("Pasta alvo inacessível"));
                        } else if (msgErro.contains("cannot be the same folder")) {
                            msgErro = juce::String::fromUTF8("A referência e o alvo não podem ser a mesma pasta.");
                        }
                    }
                } else {
                    msgErro = isPt ? juce::String::fromUTF8("Erro desconhecido") : "Unknown error";
                }

                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle(isPt ? juce::String::fromUTF8("Falha na Validação do Escaneamento") : "Scan Validation Failed")
                        .withMessage(msgErro)
                        .withButton("OK"),
                    nullptr);
                safeThis->fase_ = Fase::Selecao;
            } else {
                safeThis->fase_ = Fase::Revisao;
                juce::String resumo;
                if (isPt) {
                    resumo << juce::String::fromUTF8("Resumo: ") << planoAtual.totalNovos << juce::String::fromUTF8(" Novos, ")
                           << planoAtual.totalModificados << juce::String::fromUTF8(" Modificados, ")
                           << planoAtual.totalMovidos << juce::String::fromUTF8(" Movidos, ")
                           << planoAtual.totalRemovidos << juce::String::fromUTF8(" Para a Lixeira, ")
                           << planoAtual.totalIguais << juce::String::fromUTF8(" Inalterados | A Copiar: ")
                           << juce::File::descriptionOfSizeInBytes(planoAtual.bytesParaCopiar)
                           << juce::String::fromUTF8(" | Para a Lixeira: ")
                           << juce::File::descriptionOfSizeInBytes(planoAtual.bytesParaLixeira);
                } else {
                    resumo << "Summary: " << planoAtual.totalNovos << " New, "
                           << planoAtual.totalModificados << " Modified, "
                           << planoAtual.totalMovidos << " Moved, "
                           << planoAtual.totalRemovidos << " To Trash, "
                           << planoAtual.totalIguais << " Unchanged | To Copy: "
                           << juce::File::descriptionOfSizeInBytes(planoAtual.bytesParaCopiar)
                           << " | To Trash: "
                           << juce::File::descriptionOfSizeInBytes(planoAtual.bytesParaLixeira);
                }
                safeThis->labelResumoRevisao_->setText(resumo, juce::dontSendNotification);
                safeThis->listaRevisao_->setItens(planoAtual.itens, 0);
            }
            safeThis->resized();
            safeThis->repaint();
        });
    });
}

void SyncDestinationDialog::aplicarSincronizacao() {
    if (selectedRefIdx_ < 0 || selectedAlvoIdx_ < 0) return;

    juce::File refRaiz(destinos_[selectedRefIdx_].caminho);
    juce::File alvoRaiz(destinos_[selectedAlvoIdx_].caminho);

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    fase_ = Fase::Aplicando;
    progressoValor_ = 0.0;
    progressoTexto_ = isPt ? juce::String::fromUTF8("Aplicando alterações de sincronização ao Alvo...")
                           : "Applying synchronization changes to Target...";
    cancelamento_ = std::make_shared<matriz::app::Cancelamento>();
    auto cancelamento = cancelamento_; // cópia local — mesma razão de iniciarEscaneamento()
    auto plano = planoAtual_;

    resized();
    repaint();

    // safeThis em vez de `this` bruto — mesmo risco de iniciarEscaneamento():
    // juce::Thread detached, sem join no destrutor.
    juce::Component::SafePointer<SyncDestinationDialog> safeThis(this);
    juce::Thread::launch([safeThis, refRaiz, alvoRaiz, plano, cancelamento] {
        auto res = matriz::sync::SyncEngine::aplicarSync(
            refRaiz, alvoRaiz, plano,
            [safeThis](int atual, int total, const juce::String& msg) {
                juce::MessageManager::callAsync([safeThis, atual, total, msg] {
                    if (!safeThis) return;
                    safeThis->progressoValor_ = (total > 0) ? static_cast<double>(atual) / total : 0.0;
                    safeThis->progressoTexto_ = msg;
                    safeThis->labelProgressoMensagem_->setText(msg, juce::dontSendNotification);
                });
                return true;
            },
            cancelamento);

        juce::MessageManager::callAsync([safeThis, res] {
            if (!safeThis) return;
            safeThis->resultadoAtual_ = res;
            safeThis->fase_ = Fase::Concluido;

            bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
            juce::String resumo;
            if (res.sucesso) {
                if (isPt) {
                    resumo << juce::String::fromUTF8("Sincronização Concluída com Sucesso!\n\n")
                           << juce::String::fromUTF8("- Copiados / Atualizados: ") << res.itensCopiados << juce::String::fromUTF8(" arquivos\n")
                           << juce::String::fromUTF8("- Movidos internamente: ") << res.itensMovidos << juce::String::fromUTF8(" arquivos\n")
                           << juce::String::fromUTF8("- Enviados para a Lixeira: ") << res.itensLixeira << juce::String::fromUTF8(" arquivos\n")
                           << juce::String::fromUTF8("- Falhas: ") << (int)res.falhas.size() << "\n\n"
                           << juce::String::fromUTF8("Local da lixeira: ") << res.pastaLixeiraCriada.getFullPathName();
                } else {
                    resumo << "SYNC Completed Successfully!\n\n"
                           << "- Copied / Updated: " << res.itensCopiados << " files\n"
                           << "- Moved internally: " << res.itensMovidos << " files\n"
                           << "- Sent to Trash: " << res.itensLixeira << " files\n"
                           << "- Failures: " << (int)res.falhas.size() << "\n\n"
                           << "Trash location: " << res.pastaLixeiraCriada.getFullPathName();
                }
            } else if (res.cancelado) {
                if (isPt) {
                    resumo << juce::String::fromUTF8("A sincronização foi cancelada pelo usuário.\n\n")
                           << juce::String::fromUTF8("Arquivos parciais preservados. Execute a sincronização novamente a qualquer momento para continuar.");
                } else {
                    resumo << "SYNC was Cancelled by User.\n\n"
                           << "Partial files preserved. Run SYNC again anytime to resume.";
                }
            } else {
                if (isPt) {
                    resumo << juce::String::fromUTF8("A sincronização encontrou erros:\n\n");
                    for (const auto& f : res.falhas) resumo << "- " << f << "\n";
                } else {
                    resumo << "SYNC Encountered Errors:\n\n";
                    for (const auto& f : res.falhas) resumo << "- " << f << "\n";
                }
            }
            safeThis->labelResumoConcluido_->setText(resumo, juce::dontSendNotification);
            safeThis->resized();
            safeThis->repaint();
        });
    });
}

void SyncDestinationDialog::localizarDestino(int idx) {
    if (idx < 0 || idx >= static_cast<int>(destinos_.size())) return;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    auto chooser = std::make_shared<juce::FileChooser>(isPt ? juce::String::fromUTF8("Relocalizar pasta do DESTINO...") : "Relocate DESTINATION folder...");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this, idx, chooser](const juce::FileChooser& fc) {
                             juce::File escolhido = fc.getResult();
                             if (escolhido == juce::File()) return;

                             bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
                             auto destInfo = matriz::model::DestinationInfo::lerDeArquivo(escolhido.getChildFile("destination.json"));
                             if (!destInfo || destInfo->destinationId != destinos_[idx].destinationId) {
                                 juce::AlertWindow::showAsync(
                                     juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle(isPt ? juce::String::fromUTF8("Destino Inválido") : "Invalid Destination")
                                         .withMessage(isPt ? juce::String::fromUTF8("A pasta selecionada não corresponde ao ID deste DESTINO.")
                                                           : "The selected folder does not match this DESTINATION ID.")
                                         .withButton("OK"),
                                     nullptr);
                                 return;
                             }

                             projeto_.registro().run("UPDATE backup_destino SET destino_path = ? WHERE id = ?",
                                                     {matriz::db::Value::of(escolhido.getFullPathName().toStdString()),
                                                      matriz::db::Value::of(destinos_[idx].id)});
                             carregarDestinos();
                         });
}

void SyncDestinationDialog::adicionarDestinoExistente() {
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    auto chooser = std::make_shared<juce::FileChooser>(isPt ? juce::String::fromUTF8("Selecionar Pasta de DESTINO Existente...") : "Select Existing DESTINATION Folder...");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this, chooser](const juce::FileChooser& fc) {
                             juce::File escolhido = fc.getResult();
                             if (escolhido == juce::File()) return;

                             bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

                             auto dentroDeMediaOuProject = [](const juce::File& f) {
                                 juce::File cur = f;
                                 while (cur != juce::File() && cur != cur.getParentDirectory()) {
                                     if (cur.getFileName().equalsIgnoreCase("Media") || cur.getFileName().equalsIgnoreCase("Project"))
                                         return true;
                                     cur = cur.getParentDirectory();
                                 }
                                 return false;
                             };

                             if (dentroDeMediaOuProject(escolhido) || escolhido.isAChildOf(projeto_.raiz()) || escolhido.isAChildOf(projeto_.pasta())) {
                                 juce::AlertWindow::showAsync(
                                     juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle(isPt ? juce::String::fromUTF8("Destino Inválido") : "Invalid Destination")
                                         .withMessage(isPt ? juce::String::fromUTF8("A pasta de destino não pode se chamar 'Media' ou 'Project', nem estar dentro de uma pasta Media ou Project.")
                                                           : "The destination folder cannot be named 'Media' or 'Project', nor be inside a Media or Project folder.")
                                         .withButton("OK"),
                                     nullptr);
                                 return;
                             }

                             escolhido = matriz::model::normalizarParaRaizDestino(escolhido);
                             auto destInfo = matriz::model::DestinationInfo::lerDeArquivo(escolhido.getChildFile("destination.json"));
                             if (!destInfo || destInfo->projetoId != projeto_.projetoId()) {
                                 juce::AlertWindow::showAsync(
                                     juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle(isPt ? juce::String::fromUTF8("Clone Inválido") : "Invalid Clone")
                                         .withMessage(isPt ? juce::String::fromUTF8("A pasta selecionada não contém um clone válido para este projeto ativo.")
                                                           : "The selected folder does not hold a valid clone for this active project.")
                                         .withButton("OK"),
                                     nullptr);
                                 return;
                             }

                             std::string agora = matriz::model::agoraIso8601();
                             projeto_.registro().run(
                                 "INSERT INTO backup_destino (id, destino_path, rotulo, ativo, criado_em, destination_id, papel, ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
                                 "VALUES (?, ?, ?, 1, ?, ?, ?, ?, ?, ?) "
                                 "ON CONFLICT(id) DO UPDATE SET destino_path = excluded.destino_path, ultimo_visto_em = excluded.ultimo_visto_em, "
                                 "ultima_edicao_conhecida = excluded.ultima_edicao_conhecida, ultima_revisao_conhecida = excluded.ultima_revisao_conhecida",
                                 {
                                     matriz::db::Value::of(destInfo->destinationId),
                                     matriz::db::Value::of(escolhido.getFullPathName().toStdString()),
                                     matriz::db::Value::of(destInfo->rotulo.empty() ? escolhido.getFileName().toStdString() : destInfo->rotulo),
                                     matriz::db::Value::of(destInfo->criadoEm.empty() ? agora : destInfo->criadoEm),
                                     matriz::db::Value::of(destInfo->destinationId),
                                     matriz::db::Value::of(destInfo->papel),
                                     matriz::db::Value::of(static_cast<long long>(destInfo->revisao)),
                                     matriz::db::Value::of(agora),
                                     matriz::db::Value::of(destInfo->ultimaEdicaoUtc.empty() ? agora : destInfo->ultimaEdicaoUtc)
                                 });

                             carregarDestinos();
                         });
}

void SyncDestinationDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);
    g.setColour(tk.borda);
    g.drawRect(getLocalBounds(), 1);
}

void SyncDestinationDialog::resized() {
    auto area = getLocalBounds().reduced(20);
    labelTitulo_->setBounds(area.removeFromTop(28));
    area.removeFromTop(12);

    bool emSelecao = (fase_ == Fase::Selecao);
    bool emProgresso = (fase_ == Fase::Escaneando || fase_ == Fase::Aplicando);
    bool emRevisao = (fase_ == Fase::Revisao);
    bool emConcluido = (fase_ == Fase::Concluido);

    listDestinos_->setVisible(emSelecao);
    btnLocalizarDestino_->setVisible(emSelecao);
    btnAdicionarExistente_->setVisible(emSelecao);
    labelRef_->setVisible(emSelecao);
    comboRef_->setVisible(emSelecao);
    labelAlvo_->setVisible(emSelecao);
    comboAlvo_->setVisible(emSelecao);
    labelErroValidacao_->setVisible(emSelecao);
    btnEscanear_->setVisible(emSelecao);
    btnCancelarGeral_->setVisible(emSelecao);

    barraProgresso_->setVisible(emProgresso);
    labelProgressoMensagem_->setVisible(emProgresso);
    btnCancelarOperacao_->setVisible(emProgresso);

    labelResumoRevisao_->setVisible(emRevisao);
    comboFiltroClasse_->setVisible(emRevisao);
    viewportRevisao_->setVisible(emRevisao);
    btnAplicarSync_->setVisible(emRevisao);
    btnVoltarSelecao_->setVisible(emRevisao);

    labelResumoConcluido_->setVisible(emConcluido);
    btnAbrirLixeira_->setVisible(emConcluido);
    btnConcluirFinal_->setVisible(emConcluido);

    if (emSelecao) {
        listDestinos_->setBounds(area.removeFromTop(140));
        area.removeFromTop(8);

        auto btnRow = area.removeFromTop(26);
        btnLocalizarDestino_->setBounds(btnRow.removeFromLeft(120));
        btnRow.removeFromLeft(8);
        btnAdicionarExistente_->setBounds(btnRow.removeFromLeft(180));

        area.removeFromTop(12);

        auto rowSel = area.removeFromTop(48);
        auto colRef = rowSel.removeFromLeft((rowSel.getWidth() - 16) / 2);
        rowSel.removeFromLeft(16);
        auto colAlvo = rowSel;

        labelRef_->setBounds(colRef.removeFromTop(18));
        comboRef_->setBounds(colRef.removeFromTop(26));

        labelAlvo_->setBounds(colAlvo.removeFromTop(18));
        comboAlvo_->setBounds(colAlvo.removeFromTop(26));

        if (labelAvisoTempo_->isVisible()) {
            area.removeFromTop(6);
            labelAvisoTempo_->setBounds(area.removeFromTop(18));
            toggleConfirmarReferenciaAntiga_->setBounds(area.removeFromTop(22));
        }

        area.removeFromTop(6);
        labelErroValidacao_->setBounds(area.removeFromTop(18));

        auto bottomRow = area.removeFromBottom(30);
        btnEscanear_->setBounds(bottomRow.removeFromRight(200));
        bottomRow.removeFromRight(10);
        btnCancelarGeral_->setBounds(bottomRow.removeFromRight(100));
    } else if (emProgresso) {
        area.removeFromTop(60);
        labelProgressoMensagem_->setBounds(area.removeFromTop(22));
        area.removeFromTop(8);
        barraProgresso_->setBounds(area.removeFromTop(26));
        area.removeFromTop(20);
        btnCancelarOperacao_->setBounds(area.removeFromTop(28).removeFromLeft(120));
    } else if (emRevisao) {
        auto topRow = area.removeFromTop(26);
        comboFiltroClasse_->setBounds(topRow.removeFromRight(140));
        topRow.removeFromRight(10);
        labelResumoRevisao_->setBounds(topRow);

        area.removeFromTop(8);
        auto bottomRow = area.removeFromBottom(32);
        btnAplicarSync_->setBounds(bottomRow.removeFromRight(180));
        bottomRow.removeFromRight(10);
        btnVoltarSelecao_->setBounds(bottomRow.removeFromRight(140));

        viewportRevisao_->setBounds(area);
        if (listaRevisao_) listaRevisao_->setSize(viewportRevisao_->getWidth() - 16, listaRevisao_->getHeight());
    } else if (emConcluido) {
        labelResumoConcluido_->setBounds(area.removeFromTop(200));
        area.removeFromTop(20);
        auto bottomRow = area.removeFromBottom(32);
        btnConcluirFinal_->setBounds(bottomRow.removeFromRight(120));
        bottomRow.removeFromRight(12);
        btnAbrirLixeira_->setBounds(bottomRow.removeFromRight(220));
    }
}

void SyncDestinationDialog::lookAndFeelChanged() {
    repaint();
}

void SyncDestinationDialog::abrirModal(matriz::model::Project& projeto, std::function<void()> aoConcluir) {
    auto janela = std::make_unique<SyncDestinationDialog>(projeto, [aoConcluir] {
        if (aoConcluir) aoConcluir();
    });

    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "BACKUP SYNC";
    opt.content.set(janela.release(), true);
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

} // namespace matriz::ui
