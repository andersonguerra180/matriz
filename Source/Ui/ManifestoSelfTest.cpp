#include "ManifestoSelfTest.h"

#include <JuceHeader.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "../Consolidacao/Consolidacao.h"
#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "BackupWorkspaceComponent.h"
#include "ProjetoAberto.h"

namespace matriz::ui {

namespace {

int inteiroDoAmbiente(const char* nome, int padrao) {
    const char* v = std::getenv(nome);
    return (v && std::atoi(v) > 0) ? std::atoi(v) : padrao;
}

// Item com o arquivo na SOURCE (caminho absoluto), como a ingestão grava —
// com o checksum da origem em arquivo.checksum_sha256.
void inserirItemComArquivo(matriz::db::Database& reg, const std::string& projetoId, const std::string& codigo,
                           const juce::File& arquivo) {
    using matriz::db::Value;
    const std::string id = matriz::model::novoUuid(), agora = matriz::model::agoraIso8601();
    reg.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em, "
            "dc_title, dc_creator, notas_livres) VALUES (?, ?, ?, ?, 'digital_audio', 'novo', ?, ?, ?, 'Embed Tester', 'embed me')",
            {Value::of(id), Value::of(projetoId), Value::of(codigo), Value::of(codigo), Value::of(agora), Value::of(agora),
             Value::of("Title " + codigo)});
    reg.run("INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, "
            "checksum_sha256, criado_em, atualizado_em) VALUES (?, ?, ?, ?, 'preservation_master', 1, ?, ?, ?, ?)",
            {Value::of(matriz::model::novoUuid()), Value::of(id), Value::of(arquivo.getFileName().toStdString()),
             Value::of(arquivo.getFullPathName().toStdString()), Value::of(static_cast<long long>(arquivo.getSize())),
             Value::of(matriz::ingest::calcularChecksums(arquivo).sha256), Value::of(agora), Value::of(agora)});
}

juce::String sha256Do(const juce::File& f) { return juce::SHA256(f).toHexString().toLowerCase(); }

} // namespace

int rodarManifestoSelfTest() {
    const int N = inteiroDoAmbiente("MATRIZ_MANIFESTO_N", 24);
    const int mb = inteiroDoAmbiente("MATRIZ_MANIFESTO_MB", 2);
    std::cout << "== Backup checksum manifest, " << N << " files of ~" << mb << " MB (embed on) ==\n";
    int falhas = 0;
    auto checar = [&](bool ok, const juce::String& descricao) {
        std::cout << (ok ? "  OK   " : "  FAIL ") << descricao << "\n";
        if (!ok) ++falhas;
    };
    using Clock = std::chrono::steady_clock;
    auto seg = [](Clock::time_point a) { return std::chrono::duration<double>(Clock::now() - a).count(); };
    using matriz::db::Value;

    juce::File raiz = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("matriz_manifesto_" + juce::Uuid().toDashedString());
    try {
        raiz.createDirectory();
        matriz::model::NovoProjetoParams params;
        params.nome = "ManifTest";
        params.prefixoNomenclatura = "MNF";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("projeto"), params);
        const std::string projetoId = projeto->projetoId();
        auto* bruto = projeto.get();
        auto& reg = bruto->registro();
        const juce::File pasta = bruto->pasta();

        // JPEGs de verdade (o embed grava XMP/EXIF neles). Só 5 imagens de ruído; o resto é cópia + 16 bytes
        // distintos no fim, pra cada arquivo ter hash próprio sem pagar a geração de N imagens.
        juce::File srcDir = raiz.getChildFile("SRC");
        srcDir.createDirectory();
        juce::Random rnd(42);
        const int lado = static_cast<int>(std::sqrt(static_cast<double>(mb) * 1024.0 * 1024.0 / 1.2));
        for (int i = 0; i < N; ++i) {
            const std::string cod = "MNF-" + std::to_string(i);
            juce::File f = srcDir.getChildFile(juce::String(cod) + ".jpg");
            if (i < 5) {
                juce::Image img(juce::Image::RGB, lado, lado, false);
                {
                    juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
                    for (int y = 0; y < lado; ++y)
                        for (int x = 0; x < lado; ++x)
                            bd.setPixelColour(x, y, juce::Colour(static_cast<juce::uint8>(rnd.nextInt(256)),
                                                                  static_cast<juce::uint8>(rnd.nextInt(256)),
                                                                  static_cast<juce::uint8>(rnd.nextInt(256))));
                }
                juce::FileOutputStream fo(f);
                fo.setPosition(0);
                fo.truncate();
                juce::JPEGImageFormat jf;
                jf.setQuality(0.8f);
                jf.writeImageToStream(img, fo);
            } else {
                srcDir.getChildFile("MNF-" + juce::String(i % 5) + ".jpg").copyFileTo(f);
                juce::FileOutputStream fo(f);
                fo.setPosition(f.getSize());
                for (int k = 0; k < 16; ++k) fo.writeByte(static_cast<char>(rnd.nextInt(256)));
            }
            inserirItemComArquivo(reg, projetoId, cod, f);
        }

        ProjetoAberto pa(std::move(projeto));
        BackupWorkspaceComponent bw(pa, {});
        const juce::File destRaiz = raiz.getChildFile("DEST"), media = destRaiz.getChildFile("Media");
        media.createDirectory();
        matriz::consolidacao::HierarquiaBackup hier = {matriz::consolidacao::NivelHierarquia::TipoMidia};
        bw.plano_ = matriz::consolidacao::planejarConsolidacao(reg, pasta, media, hier, {},
                                                                matriz::consolidacao::ModoPrefixoArquivo::Nenhum, "", true, false);
        checar(static_cast<int>(bw.plano_.itens.size()) == N, "the plan has all " + juce::String(N) + " items");

        auto t0 = Clock::now();
        auto res = matriz::consolidacao::executarConsolidacao(reg, pasta, media, bw.plano_, {}, {}, /*embutirNaCopia*/ true);
        std::cout << "  (consolidation: " << res.consolidados << " files, " << seg(t0) << " s)\n";
        checar(res.consolidados == N && res.falhas.empty(), "all files consolidated with embed");

        auto contarDivergentes = [&](const juce::String& manifesto, int& linhas) {
            int ruins = 0;
            linhas = 0;
            for (auto& l : juce::StringArray::fromLines(manifesto)) {
                if (l.isEmpty()) continue;
                ++linhas;
                const juce::String hash = l.upToFirstOccurrenceOf("  ", false, false);
                const juce::File dest = media.getChildFile(l.fromFirstOccurrenceOf("  ", false, false));
                if (!dest.existsAsFile() || sha256Do(dest) != hash) ++ruins;
            }
            return ruins;
        };
        auto hashDaOrigem = [&](const std::string& arquivoId) {
            auto st = reg.prepare("SELECT checksum_sha256 FROM arquivo WHERE id = ?");
            st.bind(1, Value::of(arquivoId));
            st.step();
            return juce::String(st.columnText(0));
        };

        int origemDifere = 0;
        for (const auto& ip : bw.plano_.itens)
            if (sha256Do(media.getChildFile(ip.caminhoRelativoDestino)) != hashDaOrigem(ip.arquivoId)) ++origemDifere;
        checar(origemDifere == N, "embed changed the delivered bytes of all files, so the SOURCE hash would fail shasum -c ("
                                      + juce::String(origemDifere) + ")");

        // (a) hash do registro (bytes finais do destino), com progresso
        int chamadas = 0, ultimo = -1;
        bool monotonico = true, cancelado = true;
        t0 = Clock::now();
        juce::String manifesto = bw.gerarManifestChecksumsBackup(
            [&](int feito, int) { ++chamadas; if (feito < ultimo) monotonico = false; ultimo = feito; return true; },
            &cancelado, media);
        const double tempoA = seg(t0);  // só a geração; conferir os arquivos depois não entra na medição
        int linhas = 0;
        int ruins = contarDivergentes(manifesto, linhas);
        checar(!cancelado && linhas == N && ruins == 0,
               "(a) manifest from the registro: " + juce::String(linhas) + " lines, " + juce::String(ruins) + " mismatches, " +
                   juce::String(tempoA, 3) + " s");
        checar(tempoA < 1.0, "(a) never re-reads the files (" + juce::String(tempoA, 3) + " s)");
        checar(chamadas == N + 1 && monotonico, "progress is called N+1 times and never goes backwards (" + juce::String(chamadas) + ")");

        // O manifesto não abre arquivo: some com os arquivos do destino e ele sai igual.
        const juce::File media2 = raiz.getChildFile("DEST_APAGADO");
        {
            const juce::File sombra = raiz.getChildFile("DEST_SOMBRA");
            media.copyDirectoryTo(sombra);  // guarda uma cópia só pra poder restaurar depois
            media.deleteRecursively();
            media.createDirectory();
            t0 = Clock::now();
            const juce::String semArquivos = bw.gerarManifestChecksumsBackup(nullptr, nullptr, media);
            const double tempoSem = seg(t0);
            int linhasSem = 0;
            for (auto& l : juce::StringArray::fromLines(semArquivos)) if (l.isNotEmpty() && !l.startsWith("#")) ++linhasSem;
            checar(linhasSem == N && tempoSem < 1.0,
                   "(b) the manifest reads no file: same " + juce::String(linhasSem) + " lines with the destination files deleted (" +
                       juce::String(tempoSem, 3) + " s)");
            media.deleteRecursively();
            sombra.moveFileTo(media);
        }
        (void) media2;

        // Item sem registro de consolidação: comentário no fim, nunca hash zerado; resumo UMA vez no log.md.
        reg.run("UPDATE consolidacao_registro SET checksum_sha256 = ''", {});
        const juce::File logf = pasta.getChildFile("log.md");
        const juce::String logAntes = logf.existsAsFile() ? logf.loadFileAsString() : juce::String();
        const juce::String semRegistro = bw.gerarManifestChecksumsBackup(nullptr, nullptr, media);
        const juce::String logDepois = logf.existsAsFile() ? logf.loadFileAsString() : juce::String();
        int comentarios = 0, hashes = 0;
        bool hashZerado = false;
        for (auto& l : juce::StringArray::fromLines(semRegistro)) {
            if (l.startsWith("# not consolidated: ")) ++comentarios;
            else if (l.isNotEmpty()) { ++hashes; if (l.startsWith("0000000000000000")) hashZerado = true; }
        }
        checar(comentarios == N && hashes == 0 && !hashZerado,
               "(c) items with no consolidation record become '# not consolidated' comments, never a zero hash (" +
                   juce::String(comentarios) + ")");
        const juce::String marca = "Checksum manifest: items not consolidated";
        checar(logDepois.length() > logAntes.length() && logDepois.contains(marca) &&
                   logDepois.fromFirstOccurrenceOf(marca, false, false).fromFirstOccurrenceOf(marca, false, false).isEmpty(),
               "(c) logs exactly one summary entry in log.md");
        // Sem destino acessível: também só comentários (nada de hash da SOURCE).
        {
            const juce::String semDestino = bw.gerarManifestChecksumsBackup(nullptr, nullptr, juce::File());
            int linhasComHash = 0;
            for (auto& l : juce::StringArray::fromLines(semDestino)) if (l.isNotEmpty() && !l.startsWith("#")) ++linhasComHash;
            checar(linhasComHash == 0, "with no destination folder there is no hash line at all (no SOURCE hash fallback)");
        }

        // exportarChecksumsPara: cancelamento no meio e sucesso
        const juce::File rel = pasta.getChildFile("relatorios");
        rel.createDirectory();
        const juce::File sha = rel.getChildFile("ManifTest.sha256"), tmp = rel.getChildFile("ManifTest.sha256.tmp");
        sha.replaceWithText("MANIFESTO ANTIGO");
        bool ok = bw.exportarChecksumsPara(rel, media, [&](int feito, int) { return feito < 5; });
        checar(!ok && !sha.exists() && !tmp.exists(), "cancelling mid-manifest leaves no .sha256 (the previous one is removed) and no .tmp");
        reg.run("UPDATE consolidacao_registro SET checksum_sha256 = (SELECT checksum_sha256 FROM arquivo a "
                "WHERE a.id = consolidacao_registro.arquivo_id)", {});
        ok = bw.exportarChecksumsPara(rel, media, nullptr);
        checar(ok && sha.existsAsFile() && !tmp.exists(), "a completed manifest is written and no .tmp is left");
#if JUCE_MAC || JUCE_LINUX
        // O mesmo comando que o usuário roda: shasum -c dentro de Media/. O registro tem o hash da ORIGEM
        // aqui (acima), então regrava o do destino antes — é o que a consolidação grava de verdade.
        for (const auto& ip : bw.plano_.itens)
            reg.run("UPDATE consolidacao_registro SET checksum_sha256 = ? WHERE arquivo_id = ?",
                    {Value::of(sha256Do(media.getChildFile(ip.caminhoRelativoDestino)).toStdString()), Value::of(ip.arquivoId)});
        bw.exportarChecksumsPara(rel, media, nullptr);
        juce::ChildProcess cp;
        const juce::String cmd = "cd \"" + media.getFullPathName() + "\" && shasum -a 256 -c \"" + sha.getFullPathName() + "\" 2>&1";
        const bool lancou = cp.start(juce::StringArray{"/bin/sh", "-c", cmd});
        const juce::String saida = cp.readAllProcessOutput();
        cp.waitForProcessToFinish(60000);
        const bool shasumOk = lancou && cp.getExitCode() == 0;
        checar(shasumOk, "`shasum -a 256 -c` passes on the delivered files (embed on)");
        if (!shasumOk) std::cout << "    shasum said: " << saida.substring(0, 300) << "\n";
#elif JUCE_WINDOWS
        for (const auto& ip : bw.plano_.itens)
            reg.run("UPDATE consolidacao_registro SET checksum_sha256 = ? WHERE arquivo_id = ?",
                    {Value::of(sha256Do(media.getChildFile(ip.caminhoRelativoDestino)).toStdString()), Value::of(ip.arquivoId)});
        bw.exportarChecksumsPara(rel, media, nullptr);
        int linhasSha = 0;
        int errosSha = contarDivergentes(sha.loadFileAsString(), linhasSha);
        checar(linhasSha == N && errosSha == 0, "`shasum -a 256 -c` equivalent passes on the delivered files (embed on)");
#endif

        // Fase final do backup (CSV, XLS, Dublin Core e manifesto) em thread de fundo — como o job do
        // BackupWorkspaceComponent — enquanto a message thread grava no banco: o que o TSan precisa ver.
        const juce::File relBg = pasta.getChildFile("relatorios_bg");
        relBg.createDirectory();
        std::atomic<bool> fim{false};
        bool manifestoBgOk = false;
        std::thread th([&] {
            bw.exportarCsvPara(relBg);
            bw.exportarXlsPara(relBg);
            bw.exportarDublinCorePara(relBg);
            manifestoBgOk = bw.exportarChecksumsPara(relBg, media, nullptr);
            fim = true;
        });
        int escritas = 0;
        while (!fim.load()) {
            reg.run("UPDATE item SET notas_livres = ? WHERE codigo_acervo = 'MNF-0'", {Value::of("w" + std::to_string(escritas++))});
            juce::Thread::sleep(2);
        }
        th.join();
        std::cout << "  (" << escritas << " writes on the message thread during the background final phase)\n";
        int tmps = 0;
        for (auto& f : relBg.findChildFiles(juce::File::findFiles, false, "*.tmp")) { (void) f; ++tmps; }
        checar(manifestoBgOk && tmps == 0 && relBg.getChildFile("ManifTest_full_report.csv").existsAsFile() &&
                   relBg.getChildFile("ManifTest_catalog.xls").existsAsFile() &&
                   relBg.getChildFile("ManifTest_dublin_core.csv").existsAsFile() &&
                   relBg.getChildFile("ManifTest.sha256").existsAsFile(),
               "the 4 reports built on a background thread complete (no .tmp left) while the message thread writes to the DB");
    } catch (const std::exception& e) {
        checar(false, juce::String("manifest selftest: ") + e.what());
    }
    raiz.deleteRecursively();
    std::cout << (falhas == 0 ? "\nALL TESTS PASSED\n" : "\n" + juce::String(falhas) + " FAILURE(S)\n");
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
