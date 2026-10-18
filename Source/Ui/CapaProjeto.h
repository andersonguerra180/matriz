#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>

namespace matriz::ui {

class ProjetoAberto;

// Capa de um PROJECT (DDP, vinil, fita, produção musical): imagem quadrada 1:1 e, no modo DETALHADO, o bloco "PROJECT COVER" com o nome
// do projeto, o artista, a origem da capa, "Trocar capa..." / "Remover" e uma área para soltar uma imagem. A capa é a mesma da ficha
// do IMPORT: trocar aqui troca no container (INTAKE, METADATA e Folder Map).
class CapaProjetoComponent : public juce::Component, public juce::FileDragAndDropTarget {
public:
    enum class Modo { Compacto, Detalhado };

    CapaProjetoComponent(ProjetoAberto& projeto, const std::string& nestId, Modo modo);
    ~CapaProjetoComponent() override;

    void definirContainer(const std::string& nestId);
    void recarregar();  // relê nome, artista e capa

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& arquivos) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { soltando_ = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { soltando_ = false; repaint(); }
    void filesDropped(const juce::StringArray& arquivos, int, int) override;

    // Depois de trocar/remover a capa (para quem hospeda recarregar o que mostra).
    std::function<void()> aoMudar;

    // Self-test.
    const juce::String& nomeParaTeste() const { return nome_; }
    const juce::String& artistaParaTeste() const { return artista_; }
    bool temCapaParaTeste() const { return caminhoCapa_.isNotEmpty(); }
    void trocarCapaParaTeste(const juce::File& imagem) { trocarCapa(imagem); }
    void removerCapaParaTeste() { removerCapa(); }

private:
    void escolherNovaCapa();
    void trocarCapa(const juce::File& imagem);
    void removerCapa();
    void aposMudar();

    ProjetoAberto& projeto_;
    std::string nestId_;
    Modo modo_;
    juce::String nome_, artista_, caminhoCapa_;
    std::string origem_;
    juce::TextButton btnTrocar_, btnRemover_;
    juce::Rectangle<int> areaCapa_, areaTexto_, areaSoltar_;
    bool soltando_ = false;
    std::shared_ptr<juce::FileChooser> seletor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CapaProjetoComponent)
};

}  // namespace matriz::ui
