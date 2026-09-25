#pragma once

#include <JuceHeader.h>
#include "ProgressoGlobal.h"

namespace matriz::ui {

// Conteúdo visual do modal genérico de progresso — grande, centralizado,
// com título/detalhe/porcentagem/barra e um botão CANCEL quando a tarefa
// permite. É injetado dentro de uma juce::DialogWindow verdadeira (ver
// TarefaGlobalModalWatcher), então bloqueia o resto do app enquanto está
// na tela — exatamente o "trava o app, faz o trabalho, informa passo a
// passo" pedido, em vez da barrinha discreta que já existia
// (BarraProgressoGlobalComponent, que continua existindo pra quem já a usa).
class TarefaGlobalModalDialog : public juce::Component {
public:
    TarefaGlobalModalDialog();

    void definirEstado(const EstadoProgresso& estado);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void atualizarVisual();

    EstadoProgresso estado_;
    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblDetalhe_;
    std::unique_ptr<juce::Label> lblPorcentagem_;
    std::unique_ptr<juce::TextButton> btnCancelar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TarefaGlobalModalDialog)
};

// Fica vivo a partir de um ponto único no topo do app (ver MainWindow) e
// escuta ProgressoGlobal globalmente. Qualquer tarefa que fique ATIVA por
// mais de ~400ms (limiar propositalmente curto — o objetivo é nunca deixar
// o usuário se perguntando se travou) e não tenha seu próprio modal
// dedicado (estado.temModalProprio, ex.: ingest já usa
// IngestProgressModalDialog) ganha automaticamente este modal grande —
// sem precisar tocar em cada call site de ProgressoGlobal espalhado pelo
// app. Tarefas rápidas (abaixo do limiar) nunca chegam a mostrar nada,
// pra não piscar modal em toda recarga rotineira da grade.
class TarefaGlobalModalWatcher : private ProgressoGlobalListener, private juce::Timer {
public:
    TarefaGlobalModalWatcher();
    ~TarefaGlobalModalWatcher() override;

private:
    void aoProgressoAtualizado(const EstadoProgresso& estado) override;
    void timerCallback() override;
    void abrirModal(const EstadoProgresso& estado);
    void atualizarModal(const EstadoProgresso& estado);
    void fecharModal();

    juce::Component::SafePointer<juce::DialogWindow> janela_;
    TarefaGlobalModalDialog* conteudo_ = nullptr; // dono é a DialogWindow (options.content.setOwned)
    juce::String idPendente_;
    juce::String idMostrando_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TarefaGlobalModalWatcher)
};

} // namespace matriz::ui
