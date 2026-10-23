#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "OverlayComponent.h"
#include "Tablet.h"
#include "../Celular/ServidorCelular.h"

// "RECEBER DO CELULAR": painel (PainelOverlay) com instruções grandes, QR code, endereço em texto,
// seletor de IP, progresso ao vivo e CONCLUIR. O servidor só existe enquanto o painel está aberto.
// CONCLUIR devolve a pasta desta sessão de recebimento para entrar no fluxo como se tivesse sido
// solta na área de drop (abre o questionário com "Backup do celular" marcado).

namespace matriz::ui {

// De onde vêm os arquivos no celular: GALLERY (fotos/vídeos da galeria) ou WHATSAPP (conversa exportada, .zip).
// Os dois usam o mesmo servidor local; muda o passo a passo mostrado. ANDROID USB = cabo, sem rede (PainelAndroidUsb).
enum class ModoImportacaoCelular { Galeria, WhatsApp, AndroidUsb };

struct RespostaReceberCelular {
    bool concluiu = false;          // CONCLUIR (false = FECHAR/Esc)
    int recebidos = 0;
    juce::File pastaSessao;         // vazia/inexistente se nada chegou
};

class PainelReceberCelular : public juce::Component, private juce::Timer {
public:
    PainelReceberCelular(matriz::celular::OpcoesServidor opcoes, std::shared_ptr<RespostaReceberCelular> resposta,
                         ModoImportacaoCelular modo = ModoImportacaoCelular::Galeria);
    ~PainelReceberCelular() override;

    bool servidorAtivo() const { return servidor_.ativo(); }
    int porta() const { return servidor_.porta(); }
    juce::String url() const { return url_; }
    const juce::String& erroInicio() const { return erro_; }
    matriz::celular::ServidorCelular& servidor() { return servidor_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    bool tratarTecla(const juce::KeyPress&);

    void concluir(bool comIngestao);
    // Para o self-test: avança "o relógio" para a dica de firewall.
    void forcarDicaFirewallParaTeste() { forcarDica_ = true; atualizar(); }
    juce::String textoProgressoParaTeste() const { return lblProgresso_.getText(); }

private:
    void timerCallback() override { atualizar(); }
    void atualizar();
    void montarEndereco();
    juce::String dicaFirewall(bool destacada) const;

    matriz::celular::ServidorCelular servidor_;
    std::shared_ptr<RespostaReceberCelular> resposta_;
    ModoImportacaoCelular modo_ = ModoImportacaoCelular::Galeria;
    std::vector<std::string> ips_;
    juce::String url_, erro_;
    juce::Image qr_;
    bool forcarDica_ = false;
    bool dicaDestacada_ = false;
    double progresso_ = 0.0;

    juce::ComboBox comboIp_;
    juce::Label lblUrl_, lblArquivo_, lblProgresso_, lblDica_, lblSemIp_;
    juce::ProgressBar barra_{progresso_};
    std::unique_ptr<BotaoPilula> btnConcluir_, btnFechar_;
    juce::Rectangle<int> rcQr_, rcInstrucoes_, rcCartaoQr_, rcCartaoInfo_, rcCartaoProgresso_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PainelReceberCelular)
};

// Mostra o painel no overlay. `aoFechar` roda UMA vez (message thread, overlay já fechado).
// Devolve o painel para o chamador/self-test.
std::shared_ptr<PainelReceberCelular> mostrarReceberDoCelular(PainelOverlay& overlay, matriz::celular::OpcoesServidor opcoes,
                                                              std::function<void(RespostaReceberCelular)> aoFechar,
                                                              ModoImportacaoCelular modo = ModoImportacaoCelular::Galeria);

// Card "Importar do celular": dois ícones grandes (GALLERY e WHATSAPP). `aoConcluir` roda UMA vez com a escolha
// (nullopt = cancelou). Vive dentro de um PainelOverlay.
// `sistema` = true: segunda tela (depois de WHATSAPP), com os cards ANDROID e IPHONE no lugar de GALLERY/WHATSAPP.
// `comUsb` = true (só na primeira tela): terceiro card ANDROID USB.
class EscolhaImportacaoCelular : public juce::Component {
public:
    explicit EscolhaImportacaoCelular(bool sistema = false, bool comUsb = false);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool tratarTecla(const juce::KeyPress&);
    // Para o self-test
    void escolherParaTeste(ModoImportacaoCelular m) { escolher(m); }

private:
    void escolher(ModoImportacaoCelular m);
    int tileEm(juce::Point<int> p) const;
    int totalTiles() const { return comUsb_ ? 3 : 2; }
    juce::Rectangle<int> tiles_[3];
    int sobre_ = -1;
    juce::Image iconeGaleria_, iconeWhatsapp_, iconeAndroid_, iconeApple_;
    bool sistema_ = false;
    bool comUsb_ = false;
};

// Segunda tela do WHATSAPP: ANDROID segue o fluxo do WhatsApp de hoje; IPHONE abre o mesmo QR code da GALLERY
// (o iPhone exporta o .zip da conversa e o envia pelo QR como se fosse uma foto).
enum class SistemaCelular { Android, IPhone };
// `aoVoltar` (opcional): ganha o botão VOLTAR, que chama este callback em vez de concluir.
void mostrarEscolhaSistemaCelular(PainelOverlay& overlay, std::function<void(std::optional<SistemaCelular>)> aoConcluir,
                                  std::function<void()> aoVoltar = nullptr);

void mostrarEscolhaImportacaoCelular(PainelOverlay& overlay, std::function<void(std::optional<ModoImportacaoCelular>)> aoConcluir,
                                     std::weak_ptr<EscolhaImportacaoCelular>* devolver = nullptr,
                                     std::function<void()> aoVoltar = nullptr, bool comUsb = false);

} // namespace matriz::ui
