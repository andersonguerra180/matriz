#include "Tokens.h"
#include "../App/Preferencias.h"

namespace matriz::ui {

namespace {
    enum class TemaAtivo { Dark, Light };
    TemaAtivo temaAtivo_ = TemaAtivo::Dark;
    float escalaFonte_ = 1.0f;
    bool inicializado_ = false;

    void inicializarSeNecessario() {
        if (inicializado_) return;
        inicializado_ = true;
        auto pref = matriz::app::lerTema();
        temaAtivo_ = (pref == "light") ? TemaAtivo::Light : TemaAtivo::Dark;
        escalaFonte_ = matriz::app::lerEscalaFonte();
    }
}

const Tema& tema() {
    inicializarSeNecessario();
    return temaAtivo_ == TemaAtivo::Light ? temaBkrLight() : temaBkrDark();
}

void recarregarTema() {
    inicializado_ = false;
}

float escalaFonte() {
    inicializarSeNecessario();
    return escalaFonte_;
}

} // namespace matriz::ui
