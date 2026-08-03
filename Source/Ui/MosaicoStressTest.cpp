#include "MosaicoStressTest.h"

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
        stmt.bind(4, matriz::db::Value::of((juce::String("Item sintético ") + juce::String(i)).toStdString()));
        stmt.bind(5, matriz::db::Value::of(std::string(kEstados[i % 5])));
        stmt.bind(6, matriz::db::Value::of(agora));
        stmt.bind(7, matriz::db::Value::of(agora));
        stmt.step();
    }
    registro.exec("COMMIT");
}

double medirMs(const std::function<void()>& fn) {
    double inicio = juce::Time::getMillisecondCounterHiRes();
    fn();
    return juce::Time::getMillisecondCounterHiRes() - inicio;
}

// Mediana de N amostras — esta máquina de desenvolvimento roda outros apps
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
// `largura`x`altura` — replica o que um Viewport real faz (desloca a origem
// da Graphics, então só a região exposta entra no clip que paint() lê via
// g.getClipBounds()). Mediana de várias amostras — ver medirMsMediana.
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
    std::cout << "== Stress test do mosaico virtualizado (B.2) ==\n";
    int falhas = 0;
    auto checar = [&](bool condicao, const juce::String& descricao) {
        std::cout << (condicao ? "  OK   " : "  FAIL ") << descricao << "\n";
        if (!condicao) ++falhas;
    };

    juce::File tmpRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_mosaico_stress_" + juce::Uuid().toDashedString());

    constexpr int kLarguraViewport = 1200;
    constexpr int kAlturaViewport = 800;
    constexpr int kTotalItensGrande = 10000;
    constexpr int kTotalItensPequeno = 100;

    try {
        // --- Projeto pequeno (100 itens) — referência ---
        matriz::model::NovoProjetoParams paramsPequeno;
        paramsPequeno.nome = "Stress pequeno";
        paramsPequeno.prefixoNomenclatura = "STR";
        auto projetoPequeno = matriz::model::Project::criar(tmpRoot.getChildFile("pequeno"), paramsPequeno);
        inserirItensSinteticos(projetoPequeno->registro(), projetoPequeno->projetoId(), kTotalItensPequeno);
        ProjetoAberto pa1(std::move(projetoPequeno));
        MosaicoComponent mosaicoPequeno(pa1);
        mosaicoPequeno.setBounds(0, 0, kLarguraViewport, kAlturaViewport);
        double tRecarregarPequeno = medirMs([&] { mosaicoPequeno.recarregar(); });
        checar(mosaicoPequeno.totalItensCarregados() == kTotalItensPequeno,
               "projeto pequeno carregou " + juce::String(mosaicoPequeno.totalItensCarregados()) + " itens");
        double tPaintPequenoTopo = medirPaintEmScroll(mosaicoPequeno, kLarguraViewport, kAlturaViewport, 0);

        // --- Projeto grande (10 mil itens) ---
        matriz::model::NovoProjetoParams paramsGrande;
        paramsGrande.nome = "Stress grande";
        paramsGrande.prefixoNomenclatura = "STR";
        auto projetoGrande = matriz::model::Project::criar(tmpRoot.getChildFile("grande"), paramsGrande);
        std::string projetoGrandeId = projetoGrande->projetoId();
        double tInsercao = medirMs([&] { inserirItensSinteticos(projetoGrande->registro(), projetoGrandeId, kTotalItensGrande); });
        std::cout << "  ..  inserção de " << kTotalItensGrande << " itens sintéticos: " << (int)tInsercao << "ms\n";

        ProjetoAberto pa2(std::move(projetoGrande));
        MosaicoComponent mosaicoGrande(pa2);
        mosaicoGrande.setBounds(0, 0, kLarguraViewport, kAlturaViewport);

        double tRecarregarGrande = medirMs([&] { mosaicoGrande.recarregar(); });
        checar(mosaicoGrande.totalItensCarregados() == kTotalItensGrande,
               "projeto grande carregou " + juce::String(mosaicoGrande.totalItensCarregados()) + " itens");
        std::cout << "  ..  recarregar(): pequeno=" << (int)tRecarregarPequeno << "ms, grande=" << (int)tRecarregarGrande
                   << "ms\n";

        // resized()/recalcularLayout() é O(1) (só aritmética a partir da
        // contagem total) — não itera item por item, então o tempo não deve
        // crescer de forma perceptível entre 100 e 10 mil itens.
        double tResizedPequeno = medirMs([&] { mosaicoPequeno.resized(); });
        double tResizedGrande = medirMs([&] { mosaicoGrande.resized(); });
        checar(tResizedGrande < 5.0, "resized() com 10 mil itens é essencialmente instantâneo (" +
                                          juce::String(tResizedGrande, 3) + "ms)");

        // paint() no topo e rolado bem fundo na lista — só a área exposta
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
        // depuração — não é bancada de CI dedicada. O limiar existe pra
        // pegar uma regressão real (que apareceria em segundos, não
        // milissegundos), não pra exigir 60fps de laboratório.
        checar(tPaintGrandeTopo < 200.0, "paint() no topo dos 10 mil itens fica sob 200ms (mediana " + juce::String(tPaintGrandeTopo, 2) + "ms)");
        checar(tPaintGrandeFundo < 200.0,
               "paint() rolado até o fim dos 10 mil itens fica sob 200ms (mediana " + juce::String(tPaintGrandeFundo, 2) + "ms)");
        // A prova real de virtualização: pintar 10 mil itens não pode ser
        // ordens de magnitude mais lento que pintar 100 — se fosse, algum
        // código estaria iterando a lista inteira em vez de só o clip visível.
        checar(tPaintGrandeTopo < tPaintPequenoTopo * 5.0 + 30.0,
               "paint() do topo não escala com o total de itens (10 mil não é >5x mais lento que 100)");
        checar(tPaintGrandeFundo < tPaintPequenoTopo * 5.0 + 30.0,
               "paint() do fundo não escala com o total de itens (10 mil não é >5x mais lento que 100)");

    } catch (const std::exception& e) {
        checar(false, juce::String("stress test do mosaico: ") + e.what());
    }

    tmpRoot.deleteRecursively();

    std::cout << "\n" << (falhas == 0 ? "TODOS OS TESTES PASSARAM" : juce::String(falhas) + " FALHA(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
