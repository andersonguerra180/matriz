#include "VerifySource.h"
#include "../Ingest/Checksum.h"
#include <set>

namespace matriz::vault {

namespace {

bool temComponenteOculto(const juce::File& f) {
    const auto partes = juce::StringArray::fromTokens(f.getFullPathName(), "/", "");
    for (const auto& p : partes)
        if (p.startsWithChar('.') || p == "$RECYCLE.BIN" || p == "System Volume Information") return true;
    return false;
}

} // namespace

bool VerifySource::deveIgnorar(const juce::File& f) {
    if (temComponenteOculto(f)) return true;
    const auto ext = f.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    return ext == "sfk" || ext == "reapeaks" || ext == "asd";
}

std::vector<juce::String> VerifySource::volumesDeOrigem(matriz::db::Database& db) {
    std::set<juce::String> nomes;
    try {
        auto st = db.prepare("SELECT DISTINCT caminho_absoluto_origem FROM arquivo "
                             "WHERE caminho_absoluto_origem LIKE '/Volumes/%'");
        while (st.step()) {
            const auto partes = juce::StringArray::fromTokens(juce::String(st.columnText(0)), "/", "");
            if (partes.size() >= 2 && partes[0] == "Volumes" && partes[1].isNotEmpty()) nomes.insert(partes[1]);
        }
    } catch (...) {}
    return {nomes.begin(), nomes.end()};
}

HashesConhecidos VerifySource::carregarHashes(matriz::db::Database& db) {
    HashesConhecidos h;
    try {
        std::string mainMediaPath, mainDestinationId;
        {
            auto main = db.prepare("SELECT destino_path, COALESCE(destination_id, id) FROM backup_destino "
                                   "WHERE papel = 'ORIGINAL' ORDER BY ativo DESC LIMIT 1");
            if (main.step()) {
                const juce::File raizMain(juce::String::fromUTF8(main.columnText(0).c_str()));
                if (raizMain != juce::File())
                    mainMediaPath = raizMain.getChildFile("Media").getFullPathName().trimCharactersAtEnd("/").toStdString();
                mainDestinationId = main.columnText(1);
            }
        }
        auto st = db.prepare(
            "SELECT a.checksum_sha256, CASE WHEN EXISTS ("
            "SELECT 1 FROM consolidacao_registro c WHERE c.arquivo_id = a.id AND ("
            "COALESCE(c.destino_path, '') = ? OR COALESCE(c.destino_path, '') = '' OR "
            "(? != '' AND COALESCE(c.destino_id, '') = ?))) THEN 1 ELSE 0 END "
            "FROM arquivo a WHERE a.checksum_sha256 IS NOT NULL AND a.checksum_sha256 != ''");
        st.bind(1, matriz::db::Value::of(mainMediaPath));
        st.bind(2, matriz::db::Value::of(mainDestinationId));
        st.bind(3, matriz::db::Value::of(mainDestinationId));
        while (st.step()) {
            const auto sha = st.columnText(0);
            if (st.columnInt(1) != 0) {
                h.soCatalogo.erase(sha);
                h.noMain.insert(sha);
            } else if (!h.noMain.count(sha)) {
                h.soCatalogo.insert(sha);
            }
        }
    } catch (...) {}
    try {
        auto st = db.prepare("SELECT sha256 FROM intake_rejeitados");
        while (st.step()) h.rejeitados.insert(st.columnText(0));
    } catch (...) {}
    return h;
}

ResultadoVerificacaoFonte VerifySource::verificar(const juce::File& raiz, const HashesConhecidos& conhecidos,
                                                  const std::atomic<bool>& cancelar,
                                                  const std::function<void(int, int, const juce::String&)>& aoProgredir) {
    ResultadoVerificacaoFonte r;
    std::vector<juce::File> arquivos;
    for (const auto& entry : juce::RangedDirectoryIterator(raiz, true, "*", juce::File::findFiles)) {
        if (cancelar.load()) { r.cancelado = true; return r; }
        const auto f = entry.getFile();
        if (!deveIgnorar(f) && f.getSize() > 0) arquivos.push_back(f);
    }

    const int total = static_cast<int>(arquivos.size());
    std::unordered_set<std::string> jaListados;  // a mesma cópia em duas pastas do disco aparece uma vez só
    int feitos = 0;
    for (const auto& f : arquivos) {
        if (cancelar.load()) { r.cancelado = true; return r; }
        if (aoProgredir) aoProgredir(feitos, total, f.getFileName());
        ++feitos;

        bool abortado = false;
        const auto sha = matriz::ingest::sha256DoArquivo(f, [&] { return !cancelar.load(); }, &abortado);
        if (abortado) { r.cancelado = true; return r; }
        if (sha.empty()) { ++r.ilegiveis; continue; }

        ++r.totalLidos;
        if (conhecidos.noMain.count(sha)) { ++r.totalNoMain; continue; }
        if (conhecidos.soCatalogo.count(sha)) { ++r.totalSoCatalogo; continue; }
        if (!jaListados.insert(sha).second) continue;

        FaltanteFonte fa;
        fa.arquivo = f;
        fa.sha256 = sha;
        fa.tamanhoBytes = f.getSize();
        fa.rejeitadoAntes = conhecidos.rejeitados.count(sha) > 0;
        r.faltantes.push_back(std::move(fa));
    }
    if (aoProgredir) aoProgredir(total, total, {});
    return r;
}

} // namespace matriz::vault
