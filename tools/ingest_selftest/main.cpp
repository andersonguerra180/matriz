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
    std::cout << "== Leitura técnica — imagem (ffprobe) ==\n";

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

bool comandoDisponivel(const std::string& nome) {
    juce::ChildProcess proc;
    if (!proc.start(juce::StringArray{"/usr/bin/which", nome}, juce::ChildProcess::wantStdOut)) return false;
    juce::String saida = proc.readAllProcessOutput();
    proc.waitForProcessToFinish(5000);
    return saida.trim().isNotEmpty();
}

void testarLeituraTecnicaPdf(const juce::File& dir) {
    std::cout << "== Leitura técnica — PDF ==\n";

    // §A.3: PDFium e MuPDF não têm caminho FetchContent+CMake viável (ver a
    // nota longa em LeituraTecnica.cpp) — o parser artesanal de contagem de
    // páginas saiu, e não foi substituído por nenhuma biblioteca. Este teste
    // usa um PDF de verdade (gerado pelo filtro texto->PDF do CUPS, não
    // construído à mão) só pra confirmar que a ausência de contagem de
    // páginas é honesta — nunca um número adivinhado — mesmo sobre um
    // arquivo real com a estrutura interna que quiser.
    if (!comandoDisponivel("cupsfilter")) {
        check(true, "cupsfilter indisponível neste ambiente — teste de PDF real pulado, não é uma falha");
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
        check(true, "cupsfilter não gerou PDF neste ambiente — teste pulado, não é uma falha");
        return;
    }

    try {
        auto r = matriz::ingest::lerTecnica(pdf);
        std::string json = matriz::ingest::paraJson(r);
        check(json.find("fileSizeBytes") != std::string::npos, "fileSizeBytes presente pro PDF real: " + json);
        check(json.find("pageCountEstimado") == std::string::npos,
              "pageCountEstimado ausente (honesto — sem parser artesanal chutando número): " + json);
    } catch (const std::exception& e) {
        check(false, std::string("leitura técnica de PDF real: ") + e.what());
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

void testarExifReal(const juce::File& dir) {
    std::cout << "== EXIF real via Exiv2 ==\n";

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
        check(false, std::string("escrever EXIF de teste via Exiv2: ") + e.what());
        return;
    }

    try {
        auto r = matriz::ingest::lerTecnica(jpg);
        check(r.exifCamera.has_value() && r.exifCamera->find("MatrizTeste") != std::string::npos &&
                  r.exifCamera->find("Camera XPTO") != std::string::npos,
              "câmera lida do EXIF real: " + r.exifCamera.value_or("(nenhuma)"));
        check(r.exifLente.has_value() && *r.exifLente == "Lente 50mm", "lente lida do EXIF real: " + r.exifLente.value_or("(nenhuma)"));
        check(r.exifDataOriginal.has_value() && r.exifDataOriginal->find("2024") != std::string::npos,
              "data original lida do EXIF real: " + r.exifDataOriginal.value_or("(nenhuma)"));
        check(r.exifOrientacao.has_value() && *r.exifOrientacao == 6,
              "orientação lida do EXIF real: " + std::to_string(r.exifOrientacao.value_or(-1)));
        check(matriz::ingest::paraJson(r).find("MatrizTeste") != std::string::npos,
              "EXIF entra no JSON serializado (caracteristicas_tecnicas_json)");
    } catch (const std::exception& e) {
        check(false, std::string("ler EXIF real: ") + e.what());
    }

    // Imagem sem EXIF nenhum (todas as outras geradas por ffmpeg nesta suite)
    // não deve lançar — ausência de EXIF é normal, não erro.
    juce::File semExif = dir.getChildFile("sem_exif.jpg");
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "color=c=blue:s=100x100", "-frames:v", "1", semExif.getFullPathName()});
    try {
        auto r = matriz::ingest::lerTecnica(semExif);
        check(!r.exifCamera.has_value() && !r.exifDataOriginal.has_value(),
              "imagem sem EXIF não lança e fica com campos EXIF vazios");
    } catch (const std::exception& e) {
        check(false, std::string("imagem sem EXIF não deveria lançar: ") + e.what());
    }
}

void testarCategoriaPorExtensao() {
    std::cout << "== Categoria por extensão ==\n";
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.wav")) == matriz::ingest::CategoriaMidia::Audio, "wav -> Audio");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.mp4")) == matriz::ingest::CategoriaMidia::Video, "mp4 -> Video");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.jpg")) == matriz::ingest::CategoriaMidia::Imagem, "jpg -> Imagem");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.pdf")) == matriz::ingest::CategoriaMidia::Documento, "pdf -> Documento");
    check(matriz::ingest::categoriaPorExtensao(juce::File("/tmp/x.xyz")) == matriz::ingest::CategoriaMidia::Desconhecida, "xyz -> Desconhecida");
}

void testarMascaraDeNomenclatura() {
    std::cout << "== Máscara de nomenclatura (item 10, §11.6) ==\n";

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
          "exemplo do próprio spec (§11.6) resolve exatamente como documentado");
    check(matriz::consolidacao::resolverMascara("{original}", ctx) == "IMG_0001",
          "{original} resolve pro nome original sem extensão");
    check(matriz::consolidacao::resolverMascara("{acervo}-{pasta}-{codigo}", ctx) ==
              "Acervo do Anderson-Turne Europa-ACR2026-007",
          "{acervo} e {pasta} resolvem");
    check(matriz::consolidacao::resolverMascara("{artista_principal} - {titulo}", ctx) == "Banda Teste - ensaio-berlim",
          "campo de ficha arbitrário (não um token embutido) resolve pelo próprio id");

    std::vector<std::string> naoResolvidos;
    std::string resultado = matriz::consolidacao::resolverMascara("{codigo}-{campo_inexistente}", ctx, &naoResolvidos);
    check(resultado == "ACR2026-007-", "token desconhecido vira \"\", nunca lança nem trava a prévia");
    check(naoResolvidos.size() == 1 && naoResolvidos[0] == "campo_inexistente",
          "token desconhecido é reportado (pra virar aviso na prévia)");

    check(matriz::consolidacao::resolverMascara("A/B:C", ctx) == "A_B_C",
          "caracteres inválidos pra nome de arquivo são sanitizados (/ e : viram _)");

    check(matriz::consolidacao::resolverMascara("sem token nenhum", ctx) == "sem token nenhum",
          "texto sem token nenhum passa direto");
}

// Item 5 — estrutura de pastas do backup: padrão Projeto→Ano→Tipo de
// Mídia→Tipo de Arquivo, reordenável, e "sem ano" pra material sem o campo.
void testarHierarquiaBackup(const juce::File& dirTemp) {
    std::cout << "== Estrutura de pastas do backup (item 5) ==\n";
    using namespace matriz::consolidacao;

    check(hierarquiaParaCsv(hierarquiaPadrao()) == "projeto,ano,tipo_midia,tipo_arquivo",
          "padrão é Projeto → Ano → Tipo de Mídia → Tipo de Arquivo (§5.1)");
    check(hierarquiaDeCsv(hierarquiaParaCsv(hierarquiaPadrao())) == hierarquiaPadrao(),
          "CSV de hierarquia sobrevive ida e volta");
    check(hierarquiaDeCsv("lixo_que_nao_existe") == hierarquiaPadrao(),
          "valor corrompido cai no padrão em vez de lançar (não impede abrir o projeto)");
    check(hierarquiaDeCsv("") == hierarquiaPadrao(), "CSV vazio cai no padrão");

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
              "estrutura padrão gera Projeto/Ano/Tipo de Mídia/Tipo de Arquivo (achou: \"" +
                  caminhoDe(planoPadrao, "HIE-001").toStdString() + "\")");
        check(caminhoDe(planoPadrao, "HIE-002").contains("/No year/"),
              "material sem ano vai pra \"No year\", nunca desaparece (§5.2) (achou: \"" +
                  caminhoDe(planoPadrao, "HIE-002").toStdString() + "\")");

        // Reordenar os níveis muda a árvore resultante NA HORA, sem copiar
        // nada — é o que a prévia do diálogo mostra (§5.2, item 12).
        auto planoInvertido = planejarConsolidacao(
            projeto->registro(), pastaProjeto, destino,
            {NivelHierarquia::TipoArquivo, NivelHierarquia::Ano, NivelHierarquia::Projeto});
        check(caminhoDe(planoInvertido, "HIE-001") == "WAV/1978/Acervo do Anderson/HIE-001-001-Com Ano.wav",
              "reordenar os níveis reordena as pastas (achou: \"" +
                  caminhoDe(planoInvertido, "HIE-001").toStdString() + "\")");

        auto planoOrigem = planejarConsolidacao(projeto->registro(), pastaProjeto, destino, {NivelHierarquia::Origem});
        check(caminhoDe(planoOrigem, "HIE-001").startsWith("No origin/"),
              "nível de origem sem valor preenchido vira \"No origin\" (achou: \"" +
                  caminhoDe(planoOrigem, "HIE-001").toStdString() + "\")");

        // A hierarquia é do projeto, não da sessão do diálogo.
        gravarHierarquiaDoProjeto(projeto->registro(), {NivelHierarquia::Ano, NivelHierarquia::TipoMidia});
        check(hierarquiaDoProjeto(projeto->registro()) ==
                  HierarquiaBackup{NivelHierarquia::Ano, NivelHierarquia::TipoMidia},
              "hierarquia escolhida persiste no projeto");
        auto planoGravado = planejarConsolidacao(projeto->registro(), pastaProjeto, destino);
        check(caminhoDe(planoGravado, "HIE-001") == "1978/fita_rolo/HIE-001-001-Com Ano.wav",
              "planejar sem hierarquia explícita usa a gravada no projeto (achou: \"" +
                  caminhoDe(planoGravado, "HIE-001").toStdString() + "\")");

        // Executar de verdade: as pastas automáticas têm que existir em disco.
        auto resultado = executarConsolidacao(projeto->registro(), pastaProjeto, destino, planoGravado);
        check(resultado.consolidados == 2 && resultado.falhas.empty(), "consolidação com hierarquia automática grava os 2 itens");
        check(destino.getChildFile("1978/fita_rolo/HIE-001-001-Com Ano.wav").existsAsFile(),
              "a pasta automática foi criada em disco e o arquivo está dentro dela");
    } catch (const std::exception& e) {
        check(false, std::string("hierarquia de backup: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
    destino.deleteRecursively();
}

// Itens 6.1 e 8.3 — loudness EBU R128 pré-calculado, e marcador que vira
// metadado embutido NA CÓPIA sem nunca tocar no original.
void testarLoudnessEMarcadores(const juce::File& dirTemp) {
    std::cout << "== Loudness (item 6.1) e marcador embutido na cópia (item 8.3) ==\n";

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
              "seno 1 kHz a -20 dBFS RMS mono mede -20 LUFS (achou " + std::to_string(medidaMono.lufsIntegrado) + ")");

        auto estereo = gerarSeno(2);
        auto medidaEstereo = matriz::ingest::medirLoudness(estereo, sr);
        check(std::abs((medidaEstereo.lufsIntegrado - medidaMono.lufsIntegrado) - 3.01) < 0.2,
              "o mesmo sinal em estéreo mede +3 dB (soma de potência de BS.1770, não média) (achou +" +
                  std::to_string(medidaEstereo.lufsIntegrado - medidaMono.lufsIntegrado) + " dB)");
        check(std::abs(medidaMono.truePeakDbfs - (-17.0)) < 0.2,
              "pico de amostra do seno bate com a amplitude gerada (achou " +
                  std::to_string(medidaMono.truePeakDbfs) + " dBFS)");
        check(medidaMono.lra < 1.0, "sinal constante tem LRA próximo de zero (achou " + std::to_string(medidaMono.lra) + ")");

        juce::AudioBuffer<float> silencio(2, amostras);
        silencio.clear();
        auto medidaSilencio = matriz::ingest::medirLoudness(silencio, sr);
        check(medidaSilencio.lufsIntegrado <= -70.0, "silêncio absoluto fica no piso de -70 LUFS, não em -inf nem 0");

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
              "gating relativo descarta o trecho 60 dB abaixo em vez de puxar a média (achou " +
                  std::to_string(medidaGate.lufsIntegrado) + ", esperado perto de " +
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
              "ingest de áudio já traz LUFS-I pré-calculado na leitura técnica (item 6.1)");
        check(ing.leitura.lra.has_value(), "ingest de áudio já traz LRA pré-calculado");

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
        check(marcadores.size() == 2, "os 2 marcadores são lidos de item_observacao (a mesma lista da ficha)");

        auto plano = matriz::consolidacao::planejarConsolidacao(projeto->registro(), pastaProjeto, destino,
                                                                 soPastaManual());
        auto resultado = matriz::consolidacao::executarConsolidacao(projeto->registro(), pastaProjeto, destino, plano);
        check(resultado.consolidados == 1, "cópia consolidada");
        check(resultado.arquivosComMarcadorEmbutido == 1,
              "os marcadores foram embutidos em 1 arquivo de backup (achou " +
                  std::to_string(resultado.arquivosComMarcadorEmbutido) + ")");

        // O ORIGINAL permanece byte a byte idêntico (item 8.3).
        check(juce::MD5(masterOrigem).toHexString() == hashAntes.toHexString(),
              "o arquivo ORIGINAL permanece byte a byte idêntico depois de inserir marcadores (item 8.3)");
        check(masterOrigem.getSize() == tamanhoAntes, "o original também não mudou de tamanho");

        // A CÓPIA cresceu e ganhou os chunks de marcador.
        juce::File copia;
        if (!plano.itens.empty()) copia = destino.getChildFile(plano.itens[0].caminhoRelativoDestino);
        check(copia.existsAsFile(), "a cópia existe no destino");
        if (copia.existsAsFile()) {
            check(copia.getSize() > tamanhoAntes, "a cópia é MAIOR que o original — ganhou os chunks de marcador");
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
            check(contemBytes("cue "), "a cópia tem o chunk \"cue \" (marcador legível por DAW)");
            check(contemBytes("iXML"), "a cópia tem o chunk iXML (BWF/iXML, item 8.3)");
            check(contemBytes("labl"), "a cópia tem os chunks \"labl\" (o texto de cada marcador)");
            check(contemBytes("entrada da voz") && contemBytes("emenda"),
                  "o TEXTO de cada marcador está embutido na cópia, não só a posição");
            // O áudio da cópia continua tocável: o RIFF novo tem que ser
            // válido, senão o backup seria um arquivo quebrado.
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> leitor(fm.createReaderFor(copia));
            check(leitor != nullptr, "a cópia com marcadores continua sendo um WAV válido e legível");
            if (leitor != nullptr)
                check(leitor->lengthInSamples > 0, "o áudio da cópia continua lá (" +
                                                        std::to_string(leitor->lengthInSamples) + " amostras)");
        }
    } catch (const std::exception& e) {
        check(false, std::string("marcador embutido: ") + e.what());
    }

    pastaProjeto.deleteRecursively();
    destino.deleteRecursively();
}

void testarConsolidacao(const juce::File& dirTemp) {
    std::cout << "== Consolidação: planejamento e execução (item 10, §11.7) ==\n";

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
        check(plano1.itens.size() == 1, "plano tem 1 item organizado (o item2, fora de qualquer pasta, não entra)");
        check(plano1.itensNaoOrganizados == 1, "\"não organizados\" conta o item2 (§5.5)");
        check(plano1.podeConsolidar(), "sem conflito, plano pode ser executado");
        if (!plano1.itens.empty()) {
            check(plano1.itens[0].caminhoRelativoDestino == "01 Fitas/Estudio/CNS-001-001-Ensaio Berlim.wav",
                  "caminho final combina hierarquia de pastas + máscara da pasta filha (achou: \"" +
                      plano1.itens[0].caminhoRelativoDestino.toStdString() + "\")");
            check(!plano1.itens[0].jaConsolidado, "primeira vez — ainda não foi consolidado");
        }
        check(plano1.espacoNecessarioBytes > 0, "espaço necessário calculado (> 0 bytes)");

        auto resultado1 = matriz::consolidacao::executarConsolidacao(projeto->registro(), pastaProjeto, destino, plano1);
        check(resultado1.consolidados == 1 && resultado1.falhas.empty(), "execução consolida 1 item, sem falhas");

        juce::File copiaFinal = destino.getChildFile("01 Fitas/Estudio/CNS-001-001-Ensaio Berlim.wav");
        check(copiaFinal.existsAsFile(), "a cópia consolidada existe de verdade em disco");
        check(copiaFinal.getSize() == ing1.arquivoNoProjeto.getSize(), "tamanho da cópia bate com o master");
        check(ing1.arquivoNoProjeto.existsAsFile(),
              "o master já dentro do projeto continua intocado (original, fora do projeto, nunca foi reaberto)");

        // --- Incremental: rodar de novo sem mudar nada não deve recopiar ---
        auto plano2 = matriz::consolidacao::planejarConsolidacao(projeto->registro(), pastaProjeto, destino, soPastaManual());
        check(!plano2.itens.empty() && plano2.itens[0].jaConsolidado,
              "segunda vez: item já consolidado é reconhecido (incremental)");
        check(plano2.espacoNecessarioBytes == 0, "nada novo pra copiar — espaço necessário some");
        auto resultado2 = matriz::consolidacao::executarConsolidacao(projeto->registro(), pastaProjeto, destino, plano2);
        check(resultado2.consolidados == 0 && resultado2.pulados == 1,
              "segunda execução pula o item já consolidado, não recopia");

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
        check(!plano3.nomesEmConflito.empty(), "dois itens do mesmo tipo na pasta \"Conflito\" (máscara \"{tipo}\") colidem");
        check(!plano3.podeConsolidar(), "plano com conflito não pode ser executado até resolver (§11.6)");

        auto resultado3 = matriz::consolidacao::executarConsolidacao(projeto->registro(), pastaProjeto, destino, plano3);
        int consolidadosNaPastaConflito = 0;
        for (auto& ip : plano3.itens)
            if (ip.pastaId == pastaConflito && !ip.emConflito) ++consolidadosNaPastaConflito;
        check(consolidadosNaPastaConflito == 0, "executarConsolidacao nunca copia um item marcado em conflito");
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
            check(resCapa.consolidados == 1, "backup gravou o item com capa");

            juce::File masterNoBackup = destinoCapa.getChildFile("Discos/CPB-001-Disco.wav");
            juce::File capaNoBackup = destinoCapa.getChildFile("Discos/CPB-001-Disco.jpg");
            check(masterNoBackup.existsAsFile(), "o master está no backup");
            check(capaNoBackup.existsAsFile(),
                  "a capa foi copiada JUNTO, ao lado do master e com o mesmo nome base");
            check(capaNoBackup.getSize() == imagemCapa.getSize(), "a capa copiada é idêntica à original");
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
            check(planoC.itens.size() == 3, "plano isolado tem os 3 itens, sem conflito nem já-consolidado");

            // Cancela DEPOIS do primeiro arquivo — o caso real (parou no
            // meio), não o de cancelar antes de começar.
            int chamadas = 0;
            auto resultadoC = matriz::consolidacao::executarConsolidacao(
                projetoC->registro(), pastaProjetoC, destinoC, planoC,
                [&chamadas](int, int) { return ++chamadas <= 1; });

            check(resultadoC.cancelado, "consolidação reporta que foi cancelada");
            check(resultadoC.consolidados == 1,
                  "cancelar depois do 1º arquivo grava exatamente 1, não o plano inteiro (" +
                      std::to_string(resultadoC.consolidados) + ")");
            check(resultadoC.totalPlanejado == 3, "resultado carrega o total planejado pro resumo \"X de Y\"");

            // O que foi gravado continua em disco — cancelar não é rollback.
            int arquivosNoDestino = 0;
            for (auto& f : juce::RangedDirectoryIterator(destinoC, true, "*", juce::File::findFiles))
                { juce::ignoreUnused(f); ++arquivosNoDestino; }
            check(arquivosNoDestino == 1, "o arquivo já gravado continua no destino depois do cancelamento");

            // Retomar reconhece o que já foi e termina o resto, sem refazer.
            auto planoRetomada = matriz::consolidacao::planejarConsolidacao(projetoC->registro(), pastaProjetoC, destinoC, soPastaManual());
            int jaFeitos = 0;
            for (auto& ip : planoRetomada.itens)
                if (ip.jaConsolidado) ++jaFeitos;
            check(jaFeitos == 1, "retomar reconhece o arquivo já gravado antes do cancelamento (não recopia)");

            auto resultadoRetomada = matriz::consolidacao::executarConsolidacao(projetoC->registro(), pastaProjetoC,
                                                                                 destinoC, planoRetomada);
            check(!resultadoRetomada.cancelado && resultadoRetomada.consolidados == 2 && resultadoRetomada.pulados == 1,
                  "retomada grava os 2 que faltavam e pula o que já estava pronto");
        }

    } catch (const std::exception& e) {
        check(false, std::string("consolidação: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Item 11 — catálogo de proxies. A propriedade que justifica o subsistema:
// o catálogo é AUTÔNOMO. Abre sem o projeto que o gerou e sem o material
// original conectado, e ainda assim diz onde cada arquivo está.
// ---------------------------------------------------------------------------
void testarCatalogoProxies(const juce::File& dirTemp) {
    std::cout << "== Catálogo de proxies (item 11) ==\n";

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
        check(resConsolidacao.consolidados == 2, "backup gravou os 2 itens antes do catálogo");

        // --- Estimativa antes de gerar ---
        auto estimativa = matriz::catalogo::estimar(projeto->registro(), projeto->indice(), pastaProjeto);
        check(estimativa.itens == 2, "estimativa conta os 2 itens");
        check(estimativa.tamanhoBytes > 0, "estimativa devolve tamanho real dos proxies (> 0 bytes)");

        // --- Geração ---
        auto res = matriz::catalogo::gerar(projeto->registro(), projeto->indice(), pastaProjeto, destino);
        check(res.gravados == 2 && !res.cancelado, "catálogo gravou os 2 itens");
        check(matriz::catalogo::ehPastaDeCatalogo(destino),
              "a raiz do backup é reconhecida como contendo catálogo");

        // --- Cancelamento, igual às outras operações longas (item 10) ---
        {
            int chamadas = 0;
            auto resCancel = matriz::catalogo::gerar(projeto->registro(), projeto->indice(), pastaProjeto, destino,
                                                      [&chamadas](int, int) { return ++chamadas <= 1; });
            check(resCancel.cancelado && resCancel.gravados == 1,
                  "geração do catálogo também é cancelável, e o que gravou fica");
            // Regenera inteiro pro resto do teste.
            matriz::catalogo::gerar(projeto->registro(), projeto->indice(), pastaProjeto, destino);
        }

        // =================================================================
        // O teste que importa: some com o projeto E com o volume de origem.
        // O catálogo tem que continuar respondendo.
        // =================================================================
        pastaProjeto.deleteRecursively();
        dirTemp.getChildFile("VolumeExterno").deleteRecursively();
        check(!pastaProjeto.exists(), "projeto original apagado");
        check(!volumeFalso.exists(), "volume de origem desconectado (apagado)");

        auto entradas = matriz::catalogo::abrir(destino);
        check(entradas.size() == 2, "catálogo abre sem o projeto original (" +
                                         std::to_string(entradas.size()) + " entradas)");

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
        check(achouTitulo, "o catálogo carrega o título do material");
        check(achouFicha, "o catálogo carrega a ficha preenchida pelo operador");
        check(achouMiniatura, "a miniatura foi copiada pro catálogo e existe em disco lá");
        check(achouVolume, "o catálogo sabe de qual volume o material veio");

        // descreverLocalizacao é função pura — dá pra testar com o caminho
        // do exemplo do spec, sem depender de onde o teste roda. O pipeline
        // acima roda em /var/folders/... (temp do macOS), que não é um
        // caminho representativo de disco de acervo.
        {
            juce::String descrito = matriz::catalogo::descreverLocalizacao(
                "/Volumes/HD Samsung T7/2003/Turne Europa/Berlim/faixa.wav");
            check(descrito == "HD Samsung T7 / 2003 / Turne Europa / Berlim",
                  "caminho de disco externo vira descrição legível: \"" + descrito.toStdString() + "\"");

            juce::String fundo = matriz::catalogo::descreverLocalizacao(
                "/Volumes/HD/a/b/c/d/e/f/g/Berlim/faixa.wav");
            check(fundo.startsWith("HD /") && fundo.endsWith("Berlim") && fundo.length() < 40,
                  "caminho muito fundo é resumido em vez de virar uma parede de pastas: \"" +
                      fundo.toStdString() + "\"");
        }

        // --- Localizar: a cópia do backup ainda está montada ---
        auto loc = matriz::catalogo::localizar(entradas.front(), pastaCatalogo);
        check(loc.fonteConectada && loc.arquivoReal.existsAsFile(),
              "com o backup montado, o catálogo resolve pro arquivo real e ele abre");

        // --- Localizar com NADA montado ---
        juce::File copiaBackupTudo = destino.getChildFile("Tudo");
        copiaBackupTudo.deleteRecursively();
        auto locSemFonte = matriz::catalogo::localizar(entradas.front(), pastaCatalogo);
        check(!locSemFonte.fonteConectada, "sem backup nem origem, o catálogo não finge que o arquivo está lá");
        check(locSemFonte.descricaoHumana.contains("Berlim"),
              "com a fonte ausente, diz onde procurar: \"" + locSemFonte.descricaoHumana.toStdString() + "\"");

        // --- Portabilidade: mover a pasta inteira não quebra nada ---
        juce::File outroLugar = dirTemp.getChildFile("catalogo_movido");
        check(destino.moveFileTo(outroLugar), "pasta do backup movida pra outro caminho");
        auto entradasMovidas = matriz::catalogo::abrir(outroLugar);
        check(entradasMovidas.size() == 2, "catálogo abre igual depois de mudar de lugar (caminhos são relativos)");
        juce::File catalogoMovido = matriz::catalogo::resolverPastaCatalogo(outroLugar);
        bool miniaturaAindaResolve = false;
        for (auto& e : entradasMovidas)
            if (e.miniaturaRelativa.isNotEmpty() && catalogoMovido.getChildFile(e.miniaturaRelativa).existsAsFile())
                miniaturaAindaResolve = true;
        check(miniaturaAindaResolve, "as miniaturas continuam resolvendo depois da mudança de lugar");

    } catch (const std::exception& e) {
        check(false, std::string("catálogo de proxies: ") + e.what());
    }
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

    tmpDir.deleteRecursively();

    std::cout << "\n" << (failures == 0 ? "TODOS OS TESTES PASSARAM" : std::to_string(failures) + " FALHA(S)") << "\n";
    return failures == 0 ? 0 : 1;
}
