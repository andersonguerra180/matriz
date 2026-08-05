#include "IngerirArquivosTest.h"

#include "../Model/Project.h"
#include "MainComponent.h"
#include "PainelInconsistenciasComponent.h"
#include "ProjetoAberto.h"
#include "SelecionarTipoMidiaDialogo.h"

#include <JuceHeader.h>

#include <algorithm>
#include <iostream>

namespace matriz::ui {

namespace {

void gerarComFfmpeg(const juce::StringArray& args) {
    juce::ChildProcess proc;
    if (!proc.start(args, juce::ChildProcess::wantStdOut))
        throw std::runtime_error("não foi possível iniciar o ffmpeg");
    proc.readAllProcessOutput();
    proc.waitForProcessToFinish(30000);
    if (proc.getExitCode() != 0) throw std::runtime_error("ffmpeg terminou com erro ao gerar mídia de teste");
}

// Ingest agora roda em background (ThreadPool + callAsync) — o teste precisa
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

} // namespace

int rodarTestIngerirArquivos() {
    std::cout << "== Teste da ponte UI -> ingest (MainComponent::ingerirArquivos) ==\n";
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
        checar(stmtContagem.columnInt(0) == 2, "2 itens foram criados (áudio + imagem)");

        // Nenhum tipo de mídia escolhido no ingest (Reorientação completa
        // §2.1/§7.1): o item aparece com tipo_midia NULL — classificar é
        // trabalho de depois, por cima do que já está na grade.
        auto stmtAudio = registro.prepare(
            "SELECT tipo_midia, estado FROM item WHERE titulo = 'tom'");
        bool achouAudio = stmtAudio.step();
        checar(achouAudio && stmtAudio.columnIsNull(0) && stmtAudio.columnText(1) == "capturado",
               "item de áudio entra com tipo_midia=NULL (ainda não classificado) e estado=capturado");

        auto stmtArquivoAudio = registro.prepare(
            "SELECT a.papel, a.eh_master, a.checksum_sha256, a.caracteristicas_tecnicas_json "
            "FROM arquivo a JOIN item i ON i.id = a.item_id WHERE i.titulo = 'tom'");
        bool achouArquivoAudio = stmtArquivoAudio.step();
        checar(achouArquivoAudio && stmtArquivoAudio.columnText(0) == "preservation_master" &&
                   stmtArquivoAudio.columnInt(1) == 1,
               "arquivo de áudio ingerido como preservation_master/master");
        checar(achouArquivoAudio && stmtArquivoAudio.columnText(2).length() == 64,
               "checksum SHA-256 real foi calculado (64 hex chars)");
        checar(achouArquivoAudio && stmtArquivoAudio.columnText(3).find("duracaoSegundos") != std::string::npos,
               "leitura técnica (ffprobe) populou caracteristicas_tecnicas_json");

        auto stmtImagem = registro.prepare("SELECT tipo_midia FROM item WHERE titulo = 'foto'");
        bool achouImagem = stmtImagem.step();
        checar(achouImagem && stmtImagem.columnIsNull(0), "item de imagem também entra com tipo_midia=NULL");

        // Ingerir de novo não deve duplicar itens indefinidamente nem quebrar
        // (cada chamada cria itens novos por design atual — confirma que ao
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
        // Item 10 — cancelar operação longa.
        //
        // O que precisa valer, independente de QUANTOS arquivos deram
        // tempo de ser processados antes do clique (o teste não controla o
        // escalonador): o lote termina de verdade, o que foi processado
        // continua válido, e não sobra item-fantasma — a fase 1 insere
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
                   "lote cancelado TERMINA — pendentes zera em vez de ficar preso 'em andamento'");

            auto stmtDepois = registro.prepare("SELECT COUNT(*) FROM item");
            stmtDepois.step();
            int itensDepois = stmtDepois.columnInt(0);
            checar(itensDepois < itensAntes + kQuantidade,
                   "cancelar interrompeu de fato (entraram menos que os " + juce::String(kQuantidade) +
                       " arquivos do lote: " + juce::String(itensDepois - itensAntes) + ")");
            checar(itensDepois >= itensAntes,
                   "cancelar NÃO reverteu o que já existia nem o que já tinha sido processado");

            // A invariante que importa: nenhum item sem arquivo atrás.
            // 'duplicata' fica de fora porque item duplicado NÃO tem linha
            // em `arquivo` por design (item 9 — "um asset, muitas
            // localizações": conteúdo já conhecido é reconhecido, não
            // copiado de novo). As 40 cópias do mesmo .wav caem todas aí.
            auto stmtFantasmas = registro.prepare(
                "SELECT COUNT(*) FROM item i WHERE i.estado <> 'duplicata' "
                "AND NOT EXISTS (SELECT 1 FROM arquivo a WHERE a.item_id = i.id)");
            stmtFantasmas.step();
            checar(stmtFantasmas.columnInt(0) == 0,
                   "nenhum item-fantasma sobrou: todo item no projeto tem arquivo de verdade");

            checar(mainComponent.textoProgressoIngestParaTeste().contains("ancel"),
                   "a faixa informa que foi cancelado, e não um resumo de sucesso");
        }


        mainComponent.fecharProjeto();
        checar(!mainComponent.temProjetoAberto(), "fecharProjeto() limpa o estado corretamente");

        // Restrição de tipos de mídia por modo (Parte 1 da correção de
        // fluxo, §1.2/§1.3): Archive oferece os 14 tipos, Catalog só o
        // musicalmente relevante — nunca fita_rolo aparecendo como se
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
               "modo catálogo oferece release/sample/fita_rolo/vinil");
        checar(!temTipo(tiposCatalog, "documento") && !temTipo(tiposCatalog, "cd") && !temTipo(tiposCatalog, "foto"),
               "modo catálogo NÃO oferece tipos irrelevantes (documento/cd/foto)");

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
               "modo catálogo mostra o painel de inconsistências na área central, mesmo sem nenhum item ainda");

        // Classificar por tipo de mídia (§7.4) ainda não existe na UI —
        // fora do ponto de parada desta etapa (item 6 em diante). Simula
        // aqui só pra confirmar que o motor de detecção (Source/Ingest/
        // PainelInconsistencias.cpp) continua reagindo assim que um item
        // tem tipo_midia='release' sem capa, não importa como ele chegou
        // lá — o item entra sempre com tipo_midia NULL agora (§7.1).
        mainComponentCatalog.ingerirArquivos({audio});
        esperarIngestTerminar(mainComponentCatalog);
        matriz::db::Database& registroPainel = mainComponentCatalog.projetoAberto()->projeto().registro();
        registroPainel.run("UPDATE item SET tipo_midia = 'release'", {});

        PainelInconsistenciasComponent painelDireto(*mainComponentCatalog.projetoAberto());
        painelDireto.recarregar();
        checar(painelDireto.totalInconsistencias() > 0,
               "release sem capa é detectado assim que o item é classificado (" +
                   std::to_string(painelDireto.totalInconsistencias()) + " inconsistência(s))");

        matriz::model::NovoProjetoParams paramsPainelArchive;
        paramsPainelArchive.nome = "Teste painel archive";
        paramsPainelArchive.modo = matriz::model::Modo::Preservacao;
        paramsPainelArchive.prefixoNomenclatura = "PLA";
        auto projetoPainelArchive =
            matriz::model::Project::criar(tmpRoot.getChildFile("projeto_painel_archive"), paramsPainelArchive);
        MainComponent mainComponentArchive;
        mainComponentArchive.abrirProjeto(std::move(projetoPainelArchive));
        checar(!mainComponentArchive.temPainelInconsistencias(),
               "modo acervo não tem posição fixa pro painel de inconsistências nesta etapa");

        matriz::model::NovoProjetoParams paramsArchive;
        paramsArchive.nome = "Teste acervo";
        paramsArchive.modo = matriz::model::Modo::Preservacao;
        paramsArchive.prefixoNomenclatura = "ARC";
        auto projetoArchive = matriz::model::Project::criar(tmpRoot.getChildFile("projeto_archive"), paramsArchive);
        ProjetoAberto abertoArchive(std::move(projetoArchive));
        auto tiposArchive = listarTiposMidiaDisponiveis(abertoArchive);
        checar(tiposArchive.size() == 14, "modo acervo continua oferecendo os 14 tipos (" +
                                                juce::String(static_cast<int>(tiposArchive.size())).toStdString() + ")");

    } catch (const std::exception& e) {
        checar(false, juce::String("teste da ponte de ingest: ") + e.what());
    }

    tmpRoot.deleteRecursively();

    std::cout << "\n" << (falhas == 0 ? "TODOS OS TESTES PASSARAM" : juce::String(falhas) + " FALHA(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
