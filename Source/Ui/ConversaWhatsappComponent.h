#pragma once

#include <JuceHeader.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../ProvaData/ExportadorTrecho.h"
#include "../Whatsapp/WhatsappIngestao.h"
#include "OverlayComponent.h"
#include "ProjetoAberto.h"
#include "Tablet.h"

namespace matriz::ui {

// Tela da conversa do WhatsApp (Backup Explorer): aparece no lugar do preview do .zip da conversa.
//
//  - Cabeçalho: iniciais do contato, nome e período. Busca "Buscar nesta conversa…" com destaque, contagem e salto.
//  - Mensagens em balões (você à direita, em acento escuro; os outros à esquerda), separadores de dia, avisos do
//    sistema centralizados, hora em cada balão; fotos e vídeos com miniatura, álbum 2×2 com "+N", áudio com play
//    e forma de onda tocando o FLAC dentro do balão; mídia oculta, mensagem vazia e ligação perdida.
//  - Somente leitura: a tela é montada lendo o .txt de dentro do zip e as mídias do backup. Nada é gravado de volta.
//  - Lista virtualizada: só desenha o que está visível; miniaturas e formas de onda sob demanda, em background.
//
// IDs: wa.root, wa.header, wa.search, wa.count, wa.prev, wa.next
class ConversaWhatsappComponent : public juce::Component, private juce::Timer, private juce::ScrollBar::Listener {
public:
    ConversaWhatsappComponent(ProjetoAberto& projeto, const std::string& conversaItemId);
    ~ConversaWhatsappComponent() override;

    // Clicar numa foto/vídeo/álbum: quem hospeda abre o visualizador normal do Matriz com esta sequência
    // (um item = foto/vídeo único; vários = álbum, com ◀ ▶). `inicial` é o item clicado.
    std::function<void(const std::string& inicial, const std::vector<std::string>& sequencia)> aoAbrirMidia;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void lookAndFeelChanged() override;

    // ── Para o auto-teste ───────────────────────────────────────────────────────────────────
    bool carregadaParaTeste() const { return carregada_; }
    juce::String erroParaTeste() const { return erro_; }
    int mensagensParaTeste() const;
    bool medidasProntasParaTeste() const { return !ys_.empty(); }
    int linhasParaTeste() const { return static_cast<int>(linhas_.size()); }
    int linhasDesenhadasNoUltimoPaintParaTeste() const { return ultimasDesenhadas_; }
    void buscarParaTeste(const juce::String& texto);
    int resultadosDaBuscaParaTeste() const { return static_cast<int>(resultados_.size()); }
    float rolagemParaTeste() const { return rolagem_; }
    float alturaTotalParaTeste() const { return alturaTotal_; }
    int albunsParaTeste() const;
    juce::String vocePraTeste() const { return voce_; }
    bool perguntandoVoceParaTeste() const { return perguntarVoce_; }
    void escolherVoceParaTeste(const juce::String& autorOuVazio) { definirVoce(autorOuVazio); }
    void clicarMidiaParaTeste(int indiceDaLinha);
    // Print e exportação de trecho (o harness aciona sem mouse)
    juce::Image comporPrintParaTeste(juce::Rectangle<int> area) { return comporPrint(area); }
    void entrarSelecaoParaTeste() { entrarSelecao(); }
    void selecionarTudoParaTeste() { selecionarTudo(); }
    void selecionarIntervaloParaTeste(int linhaA, int linhaB) { selecionarIntervalo(linhaA, linhaB); }
    void selecionarPeriodoParaTeste(const juce::String& de, const juce::String& ate) { campoDe_.setText(de, false); campoAte_.setText(ate, false); selecionarPeriodo(); }
    int selecionadasParaTeste() const { return mensagensSelecionadas(); }
    bool emSelecaoParaTeste() const { return modoSelecao_; }
    // Exporta o trecho selecionado agora, em segundo plano; `aoTerminar` volta na message thread.
    void exportarTrechoParaTeste(const matriz::provadata::OpcoesTrecho& opcoes, bool usarRede, std::function<void(matriz::provadata::ResultadoTrecho)> aoTerminar) {
        executarExportacaoDeTrecho(opcoes, usarRede, std::move(aoTerminar));
    }

    enum class Tipo { Dia, Sistema, Texto, Foto, Video, Audio, Figurinha, Documento, Album, Oculta, Vazia, Apagada, LigVoz, LigVideo };

    struct Linha {
        Tipo tipo = Tipo::Texto;
        int iMsg = -1;        // primeira mensagem da linha
        int nMsgs = 1;        // álbum: quantas mensagens
        bool inicioSequencia = false;
        bool meu = false;
        bool ausente = false; // anexo que o backup não tem
        float largura = 0, altura = 0;
    };

private:
    struct Impl;
    struct Medida;
    struct Acerto;

    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar*, double novoInicio) override;

    void iniciarCarga();
    void aoCarregar(std::shared_ptr<const matriz::whatsapp::ConversaParseada> conversa, matriz::whatsapp::InfoConversa info,
                    std::vector<Linha> linhas, juce::String erro);
    void remedirEmBackground();
    void aplicarMedidas(uint64_t geracao, std::vector<Linha> linhas);
    void atualizarBarra();
    void definirBusca(const juce::String& texto);
    void irParaResultado(int indice);
    void rolarPara(float y);
    int linhaEm(float y) const;

    float larguraMaximaBalao() const;
    Medida medirLinha(const Linha& l, float larguraBalaoMax) const;
    const Medida& medidaEmCache(int indice);

    void desenharCabecalho(juce::Graphics& g, juce::Rectangle<int> area);
    void desenharLinha(juce::Graphics& g, int indice, float yTopo);
    void desenharTexto(juce::Graphics& g, const Medida& m, float x, float y, juce::Colour cor, int iMsg, const juce::Font& base);

    void pedirMiniatura(const std::string& midiaId, bool ehVideo);
    void pedirOnda(const std::string& midiaId);
    void alternarAudio(const std::string& midiaId);
    std::string idDaMidia(int iMsg) const;
    void abrirMidia(const std::string& inicial, std::vector<std::string> sequencia);

    // ── Print (B6) ──────────────────────────────────────────────────────────────────────────
    class SobreposicaoPrint;
    std::unique_ptr<juce::Component> sobreposicaoPrint_;
    std::unique_ptr<PainelOverlay> overlay_;  // diálogos locais (salvar print, opções do trecho, aviso de prova pendente)
    std::unique_ptr<BotaoPilula> btnPrint_, btnTrecho_;
    std::shared_ptr<juce::FileChooser> seletorPasta_;
    void iniciarPrint();
    void cancelarPrint();
    void aoSelecionarAreaDoPrint(juce::Rectangle<int> area);
    juce::Image comporPrint(juce::Rectangle<int> area);
    void mostrarSalvarPrint(juce::Image imagem);
    void fecharOverlay();
    void avisar(const juce::String& texto);

    // ── Exportar trecho (A6) ────────────────────────────────────────────────────────────────
    bool modoSelecao_ = false;
    int ancoraSel_ = -1, selIni_ = -1, selFim_ = -1;     // índices de linha (inclusive)
    juce::TextEditor campoDe_, campoAte_;
    juce::Label lblDe_, lblAte_, lblSelecionadas_, lblDicaSel_;
    std::unique_ptr<BotaoPilula> btnPeriodo_, btnTodas_, btnExportar_, btnSairSel_;
    juce::Rectangle<int> areaBarraSel_;
    void entrarSelecao();
    void sairSelecao();
    void selecionarIntervalo(int linhaA, int linhaB);
    void selecionarPeriodo();
    void selecionarTudo();
    void atualizarBarraSelecao();
    int mensagensSelecionadas() const;
    void clicarSelecao(const juce::MouseEvent& e);
    void desenharMarcaSelecao(juce::Graphics& g, int indice, float yTopo);
    void abrirOpcoesDoTrecho();
    void exportarComAviso(matriz::provadata::OpcoesTrecho opcoes);
    void executarExportacaoDeTrecho(matriz::provadata::OpcoesTrecho opcoes, bool usarRede, std::function<void(matriz::provadata::ResultadoTrecho)> aoTerminar);

    ProjetoAberto& projeto_;
    std::string conversaItemId_;

    std::unique_ptr<Impl> impl_;

    // dados
    std::shared_ptr<const matriz::whatsapp::ConversaParseada> conversa_;
    matriz::whatsapp::InfoConversa info_;
    std::map<juce::String, matriz::whatsapp::LinhaMidia> mapaMidias_;
    std::vector<Linha> linhas_;
    std::vector<float> ys_;       // y do topo de cada linha (prefixo das alturas)
    float alturaTotal_ = 0;
    juce::Font fonteTexto_;  // criada na message thread; a medição em background só a copia
    std::vector<int> mensagemParaLinha_;
    juce::String voce_;
    bool carregada_ = false;
    juce::String erro_;
    uint64_t geracaoMedida_ = 0;
    float larguraMedida_ = 0;
    bool remedirPendente_ = false;
    bool portugues_ = true;

    // rolagem
    juce::ScrollBar barra_{true};
    float rolagem_ = 0;

    // busca
    juce::TextEditor campoBusca_;
    juce::Label lblContagem_;
    std::unique_ptr<BotaoPilula> btnAnterior_, btnProximo_;
    std::unique_ptr<BotaoPilula> btnVoce_;                       // "Você: X ▾" (trocar quem é você)
    std::vector<std::unique_ptr<BotaoPilula>> botoesVoce_;       // "Quem é você nesta conversa?" (primeira vez que abre)
    bool perguntarVoce_ = false;
    juce::Rectangle<int> rcSeletorVoce_;
    void definirVoce(const juce::String& autorOuVazio);
    void montarSeletorDeVoce();
    void mostrarMenuDeVoce();
    juce::String consulta_;
    std::vector<int> resultados_;  // índice da linha de cada resultado
    int resultadoAtual_ = -1;

    int ultimasDesenhadas_ = 0;
    juce::Rectangle<int> areaCabecalho_, areaLista_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConversaWhatsappComponent)
};

} // namespace matriz::ui
