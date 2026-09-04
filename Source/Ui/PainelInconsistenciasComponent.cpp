#include "PainelInconsistenciasComponent.h"
#include "../Diag/Watchdog.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

#include <set>

namespace matriz::ui {

PainelInconsistenciasComponent::PainelInconsistenciasComponent(ProjetoAberto& projeto) : projeto_(projeto) {}

std::vector<matriz::ingest::Inconsistencia> PainelInconsistenciasComponent::coletar(ProjetoAberto& projeto) {
    // Só carrega definição de ficha dos tipos que de fato aparecem no
    std::map<std::string, matriz::ficha::FichaDefinition> definicoesPorTipo;
    try {
        auto stmt = projeto.projeto().registro().prepare("SELECT DISTINCT tipo_midia FROM item WHERE tipo_midia IS NOT NULL AND tipo_midia <> ''");
        while (stmt.step()) {
            std::string tm = stmt.columnText(0);
            try {
                definicoesPorTipo.emplace(tm, projeto.definicaoPara(tm));
            } catch (const std::exception&) {}
        }
    } catch (...) {}

    auto itens = matriz::ingest::detectarInconsistenciasFicha(projeto.projeto().registro(), definicoesPorTipo);
    auto doDisco = matriz::ingest::verificarArquivosNoDisco(projeto.projeto());
    itens.insert(itens.end(), doDisco.begin(), doDisco.end());
    return itens;
}

void PainelInconsistenciasComponent::aplicar(std::vector<matriz::ingest::Inconsistencia> itens) {
    itens_ = std::move(itens);
    setSize(getWidth(), juce::jmax(getParentHeight(),
                                     kAlturaCabecalho + static_cast<int>(itens_.size()) * kAlturaLinha +
                                         tema().espacoPainel));
    repaint();
}

void PainelInconsistenciasComponent::recarregar() {
    const int geracao = ++geracao_;
    juce::Component::SafePointer<PainelInconsistenciasComponent> safeThis(this);
    ProjetoAberto* projeto = &projeto_;

    pool_.addJob([safeThis, projeto, geracao]() {
        std::vector<matriz::ingest::Inconsistencia> itens;
        try {
            itens = coletar(*projeto);
        } catch (const std::exception&) {
            return;  // projeto fechado no meio da verificação
        }

        juce::MessageManager::callAsync([safeThis, geracao, itens = std::move(itens)]() mutable {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            if (geracao != self->geracao_) return;  // verificação superada
            MATRIZ_TRACE("PainelInconsistencias::aplicar");
            self->aplicar(std::move(itens));
        });
    });
}

void PainelInconsistenciasComponent::recarregarSincrono() {
    ++geracao_;  // invalida qualquer verificação em voo
    aplicar(coletar(projeto_));
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
