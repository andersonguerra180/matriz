#include "BackupFileSelectorDialog.h"
#include "MosaicoComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::ui {

BackupFileSelectorDialog::BackupFileSelectorDialog(ProjetoAberto& projeto, const std::set<std::string>& initialSelectedIds)
    : projeto_(projeto), selectedIds_(initialSelectedIds)
{
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    // === HEADER ===
    lblTitulo_ = std::make_unique<juce::Label>();
    lblTitulo_->setText(isPt ? juce::String::fromUTF8("SELECIONAR ARQUIVOS PARA BACKUP") : "SELECT FILES FOR BACKUP", juce::dontSendNotification);
    lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    campoBusca_ = std::make_unique<juce::TextEditor>();
    campoBusca_->setTextToShowWhenEmpty(isPt ? juce::String::fromUTF8("Buscar arquivo...") : "Search files...", tk.textoTerciario);
    campoBusca_->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
    campoBusca_->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    campoBusca_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    campoBusca_->onTextChange = [this] {
        if (mosaico_) mosaico_->definirBusca(campoBusca_->getText());
        if (btnLimparBusca_) btnLimparBusca_->setVisible(campoBusca_->getText().isNotEmpty());
        atualizarResumoSelecao();
    };
    addAndMakeVisible(*campoBusca_);

    btnLimparBusca_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x9c\x95"));
    btnLimparBusca_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    btnLimparBusca_->setColour(juce::TextButton::textColourOffId, tk.textoTerciario);
    btnLimparBusca_->onClick = [this] {
        campoBusca_->setText("", juce::sendNotification);
    };
    addChildComponent(*btnLimparBusca_);

    btnVisaoGrade_ = std::make_unique<ViewModeIconButton>(ViewModeIconButton::IconType::Grid);
    btnVisaoGrade_->setAtivo(true);
    btnVisaoGrade_->setTooltip(isPt ? juce::String::fromUTF8("Modo Grade") : "Grid View");
    btnVisaoGrade_->onClick = [this] {
        modoVisaoGrade_ = true;
        if (mosaico_) {
            mosaico_->definirModoVisao(MosaicoComponent::ModoVisao::Grade);
            if (sliderTamanho_) mosaico_->definirTamanhoContinuo(sliderTamanho_->getValue());
        }
        btnVisaoGrade_->setAtivo(true);
        btnVisaoLista_->setAtivo(false);
        if (sliderTamanho_) sliderTamanho_->setEnabled(true);
    };
    addAndMakeVisible(*btnVisaoGrade_);

    btnVisaoLista_ = std::make_unique<ViewModeIconButton>(ViewModeIconButton::IconType::List);
    btnVisaoLista_->setAtivo(false);
    btnVisaoLista_->setTooltip(isPt ? juce::String::fromUTF8("Modo Lista") : "List View");
    btnVisaoLista_->onClick = [this] {
        modoVisaoGrade_ = false;
        if (mosaico_) mosaico_->definirModoVisao(MosaicoComponent::ModoVisao::Lista);
        btnVisaoLista_->setAtivo(true);
        btnVisaoGrade_->setAtivo(false);
        if (sliderTamanho_) sliderTamanho_->setEnabled(false);
    };
    addAndMakeVisible(*btnVisaoLista_);

    sliderTamanho_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    sliderTamanho_->setRange(0.0, 1.0, 0.01);
    sliderTamanho_->setValue(0.24, juce::dontSendNotification);
    sliderTamanho_->setColour(juce::Slider::trackColourId, tk.borda);
    sliderTamanho_->setColour(juce::Slider::thumbColourId, tk.acento);
    sliderTamanho_->setColour(juce::Slider::backgroundColourId, tk.painelAlt);
    sliderTamanho_->onValueChange = [this] {
        if (mosaico_) mosaico_->definirTamanhoContinuo(sliderTamanho_->getValue());
    };
    sliderTamanho_->setTooltip("Adjust thumbnail display size");
    addAndMakeVisible(*sliderTamanho_);

    lblTamanho_ = std::make_unique<juce::Label>("", isPt ? "TAMANHO" : "SIZE");
    lblTamanho_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteMicro, juce::Font::bold)));
    lblTamanho_->setColour(juce::Label::textColourId, tk.textoTerciario);
    lblTamanho_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblTamanho_);

    // === SIDEBAR CONTROLS ===
    lblSecaoCategorias_ = std::make_unique<juce::Label>("", isPt ? "CATEGORIAS" : "CATEGORIES");
    lblSecaoCategorias_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblSecaoCategorias_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblSecaoCategorias_);

    lblSecaoFiltros_ = std::make_unique<juce::Label>("", isPt ? "FILTROS ADICIONAIS" : "ADDITIONAL FILTERS");
    lblSecaoFiltros_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblSecaoFiltros_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblSecaoFiltros_);

    comboAnos_ = std::make_unique<juce::ComboBox>();
    comboAnos_->setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
    comboAnos_->setColour(juce::ComboBox::textColourId, tk.textoPrimario);
    comboAnos_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboAnos_->onChange = [this] { aplicarFiltros(); };
    addAndMakeVisible(*comboAnos_);

    comboColecoes_ = std::make_unique<juce::ComboBox>();
    comboColecoes_->setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
    comboColecoes_->setColour(juce::ComboBox::textColourId, tk.textoPrimario);
    comboColecoes_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboColecoes_->onChange = [this] { aplicarFiltros(); };
    addAndMakeVisible(*comboColecoes_);

    lblSecaoSelecao_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("AÇÕES DE SELEÇÃO") : "SELECTION ACTIONS");
    lblSecaoSelecao_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblSecaoSelecao_->setColour(juce::Label::textColourId, tk.textoTerciario);
    addAndMakeVisible(*lblSecaoSelecao_);

    btnSelecionarTodos_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Selecionar Todos") : "Select All");
    btnSelecionarTodos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnSelecionarTodos_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnSelecionarTodos_->onClick = [this] {
        if (mosaico_) {
            mosaico_->selecionarTodos();
            selectedIds_ = mosaico_->itensSelecionados();
            atualizarResumoSelecao();
        }
    };
    addAndMakeVisible(*btnSelecionarTodos_);

    btnDesmarcarTodos_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Desmarcar Todos") : "Deselect All");
    btnDesmarcarTodos_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnDesmarcarTodos_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
    btnDesmarcarTodos_->onClick = [this] {
        if (mosaico_) {
            mosaico_->limparSelecao();
            selectedIds_.clear();
            atualizarResumoSelecao();
        }
    };
    addAndMakeVisible(*btnDesmarcarTodos_);

    btnInverterSelecao_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("Inverter Seleção") : "Invert Selection");
    btnInverterSelecao_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnInverterSelecao_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
    btnInverterSelecao_->onClick = [this] {
        if (mosaico_) {
            auto visiveis = mosaico_->todosItensEmMemoria();
            auto atuas = mosaico_->itensSelecionados();
            std::set<std::string> invertidos;
            for (const auto& item : visiveis) {
                if (atuas.find(item.id) == atuas.end())
                    invertidos.insert(item.id);
            }
            mosaico_->definirSelecao(invertidos);
            selectedIds_ = invertidos;
            atualizarResumoSelecao();
        }
    };
    addAndMakeVisible(*btnInverterSelecao_);

    lblContadorSelecao_ = std::make_unique<juce::Label>();
    lblContadorSelecao_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    lblContadorSelecao_->setColour(juce::Label::textColourId, tk.textoSecundario);
    lblContadorSelecao_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblContadorSelecao_);

    // === MAIN MOSAICO ===
    mosaico_ = std::make_unique<MosaicoComponent>(projeto_);
    mosaico_->aoMudarSelecao = [this] {
        if (mosaico_) {
            selectedIds_ = mosaico_->itensSelecionados();
            atualizarResumoSelecao();
        }
    };
    mosaico_->aoMudarConteudoVisivel = [this] {
        atualizarContagens();
        atualizarResumoSelecao();
    };
    mosaicoViewport_ = std::make_unique<juce::Viewport>();
    mosaicoViewport_->setViewedComponent(mosaico_.get(), false);
    addAndMakeVisible(*mosaicoViewport_);

    // === FOOTER ===
    lblResumo_ = std::make_unique<juce::Label>();
    lblResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    lblResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    lblResumo_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*lblResumo_);

    btnCancelar_ = std::make_unique<juce::TextButton>(isPt ? "CANCELAR" : "CANCEL");
    btnCancelar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
    btnCancelar_->onClick = [this] {
        if (aoCancelar) aoCancelar();
    };
    addAndMakeVisible(*btnCancelar_);

    btnConfirmar_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("SELECIONAR ARQUIVOS") : "SELECT FILES");
    btnConfirmar_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnConfirmar_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnConfirmar_->onClick = [this] {
        if (aoConfirmar && mosaico_) {
            aoConfirmar(mosaico_->itensSelecionados());
        }
    };
    addAndMakeVisible(*btnConfirmar_);

    construirSidebar();
    mosaico_->recarregarSincrono();
    mosaico_->definirSelecao(selectedIds_);
    atualizarContagens();
    atualizarResumoSelecao();
}

BackupFileSelectorDialog::~BackupFileSelectorDialog() = default;

void BackupFileSelectorDialog::construirSidebar() {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    for (auto& b : botoesCategorias_) removeChildComponent(b.get());
    botoesCategorias_.clear();
    categorias_.clear();

    categorias_.push_back({isPt ? juce::String::fromUTF8("Todos os Arquivos") : "All Files", "all", 0});
    categorias_.push_back({isPt ? juce::String::fromUTF8("Áudio") : "Audio", "audio", 0});
    categorias_.push_back({isPt ? juce::String::fromUTF8("Vídeo") : "Video", "video", 0});
    categorias_.push_back({isPt ? juce::String::fromUTF8("Imagens") : "Images", "images", 0});
    categorias_.push_back({isPt ? juce::String::fromUTF8("Documentos") : "Documents", "documents", 0});
    categorias_.push_back({isPt ? juce::String::fromUTF8("Sessões / Projetos") : "Sessions / Projects", "sessions", 0});
    categorias_.push_back({isPt ? juce::String::fromUTF8("Outros") : "Other", "other", 0});

    const auto& tk = tema();
    for (size_t i = 0; i < categorias_.size(); ++i) {
        auto btn = std::make_unique<juce::TextButton>(categorias_[i].rotulo);
        btn->setColour(juce::TextButton::buttonColourId, (static_cast<int>(i) == categoriaSelecionada_) ? tk.acento.withAlpha(0.25f) : juce::Colours::transparentBlack);
        btn->setColour(juce::TextButton::textColourOffId, (static_cast<int>(i) == categoriaSelecionada_) ? tk.textoPrimario : tk.textoSecundario);
        btn->onClick = [this, idx = static_cast<int>(i)] {
            selecionarCategoria(idx);
        };
        addAndMakeVisible(*btn);
        botoesCategorias_.push_back(std::move(btn));
    }

    // Populate Years and Collections Combos
    auto itens = projeto_.listarItens();
    std::set<int> anos;
    std::set<std::string> colecoes;
    for (const auto& item : itens) {
        if (item.ano.has_value() && *item.ano > 0) anos.insert(*item.ano);
        if (item.collectionType.has_value() && !item.collectionType->empty()) colecoes.insert(*item.collectionType);
    }

    comboAnos_->clear(juce::dontSendNotification);
    comboAnos_->addItem(isPt ? juce::String::fromUTF8("Todos os Anos") : "All Years", 1);
    int anoId = 2;
    for (int a : anos) {
        comboAnos_->addItem(juce::String(a), anoId++);
    }
    comboAnos_->setSelectedId(1, juce::dontSendNotification);

    comboColecoes_->clear(juce::dontSendNotification);
    comboColecoes_->addItem(isPt ? juce::String::fromUTF8("Todas as Coleções") : "All Collections", 1);
    int colId = 2;
    for (const auto& col : colecoes) {
        comboColecoes_->addItem(col, colId++);
    }
    comboColecoes_->setSelectedId(1, juce::dontSendNotification);
}

void BackupFileSelectorDialog::atualizarContagens() {
    auto itens = projeto_.listarItens();
    int cTotal = static_cast<int>(itens.size());
    int cAudio = 0, cVideo = 0, cImg = 0, cDoc = 0, cSess = 0, cOther = 0;

    for (const auto& item : itens) {
        auto ext = juce::String(item.extensaoArquivo).toLowerCase();
        auto cat = matriz::ingest::categoriaPorExtensao(ext);
        switch (cat) {
            case matriz::ingest::CategoriaMidia::Audio: cAudio++; break;
            case matriz::ingest::CategoriaMidia::Video: cVideo++; break;
            case matriz::ingest::CategoriaMidia::Imagem:
            case matriz::ingest::CategoriaMidia::Arte: cImg++; break;
            case matriz::ingest::CategoriaMidia::Documento:
            case matriz::ingest::CategoriaMidia::Texto: cDoc++; break;
            case matriz::ingest::CategoriaMidia::Sessao: cSess++; break;
            default: cOther++; break;
        }
    }

    if (categorias_.size() >= 7) {
        categorias_[0].contagem = cTotal;
        categorias_[1].contagem = cAudio;
        categorias_[2].contagem = cVideo;
        categorias_[3].contagem = cImg;
        categorias_[4].contagem = cDoc;
        categorias_[5].contagem = cSess;
        categorias_[6].contagem = cOther;
    }

    atualizarBotoesSidebar();
}

void BackupFileSelectorDialog::atualizarBotoesSidebar() {
    const auto& tk = tema();
    for (size_t i = 0; i < categorias_.size() && i < botoesCategorias_.size(); ++i) {
        bool ativo = (static_cast<int>(i) == categoriaSelecionada_);
        juce::String txt = categorias_[i].rotulo + " (" + juce::String(categorias_[i].contagem) + ")";
        botoesCategorias_[i]->setButtonText(txt);
        botoesCategorias_[i]->setColour(juce::TextButton::buttonColourId, ativo ? tk.acento.withAlpha(0.25f) : juce::Colours::transparentBlack);
        botoesCategorias_[i]->setColour(juce::TextButton::textColourOffId, ativo ? tk.textoPrimario : tk.textoSecundario);
    }
}

void BackupFileSelectorDialog::selecionarCategoria(int indice) {
    if (indice < 0 || indice >= static_cast<int>(categorias_.size())) return;
    categoriaSelecionada_ = indice;
    atualizarBotoesSidebar();
    aplicarFiltros();
}

void BackupFileSelectorDialog::aplicarFiltros() {
    if (!mosaico_) return;

    std::string chaveCat = (categoriaSelecionada_ >= 0 && categoriaSelecionada_ < static_cast<int>(categorias_.size()))
                           ? categorias_[static_cast<size_t>(categoriaSelecionada_)].chave : "all";

    int idAno = comboAnos_ ? comboAnos_->getSelectedId() : 1;
    int anoFiltro = (idAno > 1) ? comboAnos_->getText().getIntValue() : 0;

    int idCol = comboColecoes_ ? comboColecoes_->getSelectedId() : 1;
    juce::String colFiltro = (idCol > 1) ? comboColecoes_->getText() : "";

    auto itens = projeto_.listarItens();
    std::set<std::string> filteredIds;
    bool anyFilter = false;

    for (const auto& item : itens) {
        if (chaveCat != "all") {
            anyFilter = true;
            auto ext = juce::String(item.extensaoArquivo).toLowerCase();
            auto cat = matriz::ingest::categoriaPorExtensao(ext);
            bool match = false;
            if (chaveCat == "audio") match = (cat == matriz::ingest::CategoriaMidia::Audio);
            else if (chaveCat == "video") match = (cat == matriz::ingest::CategoriaMidia::Video);
            else if (chaveCat == "images") match = (cat == matriz::ingest::CategoriaMidia::Imagem || cat == matriz::ingest::CategoriaMidia::Arte);
            else if (chaveCat == "documents") match = (cat == matriz::ingest::CategoriaMidia::Documento || cat == matriz::ingest::CategoriaMidia::Texto);
            else if (chaveCat == "sessions") match = (cat == matriz::ingest::CategoriaMidia::Sessao);
            else if (chaveCat == "other") match = (cat == matriz::ingest::CategoriaMidia::Desconhecida);
            if (!match) continue;
        }

        if (anoFiltro > 0) {
            anyFilter = true;
            if (!item.ano.has_value() || *item.ano != anoFiltro) continue;
        }

        if (colFiltro.isNotEmpty()) {
            anyFilter = true;
            if (!item.collectionType.has_value() || *item.collectionType != colFiltro.toStdString()) continue;
        }

        filteredIds.insert(item.id);
    }

    if (anyFilter) {
        mosaico_->definirFiltroItens(std::move(filteredIds));
    } else {
        mosaico_->definirFiltroItens(std::nullopt);
    }

    mosaico_->recarregar();
    atualizarResumoSelecao();
}

void BackupFileSelectorDialog::atualizarResumoSelecao() {
    if (!mosaico_) return;

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    auto selecionados = mosaico_->itensSelecionados();
    int totalVisiveis = mosaico_->totalItensVisiveis();
    int totalSelecionados = static_cast<int>(selecionados.size());

    // Compute cumulative size of selected items
    juce::int64 totalBytes = 0;
    auto todos = mosaico_->todosItensEmMemoria();
    for (const auto& item : todos) {
        if (selecionados.find(item.id) != selecionados.end()) {
            totalBytes += item.tamanhoBytes;
        }
    }

    juce::String descTamanho = juce::File::descriptionOfSizeInBytes(totalBytes);
    juce::String textoResumo = isPt
        ? (juce::String(totalSelecionados) + (totalSelecionados == 1 ? " arquivo selecionado (" : " arquivos selecionados (") + descTamanho + ")")
        : (juce::String(totalSelecionados) + (totalSelecionados == 1 ? " file selected (" : " files selected (") + descTamanho + ")");

    if (lblResumo_) lblResumo_->setText(textoResumo, juce::dontSendNotification);

    juce::String textoContador = isPt
        ? (juce::String(totalSelecionados) + " de " + juce::String(totalVisiveis) + " visíveis")
        : (juce::String(totalSelecionados) + " of " + juce::String(totalVisiveis) + " visible");
    if (lblContadorSelecao_) lblContadorSelecao_->setText(textoContador, juce::dontSendNotification);

    if (btnConfirmar_) {
        btnConfirmar_->setEnabled(totalSelecionados > 0);
    }
}

void BackupFileSelectorDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Header strip
    g.setColour(tk.painel);
    g.fillRect(0, 0, getWidth(), 54);
    g.setColour(tk.borda);
    g.drawHorizontalLine(53, 0.0f, static_cast<float>(getWidth()));

    // Sidebar background
    int sidebarW = 230;
    int footerH = 54;
    int bodyH = getHeight() - 54 - footerH;
    g.setColour(tk.painel);
    g.fillRect(0, 54, sidebarW, bodyH);
    g.setColour(tk.borda);
    g.drawVerticalLine(sidebarW, 54.0f, static_cast<float>(54 + bodyH));

    // Footer strip
    g.setColour(tk.painel);
    g.fillRect(0, getHeight() - footerH, getWidth(), footerH);
    g.setColour(tk.borda);
    g.drawHorizontalLine(getHeight() - footerH, 0.0f, static_cast<float>(getWidth()));
}

void BackupFileSelectorDialog::resized() {
    const auto& tk = tema();
    auto area = getLocalBounds();

    // 1. Header (54px)
    auto header = area.removeFromTop(54).reduced(tk.espacoMedio, 10);
    if (lblTitulo_) lblTitulo_->setBounds(header.removeFromLeft(300));

    if (sliderTamanho_ && lblTamanho_) {
        sliderTamanho_->setBounds(header.removeFromRight(100));
        header.removeFromRight(4);
        lblTamanho_->setBounds(header.removeFromRight(50));
        header.removeFromRight(tk.espacoMedio);
    }
    if (btnVisaoLista_) {
        btnVisaoLista_->setBounds(header.removeFromRight(32));
        header.removeFromRight(4);
    }
    if (btnVisaoGrade_) {
        btnVisaoGrade_->setBounds(header.removeFromRight(32));
        header.removeFromRight(tk.espacoMedio);
    }
    if (campoBusca_) {
        int buscaW = std::min(240, header.getWidth());
        auto buscaArea = header.removeFromRight(buscaW);
        campoBusca_->setBounds(buscaArea);
        if (btnLimparBusca_) {
            btnLimparBusca_->setBounds(buscaArea.removeFromRight(24).reduced(2));
        }
    }

    // 2. Footer (54px)
    auto footer = area.removeFromBottom(54).reduced(tk.espacoGrande, 10);
    if (btnConfirmar_) {
        btnConfirmar_->setBounds(footer.removeFromRight(190));
        footer.removeFromRight(tk.espacoMedio);
    }
    if (btnCancelar_) {
        btnCancelar_->setBounds(footer.removeFromRight(110));
        footer.removeFromRight(tk.espacoGrande);
    }
    if (lblResumo_) {
        lblResumo_->setBounds(footer);
    }

    // 3. Body: Sidebar (230px) + Main Viewport
    int sidebarW = 230;
    auto sidebar = area.removeFromLeft(sidebarW).reduced(12, 10);

    if (lblSecaoCategorias_) lblSecaoCategorias_->setBounds(sidebar.removeFromTop(18));
    sidebar.removeFromTop(4);
    for (auto& btn : botoesCategorias_) {
        if (btn) {
            btn->setBounds(sidebar.removeFromTop(24));
            sidebar.removeFromTop(2);
        }
    }

    sidebar.removeFromTop(8);
    if (lblSecaoFiltros_) lblSecaoFiltros_->setBounds(sidebar.removeFromTop(18));
    sidebar.removeFromTop(4);
    if (comboAnos_) {
        comboAnos_->setBounds(sidebar.removeFromTop(26));
        sidebar.removeFromTop(4);
    }
    if (comboColecoes_) {
        comboColecoes_->setBounds(sidebar.removeFromTop(26));
        sidebar.removeFromTop(8);
    }

    if (lblSecaoSelecao_) lblSecaoSelecao_->setBounds(sidebar.removeFromTop(18));
    sidebar.removeFromTop(4);
    if (btnSelecionarTodos_) {
        btnSelecionarTodos_->setBounds(sidebar.removeFromTop(26));
        sidebar.removeFromTop(4);
    }
    if (btnDesmarcarTodos_) {
        btnDesmarcarTodos_->setBounds(sidebar.removeFromTop(26));
        sidebar.removeFromTop(4);
    }
    if (btnInverterSelecao_) {
        btnInverterSelecao_->setBounds(sidebar.removeFromTop(26));
        sidebar.removeFromTop(4);
    }
    if (lblContadorSelecao_) {
        lblContadorSelecao_->setBounds(sidebar.removeFromTop(20));
    }

    // 4. Center Main Viewport
    if (mosaicoViewport_) {
        mosaicoViewport_->setBounds(area);
        if (mosaico_) mosaico_->setSize(area.getWidth(), mosaico_->getHeight());
    }
}

void BackupFileSelectorDialog::lookAndFeelChanged() {
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (lblTitulo_) {
        lblTitulo_->setText(isPt ? juce::String::fromUTF8("SELECIONAR ARQUIVOS PARA BACKUP") : "SELECT FILES FOR BACKUP", juce::dontSendNotification);
        lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (btnCancelar_) {
        btnCancelar_->setButtonText(isPt ? "CANCELAR" : "CANCEL");
        btnCancelar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnCancelar_->setColour(juce::TextButton::textColourOffId, tk.textoSecundario);
    }
    if (btnConfirmar_) {
        btnConfirmar_->setButtonText(isPt ? juce::String::fromUTF8("SELECIONAR ARQUIVOS") : "SELECT FILES");
        btnConfirmar_->setColour(juce::TextButton::buttonColourId, tk.acento);
        btnConfirmar_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    }
    if (mosaico_) mosaico_->sendLookAndFeelChange();
    repaint();
}

void BackupFileSelectorDialog::exibirModal(ProjetoAberto& projeto,
                                          const std::set<std::string>& initialSelectedIds,
                                          juce::Component* parentComp,
                                          std::function<void(const std::set<std::string>&)> callback)
{
    auto dlg = std::make_unique<BackupFileSelectorDialog>(projeto, initialSelectedIds);
    dlg->setSize(1040, 680);

    auto* rawDlg = dlg.get();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dlg.release());
    opt.dialogTitle = isPt ? juce::String::fromUTF8("Selecionar Arquivos para Backup") : "Select Files for Backup";
    opt.dialogBackgroundColour = tema().fundo;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = false;
    opt.resizable = true;

    auto* win = opt.launchAsync();
    if (win) {
        win->setResizeLimits(850, 520, 1920, 1200);
    }

    rawDlg->aoCancelar = [win] {
        if (win) win->exitModalState(0);
    };

    rawDlg->aoConfirmar = [win, cb = std::move(callback)](const std::set<std::string>& novosIds) {
        if (cb) cb(novosIds);
        if (win) win->exitModalState(0);
    };
}

} // namespace matriz::ui
