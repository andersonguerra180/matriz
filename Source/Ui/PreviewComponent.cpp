#include "PreviewComponent.h"
#include "../Ingest/LeituraTecnica.h"

#include "../Ficha/FichaI18n.h"
#include "../I18n/Strings.h"
#include "Tokens.h"

#include <exiv2/exiv2.hpp>
#include <algorithm>
#include <cmath>

namespace matriz::ui {

namespace {

static int lerOrientacaoExif(const juce::File& arquivo) {
    try {
        auto imgFactory = Exiv2::ImageFactory::open(arquivo.getFullPathName().toStdString());
        imgFactory->readMetadata();
        const auto& ed = imgFactory->exifData();
        auto pos = ed.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
        if (pos != ed.end()) return static_cast<int>(pos->toInt64());
    } catch (...) {}
    return 1;
}

inline juce::File obterPastaAssets() {
    return juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("Assets");
}

static juce::Image rotacionarConformeExif(const juce::Image& src, int orient) {
    if (!src.isValid() || orient <= 1) return src;
    if (orient == 6) { // 90 deg CW (vertical)
        juce::Image dst(src.getFormat(), src.getHeight(), src.getWidth(), true);
        juce::Graphics g(dst);
        g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi, 0, 0)
                                              .translated(static_cast<float>(src.getHeight()), 0));
        g.drawImageAt(src, 0, 0);
        return dst;
    } else if (orient == 8) { // 270 deg CW / 90 CCW (vertical)
        juce::Image dst(src.getFormat(), src.getHeight(), src.getWidth(), true);
        juce::Graphics g(dst);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, 0, 0)
                                              .translated(0, static_cast<float>(src.getWidth())));
        g.drawImageAt(src, 0, 0);
        return dst;
    } else if (orient == 3) { // 180 deg
        juce::Image dst(src.getFormat(), src.getWidth(), src.getHeight(), true);
        juce::Graphics g(dst);
        g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi,
                                                       src.getWidth() * 0.5f, src.getHeight() * 0.5f));
        g.drawImageAt(src, 0, 0);
        return dst;
    }
    return src;
}

// Dispositivo de áudio compartilhado entre todos os previews - só é
// realmente aberto (toca hardware) na primeira vez que o operador aperta
// "play", nunca só por um PreviewComponent existir ou ser renderizado
// (importante pro harness headless, que nunca deve precisar de um
// dispositivo de áudio real só pra tirar um snapshot).
juce::AudioDeviceManager& dispositivoDeAudioCompartilhado() {
    static juce::AudioDeviceManager dm;
    return dm;
}

juce::AudioSourcePlayer& playerDeAudioCompartilhado() {
    static juce::AudioSourcePlayer player;
    return player;
}

void garantirDispositivoAberto() {
    static bool aberto = false;
    if (aberto) return;
    dispositivoDeAudioCompartilhado().initialiseWithDefaultDevices(0, 2); // sem entrada - só reprodução
    dispositivoDeAudioCompartilhado().addAudioCallback(&playerDeAudioCompartilhado());
    aberto = true;
}

juce::String formatarTempo(double segundos) {
    if (segundos < 0 || std::isnan(segundos)) segundos = 0;
    int total = static_cast<int>(segundos);
    return juce::String::formatted("%d:%02d", total / 60, total % 60);
}

// `caracteristicas_tecnicas_json` tem campos soltos no nível raiz
// (duracaoSegundos, sampleRate, larguraPx, alturaPx, ...) escritos por
// construirCaracteristicasJson (Source/Ingest/IngestArquivo.cpp) - o JSON
// bruto do ffprobe fica À PARTE, aninhado embaixo de "bruto", só pra
// auditoria; não é o que se lê aqui. Nunca lança, ausência de um campo só
// significa não mostrá-lo.
struct MetadadosPreview {
    std::optional<double> duracaoSegundos;
    std::optional<int> sampleRate;
    std::optional<int> larguraPx;
    std::optional<int> alturaPx;
};

MetadadosPreview extrairMetadadosPreview(const juce::String& json) {
    MetadadosPreview m;
    juce::var raiz = juce::JSON::parse(json);
    if (!raiz.isObject()) return m;

    if (raiz.hasProperty("duracaoSegundos")) m.duracaoSegundos = static_cast<double>(raiz["duracaoSegundos"]);
    if (raiz.hasProperty("sampleRate")) m.sampleRate = static_cast<int>(raiz["sampleRate"]);
    if (raiz.hasProperty("larguraPx")) m.larguraPx = static_cast<int>(raiz["larguraPx"]);
    if (raiz.hasProperty("alturaPx")) m.alturaPx = static_cast<int>(raiz["alturaPx"]);
    return m;
}

} // namespace

PreviewComponent::PreviewComponent(ProjetoAberto& projeto) : projeto_(projeto) {
    // Leitores de áudio agora vivem dentro da TimelineComponent, junto com o
    // transporte - este Component não decodifica mais nada por conta própria.

    cabecalhoTitulo_ = std::make_unique<juce::Label>();
    cabecalhoTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
    cabecalhoTitulo_->setColour(juce::Label::textColourId, tema().textoPrimario);
    addAndMakeVisible(*cabecalhoTitulo_);

    botaoAnterior_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x97\x80")); // ◀
    botaoAnterior_->onClick = [this] { if (aoNavegar) aoNavegar(-1); };
    addAndMakeVisible(*botaoAnterior_);

    botaoProximo_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x96\xb6")); // ▶
    botaoProximo_->onClick = [this] { if (aoNavegar) aoNavegar(1); };
    addAndMakeVisible(*botaoProximo_);

    botaoFechar_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xc3\x97")); // ×
    botaoFechar_->onClick = [this] { if (aoFechar) aoFechar(); };
    addAndMakeVisible(*botaoFechar_);

    labelMetadados_ = std::make_unique<juce::Label>();
    labelMetadados_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    labelMetadados_->setColour(juce::Label::textColourId, tema().textoSecundario);
    labelMetadados_->setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(*labelMetadados_);

    labelSemPreview_ = std::make_unique<juce::Label>();
    labelSemPreview_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    labelSemPreview_->setColour(juce::Label::textColourId, tema().textoTerciario);
    labelSemPreview_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*labelSemPreview_);

    setWantsKeyboardFocus(true);
    startTimerHz(15);
}

PreviewComponent::~PreviewComponent() { stopTimer(); }

void PreviewComponent::mostrarItem(const std::string& itemId) {
    timeline_.reset();
    videoPlayer_.reset();

    itemId_ = itemId;
    construirParaItem();
    resized();
    repaint();
}

void PreviewComponent::construirParaItem() {
    timeline_.reset();
    videoPlayer_.reset();
    textViewer_.reset();
    imagemPrincipal_ = {};
    labelSemPreview_->setText("", juce::dontSendNotification);
    categoriaAtual_ = matriz::ingest::CategoriaMidia::Desconhecida;

    auto itens = projeto_.listarItens();
    auto it = std::find_if(itens.begin(), itens.end(), [this](const ItemResumo& r) { return r.id == itemId_; });
    juce::String titulo = it != itens.end() ? juce::String(it->titulo) : juce::String();
    juce::String codigo = it != itens.end() ? juce::String(it->codigoAcervo) : juce::String();
    cabecalhoTitulo_->setText(codigo + " - " + (titulo.isNotEmpty() ? titulo : matriz::i18n::t("ficha.cabecalho_sem_titulo")),
                               juce::dontSendNotification);

    auto arquivo = projeto_.arquivoPrincipal(itemId_);
    if (!arquivo) {
        labelSemPreview_->setText(matriz::i18n::t("preview.sem_arquivo"), juce::dontSendNotification);
        labelMetadados_->setText("", juce::dontSendNotification);
        return;
    }

    juce::File arquivoFile(arquivo->caminhoAbsoluto);
    categoriaAtual_ = matriz::ingest::categoriaPorExtensao(arquivoFile);

    juce::String metadados = arquivoFile.getFileName() + "\n" +
                              juce::File::descriptionOfSizeInBytes(arquivo->caminhoAbsoluto.isEmpty()
                                                                        ? 0
                                                                        : static_cast<juce::int64>(arquivoFile.getSize()));
    MetadadosPreview tecnicas = extrairMetadadosPreview(arquivo->caracteristicasTecnicasJson);
    if (tecnicas.duracaoSegundos)
        metadados += "\n" + matriz::i18n::t("preview.duracao") + ": " + formatarTempo(*tecnicas.duracaoSegundos);
    if (tecnicas.sampleRate)
        metadados += "\n" + matriz::i18n::t("preview.sample_rate") + ": " + juce::String(*tecnicas.sampleRate) + " Hz";
    if (tecnicas.larguraPx && tecnicas.alturaPx)
        metadados += "\n" + matriz::i18n::t("preview.dimensoes") + ": " + juce::String(*tecnicas.larguraPx) + " x " +
                     juce::String(*tecnicas.alturaPx);
    labelMetadados_->setText(metadados, juce::dontSendNotification);

    auto caminhoMiniatura = projeto_.caminhoMiniaturaPrincipal(itemId_);

    switch (categoriaAtual_) {
        case matriz::ingest::CategoriaMidia::Imagem: {
            imagemPrincipal_ = juce::ImageFileFormat::loadFrom(arquivoFile);
            if (!imagemPrincipal_.isValid() && caminhoMiniatura)
                imagemPrincipal_ = juce::ImageFileFormat::loadFrom(juce::File(*caminhoMiniatura));
            int orient = lerOrientacaoExif(arquivoFile);
            if (orient > 1 && imagemPrincipal_.isValid())
                imagemPrincipal_ = rotacionarConformeExif(imagemPrincipal_, orient);
            break;
        }
        case matriz::ingest::CategoriaMidia::Video: {
            videoPlayer_ = std::make_unique<VideoPlayerComponent>();
            addAndMakeVisible(*videoPlayer_);
            if (!videoPlayer_->carregar(arquivoFile)) {
                videoPlayer_.reset();
                if (caminhoMiniatura) imagemPrincipal_ = juce::ImageFileFormat::loadFrom(juce::File(*caminhoMiniatura));
                labelSemPreview_->setText(matriz::i18n::t("preview.video_sem_player"), juce::dontSendNotification);
            } else {
                timeline_ = std::make_unique<TimelineComponent>(projeto_);
                timeline_->aoMudarMarcadores = [this] { if (aoMudarMarcadores) aoMudarMarcadores(); };
                timeline_->aoMedirNivel = [this](float e, float d) { if (aoMedirNivel) aoMedirNivel(e, d); };
                addAndMakeVisible(*timeline_);
                timeline_->carregarItem(itemId_, arquivoFile);
            }
            break;
        }
        case matriz::ingest::CategoriaMidia::Audio: {
            // Timeline de editor em tempo real (item 8) no lugar do player
            // simples que havia aqui: forma de onda, cursor, zoom, régua,
            // transporte, jog/shuttle e marcadores.
            timeline_ = std::make_unique<TimelineComponent>(projeto_);
            timeline_->aoMudarMarcadores = [this] { if (aoMudarMarcadores) aoMudarMarcadores(); };
            timeline_->aoMedirNivel = [this](float e, float d) { if (aoMedirNivel) aoMedirNivel(e, d); };
            addAndMakeVisible(*timeline_);
            if (!timeline_->carregarItem(itemId_, arquivoFile)) {
                // Formato não decodificável - não deixa uma timeline vazia
                // fingindo que carregou.
                timeline_.reset();
                if (caminhoMiniatura) imagemPrincipal_ = juce::ImageFileFormat::loadFrom(juce::File(*caminhoMiniatura));
                labelSemPreview_->setText(matriz::i18n::t("timeline.sem_onda"), juce::dontSendNotification);
            }
            break;
        }
        case matriz::ingest::CategoriaMidia::Sessao: {
            labelSemPreview_->setText("", juce::dontSendNotification);
            juce::String ext = arquivoFile.getFileExtension().toLowerCase().replace(".", "");
            juce::String logoFile = matriz::ingest::obterLogoParaExtensao(ext);
            if (logoFile.isEmpty() && ext == "pd") logoFile = "puredata.png";

            if (logoFile.isNotEmpty()) {
                juce::File logoImgFile = obterPastaAssets().getChildFile(logoFile);
                if (logoImgFile.existsAsFile()) {
                    imagemPrincipal_ = juce::ImageFileFormat::loadFrom(logoImgFile);
                }
            }
            break;
        }
        case matriz::ingest::CategoriaMidia::Texto: {
            juce::String conteudo = arquivoFile.loadFileAsString();
            if (conteudo.length() > 50000) conteudo = conteudo.substring(0, 50000) + "\n\n[truncated]";
            textViewer_ = std::make_unique<juce::TextEditor>();
            textViewer_->setMultiLine(true, true);
            textViewer_->setReadOnly(true);
            textViewer_->setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), tema().tamanhoFonteCorpo, juce::Font::plain)));
            textViewer_->setColour(juce::TextEditor::backgroundColourId, tema().painel);
            textViewer_->setColour(juce::TextEditor::textColourId, tema().textoPrimario);
            textViewer_->setColour(juce::TextEditor::outlineColourId, tema().borda);
            textViewer_->setText(conteudo, false);
            addAndMakeVisible(*textViewer_);
            break;
        }
        case matriz::ingest::CategoriaMidia::Documento: {
            auto ext = arquivoFile.getFileExtension().toLowerCase();
            if (ext == ".pdf") {
                labelSemPreview_->setText("", juce::dontSendNotification);
                juce::File logoImgFile = obterPastaAssets().getChildFile("pdf.jpeg");
                if (logoImgFile.existsAsFile()) {
                    imagemPrincipal_ = juce::ImageFileFormat::loadFrom(logoImgFile);
                }
            } else {
                juce::String conteudo = arquivoFile.loadFileAsString();
                if (conteudo.containsNonWhitespaceChars() && conteudo.length() < 200000) {
                    if (conteudo.length() > 50000) conteudo = conteudo.substring(0, 50000) + "\n\n[truncated]";
                    textViewer_ = std::make_unique<juce::TextEditor>();
                    textViewer_->setMultiLine(true, true);
                    textViewer_->setReadOnly(true);
                    textViewer_->setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), tema().tamanhoFonteCorpo, juce::Font::plain)));
                    textViewer_->setColour(juce::TextEditor::backgroundColourId, tema().painel);
                    textViewer_->setColour(juce::TextEditor::textColourId, tema().textoPrimario);
                    textViewer_->setColour(juce::TextEditor::outlineColourId, tema().borda);
                    textViewer_->setText(conteudo, false);
                    addAndMakeVisible(*textViewer_);
                } else {
                    labelSemPreview_->setText("Binary document - use an external viewer.", juce::dontSendNotification);
                }
            }
            break;
        }
        default:
            labelSemPreview_->setText(matriz::i18n::t("preview.sem_visualizacao"), juce::dontSendNotification);
            break;
    }
}

void PreviewComponent::timerCallback() {
    if (videoPlayer_ && timeline_) {
        bool videoTocando = videoPlayer_->estaTocando();
        bool timelineTocando = timeline_->estaTocando();
        if (timelineTocando != videoTocando) {
            if (timelineTocando) videoPlayer_->tocar();
            else videoPlayer_->pausar();
        }

        double posTimeline = timeline_->posicaoSegundos();
        double posVideo = videoPlayer_->posicaoAtual();
        if (std::abs(posTimeline - posVideo) > 0.08) {
            videoPlayer_->irPara(posTimeline);
        }
    }
}

bool PreviewComponent::keyPressed(const juce::KeyPress& tecla) {
    if (tecla == juce::KeyPress::escapeKey) {
        if (aoFechar) aoFechar();
        return true;
    }
    if (tecla == juce::KeyPress::leftKey) {
        if (aoNavegar) aoNavegar(-1);
        return true;
    }
    if (tecla == juce::KeyPress::rightKey) {
        if (aoNavegar) aoNavegar(1);
        return true;
    }
    if (tecla.getKeyCode() == 'M' || tecla.getKeyCode() == 'm') {
        if (timeline_ != nullptr) {
            return timeline_->keyPressed(tecla);
        }
    }
    return false;
}

void PreviewComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);
    if (imagemPrincipal_.isValid()) {
        auto area = getLocalBounds();
        area.removeFromTop(48);
        area.removeFromBottom(80);
        
        if (timeline_ != nullptr) {
            area.removeFromBottom(145);
        }
        
        area = area.reduced(tema().espacoGrande);
        g.drawImage(imagemPrincipal_, area.toFloat(), juce::RectanglePlacement::centred);
    }

    g.setColour(tema().textoTerciario);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    auto hint = getLocalBounds().removeFromBottom(18).reduced(tema().espacoGrande, 0);
    g.drawText("Press M to add marker", hint, juce::Justification::centredRight);
}

void PreviewComponent::resized() {
    auto area = getLocalBounds();
    auto topo = area.removeFromTop(48).reduced(tema().espacoMedio, 8);
    botaoFechar_->setBounds(topo.removeFromRight(32));
    botaoProximo_->setBounds(topo.removeFromRight(32));
    botaoAnterior_->setBounds(topo.removeFromRight(32));
    topo.removeFromRight(tema().espacoMedio);
    cabecalhoTitulo_->setBounds(topo);

    if (labelSemPreview_->getText().isNotEmpty() && !imagemPrincipal_.isValid()) {
        labelSemPreview_->setBounds(area.withSizeKeepingCentre(400, 60));
    } else {
        labelSemPreview_->setBounds({});
    }

    auto areaMetadados = area.removeFromBottom(80).reduced(tema().espacoGrande, tema().espacoMedio);
    labelMetadados_->setBounds(areaMetadados);

    if (timeline_) {
        auto areaTimeline = area.removeFromBottom(145).reduced(tema().espacoMedio, tema().espacoPequeno);
        timeline_->setBounds(areaTimeline);
    }
    if (videoPlayer_) {
        videoPlayer_->setBounds(area.reduced(tema().espacoMedio, tema().espacoPequeno));
    }
    if (textViewer_) {
        textViewer_->setBounds(area.reduced(tema().espacoMedio, tema().espacoPequeno));
    }
}

} // namespace matriz::ui
