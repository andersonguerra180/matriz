#include "ArvoreBackupComponent.h"
#include "../Ingest/FluxoLote.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Vault/Resolucao.h"
#include "ModalMitigacao.h"
#include "Tokens.h"

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

    juce::String folderName;
    std::vector<SubfolderEntry> subfolders;
    std::vector<FileEntry> files;
    std::function<void(const std::string&)> aoClicarArquivo;
    int hoverFileIndex_ = -1;

    void recalcularAltura() {
        int h = 52;
        if (!subfolders.empty()) h += 28 + static_cast<int>(subfolders.size()) * 24;
        if (!files.empty()) h += 28 + static_cast<int>(files.size()) * 24;
        h += 16;
        setSize(getWidth(), std::max(h, getParentHeight()));
    }

    int fileIndexAtY(int mouseY) const {
        int y = 8 + 24 + 4;
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

        g.setColour(tema().borda);
        g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
        area.removeFromTop(4);

        if (!subfolders.empty()) {
            g.setColour(tema().textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText("SUBFOLDERS", area.removeFromTop(20), juce::Justification::centredLeft);

            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            for (const auto& sf : subfolders) {
                auto row = area.removeFromTop(24);
                g.setColour(tema().textoPrimario);
                g.drawText(juce::String(juce::CharPointer_UTF8("\xf0\x9f\x93\x81 ")) + sf.nome,
                           row.removeFromLeft(row.getWidth() - 50),
                           juce::Justification::centredLeft, true);
                g.setColour(tema().textoTerciario);
                g.drawText(juce::String(sf.contagemItens) + " items",
                           row, juce::Justification::centredRight);
            }
            area.removeFromTop(4);
        }

        if (!files.empty()) {
            g.setColour(tema().textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText("FILES", area.removeFromTop(20), juce::Justification::centredLeft);

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
            g.drawText("Empty folder", area, juce::Justification::centredLeft);
        }
    }
};

namespace {

void pedirTextoBackup(const juce::String& titulo, const juce::String& mensagem, const juce::String& valorInicial,
                      std::function<void(std::optional<juce::String>)> aoConcluir) {
    juce::MessageManager::callAsync([titulo, mensagem, valorInicial, aoConcluir]() {
        auto janela = std::make_shared<juce::AlertWindow>(titulo, mensagem, juce::MessageBoxIconType::NoIcon);
        janela->addTextEditor("valor", valorInicial);
        janela->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        janela->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        if (auto* editor = janela->getTextEditor("valor")) {
            editor->grabKeyboardFocus();
            editor->setHighlightedRegion(juce::Range<int>(0, valorInicial.length()));
        }
        janela->enterModalState(true, juce::ModalCallbackFunction::create([janela, aoConcluir](int resultado) mutable {
            retirarPeerDaTela(*janela);
            if (resultado == 1) aoConcluir(janela->getTextEditorContents("valor"));
            else aoConcluir(std::nullopt);
            janela.reset();
        }));
    });
}

} // namespace

ArvoreBackupComponent::ArvoreBackupComponent(ProjetoAberto& projeto)
    : projeto_(projeto) {

    btnCriarPasta_ = std::make_unique<juce::TextButton>("+ NEW FOLDER");
    btnCriarPasta_->onClick = [this] {
        juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
        pedirTextoBackup("CREATE FOLDER", "Enter new folder name:", "New Folder",
            [safeThis](std::optional<juce::String> nome) {
                if (!safeThis || !nome || nome->trim().isEmpty()) return;
                safeThis->criarNovaPasta(nome->trim().toStdString(), std::nullopt);
            });
    };
    addAndMakeVisible(*btnCriarPasta_);

    btnRenomearPasta_ = std::make_unique<juce::TextButton>("RENAME");
    btnRenomearPasta_->onClick = [this] {
        for (const auto& n : nodes_) {
            if (n.selecionado) {
                std::string pId = n.id;
                juce::String nomeAtual = n.nome;
                juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
                pedirTextoBackup("RENAME FOLDER", "Enter new folder name:", nomeAtual,
                    [safeThis, pId](std::optional<juce::String> nome) {
                        if (!safeThis || !nome || nome->trim().isEmpty()) return;
                        safeThis->renomearPastaSelecionada(pId, nome->trim().toStdString());
                    });
                break;
            }
        }
    };
    addAndMakeVisible(*btnRenomearPasta_);

    btnApagarPasta_ = std::make_unique<juce::TextButton>("DELETE");
    btnApagarPasta_->onClick = [this] {
        for (const auto& n : nodes_) {
            if (n.selecionado) {
                apagarPastaSelecionada(n.id);
                break;
            }
        }
    };
    addAndMakeVisible(*btnApagarPasta_);

    btnImportarEstrutura_ = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x86\xbb IMPORT / REFRESH FOLDER STRUCTURE"));
    btnImportarEstrutura_->onClick = [this] {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("IMPORT / REFRESH FOLDER STRUCTURE")
                .withMessage("This will DELETE all current folders and connections, "
                             "then recreate the folder tree from the original ingested file paths.\n\n"
                             "Any custom folder arrangement will be lost.\n\n"
                             "Continue?")
                .withButton("Import")
                .withButton("Cancel"),
            [this](int res) {
                if (res == 1) {
                    try {
                        projeto_.resetarEImportarEstruturaOrigem();
                    } catch (const std::exception& e) {
                        juce::AlertWindow::showAsync(
                            juce::MessageBoxOptions()
                                .withIconType(juce::MessageBoxIconType::WarningIcon)
                                .withTitle("Error")
                                .withMessage(juce::String("Failed to import structure: ") + e.what())
                                .withButton("OK"),
                            nullptr);
                        return;
                    }
                    recarregar();
                }
            });
    };
    addAndMakeVisible(*btnImportarEstrutura_);

    btnAutoArranjar_ = std::make_unique<juce::TextButton>("AUTO ARRANGE");
    btnAutoArranjar_->onClick = [this] { autoArranjar(); };
    addAndMakeVisible(*btnAutoArranjar_);

    btnZoomIn_ = std::make_unique<juce::TextButton>("+");
    btnZoomIn_->onClick = [this] { aplicarZoom(zoom_ * 1.2f, {getWidth() / 2.0f, getHeight() / 2.0f}); };
    addAndMakeVisible(*btnZoomIn_);

    btnZoomOut_ = std::make_unique<juce::TextButton>("-");
    btnZoomOut_->onClick = [this] { aplicarZoom(zoom_ / 1.2f, {getWidth() / 2.0f, getHeight() / 2.0f}); };
    addAndMakeVisible(*btnZoomOut_);

    btnZoomFit_ = std::make_unique<juce::TextButton>("FIT");
    btnZoomFit_->onClick = [this] { zoom_ = 1.0f; panOffset_ = {0.0f, 0.0f}; repaint(); };
    addAndMakeVisible(*btnZoomFit_);

    detailContent_ = std::make_unique<TreeDetailContent>();
    detailViewport_ = std::make_unique<juce::Viewport>();
    detailViewport_->setViewedComponent(detailContent_.get(), false);
    detailViewport_->setScrollBarsShown(true, false);
    detailViewport_->setVisible(false);
    addAndMakeVisible(*detailViewport_);

    setWantsKeyboardFocus(true);
    recarregar();
}

ArvoreBackupComponent::~ArvoreBackupComponent() {
    detailViewport_->setViewedComponent(nullptr, false);
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

                int defaultX = 40 + nivel * 240;
                int defaultY = 80 + index * 110;
                int x = (no.posicaoX != 0 || no.posicaoY != 0) ? no.posicaoX : defaultX;
                int y = (no.posicaoX != 0 || no.posicaoY != 0) ? no.posicaoY : defaultY;

                node.bounds = juce::Rectangle<int>(x, y, 190, 84);
                nodes_.push_back(node);
            }

            for (size_t i = 0; i < no.filhos.size(); ++i) {
                adicionarNo(no.filhos[i], nivel + 1, static_cast<int>(i));
            }
        };

    for (size_t i = 0; i < arvore.filhos.size(); ++i) {
        adicionarNo(arvore.filhos[i], 0, static_cast<int>(i));
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
    projeto_.criarPastaAcervo(nome, pastaPaiId);
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
            projeto_.atualizarPosicaoPastaAcervo(n.id, it->second.x, it->second.y);
        }
    }

    panOffset_ = {0.0f, 0.0f};
    repaint();
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
    g.fillAll(tema().fundo);

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
    g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    g.drawText("Project TREE Workspace", getLocalBounds().reduced(20, 16).removeFromTop(24), juce::Justification::centredLeft);

    // Zoom indicator
    g.setColour(tema().textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(juce::String(static_cast<int>(zoom_ * 100)) + "%", getLocalBounds().reduced(20, 16).removeFromTop(24), juce::Justification::centredRight);

    if (nodes_.empty()) {
        g.setColour(tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText("TREE is empty. Click '+ NEW FOLDER' or ingest a folder structure to populate the graph.",
                   getLocalBounds(), juce::Justification::centred, true);
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

        g.setColour(node.selecionado ? tema().acento.withAlpha(0.2f) : (node.ativo ? tema().painel : tema().painelAlt));
        g.fillRoundedRectangle(b, 8.0f);

        g.setColour(node.selecionado ? tema().acento : (node.ativo ? tema().borda : tema().textoTerciario));
        g.drawRoundedRectangle(b, 8.0f, node.selecionado ? 2.0f : 1.2f);

        auto header = b.removeFromTop(24);
        g.setColour(node.ativo ? tema().painelAlt : tema().fundo);
        g.fillRoundedRectangle(header, 8.0f);
        g.fillRect(header.withTrimmedTop(16));

        g.setColour(node.ativo ? tema().acento : tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(node.ativo ? "FOLDER NODE [ACTIVE]" : "FOLDER NODE [DISABLED]", header.reduced(10, 0), juce::Justification::centredLeft);

        auto content = b.reduced(10, 6);
        g.setColour(node.ativo ? tema().textoPrimario : tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText(node.nome, content.removeFromTop(18), juce::Justification::centredLeft, true);

        g.setColour(tema().textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        juce::String desc = juce::String(node.contagemItens) + " items";
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
    }

    g.restoreState();

    // Minimap (drawn in screen space)
    desenharMinimap(g);
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

    if (detailViewport_ && detailViewport_->isVisible()) {
        auto panelArea = getLocalBounds().withTrimmedTop(44).removeFromRight(kDetailPanelWidth);
        detailViewport_->setBounds(panelArea);
        detailContent_->setSize(panelArea.getWidth() - detailViewport_->getScrollBarThickness(), detailContent_->getHeight());
    }
}

void ArvoreBackupComponent::mouseDown(const juce::MouseEvent& e) {
    finalizarEdicaoInline();
    nodeDragIndice_ = -1;
    socketDragParentId_.clear();

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
    bool hitNode = false;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].bounds.toFloat().contains(canvasClick)) {
            hitNode = true;
            nodeDragIndice_ = static_cast<int>(i);
            arrastoOffset_ = e.getPosition() - canvasToScreen(nodes_[i].bounds.getPosition().toFloat());
            nodes_[i].selecionado = true;

            if (e.mods.isPopupMenu()) {
                juce::PopupMenu menu;
                std::string pId = nodes_[i].id;
                bool temPai = !nodes_[i].pastaPaiId.empty();
                int idx = static_cast<int>(i);

                menu.addItem("+ New Subfolder", [this, pId] {
                    juce::Component::SafePointer<ArvoreBackupComponent> safeThis(this);
                    pedirTextoBackup("NEW SUBFOLDER", "Enter subfolder name:", "New Subfolder",
                        [safeThis, pId](std::optional<juce::String> nome) {
                            if (!safeThis || !nome || nome->trim().isEmpty()) return;
                            safeThis->criarNovaPasta(nome->trim().toStdString(), pId);
                        });
                });
                menu.addSeparator();

                if (temPai) {
                    menu.addItem("Disconnect from Parent", [this, pId] {
                        conectarPastas(pId, std::nullopt);
                    });
                }

                menu.addItem("Rename Folder...", [this, pId, idx] {
                    iniciarEdicaoInline(idx);
                });
                menu.addItem("Toggle ACTIVE / DISABLED", [this, pId] {
                    alternarAtivoPasta(pId);
                });
                menu.addItem("Delete Folder", [this, pId] {
                    apagarPastaSelecionada(pId);
                });
                menu.showMenuAsync(juce::PopupMenu::Options());
            }
        } else {
            nodes_[i].selecionado = false;
        }
    }

    if (!hitNode) {
        panning_ = true;
        panStart_ = e.getPosition();
        atualizarPainelDetalhe("");
    } else {
        std::string selId;
        for (const auto& n : nodes_)
            if (n.selecionado) { selId = n.id; break; }
        atualizarPainelDetalhe(selId);
    }

    repaint();
}

void ArvoreBackupComponent::mouseDrag(const juce::MouseEvent& e) {
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
        nodes_[static_cast<size_t>(nodeDragIndice_)].bounds.setPosition(static_cast<int>(canvasPos.x), static_cast<int>(canvasPos.y));
        repaint();
    }
}

void ArvoreBackupComponent::mouseUp(const juce::MouseEvent& e) {
    minimapDragging_ = false;
    panning_ = false;

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
        const auto& node = nodes_[static_cast<size_t>(nodeDragIndice_)];
        projeto_.atualizarPosicaoPastaAcervo(node.id, node.bounds.getX(), node.bounds.getY());
    }
    nodeDragIndice_ = -1;
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

void ArvoreBackupComponent::atualizarPainelDetalhe(const std::string& folderId) {
    if (folderId.empty() || folderId == selectedFolderId_) {
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
            fe.nome = d.nome;
            fe.extensao = d.extensao;
            fe.tamanhoBytes = d.tamanhoBytes;
            detailContent_->files.push_back(fe);
        }
    }

    detailViewport_->setVisible(true);
    resized();
    detailContent_->recalcularAltura();
    detailContent_->repaint();
}

} // namespace matriz::ui
