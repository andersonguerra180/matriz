// Self-test headless da Etapa 2 (§16): motor de ingestão. Cada peça nova da
// Etapa 2 ganha cobertura aqui, sobre mídia sintética gerada com ffmpeg —
// sem depender de arquivos de exemplo versionados no repositório.

#include <JuceHeader.h>

#include <exiv2/exiv2.hpp>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>

#include "Catalogo/CatalogoProxies.h"
#include "Consolidacao/Consolidacao.h"
#include "Consolidacao/MetadadoEmbutido.h"
#include "Consolidacao/Mascara.h"
#include "Ficha/FichaDefinition.h"
#include "Ingest/Checksum.h"
#include "Ingest/ClassificadorFalaMusica.h"
#include "Ingest/Duplicata.h"
#include "Ingest/FluxoLote.h"
#include "Ingest/InferenciaEstrutura.h"
#include "Ingest/IngestArquivo.h"
#include "Ingest/LeituraTecnica.h"
#include "Ingest/Loudness.h"
#include "Ingest/Miniaturas.h"
#include "Ingest/PainelInconsistencias.h"
#include "Ingest/CacheArquivo.h"
#include "Model/Project.h"
#include "Publicacao/Publicacao.h"
#include "Vault/Reconciliacao.h"
#include "Vault/Resolucao.h"

#include <map>

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

// Hierarquia de backup usada pelos testes escritos ANTES do item 5 (estrutura
// de pastas automática): só a árvore que o operador montou à mão, que era o
// único comportamento possível então. A estrutura automática padrão
// (Projeto→Ano→Tipo de Mídia→Tipo de Arquivo) tem cobertura própria em
// testarHierarquiaBackup().
matriz::consolidacao::HierarquiaBackup soPastaManual() {
    return {matriz::consolidacao::NivelHierarquia::PastaManual};
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
        throw std::runtime_error("could not start ffmpeg");
    proc.readAllProcessOutput();
    proc.waitForProcessToFinish(30000);
    if (proc.getExitCode() != 0)
        throw std::runtime_error("ffmpeg failed while generating test media");
}

void testarChecksum(const juce::File& dir) {
    std::cout << "== Checksum ==\n";
    juce::File f = dir.getChildFile("checksum_test.bin");
    f.replaceWithText("conteudo de teste do MATRIZ");

    auto c1 = matriz::ingest::calcularChecksums(f);
    check(c1.md5.size() == 32, "MD5 has 32 hex chars: " + c1.md5);
    check(c1.sha256.size() == 64, "SHA-256 has 64 hex chars: " + c1.sha256);

    auto c2 = matriz::ingest::calcularChecksums(f);
    check(c1.md5 == c2.md5 && c1.sha256 == c2.sha256, "the checksum is deterministic (same file, same hash)");

    f.replaceWithText("conteudo DIFERENTE");
    auto c3 = matriz::ingest::calcularChecksums(f);
    check(c3.sha256 != c1.sha256, "the checksum changes when the content changes");
}

void testarLeituraTecnicaAudio(const juce::File& dir) {
    std::cout << "== Technical read - audio (ffprobe) ==\n";

    juce::File wav = dir.getChildFile("tom_1khz.wav");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=1000:duration=2", "-ar", "48000", "-ac", "2",
                    "-sample_fmt", "s16", wav.getFullPathName()});

    try {
        auto r = matriz::ingest::lerTecnica(wav);
        check(r.duracaoSegundos.has_value() && *r.duracaoSegundos > 1.5 && *r.duracaoSegundos < 2.5,
              "duration ~2s read correctly");
        check(r.sampleRate.has_value() && *r.sampleRate == 48000, "sample rate 48000 read correctly");
        check(r.canais.has_value() && *r.canais == 2, "2 channels read correctly");
        check(r.codec == "pcm_s16le", "codec pcm_s16le read correctly: " + r.codec);
        check(!r.codecLossyDeclarado, "PCM is not flagged as declared-lossy");
        check(!matriz::ingest::paraJson(r).empty(), "caracteristicas_tecnicas_json is not left empty");
    } catch (const std::exception& e) {
        check(false, std::string("technical read of PCM audio: ") + e.what());
    }

    juce::File mp3 = dir.getChildFile("tom_1khz.mp3");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=1000:duration=1", "-c:a", "libmp3lame", "-b:a", "128k",
                    mp3.getFullPathName()});
    try {
        auto r = matriz::ingest::lerTecnica(mp3);
        check(r.codec == "mp3", "codec mp3 read correctly: " + r.codec);
        check(r.codecLossyDeclarado, "mp3 is flagged as declared-lossy");
    } catch (const std::exception& e) {
        check(false, std::string("technical read of mp3 audio: ") + e.what());
    }
}

void testarLeituraTecnicaImagem(const juce::File& dir) {
    std::cout << "== Technical read - image (ffprobe) ==\n";

    juce::File jpg = dir.getChildFile("teste.jpg");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "color=c=blue:s=64x48", "-frames:v", "1", jpg.getFullPathName()});

    try {
        auto r = matriz::ingest::lerTecnica(jpg);
        check(r.larguraPx.has_value() && *r.larguraPx == 64, "width 64px read correctly");
        check(r.alturaPx.has_value() && *r.alturaPx == 48, "height 48px read correctly");
        check(!matriz::ingest::paraJson(r).empty(), "caracteristicas_tecnicas_json is not left empty");
    } catch (const std::exception& e) {
        check(false, std::string("technical read of image: ") + e.what());
    }
}

bool comandoDisponivel(const std::string& nome) {
    juce::ChildProcess proc;
    if (!proc.start(juce::StringArray{"/usr/bin/which", nome}, juce::ChildProcess::wantStdOut)) return false;
    juce::String saida = proc.readAllProcessOutput();
    proc.waitForProcessToFinish(5000);
    return saida.trim().isNotEmpty();
}

void testarLeituraTecnicaPdf(const juce::File& dir) {
    std::cout << "== Technical read - PDF ==\n";

    // §A.3: PDFium e MuPDF não têm caminho FetchContent+CMake viável (ver a
    // nota longa em LeituraTecnica.cpp) — o parser artesanal de contagem de
    // páginas saiu, e não foi substituído por nenhuma biblioteca. Este teste
    // usa um PDF de verdade (gerado pelo filtro texto->PDF do CUPS, não
    // construído à mão) só pra confirmar que a ausência de contagem de
    // páginas é honesta — nunca um número adivinhado — mesmo sobre um
    // arquivo real com a estrutura interna que quiser.
    if (!comandoDisponivel("cupsfilter")) {
        check(true, "cupsfilter unavailable here - real PDF test skipped, not a failure");
        return;
    }

    juce::File txt = dir.getChildFile("origem_pdf_real.txt");
    juce::String conteudo;
    for (int pagina = 0; pagina < 2; ++pagina) {
        for (int linha = 0; linha < 70; ++linha) conteudo += "Linha de teste do MATRIZ.\n";
        if (pagina == 0) conteudo += "\f"; // form feed força quebra de página no filtro texto->PostScript->PDF do CUPS
    }
    txt.replaceWithText(conteudo);

    juce::File pdf = dir.getChildFile("real_gerado_por_cups.pdf");
    juce::ChildProcess proc;
    juce::String comando = "/usr/sbin/cupsfilter \"" + txt.getFullPathName() + "\" > \"" + pdf.getFullPathName() + "\"";
    system(comando.toRawUTF8());

    if (!pdf.existsAsFile() || pdf.getSize() < 100) {
        check(true, "cupsfilter produced no PDF here - test skipped, not a failure");
        return;
    }

    try {
        auto r = matriz::ingest::lerTecnica(pdf);
        std::string json = matriz::ingest::paraJson(r);
        check(json.find("fileSizeBytes") != std::string::npos, "fileSizeBytes present for the real PDF: " + json);
        check(json.find("pageCountEstimado") == std::string::npos,
              "pageCountEstimado absent (honest - no hand-rolled parser guessing a number): " + json);
    } catch (const std::exception& e) {
        check(false, std::string("technical read of a real PDF: ") + e.what());
    }
}

void testarMiniaturas(const juce::File& dir) {
    std::cout << "== Thumbnails, keyframes and waveform ==\n";

    juce::File imgGrande = dir.getChildFile("grande.jpg");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "color=c=green:s=800x600", "-frames:v", "1", imgGrande.getFullPathName()});

    try {
        juce::File miniatura = dir.getChildFile("mini.jpg");
        auto dim = matriz::ingest::gerarMiniaturaImagem(imgGrande, miniatura, 200);
        check(miniatura.existsAsFile(), "image thumbnail written to disk");
        check(dim.largura <= 200 && dim.altura <= 200 && dim.largura > 0,
              "the thumbnail respects the maximum side (" + std::to_string(dim.largura) + "x" + std::to_string(dim.altura) + ")");
    } catch (const std::exception& e) {
        check(false, std::string("image thumbnail: ") + e.what());
    }

    juce::File video = dir.getChildFile("video_teste.mp4");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "testsrc=size=320x240:rate=10:duration=3", "-pix_fmt", "yuv420p",
                    video.getFullPathName()});

    try {
        juce::File dirKeyframes = dir.getChildFile("keyframes");
        auto kfs = matriz::ingest::gerarKeyframesVideo(video, 3.0, 4, dirKeyframes, "kf", 160);
        check(kfs.size() == 4, "keyframe count generated == 4");
        bool todosExistem = true;
        bool temposCrescentes = true;
        for (size_t i = 0; i < kfs.size(); ++i) {
            if (!kfs[i].arquivo.existsAsFile()) todosExistem = false;
            if (i > 0 && kfs[i].tempoSegundos <= kfs[i - 1].tempoSegundos) temposCrescentes = false;
        }
        check(todosExistem, "every keyframe file exists on disk");
        check(temposCrescentes, "keyframe timestamps increase along the video");
        check(kfs.front().dimensao.largura == 160, "the keyframe respects the requested width (160px)");
    } catch (const std::exception& e) {
        check(false, std::string("video keyframes: ") + e.what());
    }

    juce::File tomAudio = dir.getChildFile("tom_forma_onda.wav");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=440:duration=5", "-ar", "44100", tomAudio.getFullPathName()});

    try {
        auto onda = matriz::ingest::calcularFormaDeOnda(tomAudio, dir, 20.0);
        check(onda.duracaoSegundos > 4.5 && onda.duracaoSegundos < 5.5,
              "waveform duration ~5s: " + std::to_string(onda.duracaoSegundos));
        check(!onda.minimos.empty() && onda.minimos.size() == onda.maximos.size(),
              "peaks generated (" + std::to_string(onda.minimos.size()) + " buckets)");
        bool dentroDaFaixa = true;
        for (size_t i = 0; i < onda.minimos.size(); ++i)
            if (onda.minimos[i] < -1.5f || onda.maximos[i] > 1.5f) dentroDaFaixa = false;
        check(dentroDaFaixa, "peaks are within the expected amplitude range (~[-1,1])");
        // O filtro "sine" deste build de ffmpeg não expõe parâmetro de amplitude e gera
        // um tom de nível baixo (~0.125) por padrão — o limiar aqui é só "não é silêncio".
        check(onda.maximos.front() > 0.05f, "the 440Hz tone is audible in the peaks (not silence)");

        auto blob = onda.paraBlob();
        check(blob.size() == onda.minimos.size() * 2 * sizeof(float), "the serialised blob has the expected size");
    } catch (const std::exception& e) {
        check(false, std::string("waveform: ") + e.what());
    }
}

void testarDuplicataPHash(const juce::File& dir) {
    std::cout << "== Image pHash ==\n";

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

        check(distAA2 <= 10, "same image recompressed at a different quality: low distance (" +
                                  std::to_string(distAA2) + ")");
        check(distAB > distAA2, "completely different images have a larger distance (" +
                                     std::to_string(distAB) + " > " + std::to_string(distAA2) + ")");
        check(matriz::ingest::provavelmenteDuplicadaPHash(hashA, hashA2), "provavelmenteDuplicadaPHash confirms the similar pair");
        check(!matriz::ingest::provavelmenteDuplicadaPHash(hashA, hashB), "provavelmenteDuplicadaPHash rejects the different pair");
    } catch (const std::exception& e) {
        check(false, std::string("pHash: ") + e.what());
    }
}

void testarDuplicataFingerprintAudio(const juce::File& dir) {
    std::cout << "== Audio fingerprint ==\n";

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

        check(simAA2 > 0.7, "same tone, round-tripped through mp3: high similarity (" + std::to_string(simAA2) + ")");
        check(simAB < simAA2, "tones of very different frequency: lower similarity (" + std::to_string(simAB) +
                                   " < " + std::to_string(simAA2) + ")");
    } catch (const std::exception& e) {
        check(false, std::string("audio fingerprint: ") + e.what());
    }
}

void testarCorteDeBanda(const juce::File& dir) {
    std::cout << "== Bandwidth cutoff detection (lossy) ==\n";

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

        check(!analiseFullband.corteDetectado, "full-band white noise is not flagged as cut off");
        check(analiseCortado.corteDetectado, "audio low-passed at 12kHz is detected as cut off");
        if (analiseCortado.corteDetectado && analiseCortado.freqCorteEstimadaHz) {
            double f = *analiseCortado.freqCorteEstimadaHz;
            check(f > 8000.0 && f < 17000.0, "the estimated cutoff frequency is plausible (~12kHz): " + std::to_string(f));
        }
    } catch (const std::exception& e) {
        check(false, std::string("bandwidth cutoff detection: ") + e.what());
    }
}

void testarClassificadorFalaMusica(const juce::File& dir) {
    std::cout << "== Speech vs music classifier (DSP heuristic) ==\n";

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
              "a 4Hz-modulated tone has more syllabic modulation energy than a sustained tone (" +
                  std::to_string(resFala.energiaModulacao4HzRatio) + " > " +
                  std::to_string(resMusica.energiaModulacao4HzRatio) + ")");

        check(resMusica.rotulo.has_value() && *resMusica.rotulo == "musica",
              "a sustained tone is classified as music: " + resMusica.rotulo.value_or("(nenhum)"));
        check(resFala.rotulo.has_value() && *resFala.rotulo == "fala",
              "a 4Hz-modulated tone is classified as speech: " + resFala.rotulo.value_or("(nenhum)"));
        check(resMusica.confianca > 0.0 && resFala.confianca > 0.0, "both results carry a confidence > 0");
    } catch (const std::exception& e) {
        check(false, std::string("speech vs music classifier: ") + e.what());
    }
}

void testarInferenciaEstrutura(const juce::File& dir) {
    std::cout << "== Folder structure inference ==\n";

    {
        auto r = matriz::ingest::inferirDePastaRelease("Chico Buarque - Construcao (1971)");
        check(r.artista.has_value() && *r.artista == "Chico Buarque", "artist extracted from the folder name");
        check(r.album.has_value() && *r.album == "Construcao", "album extracted from the folder name");
        check(r.ano.has_value() && *r.ano == 1971, "year extracted from the folder name");
    }
    {
        auto r = matriz::ingest::inferirDePastaRelease("pasta qualquer sem padrao");
        check(!r.artista && !r.album && !r.ano, "a folder with no pattern produces no invented inference");
    }
    {
        auto r = matriz::ingest::inferirDeNomeArquivoFaixa("01 - Faixa.wav");
        check(r.numero.has_value() && *r.numero == 1, "track number extracted (\"01 - Faixa.wav\")");
        check(r.titulo.has_value() && *r.titulo == "Faixa", "track title extracted (\"01 - Faixa.wav\")");
    }
    {
        auto r = matriz::ingest::inferirDeNomeArquivoFaixa("12.Outra Faixa.mp3");
        check(r.numero.has_value() && *r.numero == 12, "track number extracted (dot separator)");
        check(r.titulo.has_value() && *r.titulo == "Outra Faixa", "track title extracted (dot separator)");
    }
    {
        auto r = matriz::ingest::inferirDeNomeArquivoFaixa("semnumero.wav");
        check(!r.numero && !r.titulo, "a name with no numbering produces no invented inference");
    }

    juce::File mp3Tag = dir.getChildFile("com_tags.mp3");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=300:duration=1", "-c:a", "libmp3lame",
                    "-metadata", "title=Minha Faixa", "-metadata", "artist=Minha Artista",
                    "-metadata", "composer=Meu Compositor", mp3Tag.getFullPathName()});
    try {
        auto leitura = matriz::ingest::lerTecnica(mp3Tag);
        auto tags = matriz::ingest::extrairTagsComuns(leitura);
        check(tags.titulo.has_value() && *tags.titulo == "Minha Faixa", "title extracted from ID3 tags: " + tags.titulo.value_or("(nenhum)"));
        check(tags.artista.has_value() && *tags.artista == "Minha Artista", "artist extracted from ID3 tags");
        check(tags.compositor.has_value() && *tags.compositor == "Meu Compositor", "composer extracted from ID3 tags");
    } catch (const std::exception& e) {
        check(false, std::string("embedded tag extraction: ") + e.what());
    }

    check(matriz::ingest::provavelCapaFrente({3000, 3000}), "a 3000x3000 image is a likely front cover");
    check(!matriz::ingest::provavelCapaFrente({3000, 2000}), "a 3000x2000 image (not square) is not a front cover");
    check(!matriz::ingest::provavelCapaFrente({500, 500}), "a 500x500 image (too small) is not a front cover");

    check(matriz::ingest::provavelmenteMesmaGravacao(120.3, 120.8, 0.9), "close durations + similar fingerprint => same recording");
    check(!matriz::ingest::provavelmenteMesmaGravacao(120.3, 120.8, 0.5), "dissimilar fingerprint => not the same recording even with equal duration");
    check(!matriz::ingest::provavelmenteMesmaGravacao(120.0, 200.0, 0.95), "very different duration => not the same recording even with a similar fingerprint");

    {
        using matriz::ingest::ItemParaAgrupar;
        std::vector<ItemParaAgrupar> itens = {
            {"foto1", 1000.0}, {"foto2", 1005.0}, {"foto3", 1300.0}, // grupo 1: gaps pequenos
            {"foto4", 9000.0}, {"foto5", 9010.0},                    // grupo 2: salto grande antes
        };
        auto grupos = matriz::ingest::agruparPorSaltoDeTimestamp(itens, 1800.0);
        check(grupos.size() == 2, "grouping by timestamp yields 2 groups: " + std::to_string(grupos.size()));
        if (grupos.size() == 2) {
            check(grupos[0].idsOrdenados.size() == 3, "the first group has 3 photos");
            check(grupos[1].idsOrdenados.size() == 2, "the second group has 2 photos");
            check(grupos[0].idsOrdenados[0] == "foto1" && grupos[0].idsOrdenados[2] == "foto3",
                  "the first group keeps chronological order");
        }
    }
}

void testarPipelineCompletoDeIngest(const juce::File& dirTemp) {
    std::cout << "== Full pipeline: ingest -> batch record -> inconsistency panel ==\n";

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
        check(temTipo("faixa_sem_isrc"), "the panel detects a track with no ISRC");
        check(temTipo("release_sem_capa"), "the panel detects a release with no cover (item1 and item2 have none)");
        check(temTipo("master_lossy"), "the panel detects a master in a lossy codec");
        check(temTipo("sample_rate_divergente"), "the panel detects mismatched sample rates among masters of the same item");
        check(temTipo("splits_nao_somam_100"), "the panel detects splits that do not add up to 100");
        check(temTipo("isrc_duplicado"), "the panel detects an ISRC duplicated across items");

        // --- Corrige "release sem capa" pro item1 e confirma que some do painel pra ele ---
        juce::File capaPequena = dirTemp.getChildFile("capa_pequena.jpg");
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                        "-i", "color=c=red:s=500x500", "-frames:v", "1", capaPequena.getFullPathName()});
        matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, item1, capaPequena, "capa_frente", false);

        auto problemas2 = matriz::ingest::detectarInconsistenciasFicha(projeto->registro(), definicoes);
        bool item1AindaSemCapa = std::any_of(problemas2.begin(), problemas2.end(), [&](auto& i) {
            return i.tipo == "release_sem_capa" && i.itemId == item1;
        });
        check(!item1AindaSemCapa, "after ingesting the cover, item1 no longer shows up under release_sem_capa");
        bool capaAbaixoDoMinimo = std::any_of(problemas2.begin(), problemas2.end(), [&](auto& i) {
            return i.tipo == "capa_abaixo_minimo" && i.itemId == item1;
        });
        check(capaAbaixoDoMinimo, "a 500x500 cover is flagged as below the minimum required by release.yaml (3000x3000)");

        // --- Fluxo de ficha em lote ---
        matriz::ingest::aplicarFichaEmLote(projeto->registro(), {item1, item2}, "raiz", 0,
                                            {{"genero", "MPB"}}, "operador-teste");
        for (auto& id : {item1, item2}) {
            auto stmt = projeto->registro().prepare(
                "SELECT valor, fonte FROM item_campo WHERE item_id = ? AND campo_id = 'genero'");
            stmt.bind(1, matriz::db::Value::of(id));
            bool achou = stmt.step();
            check(achou && stmt.columnText(0) == "MPB" && stmt.columnText(1) == "humano",
                  "the batch flow wrote 'genero' = MPB for item " + id);
        }
        auto stmtHist = projeto->registro().prepare(
            "SELECT COUNT(*) FROM item_historico WHERE campo_id = 'genero' AND tipo_evento = 'edicao_campo'");
        stmtHist.step();
        check(stmtHist.columnInt(0) == 2, "the batch flow wrote 2 history events (one per item)");

        // --- Verificação de disco: tudo limpo logo após o ingest ---
        auto verificacao1 = matriz::ingest::verificarArquivosNoDisco(*projeto);
        check(verificacao1.empty(), "no disk divergence right after ingest (" +
                                         std::to_string(verificacao1.size()) + " found)");

        // --- Corrompe um arquivo no disco e confirma que o checksum divergente aparece ---
        ing1.arquivoNoProjeto.appendText("extra bytes that should not be here");
        auto verificacao2 = matriz::ingest::verificarArquivosNoDisco(*projeto);
        bool checksumDivergenteDetectado = std::any_of(verificacao2.begin(), verificacao2.end(), [&](auto& i) {
            return i.tipo == "checksum_divergente" && i.arquivoId == ing1.arquivoId;
        });
        check(checksumDivergenteDetectado, "a file corrupted on disk is detected as a checksum mismatch");

        // --- Arquivo não catalogado ao lado do master NÃO é inconsistência ---
        // Com a ingestão in-place (I5) o projeto não tem pasta `arquivos/`
        // própria: os masters ficam no volume de origem, onde conviver com
        // arquivos não catalogados é o estado normal. A varredura de órfãos
        // do modelo antigo (tudo copiado pra dentro do projeto) marcaria o
        // volume inteiro como problema.
        juce::File naoCatalogado = ing1.arquivoNoProjeto.getSiblingFile("nao_catalogado.txt");
        naoCatalogado.replaceWithText("este arquivo nunca passou por ingerirArquivo");
        auto verificacao3 = matriz::ingest::verificarArquivosNoDisco(*projeto);
        bool orfaoReportado = std::any_of(verificacao3.begin(), verificacao3.end(),
                                           [](auto& i) { return i.tipo == "arquivo_orfao"; });
        check(!orfaoReportado, "an uncatalogued file on the volume is not an inconsistency (in-place ingest, I5)");
        naoCatalogado.deleteFile();

        // --- Vault offline não é corrupção (I3) ---
        projeto->registro().run("UPDATE vault SET status = 'offline'", {});
        auto verificacao4 = matriz::ingest::verificarArquivosNoDisco(*projeto);
        check(verificacao4.empty(), "with the Vault offline nothing is reported as divergent (" +
                                         std::to_string(verificacao4.size()) + " reported)");
        projeto->registro().run("UPDATE vault SET status = 'online'", {});

    } catch (const std::exception& e) {
        check(false, std::string("full ingest pipeline: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
}

void testarExifReal(const juce::File& dir) {
    std::cout << "== Real EXIF via Exiv2 ==\n";

    juce::File jpg = dir.getChildFile("com_exif.jpg");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "color=c=gray:s=320x240", "-frames:v", "1", jpg.getFullPathName()});

    // Escreve EXIF de verdade com o próprio Exiv2 (mais confiável que montar
    // o binário JPEG+EXIF na mão) — prova que a leitura em LeituraTecnica.cpp
    // funciona sobre um arquivo com metadado real, não só a ausência dele.
    try {
        auto image = Exiv2::ImageFactory::open(jpg.getFullPathName().toStdString());
        image->readMetadata();
        Exiv2::ExifData exifData;
        exifData["Exif.Image.Make"] = "MatrizTeste";
        exifData["Exif.Image.Model"] = "Camera XPTO";
        exifData["Exif.Photo.LensModel"] = "Lente 50mm";
        exifData["Exif.Photo.DateTimeOriginal"] = "2024:05:01 12:00:00";
        exifData["Exif.Image.Orientation"] = uint16_t(6);
        image->setExifData(exifData);
        image->writeMetadata();
    } catch (const std::exception& e) {
        check(false, std::string("writing test EXIF via Exiv2: ") + e.what());
        return;
    }

    try {
        auto r = matriz::ingest::lerTecnica(jpg);
        check(r.exifCamera.has_value() && r.exifCamera->find("MatrizTeste") != std::string::npos &&
                  r.exifCamera->find("Camera XPTO") != std::string::npos,
              "camera read from real EXIF: " + r.exifCamera.value_or("(nenhuma)"));
        check(r.exifLente.has_value() && *r.exifLente == "Lente 50mm", "lens read from real EXIF: " + r.exifLente.value_or("(nenhuma)"));
        check(r.exifDataOriginal.has_value() && r.exifDataOriginal->find("2024") != std::string::npos,
              "original date read from real EXIF: " + r.exifDataOriginal.value_or("(nenhuma)"));
        check(r.exifOrientacao.has_value() && *r.exifOrientacao == 6,
              "orientation read from real EXIF: " + std::to_string(r.exifOrientacao.value_or(-1)));
        check(matriz::ingest::paraJson(r).find("MatrizTeste") != std::string::npos,
              "EXIF makes it into the serialised JSON (caracteristicas_tecnicas_json)");
    } catch (const std::exception& e) {
        check(false, std::string("reading real EXIF: ") + e.what());
    }

    // Imagem sem EXIF nenhum (todas as outras geradas por ffmpeg nesta suite)
    // não deve lançar — ausência de EXIF é normal, não erro.
    juce::File semExif = dir.getChildFile("sem_exif.jpg");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "color=c=blue:s=100x100", "-frames:v", "1", semExif.getFullPathName()});
    try {
        auto r = matriz::ingest::lerTecnica(semExif);
        check(!r.exifCamera.has_value() && !r.exifDataOriginal.has_value(),
              "an image with no EXIF does not throw and leaves the EXIF fields empty");
    } catch (const std::exception& e) {
        check(false, std::string("an image with no EXIF should not throw: ") + e.what());
    }
}

void testarCategoriaPorExtensao() {
    std::cout << "== Category by extension ==\n";
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.wav")) == matriz::ingest::CategoriaMidia::Audio, "wav -> Audio");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.mp4")) == matriz::ingest::CategoriaMidia::Video, "mp4 -> Video");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.jpg")) == matriz::ingest::CategoriaMidia::Imagem, "jpg -> Imagem");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.pdf")) == matriz::ingest::CategoriaMidia::Documento, "pdf -> Documento");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.xyz")) == matriz::ingest::CategoriaMidia::Desconhecida, "xyz -> Desconhecida");
}

void testarMascaraDeNomenclatura() {
    std::cout << "== Naming mask ==\n";

    matriz::consolidacao::ContextoMascara ctx;
    ctx.codigoAcervo = "ACR2026-007";
    ctx.titulo = "ensaio-berlim";
    ctx.tipoMidia = "fita_rolo";
    ctx.nomeOriginalSemExtensao = "IMG_0001";
    ctx.nomeAcervo = "Acervo do Anderson";
    ctx.nomePasta = "Turne Europa";
    ctx.seq = 7;
    ctx.camposFicha["artista_principal"] = "Banda Teste";

    check(matriz::consolidacao::resolverMascara("{codigo}-{seq:03}-{titulo}", ctx) == "ACR2026-007-007-ensaio-berlim",
          "the spec's own example resolves exactly as documented");
    check(matriz::consolidacao::resolverMascara("{original}", ctx) == "IMG_0001",
          "{original} resolves to the original name without extension");
    check(matriz::consolidacao::resolverMascara("{acervo}-{pasta}-{codigo}", ctx) ==
              "Acervo do Anderson-Turne Europa-ACR2026-007",
          "{acervo} and {pasta} resolve");
    check(matriz::consolidacao::resolverMascara("{artista_principal} - {titulo}", ctx) == "Banda Teste - ensaio-berlim",
          "an arbitrary record field (not a built-in token) resolves by its own id");

    std::vector<std::string> naoResolvidos;
    std::string resultado = matriz::consolidacao::resolverMascara("{codigo}-{campo_inexistente}", ctx, &naoResolvidos);
    check(resultado == "ACR2026-007-", "an unknown token becomes \"\", never throws and never hangs the preview");
    check(naoResolvidos.size() == 1 && naoResolvidos[0] == "campo_inexistente",
          "an unknown token is reported (to become a warning in the preview)");

    check(matriz::consolidacao::resolverMascara("A/B:C", ctx) == "A_B_C",
          "characters invalid in a file name are sanitised (/ and : become _)");

    check(matriz::consolidacao::resolverMascara("sem token nenhum", ctx) == "sem token nenhum",
          "text with no token at all passes through");
}

// Item 5 — estrutura de pastas do backup: padrão Projeto→Ano→Tipo de
// Mídia→Tipo de Arquivo, reordenável, e "sem ano" pra material sem o campo.
void testarHierarquiaBackup(const juce::File& dirTemp) {
    std::cout << "== Backup folder structure ==\n";
    using namespace matriz::consolidacao;

    check(hierarquiaParaCsv(hierarquiaPadrao()) == "projeto,ano,tipo_midia,tipo_arquivo",
          "the default is Project -> Year -> Media type -> File type");
    check(hierarquiaDeCsv(hierarquiaParaCsv(hierarquiaPadrao())) == hierarquiaPadrao(),
          "the hierarchy CSV survives a round trip");
    check(hierarquiaDeCsv("lixo_que_nao_existe") == hierarquiaPadrao(),
          "a corrupted value falls back to the default instead of throwing (never blocks opening the project)");
    check(hierarquiaDeCsv("") == hierarquiaPadrao(), "an empty CSV falls back to the default");

    juce::File pastaProjeto = dirTemp.getChildFile("projeto_hierarquia_" + juce::Uuid().toDashedString());
    juce::File destino = dirTemp.getChildFile("destino_hierarquia_" + juce::Uuid().toDashedString());
    destino.createDirectory();

    matriz::model::NovoProjetoParams params;
    params.nome = "Acervo do Anderson";
    params.modo = matriz::model::Modo::Preservacao;
    params.prefixoNomenclatura = "HIE";

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        std::string agora = matriz::model::agoraIso8601();
        std::string projetoId = projeto->projetoId();

        // Um item COM ano (1978) e um SEM ano — o §5.2 exige que o sem ano
        // vá pra uma pasta "sem ano" em vez de desaparecer ou travar.
        auto criarItem = [&](const char* codigo, const char* titulo, const char* tipo,
                              std::optional<std::string> ano) -> std::string {
            std::string id = matriz::model::novoUuid();
            projeto->registro().run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)",
                {matriz::db::Value::of(id), matriz::db::Value::of(projetoId), matriz::db::Value::of(std::string(codigo)),
                 matriz::db::Value::of(std::string(titulo)), matriz::db::Value::of(std::string(tipo)),
                 matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
            if (ano)
                projeto->registro().run(
                    "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                    "VALUES (?, ?, 'raiz', 0, 'ano', ?, 'humano', ?)",
                    {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id),
                     matriz::db::Value::of(*ano), matriz::db::Value::of(agora)});
            return id;
        };

        juce::File masterOrigem = dirTemp.getChildFile("hierarquia_master.wav");
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                         "sine=frequency=330:duration=1", masterOrigem.getFullPathName()});

        std::string pasta = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, criado_em, atualizado_em) "
            "VALUES (?, ?, NULL, 'Tudo', 0, ?, ?)",
            {matriz::db::Value::of(pasta), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});

        std::string comAno = criarItem("HIE-001", "Com Ano", "fita_rolo", std::string("1978"));
        std::string semAno = criarItem("HIE-002", "Sem Ano", "fita_rolo", std::nullopt);
        for (auto& id : {comAno, semAno}) {
            matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, id, masterOrigem, "preservation_master", true);
            projeto->registro().run(
                "INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id),
                 matriz::db::Value::of(pasta), matriz::db::Value::of(agora)});
        }

        auto caminhoDe = [](const PlanoConsolidacao& p, const std::string& codigo) -> juce::String {
            for (auto& i : p.itens)
                if (i.codigoAcervo == codigo) return i.caminhoRelativoDestino;
            return {};
        };

        auto planoPadrao = planejarConsolidacao(projeto->registro(), pastaProjeto, destino, hierarquiaPadrao());
        check(caminhoDe(planoPadrao, "HIE-001") == "Acervo do Anderson/1978/fita_rolo/WAV/HIE-001-001-Com Ano.wav",
              "the default structure yields Project/Year/Media type/File type (got: \"" +
                  caminhoDe(planoPadrao, "HIE-001").toStdString() + "\")");
        check(caminhoDe(planoPadrao, "HIE-002").contains("/No year/"),
              "material with no year goes to \"No year\", never disappears (got: \"" +
                  caminhoDe(planoPadrao, "HIE-002").toStdString() + "\")");

        // Reordenar os níveis muda a árvore resultante NA HORA, sem copiar
        // nada — é o que a prévia do diálogo mostra (§5.2, item 12).
        auto planoInvertido = planejarConsolidacao(
            projeto->registro(), pastaProjeto, destino,
            {NivelHierarquia::TipoArquivo, NivelHierarquia::Ano, NivelHierarquia::Projeto});
        check(caminhoDe(planoInvertido, "HIE-001") == "WAV/1978/Acervo do Anderson/HIE-001-001-Com Ano.wav",
              "reordering the levels reorders the folders (got: \"" +
                  caminhoDe(planoInvertido, "HIE-001").toStdString() + "\")");

        auto planoOrigem = planejarConsolidacao(projeto->registro(), pastaProjeto, destino, {NivelHierarquia::Origem});
        check(caminhoDe(planoOrigem, "HIE-001").startsWith("No origin/"),
              "an origin level with no value becomes \"No origin\" (got: \"" +
                  caminhoDe(planoOrigem, "HIE-001").toStdString() + "\")");

        // A hierarquia é do projeto, não da sessão do diálogo.
        gravarHierarquiaDoProjeto(projeto->registro(), {NivelHierarquia::Ano, NivelHierarquia::TipoMidia});
        check(hierarquiaDoProjeto(projeto->registro()) ==
                  HierarquiaBackup{NivelHierarquia::Ano, NivelHierarquia::TipoMidia},
              "the chosen hierarchy persists in the project");
        auto planoGravado = planejarConsolidacao(projeto->registro(), pastaProjeto, destino);
        check(caminhoDe(planoGravado, "HIE-001") == "1978/fita_rolo/HIE-001-001-Com Ano.wav",
              "planning without an explicit hierarchy uses the one stored in the project (got: \"" +
                  caminhoDe(planoGravado, "HIE-001").toStdString() + "\")");

        // Executar de verdade: as pastas automáticas têm que existir em disco.
        auto resultado = executarConsolidacao(projeto->registro(), pastaProjeto, destino, planoGravado);
        check(resultado.consolidados == 2 && resultado.falhas.empty(), "backup with automatic hierarchy writes both items");
        check(destino.getChildFile("1978/fita_rolo/HIE-001-001-Com Ano.wav").existsAsFile(),
              "the automatic folder was created on disk and the file is inside it");
    } catch (const std::exception& e) {
        check(false, std::string("backup hierarchy: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
    destino.deleteRecursively();
}

// Itens 6.1 e 8.3 — loudness EBU R128 pré-calculado, e marcador que vira
// metadado embutido NA CÓPIA sem nunca tocar no original.
void testarLoudnessEMarcadores(const juce::File& dirTemp) {
    std::cout << "== Loudness and markers embedded in the copy ==\n";

    // --- Loudness contra referência da própria norma ---
    // Referência de BS.1770-4: um seno de 1 kHz a -20 dBFS RMS em UM canal
    // mede -20 LUFS (o offset de -0,691 e o ganho do filtro K a 1 kHz se
    // cancelam quase exatamente nessa frequência — é por isso que 1 kHz é a
    // frequência de calibração da norma). O MESMO sinal duplicado em estéreo
    // mede 3 dB MAIS ALTO, porque a norma SOMA a potência ponderada dos
    // canais (L_K = -0,691 + 10log10(Σ G_i·z_i)) em vez de fazer média — é o
    // comportamento que distingue uma implementação correta de uma que só
    // devolve "um número plausível".
    {
        const double sr = 48000.0;
        const int amostras = static_cast<int>(sr * 10.0);
        const float amplitude = std::pow(10.0f, -20.0f / 20.0f) * std::sqrt(2.0f); // pico -> -20 dBFS RMS
        auto gerarSeno = [&](int canais) {
            juce::AudioBuffer<float> b(canais, amostras);
            for (int i = 0; i < amostras; ++i) {
                float v = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f *
                                                static_cast<float>(i) / static_cast<float>(sr));
                for (int c = 0; c < canais; ++c) b.setSample(c, i, v);
            }
            return b;
        };

        auto mono = gerarSeno(1);
        auto medidaMono = matriz::ingest::medirLoudness(mono, sr);
        check(std::abs(medidaMono.lufsIntegrado - (-20.0)) < 0.2,
              "a 1 kHz sine at -20 dBFS RMS mono measures -20 LUFS (got " + std::to_string(medidaMono.lufsIntegrado) + ")");

        auto estereo = gerarSeno(2);
        auto medidaEstereo = matriz::ingest::medirLoudness(estereo, sr);
        check(std::abs((medidaEstereo.lufsIntegrado - medidaMono.lufsIntegrado) - 3.01) < 0.2,
              "the same signal in stereo measures +3 dB (BS.1770 power sum, not an average) (got +" +
                  std::to_string(medidaEstereo.lufsIntegrado - medidaMono.lufsIntegrado) + " dB)");
        check(std::abs(medidaMono.truePeakDbfs - (-17.0)) < 0.2,
              "the sine sample peak matches the generated amplitude (got " +
                  std::to_string(medidaMono.truePeakDbfs) + " dBFS)");
        check(medidaMono.lra < 1.0, "a constant signal has an LRA close to zero (got " + std::to_string(medidaMono.lra) + ")");

        juce::AudioBuffer<float> silencio(2, amostras);
        silencio.clear();
        auto medidaSilencio = matriz::ingest::medirLoudness(silencio, sr);
        check(medidaSilencio.lufsIntegrado <= -70.0, "absolute silence sits at the -70 LUFS floor, not at -inf nor 0");

        // Gating relativo: um trecho alto seguido de um trecho muito baixo —
        // o baixo tem que ser DESCARTADO pelo gate, então o integrado fica
        // perto do trecho alto, não na média dos dois.
        juce::AudioBuffer<float> comGate(1, amostras);
        comGate.clear();
        for (int i = 0; i < amostras; ++i) {
            float v = std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * static_cast<float>(i) /
                                static_cast<float>(sr));
            comGate.setSample(0, i, i < amostras / 2 ? amplitude * v : amplitude * v * 0.001f); // 2ª metade -60 dB
        }
        auto medidaGate = matriz::ingest::medirLoudness(comGate, sr);
        check(std::abs(medidaGate.lufsIntegrado - medidaMono.lufsIntegrado) < 0.5,
              "relative gating drops the 60 dB-below passage instead of dragging the average (got " +
                  std::to_string(medidaGate.lufsIntegrado) + ", expected near " +
                  std::to_string(medidaMono.lufsIntegrado) + ")");
    }

    // --- Marcador embutido na cópia, original intacto ---
    juce::File pastaProjeto = dirTemp.getChildFile("projeto_marcador_" + juce::Uuid().toDashedString());
    juce::File destino = dirTemp.getChildFile("destino_marcador_" + juce::Uuid().toDashedString());
    destino.createDirectory();

    matriz::model::NovoProjetoParams params;
    params.nome = "Marcadores";
    params.modo = matriz::model::Modo::Preservacao;
    params.prefixoNomenclatura = "MRC";

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        std::string agora = matriz::model::agoraIso8601();
        std::string projetoId = projeto->projetoId();

        std::string itemId = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
            "VALUES (?, ?, 'MRC-001', 'Com marcadores', 'fita_rolo', ?, ?)",
            {matriz::db::Value::of(itemId), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});

        juce::File masterOrigem = dirTemp.getChildFile("marcador_master.wav");
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                         "sine=frequency=440:duration=5", masterOrigem.getFullPathName()});

        // Hash do ORIGINAL antes de tudo — é o que prova o item 8.3.
        juce::MD5 hashAntes(masterOrigem);
        juce::int64 tamanhoAntes = masterOrigem.getSize();

        auto ing = matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, itemId, masterOrigem,
                                                   "preservation_master", true);
        // A leitura técnica do ingest já mediu o loudness (item 6.1).
        check(ing.leitura.lufsIntegrado.has_value(),
              "audio ingest already carries a pre-computed LUFS-I from the technical read");
        check(ing.leitura.lra.has_value(), "audio ingest already carries a pre-computed LRA");

        std::string pasta = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, criado_em, atualizado_em) "
            "VALUES (?, ?, NULL, 'Tudo', 0, ?, ?)",
            {matriz::db::Value::of(pasta), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
        projeto->registro().run(
            "INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(pasta), matriz::db::Value::of(agora)});

        // Dois marcadores, como a timeline gravaria (item 8.2/9.2 — a mesma
        // tabela item_observacao que a ficha lê).
        auto inserirMarcador = [&](int64_t ms, const char* texto) {
            projeto->registro().run(
                "INSERT INTO item_observacao (id, item_id, texto, autor, criado_em, minutagem_ms) "
                "VALUES (?, ?, ?, 'selftest', ?, ?)",
                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                 matriz::db::Value::of(std::string(texto)), matriz::db::Value::of(agora),
                 matriz::db::Value::of(static_cast<long long>(ms))});
        };
        inserirMarcador(1500, "entrada da voz");
        inserirMarcador(3200, "emenda");

        auto marcadores = matriz::consolidacao::marcadoresDoItem(projeto->registro(), itemId);
        check(marcadores.size() == 2, "both markers are read from item_observacao (the same list the record shows)");

        auto plano = matriz::consolidacao::planejarConsolidacao(projeto->registro(), pastaProjeto, destino,
                                                                 soPastaManual());
        auto resultado = matriz::consolidacao::executarConsolidacao(projeto->registro(), pastaProjeto, destino, plano);
        check(resultado.consolidados == 1, "backup copy");
        check(resultado.arquivosComMarcadorEmbutido == 1,
              "markers were embedded in 1 backup file (got " +
                  std::to_string(resultado.arquivosComMarcadorEmbutido) + ")");

        // O ORIGINAL permanece byte a byte idêntico (item 8.3).
        check(juce::MD5(masterOrigem).toHexString() == hashAntes.toHexString(),
              "the ORIGINAL file stays byte-for-byte identical after inserting markers");
        check(masterOrigem.getSize() == tamanhoAntes, "the original did not change size either");

        // A CÓPIA cresceu e ganhou os chunks de marcador.
        juce::File copia;
        if (!plano.itens.empty()) copia = destino.getChildFile(plano.itens[0].caminhoRelativoDestino);
        check(copia.existsAsFile(), "the copy exists at the destination");
        if (copia.existsAsFile()) {
            check(copia.getSize() > tamanhoAntes, "the copy is LARGER than the original - it gained the marker chunks");
            juce::MemoryBlock conteudo;
            copia.loadFileAsData(conteudo);
            // Busca nos BYTES, não via juce::String: WAV é binário e cheio de
            // \0, e String truncaria no primeiro deles (foi o que fez este
            // teste falhar na primeira tentativa, escondendo que os chunks
            // estavam lá).
            auto contemBytes = [&conteudo](const char* agulha) {
                size_t n = std::strlen(agulha);
                const char* dados = static_cast<const char*>(conteudo.getData());
                if (conteudo.getSize() < n) return false;
                for (size_t i = 0; i + n <= conteudo.getSize(); ++i)
                    if (std::memcmp(dados + i, agulha, n) == 0) return true;
                return false;
            };
            check(contemBytes("cue "), "the copy has the \"cue \" chunk (marker readable by a DAW)");
            check(contemBytes("iXML"), "the copy has the iXML chunk (BWF/iXML)");
            check(contemBytes("labl"), "the copy has the \"labl\" chunks (the text of each marker)");
            check(contemBytes("entrada da voz") && contemBytes("emenda"),
                  "the TEXT of each marker is embedded in the copy, not just the position");
            // O áudio da cópia continua tocável: o RIFF novo tem que ser
            // válido, senão o backup seria um arquivo quebrado.
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> leitor(fm.createReaderFor(copia));
            check(leitor != nullptr, "the copy with markers is still a valid, readable WAV");
            if (leitor != nullptr)
                check(leitor->lengthInSamples > 0, "the audio in the copy is still there (" +
                                                        std::to_string(leitor->lengthInSamples) + " samples)");
        }
    } catch (const std::exception& e) {
        check(false, std::string("embedded marker: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
    destino.deleteRecursively();
}

void testarConsolidacao(const juce::File& dirTemp) {
    std::cout << "== Backup: planning and execution ==\n";

    juce::File pastaProjeto = dirTemp.getChildFile("projeto_consolidacao_" + juce::Uuid().toDashedString());
    juce::File destino = dirTemp.getChildFile("destino_consolidacao_" + juce::Uuid().toDashedString());
    destino.createDirectory();

    matriz::model::NovoProjetoParams params;
    params.nome = "Acervo Consolidacao Teste";
    params.modo = matriz::model::Modo::Preservacao;
    params.prefixoNomenclatura = "CNS";

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        std::string agora = matriz::model::agoraIso8601();
        std::string projetoId = projeto->projetoId();

        // --- Item 1: organizado em "01 Fitas/Estudio" (com máscara própria) ---
        std::string item1 = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
            "VALUES (?, ?, 'CNS-001', 'Ensaio Berlim', 'fita_rolo', ?, ?)",
            {matriz::db::Value::of(item1), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});

        juce::File masterOrigem = dirTemp.getChildFile("consolidacao_master.wav");
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                         "sine=frequency=440:duration=1", masterOrigem.getFullPathName()});
        auto ing1 = matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, item1, masterOrigem,
                                                     "preservation_master", true);

        std::string pastaTopo = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, criado_em, atualizado_em) "
            "VALUES (?, ?, NULL, '01 Fitas', 0, ?, ?)",
            {matriz::db::Value::of(pastaTopo), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
        std::string pastaFilha = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mascara_nomenclatura, criado_em, "
            "atualizado_em) VALUES (?, ?, ?, 'Estudio', 0, '{codigo}-{seq:03}-{titulo}', ?, ?)",
            {matriz::db::Value::of(pastaFilha), matriz::db::Value::of(projetoId), matriz::db::Value::of(pastaTopo),
             matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
        projeto->registro().run(
            "INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item1),
             matriz::db::Value::of(pastaFilha), matriz::db::Value::of(agora)});

        // --- Item 2: fica de fora, nunca organizado — só pra provar "não organizados" ---
        std::string item2 = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
            "VALUES (?, ?, 'CNS-002', 'Sem Pasta', 'fita_rolo', ?, ?)",
            {matriz::db::Value::of(item2), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});

        auto plano1 = matriz::consolidacao::planejarConsolidacao(projeto->registro(), pastaProjeto, destino, soPastaManual());
        check(plano1.itens.size() == 1, "the plan has 1 filed item (item2, outside any folder, is left out)");
        check(plano1.itensNaoOrganizados == 1, "\"no folder yet\" counts item2");
        check(plano1.podeConsolidar(), "with no clash, the plan can be executed");
        if (!plano1.itens.empty()) {
            check(plano1.itens[0].caminhoRelativoDestino == "01 Fitas/Estudio/CNS-001-001-Ensaio Berlim.wav",
                  "the final path combines folder hierarchy + the child folder mask (got: \"" +
                      plano1.itens[0].caminhoRelativoDestino.toStdString() + "\")");
            check(!plano1.itens[0].jaConsolidado, "first time - not written to backup yet");
        }
        check(plano1.espacoNecessarioBytes > 0, "required space computed (> 0 bytes)");

        auto resultado1 = matriz::consolidacao::executarConsolidacao(projeto->registro(), pastaProjeto, destino, plano1);
        check(resultado1.consolidados == 1 && resultado1.falhas.empty(), "the run writes 1 item, with no failures");

        juce::File copiaFinal = destino.getChildFile("01 Fitas/Estudio/CNS-001-001-Ensaio Berlim.wav");
        check(copiaFinal.existsAsFile(), "the written copy really exists on disk");
        check(copiaFinal.getSize() == ing1.arquivoNoProjeto.getSize(), "the copy size matches the master");
        check(ing1.arquivoNoProjeto.existsAsFile(),
              "the master stays untouched (the original, outside the project, was never reopened)");

        // --- Incremental: rodar de novo sem mudar nada não deve recopiar ---
        auto plano2 = matriz::consolidacao::planejarConsolidacao(projeto->registro(), pastaProjeto, destino, soPastaManual());
        check(!plano2.itens.empty() && plano2.itens[0].jaConsolidado,
              "second run: an already-written item is recognised (incremental)");
        check(plano2.espacoNecessarioBytes == 0, "nothing new to copy - required space goes to zero");
        auto resultado2 = matriz::consolidacao::executarConsolidacao(projeto->registro(), pastaProjeto, destino, plano2);
        check(resultado2.consolidados == 0 && resultado2.pulados == 1,
              "the second run skips the already-written item instead of copying it again");

        // --- Conflito: um segundo item na MESMA pasta com máscara sem {seq}/{codigo} único ---
        std::string item3 = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
            "VALUES (?, ?, 'CNS-003', 'Outro', 'fita_rolo', ?, ?)",
            {matriz::db::Value::of(item3), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
        juce::File masterOrigem3 = dirTemp.getChildFile("consolidacao_master3.wav");
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                         "sine=frequency=220:duration=1", masterOrigem3.getFullPathName()});
        matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, item3, masterOrigem3, "preservation_master", true);

        std::string pastaConflito = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mascara_nomenclatura, criado_em, "
            "atualizado_em) VALUES (?, ?, NULL, 'Conflito', 0, '{tipo}', ?, ?)", // mesma máscara pros dois -> mesmo nome final
            {matriz::db::Value::of(pastaConflito), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
        projeto->registro().run(
            "INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item1),
             matriz::db::Value::of(pastaConflito), matriz::db::Value::of(agora)});
        projeto->registro().run(
            "INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item3),
             matriz::db::Value::of(pastaConflito), matriz::db::Value::of(agora)});

        auto plano3 = matriz::consolidacao::planejarConsolidacao(projeto->registro(), pastaProjeto, destino, soPastaManual());
        check(!plano3.nomesEmConflito.empty(), "two items of the same type in the \"Conflito\" folder (mask \"{tipo}\") clash");
        check(!plano3.podeConsolidar(), "a plan with a clash cannot run until it is resolved");

        auto resultado3 = matriz::consolidacao::executarConsolidacao(projeto->registro(), pastaProjeto, destino, plano3);
        int consolidadosNaPastaConflito = 0;
        for (auto& ip : plano3.itens)
            if (ip.pastaId == pastaConflito && !ip.emConflito) ++consolidadosNaPastaConflito;
        check(consolidadosNaPastaConflito == 0, "executarConsolidacao never copies an item marked as clashing");
        juce::ignoreUnused(resultado3);

        // --- Item 9: capa vai junto da cópia no backup ---
        //
        // Embutir no arquivo (ID3/FLAC/MP4) não existe — sem escritor de
        // metadado por formato, ver cabeçalho de Consolidacao.h. A metade
        // que dá pra garantir hoje é "copiada junto quando não".
        {
            juce::File pastaProjetoCapa = dirTemp.getChildFile("projeto_capa_backup");
            juce::File destinoCapa = dirTemp.getChildFile("destino_capa_backup");
            destinoCapa.createDirectory();

            matriz::model::NovoProjetoParams paramsCapa;
            paramsCapa.nome = "Capa no backup";
            paramsCapa.modo = matriz::model::Modo::Preservacao;
            paramsCapa.prefixoNomenclatura = "CPB";
            auto projetoCapa = matriz::model::Project::criar(pastaProjetoCapa, paramsCapa);
            std::string projetoCapaId = projetoCapa->projetoId();
            std::string agoraCapa = matriz::model::agoraIso8601();

            std::string pastaCapaId = matriz::model::novoUuid();
            projetoCapa->registro().run(
                "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mascara_nomenclatura, criado_em, "
                "atualizado_em) VALUES (?, ?, NULL, 'Discos', 0, '{codigo}-{titulo}', ?, ?)",
                {matriz::db::Value::of(pastaCapaId), matriz::db::Value::of(projetoCapaId),
                 matriz::db::Value::of(agoraCapa), matriz::db::Value::of(agoraCapa)});

            std::string itemCapa = matriz::model::novoUuid();
            projetoCapa->registro().run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
                "VALUES (?, ?, 'CPB-001', 'Disco', 'fita_rolo', ?, ?)",
                {matriz::db::Value::of(itemCapa), matriz::db::Value::of(projetoCapaId),
                 matriz::db::Value::of(agoraCapa), matriz::db::Value::of(agoraCapa)});

            juce::File masterCapa = dirTemp.getChildFile("capa_backup_master.wav");
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                             "sine=frequency=550:duration=1", masterCapa.getFullPathName()});
            matriz::ingest::ingerirArquivo(projetoCapa->registro(), pastaProjetoCapa, itemCapa, masterCapa,
                                            "preservation_master", true);

            // A capa: um arquivo de papel 'capa_frente' do mesmo item.
            juce::File imagemCapa = dirTemp.getChildFile("arte.jpg");
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                             "color=c=purple:s=64x64", "-frames:v", "1", imagemCapa.getFullPathName()});
            matriz::ingest::ingerirArquivo(projetoCapa->registro(), pastaProjetoCapa, itemCapa, imagemCapa,
                                            "capa_frente", false);

            projetoCapa->registro().run(
                "INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemCapa),
                 matriz::db::Value::of(pastaCapaId), matriz::db::Value::of(agoraCapa)});

            auto planoCapa = matriz::consolidacao::planejarConsolidacao(projetoCapa->registro(), pastaProjetoCapa, destinoCapa, soPastaManual());
            auto resCapa = matriz::consolidacao::executarConsolidacao(projetoCapa->registro(), pastaProjetoCapa,
                                                                       destinoCapa, planoCapa);
            check(resCapa.consolidados == 1, "the backup wrote the item with its cover");

            juce::File masterNoBackup = destinoCapa.getChildFile("Discos/CPB-001-Disco.wav");
            juce::File capaNoBackup = destinoCapa.getChildFile("Discos/CPB-001-Disco.jpg");
            check(masterNoBackup.existsAsFile(), "the master is in the backup");
            check(capaNoBackup.existsAsFile(),
                  "the cover was copied ALONGSIDE the master, with the same base name");
            check(capaNoBackup.getSize() == imagemCapa.getSize(), "the copied cover is identical to the original");
        }

        // --- Item 10: cancelar a gravação do backup ---
        //
        // Projeto PRÓPRIO, não o compartilhado acima: os testes anteriores
        // deixam itens em conflito e já consolidados espalhados, e um plano
        // contaminado por eles não deixa afirmar "processou exatamente 1".
        //
        // O que precisa valer: o que já foi copiado E VERIFICADO continua
        // válido e registrado, e retomar reconhece isso em vez de recopiar.
        // Cancelar não é rollback.
        {
            juce::File pastaProjetoC = dirTemp.getChildFile("projeto_cancelamento");
            juce::File destinoC = dirTemp.getChildFile("destino_cancelamento");
            destinoC.createDirectory();

            matriz::model::NovoProjetoParams paramsC;
            paramsC.nome = "Cancelamento backup";
            paramsC.modo = matriz::model::Modo::Preservacao;
            paramsC.prefixoNomenclatura = "CAN";
            auto projetoC = matriz::model::Project::criar(pastaProjetoC, paramsC);
            std::string projetoCId = projetoC->projetoId();
            std::string agoraC = matriz::model::agoraIso8601();

            std::string pastaAlvo = matriz::model::novoUuid();
            projetoC->registro().run(
                "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mascara_nomenclatura, criado_em, "
                "atualizado_em) VALUES (?, ?, NULL, 'Tudo', 0, '{codigo}-{titulo}', ?, ?)",
                {matriz::db::Value::of(pastaAlvo), matriz::db::Value::of(projetoCId), matriz::db::Value::of(agoraC),
                 matriz::db::Value::of(agoraC)});

            // Três itens, conteúdos diferentes (frequências distintas) pra
            // nenhum virar duplicata do outro.
            for (int i = 0; i < 3; ++i) {
                std::string itemId = matriz::model::novoUuid();
                juce::String codigo = "CAN-" + juce::String(i + 1).paddedLeft('0', 3);
                projetoC->registro().run(
                    "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
                    "VALUES (?, ?, ?, ?, 'fita_rolo', ?, ?)",
                    {matriz::db::Value::of(itemId), matriz::db::Value::of(projetoCId),
                     matriz::db::Value::of(codigo.toStdString()),
                     matriz::db::Value::of("Faixa " + std::to_string(i + 1)), matriz::db::Value::of(agoraC),
                     matriz::db::Value::of(agoraC)});

                juce::File master = dirTemp.getChildFile("cancel_master_" + juce::String(i) + ".wav");
                gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                                 "sine=frequency=" + juce::String(200 + i * 100) + ":duration=1",
                                 master.getFullPathName()});
                matriz::ingest::ingerirArquivo(projetoC->registro(), pastaProjetoC, itemId, master,
                                                "preservation_master", true);

                projetoC->registro().run(
                    "INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
                    {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                     matriz::db::Value::of(pastaAlvo), matriz::db::Value::of(agoraC)});
            }

            auto planoC = matriz::consolidacao::planejarConsolidacao(projetoC->registro(), pastaProjetoC, destinoC, soPastaManual());
            check(planoC.itens.size() == 3, "the isolated plan holds all 3 items, with no clash and nothing already written");

            // Cancela DEPOIS do primeiro arquivo — o caso real (parou no
            // meio), não o de cancelar antes de começar.
            int chamadas = 0;
            auto resultadoC = matriz::consolidacao::executarConsolidacao(
                projetoC->registro(), pastaProjetoC, destinoC, planoC,
                [&chamadas](int, int) { return ++chamadas <= 1; });

            check(resultadoC.cancelado, "the backup reports that it was cancelled");
            check(resultadoC.consolidados == 1,
                  "cancelling after the 1st file writes exactly 1, not the whole plan (" +
                      std::to_string(resultadoC.consolidados) + ")");
            check(resultadoC.totalPlanejado == 3, "the result carries the planned total for the \"X of Y\" summary");

            // O que foi gravado continua em disco — cancelar não é rollback.
            int arquivosNoDestino = 0;
            for (auto& f : juce::RangedDirectoryIterator(destinoC, true, "*", juce::File::findFiles))
                { juce::ignoreUnused(f); ++arquivosNoDestino; }
            check(arquivosNoDestino == 1, "the already-written file stays at the destination after cancelling");

            // Retomar reconhece o que já foi e termina o resto, sem refazer.
            auto planoRetomada = matriz::consolidacao::planejarConsolidacao(projetoC->registro(), pastaProjetoC, destinoC, soPastaManual());
            int jaFeitos = 0;
            for (auto& ip : planoRetomada.itens)
                if (ip.jaConsolidado) ++jaFeitos;
            check(jaFeitos == 1, "resuming recognises the file written before cancelling (does not copy it again)");

            auto resultadoRetomada = matriz::consolidacao::executarConsolidacao(projetoC->registro(), pastaProjetoC,
                                                                                 destinoC, planoRetomada);
            check(!resultadoRetomada.cancelado && resultadoRetomada.consolidados == 2 && resultadoRetomada.pulados == 1,
                  "resuming writes the 2 that were missing and skips the one already done");
        }

    } catch (const std::exception& e) {
        check(false, std::string("backup: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Item 11 — catálogo de proxies. A propriedade que justifica o subsistema:
// o catálogo é AUTÔNOMO. Abre sem o projeto que o gerou e sem o material
// original conectado, e ainda assim diz onde cada arquivo está.
// ---------------------------------------------------------------------------
void testarCatalogoProxies(const juce::File& dirTemp) {
    std::cout << "== Proxy catalogue ==\n";

    juce::File pastaProjeto = dirTemp.getChildFile("projeto_catalogo");
    juce::File destino = dirTemp.getChildFile("backup_catalogo");
    destino.createDirectory();

    // Simula um volume externo: o arquivo "vem" daqui, e depois somem os
    // dois (projeto e volume) pra provar que o catálogo se vira sozinho.
    juce::File volumeFalso = dirTemp.getChildFile("VolumeExterno").getChildFile("2003").getChildFile("Berlim");
    volumeFalso.createDirectory();

    matriz::model::NovoProjetoParams params;
    params.nome = "Catalogo Teste";
    params.modo = matriz::model::Modo::Preservacao;
    params.prefixoNomenclatura = "CAT";

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        std::string projetoId = projeto->projetoId();
        std::string agora = matriz::model::agoraIso8601();

        juce::File pastaAcervo;
        std::string pastaId = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mascara_nomenclatura, criado_em, "
            "atualizado_em) VALUES (?, ?, NULL, 'Tudo', 0, '{codigo}-{titulo}', ?, ?)",
            {matriz::db::Value::of(pastaId), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});

        // Uma imagem (gera miniatura de verdade) e um áudio (gera forma de onda).
        struct Fonte { const char* nome; const char* filtro; const char* extra; };
        std::vector<std::string> itemIds;
        for (int i = 0; i < 2; ++i) {
            std::string itemId = matriz::model::novoUuid();
            juce::String codigo = "CAT-" + juce::String(i + 1).paddedLeft('0', 3);
            projeto->registro().run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
                "VALUES (?, ?, ?, ?, 'foto', ?, ?)",
                {matriz::db::Value::of(itemId), matriz::db::Value::of(projetoId),
                 matriz::db::Value::of(codigo.toStdString()),
                 matriz::db::Value::of(i == 0 ? "Foto do palco" : "Registro sonoro"),
                 matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

            juce::File fonte = volumeFalso.getChildFile(i == 0 ? "palco.jpg" : "som.wav");
            if (i == 0)
                gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                                 "color=c=green:s=320x240", "-frames:v", "1", fonte.getFullPathName()});
            else
                gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                                 "sine=frequency=440:duration=1", fonte.getFullPathName()});

            auto ing = matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, itemId, fonte,
                                                       "preservation_master", true);
            matriz::ingest::gerarEGravarMiniaturaPrincipal(
                projeto->indice(), pastaProjeto, itemId, ing.arquivoId, ing.arquivoNoProjeto,
                matriz::ingest::categoriaPorExtensao(fonte), ing.leitura.duracaoSegundos);

            projeto->registro().run(
                "INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                 matriz::db::Value::of(pastaId), matriz::db::Value::of(agora)});

            // Um valor de ficha, pra provar que o catálogo carrega o que o
            // operador preencheu, não só o técnico.
            projeto->registro().run(
                "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                "VALUES (?, ?, 'raiz', 0, 'local', ?, 'humano', ?)",
                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                 matriz::db::Value::of("Berlim"), matriz::db::Value::of(agora)});

            itemIds.push_back(itemId);
        }

        // Grava o backup primeiro — é ele que dá o caminho_no_backup.
        auto plano = matriz::consolidacao::planejarConsolidacao(projeto->registro(), pastaProjeto, destino, soPastaManual());
        auto resConsolidacao = matriz::consolidacao::executarConsolidacao(projeto->registro(), pastaProjeto, destino, plano);
        check(resConsolidacao.consolidados == 2, "the backup wrote both items before the catalogue");

        // --- Estimativa antes de gerar ---
        auto estimativa = matriz::catalogo::estimar(projeto->registro(), projeto->indice(), pastaProjeto);
        check(estimativa.itens == 2, "the estimate counts both items");
        check(estimativa.tamanhoBytes > 0, "the estimate returns the real proxy size (> 0 bytes)");

        // --- Geração ---
        auto res = matriz::catalogo::gerar(projeto->registro(), projeto->indice(), pastaProjeto, destino);
        check(res.gravados == 2 && !res.cancelado, "the catalogue wrote both items");
        check(matriz::catalogo::ehPastaDeCatalogo(destino),
              "the backup root is recognised as holding a catalogue");

        // --- Cancelamento, igual às outras operações longas (item 10) ---
        {
            int chamadas = 0;
            auto resCancel = matriz::catalogo::gerar(projeto->registro(), projeto->indice(), pastaProjeto, destino,
                                                      [&chamadas](int, int) { return ++chamadas <= 1; });
            check(resCancel.cancelado && resCancel.gravados == 1,
                  "catalogue generation is cancellable too, and what it wrote stays");
            // Regenera inteiro pro resto do teste.
            matriz::catalogo::gerar(projeto->registro(), projeto->indice(), pastaProjeto, destino);
        }

        // =================================================================
        // O teste que importa: some com o projeto E com o volume de origem.
        // O catálogo tem que continuar respondendo.
        // =================================================================
        pastaProjeto.deleteRecursively();
        dirTemp.getChildFile("VolumeExterno").deleteRecursively();
        check(!pastaProjeto.exists(), "the original project was deleted");
        check(!volumeFalso.exists(), "the source volume was disconnected (deleted)");

        auto entradas = matriz::catalogo::abrir(destino);
        check(entradas.size() == 2, "the catalogue opens without the original project (" +
                                         std::to_string(entradas.size()) + " entries)");

        bool achouTitulo = false, achouFicha = false, achouMiniatura = false, achouVolume = false;
        juce::File pastaCatalogo = matriz::catalogo::resolverPastaCatalogo(destino);
        for (auto& e : entradas) {
            if (e.titulo == "Foto do palco") achouTitulo = true;
            if (e.fichaJson.contains("Berlim")) achouFicha = true;
            if (e.miniaturaRelativa.isNotEmpty() &&
                pastaCatalogo.getChildFile(e.miniaturaRelativa).existsAsFile())
                achouMiniatura = true;
            if (e.volumeOrigem.isNotEmpty()) achouVolume = true;
        }
        check(achouTitulo, "the catalogue carries the material title");
        check(achouFicha, "the catalogue carries the record the operator filled in");
        check(achouMiniatura, "the thumbnail was copied into the catalogue and exists on disk there");
        check(achouVolume, "the catalogue knows which drive the material came from");

        // descreverLocalizacao é função pura — dá pra testar com o caminho
        // do exemplo do spec, sem depender de onde o teste roda. O pipeline
        // acima roda em /var/folders/... (temp do macOS), que não é um
        // caminho representativo de disco de acervo.
        {
            juce::String descrito = matriz::catalogo::descreverLocalizacao(
                "/Volumes/HD Samsung T7/2003/Turne Europa/Berlim/faixa.wav");
            check(descrito == "HD Samsung T7 / 2003 / Turne Europa / Berlim",
                  "an external drive path becomes a readable description: \"" + descrito.toStdString() + "\"");

            juce::String fundo = matriz::catalogo::descreverLocalizacao(
                "/Volumes/HD/a/b/c/d/e/f/g/Berlim/faixa.wav");
            check(fundo.startsWith("HD /") && fundo.endsWith("Berlim") && fundo.length() < 40,
                  "a very deep path is summarised instead of becoming a wall of folders: \"" +
                      fundo.toStdString() + "\"");
        }

        // --- Localizar: a cópia do backup ainda está montada ---
        auto loc = matriz::catalogo::localizar(entradas.front(), pastaCatalogo);
        check(loc.fonteConectada && loc.arquivoReal.existsAsFile(),
              "with the backup mounted, the catalogue resolves to the real file and it opens");

        // --- Localizar com NADA montado ---
        juce::File copiaBackupTudo = destino.getChildFile("Tudo");
        copiaBackupTudo.deleteRecursively();
        auto locSemFonte = matriz::catalogo::localizar(entradas.front(), pastaCatalogo);
        check(!locSemFonte.fonteConectada, "with neither backup nor source, the catalogue does not pretend the file is there");
        check(locSemFonte.descricaoHumana.contains("Berlim"),
              "with the source missing, it says where to look: \"" + locSemFonte.descricaoHumana.toStdString() + "\"");

        // --- Portabilidade: mover a pasta inteira não quebra nada ---
        juce::File outroLugar = dirTemp.getChildFile("catalogo_movido");
        check(destino.moveFileTo(outroLugar), "the backup folder was moved to another path");
        auto entradasMovidas = matriz::catalogo::abrir(outroLugar);
        check(entradasMovidas.size() == 2, "the catalogue opens the same after moving (paths are relative)");
        juce::File catalogoMovido = matriz::catalogo::resolverPastaCatalogo(outroLugar);
        bool miniaturaAindaResolve = false;
        for (auto& e : entradasMovidas)
            if (e.miniaturaRelativa.isNotEmpty() && catalogoMovido.getChildFile(e.miniaturaRelativa).existsAsFile())
                miniaturaAindaResolve = true;
        check(miniaturaAindaResolve, "thumbnails still resolve after the move");

    } catch (const std::exception& e) {
        check(false, std::string("proxy catalogue: ") + e.what());
    }
}

} // namespace

// ===========================================================================
// Cache de análise (I2 / §4 cache_arquivo) — tudo medido UMA vez, na
// ingestão, e guardado no registro. É o que faz a Estação de Escuta abrir
// offline (critério 2) sem recalcular nada num clique (I2).
// ===========================================================================
void testarCacheDeArquivo(const juce::File& dir) {
    std::cout << "== Per-file analysis cache (I2) ==\n";

    juce::File pastaProjeto = dir.getChildFile("projeto_cache");
    juce::File wav = dir.getChildFile("cache_tom.wav");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=440:duration=3", "-ar", "48000", "-ac", "2",
                    "-sample_fmt", "s16", wav.getFullPathName()});

    matriz::model::NovoProjetoParams params;
    params.nome = "Cache Teste";
    params.modo = matriz::model::Modo::Preservacao;
    params.prefixoNomenclatura = "CCH";

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        std::string agora = matriz::model::agoraIso8601();
        std::string itemId = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
            "VALUES (?, ?, 'CCH-001', 'Tom de teste', 'digital_audio', ?, ?)",
            {matriz::db::Value::of(itemId), matriz::db::Value::of(projeto->projetoId()),
             matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

        auto ing = matriz::ingest::ingerirArquivo(projeto->registro(), pastaProjeto, itemId, wav,
                                                   "preservation_master", true);

        matriz::ingest::calcularEGravarCache(projeto->registro(), ing.arquivoId, wav,
                                              matriz::ingest::CategoriaMidia::Audio, pastaProjeto, 3.0);

        auto lido = matriz::ingest::lerCache(projeto->registro(), ing.arquivoId);
        check(lido.has_value(), "cache_arquivo written and read back by primary key");
        if (!lido) return;

        check(!lido->formaOnda.empty(), "the waveform was serialised as a BLOB in the registry");
        // Referência: `ffmpeg -af ebur128` sobre ESTE mesmo arquivo dá
        // I = -21.8 LUFS e peak = -21.1 dBFS (o filtro `sine` do lavfi não
        // gera em fundo de escala). Critério 9 pede 0.1 LU de tolerância
        // contra um analisador externo — é essa a checagem aqui.
        check(lido->lufsI.has_value() && std::abs(*lido->lufsI - (-21.8)) <= 0.1,
              "LUFS-I matches ffmpeg's ebur128 within 0.1 LU (measured " +
                  std::to_string(lido->lufsI.value_or(0.0)) + ", reference -21.8)");
        check(lido->truePeak.has_value() && std::abs(*lido->truePeak - (-21.1)) <= 0.1,
              "true peak matches the reference within 0.1 dB (measured " +
                  std::to_string(lido->truePeak.value_or(-144.0)) + ", reference -21.1)");
        check(lido->peakAmostra.has_value() && *lido->peakAmostra > 0.0, "sample peak written");
        // Tom senoidal idêntico nos dois canais: correlação tem que ser ~+1.
        check(lido->correlacaoMedia.has_value() && *lido->correlacaoMedia > 0.95,
              "correlation of an identical L/R signal is ~+1 (" +
                  std::to_string(lido->correlacaoMedia.value_or(0.0)) + ")");

        auto onda = matriz::ingest::lerFormaDeOnda(lido->formaOnda);
        check(onda.minimos.size() == onda.maximos.size() && !onda.minimos.empty(),
              "the blob comes back as min/max pairs of the same length (" + std::to_string(onda.minimos.size()) +
                  " buckets)");
        // 20 baldes por segundo × ~3 s.
        check(onda.minimos.size() > 40 && onda.minimos.size() < 80,
              "waveform resolution matches 20 buckets per second");

        // A prova de I3: o cache é lido sem o arquivo existir.
        check(wav.deleteFile(), "the original file was deleted (simulates a disconnected Vault)");
        auto offline = matriz::ingest::lerCache(projeto->registro(), ing.arquivoId);
        check(offline.has_value() && !offline->formaOnda.empty() && offline->lufsI.has_value(),
              "waveform and metrics stay available with the file offline (I3)");
    } catch (const std::exception& e) {
        check(false, std::string("analysis cache: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
}

// ===========================================================================
// Reconciliação de Vault (§8) — o conhecimento sobrevive ao arquivo: mover,
// alterar e apagar não podem custar a ficha nem o histórico.
// ===========================================================================
void testarReconciliacaoDeVault(const juce::File& dir) {
    std::cout << "== Vault reconciliation ==\n";

    juce::File pastaProjeto = dir.getChildFile("projeto_vault");
    juce::File volume = dir.getChildFile("VolumeVault");
    juce::File sub = volume.getChildFile("2004");
    sub.createDirectory();

    juce::File a = sub.getChildFile("faixa_a.wav");
    juce::File b = sub.getChildFile("faixa_b.wav");
    juce::File c = sub.getChildFile("faixa_c.wav");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=300:duration=1", "-ar", "44100", a.getFullPathName()});
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=600:duration=1", "-ar", "44100", b.getFullPathName()});
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "sine=frequency=900:duration=1", "-ar", "44100", c.getFullPathName()});

    matriz::model::NovoProjetoParams params;
    params.nome = "Vault Teste";
    params.modo = matriz::model::Modo::Preservacao;
    params.prefixoNomenclatura = "VLT";

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        std::string agora = matriz::model::agoraIso8601();

        // Um Vault apontando pro volume sintético. Sem UUID: um diretório
        // comum não é um volume montado de verdade, e a resolução por
        // caminho é justamente o fallback previsto pra esse caso.
        std::string vaultId = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO vault (id, projeto_id, nome, tipo, uuid_volume, raiz_relativa, localizacao, status, "
            "criado_em) VALUES (?, ?, 'Volume de Teste', 'local', '', '', ?, 'online', ?)",
            {matriz::db::Value::of(vaultId), matriz::db::Value::of(projeto->projetoId()),
             matriz::db::Value::of(volume.getFullPathName().toStdString()), matriz::db::Value::of(agora)});

        struct Registrado { std::string itemId, arquivoId; juce::File arquivo; };
        std::vector<Registrado> registrados;
        int n = 0;
        for (auto* arquivo : {&a, &b, &c}) {
            std::string itemId = matriz::model::novoUuid();
            projeto->registro().run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
                "VALUES (?, ?, ?, ?, 'digital_audio', ?, ?)",
                {matriz::db::Value::of(itemId), matriz::db::Value::of(projeto->projetoId()),
                 matriz::db::Value::of("VLT-00" + std::to_string(++n)),
                 matriz::db::Value::of(arquivo->getFileNameWithoutExtension().toStdString()),
                 matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

            auto hash = matriz::ingest::calcularChecksums(*arquivo);
            std::string arquivoId = matriz::model::novoUuid();
            projeto->registro().run(
                "INSERT INTO arquivo (id, item_id, vault_id, caminho_relativo, caminho_absoluto_origem, papel, "
                "eh_master, tamanho_bytes, checksum_md5, checksum_sha256, checksum_gerado_em, estado_presenca, "
                "criado_em, atualizado_em) "
                "VALUES (?, ?, ?, ?, ?, 'preservation_master', 1, ?, ?, ?, ?, 'presente', ?, ?)",
                {matriz::db::Value::of(arquivoId), matriz::db::Value::of(itemId), matriz::db::Value::of(vaultId),
                 matriz::db::Value::of(arquivo->getRelativePathFrom(volume).toStdString()),
                 matriz::db::Value::of(arquivo->getFullPathName().toStdString()),
                 matriz::db::Value::of(static_cast<long long>(arquivo->getSize())),
                 matriz::db::Value::of(hash.md5), matriz::db::Value::of(hash.sha256),
                 matriz::db::Value::of(agora), matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

            // Ficha preenchida pelo operador: é ela que NÃO pode se perder
            // quando o arquivo se mexe.
            projeto->registro().run(
                "INSERT INTO item_campo (item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                "VALUES (?, 'raiz', 0, 'genero', 'Jazz', 'humano', ?)",
                {matriz::db::Value::of(itemId), matriz::db::Value::of(agora)});

            registrados.push_back({itemId, arquivoId, *arquivo});
        }

        // --- Varredura com tudo no lugar: nada a reportar ---
        auto limpo = matriz::vault::varreduraRapida(projeto->registro(), vaultId);
        check(limpo.semMudancas(), "a scan with everything in place reports no change at all (" +
                                        limpo.comoTexto().toStdString() + ")");

        // --- MOVER: mesmo conteúdo, outro caminho ---
        juce::File novoLugar = volume.getChildFile("2005");
        novoLugar.createDirectory();
        juce::File aMovido = novoLugar.getChildFile("faixa_a_renomeada.wav");
        check(a.moveFileTo(aMovido), "file A moved and renamed outside the software");

        // --- ALTERAR: mesmo caminho, conteúdo diferente ---
        b.appendText("bytes que mudam o conteudo e o tamanho");

        // --- APAGAR ---
        check(c.deleteFile(), "file C deleted outside the software");

        // --- NOVO: material que apareceu no volume sem catalogação ---
        novoLugar.getChildFile("nunca_catalogado.wav").replaceWithText("conteudo novo");

        auto resumo = matriz::vault::varreduraRapida(projeto->registro(), vaultId);
        check(resumo.movidos == 1, "a moved file is recognised by its hash, not re-registered (" +
                                        std::to_string(resumo.movidos) + ")");
        check(resumo.alterados == 1, "a file with different content is marked as changed (" +
                                          std::to_string(resumo.alterados) + ")");
        check(resumo.ausentes == 1, "a deleted file becomes missing, it does not vanish from the catalogue (" +
                                         std::to_string(resumo.ausentes) + ")");
        check(resumo.novos == 1, "uncatalogued material on the volume is counted as new (" +
                                      std::to_string(resumo.novos) + ")");
        check(resumo.comoTexto().contains("moved") && resumo.comoTexto().contains("missing"),
              "the quiet summary has the expected format: \"" + resumo.comoTexto().toStdString() + "\"");

        // O ponto inteiro do §8: a ficha acompanhou o asset movido.
        {
            auto stmt = projeto->registro().prepare(
                "SELECT a.caminho_relativo, a.estado_presenca, "
                "(SELECT valor FROM item_campo c WHERE c.item_id = a.item_id AND c.campo_id = 'genero') "
                "FROM arquivo a WHERE a.id = ?");
            stmt.bind(1, matriz::db::Value::of(registrados[0].arquivoId));
            check(stmt.step(), "the record of the moved file still exists (same id)");
            check(juce::String(stmt.columnText(0)).contains("faixa_a_renomeada"),
                  "the path was updated to the new location: " + stmt.columnText(0));
            check(stmt.columnText(1) == "presente", "the moved file goes back to 'presente'");
            check(stmt.columnText(2) == "Jazz", "the filled-in record followed the moved asset (criterion 15)");
        }

        // Critério 16: remoção física preserva o conhecimento.
        {
            auto stmt = projeto->registro().prepare(
                "SELECT a.estado_presenca, IFNULL(a.visto_pela_ultima_vez, ''), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = a.item_id AND c.campo_id = 'genero') "
                "FROM arquivo a WHERE a.id = ?");
            stmt.bind(1, matriz::db::Value::of(registrados[2].arquivoId));
            check(stmt.step(), "the record of the deleted file stays in the catalogue (criterion 16)");
            check(stmt.columnText(0) == "ausente", "the deleted file's state is 'ausente'");
            check(!stmt.columnText(1).empty(), "'last seen' was recorded");
            check(stmt.columnText(2) == "Jazz", "the deleted item's record is still intact");
        }

        // Proveniência append-only registrou os três eventos.
        {
            auto stmt = projeto->registro().prepare(
                "SELECT evento, COUNT(*) FROM proveniencia GROUP BY evento");
            std::map<std::string, int> eventos;
            while (stmt.step()) eventos[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
            check(eventos["arquivo_movido"] == 1, "provenance recorded the move event");
            check(eventos["arquivo_alterado"] == 1, "provenance recorded the change event");
            check(eventos["arquivo_ausente"] == 1, "provenance recorded the missing event");
        }

        // --- Verificação completa: bit rot ---
        // Corrompe o arquivo movido MANTENDO o tamanho: é exatamente o caso
        // que a varredura rápida não pega de propósito, e que a auditoria
        // completa existe pra encontrar.
        {
            juce::MemoryBlock bloco;
            aMovido.loadFileAsData(bloco);
            auto* bytes = static_cast<char*>(bloco.getData());
            for (size_t i = bloco.getSize() / 2; i < bloco.getSize() / 2 + 64 && i < bloco.getSize(); ++i)
                bytes[i] = static_cast<char>(~bytes[i]);
            aMovido.replaceWithData(bloco.getData(), bloco.getSize());

            auto rapida = matriz::vault::varreduraRapida(projeto->registro(), vaultId);
            check(rapida.alterados == 0,
                  "the quick scan does NOT re-read bytes of a file with the same size (criterion 17)");

            auto completa = matriz::vault::verificacaoCompleta(projeto->registro(), vaultId);
            check(completa.corrompidos == 1, "the full check finds the bit rot (" +
                                                  std::to_string(completa.corrompidos) + ")");

            auto stmt = projeto->registro().prepare("SELECT estado_presenca FROM arquivo WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(registrados[0].arquivoId));
            check(stmt.step() && stmt.columnText(0) == "corrompido", "the corrupted file is marked as such");

            auto prov = projeto->registro().prepare(
                "SELECT COUNT(*) FROM proveniencia WHERE evento = 'arquivo_corrompido'");
            prov.step();
            check(prov.columnInt(0) == 1, "the old hash was preserved in provenance before anything else");
        }

        // --- Vault offline ---
        {
            juce::File guardado = dir.getChildFile("VolumeVault_desmontado");
            check(volume.moveFileTo(guardado), "volume unmounted (moved away from the registered path)");
            matriz::vault::atualizarPresencaDoVault(projeto->registro(), vaultId);
            auto stmt = projeto->registro().prepare("SELECT status FROM vault WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(vaultId));
            check(stmt.step() && stmt.columnText(0) == "offline", "a Vault whose path is not mounted becomes 'offline'");

            auto ficaramOnline = matriz::vault::reavaliarVaults(projeto->registro());
            check(ficaramOnline.empty(), "re-evaluating with the volume away returns no Vault as reconnected");

            check(guardado.moveFileTo(volume), "volume remounted");
            auto reconectados = matriz::vault::reavaliarVaults(projeto->registro());
            check(reconectados.size() == 1 && reconectados[0] == vaultId,
                  "remounting the volume returns the Vault as freshly connected (the scan trigger)");
        }
    } catch (const std::exception& e) {
        check(false, std::string("Vault reconciliation: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
    volume.deleteRecursively();
}

// ===========================================================================
// Coleções Inteligentes (§10) — VIEWS, não tabelas: cada consulta reexecuta
// a pergunta sobre o estado atual, sem ninguém reindexar nada.
// ===========================================================================
void testarColecoesInteligentes(const juce::File& dir) {
    std::cout << "== Smart collections as SQL views ==\n";

    juce::File pastaProjeto = dir.getChildFile("projeto_colecoes");
    if (pastaProjeto.exists()) pastaProjeto.deleteRecursively();

    matriz::model::NovoProjetoParams params;
    params.nome = "Colecoes Teste";
    params.modo = matriz::model::Modo::Preservacao;
    params.prefixoNomenclatura = "COL";

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        std::string agora = matriz::model::agoraIso8601();
        auto& db = projeto->registro();

        auto novoItem = [&](const std::string& codigo, const std::string& titulo) {
            std::string id = matriz::model::novoUuid();
            db.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
                   "VALUES (?, ?, ?, ?, 'digital_audio', ?, ?)",
                   {matriz::db::Value::of(id), matriz::db::Value::of(projeto->projetoId()),
                    matriz::db::Value::of(codigo), matriz::db::Value::of(titulo),
                    matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
            return id;
        };
        auto novoArquivo = [&](const std::string& itemId, const std::string& estadoPresenca) {
            std::string id = matriz::model::novoUuid();
            db.run("INSERT INTO arquivo (id, item_id, caminho_relativo, papel, eh_master, estado_presenca, "
                   "criado_em, atualizado_em) VALUES (?, ?, ?, 'preservation_master', 1, ?, ?, ?)",
                   {matriz::db::Value::of(id), matriz::db::Value::of(itemId),
                    matriz::db::Value::of("x/" + id + ".wav"), matriz::db::Value::of(estadoPresenca),
                    matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
            return id;
        };
        auto naColecao = [&](const std::string& colecao) {
            auto stmt = db.prepare("SELECT COUNT(*) FROM colecao_embutida WHERE colecao = ?");
            stmt.bind(1, matriz::db::Value::of(colecao));
            stmt.step();
            return static_cast<int>(stmt.columnInt(0));
        };

        // Clipping: true peak acima de -0.1 dBTP no cache.
        std::string clipando = novoItem("COL-001", "Master estourado");
        std::string arqClipando = novoArquivo(clipando, "presente");
        db.run("INSERT INTO cache_arquivo (arquivo_id, true_peak, calculado_em, versao_analise) "
               "VALUES (?, 0.4, ?, 1)",
               {matriz::db::Value::of(arqClipando), matriz::db::Value::of(agora)});

        std::string limpo = novoItem("COL-002", "Master limpo");
        std::string arqLimpo = novoArquivo(limpo, "presente");
        db.run("INSERT INTO cache_arquivo (arquivo_id, true_peak, calculado_em, versao_analise) "
               "VALUES (?, -3.2, ?, 1)",
               {matriz::db::Value::of(arqLimpo), matriz::db::Value::of(agora)});

        check(naColecao("clipping") == 1, "Clipping catches only what goes above -0.1 dBTP (" +
                                               std::to_string(naColecao("clipping")) + ")");

        // Ausentes e não baixados.
        std::string sumido = novoItem("COL-003", "Sumiu");
        novoArquivo(sumido, "ausente");
        std::string naNuvem = novoItem("COL-004", "Placeholder de nuvem");
        novoArquivo(naNuvem, "nao_baixado");
        check(naColecao("ausentes") == 1, "Missing lists the file marked as missing");
        check(naColecao("nao_baixados") == 1, "Not downloaded lists the cloud placeholder");

        // Uma view se atualiza sozinha: mudar o estado muda a coleção, sem
        // ninguém reindexar. É a razão de serem views e não tabelas.
        db.run("UPDATE arquivo SET estado_presenca = 'presente' WHERE item_id = ?",
               {matriz::db::Value::of(sumido)});
        check(naColecao("ausentes") == 0, "finding the file again drops the item from the collection at once, with no reindexing");

        // Vulneráveis: nenhum item foi publicado ainda, então todos entram.
        int totalItens = 4;
        check(naColecao("vulneraveis") == totalItens,
              "with no completed publication, every item is vulnerable (" + std::to_string(naColecao("vulneraveis")) + ")");

        // Revisão: marcador aberto do tipo certo.
        db.run("INSERT INTO marcador (id, item_id, tempo_inicio, tipo_id, status, autor, criado_em) "
               "VALUES (?, ?, 12.5, 'dropout', 'aberto', 'operador', ?)",
               {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(limpo),
                matriz::db::Value::of(agora)});
        db.run("INSERT INTO marcador (id, item_id, tempo_inicio, tipo_id, status, autor, criado_em) "
               "VALUES (?, ?, 30.0, 'mofo', 'aberto', 'operador', ?)",
               {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(clipando),
                matriz::db::Value::of(agora)});
        check(naColecao("revisao") == 1, "Needs review catches open dropout/review markers, not just any marker (" +
                                              std::to_string(naColecao("revisao")) + ")");

        // Incompletos: sem artista nem ISRC, todos entram; preencher os dois
        // tira o item da lista.
        check(naColecao("incompletos") == totalItens, "with no artist and no ISRC, every item is incomplete");
        db.run("INSERT INTO item_campo (item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
               "VALUES (?, 'raiz', 0, 'artista_principal', 'Banda X', 'humano', ?)",
               {matriz::db::Value::of(limpo), matriz::db::Value::of(agora)});
        db.run("INSERT INTO item_campo (item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
               "VALUES (?, 'raiz', 0, 'isrc', 'BRABC2400001', 'humano', ?)",
               {matriz::db::Value::of(limpo), matriz::db::Value::of(agora)});
        check(naColecao("incompletos") == totalItens - 1,
              "filling in artist and ISRC drops the item from Incomplete (" +
                  std::to_string(naColecao("incompletos")) + ")");
    } catch (const std::exception& e) {
        check(false, std::string("smart collections: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
}

// ===========================================================================
// Publicação (§9) — o pacote autocontido `<Nome>.matriz/`, com manifesto em
// texto puro conferível por `shasum -c` e leitura de volta de cada byte.
// ===========================================================================
void testarPublicacao(const juce::File& dir) {
    std::cout << "== Backup publication ==\n";

    juce::File pastaProjeto = dir.getChildFile("projeto_publicacao");
    juce::File volume = dir.getChildFile("VolumePub");
    volume.createDirectory();
    juce::File destino = dir.getChildFile("MidiaDeArquivamento");
    destino.createDirectory();

    matriz::model::NovoProjetoParams params;
    params.nome = "Publicacao Teste";
    params.modo = matriz::model::Modo::Preservacao;
    params.prefixoNomenclatura = "PUB";

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        std::string agora = matriz::model::agoraIso8601();
        auto& db = projeto->registro();

        std::string vaultId = matriz::model::novoUuid();
        db.run("INSERT INTO vault (id, projeto_id, nome, tipo, uuid_volume, raiz_relativa, localizacao, status, "
               "criado_em) VALUES (?, ?, 'Origem', 'local', '', '', ?, 'online', ?)",
               {matriz::db::Value::of(vaultId), matriz::db::Value::of(projeto->projetoId()),
                matriz::db::Value::of(volume.getFullPathName().toStdString()), matriz::db::Value::of(agora)});

        std::string pastaId = matriz::model::novoUuid();
        db.run("INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mascara_nomenclatura, "
               "criado_em, atualizado_em) VALUES (?, ?, NULL, 'Tudo', 0, '{codigo}-{titulo}', ?, ?)",
               {matriz::db::Value::of(pastaId), matriz::db::Value::of(projeto->projetoId()),
                matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

        std::vector<std::string> itemIds;
        for (int i = 0; i < 2; ++i) {
            juce::File arquivo = volume.getChildFile("faixa_" + juce::String(i + 1) + ".wav");
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                            "-i", "sine=frequency=" + juce::String(200 * (i + 1)) + ":duration=1",
                            "-ar", "44100", arquivo.getFullPathName()});

            std::string itemId = matriz::model::novoUuid();
            db.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
                   "VALUES (?, ?, ?, ?, 'digital_audio', ?, ?)",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(projeto->projetoId()),
                    matriz::db::Value::of("PUB-00" + std::to_string(i + 1)),
                    matriz::db::Value::of("Faixa " + std::to_string(i + 1)),
                    matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

            auto hash = matriz::ingest::calcularChecksums(arquivo);
            std::string arquivoId = matriz::model::novoUuid();
            db.run("INSERT INTO arquivo (id, item_id, vault_id, caminho_relativo, caminho_absoluto_origem, papel, "
                   "eh_master, tamanho_bytes, checksum_sha256, checksum_gerado_em, estado_presenca, criado_em, "
                   "atualizado_em) VALUES (?, ?, ?, ?, ?, 'preservation_master', 1, ?, ?, ?, 'presente', ?, ?)",
                   {matriz::db::Value::of(arquivoId), matriz::db::Value::of(itemId),
                    matriz::db::Value::of(vaultId),
                    matriz::db::Value::of(arquivo.getRelativePathFrom(volume).toStdString()),
                    matriz::db::Value::of(arquivo.getFullPathName().toStdString()),
                    matriz::db::Value::of(static_cast<long long>(arquivo.getSize())),
                    matriz::db::Value::of(hash.sha256), matriz::db::Value::of(agora),
                    matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

            matriz::ingest::calcularEGravarCache(db, arquivoId, arquivo,
                                                  matriz::ingest::CategoriaMidia::Audio, pastaProjeto, 1.0);

            db.run("INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
                   {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                    matriz::db::Value::of(pastaId), matriz::db::Value::of(agora)});
            itemIds.push_back(itemId);
        }

        // Um marcador aberto: tem que aparecer no relatório de preservação
        // mesmo com o pacote íntegro byte a byte.
        db.run("INSERT INTO marcador (id, item_id, tempo_inicio, tipo_id, status, autor, criado_em) "
               "VALUES (?, ?, 4.0, 'revisar', 'aberto', 'operador', ?)",
               {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemIds[0]),
                matriz::db::Value::of(agora)});

        matriz::publicacao::ParamsPublicacao pp;
        pp.pastaDestino = destino;
        pp.nomePacote = "Acervo2024";
        pp.autor = "Anderson Guerra";
        pp.vaultId = vaultId;
        pp.nota = "Primeira leva de arquivamento";
        pp.hierarquia = soPastaManual();

        auto res = matriz::publicacao::publicar(db, pastaProjeto, pp);
        check(res.concluida, "publication completed with no failures");
        check(res.itensGravados == 2, "both items were written and verified (" +
                                           std::to_string(res.itensGravados) + ")");

        juce::File pacote = res.pacote;
        check(pacote.getFileName() == "Acervo2024.matriz", "the package has the expected name and suffix: " +
                                                                pacote.getFileName().toStdString());
        check(pacote.getChildFile("manifest.txt").existsAsFile(), "manifest.txt exists");
        check(pacote.getChildFile("manifest.sqlite").existsAsFile(), "manifest.sqlite exists");
        check(pacote.getChildFile("catalog.sqlite").existsAsFile(), "catalog.sqlite exists");
        check(pacote.getChildFile("exports").isDirectory(), "exports/ exists");
        check(pacote.getChildFile("cache").isDirectory(), "cache/ exists");
        check(res.relatorio.existsAsFile(), "preservation report generated");

        // O manifesto é texto puro e legível — critério 19.
        {
            juce::String texto = pacote.getChildFile("manifest.txt").loadFileAsString();
            check(texto.contains("SHA-256"), "manifest.txt states the algorithm in readable text");
            check(texto.contains("exports/"), "manifest.txt lists the relative paths of the files");
            int linhasDeArquivo = 0;
            juce::StringArray linhas;
            linhas.addLines(texto);
            for (const auto& l : linhas)
                if (!l.trim().startsWith("#") && l.contains("  ")) ++linhasDeArquivo;
            check(linhasDeArquivo == 2, "manifest.txt has one line per published file (" +
                                             std::to_string(linhasDeArquivo) + ")");
        }

        // O catálogo viajou junto: as fichas abrem sem o projeto original.
        {
            matriz::db::Database copia(pacote.getChildFile("catalog.sqlite").getFullPathName().toStdString());
            auto stmt = copia.prepare("SELECT COUNT(*) FROM item");
            stmt.step();
            check(stmt.columnInt(0) == 2, "catalog.sqlite carries the catalogue items");

            auto prov = copia.prepare("SELECT COUNT(*) FROM proveniencia WHERE evento = 'publicado'");
            prov.step();
            check(prov.columnInt(0) == 2, "the publication provenance travelled inside the package");
        }

        // O relatório mistura integridade e curadoria, como o §9 pede.
        {
            juce::String rel = res.relatorio.loadFileAsString();
            check(rel.contains("Anderson Guerra"), "the report carries the operator signature");
            check(rel.contains("read back from the destination"),
                  "the report states that the bytes were read back from the destination");
            check(rel.contains("revisar") || rel.contains("PUB-001"),
                  "the open audit marker shows up in the report");
        }

        // item_publicacao guarda o checksum VALIDADO no destino.
        {
            auto stmt = db.prepare(
                "SELECT COUNT(*) FROM item_publicacao WHERE publicacao_id = ? AND checksum_validado <> ''");
            stmt.bind(1, matriz::db::Value::of(res.publicacaoId));
            stmt.step();
            check(stmt.columnInt(0) == 2, "item_publicacao recorded the validated checksum of each item");
        }

        // Publicado deixa de ser vulnerável — a view responde na hora.
        {
            auto stmt = db.prepare("SELECT COUNT(*) FROM colecao_vulneraveis");
            stmt.step();
            check(stmt.columnInt(0) == 0, "after a completed publication, no item stays vulnerable");
        }

        // Conferência do pacote anos depois, sem o projeto de origem.
        auto verificacao = matriz::publicacao::verificarPacote(pacote);
        check(verificacao.integro() && verificacao.conferidos == 2,
              "verificarPacote checks the whole package against its own manifest");

        // E detecta corrupção.
        {
            juce::File umArquivo;
            for (const auto& e : juce::RangedDirectoryIterator(pacote.getChildFile("exports"), true, "*",
                                                                juce::File::findFiles)) {
                umArquivo = e.getFile();
                break;
            }
            check(umArquivo.existsAsFile(), "found a published file to corrupt");
            umArquivo.appendText("corrupcao");
            auto depois = matriz::publicacao::verificarPacote(pacote);
            check(!depois.integro() && depois.divergentes == 1,
                  "one flipped byte in the package is caught by the manifest check");
        }

        // Vault offline não pode virar pacote "concluído" vazio de conteúdo.
        {
            juce::File guardado = dir.getChildFile("VolumePub_desmontado");
            volume.moveFileTo(guardado);
            matriz::publicacao::ParamsPublicacao falha = pp;
            falha.nomePacote = "AcervoSemFonte";
            auto resFalha = matriz::publicacao::publicar(db, pastaProjeto, falha);
            check(!resFalha.concluida, "publishing with the Vault disconnected does NOT produce a 'concluida' package");
            check(resFalha.itensGravados == 0 && resFalha.falhas.size() == 2,
                  "each unavailable item becomes an explicit failure, never a silent omission");

            auto stmt = db.prepare("SELECT status FROM publicacao WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(resFalha.publicacaoId));
            check(stmt.step() && stmt.columnText(0) == "falha", "the publication is recorded as 'falha'");
            guardado.moveFileTo(volume);
        }
    } catch (const std::exception& e) {
        check(false, std::string("publication: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
    volume.deleteRecursively();
    destino.deleteRecursively();
}

int main() {
    if (!ffmpegDisponivel()) {
        std::cout << "ffmpeg unavailable - cannot generate test media. Aborting.\n";
        return 1;
    }

    juce::File tmpDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("matriz_ingest_selftest_" + juce::Uuid().toDashedString());
    tmpDir.createDirectory();

    testarCategoriaPorExtensao();
    testarChecksum(tmpDir);
    testarLeituraTecnicaAudio(tmpDir);
    testarLeituraTecnicaImagem(tmpDir);
    testarExifReal(tmpDir);
    testarLeituraTecnicaPdf(tmpDir);
    testarMiniaturas(tmpDir);
    testarDuplicataPHash(tmpDir);
    testarDuplicataFingerprintAudio(tmpDir);
    testarCorteDeBanda(tmpDir);
    testarClassificadorFalaMusica(tmpDir);
    testarInferenciaEstrutura(tmpDir);
    testarPipelineCompletoDeIngest(tmpDir);
    testarMascaraDeNomenclatura();
    testarHierarquiaBackup(tmpDir);
    testarLoudnessEMarcadores(tmpDir);
    testarConsolidacao(tmpDir);
    testarCatalogoProxies(tmpDir);
    testarCacheDeArquivo(tmpDir);
    testarReconciliacaoDeVault(tmpDir);
    testarColecoesInteligentes(tmpDir);
    testarPublicacao(tmpDir);

    tmpDir.deleteRecursively();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : std::to_string(failures) + " FAILURE(S)") << "\n";
    return failures == 0 ? 0 : 1;
}
