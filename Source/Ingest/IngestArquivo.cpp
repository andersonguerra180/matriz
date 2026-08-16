#include "IngestArquivo.h"

#include <sys/stat.h>

#include "../Model/Project.h"
#include "../Vault/Volume.h"

namespace matriz::ingest {

using matriz::db::Value;

namespace {

juce::var construirCaracteristicasJson(const LeituraTecnicaResultado& leitura) {
    auto obj = std::make_unique<juce::DynamicObject>();
    if (leitura.duracaoSegundos) obj->setProperty("duracaoSegundos", *leitura.duracaoSegundos);
    if (leitura.sampleRate) obj->setProperty("sampleRate", *leitura.sampleRate);
    if (leitura.bitDepth) obj->setProperty("bitDepth", *leitura.bitDepth);
    if (leitura.canais) obj->setProperty("canais", *leitura.canais);
    if (!leitura.codec.empty()) obj->setProperty("codec", juce::String(leitura.codec));
    if (leitura.larguraPx) obj->setProperty("larguraPx", *leitura.larguraPx);
    if (leitura.alturaPx) obj->setProperty("alturaPx", *leitura.alturaPx);
    if (leitura.fps) obj->setProperty("fps", *leitura.fps);
    if (leitura.lufsIntegrado) obj->setProperty("lufsIntegrado", *leitura.lufsIntegrado);
    if (leitura.lra) obj->setProperty("lra", *leitura.lra);
    if (leitura.picoDbfs) obj->setProperty("picoDbfs", *leitura.picoDbfs);
    obj->setProperty("codecLossyDeclarado", leitura.codecLossyDeclarado);
    obj->setProperty("bruto", leitura.bruto);
    return juce::var(obj.release());
}

} // namespace

std::string obterOuCriarVaultParaArquivo(matriz::db::Database& registro, const juce::File& arquivo, const std::string& projetoId) {
    auto volume = matriz::vault::descreverVolume(arquivo);
    std::string caminhoMontagem = volume.pontoMontagem.getFullPathName().toStdString();

    // O UUID é a chave forte (sobrevive a remontagem em outro caminho, §8);
    // o ponto de montagem é o fallback pra volumes que não expõem UUID. Sem
    // o guard de `uuid_volume <> ''` um volume sem UUID casaria com qualquer
    // outro volume sem UUID.
    auto stmt = registro.prepare(
        "SELECT id FROM vault WHERE (uuid_volume <> '' AND uuid_volume = ?) OR localizacao = ? LIMIT 1");
    stmt.bind(1, Value::of(volume.uuid));
    stmt.bind(2, Value::of(caminhoMontagem));
    if (stmt.step()) {
        return stmt.columnText(0);
    }

    std::string vaultId = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();
    registro.run(
        "INSERT INTO vault (id, projeto_id, nome, tipo, uuid_volume, raiz_relativa, localizacao, status, visto_em, criado_em) "
        "VALUES (?, ?, ?, 'local', ?, '', ?, 'online', ?, ?)",
        {Value::of(vaultId), Value::of(projetoId), Value::of(volume.nome),
         Value::of(volume.uuid), Value::of(caminhoMontagem), Value::of(agora), Value::of(agora)});
    return vaultId;
}

bool verificarSePlaceholderNuvem(const juce::File& f) {
    struct stat st;
    if (::stat(f.getFullPathName().toRawUTF8(), &st) == 0) {
        if (st.st_blocks == 0 && st.st_size > 0) {
            return true;
        }
    }
    return false;
}

AnaliseDeArquivo analisarArquivo(const juce::File& arquivoOrigem) {
    if (!arquivoOrigem.existsAsFile())
        throw IngestArquivoError("source file not found: " + arquivoOrigem.getFullPathName().toStdString());

    AnaliseDeArquivo a;
    a.arquivo = arquivoOrigem;
    a.ehPlaceholderNuvem = verificarSePlaceholderNuvem(arquivoOrigem);

    if (!a.ehPlaceholderNuvem) {
        a.checksums = calcularChecksums(arquivoOrigem);
        a.leitura = lerTecnica(arquivoOrigem);
    } else {
        // Placeholder de nuvem: registra o que se sabe SEM puxar os bytes
        // (critério 12). Ler o arquivo aqui dispararia o download do acervo
        // inteiro só por ter aberto o programa.
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("fileSizeBytes", static_cast<juce::int64>(arquivoOrigem.getSize()));
        a.leitura.bruto = juce::var(obj.release());
    }
    return a;
}

ResultadoIngestArquivo gravarArquivoAnalisado(matriz::db::Database& registro, const std::string& itemId,
                                               const AnaliseDeArquivo& analise, const std::string& papel,
                                               bool ehMaster) {
    std::string projetoId;
    auto stmtProj = registro.prepare("SELECT id FROM projeto LIMIT 1");
    if (stmtProj.step()) projetoId = stmtProj.columnText(0);

    std::string vaultId = obterOuCriarVaultParaArquivo(registro, analise.arquivo, projetoId);

    std::string caminhoRelativo = matriz::vault::caminhoRelativoAoVolume(analise.arquivo);

    ResultadoIngestArquivo resultado;
    resultado.arquivoId = matriz::model::novoUuid();
    resultado.arquivoNoProjeto = analise.arquivo;  // In-place (I5): a referência é o original
    resultado.checksums = analise.checksums;
    resultado.leitura = analise.leitura;

    std::string estadoPresenca = analise.ehPlaceholderNuvem ? "nao_baixado" : "presente";
    std::string caracteristicasJson =
        juce::JSON::toString(construirCaracteristicasJson(analise.leitura), true).toStdString();
    std::string agora = matriz::model::agoraIso8601();

    registro.run(
        "INSERT INTO arquivo (id, item_id, vault_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, "
        "checksum_md5, checksum_sha256, checksum_gerado_em, caracteristicas_tecnicas_json, estado_presenca, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {Value::of(resultado.arquivoId), Value::of(itemId), Value::of(vaultId),
         Value::of(caminhoRelativo),
         Value::of(analise.arquivo.getFullPathName().toStdString()), Value::of(papel), Value::of(ehMaster),
         Value::of(static_cast<long long>(analise.arquivo.getSize())),
         analise.ehPlaceholderNuvem ? Value::null() : Value::of(analise.checksums.md5),
         analise.ehPlaceholderNuvem ? Value::null() : Value::of(analise.checksums.sha256),
         analise.ehPlaceholderNuvem ? Value::null() : Value::of(agora),
         Value::of(caracteristicasJson), Value::of(estadoPresenca), Value::of(agora), Value::of(agora)});

    return resultado;
}

ResultadoIngestArquivo ingerirArquivo(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                      const std::string& itemId, const juce::File& arquivoOrigem,
                                      const std::string& papel, bool ehMaster) {
    juce::ignoreUnused(pastaProjeto);  // I5: nada é copiado pra dentro do projeto
    return gravarArquivoAnalisado(registro, itemId, analisarArquivo(arquivoOrigem), papel, ehMaster);
}

std::optional<AssetConhecido> buscarAssetPorChecksum(matriz::db::Database& registro,
                                                      const std::string& sha256) {
    if (sha256.empty()) return std::nullopt;

    auto stmt = registro.prepare(
        "SELECT a.id, i.id, i.codigo_acervo FROM arquivo a JOIN item i ON i.id = a.item_id "
        "WHERE a.checksum_sha256 = ? LIMIT 1");
    stmt.bind(1, Value::of(sha256));
    if (!stmt.step()) return std::nullopt;

    AssetConhecido conhecido;
    conhecido.arquivoId = stmt.columnText(0);
    conhecido.itemId = stmt.columnText(1);
    conhecido.codigoAcervo = stmt.columnText(2);
    return conhecido;
}

std::optional<AssetConhecido> buscarAssetConhecido(matriz::db::Database& registro, const juce::File& arquivoOrigem) {
    if (!arquivoOrigem.existsAsFile()) return std::nullopt;

    Checksums cs = calcularChecksums(arquivoOrigem);

    auto stmt = registro.prepare(
        "SELECT a.id, i.id, i.codigo_acervo FROM arquivo a JOIN item i ON i.id = a.item_id "
        "WHERE a.checksum_sha256 = ? LIMIT 1");
    stmt.bind(1, Value::of(cs.sha256));
    if (!stmt.step()) return std::nullopt;

    AssetConhecido a;
    a.arquivoId = stmt.columnText(0);
    a.itemId = stmt.columnText(1);
    a.codigoAcervo = stmt.columnText(2);
    return a;
}

void registrarLocalizacaoConhecida(matriz::db::Database& registro, const std::string& arquivoId,
                                    const juce::File& caminhoAbsoluto) {
    registro.run(
        "INSERT OR IGNORE INTO localizacao_conhecida (id, arquivo_id, caminho_absoluto, criado_em) VALUES (?, ?, ?, ?)",
        {Value::of(matriz::model::novoUuid()), Value::of(arquivoId),
         Value::of(caminhoAbsoluto.getFullPathName().toStdString()), Value::of(matriz::model::agoraIso8601())});
}

} // namespace matriz::ingest
