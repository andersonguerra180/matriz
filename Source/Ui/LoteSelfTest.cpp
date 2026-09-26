#include "LoteSelfTest.h"

#include <JuceHeader.h>

#include <iostream>

#include "../Model/Project.h"
#include "CatalogWorkspaceComponent.h"
#include "FichaPanelComponent.h"
#include "IntakeWorkspaceComponent.h"
#include "MainComponent.h"
#include "MosaicoComponent.h"
#include "OriginalSourceMedium.h"
#include "ProjetoAberto.h"

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
                        bool quarentena) {
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
             matriz::db::Value::of("originais/" + codigo + ".wav"), matriz::db::Value::of(agora),
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
            // Item 5: tecla E aparece na hora (sem recarregar) e não marca
            // o item como "metadado editado".
            {
                const std::string alvoE = mosaico->todosItensEmMemoria().front().id;
                pa->alternarMarcadoRevisado({alvoE});
                bombear(100);
                bool revisadoMem = false, editadoMem = true;
                for (const auto& it : mosaico->todosItensEmMemoria())
                    if (it.id == alvoE) { revisadoMem = it.marcadoRevisado; editadoMem = it.metadadosEditados; }
                checar(revisadoMem, "E shows on the grid item right away (in memory, no reload)");
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

    std::cout << "\n" << (falhas == 0 ? juce::String("ALL TESTS PASSED") : juce::String(falhas) + " FAILURE(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
