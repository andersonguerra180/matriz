#include "BarraMetricasComponent.h"

#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"
#include "Tokens.h"

#include <cmath>

namespace matriz::ui {

namespace {

// Leitura defensiva do JSON de leitura técnica: qualquer campo pode faltar
// (ffprobe não devolve bit depth pra todo codec, imagem não tem sample rate),
// e faltar é normal — nunca erro.
juce::var propriedade(const juce::var& obj, const char* chave) {
    if (auto* o = obj.getDynamicObject()) return o->getProperty(juce::Identifier(chave));
    return {};
}

juce::String textoOuVazio(const juce::var& v) { return v.isVoid() || v.isUndefined() ? juce::String() : v.toString(); }

juce::String formatarDuracao(double segundos) {
    if (segundos <= 0.0) return {};
    int total = static_cast<int>(std::llround(segundos));
    int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0) return juce::String(h) + ":" + juce::String(m).paddedLeft('0', 2) + ":" + juce::String(s).paddedLeft('0', 2);
    return juce::String(m).paddedLeft('0', 2) + ":" + juce::String(s).paddedLeft('0', 2);
}

juce::String formatarDb(double db) { return juce::String(db, 1) + " dB"; }

} // namespace

BarraMetricasComponent::BarraMetricasComponent() { startTimerHz(30); }
BarraMetricasComponent::~BarraMetricasComponent() { stopTimer(); }

void BarraMetricasComponent::mostrarResumoDaGrade(int totalItens, juce::int64 tamanhoTotalBytes) {
    campos_.clear();
    titulo_ = matriz::i18n::t("metricas.resumo_grade");
    campos_.push_back({matriz::i18n::t("metricas.itens"), juce::String(totalItens)});
    campos_.push_back({matriz::i18n::t("metricas.tamanho"), juce::File::descriptionOfSizeInBytes(tamanhoTotalBytes)});
    zerarVu();
    repaint();
}

void BarraMetricasComponent::mostrarItem(ProjetoAberto& projeto, const std::string& itemId) {
    campos_.clear();
    titulo_.clear();
    if (itemId.empty()) {
        repaint();
        return;
    }

    auto arquivo = projeto.arquivoPrincipal(itemId);
    if (!arquivo) {
        titulo_ = matriz::i18n::t("metricas.sem_arquivo");
        repaint();
        return;
    }

    juce::File arquivoEmDisco(arquivo->caminhoAbsoluto);
    titulo_ = arquivoEmDisco.getFileName();
    juce::String extensao = arquivoEmDisco.getFileExtension().trimCharactersAtStart(".").toLowerCase();

    juce::var tecnica = juce::JSON::parse(arquivo->caracteristicasTecnicasJson);
    reconstruirCampos(tecnica, extensao.toStdString());
    repaint();
}

void BarraMetricasComponent::reconstruirCampos(const juce::var& tecnica, const std::string& extensao) {
    auto categoria = matriz::ingest::categoriaPorExtensao(juce::File("x." + juce::String(extensao)));
    using Cat = matriz::ingest::CategoriaMidia;

    auto adicionar = [this](const char* chaveI18n, const juce::String& valor) {
        if (valor.isNotEmpty()) campos_.push_back({matriz::i18n::t(chaveI18n), valor});
    };

    // Formato do container vale pra tudo.
    adicionar("metricas.formato", juce::String(extensao).toUpperCase());

    if (categoria == Cat::Audio || categoria == Cat::Video) {
        // LUFS-I / LRA — pré-calculados na leitura técnica (item 6.1). Se o
        // arquivo entrou antes de a medida existir, o campo não aparece em
        // vez de mostrar um zero que parece medido.
        juce::var lufs = propriedade(tecnica, "lufsIntegrado");
        juce::var lra = propriedade(tecnica, "lra");
        juce::var pico = propriedade(tecnica, "picoDbfs");
        if (!lufs.isVoid()) adicionar("metricas.lufs_i", juce::String(static_cast<double>(lufs), 1) + " LUFS");
        if (!lra.isVoid()) adicionar("metricas.lra", juce::String(static_cast<double>(lra), 1) + " LU");
        if (!pico.isVoid()) adicionar("metricas.pico", formatarDb(static_cast<double>(pico)));

        juce::var sampleRate = propriedade(tecnica, "sampleRate");
        if (!sampleRate.isVoid())
            adicionar("metricas.sample_rate", juce::String(static_cast<double>(sampleRate) / 1000.0, 1) + " kHz");
        juce::var bitDepth = propriedade(tecnica, "bitDepth");
        if (!bitDepth.isVoid()) adicionar("metricas.bit_depth", bitDepth.toString() + " bit");
        adicionar("metricas.canais", textoOuVazio(propriedade(tecnica, "canais")));
        adicionar("metricas.codec", textoOuVazio(propriedade(tecnica, "codec")));

        juce::var duracao = propriedade(tecnica, "duracaoSegundos");
        if (!duracao.isVoid()) adicionar("metricas.duracao", formatarDuracao(static_cast<double>(duracao)));
    }

    if (categoria == Cat::Video) {
        juce::var largura = propriedade(tecnica, "larguraPx");
        juce::var altura = propriedade(tecnica, "alturaPx");
        if (!largura.isVoid() && !altura.isVoid())
            adicionar("metricas.resolucao", largura.toString() + juce::String::fromUTF8("×") + altura.toString());
        juce::var fps = propriedade(tecnica, "fps");
        if (!fps.isVoid()) adicionar("metricas.fps", juce::String(static_cast<double>(fps), 3) + " fps");
    }

    if (categoria == Cat::Imagem) {
        juce::var largura = propriedade(tecnica, "larguraPx");
        juce::var altura = propriedade(tecnica, "alturaPx");
        if (!largura.isVoid() && !altura.isVoid())
            adicionar("metricas.dimensao", largura.toString() + juce::String::fromUTF8("×") + altura.toString());
        juce::var bitDepth = propriedade(tecnica, "bitDepth");
        if (!bitDepth.isVoid()) adicionar("metricas.profundidade", bitDepth.toString() + " bit");
    }

    if (categoria == Cat::Documento) {
        juce::var duracao = propriedade(tecnica, "duracaoSegundos");
        juce::ignoreUnused(duracao);
        // Contagem de páginas não existe ainda na leitura técnica (PDFium/
        // MuPDF sem build viável — ver nota em LeituraTecnica.cpp); só o
        // tamanho do arquivo é conhecido. Ausência declarada, não zero falso.
    }

    // Bitrate e espaço de cor saem do objeto bruto do ffprobe quando ele os
    // traz — não há campo tipado pra eles em LeituraTecnicaResultado.
    if (categoria == Cat::Video || categoria == Cat::Imagem) {
        juce::var bruto = propriedade(tecnica, "bruto");
        adicionar("metricas.bitrate", textoOuVazio(propriedade(bruto, "bit_rate")));
        adicionar("metricas.espaco_cor", textoOuVazio(propriedade(bruto, "color_space")));
    }

    vuAtivo_ = categoria == Cat::Audio || categoria == Cat::Video;
}

void BarraMetricasComponent::alimentarVu(float picoEsquerda, float picoDireita) {
    // Subida imediata (o VU tem que reagir ao transiente), queda com
    // constante de 300 ms aplicada no timer.
    vuAlvoEsquerda_ = juce::jlimit(0.0f, 1.0f, picoEsquerda);
    vuAlvoDireita_ = juce::jlimit(0.0f, 1.0f, picoDireita);
    vuEsquerda_ = std::max(vuEsquerda_, vuAlvoEsquerda_);
    vuDireita_ = std::max(vuDireita_, vuAlvoDireita_);
    vuAtivo_ = true;
}

void BarraMetricasComponent::zerarVu() {
    vuEsquerda_ = vuDireita_ = vuAlvoEsquerda_ = vuAlvoDireita_ = 0.0f;
    repaint();
}

void BarraMetricasComponent::timerCallback() {
    // 300 ms pra cair ~63% (uma constante de tempo), a 30 Hz.
    const float coef = std::exp(-1.0f / (0.300f * 30.0f));
    float antesE = vuEsquerda_, antesD = vuDireita_;
    vuEsquerda_ = vuAlvoEsquerda_ + (vuEsquerda_ - vuAlvoEsquerda_) * coef;
    vuDireita_ = vuAlvoDireita_ + (vuDireita_ - vuAlvoDireita_) * coef;
    vuAlvoEsquerda_ *= coef; // sem novos blocos, o alvo também decai — o áudio parou
    vuAlvoDireita_ *= coef;
    if (std::abs(antesE - vuEsquerda_) > 0.001f || std::abs(antesD - vuDireita_) > 0.001f) repaint();
}

void BarraMetricasComponent::resized() {}

void BarraMetricasComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painelAlt);
    g.setColour(tk.borda);
    g.fillRect(0, 0, getWidth(), 1);

    auto area = getLocalBounds().reduced(tk.espacoMedio, 4);

    // Medidor VU à direita, sempre no mesmo lugar — é o elemento que o olho
    // procura em movimento, e mover de posição por tipo de arquivo cansa.
    if (vuAtivo_) {
        auto areaVu = area.removeFromRight(140);
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 2.0f)));
        g.drawText("VU", areaVu.removeFromLeft(22), juce::Justification::centredLeft);

        auto desenharBarra = [&](juce::Rectangle<int> r, float nivel) {
            g.setColour(tk.fundo);
            g.fillRect(r);
            int largura = static_cast<int>(static_cast<float>(r.getWidth()) * nivel);
            // Verde até -6 dBFS, âmbar até -1, vermelho acima — a mesma
            // leitura de relance do Groove Sculptor.
            juce::Colour cor = nivel > 0.891f ? tk.perigo : (nivel > 0.501f ? tk.alerta : tk.estadoQcOk);
            g.setColour(cor);
            g.fillRect(r.withWidth(largura));
            g.setColour(tk.borda);
            g.drawRect(r, 1);
        };
        auto esquerda = areaVu.removeFromTop(areaVu.getHeight() / 2).reduced(0, 2);
        auto direita = areaVu.reduced(0, 2);
        desenharBarra(esquerda, vuEsquerda_);
        desenharBarra(direita, vuDireita_);
        area.removeFromRight(tk.espacoMedio);
    }

    if (titulo_.isNotEmpty()) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        auto areaTitulo = area.removeFromLeft(200);
        g.drawText(titulo_, areaTitulo, juce::Justification::centredLeft, true);
        area.removeFromLeft(tk.espacoMedio);
    }

    // Pares rótulo/valor em colunas de largura fixa — o operador aprende a
    // posição de cada métrica e passa a ler sem procurar.
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 2.0f)));
    constexpr int kLarguraCampo = 108;
    for (auto& [rotulo, valor] : campos_) {
        if (area.getWidth() < kLarguraCampo) break;
        auto col = area.removeFromLeft(kLarguraCampo);
        g.setColour(tk.textoTerciario);
        g.drawText(rotulo, col.removeFromTop(col.getHeight() / 2), juce::Justification::bottomLeft, true);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(valor, col, juce::Justification::topLeft, true);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 2.0f)));
    }
}

} // namespace matriz::ui
