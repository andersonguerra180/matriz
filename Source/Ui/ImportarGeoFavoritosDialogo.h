#pragma once

#include <JuceHeader.h>

namespace matriz::ui {

class ProjetoAberto;

// TOOLS › "Import Geo Favorites from Collection…": escolhe OUTRA coleção do Matriz (aberta somente-leitura, nunca
// alterada), lista os favoritos de GEO LOCATION dela com checkbox (todos marcados) e importa os marcados para a
// coleção atual. Favorito com o mesmo nome (sem diferenciar maiúsculas) não duplica: pula e informa.
class ImportarGeoFavoritosDialogo {
public:
    static void iniciar(ProjetoAberto& projetoAtual, juce::Component* pai);
};

} // namespace matriz::ui
