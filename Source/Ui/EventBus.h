#pragma once

#include <JuceHeader.h>
#include <string>
#include <vector>

namespace matriz::ui {

struct EventoItemAlterado {
    std::string itemId;
    std::string tipoAlteracao; // "classificacao", "campo", "arquivo"; "recarregar_tudo" (itemId vazio) = lote grande
    // Evento de LOTE (renomear, tipo, marcações H/K/P/W em N itens): UM evento no
    // fim da operação, com itemId vazio e os ids afetados aqui. Listener que não
    // conhece `itemIds` enxerga itemId vazio = mudança ampla (comportamento de
    // sempre, ex.: "metadado" em lote); quem conhece trata só esses ids, numa passada.
    std::vector<std::string> itemIds;
};

class EventBusListener {
public:
    virtual ~EventBusListener() = default;
    virtual void aoItemAlterado(const EventoItemAlterado& e) = 0;
};

class EventBus {
public:
    static EventBus& obterInstancia() {
        static EventBus instancia;
        return instancia;
    }

    void registrarListener(EventBusListener* listener) {
        listeners_.add(listener);
    }

    void removerListener(EventBusListener* listener) {
        listeners_.remove(listener);
    }

    void dispararItemAlterado(const std::string& itemId, const std::string& tipoAlteracao) {
        EventoItemAlterado e{itemId, tipoAlteracao};
        listeners_.call([&e](EventBusListener& listener) {
            listener.aoItemAlterado(e);
        });
    }

    // Um item só vira o evento por item de sempre (listeners têm o caminho rápido
    // dele); dois ou mais viram UM evento de lote. Lista vazia não dispara nada.
    void dispararItensAlterados(const std::vector<std::string>& itemIds, const std::string& tipoAlteracao) {
        if (itemIds.empty()) return;
        if (itemIds.size() == 1) { dispararItemAlterado(itemIds.front(), tipoAlteracao); return; }
        EventoItemAlterado e{std::string(), tipoAlteracao, itemIds};
        listeners_.call([&e](EventBusListener& listener) {
            listener.aoItemAlterado(e);
        });
    }

private:
    EventBus() = default;
    juce::ListenerList<EventBusListener> listeners_;
};

} // namespace matriz::ui
