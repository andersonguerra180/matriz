#pragma once

#include <JuceHeader.h>

#include <memory>
#include <string>
#include <vector>

#include "../Audio/MotorReproducao.h"
#include "AssetWorkspace.h"

// Estação de Escuta (§7) — o workspace de áudio.
//
// Duas fontes de dado, deliberadamente separadas:
//
//   - O que veio do CACHE (registro): forma de onda, LUFS-I, LRA, true peak,
//     correlação média, marcadores. Tudo isso aparece com o Vault
//     desconectado, instantaneamente, porque foi calculado na ingestão (I2)
//     e guardado como BLOB no registro (I3).
//   - O que vem do ÁUDIO TOCANDO: VU/pico, correlação instantânea,
//     vetorscópio, espectrograma. Só existe com o arquivo acessível, e é
//     lido de variáveis atômicas por um Timer de 30 Hz — o audio callback
//     nunca chama a interface.
//
// É essa separação que faz a tela continuar útil sem o disco: sem ela, um
// Vault offline deixaria o painel inteiro vazio em vez de só o transporte.

namespace matriz::ui {

class VisualizadorFormaOnda;
class MedidoresComponent;

class AudioWorkspace : public AssetWorkspace, private juce::Timer {
public:
    explicit AudioWorkspace(ProjetoAberto& projeto);
    ~AudioWorkspace() override;

    void carregarAsset(const ItemResumo& snap, std::optional<juce::File> arquivo,
                       std::function<void()> aoConcluir) override;
    void descarregar() override;

    std::function<void()> aoFechar;
    std::function<void()> aoMudarMarcadores;
    std::function<void(int direcao)> aoNavegar;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    // Introspecção pra teste — o harness dirige a estação sem mouse nem
    // placa de som.
    bool transporteHabilitadoParaTeste() const { return transporteHabilitado_; }
    bool temFormaOndaParaTeste() const;
    juce::String rodapeParaTeste() const { return textoRodape_; }
    double velocidadeParaTeste() const { return motor_.velocidadeAtual(); }
    bool loopAtivoParaTeste() const { return motor_.loopAtivo(); }
    int totalMarcadoresParaTeste() const;
    // Arrasta a borda de um trecho na régua sem mouse real (§7).
    void editarBordaDeMarcadorParaTeste(int indiceMarcador, bool inicio, double segundos);
    std::optional<double> fimDoMarcadorParaTeste(int indiceMarcador) const;
    void simularTeclaParaTeste(int codigoTecla);

    static constexpr int kAlturaTransporte = 34;
    static constexpr int kAlturaRegua = 22;
    static constexpr int kAlturaMedidores = 120;
    static constexpr int kAlturaRodape = 22;

private:
    void timerCallback() override;
    void reconstruirRodape();
    void recarregarMarcadores();
    void sincronizarMarcadoresParaNotas();
    void adicionarMarcadorNaPosicaoAtual();
    void abrirEditorInlineMarcador(const std::string& observacaoId, double posSegundos);
    void fecharEditorInlineMarcador(bool gravar);
    void abrirDispositivoDeAudio();

    ProjetoAberto& projeto_;
    matriz::audio::MotorReproducao motor_;
    juce::AudioDeviceManager dispositivos_;
    bool dispositivoAberto_ = false;

    std::string itemId_;
    std::string arquivoId_;
    bool transporteHabilitado_ = false;
    juce::String textoRodape_;
    juce::String avisoVault_;

    std::unique_ptr<VisualizadorFormaOnda> forma_;
    std::unique_ptr<MedidoresComponent> medidores_;
    std::unique_ptr<juce::TextEditor> editorInlineMarcador_;
    std::string observacaoIdEditando_;
    double posEditando_ = 0.0;

    std::unique_ptr<juce::TextButton> botaoTocar_, botaoParar_, botaoLoopIn_, botaoLoopOut_, botaoLimparLoop_,
        botaoFechar_, botaoAdicionarMarcador_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioWorkspace)
};

} // namespace matriz::ui
