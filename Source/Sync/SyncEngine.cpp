#include "SyncEngine.h"

#include <algorithm>
#include "../Ingest/Checksum.h"
#include "../Model/ProjectLog.h"
#include "../Consolidacao/Consolidacao.h"

namespace matriz::sync {

namespace {

struct ArquivoScan {
    juce::String caminhoRelativo; // relativo à pasta raiz da categoria (Media/ ou Project/)
    juce::File arquivoFisico;
    juce::int64 tamanhoBytes = 0;
    std::string sha256;
};

void coletarArquivosRecursivo(const juce::File& pastaBase, const juce::File& pastaAtual,
                              std::vector<ArquivoScan>& lista, bool ehProject) {
    if (!pastaAtual.isDirectory()) return;

    juce::Array<juce::File> filhos;
    pastaAtual.findChildFiles(filhos, juce::File::findFilesAndDirectories, false);

    for (const auto& f : filhos) {
        juce::String nome = f.getFileName();
        if (nome == ".DS_Store" || nome.startsWith(".tmp_") || nome.startsWith("tmp_")) continue;
        if (nome == "_lixeira" || nome == "destination.json") continue;
        if (ehProject && (nome == ".sync_em_andamento" || nome.endsWith("-wal") || nome.endsWith("-shm") || nome.endsWith("-journal"))) continue;
        if (!ehProject && (nome == "Project" || nome == "relatorios" || nome == "log" || nome == "catalogo" || nome == "destination.json" || nome == "_lixeira" || nome == "Media")) {
            juce::Logger::writeToLog("[SyncEngine] WARNING: Non-media item skipped inside Media: " + f.getFullPathName());
            continue;
        }
        if (!ehProject && f.existsAsFile()) {
            juce::String ext = f.getFileExtension().toLowerCase();
            if (ext == ".sqlite" || ext.startsWith(".sqlite-") || ext == ".mtz" || ext == ".bkm") {
                juce::Logger::writeToLog("[SyncEngine] WARNING: Database/project file skipped inside Media: " + f.getFullPathName());
                continue;
            }
        }

        if (f.isDirectory()) {
            coletarArquivosRecursivo(pastaBase, f, lista, ehProject);
        } else if (f.existsAsFile()) {
            ArquivoScan scan;
            scan.caminhoRelativo = f.getRelativePathFrom(pastaBase);
            scan.arquivoFisico = f;
            scan.tamanhoBytes = f.getSize();
            lista.push_back(std::move(scan));
        }
    }
}

static std::optional<matriz::model::DestinationInfo> resolverOuCriarDestinationInfo(const juce::File& raiz) {
    if (!raiz.isDirectory()) return std::nullopt;

    if (raiz.getFileName().equalsIgnoreCase("Media") || raiz.getFileName().equalsIgnoreCase("Project"))
        return std::nullopt;

    juce::File check = raiz.getParentDirectory();
    while (check != juce::File() && check != check.getParentDirectory()) {
        if (check.getFileName().equalsIgnoreCase("Media") || check.getFileName().equalsIgnoreCase("Project"))
            return std::nullopt;
        check = check.getParentDirectory();
    }

    // 1. Diretamente na raiz
    juce::File jsonRaiz = raiz.getChildFile("destination.json");
    if (jsonRaiz.existsAsFile()) {
        auto infoOpt = matriz::model::DestinationInfo::lerDeArquivo(jsonRaiz);
        if (infoOpt.has_value() && !infoOpt->projetoId.empty()) return infoOpt;
    }

    // 2. Na subpasta Project/
    juce::File jsonProj = raiz.getChildFile("Project").getChildFile("destination.json");
    if (jsonProj.existsAsFile()) {
        auto infoOpt = matriz::model::DestinationInfo::lerDeArquivo(jsonProj);
        if (infoOpt.has_value() && !infoOpt->projetoId.empty()) {
            infoOpt->gravarEmArquivo(jsonRaiz);
            return infoOpt;
        }
    }

    // 3. Tenta recuperar do SQLite registro.sqlite (seja em Project/registro.sqlite ou raiz)
    juce::File regFile = raiz.getChildFile("Project").getChildFile("registro.sqlite");
    if (!regFile.existsAsFile()) regFile = raiz.getChildFile("registro.sqlite");

    if (regFile.existsAsFile()) {
        try {
            matriz::db::Database db(regFile.getFullPathName().toStdString());
            auto stmt = db.prepare("SELECT id, nome, atualizado_em FROM projeto LIMIT 1");
            if (stmt.step()) {
                matriz::model::DestinationInfo info;
                info.formato = 1;
                info.destinationId = matriz::model::novoUuid();
                info.projetoId = stmt.columnText(0);
                info.rotulo = stmt.columnText(1);
                info.papel = "CLONE";
                info.revisao = 1;
                info.ultimaEdicaoUtc = stmt.columnText(2);
                info.criadoEm = info.ultimaEdicaoUtc.empty() ? matriz::model::agoraIso8601() : info.ultimaEdicaoUtc;
                info.gravarEmArquivo(jsonRaiz);
                return info;
            }
        } catch (...) {}
    }

    // 4. Tenta recuperar de arquivo de projeto .mtz ou .bkm (na raiz ou em Project/)
    juce::Array<juce::File> projFiles;
    raiz.findChildFiles(projFiles, juce::File::findFiles, false, "*.mtz;*.bkm");
    juce::File projDir = raiz.getChildFile("Project");
    if (projDir.isDirectory()) {
        projDir.findChildFiles(projFiles, juce::File::findFiles, false, "*.mtz;*.bkm");
    }
    for (const auto& pf : projFiles) {
        juce::var parsed = juce::JSON::parse(pf);
        if (parsed.isObject()) {
            std::string projId = parsed.getProperty("projeto_id", "").toString().toStdString();
            if (!projId.empty()) {
                matriz::model::DestinationInfo info;
                info.formato = 1;
                info.destinationId = parsed.getProperty("destination_id", juce::String(matriz::model::novoUuid())).toString().toStdString();
                info.projetoId = projId;
                info.rotulo = parsed.getProperty("nome", pf.getFileNameWithoutExtension()).toString().toStdString();
                info.papel = "CLONE";
                info.revisao = 1;
                info.ultimaEdicaoUtc = parsed.getProperty("criado_em", juce::String(matriz::model::agoraIso8601())).toString().toStdString();
                info.criadoEm = info.ultimaEdicaoUtc;
                info.gravarEmArquivo(jsonRaiz);
                return info;
            }
        }
    }

    // Regra 4: Não cria destination.json em pasta qualquer sem indício de projeto.
    return std::nullopt;
}

juce::File criarPastaLixeira(const juce::File& alvoRaiz) {
    juce::File projDir = alvoRaiz.getChildFile("Project");
    if (!projDir.exists()) projDir.createDirectory();
    juce::File lixeiraBase = projDir.getChildFile("_lixeira");
    if (!lixeiraBase.exists()) lixeiraBase.createDirectory();

    // Clean up legacy _lixeira on root if it exists
    juce::File legacyLixeira = alvoRaiz.getChildFile("_lixeira");
    if (legacyLixeira.isDirectory()) {
        legacyLixeira.deleteRecursively();
    }

    juce::String timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H%M%S");
    juce::File pastaSync = lixeiraBase.getChildFile(timestamp + "_sync");
    int seq = 1;
    while (pastaSync.exists()) {
        pastaSync = lixeiraBase.getChildFile(timestamp + "_sync_" + juce::String(seq++));
    }
    pastaSync.createDirectory();
    return pastaSync;
}

bool moverParaLixeira(const juce::File& arquivoOrigem, const juce::File& pastaLixeira,
                      const juce::String& categoria, const juce::String& caminhoRelativo) {
    if (!arquivoOrigem.exists()) return true;

    juce::File destino = pastaLixeira.getChildFile(categoria).getChildFile(caminhoRelativo);
    juce::File destinoDir = destino.getParentDirectory();
    if (!destinoDir.exists()) destinoDir.createDirectory();

    if (destino.exists()) {
        juce::String base = destino.getFileNameWithoutExtension();
        juce::String ext = destino.getFileExtension();
        int seq = 1;
        while (destino.exists()) {
            destino = destinoDir.getChildFile(base + "_" + juce::String(seq++) + ext);
        }
    }

    return arquivoOrigem.moveFileTo(destino);
}

} // namespace

bool SyncEngine::temMarcadorSyncIncompleto(const juce::File& destinoRaiz) {
    return destinoRaiz.getChildFile("Project").getChildFile(".sync_em_andamento").existsAsFile();
}

void SyncEngine::criarMarcadorSync(const juce::File& destinoRaiz, const juce::String& refInfo) {
    juce::File marker = destinoRaiz.getChildFile("Project").getChildFile(".sync_em_andamento");
    juce::File parent = marker.getParentDirectory();
    if (!parent.exists()) parent.createDirectory();
    juce::String content = "SYNC IN PROGRESS\nReference: " + refInfo + "\nStarted: " + matriz::model::agoraIso8601();
    marker.replaceWithText(content);
}

void SyncEngine::removerMarcadorSync(const juce::File& destinoRaiz) {
    juce::File marker = destinoRaiz.getChildFile("Project").getChildFile(".sync_em_andamento");
    if (marker.existsAsFile()) marker.deleteFile();
}

PlanoSync SyncEngine::escanearEComparar(const juce::File& referenciaRaiz,
                                       const juce::File& alvoRaiz,
                                       bool modoCompletoSha256,
                                       const CallbackProgressoSync& progresso,
                                       matriz::app::CancelamentoPtr cancelamento) {
    PlanoSync plano;

    // 1. Validações prévias
    if (!referenciaRaiz.isDirectory()) {
        plano.errosValidacao.push_back("Reference destination folder not found: " + referenciaRaiz.getFullPathName().toStdString());
        return plano;
    }
    if (!alvoRaiz.isDirectory()) {
        plano.errosValidacao.push_back("Target destination folder not found: " + alvoRaiz.getFullPathName().toStdString());
        return plano;
    }
    if (referenciaRaiz == alvoRaiz) {
        plano.errosValidacao.push_back("Reference and target cannot be the same folder.");
        return plano;
    }

    matriz::model::sanitizarEstruturaDestino(referenciaRaiz);
    matriz::model::sanitizarEstruturaDestino(alvoRaiz);

    auto dentroDeMediaOuProject = [](const juce::File& f) {
        juce::File cur = f;
        while (cur != juce::File() && cur != cur.getParentDirectory()) {
            if (cur.getFileName().equalsIgnoreCase("Media") || cur.getFileName().equalsIgnoreCase("Project"))
                return true;
            cur = cur.getParentDirectory();
        }
        return false;
    };
    if (dentroDeMediaOuProject(referenciaRaiz) || dentroDeMediaOuProject(alvoRaiz)) {
        plano.errosValidacao.push_back("Selecting a 'Media' or 'Project' folder (or subfolder within them) as a destination is not allowed.");
        return plano;
    }

    auto destJsonRef = resolverOuCriarDestinationInfo(referenciaRaiz);
    auto destJsonAlvo = resolverOuCriarDestinationInfo(alvoRaiz);

    if (!destJsonRef) {
        plano.errosValidacao.push_back("Reference destination folder not accessible: " + referenciaRaiz.getFullPathName().toStdString());
        return plano;
    }
    if (!destJsonAlvo) {
        plano.errosValidacao.push_back("Target destination folder not accessible: " + alvoRaiz.getFullPathName().toStdString());
        return plano;
    }

    // Proteção de Backup: validação de projetoId entre destinos
    if (!destJsonRef->projetoId.empty() && !destJsonAlvo->projetoId.empty() && destJsonRef->projetoId != destJsonAlvo->projetoId) {
        plano.errosValidacao.push_back("Target destination belongs to a different project (ID: " +
                                       destJsonAlvo->projetoId + ", expected: " + destJsonRef->projetoId + ")");
        return plano;
    }
    if (destJsonAlvo->projetoId.empty() && !destJsonRef->projetoId.empty()) {
        destJsonAlvo->projetoId = destJsonRef->projetoId;
        destJsonAlvo->gravarEmArquivo(alvoRaiz.getChildFile("destination.json"));
    }

    if (temMarcadorSyncIncompleto(referenciaRaiz)) {
        // Remove marcador residual de sessão anterior interrompida para desbloquear o escaneamento
        removerMarcadorSync(referenciaRaiz);
    }

    plano.revisaoRef = destJsonRef->revisao;
    plano.revisaoAlvo = destJsonAlvo->revisao;
    plano.rotuloRef = destJsonRef->rotulo.empty() ? referenciaRaiz.getFileName().toStdString() : destJsonRef->rotulo;
    plano.rotuloAlvo = destJsonAlvo->rotulo.empty() ? alvoRaiz.getFileName().toStdString() : destJsonAlvo->rotulo;
    plano.referenciaMaisAntiga = (destJsonRef->revisao < destJsonAlvo->revisao);

    // 2. Coletar arquivos
    juce::File refMedia = referenciaRaiz.getChildFile("Media");
    juce::File refProj = referenciaRaiz.getChildFile("Project");
    juce::File alvoMedia = alvoRaiz.getChildFile("Media");
    juce::File alvoProj = alvoRaiz.getChildFile("Project");

    std::vector<ArquivoScan> refMediaFiles, refProjFiles;
    std::vector<ArquivoScan> alvoMediaFiles, alvoProjFiles;

    if (refMedia.isDirectory()) {
        coletarArquivosRecursivo(refMedia, refMedia, refMediaFiles, false);
    }

    if (refProj.isDirectory()) {
        coletarArquivosRecursivo(refProj, refProj, refProjFiles, true);
    }

    if (alvoMedia.isDirectory()) {
        coletarArquivosRecursivo(alvoMedia, alvoMedia, alvoMediaFiles, false);
    }

    if (alvoProj.isDirectory()) {
        coletarArquivosRecursivo(alvoProj, alvoProj, alvoProjFiles, true);
    }

    int totalArquivosParaLer = static_cast<int>(refMediaFiles.size() + refProjFiles.size() +
                                                alvoMediaFiles.size() + alvoProjFiles.size());
    int arquivosLidos = 0;

    auto calcularHashSeNecessario = [&](ArquivoScan& a, const juce::String& prefixoMsg) {
        if (cancelamento && cancelamento->pedido()) return false;
        if (modoCompletoSha256) {
            a.sha256 = matriz::ingest::calcularChecksums(a.arquivoFisico).sha256;
        } else {
            a.sha256 = juce::String(a.tamanhoBytes).toStdString();
        }
        arquivosLidos++;
        if (progresso) {
            progresso(arquivosLidos, totalArquivosParaLer, prefixoMsg + " " + a.caminhoRelativo);
        }
        return true;
    };

    for (auto& a : refMediaFiles) {
        if (!calcularHashSeNecessario(a, "Reading reference Media:")) return plano;
    }
    for (auto& a : refProjFiles) {
        if (!calcularHashSeNecessario(a, "Reading reference Project:")) return plano;
    }
    for (auto& a : alvoMediaFiles) {
        if (!calcularHashSeNecessario(a, "Reading target Media:")) return plano;
    }
    for (auto& a : alvoProjFiles) {
        if (!calcularHashSeNecessario(a, "Reading target Project:")) return plano;
    }

    // 3. Comparação de categorias
    auto processarCategoria = [&](const std::vector<ArquivoScan>& refFiles,
                                  const std::vector<ArquivoScan>& alvoFiles,
                                  CategoriaSync cat) {
        std::map<juce::String, const ArquivoScan*> refMap;
        for (const auto& r : refFiles) refMap[r.caminhoRelativo] = &r;

        std::map<juce::String, const ArquivoScan*> alvoMap;
        for (const auto& a : alvoFiles) alvoMap[a.caminhoRelativo] = &a;

        std::vector<ItemSync> novos;
        std::vector<ItemSync> removidos;

        // Arquivos presentes na referência
        for (const auto& r : refFiles) {
            bool ehBanco = (cat == CategoriaSync::Project &&
                           (r.caminhoRelativo == "registro.sqlite" || r.caminhoRelativo == "indice.sqlite"));

            auto it = alvoMap.find(r.caminhoRelativo);
            if (it != alvoMap.end()) {
                const auto* a = it->second;
                ItemSync item;
                item.caminhoRelativo = r.caminhoRelativo;
                item.categoria = cat;
                item.tamanhoBytes = r.tamanhoBytes;
                item.sha256Ref = r.sha256;
                item.sha256Alvo = a->sha256;

                if (ehBanco) {
                    if (plano.revisaoRef == plano.revisaoAlvo) {
                        item.classe = ClasseSync::Igual;
                        plano.totalIguais++;
                    } else {
                        item.classe = ClasseSync::Modificado;
                        plano.totalModificados++;
                        plano.bytesParaCopiar += r.tamanhoBytes;
                        plano.bytesParaLixeira += a->tamanhoBytes;
                    }
                } else if (r.sha256 == a->sha256) {
                    item.classe = ClasseSync::Igual;
                    plano.totalIguais++;
                } else {
                    item.classe = ClasseSync::Modificado;
                    plano.totalModificados++;
                    plano.bytesParaCopiar += r.tamanhoBytes;
                    plano.bytesParaLixeira += a->tamanhoBytes;
                }
                plano.itens.push_back(std::move(item));
            } else {
                ItemSync item;
                item.caminhoRelativo = r.caminhoRelativo;
                item.categoria = cat;
                item.tamanhoBytes = r.tamanhoBytes;
                item.sha256Ref = r.sha256;
                novos.push_back(std::move(item));
            }
        }

        // Arquivos presentes somente no alvo
        for (const auto& a : alvoFiles) {
            if (refMap.find(a.caminhoRelativo) == refMap.end()) {
                ItemSync item;
                item.caminhoRelativo = a.caminhoRelativo;
                item.categoria = cat;
                item.tamanhoBytes = a.tamanhoBytes;
                item.sha256Alvo = a.sha256;
                removidos.push_back(std::move(item));
            }
        }

        // Pareamento de MOVIDO (mesmo SHA-256 entre novo e removido)
        std::vector<bool> novoUsado(novos.size(), false);
        std::vector<bool> removidoUsado(removidos.size(), false);

        for (size_t ni = 0; ni < novos.size(); ++ni) {
            for (size_t ri = 0; ri < removidos.size(); ++ri) {
                if (removidoUsado[ri]) continue;
                if (!novos[ni].sha256Ref.empty() && novos[ni].sha256Ref == removidos[ri].sha256Alvo) {
                    // Pareado como MOVIDO
                    ItemSync item = novos[ni];
                    item.classe = ClasseSync::Movido;
                    item.caminhoOrigemMovido = removidos[ri].caminhoRelativo;
                    plano.totalMovidos++;
                    plano.itens.push_back(std::move(item));
                    novoUsado[ni] = true;
                    removidoUsado[ri] = true;
                    break;
                }
            }
        }

        // Os novos não pareados viram NOVO
        for (size_t ni = 0; ni < novos.size(); ++ni) {
            if (!novoUsado[ni]) {
                novos[ni].classe = ClasseSync::Novo;
                plano.totalNovos++;
                plano.bytesParaCopiar += novos[ni].tamanhoBytes;
                plano.itens.push_back(std::move(novos[ni]));
            }
        }

        // Os removidos não pareados viram REMOVIDO
        for (size_t ri = 0; ri < removidos.size(); ++ri) {
            if (!removidoUsado[ri]) {
                removidos[ri].classe = ClasseSync::Removido;
                plano.totalRemovidos++;
                plano.bytesParaLixeira += removidos[ri].tamanhoBytes;
                plano.itens.push_back(std::move(removidos[ri]));
            }
        }
    };

    processarCategoria(refMediaFiles, alvoMediaFiles, CategoriaSync::Media);
    processarCategoria(refProjFiles, alvoProjFiles, CategoriaSync::Project);

    // Validação de espaço livre no alvo
    juce::int64 espacoDisponivel = alvoRaiz.getBytesFreeOnVolume();
    if (espacoDisponivel > 0 && plano.bytesParaCopiar > espacoDisponivel) {
        plano.errosValidacao.push_back("Insufficient free disk space on target destination (Required: " +
                                       juce::File::descriptionOfSizeInBytes(plano.bytesParaCopiar).toStdString() +
                                       ", Available: " +
                                       juce::File::descriptionOfSizeInBytes(espacoDisponivel).toStdString() + ").");
    }

    return plano;
}

ResultadoSync SyncEngine::aplicarSync(const juce::File& referenciaRaiz,
                                      const juce::File& alvoRaiz,
                                      const PlanoSync& plano,
                                      const CallbackProgressoSync& progresso,
                                      matriz::app::CancelamentoPtr cancelamento) {
    ResultadoSync res;

    if (!plano.podeAplicar()) {
        res.falhas = plano.errosValidacao;
        return res;
    }

    juce::File refProj = referenciaRaiz.getChildFile("Project");
    juce::File refMedia = referenciaRaiz.getChildFile("Media");
    juce::File alvoProj = alvoRaiz.getChildFile("Project");
    juce::File alvoMedia = alvoRaiz.getChildFile("Media");

    if (!alvoProj.exists()) alvoProj.createDirectory();
    if (!alvoMedia.exists()) alvoMedia.createDirectory();

    // 1. Criar marcador de sync em andamento e pasta da lixeira
    criarMarcadorSync(alvoRaiz, juce::String(plano.rotuloRef) + " (" + juce::String(plano.revisaoRef) + ")");
    juce::File pastaLixeira = criarPastaLixeira(alvoRaiz);
    res.pastaLixeiraCriada = pastaLixeira;

    int totalOperacoes = static_cast<int>(plano.itens.size()) + 2; // +2 para bancos e metadata
    int opAtual = 0;

    auto notificar = [&](const juce::String& msg) {
        opAtual++;
        if (progresso) progresso(opAtual, totalOperacoes, msg);
    };

    try {
        // 2. Aplicar itens de Media/
        for (const auto& item : plano.itens) {
            if (cancelamento && cancelamento->pedido()) {
                res.cancelado = true;
                break;
            }

            if (item.categoria != CategoriaSync::Media) continue;

            juce::File fRef = refMedia.getChildFile(item.caminhoRelativo);
            juce::File fAlvo = alvoMedia.getChildFile(item.caminhoRelativo);

            if (item.classe == ClasseSync::Movido) {
                juce::File fOrigemAlvo = alvoMedia.getChildFile(item.caminhoOrigemMovido);
                if (fOrigemAlvo.existsAsFile()) {
                    juce::File pDir = fAlvo.getParentDirectory();
                    if (!pDir.exists()) pDir.createDirectory();
                    if (fAlvo.exists()) fAlvo.deleteFile();
                    if (fOrigemAlvo.moveFileTo(fAlvo)) {
                        res.itensMovidos++;
                    } else {
                        res.falhas.push_back("Failed to move: " + item.caminhoOrigemMovido.toStdString() + " -> " + item.caminhoRelativo.toStdString());
                    }
                }
                notificar("Moved: " + item.caminhoRelativo);
            } else if (item.classe == ClasseSync::Removido) {
                if (fAlvo.existsAsFile()) {
                    if (moverParaLixeira(fAlvo, pastaLixeira, "Media", item.caminhoRelativo)) {
                        res.itensLixeira++;
                    } else {
                        res.falhas.push_back("Failed to move to trash: " + item.caminhoRelativo.toStdString());
                    }
                }
                notificar("Sent to trash: " + item.caminhoRelativo);
            } else if (item.classe == ClasseSync::Modificado) {
                if (fAlvo.existsAsFile()) {
                    moverParaLixeira(fAlvo, pastaLixeira, "Media", item.caminhoRelativo);
                    res.itensLixeira++;
                }
                juce::File pDir = fAlvo.getParentDirectory();
                if (!pDir.exists()) pDir.createDirectory();
                if (fRef.copyFileTo(fAlvo)) {
                    std::string shaAlvo = matriz::ingest::calcularChecksums(fAlvo).sha256;
                    if (!item.sha256Ref.empty() && item.sha256Ref.length() == 64 && shaAlvo != item.sha256Ref) {
                        res.falhas.push_back("SHA-256 verification failed after copy: " + item.caminhoRelativo.toStdString());
                    } else {
                        res.itensCopiados++;
                    }
                } else {
                    res.falhas.push_back("Failed to copy modified: " + item.caminhoRelativo.toStdString());
                }
                notificar("Updated: " + item.caminhoRelativo);
            } else if (item.classe == ClasseSync::Novo) {
                juce::File pDir = fAlvo.getParentDirectory();
                if (!pDir.exists()) pDir.createDirectory();
                if (fRef.copyFileTo(fAlvo)) {
                    std::string shaAlvo = matriz::ingest::calcularChecksums(fAlvo).sha256;
                    if (!item.sha256Ref.empty() && item.sha256Ref.length() == 64 && shaAlvo != item.sha256Ref) {
                        res.falhas.push_back("SHA-256 verification failed after copy: " + item.caminhoRelativo.toStdString());
                    } else {
                        res.itensCopiados++;
                    }
                } else {
                    res.falhas.push_back("Failed to copy: " + item.caminhoRelativo.toStdString());
                }
                notificar("Copied: " + item.caminhoRelativo);
            }
        }

        // 3. Aplicar itens de Project/ (exceto bancos)
        for (const auto& item : plano.itens) {
            if (cancelamento && cancelamento->pedido()) {
                res.cancelado = true;
                break;
            }

            if (item.categoria != CategoriaSync::Project) continue;
            if (item.caminhoRelativo == "registro.sqlite" || item.caminhoRelativo == "indice.sqlite") continue;

            juce::File fRef = refProj.getChildFile(item.caminhoRelativo);
            juce::File fAlvo = alvoProj.getChildFile(item.caminhoRelativo);

            if (item.classe == ClasseSync::Removido || item.classe == ClasseSync::Modificado) {
                if (fAlvo.existsAsFile()) {
                    moverParaLixeira(fAlvo, pastaLixeira, "Project", item.caminhoRelativo);
                    res.itensLixeira++;
                }
            }

            if (item.classe == ClasseSync::Novo || item.classe == ClasseSync::Modificado) {
                juce::File pDir = fAlvo.getParentDirectory();
                if (!pDir.exists()) pDir.createDirectory();
                if (fRef.copyFileTo(fAlvo)) {
                    res.itensCopiados++;
                } else {
                    res.falhas.push_back("Failed to copy project file: " + item.caminhoRelativo.toStdString());
                }
                notificar("Updated project: " + item.caminhoRelativo);
            }
        }

        // 4. Cópia segura dos bancos de dados
        if (!res.cancelado) {
            notificar("Safely syncing databases...");
            juce::File regRefFile = refProj.getChildFile("registro.sqlite");
            juce::File indRefFile = refProj.getChildFile("indice.sqlite");
            juce::File regAlvoFile = alvoProj.getChildFile("registro.sqlite");
            juce::File indAlvoFile = alvoProj.getChildFile("indice.sqlite");

            // Mover bancos antigos para a lixeira
            if (regAlvoFile.existsAsFile()) moverParaLixeira(regAlvoFile, pastaLixeira, "Project", "registro.sqlite");
            if (indAlvoFile.existsAsFile()) moverParaLixeira(indAlvoFile, pastaLixeira, "Project", "indice.sqlite");

            if (regRefFile.existsAsFile()) {
                matriz::db::Database dbRefReg(regRefFile.getFullPathName().toStdString());
                dbRefReg.copiarSeguroPara(regAlvoFile.getFullPathName().toStdString());
            }
            if (indRefFile.existsAsFile()) {
                matriz::db::Database dbRefInd(indRefFile.getFullPathName().toStdString());
                dbRefInd.copiarSeguroPara(indAlvoFile.getFullPathName().toStdString());
            }

            // 5. Ajustar o banco novo do alvo com sua identidade
            auto destJsonAlvo = matriz::model::DestinationInfo::lerDeArquivo(alvoRaiz.getChildFile("destination.json"));
            if (destJsonAlvo && regAlvoFile.existsAsFile()) {
                matriz::db::Database dbAlvoReg(regAlvoFile.getFullPathName().toStdString());
                std::string agora = matriz::model::agoraIso8601();

                dbAlvoReg.run(
                    "INSERT INTO backup_destino (id, destino_path, rotulo, ativo, criado_em, destination_id, papel, ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
                    "VALUES (?, ?, ?, 1, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(destino_path) DO UPDATE SET destination_id = excluded.destination_id, ultimo_visto_em = excluded.ultimo_visto_em, "
                    "ultima_edicao_conhecida = excluded.ultima_edicao_conhecida, ultima_revisao_conhecida = excluded.ultima_revisao_conhecida",
                    {
                        matriz::db::Value::of(destJsonAlvo->destinationId),
                        matriz::db::Value::of(alvoRaiz.getFullPathName().toStdString()),
                        matriz::db::Value::of(destJsonAlvo->rotulo.empty() ? alvoRaiz.getFileName().toStdString() : destJsonAlvo->rotulo),
                        matriz::db::Value::of(destJsonAlvo->criadoEm.empty() ? agora : destJsonAlvo->criadoEm),
                        matriz::db::Value::of(destJsonAlvo->destinationId),
                        matriz::db::Value::of(destJsonAlvo->papel),
                        matriz::db::Value::of(static_cast<long long>(plano.revisaoRef)),
                        matriz::db::Value::of(agora),
                        matriz::db::Value::of(agora)
                    });
            }

            // 6. Logs em ambos os DESTINATIONs
            juce::StringArray logDetails;
            logDetails.add("Reference: " + juce::String(plano.rotuloRef) + " (Rev " + juce::String(plano.revisaoRef) + ")");
            logDetails.add("Target: " + juce::String(plano.rotuloAlvo) + " (Rev " + juce::String(plano.revisaoAlvo) + ")");
            logDetails.add("Copied: " + juce::String(res.itensCopiados) + " files");
            logDetails.add("Moved internally: " + juce::String(res.itensMovidos) + " files");
            logDetails.add("Moved to trash: " + juce::String(res.itensLixeira) + " files");
            if (!res.falhas.empty()) logDetails.add("Failures: " + juce::String((int)res.falhas.size()));

            matriz::model::ProjectLog plRef(refProj);
            plRef.appendEntry("BACKUP SYNC", logDetails);

            matriz::model::ProjectLog plAlvo(alvoProj);
            plAlvo.appendEntry("BACKUP SYNC", logDetails);

            // 7. Atualizar destination.json do alvo se não houve falhas graves
            if (res.falhas.empty() && destJsonAlvo) {
                destJsonAlvo->revisao = plano.revisaoRef;
                destJsonAlvo->ultimaEdicaoUtc = matriz::model::agoraIso8601();
                destJsonAlvo->gravarEmArquivo(alvoRaiz.getChildFile("destination.json"));
                removerMarcadorSync(alvoRaiz);
                res.sucesso = true;
            }
        }
        matriz::model::sanitizarEstruturaDestino(alvoRaiz);
    } catch (const std::exception& e) {
        res.falhas.push_back(std::string("Exception during sync: ") + e.what());
    }

    return res;
}

std::vector<SyncEngine::StatusEspelhamento> SyncEngine::executarEspelhamentoAutomatico(matriz::model::Project& projeto,
                                                                                    const std::set<std::string>& ignorarIds,
                                                                                    bool aplicarRemocoes) {
    std::vector<StatusEspelhamento> resultados;
    auto& db = projeto.registro();
    std::string activeDestId = projeto.destinationId();
    juce::File refRaiz = matriz::model::normalizarParaRaizDestino(projeto.raiz());

    // Confirm active revision before mirroring
    projeto.confirmarRevisao();

    try {
        auto stmt = db.prepare("SELECT id, destino_path, rotulo, ultima_revisao_conhecida FROM backup_destino WHERE ativo = 1");
        struct DestRow {
            std::string id;
            juce::String path;
            juce::String rotulo;
            int64_t ultimaRevisao = 0;
        };
        std::vector<DestRow> rows;
        while (stmt.step()) {
            std::string id = stmt.columnText(0);
            if (id == activeDestId) continue;
            if (ignorarIds.count(id)) continue;
            DestRow r;
            r.id = id;
            r.path = stmt.columnText(1);
            r.rotulo = stmt.columnText(2);
            r.ultimaRevisao = stmt.columnInt(3);
            rows.push_back(std::move(r));
        }

        for (const auto& r : rows) {
            StatusEspelhamento st;
            st.destinationId = r.id;
            st.rotulo = r.rotulo;
            st.caminho = r.path;

            juce::File cloneRaiz = matriz::model::normalizarParaRaizDestino(juce::File(r.path));
            if (!cloneRaiz.isDirectory()) {
                st.estado = StatusEspelhamento::Estado::PendenteOffline;
                st.mensagem = "Drive or destination offline";
                resultados.push_back(st);
                continue;
            }

            auto cloneInfo = resolverOuCriarDestinationInfo(cloneRaiz);
            if (!cloneInfo) {
                st.estado = StatusEspelhamento::Estado::PendenteOffline;
                st.mensagem = "destination.json not reachable";
                resultados.push_back(st);
                continue;
            }
            if (cloneInfo->projetoId != projeto.projetoId()) {
                if (!cloneInfo->projetoId.empty()) {
                    st.estado = StatusEspelhamento::Estado::Falha;
                    st.mensagem = "Target destination belongs to a different project (" + juce::String(cloneInfo->projetoId) + ")";
                    resultados.push_back(st);
                    continue;
                }
                cloneInfo->projetoId = projeto.projetoId();
                cloneInfo->gravarEmArquivo(cloneRaiz.getChildFile("destination.json"));
            }

            if (cloneInfo->revisao == r.ultimaRevisao) {
                // Alvo não mudou por conta própria: aplicar espelhamento
                PlanoSync plano = escanearEComparar(refRaiz, cloneRaiz, false);
                if (!aplicarRemocoes) {
                    plano.itens.erase(std::remove_if(plano.itens.begin(), plano.itens.end(),
                                                     [](const ItemSync& it) { return it.classe == ClasseSync::Removido; }),
                                      plano.itens.end());
                    plano.totalRemovidos = 0;
                }
                if (!plano.podeAplicar()) {
                    st.estado = StatusEspelhamento::Estado::Falha;
                    st.mensagem = plano.errosValidacao.empty() ? "Validation failed" : juce::String(plano.errosValidacao.front());
                } else {
                    ResultadoSync res = aplicarSync(refRaiz, cloneRaiz, plano);
                    if (res.sucesso) {
                        st.estado = StatusEspelhamento::Estado::Aplicado;
                        st.mensagem = "Successfully mirrored (Rev " + juce::String(projeto.revisao()) + ")";
                        db.run("UPDATE backup_destino SET ultima_revisao_conhecida = ?, ultimo_visto_em = ? WHERE id = ?",
                               {matriz::db::Value::of(static_cast<long long>(projeto.revisao())),
                                matriz::db::Value::of(matriz::model::agoraIso8601()),
                                matriz::db::Value::of(r.id)});
                    } else {
                        st.estado = StatusEspelhamento::Estado::Falha;
                        st.mensagem = res.falhas.empty() ? "Sync failed" : juce::String(res.falhas.front());
                    }
                }
            } else {
                // Revisão divergente
                st.estado = StatusEspelhamento::Estado::Divergente;
                st.mensagem = "Target has independent revisions (Use manual BACKUP SYNC)";
            }

            resultados.push_back(st);
        }
    } catch (...) {}

    return resultados;
}

// ------------------------------------------------------------------ Etapa 7

namespace {

bool ehArquivoDeSistema(const juce::String& nome) {
    return nome == ".DS_Store" || nome == ".Spotlight-V100" || nome == ".fseventsd" || nome == ".Trashes" ||
           nome == ".TemporaryItems" || nome == ".DocumentRevisions-V100" || nome.startsWith("._");
}

// Arquivos de `base` (recursivo), relativos a ela; pula lixo de sistema, a
// lixeira e o manifesto do próprio clone.
void listarArquivos(const juce::File& base, const juce::File& atual, std::vector<juce::String>& out) {
    juce::Array<juce::File> filhos;
    atual.findChildFiles(filhos, juce::File::findFilesAndDirectories, false);
    for (const auto& f : filhos) {
        const auto nome = f.getFileName();
        if (ehArquivoDeSistema(nome) || nome == "_lixeira") continue;
        if (atual == base && nome == "checksums.sha256") continue;
        if (f.isDirectory()) listarArquivos(base, f, out);
        else if (f.existsAsFile()) out.push_back(f.getRelativePathFrom(base));
    }
}

juce::File pastaLivre(const juce::File& dentro, const juce::String& nome) {
    juce::File f = dentro.getChildFile(nome);
    int n = 2;
    while (f.exists()) f = dentro.getChildFile(nome + "_" + juce::String(n++));
    return f;
}

void garantirTabelaSourceClone(matriz::db::Database& db) {
    db.exec("CREATE TABLE IF NOT EXISTS source_clone ("
            "  id TEXT PRIMARY KEY, vault_id TEXT NOT NULL, origem_path TEXT NOT NULL, destino_path TEXT NOT NULL,"
            "  rotulo TEXT NOT NULL DEFAULT '', criado_em TEXT NOT NULL, ultima_sync_em TEXT, arquivos INTEGER NOT NULL DEFAULT 0)");
}

} // namespace

SyncEngine::ResultadoClone SyncEngine::clonarMain(matriz::model::Project& projeto, const juce::File& pastaEscolhida,
                                                  const CallbackProgressoSync& progresso,
                                                  matriz::app::CancelamentoPtr cancelamento) {
    ResultadoClone r;
    auto& db = projeto.registro();
    const juce::File refRaiz = matriz::model::normalizarParaRaizDestino(projeto.raiz());
    juce::File raiz = pastaEscolhida;
    juce::Array<juce::File> conteudo;
    if (raiz.isDirectory()) raiz.findChildFiles(conteudo, juce::File::findFilesAndDirectories, false);
    bool vazia = true;
    for (auto& f : conteudo) if (!ehArquivoDeSistema(f.getFileName())) vazia = false;
    if (!vazia) raiz = pastaLivre(pastaEscolhida, juce::File::createLegalFileName(juce::String::fromUTF8(projeto.nome().c_str())) + " CLONE");
    if (raiz == refRaiz || raiz.isAChildOf(refRaiz)) {
        r.falhas.push_back("The clone cannot be inside the MAIN");
        return r;
    }
    if (!raiz.createDirectory()) {
        r.falhas.push_back("Could not create " + raiz.getFullPathName().toStdString());
        return r;
    }
    const std::string agora = matriz::model::agoraIso8601();
    matriz::model::DestinationInfo info;
    info.destinationId = matriz::model::novoUuid();
    info.projetoId = projeto.projetoId();
    info.papel = "CLONE";
    info.rotulo = raiz.getFileName().toStdString();
    info.revisao = 0;
    info.criadoEm = agora;
    info.gravarEmArquivo(raiz.getChildFile("destination.json"));
    db.run("INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, "
           "ultima_revisao_conhecida) VALUES (?, ?, ?, ?, 'CLONE', 1, ?, 0)",
           {matriz::db::Value::of(info.destinationId), matriz::db::Value::of(info.destinationId),
            matriz::db::Value::of(raiz.getFullPathName().toStdString()), matriz::db::Value::of(info.rotulo),
            matriz::db::Value::of(agora)});
    r.id = info.destinationId;
    r.raiz = raiz;
    auto res = sincronizarCloneDoMain(projeto, r.id, false, progresso, cancelamento);
    r.sucesso = res.sucesso;
    r.cancelado = res.cancelado;
    r.copiados = res.itensCopiados;
    r.falhas = res.falhas;
    matriz::model::ProjectLog(projeto.pasta()).appendEntry("MAIN Cloned", {
        "Clone: " + raiz.getFullPathName(), "Files copied: " + juce::String(r.copiados),
        "Failures: " + juce::String((int) r.falhas.size())});
    return r;
}

PlanoSync SyncEngine::compararCloneDoMain(matriz::model::Project& projeto, const std::string& cloneId) {
    PlanoSync vazio;
    std::string caminho;
    {
        auto st = projeto.registro().prepare("SELECT destino_path FROM backup_destino WHERE id = ? AND papel <> 'ORIGINAL'");
        st.bind(1, matriz::db::Value::of(cloneId));
        if (st.step()) caminho = st.columnText(0);
    }
    const juce::File cloneRaiz = matriz::model::normalizarParaRaizDestino(juce::File(juce::String::fromUTF8(caminho.c_str())));
    if (caminho.empty() || !cloneRaiz.isDirectory()) {
        vazio.errosValidacao.push_back("Clone offline or not registered");
        return vazio;
    }
    return escanearEComparar(matriz::model::normalizarParaRaizDestino(projeto.raiz()), cloneRaiz, false);
}

ResultadoSync SyncEngine::sincronizarCloneDoMain(matriz::model::Project& projeto, const std::string& cloneId,
                                                 bool aplicarRemocoes, const CallbackProgressoSync& progresso,
                                                 matriz::app::CancelamentoPtr cancelamento) {
    ResultadoSync res;
    auto plano = compararCloneDoMain(projeto, cloneId);
    if (!plano.podeAplicar()) {
        res.falhas.push_back(plano.errosValidacao.front());
        return res;
    }
    if (!aplicarRemocoes) {
        plano.itens.erase(std::remove_if(plano.itens.begin(), plano.itens.end(),
                                         [](const ItemSync& it) { return it.classe == ClasseSync::Removido; }),
                          plano.itens.end());
        plano.totalRemovidos = 0;
    }
    std::string caminho;
    {
        auto st = projeto.registro().prepare("SELECT destino_path FROM backup_destino WHERE id = ?");
        st.bind(1, matriz::db::Value::of(cloneId));
        if (st.step()) caminho = st.columnText(0);
    }
    const juce::File cloneRaiz = matriz::model::normalizarParaRaizDestino(juce::File(juce::String::fromUTF8(caminho.c_str())));
    res = aplicarSync(matriz::model::normalizarParaRaizDestino(projeto.raiz()), cloneRaiz, plano, progresso, cancelamento);
    if (res.sucesso)
        projeto.registro().run("UPDATE backup_destino SET ultima_revisao_conhecida = ?, ultimo_visto_em = ? WHERE id = ?",
                               {matriz::db::Value::of(static_cast<long long>(plano.revisaoRef)),
                                matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(cloneId)});
    return res;
}

SyncEngine::ResultadoClone SyncEngine::clonarSource(matriz::model::Project& projeto, const std::string& vaultId,
                                                    const juce::File& pastaEscolhida,
                                                    const CallbackProgressoSync& progresso,
                                                    matriz::app::CancelamentoPtr cancelamento) {
    ResultadoClone r;
    auto& db = projeto.registro();
    garantirTabelaSourceClone(db);
    std::string localizacao, nome;
    std::vector<std::string> relativos;
    {
        auto st = db.prepare("SELECT localizacao, nome FROM vault WHERE id = ?");
        st.bind(1, matriz::db::Value::of(vaultId));
        if (st.step()) {
            localizacao = st.columnText(0);
            nome = st.columnText(1);
        }
        auto sa = db.prepare("SELECT caminho_relativo FROM arquivo WHERE vault_id = ? AND COALESCE(caminho_relativo, '') <> ''");
        sa.bind(1, matriz::db::Value::of(vaultId));
        while (sa.step()) relativos.push_back(sa.columnText(0));
    }
    juce::File volume(juce::String::fromUTF8(localizacao.c_str()));
    if (localizacao.empty() || !volume.isDirectory()) {
        r.falhas.push_back("The SOURCE is not connected");
        return r;
    }
    // Cartão/HD externo: o volume inteiro. Disco do sistema: só a pasta comum
    // aos arquivos ingeridos (clonar o Macintosh HD inteiro não faz sentido).
    juce::File origem = volume;
    if (!volume.getFullPathName().startsWith("/Volumes/") && !relativos.empty()) {
        juce::StringArray comum;
        comum.addTokens(juce::String(relativos.front()), "/", "");
        comum.remove(comum.size() - 1);
        for (const auto& rel : relativos) {
            juce::StringArray partes;
            partes.addTokens(juce::String(rel), "/", "");
            partes.remove(partes.size() - 1);
            int i = 0;
            while (i < comum.size() && i < partes.size() && comum[i] == partes[i]) ++i;
            comum.removeRange(i, comum.size() - i);
        }
        for (auto& p : comum) if (p.isNotEmpty()) origem = origem.getChildFile(p);
    }
    const auto codigos = matriz::consolidacao::codigosDeSource(db);
    const juce::String codigo = codigos.count(vaultId) ? juce::String(codigos.at(vaultId)) : juce::String("SRC");
    r.raiz = pastaLivre(pastaEscolhida, juce::File::createLegalFileName(juce::String::fromUTF8(nome.c_str()) + "_" + codigo));
    if (!r.raiz.createDirectory()) {
        r.falhas.push_back("Could not create " + r.raiz.getFullPathName().toStdString());
        return r;
    }
    std::vector<juce::String> arquivos;
    listarArquivos(origem, origem, arquivos);
    juce::String manifesto;
    const int total = static_cast<int>(arquivos.size());
    for (int i = 0; i < total; ++i) {
        if ((cancelamento && cancelamento->pedido()) || (progresso && !progresso(i, total, arquivos[(size_t) i]))) {
            r.cancelado = true;
            break;
        }
        const juce::File de = origem.getChildFile(arquivos[(size_t) i]);
        const juce::File para = r.raiz.getChildFile(arquivos[(size_t) i]);
        para.getParentDirectory().createDirectory();
        const std::string shaOrigem = matriz::ingest::calcularChecksums(de).sha256;
        if (!de.copyFileTo(para) || matriz::ingest::calcularChecksums(para).sha256 != shaOrigem) {
            r.falhas.push_back("Copy/verification failed: " + arquivos[(size_t) i].toStdString());
            continue;
        }
        manifesto << juce::String(shaOrigem) << "  " << arquivos[(size_t) i] << "\n";
        ++r.copiados;
    }
    r.raiz.getChildFile("checksums.sha256").replaceWithText(manifesto);
    const std::string agora = matriz::model::agoraIso8601();
    r.id = matriz::model::novoUuid();
    db.run("INSERT INTO source_clone (id, vault_id, origem_path, destino_path, rotulo, criado_em, ultima_sync_em, arquivos) "
           "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
           {matriz::db::Value::of(r.id), matriz::db::Value::of(vaultId),
            matriz::db::Value::of(origem.getFullPathName().toStdString()),
            matriz::db::Value::of(r.raiz.getFullPathName().toStdString()),
            matriz::db::Value::of(r.raiz.getFileName().toStdString()), matriz::db::Value::of(agora),
            matriz::db::Value::of(agora), matriz::db::Value::of(r.copiados)});
    r.sucesso = !r.cancelado && r.falhas.empty();
    matriz::model::ProjectLog(projeto.pasta()).appendEntry("SOURCE Cloned (raw copy)", {
        "SOURCE: " + codigo + " " + juce::String::fromUTF8(nome.c_str()), "From: " + origem.getFullPathName(),
        "To: " + r.raiz.getFullPathName(), "Files: " + juce::String(r.copiados) + " (checksums.sha256)",
        "Failures: " + juce::String((int) r.falhas.size())});
    return r;
}

SyncEngine::PlanoCloneSource SyncEngine::compararCloneDeSource(matriz::model::Project& projeto, const std::string& cloneId) {
    PlanoCloneSource p;
    auto& db = projeto.registro();
    garantirTabelaSourceClone(db);
    auto st = db.prepare("SELECT origem_path, destino_path FROM source_clone WHERE id = ?");
    st.bind(1, matriz::db::Value::of(cloneId));
    if (!st.step()) {
        p.erro = "Clone not registered";
        return p;
    }
    p.origem = juce::File(juce::String::fromUTF8(st.columnText(0).c_str()));
    p.clone = juce::File(juce::String::fromUTF8(st.columnText(1).c_str()));
    if (!p.origem.isDirectory()) { p.erro = "The SOURCE is not connected"; return p; }
    if (!p.clone.isDirectory()) { p.erro = "The clone is offline"; return p; }
    std::vector<juce::String> naOrigem, noClone;
    listarArquivos(p.origem, p.origem, naOrigem);
    listarArquivos(p.clone, p.clone, noClone);
    std::set<juce::String> setClone(noClone.begin(), noClone.end()), setOrigem(naOrigem.begin(), naOrigem.end());
    for (const auto& rel : naOrigem)
        if (!setClone.count(rel) || p.clone.getChildFile(rel).getSize() != p.origem.getChildFile(rel).getSize())
            p.novos.push_back(rel);
    for (const auto& rel : noClone)
        if (!setOrigem.count(rel)) p.removidos.push_back(rel);
    return p;
}

ResultadoSync SyncEngine::sincronizarCloneDeSource(matriz::model::Project& projeto, const std::string& cloneId,
                                                   bool aplicarRemocoes, const CallbackProgressoSync& progresso,
                                                   matriz::app::CancelamentoPtr cancelamento) {
    ResultadoSync res;
    auto p = compararCloneDeSource(projeto, cloneId);
    if (!p.erro.empty()) {
        res.falhas.push_back(p.erro);
        return res;
    }
    juce::File lixeira;
    auto pastaLixeiraClone = [&] {
        if (lixeira == juce::File()) {
            lixeira = pastaLivre(p.clone.getChildFile("_lixeira"),
                                 juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H%M%S") + "_sync");
            lixeira.createDirectory();
        }
        return lixeira;
    };
    juce::String manifesto = p.clone.getChildFile("checksums.sha256").loadFileAsString();
    const int total = static_cast<int>(p.novos.size());
    for (int i = 0; i < total; ++i) {
        if ((cancelamento && cancelamento->pedido()) || (progresso && !progresso(i, total, p.novos[(size_t) i]))) {
            res.cancelado = true;
            break;
        }
        const auto& rel = p.novos[(size_t) i];
        const juce::File de = p.origem.getChildFile(rel), para = p.clone.getChildFile(rel);
        if (para.existsAsFile()) {  // tamanho mudou: a versão antiga vai pra lixeira do clone, nunca some
            moverParaLixeira(para, pastaLixeiraClone(), "SOURCE", rel);
            res.itensLixeira++;
        }
        para.getParentDirectory().createDirectory();
        const std::string sha = matriz::ingest::calcularChecksums(de).sha256;
        if (!de.copyFileTo(para) || matriz::ingest::calcularChecksums(para).sha256 != sha) {
            res.falhas.push_back("Copy/verification failed: " + rel.toStdString());
            continue;
        }
        manifesto << juce::String(sha) << "  " << rel << "\n";
        res.itensCopiados++;
    }
    if (aplicarRemocoes && !res.cancelado)
        for (const auto& rel : p.removidos)
            if (moverParaLixeira(p.clone.getChildFile(rel), pastaLixeiraClone(), "SOURCE", rel)) res.itensLixeira++;
    p.clone.getChildFile("checksums.sha256").replaceWithText(manifesto);
    res.sucesso = !res.cancelado && res.falhas.empty();
    res.pastaLixeiraCriada = lixeira;
    projeto.registro().run("UPDATE source_clone SET ultima_sync_em = ?, arquivos = arquivos + ? WHERE id = ?",
                           {matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(res.itensCopiados),
                            matriz::db::Value::of(cloneId)});
    return res;
}

bool SyncEngine::promoverAMain(matriz::model::Project& projeto, const std::string& cloneId, juce::String& erro) {
    auto& db = projeto.registro();
    std::string caminhoClone, destIdClone, idMainAntigo, caminhoMainAntigo;
    {
        auto st = db.prepare("SELECT destino_path, COALESCE(destination_id, id) FROM backup_destino "
                             "WHERE id = ? AND papel <> 'ORIGINAL' AND ativo = 1");
        st.bind(1, matriz::db::Value::of(cloneId));
        if (!st.step()) { erro = "Not an active CLONE"; return false; }
        caminhoClone = st.columnText(0);
        destIdClone = st.columnText(1);
        auto sm = db.prepare("SELECT COALESCE(destination_id, id), destino_path FROM backup_destino WHERE papel = 'ORIGINAL' LIMIT 1");
        if (sm.step()) { idMainAntigo = sm.columnText(0); caminhoMainAntigo = sm.columnText(1); }
    }
    const juce::File cloneRaiz = matriz::model::normalizarParaRaizDestino(juce::File(juce::String::fromUTF8(caminhoClone.c_str())));
    const juce::File bancoDoClone = cloneRaiz.getChildFile("Project").getChildFile("registro.sqlite");
    if (!cloneRaiz.isDirectory() || !bancoDoClone.existsAsFile()) {
        erro = "The clone must be connected and have the project database (sync it first)";
        return false;
    }
    const std::string mediaNova = cloneRaiz.getChildFile("Media").getFullPathName().trimCharactersAtEnd("/").toStdString();
    auto aplicar = [&](matriz::db::Database& d) {
        d.run("BEGIN IMMEDIATE", {});
        try {
            d.run("UPDATE backup_destino SET papel = CASE WHEN id = ? OR destination_id = ? THEN 'ORIGINAL' ELSE 'CLONE' END "
                  "WHERE ativo = 1",
                  {matriz::db::Value::of(cloneId), matriz::db::Value::of(destIdClone)});
            // É espelho: os mesmos caminhos relativos valem no novo MAIN.
            d.run("UPDATE OR IGNORE consolidacao_registro SET destino_id = ?, destino_path = ? "
                  "WHERE COALESCE(destino_id, '') = '' OR destino_id = ?",
                  {matriz::db::Value::of(destIdClone), matriz::db::Value::of(mediaNova),
                   matriz::db::Value::of(idMainAntigo)});
            d.run("UPDATE projeto SET destino_backup_ativo_path = ?",
                  {matriz::db::Value::of(cloneRaiz.getFullPathName().toStdString())});
            d.run("COMMIT", {});
        } catch (...) {
            try { d.run("ROLLBACK", {}); } catch (...) {}
            throw;
        }
    };
    try {
        aplicar(db);
        if (bancoDoClone != projeto.pasta().getChildFile("registro.sqlite")) {
            matriz::db::Database dbClone(bancoDoClone.getFullPathName().toStdString());
            aplicar(dbClone);
        }
    } catch (const std::exception& e) {
        erro = e.what();
        return false;
    }
    if (auto info = matriz::model::DestinationInfo::lerDeArquivo(cloneRaiz.getChildFile("destination.json"))) {
        info->papel = "ORIGINAL";
        info->gravarEmArquivo(cloneRaiz.getChildFile("destination.json"));
    }
    const juce::File mainAntigo = matriz::model::normalizarParaRaizDestino(juce::File(juce::String::fromUTF8(caminhoMainAntigo.c_str())));
    if (!caminhoMainAntigo.empty() && mainAntigo != cloneRaiz)
        if (auto info = matriz::model::DestinationInfo::lerDeArquivo(mainAntigo.getChildFile("destination.json"))) {
            info->papel = "CLONE";
            info->gravarEmArquivo(mainAntigo.getChildFile("destination.json"));
        }
    matriz::model::ProjectLog(projeto.pasta()).appendEntry("CLONE Promoted to MAIN", {
        "New MAIN: " + cloneRaiz.getFullPathName(), "Previous MAIN: " + juce::String::fromUTF8(caminhoMainAntigo.c_str()),
        "Nothing copied or deleted; open the project from the new MAIN"});
    return true;
}


} // namespace matriz::sync
