#pragma once

#include <JuceHeader.h>

namespace juce {
class Component;
class LookAndFeel_V4;
}

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

    // Tipografia (Padronização Estilo Adobe Pro UI)
    float tamanhoFonteTitulo;      // 18.0f - Workspace Header (ex.: INTAKE, CATALOG, TREEMAP, BACKUP, DISK)
    float tamanhoFonteSubtitulo;   // 14.0f - Section / Group header / Dialog Title
    float tamanhoFonteCorpo;       // 12.5f - Standard body, buttons, table items, inputs, combos
    float tamanhoFontePequena;     // 11.0f - Captions, secondary paths, metadata labels, timecodes
    float tamanhoFonteMicro;       // 9.5f  - Status tags, pills (ONLINE/OFFLINE, QC OK, EXT)

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
// Pro Tools / Modern Slate default.
inline const Tema& temaBkrDark() {
    static const Tema t = [] {
        Tema x;
        x.fundo = juce::Colour(0xff14161c);          // Deep matte background
        x.painel = juce::Colour(0xff252934);         // Clearly lighter elevated card surface (matching UI reference)
        x.painelAlt = juce::Colour(0xff313746);      // Secondary card / hover / table header
        x.borda = juce::Colour(0xff434a5d);          // Crisp elegant borders
        x.bordaFoco = juce::Colour(0xff38bdf8);      // Electric cyan / sky blue focus

        x.textoPrimario = juce::Colour(0xffffffff);   // Pure 100% white
        x.textoSecundario = juce::Colour(0xffe2e8f0); // High-contrast bright slate
        x.textoTerciario = juce::Colour(0xffcbd5e1);  // Clean readable off-white
        x.textoSobreAcento = juce::Colour(0xff0b0f17); // Maximum contrast on accent

        x.acento = juce::Colour(0xff38bdf8);         // Vibrant sky blue / cyan
        x.acentoHover = juce::Colour(0xff60a5fa);    // Electric blue hover
        x.perigo = juce::Colour(0xffef4444);         // Vivid red
        x.alerta = juce::Colour(0xfff59e0b);         // Vivid amber

        x.estadoNaoDigitalizado = juce::Colour(0xff94a3b8);
        x.estadoCapturado = juce::Colour(0xff38bdf8);
        x.estadoQcOk = juce::Colour(0xff22c55e);
        x.estadoAlerta = juce::Colour(0xfff59e0b);
        x.haloSincronizado = juce::Colour(0xff14b8a6);

        x.campoHumano = juce::Colour(0xffffffff);
        x.campoHerdado = juce::Colour(0xffe2e8f0);
        x.campoLeituraTecnica = juce::Colour(0xffe2e8f0);
        x.campoSugestaoIa = juce::Colour(0xfffbbf24);
        x.campoSugestaoIaFundo = juce::Colour(0x33fbbf24);

        x.tamanhoFonteTitulo = 18.0f;
        x.tamanhoFonteSubtitulo = 14.0f;
        x.tamanhoFonteCorpo = 12.5f;
        x.tamanhoFontePequena = 11.0f;
        x.tamanhoFonteMicro = 9.5f;

        x.espacoPequeno = 4;
        x.espacoMedio = 8;
        x.espacoGrande = 16;
        x.espacoPainel = 12;

        x.raioPequeno = 5.0f;
        x.raioMedio = 8.0f;
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
        x.perigo = juce::Colour(0xffb83232);
        x.alerta = juce::Colour(0xffb87a1a);

        x.estadoNaoDigitalizado = juce::Colour(0xff68686d);
        x.estadoCapturado = juce::Colour(0xff3d7fd6);
        x.estadoQcOk = juce::Colour(0xff2d8a4e);
        x.estadoAlerta = juce::Colour(0xffb87a1a);
        x.haloSincronizado = juce::Colour(0xff1f9d85);

        x.campoHumano = juce::Colour(0xff000000);
        x.campoHerdado = juce::Colour(0xff2c2c2e);
        x.campoLeituraTecnica = juce::Colour(0xff2c2c2e);
        x.campoSugestaoIa = juce::Colour(0xff8a5700);
        x.campoSugestaoIaFundo = juce::Colour(0x33b87a1a);

        x.tamanhoFonteTitulo = 18.0f;
        x.tamanhoFonteSubtitulo = 14.0f;
        x.tamanhoFonteCorpo = 12.5f;
        x.tamanhoFontePequena = 11.0f;
        x.tamanhoFonteMicro = 9.5f;

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
void aplicarTemaGlobal(juce::Component* raiz = nullptr);

// Configure JUCE LookAndFeel with current theme tokens across all UI widgets
void configurarLookAndFeel(juce::LookAndFeel_V4& lf);

// Font scale applied globally.
float escalaFonte();

} // namespace matriz::ui
