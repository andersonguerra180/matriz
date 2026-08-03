#pragma once

#include <JuceHeader.h>

// Design system por tokens, desde a primeira linha de UI (§11.6). Nenhum
// Component deve ter juce::Colour(0xff...) espalhado no código — tudo
// referencia matriz::ui::tema().<campo>.
//
// Só BKR Dark existe agora. System CRT é B.1.7 (depois do ponto de
// validação combinado) — a estrutura em Tema já é agnóstica de tema
// (tema() é a única função que muda), então adicionar o segundo tema depois
// não exige tocar em nenhum Component, só escrever temaSystemCrt() e trocar
// o que tema() devolve.

namespace matriz::ui {

struct Tema {
    // Superfícies
    juce::Colour fundo;
    juce::Colour painel;
    juce::Colour painelAlt;
    juce::Colour borda;
    juce::Colour bordaFoco;

    // Texto
    juce::Colour textoPrimario;
    juce::Colour textoSecundario;
    juce::Colour textoTerciario;
    juce::Colour textoSobreAcento;

    // Ação
    juce::Colour acento;
    juce::Colour acentoHover;
    juce::Colour perigo;
    juce::Colour alerta;

    // Estado do mosaico (§11.2)
    juce::Colour estadoNaoDigitalizado; // cinza
    juce::Colour estadoCapturado;       // azul
    juce::Colour estadoQcOk;            // verde
    juce::Colour estadoAlerta;          // âmbar
    juce::Colour haloSincronizado;      // anel, sobreposto a qualquer estado acima

    // Ficha — proveniência de valor (P3: sugestão de IA visualmente distinta de decisão humana)
    juce::Colour campoHumano;
    juce::Colour campoHerdado;
    juce::Colour campoLeituraTecnica;
    juce::Colour campoSugestaoIa;
    juce::Colour campoSugestaoIaFundo;

    // Tipografia
    float tamanhoFonteTitulo;
    float tamanhoFonteSubtitulo;
    float tamanhoFonteCorpo;
    float tamanhoFontePequena;

    // Espaçamento
    int espacoPequeno;
    int espacoMedio;
    int espacoGrande;
    int espacoPainel;

    // Forma
    float raioPequeno;
    float raioMedio;
};

// BKR Dark — padrão, referência Logic Pro dark (§11.6): cinza neutro escuro,
// acento azul frio, reconhecível sem cansar em turno longo.
inline const Tema& temaBkrDark() {
    static const Tema t = [] {
        Tema x;
        x.fundo = juce::Colour(0xff1a1a1c);
        x.painel = juce::Colour(0xff232326);
        x.painelAlt = juce::Colour(0xff1e1e20);
        x.borda = juce::Colour(0xff38383c);
        x.bordaFoco = juce::Colour(0xff5b9dff);

        x.textoPrimario = juce::Colour(0xffe8e8ea);
        x.textoSecundario = juce::Colour(0xff9a9aa0);
        x.textoTerciario = juce::Colour(0xff68686e);
        x.textoSobreAcento = juce::Colour(0xffffffff);

        x.acento = juce::Colour(0xff5b9dff);
        x.acentoHover = juce::Colour(0xff7bb0ff);
        x.perigo = juce::Colour(0xffff6b6b);
        x.alerta = juce::Colour(0xffffb84d);

        x.estadoNaoDigitalizado = juce::Colour(0xff6e6e74);
        x.estadoCapturado = juce::Colour(0xff4a90e2);
        x.estadoQcOk = juce::Colour(0xff4caf50);
        x.estadoAlerta = juce::Colour(0xffffb84d);
        x.haloSincronizado = juce::Colour(0xff6fe7dd);

        x.campoHumano = juce::Colour(0xffe8e8ea);
        x.campoHerdado = juce::Colour(0xff9a9aa0);
        x.campoLeituraTecnica = juce::Colour(0xff9a9aa0);
        x.campoSugestaoIa = juce::Colour(0xffffb84d);
        x.campoSugestaoIaFundo = juce::Colour(0x22ffb84d);

        x.tamanhoFonteTitulo = 20.0f;
        x.tamanhoFonteSubtitulo = 15.0f;
        x.tamanhoFonteCorpo = 13.0f;
        x.tamanhoFontePequena = 11.0f;

        x.espacoPequeno = 4;
        x.espacoMedio = 8;
        x.espacoGrande = 16;
        x.espacoPainel = 12;

        x.raioPequeno = 4.0f;
        x.raioMedio = 6.0f;
        return x;
    }();
    return t;
}

inline const Tema& tema() { return temaBkrDark(); }

} // namespace matriz::ui
