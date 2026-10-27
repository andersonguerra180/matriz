#include "VerifySourceSelfTest.h"

#include <JuceHeader.h>
#include <iostream>

#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "../Vault/VerifySource.h"

namespace matriz::ui {

namespace {

int falhas = 0;
void confere(bool ok, const char* o) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << o << "\n";
    if (!ok) ++falhas;
}

juce::File escrever(const juce::File& raiz, const juce::String& rel, const juce::String& conteudo) {
    auto f = raiz.getChildFile(rel);
    f.getParentDirectory().createDirectory();
    f.replaceWithText(conteudo);
    return f;
}

} // namespace

int rodarVerifySourceSelfTest() {
    using matriz::vault::VerifySource;
    falhas = 0;
    auto raiz = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("verifysource-" + juce::Uuid().toString());
    raiz.createDirectory();

    const auto noMain = escrever(raiz, "a/no_main.txt", "ja esta no main");
    const auto rej = escrever(raiz, "a/rejeitado.txt", "foi rejeitado no intake");
    escrever(raiz, "b/faltando.txt", "falta no main");
    escrever(raiz, "b/renomeado_do_main.txt", "ja esta no main");  // mesmo conteudo, outro nome: nao e faltante
    escrever(raiz, "c/faltando_copia.txt", "falta no main");        // mesma copia em outra pasta: lista uma vez
    escrever(raiz, ".oculto/x.txt", "oculto");
    escrever(raiz, "c/.DS_Store", "lixo");
    escrever(raiz, "c/vazio.txt", "");

    matriz::vault::HashesConhecidos h;
    h.noMain.insert(matriz::ingest::sha256DoArquivo(noMain));
    h.rejeitados.insert(matriz::ingest::sha256DoArquivo(rej));

    std::atomic<bool> cancelar{false};
    int ultimoTotal = 0;
    auto r = VerifySource::verificar(raiz, h, cancelar, [&](int, int total, const juce::String&) { ultimoTotal = total; });

    int faltando = 0, rejeitados = 0;
    for (const auto& f : r.faltantes) (f.rejeitadoAntes ? rejeitados : faltando)++;
    confere(!r.cancelado, "scan completo, nao cancelado");
    confere(ultimoTotal == 5, "5 arquivos considerados (oculto, .DS_Store e vazio ficam de fora)");
    confere(r.totalNoMain == 2, "2 ja estao no MAIN (inclui o renomeado)");
    confere(faltando == 1, "1 faltante (a copia repetida nao conta duas vezes)");
    confere(rejeitados == 1, "1 rejeitado antes");

    cancelar = true;
    confere(VerifySource::verificar(raiz, h, cancelar).cancelado, "cancelar interrompe o scan");

    {
        matriz::model::NovoProjetoParams params;
        params.nome = "Verify Source catalog/main split";
        params.prefixoNomenclatura = "VFY";
        auto projeto = matriz::model::Project::criar(raiz.getChildFile("verify-project"), params);
        auto& db = projeto->registro();
        const auto mainOriginal = projeto->pastaMedia().getChildFile("main.txt");
        mainOriginal.replaceWithText("bytes already in MAIN");
        const auto catalogOriginal = escrever(raiz, "verify-source/catalog-only.txt", "catalogued without MAIN copy");
        const auto src = raiz.getChildFile("verify-source-scan");
        const auto sourceMain = escrever(src, "a/main.txt", "bytes already in MAIN");
        const auto sourceCatalog = escrever(src, "a/catalog-only.txt", "catalogued without MAIN copy");
        const auto sourceMissing = escrever(src, "a/missing.txt", "not yet catalogued");
        const auto agora = matriz::model::agoraIso8601();
        const auto mainItem = matriz::model::novoUuid(), mainFileId = matriz::model::novoUuid();
        const auto catalogItem = matriz::model::novoUuid(), catalogFileId = matriz::model::novoUuid();
        auto inserir = [&](const std::string& itemId, const std::string& arquivoId, const std::string& codigo,
                           const juce::File& arquivo) {
            const auto sha = matriz::ingest::sha256DoArquivo(arquivo);
            db.run("INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, criado_em, atualizado_em) "
                   "VALUES (?, ?, ?, ?, 'documento', 'novo', ?, ?)",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(projeto->projetoId()),
                    matriz::db::Value::of(codigo), matriz::db::Value::of(codigo),
                    matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
            db.run("INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, checksum_sha256, criado_em, atualizado_em) "
                   "VALUES (?, ?, ?, ?, 'preservation_master', 1, ?, ?, ?, ?)",
                   {matriz::db::Value::of(arquivoId), matriz::db::Value::of(itemId),
                    matriz::db::Value::of(arquivo.getFileName().toStdString()),
                    matriz::db::Value::of(arquivo.getFullPathName().toStdString()),
                    matriz::db::Value::of(static_cast<long long>(arquivo.getSize())), matriz::db::Value::of(sha),
                    matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
            return sha;
        };
        const auto shaMain = inserir(mainItem, mainFileId, "VFY-1", mainOriginal);
        const auto shaCatalog = inserir(catalogItem, catalogFileId, "VFY-2", catalogOriginal);
        auto destino = db.prepare("SELECT COALESCE(destination_id, id) FROM backup_destino WHERE papel = 'ORIGINAL' LIMIT 1");
        const std::string destinoId = destino.step() ? destino.columnText(0) : std::string();
        db.run("INSERT INTO consolidacao_registro (id, item_id, arquivo_id, caminho_relativo_destino, checksum_sha256, consolidado_em, destino_path, destino_id) "
               "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
               {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(mainItem),
                matriz::db::Value::of(mainFileId), matriz::db::Value::of("main.txt"),
                matriz::db::Value::of(shaMain), matriz::db::Value::of(agora),
                matriz::db::Value::of(projeto->pastaMedia().getFullPathName().toStdString()),
                matriz::db::Value::of(destinoId)});

        auto hashes = VerifySource::carregarHashes(db);
        std::atomic<bool> verificarCancelar{false};
        auto split = VerifySource::verificar(src, hashes, verificarCancelar);
        confere(hashes.noMain.count(shaMain) == 1 && hashes.soCatalogo.count(shaCatalog) == 1,
                "catalog hashes are split by an actual MAIN consolidation record");
        confere(split.totalNoMain == 1 && split.totalSoCatalogo == 1 && split.faltantes.size() == 1 &&
                    split.faltantes.front().arquivo == sourceMissing,
                "catalogued without MAIN is counted separately and never offered for re-ingest");

        db.run("INSERT INTO consolidacao_registro (id, item_id, arquivo_id, caminho_relativo_destino, checksum_sha256, consolidado_em, destino_path, destino_id) "
               "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
               {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(catalogItem),
                matriz::db::Value::of(catalogFileId), matriz::db::Value::of("catalog-only.txt"),
                matriz::db::Value::of(shaCatalog), matriz::db::Value::of(agora),
                matriz::db::Value::of(projeto->pastaMedia().getFullPathName().toStdString()),
                matriz::db::Value::of(destinoId)});
        hashes = VerifySource::carregarHashes(db);
        split = VerifySource::verificar(src, hashes, verificarCancelar);
        confere(split.totalNoMain == 2 && split.totalSoCatalogo == 0,
                "a consolidation record for the MAIN moves the catalogued hash into already-in-MAIN");
        (void) sourceMain;
    }

    raiz.deleteRecursively();
    std::cout << (falhas == 0 ? "VERIFYSOURCE OK\n" : "VERIFYSOURCE COM FALHAS\n");
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
