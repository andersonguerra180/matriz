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
class PainelInconsistenciasComponent;
class CartaoModo;
class LinhaProjetoRecente;

class MainComponent : public juce::Component {
public:
    MainComponent();
    ~MainComponent() override;

    void abrirProjeto(std::unique_ptr<matriz::model::Project> projeto);
    void fecharProjeto();

    // Move o projeto atualmente aberto (se houver) pra fora deste
    // MainComponent — usado só pela troca de idioma em Preferences, que
    // destrói e recria o MainComponent inteiro pra retraduzir toda a UI, e
    // precisa entregar o projeto aberto pro substituto. nullptr se nenhum
    // projeto estava aberto. Nunca chamar com ingest em andamento.
    std::unique_ptr<matriz::model::Project> destacarProjeto();
    bool temProjetoAberto() const { return projetoAberto_ != nullptr; }
    ProjetoAberto* projetoAberto() { return projetoAberto_.get(); }

    // Introspecção pra teste (Parte 1 — painel de inconsistências fixo no
    // Catalog, §1.3/§3.5): definido em .cpp porque PainelInconsistenciasComponent
    // só está forward-declarado aqui.
    bool temPainelInconsistencias() const;
    int totalInconsistencias() const;

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

    // Tela inicial (nenhum projeto aberto) — Parte 1 da correção de fluxo:
    // a primeira decisão é o modo (dois cartões grandes, §1.1), nunca uma
    // configuração escondida atrás de um botão genérico "Novo projeto".
    std::unique_ptr<juce::Label> telaInicialSubtitulo_;
    std::unique_ptr<CartaoModo> telaInicialCartaoArchive_;
    std::unique_ptr<CartaoModo> telaInicialCartaoCatalog_;
    std::unique_ptr<juce::TextButton> telaInicialBotaoAbrir_;
    std::unique_ptr<juce::Label> telaInicialRecentesTitulo_;
    std::vector<std::unique_ptr<LinhaProjetoRecente>> telaInicialLinhasRecentes_;

    // Layout de projeto aberto
    std::unique_ptr<juce::Viewport> mosaicoViewport_;
    std::unique_ptr<MosaicoComponent> mosaico_;
    // Área central: visualizador (vídeo/imagem) é B.1.4+, ainda um
    // placeholder vazio no Archive. No Catalog, essa mesma área tem posição
    // fixa reservada pro painel de inconsistências (§1.3/§3.5 — "nunca
    // escondido em menu") — só um dos dois existe por vez.
    std::unique_ptr<juce::Component> visualizadorPlaceholder_;
    std::unique_ptr<juce::Viewport> painelInconsistenciasViewport_;
    std::unique_ptr<PainelInconsistenciasComponent> painelInconsistencias_;
    std::unique_ptr<FichaPanelComponent> fichaPanel_;

    // Ingest em background (corrige o travamento reportado: cópia + checksum
    // + ffprobe/Exiv2 nunca rodam na thread de mensagens).
    juce::ThreadPool ingestPool_{1};
    std::unique_ptr<juce::Label> labelProgressoIngest_;
    std::atomic<int> ingestsPendentes_{0};
    std::atomic<int> ingestsTotalLote_{0};
    std::atomic<int> ingestsErrosLote_{0};

public:
    // Modo já decidido (o cartão clicado) — o diálogo de novo projeto só
    // pergunta o que é comum aos dois modos (nome, pasta...).
    std::function<void(matriz::model::Modo)> aoPedirNovoProjeto;
    std::function<void()> aoPedirAbrirProjeto;

    // Abre diretamente uma pasta de projeto já conhecida (clique num item
    // da lista de recentes), sem passar pelo FileChooser.
    std::function<void(juce::File)> aoAbrirRecente;

    // Se definido, substitui o AlertWindow padrão de resumo ao final de um
    // lote de ingest — único jeito de rodar o fluxo real em
    // --selftest-ingerir-arquivos, que não tem humano pra clicar OK num
    // modal (AlertWindow::showAsync com callback nulo roda um loop modal
    // síncrono nesta versão do JUCE, e travaria o self-test pra sempre).
    // Em produção fica vazio; o alerta normal continua aparecendo.
    std::function<void(int sucessos, juce::StringArray erros)> aoConcluirLoteIngestParaTeste;
};

} // namespace matriz::ui
