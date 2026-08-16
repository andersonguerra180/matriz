#include "MetadadosOriginaisComponent.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

juce::String formatarDuracao(double segundos) {
    if (segundos <= 0.0) return {};
    int total = static_cast<int>(segundos + 0.5);
    int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0)
        return juce::String(h) + ":" + juce::String(m).paddedLeft('0', 2) + ":" + juce::String(s).paddedLeft('0', 2);
    return juce::String(m) + ":" + juce::String(s).paddedLeft('0', 2);
}

// Separador de milhar, pra 48000 virar "48 000 Hz" — número técnico longo
// grudado é exatamente o tipo de coisa que faz um painel parecer log de
// debug em vez de informação.
juce::String comSeparador(juce::int64 valor) {
    juce::String bruto(valor);
    juce::String saida;
    int contador = 0;
    for (int i = bruto.length() - 1; i >= 0; --i) {
        saida = juce::String::charToString(bruto[i]) + saida;
        if (++contador % 3 == 0 && i > 0) saida = " " + saida;
    }
    return saida;
}

juce::String formatarBytes(juce::int64 bytes) {
    if (bytes <= 0) return {};
    const char* unidades[] = {"B", "KB", "MB", "GB", "TB"};
    double valor = static_cast<double>(bytes);
    int u = 0;
    while (valor >= 1024.0 && u < 4) { valor /= 1024.0; ++u; }
    return juce::String(valor, valor < 10.0 && u > 0 ? 1 : 0) + " " + unidades[u];
}

} // namespace

MetadadosOriginaisComponent::MetadadosOriginaisComponent(ProjetoAberto& projeto) : projeto_(projeto) {}

void MetadadosOriginaisComponent::mostrarItem(const std::string& itemId) {
    reconstruir(itemId);
    if (aoMudarAltura) aoMudarAltura();
    repaint();
}

void MetadadosOriginaisComponent::reconstruir(const std::string& itemId) {
    linhas_.clear();
    semArquivo_ = false;
    if (itemId.empty()) {
        semArquivo_ = true;
        return;
    }

    auto arquivo = projeto_.arquivoPrincipal(itemId);
    if (!arquivo) {
        semArquivo_ = true;
        return;
    }

    juce::var dados = juce::JSON::parse(arquivo->caracteristicasTecnicasJson);
    if (!dados.isObject()) {
        semArquivo_ = true;
        return;
    }

    auto texto = [&](const char* chave) { return dados[chave].toString(); };
    auto tem = [&](const char* chave) { return dados.hasProperty(chave); };

    // Ordem curada, não a ordem do JSON: o que o operador procura primeiro
    // (o que é este arquivo, quanto dura, que tamanho tem) vem antes do
    // detalhe de codec.
    if (tem("duracaoSegundos")) {
        juce::String d = formatarDuracao(static_cast<double>(dados["duracaoSegundos"]));
        if (d.isNotEmpty()) linhas_.push_back({matriz::i18n::t("metadados.duracao"), d});
    }
    if (tem("larguraPx") && tem("alturaPx"))
        linhas_.push_back({matriz::i18n::t("metadados.dimensoes"),
                            texto("larguraPx") + " x " + texto("alturaPx") + " px"});
    if (tem("codec")) linhas_.push_back({matriz::i18n::t("metadados.codec"), texto("codec")});
    if (tem("sampleRate"))
        linhas_.push_back({matriz::i18n::t("metadados.sample_rate"),
                            comSeparador(static_cast<juce::int64>(dados["sampleRate"])) + " Hz"});
    if (tem("bitDepth")) linhas_.push_back({matriz::i18n::t("metadados.bit_depth"), texto("bitDepth") + " bits"});
    if (tem("canais")) linhas_.push_back({matriz::i18n::t("metadados.canais"), texto("canais")});
    if (tem("fps")) linhas_.push_back({matriz::i18n::t("metadados.fps"), texto("fps")});

    // Codec lossy é um FATO de preservação, não um detalhe: um "master" que
    // já nasceu de MP3 precisa aparecer aqui em vez de passar batido.
    if (tem("codecLossyDeclarado") && static_cast<bool>(dados["codecLossyDeclarado"]))
        linhas_.push_back({matriz::i18n::t("metadados.lossy"), matriz::i18n::t("comum.sim")});

    // Tamanho do arquivo mora em "bruto" (é o que lerDocumentoPdf grava, e
    // o ffprobe também traz). Sem isto um PDF não mostraria campo nenhum e
    // a seção pareceria quebrada justamente pro tipo de material que menos
    // tem metadado técnico.
    juce::var brutoTamanho = dados["bruto"];
    if (brutoTamanho.isObject() && brutoTamanho.hasProperty("fileSizeBytes")) {
        juce::String tam = formatarBytes(static_cast<juce::int64>(brutoTamanho["fileSizeBytes"]));
        if (tam.isNotEmpty()) linhas_.push_back({matriz::i18n::t("metadados.tamanho"), tam});
    }

    // EXIF vive dentro de "bruto" (ver Ingest/IngestArquivo.cpp) — é o que
    // o Exiv2 leu da foto, chave a chave, sem normalização nossa.
    juce::var bruto = dados["bruto"];
    if (bruto.isObject()) {
        juce::var exif = bruto["exif"];
        if (auto* obj = exif.getDynamicObject())
            for (auto& prop : obj->getProperties())
                linhas_.push_back({prop.name.toString(), prop.value.toString()});
    }

    juce::File fArquivo(arquivo->caminhoAbsoluto);
    if (fArquivo.existsAsFile()) {
        juce::String tam = formatarBytes(fArquivo.getSize());
        if (tam.isNotEmpty() && linhas_.size() < static_cast<size_t>(kMaxLinhasVisiveis))
            linhas_.push_back({matriz::i18n::t("metadados.tamanho_arquivo"), tam});

        auto criado = fArquivo.getCreationTime();
        if (criado.toMilliseconds() > 0)
            linhas_.push_back({matriz::i18n::t("metadados.criado_em"), criado.toString(true, true, false)});

        auto modificado = fArquivo.getLastModificationTime();
        if (modificado.toMilliseconds() > 0)
            linhas_.push_back({matriz::i18n::t("metadados.modificado_em"), modificado.toString(true, true, false)});

        linhas_.push_back({matriz::i18n::t("metadados.extensao"), fArquivo.getFileExtension()});
    }

    if (linhas_.empty()) semArquivo_ = true;
}

void MetadadosOriginaisComponent::alternarColapso() {
    colapsada_ = !colapsada_;
    if (aoMudarAltura) aoMudarAltura();
    repaint();
}

int MetadadosOriginaisComponent::alturaDesejada() const {
    if (colapsada_) return kAlturaCabecalho;
    int linhas = semArquivo_ ? 1 : static_cast<int>(linhas_.size());
    // Teto: um JPEG com EXIF farto tem dezenas de chaves, e deixar a seção
    // crescer sem limite empurraria a ficha inteira pra fora do painel.
    linhas = juce::jlimit(1, kMaxLinhasVisiveis, linhas);
    return kAlturaCabecalho + linhas * kAlturaLinha + tema().espacoPequeno;
}

std::vector<juce::String> MetadadosOriginaisComponent::rotulosParaTeste() const {
    std::vector<juce::String> out;
    for (auto& l : linhas_) out.push_back(l.rotulo);
    return out;
}

void MetadadosOriginaisComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.getPosition().y < kAlturaCabecalho) alternarColapso();
}

void MetadadosOriginaisComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painelAlt);

    auto cabecalho = getLocalBounds().removeFromTop(kAlturaCabecalho).reduced(tk.espacoMedio, 0);
    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    g.drawText((colapsada_ ? juce::String("+  ") : juce::String("-  ")) +
                   juce::String("AUTO + FIXED"),
               cabecalho, juce::Justification::centredLeft);

    g.setColour(tk.textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    g.drawText(juce::String("extracted from file"), cabecalho, juce::Justification::centredRight);

    g.setColour(tk.borda);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    if (colapsada_) return;

    auto area = getLocalBounds().withTrimmedTop(kAlturaCabecalho).reduced(tk.espacoMedio, 0);

    if (semArquivo_) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(matriz::i18n::t("metadados.vazio"), area.removeFromTop(kAlturaLinha),
                   juce::Justification::centredLeft, true);
        return;
    }

    int limite = juce::jmin(static_cast<int>(linhas_.size()), kMaxLinhasVisiveis);
    for (int i = 0; i < limite; ++i) {
        auto linha = area.removeFromTop(kAlturaLinha);
        auto areaRotulo = linha.removeFromLeft(linha.getWidth() * 2 / 5);

        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(linhas_[static_cast<size_t>(i)].rotulo, areaRotulo, juce::Justification::centredLeft, true);

        // Cor de "leitura técnica" do design system — a mesma que a ficha já
        // usa pra valor vindo de máquina, não de pessoa.
        g.setColour(tk.campoLeituraTecnica);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(linhas_[static_cast<size_t>(i)].valor, linha, juce::Justification::centredLeft, true);
    }

    if (static_cast<int>(linhas_.size()) > limite) {
        g.setColour(tk.textoTerciario);
        g.drawText(matriz::i18n::t("metadados.mais")
                       .replace("{n}", juce::String(static_cast<int>(linhas_.size()) - limite)),
                   area.removeFromTop(kAlturaLinha), juce::Justification::centredLeft, true);
    }
}

} // namespace matriz::ui
