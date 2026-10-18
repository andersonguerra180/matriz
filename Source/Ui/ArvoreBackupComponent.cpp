#include "ArvoreBackupComponent.h"
#include "TiposContainer.h"
#include "../I18n/Strings.h"
#include "../Ingest/FluxoLote.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Vault/Resolucao.h"
#include "HierarquiaEditorComponent.h"
#include "LayoutArvoreFolderMap.h"
#include "ModalMitigacao.h"
#include "ProgressoGlobal.h"
#include "Tokens.h"
#include "Moldura.h"
#include "OverlayComponent.h"
#include "PainelReceberCelular.h"
#include "PainelImportarWhatsapp.h"
#include "ConversaWhatsappComponent.h"
#include "Tablet.h"
#include "ContainerVisual.h"
#include "ImportarMenu.h"
#include "LogosTipoArquivo.h"
#include "TrashWindow.h"
#include "../Celular/ExistentesProjeto.h"
#include "../Whatsapp/WhatsappIngestao.h"
#include "../Whatsapp/ServidorWhatsappApp.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace matriz::ui {

// ── Detail panel content ─────────────────────────────────────────────

namespace {

class BotaoIconeCelular : public juce::Button {
public:
    BotaoIconeCelular() : juce::Button("celular") { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paintButton(juce::Graphics& g, bool sobre, bool apertado) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        desenharCartao(g, r, tk.raioCartao * 0.8f, apertado ? tk.acento.darker(0.15f) : (sobre ? tk.acentoHover : tk.acento), true);
        auto icone = r.removeFromLeft(r.getHeight());
        desenharIcone(g, IconeUi::Celular, icone.withSizeKeepingCentre(icone.getHeight() * 0.62f, icone.getHeight() * 0.62f), tk.textoSobreAcento);
        if (r.getWidth() > 60.0f) {
            r.removeFromRight(10.0f);
            g.setColour(tk.textoSobreAcento);
            g.setFont(fonteUi(tk.fonteCorpoGrande - 1.0f, juce::Font::bold).withExtraKerningFactor(0.05f));
            g.drawText(i18n::t("light.cel.importar"), r.removeFromTop(r.getHeight() * 0.55f).toNearestInt(), juce::Justification::bottomLeft, true);
            g.setFont(fonteUi(tk.tamanhoFontePequena));
            g.setColour(tk.textoSobreAcento.withAlpha(0.85f));
            g.drawText(i18n::t("light.cel.importarSub"), r.toNearestInt(), juce::Justification::topLeft, true);
        }
    }
};

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
    bool isTrash = false;
    std::vector<SubfolderEntry> subfolders;
    std::vector<FileEntry> files;
    std::function<void(const std::string&)> aoClicarArquivo;
    std::function<void(const std::string&, bool)> aoMenuArquivo;
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
        if (idx >= 0 && idx < static_cast<int>(files.size())) {
            const auto& fileId = files[static_cast<size_t>(idx)].id;
            if (e.mods.isPopupMenu() && aoMenuArquivo) {
                aoMenuArquivo(fileId, true);
                return;
            }
            if (aoClicarArquivo)
                aoClicarArquivo(fileId);
        } else if (e.mods.isPopupMenu() && aoMenuArquivo) {
            aoMenuArquivo("", true);
        }
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

        g.setColour(isTrash ? tema().perigo : tema().textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        juce::String tituloHeader = folderName;
        if (isTrash) tituloHeader += " " + i18n::t("arvore_backup.trash_sub");
        g.drawText(tituloHeader, area.removeFromTop(24), juce::Justification::centredLeft, true);

        area.removeFromTop(36); // Space for btnShowInGrid

        g.setColour(tema().borda);
        g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
        area.removeFromTop(4);

        if (!subfolders.empty()) {
            g.setColour(tema().textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
            g.drawText(i18n::t("arvore_backup.subpastas"), area.removeFromTop(20), juce::Justification::centredLeft);

            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
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
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
            g.drawText(i18n::t("arvore_backup.arquivos"), area.removeFromTop(20), juce::Justification::centredLeft);

            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
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
            g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
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
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
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
    static juce::Font fonte(bool negrito) { return juce::Font(juce::FontOptions(tema().tamanhoFontePequena, negrito ? juce::Font::bold : juce::Font::plain)); }
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

    chkContainersComoPastas_ = std::make_unique<juce::ToggleButton>(i18n::t("arvore_backup.containers_como_pastas"));
    chkContainersComoPastas_->setToggleState(containersComoPastas_, juce::dontSendNotification);
    chkContainersComoPastas_->setTooltip(i18n::t("arvore_backup.containers_como_pastas_dica"));
    chkContainersComoPastas_->setColour(juce::ToggleButton::textColourId, tema().textoPrimario);
    chkContainersComoPastas_->onClick = [this] {
        containersComoPastas_ = chkContainersComoPastas_->getToggleState();
        gravarLayoutSalvo();
        recarregar();
    };
    addAndMakeVisible(*chkContainersComoPastas_);

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
        rotulo->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
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
    addChildComponent(*detailViewport_);

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

    detailContent_->aoClicarArquivo = [this](const std::string& itemId) {
        if (matriz::whatsapp::itemEhConversa(projeto_.projeto().registroLeitura(), itemId)) {
            abrirConversaWhatsapp(itemId);
        }
    };
    detailContent_->aoMenuArquivo = [this](const std::string& itemId, bool) {
        mostrarMenuArquivoDetalhe(itemId);
    };

    // btnReceberCelular_ transferido para INTAKE (item 9C)
    setWantsKeyboardFocus(true);
    setOpaque(true);  // o paint faz fillAll do fundo inteiro

    migrarPresetsAntigosSeNecessario();
    carregarLayoutSalvo();
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
    janelaTrash_.reset();
    try { persistirEscalas(); } catch (...) {}
    EventBus::obterInstancia().removerListener(this);
    detailViewport_->setViewedComponent(nullptr, false);
    if (treeExplorador_) treeExplorador_->setRootItem(nullptr);
}

// ---- Folder Map SOBREPOSTO (duplo clique num folder-card) --------------------------------------------------------
namespace {
// Zona de clique invisível sobre o X redondo da moldura (o desenho é do moldura::desenhar): mesma ação do antigo EXIT.
class AreaFecharMoldura : public juce::Button {
public:
    AreaFecharMoldura() : juce::Button("fechar") { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paintButton(juce::Graphics&, bool, bool) override {}
};

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
        // O título vai para o cabeçalho da moldura (pintado em paint()); o EXIT vira o X redondo (mesma ação).
        btnSair_.setTooltip(i18n::t("arvore_backup.sobreposto_sair"));
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
        juce::ignoreUnused(tk);
        moldura::desenhar(g, moldura::geometria(painel(), false), moldura::Secao::Explorador, "Folder Map", titulo_.getText(), false);
    }
    void resized() override {
        const auto geo = moldura::geometria(painel(), false);
        btnSair_.setBounds(geo.botaoFechar);
        mapa_->setBounds(geo.corpo.reduced(12));
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
    AreaFecharMoldura btnSair_;
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
    // Com o painel já no tamanho final: o mesmo que o AJUSTAR (auto-organiza o recorte) e centraliza a árvore na tela.
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (!safeThis || !safeThis->sobreposto_) return;
        safeThis->autoArranjar();
        safeThis->enquadrarPendente_ = true;
        safeThis->tentarEnquadrar();
        safeThis->sincronizarSlider();
        safeThis->repaint();
    });
}

void ArvoreBackupComponent::aoItemAlterado(const EventoItemAlterado& e) {
    if (e.tipoAlteracao == "recarregar_tudo") {  // ex.: Edit Project Info mudou nome/capa de um container
        juce::Component::SafePointer<ArvoreBackupComponent> seguro(this);
        juce::MessageManager::callAsync([seguro] { if (seguro && seguro->isShowing()) seguro->recarregar(); });  // escondido: recarrega ao voltar à aba
        return;
    }
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
        moldura::lancar(opt);
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
        l.rotulo->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro, juce::Font::bold)));
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
    // Containers importados (celular/ingest de pasta) aparecem DEBAIXO do card _trash, que fica no topo.
    const auto trash = boundsPastaTrash();
    const int yPainel = trash.isEmpty() ? canvas.getY() + kMargem : trash.getBottom() + 12;
    const int alturaMax = std::max(PainelPropostas::kAlturaCabecalho + PainelPropostas::kAlturaRodape + 60, canvas.getBottom() - kMargem - yPainel);
    const int altura = painelPropostasRecolhido_
                           ? PainelPropostas::kAlturaCabecalho
                           : std::min(alturaMax, PainelPropostas::kAlturaCabecalho + PainelPropostas::kAlturaRodape + 8 + alturaConteudo);
    painelPropostas_->setBounds(canvas.getX() + kMargem, yPainel, largura, altura);
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
    trashFolderId_.clear();
    trashContagem_ = 0;
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
            if (no.nome == "_trash") {
                trashFolderId_ = no.id;
                trashContagem_ = static_cast<int>(no.itemIds.size());
                return; // Lixeira fica fixa no topo esquerdo do canvas (fora do zoom)
            }
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

    if (!original && trashFolderId_.empty()) {
        trashFolderId_ = projeto_.obterOuCriarPastaTrash(mapaAtivoId_);
        trashContagem_ = static_cast<int>(projeto_.itensNaPastaTrash(mapaAtivoId_).size());
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

    looseFiles_.clear();
    try {
        const auto soltos = projeto_.itensSemPasta(mapaAtivoId_);
        auto& db = projeto_.projeto().registro();
        auto stmt = db.prepare(
            "SELECT i.id, i.titulo, i.tipo_midia, a.caminho_relativo, a.tamanho_bytes "
            "FROM item i "
            "LEFT JOIN arquivo a ON i.id = a.item_id AND a.id = (SELECT a2.id FROM arquivo a2 WHERE a2.item_id = i.id ORDER BY a2.eh_master DESC, a2.id LIMIT 1) "
            "WHERE COALESCE(i.estado, '') != 'duplicata' AND COALESCE(i.em_quarentena, 0) = 0 "
            "ORDER BY i.criado_em"
        );

        int maxNodeY = 40;
        int maxNodeX = 40;
        for (const auto& n : nodes_) {
            maxNodeY = std::max(maxNodeY, n.bounds.getBottom());
            maxNodeX = std::max(maxNodeX, n.bounds.getRight());
        }

        int looseStartX = 40;
        int looseStartY = nodes_.empty() ? 40 : (maxNodeY + 50);
        int looseX = looseStartX;
        int looseY = looseStartY;

        const auto nests = ProjetoAberto::mapaDeNests(db);
        containersSemPosicao_.clear();
        const bool original = mapaAtivoEhOriginal();
        auto stmtPasta = db.prepare("SELECT pasta_id FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ? LIMIT 1");
        std::map<std::string, int> containersPorPasta;  // para empilhar os containers ao lado da pasta deles
        const int largContainer = containersComoPastas_ ? 190 : 126, altContainer = containersComoPastas_ ? 64 : 138;
        while (stmt.step()) {
            const std::string idItem = stmt.columnText(0);
            const bool solto = soltos.count(idItem) > 0;
            int nestTotal = 0;
            const ProjetoAberto::NestInfo* infoContainer = nullptr;
            if (auto nit = nests.find(idItem); nit != nests.end()) {
                if (nit->second.capaId != idItem) continue;
                nestTotal = nit->second.total;
                if (!nit->second.origem.empty()) infoContainer = &nit->second;
            }
            // Todo CONTAINER aparece no mapa (solto ou ligado à pasta onde está); o resto só se estiver solto.
            if (!solto && infoContainer == nullptr) continue;
            std::string pastaPai;
            if (infoContainer != nullptr && !solto && !original) {
                stmtPasta.reset();
                stmtPasta.bind(1, matriz::db::Value::of(idItem));
                stmtPasta.bind(2, matriz::db::Value::of(mapaAtivoId_));
                if (stmtPasta.step()) pastaPai = stmtPasta.columnText(0);
                const bool pastaVisivel = std::any_of(nodes_.begin(), nodes_.end(), [&](const FolderNode& n) { return n.id == pastaPai; });
                if (!pastaVisivel) continue;  // a pasta não está no mapa (ex.: _trash): o container também não
            }
            LooseFileNode lf;
            lf.nestTotal = nestTotal;
            lf.pastaPaiId = pastaPai;
            if (infoContainer != nullptr) {
                lf.ehContainer = true;
                lf.nestId = infoContainer->nestId;
                lf.containerOrigem = infoContainer->origem;
                lf.containerIcone = infoContainer->icone;
                if (!infoContainer->capaImagem.empty())
                    lf.containerCapa = projeto_.projeto().pasta().getChildFile(juce::String(infoContainer->capaImagem)).getFullPathName().toStdString();
            }
            lf.id = idItem;
            lf.titulo = juce::String::fromUTF8(stmt.columnText(1).c_str());
            if (infoContainer != nullptr)  // o container mostra o nome da ficha ou, sem nome, o rótulo do tipo — nunca o de um arquivo
                lf.titulo = !infoContainer->nome.empty() ? juce::String::fromUTF8(infoContainer->nome.c_str()) : i18n::t("intake.container_" + juce::String(infoContainer->origem));
            lf.tipoMidia = stmt.columnText(2);
            lf.extensao = stmt.columnIsNull(3) ? juce::String() : juce::File(juce::String::fromUTF8(stmt.columnText(3).c_str())).getFileExtension().trimCharactersAtStart(".");
            lf.tamanhoBytes = stmt.columnIsNull(4) ? 0 : static_cast<juce::int64>(stmt.columnInt(4));

            try { lf.ehConversaWhatsapp = matriz::whatsapp::itemEhConversa(projeto_.projeto().registroLeitura(), lf.id); } catch (...) {}
            // Órfãos NÃO vão para o canvas por conta própria (milhares de arquivos soltos por erro de operador):
            // ficam guardados no card fixo _NO FOLDER. Só aparece solto o que o usuário posicionou de propósito
            // (posição salva) ou uma conversa de WhatsApp. Container aprovado no Intake sempre aparece (como no Mini).
            if (layoutSalvo_.find(chaveLayout(lf.id)) == layoutSalvo_.end() && !lf.ehConversaWhatsapp && !lf.ehContainer) continue;
            lf.bounds = juce::Rectangle<int>(looseX, looseY, lf.ehContainer ? largContainer : 126, lf.ehContainer ? altContainer : 138);
            looseY += 148;
            if (looseY > looseStartY + 420) {
                looseY = looseStartY;
                looseX += 138;
            }
            auto salvo = layoutSalvo_.find(chaveLayout(lf.id));
            if (salvo != layoutSalvo_.end()) {
                lf.bounds.setPosition(salvo->second);
            } else if (lf.ehContainer && !pastaPai.empty()) {
                // Container numa pasta: nasce à direita dela, ligado por uma linha (a posição só é gravada se o usuário o mover).
                for (const auto& n : nodes_) {
                    if (n.id != pastaPai) continue;
                    const int k = containersPorPasta[pastaPai]++;
                    lf.bounds.setPosition(n.bounds.getRight() + 90, n.bounds.getY() + k * (altContainer + 14));
                    break;
                }
            } else if (lf.ehContainer) {
                containersSemPosicao_.insert(lf.id);  // posicionado por posicionarContainersNovos()
            } else if (lf.ehConversaWhatsapp) {
                const auto canvasTopLeft = areaCanvas().getTopLeft();
                auto posCanvas = screenToCanvas(canvasTopLeft + juce::Point<int>(36, 36));
                lf.bounds.setPosition(posCanvas.toInt());
                layoutSalvo_[chaveLayout(lf.id)] = lf.bounds.getPosition();
                gravarLayoutSalvo();
            }
            looseFiles_.push_back(lf);
        }
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("recalcularNodes looseFiles erro: " + juce::String(e.what()));
    }
    posicionarContainersNovos();
}

// Porta de ENTRADA do container (lado esquerdo): é a única; nada sai dele.
juce::Point<float> ArvoreBackupComponent::portaEntradaDoContainer(const LooseFileNode& lf) const {
    const auto b = lf.bounds.toFloat();
    if (containersComoPastas_) return { b.getX(), b.getCentreY() };
    return { b.getCentreX() - 48.0f + 4.0f, b.getY() + 2.0f + 48.0f };
}

// Container sob o ponto (canvas): dentro da caixa ou perto da porta de entrada.
int ArvoreBackupComponent::containerSobre(juce::Point<float> canvas) const {
    for (size_t i = 0; i < looseFiles_.size(); ++i) {
        const auto& lf = looseFiles_[i];
        if (!lf.ehContainer) continue;
        if (lf.bounds.toFloat().contains(canvas) || portaEntradaDoContainer(lf).getDistanceSquaredFrom(canvas) <= 900.0f) return static_cast<int>(i);
    }
    return -1;
}

// Os containers ligados a uma pasta acompanham a pasta quando ela é arrastada.
void ArvoreBackupComponent::moverContainersDaPasta(const std::string& pastaId, int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    for (auto& lf : looseFiles_)
        if (lf.ehContainer && lf.pastaPaiId == pastaId) lf.bounds.translate(dx, dy);
}

// D com containers selecionados: tira o container da pasta (volta para a raiz do mapa, solto).
void ArvoreBackupComponent::desconectarContainersSelecionados() {
    if (mapaAtivoEhOriginal()) return;
    std::vector<std::string> ids;
    for (const auto& lf : looseFiles_)
        if (lf.selecionado && lf.ehContainer && !lf.pastaPaiId.empty()) ids.push_back(lf.id);
    if (ids.empty()) return;
    for (const auto& lf : looseFiles_)
        if (std::find(ids.begin(), ids.end(), lf.id) != ids.end()) layoutSalvo_[chaveLayout(lf.id)] = lf.bounds.getPosition();
    gravarLayoutSalvo();
    projeto_.removerItensDoBackup(ids, mapaAtivoId_);  // um nest sai inteiro
    recarregar();
}

// Containers sem posição salva (acabaram de ser aprovados no Intake) entram empilhados no canto superior esquerdo
// da vista, no primeiro lugar livre. Espera o enquadramento inicial e o tamanho do canvas para não gravar posição de uma vista provisória.
void ArvoreBackupComponent::posicionarContainersNovos() {
    if (containersSemPosicao_.empty()) return;
    const auto canvas = areaCanvas();
    if (canvas.getWidth() < 50 || canvas.getHeight() < 50) return;  // ainda sem tamanho: resized() tenta de novo
    if (enquadrarPendente_ && !nodes_.empty()) return;              // tentarEnquadrar() chama de volta
    bool mudou = false;
    std::set<std::string> pendentes = containersSemPosicao_;
    for (auto& lf : looseFiles_) {
        if (pendentes.count(lf.id) == 0) continue;
        // Abaixo do card _trash, que fica fixo no canto superior esquerdo da vista.
        const auto trash = boundsPastaTrash();
        const int yTela = std::max(canvas.getY() + 36, trash.isEmpty() ? 0 : trash.getBottom() + 14);
        auto pos = screenToCanvas({canvas.getX() + 36, yTela}).toInt();
        for (int tentativa = 0; tentativa < 400; ++tentativa) {
            const juce::Rectangle<int> r(pos.x, pos.y, lf.bounds.getWidth(), lf.bounds.getHeight());
            bool ocupado = false;
            for (const auto& outro : looseFiles_)
                if (&outro != &lf && !pendentes.count(outro.id) && r.intersects(outro.bounds)) { ocupado = true; break; }
            if (!ocupado) for (const auto& n : nodes_) if (r.intersects(n.bounds)) { ocupado = true; break; }
            if (!ocupado) break;
            pos.y += 148;
        }
        lf.bounds.setPosition(pos);
        layoutSalvo_[chaveLayout(lf.id)] = pos;
        pendentes.erase(lf.id);
        mudou = true;
    }
    containersSemPosicao_.clear();
    if (mudou) { gravarLayoutSalvo(); repaint(); }
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
    if (aplicarFocoPendente()) { posicionarContainersNovos(); return; }
    if (!enquadrarPendente_ || nodes_.empty()) { posicionarContainersNovos(); return; }
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
    posicionarContainersNovos();
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
    auto font = juce::Font(juce::FontOptions(tema().tamanhoFontePequena));
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

    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
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

void ArvoreBackupComponent::confirmarEApagarPastas(const std::vector<std::string>& pastaIds) {
    if (mapaAtivoEhOriginal() || pastaIds.empty()) return;

    std::vector<std::string> validas;
    for (const auto& id : pastaIds) {
        if (id != trashFolderId_ && !projeto_.ehPastaTrash(id))
            validas.push_back(id);
    }
    if (validas.empty()) return;

    std::set<std::string> todasPastas(validas.begin(), validas.end());
    bool expandiu = true;
    while (expandiu) {
        expandiu = false;
        for (const auto& n : nodes_) {
            if (todasPastas.count(n.pastaPaiId) > 0 && todasPastas.count(n.id) == 0) {
                todasPastas.insert(n.id);
                expandiu = true;
            }
        }
    }

    std::set<std::string> todosItens;
    for (const auto& n : nodes_) {
        if (todasPastas.count(n.id) > 0) {
            todosItens.insert(n.itemIdsDiretos.begin(), n.itemIdsDiretos.end());
        }
    }

    std::string nomePrimeira;
    for (const auto& n : nodes_) {
        if (n.id == validas.front()) {
            nomePrimeira = n.nome.toStdString();
            break;
        }
    }

    if (todosItens.empty()) {
        projeto_.iniciarGrupoUndo("Delete Folders");
        for (const auto& id : validas) projeto_.apagarPastaAcervo(id);
        projeto_.finalizarGrupoUndo();
        recarregar();
        return;
    }

    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
    juce::String titulo = i18n::t("arvore_backup.dialogo_deletar_pasta_titulo");
    juce::String msg = i18n::t("arvore_backup.dialogo_deletar_pasta_msg")
                           .replace("{n}", juce::String(todosItens.size()));

    auto* alert = new juce::AlertWindow(titulo, msg, juce::AlertWindow::QuestionIcon);
    alert->addButton(i18n::t("arvore_backup.btn_mover_para_trash"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton(i18n::t("arvore_backup.btn_manter_soltos"), 2);
    alert->addButton(i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alert->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, validas, todosItens, nomePrimeira](int resultado) {
        if (!safeThis || resultado == 0) return;

        std::vector<std::string> itemIds(todosItens.begin(), todosItens.end());

        if (resultado == 1) {
            safeThis->projeto_.moverItensParaTrash(itemIds, safeThis->mapaAtivoId_, nomePrimeira);
        } else if (resultado == 2) {
            safeThis->projeto_.extrairItensParaCanvas(itemIds, safeThis->mapaAtivoId_);
        }

        safeThis->projeto_.iniciarGrupoUndo("Delete Folders");
        for (const auto& id : validas) safeThis->projeto_.apagarPastaAcervo(id);
        safeThis->projeto_.finalizarGrupoUndo();
        safeThis->recarregar();
    }), true);
}

void ArvoreBackupComponent::mostrarMenuArquivoDetalhe(const std::string& itemId) {
    if (itemId.empty()) {
        if (detailContent_ && detailContent_->isTrash) {
            mostrarMenuPastaTrash();
        }
        return;
    }
    juce::PopupMenu menu;
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);

    menu.addItem(i18n::t("light.ui.mb.ctx.showAtSource"), [safeThis, itemId] {
        if (safeThis) safeThis->mostrarNaOrigem(itemId);
    });

    if (detailContent_ && detailContent_->isTrash) {
        menu.addItem(i18n::t("arvore_backup.restaurar_item"), [safeThis, itemId] {
            if (!safeThis) return;
            safeThis->projeto_.restaurarItensDaTrash({itemId}, safeThis->mapaAtivoId_);
            safeThis->recarregar();
        });
        menu.addItem(i18n::t("arvore_backup.extrair_canvas"), [safeThis, itemId] {
            if (!safeThis) return;
            safeThis->projeto_.extrairItensParaCanvas({itemId}, safeThis->mapaAtivoId_);
            safeThis->recarregar();
        });
    } else {
        if (!mapaAtivoEhOriginal()) {
            menu.addItem(i18n::t("arvore_backup.extrair_canvas"), [safeThis, itemId] {
                if (!safeThis) return;
                safeThis->projeto_.extrairItensParaCanvas({itemId}, safeThis->mapaAtivoId_);
                safeThis->recarregar();
            });
            menu.addItem(i18n::t("arvore_backup.mover_para_trash"), [safeThis, itemId] {
                if (!safeThis) return;
                safeThis->projeto_.moverItensParaTrash({itemId}, safeThis->mapaAtivoId_);
                safeThis->recarregar();
            });
        }
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
}

void ArvoreBackupComponent::mostrarMenuPastaTrash() {
    if (mapaAtivoEhOriginal()) return;
    auto itens = projeto_.itensNaPastaTrash(mapaAtivoId_);
    juce::PopupMenu menu;
    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);

    const bool temItens = !itens.empty();

    menu.addItem(i18n::t("arvore_backup.trash_restaurar_tudo"), temItens, false, [safeThis, itens] {
        if (!safeThis || itens.empty()) return;
        safeThis->projeto_.restaurarItensDaTrash(itens, safeThis->mapaAtivoId_);
        safeThis->recarregar();
    });

    menu.addItem(i18n::t("arvore_backup.trash_extrair_tudo"), temItens, false, [safeThis, itens] {
        if (!safeThis || itens.empty()) return;
        safeThis->projeto_.extrairItensParaCanvas(itens, safeThis->mapaAtivoId_);
        safeThis->recarregar();
    });

    menu.addSeparator();

    menu.addItem(i18n::t("arvore_backup.trash_esvaziar"), temItens, false, [safeThis, itens] {
        if (!safeThis || itens.empty()) return;
        safeThis->projeto_.removerItensDoProjeto(itens);
        safeThis->recarregar();
    });

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
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
        if (tema().fundo.getBrightness() > 0.5f)  // claro: conectores cinza #8C857B
            g.setColour(juce::Colour(0xff8c857b).withAlpha(ativo ? 1.0f : 0.55f));  // conectores #8C857B
        else
            g.setColour(ativo ? tema().acento.withAlpha(0.40f) : tema().textoTerciario.withAlpha(0.15f));
        g.strokePath(p, juce::PathStrokeType(ativo && tema().fundo.getBrightness() > 0.5f ? 2.0f : 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
    g.drawText(juce::String(n) + " " + i18n::t("arvore_backup.items"), semPastaBounds_, juce::Justification::centredLeft, true);
}

// LIXEIRA (_trash) — card fixo no canto superior esquerdo do canvas (espaço de TELA, imune ao zoom e pan).
juce::Rectangle<int> ArvoreBackupComponent::boundsPastaTrash() const {
    if (mapaAtivoEhOriginal() || (!raizSobreposicaoId_.empty() && raizSobreposicaoId_ == trashFolderId_))
        return {};
    const auto canvas = areaCanvas();
    int x = canvas.getX() + 16;
    int y = canvas.getY() + 16;
    return juce::Rectangle<int>(x, y, 160, 56);  // _trash no topo; o painel de containers vem logo abaixo
}

void ArvoreBackupComponent::desenharPastaTrash(juce::Graphics& g) const {
    auto bounds = boundsPastaTrash();
    if (bounds.isEmpty()) return;

    const auto& tk = tema();
    auto bf = bounds.toFloat();

    g.setColour(tk.painel.withAlpha(0.95f));
    g.fillRoundedRectangle(bf, tk.raioPequeno);

    const bool selecionada = (selectedFolderId_ == trashFolderId_ && !trashFolderId_.empty());
    if (trashHover_ || selecionada) {
        g.setColour(tk.perigo);
        g.drawRoundedRectangle(bf, tk.raioPequeno, selecionada ? 2.0f : 1.5f);
    } else {
        g.setColour(tk.borda);
        g.drawRoundedRectangle(bf, tk.raioPequeno, 1.0f);
    }

    auto header = bf.removeFromTop(22.0f);
    g.setColour(tk.painelAlt);
    g.fillRoundedRectangle(header, tk.raioPequeno);
    g.fillRect(header.withTrimmedTop(14.0f));

    g.setColour(tk.perigo);
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    g.drawText(juce::String::fromUTF8("🗑 _trash"), header.reduced(8.0f, 0.0f), juce::Justification::centredLeft, true);

    g.setColour(tk.textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
    g.drawText(i18n::t("arvore_backup.trash_sub"), header.reduced(8.0f, 0.0f), juce::Justification::centredRight, true);

    auto content = bf.reduced(8.0f, 4.0f);
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    juce::String contStr = juce::String(trashContagem_) + " " + i18n::t("arvore_backup.items");
    g.drawText(contStr, content.removeFromTop(16.0f), juce::Justification::centredLeft, true);
}

namespace {
juce::Colour corDoTipoDeArquivo(const Tema& tk, const juce::String& tipoMidia, const juce::String& ext, juce::String& tag) {
    if (ext == "ZIP" || ext == "RAR" || ext == "7Z" || ext == "TAR" || ext == "GZ") { tag = "ZIP"; return tk.tipoZip; }
    if (tipoMidia == "audio" || ext == "WAV" || ext == "MP3" || ext == "FLAC" || ext == "AAC" || ext == "AIFF") { tag = "AUDIO"; return tk.tipoAudio; }
    if (tipoMidia == "video" || ext == "MOV" || ext == "MP4" || ext == "MKV" || ext == "AVI") { tag = "MOVIE"; return tk.tipoVideo; }
    if (tipoMidia == "foto" || ext == "JPG" || ext == "JPEG" || ext == "PNG" || ext == "TIFF" || ext == "CR2" || ext == "RAW") { tag = "IMG"; return tk.tipoImagem; }
    if (ext == "PDF" || ext == "DOC" || ext == "DOCX" || ext == "XLS" || ext == "XLSX") { tag = "DOC"; return tk.tipoDocumento; }
    if (ext == "TXT" || ext == "MD" || ext == "RTF" || ext == "JSON" || ext == "CSV") { tag = "TEXT"; return tk.tipoSessao; }
    tag = "FILE";
    return tk.tipoOutros;
}
} // namespace

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
    bool sobreTrash = boundsPastaTrash().contains(details.localPosition);
    if (sobreTrash != trashHover_) {
        trashHover_ = sobreTrash;
        repaint();
    }
}

void ArvoreBackupComponent::itemDragExit(const SourceDetails&) {
    if (semPastaHover_) {
        semPastaHover_ = false;
        repaint();
    }
    if (trashHover_) {
        trashHover_ = false;
        repaint();
    }
}

void ArvoreBackupComponent::itemDropped(const SourceDetails& details) {
    bool sobre = semPastaHover_;
    bool sobreTrash = trashHover_;
    semPastaHover_ = false;
    trashHover_ = false;
    repaint();
    if (mapaAtivoEhOriginal()) return;

    auto* arr = details.description.getArray();
    if (!arr) return;
    std::vector<std::string> itemIds;
    for (auto& v : *arr) itemIds.push_back(v.toString().toStdString());
    if (itemIds.empty()) return;

    if (sobreTrash) {
        projeto_.moverItensParaTrash(itemIds, mapaAtivoId_);
        recarregar();
        return;
    }

    if (!sobre) return;

    projeto_.removerItensDoBackup(itemIds, mapaAtivoId_);
    recarregar();
}

bool ArvoreBackupComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    return !files.isEmpty();
}

void ArvoreBackupComponent::fileDragEnter(const juce::StringArray&, int, int) {
    repaint();
}

void ArvoreBackupComponent::fileDragMove(const juce::StringArray&, int x, int y) {
    int alvo = -1;
    if (!mapaAtivoEhOriginal()) alvo = pastaAlvoDoDrop(screenToCanvas({ x, y }), {});
    if (alvo != nodeHoverDrop_) { nodeHoverDrop_ = alvo; repaint(); }
    bool sobreTrash = !mapaAtivoEhOriginal() && boundsPastaTrash().contains(x, y);
    if (sobreTrash != trashHover_) { trashHover_ = sobreTrash; repaint(); }
}

void ArvoreBackupComponent::fileDragExit(const juce::StringArray&) {
    if (nodeHoverDrop_ != -1) { nodeHoverDrop_ = -1; repaint(); }
    if (trashHover_) { trashHover_ = false; repaint(); }
}

void ArvoreBackupComponent::filesDropped(const juce::StringArray& files, int x, int y) {
    const bool sobreTrash = !mapaAtivoEhOriginal() && boundsPastaTrash().contains(x, y);
    int alvo = -1;
    if (!mapaAtivoEhOriginal() && !sobreTrash) alvo = pastaAlvoDoDrop(screenToCanvas({ x, y }), {});
    fileDragExit({});
    juce::Array<juce::File> arr;
    for (const auto& f : files) arr.add(juce::File(f));
    if (arr.isEmpty()) return;
    if (sobreTrash) {
        if (trashFolderId_.empty()) trashFolderId_ = projeto_.obterOuCriarPastaTrash(mapaAtivoId_);
        if (aoIngerirArquivos) aoIngerirArquivos(arr, trashFolderId_);
        return;
    }
    if (alvo >= 0 && alvo < static_cast<int>(nodes_.size())) {
        if (aoIngerirArquivos) aoIngerirArquivos(arr, nodes_[static_cast<size_t>(alvo)].id);
        return;
    }
    if (aoIngerirArquivos) {
        aoIngerirArquivos(arr, {});
    }
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
    // Claro: fundo pontilhado bege (poço #EDE8E0); escuro: como antes.
    juce::Colour bg = isLight ? tk.painelAlt : (tk.fundo.brighter(0.30f)).brighter(0.30f);
    g.fillAll(bg);

    g.saveState();
    g.reduceClipRegion(canvas);

    // Canvas background grid pattern (n8n style dots) — in screen space
    // Zoom afastado: os pontos ficam mais espaçados (nunca milhares); todos numa chamada só.
    g.setColour(isLight ? tk.borda : tema().painelAlt.withAlpha(0.4f));
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

    if (nodes_.empty() && looseFiles_.empty()) {
        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
        g.drawText(i18n::t("arvore_backup.vazio"), canvas, juce::Justification::centred, true);
        g.restoreState();
        coluna_.paint(g);
        desenharSemPasta(g);
        desenharPastaTrash(g);
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

    // Containers ligados a uma pasta: a linha sai da porta de saída da pasta e entra pela porta de entrada do container.
    for (const auto& lf : looseFiles_) {
        if (!lf.ehContainer || lf.pastaPaiId.empty() || !outputPorts.count(lf.pastaPaiId)) continue;
        const auto p1 = outputPorts[lf.pastaPaiId], p2 = portaEntradaDoContainer(lf);
        if (!juce::Rectangle<float>(p1, p2).expanded(40.0f).intersects(visivel)) continue;
        desenharLinhaConexaoN8n(g, p1, p2, true, false);
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

        const float kRaioNo = isLight ? 12.0f : 8.0f;  // claro: card creme canto 12
        g.setColour(node.ativo ? tema().painel : tema().painelAlt);
        g.fillRoundedRectangle(b, kRaioNo);

        // FOLDER COLOR (item 12): camada translúcida de identificação, nunca
        // preenchimento sólido — baixa opacidade, por cima do fundo do card
        // e por BAIXO do cabeçalho/texto (desenhados depois), pra não afetar
        // cálculo nenhum do Treemap nem tapar leitura de nome/contagem.
        if (node.hasCorCustomizada) {
            g.setColour(node.corCustomizada.withAlpha(0.22f));
            g.fillRoundedRectangle(b, kRaioNo);
        }

        if (node.selecionado && isLight) {
            // Claro: seleção = borda grafite 2 px (sem zebra).
            g.setColour(tema().acento);
            g.drawRoundedRectangle(b.reduced(1.0f), kRaioNo, 2.0f);
        } else if (node.selecionado && arrastando) {
            // Durante o arrasto/pan: traço sólido simples (o tracejado custa caro a cada frame).
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(b, kRaioNo, 2.5f);
        } else if (node.selecionado) {
            // Zebra (marching-ants) selection border: solid white base stroke
            // with a dashed black stroke on top, instead of tinting the fill.
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(b, kRaioNo, 2.5f);

            juce::Path outline;
            outline.addRoundedRectangle(b, kRaioNo);
            juce::PathStrokeType stroke(2.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
            float dashLengths[] = { 4.0f, 4.0f };
            stroke.createDashedStroke(outline, outline, dashLengths, 2);
            g.setColour(juce::Colours::black);
            g.strokePath(outline, stroke);
        } else {
            g.setColour(node.ativo ? tema().borda : tema().textoTerciario);
            g.drawRoundedRectangle(b, kRaioNo, 1.2f);
        }

        // A fonte acompanha o tamanho do folder (slider, escala por pasta, nível): 190x84 = 100%.
        const float fs = juce::jlimit(0.4f, 4.0f, std::min(b.getWidth() / 190.0f, b.getHeight() / 84.0f));
        // Texto que sairia com menos de 5 px na tela: nem desenha (só retângulo, borda e portas).
        if (12.0f * fs * zoom_ >= 5.0f) {
            auto header = b.removeFromTop(24.0f * fs);
            g.setColour(node.ativo ? (isLight ? tema().painelAlt : tema().painelAlt) : tema().fundo);
            g.fillRoundedRectangle(header, kRaioNo);
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
        if (nodeHoverDrop_ >= 0 && nodeHoverDrop_ < static_cast<int>(nodes_.size()) &&
            nodes_[static_cast<size_t>(nodeHoverDrop_)].id == node.id) {
            g.setColour(tema().acento);
            g.drawRoundedRectangle(node.bounds.toFloat().expanded(3.0f), 10.0f, 2.5f);
        }
    }

    // Arquivos soltos (na raiz do mapa): ícone de arquivo com a extensão dentro e o nome embaixo — sem card.
    for (const auto& lf : looseFiles_) {
        const auto b = lf.bounds.toFloat();
        juce::String ext = lf.extensao.toUpperCase().trimCharactersAtStart(".");
        juce::String tag;
        const auto cor = corDoTipoDeArquivo(tk, lf.tipoMidia, ext, tag);
        if (lf.selecionado) {
            g.setColour(tk.acento.withAlpha(0.22f));
            g.fillRoundedRectangle(b, 14.0f);
            g.setColour(tk.acento);
            g.drawRoundedRectangle(b.reduced(1.0f), 14.0f, 2.0f);
        }
        const float lw = 56.0f, lh = 70.0f;
        if (lf.ehContainer && containersComoPastas_) {
            // SHOW CONTAINERS AS FOLDERS: o container como uma pasta comum, mas só com porta de ENTRADA (à esquerda).
            constexpr float kRaioNo = 8.0f;
            g.setColour(tk.painel);
            g.fillRoundedRectangle(b, kRaioNo);
            g.setColour(lf.selecionado ? tk.acento : tk.borda);
            g.drawRoundedRectangle(b.reduced(0.5f), kRaioNo, lf.selecionado ? 2.0f : 1.0f);
            auto cab = b.withHeight(22.0f);
            g.setColour(tk.painelAlt);
            g.fillRoundedRectangle(cab, kRaioNo);
            g.fillRect(cab.withTrimmedTop(14.0f));
            g.setColour(tk.acento);
            g.setFont(fonteUi(tk.tamanhoFonteMicro, juce::Font::bold));
            g.drawText(i18n::t("intake.container_" + juce::String(lf.containerOrigem)).toUpperCase(), cab.reduced(10.0f, 0.0f), juce::Justification::centredLeft, true);
            g.setColour(tk.textoPrimario);
            g.setFont(fonteUi(12.0f, juce::Font::bold));
            g.drawText(lf.titulo, b.withTrimmedTop(24.0f).withHeight(20.0f).reduced(10.0f, 0.0f), juce::Justification::centredLeft, true);
            g.setColour(tk.textoSecundario);
            g.setFont(fonteUi(10.0f));
            g.drawText(juce::String(lf.nestTotal) + " " + i18n::t("arvore_backup.items"), b.withTrimmedTop(44.0f).reduced(10.0f, 0.0f), juce::Justification::centredLeft, true);
            const auto entrada = portaEntradaDoContainer(lf);
            g.setColour(tk.acento);
            g.fillEllipse(entrada.x - 6.0f, entrada.y - 6.0f, 12.0f, 12.0f);
            g.setColour(tk.textoPrimario);
            g.drawEllipse(entrada.x - 6.0f, entrada.y - 6.0f, 12.0f, 12.0f, 1.2f);
            continue;
        }
        if (lf.ehContainer) {
            // Container: ícone/capa do tipo + contagem + porta de ENTRADA à esquerda. Sem porta de saída: nada conecta nele.
            const juce::Rectangle<float> area(b.getCentreX() - 48.0f, b.getY() + 2.0f, 96.0f, 96.0f);
            matriz::ui::desenharContainerGrafico(g, area, juce::String(lf.containerOrigem), juce::String::fromUTF8(lf.containerIcone.c_str()),
                                                 juce::String::fromUTF8(lf.containerCapa.c_str()));
            if (lf.nestTotal > 0) {
                const juce::String n(lf.nestTotal);
                const float w = std::max(22.0f, 10.0f + 7.0f * static_cast<float>(n.length()));
                const juce::Rectangle<float> pilula(area.getRight() - w, area.getBottom() - 16.0f, w, 16.0f);
                g.setColour(juce::Colours::black.withAlpha(0.72f));
                g.fillRoundedRectangle(pilula, 8.0f);
                g.setColour(juce::Colours::white);
                g.setFont(fonteUi(tk.tamanhoFonteMicro, juce::Font::bold));
                g.drawText(n, pilula.toNearestInt(), juce::Justification::centred, false);
            }
            const auto entrada = portaEntradaDoContainer(lf);
            g.setColour(tk.acento);
            g.fillEllipse(entrada.x - 6.0f, entrada.y - 6.0f, 12.0f, 12.0f);
            g.setColour(tk.textoPrimario);
            g.drawEllipse(entrada.x - 6.0f, entrada.y - 6.0f, 12.0f, 12.0f, 1.2f);
            if (matriz::ui::ehOrigemProject(juce::String(lf.containerOrigem))) {  // tag PROJECT no canto superior esquerdo
                const juce::String rotulo = i18n::t("projeto.tag");
                const juce::Rectangle<float> tag(b.getX() + 14.0f, b.getY() + 2.0f, 52.0f, 15.0f);
                g.setColour(juce::Colour(0xffb8860b).withAlpha(0.95f));
                g.fillRoundedRectangle(tag, 7.5f);
                g.setColour(juce::Colours::white);
                g.setFont(fonteUi(tk.tamanhoFonteMicro, juce::Font::bold));
                g.drawText(rotulo, tag.toNearestInt(), juce::Justification::centred, false);
            }
            g.setFont(fonteUi(11.5f, juce::Font::bold));
            g.drawFittedText(lf.titulo, b.withTrimmedTop(100.0f).reduced(4.0f, 0.0f).toNearestInt(), juce::Justification::centredTop, 2, 0.85f);
            continue;
        }
        if (lf.ehConversaWhatsapp) {
            const auto bolha = matriz::ui::logoConversaWhatsapp(256);
            if (bolha.isValid()) {
                g.setOpacity(1.0f);
                g.drawImage(bolha, juce::Rectangle<float>(b.getCentreX() - 36.0f, b.getY() + 6.0f, 72.0f, 72.0f), juce::RectanglePlacement::centred);
                g.setColour(tk.textoPrimario);
                g.setFont(fonteUi(11.5f, juce::Font::bold));
                const auto areaTexto = b.withTrimmedTop(82.0f).reduced(4.0f, 2.0f);
                g.drawFittedText(lf.titulo, areaTexto.toNearestInt(), juce::Justification::centredTop, 2, 0.85f);
                continue;
            }
        }
        const juce::Rectangle<float> folha(b.getCentreX() - lw * 0.5f, b.getY() + 8.0f, lw, lh);
        const float dobra = 16.0f;
        juce::Path p;
        p.startNewSubPath(folha.getX(), folha.getY() + 6.0f);
        p.quadraticTo(folha.getX(), folha.getY(), folha.getX() + 6.0f, folha.getY());
        p.lineTo(folha.getRight() - dobra, folha.getY());
        p.lineTo(folha.getRight(), folha.getY() + dobra);
        p.lineTo(folha.getRight(), folha.getBottom() - 6.0f);
        p.quadraticTo(folha.getRight(), folha.getBottom(), folha.getRight() - 6.0f, folha.getBottom());
        p.lineTo(folha.getX() + 6.0f, folha.getBottom());
        p.quadraticTo(folha.getX(), folha.getBottom(), folha.getX(), folha.getBottom() - 6.0f);
        p.closeSubPath();
        g.setColour(tk.sombra);
        g.fillPath(p, juce::AffineTransform::translation(0.0f, 3.0f));
        g.setColour(tk.cartao);
        g.fillPath(p);
        g.setColour(cor.withAlpha(0.20f));
        g.fillPath(p);
        g.setColour(cor);
        g.strokePath(p, juce::PathStrokeType(2.0f));
        juce::Path dobraP;
        dobraP.startNewSubPath(folha.getRight() - dobra, folha.getY());
        dobraP.lineTo(folha.getRight() - dobra, folha.getY() + dobra);
        dobraP.lineTo(folha.getRight(), folha.getY() + dobra);
        g.strokePath(dobraP, juce::PathStrokeType(1.6f));
        g.setColour(cor);
        g.setFont(fonteUi(ext.length() > 4 ? 11.0f : 14.0f, juce::Font::bold));
        g.drawText(ext.isNotEmpty() ? ext : tag, folha.withTrimmedTop(dobra).toNearestInt(), juce::Justification::centred, false);
        if (lf.nestTotal > 1) {
            const juce::Rectangle<float> selo(folha.getRight() - 12.0f, folha.getBottom() - 16.0f, 24.0f, 18.0f);
            g.setColour(tk.perigo);
            g.fillRoundedRectangle(selo, 5.0f);
            g.setColour(tk.textoSobreAcento);
            g.setFont(fonteUi(tk.tamanhoFonteMicro, juce::Font::bold));
            g.drawText("N" + juce::String(lf.nestTotal), selo.toNearestInt(), juce::Justification::centred, false);
        }
        g.setColour(tk.textoPrimario);
        g.setFont(fonteUi(tk.tamanhoFontePequena + 1.0f, juce::Font::bold));
        g.drawFittedText(lf.titulo, b.withTrimmedTop(lh + 12.0f).reduced(4.0f, 0.0f).toNearestInt(), juce::Justification::centredTop, 2, 0.9f);
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
    desenharPastaTrash(g);
    desenharIndicadorZoom(g);
    desenharBarraDeAbas(g);
}

void ArvoreBackupComponent::desenharIndicadorZoom(juce::Graphics& g) const {
    // (também desenha a moldura da lista da pasta selecionada, que fica logo abaixo dos cards)
    if (!detalheBounds_.isEmpty() && !selectedFolderId_.empty()) {
        g.setColour(tema().borda.withAlpha(0.7f));
        g.drawRoundedRectangle(detalheBounds_.toFloat().reduced(0.5f), tema().raioPequeno, 1.0f);
    }
    g.setColour(tema().textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
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
    corpo.removeFromTop(4);
    if (chkContainersComoPastas_) chkContainersComoPastas_->setBounds(corpo.removeFromTop(22));
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
    if (detailViewport_ && detailViewport_->isVisible() && !selectedFolderId_.empty()) {
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
    } else if (detailViewport_) {
        detailViewport_->setBounds({});
    }

    const auto canvas = areaCanvas();
    if (conversaWhatsapp_ && conversaWhatsapp_->isVisible()) {
        conversaWhatsapp_->setBounds(canvas);
        if (btnVoltarMapa_) {
            btnVoltarMapa_->setBounds(canvas.getX() + 16, canvas.getY() + 16, 140, 32);
            btnVoltarMapa_->toFront(true);
        }
    }

    if (btnReceberCelular_) {
        constexpr int kAltura = 46;
        constexpr int kMargem = 16;
        const int larguraBtn = canvas.getWidth() > 600 ? 250 : kAltura;
        if (canvas.getWidth() > 150 && canvas.getHeight() > 100 && (!conversaWhatsapp_ || !conversaWhatsapp_->isVisible())) {
            btnReceberCelular_->setVisible(true);
            btnReceberCelular_->setBounds(canvas.getRight() - larguraBtn - kMargem,
                                          canvas.getBottom() - kAltura - kMargem,
                                          larguraBtn, kAltura);
            btnReceberCelular_->toFront(true);
        } else {
            btnReceberCelular_->setVisible(false);
        }
    }

    if (overlayAtual_ && overlayAtual_->isVisible()) {
        overlayAtual_->setBounds(getLocalBounds());
        overlayAtual_->toFront(true);
    }
}

void ArvoreBackupComponent::limparDicaPasta() {
    dicaPasta_.stopTimer();
    dicaPastaId_.clear();
    ProgressoGlobal::obterInstancia().definirDica({}, {});
}

void ArvoreBackupComponent::mouseMove(const juce::MouseEvent& e) {
    const bool sobreTrash = !mapaAtivoEhOriginal() && boundsPastaTrash().contains(e.getPosition());
    if (sobreTrash != trashHover_) {
        trashHover_ = sobreTrash;
        repaint();
    }
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
    if (trashHover_) {
        trashHover_ = false;
        repaint();
    }
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

    // LIXEIRA (_trash) — card fixo no canto superior esquerdo do canvas.
    if (!mapaAtivoEhOriginal() && boundsPastaTrash().contains(e.getPosition())) {
        if (trashFolderId_.empty()) trashFolderId_ = projeto_.obterOuCriarPastaTrash(mapaAtivoId_);
        if (e.mods.isPopupMenu()) {
            mostrarMenuPastaTrash();
        } else {
            limparSelecaoTudo();
            selectedFolderId_ = trashFolderId_;
            atualizarPainelDetalhe(trashFolderId_);
            if (janelaTrash_) {
                janelaTrash_->toFront(true);
            } else {
                janelaTrash_ = std::make_unique<TrashWindow>(projeto_, mapaAtivoId_, [this] {
                    recarregar();
                    repaint();
                });
            }
            repaint();
        }
        return;
    }

    // Clique no fundo da coluna esquerda (fora dos controles): nada a fazer.
    if (!areaCanvas().contains(e.getPosition())) return;

    auto canvasClick = screenToCanvas(e.getPosition());

    // Arquivos soltos: clique, Cmd/Ctrl+clique (soma/tira), Shift+clique (intervalo) e botão direito (menu).
    looseFileDragIndice_ = -1;
    for (size_t i = 0; i < looseFiles_.size(); ++i) {
        if (!looseFiles_[i].bounds.toFloat().contains(canvasClick)) continue;
        const std::string idClicado = looseFiles_[i].id;
        const bool incremental = e.mods.isCommandDown() || e.mods.isCtrlDown();
        if (e.mods.isPopupMenu()) {
            if (!looseFiles_[i].selecionado) {
                limparSelecaoTudo();
                looseFiles_[i].selecionado = true;
            }
            ancoraTipo_ = 1;
            ancoraId_ = idClicado;
            repaint();
            mostrarMenuDeArquivosSoltos();
            return;
        }
        if (incremental) {
            looseFiles_[i].selecionado = !looseFiles_[i].selecionado;
            ancoraTipo_ = 1;
            ancoraId_ = idClicado;
            repaint();
            return;
        }
        if (e.mods.isShiftDown() && ancoraTipo_ == 1 && !ancoraId_.empty() && ancoraId_ != idClicado) {
            selecionarIntervalo(1, ancoraId_, idClicado);
            repaint();
            return;
        }
        looseFileDragIndice_ = static_cast<int>(i);
        arrastoOffset_ = { static_cast<int>(canvasClick.x - looseFiles_[i].bounds.getX()),
                           static_cast<int>(canvasClick.y - looseFiles_[i].bounds.getY()) };
        soltosArrastoIniciais_.clear();
        size_t selecionados = 0;
        for (const auto& lf : looseFiles_) if (lf.selecionado) ++selecionados;
        if (looseFiles_[i].selecionado && selecionados > 1) {
            for (size_t k = 0; k < looseFiles_.size(); ++k)
                if (looseFiles_[k].selecionado) soltosArrastoIniciais_.push_back({ k, looseFiles_[k].bounds.getPosition() });
            ancoraTipo_ = 1;
            ancoraId_ = idClicado;
            repaint();
            return;
        }
        limparSelecaoTudo();
        looseFiles_[i].selecionado = true;
        ancoraTipo_ = 1;
        ancoraId_ = idClicado;
        repaint();
        return;
    }

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
                // O Folder Map sobreposto da pasta (antes aberto pelo duplo clique, que agora abre a grade).
                menu.addItem(i18n::t("arvore_backup.abrir_como_foldermap"), [this, pId, nomeNo = hitNodeRef.nome] {
                    abrirSobreposicao(pId, nomeNo);
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
                confirmarEApagarPastas(selectedIds);
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
            for (auto& lf : looseFiles_) lf.selecionado = false;
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

    if (looseFileDragIndice_ >= 0 && looseFileDragIndice_ < static_cast<int>(looseFiles_.size())) {
        auto canvasPos = screenToCanvas(e.getPosition());
        const auto posAntiga = looseFiles_[static_cast<size_t>(looseFileDragIndice_)].bounds.getPosition();
        looseFiles_[static_cast<size_t>(looseFileDragIndice_)].bounds.setPosition(
            static_cast<int>(canvasPos.x - arrastoOffset_.x),
            static_cast<int>(canvasPos.y - arrastoOffset_.y)
        );
        if (soltosArrastoIniciais_.size() > 1) {
            const auto delta = looseFiles_[static_cast<size_t>(looseFileDragIndice_)].bounds.getPosition() - posAntiga;
            for (const auto& [k, ini] : soltosArrastoIniciais_)
                if (static_cast<int>(k) != looseFileDragIndice_ && k < looseFiles_.size())
                    looseFiles_[k].bounds.translate(delta.x, delta.y);
        }

        const int nodeSobre = mapaAtivoEhOriginal()
                                  ? -1
                                  : pastaAlvoDoDrop(canvasPos, looseFiles_[static_cast<size_t>(looseFileDragIndice_)].bounds);
        if (nodeSobre != nodeHoverDrop_) {
            nodeHoverDrop_ = nodeSobre;
        }
        const bool sobreTrash = !mapaAtivoEhOriginal() && boundsPastaTrash().contains(e.getPosition());
        if (sobreTrash != trashHover_) {
            trashHover_ = sobreTrash;
        }
        repaint();
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
            const auto antes = nodes_[static_cast<size_t>(idx)].bounds.getPosition();
            nodes_[static_cast<size_t>(idx)].bounds.setPosition(static_cast<int>(novaPos.x), static_cast<int>(novaPos.y));
            const auto depois = nodes_[static_cast<size_t>(idx)].bounds.getPosition();
            moverContainersDaPasta(nodes_[static_cast<size_t>(idx)].id, depois.x - antes.x, depois.y - antes.y);
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

    if (looseFileDragIndice_ >= 0 && looseFileDragIndice_ < static_cast<int>(looseFiles_.size())) {
        if (!mapaAtivoEhOriginal() && boundsPastaTrash().contains(e.getPosition())) {
            trashHover_ = false;
            std::vector<std::string> ids;
            if (soltosArrastoIniciais_.size() > 1) {
                for (const auto& [k, ini] : soltosArrastoIniciais_)
                    if (k < looseFiles_.size()) ids.push_back(looseFiles_[k].id);
            } else {
                ids.push_back(looseFiles_[static_cast<size_t>(looseFileDragIndice_)].id);
            }
            projeto_.moverItensParaTrash(ids, mapaAtivoId_);
            for (const auto& id : ids) layoutSalvo_.erase(chaveLayout(id));
            gravarLayoutSalvo();
            soltosArrastoIniciais_.clear();
            looseFileDragIndice_ = -1;
            nodeHoverDrop_ = -1;
            recarregar();
            return;
        }
        concluirArrastoDeArquivoSolto(screenToCanvas(e.getPosition()));
        return;
    }

    if (marqueeSelecting_) {
        marqueeSelecting_ = false;
        int countSelecionados = 0;
        std::string unicoSelId;
        for (auto& n : nodes_) {
            n.selecionado = marqueeRectCanvas_.intersects(n.bounds.toFloat());
            if (n.selecionado) { ++countSelecionados; unicoSelId = n.id; }
        }
        for (auto& lf : looseFiles_) {
            lf.selecionado = marqueeRectCanvas_.intersects(lf.bounds.toFloat());
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
        } else if (const int c = containerSobre(canvasRelease); c >= 0 && !mapaAtivoEhOriginal()) {
            // A saída de uma pasta solta num CONTAINER: ele (e os arquivos dele) passa a morar nessa pasta.
            const auto pai = socketDragParentId_;
            projeto_.adicionarItensAPasta({ looseFiles_[static_cast<size_t>(c)].id }, pai);
            socketDragParentId_.clear();
            recarregar();
            return;
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
                    if (idx >= 0 && idx < static_cast<int>(nodes_.size())) {
                        const auto atual = nodes_[static_cast<size_t>(idx)].bounds.getPosition();
                        nodes_[static_cast<size_t>(idx)].bounds.setPosition(static_cast<int>(posInicial.x), static_cast<int>(posInicial.y));
                        moverContainersDaPasta(nodes_[static_cast<size_t>(idx)].id, static_cast<int>(posInicial.x) - atual.x, static_cast<int>(posInicial.y) - atual.y);
                    }
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
        // Os containers ligados às pastas movidas foram junto: a posição nova deles também fica guardada.
        bool gravou = false;
        for (int idx : indices) {
            if (idx < 0 || idx >= static_cast<int>(nodes_.size())) continue;
            for (const auto& lf : looseFiles_)
                if (lf.ehContainer && lf.pastaPaiId == nodes_[static_cast<size_t>(idx)].id) { layoutSalvo_[chaveLayout(lf.id)] = lf.bounds.getPosition(); gravou = true; }
        }
        if (gravou) gravarLayoutSalvo();
    }
    nodeDragIndice_ = -1;
    arrastoGrupoPosicoesIniciais_.clear();
    repaint(areaCanvas());  // fim do arrasto/pan: a borda da seleção volta ao tracejado
}

void ArvoreBackupComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!mapaAtivoEhOriginal() && boundsPastaTrash().contains(e.getPosition())) {
        if (trashFolderId_.empty()) trashFolderId_ = projeto_.obterOuCriarPastaTrash(mapaAtivoId_);
        limparSelecaoTudo();
        selectedFolderId_ = trashFolderId_;
        atualizarPainelDetalhe(trashFolderId_, true);
        repaint();
        return;
    }
    if (!areaCanvas().contains(e.getPosition())) return;
    auto canvasClick = screenToCanvas(e.getPosition());
    for (const auto& lf : looseFiles_) {
        if (lf.bounds.toFloat().contains(canvasClick)) {
            sequenciaPreview_.clear();
            if (lf.ehConversaWhatsapp) {
                abrirConversaWhatsapp(lf.id);
            } else if (lf.ehContainer) {
                if (aoAbrirContainerJanela) aoAbrirContainerJanela(lf.nestId, lf.titulo);
            }
            return;
        }
    }
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
        if (nodes_[static_cast<size_t>(i)].bounds.toFloat().contains(canvasClick)) {
            // Mapa principal: abre a pasta num Folder Map sobreposto. Dentro da sobreposição: renomeia, como antes.
            if (sobreposto_) iniciarEdicaoInline(i);
            else if (aoAbrirPastaJanela) aoAbrirPastaJanela(nodes_[static_cast<size_t>(i)].id, nodes_[static_cast<size_t>(i)].nome);
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
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (inlineEditor_ && inlineEditor_->hasKeyboardFocus(true)) return false;
        if (mapaAtivoEhOriginal()) return false;
        std::vector<std::string> selectedPastaIds;
        for (const auto& n : nodes_) if (n.selecionado) selectedPastaIds.push_back(n.id);
        std::vector<std::string> selectedLooseIds;
        for (const auto& lf : looseFiles_) if (lf.selecionado) selectedLooseIds.push_back(lf.id);

        if (!selectedPastaIds.empty()) {
            confirmarEApagarPastas(selectedPastaIds);
            return true;
        } else if (!selectedLooseIds.empty()) {
            projeto_.moverItensParaTrash(selectedLooseIds, mapaAtivoId_);
            for (const auto& id : selectedLooseIds) layoutSalvo_.erase(chaveLayout(id));
            gravarLayoutSalvo();
            recarregar();
            return true;
        }
    }
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
        desconectarContainersSelecionados();
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

    if (!trashFolderId_.empty() && folderId == trashFolderId_) {
        detailContent_->isTrash = true;
        detailContent_->folderName = "_trash";
        detailContent_->subfolders.clear();
        detailContent_->files.clear();
        auto trashItens = projeto_.itensNaPastaTrash(mapaAtivoId_);
        if (!trashItens.empty()) {
            std::set<std::string> setItens(trashItens.begin(), trashItens.end());
            auto detalhes = projeto_.obterDetalhesItens(setItens);
            for (const auto& d : detalhes) {
                TreeDetailContent::FileEntry fe;
                fe.id = d.id;
                fe.nome = d.nome;
                fe.extensao = d.extensao;
                fe.tamanhoBytes = d.tamanhoBytes;
                detailContent_->files.push_back(fe);
            }
        }
        detailContent_->aoMostrarNaGrade = [this, trashItens] {
            if (aoMostrarConteudoNaGrade) {
                std::set<std::string> s(trashItens.begin(), trashItens.end());
                aoMostrarConteudoNaGrade(s);
            }
        };
        detailViewport_->setVisible(true);
        resized();
        detailContent_->recalcularAltura();
        detailContent_->repaint();
        return;
    }

    const FolderNode* selected = nullptr;
    for (const auto& n : nodes_)
        if (n.id == folderId) { selected = &n; break; }
    if (!selected) { detailViewport_->setVisible(false); return; }

    detailContent_->isTrash = false;
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
    if (nodes_.empty() && looseFiles_.empty()) return;
    const auto canvas = areaCanvas();
    if (canvas.getWidth() < 50 || canvas.getHeight() < 50) return;
    juce::Rectangle<int> uniao;
    bool primeiro = true;
    for (const auto& n : nodes_) {
        uniao = primeiro ? n.bounds : uniao.getUnion(n.bounds);
        primeiro = false;
    }
    for (const auto& lf : looseFiles_) {
        uniao = primeiro ? lf.bounds : uniao.getUnion(lf.bounds);
        primeiro = false;
    }
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

void ArvoreBackupComponent::abrirReceberDoCelular() {
    if (overlayAtual_ != nullptr) return;
    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<ArvoreBackupComponent> seguro(this);
    mostrarEscolhaImportacaoCelular(*overlayAtual_, [seguro](std::optional<ModoImportacaoCelular> modo) {
        juce::MessageManager::callAsync([seguro, modo] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->escolhaImportacaoAtual_.reset();
            seguro->resized();
            if (modo.has_value()) seguro->iniciarReceberDoCelular(*modo);
        });
    }, &escolhaImportacaoAtual_);
    resized();
}

void ArvoreBackupComponent::iniciarReceberDoCelular(ModoImportacaoCelular modo) {
    if (overlayAtual_ != nullptr) return;
    if (modo == ModoImportacaoCelular::WhatsApp) {
        overlayAtual_ = std::make_unique<PainelOverlay>();
        addAndMakeVisible(*overlayAtual_);
        juce::Component::SafePointer<ArvoreBackupComponent> seguro(this);
        mostrarImportarWhatsapp(*overlayAtual_, [seguro] {
            juce::MessageManager::callAsync([seguro] {
                if (seguro == nullptr) return;
                seguro->overlayAtual_.reset();
                seguro->resized();
            });
        });
        resized();
        return;
    }

    matriz::celular::OpcoesServidor opcoes;
    auto* pProj = &projeto_.projeto();
    opcoes.existentesDoProjeto = [pProj]() -> matriz::celular::ConjuntoExistentes {
        if (pProj == nullptr) return {};
        return matriz::celular::existentesDoProjeto(*pProj);
    };

    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<ArvoreBackupComponent> seguro(this);
    painelCelularAtual_ = mostrarReceberDoCelular(*overlayAtual_, opcoes, [seguro](RespostaReceberCelular r) {
        juce::MessageManager::callAsync([seguro, r] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            if (r.concluiu && r.recebidos > 0 && r.pastaSessao.isDirectory()) {
                if (seguro->aoIngerirArquivos) {
                    seguro->aoIngerirArquivos({r.pastaSessao}, seguro->selectedFolderId_);
                }
            }
        });
    }, modo);
    resized();
}

void ArvoreBackupComponent::abrirConversaWhatsapp(const std::string& itemId) {
    if (itemId.empty()) return;
    conversaWhatsapp_ = std::make_unique<ConversaWhatsappComponent>(projeto_, itemId);
    btnVoltarMapa_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("◀ ") + i18n::t("arvore_backup.voltar_mapa"));
    btnVoltarMapa_->setColour(juce::TextButton::buttonColourId, tema().cartao);
    btnVoltarMapa_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnVoltarMapa_->onClick = [this] { fecharConversaWhatsapp(); };
    addAndMakeVisible(*conversaWhatsapp_);
    addAndMakeVisible(*btnVoltarMapa_);
    if (btnReceberCelular_) btnReceberCelular_->setVisible(false);
    resized();
}

void ArvoreBackupComponent::fecharConversaWhatsapp() {
    conversaWhatsapp_.reset();
    btnVoltarMapa_.reset();
    if (btnReceberCelular_) btnReceberCelular_->setVisible(true);
    resized();
    repaint();
}

void ArvoreBackupComponent::fecharOverlayImportacao() {
    if (overlayAtual_ != nullptr) {
        overlayAtual_.reset();
        painelCelularAtual_.reset();
        escolhaImportacaoAtual_.reset();
        resized();
        repaint();
    }
}

// ── cards_layout.json: posição dos nós de arquivo solto ──────────────
juce::File ArvoreBackupComponent::arquivoLayout() const {
    return projeto_.projeto().pasta().getChildFile("cards_layout.json");
}

void ArvoreBackupComponent::carregarLayoutSalvo() {
    layoutSalvo_.clear();
    containersComoPastas_ = false;
    if (chkContainersComoPastas_) chkContainersComoPastas_->setToggleState(false, juce::dontSendNotification);
    const auto arq = arquivoLayout();
    if (!arq.existsAsFile()) return;
    const auto json = juce::JSON::parse(arq);
    containersComoPastas_ = static_cast<bool>(json.getProperty("containersComoPastas", false));
    if (chkContainersComoPastas_) chkContainersComoPastas_->setToggleState(containersComoPastas_, juce::dontSendNotification);
    if (auto* obj = json.getProperty("layouts", {}).getDynamicObject()) {
        for (const auto& prop : obj->getProperties()) {
            const auto* arr = prop.value.getArray();
            if (arr != nullptr && arr->size() == 2)
                layoutSalvo_[prop.name.toString().toStdString()] = { static_cast<int>((*arr)[0]), static_cast<int>((*arr)[1]) };
        }
    }
}

void ArvoreBackupComponent::gravarLayoutSalvo() {
    juce::DynamicObject::Ptr layouts = new juce::DynamicObject();
    for (const auto& [chave, ponto] : layoutSalvo_)
        layouts->setProperty(juce::Identifier(chave.c_str()), juce::Array<juce::var>{ ponto.x, ponto.y });
    juce::DynamicObject::Ptr raiz = new juce::DynamicObject();
    raiz->setProperty("layouts", juce::var(layouts.get()));
    raiz->setProperty("containersComoPastas", containersComoPastas_);
    if (!arquivoLayout().replaceWithText(juce::JSON::toString(juce::var(raiz.get()), true)))
        juce::Logger::writeToLog("Folder Map: não foi possível gravar " + arquivoLayout().getFullPathName());
}

int ArvoreBackupComponent::pastaAlvoDoDrop(juce::Point<float> cursorCanvas, juce::Rectangle<int> iconeCanvas) const {
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].bounds.toFloat().contains(cursorCanvas)) return static_cast<int>(i);
    int melhor = -1;
    long long melhorArea = 0;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const auto inter = nodes_[i].bounds.getIntersection(iconeCanvas);
        const long long area = static_cast<long long>(inter.getWidth()) * inter.getHeight();
        if (area > melhorArea) { melhorArea = area; melhor = static_cast<int>(i); }
    }
    return melhor;
}

void ArvoreBackupComponent::concluirArrastoDeArquivoSolto(juce::Point<float> canvasRelease) {
    if (looseFileDragIndice_ < 0 || looseFileDragIndice_ >= static_cast<int>(looseFiles_.size())) return;
    const int targetNodeIndex =
        pastaAlvoDoDrop(canvasRelease, looseFiles_[static_cast<size_t>(looseFileDragIndice_)].bounds);
    if (targetNodeIndex >= 0 && mapaAtivoEhOriginal()) {
        recarregar();
    } else if (targetNodeIndex >= 0 && targetNodeIndex < static_cast<int>(nodes_.size())) {
        std::vector<std::string> ids;
        if (soltosArrastoIniciais_.size() > 1) {
            for (const auto& [k, ini] : soltosArrastoIniciais_) if (k < looseFiles_.size()) ids.push_back(looseFiles_[k].id);
        } else {
            ids.push_back(looseFiles_[static_cast<size_t>(looseFileDragIndice_)].id);
        }
        std::string targetPastaId = nodes_[static_cast<size_t>(targetNodeIndex)].id;
        projeto_.adicionarItensAPasta(ids, targetPastaId);
        bool mudouLayout = false;
        for (const auto& id : ids) mudouLayout |= layoutSalvo_.erase(chaveLayout(id)) > 0;
        if (mudouLayout) gravarLayoutSalvo();
        recarregar();
    } else {
        if (soltosArrastoIniciais_.size() > 1) {
            for (const auto& [k, ini] : soltosArrastoIniciais_)
                if (k < looseFiles_.size()) layoutSalvo_[chaveLayout(looseFiles_[k].id)] = looseFiles_[k].bounds.getPosition();
        } else {
            const auto& lf = looseFiles_[static_cast<size_t>(looseFileDragIndice_)];
            layoutSalvo_[chaveLayout(lf.id)] = lf.bounds.getPosition();
        }
        gravarLayoutSalvo();
    }
    soltosArrastoIniciais_.clear();
    looseFileDragIndice_ = -1;
    nodeHoverDrop_ = -1;
    repaint();
}

void ArvoreBackupComponent::limparSelecaoTudo() {
    for (auto& n : nodes_) n.selecionado = false;
    for (auto& lf : looseFiles_) lf.selecionado = false;
}

namespace {
template <typename Lista>
std::vector<size_t> ordemPorPosicao(const Lista& v) {
    std::vector<size_t> idx(v.size());
    for (size_t i = 0; i < v.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return v[a].bounds.getY() < v[b].bounds.getY(); });
    std::vector<size_t> out;
    size_t i = 0;
    while (i < idx.size()) {
        const int y0 = v[idx[i]].bounds.getY();
        const int tolerancia = juce::jmax(8, v[idx[i]].bounds.getHeight() / 2);
        std::vector<size_t> linha;
        while (i < idx.size() && std::abs(v[idx[i]].bounds.getY() - y0) <= tolerancia) linha.push_back(idx[i++]);
        std::sort(linha.begin(), linha.end(), [&](size_t a, size_t b) { return v[a].bounds.getX() < v[b].bounds.getX(); });
        out.insert(out.end(), linha.begin(), linha.end());
    }
    return out;
}
} // namespace

std::vector<size_t> ArvoreBackupComponent::ordemDeLeituraSoltos() const { return ordemPorPosicao(looseFiles_); }
std::vector<size_t> ArvoreBackupComponent::ordemDeLeituraPastas() const { return ordemPorPosicao(nodes_); }

void ArvoreBackupComponent::selecionarIntervalo(int tipo, const std::string& deId, const std::string& ateId) {
    const auto ordem = tipo == 1 ? ordemDeLeituraSoltos() : ordemDeLeituraPastas();
    int a = -1, b = -1;
    for (size_t k = 0; k < ordem.size(); ++k) {
        const auto& id = tipo == 1 ? looseFiles_[ordem[k]].id : nodes_[ordem[k]].id;
        if (id == deId) a = static_cast<int>(k);
        if (id == ateId) b = static_cast<int>(k);
    }
    if (a < 0 || b < 0) return;
    limparSelecaoTudo();
    for (int k = std::min(a, b); k <= std::max(a, b); ++k) {
        if (tipo == 1) looseFiles_[ordem[static_cast<size_t>(k)]].selecionado = true;
        else nodes_[ordem[static_cast<size_t>(k)]].selecionado = true;
    }
}

std::vector<std::string> ArvoreBackupComponent::idsSoltosSelecionados() const {
    std::vector<std::string> out;
    for (size_t i : ordemDeLeituraSoltos())
        if (looseFiles_[i].selecionado) out.push_back(looseFiles_[i].id);
    return out;
}

void ArvoreBackupComponent::mostrarNaOrigem(const std::string& itemId) {
    juce::File origem;
    try {
        auto st = projeto_.projeto().registro().prepare("SELECT caminho_absoluto_origem FROM arquivo WHERE item_id = ? ORDER BY eh_master DESC LIMIT 1");
        st.bind(1, matriz::db::Value::of(itemId));
        if (st.step() && !st.columnIsNull(0)) origem = juce::File(juce::String::fromUTF8(st.columnText(0).c_str()));
    } catch (...) {}
    if (origem != juce::File() && origem.exists()) {
        origem.revealToUser();
    }
}

bool ArvoreBackupComponent::aplicarMovimentoDeSoltos(const std::vector<std::string>& ids, const std::string& pastaId) {
    if (mapaAtivoEhOriginal()) return false;
    projeto_.adicionarItensAPasta(ids, pastaId);
    bool mudou = false;
    for (const auto& id : ids) mudou |= layoutSalvo_.erase(chaveLayout(id)) > 0;
    if (mudou) gravarLayoutSalvo();
    recarregar();
    return true;
}

void ArvoreBackupComponent::moverSoltosParaPasta(const std::vector<std::string>& ids, const std::string& pastaId) {
    aplicarMovimentoDeSoltos(ids, pastaId);
}

void ArvoreBackupComponent::adicionarSubmenuDePastas(juce::PopupMenu& menu, const std::string& paiId, const std::vector<std::string>& ids) {
    juce::Component::SafePointer<ArvoreBackupComponent> seguro(this);
    for (const auto& n : nodes_) {
        if (n.pastaPaiId != paiId) continue;
        const std::string pastaId = n.id;
        bool temFilhas = false;
        for (const auto& m : nodes_) if (m.pastaPaiId == pastaId) { temFilhas = true; break; }
        if (!temFilhas) {
            menu.addItem(n.nome, true, false, [seguro, ids, pastaId] { if (seguro) seguro->moverSoltosParaPasta(ids, pastaId); });
        } else {
            juce::PopupMenu sub;
            sub.addItem(n.nome, true, false, [seguro, ids, pastaId] { if (seguro) seguro->moverSoltosParaPasta(ids, pastaId); });
            sub.addSeparator();
            adicionarSubmenuDePastas(sub, pastaId, ids);
            menu.addSubMenu(n.nome, sub);
        }
    }
}

void ArvoreBackupComponent::removerSoltosDoBackup(const std::vector<std::string>& ids) {
    if (ids.empty()) return;
    projeto_.removerItensDoProjeto(ids);
    for (const auto& id : ids) layoutSalvo_.erase(chaveLayout(id));
    gravarLayoutSalvo();
    recarregar();
}

// Botão direito num container solto > Editar questionário inicial.
void ArvoreBackupComponent::editarQuestionarioDoContainerSolto(const std::string& nestId, const std::string& origem) {
    if (overlayAtual_ != nullptr || nestId.empty()) return;
    overlayAtual_ = std::make_unique<PainelOverlay>();
    addAndMakeVisible(*overlayAtual_);
    juce::Component::SafePointer<ArvoreBackupComponent> seguro(this);
    editarQuestionarioDoContainer(*overlayAtual_, projeto_, nestId, origem, [seguro](bool aplicou) {
        juce::MessageManager::callAsync([seguro, aplicou] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
            if (aplicou) seguro->recarregar();
        });
    });
    resized();
}

void ArvoreBackupComponent::mostrarMenuDeArquivosSoltos() {
    const auto ids = idsSoltosSelecionados();
    if (ids.empty()) return;
    juce::PopupMenu menu;
    juce::Component::SafePointer<ArvoreBackupComponent> seguro(this);
    const bool unico = ids.size() == 1;
    if (unico) {
        for (const auto& lf : looseFiles_) {
            if (lf.id != ids.front() || !lf.ehContainer) continue;
            const std::string nestId = lf.nestId, origem = lf.containerOrigem;
            if (matriz::ui::ehOrigemProject(juce::String(origem)))
                menu.addItem(i18n::t("projeto.abrir"), true, false, [seguro, nestId, origem] { if (seguro && seguro->aoAbrirProject) seguro->aoAbrirProject(nestId, origem); });
            if (matriz::ui::ehOrigemProject(juce::String(origem)))
                menu.addItem(i18n::t("imp.editar_questionario"), true, false, [seguro, nestId, origem] { if (seguro) seguro->editarQuestionarioDoContainerSolto(nestId, origem); });
            menu.addSeparator();
            break;
        }
    }
    menu.addItem(i18n::t("light.ui.mb.ctx.showAtSource"), unico, false, [seguro, ids] { if (seguro) seguro->mostrarNaOrigem(ids.front()); });
    juce::PopupMenu mover;
    adicionarSubmenuDePastas(mover, "", ids);
    menu.addSubMenu(i18n::t("light.ui.mb.ctx.moveToFolder"), mover, mover.getNumItems() > 0 && !mapaAtivoEhOriginal());
    menu.addSeparator();
    menu.addItem("NEST", ids.size() > 1, false, [seguro, ids] {
        if (!seguro) return;
        seguro->projeto_.criarNest(ids);
        seguro->recarregar();
    });
    bool algumEmNest = false;
    for (const auto& id : ids) if (projeto_.nestDoItem(id)) { algumEmNest = true; break; }
    if (algumEmNest)
        menu.addItem("UNEST", true, false, [seguro, ids] {
            if (!seguro) return;
            seguro->projeto_.desfazerNest(ids);
            seguro->recarregar();
        });
    {   // Group as Project Container / Change Container Type / Ungroup Container
        matriz::ui::HospedeOverlay hospede;
        hospede.abrir = [seguro]() -> PainelOverlay* {
            if (seguro == nullptr || seguro->overlayAtual_ != nullptr) return nullptr;
            seguro->overlayAtual_ = std::make_unique<PainelOverlay>();
            seguro->addAndMakeVisible(*seguro->overlayAtual_);
            seguro->resized();
            return seguro->overlayAtual_.get();
        };
        hospede.fechar = [seguro] {
            if (seguro == nullptr) return;
            seguro->overlayAtual_.reset();
            seguro->resized();
        };
        hospede.aoMudarDados = [seguro] { if (seguro != nullptr) seguro->recarregar(); };
        juce::PopupMenu grupo;
        matriz::ui::adicionarMenuProjectContainer(grupo, projeto_, ids, hospede);
        if (grupo.getNumItems() > 0) {
            menu.addSeparator();
            for (juce::PopupMenu::MenuItemIterator it(grupo); it.next();) menu.addItem(it.getItem());
        }
    }
    menu.addSeparator();
    menu.addItem(i18n::t("arvore_backup.mover_para_trash"), !mapaAtivoEhOriginal(), false, [seguro, ids] {
        if (!seguro) return;
        seguro->projeto_.moverItensParaTrash(ids, seguro->mapaAtivoId_);
        for (const auto& id : ids) seguro->layoutSalvo_.erase(seguro->chaveLayout(id));
        seguro->gravarLayoutSalvo();
        seguro->recarregar();
    });
    menu.addItem(i18n::t("light.ui.mb.ctx.remove"), true, false, [seguro, ids] { if (seguro) seguro->removerSoltosDoBackup(ids); });
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
}

std::optional<std::string> ArvoreBackupComponent::soltoAdjacente(const std::string& idAtual, int direcao) const {
    std::vector<std::pair<juce::String, std::string>> ordem;
    const bool soSelecao = std::find(sequenciaPreview_.begin(), sequenciaPreview_.end(), idAtual) != sequenciaPreview_.end();
    for (const auto& lf : looseFiles_)
        if (!soSelecao || std::find(sequenciaPreview_.begin(), sequenciaPreview_.end(), lf.id) != sequenciaPreview_.end())
            ordem.push_back({ lf.titulo.toLowerCase(), lf.id });
    std::sort(ordem.begin(), ordem.end());
    const auto it = std::find_if(ordem.begin(), ordem.end(), [&](const auto& p) { return p.second == idAtual; });
    if (it == ordem.end() || ordem.size() < 2) return std::nullopt;
    const int n = static_cast<int>(ordem.size());
    const int i = static_cast<int>(it - ordem.begin());
    return ordem[static_cast<size_t>(((i + (direcao >= 0 ? 1 : -1)) % n + n) % n)].second;
}

void ArvoreBackupComponent::selecionarSolto(const std::string& id) {
    limparSelecaoTudo();
    for (auto& lf : looseFiles_) lf.selecionado = (lf.id == id);
    ancoraTipo_ = 1;
    ancoraId_ = id;
    repaint();
}

void ArvoreBackupComponent::arranjarSoltosSelecionados() {
    std::vector<size_t> sel;
    for (size_t i : ordemDeLeituraSoltos()) if (looseFiles_[i].selecionado) sel.push_back(i);
    if (sel.empty()) return;
    juce::Rectangle<int> uniao = looseFiles_[sel.front()].bounds;
    for (size_t i : sel) uniao = uniao.getUnion(looseFiles_[i].bounds);
    constexpr int cw = 128, ch = 136;
    const int colunas = juce::jmax(1, uniao.getWidth() / cw);
    const auto posicoes = [&](juce::Point<int> origem) {
        std::vector<juce::Point<int>> out;
        for (size_t k = 0; k < sel.size(); ++k)
            out.push_back({origem.x + static_cast<int>(k % static_cast<size_t>(colunas)) * cw, origem.y + static_cast<int>(k / static_cast<size_t>(colunas)) * ch});
        return out;
    };
    const auto colide = [&](const std::vector<juce::Point<int>>& pos) {
        for (size_t k = 0; k < sel.size(); ++k) {
            const auto r = looseFiles_[sel[k]].bounds.withPosition(pos[k]);
            for (const auto& n : nodes_) if (r.intersects(n.bounds)) return true;
            for (size_t j = 0; j < looseFiles_.size(); ++j)
                if (!looseFiles_[j].selecionado && r.intersects(looseFiles_[j].bounds)) return true;
        }
        return false;
    };
    auto pos = posicoes(uniao.getTopLeft());
    if (colide(pos)) {
        int baixo = uniao.getBottom();
        for (const auto& n : nodes_) baixo = std::max(baixo, n.bounds.getBottom());
        for (const auto& lf : looseFiles_) baixo = std::max(baixo, lf.bounds.getBottom());
        pos = posicoes({uniao.getX(), baixo + 40});
    }
    for (size_t k = 0; k < sel.size(); ++k) {
        looseFiles_[sel[k]].bounds.setPosition(pos[k]);
        layoutSalvo_[chaveLayout(looseFiles_[sel[k]].id)] = pos[k];
    }
    gravarLayoutSalvo();
    repaint();
}

void ArvoreBackupComponent::reunirArquivosSoltos() {
    recarregar();
    if (looseFiles_.empty()) return;
    int baixo = 0;
    for (const auto& n : nodes_) baixo = std::max(baixo, n.bounds.getBottom());
    const int origemX = 40, origemY = nodes_.empty() ? 40 : baixo + 80;
    constexpr int celulaW = 128, celulaH = 140;
    const int n = static_cast<int>(looseFiles_.size());
    const int colunas = juce::jlimit(1, 12, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n) * 1.5))));
    limparSelecaoTudo();
    for (int i = 0; i < n; ++i) {
        auto& lf = looseFiles_[static_cast<size_t>(i)];
        lf.bounds = juce::Rectangle<int>(origemX + (i % colunas) * celulaW, origemY + (i / colunas) * celulaH, 112, 124);
        lf.selecionado = true;
        layoutSalvo_[chaveLayout(lf.id)] = lf.bounds.getPosition();
    }
    gravarLayoutSalvo();
    ancoraTipo_ = 1;
    ancoraId_ = looseFiles_.front().id;

    juce::Rectangle<int> uniao = looseFiles_.front().bounds;
    for (const auto& lf : looseFiles_) uniao = uniao.getUnion(lf.bounds);
    const auto canvas = areaCanvas();
    if (canvas.getWidth() >= 50 && canvas.getHeight() >= 50) {
        constexpr float kMargem = 60.0f;
        const float zx = (static_cast<float>(canvas.getWidth()) - 2.0f * kMargem) / static_cast<float>(std::max(1, uniao.getWidth()));
        const float zy = (static_cast<float>(canvas.getHeight()) - 2.0f * kMargem) / static_cast<float>(std::max(1, uniao.getHeight()));
        zoom_ = juce::jlimit(0.15f, 1.0f, std::min(zx, zy));
        const auto centro = uniao.getCentre().toFloat();
        panOffset_ = {static_cast<float>(canvas.getWidth()) * 0.5f - centro.x * zoom_,
                      static_cast<float>(canvas.getHeight()) * 0.5f - centro.y * zoom_};
        enquadrarPendente_ = false;
    }
    repaint();
}

} // namespace matriz::ui
