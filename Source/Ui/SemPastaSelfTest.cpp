#include "SemPastaSelfTest.h"

#include <JuceHeader.h>

#include <functional>
#include <iostream>

#include "../I18n/Strings.h"
#include "../Model/Project.h"
#include "ArvoreBackupComponent.h"
#include "CartoesAnalyticsComponent.h"
#include "JanelaPastaFlutuante.h"
#include "MosaicoComponent.h"
#include "ProjetoAberto.h"

namespace matriz::ui {

namespace {

using matriz::db::Value;

void bombear(int ms = 30) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

template <typename Cond>
bool esperarAte(Cond cond, int timeoutMs = 20000) {
    const auto inicio = juce::Time::getMillisecondCounter();
    while (!cond()) {
        if (juce::Time::getMillisecondCounter() - inicio > static_cast<juce::uint32>(timeoutMs)) return false;
        bombear(20);
    }
    return true;
}

std::string inserirItem(matriz::db::Database& reg, const std::string& projetoId, const std::string& codigo, bool quarentena,
                        const char* collectionType = nullptr) {
    const std::string id = matriz::model::novoUuid(), agora = matriz::model::agoraIso8601();
    reg.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
            "VALUES (?, ?, ?, ?, 'digital_audio', 'novo', ?, ?)",
            {Value::of(id), Value::of(projetoId), Value::of(codigo), Value::of(codigo), Value::of(agora), Value::of(agora)});
    reg.run("UPDATE item SET em_quarentena = ? WHERE id = ?", {Value::of(quarentena ? 1 : 0), Value::of(id)});
    if (collectionType != nullptr)
        reg.run("UPDATE item SET collection_type = ? WHERE id = ?", {Value::of(std::string(collectionType)), Value::of(id)});
    reg.run("INSERT INTO arquivo (id, item_id, caminho_relativo, papel, eh_master, tamanho_bytes, criado_em, atualizado_em) "
            "VALUES (?, ?, ?, 'preservation_master', 1, 1024, ?, ?)",
            {Value::of(matriz::model::novoUuid()), Value::of(id), Value::of("originais/" + codigo + ".wav"), Value::of(agora),
             Value::of(agora)});
    return id;
}

juce::MouseEvent clique(juce::Component& c, juce::Point<int> pos) {
    const auto agora = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), juce::ModifierKeys(),
                            juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, agora, pos.toFloat(), agora,
                            1, false);
}

template <typename T>
T* acharFilho(juce::Component& raiz, std::function<bool(T&)> filtro = {}) {
    for (auto* f : raiz.getChildren()) {
        if (auto* t = dynamic_cast<T*>(f); t != nullptr && (!filtro || filtro(*t))) return t;
        if (auto* achou = acharFilho<T>(*f, filtro)) return achou;
    }
    return nullptr;
}

juce::AlertWindow* alertaComTitulo(const juce::String& titulo) {
    auto* mcm = juce::ModalComponentManager::getInstance();
    for (int i = 0; i < mcm->getNumModalComponents(); ++i)
        if (auto* aw = dynamic_cast<juce::AlertWindow*>(mcm->getModalComponent(i)))
            if (aw->getName() == titulo) return aw;
    return nullptr;
}

}  // namespace

int rodarSemPastaSelfTest() {
    std::cout << "== _no folder: Folder Map card, window, NO FOLDER card, IMPORT FROM _no folder ==\n";
    int falhas = 0;
    auto checar = [&](bool ok, const juce::String& descricao) {
        std::cout << (ok ? "  OK   " : "  FAIL ") << descricao << "\n";
        if (!ok) ++falhas;
    };

    juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("matriz_sem_pasta_selftest_" + juce::Uuid().toDashedString());
    try {
        raiz.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "SemPasta";
        params.prefixoNomenclatura = "SPT";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        const std::string mapa = pa.mapaAtivoPadrao();
        pa.definirMapaAtivo(mapa);

        const auto solto = inserirItem(reg, projetoId, "SPT-1", false);
        const auto oculto = inserirItem(reg, projetoId, "SPT-2", false, "Hidden");  // a aba METADATA esconde
        const auto noIntake = inserirItem(reg, projetoId, "SPT-3", true);           // não entra no catálogo
        const auto organizado = inserirItem(reg, projetoId, "SPT-4", false);
        const std::string pastaAuto = pa.criarPastaAcervo("Auto", std::nullopt, mapa);
        pa.adicionarItensAPasta({organizado}, pastaAuto);
        checar(pa.aplicarAutoOrganizacao(pastaAuto, "ano").status == ProjetoAberto::StatusAutoOrg::Ok, "setup: folder Auto has a rule (YEAR)");
        checar(pa.contarItensSemPasta(mapa) == 3, "setup: NO FOLDER counts 3 (loose, Hidden, INTAKE)");

        // ---- 1. card `_no folder` logo abaixo do _trash
        {
            ArvoreBackupComponent arvore(pa);
            arvore.setBounds(0, 0, 1400, 900);
            bombear(150);
            checar(arvore.boundsPastaSemPastaVirtual().isEmpty(), "without a host to open the window there is no _no folder card");
            std::string pastaPedida;
            juce::String nomePedido;
            arvore.aoAbrirPastaJanela = [&](const std::string& id, const juce::String& nome) { pastaPedida = id; nomePedido = nome; };
            const auto trash = arvore.boundsPastaTrash();
            const auto card = arvore.boundsPastaSemPastaVirtual();
            checar(!trash.isEmpty() && !card.isEmpty() && card.getX() == trash.getX() && card.getY() > trash.getBottom() &&
                       card.getY() - trash.getBottom() <= 12 && card.getWidth() == trash.getWidth(),
                   "_no folder sits right below _trash, same width");
            checar(arvore.contagemSemPastaAtual() == 3, "the card shows the same count as NO FOLDER (3)");
            arvore.mouseDown(clique(arvore, card.getCentre()));
            checar(pastaPedida == JanelaPastaFlutuante::kPastaSemPasta && nomePedido == JanelaPastaFlutuante::kNomeSemPasta,
                   "clicking the card asks the host to open the _no folder window");

            // ---- 3. IMPORT FROM _no folder (pasta com auto-organização)
            bool reconta = false;
            arvore.aoMudarSemPasta = [&] { reconta = true; };
            arvore.importarSemPastaNaPasta(pastaAuto, "Auto", "ano");
            const auto titulo = i18n::t("arvore_backup.importar_sem_pasta_titulo");
            checar(esperarAte([&] { return alertaComTitulo(titulo) != nullptr; }), "IMPORT asks for confirmation first");
            if (auto* aw = alertaComTitulo(titulo)) {
                checar(aw->getNumButtons() == 2, "the confirmation offers IMPORT / CANCEL");
                aw->exitModalState(1);
            }
            checar(esperarAte([&] { return reconta; }), "after IMPORT the NO FOLDER card is asked to recount");
            const auto restam = pa.itensSemPasta(mapa);
            checar(restam.size() == 1 && restam.count(noIntake) == 1, "only the INTAKE item is still in _no folder");
            auto pastaDe = [&](const std::string& item) {
                auto st = reg.prepare("SELECT p.nome, COALESCE(pai.nome, ''), COALESCE(p.regra_organizacao, '') FROM acervo_item_pasta aip "
                                      "JOIN acervo_pasta p ON p.id = aip.pasta_id LEFT JOIN acervo_pasta pai ON pai.id = p.pasta_pai_id "
                                      "WHERE aip.item_id = ? AND aip.mapa_id = ?");
                st.bind(1, Value::of(item));
                st.bind(2, Value::of(mapa));
                return st.step() ? st.columnText(1) + "/" + st.columnText(0) + " " + st.columnText(2) : std::string();
            };
            checar(pastaDe(solto).rfind("Auto/", 0) == 0 && pastaDe(oculto).rfind("Auto/", 0) == 0 &&
                       pastaDe(solto).find("@auto") != std::string::npos,
                   "imported files were distributed into Auto's AUTO subfolders (" + juce::String(pastaDe(solto)) + ")");
            checar(pa.desfazer() && pa.desfazer() && pa.itensSemPasta(mapa).size() == 3, "two Undo steps put the 2 files back in _no folder");
        }

        // ---- 1/2. janela `_no folder`: Hidden aparece, INTAKE é contado, abre com tudo selecionado
        {
            bool mudou = false;
            JanelaPastaFlutuante janela(pa, JanelaPastaFlutuante::kPastaSemPasta, JanelaPastaFlutuante::kNomeSemPasta, [] {},
                                        [&] { mudou = true; }, {}, true);
            auto* mosaico = acharFilho<MosaicoComponent>(janela);
            checar(mosaico != nullptr, "the _no folder window uses the app's grid (MosaicoComponent)");
            if (mosaico != nullptr) {
                checar(esperarAte([&] { return mosaico->itensSelecionados().size() == 2; }),
                       "opened from the NO FOLDER card it arrives with every shown file selected (2)");
                const auto sel = mosaico->itensSelecionados();
                checar(sel.count(solto) == 1 && sel.count(oculto) == 1, "the CONTENT = Hidden file is shown and selected");
            }
            const auto* rotulo = acharFilho<juce::Label>(janela, [](juce::Label& l) { return l.getText().contains("INTAKE"); });
            checar(rotulo != nullptr && rotulo->getText().contains("1 with CONTENT = Hidden") && rotulo->getText().contains("1 still in INTAKE"),
                   "the count line says where the rest are: " + (rotulo != nullptr ? rotulo->getText() : juce::String("(no label)")));
            janela.setVisible(false);
            juce::ignoreUnused(mudou);
        }

        // ---- 2. card NO FOLDER do Analytics
        {
            CartoesAnalyticsComponent cartoes(pa);
            cartoes.setBounds(0, 0, 1400, 100);
            cartoes.dados_ = CartoesAnalyticsComponent::calcular(pa, mapa, false, "Folder Map 1");
            checar(cartoes.dados_.semPasta == 3, "NO FOLDER card counts 3");
            int cliques = 0;
            cartoes.aoAbrirSemPasta = [&] { ++cliques; };
            cartoes.mouseDown(clique(cartoes, cartoes.retanguloCard(5, cartoes.getWidth()).getCentre()));
            checar(cliques == 1, "clicking NO FOLDER goes to STRUCTURE > _no folder (no DB read on the click)");
            checar(i18n::t("analytics.sem_pasta_onde").replace("{mapa}", "Folder Map 1").contains("_no folder"),
                   "the card subtitle says where the files are");
        }
    } catch (const std::exception& e) {
        checar(false, juce::String("_no folder selftest: ") + e.what());
    }
    bombear(200);
    raiz.deleteRecursively();

    std::cout << "\n" << (falhas == 0 ? juce::String("ALL TESTS PASSED") : juce::String(falhas) + " FAILURE(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

}  // namespace matriz::ui
