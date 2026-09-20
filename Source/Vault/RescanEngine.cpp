#include "RescanEngine.h"
#include "Volume.h"
#include "../Ui/ProjetoAberto.h"
#include "../Ingest/Checksum.h"
#include "../Ingest/LeituraTecnica.h"
#include "../Model/Project.h"
#include <set>
#include <map>
#include <regex>

namespace matriz::vault {

namespace {

bool deveIgnorarArquivo(const juce::File& f) {
    auto name = f.getFileName();
    if (name.startsWith(".") || f.getSize() == 0) return true;
    auto ext = f.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    if (ext == "sfk" || ext == "reapeaks" || ext == "asd" || ext == "ds_store") return true;
    return false;
}

void coletarArquivosRecursivo(const juce::File& dirOuArquivo, std::vector<juce::File>& lista, const std::atomic<bool>& cancel) {
    if (cancel.load()) return;

    if (!dirOuArquivo.exists()) return;

    if (!dirOuArquivo.isDirectory()) {
        if (!deveIgnorarArquivo(dirOuArquivo)) {
            lista.push_back(dirOuArquivo);
        }
        return;
    }

    for (const auto& entry : juce::RangedDirectoryIterator(dirOuArquivo, true, "*", juce::File::findFiles)) {
        if (cancel.load()) return;
        juce::File f = entry.getFile();
        if (!deveIgnorarArquivo(f)) {
            lista.push_back(f);
        }
    }
}

std::string extrairTituloBaseSemVersao(const std::string& titulo) {
    std::regex vRegex(R"(\s+\(v\d+\)$)", std::regex::icase);
    return std::regex_replace(titulo, vRegex, "");
}

} // namespace

std::vector<RescanPair> RescanEngine::obterParesDeBackup(ui::ProjetoAberto& projeto) {
    std::vector<RescanPair> pares;
    auto& db = projeto.projeto().registro();
    std::string projId = projeto.projeto().projetoId();

    try {
        matriz::vault::sincronizarDrivesDoProjeto(db, projId);
    } catch (...) {}

    // 1. Discover all distinct source paths and their associated source vaults
    std::map<juce::String, std::vector<juce::File>> rotasPorOrigem;

    try {
        auto stmt = db.prepare(
            "SELECT DISTINCT COALESCE(v.nome, ''), COALESCE(v.localizacao, ''), a.caminho_absoluto_origem "
            "FROM arquivo a "
            "LEFT JOIN vault v ON v.id = a.vault_id "
            "WHERE a.caminho_absoluto_origem IS NOT NULL AND a.caminho_absoluto_origem != '';");

        while (stmt.step()) {
            juce::String vNome = stmt.columnText(0);
            juce::String vLoc = stmt.columnText(1);
            juce::String absPath = stmt.columnText(2);

            if (absPath.isEmpty()) continue;
            juce::File arq(absPath);

            juce::String nomeOrigem = vNome;
            if (nomeOrigem.isEmpty()) {
                if (absPath.startsWith("/Volumes/")) {
                    auto partes = juce::StringArray::fromTokens(absPath, "/", "");
                    if (partes.size() >= 2 && partes[0] == "Volumes") {
                        nomeOrigem = partes[1];
                    }
                }
            }
            if (nomeOrigem.isEmpty()) {
                nomeOrigem = "Local Source Drive";
            }

            // Find directory root
            juce::File parentDir = arq.getParentDirectory();
            if (parentDir.exists()) {
                rotasPorOrigem[nomeOrigem].push_back(parentDir);
            }
        }
    } catch (...) {}

    // Deduplicate and resolve directory roots per source drive
    std::map<juce::String, std::vector<juce::File>> raizesPorOrigem;
    for (auto& pair : rotasPorOrigem) {
        std::set<juce::String> paths;
        std::vector<juce::File> roots;
        for (const auto& dir : pair.second) {
            juce::String p = dir.getFullPathName();
            if (!paths.count(p)) {
                paths.insert(p);
                roots.push_back(dir);
            }
        }
        raizesPorOrigem[pair.first] = roots;
    }

    // 2. Discover Backup Drives configured in SQLite
    std::vector<juce::String> backupDriveNames;
    try {
        auto stmtBkp = db.prepare(
            "SELECT nome, localizacao FROM vault "
            "WHERE tipo = 'backup' OR EXISTS (SELECT 1 FROM vault_evento ve WHERE ve.vault_id = vault.id) "
            "ORDER BY nome ASC;");
        while (stmtBkp.step()) {
            juce::String bName = stmtBkp.columnText(0);
            if (bName.isEmpty()) bName = stmtBkp.columnText(1);
            if (bName.isNotEmpty()) {
                backupDriveNames.push_back(bName);
            }
        }
    } catch (...) {}

    if (backupDriveNames.empty()) {
        backupDriveNames.push_back("Backup Destination (Configured)");
    }

    // 3. Build pairs
    if (raizesPorOrigem.empty()) {
        // Fallback: If no files in database yet, create a default entry with project folder
        RescanPair p;
        p.sourceDriveName = "Primary Source";
        p.backupDriveName = backupDriveNames.front();
        p.sourceRootPaths.push_back(projeto.projeto().pasta());
        pares.push_back(std::move(p));
    } else {
        int bkpIdx = 0;
        for (auto& entry : raizesPorOrigem) {
            RescanPair p;
            p.sourceDriveName = entry.first;
            p.backupDriveName = backupDriveNames[static_cast<size_t>(bkpIdx % static_cast<int>(backupDriveNames.size()))];
            p.sourceRootPaths = entry.second;
            pares.push_back(std::move(p));
            bkpIdx++;
        }
    }

    return pares;
}

std::string RescanEngine::calcularProximoTituloComVersao(matriz::db::Database& db, const std::string& tituloBase) {
    using matriz::db::Value;
    std::string base = extrairTituloBaseSemVersao(tituloBase);
    if (base.empty()) base = tituloBase;

    int totalExistentes = 0;
    try {
        auto stmt = db.prepare(
            "SELECT COUNT(*) FROM item "
            "WHERE titulo = ? OR titulo LIKE (? || ' (v%)');");
        stmt.bind(1, Value::of(base));
        stmt.bind(2, Value::of(base));
        if (stmt.step()) {
            totalExistentes = static_cast<int>(stmt.columnInt(0));
        }
    } catch (...) {
        totalExistentes = 1;
    }

    int versao = std::max(2, totalExistentes + 1);
    while (true) {
        std::string cand = base + " (v" + std::to_string(versao) + ")";
        try {
            auto stmtCheck = db.prepare("SELECT 1 FROM item WHERE titulo = ? LIMIT 1;");
            stmtCheck.bind(1, Value::of(cand));
            if (!stmtCheck.step()) {
                return cand;
            }
        } catch (...) {
            return cand;
        }
        versao++;
    }
}

int RescanEngine::contarTotalArquivos(const std::vector<RescanPair>& pares) {
    std::atomic<bool> dummyCancel{false};
    std::vector<juce::File> todos;
    for (const auto& par : pares) {
        for (const auto& root : par.sourceRootPaths) {
            coletarArquivosRecursivo(root, todos, dummyCancel);
        }
    }
    return static_cast<int>(todos.size());
}

bool RescanEngine::executarVarredura(
    const std::vector<RescanPair>& pares,
    matriz::db::Database& db,
    std::atomic<bool>& cancelRequested,
    std::function<void(int processedFiles, int totalFiles, const juce::String& currentFileName)> onProgress,
    std::vector<RescanFileResult>& outResultados) {

    using matriz::db::Value;
    outResultados.clear();

    // 1. Gather all unique files across all pairs in sequential order
    std::vector<juce::File> arquivosParaVarrer;
    std::set<juce::String> caminhosVisitados;

    for (const auto& par : pares) {
        if (cancelRequested.load()) {
            outResultados.clear();
            return false;
        }
        for (const auto& root : par.sourceRootPaths) {
            if (cancelRequested.load()) {
                outResultados.clear();
                return false;
            }
            std::vector<juce::File> temp;
            coletarArquivosRecursivo(root, temp, cancelRequested);
            for (const auto& f : temp) {
                juce::String full = f.getFullPathName();
                if (!caminhosVisitados.count(full)) {
                    caminhosVisitados.insert(full);
                    arquivosParaVarrer.push_back(f);
                }
            }
        }
    }

    int totalArquivos = static_cast<int>(arquivosParaVarrer.size());
    if (totalArquivos == 0) {
        if (onProgress) onProgress(0, 0, "");
        return true;
    }

    // 2. Process each file: compute SHA-256 and query SQLite
    std::vector<RescanFileResult> resultadosTemp;
    resultadosTemp.reserve(totalArquivos);

    for (int i = 0; i < totalArquivos; ++i) {
        if (cancelRequested.load()) {
            outResultados.clear();
            return false;
        }

        const auto& f = arquivosParaVarrer[static_cast<size_t>(i)];
        juce::String currentPath = f.getFullPathName();

        if (onProgress) {
            onProgress(i, totalArquivos, f.getFileName());
        }

        // Full SHA-256 calculation (no shortcuts as mandated by Section 3)
        auto cs = matriz::ingest::calcularChecksums(f);
        std::string sha256Calculado = cs.sha256;

        if (cancelRequested.load()) {
            outResultados.clear();
            return false;
        }

        // Compare against SQLite DB by caminho_absoluto_origem or caminho_relativo
        std::string itemId;
        std::string sha256Registrado;
        std::string tituloOriginal;
        std::string tipoMidiaOriginal;
        bool encontradoNoCatalogo = false;

        try {
            auto stmt = db.prepare(
                "SELECT a.item_id, COALESCE(a.checksum_sha256, ''), i.titulo, COALESCE(i.tipo_midia, '') "
                "FROM arquivo a "
                "JOIN item i ON i.id = a.item_id "
                "WHERE a.caminho_absoluto_origem = ? "
                "ORDER BY a.eh_master DESC, a.criado_em DESC "
                "LIMIT 1;");
            stmt.bind(1, Value::of(currentPath.toStdString()));

            if (stmt.step()) {
                encontradoNoCatalogo = true;
                itemId = stmt.columnText(0);
                sha256Registrado = stmt.columnText(1);
                tituloOriginal = stmt.columnText(2);
                tipoMidiaOriginal = stmt.columnText(3);
            }
        } catch (...) {}

        if (encontradoNoCatalogo) {
            if (sha256Registrado.empty() || sha256Calculado != sha256Registrado) {
                // MODIFICADO: Same path, different hash
                RescanFileResult res;
                res.status = RescanFileResult::Status::Modificado;
                res.file = f;
                res.caminhoAbsoluto = currentPath.toStdString();
                res.sha256Calculado = sha256Calculado;
                res.sha256Registrado = sha256Registrado;
                res.itemIdOriginal = itemId;
                res.tituloBase = tituloOriginal.empty() ? f.getFileNameWithoutExtension().toStdString() : tituloOriginal;
                res.novoTitulo = calcularProximoTituloComVersao(db, res.tituloBase);
                res.tamanhoBytes = f.getSize();
                res.extensao = f.getFileExtension().trimCharactersAtStart(".").toLowerCase().toStdString();
                res.tipoMidia = tipoMidiaOriginal;
                resultadosTemp.push_back(std::move(res));
            }
            // INALTERADO: Same path, same hash -> ignore, not added to results
        } else {
            // NOVO: Path does not exist in catalog
            RescanFileResult res;
            res.status = RescanFileResult::Status::Novo;
            res.file = f;
            res.caminhoAbsoluto = currentPath.toStdString();
            res.sha256Calculado = sha256Calculado;
            res.sha256Registrado = "";
            res.itemIdOriginal = "";
            res.tituloBase = f.getFileNameWithoutExtension().toStdString();
            res.novoTitulo = res.tituloBase;
            res.tamanhoBytes = f.getSize();
            res.extensao = f.getFileExtension().trimCharactersAtStart(".").toLowerCase().toStdString();

            auto cat = matriz::ingest::categoriaPorExtensao(f);
            switch (cat) {
                case matriz::ingest::CategoriaMidia::Audio:      res.tipoMidia = "digital_audio"; break;
                case matriz::ingest::CategoriaMidia::Video:      res.tipoMidia = "digital_video"; break;
                case matriz::ingest::CategoriaMidia::Imagem:     res.tipoMidia = "foto"; break;
                case matriz::ingest::CategoriaMidia::Documento:  res.tipoMidia = "documento"; break;
                case matriz::ingest::CategoriaMidia::Texto:      res.tipoMidia = "documento"; break;
                case matriz::ingest::CategoriaMidia::Sessao:     res.tipoMidia = "sessao"; break;
                default: break;
            }

            resultadosTemp.push_back(std::move(res));
        }
    }

    if (cancelRequested.load()) {
        outResultados.clear();
        return false;
    }

    if (onProgress) {
        onProgress(totalArquivos, totalArquivos, "Complete");
    }

    outResultados = std::move(resultadosTemp);
    return true;
}

} // namespace matriz::vault
