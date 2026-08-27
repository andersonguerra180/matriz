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

// BKR Dark — Pro Tools-inspired dark theme: medium charcoal surfaces,
// visible borders, cool blue accent. Brighter than Logic, closer to
// Pro Tools / Reaper default.
inline const Tema& temaBkrDark() {
    static const Tema t = [] {
        Tema x;
        x.fundo = juce::Colour(0xff181818);          // Dark charcoal background
        x.painel = juce::Colour(0xff222222);         // Slightly lighter panel
        x.painelAlt = juce::Colour(0xff2d2d2d);      // Alternative panel
        x.borda = juce::Colour(0xff1c1c1c);          // Dark border
        x.bordaFoco = juce::Colour(0xffe6a100);      // Lightroom amber focus

        x.textoPrimario = juce::Colour(0xffe0e0e0);   // Light text
        x.textoSecundario = juce::Colour(0xffa0a0a0); // Muted text
        x.textoTerciario = juce::Colour(0xff707070);  // Darker gray text
        x.textoSobreAcento = juce::Colour(0xff181818); // Dark text on amber accent

        x.acento = juce::Colour(0xffe6a100);         // Lightroom signature amber/gold
        x.acentoHover = juce::Colour(0xffffb732);    // Brighter amber
        x.perigo = juce::Colour(0xffc0392b);
        x.alerta = juce::Colour(0xffe67e22);

        x.estadoNaoDigitalizado = juce::Colour(0xff707070);
        x.estadoCapturado = juce::Colour(0xff3d7fd6);
        x.estadoQcOk = juce::Colour(0xff3f9142);
        x.estadoAlerta = juce::Colour(0xffb87517);
        x.haloSincronizado = juce::Colour(0xff1f8f84);

        x.campoHumano = juce::Colour(0xffe0e0e0);
        x.campoHerdado = juce::Colour(0xffa0a0a0);
        x.campoLeituraTecnica = juce::Colour(0xffa0a0a0);
        x.campoSugestaoIa = juce::Colour(0xffb87517);
        x.campoSugestaoIaFundo = juce::Colour(0x22b87517);

        x.tamanhoFonteTitulo = 22.0f;
        x.tamanhoFonteSubtitulo = 17.0f;
        x.tamanhoFonteCorpo = 15.0f;
        x.tamanhoFontePequena = 13.0f;

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

// BKR Light — cinza claro neutro, mesmo acento azul frio do BKR Dark pra
// manter a identidade visual entre os dois. Estrutura idêntica ao Dark
// campo a campo, de propósito: adicionar o terceiro tema (System CRT,
// item 15 da ordem de trabalho) não deve exigir tocar nesta função nem em
// nenhum Component, só escrever temaSystemCrt() e trocar o que tema()
// devolve — mesma garantia que já valia pra este ser o segundo tema.
inline const Tema& temaBkrLight() {
    static const Tema t = [] {
        Tema x;
        x.fundo = juce::Colour(0xfff8f2ec);
        x.painel = juce::Colour(0xffefe8e0);
        x.painelAlt = juce::Colour(0xffe6ded5);
        x.borda = juce::Colour(0xffc8bfb5);
        x.bordaFoco = juce::Colour(0xff3d7fd6);

        x.textoPrimario = juce::Colour(0xff000000);
        x.textoSecundario = juce::Colour(0xff2c2c2e);
        x.textoTerciario = juce::Colour(0xff68686d);
        x.textoSobreAcento = juce::Colour(0xffffffff);

        x.acento = juce::Colour(0xff3d7fd6);
        x.acentoHover = juce::Colour(0xff5b9dff);
        x.perigo = juce::Colour(0xffc0392b);
        x.alerta = juce::Colour(0xffd35400);

        x.estadoNaoDigitalizado = juce::Colour(0xff9a9aa0);
        x.estadoCapturado = juce::Colour(0xff3d7fd6);
        x.estadoQcOk = juce::Colour(0xff3f9142);
        x.estadoAlerta = juce::Colour(0xffb87517);
        x.haloSincronizado = juce::Colour(0xff1f8f84);

        x.campoHumano = juce::Colour(0xff1c1c1e);
        x.campoHerdado = juce::Colour(0xff5a5a60);
        x.campoLeituraTecnica = juce::Colour(0xff5a5a60);
        x.campoSugestaoIa = juce::Colour(0xffb87517);
        x.campoSugestaoIaFundo = juce::Colour(0x22b87517);

        x.tamanhoFonteTitulo = 22.0f;
        x.tamanhoFonteSubtitulo = 17.0f;
        x.tamanhoFonteCorpo = 15.0f;
        x.tamanhoFontePequena = 13.0f;

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

// Active theme — reads preference on first call, caches until recarregarTema().
// Not inline: defined in Tokens.cpp (linked via CMakeLists).
const Tema& tema();
void recarregarTema();

// Font scale applied globally.
float escalaFonte();

} // namespace matriz::ui
