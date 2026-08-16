#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

namespace matriz::ui {

class ProjetoAberto;

class HomePanelComponent : public juce::Component {
public:
    explicit HomePanelComponent(ProjetoAberto& projeto);
    ~HomePanelComponent() override;

    void recarregar();

    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void()> aoIngerir;
    std::function<void()> aoCatalogar;
    std::function<void()> aoBackup;
    std::function<void()> aoPreservacao;
    std::function<void(const std::string& chave)> aoClicarAtencao;

private:
    class CartaoTarefa;
    class LinhaAtencao;

    ProjetoAberto& projeto_;

    std::unique_ptr<juce::Label> titulo_;
    std::unique_ptr<juce::Label> subtitulo_;
    std::unique_ptr<juce::Label> totalAssets_;

    std::unique_ptr<CartaoTarefa> cartaoIngest_;
    std::unique_ptr<CartaoTarefa> cartaoCatalog_;
    std::unique_ptr<CartaoTarefa> cartaoBackup_;
    std::unique_ptr<CartaoTarefa> cartaoPreservacao_;

    std::unique_ptr<juce::Label> atencaoTitulo_;
    std::vector<std::unique_ptr<LinhaAtencao>> linhasAtencao_;
};

} // namespace matriz::ui
