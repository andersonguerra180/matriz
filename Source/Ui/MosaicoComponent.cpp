#include "MosaicoComponent.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

#include <algorithm>
#include <map>

namespace matriz::ui {

MosaicoComponent::MosaicoComponent(ProjetoAberto& projeto) : projeto_(projeto) { setWantsKeyboardFocus(true); }

MosaicoComponent::~MosaicoComponent() { poolMiniaturas_.removeAllJobs(true, 2000); }

void MosaicoComponent::recarregar() {
    {
        const juce::ScopedLock sl(cacheLock_);
        semMiniatura_.clear(); // um novo ingest pode ter gerado miniatura pra itens antes sem
    }
    itensTodos_ = projeto_.listarItens();
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirFiltroEstado(const juce::String& estado) {
    filtroEstado_ = estado;
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirFiltroTipo(const juce::String& tipo) {
    filtroTipo_ = tipo;
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirBusca(const juce::String& texto) {
    buscaTexto_ = texto;
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::definirOrdenacao(Ordenacao ordenacao) {
    ordenacao_ = ordenacao;
    aplicarFiltrosEOrdenacao();
}

void MosaicoComponent::aplicarFiltrosEOrdenacao() {
    itensFiltrados_.clear();
    juce::String buscaLower = buscaTexto_.toLowerCase();

    for (auto& item : itensTodos_) {
        if (filtroEstado_.isNotEmpty() && juce::String(item.estado) != filtroEstado_) continue;
        if (filtroTipo_.isNotEmpty() && juce::String(item.tipoMidia) != filtroTipo_) continue;
        if (buscaLower.isNotEmpty()) {
            juce::String alvo = (juce::String(item.codigoAcervo) + " " + juce::String(item.titulo)).toLowerCase();
            if (!alvo.contains(buscaLower)) continue;
        }
        itensFiltrados_.push_back(item);
    }

    auto comparador = [this](const ItemResumo& a, const ItemResumo& b) {
        switch (ordenacao_) {
            case Ordenacao::Titulo: return a.titulo < b.titulo;
            case Ordenacao::Estado: return a.estado < b.estado;
            case Ordenacao::Atualizado: return a.atualizadoEm > b.atualizadoEm;
            case Ordenacao::Codigo:
            default: return a.codigoAcervo < b.codigoAcervo;
        }
    };

    // Mosaico agrupado (Parte 1 da correção de fluxo, §3.5): por tipo de
    // mídia no Archive, por artista/lançamento no Catalog. A ordenação
    // escolhida pelo operador vale DENTRO de cada grupo, não entre grupos.
    bool catalog = projeto_.projeto().modo() == matriz::model::Modo::Catalogo;
    std::map<juce::String, std::vector<ItemResumo>> baldes;
    for (auto& item : itensFiltrados_) {
        juce::String chave;
        if (catalog && item.artistaLancamento && item.tituloLancamento) {
            chave = "0:" + *item.artistaLancamento + " — " + *item.tituloLancamento;
        } else if (catalog) {
            chave = "1:" + rotuloGrupoArchive(item); // sample/suporte sem lançamento — bucket pelo próprio tipo
        } else {
            chave = "0:" + rotuloGrupoArchive(item);
        }
        baldes[chave].push_back(item);
    }

    itensFiltrados_.clear();
    grupos_.clear();
    for (auto& [chave, itensDoGrupo] : baldes) {
        std::vector<ItemResumo> ordenados = itensDoGrupo;
        std::sort(ordenados.begin(), ordenados.end(), comparador);

        GrupoMosaico g;
        g.rotulo = chave.fromFirstOccurrenceOf(":", false, false) + " — " +
                   juce::String(static_cast<int>(ordenados.size()));
        g.indiceInicio = static_cast<int>(itensFiltrados_.size());
        g.quantidade = static_cast<int>(ordenados.size());
        grupos_.push_back(g);

        for (auto& item : ordenados) itensFiltrados_.push_back(std::move(item));
    }

    recalcularLayout();
    repaint();
}

juce::String MosaicoComponent::rotuloGrupoArchive(const ItemResumo& item) const {
    try {
        const auto& def = projeto_.definicaoPara(item.tipoMidia);
        return def.rotulo.empty() ? juce::String(item.tipoMidia) : juce::String(def.rotulo);
    } catch (const std::exception&) {
        return juce::String(item.tipoMidia);
    }
}

void MosaicoComponent::selecionarItem(const std::string& itemId) {
    selecionadoId_ = itemId;
    repaint();
}

void MosaicoComponent::recalcularLayout() {
    int larguraDisponivel = getWidth();
    colunas_ = juce::jmax(1, larguraDisponivel / kCelulaLargura);

    // Grupos são O(quantidade de grupos), não O(total de itens) — mesmo
    // com 10 mil itens num único grupo (ver stress test B.2), este laço
    // roda uma vez só por grupo, cada um O(1).
    int cursor = matriz::ui::tema().espacoPainel;
    for (auto& g : grupos_) {
        g.yTopo = cursor;
        g.yItens = cursor + kAlturaCabecalhoGrupo;
        g.linhas = (g.quantidade + colunas_ - 1) / colunas_;
        cursor = g.yItens + g.linhas * kCelulaAltura + kEspacoEntreGrupos;
    }

    setSize(getWidth(), juce::jmax(getParentHeight(), cursor));
}

void MosaicoComponent::resized() { recalcularLayout(); }

const GrupoMosaico* MosaicoComponent::grupoNaPosicaoY(int y) const {
    for (auto& g : grupos_) {
        int yFim = g.yItens + g.linhas * kCelulaAltura;
        if (y >= g.yTopo && y < yFim) return &g;
    }
    return nullptr;
}

juce::Rectangle<int> MosaicoComponent::boundsDaCelula(int indice) const {
    for (auto& g : grupos_) {
        if (indice < g.indiceInicio || indice >= g.indiceInicio + g.quantidade) continue;
        int localIndice = indice - g.indiceInicio;
        int coluna = localIndice % colunas_;
        int linha = localIndice / colunas_;
        return {coluna * kCelulaLargura, g.yItens + linha * kCelulaAltura, kCelulaLargura, kCelulaAltura};
    }
    return {};
}

int MosaicoComponent::indiceNaPosicao(juce::Point<int> pos) const {
    if (colunas_ <= 0) return -1;
    const GrupoMosaico* g = grupoNaPosicaoY(pos.y);
    if (!g || pos.y < g->yItens) return -1; // fora de um grupo, ou em cima do cabeçalho (não clicável)

    int coluna = pos.x / kCelulaLargura;
    if (coluna < 0 || coluna >= colunas_) return -1;
    int linha = (pos.y - g->yItens) / kCelulaAltura;
    int localIndice = linha * colunas_ + coluna;
    if (localIndice < 0 || localIndice >= g->quantidade) return -1;
    return g->indiceInicio + localIndice;
}

juce::Colour MosaicoComponent::corDoEstado(const std::string& estado) const {
    const auto& tk = matriz::ui::tema();
    if (estado == "capturado") return tk.estadoCapturado;
    if (estado == "qc_ok") return tk.estadoQcOk;
    if (estado == "alerta") return tk.estadoAlerta;
    if (estado == "publicado") return tk.estadoQcOk;
    return tk.estadoNaoDigitalizado;
}

void MosaicoComponent::mouseDown(const juce::MouseEvent& e) {
    int indice = indiceNaPosicao(e.getPosition());
    if (indice < 0) return;
    selecionadoId_ = itensFiltrados_[static_cast<size_t>(indice)].id;
    repaint();
    if (aoSelecionar) aoSelecionar(selecionadoId_);
}

const juce::Image* MosaicoComponent::miniaturaCache(const std::string& itemId) {
    const juce::ScopedLock sl(cacheLock_);
    auto it = cacheMiniaturas_.find(itemId);
    if (it != cacheMiniaturas_.end()) return &it->second;
    return nullptr;
}

void MosaicoComponent::pedirCarregamentoMiniatura(const std::string& itemId) {
    {
        const juce::ScopedLock sl(cacheLock_);
        if (emCarregamento_.count(itemId) || cacheMiniaturas_.count(itemId) || semMiniatura_.count(itemId)) return;
        emCarregamento_[itemId] = true;
    }

    // A consulta ao índice também vai pro pool — antes rodava síncrona na
    // thread de paint(), o que reconsultava o banco a cada repaint pra cada
    // célula sem miniatura ainda (achado no stress test de 10 mil itens,
    // B.2). Cache negativo (semMiniatura_) evita repetir a consulta depois
    // que já se sabe que o item não tem miniatura.
    juce::Component::SafePointer<MosaicoComponent> ponteiroSeguro(this);
    ProjetoAberto* projeto = &projeto_;

    poolMiniaturas_.addJob([ponteiroSeguro, projeto, itemId]() mutable {
        auto caminho = projeto->caminhoMiniaturaPrincipal(itemId);
        juce::Image imagem;
        if (caminho) imagem = juce::ImageFileFormat::loadFrom(juce::File(*caminho));
        bool temCaminho = caminho.has_value();

        juce::MessageManager::callAsync([ponteiroSeguro, itemId, imagem, temCaminho]() mutable {
            if (!ponteiroSeguro) return;
            auto* self = ponteiroSeguro.getComponent();
            const juce::ScopedLock sl(self->cacheLock_);
            self->emCarregamento_.erase(itemId);
            if (imagem.isValid()) {
                self->cacheMiniaturas_[itemId] = imagem;
                self->ordemCache_.push_back(itemId);
                while (self->ordemCache_.size() > kCapacidadeCache) {
                    self->cacheMiniaturas_.erase(self->ordemCache_.front());
                    self->ordemCache_.pop_front();
                }
            } else if (!temCaminho) {
                self->semMiniatura_[itemId] = true;
            }
            self->repaint();
        });
    });
}

void MosaicoComponent::paint(juce::Graphics& g) {
    const auto& tk = matriz::ui::tema();
    g.fillAll(tk.fundo);

    if (arrastandoArquivo_) {
        g.setColour(tk.acento.withAlpha(0.12f));
        g.fillRect(g.getClipBounds());
        g.setColour(tk.acento);
        g.drawRect(getLocalBounds(), 2);
    }

    if (itensFiltrados_.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t(arrastandoArquivo_ ? "mosaico.solte_para_ingerir" : "mosaico.vazio"),
                   getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    juce::Rectangle<int> clip = g.getClipBounds();
    if (colunas_ <= 0) return;

    // Só os grupos cujo intervalo vertical cruza o clip entram no laço —
    // O(grupos), nunca O(total de itens). Dentro de cada grupo, só as
    // linhas visíveis (mesma lógica de antes, agora relativa a g.yItens).
    for (auto& grupo : grupos_) {
        int yFimGrupo = grupo.yItens + grupo.linhas * kCelulaAltura;
        if (yFimGrupo < clip.getY() || grupo.yTopo > clip.getBottom()) continue;

        if (grupo.yTopo < clip.getBottom() && yFimGrupo > clip.getY()) {
            juce::Rectangle<int> areaCabecalho(0, grupo.yTopo, getWidth(), kAlturaCabecalhoGrupo);
            g.setColour(tk.painelAlt);
            g.fillRect(areaCabecalho);
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            g.drawText(grupo.rotulo, areaCabecalho.reduced(tema().espacoMedio, 0), juce::Justification::centredLeft);
        }

        int primeiraLinha = juce::jmax(0, (clip.getY() - grupo.yItens) / kCelulaAltura);
        int ultimaLinha = juce::jmax(0, (clip.getBottom() - grupo.yItens) / kCelulaAltura + 1);
        int primeiroLocal = juce::jmax(0, primeiraLinha * colunas_);
        int ultimoLocal = juce::jmin(grupo.quantidade, (ultimaLinha + 1) * colunas_);

        for (int local = primeiroLocal; local < ultimoLocal; ++local) {
            int i = grupo.indiceInicio + local;
            const ItemResumo& item = itensFiltrados_[static_cast<size_t>(i)];
        juce::Rectangle<int> bounds = boundsDaCelula(i).reduced(4);

        juce::Colour corEstado = corDoEstado(item.estado);

        // Área de miniatura (ou placeholder colorido pelo estado, §11.2)
        juce::Rectangle<int> areaImagem = bounds.withHeight(bounds.getHeight() - 34);
        const juce::Image* img = miniaturaCache(item.id);
        if (img) {
            g.setColour(tk.painelAlt);
            g.fillRoundedRectangle(areaImagem.toFloat(), tk.raioPequeno);
            g.drawImage(*img, areaImagem.toFloat(), juce::RectanglePlacement::centred);
        } else {
            pedirCarregamentoMiniatura(item.id);
            g.setColour(corEstado.withAlpha(0.25f));
            g.fillRoundedRectangle(areaImagem.toFloat(), tk.raioPequeno);
        }

        // Borda de estado (cinza/azul/verde/âmbar)
        g.setColour(corEstado);
        g.drawRoundedRectangle(areaImagem.toFloat(), tk.raioPequeno, 2.0f);

        // Halo de sincronizado — anel adicional por fora, sobreposto a qualquer estado
        if (item.sincronizado) {
            g.setColour(tk.haloSincronizado);
            g.drawRoundedRectangle(bounds.toFloat(), tk.raioMedio, 1.5f);
        }

        // Seleção
        if (item.id == selecionadoId_) {
            g.setColour(tk.bordaFoco);
            g.drawRoundedRectangle(bounds.toFloat().expanded(2.0f), tk.raioMedio, 2.0f);
        }

        // Código + título
        juce::Rectangle<int> areaTexto = bounds.removeFromBottom(30);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText(item.codigoAcervo, areaTexto.removeFromTop(15), juce::Justification::centredLeft, true);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(item.titulo, areaTexto, juce::Justification::centredLeft, true);
        }
    }
}

} // namespace matriz::ui
