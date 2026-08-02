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
        "INSERT INTO arquivo (id, item_id, caminho_relativo, papel, eh_master, checksum_md5, checksum_sha256, "
        "checksum_gerado_em, caracteristicas_tecnicas_json, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {Value::of(arquivoId), Value::of(itemId), Value::of(caminhoRelativo.toStdString()), Value::of(papel),
         Value::of(ehMaster), Value::of(resultado.checksums.md5), Value::of(resultado.checksums.sha256),
         Value::of(agora), Value::of(caracteristicasJson), Value::of(agora), Value::of(agora)});

    return resultado;
}

} // namespace matriz::ingest
