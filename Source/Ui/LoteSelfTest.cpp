#include "FloatingPreviewWindow.h"
#include "LoteSelfTest.h"

#include <JuceHeader.h>

#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <optional>
#include <iostream>

#include "../Model/Project.h"
#include "../App/Preferencias.h"
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
#include "ArvoreBackupComponent.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Audio/FormatoAudioQuickTime.h"
#include "../Model/CompactacaoRegistro.h"
#include "../Model/NotasEstruturadas.h"
#include "../Model/NomesCanonicos.h"
#include "../Model/ProjectLog.h"
#include "../Consolidacao/PacoteCollection.h"
#include "../Ingest/Checksum.h"
#include <exiv2/exiv2.hpp>
#include "../Ficha/AutocompleteHistorico.h"
#include "InitialRelinkDialog.h"
#include "DuplicatesWorkspaceComponent.h"
#include "LayoutArvoreFolderMap.h"
#include "ConflitosMergeDialog.h"
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

// ---------------------------------------------------------------------------
// Pacote de collection — Fase 1: tags, pessoas e subjects case-insensitive.
// ---------------------------------------------------------------------------
using Checar = std::function<void(bool, const juce::String&)>;

std::vector<std::string> tagsDoItem(matriz::db::Database& reg, const std::string& itemId) {
    std::vector<std::string> out;
    auto st = reg.prepare("SELECT tag FROM item_tag WHERE item_id = ? ORDER BY tag");
    st.bind(1, matriz::db::Value::of(itemId));
    while (st.step()) out.push_back(st.columnText(0));
    return out;
}

std::string colunaDoItem(matriz::db::Database& reg, const std::string& coluna, const std::string& itemId) {
    auto st = reg.prepare("SELECT COALESCE(" + coluna + ", '') FROM item WHERE id = ?");
    st.bind(1, matriz::db::Value::of(itemId));
    return st.step() ? st.columnText(0) : std::string();
}

juce::AlertWindow* alertaModalComTitulo(const juce::String& titulo) {
    auto* mcm = juce::ModalComponentManager::getInstance();
    for (int i = 0; i < mcm->getNumModalComponents(); ++i)
        if (auto* aw = dynamic_cast<juce::AlertWindow*>(mcm->getModalComponent(i)))
            if (aw->getName() == titulo) return aw;
    return nullptr;
}

void rodarTestesNomesCanonicos(const Checar& checar) {
    namespace nomes = matriz::model::nomes;
    using matriz::db::Value;
    std::cout << "\n-- Collection package, phase 1: case-insensitive tags/people/subjects --\n";

    checar(nomes::chave("  SHOW ") == "show", "the key ignores case and spaces at the ends");
    checar(nomes::chave("S\xc3\xa3o") != nomes::chave("Sao"), juce::String::fromUTF8("accents still count (\"S\xc3\xa3o\" != \"Sao\")"));
    checar(nomes::chave("\xc3\x89POCA") == nomes::chave("\xc3\xa9poca"), "upper/lower case of accented letters is the same name");
    {
        std::map<std::string, std::string> vocab = {{"show", "Show"}, {"tour", "Tour"}};
        auto canon = [&](const std::string& t) {
            auto it = vocab.find(nomes::chave(t));
            return it != vocab.end() ? it->second : t;
        };
        checar(nomes::canonizarListaSubjects("show; BACKSTAGE, TOUR", canon) == "Show; BACKSTAGE, Tour",
               "a subject list keeps its separators and takes the existing spellings");
        checar(nomes::canonizarListaSubjects("Show, show", canon) == "Show", "the same subject twice in one item becomes one");
        checar(nomes::canonizarListaSubjects("Show, Tour", canon) == "Show, Tour", "an already canonical list is left untouched");
    }

    // --- Gravação pela ficha/lote (ProjetoAberto) -----------------------------
    juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("matriz_nomes_selftest_" + juce::Uuid().toDashedString());
    try {
        matriz::model::NovoProjetoParams params;
        params.nome = "Nomes";
        params.prefixoNomenclatura = "NOM";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("A"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        const std::string i1 = inserirItem(reg, projetoId, "NOM-1", false);
        const std::string i2 = inserirItem(reg, projetoId, "NOM-2", false);

        pa.adicionarTag(i1, "Show");
        pa.adicionarTag(i2, " show ");
        checar(tagsDoItem(reg, i2) == std::vector<std::string>{"Show"}, "a new \" show \" tag takes the existing \"Show\"");
        pa.definirTags(i2, {"SHOW", "Tour", "tour"});
        checar(tagsDoItem(reg, i2) == (std::vector<std::string>{"Show", "Tour"}),
               "setting tags with variations keeps one entry per name");
        pa.adicionarTag(i1, "show");
        checar(tagsDoItem(reg, i1) == std::vector<std::string>{"Show"}, "adding \"show\" to an item with \"Show\" adds nothing");
        pa.removerTag(i1, "SHOW");
        checar(tagsDoItem(reg, i1).empty(), "removing \"SHOW\" removes the item's \"Show\"");
        pa.adicionarTag(i1, "\xc3\x89poca");
        pa.adicionarTag(i2, "\xc3\x89POCA");
        pa.adicionarTag(i2, "Epoca");
        auto t2 = tagsDoItem(reg, i2);
        checar(std::count(t2.begin(), t2.end(), std::string("\xc3\x89poca")) == 1 &&
                   std::count(t2.begin(), t2.end(), std::string("Epoca")) == 1 && t2.size() == 4,
               juce::String::fromUTF8("\"\xc3\x89POCA\" becomes \"\xc3\x89poca\"; \"Epoca\" (no accent) stays a different tag"));

        pa.adicionarPessoa("Jo\xc3\xa3o Silva");
        pa.adicionarPessoa("jo\xc3\xa3o silva");
        checar(pa.listarPessoas().size() == 1, juce::String::fromUTF8("the PEOPLE list gets one entry for \"Jo\xc3\xa3o Silva\"/\"jo\xc3\xa3o silva\""));
        pa.adicionarTag(i1, "JO\xc3\x83O SILVA");
        auto t1 = tagsDoItem(reg, i1);
        checar(std::find(t1.begin(), t1.end(), std::string("Jo\xc3\xa3o Silva")) != t1.end(),
               "a person added as a tag takes the PEOPLE list spelling");

        pa.salvarMetadado(i1, "dc_subject", "Show, Backstage");
        pa.salvarMetadado(i2, "dc_subject", "show; BACKSTAGE; Tour");
        checar(colunaDoItem(reg, "dc_subject", i2) == "Show; Backstage; Tour",
               "SUBJECT typed in another case takes the existing spellings");
        pa.salvarMetadadoEmLote({i1, i2}, {{"dc_subject", "SHOW"}});
        checar(colunaDoItem(reg, "dc_subject", i1) == "Show" && colunaDoItem(reg, "dc_subject", i2) == "Show",
               "batch SUBJECT \"SHOW\" is recorded as \"Show\"");
        checar(nomes::levantarUnificacao(reg).empty(), "data written through the app never needs unifying");
    } catch (const std::exception& e) {
        checar(false, juce::String("names selftest (writes): ") + e.what());
    }

    // --- Migração de projeto existente ------------------------------------------
    try {
        matriz::model::NovoProjetoParams params;
        params.nome = "Nomes antigos";
        params.prefixoNomenclatura = "OLD";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("B"), params);
        const std::string projetoId = projeto->projetoId();
        const juce::File pastaProjeto = projeto->pasta();
        std::string a, b, c;
        {
            auto& reg = projeto->registro();
            a = inserirItem(reg, projetoId, "OLD-A", false);
            b = inserirItem(reg, projetoId, "OLD-B", false);
            c = inserirItem(reg, projetoId, "OLD-C", false);
            // Banco de antes da Fase 1: variações gravadas direto.
            auto tag = [&](const std::string& item, const std::string& t) {
                reg.run("INSERT INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                        {Value::of(matriz::model::novoUuid()), Value::of(item), Value::of(t)});
            };
            tag(a, "Show");
            tag(b, "show");
            tag(c, " SHOW ");
            tag(c, "Show");
            tag(c, "Maria");
            reg.run("INSERT INTO collection_person (id, nome, criado_em) VALUES (?, 'maria', ?)",
                    {Value::of(matriz::model::novoUuid()), Value::of(matriz::model::agoraIso8601())});
            reg.run("UPDATE item SET dc_subject = 'Show, Tour' WHERE id = ?", {Value::of(a)});
            reg.run("UPDATE item SET dc_subject = 'show; tour' WHERE id = ?", {Value::of(b)});
            reg.run("INSERT INTO item_campo (id, item_id, campo_id, valor, fonte, atualizado_em) "
                    "VALUES (?, ?, 'dc_subject', 'show; tour', 'humano', ?)",
                    {Value::of(matriz::model::novoUuid()), Value::of(b), Value::of(matriz::model::agoraIso8601())});
        }

        auto grupos = nomes::levantarUnificacao(projeto->registro());
        bool achouShow = false;
        for (auto& g : grupos)
            if (g.tipo == nomes::GrupoUnificacao::Tipo::Tags && g.canonico == "Show" && g.variantes.size() == 2 &&
                g.itens == 2)
                achouShow = true;
        checar(achouShow, "the preview lists \"Show\" + \"show\" + \" SHOW \" -> \"Show\" (2 items)");

        MainComponent janela;
        janela.setBounds(0, 0, 1200, 800);
        janela.abrirProjeto(std::move(projeto));
        const juce::String titulo = matriz::i18n::t("nomes.unificar_titulo");
        juce::AlertWindow* dlg = nullptr;
        esperarAte([&] { return (dlg = alertaModalComTitulo(titulo)) != nullptr; }, 20000);
        checar(dlg != nullptr, "opening an older project asks before unifying names");
        auto* pa = janela.projetoAberto();
        if (dlg && pa) {
            auto& reg = pa->projeto().registro();
            checar(tagsDoItem(reg, b) == std::vector<std::string>{"show"}, "nothing changes before the operator confirms");
            dlg->exitModalState(1);  // Unify
            // A caixa de resultado tem o mesmo título, mas sem a lista.
            juce::AlertWindow* fim = nullptr;
            esperarAte([&] {
                fim = alertaModalComTitulo(titulo);
                return fim != nullptr && fim->getTextEditor("lista") == nullptr;
            }, 30000);
            if (fim) fim->exitModalState(0);
            bombear(100);

            checar(tagsDoItem(reg, b) == std::vector<std::string>{"Show"}, "after unifying, \"show\" became \"Show\"");
            checar(tagsDoItem(reg, c) == (std::vector<std::string>{"Show", "maria"}),
                   "an item that had two variations keeps a single entry");
            checar(colunaDoItem(reg, "dc_subject", b) == "Show; Tour", "subjects were unified in item.dc_subject");
            {
                auto st = reg.prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = 'dc_subject'");
                st.bind(1, Value::of(b));
                checar(st.step() && st.columnText(0) == "Show; Tour", "...and in the item_campo mirror");
            }
            {
                auto st = reg.prepare("SELECT COUNT(*) FROM busca_fts_map WHERE item_id = ? AND conteudo = 'show'");
                st.bind(1, Value::of(b));
                checar(st.step() && st.columnInt(0) == 0, "search index no longer has the old spelling");
            }
            checar(pa->listarPessoas() == std::vector<std::string>{"maria"}, "the PEOPLE list keeps one \"maria\"");
            bool temBackup = false;
            for (auto& f : pastaProjeto.findChildFiles(juce::File::findFiles, false, "registro.antes-unificacao-*.sqlite"))
                temBackup = temBackup || f.getSize() > 0;
            checar(temBackup, "a backup of the registry was saved in the project folder first");
            checar(matriz::model::ProjectLog(pastaProjeto).readContent().contains("Names Unified"),
                   "the unification is in the project log");
            checar(nomes::levantarUnificacao(reg).empty(), "nothing left to unify afterwards");
        }
    } catch (const std::exception& e) {
        checar(false, juce::String("names selftest (migration): ") + e.what());
    }
    raiz.deleteRecursively();
}

// ---------------------------------------------------------------------------
// Pacote de collection — Fase 2 (EXPORT) e Fase 3 (INTAKE).
// ---------------------------------------------------------------------------
void escreverWavTeste(const juce::File& destino, int semente) {
    juce::MemoryOutputStream out;
    const int amostras = 800, taxa = 8000;
    out.write("RIFF", 4);
    out.writeInt(36 + amostras * 2);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    out.writeInt(16);
    out.writeShort(1);
    out.writeShort(1);
    out.writeInt(taxa);
    out.writeInt(taxa * 2);
    out.writeShort(2);
    out.writeShort(16);
    out.write("data", 4);
    out.writeInt(amostras * 2);
    for (int i = 0; i < amostras; ++i) out.writeShort(static_cast<short>((i * (semente + 7)) % 4000 - 2000));
    destino.getParentDirectory().createDirectory();
    destino.replaceWithData(out.getData(), out.getDataSize());
}

// Item de B com o arquivo na SOURCE (caminho absoluto), como a ingestão grava.
std::string inserirItemComArquivo(matriz::db::Database& reg, const std::string& projetoId, const std::string& codigo,
                                  const juce::File& arquivo) {
    const std::string id = matriz::model::novoUuid(), agora = matriz::model::agoraIso8601();
    reg.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
            "VALUES (?, ?, ?, ?, 'digital_audio', 'novo', ?, ?)",
            {matriz::db::Value::of(id), matriz::db::Value::of(projetoId), matriz::db::Value::of(codigo),
             matriz::db::Value::of(arquivo.getFileNameWithoutExtension().toStdString()), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
    reg.run("INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, "
            "checksum_sha256, criado_em, atualizado_em) VALUES (?, ?, ?, ?, 'preservation_master', 1, ?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id),
             matriz::db::Value::of(arquivo.getFileName().toStdString()),
             matriz::db::Value::of(arquivo.getFullPathName().toStdString()),
             matriz::db::Value::of(static_cast<long long>(arquivo.getSize())),
             matriz::db::Value::of(matriz::ingest::calcularChecksums(arquivo).sha256), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
    return id;
}

// Retrato textual do registro de B (tudo que o export não pode mudar).
std::string retratoDoRegistro(matriz::db::Database& reg) {
    std::string s;
    for (const char* sql : {"SELECT id || COALESCE(atualizado_em,'') || COALESCE(dc_title,'') || COALESCE(dc_subject,'') "
                            "|| COALESCE(ano,'') || estado FROM item ORDER BY id",
                            "SELECT item_id || tag FROM item_tag ORDER BY 1", "SELECT COUNT(*) FROM item_campo",
                            "SELECT COUNT(*) FROM item_observacao", "SELECT COUNT(*) FROM asset_geolocation",
                            "SELECT COUNT(*) FROM folder_map", "SELECT COUNT(*) FROM acervo_pasta",
                            "SELECT COUNT(*) FROM acervo_item_pasta", "SELECT COUNT(*) FROM consolidacao_registro",
                            "SELECT COUNT(*) FROM collection_person"}) {
        auto st = reg.prepare(sql);
        while (st.step()) s += st.columnText(0) + "\n";
    }
    return s;
}

void rodarTestesPacote(const Checar& checar) {
    namespace pk = matriz::consolidacao::pacote;
    using matriz::db::Value;
    std::cout << "\n-- Collection package, phase 2: EXPORT -> package --\n";
    juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("matriz_pacote_selftest_" + juce::Uuid().toDashedString());
    juce::File pacote;
    std::vector<juce::File> fontes;
    try {
        juce::File source = raiz.getChildFile("SOURCE_B");
        for (int i = 0; i < 5; ++i) {
            fontes.push_back(source.getChildFile("faixa" + juce::String(i + 1) + ".wav"));
            escreverWavTeste(fontes.back(), 11 * (i + 1));
        }
        matriz::model::NovoProjetoParams params;
        params.nome = "Collection B";
        params.prefixoNomenclatura = "CLB";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("B"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pb(std::move(projeto));
        auto& reg = pb.projeto().registro();
        std::vector<std::string> ids;
        for (int i = 0; i < 5; ++i) ids.push_back(inserirItemComArquivo(reg, projetoId, "CLB-" + std::to_string(i + 1), fontes[i]));

        // Folder map: Shows/2015 (itens 1 e 2), Shows (item 3), Vazia (sem itens);
        // item 4 fica fora do MAIN, item 5 sem pasta.
        const std::string mapa = pb.criarFolderMap("Mapa B", std::nullopt);
        const std::string shows = pb.criarPastaAcervo("Shows", std::nullopt, mapa);
        const std::string ano2015 = pb.criarPastaAcervo("2015", shows, mapa);
        pb.criarPastaAcervo("Vazia", std::nullopt, mapa);
        pb.adicionarItensAPasta({ids[0], ids[1]}, ano2015);
        pb.adicionarItensAPasta({ids[2], ids[3]}, shows);

        // Dados de ficha do item 1 (todos os campos do Passo 0) e 2 (parcial).
        pb.adicionarPessoa("Maria");
        pb.salvarMetadado(ids[0], "dc_title", "Show no Rio");
        pb.salvarMetadado(ids[0], "dc_description", "Primeira noite");
        pb.salvarMetadado(ids[0], "ano", "12/03/2015");
        pb.salvarMetadado(ids[0], "collection_type", "Concert");
        pb.salvarMetadado(ids[0], "dc_subject", "Show, Backstage");
        pb.definirTags(ids[0], {"Show", "Maria"});
        pb.adicionarObservacao(ids[0], "entrada da voz", 1500, "selftest");
        pb.adicionarObservacao(ids[0], "nota sem tempo", std::nullopt, "selftest");
        reg.run("INSERT INTO asset_geolocation (asset_id, latitude, longitude, city, country, source) "
                "VALUES (?, -22.9, -43.2, 'Rio de Janeiro', 'Brasil', 'USER_COORDINATES')",
                {Value::of(ids[0])});
        pb.salvarMetadado(ids[1], "dc_subject", "show");

        // MAIN: primeiro backup (itens 1, 2, 3; o 4 fica só no SOURCE).
        const juce::File media = pb.projeto().pastaMedia();
        auto plano = matriz::consolidacao::planejarConsolidacao(reg, pb.projeto().pasta(), media,
                                                                {matriz::consolidacao::NivelHierarquia::PastaManual}, {},
                                                                matriz::consolidacao::ModoPrefixoArquivo::Nenhum, {}, true,
                                                                false, false, false, mapa);
        std::vector<matriz::consolidacao::ItemPlanejado> noMain;
        for (auto& ip : plano.itens)
            if (ip.itemId == ids[0] || ip.itemId == ids[1] || ip.itemId == ids[2]) noMain.push_back(ip);
        plano.itens = noMain;
        auto res = matriz::consolidacao::executarConsolidacao(reg, pb.projeto().pasta(), media, plano);
        checar(res.consolidados == 3, "setup: 3 files in B's MAIN (" + juce::String(res.consolidados) + ")");

        const std::string antes = retratoDoRegistro(reg);
        const juce::File destino = raiz.getChildFile("saida");
        destino.createDirectory();
        destino.getChildFile("Pacote Teste").createDirectory();  // nome já existe: sufixo numérico
        int ultimoTotal = 0;
        auto r = pk::gerarPacote(reg, pb.projeto().pasta(), destino, "Pacote Teste", mapa, "Mapa B", "Collection B",
                                 {ids.begin(), ids.end()}, [&](int, int total) { ultimoTotal = total; return true; });
        pacote = r.pasta;
        checar(r.copiados == 3 && r.falhas.empty(), "3 files went into the package (" + juce::String(r.copiados) + ")");
        checar(r.foraDoMain == 1, "the file only in SOURCE stays out (nothing leaves before the MAIN)");
        checar(r.semPasta == 1, "the item without a folder in the chosen map stays out and is counted");
        checar(pacote.getFileName() == "Pacote Teste (2)", "an existing folder name gets a numeric suffix");
        checar(retratoDoRegistro(reg) == antes, "project B is not changed by the export");
        checar(matriz::model::ProjectLog(pb.projeto().pasta()).readContent().contains("Collection Package Exported"),
               "the export is in B's project log");

        auto leitura = pk::lerPacote(pacote);
        checar(leitura.status == pk::StatusLeitura::Ok && leitura.pacote.arquivos.size() == 3,
               "matriz-pacote.json is valid (formato/versao) with one record per file");
        checar(pacote.getChildFile("Media/Shows/2015/faixa1.wav").existsAsFile() &&
                   pacote.getChildFile("Media/Shows/faixa3.wav").existsAsFile(),
               "Media/ follows the chosen folder map");
        bool shaConfere = true;
        for (const auto& a : leitura.pacote.arquivos)
            shaConfere = shaConfere && juce::String(matriz::ingest::calcularChecksums(
                                                        pacote.getChildFile("Media").getChildFile(a.caminho)).sha256)
                                               .toLowerCase()
                                               .toStdString() == a.sha256;
        checar(shaConfere, "each record's SHA-256 is the delivered file's");
        const pk::RegistroArquivo* r1 = nullptr;
        const pk::RegistroArquivo* r3 = nullptr;
        for (const auto& a : leitura.pacote.arquivos) {
            if (a.caminho == "Shows/2015/faixa1.wav") r1 = &a;
            if (a.caminho == "Shows/faixa3.wav") r3 = &a;
        }
        checar(r1 && r1->dados.titulo == "Show no Rio" && r1->dados.descricao == "Primeira noite" &&
                   r1->dados.eventDate == "12/03/2015" && r1->dados.content == "Concert" &&
                   r1->dados.subjects == std::vector<std::string>{"Show", "Backstage"} &&
                   r1->dados.tags == std::vector<std::string>{"Show"} &&
                   r1->dados.pessoas == std::vector<std::string>{"Maria"},
               "record 1 carries title, description, event date, content, subjects, tags and people");
        checar(r1 && r1->dados.geo.getProperty("city", {}).toString() == "Rio de Janeiro" &&
                   std::abs(static_cast<double>(r1->dados.geo.getProperty("latitude", 0.0)) + 22.9) < 1e-6,
               "record 1 carries GEO LOCATION");
        checar(r1 && r1->dados.marcadores.size() == 2 && r1->dados.marcadores[0].tempoS &&
                   std::abs(*r1->dados.marcadores[0].tempoS - 1.5) < 1e-9 && !r1->dados.marcadores[1].tempoS,
               "markers travel with times in seconds");
        checar(r1 && r1->pasta == std::vector<juce::String>{"Shows", "2015"}, "record 1 knows its folder in the map");
        checar(r3 && r3->dados.vazio(), "empty fields are omitted");
        {
            const auto json = juce::JSON::parse(pacote.getChildFile(pk::kArquivoJson));
            const auto* pastas = json.getProperty("pastas", {}).getArray();
            checar(pastas && pastas->size() == 1 && (*pastas)[0].getProperty("nome", {}).toString() == "Shows",
                   "the folder tree has only the folders with package items (\"Vazia\" is not there)");
        }

        // Cancelar no meio: a pasta do pacote não fica.
        auto rc = pk::gerarPacote(reg, pb.projeto().pasta(), destino, "Cancelado", mapa, "Mapa B", "Collection B",
                                  {ids.begin(), ids.end()}, [](int feito, int) { return feito < 1; });
        checar(rc.cancelado && !rc.pasta.exists(), "cancelling removes the half-made package folder");

        // Versão desconhecida / formato errado: recusa com motivo.
        juce::File v2 = raiz.getChildFile("v2");
        v2.getChildFile("Media").createDirectory();
        v2.getChildFile(pk::kArquivoJson).replaceWithText("{\"formato\":\"matriz-pacote\",\"versao\":2,\"arquivos\":[]}");
        checar(pk::lerPacote(v2).status == pk::StatusLeitura::VersaoDesconhecida, "an unknown package version is refused");
        v2.getChildFile(pk::kArquivoJson).replaceWithText("{\"formato\":\"outro\",\"versao\":1}");
        checar(pk::lerPacote(v2).status == pk::StatusLeitura::Invalido, "a JSON that is not a package is refused");
        checar(pk::lerPacote(raiz.getChildFile("SOURCE_B")).status == pk::StatusLeitura::NaoEPacote,
               "a normal folder is not a package");
    } catch (const std::exception& e) {
        checar(false, juce::String("package selftest (export): ") + e.what());
    }
    std::cout << "\n-- Collection package, phase 3: INTAKE of a package --\n";
    try {
        if (!pacote.isDirectory()) throw std::runtime_error("no package from phase 2");
        // Um arquivo em Media/ sem registro e um registro sem arquivo.
        escreverWavTeste(pacote.getChildFile("Media/extra.wav"), 999);
        {
            auto json = juce::JSON::parse(pacote.getChildFile(pk::kArquivoJson));
            auto* orfao = new juce::DynamicObject();
            orfao->setProperty("sha256", "00deadbeef");
            orfao->setProperty("caminho", "sumiu.wav");
            orfao->setProperty("titulo", "nunca chega");
            json.getProperty("arquivos", {}).getArray()->add(juce::var(orfao));
            pacote.getChildFile(pk::kArquivoJson).replaceWithText(juce::JSON::toString(json));
        }
        matriz::model::NovoProjetoParams params;
        params.nome = "Collection A";
        params.prefixoNomenclatura = "CLA";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("A"), params);
        const std::string projetoId = projeto->projetoId();
        std::string existente;
        {
            auto& reg = projeto->registro();
            // A já tem o mesmo conteúdo da faixa2 (outra cópia) e suas grafias.
            juce::File copiaA = raiz.getChildFile("SOURCE_A/faixa2-copia.wav");
            copiaA.getParentDirectory().createDirectory();
            fontes[1].copyFileTo(copiaA);
            existente = inserirItemComArquivo(reg, projetoId, "CLA-00001", copiaA);
            reg.run("INSERT INTO item_tag (id, item_id, tag) VALUES (?, ?, 'show')",
                    {Value::of(matriz::model::novoUuid()), Value::of(existente)});
            reg.run("UPDATE item SET dc_subject = 'SHOW' WHERE id = ?", {Value::of(existente)});
            reg.run("INSERT INTO folder_map (id, projeto_id, nome, ordem, criado_em, atualizado_em) "
                    "VALUES (?, ?, 'Pacote Teste (2)', 9, ?, ?)",
                    {Value::of(matriz::model::novoUuid()), Value::of(projetoId), Value::of(matriz::model::agoraIso8601()),
                     Value::of(matriz::model::agoraIso8601())});
        }

        MainComponent janela;
        janela.setBounds(0, 0, 1200, 800);
        std::optional<ProjetoAberto::ResultadoIntakePacote> resultado;
        janela.aoAplicarPacoteParaTeste = [&](const ProjetoAberto::ResultadoIntakePacote& r) { resultado = r; };
        janela.aoConcluirLoteIngestParaTeste = [](int, const juce::StringArray&) {};
        janela.abrirProjeto(std::move(projeto));
        bombear(300);
        auto* pa = janela.projetoAberto();
        const auto mapasAntes = pa->listarFolderMaps().size();
        janela.ingerirArquivos({pacote});
        esperarAte([&] { return resultado.has_value(); }, 120000);
        checar(resultado.has_value() && resultado->ok, "the package went through INTAKE and its catalog was applied");
        juce::AlertWindow* relatorio = nullptr;
        esperarAte([&] { return (relatorio = alertaModalComTitulo(matriz::i18n::t("intake.pacote_titulo"))) != nullptr; }, 5000);
        checar(relatorio != nullptr, "a final report is shown");
        if (relatorio) relatorio->exitModalState(0);
        bombear(100);
        if (resultado) {
            checar(resultado->entraram == 4, "report: 4 files in (" + juce::String(resultado->entraram) + ")");
            checar(resultado->comDados == 2, "report: 2 with catalog data (" + juce::String(resultado->comDados) + ")");
            checar(resultado->semCorrespondencia == 1, "report: 1 file without a catalog record (extra.wav)");
            checar(resultado->registrosSemArquivo == 1, "report: 1 catalog record without a file");
            checar(resultado->jaExistiam == 1, "report: 1 file was already in A (a normal duplicate now)");
            checar(resultado->folderMap == "Pacote Teste (2) (2)", "a new folder map named after the package, with a suffix");
        }
        auto& reg = pa->projeto().registro();
        auto itemDoArquivo = [&](const juce::String& fim) {
            auto st = reg.prepare("SELECT a.item_id FROM arquivo a WHERE a.caminho_absoluto_origem LIKE ?");
            st.bind(1, Value::of(("%" + fim).toStdString()));
            return st.step() ? st.columnText(0) : std::string();
        };
        const std::string novo1 = itemDoArquivo("/Media/Shows/2015/faixa1.wav");
        const std::string novo2 = itemDoArquivo("/Media/Shows/2015/faixa2.wav");
        checar(!novo1.empty() && !novo2.empty(), "the package files are items in A");
        checar(colunaDoItem(reg, "dc_title", novo1) == "Show no Rio" &&
                   colunaDoItem(reg, "dc_description", novo1) == "Primeira noite" &&
                   colunaDoItem(reg, "ano", novo1) == "12/03/2015" && colunaDoItem(reg, "collection_type", novo1) == "Concert",
               "title, description, EVENT DATE and CONTENT arrive in the record");
        checar(colunaDoItem(reg, "dc_subject", novo1) == "SHOW, Backstage",
               "SUBJECT arrives unified with A's spelling (\"Show\" -> \"SHOW\")");
        checar(tagsDoItem(reg, novo1) == (std::vector<std::string>{"Maria", "show"}),
               "TAGS/PEOPLE arrive unified with A's spelling (\"Show\" -> \"show\")");
        {
            auto pessoas = pa->listarPessoas();
            checar(std::find(pessoas.begin(), pessoas.end(), std::string("Maria")) != pessoas.end(),
                   "people also enter A's PEOPLE list");
        }
        {
            auto geo = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(reg, novo1);
            checar(geo && geo->city == "Rio de Janeiro" && geo->latitude && std::abs(*geo->latitude + 22.9) < 1e-6,
                   "GEO LOCATION arrives");
        }
        {
            auto obs = pa->observacoesDoItem(novo1);
            checar(obs.size() == 2 && obs[0].minutagemMs && *obs[0].minutagemMs == 1500 && obs[0].texto == "entrada da voz",
                   "markers arrive at the same time");
        }
        {
            std::string mapaNovo;
            for (auto& m : pa->listarFolderMaps())
                if (m.nome == juce::String("Pacote Teste (2) (2)")) mapaNovo = m.id;
            checar(pa->listarFolderMaps().size() == mapasAntes + 1, "only one folder map was added; the others are untouched");
            auto arvore = pa->arvoreAcervo(mapaNovo);
            const ProjetoAberto::NoArvore* no2015 = nullptr;
            for (auto& f : arvore.filhos)
                if (f.nome == juce::String("Shows"))
                    for (auto& g : f.filhos)
                        if (g.nome == juce::String("2015")) no2015 = &g;
            checar(no2015 && no2015->itemIdsDiretos.count(novo1) && no2015->itemIdsDiretos.count(novo2),
                   "the new folder map reproduces Shows/2015 with each item in its folder");
        }
        checar(tagsDoItem(reg, existente) == std::vector<std::string>{"show"} &&
                   colunaDoItem(reg, "dc_title", existente).empty(),
               "the item already in A is not touched (nothing is merged at INTAKE)");
    } catch (const std::exception& e) {
        checar(false, juce::String("package selftest (intake): ") + e.what());
    }
    raiz.deleteRecursively();
}

// ---------------------------------------------------------------------------
// Pacote de collection — Fase 4: resolver duplicata junta as fichas.
// ---------------------------------------------------------------------------
void rodarTestesMerge(const Checar& checar) {
    namespace mg = matriz::model::merge;
    using matriz::db::Value;
    std::cout << "\n-- Collection package, phase 4: resolving a duplicate merges the records --\n";
    checar(mg::datasCompativeis("2015", "12/03/2015") && mg::datasCompativeis("03/2015", "12/03/2015") &&
               mg::datasCompativeis("2015-03-12", "12/03/2015") && !mg::datasCompativeis("2015", "2016") &&
               !mg::datasCompativeis("04/2015", "12/03/2015"),
           "compatible dates: \"2015\" x \"12/03/2015\", \"03/2015\" x \"12/03/2015\"; different years/months are not");
    {
        std::string maisPrecisa;
        mg::datasCompativeis("2015", "2015-03-12", &maisPrecisa);
        checar(maisPrecisa == "2015-03-12", "the more precise of two compatible dates is kept");
    }

    juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("matriz_merge_selftest_" + juce::Uuid().toDashedString());
    try {
        matriz::model::NovoProjetoParams params;
        params.nome = "Merge";
        params.prefixoNomenclatura = "MRG";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("A"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        const std::string a = inserirItem(reg, projetoId, "MRG-1", false);  // mantido
        const std::string b = inserirItem(reg, projetoId, "MRG-2", false);  // descartado
        pa.definirTags(a, {"Show"});
        pa.salvarMetadado(a, "ano", "2015");
        pa.salvarMetadado(a, "dc_title", "Titulo A");
        pa.salvarMetadado(a, "dc_subject", "Show");
        pa.adicionarObservacao(a, "entrada", 1000, "t");
        reg.run("INSERT INTO item_tag (id, item_id, tag) VALUES (?, ?, 'show')",
                {Value::of(matriz::model::novoUuid()), Value::of(b)});
        pa.adicionarTag(b, "Tour");
        pa.salvarMetadado(b, "ano", "12/03/2015");
        pa.salvarMetadado(b, "dc_title", "Titulo B");
        pa.salvarMetadado(b, "collection_type", "Concert");
        pa.salvarMetadado(b, "dc_subject", "Backstage");
        pa.adicionarObservacao(b, "entrada", 1000, "t");
        pa.adicionarObservacao(b, "saida", 2000, "t");
        reg.run("INSERT INTO asset_geolocation (asset_id, latitude, longitude, city, source) "
                "VALUES (?, -22.9, -43.2, 'Rio de Janeiro', 'USER_COORDINATES')",
                {Value::of(b)});

        std::optional<std::vector<mg::Conflito>> simulados;
        pa.simularJuncaoEmSegundoPlano(a, b, [&](std::vector<mg::Conflito> c) { simulados = std::move(c); });
        esperarAte([&] { return simulados.has_value(); }, 20000);
        checar(simulados && simulados->size() == 1 && (*simulados)[0].campo == "dc_title" &&
                   colunaDoItem(reg, "ano", a) == "2015",
               "preview (nothing written): only the title is a real conflict; the event date is compatible");

        auto resolver = [&](std::set<std::string> usarDescartado) {
            std::optional<bool> ok;
            pa.resolverDuplicatasEmSegundoPlano(
                {a, b},
                [a, b, usarDescartado](matriz::db::Database& db) {
                    return std::vector<ProjetoAberto::ResultadoSanitizacao>{
                        ProjetoAberto::sanitizarDuplicata(db, a, b, usarDescartado)};
                },
                [&](bool r, std::vector<ProjetoAberto::ResultadoSanitizacao>) { ok = r; });
            esperarAte([&] { return ok.has_value(); }, 20000);
            return ok.value_or(false);
        };
        checar(resolver({}), "KEEP FILE 1 runs in the background, in one transaction");
        checar(tagsDoItem(reg, a) == (std::vector<std::string>{"Show", "Tour"}),
               "lists add up: tags \"Show\" + \"show\"/\"Tour\" -> Show, Tour");
        checar(colunaDoItem(reg, "dc_subject", a) == "Show, Backstage", "subjects add up");
        checar(pa.observacoesDoItem(a).size() == 2, "markers add up; the identical one (same time, same text) is not duplicated");
        checar(colunaDoItem(reg, "collection_type", a) == "Concert", "empty CONTENT on the kept item is filled");
        {
            auto geo = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(reg, a);
            checar(geo && geo->city == "Rio de Janeiro", "empty GEO LOCATION on the kept item is filled");
        }
        checar(colunaDoItem(reg, "ano", a) == "12/03/2015", "compatible dates: the more precise one stays");
        checar(colunaDoItem(reg, "dc_title", a) == "Titulo A", "real conflict: the kept item's value stays");
        auto pend = pa.conflitosMergePendentes(a);
        checar(pend.size() == 1 && pend[0].campo == "dc_title" && pend[0].valorAlternativo == "Titulo B" &&
                   pend[0].origem == "duplicate resolved",
               "the losing value is in the item history, with its origin");
        checar(pa.itensDaColecaoEmbutida("merge_conflitos").count(a) == 1, "the kept item shows up in \"Merge conflicts\"");
        checar(colunaDoItem(reg, "estado", b) == "duplicata" && juce::String(colunaDoItem(reg, "notas_livres", a)).contains("merge conflict"),
               "the discarded side is handled as before (state duplicata) and the notes get one short line");

        checar(pa.desfazer(), "Undo is available");
        checar(tagsDoItem(reg, a) == std::vector<std::string>{"Show"} && colunaDoItem(reg, "ano", a) == "2015" &&
                   colunaDoItem(reg, "collection_type", a).empty() && pa.observacoesDoItem(a).size() == 1 &&
                   !matriz::analytics::AssetGeolocationRepository::obterPorAssetId(reg, a) &&
                   colunaDoItem(reg, "estado", b) == "capturado" && pa.conflitosMergePendentes(a).empty(),
               "Undo puts both records back exactly as they were");

        checar(resolver({"dc_title"}) && colunaDoItem(reg, "dc_title", a) == "Titulo B",
               "the operator can pick the other value in the conflict screen");
        pend = pa.conflitosMergePendentes(a);
        checar(pend.size() == 1 && pend[0].valorAlternativo == "Titulo A", "...and then the kept item's old value is in the history");
        pa.revisarConflitosMerge(a, {pend[0].historicoId});
        esperarAte([&] { return pa.conflitosMergePendentes(a).empty(); }, 20000);
        checar(colunaDoItem(reg, "dc_title", a) == "Titulo A" && pa.itensDaColecaoEmbutida("merge_conflitos").count(a) == 0,
               "review: the value is recoverable from the history, and the item leaves \"Merge conflicts\"");

        // Origem "pacote" no histórico.
        const std::string c = inserirItem(reg, projetoId, "MRG-3", false);
        const std::string d = inserirItem(reg, projetoId, "MRG-4", false);
        pa.salvarMetadado(c, "dc_description", "daqui");
        pa.salvarMetadado(d, "dc_description", "do pacote");
        reg.run("INSERT INTO item_campo (id, item_id, campo_id, valor, fonte, atualizado_em) "
                "VALUES (?, ?, 'pacote_origem', 'Pacote X', 'leitura_tecnica', ?)",
                {Value::of(matriz::model::novoUuid()), Value::of(d), Value::of(matriz::model::agoraIso8601())});
        std::optional<bool> ok;
        pa.resolverDuplicatasEmSegundoPlano(
            {c, d}, [c, d](matriz::db::Database& db) {
                return std::vector<ProjetoAberto::ResultadoSanitizacao>{ProjetoAberto::sanitizarDuplicata(db, c, d)};
            },
            [&](bool r, std::vector<ProjetoAberto::ResultadoSanitizacao>) { ok = r; });
        esperarAte([&] { return ok.has_value(); }, 20000);
        pend = pa.conflitosMergePendentes(c);
        checar(pend.size() == 1 && pend[0].origem == "package Pacote X", "a value from a package names the package as origin");
    } catch (const std::exception& e) {
        checar(false, juce::String("merge selftest: ") + e.what());
    }
    raiz.deleteRecursively();

    // Tela de conflitos lado a lado: o do mantido vem marcado; trocar devolve a chave.
    {
        std::optional<std::set<std::string>> trocadas;
        ConflitosMergeDialog::mostrar("t", "i", "kept", "other",
                                      {{"k1", "dc_title", "A", "B"}, {"k2", "ano", "2015", "2016"}},
                                      [&](bool confirmado, std::set<std::string> t) { if (confirmado) trocadas = t; });
        ConflitosMergeDialog* dlg = nullptr;
        esperarAte([&] {
            auto* mcm = juce::ModalComponentManager::getInstance();
            for (int i = 0; i < mcm->getNumModalComponents(); ++i)
                if (auto* dw = dynamic_cast<juce::DialogWindow*>(mcm->getModalComponent(i)))
                    if ((dlg = dynamic_cast<ConflitosMergeDialog*>(dw->getContentComponent())) != nullptr) return true;
            return false;
        }, 5000);
        checar(dlg != nullptr, "the conflict screen opens");
        if (dlg) {
            dlg->escolherOutroParaTeste(1);
            dlg->confirmarParaTeste();
        }
        bombear(100);
        checar(trocadas && *trocadas == std::set<std::string>{"k2"}, "only the rows switched to the other value come back");
    }
}

// ---------------------------------------------------------------------------
// Lote de ajustes (8 itens): Unknown do METADATA, layout do Folder Map e auto-organização.
// ---------------------------------------------------------------------------
void definirCampoRaiz(matriz::db::Database& reg, const std::string& itemId, const std::string& campo, const std::string& valor) {
    reg.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
            "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(campo),
             matriz::db::Value::of(valor), matriz::db::Value::of(matriz::model::agoraIso8601())});
    // EVENT DATE é a coluna item.ano (é o que a ficha mostra e o que o METADATA/auto-organização leem).
    if (campo == "ano") reg.run("UPDATE item SET ano = ? WHERE id = ?", {matriz::db::Value::of(valor), matriz::db::Value::of(itemId)});
}

// Etapa 3: segunda conexão somente-leitura (Project::registroLeitura) e cache de statements.
void rodarTestesConexaoLeitura(const Checar& checar) {
    using matriz::db::Value;
    std::cout << "\n-- Read-only connection + prepared statement cache --\n";
    juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("matriz_leitura_selftest_" + juce::Uuid().toDashedString());
    try {
        matriz::model::NovoProjetoParams params;
        params.nome = "Leitura";
        params.prefixoNomenclatura = "LEI";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        const auto a = inserirItem(reg, projetoId, "LEI-1", false);

        std::string titulo, tipo, codigo;
        checar(&pa.projeto().registroLeitura() != &reg && &pa.projeto().indiceLeitura() != &pa.projeto().indice(),
               "reads get a second connection (registro and indice), separate from the write one");
        checar(pa.obterItemInfo(a, titulo, tipo, codigo) && codigo == "LEI-1",
               "a committed write is visible at once on the read connection");

        // Transação aberta por ESTA thread: ela lê pela conexão de escrita (vê o que acabou de escrever).
        reg.run("BEGIN", {});
        const auto b = inserirItem(reg, projetoId, "LEI-2", false);
        checar(&pa.projeto().registroLeitura() == &reg, "inside this thread's open transaction, reads use the write connection");
        codigo.clear();
        checar(pa.obterItemInfo(b, titulo, tipo, codigo) && codigo == "LEI-2",
               "... and see its own uncommitted write");

        // OUTRA thread lendo com a transação ainda aberta: não espera o COMMIT e vê só o que já está commitado.
        auto outra = std::async(std::launch::async, [&] {
            auto st = pa.projeto().registroLeitura().prepare("SELECT COUNT(*) FROM item WHERE id = ?");
            st.bind(1, Value::of(b));
            return st.step() ? static_cast<int>(st.columnInt(0)) : -1;
        });
        const bool naoBloqueou = outra.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        reg.run("COMMIT", {});
        const int viu = outra.get();
        checar(naoBloqueou, "another thread's read is not blocked behind the open transaction");
        checar(viu == 0, "... and sees the last committed state, not the open transaction (" + juce::String(viu) + ")");
        codigo.clear();
        checar(pa.obterItemInfo(b, titulo, tipo, codigo) && codigo == "LEI-2", "after COMMIT the read connection sees it");

        // Cache de statements: duas cópias do mesmo SQL vivas ao mesmo tempo são independentes,
        // e o statement devolvido ao cache volta sem bindings.
        {
            auto s1 = reg.prepare("SELECT codigo_acervo FROM item WHERE id = ?");
            s1.bind(1, Value::of(a));
            auto s2 = reg.prepare("SELECT codigo_acervo FROM item WHERE id = ?");
            s2.bind(1, Value::of(b));
            const bool r1 = s1.step(), r2 = s2.step();
            checar(r1 && r2 && s1.columnText(0) == "LEI-1" && s2.columnText(0) == "LEI-2",
                   "two live statements for the same SQL do not share state");
        }
        {
            auto s3 = reg.prepare("SELECT codigo_acervo FROM item WHERE id = ?");
            checar(!s3.step(), "a statement returned to the cache comes back with its bindings cleared");
            s3.reset();
            s3.bind(1, Value::of(b));
            checar(s3.step() && s3.columnText(0) == "LEI-2", "... and works again after binding");
        }
        for (int i = 0; i < 50; ++i) {
            auto st = reg.prepare("SELECT COUNT(*) FROM item WHERE projeto_id = ?");
            st.bind(1, Value::of(projetoId));
            if (!st.step() || st.columnInt(0) != 2) { checar(false, "repeated cached SELECT returns the same result"); break; }
        }

        // Um snapshot mantido aberto por OUTRA thread (statement ativo na conexão DELA) não pode
        // fazer esta thread ler o estado antigo depois de um COMMIT: uma conexão por thread.
        {
            std::promise<void> segurando, soltar;
            auto prontoSeg = segurando.get_future();
            auto fimSeg = soltar.get_future().share();
            auto outraThread = std::async(std::launch::async, [&] {
                auto s = pa.projeto().registroLeitura().prepare("SELECT id FROM item");
                s.step();  // statement ativo = snapshot aberto nesta conexão
                segurando.set_value();
                fimSeg.wait();
            });
            prontoSeg.wait();
            const auto c = inserirItem(reg, projetoId, "LEI-3", false);
            codigo.clear();
            checar(pa.obterItemInfo(c, titulo, tipo, codigo) && codigo == "LEI-3",
                   "a snapshot held open by another thread does not make this thread read stale data");
            soltar.set_value();
            outraThread.get();
        }
    } catch (const std::exception& e) {
        checar(false, juce::String("read connection selftest: ") + e.what());
    }
    raiz.deleteRecursively();
}

void rodarTestesLoteAjustes(const Checar& checar) {
    using matriz::db::Value;
    std::cout << "\n-- Folder Map: tree layout (item H) --\n";
    {
        namespace la = matriz::ui::layoutarvore;
        std::vector<la::No> nos = {{"a", "", false, 0, 0}, {"b", "a", false, 0, 0}, {"c", "a", false, 0, 0},
                                   {"d", "b", false, 0, 0}, {"e", "", false, 0, 0}};
        const auto pos = la::calcular(nos, {});
        checar(pos.size() == 5, "every free node gets a position");
        bool sobrepoe = false;
        for (const auto& [i1, p1] : pos)
            for (const auto& [i2, p2] : pos)
                if (i1 < i2 && std::abs(p1.x - p2.x) < 190 && std::abs(p1.y - p2.y) < 84) sobrepoe = true;
        checar(!sobrepoe, "the default layout never overlaps two folders");
        checar(pos.at("a").y == (pos.at("b").y + pos.at("c").y) / 2, "the parent is centered vertically over its children");
        checar(pos.at("b").x == pos.at("c").x && pos.at("b").x > pos.at("a").x && pos.at("d").x > pos.at("b").x,
               "one column per depth level");
        // Filhas de pais diferentes (o bug do defaultY por índice entre irmãos) não colidem.
        checar(pos.at("e").y >= pos.at("d").y + 84 || pos.at("e").y + 84 <= pos.at("a").y, "a second root does not land on the first tree");

        std::vector<la::No> mistos = {{"p", "", true, 500, 300}, {"f", "p", false, 0, 0}, {"g", "p", false, 0, 0},
                                      {"solto", "", false, 0, 0}};
        const auto pos2 = la::calcular(mistos, {});
        checar(pos2.count("p") == 0, "a folder with a saved position is never moved");
        checar(pos2.at("f").x == 500 + 190 + 50 && pos2.at("g").x == pos2.at("f").x,
               "unsaved children of a saved parent sit to its right");
        checar(pos2.at("f").y + 84 + 26 == pos2.at("g").y, "the children stack with uniform spacing");
        checar((pos2.at("f").y + pos2.at("g").y + 84) / 2 == 300 + 42, "the children block is centered on the saved parent");
        checar(pos2.at("solto").y >= 300 + 84, "a new root goes below the saved folders instead of on top of them");
    }

    std::cout << "\n-- METADATA: Unknown = no EVENT DATE (item A) + auto-organize (item F) --\n";
    juce::File raizAj = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("matriz_lote_ajustes_" + juce::Uuid().toDashedString());
    try {
        raizAj.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Ajustes";
        params.prefixoNomenclatura = "AJU";
        auto projeto = matriz::model::Project::criar(raizAj.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        const std::string mapa = pa.mapaAtivoPadrao();

        auto arquivoReal = [&](const juce::String& nome) {
            auto f = raizAj.getChildFile(nome);
            f.replaceWithText("x");
            return f;
        };
        const auto i1 = inserirItemComArquivo(reg, projetoId, "AJU-1", arquivoReal("um.wav"));      // só data do disco
        const auto i2 = inserirItemComArquivo(reg, projetoId, "AJU-2", arquivoReal("dois.wav"));    // EVENT DATE 2019
        const auto i3 = inserirItemComArquivo(reg, projetoId, "AJU-3", arquivoReal("tres.jpg"));    // EXIF 2011
        const auto i4 = inserirItemComArquivo(reg, projetoId, "AJU-4", arquivoReal("quatro.wav"));  // dc_created 2020
        const auto i5 = inserirItemComArquivo(reg, projetoId, "AJU-5", arquivoReal("cinco.wav"));   // ficará na subpasta manual
        definirCampoRaiz(reg, i2, "ano", "12/03/2019");  // EVENT DATE em data completa, não só o ano
        definirCampoRaiz(reg, i3, "ano", "2011");
        definirCampoRaiz(reg, i4, "ano", "2020");
        // Só EXIF / dc_created / data do disco, sem EVENT DATE: Unknown.
        const auto i7 = inserirItemComArquivo(reg, projetoId, "AJU-7", arquivoReal("sete.jpg"));
        const auto i8 = inserirItemComArquivo(reg, projetoId, "AJU-8", arquivoReal("oito.wav"));
        reg.run("UPDATE arquivo SET caracteristicas_tecnicas_json = ? WHERE item_id = ?",
                {Value::of("{\"exifDataOriginal\":\"2011:05:01 10:00:00\"}"), Value::of(i7)});
        definirCampoRaiz(reg, i7, "dc_created", "2020-03-04");
        definirCampoRaiz(reg, i8, "ano", "0");
        reg.run("UPDATE item SET collection_type = 'Photo' WHERE id IN (?, ?)", {Value::of(i2), Value::of(i3)});
        reg.run("UPDATE item SET collection_type = 'Video' WHERE id = ?", {Value::of(i4)});

        // ----- A: Unknown
        auto itens = ProjetoAberto::listarItensDeProjeto(reg, pa.projeto().indice(), pa.projeto().pasta());
        auto achar = [&](const std::string& id) -> const ItemResumo* {
            for (const auto& r : itens) if (r.id == id) return &r;
            return nullptr;
        };
        const auto* r1 = achar(i1);
        const auto* r2 = achar(i2);
        const auto* r3 = achar(i3);
        checar(r1 && r1->anoDesconhecido() && !r1->ano.has_value(),
               "no EVENT DATE (the file date on disk does not count) is Unknown");
        checar(r2 && r2->ano == 2019 && !r2->anoDesconhecido(),
               "an EVENT DATE (full date 12/03/2019) is not Unknown and gives its year");
        checar(r3 && r3->ano == 2011 && !r3->anoDesconhecido(), "a filled EVENT DATE is not Unknown");
        const auto* r7 = achar(i7);
        const auto* r8 = achar(i8);
        checar(r7 && r7->anoDesconhecido(), "EXIF / dc_created without an EVENT DATE is still Unknown");
        checar(r8 && r8->anoDesconhecido(), "EVENT DATE = 0 is Unknown");

        // ----- F: segmentos de pasta (uma consulta por lote)
        using N = matriz::consolidacao::NivelHierarquia;
        const auto segs = pa.segmentosDeOrganizacao({i1, i2, i3, i4, i7, i8},
                                                     {N::Ano, N::ContentType, N::TipoArquivo, N::Origem, N::Artista, N::Subject, N::TipoMidia});
        auto junta = [&](const std::string& id) {
            juce::StringArray a;
            for (auto& x : segs.at(id)) a.add(x);
            return a.joinIntoString("/");
        };
        checar(junta(i1) == "No year/No content/WAV/No source medium/No creator/No subject/digital_audio",
               "empty values fall back to the backup's labels; the file date on disk is NOT a year (" + junta(i1) + ")");
        checar(junta(i2).startsWith("2019/Photo/WAV/"), "YEAR comes from EVENT DATE, CONTENT from collection_type, FILE TYPE from the extension");
        checar(junta(i3).startsWith("2011/Photo/JPG/"), "YEAR comes from EVENT DATE");
        checar(junta(i4).startsWith("2020/Video/"), "YEAR comes from EVENT DATE (2020)");
        checar(junta(i7).startsWith("No year/") && junta(i8).startsWith("No year/"),
               "EXIF / dc_created without EVENT DATE, and EVENT DATE 0, go to No year");

        // ----- F: organizar
        const std::string inbox = pa.criarPastaAcervo("Inbox", std::nullopt, mapa);
        const std::string manual = pa.criarPastaAcervo("Manual", inbox, mapa);
        pa.adicionarItensAPasta({i1, i2, i3, i4}, inbox);
        pa.adicionarItensAPasta({i5}, manual);
        auto pastaDoItem = [&](const std::string& item) {
            auto st = reg.prepare("SELECT p.id, p.nome, COALESCE(p.pasta_pai_id, ''), COALESCE(p.regra_organizacao, '') "
                                  "FROM acervo_item_pasta aip JOIN acervo_pasta p ON p.id = aip.pasta_id WHERE aip.item_id = ? AND aip.mapa_id = ?");
            st.bind(1, Value::of(item));
            st.bind(2, Value::of(mapa));
            struct R { std::string id, nome, pai, regra; };
            std::vector<R> out;
            while (st.step()) out.push_back({st.columnText(0), st.columnText(1), st.columnText(2), st.columnText(3)});
            return out;
        };
        auto caminhoDaPasta = [&](std::string id) {
            juce::StringArray a;
            while (!id.empty()) {
                auto st = reg.prepare("SELECT nome, COALESCE(pasta_pai_id, '') FROM acervo_pasta WHERE id = ?");
                st.bind(1, Value::of(id));
                if (!st.step()) break;
                a.insert(0, juce::String::fromUTF8(st.columnText(0).c_str()));
                id = st.columnText(1);
            }
            return a.joinIntoString("/");
        };
        auto contarPastas = [&] {
            auto st = reg.prepare("SELECT COUNT(*) FROM acervo_pasta WHERE mapa_id = ?");
            st.bind(1, Value::of(mapa));
            return st.step() ? static_cast<int>(st.columnInt(0)) : 0;
        };
        auto regraDe = [&](const std::string& id) {
            auto st = reg.prepare("SELECT COALESCE(regra_organizacao, '') FROM acervo_pasta WHERE id = ?");
            st.bind(1, Value::of(id));
            return st.step() ? st.columnText(0) : std::string();
        };
        const int pastasAntes = contarPastas();
        checar(!pa.pastaTemArquivosNoMain(inbox), "setup: Inbox has nothing in the MAIN");

        const auto r = pa.aplicarAutoOrganizacao(inbox, "ano,content_type");
        checar(r.status == ProjetoAberto::StatusAutoOrg::Ok && r.itensMovidos == 4, "YEAR > CONTENT moved the 4 loose items");
        checar(caminhoDaPasta(pastaDoItem(i2).front().id) == "Inbox/2019/Photo", "EVENT DATE 2019 + Photo -> Inbox/2019/Photo");
        checar(caminhoDaPasta(pastaDoItem(i3).front().id) == "Inbox/2011/Photo", "EVENT DATE 2011 + Photo -> Inbox/2011/Photo");
        checar(caminhoDaPasta(pastaDoItem(i4).front().id) == "Inbox/2020/Video", "dc_created 2020 + Video -> Inbox/2020/Video");
        checar(caminhoDaPasta(pastaDoItem(i1).front().id) == "Inbox/No year/No content", "empty values go to No year/No content");
        checar(pastaDoItem(i2).front().regra == "@auto", "generated subfolders are marked AUTO");
        checar(regraDe(inbox) == "ano,content_type", "the folder keeps the rule");
        checar(pastaDoItem(i5).size() == 1 && pastaDoItem(i5).front().id == manual, "the hand-made subfolder is left untouched");

        // Reorganizar sem mudança: nada mexe, nem cria pasta.
        const int pastasDepois = contarPastas();
        const auto r2again = pa.aplicarAutoOrganizacao(inbox, "ano,content_type");
        checar(r2again.itensMovidos == 0 && r2again.pastasCriadas == 0 && contarPastas() == pastasDepois,
               "reorganizing with nothing changed is a no-op (folders are reused by name)");

        // Metadado mudou: reorganizar mexe só no item e apaga a AUTO que esvaziou.
        definirCampoRaiz(reg, i3, "ano", "2020");
        const auto rMudou = pa.aplicarAutoOrganizacao(inbox, "ano,content_type");
        checar(rMudou.itensMovidos == 1 && rMudou.pastasApagadas >= 2, "after the metadata changed, only that item moves and the emptied AUTO folders are deleted");
        checar(caminhoDaPasta(pastaDoItem(i3).front().id) == "Inbox/2020/Photo", "the item lands in the reused/created 2020/Photo");
        {
            auto st = reg.prepare("SELECT COUNT(*) FROM acervo_pasta WHERE id = ?");
            st.bind(1, Value::of(manual));
            st.step();
            checar(st.columnInt(0) == 1, "a manual folder is never deleted");
        }

        // Desfazer: uma organização = um Cmd+Z.
        pa.desfazer();  // desfaz r3
        checar(caminhoDaPasta(pastaDoItem(i3).front().id) == "Inbox/2011/Photo", "one Undo reverts the whole re-organization (item and folders back)");
        pa.desfazer();  // desfaz a 1ª organização
        checar(pastaDoItem(i2).size() == 1 && pastaDoItem(i2).front().id == inbox && regraDe(inbox).empty() && contarPastas() == pastasAntes,
               "one Undo reverts the whole first organization: items back in Inbox, AUTO folders gone, rule cleared");

        // Passada automática: item solto na pasta com regra é organizado sem entrada de desfazer.
        pa.aplicarAutoOrganizacao(inbox, "ano,content_type");
        const auto descricaoAntes = pa.descricaoUndoAtual();
        const auto i6 = inserirItemComArquivo(reg, projetoId, "AJU-6", arquivoReal("seis.wav"));
        definirCampoRaiz(reg, i6, "ano", "2019");
        reg.run("UPDATE item SET collection_type = 'Photo' WHERE id = ?", {Value::of(i6)});
        pa.adicionarItensAPasta({i6}, inbox);
        const auto descricaoComMove = pa.descricaoUndoAtual();
        checar(pa.organizarItensSoltosDasPastasComRegra(mapa) == 1, "the automatic pass organizes the folder that received a loose item");
        checar(caminhoDaPasta(pastaDoItem(i6).front().id) == "Inbox/2019/Photo", "the loose item went to the AUTO subfolder");
        checar(pa.descricaoUndoAtual() == descricaoComMove, "the automatic pass registers no undo entry of its own");
        checar(pa.organizarItensSoltosDasPastasComRegra(mapa) == 0, "nothing loose left: the check is a no-op");
        juce::ignoreUnused(descricaoAntes);

        // Desligar: mantém as pastas, tira só a regra e a marcação AUTO.
        const int pastasComRegra = contarPastas();
        pa.desligarAutoOrganizacao(inbox);
        checar(regraDe(inbox).empty() && pastaDoItem(i6).front().regra.empty() && contarPastas() == pastasComRegra,
               "turning it off keeps every folder and only removes the rule and the AUTO mark");
        pa.desfazer();
        checar(regraDe(inbox) == "ano,content_type" && pastaDoItem(i6).front().regra == "@auto", "undo of turning it off restores rule and AUTO marks");

        // Bloqueios: pasta com arquivo no MAIN, subpasta AUTO, mapa ORIGINAL.
        checar(pa.aplicarAutoOrganizacao(pastaDoItem(i6).front().pai, "ano").status == ProjetoAberto::StatusAutoOrg::SubpastaAuto,
               "an AUTO subfolder does not accept its own rule");
        reg.run("INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                "checksum_sha256, consolidado_em) SELECT ?, item_id, ?, id, 'x.wav', 'abc', ? FROM arquivo WHERE item_id = ?",
                {Value::of(matriz::model::novoUuid()), Value::of(manual), Value::of(matriz::model::agoraIso8601()), Value::of(i5)});
        const std::string bloqueada = pa.criarPastaAcervo("Bloqueada", std::nullopt, mapa);
        const std::string dentro = pa.criarPastaAcervo("Dentro", bloqueada, mapa);
        pa.adicionarItensAPasta({i5}, dentro);
        checar(pa.aplicarAutoOrganizacao(bloqueada, "ano").status == ProjetoAberto::StatusAutoOrg::TemArquivosNoMain,
               "a folder that already has files in the MAIN is blocked");
        checar(pa.aplicarAutoOrganizacao(inbox, "").status == ProjetoAberto::StatusAutoOrg::SemNiveis, "no blocks: nothing to apply");

        // ----- INTAKE: R (Reject)
        std::cout << "\n-- INTAKE: R (Reject) --\n";
        {
            auto novoRej = [&](const char* codigo, const char* conteudo, bool quarentena) {
                auto f = raizAj.getChildFile(juce::String(codigo) + ".bin");
                f.replaceWithText(conteudo);
                const auto id = inserirItemComArquivo(reg, projetoId, codigo, f);
                reg.run("UPDATE item SET em_quarentena = ? WHERE id = ?", {Value::of(quarentena ? 1 : 0), Value::of(id)});
                return std::make_pair(id, f);
            };
            const auto [ra, fa] = novoRej("REJ-A", "conteudo A", true);
            const auto [rb, fb] = novoRej("REJ-B", "conteudo B", true);
            const auto [rc, fc] = novoRej("REJ-C", "conteudo C", true);
            const auto [rd, fd] = novoRej("REJ-D", "conteudo D", false);  // já no grid: fora do INTAKE

            pa.alternarMarcaR({ra, rb, rc});
            checar(pa.idsMarcadosR() == std::set<std::string>({ra, rb, rc}), "R marks the items and the marks persist in the project");
            pa.alternarMarcaR({ra, rb, rc});
            checar(pa.idsMarcadosR().empty(), "R again unmarks them");
            pa.alternarMarcaR({ra});
            pa.alternarMarcaR({ra, rb});
            checar(pa.idsMarcadosR() == std::set<std::string>({ra, rb}), "R on a mixed selection marks all of it (same rule as P/W/K)");
            pa.alternarMarcaR({rd});
            checar(pa.idsMarcadosR().count(rd) == 0, "an item that is no longer in the INTAKE is never listed as marked");

            // Marcado com R não entra no grid de jeito nenhum (nem em lote, nem pelo "+" de um item só).
            auto emQuarentena = [&](const std::string& id) {
                auto st = reg.prepare("SELECT COALESCE(em_quarentena, 0) FROM item WHERE id = ?");
                st.bind(1, Value::of(id));
                st.step();
                return static_cast<int>(st.columnInt(0));
            };
            pa.confirmarLoteGrid({ra, rb, rc});
            checar(emQuarentena(ra) == 1 && emQuarentena(rb) == 1 && emQuarentena(rc) == 0,
                   "Send to GRID skips the files marked R (they stay in the INTAKE) and sends the rest");
            pa.confirmarItemGrid(ra);
            checar(emQuarentena(ra) == 1, "sending a single file marked R to the GRID does nothing");
            pa.desfazer();  // desfaz o envio do rc; a pilha volta ao que o teste espera
            checar(emQuarentena(rc) == 1, "undo of Send to GRID brings the unmarked file back");

            // Legenda de atalhos: limpar a marca E só dos selecionados.
            pa.alternarMarcadoRevisado({ra, rb});
            pa.limparMarcadoRevisadoDe({ra});
            auto revisado = [&](const std::string& id) { return pa.itemMarcadoRevisado(id); };
            checar(!revisado(ra) && revisado(rb), "clearing a shortcut mark touches only the files given, not every marked file");
            pa.limparMarcadoRevisadoDe({rb});

            auto contarItens = [&](const std::string& id) {
                auto st = reg.prepare("SELECT COUNT(*) FROM item WHERE id = ?");
                st.bind(1, Value::of(id));
                st.step();
                return static_cast<int>(st.columnInt(0));
            };
            auto contarRejeitados = [&] {
                auto st = reg.prepare("SELECT COUNT(*) FROM intake_rejeitados");
                st.step();
                return static_cast<int>(st.columnInt(0));
            };
            checar(pa.rejeitarMarcadosR() == 2, "REJECT MARKED takes out exactly the 2 marked items");
            checar(contarItens(ra) == 0 && contarItens(rb) == 0, "the marked items left the project");
            checar(contarItens(rc) == 1 && contarItens(rd) == 1, "the unmarked ones (even if they were selected) stay");
            checar(contarRejeitados() == 2, "the project keeps the SHA-256 of each rejected file");
            checar(fa.existsAsFile() && fb.existsAsFile() && fa.loadFileAsString() == "conteudo A",
                   "the original files are untouched");
            pa.desfazer();
            checar(contarItens(ra) == 1 && contarItens(rb) == 1, "one Undo brings the rejected items back");
            checar(contarRejeitados() == 0, "and takes their hashes out of the rejected list");
        }

        // ----- NEST (camada de dados)
        std::cout << "\n-- NEST: create, merge, cover, outputs, folders, undo --\n";
        {
            auto novoNest = [&](const char* codigo, const char* data) {
                auto f = raizAj.getChildFile(juce::String(codigo) + ".nst");
                f.replaceWithText(codigo);
                const auto id = inserirItemComArquivo(reg, projetoId, codigo, f);
                definirCampoRaiz(reg, id, "data_criacao", data);
                return id;
            };
            const auto n1 = novoNest("NST-1", "2011-05-01 10:00:03");
            const auto n2 = novoNest("NST-2", "2011-05-01 10:00:02");
            const auto n3 = novoNest("NST-3", "2011-05-01 10:00:01");  // o mais antigo
            const auto n4 = novoNest("NST-4", "2011-05-01 10:00:04");
            const auto n5 = novoNest("NST-5", "2011-05-02 09:00:00");  // fora do nest

            checar(pa.criarNest({n1}).empty(), "a nest needs at least 2 files");
            const auto nestId = pa.criarNest({n1, n2, n3});
            auto info = pa.nestDoItem(n1);
            checar(!nestId.empty() && info && info->total == 3, "Nest groups the 3 selected files");
            checar(info && info->capaId == n3, "before the user chooses, the cover is the first file by date");
            checar(!pa.nestDoItem(n5).has_value(), "files outside the nest stay normal");

            // Nest sobre nest junta tudo num só, mantendo a capa do mais antigo
            const auto nestId2 = pa.criarNest({n4, n1});
            info = pa.nestDoItem(n2);
            checar(nestId2 == nestId && info && info->total == 4 && info->capaId == n3, "Nest on a file already nested merges everything into ONE nest, keeping its cover");

            // Capa: a marca de EXPORT (K) acompanha
            pa.definirMarcacao(ProjetoAberto::TipoMarcacao::Zip, {n3}, true);
            pa.definirCapaDoNest(nestId, n2);
            checar(pa.nestDoItem(n1) && pa.nestDoItem(n1)->capaId == n2, "clicking a file in the preview column makes it the new cover");
            checar(pa.contemMarcacao(ProjetoAberto::TipoMarcacao::Zip, n2) && !pa.contemMarcacao(ProjetoAberto::TipoMarcacao::Zip, n3),
                   "the export mark follows the cover");
            checar(pa.membrosDoNest(nestId).size() == 4, "the files that were not chosen stay in the nest (a nest is not a rejection)");

            // Saídas: de cada nest sai só a capa
            const auto saida = pa.semMembrosNaoCapaDeNest({n1, n2, n3, n4, n5});
            checar(saida == std::vector<std::string>({n2, n5}), "EXPORT/HTML/ZIP/Print/Watermark take only the cover of each nest (" + juce::String((int) saida.size()) + " ids)");
            const auto todos = pa.idsDoCatalogoSemNaoCapas();
            bool temNaoCapa = false;
            if (todos) for (const auto& id : *todos) if (id == n1 || id == n3 || id == n4) temNaoCapa = true;
            checar(todos.has_value() && !temNaoCapa, "'all assets' also leaves the non-cover files out");

            // Mover o nest para uma pasta move todos os arquivos juntos
            const std::string pastaNest = pa.criarPastaAcervo("Para o nest", std::nullopt, mapa);
            pa.adicionarItensAPasta({n2}, pastaNest);
            auto naPasta = [&](const std::string& id) {
                auto st = reg.prepare("SELECT COUNT(*) FROM acervo_item_pasta WHERE item_id = ? AND pasta_id = ?");
                st.bind(1, Value::of(id));
                st.bind(2, Value::of(pastaNest));
                st.step();
                return st.columnInt(0) == 1;
            };
            checar(naPasta(n1) && naPasta(n2) && naPasta(n3) && naPasta(n4) && !naPasta(n5), "moving a nest to a folder moves all its files together");
            pa.desfazer();
            checar(!naPasta(n1) && !naPasta(n4), "one Undo takes them all back");

            // Un-nest e Undo
            pa.desfazerNest({n2});
            checar(!pa.nestDoItem(n1) && !pa.nestDoItem(n3) && pa.semMembrosNaoCapaDeNest({n1, n3}).size() == 2,
                   "Un-nest returns every file to the grid as a normal item");
            pa.desfazer();
            checar(pa.nestDoItem(n1) && pa.nestDoItem(n1)->total == 4 && pa.nestDoItem(n1)->capaId == n2, "Undo of Un-nest brings the nest back with its cover");
            pa.desfazerNest({n1});  // deixa o projeto sem nests para o resto dos testes
        }
    } catch (const std::exception& e) {
        checar(false, juce::String("lote de ajustes selftest: ") + e.what());
    }
    raizAj.deleteRecursively();
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
            checar(pa->contarItens() == kItensPorLado && pa->contarItens() == static_cast<int>(pa->listarItens().size()),
                   "contarItens() (SQL COUNT, no disk access) matches listarItens()");
            // GET EXIF grava a OTHER METADATA sem tocar nas notas do usuário;
            // rodar de novo substitui (não duplica).
            {
                // back(): o teste da tecla E logo abaixo usa o front() e exige
                // um item nunca editado.
                const std::string alvo = mosaico->todosItensEmMemoria().back().id;
                const std::string antes = pa->lerMetadado(alvo, "notas_livres").value_or("");
                pa->salvarMetadado(alvo, "notas_livres", "[NOTES]\nminha nota");
                pa->gravarOutraMetadataEmLote({{alvo, "FNumber: 5/1"}});
                pa->gravarOutraMetadataEmLote({{alvo, "FNumber: 8/1\nISOSpeedRatings: 400"}});
                const std::string depois = pa->lerMetadado(alvo, "notas_livres").value_or("");
                checar(depois == "[" + std::string(matriz::model::kOutraMetadataTitulo) +
                                     "]\nFNumber: 8/1\nISOSpeedRatings: 400\n\n[NOTES]\nminha nota",
                       "GET EXIF writes OTHER METADATA, replaces it on a second run, keeps the user's notes");
                pa->salvarMetadado(alvo, "notas_livres", antes);
            }
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

            // Item 3 (correção METADATA 2026-09-28): E ficava preso no campo
            // até reselecionar na grade porque o campo mantém o foco depois
            // de Enter. O fix pede o foco de volta (aoPedirFocoGrade) só
            // quando o commit veio de Enter — nunca de onFocusLost (Tab/
            // clique noutro campo), senão isso rouba o foco do campo que o
            // usuário acabou de entrar. Sem depender de foco real de janela
            // (o harness não usa addToDesktop): espiona o próprio sinal.
            // Fica por último no bloco Catalog — muda CREATOR mais uma vez,
            // sem mais nada abaixo que dependa do valor ou da pilha de undo.
            {
                int pedidosFoco = 0;
                auto espiao = [&] { ++pedidosFoco; };
                auto original = cw->fichaPanel_->aoPedirFocoGrade;
                cw->fichaPanel_->aoPedirFocoGrade = espiao;

                auto* edCreator = dynamic_cast<juce::TextEditor*>(ficha->editorDoCampoLoteParaTeste("creator"));
                checar(edCreator != nullptr, "CREATOR editor available for the focus-return test");
                if (edCreator) {
                    // Caso 1: Enter -> pede o foco de volta pra grade.
                    edCreator->setText("Creator Enter", false);
                    if (edCreator->onReturnKey) edCreator->onReturnKey();
                    bombear(100);
                    checar(pedidosFoco == 1,
                           "pressing Enter after a batch edit asks the grid for focus back, so E works with no reselect (" +
                               juce::String(pedidosFoco) + ")");
                    checar(contar(false, "dc_creator", "Creator Enter") == kItensPorLado,
                           "the Enter case still writes the value to every selected item");

                    // Caso 2: sair do campo (Tab/clique noutro campo) ->
                    // NUNCA pede o foco de volta (roubaria do campo pro qual
                    // o usuário acabou de ir).
                    pedidosFoco = 0;
                    edCreator->setText("Creator Blur", false);
                    if (edCreator->onFocusLost) edCreator->onFocusLost();
                    bombear(100);
                    checar(pedidosFoco == 0,
                           "leaving the field via Tab/click (focus-lost) never asks for the grid's focus back (" +
                               juce::String(pedidosFoco) + ")");
                    checar(contar(false, "dc_creator", "Creator Blur") == kItensPorLado,
                           "the focus-lost case still writes the value to every selected item, it just doesn't steal focus");
                }

                // Pedido 2026-09-29: dropdown (ComboBox) também precisa
                // pedir o foco de volta — diferente de texto, escolher uma
                // opção é sempre um commit completo, nunca um "só passando
                // por aqui" ambíguo, então não tem o mesmo cuidado do
                // onFocusLost acima.
                pedidosFoco = 0;
                auto* cbContent = dynamic_cast<juce::ComboBox*>(ficha->editorDoCampoLoteParaTeste("collection"));
                checar(cbContent != nullptr && cbContent->getNumItems() > 1, "CONTENT dropdown has enough options for the focus-return test");
                if (cbContent && cbContent->getNumItems() > 1) {
                    // Índice diferente do já selecionado (um clique acima já
                    // deixou em 0) — senão onChange não dispara (sem mudança
                    // real) e o teste não provaria nada.
                    int idxAlvo = cbContent->getSelectedItemIndex() == 0 ? 1 : 0;
                    cbContent->setSelectedItemIndex(idxAlvo, juce::sendNotificationSync);
                    bombear(100);
                    checar(pedidosFoco == 1,
                           "picking a dropdown option also asks the grid for focus back, so E works right after it without reselecting (" +
                               juce::String(pedidosFoco) + ")");
                }
                cw->fichaPanel_->aoPedirFocoGrade = original;
            }
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

            // ---- R (Reject): tecla, botão e a regra "a seleção é ignorada"
            {
                iw->selecionarTodos(false);
                const auto R = juce::KeyPress('r', juce::ModifierKeys(), (juce::juce_wchar) 'r');
                for (int i = 0; i < 3; ++i) iw->todosItens_[(size_t) i].selecionado = true;
                iw->atualizarFiltragem();
                checar(iw->keyPressed(R), "R key is handled in the INTAKE");
                checar(iw->marcadosR_.size() == 3, "R marks every selected file (" + juce::String((int) iw->marcadosR_.size()) + ")");
                iw->keyPressed(R);
                checar(iw->marcadosR_.empty(), "R again unmarks them");
                checar(!iw->keyPressed(juce::KeyPress('r', juce::ModifierKeys::commandModifier, (juce::juce_wchar) 'r')),
                       "Cmd+R is not the R mark");

                iw->keyPressed(R);  // marca 0,1,2
                const auto marcadosIds = iw->marcadosR_;
                if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
                    // Altura de uma janela real (a de teste corta o fim da coluna esquerda).
                    const auto tamanhoOriginal = iw->getBounds();
                    iw->setBounds(tamanhoOriginal.withHeight(1000));
                    bombear(200);
                    juce::PNGImageFormat png;
                    auto arq = dir.getChildFile("intake_reject.png");
                    arq.deleteFile();
                    if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                        png.writeImageToStream(iw->createComponentSnapshot(iw->getLocalBounds()), *out);
                    iw->definirModoVisao(IntakeWorkspaceComponent::ModoVisao::Icones);
                    bombear(300);
                    auto arqGrade = dir.getChildFile("intake_reject_grade.png");
                    arqGrade.deleteFile();
                    if (auto out = std::unique_ptr<juce::FileOutputStream>(arqGrade.createOutputStream()))
                        png.writeImageToStream(iw->createComponentSnapshot(iw->getLocalBounds()), *out);
                    iw->definirModoVisao(IntakeWorkspaceComponent::ModoVisao::Lista);
                    iw->setBounds(tamanhoOriginal);
                    bombear(100);
                }
                checar(iw->btnRemoverSelecao_->getButtonText().contains("(3)") && iw->btnRemoverSelecao_->isEnabled(),
                       "the button shows how many files will be rejected");
                iw->selecionarTodos(false);
                for (int i = 5; i < 7; ++i) iw->todosItens_[(size_t) i].selecionado = true;  // seleção DIFERENTE das marcas
                iw->atualizarFiltragem();
                int mudouUndo = 0;
                janela.aoMudouUndo = [&] { ++mudouUndo; };
                iw->rejeitarMarcados();
                bombear(300);
                checar(mudouUndo >= 1, "the menu bar is told when the Undo stack changes (Edit > Undo stops being greyed out)");
                janela.aoMudouUndo = nullptr;
                int restam = contar(true, "estado", "novo");
                auto ainda = [&](const std::string& id) {
                    auto st = reg.prepare("SELECT COUNT(*) FROM item WHERE id = ?");
                    st.bind(1, matriz::db::Value::of(id));
                    st.step();
                    return st.columnInt(0) == 1;
                };
                bool algumMarcadoFicou = false;
                for (const auto& id : marcadosIds) algumMarcadoFicou = algumMarcadoFicou || ainda(id);
                (void) restam;
                checar(!algumMarcadoFicou, "REJECT MARKED removed every file marked R");
                checar(static_cast<int>(iw->todosItens_.size()) == kItensPorLado - 3,
                       "and only those: the selection was ignored (" + juce::String((int) iw->todosItens_.size()) + " left)");
                checar(iw->marcadosR_.empty() && !iw->btnRemoverSelecao_->isEnabled(), "no marks left: the button is disabled");
                pa->desfazer();
                bombear(300);
                iw->recarregar();
                esperarAte([&] { return !iw->snapshotPendente() && static_cast<int>(iw->todosItens_.size()) >= kItensPorLado; });
                checar(static_cast<int>(iw->todosItens_.size()) == kItensPorLado, "Undo brings the rejected files back");
            }
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
        std::vector<std::string> antigos, loteA, loteB, loteC;
        for (int i = 0; i < 3; ++i) antigos.push_back(inserirItem(projeto->registro(), projetoId, "REC-OLD-" + std::to_string(i), false));
        for (int i = 0; i < 6; ++i) loteA.push_back(inserirItem(projeto->registro(), projetoId, "REC-A-" + std::to_string(i), true));
        for (int i = 0; i < 4; ++i) loteB.push_back(inserirItem(projeto->registro(), projetoId, "REC-B-" + std::to_string(i), true));
        for (int i = 0; i < 5; ++i) loteC.push_back(inserirItem(projeto->registro(), projetoId, "REC-C-" + std::to_string(i), true));
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
        auto contagemPorChave = [](CatalogWorkspaceComponent* cw, const std::string& chave) {
            for (const auto& c : cw->categorias_) if (c.chave == chave) return c.contagem;
            return -1;
        };

        auto* cw = abrirGrid();
        // SUBJECT: opções vindas dos subjects existentes, filtro por um deles.
        esperarAte([&] { return !cw->subjectsDisponiveis_.empty(); }, 5000);
        int idTour = -1;
        for (size_t i = 0; i < cw->subjectsDisponiveis_.size(); ++i)
            if (cw->subjectsDisponiveis_[i].first == "Tour") idTour = static_cast<int>(i + 1);
        // 2 subjects existentes + a entrada fixa "No Subject" (kSemSubject), sempre a última.
        checar(cw->comboSubject_ != nullptr && idTour > 0 && cw->subjectsDisponiveis_.size() == 3 &&
                   cw->subjectsDisponiveis_.back().first == "__sem_subject__",
               "SUBJECT dropdown lists each existing subject once, plus the fixed No Subject entry last (" +
                   juce::String((int) cw->subjectsDisponiveis_.size()) + ")");
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
        // Item 4 (correção METADATA 2026-09-28), caminho "Send to Grid +
        // Recently Ingested": não é só o total que precisa ficar restrito à
        // leva — MEDIA TYPE também. Catálogo tem 9 itens de áudio (3
        // antigos + 6 de A); sem o filtro aplicado à contagem por tipo isto
        // daria 9, não 6.
        checar(contagemPorChave(cw, "audio") == 6,
               "MEDIA TYPE count (audio) also follows Send to Grid + Recently Ingested, not the whole catalog (" +
                   juce::String(contagemPorChave(cw, "audio")) + "/6)");

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

        // Reproduz a corrida real (item 1 do pedido 2026-09-28): Send to Grid
        // dispara um evento "quarentena" POR ITEM (EventBus -> callAsync na
        // message thread). Se um desses eventos chega enquanto o recarregar()
        // que o toggle "Show Recently Ingested" disparou ainda está em voo,
        // ele bumps MosaicoComponent::geracaoSnapshot_ (mesmo pra um item
        // ainda fora de memória) e orfanava esse recarregar() — a resposta
        // chegava descartada, aoMudarConteudoVisivel nunca disparava, e
        // filtrosAguardandoSnapshot_ ficava travado até um clique qualquer
        // (ex.: MEDIA TYPE) forçar outro aplicarFiltrosAdicionais().
        //
        // O reopen do projeto acima recria o CatalogWorkspaceComponent, que
        // agenda um job de miniaturas faltantes (500 ms, ver construtor) cujo
        // recarregar() de conclusão poderia, por coincidência de tempo,
        // disparar bem no meio da corrida abaixo e mascarar um fix quebrado —
        // deixa esse job terminar e se acomodar primeiro.
        bombear(900);
        esperarAte([&] { return !cw->mosaico_->snapshotPendente(); });

        cw->btnMostrarRecentes_->onClick();  // desliga (estava mostrando B)
        esperarAte([&] { return !cw->mosaico_->snapshotPendente(); });
        promover(loteC);  // drena os próprios eventos "quarentena" (no-op: C ainda não está em itensTodos_)

        // Chama atualizarItemEmMemoria() diretamente, na MESMA pilha, logo
        // após o recarregar() do toggle e antes de qualquer bombear — o
        // mesmo efeito de um evento "quarentena" chegando em voo, mas sem
        // depender do tempo real do job em background (determinístico).
        cw->btnMostrarRecentes_->onClick();  // liga; loteC ainda fora de itensTodos_ -> recarregar() em voo
        checar(cw->mosaico_->snapshotPendente(), "toggling ON with a fresh batch not yet in memory starts a snapshot reload");
        for (const auto& id : loteC) cw->mosaico_->atualizarItemEmMemoria(id);
        esperarAte([&] { return !cw->mosaico_->snapshotPendente() && visiveis(cw) == static_cast<int>(loteC.size()); }, 10000);
        checar(visiveis(cw) == static_cast<int>(loteC.size()),
               "an item-changed event landing mid-flight (e.g. Send to Grid) doesn't strand the toggle — it still shows the new batch with no extra click (" +
                   juce::String(visiveis(cw)) + ")");

        janela.reset();
    } catch (const std::exception& e) {
        checar(false, juce::String("recent batch selftest: ") + e.what());
    }
    raizR.deleteRecursively();

    // --------------------- METADATA: pasta enviada ao grid = seleção real (item 4, 2026-09-28)
    // "Show content in grid" (Folder Map/SOURCE/Acervo tree) precisa se
    // comportar como uma seleção feita no próprio grid: MEDIA TYPE e ano
    // contam só o conjunto da pasta, e a ficha (CONTENT) mostra o valor
    // real da pasta, não o que estava selecionado antes.
    std::cout << "\n-- METADATA: folder-sent-to-grid behaves like a real grid selection --\n";
    juce::File raizFolderSel = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("matriz_folder_selecao_selftest_" + juce::Uuid().toDashedString());
    try {
        raizFolderSel.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "FolderSel";
        params.prefixoNomenclatura = "FSEL";
        auto projeto = matriz::model::Project::criar(raizFolderSel.getChildFile("projeto"), params);
        const std::string projetoId = projeto->projetoId();
        // Pasta: 2 áudio + 1 vídeo, CONTENT "Music". Fora da pasta: 3
        // imagens, CONTENT "Interview" — se a contagem ou o CONTENT vazarem
        // do catálogo inteiro, esses 3 aparecem onde não deveriam.
        std::vector<std::string> pastaIds, foraIds;
        pastaIds.push_back(inserirItem(projeto->registro(), projetoId, "FSEL-A0", false, ".wav"));
        pastaIds.push_back(inserirItem(projeto->registro(), projetoId, "FSEL-A1", false, ".wav"));
        pastaIds.push_back(inserirItem(projeto->registro(), projetoId, "FSEL-V0", false, ".mp4"));
        foraIds.push_back(inserirItem(projeto->registro(), projetoId, "FSEL-I0", false, ".jpg"));
        foraIds.push_back(inserirItem(projeto->registro(), projetoId, "FSEL-I1", false, ".jpg"));
        foraIds.push_back(inserirItem(projeto->registro(), projetoId, "FSEL-I2", false, ".jpg"));
        auto& regF = projeto->registro();
        for (const auto& id : pastaIds) regF.run("UPDATE item SET collection_type = 'Music' WHERE id = ?", {matriz::db::Value::of(id)});
        for (const auto& id : foraIds) regF.run("UPDATE item SET collection_type = 'Interview' WHERE id = ?", {matriz::db::Value::of(id)});

        auto janela = std::make_unique<MainComponent>();
        janela->setBounds(0, 0, 1400, 900);
        janela->abrirProjeto(std::move(projeto));
        bombear(200);
        janela->mostrarGrid();
        auto* cw = janela->catalogWorkspace_.get();
        auto* mosaico = cw->mosaico_.get();
        esperarAte([&] { return !mosaico->snapshotPendente() && mosaico->totalItensCarregados() >= 6; });

        // Estado anterior "sujo" de propósito: seleciona um item DE FORA da
        // pasta primeiro, pra garantir que o fix realmente troca o que a
        // ficha mostra, não é coincidência de já estar certo.
        mosaico->selecionarItem(foraIds[0]);
        cw->selecionarItem(foraIds[0]);
        bombear(100);

        std::set<std::string> pasta(pastaIds.begin(), pastaIds.end());
        cw->definirSelecaoItens(pasta);
        esperarAte([&] { return !mosaico->snapshotPendente(); }, 5000);
        bombear(200);

        checar(mosaico->totalItensVisiveis() == 3,
               "the folder's content shows only its 3 items in the grid (" + juce::String(mosaico->totalItensVisiveis()) + ")");

        auto contagemPorChaveF = [&](const std::string& chave) {
            for (const auto& c : cw->categorias_) if (c.chave == chave) return c.contagem;
            return -1;
        };
        checar(contagemPorChaveF("audio") == 2,
               "MEDIA TYPE counts are scoped to the folder, not the whole catalog: audio (" + juce::String(contagemPorChaveF("audio")) + "/2)");
        checar(contagemPorChaveF("video") == 1, "... video (" + juce::String(contagemPorChaveF("video")) + "/1)");
        checar(contagemPorChaveF("images") == 0,
               "... images excluded even though the catalog has 3 of them outside the folder (" + juce::String(contagemPorChaveF("images")) + "/0)");

        auto* cbContent = dynamic_cast<juce::ComboBox*>(cw->fichaPanel_->editorDoCampoLoteParaTeste("collection"));
        checar(cbContent != nullptr && cbContent->getText() == "Music",
               "CONTENT reflects the folder's real value, not the item selected before (\"" +
                   (cbContent ? cbContent->getText() : juce::String()) + "\")");

        janela.reset();
    } catch (const std::exception& e) {
        checar(false, juce::String("folder selection selftest: ") + e.what());
    }
    raizFolderSel.deleteRecursively();

    // --------------------------------- PROJETO: Rename Project (item 5, 2026-09-28)
    // Só o rótulo muda: pasta, prefixo, máscara e o .mtz/.bkm original (por
    // conteúdo, não por nome reconstruído) continuam intactos. Testa o
    // modelo direto (Project::renomear) — o diálogo em si (MainWindow) é só
    // fiação sobre isto, sem lógica própria pra errar.
    std::cout << "\n-- PROJECT: Rename Project --\n";
    juce::File raizRename = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("matriz_rename_selftest_" + juce::Uuid().toDashedString());
    try {
        raizRename.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Antes do Rename";
        params.prefixoNomenclatura = "REN";
        auto projeto = matriz::model::Project::criar(raizRename.getChildFile("projeto"), params);
        const std::string projetoId = projeto->projetoId();
        const juce::File pastaProjetoDb = raizRename.getChildFile("projeto");

        auto lerPrefixoMascara = [&]() {
            auto stmt = projeto->registro().prepare("SELECT prefixo_nomenclatura, mascara_nomenclatura FROM projeto LIMIT 1");
            stmt.step();
            return std::make_pair(stmt.columnText(0), stmt.columnText(1));
        };
        auto [prefixoAntes, mascaraAntes] = lerPrefixoMascara();

        juce::Array<juce::File> markersAntes;
        projeto->raiz().findChildFiles(markersAntes, juce::File::findFiles, false, "*.mtz");
        checar(markersAntes.size() == 1, "project creation writes exactly one .mtz marker file (" + juce::String(markersAntes.size()) + ")");
        juce::String conteudoOriginal = markersAntes.isEmpty() ? juce::String() : markersAntes.getReference(0).loadFileAsString();
        juce::var jsonOriginal = juce::JSON::parse(conteudoOriginal);
        juce::String criadoEmOriginal = jsonOriginal.isObject() ? jsonOriginal.getProperty("criado_em", "").toString() : juce::String();
        checar(criadoEmOriginal.isNotEmpty(), "the original marker file has a criado_em to compare against after rename");

        projeto->renomear("Depois do Rename");
        checar(projeto->nome() == "Depois do Rename", "renomear() updates nome() live (\"" + juce::String(projeto->nome()) + "\")");

        auto [prefixoDepois, mascaraDepois] = lerPrefixoMascara();
        checar(prefixoAntes == prefixoDepois, "renaming never touches prefixo_nomenclatura (" + juce::String(prefixoDepois) + ")");
        checar(mascaraAntes == mascaraDepois, "renaming never touches mascara_nomenclatura");
        checar(projeto->raiz().getFullPathName() == pastaProjetoDb.getFullPathName() && pastaProjetoDb.exists(),
               "renaming never moves or renames the project folder (" + projeto->raiz().getFullPathName() + ")");

        // Fecha e reabre: Project::abrir() reconstruía o nome do arquivo a
        // partir do nome AO VIVO e, não achando o antigo, criava um .mtz
        // órfão novo — precisa continuar achando o original por extensão.
        projeto.reset();
        auto reaberto = matriz::model::Project::abrir(pastaProjetoDb);
        juce::Array<juce::File> markersDepois;
        reaberto->raiz().findChildFiles(markersDepois, juce::File::findFiles, false, "*.mtz");
        checar(markersDepois.size() == 1,
               "reopening after a rename still finds exactly one .mtz — no orphaned duplicate (" + juce::String(markersDepois.size()) + ")");
        if (markersDepois.size() == 1) {
            juce::var jsonDepois = juce::JSON::parse(markersDepois.getReference(0).loadFileAsString());
            juce::String criadoEmDepois = jsonDepois.isObject() ? jsonDepois.getProperty("criado_em", "").toString() : juce::String();
            checar(criadoEmDepois == criadoEmOriginal,
                   "the marker file found after rename is the ORIGINAL one (same criado_em), not a freshly synthesized replacement");
        }

        // Lista de Arquivos Recentes (o que pedirRenomearProjeto() chama
        // depois de Project::renomear).
        matriz::app::registrarRecente(reaberto->raiz().getFullPathName(), "Depois do Rename",
                                       matriz::model::modoToString(reaberto->modo()));
        bool achouRecente = false;
        for (const auto& r : matriz::app::lerRecentes())
            if (r.pasta == reaberto->raiz().getFullPathName()) achouRecente = (r.nome == "Depois do Rename");
        checar(achouRecente, "the Recent Files entry reflects the new name, keyed by the same folder path");

        // Compatibilidade retroativa (pedido explícito 2026-09-29): um
        // projeto criado ANTES desta mudança pode ter o .mtz num nome que
        // nunca bateu com projeto.nome, por qualquer motivo — não só
        // rename (ex.: sanitização diferente numa versão antiga do app).
        // Sem chamar renomear() nenhuma vez: simula renomeando só o ARQUIVO
        // no disco, deixando o banco intocado.
        {
            juce::File raizLegado = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                         .getChildFile("matriz_rename_legado_selftest_" + juce::Uuid().toDashedString());
            raizLegado.createDirectory();
            matriz::model::NovoProjetoParams paramsLegado;
            paramsLegado.nome = "Projeto Legado";
            paramsLegado.prefixoNomenclatura = "LEG";
            auto projetoLegado = matriz::model::Project::criar(raizLegado.getChildFile("projeto"), paramsLegado);
            const juce::File pastaLegado = raizLegado.getChildFile("projeto");
            juce::Array<juce::File> markersLegado;
            projetoLegado->raiz().findChildFiles(markersLegado, juce::File::findFiles, false, "*.mtz");
            checar(markersLegado.size() == 1, "legacy scenario setup: exactly one .mtz created (" + juce::String(markersLegado.size()) + ")");
            juce::String criadoEmLegado;
            if (!markersLegado.isEmpty()) {
                juce::var jAntes = juce::JSON::parse(markersLegado.getReference(0).loadFileAsString());
                criadoEmLegado = jAntes.isObject() ? jAntes.getProperty("criado_em", "").toString() : juce::String();
                markersLegado.getReference(0).moveFileTo(projetoLegado->raiz().getChildFile("Nome Bem Diferente Do Projeto.mtz"));
            }
            projetoLegado.reset();

            auto reabertoLegado = matriz::model::Project::abrir(pastaLegado);
            juce::Array<juce::File> markersLegadoDepois;
            reabertoLegado->raiz().findChildFiles(markersLegadoDepois, juce::File::findFiles, false, "*.mtz");
            checar(markersLegadoDepois.size() == 1,
                   "opening a project whose .mtz filename never matched its nome (pre-dating this fix) still finds exactly one, not a new one (" +
                       juce::String(markersLegadoDepois.size()) + ")");
            if (markersLegadoDepois.size() == 1) {
                juce::var jDepois = juce::JSON::parse(markersLegadoDepois.getReference(0).loadFileAsString());
                juce::String criadoEmDepois = jDepois.isObject() ? jDepois.getProperty("criado_em", "").toString() : juce::String();
                checar(criadoEmDepois == criadoEmLegado && markersLegadoDepois.getReference(0).getFileName() == "Nome Bem Diferente Do Projeto.mtz",
                       "the mismatched-name marker is used as-is (same content, same old filename) — nothing new synthesized");
            }
            reabertoLegado.reset();
            raizLegado.deleteRecursively();
        }

        reaberto.reset();
    } catch (const std::exception& e) {
        checar(false, juce::String("rename project selftest: ") + e.what());
    }
    raizRename.deleteRecursively();

    // ------------------ METADATA: P/W só aceitam fotos (pedido 2026-09-29)
    std::cout << "\n-- METADATA: P (Send to Print) / W (Watermark) only apply to photos --\n";
    juce::File raizFotoPW = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("matriz_foto_pw_selftest_" + juce::Uuid().toDashedString());
    try {
        raizFotoPW.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "FotoPW";
        params.prefixoNomenclatura = "FPW";
        auto projeto = matriz::model::Project::criar(raizFotoPW.getChildFile("projeto"), params);
        const std::string projetoId = projeto->projetoId();
        std::string idFoto = inserirItem(projeto->registro(), projetoId, "FPW-FOTO", false, ".jpg");
        std::string idAudio = inserirItem(projeto->registro(), projetoId, "FPW-AUDIO", false, ".wav");

        auto janela = std::make_unique<MainComponent>();
        janela->setBounds(0, 0, 1400, 900);
        janela->abrirProjeto(std::move(projeto));
        bombear(200);
        janela->mostrarGrid();
        auto* cw = janela->catalogWorkspace_.get();
        auto* mosaico = cw->mosaico_.get();
        esperarAte([&] { return !mosaico->snapshotPendente() && mosaico->totalItensCarregados() >= 2; });

        std::set<std::string> sel{idFoto, idAudio};
        mosaico->definirSelecao(sel);
        bombear(50);

        mosaico->keyPressed(juce::KeyPress('P', juce::ModifierKeys(), (juce::juce_wchar) 'p'));
        bombear(100);
        auto* pa = janela->projetoAberto();
        checar(pa->contemMarcacao(ProjetoAberto::TipoMarcacao::Print, idFoto), "P marks the photo for Print");
        checar(!pa->contemMarcacao(ProjetoAberto::TipoMarcacao::Print, idAudio),
               "P does not mark the non-photo item for Print, even though it was selected too");

        mosaico->keyPressed(juce::KeyPress('W', juce::ModifierKeys(), (juce::juce_wchar) 'w'));
        bombear(100);
        checar(pa->contemMarcacao(ProjetoAberto::TipoMarcacao::Watermark, idFoto), "W marks the photo for Watermark");
        checar(!pa->contemMarcacao(ProjetoAberto::TipoMarcacao::Watermark, idAudio),
               "W does not mark the non-photo item for Watermark, even though it was selected too");

        janela.reset();
    } catch (const std::exception& e) {
        checar(false, juce::String("print/watermark photo-only selftest: ") + e.what());
    }
    raizFotoPW.deleteRecursively();

    // ------------------- Folder Map: Disconnect from Parent em lote (item 7, 2026-09-29)
    std::cout << "\n-- Folder Map: batch Disconnect from Parent + undo (item 7) --\n";
    juce::File raizDisc = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("matriz_folder_disconnect_selftest_" + juce::Uuid().toDashedString());
    try {
        raizDisc.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Disconnect";
        params.prefixoNomenclatura = "DSC";
        auto projeto = matriz::model::Project::criar(raizDisc.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        std::string mapaPadrao = pa.mapaAtivoPadrao();

        std::string root = pa.criarPastaAcervo("Root", std::nullopt, mapaPadrao);
        std::string a = pa.criarPastaAcervo("A", root, mapaPadrao);
        std::string b = pa.criarPastaAcervo("B", root, mapaPadrao);
        std::string c = pa.criarPastaAcervo("C", root, mapaPadrao);  // vai ficar travada pelo MAIN

        const std::string itemC = inserirItem(reg, projetoId, "DSC-C", false);
        reg.run("INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                "checksum_sha256, consolidado_em) SELECT ?, item_id, ?, id, 'x.wav', 'abc', ? FROM arquivo WHERE item_id = ?",
                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(c),
                 matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(itemC)});
        checar(pa.pastaTemArquivosNoMain(c), "setup: folder C is locked by the MAIN");

        // Folder Map abre no ORIGINAL na 1ª vez e depois no último mapa usado.
        checar(pa.mapaInicialDoFolderMap() == ProjetoAberto::kMapaOriginal,
               "Folder Map opens on ORIGINAL the first time (no remembered map)");
        pa.definirMapaAtivo(mapaPadrao);  // o componente abre no último mapa usado (persistido)
        checar(pa.mapaInicialDoFolderMap() == mapaPadrao,
               "Folder Map reopens on the last used map");
        ArvoreBackupComponent arvore(pa);
        auto lerPai = [&](const std::string& id) -> std::string {
            auto stmt = reg.prepare("SELECT pasta_pai_id FROM acervo_pasta WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(id));
            if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
            return {};
        };

        // Seleciona A, B, C (com pai) e Root (sem pai — deve ser ignorada
        // em silêncio, sem contar como pulada).
        for (auto& n : arvore.nodes_)
            if (n.id == a || n.id == b || n.id == c || n.id == root) n.selecionado = true;

        checar(!pa.podeDesfazer(), "setup: nothing to undo yet");
        arvore.desconectarSelecionadas();

        checar(lerPai(a).empty() && lerPai(b).empty(), "A and B (unlocked) got disconnected from Root");
        checar(lerPai(c) == root, "C (locked by the MAIN) stayed connected — skipped, no error");
        checar(pa.podeDesfazer(), "the batch disconnect left an undo entry");

        pa.desfazer();
        checar(lerPai(a) == root && lerPai(b) == root,
               "a SINGLE Undo restores BOTH A and B back under Root — one undo step for the whole batch");
        checar(!pa.podeDesfazer(),
               "after undoing, the stack is empty — the batch really was one single step, not two");

        // Pedido explícito: arrastar (mover) uma pasta pra outro pai e
        // desfazer — moverPastaAcervo registra undo pra QUALQUER move de
        // hierarquia, não só desconectar; e desfazer() nunca registra a si
        // mesmo de novo (guard desfazendo_).
        std::string outroPai = pa.criarPastaAcervo("Outro Pai", std::nullopt, mapaPadrao);
        checar(pa.moverPastaAcervo(a, outroPai), "dragging A to a brand-new parent succeeds");
        checar(lerPai(a) == outroPai, "A is now under the new parent");
        checar(pa.podeDesfazer(), "the move left an undo entry");
        pa.desfazer();
        checar(lerPai(a) == root, "undo restores A back to its original parent (Root)");
        checar(!pa.podeDesfazer(),
               "the undo stack is empty right after reverting the move — the reverse move did NOT "
               "register a new undo entry (not registered twice)");
    } catch (const std::exception& e) {
        checar(false, juce::String("folder disconnect selftest: ") + e.what());
    }
    raizDisc.deleteRecursively();

    // ------------------- Folder Map: arrastar pasta-pai move o bloco (item 8, 2026-09-29)
    // mouseDown/mouseDrag/mouseUp dependem de juce::MouseEvent real (sem
    // precedente de simular isso em nenhum self-test deste arquivo) — testa
    // direto as duas peças que eles orquestram: coletarDescendentesIndices()
    // (que pastas entram no bloco) e persistirPosicoesEmLote() (a escrita
    // em lote de verdade, numa transação só). A separação entre Shift+
    // arrastar-sobre-pasta (move solo) e Shift+arrastar-no-vazio (laço de
    // seleção) é estrutural — ramos mutuamente exclusivos por hitNode em
    // mouseDown, conferida por leitura de código, não por este teste.
    std::cout << "\n-- Folder Map: dragging a parent moves the whole descendant block (item 8) --\n";
    juce::File raizBloco = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("matriz_folder_bloco_selftest_" + juce::Uuid().toDashedString());
    try {
        raizBloco.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Bloco";
        params.prefixoNomenclatura = "BLC";
        auto projeto = matriz::model::Project::criar(raizBloco.getChildFile("MAIN"), params);
        ProjetoAberto pa(std::move(projeto));
        std::string mapaPadrao = pa.mapaAtivoPadrao();

        // G -> P -> C (3 níveis) e S, irmã de P (mesmo pai G, fora da
        // subárvore de P).
        std::string g = pa.criarPastaAcervo("G", std::nullopt, mapaPadrao);
        std::string p = pa.criarPastaAcervo("P", g, mapaPadrao);
        std::string c = pa.criarPastaAcervo("C", p, mapaPadrao);
        std::string s = pa.criarPastaAcervo("S", g, mapaPadrao);
        pa.atualizarPosicaoPastaAcervo(g, 10, 10);  // (0,0) é tratado como "sem posição" (auto-arranja) em recalcularNodes()
        pa.atualizarPosicaoPastaAcervo(p, 100, 100);
        pa.atualizarPosicaoPastaAcervo(c, 200, 200);
        pa.atualizarPosicaoPastaAcervo(s, 300, 0);

        pa.definirMapaAtivo(mapaPadrao);  // o componente abre no último mapa usado (persistido)
        ArvoreBackupComponent arvore(pa);
        auto indiceDe = [&](const std::string& id) -> int {
            for (size_t i = 0; i < arvore.nodes_.size(); ++i) if (arvore.nodes_[i].id == id) return static_cast<int>(i);
            return -1;
        };
        int idxG = indiceDe(g), idxP = indiceDe(p), idxC = indiceDe(c), idxS = indiceDe(s);
        checar(idxG >= 0 && idxP >= 0 && idxC >= 0 && idxS >= 0, "setup: G/P/C/S nodes all found on the canvas");

        std::set<int> desc;
        arvore.coletarDescendentesIndices(idxP, desc);
        bool soC = desc.size() == 1 && desc.count(idxC) == 1;
        checar(soC, "descendants of P are only C (P's child) — not S (a sibling) or P/G themselves (" + juce::String((int) desc.size()) + ")");

        std::set<int> descG;
        arvore.coletarDescendentesIndices(idxG, descG);
        checar(descG.size() == 3 && descG.count(idxP) == 1 && descG.count(idxC) == 1 && descG.count(idxS) == 1,
               "descendants of G are P, C (grandchild) and S (G's other direct child) — the whole subtree (" +
                   juce::String((int) descG.size()) + ")");

        // Simula o resultado de um arrasto: P e sua descendente C recebem o
        // MESMO delta (mouseDrag já aplica isto); S e G ficam parados.
        auto posAntesS = arvore.nodes_[static_cast<size_t>(idxS)].bounds.getPosition();
        auto posAntesG = arvore.nodes_[static_cast<size_t>(idxG)].bounds.getPosition();
        juce::Point<int> delta(150, 80);
        arvore.nodes_[static_cast<size_t>(idxP)].bounds.setPosition(arvore.nodes_[static_cast<size_t>(idxP)].bounds.getPosition() + delta);
        arvore.nodes_[static_cast<size_t>(idxC)].bounds.setPosition(arvore.nodes_[static_cast<size_t>(idxC)].bounds.getPosition() + delta);
        auto posDepoisP = arvore.nodes_[static_cast<size_t>(idxP)].bounds.getPosition();
        auto posDepoisC = arvore.nodes_[static_cast<size_t>(idxC)].bounds.getPosition();

        arvore.persistirPosicoesEmLote({idxP, idxC});

        auto lerPos = [&](const std::string& id) {
            auto stmt = pa.projeto().registro().prepare("SELECT posicao_x, posicao_y FROM acervo_pasta WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(id));
            stmt.step();
            return juce::Point<int>(stmt.columnInt(0), stmt.columnInt(1));
        };
        checar(lerPos(p) == posDepoisP, "P's new position was persisted (single batched transaction)");
        checar(lerPos(c) == posDepoisC, "C moved together with its parent P (same delta)");
        checar(lerPos(s) == posAntesS, "S (a sibling, not part of the dragged block) keeps its original position");
        checar(lerPos(g) == posAntesG, "G (the ancestor, not selected/dragged) keeps its original position");
    } catch (const std::exception& e) {
        checar(false, juce::String("folder drag block selftest: ") + e.what());
    }
    raizBloco.deleteRecursively();

    // ------------------------- Folder Map: atalhos C e D (item 9, 2026-09-29)
    std::cout << "\n-- Folder Map: C (Folder Color) / D (Disconnect) shortcuts --\n";
    juce::File raizCD = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("matriz_folder_cd_selftest_" + juce::Uuid().toDashedString());
    try {
        raizCD.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "AtalhosCD";
        params.prefixoNomenclatura = "ACD";
        auto projeto = matriz::model::Project::criar(raizCD.getChildFile("MAIN"), params);
        ProjetoAberto pa(std::move(projeto));
        std::string mapaPadrao = pa.mapaAtivoPadrao();
        std::string root = pa.criarPastaAcervo("Root", std::nullopt, mapaPadrao);
        std::string filha = pa.criarPastaAcervo("Filha", root, mapaPadrao);

        pa.definirMapaAtivo(mapaPadrao);  // o componente abre no último mapa usado (persistido)
        ArvoreBackupComponent arvore(pa);
        auto& reg = pa.projeto().registro();
        auto lerCor = [&](const std::string& id) -> juce::String {
            auto stmt = reg.prepare("SELECT cor_customizada FROM acervo_pasta WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(id));
            if (stmt.step() && !stmt.columnIsNull(0)) return juce::String(stmt.columnText(0));
            return {};
        };
        auto lerPai = [&](const std::string& id) -> std::string {
            auto stmt = reg.prepare("SELECT pasta_pai_id FROM acervo_pasta WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(id));
            if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
            return {};
        };

        // D desconecta a(s) selecionada(s) — mesma rotina do item 7, agora
        // pelo atalho de teclado.
        for (auto& n : arvore.nodes_) if (n.id == filha) n.selecionado = true;
        checar(arvore.keyPressed(juce::KeyPress('D', juce::ModifierKeys(), (juce::juce_wchar) 'd')),
               "D is handled (returns true) with a folder selected");
        checar(lerPai(filha).empty(), "D disconnected the selected folder from its parent");

        // C abre o color picker pra seleção; C de novo fecha em vez de reabrir.
        for (auto& n : arvore.nodes_) if (n.id == filha) n.selecionado = true;
        checar(arvore.corCallout_ == nullptr, "setup: no color picker open yet");
        checar(arvore.keyPressed(juce::KeyPress('C', juce::ModifierKeys(), (juce::juce_wchar) 'c')),
               "C is handled (returns true) with a folder selected");
        checar(arvore.corCallout_ != nullptr, "C opened the folder color picker");
        checar(arvore.keyPressed(juce::KeyPress('C', juce::ModifierKeys(), (juce::juce_wchar) 'c')),
               "C again is handled (returns true) while the picker is open");
        checar(arvore.corCallout_ == nullptr, "C again CLOSED the picker instead of reopening it");

        // ORIGINAL: C e D desabilitados (a trava que faltava em
        // mostrarSeletorDeCorPasta/aplicarCorAPastas, ver item 9).
        arvore.mapaAtivoId_ = ProjetoAberto::kMapaOriginal;
        for (auto& n : arvore.nodes_) if (n.id == filha) n.selecionado = true;
        juce::String corAntes = lerCor(filha);
        checar(!arvore.keyPressed(juce::KeyPress('C', juce::ModifierKeys(), (juce::juce_wchar) 'c')),
               "C on the read-only ORIGINAL map is not handled (returns false)");
        checar(arvore.corCallout_ == nullptr, "C on ORIGINAL never opens the picker");
        checar(!arvore.keyPressed(juce::KeyPress('D', juce::ModifierKeys(), (juce::juce_wchar) 'd')),
               "D on the read-only ORIGINAL map is not handled (returns false)");
        // aplicarCorAPastas() direto (defesa em profundidade) também trava.
        arvore.aplicarCorAPastas({filha}, juce::Colours::red);
        checar(lerCor(filha) == corAntes, "aplicarCorAPastas() itself refuses to write on ORIGINAL, even called directly");
    } catch (const std::exception& e) {
        checar(false, juce::String("folder C/D shortcuts selftest: ") + e.what());
    }
    raizCD.deleteRecursively();

    // ------------------------- METADATA: GEO LOCATION em lote (item 2, 2026-09-28)
    // Valor comum só quando 100% dos selecionados concordam; divergente fica
    // vazio com indicador "mixed" e NÃO pode ser gravado sem edição real;
    // editar um subcampo não pode apagar os outros por item; leitura+mescla+
    // escrita da seleção inteira numa transação só, mesmo com 1000+ itens.
    std::cout << "\n-- METADATA: GEO LOCATION common values / partial write / large selection --\n";
    juce::File raizGeoLote = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("matriz_geo_selftest_" + juce::Uuid().toDashedString());
    try {
        raizGeoLote.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Geo";
        params.prefixoNomenclatura = "GEO";
        auto projeto = matriz::model::Project::criar(raizGeoLote.getChildFile("projeto"), params);
        const std::string projetoId = projeto->projetoId();
        std::vector<std::string> ids;
        for (int i = 0; i < 4; ++i) ids.push_back(inserirItem(projeto->registro(), projetoId, "GEO-" + std::to_string(i), false));
        auto& reg = projeto->registro();
        auto gravarGeoDireto = [&](const std::string& id, const std::string& city, const std::string& state, const std::string& country) {
            reg.run("INSERT INTO asset_geolocation (asset_id, city, state_province, country, source, created_at, updated_at) "
                    "VALUES (?, ?, ?, ?, 'USER_CITY', datetime('now'), datetime('now'))",
                    {matriz::db::Value::of(id), matriz::db::Value::of(city), matriz::db::Value::of(state), matriz::db::Value::of(country)});
        };
        gravarGeoDireto(ids[0], "Rio de Janeiro", "RJ", "Brazil");
        gravarGeoDireto(ids[1], "Sao Paulo", "SP", "Brazil");
        gravarGeoDireto(ids[2], "Salvador", "BA", "Brazil");
        // ids[3]: só COUNTRY (sem city/state) — os 4 concordam em COUNTRY
        // (testa "valor comum"), mas CITY/STATE continuam divergentes entre
        // eles (testa "mixed"), e o merge parte de uma linha existente
        // incompleta em vez de nenhuma linha.
        reg.run("INSERT INTO asset_geolocation (asset_id, country, source, created_at, updated_at) "
                "VALUES (?, 'Brazil', 'USER_COUNTRY', datetime('now'), datetime('now'))",
                {matriz::db::Value::of(ids[3])});

        auto janela = std::make_unique<MainComponent>();
        janela->setBounds(0, 0, 1400, 900);
        janela->abrirProjeto(std::move(projeto));
        bombear(200);
        janela->mostrarGrid();
        auto* cw = janela->catalogWorkspace_.get();
        auto* mosaico = cw->mosaico_.get();
        esperarAte([&] { return !mosaico->snapshotPendente() && mosaico->totalItensCarregados() >= (int) ids.size(); });

        std::set<std::string> sel(ids.begin(), ids.end());
        mosaico->definirSelecao(sel);
        cw->selecionarItem({});  // mesmo truque do Select All: definirSelecao() só dispara aoMudarSelecao
        bombear(100);
        auto* ficha = cw->fichaPanel_.get();

        auto* edCountry = ficha ? dynamic_cast<juce::TextEditor*>(ficha->editorDoCampoLoteParaTeste("geo_country")) : nullptr;
        auto* edCity = ficha ? dynamic_cast<juce::TextEditor*>(ficha->editorDoCampoLoteParaTeste("geo_city")) : nullptr;
        auto* edState = ficha ? dynamic_cast<juce::TextEditor*>(ficha->editorDoCampoLoteParaTeste("geo_state")) : nullptr;
        checar(edCountry != nullptr && edCity != nullptr && edState != nullptr, "GEO LOCATION batch editors exist for a mixed selection");

        auto cidadeDe = [&](const std::string& id) {
            auto g = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(reg, id);
            return g && g->city ? *g->city : std::string();
        };
        auto estadoDe = [&](const std::string& id) {
            auto g = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(reg, id);
            return g && g->stateProvince ? *g->stateProvince : std::string();
        };
        auto paisDe = [&](const std::string& id) {
            auto g = matriz::analytics::AssetGeolocationRepository::obterPorAssetId(reg, id);
            return g && g->country ? *g->country : std::string();
        };

        if (edCountry && edCity && edState) {
            checar(edCountry->getText() == "Brazil", "COUNTRY shows the common value when all selected items agree (" + edCountry->getText() + ")");
            checar(edCity->getText().isEmpty(), "CITY is blank when the selected items disagree (" + edCity->getText() + ")");
            checar(edCity->getTextToShowWhenEmpty() == matriz::i18n::t("ficha.lote_valores_multiplos"),
                   "CITY shows the \"different values\" indicator instead of a plain placeholder");

            // Enter sem digitar nada num campo "mixed": nada pode ser gravado.
            if (edCity->onReturnKey) edCity->onReturnKey();
            bombear(200);
            checar(cidadeDe(ids[0]) == "Rio de Janeiro" && cidadeDe(ids[1]) == "Sao Paulo" && cidadeDe(ids[2]) == "Salvador",
                   "committing a mixed CITY field with no real edit doesn't wipe any item's own value");

            // Edita só CITY: aplica a todos; COUNTRY/STATE (não tocados) sobrevivem por item.
            edCity->setText("Curitiba", false);
            if (edCity->onReturnKey) edCity->onReturnKey();
            bombear(200);
            checar(cidadeDe(ids[0]) == "Curitiba" && cidadeDe(ids[1]) == "Curitiba" &&
                       cidadeDe(ids[2]) == "Curitiba" && cidadeDe(ids[3]) == "Curitiba",
                   "editing only CITY applies the new value to every selected item, including one that had no CITY before (only COUNTRY)");
            checar(estadoDe(ids[0]) == "RJ" && estadoDe(ids[1]) == "SP" && estadoDe(ids[2]) == "BA" && estadoDe(ids[3]).empty(),
                   "editing CITY alone leaves each item's own STATE untouched");
            checar(paisDe(ids[0]) == "Brazil" && paisDe(ids[1]) == "Brazil" && paisDe(ids[2]) == "Brazil" && paisDe(ids[3]) == "Brazil",
                   "editing CITY alone leaves COUNTRY untouched per item, including the one whose only prior data was COUNTRY");

            // Auditoria (2026-09-29): confirma que remover aoAplicarSucessoItem
            // do caminho de geo (item 2) não perdeu nada — Database::run()
            // já marca o registro sujo pra QUALQUER escrita (inclusive a
            // UPSERT crua de geo), independente daquele callback; nenhum
            // campo em lote (Dublin Core ou geo) grava em log.md; e nada em
            // ItemResumo reflete geo location, então não há atualização
            // visual do grid pra perder.
            auto& projLive = janela->projetoAberto()->projeto();
            projLive.registro().limparSujo();  // zera pra medir só a próxima edição
            juce::File logFile = projLive.pasta().getChildFile("log.md");
            juce::String logAntes = logFile.existsAsFile() ? logFile.loadFileAsString() : juce::String();
            int versaoSnapshotAntes = mosaico->versaoSnapshot();

            edCity->setText("Belo Horizonte", false);
            if (edCity->onReturnKey) edCity->onReturnKey();
            bombear(200);

            checar(projLive.registro().estaSujo(),
                   "a geo batch edit marks the registro dirty, same as any other write (revision bump feeds off this)");
            int64_t revAntes = projLive.revisao();
            projLive.confirmarRevisao();
            checar(projLive.revisao() == revAntes + 1,
                   "confirmarRevisao() (called by the clone's auto-sync, not by individual edits) bumps after a geo batch edit exactly like it would after any other edit");

            juce::String logDepois = logFile.existsAsFile() ? logFile.loadFileAsString() : juce::String();
            checar(logDepois == logAntes,
                   "geo batch edits don't write to log.md — neither do Dublin Core batch edits (salvarMetadado doesn't log either), so this is consistent, not a loss");

            checar(mosaico->versaoSnapshot() == versaoSnapshotAntes,
                   "no full grid reload/version bump happens for a geo-only edit — nothing in ItemResumo reflects geo location, so there's no applicable visual update to perform");
        }

        // Performance (ajuste do pedido: seleção grande não pode travar a
        // interface): 1200 itens sem geolocalização prévia, uma edição de
        // CITY em lote precisa ler+mesclar+gravar todos numa transação só.
        constexpr int kGrande = 1200;
        std::vector<std::string> idsGrande;
        idsGrande.reserve(kGrande);
        reg.exec("BEGIN TRANSACTION");
        for (int i = 0; i < kGrande; ++i) idsGrande.push_back(inserirItem(reg, projetoId, "GEO-BIG-" + std::to_string(i), false));
        reg.exec("COMMIT");

        cw->recarregar();
        esperarAte([&] { return !mosaico->snapshotPendente() && mosaico->totalItensCarregados() >= kGrande; }, 20000);
        std::set<std::string> selGrande(idsGrande.begin(), idsGrande.end());
        mosaico->definirSelecao(selGrande);
        // Construir o card em lote pra 1200 itens também mexe nos campos
        // Dublin Core (cada um com sua própria varredura O(N) preexistente,
        // fora do escopo deste item) — não cronometrado aqui de propósito;
        // o pedido do usuário é sobre a GRAVAÇÃO de GEO LOCATION, não sobre
        // abrir a ficha.
        cw->selecionarItem({});
        auto* fichaG = cw->fichaPanel_.get();
        auto* edCityG = fichaG ? dynamic_cast<juce::TextEditor*>(fichaG->editorDoCampoLoteParaTeste("geo_city")) : nullptr;
        checar(edCityG != nullptr, "GEO LOCATION batch editor exists for a " + juce::String(kGrande) + "-item selection");
        if (edCityG) {
            edCityG->setText("Manaus", false);
            auto t2 = juce::Time::getMillisecondCounter();
            if (edCityG->onReturnKey) edCityG->onReturnKey();
            auto t3 = juce::Time::getMillisecondCounter();
            bombear(200);
            double msAplicar = static_cast<double>(t3 - t2);
            checar(msAplicar < 3000.0, "applying GEO LOCATION to " + juce::String(kGrande) + " items in one transaction stays under 3s (" + juce::String(msAplicar, 1) + " ms)");
            auto st = reg.prepare("SELECT COUNT(*) FROM asset_geolocation WHERE city = ?");
            st.bind(1, matriz::db::Value::of(std::string("Manaus")));
            st.step();
            int cGrande = st.columnInt(0);
            checar(cGrande == kGrande, "GEO LOCATION written to all " + juce::String(kGrande) + " items in the large selection (" + juce::String(cGrande) + ")");
        }

        janela.reset();
    } catch (const std::exception& e) {
        checar(false, juce::String("geo location selftest: ") + e.what());
    }
    raizGeoLote.deleteRecursively();

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
        // Fase 4 (pacote de collection): o valor divergente vai pro item_historico
        // (recuperável); nas notas fica só uma linha curta apontando pra ele.
        checar(juce::String(notasK).startsWith("nota do mantido") && juce::String(notasK).contains("1 merge conflict")
                   && juce::String(notasK).contains("nota do descartado") && juce::String(notasK).contains("DUP-DROP")
                   && juce::String(notasK).contains("originais/DUP-DROP.wav"),
               "kept notes: appended (not overwritten) with a conflict line, duplicate notes, name and location");
        checar(texto("SELECT valor_anterior FROM item_historico WHERE item_id = ? AND campo_id = 'dc_creator'", manter) ==
                   "Creator Drop",
               "the differing value (Creator Drop) is in the kept item's history");
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
        // Fase 6: os 4 botões de saída viraram um EXPORT + dropdown; o antigo segue existindo (escondido)
        // e é o que a opção "Selected Files" dispara.
        checar(bw.btnExportUnificado_ && bw.btnExportUnificado_->isVisible() && bw.comboExportOrigem_ &&
                   bw.comboExportOrigem_->isVisible() && bw.btnExportar_,
               "EXPORT button + source dropdown are on the backup screen");
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
            // Fase 4: a resolução roda em background; espera o fim.
            esperarAte([&] { return dwLote.gruposDetectados_.empty(); });
        }
        EventBus::obterInstancia().removerListener(&contador);
        checar(contador.recarga == 1 && contador.porItem == 0,
               "Validate All on 15 pairs: ONE grid reload event, not one per item (" + juce::String(contador.porItem) + ")");

        DuplicatesWorkspaceComponent dw(pa);
        dw.gruposDetectados_ = {par(a1, a2), par(b1, b2)};
        // Card de duplicata com miniatura grande (conferir a olho):
        // test-output/duplicatas_card.png.
        if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
            for (auto& g : dw.gruposDetectados_) {
                for (auto* m : {&g.original, &g.duplicata}) {
                    m->titulo = "Entrevista Dona Maria - fita 3";
                    m->codigoAcervo = "CRT-000123";
                    m->ext = "wav";
                    m->duracao = 754.0;
                    m->lufs = -18.4;
                    m->tamanhoBytes = 133 * 1024 * 1024;
                    m->fullPath = "/Volumes/ACERVO/Entrevistas/1987/Dona Maria/fita 3/Entrevista Dona Maria - fita 3.wav";
                }
                g.duplicata.tamanhoCoincide = g.original.tamanhoCoincide = true;
            }
            dw.setSize(1300, 520);
            dw.estado_ = DuplicatesWorkspaceComponent::State::Results;
            dw.viewport_->setVisible(true);
            dw.atualizarListaEStatusAposResolucao();
            bombear(100);
            juce::PNGImageFormat png;
            auto arq = dir.getChildFile("duplicatas_card.png");
            arq.deleteFile();
            if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                png.writeImageToStream(dw.createComponentSnapshot(dw.getLocalBounds()), *out);
        }
        dw.aplicarEscolhaGlobal(5);  // manter a primeira no backup
        esperarAte([&] { return dw.gruposDetectados_.size() == 1; });
        checar(estado(a2) == "duplicata" && estado(a1) != "duplicata", "first in backup: pair A keeps file 1");
        checar(dw.gruposDetectados_.size() == 1 && dw.gruposDetectados_.front().original.itemId == b1 &&
                   estado(b1) != "duplicata" && estado(b2) != "duplicata",
               "no backup on either side: pair B stays for a manual decision (flagged)");
        dw.aplicarEscolhaGlobal(4);  // manter a ingestão mais recente
        esperarAte([&] { return dw.gruposDetectados_.empty(); });
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
    std::cout << "\n-- registro.sqlite: no binary EXIF, no thumbnail blobs, compaction --\n";
    {
        using matriz::db::Value;
        const std::string numeros = [] { std::string x; for (int i = 0; i < 400; ++i) x += std::to_string(i % 256) + " "; return x; }();
        checar(matriz::ingest::ehExifBinarioVolumoso("Exif.Photo.MakerNote", "37 0 1") &&
                   matriz::ingest::ehExifBinarioVolumoso("Exif.Canon.ColorData", numeros) &&
                   !matriz::ingest::ehExifBinarioVolumoso("Exif.Photo.FNumber", "5/1") &&
                   !matriz::ingest::ehExifBinarioVolumoso("Exif.Image.ImageDescription", std::string(400, 'a')),
               "binary EXIF rule: MakerNote and long number dumps out; real text and short values stay");

        const std::string outra = std::string(matriz::model::kOutraMetadataTitulo);
        const std::string notasMinhas = "[NOTES]\nMinha nota: 12 34\nMakerNote: escrito por mim";
        const std::string notas = "[" + outra + "]\nFNumber: 5/1\nMakerNote: 37 0 1\nColorData: " + numeros +
                                  "\nISOSpeedRatings: 3200\n\n" + notasMinhas;
        bool mudou = false;
        const auto limpas = matriz::model::removerOutraMetadataDasNotas(notas, &mudou);
        bool mudouMeio = false;
        const auto meio = matriz::model::removerOutraMetadataDasNotas(
            "[A]\nx\n\n[" + outra + "]\nFNumber: 5/1\n\n[B]\ny", &mudouMeio);
        bool mudouSo = true;
        const auto so = matriz::model::removerOutraMetadataDasNotas("[" + outra + "]\nFNumber: 5/1", &mudouSo);
        checar(mudou && limpas == notasMinhas && mudouMeio && meio == "[A]\nx\n\n[B]\ny" && mudouSo && so.empty(),
               "notes: the automatic [OTHER METADATA] section goes; the user's own sections stay byte-identical");

        // Ingest: no banco só as chaves da ficha; GET EXIF traz o resto (sem binário).
        {
            juce::File dirJ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("matriz_exif_" + juce::Uuid().toDashedString());
            dirJ.createDirectory();
            juce::File jpg = dirJ.getChildFile("foto.jpg");
            {
                juce::Image img(juce::Image::RGB, 64, 48, true);
                juce::JPEGImageFormat fmt;
                if (auto out = std::unique_ptr<juce::FileOutputStream>(jpg.createOutputStream()))
                    fmt.writeImageToStream(img, *out);
            }
            try {
                auto image = Exiv2::ImageFactory::open(jpg.getFullPathName().toStdString());
                image->readMetadata();
                Exiv2::ExifData ed;
                ed["Exif.Image.Make"] = "Canon";
                ed["Exif.Image.Model"] = "Canon EOS 5D Mark II";
                ed["Exif.Photo.FNumber"] = Exiv2::Rational(5, 1);
                ed["Exif.Photo.DateTimeOriginal"] = "2011:04:09 15:32:23";
                Exiv2::DataValue maker(Exiv2::undefined);
                maker.read(numeros);
                ed.add(Exiv2::ExifKey("Exif.Photo.MakerNote"), &maker);
                image->setExifData(ed);
                image->writeMetadata();
            } catch (const std::exception& e) {
                checar(false, juce::String("exiv2 write: ") + e.what());
            }
            try {
                auto lt = matriz::ingest::lerTecnica(jpg);
                auto json = juce::String(matriz::ingest::paraJson(lt));
                checar(json.contains("Exif.Image.Make") && json.contains("Exif.Photo.DateTimeOriginal") &&
                           !json.contains("FNumber") && !json.contains("MakerNote") && !lt.metaUnmappedExtras,
                       "ingest keeps only the ficha's EXIF keys in the DB, no notes dump");
            } catch (const std::exception& e) {
                checar(false, juce::String("lerTecnica: ") + e.what());
            }
            auto completo = matriz::ingest::lerExifCompletoParaNotas(jpg);
            checar(completo && juce::String(*completo).contains("FNumber: 5/1") && !juce::String(*completo).contains("MakerNote") &&
                       !juce::String(*completo).contains("Make:"),
                   "GET EXIF reads the full EXIF (FNumber), without binary or keys already in the ficha");
            dirJ.deleteRecursively();
        }

        juce::File raizC = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("matriz_compactar_" + juce::Uuid().toDashedString());
        try {
            raizC.createDirectory();
            matriz::model::NovoProjetoParams params;
            params.nome = "Compactar";
            params.prefixoNomenclatura = "CMP";
            auto projeto = matriz::model::Project::criar(raizC.getChildFile("projeto"), params);
            const juce::File pastaProject = projeto->pasta();
            auto& reg = projeto->registro();
            const std::string itemId = inserirItem(reg, projeto->projetoId(), "CMP-1", false, ".jpg");
            reg.run("UPDATE item SET notas_livres = ? WHERE id = ?", {Value::of(notas), Value::of(itemId)});
            // 200 tags a mais: a 1ª versão (json_remove com uma chave por
            // argumento) estourava o limite de argumentos do SQLite no
            // projeto real.
            std::string muitas;
            for (int t = 0; t < 200; ++t) muitas += "\"Exif.Canon.Tag" + std::to_string(t) + "\": \"" + std::to_string(t) + "\", ";
            const std::string json = "{\"codec\": \"mjpeg\", \"bruto\": {\"exif\": {" + muitas + "\"Exif.Image.Orientation\": \"6\", \"Exif.Photo.FNumber\": \"5/1\", "
                                     "\"Exif.Photo.MakerNote\": \"37 0 1\", \"Exif.Canon.ColorData\": \"" + numeros + "\"}}}";
            reg.run("UPDATE arquivo SET caracteristicas_tecnicas_json = ? WHERE item_id = ?", {Value::of(json), Value::of(itemId)});
            std::string arquivoId;
            { auto st = reg.prepare("SELECT id FROM arquivo WHERE item_id = ?"); st.bind(1, Value::of(itemId)); st.step(); arquivoId = st.columnText(0); }
            reg.run("INSERT INTO cache_arquivo (arquivo_id, miniatura, lufs_i, calculado_em, versao_analise) VALUES (?, ?, -18.5, 'x', 1)",
                    {Value::of(arquivoId), Value::ofBlob(std::vector<uint8_t>(40000, 0xAB))});
            projeto.reset();  // projeto fechado (como exige a compactação)

            auto r = matriz::model::compactarRegistro(pastaProject);
            checar(r.ok && r.miniaturasRemovidas == 1 && r.jsonsLimpos == 1 && r.notasLimpas == 1 && r.copiaOriginal.existsAsFile(),
                   "compaction ran: 1 blob, 1 json, 1 note cleaned; original kept aside (" + juce::String(r.erro) + ")");

            matriz::db::Database db(pastaProject.getChildFile("registro.sqlite").getFullPathName().toStdString());
            auto st = db.prepare("SELECT i.notas_livres, a.caracteristicas_tecnicas_json, c.miniatura IS NULL, c.lufs_i "
                                 "FROM item i JOIN arquivo a ON a.item_id = i.id JOIN cache_arquivo c ON c.arquivo_id = a.id WHERE i.id = ?");
            st.bind(1, Value::of(itemId));
            const bool achou = st.step();
            const std::string jsonDepois = achou ? st.columnText(1) : "";
            checar(achou && st.columnText(0) == limpas && st.columnInt(2) == 1 && std::abs(st.columnReal(3) + 18.5) < 1e-9,
                   "after compaction: notes cleaned, thumbnail blob gone, loudness kept");
            checar(jsonDepois.find("MakerNote") == std::string::npos && jsonDepois.find("ColorData") == std::string::npos &&
                       jsonDepois.find("FNumber") == std::string::npos && jsonDepois.find("Exif.Canon.Tag") == std::string::npos &&
                       jsonDepois.find("Exif.Image.Orientation") != std::string::npos && jsonDepois.find("mjpeg") != std::string::npos,
                   "after compaction: technical JSON keeps the ficha keys (Orientation) and codec, drops the rest");
        } catch (const std::exception& e) {
            checar(false, juce::String("compaction selftest: ") + e.what());
        }
        raizC.deleteRecursively();
    }

    std::cout << "\n-- PDF page count: no infinite loop --\n";
    {
        juce::File dirP = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_pdf_" + juce::Uuid().toDashedString());
        dirP.createDirectory();
        auto so = dirP.getChildFile("so_sem_espaco.pdf");
        so.replaceWithText("%PDF-1.4\n1 0 obj <</Type/Pages /Kids [2 0 R 3 0 R]>>\n2 0 obj <</Type/Page>>\n3 0 obj <</Type/Page>>\n/Title (Teste)\n");
        auto mix = dirP.getChildFile("misto.pdf");
        mix.replaceWithText("%PDF-1.4\n<</Type /Pages>>\n<</Type /Page>>\n<</Type/Page>>\n<</Type /Page>>\n");
        auto contar = [](const juce::File& f) {
            auto fut = std::async(std::launch::async, [f] { return matriz::ingest::lerTecnica(f).pageCount; });
            if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) return -99;  // travou
            auto n = fut.get();
            return n ? static_cast<int>(*n) : 0;
        };
        const int nSo = contar(so), nMix = contar(mix);
        checar(nSo == 2, "PDF with only \"/Type/Page\": 2 pages, no infinite loop (" + juce::String(nSo) + ")");
        checar(nMix == 3, "PDF with both forms: 3 pages, \"/Type /Pages\" not counted (" + juce::String(nMix) + ")");
        if (nSo != -99 && nMix != -99) dirP.deleteRecursively();
    }

    std::cout << "\n-- Video preview: audio of a .mov (waveform + sound) --\n";
    {
        auto mov = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("tools/fixtures/video_aac.mov");
        juce::AudioFormatManager basico;
        basico.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> semQt(basico.createReaderFor(mov));
        checar(mov.existsAsFile() && semQt == nullptr,
               "fixture exists and plain JUCE formats cannot read a .mov (the bug)");
        juce::AudioFormatManager gm;
        matriz::audio::registrarFormatosDeAudio(gm);
        std::unique_ptr<juce::AudioFormatReader> leitor(gm.createReaderFor(mov));
        checar(leitor != nullptr, ".mov audio opens with the QuickTime reader (timeline)");
        std::unique_ptr<juce::AudioFormatReader> leitorStream(
            gm.createReaderFor(std::unique_ptr<juce::InputStream>(mov.createInputStream().release())));
        checar(leitorStream != nullptr, ".mov audio opens from a stream too (waveform / AudioThumbnail)");
        if (leitor) {
            const double dur = static_cast<double>(leitor->lengthInSamples) / leitor->sampleRate;
            juce::AudioBuffer<float> buf(static_cast<int>(leitor->numChannels), 4800);
            leitor->read(&buf, 0, 4800, 24000, true, true);  // do meio do arquivo
            checar(leitor->numChannels == 2 && std::abs(dur - 1.0) < 0.1 && buf.getMagnitude(0, 4800) > 0.05f,  // senoide do ffmpeg: amplitude 1/8
                   ".mov: 2 channels, ~1 s, real signal (dur " + juce::String(dur, 2) + ", peak " +
                       juce::String(buf.getMagnitude(0, 4800), 2) + ")");
        }
    }

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
        // Pacote de collection (Fase 2): opção nova no dropdown, as de antes seguem lá.
        auto& combo = *bw.comboExportOrigem_;
        checar(combo.getNumItems() == 6 && combo.getItemText(5).startsWith(matriz::i18n::t("export.pacote_titulo")) &&
                   combo.getItemText(0).startsWith("Selected Files"),
               "EXPORT dropdown: the 5 options as before + Collection Package");
        combo.setSelectedId(BackupWorkspaceComponent::kExpPacote, juce::sendNotificationSync);
        checar(!bw.btnExportUnificado_->isEnabled(), "no MAIN: Collection Package locked too");
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
        bw.atualizarExportUnificado();
        checar(bw.btnExportUnificado_->isEnabled() == (bw.contagemSelecionados_ > 0),
               "MAIN created: Collection Package follows the selection like Selected Files");
        combo.setSelectedId(BackupWorkspaceComponent::kExpSelecionados, juce::sendNotificationSync);
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

    // ------------------------------- Fase 1: Folder Maps múltiplos
    std::cout << "\n-- Phase 1: multiple independent folder maps per project --\n";
    juce::File raizMapas = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("matriz_folder_maps_selftest_" + juce::Uuid().toDashedString());
    try {
        matriz::model::NovoProjetoParams params;
        params.nome = "Mapas";
        params.prefixoNomenclatura = "MAP";
        auto projeto = matriz::model::Project::criar(raizMapas.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        using matriz::db::Value;

        const std::string itemA = inserirItem(reg, projetoId, "MAP-1", false);
        const std::string itemB = inserirItem(reg, projetoId, "MAP-2", false);

        std::string mapaPadrao = pa.mapaAtivoPadrao();
        checar(!mapaPadrao.empty(), "a fresh project already has a default user folder map");

        auto contemDireto = [](const ProjetoAberto::NoArvore& raiz, const std::string& pastaId, const std::string& itemId) {
            for (auto& f : raiz.filhos) if (f.id == pastaId) return f.itemIdsDiretos.count(itemId) > 0;
            return false;
        };
        auto achaPastaPorNome = [](const ProjetoAberto::NoArvore& raiz, const juce::String& nome) {
            for (auto& f : raiz.filhos) if (f.nome == nome) return true;
            return false;
        };
        auto checarSincroniaMapaId = [&](const juce::String& contexto) {
            auto st = reg.prepare(
                "SELECT COUNT(*) FROM acervo_item_pasta aip JOIN acervo_pasta ap ON ap.id = aip.pasta_id "
                "WHERE aip.mapa_id IS NOT ap.mapa_id");
            st.step();
            checar(st.columnInt(0) == 0, "acervo_item_pasta.mapa_id matches its folder's mapa_id — " + contexto);
        };

        // --- isolamento entre mapas ---
        std::string mapaB = pa.criarFolderMap("Mapa B", std::nullopt);
        checar(!mapaB.empty() && mapaB != mapaPadrao, "criarFolderMap creates an independent second map");

        std::string pastaA1 = pa.criarPastaAcervo("Pasta A1", std::nullopt, mapaPadrao);
        std::string pastaB1 = pa.criarPastaAcervo("Pasta B1", std::nullopt, mapaB);
        pa.adicionarItensAPasta({itemA}, pastaA1);
        pa.adicionarItensAPasta({itemA}, pastaB1);

        checar(contemDireto(pa.arvoreAcervo(mapaPadrao), pastaA1, itemA) &&
                   contemDireto(pa.arvoreAcervo(mapaB), pastaB1, itemA),
               "the same item sits in different folders on independent maps at the same time");
        checar(pa.itensSemPasta(mapaPadrao).count(itemB) == 1 && pa.itensSemPasta(mapaB).count(itemB) == 1,
               "an item unassigned in both maps counts as SEM PASTA in both");
        checar(pa.contarItensSemPasta(mapaPadrao) == 1 && pa.contarItensSemPasta(mapaB) == 1,
               "SEM PASTA count matches per map");

        std::string pastaA2 = pa.criarPastaAcervo("Pasta A2", std::nullopt, mapaPadrao);
        checar(pa.moverPastaAcervo(pastaA1, pastaA2), "moving a folder within its own map works");
        checar(contemDireto(pa.arvoreAcervo(mapaB), pastaB1, itemA),
               "moving a folder in one map leaves the other map's placement untouched");

        // Arrastar pra SEM PASTA (removerItensDoBackup escopado por mapa) só
        // afeta o mapa dado.
        pa.removerItensDoBackup({itemA}, mapaPadrao);
        checar(pa.itensSemPasta(mapaPadrao).count(itemA) == 1 && contemDireto(pa.arvoreAcervo(mapaB), pastaB1, itemA),
               "removing an item to SEM PASTA in one map leaves it untouched in the other map");

        // --- ORIGINAL somente leitura ---
        checar(pa.criarPastaAcervo("X", std::nullopt, ProjetoAberto::kMapaOriginal).empty(),
               "ORIGINAL rejects creating a folder");
        checar(pa.itensSemPasta(ProjetoAberto::kMapaOriginal).empty() &&
                   pa.contarItensSemPasta(ProjetoAberto::kMapaOriginal) == 0,
               "ORIGINAL never has a SEM PASTA count");
        auto listaMapas = pa.listarFolderMaps();
        checar(!listaMapas.empty() && listaMapas.front().id == ProjetoAberto::kMapaOriginal && listaMapas.front().original,
               "listarFolderMaps() always puts ORIGINAL first");
        checar(!pa.apagarFolderMap(ProjetoAberto::kMapaOriginal), "ORIGINAL can't be deleted");
        checar(!pa.renomearFolderMap(ProjetoAberto::kMapaOriginal, "Novo nome"), "ORIGINAL can't be renamed");

        checarSincroniaMapaId("after create/move/remove-to-SEM-PASTA");

        // --- duplicar mapa (== mecanismo por trás de "Import from file") ---
        int totalMapasAntes = static_cast<int>(pa.listarFolderMaps().size());
        std::string mapaDup = pa.criarFolderMap("Mapa A copy", mapaPadrao);
        checar(!mapaDup.empty() && mapaDup != mapaPadrao,
               "duplicating/importing a map always creates a brand-new map, never overwrites the source");
        checar(static_cast<int>(pa.listarFolderMaps().size()) == totalMapasAntes + 1,
               "the map count grows by exactly one after duplicate/import");
        checar(achaPastaPorNome(pa.arvoreAcervo(mapaDup), "Pasta A2"), "the duplicate contains the source map's folders");

        pa.renomearPastaAcervo(pastaA2, "Pasta A2 renomeada");
        checar(achaPastaPorNome(pa.arvoreAcervo(mapaDup), "Pasta A2") &&
                   !achaPastaPorNome(pa.arvoreAcervo(mapaPadrao), "Pasta A2"),
               "renaming a folder in the source map never changes the duplicate (fully independent copies)");

        checarSincroniaMapaId("after duplicate/import into a new map");

        checar(pa.apagarFolderMap(mapaDup), "a user map can be deleted");
        checarSincroniaMapaId("after deleting a map");
    } catch (const std::exception& e) {
        checar(false, juce::String("folder maps selftest: ") + e.what());
    }
    raizMapas.deleteRecursively();

    // ------------------------------- Fase 1: migração de projeto anterior à Fase 1
    std::cout << "\n-- Phase 1: migrating a pre-multi-map project preserves the old single map --\n";
    juce::File raizMig = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("matriz_folder_maps_migracao_" + juce::Uuid().toDashedString());
    try {
        matriz::model::NovoProjetoParams params;
        params.nome = "Migracao";
        params.prefixoNomenclatura = "MIG";
        auto projeto = matriz::model::Project::criar(raizMig.getChildFile("MAIN"), params);
        juce::File pastaProjeto = projeto->pasta();
        const std::string projetoId = projeto->projetoId();
        std::string pastaVelhaId, itemVelhoId;
        {
            ProjetoAberto pa(std::move(projeto));
            auto& reg = pa.projeto().registro();
            using matriz::db::Value;
            itemVelhoId = inserirItem(reg, projetoId, "MIG-1", false);
            pastaVelhaId = pa.criarPastaAcervo("Pasta Antiga", std::nullopt, pa.mapaAtivoPadrao());
            pa.adicionarItensAPasta({itemVelhoId}, pastaVelhaId);

            // Simula o estado de ANTES da Fase 1: apaga o folder_map e zera
            // mapa_id, como um banco criado por uma versão anterior do app.
            reg.run("DELETE FROM folder_map WHERE projeto_id = ?", {Value::of(projetoId)});
            reg.run("UPDATE acervo_pasta SET mapa_id = NULL WHERE projeto_id = ?", {Value::of(projetoId)});
            reg.run("UPDATE acervo_item_pasta SET mapa_id = NULL WHERE pasta_id = ?", {Value::of(pastaVelhaId)});
        } // fecha o ProjetoAberto/Project antes de reabrir

        auto reaberto = matriz::model::Project::abrir(pastaProjeto);
        checar(reaberto != nullptr, "the project reopens after simulating a pre-Phase-1 database");
        if (reaberto) {
            ProjetoAberto pa2(std::move(reaberto));
            bool achouFolderMap1 = false;
            for (auto& m : pa2.listarFolderMaps())
                if (!m.original && m.nome == juce::String("Folder Map 1")) achouFolderMap1 = true;
            checar(achouFolderMap1, "migration recreates the single legacy map as \"Folder Map 1\"");

            auto arvore = pa2.arvoreAcervo(pa2.mapaAtivoPadrao());
            bool achouPastaAntiga = false;
            for (auto& f : arvore.filhos)
                if (f.id == pastaVelhaId && f.itemIdsDiretos.count(itemVelhoId)) achouPastaAntiga = true;
            checar(achouPastaAntiga, "migration keeps the old folder and its item placement intact");
        }
    } catch (const std::exception& e) {
        checar(false, juce::String("folder maps migration selftest: ") + e.what());
    }
    raizMig.deleteRecursively();

    // ------------------------------------------------ Folder Map: auto-arranjo, tamanhos, slider, RESET, FIT
    std::cout << "\n-- Folder Map: auto-arrange, sizes by level, slider, RESET SIZES, FIT --\n";
    {
        namespace la = matriz::ui::layoutarvore;
        // O slider PARA ao encostar: alvo no centro (100,100), 100x50, pedindo 3x; um vizinho fixo à direita.
        la::AlvoEscala a;
        a.cx = 100; a.cy = 100; a.autoW = 100; a.autoH = 50; a.de = 1.0; a.para = 3.0;
        const la::CaixaAuto vizinho{200, 60, 80, 80};  // o alvo a 300% iria de -50 a 250: passaria por cima
        const double t = la::fracaoPermitidaDoCrescimento({a}, {vizinho});
        const auto parou = la::caixaDoAlvo(a, t);
        checar(t > 0.0 && t < 1.0, "growth stops before the requested size when a neighbor is in the way (" + juce::String(t, 2) + ")");
        checar(!la::caixasSeSobrepoem(parou, vizinho, 0), "and the grown card does not touch the neighbor");
        checar(la::fracaoPermitidaDoCrescimento({a}, {}) == 1.0, "with free space the requested size is reached");
        la::AlvoEscala encolhe = a;
        encolhe.de = 1.0; encolhe.para = 0.5;
        checar(la::fracaoPermitidaDoCrescimento({encolhe}, {vizinho}) == 1.0, "shrinking is never blocked");
    }
    juce::File raizFM = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("matriz_foldermap_tamanhos_" + juce::Uuid().toDashedString());
    try {
        raizFM.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "TamanhosFM";
        params.prefixoNomenclatura = "TFM";
        auto projeto = matriz::model::Project::criar(raizFM.getChildFile("MAIN"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        const std::string mapa = pa.mapaAtivoPadrao();

        const std::string g = pa.criarPastaAcervo("G", std::nullopt, mapa);
        const std::string p = pa.criarPastaAcervo("P", g, mapa);
        const std::string c1 = pa.criarPastaAcervo("C1", p, mapa);
        const std::string c2 = pa.criarPastaAcervo("C2", p, mapa);
        const std::string sFolha = pa.criarPastaAcervo("S", g, mapa);
        const std::string r = pa.criarPastaAcervo("R", std::nullopt, mapa);
        const std::string x = pa.criarPastaAcervo("X", r, mapa);
        pa.definirMapaAtivo(mapa);

        {
            ArvoreBackupComponent arvore(pa);
            arvore.setBounds(0, 0, 1400, 900);
            bombear(100);
            auto idx = [&](const std::string& id) { for (size_t i = 0; i < arvore.nodes_.size(); ++i) if (arvore.nodes_[i].id == id) return (int) i; return -1; };
            auto caixa = [&](const std::string& id) { return arvore.nodes_[(size_t) idx(id)].bounds; };
            auto semSobreposicao = [&] {
                for (size_t i = 0; i < arvore.nodes_.size(); ++i)
                    for (size_t j = i + 1; j < arvore.nodes_.size(); ++j)
                        if (arvore.nodes_[i].bounds.intersects(arvore.nodes_[j].bounds)) return false;
                return true;
            };
            auto semCruzamento = [&] {
                struct Seg { float x1, y1, x2, y2; std::string pai; };
                std::vector<Seg> segs;
                for (const auto& n : arvore.nodes_) {
                    if (n.pastaPaiId.empty() || idx(n.pastaPaiId) < 0) continue;
                    const auto a = caixa(n.pastaPaiId);
                    const auto b = n.bounds;
                    segs.push_back({(float) a.getRight(), (float) a.getCentreY(), (float) b.getX(), (float) b.getCentreY(), n.pastaPaiId});
                }
                auto o = [](float px, float py, float qx, float qy, float rx, float ry) { return (qx - px) * (ry - py) - (qy - py) * (rx - px); };
                for (size_t i = 0; i < segs.size(); ++i)
                    for (size_t j = i + 1; j < segs.size(); ++j) {
                        if (segs[i].pai == segs[j].pai) continue;
                        const auto& A = segs[i]; const auto& B = segs[j];
                        const float d1 = o(B.x1, B.y1, B.x2, B.y2, A.x1, A.y1), d2 = o(B.x1, B.y1, B.x2, B.y2, A.x2, A.y2);
                        const float d3 = o(A.x1, A.y1, A.x2, A.y2, B.x1, B.y1), d4 = o(A.x1, A.y1, A.x2, A.y2, B.x2, B.y2);
                        if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return false;
                    }
                return true;
            };

            // --- abre já arranjado, por nível, dentro da área, sem sobreposição e sem linhas cruzadas
            checar(arvore.nodes_.size() == 7, "the map shows its 7 folders");
            checar(arvore.fatorCards_ > 0.0, "a map with no saved positions opens already arranged (card factor " + juce::String(arvore.fatorCards_, 2) + ")");
            checar(semSobreposicao(), "auto-arrange: no card overlaps another");
            checar(semCruzamento(), "auto-arrange: no connection line crosses another");
            checar(caixa(g).getWidth() > caixa(p).getWidth() && caixa(p).getWidth() > caixa(c1).getWidth(),
                   "higher levels are bigger: G " + juce::String(caixa(g).getWidth()) + " > P " + juce::String(caixa(p).getWidth()) +
                       " > C1 " + juce::String(caixa(c1).getWidth()));
            juce::Rectangle<int> uniao = arvore.nodes_.front().bounds;
            for (const auto& n : arvore.nodes_) uniao = uniao.getUnion(n.bounds);
            const auto area = arvore.areaCanvas().withZeroOrigin();
            checar(area.contains(uniao), "all the cards are inside the map area");
            checar(uniao.getWidth() > area.getWidth() * 0.8 || uniao.getHeight() > area.getHeight() * 0.8,
                   "and they use the available space (" + juce::String(uniao.getWidth()) + "x" + juce::String(uniao.getHeight()) + " of " +
                       juce::String(area.getWidth()) + "x" + juce::String(area.getHeight()) + ")");

            // --- vários selecionados: só esse conjunto
            {
                std::map<std::string, juce::Rectangle<int>> antesM;
                for (const auto& n : arvore.nodes_) antesM[n.id] = n.bounds;
                arvore.nodes_[(size_t) idx(r)].selecionado = true;
                arvore.nodes_[(size_t) idx(x)].selecionado = true;
                arvore.sliderTamanho_->setValue(150.0, juce::sendNotificationSync);
                checar(arvore.nodes_[(size_t) idx(r)].escalaManual > 1.0f && arvore.nodes_[(size_t) idx(x)].escalaManual > 1.0f,
                       "slider with several folders selected acts on that set");
                bool restoIgual = true;
                for (const auto& n : arvore.nodes_)
                    if (n.id != r && n.id != x && n.bounds != antesM[n.id]) restoIgual = false;
                checar(restoIgual && semSobreposicao(), "and only on it, with no overlap");
                arvore.nodes_[(size_t) idx(r)].selecionado = false;
                arvore.nodes_[(size_t) idx(x)].selecionado = false;
            }

            // --- slider com UMA pasta selecionada: só ela muda, e para ao encostar
            std::map<std::string, juce::Rectangle<int>> antes;
            for (const auto& n : arvore.nodes_) antes[n.id] = n.bounds;
            arvore.nodes_[(size_t) idx(p)].selecionado = true;
            arvore.sliderTamanho_->setValue(200.0, juce::sendNotificationSync);
            const float mP = arvore.nodes_[(size_t) idx(p)].escalaManual;
            checar(mP > 1.0f, "slider with a folder selected enlarges it (" + juce::String(mP, 2) + "x)");
            checar(semSobreposicao(), "and it never overlaps a neighbor, even asking for 200%");
            bool outrosIguais = true;
            for (const auto& n : arvore.nodes_)
                if (n.id != p && n.bounds != antes[n.id]) outrosIguais = false;
            checar(outrosIguais, "the other folders were not moved or resized by the slider");
            checar(std::abs(arvore.sliderTamanho_->getValue() - std::round(mP * 100.0)) < 1.5,
                   "the slider shows where the folder really stopped (" + juce::String(arvore.sliderTamanho_->getValue(), 0) + "%)");

            // --- auto-arranjo mantém o tamanho ajustado; só reorganiza posições
            arvore.nodes_[(size_t) idx(c1)].selecionado = false;
            arvore.nodes_[(size_t) idx(c2)].selecionado = false;
            const float mC1 = arvore.nodes_[(size_t) idx(x)].escalaManual;
            arvore.autoArranjar();
            checar(std::abs(arvore.nodes_[(size_t) idx(x)].escalaManual - mC1) < 0.001f && std::abs(arvore.nodes_[(size_t) idx(p)].escalaManual - mP) < 0.001f,
                   "auto-arrange again keeps the sizes the user adjusted");
            checar(semSobreposicao() && semCruzamento(), "and still has no overlap and no crossing");

            // --- escala global: todos, proporcional, mantendo a diferença entre níveis
            const float razaoAntes = (float) caixa(g).getWidth() / (float) caixa(c1).getWidth();
            arvore.sliderTamanho_->setValue(150.0, juce::sendNotificationSync);
            const float razaoDepois = (float) caixa(g).getWidth() / (float) caixa(c1).getWidth();
            checar(std::abs(razaoAntes - razaoDepois) < 0.05f, "global slider keeps the size difference between levels (" +
                                                                   juce::String(razaoAntes, 2) + " -> " + juce::String(razaoDepois, 2) + ")");
            checar(semSobreposicao(), "global slider at 150%: cards and gaps grow together, nothing overlaps");

            // --- persistência do mapa do usuário
            arvore.persistirEscalas();
            arvore.escalasSujas_ = true;
            arvore.persistirEscalas();

            // --- RESET SIZES
            arvore.redefinirTamanhos();
            bool todosUm = true, tamanhoCalculado = true;
            for (const auto& n : arvore.nodes_) {
                todosUm = todosUm && std::abs(n.escalaManual - 1.0f) < 0.001f;
                tamanhoCalculado = tamanhoCalculado && std::abs(n.boundsOriginal.getWidth() - n.autoW) <= 1;
            }
            checar(todosUm && tamanhoCalculado, "RESET SIZES returns every folder to the size calculated by auto-arrange");
            checar(std::abs(arvore.escalaTamanho_ - 1.0f) < 0.001f && arvore.sliderTamanho_->getValue() == 100.0, "and the global scale to 100%");
            checar(semSobreposicao() && caixa(g).getWidth() > caixa(p).getWidth() && caixa(p).getWidth() > caixa(c1).getWidth(),
                   "with the level hierarchy kept and no overlap");

            // --- FIT
            arvore.zoom_ = 0.2f;
            arvore.panOffset_ = {900.0f, 700.0f};
            arvore.enquadrarTudo();
            juce::Rectangle<float> vista;
            bool primeiro = true;
            for (const auto& n : arvore.nodes_) {
                const auto r2 = n.bounds.toFloat() * arvore.zoom_ + arvore.panOffset_;
                vista = primeiro ? r2 : vista.getUnion(r2);
                primeiro = false;
            }
            checar(arvore.areaCanvas().withZeroOrigin().toFloat().contains(vista), "FIT shows the whole Folder Map inside the visible area");

            if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
                arvore.nodes_[(size_t) idx(p)].selecionado = true;
                arvore.repaint();
                bombear(200);
                juce::PNGImageFormat png;
                auto arq = dir.getChildFile("foldermap_tamanhos.png");
                arq.deleteFile();
                if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                    png.writeImageToStream(arvore.createComponentSnapshot(arvore.getLocalBounds()), *out);
                arvore.nodes_[(size_t) idx(p)].selecionado = false;
            }
            arvore.nodes_[(size_t) idx(g)].selecionado = true;
            arvore.sliderTamanho_->setValue(130.0, juce::sendNotificationSync);
            arvore.nodes_[(size_t) idx(g)].selecionado = false;
        }   // o destrutor grava os tamanhos ajustados

        {   // reabrir: multiplicador de cada pasta e fator do mapa voltam do projeto
            ArvoreBackupComponent reaberta(pa);
            reaberta.setBounds(0, 0, 1400, 900);
            bombear(100);
            float mG = 0.0f;
            for (const auto& n : reaberta.nodes_) if (n.id == g) mG = n.escalaManual;
            checar(mG > 1.0f, "a folder's size survives closing and reopening the map (" + juce::String(mG, 2) + "x)");
            checar(reaberta.fatorCards_ > 0.0 && pa.fatorCardsDoMapa(mapa) > 0.0, "and so does the map's card factor");
        }

        // Mapa GRANDE (centenas de pastas): o cartão nunca encolhe abaixo do legível — o mapa passa da área e
        // o FIT mostra o conjunto — e continua sem sobreposição. (Antes encolhia a 30% e ficava ilegível.)
        {
            const std::string grande = pa.criarFolderMap("Grande", std::nullopt);
            for (int a = 0; a < 12; ++a) {
                const std::string pai = pa.criarPastaAcervo("N" + std::to_string(a), std::nullopt, grande);
                for (int b = 0; b < 10; ++b) {
                    const std::string filho = pa.criarPastaAcervo("F" + std::to_string(b), pai, grande);
                    if (b % 3 == 0) pa.criarPastaAcervo("Sub", filho, grande);
                }
            }
            pa.definirMapaAtivo(grande);
            ArvoreBackupComponent vasto(pa);
            vasto.setBounds(0, 0, 1400, 900);
            bombear(150);
            vasto.selecionarMapaPorId(grande);
            bombear(100);
            int menorW = 100000, menorH = 100000;
            bool sobra = false;
            for (size_t i = 0; i < vasto.nodes_.size(); ++i) {
                menorW = std::min(menorW, vasto.nodes_[i].boundsOriginal.getWidth());
                menorH = std::min(menorH, vasto.nodes_[i].boundsOriginal.getHeight());
                for (size_t j = i + 1; j < vasto.nodes_.size() && !sobra; ++j)
                    if (vasto.nodes_[i].boundsOriginal.intersects(vasto.nodes_[j].boundsOriginal)) sobra = true;
            }
            checar(vasto.nodes_.size() > 150, "a big map with " + juce::String((int) vasto.nodes_.size()) + " folders");
            checar(menorW >= 140 && menorH >= 60, "even the smallest card stays legible: " + juce::String(menorW) + "x" + juce::String(menorH) + " (min 140x60)");
            checar(!sobra, "and the big map still has no overlap");
            vasto.enquadrarTudo();
            juce::Rectangle<float> vista;
            bool primeiro = true;
            for (const auto& n : vasto.nodes_) {
                const auto r2 = n.bounds.toFloat() * vasto.zoom_ + vasto.panOffset_;
                vista = primeiro ? r2 : vista.getUnion(r2);
                primeiro = false;
            }
            checar(vasto.areaCanvas().withZeroOrigin().toFloat().expanded(1.0f).contains(vista), "FIT shows the whole big map (zoom " + juce::String(vasto.zoom_, 2) + ")");
            if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
                juce::PNGImageFormat png;
                for (const char* nome : {"foldermap_grande_fit.png", "foldermap_grande_100.png"}) {
                    if (juce::String(nome).contains("100")) { vasto.zoom_ = 1.0f; vasto.panOffset_ = {20.0f, -200.0f}; }
                    bombear(150);
                    auto arq = dir.getChildFile(nome);
                    arq.deleteFile();
                    if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                        png.writeImageToStream(vasto.createComponentSnapshot(vasto.getLocalBounds()), *out);
                }
                vasto.enquadrarTudo();
            }
            // Um AJUSTAR de versão anterior (fator < 1, posições compactas) é descartado ao abrir.
            pa.definirFatorCardsDoMapa(grande, 0.3);
            pa.atualizarPosicaoPastaAcervo(vasto.nodes_.front().id, 12, 12);
            ArvoreBackupComponent antigo(pa);
            antigo.setBounds(0, 0, 1400, 900);
            bombear(150);
            checar(pa.fatorCardsDoMapa(grande) == 0.0 && antigo.fatorCards_ >= 1.0, "an old arrangement with cards shrunk below 100% is discarded and re-arranged");
            pa.definirMapaAtivo(mapa);
        }

        // ORIGINAL: estrutura vinda do disco, já com auto-arranjo, sem precisar montar nada
        for (const char* rel : {"A/B/um.wav", "A/B/dois.wav", "A/C/tres.wav", "D/quatro.wav"}) {
            auto f = raizFM.getChildFile("fontes").getChildFile(rel);
            f.getParentDirectory().createDirectory();
            f.replaceWithText(rel);
            inserirItemComArquivo(pa.projeto().registro(), projetoId, std::string("ORG-") + rel, f);
        }
        pa.definirMapaAtivo(ProjetoAberto::kMapaOriginal);
        ArvoreBackupComponent original(pa);
        original.setBounds(0, 0, 1400, 900);
        bombear(100);
        original.selecionarMapaPorId(ProjetoAberto::kMapaOriginal);
        bombear(100);
        checar(original.mapaAtivoEhOriginal() && original.nodes_.size() >= 4, "ORIGINAL shows the imported folder structure (" + juce::String((int) original.nodes_.size()) + " folders)");
        bool sobra = false;
        for (size_t i = 0; i < original.nodes_.size(); ++i)
            for (size_t j = i + 1; j < original.nodes_.size(); ++j)
                if (original.nodes_[i].bounds.intersects(original.nodes_[j].bounds)) sobra = true;
        checar(original.fatorCards_ > 0.0 && !sobra, "ORIGINAL opens already auto-arranged, with no overlap");
    } catch (const std::exception& e) {
        checar(false, juce::String("folder map sizes selftest: ") + e.what());
    }
    raizFM.deleteRecursively();

    // ------------------------------------------------ Setas: grade do METADATA e miniaturas do INTAKE
    std::cout << "\n-- Arrow navigation: METADATA grid + INTAKE thumbnails --\n";
    juce::File raizSetas = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("matriz_setas_selftest_" + juce::Uuid().toDashedString());
    try {
        raizSetas.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "Setas";
        params.prefixoNomenclatura = "SET";
        auto projeto = matriz::model::Project::criar(raizSetas.getChildFile("projeto"), params);
        const std::string projetoId = projeto->projetoId();
        constexpr int kGrande = 15000, kIntake = 3000;
        {
            auto& r0 = projeto->registro();
            r0.run("BEGIN TRANSACTION", {});
            for (int i = 0; i < kGrande; ++i) inserirItem(r0, projetoId, juce::String::formatted("CAT-%05d", i).toStdString(), false);
            for (int i = 0; i < kIntake; ++i) inserirItem(r0, projetoId, juce::String::formatted("INT-%05d", i).toStdString(), true);
            r0.run("COMMIT", {});
        }
        MainComponent janela;
        janela.setBounds(0, 0, 1400, 900);
        janela.abrirProjeto(std::move(projeto));
        bombear(200);

        // ---- METADATA (MosaicoComponent)
        janela.mostrarGrid();
        auto* cw = janela.catalogWorkspace_.get();
        auto* mo = cw->mosaico_.get();
        esperarAte([&] { return !mo->snapshotPendente() && mo->totalItensCarregados() >= kGrande; }, 120000);
        bombear(300);
        checar(mo->totalItensCarregados() == kGrande, "METADATA grid loaded " + juce::String(kGrande) + " items (" + juce::String(mo->totalItensCarregados()) + ")");

        std::map<std::string, int> posicaoDoItem;  // id -> posição na ordem em que a grade os mostra
        {
            int pos = 0;
            for (const auto& id : mo->idsVisiveisEmOrdem()) posicaoDoItem[id] = pos++;
        }
        auto foco = [&] { auto f = posicaoDoItem.find(mo->itemEmFoco()); return f == posicaoDoItem.end() ? -1 : f->second; };
        auto tecla = [&](int codigo, bool shift = false) {
            return mo->keyPressed(juce::KeyPress(codigo, shift ? juce::ModifierKeys::shiftModifier : juce::ModifierKeys(), 0));
        };
        const int cols = mo->colunasParaTeste();
        checar(cols >= 2, "the grid has several columns (" + juce::String(cols) + ")");

        checar(tecla(juce::KeyPress::rightKey) && foco() == 0 && mo->itensSelecionados().size() == 1,
               "first arrow lands on the first item and selects it");
        tecla(juce::KeyPress::rightKey);
        checar(foco() == 1, "Right moves to the next item in the row");
        tecla(juce::KeyPress::downKey);
        checar(foco() == 1 + cols, "Down moves to the same column, next row (" + juce::String(foco()) + ")");
        tecla(juce::KeyPress::upKey);
        checar(foco() == 1, "Up goes back");
        for (int i = 0; i < cols - 2; ++i) tecla(juce::KeyPress::rightKey);  // fim da linha
        checar(foco() == cols - 1, "Right walks to the end of the row");
        tecla(juce::KeyPress::rightKey);
        checar(foco() == cols, "Right at the end of a row goes to the first item of the next row");
        tecla(juce::KeyPress::leftKey);
        checar(foco() == cols - 1, "Left at the start of a row goes to the last item of the previous row");
        tecla(juce::KeyPress::upKey);
        tecla(juce::KeyPress::upKey);
        checar(foco() == cols - 1, "Up at the first row stays put");
        checar(mo->itensSelecionados().size() == 1, "plain arrows keep a single selected item");

        tecla(juce::KeyPress::rightKey, true);
        tecla(juce::KeyPress::rightKey, true);
        checar(mo->itensSelecionados().size() == 3, "Shift+Right x2 extends the selection to 3 items (" + juce::String((int) mo->itensSelecionados().size()) + ")");
        tecla(juce::KeyPress::leftKey, true);
        checar(mo->itensSelecionados().size() == 2, "Shift+Left shrinks it again");
        tecla(juce::KeyPress::downKey, true);
        checar(mo->itensSelecionados().size() == static_cast<size_t>(cols + 2),
               "Shift+Down extends by a full row (2 + " + juce::String(cols) + " = " + juce::String((int) mo->itensSelecionados().size()) + ")");
        if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
            bombear(300);
            juce::PNGImageFormat png;
            auto arq = dir.getChildFile("metadata_foco.png");
            arq.deleteFile();
            if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                png.writeImageToStream(cw->createComponentSnapshot(cw->getLocalBounds()), *out);
        }

        std::string abriu;
        auto abrirOriginal = mo->aoAbrirPreview;
        mo->aoAbrirPreview = [&](const std::string& id) { abriu = id; };
        tecla(juce::KeyPress::spaceKey);
        checar(abriu == mo->itemEmFoco() && !abriu.empty(), "Space opens the preview of the focused item");
        mo->aoAbrirPreview = abrirOriginal;
        checar(!mo->keyPressed(juce::KeyPress('r', juce::ModifierKeys(), (juce::juce_wchar) 'r')),
               "in the grid, R no longer renames (it is the INTAKE's Reject key); rename is N");
        bombear(150);

        // Desempenho: segurar a seta para baixo numa coleção de 15.000 itens.
        int chamadasFicha = 0;
        auto fichaOriginal = mo->aoSelecionar;
        mo->aoSelecionar = [&](const std::string&) { ++chamadasFicha; };
        const int focoAntes = foco();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 600; ++i) tecla(juce::KeyPress::downKey);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        checar(ms < 2000.0, "600 held Down presses on 15,000 items take " + juce::String(ms, 0) + " ms (limit 2000)");
        checar(chamadasFicha == 0, "while the key repeats, the record panel is not reloaded (" + juce::String(chamadasFicha) + " reloads)");
        bombear(300);
        checar(chamadasFicha == 1, "it reloads once, after the key stops (" + juce::String(chamadasFicha) + ")");
        checar(foco() == focoAntes + 600 * cols, "600 Down presses moved exactly 600 rows (" + juce::String(focoAntes) + " -> " + juce::String(foco()) + ")");
        mo->aoSelecionar = fichaOriginal;

        // Até o fim da lista e de volta: sem travar, sem sair do intervalo.
        for (int i = 0; i < kGrande / cols + 10; ++i) tecla(juce::KeyPress::downKey);
        checar(foco() / cols == (kGrande - 1) / cols, "Down past the end stops in the last row (" + juce::String(foco()) + ")");
        for (int i = 0; i < kGrande / cols + 10; ++i) tecla(juce::KeyPress::upKey);
        checar(foco() >= 0 && foco() < cols, "Up past the start stops in the first row (" + juce::String(foco()) + ")");
        bombear(300);

        // ---- NEST na grade: uma célula, contagens, busca
        {
            auto* pa = janela.projetoAberto();
            const auto ordem = mo->idsVisiveisEmOrdem();
            const std::vector<std::string> grupo(ordem.begin(), ordem.begin() + 5);
            std::string codigoDoEscondido;
            const int visiveisAntes = mo->totalItensVisiveis();
            pa->criarNest(grupo);
            esperarAte([&] { return mo->totalItensVisiveis() == visiveisAntes - 4; }, 10000);
            bombear(200);
            checar(mo->totalItensVisiveis() == visiveisAntes - 4, "a nest of 5 files is ONE cell in the grid (" + juce::String(mo->totalItensVisiveis()) + " of " + juce::String(visiveisAntes) + ")");
            const auto capaId = pa->nestDoItem(grupo[0])->capaId;
            std::string escondido;
            for (const auto& id : grupo) if (id != capaId) { escondido = id; break; }
            bool capaVisivel = false, escondidoVisivel = false;
            for (const auto& id : mo->idsVisiveisEmOrdem()) { capaVisivel = capaVisivel || id == capaId; escondidoVisivel = escondidoVisivel || id == escondido; }
            checar(capaVisivel && !escondidoVisivel, "the cell is the cover; the other files are hidden");

            // contagens: "All Assets" segue o total real; o resto conta o nest como 1
            cw->atualizarContagens();
            auto contagem = [&](const char* chave) { for (const auto& c : cw->categorias_) if (c.chave == chave) return c.contagem; return -1; };
            esperarAte([&] { return contagem("audio") == kGrande - 4; }, 10000);
            checar(contagem("audio") == kGrande - 4, "media type counts see the nest as 1 item (" + juce::String(contagem("audio")) + ")");
            checar(contagem("all") == kGrande, "while All Assets keeps the real total of files (" + juce::String(contagem("all")) + ")");

            if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
                juce::PNGImageFormat png;
                bombear(300);
                auto arq = dir.getChildFile("nest_grade.png");
                arq.deleteFile();
                if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                    png.writeImageToStream(cw->createComponentSnapshot(cw->getLocalBounds()), *out);
                // Preview do nest: coluna com todos os arquivos do grupo.
                FloatingPreviewWindow janelaPreview(*pa, capaId, [] {}, nullptr, nullptr);
                bombear(500);
                auto arq2 = dir.getChildFile("nest_preview.png");
                arq2.deleteFile();
                if (auto out = std::unique_ptr<juce::FileOutputStream>(arq2.createOutputStream()))
                    png.writeImageToStream(janelaPreview.createComponentSnapshot(janelaPreview.getLocalBounds()), *out);
            }

            // busca: um arquivo escondido dentro do nest NUNCA some — a célula passa a ser ele
            for (const auto& it : mo->todosItensEmMemoria()) if (it.id == escondido) codigoDoEscondido = it.codigoAcervo;
            mo->definirBusca(juce::String(codigoDoEscondido));
            esperarAte([&] { for (const auto& id : mo->idsVisiveisEmOrdem()) if (id == escondido) return true; return false; }, 10000);
            bool achado = false;
            for (const auto& id : mo->idsVisiveisEmOrdem()) achado = achado || id == escondido;
            checar(achado, "searching for a file inside a nest shows the nest, opened on that file");
            mo->definirBusca({});
            bombear(300);
            pa->desfazerNest(grupo);
            esperarAte([&] { return mo->totalItensVisiveis() == visiveisAntes; }, 10000);
            checar(mo->totalItensVisiveis() == visiveisAntes, "Un-nest gives the 5 cells back");
        }

        // ---- INTAKE (miniaturas)
        janela.mostrarIntake();
        auto* iw = janela.intakeWorkspace_.get();
        iw->recarregar();
        esperarAte([&] { return !iw->snapshotPendente() && static_cast<int>(iw->todosItens_.size()) >= kIntake; }, 120000);
        checar(static_cast<int>(iw->todosItens_.size()) == kIntake, "INTAKE loaded " + juce::String(kIntake) + " items");
        iw->definirModoVisao(IntakeWorkspaceComponent::ModoVisao::Icones);
        bombear(300);
        auto teclaI = [&](int codigo, bool shift = false) {
            return iw->keyPressed(juce::KeyPress(codigo, shift ? juce::ModifierKeys::shiftModifier : juce::ModifierKeys(), 0));
        };
        checar(teclaI(juce::KeyPress::rightKey) && iw->posicaoDoFoco() == 0, "INTAKE: first arrow focuses the first card");
        teclaI(juce::KeyPress::rightKey);
        checar(iw->posicaoDoFoco() == 1, "INTAKE: Right moves to the next card");
        teclaI(juce::KeyPress::downKey);
        const int colsI = iw->posicaoDoFoco() - 1;
        checar(colsI >= 2, "INTAKE: Down moves one row (" + juce::String(colsI) + " columns)");
        for (int i = 0; i < colsI; ++i) teclaI(juce::KeyPress::upKey);
        checar(iw->itensSelecionados().empty(), "INTAKE: plain arrows move the focus without touching the selection");
        const int base = iw->posicaoDoFoco();
        teclaI(juce::KeyPress::rightKey, true);
        teclaI(juce::KeyPress::rightKey, true);
        checar(iw->itensSelecionados().size() == 3, "INTAKE: Shift+Right x2 selects 3 cards from the anchor (" + juce::String((int) iw->itensSelecionados().size()) + ")");
        teclaI(juce::KeyPress::leftKey, true);
        checar(iw->itensSelecionados().size() == 2, "INTAKE: Shift+Left shrinks the selection");
        if (auto dir = juce::File(MATRIZ_FICHAS_DIR).getParentDirectory().getChildFile("test-output"); dir.isDirectory()) {
            bombear(300);
            juce::PNGImageFormat png;
            auto arq = dir.getChildFile("intake_foco.png");
            arq.deleteFile();
            if (auto out = std::unique_ptr<juce::FileOutputStream>(arq.createOutputStream()))
                png.writeImageToStream(iw->createComponentSnapshot(iw->getLocalBounds()), *out);
        }
        iw->selecionarTodos(false);
        iw->marcadosR_.clear();
        teclaI(juce::KeyPress::rightKey);
        const int focoR = iw->posicaoDoFoco();
        checar(focoR >= 0 && focoR != base - 1, "INTAKE: arrow after clearing still moves the focus");
        iw->keyPressed(juce::KeyPress('r', juce::ModifierKeys(), (juce::juce_wchar) 'r'));
        checar(iw->marcadosR_.size() == 1 && iw->marcadosR_.count(iw->focoId_) == 1,
               "INTAKE: with nothing selected, R marks the focused card (arrow + R = triage from the keyboard)");
        iw->keyPressed(juce::KeyPress('r', juce::ModifierKeys(), (juce::juce_wchar) 'r'));
        checar(iw->marcadosR_.empty(), "INTAKE: R again unmarks it");

        // O R vale onde a seta está, mesmo com OUTROS cards marcados (tique) e o card em foco sem tique.
        {
            iw->selecionarTodos(false);
            const int p0 = iw->posicaoDoFoco();
            iw->todosItens_[(size_t) iw->indicesFiltrados_[(size_t) p0]].selecionado = true;  // tique só neste
            iw->atualizarContagens();
            teclaI(juce::KeyPress::rightKey);  // o foco anda para um card SEM tique
            const std::string idFoco = iw->focoId_;
            iw->keyPressed(juce::KeyPress('r', juce::ModifierKeys(), (juce::juce_wchar) 'r'));
            checar(iw->marcadosR_.size() == 1 && iw->marcadosR_.count(idFoco) == 1,
                   "INTAKE: R marks the card where the arrow is, not the ticked one");
            iw->keyPressed(juce::KeyPress('r', juce::ModifierKeys(), (juce::juce_wchar) 'r'));
            // Foco dentro da seleção (Shift+seta): o R vale para a seleção inteira.
            iw->selecionarTodos(false);
            teclaI(juce::KeyPress::rightKey, true);
            teclaI(juce::KeyPress::rightKey, true);
            const size_t nSel = iw->itensSelecionados().size();
            iw->keyPressed(juce::KeyPress('r', juce::ModifierKeys(), (juce::juce_wchar) 'r'));
            checar(nSel >= 2 && iw->marcadosR_.size() == nSel, "INTAKE: with the focus inside a Shift+arrow selection, R marks the whole selection");
            iw->keyPressed(juce::KeyPress('r', juce::ModifierKeys(), (juce::juce_wchar) 'r'));
            iw->selecionarTodos(false);
        }

        const auto ti0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 2000; ++i) teclaI(juce::KeyPress::downKey);
        const double msI = juce::Time::getMillisecondCounterHiRes() - ti0;
        checar(msI < 3000.0, "INTAKE: 2000 held Down presses on 3,000 cards take " + juce::String(msI, 0) + " ms (limit 3000)");
        checar(iw->posicaoDoFoco() / colsI == (kIntake - 1) / colsI, "INTAKE: Down past the end stops in the last row (" + juce::String(iw->posicaoDoFoco()) + ")");
        bombear(300);
    } catch (const std::exception& e) {
        checar(false, juce::String("arrow navigation selftest: ") + e.what());
    }
    raizSetas.deleteRecursively();

    rodarTestesNomesCanonicos(checar);
    rodarTestesPacote(checar);
    rodarTestesMerge(checar);
    rodarTestesLoteAjustes(checar);
    rodarTestesConexaoLeitura(checar);

    std::cout << "\n" << (falhas == 0 ? juce::String("ALL TESTS PASSED") : juce::String(falhas) + " FAILURE(S)") << "\n";
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
