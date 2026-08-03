#pragma once

#include <JuceHeader.h>

#include <atomic>
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

    // Ingere arquivos (ou pastas, expandidas recursivamente) como itens
    // novos. Pergunta o tipo de mídia do lote antes de processar — nunca
    // adivinha pela extensão (§6) — e roda checksum/leitura técnica em
    // background (nunca trava a UI). Ponte entre o motor de ingestão
    // (Etapa 2, headless) e a UI — chamada tanto pelo menu "Ingerir
    // arquivos..." quanto por arrastar arquivos do Finder no mosaico.
    void ingerirArquivos(const juce::Array<juce::File>& arquivosOuPastas);

    // Mesma coisa, mas pulando o diálogo modal de escolha de tipo — usado só
    // por --selftest-ingerir-arquivos, que não tem um humano pra responder o
    // diálogo (um AlertWindow modal travaria pra sempre num teste headless).
    void ingerirArquivosComTipoConhecido(const juce::Array<juce::File>& arquivosOuPastas, std::string tipoMidia);

    // Enquanto um lote está sendo processado em background, fechar/trocar
    // de projeto destruiria o banco que o job em background ainda está
    // usando — o menu consulta isto pra desabilitar essas ações até terminar.
    bool ingestEmAndamento() const { return ingestsPendentes_.load() > 0; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void selecionarItem(const std::string& itemId);
    void reconstruirTelaInicial();
    void reconstruirLayoutProjeto();
    std::vector<juce::File> expandirArquivos(const juce::Array<juce::File>& arquivosOuPastas) const;
    void processarLoteEmBackground(std::vector<juce::File> arquivos, std::string tipoMidia);
    void atualizarLabelProgresso();

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

    // Ingest em background (corrige o travamento reportado: cópia + checksum
    // + ffprobe/Exiv2 nunca rodam na thread de mensagens).
    juce::ThreadPool ingestPool_{1};
    std::unique_ptr<juce::Label> labelProgressoIngest_;
    std::atomic<int> ingestsPendentes_{0};
    std::atomic<int> ingestsTotalLote_{0};
    std::atomic<int> ingestsErrosLote_{0};

public:
    std::function<void()> aoPedirNovoProjeto;
    std::function<void()> aoPedirAbrirProjeto;

    // Se definido, substitui o AlertWindow padrão de resumo ao final de um
    // lote de ingest — único jeito de rodar o fluxo real em
    // --selftest-ingerir-arquivos, que não tem humano pra clicar OK num
    // modal (AlertWindow::showAsync com callback nulo roda um loop modal
    // síncrono nesta versão do JUCE, e travaria o self-test pra sempre).
    // Em produção fica vazio; o alerta normal continua aparecendo.
    std::function<void(int sucessos, juce::StringArray erros)> aoConcluirLoteIngestParaTeste;
};

} // namespace matriz::ui
