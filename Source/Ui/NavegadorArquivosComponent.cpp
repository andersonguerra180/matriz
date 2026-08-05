#include "NavegadorArquivosComponent.h"

#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"
#include "Tokens.h"

#include <algorithm>

namespace matriz::ui {

namespace {

matriz::ingest::CategoriaMidia categoriaDe(const juce::File& f) { return matriz::ingest::categoriaPorExtensao(f); }

} // namespace

NavegadorArquivosComponent::NavegadorArquivosComponent() {
    setWantsKeyboardFocus(true);
    startTimer(120); // mede tamanho de pasta em fatias, fora do caminho do clique
}

NavegadorArquivosComponent::~NavegadorArquivosComponent() { stopTimer(); }

void NavegadorArquivosComponent::definirVisao(Visao v) {
    if (visao_ == v) return;
    visao_ = v;
    // Reconstrói: colunas mostra uma coluna por nível do caminho, lista e
    // ícones mostram só a pasta atual. Sem isto, trocar de visão deixava as
    // colunas ancestrais na tela em modo lista (bug pego pelo harness).
    reconstruirColunas();
}

void NavegadorArquivosComponent::definirOrdenacao(Ordenacao o) {
    ordenacao_ = o;
    reconstruirColunas();
}

void NavegadorArquivosComponent::definirBusca(const juce::String& texto) {
    busca_ = texto;
    reconstruirColunas();
}

void NavegadorArquivosComponent::definirFiltroTipo(FiltroTipo f) {
    filtroTipo_ = f;
    reconstruirColunas();
}

void NavegadorArquivosComponent::irPara(const juce::File& pasta) {
    if (!pasta.isDirectory()) return;
    registrarNoHistorico(pasta);

    // Cadeia da raiz até a pasta — é o que dá as colunas ancestrais à
    // esquerda, como no Finder.
    caminho_.clear();
    juce::File atual = pasta;
    while (atual != juce::File()) {
        caminho_.insert(caminho_.begin(), atual);
        juce::File pai = atual.getParentDirectory();
        if (pai == atual) break; // chegou na raiz do volume
        atual = pai;
    }
    reconstruirColunas();
}

void NavegadorArquivosComponent::registrarNoHistorico(const juce::File& pasta) {
    if (indiceHistorico_ >= 0 && indiceHistorico_ < static_cast<int>(historico_.size()) &&
        historico_[static_cast<size_t>(indiceHistorico_)] == pasta)
        return;
    // Navegar depois de voltar descarta o "avançar" pendente — mesmo
    // comportamento de histórico de navegador.
    if (indiceHistorico_ + 1 < static_cast<int>(historico_.size()))
        historico_.erase(historico_.begin() + indiceHistorico_ + 1, historico_.end());
    historico_.push_back(pasta);
    indiceHistorico_ = static_cast<int>(historico_.size()) - 1;
}

void NavegadorArquivosComponent::voltar() {
    if (!podeVoltar()) return;
    --indiceHistorico_;
    juce::File destino = historico_[static_cast<size_t>(indiceHistorico_)];
    caminho_.clear();
    juce::File atual = destino;
    while (atual != juce::File()) {
        caminho_.insert(caminho_.begin(), atual);
        juce::File pai = atual.getParentDirectory();
        if (pai == atual) break;
        atual = pai;
    }
    reconstruirColunas();
}

void NavegadorArquivosComponent::avancar() {
    if (!podeAvancar()) return;
    ++indiceHistorico_;
    juce::File destino = historico_[static_cast<size_t>(indiceHistorico_)];
    caminho_.clear();
    juce::File atual = destino;
    while (atual != juce::File()) {
        caminho_.insert(caminho_.begin(), atual);
        juce::File pai = atual.getParentDirectory();
        if (pai == atual) break;
        atual = pai;
    }
    reconstruirColunas();
}

void NavegadorArquivosComponent::subirUmNivel() {
    juce::File atual = pastaAtual();
    if (atual == juce::File()) return;
    juce::File pai = atual.getParentDirectory();
    if (pai != atual) irPara(pai);
}

bool NavegadorArquivosComponent::passaFiltros(const juce::File& arquivo, bool ehPasta) const {
    // Pasta nunca é escondida por filtro de tipo: esconder pasta impede de
    // navegar até o que está dentro dela.
    if (!ehPasta && filtroTipo_ != FiltroTipo::Todos) {
        auto cat = categoriaDe(arquivo);
        bool bate = (filtroTipo_ == FiltroTipo::Audio && cat == matriz::ingest::CategoriaMidia::Audio) ||
                    (filtroTipo_ == FiltroTipo::Video && cat == matriz::ingest::CategoriaMidia::Video) ||
                    (filtroTipo_ == FiltroTipo::Imagem && cat == matriz::ingest::CategoriaMidia::Imagem) ||
                    (filtroTipo_ == FiltroTipo::Documento && cat == matriz::ingest::CategoriaMidia::Documento);
        if (!bate) return false;
    }
    if (busca_.trim().isNotEmpty() && !arquivo.getFileName().containsIgnoreCase(busca_.trim())) return false;
    return true;
}

std::vector<NavegadorArquivosComponent::Entrada> NavegadorArquivosComponent::lerPasta(const juce::File& pasta) const {
    std::vector<Entrada> out;
    if (!pasta.isDirectory()) return out;

    // Busca ativa alcança as subpastas também (item 3.2 — "filtrando a pasta
    // atual e as subpastas"), então o modo de varredura muda com a busca.
    bool recursivo = busca_.trim().isNotEmpty();
    int flags = juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles;
    for (const auto& f : pasta.findChildFiles(flags, recursivo)) {
        bool ehPasta = f.isDirectory();
        if (recursivo && ehPasta) continue; // numa busca, o resultado é arquivo, não pasta intermediária
        if (!passaFiltros(f, ehPasta)) continue;
        Entrada e;
        e.arquivo = f;
        e.ehPasta = ehPasta;
        e.tamanho = ehPasta ? 0 : f.getSize();
        e.modificado = f.getLastModificationTime();
        out.push_back(std::move(e));
    }

    std::sort(out.begin(), out.end(), [this](const Entrada& a, const Entrada& b) {
        // Pasta antes de arquivo sempre — é o que faz navegar rápido.
        if (a.ehPasta != b.ehPasta) return a.ehPasta;
        switch (ordenacao_) {
            case Ordenacao::Data: return a.modificado > b.modificado;
            case Ordenacao::Tamanho: return a.tamanho > b.tamanho;
            case Ordenacao::Tipo:
                return a.arquivo.getFileExtension().compareIgnoreCase(b.arquivo.getFileExtension()) < 0;
            case Ordenacao::Nome:
            default: return a.arquivo.getFileName().compareIgnoreCase(b.arquivo.getFileName()) < 0;
        }
    });
    return out;
}

void NavegadorArquivosComponent::reconstruirColunas() {
    colunas_.clear();
    if (visao_ == Visao::Colunas) {
        for (auto& pasta : caminho_) colunas_.push_back({pasta, lerPasta(pasta), 0});
    } else if (!caminho_.empty()) {
        colunas_.push_back({caminho_.back(), lerPasta(caminho_.back()), 0});
    }
    resized();
    repaint();
}

void NavegadorArquivosComponent::limparSelecao() {
    selecao_.clear();
    notificarSelecao();
    repaint();
}

void NavegadorArquivosComponent::notificarSelecao() {
    for (auto& f : selecao_)
        if (f.isDirectory() && !cacheTamanhoPasta_.count(f.getFullPathName()))
            pastasPendentesDeMedida_.insert(f.getFullPathName());
    if (aoMudarSelecao) aoMudarSelecao();
}

void NavegadorArquivosComponent::timerCallback() {
    if (pastasPendentesDeMedida_.empty()) return;
    juce::String caminho = *pastasPendentesDeMedida_.begin();
    pastasPendentesDeMedida_.erase(pastasPendentesDeMedida_.begin());

    juce::File pasta(caminho);
    int contagem = 0;
    juce::int64 bytes = 0;
    for (const auto& f : pasta.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, true)) {
        ++contagem;
        bytes += f.getSize();
    }
    cacheTamanhoPasta_[caminho] = {contagem, bytes};
    if (aoMudarSelecao) aoMudarSelecao(); // o resumo muda quando a medida chega
}

int NavegadorArquivosComponent::totalArquivosNaSelecao() const {
    int total = 0;
    for (auto& f : selecao_) {
        if (!f.isDirectory()) {
            ++total;
            continue;
        }
        auto it = cacheTamanhoPasta_.find(f.getFullPathName());
        if (it != cacheTamanhoPasta_.end()) total += it->second.first;
    }
    return total;
}

juce::int64 NavegadorArquivosComponent::tamanhoTotalDaSelecao() const {
    juce::int64 total = 0;
    for (auto& f : selecao_) {
        if (!f.isDirectory()) {
            total += f.getSize();
            continue;
        }
        auto it = cacheTamanhoPasta_.find(f.getFullPathName());
        if (it != cacheTamanhoPasta_.end()) total += it->second.second;
    }
    return total;
}

juce::String NavegadorArquivosComponent::resumoSelecao() const {
    if (selecao_.empty()) return matriz::i18n::t("navegador.selecao_vazia");
    return matriz::i18n::t("navegador.resumo_selecao")
        .replace("{n}", juce::String(totalArquivosNaSelecao()))
        .replace("{tamanho}", juce::File::descriptionOfSizeInBytes(tamanhoTotalDaSelecao()));
}

// --- layout e desenho --------------------------------------------------------

void NavegadorArquivosComponent::resized() {
    int alturaNecessaria = 0;
    for (auto& c : colunas_) alturaNecessaria = std::max(alturaNecessaria, static_cast<int>(c.entradas.size()) * kAlturaLinha);
    if (visao_ == Visao::Colunas) {
        setSize(std::max(getParentWidth(), static_cast<int>(colunas_.size()) * kLarguraColuna),
                 std::max(getParentHeight(), alturaNecessaria));
    } else {
        setSize(std::max(getParentWidth(), kLarguraColuna), std::max(getParentHeight(), alturaNecessaria));
    }
}

void NavegadorArquivosComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    for (size_t ic = 0; ic < colunas_.size(); ++ic) {
        auto& coluna = colunas_[ic];
        int x = visao_ == Visao::Colunas ? static_cast<int>(ic) * kLarguraColuna : 0;
        int largura = visao_ == Visao::Colunas ? kLarguraColuna : getWidth();

        if (visao_ == Visao::Colunas && ic + 1 < colunas_.size()) {
            g.setColour(tk.borda);
            g.drawVerticalLine(x + largura - 1, 0.0f, static_cast<float>(getHeight()));
        }

        for (size_t i = 0; i < coluna.entradas.size(); ++i) {
            auto& e = coluna.entradas[i];
            juce::Rectangle<int> linha(x, static_cast<int>(i) * kAlturaLinha, largura - 1, kAlturaLinha);
            bool selecionado = std::find(selecao_.begin(), selecao_.end(), e.arquivo) != selecao_.end();
            bool noCaminho = e.ehPasta && std::find(caminho_.begin(), caminho_.end(), e.arquivo) != caminho_.end();

            if (selecionado) {
                g.setColour(tk.acento);
                g.fillRect(linha);
            } else if (noCaminho) {
                g.setColour(tk.painelAlt);
                g.fillRect(linha);
            }

            g.setColour(selecionado ? tk.textoSobreAcento : tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawText(e.arquivo.getFileName(), linha.reduced(6, 0).withTrimmedRight(e.ehPasta ? 14 : 0),
                        juce::Justification::centredLeft, true);

            if (e.ehPasta) {
                // Seta de "tem mais coisa aqui dentro" — é o que ensina que
                // a coluna seguinte existe.
                g.setColour(selecionado ? tk.textoSobreAcento : tk.textoTerciario);
                g.drawText(">", linha.removeFromRight(14), juce::Justification::centred);
            } else if (visao_ != Visao::Colunas) {
                g.setColour(selecionado ? tk.textoSobreAcento : tk.textoTerciario);
                g.drawText(juce::File::descriptionOfSizeInBytes(e.tamanho), linha.removeFromRight(90),
                            juce::Justification::centredRight);
            }
        }
    }

    if (lacoAtivo_) {
        g.setColour(tk.acento.withAlpha(0.18f));
        g.fillRect(lacoAtual_);
        g.setColour(tk.acento);
        g.drawRect(lacoAtual_, 1);
    }

    if (colunas_.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("navegador.vazio"), getLocalBounds(), juce::Justification::centred);
    }
}

// --- interação ---------------------------------------------------------------

int NavegadorArquivosComponent::colunaNaPosicao(juce::Point<int> p) const {
    if (visao_ != Visao::Colunas) return colunas_.empty() ? -1 : 0;
    int ic = p.x / kLarguraColuna;
    return ic >= 0 && ic < static_cast<int>(colunas_.size()) ? ic : -1;
}

int NavegadorArquivosComponent::indiceEntradaNaPosicao(int coluna, juce::Point<int> p) const {
    if (coluna < 0 || coluna >= static_cast<int>(colunas_.size())) return -1;
    int i = p.y / kAlturaLinha;
    return i >= 0 && i < static_cast<int>(colunas_[static_cast<size_t>(coluna)].entradas.size()) ? i : -1;
}

void NavegadorArquivosComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    int ic = colunaNaPosicao(e.getPosition());
    int ie = indiceEntradaNaPosicao(ic, e.getPosition());

    if (ie < 0) { // clique em área vazia começa o laço de seleção
        lacoAtivo_ = true;
        lacoInicio_ = e.getPosition();
        lacoAtual_ = juce::Rectangle<int>(lacoInicio_, lacoInicio_);
        selecaoAntesDoLaco_ = e.mods.isCommandDown() ? selecao_ : std::vector<juce::File>();
        if (!e.mods.isCommandDown()) selecao_.clear();
        notificarSelecao();
        repaint();
        return;
    }

    const auto& entrada = colunas_[static_cast<size_t>(ic)].entradas[static_cast<size_t>(ie)];

    if (e.mods.isShiftDown() && ancoraShift_ != juce::File()) {
        // Intervalo contíguo dentro da mesma coluna.
        auto& entradas = colunas_[static_cast<size_t>(ic)].entradas;
        int indiceAncora = -1;
        for (size_t i = 0; i < entradas.size(); ++i)
            if (entradas[i].arquivo == ancoraShift_) indiceAncora = static_cast<int>(i);
        if (indiceAncora >= 0) {
            selecao_.clear();
            for (int i = std::min(indiceAncora, ie); i <= std::max(indiceAncora, ie); ++i)
                selecao_.push_back(entradas[static_cast<size_t>(i)].arquivo);
            notificarSelecao();
            repaint();
            return;
        }
    }

    if (e.mods.isCommandDown()) {
        auto it = std::find(selecao_.begin(), selecao_.end(), entrada.arquivo);
        if (it != selecao_.end()) selecao_.erase(it);
        else selecao_.push_back(entrada.arquivo);
    } else {
        selecao_ = {entrada.arquivo};
        // Clicar numa pasta navega pra dentro dela (abre a coluna seguinte),
        // mantendo-a selecionada — é assim que o Finder se comporta, e é o
        // que permite selecionar a pasta INTEIRA e adicionar de uma vez.
        if (entrada.ehPasta) {
            juce::File pasta = entrada.arquivo;
            irPara(pasta);
            selecao_ = {pasta};
        }
    }
    ancoraShift_ = entrada.arquivo;
    notificarSelecao();
    repaint();
}

void NavegadorArquivosComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!lacoAtivo_) return;
    lacoAtual_ = juce::Rectangle<int>(lacoInicio_, e.getPosition());
    atualizarSelecaoDoLaco(e);
    repaint();
}

void NavegadorArquivosComponent::atualizarSelecaoDoLaco(const juce::MouseEvent&) {
    selecao_ = selecaoAntesDoLaco_;
    for (size_t ic = 0; ic < colunas_.size(); ++ic) {
        int x = visao_ == Visao::Colunas ? static_cast<int>(ic) * kLarguraColuna : 0;
        int largura = visao_ == Visao::Colunas ? kLarguraColuna : getWidth();
        auto& entradas = colunas_[ic].entradas;
        for (size_t i = 0; i < entradas.size(); ++i) {
            juce::Rectangle<int> linha(x, static_cast<int>(i) * kAlturaLinha, largura, kAlturaLinha);
            if (!lacoAtual_.intersects(linha)) continue;
            if (std::find(selecao_.begin(), selecao_.end(), entradas[i].arquivo) == selecao_.end())
                selecao_.push_back(entradas[i].arquivo);
        }
    }
    notificarSelecao();
}

void NavegadorArquivosComponent::mouseUp(const juce::MouseEvent&) {
    if (!lacoAtivo_) return;
    lacoAtivo_ = false;
    repaint();
}

void NavegadorArquivosComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    int ic = colunaNaPosicao(e.getPosition());
    int ie = indiceEntradaNaPosicao(ic, e.getPosition());
    if (ie < 0) return;
    const auto& entrada = colunas_[static_cast<size_t>(ic)].entradas[static_cast<size_t>(ie)];
    if (entrada.ehPasta) return; // pasta já navega no clique simples
    if (aoConfirmarSelecao) aoConfirmarSelecao();
}

bool NavegadorArquivosComponent::keyPressed(const juce::KeyPress& k) {
    // Navegável por teclado, como todo o resto (item 3.2).
    if (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey) {
        if (colunas_.empty()) return false;
        auto& entradas = colunas_.back().entradas;
        if (entradas.empty()) return false;
        int atual = -1;
        for (size_t i = 0; i < entradas.size(); ++i)
            if (!selecao_.empty() && entradas[i].arquivo == selecao_.back()) atual = static_cast<int>(i);
        int proximo = juce::jlimit(0, static_cast<int>(entradas.size()) - 1,
                                   atual + (k.getKeyCode() == juce::KeyPress::downKey ? 1 : -1));
        selecao_ = {entradas[static_cast<size_t>(proximo)].arquivo};
        ancoraShift_ = selecao_.front();
        notificarSelecao();
        repaint();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::leftKey) {
        subirUmNivel();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::rightKey || k.getKeyCode() == juce::KeyPress::returnKey) {
        if (!selecao_.empty() && selecao_.back().isDirectory()) {
            juce::File pasta = selecao_.back();
            irPara(pasta);
            selecao_ = {pasta};
            notificarSelecao();
        } else if (k.getKeyCode() == juce::KeyPress::returnKey && aoConfirmarSelecao) {
            aoConfirmarSelecao();
        }
        return true;
    }
    if (k == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) {
        if (!colunas_.empty()) {
            selecao_.clear();
            for (auto& e : colunas_.back().entradas) selecao_.push_back(e.arquivo);
            notificarSelecao();
            repaint();
        }
        return true;
    }
    return false;
}

// --- introspecção pra teste --------------------------------------------------

std::vector<juce::File> NavegadorArquivosComponent::entradasVisiveisParaTeste(int coluna) const {
    std::vector<juce::File> out;
    if (coluna < 0 || coluna >= static_cast<int>(colunas_.size())) return out;
    for (auto& e : colunas_[static_cast<size_t>(coluna)].entradas) out.push_back(e.arquivo);
    return out;
}

void NavegadorArquivosComponent::selecionarParaTeste(const juce::File& arquivo, bool somarASelecao) {
    if (!somarASelecao) selecao_.clear();
    if (std::find(selecao_.begin(), selecao_.end(), arquivo) == selecao_.end()) selecao_.push_back(arquivo);
    ancoraShift_ = arquivo;
    notificarSelecao();
    repaint();
}

} // namespace matriz::ui
