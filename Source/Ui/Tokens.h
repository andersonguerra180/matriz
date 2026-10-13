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

    // ── Redesenho "tablet" ─────────────────────────────────────────────
    // Superfícies e linhas
    juce::Colour cartao;            // cartão / folha
    juce::Colour cartaoDestaque;    // item ativo da barra lateral
    juce::Colour linhaSuave;        // divisórias e contornos de cartão (já com alfa)
    juce::Colour canvasMapa;        // fundo do Folder Map
    juce::Colour pontilhadoMapa;    // pontos do Folder Map
    juce::Colour campoFundo;        // fundo de campos de texto / combos
    juce::Colour campoTexto;        // texto dentro de campos
    juce::Colour campoSomenteLeitura; // campos automáticos (cinza)
    juce::Colour sombra;            // sombra de cartões
    juce::Colour scrim;             // véu atrás de folhas modais
    juce::Colour perigoSuave;       // fundo do botão SAIR
    // Cores de seção
    juce::Colour secaoBackup;
    juce::Colour secaoExplorer;
    juce::Colour secaoStats;
    // Cores de tipo de arquivo
    juce::Colour tipoAudio;
    juce::Colour tipoVideo;
    juce::Colour tipoImagem;
    juce::Colour tipoDocumento;
    juce::Colour tipoSessao;
    juce::Colour tipoOutros;
    juce::Colour tipoConflito;
    juce::Colour tipoZip;

    // Medidas
    float raioCartao;        // 20
    float raioFolha;         // 32
    float raioCanvas;        // 28
    float raioCampo;         // 12
    int alturaBotao;         // 44
    int alturaBotaoPrimario; // 48
    int alturaChip;          // 30
    int alturaCampo;         // 40
    int alvoToqueMinimo;     // 40
    int larguraNav;          // 96
    int alturaTituloTela;    // 80
    int alturaStatus;        // 32
    int larguraFiltros;      // 290
    int larguraMetadados;    // 320

    // Tipografia de tela
    float fonteTituloTela;    // 26
    float fonteSubtituloTela; // 13
    float fonteRotuloSecao;   // 11
    float fonteTituloHome;    // 40
    float fonteKpi;           // 30
    float fonteNomeNo;        // 18
    float fonteCorpoGrande;   // 15
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

        // Redesenho tablet — BKR Dark
        x.cartao = x.painel;
        x.cartaoDestaque = x.painelAlt;
        x.linhaSuave = x.borda.withAlpha(0.45f);
        x.canvasMapa = x.fundo.darker(0.25f);
        x.pontilhadoMapa = x.borda.withAlpha(0.35f);
        x.campoFundo = juce::Colour(0xff0f1218);
        x.campoTexto = juce::Colour(0xfff1f5f9);
        x.campoSomenteLeitura = x.painelAlt.darker(0.2f);
        x.sombra = juce::Colour(0x66000000);
        x.scrim = juce::Colour(0xb0000000);
        x.perigoSuave = juce::Colour(0x33ef4444);
        x.secaoBackup = juce::Colour(0xff38bdf8);
        x.secaoExplorer = juce::Colour(0xff22c55e);
        x.secaoStats = juce::Colour(0xfff59e0b);
        x.tipoAudio = juce::Colour(0xfff59e0b);
        x.tipoVideo = juce::Colour(0xffa855f7);
        x.tipoImagem = juce::Colour(0xff22c55e);
        x.tipoDocumento = juce::Colour(0xff60a5fa);
        x.tipoSessao = juce::Colour(0xfffb923c);
        x.tipoOutros = juce::Colour(0xff94a3b8);
        x.tipoConflito = juce::Colour(0xffef4444);
        x.tipoZip = juce::Colour(0xff0ea5e9);

        x.raioCartao = 20.0f;
        x.raioFolha = 32.0f;
        x.raioCanvas = 28.0f;
        x.raioCampo = 12.0f;
        x.alturaBotao = 44;
        x.alturaBotaoPrimario = 48;
        x.alturaChip = 30;
        x.alturaCampo = 40;
        x.alvoToqueMinimo = 40;
        x.larguraNav = 96;
        x.alturaTituloTela = 80;
        x.alturaStatus = 32;
        x.larguraFiltros = 290;
        x.larguraMetadados = 320;
        x.fonteTituloTela = 26.0f;
        x.fonteSubtituloTela = 13.0f;
        x.fonteRotuloSecao = 11.0f;
        x.fonteTituloHome = 40.0f;
        x.fonteKpi = 30.0f;
        x.fonteNomeNo = 18.0f;
        x.fonteCorpoGrande = 15.0f;
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

        // Redesenho tablet — BKR Light
        x.cartao = x.painel;
        x.cartaoDestaque = x.painel;
        x.linhaSuave = x.borda.withAlpha(0.60f);
        x.canvasMapa = juce::Colour(0xffe4dfd5);
        x.pontilhadoMapa = x.borda.withAlpha(0.55f);
        x.campoFundo = juce::Colour(0xffffffff);
        x.campoTexto = x.textoPrimario;
        x.campoSomenteLeitura = juce::Colour(0xffe9e5dd);
        x.sombra = juce::Colour(0x14000000);
        x.scrim = juce::Colour(0x8c1a1815);
        x.perigoSuave = juce::Colour(0xfffbe4e1);
        x.secaoBackup = juce::Colour(0xff38bdf8);
        x.secaoExplorer = juce::Colour(0xff22c55e);
        x.secaoStats = juce::Colour(0xfff59e0b);
        x.tipoAudio = juce::Colour(0xffd97706);
        x.tipoVideo = juce::Colour(0xff9333ea);
        x.tipoImagem = juce::Colour(0xff16a34a);
        x.tipoDocumento = juce::Colour(0xff2563eb);
        x.tipoSessao = juce::Colour(0xffea580c);
        x.tipoOutros = juce::Colour(0xff6b6560);
        x.tipoConflito = juce::Colour(0xffdc2626);
        x.tipoZip = juce::Colour(0xff0284c7);

        x.raioCartao = 20.0f;
        x.raioFolha = 32.0f;
        x.raioCanvas = 28.0f;
        x.raioCampo = 12.0f;
        x.alturaBotao = 44;
        x.alturaBotaoPrimario = 48;
        x.alturaChip = 30;
        x.alturaCampo = 40;
        x.alvoToqueMinimo = 40;
        x.larguraNav = 96;
        x.alturaTituloTela = 80;
        x.alturaStatus = 32;
        x.larguraFiltros = 290;
        x.larguraMetadados = 320;
        x.fonteTituloTela = 26.0f;
        x.fonteSubtituloTela = 13.0f;
        x.fonteRotuloSecao = 11.0f;
        x.fonteTituloHome = 40.0f;
        x.fonteKpi = 30.0f;
        x.fonteNomeNo = 18.0f;
        x.fonteCorpoGrande = 15.0f;
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
