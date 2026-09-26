#include "Resolucao.h"

namespace matriz::vault {

namespace {

// Candidatos de PROVENIÊNCIA, na ordem 3 → 4 → 5 descrita no header.
std::vector<juce::File> candidatosDeOrigem(const juce::File& pastaProjeto, const std::string& localizacaoVault,
                                           const std::string& caminhoRelativo,
                                           const std::string& caminhoAbsolutoOrigem) {
    std::vector<juce::File> out;

    // 3. SOURCE: vault registrado + caminho relativo à raiz do volume.
    if (!localizacaoVault.empty() && !caminhoRelativo.empty())
        out.push_back(juce::File(juce::String(localizacaoVault)).getChildFile(juce::String(caminhoRelativo)));

    // 4. Caminho absoluto de origem.
    if (!caminhoAbsolutoOrigem.empty() && juce::File::isAbsolutePath(juce::String(caminhoAbsolutoOrigem)))
        out.push_back(juce::File(juce::String(caminhoAbsolutoOrigem)));

    // 5. Legado: o ingest antigo copiava pra dentro do projeto.
    if (!caminhoRelativo.empty()) {
        juce::File pastaRaiz = pastaProjeto.getParentDirectory();
        if (pastaRaiz.isDirectory()) {
            out.push_back(pastaRaiz.getChildFile("Media").getChildFile(juce::String(caminhoRelativo)));
            out.push_back(pastaRaiz.getChildFile(juce::String(caminhoRelativo)));
        }
        if (pastaProjeto.isDirectory()) {
            out.push_back(pastaProjeto.getChildFile("Media").getChildFile(juce::String(caminhoRelativo)));
            out.push_back(pastaProjeto.getChildFile(juce::String(caminhoRelativo)));
        }
    }
    return out;
}

std::optional<juce::File> primeiroExistente(const std::vector<juce::File>& lista) {
    for (auto& f : lista)
        if (f.existsAsFile()) return f;
    return std::nullopt;
}

struct RegistroDestino {
    std::string caminhoRelativoDestino;
    std::string destinoId;
};

// Candidatos no MAIN e nos CLONEs (ordem 1 → 2). Em cada destino, primeiro os
// registros daquele destino, depois os legados (destino_id vazio) e os dos
// demais — um CLONE é espelho do MAIN, então o caminho de lá vale aqui.
void acrescentarCandidatosDeBackup(std::vector<juce::File>& out, const std::vector<DestinoDeBackup>& destinos,
                                   const std::vector<RegistroDestino>& registros) {
    if (registros.empty()) return;
    for (const auto& d : destinos) {
        juce::File media = d.raiz.getChildFile("Media");
        for (int passo = 0; passo < 2; ++passo) {
            for (const auto& r : registros) {
                if (r.caminhoRelativoDestino.empty()) continue;
                const bool desteDestino = !r.destinoId.empty() && r.destinoId == d.destinationId;
                if ((passo == 0) != desteDestino) continue;
                out.push_back(media.getChildFile(juce::String(r.caminhoRelativoDestino)));
            }
        }
    }
}

bool temColuna(matriz::db::Database& db, const std::string& tabela, const std::string& coluna) {
    try {
        auto st = db.prepare("SELECT 1 FROM pragma_table_info('" + tabela + "') WHERE name = ?");
        st.bind(1, matriz::db::Value::of(coluna));
        return st.step();
    } catch (...) {
        return false;
    }
}

std::string colunaDestinoId(matriz::db::Database& db) {
    // Bancos antigos (ou abertos sem migração) não têm a coluna.
    return temColuna(db, "consolidacao_registro", "destino_id") ? "COALESCE(destino_id, '')" : "''";
}

} // namespace

std::string destinationIdDaRaiz(const juce::File& raiz) {
    auto json = raiz.getChildFile("destination.json");
    if (!json.existsAsFile()) return {};
    auto v = juce::JSON::parse(json);
    if (!v.isObject()) return {};
    return v.getProperty("destination_id", "").toString().toStdString();
}

std::vector<DestinoDeBackup> destinosDeBackup(matriz::db::Database& registro, const juce::File& pastaProjeto) {
    std::vector<DestinoDeBackup> main, clones;
    const juce::File raizAberta = pastaProjeto.getParentDirectory();
    const std::string idDaRaizAberta = destinationIdDaRaiz(raizAberta);
    try {
        auto st = registro.prepare(
            "SELECT COALESCE(destination_id, id), destino_path, COALESCE(papel, 'CLONE') FROM backup_destino "
            "WHERE ativo = 1");
        while (st.step()) {
            DestinoDeBackup d;
            d.destinationId = st.columnText(0);
            // Identidade pelo destination_id, nunca pelo caminho: se a raiz
            // aberta É este destino, vale a montagem atual (o disco pode ter
            // montado com outro nome em /Volumes).
            if (!idDaRaizAberta.empty() && d.destinationId == idDaRaizAberta)
                d.raiz = raizAberta;
            else
                d.raiz = juce::File(juce::String(st.columnText(1)));
            d.ehMain = st.columnText(2) == "ORIGINAL";
            (d.ehMain ? main : clones).push_back(std::move(d));
        }
    } catch (...) {}
    // Mais de um ORIGINAL registrado (bancos antigos): o da raiz aberta vem
    // primeiro; os demais são tratados como candidatos seguintes.
    std::stable_sort(main.begin(), main.end(), [&](const DestinoDeBackup& a, const DestinoDeBackup& b) {
        return (a.destinationId == idDaRaizAberta) > (b.destinationId == idDaRaizAberta);
    });
    main.insert(main.end(), clones.begin(), clones.end());
    return main;
}

std::optional<juce::File> resolverCaminho(const juce::File& pastaProjeto, const std::string& localizacaoVault,
                                          const std::string& caminhoRelativo,
                                          const std::string& caminhoAbsolutoOrigem) {
    return primeiroExistente(candidatosDeOrigem(pastaProjeto, localizacaoVault, caminhoRelativo, caminhoAbsolutoOrigem));
}

juce::File caminhoEsperado(const juce::File& pastaProjeto, const std::string& localizacaoVault,
                           const std::string& caminhoRelativo, const std::string& caminhoAbsolutoOrigem) {
    auto lista = candidatosDeOrigem(pastaProjeto, localizacaoVault, caminhoRelativo, caminhoAbsolutoOrigem);
    if (auto f = primeiroExistente(lista)) return *f;
    return lista.empty() ? juce::File() : lista.front();
}

std::optional<juce::File> resolverArquivo(matriz::db::Database& registro, const std::string& arquivoId,
                                          const juce::File& pastaProjeto, Preferencia preferencia) {
    auto stmt = registro.prepare(std::string("SELECT ") + colunasDeResolucao() +
                                 " FROM arquivo a " + joinDeResolucao() + " WHERE a.id = ?");
    stmt.bind(1, matriz::db::Value::of(arquivoId));
    if (!stmt.step()) return std::nullopt;
    const std::string loc = stmt.columnText(0), rel = stmt.columnText(1), abs = stmt.columnText(2);

    std::vector<juce::File> candidatos;
    if (preferencia == Preferencia::MainPrimeiro) {
        std::vector<RegistroDestino> registros;
        try {
            auto st = registro.prepare("SELECT caminho_relativo_destino, " + colunaDestinoId(registro) +
                                       " FROM consolidacao_registro WHERE arquivo_id = ?");
            st.bind(1, matriz::db::Value::of(arquivoId));
            while (st.step()) registros.push_back({st.columnText(0), st.columnText(1)});
        } catch (...) {}
        if (!registros.empty())
            acrescentarCandidatosDeBackup(candidatos, destinosDeBackup(registro, pastaProjeto), registros);
    }
    auto origem = candidatosDeOrigem(pastaProjeto, loc, rel, abs);
    candidatos.insert(candidatos.end(), origem.begin(), origem.end());
    return primeiroExistente(candidatos);
}

juce::File caminhoEsperadoArquivo(matriz::db::Database& registro, const std::string& arquivoId,
                                  const juce::File& pastaProjeto) {
    if (auto f = resolverArquivo(registro, arquivoId, pastaProjeto)) return *f;
    auto stmt = registro.prepare(std::string("SELECT ") + colunasDeResolucao() +
                                 " FROM arquivo a " + joinDeResolucao() + " WHERE a.id = ?");
    stmt.bind(1, matriz::db::Value::of(arquivoId));
    if (!stmt.step()) return {};
    return caminhoEsperado(pastaProjeto, stmt.columnText(0), stmt.columnText(1), stmt.columnText(2));
}

ResolvedorEmLote::ResolvedorEmLote(matriz::db::Database& registro, const juce::File& pastaProjeto)
    : pastaProjeto_(pastaProjeto) {
    destinos_ = destinosDeBackup(registro, pastaProjeto);
    try {
        auto st = registro.prepare("SELECT arquivo_id, caminho_relativo_destino, " + colunaDestinoId(registro) +
                                   " FROM consolidacao_registro");
        while (st.step())
            registrosPorArquivo_[st.columnText(0)].push_back({st.columnText(1), st.columnText(2)});
    } catch (...) {}
}

std::optional<juce::File> ResolvedorEmLote::resolver(const std::string& arquivoId,
                                                     const std::string& localizacaoVault,
                                                     const std::string& caminhoRelativo,
                                                     const std::string& caminhoAbsolutoOrigem) const {
    std::vector<juce::File> candidatos;
    auto it = registrosPorArquivo_.find(arquivoId);
    if (it != registrosPorArquivo_.end()) {
        std::vector<RegistroDestino> registros;
        for (const auto& r : it->second) registros.push_back({r.caminhoRelativoDestino, r.destinoId});
        acrescentarCandidatosDeBackup(candidatos, destinos_, registros);
    }
    auto origem = candidatosDeOrigem(pastaProjeto_, localizacaoVault, caminhoRelativo, caminhoAbsolutoOrigem);
    candidatos.insert(candidatos.end(), origem.begin(), origem.end());
    return primeiroExistente(candidatos);
}

} // namespace matriz::vault
