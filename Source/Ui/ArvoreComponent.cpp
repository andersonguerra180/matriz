#include "ArvoreComponent.h"
#include "MosaicoComponent.h"
#include "../Diag/Watchdog.h"
#include "../Ingest/LeituraTecnica.h"

#include "../I18n/Strings.h"
#include "Tokens.h"
#include "ModalMitigacao.h"

namespace matriz::ui {

namespace {

void pedirTexto(const juce::String& titulo, const juce::String& mensagem, const juce::String& valorInicial,
                 std::function<void(std::optional<juce::String>)> aoConcluir) {
    juce::MessageManager::callAsync([titulo, mensagem, valorInicial, aoConcluir]() {
        auto janela = std::make_shared<juce::AlertWindow>(titulo, mensagem, juce::MessageBoxIconType::NoIcon);
        janela->addTextEditor("valor", valorInicial);
        janela->addButton(matriz::i18n::t("comum.ok"), 1, juce::KeyPress(juce::KeyPress::returnKey));
        janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
        
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

const char* const kDescricaoPastaExplorer = "matriz:pasta-explorer";

void desenharTrianguloDisclosure(juce::Graphics& g, juce::Rectangle<int> area, bool expandido, juce::Colour cor) {
    juce::Path p;
    auto centro = area.getCentre().toFloat();
    float tamanho = 4.0f;
    if (expandido) {
        p.addTriangle(centro.x - tamanho, centro.y - tamanho * 0.5f,
                      centro.x + tamanho, centro.y - tamanho * 0.5f,
                      centro.x, centro.y + tamanho * 0.5f);
    } else {
        p.addTriangle(centro.x - tamanho * 0.5f, centro.y - tamanho,
                      centro.x + tamanho * 0.5f, centro.y,
                      centro.x - tamanho * 0.5f, centro.y + tamanho);
    }
    g.setColour(cor);
    g.fillPath(p);
}

void desenharIconePasta(juce::Graphics& g, juce::Rectangle<int> area, juce::Colour cor) {
    auto r = area.toFloat().reduced(2.0f);
    float abaTopo = r.getHeight() * 0.25f;
    float abaLarg = r.getWidth() * 0.4f;

    juce::Path p;
    p.addRoundedRectangle(r.getX(), r.getY() + abaTopo, r.getWidth(), r.getHeight() - abaTopo, 3.0f);
    p.addRoundedRectangle(r.getX(), r.getY(), abaLarg, abaTopo + 2.0f, 2.0f);

    g.setColour(cor);
    g.fillPath(p);
    g.setColour(cor.brighter(0.15f));
    g.strokePath(p, juce::PathStrokeType(1.0f));
}

void desenharIconeArquivo(juce::Graphics& g, juce::Rectangle<int> area, juce::Colour cor) {
    auto r = area.toFloat().reduced(4.0f);
    float dobra = 8.0f;

    juce::Path p;
    p.startNewSubPath(r.getX(), r.getY());
    p.lineTo(r.getRight() - dobra, r.getY());
    p.lineTo(r.getRight(), r.getY() + dobra);
    p.lineTo(r.getRight(), r.getBottom());
    p.lineTo(r.getX(), r.getBottom());
    p.closeSubPath();

    g.setColour(cor.withAlpha(0.6f));
    g.fillPath(p);
    g.setColour(cor);
    g.strokePath(p, juce::PathStrokeType(1.0f));
}

} // namespace

ArvoreComponent::ArvoreComponent(ProjetoAberto& projeto) : projeto_(projeto) { recarregarSincrono(); }

void ArvoreComponent::definirAba(Aba aba) {
    if (aba_ == aba) return;
    aba_ = aba;
    if (aoSelecionarNo) aoSelecionarNo(std::nullopt);
    pastaAtual_ = nullptr;
    cacheMiniaturas_.clear();
    recarregar();
}

void ArvoreComponent::definirModoVisao(ModoVisao modo) {
    if (modoVisao_ == modo) return;
    modoVisao_ = modo;
    if (modo == ModoVisao::Icones) {
        pastaAtual_ = nullptr;
        reconstruirConteudoPastaAtual();
    } else {
        reconstruirLinhas();
    }
    resized();
    repaint();
}

void ArvoreComponent::recarregar() {
    if (arvorePendente_) return;
    arvorePendente_ = true;
    const int geracao = ++geracaoArvore_;
    auto aba = aba_;
    juce::Component::SafePointer<ArvoreComponent> safeThis(this);
    ProjetoAberto* projeto = &projeto_;
    std::string idSelecionado = selecionado_ ? selecionado_->id : "";
    std::string idPastaAtual = pastaAtual_ ? pastaAtual_->id : "";

    poolArvore_.addJob([safeThis, projeto, aba, geracao, idSelecionado, idPastaAtual]() {
        ProjetoAberto::NoArvore raiz;
        try {
            raiz = aba == Aba::Origem ? projeto->arvoreOrigem() : projeto->arvoreAcervo();
        } catch (const std::exception&) {
            return;
        }

        juce::MessageManager::callAsync([safeThis, geracao, raiz = std::move(raiz), idSelecionado, idPastaAtual]() mutable {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            if (geracao != self->geracaoArvore_) return;
            MATRIZ_TRACE("ArvoreComponent::aplicarArvore");
            self->arvorePendente_ = false;
            self->raiz_ = std::move(raiz);
            self->selecionado_ = idSelecionado.empty() ? nullptr : self->encontrarNoPorId(self->raiz_, idSelecionado);
            self->pastaAtual_ = idPastaAtual.empty() ? nullptr : self->encontrarNoPorId(self->raiz_, idPastaAtual);
            if (self->modoVisao_ == ModoVisao::Icones)
                self->reconstruirConteudoPastaAtual();
            else
                self->reconstruirLinhas();
            self->resized();
            self->repaint();
        });
    });
}

void ArvoreComponent::recarregarSincrono() {
    MATRIZ_TRACE("ArvoreComponent::recarregarSincrono");
    std::string idSelecionado = selecionado_ ? selecionado_->id : "";
    std::string idPastaAtual = pastaAtual_ ? pastaAtual_->id : "";
    ++geracaoArvore_;
    arvorePendente_ = false;
    raiz_ = aba_ == Aba::Origem ? projeto_.arvoreOrigem() : projeto_.arvoreAcervo();
    selecionado_ = idSelecionado.empty() ? nullptr : encontrarNoPorId(raiz_, idSelecionado);
    pastaAtual_ = idPastaAtual.empty() ? nullptr : encontrarNoPorId(raiz_, idPastaAtual);
    if (modoVisao_ == ModoVisao::Icones)
        reconstruirConteudoPastaAtual();
    else
        reconstruirLinhas();
    resized();
    repaint();
}

void ArvoreComponent::reconstruirLinhas() {
    linhas_.clear();
    LinhaAchatada pseudo{&raiz_, 0, true, false};
    linhas_.push_back(pseudo);
    for (auto& filho : raiz_.filhos) achatar(filho, 0);
}

void ArvoreComponent::achatar(const ProjetoAberto::NoArvore& no, int profundidade) {
    bool ehPasta = !no.filhos.empty() || (aba_ == Aba::Acervo && !no.id.empty());
    linhas_.push_back({&no, profundidade, false, ehPasta});

    bool colapsado = colapsados_.count(no.id) > 0;
    if (!colapsado) {
        for (auto& filho : no.filhos) achatar(filho, profundidade + 1);
    }
}

juce::Rectangle<int> ArvoreComponent::boundsDaLinha(int indice) const {
    return {0, indice * kAlturaLinha, getWidth(), kAlturaLinha};
}

int ArvoreComponent::indiceLinhaNaPosicao(int y) const {
    if (y < 0) return -1;
    int indice = y / kAlturaLinha;
    if (indice < 0 || indice >= static_cast<int>(linhas_.size())) return -1;
    return indice;
}

// --- Icon view helpers ---

void ArvoreComponent::reconstruirConteudoPastaAtual() {
    itensIcone_.clear();
    breadcrumb_.clear();

    const ProjetoAberto::NoArvore* pasta = pastaAtual_ ? pastaAtual_ : &raiz_;

    // Build breadcrumb
    if (pastaAtual_) {
        // Walk up to find path - simplified: just show current name
        breadcrumb_.push_back(matriz::i18n::t("arvore.todos_os_materiais"));
        // Find path from root to current
        std::function<bool(const ProjetoAberto::NoArvore&, std::vector<juce::String>&)> acharCaminho;
        acharCaminho = [&](const ProjetoAberto::NoArvore& no, std::vector<juce::String>& caminho) -> bool {
            for (auto& filho : no.filhos) {
                caminho.push_back(filho.nome);
                if (&filho == pastaAtual_) return true;
                if (acharCaminho(filho, caminho)) return true;
                caminho.pop_back();
            }
            return false;
        };
        std::vector<juce::String> caminho;
        acharCaminho(raiz_, caminho);
        for (auto& c : caminho) breadcrumb_.push_back(c);
    }

    // Add subfolders first
    for (auto& filho : pasta->filhos) {
        ItemIcone item;
        item.noPasta = &filho;
        item.nome = filho.nome;
        item.ehPasta = true;
        itensIcone_.push_back(std::move(item));
    }

    // Add direct items (files in this folder)
    for (auto& itemId : pasta->itemIdsDiretos) {
        ItemIcone item;
        item.itemId = itemId;
        std::string titulo, tipoMidia, codigo;
        if (projeto_.obterItemInfo(itemId, titulo, tipoMidia, codigo))
            item.nome = juce::String(titulo);
        else
            item.nome = "?";
        item.ehPasta = false;
        itensIcone_.push_back(std::move(item));
    }
}

juce::Rectangle<int> ArvoreComponent::boundsDaCelula(int indice) const {
    int colunas = juce::jmax(1, getWidth() / kLarguraCelula);
    int linha = indice / colunas;
    int coluna = indice % colunas;
    int offsetY = pastaAtual_ ? kAlturaBreadcrumb : 0;
    return {coluna * kLarguraCelula, offsetY + linha * kAlturaCelula, kLarguraCelula, kAlturaCelula};
}

int ArvoreComponent::indiceCelulaNaPosicao(int x, int y) const {
    int offsetY = pastaAtual_ ? kAlturaBreadcrumb : 0;
    if (y < offsetY) return -2; // breadcrumb area
    int colunas = juce::jmax(1, getWidth() / kLarguraCelula);
    int linha = (y - offsetY) / kAlturaCelula;
    int coluna = x / kLarguraCelula;
    int indice = linha * colunas + coluna;
    if (indice < 0 || indice >= static_cast<int>(itensIcone_.size())) return -1;
    return indice;
}

void ArvoreComponent::navegarParaPasta(const ProjetoAberto::NoArvore* pasta) {
    pastaAtual_ = pasta;
    selecionado_ = pasta;
    cacheMiniaturas_.clear();
    reconstruirConteudoPastaAtual();
    resized();
    repaint();
    notificarSelecao();
}

void ArvoreComponent::navegarPraCima() {
    if (!pastaAtual_) return;

    // Find parent of current folder
    std::function<const ProjetoAberto::NoArvore*(const ProjetoAberto::NoArvore&)> acharPai;
    acharPai = [&](const ProjetoAberto::NoArvore& no) -> const ProjetoAberto::NoArvore* {
        for (auto& filho : no.filhos) {
            if (&filho == pastaAtual_) return &no;
            auto* resultado = acharPai(filho);
            if (resultado) return resultado;
        }
        return nullptr;
    };

    auto* pai = acharPai(raiz_);
    if (pai == &raiz_)
        pastaAtual_ = nullptr;
    else
        pastaAtual_ = pai;

    selecionado_ = pastaAtual_;
    cacheMiniaturas_.clear();
    reconstruirConteudoPastaAtual();
    resized();
    repaint();
    notificarSelecao();
}

inline juce::File obterPastaAssets() {
    return juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("Assets");
}

juce::Image ArvoreComponent::miniaturaParaItem(const std::string& itemId) {
    auto it = cacheMiniaturas_.find(itemId);
    if (it != cacheMiniaturas_.end()) return it->second;

    juce::Image img;
    if (auto caminho = projeto_.caminhoMiniaturaPrincipal(itemId)) {
        juce::File f(caminho->toStdString());
        if (f.existsAsFile()) {
            img = juce::ImageFileFormat::loadFrom(f);
            if (img.isValid()) {
                int s = 64;
                img = img.rescaled(s, s, juce::Graphics::mediumResamplingQuality);
            }
        }
    }

    if (!img.isValid()) {
        if (auto arq = projeto_.arquivoPrincipal(itemId)) {
            juce::File f(arq->caminhoAbsoluto);
            juce::String ext = f.getFileExtension().toLowerCase().replace(".", "");
            juce::String logoFile = matriz::ingest::obterLogoSessaoPorExtensao(ext);
            if (logoFile.isEmpty() && ext == "pd") logoFile = "puredata.png";

            if (logoFile.isNotEmpty()) {
                juce::File logoImgFile = obterPastaAssets().getChildFile(logoFile);
                if (logoImgFile.existsAsFile()) {
                    img = juce::ImageFileFormat::loadFrom(logoImgFile);
                    if (img.isValid()) {
                        img = img.rescaled(64, 64, juce::Graphics::mediumResamplingQuality);
                    }
                }
            }
        }
    }

    cacheMiniaturas_[itemId] = img;
    return img;
}

void ArvoreComponent::resized() {
    if (modoVisao_ == ModoVisao::Icones) {
        int colunas = juce::jmax(1, getWidth() / kLarguraCelula);
        int linhas = (static_cast<int>(itensIcone_.size()) + colunas - 1) / colunas;
        int offsetY = pastaAtual_ ? kAlturaBreadcrumb : 0;
        setSize(getWidth(), offsetY + linhas * kAlturaCelula + kAlturaCelula / 2);
    } else {
        setSize(getWidth(), static_cast<int>(linhas_.size()) * kAlturaLinha);
    }
}

void ArvoreComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().painel);
    if (modoVisao_ == ModoVisao::Icones)
        pintarModoIcones(g);
    else
        pintarModoLista(g);
}

void ArvoreComponent::pintarModoLista(juce::Graphics& g) {
    const auto& tk = tema();

    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
        auto& linha = linhas_[static_cast<size_t>(i)];
        auto bounds = boundsDaLinha(i);
        bool destacada = linha.ehPseudoTodos ? (selecionado_ == nullptr) : (linha.no == selecionado_);

        if (i == linhaAlvoDrop_) {
            g.setColour(tk.acento.withAlpha(0.30f));
            g.fillRect(bounds);
        } else if (destacada) {
            g.setColour(tk.acento.withAlpha(0.25f));
            g.fillRect(bounds);
        }

        auto miolo = bounds;
        miolo.removeFromLeft(tk.espacoMedio + linha.profundidade * kIndentacao);

        // Disclosure triangle for folders
        if (!linha.ehPseudoTodos && linha.ehPasta) {
            auto areaTriangulo = miolo.removeFromLeft(16);
            bool expandido = colapsados_.count(linha.no->id) == 0;
            desenharTrianguloDisclosure(g, areaTriangulo, expandido, tk.textoTerciario);
        } else if (!linha.ehPseudoTodos) {
            miolo.removeFromLeft(16);
        }

        juce::String nome = linha.ehPseudoTodos ? matriz::i18n::t("arvore.todos_os_materiais") : linha.no->nome;

        if (!linha.ehPseudoTodos) {
            auto areaContagem = miolo.removeFromRight(36);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawText(juce::String(static_cast<int>(linha.no->itemIds.size())), areaContagem,
                       juce::Justification::centredRight);
        }

        g.setColour(destacada ? tk.textoPrimario : tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, linha.ehPseudoTodos ? juce::Font::bold : juce::Font::plain)));
        g.drawText(nome, miolo.reduced(4, 0), juce::Justification::centredLeft, true);
    }
}

void ArvoreComponent::pintarModoIcones(juce::Graphics& g) {
    const auto& tk = tema();

    // Breadcrumb navigation bar
    if (pastaAtual_) {
        auto barra = juce::Rectangle<int>(0, 0, getWidth(), kAlturaBreadcrumb);
        g.setColour(tk.painelAlt);
        g.fillRect(barra);
        g.setColour(tk.borda);
        g.fillRect(barra.removeFromBottom(1));

        auto barraTexto = juce::Rectangle<int>(0, 0, getWidth(), kAlturaBreadcrumb - 1);

        // Back arrow
        g.setColour(tk.acento);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(juce::CharPointer_UTF8("\xe2\x97\x80"), barraTexto.removeFromLeft(24).reduced(4, 0),
                   juce::Justification::centred);

        // Breadcrumb path
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        for (int i = 0; i < static_cast<int>(breadcrumb_.size()); ++i) {
            if (i > 0) {
                g.setColour(tk.textoTerciario);
                g.drawText(" > ", barraTexto.removeFromLeft(20), juce::Justification::centred);
            }
            bool ultimo = i == static_cast<int>(breadcrumb_.size()) - 1;
            g.setColour(ultimo ? tk.textoPrimario : tk.acento);
            auto fonte = juce::Font(juce::FontOptions(tk.tamanhoFontePequena));
            auto largura = juce::jmin(120, static_cast<int>(juce::GlyphArrangement::getStringWidth(fonte, breadcrumb_[static_cast<size_t>(i)])) + 8);
            g.drawText(breadcrumb_[static_cast<size_t>(i)], barraTexto.removeFromLeft(largura), juce::Justification::centredLeft, true);
        }
    }

    // Icon cells
    for (int i = 0; i < static_cast<int>(itensIcone_.size()); ++i) {
        auto& item = itensIcone_[static_cast<size_t>(i)];
        auto celula = boundsDaCelula(i);

        if (i == iconeHover_) {
            g.setColour(tk.painelAlt);
            g.fillRoundedRectangle(celula.toFloat().reduced(2.0f), tk.raioPequeno);
        }

        // Check if this item is selected
        bool selecionadoItem = false;
        if (item.ehPasta && item.noPasta == selecionado_) selecionadoItem = true;

        if (selecionadoItem) {
            g.setColour(tk.acento.withAlpha(0.25f));
            g.fillRoundedRectangle(celula.toFloat().reduced(2.0f), tk.raioPequeno);
        }

        auto areaIcone = celula.reduced(8, 4).withHeight(56).withY(celula.getY() + 4);
        auto areaIconeInner = areaIcone.reduced(8, 0).withHeight(48);

        if (item.ehPasta) {
            desenharIconePasta(g, areaIconeInner, tk.acento.withAlpha(0.7f));
            // Item count inside folder
            g.setColour(tk.textoSobreAcento);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(juce::String(static_cast<int>(item.noPasta->itemIds.size())),
                       areaIconeInner, juce::Justification::centred);
        } else {
            auto img = miniaturaParaItem(item.itemId);
            if (img.isValid()) {
                auto thumbBounds = areaIconeInner.withSizeKeepingCentre(
                    juce::jmin(areaIconeInner.getWidth(), 48),
                    juce::jmin(areaIconeInner.getHeight(), 48));
                g.drawImage(img, thumbBounds.toFloat(), juce::RectanglePlacement::centred);
            } else {
                desenharIconeArquivo(g, areaIconeInner, tk.textoTerciario);
            }
        }

        // Name below icon
        auto areaNome = celula.withTop(areaIcone.getBottom()).reduced(4, 0);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(item.nome, areaNome, juce::Justification::centredTop, true);
    }

    // Empty state
    if (itensIcone_.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        auto centro = getLocalBounds().withTrimmedTop(pastaAtual_ ? kAlturaBreadcrumb : 0);
        g.drawText(aba_ == Aba::Acervo ? matriz::i18n::t("acoes.sem_pastas") : "",
                   centro, juce::Justification::centred);
    }
}

void ArvoreComponent::mouseDown(const juce::MouseEvent& e) {
    if (modoVisao_ == ModoVisao::Icones) {
        int indice = indiceCelulaNaPosicao(e.getPosition().x, e.getPosition().y);

        // Breadcrumb back arrow
        if (indice == -2 && pastaAtual_) {
            if (e.getPosition().x < 24) {
                navegarPraCima();
                return;
            }
            navegarPraCima();
            return;
        }

        if (indice < 0) return;
        auto& item = itensIcone_[static_cast<size_t>(indice)];

        if (e.mods.isPopupMenu() && item.ehPasta && aba_ == Aba::Acervo && !item.noPasta->id.empty()) {
            selecionado_ = item.noPasta;
            repaint();
            juce::PopupMenu menu;
            enum { kNovaSub = 1, kRenomear, kApagar };
            menu.addItem(kNovaSub, matriz::i18n::t("arvore.menu_nova_subpasta"));
            menu.addItem(kRenomear, matriz::i18n::t("arvore.menu_renomear"));
            menu.addItem(kApagar, matriz::i18n::t("arvore.menu_apagar"));
            std::string pastaId = item.noPasta->id;
            juce::String nomeAtual = item.noPasta ? juce::String(item.noPasta->nome) : juce::String();
            juce::Component::SafePointer<ArvoreComponent> safeThis(this);
            menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, pastaId, nomeAtual](int resultado) {
                if (!safeThis) return;
                auto* self = safeThis.getComponent();
                if (resultado == 1) {
                    juce::Component::SafePointer<ArvoreComponent> s2(self);
                    pedirTexto(matriz::i18n::t("arvore.nome_nova_pasta_titulo"), matriz::i18n::t("arvore.nome_nova_pasta_mensagem"),
                               "", [s2, pastaId](std::optional<juce::String> nome) {
                                   if (!s2 || !nome || nome->trim().isEmpty()) return;
                                   s2->projeto_.criarPastaAcervo(nome->trim().toStdString(), pastaId);
                                   s2->recarregar();
                                   if (s2->aoMudarOrganizacao) s2->aoMudarOrganizacao();
                               });
                } else if (resultado == 2) {
                    juce::Component::SafePointer<ArvoreComponent> s2(self);
                    pedirTexto(matriz::i18n::t("arvore.renomear_pasta_titulo"), matriz::i18n::t("arvore.renomear_pasta_mensagem"),
                               nomeAtual, [s2, pastaId](std::optional<juce::String> nome) {
                                   if (!s2 || !nome || nome->trim().isEmpty()) return;
                                   s2->projeto_.renomearPastaAcervo(pastaId, nome->trim().toStdString());
                                   s2->recarregar();
                                   if (s2->aoMudarOrganizacao) s2->aoMudarOrganizacao();
                               });
                } else if (resultado == 3) {
                    auto janela = std::make_shared<juce::AlertWindow>(matriz::i18n::t("arvore.apagar_confirmar_titulo"),
                                                                        matriz::i18n::t("arvore.apagar_confirmar_mensagem"),
                                                                        juce::MessageBoxIconType::WarningIcon);
                    janela->addButton(matriz::i18n::t("arvore.menu_apagar"), 1);
                    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
                    juce::Component::SafePointer<ArvoreComponent> s2(self);
                    janela->enterModalState(true, juce::ModalCallbackFunction::create([s2, janela, pastaId](int resultado) mutable {
                        retirarPeerDaTela(*janela);
                        if (!s2 || resultado != 1) {
                            janela.reset();
                            return;
                        }
                        s2->projeto_.apagarPastaAcervo(pastaId);
                        s2->recarregar();
                        if (s2->aoMudarOrganizacao) s2->aoMudarOrganizacao();
                        janela.reset();
                    }));
                }
            });
            return;
        }

        if (item.ehPasta) {
            selecionado_ = item.noPasta;
            repaint();
            notificarSelecao();
        }
        return;
    }

    // List mode
    int indice = indiceLinhaNaPosicao(e.getPosition().y);
    if (indice < 0) return;
    auto& linha = linhas_[static_cast<size_t>(indice)];

    // Check if click is on disclosure triangle
    if (!linha.ehPseudoTodos && linha.ehPasta) {
        int xTriangulo = tema().espacoMedio + linha.profundidade * kIndentacao;
        if (e.getPosition().x >= xTriangulo && e.getPosition().x < xTriangulo + 16) {
            if (colapsados_.count(linha.no->id) > 0)
                colapsados_.erase(linha.no->id);
            else
                colapsados_.insert(linha.no->id);
            reconstruirLinhas();
            resized();
            repaint();
            return;
        }
    }

    if (e.mods.isPopupMenu() && aba_ == Aba::Acervo && !linha.ehPseudoTodos && !linha.no->id.empty()) {
        abrirMenuContexto(indice);
        return;
    }

    selecionado_ = linha.ehPseudoTodos ? nullptr : linha.no;
    repaint();
    notificarSelecao();
}

void ArvoreComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (modoVisao_ == ModoVisao::Icones) {
        int indice = indiceCelulaNaPosicao(e.getPosition().x, e.getPosition().y);
        if (indice < 0) return;
        auto& item = itensIcone_[static_cast<size_t>(indice)];
        if (item.ehPasta) {
            navegarParaPasta(item.noPasta);
        }
        return;
    }

    // List mode: double-click on folder opens rename box in Acervo, otherwise toggles collapse
    int indice = indiceLinhaNaPosicao(e.getPosition().y);
    if (indice < 0) return;
    auto& linha = linhas_[static_cast<size_t>(indice)];
    if (!linha.ehPseudoTodos && linha.ehPasta) {
        if (aba_ == Aba::Acervo && !linha.no->id.empty()) {
            std::string pastaId = linha.no->id;
            juce::String nomeAtual = linha.no->nome;
            juce::Component::SafePointer<ArvoreComponent> s2(this);
            pedirTexto(matriz::i18n::t("arvore.renomear_pasta_titulo"), matriz::i18n::t("arvore.renomear_pasta_mensagem"),
                       nomeAtual, [s2, pastaId](std::optional<juce::String> nome) {
                           if (!s2 || !nome || nome->trim().isEmpty()) return;
                           s2->projeto_.renomearPastaAcervo(pastaId, nome->trim().toStdString());
                           s2->recarregar();
                           if (s2->aoMudarOrganizacao) s2->aoMudarOrganizacao();
                       });
        } else {
            if (colapsados_.count(linha.no->id) > 0)
                colapsados_.erase(linha.no->id);
            else
                colapsados_.insert(linha.no->id);
            reconstruirLinhas();
            resized();
            repaint();
        }
    }
}

void ArvoreComponent::notificarSelecao() {
    auto enviarSubpastas = [this](const std::vector<ProjetoAberto::NoArvore>& filhos) {
        if (!aoMudarSubpastas) return;
        std::vector<SubpastaInfo> subs;
        for (auto& f : filhos)
            subs.push_back({f.nome, static_cast<int>(f.itemIds.size()), f.itemIds});
        aoMudarSubpastas(std::move(subs));
    };

    if (!aoSelecionarNo) return;
    if (!selecionado_) {
        aoSelecionarNo(std::nullopt);
        enviarSubpastas(raiz_.filhos);
        return;
    }
    aoSelecionarNo(incluirSubpastas_ ? selecionado_->itemIds : selecionado_->itemIdsDiretos);
    enviarSubpastas(selecionado_->filhos);
}

void ArvoreComponent::selecionarNoPorItemIds(const std::set<std::string>& itemIds) {
    if (modoVisao_ == ModoVisao::Icones) {
        std::function<const ProjetoAberto::NoArvore*(const ProjetoAberto::NoArvore&)> buscar;
        buscar = [&](const ProjetoAberto::NoArvore& no) -> const ProjetoAberto::NoArvore* {
            if (no.itemIds == itemIds) return &no;
            for (auto& f : no.filhos)
                if (auto* achado = buscar(f)) return achado;
            return nullptr;
        };
        if (auto* achado = buscar(raiz_)) {
            navegarParaPasta(achado);
            return;
        }
    }
    for (auto& linha : linhas_) {
        if (linha.ehPseudoTodos) continue;
        if (linha.no->itemIds == itemIds) {
            selecionado_ = linha.no;
            repaint();
            notificarSelecao();
            return;
        }
    }
}

void ArvoreComponent::definirIncluirSubpastas(bool incluir) {
    if (incluirSubpastas_ == incluir) return;
    incluirSubpastas_ = incluir;
    notificarSelecao();
}

void ArvoreComponent::mouseDrag(const juce::MouseEvent& e) {
    if (aba_ != Aba::Origem || subarvoreArrastada_) return;
    if (e.getDistanceFromDragStart() < 8) return;

    if (modoVisao_ == ModoVisao::Lista) {
        int indice = indiceLinhaNaPosicao(e.getMouseDownPosition().y);
        if (indice < 0) return;
        auto& linha = linhas_[static_cast<size_t>(indice)];
        if (linha.ehPseudoTodos) return;

        auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (!container || container->isDragAndDropActive()) return;

        subarvoreArrastada_ = *linha.no;
        container->startDragging(juce::var(kDescricaoPastaExplorer), this);
    }
}

void ArvoreComponent::abrirMenuContexto(int indiceLinha) {
    std::string pastaId = linhas_[static_cast<size_t>(indiceLinha)].no->id;

    juce::PopupMenu menu;
    enum { kNovaSub = 1, kRenomear, kApagar };
    menu.addItem(kNovaSub, matriz::i18n::t("arvore.menu_nova_subpasta"));
    menu.addItem(kRenomear, matriz::i18n::t("arvore.menu_renomear"));
    menu.addItem(kApagar, matriz::i18n::t("arvore.menu_apagar"));

    juce::String nomeAtual;
    if (indiceLinha >= 0 && static_cast<size_t>(indiceLinha) < linhas_.size()) {
        const auto& l = linhas_[static_cast<size_t>(indiceLinha)];
        if (l.no) nomeAtual = l.no->nome;
    }

    juce::Component::SafePointer<ArvoreComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, pastaId, nomeAtual](int resultado) {
        if (!safeThis) return;
        auto* self = safeThis.getComponent();
        if (resultado == 1) {
            juce::Component::SafePointer<ArvoreComponent> s2(self);
            pedirTexto(matriz::i18n::t("arvore.nome_nova_pasta_titulo"), matriz::i18n::t("arvore.nome_nova_pasta_mensagem"),
                       "", [s2, pastaId](std::optional<juce::String> nome) {
                           if (!s2 || !nome || nome->trim().isEmpty()) return;
                           s2->projeto_.criarPastaAcervo(nome->trim().toStdString(), pastaId);
                           s2->recarregar();
                           if (s2->aoMudarOrganizacao) s2->aoMudarOrganizacao();
                       });
        } else if (resultado == 2) {
            juce::Component::SafePointer<ArvoreComponent> s2(self);
            pedirTexto(matriz::i18n::t("arvore.renomear_pasta_titulo"), matriz::i18n::t("arvore.renomear_pasta_mensagem"),
                       nomeAtual, [s2, pastaId](std::optional<juce::String> nome) {
                           if (!s2 || !nome || nome->trim().isEmpty()) return;
                           s2->projeto_.renomearPastaAcervo(pastaId, nome->trim().toStdString());
                           s2->recarregar();
                           if (s2->aoMudarOrganizacao) s2->aoMudarOrganizacao();
                       });
        } else if (resultado == 3) {
            auto janela = std::make_shared<juce::AlertWindow>(matriz::i18n::t("arvore.apagar_confirmar_titulo"),
                                                                matriz::i18n::t("arvore.apagar_confirmar_mensagem"),
                                                                juce::MessageBoxIconType::WarningIcon);
            janela->addButton(matriz::i18n::t("arvore.menu_apagar"), 1);
            janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
            juce::Component::SafePointer<ArvoreComponent> s2(self);
            janela->enterModalState(true, juce::ModalCallbackFunction::create([s2, janela, pastaId](int resultado) mutable {
                retirarPeerDaTela(*janela);
                if (!s2 || resultado != 1) {
                    janela.reset();
                    return;
                }
                s2->projeto_.apagarPastaAcervo(pastaId);
                s2->recarregar();
                if (s2->aoMudarOrganizacao) s2->aoMudarOrganizacao();
                janela.reset();
            }));
        }
    });
}

void ArvoreComponent::criarPastaDeTopoNivel() {
    pedirTexto(matriz::i18n::t("arvore.nome_nova_pasta_titulo"), matriz::i18n::t("arvore.nome_nova_pasta_mensagem"), "",
               [this](std::optional<juce::String> nome) {
                   if (!nome || nome->trim().isEmpty()) return;
                   projeto_.criarPastaAcervo(nome->trim().toStdString(), std::nullopt);
                   recarregar();
                   if (aoMudarOrganizacao) aoMudarOrganizacao();
               });
}

bool ArvoreComponent::isInterestedInDragSource(const SourceDetails& details) {
    if (aba_ != Aba::Acervo) return false;
    return details.description.isArray() || details.description.toString() == kDescricaoPastaExplorer;
}

void ArvoreComponent::itemDragMove(const SourceDetails& details) {
    if (modoVisao_ == ModoVisao::Icones) {
        // For icon mode, highlight the folder being hovered
        int indice = indiceCelulaNaPosicao(details.localPosition.x, details.localPosition.y);
        int novoAlvo = -1;
        if (indice >= 0 && indice < static_cast<int>(itensIcone_.size()) && itensIcone_[static_cast<size_t>(indice)].ehPasta)
            novoAlvo = indice;
        if (novoAlvo != iconeHover_) {
            iconeHover_ = novoAlvo;
            repaint();
        }
        return;
    }

    int indice = indiceLinhaNaPosicao(details.localPosition.y);
    bool alvoValido = indice >= 0 &&
                       (linhas_[static_cast<size_t>(indice)].ehPseudoTodos ||
                        !linhas_[static_cast<size_t>(indice)].no->id.empty());
    int novoAlvo = alvoValido ? indice : -1;
    if (novoAlvo != linhaAlvoDrop_) {
        linhaAlvoDrop_ = novoAlvo;
        repaint();
    }
}

void ArvoreComponent::itemDragExit(const SourceDetails&) {
    if (linhaAlvoDrop_ != -1 || iconeHover_ != -1) {
        linhaAlvoDrop_ = -1;
        iconeHover_ = -1;
        repaint();
    }
}

void ArvoreComponent::itemDropped(const SourceDetails& details) {
    std::string pastaDestinoId;

    if (modoVisao_ == ModoVisao::Icones) {
        int alvo = iconeHover_;
        iconeHover_ = -1;
        repaint();

        if (alvo >= 0 && alvo < static_cast<int>(itensIcone_.size()) && itensIcone_[static_cast<size_t>(alvo)].ehPasta) {
            pastaDestinoId = itensIcone_[static_cast<size_t>(alvo)].noPasta->id;
        } else if (pastaAtual_ && !pastaAtual_->id.empty()) {
            pastaDestinoId = pastaAtual_->id;
        } else {
            return;
        }
    } else {
        int alvo = linhaAlvoDrop_;
        linhaAlvoDrop_ = -1;
        repaint();
        if (alvo < 0) return;

        bool naRaiz = linhas_[static_cast<size_t>(alvo)].ehPseudoTodos;
        pastaDestinoId = naRaiz ? std::string() : linhas_[static_cast<size_t>(alvo)].no->id;

        if (details.description.toString() == kDescricaoPastaExplorer) {
            if (!subarvoreArrastada_) {
                auto* origem = dynamic_cast<ArvoreComponent*>(details.sourceComponent.get());
                if (origem) subarvoreArrastada_ = origem->pegarSubarvoreArrastada();
            }
            soltarPastaDaExplorer(pastaDestinoId);
            return;
        }

        if (naRaiz)
            pastaDestinoId = projeto_.criarPastaAcervo(matriz::i18n::t("arvore.pasta_raiz_auto").toStdString(), std::nullopt);
    }

    if (details.description.toString() == kDescricaoPastaExplorer) {
        if (!subarvoreArrastada_) {
            auto* origem = dynamic_cast<ArvoreComponent*>(details.sourceComponent.get());
            if (origem) subarvoreArrastada_ = origem->pegarSubarvoreArrastada();
        }
        soltarPastaDaExplorer(pastaDestinoId);
        return;
    }

    std::vector<std::string> itemIds;
    if (auto* arr = details.description.getArray())
        for (auto& v : *arr) itemIds.push_back(v.toString().toStdString());
    if (itemIds.empty()) return;

    if (pastaDestinoId.empty())
        pastaDestinoId = projeto_.criarPastaAcervo(matriz::i18n::t("arvore.pasta_raiz_auto").toStdString(), std::nullopt);

    projeto_.adicionarItensAPasta(itemIds, pastaDestinoId);
    recarregar();
    if (aoMudarOrganizacao) aoMudarOrganizacao();
}

void ArvoreComponent::soltarPastaDaExplorer(const std::string& pastaDestinoId) {
    if (!subarvoreArrastada_) return;
    auto subarvore = std::make_shared<ProjetoAberto::NoArvore>(std::move(*subarvoreArrastada_));
    subarvoreArrastada_.reset();

    int emSubpastas = static_cast<int>(subarvore->itemIds.size() - subarvore->itemIdsDiretos.size());

    if (emSubpastas <= 0) {
        projeto_.replicarSubarvoreNoAcervo(*subarvore, pastaDestinoId, true);
        recarregar();
        if (aoMudarOrganizacao) aoMudarOrganizacao();
        return;
    }

    auto janela = std::make_shared<juce::AlertWindow>(
        matriz::i18n::t("arvore.soltar_pasta_titulo"),
        matriz::i18n::t("arvore.soltar_pasta_mensagem")
            .replace("{pasta}", subarvore->nome)
            .replace("{n}", juce::String(emSubpastas)),
        juce::MessageBoxIconType::QuestionIcon);
    janela->addButton(matriz::i18n::t("arvore.manter_estrutura"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("arvore.achatar_estrutura"), 2);
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ArvoreComponent> safeThis(this);
    janela->enterModalState(
        true, juce::ModalCallbackFunction::create([safeThis, janela, subarvore, pastaDestinoId](int resultado) mutable {
            retirarPeerDaTela(*janela);
            if (!safeThis || (resultado != 1 && resultado != 2)) {
                janela.reset();
                return;
            }
            safeThis->projeto_.replicarSubarvoreNoAcervo(*subarvore, pastaDestinoId, resultado == 1);
            safeThis->recarregar();
            if (safeThis->aoMudarOrganizacao) safeThis->aoMudarOrganizacao();
            janela.reset();
        }));
}

const ProjetoAberto::NoArvore* ArvoreComponent::encontrarNoPorId(const ProjetoAberto::NoArvore& no, const std::string& id) const {
    if (no.id == id) return &no;
    for (auto& filho : no.filhos) {
        if (auto* achado = encontrarNoPorId(filho, id)) {
            return achado;
        }
    }
    return nullptr;
}

} // namespace matriz::ui
