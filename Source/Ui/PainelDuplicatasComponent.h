#pragma once

#include <JuceHeader.h>
#include "ProjetoAberto.h"

namespace matriz::ui {

class PainelDuplicatasComponent : public juce::Component {
public:
    explicit PainelDuplicatasComponent(ProjetoAberto& projeto);
    ~PainelDuplicatasComponent() override = default;

    void recarregar();
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class ContentComponent : public juce::Component {
    public:
        explicit ContentComponent(PainelDuplicatasComponent& owner);
        void atualizarGrupos(std::vector<ProjetoAberto::ParDuplicatas> novosGrupos);
        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        PainelDuplicatasComponent& owner_;
        std::vector<ProjetoAberto::ParDuplicatas> grupos_;

        struct BotaoInfo {
            std::unique_ptr<juce::TextButton> btnManter;
            std::unique_ptr<juce::TextButton> btnDuplicata;
            std::unique_ptr<juce::TextButton> btnRemover;
        };
        std::vector<std::vector<BotaoInfo>> botoesPorGrupo_;

        void acaoManter(const std::string& itemId);
        void acaoDuplicata(const std::string& itemId);
        void acaoRemover(const std::string& itemId);
    };

    ProjetoAberto& projeto_;
    juce::Viewport viewport_;
    std::unique_ptr<ContentComponent> conteudo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PainelDuplicatasComponent)
};

} // namespace matriz::ui
