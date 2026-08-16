#pragma once

#include <JuceHeader.h>
#include <string>

namespace matriz::ui {

struct EventoItemAlterado {
    std::string itemId;
    std::string tipoAlteracao; // "classificacao", "campo", "arquivo"
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

private:
    EventBus() = default;
    juce::ListenerList<EventBusListener> listeners_;
};

} // namespace matriz::ui
