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
        auto st = db.prepare("SELECT checksum_sha256 FROM arquivo WHERE checksum_sha256 IS NOT NULL AND checksum_sha256 != ''");
        while (st.step()) h.noMain.insert(st.columnText(0));
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
