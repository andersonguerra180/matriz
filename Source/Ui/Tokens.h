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
        x.fundo = juce::Colour(0xff14161c).brighter(0.30f);          // 30% brighter gray background
        x.painel = juce::Colour(0xff252934).brighter(0.30f);         // 30% brighter elevated card surface
        x.painelAlt = juce::Colour(0xff313746).brighter(0.30f);      // 30% brighter secondary card / hover
        x.borda = juce::Colour(0xff75859e);          // Crisp light-tone gray border for high contrast
        x.bordaFoco = juce::Colour(0xff38bdf8);      // Electric cyan / sky blue focus

        x.textoPrimario = juce::Colour(0xffffffff);   // Pure 100% white
        x.textoSecundario = juce::Colour(0xfff1f5f9); // High-contrast bright slate
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

// BKR Light — paleta creme quente inspirada na splash screen (Assets/splash.png).
// Progressão tonal: creme puro (#F5F2EC) → cinza quente escuro (#2D2A26).
// Conforme escurece, puxa pro cinza mas sempre com calor residual (sem azulado).
// Texto sempre escuro-quente para máxima legibilidade sobre fundos creme.
inline const Tema& temaBkrLight() {
    static const Tema t = [] {
        Tema x;
        x.fundo    = juce::Colour(0xffede8df);   // Creme base — tom exato da splash screen
        x.painel   = juce::Colour(0xfff5f2ec);   // Creme levemente claro — cards/painéis
        x.painelAlt = juce::Colour(0xffd8d2c7);  // Creme-cinza médio — inputs/controles/tags
        x.borda    = juce::Colour(0xffb8b2a7);   // Cinza quente médio — bordas visíveis
        x.bordaFoco = juce::Colour(0xff5a5550);  // Cinza quente escuro — anel de foco

        x.textoPrimario   = juce::Colour(0xff1a1815); // Preto quente (não azulado)
        x.textoSecundario = juce::Colour(0xff3d3a35); // Cinza escuro quente
        x.textoTerciario  = juce::Colour(0xff6b6560); // Cinza médio quente
        x.textoSobreAcento = juce::Colour(0xfff5f2ec); // Creme claro sobre botão escuro

        x.acento      = juce::Colour(0xff4a4540); // Cinza quente muito escuro — ações primárias
        x.acentoHover = juce::Colour(0xff2d2a26); // Quase-preto quente — hover
        x.perigo      = juce::Colour(0xffdc2626); // Vermelho — mantido (semântico)
        x.alerta      = juce::Colour(0xffd97706); // Âmbar — mantido (semântico)

        x.estadoNaoDigitalizado = juce::Colour(0xff6b6560); // Cinza quente médio
        x.estadoCapturado       = juce::Colour(0xff4a4540); // Cinza quente escuro (acento)
        x.estadoQcOk            = juce::Colour(0xff16a34a); // Verde — mantido (semântico)
        x.estadoAlerta          = juce::Colour(0xffd97706); // Âmbar — mantido (semântico)
        x.haloSincronizado      = juce::Colour(0xff0d9488); // Teal — mantido (semântico)

        x.campoHumano          = juce::Colour(0xff1a1815); // Preto quente
        x.campoHerdado         = juce::Colour(0xff3d3a35); // Cinza escuro quente
        x.campoLeituraTecnica  = juce::Colour(0xff3d3a35); // Cinza escuro quente
        x.campoSugestaoIa      = juce::Colour(0xffb45309); // Âmbar — mantido
        x.campoSugestaoIaFundo = juce::Colour(0x22f59e0b); // Fundo âmbar tênue — mantido

        x.tamanhoFonteTitulo    = 18.0f;
        x.tamanhoFonteSubtitulo = 14.0f;
        x.tamanhoFonteCorpo     = 12.5f;
        x.tamanhoFontePequena   = 11.0f;
        x.tamanhoFonteMicro     = 9.5f;

        x.espacoPequeno = 4;
        x.espacoMedio   = 8;
        x.espacoGrande  = 16;
        x.espacoPainel  = 12;

        x.raioPequeno = 4.0f;
        x.raioMedio   = 6.0f;
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
