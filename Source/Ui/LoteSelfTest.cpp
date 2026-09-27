#include "LoteSelfTest.h"

#include <JuceHeader.h>

#include <iostream>

#include "../Model/Project.h"
#include "../I18n/Strings.h"
#include "CatalogWorkspaceComponent.h"
#include "FichaPanelComponent.h"
#include "IntakeWorkspaceComponent.h"
#include "MainComponent.h"
#include "MosaicoComponent.h"
#include "OriginalSourceMedium.h"
#include "ProjetoAberto.h"
#include "BackupVersionsComponent.h"
#include "BackupWorkspaceComponent.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Ficha/AutocompleteHistorico.h"
#include "InitialRelinkDialog.h"
#include "DuplicatesWorkspaceComponent.h"
#include "EventBus.h"
#include "../Sync/SyncEngine.h"
#include "../Consolidacao/Consolidacao.h"
#include "../Analytics/AssetGeolocation.h"

namespace matriz::ui {

namespace {

constexpr int kItensPorLado = 12;

void bombear(int ms = 30) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

template <typename Cond>
bool esperarAte(Cond cond, int timeoutMs = 30000) {
    auto inicio = juce::Time::getMillisecondCounter();
    while (!cond()) {
        if (juce::Time::getMillisecondCounter() - inicio > static_cast<juce::uint32>(timeoutMs)) return false;
        bombear(20);
    }
    return true;
}

std::string inserirItem(matriz::db::Database& reg, const std::string& projetoId, const std::string& codigo,
                        bool quarentena, const std::string& extensao = ".wav") {
    std::string id = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();
    reg.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
            "VALUES (?, ?, ?, ?, 'digital_audio', 'capturado', ?, ?)",
            {matriz::db::Value::of(id), matriz::db::Value::of(projetoId), matriz::db::Value::of(codigo),
             matriz::db::Value::of(codigo), matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
    reg.run("UPDATE item SET em_quarentena = ? WHERE id = ?",
            {matriz::db::Value::of(quarentena ? 1 : 0), matriz::db::Value::of(id)});
    reg.run("INSERT INTO arquivo (id, item_id, caminho_relativo, papel, eh_master, tamanho_bytes, criado_em, "
            "atualizado_em) VALUES (?, ?, ?, 'preservation_master', 1, 1024, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id),
             matriz::db::Value::of("originais/" + codigo + extensao), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
    return id;
}

} // namespace

int rodarLoteSelfTest() {
    std::cout << "== Batch assignment, " << kItensPorLado << " items per side (Catalog + Intake) ==\n";
    int falhas = 0;
    auto checar = [&](bool ok, const juce::String& descricao) {
        std::cout << (ok ? "  OK   " : "  FAIL ") << descricao << "\n";
        if (!ok) ++falhas;
    };

    juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("matriz_lote_selftest_" + juce::Uuid().toDashedString());
    try {
        raiz.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Lote";
        params.prefixoNomenclatura = "LOT";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("projeto"), params);
        const std::string projetoId = projeto->projetoId();
        for (int i = 0; i < kItensPorLado; ++i) {
            inserirItem(projeto->registro(), projetoId, "LOT-CAT-" + std::to_string(i), false);
            inserirItem(projeto->registro(), projetoId, "LOT-INT-" + std::to_string(i), true);
        }

        MainComponent janela;
        janela.setBounds(0, 0, 1400, 900);
        janela.abrirProjeto(std::move(projeto));
        bombear(200);
        auto* pa = janela.projetoAberto();
        auto& reg = pa->projeto().registro();

        // Quantos itens de um lado (quarentena=0 Catalog, 1 Intake) têm coluna == valor.
        auto contar = [&](bool quarentena, const std::string& coluna, const std::string& valor) {
            auto st = reg.prepare("SELECT COUNT(*) FROM item WHERE COALESCE(em_quarentena, 0) = ? AND " + coluna + " = ?");
            st.bind(1, matriz::db::Value::of(quarentena ? 1 : 0));
            st.bind(2, matriz::db::Value::of(valor));
            st.step();
            return static_cast<int>(st.columnInt(0));
        };
        auto contarGeo = [&](bool quarentena, const std::string& cidade) {
            auto st = reg.prepare("SELECT COUNT(*) FROM asset_geolocation g JOIN item i ON i.id = g.asset_id "
                                  "WHERE COALESCE(i.em_quarentena, 0) = ? AND g.city = ?");
            st.bind(1, matriz::db::Value::of(quarentena ? 1 : 0));
            st.bind(2, matriz::db::Value::of(cidade));
            st.step();
            return static_cast<int>(st.columnInt(0));
        };
        auto n = [](int v) { return " (" + juce::String(v) + "/" + juce::String(kItensPorLado) + ")"; };

        // ------------------------------------------------------------ Catalog
        std::cout << "\n-- Catalog: batch record (FichaPanelComponent, real-time apply) --\n";
        janela.mostrarGrid();
        auto* cw = janela.catalogWorkspace_.get();
        checar(cw != nullptr, "Catalog workspace exists");
        if (cw) {
            auto* mosaico = cw->mosaico_.get();
            bool carregou = esperarAte([&] {
                return !mosaico->snapshotPendente() && mosaico->totalItensCarregados() >= kItensPorLado;
            });
            checar(carregou, "Catalog grid loaded the " + juce::String(kItensPorLado) + " items");
            // Lista do Metadata no mesmo formato da do INTAKE (conferir a olho):
            // test-output/metadata_lista.png x intake_lista.png.
            if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
                mosaico->definirModoVisao(MosaicoComponent::ModoVisao::Lista);
                bombear(300);
                juce::PNGImageFormat png;
                auto arq = dir.getChildFile("metadata_lista.png");
                arq.deleteFile();
                auto area = mosaico->getLocalBounds().withHeight(juce::jmin(mosaico->getHeight(), 520));
                if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                    png.writeImageToStream(mosaico->createComponentSnapshot(area), *out);
                mosaico->definirModoVisao(MosaicoComponent::ModoVisao::Grade);
                bombear(100);
            }
            // Lista paginada (item 8) e ordenação por coluna (item 7).
            {
                mosaico->definirModoVisao(MosaicoComponent::ModoVisao::Lista);
                mosaico->definirItensPorPaginaLista(5);
                checar(mosaico->paginacaoListaAtiva() && mosaico->totalPaginasLista() == 3,
                       "LIST paginates: 12 items at 5 per page -> 3 pages (" + juce::String(mosaico->totalPaginasLista()) + ")");
                mosaico->irParaPaginaLista(2);
                checar(mosaico->paginaListaAtual() == 2, "go to the last page");
                mosaico->selecionarTodos();
                checar(static_cast<int>(mosaico->itensSelecionados().size()) == kItensPorLado,
                       "Cmd+A selects the whole filter, all pages (" + juce::String((int) mosaico->itensSelecionados().size()) + ")");
                mosaico->limparSelecao();
                ItemResumo a, b;
                a.titulo = "Item 2"; b.titulo = "Item 10";
                a.tamanhoBytes = 10; b.tamanhoBytes = 5;
                checar(MosaicoComponent::compararPorColunaDaLista(a, b, 2) < 0 && MosaicoComponent::compararPorColunaDaLista(a, b, 5) > 0,
                       "column sort: name in natural order (2 before 10), size by bytes");
                // Ordenar pela coluna vale pra lista INTEIRA, não só a página:
                // a página 1 tem os 5 menores de todo o filtro, e a última
                // página os maiores (antes cada grupo de tipo ordenava à parte).
                {
                    // Dois grupos de tipo (pares = vídeo): o bug só aparecia
                    // com mais de um grupo na lista.
                    reg.run("UPDATE item SET tipo_midia = 'digital_video' WHERE COALESCE(em_quarentena, 0) = 0 "
                            "AND CAST(substr(codigo_acervo, 9) AS INTEGER) % 2 = 0", {});
                    mosaico->recarregarSincrono();
                    bombear(100);
                    std::vector<std::pair<juce::String, std::string>> todos;
                    for (const auto& it : mosaico->todosItensEmMemoria())
                        todos.push_back({juce::String::fromUTF8((it.titulo.empty() ? it.nomeOriginalArquivo : it.titulo).c_str()), it.id});
                    std::sort(todos.begin(), todos.end(), [](const auto& x, const auto& y) {
                        const int c = x.first.compareNatural(y.first);
                        return c != 0 ? c < 0 : x.second < y.second;
                    });
                    auto nomesDe = [&](const std::vector<std::string>& ids) {
                        std::vector<juce::String> out;
                        for (const auto& id : ids)
                            for (const auto& t : todos) if (t.second == id) out.push_back(t.first);
                        std::sort(out.begin(), out.end(), [](const juce::String& x, const juce::String& y) { return x.compareNatural(y) < 0; });
                        return out;
                    };
                    std::vector<std::string> esperadosIni, esperadosFim;
                    for (int i = 0; i < 5; ++i) esperadosIni.push_back(todos[(size_t) i].second);
                    for (size_t i = todos.size() - 2; i < todos.size(); ++i) esperadosFim.push_back(todos[i].second);
                    mosaico->ordenarListaPorColuna(2, true);
                    checar(mosaico->paginaListaAtual() == 0 && nomesDe(mosaico->idsVisiveisEmOrdem()) == nomesDe(esperadosIni),
                           "column sort ascending: page 1 holds the first 5 of the WHOLE list");
                    mosaico->irParaPaginaLista(2);
                    checar(nomesDe(mosaico->idsVisiveisEmOrdem()) == nomesDe(esperadosFim),
                           "column sort ascending: the last page holds the last items of the WHOLE list");
                    mosaico->ordenarListaPorColuna(2, false);
                    std::vector<std::string> esperadosDesc;
                    for (size_t i = todos.size() - 5; i < todos.size(); ++i) esperadosDesc.push_back(todos[i].second);
                    checar(nomesDe(mosaico->idsVisiveisEmOrdem()) == nomesDe(esperadosDesc),
                           "column sort descending: page 1 holds the last 5 of the WHOLE list");
                    mosaico->definirOrdenacao(Ordenacao::Codigo);
                    reg.run("UPDATE item SET tipo_midia = 'digital_audio'", {});
                    mosaico->recarregarSincrono();
                    bombear(100);
                }
                mosaico->definirItensPorPaginaLista(100);
                mosaico->definirModoVisao(MosaicoComponent::ModoVisao::Grade);
                checar(!mosaico->paginacaoListaAtiva(), "the thumbnail grid is not paginated");
            }
            // "Show content in grid" (Folder Map): salvar um campo da ficha
            // reaplica os filtros — a pasta tem que continuar na grade.
            {
                std::set<std::string> pasta;
                for (const auto& it : mosaico->todosItensEmMemoria())
                    if (pasta.size() < 4) pasta.insert(it.id);
                cw->definirSelecaoItens(pasta);
                cw->atualizarFiltrosDeData();  // o que aoAplicarSucesso chama ao salvar
                checar(mosaico->totalItensVisiveis() == 4,
                       "Folder Map content stays in the grid after saving a field (" +
                           juce::String(mosaico->totalItensVisiveis()) + "/4)");
                cw->limparTodosOsFiltros();
                checar(mosaico->totalItensVisiveis() == kItensPorLado, "HOME drops the folder filter");
                mosaico->limparSelecao();
            }
            // Item 5: tecla E aparece na hora (sem recarregar) e não marca
            // o item como "metadado editado".
            {
                const std::string alvoE = mosaico->todosItensEmMemoria().front().id;
                pa->alternarMarcadoRevisado({alvoE});
                bool revisadoMem = false, editadoMem = true;
                auto ler = [&] {
                    for (const auto& it : mosaico->todosItensEmMemoria())
                        if (it.id == alvoE) { revisadoMem = it.marcadoRevisado; editadoMem = it.metadadosEditados; }
                    return revisadoMem;
                };
                // "Na hora" = bem antes dos ~10 s do bug; teto de 3 s cobre um
                // snapshot concorrente (MatrizMiniGen) chegando no meio.
                esperarAte(ler, 3000);
                checar(revisadoMem, "E shows on the grid item right away (< 3 s, not the ~10 s full reload)");
                checar(!editadoMem, "E does not flag the item as metadata-edited");
                // (Sem check de versaoSnapshot: o MatrizMiniGen recarrega a grade
                // sozinho na abertura e tornava o check intermitente. O check de
                // cima — flag já em memória 100 ms depois — é o que prova que o
                // E não depende mais de um reload.)
                auto resumo = pa->obterItemResumo(alvoE);
                checar(resumo && resumo->marcadoRevisado, "obterItemResumo() reports marcadoRevisado");
                pa->alternarMarcadoRevisado({alvoE});
                bombear(100);
            }

            // Item 2: DATE conta pelo MEDIA TYPE ativo; limpar os filtros
            // (HOME) tem que recontar na hora, sem esperar o timer de 60 s.
            {
                auto somaAnos = [&] {
                    int soma = 0;
                    for (const auto& [ano, n] : cw->anosDisponiveis_) soma += n;
                    return soma;
                };
                cw->tipoMidiaSelecionado_ = std::string("video");  // os 12 são áudio
                cw->aplicarFiltrosAdicionais();
                cw->atualizarContagens();
                esperarAte([&] { return somaAnos() == 0; }, 5000);
                checar(somaAnos() == 0, "DATE counts follow the MEDIA TYPE filter (video -> " + juce::String(somaAnos()) + ")");
                cw->limparTodosOsFiltros(true);
                esperarAte([&] { return somaAnos() == kItensPorLado; }, 5000);
                checar(somaAnos() == kItensPorLado,
                       "clearing all filters recounts DATE right away (" + juce::String(somaAnos()) + ")");
                esperarAte([&] { return !mosaico->snapshotPendente(); }, 10000);
            }

            // Como o operador: clica num item (ficha de 1 item) e depois no
            // botão "Select All" da barra do Catalog.
            const std::string primeiro = mosaico->todosItensEmMemoria().front().id;
            mosaico->selecionarItem(primeiro);
            cw->selecionarItem(primeiro);
            bombear(100);
            if (cw->btnSelecionarTodos_ && cw->btnSelecionarTodos_->onClick) cw->btnSelecionarTodos_->onClick();
            bombear(100);
            auto sel = mosaico->itensSelecionados();
            checar(static_cast<int>(sel.size()) == kItensPorLado, "all items selected (" + juce::String((int) sel.size()) + ")");
            auto* ficha = cw->fichaPanel_.get();

            auto aplicarTexto = [&](const std::string& campo, const juce::String& valor) {
                auto* te = dynamic_cast<juce::TextEditor*>(ficha->editorDoCampoLoteParaTeste(campo));
                if (!te) return false;
                te->setText(valor, false);
                if (te->onReturnKey) te->onReturnKey();
                bombear(300);
                return true;
            };

            checar(aplicarTexto("creator", "Creator Lote"), "CREATOR editor exists in batch mode");
            int c = contar(false, "dc_creator", "Creator Lote");
            checar(c == kItensPorLado, "Catalog CREATOR written to every selected item" + n(c));

            checar(aplicarTexto("subject", "Subject Lote"), "SUBJECT editor exists in batch mode");
            c = contar(false, "dc_subject", "Subject Lote");
            checar(c == kItensPorLado, "Catalog SUBJECT written to every selected item" + n(c));

            checar(aplicarTexto("year", "1987"), "EVENT DATE editor exists in batch mode");
            c = contar(false, "ano", "1987");
            checar(c == kItensPorLado, "Catalog EVENT DATE written to every selected item" + n(c));

            auto* cb = dynamic_cast<juce::ComboBox*>(ficha->editorDoCampoLoteParaTeste("collection"));
            checar(cb != nullptr && cb->getNumItems() > 0, "CONTENT dropdown exists in batch mode");
            std::string contentGravado;
            if (cb && cb->getNumItems() > 0) {
                cb->setSelectedItemIndex(0, juce::sendNotificationSync);
                bombear(300);
                contentGravado = pa->lerMetadado(*sel.begin(), "collection_type").value_or("");
                c = contentGravado.empty() ? 0 : contar(false, "collection_type", contentGravado);
                checar(c == kItensPorLado, "Catalog CONTENT written to every selected item" + n(c));
            }

            auto* osm = dynamic_cast<OriginalSourceMediumEditorComponent*>(ficha->editorDoCampoLoteParaTeste("source_media"));
            checar(osm != nullptr, "ORIGINAL SOURCE MEDIUM editor exists in batch mode");
            std::string smGravado;
            if (osm) {
                OriginalSourceMediumInfo info;
                info.medium = "Cassette";
                info.recordingDevice = "Nakamichi Dragon";
                osm->setValue(info);
                smGravado = osm->getValueString();
                if (osm->onChange) osm->onChange();
                bombear(300);
                c = contar(false, "source_media", smGravado);
                checar(c == kItensPorLado, "Catalog SOURCE MEDIUM written to every selected item" + n(c));
            }

            auto* geoCity = dynamic_cast<juce::TextEditor*>(ficha->editorDoCampoLoteParaTeste("geo_city"));
            checar(geoCity != nullptr, "GEO LOCATION city editor exists in batch mode");
            if (geoCity) {
                geoCity->setText("Porto Seguro", false);
                if (geoCity->onReturnKey) geoCity->onReturnKey();
                bombear(300);
                c = contarGeo(false, "Porto Seguro");
                checar(c == kItensPorLado, "Catalog GEO LOCATION written to every selected item" + n(c));
            }

            // Na tela, sem reabrir: a grade em memória e a ficha reaberta.
            bombear(500);
            esperarAte([&] { return !mosaico->snapshotPendente(); }, 10000);
            int anoMem = 0, contentMem = 0;
            for (const auto& it : mosaico->todosItensEmMemoria()) {
                if (!sel.count(it.id)) continue;
                if (it.ano == 1987) ++anoMem;
                if (it.collectionType && *it.collectionType == contentGravado) ++contentMem;
            }
            checar(anoMem == kItensPorLado, "grid shows the new EVENT DATE on every item without reopening" + n(anoMem));
            checar(contentMem == kItensPorLado, "grid shows the new CONTENT on every item without reopening" + n(contentMem));
            checar(static_cast<int>(mosaico->itensSelecionados().size()) == kItensPorLado,
                   "the selection survived the batch edits (" + juce::String((int) mosaico->itensSelecionados().size()) + ")");
            cw->selecionarItem(*sel.begin());
            bombear(100);
            auto* teCreator = dynamic_cast<juce::TextEditor*>(ficha->editorDoCampoLoteParaTeste("creator"));
            checar(teCreator && teCreator->getText() == "Creator Lote",
                   "re-shown batch record shows the common CREATOR (\"" + (teCreator ? teCreator->getText() : juce::String()) + "\")");

            // Desfazer o lote.
            checar(aplicarTexto("creator", "Creator Desfazer"), "CREATOR re-applied for the undo test");
            c = contar(false, "dc_creator", "Creator Desfazer");
            checar(c == kItensPorLado, "second CREATOR batch written" + n(c));
            // Desfazer de verdade é o Cmd+Z global (o lote em tempo real abre
            // um grupo de Undo por campo aplicado).
            checar(janela.podeDesfazer(), "the batch left an undo entry (Cmd+Z)");
            janela.executarUndo();
            bombear(300);
            c = contar(false, "dc_creator", "Creator Lote");
            checar(c == kItensPorLado, "batch UNDO restores the previous CREATOR on every item" + n(c));
        }

        // ------------------------------------------------------------- Intake
        std::cout << "\n-- Intake: batch popups (salvarMetadadoEmLote in background) --\n";
        janela.mostrarIntake();
        auto* iw = janela.intakeWorkspace_.get();
        checar(iw != nullptr, "Intake workspace exists");
        if (iw) {
            iw->recarregar();
            bool carregou = esperarAte([&] {
                return !iw->snapshotPendente() && static_cast<int>(iw->todosItens_.size()) >= kItensPorLado;
            });
            checar(carregou, "Intake list loaded the " + juce::String(kItensPorLado) + " items (" +
                                 juce::String((int) iw->todosItens_.size()) + ")");
            if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
                bombear(300);
                juce::PNGImageFormat png;
                auto arq = dir.getChildFile("intake_lista.png");
                arq.deleteFile();
                if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                    png.writeImageToStream(iw->createComponentSnapshot(iw->getLocalBounds()), *out);
            }
            iw->selecionarTodos(true);
            checar(static_cast<int>(iw->itensSelecionados().size()) == kItensPorLado,
                   "all Intake items selected (" + juce::String((int) iw->itensSelecionados().size()) + ")");

            OriginalSourceMediumEditorComponent osmTemp;
            OriginalSourceMediumInfo info;
            info.medium = "Vinyl";
            osmTemp.setValue(info);
            const std::string smIntake = osmTemp.getValueString();

            auto esperarLote = [&] {
                esperarAte([&] { return iw->poolMetadadoLote_.getNumJobs() == 0; }, 10000);
                bombear(300);
            };
            iw->aplicarOriginalSourceMediumAosSelecionados(smIntake); esperarLote();
            int c = contar(true, "source_media", smIntake);
            checar(c == kItensPorLado, "Intake SOURCE MEDIUM written to every selected item" + n(c));
            iw->aplicarCreatorAosSelecionados("Creator Intake"); esperarLote();
            c = contar(true, "dc_creator", "Creator Intake");
            checar(c == kItensPorLado, "Intake CREATOR written to every selected item" + n(c));
            iw->aplicarSubjectAosSelecionados("Subject Intake"); esperarLote();
            c = contar(true, "dc_subject", "Subject Intake");
            checar(c == kItensPorLado, "Intake SUBJECT written to every selected item" + n(c));
            iw->aplicarEventDateAosSelecionados("1975"); esperarLote();
            c = contar(true, "ano", "1975");
            checar(c == kItensPorLado, "Intake EVENT DATE written to every selected item" + n(c));
            iw->aplicarGeolocationAosSelecionados("", "", "Salvador", "", ""); esperarLote();
            c = contarGeo(true, "Salvador");
            checar(c == kItensPorLado, "Intake GEO LOCATION written to every selected item" + n(c));
            iw->aplicarColecaoAosSelecionados("Music"); esperarLote();
            c = contar(true, "collection_type", "Music");
            checar(c == kItensPorLado, "Intake CONTENT written to every selected item" + n(c));

            int smMem = 0, colMem = 0, dataMem = 0;
            for (const auto& it : iw->todosItens_) {
                if (it.sourceMedia.toStdString() == smIntake) ++smMem;
                if (it.collection == "Music") ++colMem;
                if (it.dataCriacao == "1975") ++dataMem;
            }
            checar(smMem == kItensPorLado, "Intake list shows SOURCE MEDIUM on every item without reopening" + n(smMem));
            checar(colMem == kItensPorLado, "Intake list shows CONTENT on every item without reopening" + n(colMem));
            checar(dataMem == kItensPorLado, "Intake list shows EVENT DATE on every item without reopening" + n(dataMem));

            checar(pa->podeDesfazer(), "the Intake batch left an undo entry");
            pa->desfazer();
            bombear(300);
            c = contar(true, "collection_type", "Music");
            checar(c == 0, "undo of the last Intake batch (CONTENT) reverts every item (" + juce::String(c) + " left)");
            c = contar(true, "dc_creator", "Creator Intake");
            checar(c == kItensPorLado, "undo reverted ONLY the last batch, earlier batches stay" + n(c));
        }
    } catch (const std::exception& e) {
        checar(false, juce::String("batch selftest: ") + e.what());
    }
    raiz.deleteRecursively();

    // ------------------------------------------------ Show Recently Ingested
    // Ingerir A, promover -> ligar filtro -> só A. Promover B com o filtro
    // ligado -> só B, sem clicar em nada. Fechar/reabrir -> ligar -> só B.
    // + SUBJECT (card CONTENT TYPE) e contagens da sidebar com o filtro.
    std::cout << "\n-- METADATA: Show Recently Ingested + SUBJECT filter --\n";
    juce::File raizR = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_recentes_selftest_" + juce::Uuid().toDashedString());
    try {
        raizR.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Recentes";
        params.prefixoNomenclatura = "REC";
        auto projeto = matriz::model::Project::criar(raizR.getChildFile("projeto"), params);
        const std::string projetoId = projeto->projetoId();
        std::vector<std::string> antigos, loteA, loteB;
        for (int i = 0; i < 3; ++i) antigos.push_back(inserirItem(projeto->registro(), projetoId, "REC-OLD-" + std::to_string(i), false));
        for (int i = 0; i < 6; ++i) loteA.push_back(inserirItem(projeto->registro(), projetoId, "REC-A-" + std::to_string(i), true));
        for (int i = 0; i < 4; ++i) loteB.push_back(inserirItem(projeto->registro(), projetoId, "REC-B-" + std::to_string(i), true));
        auto& regR = projeto->registro();
        regR.run("UPDATE item SET dc_subject = 'Show, Tour' WHERE id = ?", {matriz::db::Value::of(antigos[0])});
        regR.run("UPDATE item SET dc_subject = 'Tour' WHERE id = ?", {matriz::db::Value::of(antigos[1])});
        const juce::File pastaProjeto = raizR.getChildFile("projeto");

        auto janela = std::make_unique<MainComponent>();
        janela->setBounds(0, 0, 1400, 900);
        janela->abrirProjeto(std::move(projeto));
        bombear(200);

        auto abrirGrid = [&]() -> CatalogWorkspaceComponent* {
            janela->mostrarGrid();
            auto* cw = janela->catalogWorkspace_.get();
            esperarAte([&] { return !cw->mosaico_->snapshotPendente() && cw->mosaico_->totalItensCarregados() > 0; });
            bombear(200);
            return cw;
        };
        auto visiveis = [](CatalogWorkspaceComponent* cw) { return cw->mosaico_->totalItensVisiveis(); };
        auto promover = [&](const std::vector<std::string>& ids) {
            janela->mostrarIntake();
            auto* iw = janela->intakeWorkspace_.get();
            iw->recarregar();
            esperarAte([&] { return !iw->snapshotPendente() && !iw->todosItens_.empty(); });
            std::set<std::string> alvo(ids.begin(), ids.end());
            for (auto& it : iw->todosItens_) it.selecionado = alvo.count(it.id) > 0;
            iw->confirmarSelecaoParaGrid();
            bombear(200);
        };
        auto contagemTotal = [](CatalogWorkspaceComponent* cw) {
            for (const auto& c : cw->categorias_) if (c.chave == "all") return c.contagem;
            return -1;
        };

        auto* cw = abrirGrid();
        // SUBJECT: opções vindas dos subjects existentes, filtro por um deles.
        esperarAte([&] { return !cw->subjectsDisponiveis_.empty(); }, 5000);
        int idTour = -1;
        for (size_t i = 0; i < cw->subjectsDisponiveis_.size(); ++i)
            if (cw->subjectsDisponiveis_[i].first == "Tour") idTour = static_cast<int>(i + 1);
        checar(cw->comboSubject_ != nullptr && idTour > 0 && cw->subjectsDisponiveis_.size() == 2,
               "SUBJECT dropdown lists each existing subject once (" + juce::String((int) cw->subjectsDisponiveis_.size()) + ")");
        if (cw->comboSubject_ && idTour > 0) {
            cw->comboSubject_->setSelectedId(idTour, juce::sendNotificationSync);
            esperarAte([&] { return !cw->mosaico_->snapshotPendente(); });
            bombear(200);
            checar(visiveis(cw) == 2, "SUBJECT \"Tour\" shows only the 2 items tagged with it (" + juce::String(visiveis(cw)) + ")");
            cw->limparTodosOsFiltros(true);
            cw->aplicarFiltrosAdicionais();
            esperarAte([&] { return !cw->mosaico_->snapshotPendente(); });
            bombear(200);
        }

        // Sem nenhuma leva registrada: liga e a grade fica VAZIA (com aviso).
        cw->btnMostrarRecentes_->onClick();
        esperarAte([&] { return !cw->mosaico_->snapshotPendente(); });
        bombear(200);
        checar(visiveis(cw) == 0, "no batch yet: the filter shows an empty grid, not the whole catalog (" + juce::String(visiveis(cw)) + ")");
        cw->btnMostrarRecentes_->onClick();  // desliga

        promover(loteA);
        cw = abrirGrid();
        cw->btnMostrarRecentes_->onClick();
        esperarAte([&] { return !cw->mosaico_->snapshotPendente() && visiveis(cw) == 6; }, 10000);
        checar(visiveis(cw) == 6, "after promoting A, the filter shows only A (" + juce::String(visiveis(cw)) + ")");
        esperarAte([&] { return contagemTotal(cw) == 6; }, 5000);
        checar(contagemTotal(cw) == 6, "sidebar counts follow the recent filter (" + juce::String(contagemTotal(cw)) + ")");

        promover(loteB);  // filtro continua ligado
        cw = abrirGrid();
        esperarAte([&] { return !cw->mosaico_->snapshotPendente() && visiveis(cw) == 4; }, 10000);
        checar(cw->mostrarApenasRecentes_ && visiveis(cw) == 4,
               "promoting B with the filter ON switches to B with no clicks (" + juce::String(visiveis(cw)) + ")");

        janela->fecharProjeto();
        bombear(200);
        janela->abrirProjeto(matriz::model::Project::abrir(pastaProjeto));
        bombear(200);
        cw = abrirGrid();
        cw->btnMostrarRecentes_->onClick();
        esperarAte([&] { return !cw->mosaico_->snapshotPendente() && visiveis(cw) == 4; }, 10000);
        checar(visiveis(cw) == 4, "after closing/reopening the project the filter still shows only B (" + juce::String(visiveis(cw)) + ")");
        std::set<std::string> esperadoB(loteB.begin(), loteB.end());
        auto recentes = janela->projetoAberto()->ultimosItensIngeridos();
        checar(std::set<std::string>(recentes.begin(), recentes.end()) == esperadoB, "the persisted batch is exactly B");
        janela.reset();
    } catch (const std::exception& e) {
        checar(false, juce::String("recent batch selftest: ") + e.what());
    }
    raizR.deleteRecursively();

    // ------------------------------------------ Miniatura no grid do Intake
    // O card entra na grade ANTES da miniatura existir (ingest insere o item
    // e só depois gera a miniatura). O grid não pode guardar "sem miniatura"
    // pra sempre: depois do próximo snapshot, a miniatura tem que aparecer.
    std::cout << "\n-- INTAKE grid: thumbnail generated after the card appeared --\n";
    juce::File raizM = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_intake_thumb_selftest_" + juce::Uuid().toDashedString());
    try {
        raizM.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Thumb";
        params.prefixoNomenclatura = "THB";
        auto projeto = matriz::model::Project::criar(raizM.getChildFile("projeto"), params);
        std::string itemId = inserirItem(projeto->registro(), projeto->projetoId(), "THB-1", true, ".jpg");
        auto st = projeto->registro().prepare("SELECT id FROM arquivo WHERE item_id = ?");
        st.bind(1, matriz::db::Value::of(itemId));
        st.step();
        const std::string arquivoId = st.columnText(0);
        const juce::File pastaProj = projeto->pasta();

        MainComponent janela;
        janela.setBounds(0, 0, 1400, 900);
        janela.abrirProjeto(std::move(projeto));
        bombear(200);
        janela.mostrarIntake();
        auto* iw = janela.intakeWorkspace_.get();
        iw->recarregar();
        esperarAte([&] { return !iw->snapshotPendente() && !iw->todosItens_.empty(); });
        iw->pintarGridParaTeste();  // pede a miniatura: ainda não existe
        bombear(500);
        checar(!iw->miniaturaEmCacheParaTeste(itemId), "before the thumbnail exists the card shows the placeholder");

        // A miniatura "chega" (como o ingest faz alguns segundos depois).
        juce::File mini = pastaProj.getChildFile(".miniaturas").getChildFile(arquivoId + ".jpg");
        mini.getParentDirectory().createDirectory();
        {
            juce::Image img(juce::Image::RGB, 64, 48, true);
            juce::Graphics(img).fillAll(juce::Colours::orange);
            juce::JPEGImageFormat jpeg;
            if (auto out = std::unique_ptr<juce::FileOutputStream>(mini.createOutputStream()))
                jpeg.writeImageToStream(img, *out);
        }
        janela.projetoAberto()->projeto().indice().run(
            "INSERT INTO miniatura (id, item_id, arquivo_id, tipo, caminho_relativo, largura, altura, gerado_em) "
            "VALUES (?, ?, ?, 'miniatura', ?, 64, 48, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(arquivoId), matriz::db::Value::of(".miniaturas/" + arquivoId + ".jpg"),
             matriz::db::Value::of(matriz::model::agoraIso8601())});

        iw->recarregar();  // o ingest recarrega o Intake periodicamente e no fim
        esperarAte([&] { return !iw->snapshotPendente(); });
        bool apareceu = esperarAte([&] {
            iw->pintarGridParaTeste();
            return iw->miniaturaEmCacheParaTeste(itemId);
        }, 5000);
        checar(apareceu, "after the next Intake reload the generated thumbnail shows on the card");
    } catch (const std::exception& e) {
        checar(false, juce::String("intake thumbnail selftest: ") + e.what());
    }
    raizM.deleteRecursively();

    // ------------------------------------------- GEO LOCATION popup (Intake)
    std::cout << "\n-- INTAKE Geo Location: used-locations autocomplete + Add to Favorites --\n";
    juce::File raizG = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_geo_selftest_" + juce::Uuid().toDashedString());
    try {
        raizG.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Geo";
        params.prefixoNomenclatura = "GEO";
        auto projeto = matriz::model::Project::criar(raizG.getChildFile("projeto"), params);
        std::vector<std::string> ids;
        for (int i = 0; i < 4; ++i) ids.push_back(inserirItem(projeto->registro(), projeto->projetoId(), "GEO-" + std::to_string(i), true));
        matriz::analytics::AssetGeolocation porto;
        porto.latitude = -16.4435; porto.longitude = -39.0643;
        porto.city = "Porto Seguro"; porto.stateProvince = "Bahia"; porto.country = "Brazil";
        porto.source = matriz::analytics::GeoSource::UserCoordinates;
        matriz::analytics::AssetGeolocationRepository::salvarEmLote(projeto->registro(), {ids[0], ids[1], ids[2]}, porto);
        matriz::analytics::AssetGeolocation salvador;
        salvador.city = "Salvador"; salvador.country = "Brazil";
        salvador.source = matriz::analytics::GeoSource::UserCity;
        matriz::analytics::AssetGeolocationRepository::salvarEmLote(projeto->registro(), {ids[3]}, salvador);
        IntakeWorkspaceComponent::autotestePopupGeoParaTeste(projeto->registro(), checar);
    } catch (const std::exception& e) {
        checar(false, juce::String("geo popup selftest: ") + e.what());
    }
    raizG.deleteRecursively();

    // ------------------------------- EVENT DATE só com o ano (Intake, lote)
    // Ano igual ao do DATE CREATED original -> DATE CREATED intacto; ano
    // diferente (ou sem data original) -> atualiza. EVENT DATE sempre grava.
    std::cout << "\n-- INTAKE Event Date: year-only keeps an original Date Created of the same year --\n";
    juce::File raizD = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_eventdate_selftest_" + juce::Uuid().toDashedString());
    try {
        raizD.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "EventDate";
        params.prefixoNomenclatura = "EVD";
        auto projeto = matriz::model::Project::criar(raizD.getChildFile("projeto"), params);
        auto& regD = projeto->registro();
        const std::string a = inserirItem(regD, projeto->projetoId(), "EVD-A", true, ".jpg");
        const std::string b = inserirItem(regD, projeto->projetoId(), "EVD-B", true, ".jpg");
        const std::string c = inserirItem(regD, projeto->projetoId(), "EVD-C", true, ".jpg");
        auto gravarCreated = [&](const std::string& id, const std::string& v) {
            regD.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                     "VALUES (?, ?, 'raiz', 0, 'dc_created', ?, 'leitura_tecnica', ?)",
                     {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id), matriz::db::Value::of(v),
                      matriz::db::Value::of(matriz::model::agoraIso8601())});
        };
        gravarCreated(a, "2015-04-17 10:30:24");
        gravarCreated(b, "2014-12-22");

        MainComponent janela;
        janela.setBounds(0, 0, 1400, 900);
        janela.abrirProjeto(std::move(projeto));
        bombear(200);
        auto* pa = janela.projetoAberto();
        auto created = [&](const std::string& id) { return pa->valorCampo(id, "raiz", 0, "dc_created").value_or(""); };
        auto ano = [&](const std::string& id) { return pa->lerMetadado(id, "ano").value_or(""); };

        janela.mostrarIntake();
        auto* iw = janela.intakeWorkspace_.get();
        iw->recarregar();
        esperarAte([&] { return !iw->snapshotPendente() && iw->todosItens_.size() >= 3; });
        iw->selecionarTodos(true);
        auto esperarLote = [&] {
            esperarAte([&] { return iw->poolMetadadoLote_.getNumJobs() == 0; }, 10000);
            bombear(300);
        };
        iw->aplicarEventDateAosSelecionados("2015");
        esperarLote();
        checar(created(a) == "2015-04-17 10:30:24", "A: same year -> original Date Created kept (" + juce::String(created(a)) + ")");
        checar(created(b) == "2015", "B: different year -> Date Created updated (" + juce::String(created(b)) + ")");
        checar(created(c) == "2015", "C: no original Date Created -> set (" + juce::String(created(c)) + ")");
        checar(ano(a) == "2015" && ano(b) == "2015" && ano(c) == "2015", "Event Date written on all three");
        juce::String telaA;
        for (const auto& it : iw->todosItens_) if (it.id == a) telaA = it.dataCriacao;
        checar(telaA == "2015-04-17 10:30:24", "the Intake list still shows A's original Date Created (" + telaA + ")");

        pa->desfazer();
        bombear(200);
        checar(created(b) == "2014-12-22" && created(a) == "2015-04-17 10:30:24" && ano(a).empty(),
               "one undo reverts the whole Event Date batch");

        iw->aplicarEventDateAosSelecionados("2015-05-01");  // data completa: sempre atualiza
        esperarLote();
        checar(created(a) == "2015-05-01", "a full date (not year-only) updates Date Created (" + juce::String(created(a)) + ")");
    } catch (const std::exception& e) {
        checar(false, juce::String("event date selftest: ") + e.what());
    }
    raizD.deleteRecursively();

    // ------------------------------------------------ Etapa 3: papéis/versões
    std::cout << "\n-- Backup versions: MAIN / CLONE / SOURCE roles + old-project migration --\n";
    juce::File raizV = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_versions_selftest_" + juce::Uuid().toDashedString());
    try {
        raizV.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Versions";
        params.prefixoNomenclatura = "VER";
        juce::File raizMain = raizV.getChildFile("MAIN");
        juce::File raizClone1 = raizV.getChildFile("CLONE1"), raizClone2 = raizV.getChildFile("CLONE2");
        raizClone1.createDirectory();
        juce::File discoSource = raizV.getChildFile("CARD");
        discoSource.createDirectory();
        auto projeto = matriz::model::Project::criar(raizMain, params);
        auto& reg = projeto->registro();
        const std::string agora = matriz::model::agoraIso8601();
        auto inserirDestino = [&](const std::string& id, const juce::File& raiz, const std::string& papel, long long rev) {
            reg.run("INSERT INTO backup_destino (id, destino_path, rotulo, ativo, criado_em, destination_id, papel, "
                    "ultima_revisao_conhecida, ultimo_visto_em) VALUES (?, ?, ?, 1, ?, ?, ?, ?, '2026-09-20T10:00:00Z')",
                    {matriz::db::Value::of(id), matriz::db::Value::of(raiz.getFullPathName().toStdString()),
                     matriz::db::Value::of(raiz.getFileName().toStdString()), matriz::db::Value::of(agora),
                     matriz::db::Value::of(id), matriz::db::Value::of(papel), matriz::db::Value::of(rev)});
        };
        inserirDestino("clone-1", raizClone1, "CLONE", 1);   // em dia (revisão 1)
        inserirDestino("clone-2", raizClone2, "CLONE", 0);   // desatualizado, offline
        // SOURCE: um volume com 2 arquivos, nenhum no MAIN ainda.
        reg.run("INSERT INTO vault (id, projeto_id, nome, tipo, localizacao, status, criado_em) "
                "VALUES ('vault-card', ?, 'CARD', 'local', ?, 'online', ?)",
                {matriz::db::Value::of(projeto->projetoId()), matriz::db::Value::of(discoSource.getFullPathName().toStdString()),
                 matriz::db::Value::of(agora)});
        std::vector<std::string> doCartao;
        for (int i = 0; i < 2; ++i) {
            auto itemId = inserirItem(reg, projeto->projetoId(), "VER-" + std::to_string(i), false, ".jpg");
            reg.run("UPDATE arquivo SET vault_id = 'vault-card' WHERE item_id = ?", {matriz::db::Value::of(itemId)});
            auto st = reg.prepare("SELECT id FROM arquivo WHERE item_id = ?");
            st.bind(1, matriz::db::Value::of(itemId));
            st.step();
            doCartao.push_back(st.columnText(0));
        }

        MainComponent janela;
        janela.setBounds(0, 0, 1400, 900);
        janela.abrirProjeto(std::move(projeto));
        bombear(300);
        auto* pa = janela.projetoAberto();
        auto& db = pa->projeto().registro();
        auto papelDe = [&](const std::string& id) {
            auto st = db.prepare("SELECT papel FROM backup_destino WHERE id = ?");
            st.bind(1, matriz::db::Value::of(id));
            return st.step() ? st.columnText(0) : std::string();
        };
        const std::string idMain = pa->projeto().destinationId();

        auto sit = pa->normalizarPapelMain();
        checar(sit.tipo == ProjetoAberto::SituacaoMain::Tipo::Ok && papelDe(idMain) == "ORIGINAL",
               "a project with exactly one MAIN is left as is");

        // Projeto antigo sem MAIN: pergunta; a escolha deixa exatamente um MAIN.
        db.run("UPDATE backup_destino SET papel = 'CLONE'", {});
        sit = pa->normalizarPapelMain();
        checar(sit.tipo == ProjetoAberto::SituacaoMain::Tipo::Perguntar && sit.opcoes.size() == 3,
               "no MAIN registered -> ask once, listing the " + juce::String((int) sit.opcoes.size()) + " versions");
        pa->definirMain(idMain);
        checar(papelDe(idMain) == "ORIGINAL" && papelDe("clone-1") == "CLONE" && papelDe("clone-2") == "CLONE",
               "choosing the MAIN makes it ORIGINAL and the others CLONE");

        // Dois ORIGINAL (bancos antigos): a raiz aberta fica MAIN, o outro vira CLONE.
        db.run("UPDATE backup_destino SET papel = 'ORIGINAL' WHERE id = 'clone-1'", {});
        sit = pa->normalizarPapelMain();
        checar(sit.tipo == ProjetoAberto::SituacaoMain::Tipo::Ok && papelDe(idMain) == "ORIGINAL" && papelDe("clone-1") == "CLONE",
               "two ORIGINAL rows -> the opened root stays MAIN, the other becomes CLONE (labels only)");
        checar(raizClone1.isDirectory() && raizMain.getChildFile("destination.json").existsAsFile(),
               "nothing on disk was moved or deleted by the migration");

        using Papel = ProjetoAberto::VersaoResumo::Papel;
        auto versoes = pa->listarVersoes();
        auto achar = [&](const std::vector<ProjetoAberto::VersaoResumo>& vs, Papel p, const std::string& id = {}) {
            for (const auto& v : vs) if (v.papel == p && (id.empty() || v.id == id)) return &v;
            return static_cast<const ProjetoAberto::VersaoResumo*>(nullptr);
        };
        checar(!versoes.empty() && versoes.front().papel == Papel::Main, "the MAIN is always the first row");
        auto* c1 = achar(versoes, Papel::Clone, "clone-1");
        auto* c2 = achar(versoes, Papel::Clone, "clone-2");
        checar(c1 && !c1->desatualizado && c1->online, "CLONE with the current revision -> up to date");
        checar(c2 && c2->desatualizado && !c2->online && c2->ultimaData.startsWith("2026-09-20"),
               "CLONE behind the project revision -> out of date since its last sync");
        auto* src = achar(versoes, Papel::Source);
        checar(src && src->online && src->totalItens == 2 && src->dependentes == 2,
               "the SOURCE row shows its files and how many still depend on it (" +
                   juce::String(src ? src->dependentes : -1) + ")");

        checar(pa->arquivosQueDependemDoSource() == 2, "end-of-backup check counts the files still on the SOURCE only (" +
                                                           juce::String(pa->arquivosQueDependemDoSource()) + ")");
        // Os dois arquivos entram no MAIN (registro legado): SOURCE liberado.
        for (const auto& arqId : doCartao)
            db.run("INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, checksum_sha256, consolidado_em) "
                   "SELECT ?, item_id, '', id, 'x.jpg', 'abc', ? FROM arquivo WHERE id = ?",
                   {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(agora), matriz::db::Value::of(arqId)});
        versoes = pa->listarVersoes();
        src = achar(versoes, Papel::Source);
        checar(src && src->dependentes == 0, "all its files in the MAIN -> SOURCE safe to disconnect");
        checar(pa->arquivosQueDependemDoSource() == 0, "end-of-backup check: no file depends on the SOURCE any more");
        auto* mainRow = achar(versoes, Papel::Main);
        checar(mainRow && mainRow->totalItens == 2, "legacy consolidation records count for the MAIN (" +
                                                        juce::String(mainRow ? mainRow->totalItens : -1) + ")");
        discoSource.deleteRecursively();
        versoes = pa->listarVersoes();
        src = achar(versoes, Papel::Source);
        checar(src && !src->online, "SOURCE disconnected -> stored");

        // Visual da lista (pra conferir a olho): test-output/backup_versions.png
        {
            BackupVersionsComponent lista(*pa);
            lista.setSize(1100, 420);
            esperarAte([&] { return lista.getNumRows() > 0; }, 5000);
            bombear(100);
            if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
                juce::PNGImageFormat png;
                auto arq = dir.getChildFile("backup_versions.png");
                arq.deleteFile();
                if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                    png.writeImageToStream(lista.createComponentSnapshot(lista.getLocalBounds()), *out);
            }
            checar(lista.getNumRows() == 4, "the Versions list shows MAIN, 2 CLONEs and the SOURCE (" +
                                                juce::String(lista.getNumRows()) + " rows)");
        }
    } catch (const std::exception& e) {
        checar(false, juce::String("versions selftest: ") + e.what());
    }
    raizV.deleteRecursively();

    // ------------------------------------ Etapa 4: sincronizar clones só adições
    std::cout << "\n-- Sync clones after ADD TO MAIN: additions only, nothing removed --\n";
    juce::File raizS = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_syncadd_selftest_" + juce::Uuid().toDashedString());
    try {
        raizS.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "SyncAdd";
        params.prefixoNomenclatura = "SYN";
        juce::File raizMain = raizS.getChildFile("MAIN"), raizClone = raizS.getChildFile("CLONE");
        auto projeto = matriz::model::Project::criar(raizMain, params);
        raizMain.getChildFile("Media").createDirectory();
        raizMain.getChildFile("Media").getChildFile("novo.txt").replaceWithText("novo no MAIN");
        raizClone.getChildFile("Media").createDirectory();
        raizClone.getChildFile("Media").getChildFile("so_no_clone.txt").replaceWithText("existe so no clone");
        matriz::model::DestinationInfo info;
        info.formato = 1;
        info.destinationId = "clone-sync";
        info.projetoId = projeto->projetoId();
        info.papel = "CLONE";
        info.rotulo = "CLONE";
        info.revisao = 1;
        info.gravarEmArquivo(raizClone.getChildFile("destination.json"));
        projeto->registro().run(
            "INSERT INTO backup_destino (id, destino_path, rotulo, ativo, criado_em, destination_id, papel, ultima_revisao_conhecida) "
            "VALUES ('clone-sync', ?, 'CLONE', 1, ?, 'clone-sync', 'CLONE', 1)",
            {matriz::db::Value::of(raizClone.getFullPathName().toStdString()), matriz::db::Value::of(matriz::model::agoraIso8601())});

        auto status = matriz::sync::SyncEngine::executarEspelhamentoAutomatico(*projeto, {}, /*aplicarRemocoes*/ false);
        bool aplicado = false;
        for (const auto& st : status)
            if (st.destinationId == "clone-sync" && st.estado == matriz::sync::SyncEngine::StatusEspelhamento::Estado::Aplicado)
                aplicado = true;
        checar(aplicado, "the clone sync ran");
        checar(raizClone.getChildFile("Media").getChildFile("novo.txt").existsAsFile(), "the new MAIN file reached the clone");
        checar(raizClone.getChildFile("Media").getChildFile("so_no_clone.txt").existsAsFile(),
               "a file that exists only in the clone was NOT removed (removals need confirmation, stage 7)");
    } catch (const std::exception& e) {
        checar(false, juce::String("clone sync selftest: ") + e.what());
    }
    raizS.deleteRecursively();

    // ------------------------- Duplicates: sanitizar (o descartado não vai pro MAIN)
    std::cout << "\n-- Duplicates: the discarded side stays in SOURCE, out of backup, nothing deleted --\n";
    juce::File raizSan = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_dup_selftest_" + juce::Uuid().toDashedString());
    try {
        raizSan.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Dups";
        params.prefixoNomenclatura = "DUP";
        auto projeto = matriz::model::Project::criar(raizSan.getChildFile("MAIN"), params);
        auto& reg = projeto->registro();
        const std::string projetoId = projeto->projetoId();
        using matriz::db::Value;
        auto texto = [&](const std::string& sql, const std::string& id) {
            auto st = reg.prepare(sql);
            st.bind(1, Value::of(id));
            return st.step() ? st.columnText(0) : std::string("<none>");
        };
        auto planejados = [&]() {
            std::set<std::string> ids;
            auto plano = matriz::consolidacao::planejarConsolidacao(reg, projeto->pasta(), raizSan.getChildFile("MAIN"),
                                                                    {matriz::consolidacao::NivelHierarquia::Origem});
            for (const auto& ip : plano.itens) ids.insert(ip.itemId);
            return ids;
        };

        const std::string manter = inserirItem(reg, projetoId, "DUP-KEEP", false);
        const std::string descartar = inserirItem(reg, projetoId, "DUP-DROP", false);
        reg.run("UPDATE item SET dc_creator = 'Creator Keep', notas_livres = 'nota do mantido' WHERE id = ?", {Value::of(manter)});
        reg.run("UPDATE item SET dc_creator = 'Creator Drop', source_media = 'Cassette', "
                "notas_livres = 'nota do descartado' WHERE id = ?", {Value::of(descartar)});
        reg.run("INSERT INTO item_tag (id, item_id, tag) VALUES (?, ?, 'show'), (?, ?, 'ao vivo')",
                {Value::of(matriz::model::novoUuid()), Value::of(manter), Value::of(matriz::model::novoUuid()),
                 Value::of(descartar)});
        auto antes = planejados();
        checar(antes.count(manter) && antes.count(descartar), "before: both items are in the backup plan");

        reg.run("BEGIN TRANSACTION", {});
        auto r = ProjetoAberto::sanitizarDuplicata(reg, manter, descartar);
        reg.run("COMMIT", {});

        auto depois = planejados();
        checar(depois.count(manter) && !depois.count(descartar), "after: only the kept item goes to the MAIN");
        checar(texto("SELECT estado FROM item WHERE id = ?", descartar) == "duplicata", "the discarded item stays in the catalog as 'duplicata'");
        checar(texto("SELECT COUNT(*) FROM arquivo WHERE item_id = ?", descartar) == "1", "its file record (SOURCE location) is kept");
        checar(texto("SELECT source_media FROM item WHERE id = ?", manter) == "Cassette", "an empty field of the kept item is filled from the duplicate");
        checar(texto("SELECT dc_creator FROM item WHERE id = ?", manter) == "Creator Keep", "a filled field of the kept item is not overwritten");
        checar(texto("SELECT COUNT(*) FROM item_tag WHERE item_id = ?", manter) == "2", "tags are merged (union)");
        const std::string notasK = texto("SELECT notas_livres FROM item WHERE id = ?", manter);
        checar(juce::String(notasK).startsWith("nota do mantido") && juce::String(notasK).contains("Creator Drop")
                   && juce::String(notasK).contains("nota do descartado") && juce::String(notasK).contains("DUP-DROP")
                   && juce::String(notasK).contains("originais/DUP-DROP.wav"),
               "kept notes: appended (not overwritten) with differing value, duplicate notes, name and location");
        checar(juce::String(texto("SELECT notas_livres FROM item WHERE id = ?", descartar)).startsWith("nota do descartado"),
               "discarded notes appended, not overwritten");
        checar(texto("SELECT COUNT(*) FROM preservation_event WHERE item_id = ? AND event_type = 'VALIDATION'", descartar) == "1",
               "a VALIDATION event is logged for the discarded item");
        checar(r.linhasLog.size() >= 3, "log lines for log.md were produced");
    } catch (const std::exception& e) {
        checar(false, juce::String("duplicates selftest: ") + e.what());
    }
    raizSan.deleteRecursively();

    // --------------------------- Etapa 5: travas do MAPA e da Configuração
    std::cout << "\n-- After the MAIN exists: folder map and backup settings are locked --\n";
    juce::File raizT = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_travas_selftest_" + juce::Uuid().toDashedString());
    try {
        raizT.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Travas";
        params.prefixoNomenclatura = "TRV";
        auto projeto = matriz::model::Project::criar(raizT.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        auto* bruto = projeto.get();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = bruto->registro();
        using matriz::db::Value;
        const std::string pastaEvento = pa.criarPastaAcervo("Evento", std::nullopt);
        const std::string pastaLivre = pa.criarPastaAcervo("Livre", std::nullopt);
        const std::string item = inserirItem(reg, projetoId, "TRV-1", false);
        reg.run("INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
                {Value::of(matriz::model::novoUuid()), Value::of(item), Value::of(pastaEvento),
                 Value::of(matriz::model::agoraIso8601())});
        auto nomeDe = [&](const std::string& id) {
            auto st = reg.prepare("SELECT nome FROM acervo_pasta WHERE id = ?");
            st.bind(1, Value::of(id));
            return st.step() ? st.columnText(0) : std::string();
        };
        checar(!pa.mainExiste() && pa.renomearPastaAcervo(pastaEvento, "Evento 1") && nomeDe(pastaEvento) == "Evento 1",
               "before the first backup the folder map is free");

        // Primeiro backup (registro legado, sem destino gravado).
        reg.run("INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                "checksum_sha256, consolidado_em) SELECT ?, item_id, ?, id, 'Evento 1/TRV-1.wav', 'abc', ? FROM arquivo "
                "WHERE item_id = ?",
                {Value::of(matriz::model::novoUuid()), Value::of(pastaEvento), Value::of(matriz::model::agoraIso8601()),
                 Value::of(item)});
        checar(pa.mainExiste(), "the MAIN exists after the first backup");
        checar(!pa.renomearPastaAcervo(pastaEvento, "Outro nome") && nomeDe(pastaEvento) == "Evento 1",
               "renaming a folder with files in the MAIN is blocked");
        checar(!pa.moverPastaAcervo(pastaEvento, pastaLivre), "moving a folder with files in the MAIN is blocked");
        checar(!pa.apagarPastaAcervo(pastaEvento) && !nomeDe(pastaEvento).empty(), "deleting a folder with files in the MAIN is blocked");
        checar(!pa.criarPastaAcervo("Nova", std::nullopt).empty(), "creating a new folder is still allowed");
        checar(pa.renomearPastaAcervo(pastaLivre, "Livre 2"), "a folder with nothing in the MAIN can still be renamed");

        BackupWorkspaceComponent bw(pa, {});
        bw.atualizarTravasDoMain();
        checar(bw.mainSelado_ && !bw.configTravada_,
               "old MAIN without saved settings: not locked yet (locks on the next backup)");
        checar(!bw.toggleEmbutirMetadados_->isVisible() && !bw.toggleForcarRebackup_->isVisible(),
               "with a MAIN, embed and force-full-backup are gone from the add flow");
        bw.comboOrg_->setSelectedId(3, juce::dontSendNotification);
        bw.comboModoPrefixo_->setSelectedId(2, juce::dontSendNotification);
        bw.gravarConfigDoMain();
        bw.comboOrg_->setSelectedId(1, juce::dontSendNotification);  // operador tenta mudar depois
        bw.comboModoPrefixo_->setSelectedId(1, juce::dontSendNotification);
        bw.atualizarTravasDoMain();
        checar(bw.configTravada_ && bw.comboOrg_->getSelectedId() == 3 && !bw.comboOrg_->isEnabled(),
               "folder structure comes back to the first-backup choice, greyed out");
        checar(bw.comboModoPrefixo_->getSelectedId() == 2 && !bw.comboModoPrefixo_->isEnabled() &&
                   !bw.togglePreservarEstrutura_->isEnabled(),
               "naming / prefix locked too");
        checar(bw.labelOrg_->getText().contains(matriz::i18n::t("backup.definido_primeiro_backup")),
               "the section says it was set in the first backup");
        checar(!bw.organizarPorSource_, "an old MAIN keeps its naming rule (no per-SOURCE suffix)");
        checar(bw.btnAtualizarSidecars_ && bw.btnAtualizarSidecars_->isVisible(),
               "with a MAIN, UPDATE SIDECARS takes the place of embed");
        // Etapa 6: EXPORT ao lado do botão principal; planilha com nome novo.
        bw.setSize(1700, 900);
        bw.resized();
        checar(bw.btnExportar_ && bw.btnExportar_->isVisible(), "EXPORT button is on the backup screen");
        checar(bw.btnExportJanela_->getButtonText() == matriz::i18n::t("backup.btn_exportar_metadados") &&
                   bw.btnExportJanela_->getButtonText().containsIgnoreCase(matriz::i18n::localeAtivo() == "pt_BR" ? "planilha" : "spreadsheet"),
               "the metadata export button is now the metadata SPREADSHEET export");
        if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
            bombear(200);
            juce::PNGImageFormat png;
            auto arq = dir.getChildFile("backup_config_locked.png");
            arq.deleteFile();
            if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                png.writeImageToStream(bw.createComponentSnapshot(bw.getLocalBounds()), *out);
        }
    } catch (const std::exception& e) {
        checar(false, juce::String("locks selftest: ") + e.what());
    }
    raizT.deleteRecursively();

    // ------------------------- Etapa 7: CLONAR, sincronizar clone, promover a MAIN
    std::cout << "\n-- Clone MAIN / SOURCE, sync clone (removals only on confirm), promote to MAIN --\n";
    juce::File raizC = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_clones_selftest_" + juce::Uuid().toDashedString());
    try {
        raizC.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Clones";
        params.prefixoNomenclatura = "CLN";
        juce::File raizMain = raizC.getChildFile("MAIN");
        auto projeto = matriz::model::Project::criar(raizMain, params);
        const std::string projetoId = projeto->projetoId();
        const std::string idMain = projeto->destinationId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        using matriz::db::Value;
        using matriz::sync::SyncEngine;
        pa.listarVersoes();       // registra o MAIN em backup_destino
        pa.normalizarPapelMain();
        raizMain.getChildFile("Media").createDirectory();
        raizMain.getChildFile("Media").getChildFile("a.txt").replaceWithText("arquivo a");
        const std::string item = inserirItem(reg, projetoId, "CLN-1", false);
        reg.run("INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                "checksum_sha256, consolidado_em, destino_path, destino_id) SELECT ?, item_id, '', id, 'a.txt', 'x', ?, ?, ? "
                "FROM arquivo WHERE item_id = ?",
                {Value::of(matriz::model::novoUuid()), Value::of(matriz::model::agoraIso8601()),
                 Value::of(raizMain.getChildFile("Media").getFullPathName().toStdString()), Value::of(idMain), Value::of(item)});

        // 1. CLONAR o MAIN numa pasta vazia.
        juce::File pastaClone = raizC.getChildFile("DISCO2");
        pastaClone.createDirectory();
        auto rc = SyncEngine::clonarMain(pa.projeto(), pastaClone);
        auto info = matriz::model::DestinationInfo::lerDeArquivo(rc.raiz.getChildFile("destination.json"));
        checar(rc.sucesso && rc.raiz == pastaClone && info && info->papel == "CLONE" && info->projetoId == projetoId,
               "CLONE of the MAIN: registered as CLONE of this project");
        checar(rc.raiz.getChildFile("Media").getChildFile("a.txt").existsAsFile() &&
                   rc.raiz.getChildFile("Project").getChildFile("registro.sqlite").existsAsFile(),
               "the clone has the media and the project database");

        // 2. Arquivo novo no MAIN + arquivo que só existe no clone.
        raizMain.getChildFile("Media").getChildFile("b.txt").replaceWithText("arquivo b");
        rc.raiz.getChildFile("Media").getChildFile("so_no_clone.txt").replaceWithText("so aqui");
        auto plano = SyncEngine::compararCloneDoMain(pa.projeto(), rc.id);
        int novos = 0, removidos = 0;
        for (const auto& it : plano.itens) {
            if (it.categoria != matriz::sync::CategoriaSync::Media) continue;
            if (it.classe == matriz::sync::ClasseSync::Removido) ++removidos;
            else if (it.classe != matriz::sync::ClasseSync::Igual) ++novos;
        }
        checar(novos == 1 && removidos == 1, "sync compare: 1 addition and 1 pending removal, listed separately");
        auto rs = SyncEngine::sincronizarCloneDoMain(pa.projeto(), rc.id, /*aplicarRemocoes*/ false);
        checar(rs.sucesso && rc.raiz.getChildFile("Media").getChildFile("b.txt").existsAsFile() &&
                   rc.raiz.getChildFile("Media").getChildFile("so_no_clone.txt").existsAsFile(),
               "sync without confirmation: addition copied, clone-only file kept");
        rs = SyncEngine::sincronizarCloneDoMain(pa.projeto(), rc.id, /*aplicarRemocoes*/ true);
        bool naLixeira = false;
        for (const auto& e : juce::RangedDirectoryIterator(rc.raiz.getChildFile("Project").getChildFile("_lixeira"), true,
                                                          "so_no_clone.txt", juce::File::findFiles)) {
            (void) e;
            naLixeira = true;
        }
        checar(!rc.raiz.getChildFile("Media").getChildFile("so_no_clone.txt").existsAsFile() && naLixeira,
               "confirmed removal goes to the clone's _lixeira (never deleted)");

        // 3. PROMOVER A MAIN.
        juce::String erro;
        checar(SyncEngine::promoverAMain(pa.projeto(), rc.id, erro), "promote to MAIN: " + erro);
        auto papel = [&](matriz::db::Database& d, const std::string& id) {
            auto st = d.prepare("SELECT papel FROM backup_destino WHERE id = ? OR destination_id = ?");
            st.bind(1, Value::of(id));
            st.bind(2, Value::of(id));
            return st.step() ? st.columnText(0) : std::string("-");
        };
        checar(papel(reg, rc.id) == "ORIGINAL" && papel(reg, idMain) == "CLONE", "open database: clone is MAIN, old MAIN is CLONE");
        {
            auto st = reg.prepare("SELECT destino_id FROM consolidacao_registro LIMIT 1");
            checar(st.step() && st.columnText(0) == rc.id, "copy records now point to the new MAIN (mirror paths)");
        }
        {
            matriz::db::Database dbClone(rc.raiz.getChildFile("Project").getChildFile("registro.sqlite").getFullPathName().toStdString());
            checar(papel(dbClone, rc.id) == "ORIGINAL", "the clone's own database also says it is the MAIN");
        }
        auto infoNovo = matriz::model::DestinationInfo::lerDeArquivo(rc.raiz.getChildFile("destination.json"));
        auto infoVelho = matriz::model::DestinationInfo::lerDeArquivo(raizMain.getChildFile("destination.json"));
        checar(infoNovo && infoNovo->papel == "ORIGINAL" && infoVelho && infoVelho->papel == "CLONE",
               "destination.json updated on both drives");
        checar(raizMain.getChildFile("Media").getChildFile("a.txt").existsAsFile(), "nothing deleted from the old MAIN");

        // 4. CLONAR um SOURCE (cópia bruta com checksums).
        // Disco que não está em /Volumes: o clone copia a pasta comum aos
        // arquivos ingeridos dele ("originais/", do inserirItem).
        juce::File card = raizC.getChildFile("CARD");
        juce::File base = card.getChildFile("originais");
        base.getChildFile("DCIM").createDirectory();
        base.getChildFile("DCIM").getChildFile("IMG_1.jpg").replaceWithText("foto 1");
        base.getChildFile("DCIM").getChildFile("IMG_2.jpg").replaceWithText("foto 2");
        base.getChildFile(".DS_Store").replaceWithText("x");
        reg.run("INSERT INTO vault (id, projeto_id, nome, tipo, localizacao, status, criado_em) VALUES ('vault-card', ?, 'CARD', 'local', ?, 'online', ?)",
                {Value::of(projetoId), Value::of(card.getFullPathName().toStdString()), Value::of(matriz::model::agoraIso8601())});
        const std::string itemCard = inserirItem(reg, projetoId, "CLN-2", false, ".jpg");
        reg.run("UPDATE arquivo SET vault_id = 'vault-card' WHERE item_id = ?", {Value::of(itemCard)});
        juce::File destSrc = raizC.getChildFile("DISCO3");
        destSrc.createDirectory();
        auto rsrc = SyncEngine::clonarSource(pa.projeto(), "vault-card", destSrc);
        const juce::String manifesto = rsrc.raiz.getChildFile("checksums.sha256").loadFileAsString();
        checar(rsrc.sucesso && rsrc.copiados == 2 && rsrc.raiz.getChildFile("DCIM").getChildFile("IMG_2.jpg").existsAsFile() &&
                   !rsrc.raiz.getChildFile(".DS_Store").exists(),
               "CLONE SOURCE: original structure and names, system files skipped (" + rsrc.raiz.getFileName() + ")");
        checar(manifesto.contains("DCIM/IMG_1.jpg") && manifesto.contains("DCIM/IMG_2.jpg"), "checksums.sha256 written with the clone");
        base.getChildFile("DCIM").getChildFile("IMG_3.jpg").replaceWithText("foto 3");
        auto ps = SyncEngine::compararCloneDeSource(pa.projeto(), rsrc.id);
        checar(ps.erro.empty() && ps.novos.size() == 1 && ps.removidos.empty(), "SOURCE clone sync sees the new file");
        auto rss = SyncEngine::sincronizarCloneDeSource(pa.projeto(), rsrc.id, false);
        checar(rss.itensCopiados == 1 && rsrc.raiz.getChildFile("DCIM").getChildFile("IMG_3.jpg").existsAsFile(),
               "SOURCE clone synced");
        bool linhaClone = false;
        for (const auto& v : pa.listarVersoes())
            if (v.cloneDeSource && v.id == rsrc.id && v.origem.contains("CARD")) linhaClone = true;
        checar(linhaClone, "the SOURCE clone is listed as CLONE with its origin");
    } catch (const std::exception& e) {
        checar(false, juce::String("clones selftest: ") + e.what());
    }
    raizC.deleteRecursively();

    // ------------------------------- Etapa 8: .xmp do cliente não vira item
    std::cout << "\n-- Ingest: a client .xmp next to its media is a sidecar, not an item --\n";
    {
        juce::File pasta = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("matriz_xmp_ingest_" + juce::Uuid().toDashedString());
        pasta.createDirectory();
        pasta.getChildFile("IMG_1.CR2").replaceWithText("raw");
        pasta.getChildFile("IMG_1.xmp").replaceWithText("<x/>");
        pasta.getChildFile("IMG_2.jpg").replaceWithText("jpg");
        pasta.getChildFile("IMG_2.jpg.xmp").replaceWithText("<x/>");
        pasta.getChildFile("solto.xmp").replaceWithText("<x/>");
        MainComponent janelaXmp;
        juce::Array<juce::File> entrada;
        entrada.add(pasta);
        juce::StringArray nomes;
        for (const auto& f : janelaXmp.expandirArquivos(entrada)) nomes.add(f.getFileName());
        checar(nomes.contains("IMG_1.CR2") && nomes.contains("IMG_2.jpg") && !nomes.contains("IMG_1.xmp") &&
                   !nomes.contains("IMG_2.jpg.xmp") && nomes.contains("solto.xmp"),
               "sidecars are recognised, a lone .xmp is still a file (" + nomes.joinIntoString(", ") + ")");
        pasta.deleteRecursively();
    }

    // --------------------------------- Etapa 9: Duplicates, critérios rápidos
    std::cout << "\n-- Duplicates quick criteria: most recent ingest / first in backup, ties stay manual --\n";
    juce::File raizQ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_criterios_selftest_" + juce::Uuid().toDashedString());
    try {
        raizQ.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Criterios";
        params.prefixoNomenclatura = "CRT";
        auto projeto = matriz::model::Project::criar(raizQ.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        using matriz::db::Value;
        auto novo = [&](const std::string& codigo, const std::string& ingerido) {
            auto id = inserirItem(reg, projetoId, codigo, false);
            reg.run("UPDATE arquivo SET criado_em = ? WHERE item_id = ?", {Value::of(ingerido), Value::of(id)});
            return id;
        };
        auto noBackup = [&](const std::string& id, const std::string& quando) {
            reg.run("INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                    "checksum_sha256, consolidado_em) SELECT ?, item_id, '', id, ?, 'x', ? FROM arquivo WHERE item_id = ?",
                    {Value::of(matriz::model::novoUuid()), Value::of(id + ".wav"), Value::of(quando), Value::of(id)});
        };
        auto par = [&](const std::string& a, const std::string& b) {
            DuplicatesWorkspaceComponent::DuplicateGroup g;
            g.original.itemId = a;
            g.duplicata.itemId = b;
            return g;
        };
        auto estado = [&](const std::string& id) {
            auto st = reg.prepare("SELECT estado FROM item WHERE id = ?");
            st.bind(1, Value::of(id));
            return st.step() ? st.columnText(0) : std::string();
        };
        // Par A: o 1 entrou no backup; o 2 foi ingerido depois.
        const auto a1 = novo("CRT-A1", "2026-01-01T10:00:00Z"), a2 = novo("CRT-A2", "2026-03-01T10:00:00Z");
        noBackup(a1, "2026-01-05T10:00:00Z");
        // Par B: nenhum tem backup.
        const auto b1 = novo("CRT-B1", "2026-02-01T10:00:00Z"), b2 = novo("CRT-B2", "2026-01-01T10:00:00Z");

        // Lote grande: um aviso só pra grade (não um por item).
        struct Contador : EventBusListener {
            int porItem = 0, recarga = 0;
            void aoItemAlterado(const EventoItemAlterado& e) override {
                if (e.tipoAlteracao == "recarregar_tudo") ++recarga; else ++porItem;
            }
        } contador;
        EventBus::obterInstancia().registrarListener(&contador);
        {
            DuplicatesWorkspaceComponent dwLote(pa);
            for (int i = 0; i < 15; ++i) {
                const auto x = novo("CRT-L" + std::to_string(i) + "a", "2026-01-01T10:00:00Z");
                const auto y = novo("CRT-L" + std::to_string(i) + "b", "2026-02-01T10:00:00Z");
                dwLote.gruposDetectados_.push_back(par(x, y));
            }
            dwLote.aplicarEscolhaGlobal(1);
        }
        EventBus::obterInstancia().removerListener(&contador);
        checar(contador.recarga == 1 && contador.porItem == 0,
               "Validate All on 15 pairs: ONE grid reload event, not one per item (" + juce::String(contador.porItem) + ")");

        DuplicatesWorkspaceComponent dw(pa);
        dw.gruposDetectados_ = {par(a1, a2), par(b1, b2)};
        dw.aplicarEscolhaGlobal(5);  // manter a primeira no backup
        checar(estado(a2) == "duplicata" && estado(a1) != "duplicata", "first in backup: pair A keeps file 1");
        checar(dw.gruposDetectados_.size() == 1 && dw.gruposDetectados_.front().original.itemId == b1 &&
                   estado(b1) != "duplicata" && estado(b2) != "duplicata",
               "no backup on either side: pair B stays for a manual decision (flagged)");
        dw.aplicarEscolhaGlobal(4);  // manter a ingestão mais recente
        checar(estado(b2) == "duplicata" && estado(b1) != "duplicata" && dw.gruposDetectados_.empty(),
               "most recent ingest: pair B keeps the newer file");
    } catch (const std::exception& e) {
        checar(false, juce::String("criteria selftest: ") + e.what());
    }
    raizQ.deleteRecursively();

    // ------------------------------ Etapa 10: INTAKE, Enter = APPLY nos popups
    std::cout << "\n-- INTAKE batch popups: Enter = APPLY, Esc closes, autocomplete confirms first --\n";
    IntakeWorkspaceComponent::autotesteTeclasPopupsParaTeste(checar);

    // ------------------------- Intake: Cmd+Z desfaz Reject e Send to Grid
    std::cout << "\n-- Intake undo: Reject and Send to Grid come back with Cmd+Z --\n";
    juce::File raizU = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_undo_intake_" + juce::Uuid().toDashedString());
    try {
        raizU.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "UndoIntake";
        params.prefixoNomenclatura = "UND";
        auto projeto = matriz::model::Project::criar(raizU.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        using matriz::db::Value;
        const auto i1 = inserirItem(reg, projetoId, "UND-1", true), i2 = inserirItem(reg, projetoId, "UND-2", true);
        reg.run("INSERT INTO item_tag (id, item_id, tag) VALUES (?, ?, 'show')", {Value::of(matriz::model::novoUuid()), Value::of(i1)});
        reg.run("UPDATE item SET dc_creator = 'Fulano' WHERE id = ?", {Value::of(i2)});
        auto contar = [&](const std::string& sql) {
            auto st = reg.prepare(sql);
            return st.step() ? st.columnInt(0) : -1LL;
        };
        pa.removerItensDoProjeto({i1, i2});
        checar(contar("SELECT COUNT(*) FROM item") == 0 && contar("SELECT COUNT(*) FROM arquivo") == 0, "Reject removes the items");
        checar(pa.podeDesfazer() && pa.desfazer(), "Reject is on the undo stack");
        checar(contar("SELECT COUNT(*) FROM item WHERE em_quarentena = 1") == 2 && contar("SELECT COUNT(*) FROM arquivo") == 2 &&
                   contar("SELECT COUNT(*) FROM item_tag WHERE tag = 'show'") == 1 &&
                   contar("SELECT COUNT(*) FROM item WHERE dc_creator = 'Fulano'") == 1,
               "Cmd+Z brings them back to Intake with files, tags and metadata");
        pa.confirmarLoteGrid({i1, i2});
        checar(contar("SELECT COUNT(*) FROM item WHERE em_quarentena = 0") == 2, "Send to Grid moves them");
        checar(pa.desfazer() && contar("SELECT COUNT(*) FROM item WHERE em_quarentena = 1") == 2,
               "Cmd+Z sends them back to Intake");
    } catch (const std::exception& e) {
        checar(false, juce::String("intake undo selftest: ") + e.what());
    }
    raizU.deleteRecursively();

    // ------------------- Lista de hoje: LOCATE na pasta, autocomplete único, CDR
    std::cout << "\n-- Locate opens in the file's folder, shared autocomplete, CorelDRAW icon --\n";
    {
        juce::File base = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_locate_" + juce::Uuid().toDashedString());
        base.getChildFile("fotos").createDirectory();
        checar(InitialRelinkDialog::pastaInicialParaLocalizar(base.getChildFile("fotos/IMG_1.jpg").getFullPathName()) ==
                   base.getChildFile("fotos"),
               "LOCATE opens straight in the folder where the file was");
        checar(InitialRelinkDialog::pastaInicialParaLocalizar(base.getChildFile("sumiu/sub/IMG_1.jpg").getFullPathName()) == base,
               "folder gone: opens in the nearest folder that still exists");
        checar(InitialRelinkDialog::pastaInicialParaLocalizar("") ==
                   juce::File::getSpecialLocation(juce::File::userHomeDirectory),
               "no path: home folder");
        base.deleteRecursively();
        checar(matriz::ingest::obterLogoParaExtensao("cdr") == "corel.jpeg" &&
                   juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("Assets/corel.jpeg").existsAsFile(),
               "CorelDRAW .cdr files get the Corel icon");
    }
    {
        juce::File raizA = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("matriz_autocomp_" + juce::Uuid().toDashedString());
        try {
            matriz::model::NovoProjetoParams params;
            params.nome = "Autocomp";
            params.prefixoNomenclatura = "ACP";
            auto projeto = matriz::model::Project::criar(raizA.getChildFile("MAIN"), params);
            const auto item = inserirItem(projeto->registro(), projeto->projetoId(), "ACP-1", true);
            // Aplicado no INTAKE (grava direto no item): tem que aparecer na ficha.
            projeto->registro().run("UPDATE item SET dc_creator = 'Banda do Intake' WHERE id = ?",
                                    {matriz::db::Value::of(item)});
            // Digitado na ficha (histórico): tem que aparecer no INTAKE.
            matriz::ficha::AutocompleteRepository::registrar(projeto->registro(), "dc_creator", "Autor da Ficha");
            auto lista = matriz::ficha::AutocompleteRepository::listar(projeto->registro(), "dc_creator");
            const bool temIntake = std::find(lista.begin(), lista.end(), "Banda do Intake") != lista.end();
            const bool temFicha = std::find(lista.begin(), lista.end(), "Autor da Ficha") != lista.end();
            checar(temIntake && temFicha, "CREATOR autocomplete is one list for Intake and Metadata (" +
                                              juce::String((int) lista.size()) + " values)");
        } catch (const std::exception& e) {
            checar(false, juce::String("autocomplete selftest: ") + e.what());
        }
        raizA.deleteRecursively();
    }

    // ------------------- Backup: nada sai do projeto antes do MAIN existir
    std::cout << "\n-- Backup screen: output buttons locked until MAKE BACKUP creates the MAIN --\n";
    juce::File raizS2 = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("matriz_saida_main_" + juce::Uuid().toDashedString());
    try {
        matriz::model::NovoProjetoParams params;
        params.nome = "SaidaMain";
        params.prefixoNomenclatura = "SMN";
        auto projeto = matriz::model::Project::criar(raizS2.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        const auto item = inserirItem(pa.projeto().registro(), projetoId, "SMN-1", false);
        BackupWorkspaceComponent bw(pa, {});
        bw.setSize(1700, 900);
        bw.resolvedDestFolder_ = juce::File();  // nenhum destino destacado
        bw.atualizarResumo();
        bw.resized();
        auto travados = [&] {
            for (auto* b : {static_cast<juce::Button*>(bw.btnPublishHtml_.get()), static_cast<juce::Button*>(bw.btnExportar_.get()),
                            static_cast<juce::Button*>(bw.btnExportJanela_.get()), static_cast<juce::Button*>(bw.btnSyncDestino_.get())})
                if (b == nullptr || b->isEnabled() || b->getTooltip() != matriz::i18n::t("backup.saida_sem_main")) return false;
            return true;
        };
        checar(!bw.mainSelado_ && travados(), "no MAIN: PUBLISH / EXPORT / SPREADSHEET / SYNC locked, with the hint");
        // FAZER BACKUP criou o MAIN (registro de cópia), ainda sem destino destacado.
        pa.projeto().registro().run("INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                                    "checksum_sha256, consolidado_em) SELECT ?, item_id, '', id, 'x.wav', 'abc', ? FROM arquivo WHERE item_id = ?",
                                    {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(matriz::model::agoraIso8601()),
                                     matriz::db::Value::of(item)});
        bw.atualizarResumo();
        bool liberados = bw.mainSelado_;
        for (auto* b : {static_cast<juce::Button*>(bw.btnPublishHtml_.get()), static_cast<juce::Button*>(bw.btnExportar_.get()),
                        static_cast<juce::Button*>(bw.btnExportJanela_.get()), static_cast<juce::Button*>(bw.btnSyncDestino_.get())})
            liberados = liberados && b->isEnabled() && b->getTooltip() != matriz::i18n::t("backup.saida_sem_main");
        checar(liberados, "MAIN created: the 4 unlock even with no destination selected and no H marks");
    } catch (const std::exception& e) {
        checar(false, juce::String("output lock selftest: ") + e.what());
    }
    raizS2.deleteRecursively();

    // ------------------------------- Metadata: "Buscar em" (escopo da busca)
    std::cout << "\n-- Metadata search scope: All / Creator / People-Tags / Extension / Geo --\n";
    juce::File raizBusca = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("matriz_escopo_busca_" + juce::Uuid().toDashedString());
    try {
        matriz::model::NovoProjetoParams params;
        params.nome = "Escopo";
        params.prefixoNomenclatura = "ESC";
        auto projeto = matriz::model::Project::criar(raizBusca.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        using matriz::db::Value;
        const auto comCriador = inserirItem(reg, projetoId, "ESC-1", false);
        const auto comTag = inserirItem(reg, projetoId, "ESC-2", false, ".jpg");
        reg.run("UPDATE item SET dc_creator = 'Maria Bethania' WHERE id = ?", {Value::of(comCriador)});
        reg.run("INSERT INTO item_tag (id, item_id, tag) VALUES (?, ?, 'maria')", {Value::of(matriz::model::novoUuid()), Value::of(comTag)});
        reg.run("INSERT INTO asset_geolocation (asset_id, latitude, longitude, city, country, created_at, updated_at) "
                "VALUES (?, -16.4435, -39.0643, 'Porto Seguro', 'Brazil', ?, ?)",
                {Value::of(comTag), Value::of(matriz::model::agoraIso8601()), Value::of(matriz::model::agoraIso8601())});
        using E = ProjetoAberto::EscopoBusca;
        auto achou = [&](const char* t, E e) { return pa.buscarItens(t, e); };
        checar(achou("maria", E::Criador) == std::set<std::string>{comCriador}, "Creator: only the item whose creator matches");
        checar(achou("maria", E::PessoasTags) == std::set<std::string>{comTag}, "People/Tags: only the tagged item");
        checar(achou("jpg", E::Extensao) == std::set<std::string>{comTag}, "Extension: .jpg");
        checar(achou("porto", E::Geo) == std::set<std::string>{comTag} && achou("-16.44", E::Geo) == std::set<std::string>{comTag},
               "Geo location: by place name and by coordinates");
        checar(achou("maria", E::Notas).empty(), "Notes: nothing when no note matches");
    } catch (const std::exception& e) {
        checar(false, juce::String("search scope selftest: ") + e.what());
    }
    raizBusca.deleteRecursively();

    std::cout << "\n" << (falhas == 0 ? juce::String("ALL TESTS PASSED") : juce::String(falhas) + " FAILURE(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
