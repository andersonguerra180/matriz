#include "Miniaturas.h"

#include "../Model/Project.h"
#include "../Model/CaminhosBanco.h"
#include "../Model/NomesSeguros.h"
#include "ProcessoExterno.h"
#include "MiniaturaPsd.h"
#include "MiniaturaDocumento.h"

#include <exiv2/exiv2.hpp>
#include <cstring>
#include <algorithm>

namespace matriz::ingest {

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

static juce::Image rotacionarConformeExif(const juce::Image& src, int orient) {
    if (!src.isValid() || orient <= 1) return src;
    if (orient == 6) { // 90 deg CW
        juce::Image dst(src.getFormat(), src.getHeight(), src.getWidth(), true);
        juce::Graphics g(dst);
        g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi, 0, 0)
                                              .translated(static_cast<float>(src.getHeight()), 0));
        g.drawImageAt(src, 0, 0);
        return dst;
    } else if (orient == 8) { // 270 deg CW
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

DimensaoImagem dimensoesImagem(const juce::File& imagem) {
    juce::Image img = juce::ImageFileFormat::loadFrom(imagem);
    DimensaoImagem d;
    if (!img.isNull()) {
        int orient = lerOrientacaoExif(imagem);
        if (orient == 6 || orient == 8 || orient == 5 || orient == 7) {
            d.largura = img.getHeight();
            d.altura = img.getWidth();
        } else {
            d.largura = img.getWidth();
            d.altura = img.getHeight();
        }
    }
    if (d.largura == 0 || d.altura == 0)
        throw MiniaturaError("could not read image dimensions: " + imagem.getFullPathName().toStdString());
    return d;
}

bool gravarJpeg(const juce::Image& img, const juce::File& destino, float qualidade) {
    if (!img.isValid() || img.getWidth() <= 0 || img.getHeight() <= 0) return false;
    juce::MemoryOutputStream memoria;
    juce::JPEGImageFormat jpeg;
    if (qualidade >= 0.0f) jpeg.setQuality(qualidade);
    if (!jpeg.writeImageToStream(img, memoria) || memoria.getDataSize() == 0) return false;
    destino.getParentDirectory().createDirectory();
    return destino.replaceWithData(memoria.getData(), memoria.getDataSize());
}

DimensaoImagem gerarMiniaturaImagem(const juce::File& origem, const juce::File& destino, int ladoMaximoPx) {
    juce::Image img = juce::ImageFileFormat::loadFrom(origem);
    if (img.isNull()) {
        // JUCE só decodifica PNG/JPEG/GIF — PSD/PSB/TIFF/CR2 caem aqui
        // sempre. Fallback nativo (ImageIO do macOS, que já decodifica
        // esses formatos nativamente) só pra esses; se também falhar,
        // segue o mesmo throw de antes e quem chama isto
        // (gerarEGravarMiniaturaPrincipal) cai pro ícone genérico da
        // categoria — comportamento inalterado pros outros formatos.
        juce::String ext = origem.getFileExtension().toLowerCase();
        if (ext == ".psd" || ext == ".psb" || ext == ".tif" || ext == ".tiff" || ext == ".cr2") {
            destino.getParentDirectory().createDirectory();
            int w = 0, h = 0;
            if (gerarMiniaturaPsdNativa(origem.getFullPathName().toRawUTF8(),
                                        destino.getFullPathName().toRawUTF8(),
                                        ladoMaximoPx, &w, &h)) {
                DimensaoImagem d;
                d.largura = w;
                d.altura = h;
                return d;
            }
            // Windows não decodifica PSD sem codec de terceiros no WIC; o ffmpeg tem decoder próprio. Só reduz (nunca amplia).
            if (ext == ".psd") {
                try {
                    const juce::String lado = juce::String(ladoMaximoPx);
                    rodarEsperandoSucesso("ffmpeg",
                                          {"-y", "-hide_banner", "-loglevel", "error", "-i", origem.getFullPathName(), "-frames:v", "1",
                                           "-vf", "scale='min(" + lado + ",iw)':'min(" + lado + ",ih)':force_original_aspect_ratio=decrease",
                                           "-q:v", "3", destino.getFullPathName()},
                                          60000);
                    juce::Image pronta = juce::ImageFileFormat::loadFrom(destino);
                    if (pronta.isValid()) {
                        DimensaoImagem d;
                        d.largura = pronta.getWidth();
                        d.altura = pronta.getHeight();
                        return d;
                    }
                } catch (const std::exception&) {
                    // cai no throw abaixo: quem chama usa o ícone da categoria
                }
            }
        }
        throw MiniaturaError("Falha ao decodificar imagem para miniatura: " + origem.getFullPathName().toStdString());
    }

    int orient = lerOrientacaoExif(origem);
    if (orient > 1) {
        img = rotacionarConformeExif(img, orient);
    }

    int w = img.getWidth();
    int h = img.getHeight();
    if (w > ladoMaximoPx || h > ladoMaximoPx) {
        float ratio = std::min(static_cast<float>(ladoMaximoPx) / w, static_cast<float>(ladoMaximoPx) / h);
        w = std::max(1, static_cast<int>(w * ratio));  // panorâmica extrema: o lado curto não pode virar 0
        h = std::max(1, static_cast<int>(h * ratio));
    }
    juce::Image scaled = img.rescaled(w, h, juce::Graphics::highResamplingQuality);

    if (!gravarJpeg(scaled, destino))
        throw MiniaturaError("Falha ao gravar miniatura em " + destino.getFullPathName().toStdString());

    DimensaoImagem d;
    d.largura = w;
    d.altura = h;
    return d;
}

std::vector<KeyframeGerado> gerarKeyframesVideo(const juce::File& origem, double duracaoSegundos, int quantidade,
                                                 const juce::File& dirDestino, const juce::String& prefixo,
                                                 int larguraPx, bool primeiroFrame, double janelaInicio, double janelaFim,
                                                 matriz::app::CancelamentoPtr cancelamento) {
    if (quantidade <= 0 || larguraPx <= 0)
        throw MiniaturaError("quantidade e largura de keyframes devem ser positivas");
    // Um frame em t=0 não depende da duração no container (MOV pode não a informar).
    if (duracaoSegundos <= 0.0 && !(quantidade == 1 && primeiroFrame))
        throw MiniaturaError("invalid duration for keyframe extraction: " + origem.getFullPathName().toStdString());

    dirDestino.createDirectory();
    std::vector<KeyframeGerado> resultado;
    resultado.reserve(static_cast<size_t>(quantidade));
    juce::Random rng;

    for (int i = 0; i < quantidade; ++i) {
        if (cancelamento && cancelamento->pedido()) throw MiniaturaError("keyframe extraction cancelled");
        double tempo = quantidade == 1
            ? (primeiroFrame ? 0.0 : duracaoSegundos * (janelaInicio + rng.nextDouble() * (janelaFim - janelaInicio)))
            : duracaoSegundos * (i + 0.5) / quantidade;
        const auto nome = matriz::nomes_seguros::sanitizarComponente(prefixo + "_" + juce::String(i).paddedLeft('0', 4) + ".jpg");
        juce::File destino = dirDestino.getChildFile(nome);
        juce::Image frame;
        // Primeiro seek rápido; depois seek decodificando; por último, primeiro frame sem seek.
        // O wrapper drena stdout/stderr enquanto roda. waitForProcessToFinish sozinho deixava o ffmpeg
        // preso no pipe de logs (comum em MOV/ProRes), além de cortar decodificações após 15 segundos.
        for (int tentativa = 0; tentativa < 3; ++tentativa) {
            if (cancelamento && cancelamento->pedido()) throw MiniaturaError("keyframe extraction cancelled");
            juce::StringArray args{"-progress", "pipe:1"};
            if (tentativa == 0) { args.add("-ss"); args.add(juce::String(tempo, 3)); }
            args.add("-i"); args.add(origem.getFullPathName());
            if (tentativa == 1) { args.add("-ss"); args.add(juce::String(tempo, 3)); }
            args.addArray({"-map", "0:v:0", "-frames:v", "1", "-an", "-sn", "-dn", "-vf",
                           "scale=" + juce::String(larguraPx) + ":-2", "-pix_fmt", "yuvj420p", "-q:v", "5",
                           destino.getFullPathName()});
            destino.deleteFile();
            try {
                rodarEsperandoSucesso("ffmpeg", args, 300000, cancelamento);
                frame = juce::ImageFileFormat::loadFrom(destino);
                if (frame.isValid()) {
                    if (tentativa == 2) tempo = 0.0;
                    break;
                }
            } catch (const ProcessoExternoError&) {
                if (cancelamento && cancelamento->pedido()) { destino.deleteFile(); throw; }
            }
        }
        if (!frame.isValid()) {
            destino.deleteFile();
            // Não indexar um placeholder como sucesso: o próximo carregamento pode tentar de novo.
            throw MiniaturaError("Falha ao extrair keyframe de " + origem.getFullPathName().toStdString());
        }
        KeyframeGerado kf;
        kf.tempoSegundos = tempo;
        kf.arquivo = destino;
        kf.dimensao.largura = frame.getWidth();
        kf.dimensao.altura = frame.getHeight();
        resultado.push_back(std::move(kf));
    }
    return resultado;
}

bool miniaturaVideoPrecisaSerRegenerada(const juce::File& miniatura) {
    if (!miniatura.existsAsFile() || miniatura.getSize() == 0) return true;
    // Comparação dos bytes do JPEG que o código antigo produzia: não confundir uma cena escura real com falha.
    static const juce::MemoryBlock placeholder = [] {
        juce::Image img(juce::Image::RGB, 320, 180, true);
        juce::Graphics g(img);
        g.fillAll(juce::Colour(0xff2d2d32));
        g.setColour(juce::Colour(0xff3c3c44));
        const int lado = 45;
        g.fillRect(img.getBounds().getCentreX() - lado / 2, img.getBounds().getCentreY() - lado / 2, lado, lado);
        juce::MemoryOutputStream out;
        juce::JPEGImageFormat jpeg;
        jpeg.writeImageToStream(img, out);
        return out.getMemoryBlock();
    }();
    if (miniatura.getSize() != static_cast<juce::int64>(placeholder.getSize())) return false;
    juce::MemoryBlock bytes;
    return miniatura.loadFileAsData(bytes) && bytes == placeholder;
}

std::vector<uint8_t> FormaDeOnda::paraBlob() const {
    std::vector<uint8_t> blob(minimos.size() * 2 * sizeof(float));
    for (size_t i = 0; i < minimos.size(); ++i) {
        std::memcpy(blob.data() + i * 2 * sizeof(float), &minimos[i], sizeof(float));
        std::memcpy(blob.data() + i * 2 * sizeof(float) + sizeof(float), &maximos[i], sizeof(float));
    }
    return blob;
}

FormaDeOnda calcularFormaDeOnda(const juce::File& origemAudio, const juce::File& dirTemporario,
                                 double bucketsPorSegundo) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(origemAudio));

    // O JUCE não abre todo formato em toda plataforma (.m4a AAC/ALAC não abre no Windows). Nesse caso o ffmpeg
    // decodifica para um WAV mono de 16 kHz — de sobra para a forma de onda — que é lido e apagado ao sair.
    struct ApagaAoSair {
        juce::File f;
        ~ApagaAoSair() { if (f != juce::File()) f.deleteFile(); }
    } temporario;
    if (reader == nullptr) {
        const juce::File base = dirTemporario.isDirectory() ? dirTemporario : juce::File::getSpecialLocation(juce::File::tempDirectory);
        temporario.f = base.getNonexistentChildFile("matriz_onda_", ".wav", false);
        try {
            rodarEsperandoSucesso("ffmpeg", {"-y", "-hide_banner", "-loglevel", "error", "-i", origemAudio.getFullPathName(), "-vn",
                                             "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le", temporario.f.getFullPathName()});
            reader.reset(formatManager.createReaderFor(temporario.f));
        } catch (const std::exception&) {
        }
    }
    if (reader == nullptr)
        throw MiniaturaError("could not read audio file: " + origemAudio.getFullPathName().toStdString());

    double fileSampleRate = reader->sampleRate;
    juce::int64 numSamples = reader->lengthInSamples;

    FormaDeOnda onda;
    onda.duracaoSegundos = static_cast<double>(numSamples) / fileSampleRate;

    int amostrasPorBucket = juce::jmax(1, static_cast<int>(fileSampleRate / bucketsPorSegundo));
    onda.bucketsPorSegundo = fileSampleRate / amostrasPorBucket;

    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), 8192);
    juce::int64 restantes = numSamples;
    juce::int64 lidos = 0;

    float bucketMin = 0.0f, bucketMax = 0.0f;
    int amostrasNoBucketAtual = 0;
    bool bucketAberto = false;

    int numChannels = reader->numChannels;

    while (restantes > 0) {
        int aLer = static_cast<int>(juce::jmin<juce::int64>(8192, restantes));
        reader->read(&buffer, 0, aLer, lidos, true, true);

        for (int i = 0; i < aLer; ++i) {
            float soma = 0.0f;
            for (int c = 0; c < numChannels; ++c) {
                soma += buffer.getSample(c, i);
            }
            float amostra = soma / static_cast<float>(numChannels);

            if (!bucketAberto) {
                bucketMin = bucketMax = amostra;
                bucketAberto = true;
            } else {
                bucketMin = juce::jmin(bucketMin, amostra);
                bucketMax = juce::jmax(bucketMax, amostra);
            }
            ++amostrasNoBucketAtual;
            if (amostrasNoBucketAtual >= amostrasPorBucket) {
                onda.minimos.push_back(bucketMin);
                onda.maximos.push_back(bucketMax);
                amostrasNoBucketAtual = 0;
                bucketAberto = false;
            }
        }
        lidos += aLer;
        restantes -= aLer;
    }

    if (bucketAberto) {
        onda.minimos.push_back(bucketMin);
        onda.maximos.push_back(bucketMax);
    }

    return onda;
}

namespace {

juce::Image desenharMiniaturaFormaDeOnda(const FormaDeOnda& onda, int largura, int altura) {
    juce::Image img(juce::Image::RGB, largura, altura, true);
    juce::Graphics g(img);
    g.fillAll(juce::Colour(0xff1e1e20));
    if (onda.minimos.empty()) return img;

    g.setColour(juce::Colour(0xff5b9dff));
    int n = static_cast<int>(onda.minimos.size());
    float meioY = altura / 2.0f;
    for (int x = 0; x < largura; ++x) {
        int idx = juce::jlimit(0, n - 1, static_cast<int>(static_cast<double>(x) / largura * n));
        float y1 = meioY - onda.maximos[static_cast<size_t>(idx)] * meioY;
        float y2 = meioY - onda.minimos[static_cast<size_t>(idx)] * meioY;
        g.drawLine(static_cast<float>(x), y1, static_cast<float>(x), juce::jmax(y1 + 1.0f, y2), 1.0f);
    }
    return img;
}

} // namespace

bool gerarMiniaturaDocumento(const juce::File& origem, const juce::File& destino, int ladoMaximoPx,
                             int* larguraOut, int* alturaOut) {
    if (!origem.existsAsFile()) return false;
    destino.getParentDirectory().createDirectory();
    int w = 0, h = 0;
    bool ok = gerarMiniaturaDocumentoNativa(origem.getFullPathName().toRawUTF8(),
                                           destino.getFullPathName().toRawUTF8(),
                                           ladoMaximoPx, &w, &h);
    if (ok) {
        if (larguraOut) *larguraOut = w;
        if (alturaOut) *alturaOut = h;
    }
    return ok;
}

void gerarEGravarMiniaturaPrincipal(matriz::db::Database& indice, const juce::File& pastaProjeto,
                                     const std::string& itemId, const std::string& arquivoId,
                                     const juce::File& arquivoNoProjeto, CategoriaMidia categoria,
                                     std::optional<double> duracaoSegundosConhecida,
                                     const std::function<void(const std::function<void()>&)>& escreverNoIndice) {
    auto escrever = [&](const std::function<void()>& escrita) {
        if (escreverNoIndice) escreverNoIndice(escrita);
        else escrita();
    };
    try {
        juce::File pastaMiniaturas = pastaProjeto.getChildFile(".miniaturas");
        pastaMiniaturas.createDirectory();
        juce::File destino = pastaMiniaturas.getChildFile(arquivoId + ".jpg");
        int largura = 0;
        int altura = 0;

        if (categoria == CategoriaMidia::Imagem) {
            auto dim = gerarMiniaturaImagem(arquivoNoProjeto, destino);
            largura = dim.largura;
            altura = dim.altura;
        } else if (categoria == CategoriaMidia::Video) {
            double duracao = duracaoSegundosConhecida.value_or(0.0);
            auto frames = gerarKeyframesVideo(arquivoNoProjeto, duracao, 1, pastaMiniaturas, arquivoId,
                                               320, /*primeiroFrame=*/true);
            if (frames.empty()) return;
            if (frames.front().arquivo != destino) frames.front().arquivo.moveFileTo(destino);
            largura = frames.front().dimensao.largura;
            altura = frames.front().dimensao.altura;
        } else if (categoria == CategoriaMidia::Audio) {
            FormaDeOnda onda = calcularFormaDeOnda(arquivoNoProjeto, pastaMiniaturas);
            juce::Image img = desenharMiniaturaFormaDeOnda(onda, 512, 160);
            gravarJpeg(img, destino);
            largura = img.getWidth();
            altura = img.getHeight();

            std::string agora = matriz::model::agoraIso8601();
            escrever([&] { indice.run(
                "INSERT INTO forma_onda (id, item_id, arquivo_id, duracao_segundos, buckets_por_segundo, peaks, gerado_em) "
                "VALUES (?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(arquivo_id) DO UPDATE SET peaks = excluded.peaks, "
                "duracao_segundos = excluded.duracao_segundos, buckets_por_segundo = excluded.buckets_por_segundo, "
                "gerado_em = excluded.gerado_em",
                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                 matriz::db::Value::of(arquivoId), matriz::db::Value::of(onda.duracaoSegundos),
                 matriz::db::Value::of(onda.bucketsPorSegundo), matriz::db::Value::ofBlob(onda.paraBlob()),
                 matriz::db::Value::of(agora)}); });
        } else if (categoria == CategoriaMidia::Documento || categoria == CategoriaMidia::Texto) {
            int w = 0, h = 0;
            if (gerarMiniaturaDocumento(arquivoNoProjeto, destino, 512, &w, &h)) {
                largura = w;
                altura = h;
            } else {
                return;
            }
        } else {
            return;
        }

        if (!destino.existsAsFile()) return;
        std::string relativo = matriz::caminhos::relativoParaBanco(destino, pastaProjeto);
        escrever([&] { indice.run(
            "INSERT INTO miniatura (id, item_id, arquivo_id, tipo, caminho_relativo, largura, altura, gerado_em) "
            "VALUES (?, ?, ?, 'miniatura', ?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(arquivoId), matriz::db::Value::of(relativo),
             matriz::db::Value::of(largura), matriz::db::Value::of(altura), matriz::db::Value::of(matriz::model::agoraIso8601())}); });
    } catch (const std::exception&) {
    }
}

} // namespace matriz::ingest
