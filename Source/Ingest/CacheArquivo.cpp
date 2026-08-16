#include "CacheArquivo.h"

#include <cmath>
#include <cstring>

#include "../Model/Project.h"
#include "Loudness.h"
#include "Miniaturas.h"

namespace matriz::ingest {

using matriz::db::Value;

namespace {

// Teto de leitura, o mesmo de medirLoudnessDoArquivo: 10 minutos cobrem o
// material típico e dão medida estável sem carregar um arquivo de horas
// inteiro na memória de um job de ingest.
constexpr double kMaxSegundosAnalise = 600.0;

// Resolução da forma de onda guardada. 20 baldes por segundo desenha bem
// numa timeline de tela cheia e mantém o blob pequeno: uma hora de áudio dá
// 72 mil baldes, ~576 KB — cabe no registro sem inchar o projeto.
constexpr double kBucketsPorSegundo = 20.0;

std::vector<uint8_t> lerBytes(const juce::File& arquivo) {
    juce::MemoryBlock bloco;
    if (!arquivo.loadFileAsData(bloco)) return {};
    auto* dados = static_cast<const uint8_t*>(bloco.getData());
    return std::vector<uint8_t>(dados, dados + bloco.getSize());
}

// Correlação de fase média entre L e R (§7 — indicador de estereofonia).
// Pearson sobre o par, na forma normalizada usual de medidor de correlação:
// +1 mono/em fase, 0 descorrelacionado, -1 em oposição de fase.
std::optional<double> correlacaoMedia(const juce::AudioBuffer<float>& audio) {
    if (audio.getNumChannels() < 2) return std::nullopt;  // mono não tem o que correlacionar
    const int n = audio.getNumSamples();
    if (n <= 0) return std::nullopt;

    const float* l = audio.getReadPointer(0);
    const float* r = audio.getReadPointer(1);

    double somaLR = 0.0, somaLL = 0.0, somaRR = 0.0;
    for (int i = 0; i < n; ++i) {
        somaLR += static_cast<double>(l[i]) * r[i];
        somaLL += static_cast<double>(l[i]) * l[i];
        somaRR += static_cast<double>(r[i]) * r[i];
    }

    double denom = std::sqrt(somaLL * somaRR);
    if (denom <= 1.0e-12) return std::nullopt;  // silêncio: correlação indefinida, não zero
    return juce::jlimit(-1.0, 1.0, somaLR / denom);
}

// Forma de onda direto do buffer já decodificado — sem passar por ffmpeg e
// sem arquivo temporário, já que o áudio está na memória de qualquer jeito
// pra medir loudness.
std::vector<uint8_t> formaDeOndaDoBuffer(const juce::AudioBuffer<float>& audio, double sampleRate) {
    if (audio.getNumSamples() <= 0 || sampleRate <= 0.0) return {};

    FormaDeOnda onda;
    onda.duracaoSegundos = audio.getNumSamples() / sampleRate;
    onda.bucketsPorSegundo = kBucketsPorSegundo;

    const int amostrasPorBucket = juce::jmax(1, static_cast<int>(sampleRate / kBucketsPorSegundo));
    const int canais = audio.getNumChannels();

    for (int inicio = 0; inicio < audio.getNumSamples(); inicio += amostrasPorBucket) {
        const int fim = juce::jmin(inicio + amostrasPorBucket, audio.getNumSamples());
        float minimo = 0.0f, maximo = 0.0f;
        for (int c = 0; c < canais; ++c) {
            auto faixa = juce::FloatVectorOperations::findMinAndMax(audio.getReadPointer(c) + inicio, fim - inicio);
            minimo = juce::jmin(minimo, faixa.getStart());
            maximo = juce::jmax(maximo, faixa.getEnd());
        }
        onda.minimos.push_back(minimo);
        onda.maximos.push_back(maximo);
    }

    return onda.paraBlob();
}

// Miniatura de imagem: reduz num temporário e lê os bytes de volta. O
// temporário morre aqui — quem guarda é o blob no registro (I3).
std::vector<uint8_t> miniaturaDeImagem(const juce::File& origem, const juce::File& dirTemporario) {
    juce::File tmp = dirTemporario.getChildFile("cache_min_" + juce::Uuid().toDashedString() + ".png");
    std::vector<uint8_t> bytes;
    try {
        gerarMiniaturaImagem(origem, tmp, 512);
        bytes = lerBytes(tmp);
    } catch (const std::exception&) {
        // Formato que o sips não abre: sem miniatura, com o item catalogado
        // do mesmo jeito. O mosaico cai no ícone por categoria.
    }
    tmp.deleteFile();
    return bytes;
}

std::vector<uint8_t> miniaturaDeVideo(const juce::File& origem, const juce::File& dirTemporario,
                                       std::optional<double> duracaoSegundos) {
    if (!duracaoSegundos || *duracaoSegundos <= 0.0) return {};
    std::vector<uint8_t> bytes;
    juce::File dir = dirTemporario.getChildFile("cache_kf_" + juce::Uuid().toDashedString());
    dir.createDirectory();
    try {
        auto frames = gerarKeyframesVideo(origem, *duracaoSegundos, 1, dir, "kf", 512);
        if (!frames.empty()) bytes = lerBytes(frames.front().arquivo);
    } catch (const std::exception&) {
        // Mesma regra da imagem: falta de prévia não invalida a catalogação.
    }
    dir.deleteRecursively();
    return bytes;
}

} // namespace

AnaliseCache calcularCache(const juce::File& arquivo, CategoriaMidia categoria,
                            const juce::File& dirTemporario, std::optional<double> duracaoSegundos) {
    AnaliseCache out;
    if (!arquivo.existsAsFile()) return out;

    if (categoria == CategoriaMidia::Imagem) {
        out.miniatura = miniaturaDeImagem(arquivo, dirTemporario);
        return out;
    }

    if (categoria == CategoriaMidia::Video) {
        out.miniatura = miniaturaDeVideo(arquivo, dirTemporario, duracaoSegundos);
        // A trilha de áudio de um vídeo precisaria ser extraída com ffmpeg
        // pra ser medida; não é feito aqui (gap declarado, não silencioso).
        return out;
    }

    if (categoria != CategoriaMidia::Audio) return out;

    juce::AudioFormatManager formatos;
    formatos.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> leitor(formatos.createReaderFor(arquivo));
    if (leitor == nullptr || leitor->lengthInSamples <= 0 || leitor->numChannels == 0) return out;

    const juce::int64 maxAmostras = static_cast<juce::int64>(leitor->sampleRate * kMaxSegundosAnalise);
    const int amostras = static_cast<int>(std::min(leitor->lengthInSamples, maxAmostras));

    juce::AudioBuffer<float> buffer(static_cast<int>(leitor->numChannels), amostras);
    if (!leitor->read(&buffer, 0, amostras, 0, true, true)) return out;

    // Uma decodificação, todas as medidas: loudness, pico, correlação e forma
    // de onda saem do MESMO buffer. Ler o arquivo quatro vezes seria o custo
    // de ingest que I2 existe pra evitar.
    auto loudness = medirLoudness(buffer, leitor->sampleRate);
    out.lufsI = loudness.lufsIntegrado;
    out.lra = loudness.lra;
    out.truePeak = loudness.truePeakDbfs;
    out.peakAmostra = static_cast<double>(buffer.getMagnitude(0, buffer.getNumSamples()));
    out.correlacaoMedia = correlacaoMedia(buffer);
    out.formaOnda = formaDeOndaDoBuffer(buffer, leitor->sampleRate);
    return out;
}

void gravarCache(matriz::db::Database& registro, const std::string& arquivoId, const AnaliseCache& analise) {
    auto real = [](const std::optional<double>& v) { return v ? Value::of(*v) : Value::null(); };
    auto blob = [](const std::vector<uint8_t>& b) { return b.empty() ? Value::null() : Value::ofBlob(b); };

    registro.run(
        "INSERT INTO cache_arquivo (arquivo_id, miniatura, forma_onda, lufs_i, lra, true_peak, peak_amostra, "
        "correlacao_media, calculado_em, versao_analise) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(arquivo_id) DO UPDATE SET miniatura = excluded.miniatura, forma_onda = excluded.forma_onda, "
        "lufs_i = excluded.lufs_i, lra = excluded.lra, true_peak = excluded.true_peak, "
        "peak_amostra = excluded.peak_amostra, correlacao_media = excluded.correlacao_media, "
        "calculado_em = excluded.calculado_em, versao_analise = excluded.versao_analise",
        {Value::of(arquivoId), blob(analise.miniatura), blob(analise.formaOnda), real(analise.lufsI),
         real(analise.lra), real(analise.truePeak), real(analise.peakAmostra), real(analise.correlacaoMedia),
         Value::of(matriz::model::agoraIso8601()), Value::of(kVersaoAnalise)});
}

void calcularEGravarCache(matriz::db::Database& registro, const std::string& arquivoId,
                           const juce::File& arquivo, CategoriaMidia categoria,
                           const juce::File& dirTemporario, std::optional<double> duracaoSegundos) {
    try {
        gravarCache(registro, arquivoId, calcularCache(arquivo, categoria, dirTemporario, duracaoSegundos));
    } catch (const std::exception&) {
        // Cache é derivado e reconstruível: nunca derruba o ingest do item.
    }
}

std::optional<AnaliseCache> lerCache(matriz::db::Database& registro, const std::string& arquivoId) {
    auto stmt = registro.prepare(
        "SELECT miniatura, forma_onda, lufs_i, lra, true_peak, peak_amostra, correlacao_media "
        "FROM cache_arquivo WHERE arquivo_id = ? AND versao_analise = ?");
    stmt.bind(1, Value::of(arquivoId));
    stmt.bind(2, Value::of(kVersaoAnalise));
    if (!stmt.step()) return std::nullopt;

    AnaliseCache out;
    out.miniatura = stmt.columnBlob(0);
    out.formaOnda = stmt.columnBlob(1);
    if (!stmt.columnIsNull(2)) out.lufsI = stmt.columnReal(2);
    if (!stmt.columnIsNull(3)) out.lra = stmt.columnReal(3);
    if (!stmt.columnIsNull(4)) out.truePeak = stmt.columnReal(4);
    if (!stmt.columnIsNull(5)) out.peakAmostra = stmt.columnReal(5);
    if (!stmt.columnIsNull(6)) out.correlacaoMedia = stmt.columnReal(6);
    return out;
}

FormaDeOndaLida lerFormaDeOnda(const std::vector<uint8_t>& blob) {
    FormaDeOndaLida out;
    // Pares [min,max] float32 intercalados — 8 bytes por balde.
    const size_t pares = blob.size() / (2 * sizeof(float));
    out.minimos.reserve(pares);
    out.maximos.reserve(pares);
    for (size_t i = 0; i < pares; ++i) {
        float minimo = 0.0f, maximo = 0.0f;
        std::memcpy(&minimo, blob.data() + i * 2 * sizeof(float), sizeof(float));
        std::memcpy(&maximo, blob.data() + i * 2 * sizeof(float) + sizeof(float), sizeof(float));
        out.minimos.push_back(minimo);
        out.maximos.push_back(maximo);
    }
    return out;
}

} // namespace matriz::ingest
