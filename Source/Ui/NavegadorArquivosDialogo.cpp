#include "NavegadorArquivosDialogo.h"

#include "../I18n/Strings.h"
#include "NavegadorArquivosComponent.h"
#include "Tokens.h"

#include <algorithm>

namespace matriz::ui {

namespace {

// Barra lateral: volumes conectados, favoritos e recentes (item 3.2). Fontes
// de rede já montadas aparecem como volume, que é como o sistema as
// apresenta — não há descoberta de rede própria aqui, e não finge haver.
class BarraLateral : public juce::Component {
public:
    std::function<void(juce::File)> aoEscolher;

    BarraLateral() { recarregar(); }

    void recarregar() {
        linhas_.clear();

        linhas_.push_back({matriz::i18n::t("navegador.secao_favoritos"), juce::File(), true});
        for (auto tipo : {juce::File::userHomeDirectory, juce::File::userDesktopDirectory,
                           juce::File::userDocumentsDirectory, juce::File::userMusicDirectory,
                           juce::File::userMoviesDirectory, juce::File::userPicturesDirectory}) {
            juce::File f = juce::File::getSpecialLocation(tipo);
            if (f.isDirectory()) linhas_.push_back({f.getFileName(), f, false});
        }

        linhas_.push_back({matriz::i18n::t("navegador.secao_volumes"), juce::File(), true});
        // Volumes conectados, incluindo montagens de rede já feitas pelo
        // sistema — que é como o SO as apresenta. Não há descoberta de rede
        // própria aqui, e o navegador não finge que há.
        juce::Array<juce::File> raizes;
        juce::File::findFileSystemRoots(raizes);
        for (const auto& raiz : raizes) {
            if (!raiz.isDirectory()) continue;
            juce::String nome = raiz.getVolumeLabel();
            linhas_.push_back({nome.isEmpty() ? raiz.getFullPathName() : nome, raiz, false});
        }

        if (!recentes_.empty()) {
            linhas_.push_back({matriz::i18n::t("navegador.secao_recentes"), juce::File(), true});
            for (auto& r : recentes_) linhas_.push_back({r.getFileName(), r, false});
        }

        setSize(getWidth(), static_cast<int>(linhas_.size()) * kAlturaLinha);
        repaint();
    }

    void registrarRecente(const juce::File& pasta) {
        if (!pasta.isDirectory()) return;
        recentes_.erase(std::remove(recentes_.begin(), recentes_.end(), pasta), recentes_.end());
        recentes_.insert(recentes_.begin(), pasta);
        if (recentes_.size() > 5) recentes_.resize(5);
        recarregar();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painelAlt);
        for (size_t i = 0; i < linhas_.size(); ++i) {
            juce::Rectangle<int> linha(0, static_cast<int>(i) * kAlturaLinha, getWidth(), kAlturaLinha);
            auto& l = linhas_[i];
            if (l.cabecalho) {
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            } else {
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            }
            g.drawText(l.rotulo, linha.reduced(l.cabecalho ? 6 : 16, 0), juce::Justification::centredLeft, true);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int i = e.getPosition().y / kAlturaLinha;
        if (i < 0 || i >= static_cast<int>(linhas_.size())) return;
        if (linhas_[static_cast<size_t>(i)].cabecalho) return;
        if (aoEscolher) aoEscolher(linhas_[static_cast<size_t>(i)].pasta);
    }

    static constexpr int kAlturaLinha = 22;

private:
    struct Linha {
        juce::String rotulo;
        juce::File pasta;
        bool cabecalho = false;
    };
    std::vector<Linha> linhas_;
    std::vector<juce::File> recentes_;
};

// Caminho navegável no topo, clicável em qualquer nível (item 3.2).
class BarraCaminho : public juce::Component {
public:
    std::function<void(juce::File)> aoEscolher;

    void definirPasta(const juce::File& pasta) {
        segmentos_.clear();
        juce::File atual = pasta;
        while (atual != juce::File()) {
            segmentos_.insert(segmentos_.begin(), atual);
            juce::File pai = atual.getParentDirectory();
            if (pai == atual) break;
            atual = pai;
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);
        juce::Font fonte(juce::FontOptions(tk.tamanhoFontePequena));
        g.setFont(fonte);
        int x = 6;
        limites_.clear();
        for (size_t i = 0; i < segmentos_.size(); ++i) {
            juce::String nome = segmentos_[i].getFileName();
            if (nome.isEmpty()) nome = segmentos_[i].getFullPathName();
            int largura = static_cast<int>(juce::GlyphArrangement::getStringWidth(fonte, nome)) + 8;
            juce::Rectangle<int> area(x, 0, largura, getHeight());
            limites_.push_back(area);
            g.setColour(i + 1 == segmentos_.size() ? tk.textoPrimario : tk.acento);
            g.drawText(nome, area, juce::Justification::centred, true);
            x += largura;
            if (i + 1 < segmentos_.size()) {
                g.setColour(tk.textoTerciario);
                g.drawText("/", juce::Rectangle<int>(x, 0, 10, getHeight()), juce::Justification::centred);
                x += 10;
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        for (size_t i = 0; i < limites_.size() && i < segmentos_.size(); ++i)
            if (limites_[i].contains(e.getPosition()) && aoEscolher) {
                aoEscolher(segmentos_[i]);
                return;
            }
    }

private:
    std::vector<juce::File> segmentos_;
    std::vector<juce::Rectangle<int>> limites_;
};

// Miniatura/prévia do item selecionado, sem sair do navegador (item 3.2).
class PainelPrevia : public juce::Component {
public:
    void definirArquivo(const juce::File& f) {
        arquivo_ = f;
        imagem_ = juce::Image();
        if (f.existsAsFile()) {
            // Só imagem tem prévia real aqui; áudio/vídeo/documento mostram
            // nome + tamanho + data. Gerar waveform/keyframe exigiria rodar
            // ffmpeg por arquivo selecionado dentro de um navegador —
            // caro e lento pra um painel de prévia; a leitura técnica de
            // verdade acontece no ingest.
            imagem_ = juce::ImageFileFormat::loadFrom(f);
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painelAlt);
        auto area = getLocalBounds().reduced(8);

        if (arquivo_ == juce::File()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawText(matriz::i18n::t("navegador.previa_vazia"), area, juce::Justification::centred, true);
            return;
        }

        auto areaTexto = area.removeFromBottom(70);
        if (imagem_.isValid()) {
            g.drawImageWithin(imagem_, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                               juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        } else {
            g.setColour(tk.painel);
            g.fillRect(area);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo)));
            g.drawText(arquivo_.getFileExtension().toUpperCase(), area, juce::Justification::centred);
        }

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText(arquivo_.getFileName(), areaTexto.removeFromTop(18), juce::Justification::centredLeft, true);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        if (arquivo_.isDirectory()) {
            g.drawText(matriz::i18n::t("navegador.previa_pasta"), areaTexto.removeFromTop(16),
                        juce::Justification::centredLeft, true);
        } else {
            g.drawText(juce::File::descriptionOfSizeInBytes(arquivo_.getSize()), areaTexto.removeFromTop(16),
                        juce::Justification::centredLeft, true);
        }
        g.drawText(arquivo_.getLastModificationTime().toString(true, true), areaTexto.removeFromTop(16),
                    juce::Justification::centredLeft, true);
    }

private:
    juce::File arquivo_;
    juce::Image imagem_;
};

// Conteúdo inteiro da janela.
class ConteudoNavegador : public juce::Component {
public:
    ConteudoNavegador(const juce::File& inicial, std::function<void(ResultadoNavegador)> aoAdicionar,
                       std::function<void(juce::File)> aoLembrar, std::function<void()> aoFechar)
        : aoAdicionar_(std::move(aoAdicionar)), aoLembrar_(std::move(aoLembrar)), aoFechar_(std::move(aoFechar)) {
        addAndMakeVisible(lateral_);
        lateral_.aoEscolher = [this](juce::File f) { navegar(f); };

        viewportNavegador_.setViewedComponent(&navegador_, false);
        addAndMakeVisible(viewportNavegador_);

        addAndMakeVisible(caminho_);
        caminho_.aoEscolher = [this](juce::File f) { navegar(f); };

        addAndMakeVisible(previa_);

        botaoVoltar_.setButtonText("<");
        botaoVoltar_.onClick = [this] {
            navegador_.voltar();
            aposNavegar();
        };
        addAndMakeVisible(botaoVoltar_);

        botaoAvancar_.setButtonText(">");
        botaoAvancar_.onClick = [this] {
            navegador_.avancar();
            aposNavegar();
        };
        addAndMakeVisible(botaoAvancar_);

        botaoSubir_.setButtonText(juce::String::fromUTF8("↑"));
        botaoSubir_.onClick = [this] {
            navegador_.subirUmNivel();
            aposNavegar();
        };
        addAndMakeVisible(botaoSubir_);

        busca_.setTextToShowWhenEmpty(matriz::i18n::t("navegador.buscar"), tema().textoTerciario);
        busca_.onTextChange = [this] { navegador_.definirBusca(busca_.getText()); };
        addAndMakeVisible(busca_);

        comboVisao_.addItem(matriz::i18n::t("navegador.visao_colunas"), 1);
        comboVisao_.addItem(matriz::i18n::t("navegador.visao_lista"), 2);
        comboVisao_.addItem(matriz::i18n::t("navegador.visao_icones"), 3);
        comboVisao_.setSelectedId(1, juce::dontSendNotification);
        comboVisao_.onChange = [this] {
            navegador_.definirVisao(comboVisao_.getSelectedId() == 2   ? NavegadorArquivosComponent::Visao::Lista
                                     : comboVisao_.getSelectedId() == 3 ? NavegadorArquivosComponent::Visao::Icones
                                                                        : NavegadorArquivosComponent::Visao::Colunas);
        };
        addAndMakeVisible(comboVisao_);

        comboFiltro_.addItem(matriz::i18n::t("navegador.filtro_todos"), 1);
        comboFiltro_.addItem(matriz::i18n::t("navegador.filtro_audio"), 2);
        comboFiltro_.addItem(matriz::i18n::t("navegador.filtro_video"), 3);
        comboFiltro_.addItem(matriz::i18n::t("navegador.filtro_imagem"), 4);
        comboFiltro_.addItem(matriz::i18n::t("navegador.filtro_documento"), 5);
        comboFiltro_.setSelectedId(1, juce::dontSendNotification);
        comboFiltro_.onChange = [this] {
            using F = NavegadorArquivosComponent::FiltroTipo;
            F f = F::Todos;
            switch (comboFiltro_.getSelectedId()) {
                case 2: f = F::Audio; break;
                case 3: f = F::Video; break;
                case 4: f = F::Imagem; break;
                case 5: f = F::Documento; break;
                default: break;
            }
            navegador_.definirFiltroTipo(f);
        };
        addAndMakeVisible(comboFiltro_);

        comboOrdem_.addItem(matriz::i18n::t("navegador.ordem_nome"), 1);
        comboOrdem_.addItem(matriz::i18n::t("navegador.ordem_data"), 2);
        comboOrdem_.addItem(matriz::i18n::t("navegador.ordem_tamanho"), 3);
        comboOrdem_.addItem(matriz::i18n::t("navegador.ordem_tipo"), 4);
        comboOrdem_.setSelectedId(1, juce::dontSendNotification);
        comboOrdem_.onChange = [this] {
            using O = NavegadorArquivosComponent::Ordenacao;
            O o = O::Nome;
            switch (comboOrdem_.getSelectedId()) {
                case 2: o = O::Data; break;
                case 3: o = O::Tamanho; break;
                case 4: o = O::Tipo; break;
                default: break;
            }
            navegador_.definirOrdenacao(o);
        };
        addAndMakeVisible(comboOrdem_);

        // Manter a estrutura de pastas é o PADRÃO (item 3.3) — adicionar uma
        // pasta traz a subárvore inteira; achatar é a escolha explícita.
        caixaManterEstrutura_.setButtonText(matriz::i18n::t("navegador.manter_estrutura"));
        caixaManterEstrutura_.setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(caixaManterEstrutura_);

        resumo_.setColour(juce::Label::textColourId, tema().textoSecundario);
        resumo_.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        addAndMakeVisible(resumo_);

        botaoAdicionar_.setButtonText(matriz::i18n::t("navegador.adicionar"));
        botaoAdicionar_.setEnabled(false); // desabilitado com seleção vazia (item 3.4)
        botaoAdicionar_.onClick = [this] { confirmar(); };
        addAndMakeVisible(botaoAdicionar_);

        navegador_.aoMudarSelecao = [this] { atualizarSelecao(); };
        navegador_.aoConfirmarSelecao = [this] { confirmar(); };

        juce::File abrirEm = inicial.isDirectory() ? inicial : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        navegar(abrirEm);
        setSize(1000, 640);
    }

    // Introspecção pra teste.
    NavegadorArquivosComponent& navegador() { return navegador_; }
    juce::TextButton& botaoAdicionarParaTeste() { return botaoAdicionar_; }
    juce::ToggleButton& caixaManterEstruturaParaTeste() { return caixaManterEstrutura_; }
    juce::Label& resumoParaTeste() { return resumo_; }

    void paint(juce::Graphics& g) override { g.fillAll(tema().fundo); }

    void resized() override {
        const auto& tk = tema();
        auto area = getLocalBounds();

        auto barraTopo = area.removeFromTop(32).reduced(4, 4);
        botaoVoltar_.setBounds(barraTopo.removeFromLeft(28));
        barraTopo.removeFromLeft(2);
        botaoAvancar_.setBounds(barraTopo.removeFromLeft(28));
        barraTopo.removeFromLeft(2);
        botaoSubir_.setBounds(barraTopo.removeFromLeft(28));
        barraTopo.removeFromLeft(tk.espacoMedio);
        comboVisao_.setBounds(barraTopo.removeFromRight(110));
        barraTopo.removeFromRight(4);
        comboFiltro_.setBounds(barraTopo.removeFromRight(120));
        barraTopo.removeFromRight(4);
        comboOrdem_.setBounds(barraTopo.removeFromRight(110));
        barraTopo.removeFromRight(4);
        busca_.setBounds(barraTopo.removeFromRight(200));

        caminho_.setBounds(area.removeFromTop(24));

        auto barraBaixo = area.removeFromBottom(40).reduced(6, 6);
        botaoAdicionar_.setBounds(barraBaixo.removeFromRight(200));
        barraBaixo.removeFromRight(tk.espacoMedio);
        caixaManterEstrutura_.setBounds(barraBaixo.removeFromRight(220));
        resumo_.setBounds(barraBaixo);

        lateral_.setBounds(area.removeFromLeft(170));
        previa_.setBounds(area.removeFromRight(220));
        viewportNavegador_.setBounds(area);
        navegador_.resized();
    }

private:
    void navegar(const juce::File& pasta) {
        navegador_.irPara(pasta);
        aposNavegar();
    }

    void aposNavegar() {
        juce::File atual = navegador_.pastaAtual();
        caminho_.definirPasta(atual);
        // A coluna da pasta atual é a última — rola até ela, senão o
        // operador olha as colunas ancestrais e não vê onde acabou de entrar.
        viewportNavegador_.setViewPosition(std::max(0, navegador_.getWidth() - viewportNavegador_.getWidth()), 0);
        botaoVoltar_.setEnabled(navegador_.podeVoltar());
        botaoAvancar_.setEnabled(navegador_.podeAvancar());
        lateral_.registrarRecente(atual);
        if (aoLembrar_) aoLembrar_(atual);
        atualizarSelecao();
    }

    void atualizarSelecao() {
        auto& sel = navegador_.selecao();
        botaoAdicionar_.setEnabled(!sel.empty());
        // O botão mostra a contagem quando há seleção (item 3.4).
        botaoAdicionar_.setButtonText(sel.empty() ? matriz::i18n::t("navegador.adicionar")
                                                  : matriz::i18n::t("navegador.adicionar_n")
                                                        .replace("{n}", juce::String(navegador_.totalArquivosNaSelecao())));
        resumo_.setText(navegador_.resumoSelecao(), juce::dontSendNotification);
        previa_.definirArquivo(sel.empty() ? juce::File() : sel.back());
    }

    void confirmar() {
        auto& sel = navegador_.selecao();
        if (sel.empty()) return;
        ResultadoNavegador r;
        r.selecionados = sel;
        r.manterEstrutura = caixaManterEstrutura_.getToggleState();
        if (aoAdicionar_) aoAdicionar_(r);
        // Fecha automaticamente ao adicionar (item 3.1).
        if (aoFechar_) aoFechar_();
    }

    BarraLateral lateral_;
    BarraCaminho caminho_;
    NavegadorArquivosComponent navegador_;
    juce::Viewport viewportNavegador_;
    PainelPrevia previa_;
    juce::TextButton botaoVoltar_, botaoAvancar_, botaoSubir_, botaoAdicionar_;
    juce::TextEditor busca_;
    juce::ComboBox comboVisao_, comboFiltro_, comboOrdem_;
    juce::ToggleButton caixaManterEstrutura_;
    juce::Label resumo_;
    std::function<void(ResultadoNavegador)> aoAdicionar_;
    std::function<void(juce::File)> aoLembrar_;
    std::function<void()> aoFechar_;
};

class JanelaNavegador : public juce::DocumentWindow {
public:
    JanelaNavegador(const juce::File& inicial, std::function<void(ResultadoNavegador)> aoAdicionar,
                     std::function<void(juce::File)> aoLembrar)
        : juce::DocumentWindow(matriz::i18n::t("navegador.titulo"), tema().fundo, juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        auto conteudo = std::make_unique<ConteudoNavegador>(inicial, std::move(aoAdicionar), std::move(aoLembrar),
                                                             [this] { fechar(); });
        conteudo_ = conteudo.get();
        setContentOwned(conteudo.release(), true);
        centreWithSize(1000, 640);
        setResizable(true, false);
    }

    void closeButtonPressed() override { fechar(); }

    ConteudoNavegador* conteudo() { return conteudo_; }

private:
    void fechar() { setVisible(false); }
    ConteudoNavegador* conteudo_ = nullptr;
};

} // namespace

std::unique_ptr<juce::DocumentWindow> mostrarNavegadorArquivos(const juce::File& ultimaLocalizacao,
                                                                std::function<void(ResultadoNavegador)> aoAdicionar,
                                                                std::function<void(juce::File)> aoLembrarLocalizacao) {
    auto janela = std::make_unique<JanelaNavegador>(ultimaLocalizacao, std::move(aoAdicionar),
                                                     std::move(aoLembrarLocalizacao));
    janela->setVisible(true);
    janela->toFront(true);
    return janela;
}

} // namespace matriz::ui
