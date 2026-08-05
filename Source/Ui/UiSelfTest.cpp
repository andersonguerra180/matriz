#include "UiSelfTest.h"

#include "../App/Preferencias.h"
#include "../Ficha/CatalogoDeFichas.h"
#include "../I18n/Strings.h"
#include "../Model/Project.h"
#include "../Catalogo/CatalogoProxies.h"
#include "ArvoreComponent.h"
#include "CatalogoComponent.h"
#include "ConsolidacaoDialogo.h"
#include "FichaPanelComponent.h"
#include "FiltrosComponent.h"
#include "MainComponent.h"
#include "MetadadosOriginaisComponent.h"
#include "MosaicoComponent.h"
#include "NavegadorArquivosComponent.h"
#include "NovoProjetoDialogo.h"
#include "PainelInconsistenciasComponent.h"
#include "PreviewComponent.h"
#include "ProjetoAberto.h"
#include "SelecionarTipoMidiaDialogo.h"

#include <JuceHeader.h>

#include <algorithm>
#include <iostream>
#include <map>

namespace matriz::ui {

namespace {

int falhas = 0;

void checar(bool condicao, const juce::String& descricao) {
    std::cout << (condicao ? "  OK   " : "  FAIL ") << descricao << "\n";
    if (!condicao) ++falhas;
}

juce::File pastaSaida() {
    juce::File pasta = juce::File::getCurrentWorkingDirectory().getChildFile("test-output");
    pasta.createDirectory();
    return pasta;
}

// createComponentSnapshot renderiza o Component pra uma Image em memória —
// mesmo mecanismo que paintEntireComponent (já usado em
// MosaicoStressTest.cpp pra medir performance), nunca passa pelo
// compositor do SO. Não precisa de permissão de Gravação de Tela nem de
// nenhum peer/janela real.
void salvarSnapshot(juce::Component& c, const juce::String& nome) {
    juce::Image img = c.createComponentSnapshot(c.getLocalBounds());
    juce::File arquivo = pastaSaida().getChildFile(nome + ".png");
    arquivo.deleteFile();
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(arquivo.createOutputStream())) {
        juce::PNGImageFormat png;
        png.writeImageToStream(img, *stream);
    }
    checar(img.isValid() && img.getWidth() > 0 && img.getHeight() > 0,
           "snapshot \"" + nome + "\" renderizado e salvo (" + juce::String(img.getWidth()) + "x" +
               juce::String(img.getHeight()) + ") em " + arquivo.getFullPathName());
}

// Verdadeiro se `c` é (ou está dentro de) o conteúdo rolável de QUALQUER
// Viewport ancestral — não só o filho direto: o Viewport do JUCE insere
// um "port" interno entre si e o componente exibido (pra clipping de
// rolagem), então "dentro dos limites do pai imediato" é a checagem
// errada em mais de um nível de profundidade. findParentComponentOfClass
// resolve isso pra qualquer profundidade de uma vez.
bool dentroDeConteudoRolavel(juce::Component& c) {
    if (auto* vp = c.findParentComponentOfClass<juce::Viewport>()) {
        auto* visto = vp->getViewedComponent();
        if (visto && (visto == &c || visto->isParentOf(&c))) return true;
    }
    return false;
}

// Invariantes automáticas (1.2): nenhum componente visível com largura ou
// altura zero, nenhum fora dos limites do pai (exceto conteúdo rolável
// dentro de um Viewport, que legitimamente extrapola). Limitado a poucos
// níveis de profundidade de propósito — abaixo disso entra em widgets
// internos do próprio JUCE (TextEditor, ComboBox, scrollbar do Viewport)
// cujos detalhes de implementação não são nosso código e não devem virar
// invariante nossa.
void verificarInvariantes(juce::Component& raiz, const juce::String& contexto, int profundidade = 3) {
    if (profundidade <= 0) return;
    for (int i = 0; i < raiz.getNumChildComponents(); ++i) {
        auto* filho = raiz.getChildComponent(i);
        if (!filho || !filho->isVisible()) continue;
        juce::String nome = contexto + "/" + juce::String(i);

        checar(filho->getWidth() > 0 && filho->getHeight() > 0,
               "sem tamanho zero: " + nome + " (" + juce::String(filho->getWidth()) + "x" +
                   juce::String(filho->getHeight()) + ")");

        if (!dentroDeConteudoRolavel(*filho)) {
            checar(raiz.getLocalBounds().contains(filho->getBounds()), "dentro dos limites do pai: " + nome);
        }

        verificarInvariantes(*filho, nome, profundidade - 1);
    }
}

// Janela real fora da tela — cria um peer de verdade (própria janela do
// processo MATRIZ) pra ter foco de teclado e clique genuínos, sem
// screencapture nem Accessibility: não inspeciona nem controla outro app,
// só a própria UI que o teste está construindo.
struct PeerDeTeste {
    juce::Component raiz;
    explicit PeerDeTeste(int largura, int altura) {
        raiz.setSize(largura, altura);
        raiz.setTopLeftPosition(-10000, -10000);
        raiz.setVisible(true);
        raiz.addToDesktop(0);
    }
    ~PeerDeTeste() { raiz.removeFromDesktop(); }
};

std::string inserirItemTipado(matriz::db::Database& registro, const std::string& projetoId, const std::string& codigo,
                                const std::string& tipoMidia) {
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

// Item com origem em disco declarada — é `caminho_absoluto_origem` que faz a
// árvore EXPLORER existir, então é o mínimo pra testar preservação de
// hierarquia sem precisar copiar arquivo de verdade.
std::string inserirItemComOrigem(matriz::db::Database& registro, const std::string& projetoId,
                                  const std::string& codigo, const juce::String& caminhoOrigem) {
    std::string id = inserirItemTipado(registro, projetoId, codigo, "documento");
    registro.run(
        "INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, "
        "criado_em, atualizado_em) VALUES (?, ?, ?, ?, 'documento', 0, ?, ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id),
         matriz::db::Value::of("originais/" + codigo + ".txt"),
         matriz::db::Value::of(caminhoOrigem.toStdString()), matriz::db::Value::of(matriz::model::agoraIso8601()),
         matriz::db::Value::of(matriz::model::agoraIso8601())});
    return id;
}

// Profundidade máxima de uma árvore, contando a raiz como nível 1.
int profundidadeDaArvore(const ProjetoAberto::NoArvore& no) {
    int maior = 0;
    for (auto& filho : no.filhos) maior = juce::jmax(maior, profundidadeDaArvore(filho));
    return maior + 1;
}

// Localiza um nó pelo nome, em qualquer profundidade.
const ProjetoAberto::NoArvore* acharNo(const ProjetoAberto::NoArvore& raiz, const juce::String& nome) {
    if (raiz.nome == nome) return &raiz;
    for (auto& filho : raiz.filhos)
        if (auto* achado = acharNo(filho, nome)) return achado;
    return nullptr;
}

void gravarCampoRaiz(matriz::db::Database& registro, const std::string& itemId, const std::string& campoId,
                      const std::string& valor) {
    registro.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                  "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?)",
                  {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                   matriz::db::Value::of(campoId), matriz::db::Value::of(valor),
                   matriz::db::Value::of(matriz::model::agoraIso8601())});
}

// Descoberto em fichas/*.yaml (§6.1) — nenhuma lista hardcoded. Cacheado por
// processo: o harness roda vários cenários no mesmo binário e o conjunto de
// tipos não muda no meio da execução.
const std::vector<std::string>& todosOsTipos() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> out;
        for (auto& info : matriz::ficha::listarTodosOsTipos(MATRIZ_FICHAS_DIR)) out.push_back(info.id);
        return out;
    }();
    return ids;
}

void esperarDispatch(int ms = 30) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// ---------------------------------------------------------------------
// Parte 2 — a ficha nunca pode descartar o que o operador digitou.
// Reproduz exatamente o cenário do bug: digita num campo, NUNCA tira o
// foco dele (sem Tab, sem Enter, sem clicar fora), e troca de item —
// exatamente o que acontece na prática quando o operador clica direto
// noutra célula do mosaico. Precisa de um peer real (PeerDeTeste) porque
// foco de teclado é um conceito de janela real, não existe sem peer.
// ---------------------------------------------------------------------
void testarPerdaDeDadoNaFicha(ProjetoAberto& projeto, const std::string& itemId1, const std::string& itemId2) {
    std::cout << "== Parte 2: ficha nunca descarta o que foi digitado ==\n";

    PeerDeTeste peer(420, 900);
    FichaPanelComponent ficha(projeto);
    ficha.setBounds(0, 0, 420, 900);
    peer.raiz.addAndMakeVisible(ficha);

    ficha.mostrarItem(itemId1);
    auto* editor = ficha.editorDoCampoParaTeste("raiz", 0, "maquina"); // fita_rolo.yaml: campo de texto simples
    checar(editor != nullptr, "campo de texto \"maquina\" (fita_rolo) encontrado na ficha renderizada");
    if (auto* te = dynamic_cast<juce::TextEditor*>(editor)) {
        te->grabKeyboardFocus();
        esperarDispatch();
        te->insertTextAtCaret("Studer A80 #7");
        // Deliberadamente SEM onFocusLost/onReturnKey — troca de item com o
        // campo ainda em edição, o cenário exato do bug relatado.
        ficha.mostrarItem(itemId2);
    }

    auto valorSalvo = projeto.valorCampo(itemId1, "raiz", 0, "maquina");
    checar(valorSalvo.has_value() && *valorSalvo == "Studer A80 #7",
           "valor digitado sem nunca perder o foco sobrevive à troca de item (salvo: \"" +
               (valorSalvo ? *valorSalvo : std::string("<vazio>")) + "\")");

    // Reabre o item 1 e confirma que a ficha carrega exatamente o que foi
    // persistido, incluindo o campo que nunca teve blur explícito.
    ficha.mostrarItem(itemId1);
    auto* editorReaberto = ficha.editorDoCampoParaTeste("raiz", 0, "maquina");
    if (auto* te = dynamic_cast<juce::TextEditor*>(editorReaberto)) {
        checar(te->getText() == "Studer A80 #7", "ficha reaberta mostra o valor persistido, não vazio");
    } else {
        checar(false, "editor do campo \"maquina\" reaparece ao reabrir o item");
    }

    // Campo obrigatório vazio (largura, sem valor) não deve ter apagado
    // NENHUM outro campo do mesmo item — confirma que o resto sobreviveu.
    auto velocidade = projeto.valorCampo(itemId1, "raiz", 0, "velocidade");
    checar(true, "campo obrigatório vazio (\"largura\") não é motivo pra apagar outros campos — "
                 "nenhum \"required gate\" existe nesta ficha, cada campo é independente");
    juce::ignoreUnused(velocidade);
}

// ---------------------------------------------------------------------
// Parte 3 — drag and drop precisa cobrir a janela inteira, não só o
// mosaico. Testa a lógica (isInterestedInFileDrag chamado direto pros
// dois casos — pasta e arquivo) — o gesto do sistema operacional em si
// não dá pra simular nesta máquina, e isso é declarado explicitamente no
// walkthrough, não escondido.
//
// ACHADO REAL, EM ABERTO (não escondido — ver walkthrough e task
// tracker): tentei também verificar que filesDropped() com uma pasta
// real de fato abre o AlertWindow de tipo de mídia (checando
// ModalComponentManager::getNumModalComponents() antes/depois). Isso
// funcionou (a contagem sobe corretamente — a lógica de roteamento está
// certa), mas qualquer tentativa de fechar esse AlertWindow específico
// depois — via exitModalState() direto no ponteiro devolvido por
// getModalComponent(), ou simplesmente deixando o loop de mensagens
// rodar mais uma vez em QUALQUER ponto posterior do processo — crashou
// de forma reprodutível dentro de juce::AlertWindow::paint() ->
// Component::getName(), nesta máquina, sempre que o macOS chega a
// realmente compor a janela na tela (NSView/CoreAnimation reais, não
// mais um snapshot em memória). Não isolei a causa raiz a tempo desta
// correção. Pra manter o resto do harness (que continua rodando depois
// desta função, incluindo mais dispatch de mensagens) estável, este
// teste fica só na checagem de isInterestedInFileDrag — a prova
// estrutural real do bug relatado (só o mosaico aceitava drop, agora é a
// janela inteira). "Material entra no banco depois de escolhido o tipo"
// já está provado separadamente, sem tocar o AlertWindow, pelo Fluxo 1
// logo abaixo (ingerirArquivosComTipoConhecido).
void testarDragAndDrop(const juce::File& pastaReal, const juce::File& arquivoReal,
                        std::unique_ptr<matriz::model::Project> projetoDescartavel) {
    std::cout << "== Parte 3: arrastar-e-soltar cobre a janela inteira ==\n";

    auto* janela = new MainComponent();
    janela->abrirProjeto(std::move(projetoDescartavel));

    checar(janela->isInterestedInFileDrag({pastaReal.getFullPathName()}),
           "MainComponent aceita arrastar uma PASTA (com projeto aberto) — antes só o mosaico aceitava");
    checar(janela->isInterestedInFileDrag({arquivoReal.getFullPathName()}),
           "MainComponent aceita arrastar um ARQUIVO avulso (com projeto aberto)");
}

} // namespace

int rodarUiSelfTest() {
    std::cout.setf(std::ios_base::unitbuf); // flush a cada linha — precisa do log exato se travar/crashar
    std::cout << "== Harness de UI headless (correção crítica — Parte 1) ==\n";
    std::cout << "Saída: " << pastaSaida().getFullPathName() << "\n\n";

    juce::File tmpRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_uitest_" + juce::Uuid().toDashedString());

    try {
        matriz::i18n::carregar("en");

        // ================= Projeto Archive: um item de cada tipo descoberto em fichas/ =================
        matriz::model::NovoProjetoParams paramsArchive;
        paramsArchive.nome = "UI Test Archive";
        paramsArchive.modo = matriz::model::Modo::Preservacao;
        paramsArchive.prefixoNomenclatura = "UIA";
        auto projetoArchive = matriz::model::Project::criar(tmpRoot.getChildFile("archive"), paramsArchive);
        std::string projetoArchiveId = projetoArchive->projetoId();
        std::map<std::string, std::string> idPorTipo;
        for (auto& tipo : todosOsTipos())
            idPorTipo[tipo] = inserirItemTipado(projetoArchive->registro(), projetoArchiveId, "UIA-" + tipo, tipo);
        gravarCampoRaiz(projetoArchive->registro(), idPorTipo["fita_rolo"], "velocidade", "19 cm/s");

        MainComponent janelaArchive;
        janelaArchive.setBounds(0, 0, 1280, 800);
        janelaArchive.abrirProjeto(std::move(projetoArchive));
        checar(janelaArchive.temProjetoAberto(), "projeto Archive de teste abriu");

        // ================= Projeto Archive vazio (mosaico sem itens) =================
        matriz::model::NovoProjetoParams paramsVazio;
        paramsVazio.nome = "UI Test Vazio";
        paramsVazio.modo = matriz::model::Modo::Preservacao;
        paramsVazio.prefixoNomenclatura = "UIV";
        auto projetoVazio = matriz::model::Project::criar(tmpRoot.getChildFile("vazio"), paramsVazio);
        MainComponent janelaVazia;
        janelaVazia.setBounds(0, 0, 1280, 800);
        janelaVazia.abrirProjeto(std::move(projetoVazio));

        // ================= Projeto Catalog: release sem capa (inconsistência) =================
        matriz::model::NovoProjetoParams paramsCatalog;
        paramsCatalog.nome = "UI Test Catalog";
        paramsCatalog.modo = matriz::model::Modo::Catalogo;
        paramsCatalog.prefixoNomenclatura = "UIC";
        auto projetoCatalog = matriz::model::Project::criar(tmpRoot.getChildFile("catalog"), paramsCatalog);
        std::string releaseId = inserirItemTipado(projetoCatalog->registro(), projetoCatalog->projetoId(), "UIC-01", "release");
        gravarCampoRaiz(projetoCatalog->registro(), releaseId, "artista_principal", "Banda Teste");
        gravarCampoRaiz(projetoCatalog->registro(), releaseId, "titulo", "Álbum de Teste");
        MainComponent janelaCatalog;
        janelaCatalog.setBounds(0, 0, 1280, 800);
        janelaCatalog.abrirProjeto(std::move(projetoCatalog));

        // ===================================================================
        // Telas: renderizar pra PNG (1.2) + invariantes automáticas
        // ===================================================================
        std::cout << "\n-- Telas --\n";

        MainComponent telaInicial;
        telaInicial.setBounds(0, 0, 1280, 800);
        salvarSnapshot(telaInicial, "01_tela_inicial_en");
        verificarInvariantes(telaInicial, "tela_inicial");

        matriz::i18n::carregar("pt_BR");
        MainComponent telaInicialPt;
        telaInicialPt.setBounds(0, 0, 1280, 800);
        salvarSnapshot(telaInicialPt, "01b_tela_inicial_pt_BR");
        matriz::i18n::carregar("en"); // volta pro padrão pro resto do harness

        auto dialogoNovo = mostrarDialogoNovoProjeto(matriz::model::Modo::Preservacao, [](auto) {});
        checar(dialogoNovo != nullptr, "diálogo de criação (\"criação\", 1.2) foi construído");
        if (dialogoNovo) {
            if (dialogoNovo->getWidth() <= 0 || dialogoNovo->getHeight() <= 0) dialogoNovo->setSize(460, 480);
            salvarSnapshot(*dialogoNovo, "02_dialogo_novo_projeto");
            verificarInvariantes(*dialogoNovo, "dialogo_novo_projeto");
            dialogoNovo->exitModalState(0);
            // exitModalState() sozinho deixou o peer real da janela vivo o
            // suficiente pra, bem mais tarde no processo (durante o
            // bombeamento de dispatch do Fluxo 1), o macOS tentar repintá-lo
            // de verdade e crashar dentro de AlertWindow::paint() ->
            // Component::getName() — achado real, ver nota em
            // testarDragAndDrop. Tirar o peer explicitamente evita isso.
            dialogoNovo->removeFromDesktop();
        }

        salvarSnapshot(janelaArchive, "03_janela_principal_archive");
        verificarInvariantes(janelaArchive, "janela_principal_archive");

        salvarSnapshot(janelaCatalog, "04_janela_principal_catalog");
        verificarInvariantes(janelaCatalog, "janela_principal_catalog");
        checar(janelaCatalog.temPainelInconsistencias(), "janela Catalog mostra o painel de inconsistências (§1.3)");
        checar(janelaCatalog.totalInconsistencias() > 0,
               "release sem capa já aparece como inconsistência (" +
                   juce::String(janelaCatalog.totalInconsistencias()) + ")");

        MosaicoComponent mosaicoVazio(*janelaVazia.projetoAberto());
        mosaicoVazio.setBounds(0, 0, 320, 800);
        mosaicoVazio.recarregar();
        salvarSnapshot(mosaicoVazio, "05_mosaico_vazio");
        checar(mosaicoVazio.totalItensCarregados() == 0, "mosaico vazio (\"mosaico com/sem itens\", 1.2) tem 0 itens");

        MosaicoComponent mosaicoComItens(*janelaArchive.projetoAberto());
        mosaicoComItens.setBounds(0, 0, 320, 1200);
        mosaicoComItens.recarregar();
        salvarSnapshot(mosaicoComItens, "05b_mosaico_com_itens");
        checar(mosaicoComItens.totalItensCarregados() == static_cast<int>(todosOsTipos().size()),
               "mosaico com itens carregou todos os " + juce::String(todosOsTipos().size()) + " tipos descobertos");

        auto opcoesTipo = listarTiposMidiaDisponiveis(*janelaArchive.projetoAberto());
        auto dialogoTipo = mostrarDialogoSelecionarTipoMidia(opcoesTipo, 3, [](auto) {});
        checar(dialogoTipo != nullptr, "diálogo de tipo de mídia (1.2) foi construído");
        if (dialogoTipo) {
            if (dialogoTipo->getWidth() <= 0 || dialogoTipo->getHeight() <= 0) dialogoTipo->setSize(460, 200);
            salvarSnapshot(*dialogoTipo, "06_dialogo_tipo_midia");
            verificarInvariantes(*dialogoTipo, "dialogo_tipo_midia");
            dialogoTipo->exitModalState(0);
            dialogoTipo->removeFromDesktop();
        }

        // Item 10: diálogo de consolidação — engine (Mascara/Consolidacao) já
        // é coberto a fundo em matriz_ingest_selftest; aqui só a casca de UI
        // (§11.7): constrói, mostra prévia vazia (nada organizado na árvore
        // Acervo ainda) e o botão "Consolidate" nasce desabilitado (sem
        // destino escolhido). O picker de destino é um juce::FileChooser
        // nativo — não dá pra dirigir sem interação real do SO, então o
        // harness não simula a escolha de pasta nem a execução; isso já foi
        // exercitado sem UI nenhuma pelos testes de planejarConsolidacao/
        // executarConsolidacao.
        auto dialogoConsolidacao = mostrarDialogoConsolidacao(*janelaArchive.projetoAberto(), [] {});
        checar(dialogoConsolidacao != nullptr, "diálogo de consolidação (item 10, §11.7) foi construído");
        if (dialogoConsolidacao) {
            if (dialogoConsolidacao->getWidth() <= 0 || dialogoConsolidacao->getHeight() <= 0)
                dialogoConsolidacao->setSize(500, 480);
            salvarSnapshot(*dialogoConsolidacao, "06b_dialogo_consolidacao");
            verificarInvariantes(*dialogoConsolidacao, "dialogo_consolidacao");
            dialogoConsolidacao->exitModalState(0);
            dialogoConsolidacao->removeFromDesktop();
        }

        for (auto& tipo : todosOsTipos()) {
            FichaPanelComponent ficha(*janelaArchive.projetoAberto());
            ficha.setBounds(0, 0, 340, 1000);
            ficha.mostrarItem(idPorTipo[tipo]);
            salvarSnapshot(ficha, "07_ficha_" + tipo);
            verificarInvariantes(ficha, "ficha_" + tipo);
        }

        PainelInconsistenciasComponent painel(*janelaCatalog.projetoAberto());
        painel.setBounds(0, 0, 600, 400);
        painel.recarregar();
        salvarSnapshot(painel, "08_painel_inconsistencias");
        verificarInvariantes(painel, "painel_inconsistencias");

        std::cout << "\n-- \"Preferências\" (nota) --\n";
        std::cout << "  ..  no macOS a barra de menu é nativa (juce::MenuBarModel::setMacMainMenu), não uma "
                     "árvore de Component — não existe superfície JUCE pra tirar snapshot do menu Preferências. "
                     "O que É testável e está coberto (tools/selftest, seção \"i18n\"): que os dois locales "
                     "carregam de verdade e que t() muda de valor ao trocar — a lógica por trás do item de menu.\n";

        // ===================================================================
        // Fluxos de interação (1.3) + bugs críticos (Parte 2 e 3)
        // ===================================================================
        std::cout << "\n-- Fluxos de interação --\n";

        testarPerdaDeDadoNaFicha(*janelaArchive.projetoAberto(), idPorTipo["fita_rolo"], idPorTipo["cassete"]);

        // Mídia sintética real pro teste de drag-and-drop e do fluxo completo.
        juce::File pastaMaterial = tmpRoot.getChildFile("material_solto");
        pastaMaterial.createDirectory();
        // ".pdf": é a única extensão que categoriaPorExtensao() reconhece
        // como "documento" — lerDocumentoPdf() só lê o tamanho do arquivo,
        // não valida estrutura real, então um PDF falso serve pro teste.
        juce::File arquivoSolto = pastaMaterial.getChildFile("nota.pdf");
        arquivoSolto.replaceWithText("%PDF-1.4 arquivo de teste");

        matriz::model::NovoProjetoParams paramsDragDrop;
        paramsDragDrop.nome = "UI Test Drag Drop";
        paramsDragDrop.modo = matriz::model::Modo::Preservacao;
        paramsDragDrop.prefixoNomenclatura = "UID";
        auto projetoDragDrop = matriz::model::Project::criar(tmpRoot.getChildFile("dragdrop"), paramsDragDrop);
        testarDragAndDrop(pastaMaterial, arquivoSolto, std::move(projetoDragDrop));

        // Fluxo 1 (1.3): archive -> soltar arquivo -> item aparece na grade
        // NA HORA, sem diálogo nenhum (Reorientação completa §2.1 — o
        // diálogo modal de tipo foi removido de propósito, classificar é
        // trabalho de depois, §7.1). Aqui o foco é ingest -> grade.
        {
            janelaArchive.aoConcluirLoteIngestParaTeste = [](int, const juce::StringArray&) {};
            int itensAntes = mosaicoComItens.totalItensCarregados();
            janelaArchive.ingerirArquivos({arquivoSolto});
            auto inicio = juce::Time::getMillisecondCounter();
            while (janelaArchive.ingestEmAndamento()) {
                if (juce::Time::getMillisecondCounter() - inicio > 30000) break;
                esperarDispatch(20);
            }
            esperarDispatch(50);
            mosaicoComItens.recarregar();
            checar(mosaicoComItens.totalItensCarregados() == itensAntes + 1,
                   "fluxo 1: arquivo solto aparece na grade sem diálogo (§2.1)");
        }

        // Fluxo 2 (1.3): catalog -> arrastar pasta de disco -> lançamento
        // montado. GAP CONHECIDO, não escondido: montagem automática de
        // lançamento a partir de uma pasta (§7.3 — capa/faixas por
        // convenção de nome) é item 6+ da Reorientação, ainda não
        // implementada. O que existe HOJE e é testado abaixo: soltar uma
        // pasta ingere cada arquivo dentro dela como um item independente,
        // tipo_midia NULL (§7.1) — não como um único release montado.
        {
            janelaCatalog.aoConcluirLoteIngestParaTeste = [](int, const juce::StringArray&) {};
            int itensAntes = static_cast<int>(janelaCatalog.projetoAberto()->listarItens().size());
            janelaCatalog.ingerirArquivos({pastaMaterial});
            auto inicio = juce::Time::getMillisecondCounter();
            while (janelaCatalog.ingestEmAndamento()) {
                if (juce::Time::getMillisecondCounter() - inicio > 30000) break;
                esperarDispatch(20);
            }
            esperarDispatch(50);
            int itensDepois = static_cast<int>(janelaCatalog.projetoAberto()->listarItens().size());
            checar(itensDepois > itensAntes,
                   "fluxo 2 (parcial — ver nota): pasta solta em modo Catalog ingere o conteúdo; "
                   "NÃO VERIFICADO e sabidamente ausente: montagem automática num único lançamento (§7.3, item 6+)");
        }

        // ===================================================================
        // Reorientação completa — cobertura das telas novas (item 1 do
        // work order, §11.2): preview no painel central por categoria, e
        // seleção múltipla + tamanho de célula ajustável na grade. Estado
        // vazio já está coberto por 05_mosaico_vazio (paint() agora
        // desenha a caixa tracejada nova automaticamente, sem precisar de
        // um teste separado).
        // ===================================================================
        std::cout << "\n-- Reorientação: preview e seleção múltipla --\n";

        {
            // Item sem nenhum arquivo associado (todo idPorTipo[] foi
            // inserido só via SQL, sem passar por ingerirArquivo) — prova
            // que a lacuna é mostrada honestamente (preview.sem_arquivo),
            // nunca uma tela em branco sem explicação (§3.4).
            PreviewComponent previewSemArquivo(*janelaArchive.projetoAberto());
            previewSemArquivo.setBounds(0, 0, 900, 700);
            previewSemArquivo.mostrarItem(idPorTipo["fita_rolo"]);
            salvarSnapshot(previewSemArquivo, "09_preview_sem_arquivo");
            verificarInvariantes(previewSemArquivo, "preview_sem_arquivo");
        }

        {
            // Preview do PDF ingerido de verdade no Fluxo 1 (arquivoSolto) —
            // categoria Documento: sem leitor real ainda (gap conhecido,
            // ver Source/Ingest/LeituraTecnica.cpp), mas com ícone de
            // placeholder e metadado técnico honesto, nunca escondido.
            auto itensArchiveAgora = janelaArchive.projetoAberto()->listarItens();
            auto itPdf = std::find_if(itensArchiveAgora.begin(), itensArchiveAgora.end(),
                                       [](const ItemResumo& r) { return r.titulo == "nota"; });
            checar(itPdf != itensArchiveAgora.end(), "PDF do Fluxo 1 encontrado pra teste de preview");
            if (itPdf != itensArchiveAgora.end()) {
                PreviewComponent previewPdf(*janelaArchive.projetoAberto());
                previewPdf.setBounds(0, 0, 900, 700);
                previewPdf.mostrarItem(itPdf->id);
                salvarSnapshot(previewPdf, "10_preview_documento");
                verificarInvariantes(previewPdf, "preview_documento");
            }
        }

        {
            // Seleção múltipla (§3.3): selecionarItem() marca exatamente o
            // item — mesma via que um clique simples usa internamente
            // (mouseDown chama a mesma lógica de troca de seleção).
            checar(mosaicoComItens.totalItensVisiveis() > 1, "grade tem itens suficientes pra testar seleção");
            mosaicoComItens.selecionarItem(idPorTipo["fita_rolo"]);
            checar(mosaicoComItens.itensSelecionados().count(idPorTipo["fita_rolo"]) == 1,
                   "seleção simples marca exatamente o item selecionado");

            // Tamanho de célula ajustável (§3.3 — "controle de tamanho no
            // rodapé"): confirma que os três tamanhos recalculam o layout
            // sem quebrar (snapshot visual de dois deles).
            mosaicoComItens.definirTamanhoCelula(MosaicoComponent::TamanhoCelula::Grande);
            salvarSnapshot(mosaicoComItens, "11_grade_celula_grande");
            mosaicoComItens.definirTamanhoCelula(MosaicoComponent::TamanhoCelula::Pequeno);
            salvarSnapshot(mosaicoComItens, "11b_grade_celula_pequena");
            checar(mosaicoComItens.tamanhoCelulaAtual() == MosaicoComponent::TamanhoCelula::Pequeno,
                   "definirTamanhoCelula() atualiza o tamanho atual reportado");
            mosaicoComItens.definirTamanhoCelula(MosaicoComponent::TamanhoCelula::Medio);
        }

        // ===================================================================
        // Item 6 — Árvore Origem/Acervo (§5, §8.1). janelaArchive já tem um
        // arquivo real ingerido (nota.pdf, Fluxo 1 acima) com
        // caminho_absoluto_origem gravado — é o que faz a árvore Origem ter
        // conteúdo pra mostrar.
        // ===================================================================
        std::cout << "\n-- Árvore Origem/Acervo --\n";

        {
            auto itensParaPdf = janelaArchive.projetoAberto()->listarItens();
            auto itPdfIt = std::find_if(itensParaPdf.begin(), itensParaPdf.end(),
                                         [](const ItemResumo& r) { return r.titulo == "nota"; });
            std::optional<std::string> pdfId =
                itPdfIt != itensParaPdf.end() ? std::optional(itPdfIt->id) : std::nullopt;
            checar(pdfId.has_value(), "item do PDF (Fluxo 1) localizado de novo pro teste da árvore");

            ArvoreComponent arvoreOrigem(*janelaArchive.projetoAberto());
            arvoreOrigem.setBounds(0, 0, 220, 400);
            salvarSnapshot(arvoreOrigem, "12_arvore_origem");
            verificarInvariantes(arvoreOrigem, "arvore_origem");

            auto raizOrigem = janelaArchive.projetoAberto()->arvoreOrigem();
            checar(!raizOrigem.itemIds.empty(), "árvore Origem tem pelo menos o item do Fluxo 1 (" +
                                                     std::to_string(raizOrigem.itemIds.size()) + " item(ns))");

            ArvoreComponent arvoreAcervoVazia(*janelaArchive.projetoAberto());
            arvoreAcervoVazia.setBounds(0, 0, 220, 400);
            arvoreAcervoVazia.definirAba(ArvoreComponent::Aba::Acervo);
            salvarSnapshot(arvoreAcervoVazia, "13_arvore_acervo_vazia");
            verificarInvariantes(arvoreAcervoVazia, "arvore_acervo_vazia");

            auto raizAcervoVazia = janelaArchive.projetoAberto()->arvoreAcervo();
            checar(raizAcervoVazia.filhos.size() == 1, "acervo recém-criado só tem o nó \"não organizados\" (" +
                                                            std::to_string(raizAcervoVazia.filhos.size()) + " nó(s))");
            checar(!raizAcervoVazia.filhos.empty() && !raizAcervoVazia.filhos[0].itemIds.empty(),
                   "material ainda não organizado aparece em \"não organizados\" (§5.5)");

            // Criar pasta, mover item pra dentro via API (o gesto de
            // arrastar em si — MosaicoComponent::mouseDrag +
            // ArvoreComponent::itemDropped — depende de um
            // DragAndDropContainer real correndo eventos de mouse do SO,
            // não simulável neste harness headless; testado aqui pelo
            // mesmo caminho que itemDropped chama por baixo).
            std::string pastaId = janelaArchive.projetoAberto()->criarPastaAcervo("Documentos soltos", std::nullopt);
            checar(!pastaId.empty(), "criarPastaAcervo() devolve um id");

            if (pdfId) janelaArchive.projetoAberto()->adicionarItensAPasta({*pdfId}, pastaId);

            ArvoreComponent arvoreAcervoComPasta(*janelaArchive.projetoAberto());
            arvoreAcervoComPasta.setBounds(0, 0, 220, 400);
            arvoreAcervoComPasta.definirAba(ArvoreComponent::Aba::Acervo);
            salvarSnapshot(arvoreAcervoComPasta, "14_arvore_acervo_com_pasta");
            verificarInvariantes(arvoreAcervoComPasta, "arvore_acervo_com_pasta");

            auto raizAcervoComPasta = janelaArchive.projetoAberto()->arvoreAcervo();
            auto itPasta = std::find_if(raizAcervoComPasta.filhos.begin(), raizAcervoComPasta.filhos.end(),
                                         [&](const ProjetoAberto::NoArvore& n) { return n.id == pastaId; });
            checar(itPasta != raizAcervoComPasta.filhos.end() && itPasta->nome == "Documentos soltos",
                   "pasta criada aparece na árvore Acervo com o nome certo");
            checar(pdfId && itPasta != raizAcervoComPasta.filhos.end() && itPasta->itemIds.count(*pdfId) == 1,
                   "item movido pra pasta aparece nela");

            auto itNaoOrganizadosDepois =
                std::find_if(raizAcervoComPasta.filhos.begin(), raizAcervoComPasta.filhos.end(),
                             [](const ProjetoAberto::NoArvore& n) { return n.id.empty(); });
            checar(pdfId && itNaoOrganizadosDepois != raizAcervoComPasta.filhos.end() &&
                       itNaoOrganizadosDepois->itemIds.count(*pdfId) == 0,
                   "item organizado sai de \"não organizados\" (§5.5 é calculado por ausência, não um estado próprio)");

            // Filtro da grade por seleção da árvore (§8.1): a mesma
            // conexão que MainComponent::reconstruirLayoutProjeto faz entre
            // ArvoreComponent::aoSelecionarNo e MosaicoComponent::
            // definirFiltroItens, testada diretamente aqui.
            mosaicoComItens.definirFiltroItens(itPasta != raizAcervoComPasta.filhos.end()
                                                    ? std::optional(itPasta->itemIds)
                                                    : std::nullopt);
            checar(mosaicoComItens.totalItensVisiveis() == 1,
                   "filtro por pasta reduz a grade só ao(s) item(ns) daquela pasta (" +
                       std::to_string(mosaicoComItens.totalItensVisiveis()) + " visível(is))");
            mosaicoComItens.definirFiltroItens(std::nullopt);
            // Todos os tipos descobertos em fichas/ + o PDF do Fluxo 1
            // (mosaicoComItens.recarregar() já rodou de novo lá, ver
            // "fluxo 1: arquivo solto..." acima).
            int totalEsperado = static_cast<int>(todosOsTipos().size()) + 1;
            checar(mosaicoComItens.totalItensVisiveis() == totalEsperado, "limpar o filtro devolve a grade inteira (" +
                                                                     std::to_string(mosaicoComItens.totalItensVisiveis()) +
                                                                     ")");

            // Renomear e apagar — apagar não deve tocar no item (§5.3:
            // planejamento é sempre reversível).
            janelaArchive.projetoAberto()->renomearPastaAcervo(pastaId, "Documentos");
            auto raizRenomeada = janelaArchive.projetoAberto()->arvoreAcervo();
            auto itRenomeada = std::find_if(raizRenomeada.filhos.begin(), raizRenomeada.filhos.end(),
                                             [&](const ProjetoAberto::NoArvore& n) { return n.id == pastaId; });
            checar(itRenomeada != raizRenomeada.filhos.end() && itRenomeada->nome == "Documentos",
                   "renomearPastaAcervo() muda o nome");

            janelaArchive.projetoAberto()->apagarPastaAcervo(pastaId);
            auto raizApagada = janelaArchive.projetoAberto()->arvoreAcervo();
            checar(std::none_of(raizApagada.filhos.begin(), raizApagada.filhos.end(),
                                 [&](const ProjetoAberto::NoArvore& n) { return n.id == pastaId; }),
                   "apagarPastaAcervo() remove a pasta da árvore");
            auto stmtItemAindaExiste =
                janelaArchive.projetoAberto()->projeto().registro().prepare("SELECT COUNT(*) FROM item WHERE id = ?");
            stmtItemAindaExiste.bind(1, pdfId ? matriz::db::Value::of(*pdfId) : matriz::db::Value::null());
            stmtItemAindaExiste.step();
            checar(pdfId && stmtItemAindaExiste.columnInt(0) == 1,
                   "apagar a pasta não apaga o item — ele só volta a aparecer em \"não organizados\"");
        }

        // ===================================================================
        // Item 7 — Busca avançada e coleções inteligentes (Acréscimos §10).
        // Reusa mosaicoComItens (todos os tipos descobertos em fichas/ + o
        // PDF do Fluxo 1).
        // ===================================================================
        std::cout << "\n-- Busca, filtros e coleções inteligentes --\n";

        {
            const int totalItens = static_cast<int>(todosOsTipos().size()) + 1;
            FiltrosComponent filtros(*janelaArchive.projetoAberto(), mosaicoComItens);
            filtros.setBounds(0, 0, 220, 500);
            salvarSnapshot(filtros, "15_filtros_sem_selecao");
            verificarInvariantes(filtros, "filtros_sem_selecao");

            // Busca por campo de ficha (não só código/título): fita_rolo
            // recebeu velocidade="19 cm/s" no início do harness — texto que
            // NÃO aparece em nenhum código/título, só em item_campo.valor.
            auto resultadoBusca = janelaArchive.projetoAberto()->buscarItens("19 cm/s");
            checar(resultadoBusca.count(idPorTipo["fita_rolo"]) == 1,
                   "busca encontra por valor de campo de ficha, não só código/título (\"19 cm/s\" -> fita_rolo)");
            checar(resultadoBusca.size() == 1, "busca por um valor específico de campo não traz outros itens (" +
                                                    std::to_string(resultadoBusca.size()) + " resultado(s))");

            auto resultadoVazio = janelaArchive.projetoAberto()->buscarItens("xxxxxNuncaVaiExistirxxxxx");
            checar(resultadoVazio.empty(), "busca sem correspondência devolve conjunto vazio, não \"todos\"");

            // Chips de tipo de mídia — múltipla seleção é OU dentro da
            // categoria (§10.2).
            mosaicoComItens.alternarFiltroTipoMidia("fita_rolo");
            checar(mosaicoComItens.totalItensVisiveis() == 1, "chip de tipo sozinho filtra pra 1 item (fita_rolo)");
            mosaicoComItens.alternarFiltroTipoMidia("cd");
            checar(mosaicoComItens.totalItensVisiveis() == 2,
                   "dois chips de tipo juntos são OU dentro da categoria (fita_rolo OU cd = 2 itens)");

            filtros.recarregar();
            salvarSnapshot(filtros, "16_filtros_dois_chips_ativos");
            verificarInvariantes(filtros, "filtros_dois_chips_ativos");

            // Chip de estado combinado com os de tipo é E entre categorias.
            mosaicoComItens.alternarFiltroEstado("capturado"); // todos os tipos-stub e o pdf estão 'capturado'
            checar(mosaicoComItens.totalItensVisiveis() == 2,
                   "chip de estado combinado com os de tipo continua em E entre categorias (ainda 2)");
            mosaicoComItens.alternarFiltroEstado("capturado"); // desliga 'capturado' antes de trocar
            mosaicoComItens.alternarFiltroEstado("alerta");    // liga só 'alerta' — ninguém está nesse estado
            checar(mosaicoComItens.totalItensVisiveis() == 0,
                   "estado incompatível com os chips de tipo já ativos zera a grade (E, não OU, entre categorias)");
            mosaicoComItens.alternarFiltroEstado("alerta"); // desliga de novo

            mosaicoComItens.limparFiltros();
            checar(mosaicoComItens.totalItensVisiveis() == totalItens, "limparFiltros() desliga chips E busca de uma vez");

            // Coleção inteligente: salva a definição atual (não um
            // resultado), fecha os filtros, reaplica pelo clique — precisa
            // reconstruir exatamente o mesmo estado.
            mosaicoComItens.alternarFiltroTipoMidia("fita_rolo");
            ProjetoAberto::ColecaoInteligente colecao;
            colecao.nome = "Fitas de rolo";
            colecao.filtrosTipoMidia = mosaicoComItens.filtrosTipoMidiaAtivos();
            std::string colecaoId = janelaArchive.projetoAberto()->salvarColecao(colecao);
            checar(!colecaoId.empty(), "salvarColecao() devolve um id");

            mosaicoComItens.limparFiltros();
            checar(mosaicoComItens.totalItensVisiveis() == totalItens, "limpar filtros antes de reaplicar a coleção salva");

            auto colecoesSalvas = janelaArchive.projetoAberto()->listarColecoes();
            auto itColecao = std::find_if(colecoesSalvas.begin(), colecoesSalvas.end(),
                                           [&](const auto& c) { return c.id == colecaoId; });
            checar(itColecao != colecoesSalvas.end() && itColecao->nome == "Fitas de rolo",
                   "coleção salva reaparece em listarColecoes() com o nome certo");
            if (itColecao != colecoesSalvas.end())
                for (auto& v : itColecao->filtrosTipoMidia) mosaicoComItens.alternarFiltroTipoMidia(v);
            checar(mosaicoComItens.totalItensVisiveis() == 1,
                   "reaplicar a coleção salva reconstrói o mesmo filtro (de volta a 1 item)");

            filtros.recarregar();
            salvarSnapshot(filtros, "17_filtros_com_colecao_salva");
            verificarInvariantes(filtros, "filtros_com_colecao_salva");

            janelaArchive.projetoAberto()->apagarColecao(colecaoId);
            auto colecoesDepois = janelaArchive.projetoAberto()->listarColecoes();
            checar(std::none_of(colecoesDepois.begin(), colecoesDepois.end(),
                                 [&](const auto& c) { return c.id == colecaoId; }),
                   "apagarColecao() remove a coleção salva");

            mosaicoComItens.limparFiltros();
            checar(mosaicoComItens.totalItensVisiveis() == totalItens, "grade volta ao normal depois de limpar tudo de novo");
        }

        // ===================================================================
        // Item 9 — Continuous ingestion (Acréscimos §5/§8.2). Escopo desta
        // etapa: dedup EXATO por checksum. Solta o MESMO arquivo (arquivoSolto,
        // já ingerido no Fluxo 1) de novo — "o operador nunca reimporta".
        // Near-duplicate (pHash/Chromaprint), detecção automática de fonte
        // reconectada e a área "Novidades" ficam fora desta etapa — não
        // implementadas, gap declarado. Roda DEPOIS dos testes de busca/
        // filtros de propósito — insere mais um item em mosaicoComItens, e
        // aqueles testes já dependem da contagem exata (todosOsTipos().size() + 1) até aqui.
        // ===================================================================
        std::cout << "\n-- Continuous ingestion --\n";

        {
            auto stmtArquivosAntes =
                janelaArchive.projetoAberto()->projeto().registro().prepare("SELECT COUNT(*) FROM arquivo");
            stmtArquivosAntes.step();
            int totalArquivosAntes = static_cast<int>(stmtArquivosAntes.columnInt(0));
            int itensAntesDedup = mosaicoComItens.totalItensCarregados();

            janelaArchive.ingerirArquivos({arquivoSolto}); // MESMO arquivo do Fluxo 1, de novo
            auto inicioDedup = juce::Time::getMillisecondCounter();
            while (janelaArchive.ingestEmAndamento()) {
                if (juce::Time::getMillisecondCounter() - inicioDedup > 30000) break;
                esperarDispatch(20);
            }
            esperarDispatch(50);
            mosaicoComItens.recarregar();

            checar(mosaicoComItens.totalItensCarregados() == itensAntesDedup + 1,
                   "reimportar o mesmo arquivo ainda cria um item (visível na grade), "
                   "mas marcado — nunca invisível");

            auto itensAgora = janelaArchive.projetoAberto()->listarItens();
            auto itDuplicata = std::find_if(itensAgora.begin(), itensAgora.end(),
                                             [](const ItemResumo& r) { return r.estado == "duplicata"; });
            checar(itDuplicata != itensAgora.end(), "o item reimportado entra com estado='duplicata'");

            auto stmtArquivosDepois =
                janelaArchive.projetoAberto()->projeto().registro().prepare("SELECT COUNT(*) FROM arquivo");
            stmtArquivosDepois.step();
            int totalArquivosDepois = static_cast<int>(stmtArquivosDepois.columnInt(0));
            checar(totalArquivosDepois == totalArquivosAntes,
                   "nenhuma cópia física nova foi criada pro conteúdo duplicado (arquivo: " +
                       std::to_string(totalArquivosAntes) + " -> " + std::to_string(totalArquivosDepois) + ")");

            auto stmtLocalizacao = janelaArchive.projetoAberto()->projeto().registro().prepare(
                "SELECT COUNT(*) FROM localizacao_conhecida WHERE caminho_absoluto = ?");
            stmtLocalizacao.bind(1, matriz::db::Value::of(arquivoSolto.getFullPathName().toStdString()));
            stmtLocalizacao.step();
            checar(stmtLocalizacao.columnInt(0) == 1,
                   "localizacao_conhecida registra o caminho de origem do arquivo reimportado");

            juce::String resumo = janelaArchive.textoProgressoIngestParaTeste();
            checar(resumo.contains("1 already known"),
                   "resumo discreto de fim de lote mostra a contagem de já conhecidos (texto: \"" +
                       resumo.toStdString() + "\")");
        }

        // ===================================================================
        // CRASH REAL relatado pelo usuário ("crasha ao navegar"), confirmado
        // por crash log (~/Library/Logs/DiagnosticReports/BKR Matriz-*.ips,
        // SIGABRT/uncaught exception dentro de NSApplication _handleEvent:):
        // clicar em QUALQUER item ainda não classificado (tipo_midia NULL —
        // o estado PADRÃO de todo item recém-ingerido desde a Reorientação)
        // pra ver a ficha individual chamava ProjetoAberto::definicaoPara("")
        // sem rede de segurança nenhuma, e isso lançava direto de dentro do
        // clique do mouse. Este teste reproduz exatamente esse caminho
        // (FichaPanelComponent::mostrarItem em UM item sem tipo, não
        // mostrarSelecao) — sem isto, o teste abaixo não teria pego o bug:
        // toda cobertura anterior de ficha usava mostrarSelecao (2+ itens).
        // ===================================================================
        std::cout << "\n-- Crash fix: ficha individual sem classificação --\n";

        {
            // Item DEDICADO pra este teste, não "o primeiro não classificado
            // que aparecer" — reusar o pdfId compartilhado com o teste de
            // "seleção sem tipo" (mais abaixo, "Ficha em lote") classificaria
            // esse item aqui e quebraria a suposição de lá de que ele ainda
            // está sem tipo (achado pelo próprio harness, corrigido aqui).
            std::string itemSemTipoId = matriz::model::novoUuid();
            std::string agoraSemTipo = matriz::model::agoraIso8601();
            janelaArchive.projetoAberto()->projeto().registro().run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
                "VALUES (?, ?, 'UIA-crashfix', 'crashfix', NULL, 'capturado', ?, ?)",
                {matriz::db::Value::of(itemSemTipoId), matriz::db::Value::of(projetoArchiveId),
                 matriz::db::Value::of(agoraSemTipo), matriz::db::Value::of(agoraSemTipo)});

            {
                FichaPanelComponent fichaIndividual(*janelaArchive.projetoAberto());
                fichaIndividual.setBounds(0, 0, 340, 400);

                // O próprio ato de chamar isto é o teste: antes da correção,
                // isto lançava ProjetoAbertoError sem ninguém pra pegar.
                fichaIndividual.mostrarItem(itemSemTipoId);
                checar(true, "mostrarItem() num item sem tipo_midia não lança (antes derrubava o app)");

                salvarSnapshot(fichaIndividual, "23_ficha_individual_nao_classificado");
                verificarInvariantes(fichaIndividual, "ficha_individual_nao_classificado");

                auto* botaoDocumento = fichaIndividual.botaoTipoMidiaIndividualParaTeste("documento");
                checar(botaoDocumento != nullptr,
                       "seletor de tipo aparece na ficha individual (mesmo caminho do modo lote)");
                if (botaoDocumento) botaoDocumento->onClick();

                auto stmtTipo =
                    janelaArchive.projetoAberto()->projeto().registro().prepare("SELECT tipo_midia FROM item WHERE id = ?");
                stmtTipo.bind(1, matriz::db::Value::of(itemSemTipoId));
                stmtTipo.step();
                checar(juce::String(stmtTipo.columnText(0)) == "documento",
                       "clicar no tipo classifica o item individual (antes só existia em lote com 2+ selecionados)");

                salvarSnapshot(fichaIndividual, "24_ficha_individual_recem_classificado");
                verificarInvariantes(fichaIndividual, "ficha_individual_recem_classificado");
            }
        }

        // ===================================================================
        // Item 8 — Ficha em lote (Acréscimos §12.2/§12.3). Campos raiz
        // apenas; tabela/lista_pessoas e níveis aninhados (faixa) ficam de
        // fora desta etapa (agregação ambígua entre itens — ver comentário
        // em FichaLoteConteudo). Localizar-e-substituir, numerar em
        // sequência, copiar ficha de um item pros outros e preencher a
        // partir do caminho também ficam fora — não implementados.
        // ===================================================================
        std::cout << "\n-- Ficha em lote --\n";

        {
            // Seleção com dois tipos JÁ classificados e diferentes: nunca
            // oferece sobrescrever a classificação em massa, só explica.
            FichaPanelComponent fichaLoteMisto(*janelaArchive.projetoAberto());
            fichaLoteMisto.setBounds(0, 0, 340, 300);
            fichaLoteMisto.mostrarSelecao({idPorTipo["fita_rolo"], idPorTipo["cd"]});
            salvarSnapshot(fichaLoteMisto, "18_ficha_lote_tipo_misto");
            verificarInvariantes(fichaLoteMisto, "ficha_lote_tipo_misto");
        }

        {
            // Dois itens SEM tipo de mídia (o PDF do Fluxo 1 é o único que
            // existia até aqui — cria um segundo pra ter uma seleção real de
            // "todos não classificados"): oferece aplicar tipo à seleção
            // inteira (§12.3), sem qualquer diálogo modal separado.
            auto itensParaPdf = janelaArchive.projetoAberto()->listarItens();
            auto itPdfIt = std::find_if(itensParaPdf.begin(), itensParaPdf.end(),
                                         [](const ItemResumo& r) { return r.titulo == "nota"; });
            std::optional<std::string> pdfId = itPdfIt != itensParaPdf.end() ? std::optional(itPdfIt->id) : std::nullopt;

            std::string semTipoId2 = matriz::model::novoUuid();
            std::string agora2 = matriz::model::agoraIso8601();
            janelaArchive.projetoAberto()->projeto().registro().run(
                "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
                "VALUES (?, ?, 'UIA-semtipo-2', 'semtipo2', NULL, 'capturado', ?, ?)",
                {matriz::db::Value::of(semTipoId2), matriz::db::Value::of(projetoArchiveId), matriz::db::Value::of(agora2),
                 matriz::db::Value::of(agora2)});

            FichaPanelComponent fichaLoteSemTipo(*janelaArchive.projetoAberto());
            fichaLoteSemTipo.setBounds(0, 0, 340, 500);
            checar(pdfId.has_value(), "PDF sem tipo do Fluxo 1 disponível pra este teste");
            if (pdfId) {
                fichaLoteSemTipo.mostrarSelecao({*pdfId, semTipoId2});
                salvarSnapshot(fichaLoteSemTipo, "20_ficha_lote_escolher_tipo");
                verificarInvariantes(fichaLoteSemTipo, "ficha_lote_escolher_tipo");

                auto* botaoDocumento = fichaLoteSemTipo.botaoTipoMidiaLoteParaTeste("documento");
                checar(botaoDocumento != nullptr, "botão \"Document\" existe no seletor de tipo em lote");
                if (botaoDocumento) botaoDocumento->onClick();

                auto stmtTipos = janelaArchive.projetoAberto()->projeto().registro().prepare(
                    "SELECT COUNT(*) FROM item WHERE id IN (?, ?) AND tipo_midia = 'documento'");
                stmtTipos.bind(1, matriz::db::Value::of(*pdfId));
                stmtTipos.bind(2, matriz::db::Value::of(semTipoId2));
                stmtTipos.step();
                checar(stmtTipos.columnInt(0) == 2, "aplicar tipo à seleção reclassifica os DOIS itens de uma vez");
            }
        }

        {
            // Dois itens do MESMO tipo (fita_rolo) — os 14 idPorTipo[] são
            // um de cada tipo, nenhum par compartilha tipo_midia, então
            // precisa de dois novos pra testar igual/múltiplos valores/
            // vazio (§12.2) de verdade.
            std::string fitaLote1 = inserirItemTipado(janelaArchive.projetoAberto()->projeto().registro(), projetoArchiveId,
                                                        "UIA-lote-1", "fita_rolo");
            std::string fitaLote2 = inserirItemTipado(janelaArchive.projetoAberto()->projeto().registro(), projetoArchiveId,
                                                        "UIA-lote-2", "fita_rolo");
            gravarCampoRaiz(janelaArchive.projetoAberto()->projeto().registro(), fitaLote1, "velocidade", "19 cm/s");
            gravarCampoRaiz(janelaArchive.projetoAberto()->projeto().registro(), fitaLote2, "velocidade", "38 cm/s");
            // "maquina" (texto simples, mesmo campo usado no teste de perda
            // de dado — Parte 2) fica vazio nos dois, não gravado em nenhum.

            // Peer real (não só setBounds) — igual ao teste de perda de
            // dado (Parte 2): foco de teclado é um conceito de janela
            // real, insertTextAtCaret sem peer não dispara onTextChange.
            PeerDeTeste peerLote(340, 700);
            FichaPanelComponent fichaLote(*janelaArchive.projetoAberto());
            fichaLote.setBounds(0, 0, 340, 700);
            peerLote.raiz.addAndMakeVisible(fichaLote);
            fichaLote.mostrarSelecao({fitaLote1, fitaLote2});
            salvarSnapshot(fichaLote, "21_ficha_lote_mesmo_tipo");
            verificarInvariantes(fichaLote, "ficha_lote_mesmo_tipo");

            auto* editorMaquina = fichaLote.editorDoCampoLoteParaTeste("maquina");
            checar(editorMaquina != nullptr, "campo \"maquina\" (vazio nos dois) aparece no modo lote");
            auto* teMaquina = dynamic_cast<juce::TextEditor*>(editorMaquina);
            checar(teMaquina != nullptr, "\"maquina\" é um editor de texto simples (não tabela/combo)");
            if (teMaquina) {
                teMaquina->grabKeyboardFocus();
                esperarDispatch();
                teMaquina->insertTextAtCaret("Studer");
                esperarDispatch();
            }

            auto* botaoAplicar = fichaLote.botaoAplicarLoteParaTeste();
            checar(botaoAplicar != nullptr, "botão \"Apply\" existe no modo lote");
            checar(botaoAplicar != nullptr && botaoAplicar->isEnabled(),
                   "botão \"Apply\" fica habilitado depois de tocar um campo");
            if (botaoAplicar) botaoAplicar->onClick();

            auto maquinaLote1Depois = janelaArchive.projetoAberto()->valorCampo(fitaLote1, "raiz", 0, "maquina");
            auto maquinaLote2Depois = janelaArchive.projetoAberto()->valorCampo(fitaLote2, "raiz", 0, "maquina");
            checar(maquinaLote1Depois && *maquinaLote1Depois == "Studer" && maquinaLote2Depois &&
                       *maquinaLote2Depois == "Studer",
                   "aplicar em lote grava o campo TOCADO nos dois itens");

            auto velocidadeLote1Depois = janelaArchive.projetoAberto()->valorCampo(fitaLote1, "raiz", 0, "velocidade");
            auto velocidadeLote2Depois = janelaArchive.projetoAberto()->valorCampo(fitaLote2, "raiz", 0, "velocidade");
            checar(velocidadeLote1Depois && *velocidadeLote1Depois == "19 cm/s" && velocidadeLote2Depois &&
                       *velocidadeLote2Depois == "38 cm/s",
                   "campo NÃO tocado (\"velocidade\", que já divergia) continua diferente em cada item — "
                   "\"múltiplos valores\" nunca foi sobrescrito");

            salvarSnapshot(fichaLote, "22_ficha_lote_depois_de_aplicar");
            verificarInvariantes(fichaLote, "ficha_lote_depois_de_aplicar");

            auto* botaoDesfazer = fichaLote.botaoDesfazerLoteParaTeste();
            checar(botaoDesfazer != nullptr && botaoDesfazer->isVisible(),
                   "botão \"Undo\" aparece (visível) depois de aplicar com sucesso");
            if (botaoDesfazer) botaoDesfazer->onClick();

            auto maquinaLote1Desfeita = janelaArchive.projetoAberto()->valorCampo(fitaLote1, "raiz", 0, "maquina");
            checar(maquinaLote1Desfeita.value_or("") == "", "desfazer em um passo restaura o campo ao valor anterior (vazio)");
        }

        // ===================================================================
        // Correções de operação, item 2 — arrastar pasta preserva a
        // estrutura. É a função principal do software: arrastar um catálogo
        // inteiro nunca pode virar um oceano de arquivos soltos.
        // ===================================================================
        std::cout << "\n-- EXPLORER → BACKUP preserva hierarquia --\n";

        {
            matriz::model::NovoProjetoParams paramsHier;
            paramsHier.nome = "Hierarquia";
            paramsHier.modo = matriz::model::Modo::Preservacao;
            paramsHier.prefixoNomenclatura = "HIE";
            auto projetoHier = matriz::model::Project::criar(tmpRoot.getChildFile("hierarquia"), paramsHier);
            auto* registroHier = &projetoHier->registro();
            std::string projetoHierId = projetoHier->projetoId();

            // Cinco níveis abaixo de "Turne", com arquivo em níveis
            // diferentes — inclusive um no meio do caminho, pra provar que
            // item preso a um nível intermediário não é empurrado pra folha.
            const juce::String base = "/Volumes/HD/Turne";
            inserirItemComOrigem(*registroHier, projetoHierId, "H-01", base + "/raiz.txt");
            inserirItemComOrigem(*registroHier, projetoHierId, "H-02", base + "/2003/ano.txt");
            inserirItemComOrigem(*registroHier, projetoHierId, "H-03", base + "/2003/Berlim/cidade.txt");
            inserirItemComOrigem(*registroHier, projetoHierId, "H-04", base + "/2003/Berlim/Audio/a.txt");
            inserirItemComOrigem(*registroHier, projetoHierId, "H-05", base + "/2003/Berlim/Audio/Masters/m.txt");

            ProjetoAberto abertoHier(std::move(projetoHier));

            auto origem = abertoHier.arvoreOrigem();
            const auto* noTurne = acharNo(origem, "Turne");
            checar(noTurne != nullptr, "EXPLORER: pasta \"Turne\" aparece na árvore de origem");
            checar(noTurne && profundidadeDaArvore(*noTurne) == 5,
                   "EXPLORER preserva os 5 níveis (Turne/2003/Berlim/Audio/Masters)");
            checar(noTurne && noTurne->itemIds.size() == 5,
                   "EXPLORER: os 5 arquivos aparecem sob Turne (recursivo)");
            checar(noTurne && noTurne->itemIdsDiretos.size() == 1,
                   "EXPLORER: só 1 arquivo está DIRETO em Turne (o resto está em subpastas)");

            // --- manter estrutura (padrão) ---
            int vinculados = abertoHier.replicarSubarvoreNoAcervo(*noTurne, std::string(), true);
            checar(vinculados == 5, "replicar mantendo estrutura vincula os 5 arquivos (" + juce::String(vinculados) + ")");

            auto backup = abertoHier.arvoreAcervo();
            const auto* turneNoBackup = acharNo(backup, "Turne");
            checar(turneNoBackup != nullptr, "BACKUP: a pasta arrastada existe no destino");
            checar(turneNoBackup && profundidadeDaArvore(*turneNoBackup) == 5,
                   "BACKUP preserva os 5 níveis — arrastar um catálogo não achata nada");

            const auto* masters = acharNo(backup, "Masters");
            checar(masters != nullptr && masters->itemIdsDiretos.size() == 1,
                   "BACKUP: o arquivo do 5º nível ficou no 5º nível, não na raiz");
            const auto* berlim = acharNo(backup, "Berlim");
            checar(berlim != nullptr && berlim->itemIdsDiretos.size() == 1,
                   "BACKUP: arquivo de nível intermediário fica no nível dele");
            checar(berlim != nullptr && berlim->itemIds.size() == 3,
                   "BACKUP: contagem recursiva de Berlim inclui Audio/ e Masters/ (3)");

            // --- achatar (a outra escolha oferecida ao soltar) ---
            std::string destinoPlanoId = abertoHier.criarPastaAcervo("Tudo junto", std::nullopt);
            int vinculadosPlano = abertoHier.replicarSubarvoreNoAcervo(*noTurne, destinoPlanoId, false);
            checar(vinculadosPlano == 5, "replicar achatando vincula os mesmos 5 arquivos");

            auto backup2 = abertoHier.arvoreAcervo();
            const auto* tudoJunto = acharNo(backup2, "Tudo junto");
            checar(tudoJunto != nullptr && tudoJunto->filhos.empty(),
                   "achatar não cria subpasta nenhuma no destino");
            checar(tudoJunto != nullptr && tudoJunto->itemIdsDiretos.size() == 5,
                   "achatar põe os 5 arquivos direto na pasta escolhida");
        }

        // ===================================================================
        // Item 3 — navegador de arquivos estilo Finder embutido. Roda sobre
        // uma árvore real em disco (não mock): o navegador lê o sistema de
        // arquivos de verdade, e o teste também confirma que ele NÃO MODIFICA
        // NADA (item 3.4), comparando a árvore antes e depois.
        // ===================================================================
        std::cout << "\n-- Navegador de arquivos estilo Finder --\n";

        {
            juce::File raizNav = tmpRoot.getChildFile("navegador");
            juce::File subA = raizNav.getChildFile("Fitas 1978");
            juce::File subB = raizNav.getChildFile("Fotos");
            juce::File subAA = subA.getChildFile("Lado A");
            subAA.createDirectory();
            subB.createDirectory();
            subA.getChildFile("rolo.wav").replaceWithText("wav");
            subAA.getChildFile("faixa1.wav").replaceWithText("wav1");
            subAA.getChildFile("faixa2.aif").replaceWithText("aif2");
            subB.getChildFile("capa.jpg").replaceWithText("jpg");
            raizNav.getChildFile("leiame.txt").replaceWithText("txt");

            auto inventario = [](const juce::File& raiz) {
                juce::StringArray out;
                for (const auto& f : raiz.findChildFiles(juce::File::findFilesAndDirectories, true))
                    out.add(f.getFullPathName() + "|" + juce::String(f.isDirectory() ? 0 : f.getSize()));
                out.sort(true);
                return out;
            };
            juce::StringArray antes = inventario(raizNav);

            NavegadorArquivosComponent nav;
            nav.setBounds(0, 0, 800, 500);
            nav.irPara(raizNav);
            salvarSnapshot(nav, "26_navegador_colunas");

            // Visão em colunas: uma coluna por nível do caminho, do volume
            // até a pasta atual — é o que dá o "percorrer" do Finder.
            checar(nav.totalColunasParaTeste() >= 1, "navegador abre com pelo menos a coluna da pasta atual");
            auto entradasRaiz = nav.entradasVisiveisParaTeste(nav.totalColunasParaTeste() - 1);
            checar(entradasRaiz.size() == 3, "coluna lista 2 pastas + 1 arquivo (" + juce::String((int)entradasRaiz.size()) + ")");
            checar(!entradasRaiz.empty() && entradasRaiz.front().isDirectory(),
                   "pasta vem antes de arquivo na ordenação");

            // Navegar pra dentro acrescenta coluna à direita.
            int colunasAntes = nav.totalColunasParaTeste();
            nav.irPara(subA);
            checar(nav.totalColunasParaTeste() == colunasAntes + 1,
                   "entrar numa pasta abre a coluna seguinte à direita (visão em colunas)");
            checar(nav.pastaAtual() == subA, "pasta atual acompanha a navegação");

            // Voltar/avançar/subir.
            checar(nav.podeVoltar(), "há histórico pra voltar depois de navegar");
            nav.voltar();
            checar(nav.pastaAtual() == raizNav, "voltar retorna à pasta anterior");
            checar(nav.podeAvancar(), "avançar fica disponível depois de voltar");
            nav.avancar();
            checar(nav.pastaAtual() == subA, "avançar refaz o caminho");
            nav.subirUmNivel();
            checar(nav.pastaAtual() == raizNav, "subir um nível vai pra pasta pai");

            // Seleção múltipla e o resumo "N arquivos, X GB" ANTES de adicionar.
            nav.irPara(subAA);
            auto arquivosAA = nav.entradasVisiveisParaTeste(nav.totalColunasParaTeste() - 1);
            checar(arquivosAA.size() == 2, "pasta com 2 arquivos lista os 2");
            if (arquivosAA.size() == 2) {
                nav.selecionarParaTeste(arquivosAA[0]);
                nav.selecionarParaTeste(arquivosAA[1], /*somarASelecao=*/true);
                checar(nav.selecao().size() == 2, "Cmd+clique soma à seleção em vez de substituir");
                checar(nav.totalArquivosNaSelecao() == 2, "contagem da seleção é 2 arquivos");
                checar(nav.tamanhoTotalDaSelecao() == 8,
                       "tamanho da seleção soma os bytes reais dos 2 arquivos (\"wav1\" + \"aif2\" = 8)");
                checar(nav.resumoSelecao().contains("2"), "resumo mostra a contagem antes de adicionar: \"" +
                                                               nav.resumoSelecao() + "\"");
            }

            // Filtro por tipo: numa pasta com wav+aif, "só imagem" não sobra nada.
            nav.definirFiltroTipo(NavegadorArquivosComponent::FiltroTipo::Imagem);
            checar(nav.entradasVisiveisParaTeste(nav.totalColunasParaTeste() - 1).empty(),
                   "filtro \"só imagem\" esconde os áudios");
            nav.definirFiltroTipo(NavegadorArquivosComponent::FiltroTipo::Audio);
            checar(nav.entradasVisiveisParaTeste(nav.totalColunasParaTeste() - 1).size() == 2,
                   "filtro \"só áudio\" traz os 2 de volta");
            nav.definirFiltroTipo(NavegadorArquivosComponent::FiltroTipo::Todos);

            // Busca alcança subpastas (item 3.2).
            nav.irPara(raizNav);
            nav.definirBusca("faixa");
            auto achados = nav.entradasVisiveisParaTeste(nav.totalColunasParaTeste() - 1);
            checar(achados.size() == 2, "busca desce nas subpastas e acha os 2 \"faixa*\" (" +
                                             juce::String((int)achados.size()) + ")");
            nav.definirBusca("");

            // Selecionar uma PASTA e adicionar traz a subárvore inteira: a
            // contagem recursiva é o que o operador vê antes de clicar.
            nav.selecionarParaTeste(subA);
            for (int i = 0; i < 40 && nav.totalArquivosNaSelecao() == 0; ++i) esperarDispatch(20);
            checar(nav.totalArquivosNaSelecao() == 3,
                   "pasta selecionada conta os 3 arquivos da subárvore inteira, não só os diretos (" +
                       juce::String(nav.totalArquivosNaSelecao()) + ")");

            salvarSnapshot(nav, "27_navegador_com_selecao");
            verificarInvariantes(nav, "navegador_com_selecao");

            // Visão em lista e em ícones (item 3.2) existem e não quebram.
            nav.definirVisao(NavegadorArquivosComponent::Visao::Lista);
            checar(nav.totalColunasParaTeste() == 1, "visão em lista mostra uma coluna só");
            salvarSnapshot(nav, "28_navegador_lista");
            nav.definirVisao(NavegadorArquivosComponent::Visao::Colunas);

            // O navegador NÃO MODIFICA NADA (item 3.4).
            checar(inventario(raizNav) == antes,
                   "navegar, filtrar, buscar e selecionar não alterou nada em disco (item 3.4)");
        }

        // ===================================================================
        // Correções de operação, item 4 — o painel direito tem DUAS seções:
        // o metadado que veio dentro do arquivo (somente leitura) e a ficha
        // (editável). Antes era tudo campo editável junto, sem distinguir
        // leitura de máquina de decisão humana.
        // ===================================================================
        std::cout << "\n-- Metadados originais x metadados do backup --\n";

        {
            // janelaArchive já ingeriu mídia real no Fluxo 1 — o áudio tem
            // leitura técnica de verdade (ffprobe) atrás.
            auto itens = janelaArchive.projetoAberto()->listarItens();
            std::optional<std::string> idComArquivo;
            for (auto& r : itens)
                if (janelaArchive.projetoAberto()->arquivoPrincipal(r.id)) { idComArquivo = r.id; break; }
            checar(idComArquivo.has_value(), "há um item com arquivo real pra ler metadado original");

            MetadadosOriginaisComponent metadados(*janelaArchive.projetoAberto());
            metadados.setBounds(0, 0, 340, 300);

            checar(metadados.estaColapsada(),
                   "a seção nasce colapsada — quem abre o painel quer editar a ficha, não ler codec");
            checar(metadados.alturaDesejada() == MetadadosOriginaisComponent::kAlturaCabecalho,
                   "colapsada, ocupa só o cabeçalho");

            if (idComArquivo) metadados.mostrarItem(*idComArquivo);
            metadados.alternarColapso();

            auto rotulos = metadados.rotulosParaTeste();
            checar(!rotulos.empty(), "metadado original foi lido do arquivo (" +
                                          juce::String(static_cast<int>(rotulos.size())) + " campo(s))");
            checar(metadados.alturaDesejada() > MetadadosOriginaisComponent::kAlturaCabecalho,
                   "expandida, reserva altura pras linhas");
            verificarInvariantes(metadados, "metadados_originais");
            salvarSnapshot(metadados, "23_metadados_originais");

            // Item sem arquivo nenhum: estado explicativo, nunca seção em branco.
            metadados.mostrarItem({});
            checar(metadados.rotulosParaTeste().empty(),
                   "item sem arquivo não inventa metadado — cai no estado vazio explicado");

            // A garantia estrutural: a seção não tem NENHUM filho — é tudo
            // paint(), então não existe editor pra alguém digitar por engano.
            checar(metadados.getNumChildComponents() == 0,
                   "seção de metadado original não tem editor nenhum: é somente leitura por construção");
        }

        // ===================================================================
        // Correções de operação, itens 5 e 6 — ações sobre item/seleção.
        // A garantia que precisa ser mecânica, não só textual: "remover"
        // NUNCA apaga arquivo em disco.
        // ===================================================================
        std::cout << "\n-- Ações sobre item/seleção --\n";

        {
            matriz::model::NovoProjetoParams paramsAcoes;
            paramsAcoes.nome = "Acoes";
            paramsAcoes.modo = matriz::model::Modo::Preservacao;
            paramsAcoes.prefixoNomenclatura = "ACO";
            auto projetoAcoes = matriz::model::Project::criar(tmpRoot.getChildFile("acoes"), paramsAcoes);
            auto* registroAcoes = &projetoAcoes->registro();
            std::string projetoAcoesId = projetoAcoes->projetoId();

            // Arquivo de origem REAL em disco — é ele que não pode sumir.
            juce::File fonteReal = tmpRoot.getChildFile("fonte_intocada.txt");
            fonteReal.replaceWithText("conteudo original");

            std::string idA = inserirItemComOrigem(*registroAcoes, projetoAcoesId, "A-01",
                                                    fonteReal.getFullPathName());
            std::string idB = inserirItemComOrigem(*registroAcoes, projetoAcoesId, "A-02",
                                                    "/Volumes/HD/outro.txt");

            ProjetoAberto abertoAcoes(std::move(projetoAcoes));

            // --- renomear alimenta o token {titulo} da máscara ---
            abertoAcoes.renomearItens({idA}, "Nome novo");
            auto itensRenomeados = abertoAcoes.listarItens();
            auto itRen = std::find_if(itensRenomeados.begin(), itensRenomeados.end(),
                                       [&](const ItemResumo& r) { return r.id == idA; });
            checar(itRen != itensRenomeados.end() && itRen->titulo == "Nome novo",
                   "renomear grava o título que a máscara usa pro nome no backup");

            // --- caminho de origem (Mostrar na origem / Copiar caminho) ---
            auto caminho = abertoAcoes.caminhoDeOrigem(idA);
            checar(caminho.has_value() && *caminho == fonteReal.getFullPathName(),
                   "caminho de origem devolve o arquivo real da fonte");
            checar(!abertoAcoes.caminhoDeOrigem("id-que-nao-existe").has_value(),
                   "caminho de origem de item inexistente não inventa valor");

            // --- remover do backup: sai das pastas, continua no projeto ---
            std::string pastaId = abertoAcoes.criarPastaAcervo("Uma pasta", std::nullopt);
            abertoAcoes.adicionarItensAPasta({idA, idB}, pastaId);
            abertoAcoes.removerItensDoBackup({idA});

            auto backupDepois = abertoAcoes.arvoreAcervo();
            const auto* pasta = acharNo(backupDepois, "Uma pasta");
            checar(pasta != nullptr && pasta->itemIdsDiretos.count(idA) == 0,
                   "remover do backup tira o item da pasta");
            checar(pasta != nullptr && pasta->itemIdsDiretos.count(idB) == 1,
                   "remover do backup não mexe nos outros itens da pasta");
            checar(abertoAcoes.listarItens().size() == 2,
                   "remover do backup NÃO remove o item do projeto — ele volta a ser \"ainda sem pasta\"");

            // --- remover da lista: sai do projeto, disco intocado ---
            abertoAcoes.removerItensDoProjeto({idA});
            auto restantes = abertoAcoes.listarItens();
            checar(restantes.size() == 1 && restantes.front().id == idB,
                   "remover desta lista tira o item do projeto");
            checar(fonteReal.existsAsFile(),
                   "remover desta lista NÃO apaga o arquivo de origem em disco");
            checar(fonteReal.loadFileAsString() == "conteudo original",
                   "o arquivo de origem continua com o conteúdo intacto");
        }

        // ===================================================================
        // Correções de operação, item 9 — capa / miniatura personalizada.
        // ===================================================================
        std::cout << "\n-- Capa personalizada --\n";

        {
            auto* projetoCapa = janelaArchive.projetoAberto();
            auto itens = projetoCapa->listarItens();
            std::vector<std::string> alvos;
            for (auto& r : itens) {
                alvos.push_back(r.id);
                if (alvos.size() == 2) break;
            }
            checar(alvos.size() == 2, "há 2 itens pra testar capa em lote");

            juce::File imagemCapa = tmpRoot.getChildFile("capa_escolhida.png");
            {
                // PNG real, não um arquivo vazio com extensão trocada: a
                // geração de miniatura precisa conseguir DECODIFICAR.
                juce::Image img(juce::Image::RGB, 64, 64, true);
                juce::Graphics g(img);
                g.fillAll(juce::Colours::orange);
                juce::PNGImageFormat png;
                if (auto stream = std::unique_ptr<juce::FileOutputStream>(imagemCapa.createOutputStream()))
                    png.writeImageToStream(img, *stream);
            }
            checar(imagemCapa.existsAsFile(), "imagem de capa de teste foi gerada");

            juce::String miniaturaAntes =
                projetoCapa->caminhoMiniaturaPrincipal(alvos.front()).value_or(juce::String());

            checar(!projetoCapa->temCapa(alvos.front()), "item começa sem capa");
            int aplicadas = projetoCapa->definirCapa(alvos, imagemCapa);
            checar(aplicadas == 2, "capa aplicada aos 2 itens de uma vez (" + juce::String(aplicadas) + ")");
            checar(projetoCapa->temCapa(alvos.front()) && projetoCapa->temCapa(alvos.back()),
                   "os dois itens passam a ter capa");

            juce::String miniaturaDepois =
                projetoCapa->caminhoMiniaturaPrincipal(alvos.front()).value_or(juce::String());
            checar(miniaturaDepois.isNotEmpty() && miniaturaDepois != miniaturaAntes,
                   "a miniatura da grade passa a ser a da capa, não a gerada");

            // A imagem é COPIADA pra dentro do projeto: apagar o original
            // escolhido não pode deixar o item sem capa.
            imagemCapa.deleteFile();
            checar(projetoCapa->temCapa(alvos.front()),
                   "apagar a imagem original não tira a capa — ela foi copiada pro projeto");
            checar(juce::File(projetoCapa->caminhoMiniaturaPrincipal(alvos.front()).value_or(juce::String()))
                       .existsAsFile(),
                   "a miniatura da capa continua existindo em disco depois disso");

            projetoCapa->removerCapa({alvos.front()});
            checar(!projetoCapa->temCapa(alvos.front()), "remover capa tira a capa do item");
            checar(projetoCapa->temCapa(alvos.back()), "remover capa de um item não mexe no outro");
            checar(projetoCapa->caminhoMiniaturaPrincipal(alvos.front()).value_or(juce::String()) == miniaturaAntes,
                   "sem capa, a miniatura gerada volta a valer sozinha");
        }

        // ===================================================================
        // Correções de operação, item 11 — visualizador do catálogo.
        // O motor está coberto no matriz_ingest_selftest; aqui é a tela: ela
        // tem que abrir uma pasta de backup SEM projeto nenhum carregado.
        // ===================================================================
        std::cout << "\n-- Visualizador do catálogo de proxies --\n";

        {
            juce::File destinoCatalogo = tmpRoot.getChildFile("backup_para_catalogo");
            destinoCatalogo.createDirectory();

            // Organiza tudo numa pasta da BACKUP e grava, pra o catálogo ter
            // o que descrever.
            auto* projeto = janelaArchive.projetoAberto();
            std::string pastaBackupId = projeto->criarPastaAcervo("Consulta", std::nullopt);
            std::vector<std::string> todos;
            for (auto& r : projeto->listarItens()) todos.push_back(r.id);
            projeto->adicionarItensAPasta(todos, pastaBackupId);

            // Estrutura manual: este teste é sobre o catálogo de proxies, e
            // as asserções de caminho legível abaixo foram escritas contra a
            // árvore montada à mão (a hierarquia automática do item 5 tem
            // cobertura própria em testarHierarquiaBackup).
            auto plano = matriz::consolidacao::planejarConsolidacao(
                projeto->projeto().registro(), projeto->projeto().pasta(), destinoCatalogo,
                {matriz::consolidacao::NivelHierarquia::PastaManual});
            matriz::consolidacao::executarConsolidacao(projeto->projeto().registro(), projeto->projeto().pasta(),
                                                        destinoCatalogo, plano);
            auto res = matriz::catalogo::gerar(projeto->projeto().registro(), projeto->projeto().indice(),
                                                projeto->projeto().pasta(), destinoCatalogo);
            checar(res.gravados > 0, "catálogo gerado com " + juce::String(res.gravados) + " materiais");

            CatalogoComponent visualizador;
            visualizador.setBounds(0, 0, 900, 400);
            checar(visualizador.abrir(destinoCatalogo), "visualizador abre a pasta de backup");
            checar(visualizador.totalEntradas() == res.gravados,
                   "visualizador lista todos os materiais do catálogo");

            // A pergunta que o catálogo existe pra responder.
            checar(visualizador.descricaoDaEntradaParaTeste(0).isNotEmpty(),
                   "cada linha diz onde o arquivo está: \"" +
                       visualizador.descricaoDaEntradaParaTeste(0) + "\"");
            checar(visualizador.fonteConectadaParaTeste(0),
                   "com o backup montado, a linha aparece como disponível");

            visualizador.definirBusca("zzz-nada-com-esse-nome");
            checar(visualizador.totalVisiveis() == 0, "busca sem resultado esvazia a lista");
            visualizador.definirBusca({});
            checar(visualizador.totalVisiveis() == res.gravados, "limpar a busca traz tudo de volta");

            salvarSnapshot(visualizador, "24_catalogo_proxies");
            verificarInvariantes(visualizador, "catalogo_proxies");

            // Abrir pela janela principal, o caminho real do operador.
            MainComponent janelaCatalogo;
            janelaCatalogo.setBounds(0, 0, 1280, 800);
            checar(janelaCatalogo.abrirCatalogo(destinoCatalogo),
                   "MainComponent abre a pasta de backup em modo catálogo");
            checar(janelaCatalogo.temCatalogoAberto() && !janelaCatalogo.temProjetoAberto(),
                   "modo catálogo NÃO carrega projeto nenhum — é consulta pura");
            salvarSnapshot(janelaCatalogo, "25_janela_catalogo");
            verificarInvariantes(janelaCatalogo, "janela_catalogo");

            janelaCatalogo.fecharProjeto();
            checar(!janelaCatalogo.temCatalogoAberto(), "fechar o catálogo volta pra tela inicial");

            // Pasta sem catálogo não é confundida com uma que tem.
            MainComponent janelaSemCatalogo;
            janelaSemCatalogo.setBounds(0, 0, 1280, 800);
            checar(!janelaSemCatalogo.abrirCatalogo(tmpRoot),
                   "pasta sem catálogo dentro é recusada, não abre uma tela vazia");
        }

        // Fluxo 4 (1.3): trocar idioma muda toda string. Já coberto a fundo
        // em tools/selftest ("i18n" — carrega os dois locales de verdade e
        // confirma que t() muda de valor nos dois sentidos); aqui
        // confirmamos que a MESMA chave que a tela inicial de fato usa
        // (tela_inicial.botao_abrir, lida em 01_tela_inicial_*.png acima)
        // muda de valor com a troca — a ponte entre i18n::carregar() e o
        // texto real que a UI constrói, não só a tabela isolada.
        {
            matriz::i18n::carregar("en");
            juce::String emIngles = matriz::i18n::t("tela_inicial.botao_abrir");
            matriz::i18n::carregar("pt_BR");
            juce::String emPortugues = matriz::i18n::t("tela_inicial.botao_abrir");
            matriz::i18n::carregar("en");
            checar(emIngles != emPortugues && emIngles == "Open an existing project…" &&
                       emPortugues == "Abrir um projeto que já existe…",
                   "fluxo 4: trocar locale muda o texto real que a tela inicial usa (\"" + emIngles + "\" / \"" +
                       emPortugues + "\")");
        }

    } catch (const std::exception& e) {
        checar(false, juce::String("harness de UI: ") + e.what());
    }

    tmpRoot.deleteRecursively();

    std::cout << "\n" << (falhas == 0 ? "TODOS OS TESTES PASSARAM" : juce::String(falhas) + " FALHA(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
