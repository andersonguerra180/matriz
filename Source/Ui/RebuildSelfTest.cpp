#include "RebuildSelfTest.h"

#include <JuceHeader.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <thread>

#include "../Consolidacao/Consolidacao.h"
#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "../Vault/Resolucao.h"
#include "ProjetoAberto.h"
#include "TimelineComponent.h"

namespace matriz::ui {

namespace {

using matriz::db::Value;
namespace cons = matriz::consolidacao;

std::string sha(const juce::File& f) { return matriz::ingest::calcularChecksums(f).sha256; }

std::string inserirItem(matriz::db::Database& reg, const std::string& projetoId, const std::string& codigo, const juce::File& arq) {
    const std::string id = matriz::model::novoUuid(), agora = matriz::model::agoraIso8601();
    reg.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
            "VALUES (?, ?, ?, ?, 'audio', 'novo', ?, ?)",
            {Value::of(id), Value::of(projetoId), Value::of(codigo), Value::of(arq.getFileNameWithoutExtension().toStdString()),
             Value::of(agora), Value::of(agora)});
    reg.run("INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, "
            "checksum_sha256, criado_em, atualizado_em) VALUES (?, ?, ?, ?, 'preservation_master', 1, ?, ?, ?, ?)",
            {Value::of(matriz::model::novoUuid()), Value::of(id), Value::of(arq.getFileName().toStdString()),
             Value::of(arq.getFullPathName().toStdString()), Value::of(static_cast<long long>(arq.getSize())), Value::of(sha(arq)),
             Value::of(agora), Value::of(agora)});
    return id;
}

void escreverArquivo(const juce::File& destino, int semente, int tamanho) {
    juce::MemoryBlock mb(static_cast<size_t>(tamanho));
    auto* p = static_cast<juce::uint8*>(mb.getData());
    for (int i = 0; i < tamanho; ++i) p[i] = static_cast<juce::uint8>((i * 31 + semente * 7) & 0xff);
    destino.getParentDirectory().createDirectory();
    destino.replaceWithData(mb.getData(), mb.getSize());
}

std::map<std::string, std::pair<std::string, juce::int64>> retrato(const juce::File& pasta) {
    std::map<std::string, std::pair<std::string, juce::int64>> m;  // caminho -> (tamanho:mtime)
    for (const auto& f : pasta.findChildFiles(juce::File::findFiles, true, "*"))
        m[f.getFullPathName().toStdString()] = {std::to_string(f.getSize()), f.getLastModificationTime().toMilliseconds()};
    return m;
}

int contarTemporarios(const juce::File& pasta) {
    return pasta.findChildFiles(juce::File::findFiles, true, "*.mtz-tmp").size();
}

}  // namespace

int rodarRebuildSelfTest() {
    std::cout << "== Cópia verificada + Rebuild do MAIN ==\n";
    int falhas = 0;
    auto checar = [&](bool ok, const juce::String& d) {
        std::cout << (ok ? "  OK   " : "  FAIL ") << d << "\n";
        if (!ok) ++falhas;
    };
    juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("matriz_rebuild_selftest_" + juce::Uuid().toDashedString());
    try {
        const juce::File src = raiz.getChildFile("SOURCE");
        src.createDirectory();
        const juce::File fa = src.getChildFile("a.wav"), fb = src.getChildFile("b.wav"), fc = src.getChildFile("c.wav"), fd = src.getChildFile("d.wav");
        escreverArquivo(fa, 1, 300000);
        escreverArquivo(fb, 2, 450000);
        escreverArquivo(fc, 3, 200000);
        escreverArquivo(fd, 4, 120000);

        {
            const auto origemCopia = raiz.getChildFile("copia_verificada_origem.bin");
            const auto destinoCopia = raiz.getChildFile("copia_verificada_destino.bin");
            escreverArquivo(origemCopia, 11, 4096);
            destinoCopia.replaceWithText("conteudo antigo");
            auto copia = cons::copiarVerificado(origemCopia, destinoCopia);
            checar(copia.ok && destinoCopia.getSize() == 4096 &&
                       !destinoCopia.getSiblingFile(destinoCopia.getFileName() + ".mtz-tmp").exists(),
                   "verified copy replaces an existing destination and leaves no temporary file");

            destinoCopia.replaceWithText("destino antigo intacto");
            copia = cons::copiarVerificado(raiz.getChildFile("origem_ausente.bin"), destinoCopia);
            checar(!copia.ok && destinoCopia.loadFileAsString() == "destino antigo intacto",
                   "failed verified copy leaves the existing destination unchanged");

            // Nome iniciado por "~" (arquivo de trava do Office): o JUCE o trata como caminho de home.
            const juce::File pastaTil = raiz.getChildFile("til");
            pastaTil.createDirectory();
            const juce::File destinoTil(pastaTil.getFullPathName() + juce::File::getSeparatorString() + "~$teste.xlsx");
            copia = cons::copiarVerificado(origemCopia, destinoTil);
            checar(copia.ok && destinoTil.getSize() == 4096 &&
                       pastaTil.findChildFiles(juce::File::findFiles, false, "*.mtz-tmp").isEmpty(),
                   "verified copy of a file whose name starts with ~ (Office lock file)");
        }

        matriz::model::NovoProjetoParams params;
        params.nome = "Rebuild";
        params.prefixoNomenclatura = "RBD";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("P"), params);
        const std::string projetoId = projeto->projetoId();
        ProjetoAberto pa(std::move(projeto));
        auto& reg = pa.projeto().registro();
        const auto idA = inserirItem(reg, projetoId, "RBD-1", fa);
        const auto idB = inserirItem(reg, projetoId, "RBD-2", fb);
        const auto idC = inserirItem(reg, projetoId, "RBD-3", fc);
        const auto idD = inserirItem(reg, projetoId, "RBD-4", fd);
        (void)idA; (void)idD;

#if JUCE_MAC && JUCE_INTEL
        {
            // Fechar o preview com o x87 sujo de MMX (instrução MMX sem EMMS na message thread) travava para sempre em
            // ~TimelineComponent (Thread::sleep -> sleep_for em long double dá NaN). Se travar, o vigia reprova e sai.
            std::atomic<bool> terminou{false};
            std::thread vigia([&terminou] {
                for (int i = 0; i < 100 && !terminou.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!terminou.load()) {
                    std::cout << "  FAIL ~TimelineComponent hung with the x87 dirty from MMX (preview close freeze)\n" << std::flush;
                    std::_Exit(1);
                }
            });
            auto timeline = std::make_unique<TimelineComponent>(pa);
            __asm__ volatile("pxor %%mm0, %%mm0" ::: "mm0");  // MMX sem EMMS: deixa o x87 cheio
            timeline.reset();
            terminou = true;
            vigia.join();
            checar(true, "closing the preview timeline with the x87 dirty from MMX does not hang");
        }
#endif
        const std::string hashA = sha(fa), hashB = sha(fb), hashC = sha(fc), hashD = sha(fd);
        const juce::File media = pa.projeto().pastaMedia();
        const auto hier = cons::HierarquiaBackup{cons::NivelHierarquia::TipoMidia};

        auto planejar = [&](const juce::File& mediaDestino, bool estrito) {
            return cons::planejarConsolidacao(reg, pa.projeto().pasta(), mediaDestino, hier, {}, cons::ModoPrefixoArquivo::Nenhum, {}, true,
                                              false, false, false, {}, estrito);
        };
        auto itemDe = [&](const cons::PlanoConsolidacao& p, const std::string& id) -> const cons::ItemPlanejado* {
            for (const auto& ip : p.itens) if (ip.itemId == id) return &ip;
            return nullptr;
        };
        auto arquivoNoMain = [&](const juce::File& m, const std::string& codigo) {
            for (const auto& f : m.findChildFiles(juce::File::findFiles, true, "*"))
                if (f.getFileNameWithoutExtension().toStdString() == codigo) return f;
            return juce::File();
        };

        // ---- 1) primeiro backup: cópia verificada
        {
            auto plano = planejar(media, false);
            auto res = cons::executarConsolidacao(reg, pa.projeto().pasta(), media, plano);
            checar(res.consolidados == 4 && res.falhas.empty(), "first backup: 4 copied, no failures (" + juce::String(res.consolidados) + ")");
            checar(contarTemporarios(media) == 0, "first backup: no .mtz-tmp left");
            auto st = reg.prepare("SELECT COUNT(*) FROM consolidacao_registro WHERE tamanho_bytes > 0");
            st.step();
            checar(st.columnInt(0) == 4, "first backup: the final size of each copy is registered");
            checar(sha(arquivoNoMain(media, "a")) == hashA && sha(arquivoNoMain(media, "b")) == hashB,
                   "first backup: MAIN files have the same SHA-256 as the SOURCE (no embedded metadata)");
        }

        // ---- 2) arquivo registrado e truncado é recopiado; o resto não é tocado
        {
            const auto b = arquivoNoMain(media, "b");
            juce::MemoryBlock mb;
            b.loadFileAsData(mb);
            b.replaceWithData(mb.getData(), mb.getSize() / 2);  // truncado de propósito
            auto plano = planejar(media, false);
            const auto* ib = itemDe(plano, idB);
            const auto* ic = itemDe(plano, idC);
            checar(ib && !ib->jaConsolidado && ib->registradoDivergente, "truncated registered file is planned for a new copy");
            checar(ic && ic->jaConsolidado, "intact files stay 'already consolidated'");
            auto res = cons::executarConsolidacao(reg, pa.projeto().pasta(), media, plano);
            checar(res.consolidados == 1 && res.falhas.empty() && sha(arquivoNoMain(media, "b")) == hashB,
                   "truncated file recopied from the SOURCE with the right hash");
        }

        // ---- 3) .mtz-tmp velho é limpo; cancelar não deixa temporário nem registro
        {
            media.getChildFile("x.mtz-tmp").replaceWithText("lixo");
            checar(cons::limparTemporariosDeCopia(media) == 1 && contarTemporarios(media) == 0, "stale .mtz-tmp files are removed");
            const juce::File novo = raiz.getChildFile("CANCELA").getChildFile("Media");
            auto plano = planejar(novo, true);
            auto res = cons::executarConsolidacao(reg, pa.projeto().pasta(), novo, plano, {}, {}, false, [] { return false; });
            auto st = reg.prepare("SELECT COUNT(*) FROM consolidacao_registro WHERE destino_path = ?");
            st.bind(1, Value::of(cons::chaveDestino(novo)));
            st.step();
            checar(res.cancelado && contarTemporarios(novo.getParentDirectory()) == 0 && st.columnInt(0) == 0,
                   "cancel: stops, leaves no .mtz-tmp and registers nothing");
        }

        // ---- 4) rebuild
        // MAIN antigo "com embed": o arquivo c ganha bytes extras (hash diferente do catálogo).
        {
            const auto c = arquivoNoMain(media, "c");
            juce::MemoryBlock mb;
            c.loadFileAsData(mb);
            mb.append("EMBEDDED-METADATA", 17);
            c.replaceWithData(mb.getData(), mb.getSize());
            reg.run("UPDATE consolidacao_registro SET checksum_sha256 = ?, tamanho_bytes = ? WHERE arquivo_id = (SELECT id FROM arquivo WHERE item_id = ?)",
                    {Value::of(sha(c)), Value::of(static_cast<long long>(c.getSize())), Value::of(idC)});
        }
        const auto retratoAntes = retrato(media);
        // Config do MAIN por regra (como um projeto real): o Cancel Rebuild tem de devolvê-la.
        const std::string cfgMain = R"({"preservar": false, "mapa": false, "org": 5, "hierarquia": "subject,ano", "por_source": true})";
        reg.run("UPDATE projeto SET backup_config_main = ?", {Value::of(cfgMain)});
        checar(pa.podeEditarMain(), "rebuild: the project can enter the mode (MAIN exists)");
        checar(!pa.entrarModoEdicaoMain("nome errado") && !pa.emRebuild(), "rebuild: wrong project name keeps the MAIN locked");
        checar(pa.entrarModoEdicaoMain("Rebuild") && pa.emRebuild(), "rebuild: entered with the project name");
        {
            auto st = reg.prepare("SELECT COUNT(*) FROM backup_destino WHERE papel = 'FONTE_REBUILD'");
            st.step();
            checar(st.columnInt(0) == 1 && !pa.mainExiste() && !pa.mainUsaMapa(), "rebuild: old MAIN is FONTE_REBUILD and the project has no MAIN");
        }
        const auto fonte = pa.fonteRebuild();
        checar(fonte.has_value() && fonte->raiz == pa.projeto().raiz(), "rebuild: source is the old MAIN root");

        // ---- 4-) Cancel Rebuild: volta ao MAIN antigo sem copiar nada, com a config de antes; uma cópia de rebuild
        // começada numa pasta nova sai do banco (os arquivos dela ficam no disco). Depois entra de novo para o resto.
        {
            auto cont = [&](const std::string& sql) { auto st = reg.prepare(sql); st.step(); return static_cast<int>(st.columnInt(0)); };
            const int registrosAntes = cont("SELECT COUNT(*) FROM consolidacao_registro");
            const juce::File parcial = raiz.getChildFile("PARCIAL");
            parcial.getChildFile("Media").createDirectory();
            parcial.getChildFile("Media").getChildFile("ja_copiado.bin").replaceWithText("x");
            pa.definirRebuildDestino(parcial);
            const std::string chaveParcial = cons::chaveDestino(parcial.getChildFile("Media"));
            reg.run("INSERT INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, checksum_sha256, "
                    "consolidado_em, destino_path, destino_id, tamanho_bytes) "
                    "SELECT 'parcial-1', item_id, pasta_id, arquivo_id, caminho_relativo_destino, checksum_sha256, consolidado_em, ?, 'novo', "
                    "tamanho_bytes FROM consolidacao_registro LIMIT 1",
                    {Value::of(chaveParcial)});
            bool cfgOk = false;
            checar(pa.cancelarRebuild("nome errado", cfgOk).isNotEmpty() && pa.emRebuild(), "cancel rebuild: wrong project name keeps the rebuild");
            const auto erro = pa.cancelarRebuild("Rebuild", cfgOk);
            checar(erro.isEmpty() && !pa.emRebuild() && cfgOk, "cancel rebuild: mode ends with the project name");
            checar(cont("SELECT COUNT(*) FROM backup_destino WHERE papel = 'FONTE_REBUILD'") == 0 &&
                       cont("SELECT COUNT(*) FROM backup_destino WHERE papel = 'ORIGINAL' AND ativo = 1") == 1 && !pa.fonteRebuild(),
                   "cancel rebuild: the old MAIN is the ORIGINAL again");
            {
                auto st = reg.prepare("SELECT COALESCE(backup_config_main, ''), COALESCE(rebuild_destino, '') FROM projeto LIMIT 1");
                st.step();
                const auto cfg = juce::JSON::parse(juce::String::fromUTF8(st.columnText(0).c_str()));
                checar(cfg.isObject() && cfg.getProperty("hierarquia", "").toString() == "subject,ano" && st.columnText(1).empty(),
                       "cancel rebuild: the old MAIN settings come back from the Project Log");
            }
            checar(cont("SELECT COUNT(*) FROM consolidacao_registro WHERE destino_path = '" + chaveParcial + "'") == 0 &&
                       cont("SELECT COUNT(*) FROM consolidacao_registro") == registrosAntes,
                   "cancel rebuild: the unfinished rebuild copy leaves the registry, the old MAIN's registrations stay");
            checar(parcial.getChildFile("Media").getChildFile("ja_copiado.bin").existsAsFile() && retrato(media) == retratoAntes,
                   "cancel rebuild: no file is touched (old MAIN and the unfinished folder)");
            checar(pa.mainExiste() && pa.podeEditarMain(), "cancel rebuild: the project has its MAIN again");
            parcial.deleteRecursively();
            checar(pa.entrarModoEdicaoMain("Rebuild") && pa.emRebuild(), "rebuild: entered again after the cancel test");
        }

        // clone antigo registrado (vai ficar desatualizado no fim)
        reg.run("INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em) VALUES ('clone1', 'clone1', ?, 'Clone', 'CLONE', 1, ?)",
                {Value::of(raiz.getChildFile("CLONE").getFullPathName().toStdString()), Value::of(matriz::model::agoraIso8601())});

        auto novoMain = [&](const juce::String& nome) {
            const juce::File r = raiz.getChildFile(nome);
            r.createDirectory();
            matriz::model::DestinationInfo d;
            d.destinationId = matriz::model::novoUuid();
            d.projetoId = projetoId;
            d.papel = "ORIGINAL";
            d.rotulo = nome.toStdString();
            d.revisao = 1;
            d.ultimaEdicaoUtc = d.criadoEm = matriz::model::agoraIso8601();
            d.gravarEmArquivo(r.getChildFile("destination.json"));
            return r;
        };
        cons::OpcoesRebuild op;
        op.fonteMedia = fonte->raiz.getChildFile("Media");
        op.fonteDestinoId = fonte->destinoId;
        op.descricaoFonte = fonte->raiz.getFullPathName().toStdString();

        // 4a) SOURCE montada: o arquivo com "embed" vem da SOURCE
        const juce::File novo1 = novoMain("NOVO1");
        {
            auto plano = planejar(novo1.getChildFile("Media"), true);
            checar(plano.itens.size() == 4 && !itemDe(plano, idA)->jaConsolidado, "rebuild: strict plan ignores the old MAIN registrations (4 to copy)");
            auto res = cons::executarConsolidacao(reg, pa.projeto().pasta(), novo1.getChildFile("Media"), plano, {}, {}, false, {}, &op);
            checar(res.consolidados == 4 && res.falhas.empty(), "rebuild 4a: 4 copied, no failures");
            checar(res.copiadosDaSourcePorDivergencia == 1, "rebuild 4a: 1 file taken from the SOURCE because the old MAIN differed");
            checar(sha(arquivoNoMain(novo1.getChildFile("Media"), "c")) == hashC, "rebuild 4a: the old-MAIN-with-embed file ends with the catalog hash");
            checar(sha(arquivoNoMain(novo1.getChildFile("Media"), "a")) == hashA && sha(arquivoNoMain(novo1.getChildFile("Media"), "d")) == hashD,
                   "rebuild 4a: untouched files copied from the old MAIN keep the catalog hash");
            checar(contarTemporarios(novo1) == 0, "rebuild 4a: no .mtz-tmp");
        }
        // 4b) SOURCE fora do ar: tudo vem do MAIN antigo (inclusive o que difere)
        {
            const juce::File srcOff = raiz.getChildFile("SOURCE_OFF");
            src.moveFileTo(srcOff);
            const juce::File novo2 = novoMain("NOVO2");
            auto plano = planejar(novo2.getChildFile("Media"), true);
            auto res = cons::executarConsolidacao(reg, pa.projeto().pasta(), novo2.getChildFile("Media"), plano, {}, {}, false, {}, &op);
            checar(res.consolidados == 4 && res.falhas.empty(), "rebuild 4b (no SOURCE): 4 copied from the old MAIN");
            checar(res.mantidosDoMainAntigo.size() == 1 && res.copiadosDaSourcePorDivergencia == 0,
                   "rebuild 4b: the file that differs from the SOURCE is listed as kept from the old MAIN");
            checar(sha(arquivoNoMain(novo2.getChildFile("Media"), "c")) == sha(arquivoNoMain(media, "c")), "rebuild 4b: kept file is the old MAIN's bytes");
            // arquivo ausente nos dois lados => falha com motivo
            const juce::File fe = raiz.getChildFile("EXTRA").getChildFile("e.wav");
            escreverArquivo(fe, 5, 1000);
            const auto idE = inserirItem(reg, projetoId, "RBD-5", fe);
            fe.deleteFile();  // nunca esteve no MAIN antigo e a SOURCE dele sumiu
            const juce::File novo3 = novoMain("NOVO3");
            auto plano3 = planejar(novo3.getChildFile("Media"), true);
            auto res3 = cons::executarConsolidacao(reg, pa.projeto().pasta(), novo3.getChildFile("Media"), plano3, {}, {}, false, {}, &op);
            bool motivo = false;
            for (const auto& f : res3.falhas) if (f.find("ausente no MAIN antigo e na SOURCE") != std::string::npos) motivo = true;
            checar(motivo && res3.consolidados == 4, "rebuild: missing in the old MAIN and in the SOURCE => failure with the reason");
            reg.run("DELETE FROM item WHERE id = ?", {Value::of(idE)});
            srcOff.moveFileTo(src);
        }
        checar(retrato(media) == retratoAntes, "rebuild: the old MAIN has the same files, sizes and mtimes after everything");

        // 4c) fim do modo
        {
            const std::string novoId = matriz::vault::destinationIdDaRaiz(novo1);
            const auto erro = pa.concluirRebuild(novo1, novoId);
            checar(erro.isEmpty() && !pa.emRebuild(), "rebuild: mode ends when the new MAIN is committed");
            auto cont = [&](const std::string& sql) { auto st = reg.prepare(sql); st.step(); return static_cast<int>(st.columnInt(0)); };
            checar(cont("SELECT COUNT(*) FROM backup_destino WHERE papel = 'FONTE_REBUILD'") == 0, "rebuild end: old MAIN left the destination list");
            checar(cont("SELECT COUNT(*) FROM backup_destino WHERE papel = 'ORIGINAL'") == 1, "rebuild end: the new destination is the ORIGINAL");
            checar(cont("SELECT COUNT(*) FROM backup_destino WHERE id = 'clone1' AND desatualizado_rebuild = 1") == 1, "rebuild end: old clones are marked outdated");
            checar(cont("SELECT COUNT(*) FROM consolidacao_registro WHERE COALESCE(destino_id, '') IN ('', '" + fonte->destinoId + "')") == 0 &&
                       cont("SELECT COUNT(*) FROM consolidacao_registro WHERE destino_path = '" + cons::chaveDestino(novo1.getChildFile("Media")) + "'") == 4,
                   "rebuild end: the old MAIN's registrations are gone and the new MAIN's 4 remain");
            checar(pa.mainExiste() && !pa.podeEditarMain() == false, "rebuild end: the project has a MAIN again");
        }

        // ---- 5) INBOX: o projeto nasce com INBOX + README; arquivo da INBOX é MOVIDO para o MAIN (copiado, verificado,
        // removido da INBOX), o .xmp vai junto, subpastas vazias somem; arquivo de fora da INBOX continua sendo COPIADO.
        {
            matriz::model::NovoProjetoParams params2;
            params2.nome = "Inbox";
            params2.prefixoNomenclatura = "INB";
            auto projeto2 = matriz::model::Project::criar(raiz.getChildFile("P2"), params2);
            const std::string projetoId2 = projeto2->projetoId();
            ProjetoAberto pb(std::move(projeto2));
            auto& regb = pb.projeto().registro();
            const juce::File raizMain = pb.projeto().raiz();
            const juce::File inbox = raizMain.getChildFile("INBOX");
            checar(inbox.isDirectory() && raizMain.getChildFile("README - LEIA-ME.txt").existsAsFile(),
                   "inbox: a new project is created with INBOX and the README");

            const juce::File dentro = inbox.getChildFile("Show X/2027/novo.wav");
            const juce::File fora = src.getChildFile("fora.wav");
            escreverArquivo(dentro, 9, 150000);
            escreverArquivo(fora, 10, 90000);
            dentro.getSiblingFile("novo.wav.xmp").replaceWithText("<x:xmpmeta/>");
            inbox.getChildFile("Vazia/Sub").createDirectory();
            const std::string idDentro = inserirItem(regb, projetoId2, "INB-1", dentro);
            const std::string idFora = inserirItem(regb, projetoId2, "INB-2", fora);
            const std::string hashDentro = sha(dentro), hashFora = sha(fora);
            const juce::File mediaB = pb.projeto().pastaMedia();
            auto planoB = cons::planejarConsolidacao(regb, pb.projeto().pasta(), mediaB, hier, {}, cons::ModoPrefixoArquivo::Nenhum, {}, true,
                                                     false, false, false, {}, false);
            auto resB = cons::executarConsolidacao(regb, pb.projeto().pasta(), mediaB, planoB);
            checar(resB.consolidados == 2 && resB.falhas.empty(), "inbox: both files consolidated, no failures");
            checar(resB.movidosDaInbox == 1 && !dentro.existsAsFile(), "inbox: the INBOX file was MOVED (it left the INBOX)");
            const auto noMainB = arquivoNoMain(mediaB, "novo");
            checar(noMainB.existsAsFile() && sha(noMainB) == hashDentro, "inbox: the MAIN copy has the same SHA-256");
            checar(noMainB.getSiblingFile("novo.wav.xmp").existsAsFile() && !dentro.getSiblingFile("novo.wav.xmp").existsAsFile(),
                   "inbox: the .xmp sidecar went along");
            checar(inbox.isDirectory() && !inbox.getChildFile("Show X").exists() && !inbox.getChildFile("Vazia").exists(),
                   "inbox: empty subfolders removed, the INBOX itself stays");
            checar(fora.existsAsFile() && sha(arquivoNoMain(mediaB, "fora")) == hashFora, "inbox: a file outside the INBOX is still COPIED, not moved");

            // ---- LIST a partir do Folder Map (cadeia de pastas, prévia, aplicar)
            const std::string pShows = pb.criarPastaAcervo("Shows", std::nullopt);
            const std::string p2027 = pb.criarPastaAcervo("2027", pShows);
            const std::string pCayena = pb.criarPastaAcervo("Cayena", p2027);
            pb.adicionarItemAPastaSemRemoverOutras(idDentro, pCayena);
            const auto cadeia = pb.cadeiaDePastasDoItem(idDentro);
            checar(cadeia.size() == 3 && cadeia[0] == "Cayena" && cadeia[1] == "2027" && cadeia[2] == "Shows",
                   "list-from-folder: the folder chain goes from the item's folder up to the root");
            checar(pb.cadeiaDePastasDoItem(idFora).empty(), "list-from-folder: an item outside any folder has an empty chain");
            auto previa = pb.previaListaPelasPastas({idDentro, idFora}, 0, true);
            checar(previa.itemEValor.size() == 1 && previa.itemEValor[0].second == "Cayena" && previa.semPasta == 1,
                   "list-from-folder: preview = 1 file gets 'Cayena', 1 is outside any folder");
            pb.aplicarListaPelasPastas(previa.itemEValor);
            checar(pb.lerMetadado(idDentro, "lista").value_or("") == "Cayena", "list-from-folder: LIST was written");
            checar(pb.previaListaPelasPastas({idDentro}, 1, true).jaPreenchidos == 1,
                   "list-from-folder: 'fill only the empty ones' keeps the value that is already there");
            auto prev2 = pb.previaListaPelasPastas({idDentro}, 1, false);
            checar(prev2.itemEValor.size() == 1 && prev2.itemEValor[0].second == "2027", "list-from-folder: 'replace all' at level 2 gives the parent folder");
            checar(pb.previaListaPelasPastas({idDentro}, 5, false).semNivel == 1, "list-from-folder: a level above the root is reported, not invented");
        }

        // ---- 6) Reorganizar o MAIN pelo folder map: pasta renomeada (inclusive só a caixa), pasta apagada,
        // item trocado de pasta e colisão de nome viram MOVES no backup seguinte — nunca recópia, nunca apaga.
        {
            matriz::model::NovoProjetoParams params3;
            params3.nome = "Reorg";
            params3.prefixoNomenclatura = "REO";
            auto projeto3 = matriz::model::Project::criar(raiz.getChildFile("P3"), params3);
            const std::string projetoId3 = projeto3->projetoId();
            ProjetoAberto pc(std::move(projeto3));
            auto& regc = pc.projeto().registro();
            const juce::File mediaC = pc.projeto().pastaMedia();
            const std::string mapa = pc.criarFolderMap("Mapa do MAIN", std::nullopt);
            const std::string pShows = pc.criarPastaAcervo("Shows", std::nullopt, mapa);
            const std::string p2027 = pc.criarPastaAcervo("2027", pShows, mapa);
            const std::string pFotos = pc.criarPastaAcervo("fotos", std::nullopt, mapa);
            const std::string pVelha = pc.criarPastaAcervo("Velha", std::nullopt, mapa);
            const std::string pA = pc.criarPastaAcervo("A", std::nullopt, mapa);
            const std::string pB = pc.criarPastaAcervo("B", std::nullopt, mapa);
            const std::string pC = pc.criarPastaAcervo("C", std::nullopt, mapa);
            const juce::File s3 = raiz.getChildFile("SOURCE3");
            auto novoArq = [&](const juce::String& rel, int semente) {
                const juce::File f = s3.getChildFile(rel);
                escreverArquivo(f, semente, 40000 + semente * 1000);
                return f;
            };
            const auto f1 = novoArq("show.wav", 21), f2 = novoArq("foto.wav", 22), f3 = novoArq("velha.wav", 23);
            const auto f4 = novoArq("a/x.wav", 24), f5 = novoArq("duas.wav", 25), f6 = novoArq("c/x.wav", 26);
            const auto i1 = inserirItem(regc, projetoId3, "REO-1", f1), i2 = inserirItem(regc, projetoId3, "REO-2", f2);
            const auto i3 = inserirItem(regc, projetoId3, "REO-3", f3), i4 = inserirItem(regc, projetoId3, "REO-4", f4);
            const auto i5 = inserirItem(regc, projetoId3, "REO-5", f5), i6 = inserirItem(regc, projetoId3, "REO-6", f6);
            pc.adicionarItensAPasta({i1}, p2027);
            pc.adicionarItensAPasta({i2}, pFotos);
            pc.adicionarItensAPasta({i3}, pVelha);
            pc.adicionarItensAPasta({i4, i5}, pA);
            pc.adicionarItensAPasta({i6}, pC);
            // MAIN por folder map (o que o primeiro Make Backup grava).
            regc.run("UPDATE projeto SET backup_config_main = ?", {Value::of("{\"mapa_id\": \"" + mapa + "\"}")});
            const auto hierMapa = cons::HierarquiaBackup{cons::NivelHierarquia::PastaManual};
            auto planejarC = [&] {
                return cons::planejarConsolidacao(regc, pc.projeto().pasta(), mediaC, hierMapa, {}, cons::ModoPrefixoArquivo::Nenhum, {}, true,
                                                  false, false, false, mapa, false);
            };
            auto res0 = cons::executarConsolidacao(regc, pc.projeto().pasta(), mediaC, planejarC());
            checar(res0.consolidados == 6 && res0.falhas.empty() && mediaC.getChildFile("Shows/2027/show.wav").existsAsFile() &&
                       mediaC.getChildFile("A/x.wav").existsAsFile() && mediaC.getChildFile("C/x.wav").existsAsFile(),
                   "reorganize: first backup lays the MAIN out by the folder map");
            mediaC.getChildFile("Shows/2027/show.wav.xmp").replaceWithText("<x:xmpmeta/>");
            checar(planejarC().movimentosSemPasta.empty(), "reorganize: nothing to move right after a backup");

            // Pastas do mapa do MAIN agora ficam livres.
            checar(!pc.pastaTemArquivosNoMain(pShows), "reorganize: a MAIN map folder with files is no longer locked");
            checar(pc.renomearPastaAcervo(pShows, "Concerts"), "reorganize: renaming a folder that has files in the MAIN is allowed");
            checar(pc.renomearPastaAcervo(pFotos, "Fotos"), "reorganize: case-only rename is allowed");
            checar(pc.apagarPastaAcervo(pVelha), "reorganize: deleting a folder that has files in the MAIN is allowed");
            pc.adicionarItensAPasta({i4, i6}, pB);            // A -> B e C -> B (os dois se chamam x.wav)
            pc.adicionarItemAPastaSemRemoverOutras(i5, pB);   // i5 em A e B, uma cópia só: fica onde está
            regc.run("INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em) "
                     "VALUES ('clone3', 'clone3', ?, 'Clone', 'CLONE', 1, ?)",
                     {Value::of(raiz.getChildFile("CLONE3").getFullPathName().toStdString()), Value::of(matriz::model::agoraIso8601())});

            auto plano = planejarC();
            std::map<std::string, std::pair<juce::String, juce::String>> mv;  // item -> (de, para)
            bool todosReorg = true;
            for (const auto& m : plano.movimentosSemPasta) {
                mv[m.itemId] = {m.moverDe, m.caminhoRelativoDestino};
                todosReorg = todosReorg && m.reorganizacao;
            }
            checar(plano.movimentosSemPasta.size() == 5 && todosReorg, "reorganize: 5 moves planned (" + juce::String(static_cast<int>(plano.movimentosSemPasta.size())) + ")");
            checar(mv[i1].second == "Concerts/2027/show.wav", "reorganize: renamed parent folder -> Concerts/2027/show.wav");
            checar(mv[i2].first == "fotos/foto.wav" && mv[i2].second == "Fotos/foto.wav", "reorganize: case-only rename -> Fotos/foto.wav");
            checar(mv[i3].second == juce::String(cons::kPastaSemPasta) + "/velha.wav", "reorganize: deleted folder -> _SEM_PASTA/velha.wav");
            const bool colisaoOk = (mv[i4].second == "B/x.wav" && mv[i6].second == "B/x_1.wav") || (mv[i4].second == "B/x_1.wav" && mv[i6].second == "B/x.wav");
            checar(colisaoOk, "reorganize: two x.wav moved to B -> one gets _1, nothing is overwritten");
            checar(mv.count(i5) == 0, "reorganize: an item still in the folder of its copy is not moved");
            bool copiaNova = false;
            for (const auto& ip : plano.itens) copiaNova = copiaNova || !ip.jaConsolidado;
            checar(!copiaNova, "reorganize: nothing is planned to be copied again");

            const std::string sha1 = sha(mediaC.getChildFile("Shows/2027/show.wav"));
            auto resMv = cons::executarMovimentosSemPasta(regc, pc.projeto().pasta(), mediaC, plano.movimentosSemPasta);
            checar(resMv.movidos == 5 && resMv.falhas.empty(), "reorganize: 5 files moved, no failures");
            checar(mediaC.getChildFile("Concerts/2027/show.wav").existsAsFile() && sha(mediaC.getChildFile("Concerts/2027/show.wav")) == sha1,
                   "reorganize: moved file has the same SHA-256");
            checar(mediaC.getChildFile("Concerts/2027/show.wav.xmp").existsAsFile(), "reorganize: the .xmp sidecar went along");
            bool caixaOk = false;
            for (const auto& f : mediaC.findChildFiles(juce::File::findDirectories, false)) caixaOk = caixaOk || f.getFileName() == "Fotos";
            checar(caixaOk && mediaC.getChildFile("Fotos/foto.wav").existsAsFile(), "reorganize: the folder on disk is now 'Fotos' (case changed)");
            checar(mediaC.getChildFile(juce::String(cons::kPastaSemPasta) + "/velha.wav").existsAsFile(), "reorganize: the deleted folder's file is in _SEM_PASTA");
            checar(!mediaC.getChildFile("Shows").exists() && !mediaC.getChildFile("Velha").exists() && !mediaC.getChildFile("C").exists() &&
                       mediaC.getChildFile("A/duas.wav").existsAsFile(),
                   "reorganize: emptied folders are removed, a folder with a file left stays");
            auto cont = [&](const std::string& sql) { auto st = regc.prepare(sql); st.step(); return static_cast<int>(st.columnInt(0)); };
            checar(cont("SELECT COUNT(*) FROM consolidacao_registro WHERE caminho_relativo_destino = 'Concerts/2027/show.wav'") == 1 &&
                       cont("SELECT COUNT(*) FROM consolidacao_registro") == 6,
                   "reorganize: the registry points to the new paths and keeps all 6 copies");
            checar(cont("SELECT COUNT(*) FROM clone_move_pendente WHERE clone_id = 'clone3'") == 5, "reorganize: the clone gets the 5 moves for its next sync");
            auto depois = planejarC();
            bool nadaACopiar = true;
            for (const auto& ip : depois.itens) nadaACopiar = nadaACopiar && ip.jaConsolidado;
            checar(depois.movimentosSemPasta.empty() && nadaACopiar, "reorganize: the next plan has nothing to move and nothing to copy");
        }

        // ---- 7) Reorganizar MAIN por REGRA (reorganizarMain): SUBJECT editado na ficha e estrutura trocada no
        // REORGANIZE MAIN viram moves — nunca recópia; sem a flag (outros chamadores) e no rebuild, nada muda.
        {
            matriz::model::NovoProjetoParams params4;
            params4.nome = "Regra";
            params4.prefixoNomenclatura = "RGR";
            auto projeto4 = matriz::model::Project::criar(raiz.getChildFile("P4"), params4);
            const std::string projetoId4 = projeto4->projetoId();
            ProjetoAberto pr(std::move(projeto4));
            auto& regr = pr.projeto().registro();
            const juce::File mediaR = pr.projeto().pastaMedia();
            const juce::File s4 = raiz.getChildFile("SOURCE4");
            const juce::File g1 = s4.getChildFile("um.wav"), g2 = s4.getChildFile("dois.wav");
            escreverArquivo(g1, 31, 41000);
            escreverArquivo(g2, 32, 42000);
            const auto r1 = inserirItem(regr, projetoId4, "RGR-1", g1), r2 = inserirItem(regr, projetoId4, "RGR-2", g2);
            regr.run("UPDATE item SET dc_subject = 'Rock' WHERE id = ?", {Value::of(r1)});
            regr.run("UPDATE item SET dc_subject = 'Jazz' WHERE id = ?", {Value::of(r2)});
            const auto porSubject = cons::HierarquiaBackup{cons::NivelHierarquia::Subject};
            const auto porTipo = cons::HierarquiaBackup{cons::NivelHierarquia::TipoArquivo};
            auto planejarR = [&](const cons::HierarquiaBackup& h, bool reorganizar, bool estrito = false) {
                return cons::planejarConsolidacao(regr, pr.projeto().pasta(), mediaR, h, {}, cons::ModoPrefixoArquivo::Nenhum, {}, true,
                                                  false, false, false, {}, estrito, false, reorganizar);
            };
            auto dirDe = [](const juce::String& rel) { return rel.upToLastOccurrenceOf("/", false, false); };
            auto res0 = cons::executarConsolidacao(regr, pr.projeto().pasta(), mediaR, planejarR(porSubject, true));
            checar(res0.consolidados == 2 && res0.falhas.empty(), "rule reorganize: first backup by SUBJECT");
            checar(planejarR(porSubject, true).movimentosSemPasta.empty(), "rule reorganize: nothing to move right after a backup");

            // SUBJECT editado: só o item editado muda de pasta.
            regr.run("UPDATE item SET dc_subject = 'Blues' WHERE id = ?", {Value::of(r1)});
            checar(planejarR(porSubject, false).movimentosSemPasta.empty(), "rule reorganize: without the flag nothing is moved (other callers)");
            checar(planejarR(porSubject, true, /*estrito*/ true).movimentosSemPasta.empty(), "rule reorganize: never during a rebuild");
            auto p1 = planejarR(porSubject, true);
            checar(p1.movimentosSemPasta.size() == 1 && p1.movimentosSemPasta[0].itemId == r1 && p1.movimentosSemPasta[0].reorganizacao &&
                       dirDe(p1.movimentosSemPasta[0].moverDe) == "Rock" && dirDe(p1.movimentosSemPasta[0].caminhoRelativoDestino) == "Blues",
                   "rule reorganize: edited SUBJECT -> 1 move Rock/ -> Blues/");
            bool copiaNova = false;
            for (const auto& ip : p1.itens) copiaNova = copiaNova || !ip.jaConsolidado;
            checar(!copiaNova, "rule reorganize: nothing is planned to be copied again");
            const juce::File antes = mediaR.getChildFile(p1.movimentosSemPasta[0].moverDe);
            const std::string shaAntes = sha(antes);
            auto mv1 = cons::executarMovimentosSemPasta(regr, pr.projeto().pasta(), mediaR, p1.movimentosSemPasta);
            const juce::File depoisF = mediaR.getChildFile(p1.movimentosSemPasta[0].caminhoRelativoDestino);
            checar(mv1.movidos == 1 && mv1.falhas.empty() && depoisF.existsAsFile() && sha(depoisF) == shaAntes &&
                       !mediaR.getChildFile("Rock").exists(),
                   "rule reorganize: file moved with the same SHA-256, emptied folder removed");

            // Estrutura trocada (SUBJECT -> tipo de arquivo): os dois vão para WAV/.
            auto p2 = planejarR(porTipo, true);
            bool todosWav = p2.movimentosSemPasta.size() == 2;
            for (const auto& m : p2.movimentosSemPasta) todosWav = todosWav && dirDe(m.caminhoRelativoDestino) == "WAV";
            checar(todosWav, "rule reorganize: new structure -> both files move to WAV/");
            auto mv2 = cons::executarMovimentosSemPasta(regr, pr.projeto().pasta(), mediaR, p2.movimentosSemPasta);
            auto cont = [&](const std::string& sql) { auto st = regr.prepare(sql); st.step(); return static_cast<int>(st.columnInt(0)); };
            checar(mv2.movidos == 2 && mv2.falhas.empty() && cont("SELECT COUNT(*) FROM consolidacao_registro") == 2 &&
                       cont("SELECT COUNT(*) FROM consolidacao_registro WHERE caminho_relativo_destino LIKE 'WAV/%'") == 2,
                   "rule reorganize: registry follows, still 2 copies");
            auto p3 = planejarR(porTipo, true);
            bool nada = p3.movimentosSemPasta.empty();
            for (const auto& ip : p3.itens) nada = nada && ip.jaConsolidado;
            checar(nada, "rule reorganize: the next plan has nothing to move and nothing to copy");
        }
    } catch (const std::exception& e) {
        std::cout << "  FAIL exception: " << e.what() << "\n";
        ++falhas;
    }
    raiz.deleteRecursively();
    std::cout << (falhas == 0 ? "== Rebuild: ALL OK ==\n" : "== Rebuild: FAILURES: " + std::to_string(falhas) + " ==\n");
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
