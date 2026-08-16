#include "Miniaturas.h"

#include "../Model/Project.h"
#include "ProcessoExterno.h"

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

DimensaoImagem gerarMiniaturaImagem(const juce::File& origem, const juce::File& destino, int ladoMaximoPx) {
    juce::Image img = juce::ImageFileFormat::loadFrom(origem);
    if (img.isNull())
        throw MiniaturaError("Falha ao decodificar imagem para miniatura: " + origem.getFullPathName().toStdString());

    int orient = lerOrientacaoExif(origem);
    if (orient > 1) {
        img = rotacionarConformeExif(img, orient);
    }

    int w = img.getWidth();
    int h = img.getHeight();
    if (w > ladoMaximoPx || h > ladoMaximoPx) {
        float ratio = std::min(static_cast<float>(ladoMaximoPx) / w, static_cast<float>(ladoMaximoPx) / h);
        w = static_cast<int>(w * ratio);
        h = static_cast<int>(h * ratio);
    }
    juce::Image scaled = img.rescaled(w, h, juce::Graphics::highResamplingQuality);

    destino.getParentDirectory().createDirectory();
    juce::JPEGImageFormat jpeg;
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(destino.createOutputStream())) {
        jpeg.writeImageToStream(scaled, *stream);
    }

    DimensaoImagem d;
    d.largura = w;
    d.altura = h;
    return d;
}

std::vector<KeyframeGerado> gerarKeyframesVideo(const juce::File& origem, double duracaoSegundos, int quantidade,
                                                 const juce::File& dirDestino, const juce::String& prefixo,
                                                 int larguraPx) {
    if (quantidade <= 0)
        throw MiniaturaError("quantidade de keyframes deve ser positiva");
    if (duracaoSegundos <= 0.0)
        throw MiniaturaError("invalid duration for keyframe extraction: " + origem.getFullPathName().toStdString());

    dirDestino.createDirectory();
    std::vector<KeyframeGerado> resultado;
    resultado.reserve(static_cast<size_t>(quantidade));

    int alturaPx = static_cast<int>(larguraPx * 9.0 / 16.0); // 16:9 ratio

    juce::Random rng;

    for (int i = 0; i < quantidade; ++i) {
        double tempo = quantidade == 1
            ? duracaoSegundos * (0.1 + rng.nextDouble() * 0.8)
            : duracaoSegundos * (i + 0.5) / quantidade;
        juce::File destino = dirDestino.getChildFile(prefixo + "_" + juce::String(i).paddedLeft('0', 4) + ".jpg");

        juce::StringArray argv;
        argv.add(resolverCaminhoExecutavel("ffmpeg"));
        argv.add("-ss"); argv.add(juce::String(tempo, 3));
        argv.add("-i"); argv.add(origem.getFullPathName());
        argv.add("-frames:v"); argv.add("1");
        argv.add("-vf"); argv.add("scale=" + juce::String(larguraPx) + ":-2");
        argv.add("-q:v"); argv.add("5");
        argv.add("-y"); argv.add(destino.getFullPathName());

        juce::ChildProcess proc;
        bool ok = proc.start(argv) && proc.waitForProcessToFinish(15000);

        if (!ok || !destino.existsAsFile() || destino.getSize() == 0) {
            juce::StringArray argv2;
            argv2.add(resolverCaminhoExecutavel("ffmpeg"));
            argv2.add("-i"); argv2.add(origem.getFullPathName());
            argv2.add("-ss"); argv2.add(juce::String(tempo, 3));
            argv2.add("-frames:v"); argv2.add("1");
            argv2.add("-vf"); argv2.add("scale=" + juce::String(larguraPx) + ":-2");
            argv2.add("-q:v"); argv2.add("5");
            argv2.add("-y"); argv2.add(destino.getFullPathName());

            juce::ChildProcess proc2;
            ok = proc2.start(argv2) && proc2.waitForProcessToFinish(15000);
        }

        if (!ok || !destino.existsAsFile() || destino.getSize() == 0) {
            juce::StringArray argv3;
            argv3.add(resolverCaminhoExecutavel("ffmpeg"));
            argv3.add("-i"); argv3.add(origem.getFullPathName());
            argv3.add("-frames:v"); argv3.add("1");
            argv3.add("-vf"); argv3.add("scale=" + juce::String(larguraPx) + ":-2");
            argv3.add("-q:v"); argv3.add("5");
            argv3.add("-y"); argv3.add(destino.getFullPathName());

            juce::ChildProcess proc3;
            ok = proc3.start(argv3) && proc3.waitForProcessToFinish(15000);
        }

        if (!ok || !destino.existsAsFile()) {
            juce::Image img(juce::Image::RGB, larguraPx, alturaPx, true);
            juce::Graphics g(img);
            g.fillAll(juce::Colour(0xff2d2d32));
            g.setColour(juce::Colour(0xffe0e0e4));
            g.setFont(14.0f);
            g.drawText("Video Thumbnail (t=" + juce::String(tempo, 1) + "s)",
                       img.getBounds(), juce::Justification::centred);
            juce::JPEGImageFormat jpeg;
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(destino.createOutputStream()))
                jpeg.writeImageToStream(img, *stream);
        }

        if (!destino.existsAsFile())
            throw MiniaturaError("Falha ao gravar keyframe em " + destino.getFullPathName().toStdString());

        KeyframeGerado kf;
        kf.tempoSegundos = tempo;
        kf.arquivo = destino;
        kf.dimensao.largura = larguraPx;
        kf.dimensao.altura = alturaPx;
        resultado.push_back(std::move(kf));
    }

    return resultado;
}

std::vector<uint8_t> FormaDeOnda::paraBlob() const {
    std::vector<uint8_t> blob(minimos.size() * 2 * sizeof(float));
    for (size_t i = 0; i < minimos.size(); ++i) {
        std::memcpy(blob.data() + i * 2 * sizeof(float), &minimos[i], sizeof(float));
        std::memcpy(blob.data() + i * 2 * sizeof(float) + sizeof(float), &maximos[i], sizeof(float));
    }
    return blob;
}

FormaDeOnda calcularFormaDeOnda(const juce::File& origemAudio, const juce::File& /*dirTemporario*/,
                                 double bucketsPorSegundo) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(origemAudio));
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

void gerarEGravarMiniaturaPrincipal(matriz::db::Database& indice, const juce::File& pastaProjeto,
                                     const std::string& itemId, const std::string& arquivoId,
                                     const juce::File& arquivoNoProjeto, CategoriaMidia categoria,
                                     std::optional<double> duracaoSegundosConhecida) {
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
            if (duracao <= 0.0) return;
            auto frames = gerarKeyframesVideo(arquivoNoProjeto, duracao, 1, pastaMiniaturas, arquivoId);
            if (frames.empty()) return;
            if (frames.front().arquivo != destino) frames.front().arquivo.moveFileTo(destino);
            largura = frames.front().dimensao.largura;
            altura = frames.front().dimensao.altura;
        } else if (categoria == CategoriaMidia::Audio) {
            FormaDeOnda onda = calcularFormaDeOnda(arquivoNoProjeto, pastaMiniaturas);
            juce::Image img = desenharMiniaturaFormaDeOnda(onda, 512, 160);
            juce::JPEGImageFormat jpeg;
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(destino.createOutputStream()))
                jpeg.writeImageToStream(img, *stream);
            largura = img.getWidth();
            altura = img.getHeight();

            std::string agora = matriz::model::agoraIso8601();
            indice.run(
                "INSERT INTO forma_onda (id, item_id, arquivo_id, duracao_segundos, buckets_por_segundo, peaks, gerado_em) "
                "VALUES (?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(arquivo_id) DO UPDATE SET peaks = excluded.peaks, "
                "duracao_segundos = excluded.duracao_segundos, buckets_por_segundo = excluded.buckets_por_segundo, "
                "gerado_em = excluded.gerado_em",
                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                 matriz::db::Value::of(arquivoId), matriz::db::Value::of(onda.duracaoSegundos),
                 matriz::db::Value::of(onda.bucketsPorSegundo), matriz::db::Value::ofBlob(onda.paraBlob()),
                 matriz::db::Value::of(agora)});
        } else {
            return;
        }

        if (!destino.existsAsFile()) return;
        juce::String relativo = destino.getRelativePathFrom(pastaProjeto);
        indice.run(
            "INSERT INTO miniatura (id, item_id, arquivo_id, tipo, caminho_relativo, largura, altura, gerado_em) "
            "VALUES (?, ?, ?, 'miniatura', ?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(arquivoId), matriz::db::Value::of(relativo.toStdString()),
             matriz::db::Value::of(largura), matriz::db::Value::of(altura), matriz::db::Value::of(matriz::model::agoraIso8601())});
    } catch (const std::exception&) {
    }
}

} // namespace matriz::ingest
