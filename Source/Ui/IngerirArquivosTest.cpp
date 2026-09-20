#include "IngerirArquivosTest.h"

#include "../Ficha/CatalogoDeFichas.h"
#include "../Model/Project.h"
#include "MainComponent.h"
#include "PainelInconsistenciasComponent.h"
#include "ProjetoAberto.h"
#include "SelecionarTipoMidiaDialogo.h"
#include "../Ingest/LightroomImporter.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>

#ifdef __APPLE__
#include <mach/mach.h>
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

            checar(mainComponent.textoProgressoIngestParaTeste().contains("ancel"),
                   "the banner reports a cancellation, not a success summary");
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
        auto todosOsTiposEmDisco = matriz::ficha::listarTodosOsTipos(MATRIZ_FICHAS_DIR);
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
                struct task_basic_info info;
                mach_msg_type_number_t contagem = TASK_BASIC_INFO_COUNT;
                if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &contagem) == KERN_SUCCESS)
                    picoRamBytes = juce::jmax(picoRamBytes, (size_t)info.resident_size);
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

} // namespace matriz::ui
