#include "IngerirArquivosTest.h"
#include "FichasEmbutidas.h"

#include "../Ficha/CatalogoDeFichas.h"
#include "../Model/Project.h"
#include "MainComponent.h"
#include "PainelInconsistenciasComponent.h"
#include "ProjetoAberto.h"
#include "SelecionarTipoMidiaDialogo.h"
#include "ImportarMenu.h"
#include "GeoAcoes.h"
#include "CapaProjeto.h"
#include "DocumentPreviewComponent.h"
#include "JanelaContainerDdp.h"
#include "MosaicoComponent.h"
#include "IntakeWorkspaceComponent.h"
#include "OriginalSourceMedium.h"
#include "../Ingest/LightroomImporter.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>

#ifdef __APPLE__
#include <mach/mach.h>
#elif defined(_WIN32) || defined(_MSC_VER)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif
#include <iostream>

namespace matriz::ui {

namespace {

void gerarComFfmpeg(const juce::StringArray& args) {
    juce::ChildProcess proc;
    if (!proc.start(args, juce::ChildProcess::wantStdOut))
        throw std::runtime_error("could not start ffmpeg");
    proc.readAllProcessOutput();
    proc.waitForProcessToFinish(30000);
    if (proc.getExitCode() != 0) throw std::runtime_error("ffmpeg failed while generating test media");
}

// WAV PCM 16-bit mono minimo, escrito na mao. Gerar 5.000 arquivos com
// ffmpeg mediria a velocidade do ffmpeg, nao a concorrencia do ingest - e
// levaria dezenas de minutos. Estes sao arquivos de audio VALIDOS: o
// ffprobe le, o JUCE decodifica, o pipeline inteiro roda de verdade.
void escreverWavMinimo(const juce::File& destino, int amostras, int semente) {
    juce::MemoryOutputStream out;
    const int taxaAmostragem = 8000;
    const int bytesDados = amostras * 2;

    out.write("RIFF", 4);
    out.writeInt(36 + bytesDados);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    out.writeInt(16);            // tamanho do bloco fmt
    out.writeShort(1);           // PCM
    out.writeShort(1);           // mono
    out.writeInt(taxaAmostragem);
    out.writeInt(taxaAmostragem * 2);  // byte rate
    out.writeShort(2);           // block align
    out.writeShort(16);          // bits por amostra
    out.write("data", 4);
    out.writeInt(bytesDados);

    // Conteudo diferente por arquivo: com bytes iguais, o dedup por SHA-256
    // reconheceria 4.999 duplicatas e o teste mediria outra coisa.
    // A semente entra em CADA amostra, não só na frequência: com um punhado
    // de frequências distintas, arquivos diferentes sairiam byte a byte
    // iguais e o dedup por SHA-256 os reconheceria como duplicata - o teste
    // mediria o caminho errado.
    for (int i = 0; i < amostras; ++i) {
        double fase = i * 2.0 * juce::MathConstants<double>::pi / taxaAmostragem;
        double v = std::sin(fase * (200 + (semente % 977)));
        int ruido = ((semente * 2654435761u) + static_cast<unsigned>(i) * 40503u) % 61;
        out.writeShort(static_cast<short>(v * 8000.0 + ruido - 30));
    }

    destino.replaceWithData(out.getData(), out.getDataSize());
}

// Mede se a interface CONGELOU, do jeito que o operador sente: um Timer na
// message thread que cronometra o atraso do proprio tique.
//
// A primeira versao deste teste cronometrava em volta de
// runDispatchLoopUntil(1) e acusava picos de 18 s enquanto o vigia do
// software (MessageLoopMonitor) media 0,6 s no mesmo lote. As duas nao
// podiam estar certas - e a errada era a minha: envolver uma chamada de
// bombeamento num relogio mede tambem o tempo em que a thread simplesmente
// nao foi ESCALONADA (6 workers de ingest + milhares de subprocessos de
// ffprobe disputando CPU). Nao ser escalonado por um instante nao e a janela
// travada; e o sistema operacional dividindo a maquina.
//
// Atraso de tique mede a coisa certa: se o loop parou de girar, o tique nao
// acontece, e a diferenca ate o proximo E o congelamento.
class MedidorDeTravamento : private juce::Timer {
public:
    explicit MedidorDeTravamento(int intervaloMs) : intervalo_(intervaloMs) {
        ultimo_ = juce::Time::getMillisecondCounterHiRes();
        startTimer(intervaloMs);
    }
    ~MedidorDeTravamento() override { stopTimer(); }

    double piorAtrasoMs() const { return piorAtraso_; }
    int totalDeTiques() const { return tiques_; }
    // Cada travamento acima de 300 ms, com o instante em que aconteceu —
    // "pior travamento" sozinho nao diz se foi no meio do lote ou no fim.
    const std::vector<std::pair<double, double>>& travamentos() const { return travamentos_; }
    void marcarInicio() { inicio_ = juce::Time::getMillisecondCounterHiRes(); }

private:
    void timerCallback() override {
        double agora = juce::Time::getMillisecondCounterHiRes();
        double atraso = (agora - ultimo_) - intervalo_;
        if (tiques_ > 0) {
            piorAtraso_ = juce::jmax(piorAtraso_, atraso);
            if (atraso > 300.0) travamentos_.push_back({(agora - inicio_) / 1000.0, atraso});
        }
        ultimo_ = agora;
        ++tiques_;
    }

    int intervalo_;
    double ultimo_ = 0.0;
    double inicio_ = 0.0;
    double piorAtraso_ = 0.0;
    int tiques_ = 0;
    std::vector<std::pair<double, double>> travamentos_;
};

// Ingest agora roda em background (ThreadPool + callAsync) - o teste precisa
// bombear o loop de mensagens pra que os callbacks assíncronos rodem, já que
// não há ninguém chamando runDispatchLoop de fora neste modo headless.
void esperarIngestTerminar(MainComponent& mainComponent) {
    auto inicio = juce::Time::getMillisecondCounter();
    while (mainComponent.ingestEmAndamento()) {
        if (juce::Time::getMillisecondCounter() - inicio > 30000)
            throw std::runtime_error("timeout esperando o lote de ingest terminar");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    // Mais uma passada pra garantir que o callAsync final (recarregar
    // mosaico + resumo) já foi processado antes de conferir o banco.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}

void testarImportacaoLightroom(const juce::File& tmpRoot, const std::function<void(bool, const juce::String&)>& checar) {
    std::cout << "\n== Lightroom Catalog Import (.lrcat) ==\n";

    juce::File pastaOrigem = tmpRoot.getChildFile("lr_origem");
    pastaOrigem.createDirectory();
    juce::File pastaFotos = pastaOrigem.getChildFile("Fotos");
    pastaFotos.createDirectory();

    // 1. Imagens e arquivos acompanhantes
    juce::File foto1 = pastaFotos.getChildFile("foto1.jpg");
    juce::File foto2 = pastaFotos.getChildFile("foto2.jpg");
    juce::File xmp1 = pastaFotos.getChildFile("foto1.xmp");

    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                     "color=c=green:s=160x120", "-frames:v", "1", foto1.getFullPathName()});
    gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                     "color=c=yellow:s=160x120", "-frames:v", "1", foto2.getFullPathName()});
    xmp1.replaceWithText("<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF></rdf:RDF></x:xmpmeta>");

    // Pasta .lrdata que DEVE ser ignorada
    juce::File pastaLrdata = pastaOrigem.getChildFile("CatalogoTeste Previews.lrdata");
    pastaLrdata.createDirectory();
    pastaLrdata.getChildFile("previews.db").replaceWithText("dummy preview data");

    // 2. Criar banco .lrcat sintético
    juce::File lrcatFile = pastaOrigem.getChildFile("CatalogoTeste.lrcat");
    if (lrcatFile.existsAsFile()) lrcatFile.deleteFile();

    {
        matriz::db::Database db(lrcatFile.getFullPathName().toStdString());
        db.execScript(
            "CREATE TABLE AgLibraryRootFolder ("
            "  id_local INTEGER PRIMARY KEY, "
            "  absolutePath TEXT, "
            "  name TEXT"
            ");"
            "CREATE TABLE AgLibraryFolder ("
            "  id_local INTEGER PRIMARY KEY, "
            "  rootFolder INTEGER, "
            "  pathFromRoot TEXT"
            ");"
            "CREATE TABLE AgLibraryFile ("
            "  id_local INTEGER PRIMARY KEY, "
            "  folder INTEGER, "
            "  idxInFolder INTEGER, "
            "  baseName TEXT, "
            "  extension TEXT, "
            "  sidecarExtensions TEXT"
            ");"
            "CREATE TABLE Adobe_images ("
            "  id_local INTEGER PRIMARY KEY, "
            "  rootFile INTEGER, "
            "  captureTime TEXT, "
            "  rating REAL, "
            "  colorLabels TEXT, "
            "  pick REAL, "
            "  masterImage INTEGER, "
            "  touchCount INTEGER"
            ");"
            "CREATE TABLE AgHarvestedExifMetadata ("
            "  id_local INTEGER PRIMARY KEY, "
            "  image INTEGER, "
            "  focalLength REAL, "
            "  aperture REAL, "
            "  shutterSpeed REAL, "
            "  isoSpeedRating REAL, "
            "  hasGPS INTEGER, "
            "  gpsLatitude REAL, "
            "  gpsLongitude REAL, "
            "  gpsAltitude REAL, "
            "  cameraModelRef INTEGER, "
            "  lensRef INTEGER"
            ");"
            "CREATE TABLE AgInternedExifCameraModel ("
            "  id_local INTEGER PRIMARY KEY, "
            "  value TEXT"
            ");"
            "CREATE TABLE AgInternedExifLens ("
            "  id_local INTEGER PRIMARY KEY, "
            "  value TEXT"
            ");"
            "CREATE TABLE AgHarvestedIptcMetadata ("
            "  id_local INTEGER PRIMARY KEY, "
            "  image INTEGER, "
            "  creatorRef INTEGER"
            ");"
            "CREATE TABLE AgInternedIptcCreator ("
            "  id_local INTEGER PRIMARY KEY, "
            "  value TEXT"
            ");"
            "CREATE TABLE AgLibraryIPTC ("
            "  id_local INTEGER PRIMARY KEY, "
            "  image INTEGER, "
            "  caption TEXT, "
            "  copyright TEXT, "
            "  title TEXT"
            ");"
            "CREATE TABLE AgLibraryKeyword ("
            "  id_local INTEGER PRIMARY KEY, "
            "  name TEXT"
            ");"
            "CREATE TABLE AgLibraryKeywordImage ("
            "  id_local INTEGER PRIMARY KEY, "
            "  tag INTEGER, "
            "  image INTEGER"
            ");"
            "CREATE TABLE AgLibraryCollection ("
            "  id_local INTEGER PRIMARY KEY, "
            "  name TEXT"
            ");"
            "CREATE TABLE AgLibraryCollectionImage ("
            "  id_local INTEGER PRIMARY KEY, "
            "  collection INTEGER, "
            "  image INTEGER"
            ");"
        );

        using V = matriz::db::Value;

        // Raiz e pasta
        juce::String rootPath = pastaOrigem.getFullPathName();
        if (!rootPath.endsWithChar('/')) rootPath << "/";
        db.run("INSERT INTO AgLibraryRootFolder (id_local, absolutePath, name) VALUES (1, ?, 'lr_origem')",
               {V::of(rootPath.toStdString())});
        db.run("INSERT INTO AgLibraryFolder (id_local, rootFolder, pathFromRoot) VALUES (1, 1, 'Fotos/')", {});

        // Arquivos
        db.run("INSERT INTO AgLibraryFile (id_local, folder, idxInFolder, baseName, extension, sidecarExtensions) "
               "VALUES (1, 1, 1, 'foto1', 'jpg', 'xmp')", {});
        db.run("INSERT INTO AgLibraryFile (id_local, folder, idxInFolder, baseName, extension, sidecarExtensions) "
               "VALUES (2, 1, 2, 'foto2', 'jpg', '')", {});

        // Câmera, lente, criador
        db.run("INSERT INTO AgInternedExifCameraModel (id_local, value) VALUES (1, 'Nikon Z6')", {});
        db.run("INSERT INTO AgInternedExifLens (id_local, value) VALUES (1, 'NIKKOR Z 50mm f/1.8 S')", {});
        db.run("INSERT INTO AgInternedIptcCreator (id_local, value) VALUES (1, 'Anderson Fotografo')", {});

        // Adobe_images
        db.run("INSERT INTO Adobe_images (id_local, rootFile, captureTime, rating, colorLabels, pick, masterImage, touchCount) "
               "VALUES (1, 1, '2025-11-20T14:30:00', 5.0, 'blue', 1.0, NULL, 0)", {});
        db.run("INSERT INTO Adobe_images (id_local, rootFile, captureTime, rating, colorLabels, pick, masterImage, touchCount) "
               "VALUES (2, 2, '2025-11-21T09:15:00', 3.0, 'red', 0.0, NULL, 0)", {});

        // IPTC
        db.run("INSERT INTO AgLibraryIPTC (id_local, image, caption, copyright, title) "
               "VALUES (1, 1, 'Retrato no parque', '(C) 2025 Anderson', 'Retrato Outono')", {});
        db.run("INSERT INTO AgHarvestedIptcMetadata (id_local, image, creatorRef) VALUES (1, 1, 1)", {});

        // EXIF
        db.run("INSERT INTO AgHarvestedExifMetadata (id_local, image, focalLength, aperture, shutterSpeed, isoSpeedRating, hasGPS, gpsLatitude, gpsLongitude, gpsAltitude, cameraModelRef, lensRef) "
               "VALUES (1, 1, 50.0, 1.8, 0.005, 400.0, 1, -23.5505, -46.6333, 760.0, 1, 1)", {});

        // Keywords
        db.run("INSERT INTO AgLibraryKeyword (id_local, name) VALUES (1, 'Retrato')", {});
        db.run("INSERT INTO AgLibraryKeyword (id_local, name) VALUES (2, 'Natureza')", {});
        db.run("INSERT INTO AgLibraryKeywordImage (id_local, tag, image) VALUES (1, 1, 1)", {});
        db.run("INSERT INTO AgLibraryKeywordImage (id_local, tag, image) VALUES (2, 2, 1)", {});

        // Collection
        db.run("INSERT INTO AgLibraryCollection (id_local, name) VALUES (1, 'Portfolio 2025')", {});
        db.run("INSERT INTO AgLibraryCollectionImage (id_local, collection, image) VALUES (1, 1, 1)", {});
    }

    // 3. Criar projeto de teste e executar importação
    matriz::model::NovoProjetoParams paramsLr;
    paramsLr.nome = "Projeto Teste Lightroom";
    paramsLr.prefixoNomenclatura = "LRTEST";
    auto projetoLr = matriz::model::Project::criar(tmpRoot.getChildFile("projeto_lr"), paramsLr);

    auto resultado = matriz::ingest::LightroomImporter::importarCatalogo(
        lrcatFile,
        projetoLr->registro(),
        projetoLr->indice(),
        projetoLr->pasta(),
        projetoLr->projetoId(),
        "LRTEST",
        nullptr,
        nullptr,
        nullptr);

    checar(resultado.sucesso, "LightroomImporter reports success: " + resultado.erro);
    checar(resultado.fotosImportadas == 2, "imported exactly 2 photos (got " + juce::String(resultado.fotosImportadas) + ")");
    checar(resultado.fotosNaoEncontradas == 0, "zero missing photos");
    checar(resultado.arquivosSessaoImportados >= 1, "session file (.lrcat) imported as session item (" + juce::String(resultado.arquivosSessaoImportados) + ")");

    auto& reg = projetoLr->registro();

    // 4. Conferir fotos importadas e metadados no banco
    {
        auto stmtFoto1 = reg.prepare(
            "SELECT i.id, i.titulo, i.tipo_midia, i.codigo_acervo "
            "FROM item i JOIN arquivo a ON a.item_id = i.id WHERE a.caminho_absoluto_origem LIKE '%foto1.jpg'");
        bool achouFoto1 = stmtFoto1.step();
        checar(achouFoto1, "foto1 found in database via joined arquivo");
        if (achouFoto1) {
            std::string item1Id = stmtFoto1.columnText(0);
            juce::String titulo = juce::String::fromUTF8(stmtFoto1.columnText(1).c_str());
            juce::String tipoMidia = juce::String::fromUTF8(stmtFoto1.columnText(2).c_str());
            checar(tipoMidia == "foto", "foto1 imported with tipo_midia='foto'");
            checar(titulo == "Retrato Outono", "foto1 title set from catalog IPTC title ('Retrato Outono')");

            auto sCampos = reg.prepare(
                "SELECT campo_id, valor FROM item_campo "
                "WHERE item_id = ? AND nivel = 'raiz'");
            sCampos.bind(1, matriz::db::Value::of(item1Id));
            std::map<std::string, std::string> campos;
            while (sCampos.step()) {
                campos[sCampos.columnText(0)] = sCampos.columnText(1);
            }

            checar(campos["dc_title"] == "Retrato Outono", "dc_title mapped correctly ('Retrato Outono')");
            checar(campos["dc_description"] == "Retrato no parque", "dc_description mapped correctly ('Retrato no parque')");
            checar(campos["dc_creator"] == "Anderson Fotografo", "dc_creator mapped correctly ('Anderson Fotografo')");
            checar(campos["dc_rights"] == "(C) 2025 Anderson", "dc_rights mapped correctly");
            checar(campos["dc_subject"].find("Retrato") != std::string::npos &&
                   campos["dc_subject"].find("Natureza") != std::string::npos,
                   "dc_subject contains keywords ('Retrato', 'Natureza')");
            checar(campos["user_rating"] == "5", "user_rating mapped (5 stars)");
            checar(campos["color_label"] == "blue", "color_label mapped ('blue')");
            checar(campos["pick_status"] == "pick", "pick_status mapped ('pick')");
            checar(campos["collection_type"] == "Portfolio 2025", "collection_type mapped ('Portfolio 2025')");

            // Notas técnicas com câmera, lente, abertura, velocidade, ISO
            auto sNotas = reg.prepare("SELECT notas_livres FROM item WHERE id = ?");
            sNotas.bind(1, matriz::db::Value::of(item1Id));
            sNotas.step();
            juce::String notas = juce::String::fromUTF8(sNotas.columnText(0).c_str());
            checar(notas.contains("Nikon Z6") && notas.contains("NIKKOR Z 50mm") &&
                   notas.contains("50.0mm") && notas.contains("400"),
                   "technical details appended to notas_livres (" + notas.replace("\n", " | ") + ")");

            // Geolocalização
            auto sGeo = reg.prepare(
                "SELECT latitude, longitude FROM asset_geolocation "
                "WHERE asset_id = ?");
            sGeo.bind(1, matriz::db::Value::of(item1Id));
            bool achouGeo = sGeo.step();
            checar(achouGeo, "geolocation entry exists for foto1 in asset_geolocation");
            if (achouGeo) {
                double lat = sGeo.columnReal(0);
                double lon = sGeo.columnReal(1);
                checar(std::abs(lat - (-23.5505)) < 0.001 && std::abs(lon - (-46.6333)) < 0.001,
                       "coordinates match catalog GPS (-23.5505, -46.6333)");
            }
        }
    }

    // 5. Conferir arquivo de sessão (.lrcat)
    {
        auto sSessao = reg.prepare("SELECT COUNT(*) FROM item WHERE tipo_midia = 'sessao'");
        sSessao.step();
        checar(sSessao.columnInt(0) >= 1, "session item created in registro.sqlite");

        juce::File pastaSessoes = projetoLr->pastaMedia().getChildFile("Sessões");
        if (!pastaSessoes.isDirectory()) pastaSessoes = projetoLr->pasta().getChildFile("Sessões");
        juce::File lrcatCopiado = pastaSessoes.getChildFile("CatalogoTeste").getChildFile("CatalogoTeste.lrcat");
        checar(lrcatCopiado.existsAsFile(), "CatalogoTeste.lrcat copied to project session folder: " + lrcatCopiado.getFullPathName());
    }

    // 6. Conferir que .lrdata foi totalmente ignorado
    {
        auto sLrdata = reg.prepare(
            "SELECT COUNT(*) FROM item WHERE titulo LIKE '%lrdata%' OR titulo LIKE '%previews%'");
        sLrdata.step();
        checar(sLrdata.columnInt(0) == 0, ".lrdata previews folder was completely ignored (0 items in DB)");
    }
}

} // namespace

int rodarTestIngerirArquivos() {
    std::cout << "== UI -> ingest bridge (MainComponent::ingerirArquivos) ==\n";
    int falhas = 0;
    auto checar = [&](bool condicao, const juce::String& descricao) {
        std::cout << (condicao ? "  OK   " : "  FAIL ") << descricao << "\n";
        if (!condicao) ++falhas;
    };

    juce::File tmpRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_ingerir_ui_test_" + juce::Uuid().toDashedString());

    try {
        juce::File audio = tmpRoot.getChildFile("tom.wav");
        juce::File imagem = tmpRoot.getChildFile("foto.jpg");
        tmpRoot.createDirectory();
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                         "sine=frequency=440:duration=1", audio.getFullPathName()});
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                         "color=c=red:s=320x240", "-frames:v", "1", imagem.getFullPathName()});

        matriz::model::NovoProjetoParams params;
        params.nome = "Teste ingest UI";
        params.prefixoNomenclatura = "UITST";
        auto projeto = matriz::model::Project::criar(tmpRoot.getChildFile("projeto"), params);
        matriz::db::Database& registro = projeto->registro();

        MainComponent mainComponent;
        mainComponent.aoConcluirLoteIngestParaTeste = [](int, const juce::StringArray&) {};
        mainComponent.abrirProjeto(std::move(projeto));
        checar(mainComponent.temProjetoAberto(), "MainComponent abriu o projeto de teste");

        mainComponent.ingerirArquivos({audio});
        esperarIngestTerminar(mainComponent);
        mainComponent.ingerirArquivos({imagem});
        esperarIngestTerminar(mainComponent);

        auto stmtContagem = registro.prepare("SELECT COUNT(*) FROM item");
        stmtContagem.step();
        checar(stmtContagem.columnInt(0) == 2, "2 items were created (audio + image)");

        // Nenhum tipo de mídia escolhido no ingest (Reorientação completa
        // §2.1/§7.1): o item aparece com tipo_midia NULL - classificar é
        // trabalho de depois, por cima do que já está na grade.
        auto stmtAudio = registro.prepare(
            "SELECT tipo_midia, estado FROM item WHERE titulo = 'tom'");
        bool achouAudio = stmtAudio.step();
        checar(achouAudio && stmtAudio.columnIsNull(0) && stmtAudio.columnText(1) == "novo",
               "the audio item comes in with tipo_midia=NULL and state 'novo' - ingested, not yet classified");

        auto stmtArquivoAudio = registro.prepare(
            "SELECT a.papel, a.eh_master, a.checksum_sha256, a.caracteristicas_tecnicas_json "
            "FROM arquivo a JOIN item i ON i.id = a.item_id WHERE i.titulo = 'tom'");
        bool achouArquivoAudio = stmtArquivoAudio.step();
        checar(achouArquivoAudio && stmtArquivoAudio.columnText(0) == "preservation_master" &&
                   stmtArquivoAudio.columnInt(1) == 1,
               "the audio file is ingested as preservation_master/master");
        checar(achouArquivoAudio && stmtArquivoAudio.columnText(2).length() == 64,
               "checksum SHA-256 real foi calculado (64 hex chars)");
        checar(achouArquivoAudio && stmtArquivoAudio.columnText(3).find("duracaoSegundos") != std::string::npos,
               "the technical read (ffprobe) filled caracteristicas_tecnicas_json");

        auto stmtImagem = registro.prepare("SELECT tipo_midia FROM item WHERE titulo = 'foto'");
        bool achouImagem = stmtImagem.step();
        checar(achouImagem && stmtImagem.columnIsNull(0), "the image item also comes in with tipo_midia=NULL");

        // Ingerir de novo não deve duplicar itens indefinidamente nem quebrar
        // (cada chamada cria itens novos por design atual - confirma que ao
        // menos não lança e o mosaico continua consistente).
        mainComponent.ingerirArquivos({audio});
        esperarIngestTerminar(mainComponent);
        auto stmtContagem2 = registro.prepare("SELECT COUNT(*) FROM item");
        stmtContagem2.step();
        checar(stmtContagem2.columnInt(0) == 3, "ingerir de novo soma mais um item (3 no total), sem travar");

        // Ingest de pasta: arrasta a pasta, não os arquivos dentro dela —
        // tem que expandir recursivamente (era o bug #2 reportado: pasta
        // inteira era ignorada silenciosamente).
        juce::File pastaFotos = tmpRoot.getChildFile("fotos_da_caixa");
        pastaFotos.createDirectory();
        juce::File subpasta = pastaFotos.getChildFile("lote_1");
        subpasta.createDirectory();
        gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                         "color=c=blue:s=320x240", "-frames:v", "1", subpasta.getChildFile("capa.jpg").getFullPathName()});

        mainComponent.ingerirArquivos({pastaFotos});
        esperarIngestTerminar(mainComponent);
        auto stmtContagem3 = registro.prepare("SELECT COUNT(*) FROM item");
        stmtContagem3.step();
        checar(stmtContagem3.columnInt(0) == 4,
               "ingerir uma pasta expande recursivamente e ingere o arquivo de dentro (4 no total)");

        // ===============================================================
        // Item 10 - cancelar operação longa.
        //
        // O que precisa valer, independente de QUANTOS arquivos deram
        // tempo de ser processados antes do clique (o teste não controla o
        // escalonador): o lote termina de verdade, o que foi processado
        // continua válido, e não sobra item-fantasma - a fase 1 insere
        // TODOS os itens antes de processar, então cancelar sem limpar
        // deixaria linhas com código de acervo e nenhum arquivo atrás.
        // ===============================================================
        {
            juce::File pastaLote = tmpRoot.getChildFile("lote_grande");
            pastaLote.createDirectory();
            constexpr int kQuantidade = 40;
            juce::Array<juce::File> muitos;
            for (int i = 0; i < kQuantidade; ++i) {
                juce::File copia = pastaLote.getChildFile("copia_" + juce::String(i) + ".wav");
                audio.copyFileTo(copia);
                muitos.add(copia);
            }

            auto stmtAntes = registro.prepare("SELECT COUNT(*) FROM item");
            stmtAntes.step();
            int itensAntes = stmtAntes.columnInt(0);

            mainComponent.ingerirArquivos(muitos);
            // Deixa alguns arquivos passarem antes de cancelar, pra exercitar
            // o caso real (lote em curso), não o de cancelar antes de começar.
            juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
            mainComponent.cancelarLoteIngest();
            esperarIngestTerminar(mainComponent);

            checar(!mainComponent.ingestEmAndamento(),
                   "lote cancelado TERMINA - pendentes zera em vez de ficar preso 'em andamento'");

            auto stmtDepois = registro.prepare("SELECT COUNT(*) FROM item");
            stmtDepois.step();
            int itensDepois = stmtDepois.columnInt(0);
            checar(itensDepois < itensAntes + kQuantidade,
                   "cancelar interrompeu de fato (entraram menos que os " + juce::String(kQuantidade) +
                       " arquivos do lote: " + juce::String(itensDepois - itensAntes) + ")");
            checar(itensDepois >= itensAntes,
                   "cancelling did NOT revert what already existed nor what had already been processed");

            // A invariante que importa: nenhum item sem arquivo atrás.
            // 'duplicata' fica de fora porque item duplicado NÃO tem linha
            // em `arquivo` por design (item 9 - "um asset, muitas
            // localizações": conteúdo já conhecido é reconhecido, não
            // copiado de novo). As 40 cópias do mesmo .wav caem todas aí.
            auto stmtFantasmas = registro.prepare(
                "SELECT COUNT(*) FROM item i WHERE i.estado <> 'duplicata' "
                "AND NOT EXISTS (SELECT 1 FROM arquivo a WHERE a.item_id = i.id)");
            stmtFantasmas.step();
            checar(stmtFantasmas.columnInt(0) == 0,
                   "nenhum item-fantasma sobrou: todo item no projeto tem arquivo de verdade");

            checar(mainComponent.textoProgressoIngestParaTeste().containsIgnoreCase("ancel") ||
                   mainComponent.textoProgressoIngestParaTeste().containsIgnoreCase("interromp") ||
                   mainComponent.textoProgressoIngestParaTeste().containsIgnoreCase("stopped"),
                   "the banner reports a cancellation, not a success summary");
        }

        // ===============================================================
        // Ingestão Resiliente: Cancelar mantendo arquivos + Re-arrastar pasta
        // ===============================================================
        {
            juce::File pastaBackup = tmpRoot.getChildFile("backup_resiliente");
            pastaBackup.createDirectory();
            for (int i = 0; i < 10; ++i) {
                juce::File f = pastaBackup.getChildFile("backup_" + juce::String(i).paddedLeft('0', 2) + ".wav");
                escreverWavMinimo(f, 400, 60000 + i);
            }

            mainComponent.ingerirArquivos({pastaBackup});
            juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
            mainComponent.cancelarLoteIngest(true); // Manter arquivos já importados
            esperarIngestTerminar(mainComponent);

            auto stmtKept = registro.prepare("SELECT COUNT(*) FROM item WHERE em_quarentena = 1");
            stmtKept.step();
            int mantidos = stmtKept.columnInt(0);
            checar(mantidos > 0, "cancelar com 'manter' preservou os arquivos ja carregados no Intake (" + juce::String(mantidos) + " mantidos)");

            // Adiciona mais 5 arquivos na mesma pasta
            for (int i = 10; i < 15; ++i) {
                juce::File f = pastaBackup.getChildFile("backup_" + juce::String(i).paddedLeft('0', 2) + ".wav");
                escreverWavMinimo(f, 400, 60000 + i);
            }

            // Arrasta a mesma pasta de novo! Deve ignorar os ja existentes no Intake e ingerir apenas os restantes
            mainComponent.ingerirArquivos({pastaBackup});
            esperarIngestTerminar(mainComponent);

            auto stmtFinal = registro.prepare("SELECT COUNT(*) FROM item WHERE em_quarentena = 1");
            stmtFinal.step();
            int totalFinal = stmtFinal.columnInt(0);
            checar(totalFinal == 15, "re-arrastar a pasta ignorou os do Intake e importou os restantes (esperado 15, obtido: " + juce::String(totalFinal) + ")");
        }


        mainComponent.fecharProjeto();
        checar(!mainComponent.temProjetoAberto(), "fecharProjeto() limpa o estado corretamente");

        // Restrição de tipos de mídia por modo (Parte 1 da correção de
        // fluxo, §1.2/§1.3): Archive oferece todos os tipos, Catalog só o
        // musicalmente relevante - nunca fita_rolo aparecendo como se
        // fosse o único tipo de áudio possível (era exatamente o bug #4
        // original, só que agora do lado da restrição por modo).
        matriz::model::NovoProjetoParams paramsCatalog;
        paramsCatalog.nome = "Teste catálogo";
        paramsCatalog.modo = matriz::model::Modo::Catalogo;
        paramsCatalog.prefixoNomenclatura = "CAT";
        auto projetoCatalog = matriz::model::Project::criar(tmpRoot.getChildFile("projeto_catalog"), paramsCatalog);
        ProjetoAberto abertoCatalog(std::move(projetoCatalog));
        auto tiposCatalog = listarTiposMidiaDisponiveis(abertoCatalog);
        auto temTipo = [&](const std::vector<TipoMidiaOpcao>& tipos, const std::string& id) {
            return std::any_of(tipos.begin(), tipos.end(), [&](const TipoMidiaOpcao& o) { return o.id == id; });
        };
        checar(temTipo(tiposCatalog, "release") && temTipo(tiposCatalog, "sample") &&
                   temTipo(tiposCatalog, "fita_rolo") && temTipo(tiposCatalog, "vinil"),
               "catalog mode offers release/sample/reel tape/vinyl");
        checar(!temTipo(tiposCatalog, "documento") && !temTipo(tiposCatalog, "cd") && !temTipo(tiposCatalog, "foto"),
               "catalog mode does NOT offer irrelevant types (document/cd/photo)");

        // Painel de inconsistências com posição fixa no modo Catalog (§1.3):
        // ingere um release sem capa e confirma que o painel existe e já
        // detectou a falta (release_sem_capa) assim que o projeto abre —
        // "nunca escondido em menu".
        matriz::model::NovoProjetoParams paramsPainel;
        paramsPainel.nome = "Teste painel";
        paramsPainel.modo = matriz::model::Modo::Catalogo;
        paramsPainel.prefixoNomenclatura = "PNL";
        auto projetoPainel = matriz::model::Project::criar(tmpRoot.getChildFile("projeto_painel"), paramsPainel);
        MainComponent mainComponentCatalog;
        mainComponentCatalog.aoConcluirLoteIngestParaTeste = [](int, const juce::StringArray&) {};
        mainComponentCatalog.abrirProjeto(std::move(projetoPainel));
        checar(mainComponentCatalog.temPainelInconsistencias(),
               "catalog mode shows the inconsistency panel in the centre, even with no items yet");

        // Classificar por tipo de mídia (§7.4) ainda não existe na UI —
        // fora do ponto de parada desta etapa (item 6 em diante). Simula
        // aqui só pra confirmar que o motor de detecção (Source/Ingest/
        // PainelInconsistencias.cpp) continua reagindo assim que um item
        // tem tipo_midia='release' sem capa, não importa como ele chegou
        // lá - o item entra sempre com tipo_midia NULL agora (§7.1).
        mainComponentCatalog.ingerirArquivos({audio});
        esperarIngestTerminar(mainComponentCatalog);
        matriz::db::Database& registroPainel = mainComponentCatalog.projetoAberto()->projeto().registro();
        registroPainel.run("UPDATE item SET tipo_midia = 'release'", {});

        PainelInconsistenciasComponent painelDireto(*mainComponentCatalog.projetoAberto());
        painelDireto.recarregarSincrono();
        checar(painelDireto.totalInconsistencias() > 0,
               "a release with no cover is detected as soon as the item is classified (" +
                   std::to_string(painelDireto.totalInconsistencias()) + " inconsistency(ies))");

        matriz::model::NovoProjetoParams paramsPainelArchive;
        paramsPainelArchive.nome = "Teste painel archive";
        paramsPainelArchive.modo = matriz::model::Modo::Preservacao;
        paramsPainelArchive.prefixoNomenclatura = "PLA";
        auto projetoPainelArchive =
            matriz::model::Project::criar(tmpRoot.getChildFile("projeto_painel_archive"), paramsPainelArchive);
        MainComponent mainComponentArchive;
        mainComponentArchive.abrirProjeto(std::move(projetoPainelArchive));
        checar(!mainComponentArchive.temPainelInconsistencias(),
               "archive mode has no fixed slot for the inconsistency panel at this stage");

        matriz::model::NovoProjetoParams paramsArchive;
        paramsArchive.nome = "Teste acervo";
        paramsArchive.modo = matriz::model::Modo::Preservacao;
        paramsArchive.prefixoNomenclatura = "ARC";
        auto projetoArchive = matriz::model::Project::criar(tmpRoot.getChildFile("projeto_archive"), paramsArchive);
        ProjetoAberto abertoArchive(std::move(projetoArchive));
        // Numero exato NAO fica travado no teste: os tipos sao DESCOBERTOS
        // em fichas/*.yaml (§6.1) - acrescentar um YAML nao pode virar teste
        // vermelho. O que importa e que Archive oferece TODOS e Catalog nao.
        auto tiposArchive = listarTiposMidiaDisponiveis(abertoArchive);
        auto todosOsTiposEmDisco = matriz::ficha::listarTodosOsTipos(matriz::ui::fichasEmbutidas());
        checar(tiposArchive.size() == todosOsTiposEmDisco.size(),
               "archive mode offers every type discovered in fichas/ (" +
                   juce::String(static_cast<int>(tiposArchive.size())).toStdString() + " of " +
                   juce::String(static_cast<int>(todosOsTiposEmDisco.size())).toStdString() + ")");
        checar(tiposCatalog.size() < tiposArchive.size(),
               "catalog mode offers a narrower list than archive");

        testarImportacaoLightroom(tmpRoot, checar);

    } catch (const std::exception& e) {
        checar(false, juce::String("teste da ponte de ingest: ") + e.what());
    }

    // =======================================================================
    // Criterio 3 - 5.000 arquivos de uma vez, sem congelar a UI, com
    // progresso e cancelamento inteiros.
    //
    // A pergunta nao e "quanto tempo leva" (depende do disco): e se a MESSAGE
    // THREAD continua respondendo enquanto o lote roda. Medimos a latencia de
    // cada volta do loop de mensagens durante o lote - e isso que o operador
    // sente como travamento.
    // =======================================================================
    std::cout << "\n== Criterion 3: batch of 5,000 files ==\n";

    juce::File loteRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("matriz_lote5k_" + juce::Uuid().toDashedString());
    try {
        loteRoot.createDirectory();
        juce::File pastaFontes = loteRoot.getChildFile("fontes");
        pastaFontes.createDirectory();

        // Arquivos pequenos e sinteticos: o alvo do teste e a concorrencia e a
        // responsividade, nao a velocidade de decodificar midia real (isso ja
        // tem cobertura propria no matriz_ingest_selftest).
        constexpr int kTotalArquivos = 5000;
        juce::Array<juce::File> arquivos;
        for (int i = 0; i < kTotalArquivos; ++i) {
            juce::File f = pastaFontes.getChildFile("asset_" + juce::String(i).paddedLeft('0', 5) + ".wav");
            escreverWavMinimo(f, 800, i);
            arquivos.add(f);
        }
        checar(pastaFontes.getNumberOfChildFiles(juce::File::findFiles) == kTotalArquivos,
               "5,000 synthetic files created");

        matriz::model::NovoProjetoParams params;
        params.nome = "Lote 5k";
        params.modo = matriz::model::Modo::Preservacao;
        params.prefixoNomenclatura = "L5K";
        auto projeto = matriz::model::Project::criar(loteRoot.getChildFile("projeto"), params);

        MainComponent janela;
        janela.setBounds(0, 0, 1280, 800);
        janela.abrirProjeto(std::move(projeto));
        janela.aoConcluirLoteIngestParaTeste = [](int, const juce::StringArray&) {};

        auto inicioLote = juce::Time::getMillisecondCounter();
        double inicioLoteHiRes = juce::Time::getMillisecondCounterHiRes();
        janela.ingerirArquivos(arquivos);

        // Os itens tem que estar VISIVEIS na grade antes do processamento
        // pesado terminar (§2.1) - e a promessa que faz o operador nao achar
        // que o programa ignorou o drop.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        int visiveisCedo = static_cast<int>(janela.projetoAberto()->listarItens().size());
        checar(visiveisCedo == kTotalArquivos,
               "all 5,000 items show up in the grid before processing finishes (" +
                   juce::String(visiveisCedo) + ")");

        // Congelamento da message thread durante o lote.
        //
        // Medimos do jeito que um CLIQUE sente: postamos um callAsync e
        // cronometramos quanto tempo ele leva pra ser entregue. E exatamente
        // o caminho que o handler de um botao percorre.
        //
        // Duas tentativas anteriores mediram outra coisa e me levaram a
        // conclusoes erradas, entao vale registrar:
        //  - cronometrar em volta de runDispatchLoopUntil() media tambem o
        //    tempo em que a thread nao foi ESCALONADA (6 workers de ingest +
        //    milhares de subprocessos de ffmpeg competindo);
        //  - medir atraso de juce::Timer acusou travamentos de 20 s enquanto
        //    o proprio loop girava 3.160 vezes no mesmo periodo. Timer do
        //    JUCE depende de uma thread interna que tambem disputa CPU; num
        //    laco de bombeamento apertado como este, ele deixa de disparar
        //    sem que a interface esteja travada.
        //
        // Latencia de entrega de mensagem nao tem esses dois problemas: se o
        // callAsync demora, a interface DEMOROU.
        auto marcaEnvio = std::make_shared<double>(0.0);
        auto piorEntrega = std::make_shared<double>(0.0);
        auto entregaPendente = std::make_shared<bool>(false);
        std::vector<std::pair<double, double>> travamentos;

        int voltas = 0;
        size_t picoRamBytes = 0;
        double ultimaSonda = 0.0;

        while (janela.ingestEmAndamento()) {
            if (juce::Time::getMillisecondCounter() - inicioLote > 600000) break;  // teto de sanidade

            double agora = juce::Time::getMillisecondCounterHiRes();
            if (!*entregaPendente && agora - ultimaSonda > 200.0) {
                ultimaSonda = agora;
                *marcaEnvio = agora;
                *entregaPendente = true;
                double t0Lote = (agora - inicioLoteHiRes) / 1000.0;
                juce::MessageManager::callAsync([marcaEnvio, piorEntrega, entregaPendente, t0Lote,
                                                  &travamentos]() {
                    double atraso = juce::Time::getMillisecondCounterHiRes() - *marcaEnvio;
                    *piorEntrega = juce::jmax(*piorEntrega, atraso);
                    if (atraso > 300.0) travamentos.push_back({t0Lote, atraso});
                    *entregaPendente = false;
                });
            }

            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
            if ((voltas % 50) == 0) {
#ifdef __APPLE__
                struct task_basic_info info;
                mach_msg_type_number_t contagem = TASK_BASIC_INFO_COUNT;
                if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &contagem) == KERN_SUCCESS)
                    picoRamBytes = juce::jmax(picoRamBytes, (size_t)info.resident_size);
#elif defined(_WIN32)
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
                    picoRamBytes = juce::jmax(picoRamBytes, (size_t)pmc.WorkingSetSize);
#endif
            }
            ++voltas;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        double duracaoLote = (juce::Time::getMillisecondCounter() - inicioLote) / 1000.0;
        double piorLatencia = *piorEntrega;

        for (const auto& [quando, quanto] : travamentos)
            std::cout << "  ..  stall of " << (int)quanto << "ms at t+" << juce::String(quando, 1).toStdString()
                      << "s\n";
        std::cout << "  ..  peak RSS during batch: " << (int)(picoRamBytes / (1024 * 1024)) << " MB\n";
        std::cout << "  ..  batch of " << kTotalArquivos << " finished in " << (int)duracaoLote
                  << "s, " << voltas << " loop turns, worst message delivery latency "
                  << juce::String(piorLatencia, 1).toStdString() << "ms\n";

        checar(piorLatencia < 1000.0,
               "the message thread was never stuck for more than 1s during the batch (worst: " +
                   juce::String(piorLatencia, 1) + "ms)");
        checar(voltas > 100,
               "the message loop kept turning throughout the batch, not only at the end (" +
                   juce::String(voltas) + " turns)");

        {
            auto stmt = janela.projetoAberto()->projeto().registro().prepare(
                "SELECT COUNT(*) FROM item WHERE codigo_acervo IS NOT NULL");
            stmt.step();
            int comCodigo = static_cast<int>(stmt.columnInt(0));
            checar(comCodigo == kTotalArquivos,
                   "all 5,000 got an archive code after a successful ingest (" +
                       juce::String(comCodigo) + ")");
        }
        {
            // Criterio 4: nenhum dotfile nem arquivo de tamanho zero entrou.
            //
            // A checagem e no NOME do arquivo, feita aqui em C++, e nao com
            // LIKE no SQL: em LIKE, "_" e curinga de um caractere, entao um
            // padrao como '%/._%' casa com qualquer "/x" - inclusive com o
            // "/.." de um caminho. Passou a reportar falso positivo em todo
            // arquivo do lote.
            auto stmt = janela.projetoAberto()->projeto().registro().prepare(
                "SELECT caminho_relativo, IFNULL(tamanho_bytes, -1) FROM arquivo");
            juce::StringArray ofensores;
            while (stmt.step()) {
                juce::String caminho = stmt.columnText(0);
                juce::String nome = juce::File::createFileWithoutCheckingPath(caminho).getFileName();
                juce::int64 tamanho = stmt.columnInt(1);
                if (nome.startsWith(".") || tamanho <= 0)
                    ofensores.add(caminho + " (" + juce::String(tamanho) + " bytes)");
            }
            checar(ofensores.isEmpty(),
                   "no dotfiles and no zero-byte files in the catalog (criterion 4)" +
                       (ofensores.isEmpty() ? juce::String()
                                            : " - found " + juce::String(ofensores.size()) + ": " +
                                                  ofensores.strings[0]));

            // I5 + §8: o caminho relativo tem que ser DE VERDADE relativo a
            // raiz do Vault. Um "../.." aqui quebraria a comparacao de
            // caminhos da reconciliacao.
            auto stmtRel = janela.projetoAberto()->projeto().registro().prepare(
                "SELECT caminho_relativo FROM arquivo "
                "WHERE caminho_relativo LIKE '..%' OR caminho_relativo LIKE '/%' LIMIT 3");
            juce::StringArray relativosRuins;
            while (stmtRel.step()) relativosRuins.add(juce::String(stmtRel.columnText(0)));
            checar(relativosRuins.isEmpty(),
                   juce::String("every caminho_relativo really is relative to the Vault root") +
                       (relativosRuins.isEmpty() ? juce::String()
                                                 : " - found: " + relativosRuins.joinIntoString("; ")));
        }

        // Cancelamento integro no meio de um lote novo: o que ja foi
        // processado continua valido, e o lote termina de verdade.
        juce::Array<juce::File> segundoLote;
        for (int i = 0; i < 800; ++i) {
            juce::File f = pastaFontes.getChildFile("cancel_" + juce::String(i).paddedLeft('0', 4) + ".wav");
            escreverWavMinimo(f, 800, 100000 + i);
            segundoLote.add(f);
        }
        janela.ingerirArquivos(segundoLote);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        janela.cancelarLoteIngest();

        auto inicioCancel = juce::Time::getMillisecondCounter();
        while (janela.ingestEmAndamento()) {
            if (juce::Time::getMillisecondCounter() - inicioCancel > 120000) break;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
        checar(!janela.ingestEmAndamento(), "cancelling really ends the batch - nothing is left hanging");

        {
            auto stmt = janela.projetoAberto()->projeto().registro().prepare(
                "SELECT COUNT(*) FROM item WHERE codigo_acervo IS NOT NULL");
            stmt.step();
            checar(stmt.columnInt(0) >= kTotalArquivos,
                   "whatever was already processed stays valid after cancelling");
        }
    } catch (const std::exception& e) {
        checar(false, juce::String("batch of 5,000: ") + e.what());
    }
    loteRoot.deleteRecursively();

    // Item 2 (freeze no fim do ingest): lote B entra DURANTE a finalização do
    // lote A — como PASTA, pra passar pelo modal de scan, que antes
    // sobrescrevia ingestModalDialog_ e deixava o modal de A órfão e aberto
    // pra sempre ("560 of 586"). Os dois lotes têm que ser finalizados e
    // nenhum modal pode sobrar.
    {
        std::cout << "\n== Batch B arriving while batch A is finalizing ==\n";
        juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_lote_sobreposto_" + juce::Uuid().toDashedString());
        try {
            raiz.createDirectory();
            juce::Array<juce::File> loteA;
            for (int i = 0; i < 20; ++i) {
                juce::File f = raiz.getChildFile("a").getChildFile("a_" + juce::String(i) + ".wav");
                f.getParentDirectory().createDirectory();
                escreverWavMinimo(f, 400, 200000 + i);
                loteA.add(f);
            }
            juce::File pastaB = raiz.getChildFile("b");
            pastaB.createDirectory();
            for (int i = 0; i < 300; ++i)
                escreverWavMinimo(pastaB.getChildFile("b_" + juce::String(i) + ".wav"), 400, 300000 + i);

            matriz::model::NovoProjetoParams params;
            params.nome = "Lotes sobrepostos";
            params.prefixoNomenclatura = "SOB";
            auto projeto = matriz::model::Project::criar(raiz.getChildFile("projeto"), params);

            MainComponent janela;
            janela.setBounds(0, 0, 1280, 800);
            janela.abrirProjeto(std::move(projeto));
            // Blocos anteriores podem deixar modais (ex.: cancelamento): conta só os deste.
            const int modaisAntes = juce::ModalComponentManager::getInstance()->getNumModalComponents();
            auto finalizacoes = std::make_shared<int>(0);
            janela.aoConcluirLoteIngestParaTeste = [finalizacoes](int, const juce::StringArray&) { ++*finalizacoes; };

            janela.ingerirArquivos(loteA);
            auto t0 = juce::Time::getMillisecondCounter();
            while (*finalizacoes < 1 && juce::Time::getMillisecondCounter() - t0 < 120000)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
            checar(*finalizacoes >= 1, "batch A reached finalization");

            janela.ingerirArquivos({pastaB});  // A ainda finalizando
            t0 = juce::Time::getMillisecondCounter();
            while (janela.ingestEmAndamento() && juce::Time::getMillisecondCounter() - t0 < 300000)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(500);

            checar(!janela.ingestEmAndamento(), "both batches ended (nothing left hanging)");
            checar(*finalizacoes >= 2, "batch B was finalized too (" + juce::String(*finalizacoes) + " finalizations)");
            auto stmt = janela.projetoAberto()->projeto().registro().prepare(
                "SELECT COUNT(*) FROM item WHERE codigo_acervo IS NOT NULL");
            stmt.step();
            checar(stmt.columnInt(0) == 320,
                   "all 320 items got an archive code (" + juce::String(stmt.columnInt(0)) + ")");
            int modais = juce::ModalComponentManager::getInstance()->getNumModalComponents();
            checar(modais <= modaisAntes, "no ingest modal left open by these batches (" + juce::String(modais) +
                                              " modal(s), " + juce::String(modaisAntes) + " before)");
        } catch (const std::exception& e) {
            checar(false, juce::String("overlapping batches: ") + e.what());
        }
        raiz.deleteRecursively();
    }

    tmpRoot.deleteRecursively();

    std::cout << "\n" << (falhas == 0 ? "ALL TESTS PASSED" : juce::String(falhas) + " FAILURE(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

int rodarTestLightroom() {
    std::cout << "== Lightroom Ingest Self-Test ==\n";
    int falhas = 0;
    auto checar = [&](bool condicao, const juce::String& descricao) {
        std::cout << (condicao ? "  OK   " : "  FAIL ") << descricao << "\n";
        if (!condicao) ++falhas;
    };

    juce::File tmpRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_lr_test_" + juce::Uuid().toDashedString());
    try {
        testarImportacaoLightroom(tmpRoot, checar);
    } catch (const std::exception& e) {
        checar(false, juce::String("teste lightroom: ") + e.what());
    }
    tmpRoot.deleteRecursively();

    std::cout << "\n" << (falhas == 0 ? "ALL TESTS PASSED" : juce::String(falhas) + " FAILURE(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

// Janela IMPORT: o contexto do card chega aos itens (CONTENT, source medium, tags, sufixo) sem tocar no resto.
int rodarTestImportarComContexto() {
    std::cout << "== IMPORT window context (ingerirArquivos com SobreMaterial) ==\n";
    int falhas = 0;
    auto checar = [&](bool condicao, const juce::String& descricao) {
        std::cout << (condicao ? "  OK   " : "  FAIL ") << descricao << "\n";
        if (!condicao) ++falhas;
    };

    juce::File tmpRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_importar_ctx_test_" + juce::Uuid().toDashedString());
    try {
        tmpRoot.createDirectory();
        const auto gerarTom = [&](const char* nome) {
            juce::File f = tmpRoot.getChildFile(nome);
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                             "sine=frequency=440:duration=1", f.getFullPathName()});
            return f;
        };
        const juce::File samples = gerarTom("samples.wav"), fita = gerarTom("fita.wav"), rede = gerarTom("rede.wav"),
                         sufixo = gerarTom("sufixo.wav"), neutro = gerarTom("neutro.wav"), local = gerarTom("local.wav");

        matriz::model::NovoProjetoParams params;
        params.nome = "Teste IMPORT contexto";
        params.prefixoNomenclatura = "CTXTST";
        auto projeto = matriz::model::Project::criar(tmpRoot.getChildFile("projeto"), params);
        matriz::db::Database& registro = projeto->registro();

        MainComponent mainComponent;
        mainComponent.aoConcluirLoteIngestParaTeste = [](int, const juce::StringArray&) {};
        mainComponent.abrirProjeto(std::move(projeto));
        checar(mainComponent.temProjetoAberto(), "MainComponent abriu o projeto de teste");

        const auto ingerir = [&](const juce::File& f, std::shared_ptr<const matriz::ingest::SobreMaterial> sobre) {
            mainComponent.ingerirArquivos({f}, true, std::move(sobre));
            esperarIngestTerminar(mainComponent);
        };
        const auto coluna = [&](const char* titulo, const char* col) -> std::string {
            auto st = registro.prepare(std::string("SELECT ") + col + " FROM item WHERE titulo LIKE ?");
            st.bind(1, matriz::db::Value::of(std::string(titulo) + "%"));
            return st.step() && !st.columnIsNull(0) ? st.columnText(0) : std::string();
        };

        ingerir(samples, contextoDaFolhaImportar("audio.production.samples"));
        checar(coluna("samples", "collection_type") == "Sample Pack", "audio.production.samples grava CONTENT = Sample Pack");

        // Vídeo: o formato de origem vem da ficha (campo "source_media"), somado ao que o card já implica.
        {
            auto daFicha = std::make_shared<matriz::ingest::SobreMaterial>();
            OriginalSourceMediumInfo info;
            info.medium = "VHS";
            daFicha->campos.push_back({ "source_media", info.serialize() });
            ingerir(fita, mesclarContextosImportar(contextoDaFolhaImportar("videos.digital"), daFicha));
            checar(coluna("fita", "source_media").find("VHS") != std::string::npos, "ficha de video grava SOURCE MEDIUM = VHS (JSON em item.source_media)");
        }

        ingerir(rede, contextoDaFolhaImportar("other.social.instagram"));
        {
            auto st = registro.prepare("SELECT COUNT(*) FROM item_tag t JOIN item i ON i.id = t.item_id WHERE i.titulo = 'rede' "
                                       "AND t.tag IN ('Social media', 'Instagram')");
            st.step();
            checar(st.columnInt(0) == 2, "other.social.instagram soma as tags 'Social media' e 'Instagram'");
            auto fts = registro.prepare("SELECT COUNT(*) FROM busca_fts f JOIN item i ON i.id = f.item_id WHERE i.titulo = 'rede' AND f.conteudo = 'Instagram'");
            fts.step();
            auto mapa = registro.prepare("SELECT COUNT(*) FROM busca_fts_map m JOIN item i ON i.id = m.item_id WHERE i.titulo = 'rede' AND m.conteudo = 'Instagram'");
            mapa.step();
            checar(fts.columnInt(0) == 1 && mapa.columnInt(0) == 1, "a tag entra UMA vez em busca_fts, com a linha correspondente em busca_fts_map");
        }

        auto comSufixo = std::make_shared<matriz::ingest::SobreMaterial>();
        comSufixo->sufixoTitulo = "2026";
        ingerir(sufixo, comSufixo);
        checar(coluna("sufixo", "titulo") == "sufixo - 2026", "sufixo do titulo e somado uma vez");

        // Pasta inteira (expansão assíncrona + checagem de duplicatas antes do lote): o contexto tem que sobreviver à cadeia toda.
        {
            juce::File pasta = tmpRoot.getChildFile("caixa_de_discos");
            pasta.createDirectory();
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=330:duration=1",
                             pasta.getChildFile("lado_a.wav").getFullPathName()});
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=550:duration=1",
                             pasta.getChildFile("lado_b.wav").getFullPathName()});
            mainComponent.ingerirArquivos({pasta}, true, contextoDaFolhaImportar("audio.preservation.vinyl"));
            const auto t0 = juce::Time::getMillisecondCounter();
            int comTag = 0;
            while (juce::Time::getMillisecondCounter() - t0 < 30000) {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                if (mainComponent.ingestEmAndamento()) continue;
                auto st = registro.prepare("SELECT COUNT(*) FROM item_tag t JOIN item i ON i.id = t.item_id "
                                           "WHERE i.titulo LIKE 'lado_%' AND t.tag = 'Vinyl transcription'");
                st.step();
                comTag = static_cast<int>(st.columnInt(0));
                if (comTag == 2) break;
            }
            esperarIngestTerminar(mainComponent);
            checar(comTag == 2, "pasta inteira: os 2 arquivos de dentro recebem a tag 'Vinyl transcription' do card (" + juce::String(comTag) + ")");
        }

        // Localização padrão da ficha de fotos: entra como USER_CITY quando o arquivo não tem geolocalização própria.
        {
            auto comGeo = std::make_shared<matriz::ingest::SobreMaterial>();
            comGeo->geoPadrao.cidade = "Rio de Janeiro";
            comGeo->geoPadrao.pais = "Brazil";
            ingerir(local, comGeo);
            auto st = registro.prepare("SELECT g.city, g.country, g.source FROM asset_geolocation g JOIN item i ON i.id = g.asset_id WHERE i.titulo = 'local'");
            const bool achou = st.step();
            checar(achou && st.columnText(0) == "Rio de Janeiro" && st.columnText(1) == "Brazil" && st.columnText(2) == "USER_CITY",
                   "localizacao padrao da ficha entra em asset_geolocation (USER_CITY)");
        }

        // DDP master: conjunto que entra junto vira CONTAINER ("ddp") no Intake e o contexto marca cada arquivo.
        {
            const auto ctxDdp = contextoDaFolhaImportar("audio.preservation.ddp");
            checar(ctxDdp != nullptr && ctxDdp->containerOrigem == "ddp", "audio.preservation.ddp pede container 'ddp'");
            juce::File pasta = tmpRoot.getChildFile("master_ddp");
            pasta.createDirectory();
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=220:duration=1",
                             pasta.getChildFile("faixa_um.wav").getFullPathName()});
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=260:duration=1",
                             pasta.getChildFile("faixa_dois.wav").getFullPathName()});
            mainComponent.ingerirArquivos({pasta}, true, ctxDdp);
            const auto t0 = juce::Time::getMillisecondCounter();
            int containers = 0;
            while (juce::Time::getMillisecondCounter() - t0 < 30000) {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                if (mainComponent.ingestEmAndamento()) continue;
                auto st = registro.prepare("SELECT COUNT(*) FROM nest WHERE origem = 'ddp'");
                st.step();
                containers = static_cast<int>(st.columnInt(0));
                if (containers == 1) break;
            }
            esperarIngestTerminar(mainComponent);
            checar(containers == 1, "pasta DDP vira 1 container 'ddp' no Intake (" + juce::String(containers) + ")");
            auto membros = registro.prepare("SELECT COUNT(*) FROM nest_item ni JOIN nest n ON n.id = ni.nest_id WHERE n.origem = 'ddp'");
            membros.step();
            checar(membros.columnInt(0) == 2, "o container 'ddp' tem os 2 arquivos");
        }

        // Capa do container (vinil): a imagem escolhida na ficha vira a capa do nest e fica em .miniaturas do projeto.
        {
            juce::File pasta = tmpRoot.getChildFile("lado_a_vinil");
            pasta.createDirectory();
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=300:duration=1",
                             pasta.getChildFile("faixa_1.wav").getFullPathName()});
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=360:duration=1",
                             pasta.getChildFile("faixa_2.wav").getFullPathName()});
            juce::File capa = tmpRoot.getChildFile("capa_vinil.png");
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=orange:s=800x800", "-frames:v", "1",
                             capa.getFullPathName()});
            const std::map<std::string, std::string> respostasVelhas = { { "artist", "Banda Velha" }, { "album", "Disco Velho" } };
            auto ctxVinil = std::make_shared<matriz::ingest::SobreMaterial>(
                *mesclarContextosImportar(contextoDaFolhaImportar("audio.preservation.vinyl"), contextoDeRespostasImportar("audio.preservation.vinyl", respostasVelhas)));
            ctxVinil->capaContainer = capa.getFullPathName().toStdString();
            checar(ctxVinil->containerOrigem == "vinyl", "audio.preservation.vinyl pede container 'vinyl'");
            mainComponent.ingerirArquivos({pasta}, true, ctxVinil);
            const auto t0 = juce::Time::getMillisecondCounter();
            std::string capaGravada;
            while (juce::Time::getMillisecondCounter() - t0 < 30000) {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                if (mainComponent.ingestEmAndamento()) continue;
                auto st = registro.prepare("SELECT capa_imagem FROM nest WHERE origem = 'vinyl' AND capa_imagem IS NOT NULL LIMIT 1");
                if (st.step()) { capaGravada = st.columnText(0); break; }
            }
            esperarIngestTerminar(mainComponent);
            const juce::File arquivoCapa = mainComponent.projetoAberto()->projeto().pasta().getChildFile(juce::String(capaGravada));
            checar(!capaGravada.empty() && arquivoCapa.existsAsFile() && arquivoCapa.getSize() > 0,
                   "capa do container 'vinyl' gravada em .miniaturas e registrada no nest (" + juce::String(capaGravada) + ")");
            // A miniatura do Intake para o item-capa do container é a imagem escolhida, não a forma de onda.
            auto capaItem = registro.prepare("SELECT capa_item_id FROM nest WHERE origem = 'vinyl' AND capa_imagem IS NOT NULL LIMIT 1");
            const bool achouCapaItem = capaItem.step();
            checar(achouCapaItem && mainComponent.projetoAberto()->capaImagemDoContainer(capaItem.columnText(0)).has_value(),
                   "capaImagemDoContainer devolve a imagem para o item-capa");

            // Editar o questionário inicial: as respostas ficam guardadas no container e trocá-las tira a resposta antiga dos arquivos.
            {
                auto ninho = registro.prepare("SELECT id FROM nest WHERE origem = 'vinyl' AND capa_imagem IS NOT NULL LIMIT 1");
                const bool achou = ninho.step();
                const std::string nestId = achou ? ninho.columnText(0) : std::string();
                auto* proj = mainComponent.projetoAberto();
                std::string folha;
                std::map<std::string, std::string> guardadas;
                checar(achou && questionarioDeJson(proj->questionarioDoContainer(nestId), folha, guardadas) && folha == "audio.preservation.vinyl" &&
                           guardadas["artist"] == "Banda Velha",
                       "o container guarda o questionário inicial (card + respostas)");
                const auto assuntoDosMembros = [&] {
                    std::string todos;
                    for (const auto& id : proj->membrosDoNest(nestId)) todos += "[" + proj->lerMetadado(id, "dc_subject").value_or("") + "]";
                    return todos;
                };
                checar(assuntoDosMembros().find("Banda Velha") != std::string::npos, "antes de editar: SUBJECT dos arquivos tem a resposta antiga");
                const std::map<std::string, std::string> novas = { { "artist", "Banda Nova" }, { "album", "Disco Novo" } };
                const auto antigo = contextoDeRespostasImportar(folha, guardadas);
                const auto novo = contextoDeRespostasImportar(folha, novas);
                const bool ok = antigo && novo && proj->reaplicarQuestionarioDoContainer(nestId, *antigo, *novo, questionarioParaJson(*novo));
                const auto depois = assuntoDosMembros();
                checar(ok && depois.find("Banda Nova") != std::string::npos && depois.find("Banda Velha") == std::string::npos,
                       "editar o questionário troca a resposta antiga pela nova em todos os arquivos (" + juce::String(depois) + ")");
                {
                    auto n = registro.prepare("SELECT nome FROM nest WHERE id = ?");
                    n.bind(1, matriz::db::Value::of(nestId));
                    const bool tem = n.step();
                    checar(tem && n.columnText(0) == "Disco Novo", "o álbum da ficha vira o nome do container (nest.nome) e a LIST dos arquivos");
                    const auto itens = proj->membrosDoNest(nestId);
                    checar(!itens.empty() && proj->lerMetadado(itens.front(), "lista").value_or("") == "Disco Novo", "LIST dos arquivos = álbum");
                }
                // Janela do projeto: o bloco da capa mostra o nome do container e o artista; trocar e remover a capa funcionam.
                {
                    CapaProjetoComponent cp(*proj, nestId, CapaProjetoComponent::Modo::Detalhado);
                    cp.setSize(900, 330);
                    checar(cp.nomeParaTeste() == "Disco Novo" && cp.artistaParaTeste() == "Banda Nova" && cp.temCapaParaTeste(),
                           "PROJECT cover block shows the container name, the artist and the cover");
                    cp.removerCapaParaTeste();
                    auto semCapa = registro.prepare("SELECT capa_imagem FROM nest WHERE id = ?");
                    semCapa.bind(1, matriz::db::Value::of(nestId));
                    checar(semCapa.step() && semCapa.columnIsNull(0) && !cp.temCapaParaTeste(), "Remove clears the container cover");
                    cp.trocarCapaParaTeste(capa);
                    const auto t0 = juce::Time::getMillisecondCounter();
                    while (!cp.temCapaParaTeste() && juce::Time::getMillisecondCounter() - t0 < 10000)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                    checar(cp.temCapaParaTeste(), "Change cover sets a new cover on the container");
                    std::string folhaC;
                    std::map<std::string, std::string> respC;
                    checar(questionarioDeJson(proj->questionarioDoContainer(nestId), folhaC, respC) && respC["capa"] == capa.getFullPathName().toStdString(),
                           "Edit Project Info opens with the cover that was changed outside it");
                }
                std::string folha2;
                std::map<std::string, std::string> guardadas2;
                checar(questionarioDeJson(proj->questionarioDoContainer(nestId), folha2, guardadas2) && guardadas2["artist"] == "Banda Nova",
                       "o questionário guardado passa a ser o novo");
            }
        }

        // Edit Project Info de ponta a ponta, pelo caminho real: ficha preenchida na tela -> importação -> container -> reabrir a ficha.
        {
            juce::File pasta2 = tmpRoot.getChildFile("lado_b_vinil");
            pasta2.createDirectory();
            for (int i = 1; i <= 2; ++i)
                gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=" + std::to_string(200 + i * 40) + ":duration=1",
                                 pasta2.getChildFile("b_" + juce::String(i) + ".wav").getFullPathName()});
            const auto girar = [](int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };
            std::shared_ptr<const matriz::ingest::SobreMaterial> daFicha;
            PainelOverlay ficha;
            ficha.setBounds(0, 0, 1100, 760);
            mostrarFichaImportar(ficha, "audio.preservation.vinyl", [&](bool, std::shared_ptr<const matriz::ingest::SobreMaterial> sm) { daFicha = sm; });
            girar(50);
            const juce::String ladoEscolhido = ficha.valorDoCampo("side").isEmpty() ? juce::String() : juce::String();
            juce::ignoreUnused(ladoEscolhido);
            ficha.definirValorCampo("artist", "Artista Real");
            ficha.definirValorCampo("album", "Album Real");
            ficha.definirValorCampo("context", "Notas do disco");
            // Escolha: o 2º rótulo da lista de Lado (qualquer idioma) tem que voltar igual ao reabrir.
            ficha.selecionarOpcaoDoCampoParaTeste("side", 2);
            const juce::String ladoFicha = ficha.valorDoCampo("side");
            ficha.concluirExterno(1);
            girar(50);
            checar(daFicha != nullptr && daFicha->folhaId == "audio.preservation.vinyl" && daFicha->respostas.count("album") && daFicha->respostas.at("album") == "Album Real",
                   "the ficha result carries the card id and the answers (for Edit Project Info)");
            if (daFicha != nullptr) {
                auto contexto = mesclarContextosImportar(contextoDaFolhaImportar("audio.preservation.vinyl"), daFicha);
                mainComponent.ingerirArquivos({pasta2}, true, contexto);
                const auto t0 = juce::Time::getMillisecondCounter();
                std::string nestReal;
                while (juce::Time::getMillisecondCounter() - t0 < 30000) {
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                    if (mainComponent.ingestEmAndamento()) continue;
                    auto st = registro.prepare("SELECT id FROM nest WHERE origem = 'vinyl' AND nome = 'Album Real' LIMIT 1");
                    if (st.step()) { nestReal = st.columnText(0); break; }
                }
                esperarIngestTerminar(mainComponent);
                checar(!nestReal.empty(), "a container made from the real ficha path is named after the album");
                if (!nestReal.empty()) {
                    PainelOverlay reaberta;
                    reaberta.setBounds(0, 0, 1100, 760);
                    editarQuestionarioDoContainer(reaberta, *mainComponent.projetoAberto(), nestReal, "vinyl", [](bool) {});
                    girar(80);
                    checar(reaberta.valorDoCampo("artist") == "Artista Real" && reaberta.valorDoCampo("album") == "Album Real" &&
                               reaberta.valorDoCampo("context") == "Notas do disco",
                           "Edit Project Info opens with the text answers filled in");
                    checar(ladoFicha.isNotEmpty() && reaberta.valorDoCampo("side") == ladoFicha, "...and with the choice answers filled in (" + reaberta.valorDoCampo("side") + ")");
                }
            }
        }

        // YouTube Production: vídeo + thumbnail + roteiro entram juntos e viram UM container 'youtube'. A thumbnail é a capa, o vídeo
        // representa o container e só ele recebe título e descrição (Dublin Core).
        {
            juce::File dir = tmpRoot.getChildFile("youtube_arquivos");
            dir.createDirectory();
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc=size=640x360:rate=10:duration=4",
                             "-pix_fmt", "yuv420p", dir.getChildFile("video_final.mp4").getFullPathName()});
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=purple:s=640x360", "-frames:v", "1",
                             dir.getChildFile("thumb.png").getFullPathName()});
            dir.getChildFile("roteiro.txt").replaceWithText("roteiro do video");
            auto ctxYt = std::make_shared<matriz::ingest::SobreMaterial>(*contextoDaFolhaImportar("videos.youtube"));
            checar(ctxYt->containerOrigem == "youtube" && ctxYt->capaFrameDoVideo, "videos.youtube pede container 'youtube' com o video como representante");
            ctxYt->capaContainer = dir.getChildFile("thumb.png").getFullPathName().toStdString();
            ctxYt->camposDoVideo = { { "dc_title", "Meu vlog" }, { "dc_description", "Descricao do vlog" } };
            mainComponent.ingerirArquivos({ dir.getChildFile("video_final.mp4"), dir.getChildFile("thumb.png"), dir.getChildFile("roteiro.txt") }, true, ctxYt);
            const auto t0 = juce::Time::getMillisecondCounter();
            int containers = 0;
            std::string capa, itemCapa;
            while (juce::Time::getMillisecondCounter() - t0 < 40000) {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                if (mainComponent.ingestEmAndamento()) continue;
                auto st = registro.prepare("SELECT COUNT(*), COALESCE(MAX(capa_imagem), ''), COALESCE(MAX(capa_item_id), '') FROM nest WHERE origem = 'youtube'");
                st.step();
                containers = static_cast<int>(st.columnInt(0));
                capa = st.columnText(1);
                itemCapa = st.columnText(2);
                if (containers == 1 && !capa.empty() && !itemCapa.empty()) {
                    auto v = registro.prepare("SELECT tipo_midia FROM item WHERE id = ?");
                    v.bind(1, matriz::db::Value::of(itemCapa));
                    if (v.step() && v.columnText(0) == "digital_video") break;
                }
            }
            esperarIngestTerminar(mainComponent);
            checar(containers == 1, "video + thumbnail + roteiro viram 1 container 'youtube' (" + juce::String(containers) + ")");
            checar(!capa.empty() && mainComponent.projetoAberto()->projeto().pasta().getChildFile(juce::String(capa)).existsAsFile(), "a thumbnail vira a capa do container");
            auto v = registro.prepare("SELECT tipo_midia FROM item WHERE id = ?");
            v.bind(1, matriz::db::Value::of(itemCapa));
            checar(v.step() && v.columnText(0) == "digital_video", "o video e o item que representa o container (abre o preview dele)");
            auto d = registro.prepare("SELECT dc_title, dc_description FROM item WHERE titulo = 'video_final'");
            checar(d.step() && d.columnText(0) == "Meu vlog" && d.columnText(1) == "Descricao do vlog", "titulo e descricao entram na ficha Dublin Core do video");
            auto n = registro.prepare("SELECT COUNT(*) FROM item WHERE titulo IN ('thumb', 'roteiro') AND dc_description IS NOT NULL AND dc_description <> ''");
            n.step();
            checar(n.columnInt(0) == 0, "a thumbnail e o roteiro nao recebem a descricao do video");
        }

        // NEST (e não container): arquivos com tema em comum (sample pack) ficam num nest sem origem.
        {
            const auto ctxSamples = contextoDaFolhaImportar("audio.production.samples");
            checar(ctxSamples != nullptr && ctxSamples->agruparComoNest && ctxSamples->containerOrigem.empty(), "sample pack pede NEST, nao container");
            juce::File pasta = tmpRoot.getChildFile("pacote_de_samples");
            pasta.createDirectory();
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=100:duration=1", pasta.getChildFile("kick.wav").getFullPathName()});
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=200:duration=1", pasta.getChildFile("snare.wav").getFullPathName()});
            mainComponent.ingerirArquivos({pasta}, true, ctxSamples);
            const auto t0 = juce::Time::getMillisecondCounter();
            int nests = 0;
            while (juce::Time::getMillisecondCounter() - t0 < 30000) {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                if (mainComponent.ingestEmAndamento()) continue;
                auto st = registro.prepare("SELECT COUNT(*) FROM nest WHERE origem IS NULL");
                st.step();
                nests = static_cast<int>(st.columnInt(0));
                if (nests == 1) break;
            }
            esperarIngestTerminar(mainComponent);
            checar(nests == 1, "sample pack importado junto vira 1 NEST sem origem (" + juce::String(nests) + ")");
            auto membros = registro.prepare("SELECT COUNT(*) FROM nest_item ni JOIN nest n ON n.id = ni.nest_id WHERE n.origem IS NULL");
            membros.step();
            checar(membros.columnInt(0) == 2, "o nest tem os 2 arquivos");
        }

        // Music Production: a DAW da ficha vira o ícone do container (sem capa); com capa, vale a capa.
        {
            const auto base = contextoDaFolhaImportar("audio.production.project");
            checar(base != nullptr && base->containerOrigem == "production", "audio.production.project pede container 'production'");
            const auto comDaw = std::make_shared<matriz::ingest::SobreMaterial>(*base);
            comDaw->iconeContainer = "ci_daw_pro_tools_png";
            juce::File pasta = tmpRoot.getChildFile("sessao_pro_tools");
            pasta.createDirectory();
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=150:duration=1", pasta.getChildFile("stem_1.wav").getFullPathName()});
            gerarComFfmpeg({"ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=170:duration=1", pasta.getChildFile("stem_2.wav").getFullPathName()});
            mainComponent.ingerirArquivos({pasta}, true, comDaw);
            const auto t0 = juce::Time::getMillisecondCounter();
            std::string icone;
            while (juce::Time::getMillisecondCounter() - t0 < 30000) {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                if (mainComponent.ingestEmAndamento()) continue;
                auto st = registro.prepare("SELECT icone FROM nest WHERE origem = 'production' AND icone IS NOT NULL LIMIT 1");
                if (st.step()) { icone = st.columnText(0); break; }
            }
            esperarIngestTerminar(mainComponent);
            checar(icone == "ci_daw_pro_tools_png", "a DAW escolhida vira o icone do container de producao (" + juce::String(icone) + ")");
        }

        // GRID: o container aprovado do INTAKE continua container (ícone/capa e contagem), não vira um NEST com "N".
        {
            registro.run("UPDATE item SET em_quarentena = 0", {});
            MosaicoComponent mosaico(*mainComponent.projetoAberto());
            mosaico.setBounds(0, 0, 1300, 800);
            mosaico.recarregarSincrono();
            const auto t0 = juce::Time::getMillisecondCounter();
            while (juce::Time::getMillisecondCounter() - t0 < 3000) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            mosaico.createComponentSnapshot(mosaico.getLocalBounds());
            const auto t1 = juce::Time::getMillisecondCounter();
            while (juce::Time::getMillisecondCounter() - t1 < 4000) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            juce::Image img = mosaico.createComponentSnapshot(mosaico.getLocalBounds());
            juce::File png = juce::File::getCurrentWorkingDirectory().getChildFile("test-output/grid_containers.png");
            png.getParentDirectory().createDirectory();
            png.deleteFile();
            if (auto out = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) juce::PNGImageFormat().writeImageToStream(img, *out);
            std::cout << "  ..  foto do Grid: " << png.getFullPathName() << "\n";
            registro.run("UPDATE item SET em_quarentena = 1", {});  // volta ao estado do INTAKE para o resto do teste
        }

        // Janela do container DDP: lista de arquivos à esquerda, player de CD no meio, metadados à direita. Sem um DDP de verdade aqui,
        // usa uma pasta sintética (DDPID + IMAGE.DAT de silêncio) só para conferir o desenho e a lista.
        {
            juce::File ddp = tmpRoot.getChildFile("ddp_sintetico");
            ddp.createDirectory();
            ddp.getChildFile("DDPID").replaceWithText("DDP 2.00");
            {
                juce::MemoryBlock silencio(588 * 75 * 6);  // 6 s de CD (588 amostras estéreo por quadro, 4 bytes cada = 2352 bytes)
                silencio.setSize(2352 * 75 * 6, true);
                ddp.getChildFile("IMAGE.DAT").replaceWithData(silencio.getData(), silencio.getSize());
            }
            {  // PQDESCR em texto (DDP 2.00): faixa 1 aos 00:02:00, faixa 2 aos 00:03:00 e lead-out aos 00:06:00
                const auto registro64 = [](const char* faixa, const char* indice, const char* msf) {
                    std::string r = std::string("VVVS") + faixa + indice + "  " + msf;
                    r.resize(64, ' ');
                    return r;
                };
                const std::string pq = registro64("01", "01", "000200") + registro64("02", "01", "000300") + registro64("AA", "01", "000600");
                ddp.getChildFile("PQDESCR").replaceWithData(pq.data(), pq.size());
            }
            auto stNest = registro.prepare("SELECT id FROM nest WHERE origem = 'ddp' LIMIT 1");
            if (stNest.step()) {
                JanelaContainerDdp janela(*mainComponent.projetoAberto(), stNest.columnText(0), ddp);
                std::cout << "  ..  janela DDP: carregada=" << (janela.carregado() ? "sim" : "nao") << " erro=" << janela.erro() << "\n";
                if (janela.carregado()) {
                    checar(janela.numeroDeArquivosParaTeste() == 2, "a janela do DDP lista os 2 arquivos do container");
                    janela.resized();
                    const auto t0 = juce::Time::getMillisecondCounter();
                    while (juce::Time::getMillisecondCounter() - t0 < 1500) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                    juce::Image img = janela.createComponentSnapshot(janela.getLocalBounds());
                    juce::File png = juce::File::getCurrentWorkingDirectory().getChildFile("test-output/janela_ddp.png");
                    png.getParentDirectory().createDirectory();
                    png.deleteFile();
                    if (auto out = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) juce::PNGImageFormat().writeImageToStream(img, *out);
                    std::cout << "  ..  foto da janela DDP: " << png.getFullPathName() << "\n";
                }
            }
        }

        // Foto do Intake (miniaturas) com os containers 'ddp' e 'vinyl' já criados — para olhar como o container aparece.
        {
            IntakeWorkspaceComponent intake(*mainComponent.projetoAberto());
            intake.setBounds(0, 0, 1500, 900);
            intake.definirModoVisao(IntakeWorkspaceComponent::ModoVisao::Icones);
            intake.recarregar();
            const auto t0 = juce::Time::getMillisecondCounter();
            while (juce::Time::getMillisecondCounter() - t0 < 4000) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            intake.createComponentSnapshot(intake.getLocalBounds());  // 1ª pintura pede as miniaturas
            const auto t1 = juce::Time::getMillisecondCounter();
            while (juce::Time::getMillisecondCounter() - t1 < 6000) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            juce::Image img = intake.createComponentSnapshot(intake.getLocalBounds());
            juce::File png = juce::File::getCurrentWorkingDirectory().getChildFile("test-output/intake_containers.png");
            png.getParentDirectory().createDirectory();
            png.deleteFile();
            if (auto out = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) juce::PNGImageFormat().writeImageToStream(img, *out);
            std::cout << "  ..  foto do Intake: " << png.getFullPathName() << "\n";
        }

        ingerir(neutro, nullptr);
        checar(coluna("neutro", "collection_type").empty() && coluna("neutro", "source_media").empty(),
               "sem contexto (card neutro / arrastar) o item entra como antes: sem CONTENT nem SOURCE MEDIUM");
    } catch (const std::exception& e) {
        checar(false, juce::String("teste importar contexto: ") + e.what());
    }
    tmpRoot.deleteRecursively();

#if JUCE_MAC
    // PDF no preview: o NSView do documento fica onde o componente está, mesmo depois de a janela mudar de tamanho
    // (docResize jogava a origem para (0,0): o PDF ficava torto, grudado no canto da janela).
    {
        const juce::File dirPdf = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("matriz_pdf_torto_" + juce::Uuid().toDashedString());
        dirPdf.createDirectory();
        const juce::File pdf = dirPdf.getChildFile("teste.pdf");
        pdf.replaceWithText("%PDF-1.1\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n"
                            "3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]>>endobj\ntrailer<</Root 1 0 R/Size 4>>\n%%EOF\n");
        juce::Component host;
        host.setOpaque(true);
        host.addToDesktop(juce::ComponentPeer::windowHasTitleBar);
        host.setBounds(100, 100, 700, 600);
        auto* previa = new DocumentPreviewComponent();
        host.addAndMakeVisible(previa);
        previa->setBounds(150, 60, 400, 300);
        host.setVisible(true);
        const bool carregou = previa->carregar(pdf);
        for (int i = 0; i < 10; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        previa->setBounds(210, 90, 450, 420);  // a janela mudou de layout depois de o PDF abrir
        for (int i = 0; i < 10; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        double x = -1, y = -1;
        const bool tem = previa->origemNativaParaTeste(x, y);
        const double esperadoX = 210 + 4, esperadoY = 90 + 36 + 4;  // posição do componente + margem + barra de navegação
        const bool ok = carregou && tem && std::abs(x - esperadoX) < 2.0 && std::abs(y - esperadoY) < 2.0;
        std::cout << (ok ? "  OK   " : "  FAIL ") << "PDF no preview fica na posição do componente depois de a janela mudar (x=" << x << " y=" << y
                  << ", esperado " << esperadoX << "," << esperadoY << ")\n";
        if (!ok) ++falhas;
        delete previa;
        dirPdf.deleteRecursively();
    }
#endif

    // GEO LOCATION: OPEN abre o Google Maps nas coordenadas; sem coordenadas válidas usa o endereço; sem nada, não abre.
    {
        using matriz::ui::geoacoes::Local;
        using matriz::ui::geoacoes::urlDoGoogleMaps;
        checar(urlDoGoogleMaps(Local{ "-16.4435, -39.0643", "", "", "", "" }) ==
                   "https://www.google.com/maps/search/?api=1&query=" + juce::URL::addEscapeChars("-16.443500,-39.064300", true),
               "GEO OPEN: coordenadas válidas viram a busca por lat,lng");
        const auto porEndereco = urlDoGoogleMaps(Local{ "", "Av. Paulista, 1000", "Sao Paulo", "SP", "Brasil" });
        checar(porEndereco.contains("Paulista") && porEndereco.contains("Paulo"), "GEO OPEN: sem coordenadas usa endereço + cidade + estado + país");
        checar(urlDoGoogleMaps(Local{ "0, 0", "", "", "", "" }).isEmpty() && urlDoGoogleMaps(Local{}).isEmpty(),
               "GEO OPEN: coordenada 0,0 ou campos vazios não abrem nada");
        checar(urlDoGoogleMaps(Local{ "99, 500", "Rua X", "", "", "" }).contains("Rua"), "GEO OPEN: coordenada fora do intervalo cai no endereço");
    }

    std::cout << "\n" << (falhas == 0 ? "ALL TESTS PASSED" : juce::String(falhas) + " FAILURE(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

// Renderiza a janela IMPORT (raiz, sub-janelas) e a ficha de vídeo em PNG (./test-output) para conferir o visual.
int rodarTestImportarVisual() {
    std::cout << "== IMPORT window snapshots ==\n";
    int falhas = 0;
    juce::File saida = juce::File::getCurrentWorkingDirectory().getChildFile("test-output");
    saida.createDirectory();
    const auto salvar = [&](juce::Component& c, const juce::String& nome) {
        juce::Image img = c.createComponentSnapshot(c.getLocalBounds());
        juce::File arquivo = saida.getChildFile(nome + ".png");
        arquivo.deleteFile();
        if (auto stream = std::unique_ptr<juce::FileOutputStream>(arquivo.createOutputStream())) {
            juce::PNGImageFormat png;
            png.writeImageToStream(img, *stream);
        }
        const bool ok = img.isValid() && img.getWidth() > 0;
        std::cout << (ok ? "  OK   " : "  FAIL ") << nome << " -> " << arquivo.getFullPathName() << "\n";
        if (!ok) ++falhas;
    };
    const auto girar = [](int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };

    matriz::i18n::carregar("en");
    PainelOverlay overlay;
    overlay.setBounds(0, 0, 1100, 760);
    std::optional<std::string> escolha;
    bool terminou = false;
    mostrarMenuImportar(overlay, [&](std::optional<std::string> id, std::vector<std::string>) { escolha = id; terminou = true; });
    girar(100);
    salvar(overlay, "imp_01_raiz");

    overlay.concluirExterno(103);  // Photos
    girar(150);
    salvar(overlay, "imp_01b_photos");
    overlay.concluirExterno(1);
    girar(150);
    overlay.concluirExterno(105);  // Audio
    girar(150);
    salvar(overlay, "imp_02_audio");
    overlay.concluirExterno(100);  // Production
    girar(150);
    salvar(overlay, "imp_03_audio_production");
    overlay.concluirExterno(1);    // VOLTAR
    girar(150);
    overlay.concluirExterno(101);  // Preservation
    girar(150);
    salvar(overlay, "imp_03b_audio_preservation");
    overlay.concluirExterno(1);
    girar(150);
    overlay.concluirExterno(102);  // General
    girar(150);
    salvar(overlay, "imp_03c_audio_general");
    overlay.concluirExterno(1);
    girar(150);
    overlay.concluirExterno(1);    // VOLTAR (raiz)
    girar(150);
    overlay.concluirExterno(104);  // Videos
    girar(150);
    salvar(overlay, "imp_04_videos");
    overlay.concluirExterno(1);
    girar(150);
    overlay.concluirExterno(107);  // Other
    girar(150);
    salvar(overlay, "imp_05_other");
    overlay.concluirExterno(102);  // Social Media
    girar(150);
    salvar(overlay, "imp_06_social");
    overlay.concluirExterno(1);
    girar(150);
    overlay.concluirExterno(101);  // Multimedia
    girar(150);
    salvar(overlay, "imp_07_multimedia");
    overlay.concluirExterno(0);
    girar(100);
    std::cout << (terminou && !escolha.has_value() ? "  OK   " : "  FAIL ") << "cancelar encerra sem escolha\n";
    if (!(terminou && !escolha.has_value())) ++falhas;

    {
        PainelOverlay overlayAudio;
        overlayAudio.setBounds(0, 0, 1100, 760);
        mostrarFichaImportar(overlayAudio, "audio.production.project", [](bool, std::shared_ptr<const matriz::ingest::SobreMaterial>) {});
        girar(100);
        salvar(overlayAudio, "imp_09_ficha_audio_producao");
        PainelOverlay overlayVinil;
        overlayVinil.setBounds(0, 0, 1100, 760);
        mostrarFichaImportar(overlayVinil, "audio.preservation.vinyl", [](bool, std::shared_ptr<const matriz::ingest::SobreMaterial>) {});
        girar(100);
        salvar(overlayVinil, "imp_10_ficha_audio_vinil");
        {
            PainelOverlay o;
            o.setBounds(0, 0, 1100, 760);
            mostrarListaDeContasParaTeste(o);
            girar(100);
            salvar(o, "imp_18_drive_lista");
        }
        const char* fichasVisuais[][2] = { { "photos.folder", "imp_13_ficha_fotos" }, { "documents.web", "imp_15_ficha_docs_web" }, { "other.multimedia.interactive", "imp_16_ficha_interativo" },
                                           { "other.social.other", "imp_17_ficha_rede_outra" },
                                           { "audio.preservation.vinyl", "imp_19_ficha_vinil" }, { "audio.preservation.tape", "imp_20_ficha_fita" },
                                           { "audio.general.field", "imp_21_ficha_campo" }, { "audio.general.film_sound", "imp_22_ficha_film_sound" },
                                           { "audio.general.voice_over", "imp_23_ficha_locucao" }, { "documents.scanned", "imp_24_ficha_docs_escaneado" },
                                           { "other.design", "imp_25_ficha_design" }, { "files.optical", "imp_14_ficha_arquivos_optico" },
                                           { "audio.production.midi", "imp_26_ficha_midi" }, { "audio.production.tools", "imp_27_ficha_tools" },
                                           { "photos.camera_card", "imp_28_ficha_cartao_camera" }, { "videos.youtube", "imp_30_ficha_youtube" } };
        for (const auto& fv : fichasVisuais) {
            PainelOverlay o;
            o.setBounds(0, 0, 1100, 760);
            mostrarFichaImportar(o, fv[0], [](bool, std::shared_ptr<const matriz::ingest::SobreMaterial>) {});
            girar(100);
            salvar(o, fv[1]);
        }
        // Toda ficha tem que concluir com CONTINUE (era o que faltava testar: o DDP quebrava aqui) e com PULAR.
        {
            int total = 0, concluiram = 0;
            for (const auto& id : idsDasFolhasDoMenuImportar()) {
                if (!folhaImportarTemFicha(id)) continue;
                for (int botao : { 1, 2 }) {
                    PainelOverlay o;
                    o.setBounds(0, 0, 1100, 760);
                    bool respondeu = false;
                    mostrarFichaImportar(o, id, [&](bool, std::shared_ptr<const matriz::ingest::SobreMaterial>) { respondeu = true; });
                    girar(30);
                    o.concluirExterno(botao);
                    girar(30);
                    ++total;
                    if (respondeu) ++concluiram;
                    else std::cout << "  FAIL ficha sem resposta: " << id << " botao " << botao << "\n";
                }
            }
            const bool ok = total > 0 && total == concluiram;
            std::cout << (ok ? "  OK   " : "  FAIL ") << "todas as fichas concluem com CONTINUE e PULAR (" << concluiram << "/" << total << ")\n";
            if (!ok) ++falhas;
        }

        // Metadata pós-questionário: cada ficha preenche SUBJECT / LIST / TAGS / NOTES do jeito pedido.
        {
            const auto responder = [&](const std::string& folha, const std::vector<std::pair<juce::String, juce::String>>& valores) {
                PainelOverlay o;
                o.setBounds(0, 0, 1100, 760);
                std::shared_ptr<const matriz::ingest::SobreMaterial> r;
                mostrarFichaImportar(o, folha, [&](bool, std::shared_ptr<const matriz::ingest::SobreMaterial> sm) { r = sm; });
                girar(30);
                for (const auto& [id, v] : valores) o.definirValorCampo(id, v);
                o.concluirExterno(1);
                girar(30);
                return r;
            };
            const auto lista = [](const matriz::ingest::SobreMaterial& sm) {
                for (const auto& c : sm.campos) if (c.first == "lista") return c.second;
                return std::string();
            };
            const auto temTag = [](const matriz::ingest::SobreMaterial& sm, const std::string& t) {
                return std::find(sm.tags.begin(), sm.tags.end(), t) != sm.tags.end();
            };
            const auto confere = [&](bool ok, const char* nome) {
                std::cout << (ok ? "  OK   " : "  FAIL ") << nome << "\n";
                if (!ok) ++falhas;
            };
            if (auto sm = responder("audio.production.tools", { { "asset_name", "Valhalla VintageVerb" }, { "tool_function", "Reverb, Plate" } }); sm)
                confere(lista(*sm) == "Valhalla VintageVerb" && temTag(*sm, "Reverb") && temTag(*sm, "Plate") && sm->subject.find("Tools") == std::string::npos,
                        "Tools: LIST = nome, função = tags, SUBJECT = tipo (não 'Tools')");
            else confere(false, "Tools: ficha respondeu");
            if (auto sm = responder("audio.production.samples", { { "pack_name", "Drums Vol 1" }, { "sound_type", "Kick, Snare" } }); sm)
                confere(sm->subject == "Sample Pack" && lista(*sm) == "Drums Vol 1" && temTag(*sm, "Kick") && temTag(*sm, "Snare"),
                        "Sample Pack: SUBJECT = Sample Pack, LIST = nome do pack, tipo de som = tags");
            else confere(false, "Sample Pack: ficha respondeu");
            if (auto sm = responder("audio.production.midi", { { "colecao", "Album Demos" }, { "ecossistema", "Logic Pro" }, { "origem_hw", "Akai MPC2000" } }); sm)
                confere(sm->subject == "Midi" && lista(*sm) == "Album Demos" && temTag(*sm, "Logic Pro") && temTag(*sm, "Akai MPC2000"),
                        "MIDI: SUBJECT = Midi, LIST = nome do projeto, ecossistema e hardware = tags");
            else confere(false, "MIDI: ficha respondeu");
            if (auto sm = responder("audio.production.project", { { "artist", "Banda X" }, { "project", "Cancao Y" }, { "description", "Mix final aprovado" } }); sm)
                confere(sm->subject == "Banda X" && lista(*sm) == "Cancao Y" && sm->descricao.find("Mix final aprovado") != std::string::npos,
                        "Music Production: SUBJECT = artista/banda, LIST = projeto/música, descrição = NOTES");
            else confere(false, "Music Production: ficha respondeu");
            if (auto sm = responder("videos.youtube", { { "nome", "Meu video" }, { "sobre", "um dois tres quatro cinco seis" } }); sm)
                confere(lista(*sm) == "um dois tres quatro" && sm->subject.find("Meu video") == std::string::npos,
                        "YouTube: LIST = do que se trata (máx. 4 palavras), o nome do vídeo não vira SUBJECT");
            else confere(false, "YouTube: ficha respondeu");
        }

        // VOLTAR: a escolha de uma folha devolve o caminho de grupos, e reabrir com esse caminho cai no mesmo nível.
        {
            std::string idRecebido;
            std::vector<std::string> caminhoRecebido;
            PainelOverlay o;
            o.setBounds(0, 0, 1100, 760);
            mostrarMenuImportar(o, [&](std::optional<std::string> id, std::vector<std::string> caminho) {
                if (id.has_value()) idRecebido = *id;
                caminhoRecebido = caminho;
            });
            girar(50);
            o.concluirExterno(105); girar(80);  // Audio
            o.concluirExterno(101); girar(80);  // Preservation
            o.concluirExterno(102); girar(80);  // Vinyl transcription
            const std::vector<std::string> esperado = { "audio", "audio.preservation" };
            const bool caminhoOk = idRecebido == "audio.preservation.vinyl" && caminhoRecebido == esperado;
            std::cout << (caminhoOk ? "  OK   " : "  FAIL ") << "a folha escolhida devolve o caminho de grupos (" << idRecebido << ")\n";
            if (!caminhoOk) ++falhas;

            PainelOverlay o2;
            o2.setBounds(0, 0, 1100, 760);
            mostrarMenuImportar(o2, [](std::optional<std::string>, std::vector<std::string>) {}, caminhoRecebido);
            girar(80);
            const bool reabriu = o2.tituloParaTeste().containsIgnoreCase("preservation") && o2.totalBotoesParaTeste() == 2;
            std::cout << (reabriu ? "  OK   " : "  FAIL ") << "reabrir com o caminho volta ao mesmo card, com VOLTAR (" << o2.tituloParaTeste() << ")\n";
            if (!reabriu) ++falhas;

            // Toda ficha ganha VOLTAR quando há para onde voltar, e ele chama o retorno em vez de concluir.
            bool voltou = false, concluiu = false;
            PainelOverlay o3;
            o3.setBounds(0, 0, 1100, 760);
            mostrarFichaImportar(o3, "audio.preservation.vinyl", [&](bool, std::shared_ptr<const matriz::ingest::SobreMaterial>) { concluiu = true; },
                                 [&] { voltou = true; });
            girar(50);
            const bool quatroBotoes = o3.totalBotoesParaTeste() == 4;
            salvar(o3, "imp_29_ficha_com_voltar");
            o3.concluirExterno(3);
            girar(50);
            const bool fichaOk = quatroBotoes && voltou && !concluiu;
            std::cout << (fichaOk ? "  OK   " : "  FAIL ") << "a ficha tem VOLTAR (4 botoes) e ele nao conclui a importacao\n";
            if (!fichaOk) ++falhas;
        }

        {
            PainelOverlay o;
            o.setBounds(0, 0, 1100, 760);
            mostrarPerguntaOutroYoutube(o, "Meu vlog de domingo", 2, [](int) {});
            girar(80);
            salvar(o, "imp_31_youtube_outro");
        }

        {
            PainelOverlay o;
            o.setBounds(0, 0, 1100, 760);
            mostrarArquivosYoutube(o, {}, [](int, ArquivosYoutube) {});
            girar(80);
            salvar(o, "imp_32_youtube_arquivos");
        }

        PainelOverlay overlayDdp;
        overlayDdp.setBounds(0, 0, 1100, 760);
        mostrarFichaImportar(overlayDdp, "audio.preservation.ddp", [](bool, std::shared_ptr<const matriz::ingest::SobreMaterial>) {});
        girar(100);
        salvar(overlayDdp, "imp_12_ficha_ddp");
        PainelOverlay overlayCustom;
        overlayCustom.setBounds(0, 0, 1100, 760);
        mostrarFichaImportar(overlayCustom, "other.custom", [](bool, std::shared_ptr<const matriz::ingest::SobreMaterial>) {});
        girar(100);
        salvar(overlayCustom, "imp_11_ficha_outro");
    }

    PainelOverlay overlayFicha;
    overlayFicha.setBounds(0, 0, 1100, 760);
    bool fichaRespondeu = false;
    mostrarFichaImportar(overlayFicha, "videos.digital", [&](bool, std::shared_ptr<const matriz::ingest::SobreMaterial>) { fichaRespondeu = true; });
    girar(100);
    salvar(overlayFicha, "imp_08_ficha_video");
    overlayFicha.concluirExterno(2);
    girar(50);
    std::cout << (fichaRespondeu ? "  OK   " : "  FAIL ") << "PULAR conclui a ficha\n";
    if (!fichaRespondeu) ++falhas;

    std::cout << "\n" << (falhas == 0 ? "ALL TESTS PASSED" : juce::String(falhas) + " FAILURE(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
