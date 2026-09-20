#include "SendToPrintDialog.h"
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
        { "10x15", "10 × 15 cm  (4 × 6\")", 10.0, 15.0, false },
        { "13x18", "13 × 18 cm  (5 × 7\")", 13.0, 18.0, false },
        { "15x21", "15 × 21 cm  (6 × 8\")", 15.0, 21.0, false },
        { "20x25", "20 × 25 cm  (8 × 10\")", 20.3, 25.4, false },
        { "20x30", "20 × 30 cm  (8 × 12\")", 20.0, 30.0, false },
        { "30x40", "30 × 40 cm  (12 × 16\")", 30.0, 40.0, false },
        { "a4",    "A4  (21.0 × 29.7 cm)", 21.0, 29.7, false },
        { "polaroid", "Polaroid  (8.8 × 10.7 cm)", 8.8, 10.7, true }
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
    double dpi) {
    if (!src.valido()) return {};

    if (papel.polaroid) {
        int papelW = papel.larguraPixels(dpi, false);
        int papelH = papel.alturaPixels(dpi, false);
        int fotoDim = static_cast<int>(std::round((7.9 / 2.54) * dpi));

        auto fotoArea = matriz::imagem::enquadrar(src, fotoDim, fotoDim, modo, offsetX, offsetY);
        matriz::imagem::aplicarAjustes(fotoArea, brilho, contraste, saturacao, 0, 255, 1.0f);
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
    matriz::imagem::aplicarAjustes(processada, brilho, contraste, saturacao, 0, 255, 1.0f);
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
                                         ZoomPrevia zoom) {
    itemAtivo_ = item;
    papelAtual_ = papel;
    orientacaoConfig_ = orientacao;
    modoEnquadramento_ = modo;
    zoom_ = zoom;
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

    const juce::Image& img = itemAtivo_->imagemPreview.isValid() ? itemAtivo_->imagemPreview : itemAtivo_->miniatura;
    if (!img.isValid()) {
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText("Carregando prévia...", papelRect, juce::Justification::centred);
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
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float brilho = 0.0f;
        float contraste = 0.0f;
        float saturacao = 1.0f;
        float nitidez = 0.0f;
    };

    struct Params {
        juce::File pastaDestino;
        DefinicaoPapel papel;
        OrientacaoPapel orientacao = OrientacaoPapel::Auto;
        matriz::imagem::ModoEnquadramento modo = matriz::imagem::ModoEnquadramento::Preencher;
        matriz::imagem::FormatoSaida formato = matriz::imagem::FormatoSaida::Jpeg;
        int qualidade = 95;
        double dpi = 300.0;
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

        for (int i = 0; i < total; ++i) {
            if (threadShouldExit()) break;

            const auto& item = params_.itens[i];
            juce::String nomeArq = item.arquivoOrigem.getFileName();
            double p = static_cast<double>(i) / std::max(1, total);

            juce::String msg = juce::String::formatted(
                matriz::i18n::t("print.progresso_foto").toRawUTF8(),
                i + 1, total, nomeArq.toRawUTF8());
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
            if (params_.orientacao == OrientacaoPapel::Paisagem) paisagem = true;
            else if (params_.orientacao == OrientacaoPapel::Retrato) paisagem = false;
            else paisagem = (bufOrientado.largura >= bufOrientado.altura);

            // 4. Processa enquadramento, redimensionamento 300 DPI e ajustes
            auto bufFinal = processarFotoParaPapel(
                bufOrientado,
                params_.papel,
                paisagem,
                params_.modo,
                item.offsetX,
                item.offsetY,
                item.brilho,
                item.contraste,
                item.saturacao,
                item.nitidez,
                params_.dpi);

            if (!bufFinal.valido()) {
                erros.add(nomeArq + ": falha no processamento de imagem");
                continue;
            }

            // 5. Determina nome e caminho do arquivo de saída
            juce::String nomeBase = item.arquivoOrigem.getFileNameWithoutExtension();
            juce::String sufixo = "_print_" + params_.papel.id;
            juce::String ext = (params_.formato == matriz::imagem::FormatoSaida::Jpeg) ? "jpg" : "png";

            juce::File arquivoDestino = resolverColisaoArquivo(params_.pastaDestino, nomeBase, sufixo, ext);

            // 6. Grava com injeção em nível de bytes de 300 DPI
            bool gravou = matriz::imagem::gravar(
                bufFinal,
                arquivoDestino,
                params_.formato,
                params_.qualidade,
                params_.dpi);

            if (gravou) {
                exportados++;
            } else {
                erros.add(nomeArq + ": falha ao gravar arquivo em disco");
            }
        }

        notificarProgresso(1.0, "Concluído");
        notificarFim(!threadShouldExit() && exportados > 0, exportados, params_.pastaDestino, erros);
    }

private:
    void notificarProgresso(double p, const juce::String& msg) {
        juce::MessageManager::callAsync([this, p, msg] {
            if (onProgresso_) onProgresso_(p, msg);
        });
    }

    void notificarFim(bool sucesso, int totalExportados, const juce::File& pasta, const juce::StringArray& erros) {
        juce::MessageManager::callAsync([this, sucesso, totalExportados, pasta, erros] {
            if (onConcluido_) onConcluido_(sucesso, totalExportados, pasta, erros);
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
    lblFilaTitulo_ = std::make_unique<juce::Label>("lblFilaTitulo", juce::String::formatted(matriz::i18n::t("print.fila_titulo").toRawUTF8(), 0));
    lblFilaTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblFilaTitulo_->setColour(juce::Label::textColourId, tk.acento);
    addAndMakeVisible(*lblFilaTitulo_);

    listaFila_ = std::make_unique<juce::ListBox>("listaFila", this);
    listaFila_->setRowHeight(68);
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
    cboTamanhoPapel_->onChange = [this] { atualizarDetalhesFotoAtiva(); };
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
    cboOrientacao_->onChange = [this] { atualizarDetalhesFotoAtiva(); };
    addAndMakeVisible(*cboOrientacao_);

    lblModoEnquadramento_ = std::make_unique<juce::Label>("lblModoEnquadramento", matriz::i18n::t("print.modo_enquadramento"));
    lblModoEnquadramento_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    lblModoEnquadramento_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblModoEnquadramento_);

    radPreencher_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("print.modo_preencher"));
    radPreencher_->setRadioGroupId(1001);
    radPreencher_->setToggleState(true, juce::dontSendNotification);
    radPreencher_->onClick = [this] { atualizarDetalhesFotoAtiva(); };
    addAndMakeVisible(*radPreencher_);

    radEncaixar_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("print.modo_encaixar"));
    radEncaixar_->setRadioGroupId(1001);
    radEncaixar_->onClick = [this] { atualizarDetalhesFotoAtiva(); };
    addAndMakeVisible(*radEncaixar_);

    lblResolucaoInfo_ = std::make_unique<juce::Label>("lblResolucaoInfo", "Resolução: 300 DPI (Padrão)");
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
            previaPapel_->repaint();
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
            previaPapel_->repaint();
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
            previaPapel_->repaint();
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
            previaPapel_->repaint();
        }
    };
    addAndMakeVisible(*sldNitidez_);

    btnResetarAjustes_ = std::make_unique<juce::TextButton>(matriz::i18n::t("print.resetar_ajustes"));
    btnResetarAjustes_->onClick = [this] { resetarAjustes(); };
    addAndMakeVisible(*btnResetarAjustes_);

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

    btnExportar_ = std::make_unique<juce::TextButton>(juce::String::formatted(matriz::i18n::t("print.btn_exportar").toRawUTF8(), 0));
    btnExportar_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xffff6b00));
    btnExportar_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnExportar_->onClick = [this] { iniciarExportacao(); };
    addAndMakeVisible(*btnExportar_);

    carregarFila();
    setSize(1120, 720);
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

    // Linhas verticais separadoras sutis entre as 3 colunas
    g.setColour(tema().borda.withAlpha(0.6f));
    auto bounds = getLocalBounds();
    int col1W = 280;
    int col3W = 280;
    g.drawVerticalLine(col1W + 16, 16.0f, static_cast<float>(bounds.getHeight() - 60));
    g.drawVerticalLine(bounds.getWidth() - col3W - 16, 16.0f, static_cast<float>(bounds.getHeight() - 60));

    // Linha horizontal separadora da barra inferior
    g.drawHorizontalLine(bounds.getHeight() - 56, 16.0f, static_cast<float>(bounds.getWidth() - 16));
}

void SendToPrintDialog::resized() {
    auto area = getLocalBounds().reduced(16);

    // Barra Inferior
    auto bottomRow = area.removeFromBottom(42);
    btnLimparLista_->setBounds(bottomRow.removeFromLeft(120).withHeight(32));
    btnExportar_->setBounds(bottomRow.removeFromRight(190).withHeight(32));
    bottomRow.removeFromRight(12);
    btnCancelar_->setBounds(bottomRow.removeFromRight(100).withHeight(32));

    auto statusArea = bottomRow.reduced(16, 4);
    barraProgresso_->setBounds(statusArea.removeFromTop(12));
    lblStatusProgresso_->setBounds(statusArea);

    area.removeFromBottom(12);

    // Coluna 1 (Esquerda) - 280px
    int col1W = 280;
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
    auto grpDestBounds = col3.removeFromTop(90);
    grpDestino_->setBounds(grpDestBounds);
    auto innerDest = grpDestBounds.reduced(12, 10);
    innerDest.removeFromTop(16);
    lblCaminhoDestino_->setBounds(innerDest.removeFromTop(20));
    btnEscolherPasta_->setBounds(innerDest.removeFromTop(26));

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

    innerAj.removeFromTop(8);
    btnResetarAjustes_->setBounds(innerAj.removeFromTop(26));

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

    // Fundo de seleção / hover
    if (rowIsSelected) {
        g.setColour(tk.acento.withAlpha(0.20f));
        g.fillRoundedRectangle(rowBounds.toFloat().reduced(2.0f), 4.0f);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(rowBounds.toFloat().reduced(2.0f), 4.0f, 1.5f);
    } else {
        g.setColour(tk.painelAlt.withAlpha(0.35f));
        g.fillRoundedRectangle(rowBounds.toFloat().reduced(2.0f), 4.0f);
        g.setColour(tk.borda.withAlpha(0.5f));
        g.drawRoundedRectangle(rowBounds.toFloat().reduced(2.0f), 4.0f, 0.8f);
    }

    auto r = rowBounds.reduced(8, 6);

    // Thumbnail quadrado de 52x52
    auto thumbRect = r.removeFromLeft(52);
    if (item.miniatura.isValid()) {
        g.drawImage(item.miniatura, thumbRect.toFloat(), juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    } else {
        g.setColour(tk.fundo);
        g.fillRoundedRectangle(thumbRect.toFloat(), 3.0f);
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("SEM", thumbRect, juce::Justification::centred);
    }
    g.setColour(tk.borda);
    g.drawRoundedRectangle(thumbRect.toFloat(), 3.0f, 1.0f);

    r.removeFromLeft(8);

    // Nome do Arquivo
    auto nameArea = r.removeFromTop(20);
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    g.setColour(item.valido ? tk.textoPrimario : tk.textoTerciario);
    g.drawText(item.nomeArquivo, nameArea, juce::Justification::centredLeft, true);

    // Dimensões originais em pixels
    auto dimArea = r.removeFromTop(16);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(tk.textoSecundario);
    if (item.larguraOriginal > 0 && item.alturaOriginal > 0) {
        g.drawText(juce::String(item.larguraOriginal) + " × " + juce::String(item.alturaOriginal) + " px",
                   dimArea, juce::Justification::centredLeft, true);
    }

    // Badge de Validação / Resolução
    auto badgeArea = r.removeFromTop(18);
    if (item.valido) {
        // Calcula DPI para o papel selecionado atualmente
        int selPapel = std::clamp(cboTamanhoPapel_->getSelectedId() - 1, 0, static_cast<int>(papeisPadrao().size()) - 1);
        const auto& p = papeisPadrao()[selPapel];
        bool paisagem = (cboOrientacao_->getSelectedId() == static_cast<int>(OrientacaoPapel::Paisagem)) ||
                        (cboOrientacao_->getSelectedId() == static_cast<int>(OrientacaoPapel::Auto) && item.larguraOriginal >= item.alturaOriginal);
        int targetW = p.larguraPixels(300.0, paisagem);
        int targetH = p.alturaPixels(300.0, paisagem);

        int dpiEfetivo = item.calcularDpiEfetivo(targetW, targetH);
        if (dpiEfetivo >= 240) {
            // OK (Verde/Acento)
            g.setColour(tk.acento.withAlpha(0.20f));
            auto bRect = badgeArea.removeFromLeft(60);
            g.fillRoundedRectangle(bRect.toFloat(), 8.0f);
            g.setColour(tk.acento);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText("OK  300 DPI", bRect, juce::Justification::centred);
        } else {
            // Aviso de baixa resolução (Amarelo/Laranja)
            g.setColour(juce::Colour(0xffff9900).withAlpha(0.25f));
            auto bRect = badgeArea.removeFromLeft(120);
            g.fillRoundedRectangle(bRect.toFloat(), 8.0f);
            g.setColour(juce::Colour(0xffff9900));
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(juce::String(dpiEfetivo) + " DPI (Baixa)", bRect, juce::Justification::centred);
        }
    } else {
        // Desabilitado / Erro de formato (Vermelho)
        g.setColour(tk.perigo.withAlpha(0.20f));
        auto bRect = badgeArea.removeFromLeft(std::min(r.getWidth(), 140));
        g.fillRoundedRectangle(bRect.toFloat(), 8.0f);
        g.setColour(tk.perigo);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(item.motivoInvalido, bRect, juce::Justification::centred, true);
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
            item.motivoInvalido = matriz::i18n::t("print.status_ausente");
            fila_.push_back(std::move(item));
            continue;
        }

        auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".mp4" || ext == ".mov" || ext == ".mkv" || ext == ".avi" || ext == ".wmv" || ext == ".m4v") {
            item.valido = false;
            item.motivoInvalido = matriz::i18n::t("print.status_video");
            fila_.push_back(std::move(item));
            continue;
        }

        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aif" || ext == ".aiff" || ext == ".m4a") {
            item.valido = false;
            item.motivoInvalido = matriz::i18n::t("print.status_audio");
            fila_.push_back(std::move(item));
            continue;
        }

        if (ext == ".tif" || ext == ".tiff") {
            item.valido = false;
            item.motivoInvalido = matriz::i18n::t("print.status_tiff");
            fila_.push_back(std::move(item));
            continue;
        }

        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") {
            item.valido = false;
            item.motivoInvalido = matriz::i18n::t("print.status_nao_suportado");
            fila_.push_back(std::move(item));
            continue;
        }

        // Validação binária estrita (rejeita CMYK, checa magic bytes)
        auto res = matriz::imagem::lerImagem(f);
        if (!res.sucesso) {
            item.valido = false;
            item.motivoInvalido = res.erro;
            fila_.push_back(std::move(item));
            continue;
        }

        // Imagem RGB válida
        item.valido = true;
        item.larguraOriginal = res.buffer.largura;
        item.alturaOriginal = res.buffer.altura;
        item.orientacaoExif = res.orientacaoExif;

        // Gera thumbnail local para a lista
        int thumbW = 104;
        int thumbH = static_cast<int>(std::round(thumbW * (static_cast<double>(res.buffer.altura) / res.buffer.largura)));
        if (thumbH <= 0) thumbH = 104;
        auto thumbBuf = matriz::imagem::redimensionar(res.buffer, thumbW, thumbH);

        juce::Image tImg(juce::Image::RGB, thumbBuf.largura, thumbBuf.altura, false);
        for (int y = 0; y < thumbBuf.altura; ++y) {
            for (int x = 0; x < thumbBuf.largura; ++x) {
                const uint8_t* p = thumbBuf.pixel(x, y);
                tImg.setPixelAt(x, y, juce::Colour(p[0], p[1], p[2]));
            }
        }
        item.miniatura = tImg;

        // Pré-cache para o preview central
        int prevW = std::min(res.buffer.largura, 1200);
        int prevH = static_cast<int>(std::round(prevW * (static_cast<double>(res.buffer.altura) / res.buffer.largura)));
        if (prevH <= 0) prevH = prevW;
        auto prevBuf = matriz::imagem::redimensionar(res.buffer, prevW, prevH);

        juce::Image pImg(juce::Image::RGB, prevBuf.largura, prevBuf.altura, false);
        for (int y = 0; y < prevBuf.altura; ++y) {
            for (int x = 0; x < prevBuf.largura; ++x) {
                const uint8_t* p = prevBuf.pixel(x, y);
                pImg.setPixelAt(x, y, juce::Colour(p[0], p[1], p[2]));
            }
        }
        item.imagemPreview = pImg;

        fila_.push_back(std::move(item));
    }

    // Contagem de fotos válidas
    int fotosValidas = 0;
    for (const auto& item : fila_) {
        if (item.valido) fotosValidas++;
    }

    lblFilaTitulo_->setText(juce::String::formatted(matriz::i18n::t("print.fila_titulo").toRawUTF8(), static_cast<int>(fila_.size())), juce::dontSendNotification);
    btnExportar_->setButtonText(juce::String::formatted(matriz::i18n::t("print.btn_exportar").toRawUTF8(), fotosValidas));
    btnExportar_->setEnabled(fotosValidas > 0);

    listaFila_->updateContent();

    if (!fila_.empty()) {
        selecionarFoto(0);
        listaFila_->selectRow(0);
    } else {
        selecionarFoto(-1);
    }
}

void SendToPrintDialog::selecionarFoto(int indice) {
    indiceSelecionado_ = indice;
    if (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size())) {
        const auto& item = fila_[indiceSelecionado_];
        sldBrilho_->setValue(item.brilho * 100.0, juce::dontSendNotification);
        sldContraste_->setValue(item.contraste * 100.0, juce::dontSendNotification);
        sldSaturacao_->setValue(item.saturacao * 100.0, juce::dontSendNotification);
        sldNitidez_->setValue(item.nitidez * 100.0, juce::dontSendNotification);
    }
    atualizarDetalhesFotoAtiva();
}

void SendToPrintDialog::atualizarDetalhesFotoAtiva() {
    int selPapel = std::clamp(cboTamanhoPapel_->getSelectedId() - 1, 0, static_cast<int>(papeisPadrao().size()) - 1);
    const auto& p = papeisPadrao()[selPapel];

    OrientacaoPapel ori = static_cast<OrientacaoPapel>(cboOrientacao_->getSelectedId());
    matriz::imagem::ModoEnquadramento modo = radPreencher_->getToggleState()
        ? matriz::imagem::ModoEnquadramento::Preencher
        : matriz::imagem::ModoEnquadramento::Encaixar;

    ItemFilaPrint* item = (indiceSelecionado_ >= 0 && indiceSelecionado_ < static_cast<int>(fila_.size()))
        ? &fila_[indiceSelecionado_]
        : nullptr;

    previaPapel_->configurarItem(item, p, ori, modo, zoomAtual_);

    if (item && item->valido) {
        bool paisagem = (ori == OrientacaoPapel::Paisagem) ||
                        (ori == OrientacaoPapel::Auto && item->larguraOriginal >= item->alturaOriginal);
        int pxW = p.larguraPixels(300.0, paisagem);
        int pxH = p.alturaPixels(300.0, paisagem);

        lblInfoPapel_->setText(p.nome + "  |  " + juce::String(pxW) + " × " + juce::String(pxH) + " px @ 300 DPI  |  Foto: " +
                              juce::String(item->larguraOriginal) + " × " + juce::String(item->alturaOriginal) + " px",
                              juce::dontSendNotification);
    } else {
        lblInfoPapel_->setText(p.nome + " @ 300 DPI", juce::dontSendNotification);
    }

    listaFila_->repaint();
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
        fila_[indiceSelecionado_].offsetX = 0.0f;
        fila_[indiceSelecionado_].offsetY = 0.0f;
        fila_[indiceSelecionado_].brilho = 0.0f;
        fila_[indiceSelecionado_].contraste = 0.0f;
        fila_[indiceSelecionado_].saturacao = 1.0f;
        fila_[indiceSelecionado_].nitidez = 0.0f;

        sldBrilho_->setValue(0.0, juce::dontSendNotification);
        sldContraste_->setValue(0.0, juce::dontSendNotification);
        sldSaturacao_->setValue(100.0, juce::dontSendNotification);
        sldNitidez_->setValue(0.0, juce::dontSendNotification);

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

    int selPapel = std::clamp(cboTamanhoPapel_->getSelectedId() - 1, 0, static_cast<int>(papeisPadrao().size()) - 1);
    params.papel = papeisPadrao()[selPapel];
    params.orientacao = static_cast<OrientacaoPapel>(cboOrientacao_->getSelectedId());
    params.modo = radPreencher_->getToggleState() ? matriz::imagem::ModoEnquadramento::Preencher
                                                  : matriz::imagem::ModoEnquadramento::Encaixar;
    params.formato = matriz::imagem::FormatoSaida::Jpeg;
    params.qualidade = 95;
    params.dpi = 300.0;

    for (const auto& item : fila_) {
        if (item.valido && item.arquivo.existsAsFile()) {
            ExportPrintThread::ItemExport ie;
            ie.arquivoOrigem = item.arquivo;
            ie.offsetX = item.offsetX;
            ie.offsetY = item.offsetY;
            ie.brilho = item.brilho;
            ie.contraste = item.contraste;
            ie.saturacao = item.saturacao;
            ie.nitidez = item.nitidez;
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
    btnResetarAjustes_->setEnabled(false);
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
        [safeThis](bool sucesso, int totalExportados, const juce::File& pasta, const juce::StringArray& erros) {
            if (!safeThis) return;
            safeThis->exportando_ = false;
            safeThis->btnExportar_->setEnabled(true);
            safeThis->btnLimparLista_->setEnabled(true);
            safeThis->cboTamanhoPapel_->setEnabled(true);
            safeThis->cboOrientacao_->setEnabled(true);
            safeThis->radPreencher_->setEnabled(true);
            safeThis->radEncaixar_->setEnabled(true);
            safeThis->btnEscolherPasta_->setEnabled(true);
            safeThis->btnResetarAjustes_->setEnabled(true);
            safeThis->btnCancelar_->setButtonText(matriz::i18n::t("print.btn_cancelar"));
            safeThis->barraProgresso_->setVisible(false);
            safeThis->lblStatusProgresso_->setVisible(false);

            if (sucesso) {
                juce::String msg = juce::String::formatted(
                    matriz::i18n::t("print.sucesso_msg").toRawUTF8(),
                    totalExportados,
                    pasta.getFullPathName().toRawUTF8());

                if (!erros.isEmpty()) {
                    msg += "\n\nErros:\n" + erros.joinIntoString("\n");
                }

                auto* alert = new juce::AlertWindow(
                    matriz::i18n::t("print.sucesso_titulo"),
                    msg,
                    juce::AlertWindow::InfoIcon);

                alert->addButton(matriz::i18n::t("print.btn_revelar"), 1);
                alert->addButton(matriz::i18n::t("print.btn_fechar"), 0);

                alert->enterModalState(true, juce::ModalCallbackFunction::create([pasta, safeThis](int result) {
                    if (result == 1) {
                        pasta.revealToUser();
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
