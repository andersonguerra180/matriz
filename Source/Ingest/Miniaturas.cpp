#include "Miniaturas.h"

#include "../Model/Project.h"
#include "ProcessoExterno.h"

#include <cstring>

namespace matriz::ingest {

DimensaoImagem dimensoesImagem(const juce::File& imagem) {
    std::string saida = capturarSaidaTexto(
        "ffprobe", {"-v", "quiet", "-print_format", "json", "-show_streams", imagem.getFullPathName()});
    juce::var root = juce::JSON::parse(juce::String(saida));
    DimensaoImagem d;
    if (root.isObject()) {
        juce::var streams = root["streams"];
        if (streams.isArray() && streams.getArray()->size() > 0) {
            juce::var primeiro = (*streams.getArray())[0];
            d.largura = primeiro["width"].toString().getIntValue();
            d.altura = primeiro["height"].toString().getIntValue();
        }
    }
    if (d.largura == 0 || d.altura == 0)
        throw MiniaturaError("ffprobe não retornou dimensões válidas para: " + imagem.getFullPathName().toStdString());
    return d;
}

DimensaoImagem gerarMiniaturaImagem(const juce::File& origem, const juce::File& destino, int ladoMaximoPx) {
    destino.getParentDirectory().createDirectory();
    // scale com -1/-2 preserva proporção pelo lado que sobra; aqui limitamos
    // o maior lado com force_original_aspect_ratio=decrease, que funciona
    // tanto pra imagem paisagem quanto retrato sem precisar saber qual é.
    rodarEsperandoSucesso("ffmpeg",
                          {"-y", "-hide_banner", "-loglevel", "error", "-i", origem.getFullPathName(), "-vf",
                           "scale=" + juce::String(ladoMaximoPx) + ":" + juce::String(ladoMaximoPx) +
                               ":force_original_aspect_ratio=decrease",
                           destino.getFullPathName()});
    if (!destino.existsAsFile())
        throw MiniaturaError("ffmpeg não gerou a miniatura esperada: " + destino.getFullPathName().toStdString());
    return dimensoesImagem(destino);
}

std::vector<KeyframeGerado> gerarKeyframesVideo(const juce::File& origem, double duracaoSegundos, int quantidade,
                                                 const juce::File& dirDestino, const juce::String& prefixo,
                                                 int larguraPx) {
    if (quantidade <= 0)
        throw MiniaturaError("quantidade de keyframes deve ser positiva");
    if (duracaoSegundos <= 0.0)
        throw MiniaturaError("duração inválida para extração de keyframes: " + origem.getFullPathName().toStdString());

    dirDestino.createDirectory();
    std::vector<KeyframeGerado> resultado;
    resultado.reserve(static_cast<size_t>(quantidade));

    for (int i = 0; i < quantidade; ++i) {
        double tempo = duracaoSegundos * (i + 0.5) / quantidade;
        juce::File destino = dirDestino.getChildFile(prefixo + "_" + juce::String(i).paddedLeft('0', 4) + ".jpg");

        rodarEsperandoSucesso("ffmpeg",
                              {"-y", "-hide_banner", "-loglevel", "error", "-ss", juce::String(tempo),
                               "-i", origem.getFullPathName(), "-frames:v", "1", "-vf",
                               "scale=" + juce::String(larguraPx) + ":-2", destino.getFullPathName()});
        if (!destino.existsAsFile())
            throw MiniaturaError("ffmpeg não gerou o keyframe esperado em t=" + std::to_string(tempo) + "s: " +
                                  origem.getFullPathName().toStdString());

        KeyframeGerado kf;
        kf.tempoSegundos = tempo;
        kf.arquivo = destino;
        kf.dimensao = dimensoesImagem(destino);
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

FormaDeOnda calcularFormaDeOnda(const juce::File& origemAudio, const juce::File& dirTemporario,
                                 double bucketsPorSegundo) {
    constexpr int sampleRatePcm = 22050;

    dirTemporario.createDirectory();
    juce::File pcmTemp = dirTemporario.getChildFile("matriz_pcm_" + juce::Uuid().toDashedString() + ".f32le");

    rodarEsperandoSucesso("ffmpeg",
                          {"-y", "-hide_banner", "-loglevel", "error", "-i", origemAudio.getFullPathName(),
                           "-f", "f32le", "-ac", "1", "-ar", juce::String(sampleRatePcm),
                           pcmTemp.getFullPathName()});

    if (!pcmTemp.existsAsFile()) {
        throw MiniaturaError("ffmpeg não gerou o PCM temporário para forma de onda: " +
                              origemAudio.getFullPathName().toStdString());
    }

    FormaDeOnda onda;

    {
        juce::FileInputStream in(pcmTemp);
        if (!in.openedOk()) {
            pcmTemp.deleteFile();
            throw MiniaturaError("não foi possível ler o PCM temporário: " + pcmTemp.getFullPathName().toStdString());
        }

        juce::int64 numSamples = in.getTotalLength() / static_cast<juce::int64>(sizeof(float));
        int amostrasPorBucket = juce::jmax(1, static_cast<int>(sampleRatePcm / bucketsPorSegundo));

        onda.duracaoSegundos = static_cast<double>(numSamples) / sampleRatePcm;
        onda.bucketsPorSegundo = static_cast<double>(sampleRatePcm) / amostrasPorBucket;

        constexpr int tamanhoBuffer = 8192;
        std::vector<float> buffer(tamanhoBuffer);

        float bucketMin = 0.0f, bucketMax = 0.0f;
        int amostrasNoBucketAtual = 0;
        bool bucketAberto = false;

        juce::int64 restantes = numSamples;
        while (restantes > 0) {
            int aLer = static_cast<int>(juce::jmin<juce::int64>(tamanhoBuffer, restantes));
            int bytesLidos = in.read(buffer.data(), aLer * static_cast<int>(sizeof(float)));
            int amostrasLidas = bytesLidos / static_cast<int>(sizeof(float));
            if (amostrasLidas <= 0) break;

            for (int i = 0; i < amostrasLidas; ++i) {
                float amostra = buffer[static_cast<size_t>(i)];
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
            restantes -= amostrasLidas;
        }
        if (bucketAberto) {
            onda.minimos.push_back(bucketMin);
            onda.maximos.push_back(bucketMax);
        }
    } // `in` fecha o arquivo aqui, antes de apagá-lo

    pcmTemp.deleteFile();

    return onda;
}

namespace {

// Renderiza os peaks min/max diretamente numa imagem — vira o arquivo de
// miniatura de áudio (mesmo pipeline de carregamento que imagem/vídeo já
// usam, sem precisar de um caso especial no mosaico).
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
            return; // Documento/Desconhecida: sem geração de miniatura real nesta etapa
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
        // Nunca propaga — falha de miniatura não pode derrubar o ingest.
    }
}

} // namespace matriz::ingest
