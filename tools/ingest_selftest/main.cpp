// Self-test headless da Etapa 2 (§16): motor de ingestão. Cada peça nova da
// Etapa 2 ganha cobertura aqui, sobre mídia sintética gerada com ffmpeg —
// sem depender de arquivos de exemplo versionados no repositório.

#include <JuceHeader.h>

#include <algorithm>
#include <functional>
#include <iostream>

#include "Ficha/FichaDefinition.h"
#include "Ingest/Checksum.h"
#include "Ingest/ClassificadorFalaMusica.h"
#include "Ingest/Duplicata.h"
#include "Ingest/FluxoLote.h"
#include "Ingest/InferenciaEstrutura.h"
#include "Ingest/IngestArquivo.h"
#include "Ingest/LeituraTecnica.h"
#include "Ingest/Miniaturas.h"
#include "Ingest/PainelInconsistencias.h"
#include "Model/Project.h"

namespace {

int failures = 0;

void check(bool condition, const std::string& description) {
    if (condition) {
        std::cout << "  OK   " << description << "\n";
    } else {
        std::cout << "  FAIL " << description << "\n";
        ++failures;
    }
}

bool ffmpegDisponivel() {
    juce::ChildProcess proc;
    if (!proc.start(juce::StringArray{"ffmpeg", "-version"}, juce::ChildProcess::wantStdOut))
        return false;
    proc.readAllProcessOutput();
    proc.waitForProcessToFinish(5000);
    return proc.getExitCode() == 0;
}

void gerarComFfmpeg(const juce::StringArray& args) {
    juce::ChildProcess proc;
    if (!proc.start(args, juce::ChildProcess::wantStdOut))
        throw std::runtime_error("não foi possível iniciar o ffmpeg");
    proc.readAllProcessOutput();
    proc.waitForProcessToFinish(30000);
    if (proc.getExitCode() != 0)
        throw std::runtime_error("ffmpeg terminou com erro ao gerar mídia de teste");
}

void testarChecksum(const juce::File& dir) {
    std::cout << "== Checksum ==\n";
    juce::File f = dir.getChildFile("checksum_test.bin");
    f.replaceWithText("conteudo de teste do MATRIZ");

    auto c1 = matriz::ingest::calcularChecksums(f);
    check(c1.md5.size() == 32, "MD5 tem 32 hex chars: " + c1.md5);
    check(c1.sha256.size() == 64, "SHA-256 tem 64 hex chars: " + c1.sha256);

    auto c2 = matriz::ingest::calcularChecksums(f);
    check(c1.md5 == c2.md5 && c1.sha256 == c2.sha256, "checksum é determinístico (mesmo arquivo, mesmo hash)");

    f.replaceWithText("conteudo DIFERENTE");
    auto c3 = matriz::ingest::calcularChecksums(f);
    check(c3.sha256 != c1.sha256, "checksum muda quando o conteúdo muda");
}

void testarLeituraTecnicaAudio(const juce::File& dir) {
    std::cout << "== Leitura técnica — áudio (ffprobe) ==\n";

    juce::File wav = dir.getChildFile("tom_1khz.wav");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=1000:duration=2", "-ar", "48000", "-ac", "2",
                    "-sample_fmt", "s16", wav.getFullPathName()});

    try {
        auto r = matriz::ingest::lerTecnica(wav);
        check(r.duracaoSegundos.has_value() && *r.duracaoSegundos > 1.5 && *r.duracaoSegundos < 2.5,
              "duração ~2s lida corretamente");
        check(r.sampleRate.has_value() && *r.sampleRate == 48000, "sample rate 48000 lido corretamente");
        check(r.canais.has_value() && *r.canais == 2, "2 canais lidos corretamente");
        check(r.codec == "pcm_s16le", "codec pcm_s16le lido corretamente: " + r.codec);
        check(!r.codecLossyDeclarado, "PCM não é marcado como lossy declarado");
        check(!matriz::ingest::paraJson(r).empty(), "caracteristicas_tecnicas_json não fica vazio");
    } catch (const std::exception& e) {
        check(false, std::string("leitura técnica de áudio PCM: ") + e.what());
    }

    juce::File mp3 = dir.getChildFile("tom_1khz.mp3");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=1000:duration=1", "-c:a", "libmp3lame", "-b:a", "128k",
                    mp3.getFullPathName()});
    try {
        auto r = matriz::ingest::lerTecnica(mp3);
        check(r.codec == "mp3", "codec mp3 lido corretamente: " + r.codec);
        check(r.codecLossyDeclarado, "mp3 é marcado como lossy declarado (§7.2)");
    } catch (const std::exception& e) {
        check(false, std::string("leitura técnica de áudio mp3: ") + e.what());
    }
}

void testarLeituraTecnicaImagem(const juce::File& dir) {
    std::cout << "== Leitura técnica — imagem (sips) ==\n";

    juce::File jpg = dir.getChildFile("teste.jpg");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "color=c=blue:s=64x48", "-frames:v", "1", jpg.getFullPathName()});

    try {
        auto r = matriz::ingest::lerTecnica(jpg);
        check(r.larguraPx.has_value() && *r.larguraPx == 64, "largura 64px lida corretamente");
        check(r.alturaPx.has_value() && *r.alturaPx == 48, "altura 48px lida corretamente");
        check(!matriz::ingest::paraJson(r).empty(), "caracteristicas_tecnicas_json não fica vazio");
    } catch (const std::exception& e) {
        check(false, std::string("leitura técnica de imagem: ") + e.what());
    }
}

void testarLeituraTecnicaPdf(const juce::File& dir) {
    std::cout << "== Leitura técnica — PDF (contagem de páginas por varredura) ==\n";

    juce::File pdf = dir.getChildFile("teste_2paginas.pdf");
    juce::String conteudo =
        "%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>\nendobj\n"
        "4 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>\nendobj\n"
        "trailer\n<< /Size 5 /Root 1 0 R >>\n%%EOF\n";
    pdf.replaceWithText(conteudo);

    try {
        auto r = matriz::ingest::lerTecnica(pdf);
        std::string json = matriz::ingest::paraJson(r);
        check(json.find("\"pageCountEstimado\": 2") != std::string::npos ||
                  json.find("\"pageCountEstimado\":2") != std::string::npos,
              "2 páginas contadas corretamente (não conta o nó /Pages pai): " + json);
    } catch (const std::exception& e) {
        check(false, std::string("leitura técnica de PDF: ") + e.what());
    }
}

void testarMiniaturas(const juce::File& dir) {
    std::cout << "== Miniaturas, keyframes e forma de onda ==\n";

    juce::File imgGrande = dir.getChildFile("grande.jpg");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "color=c=green:s=800x600", "-frames:v", "1", imgGrande.getFullPathName()});

    try {
        juce::File miniatura = dir.getChildFile("mini.jpg");
        auto dim = matriz::ingest::gerarMiniaturaImagem(imgGrande, miniatura, 200);
        check(miniatura.existsAsFile(), "miniatura de imagem gerada em disco");
        check(dim.largura <= 200 && dim.altura <= 200 && dim.largura > 0,
              "miniatura respeita o lado máximo (" + std::to_string(dim.largura) + "x" + std::to_string(dim.altura) + ")");
    } catch (const std::exception& e) {
        check(false, std::string("miniatura de imagem: ") + e.what());
    }

    juce::File video = dir.getChildFile("video_teste.mp4");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "testsrc=size=320x240:rate=10:duration=3", "-pix_fmt", "yuv420p",
                    video.getFullPathName()});

    try {
        juce::File dirKeyframes = dir.getChildFile("keyframes");
        auto kfs = matriz::ingest::gerarKeyframesVideo(video, 3.0, 4, dirKeyframes, "kf", 160);
        check(kfs.size() == 4, "quantidade de keyframes gerados == 4");
        bool todosExistem = true;
        bool temposCrescentes = true;
        for (size_t i = 0; i < kfs.size(); ++i) {
            if (!kfs[i].arquivo.existsAsFile()) todosExistem = false;
            if (i > 0 && kfs[i].tempoSegundos <= kfs[i - 1].tempoSegundos) temposCrescentes = false;
        }
        check(todosExistem, "todos os arquivos de keyframe existem em disco");
        check(temposCrescentes, "tempos dos keyframes são crescentes ao longo do vídeo");
        check(kfs.front().dimensao.largura == 160, "keyframe respeita a largura pedida (160px)");
    } catch (const std::exception& e) {
        check(false, std::string("keyframes de vídeo: ") + e.what());
    }

    juce::File tomAudio = dir.getChildFile("tom_forma_onda.wav");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=440:duration=5", "-ar", "44100", tomAudio.getFullPathName()});

    try {
        auto onda = matriz::ingest::calcularFormaDeOnda(tomAudio, dir, 20.0);
        check(onda.duracaoSegundos > 4.5 && onda.duracaoSegundos < 5.5,
              "duração da forma de onda ~5s: " + std::to_string(onda.duracaoSegundos));
        check(!onda.minimos.empty() && onda.minimos.size() == onda.maximos.size(),
              "peaks gerados (" + std::to_string(onda.minimos.size()) + " buckets)");
        bool dentroDaFaixa = true;
        for (size_t i = 0; i < onda.minimos.size(); ++i)
            if (onda.minimos[i] < -1.5f || onda.maximos[i] > 1.5f) dentroDaFaixa = false;
        check(dentroDaFaixa, "peaks dentro da faixa esperada de amplitude (~[-1,1])");
        // O filtro "sine" deste build de ffmpeg não expõe parâmetro de amplitude e gera
        // um tom de nível baixo (~0.125) por padrão — o limiar aqui é só "não é silêncio".
        check(onda.maximos.front() > 0.05f, "amplitude do tom de 440Hz é audível nos peaks (não é silêncio)");

        auto blob = onda.paraBlob();
        check(blob.size() == onda.minimos.size() * 2 * sizeof(float), "blob serializado tem o tamanho esperado");
    } catch (const std::exception& e) {
        check(false, std::string("forma de onda: ") + e.what());
    }
}

void testarDuplicataPHash(const juce::File& dir) {
    std::cout << "== pHash de imagem ==\n";

    juce::File a = dir.getChildFile("phash_a.jpg");
    juce::File aRedigitalizada = dir.getChildFile("phash_a2.jpg");
    juce::File b = dir.getChildFile("phash_b.jpg");

    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "testsrc=size=256x256", "-frames:v", "1", "-q:v", "2", a.getFullPathName()});
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "testsrc=size=256x256", "-frames:v", "1", "-q:v", "20", aRedigitalizada.getFullPathName()});
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "mandelbrot=size=256x256", "-frames:v", "1", b.getFullPathName()});

    try {
        uint64_t hashA = matriz::ingest::calcularPHashImagem(a, dir);
        uint64_t hashA2 = matriz::ingest::calcularPHashImagem(aRedigitalizada, dir);
        uint64_t hashB = matriz::ingest::calcularPHashImagem(b, dir);

        int distAA2 = matriz::ingest::distanciaHamming(hashA, hashA2);
        int distAB = matriz::ingest::distanciaHamming(hashA, hashB);

        check(distAA2 <= 10, "mesma imagem recomprimida em qualidade diferente: distância baixa (" +
                                  std::to_string(distAA2) + ")");
        check(distAB > distAA2, "imagens completamente diferentes têm distância maior (" +
                                     std::to_string(distAB) + " > " + std::to_string(distAA2) + ")");
        check(matriz::ingest::provavelmenteDuplicadaPHash(hashA, hashA2), "provavelmenteDuplicadaPHash confirma o par similar");
        check(!matriz::ingest::provavelmenteDuplicadaPHash(hashA, hashB), "provavelmenteDuplicadaPHash rejeita o par diferente");
    } catch (const std::exception& e) {
        check(false, std::string("pHash: ") + e.what());
    }
}

void testarDuplicataFingerprintAudio(const juce::File& dir) {
    std::cout << "== Fingerprint de áudio ==\n";

    juce::File a = dir.getChildFile("fp_a.wav");
    juce::File aReencodada = dir.getChildFile("fp_a2.wav");
    juce::File aMp3 = dir.getChildFile("fp_a.mp3");
    juce::File b = dir.getChildFile("fp_b.wav");

    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=440:duration=6", "-ar", "44100", a.getFullPathName()});
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", a.getFullPathName(),
                    "-c:a", "libmp3lame", "-b:a", "192k", aMp3.getFullPathName()});
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", aMp3.getFullPathName(),
                    "-ar", "44100", aReencodada.getFullPathName()});
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=3000:duration=6", "-ar", "44100", b.getFullPathName()});

    try {
        auto fpA = matriz::ingest::calcularFingerprintAudio(a, dir);
        auto fpA2 = matriz::ingest::calcularFingerprintAudio(aReencodada, dir);
        auto fpB = matriz::ingest::calcularFingerprintAudio(b, dir);

        double simAA2 = matriz::ingest::similaridadeFingerprint(fpA, fpA2);
        double simAB = matriz::ingest::similaridadeFingerprint(fpA, fpB);

        check(simAA2 > 0.7, "mesmo tom, ida e volta por mp3: similaridade alta (" + std::to_string(simAA2) + ")");
        check(simAB < simAA2, "tons de frequência muito diferente: similaridade menor (" + std::to_string(simAB) +
                                   " < " + std::to_string(simAA2) + ")");
    } catch (const std::exception& e) {
        check(false, std::string("fingerprint de áudio: ") + e.what());
    }
}

void testarCorteDeBanda(const juce::File& dir) {
    std::cout << "== Detecção de corte de banda (lossy) ==\n";

    juce::File fullband = dir.getChildFile("fullband.wav");
    juce::File cortado = dir.getChildFile("cortado.wav");

    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "anoisesrc=color=white:duration=6:sample_rate=44100", fullband.getFullPathName()});
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", fullband.getFullPathName(),
                    "-af", "lowpass=f=12000:poles=2,lowpass=f=12000:poles=2,lowpass=f=12000:poles=2,lowpass=f=12000:poles=2",
                    cortado.getFullPathName()});

    try {
        auto analiseFullband = matriz::ingest::detectarCorteDeBanda(fullband, dir);
        auto analiseCortado = matriz::ingest::detectarCorteDeBanda(cortado, dir);

        check(!analiseFullband.corteDetectado, "ruído branco full-band não é marcado como cortado");
        check(analiseCortado.corteDetectado, "áudio com lowpass em 12kHz é detectado como cortado");
        if (analiseCortado.corteDetectado && analiseCortado.freqCorteEstimadaHz) {
            double f = *analiseCortado.freqCorteEstimadaHz;
            check(f > 8000.0 && f < 17000.0, "frequência de corte estimada é plausível (~12kHz): " + std::to_string(f));
        }
    } catch (const std::exception& e) {
        check(false, std::string("detecção de corte de banda: ") + e.what());
    }
}

void testarClassificadorFalaMusica(const juce::File& dir) {
    std::cout << "== Classificador fala x música (heurístico DSP) ==\n";

    juce::File tomSustentado = dir.getChildFile("cfm_musica.wav");
    juce::File tomModulado = dir.getChildFile("cfm_fala.wav");

    // Proxy sintético de "música": tom contínuo, envelope de energia estável,
    // sem modulação silábica.
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=220:duration=12", "-ar", "22050", tomSustentado.getFullPathName()});

    // Proxy sintético de "fala": mesmo tom com modulação de amplitude forte em
    // 4Hz (taxa silábica) via tremolo — a assinatura que o classificador busca.
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=220:duration=12", "-af", "tremolo=f=4:d=0.9", "-ar", "22050",
                    tomModulado.getFullPathName()});

    try {
        auto resMusica = matriz::ingest::classificarFalaMusica(tomSustentado, dir);
        auto resFala = matriz::ingest::classificarFalaMusica(tomModulado, dir);

        check(resFala.energiaModulacao4HzRatio > resMusica.energiaModulacao4HzRatio,
              "tom modulado em 4Hz tem mais energia de modulação silábica que o tom sustentado (" +
                  std::to_string(resFala.energiaModulacao4HzRatio) + " > " +
                  std::to_string(resMusica.energiaModulacao4HzRatio) + ")");

        check(resMusica.rotulo.has_value() && *resMusica.rotulo == "musica",
              "tom sustentado é classificado como música: " + resMusica.rotulo.value_or("(nenhum)"));
        check(resFala.rotulo.has_value() && *resFala.rotulo == "fala",
              "tom com modulação de 4Hz é classificado como fala: " + resFala.rotulo.value_or("(nenhum)"));
        check(resMusica.confianca > 0.0 && resFala.confianca > 0.0, "ambos os resultados carregam confiança > 0");
    } catch (const std::exception& e) {
        check(false, std::string("classificador fala x música: ") + e.what());
    }
}

void testarInferenciaEstrutura(const juce::File& dir) {
    std::cout << "== Inferência de estrutura de pasta (§7.3) ==\n";

    {
        auto r = matriz::ingest::inferirDePastaRelease("Chico Buarque - Construcao (1971)");
        check(r.artista.has_value() && *r.artista == "Chico Buarque", "artista extraído do nome da pasta");
        check(r.album.has_value() && *r.album == "Construcao", "álbum extraído do nome da pasta");
        check(r.ano.has_value() && *r.ano == 1971, "ano extraído do nome da pasta");
    }
    {
        auto r = matriz::ingest::inferirDePastaRelease("pasta qualquer sem padrao");
        check(!r.artista && !r.album && !r.ano, "pasta sem o padrão não gera inferência inventada");
    }
    {
        auto r = matriz::ingest::inferirDeNomeArquivoFaixa("01 - Faixa.wav");
        check(r.numero.has_value() && *r.numero == 1, "número da faixa extraído (\"01 - Faixa.wav\")");
        check(r.titulo.has_value() && *r.titulo == "Faixa", "título da faixa extraído (\"01 - Faixa.wav\")");
    }
    {
        auto r = matriz::ingest::inferirDeNomeArquivoFaixa("12.Outra Faixa.mp3");
        check(r.numero.has_value() && *r.numero == 12, "número da faixa extraído (separador por ponto)");
        check(r.titulo.has_value() && *r.titulo == "Outra Faixa", "título da faixa extraído (separador por ponto)");
    }
    {
        auto r = matriz::ingest::inferirDeNomeArquivoFaixa("semnumero.wav");
        check(!r.numero && !r.titulo, "nome sem numeração não gera inferência inventada");
    }

    juce::File mp3Tag = dir.getChildFile("com_tags.mp3");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=300:duration=1", "-c:a", "libmp3lame",
                    "-metadata", "title=Minha Faixa", "-metadata", "artist=Minha Artista",
                    "-metadata", "composer=Meu Compositor", mp3Tag.getFullPathName()});
    try {
        auto leitura = matriz::ingest::lerTecnica(mp3Tag);
        auto tags = matriz::ingest::extrairTagsComuns(leitura);
        check(tags.titulo.has_value() && *tags.titulo == "Minha Faixa", "título extraído das tags ID3: " + tags.titulo.value_or("(nenhum)"));
        check(tags.artista.has_value() && *tags.artista == "Minha Artista", "artista extraído das tags ID3");
        check(tags.compositor.has_value() && *tags.compositor == "Meu Compositor", "compositor extraído das tags ID3");
    } catch (const std::exception& e) {
        check(false, std::string("extração de tags embutidas: ") + e.what());
    }

    check(matriz::ingest::provavelCapaFrente({3000, 3000}), "imagem 3000x3000 é provável capa frente");
    check(!matriz::ingest::provavelCapaFrente({3000, 2000}), "imagem 3000x2000 (não quadrada) não é capa frente");
    check(!matriz::ingest::provavelCapaFrente({500, 500}), "imagem 500x500 (pequena demais) não é capa frente");

    check(matriz::ingest::provavelmenteMesmaGravacao(120.3, 120.8, 0.9), "durações próximas + fingerprint similar => mesma gravação");
    check(!matriz::ingest::provavelmenteMesmaGravacao(120.3, 120.8, 0.5), "fingerprint dissimilar => não é a mesma gravação mesmo com duração igual");
    check(!matriz::ingest::provavelmenteMesmaGravacao(120.0, 200.0, 0.95), "duração muito diferente => não é a mesma gravação mesmo com fingerprint parecido");

    {
        using matriz::ingest::ItemParaAgrupar;
        std::vector<ItemParaAgrupar> itens = {
            {"foto1", 1000.0}, {"foto2", 1005.0}, {"foto3", 1300.0}, // grupo 1: gaps pequenos
            {"foto4", 9000.0}, {"foto5", 9010.0},                    // grupo 2: salto grande antes
        };
        auto grupos = matriz::ingest::agruparPorSaltoDeTimestamp(itens, 1800.0);
        check(grupos.size() == 2, "agrupamento por timestamp produz 2 grupos: " + std::to_string(grupos.size()));
        if (grupos.size() == 2) {
            check(grupos[0].idsOrdenados.size() == 3, "primeiro grupo tem 3 fotos");
            check(grupos[1].idsOrdenados.size() == 2, "segundo grupo tem 2 fotos");
            check(grupos[0].idsOrdenados[0] == "foto1" && grupos[0].idsOrdenados[2] == "foto3",
                  "primeiro grupo mantém ordem cronológica");
        }
    }
}

void testarPipelineCompletoDeIngest(const juce::File& dirTemp) {
    std::cout << "== Pipeline completo: ingest -> ficha em lote -> painel de inconsistências ==\n";

    juce::File pastaProjeto = dirTemp.getChildFile("projeto_pipeline_" + juce::Uuid().toDashedString());

    matriz::model::NovoProjetoParams params;
    params.nome = "Selo de Teste";
    params.modo = matriz::model::Modo::Catalogo;
    params.prefixoNomenclatura = "SEL";
    params.isrcRegistrante = "BR-XYZ";

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        std::string agora = matriz::model::agoraIso8601();

        // Definições de ficha carregadas pra alimentar o painel (capa abaixo do mínimo precisa saber o mínimo exigido).
        std::map<std::string, matriz::ficha::FichaDefinition> definicoes;
        definicoes.emplace("release", matriz::ficha::loadFromFile(
                                           juce::File(MATRIZ_FICHAS_DIR).getChildFile("release.yaml").getFullPathName().toStdString()));

        // --- Item 1: release com dois masters de sample rate diferente, um em lossy, faixa 1 sem ISRC ---
        std::string item1 = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
            "VALUES (?, ?, 'SEL-001', 'Release Um', 'release', ?, ?)",
            {matriz::db::Value::of(item1), matriz::db::Value::of(projeto->projetoId()), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});

        // faixa 1 sem isrc, faixa 2 com isrc duplicado (proposital, ver item2 abaixo)
        projeto->registro().run(
            "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'faixa', 1, 'numero', '1', 'humano', ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item1), matriz::db::Value::of(agora)});
        projeto->registro().run(
            "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'faixa', 1, 'splits', ?, 'humano', ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item1),
             matriz::db::Value::of(std::string(R"([{"pessoa":"A","papel":"compositor","percentual":60},)"
                                                 R"({"pessoa":"B","papel":"compositor","percentual":30}])")),
             matriz::db::Value::of(agora)}); // soma 90, não 100
        projeto->registro().run(
            "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'faixa', 2, 'isrc', 'BRXYZ2400001', 'humano', ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item1), matriz::db::Value::of(agora)});

        juce::File masterA = dirTemp.getChildFile("master_44k.wav");
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                        "-i", "sine=frequency=440:duration=2", "-ar", "44100", masterA.getFullPathName()});
        juce::File masterB = dirTemp.getChildFile("master_48k.wav");
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                        "-i", "sine=frequency=440:duration=2", "-ar", "48000", masterB.getFullPathName()});
        juce::File masterLossy = dirTemp.getChildFile("master_lossy.mp3");
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                        "-i", "sine=frequency=440:duration=2", "-c:a", "libmp3lame", masterLossy.getFullPathName()});

        auto ing1 = matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, item1, masterA, "preservation_master", true);
        matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, item1, masterB, "preservation_master", true);
        matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, item1, masterLossy, "preservation_master", true);

        // --- Item 2: só pra provocar ISRC duplicado com a faixa 2 do item 1 ---
        std::string item2 = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
            "VALUES (?, ?, 'SEL-002', 'Release Dois', 'release', ?, ?)",
            {matriz::db::Value::of(item2), matriz::db::Value::of(projeto->projetoId()), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
        projeto->registro().run(
            "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'faixa', 1, 'isrc', 'BRXYZ2400001', 'humano', ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item2), matriz::db::Value::of(agora)});

        // --- Painel de inconsistências: verifica que cada problema plantado aparece ---
        auto problemas = matriz::ingest::detectarInconsistenciasFicha(projeto->registro(), definicoes);
        auto temTipo = [&](const std::string& tipo) {
            return std::any_of(problemas.begin(), problemas.end(), [&](auto& i) { return i.tipo == tipo; });
        };
        check(temTipo("faixa_sem_isrc"), "painel detecta faixa sem ISRC");
        check(temTipo("release_sem_capa"), "painel detecta release sem capa (item1 e item2 não têm capa)");
        check(temTipo("master_lossy"), "painel detecta master em codec lossy");
        check(temTipo("sample_rate_divergente"), "painel detecta sample rate divergente entre masters do mesmo item");
        check(temTipo("splits_nao_somam_100"), "painel detecta splits que não somam 100");
        check(temTipo("isrc_duplicado"), "painel detecta ISRC duplicado entre itens");

        // --- Corrige "release sem capa" pro item1 e confirma que some do painel pra ele ---
        juce::File capaPequena = dirTemp.getChildFile("capa_pequena.jpg");
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                        "-i", "color=c=red:s=500x500", "-frames:v", "1", capaPequena.getFullPathName()});
        matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, item1, capaPequena, "capa_frente", false);

        auto problemas2 = matriz::ingest::detectarInconsistenciasFicha(projeto->registro(), definicoes);
        bool item1AindaSemCapa = std::any_of(problemas2.begin(), problemas2.end(), [&](auto& i) {
            return i.tipo == "release_sem_capa" && i.itemId == item1;
        });
        check(!item1AindaSemCapa, "depois de ingerir a capa, item1 não aparece mais em release_sem_capa");
        bool capaAbaixoDoMinimo = std::any_of(problemas2.begin(), problemas2.end(), [&](auto& i) {
            return i.tipo == "capa_abaixo_minimo" && i.itemId == item1;
        });
        check(capaAbaixoDoMinimo, "capa 500x500 é sinalizada como abaixo do mínimo exigido por release.yaml (3000x3000)");

        // --- Fluxo de ficha em lote ---
        matriz::ingest::aplicarFichaEmLote(projeto->registro(), {item1, item2}, "raiz", 0,
                                            {{"genero", "MPB"}}, "operador-teste");
        for (auto& id : {item1, item2}) {
            auto stmt = projeto->registro().prepare(
                "SELECT valor, fonte FROM item_campo WHERE item_id = ? AND campo_id = 'genero'");
            stmt.bind(1, matriz::db::Value::of(id));
            bool achou = stmt.step();
            check(achou && stmt.columnText(0) == "MPB" && stmt.columnText(1) == "humano",
                  "fluxo em lote gravou 'genero' = MPB para o item " + id);
        }
        auto stmtHist = projeto->registro().prepare(
            "SELECT COUNT(*) FROM item_historico WHERE campo_id = 'genero' AND tipo_evento = 'edicao_campo'");
        stmtHist.step();
        check(stmtHist.columnInt(0) == 2, "fluxo em lote gravou 2 eventos de histórico (um por item)");

        // --- Verificação de disco: tudo limpo logo após o ingest ---
        auto verificacao1 = matriz::ingest::verificarArquivosNoDisco(*projeto);
        check(verificacao1.empty(), "nenhuma divergência de disco logo após o ingest (" +
                                         std::to_string(verificacao1.size()) + " encontradas)");

        // --- Corrompe um arquivo no disco e confirma que o checksum divergente aparece ---
        ing1.arquivoNoProjeto.appendText("bytes extras que não deveriam estar aqui");
        auto verificacao2 = matriz::ingest::verificarArquivosNoDisco(*projeto);
        bool checksumDivergenteDetectado = std::any_of(verificacao2.begin(), verificacao2.end(), [&](auto& i) {
            return i.tipo == "checksum_divergente" && i.arquivoId == ing1.arquivoId;
        });
        check(checksumDivergenteDetectado, "arquivo corrompido no disco é detectado como checksum divergente");

        // --- Solta um arquivo órfão dentro de arquivos/ e confirma que aparece ---
        juce::File orfao = pastaProjeto.getChildFile("arquivos").getChildFile("nao_catalogado.txt");
        orfao.replaceWithText("este arquivo nunca passou por ingerirArquivo");
        auto verificacao3 = matriz::ingest::verificarArquivosNoDisco(*projeto);
        bool orfaoDetectado = std::any_of(verificacao3.begin(), verificacao3.end(),
                                           [](auto& i) { return i.tipo == "arquivo_orfao"; });
        check(orfaoDetectado, "arquivo solto na pasta arquivos/ sem registro é detectado como órfão");

    } catch (const std::exception& e) {
        check(false, std::string("pipeline completo de ingest: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
}

void testarCategoriaPorExtensao() {
    std::cout << "== Categoria por extensão ==\n";
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.wav")) == matriz::ingest::CategoriaMidia::Audio, "wav -> Audio");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.mp4")) == matriz::ingest::CategoriaMidia::Video, "mp4 -> Video");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.jpg")) == matriz::ingest::CategoriaMidia::Imagem, "jpg -> Imagem");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.pdf")) == matriz::ingest::CategoriaMidia::Documento, "pdf -> Documento");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.xyz")) == matriz::ingest::CategoriaMidia::Desconhecida, "xyz -> Desconhecida");
}

} // namespace

int main() {
    if (!ffmpegDisponivel()) {
        std::cout << "ffmpeg não disponível — não é possível gerar mídia de teste. Abortando.\n";
        return 1;
    }

    juce::File tmpDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("matriz_ingest_selftest_" + juce::Uuid().toDashedString());
    tmpDir.createDirectory();

    testarCategoriaPorExtensao();
    testarChecksum(tmpDir);
    testarLeituraTecnicaAudio(tmpDir);
    testarLeituraTecnicaImagem(tmpDir);
    testarLeituraTecnicaPdf(tmpDir);
    testarMiniaturas(tmpDir);
    testarDuplicataPHash(tmpDir);
    testarDuplicataFingerprintAudio(tmpDir);
    testarCorteDeBanda(tmpDir);
    testarClassificadorFalaMusica(tmpDir);
    testarInferenciaEstrutura(tmpDir);
    testarPipelineCompletoDeIngest(tmpDir);

    tmpDir.deleteRecursively();

    std::cout << "\n" << (failures == 0 ? "TODOS OS TESTES PASSARAM" : std::to_string(failures) + " FALHA(S)") << "\n";
    return failures == 0 ? 0 : 1;
}
