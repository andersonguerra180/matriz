#include "ProgressoGlobal.h"

namespace matriz::ui {

ProgressoGlobal& ProgressoGlobal::obterInstancia() {
    static ProgressoGlobal instancia;
    return instancia;
}

void ProgressoGlobal::adicionarListener(ProgressoGlobalListener* listener) {
    if (!listener) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void ProgressoGlobal::removerListener(ProgressoGlobalListener* listener) {
    if (!listener) return;
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void ProgressoGlobal::iniciarTarefa(const juce::String& id,
                                    const juce::String& titulo,
                                    int totalItens,
                                    std::function<void()> aoCancelar,
                                    const juce::String& detalheInicial) {
    EstadoProgresso estado;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        estadoAtual_.id = id;
        estadoAtual_.titulo = titulo;
        estadoAtual_.detalhe = detalheInicial;
        estadoAtual_.totalItens = totalItens;
        estadoAtual_.itensConcluidos = 0;
        estadoAtual_.fracao = (totalItens > 0) ? 0.0 : -1.0;
        estadoAtual_.ativo = true;
        estadoAtual_.cancelavel = (aoCancelar != nullptr);
        estadoAtual_.aoCancelar = std::move(aoCancelar);
        estadoAtual_.mensagemConclusao = "";
        estado = estadoAtual_;
    }
    notificarListeners(estado);
}

void ProgressoGlobal::atualizarProgresso(const juce::String& id,
                                         int itensConcluidos,
                                         const juce::String& detalhe) {
    EstadoProgresso estado;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (estadoAtual_.id != id && estadoAtual_.ativo) return;
        estadoAtual_.itensConcluidos = itensConcluidos;
        if (estadoAtual_.totalItens > 0) {
            estadoAtual_.fracao = juce::jlimit(0.0, 1.0, static_cast<double>(itensConcluidos) / estadoAtual_.totalItens);
        }
        if (detalhe.isNotEmpty()) {
            estadoAtual_.detalhe = detalhe;
        }
        estado = estadoAtual_;
    }
    notificarListeners(estado);
}

void ProgressoGlobal::atualizarFracao(const juce::String& id,
                                      double fracao,
                                      const juce::String& detalhe) {
    EstadoProgresso estado;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (estadoAtual_.id != id && estadoAtual_.ativo) return;
        estadoAtual_.fracao = juce::jlimit(0.0, 1.0, fracao);
        if (detalhe.isNotEmpty()) {
            estadoAtual_.detalhe = detalhe;
        }
        estado = estadoAtual_;
    }
    notificarListeners(estado);
}

void ProgressoGlobal::atualizarDetalhe(const juce::String& id,
                                       const juce::String& detalhe) {
    EstadoProgresso estado;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (estadoAtual_.id != id && estadoAtual_.ativo) return;
        estadoAtual_.detalhe = detalhe;
        estado = estadoAtual_;
    }
    notificarListeners(estado);
}

void ProgressoGlobal::concluirTarefa(const juce::String& id,
                                     const juce::String& mensagemFinal) {
    EstadoProgresso estado;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (estadoAtual_.id != id && estadoAtual_.ativo) return;
        estadoAtual_.ativo = false;
        estadoAtual_.fracao = 1.0;
        estadoAtual_.itensConcluidos = estadoAtual_.totalItens;
        estadoAtual_.mensagemConclusao = mensagemFinal;
        estadoAtual_.horarioConclusao = juce::Time::getCurrentTime();
        estadoAtual_.aoCancelar = nullptr;
        estadoAtual_.cancelavel = false;
        estado = estadoAtual_;
    }
    notificarListeners(estado);
}

void ProgressoGlobal::cancelarTarefa(const juce::String& id) {
    std::function<void()> callbackCancelar;
    EstadoProgresso estado;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (estadoAtual_.id != id && estadoAtual_.ativo) return;
        callbackCancelar = estadoAtual_.aoCancelar;
        estadoAtual_.ativo = false;
        estadoAtual_.mensagemConclusao = "Cancelled";
        estadoAtual_.horarioConclusao = juce::Time::getCurrentTime();
        estadoAtual_.aoCancelar = nullptr;
        estadoAtual_.cancelavel = false;
        estado = estadoAtual_;
    }

    if (callbackCancelar) {
        callbackCancelar();
    }
    notificarListeners(estado);
}

EstadoProgresso ProgressoGlobal::obterEstado() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return estadoAtual_;
}

bool ProgressoGlobal::estaExecutando() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return estadoAtual_.ativo;
}

void ProgressoGlobal::notificarListeners(const EstadoProgresso& estado) {
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (!mm) return;

    if (mm->isThisTheMessageThread()) {
        std::vector<ProgressoGlobalListener*> lista;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lista = listeners_;
        }
        for (auto* l : lista) {
            if (l) l->aoProgressoAtualizado(estado);
        }
    } else {
        juce::MessageManager::callAsync([this, estado] {
            std::vector<ProgressoGlobalListener*> lista;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lista = listeners_;
            }
            for (auto* l : lista) {
                if (l) l->aoProgressoAtualizado(estado);
            }
        });
    }
}

} // namespace matriz::ui
