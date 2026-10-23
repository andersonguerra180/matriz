#pragma once

#include <JuceHeader.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "OverlayComponent.h"
#include "PainelReceberCelular.h"
#include "Tablet.h"
#include "../Celular/DispositivoAndroidUsb.h"
#include "../Celular/RecebimentoCelular.h"

// PHONE DUMP > ANDROID USB: um "mini Matriz" do celular plugado por cabo, no lugar do Android File Transfer.
//   esquerda : aparelho, memória, atalhos (Câmera, WhatsApp, Screenshots, Downloads) e as subpastas da pasta aberta
//   centro   : mosaico da pasta (miniaturas que o próprio Android gera), tamanho regulável, filtros como no Matriz
//   direita  : metadados simples (nome, tipo, tamanho, data, dimensões, câmera, GEO com OPEN MAP)
//   I        : marca/desmarca a seleção (selo preto "I", no desenho dos selos H/K/P/W)
//   espaço / duplo clique : preview (o arquivo vem para um cache temporário); ← → passam, Esc fecha
//   IMPORTAR : copia os marcados para uma pasta de sessão em "Received from phone" e devolve essa pasta ao Intake,
//              igual ao QR code da GALLERY (container gallery, Phone Dump).

namespace matriz::ui {

class VideoPlayerComponent;

class PainelAndroidUsb : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    // `existentes` (opcional) roda numa thread de fundo: (nome, tamanho) do que o projeto já tem.
    PainelAndroidUsb(std::function<matriz::celular::ConjuntoExistentes()> existentes, std::shared_ptr<RespostaReceberCelular> resposta,
                     bool comVoltar);
    ~PainelAndroidUsb() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool tratarTecla(const juce::KeyPress&);

    class Grade;
    class Preview;

private:
    friend class Grade;
    friend class Preview;
    enum class Estado { Conectando, Pronto, Erro, Copiando };
    enum class Filtro { Todos, Imagem, Video, Audio, Documento, Marcados };

    // ListBoxModel (subpastas da pasta aberta)
    int getNumRows() override { return static_cast<int>(pastas_.size()); }
    void paintListBoxItem(int linha, juce::Graphics&, int largura, int altura, bool selecionada) override;
    void listBoxItemClicked(int linha, const juce::MouseEvent&) override;
    void timerCallback() override;  // metadados do item selecionado, com atraso (não baixa a cada seta)

    void conectar();
    void resolverAtalhos();
    void irPara(std::vector<std::pair<std::uint32_t, juce::String>> cadeia);
    void abrirPasta(std::uint32_t pasta, const juce::String& nome);
    void subir();
    void recarregarPasta();
    void aplicarFiltro();
    void pedirMiniaturasVisiveis();
    void selecionar(int indiceVisivel, bool alternar, bool estender);
    void moverCursor(int delta, bool estender);
    void alternarMarcaDaSelecao();
    void abrirPreview(int indiceVisivel);
    void fecharPreview();
    void lerMetadadosDoSelecionado();
    void baixarParaCache(const matriz::celular::EntradaAndroid& e, std::function<void(juce::File)> aoTerminar);
    void importar();
    void terminar(bool comIngestao, int recebidos);
    void atualizarTudo();
    bool jaNoProjeto(const matriz::celular::EntradaAndroid& e) const;
    bool estaMarcado(const matriz::celular::EntradaAndroid& e) const { return marcados_.count(chaveDe(e)) > 0; }
    std::uint32_t memoriaAtual() const;
    const matriz::celular::EntradaAndroid* itemVisivel(int i) const;
    juce::File arquivoNoCache(const matriz::celular::EntradaAndroid& e) const;

    using Chave = std::pair<std::uint32_t, std::uint32_t>;  // (memória, id)
    static Chave chaveDe(const matriz::celular::EntradaAndroid& e) { return {e.memoria, e.id}; }

    struct Metadados {
        bool lido = false;
        std::optional<int> largura, altura;
        std::optional<double> duracao;
        juce::String camera, dataOriginal;
        std::optional<double> lat, lon;
    };

    std::unique_ptr<matriz::celular::SessaoAndroidUsb> sessao_;
    std::shared_ptr<RespostaReceberCelular> resposta_;
    matriz::celular::AparelhoAndroid aparelho_;
    Estado estado_ = Estado::Conectando;
    juce::String erro_, aviso_;

    struct Nivel { std::uint32_t id; juce::String nome; };
    std::vector<Nivel> caminho_;                                   // da raiz da memória até a pasta aberta
    std::vector<matriz::celular::EntradaAndroid> pastas_, arquivos_;
    std::vector<int> visiveis_;                                    // índices em arquivos_ depois do filtro
    std::map<Chave, matriz::celular::EntradaAndroid> marcados_;
    std::set<int> selecao_;                                        // índices em visiveis_
    int cursor_ = -1, ancora_ = -1;
    matriz::celular::ConjuntoExistentes existentes_;
    std::map<std::uint32_t, juce::Image> miniaturas_;
    std::set<std::uint32_t> semMiniatura_;
    std::map<std::uint32_t, Metadados> metadados_;
    std::map<juce::String, juce::Image> logos_;                    // por extensão
    std::vector<std::pair<juce::String, std::vector<juce::String>>> atalhosPedidos_;
    struct Atalho { juce::String rotulo; std::vector<std::pair<std::uint32_t, juce::String>> cadeia; std::unique_ptr<BotaoPilula> botao; };
    std::vector<Atalho> atalhos_;
    Filtro filtro_ = Filtro::Todos;
    int anoFiltro_ = 0;
    bool listando_ = false;
    int geracaoLista_ = 0;
    double progresso_ = 0.0;
    int ladoTile_ = 150;

    juce::ComboBox comboMemoria_, comboAno_;
    juce::Label lblAparelho_, lblCaminho_, lblStatus_, lblRodape_, lblPastas_;
    juce::ListBox listaPastas_{"cel.usb.pastas", this};
    juce::Slider sliderTamanho_;
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<Grade> grade_;
    std::unique_ptr<Preview> preview_;
    juce::ProgressBar barra_{progresso_};
    std::vector<std::unique_ptr<ChipFiltro>> chips_;
    std::unique_ptr<BotaoPilula> btnSubir_, btnTentar_, btnImportar_, btnFechar_, btnVoltar_, btnMapa_, btnMarcar_;
    juce::Rectangle<int> rcEsquerda_, rcCentro_, rcInfo_, rcInfoTexto_, rcInfoImagem_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PainelAndroidUsb)
};

// Mostra o painel no overlay. `aoFechar` roda UMA vez (message thread, overlay já fechado). `voltou` = o usuário
// pediu VOLTAR (só existe quando `comVoltar`).
void mostrarAndroidUsb(PainelOverlay& overlay, std::function<matriz::celular::ConjuntoExistentes()> existentes,
                       std::function<void(RespostaReceberCelular, bool voltou)> aoFechar, bool comVoltar);

} // namespace matriz::ui
