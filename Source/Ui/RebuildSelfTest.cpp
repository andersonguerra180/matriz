#include "RebuildSelfTest.h"

#include <JuceHeader.h>

#include <iostream>
#include <map>

#include "../Consolidacao/Consolidacao.h"
#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "../Vault/Resolucao.h"
#include "ProjetoAberto.h"

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
    } catch (const std::exception& e) {
        std::cout << "  FAIL exception: " << e.what() << "\n";
        ++falhas;
    }
    raiz.deleteRecursively();
    std::cout << (falhas == 0 ? "== Rebuild: ALL OK ==\n" : "== Rebuild: FAILURES: " + std::to_string(falhas) + " ==\n");
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
