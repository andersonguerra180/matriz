#pragma once

#include <JuceHeader.h>

#include "../Ingest/LeituraTecnica.h"
#include "Tokens.h"

// Padrão visual da janela Explorer · Folder Map (pasta do Folder Map e janela do Project Container), igual ao BKR Matriz Mini:
// papel #F6F2EB, barra lateral e faixas #EDE8E0, cartões #FBF9F5 com borda #D8D1C6, texto #1B1917 / #5E5850, selecionado em
// grafite #4A4440 com texto branco, fonte Jost em tudo. Usado pela janela e pelos componentes que ela hospeda quando estão no
// "estilo explorer" (MosaicoComponent, FiltrosComponent, PainelMetadadosArquivo). No tema escuro as mesmas funções vêm do tema.
namespace matriz::ui::explorer {

struct Cores {
    juce::Colour fundo, lateral, barra, cartao, borda, texto, secundario, grafite, sobreGrafite, dourado;
};

inline Cores cores() {
    const auto& tk = tema();
    if (tk.fundo.getBrightness() > 0.5f)
        return { juce::Colour(0xfff6f2eb), juce::Colour(0xffede8e0), juce::Colour(0xfff1ece4), juce::Colour(0xfffbf9f5),
                 juce::Colour(0xffd8d1c6), juce::Colour(0xff1b1917), juce::Colour(0xff5e5850), juce::Colour(0xff4a4440),
                 juce::Colours::white, juce::Colour(0xffb8860b) };
    return { tk.fundo, tk.painelAlt, tk.painel, tk.cartao, tk.linhaSuave, tk.textoPrimario, tk.textoSecundario, tk.acento,
             tk.textoSobreAcento, juce::Colour(0xffb8860b) };
}

// Cores padrão por tipo de mídia (barra "By media type", bolinhas da lateral, selo do formato).
inline juce::Colour corVideo() { return juce::Colour(0xff8a63c7); }
inline juce::Colour corAudio() { return juce::Colour(0xff2a9d8f); }
inline juce::Colour corFoto() { return juce::Colour(0xffe08a3c); }
inline juce::Colour corDocumento() { return juce::Colour(0xff2e8b57); }
// Destaque laranja: contador de filtros ativos ("1 active", bolinha da aba Filters, "Clear").
inline juce::Colour corDestaque() { return juce::Colour(0xffc2612a); }

enum class Tipo { Video, Audio, Foto, Documento };

inline Tipo tipoDaExtensao(const juce::String& extensao) {
    switch (matriz::ingest::categoriaPorExtensao(extensao.trimCharactersAtStart(".").toLowerCase())) {
        case matriz::ingest::CategoriaMidia::Video: return Tipo::Video;
        case matriz::ingest::CategoriaMidia::Audio: return Tipo::Audio;
        case matriz::ingest::CategoriaMidia::Arte:
        case matriz::ingest::CategoriaMidia::Imagem: return Tipo::Foto;
        default: return Tipo::Documento;
    }
}

inline juce::Colour corDoTipo(Tipo t) {
    switch (t) {
        case Tipo::Video: return corVideo();
        case Tipo::Audio: return corAudio();
        case Tipo::Foto: return corFoto();
        case Tipo::Documento: break;
    }
    return corDocumento();
}

// Jost (embutida, igual em macOS, Windows e Linux), já na escala de fonte do app.
inline juce::Font fonte(float tamanho, bool negrito = false) {
    return juce::Font(juce::FontOptions("Jost", tamanho * escalaFonte(), negrito ? juce::Font::bold : juce::Font::plain));
}

// Título de seção: caixa alta pequena, com espaçamento.
inline juce::Font fonteSecao() { return fonte(11.0f, true).withExtraKerningFactor(0.10f); }

inline int larguraDoTexto(const juce::Font& f, const juce::String& texto) { return juce::GlyphArrangement::getStringWidthInt(f, texto); }

// Inteiro com separador de milhar ("2,701"; "2.701" em português).
inline juce::String comMilhares(juce::int64 n, bool pt = false) {
    auto txt = juce::String(n < 0 ? -n : n);
    for (int i = txt.length() - 3; i > 0; i -= 3) txt = txt.substring(0, i) + (pt ? "." : ",") + txt.substring(i);
    return n < 0 ? "-" + txt : txt;
}

}  // namespace matriz::ui::explorer
