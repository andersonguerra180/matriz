#include "MosaicoComponent.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

#include <algorithm>

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

    std::sort(itensFiltrados_.begin(), itensFiltrados_.end(), [this](const ItemResumo& a, const ItemResumo& b) {
        switch (ordenacao_) {
            case Ordenacao::Titulo: return a.titulo < b.titulo;
            case Ordenacao::Estado: return a.estado < b.estado;
            case Ordenacao::Atualizado: return a.atualizadoEm > b.atualizadoEm;
            case Ordenacao::Codigo:
            default: return a.codigoAcervo < b.codigoAcervo;
        }
    });

    recalcularLayout();
    repaint();
}

void MosaicoComponent::selecionarItem(const std::string& itemId) {
    selecionadoId_ = itemId;
    repaint();
}

void MosaicoComponent::recalcularLayout() {
    int larguraDisponivel = getWidth();
    colunas_ = juce::jmax(1, larguraDisponivel / kCelulaLargura);
    int linhas = (static_cast<int>(itensFiltrados_.size()) + colunas_ - 1) / colunas_;
    setSize(getWidth(), juce::jmax(getParentHeight(), linhas * kCelulaAltura + matriz::ui::tema().espacoPainel));
}

void MosaicoComponent::resized() { recalcularLayout(); }

juce::Rectangle<int> MosaicoComponent::boundsDaCelula(int indice) const {
    int coluna = indice % colunas_;
    int linha = indice / colunas_;
    return {coluna * kCelulaLargura, linha * kCelulaAltura, kCelulaLargura, kCelulaAltura};
}

int MosaicoComponent::indiceNaPosicao(juce::Point<int> pos) const {
    if (colunas_ <= 0) return -1;
    int coluna = pos.x / kCelulaLargura;
    int linha = pos.y / kCelulaAltura;
    if (coluna < 0 || coluna >= colunas_) return -1;
    int indice = linha * colunas_ + coluna;
    if (indice < 0 || indice >= static_cast<int>(itensFiltrados_.size())) return -1;
    return indice;
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

    if (itensFiltrados_.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("mosaico.vazio"), getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    juce::Rectangle<int> clip = g.getClipBounds();
    if (colunas_ <= 0) return;

    int primeiraLinha = juce::jmax(0, clip.getY() / kCelulaAltura);
    int ultimaLinha = clip.getBottom() / kCelulaAltura + 1;
    int primeiroIndice = primeiraLinha * colunas_;
    int ultimoIndice = juce::jmin(static_cast<int>(itensFiltrados_.size()), (ultimaLinha + 1) * colunas_);

    for (int i = primeiroIndice; i < ultimoIndice; ++i) {
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

} // namespace matriz::ui
