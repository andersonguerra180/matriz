// ==============================================================================
// BackupScanEngine
//
// NOTA ARQUITETURAL IMPORTANTE:
// Este componente e estritamente responsavel pela varredura de integridade e
// calculo de hash SHA-256 no DESTINO DE BACKUP ATIVO (classificando arquivos em
// 🔴 Vermelho / 🟢 Verde / ⚪ Orfao).
//
// NAO CONFUNDIR NEM UNIFICAR com "RescanEngine / Rescan Backup Sources", que
// opera na ORIGEM dos arquivos para reingestao e deteccao de novos assets de entrada.
// Qualquer eventual unificacao requer uma decisao de arquitetura separada.
// ==============================================================================

#include "BackupScanEngine.h"
#include "../Ingest/Checksum.h"
#include "../Model/ProjectLog.h"
#include "../I18n/Strings.h"
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace matriz::consolidacao {

ResultadoScanBackup BackupScanEngine::executarScanDestino(
    matriz::db::Database& registro,
    const juce::File& pastaProjeto,
    const juce::File& destinoAtivo,
    const PlanoConsolidacao& plano,
    const AoProgredirScan& aoProgredir,
    std::atomic<bool>* cancelamento)
{
    ResultadoScanBackup resultado;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    if (!destinoAtivo.isDirectory()) {
        // Se o destino nao existe ou nao e pasta, todos os itens do grid sao Vermelhos
        for (const auto& ip : plano.itens) {
            ItemStatusBackup isb;
            isb.itemId = ip.itemId;
            isb.arquivoId = ip.arquivoId;
            isb.codigoAcervo = ip.codigoAcervo;
            isb.nomeArquivo = ip.nomeOriginal;
            isb.caminhoRelativoDestino = ip.caminhoRelativoDestino;
            isb.tamanhoBytes = ip.tamanhoBytes;
            isb.status = StatusArquivoBackup::Vermelho;
            resultado.itensGrid.push_back(std::move(isb));
            resultado.totalVermelhos++;
        }
        return resultado;
    }

    // FASE 1: Lendo arquivos fisicos presentes no destino
    if (aoProgredir && !aoProgredir(isPt ? juce::String::fromUTF8("Lendo arquivos do destino...")
                                        : "Reading files from destination...", 0, 100)) {
        resultado.cancelado = true;
        return resultado;
    }

    juce::Array<juce::File> arquivosNoDestino;
    destinoAtivo.findChildFiles(arquivosNoDestino, juce::File::findFiles, true);

    std::unordered_set<std::string> caminhosRelativosNoDestino;
    std::unordered_map<std::string, juce::File> mapaArquivosFisicos;
    for (const auto& f : arquivosNoDestino) {
        juce::String rel = f.getRelativePathFrom(destinoAtivo);
        #if JUCE_WINDOWS
        rel = rel.replaceCharacter('\\', '/');
        #endif
        std::string relStd = rel.toStdString();
        caminhosRelativosNoDestino.insert(relStd);
        mapaArquivosFisicos[relStd] = f;
    }

    // Mapear hashes conhecidos dos assets do projeto
    std::unordered_map<std::string, std::string> mapaHashesArquivo; // arquivoId -> sha256
    try {
        auto stmtArq = registro.prepare("SELECT id, checksum_sha256 FROM arquivo WHERE checksum_sha256 IS NOT NULL AND checksum_sha256 != ''");
        while (stmtArq.step()) {
            mapaHashesArquivo[stmtArq.columnText(0)] = stmtArq.columnText(1);
        }
    } catch (...) {}

    // FASE 2: Calculando hashes e comparando com o grid
    int totalItens = static_cast<int>(plano.itens.size());
    std::unordered_set<std::string> caminhosUsadosPeloGrid;
    std::unordered_set<std::string> hashesUsadosPeloGrid;

    for (int i = 0; i < totalItens; ++i) {
        if (cancelamento && cancelamento->load()) {
            resultado.cancelado = true;
            return resultado;
        }

        const auto& ip = plano.itens[static_cast<size_t>(i)];

        if (aoProgredir && !aoProgredir(
                (isPt ? juce::String::fromUTF8("Calculando hashes...") : "Calculating hashes...") +
                " (" + juce::String(i + 1) + "/" + juce::String(totalItens) + ")",
                i + 1, totalItens)) {
            resultado.cancelado = true;
            return resultado;
        }

        ItemStatusBackup isb;
        isb.itemId = ip.itemId;
        isb.arquivoId = ip.arquivoId;
        isb.codigoAcervo = ip.codigoAcervo;
        isb.nomeArquivo = ip.nomeOriginal;
        isb.caminhoRelativoDestino = ip.caminhoRelativoDestino;
        isb.tamanhoBytes = ip.tamanhoBytes;

        // Obter hash do asset no projeto
        std::string shaProjeto;
        auto itSha = mapaHashesArquivo.find(ip.arquivoId);
        if (itSha != mapaHashesArquivo.end()) {
            shaProjeto = itSha->second;
        }
        isb.sha256Projeto = shaProjeto;
        if (!shaProjeto.empty()) {
            hashesUsadosPeloGrid.insert(shaProjeto);
        }

        // Verificar arquivo no destino
        juce::String relDest = ip.caminhoRelativoDestino;
        #if JUCE_WINDOWS
        relDest = relDest.replaceCharacter('\\', '/');
        #endif
        std::string relDestStd = relDest.toStdString();
        caminhosUsadosPeloGrid.insert(relDestStd);

        juce::File arquivoDestino = destinoAtivo.getChildFile(ip.caminhoRelativoDestino);
        if (arquivoDestino.existsAsFile()) {
            auto checksumsDestino = matriz::ingest::calcularChecksums(arquivoDestino);
            isb.sha256Destino = checksumsDestino.sha256;

            if (!shaProjeto.empty() && !checksumsDestino.sha256.empty() && shaProjeto == checksumsDestino.sha256) {
                isb.status = StatusArquivoBackup::Verde;
                resultado.totalVerdes++;
            } else {
                isb.status = StatusArquivoBackup::Vermelho;
                resultado.totalVermelhos++;
            }
        } else {
            isb.status = StatusArquivoBackup::Vermelho;
            resultado.totalVermelhos++;
        }

        resultado.itensGrid.push_back(std::move(isb));
    }

    // FASE 3: Comparando com o catálogo e detectando arquivos Órfãos (⚪)
    if (aoProgredir && !aoProgredir(isPt ? juce::String::fromUTF8("Comparando com o catálogo...")
                                        : "Comparing with catalog...", totalItens, totalItens)) {
        resultado.cancelado = true;
        return resultado;
    }

    for (const auto& relStd : caminhosRelativosNoDestino) {
        if (cancelamento && cancelamento->load()) {
            resultado.cancelado = true;
            return resultado;
        }

        // Se o caminho relativo nao esta sendo usado por nenhum item do grid atual
        if (caminhosUsadosPeloGrid.find(relStd) == caminhosUsadosPeloGrid.end()) {
            juce::File arqFisico = mapaArquivosFisicos[relStd];
            if (arqFisico.existsAsFile()) {
                // Verificar se o hash corresponde a algum asset do grid
                auto cs = matriz::ingest::calcularChecksums(arqFisico);
                if (hashesUsadosPeloGrid.find(cs.sha256) == hashesUsadosPeloGrid.end()) {
                    ArquivoOrfao orfao;
                    orfao.caminhoRelativo = juce::String::fromUTF8(relStd.c_str());
                    orfao.arquivoFisico = arqFisico;
                    orfao.tamanhoBytes = arqFisico.getSize();
                    orfao.sha256 = cs.sha256;
                    resultado.orfaos.push_back(std::move(orfao));
                    resultado.totalOrfaos++;
                }
            }
        }
    }

    // Registrar no ProjectLog se o scan completou com sucesso
    if (!resultado.cancelado) {
        try {
            matriz::model::ProjectLog pLog(pastaProjeto);
            juce::StringArray details;
            details.add("Destination: " + destinoAtivo.getFullPathName());
            details.add("Total assets: " + juce::String(totalItens));
            details.add("Green (identical hash): " + juce::String(resultado.totalVerdes));
            details.add("Red (divergent/new): " + juce::String(resultado.totalVermelhos));
            details.add("Orphan files in destination: " + juce::String(resultado.totalOrfaos));
            pLog.appendEntry("Backup Destination Scanned", details);
        } catch (...) {}
    }

    return resultado;
}

} // namespace matriz::consolidacao
