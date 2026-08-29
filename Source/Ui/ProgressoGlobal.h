#pragma once

#include <JuceHeader.h>
#include <functional>
#include <mutex>
#include <vector>

namespace matriz::ui {

struct EstadoProgresso {
    juce::String id;
    juce::String titulo;
    juce::String detalhe;
    int totalItens = 0;
    int itensConcluidos = 0;
    double fracao = 0.0; // 0.0 a 1.0
    bool ativo = false;
    bool cancelavel = false;
    std::function<void()> aoCancelar;
    juce::String mensagemConclusao;
    juce::Time horarioConclusao;
};

class ProgressoGlobalListener {
public:
    virtual ~ProgressoGlobalListener() = default;
    virtual void aoProgressoAtualizado(const EstadoProgresso& estado) = 0;
};

class ProgressoGlobal {
public:
    static ProgressoGlobal& obterInstancia();

    void adicionarListener(ProgressoGlobalListener* listener);
    void removerListener(ProgressoGlobalListener* listener);

    void iniciarTarefa(const juce::String& id,
                       const juce::String& titulo,
                       int totalItens = 0,
                       std::function<void()> aoCancelar = nullptr,
                       const juce::String& detalheInicial = "");

    void atualizarProgresso(const juce::String& id,
                            int itensConcluidos,
                            const juce::String& detalhe = "");

    void atualizarFracao(const juce::String& id,
                         double fracao,
                         const juce::String& detalhe = "");

    void atualizarDetalhe(const juce::String& id,
                          const juce::String& detalhe);

    void concluirTarefa(const juce::String& id,
                        const juce::String& mensagemFinal = "");

    void cancelarTarefa(const juce::String& id);

    EstadoProgresso obterEstado() const;
    bool estaExecutando() const;

private:
    ProgressoGlobal() = default;
    ~ProgressoGlobal() = default;

    void notificarListeners(const EstadoProgresso& estado);

    mutable std::mutex mutex_;
    EstadoProgresso estadoAtual_;
    std::vector<ProgressoGlobalListener*> listeners_;
};

} // namespace matriz::ui
