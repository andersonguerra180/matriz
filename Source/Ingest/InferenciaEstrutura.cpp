#include "InferenciaEstrutura.h"

#include <algorithm>
#include <cmath>
#include <regex>

namespace matriz::ingest {

namespace {

std::optional<juce::var> buscarTagCaseInsensitive(const juce::var& tags, const std::vector<juce::String>& chaves) {
    if (!tags.isObject()) return std::nullopt;
    auto* obj = tags.getDynamicObject();
    if (!obj) return std::nullopt;
    for (auto& propriedade : obj->getProperties()) {
        juce::String nomeChave = propriedade.name.toString();
        for (auto& candidata : chaves)
            if (nomeChave.equalsIgnoreCase(candidata))
                return propriedade.value;
    }
    return std::nullopt;
}

} // namespace

InferenciaPastaRelease inferirDePastaRelease(const juce::String& nomePasta) {
    static const std::regex padrao(R"(^(.+?)\s*-\s*(.+?)\s*\((\d{4})\)\s*$)");
    std::string entrada = nomePasta.toStdString();
    std::smatch m;
    InferenciaPastaRelease r;
    if (std::regex_match(entrada, m, padrao)) {
        r.artista = m[1].str();
        r.album = m[2].str();
        r.ano = std::stoi(m[3].str());
    }
    return r;
}

InferenciaNomeFaixa inferirDeNomeArquivoFaixa(const juce::String& nomeArquivo) {
    static const std::regex padrao(R"(^(\d+)\s*[-.]\s*(.+?)\.[A-Za-z0-9]+$)");
    std::string entrada = nomeArquivo.toStdString();
    std::smatch m;
    InferenciaNomeFaixa r;
    if (std::regex_match(entrada, m, padrao)) {
        r.numero = std::stoi(m[1].str());
        juce::String titulo(m[2].str());
        r.titulo = titulo.trim().toStdString();
    }
    return r;
}

TagsEmbutidasComuns extrairTagsComuns(const LeituraTecnicaResultado& leitura) {
    TagsEmbutidasComuns r;
    juce::var tags;
    if (leitura.bruto.isObject()) {
        // ffprobe: as tags do container ficam em format.tags (áudio/vídeo).
        tags = leitura.bruto["format"]["tags"];
        if (!tags.isObject()) tags = leitura.bruto["tags"];
    }
    if (auto v = buscarTagCaseInsensitive(tags, {"title"})) r.titulo = v->toString().toStdString();
    if (auto v = buscarTagCaseInsensitive(tags, {"artist", "albumartist"})) r.artista = v->toString().toStdString();
    if (auto v = buscarTagCaseInsensitive(tags, {"isrc"})) r.isrc = v->toString().toStdString();
    if (auto v = buscarTagCaseInsensitive(tags, {"composer"})) r.compositor = v->toString().toStdString();
    return r;
}

bool provavelCapaFrente(const DimensaoImagem& dimensao, int ladoMinimoPx, double toleranciaProporcao) {
    if (dimensao.largura <= 0 || dimensao.altura <= 0) return false;
    if (dimensao.largura < ladoMinimoPx || dimensao.altura < ladoMinimoPx) return false;
    double razao = static_cast<double>(dimensao.largura) / dimensao.altura;
    return std::abs(razao - 1.0) <= toleranciaProporcao;
}

bool provavelmenteMesmaGravacao(double duracaoASegundos, double duracaoBSegundos, double similaridadeFingerprint,
                                 double toleranciaDuracaoSegundos, double limiarSimilaridade) {
    if (similaridadeFingerprint < limiarSimilaridade) return false;
    return std::abs(duracaoASegundos - duracaoBSegundos) <= toleranciaDuracaoSegundos;
}

std::vector<GrupoTemporal> agruparPorSaltoDeTimestamp(std::vector<ItemParaAgrupar> itens, double saltoMaximoSegundos) {
    std::vector<GrupoTemporal> grupos;
    if (itens.empty()) return grupos;

    std::sort(itens.begin(), itens.end(),
              [](const ItemParaAgrupar& a, const ItemParaAgrupar& b) { return a.timestampUnixSegundos < b.timestampUnixSegundos; });

    GrupoTemporal atual;
    atual.idsOrdenados.push_back(itens.front().id);
    atual.inicioUnixSegundos = atual.fimUnixSegundos = itens.front().timestampUnixSegundos;

    for (size_t i = 1; i < itens.size(); ++i) {
        double gap = itens[i].timestampUnixSegundos - itens[i - 1].timestampUnixSegundos;
        if (gap > saltoMaximoSegundos) {
            grupos.push_back(std::move(atual));
            atual = GrupoTemporal{};
            atual.inicioUnixSegundos = itens[i].timestampUnixSegundos;
        }
        atual.idsOrdenados.push_back(itens[i].id);
        atual.fimUnixSegundos = itens[i].timestampUnixSegundos;
    }
    grupos.push_back(std::move(atual));

    return grupos;
}

} // namespace matriz::ingest
