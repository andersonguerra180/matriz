#pragma once

#include <JuceHeader.h>
#include "ProjetoAberto.h"
#include "PreservationWorkspaceComponent.h"

namespace matriz::ui {

class EstatisticasComponent : public juce::Component {
public:
    explicit EstatisticasComponent(ProjetoAberto& projeto);
    ~EstatisticasComponent() override = default;

    void recarregar();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    ProjetoAberto& projeto_;

    struct StatCard {
        juce::String titulo;
        juce::String valor;
        juce::String detalhe;
        juce::Colour cor;
    };

    struct CategoriaStat {
        juce::String nome;
        int quantidade = 0;
        juce::int64 tamanhoBytes = 0;
        juce::Colour cor;
    };

    struct ExtensaoStat {
        juce::String extensao;
        int quantidade = 0;
        juce::int64 tamanhoBytes = 0;
    };

    struct DecadaStat {
        juce::String decada;
        int quantidade = 0;
    };

    struct TagStat {
        juce::String tag;
        int quantidade = 0;
    };

    int totalItens_ = 0;
    juce::int64 totalBytes_ = 0;
    int vulneraveis_ = 0;
    int singleCopy_ = 0;
    int ausentes_ = 0;

    std::unique_ptr<PreservationWorkspaceComponent> preservationSection_;

    std::vector<StatCard> cards_;
    std::vector<CategoriaStat> categorias_;
    std::vector<ExtensaoStat> extensoes_;
    std::vector<DecadaStat> decadas_;
    std::vector<TagStat> tags_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EstatisticasComponent)
};

} // namespace matriz::ui
