#include "IngestArquivo.h"

#include "../Model/Project.h"

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

ResultadoIngestArquivo ingerirArquivo(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                      const std::string& itemId, const juce::File& arquivoOrigem,
                                      const std::string& papel, bool ehMaster) {
    if (!arquivoOrigem.existsAsFile())
        throw IngestArquivoError("arquivo de origem não encontrado: " + arquivoOrigem.getFullPathName().toStdString());

    std::string arquivoId = matriz::model::novoUuid();
    juce::File destino = pastaProjeto.getChildFile("arquivos")
                              .getChildFile(arquivoId)
                              .getChildFile(arquivoOrigem.getFileName());

    destino.getParentDirectory().createDirectory();
    if (!arquivoOrigem.copyFileTo(destino))
        throw IngestArquivoError("falha ao copiar arquivo para dentro do projeto: " +
                                  arquivoOrigem.getFullPathName().toStdString());

    ResultadoIngestArquivo resultado;
    resultado.arquivoId = arquivoId;
    resultado.arquivoNoProjeto = destino;
    resultado.checksums = calcularChecksums(destino);
    resultado.leitura = lerTecnica(destino);

    juce::String caminhoRelativo = destino.getRelativePathFrom(pastaProjeto);
    std::string caracteristicasJson = juce::JSON::toString(construirCaracteristicasJson(resultado.leitura), true).toStdString();
    std::string agora = matriz::model::agoraIso8601();

    registro.run(
        "INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, "
        "checksum_md5, checksum_sha256, checksum_gerado_em, caracteristicas_tecnicas_json, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {Value::of(arquivoId), Value::of(itemId), Value::of(caminhoRelativo.toStdString()),
         Value::of(arquivoOrigem.getFullPathName().toStdString()), Value::of(papel), Value::of(ehMaster),
         Value::of(resultado.checksums.md5), Value::of(resultado.checksums.sha256), Value::of(agora),
         Value::of(caracteristicasJson), Value::of(agora), Value::of(agora)});

    return resultado;
}

std::optional<AssetConhecido> buscarAssetConhecido(matriz::db::Database& registro, const juce::File& arquivoOrigem) {
    if (!arquivoOrigem.existsAsFile()) return std::nullopt;

    // Hash da origem, não de uma cópia — o ponto inteiro é decidir ANTES de
    // copiar. Custa um hash a mais no caminho "arquivo é novo mesmo"
    // (ingerirArquivo hasheia a cópia de novo, redundante mas correto) —
    // aceitável nesta etapa; otimizar pra reaproveitar o hash já calculado
    // aqui fica pra quando escala de verdade (1M arquivos) for atacada.
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
