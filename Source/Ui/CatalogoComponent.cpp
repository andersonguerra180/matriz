#include "CatalogoComponent.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

CatalogoComponent::CatalogoComponent() { setWantsKeyboardFocus(true); }

bool CatalogoComponent::abrir(const juce::File& pasta) {
    pastaCatalogo_ = matriz::catalogo::resolverPastaCatalogo(pasta);
    if (pastaCatalogo_ == juce::File()) return false;

    entradas_ = matriz::catalogo::abrir(pastaCatalogo_);
    cache_.clear();
    ordemCache_.clear();
    hover_ = -1;
    aplicarBusca();
    return true;
}

void CatalogoComponent::definirBusca(const juce::String& texto) {
    busca_ = texto.trim();
    aplicarBusca();
}

void CatalogoComponent::aplicarBusca() {
    visiveis_.clear();
    for (int i = 0; i < static_cast<int>(entradas_.size()); ++i) {
        const auto& e = entradas_[static_cast<size_t>(i)];
        if (busca_.isNotEmpty()) {
            // Busca no que o catálogo carrega: código, título, ficha e a
            // localização. "berlim" tem que achar tanto o que TEM Berlim na
            // ficha quanto o que ESTÁ numa pasta chamada Berlim.
            juce::String alvo = juce::String(e.codigoAcervo) + " " + juce::String(e.titulo) + " " + e.fichaJson +
                                 " " + e.caminhoOrigem + " " + e.volumeOrigem;
            if (!alvo.containsIgnoreCase(busca_)) continue;
        }
        visiveis_.push_back(i);
    }
    setSize(getWidth(), juce::jmax(getParentHeight(), static_cast<int>(visiveis_.size()) * kAlturaLinha));
    repaint();
}

void CatalogoComponent::resized() {
    setSize(getWidth(), juce::jmax(getParentHeight(), static_cast<int>(visiveis_.size()) * kAlturaLinha));
}

int CatalogoComponent::indiceNaPosicao(int y) const {
    if (y < 0) return -1;
    int i = y / kAlturaLinha;
    return i < static_cast<int>(visiveis_.size()) ? i : -1;
}

const juce::Image* CatalogoComponent::miniatura(int indiceVisivel) {
    if (indiceVisivel < 0 || indiceVisivel >= static_cast<int>(visiveis_.size())) return nullptr;
    int idx = visiveis_[static_cast<size_t>(indiceVisivel)];

    auto it = cache_.find(idx);
    if (it != cache_.end()) return it->second.isValid() ? &it->second : nullptr;

    const auto& e = entradas_[static_cast<size_t>(idx)];
    juce::Image img;
    if (e.miniaturaRelativa.isNotEmpty()) {
        juce::File arquivo = pastaCatalogo_.getChildFile(e.miniaturaRelativa);
        if (arquivo.existsAsFile()) img = juce::ImageFileFormat::loadFrom(arquivo);
    }

    // Guarda mesmo quando inválida — é o cache negativo que evita reabrir o
    // mesmo arquivo ausente a cada repaint.
    cache_[idx] = img;
    ordemCache_.push_back(idx);
    while (ordemCache_.size() > kCapacidadeCache) {
        cache_.erase(ordemCache_.front());
        ordemCache_.pop_front();
    }
    auto& guardada = cache_[idx];
    return guardada.isValid() ? &guardada : nullptr;
}

juce::String CatalogoComponent::descricaoDaEntradaParaTeste(int indice) const {
    if (indice < 0 || indice >= static_cast<int>(visiveis_.size())) return {};
    auto loc = matriz::catalogo::localizar(entradas_[static_cast<size_t>(visiveis_[static_cast<size_t>(indice)])],
                                            pastaCatalogo_);
    return loc.descricaoHumana;
}

bool CatalogoComponent::fonteConectadaParaTeste(int indice) const {
    if (indice < 0 || indice >= static_cast<int>(visiveis_.size())) return false;
    return matriz::catalogo::localizar(entradas_[static_cast<size_t>(visiveis_[static_cast<size_t>(indice)])],
                                        pastaCatalogo_)
        .fonteConectada;
}

void CatalogoComponent::mouseMove(const juce::MouseEvent& e) {
    int i = indiceNaPosicao(e.getPosition().y);
    if (i == hover_) return;
    hover_ = i;
    repaint();
}

void CatalogoComponent::mouseExit(const juce::MouseEvent&) {
    if (hover_ < 0) return;
    hover_ = -1;
    repaint();
}

void CatalogoComponent::mouseDown(const juce::MouseEvent& ev) {
    int i = indiceNaPosicao(ev.getPosition().y);
    if (i < 0) return;

    const auto& entrada = entradas_[static_cast<size_t>(visiveis_[static_cast<size_t>(i)])];
    auto loc = matriz::catalogo::localizar(entrada, pastaCatalogo_);

    if (loc.fonteConectada) {
        loc.arquivoReal.revealToUser();
        return;
    }

    // Fonte ausente: diz onde procurar. É a razão de o catálogo existir, e
    // por isso é um diálogo com o caminho completo, não um tooltip que some.
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle(matriz::i18n::t("catalogo.onde_esta_titulo"))
            .withMessage(matriz::i18n::t("catalogo.onde_esta_corpo")
                             .replace("{titulo}", juce::String(entrada.titulo))
                             .replace("{volume}", entrada.volumeOrigem.isNotEmpty()
                                                        ? entrada.volumeOrigem
                                                        : matriz::i18n::t("catalogo.volume_desconhecido"))
                             .replace("{local}", loc.descricaoHumana)
                             .replace("{caminho}", entrada.caminhoOrigem))
            .withButton(matriz::i18n::t("comum.ok")),
        static_cast<juce::ModalComponentManager::Callback*>(nullptr));
}

void CatalogoComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    if (visiveis_.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t(entradas_.empty() ? "catalogo.vazio" : "catalogo.sem_resultado"),
                   getLocalBounds().reduced(tk.espacoGrande), juce::Justification::centredTop, true);
        return;
    }

    auto clip = g.getClipBounds();
    int primeira = juce::jmax(0, clip.getY() / kAlturaLinha);
    int ultima = juce::jmin(static_cast<int>(visiveis_.size()), clip.getBottom() / kAlturaLinha + 1);

    for (int i = primeira; i < ultima; ++i) {
        const auto& entrada = entradas_[static_cast<size_t>(visiveis_[static_cast<size_t>(i)])];
        juce::Rectangle<int> linha(0, i * kAlturaLinha, getWidth(), kAlturaLinha);

        if (i == hover_) {
            g.setColour(tk.painelAlt);
            g.fillRect(linha);
        }
        g.setColour(tk.borda);
        g.fillRect(linha.getX(), linha.getBottom() - 1, linha.getWidth(), 1);

        auto miolo = linha.reduced(tk.espacoMedio, tk.espacoPequeno);

        // Miniatura
        auto areaImagem = miolo.removeFromLeft(kAlturaLinha - tk.espacoMedio);
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(areaImagem.toFloat(), tk.raioPequeno);
        if (const juce::Image* img = miniatura(i))
            g.drawImage(*img, areaImagem.toFloat(), juce::RectanglePlacement::centred);

        miolo.removeFromLeft(tk.espacoMedio);

        // Estado da fonte à direita: é a informação que o operador veio
        // buscar, então tem posição fixa em vez de vir depois do título.
        auto loc = matriz::catalogo::localizar(entrada, pastaCatalogo_);
        auto areaEstado = miolo.removeFromRight(110);
        g.setColour(loc.fonteConectada ? tk.estadoQcOk : tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText(matriz::i18n::t(loc.fonteConectada ? "catalogo.disponivel" : "catalogo.ausente"), areaEstado,
                   juce::Justification::centredRight);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        g.drawText(juce::String(entrada.titulo), miolo.removeFromTop(18), juce::Justification::centredLeft, true);

        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(juce::String(entrada.codigoAcervo), miolo.removeFromTop(15), juce::Justification::centredLeft, true);

        g.setColour(tk.textoTerciario);
        g.drawText(loc.descricaoHumana, miolo, juce::Justification::centredLeft, true);
    }
}

} // namespace matriz::ui
