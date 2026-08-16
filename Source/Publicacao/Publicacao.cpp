#include "Publicacao.h"

#include <map>

#include "../Ingest/CacheArquivo.h"
#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "../Vault/Reconciliacao.h"
#include "../Vault/Resolucao.h"

namespace matriz::publicacao {

using matriz::db::Value;

namespace {

const char* kManifestTxt = "manifest.txt";
const char* kManifestSqlite = "manifest.sqlite";
const char* kCatalogSqlite = "catalog.sqlite";
const char* kPastaExports = "exports";
const char* kPastaCache = "cache";
const char* kRelatorio = "preservation-report.txt";
const char* kSufixoPacote = ".matriz";

// Copia e RELÊ. O retorno é o SHA-256 do que ficou no destino, não o da
// origem: é a diferença entre "mandei copiar" e "está gravado certo".
// Vazio = falhou.
std::string copiarEVerificar(const juce::File& origem, const juce::File& destino, std::string& erro) {
    destino.getParentDirectory().createDirectory();
    destino.deleteFile();

    if (!origem.copyFileTo(destino)) {
        erro = "could not copy to " + destino.getFullPathName().toStdString();
        return {};
    }
    if (!destino.existsAsFile()) {
        erro = "the copy does not exist after copying";
        return {};
    }
    if (destino.getSize() != origem.getSize()) {
        erro = "copy size does not match the original";
        return {};
    }

    auto hashOrigem = matriz::ingest::calcularChecksums(origem);
    auto hashDestino = matriz::ingest::calcularChecksums(destino);
    if (hashOrigem.sha256 != hashDestino.sha256) {
        erro = "SHA-256 read back from the destination does not match the original";
        return {};
    }
    return hashDestino.sha256;
}

void escreverBytes(const juce::File& destino, const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) return;
    destino.getParentDirectory().createDirectory();
    destino.deleteFile();
    juce::FileOutputStream saida(destino);
    if (saida.openedOk()) saida.write(bytes.data(), bytes.size());
}

std::string valorDoProjeto(matriz::db::Database& registro, const char* coluna) {
    auto stmt = registro.prepare(std::string("SELECT IFNULL(") + coluna + ", '') FROM projeto LIMIT 1");
    return stmt.step() ? stmt.columnText(0) : std::string();
}

} // namespace

bool ehPacotePublicado(const juce::File& pasta) {
    if (pasta.getChildFile(kManifestTxt).existsAsFile()) return true;
    for (const auto& e : juce::RangedDirectoryIterator(pasta, false, "*" + juce::String(kSufixoPacote),
                                                        juce::File::findDirectories))
        if (e.getFile().getChildFile(kManifestTxt).existsAsFile()) return true;
    return false;
}

ResultadoPublicacao publicar(matriz::db::Database& registro, const juce::File& pastaProjeto,
                              const ParamsPublicacao& params, const AoProgredir& aoProgredir) {
    ResultadoPublicacao resultado;

    juce::String nome = params.nomePacote.isNotEmpty() ? params.nomePacote : juce::String("Projeto");
    juce::File pacote = params.pastaDestino.getChildFile(nome + kSufixoPacote);
    juce::File exports = pacote.getChildFile(kPastaExports);
    juce::File cache = pacote.getChildFile(kPastaCache);
    pacote.createDirectory();
    exports.createDirectory();
    cache.createDirectory();
    resultado.pacote = pacote;

    std::string projetoId;
    {
        auto stmt = registro.prepare("SELECT id FROM projeto LIMIT 1");
        if (stmt.step()) projetoId = stmt.columnText(0);
    }

    // A publicação nasce 'planejada' e só vira 'concluida' depois que TUDO
    // foi relido e conferido. Um pacote interrompido no meio fica marcado
    // como o que é, em vez de passar por backup bom.
    resultado.publicacaoId = matriz::model::novoUuid();
    registro.run(
        "INSERT INTO publicacao (id, projeto_id, vault_id, autor, layout_pasta, criada_em, status, nota) "
        "VALUES (?, ?, ?, ?, ?, ?, 'planejada', ?)",
        {Value::of(resultado.publicacaoId), Value::of(projetoId), Value::of(params.vaultId),
         Value::of(params.autor),
         Value::of(matriz::consolidacao::hierarquiaParaCsv(
             params.hierarquia.empty() ? matriz::consolidacao::hierarquiaDoProjeto(registro) : params.hierarquia)),
         Value::of(matriz::model::agoraIso8601()), Value::of(params.nota)});

    // O plano de consolidação já resolve máscara e hierarquia — publicar usa
    // os mesmos caminhos relativos, dentro de exports/.
    auto plano = matriz::consolidacao::planejarConsolidacao(registro, pastaProjeto, exports, params.hierarquia,
                                                             params.rotuloTipoMidia);

    // caminho relativo ao pacote -> sha256. É daqui que sai o manifest.
    std::map<std::string, std::string> manifesto;
    const int total = static_cast<int>(plano.itens.size());
    int feito = 0;

    for (const auto& item : plano.itens) {
        if (aoProgredir && !aoProgredir(feito, total)) {
            resultado.cancelada = true;
            break;
        }
        ++feito;

        if (item.emConflito) {
            resultado.falhas.push_back(item.codigoAcervo + ": destination path clashes with another item");
            continue;
        }

        auto origem = matriz::vault::resolverArquivo(registro, item.arquivoId, pastaProjeto);
        if (!origem) {
            // Vault offline ou arquivo ausente: não dá pra publicar o que não
            // se consegue ler. Falha explícita — publicar um pacote sem o
            // arquivo e chamar de backup seria o pior resultado possível.
            resultado.falhas.push_back(item.codigoAcervo + ": file unavailable (Vault offline or missing)");
            continue;
        }

        std::string relativo = std::string(kPastaExports) + "/" + item.caminhoRelativoDestino.toStdString();
        juce::File destino = pacote.getChildFile(juce::String(relativo));

        std::string erro;
        std::string sha = copiarEVerificar(*origem, destino, erro);
        if (sha.empty()) {
            resultado.falhas.push_back(item.codigoAcervo + ": " + erro);
            continue;
        }

        manifesto[relativo] = sha;
        registro.run(
            "INSERT INTO item_publicacao (item_id, publicacao_id, caminho_relativo, checksum_validado, verificado_em) "
            "VALUES (?, ?, ?, ?, ?) ON CONFLICT(item_id, publicacao_id) DO UPDATE SET "
            "caminho_relativo = excluded.caminho_relativo, checksum_validado = excluded.checksum_validado, "
            "verificado_em = excluded.verificado_em",
            {Value::of(item.itemId), Value::of(resultado.publicacaoId), Value::of(relativo), Value::of(sha),
             Value::of(matriz::model::agoraIso8601())});

        matriz::vault::registrarProveniencia(registro, item.itemId, "publicado", params.autor,
                                              "pacote " + pacote.getFileName().toStdString() + " -> " + relativo);
        ++resultado.itensGravados;

        // cache/: miniatura e forma de onda serializadas, pra o pacote ser
        // navegável sem os originais nem este software (I3 levado ao
        // pacote, não só ao projeto).
        if (auto analise = matriz::ingest::lerCache(registro, item.arquivoId)) {
            juce::File dirCache = cache.getChildFile(juce::String(item.arquivoId));
            escreverBytes(dirCache.getChildFile("thumb.png"), analise->miniatura);
            escreverBytes(dirCache.getChildFile("waveform.bin"), analise->formaOnda);
        }
    }

    // ---- manifest.txt: a fonte da verdade ----
    // Formato de `shasum -a 256`: "<sha256>  <caminho>". Escolhido por ser
    // conferível com uma ferramenta que existe em qualquer Unix desde
    // sempre — um manifesto que só o nosso software lê não preserva nada.
    {
        juce::String texto;
        texto << "# BKR Matriz preservation manifest\n";
        texto << "# package: " << pacote.getFileName() << "\n";
        texto << "# created: " << juce::String(matriz::model::agoraIso8601()) << "\n";
        texto << "# algorithm: SHA-256\n";
        texto << "# verify with: shasum -a 256 -c " << kManifestTxt << "\n";
        for (const auto& [caminho, sha] : manifesto) texto << juce::String(sha) << "  " << juce::String(caminho) << "\n";
        pacote.getChildFile(kManifestTxt).replaceWithText(texto, false, false, "\n");
    }

    // ---- manifest.sqlite: o mesmo conteúdo, indexado ----
    {
        juce::File arquivo = pacote.getChildFile(kManifestSqlite);
        arquivo.deleteFile();
        matriz::db::Database manifestoDb(arquivo.getFullPathName().toStdString());
        manifestoDb.execScript(
            "CREATE TABLE IF NOT EXISTS manifesto ("
            "  caminho_relativo TEXT PRIMARY KEY,"
            "  checksum_sha256  TEXT NOT NULL,"
            "  verificado_em    TEXT NOT NULL);");
        manifestoDb.run("BEGIN", {});
        for (const auto& [caminho, sha] : manifesto)
            manifestoDb.run(
                "INSERT OR REPLACE INTO manifesto (caminho_relativo, checksum_sha256, verificado_em) VALUES (?, ?, ?)",
                {Value::of(caminho), Value::of(sha), Value::of(matriz::model::agoraIso8601())});
        manifestoDb.run("COMMIT", {});
    }

    // ---- catalog.sqlite: o conhecimento ----
    // VACUUM INTO, não cópia de arquivo: com WAL ligado, copiar o .sqlite
    // solto pode capturar um banco sem as transações que ainda estão no
    // -wal. VACUUM INTO produz um banco íntegro e compactado, e o pacote
    // não é modificável mesmo.
    {
        juce::File arquivo = pacote.getChildFile(kCatalogSqlite);
        arquivo.deleteFile();
        try {
            auto stmt = registro.prepare("VACUUM INTO ?");
            stmt.bind(1, Value::of(arquivo.getFullPathName().toStdString()));
            stmt.step();
        } catch (const std::exception& e) {
            resultado.falhas.push_back(std::string("catalog.sqlite: ") + e.what());
        }
    }

    // ---- Preservation report ----
    {
        juce::String rel;
        rel << "BKR MATRIZ - PRESERVATION REPORT\n";
        rel << "================================\n\n";
        rel << "Package:     " << pacote.getFileName() << "\n";
        rel << "Project:     " << juce::String(valorDoProjeto(registro, "nome")) << "\n";
        rel << "Institution: " << juce::String(valorDoProjeto(registro, "instituicao_ou_selo")) << "\n";
        rel << "Operator:    " << juce::String(params.autor) << "\n";
        rel << "Created:     " << juce::String(matriz::model::agoraIso8601()) << "\n";
        rel << "Publication: " << juce::String(resultado.publicacaoId) << "\n\n";

        rel << "INTEGRITY\n---------\n";
        rel << "Files written and verified: " << resultado.itensGravados << "\n";
        rel << "Planned items:              " << total << "\n";
        rel << "Failures:                   " << static_cast<int>(resultado.falhas.size()) << "\n";
        rel << "Every file above was read back from the destination and its SHA-256\n";
        rel << "recomputed before this report was written.\n\n";

        if (!resultado.falhas.empty()) {
            rel << "FAILURES\n--------\n";
            for (const auto& f : resultado.falhas) rel << "  - " << juce::String(f) << "\n";
            rel << "\n";
        }

        // Marcadores de auditoria ainda abertos: o pacote pode estar íntegro
        // byte a byte e mesmo assim conter material que alguém marcou como
        // problemático. As duas coisas precisam aparecer no mesmo relatório.
        rel << "OPEN AUDIT MARKERS\n------------------\n";
        int abertos = 0;
        try {
            auto stmt = registro.prepare(
                "SELECT IFNULL(i.codigo_acervo, '(no code)'), IFNULL(m.tipo_id, 'other'), IFNULL(m.titulo, '') "
                "FROM marcador m JOIN item i ON i.id = m.item_id "
                "WHERE IFNULL(m.status, 'aberto') = 'aberto' ORDER BY i.codigo_acervo");
            while (stmt.step()) {
                rel << "  - " << juce::String(stmt.columnText(0)) << " [" << juce::String(stmt.columnText(1))
                    << "] " << juce::String(stmt.columnText(2)) << "\n";
                ++abertos;
            }
        } catch (const std::exception&) {
            // Projeto antigo sem as colunas de marcador — o relatório sai
            // sem esta seção em vez de não sair.
        }
        if (abertos == 0) rel << "  (none)\n";
        rel << "\n";

        rel << "PACKAGE LAYOUT\n--------------\n";
        rel << "  " << kManifestTxt << "     plain-text path + SHA-256 (source of truth)\n";
        rel << "  " << kManifestSqlite << "  same content, indexed\n";
        rel << "  " << kCatalogSqlite << "   read-only copy of the archivist's catalog\n";
        rel << "  " << kPastaCache << "/          serialised thumbnails and waveforms\n";
        rel << "  " << kPastaExports << "/        original files, organised by metadata mask\n\n";

        rel << "SIGNED-OFF BY\n-------------\n";
        rel << juce::String(params.autor.empty() ? std::string("(unsigned)") : params.autor) << "\n";
        if (!params.nota.empty()) rel << "\nNote: " << juce::String(params.nota) << "\n";

        resultado.relatorio = pacote.getChildFile(kRelatorio);
        resultado.relatorio.replaceWithText(rel, false, false, "\n");
    }

    // 'concluida' exige: não cancelada, nenhuma falha, e pelo menos um
    // arquivo gravado. Qualquer outra combinação é 'falha' — um pacote
    // parcial marcado como concluído é a mentira mais cara que este
    // software poderia contar.
    resultado.concluida = !resultado.cancelada && resultado.falhas.empty() && resultado.itensGravados > 0;
    registro.run("UPDATE publicacao SET status = ? WHERE id = ?",
                 {Value::of(resultado.concluida ? "concluida" : "falha"), Value::of(resultado.publicacaoId)});

    return resultado;
}

ResultadoVerificacaoPacote verificarPacote(const juce::File& pasta) {
    ResultadoVerificacaoPacote out;

    juce::File pacote = pasta;
    if (!pacote.getChildFile(kManifestTxt).existsAsFile()) {
        for (const auto& e : juce::RangedDirectoryIterator(pasta, false, "*" + juce::String(kSufixoPacote),
                                                            juce::File::findDirectories)) {
            if (e.getFile().getChildFile(kManifestTxt).existsAsFile()) {
                pacote = e.getFile();
                break;
            }
        }
    }

    juce::File manifesto = pacote.getChildFile(kManifestTxt);
    if (!manifesto.existsAsFile()) {
        out.problemas.push_back("manifest.txt not found");
        return out;
    }

    juce::StringArray linhas;
    linhas.addLines(manifesto.loadFileAsString());
    for (const auto& linha : linhas) {
        if (linha.trim().isEmpty() || linha.trim().startsWith("#")) continue;

        // "<sha256>  <caminho>" — dois espaços, formato do shasum. O caminho
        // pode conter espaços, então só o PRIMEIRO separador conta.
        int corte = linha.indexOf("  ");
        if (corte <= 0) continue;
        juce::String sha = linha.substring(0, corte).trim();
        juce::String relativo = linha.substring(corte + 2).trim();

        juce::File arquivo = pacote.getChildFile(relativo);
        ++out.conferidos;
        if (!arquivo.existsAsFile()) {
            ++out.ausentes;
            out.problemas.push_back("missing: " + relativo.toStdString());
            continue;
        }
        if (matriz::ingest::calcularChecksums(arquivo).sha256 != sha.toStdString()) {
            ++out.divergentes;
            out.problemas.push_back("checksum mismatch: " + relativo.toStdString());
        }
    }
    return out;
}

} // namespace matriz::publicacao
