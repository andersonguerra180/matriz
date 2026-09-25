#include "SyncEngine.h"
#include "../Ingest/Checksum.h"
#include "../Model/ProjectLog.h"

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

std::vector<SyncEngine::StatusEspelhamento> SyncEngine::executarEspelhamentoAutomatico(matriz::model::Project& projeto) {
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

} // namespace matriz::sync
