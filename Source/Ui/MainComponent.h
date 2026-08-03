#pragma once

#include <JuceHeader.h>

#include <memory>

#include "ProjetoAberto.h"

// Layout de três painéis (§11.1): mosaico | visualizador+transporte+tira |
// ficha. Sem abas, sem janela flutuante. Nesta etapa (B.1.1-B.1.3): mosaico
// funcional e ficha lateral genérica; visualizador/transporte/tira de
// diagnóstico são B.1.4/B.1.5 (fronteira de etapa, ver walkthrough).

namespace matriz::ui {

class MosaicoComponent;
class FichaPanelComponent;

class MainComponent : public juce::Component {
public:
    MainComponent();
    ~MainComponent() override;

    void abrirProjeto(std::unique_ptr<matriz::model::Project> projeto);
    void fecharProjeto();
    bool temProjetoAberto() const { return projetoAberto_ != nullptr; }
    ProjetoAberto* projetoAberto() { return projetoAberto_.get(); }

    // Ingere cada arquivo como um item novo (§7 estágio 1: checksum + leitura
    // técnica automáticos). Ponte entre o motor de ingestão (Etapa 2,
    // headless) e a UI — chamada tanto pelo menu "Ingerir arquivos..." quanto
    // por arrastar arquivos do Finder direto no mosaico.
    void ingerirArquivos(const juce::Array<juce::File>& arquivos);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void selecionarItem(const std::string& itemId);
    void reconstruirTelaInicial();
    void reconstruirLayoutProjeto();

    std::unique_ptr<ProjetoAberto> projetoAberto_;

    // Tela inicial (nenhum projeto aberto)
    std::unique_ptr<juce::Label> telaInicialTitulo_;
    std::unique_ptr<juce::Label> telaInicialSubtitulo_;
    std::unique_ptr<juce::TextButton> telaInicialBotaoNovo_;
    std::unique_ptr<juce::TextButton> telaInicialBotaoAbrir_;

    // Layout de projeto aberto
    std::unique_ptr<juce::Viewport> mosaicoViewport_;
    std::unique_ptr<MosaicoComponent> mosaico_;
    std::unique_ptr<juce::Component> visualizadorPlaceholder_;
    std::unique_ptr<FichaPanelComponent> fichaPanel_;

public:
    std::function<void()> aoPedirNovoProjeto;
    std::function<void()> aoPedirAbrirProjeto;
};

} // namespace matriz::ui
