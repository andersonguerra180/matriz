#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <set>
#include <string>

#include "EventBus.h"

namespace matriz::ui {

class ProjetoAberto;

// Os 6 cards de analytics (TOTAL ASSETS · STORAGE SIZE · PRIMARY FORMAT · NEEDS ATTENTION · BACKUP HEALTH ·
// NO FOLDER) num card coletivo de cantos arredondados, sobre a faixa de sub-abas FOLDER MAP / SPACE MAP (visível nas
// duas). Os números são calculados em BACKGROUND (conexão de leitura); só o desenho roda na message thread.
// BACKUP HEALTH, NEEDS ATTENTION e NO FOLDER são clicáveis. NO FOLDER segue o mapa selecionado no dropdown do Folder Map.
class CartoesAnalyticsComponent : public juce::Component, private EventBusListener, private juce::Timer {
    friend int rodarSemPastaSelfTest();  // --selftest-sem-pasta (SemPastaSelfTest.cpp)
public:
    explicit CartoesAnalyticsComponent(ProjetoAberto& projeto);
    ~CartoesAnalyticsComponent() override;

    // Altura necessária para a largura dada (os cards quebram em 2 ou 3 linhas em janelas estreitas).
    int alturaPara(int largura) const;

    // Relê tudo em background (agrupa pedidos repetidos).
    void recarregar();

    std::function<void(const std::set<std::string>&)> aoAbrirNoGrid;           // BACKUP HEALTH → itens vulneráveis
    std::function<void(const std::set<std::string>&)> aoClicarNeedsAttention;
    std::function<void()> aoAbrirSemPasta;                                      // NO FOLDER → STRUCTURE › _no folder (os ids são lidos lá)
    std::function<void()> aoMudarAltura;                                        // (reservado: a altura só depende da largura)

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    struct Dados {
        juce::uint64 totalAssets = 0;
        juce::int64 totalBytes = 0;
        std::string formatoPrincipal = "-";
        juce::uint64 formatoPrincipalQtd = 0;
        juce::uint64 precisaAtencao = 0;
        double saudeBackupPct = 0.0;
        juce::uint64 vulneraveis = 0;
        int semPasta = 0;
        bool semPastaOriginal = false;
        juce::String semPastaMapaNome;
        std::string semPastaMapaId;
        std::set<std::string> idsPrecisaAtencao;
    };
    // Cálculo puro (sem UI): seguro em thread de fundo.
    static Dados calcular(ProjetoAberto& projeto, const std::string& mapaId, bool mapaOriginal, const juce::String& mapaNome);

private:
    void aoItemAlterado(const EventoItemAlterado& e) override;
    void timerCallback() override;
    int colunasPara(int largura) const;
    juce::Rectangle<int> retanguloCard(int indice, int largura) const;

    ProjetoAberto& projeto_;
    Dados dados_;
    std::atomic<int> geracao_{0};
    int hover_ = -1;
    juce::ThreadPool pool_{1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CartoesAnalyticsComponent)
};

} // namespace matriz::ui
