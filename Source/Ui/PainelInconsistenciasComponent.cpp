#include "PainelInconsistenciasComponent.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

#include <set>

namespace matriz::ui {

PainelInconsistenciasComponent::PainelInconsistenciasComponent(ProjetoAberto& projeto) : projeto_(projeto) {}

void PainelInconsistenciasComponent::recarregar() {
    itens_.clear();

    // Só carrega definição de ficha dos tipos que de fato aparecem no
    // projeto — nunca as 14, a maioria delas irrelevante fora do Archive.
    std::map<std::string, matriz::ficha::FichaDefinition> definicoesPorTipo;
    std::set<std::string> tiposVistos;
    for (auto& item : projeto_.listarItens()) {
        if (tiposVistos.count(item.tipoMidia)) continue;
        tiposVistos.insert(item.tipoMidia);
        try {
            definicoesPorTipo.emplace(item.tipoMidia, projeto_.definicaoPara(item.tipoMidia));
        } catch (const std::exception&) {
            // Ficha ausente/corrompida pra esse tipo — não impede o resto
            // do painel, só não checa esse tipo específico.
        }
    }

    itens_ = matriz::ingest::detectarInconsistenciasFicha(projeto_.projeto().registro(), definicoesPorTipo);
    auto doDisco = matriz::ingest::verificarArquivosNoDisco(projeto_.projeto());
    itens_.insert(itens_.end(), doDisco.begin(), doDisco.end());

    setSize(getWidth(), juce::jmax(getParentHeight(),
                                     kAlturaCabecalho + static_cast<int>(itens_.size()) * kAlturaLinha +
                                         tema().espacoPainel));
    repaint();
}

void PainelInconsistenciasComponent::resized() {}

void PainelInconsistenciasComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painelAlt);

    auto area = getLocalBounds();
    auto areaCabecalho = area.removeFromTop(kAlturaCabecalho);
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
    g.drawText(matriz::i18n::t("painel_inconsistencias.titulo") + " (" + juce::String((int)itens_.size()) + ")",
               areaCabecalho.reduced(tk.espacoMedio, 0), juce::Justification::centredLeft);

    if (itens_.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("painel_inconsistencias.vazio"), area, juce::Justification::centred, true);
        return;
    }

    juce::Rectangle<int> clip = g.getClipBounds();
    for (size_t i = 0; i < itens_.size(); ++i) {
        juce::Rectangle<int> linha(0, kAlturaCabecalho + static_cast<int>(i) * kAlturaLinha, getWidth(), kAlturaLinha);
        if (linha.getBottom() < clip.getY() || linha.getY() > clip.getBottom()) continue;

        auto miolo = linha.reduced(tk.espacoMedio, 4);
        g.setColour(tk.alerta);
        g.fillRect(miolo.removeFromLeft(3));
        miolo.removeFromLeft(tk.espacoPequeno);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        // Nota de honestidade: `descricao` vem do motor de ingestão
        // (Source/Ingest/PainelInconsistencias.cpp), escrito antes da
        // externalização de strings da UI (§0.6/B.5) — ainda não passa por
        // i18n::t(). Corrigir isso exige estruturar cada tipo de
        // inconsistência com parâmetros próprios em vez de uma string
        // pronta; fica como item em aberto, não escondido.
        g.drawFittedText(juce::String(itens_[i].descricao), miolo, juce::Justification::centredLeft, 2);
    }
}

} // namespace matriz::ui
