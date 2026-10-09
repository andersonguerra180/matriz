#include "ArvoreBackupComponent.h"
#include "../I18n/Strings.h"
#include "../Ingest/FluxoLote.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Vault/Resolucao.h"
#include "HierarquiaEditorComponent.h"
#include "LayoutArvoreFolderMap.h"
#include "ModalMitigacao.h"
#include "ProgressoGlobal.h"
#include "Tokens.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

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
                    // Aplica direto (não depende do aviso do ColourSelector, que não dispara quando a
                    // cor do seletor já é igual) e só atualiza o seletor para exibir, sem notificar de novo.
                    const auto cor = historico_[static_cast<size_t>(i)];
                    seletor_.setCurrentColour(cor, juce::dontSendNotification);
                    if (onChange_) onChange_(cor);
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

// ── Janela flutuante NEW FOLDERS - ACTION NEEDED (sobre o stage) ──────────────────────────────────────────────
// Só é mostrada quando há proposta pendente. O cabeçalho recolhe/expande; as linhas (rótulo + 3 botões) moram
// no `conteudo()`, dentro de um Viewport (rola se houver muitas propostas).
class PainelPropostas : public juce::Component {
public:
    static constexpr int kAlturaCabecalho = 28;

    static constexpr int kAlturaRodape = 34;

    PainelPropostas() {
        vp_.setViewedComponent(&conteudo_, false);
        vp_.setScrollBarsShown(true, false);
        addAndMakeVisible(vp_);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        for (auto* b : {&usarTodas, &mesclarTodas, &descartarTodas}) addAndMakeVisible(*b);
        usarTodas.setButtonText(i18n::t("arvore_backup.btn_usar_todas"));
        mesclarTodas.setButtonText(i18n::t("arvore_backup.btn_mesclar_todas"));
        descartarTodas.setButtonText(i18n::t("arvore_backup.btn_descartar_todas"));
        usarTodas.setTooltip(i18n::t("arvore_backup.usar_todas_dica"));
        mesclarTodas.setTooltip(i18n::t("arvore_backup.mesclar_todas_dica"));
        descartarTodas.setTooltip(i18n::t("arvore_backup.descartar_todas_dica"));
        usarTodas.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22c55e));
        mesclarTodas.setColour(juce::TextButton::buttonColourId, tema().acento);
        descartarTodas.setColour(juce::TextButton::buttonColourId, tema().perigo);
        for (auto* b : {&usarTodas, &mesclarTodas, &descartarTodas}) b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }

    juce::TextButton usarTodas, mesclarTodas, descartarTodas;

    juce::Component& conteudo() { return conteudo_; }
    std::function<void()> aoAlternar;

    void definir(const juce::String& titulo, bool recolhido) {
        titulo_ = titulo;
        recolhido_ = recolhido;
        vp_.setVisible(!recolhido);
        for (auto* b : {&usarTodas, &mesclarTodas, &descartarTodas}) b->setVisible(!recolhido);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat();
        g.setColour(tk.painel);
        g.fillRoundedRectangle(r, tk.raioMedio);
        g.setColour(tk.alerta.withAlpha(0.18f));
        g.fillRoundedRectangle(r, tk.raioMedio);
        g.setColour(tk.alerta);
        g.drawRoundedRectangle(r.reduced(1.0f), tk.raioMedio, 2.5f);
        auto cab = getLocalBounds().removeFromTop(kAlturaCabecalho).reduced(10, 0);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(recolhido_ ? juce::String::fromUTF8("\xe2\x96\xb8") : juce::String::fromUTF8("\xe2\x96\xbe"),
                   cab.removeFromRight(14), juce::Justification::centred);
        g.drawText(titulo_, cab, juce::Justification::centredLeft, true);
    }

    void resized() override {
        auto area = getLocalBounds().withTrimmedTop(kAlturaCabecalho).reduced(8, 4);
        auto rodape = area.removeFromBottom(kAlturaRodape - 8);
        area.removeFromBottom(4);
        vp_.setBounds(area);
        const int w = (rodape.getWidth() - 8) / 3;
        usarTodas.setBounds(rodape.removeFromLeft(w));
        rodape.removeFromLeft(4);
        mesclarTodas.setBounds(rodape.removeFromLeft(w));
        rodape.removeFromLeft(4);
        descartarTodas.setBounds(rodape);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.y < kAlturaCabecalho && aoAlternar) aoAlternar();
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}  // não vaza pro zoom do canvas

    int larguraConteudo() const { return std::max(0, vp_.getMaximumVisibleWidth()); }

private:
    juce::Viewport vp_;
    juce::Component conteudo_;
    juce::String titulo_;
    bool recolhido_ = false;
};

// ── Explorer de pastas (só pastas, nunca arquivos) ────────────────────────────────────────────────────────────
class ItemPastaExplorador : public juce::TreeViewItem {
public:
    ItemPastaExplorador(std::string id, juce::String nome, int itens, bool ativo,
                        std::function<void(const std::string&)> aoSelecionar,
                        std::function<void(const std::string&)> aoLocalizar,
                        std::function<void(ItemPastaExplorador&)> aoAbrir = nullptr,
                        std::function<void(const juce::MouseEvent&)> aoMenu = nullptr)
        : id_(std::move(id)), nome_(std::move(nome)), itens_(itens), ativo_(ativo),
          aoSelecionar_(std::move(aoSelecionar)), aoLocalizar_(std::move(aoLocalizar)), aoAbrir_(std::move(aoAbrir)),
          aoMenu_(std::move(aoMenu)) {}

    // Nome inteiro + contagem logo depois dele: a largura do item é a do texto (a árvore ganha rolagem horizontal).
    juce::String texto() const {
        return juce::String::fromUTF8("\xf0\x9f\x93\x81 ") + nome_ + (itens_ > 0 ? "   " + juce::String(itens_) : juce::String());
    }
    static juce::Font fonte(bool negrito) { return juce::Font(juce::FontOptions(11.0f, negrito ? juce::Font::bold : juce::Font::plain)); }
    int getItemWidth() const override { return juce::GlyphArrangement::getStringWidthInt(fonte(true), texto()) + 12; }
    void itemOpennessChanged(bool aberto) override { if (aberto && aoAbrir_) aoAbrir_(*this); }

    const std::string& id() const { return id_; }
    bool mightContainSubItems() override { return getNumSubItems() > 0; }
    juce::String getUniqueName() const override { return id_.empty() ? juce::String("raiz") : juce::String(id_); }
    int getItemHeight() const override { return 20; }

    void paintItem(juce::Graphics& g, int width, int height) override {
        const auto& tk = tema();
        if (isSelected()) {
            g.setColour(tk.acento.withAlpha(0.25f));
            g.fillRect(0, 0, width, height);
        }
        g.setColour(ativo_ ? tk.textoPrimario : tk.textoTerciario);
        g.setFont(fonte(isSelected()));
        g.drawText(texto(), juce::Rectangle<int>(0, 0, width, height).reduced(2, 0), juce::Justification::centredLeft, false);
    }

    void itemSelectionChanged(bool selecionado) override {
        if (selecionado && aoSelecionar_) aoSelecionar_(id_);
    }
    void itemDoubleClicked(const juce::MouseEvent&) override {
        if (aoLocalizar_) aoLocalizar_(id_);
    }
    void itemClicked(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && aoMenu_) aoMenu_(e);
    }

private:
    std::string id_;
    juce::String nome_;
    int itens_;
    bool ativo_;
    std::function<void(const std::string&)> aoSelecionar_, aoLocalizar_;
    std::function<void(ItemPastaExplorador&)> aoAbrir_;
    std::function<void(const juce::MouseEvent&)> aoMenu_;
};

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


    btnAutoArranjar_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_auto_arranjar"));
    btnAutoArranjar_->onClick = [this] { autoArranjar(); };
    addAndMakeVisible(*btnAutoArranjar_);

    btnZoomIn_ = std::make_unique<juce::TextButton>("+");
    btnZoomIn_->onClick = [this] { aplicarZoom(zoom_ * 1.2f, areaCanvas().withZeroOrigin().getCentre().toFloat()); };
    addAndMakeVisible(*btnZoomIn_);

    btnZoomOut_ = std::make_unique<juce::TextButton>("-");
    btnZoomOut_->onClick = [this] { aplicarZoom(zoom_ / 1.2f, areaCanvas().withZeroOrigin().getCentre().toFloat()); };
    addAndMakeVisible(*btnZoomOut_);

    btnZoomFit_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_fit"));
    btnZoomFit_->onClick = [this] { enquadrarTudo(); };
    addAndMakeVisible(*btnZoomFit_);

    btnResetTamanhos_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_reset_tamanhos"));
    btnResetTamanhos_->setTooltip(i18n::t("arvore_backup.reset_tamanhos_dica"));
    btnResetTamanhos_->onClick = [this] { redefinirTamanhos(); };
    addAndMakeVisible(*btnResetTamanhos_);

    comboMapas_ = std::make_unique<juce::ComboBox>();
    // Mesmo tratamento de combo das outras abas.
    comboMapas_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboMapas_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboMapas_->setColour(juce::ComboBox::outlineColourId, tema().borda);
    comboMapas_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    comboMapas_->onChange = [this] {
        int idx = comboMapas_->getSelectedItemIndex();
        if (idx >= 0 && idx < static_cast<int>(mapasCache_.size())) selecionarMapaPorId(mapasCache_[static_cast<size_t>(idx)].id);
    };
    addAndMakeVisible(*comboMapas_);

    btnMenuMapa_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xE2\x8B\xAF")); // "⋯"
    btnMenuMapa_->onClick = [this] { mostrarMenuMapa(); };
    addAndMakeVisible(*btnMenuMapa_);

    btnNovoMapa_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_novo_mapa"));
    btnNovoMapa_->onClick = [this] { mostrarDialogoNovoMapa(); };
    addAndMakeVisible(*btnNovoMapa_);

    sliderTamanho_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    sliderTamanho_->setRange(50.0, 500.0, 1.0);
    sliderTamanho_->setValue(100.0, juce::dontSendNotification);
    sliderTamanho_->setDoubleClickReturnValue(true, 100.0);
    sliderTamanho_->setTextValueSuffix("%");
    sliderTamanho_->setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 20);
    sliderTamanho_->setTooltip(i18n::t("arvore_backup.slider_tamanho_tooltip"));
    sliderTamanho_->onValueChange = [this] { aplicarSlider(); };
    sliderTamanho_->onDragEnd = [this] { persistirEscalas(); };
    addAndMakeVisible(*sliderTamanho_);

    auto criarSpread = [this](std::unique_ptr<juce::Label>& rotulo, std::unique_ptr<juce::Slider>& slider, const char* chaveRotulo,
                              const char* chaveDica, float& fator) {
        rotulo = std::make_unique<juce::Label>(juce::String(), i18n::t(chaveRotulo));
        rotulo->setFont(juce::Font(juce::FontOptions(10.0f)));
        rotulo->setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(*rotulo);
        slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        // Começa no MEIO, valendo 0 (= sem espalhar). Direita espalha, esquerda comprime: fator = 10^(v/100),
        // então +100 = 10x (o antigo máximo de 1000%) e -100 = 0,1x.
        slider->setRange(-100.0, 100.0, 1.0);
        slider->setValue(0.0, juce::dontSendNotification);
        slider->setDoubleClickReturnValue(true, 0.0);
        slider->textFromValueFunction = [](double v) {
            const int n = juce::roundToInt(v);
            return (n > 0 ? "+" : "") + juce::String(n);
        };
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
        slider->setTooltip(i18n::t(chaveDica));
        auto* s = slider.get();
        slider->onValueChange = [this, s, &fator] {
            fator = static_cast<float>(std::pow(10.0, s->getValue() / 100.0));
            aplicarEscalaTamanho(escalaTamanho_);
        };
        addAndMakeVisible(*slider);
    };
    criarSpread(rotuloSpread_, sliderSpread_, "arvore_backup.spread_rotulo", "arvore_backup.spread_tooltip", espalharX_);
    criarSpread(rotuloSpreadV_, sliderSpreadV_, "arvore_backup.spread_v_rotulo", "arvore_backup.spread_v_tooltip", espalharY_);

    detailContent_ = std::make_unique<TreeDetailContent>();
    detailViewport_ = std::make_unique<juce::Viewport>();
    detailViewport_->setViewedComponent(detailContent_.get(), false);
    detailViewport_->setScrollBarsShown(true, false);
    detailViewport_->setVisible(false);
    addAndMakeVisible(*detailViewport_);

    painelPropostas_ = std::make_unique<PainelPropostas>();
    painelPropostas_->aoAlternar = [this] {
        painelPropostasRecolhido_ = !painelPropostasRecolhido_;
        posicionarPainelPropostas();
    };
    painelPropostas_->usarTodas.onClick = [this] { usarTodasPropostas(); };
    painelPropostas_->mesclarTodas.onClick = [this] { mesclarTodasPropostas(); };
    painelPropostas_->descartarTodas.onClick = [this] { confirmarDescarteTodasPropostas(); };
    addChildComponent(*painelPropostas_);

    treeExplorador_ = std::make_unique<juce::TreeView>();
    treeExplorador_->setRootItemVisible(false);
    treeExplorador_->setMultiSelectEnabled(true);  // shift/cmd+clique: seleciona 2 pastas pro MERGE
    treeExplorador_->setDefaultOpenness(false);
    treeExplorador_->setIndentSize(14);
    treeExplorador_->getViewport()->setScrollBarsShown(true, true);
    treeExplorador_->setColour(juce::TreeView::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(*treeExplorador_);

    btnFind_ = std::make_unique<juce::TextButton>(i18n::t("arvore_backup.btn_find"));
    btnFind_->setTooltip(i18n::t("arvore_backup.find_dica"));
    btnFind_->setEnabled(false);
    btnFind_->onClick = [this] { if (!pastaExploradorId_.empty()) localizarPasta(pastaExploradorId_); };
    addAndMakeVisible(*btnFind_);

    setWantsKeyboardFocus(true);
    setOpaque(true);  // o paint faz fillAll do fundo inteiro

    migrarPresetsAntigosSeNecessario();
    mapaAtivoId_ = projeto_.mapaInicialDoFolderMap();
    projeto_.definirMapaAtivo(mapaAtivoId_);  // mutadores compat seguem o mapa aberto
    recarregarComboMapas();
    atualizarEstadoBotoesParaMapa();
    recarregar();

    EventBus::obterInstancia().registrarListener(this);
}

ArvoreBackupComponent::~ArvoreBackupComponent() {
    autoRolagem_.stopTimer();
    limparDicaPasta();
    try { persistirEscalas(); } catch (...) {}
    EventBus::obterInstancia().removerListener(this);
    detailViewport_->setViewedComponent(nullptr, false);
    if (treeExplorador_) treeExplorador_->setRootItem(nullptr);
}

// ---- Folder Map SOBREPOSTO (duplo clique num folder-card) --------------------------------------------------------
namespace {
// Camada sobre o content da janela: fundo escurecido (bloqueia cliques no resto do app), painel de 90% com o título
// e o botão SAIR no canto superior direito, e um ArvoreBackupComponent em modo sobreposto.
class SobreposicaoFolderMap : public juce::Component, private juce::ComponentListener {
public:
    SobreposicaoFolderMap(ProjetoAberto& projeto, juce::Component& host, const std::string& mapaId, const std::string& pastaId,
                          const juce::String& nome, std::function<void()> aoSair)
        : host_(&host), aoSair_(std::move(aoSair)) {
        mapa_ = std::make_unique<ArvoreBackupComponent>(projeto);
        addAndMakeVisible(*mapa_);
        titulo_.setText(i18n::t("arvore_backup.sobreposto_titulo").replace("{nome}", nome), juce::dontSendNotification);
        titulo_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        titulo_.setColour(juce::Label::textColourId, tema().textoPrimario);
        addAndMakeVisible(titulo_);
        btnSair_.setButtonText(i18n::t("arvore_backup.sobreposto_sair"));
        btnSair_.setColour(juce::TextButton::buttonColourId, tema().acento);
        btnSair_.setColour(juce::TextButton::textColourOffId, tema().textoSobreAcento);
        btnSair_.onClick = [this] { if (aoSair_) aoSair_(); };
        addAndMakeVisible(btnSair_);
        host.addComponentListener(this);
        setBounds(host.getLocalBounds());
        mapa_->definirModoSobreposto(mapaId, pastaId, nome);
    }
    ~SobreposicaoFolderMap() override {
        if (host_ != nullptr) host_->removeComponentListener(this);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black.withAlpha(0.55f));
        const auto& tk = tema();
        g.setColour(tk.fundo);
        g.fillRoundedRectangle(painel().toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(painel().toFloat(), tk.raioMedio, 1.0f);
    }
    void resized() override {
        auto p = painel().reduced(8);
        auto topo = p.removeFromTop(32);
        btnSair_.setBounds(topo.removeFromRight(110).reduced(0, 2));
        titulo_.setBounds(topo);
        p.removeFromTop(6);
        mapa_->setBounds(p);
    }

private:
    juce::Rectangle<int> painel() const {
        auto r = getLocalBounds();
        return r.withSizeKeepingCentre(juce::roundToInt(r.getWidth() * 0.9f), juce::roundToInt(r.getHeight() * 0.9f));
    }
    void componentMovedOrResized(juce::Component& c, bool, bool) override { setBounds(c.getLocalBounds()); }

    juce::Component::SafePointer<juce::Component> host_;
    std::function<void()> aoSair_;
    std::unique_ptr<ArvoreBackupComponent> mapa_;
    juce::Label titulo_;
    juce::TextButton btnSair_;
};
}  // namespace

void ArvoreBackupComponent::abrirSobreposicao(const std::string& pastaId, const juce::String& nomePasta) {
    if (sobreposto_ || sobreposicao_ != nullptr) return;
    finalizarEdicaoInline();
    juce::Component* host = getTopLevelComponent();
    if (auto* janela = dynamic_cast<juce::ResizableWindow*>(host))
        if (auto* conteudo = janela->getContentComponent()) host = conteudo;
    if (host == nullptr) return;
    persistirEscalas();  // tamanhos ajustados aqui antes de abrir
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    sobreposicao_ = std::make_unique<SobreposicaoFolderMap>(projeto_, *host, mapaAtivoId_, pastaId, nomePasta, [safeThis] {
        // Fora do clique do próprio botão SAIR (ele é filho do que vai ser destruído).
        juce::MessageManager::callAsync([safeThis] { if (safeThis) safeThis->fecharSobreposicao(); });
    });
    host->addAndMakeVisible(*sobreposicao_);
    sobreposicao_->toFront(true);
}

void ArvoreBackupComponent::fecharSobreposicao() {
    sobreposicao_.reset();
    recarregar();  // o que mudou lá dentro (pastas novas, nomes, conexões) aparece aqui
    grabKeyboardFocus();
}

void ArvoreBackupComponent::definirModoSobreposto(const std::string& mapaId, const std::string& pastaRaizId,
                                                  const juce::String& nomePasta) {
    sobreposto_ = true;
    raizSobreposicaoId_ = pastaRaizId;
    if (mapaId != mapaAtivoId_) {  // o mesmo mapa aberto na tela principal
        mapaAtivoId_ = mapaId;
        projeto_.definirMapaAtivo(mapaId);
        recarregarComboMapas();
        atualizarEstadoBotoesParaMapa();
    }
    // Mesma filtragem das abas (pasta + descendentes), sem a barra de abas.
    AbaEstrutura aba;
    aba.pastaRaizId = pastaRaizId;
    aba.titulo = nomePasta;
    abas_.push_back(aba);
    abaAtiva_ = static_cast<int>(abas_.size()) - 1;
    // Trocar de mapa aqui dentro não faz sentido: a raiz é uma pasta do mapa aberto.
    for (juce::Component* c : {static_cast<juce::Component*>(comboMapas_.get()), static_cast<juce::Component*>(btnMenuMapa_.get()),
                               static_cast<juce::Component*>(btnNovoMapa_.get())})
        if (c) c->setEnabled(false);
    enquadrarPendente_ = true;
    resized();
    recarregar();
}

void ArvoreBackupComponent::aoItemAlterado(const EventoItemAlterado& e) {
    if (e.tipoAlteracao != "titulo") return;
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    std::string itemId = e.itemId;
    std::vector<std::string> itemIds = e.itemIds;  // evento de lote (renomear em N itens)
    juce::MessageManager::callAsync([safeThis, itemId, itemIds] {
        if (!safeThis || safeThis->selectedFolderId_.empty()) return;
        for (const auto& n : safeThis->nodes_) {
            if (n.id == safeThis->selectedFolderId_) {
                bool afeta = n.itemIdsDiretos.count(itemId) > 0;
                for (size_t i = 0; !afeta && i < itemIds.size(); ++i) afeta = n.itemIdsDiretos.count(itemIds[i]) > 0;
                if (afeta) safeThis->atualizarPainelDetalhe(safeThis->selectedFolderId_, true);
                break;
            }
        }
    });
}

// ---- NEW FOLDERS: árvore proposta por pasta ingerida inteira -------------------------------------------------
namespace {
// Gestão de conflitos da mescla, no estilo de Duplicates: uma linha por pasta com o mesmo nome, cada uma com
// "mesclar com a existente" ou "manter como pasta separada".
class DialogoMescla : public juce::Component {
public:
    // alvoFixo != "" = MERGE entre duas pastas existentes (menu do canvas/explorer): o destino já está escolhido.
    DialogoMescla(ProjetoAberto& proj, std::string raizId, juce::String nome, std::function<void()> aoConcluir,
                  std::string alvoFixo = {}, juce::String alvoCaminho = {})
        : proj_(proj), raizId_(std::move(raizId)), aoConcluir_(std::move(aoConcluir)), entrePastas_(!alvoFixo.empty()) {
        if (entrePastas_) {
            ProjetoAberto::PastaDoMapa d;
            d.id = alvoFixo;
            d.caminho = std::move(alvoCaminho);
            d.mesmoNome = false;
            destinos_.push_back(std::move(d));
        } else {
            destinos_ = proj_.destinosDeMescla(raizId_);
        }
        lblAlvo_.setText(i18n::t("mescla.alvo").replace("{nome}", nome), juce::dontSendNotification);
        addAndMakeVisible(lblAlvo_);
        comboAlvo_.addItem(i18n::t("mescla.escolha"), 1);
        for (size_t i = 0; i < destinos_.size(); ++i)
            comboAlvo_.addItem((destinos_[i].mesmoNome ? juce::String(juce::CharPointer_UTF8("\xe2\x98\x85 ")) : juce::String()) +
                                   destinos_[i].caminho, static_cast<int>(i) + 2);
        comboAlvo_.setSelectedId(!destinos_.empty() && (entrePastas_ || destinos_[0].mesmoNome) ? 2 : 1, juce::dontSendNotification);
        comboAlvo_.setEnabled(!entrePastas_);
        comboAlvo_.onChange = [this] { reconstruir(); };
        addAndMakeVisible(comboAlvo_);
        addAndMakeVisible(lblResumo_);
        lblResumo_.setJustificationType(juce::Justification::topLeft);
        viewport_.setViewedComponent(&linhas_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
        for (auto* b : {&btnTodosMesclar_, &btnTodosSeparar_, &btnAplicar_, &btnCancelar_}) addAndMakeVisible(*b);
        btnTodosMesclar_.setButtonText(i18n::t("mescla.todos_mesclar"));
        btnTodosSeparar_.setButtonText(i18n::t("mescla.todos_separar"));
        btnAplicar_.setButtonText(i18n::t("mescla.btn_aplicar"));
        btnCancelar_.setButtonText(i18n::t("mescla.btn_cancelar"));
        btnTodosMesclar_.onClick = [this] { definirTodos(true); };
        btnTodosSeparar_.onClick = [this] { definirTodos(false); };
        btnCancelar_.onClick = [this] { fechar(); };
        btnAplicar_.onClick = [this] {
            const auto alvo = alvoId();
            if (alvo.empty()) return;
            if (entrePastas_) proj_.mesclarPastas(raizId_, alvo, decisoes_);
            else proj_.mesclarProposta(raizId_, alvo, decisoes_);
            auto cb = aoConcluir_;
            fechar();
            if (cb) juce::MessageManager::callAsync(cb);
        };
        setSize(720, 500);
        reconstruir();
    }

    static void exibir(ProjetoAberto& proj, const std::string& raizId, const juce::String& nome, std::function<void()> aoConcluir,
                       const std::string& alvoFixo = {}, const juce::String& alvoCaminho = {}) {
        juce::DialogWindow::LaunchOptions opt;
        opt.dialogTitle = alvoFixo.empty() ? i18n::t("mescla.titulo") : i18n::t("mescla.titulo_pastas");
        opt.content.setOwned(new DialogoMescla(proj, raizId, nome, std::move(aoConcluir), alvoFixo, alvoCaminho));
        opt.dialogBackgroundColour = tema().fundo;
        opt.escapeKeyTriggersCloseButton = true;
        opt.useNativeTitleBar = true;
        opt.resizable = false;
        opt.launchAsync();
    }

    void resized() override {
        auto a = getLocalBounds().reduced(20);
        lblAlvo_.setBounds(a.removeFromTop(20));
        comboAlvo_.setBounds(a.removeFromTop(28));
        a.removeFromTop(10);
        auto rodape = a.removeFromBottom(36);
        a.removeFromBottom(8);
        auto bulk = a.removeFromBottom(28);
        a.removeFromBottom(6);
        lblResumo_.setBounds(a.removeFromTop(34));
        viewport_.setBounds(a);
        linhas_.setSize(viewport_.getMaximumVisibleWidth(), std::max(viewport_.getHeight(), alturaLinhas_));
        btnCancelar_.setBounds(rodape.removeFromRight(130));
        rodape.removeFromRight(8);
        btnAplicar_.setBounds(rodape.removeFromRight(130));
        btnTodosMesclar_.setBounds(bulk.removeFromLeft(170));
        bulk.removeFromLeft(8);
        btnTodosSeparar_.setBounds(bulk.removeFromLeft(210));
        organizarLinhas();
    }

private:
    struct Linha { std::string propostaId; std::unique_ptr<juce::Label> rotulo; std::unique_ptr<juce::ComboBox> opcao; };

    std::string alvoId() const {
        const int id = comboAlvo_.getSelectedId();
        return id >= 2 && static_cast<size_t>(id - 2) < destinos_.size() ? destinos_[static_cast<size_t>(id - 2)].id : std::string();
    }
    void fechar() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
    }
    void definirTodos(bool mesclar) {
        for (const auto& l : plano_) decisoes_[l.propostaId] = mesclar;
        reconstruir();
    }
    void reconstruir() {
        linhasUi_.clear();
        linhas_.removeAllChildren();
        const auto alvo = alvoId();
        plano_ = entrePastas_ ? proj_.planoDeMesclaPastas(raizId_, alvo, decisoes_) : proj_.planoDeMescla(raizId_, alvo, decisoes_);
        btnAplicar_.setEnabled(!alvo.empty());
        btnTodosMesclar_.setEnabled(!plano_.empty());
        btnTodosSeparar_.setEnabled(!plano_.empty());
        lblResumo_.setText(alvo.empty() ? juce::String() : (plano_.empty() ? i18n::t("mescla.sem_conflitos") : i18n::t("mescla.conflitos")),
                           juce::dontSendNotification);
        juce::Component::SafePointer<DialogoMescla> safe(this);
        for (const auto& l : plano_) {
            Linha u;
            u.propostaId = l.propostaId;
            u.rotulo = std::make_unique<juce::Label>();
            u.rotulo->setText(i18n::t("mescla.linha").replace("{p}", l.caminhoProposta).replace("{a}", l.caminhoAlvo)
                                  .replace("{n}", juce::String(l.arquivos)), juce::dontSendNotification);
            u.rotulo->setMinimumHorizontalScale(0.7f);
            u.opcao = std::make_unique<juce::ComboBox>();
            u.opcao->addItem(i18n::t("mescla.opt_mesclar"), 1);
            u.opcao->addItem(i18n::t("mescla.opt_separar"), 2);
            u.opcao->setSelectedId(l.mesclar ? 1 : 2, juce::dontSendNotification);
            const std::string id = l.propostaId;
            auto* combo = u.opcao.get();
            u.opcao->onChange = [safe, id, combo] {
                if (!safe) return;
                safe->decisoes_[id] = combo->getSelectedId() == 1;
                juce::MessageManager::callAsync([safe] { if (safe) safe->reconstruir(); });  // sem destruir o combo dentro do próprio callback
            };
            linhas_.addAndMakeVisible(*u.rotulo);
            linhas_.addAndMakeVisible(*u.opcao);
            linhasUi_.push_back(std::move(u));
        }
        alturaLinhas_ = static_cast<int>(linhasUi_.size()) * 56;
        resized();
    }
    void organizarLinhas() {
        int y = 0;
        const int w = linhas_.getWidth();
        for (auto& u : linhasUi_) {
            u.rotulo->setBounds(0, y, w, 22);
            u.opcao->setBounds(0, y + 24, std::min(w, 320), 26);
            y += 56;
        }
    }

    ProjetoAberto& proj_;
    std::string raizId_;
    std::function<void()> aoConcluir_;
    bool entrePastas_ = false;
    std::vector<ProjetoAberto::PastaDoMapa> destinos_;
    std::vector<ProjetoAberto::LinhaMescla> plano_;
    std::map<std::string, bool> decisoes_;
    std::vector<Linha> linhasUi_;
    int alturaLinhas_ = 0;
    juce::Label lblAlvo_, lblResumo_;
    juce::ComboBox comboAlvo_;
    juce::Viewport viewport_;
    juce::Component linhas_;
    juce::TextButton btnTodosMesclar_, btnTodosSeparar_, btnAplicar_, btnCancelar_;
};
}  // namespace

void ArvoreBackupComponent::reconstruirPropostas() {
    propostas_.clear();  // os filhos saem sozinhos do componente
    if (mapaAtivoEhOriginal()) return;
    juce::Component::SafePointer<ArvoreBackupComponent> safe(this);
    for (const auto& p : projeto_.listarPropostas(mapaAtivoId_)) {
        LinhaProposta l;
        l.raizId = p.raizId;
        l.arquivos = p.arquivos;
        l.rotulo = std::make_unique<juce::Label>();
        l.rotulo->setText(i18n::t("arvore_backup.nova_linha").replace("{nome}", p.nome).replace("{n}", juce::String(p.arquivos)),
                          juce::dontSendNotification);
        l.rotulo->setMinimumHorizontalScale(0.7f);
        auto botao = [this](const juce::String& texto, const juce::String& dica) {
            auto b = std::make_unique<juce::TextButton>(texto);
            b->setTooltip(dica);
            painelPropostas_->conteudo().addAndMakeVisible(*b);
            return b;
        };
        l.usar = botao(i18n::t("arvore_backup.btn_usar_arvore"), i18n::t("arvore_backup.usar_dica"));
        l.mesclar = botao(i18n::t("arvore_backup.btn_mesclar"), i18n::t("arvore_backup.mesclar_dica"));
        l.descartar = botao(i18n::t("arvore_backup.btn_descartar"), i18n::t("arvore_backup.descartar_dica"));
        // Botões coloridos pra ler a ação de longe: verde = usar, acento = mesclar, vermelho = descartar.
        auto cor = [](juce::TextButton& b, juce::Colour c) {
            b.setColour(juce::TextButton::buttonColourId, c);
            b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        };
        cor(*l.usar, juce::Colour(0xff22c55e));
        cor(*l.mesclar, tema().acento);
        cor(*l.descartar, tema().perigo);
        l.rotulo->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        const std::string id = p.raizId;
        const juce::String nome = p.nome;
        const int arquivos = p.arquivos;
        l.usar->onClick = [safe, id] {
            juce::MessageManager::callAsync([safe, id] {
                if (!safe) return;
                safe->projeto_.usarArvoreProposta(id);
                safe->recarregar();
            });
        };
        l.mesclar->onClick = [safe, id, nome] {
            juce::MessageManager::callAsync([safe, id, nome] { if (safe) safe->mostrarDialogoMescla(id, nome); });
        };
        l.descartar->onClick = [safe, id, arquivos] {
            juce::MessageManager::callAsync([safe, id, arquivos] { if (safe) safe->confirmarDescarteProposta(id, arquivos); });
        };
        painelPropostas_->conteudo().addAndMakeVisible(*l.rotulo);
        propostas_.push_back(std::move(l));
    }
    if (propostas_.size() > ultimoTotalPropostas_) painelPropostasRecolhido_ = false;  // chegou proposta nova: abre
    ultimoTotalPropostas_ = propostas_.size();
    resized();
}

// Janela flutuante: canto superior esquerdo do stage, só com proposta pendente (e nunca no ORIGINAL).
bool ArvoreBackupComponent::painelPropostasVisivel() const {
    return painelPropostas_ && painelPropostas_->isVisible() && !painelPropostas_->getBounds().isEmpty();
}

ArvoreBackupComponent* ArvoreBackupComponent::mapaDaSobreposicao() const {
    if (sobreposicao_ != nullptr)
        for (auto* filho : sobreposicao_->getChildren())
            if (auto* a = dynamic_cast<ArvoreBackupComponent*>(filho)) return a;
    return nullptr;
}

void ArvoreBackupComponent::posicionarPainelPropostas() {
    if (!painelPropostas_) return;
    const bool visivel = !mapaAtivoEhOriginal() && !propostas_.empty();
    painelPropostas_->setVisible(visivel);
    if (!visivel) return;

    constexpr int kMargem = 16, kLinha = 50, kLarguraMax = 340;
    const auto canvas = areaCanvas();
    const int largura = std::min(kLarguraMax, std::max(220, canvas.getWidth() - 2 * kMargem));
    const int alturaConteudo = static_cast<int>(propostas_.size()) * kLinha + 4;
    const int alturaMax = std::max(PainelPropostas::kAlturaCabecalho + PainelPropostas::kAlturaRodape + 60, canvas.getHeight() - 2 * kMargem);
    const int altura = painelPropostasRecolhido_
                           ? PainelPropostas::kAlturaCabecalho
                           : std::min(alturaMax, PainelPropostas::kAlturaCabecalho + PainelPropostas::kAlturaRodape + 8 + alturaConteudo);
    painelPropostas_->setBounds(canvas.getX() + kMargem, canvas.getY() + kMargem, largura, altura);
    painelPropostas_->definir(juce::String::fromUTF8("\xe2\x9a\xa0 ") + i18n::t("arvore_backup.card_novas_acao") + " (" +
                                  juce::String(propostas_.size()) + ")",
                              painelPropostasRecolhido_);

    const int larguraLinhas = std::max(100, painelPropostas_->larguraConteudo());
    painelPropostas_->conteudo().setSize(larguraLinhas, alturaConteudo);
    int y = 2;
    for (auto& p : propostas_) {
        juce::Rectangle<int> area(0, y, larguraLinhas, kLinha);
        if (p.rotulo) p.rotulo->setBounds(area.removeFromTop(16));
        area.removeFromTop(2);
        auto linha = area.removeFromTop(24);
        const int w = (linha.getWidth() - 8) / 3;
        if (p.usar) p.usar->setBounds(linha.removeFromLeft(w));
        linha.removeFromLeft(4);
        if (p.mesclar) p.mesclar->setBounds(linha.removeFromLeft(w));
        linha.removeFromLeft(4);
        if (p.descartar) p.descartar->setBounds(linha);
        y += kLinha;
    }
}

void ArvoreBackupComponent::mostrarDialogoMescla(const std::string& raizId, const juce::String& nome) {
    juce::Component::SafePointer<ArvoreBackupComponent> safe(this);
    DialogoMescla::exibir(projeto_, raizId, nome, [safe] { if (safe) safe->recarregar(); });
}

// MERGE entre duas pastas existentes: a origem some e o conteúdo dela vai pro alvo (mesmo diálogo de conflitos da mescla
// de pasta nova do INTAKE).
void ArvoreBackupComponent::iniciarMesclaPastas(const std::string& origemId, const std::string& alvoId) {
    if (mapaAtivoEhOriginal() || !projeto_.podeMesclarPastas(origemId, alvoId)) {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle(i18n::t("mescla.titulo_pastas"))
                                         .withMessage(i18n::t("mescla.pastas_invalido"))
                                         .withButton(i18n::t("dialogo.ok")),
                                     nullptr);
        return;
    }
    juce::String nomeOrigem, caminhoAlvo;
    for (const auto& n : nodes_)
        if (n.id == origemId) nomeOrigem = n.nome;
    std::function<juce::String(const std::string&)> caminho = [&](const std::string& id) -> juce::String {
        for (const auto& n : nodes_)
            if (n.id == id) return n.pastaPaiId.empty() ? n.nome : caminho(n.pastaPaiId) + " / " + n.nome;
        return {};
    };
    caminhoAlvo = caminho(alvoId);
    juce::Component::SafePointer<ArvoreBackupComponent> safe(this);
    DialogoMescla::exibir(projeto_, origemId, nomeOrigem, [safe] { if (safe) safe->recarregar(); }, alvoId, caminhoAlvo);
}

void ArvoreBackupComponent::usarTodasPropostas() {
    std::vector<std::string> ids;
    for (const auto& p : propostas_) ids.push_back(p.raizId);
    juce::Component::SafePointer<ArvoreBackupComponent> safe(this);
    juce::MessageManager::callAsync([safe, ids] {
        if (!safe) return;
        for (const auto& id : ids) safe->projeto_.usarArvoreProposta(id);
        safe->recarregar();
    });
}

// MERGE ALL: abre a mescla de cada proposta, uma depois da outra (cada uma tem seu destino e seus conflitos).
// Cancelar numa delas interrompe a fila; as já mescladas ficam.
void ArvoreBackupComponent::mesclarTodasPropostas() {
    std::vector<std::pair<std::string, juce::String>> fila;
    for (const auto& p : projeto_.listarPropostas(mapaAtivoId_)) fila.emplace_back(p.raizId, p.nome);
    juce::Component::SafePointer<ArvoreBackupComponent> safe(this);
    juce::MessageManager::callAsync([safe, fila] { if (safe) safe->mesclarProximaDaFila(fila); });
}

void ArvoreBackupComponent::mesclarProximaDaFila(std::vector<std::pair<std::string, juce::String>> fila) {
    if (fila.empty()) return;
    const auto atual = fila.front();
    fila.erase(fila.begin());
    juce::Component::SafePointer<ArvoreBackupComponent> safe(this);
    DialogoMescla::exibir(projeto_, atual.first, atual.second, [safe, fila] {
        if (!safe) return;
        safe->recarregar();
        safe->mesclarProximaDaFila(fila);
    });
}

void ArvoreBackupComponent::confirmarDescarteTodasPropostas() {
    int arquivos = 0;
    std::vector<std::string> ids;
    for (const auto& p : propostas_) { ids.push_back(p.raizId); arquivos += p.arquivos; }
    juce::Component::SafePointer<ArvoreBackupComponent> safe(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle(i18n::t("arvore_backup.descartar_titulo"))
                                     .withMessage(i18n::t("arvore_backup.descartar_msg").replace("{n}", juce::String(arquivos)))
                                     .withButton(i18n::t("arvore_backup.btn_descartar_todas"))
                                     .withButton(i18n::t("mescla.btn_cancelar")),
                                 juce::ModalCallbackFunction::create([safe, ids](int r) {
                                     if (r != 1 || !safe) return;
                                     for (const auto& id : ids) safe->projeto_.descartarProposta(id);
                                     safe->recarregar();
                                 }));
}

void ArvoreBackupComponent::confirmarDescarteProposta(const std::string& raizId, int arquivos) {
    juce::Component::SafePointer<ArvoreBackupComponent> safe(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle(i18n::t("arvore_backup.descartar_titulo"))
                                     .withMessage(i18n::t("arvore_backup.descartar_msg").replace("{n}", juce::String(arquivos)))
                                     .withButton(i18n::t("arvore_backup.btn_descartar"))
                                     .withButton(i18n::t("mescla.btn_cancelar")),
                                 juce::ModalCallbackFunction::create([safe, raizId](int r) {
                                     if (r != 1 || !safe) return;
                                     safe->projeto_.descartarProposta(raizId);
                                     safe->recarregar();
                                 }));
}

void ArvoreBackupComponent::recarregar() {
    // Um Undo pode ter apagado/recriado/renomeado mapas: o combo e o mapa ativo acompanham.
    {
        const auto mapas = projeto_.listarFolderMaps();
        bool mudou = mapas.size() != mapasCache_.size();
        for (size_t i = 0; !mudou && i < mapas.size(); ++i)
            mudou = mapas[i].id != mapasCache_[i].id || mapas[i].nome != mapasCache_[i].nome;
        if (mudou) {
            recarregarComboMapas();
            atualizarEstadoBotoesParaMapa();
        }
    }
    // Itens que chegaram soltos numa pasta com regra (arrastados, SEND TO FOLDER...) são
    // organizados aqui. Uma consulta só quando não há nada a fazer.
    if (!mapaAtivoEhOriginal()) {
        // Pasta ingerida inteira: nasce uma árvore proposta (os arquivos seguem em SEM PASTA até o usuário decidir).
        if (!aoPerguntarIngestEmProcessamento || !aoPerguntarIngestEmProcessamento())
            projeto_.gerarPropostasDeNovasPastas(mapaAtivoId_);
        projeto_.organizarItensSoltosDasPastasComRegra(mapaAtivoId_);
    }
    reconstruirPropostas();
    recalcularNodes();
    if (mapaEnquadradoId_ != mapaAtivoId_) {  // primeira abertura deste mapa
        mapaEnquadradoId_ = mapaAtivoId_;
        enquadrarPendente_ = true;
    }
    tentarEnquadrar();
    repaint();
}

void ArvoreBackupComponent::reconstruirExplorador() {
    if (!treeExplorador_) return;
    std::unique_ptr<juce::XmlElement> aberto(treeExplorador_->getOpennessState(true));
    juce::Component::SafePointer<ArvoreBackupComponent> safe(this);
    auto aoSelecionar = [safe](const std::string& id) {
        if (!safe) return;
        safe->pastaExploradorId_ = id;
        if (safe->btnFind_) safe->btnFind_->setEnabled(true);
    };
    auto aoLocalizar = [safe](const std::string& id) {
        if (safe) safe->localizarPasta(id);
    };
    auto aoMenu = [safe](const juce::MouseEvent& e) {
        if (safe) safe->mostrarMenuExplorador(e);
    };
    auto aoAbrir = [safe](ItemPastaExplorador& item) {
        if (!safe || safe->reconstruindoExplorador_) return;
        const std::string id = item.id();
        juce::MessageManager::callAsync([safe, id] { if (safe) safe->ajustarVistaExplorador(id); });
    };

    std::set<std::string> ids;
    for (const auto& n : nodes_) ids.insert(n.id);
    std::map<std::string, std::vector<const FolderNode*>> filhos;
    std::vector<const FolderNode*> raizes;
    for (const auto& n : nodes_) {
        if (n.pastaPaiId.empty() || !ids.count(n.pastaPaiId)) raizes.push_back(&n);
        else filhos[n.pastaPaiId].push_back(&n);
    }
    auto porNome = [](const FolderNode* a, const FolderNode* b) { return a->nome.compareNatural(b->nome, false) < 0; };
    std::sort(raizes.begin(), raizes.end(), porNome);
    for (auto& par : filhos) std::sort(par.second.begin(), par.second.end(), porNome);

    std::function<std::unique_ptr<ItemPastaExplorador>(const FolderNode&)> montar = [&](const FolderNode& n) {
        auto item = std::make_unique<ItemPastaExplorador>(n.id, n.nome, n.contagemItens, n.ativo, aoSelecionar, aoLocalizar, aoAbrir, aoMenu);
        auto it = filhos.find(n.id);
        if (it != filhos.end())
            for (const auto* f : it->second) item->addSubItem(montar(*f).release());
        return item;
    };

    reconstruindoExplorador_ = true;
    treeExplorador_->setRootItem(nullptr);
    raizExplorador_ = std::make_unique<ItemPastaExplorador>(std::string(), juce::String(), 0, true, nullptr, nullptr);
    for (const auto* r : raizes) raizExplorador_->addSubItem(montar(*r).release());
    treeExplorador_->setRootItem(raizExplorador_.get());
    if (aberto) treeExplorador_->restoreOpennessState(*aberto, false);
    reconstruindoExplorador_ = false;

    // Mantém a seleção do explorer se a pasta ainda existe.
    if (!pastaExploradorId_.empty() && ids.count(pastaExploradorId_)) {
        if (auto* item = treeExplorador_->findItemFromIdentifierString(juce::String(pastaExploradorId_)))
            item->setSelected(true, false, juce::dontSendNotification);
    } else {
        pastaExploradorId_.clear();
    }
    if (btnFind_) btnFind_->setEnabled(!pastaExploradorId_.empty());
}

// Ao abrir uma pasta no explorer: acha a melhor posição horizontal pra os nomes dos filhos aparecerem inteiros
// (a árvore rola pro lado só o necessário, sem esconder a seta da pasta aberta) e mantém a pasta visível.
void ArvoreBackupComponent::ajustarVistaExplorador(const std::string& pastaId) {
    if (!treeExplorador_) return;
    auto* item = dynamic_cast<ItemPastaExplorador*>(treeExplorador_->findItemFromIdentifierString(juce::String(pastaId)));
    if (item == nullptr || !item->isOpen() || item->getNumSubItems() == 0) return;
    treeExplorador_->scrollToKeepItemVisible(item);
    auto* vp = treeExplorador_->getViewport();
    const int larguraVista = vp->getMaximumVisibleWidth();
    int direita = 0, indentFilho = 0;
    for (int i = 0; i < item->getNumSubItems(); ++i) {
        auto* f = item->getSubItem(i);
        const auto pos = f->getItemPosition(true);  // relativa ao topo-esquerdo da árvore (já com o recuo)
        indentFilho = pos.getX();
        direita = std::max(direita, pos.getX() + f->getItemWidth());
    }
    const int deslocamento = juce::jlimit(0, std::max(0, indentFilho - 24), std::max(0, direita - larguraVista));
    vp->setViewPosition(deslocamento, vp->getViewPositionY());
}

// Botão direito no explorer: com 2 pastas selecionadas (shift/cmd+clique), MERGE em qualquer direção.
void ArvoreBackupComponent::mostrarMenuExplorador(const juce::MouseEvent&) {
    if (!treeExplorador_ || mapaAtivoEhOriginal()) return;
    std::vector<std::pair<std::string, juce::String>> sel;
    for (int i = 0; i < treeExplorador_->getNumSelectedItems(); ++i)
        if (auto* it = dynamic_cast<ItemPastaExplorador*>(treeExplorador_->getSelectedItem(i)))
            for (const auto& n : nodes_)
                if (n.id == it->id()) sel.emplace_back(n.id, n.nome);
    juce::PopupMenu menu;
    if (sel.size() == 2) {
        juce::PopupMenu sub;
        const bool editavel = !projeto_.somenteLeitura();
        sub.addItem(i18n::t("arvore_backup.mesclar_em").replace("{a}", sel[0].second).replace("{b}", sel[1].second), editavel, false,
                    [this, a = sel[0].first, b = sel[1].first] { iniciarMesclaPastas(a, b); });
        sub.addItem(i18n::t("arvore_backup.mesclar_em").replace("{a}", sel[1].second).replace("{b}", sel[0].second), editavel, false,
                    [this, a = sel[0].first, b = sel[1].first] { iniciarMesclaPastas(b, a); });
        menu.addSubMenu(i18n::t("arvore_backup.mesclar_menu"), sub);
    } else {
        menu.addItem(i18n::t("arvore_backup.mesclar_menu"), false, false, [] {});
        menu.addItem(i18n::t("arvore_backup.mesclar_dica_sel"), false, false, [] {});
    }
    menu.showMenuAsync(juce::PopupMenu::Options());
}

// FIND: seleciona a pasta e todos os descendentes no stage, mostra o card da pasta e enquadra a vista na árvore dela.
void ArvoreBackupComponent::localizarPasta(const std::string& pastaId) {
    int indice = -1;
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].id == pastaId) { indice = static_cast<int>(i); break; }
    if (indice < 0) return;
    const auto canvas = areaCanvas();
    if (canvas.getWidth() < 50 || canvas.getHeight() < 50) return;

    std::set<int> alvo;
    alvo.insert(indice);
    coletarDescendentesIndices(indice, alvo);

    juce::Rectangle<int> uniao;
    bool primeiro = true;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const bool sel = alvo.count(static_cast<int>(i)) > 0;
        nodes_[i].selecionado = sel;
        if (!sel) continue;
        uniao = primeiro ? nodes_[i].bounds : uniao.getUnion(nodes_[i].bounds);
        primeiro = false;
    }

    constexpr float kMargem = 60.0f;
    const float zx = (static_cast<float>(canvas.getWidth()) - 2.0f * kMargem) / static_cast<float>(std::max(1, uniao.getWidth()));
    const float zy = (static_cast<float>(canvas.getHeight()) - 2.0f * kMargem) / static_cast<float>(std::max(1, uniao.getHeight()));
    zoom_ = juce::jlimit(0.15f, 1.0f, std::min(zx, zy));
    const auto centro = uniao.getCentre().toFloat();
    panOffset_ = {static_cast<float>(canvas.getWidth()) * 0.5f - centro.x * zoom_,
                  static_cast<float>(canvas.getHeight()) * 0.5f - centro.y * zoom_};
    enquadrarPendente_ = false;
    atualizarPainelDetalhe(pastaId, true);
    sincronizarSlider();
    repaint();
}

void ArvoreBackupComponent::recalcularNodes() {
    recalcularNodesInterno();
    reconstruirExplorador();
}

void ArvoreBackupComponent::recalcularNodesInterno() {
    nodes_.clear();
    contagemSemPasta_ = -1;  // o próximo paint reconta (itens podem ter entrado/saído de pastas)
    const bool original = mapaAtivoEhOriginal();
    // Um AJUSTAR de uma versão anterior encolhia os cartões abaixo do tamanho legível (fator < 1) e gravou
    // posições compactas para isso. Esse arranjo não serve mais: descarta e deixa o mapa abrir arranjado de novo.
    if (!original) {
        const double gravado = projeto_.fatorCardsDoMapa(mapaAtivoId_);
        if (gravado > 0.0 && gravado < 0.999) {
            projeto_.projeto().registro().run("UPDATE acervo_pasta SET posicao_x = 0, posicao_y = 0 WHERE mapa_id = ?",
                                              {matriz::db::Value::of(mapaAtivoId_)});
            projeto_.definirFatorCardsDoMapa(mapaAtivoId_, 0.0);
        }
    }
    auto arvore = projeto_.arvoreAcervo(mapaAtivoId_);

    // Posição salva vale sempre; pasta sem posição recebe a do layout (calculado em memória, nunca
    // gravado aqui: só persiste quando o usuário arrasta ou usa AJUSTAR).
    std::vector<layoutarvore::No> paraLayout;
    bool algumaPosicaoSalva = false;

    std::function<void(const ProjetoAberto::NoArvore&)> adicionarNo = [&](const ProjetoAberto::NoArvore& no) {
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
            node.proposta = no.proposta;
            node.regra = no.regraOrganizacao;
            node.autoSub = ProjetoAberto::regraEhAuto(node.regra);
            if (node.regra.isNotEmpty() && !node.autoSub) node.niveisRegra = matriz::consolidacao::hierarquiaDeCsv(node.regra.toStdString());

            float escala = static_cast<float>(no.escalaNo);
            if (original) {
                auto it = escalasOriginal_.find(no.id);
                escala = it != escalasOriginal_.end() ? it->second : 1.0f;
            }
            node.escalaManual = juce::jlimit(0.5f, 5.0f, escala > 0.0f ? escala : 1.0f);

            const bool posicaoSalva = no.posicaoX != 0 || no.posicaoY != 0;
            algumaPosicaoSalva = algumaPosicaoSalva || posicaoSalva;
            node.bounds = juce::Rectangle<int>(no.posicaoX, no.posicaoY, 190, 84);
            node.boundsOriginal = node.bounds;
            nodes_.push_back(node);
            paraLayout.push_back({no.id, no.pastaPaiId, posicaoSalva, no.posicaoX, no.posicaoY});
        }
        for (const auto& filho : no.filhos) adicionarNo(filho);
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
        adicionarNo(*raizFiltro);
    } else {
        for (const auto& filho : arvore.filhos) adicionarNo(filho);
    }

    // Nível de cada pasta na hierarquia mostrada (raiz = 0; pai ausente, como numa aba isolada, = raiz).
    {
        std::map<std::string, int> indicePorId;
        for (size_t i = 0; i < nodes_.size(); ++i) indicePorId[nodes_[i].id] = static_cast<int>(i);
        for (auto& n : nodes_) {
            int d = 0;
            std::string cursor = n.pastaPaiId;
            while (!cursor.empty() && d < 200) {
                auto it = indicePorId.find(cursor);
                if (it == indicePorId.end()) break;
                ++d;
                cursor = nodes_[static_cast<size_t>(it->second)].pastaPaiId;
            }
            n.nivel = d;
        }
    }

    fatorCards_ = original ? 0.0 : projeto_.fatorCardsDoMapa(mapaAtivoId_);
    autoLayoutPendente_ = false;
    const auto canvas = areaCanvas();
    const bool canvasPronto = canvas.getWidth() >= 200 && canvas.getHeight() >= 150;

    // Mapa sem nada salvo (o ORIGINAL sempre, ou um mapa que nunca foi arranjado): já abre arranjado —
    // maior tamanho de cartão que cabe na área, sem sobreposição nem linhas cruzadas.
    if ((original || !algumaPosicaoSalva || sobreposto_) && canvasPronto && calcularEAplicarAutoArranjo()) {
        recalcularPivot();
        aplicarEscalaTamanho(escalaTamanho_);
        return;
    }
    if (original || !algumaPosicaoSalva || sobreposto_) autoLayoutPendente_ = true;  // canvas ainda sem tamanho: resized() refaz

    // Mapa com posições salvas: tamanho vem do fator do mapa (ou o tamanho único antigo) x multiplicador.
    layoutarvore::ParametrosAuto pa;
    int larguraMax = 190, alturaMax = 84;
    for (auto& n : nodes_) {
        if (fatorCards_ > 0.0) {
            const auto t = layoutarvore::tamanhoDoNivel(pa, n.nivel, fatorCards_);
            n.autoW = t.w;
            n.autoH = t.h;
        } else {
            n.autoW = 190;
            n.autoH = 84;
        }
        const int w = juce::roundToInt(n.autoW * n.escalaManual), h = juce::roundToInt(n.autoH * n.escalaManual);
        larguraMax = std::max(larguraMax, w);
        alturaMax = std::max(alturaMax, h);
        n.bounds.setSize(w, h);
        n.boundsOriginal = n.bounds;
    }

    layoutarvore::Parametros parametros;
    parametros.larguraNo = larguraMax;
    parametros.alturaNo = alturaMax;
    const auto posicoes = layoutarvore::calcular(paraLayout, parametros);
    for (auto& node : nodes_) {
        auto it = posicoes.find(node.id);
        if (it == posicoes.end()) continue;
        node.bounds.setPosition(it->second.x, it->second.y);
        node.boundsOriginal = node.bounds;
    }

    recalcularPivot();
    aplicarEscalaTamanho(escalaTamanho_); // reaplica a escala global ao layout recém-lido
}

// Auto-arranjo completo dos nodes_ atuais: posições E tamanhos (por nível), respeitando o multiplicador
// manual de cada pasta. Não persiste nada. false = nada a fazer / canvas sem tamanho.
bool ArvoreBackupComponent::calcularEAplicarAutoArranjo() {
    if (nodes_.empty()) return false;
    const auto canvas = areaCanvas();
    if (canvas.getWidth() < 200 || canvas.getHeight() < 150) return false;

    std::vector<layoutarvore::NoTam> nos;
    nos.reserve(nodes_.size());
    for (const auto& n : nodes_) nos.push_back({n.id, n.pastaPaiId, static_cast<double>(n.escalaManual)});

    layoutarvore::ParametrosAuto parametros;
    parametros.areaLargura = canvas.getWidth();
    parametros.areaAltura = canvas.getHeight();
    const auto r = layoutarvore::calcularAutoArranjo(nos, parametros);
    if (r.caixas.empty()) return false;

    fatorCards_ = r.fator;
    for (auto& n : nodes_) {
        auto it = r.caixas.find(n.id);
        if (it == r.caixas.end()) continue;
        const auto t = layoutarvore::tamanhoDoNivel(parametros, r.nivel.at(n.id), r.fator);
        n.autoW = t.w;
        n.autoH = t.h;
        n.nivel = r.nivel.at(n.id);
        n.boundsOriginal = juce::Rectangle<int>(it->second.x, it->second.y, it->second.w, it->second.h);
        n.bounds = n.boundsOriginal;
    }
    return true;
}

void ArvoreBackupComponent::recalcularPivot() {
    if (nodes_.empty()) { pivotEscala_ = {0.0f, 0.0f}; return; }
    juce::Rectangle<int> uniao = nodes_.front().boundsOriginal;
    for (const auto& n : nodes_) uniao = uniao.getUnion(n.boundsOriginal);
    pivotEscala_ = uniao.getCentre().toFloat();
}

void ArvoreBackupComponent::definirFocoNosItens(std::set<std::string> itemIds) {
    itensEmFoco_ = std::move(itemIds);
    focoPendente_ = !itensEmFoco_.empty();
    tentarEnquadrar();
    repaint();
}

bool ArvoreBackupComponent::aplicarFocoPendente() {
    if (!focoPendente_ || nodes_.empty()) return false;
    const auto canvas = areaCanvas();
    if (canvas.getWidth() < 50 || canvas.getHeight() < 50) return false;  // sem tamanho ainda: resized() tenta de novo

    bool achou = false;
    juce::Rectangle<int> uniao;
    for (auto& n : nodes_) {
        n.selecionado = false;
        for (const auto& id : itensEmFoco_) {
            if (n.itemIdsDiretos.count(id)) {
                n.selecionado = true;
                uniao = achou ? uniao.getUnion(n.bounds) : n.bounds;
                achou = true;
                break;
            }
        }
    }
    focoPendente_ = false;
    if (!achou) return false;  // o arquivo não está em nenhuma pasta deste mapa: segue o enquadramento normal

    constexpr float kMargem = 120.0f;
    const float zx = (static_cast<float>(canvas.getWidth()) - 2.0f * kMargem) / static_cast<float>(std::max(1, uniao.getWidth()));
    const float zy = (static_cast<float>(canvas.getHeight()) - 2.0f * kMargem) / static_cast<float>(std::max(1, uniao.getHeight()));
    zoom_ = juce::jlimit(0.5f, 1.0f, std::min(zx, zy));  // legível: nunca menor que 50% nem maior que 100%
    const auto centro = uniao.getCentre().toFloat();
    panOffset_ = {static_cast<float>(canvas.getWidth()) * 0.5f - centro.x * zoom_,
                  static_cast<float>(canvas.getHeight()) * 0.5f - centro.y * zoom_};
    enquadrarPendente_ = false;
    sincronizarSlider();
    repaint();
    return true;
}

void ArvoreBackupComponent::tentarEnquadrar() {
    if (aplicarFocoPendente()) return;
    if (!enquadrarPendente_ || nodes_.empty()) return;
    const auto canvas = areaCanvas();
    if (canvas.getWidth() < 50 || canvas.getHeight() < 50) return;  // ainda sem tamanho: resized() tenta de novo

    juce::Rectangle<int> uniao = nodes_.front().bounds;
    for (const auto& n : nodes_) uniao = uniao.getUnion(n.bounds);
    constexpr float kMargem = 40.0f;
    const float zx = (static_cast<float>(canvas.getWidth()) - 2.0f * kMargem) / static_cast<float>(uniao.getWidth());
    const float zy = (static_cast<float>(canvas.getHeight()) - 2.0f * kMargem) / static_cast<float>(uniao.getHeight());
    zoom_ = juce::jlimit(0.15f, 1.0f, std::min(zx, zy));  // nunca amplia além de 100%
    const auto centro = uniao.getCentre().toFloat();
    panOffset_ = {static_cast<float>(canvas.getWidth()) * 0.5f - centro.x * zoom_,
                  static_cast<float>(canvas.getHeight()) * 0.5f - centro.y * zoom_};
    enquadrarPendente_ = false;
}

juce::Rectangle<int> ArvoreBackupComponent::areaBarraAbas() const {
    if (sobreposto_) return {};  // a sobreposição não tem abas
    return getLocalBounds().withTrimmedLeft(ColunaCardsLayout::kLargura).removeFromTop(kAlturaBarraAbas);
}

juce::Rectangle<int> ArvoreBackupComponent::areaCanvas() const {
    return getLocalBounds().withTrimmedLeft(ColunaCardsLayout::kLargura).withTrimmedTop(sobreposto_ ? 0 : kAlturaBarraAbas);
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
    if (sobreposto_) return;
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
    if (itemId.empty() || mapaAtivoEhOriginal()) return;
    for (const auto& n : nodes_) {
        if (n.nome.toStdString() == novaPasta || n.id == novaPasta) {
            projeto_.adicionarItensAPasta({itemId}, n.id);
            break;
        }
    }
    recarregar();
}

void ArvoreBackupComponent::selecionarERenomearPasta(const std::string& pastaId) {
    if (mapaAtivoEhOriginal()) return;
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

// Fase 1 — "SEM PASTA"/"NO FOLDER" é nome reservado (o nó fixo do mapa):
// o usuário não cria nem renomeia pasta comum com ele, em nenhum idioma.
static bool nomeReservadoDePasta(const std::string& nome) {
    auto n = juce::String(nome).replaceCharacter('_', ' ').trim().toUpperCase();
    return n == "SEM PASTA" || n == "NO FOLDER";
}

static void avisarNomeReservado() {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(matriz::i18n::t("arvore_backup.nome_reservado_titulo"))
                                     .withMessage(matriz::i18n::t("arvore_backup.nome_reservado_msg"))
                                     .withButton(matriz::i18n::t("dialogo.ok")),
                                 juce::ModalCallbackFunction::create([](int) {}));
}

void ArvoreBackupComponent::criarNovaPasta(const std::string& nome, const std::optional<std::string>& pastaPaiId) {
    if (mapaAtivoEhOriginal()) return;
    if (nomeReservadoDePasta(nome)) { avisarNomeReservado(); return; }
    // `pos` sai em coordenadas da VISTA (já com a escala/spread do slider); o banco guarda a posição a 100%.
    const float g = std::max(0.05f, escalaTamanho_);
    auto pos = posicaoLivrePertoDoCentro(juce::roundToInt(190 * g), juce::roundToInt(84 * g));
    const auto centroVista = juce::Point<float>(pos.x + 190 * g * 0.5f, pos.y + 84 * g * 0.5f);
    const auto centroOriginal = juce::Point<float>(
        pivotEscala_.x + (centroVista.x - pivotEscala_.x) / (g * std::max(0.05f, espalharX_)),
        pivotEscala_.y + (centroVista.y - pivotEscala_.y) / (g * std::max(0.05f, espalharY_)));
    pos = {juce::roundToInt(centroOriginal.x - 95.0f), juce::roundToInt(centroOriginal.y - 42.0f)};
    // Sobreposição: pasta nova sem pai vai para dentro da raiz (senão nasceria fora da vista), e sem posição
    // gravada — o mapa principal a coloca pelo layout.
    const auto pai = (sobreposto_ && !pastaPaiId) ? std::optional<std::string>(raizSobreposicaoId_) : pastaPaiId;
    std::string novoId = projeto_.criarPastaAcervo(nome, pai, mapaAtivoId_);
    if (!sobreposto_) projeto_.atualizarPosicaoPastaAcervo(novoId, pos.x, pos.y);

    destaqueNovaPastaId_ = novoId;
    inicioPulsoMs_ = juce::Time::getMillisecondCounter();
    startTimer(30);

    recarregar();
}

void ArvoreBackupComponent::renomearPastaSelecionada(const std::string& pastaId, const std::string& novoNome) {
    if (mapaAtivoEhOriginal()) return;
    if (nomeReservadoDePasta(novoNome)) { avisarNomeReservado(); return; }
    projeto_.renomearPastaAcervo(pastaId, novoNome);
    recarregar();
}

void ArvoreBackupComponent::apagarPastaSelecionada(const std::string& pastaId) {
    if (mapaAtivoEhOriginal()) return;
    projeto_.apagarPastaAcervo(pastaId);
    recarregar();
}

void ArvoreBackupComponent::conectarPastas(const std::string& pastaFilhoId, const std::optional<std::string>& novaPastaPaiId) {
    if (mapaAtivoEhOriginal()) return;
    projeto_.moverPastaAcervo(pastaFilhoId, novaPastaPaiId);
    recarregar();
}

void ArvoreBackupComponent::desconectarSelecionadas() {
    if (mapaAtivoEhOriginal()) return;
    std::vector<std::string> selectedIds;
    for (const auto& n : nodes_) if (n.selecionado) selectedIds.push_back(n.id);
    if (selectedIds.empty()) return;

    // Sem pai: ignorada em silêncio (não é um erro, não conta no aviso).
    std::vector<std::string> comPai;
    for (const auto& id : selectedIds) {
        for (const auto& n : nodes_) {
            if (n.id == id) {
                if (!n.pastaPaiId.empty()) comPai.push_back(id);
                break;
            }
        }
    }
    if (comPai.empty()) return;

    // Um grupo de desfazer só pro lote inteiro — moverPastaAcervo() já
    // registra a reversão de cada pasta dentro dele (item 7/geral).
    projeto_.iniciarGrupoUndo("Disconnect from Parent");
    int desconectadas = 0, puladas = 0;
    for (const auto& id : comPai) {
        // Pré-checa aqui pra nunca deixar moverPastaAcervo mostrar seu
        // próprio avisarMapaTravado() por pasta — um resumo só, no final.
        if (projeto_.pastaTemArquivosNoMain(id)) { ++puladas; continue; }
        if (projeto_.moverPastaAcervo(id, std::nullopt)) ++desconectadas;
    }
    projeto_.finalizarGrupoUndo();

    if (puladas > 0) {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle(i18n::t("mapa_main.titulo"))
                .withMessage(i18n::t("arvore_backup.desconectar_puladas").replace("{n}", juce::String(puladas)))
                .withButton(i18n::t("dialogo.ok")),
            juce::ModalCallbackFunction::create([](int) {}));
    }
    if (desconectadas > 0) recarregar();
}

void ArvoreBackupComponent::alternarAtivoPasta(const std::string& pastaId) {
    if (mapaAtivoEhOriginal()) return;
    for (const auto& n : nodes_) {
        if (n.id == pastaId) {
            projeto_.registrarUndoVisualMapa("Toggle Folder Active", mapaAtivoId_);
            projeto_.alternarAtivoPastaAcervo(pastaId, !n.ativo);
            break;
        }
    }
    recarregar();
}

void ArvoreBackupComponent::mostrarSeletorDeCorPasta(std::vector<std::string> pastaIds, juce::Rectangle<int> screenBounds) {
    if (mapaAtivoEhOriginal() || pastaIds.empty()) return;
    if (screenBounds.isEmpty()) screenBounds = juce::Rectangle<int>(100, 100, 20, 20);

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
    // Guardado pra C de novo (keyPressed) fechar em vez de reabrir.
    corCallout_ = &juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
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
    if (mapaAtivoEhOriginal()) return;
    // toDisplayString(true) inclui o alfa — fromString() em recalcularNodes()
    // faz o caminho de volta. Persistida por pasta (item 12): fechar/reabrir
    // o projeto ou reconstruir o Treemap não apaga, porque recalcularNodes()
    // relê cor_customizada do banco toda vez.
    juce::String hex = cor.toDisplayString(true);
    projeto_.registrarUndoVisualMapa("Folder Color", mapaAtivoId_);
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

void ArvoreBackupComponent::coletarDescendentesIndices(int nodeIndex, std::set<int>& acc) const {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes_.size())) return;
    // Índice pai -> filhos montado uma vez: a versão recursiva varria TODAS as pastas para cada descendente
    // (quadrático; aparecia no `sample` de cada clique num mapa grande).
    std::unordered_map<std::string, std::vector<int>> filhos;
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (!nodes_[i].pastaPaiId.empty()) filhos[nodes_[i].pastaPaiId].push_back(static_cast<int>(i));
    std::set<int> visitados{nodeIndex};
    std::vector<int> pendentes{nodeIndex};
    while (!pendentes.empty()) {
        const int atual = pendentes.back();
        pendentes.pop_back();
        auto it = filhos.find(nodes_[static_cast<size_t>(atual)].id);
        if (it == filhos.end()) continue;
        for (int idx : it->second) {
            acc.insert(idx);
            if (visitados.insert(idx).second) pendentes.push_back(idx);
        }
    }
}

void ArvoreBackupComponent::persistirPosicoesEmLote(const std::vector<int>& indices) {
    if (indices.empty()) return;
    if (sobreposto_) return;  // sobreposição: posições só em memória (não mexem no layout do mapa principal)
    if (!mapaAtivoEhOriginal()) projeto_.registrarUndoVisualMapa("Move Folders", mapaAtivoId_);
    auto& db = projeto_.projeto().registro();
    std::unique_lock<std::recursive_mutex> writeLock(projeto_.writeMutex());
    bool emTransacao = false;
    try { db.exec("BEGIN IMMEDIATE"); emTransacao = true; } catch (...) {}
    for (int idx : indices) {
        if (idx < 0 || idx >= static_cast<int>(nodes_.size())) continue;
        auto& node = nodes_[static_cast<size_t>(idx)];
        // `bounds` está na escala global do slider; o que se grava é a posição a 100% (boundsOriginal).
        const float g = std::max(0.05f, escalaTamanho_);
        const auto c = node.bounds.getCentre().toFloat();
        node.boundsOriginal.setCentre(juce::Point<float>(pivotEscala_.x + (c.x - pivotEscala_.x) / (g * std::max(0.05f, espalharX_)),
                                                          pivotEscala_.y + (c.y - pivotEscala_.y) / (g * std::max(0.05f, espalharY_))).toInt());
        projeto_.atualizarPosicaoPastaAcervo(node.id, node.boundsOriginal.getX(), node.boundsOriginal.getY());
    }
    if (emTransacao) {
        try { db.exec("COMMIT"); } catch (...) { try { db.exec("ROLLBACK"); } catch (...) {} }
    }
}

void ArvoreBackupComponent::autoArranjarSelecionadas(const std::vector<int>& selecionadas) {
    std::set<int> sel(selecionadas.begin(), selecionadas.end());
    std::vector<layoutarvore::No> nos;
    juce::Rectangle<int> uniao;
    int larguraMax = 0, alturaMax = 0;
    bool primeiro = true;
    for (int i : sel) {
        const auto& n = nodes_[static_cast<size_t>(i)];
        nos.push_back({n.id, n.pastaPaiId, false, 0, 0});  // pai fora da seleção = raiz do bloco
        uniao = primeiro ? n.boundsOriginal : uniao.getUnion(n.boundsOriginal);
        primeiro = false;
        larguraMax = std::max(larguraMax, n.boundsOriginal.getWidth());
        alturaMax = std::max(alturaMax, n.boundsOriginal.getHeight());
    }
    layoutarvore::Parametros p;
    p.larguraNo = larguraMax;
    p.alturaNo = alturaMax;
    p.origemX = 0;
    p.origemY = 0;
    const auto pos = layoutarvore::calcular(nos, p);

    // Bloco novo ancorado no canto superior esquerdo da região que a seleção ocupava; se cair em cima de
    // uma pasta não selecionada, vai para baixo de todas as outras.
    auto candidatos = [&](juce::Point<int> origem) {
        std::map<int, juce::Rectangle<int>> out;
        for (int i : sel) {
            auto it = pos.find(nodes_[static_cast<size_t>(i)].id);
            if (it == pos.end()) continue;
            out[i] = nodes_[static_cast<size_t>(i)].boundsOriginal.withPosition(origem.x + it->second.x, origem.y + it->second.y);
        }
        return out;
    };
    auto colide = [&](const std::map<int, juce::Rectangle<int>>& c) {
        for (const auto& [i, r] : c)
            for (size_t k = 0; k < nodes_.size(); ++k)
                if (!sel.count(static_cast<int>(k)) && r.intersects(nodes_[k].boundsOriginal)) return true;
        return false;
    };
    auto c = candidatos(uniao.getTopLeft());
    if (colide(c)) {
        int baixo = uniao.getBottom();
        for (size_t k = 0; k < nodes_.size(); ++k)
            if (!sel.count(static_cast<int>(k))) baixo = std::max(baixo, nodes_[k].boundsOriginal.getBottom());
        c = candidatos({uniao.getX(), baixo + 40});
    }
    for (const auto& [i, r] : c) nodes_[static_cast<size_t>(i)].boundsOriginal = r;

    aplicarEscalaTamanho(escalaTamanho_);
    std::vector<int> indices(sel.begin(), sel.end());
    persistirPosicoesEmLote(indices);
}

void ArvoreBackupComponent::autoArranjar() {
    if (nodes_.empty()) return;
    // Com várias pastas selecionadas, o AJUSTAR só atua nelas.
    if (const auto selecionadas = indicesSelecionados(); selecionadas.size() >= 2) {
        autoArranjarSelecionadas(selecionadas);
        return;
    }

    // Só reorganiza as POSIÇÕES (e o fator de cartão que faz tudo caber): o multiplicador que o usuário
    // ajustou em cada pasta é mantido; a escala global volta a 100%.
    escalaTamanho_ = 1.0f;
    if (!calcularEAplicarAutoArranjo()) return;

    std::vector<int> indices;
    for (size_t i = 0; i < nodes_.size(); ++i) indices.push_back(static_cast<int>(i));
    recalcularPivot();
    persistirPosicoesEmLote(indices);  // uma transação só
    if (!mapaAtivoEhOriginal() && !sobreposto_) projeto_.definirFatorCardsDoMapa(mapaAtivoId_, fatorCards_);

    zoom_ = 1.0f;
    panOffset_ = {0.0f, 0.0f};
    aplicarEscalaTamanho(escalaTamanho_);
    sincronizarSlider();
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
    const auto origem = areaCanvas().getPosition();
    return {(static_cast<float>(screen.x - origem.x) - panOffset_.x) / zoom_,
            (static_cast<float>(screen.y - origem.y) - panOffset_.y) / zoom_};
}

juce::Point<int> ArvoreBackupComponent::canvasToScreen(juce::Point<float> canvas) const {
    const auto origem = areaCanvas().getPosition();
    return {static_cast<int>(canvas.x * zoom_ + panOffset_.x) + origem.x,
            static_cast<int>(canvas.y * zoom_ + panOffset_.y) + origem.y};
}

// centro em coordenadas locais do canvas (origem no canto do canvas, não do componente).
void ArvoreBackupComponent::aplicarZoom(float novoZoom, juce::Point<float> centro) {
    novoZoom = juce::jlimit(0.03f, 3.0f, novoZoom);  // (o FIT pode ir abaixo de 15% num mapa grande)
    float ratio = novoZoom / zoom_;
    panOffset_.x = centro.x - (centro.x - panOffset_.x) * ratio;
    panOffset_.y = centro.y - (centro.y - panOffset_.y) * ratio;
    zoom_ = novoZoom;
    repaint();
}

// Fase 1 — SEM PASTA (NO FOLDER): card na coluna esquerda (espaço de TELA, fora do
// canvas; S4/13: "visualmente separado da árvore e sem ligação com as outras
// pastas"). Nunca aparece no ORIGINAL: lá não existe conceito de "sem pasta".
juce::Rectangle<int> ArvoreBackupComponent::boundsSemPasta() const {
    return mapaAtivoEhOriginal() ? juce::Rectangle<int>() : coluna_.ultimoCard();
}

void ArvoreBackupComponent::desenharSemPasta(juce::Graphics& g) const {
    if (mapaAtivoEhOriginal() || semPastaBounds_.isEmpty()) return;
    const auto& tk = tema();
    auto card = boundsSemPasta().toFloat();

    if (semPastaHover_) {
        g.setColour(tk.acento.withAlpha(0.22f));
        g.fillRoundedRectangle(card, tk.raioPequeno);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(card.reduced(0.5f), tk.raioPequeno, 2.0f);
    }
    const auto agora = juce::Time::getMillisecondCounter();
    if (contagemSemPasta_ < 0 || contagemSemPastaMapa_ != mapaAtivoId_ || agora - contagemSemPastaMs_ > 1000) {
        contagemSemPasta_ = projeto_.contarItensSemPasta(mapaAtivoId_);
        contagemSemPastaMs_ = agora;
        contagemSemPastaMapa_ = mapaAtivoId_;
    }
    const int n = contagemSemPasta_;
    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(juce::String(n) + " " + i18n::t("arvore_backup.items"), semPastaBounds_, juce::Justification::centredLeft, true);
}

// Fase 1 — arrastar itens (MosaicoComponent::startDragging, descrição =
// array de item ids, mesmo formato que ArvoreComponent já aceita) pra
// dentro de SEM PASTA remove-os de todas as pastas do mapa ativo. Não
// interessa nenhum outro alvo de drop no canvas — reposicionar/reconectar
// pasta continua só em mouseDown/mouseDrag/mouseUp, sem relação com
// DragAndDropTarget.
bool ArvoreBackupComponent::isInterestedInDragSource(const SourceDetails& details) {
    return !mapaAtivoEhOriginal() && details.description.isArray();
}

void ArvoreBackupComponent::itemDragEnter(const SourceDetails& details) { itemDragMove(details); }

void ArvoreBackupComponent::itemDragMove(const SourceDetails& details) {
    bool sobre = boundsSemPasta().contains(details.localPosition);
    if (sobre != semPastaHover_) {
        semPastaHover_ = sobre;
        repaint();
    }
}

void ArvoreBackupComponent::itemDragExit(const SourceDetails&) {
    if (semPastaHover_) {
        semPastaHover_ = false;
        repaint();
    }
}

void ArvoreBackupComponent::itemDropped(const SourceDetails& details) {
    bool sobre = semPastaHover_;
    semPastaHover_ = false;
    repaint();
    if (!sobre || mapaAtivoEhOriginal()) return;

    auto* arr = details.description.getArray();
    if (!arr) return;
    std::vector<std::string> itemIds;
    for (auto& v : *arr) itemIds.push_back(v.toString().toStdString());
    if (itemIds.empty()) return;

    projeto_.removerItensDoBackup(itemIds, mapaAtivoId_);
    recarregar();
}

void ArvoreBackupComponent::iniciarEdicaoInline(int nodeIndex) {
    finalizarEdicaoInline();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes_.size())) return;
    auto& node = nodes_[static_cast<size_t>(nodeIndex)];
    editingNodeId_ = node.id;

    auto screenBounds = node.bounds.toFloat();
    const auto origemCanvas = areaCanvas().getPosition().toFloat();
    screenBounds.setX(screenBounds.getX() * zoom_ + panOffset_.x + origemCanvas.x);
    screenBounds.setY(screenBounds.getY() * zoom_ + panOffset_.y + origemCanvas.y);
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

void ArvoreBackupComponent::paint(juce::Graphics& g) {
    {   // o slider passa a mostrar a escala da seleção (ou a global) sempre que a seleção muda
        std::string assinatura;
        for (const auto& n : nodes_) if (n.selecionado) assinatura += n.id + ";";
        if (assinatura != assinaturaSelecaoSlider_) {
            assinaturaSelecaoSlider_ = std::move(assinatura);
            sincronizarSlider();
        }
    }
    const auto& tk = tema();
    const auto canvas = areaCanvas();
    bool isLight = (tk.fundo.getBrightness() > 0.5f);
    juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
    g.fillAll(bg);

    g.saveState();
    g.reduceClipRegion(canvas);

    // Canvas background grid pattern (n8n style dots) — in screen space
    // Zoom afastado: os pontos ficam mais espaçados (nunca milhares); todos numa chamada só.
    g.setColour(tema().painelAlt.withAlpha(0.4f));
    {
        float dotStep = 24.0f * zoom_;
        while (dotStep < 12.0f) dotStep *= 2.0f;
        float startX = std::fmod(panOffset_.x, dotStep);
        float startY = std::fmod(panOffset_.y, dotStep);
        if (startX < 0) startX += dotStep;
        if (startY < 0) startY += dotStep;
        juce::RectangleList<float> pontos;
        for (float x = static_cast<float>(canvas.getX()) + startX; x < static_cast<float>(canvas.getRight()); x += dotStep)
            for (float y = static_cast<float>(canvas.getY()) + startY; y < static_cast<float>(canvas.getBottom()); y += dotStep)
                pontos.addWithoutMerging({x, y, 2.0f, 2.0f});
        g.fillRectList(pontos);
    }

    if (nodes_.empty()) {
        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(i18n::t("arvore_backup.vazio"), canvas, juce::Justification::centred, true);
        g.restoreState();
        coluna_.paint(g);
        desenharSemPasta(g);
        desenharIndicadorZoom(g);
        desenharBarraDeAbas(g);
        return;
    }

    // screen = canvas * zoom + panOffset + origem do canvas
    auto transform = juce::AffineTransform::scale(zoom_).translated(panOffset_.x + static_cast<float>(canvas.getX()),
                                                                     panOffset_.y + static_cast<float>(canvas.getY()));
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

    // Culling: só desenha o que cai na área visível do canvas (em coordenadas do canvas, com folga de 40 px).
    // As portas acima continuam montadas para todos.
    const juce::Rectangle<float> visivel =
        juce::Rectangle<float>(screenToCanvas(canvas.getTopLeft()), screenToCanvas(canvas.getBottomRight())).expanded(40.0f);

    for (const auto& node : nodes_) {
        if (!node.pastaPaiId.empty() && outputPorts.count(node.pastaPaiId) && inputPorts.count(node.id)) {
            const auto p1 = outputPorts[node.pastaPaiId], p2 = inputPorts[node.id];
            if (!juce::Rectangle<float>(p1, p2).expanded(40.0f).intersects(visivel)) continue;
            desenharLinhaConexaoN8n(g, p1, p2, node.ativo, false);
        }
    }

    // Live connection drag preview (socketDragPos_ is in canvas space)
    if (!socketDragParentId_.empty() && outputPorts.count(socketDragParentId_)) {
        desenharLinhaConexaoN8n(g, outputPorts[socketDragParentId_], socketDragPos_, true, true);
    }

    // Node cards
    const bool arrastando = nodeDragIndice_ >= 0 || panning_;
    for (const auto& node : nodes_) {
        if (!node.bounds.toFloat().intersects(visivel)) continue;
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

        if (node.selecionado && arrastando) {
            // Durante o arrasto/pan: traço sólido simples (o tracejado custa caro a cada frame).
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(b, 8.0f, 2.5f);
        } else if (node.selecionado) {
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

        // A fonte acompanha o tamanho do folder (slider, escala por pasta, nível): 190x84 = 100%.
        const float fs = juce::jlimit(0.4f, 4.0f, std::min(b.getWidth() / 190.0f, b.getHeight() / 84.0f));
        // Texto que sairia com menos de 5 px na tela: nem desenha (só retângulo, borda e portas).
        if (12.0f * fs * zoom_ >= 5.0f) {
            auto header = b.removeFromTop(24.0f * fs);
            g.setColour(node.ativo ? tema().painelAlt : tema().fundo);
            g.fillRoundedRectangle(header, 8.0f);
            g.fillRect(header.withTrimmedTop(16.0f * fs));

            desenharCabecalhoNo(g, node, header);

            auto content = b.reduced(10.0f * fs, 6.0f * fs);
            g.setColour(node.ativo ? tema().textoPrimario : tema().textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(12.0f * fs, juce::Font::bold)));
            g.drawText(node.nome, content.removeFromTop(18.0f * fs), juce::Justification::centredLeft, true);

            g.setColour(tema().textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(10.0f * fs)));
            juce::String desc = juce::String(node.contagemItens) + " " + i18n::t("arvore_backup.items");
            g.drawText(desc, content, juce::Justification::centredLeft, true);
        }

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

    coluna_.paint(g);
    desenharSemPasta(g);
    desenharIndicadorZoom(g);
    desenharBarraDeAbas(g);
}

void ArvoreBackupComponent::desenharIndicadorZoom(juce::Graphics& g) const {
    // (também desenha a moldura da lista da pasta selecionada, que fica logo abaixo dos cards)
    if (!detalheBounds_.isEmpty()) {
        g.setColour(tema().borda.withAlpha(0.7f));
        g.drawRoundedRectangle(detalheBounds_.toFloat().reduced(0.5f), tema().raioPequeno, 1.0f);
    }
    g.setColour(tema().textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("Zoom " + juce::String(static_cast<int>(zoom_ * 100)) + "%", zoomIndicadorBounds_, juce::Justification::centredLeft);
}

// Cabeçalho do card. Pasta com regra: "AUTO" + um pontinho por bloco, na cor de cada bloco e
// na ordem da regra; subpasta gerada pela regra: marca discreta "auto" à direita.
void ArvoreBackupComponent::desenharCabecalhoNo(juce::Graphics& g, const FolderNode& node, juce::Rectangle<float> header) const {
    const auto& tk = tema();
    const float fs = header.getHeight() / 24.0f;  // o cabeçalho tem 24 px a 100% (ver paint)
    g.setFont(juce::Font(juce::FontOptions(11.0f * fs, juce::Font::bold)));
    if (node.proposta) {
        g.setColour(node.hasCorCustomizada ? node.corCustomizada : tk.acento);
        g.drawText(i18n::t("arvore_backup.nova_marca"), header.reduced(10.0f * fs, 0), juce::Justification::centredLeft);
        return;
    }
    if (node.ativo && !node.niveisRegra.empty()) {
        g.setColour(tk.acento);
        const juce::String rotulo = i18n::t("arvore_backup.auto_marca");
        const float largura = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), rotulo));
        auto area = header.reduced(10.0f * fs, 0);
        g.drawText(rotulo, area.removeFromLeft(largura + 2.0f), juce::Justification::centredLeft);
        area.removeFromLeft(8.0f);
        const float d = 8.0f * fs, passo = 12.0f * fs;
        float x = area.getX();
        for (auto nivel : node.niveisRegra) {
            g.setColour(corDoNivelHierarquia(nivel));
            g.fillEllipse(x, area.getCentreY() - d * 0.5f, d, d);
            x += passo;
        }
        return;
    }
    g.setColour(node.ativo ? tk.acento : tk.textoTerciario);
    g.drawText(node.ativo ? i18n::t("arvore_backup.no_ativo") : i18n::t("arvore_backup.no_desativado"),
               header.reduced(10.0f * fs, 0), juce::Justification::centredLeft);
    if (node.autoSub) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(9.0f * fs)));
        g.drawText(i18n::t("arvore_backup.auto_sub_marca"), header.reduced(10.0f * fs, 0), juce::Justification::centredRight);
    }
}

void ArvoreBackupComponent::resized() {
    coluna_.comecar(getLocalBounds().removeFromLeft(ColunaCardsLayout::kLargura));
    auto& corpo = coluna_.corpo();
    const bool original = mapaAtivoEhOriginal();

    // MAPA — dropdown de mapas, "⋯" e NOVO FOLDER MAP.
    coluna_.iniciarCard(i18n::t("arvore_backup.card_mapa"));
    if (comboMapas_) comboMapas_->setBounds(corpo.removeFromTop(26));
    corpo.removeFromTop(4);
    {
        auto linha = corpo.removeFromTop(26);
        if (btnMenuMapa_) btnMenuMapa_->setBounds(linha.removeFromLeft(30));
        linha.removeFromLeft(4);
        if (btnNovoMapa_) btnNovoMapa_->setBounds(linha);
    }
    coluna_.finalizarCard();

    // PASTAS — Nova pasta, Renomear, Delete e Ajustar.
    coluna_.iniciarCard(i18n::t("arvore_backup.card_pastas"));
    for (auto* b : {btnCriarPasta_.get(), btnRenomearPasta_.get(), btnAutoArranjar_.get()}) {
        if (b) b->setBounds(corpo.removeFromTop(26));
        corpo.removeFromTop(4);
    }
    coluna_.finalizarCard();

    // VISUALIZAÇÃO — zoom, tamanho dos cartões e indicador de zoom.
    coluna_.iniciarCard(i18n::t("arvore_backup.card_visualizacao"));
    {
        auto linha = corpo.removeFromTop(26);
        const int w = (linha.getWidth() - 8) / 3;
        if (btnZoomOut_) btnZoomOut_->setBounds(linha.removeFromLeft(w));
        linha.removeFromLeft(4);
        if (btnZoomFit_) btnZoomFit_->setBounds(linha.removeFromLeft(w));
        linha.removeFromLeft(4);
        if (btnZoomIn_) btnZoomIn_->setBounds(linha);
    }
    corpo.removeFromTop(2);
    zoomIndicadorBounds_ = corpo.removeFromTop(14);
    coluna_.finalizarCard();

    // FOLDER SIZE — slider de tamanho (seleção ou todos) e RESET SIZES.
    coluna_.iniciarCard(i18n::t("arvore_backup.card_tamanho"));
    if (sliderTamanho_) sliderTamanho_->setBounds(corpo.removeFromTop(22));
    corpo.removeFromTop(2);
    if (rotuloSpread_) rotuloSpread_->setBounds(corpo.removeFromTop(14));
    if (sliderSpread_) sliderSpread_->setBounds(corpo.removeFromTop(22));
    if (rotuloSpreadV_) rotuloSpreadV_->setBounds(corpo.removeFromTop(14));
    if (sliderSpreadV_) sliderSpreadV_->setBounds(corpo.removeFromTop(22));
    corpo.removeFromTop(4);
    if (btnResetTamanhos_) btnResetTamanhos_->setBounds(corpo.removeFromTop(26));
    coluna_.finalizarCard();

    // FOLDER EXPLORER — só pastas do mapa ativo; FIND seleciona a pasta (e descendentes) no stage. Ocupa o lugar
    // que era do NEW FOLDERS (agora uma janela flutuante sobre o stage, ver posicionarPainelPropostas).
    {
        constexpr int kAlturaFind = 26;
        const int reservadoSemPasta = original ? 0 : 46;
        const int sobra = corpo.getHeight() - reservadoSemPasta - (ColunaCardsLayout::kCardPad * 2 + ColunaCardsLayout::kHeaderH + 4) - kAlturaFind - 4;
        const int alturaTree = juce::jlimit(70, 230, static_cast<int>(sobra * 0.62f));
        coluna_.iniciarCard(i18n::t("arvore_backup.card_explorador"));
        if (treeExplorador_) treeExplorador_->setBounds(corpo.removeFromTop(alturaTree));
        corpo.removeFromTop(4);
        if (btnFind_) btnFind_->setBounds(corpo.removeFromTop(kAlturaFind));
        coluna_.finalizarCard();
    }
    posicionarPainelPropostas();

    // SEM PASTA — o card com a contagem, clicável e aceitando drop (nunca no ORIGINAL).
    semPastaBounds_ = {};
    if (!original) {
        coluna_.iniciarCard(i18n::t("arvore_backup.sem_pasta_titulo"));
        semPastaBounds_ = corpo.removeFromTop(16);
        coluna_.finalizarCard();
    }

    // O mapa abriu antes de o canvas ter tamanho: agora dá pra arranjar (maior cartão que cabe).
    if (autoLayoutPendente_) {
        const auto canvas = areaCanvas();
        if (canvas.getWidth() >= 200 && canvas.getHeight() >= 150) {
            recalcularNodes();
            enquadrarPendente_ = true;
        }
    }
    tentarEnquadrar();

    // Pasta selecionada: nome, subpastas e a lista de arquivos ocupam o espaço que sobra na coluna
    // esquerda (antes era um painel por cima do canvas, à direita).
    detalheBounds_ = {};
    if (detailViewport_ && detailViewport_->isVisible()) {
        auto area = corpo.withTrimmedBottom(0);
        area.setX(area.getX() - 4);
        area.setWidth(area.getWidth() + 8);
        if (area.getHeight() >= 80) {
            detalheBounds_ = area;
            detailViewport_->setBounds(area.reduced(1));
            detailContent_->setSize(area.getWidth() - 2 - detailViewport_->getScrollBarThickness(), detailContent_->getHeight());
        } else {
            detailViewport_->setBounds({});
        }
    }
}

void ArvoreBackupComponent::limparDicaPasta() {
    dicaPasta_.stopTimer();
    dicaPastaId_.clear();
    ProgressoGlobal::obterInstancia().definirDica({}, {});
}

void ArvoreBackupComponent::mouseMove(const juce::MouseEvent& e) {
    std::string idSob;
    if (areaCanvas().contains(e.getPosition())) {
        const auto p = screenToCanvas(e.getPosition());
        for (const auto& n : nodes_)
            if (n.bounds.toFloat().contains(p)) { idSob = n.id; break; }
    }
    if (idSob == dicaPastaId_) return;  // mesmo alvo: o timer de 1 s segue contando (ou a dica já está no rodapé)
    limparDicaPasta();
    if (idSob.empty()) return;
    dicaPastaId_ = idSob;
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    dicaPasta_.aoTick = [safeThis] {
        if (!safeThis) return;
        for (const auto& n : safeThis->nodes_)
            if (n.id == safeThis->dicaPastaId_) {
                ProgressoGlobal::obterInstancia().definirDica(i18n::t("arvore_backup.dica_pasta"), n.nome);
                return;
            }
    };
    dicaPasta_.startTimer(1000);
}

void ArvoreBackupComponent::mouseExit(const juce::MouseEvent&) {
    limparDicaPasta();
}

void ArvoreBackupComponent::mouseDown(const juce::MouseEvent& e) {
    limparDicaPasta();  // clicou: a dica de hover não vale mais
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

    // SEM PASTA (Fase 1) — card fixo na coluna esquerda, nunca aparece no ORIGINAL.
    if (!mapaAtivoEhOriginal() && boundsSemPasta().contains(e.getPosition())) {
        if (aoMostrarConteudoNaGrade) aoMostrarConteudoNaGrade(projeto_.itensSemPasta(mapaAtivoId_));
        return;
    }

    // Clique no fundo da coluna esquerda (fora dos controles): nada a fazer.
    if (!areaCanvas().contains(e.getPosition())) return;

    auto canvasClick = screenToCanvas(e.getPosition());

    // Check Output Socket Handles for dragging connections (in canvas space)
    // — ORIGINAL é somente leitura, nunca aceita reconectar pastas (S4/13).
    if (!mapaAtivoEhOriginal()) {
        for (size_t i = 0; i < nodes_.size(); ++i) {
            juce::Point<float> outPort(static_cast<float>(nodes_[i].bounds.getRight()), static_cast<float>(nodes_[i].bounds.getCentreY()));
            // Raio de captura em pixels de TELA, limitado a uma fração do card: com o zoom afastado o card
            // fica pequeno e um raio fixo engolia o corpo dele (impedia selecionar/mover).
            const float raioTela = juce::jmin(12.0f, 0.18f * static_cast<float>(nodes_[i].bounds.getWidth()) * zoom_);
            const float raio = raioTela / zoom_;
            if (outPort.getDistanceSquaredFrom(canvasClick) <= raio * raio) {
                socketDragParentId_ = nodes_[i].id;
                socketDragPos_ = canvasClick;
                repaint();
                return;
            }
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
        // ORIGINAL: seleção/"Show in Grid" continuam livres, só a posição
        // não é arrastável (nunca persistiria mesmo, ver kMapaOriginal).
        nodeDragIndice_ = mapaAtivoEhOriginal() ? -1 : hitIndex;
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
        bool selecaoIncremental = e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isShiftDown();

        if (selecaoIncremental && !e.mods.isPopupMenu()) {
            hitNodeRef.selecionado = !hitNodeRef.selecionado;
        } else if (!clickedIsPartOfMultiSelection) {
            for (auto& n : nodes_) n.selecionado = false;
            hitNodeRef.selecionado = true;
        }

        // Item 8: arrastar uma pasta com filhas move o bloco inteiro (ela +
        // descendentes) junto — SHIFT arrasta só a pasta sob o cursor,
        // ignorando seleção e descendentes ("move solo", regra única pra
        // qualquer pasta do canvas). Sem SHIFT, cada pasta selecionada
        // (item 10) contribui sua própria subárvore; uma pasta que caia em
        // mais de um bloco (selecionada E descendente de outra selecionada)
        // se move uma vez só (std::set dedup).
        std::set<int> indicesParaMover;
        if (e.mods.isShiftDown()) {
            indicesParaMover.insert(hitIndex);
        } else {
            for (size_t i = 0; i < nodes_.size(); ++i) {
                if (!nodes_[i].selecionado) continue;
                int idx = static_cast<int>(i);
                indicesParaMover.insert(idx);
                coletarDescendentesIndices(idx, indicesParaMover);
            }
        }
        arrastoGrupoPosicoesIniciais_.clear();
        for (int idx : indicesParaMover) {
            arrastoGrupoPosicoesIniciais_.push_back({idx, nodes_[static_cast<size_t>(idx)].bounds.getPosition().toFloat()});
        }
        if (nodeDragIndice_ >= 0) {
            autoRolagem_.aoTick = [this] { tickAutoRolagem(); };
            autoRolagem_.startTimer(30);
        }

        if (e.mods.isPopupMenu()) {
            std::vector<std::string> selectedIds;
            for (const auto& n : nodes_) if (n.selecionado) selectedIds.push_back(n.id);
            bool batch = selectedIds.size() > 1;

            std::string pId = hitNodeRef.id;
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
            if (!batch && !sobreposto_) {
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
            }

            // Auto-organização: só no mapa do usuário, uma pasta por vez; subpasta AUTO não aceita regra própria.
            if (!batch && !mapaAtivoEhOriginal() && !hitNodeRef.autoSub) {
                juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
                const juce::String nomeNo = hitNodeRef.nome;
                const juce::String regraNo = hitNodeRef.regra;
                const bool editavel = !projeto_.somenteLeitura();
                if (regraNo.isEmpty()) {
                    menu.addItem(i18n::t("arvore_backup.auto_organizar"), editavel, false, [safeThis, pId, nomeNo] {
                        if (safeThis && safeThis->pastaPodeAutoOrganizar(pId))
                            safeThis->mostrarEditorAutoOrganizar(pId, nomeNo, {});
                    });
                } else {
                    menu.addItem(i18n::t("arvore_backup.auto_editar"), editavel, false, [safeThis, pId, nomeNo, regraNo] {
                        if (safeThis && safeThis->pastaPodeAutoOrganizar(pId))
                            safeThis->mostrarEditorAutoOrganizar(pId, nomeNo, regraNo);
                    });
                    menu.addItem(i18n::t("arvore_backup.auto_reorganizar"), editavel, false, [safeThis, pId, regraNo] {
                        if (safeThis && safeThis->pastaPodeAutoOrganizar(pId))
                            safeThis->aplicarAutoOrganizacaoNaPasta(pId, regraNo.toStdString());
                    });
                    menu.addItem(i18n::t("arvore_backup.auto_desligar"), editavel, false, [safeThis, pId] {
                        if (!safeThis) return;
                        safeThis->projeto_.desligarAutoOrganizacao(pId);
                        safeThis->recarregar();
                    });
                }
                menu.addSeparator();
            }

            // Disconnect from Parent (item 7): agora funciona em lote também
            // — D é o atalho equivalente (ver keyPressed). Habilitado se
            // ALGUMA selecionada tem pai (as sem pai são puladas em silêncio
            // por desconectarSelecionadas(), sem erro).
            {
                bool algumaComPai = false;
                for (const auto& id : selectedIds) {
                    for (const auto& n : nodes_) {
                        if (n.id == id && !n.pastaPaiId.empty()) { algumaComPai = true; break; }
                    }
                    if (algumaComPai) break;
                }
                juce::PopupMenu::Item item(i18n::t("arvore_backup.desconectar_pai"));
                item.itemID = -1;
                item.isEnabled = algumaComPai;
                item.shortcutKeyDescription = "D";
                item.action = [this] { desconectarSelecionadas(); };
                menu.addItem(std::move(item));
            }

            if (!batch) {
                menu.addItem(i18n::t("arvore_backup.renomear_pasta_menu"), [this, pId, idx] {
                    iniciarEdicaoInline(idx);
                });
            }

            if (selectedIds.size() == 2 && !mapaAtivoEhOriginal()) {
                // MERGE de duas pastas: escolhe quem some (a outra recebe o conteúdo).
                juce::PopupMenu sub;
                juce::String nomeA, nomeB;
                for (const auto& n : nodes_) {
                    if (n.id == selectedIds[0]) nomeA = n.nome;
                    if (n.id == selectedIds[1]) nomeB = n.nome;
                }
                const bool editavel = !projeto_.somenteLeitura();
                sub.addItem(i18n::t("arvore_backup.mesclar_em").replace("{a}", nomeA).replace("{b}", nomeB), editavel, false,
                            [this, a = selectedIds[0], b = selectedIds[1]] { iniciarMesclaPastas(a, b); });
                sub.addItem(i18n::t("arvore_backup.mesclar_em").replace("{a}", nomeB).replace("{b}", nomeA), editavel, false,
                            [this, a = selectedIds[0], b = selectedIds[1]] { iniciarMesclaPastas(b, a); });
                menu.addSubMenu(i18n::t("arvore_backup.mesclar_menu"), sub);
            }

            menu.addItem(i18n::t("arvore_backup.alternar_ativo"), [this, selectedIds] {
                for (const auto& id : selectedIds) alternarAtivoPasta(id);
            });

            // FOLDER COLOR (item 12): funciona igual pra uma pasta só ou pro
            // lote inteiro selecionado — selectedIds já cobre os dois casos.
            // C é o atalho equivalente (item 9).
            {
                auto topLeft = canvasToScreen(hitNodeRef.bounds.getTopLeft().toFloat());
                auto bottomRight = canvasToScreen(hitNodeRef.bounds.getBottomRight().toFloat());
                juce::Rectangle<int> ancora(topLeft, bottomRight);
                juce::PopupMenu::Item item(i18n::t("arvore_backup.cor_pasta"));
                item.itemID = -1;
                item.shortcutKeyDescription = "C";
                item.action = [this, selectedIds, ancora] { mostrarSeletorDeCorPasta(selectedIds, ancora); };
                menu.addItem(std::move(item));
            }

            menu.addItem(i18n::t("arvore_backup.apagar_pasta_menu"), [this, selectedIds] {
                // Uma pasta apagada leva as subpastas; as já removidas junto com a pai são puladas. Um só Undo.
                projeto_.iniciarGrupoUndo("Delete Folders");
                for (const auto& id : selectedIds) apagarPastaSelecionada(id);
                projeto_.finalizarGrupoUndo();
            });
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
        repaint(areaCanvas());  // só o canvas: a coluna esquerda não muda durante a interação
        return;
    }

    if (panning_) {
        auto delta = e.getPosition() - panStart_;
        panOffset_.x += delta.x;
        panOffset_.y += delta.y;
        panStart_ = e.getPosition();
        repaint(areaCanvas());
        return;
    }

    if (!socketDragParentId_.empty()) {
        socketDragPos_ = screenToCanvas(e.getPosition());
        repaint(areaCanvas());
        return;
    }

    if (nodeDragIndice_ >= 0 && nodeDragIndice_ < static_cast<int>(nodes_.size())) moverArrastoPara(e.getPosition());
}

// Posiciona o bloco arrastado como se o mouse estivesse em `mouseTela` (coordenadas do componente).
void ArvoreBackupComponent::moverArrastoPara(juce::Point<int> mouseTela) {
    if (nodeDragIndice_ >= 0 && nodeDragIndice_ < static_cast<int>(nodes_.size())) {
        auto canvasPos = screenToCanvas(mouseTela - arrastoOffset_);

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
        repaint(areaCanvas());
    }
}

void ArvoreBackupComponent::tickAutoRolagem() {
    if (nodeDragIndice_ < 0 || nodeDragIndice_ >= static_cast<int>(nodes_.size()) || !isMouseButtonDown()) {
        autoRolagem_.stopTimer();
        return;
    }
    const auto canvas = areaCanvas();
    const auto mouse = getMouseXYRelative();
    constexpr int kMargem = 32;
    constexpr float kVelMax = 10.0f;  // px de tela por tick (~30 ms): devagar
    auto empurrao = [&](int pos, int ini, int fim) {
        if (pos < ini + kMargem) return juce::jmin(1.0f, static_cast<float>(ini + kMargem - pos) / kMargem) * kVelMax;
        if (pos > fim - kMargem) return -juce::jmin(1.0f, static_cast<float>(pos - (fim - kMargem)) / kMargem) * kVelMax;
        return 0.0f;
    };
    const float dx = empurrao(mouse.x, canvas.getX(), canvas.getRight());
    const float dy = empurrao(mouse.y, canvas.getY(), canvas.getBottom());
    if (dx == 0.0f && dy == 0.0f) return;
    panOffset_.x += dx;
    panOffset_.y += dy;
    moverArrastoPara(mouse);  // o bloco segue o mouse, que agora aponta pra outra região do canvas
    repaint(areaCanvas());
}

void ArvoreBackupComponent::mouseUp(const juce::MouseEvent& e) {
    panning_ = false;
    autoRolagem_.stopTimer();

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

    if (nodeDragIndice_ >= 0 && nodeDragIndice_ < static_cast<int>(nodes_.size()) && !arrastoGrupoPosicoesIniciais_.empty()) {
        // Um folder nunca fica em cima de outro: se o bloco arrastado soltou sobrepondo alguém, volta à
        // posição que tinha quando o arrasto começou.
        {
            std::set<int> movidos;
            for (auto& [idx, posInicial] : arrastoGrupoPosicoesIniciais_) { juce::ignoreUnused(posInicial); movidos.insert(idx); }
            bool sobrepoe = false;
            for (int m : movidos) {
                if (m < 0 || m >= static_cast<int>(nodes_.size())) continue;
                const auto& b = nodes_[static_cast<size_t>(m)].bounds;
                for (size_t k = 0; k < nodes_.size() && !sobrepoe; ++k) {
                    if (movidos.count(static_cast<int>(k))) continue;
                    if (b.intersects(nodes_[k].bounds)) sobrepoe = true;
                }
                if (sobrepoe) break;
            }
            if (sobrepoe) {
                for (auto& [idx, posInicial] : arrastoGrupoPosicoesIniciais_)
                    if (idx >= 0 && idx < static_cast<int>(nodes_.size()))
                        nodes_[static_cast<size_t>(idx)].bounds.setPosition(static_cast<int>(posInicial.x), static_cast<int>(posInicial.y));
                nodeDragIndice_ = -1;
                arrastoGrupoPosicoesIniciais_.clear();
                repaint();
                return;
            }
        }
        // Item 8/10: persiste a posição do bloco inteiro (pasta + descendentes
        // arrastadas junto) numa única transação ao soltar o mouse — com
        // centenas de pastas, N UPDATEs isolados seriam N transações
        // implícitas.
        std::vector<int> indices;
        indices.reserve(arrastoGrupoPosicoesIniciais_.size());
        for (auto& [idx, posInicial] : arrastoGrupoPosicoesIniciais_) {
            juce::ignoreUnused(posInicial);
            indices.push_back(idx);
        }
        persistirPosicoesEmLote(indices);
    }
    nodeDragIndice_ = -1;
    arrastoGrupoPosicoesIniciais_.clear();
    repaint(areaCanvas());  // fim do arrasto/pan: a borda da seleção volta ao tracejado
}

void ArvoreBackupComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!areaCanvas().contains(e.getPosition())) return;
    auto canvasClick = screenToCanvas(e.getPosition());
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
        if (nodes_[static_cast<size_t>(i)].bounds.toFloat().contains(canvasClick)) {
            // Mapa principal: abre a pasta num Folder Map sobreposto. Dentro da sobreposição: renomeia, como antes.
            if (sobreposto_) iniciarEdicaoInline(i);
            else abrirSobreposicao(nodes_[static_cast<size_t>(i)].id, nodes_[static_cast<size_t>(i)].nome);
            return;
        }
    }
}

void ArvoreBackupComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const auto canvas = areaCanvas();
    if (!canvas.contains(e.getPosition())) return;
    float delta = (wheel.deltaY > 0) ? 1.15f : (1.0f / 1.15f);
    aplicarZoom(zoom_ * delta, (e.getPosition() - canvas.getPosition()).toFloat());
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
        const auto canvas = areaCanvas();
        auto centro = canvas.withZeroOrigin().getCentre().toFloat();
        for (const auto& n : nodes_)
            if (n.selecionado) { centro = (canvasToScreen(n.bounds.getCentre().toFloat()) - canvas.getPosition()).toFloat(); break; }
        aplicarZoom(zoom_ * 1.2f, centro);
        return true;
    }
    if (key == juce::KeyPress('-') || key == juce::KeyPress(juce::KeyPress::numberPadSubtract)) {
        const auto canvas = areaCanvas();
        auto centro = canvas.withZeroOrigin().getCentre().toFloat();
        for (const auto& n : nodes_)
            if (n.selecionado) { centro = (canvasToScreen(n.bounds.getCentre().toFloat()) - canvas.getPosition()).toFloat(); break; }
        aplicarZoom(zoom_ / 1.2f, centro);
        return true;
    }
    if (key == juce::KeyPress(juce::KeyPress::F2Key)) {
        for (int i = 0; i < static_cast<int>(nodes_.size()); ++i)
            if (nodes_[static_cast<size_t>(i)].selecionado) { iniciarEdicaoInline(i); return true; }
    }

    // Item 9 — C (Folder Color) e D (Disconnect from Parent): nunca
    // enquanto o usuário está renomeando uma pasta (inlineEditor_ com
    // foco) — proteção explícita, além do roteamento normal de foco do
    // JUCE já impedir isto na prática. Nunca no ORIGINAL (somente leitura).
    if (inlineEditor_ && inlineEditor_->hasKeyboardFocus(true)) return false;

    auto c = key.getTextCharacter();
    bool semModificadores = !key.getModifiers().isCommandDown() &&
                            !key.getModifiers().isCtrlDown() &&
                            !key.getModifiers().isAltDown();

    if ((key.getKeyCode() == 'D' || c == 'd' || c == 'D') && semModificadores) {
        if (mapaAtivoEhOriginal()) return false;
        desconectarSelecionadas();
        return true;
    }

    if ((key.getKeyCode() == 'C' || c == 'c' || c == 'C') && semModificadores) {
        if (mapaAtivoEhOriginal()) return false;
        // C de novo com o picker aberto fecha em vez de reabrir.
        if (corCallout_ != nullptr) {
            corCallout_->dismiss();
            corCallout_ = nullptr;
            return true;
        }
        std::vector<std::string> selectedIds;
        for (const auto& n : nodes_) if (n.selecionado) selectedIds.push_back(n.id);
        if (selectedIds.empty()) return false;
        for (const auto& n : nodes_) {
            if (n.id == selectedIds.front()) {
                auto topLeft = canvasToScreen(n.bounds.getTopLeft().toFloat());
                auto bottomRight = canvasToScreen(n.bounds.getBottomRight().toFloat());
                mostrarSeletorDeCorPasta(selectedIds, juce::Rectangle<int>(topLeft, bottomRight));
                break;
            }
        }
        return true;
    }

    return false;
}

// Bloqueios da auto-organização (o mapa ORIGINAL nem oferece o menu): somente leitura e
// pasta que já tem arquivos no MAIN avisam e não abrem o editor.
bool ArvoreBackupComponent::pastaPodeAutoOrganizar(const std::string& pastaId) {
    if (mapaAtivoEhOriginal()) return false;
    if (projeto_.somenteLeitura()) { ProjetoAberto::avisarSomenteLeitura(); return false; }
    if (projeto_.pastaTemArquivosNoMain(pastaId)) {
        ProjetoAberto::avisarMapaTravado(i18n::t("arvore_backup.auto_no_main"));
        return false;
    }
    return true;
}

void ArvoreBackupComponent::mostrarEditorAutoOrganizar(const std::string& pastaId, const juce::String& nomePasta,
                                                        const juce::String& regraAtual) {
    using N = matriz::consolidacao::NivelHierarquia;
    matriz::consolidacao::HierarquiaBackup atual;
    if (regraAtual.isNotEmpty()) atual = matriz::consolidacao::hierarquiaDeCsv(regraAtual.toStdString());

    HierarquiaEditorOpcoes opcoes;
    opcoes.titulo = i18n::t("arvore_backup.auto_titulo_janela").replace("{nome}", nomePasta);
    opcoes.paleta = {N::Ano, N::TipoMidia, N::TipoArquivo, N::Origem, N::Artista, N::ContentType, N::Subject, N::Lista};
    opcoes.raizPreview = "/" + nomePasta;
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    new HierarquiaEditorWindow(atual, [safeThis, pastaId](const matriz::consolidacao::HierarquiaBackup& h) {
        if (!safeThis || h.empty()) return;
        safeThis->aplicarAutoOrganizacaoNaPasta(pastaId, matriz::consolidacao::hierarquiaParaCsv(h));
    }, opcoes);
}

void ArvoreBackupComponent::aplicarAutoOrganizacaoNaPasta(const std::string& pastaId, const std::string& regraCsv) {
    const auto r = projeto_.aplicarAutoOrganizacao(pastaId, regraCsv, false);
    using S = ProjetoAberto::StatusAutoOrg;
    switch (r.status) {
        case S::Ok: break;
        case S::SomenteLeitura: ProjetoAberto::avisarSomenteLeitura(); break;
        case S::TemArquivosNoMain: ProjetoAberto::avisarMapaTravado(i18n::t("arvore_backup.auto_no_main")); break;
        default: ProjetoAberto::avisarMapaTravado(i18n::t("arvore_backup.auto_falha")); break;
    }
    recarregar();
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
    if (btnAutoArranjar_) btnAutoArranjar_->setButtonText(i18n::t("arvore_backup.btn_auto_arranjar"));
    if (btnZoomFit_) btnZoomFit_->setButtonText(i18n::t("arvore_backup.btn_fit"));
    if (btnResetTamanhos_) {
        btnResetTamanhos_->setButtonText(i18n::t("arvore_backup.btn_reset_tamanhos"));
        btnResetTamanhos_->setTooltip(i18n::t("arvore_backup.reset_tamanhos_dica"));
    }
    if (btnNovoMapa_) btnNovoMapa_->setButtonText(i18n::t("arvore_backup.btn_novo_mapa"));
    if (sliderTamanho_) sliderTamanho_->setTooltip(i18n::t("arvore_backup.slider_tamanho_tooltip"));
    if (rotuloSpread_) rotuloSpread_->setText(i18n::t("arvore_backup.spread_rotulo"), juce::dontSendNotification);
    if (sliderSpread_) sliderSpread_->setTooltip(i18n::t("arvore_backup.spread_tooltip"));
    if (rotuloSpreadV_) rotuloSpreadV_->setText(i18n::t("arvore_backup.spread_v_rotulo"), juce::dontSendNotification);
    if (sliderSpreadV_) sliderSpreadV_->setTooltip(i18n::t("arvore_backup.spread_v_tooltip"));
    if (detailContent_) detailContent_->lookAndFeelChanged();
    resized();
    repaint();
}

void ArvoreBackupComponent::timerCallback() {
    // S4/15 — anima o pulso do destaque da pasta nova, por ~2 s ou enquanto estiver visível
    if (juce::Time::getMillisecondCounter() - inicioPulsoMs_ >= kDuracaoPulsoMs ||
        (getPeer() != nullptr && !isShowing())) {
        pararPulso();
        return;
    }
    repaint();
}

void ArvoreBackupComponent::pararPulso() {
    stopTimer();
    if (!destaqueNovaPastaId_.empty()) {
        destaqueNovaPastaId_.clear();
        repaint();
    }
}

void ArvoreBackupComponent::visibilityChanged() {
    if (!isVisible()) pararPulso();
}

// ── S4/14 — slider de tamanho ────────────────────────────────────────

// Escala GLOBAL (slider sem pasta selecionada): tamanhos e distâncias entre os cartões crescem juntos em
// torno do centro do mapa, então a diferença entre os níveis da hierarquia e a ausência de sobreposição
// são preservadas por construção.
void ArvoreBackupComponent::aplicarEscalaTamanho(float escala) {
    for (auto& n : nodes_) {
        const auto c = n.boundsOriginal.getCentre().toFloat();
        const juce::Point<float> novoCentro(pivotEscala_.x + (c.x - pivotEscala_.x) * escala * espalharX_,
                                            pivotEscala_.y + (c.y - pivotEscala_.y) * escala * espalharY_);
        const int w = juce::jmax(6, juce::roundToInt(n.boundsOriginal.getWidth() * escala));
        const int h = juce::jmax(4, juce::roundToInt(n.boundsOriginal.getHeight() * escala));
        n.bounds = juce::Rectangle<int>(w, h).withCentre(novoCentro.toInt());
    }
    repaint();
}

std::vector<int> ArvoreBackupComponent::indicesSelecionados() const {
    std::vector<int> out;
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].selecionado) out.push_back(static_cast<int>(i));
    return out;
}

// O valor do slider mostra a escala da seleção (média) ou, sem seleção, a global.
void ArvoreBackupComponent::sincronizarSlider() {
    if (!sliderTamanho_) return;
    const auto sel = indicesSelecionados();
    double v = escalaTamanho_;
    if (!sel.empty()) {
        double soma = 0.0;
        for (int i : sel) soma += nodes_[static_cast<size_t>(i)].escalaManual;
        v = soma / static_cast<double>(sel.size());
    }
    sliderTamanho_->setValue(std::round(v * 100.0), juce::dontSendNotification);
}

void ArvoreBackupComponent::aplicarSlider() {
    if (!sliderTamanho_) return;
    const double pedido = sliderTamanho_->getValue() / 100.0;
    const auto sel = indicesSelecionados();
    if (sel.empty()) {
        escalaTamanho_ = static_cast<float>(pedido);
        aplicarEscalaTamanho(escalaTamanho_);
        return;
    }

    // Só as pastas selecionadas. O que encostar num vizinho para ali: o slider não empurra nem
    // reorganiza ninguém.
    std::vector<layoutarvore::AlvoEscala> alvos;
    std::vector<layoutarvore::CaixaAuto> fixos;
    std::set<int> ehAlvo(sel.begin(), sel.end());
    for (int i : sel) {
        const auto& n = nodes_[static_cast<size_t>(i)];
        layoutarvore::AlvoEscala a;
        a.cx = n.boundsOriginal.getCentreX();
        a.cy = n.boundsOriginal.getCentreY();
        a.autoW = n.autoW;
        a.autoH = n.autoH;
        a.de = n.escalaManual;
        a.para = pedido;
        alvos.push_back(a);
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (ehAlvo.count(static_cast<int>(i))) continue;
        const auto& b = nodes_[i].boundsOriginal;
        fixos.push_back({b.getX(), b.getY(), b.getWidth(), b.getHeight()});
    }
    const double t = layoutarvore::fracaoPermitidaDoCrescimento(alvos, fixos);
    for (size_t k = 0; k < sel.size(); ++k) {
        auto& n = nodes_[static_cast<size_t>(sel[k])];
        n.escalaManual = static_cast<float>(alvos[k].de + (alvos[k].para - alvos[k].de) * t);
        const auto c = layoutarvore::caixaDoAlvo(alvos[k], t);
        n.boundsOriginal = juce::Rectangle<int>(c.x, c.y, c.w, c.h);
        if (mapaAtivoEhOriginal()) escalasOriginal_[n.id] = n.escalaManual;
    }
    escalasSujas_ = true;
    aplicarEscalaTamanho(escalaTamanho_);
    if (t < 1.0) sincronizarSlider();  // o slider mostra onde o folder realmente parou
}

void ArvoreBackupComponent::persistirEscalas() {
    if (!escalasSujas_) return;
    escalasSujas_ = false;
    if (sobreposto_) return;  // idem: tamanhos da sobreposição não são gravados
    if (mapaAtivoEhOriginal()) return;  // virtual: só em memória
    projeto_.iniciarGrupoUndo("Resize Folders");
    projeto_.registrarUndoVisualMapa("Resize Folders", mapaAtivoId_);
    std::vector<std::pair<std::string, double>> escalas;
    escalas.reserve(nodes_.size());
    for (const auto& n : nodes_) escalas.push_back({n.id, static_cast<double>(n.escalaManual)});
    projeto_.atualizarEscalasPastasAcervo(escalas);
    if (fatorCards_ > 0.0) projeto_.definirFatorCardsDoMapa(mapaAtivoId_, fatorCards_);
    // Posições/tamanhos a 100% ficam consistentes com o que foi gravado.
    std::vector<int> todos;
    for (size_t i = 0; i < nodes_.size(); ++i) todos.push_back(static_cast<int>(i));
    persistirPosicoesEmLote(todos);
    projeto_.finalizarGrupoUndo();
}

// RESET SIZES: volta cada pasta ao tamanho calculado pelo auto-arranjo (multiplicador 1) e a escala
// global a 100%, mantendo a diferença entre os níveis. As que encolhem vão primeiro; as que precisam
// crescer de volta param se encostarem em alguma vizinha.
void ArvoreBackupComponent::redefinirTamanhos() {
    if (nodes_.empty()) return;
    escalaTamanho_ = 1.0f;
    for (auto& n : nodes_) {
        if (n.escalaManual > 1.0f) {
            const auto c = n.boundsOriginal.getCentre();
            n.escalaManual = 1.0f;
            n.boundsOriginal = juce::Rectangle<int>(n.autoW, n.autoH).withCentre(c);
        }
    }
    std::vector<layoutarvore::AlvoEscala> alvos;
    std::vector<int> indicesAlvo;
    std::vector<layoutarvore::CaixaAuto> fixos;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const auto& n = nodes_[i];
        if (n.escalaManual < 1.0f) {
            layoutarvore::AlvoEscala a;
            a.cx = n.boundsOriginal.getCentreX();
            a.cy = n.boundsOriginal.getCentreY();
            a.autoW = n.autoW;
            a.autoH = n.autoH;
            a.de = n.escalaManual;
            a.para = 1.0;
            alvos.push_back(a);
            indicesAlvo.push_back(static_cast<int>(i));
        } else {
            const auto& b = n.boundsOriginal;
            fixos.push_back({b.getX(), b.getY(), b.getWidth(), b.getHeight()});
        }
    }
    if (!alvos.empty()) {
        const double t = layoutarvore::fracaoPermitidaDoCrescimento(alvos, fixos);
        for (size_t k = 0; k < indicesAlvo.size(); ++k) {
            auto& n = nodes_[static_cast<size_t>(indicesAlvo[k])];
            n.escalaManual = static_cast<float>(alvos[k].de + (alvos[k].para - alvos[k].de) * t);
            const auto c = layoutarvore::caixaDoAlvo(alvos[k], t);
            n.boundsOriginal = juce::Rectangle<int>(c.x, c.y, c.w, c.h);
        }
    }
    if (mapaAtivoEhOriginal()) {
        escalasOriginal_.clear();
        for (const auto& n : nodes_)
            if (std::abs(n.escalaManual - 1.0f) > 0.001f) escalasOriginal_[n.id] = n.escalaManual;
    }
    escalasSujas_ = true;
    aplicarEscalaTamanho(escalaTamanho_);
    persistirEscalas();
    sincronizarSlider();
}

// FIT: enquadra o mapa inteiro na área visível (também amplia, se o mapa for pequeno).
void ArvoreBackupComponent::enquadrarTudo() {
    if (nodes_.empty()) return;
    const auto canvas = areaCanvas();
    if (canvas.getWidth() < 50 || canvas.getHeight() < 50) return;
    juce::Rectangle<int> uniao = nodes_.front().bounds;
    for (const auto& n : nodes_) uniao = uniao.getUnion(n.bounds);
    constexpr float kMargem = 30.0f;
    const float zx = (static_cast<float>(canvas.getWidth()) - 2.0f * kMargem) / static_cast<float>(std::max(1, uniao.getWidth()));
    const float zy = (static_cast<float>(canvas.getHeight()) - 2.0f * kMargem) / static_cast<float>(std::max(1, uniao.getHeight()));
    zoom_ = juce::jlimit(0.03f, 3.0f, std::min(zx, zy));  // FIT mostra tudo, mesmo de longe
    const auto centro = uniao.getCentre().toFloat();
    panOffset_ = {static_cast<float>(canvas.getWidth()) * 0.5f - centro.x * zoom_,
                  static_cast<float>(canvas.getHeight()) * 0.5f - centro.y * zoom_};
    enquadrarPendente_ = false;
    repaint();
}

// ── S4/15 — posição livre pra pasta nova ─────────────────────────────

juce::Point<int> ArvoreBackupComponent::posicaoLivrePertoDoCentro(int nodeW, int nodeH) const {
    const auto canvas = areaCanvas();
    auto topLeft = screenToCanvas(canvas.getTopLeft());
    auto bottomRight = screenToCanvas(canvas.getBottomRight());
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

// Fase 1 — nunca substitui o mapa ativo: sempre cria um folder_map NOVO
// (em branco) e recria pastas/itens do JSON dentro dele. Usado por Import
// from file e pela migração de presets antigos.
void ArvoreBackupComponent::importarEsquemaComoNovoMapa(const juce::String& nomeMapa, const juce::var& dados,
                                                         int& itensRelocados, int& itensPulados) {
    itensRelocados = 0;
    itensPulados = 0;
    if (!dados.isObject()) return;

    std::string novoMapaId = projeto_.criarFolderMap(nomeMapa, std::nullopt);
    if (novoMapaId.empty()) return;

    // Recria pastas do preset em ordem pai-antes-de-filho, remapeando o id
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
                std::string novoId = projeto_.criarPastaAcervo(nome.toStdString(), paiNovo, novoMapaId);
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
            std::string novoId = projeto_.criarPastaAcervo(nome.toStdString(), std::nullopt, novoMapaId);
            idAntigoParaNovo[idAntigo] = novoId;
            projeto_.atualizarPosicaoPastaAcervo(novoId, static_cast<int>(p["x"]), static_cast<int>(p["y"]));
        }
    }

    // Recoloca os itens: por item_id quando é o mesmo projeto do preset, por
    // codigoAcervo quando é de outro (item_id original não existe aqui).
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

    selecionarMapaPorId(novoMapaId);
    recarregarComboMapas();
    recarregar();
}

void ArvoreBackupComponent::confirmarImportarComoNovoMapa(const juce::String& nomeSugerido, const juce::var& dados) {
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    pedirTextoBackup(i18n::t("arvore_backup.mapa_novo_titulo"), i18n::t("arvore_backup.preset_importar_nome_msg"),
                     nomeSugerido,
        [safeThis, dados](std::optional<juce::String> nomeMapa) {
            if (!safeThis || !nomeMapa || nomeMapa->trim().isEmpty()) return;
            int relocados = 0, pulados = 0;
            safeThis->importarEsquemaComoNovoMapa(nomeMapa->trim(), dados, relocados, pulados);
            juce::String msg = pulados > 0
                ? i18n::t("arvore_backup.preset_carregado").replace("{n}", *nomeMapa).replace("{s}", juce::String(pulados))
                : i18n::t("arvore_backup.preset_carregado_ok").replace("{n}", *nomeMapa);
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle(i18n::t("arvore_backup.preset_importar_titulo"))
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
            safeThis->confirmarImportarComoNovoMapa(arquivo.getFileNameWithoutExtension(), dados);
        });
}

// ── Fase 1 — dropdown/menu de Folder Maps múltiplos ──────────────────

void ArvoreBackupComponent::recarregarComboMapas() {
    mapasCache_ = projeto_.listarFolderMaps();
    comboMapas_->clear(juce::dontSendNotification);
    int idSelecionar = -1;
    for (size_t i = 0; i < mapasCache_.size(); ++i) {
        int itemId = static_cast<int>(i) + 1; // juce::ComboBox: ids começam em 1
        comboMapas_->addItem(mapasCache_[i].nome, itemId);
        if (mapasCache_[i].id == mapaAtivoId_) idSelecionar = itemId;
    }
    if (idSelecionar < 0 && !mapasCache_.empty()) {
        // Mapa ativo sumiu (apagado) — volta pro primeiro mapa do usuário.
        size_t idx = mapasCache_.size() > 1 ? 1 : 0;
        mapaAtivoId_ = mapasCache_[idx].id;
        projeto_.definirMapaAtivo(mapaAtivoId_);
        idSelecionar = static_cast<int>(idx) + 1;
    }
    comboMapas_->setSelectedId(idSelecionar, juce::dontSendNotification);
}

void ArvoreBackupComponent::selecionarMapaPorId(const std::string& mapaId) {
    if (mapaId == mapaAtivoId_) return;
    persistirEscalas();  // tamanhos ajustados no mapa que está saindo
    mapaAtivoId_ = mapaId;
    projeto_.definirMapaAtivo(mapaId);
    editingNodeId_.clear();
    selectedFolderId_.clear();
    atualizarEstadoBotoesParaMapa();
    recarregar();
}

void ArvoreBackupComponent::atualizarEstadoBotoesParaMapa() {
    bool original = mapaAtivoEhOriginal();
    resized();  // o card SEM PASTA some/volta conforme o mapa
    juce::String dica = original ? i18n::t("arvore_backup.original_somente_leitura") : juce::String();
    if (btnCriarPasta_) { btnCriarPasta_->setEnabled(!original); btnCriarPasta_->setTooltip(dica); }
    if (btnRenomearPasta_) { btnRenomearPasta_->setEnabled(!original); btnRenomearPasta_->setTooltip(dica); }
}

void ArvoreBackupComponent::mostrarMenuMapa() {
    bool original = mapaAtivoEhOriginal();
    juce::PopupMenu menu;
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);

    juce::String nomeAtivo;
    for (auto& m : mapasCache_) if (m.id == mapaAtivoId_) nomeAtivo = m.nome;

    menu.addItem(i18n::t("arvore_backup.mapa_renomear"), !original, false, [safeThis] {
        if (!safeThis) return;
        std::string mapaId = safeThis->mapaAtivoId_;
        juce::String nomeAtual;
        for (auto& m : safeThis->mapasCache_) if (m.id == mapaId) nomeAtual = m.nome;
        pedirTextoBackup(i18n::t("arvore_backup.mapa_renomear_titulo"), i18n::t("arvore_backup.mapa_renomear_msg"),
                         nomeAtual,
            [safeThis, mapaId](std::optional<juce::String> nome) {
                if (!safeThis || !nome || nome->trim().isEmpty()) return;
                safeThis->projeto_.renomearFolderMap(mapaId, nome->trim());
                safeThis->recarregarComboMapas();
            });
    });

    menu.addItem(i18n::t("arvore_backup.mapa_duplicar"), [safeThis, nomeAtivo] {
        if (!safeThis) return;
        std::string origemId = safeThis->mapaAtivoId_;
        pedirTextoBackup(i18n::t("arvore_backup.mapa_duplicar_titulo"), i18n::t("arvore_backup.mapa_duplicar_msg"),
                         nomeAtivo + " " + i18n::t("arvore_backup.mapa_copia_sufixo"),
            [safeThis, origemId](std::optional<juce::String> nome) {
                if (!safeThis || !nome || nome->trim().isEmpty()) return;
                std::string novoId = safeThis->projeto_.criarFolderMap(nome->trim(), origemId);
                if (novoId.empty()) return;
                safeThis->mapaAtivoId_ = novoId;
                safeThis->projeto_.definirMapaAtivo(novoId);
                safeThis->recarregarComboMapas();
                safeThis->atualizarEstadoBotoesParaMapa();
                safeThis->recarregar();
            });
    });

    menu.addItem(i18n::t("arvore_backup.mapa_apagar"),
                 !original && mapaAtivoId_ != projeto_.mapaDoMainId(), false, [safeThis] {
        if (!safeThis) return;
        int totalUsuario = 0;
        for (auto& m : safeThis->mapasCache_) if (!m.original) ++totalUsuario;
        if (totalUsuario <= 1) {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle(i18n::t("arvore_backup.mapa_apagar_titulo"))
                    .withMessage(i18n::t("arvore_backup.mapa_apagar_unico"))
                    .withButton(i18n::t("comum.ok")),
                nullptr);
            return;
        }
        std::string mapaId = safeThis->mapaAtivoId_;
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle(i18n::t("arvore_backup.mapa_apagar_titulo"))
                .withMessage(i18n::t("arvore_backup.mapa_apagar_confirmar"))
                .withButton(i18n::t("arvore_backup.mapa_apagar_titulo"))
                .withButton(i18n::t("comum.cancelar")),
            [safeThis, mapaId](int res) {
                if (res != 1 || !safeThis) return;
                if (!safeThis->projeto_.apagarFolderMap(mapaId)) {
                    ProjetoAberto::avisarMapaTravado(i18n::t("arvore_backup.mapa_do_main_nao_apaga"));
                    return;
                }
                safeThis->mapaAtivoId_ = ProjetoAberto::kMapaOriginal;
                safeThis->projeto_.definirMapaAtivo(safeThis->mapaAtivoId_);
                safeThis->recarregarComboMapas();
                safeThis->atualizarEstadoBotoesParaMapa();
                safeThis->recarregar();
            });
    });

    menu.addSeparator();
    menu.addItem(i18n::t("arvore_backup.preset_exportar"), [safeThis] { if (safeThis) safeThis->exportarPresetParaArquivo(); });
    menu.addItem(i18n::t("arvore_backup.preset_importar"), [safeThis] { if (safeThis) safeThis->importarPresetDeArquivo(); });

    menu.showMenuAsync(juce::PopupMenu::Options());
}

void ArvoreBackupComponent::mostrarDialogoNovoMapa() {
    juce::PopupMenu menu;
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);

    menu.addItem(i18n::t("arvore_backup.mapa_novo_em_branco"), [safeThis] {
        if (safeThis) safeThis->pedirNomeECriarMapa(std::nullopt);
    });
    menu.addItem(i18n::t("arvore_backup.mapa_novo_copia_original"), [safeThis] {
        if (safeThis) safeThis->pedirNomeECriarMapa(ProjetoAberto::kMapaOriginal);
    });
    menu.addItem(i18n::t("arvore_backup.mapa_novo_copia_atual"), !mapaAtivoEhOriginal(), false, [safeThis] {
        if (safeThis) safeThis->pedirNomeECriarMapa(safeThis->mapaAtivoId_);
    });

    menu.showMenuAsync(juce::PopupMenu::Options());
}

void ArvoreBackupComponent::pedirNomeECriarMapa(std::optional<std::string> origemMapaId) {
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    pedirTextoBackup(i18n::t("arvore_backup.mapa_novo_titulo"), i18n::t("arvore_backup.mapa_novo_msg"),
                     i18n::t("arvore_backup.mapa_novo_padrao"),
        [safeThis, origemMapaId](std::optional<juce::String> nome) {
            if (!safeThis || !nome || nome->trim().isEmpty()) return;
            std::string novoId = safeThis->projeto_.criarFolderMap(nome->trim(), origemMapaId);
            if (novoId.empty()) return;
            safeThis->mapaAtivoId_ = novoId;
            safeThis->projeto_.definirMapaAtivo(novoId);
            safeThis->recarregarComboMapas();
            safeThis->atualizarEstadoBotoesParaMapa();
            safeThis->recarregar();
        });
}

// Presets antigos (S4/13, arquivos presets_pastas/*.json) viram mapas do
// usuário uma vez só, na primeira abertura desta tela após o upgrade —
// cada arquivo migrado ganha o sufixo .importado (não é apagado) pra não
// ser reprocessado no próximo load. Melhor esforço: uma falha num arquivo
// não impede os demais nem trava a UI.
void ArvoreBackupComponent::migrarPresetsAntigosSeNecessario() {
    for (const auto& nome : listarPresetsSalvos()) {
        auto arquivo = pastaPresets().getChildFile(juce::File::createLegalFileName(nome) + ".json");
        auto dados = juce::JSON::parse(arquivo);
        if (dados.isObject()) {
            int relocados = 0, pulados = 0;
            importarEsquemaComoNovoMapa(nome, dados, relocados, pulados);
        }
        arquivo.moveFileTo(arquivo.getSiblingFile(arquivo.getFileName() + ".importado"));
    }
}

} // namespace matriz::ui
