#include "CatalogWorkspaceComponent.h"
#include "AcoesItem.h"
#include "FichaPanelComponent.h"
#include "MosaicoComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../App/Preferencias.h"
#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"

#include "ArvoreBackupComponent.h"
#include "EstatisticasComponent.h"
#include "PainelDuplicatasComponent.h"
#include "OfflineAssetRelinkDialog.h"
#include "../Vault/Resolucao.h"

namespace matriz::ui {

CatalogWorkspaceComponent::CatalogWorkspaceComponent(ProjetoAberto& projeto)
    : projeto_(projeto)
{
    mosaico_ = std::make_unique<MosaicoComponent>(projeto_);
    mosaico_->aoSelecionar = [this](const std::string& itemId) { selecionarItem(itemId); };
    mosaico_->aoAbrirPreview = [this](const std::string& itemId) { abrirWorkbench(itemId); };
    mosaico_->aoAbrirRelinkOffline = [this](const std::string& itemId) { abrirRelinkOffline(itemId); };
    mosaico_->aoPedirMenuContexto = [this](std::vector<std::string> itemIds) {
        abrirMenuContexto(std::move(itemIds));
    };
    mosaico_->aoMudarSelecao = [this] {
        if (categoriaSelecionada_ >= 0 && categoriaSelecionada_ < static_cast<int>(categorias_.size())) {
            if (categorias_[static_cast<size_t>(categoriaSelecionada_)].chave == "selected") {
                aplicarFiltrosAdicionais();
            }
        }
        atualizarContagens();
    };
    mosaicoViewport_ = std::make_unique<juce::Viewport>();
    mosaicoViewport_->setViewedComponent(mosaico_.get(), false);
    addAndMakeVisible(*mosaicoViewport_);

    fichaPanel_ = std::make_unique<FichaPanelComponent>(projeto_);
    fichaPanel_->aoMudar = [this] { recarregar(); };
    fichaPanel_->aoAplicarEmLote = [this] { recarregar(); };
    fichaPanel_->aoAplicarSucesso = [this](const std::string& itemId) {
        if (mosaico_) mosaico_->atualizarItemEmMemoria(itemId);
    };
    addAndMakeVisible(*fichaPanel_);

    campoBusca_ = std::make_unique<juce::TextEditor>();
    campoBusca_->setTextToShowWhenEmpty(matriz::i18n::t("barra.buscar"), tema().textoTerciario);
    campoBusca_->setColour(juce::TextEditor::backgroundColourId, tema().painelAlt);
    campoBusca_->setColour(juce::TextEditor::textColourId, tema().textoPrimario);
    campoBusca_->setColour(juce::TextEditor::outlineColourId, tema().borda);
    campoBusca_->onTextChange = [this] {
        if (mosaico_) mosaico_->definirBusca(campoBusca_->getText());
        if (btnLimparBusca_) btnLimparBusca_->setVisible(campoBusca_->getText().isNotEmpty());
    };
    campoBusca_->setTooltip("Search assets by title, metadata fields, or tags");
    addAndMakeVisible(*campoBusca_);

    btnLimparBusca_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xc3\x97"));
    btnLimparBusca_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    btnLimparBusca_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnLimparBusca_->onClick = [this] {
        campoBusca_->setText("", true);
        btnLimparBusca_->setVisible(false);
    };
    btnLimparBusca_->setTooltip("Clear search term");
    // addChildComponent, não addAndMakeVisible: o segundo força visible=true e
    // desfaz o estado inicial escondido.
    addChildComponent(*btnLimparBusca_);

    editMode_ = true;

    sliderTamanho_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    sliderTamanho_->setRange(0.0, 1.0, 0.01);
    sliderTamanho_->setValue(0.37, juce::dontSendNotification);
    sliderTamanho_->setColour(juce::Slider::trackColourId, tema().borda);
    sliderTamanho_->setColour(juce::Slider::thumbColourId, tema().acento);
    sliderTamanho_->setColour(juce::Slider::backgroundColourId, tema().painelAlt);
    sliderTamanho_->onValueChange = [this] {
        if (mosaico_) mosaico_->definirTamanhoContinuo(sliderTamanho_->getValue());
    };
    sliderTamanho_->setTooltip("Adjust thumbnail display size");
    addAndMakeVisible(*sliderTamanho_);

    lblTamanho_ = std::make_unique<juce::Label>("", "SIZE");
    lblTamanho_->setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    lblTamanho_->setColour(juce::Label::textColourId, tema().textoTerciario);
    lblTamanho_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*lblTamanho_);

    btnVisaoGrade_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x96\xa6\xe2\x96\xa6"));
    btnVisaoGrade_->setColour(juce::TextButton::buttonColourId, tema().acento);
    btnVisaoGrade_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnVisaoGrade_->setTooltip("Grid view");
    btnVisaoGrade_->onClick = [this] {
        if (mosaico_) mosaico_->definirModoVisao(MosaicoComponent::ModoVisao::Grade);
        btnVisaoGrade_->setColour(juce::TextButton::buttonColourId, tema().acento);
        btnVisaoLista_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    };
    addAndMakeVisible(*btnVisaoGrade_);

    btnVisaoLista_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x89\xa1"));
    btnVisaoLista_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnVisaoLista_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnVisaoLista_->setTooltip("List view");
    btnVisaoLista_->onClick = [this] {
        if (mosaico_) mosaico_->definirModoVisao(MosaicoComponent::ModoVisao::Lista);
        btnVisaoLista_->setColour(juce::TextButton::buttonColourId, tema().acento);
        btnVisaoGrade_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    };
    addAndMakeVisible(*btnVisaoLista_);

    btnSelecionarTodos_ = std::make_unique<juce::TextButton>("Select All");
    btnSelecionarTodos_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnSelecionarTodos_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnSelecionarTodos_->onClick = [this] { if (mosaico_) mosaico_->selecionarTodos(); };
    btnSelecionarTodos_->setTooltip("Select all items currently showing in the grid");
    addAndMakeVisible(*btnSelecionarTodos_);

    btnLimparSelecao_ = std::make_unique<juce::TextButton>("Deselect");
    btnLimparSelecao_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnLimparSelecao_->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
    btnLimparSelecao_->onClick = [this] { if (mosaico_) mosaico_->limparSelecao(); };
    btnLimparSelecao_->setTooltip("Clear current grid selection");
    addAndMakeVisible(*btnLimparSelecao_);

    painelDuplicatas_ = std::make_unique<PainelDuplicatasComponent>(projeto_);
    addAndMakeVisible(*painelDuplicatas_);
    painelDuplicatas_->setVisible(false);

    lblCaminhoNavegacao_ = std::make_unique<juce::Label>("", "");
    lblCaminhoNavegacao_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblCaminhoNavegacao_->setColour(juce::Label::textColourId, tema().textoSecundario);
    lblCaminhoNavegacao_->setColour(juce::Label::backgroundColourId, tema().painelAlt);
    addChildComponent(*lblCaminhoNavegacao_);

    construirSidebar();
    mosaico_->recarregar();
    startTimer(60000);

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    ProjetoAberto* proj = &projeto_;
    poolMiniaturas_.addJob([safeThis, proj]() {
        DBG("MatrizMiniGen: iniciando geração de miniaturas faltantes");
        proj->gerarMiniaturasFaltantes();
        DBG("MatrizMiniGen: geração concluída, recarregando grade");
        juce::MessageManager::callAsync([safeThis]() {
            if (!safeThis) return;
            safeThis->recarregar();
        });
    });
}

CatalogWorkspaceComponent::~CatalogWorkspaceComponent() {
    stopTimer();
    poolContagens_.removeAllJobs(true, 1000);
    poolMiniaturas_.removeAllJobs(true, 30000);
}

void CatalogWorkspaceComponent::timerCallback() {
    atualizarContagens();
}

void CatalogWorkspaceComponent::construirSidebar() {
    categorias_.clear();
    botoesCategorias_.clear();
    secoesSidebar_.clear();
    anosDisponiveis_.clear();
    botoesAnos_.clear();
    collectionDisponiveis_.clear();
    botoesCollection_.clear();

    secoesSidebar_.push_back({static_cast<int>(categorias_.size()), "LIBRARY"});
    categorias_.push_back({matriz::i18n::t("catwork.all"), "all", 0});
    categorias_.push_back({matriz::i18n::t("catwork.selected"), "selected", 0});
    categorias_.push_back({"Folders", "folders", 0});
    categorias_.push_back({"Duplicates", "duplicates", 0});

    indiceInicioMediaType_ = static_cast<int>(categorias_.size());
    secoesSidebar_.push_back({indiceInicioMediaType_, "MEDIA TYPE"});
    categorias_.push_back({matriz::i18n::t("catwork.audio"), "audio", 0});
    categorias_.push_back({matriz::i18n::t("catwork.video"), "video", 0});
    categorias_.push_back({matriz::i18n::t("catwork.images"), "images", 0});
    categorias_.push_back({matriz::i18n::t("catwork.documents"), "documents", 0});
    categorias_.push_back({matriz::i18n::t("catwork.sessions"), "sessions", 0});

    indiceInicioStatus_ = static_cast<int>(categorias_.size());
    secoesSidebar_.push_back({indiceInicioStatus_, "STATUS"});
    categorias_.push_back({matriz::i18n::t("catwork.needs_review"), "revisao", 0});
    categorias_.push_back({matriz::i18n::t("catwork.no_backup"), "vulneraveis", 0});
    categorias_.push_back({matriz::i18n::t("catwork.single_copy"), "single_copy", 0});
    categorias_.push_back({matriz::i18n::t("catwork.offline"), "ausentes", 0});

    for (size_t i = 0; i < categorias_.size(); ++i) {
        auto btn = std::make_unique<juce::TextButton>(categorias_[i].rotulo);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btn->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
        btn->onClick = [this, i] { selecionarCategoria(static_cast<int>(i)); };
        addAndMakeVisible(*btn);
        botoesCategorias_.push_back(std::move(btn));
    }

    construirFiltroAnos();
    construirFiltroCollection();
    atualizarContagens();
    selecionarCategoria(0);
}

void CatalogWorkspaceComponent::construirFiltroAnos() {
    for (auto& b : botoesAnos_) removeChildComponent(b.get());
    botoesAnos_.clear();
    anosDisponiveis_.clear();

    auto itens = projeto_.listarItens();
    std::map<int, int> contagemPorAno;
    int semAno = 0;
    for (const auto& item : itens) {
        if (item.ano.has_value()) contagemPorAno[*item.ano]++;
        else semAno++;
    }

    for (auto it = contagemPorAno.rbegin(); it != contagemPorAno.rend(); ++it)
        anosDisponiveis_.push_back({it->first, it->second});
    if (semAno > 0) anosDisponiveis_.push_back({-1, semAno});

    for (size_t i = 0; i < anosDisponiveis_.size(); ++i) {
        auto& [ano, contagem] = anosDisponiveis_[i];
        juce::String label = (ano == -1) ? "Unknown" : juce::String(ano);
        label += " (" + juce::String(contagem) + ")";
        auto btn = std::make_unique<juce::TextButton>(label);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btn->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
        btn->onClick = [this, i] {
            int anoSel = anosDisponiveis_[i].first;
            if (anoSelecionado_.has_value() && *anoSelecionado_ == anoSel) {
                anoSelecionado_ = std::nullopt;
            } else {
                anoSelecionado_ = anoSel;
            }
            aplicarFiltroAno();
        };
        addAndMakeVisible(*btn);
        botoesAnos_.push_back(std::move(btn));
    }
}

void CatalogWorkspaceComponent::aplicarFiltroAno() {
    aplicarFiltrosAdicionais();
}

void CatalogWorkspaceComponent::aplicarFiltrosAdicionais() {
    for (size_t i = 0; i < botoesAnos_.size(); ++i) {
        bool ativo = anoSelecionado_.has_value() && anosDisponiveis_[i].first == *anoSelecionado_;
        botoesAnos_[i]->setColour(juce::TextButton::buttonColourId,
            ativo ? tema().acento.withAlpha(0.2f) : juce::Colours::transparentBlack);
        botoesAnos_[i]->setColour(juce::TextButton::textColourOffId,
            ativo ? tema().textoPrimario : tema().textoSecundario);
    }

    for (size_t i = 0; i < botoesCollection_.size(); ++i) {
        bool ativo = collectionSelecionado_.has_value() && collectionDisponiveis_[i].first == *collectionSelecionado_;
        botoesCollection_[i]->setColour(juce::TextButton::buttonColourId,
            ativo ? tema().acento.withAlpha(0.2f) : juce::Colours::transparentBlack);
        botoesCollection_[i]->setColour(juce::TextButton::textColourOffId,
            ativo ? tema().textoPrimario : tema().textoSecundario);
    }

    if (!mosaico_) return;

    const auto& libChave = (categoriaSelecionada_ >= 0 && categoriaSelecionada_ < static_cast<int>(categorias_.size()))
        ? categorias_[static_cast<size_t>(categoriaSelecionada_)].chave : std::string();
    if (libChave == "folders") return;

    auto itens = projeto_.listarItens();
    std::set<std::string> filteredIds;
    bool anyFilter = false;

    std::set<std::string> selectedIds;
    if (libChave == "selected") {
        anyFilter = true;
        selectedIds = mosaico_ ? mosaico_->itensSelecionados() : std::set<std::string>{};
    }

    std::set<std::string> statusIds;
    if (statusSelecionado_.has_value()) {
        auto ids = projeto_.itensDaColecaoEmbutida(*statusSelecionado_);
        statusIds.insert(ids.begin(), ids.end());
    }

    for (const auto& item : itens) {
        if (libChave == "selected") {
            if (selectedIds.find(item.id) == selectedIds.end()) continue;
        }

        if (tipoMidiaSelecionado_.has_value()) {
            anyFilter = true;
            auto ext = juce::String(item.extensaoArquivo).toLowerCase();
            auto cat = matriz::ingest::categoriaPorExtensao(ext);
            bool match = false;
            if (*tipoMidiaSelecionado_ == "audio") match = (cat == matriz::ingest::CategoriaMidia::Audio);
            else if (*tipoMidiaSelecionado_ == "video") match = (cat == matriz::ingest::CategoriaMidia::Video);
            else if (*tipoMidiaSelecionado_ == "images") match = (cat == matriz::ingest::CategoriaMidia::Imagem);
            else if (*tipoMidiaSelecionado_ == "documents") match = (cat == matriz::ingest::CategoriaMidia::Documento || cat == matriz::ingest::CategoriaMidia::Texto);
            else if (*tipoMidiaSelecionado_ == "sessions") match = (cat == matriz::ingest::CategoriaMidia::Sessao);
            if (!match) continue;
        }

        if (statusSelecionado_.has_value()) {
            anyFilter = true;
            if (statusIds.find(item.id) == statusIds.end()) continue;
        }

        if (anoSelecionado_.has_value()) {
            anyFilter = true;
            if (*anoSelecionado_ == -1) {
                if (item.ano.has_value()) continue;
            } else {
                if (!item.ano.has_value() || *item.ano != *anoSelecionado_) continue;
            }
        }

        if (collectionSelecionado_.has_value()) {
            anyFilter = true;
            if (*collectionSelecionado_ == "Unknown") {
                if (item.collectionType.has_value() && !item.collectionType->empty()) continue;
            } else {
                if (!item.collectionType.has_value() || *item.collectionType != *collectionSelecionado_) continue;
            }
        }

        filteredIds.insert(item.id);
    }

    if (anyFilter) {
        mosaico_->definirFiltroItens(std::move(filteredIds));
    } else {
        mosaico_->definirFiltroItens(std::nullopt);
    }

    mosaico_->recarregar();
}

void CatalogWorkspaceComponent::construirFiltroCollection() {
    for (auto& b : botoesCollection_) removeChildComponent(b.get());
    botoesCollection_.clear();
    collectionDisponiveis_.clear();

    auto itens = projeto_.listarItens();
    std::map<std::string, int> contagemPorCollection;
    int semCollection = 0;
    for (const auto& item : itens) {
        if (item.collectionType.has_value() && !item.collectionType->empty())
            contagemPorCollection[*item.collectionType]++;
        else
            semCollection++;
    }

    for (const auto& pair : contagemPorCollection)
        collectionDisponiveis_.push_back(pair);
    if (semCollection > 0)
        collectionDisponiveis_.push_back({"Unknown", semCollection});

    for (size_t i = 0; i < collectionDisponiveis_.size(); ++i) {
        auto& [collection, contagem] = collectionDisponiveis_[i];
        juce::String label = (collection == "Unknown") ? "Unknown" : juce::String(collection);
        label += " (" + juce::String(contagem) + ")";
        auto btn = std::make_unique<juce::TextButton>(label);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btn->setColour(juce::TextButton::textColourOffId, tema().textoSecundario);
        btn->onClick = [this, collection = collection] {
            if (collectionSelecionado_.has_value() && *collectionSelecionado_ == collection) {
                collectionSelecionado_ = std::nullopt;
            } else {
                collectionSelecionado_ = collection;
            }
            aplicarFiltrosAdicionais();
        };
        addAndMakeVisible(*btn);
        botoesCollection_.push_back(std::move(btn));
    }
}


void CatalogWorkspaceComponent::atualizarContagens() {
    poolContagens_.removeAllJobs(true, 100);

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    ProjetoAberto* proj = &projeto_;

    poolContagens_.addJob([safeThis, proj]() {
        ContagensResultado res;
        try {
            auto itens = proj->listarItens();
            res.total = static_cast<int>(itens.size());

            for (const auto& item : itens) {
                auto ext = juce::String(item.extensaoArquivo).toLowerCase();
                auto cat = matriz::ingest::categoriaPorExtensao(ext);
                switch (cat) {
                    case matriz::ingest::CategoriaMidia::Audio: ++res.audio; break;
                    case matriz::ingest::CategoriaMidia::Video: ++res.video; break;
                    case matriz::ingest::CategoriaMidia::Imagem: ++res.img; break;
                    case matriz::ingest::CategoriaMidia::Sessao: ++res.sessao; break;
                    case matriz::ingest::CategoriaMidia::Documento: ++res.doc; break;
                    case matriz::ingest::CategoriaMidia::Texto: ++res.doc; break;
                    default: break;
                }
            }

            auto colecoes = proj->listarColecoesEmbutidas();
            for (const auto& c : colecoes) {
                if (c.chave == "revisao") res.revisao = c.contagem;
                else if (c.chave == "vulneraveis") res.vulneraveis = c.contagem;
                else if (c.chave == "single_copy") res.single_copy = c.contagem;
                else if (c.chave == "ausentes") res.ausentes = c.contagem;
            }

            for (const auto& g : proj->listarGruposDuplicados()) {
                res.duplicatesCount += static_cast<int>(g.itens.size());
            }
        } catch (...) {
            return;
        }

        juce::MessageManager::callAsync([safeThis, res]() {
            if (safeThis) {
                safeThis->aplicarContagens(res);
            }
        });
    });
}

void CatalogWorkspaceComponent::aplicarContagens(const ContagensResultado& res) {
    auto definirContagem = [&](int idx, int count) {
        if (idx < static_cast<int>(categorias_.size())) {
            categorias_[static_cast<size_t>(idx)].contagem = count;
            if (idx < static_cast<int>(botoesCategorias_.size())) {
                juce::String label = categorias_[static_cast<size_t>(idx)].rotulo;
                if (count > 0) label += " (" + juce::String(count) + ")";
                botoesCategorias_[static_cast<size_t>(idx)]->setButtonText(label);
            }
        }
    };

    int selCount = mosaico_ ? static_cast<int>(mosaico_->itensSelecionados().size()) : 0;

    definirContagem(0, res.total);
    definirContagem(1, selCount);
    definirContagem(2, res.total);        // Folders
    definirContagem(3, res.duplicatesCount);
    definirContagem(4, res.audio);
    definirContagem(5, res.video);
    definirContagem(6, res.img);
    definirContagem(7, res.doc);
    definirContagem(8, res.sessao);
    definirContagem(9, res.revisao);
    definirContagem(10, res.vulneraveis);
    definirContagem(11, res.single_copy);
    definirContagem(12, res.ausentes);
}

void CatalogWorkspaceComponent::selecionarCategoria(int indice) {
    const auto& chave = categorias_[static_cast<size_t>(indice)].chave;

    if (chave == "duplicates") {
        categoriaSelecionada_ = 0;
        statusSelecionado_ = std::nullopt;
        if (mosaicoViewport_) mosaicoViewport_->setVisible(false);
        if (painelDuplicatas_) {
            painelDuplicatas_->setVisible(true);
            painelDuplicatas_->recarregar();
        }
        atualizarBotoesSidebar();
        return;
    }

    if (indice < indiceInicioMediaType_) {
        categoriaSelecionada_ = indice;
    } else if (indice < indiceInicioStatus_) {
        if (tipoMidiaSelecionado_.has_value() && *tipoMidiaSelecionado_ == chave)
            tipoMidiaSelecionado_ = std::nullopt;
        else
            tipoMidiaSelecionado_ = chave;
    } else {
        if (statusSelecionado_.has_value() && *statusSelecionado_ == chave)
            statusSelecionado_ = std::nullopt;
        else
            statusSelecionado_ = chave;
    }

    atualizarBotoesSidebar();

    if (!mosaico_) return;

    if (painelDuplicatas_) painelDuplicatas_->setVisible(false);
    if (mosaicoViewport_) mosaicoViewport_->setVisible(true);
    mosaico_->definirSubpastas({});
    pastaNavegarAtual_ = std::nullopt;
    caminhoNavegacao_.clear();

    const auto& libChave = categorias_[static_cast<size_t>(categoriaSelecionada_)].chave;
    if (libChave == "folders") {
        navegarParaPastaOrigem(std::nullopt);
        return;
    }

    aplicarFiltrosAdicionais();
}

void CatalogWorkspaceComponent::atualizarBotoesSidebar() {
    for (size_t i = 0; i < botoesCategorias_.size(); ++i) {
        int idx = static_cast<int>(i);
        const auto& ch = categorias_[i].chave;
        bool ativo = false;
        if (idx < indiceInicioMediaType_)
            ativo = (idx == categoriaSelecionada_);
        else if (idx < indiceInicioStatus_)
            ativo = (tipoMidiaSelecionado_.has_value() && *tipoMidiaSelecionado_ == ch);
        else
            ativo = (statusSelecionado_.has_value() && *statusSelecionado_ == ch);
        botoesCategorias_[i]->setColour(juce::TextButton::buttonColourId,
            ativo ? tema().acento.withAlpha(0.2f) : juce::Colours::transparentBlack);
        botoesCategorias_[i]->setColour(juce::TextButton::textColourOffId,
            ativo ? tema().textoPrimario : tema().textoSecundario);
    }
}

void CatalogWorkspaceComponent::navegarParaPastaOrigem(std::optional<std::string> nomePasta) {
    auto raiz = projeto_.arvoreOrigem(true);

    const auto& libChave = categorias_.empty()
        ? std::string()
        : categorias_[static_cast<size_t>(categoriaSelecionada_)].chave;

    if (!nomePasta.has_value()) {
        caminhoNavegacao_.clear();
    } else {
        auto it = std::find(caminhoNavegacao_.begin(), caminhoNavegacao_.end(), *nomePasta);
        if (it != caminhoNavegacao_.end()) {
            caminhoNavegacao_.erase(it + 1, caminhoNavegacao_.end());
        } else {
            caminhoNavegacao_.push_back(*nomePasta);
        }
    }

    const ProjetoAberto::NoArvore* alvo = &raiz;
    for (auto& segmento : caminhoNavegacao_) {
        bool encontrado = false;
        for (auto& f : alvo->filhos) {
            if (f.nome.toStdString() == segmento) {
                alvo = &f;
                encontrado = true;
                break;
            }
        }
        if (!encontrado) break;
    }

    pastaNavegarAtual_ = nomePasta;

    if (lblCaminhoNavegacao_) {
        if (caminhoNavegacao_.empty()) {
            lblCaminhoNavegacao_->setVisible(false);
        } else {
            juce::String caminho = "  /";
            for (auto& seg : caminhoNavegacao_)
                caminho += " " + juce::String(seg) + " /";
            caminho = caminho.dropLastCharacters(2);
            lblCaminhoNavegacao_->setText(caminho, juce::dontSendNotification);
            lblCaminhoNavegacao_->setVisible(true);
        }
    }

    std::vector<SubpastaInfo> subpastas;
    if (!caminhoNavegacao_.empty()) {
        SubpastaInfo voltar;
        voltar.nome = juce::CharPointer_UTF8("\xe2\x86\x90 Back");
        voltar.quantidade = 0;
        voltar.itemIds = {};
        subpastas.push_back(voltar);
    }
    for (auto& filho : alvo->filhos) {
        if (filho.filhos.empty() && filho.itemIds.empty()) continue;
        SubpastaInfo sub;
        sub.nome = filho.nome;
        sub.quantidade = static_cast<int>(filho.itemIds.size());
        sub.itemIds = filho.itemIds;
        subpastas.push_back(sub);
    }

    mosaico_->definirSubpastas(subpastas);
    mosaico_->aoNavegarParaSubpasta = [this](const SubpastaInfo& sub) {
        if (sub.nome.startsWith(juce::CharPointer_UTF8("\xe2\x86\x90"))) {
            if (caminhoNavegacao_.size() <= 1)
                navegarParaPastaOrigem(std::nullopt);
            else
                navegarParaPastaOrigem(caminhoNavegacao_[caminhoNavegacao_.size() - 2]);
        } else {
            navegarParaPastaOrigem(sub.nome.toStdString());
        }
    };

    mosaico_->definirFiltroItens(alvo->itemIdsDiretos);
    mosaico_->recarregar();
    resized();
}

void CatalogWorkspaceComponent::revalidarPastaAtual() {
    if (caminhoNavegacao_.empty()) {
        navegarParaPastaOrigem(std::nullopt);
    } else {
        auto ultimo = caminhoNavegacao_.back();
        navegarParaPastaOrigem(ultimo);
    }
}

void CatalogWorkspaceComponent::selecionarItem(const std::string& itemId) {
    if (fichaPanel_) {
        auto sel = mosaico_ ? mosaico_->itensSelecionados() : std::set<std::string>{};
        fichaPanel_->mostrarSelecao(std::vector<std::string>(sel.begin(), sel.end()));
    }
}

void CatalogWorkspaceComponent::abrirWorkbench(const std::string& itemId) {
    activePreviewWindow_.reset();

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    auto aoFechar = [safeThis]() {
        juce::MessageManager::callAsync([safeThis]() {
            if (!safeThis) return;
            safeThis->activePreviewWindow_.reset();
            safeThis->recarregar();
        });
    };

    activePreviewWindow_ = std::make_unique<FloatingPreviewWindow>(projeto_, itemId, aoFechar);
}

void CatalogWorkspaceComponent::fecharWorkbench() {
    activePreviewWindow_.reset();
    recarregar();
}

void CatalogWorkspaceComponent::abrirRelinkOffline(const std::string& itemId) {
    std::string titulo, tipoMidia, codigoAcervo;
    projeto_.obterItemInfo(itemId, titulo, tipoMidia, codigoAcervo);

    juce::String expectedPath;
    juce::String storageName = "Local Storage";

    try {
        auto stmt = projeto_.projeto().registro().prepare(
            "SELECT a.caminho_relativo, COALESCE(a.caminho_absoluto_origem, ''), COALESCE(v.nome, 'Local Storage'), COALESCE(v.localizacao, '') "
            "FROM arquivo a "
            "LEFT JOIN vault v ON v.id = a.vault_id "
            "WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) {
            std::string camRel = stmt.columnText(0);
            std::string camAbs = stmt.columnText(1);
            storageName = stmt.columnText(2);
            std::string locVault = stmt.columnText(3);
            auto expFile = matriz::vault::caminhoEsperado(projeto_.projeto().pasta(), locVault, camRel, camAbs);
            expectedPath = expFile != juce::File() ? expFile.getFullPathName() : (camAbs.empty() ? camRel : camAbs);
        }
    } catch (...) {}

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    OfflineAssetRelinkDialog::showModal(
        projeto_.projeto().registro(),
        itemId,
        juce::String(titulo),
        expectedPath,
        storageName,
        [safeThis, itemId](const juce::File& fileSelected) {
            if (!safeThis) return;
            try {
                auto stmt = safeThis->projeto_.projeto().registro().prepare(
                    "SELECT a.id FROM arquivo a WHERE a.item_id = ? AND a.eh_master = 1 LIMIT 1");
                stmt.bind(1, matriz::db::Value::of(itemId));
                if (stmt.step()) {
                    std::string masterArqId = stmt.columnText(0);
                    safeThis->projeto_.aplicarRelinkEmMemoria(masterArqId, fileSelected.getFullPathName().toStdString());
                    if (safeThis->mosaico_) safeThis->mosaico_->recarregar();
                }
            } catch (...) {}
        });
}

void CatalogWorkspaceComponent::abrirMenuContexto(std::vector<std::string> itemIds) {
    if (itemIds.empty()) return;

    juce::Component::SafePointer<CatalogWorkspaceComponent> safeThis(this);
    acoes::Ganchos ganchos;
    ganchos.aoMudarDados = [safeThis] {
        if (safeThis) safeThis->recarregar();
    };
    ganchos.aoFiltrarItens = [safeThis](std::set<std::string> ids) {
        if (safeThis) safeThis->filtrarPorIds(std::move(ids));
    };

    auto menu = acoes::construirMenu(projeto_, itemIds);

    if (itemIds.size() > 1) {
        menu.addSeparator();
        menu.addItem(500, "Group to Folder");
    }

    juce::PopupMenu subMenuPastas;
    auto arvore = projeto_.arvoreAcervo();
    std::vector<std::pair<std::string, juce::String>> pastas;

    std::function<void(const ProjetoAberto::NoArvore&)> coletarPastas = [&](const ProjetoAberto::NoArvore& n) {
        if (!n.id.empty()) {
            pastas.push_back({n.id, n.nome});
        }
        for (const auto& f : n.filhos) coletarPastas(f);
    };
    coletarPastas(arvore);

    int pastaIdx = 1000;
    for (const auto& p : pastas) {
        subMenuPastas.addItem(pastaIdx++, p.second);
    }
    menu.addSubMenu("Move to Folder", subMenuPastas);

    ProjetoAberto* p = &projeto_;
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, p, itemIds, ganchos, pastas](int resultado) {
        if (!safeThis) return;
        if (resultado == 500) {
            std::string newFolderId = p->agruparItensEmNovaPasta(itemIds);
            if (safeThis->aoAgruparEIrParaTree && !newFolderId.empty()) {
                safeThis->aoAgruparEIrParaTree(newFolderId);
            }
            return;
        }
        if (resultado >= 1000 && resultado < 1000 + static_cast<int>(pastas.size())) {
            std::string pastaId = pastas[static_cast<size_t>(resultado - 1000)].first;
            p->adicionarItensAPasta(itemIds, pastaId);
            safeThis->recarregar();
            return;
        }
        acoes::executar(resultado, *p, itemIds, ganchos);
    });
}

void CatalogWorkspaceComponent::recarregar() {
    atualizarContagens();
    construirFiltroAnos();
    construirFiltroCollection();
    atualizarBotoesSidebar();

    const auto& libChave = categorias_.empty()
        ? std::string()
        : categorias_[static_cast<size_t>(categoriaSelecionada_)].chave;
    if (libChave == "folders") {
        revalidarPastaAtual();
    } else {
        aplicarFiltrosAdicionais();
    }

    resized();
    if (mosaico_) mosaico_->recarregar();
    if (painelDuplicatas_ && painelDuplicatas_->isVisible()) painelDuplicatas_->recarregar();
}


void CatalogWorkspaceComponent::filtrarPorChave(const std::string& chave) {
    for (size_t i = 0; i < categorias_.size(); ++i) {
        if (categorias_[i].chave == chave) {
            selecionarCategoria(static_cast<int>(i));
            return;
        }
    }
    // Keys from PreservationWorkspace that aren't sidebar categories —
    // apply them as embedded-collection filters directly.
    if (mosaico_) {
        auto ids = projeto_.itensDaColecaoEmbutida(chave);
        mosaico_->definirFiltroItens(std::move(ids));
        mosaico_->recarregar();
    }
}

void CatalogWorkspaceComponent::filtrarPorIds(std::set<std::string> ids) {
    if (mosaico_) {
        mosaico_->definirFiltroItens(std::move(ids));
        mosaico_->recarregar();
    }
}

std::set<std::string> CatalogWorkspaceComponent::itensSelecionados() const {
    if (mosaico_) return mosaico_->itensSelecionados();
    return {};
}

void CatalogWorkspaceComponent::renomearSelecionados() {
    if (mosaico_) mosaico_->renomearSelecao();
}

void CatalogWorkspaceComponent::removerSelecionadosDoBackup() {
    if (mosaico_) mosaico_->removerSelecaoDoBackup();
}

void CatalogWorkspaceComponent::selecionarFiltroSelecionados() {
    for (size_t i = 0; i < categorias_.size(); ++i) {
        if (categorias_[i].chave == "selected") {
            selecionarCategoria(static_cast<int>(i));
            break;
        }
    }
}

void CatalogWorkspaceComponent::selecionarFiltroTodos() {
    for (size_t i = 0; i < categorias_.size(); ++i) {
        if (categorias_[i].chave == "all") {
            selecionarCategoria(static_cast<int>(i));
            break;
        }
    }
}

void CatalogWorkspaceComponent::definirSelecaoItens(const std::set<std::string>& itemIds) {
    categoriaSelecionada_ = 0;
    tipoMidiaSelecionado_ = std::nullopt;
    anoSelecionado_ = std::nullopt;
    collectionSelecionado_ = std::nullopt;
    statusSelecionado_ = std::nullopt;
    if (mosaico_) {
        mosaico_->definirFiltroItens(itemIds);
        mosaico_->definirSelecao(itemIds);
    }
    repaint();
}

void CatalogWorkspaceComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);

    auto sidebar = getLocalBounds().removeFromLeft(kLarguraSidebar);
    g.setColour(tema().painel);
    g.fillRect(sidebar);
    g.setColour(tema().borda);
    g.fillRect(sidebar.getRight() - 1, sidebar.getY(), 1, sidebar.getHeight());

    for (auto& [titulo, bounds] : secaoHeaderBounds_) {
        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(titulo, bounds, juce::Justification::centredLeft);
    }

    for (size_t i = 0; i < categorias_.size(); ++i) {
        juce::Colour cor;
        const auto& ch = categorias_[i].chave;
        if      (ch == "audio")     cor = juce::Colour(0xff2a9d8f);
        else if (ch == "video")     cor = juce::Colour(0xff9d4edd);
        else if (ch == "images")    cor = juce::Colour(0xfff4a261);
        else if (ch == "documents") cor = juce::Colour(0xff2b9348);
        else if (ch == "sessions")  cor = juce::Colour(0xff2b9348);
        else continue;
        auto btnBounds = botoesCategorias_[i]->getBounds();
        float cy = btnBounds.getCentreY() - 4.0f;
        float cx = btnBounds.getX() + 2.0f;
        g.setColour(cor);
        g.fillEllipse(cx, cy, 8.0f, 8.0f);
    }
}

void CatalogWorkspaceComponent::resized() {
    secaoHeaderBounds_.clear();
    auto area = getLocalBounds();

    auto sidebar = area.removeFromLeft(kLarguraSidebar);
    sidebar.removeFromTop(8);

    auto buscaArea = sidebar.removeFromTop(28).reduced(8, 2);
    if (btnLimparBusca_) {
        auto clearArea = buscaArea.removeFromRight(24);
        btnLimparBusca_->setBounds(clearArea);
    }
    campoBusca_->setBounds(buscaArea);
    sidebar.removeFromTop(4);

    sidebar.removeFromTop(4);

    if (btnSelecionarTodos_ && btnLimparSelecao_) {
        auto selRow = sidebar.removeFromBottom(30).reduced(8, 2);
        int halfW = selRow.getWidth() / 2;
        btnSelecionarTodos_->setBounds(selRow.removeFromLeft(halfW - 2));
        selRow.removeFromLeft(4);
        btnLimparSelecao_->setBounds(selRow);
    }

    for (size_t i = 0; i < botoesCategorias_.size(); ++i) {
        for (auto& sec : secoesSidebar_) {
            if (sec.indicePrimeiro == static_cast<int>(i)) {
                secaoHeaderBounds_.push_back({sec.titulo, sidebar.removeFromTop(20).reduced(8, 0)});
                sidebar.removeFromTop(2);
            }
        }
        auto btnArea = sidebar.removeFromTop(24).reduced(4, 0);
        const auto& ch = categorias_[i].chave;
        if (ch == "audio" || ch == "video" || ch == "images" || ch == "documents" || ch == "sessions")
            btnArea.removeFromLeft(14);
        botoesCategorias_[i]->setBounds(btnArea);
    }

    if (!botoesAnos_.empty()) {
        secaoHeaderBounds_.push_back({"DATE", sidebar.removeFromTop(20).reduced(8, 0)});
        sidebar.removeFromTop(2);
        for (size_t i = 0; i < botoesAnos_.size(); i += 2) {
            auto rowArea = sidebar.removeFromTop(22).reduced(4, 0);
            int halfW = rowArea.getWidth() / 2;
            botoesAnos_[i]->setBounds(rowArea.removeFromLeft(halfW).reduced(2, 0));
            if (i + 1 < botoesAnos_.size()) {
                botoesAnos_[i + 1]->setBounds(rowArea.reduced(2, 0));
            }
        }
    }

    if (!botoesCollection_.empty()) {
        secaoHeaderBounds_.push_back({"CONTENT", sidebar.removeFromTop(20).reduced(8, 0)});
        sidebar.removeFromTop(2);
        for (auto& btn : botoesCollection_)
            btn->setBounds(sidebar.removeFromTop(22).reduced(4, 0));
    }

    auto fichaArea = area.removeFromRight(kLarguraFicha);
    if (fichaPanel_) fichaPanel_->setBounds(fichaArea);

    if (lblCaminhoNavegacao_ && lblCaminhoNavegacao_->isVisible()) {
        lblCaminhoNavegacao_->setBounds(area.removeFromTop(24));
    }

    auto toolbar = area.removeFromBottom(32);
    toolbar = toolbar.reduced(4, 2);

    if (btnVisaoGrade_ && btnVisaoLista_) {
        btnVisaoGrade_->setBounds(toolbar.removeFromLeft(28));
        toolbar.removeFromLeft(2);
        btnVisaoLista_->setBounds(toolbar.removeFromLeft(28));
        toolbar.removeFromLeft(8);
    }

    if (lblTamanho_) {
        lblTamanho_->setBounds(toolbar.removeFromLeft(30));
    }
    if (sliderTamanho_) {
        sliderTamanho_->setBounds(toolbar.removeFromLeft(120).reduced(0, 4));
    }

    if (mosaicoViewport_) mosaicoViewport_->setBounds(area);
    if (painelDuplicatas_) painelDuplicatas_->setBounds(area);
    if (mosaico_) {
        mosaico_->setSize(mosaicoViewport_->getWidth() - mosaicoViewport_->getScrollBarThickness(),
                          mosaico_->getHeight());
    }
}

} // namespace matriz::ui
