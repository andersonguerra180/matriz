#include "ArvoreBackupComponent.h"
#include "../I18n/Strings.h"
#include "../Ingest/FluxoLote.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Vault/Resolucao.h"
#include "ModalMitigacao.h"
#include "Tokens.h"
#include <algorithm>
#include <cmath>

namespace matriz::ui {

// ── Detail panel content ─────────────────────────────────────────────

namespace {

juce::String formatarTamanhoArquivo(juce::int64 bytes) {
    if (bytes <= 0) return "0 B";
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024) + " KB";
    if (bytes < 1024LL * 1024 * 1024)
        return juce::String(static_cast<double>(bytes) / (1024.0 * 1024.0), 1) + " MB";
    return juce::String(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

} // namespace

// FOLDER COLOR (item 12) — color picker padrão com color wheel; reporta
// cada mudança ao vivo via onChange, quem chama decide quando persistir.
//
// Fase 3: as três linhas RGB (showSliders) saíram pra abrir espaço pro
// histórico de cores usadas (10 slots, por projeto). onFechar é chamado só
// no destrutor (janela fechando) — e não a cada tick de onChange — porque
// arrastar no quadrado de cor/barra de matiz dispara onChange várias vezes
// por segundo; registrar cada tick encheria os 10 slots com variações
// quase idênticas de um único gesto de arrastar, em vez de guardar a cor
// que o usuário efetivamente escolheu.
class ColourPickerContent : public juce::Component, private juce::ChangeListener {
public:
    ColourPickerContent(juce::Colour corInicial, std::vector<juce::Colour> historico,
                         std::function<void(juce::Colour)> onChange,
                         std::function<void(juce::Colour)> onFechar)
        : onChange_(std::move(onChange)), onFechar_(std::move(onFechar)), historico_(std::move(historico)) {
        seletor_.setCurrentColour(corInicial, juce::dontSendNotification);
        seletor_.addChangeListener(this);
        addAndMakeVisible(seletor_);

        for (int i = 0; i < kSlots; ++i) {
            auto btn = std::make_unique<juce::TextButton>();
            bool temCor = i < static_cast<int>(historico_.size());
            btn->setColour(juce::TextButton::buttonColourId,
                           temCor ? historico_[static_cast<size_t>(i)] : tema().painelAlt);
            if (temCor) {
                btn->setTooltip(historico_[static_cast<size_t>(i)].toDisplayString(false));
                btn->onClick = [this, i] {
                    seletor_.setCurrentColour(historico_[static_cast<size_t>(i)], juce::sendNotification);
                };
            } else {
                btn->setEnabled(false);
            }
            addAndMakeVisible(*btn);
            swatches_.push_back(std::move(btn));
        }

        setSize(300, kAlturaSeletor + kEspaco + kAlturaHistorico + kEspaco);
    }

    ~ColourPickerContent() override {
        seletor_.removeChangeListener(this);
        if (onFechar_) onFechar_(seletor_.getCurrentColour());
    }

    void resized() override {
        auto area = getLocalBounds();
        seletor_.setBounds(area.removeFromTop(kAlturaSeletor));
        area.removeFromTop(kEspaco);
        auto linhaHistorico = area.removeFromTop(kAlturaHistorico);
        int colunas = kSlots / 2;
        int larguraSlot = (linhaHistorico.getWidth() - (colunas - 1) * kGapSlot) / colunas;
        int alturaSlot = (kAlturaHistorico - kGapSlot) / 2;
        for (int i = 0; i < kSlots; ++i) {
            int col = i % colunas;
            int lin = i / colunas;
            swatches_[static_cast<size_t>(i)]->setBounds(linhaHistorico.getX() + col * (larguraSlot + kGapSlot),
                                                           linhaHistorico.getY() + lin * (alturaSlot + kGapSlot),
                                                           larguraSlot, alturaSlot);
        }
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        if (onChange_) onChange_(seletor_.getCurrentColour());
    }

    static constexpr int kSlots = 10;
    static constexpr int kAlturaSeletor = 260;
    static constexpr int kAlturaHistorico = 64;
    static constexpr int kEspaco = 10;
    static constexpr int kGapSlot = 4;

    // showSliders removido (Fase 3) — hex, quadrado e barra de matiz
    // (showColourAtTop | showColourspace) ficam como estavam.
    juce::ColourSelector seletor_{juce::ColourSelector::showColourAtTop | juce::ColourSelector::showColourspace};
    std::function<void(juce::Colour)> onChange_;
    std::function<void(juce::Colour)> onFechar_;
    std::vector<juce::Colour> historico_;
    std::vector<std::unique_ptr<juce::TextButton>> swatches_;
};

class TreeDetailContent : public juce::Component {
public:
    struct SubfolderEntry {
        juce::String nome;
        int contagemItens = 0;
    };
    struct FileEntry {
        std::string id;
        juce::String nome;
        juce::String extensao;
        juce::int64 tamanhoBytes = 0;
    };

    TreeDetailContent() {
        btnShowInGrid = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.exibir_grade"));
        btnShowInGrid->setColour(juce::TextButton::buttonColourId, tema().acento);
        btnShowInGrid->setColour(juce::TextButton::textColourOffId, tema().textoSobreAcento);
        btnShowInGrid->onClick = [this] { if (aoMostrarNaGrade) aoMostrarNaGrade(); };
        addAndMakeVisible(*btnShowInGrid);
    }

    void lookAndFeelChanged() override {
        juce::Component::lookAndFeelChanged();
        if (btnShowInGrid)
            btnShowInGrid->setButtonText(i18n::t("arvore_backup.exibir_grade"));
        repaint();
    }

    juce::String folderName;
    std::vector<SubfolderEntry> subfolders;
    std::vector<FileEntry> files;
    std::function<void(const std::string&)> aoClicarArquivo;
    std::function<void()> aoMostrarNaGrade;
    std::unique_ptr<juce::TextButton> btnShowInGrid;
    int hoverFileIndex_ = -1;

    void resized() override {
        if (btnShowInGrid) {
            btnShowInGrid->setBounds(12, 38, getWidth() - 24, 26);
        }
    }

    void recalcularAltura() {
        int h = 84;
        if (!subfolders.empty()) h += 28 + static_cast<int>(subfolders.size()) * 24;
        if (!files.empty()) h += 28 + static_cast<int>(files.size()) * 24;
        h += 16;
        setSize(getWidth(), std::max(h, getParentHeight()));
    }

    int fileIndexAtY(int mouseY) const {
        int y = 8 + 24 + 4 + 36;
        if (!subfolders.empty()) y += 20 + static_cast<int>(subfolders.size()) * 24 + 4;
        if (files.empty()) return -1;
        y += 20;
        int idx = (mouseY - y) / 24;
        if (idx < 0 || idx >= static_cast<int>(files.size())) return -1;
        if (mouseY < y) return -1;
        return idx;
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int idx = fileIndexAtY(e.getPosition().y);
        if (idx >= 0 && idx < static_cast<int>(files.size()) && aoClicarArquivo)
            aoClicarArquivo(files[static_cast<size_t>(idx)].id);
    }

    void mouseMove(const juce::MouseEvent& e) override {
        int idx = fileIndexAtY(e.getPosition().y);
        if (idx != hoverFileIndex_) { hoverFileIndex_ = idx; repaint(); }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoverFileIndex_ != -1) { hoverFileIndex_ = -1; repaint(); }
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(tema().painel);
        g.setColour(tema().borda);
        g.drawLine(0.0f, 0.0f, 0.0f, static_cast<float>(getHeight()), 1.0f);

        auto area = getLocalBounds().reduced(12, 8);

        g.setColour(tema().textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(folderName, area.removeFromTop(24), juce::Justification::centredLeft, true);

        area.removeFromTop(36); // Space for btnShowInGrid

        g.setColour(tema().borda);
        g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
        area.removeFromTop(4);

        if (!subfolders.empty()) {
            g.setColour(tema().textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(i18n::t("arvore_backup.subpastas"), area.removeFromTop(20), juce::Justification::centredLeft);

            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            for (const auto& sf : subfolders) {
                auto row = area.removeFromTop(24);
                g.setColour(tema().textoPrimario);
                g.drawText(juce::String(juce::CharPointer_UTF8("\xf0\x9f\x93\x81 ")) + sf.nome,
                           row.removeFromLeft(row.getWidth() - 50),
                           juce::Justification::centredLeft, true);
                g.setColour(tema().textoTerciario);
                g.drawText(juce::String(sf.contagemItens) + " " + i18n::t("arvore_backup.items"),
                           row, juce::Justification::centredRight);
            }
            area.removeFromTop(4);
        }

        if (!files.empty()) {
            g.setColour(tema().textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(i18n::t("arvore_backup.arquivos"), area.removeFromTop(20), juce::Justification::centredLeft);

            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            for (const auto& f : files) {
                auto row = area.removeFromTop(24);
                g.setColour(tema().textoPrimario);
                auto nameArea = row.removeFromLeft(row.getWidth() - 60);
                juce::String displayName = f.nome.isNotEmpty() ? f.nome : ("file." + f.extensao);
                g.drawText(displayName, nameArea, juce::Justification::centredLeft, true);
                g.setColour(tema().textoTerciario);
                g.drawText(formatarTamanhoArquivo(f.tamanhoBytes), row, juce::Justification::centredRight);
            }
        }

        if (subfolders.empty() && files.empty()) {
            g.setColour(tema().textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(i18n::t("arvore_backup.pasta_vazia"), area, juce::Justification::centredLeft);
        }
    }
};

namespace {

void pedirTextoBackup(const juce::String& titulo, const juce::String& mensagem, const juce::String& valorInicial,
                      std::function<void(std::optional<juce::String>)> aoConcluir) {
    ModalTextoDialog::exibir(titulo, mensagem, valorInicial, aoConcluir);
}

} // namespace

ArvoreBackupComponent::ArvoreBackupComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {

    // Aba "All" (acervo inteiro) fixa na posição 0 — nunca fecha.
    AbaEstrutura abaTodos;
    abaTodos.titulo = "All";
    abas_.push_back(abaTodos);

    btnCriarPasta_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_criar_pasta"));
    btnCriarPasta_->onClick = [this] {
        juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
        pedirTextoBackup(i18n::t("arvore_backup.criar_pasta_titulo"), i18n::t("arvore_backup.criar_pasta_msg"), i18n::t("arvore_backup.criar_pasta_padrao"),
            [safeThis](std::optional<juce::String> nome) {
                if (!safeThis || !nome || nome->trim().isEmpty()) return;
                safeThis->criarNovaPasta(nome->trim().toStdString(), std::nullopt);
            });
    };
    addAndMakeVisible(*btnCriarPasta_);

    btnRenomearPasta_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_renomear"));
    btnRenomearPasta_->onClick = [this] {
        for (const auto& n : nodes_) {
            if (n.selecionado) {
                std::string pId = n.id;
                juce::String nomeAtual = n.nome;
                juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
                pedirTextoBackup(i18n::t("arvore_backup.renomear_pasta_titulo"), i18n::t("arvore_backup.renomear_pasta_msg"), nomeAtual,
                    [safeThis, pId](std::optional<juce::String> nome) {
                        if (!safeThis || !nome || nome->trim().isEmpty()) return;
                        safeThis->renomearPastaSelecionada(pId, nome->trim().toStdString());
                    });
                break;
            }
        }
    };
    addAndMakeVisible(*btnRenomearPasta_);

    btnApagarPasta_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_apagar"));
    btnApagarPasta_->onClick = [this] {
        for (const auto& n : nodes_) {
            if (n.selecionado) {
                apagarPastaSelecionada(n.id);
                break;
            }
        }
    };
    addAndMakeVisible(*btnApagarPasta_);

    btnImportarEstrutura_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_importar"));
    btnImportarEstrutura_->onClick = [this] {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle(i18n::t("arvore_backup.btn_importar"))
                .withMessage(i18n::t("arvore_backup.importar_msg"))
                .withButton(i18n::t("arvore_backup.importar_btn"))
                .withButton(i18n::t("dialogo.cancelar")),
            [this](int res) {
                if (res == 1) {
                    try {
                        salvarPresetAutoAntes();
                        projeto_.resetarEImportarEstruturaOrigem();
                    } catch (const std::exception& e) {
                        juce::AlertWindow::showAsync(
                            juce::MessageBoxOptions()
                                .withIconType(juce::MessageBoxIconType::WarningIcon)
                                .withTitle(i18n::t("dialogo.erro"))
                                .withMessage(juce::String(i18n::t("arvore_backup.falha_importar")) + e.what())
                                .withButton(i18n::t("dialogo.ok")),
                            nullptr);
                        return;
                    }
                    recarregar();
                }
            });
    };
    addAndMakeVisible(*btnImportarEstrutura_);

    btnAutoArranjar_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_auto_arranjar"));
    btnAutoArranjar_->onClick = [this] { autoArranjar(); };
    addAndMakeVisible(*btnAutoArranjar_);

    btnZoomIn_ = std::make_unique<juce::TextButton>("+");
    btnZoomIn_->onClick = [this] { aplicarZoom(zoom_ * 1.2f, {getWidth() / 2.0f, getHeight() / 2.0f}); };
    addAndMakeVisible(*btnZoomIn_);

    btnZoomOut_ = std::make_unique<juce::TextButton>("-");
    btnZoomOut_->onClick = [this] { aplicarZoom(zoom_ / 1.2f, {getWidth() / 2.0f, getHeight() / 2.0f}); };
    addAndMakeVisible(*btnZoomOut_);

    btnZoomFit_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_fit"));
    btnZoomFit_->onClick = [this] { zoom_ = 1.0f; panOffset_ = {0.0f, 0.0f}; repaint(); };
    addAndMakeVisible(*btnZoomFit_);

    btnPresets_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_presets"));
    btnPresets_->onClick = [this] { mostrarMenuPresets(); };
    addAndMakeVisible(*btnPresets_);

    sliderTamanho_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    sliderTamanho_->setRange(50.0, 200.0, 1.0);
    sliderTamanho_->setValue(100.0, juce::dontSendNotification);
    sliderTamanho_->setTextValueSuffix("%");
    sliderTamanho_->setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 20);
    sliderTamanho_->setTooltip(i18n::t("arvore_backup.slider_tamanho_tooltip"));
    sliderTamanho_->onValueChange = [this] {
        escalaTamanho_ = static_cast<float>(sliderTamanho_->getValue()) / 100.0f;
        aplicarEscalaTamanho(escalaTamanho_);
    };
    addAndMakeVisible(*sliderTamanho_);

    detailContent_ = std::make_unique<TreeDetailContent>();
    detailViewport_ = std::make_unique<juce::Viewport>();
    detailViewport_->setViewedComponent(detailContent_.get(), false);
    detailViewport_->setScrollBarsShown(true, false);
    detailViewport_->setVisible(false);
    addAndMakeVisible(*detailViewport_);

    setWantsKeyboardFocus(true);
    recarregar();

    EventBus::obterInstancia().registrarListener(this);
}

ArvoreBackupComponent::~ArvoreBackupComponent() {
    EventBus::obterInstancia().removerListener(this);
    detailViewport_->setViewedComponent(nullptr, false);
}

void ArvoreBackupComponent::aoItemAlterado(const EventoItemAlterado& e) {
    if (e.tipoAlteracao != "titulo") return;
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    std::string itemId = e.itemId;
    juce::MessageManager::callAsync([safeThis, itemId] {
        if (!safeThis || safeThis->selectedFolderId_.empty()) return;
        for (const auto& n : safeThis->nodes_) {
            if (n.id == safeThis->selectedFolderId_) {
                if (n.itemIdsDiretos.count(itemId)) {
                    safeThis->atualizarPainelDetalhe(safeThis->selectedFolderId_, true);
                }
                break;
            }
        }
    });
}

void ArvoreBackupComponent::recarregar() {
    recalcularNodes();
    repaint();
}

void ArvoreBackupComponent::recalcularNodes() {
    nodes_.clear();
    auto arvore = projeto_.arvoreAcervo();

    std::function<void(const ProjetoAberto::NoArvore&, int, int)> adicionarNo =
        [&](const ProjetoAberto::NoArvore& no, int nivel, int index) {
            if (!no.id.empty()) {
                FolderNode node;
                node.id = no.id;
                node.nome = no.nome;
                node.pastaPaiId = no.pastaPaiId;
                node.contagemItens = static_cast<int>(no.itemIds.size());
                node.ativo = no.ativo;
                node.itemIdsDiretos = no.itemIdsDiretos;
                if (no.corCustomizadaHex.isNotEmpty()) {
                    node.corCustomizada = juce::Colour::fromString(no.corCustomizadaHex);
                    node.hasCorCustomizada = true;
                }

                int defaultX = 40 + nivel * 240;
                int defaultY = 80 + index * 110;
                int x = (no.posicaoX != 0 || no.posicaoY != 0) ? no.posicaoX : defaultX;
                int y = (no.posicaoX != 0 || no.posicaoY != 0) ? no.posicaoY : defaultY;

                node.bounds = juce::Rectangle<int>(x, y, 190, 84);
                node.boundsOriginal = node.bounds;
                nodes_.push_back(node);
            }

            for (size_t i = 0; i < no.filhos.size(); ++i) {
                adicionarNo(no.filhos[i], nivel + 1, static_cast<int>(i));
            }
        };

    // Abas: se a aba ativa isola uma pasta, desenha só ela + descendentes —
    // acha o nó na árvore completa (que já veio com pastaPaiId/filhos
    // corretos do banco) e trata como se fosse a raiz.
    const ProjetoAberto::NoArvore* raizFiltro = nullptr;
    if (abaAtiva_ >= 0 && abaAtiva_ < static_cast<int>(abas_.size()) && abas_[static_cast<size_t>(abaAtiva_)].pastaRaizId.has_value()) {
        const std::string& alvoId = *abas_[static_cast<size_t>(abaAtiva_)].pastaRaizId;
        std::function<const ProjetoAberto::NoArvore*(const ProjetoAberto::NoArvore&)> encontrarNo =
            [&](const ProjetoAberto::NoArvore& no) -> const ProjetoAberto::NoArvore* {
                if (no.id == alvoId) return &no;
                for (auto& filho : no.filhos) {
                    if (auto* achado = encontrarNo(filho)) return achado;
                }
                return nullptr;
            };
        raizFiltro = encontrarNo(arvore);
    }

    if (raizFiltro) {
        adicionarNo(*raizFiltro, 0, 0);
    } else {
        for (size_t i = 0; i < arvore.filhos.size(); ++i) {
            adicionarNo(arvore.filhos[i], 0, static_cast<int>(i));
        }
    }

    aplicarEscalaTamanho(escalaTamanho_); // S4/14 — reaplica o tamanho escolhido ao layout recém-lido
}

juce::Rectangle<int> ArvoreBackupComponent::areaBarraAbas() const {
    return getLocalBounds().withTrimmedTop(44).removeFromTop(kAlturaBarraAbas);
}

juce::Rectangle<int> ArvoreBackupComponent::boundsDaAba(int indice) const {
    if (indice < 0 || indice >= static_cast<int>(abas_.size())) return {};
    auto area = areaBarraAbas();
    auto font = juce::Font(juce::FontOptions(11.0f));
    int x = area.getX() + 8;
    for (int i = 0; i < indice; ++i) {
        int textW = juce::GlyphArrangement::getStringWidthInt(font, abas_[static_cast<size_t>(i)].titulo);
        int fecharW = (i == 0) ? 0 : 16;
        x += (16 + textW + fecharW + 10) + 4;
    }
    int textW = juce::GlyphArrangement::getStringWidthInt(font, abas_[static_cast<size_t>(indice)].titulo);
    int fecharW = (indice == 0) ? 0 : 16;
    int largura = 16 + textW + fecharW + 10;
    return { x, area.getY() + 2, largura, area.getHeight() - 4 };
}

juce::Rectangle<int> ArvoreBackupComponent::boundsFecharAba(int indice) const {
    if (indice <= 0) return {}; // "All" nunca fecha
    auto b = boundsDaAba(indice);
    if (b.isEmpty()) return {};
    return { b.getRight() - 18, b.getY(), 16, b.getHeight() };
}

void ArvoreBackupComponent::abrirAbaParaPasta(const std::string& pastaId, const juce::String& nomePasta) {
    // Já existe uma aba pra essa pasta? Só troca em vez de duplicar.
    for (int i = 0; i < static_cast<int>(abas_.size()); ++i) {
        if (abas_[static_cast<size_t>(i)].pastaRaizId && *abas_[static_cast<size_t>(i)].pastaRaizId == pastaId) {
            selecionarAba(i);
            return;
        }
    }
    AbaEstrutura nova;
    nova.pastaRaizId = pastaId;
    nova.titulo = nomePasta;
    abas_.push_back(nova);
    selecionarAba(static_cast<int>(abas_.size()) - 1);
}

void ArvoreBackupComponent::selecionarAba(int indice) {
    if (indice < 0 || indice >= static_cast<int>(abas_.size())) return;
    if (indice == abaAtiva_) { repaint(); return; }

    // Salva zoom/pan/seleção da aba atual antes de trocar.
    if (abaAtiva_ >= 0 && abaAtiva_ < static_cast<int>(abas_.size())) {
        abas_[static_cast<size_t>(abaAtiva_)].zoom = zoom_;
        abas_[static_cast<size_t>(abaAtiva_)].panOffset = panOffset_;
        abas_[static_cast<size_t>(abaAtiva_)].selectedFolderId = selectedFolderId_;
    }

    abaAtiva_ = indice;
    auto& aba = abas_[static_cast<size_t>(indice)];
    zoom_ = aba.zoom;
    panOffset_ = aba.panOffset;
    selectedFolderId_ = aba.selectedFolderId;
    recarregar();
}

void ArvoreBackupComponent::fecharAba(int indice) {
    if (indice <= 0 || indice >= static_cast<int>(abas_.size())) return; // "All" (0) não fecha
    abas_.erase(abas_.begin() + indice);
    if (abaAtiva_ == indice) {
        abaAtiva_ = -1; // garante que selecionarAba(0) não pule por engano
        selecionarAba(0);
    } else {
        if (abaAtiva_ > indice) abaAtiva_--;
        repaint();
    }
}

void ArvoreBackupComponent::desenharBarraDeAbas(juce::Graphics& g) {
    const auto& tk = tema();
    auto area = areaBarraAbas();
    g.setColour(tk.painelAlt);
    g.fillRect(area);
    g.setColour(tk.borda);
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    for (int i = 0; i < static_cast<int>(abas_.size()); ++i) {
        auto b = boundsDaAba(i);
        bool ativa = (i == abaAtiva_);
        g.setColour(ativa ? tk.painel : tk.painelAlt.darker(0.05f));
        g.fillRoundedRectangle(b.toFloat(), 4.0f);
        g.setColour(tk.borda.withAlpha(ativa ? 0.9f : 0.4f));
        g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 4.0f, 1.0f);

        auto textArea = b.reduced(8, 0);
        if (i != 0) textArea.removeFromRight(20);
        g.setColour(ativa ? tk.textoPrimario : tk.textoSecundario);
        g.drawText(abas_[static_cast<size_t>(i)].titulo, textArea, juce::Justification::centredLeft, true);

        if (i != 0) {
            auto fechar = boundsFecharAba(i).toFloat();
            g.setColour(tk.textoSecundario.withAlpha(0.7f));
            float cx = fechar.getCentreX(), cy = fechar.getCentreY(), sz = 3.5f;
            g.drawLine(cx - sz, cy - sz, cx + sz, cy + sz, 1.3f);
            g.drawLine(cx + sz, cy - sz, cx - sz, cy + sz, 1.3f);
        }
    }
}

std::vector<juce::String> ArvoreBackupComponent::obterListaPastas() const {
    std::vector<juce::String> lista;
    for (const auto& n : nodes_) {
        lista.push_back(n.nome);
    }
    return lista;
}

void ArvoreBackupComponent::moverItemParaPasta(const std::string& itemId, const std::string& novaPasta) {
    if (itemId.empty()) return;
    for (const auto& n : nodes_) {
        if (n.nome.toStdString() == novaPasta || n.id == novaPasta) {
            projeto_.adicionarItensAPasta({itemId}, n.id);
            break;
        }
    }
    recarregar();
}

void ArvoreBackupComponent::selecionarERenomearPasta(const std::string& pastaId) {
    recarregar();
    
    bool encontrou = false;
    for (auto& n : nodes_) {
        if (n.id == pastaId) {
            n.selecionado = true;
            encontrou = true;
        } else {
            n.selecionado = false;
        }
    }
    
    if (encontrou) {
        juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
        juce::String nomeAtual;
        for (const auto& n : nodes_) {
            if (n.id == pastaId) {
                nomeAtual = n.nome;
                break;
            }
        }
        
        pedirTextoBackup("RENAME FOLDER", "Enter new folder name:", nomeAtual,
            [safeThis, pastaId](std::optional<juce::String> nome) {
                if (!safeThis || !nome || nome->trim().isEmpty()) return;
                safeThis->renomearPastaSelecionada(pastaId, nome->trim().toStdString());
            });
    }
    
    repaint();
}

void ArvoreBackupComponent::criarNovaPasta(const std::string& nome, const std::optional<std::string>& pastaPaiId) {
    auto pos = posicaoLivrePertoDoCentro(190, 84);
    std::string novoId = projeto_.criarPastaAcervo(nome, pastaPaiId);
    projeto_.atualizarPosicaoPastaAcervo(novoId, pos.x, pos.y);

    destaqueNovaPastaId_ = novoId;
    startTimer(30);

    recarregar();
}

void ArvoreBackupComponent::renomearPastaSelecionada(const std::string& pastaId, const std::string& novoNome) {
    projeto_.renomearPastaAcervo(pastaId, novoNome);
    recarregar();
}

void ArvoreBackupComponent::apagarPastaSelecionada(const std::string& pastaId) {
    projeto_.apagarPastaAcervo(pastaId);
    recarregar();
}

void ArvoreBackupComponent::conectarPastas(const std::string& pastaFilhoId, const std::optional<std::string>& novaPastaPaiId) {
    projeto_.moverPastaAcervo(pastaFilhoId, novaPastaPaiId);
    recarregar();
}

void ArvoreBackupComponent::alternarAtivoPasta(const std::string& pastaId) {
    for (const auto& n : nodes_) {
        if (n.id == pastaId) {
            projeto_.alternarAtivoPastaAcervo(pastaId, !n.ativo);
            break;
        }
    }
    recarregar();
}

void ArvoreBackupComponent::mostrarSeletorDeCorPasta(std::vector<std::string> pastaIds, juce::Rectangle<int> screenBounds) {
    if (pastaIds.empty()) return;

    // Parte da cor já atribuída à primeira pasta selecionada, se houver —
    // reabrir o picker pra ajustar mostra o estado atual, não sempre vermelho.
    juce::Colour corInicial = juce::Colours::red;
    for (const auto& n : nodes_) {
        if (n.id == pastaIds.front() && n.hasCorCustomizada) { corInicial = n.corCustomizada; break; }
    }

    std::vector<juce::Colour> historico;
    for (const auto& hex : projeto_.historicoCoresPasta()) historico.push_back(juce::Colour::fromString(hex));

    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    auto content = std::make_unique<ColourPickerContent>(
        corInicial, historico,
        [safeThis, pastaIds](juce::Colour cor) {
            if (safeThis) safeThis->aplicarCorAPastas(pastaIds, cor);
        },
        [safeThis](juce::Colour corFinal) {
            if (safeThis) safeThis->registrarCorNoHistorico(corFinal);
        });
    juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
}

void ArvoreBackupComponent::registrarCorNoHistorico(juce::Colour cor) {
    // Fase 3: histórico por PROJETO (não por pasta) — move pra frente se já
    // existir, insere na frente se for nova, corta em 10.
    juce::String hex = cor.toDisplayString(true);
    auto atual = projeto_.historicoCoresPasta();
    atual.erase(std::remove(atual.begin(), atual.end(), hex), atual.end());
    atual.insert(atual.begin(), hex);
    if (atual.size() > 10) atual.resize(10);
    projeto_.definirHistoricoCoresPasta(atual);
}

void ArvoreBackupComponent::aplicarCorAPastas(const std::vector<std::string>& pastaIds, juce::Colour cor) {
    // toDisplayString(true) inclui o alfa — fromString() em recalcularNodes()
    // faz o caminho de volta. Persistida por pasta (item 12): fechar/reabrir
    // o projeto ou reconstruir o Treemap não apaga, porque recalcularNodes()
    // relê cor_customizada do banco toda vez.
    juce::String hex = cor.toDisplayString(true);
    for (const auto& id : pastaIds) projeto_.definirCorPastaAcervo(id, hex);
    for (auto& n : nodes_) {
        if (std::find(pastaIds.begin(), pastaIds.end(), n.id) != pastaIds.end()) {
            n.corCustomizada = cor;
            n.hasCorCustomizada = true;
        }
    }
    repaint();
}

bool ArvoreBackupComponent::ehDescendente(const std::string& noPaiId, const std::string& noFilhoId) const {
    if (noPaiId.empty() || noFilhoId.empty()) return false;
    if (noPaiId == noFilhoId) return true;

    std::string atual = noPaiId;
    while (!atual.empty()) {
        if (atual == noFilhoId) return true;
        std::string proximoPai;
        for (const auto& n : nodes_) {
            if (n.id == atual) {
                proximoPai = n.pastaPaiId;
                break;
            }
        }
        if (proximoPai == atual) break;
        atual = proximoPai;
    }
    return false;
}

void ArvoreBackupComponent::autoArranjar() {
    if (nodes_.empty()) return;

    static constexpr int kBaseNodeW = 190;
    static constexpr int kBaseNodeH = 84;
    static constexpr int kMargin = 40;
    static constexpr int kToolbarH = 50;
    static constexpr int kMinGap = 20;

    std::map<std::string, std::vector<std::string>> filhosDe;
    std::set<std::string> temPai;
    for (auto& n : nodes_) {
        if (!n.pastaPaiId.empty()) {
            filhosDe[n.pastaPaiId].push_back(n.id);
            temPai.insert(n.id);
        }
    }

    std::vector<std::string> raizes;
    for (auto& n : nodes_)
        if (!temPai.count(n.id)) raizes.push_back(n.id);

    int maxDepth = 0;
    std::map<std::string, int> profundidade;
    std::function<void(const std::string&, int)> calcularProfundidade =
        [&](const std::string& id, int nivel) {
            profundidade[id] = nivel;
            maxDepth = std::max(maxDepth, nivel);
            for (auto& f : filhosDe[id]) calcularProfundidade(f, nivel + 1);
        };
    for (auto& r : raizes) calcularProfundidade(r, 0);

    int totalLeaves = 0;
    std::function<int(const std::string&)> contarFolhas =
        [&](const std::string& id) -> int {
            auto& filhos = filhosDe[id];
            if (filhos.empty()) return 1;
            int s = 0;
            for (auto& f : filhos) s += contarFolhas(f);
            return s;
        };
    for (auto& r : raizes) totalLeaves += contarFolhas(r);
    totalLeaves = std::max(1, totalLeaves);

    float viewW = static_cast<float>(std::max(400, getWidth())) / zoom_;
    float viewH = static_cast<float>(std::max(300, getHeight() - kToolbarH)) / zoom_;

    int cols = maxDepth + 1;
    float widthBudget = viewW - kMargin * 2.0f;
    float heightBudget = viewH - kMargin;

    float nodeScale = 1.0f;
    int nodeW = kBaseNodeW;
    int nodeH = kBaseNodeH;
    int gapX = kMinGap;
    int gapY = kMinGap;

    float neededW = cols * kBaseNodeW + std::max(0, cols - 1) * kMinGap;
    float neededH = totalLeaves * kBaseNodeH + std::max(0, totalLeaves - 1) * kMinGap;

    if (neededW > widthBudget || neededH > heightBudget) {
        float scX = widthBudget / neededW;
        float scY = heightBudget / neededH;
        nodeScale = std::max(0.7f, std::min({scX, scY, 1.0f}));
        nodeW = static_cast<int>(kBaseNodeW * nodeScale);
        nodeH = static_cast<int>(kBaseNodeH * nodeScale);
    }

    float totalNodeW = cols * nodeW;
    float remainW = widthBudget - totalNodeW;
    gapX = (cols > 1) ? std::max(kMinGap, static_cast<int>(remainW / (cols - 1))) : kMinGap;

    float totalNodeH = totalLeaves * nodeH;
    float remainH = heightBudget - totalNodeH;
    gapY = (totalLeaves > 1) ? std::max(kMinGap, static_cast<int>(remainH / (totalLeaves - 1))) : kMinGap;

    std::map<std::string, int> subtreeHeight;
    std::function<int(const std::string&)> calcularAltura =
        [&](const std::string& id) -> int {
            auto& filhos = filhosDe[id];
            if (filhos.empty()) {
                subtreeHeight[id] = nodeH;
                return nodeH;
            }
            int total = 0;
            for (size_t i = 0; i < filhos.size(); ++i) {
                if (i > 0) total += gapY;
                total += calcularAltura(filhos[i]);
            }
            subtreeHeight[id] = std::max(total, nodeH);
            return subtreeHeight[id];
        };
    for (auto& r : raizes) calcularAltura(r);

    std::map<std::string, juce::Point<int>> posicoes;
    std::function<void(const std::string&, int, int)> posicionar =
        [&](const std::string& id, int x, int yTop) {
            auto& filhos = filhosDe[id];
            if (filhos.empty()) {
                posicoes[id] = {x, yTop};
                return;
            }
            int cursorY = yTop;
            for (size_t i = 0; i < filhos.size(); ++i) {
                posicionar(filhos[i], x + nodeW + gapX, cursorY);
                cursorY += subtreeHeight[filhos[i]] + gapY;
            }
            int firstChildY = posicoes[filhos.front()].y;
            int lastChildY = posicoes[filhos.back()].y;
            int centeredY = (firstChildY + lastChildY) / 2;
            posicoes[id] = {x, centeredY};
        };

    int cursorY = kToolbarH + kMargin;
    for (auto& r : raizes) {
        posicionar(r, kMargin, cursorY);
        cursorY += subtreeHeight[r] + gapY;
    }

    for (auto& n : nodes_) {
        auto it = posicoes.find(n.id);
        if (it != posicoes.end()) {
            n.bounds = juce::Rectangle<int>(it->second.x, it->second.y, nodeW, nodeH);
            n.boundsOriginal = n.bounds;
            projeto_.atualizarPosicaoPastaAcervo(n.id, it->second.x, it->second.y);
        }
    }

    panOffset_ = {0.0f, 0.0f};
    aplicarEscalaTamanho(escalaTamanho_); // S4/14 — reaplica o tamanho escolhido (também repinta)
}

void ArvoreBackupComponent::desenharLinhaConexaoN8n(juce::Graphics& g, juce::Point<float> p1, juce::Point<float> p2, bool ativo, bool rascunho) const {
    juce::Path p;
    p.startNewSubPath(p1);

    float controlOffset = std::abs(p2.x - p1.x) * 0.5f;
    if (controlOffset < 40.0f) controlOffset = 40.0f;
    juce::Point<float> c1(p1.x + controlOffset, p1.y);
    juce::Point<float> c2(p2.x - controlOffset, p2.y);

    p.cubicTo(c1, c2, p2);

    if (rascunho) {
        g.setColour(tema().acento);
        juce::PathStrokeType stroke(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        float dashLengths[] = { 6.0f, 4.0f };
        stroke.createDashedStroke(p, p, dashLengths, 2);
        g.strokePath(p, stroke);
    } else {
        g.setColour(ativo ? tema().acento.withAlpha(0.40f) : tema().textoTerciario.withAlpha(0.15f));
        g.strokePath(p, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

juce::Point<float> ArvoreBackupComponent::screenToCanvas(juce::Point<int> screen) const {
    return {(screen.x - panOffset_.x) / zoom_, (screen.y - panOffset_.y) / zoom_};
}

juce::Point<int> ArvoreBackupComponent::canvasToScreen(juce::Point<float> canvas) const {
    return {static_cast<int>(canvas.x * zoom_ + panOffset_.x), static_cast<int>(canvas.y * zoom_ + panOffset_.y)};
}

void ArvoreBackupComponent::aplicarZoom(float novoZoom, juce::Point<float> centro) {
    novoZoom = juce::jlimit(0.15f, 3.0f, novoZoom);
    float ratio = novoZoom / zoom_;
    panOffset_.x = centro.x - (centro.x - panOffset_.x) * ratio;
    panOffset_.y = centro.y - (centro.y - panOffset_.y) * ratio;
    zoom_ = novoZoom;
    repaint();
}

juce::Rectangle<int> ArvoreBackupComponent::minimapBounds() const {
    constexpr int kMinimapW = 160, kMinimapH = 100, kMargin = 8;
    return {getWidth() - kMinimapW - kMargin, getHeight() - kMinimapH - kMargin, kMinimapW, kMinimapH};
}

void ArvoreBackupComponent::iniciarEdicaoInline(int nodeIndex) {
    finalizarEdicaoInline();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes_.size())) return;
    auto& node = nodes_[static_cast<size_t>(nodeIndex)];
    editingNodeId_ = node.id;

    auto screenBounds = node.bounds.toFloat();
    screenBounds.setX(screenBounds.getX() * zoom_ + panOffset_.x);
    screenBounds.setY(screenBounds.getY() * zoom_ + panOffset_.y);
    screenBounds.setWidth(screenBounds.getWidth() * zoom_);
    screenBounds.setHeight(screenBounds.getHeight() * zoom_);

    auto nameArea = screenBounds.withTrimmedTop(24.0f * zoom_).reduced(8.0f * zoom_, 4.0f * zoom_).removeFromTop(20.0f * zoom_);

    inlineEditor_ = std::make_unique<juce::TextEditor>();
    inlineEditor_->setText(node.nome, false);
    inlineEditor_->setFont(juce::Font(juce::FontOptions(12.0f * zoom_, juce::Font::bold)));
    inlineEditor_->setColour(juce::TextEditor::backgroundColourId, tema().painelAlt);
    inlineEditor_->setColour(juce::TextEditor::textColourId, tema().textoPrimario);
    inlineEditor_->setColour(juce::TextEditor::outlineColourId, tema().acento);
    inlineEditor_->setColour(juce::TextEditor::focusedOutlineColourId, tema().acento);
    inlineEditor_->setBounds(nameArea.toNearestInt());
    inlineEditor_->selectAll();
    inlineEditor_->onReturnKey = [this] { finalizarEdicaoInline(); };
    inlineEditor_->onEscapeKey = [this] { editingNodeId_.clear(); inlineEditor_.reset(); };
    inlineEditor_->onFocusLost = [this] { finalizarEdicaoInline(); };
    addAndMakeVisible(*inlineEditor_);
    inlineEditor_->grabKeyboardFocus();
}

void ArvoreBackupComponent::finalizarEdicaoInline() {
    if (!inlineEditor_ || editingNodeId_.empty()) return;
    auto novoNome = inlineEditor_->getText().trim();
    auto nodeId = editingNodeId_;
    editingNodeId_.clear();
    inlineEditor_.reset();
    if (novoNome.isNotEmpty()) {
        renomearPastaSelecionada(nodeId, novoNome.toStdString());
    }
}

void ArvoreBackupComponent::desenharMinimap(juce::Graphics& g) const {
    if (nodes_.empty()) return;
    auto mmBounds = minimapBounds().toFloat();

    g.setColour(tema().fundo.withAlpha(0.85f));
    g.fillRoundedRectangle(mmBounds, 4.0f);
    g.setColour(tema().borda);
    g.drawRoundedRectangle(mmBounds, 4.0f, 1.0f);

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& n : nodes_) {
        minX = std::min(minX, static_cast<float>(n.bounds.getX()));
        minY = std::min(minY, static_cast<float>(n.bounds.getY()));
        maxX = std::max(maxX, static_cast<float>(n.bounds.getRight()));
        maxY = std::max(maxY, static_cast<float>(n.bounds.getBottom()));
    }
    float canvasW = std::max(1.0f, maxX - minX + 40.0f);
    float canvasH = std::max(1.0f, maxY - minY + 40.0f);
    float mmInner = mmBounds.getWidth() - 8.0f;
    float mmInnerH = mmBounds.getHeight() - 8.0f;
    float scaleM = std::min(mmInner / canvasW, mmInnerH / canvasH);

    for (const auto& n : nodes_) {
        float nx = mmBounds.getX() + 4.0f + (n.bounds.getX() - minX) * scaleM;
        float ny = mmBounds.getY() + 4.0f + (n.bounds.getY() - minY) * scaleM;
        float nw = n.bounds.getWidth() * scaleM;
        float nh = n.bounds.getHeight() * scaleM;
        g.setColour(n.selecionado ? tema().acento : tema().painel);
        g.fillRect(nx, ny, std::max(2.0f, nw), std::max(1.0f, nh));
    }

    float vpLeft = (-panOffset_.x / zoom_ - minX) * scaleM + mmBounds.getX() + 4.0f;
    float vpTop = (-panOffset_.y / zoom_ - minY) * scaleM + mmBounds.getY() + 4.0f;
    float vpW = (getWidth() / zoom_) * scaleM;
    float vpH = (getHeight() / zoom_) * scaleM;
    g.setColour(tema().acento.withAlpha(0.3f));
    g.fillRect(vpLeft, vpTop, vpW, vpH);
    g.setColour(tema().acento);
    g.drawRect(vpLeft, vpTop, vpW, vpH, 1.0f);
}

void ArvoreBackupComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    bool isLight = (tk.fundo.getBrightness() > 0.5f);
    juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
    g.fillAll(bg);

    // Canvas background grid pattern (n8n style dots) — in screen space
    g.setColour(tema().painelAlt.withAlpha(0.4f));
    float dotStep = 24.0f * zoom_;
    if (dotStep > 4.0f) {
        float startX = std::fmod(panOffset_.x, dotStep);
        float startY = std::fmod(panOffset_.y, dotStep);
        if (startX < 0) startX += dotStep;
        if (startY < 0) startY += dotStep;
        for (float x = startX; x < getWidth(); x += dotStep)
            for (float y = startY; y < getHeight(); y += dotStep)
                g.fillEllipse(x, y, 2.0f, 2.0f);
    }

    // Header title
    g.setColour(tema().textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteTitulo, juce::Font::bold)));
    g.drawText(i18n::t("arvore_backup.titulo"), getLocalBounds().reduced(20, 16).removeFromTop(28), juce::Justification::centredLeft);

    // Zoom indicator
    g.setColour(tema().textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(juce::String(static_cast<int>(zoom_ * 100)) + "%", getLocalBounds().reduced(20, 16).removeFromTop(24), juce::Justification::centredRight);

    if (nodes_.empty()) {
        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(i18n::t("arvore_backup.vazio"),
                   getLocalBounds(), juce::Justification::centred, true);
        desenharBarraDeAbas(g);
        return;
    }

    g.saveState();
    g.addTransform(juce::AffineTransform::translation(panOffset_.x, panOffset_.y).scaled(zoom_, zoom_, panOffset_.x, panOffset_.y));

    // Correct: apply translation then scale around the translate origin
    // Actually let me reconsider. We want: screen = canvas * zoom + panOffset
    // So the transform is: scale(zoom) then translate(panOffset/zoom... no.
    // Let's use: translate(panOffset) then scale(zoom, zoom, 0, 0) isn't right either.
    // The correct transform: first translate by panOffset, but scale around origin...
    // Actually: screen_x = canvas_x * zoom + panOffset_x
    // So the affine transform is: scale(zoom) followed by translate(panOffset)
    g.restoreState();
    g.saveState();
    auto transform = juce::AffineTransform::scale(zoom_).translated(panOffset_.x, panOffset_.y);
    g.addTransform(transform);

    // Connection lines
    std::map<std::string, juce::Point<float>> outputPorts;
    std::map<std::string, juce::Point<float>> inputPorts;

    for (const auto& node : nodes_) {
        inputPorts[node.id] = juce::Point<float>(static_cast<float>(node.bounds.getX()),
                                                 static_cast<float>(node.bounds.getCentreY()));
        outputPorts[node.id] = juce::Point<float>(static_cast<float>(node.bounds.getRight()),
                                                  static_cast<float>(node.bounds.getCentreY()));
    }

    for (const auto& node : nodes_) {
        if (!node.pastaPaiId.empty() && outputPorts.count(node.pastaPaiId) && inputPorts.count(node.id)) {
            desenharLinhaConexaoN8n(g, outputPorts[node.pastaPaiId], inputPorts[node.id], node.ativo, false);
        }
    }

    // Live connection drag preview (socketDragPos_ is in canvas space)
    if (!socketDragParentId_.empty() && outputPorts.count(socketDragParentId_)) {
        desenharLinhaConexaoN8n(g, outputPorts[socketDragParentId_], socketDragPos_, true, true);
    }

    // Node cards
    for (const auto& node : nodes_) {
        auto b = node.bounds.toFloat();

        g.setColour(node.ativo ? tema().painel : tema().painelAlt);
        g.fillRoundedRectangle(b, 8.0f);

        // FOLDER COLOR (item 12): camada translúcida de identificação, nunca
        // preenchimento sólido — baixa opacidade, por cima do fundo do card
        // e por BAIXO do cabeçalho/texto (desenhados depois), pra não afetar
        // cálculo nenhum do Treemap nem tapar leitura de nome/contagem.
        if (node.hasCorCustomizada) {
            g.setColour(node.corCustomizada.withAlpha(0.22f));
            g.fillRoundedRectangle(b, 8.0f);
        }

        if (node.selecionado) {
            // Zebra (marching-ants) selection border: solid white base stroke
            // with a dashed black stroke on top, instead of tinting the fill.
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(b, 8.0f, 2.5f);

            juce::Path outline;
            outline.addRoundedRectangle(b, 8.0f);
            juce::PathStrokeType stroke(2.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
            float dashLengths[] = { 4.0f, 4.0f };
            stroke.createDashedStroke(outline, outline, dashLengths, 2);
            g.setColour(juce::Colours::black);
            g.strokePath(outline, stroke);
        } else {
            g.setColour(node.ativo ? tema().borda : tema().textoTerciario);
            g.drawRoundedRectangle(b, 8.0f, 1.2f);
        }

        auto header = b.removeFromTop(24);
        g.setColour(node.ativo ? tema().painelAlt : tema().fundo);
        g.fillRoundedRectangle(header, 8.0f);
        g.fillRect(header.withTrimmedTop(16));

        g.setColour(node.ativo ? tema().acento : tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(node.ativo ? i18n::t("arvore_backup.no_ativo") : i18n::t("arvore_backup.no_desativado"), header.reduced(10, 0), juce::Justification::centredLeft);

        auto content = b.reduced(10, 6);
        g.setColour(node.ativo ? tema().textoPrimario : tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText(node.nome, content.removeFromTop(18), juce::Justification::centredLeft, true);

        g.setColour(tema().textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        juce::String desc = juce::String(node.contagemItens) + " " + i18n::t("arvore_backup.items");
        g.drawText(desc, content, juce::Justification::centredLeft, true);

        // Input & Output socket handles
        float portRadius = 6.0f;
        auto inPort = inputPorts[node.id];
        auto outPort = outputPorts[node.id];

        g.setColour(node.pastaPaiId.empty() ? tema().painelAlt : tema().acento);
        g.fillEllipse(inPort.x - portRadius, inPort.y - portRadius, portRadius * 2.0f, portRadius * 2.0f);
        g.setColour(tema().textoPrimario);
        g.drawEllipse(inPort.x - portRadius, inPort.y - portRadius, portRadius * 2.0f, portRadius * 2.0f, 1.2f);

        g.setColour(tema().acento);
        g.fillEllipse(outPort.x - portRadius, outPort.y - portRadius, portRadius * 2.0f, portRadius * 2.0f);
        g.setColour(tema().textoPrimario);
        g.drawEllipse(outPort.x - portRadius, outPort.y - portRadius, portRadius * 2.0f, portRadius * 2.0f, 1.2f);

        // S4/15 — contorno pulsante da pasta recém-criada, até o próximo clique
        if (node.id == destaqueNovaPastaId_) {
            double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            float alpha = 0.45f + 0.45f * static_cast<float>(std::sin(t * 5.0));
            g.setColour(tema().acento.withAlpha(alpha));
            g.drawRoundedRectangle(node.bounds.toFloat().expanded(4.0f), 10.0f, 3.0f);
        }
    }

    if (marqueeSelecting_) {
        auto rect = marqueeRectCanvas_;
        g.setColour(tema().acento.withAlpha(0.15f));
        g.fillRect(rect);
        g.setColour(tema().acento);
        g.drawRect(rect, 1.2f / zoom_);
    }

    g.restoreState();

    // Minimap (drawn in screen space)
    desenharMinimap(g);

    desenharBarraDeAbas(g);
}

void ArvoreBackupComponent::resized() {
    auto area = getLocalBounds().removeFromTop(44).reduced(16, 6);

    if (btnImportarEstrutura_) btnImportarEstrutura_->setBounds(area.removeFromLeft(280));
    area.removeFromLeft(16);

    if (btnCriarPasta_) btnCriarPasta_->setBounds(area.removeFromLeft(110));
    area.removeFromLeft(8);
    if (btnRenomearPasta_) btnRenomearPasta_->setBounds(area.removeFromLeft(90));
    area.removeFromLeft(8);
    if (btnApagarPasta_) btnApagarPasta_->setBounds(area.removeFromLeft(90));
    area.removeFromLeft(16);
    if (btnAutoArranjar_) btnAutoArranjar_->setBounds(area.removeFromLeft(120));
    area.removeFromLeft(16);
    if (btnZoomOut_) btnZoomOut_->setBounds(area.removeFromLeft(30));
    area.removeFromLeft(2);
    if (btnZoomFit_) btnZoomFit_->setBounds(area.removeFromLeft(36));
    area.removeFromLeft(2);
    if (btnZoomIn_) btnZoomIn_->setBounds(area.removeFromLeft(30));
    area.removeFromLeft(16);
    if (btnPresets_) btnPresets_->setBounds(area.removeFromLeft(100));
    area.removeFromLeft(16);
    if (sliderTamanho_) sliderTamanho_->setBounds(area.removeFromLeft(180));

    if (detailViewport_ && detailViewport_->isVisible()) {
        auto panelArea = getLocalBounds().withTrimmedTop(44 + kAlturaBarraAbas).removeFromRight(kDetailPanelWidth);
        detailViewport_->setBounds(panelArea);
        detailContent_->setSize(panelArea.getWidth() - detailViewport_->getScrollBarThickness(), detailContent_->getHeight());
    }
}

void ArvoreBackupComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus(); // garante que ESC (e os atalhos +/-/F2 já existentes) cheguem a este canvas
    if (!destaqueNovaPastaId_.empty()) { // S4/15 — qualquer clique encerra o destaque da pasta nova
        destaqueNovaPastaId_.clear();
        stopTimer();
    }

    finalizarEdicaoInline();
    nodeDragIndice_ = -1;
    socketDragParentId_.clear();

    // Barra de abas — clique num × fecha, clique na aba seleciona; consome
    // o clique aqui pra não cair no drag/seleção do canvas por baixo.
    if (areaBarraAbas().contains(e.getPosition())) {
        for (int i = 0; i < static_cast<int>(abas_.size()); ++i) {
            if (boundsFecharAba(i).contains(e.getPosition())) {
                fecharAba(i);
                return;
            }
            if (boundsDaAba(i).contains(e.getPosition())) {
                selecionarAba(i);
                return;
            }
        }
        return;
    }

    // Minimap drag
    if (minimapBounds().contains(e.getPosition())) {
        minimapDragging_ = true;
        return;
    }

    auto canvasClick = screenToCanvas(e.getPosition());

    // Check Output Socket Handles for dragging connections (in canvas space)
    for (size_t i = 0; i < nodes_.size(); ++i) {
        juce::Point<float> outPort(static_cast<float>(nodes_[i].bounds.getRight()), static_cast<float>(nodes_[i].bounds.getCentreY()));
        if (outPort.getDistanceSquaredFrom(canvasClick) <= 225.0f / (zoom_ * zoom_) + 225.0f) {
            socketDragParentId_ = nodes_[i].id;
            socketDragPos_ = canvasClick;
            repaint();
            return;
        }
    }

    // Check Node Cards for dragging or context menu (in canvas space)
    int hitIndex = -1;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].bounds.toFloat().contains(canvasClick)) { hitIndex = static_cast<int>(i); break; }
    }
    bool hitNode = (hitIndex >= 0);

    if (hitNode) {
        auto& hitNodeRef = nodes_[static_cast<size_t>(hitIndex)];
        nodeDragIndice_ = hitIndex;
        arrastoOffset_ = e.getPosition() - canvasToScreen(hitNodeRef.bounds.getPosition().toFloat());

        // Um clique dentro de uma seleção múltipla (feita via marquee, item 7)
        // preserva a seleção do lote inteiro; caso contrário, seleciona só o
        // nó clicado, como antes.
        size_t previousSelectedCount = 0;
        for (const auto& n : nodes_) if (n.selecionado) ++previousSelectedCount;
        bool clickedIsPartOfMultiSelection = previousSelectedCount > 1 && hitNodeRef.selecionado;

        // Cmd (macOS) / Ctrl: seleção múltipla incremental — cada clique
        // acrescenta (ou tira) uma pasta da seleção, sem zerar as demais,
        // alimentando a mesma seleção que o marquee já produzia.
        bool selecaoIncremental = e.mods.isCommandDown() || e.mods.isCtrlDown();

        if (selecaoIncremental && !e.mods.isPopupMenu()) {
            hitNodeRef.selecionado = !hitNodeRef.selecionado;
        } else if (!clickedIsPartOfMultiSelection) {
            for (auto& n : nodes_) n.selecionado = false;
            hitNodeRef.selecionado = true;
        }

        // Item 10: registra onde cada pasta selecionada está agora — o
        // mouseDrag usa isto pra mover o lote inteiro junto com a pasta
        // clicada, não só ela.
        arrastoGrupoPosicoesIniciais_.clear();
        for (size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i].selecionado)
                arrastoGrupoPosicoesIniciais_.push_back({static_cast<int>(i), nodes_[i].bounds.getPosition().toFloat()});
        }

        if (e.mods.isPopupMenu()) {
            std::vector<std::string> selectedIds;
            for (const auto& n : nodes_) if (n.selecionado) selectedIds.push_back(n.id);
            bool batch = selectedIds.size() > 1;

            std::string pId = hitNodeRef.id;
            bool temPai = !hitNodeRef.pastaPaiId.empty();
            int idx = hitIndex;

            std::function<void(const std::string&, std::set<std::string>&)> coletarFilhos =
                [&](const std::string& parentId, std::set<std::string>& acc) {
                    for (const auto& other : nodes_) {
                        if (other.pastaPaiId == parentId) {
                            acc.insert(other.itemIdsDiretos.begin(), other.itemIdsDiretos.end());
                            coletarFilhos(other.id, acc);
                        }
                    }
                };

            std::set<std::string> allItemIds;
            if (batch) {
                for (const auto& id : selectedIds) {
                    for (const auto& n : nodes_) {
                        if (n.id == id) allItemIds.insert(n.itemIdsDiretos.begin(), n.itemIdsDiretos.end());
                    }
                    coletarFilhos(id, allItemIds);
                }
            } else {
                allItemIds = hitNodeRef.itemIdsDiretos;
                coletarFilhos(pId, allItemIds);
            }

            juce::PopupMenu menu;
            menu.addItem(i18n::t("arvore_backup.exibir_grade"), [this, allItemIds] {
                if (aoMostrarConteudoNaGrade) {
                    aoMostrarConteudoNaGrade(allItemIds);
                }
            });
            if (!batch) {
                menu.addItem(i18n::t("arvore_backup.abrir_em_aba"), [this, pId, nomeNo = hitNodeRef.nome] {
                    abrirAbaParaPasta(pId, nomeNo);
                });
            }
            menu.addSeparator();

            if (!batch) {
                menu.addItem(i18n::t("arvore_backup.nova_subpasta"), [this, pId] {
                    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
                    pedirTextoBackup(i18n::t("arvore_backup.nova_subpasta_titulo"), i18n::t("arvore_backup.nova_subpasta_msg"), i18n::t("arvore_backup.nova_subpasta_padrao"),
                        [safeThis, pId](std::optional<juce::String> nome) {
                            if (!safeThis || !nome || nome->trim().isEmpty()) return;
                            safeThis->criarNovaPasta(nome->trim().toStdString(), pId);
                        });
                });
                menu.addSeparator();

                if (temPai) {
                    menu.addItem(i18n::t("arvore_backup.desconectar_pai"), [this, pId] {
                        conectarPastas(pId, std::nullopt);
                    });
                }

                menu.addItem(i18n::t("arvore_backup.renomear_pasta_menu"), [this, pId, idx] {
                    iniciarEdicaoInline(idx);
                });
            }

            menu.addItem(i18n::t("arvore_backup.alternar_ativo"), [this, selectedIds] {
                for (const auto& id : selectedIds) alternarAtivoPasta(id);
            });

            // FOLDER COLOR (item 12): funciona igual pra uma pasta só ou pro
            // lote inteiro selecionado — selectedIds já cobre os dois casos.
            {
                auto topLeft = canvasToScreen(hitNodeRef.bounds.getTopLeft().toFloat());
                auto bottomRight = canvasToScreen(hitNodeRef.bounds.getBottomRight().toFloat());
                juce::Rectangle<int> ancora(topLeft, bottomRight);
                menu.addItem("Folder Color", [this, selectedIds, ancora] {
                    mostrarSeletorDeCorPasta(selectedIds, ancora);
                });
            }

            if (!batch) {
                menu.addItem(i18n::t("arvore_backup.apagar_pasta_menu"), [this, pId] {
                    apagarPastaSelecionada(pId);
                });
            }
            menu.showMenuAsync(juce::PopupMenu::Options());
        }
    }

    if (!hitNode) {
        if (e.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            menu.addItem(i18n::t("arvore_backup.btn_criar_pasta"), [this] {
                juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
                pedirTextoBackup(i18n::t("arvore_backup.criar_pasta_titulo"), i18n::t("arvore_backup.criar_pasta_msg"), i18n::t("arvore_backup.criar_pasta_padrao"),
                    [safeThis](std::optional<juce::String> nome) {
                        if (!safeThis || !nome || nome->trim().isEmpty()) return;
                        safeThis->criarNovaPasta(nome->trim().toStdString(), std::nullopt);
                    });
            });
            menu.showMenuAsync(juce::PopupMenu::Options());
            return;
        }
        if (e.mods.isShiftDown()) {
            marqueeSelecting_ = true;
            marqueeStartCanvas_ = screenToCanvas(e.getPosition());
            marqueeRectCanvas_ = juce::Rectangle<float>(marqueeStartCanvas_, marqueeStartCanvas_);
        } else {
            // Clique no fundo do canvas (fora de qualquer pasta) desmarca a
            // seleção atual — antes disso o clique só começava o pan e as
            // pastas continuavam com o destaque de seleção ligado.
            for (auto& n : nodes_) n.selecionado = false;
            panning_ = true;
            panStart_ = e.getPosition();
            atualizarPainelDetalhe("");
        }
    } else {
        std::string selId;
        int selCount = 0;
        for (const auto& n : nodes_)
            if (n.selecionado) { if (selId.empty()) selId = n.id; ++selCount; }
        atualizarPainelDetalhe(selCount == 1 ? selId : "");
    }

    repaint();
}

void ArvoreBackupComponent::mouseDrag(const juce::MouseEvent& e) {
    if (marqueeSelecting_) {
        auto canvasPos = screenToCanvas(e.getPosition());
        marqueeRectCanvas_ = juce::Rectangle<float>(marqueeStartCanvas_, canvasPos);
        repaint();
        return;
    }

    if (minimapDragging_) {
        if (nodes_.empty()) return;
        auto mmRect = minimapBounds().toFloat();
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (const auto& n : nodes_) {
            minX = std::min(minX, static_cast<float>(n.bounds.getX()));
            minY = std::min(minY, static_cast<float>(n.bounds.getY()));
            maxX = std::max(maxX, static_cast<float>(n.bounds.getRight()));
            maxY = std::max(maxY, static_cast<float>(n.bounds.getBottom()));
        }
        float canvasW = std::max(1.0f, maxX - minX + 40.0f);
        float canvasH = std::max(1.0f, maxY - minY + 40.0f);
        float mmInner = mmRect.getWidth() - 8.0f;
        float mmInnerH = mmRect.getHeight() - 8.0f;
        float scaleM = std::min(mmInner / canvasW, mmInnerH / canvasH);

        float relX = (e.getPosition().x - mmRect.getX() - 4.0f) / scaleM + minX;
        float relY = (e.getPosition().y - mmRect.getY() - 4.0f) / scaleM + minY;
        panOffset_.x = -(relX * zoom_ - getWidth() / 2.0f);
        panOffset_.y = -(relY * zoom_ - getHeight() / 2.0f);
        repaint();
        return;
    }

    if (panning_) {
        auto delta = e.getPosition() - panStart_;
        panOffset_.x += delta.x;
        panOffset_.y += delta.y;
        panStart_ = e.getPosition();
        repaint();
        return;
    }

    if (!socketDragParentId_.empty()) {
        socketDragPos_ = screenToCanvas(e.getPosition());
        repaint();
        return;
    }

    if (nodeDragIndice_ >= 0 && nodeDragIndice_ < static_cast<int>(nodes_.size())) {
        auto canvasPos = screenToCanvas(e.getPosition() - arrastoOffset_);

        // Item 10: desloca todo o lote selecionado pelo mesmo delta do nó
        // primário, em vez de mover só a pasta em que o arrasto começou.
        juce::Point<float> posInicialPrimario;
        for (auto& [idx, pos] : arrastoGrupoPosicoesIniciais_) {
            if (idx == nodeDragIndice_) { posInicialPrimario = pos; break; }
        }
        juce::Point<float> delta(canvasPos.x - posInicialPrimario.x, canvasPos.y - posInicialPrimario.y);

        for (auto& [idx, posInicial] : arrastoGrupoPosicoesIniciais_) {
            if (idx < 0 || idx >= static_cast<int>(nodes_.size())) continue;
            juce::Point<float> novaPos = posInicial + delta;
            nodes_[static_cast<size_t>(idx)].bounds.setPosition(static_cast<int>(novaPos.x), static_cast<int>(novaPos.y));
        }
        repaint();
    }
}

void ArvoreBackupComponent::mouseUp(const juce::MouseEvent& e) {
    minimapDragging_ = false;
    panning_ = false;

    if (marqueeSelecting_) {
        marqueeSelecting_ = false;
        int countSelecionados = 0;
        std::string unicoSelId;
        for (auto& n : nodes_) {
            n.selecionado = marqueeRectCanvas_.intersects(n.bounds.toFloat());
            if (n.selecionado) { ++countSelecionados; unicoSelId = n.id; }
        }
        atualizarPainelDetalhe(countSelecionados == 1 ? unicoSelId : "");
        marqueeRectCanvas_ = {};
        repaint();
        return;
    }

    if (!socketDragParentId_.empty()) {
        auto canvasRelease = screenToCanvas(e.getPosition());
        std::string targetChildId;

        for (const auto& node : nodes_) {
            if (node.id != socketDragParentId_) {
                juce::Point<float> inPort(static_cast<float>(node.bounds.getX()), static_cast<float>(node.bounds.getCentreY()));
                if (inPort.getDistanceSquaredFrom(canvasRelease) <= 900.0f || node.bounds.toFloat().contains(canvasRelease)) {
                    targetChildId = node.id;
                    break;
                }
            }
        }

        if (!targetChildId.empty()) {
            if (!ehDescendente(socketDragParentId_, targetChildId)) {
                conectarPastas(targetChildId, socketDragParentId_);
            }
        }

        socketDragParentId_.clear();
        repaint();
        return;
    }

    if (nodeDragIndice_ >= 0 && nodeDragIndice_ < static_cast<int>(nodes_.size())) {
        // Item 10: persiste a posição de toda a pasta movida junto, não só
        // a que recebeu o clique.
        for (auto& [idx, posInicial] : arrastoGrupoPosicoesIniciais_) {
            juce::ignoreUnused(posInicial);
            if (idx < 0 || idx >= static_cast<int>(nodes_.size())) continue;
            const auto& node = nodes_[static_cast<size_t>(idx)];
            projeto_.atualizarPosicaoPastaAcervo(node.id, node.bounds.getX(), node.bounds.getY());
        }
    }
    nodeDragIndice_ = -1;
    arrastoGrupoPosicoesIniciais_.clear();
}

void ArvoreBackupComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    auto canvasClick = screenToCanvas(e.getPosition());
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
        if (nodes_[static_cast<size_t>(i)].bounds.toFloat().contains(canvasClick)) {
            iniciarEdicaoInline(i);
            return;
        }
    }
}

void ArvoreBackupComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (minimapBounds().contains(e.getPosition())) return;
    float delta = (wheel.deltaY > 0) ? 1.15f : (1.0f / 1.15f);
    aplicarZoom(zoom_ * delta, e.getPosition().toFloat());
}

bool ArvoreBackupComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        bool haviaSelecao = false;
        for (auto& n : nodes_) { haviaSelecao |= n.selecionado; n.selecionado = false; }
        if (haviaSelecao) {
            atualizarPainelDetalhe("");
            repaint();
        }
        return true;
    }
    if (key == juce::KeyPress('+') || key == juce::KeyPress('=') || key == juce::KeyPress(juce::KeyPress::numberPadAdd)) {
        auto centro = juce::Point<float>(getWidth() / 2.0f, getHeight() / 2.0f);
        for (const auto& n : nodes_)
            if (n.selecionado) { centro = canvasToScreen(n.bounds.getCentre().toFloat()).toFloat(); break; }
        aplicarZoom(zoom_ * 1.2f, centro);
        return true;
    }
    if (key == juce::KeyPress('-') || key == juce::KeyPress(juce::KeyPress::numberPadSubtract)) {
        auto centro = juce::Point<float>(getWidth() / 2.0f, getHeight() / 2.0f);
        for (const auto& n : nodes_)
            if (n.selecionado) { centro = canvasToScreen(n.bounds.getCentre().toFloat()).toFloat(); break; }
        aplicarZoom(zoom_ / 1.2f, centro);
        return true;
    }
    if (key == juce::KeyPress(juce::KeyPress::F2Key)) {
        for (int i = 0; i < static_cast<int>(nodes_.size()); ++i)
            if (nodes_[static_cast<size_t>(i)].selecionado) { iniciarEdicaoInline(i); return true; }
    }
    return false;
}

void ArvoreBackupComponent::atualizarPainelDetalhe(const std::string& folderId, bool forcar) {
    if (folderId.empty() || (folderId == selectedFolderId_ && !forcar)) {
        if (folderId.empty() && !selectedFolderId_.empty()) {
            selectedFolderId_.clear();
            detailViewport_->setVisible(false);
            resized();
        }
        return;
    }
    selectedFolderId_ = folderId;

    const FolderNode* selected = nullptr;
    for (const auto& n : nodes_)
        if (n.id == folderId) { selected = &n; break; }
    if (!selected) { detailViewport_->setVisible(false); return; }

    detailContent_->folderName = selected->nome;

    detailContent_->subfolders.clear();
    for (const auto& n : nodes_) {
        if (n.pastaPaiId == folderId) {
            TreeDetailContent::SubfolderEntry sf;
            sf.nome = n.nome;
            sf.contagemItens = n.contagemItens;
            detailContent_->subfolders.push_back(sf);
        }
    }

    detailContent_->files.clear();
    if (!selected->itemIdsDiretos.empty()) {
        auto detalhes = projeto_.obterDetalhesItens(selected->itemIdsDiretos);
        for (const auto& d : detalhes) {
            TreeDetailContent::FileEntry fe;
            fe.id = d.id;
            fe.nome = d.nome;
            fe.extensao = d.extensao;
            fe.tamanhoBytes = d.tamanhoBytes;
            detailContent_->files.push_back(fe);
        }
    }

    std::set<std::string> allItemIds = selected->itemIdsDiretos;
    std::function<void(const std::string&)> coletarFilhos = [&](const std::string& parentId) {
        for (const auto& other : nodes_) {
            if (other.pastaPaiId == parentId) {
                allItemIds.insert(other.itemIdsDiretos.begin(), other.itemIdsDiretos.end());
                coletarFilhos(other.id);
            }
        }
    };
    coletarFilhos(folderId);

    detailContent_->aoMostrarNaGrade = [this, allItemIds] {
        if (aoMostrarConteudoNaGrade) {
            aoMostrarConteudoNaGrade(allItemIds);
        }
    };

    detailViewport_->setVisible(true);
    resized();
    detailContent_->recalcularAltura();
    detailContent_->repaint();
}

void ArvoreBackupComponent::lookAndFeelChanged() {
    juce::Component::lookAndFeelChanged();
    if (btnCriarPasta_) btnCriarPasta_->setButtonText(i18n::t("arvore_backup.btn_criar_pasta"));
    if (btnRenomearPasta_) btnRenomearPasta_->setButtonText(i18n::t("arvore_backup.btn_renomear"));
    if (btnApagarPasta_) btnApagarPasta_->setButtonText(i18n::t("arvore_backup.btn_apagar"));
    if (btnImportarEstrutura_) btnImportarEstrutura_->setButtonText(i18n::t("arvore_backup.btn_importar"));
    if (btnAutoArranjar_) btnAutoArranjar_->setButtonText(i18n::t("arvore_backup.btn_auto_arranjar"));
    if (btnZoomFit_) btnZoomFit_->setButtonText(i18n::t("arvore_backup.btn_fit"));
    if (btnPresets_) btnPresets_->setButtonText(i18n::t("arvore_backup.btn_presets"));
    if (sliderTamanho_) sliderTamanho_->setTooltip(i18n::t("arvore_backup.slider_tamanho_tooltip"));
    if (detailContent_) detailContent_->lookAndFeelChanged();
    repaint();
}

void ArvoreBackupComponent::timerCallback() {
    repaint(); // S4/15 — anima o pulso do destaque da pasta nova
}

// ── S4/14 — slider de tamanho ────────────────────────────────────────

void ArvoreBackupComponent::aplicarEscalaTamanho(float escala) {
    for (auto& n : nodes_) {
        auto centro = n.boundsOriginal.getCentre();
        int w = juce::jmax(20, juce::roundToInt(n.boundsOriginal.getWidth() * escala));
        int h = juce::jmax(20, juce::roundToInt(n.boundsOriginal.getHeight() * escala));
        n.bounds = juce::Rectangle<int>(w, h).withCentre(centro);
    }
    repaint();
}

// ── S4/15 — posição livre pra pasta nova ─────────────────────────────

juce::Point<int> ArvoreBackupComponent::posicaoLivrePertoDoCentro(int nodeW, int nodeH) const {
    auto topLeft = screenToCanvas({0, 0});
    auto bottomRight = screenToCanvas({juce::jmax(1, getWidth()), juce::jmax(1, getHeight())});
    juce::Point<float> centro((topLeft.x + bottomRight.x) * 0.5f, (topLeft.y + bottomRight.y) * 0.5f);

    auto sobrepoe = [&](juce::Point<float> c) {
        juce::Rectangle<int> cand(static_cast<int>(c.x - nodeW * 0.5f), static_cast<int>(c.y - nodeH * 0.5f), nodeW, nodeH);
        for (const auto& n : nodes_)
            if (n.bounds.intersects(cand)) return true;
        return false;
    };

    juce::Point<float> escolhido = centro;
    if (sobrepoe(centro)) {
        constexpr int kPasso = 40;
        bool achou = false;
        for (int anel = 1; anel <= 20 && !achou; ++anel) {
            int raio = anel * kPasso;
            int amostras = 8 * anel;
            for (int i = 0; i < amostras; ++i) {
                float ang = (juce::MathConstants<float>::twoPi * static_cast<float>(i)) / static_cast<float>(amostras);
                juce::Point<float> c(centro.x + raio * std::cos(ang), centro.y + raio * std::sin(ang));
                if (!sobrepoe(c)) { escolhido = c; achou = true; break; }
            }
        }
    }
    return { static_cast<int>(escolhido.x - nodeW * 0.5f), static_cast<int>(escolhido.y - nodeH * 0.5f) };
}

// ── S4/13 — presets de esquema de pastas ─────────────────────────────

juce::File ArvoreBackupComponent::pastaPresets() const {
    return projeto_.projeto().pasta().getChildFile("presets_pastas");
}

std::vector<juce::String> ArvoreBackupComponent::listarPresetsSalvos() const {
    std::vector<juce::String> out;
    auto pasta = pastaPresets();
    if (!pasta.isDirectory()) return out;
    for (const auto& entry : juce::RangedDirectoryIterator(pasta, false, "*.json", juce::File::findFiles))
        out.push_back(entry.getFile().getFileNameWithoutExtension());
    std::sort(out.begin(), out.end());
    return out;
}

juce::var ArvoreBackupComponent::construirEsquemaAtualComoVar() const {
    juce::DynamicObject::Ptr raiz = new juce::DynamicObject();
    raiz->setProperty("versao", 1);

    juce::Array<juce::var> pastas;
    for (const auto& n : nodes_) {
        juce::DynamicObject::Ptr p = new juce::DynamicObject();
        p->setProperty("id", juce::String(n.id));
        p->setProperty("nome", n.nome);
        p->setProperty("pai", n.pastaPaiId.empty() ? juce::var() : juce::var(juce::String(n.pastaPaiId)));
        p->setProperty("ativo", n.ativo);
        // Posição salva é a de 100% (boundsOriginal), nunca a escalada pelo slider de tamanho.
        p->setProperty("x", n.boundsOriginal.getX());
        p->setProperty("y", n.boundsOriginal.getY());
        pastas.add(juce::var(p.get()));
    }
    raiz->setProperty("pastas", pastas);

    juce::Array<juce::var> itens;
    for (const auto& n : nodes_) {
        for (const auto& itemId : n.itemIdsDiretos) {
            std::string titulo, tipoMidia, codigoAcervo;
            projeto_.obterItemInfo(itemId, titulo, tipoMidia, codigoAcervo);
            juce::DynamicObject::Ptr it = new juce::DynamicObject();
            it->setProperty("itemId", juce::String(itemId));
            it->setProperty("codigoAcervo", juce::String(codigoAcervo));
            it->setProperty("pastaId", juce::String(n.id));
            itens.add(juce::var(it.get()));
        }
    }
    raiz->setProperty("itens", itens);

    return juce::var(raiz.get());
}

bool ArvoreBackupComponent::salvarEsquemaComoPreset(const juce::String& nomePreset, juce::String& erro) const {
    juce::String nomeLimpo = juce::File::createLegalFileName(nomePreset.trim());
    if (nomeLimpo.isEmpty()) { erro = "empty preset name"; return false; }

    auto pasta = pastaPresets();
    if (!pasta.isDirectory() && !pasta.createDirectory()) {
        erro = "could not create " + pasta.getFullPathName();
        return false;
    }

    auto arquivo = pasta.getChildFile(nomeLimpo + ".json");
    if (!arquivo.replaceWithText(juce::JSON::toString(construirEsquemaAtualComoVar(), false))) {
        erro = "could not write " + arquivo.getFullPathName();
        return false;
    }
    return true;
}

void ArvoreBackupComponent::salvarPresetAutoAntes() const {
    juce::String erro; // melhor esforço — nunca bloqueia a ação principal por causa do backup automático
    salvarEsquemaComoPreset("auto_antes_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S"), erro);
}

void ArvoreBackupComponent::aplicarEsquemaDeVar(const juce::var& dados, int& itensRelocados, int& itensPulados) {
    itensRelocados = 0;
    itensPulados = 0;
    if (!dados.isObject()) return;

    // 1. Apaga o esquema atual inteiro — cascade (FK ON DELETE CASCADE) cuida
    // de subpastas e de acervo_item_pasta sozinho.
    for (const auto& n : nodes_)
        if (n.pastaPaiId.empty()) projeto_.apagarPastaAcervo(n.id);

    // 2. Recria pastas do preset em ordem pai-antes-de-filho, remapeando o id
    // antigo (gravado no preset) pro novo id que criarPastaAcervo devolve.
    std::map<std::string, std::string> idAntigoParaNovo;
    auto pastasVar = dados["pastas"];
    if (pastasVar.isArray()) {
        std::vector<juce::var> restantes(pastasVar.getArray()->begin(), pastasVar.getArray()->end());
        for (int rodada = 0; rodada < 64 && !restantes.empty(); ++rodada) {
            std::vector<juce::var> aindaNaoResolvidos;
            for (auto& p : restantes) {
                std::string idAntigo = p["id"].toString().toStdString();
                juce::var paiVar = p["pai"];
                std::string paiAntigo = paiVar.isVoid() ? std::string() : paiVar.toString().toStdString();

                std::optional<std::string> paiNovo;
                if (!paiAntigo.empty()) {
                    auto it = idAntigoParaNovo.find(paiAntigo);
                    if (it == idAntigoParaNovo.end()) { aindaNaoResolvidos.push_back(p); continue; }
                    paiNovo = it->second;
                }

                juce::String nome = p["nome"].toString();
                if (nome.isEmpty()) nome = i18n::t("arvore_backup.criar_pasta_padrao");
                std::string novoId = projeto_.criarPastaAcervo(nome.toStdString(), paiNovo);
                idAntigoParaNovo[idAntigo] = novoId;

                projeto_.atualizarPosicaoPastaAcervo(novoId, static_cast<int>(p["x"]), static_cast<int>(p["y"]));
                if (p.hasProperty("ativo") && !static_cast<bool>(p["ativo"]))
                    projeto_.alternarAtivoPastaAcervo(novoId, false);
            }
            restantes = std::move(aindaNaoResolvidos);
        }
        // Referência de pai quebrada (não devia acontecer, mas o preset pode
        // vir de fora) — a pasta órfã ainda assim entra, só que na raiz.
        for (auto& p : restantes) {
            std::string idAntigo = p["id"].toString().toStdString();
            juce::String nome = p["nome"].toString();
            if (nome.isEmpty()) nome = i18n::t("arvore_backup.criar_pasta_padrao");
            std::string novoId = projeto_.criarPastaAcervo(nome.toStdString(), std::nullopt);
            idAntigoParaNovo[idAntigo] = novoId;
            projeto_.atualizarPosicaoPastaAcervo(novoId, static_cast<int>(p["x"]), static_cast<int>(p["y"]));
        }
    }

    // 3. Recoloca os itens: por item_id quando é o mesmo projeto do preset,
    // por codigoAcervo quando é de outro (item_id original não existe aqui).
    auto itensVar = dados["itens"];
    if (itensVar.isArray()) {
        for (auto& it : *itensVar.getArray()) {
            std::string pastaAntiga = it["pastaId"].toString().toStdString();
            auto itPasta = idAntigoParaNovo.find(pastaAntiga);
            if (itPasta == idAntigoParaNovo.end()) { ++itensPulados; continue; }

            std::string itemId = it["itemId"].toString().toStdString();
            std::string codigoAcervo = it["codigoAcervo"].toString().toStdString();

            std::string t, tm, ca;
            std::optional<std::string> itemResolvido;
            if (!itemId.empty() && projeto_.obterItemInfo(itemId, t, tm, ca)) {
                itemResolvido = itemId;
            } else if (!codigoAcervo.empty()) {
                itemResolvido = projeto_.localizarItemPorCodigo(codigoAcervo);
            }

            if (!itemResolvido) { ++itensPulados; continue; }
            projeto_.adicionarItemAPastaSemRemoverOutras(*itemResolvido, itPasta->second);
            ++itensRelocados;
        }
    }

    recarregar();
}

void ArvoreBackupComponent::confirmarECarregarEsquema(const juce::String& nomeExibicao, const juce::var& dados) {
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle(i18n::t("arvore_backup.preset_carregar_titulo"))
            .withMessage(i18n::t("arvore_backup.preset_carregar_confirmar_msg").replace("{n}", nomeExibicao))
            .withButton(i18n::t("arvore_backup.preset_carregar_titulo"))
            .withButton(i18n::t("comum.cancelar")),
        [safeThis, dados, nomeExibicao](int res) {
            if (res != 1 || !safeThis) return;
            safeThis->salvarPresetAutoAntes();
            int relocados = 0, pulados = 0;
            safeThis->aplicarEsquemaDeVar(dados, relocados, pulados);
            juce::String msg = pulados > 0
                ? i18n::t("arvore_backup.preset_carregado").replace("{n}", nomeExibicao).replace("{s}", juce::String(pulados))
                : i18n::t("arvore_backup.preset_carregado_ok").replace("{n}", nomeExibicao);
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle(i18n::t("arvore_backup.preset_carregar_titulo"))
                    .withMessage(msg)
                    .withButton(i18n::t("comum.ok")),
                nullptr);
        });
}

void ArvoreBackupComponent::exportarPresetParaArquivo() {
    auto dados = construirEsquemaAtualComoVar();
    auto chooser = std::make_shared<juce::FileChooser>(
        i18n::t("arvore_backup.preset_exportar_titulo"),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("folder-preset.json"),
        "*.json");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [chooser, dados](const juce::FileChooser& fc) {
            juce::File destino = fc.getResult();
            if (destino == juce::File{}) return;
            if (!destino.hasFileExtension("json")) destino = destino.withFileExtension("json");
            bool ok = destino.replaceWithText(juce::JSON::toString(dados, false));
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon)
                    .withTitle(i18n::t("arvore_backup.preset_exportar_titulo"))
                    .withMessage(ok ? i18n::t("arvore_backup.preset_exportado") : i18n::t("arvore_backup.preset_falha_salvar"))
                    .withButton(i18n::t("comum.ok")),
                nullptr);
        });
}

void ArvoreBackupComponent::importarPresetDeArquivo() {
    auto chooser = std::make_shared<juce::FileChooser>(
        i18n::t("arvore_backup.preset_importar_titulo"),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.json");
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode,
        [safeThis, chooser](const juce::FileChooser& fc) {
            if (!safeThis) return;
            juce::File arquivo = fc.getResult();
            if (!arquivo.existsAsFile()) return;
            auto dados = juce::JSON::parse(arquivo);
            if (!dados.isObject()) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle(i18n::t("arvore_backup.preset_importar_titulo"))
                        .withMessage(i18n::t("arvore_backup.preset_falha_ler_arquivo"))
                        .withButton(i18n::t("comum.ok")),
                    nullptr);
                return;
            }
            safeThis->confirmarECarregarEsquema(arquivo.getFileNameWithoutExtension(), dados);
        });
}

void ArvoreBackupComponent::mostrarMenuPresets() {
    juce::PopupMenu menu;
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);

    menu.addItem(i18n::t("arvore_backup.preset_salvar"), [safeThis] {
        if (!safeThis) return;
        pedirTextoBackup(i18n::t("arvore_backup.preset_salvar_titulo"), i18n::t("arvore_backup.preset_salvar_msg"),
                         i18n::t("arvore_backup.preset_salvar_padrao"),
            [safeThis](std::optional<juce::String> nome) {
                if (!safeThis || !nome || nome->trim().isEmpty()) return;
                juce::String erro;
                bool ok = safeThis->salvarEsquemaComoPreset(nome->trim(), erro);
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon)
                        .withTitle(i18n::t("arvore_backup.preset_salvar_titulo"))
                        .withMessage(ok ? i18n::t("arvore_backup.preset_salvo").replace("{n}", nome->trim())
                                        : juce::String(i18n::t("arvore_backup.preset_falha_salvar")) + erro)
                        .withButton(i18n::t("comum.ok")),
                    nullptr);
            });
    });

    juce::PopupMenu submenuCarregar;
    auto presets = listarPresetsSalvos();
    for (auto& nome : presets) {
        submenuCarregar.addItem(nome, [safeThis, nome] {
            if (!safeThis) return;
            auto arquivo = safeThis->pastaPresets().getChildFile(juce::File::createLegalFileName(nome) + ".json");
            auto dados = juce::JSON::parse(arquivo);
            if (!dados.isObject()) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle(i18n::t("arvore_backup.btn_presets"))
                        .withMessage(i18n::t("arvore_backup.preset_falha_ler_arquivo"))
                        .withButton(i18n::t("comum.ok")),
                    nullptr);
                return;
            }
            safeThis->confirmarECarregarEsquema(nome, dados);
        });
    }
    menu.addSubMenu(i18n::t("arvore_backup.preset_carregar"), submenuCarregar, !presets.empty());

    menu.addSeparator();
    menu.addItem(i18n::t("arvore_backup.preset_exportar"), [safeThis] { if (safeThis) safeThis->exportarPresetParaArquivo(); });
    menu.addItem(i18n::t("arvore_backup.preset_importar"), [safeThis] { if (safeThis) safeThis->importarPresetDeArquivo(); });

    menu.showMenuAsync(juce::PopupMenu::Options());
}

} // namespace matriz::ui
