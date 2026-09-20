#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

#include "../Model/Project.h"
#include "../Sync/SyncEngine.h"
#include "Tokens.h"

namespace matriz::ui {

class SyncDestinationDialog : public juce::Component,
                              public juce::ListBoxModel {
public:
    enum class Fase {
        Selecao,
        Escaneando,
        Revisao,
        Aplicando,
        Concluido
    };

    explicit SyncDestinationDialog(matriz::model::Project& projeto, std::function<void()> aoFechar);
    ~SyncDestinationDialog() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    void closeDialog();

    static void abrirModal(matriz::model::Project& projeto, std::function<void()> aoConcluir = nullptr);

private:
    struct DestinoInfoUI {
        std::string id;
        std::string destinationId;
        juce::String rotulo;
        juce::String caminho;
        std::string papel;
        bool online = false;
        int64_t revisao = 0;
        juce::String ultimaEdicao;
        juce::int64 tamanhoBytes = -1;
        bool calculandoTamanho = false;
        bool temMarcadorIncompleto = false;
    };

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    juce::String getTooltipForRow(int rowNumber) override;

    void carregarDestinos();
    void atualizarValidacaoSelecao();
    void iniciarEscaneamento();
    void aplicarSincronizacao();
    void localizarDestino(int idx);
    void adicionarDestinoExistente();
    void calcularTamanhosBackground();

    matriz::model::Project& projeto_;
    std::function<void()> aoFechar_;
    Fase fase_ = Fase::Selecao;

    std::vector<DestinoInfoUI> destinos_;
    int selectedRefIdx_ = -1;
    int selectedAlvoIdx_ = -1;

    matriz::sync::PlanoSync planoAtual_;
    matriz::sync::ResultadoSync resultadoAtual_;
    matriz::app::CancelamentoPtr cancelamento_;

    double progressoValor_ = 0.0;
    juce::String progressoTexto_;

    // UI Widgets - Selecao
    std::unique_ptr<juce::Label> labelTitulo_;
    std::unique_ptr<juce::ListBox> listDestinos_;
    std::unique_ptr<juce::TextButton> btnLocalizarDestino_;
    std::unique_ptr<juce::TextButton> btnAdicionarExistente_;

    std::unique_ptr<juce::Label> labelRef_;
    std::unique_ptr<juce::ComboBox> comboRef_;
    std::unique_ptr<juce::Label> labelAlvo_;
    std::unique_ptr<juce::ComboBox> comboAlvo_;

    std::unique_ptr<juce::Label> labelAvisoTempo_;
    std::unique_ptr<juce::ToggleButton> toggleConfirmarReferenciaAntiga_;
    std::unique_ptr<juce::Label> labelErroValidacao_;

    std::unique_ptr<juce::TextButton> btnEscanear_;
    std::unique_ptr<juce::TextButton> btnCancelarGeral_;

    // UI Widgets - Escaneamento / Aplicando
    std::unique_ptr<juce::ProgressBar> barraProgresso_;
    std::unique_ptr<juce::Label> labelProgressoMensagem_;
    std::unique_ptr<juce::TextButton> btnCancelarOperacao_;

    // UI Widgets - Revisao
    class ListaRevisaoSync;
    std::unique_ptr<juce::Label> labelResumoRevisao_;
    std::unique_ptr<juce::ComboBox> comboFiltroClasse_;
    std::unique_ptr<juce::Viewport> viewportRevisao_;
    std::unique_ptr<ListaRevisaoSync> listaRevisao_;
    std::unique_ptr<juce::TextButton> btnAplicarSync_;
    std::unique_ptr<juce::TextButton> btnVoltarSelecao_;

    // UI Widgets - Concluido
    std::unique_ptr<juce::Label> labelResumoConcluido_;
    std::unique_ptr<juce::TextButton> btnAbrirLixeira_;
    std::unique_ptr<juce::TextButton> btnConcluirFinal_;
};

} // namespace matriz::ui
