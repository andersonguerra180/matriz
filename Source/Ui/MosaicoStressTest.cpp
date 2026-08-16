#include "MosaicoStressTest.h"

#include "../I18n/Strings.h"
#include "../Model/Project.h"
#include "MosaicoComponent.h"
#include "ProjetoAberto.h"

#include <JuceHeader.h>

#include <algorithm>
#include <iostream>

namespace matriz::ui {

namespace {

void inserirItensSinteticos(matriz::db::Database& registro, const std::string& projetoId, int quantidade) {
    registro.exec("BEGIN");
    auto stmt = registro.prepare(
        "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, 'fita_rolo', ?, ?, ?)");
    std::string agora = matriz::model::agoraIso8601();
    static const char* kEstados[] = {"nao_digitalizado", "capturado", "qc_ok", "alerta", "publicado"};

    for (int i = 0; i < quantidade; ++i) {
        stmt.reset();
        stmt.bind(1, matriz::db::Value::of(matriz::model::novoUuid()));
        stmt.bind(2, matriz::db::Value::of(projetoId));
        stmt.bind(3, matriz::db::Value::of(juce::String::formatted("STR-%05d", i).toStdString()));
        stmt.bind(4, matriz::db::Value::of((juce::String("Synthetic item ") + juce::String(i)).toStdString()));
        stmt.bind(5, matriz::db::Value::of(std::string(kEstados[i % 5])));
        stmt.bind(6, matriz::db::Value::of(agora));
        stmt.bind(7, matriz::db::Value::of(agora));
        stmt.step();
    }
    registro.exec("COMMIT");
}

// Item avulso de um tipo_midia específico, opcionalmente com um campo de
// ficha (nível raiz) - usado pra testar o agrupamento do mosaico por modo
// (§3.5), não pelo volume do stress test de virtualização.
std::string inserirItemTipado(matriz::db::Database& registro, const std::string& projetoId,
                                const std::string& codigo, const std::string& tipoMidia) {
    std::string id = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();
    registro.run(
        "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, 'capturado', ?, ?)",
        {matriz::db::Value::of(id), matriz::db::Value::of(projetoId), matriz::db::Value::of(codigo),
         matriz::db::Value::of(codigo), matriz::db::Value::of(tipoMidia), matriz::db::Value::of(agora),
         matriz::db::Value::of(agora)});
    return id;
}

void gravarCampoRaiz(matriz::db::Database& registro, const std::string& itemId, const std::string& campoId,
                      const std::string& valor) {
    registro.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                  "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?)",
                  {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                   matriz::db::Value::of(campoId), matriz::db::Value::of(valor),
                   matriz::db::Value::of(matriz::model::agoraIso8601())});
}

double medirMs(const std::function<void()>& fn) {
    double inicio = juce::Time::getMillisecondCounterHiRes();
    fn();
    return juce::Time::getMillisecondCounterHiRes() - inicio;
}

// Mediana de N amostras - esta máquina de desenvolvimento roda outros apps
// BKR simultaneamente (Groove Sculptor, Autopsy) e uma amostra única de
// paint() varia várias vezes mais entre execuções do mesmo código só por
// ruído de agendamento do SO. Uma medição real de "isto não escala com o
// total de itens" precisa de mediana, não de uma amostra só.
double medirMsMediana(const std::function<void()>& fn, int amostras = 7) {
    std::vector<double> tempos;
    tempos.reserve(static_cast<size_t>(amostras));
    for (int i = 0; i < amostras; ++i) tempos.push_back(medirMs(fn));
    std::sort(tempos.begin(), tempos.end());
    return tempos[static_cast<size_t>(amostras / 2)];
}

// Renderiza o mosaico como se estivesse rolado até `scrollY`, numa janela de
// `largura`x`altura` - replica o que um Viewport real faz (desloca a origem
// da Graphics, então só a região exposta entra no clip que paint() lê via
// g.getClipBounds()). Mediana de várias amostras - ver medirMsMediana.
double medirPaintEmScroll(MosaicoComponent& mosaico, int largura, int altura, int scrollY) {
    return medirMsMediana([&] {
        juce::Image img(juce::Image::ARGB, largura, altura, true);
        juce::Graphics g(img);
        g.setOrigin(0, -scrollY);
        mosaico.paintEntireComponent(g, true);
    });
}

} // namespace

int rodarStressTestMosaico10k() {
    std::cout << "== Virtualised grid stress test (100k items, criterion 18) ==\n";
    int falhas = 0;
    auto checar = [&](bool condicao, const juce::String& descricao) {
        std::cout << (condicao ? "  OK   " : "  FAIL ") << descricao << "\n";
        if (!condicao) ++falhas;
    };

    juce::File tmpRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_mosaico_stress_" + juce::Uuid().toDashedString());

    constexpr int kLarguraViewport = 1200;
    constexpr int kAlturaViewport = 800;
    // Critério 18 pede 100 mil itens simulados no mosaico sem lag. A
    // virtualização é O(área visível), não O(total) - se o número mudar o
    // tempo de paint, é porque alguém está iterando a lista inteira.
    constexpr int kTotalItensGrande = 100000;
    constexpr int kTotalItensPequeno = 100;

    try {
        // --- Projeto pequeno (100 itens) - referência ---
        matriz::model::NovoProjetoParams paramsPequeno;
        paramsPequeno.nome = "Stress pequeno";
        paramsPequeno.prefixoNomenclatura = "STR";
        auto projetoPequeno = matriz::model::Project::criar(tmpRoot.getChildFile("pequeno"), paramsPequeno);
        inserirItensSinteticos(projetoPequeno->registro(), projetoPequeno->projetoId(), kTotalItensPequeno);
        ProjetoAberto pa1(std::move(projetoPequeno));
        MosaicoComponent mosaicoPequeno(pa1);
        mosaicoPequeno.setBounds(0, 0, kLarguraViewport, kAlturaViewport);
        double tRecarregarPequeno = medirMs([&] { mosaicoPequeno.recarregarSincrono(); });
        checar(mosaicoPequeno.totalItensCarregados() == kTotalItensPequeno,
               "small project loaded " + juce::String(mosaicoPequeno.totalItensCarregados()) + " itens");
        double tPaintPequenoTopo = medirPaintEmScroll(mosaicoPequeno, kLarguraViewport, kAlturaViewport, 0);

        // --- Projeto grande (100 mil itens) ---
        matriz::model::NovoProjetoParams paramsGrande;
        paramsGrande.nome = "Stress grande";
        paramsGrande.prefixoNomenclatura = "STR";
        auto projetoGrande = matriz::model::Project::criar(tmpRoot.getChildFile("grande"), paramsGrande);
        std::string projetoGrandeId = projetoGrande->projetoId();
        double tInsercao = medirMs([&] { inserirItensSinteticos(projetoGrande->registro(), projetoGrandeId, kTotalItensGrande); });
        std::cout << "  ..  inserting " << kTotalItensGrande << " synthetic items: " << (int)tInsercao << "ms\n";

        ProjetoAberto pa2(std::move(projetoGrande));
        MosaicoComponent mosaicoGrande(pa2);
        mosaicoGrande.setBounds(0, 0, kLarguraViewport, kAlturaViewport);

        double tRecarregarGrande = medirMs([&] { mosaicoGrande.recarregarSincrono(); });
        checar(mosaicoGrande.totalItensCarregados() == kTotalItensGrande,
               "large project loaded " + juce::String(mosaicoGrande.totalItensCarregados()) + " itens");
        std::cout << "  ..  recarregar(): pequeno=" << (int)tRecarregarPequeno << "ms, grande=" << (int)tRecarregarGrande
                   << "ms\n";

        // resized()/recalcularLayout() é O(1) (só aritmética a partir da
        // contagem total) - não itera item por item, então o tempo não deve
        // crescer de forma perceptível entre 100 e 100 mil itens.
        double tResizedPequeno = medirMs([&] { mosaicoPequeno.resized(); });
        double tResizedGrande = medirMs([&] { mosaicoGrande.resized(); });
        checar(tResizedGrande < 5.0, "resized() with 100k items is essentially instant (" +
                                          juce::String(tResizedGrande, 3) + "ms)");

        // paint() no topo e rolado bem fundo na lista - só a área exposta
        // (kLarguraViewport x kAlturaViewport) deve custar trabalho, nunca o
        // total de itens.
        double tPaintGrandeTopo = medirPaintEmScroll(mosaicoGrande, kLarguraViewport, kAlturaViewport, 0);
        int alturaTotalGrande = mosaicoGrande.getHeight();
        double tPaintGrandeFundo =
            medirPaintEmScroll(mosaicoGrande, kLarguraViewport, kAlturaViewport, juce::jmax(0, alturaTotalGrande - kAlturaViewport));

        std::cout << "  ..  paint() pequeno(topo)=" << juce::String(tPaintPequenoTopo, 2)
                   << "ms, grande(topo)=" << juce::String(tPaintGrandeTopo, 2)
                   << "ms, grande(fundo)=" << juce::String(tPaintGrandeFundo, 2) << "ms\n";

        // Limiar generoso de propósito: esta é uma máquina de desenvolvimento
        // compartilhada, rodando outros apps BKR ao mesmo tempo, em build de
        // depuração - não é bancada de CI dedicada. O limiar existe pra
        // pegar uma regressão real (que apareceria em segundos, não
        // milissegundos), não pra exigir 60fps de laboratório.
        checar(tPaintGrandeTopo < 200.0, "paint() at the top of 100k items stays under 200ms (median " + juce::String(tPaintGrandeTopo, 2) + "ms)");
        checar(tPaintGrandeFundo < 200.0,
               "paint() scrolled to the end of 100k items stays under 200ms (median " + juce::String(tPaintGrandeFundo, 2) + "ms)");
        // A prova real de virtualização: pintar 100 mil itens não pode ser
        // ordens de magnitude mais lento que pintar 100 - se fosse, algum
        // código estaria iterando a lista inteira em vez de só o clip visível.
        checar(tPaintGrandeTopo < tPaintPequenoTopo * 5.0 + 30.0,
               "paint() at the top does not scale with item count (100k is not >5x slower than 100)");
        checar(tPaintGrandeFundo < tPaintPequenoTopo * 5.0 + 30.0,
               "paint() at the bottom does not scale with item count (100k is not >5x slower than 100)");

        // --- Agrupamento por modo (Parte 1 da correção de fluxo, §3.5) ---
        // Archive agrupa por tipo de mídia; Catalog por artista/lançamento
        // (lido da ficha, não de item.titulo - ver ProjetoAberto::listarItens).
        //
        // Fixa o locale antes de comparar rótulo: Main.cpp carrega o idioma
        // das preferências da MÁQUINA antes de entrar no selftest, então
        // sem isto o teste passava ou falhava conforme o idioma em que o
        // app tivesse sido deixado da última vez - verde num computador,
        // vermelho em outro, sem nada no código ter mudado.
        matriz::i18n::carregar("en");

        matriz::model::NovoProjetoParams paramsArchive;
        paramsArchive.nome = "Grupos archive";
        paramsArchive.modo = matriz::model::Modo::Preservacao;
        paramsArchive.prefixoNomenclatura = "GRA";
        auto projetoArchive = matriz::model::Project::criar(tmpRoot.getChildFile("grupos_archive"), paramsArchive);
        inserirItemTipado(projetoArchive->registro(), projetoArchive->projetoId(), "A-01", "fita_rolo");
        inserirItemTipado(projetoArchive->registro(), projetoArchive->projetoId(), "A-02", "fita_rolo");
        inserirItemTipado(projetoArchive->registro(), projetoArchive->projetoId(), "A-03", "foto");
        ProjetoAberto abertoArchive(std::move(projetoArchive));
        MosaicoComponent mosaicoArchive(abertoArchive);
        mosaicoArchive.setBounds(0, 0, kLarguraViewport, kAlturaViewport);
        mosaicoArchive.recarregarSincrono();
        auto gruposArchive = mosaicoArchive.rotulosDeGrupo();
        checar(gruposArchive.size() == 2, "archive with reel tape + photo yields 2 groups (" +
                                               juce::String(static_cast<int>(gruposArchive.size())) + ")");
        // Rótulos em inglês (locale padrão) - fichas/*.yaml agora passa
        // pela camada de tradução (Source/Ficha/FichaI18n.h, correção
        // crítica pós-teste do usuário #2).
        checar(std::any_of(gruposArchive.begin(), gruposArchive.end(),
                            [](const juce::String& s) { return s == "Reel tape - 2"; }),
               "group \"Reel tape - 2\" is present");
        checar(std::any_of(gruposArchive.begin(), gruposArchive.end(),
                            [](const juce::String& s) { return s == "Photo - 1"; }),
               "group \"Photo - 1\" is present");

        matriz::model::NovoProjetoParams paramsCatalog;
        paramsCatalog.nome = "Grupos catalog";
        paramsCatalog.modo = matriz::model::Modo::Catalogo;
        paramsCatalog.prefixoNomenclatura = "GRC";
        auto projetoCatalog = matriz::model::Project::criar(tmpRoot.getChildFile("grupos_catalog"), paramsCatalog);
        std::string idRelease1 = inserirItemTipado(projetoCatalog->registro(), projetoCatalog->projetoId(), "C-01", "release");
        gravarCampoRaiz(projetoCatalog->registro(), idRelease1, "artista_principal", "Banda X");
        gravarCampoRaiz(projetoCatalog->registro(), idRelease1, "titulo", "Album 1");
        std::string idRelease2 = inserirItemTipado(projetoCatalog->registro(), projetoCatalog->projetoId(), "C-02", "release");
        gravarCampoRaiz(projetoCatalog->registro(), idRelease2, "artista_principal", "Banda Y");
        gravarCampoRaiz(projetoCatalog->registro(), idRelease2, "titulo", "Album 2");
        inserirItemTipado(projetoCatalog->registro(), projetoCatalog->projetoId(), "C-03", "sample");
        ProjetoAberto abertoCatalog(std::move(projetoCatalog));
        MosaicoComponent mosaicoCatalog(abertoCatalog);
        mosaicoCatalog.setBounds(0, 0, kLarguraViewport, kAlturaViewport);
        mosaicoCatalog.recarregarSincrono();
        auto gruposCatalog = mosaicoCatalog.rotulosDeGrupo();
        checar(gruposCatalog.size() == 2,
               "catalog with 2 releases + 1 sample yields 2 groups (" +
                   juce::String(static_cast<int>(gruposCatalog.size())) + ")");
        checar(std::any_of(gruposCatalog.begin(), gruposCatalog.end(),
                            [](const juce::String& s) { return s == "Release - 2"; }),
               "grouping by file type (Release) is present");
        checar(std::any_of(gruposCatalog.begin(), gruposCatalog.end(),
                            [](const juce::String& s) { return s == "Sample - 1"; }),
               "grouping by file type (Sample) is present");

    } catch (const std::exception& e) {
        checar(false, juce::String("grid stress test: ") + e.what());
    }

    tmpRoot.deleteRecursively();

    std::cout << "\n" << (falhas == 0 ? "ALL TESTS PASSED" : juce::String(falhas) + " FAILURE(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
