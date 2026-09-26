#include "SendToPrintDialog.h"
#include "BatchWatermarkDialog.h"
#include "Strings.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include <algorithm>
#include <cmath>

namespace matriz::ui {

// ==============================================================================
// Lista Estática de Papéis Fotográficos Padrão
// ==============================================================================

const std::vector<DefinicaoPapel>& SendToPrintDialog::papeisPadrao() {
    static const std::vector<DefinicaoPapel> papeis = {
        { "10x15", "10x15 cm  (4x6\")", 10.0, 15.0, false },
        { "13x18", "13x18 cm  (5x7\")", 13.0, 18.0, false },
        { "15x21", "15x21 cm  (6x8\")", 15.0, 21.0, false },
        { "20x25", "20x25 cm  (8x10\")", 20.3, 25.4, false },
        { "20x30", "20x30 cm  (8x12\")", 20.0, 30.0, false },
        { "30x40", "30x40 cm  (12x16\")", 30.0, 40.0, false },
        { "a4",    "A4  (21.0 x 29.7 cm)", 21.0, 29.7, false }
    };
    return papeis;
}

// ==============================================================================
// Processamento Central e Resolução de Nomes
// ==============================================================================

matriz::imagem::ImagemBuffer SendToPrintDialog::processarFotoParaPapel(
    const matriz::imagem::ImagemBuffer& src,
    const DefinicaoPapel& papel,
    bool paisagem,
    matriz::imagem::ModoEnquadramento modo,
    float offsetX,
    float offsetY,
    float brilho,
    float contraste,
    float saturacao,
    float nitidezVal,
    double dpi,
    float temperatura) {
    if (!src.valido()) return {};

    if (papel.polaroid) {
        int papelW = papel.larguraPixels(dpi, false);
        int papelH = papel.alturaPixels(dpi, false);
        int fotoDim = static_cast<int>(std::round((7.9 / 2.54) * dpi));

        auto fotoArea = matriz::imagem::enquadrar(src, fotoDim, fotoDim, modo, offsetX, offsetY);
        matriz::imagem::aplicarAjustes(fotoArea, brilho, contraste, saturacao, 0, 255, 1.0f, temperatura);
        if (nitidezVal > 0.0f) {
            matriz::imagem::nitidez(fotoArea, nitidezVal);
        }

        matriz::imagem::ImagemBuffer papelPolaroid(papelW, papelH, 255, 255, 255, 255);
        int marginX = (papelW - fotoDim) / 2;
        int marginY = marginX;

        for (int y = 0; y < fotoDim; ++y) {
            const uint8_t* pSrc = fotoArea.pixel(0, y);
            uint8_t* pDst = papelPolaroid.pixel(marginX, marginY + y);
            std::memcpy(pDst, pSrc, static_cast<size_t>(fotoDim * 4));
        }
        return papelPolaroid;
    }

    int targetW = papel.larguraPixels(dpi, paisagem);
    int targetH = papel.alturaPixels(dpi, paisagem);

    auto processada = matriz::imagem::enquadrar(src, targetW, targetH, modo, offsetX, offsetY);
    matriz::imagem::aplicarAjustes(processada, brilho, contraste, saturacao, 0, 255, 1.0f, temperatura);
    if (nitidezVal > 0.0f) {
        matriz::imagem::nitidez(processada, nitidezVal);
    }
    return processada;
}

juce::File SendToPrintDialog::resolverColisaoArquivo(const juce::File& pasta,
                                                     const juce::String& nomeBase,
                                                     const juce::String& sufixo,
                                                     const juce::String& extensao) {
    juce::String nomeArquivo = nomeBase + sufixo + "." + extensao;
    juce::File cand = pasta.getChildFile(nomeArquivo);
    if (!cand.exists()) return cand;

    int counter = 2;
    while (true) {
        juce::String nomeComNum = nomeBase + sufixo + "_" + juce::String(counter) + "." + extensao;
        juce::File candNum = pasta.getChildFile(nomeComNum);
        if (!candNum.exists()) return candNum;
        counter++;
    }
}

// ==============================================================================
// PreviaPapelComponent (Coluna Central)
// ==============================================================================

PreviaPapelComponent::PreviaPapelComponent() {
    setRepaintsOnMouseActivity(true);
}

void PreviaPapelComponent::configurarItem(ItemFilaPrint* item,
                                         const DefinicaoPapel& papel,
                                         OrientacaoPapel orientacao,
                                         matriz::imagem::ModoEnquadramento modo,
                                         ZoomPrevia zoom,
                                         bool bypass) {
    itemAtivo_ = item;
    papelAtual_ = papel;
    orientacaoConfig_ = orientacao;
    modoEnquadramento_ = modo;
    zoom_ = zoom;
    bypass_ = bypass;
    repaint();
}

bool PreviaPapelComponent::orientacaoEfetivaEhPaisagem() const {
    if (orientacaoConfig_ == OrientacaoPapel::Paisagem) return true;
    if (orientacaoConfig_ == OrientacaoPapel::Retrato) return false;

    // Modo Automático: segue a proporção da foto ativa
    if (itemAtivo_ && itemAtivo_->valido && itemAtivo_->larguraOriginal > 0 && itemAtivo_->alturaOriginal > 0) {
        return itemAtivo_->larguraOriginal >= itemAtivo_->alturaOriginal;
    }
    return true;
}

juce::Rectangle<float> PreviaPapelComponent::calcularRetanguloPapel() const {
    auto bounds = getLocalBounds().toFloat().reduced(28.0f);
    if (bounds.isEmpty()) return {};

    bool paisagem = orientacaoEfetivaEhPaisagem();
    double wCm = paisagem ? std::max(papelAtual_.larguraCm, papelAtual_.alturaCm)
                          : std::min(papelAtual_.larguraCm, papelAtual_.alturaCm);
    double hCm = paisagem ? std::min(papelAtual_.larguraCm, papelAtual_.alturaCm)
                          : std::max(papelAtual_.larguraCm, papelAtual_.alturaCm);

    if (wCm <= 0.0 || hCm <= 0.0) {
        wCm = 15.0; hCm = 10.0;
    }

    double aspectoPapel = wCm / hCm;
    float baseW = bounds.getWidth();
    float baseH = bounds.getHeight();

    float papelW = baseW;
    float papelH = static_cast<float>(papelW / aspectoPapel);

    if (papelH > baseH) {
        papelH = baseH;
        papelW = static_cast<float>(papelH * aspectoPapel);
    }

    // Aplicação do fator de Zoom
    float fatorZoom = 1.0f;
    if (zoom_ == ZoomPrevia::Zoom100) fatorZoom = 1.35f;
    else if (zoom_ == ZoomPrevia::Zoom200) fatorZoom = 2.0f;

    papelW *= fatorZoom;
    papelH *= fatorZoom;

    float centroX = bounds.getCentreX();
    float centroY = bounds.getCentreY();

    return juce::Rectangle<float>(centroX - papelW / 2.0f, centroY - papelH / 2.0f, papelW, papelH);
}

void PreviaPapelComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto bounds = getLocalBounds().toFloat();

    // Fundo Studio Pro escuro com textura/degradê suave
    g.fillAll(juce::Colour(0xff161616));

    // Grade sutil de fundo
    g.setColour(juce::Colour(0x18ffffff));
    for (float x = 0; x < bounds.getWidth(); x += 30.0f) {
        g.drawVerticalLine(static_cast<int>(x), 0.0f, bounds.getHeight());
    }
    for (float y = 0; y < bounds.getHeight(); y += 30.0f) {
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, bounds.getWidth());
    }

    auto papelRect = calcularRetanguloPapel();
    if (papelRect.isEmpty()) return;

    // Sombra projetada do papel fotográfico
    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.fillRoundedRectangle(papelRect.translated(4.0f, 6.0f), 3.0f);

    // Folha de papel fotográfico branca
    g.setColour(juce::Colours::white);
    g.fillRoundedRectangle(papelRect, 2.0f);

    if (!itemAtivo_) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(matriz::i18n::t("print.nenhuma_foto"), bounds, juce::Justification::centred);
        return;
    }

    if (!itemAtivo_->valido) {
        g.setColour(tk.perigo.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText(itemAtivo_->motivoInvalido, papelRect.reduced(10.0f), juce::Justification::centred, true);
        return;
    }

    const juce::Image& img = (itemAtivo_->imagemPreviewAjustada.isValid() && !bypass_)
        ? itemAtivo_->imagemPreviewAjustada
        : (itemAtivo_->imagemPreview.isValid() ? itemAtivo_->imagemPreview : itemAtivo_->miniatura);
    if (!img.isValid()) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(matriz::i18n::t("print.carregando_previa"), papelRect, juce::Justification::centred);
        return;
    }

    float fotoW = static_cast<float>(img.getWidth());
    float fotoH = static_cast<float>(img.getHeight());
    if (fotoW <= 0.0f || fotoH <= 0.0f) return;

    if (modoEnquadramento_ == matriz::imagem::ModoEnquadramento::Preencher) {
        // Preencher (Fill / Crop)
        float scale = std::max(papelRect.getWidth() / fotoW, papelRect.getHeight() / fotoH);
        float sw = fotoW * scale;
        float sh = fotoH * scale;

        float excessoX = sw - papelRect.getWidth();
        float excessoY = sh - papelRect.getHeight();
        excessoPixelW_ = excessoX;
        excessoPixelH_ = excessoY;

        float posX = papelRect.getX() - (excessoX / 2.0f) + (itemAtivo_->offsetX * (excessoX / 2.0f));
        float posY = papelRect.getY() - (excessoY / 2.0f) + (itemAtivo_->offsetY * (excessoY / 2.0f));
        juce::Rectangle<float> fotoBounds(posX, posY, sw, sh);

        // 1. Desenha a área excedente fora do papel com transparência reduzida (30% alpha)
        g.saveState();
        g.excludeClipRegion(papelRect.toNearestInt());
        g.setOpacity(0.30f);
        g.drawImage(img, fotoBounds);
        g.restoreState();

        // 2. Desenha a foto recortada dentro do papel a 100% de opacidade
        g.saveState();
        g.reduceClipRegion(papelRect.toNearestInt());
        g.setOpacity(1.0f);
        g.drawImage(img, fotoBounds);
        g.restoreState();

        // 3. Linha guia de corte nas bordas do papel (borda pontilhada ciano)
        juce::Path bordaCorte;
        bordaCorte.addRoundedRectangle(papelRect, 2.0f);
        float padraoTracejado[2] = { 6.0f, 4.0f };
        juce::Path bordaTracejada;
        juce::PathStrokeType(1.5f).createDashedStroke(bordaTracejada, bordaCorte, padraoTracejado, 2);
        g.setColour(juce::Colour(0xff00d2ff).withAlpha(0.85f));
        g.strokePath(bordaTracejada, juce::PathStrokeType(1.5f));

        // Moldura sutil externa
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.drawRoundedRectangle(papelRect, 2.0f, 1.0f);
    } else {
        // Encaixar (Fit / Margens Brancas)
        excessoPixelW_ = 0.0f;
        excessoPixelH_ = 0.0f;

        float scale = std::min(papelRect.getWidth() / fotoW, papelRect.getHeight() / fotoH);
        float sw = fotoW * scale;
        float sh = fotoH * scale;

        float posX = papelRect.getX() + (papelRect.getWidth() - sw) / 2.0f;
        float posY = papelRect.getY() + (papelRect.getHeight() - sh) / 2.0f;
        juce::Rectangle<float> fotoBounds(posX, posY, sw, sh);

        // Desenha a foto centralizada sobre a folha branca
        g.saveState();
        g.reduceClipRegion(papelRect.toNearestInt());
        g.setOpacity(1.0f);
        g.drawImage(img, fotoBounds);
        g.restoreState();

        // Linha de contorno do papel
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.drawRoundedRectangle(papelRect, 2.0f, 1.0f);
    }

    if (bypass_) {
        auto tagRect = papelRect.withSizeKeepingCentre(150.0f, 22.0f).withY(papelRect.getY() + 10.0f);
        g.setColour(juce::Colours::black.withAlpha(0.80f));
        g.fillRoundedRectangle(tagRect, 4.0f);
        g.setColour(juce::Colour(0xffff8c00));
        g.drawRoundedRectangle(tagRect, 4.0f, 1.2f);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("ORIGINAL (BYPASS)", tagRect, juce::Justification::centred);
    }
}

void PreviaPapelComponent::resized() {}

void PreviaPapelComponent::mouseMove(const juce::MouseEvent& e) {
    if (modoEnquadramento_ == matriz::imagem::ModoEnquadramento::Preencher &&
        (excessoPixelW_ > 1.0f || excessoPixelH_ > 1.0f) &&
        calcularRetanguloPapel().contains(e.position)) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void PreviaPapelComponent::mouseDown(const juce::MouseEvent& e) {
    if (modoEnquadramento_ == matriz::imagem::ModoEnquadramento::Preencher &&
        (excessoPixelW_ > 1.0f || excessoPixelH_ > 1.0f) &&
        calcularRetanguloPapel().contains(e.position)) {
        arrastando_ = true;
        pontoCliqueOrigem_ = e.getPosition();
        if (itemAtivo_) {
            offsetInicialX_ = itemAtivo_->offsetX;
            offsetInicialY_ = itemAtivo_->offsetY;
        }
    }
}

void PreviaPapelComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!arrastando_ || !itemAtivo_) return;

    float deltaX = static_cast<float>(e.getPosition().x - pontoCliqueOrigem_.x);
    float deltaY = static_cast<float>(e.getPosition().y - pontoCliqueOrigem_.y);

    if (excessoPixelW_ > 1.0f) {
        float maxDeslocamentoX = excessoPixelW_ / 2.0f;
        itemAtivo_->offsetX = std::clamp(offsetInicialX_ + (deltaX / maxDeslocamentoX), -1.0f, 1.0f);
    }
    if (excessoPixelH_ > 1.0f) {
        float maxDeslocamentoY = excessoPixelH_ / 2.0f;
        itemAtivo_->offsetY = std::clamp(offsetInicialY_ + (deltaY / maxDeslocamentoY), -1.0f, 1.0f);
    }

    repaint();

    if (aoMudarOffset) {
        aoMudarOffset(itemAtivo_->offsetX, itemAtivo_->offsetY);
    }
}

void PreviaPapelComponent::mouseUp(const juce::MouseEvent&) {
    arrastando_ = false;
}

// ==============================================================================
// Thread de Exportação em Segundo Plano
// ==============================================================================

class SendToPrintDialog::ExportPrintThread : public juce::Thread {
public:
    struct ItemExport {
        juce::File arquivoOrigem;
        DefinicaoPapel papel;
        OrientacaoPapel orientacao = OrientacaoPapel::Auto;
        matriz::imagem::ModoEnquadramento modo = matriz::imagem::ModoEnquadramento::Preencher;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float brilho = 0.0f;
        float contraste = 0.0f;
        float saturacao = 1.0f;
        float nitidez = 0.0f;
        float temperatura = 0.0f;
        bool aplicarWatermark = false;
    };

    struct Params {
        juce::File pastaDestino;
        bool exportarZip = false;
        juce::String nomeProjeto;
        matriz::imagem::FormatoSaida formato = matriz::imagem::FormatoSaida::Jpeg;
        int qualidade = 95;
        double dpi = 300.0;
        ConfiguracaoWatermark configWatermark;
        std::vector<ItemExport> itens;
    };

    ExportPrintThread(Params params,
                      std::function<void(double, const juce::String&)> onProgresso,
                      std::function<void(bool, int, const juce::File&, const juce::StringArray&)> onConcluido)
        : juce::Thread("ExportPrintThread"),
          params_(std::move(params)),
          onProgresso_(std::move(onProgresso)),
          onConcluido_(std::move(onConcluido)) {}

    ~ExportPrintThread() override {
        stopThread(4000);
    }

    void run() override {
        int total = static_cast<int>(params_.itens.size());
        int exportados = 0;
        juce::StringArray erros;

        notificarProgresso(0.0, matriz::i18n::t("print.progresso_iniciando"));

        juce::File pastaDestinoEfetiva = params_.pastaDestino;
        juce::File pastaTemp;
        if (params_.exportarZip) {
            pastaTemp = juce::File::createTempFile("bkr_print_temp");
            pastaTemp.deleteFile();
            pastaTemp.createDirectory();
            pastaDestinoEfetiva = pastaTemp;
        }

        std::vector<juce::File> arquivosExportados;

        for (int i = 0; i < total; ++i) {
            if (threadShouldExit()) break;

            const auto& item = params_.itens[i];
            juce::String nomeArq = item.arquivoOrigem.getFileName();
            double p = static_cast<double>(i) / std::max(1, total);

            juce::String msg = matriz::i18n::t("print.progresso_foto")
                .replace("{n}", juce::String(i + 1))
                .replace("{total}", juce::String(total))
                .replace("{arquivo}", nomeArq);
            notificarProgresso(p, msg);

            // 1. Lê a imagem original e valida
            auto res = matriz::imagem::lerImagem(item.arquivoOrigem);
            if (!res.sucesso || !res.buffer.valido()) {
                erros.add(nomeArq + ": " + res.erro);
                continue;
            }

            // 2. Aplica orientação EXIF
            auto bufOrientado = matriz::imagem::aplicarOrientacao(res.buffer, res.orientacaoExif);

            // 3. Determina se orientação efetiva é paisagem
            bool paisagem = false;
            if (item.orientacao == OrientacaoPapel::Paisagem) paisagem = true;
            else if (item.orientacao == OrientacaoPapel::Retrato) paisagem = false;
            else paisagem = (bufOrientado.largura >= bufOrientado.altura);

            // 4. Processa enquadramento, redimensionamento 300 DPI e ajustes
            auto bufFinal = processarFotoParaPapel(
                bufOrientado,
                item.papel,
                paisagem,
                item.modo,
                item.offsetX,
                item.offsetY,
                item.brilho,
                item.contraste,
                item.saturacao,
                item.nitidez,
                params_.dpi,
                item.temperatura);

            if (!bufFinal.valido()) {
                erros.add(nomeArq + ": falha no processamento de imagem");
                continue;
            }

            bool aplicouWm = false;
            if (item.aplicarWatermark && params_.configWatermark.valida()) {
                BatchWatermarkDialog::aplicarMarcaDaguaEmBuffer(bufFinal, params_.configWatermark);
                aplicouWm = true;
            }

            // 5. Determina nome e caminho do arquivo de saída (nomedoarquivo_(print_10x15cm).jpg ou nomedoarquivo_(print_4x6in).jpg ou nomedoarquivo_(print_A4).jpg)
            juce::String nomeBase = item.arquivoOrigem.getFileNameWithoutExtension();
            if (aplicouWm) {
                nomeBase += "_w";
            }
            bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
            juce::String sufixo;
            if (item.papel.id.equalsIgnoreCase("a4")) {
                sufixo = "_(print_A4)";
            } else if (!isPt) {
                juce::String formatoInches;
                if (item.papel.id == "10x15") formatoInches = "4x6in";
                else if (item.papel.id == "13x18") formatoInches = "5x7in";
                else if (item.papel.id == "15x21") formatoInches = "6x8in";
                else if (item.papel.id == "20x25") formatoInches = "8x10in";
                else if (item.papel.id == "20x30") formatoInches = "8x12in";
                else if (item.papel.id == "30x40") formatoInches = "12x16in";
                else formatoInches = item.papel.id + "in";
                sufixo = "_(print_" + formatoInches + ")";
            } else {
                sufixo = "_(print_" + item.papel.id + "cm)";
            }
            juce::String ext = (params_.formato == matriz::imagem::FormatoSaida::Jpeg) ? "jpg" : "png";

            juce::File arquivoDestino = resolverColisaoArquivo(pastaDestinoEfetiva, nomeBase, sufixo, ext);

            // 6. Grava com injeção em nível de bytes de 300 DPI
            bool gravou = matriz::imagem::gravar(
                bufFinal,
                arquivoDestino,
                params_.formato,
                params_.qualidade,
                params_.dpi);

            if (gravou) {
                exportados++;
                arquivosExportados.push_back(arquivoDestino);
            } else {
                erros.add(nomeArq + ": falha ao gravar arquivo em disco");
            }
        }

        juce::File resultadoFinal = params_.pastaDestino;

        // Se solicitado empacotamento ZIP
        if (params_.exportarZip && exportados > 0 && !threadShouldExit()) {
            notificarProgresso(0.95, matriz::i18n::t("zip.progresso_finalizando"));

            juce::String dataHora = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
            juce::String nomeBaseZip = "Print_" + (params_.nomeProjeto.isNotEmpty() ? params_.nomeProjeto + "_" : "") + dataHora;
            juce::File arquivoZip = resolverColisaoArquivo(params_.pastaDestino, nomeBaseZip, "", "zip");

            juce::ZipFile::Builder builder;
            for (const auto& f : arquivosExportados) {
                builder.addFile(f, 0, f.getFileName()); // Arquivos já comprimidos -> nível 0
            }

            if (arquivoZip.exists()) arquivoZip.deleteFile();
            auto outStream = arquivoZip.createOutputStream();
            if (outStream && !outStream->failedToOpen()) {
                builder.writeToStream(*outStream, nullptr);
                outStream.reset();
                resultadoFinal = arquivoZip;
            } else {
                erros.add("Falha ao criar arquivo ZIP no destino");
            }
            pastaTemp.deleteRecursively();
        }

        notificarProgresso(1.0, matriz::i18n::t("print.concluido"));
        notificarFim(!threadShouldExit() && exportados > 0, exportados, resultadoFinal, erros);
    }

private:
    // callAsync captura uma CÓPIA do std::function (não `this`): o destrutor
    // do diálogo faz stopThread() e destrói este worker logo em seguida, mas
    // stopThread() não espera a fila de mensagens drenar — um callAsync
    // disparado nas últimas linhas de run() pode rodar depois deste objeto
    // já ter sido liberado. onProgresso_/onConcluido_ já carregam seu
    // próprio SafePointer pro diálogo (ver iniciarExportacao), então a cópia
    // é auto-suficiente e segura mesmo com o worker e o diálogo mortos.
    void notificarProgresso(double p, const juce::String& msg) {
        auto callback = onProgresso_;
        juce::MessageManager::callAsync([callback, p, msg] {
            if (callback) callback(p, msg);
        });
    }

    void notificarFim(bool sucesso, int totalExportados, const juce::File& pasta, const juce::StringArray& erros) {
        auto callback = onConcluido_;
        juce::MessageManager::callAsync([callback, sucesso, totalExportados, pasta, erros] {
            if (callback) callback(sucesso, totalExportados, pasta, erros);
        });
    }

    Params params_;
    std::function<void(double, const juce::String&)> onProgresso_;
    std::function<void(bool, int, const juce::File&, const juce::StringArray&)> onConcluido_;
};

// ==============================================================================
// SendToPrintDialog (Diálogo Pro de 3 Colunas)
// ==============================================================================

SendToPrintDialog::SendToPrintDialog(ProjetoAberto& projeto)
    : projeto_(projeto) {
    const auto& tk = tema();
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    // Defaults
    pastaDestinoSelecionada_ = juce::File::getSpecialLocation(juce::File::userPicturesDirectory);
    if (!pastaDestinoSelecionada_.isDirectory()) {
        pastaDestinoSelecionada_ = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    }

    // =========================================================================
    // Coluna 1 (Esquerda): Fila de Impressão
    // =========================================================================
    lblFilaTitulo_ = std::make_unique<juce::Label>("lblFilaTitulo", matriz::i18n::t("print.fila_titulo").replace("{n}", "0"));
    lblFilaTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblFilaTitulo_->setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(*lblFilaTitulo_);

    listaFila_ = std::make_unique<juce::ListBox>("listaFila", this);
    listaFila_->setRowHeight(76);
    listaFila_->setColour(juce::ListBox::backgroundColourId, tk.painel);
    listaFila_->setColour(juce::ListBox::outlineColourId, tk.borda);
    addAndMakeVisible(*listaFila_);

    // =========================================================================
    // Coluna 2 (Centro): Prévia do Papel & Controles de Zoom
    // =========================================================================
    painelCentroContainer_ = std::make_unique<juce::Component>();
    addAndMakeVisible(*painelCentroContainer_);

    lblInfoPapel_ = std::make_unique<juce::Label>("lblInfoPapel", "");
    lblInfoPapel_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblInfoPapel_->setColour(juce::Label::textColourId, tk.textoSecundario);
    painelCentroContainer_->addAndMakeVisible(*lblInfoPapel_);

    btnZoomAjustar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.zoom_ajustar"));
    btnZoomAjustar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnZoomAjustar_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnZoomAjustar_->onClick = [this] {
        zoomAtual_ = ZoomPrevia::AjustarJanela;
        atualizarDetalhesFotoAtiva();
    };
    painelCentroContainer_->addAndMakeVisible(*btnZoomAjustar_);

    btnZoom100_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.zoom_100"));
    btnZoom100_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnZoom100_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnZoom100_->onClick = [this] {
        zoomAtual_ = ZoomPrevia::Zoom100;
        atualizarDetalhesFotoAtiva();
    };
    painelCentroContainer_->addAndMakeVisible(*btnZoom100_);

    btnZoom200_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.zoom_200"));
    btnZoom200_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnZoom200_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnZoom200_->onClick = [this] {
        zoomAtual_ = ZoomPrevia::Zoom200;
        atualizarDetalhesFotoAtiva();
    };
    painelCentroContainer_->addAndMakeVisible(*btnZoom200_);

    previaPapel_ = std::make_unique<PreviaPapelComponent>();
    previaPapel_->aoMudarOffset = [this](float, float) {
        // Callback quando o usuário arrasta a foto na prévia
    };
    painelCentroContainer_->addAndMakeVisible(*previaPapel_);

    lblDicaArrastar_ = std::make_unique<juce::Label>("lblDicaArrastar", matriz::i18n::t("print.dica_arrastar"));
    lblDicaArrastar_->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
    lblDicaArrastar_->setColour(juce::Label::textColourId, tk.textoTerciario);
    lblDicaArrastar_->setJustificationType(juce::Justification::centred);
    painelCentroContainer_->addAndMakeVisible(*lblDicaArrastar_);

    // =========================================================================
    // Coluna 3 (Direita): Painel de Configurações
    // =========================================================================

    // 1. Grupo Papel & Enquadramento
    grpPapel_ = std::make_unique<juce::GroupComponent>("grpPapel", matriz::i18n::t("print.papel_enquadramento"));
    grpPapel_->setColour(juce::GroupComponent::outlineColourId, tk.borda);
    grpPapel_->setColour(juce::GroupComponent::textColourId, tk.acento);
    addAndMakeVisible(*grpPapel_);

    lblTamanhoPapel_ = std::make_unique<juce::Label>("lblTamanhoPapel", matriz::i18n::t("print.tamanho_papel"));
    lblTamanhoPapel_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblTamanhoPapel_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTamanhoPapel_);

    cboTamanhoPapel_ = std::make_unique<juce::ComboBox>("cboTamanhoPapel");
    const auto& papeis = papeisPadrao();
    for (size_t i = 0; i < papeis.size(); ++i) {
        cboTamanhoPapel_->addItem(papeis[i].nome, static_cast<int>(i + 1));
    }
    cboTamanhoPapel_->setSelectedId(1, juce::dontSendNotification); // 10x15 padrão
    cboTamanhoPapel_->onChange = [this] {
        if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
            fila_[indiceSelecionado_].papelIndex = cboTamanhoPapel_->getSelectedId() - 1;
        }
        atualizarDetalhesFotoAtiva();
    };
    addAndMakeVisible(*cboTamanhoPapel_);

    lblOrientacao_ = std::make_unique<juce::Label>("lblOrientacao", matriz::i18n::t("print.orientacao"));
    lblOrientacao_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblOrientacao_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblOrientacao_);

    cboOrientacao_ = std::make_unique<juce::ComboBox>("cboOrientacao");
    cboOrientacao_->addItem(matriz::i18n::t("print.orientacao_auto"), static_cast<int>(OrientacaoPapel::Auto));
    cboOrientacao_->addItem(matriz::i18n::t("print.orientacao_paisagem"), static_cast<int>(OrientacaoPapel::Paisagem));
    cboOrientacao_->addItem(matriz::i18n::t("print.orientacao_retrato"), static_cast<int>(OrientacaoPapel::Retrato));
    cboOrientacao_->setSelectedId(static_cast<int>(OrientacaoPapel::Auto), juce::dontSendNotification);
    cboOrientacao_->onChange = [this] {
        if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
            fila_[indiceSelecionado_].orientacao = static_cast<OrientacaoPapel>(cboOrientacao_->getSelectedId());
        }
        atualizarDetalhesFotoAtiva();
    };
    addAndMakeVisible(*cboOrientacao_);

    lblModoEnquadramento_ = std::make_unique<juce::Label>("lblModoEnquadramento", matriz::i18n::t("print.modo_enquadramento"));
    lblModoEnquadramento_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblModoEnquadramento_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblModoEnquadramento_);

    radPreencher_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("print.modo_preencher"));
    radPreencher_->setRadioGroupId(1001);
    radPreencher_->setToggleState(true, juce::dontSendNotification);
    radPreencher_->onClick = [this] {
        if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
            fila_[indiceSelecionado_].modoEnquadramento = matriz::imagem::ModoEnquadramento::Preencher;
        }
        atualizarDetalhesFotoAtiva();
    };
    addAndMakeVisible(*radPreencher_);

    radEncaixar_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("print.modo_encaixar"));
    radEncaixar_->setRadioGroupId(1001);
    radEncaixar_->onClick = [this] {
        if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
            fila_[indiceSelecionado_].modoEnquadramento = matriz::imagem::ModoEnquadramento::Encaixar;
        }
        atualizarDetalhesFotoAtiva();
    };
    addAndMakeVisible(*radEncaixar_);

    lblResolucaoInfo_ = std::make_unique<juce::Label>("lblResolucaoInfo", matriz::i18n::t("print.resolucao_info"));
    lblResolucaoInfo_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblResolucaoInfo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblResolucaoInfo_);

    // 2. Grupo Pasta Destino
    grpDestino_ = std::make_unique<juce::GroupComponent>("grpDestino", matriz::i18n::t("print.pasta_destino"));
    grpDestino_->setColour(juce::GroupComponent::outlineColourId, tk.borda);
    grpDestino_->setColour(juce::GroupComponent::textColourId, tk.acento);
    addAndMakeVisible(*grpDestino_);

    lblCaminhoDestino_ = std::make_unique<juce::Label>("lblCaminhoDestino", pastaDestinoSelecionada_.getFullPathName());
    lblCaminhoDestino_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblCaminhoDestino_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addAndMakeVisible(*lblCaminhoDestino_);

    btnEscolherPasta_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.escolher_pasta"));
    btnEscolherPasta_->onClick = [this] { escolherPastaDestino(); };
    addAndMakeVisible(*btnEscolherPasta_);

    chkExportarZip_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("print.exportar_zip"));
    chkExportarZip_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    addAndMakeVisible(*chkExportarZip_);

    // 3. Grupo Ajustes Rápidos (Sliders)
    grpAjustes_ = std::make_unique<juce::GroupComponent>("grpAjustes", matriz::i18n::t("print.ajustes_rapidos"));
    grpAjustes_->setColour(juce::GroupComponent::outlineColourId, tk.borda);
    grpAjustes_->setColour(juce::GroupComponent::textColourId, tk.acento);
    addAndMakeVisible(*grpAjustes_);

    lblBrilho_ = std::make_unique<juce::Label>("lblBrilho", matriz::i18n::t("print.brilho"));
    lblBrilho_->setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(*lblBrilho_);

    sldBrilho_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    sldBrilho_->setRange(-100.0, 100.0, 1.0);
    sldBrilho_->setValue(0.0, juce::dontSendNotification);
    sldBrilho_->onValueChange = [this] {
        if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
            fila_[indiceSelecionado_].brilho = static_cast<float>(sldBrilho_->getValue() / 100.0);
            atualizarPreviewAjustada();
        }
    };
    addAndMakeVisible(*sldBrilho_);

    lblContraste_ = std::make_unique<juce::Label>("lblContraste", matriz::i18n::t("print.contraste"));
    lblContraste_->setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(*lblContraste_);

    sldContraste_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    sldContraste_->setRange(-100.0, 100.0, 1.0);
    sldContraste_->setValue(0.0, juce::dontSendNotification);
    sldContraste_->onValueChange = [this] {
        if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
            fila_[indiceSelecionado_].contraste = static_cast<float>(sldContraste_->getValue() / 100.0);
            atualizarPreviewAjustada();
        }
    };
    addAndMakeVisible(*sldContraste_);

    lblSaturacao_ = std::make_unique<juce::Label>("lblSaturacao", matriz::i18n::t("print.saturacao"));
    lblSaturacao_->setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(*lblSaturacao_);

    sldSaturacao_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    sldSaturacao_->setRange(0.0, 200.0, 1.0);
    sldSaturacao_->setValue(100.0, juce::dontSendNotification);
    sldSaturacao_->onValueChange = [this] {
        if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
            fila_[indiceSelecionado_].saturacao = static_cast<float>(sldSaturacao_->getValue() / 100.0);
            atualizarPreviewAjustada();
        }
    };
    addAndMakeVisible(*sldSaturacao_);

    lblNitidez_ = std::make_unique<juce::Label>("lblNitidez", matriz::i18n::t("print.nitidez"));
    lblNitidez_->setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(*lblNitidez_);

    sldNitidez_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    sldNitidez_->setRange(0.0, 100.0, 1.0);
    sldNitidez_->setValue(0.0, juce::dontSendNotification);
    sldNitidez_->onValueChange = [this] {
        if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
            fila_[indiceSelecionado_].nitidez = static_cast<float>(sldNitidez_->getValue() / 100.0);
            atualizarPreviewAjustada();
        }
    };
    addAndMakeVisible(*sldNitidez_);

    lblTemperatura_ = std::make_unique<juce::Label>("lblTemperatura", isPt ? juce::String::fromUTF8("Temperatura") : "Temperature");
    lblTemperatura_->setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(*lblTemperatura_);

    sldTemperatura_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    sldTemperatura_->setRange(-100.0, 100.0, 1.0);
    sldTemperatura_->setValue(0.0, juce::dontSendNotification);
    sldTemperatura_->onValueChange = [this] {
        if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
            fila_[indiceSelecionado_].temperaturaCor = static_cast<float>(sldTemperatura_->getValue() / 100.0);
            atualizarPreviewAjustada();
        }
    };
    addAndMakeVisible(*sldTemperatura_);

    btnResetarAjustes_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.resetar_ajustes"));
    btnResetarAjustes_->onClick = [this] { resetarAjustes(); };
    addAndMakeVisible(*btnResetarAjustes_);

    btnBypass_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.bypass"));
    btnBypass_->setClickingTogglesState(true);
    btnBypass_->onClick = [this] {
        const auto& tk = tema();
        bypassAtivo_ = btnBypass_->getToggleState();
        btnBypass_->setButtonText(bypassAtivo_ ? matriz::i18n::t("print.bypass_ativo") : matriz::i18n::t("print.bypass"));
        btnBypass_->setColour(juce::TextButton::buttonColourId, bypassAtivo_ ? juce::Colour(0xffff6b00) : tk.painelAlt);
        btnBypass_->setColour(juce::TextButton::textColourOffId, bypassAtivo_ ? juce::Colours::white : tk.textoPrimario);
        atualizarDetalhesFotoAtiva();
    };
    addAndMakeVisible(*btnBypass_);

    // =========================================================================
    // Barra Inferior
    // =========================================================================
    btnLimparLista_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.limpar_lista"));
    btnLimparLista_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    btnLimparLista_->onClick = [this] { limparListaPrint(); };
    addAndMakeVisible(*btnLimparLista_);

    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    barraProgresso_->setColour(juce::ProgressBar::foregroundColourId, tk.acento);
    addChildComponent(*barraProgresso_);

    lblStatusProgresso_ = std::make_unique<juce::Label>("lblStatusProgresso", "");
    lblStatusProgresso_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblStatusProgresso_->setColour(juce::Label::textColourId, tk.textoSecundario);
    addChildComponent(*lblStatusProgresso_);

    btnCancelar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.btn_cancelar"));
    btnCancelar_->onClick = [this] {
        if (exportando_) cancelarExportacao();
        else fecharDialogo();
    };
    addAndMakeVisible(*btnCancelar_);

    btnExportar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.btn_exportar").replace("{n}", "0"));
    btnExportar_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xffff6b00));
    btnExportar_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnExportar_->onClick = [this] { iniciarExportacao(); };
    addAndMakeVisible(*btnExportar_);

    carregarFila();
    setSize(1240, 760);
}

SendToPrintDialog::~SendToPrintDialog() {
    if (threadExportacao_) {
        threadExportacao_->stopThread(4000);
        threadExportacao_.reset();
    }
    poolCarregamento_.removeAllJobs(true, 2000);
}

void SendToPrintDialog::paint(juce::Graphics& g) {
    g.fillAll(tema().painel);

    auto bounds = getLocalBounds();
    const int kBottomBarH = 60;
    int sepY = bounds.getHeight() - kBottomBarH;

    // Linhas verticais separadoras sutis entre as 3 colunas
    g.setColour(tema().borda.withAlpha(0.6f));
    int col1W = 340;
    int col3W = 280;
    g.drawVerticalLine(col1W + 16, 16.0f, static_cast<float>(sepY));
    g.drawVerticalLine(bounds.getWidth() - col3W - 16, 16.0f, static_cast<float>(sepY));

    // Linha horizontal separadora da barra inferior
    g.drawHorizontalLine(sepY, 16.0f, static_cast<float>(bounds.getWidth() - 16));
}

void SendToPrintDialog::resized() {
    auto bounds = getLocalBounds();
    const int kBottomBarH = 60;
    const int kBtnH = 34;
    // Centralizado perfeitamente entre a linha divisória (sepY) e a borda da janela
    int btnY = bounds.getHeight() - kBottomBarH + (kBottomBarH - kBtnH) / 2;

    btnLimparLista_->setBounds(16, btnY, 120, kBtnH);

    int exportW = 210;
    int cancelW = 100;
    int rightX = bounds.getWidth() - 16;
    btnExportar_->setBounds(rightX - exportW, btnY, exportW, kBtnH);
    btnCancelar_->setBounds(rightX - exportW - 12 - cancelW, btnY, cancelW, kBtnH);

    int statusLeft = 16 + 120 + 16;
    int statusRight = rightX - exportW - 12 - cancelW - 16;
    int statusW = std::max(0, statusRight - statusLeft);
    if (statusW > 0) {
        auto statusArea = juce::Rectangle<int>(statusLeft, bounds.getHeight() - kBottomBarH + 12, statusW, kBottomBarH - 24);
        barraProgresso_->setBounds(statusArea.removeFromTop(12));
        statusArea.removeFromTop(2);
        lblStatusProgresso_->setBounds(statusArea);
    }

    auto area = bounds.reduced(16);
    area.removeFromBottom(kBottomBarH - 16 + 8); // Margem de respiro acima da linha divisória

    // Coluna 1 (Esquerda) - 340px
    int col1W = 340;
    auto col1 = area.removeFromLeft(col1W);
    lblFilaTitulo_->setBounds(col1.removeFromTop(28));
    col1.removeFromTop(6);
    listaFila_->setBounds(col1);

    area.removeFromLeft(16); // Espaçador coluna 1-2

    // Coluna 3 (Direita) - 280px
    int col3W = 280;
    auto col3 = area.removeFromRight(col3W);

    // Grupo Papel & Enquadramento
    auto grpPapelBounds = col3.removeFromTop(200);
    grpPapel_->setBounds(grpPapelBounds);
    auto innerPapel = grpPapelBounds.reduced(12, 10);
    innerPapel.removeFromTop(16);

    auto rowPapel = innerPapel.removeFromTop(26);
    lblTamanhoPapel_->setBounds(rowPapel.removeFromLeft(100));
    cboTamanhoPapel_->setBounds(rowPapel);

    innerPapel.removeFromTop(6);
    auto rowOri = innerPapel.removeFromTop(26);
    lblOrientacao_->setBounds(rowOri.removeFromLeft(100));
    cboOrientacao_->setBounds(rowOri);

    innerPapel.removeFromTop(6);
    lblModoEnquadramento_->setBounds(innerPapel.removeFromTop(20));
    radPreencher_->setBounds(innerPapel.removeFromTop(22));
    radEncaixar_->setBounds(innerPapel.removeFromTop(22));
    innerPapel.removeFromTop(4);
    lblResolucaoInfo_->setBounds(innerPapel.removeFromTop(18));

    col3.removeFromTop(10);

    // Grupo Destino
    auto grpDestBounds = col3.removeFromTop(122);
    grpDestino_->setBounds(grpDestBounds);
    auto innerDest = grpDestBounds.reduced(12, 10);
    innerDest.removeFromTop(16);
    lblCaminhoDestino_->setBounds(innerDest.removeFromTop(20));
    btnEscolherPasta_->setBounds(innerDest.removeFromTop(26));
    innerDest.removeFromTop(6);
    chkExportarZip_->setBounds(innerDest.removeFromTop(24));

    col3.removeFromTop(10);

    // Grupo Ajustes Rápidos
    auto grpAjustesBounds = col3;
    grpAjustes_->setBounds(grpAjustesBounds);
    auto innerAj = grpAjustesBounds.reduced(12, 10);
    innerAj.removeFromTop(16);

    auto rowBrilho = innerAj.removeFromTop(24);
    lblBrilho_->setBounds(rowBrilho.removeFromLeft(70));
    sldBrilho_->setBounds(rowBrilho);

    innerAj.removeFromTop(4);
    auto rowContraste = innerAj.removeFromTop(24);
    lblContraste_->setBounds(rowContraste.removeFromLeft(70));
    sldContraste_->setBounds(rowContraste);

    innerAj.removeFromTop(4);
    auto rowSat = innerAj.removeFromTop(24);
    lblSaturacao_->setBounds(rowSat.removeFromLeft(70));
    sldSaturacao_->setBounds(rowSat);

    innerAj.removeFromTop(4);
    auto rowNitidez = innerAj.removeFromTop(24);
    lblNitidez_->setBounds(rowNitidez.removeFromLeft(70));
    sldNitidez_->setBounds(rowNitidez);

    innerAj.removeFromTop(4);
    auto rowTemp = innerAj.removeFromTop(24);
    lblTemperatura_->setBounds(rowTemp.removeFromLeft(70));
    sldTemperatura_->setBounds(rowTemp);

    innerAj.removeFromTop(8);
    auto rowButtons = innerAj.removeFromTop(26);
    btnResetarAjustes_->setBounds(rowButtons.removeFromLeft((rowButtons.getWidth() - 8) / 2));
    rowButtons.removeFromLeft(8);
    btnBypass_->setBounds(rowButtons);

    area.removeFromRight(16); // Espaçador coluna 2-3

    // Coluna 2 (Centro) - O restante da área central
    painelCentroContainer_->setBounds(area);

    auto centroBounds = painelCentroContainer_->getLocalBounds();
    auto topBar = centroBounds.removeFromTop(32);

    btnZoom200_->setBounds(topBar.removeFromRight(55));
    topBar.removeFromRight(4);
    btnZoom100_->setBounds(topBar.removeFromRight(55));
    topBar.removeFromRight(4);
    btnZoomAjustar_->setBounds(topBar.removeFromRight(70));
    topBar.removeFromRight(12);
    lblInfoPapel_->setBounds(topBar);

    centroBounds.removeFromTop(6);
    auto bottomHint = centroBounds.removeFromBottom(22);
    lblDicaArrastar_->setBounds(bottomHint);
    centroBounds.removeFromBottom(4);

    previaPapel_->setBounds(centroBounds);
}

void SendToPrintDialog::lookAndFeelChanged() {
    repaint();
}

bool SendToPrintDialog::keyPressed(const juce::KeyPress& key) {
    if (key.getKeyCode() == juce::KeyPress::escapeKey) {
        fecharDialogo();
        return true;
    }
    return false;
}

// ==============================================================================
// ListBoxModel (Coluna 1)
// ==============================================================================

int SendToPrintDialog::getNumRows() {
    return static_cast<int>(fila_.size());
}

void SendToPrintDialog::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(fila_.size())) return;

    const auto& tk = tema();
    const auto& item = fila_[rowNumber];

    juce::Rectangle<int> rowBounds(0, 0, width, height);
    auto cardBounds = rowBounds.reduced(2);

    // Card Container (Estilo DISK)
    if (rowIsSelected) {
        g.setColour(tk.painelAlt);
    } else {
        g.setColour(tk.painel);
    }
    g.fillRoundedRectangle(cardBounds.toFloat(), 6.0f);

    if (rowIsSelected) {
        g.setColour(tk.acento);
        g.drawRoundedRectangle(cardBounds.toFloat(), 6.0f, 1.5f);
    } else {
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cardBounds.toFloat(), 6.0f, 1.0f);
    }

    auto content = cardBounds.reduced(8, 7);

    // Thumbnail quadrado à esquerda (58x58)
    auto thumbRect = content.removeFromLeft(58);
    if (item.miniatura.isValid()) {
        g.drawImage(item.miniatura, thumbRect.toFloat(), juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    } else {
        g.setColour(tk.fundo);
        g.fillRoundedRectangle(thumbRect.toFloat(), 4.0f);
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(item.carregado ? "SEM" : "...", thumbRect, juce::Justification::centred);
    }
    g.setColour(tk.borda);
    g.drawRoundedRectangle(thumbRect.toFloat(), 4.0f, 1.0f);

    content.removeFromLeft(10);

    // Linha 1: Nome do Arquivo com índice # + Papel à direita
    auto headerRow = content.removeFromTop(18);

    int pIdx = std::clamp(item.papelIndex, 0, static_cast<int>(papeisPadrao().size()) - 1);
    const auto& papelItem = papeisPadrao()[pIdx];
    juce::String papelTag = papelItem.id.equalsIgnoreCase("a4") ? "A4" : (papelItem.id + " cm");

    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.setColour(tk.textoSecundario);
    g.drawText(papelTag, headerRow.removeFromRight(65), juce::Justification::centredRight);

    g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    g.setColour(rowIsSelected ? tk.acento : (item.valido ? tk.textoPrimario : tk.textoTerciario));
    g.drawText(juce::String(rowNumber + 1) + ". " + item.nomeArquivo, headerRow, juce::Justification::centredLeft, true);

    // Divisor sutil estilo DISK
    content.removeFromTop(3);
    auto div = content.removeFromTop(1);
    g.setColour(tk.borda.withAlpha(0.4f));
    g.fillRect(div);
    content.removeFromTop(3);

    // Linha 2: Dimensões originais
    auto dimRow = content.removeFromTop(16);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.setColour(tk.textoTerciario);
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    g.drawText(isPt ? juce::String::fromUTF8("Dimensões: ") : "Dimensions: ", dimRow.removeFromLeft(68), juce::Justification::centredLeft);

    g.setColour(tk.textoPrimario);
    if (item.larguraOriginal > 0 && item.alturaOriginal > 0) {
        g.drawText(juce::String(item.larguraOriginal) + " x " + juce::String(item.alturaOriginal) + " px",
                   dimRow, juce::Justification::centredLeft, true);
    } else if (!item.carregado) {
        g.setColour(tk.textoTerciario);
        g.drawText(isPt ? juce::String::fromUTF8("Carregando...") : "Loading...", dimRow, juce::Justification::centredLeft);
    } else {
        g.setColour(tk.textoTerciario);
        g.drawText("-", dimRow, juce::Justification::centredLeft);
    }

    // Linha 3: Status / DPI
    auto statusRow = content.removeFromTop(16);
    if (item.carregado && item.valido) {
        bool paisagem = (item.orientacao == OrientacaoPapel::Paisagem) ||
                        (item.orientacao == OrientacaoPapel::Auto && item.larguraOriginal >= item.alturaOriginal);
        int targetW = papelItem.larguraPixels(300.0, paisagem);
        int targetH = papelItem.alturaPixels(300.0, paisagem);
        int dpiEfetivo = item.calcularDpiEfetivo(targetW, targetH);

        auto dotRect = statusRow.removeFromLeft(12).toFloat().withSizeKeepingCentre(6.0f, 6.0f);
        if (dpiEfetivo >= 240) {
            g.setColour(juce::Colour(0xff22c55e)); // Verde limpo
            g.fillEllipse(dotRect);
            g.setFont(juce::Font(juce::FontOptions(11.5f)));
            g.setColour(tk.textoSecundario);
            g.drawText(isPt ? juce::String::fromUTF8("300 DPI · Pronto para impressão") : "300 DPI · Print ready",
                       statusRow, juce::Justification::centredLeft);
        } else {
            g.setColour(juce::Colour(0xffb87a1a)); // Âmbar legível de alto contraste
            g.fillEllipse(dotRect);
            g.setFont(juce::Font(juce::FontOptions(11.5f)));
            g.setColour(tk.textoPrimario);
            g.drawText(juce::String(dpiEfetivo) + (isPt ? juce::String::fromUTF8(" DPI · Resolução baixa") : " DPI · Low resolution"),
                       statusRow, juce::Justification::centredLeft);
        }
    } else if (!item.carregado) {
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.setColour(tk.textoTerciario);
        g.drawText(isPt ? juce::String::fromUTF8("Aguardando leitura...") : "Waiting...", statusRow, juce::Justification::centredLeft);
    } else {
        auto dotRect = statusRow.removeFromLeft(12).toFloat().withSizeKeepingCentre(6.0f, 6.0f);
        g.setColour(tk.perigo);
        g.fillEllipse(dotRect);
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.setColour(tk.perigo);
        g.drawText(item.motivoInvalido, statusRow, juce::Justification::centredLeft, true);
    }
}

void SendToPrintDialog::selectedRowsChanged(int lastRowSelected) {
    selecionarFoto(lastRowSelected);
}

// ==============================================================================
// Lógica de Carregamento, Validação e Seleção
// ==============================================================================

void SendToPrintDialog::carregarFila() {
    fila_.clear();
    auto ids = projeto_.idsMarcados(ProjetoAberto::TipoMarcacao::Print);

    for (const auto& id : ids) {
        ItemFilaPrint item;
        item.id = id;

        auto arqInfo = projeto_.arquivoPrincipal(id);
        if (!arqInfo) {
            item.valido = false;
            item.carregado = true;
            item.motivoInvalido = matriz::i18n::t("print.status_ausente");
            item.nomeArquivo = id;
            fila_.push_back(std::move(item));
            continue;
        }

        juce::File f(arqInfo->caminhoAbsoluto);
        item.arquivo = f;
        item.nomeArquivo = f.getFileName();

        if (!f.existsAsFile()) {
            item.valido = false;
            item.carregado = true;
            item.motivoInvalido = matriz::i18n::t("print.status_ausente");
            fila_.push_back(std::move(item));
            continue;
        }

        auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".mp4" || ext == ".mov" || ext == ".mkv" || ext == ".avi" || ext == ".wmv" || ext == ".m4v") {
            item.valido = false;
            item.carregado = true;
            item.motivoInvalido = matriz::i18n::t("print.status_video");
            fila_.push_back(std::move(item));
            continue;
        }

        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aif" || ext == ".aiff" || ext == ".m4a") {
            item.valido = false;
            item.carregado = true;
            item.motivoInvalido = matriz::i18n::t("print.status_audio");
            fila_.push_back(std::move(item));
            continue;
        }

        if (ext == ".tif" || ext == ".tiff") {
            item.valido = false;
            item.carregado = true;
            item.motivoInvalido = matriz::i18n::t("print.status_tiff");
            fila_.push_back(std::move(item));
            continue;
        }

        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") {
            item.valido = false;
            item.carregado = true;
            item.motivoInvalido = matriz::i18n::t("print.status_nao_suportado");
            fila_.push_back(std::move(item));
            continue;
        }

        // Inicialmente adicionado para carregamento assíncrono em segundo plano
        item.valido = false;
        item.carregado = false;
        fila_.push_back(std::move(item));
    }

    lblFilaTitulo_->setText(matriz::i18n::t("print.fila_titulo").replace("{n}", juce::String(static_cast<int>(fila_.size()))), juce::dontSendNotification);
    btnExportar_->setButtonText(matriz::i18n::t("print.btn_exportar").replace("{n}", "0"));
    btnExportar_->setEnabled(false);

    listaFila_->updateContent();

    if (!fila_.empty()) {
        selecionarFoto(0);
        listaFila_->selectRow(0);
    } else {
        selecionarFoto(-1);
    }

    iniciarCarregamentoAssincrono();
}

void SendToPrintDialog::iniciarCarregamentoAssincrono() {
    int total = static_cast<int>(fila_.size());
    if (total == 0) return;

    barraProgresso_->setVisible(true);
    lblStatusProgresso_->setVisible(true);
    progressoValor_ = 0.0;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    lblStatusProgresso_->setText(isPt ? juce::String::fromUTF8("Carregando fotos...") : "Loading photos...", juce::dontSendNotification);

    juce::Component::SafePointer<SendToPrintDialog> safeThis(this);

    poolCarregamento_.addJob([safeThis, total, isPt]() {
        for (int i = 0; i < total; ++i) {
            if (!safeThis) return;

            juce::File f;
            bool precisaCarregar = false;

            {
                // Com o job: desiste da espera quando o pool pede pra parar
                // (removeAllJobs no destrutor, que roda NA message thread).
                // Sem isso job e destrutor se esperavam até o JUCE matar a
                // thread à força ("!! killing thread by force !!").
                const juce::MessageManagerLock mml(juce::ThreadPoolJob::getCurrentThreadPoolJob());
                if (!mml.lockWasGained() || !safeThis) return;
                if (i < static_cast<int>(safeThis->fila_.size())) {
                    if (!safeThis->fila_[i].carregado && safeThis->fila_[i].arquivo.existsAsFile()) {
                        f = safeThis->fila_[i].arquivo;
                        precisaCarregar = true;
                    }
                }
            }

            if (!precisaCarregar || !f.existsAsFile()) continue;

            auto res = matriz::imagem::lerImagem(f);
            juce::Image tImg;
            juce::Image pImg;
            matriz::imagem::ImagemBuffer prevBuf;

            if (res.sucesso && res.buffer.valido()) {
                int thumbW = 104;
                int thumbH = static_cast<int>(std::round(thumbW * (static_cast<double>(res.buffer.altura) / res.buffer.largura)));
                if (thumbH <= 0) thumbH = 104;
                auto thumbBuf = matriz::imagem::redimensionar(res.buffer, thumbW, thumbH);

                tImg = juce::Image(juce::Image::RGB, thumbBuf.largura, thumbBuf.altura, false);
                for (int y = 0; y < thumbBuf.altura; ++y) {
                    for (int x = 0; x < thumbBuf.largura; ++x) {
                        const uint8_t* p = thumbBuf.pixel(x, y);
                        tImg.setPixelAt(x, y, juce::Colour(p[0], p[1], p[2]));
                    }
                }

                int prevW = std::min(res.buffer.largura, 1200);
                int prevH = static_cast<int>(std::round(prevW * (static_cast<double>(res.buffer.altura) / res.buffer.largura)));
                if (prevH <= 0) prevH = prevW;
                prevBuf = matriz::imagem::redimensionar(res.buffer, prevW, prevH);

                pImg = juce::Image(juce::Image::RGB, prevBuf.largura, prevBuf.altura, false);
                for (int y = 0; y < prevBuf.altura; ++y) {
                    for (int x = 0; x < prevBuf.largura; ++x) {
                        const uint8_t* p = prevBuf.pixel(x, y);
                        pImg.setPixelAt(x, y, juce::Colour(p[0], p[1], p[2]));
                    }
                }
            }

            juce::MessageManager::callAsync([safeThis, i, res, tImg, pImg, prevBuf, total, f, isPt]() {
                if (!safeThis) return;
                if (i >= static_cast<int>(safeThis->fila_.size())) return;

                auto& it = safeThis->fila_[i];
                it.carregado = true;
                if (res.sucesso && res.buffer.valido()) {
                    it.valido = true;
                    it.larguraOriginal = res.buffer.largura;
                    it.alturaOriginal = res.buffer.altura;
                    it.orientacaoExif = res.orientacaoExif;
                    it.miniatura = tImg;
                    it.imagemPreview = pImg;
                    it.bufferPreviewOriginal = prevBuf;
                } else {
                    it.valido = false;
                    it.motivoInvalido = res.erro;
                }

                safeThis->progressoValor_ = static_cast<double>(i + 1) / total;
                juce::String statusMsg = (isPt ? juce::String::fromUTF8("Carregando foto ") : "Loading photo ")
                    + juce::String(i + 1) + " / " + juce::String(total) + " (" + f.getFileName() + ")";
                safeThis->lblStatusProgresso_->setText(statusMsg, juce::dontSendNotification);

                if (safeThis->indiceSelecionado_ == i) {
                    safeThis->atualizarDetalhesFotoAtiva();
                }

                safeThis->listaFila_->updateContent();
                safeThis->listaFila_->repaint();

                int fotosValidas = 0;
                for (const auto& itemCheck : safeThis->fila_) {
                    if (itemCheck.valido) fotosValidas++;
                }
                safeThis->btnExportar_->setButtonText(
                    matriz::i18n::t("print.btn_exportar").replace("{n}", juce::String(fotosValidas)));
                safeThis->btnExportar_->setEnabled(fotosValidas > 0);

                if (i == total - 1) {
                    safeThis->barraProgresso_->setVisible(false);
                    safeThis->lblStatusProgresso_->setVisible(false);
                }
            });
        }
    });
}

void SendToPrintDialog::selecionarFoto(int indice) {
    indiceSelecionado_ = indice;
    if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
        const auto& item = fila_[indiceSelecionado_];

        // Restaura configurações individuais da foto ativa
        int pId = std::clamp(item.papelIndex + 1, 1, static_cast<int>(papeisPadrao().size()));
        cboTamanhoPapel_->setSelectedId(pId, juce::dontSendNotification);
        cboOrientacao_->setSelectedId(static_cast<int>(item.orientacao), juce::dontSendNotification);
        radPreencher_->setToggleState(item.modoEnquadramento == matriz::imagem::ModoEnquadramento::Preencher, juce::dontSendNotification);
        radEncaixar_->setToggleState(item.modoEnquadramento == matriz::imagem::ModoEnquadramento::Encaixar, juce::dontSendNotification);

        sldBrilho_->setValue(item.brilho * 100.0, juce::dontSendNotification);
        sldContraste_->setValue(item.contraste * 100.0, juce::dontSendNotification);
        sldSaturacao_->setValue(item.saturacao * 100.0, juce::dontSendNotification);
        sldNitidez_->setValue(item.nitidez * 100.0, juce::dontSendNotification);
        sldTemperatura_->setValue(item.temperaturaCor * 100.0, juce::dontSendNotification);
    }
    atualizarDetalhesFotoAtiva();
}

void SendToPrintDialog::atualizarDetalhesFotoAtiva() {
    ItemFilaPrint* item = (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size()))
        ? &fila_[indiceSelecionado_]
        : nullptr;

    int selPapel = item ? std::clamp(item->papelIndex, 0, static_cast<int>(papeisPadrao().size()) - 1)
                        : std::clamp(cboTamanhoPapel_->getSelectedId() - 1, 0, static_cast<int>(papeisPadrao().size()) - 1);
    const auto& p = papeisPadrao()[selPapel];

    OrientacaoPapel ori = item ? item->orientacao : static_cast<OrientacaoPapel>(cboOrientacao_->getSelectedId());
    matriz::imagem::ModoEnquadramento modo = item ? item->modoEnquadramento
        : (radPreencher_->getToggleState() ? matriz::imagem::ModoEnquadramento::Preencher : matriz::imagem::ModoEnquadramento::Encaixar);

    previaPapel_->configurarItem(item, p, ori, modo, zoomAtual_, bypassAtivo_);

    if (item && item->valido) {
        bool paisagem = (ori == OrientacaoPapel::Paisagem) ||
                        (ori == OrientacaoPapel::Auto && item->larguraOriginal >= item->alturaOriginal);
        int pxW = p.larguraPixels(300.0, paisagem);
        int pxH = p.alturaPixels(300.0, paisagem);

        lblInfoPapel_->setText(p.nome + "  |  " + juce::String(pxW) + " x " + juce::String(pxH) + " px @ 300 DPI  |  Foto: " +
                              juce::String(item->larguraOriginal) + " x " + juce::String(item->alturaOriginal) + " px",
                              juce::dontSendNotification);
    } else {
        lblInfoPapel_->setText(p.nome + " @ 300 DPI", juce::dontSendNotification);
    }

    listaFila_->repaint();
}

void SendToPrintDialog::atualizarPreviewAjustada() {
    if (indiceSelecionado_ < 0 || indiceSelecionado_ >= static_cast<int>(fila_.size())) return;
    auto& item = fila_[indiceSelecionado_];
    if (!item.bufferPreviewOriginal.valido()) return;

    // Se todos os ajustes estão no padrão (0, 0, 1, 0, 0), limpa cache para usar o original
    if (std::abs(item.brilho) < 0.001f && std::abs(item.contraste) < 0.001f &&
        std::abs(item.saturacao - 1.0f) < 0.001f && std::abs(item.nitidez) < 0.001f &&
        std::abs(item.temperaturaCor) < 0.001f) {
        item.imagemPreviewAjustada = juce::Image();
        previaPapel_->repaint();
        return;
    }

    matriz::imagem::ImagemBuffer buf(item.bufferPreviewOriginal);
    matriz::imagem::aplicarAjustes(buf, item.brilho, item.contraste, item.saturacao, 0, 255, 1.0f, item.temperaturaCor);
    if (item.nitidez > 0.0f) {
        matriz::imagem::nitidez(buf, item.nitidez);
    }

    juce::Image pImg(juce::Image::RGB, buf.largura, buf.altura, false);
    for (int y = 0; y < buf.altura; ++y) {
        for (int x = 0; x < buf.largura; ++x) {
            const uint8_t* p = buf.pixel(x, y);
            pImg.setPixelAt(x, y, juce::Colour(p[0], p[1], p[2]));
        }
    }
    item.imagemPreviewAjustada = pImg;
    previaPapel_->repaint();
}

void SendToPrintDialog::escolherPastaDestino() {
    auto chooser = std::make_shared<juce::FileChooser>(
        matriz::i18n::t("print.escolher_pasta"),
        pastaDestinoSelecionada_.isDirectory() ? pastaDestinoSelecionada_ : juce::File::getSpecialLocation(juce::File::userHomeDirectory));

    juce::Component::SafePointer<SendToPrintDialog> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis, chooser](const juce::FileChooser& fc) {
                              if (!safeThis) return;
                              juce::File d = fc.getResult();
                              if (d.isDirectory()) {
                                  safeThis->pastaDestinoSelecionada_ = d;
                                  safeThis->lblCaminhoDestino_->setText(d.getFullPathName(), juce::dontSendNotification);
                              }
                          });
}

void SendToPrintDialog::limparListaPrint() {
    projeto_.limparMarcacoes(ProjetoAberto::TipoMarcacao::Print);
    carregarFila();
}

void SendToPrintDialog::resetarAjustes() {
    if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
        auto& item = fila_[indiceSelecionado_];
        item.offsetX = 0.0f;
        item.offsetY = 0.0f;
        item.brilho = 0.0f;
        item.contraste = 0.0f;
        item.saturacao = 1.0f;
        item.nitidez = 0.0f;
        item.temperaturaCor = 0.0f;
        item.imagemPreviewAjustada = juce::Image();

        sldBrilho_->setValue(0.0, juce::dontSendNotification);
        sldContraste_->setValue(0.0, juce::dontSendNotification);
        sldSaturacao_->setValue(100.0, juce::dontSendNotification);
        sldNitidez_->setValue(0.0, juce::dontSendNotification);
        sldTemperatura_->setValue(0.0, juce::dontSendNotification);

        previaPapel_->repaint();
    }
}

void SendToPrintDialog::iniciarExportacao() {
    if (exportando_) return;

    if (!pastaDestinoSelecionada_.isDirectory()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            matriz::i18n::t("print.titulo"),
            matriz::i18n::t("zip.erro_sem_destino"));
        return;
    }

    ExportPrintThread::Params params;
    params.pastaDestino = pastaDestinoSelecionada_;
    params.exportarZip = chkExportarZip_->getToggleState();
    params.nomeProjeto = projeto_.projeto().nome();
    params.formato = matriz::imagem::FormatoSaida::Jpeg;
    params.qualidade = 95;
    params.dpi = 300.0;
    params.configWatermark = projeto_.obterConfiguracaoWatermark();

    for (const auto& item : fila_) {
        if (item.valido && item.arquivo.existsAsFile()) {
            ExportPrintThread::ItemExport ie;
            ie.arquivoOrigem = item.arquivo;
            int pIdx = std::clamp(item.papelIndex, 0, static_cast<int>(papeisPadrao().size()) - 1);
            ie.papel = papeisPadrao()[pIdx];
            ie.orientacao = item.orientacao;
            ie.modo = item.modoEnquadramento;
            ie.offsetX = item.offsetX;
            ie.offsetY = item.offsetY;
            ie.brilho = item.brilho;
            ie.contraste = item.contraste;
            ie.saturacao = item.saturacao;
            ie.nitidez = item.nitidez;
            ie.temperatura = item.temperaturaCor;
            ie.aplicarWatermark = projeto_.contemMarcacao(ProjetoAberto::TipoMarcacao::Watermark, item.id);
            params.itens.push_back(std::move(ie));
        }
    }

    if (params.itens.empty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            matriz::i18n::t("print.titulo"),
            matriz::i18n::t("print.nenhuma_foto_valida"));
        return;
    }

    exportando_ = true;
    btnExportar_->setEnabled(false);
    btnLimparLista_->setEnabled(false);
    cboTamanhoPapel_->setEnabled(false);
    cboOrientacao_->setEnabled(false);
    radPreencher_->setEnabled(false);
    radEncaixar_->setEnabled(false);
    btnEscolherPasta_->setEnabled(false);
    chkExportarZip_->setEnabled(false);
    btnResetarAjustes_->setEnabled(false);
    btnBypass_->setEnabled(false);
    btnCancelar_->setButtonText(matriz::i18n::t("zip.btn_cancelar"));

    barraProgresso_->setVisible(true);
    lblStatusProgresso_->setVisible(true);
    progressoValor_ = 0.0;
    lblStatusProgresso_->setText(matriz::i18n::t("print.progresso_iniciando"), juce::dontSendNotification);

    juce::Component::SafePointer<SendToPrintDialog> safeThis(this);

    threadExportacao_ = std::make_unique<ExportPrintThread>(
        std::move(params),
        [safeThis](double progresso, const juce::String& msg) {
            if (!safeThis) return;
            safeThis->progressoValor_ = progresso;
            safeThis->lblStatusProgresso_->setText(msg, juce::dontSendNotification);
        },
        [safeThis](bool sucesso, int totalExportados, const juce::File& resultado, const juce::StringArray& erros) {
            if (!safeThis) return;
            safeThis->exportando_ = false;
            safeThis->btnExportar_->setEnabled(true);
            safeThis->btnLimparLista_->setEnabled(true);
            safeThis->cboTamanhoPapel_->setEnabled(true);
            safeThis->cboOrientacao_->setEnabled(true);
            safeThis->radPreencher_->setEnabled(true);
            safeThis->radEncaixar_->setEnabled(true);
            safeThis->btnEscolherPasta_->setEnabled(true);
            safeThis->chkExportarZip_->setEnabled(true);
            safeThis->btnResetarAjustes_->setEnabled(true);
            safeThis->btnBypass_->setEnabled(true);
            safeThis->btnCancelar_->setButtonText(matriz::i18n::t("print.btn_cancelar"));
            safeThis->barraProgresso_->setVisible(false);
            safeThis->lblStatusProgresso_->setVisible(false);

            if (sucesso) {
                juce::String msg;
                if (resultado.existsAsFile()) {
                    msg = matriz::i18n::t("print.sucesso_zip_msg")
                        .replace("{n}", juce::String(totalExportados))
                        .replace("{caminho}", resultado.getFullPathName());
                } else {
                    msg = matriz::i18n::t("print.sucesso_msg")
                        .replace("{n}", juce::String(totalExportados))
                        .replace("{pasta}", resultado.getFullPathName());
                }

                if (!erros.isEmpty()) {
                    msg += "\n\nErros:\n" + erros.joinIntoString("\n");
                }

                auto* alert = new juce::AlertWindow(
                    matriz::i18n::t("print.sucesso_titulo"),
                    msg,
                    juce::AlertWindow::InfoIcon);

                alert->addButton(matriz::i18n::t("print.btn_revelar"), 1);
                alert->addButton(matriz::i18n::t("print.btn_fechar"), 0);

                alert->enterModalState(true, juce::ModalCallbackFunction::create([resultado, safeThis](int result) {
                    if (result == 1) {
                        resultado.revealToUser();
                    }
                    if (safeThis) {
                        safeThis->fecharDialogo();
                    }
                }), true);
            } else if (!erros.isEmpty()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    matriz::i18n::t("print.titulo"),
                    erros.joinIntoString("\n"));
            }
        });

    threadExportacao_->startThread();
}

void SendToPrintDialog::cancelarExportacao() {
    if (!exportando_ || !threadExportacao_) return;
    lblStatusProgresso_->setText(matriz::i18n::t("print.progresso_cancelando"), juce::dontSendNotification);
    threadExportacao_->signalThreadShouldExit();
}

void SendToPrintDialog::fecharDialogo() {
    if (aoFechar) aoFechar();
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
    }
}

void SendToPrintDialog::exibirModal(ProjetoAberto& projeto) {
    auto dlg = std::make_unique<SendToPrintDialog>(projeto);

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = matriz::i18n::t("print.titulo");
    opt.content.set(dlg.release(), true);
    opt.dialogBackgroundColour = tema().painel;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;
    opt.launchAsync();
}

} // namespace matriz::ui
