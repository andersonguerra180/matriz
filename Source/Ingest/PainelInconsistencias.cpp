#include "PainelInconsistencias.h"

#include "Checksum.h"

#include <cmath>
#include <map>
#include <set>

namespace matriz::ingest {

namespace {

double paraDouble(const juce::var& v) {
    if (v.isDouble() || v.isInt() || v.isInt64()) return static_cast<double>(v);
    return v.toString().getDoubleValue();
}

} // namespace

std::vector<Inconsistencia> detectarInconsistenciasFicha(
    matriz::db::Database& registro, const std::map<std::string, matriz::ficha::FichaDefinition>& definicoesPorTipo) {
    std::vector<Inconsistencia> out;

    // a) faixa sem ISRC
    {
        auto stmt = registro.prepare(
            "SELECT DISTINCT ic.item_id, ic.nivel_indice FROM item_campo ic "
            "JOIN item i ON i.id = ic.item_id "
            "WHERE ic.nivel = 'faixa' AND i.tipo_midia = 'release' "
            "AND NOT EXISTS (SELECT 1 FROM item_campo ic2 WHERE ic2.item_id = ic.item_id AND ic2.nivel = 'faixa' "
            "AND ic2.nivel_indice = ic.nivel_indice AND ic2.campo_id = 'isrc' AND ic2.valor IS NOT NULL AND ic2.valor != '')");
        while (stmt.step()) {
            Inconsistencia inc;
            inc.tipo = "faixa_sem_isrc";
            inc.itemId = stmt.columnText(0);
            inc.descricao = "Faixa " + std::to_string(stmt.columnInt(1)) + " sem ISRC";
            out.push_back(std::move(inc));
        }
    }

    // b) release sem capa
    {
        auto stmt = registro.prepare(
            "SELECT id FROM item WHERE tipo_midia = 'release' "
            "AND id NOT IN (SELECT item_id FROM arquivo WHERE papel = 'capa_frente')");
        while (stmt.step()) {
            Inconsistencia inc;
            inc.tipo = "release_sem_capa";
            inc.itemId = stmt.columnText(0);
            inc.descricao = "Release sem capa frente";
            out.push_back(std::move(inc));
        }
    }

    // c) master em lossy, d) sample rate divergente dentro do mesmo item
    {
        auto stmt = registro.prepare("SELECT id, item_id, caracteristicas_tecnicas_json FROM arquivo WHERE eh_master = 1");
        std::map<std::string, std::set<int>> ratesPorItem;
        while (stmt.step()) {
            std::string arquivoId = stmt.columnText(0);
            std::string itemId = stmt.columnText(1);
            juce::var json = juce::JSON::parse(juce::String(stmt.columnText(2)));
            if (!json.isObject()) continue;

            if (static_cast<bool>(json.getProperty("codecLossyDeclarado", false))) {
                Inconsistencia inc;
                inc.tipo = "master_lossy";
                inc.itemId = itemId;
                inc.arquivoId = arquivoId;
                juce::String codec = json.getProperty("codec", juce::var()).toString();
                inc.descricao = "Master em codec lossy" + (codec.isNotEmpty() ? (" (" + codec.toStdString() + ")") : "");
                out.push_back(std::move(inc));
            }
            if (json.hasProperty("sampleRate"))
                ratesPorItem[itemId].insert(static_cast<int>(paraDouble(json.getProperty("sampleRate", juce::var()))));
        }
        for (auto& [itemId, rates] : ratesPorItem) {
            if (rates.size() <= 1) continue;
            std::string lista;
            for (int r : rates) {
                if (!lista.empty()) lista += ", ";
                lista += std::to_string(r);
            }
            Inconsistencia inc;
            inc.tipo = "sample_rate_divergente";
            inc.itemId = itemId;
            inc.descricao = "Masters do item têm sample rates diferentes: " + lista;
            out.push_back(std::move(inc));
        }
    }

    // e) capa abaixo do mínimo
    {
        auto stmt = registro.prepare(
            "SELECT a.id, a.item_id, a.papel, a.caracteristicas_tecnicas_json, i.tipo_midia "
            "FROM arquivo a JOIN item i ON i.id = a.item_id");
        while (stmt.step()) {
            std::string arquivoId = stmt.columnText(0);
            std::string itemId = stmt.columnText(1);
            std::string papel = stmt.columnText(2);
            std::string tipoMidia = stmt.columnText(4);

            auto itDef = definicoesPorTipo.find(tipoMidia);
            if (itDef == definicoesPorTipo.end()) continue;

            for (auto& ae : itDef->second.arquivosEsperados) {
                if (ae.papel != papel || ae.minimo.empty()) continue;
                size_t xPos = ae.minimo.find('x');
                if (xPos == std::string::npos) continue;
                int minLargura = std::stoi(ae.minimo.substr(0, xPos));
                int minAltura = std::stoi(ae.minimo.substr(xPos + 1));

                juce::var json = juce::JSON::parse(juce::String(stmt.columnText(3)));
                int largura = json.isObject() ? static_cast<int>(paraDouble(json.getProperty("larguraPx", juce::var(0)))) : 0;
                int altura = json.isObject() ? static_cast<int>(paraDouble(json.getProperty("alturaPx", juce::var(0)))) : 0;

                if (largura < minLargura || altura < minAltura) {
                    Inconsistencia inc;
                    inc.tipo = "capa_abaixo_minimo";
                    inc.itemId = itemId;
                    inc.arquivoId = arquivoId;
                    inc.descricao = "Capa " + std::to_string(largura) + "x" + std::to_string(altura) +
                                     " abaixo do mínimo exigido (" + ae.minimo + ")";
                    out.push_back(std::move(inc));
                }
            }
        }
    }

    // f) splits que não somam 100
    {
        auto stmt = registro.prepare("SELECT item_id, nivel_indice, valor FROM item_campo WHERE campo_id = 'splits'");
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            int nivelIndice = static_cast<int>(stmt.columnInt(1));
            juce::var tabela = juce::JSON::parse(juce::String(stmt.columnText(2)));
            if (!tabela.isArray()) continue;

            double soma = 0.0;
            for (auto& linha : *tabela.getArray())
                if (linha.isObject()) soma += paraDouble(linha.getProperty("percentual", juce::var(0)));

            if (std::abs(soma - 100.0) > 0.01) {
                Inconsistencia inc;
                inc.tipo = "splits_nao_somam_100";
                inc.itemId = itemId;
                inc.descricao = "Splits da faixa " + std::to_string(nivelIndice) + " somam " + std::to_string(soma) +
                                 "%, não 100%";
                out.push_back(std::move(inc));
            }
        }
    }

    // g) ISRC duplicado
    {
        auto stmt = registro.prepare(
            "SELECT valor, COUNT(*), GROUP_CONCAT(item_id) FROM item_campo "
            "WHERE campo_id = 'isrc' AND valor IS NOT NULL AND valor != '' GROUP BY valor HAVING COUNT(*) > 1");
        while (stmt.step()) {
            Inconsistencia inc;
            inc.tipo = "isrc_duplicado";
            inc.descricao = "ISRC " + stmt.columnText(0) + " duplicado em: " + stmt.columnText(2);
            out.push_back(std::move(inc));
        }
    }

    return out;
}

std::vector<Inconsistencia> verificarArquivosNoDisco(matriz::model::Project& projeto) {
    std::vector<Inconsistencia> out;
    juce::File pasta = projeto.pasta();

    std::set<std::string> caminhosConhecidos;
    {
        auto stmt = projeto.registro().prepare("SELECT id, item_id, caminho_relativo, checksum_sha256 FROM arquivo");
        while (stmt.step()) {
            std::string arquivoId = stmt.columnText(0);
            std::string itemId = stmt.columnText(1);
            juce::String caminhoRelativo = stmt.columnText(2);
            std::string checksumRegistrado = stmt.columnText(3);

            caminhosConhecidos.insert(caminhoRelativo.toStdString());
            juce::File arquivo = pasta.getChildFile(caminhoRelativo);

            if (!arquivo.existsAsFile()) {
                Inconsistencia inc;
                inc.tipo = "checksum_divergente";
                inc.itemId = itemId;
                inc.arquivoId = arquivoId;
                inc.descricao = "Arquivo registrado não existe mais no disco: " + caminhoRelativo.toStdString();
                out.push_back(std::move(inc));
                continue;
            }
            if (!checksumRegistrado.empty()) {
                Checksums atual = calcularChecksums(arquivo);
                if (atual.sha256 != checksumRegistrado) {
                    Inconsistencia inc;
                    inc.tipo = "checksum_divergente";
                    inc.itemId = itemId;
                    inc.arquivoId = arquivoId;
                    inc.descricao = "Checksum do arquivo no disco não confere com o registrado: " +
                                     caminhoRelativo.toStdString();
                    out.push_back(std::move(inc));
                }
            }
        }
    }

    juce::File dirArquivos = pasta.getChildFile("arquivos");
    if (dirArquivos.isDirectory()) {
        for (auto& entrada : juce::RangedDirectoryIterator(dirArquivos, true, "*", juce::File::findFiles)) {
            juce::String relativo = entrada.getFile().getRelativePathFrom(pasta);
            if (caminhosConhecidos.count(relativo.toStdString()) == 0) {
                Inconsistencia inc;
                inc.tipo = "arquivo_orfao";
                inc.descricao = "Arquivo no disco sem registro no catálogo: " + relativo.toStdString();
                out.push_back(std::move(inc));
            }
        }
    }

    return out;
}

} // namespace matriz::ingest
