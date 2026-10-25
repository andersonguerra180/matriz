#include "ProjetoAberto.h"

#include <limits>
#include "FichasEmbutidas.h"
#include "TiposContainer.h"
#include "TraducaoContent.h"
#include "OriginalSourceMedium.h"
#include "../Diag/Watchdog.h"

#include "../Ingest/Miniaturas.h"
#include "../Ingest/ProcessoExterno.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/CacheArquivo.h"
#include "../Vault/Reconciliacao.h"
#include "../Model/NotasEstruturadas.h"
#include "../Vault/Resolucao.h"
#include "../Vault/Volume.h"
#include "../Consolidacao/Consolidacao.h"
#include "../Model/ProjectLog.h"
#include "../Model/NomesCanonicos.h"
#include "../Model/CaminhosBanco.h"
#include "../Consolidacao/PacoteCollection.h"

#include "../I18n/Strings.h"

#include <algorithm>
#include <unordered_map>
#include <thread>
#include "EventBus.h"
#include "ProgressoGlobal.h"
#include "../Ficha/OrigemPadrao.h"

namespace matriz::ui {

namespace {

// Nó intermediário de construção da árvore (Origem ou Acervo) — heap-
// alocado via unique_ptr de propósito: o endereço de cada nó precisa ficar
// estável enquanto a árvore cresce, porque guardamos ponteiros pra ele em
// mapas de busca (por nome na Origem, por id no Acervo) durante a
// construção. Um std::vector<NoArvore> comum invalidaria esses ponteiros a
// cada realocação — bug real que foi pensado e evitado aqui, não só teórico
// (a árvore Origem pode ter centenas de milhares de segmentos de caminho).
struct NoBuilder {
    juce::String nome;
    std::string id;
    std::string pastaPaiId;
    int posicaoX = 0;
    int posicaoY = 0;
    double escalaNo = 1.0;
    bool ativo = true;
    juce::String corCustomizadaHex;
    juce::String regraOrganizacao;
    bool proposta = false;
    std::vector<std::unique_ptr<NoBuilder>> filhos;
    std::map<juce::String, NoBuilder*> indiceFilhosPorNome; // dono é `filhos`; só busca
    std::set<std::string> itemIdsDiretos;
};

NoBuilder* obterOuCriarFilhoPorNome(NoBuilder& pai, const juce::String& nome) {
    auto it = pai.indiceFilhosPorNome.find(nome);
    if (it != pai.indiceFilhosPorNome.end()) return it->second;
    auto novo = std::make_unique<NoBuilder>();
    novo->nome = nome;
    NoBuilder* ptr = novo.get();
    pai.indiceFilhosPorNome[nome] = ptr;
    pai.filhos.push_back(std::move(novo));
    return ptr;
}

juce::String csvDeConjunto(const std::set<juce::String>& s) {
    juce::StringArray arr;
    for (auto& v : s) arr.add(v);
    return arr.joinIntoString(",");
}

std::set<juce::String> conjuntoDeCsv(const juce::String& csv) {
    std::set<juce::String> out;
    for (auto& v : juce::StringArray::fromTokens(csv, ",", ""))
        if (v.isNotEmpty()) out.insert(v);
    return out;
}

ProjetoAberto::NoArvore materializar(const NoBuilder& b, bool ordenarAlfabetico) {
    ProjetoAberto::NoArvore n;
    n.id = b.id;
    n.nome = b.nome;
    n.pastaPaiId = b.pastaPaiId;
    n.posicaoX = b.posicaoX;
    n.posicaoY = b.posicaoY;
    n.escalaNo = b.escalaNo;
    n.ativo = b.ativo;
    n.corCustomizadaHex = b.corCustomizadaHex;
    n.regraOrganizacao = b.regraOrganizacao;
    n.proposta = b.proposta;
    n.itemIds = b.itemIdsDiretos;
    n.itemIdsDiretos = b.itemIdsDiretos;

    std::vector<const NoBuilder*> ordemFilhos;
    ordemFilhos.reserve(b.filhos.size());
    for (auto& f : b.filhos) ordemFilhos.push_back(f.get());
    if (ordenarAlfabetico)
        std::sort(ordemFilhos.begin(), ordemFilhos.end(),
                  [](const NoBuilder* a, const NoBuilder* c) { return a->nome.compareIgnoreCase(c->nome) < 0; });

    n.filhos.reserve(ordemFilhos.size());
    for (auto* filho : ordemFilhos) {
        ProjetoAberto::NoArvore noFilho = materializar(*filho, ordenarAlfabetico);
        for (auto& id : noFilho.itemIds) n.itemIds.insert(id);
        n.filhos.push_back(std::move(noFilho));
    }
    return n;
}

// Fase 1 — árvore do mapa ORIGINAL: mesma lógica de arvoreOrigem(true)
// (segmentos de caminho de arquivo.caminho_absoluto_origem, prefixo comum
// descartado), mas com um id ESTÁVEL por nó (derivado do caminho
// acumulado) — arvoreOrigem() deixa `id` vazio de propósito pros seus
// outros usos (árvore Origem/Explorer), e ArvoreBackupComponent precisa de
// um id não-vazio pra renderizar, selecionar e abrir "Show in Grid" num
// nó. Puramente computado — nunca lê nem grava acervo_pasta.
ProjetoAberto::NoArvore construirArvoreOriginalVirtual(matriz::db::Database& db) {
    struct Par { std::string itemId; juce::StringArray segmentos; };
    std::vector<Par> pares;
    auto stmt = db.prepare(
        "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a WHERE a.caminho_absoluto_origem IS NOT NULL "
        "AND a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = a.item_id ORDER BY eh_master DESC, id LIMIT 1)");
    while (stmt.step()) {
        Par p;
        p.itemId = stmt.columnText(0);
        juce::File pasta = juce::File(juce::String(stmt.columnText(1))).getParentDirectory();
        p.segmentos.addTokens(pasta.getFullPathName(), juce::File::getSeparatorString(), "");
        p.segmentos.removeEmptyStrings();
        pares.push_back(std::move(p));
    }

    int prefixoComum = 0;
    if (!pares.empty()) {
        prefixoComum = pares[0].segmentos.size();
        for (size_t i = 1; i < pares.size(); ++i) {
            int n = juce::jmin(prefixoComum, pares[i].segmentos.size());
            int match = 0;
            while (match < n && pares[0].segmentos[match] == pares[i].segmentos[match]) ++match;
            prefixoComum = match;
        }
        if (prefixoComum > 0) prefixoComum -= 1;
    }

    NoBuilder raiz;
    for (auto& p : pares) {
        NoBuilder* atual = &raiz;
        juce::String caminhoAcumulado;
        for (int s = prefixoComum; s < p.segmentos.size(); ++s) {
            caminhoAcumulado += (caminhoAcumulado.isEmpty() ? "" : "/") + p.segmentos[s];
            NoBuilder* filho = obterOuCriarFilhoPorNome(*atual, p.segmentos[s]);
            if (filho->id.empty()) {
                filho->id = "original:" + caminhoAcumulado.toStdString();
                // Sem pastaPaiId o canvas não desenha a linha pai->filho.
                filho->pastaPaiId = atual->id;
            }
            atual = filho;
        }
        atual->itemIdsDiretos.insert(p.itemId);
    }
    return materializar(raiz, true);
}

} // namespace

ProjetoAberto::~ProjetoAberto() {
    poolProva_.removeAllJobs(true, 2000);
    aguardarGravacoesMarcas();
}

void ProjetoAberto::aguardarGravacoesMarcas() {
    // Sem isto o destrutor do ThreadPool descartaria gravações ainda na fila.
    for (int i = 0; i < 6000; ++i) {  // no máximo ~60 s
        {
            std::lock_guard<std::mutex> lock(marcacoesMutex_);
            if (gravacoesMarcasPendentes_ == 0 && poolMarcas_.getNumJobs() == 0) break;
        }
        juce::Thread::sleep(10);
    }
}

ProjetoAberto::ProjetoAberto(std::unique_ptr<matriz::model::Project> projeto) : projeto_(std::move(projeto)) {
    juce::File registroFile = projeto_->pasta().getChildFile("registro.sqlite");
    juce::File pastaProjeto = projeto_->pasta();
    // Fase 5: destino com papel CLONE abre somente leitura.
    somenteLeitura_ = projeto_->papel() == "CLONE" && projeto_->modo() != matriz::model::Modo::Catalogo;
    // Rebuild do MAIN em andamento (persistido: destino com papel FONTE_REBUILD) — o modo sobrevive ao fechar/reabrir.
    editandoMain_ = !somenteLeitura_ && projeto_->modo() != matriz::model::Modo::Catalogo && fonteRebuild().has_value();
    // Undo persistente: as tabelas e os passos que sobraram da sessão anterior (fecharam o projeto sem desfazer).
    garantirTabelasUndo();
    carregarUndoPersistente();
    poolMarcas_.addJob([this]() { carregarMarcasRDoBanco(); });  // marcas R: carga em background
    poolMarcas_.addJob([this]() { carregarMarcacoesDoBanco(); }); // P/W/K/HTML: idem
    poolMarcas_.addJob([this]() { carregarSessoesPrintDoBanco(); }); // histórico de impressão: idem

    const bool somenteLeituraNaAbertura = somenteLeitura_;
    std::thread([registroFile, pastaProjeto, somenteLeituraNaAbertura]() {
        if (somenteLeituraNaAbertura) return;  // manutenção de tamanho_bytes escreve no banco: não num clone
        try {
            matriz::db::Database db(registroFile.getFullPathName().toStdString());

            struct FileToUpdate {
                std::string id;
                std::string localizacaoVault;
                std::string relativo;
                std::string origem;
            };
            std::vector<FileToUpdate> pendentes;

            {
                auto stmt = db.prepare(std::string("SELECT a.id, ") + matriz::vault::colunasDeResolucao() +
                                       " FROM arquivo a " + matriz::vault::joinDeResolucao() +
                                       " WHERE a.tamanho_bytes IS NULL");
                while (stmt.step()) {
                    pendentes.push_back({stmt.columnText(0), stmt.columnText(1), stmt.columnText(2),
                                          stmt.columnText(3)});
                }
            }

            if (!pendentes.empty()) {
                // stat() de cada arquivo ANTES da transação: com ela aberta,
                // esta conexão segura o lock de escrita do arquivo e a
                // conexão principal toma SQLITE_BUSY durante todo o I/O.
                std::vector<std::pair<std::string, juce::int64>> tamanhos;
                tamanhos.reserve(pendentes.size());
                for (const auto& item : pendentes) {
                    auto f = matriz::vault::resolverCaminho(pastaProjeto, item.localizacaoVault, item.relativo,
                                                             item.origem);
                    tamanhos.emplace_back(item.id, f ? f->getSize() : 0);
                }
                db.run("BEGIN TRANSACTION", {});
                try {
                    for (const auto& [id, sz] : tamanhos)
                        db.run("UPDATE arquivo SET tamanho_bytes = ? WHERE id = ?",
                               {matriz::db::Value::of(static_cast<long long>(sz)), matriz::db::Value::of(id)});
                    db.run("COMMIT TRANSACTION", {});
                } catch (...) {
                    try { db.run("ROLLBACK", {}); } catch (...) {}
                }
            }
        } catch (...) {
            // Ignore/log errors safely
        }
    }).detach();
}

void ProjetoAberto::avisarSomenteLeitura() {
    // Alguns mutadores rodam em thread de fundo (ex.: salvarMetadadoEmLote): a janela
    // sempre nasce na message thread.
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        juce::MessageManager::callAsync([] { avisarSomenteLeitura(); });
        return;
    }
    static juce::int64 ultimoAviso = 0;
    const auto agora = juce::Time::currentTimeMillis();
    if (agora - ultimoAviso < 4000) return;  // um aviso, não um por chamada
    ultimoAviso = agora;
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::InfoIcon)
                                     .withTitle(matriz::i18n::t("clone_ro.titulo"))
                                     .withMessage(matriz::i18n::t("clone_ro.bloqueado"))
                                     .withButton(matriz::i18n::t("dialogo.ok")),
                                 juce::ModalCallbackFunction::create([](int) {}));
}

bool ProjetoAberto::reavaliarSomenteLeitura() {
    if (!projeto_) return false;
    projeto_->recarregarPapel();
    const bool novo = projeto_->papel() == "CLONE" && projeto_->modo() != matriz::model::Modo::Catalogo;
    if (novo == somenteLeitura_) return false;
    somenteLeitura_ = novo;
    if (!novo) {
        try {
            matriz::model::ProjectLog(projeto_->pasta()).appendEntry("Clone promoted to MAIN", {"Editing is unlocked for this project."});
        } catch (...) {}
    }
    return true;
}

juce::int64 ProjetoAberto::tamanhoTotalDosMasters() const {
    if (!projeto_) return 0;
    auto stmt = leitura().prepare(
        "SELECT SUM(tamanho_bytes) FROM ("
        "  SELECT COALESCE(tamanho_bytes, 0) as tamanho_bytes, ROW_NUMBER() OVER (PARTITION BY item_id ORDER BY eh_master DESC, id) as rn "
        "  FROM arquivo"
        ") WHERE rn = 1");
    if (stmt.step()) {
        return static_cast<juce::int64>(stmt.columnInt(0));
    }
    return 0;
}

namespace {
// Status offline: MAIN/CLONE primeiro (etapa 2 do modelo SOURCE/MAIN/CLONE) —
// com o backup presente o item NÃO está offline, mesmo com o SOURCE guardado.
// `resolvedor` é criado sob demanda pelo chamador (carrega o
// consolidacao_registro inteiro uma vez).
bool arquivoMasterExiste(const matriz::vault::ResolvedorEmLote& resolvedor,
                         const std::map<std::string, std::string>& relinks,
                         const std::string& masterArqId, const std::string& vaultLoc,
                         const std::string& camRel, const std::string& camAbs) {
    if (masterArqId.empty()) return false;
    auto it = relinks.find(masterArqId);
    if (it != relinks.end() && !it->second.empty()) return juce::File(it->second).existsAsFile();
    auto res = resolvedor.resolver(masterArqId, vaultLoc, camRel, camAbs);
    return res.has_value() && res->existsAsFile();
}
} // namespace

std::vector<ItemResumo> ProjetoAberto::listarItensDeProjeto(matriz::db::Database& registro,
                                                            matriz::db::Database& indice,
                                                            const juce::File& pastaProjeto,
                                                            const std::map<std::string, std::string>& inMemoryRelinks,
                                                            const std::set<std::string>* itensOffline,
                                                            const std::function<void(int feitos, int total)>& aoProgredir,
                                                            const std::set<std::string>* soIds) {
    std::vector<ItemResumo> out;
    int totalLinhas = 0;
    // Janela de uma pasta: só os ids dela, numa lista JSON (json_each), em vez do catálogo inteiro filtrado depois.
    std::string idsJson;
    if (soIds != nullptr) {
        juce::Array<juce::var> lista;
        for (const auto& id : *soIds) lista.add(juce::String(id));
        idsJson = juce::JSON::toString(juce::var(lista), true).toStdString();
    }
    const std::string filtroIds = soIds != nullptr ? " AND i.id IN (SELECT value FROM json_each(?))" : "";
    if (aoProgredir) {
        try {
            auto st = registro.prepare("SELECT COUNT(*) FROM item i WHERE COALESCE(i.em_quarentena, 0) = 0 AND COALESCE(i.estado, '') != 'duplicata'" + filtroIds);
            if (soIds != nullptr) st.bind(1, matriz::db::Value::of(idsJson));
            if (st.step()) totalLinhas = static_cast<int>(st.columnInt(0));
        } catch (...) {}
        aoProgredir(-1, totalLinhas);
    }
    std::unique_ptr<matriz::vault::ResolvedorEmLote> resolvedor;  // só se algum item precisar checar disco

    // Uma consulta com JOIN em vez de N+1 (arquivo e vault resolvidos num único join
    // para a master, características técnicas e miniatura/tags preparadas uma vez).
    auto stmt = registro.prepare(
        "SELECT i.id, i.codigo_acervo, i.titulo, i.tipo_midia, i.estado, i.atualizado_em, "
        "EXISTS(SELECT 1 FROM consolidacao_registro cr WHERE cr.item_id = i.id), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'artista_principal'), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'titulo'), "
        "a.caminho_relativo, "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'origem'), "
        "i.ano, "
        "a.caminho_absoluto_origem, "
        "(SELECT ap.nome FROM acervo_item_pasta aip JOIN acervo_pasta ap ON ap.id = aip.pasta_id WHERE aip.item_id = i.id LIMIT 1), "
        "a.tamanho_bytes, "
        "i.content_type, i.collection_type, i.criado_em, "
        "a.id, "
        "COALESCE(v.localizacao, ''), "
        "i.isrc, "
        "0, "
        "COALESCE(i.metadados_editados, 0) != 0, "
        "COALESCE((WITH RECURSIVE cadeia(pasta_id, pasta_pai_id, ativo, prof) AS ("
        "  SELECT ap0.id, ap0.pasta_pai_id, ap0.ativo, 0 FROM acervo_pasta ap0 "
        "  WHERE ap0.id = (SELECT aip0.pasta_id FROM acervo_item_pasta aip0 WHERE aip0.item_id = i.id LIMIT 1) "
        "  UNION ALL "
        "  SELECT ap1.id, ap1.pasta_pai_id, ap1.ativo, c.prof + 1 FROM acervo_pasta ap1 "
        "  JOIN cadeia c ON ap1.id = c.pasta_pai_id WHERE c.prof < 64"
        ") SELECT MIN(ativo) FROM cadeia), 1), "
        "COALESCE(i.marcado_revisado, 0) != 0, "
        "CASE WHEN json_valid(a.caracteristicas_tecnicas_json) THEN CAST(json_extract(a.caracteristicas_tecnicas_json, '$.duracaoSegundos') AS REAL) ELSE NULL END, "
        "CASE WHEN json_valid(a.caracteristicas_tecnicas_json) THEN json_extract(a.caracteristicas_tecnicas_json, '$.exifDataOriginal') ELSE NULL END, "
        "i.dc_subject, "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'dc_created'), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'data_criacao'), "
        "i.lista, "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'etapa_producao'), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'versao_escolhida'), "
        "EXISTS(SELECT 1 FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'ai_generated' AND TRIM(COALESCE(c.valor, '')) != ''), "
        "EXISTS(SELECT 1 FROM asset_geolocation g WHERE g.asset_id = i.id AND ("
        "g.latitude IS NOT NULL OR g.longitude IS NOT NULL OR "
        "TRIM(COALESCE(g.formatted_address, '')) != '' OR TRIM(COALESCE(g.city, '')) != '' OR "
        "TRIM(COALESCE(g.state_province, '')) != '' OR TRIM(COALESCE(g.country, '')) != '')), "
        // Original Source Medium (Phone Dump decide a categoria): sem ele aqui a grade nunca via o Phone Dump e a
        // releitura de um item editado (obterItemResumo, que lê) o mudava de categoria — a foto sumia após um GET.
        "i.source_media "
        "FROM item i "
        "LEFT JOIN arquivo a ON a.item_id = i.id AND a.id = ("
        "  SELECT a2.id FROM arquivo a2 WHERE a2.item_id = i.id ORDER BY a2.eh_master DESC, a2.id LIMIT 1"
        ") "
        "LEFT JOIN vault v ON v.id = a.vault_id "
        "WHERE COALESCE(i.em_quarentena, 0) = 0 AND COALESCE(i.estado, '') != 'duplicata'" + filtroIds + " ORDER BY i.codigo_acervo");
    if (soIds != nullptr) stmt.bind(1, matriz::db::Value::of(idsJson));

    const auto nests = mapaDeNests(registro);  // uma consulta: item -> nest
    auto minStmt = indice.prepare(
        "SELECT caminho_relativo FROM miniatura WHERE item_id = ? AND tipo = 'miniatura' ORDER BY gerado_em DESC LIMIT 1");
    auto tagStmt = registro.prepare(
        "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id IN ('tags', 'genero', 'estilo', 'palavras_chave') AND valor IS NOT NULL AND valor != ''");

    while (stmt.step()) {
        // O ORDER BY força o SQLite a resolver todas as linhas dentro do primeiro step();
        // daqui em diante o custo é miniatura/tags por item — esse sim dá pra contar.
        if (aoProgredir && (out.size() % 200 == 0)) aoProgredir(static_cast<int>(out.size()), totalLinhas);
        ItemResumo r;
        r.id = stmt.columnText(0);
        r.codigoAcervo = stmt.columnText(1);
        r.titulo = stmt.columnText(2);
        r.tipoMidia = stmt.columnText(3);
        r.estado = stmt.columnText(4);
        r.atualizadoEm = stmt.columnText(5);
        r.sincronizado = stmt.columnInt(6) != 0;
        if (!stmt.columnIsNull(7)) r.artistaLancamento = stmt.columnText(7);
        if (!stmt.columnIsNull(8)) r.tituloLancamento = stmt.columnText(8);
        if (!stmt.columnIsNull(9)) {
            juce::String caminho9 = stmt.columnText(9);
            int dotPos = caminho9.lastIndexOfChar('.');
            if (dotPos >= 0)
                r.extensaoArquivo = caminho9.substring(dotPos + 1).toLowerCase().toStdString();
        }
        if (!stmt.columnIsNull(10)) r.origem = stmt.columnText(10);
        if (!stmt.columnIsNull(11)) {
            // EVENT DATE (item.ano, o mesmo que a ficha mostra) em qualquer formato ("2019",
            // "2019-05-04", "04/05/2019"): vale o ano dele. Vazio ou "0" = sem ano (Unknown).
            r.ano = extrairAnoDeData(juce::String::fromUTF8(stmt.columnText(11).c_str()));
        }
        if (!stmt.columnIsNull(15)) r.contentType = stmt.columnText(15);
        if (!stmt.columnIsNull(16)) r.collectionType = stmt.columnText(16);
        r.criadoEm = stmt.columnText(17);
        if (auto nit = nests.find(r.id); nit != nests.end()) {
            r.nestId = nit->second.nestId;
            r.nestCapaId = nit->second.capaId;
            r.nestTotal = nit->second.total;
            r.nestCapa = (r.id == nit->second.capaId);
            r.nestOrigem = nit->second.origem;
            if (!nit->second.capaImagem.empty())
                r.nestCapaImagem = pastaProjeto.getChildFile(juce::String(nit->second.capaImagem)).getFullPathName().toStdString();
            r.nestIcone = nit->second.icone;
            r.nestNome = nit->second.nome;
            // A capa do container mostra o NOME DO CONTAINER (da ficha) ou, sem nome, o rótulo do tipo — nunca o nome de um arquivo.
            if (r.nestCapa && !r.nestOrigem.empty()) r.titulo = !r.nestNome.empty() ? r.nestNome : i18n::t("intake.container_" + juce::String(r.nestOrigem)).toStdString();
        }

        std::string masterArqId = stmt.columnIsNull(18) ? "" : stmt.columnText(18);
        std::string vaultLoc = stmt.columnIsNull(19) ? "" : stmt.columnText(19);
        std::string camRel = stmt.columnIsNull(9) ? "" : stmt.columnText(9);
        std::string camAbs = stmt.columnIsNull(12) ? "" : stmt.columnText(12);

        r.masterArquivoId = masterArqId;
        r.caminhoRelativoArquivo = camRel;
        r.caminhoAbsolutoOrigem = camAbs;
        if (!stmt.columnIsNull(20)) r.isrc = stmt.columnText(20);
        if (!stmt.columnIsNull(21)) r.marcadoPublicacao = stmt.columnInt(21) != 0;
        if (!stmt.columnIsNull(22)) r.metadadosEditados = stmt.columnInt(22) != 0;
        r.pastaAtiva = stmt.columnInt(23) != 0;
        r.marcadoRevisado = stmt.columnInt(24) != 0;
        if (!stmt.columnIsNull(27)) {
            std::string subj = stmt.columnText(27);
            if (!subj.empty()) r.subject = subj;
        }
        // Data do metadado (mesma regra do Intake, ver listarItensEmQuarentena):
        // sem isto a coluna DATE CREATED do Grid caía em criado_em (data do ingest).
        if (!stmt.columnIsNull(30)) {
            std::string lst = stmt.columnText(30);
            if (!lst.empty()) r.lista = lst;
        }
        if (!stmt.columnIsNull(31)) r.etapaProducao = stmt.columnText(31);
        if (!stmt.columnIsNull(32)) r.versaoEscolhida = stmt.columnText(32) == "true";
        r.aiGenerated = stmt.columnInt(33) != 0;
        r.temGeolocalizacao = stmt.columnInt(34) != 0;
        if (!stmt.columnIsNull(35)) r.sourceMedia = stmt.columnText(35);
        if (!stmt.columnIsNull(28)) r.dataCriacao = stmt.columnText(28);
        else if (!stmt.columnIsNull(29)) r.dataCriacao = stmt.columnText(29);

        // Status offline: o cache (preenchido em background na abertura) evita
        // stat por item; só os que estão no cache são re-verificados em disco
        // (relink pode tê-los trazido de volta). Sem cache: verifica todos.
        if (itensOffline == nullptr || itensOffline->count(r.id) > 0)
        {
            if (!resolvedor) resolvedor = std::make_unique<matriz::vault::ResolvedorEmLote>(registro, pastaProjeto);
            r.offline = !arquivoMasterExiste(*resolvedor, inMemoryRelinks, masterArqId, vaultLoc, camRel, camAbs);
        }

        // Características técnicas via json_extract em SQL (sem juce::JSON::parse em C++)
        if (!stmt.columnIsNull(25)) {
            r.duracaoSegundos = stmt.columnReal(25);
        }
        // Sem EVENT DATE = Unknown: nem EXIF, nem dc_created, nem a data do arquivo no disco entram
        // aqui (o ingest já copia o que achou para item.ano; o que sobra vazio é Unknown de verdade).

        // Check thumbnail from indice database (statement preparado uma vez)
        try {
            minStmt.reset();
            minStmt.bind(1, matriz::db::Value::of(r.id));
            if (minStmt.step() && !minStmt.columnIsNull(0)) {
                r.miniaturaCaminhoRelativo = minStmt.columnText(0);
            }
        } catch (...) {}

        // Extract tags / genres from item_campo (statement preparado uma vez)
        try {
            tagStmt.reset();
            tagStmt.bind(1, matriz::db::Value::of(r.id));
            while (tagStmt.step()) {
                juce::String tagVal = tagStmt.columnText(0);
                juce::StringArray parts;
                parts.addTokens(tagVal, ",;", "\"");
                for (auto& p : parts) {
                    juce::String trimmed = p.trim();
                    if (trimmed.isNotEmpty()) r.tags.push_back(trimmed.toStdString());
                }
            }
        } catch (...) {}

        if (!r.titulo.empty()) {
            r.nomeOriginalArquivo = r.titulo;
        } else if (!stmt.columnIsNull(9)) {
            juce::String caminho9 = stmt.columnText(9);
            int slashPos = std::max(caminho9.lastIndexOfChar('/'), caminho9.lastIndexOfChar('\\'));
            r.nomeOriginalArquivo = (slashPos >= 0 ? caminho9.substring(slashPos + 1) : caminho9).toStdString();
        } else if (!stmt.columnIsNull(12)) {
            juce::String caminho12 = stmt.columnText(12);
            int slashPos = std::max(caminho12.lastIndexOfChar('/'), caminho12.lastIndexOfChar('\\'));
            r.nomeOriginalArquivo = (slashPos >= 0 ? caminho12.substring(slashPos + 1) : caminho12).toStdString();
        }

        if (!stmt.columnIsNull(13)) r.pastaNome = stmt.columnText(13);
        r.tamanhoBytes = stmt.columnIsNull(14) ? 0 : static_cast<juce::int64>(stmt.columnInt(14));

        out.push_back(std::move(r));
    }
    return out;
}

int ProjetoAberto::contarItens() const {
    if (!projeto_) return 0;
    // Mesmo filtro de listarItensDeProjeto() (fora da quarentena).
    auto st = leitura().prepare("SELECT COUNT(*) FROM item WHERE COALESCE(em_quarentena, 0) = 0 AND COALESCE(estado, '') != 'duplicata'");
    return st.step() ? static_cast<int>(st.columnInt(0)) : 0;
}

std::vector<ItemResumo> ProjetoAberto::listarItens(const std::function<void(int feitos, int total)>& aoProgredir,
                                                   const std::set<std::string>* soIds) const {
    if (!projeto_) return {};
    // Cópia das marcações/relinks em memória sob marcacoesMutex_ (comentário
    // no membro, ProjetoAberto.h) -- este método roda em background
    // (MosaicoComponent::recarregar() via poolSnapshot_/thread
    // "MatrizSnapshot") enquanto a message thread pode estar escrevendo
    // nesses mesmos sets/map ao mesmo tempo (H/K/P/W, relink).
    std::map<std::string, std::string> relinkCopia;
    std::set<std::string> htmlCopia, zipCopia, printCopia, watermarkCopia, bounceCopia, rejeitadoCopia, offlineCopia, impressosCopia;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        relinkCopia = inMemoryRelinkedPaths_;
        htmlCopia = marcadosHtml_;
        zipCopia = marcadosZip_;
        printCopia = marcadosPrint_;
        watermarkCopia = marcadosWatermark_;
        bounceCopia = marcadosBounce_;
        rejeitadoCopia = marcadosRejeitado_;
        for (const auto& par : sessoesPrintPorItem_) impressosCopia.insert(par.first);
        offlineCopia = itensOfflineCache_;
    }
    auto items = listarItensDeProjeto(leitura(), leituraIndice(), projeto_->pasta(), relinkCopia, &offlineCopia, aoProgredir, soIds);
    for (auto& item : items) {
        item.marcadoPublicacao = htmlCopia.count(item.id) > 0;
        item.marcadoZip = zipCopia.count(item.id) > 0;
        item.marcadoPrint = printCopia.count(item.id) > 0;
        item.marcadoWatermark = watermarkCopia.count(item.id) > 0;
        item.marcadoBounce = bounceCopia.count(item.id) > 0;
        item.marcadoRejeitado = rejeitadoCopia.count(item.id) > 0;
        item.impresso = impressosCopia.count(item.id) > 0;
    }
    return items;
}

ProjetoAberto::ResultadoSanitizacao ProjetoAberto::sanitizarDuplicata(matriz::db::Database& registro,
                                                                      const std::string& manterId,
                                                                      const std::string& descartarId,
                                                                      const std::set<std::string>& usarDescartado) {
    using matriz::db::Value;
    ResultadoSanitizacao r;
    if (manterId.empty() || descartarId.empty() || manterId == descartarId) return r;
    // Par com algum lado já duplicata (resolvido antes, no mesmo lote ou noutro): não faz nada. Resolver de novo,
    // ainda mais com a escolha invertida, escondia as duas cópias — a foto saía do app e do backup.
    {
        auto st = registro.prepare("SELECT COUNT(*) FROM item WHERE id IN (?, ?) AND estado = 'duplicata'");
        st.bind(1, Value::of(manterId));
        st.bind(2, Value::of(descartarId));
        if (st.step() && st.columnInt(0) > 0) return r;
    }
    r.idMantido = manterId;
    const std::string agora = matriz::model::agoraIso8601();

    // Colunas de metadado do próprio `item` que entram na soma.
    static const char* const kColunas[] = {
        "tipo_midia", "ano", "content_type", "source_media", "collection_type", "isrc",
        "dc_title", "dc_creator", "dc_subject", "dc_description", "dc_publisher", "dc_contributor",
        "dc_created", "dc_issued", "dc_type", "dc_format", "dc_identifier", "dc_source",
        "dc_language", "dc_relation", "dc_coverage", "dc_rights", "lista"};
    std::string cols = "codigo_acervo, titulo, COALESCE(notas_livres, '')";
    for (auto* c : kColunas) cols += std::string(", COALESCE(") + c + ", '')";
    auto lerItem = [&](const std::string& id, std::vector<std::string>& out) {
        auto st = registro.prepare("SELECT " + cols + " FROM item WHERE id = ?");
        st.bind(1, Value::of(id));
        if (!st.step()) return false;
        const int n = 3 + static_cast<int>(std::size(kColunas));
        for (int i = 0; i < n; ++i) out.push_back(st.columnText(i));
        return true;
    };
    std::vector<std::string> k, d;
    if (!lerItem(manterId, k) || !lerItem(descartarId, d)) return r;
    r.codigoMantido = k[0];
    r.codigoDescartado = d[0];

    // Onde o arquivo do descartado está (SOURCE) — fica gravado no mantido.
    std::string arquivoDescartado, localDescartado;
    {
        auto st = registro.prepare(std::string("SELECT a.id, ") + matriz::vault::colunasDeResolucao() +
                                   " FROM arquivo a " + matriz::vault::joinDeResolucao() +
                                   " WHERE a.item_id = ? ORDER BY a.eh_master DESC, a.id LIMIT 1");
        st.bind(1, Value::of(descartarId));
        if (st.step()) {
            arquivoDescartado = st.columnText(0);
            const std::string loc = st.columnText(1), rel = st.columnText(2), abs = st.columnText(3);
            localDescartado = !abs.empty() ? abs
                            : (!loc.empty() ? juce::File(juce::String(loc)).getChildFile(juce::String(rel))
                                                  .getFullPathName().toStdString()
                                            : rel);
        }
    }
    {
        auto st = registro.prepare("SELECT 1 FROM consolidacao_registro WHERE item_id = ? LIMIT 1");
        st.bind(1, Value::of(descartarId));
        r.descartadoJaNoMain = st.step();
    }

    // 1-4. Fase 4 (MergeFichas): listas somam (tags/pessoas, subjects,
    // marcadores, assuntos); valor único: vazio preenche, datas compatíveis
    // ficam na mais precisa, diferente de verdade vale o do mantido (ou o
    // escolhido na tela) e o perdedor vai pro item_historico.
    const auto juncao = matriz::model::merge::juntarFichas(
        registro, manterId, descartarId, usarDescartado, matriz::model::merge::origemDoDescartado(registro, descartarId));
    r.camposSomados = juncao.camposSomados;
    r.conflitos = static_cast<int>(juncao.conflitos.size());

    // 5. Notas — sempre append, nunca sobrescreve.
    auto acrescentar = [](const std::string& atual, const juce::String& bloco) {
        juce::String s = juce::String::fromUTF8(atual.c_str()).trimEnd();
        return (s.isEmpty() ? bloco : s + "\n\n" + bloco).toStdString();
    };
    juce::String blocoMantido = "[DUPLICATE MERGED] " + juce::String(agora) + juce::String::fromUTF8(" — duplicate ") +
                                juce::String::fromUTF8(r.codigoDescartado.c_str()) + " (\"" +
                                juce::String::fromUTF8(d[1].c_str()) + "\") discarded; its file stays in SOURCE at " +
                                juce::String::fromUTF8(localDescartado.c_str()) + " and is not copied to backup.";
    if (r.conflitos > 0)
        blocoMantido << "\n" << juce::String(r.conflitos)
                     << " merge conflict(s): this item's values stayed; the duplicate's are in the item history "
                        "(filter \"Merge conflicts\").";
    if (!d[2].empty())
        blocoMantido << "\nNotes from the duplicate:\n" << juce::String::fromUTF8(d[2].c_str());
    registro.run("UPDATE item SET notas_livres = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                 {Value::of(acrescentar(k[2], blocoMantido)), Value::of(agora), Value::of(manterId)});

    juce::String blocoDescartado = "Validated as duplicate of " + juce::String::fromUTF8(r.codigoMantido.c_str()) +
                                   juce::String::fromUTF8(" — discarded side: file stays in SOURCE, excluded from backup; metadata merged "
                                   "into the kept item. [USER_VERIFIED_DUPLICATE]");
    registro.run("UPDATE item SET estado = 'duplicata', notas_livres = ?, atualizado_em = ? WHERE id = ?",
                 {Value::of(acrescentar(d[2], blocoDescartado)), Value::of(agora), Value::of(descartarId)});

    // 6. Log (PREMIS no banco; `linhasLog` pro log.md do projeto).
    const std::string detalhe = "Duplicate resolved: kept " + r.codigoMantido + ", discarded " + r.codigoDescartado +
                                " (file stays in SOURCE: " + localDescartado + ")";
    try {
    matriz::preservation::registrarEvento(registro, manterId, {}, matriz::preservation::EventType::Validation,
                                          detalhe, matriz::preservation::Outcome::Success,
                                          "Merged " + std::to_string(r.camposSomados) + " field(s)", "bkr-agent-sistema");
    matriz::preservation::registrarEvento(registro, descartarId, arquivoDescartado,
                                          matriz::preservation::EventType::Validation, detalhe,
                                          matriz::preservation::Outcome::Success,
                                          "Excluded from backup; nothing deleted", "bkr-agent-sistema");
    } catch (...) {}  // evento é registro auxiliar: não derruba a resolução

    r.linhasLog.add("Kept: " + juce::String::fromUTF8(r.codigoMantido.c_str()) + " (\"" +
                    juce::String::fromUTF8(k[1].c_str()) + "\")");
    r.linhasLog.add("Discarded: " + juce::String::fromUTF8(r.codigoDescartado.c_str()) + " (\"" +
                    juce::String::fromUTF8(d[1].c_str()) + "\") — stays in SOURCE, excluded from backup");
    r.linhasLog.add("Discarded file location: " + juce::String::fromUTF8(localDescartado.c_str()));
    r.linhasLog.add("Fields merged into kept item: " + juce::String(r.camposSomados) +
                    ", merge conflicts (kept in the item history): " + juce::String(r.conflitos));
    if (r.descartadoJaNoMain)
        r.linhasLog.add(juce::String::fromUTF8("The discarded item already had a copy in MAIN — left untouched (nothing is deleted)."));
    return r;
}

ProjetoAberto::ResultadoIntakePacote ProjetoAberto::aplicarPacoteIngerido(
    const matriz::consolidacao::pacote::Pacote& pacote, const std::vector<std::string>& itensIngeridos) {
    namespace pk = matriz::consolidacao::pacote;
    namespace nomes = matriz::model::nomes;
    using matriz::db::Value;
    ResultadoIntakePacote r;
    if (!projeto_) return r;
    if (somenteLeitura_) { r.erro = "read-only project"; return r; }
    auto& db = projeto_->registro();
    const std::string agora = matriz::model::agoraIso8601();
    const std::string projetoId = projeto_->projetoId();
    const std::set<std::string> doPacote(itensIngeridos.begin(), itensIngeridos.end());
    r.entraram = static_cast<int>(itensIngeridos.size());

    std::multimap<std::string, size_t> porSha;
    for (size_t i = 0; i < pacote.arquivos.size(); ++i) porSha.emplace(pacote.arquivos[i].sha256, i);
    std::vector<bool> usado(pacote.arquivos.size(), false);
    const juce::String sep = juce::String::charToString(0x1f);  // nomes de pasta podem ter '/'

    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    try {
        auto vocabTags = nomes::Vocabulario::carregar(db, nomes::Vocabulario::Tipo::Tags);
        auto vocabSubjects = nomes::Vocabulario::carregar(db, nomes::Vocabulario::Tipo::Subjects);
        db.exec("BEGIN IMMEDIATE");

        // 1. Folder map novo com o nome do pacote.
        {
            std::set<juce::String> existentes;
            auto st = db.prepare("SELECT nome FROM folder_map WHERE projeto_id = ?");
            st.bind(1, Value::of(projetoId));
            while (st.step()) existentes.insert(juce::String::fromUTF8(st.columnText(0).c_str()).trim());
            const juce::String base = pacote.nome().trim().isEmpty() ? juce::String("Package") : pacote.nome().trim();
            r.folderMap = base;
            for (int n = 2; existentes.count(r.folderMap); ++n) r.folderMap = base + " (" + juce::String(n) + ")";
        }
        const std::string mapaId = matriz::model::novoUuid();
        {
            int ordem = 0;
            auto st = db.prepare("SELECT COALESCE(MAX(ordem), -1) + 1 FROM folder_map WHERE projeto_id = ?");
            st.bind(1, Value::of(projetoId));
            if (st.step()) ordem = static_cast<int>(st.columnInt(0));
            db.run("INSERT INTO folder_map (id, projeto_id, nome, ordem, criado_em, atualizado_em) VALUES (?, ?, ?, ?, ?, ?)",
                   {Value::of(mapaId), Value::of(projetoId), Value::of(r.folderMap.toStdString()), Value::of(ordem),
                    Value::of(agora), Value::of(agora)});
        }
        std::map<juce::String, std::string> pastaPorCaminho;
        std::function<void(const juce::var&, const std::string&, const juce::String&)> criarPastas =
            [&](const juce::var& lista, const std::string& paiId, const juce::String& prefixo) {
                auto* arr = lista.getArray();
                if (!arr) return;
                int ordem = 0;
                for (const auto& no : *arr) {
                    const juce::String nome = no.getProperty("nome", {}).toString().trim();
                    if (nome.isEmpty()) continue;
                    const juce::String chave = prefixo.isEmpty() ? nome : prefixo + sep + nome;
                    std::string id;
                    if (auto it = pastaPorCaminho.find(chave); it != pastaPorCaminho.end()) {
                        id = it->second;  // irmãs com o mesmo nome viram uma pasta só
                    } else {
                        id = matriz::model::novoUuid();
                        db.run("INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mapa_id, posicao_x, "
                               "posicao_y, ativo, criado_em, atualizado_em) VALUES (?, ?, ?, ?, ?, ?, 0, 0, 1, ?, ?)",
                               {Value::of(id), Value::of(projetoId), paiId.empty() ? Value::null() : Value::of(paiId),
                                Value::of(nome.toStdString()), Value::of(ordem++), Value::of(mapaId), Value::of(agora),
                                Value::of(agora)});
                        pastaPorCaminho[chave] = id;
                    }
                    criarPastas(no.getProperty("pastas", {}), id, chave);
                }
            };
        criarPastas(pacote.pastas, {}, {});

        // 2. Cada item novo: casa pelo SHA-256, grava a ficha, põe na pasta.
        const std::string autor = "Package: " + pacote.nome().toStdString();
        for (const auto& itemId : itensIngeridos) {
            std::string sha, abs;
            {
                auto st = db.prepare("SELECT COALESCE(checksum_sha256, ''), COALESCE(caminho_absoluto_origem, '') "
                                     "FROM arquivo WHERE item_id = ? ORDER BY eh_master DESC LIMIT 1");
                st.bind(1, Value::of(itemId));
                if (st.step()) {
                    sha = juce::String(st.columnText(0)).toLowerCase().toStdString();
                    abs = st.columnText(1);
                }
            }
            // De onde veio (Fase 4: origem do valor perdedor num merge de duplicata).
            db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                   "VALUES (?, ?, 'raiz', 0, 'pacote_origem', ?, 'leitura_tecnica', ?) "
                   "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor",
                   {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(pacote.nome().toStdString()),
                    Value::of(agora)});
            const juce::File arq(juce::String::fromUTF8(abs.c_str()));
            const std::string rel = arq.getRelativePathFrom(pacote.media()).replaceCharacter('\\', '/').toStdString();
            if (!sha.empty()) {
                auto st = db.prepare("SELECT item_id FROM arquivo WHERE checksum_sha256 = ? AND item_id <> ?");
                st.bind(1, Value::of(sha));
                st.bind(2, Value::of(itemId));
                bool existia = false;
                while (!existia && st.step()) existia = doPacote.count(st.columnText(0)) == 0;
                if (existia) ++r.jaExistiam;
            }
            std::optional<size_t> escolhido;
            auto faixa = porSha.equal_range(sha);
            for (auto it = faixa.first; it != faixa.second; ++it) {
                if (usado[it->second]) continue;
                if (!escolhido || pacote.arquivos[it->second].caminho == rel) escolhido = it->second;
                if (pacote.arquivos[it->second].caminho == rel) break;
            }
            if (sha.empty() || !escolhido) {
                ++r.semCorrespondencia;
                r.semCorrespondenciaNomes.add(arq.getFileName());
                continue;
            }
            usado[*escolhido] = true;
            const auto& reg = pacote.arquivos[*escolhido];
            if (!reg.dados.vazio()) {
                pk::gravarDadosFicha(db, itemId, reg.dados, vocabTags, vocabSubjects, autor);
                ++r.comDados;
            }
            if (!reg.pasta.empty()) {
                juce::StringArray partes;
                for (const auto& n : reg.pasta) partes.add(n);
                if (auto it = pastaPorCaminho.find(partes.joinIntoString(sep)); it != pastaPorCaminho.end())
                    inserirItemPastaInterno(itemId, it->second, agora);
            }
        }
        for (size_t i = 0; i < pacote.arquivos.size(); ++i)
            if (!usado[i]) {
                ++r.registrosSemArquivo;
                r.registrosSemArquivoCaminhos.add(juce::String::fromUTF8(pacote.arquivos[i].caminho.c_str()));
            }
        db.exec("COMMIT");
    } catch (const std::exception& e) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        r.erro = e.what();
        return r;
    }
    r.ok = true;
    try {
        juce::StringArray linhas;
        linhas.add("Package: " + pacote.pasta.getFullPathName() + " (from \"" + pacote.colecao + "\", folder map \"" +
                   pacote.folderMap + "\", exported " + pacote.exportadoEm + ")");
        linhas.add("Files in: " + juce::String(r.entraram) + ", with catalog data: " + juce::String(r.comDados) +
                   ", without a match: " + juce::String(r.semCorrespondencia) + ", already in this collection: " +
                   juce::String(r.jaExistiam) + ", catalog records without a file: " + juce::String(r.registrosSemArquivo));
        linhas.add("Folder map created: " + r.folderMap);
        for (int i = 0; i < r.semCorrespondenciaNomes.size() && i < 50; ++i)
            linhas.add("No catalog match: " + r.semCorrespondenciaNomes[i]);
        for (int i = 0; i < r.registrosSemArquivoCaminhos.size() && i < 50; ++i)
            linhas.add("Catalog record without file: " + r.registrosSemArquivoCaminhos[i]);
        matriz::model::ProjectLog(projeto_->pasta()).appendEntry("Collection Package Ingested", linhas);
    } catch (...) {}
    return r;
}

void ProjetoAberto::resolverDuplicatasEmSegundoPlano(std::vector<std::string> itensAfetados, CorpoResolucao corpo,
                                                     std::function<void(bool, std::vector<ResultadoSanitizacao>)> aoConcluir,
                                                     const std::string& descricaoUndo) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || !corpo) return;
    std::weak_ptr<bool> vivo = vivo_;
    poolMerge_.addJob([this, vivo, itensAfetados, corpo, aoConcluir, descricaoUndo] {
        bool ok = false;
        std::vector<ResultadoSanitizacao> resultados;
        std::shared_ptr<matriz::model::merge::Retrato> retrato;
        {
            std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
            auto& db = projeto_->registro();
            try {
                db.exec("BEGIN IMMEDIATE");
                retrato = matriz::model::merge::retratar(db, itensAfetados);
                resultados = corpo(db);
                db.exec("COMMIT");
                ok = true;
            } catch (const std::exception& e) {
                try { db.exec("ROLLBACK"); } catch (...) {}
                juce::Logger::writeToLog("[merge] resolucao desfeita: " + juce::String(e.what()));
            }
        }
        juce::MessageManager::callAsync([this, vivo, ok, resultados, retrato, aoConcluir, descricaoUndo] {
            if (!vivo.lock()) return;
            if (ok && retrato)
                registrarUndo(descricaoUndo, [this, retrato] { restaurarRetratoMerge(*retrato); });
            if (aoConcluir) aoConcluir(ok, resultados);
        });
    });
}

void ProjetoAberto::restaurarRetratoMerge(const matriz::model::merge::Retrato& retrato) {
    if (!projeto_) return;
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    auto& db = projeto_->registro();
    try {
        db.exec("BEGIN IMMEDIATE");
        matriz::model::merge::restaurar(db, retrato);
        db.exec("COMMIT");
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        return;
    }
    EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");
}

void ProjetoAberto::simularJuncaoEmSegundoPlano(const std::string& manterId, const std::string& descartarId,
                                                std::function<void(std::vector<matriz::model::merge::Conflito>)> aoConcluir) {
    if (!projeto_) return;
    std::weak_ptr<bool> vivo = vivo_;
    poolMerge_.addJob([this, vivo, manterId, descartarId, aoConcluir] {
        std::vector<matriz::model::merge::Conflito> conflitos;
        try {
            conflitos = matriz::model::merge::juntarFichas(projeto_->registro(), manterId, descartarId, {}, {}, /*simular*/ true)
                            .conflitos;
        } catch (...) {}
        juce::MessageManager::callAsync([vivo, conflitos, aoConcluir] {
            if (vivo.lock() && aoConcluir) aoConcluir(conflitos);
        });
    });
}

bool ProjetoAberto::temConflitoMergePendente(const std::string& itemId) const {
    if (!projeto_) return false;
    try {
        return !matriz::model::merge::conflitosPendentes(leitura(), itemId).empty();
    } catch (...) {
        return false;
    }
}

std::vector<matriz::model::merge::ConflitoPendente> ProjetoAberto::conflitosMergePendentes(const std::string& itemId) const {
    if (!projeto_) return {};
    try {
        return matriz::model::merge::conflitosPendentes(leitura(), itemId);
    } catch (...) {
        return {};
    }
}

void ProjetoAberto::revisarConflitosMerge(const std::string& itemId, std::set<std::string> trocarHistoricoIds) {
    resolverDuplicatasEmSegundoPlano(
        {itemId},
        [itemId, trocarHistoricoIds](matriz::db::Database& db) {
            matriz::model::merge::revisarConflitos(db, itemId, trocarHistoricoIds);
            return std::vector<ResultadoSanitizacao>{};
        },
        [itemId](bool ok, std::vector<ResultadoSanitizacao>) {
            if (ok) EventBus::obterInstancia().dispararItemAlterado(itemId, "metadado");
        },
        "Review Merge Conflicts");
}

void ProjetoAberto::carregarConflitosMergeEmSegundoPlano(
    std::set<std::string> escopo,
    std::function<void(std::vector<matriz::model::merge::GrupoConflito>, std::map<std::string, juce::String>)> aoConcluir) {
    if (!projeto_) return;
    std::weak_ptr<bool> vivo = vivo_;
    poolMerge_.addJob([this, vivo, escopo, aoConcluir] {
        std::vector<matriz::model::merge::GrupoConflito> grupos;
        std::map<std::string, juce::String> miniaturas;
        try {
            grupos = matriz::model::merge::gruposDeConflitoPendentes(leitura(), escopo);
            for (const auto& g : grupos)
                if (!miniaturas.count(g.itemId))
                    if (auto c = caminhoMiniaturaPrincipal(g.itemId)) miniaturas[g.itemId] = *c;
        } catch (const std::exception& e) {
            juce::Logger::writeToLog("[merge] leitura dos conflitos falhou: " + juce::String(e.what()));
        }
        juce::MessageManager::callAsync([vivo, grupos = std::move(grupos), miniaturas = std::move(miniaturas), aoConcluir]() mutable {
            if (vivo.lock() && aoConcluir) aoConcluir(std::move(grupos), std::move(miniaturas));
        });
    });
}

void ProjetoAberto::resolverConflitosMergeEmSegundoPlano(
    std::vector<std::pair<matriz::model::merge::GrupoConflito, std::string>> escolhas, std::function<void(bool)> aoConcluir) {
    std::vector<std::string> itens;
    for (const auto& [g, valor] : escolhas)
        if (std::find(itens.begin(), itens.end(), g.itemId) == itens.end()) itens.push_back(g.itemId);
    resolverDuplicatasEmSegundoPlano(
        itens,
        [escolhas](matriz::db::Database& db) {
            for (const auto& [g, valor] : escolhas) matriz::model::merge::resolverGrupo(db, g, valor);
            return std::vector<ResultadoSanitizacao>{};
        },
        [itens, aoConcluir](bool ok, std::vector<ResultadoSanitizacao>) {
            if (ok) {
                // Lote grande: um aviso de recarga só (mesmo limite da resolução de duplicatas).
                if (itens.size() > 20) EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");
                else for (const auto& id : itens) EventBus::obterInstancia().dispararItemAlterado(id, "metadado");
            }
            if (aoConcluir) aoConcluir(ok);
        },
        "Resolve Merge Conflicts");
}

std::vector<ItemResumo> ProjetoAberto::listarItensDaColecao(const juce::File& pastaColecao) const {
    juce::File resolvedDir = matriz::model::Project::resolverPastaProjeto(pastaColecao);
    juce::File regFile = resolvedDir.getChildFile("registro.sqlite");
    juce::File indFile = resolvedDir.getChildFile("indice.sqlite");
    if (!regFile.existsAsFile()) return {};

    try {
        matriz::db::Database regDb(regFile.getFullPathName().toStdString());
        std::vector<ItemResumo> items;
        // Outra coleção: o cache offline é do projeto aberto, não serve aqui.
        if (indFile.existsAsFile()) {
            matriz::db::Database indDb(indFile.getFullPathName().toStdString());
            items = listarItensDeProjeto(regDb, indDb, resolvedDir);
        } else {
            // Temporary in-memory dummy db if indice.sqlite is missing
            matriz::db::Database dummyInd(":memory:");
            items = listarItensDeProjeto(regDb, dummyInd, resolvedDir);
        }
        {
            std::lock_guard<std::mutex> lock(marcacoesMutex_);
            for (auto& item : items) {
                item.marcadoPublicacao = marcadosHtml_.count(item.id) > 0;
                item.marcadoZip = marcadosZip_.count(item.id) > 0;
                item.marcadoPrint = marcadosPrint_.count(item.id) > 0;
                item.marcadoWatermark = marcadosWatermark_.count(item.id) > 0;
                item.marcadoBounce = marcadosBounce_.count(item.id) > 0;
                item.marcadoRejeitado = marcadosRejeitado_.count(item.id) > 0;
                item.impresso = sessoesPrintPorItem_.count(item.id) > 0;
            }
        }
        return items;
    } catch (...) {
        return {};
    }
}

std::vector<ItemResumo> ProjetoAberto::listarItensEmQuarentena() const {
    std::vector<ItemResumo> out;
    if (!projeto_) return out;
    std::unique_ptr<matriz::vault::ResolvedorEmLote> resolvedor;  // só se algum item precisar checar disco

    // O Intake pintava cada card disparando uma consulta separada no
    // indice.sqlite. Carrega os caminhos uma vez no snapshot de background.
    std::unordered_map<std::string, std::string> miniaturas;
    try {
        auto minStmt = leituraIndice().prepare(
            "SELECT item_id, caminho_relativo FROM miniatura "
            "WHERE tipo = 'miniatura' ORDER BY item_id, gerado_em DESC");
        while (minStmt.step())
            miniaturas.emplace(minStmt.columnText(0), minStmt.columnText(1));
    } catch (...) {}

    // Mesma cópia sob lock de listarItens() -- este método também roda em
    // background (IntakeWorkspaceComponent) enquanto a message thread pode
    // escrever nesses sets/map ao mesmo tempo.
    std::map<std::string, std::string> relinkCopia;
    std::set<std::string> htmlCopia, zipCopia, printCopia, watermarkCopia, bounceCopia, rejeitadoCopia, offlineCopia, impressosCopia;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        relinkCopia = inMemoryRelinkedPaths_;
        htmlCopia = marcadosHtml_;
        zipCopia = marcadosZip_;
        printCopia = marcadosPrint_;
        watermarkCopia = marcadosWatermark_;
        bounceCopia = marcadosBounce_;
        rejeitadoCopia = marcadosRejeitado_;
        for (const auto& par : sessoesPrintPorItem_) impressosCopia.insert(par.first);
        offlineCopia = itensOfflineCache_;
    }

    // Retry loop in case SQLite is momentarily busy during ingest transactions
    for (int tentativa = 0; tentativa < 3; ++tentativa) {
        try {
            out.clear();
            const auto nests = mapaDeNests(leitura());
            auto stmt = leitura().prepare(
                "SELECT i.id, i.codigo_acervo, i.titulo, i.tipo_midia, i.estado, i.atualizado_em, "
                "EXISTS(SELECT 1 FROM consolidacao_registro cr WHERE cr.item_id = i.id), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
                " AND c.campo_id = 'artista_principal'), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
                " AND c.campo_id = 'titulo'), "
                "(SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
                " AND c.campo_id = 'origem'), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
                " AND c.campo_id = 'ano'), "
                "(SELECT a.caminho_absoluto_origem FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "(SELECT ap.nome FROM acervo_item_pasta aip JOIN acervo_pasta ap ON ap.id = aip.pasta_id WHERE aip.item_id = i.id LIMIT 1), "
                "(SELECT a.tamanho_bytes FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "i.content_type, i.collection_type, i.criado_em, "
                "(SELECT a.id FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "(SELECT COALESCE(v.localizacao, '') FROM arquivo a LEFT JOIN vault v ON v.id = a.vault_id WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
                "0, "
                "COALESCE(i.metadados_editados, 0) != 0, "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'source_media'), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'dc_created'), "
                "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'data_criacao'), "
                "(SELECT a.caracteristicas_tecnicas_json FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1) "
                "FROM item i WHERE i.em_quarentena = 1 AND COALESCE(i.estado, '') != 'duplicata' ORDER BY i.criado_em DESC, i.id");
            while (stmt.step()) {
                ItemResumo r;
                r.id = stmt.columnText(0);
                r.codigoAcervo = stmt.columnText(1);
                r.titulo = stmt.columnText(2);
                r.tipoMidia = stmt.columnText(3);
                r.estado = stmt.columnText(4);
                r.atualizadoEm = stmt.columnText(5);
                r.sincronizado = stmt.columnInt(6) != 0;
                if (!stmt.columnIsNull(7)) r.artistaLancamento = stmt.columnText(7);
                if (!stmt.columnIsNull(8)) r.tituloLancamento = stmt.columnText(8);
                if (!stmt.columnIsNull(9)) {
                    juce::String caminho9 = stmt.columnText(9);
                    int dotPos = caminho9.lastIndexOfChar('.');
                    if (dotPos >= 0)
                        r.extensaoArquivo = caminho9.substring(dotPos + 1).toLowerCase().toStdString();
                }
                if (!stmt.columnIsNull(10)) r.origem = stmt.columnText(10);
                if (!stmt.columnIsNull(11)) {
                    juce::String anoTexto = stmt.columnText(11);
                    if (anoTexto.containsOnly("0123456789") && anoTexto.isNotEmpty()) r.ano = anoTexto.getIntValue();
                }
                if (!stmt.columnIsNull(15)) r.contentType = stmt.columnText(15);
                if (!stmt.columnIsNull(16)) r.collectionType = stmt.columnText(16);
                r.criadoEm = stmt.columnText(17);
                r.marcadoPublicacao = htmlCopia.count(r.id) > 0;
                r.marcadoZip = zipCopia.count(r.id) > 0;
                r.marcadoPrint = printCopia.count(r.id) > 0;
                r.marcadoWatermark = watermarkCopia.count(r.id) > 0;
                r.marcadoBounce = bounceCopia.count(r.id) > 0;
                r.marcadoRejeitado = rejeitadoCopia.count(r.id) > 0;
                r.impresso = impressosCopia.count(r.id) > 0;
                if (!stmt.columnIsNull(21)) r.metadadosEditados = stmt.columnInt(21) != 0;

                std::string masterArqId = stmt.columnIsNull(18) ? "" : stmt.columnText(18);
                std::string vaultLoc = stmt.columnIsNull(19) ? "" : stmt.columnText(19);
                std::string camRel = stmt.columnIsNull(9) ? "" : stmt.columnText(9);
                std::string camAbs = stmt.columnIsNull(12) ? "" : stmt.columnText(12);

                r.masterArquivoId = masterArqId;
                r.caminhoRelativoArquivo = camRel;
                r.caminhoAbsolutoOrigem = camAbs;
                if (auto mini = miniaturas.find(r.id); mini != miniaturas.end())
                    r.miniaturaCaminhoRelativo = mini->second;

                // Mesmo critério de listarItensDeProjeto: re-verifica só os do cache.
                if (offlineCopia.count(r.id) > 0)
                {
                    if (!resolvedor)
                        resolvedor = std::make_unique<matriz::vault::ResolvedorEmLote>(leitura(), projeto_->pasta());
                    r.offline = !arquivoMasterExiste(*resolvedor, relinkCopia, masterArqId, vaultLoc, camRel, camAbs);
                }

                if (!r.titulo.empty()) {
                    r.nomeOriginalArquivo = r.titulo;
                } else if (!stmt.columnIsNull(9)) {
                    juce::String caminho9 = stmt.columnText(9);
                    int slashPos = std::max(caminho9.lastIndexOfChar('/'), caminho9.lastIndexOfChar('\\'));
                    r.nomeOriginalArquivo = (slashPos >= 0 ? caminho9.substring(slashPos + 1) : caminho9).toStdString();
                } else if (!stmt.columnIsNull(12)) {
                    juce::String caminho12 = stmt.columnText(12);
                    int slashPos = std::max(caminho12.lastIndexOfChar('/'), caminho12.lastIndexOfChar('\\'));
                    r.nomeOriginalArquivo = (slashPos >= 0 ? caminho12.substring(slashPos + 1) : caminho12).toStdString();
                }

                if (!stmt.columnIsNull(13)) r.pastaNome = stmt.columnText(13);
                r.tamanhoBytes = stmt.columnIsNull(14) ? 0 : static_cast<juce::int64>(stmt.columnInt(14));

                if (!stmt.columnIsNull(22)) r.sourceMedia = stmt.columnText(22);
                if (!stmt.columnIsNull(23)) r.dataCriacao = stmt.columnText(23);
                else if (!stmt.columnIsNull(24)) r.dataCriacao = stmt.columnText(24);

                if (!stmt.columnIsNull(25)) {
                    auto jsonStr = stmt.columnText(25);
                    auto varObj = juce::JSON::parse(jsonStr);
                    if (varObj.isObject()) {
                        if (varObj.hasProperty("duracaoSegundos")) {
                            r.duracaoSegundos = static_cast<double>(varObj["duracaoSegundos"]);
                        }
                        if (!r.ano.has_value() && varObj.hasProperty("exifDataOriginal")) {
                            juce::String exifDt = varObj["exifDataOriginal"].toString();
                            for (int i = 0; i + 3 < exifDt.length(); ++i) {
                                if (std::isdigit(exifDt[i]) && std::isdigit(exifDt[i+1]) &&
                                    std::isdigit(exifDt[i+2]) && std::isdigit(exifDt[i+3])) {
                                    int yVal = exifDt.substring(i, i + 4).getIntValue();
                                    if (yVal > 1800 && yVal <= juce::Time::getCurrentTime().getYear() + 1) { r.ano = yVal; break; }
                                }
                            }
                        }
                    }
                }

                if (auto nit = nests.find(r.id); nit != nests.end()) {  // container de importação / nest
                    r.nestId = nit->second.nestId;
                    r.nestCapaId = nit->second.capaId;
                    r.nestTotal = nit->second.total;
                    r.nestCapa = (r.id == nit->second.capaId);
                    r.nestOrigem = nit->second.origem;
                    if (!nit->second.capaImagem.empty())
                        r.nestCapaImagem = projeto_->pasta().getChildFile(juce::String(nit->second.capaImagem)).getFullPathName().toStdString();
                    r.nestIcone = nit->second.icone;
                    r.nestNome = nit->second.nome;
                    if (r.nestCapa && !r.nestOrigem.empty()) r.titulo = !r.nestNome.empty() ? r.nestNome : i18n::t("intake.container_" + juce::String(r.nestOrigem)).toStdString();
                }
                out.push_back(std::move(r));
            }
            break; // Success!
        } catch (...) {
            if (tentativa < 2) {
                juce::Thread::sleep(20);
            }
        }
    }
    return out;
}

void ProjetoAberto::registrarUndoEnvioAoGrid(const std::vector<std::string>& itemIds) {
    if (desfazendo_ || !projeto_) return;
    std::vector<std::pair<std::string, std::string>> antes;  // id -> lote_grid_id anterior
    for (const auto& id : itemIds) {
        auto st = projeto_->registro().prepare("SELECT COALESCE(lote_grid_id, '') FROM item WHERE id = ? AND em_quarentena = 1");
        st.bind(1, matriz::db::Value::of(id));
        if (st.step()) antes.push_back({id, st.columnText(0)});
    }
    if (antes.empty()) return;
    registrarUndo(antes.size() == 1 ? "Send to Grid" : "Send " + std::to_string(antes.size()) + " Items to Grid",
                  [this, antes]() {
        auto& d = projeto_->registro();
        d.run("BEGIN TRANSACTION", {});
        try {
            for (const auto& [id, lote] : antes)
                d.run("UPDATE item SET em_quarentena = 1, lote_grid_id = ? WHERE id = ?",
                      {lote.empty() ? matriz::db::Value::null() : matriz::db::Value::of(lote), matriz::db::Value::of(id)});
            d.run("COMMIT", {});
        } catch (...) {
            try { d.run("ROLLBACK", {}); } catch (...) {}
            return;
        }
        ultimosItensIngeridosValido_ = false;
        for (const auto& [id, lote] : antes) EventBus::obterInstancia().dispararItemAlterado(id, "quarentena");
    });
}

std::vector<std::string> ProjetoAberto::semMarcadosR(const std::vector<std::string>& itemIds) const {
    const auto marcados = idsMarcadosR();
    if (marcados.empty()) return itemIds;
    std::vector<std::string> out;
    out.reserve(itemIds.size());
    for (const auto& id : itemIds)
        if (marcados.count(id) == 0) out.push_back(id);
    return out;
}

void ProjetoAberto::confirmarItemGrid(const std::string& itemId) {
    if (!projeto_ || itemId.empty()) return;
    if (idsMarcadosR().count(itemId) > 0) return;  // marcado com R: não entra no grid de jeito nenhum
    registrarUndoEnvioAoGrid({itemId});
    std::string agora = matriz::model::agoraIso8601();
    projeto_->registro().exec("BEGIN IMMEDIATE");
    try {
        projeto_->registro().run(
            "UPDATE item SET em_quarentena = 0, atualizado_em = ? WHERE id = ?",
            {matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
        projeto_->registro().run("DELETE FROM intake_marca_r WHERE item_id = ?", {matriz::db::Value::of(itemId)});
        projeto_->registro().exec("COMMIT");
    } catch (...) {
        try { projeto_->registro().exec("ROLLBACK"); } catch (...) {}
        throw;
    }
    EventBus::obterInstancia().dispararItemAlterado(itemId, "quarentena");
}

void ProjetoAberto::confirmarLoteGrid(const std::vector<std::string>& todosOsIdsPedidos) {
    if (!projeto_ || todosOsIdsPedidos.empty()) return;
    // CONTAINER de importação: aprovar a capa aprova todos os membros; o container se dissolve (no Grid são itens comuns).
    std::vector<std::string> todosOsIds = todosOsIdsPedidos;
    std::set<std::string> containersParaDissolver;
    {
        const auto nests = mapaDeNests(projeto_->registro());
        std::set<std::string> ja(todosOsIds.begin(), todosOsIds.end());
        for (const auto& id : todosOsIdsPedidos) {
            auto it = nests.find(id);
            if (it == nests.end() || it->second.origem.empty()) continue;
            // O container (galeria/Drive/Bridge/Lightroom/conversa) CONTINUA sendo um nest no Grid/Metadata: só se separa quando
            // o usuário escolhe UN-NEST no botão direito.
            for (const auto& m : membrosDoNest(it->second.nestId))
                if (ja.insert(m).second) todosOsIds.push_back(m);
        }
    }
    const auto itemIds = semMarcadosR(todosOsIds);  // marcado com R: não entra no grid de jeito nenhum
    if (itemIds.empty()) return;
    for (const auto& nid : containersParaDissolver) {
        try {
            projeto_->registro().run("DELETE FROM nest_item WHERE nest_id = ?", {matriz::db::Value::of(nid)});
            projeto_->registro().run("DELETE FROM nest WHERE id = ?", {matriz::db::Value::of(nid)});
        } catch (...) {}
    }
    registrarUndoEnvioAoGrid(itemIds);
    std::string agora = matriz::model::agoraIso8601();
    // Id da leva: ms desde a época (zero-padded, ordena como texto) + uuid
    // pra desempatar duas promoções no mesmo ms.
    std::string loteGridId = juce::String(juce::Time::currentTimeMillis()).paddedLeft('0', 16).toStdString() +
                             "-" + matriz::model::novoUuid();
    projeto_->registro().run("BEGIN TRANSACTION", {});
    try {
        for (const auto& id : itemIds) {
            projeto_->registro().run(
                "UPDATE item SET em_quarentena = 0, atualizado_em = ?, lote_grid_id = ? WHERE id = ?",
                {matriz::db::Value::of(agora), matriz::db::Value::of(loteGridId), matriz::db::Value::of(id)});
            projeto_->registro().run("DELETE FROM intake_marca_r WHERE item_id = ?", {matriz::db::Value::of(id)});
        }
        projeto_->registro().run("COMMIT", {});
    } catch (...) {
        projeto_->registro().run("ROLLBACK", {});
        throw;
    }
    // Item "RECENTLY INGESTED": o gatilho é a promoção INTAKE -> GRID (este
    // método), não o ingest bruto em si — um conjunto só vira "recém
    // ingerido" quando o operador aprova e manda pra grade; o próximo lote
    // aprovado substitui este — relido do banco (lote_grid_id mais recente).
    // Antes dos eventos: quem os recebe já enxerga a leva nova.
    ultimosItensIngeridosValido_ = false;
    for (const auto& id : itemIds) {
        EventBus::obterInstancia().dispararItemAlterado(id, "quarentena");
    }
}

std::vector<ProjetoAberto::ItemDetalhe> ProjetoAberto::obterDetalhesItens(const std::set<std::string>& itemIds) const {
    std::vector<ItemDetalhe> out;
    if (!projeto_ || itemIds.empty()) return out;
    out.reserve(itemIds.size());

    auto stmt = leitura().prepare(
        "SELECT i.id, i.titulo, "
        "(SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
        "(SELECT a.tamanho_bytes FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1) "
        "FROM item i WHERE i.id = ?");

    for (const auto& id : itemIds) {
        stmt.reset();
        stmt.bind(1, matriz::db::Value::of(id));
        if (!stmt.step()) continue;

        ItemDetalhe d;
        d.id = stmt.columnText(0);
        auto titulo = stmt.columnText(1);
        auto caminho = stmt.columnIsNull(2) ? std::string{} : stmt.columnText(2);

        if (!titulo.empty()) {
            d.nome = titulo;
        } else if (!caminho.empty()) {
            juce::String c(caminho);
            int slashPos = std::max(c.lastIndexOfChar('/'), c.lastIndexOfChar('\\'));
            d.nome = (slashPos >= 0 ? c.substring(slashPos + 1) : c).toStdString();
        }

        if (!caminho.empty()) {
            juce::String c(caminho);
            int dotPos = c.lastIndexOfChar('.');
            if (dotPos >= 0)
                d.extensao = c.substring(dotPos + 1).toLowerCase().toStdString();
        }

        d.tamanhoBytes = stmt.columnIsNull(3) ? 0 : static_cast<juce::int64>(stmt.columnInt(3));
        out.push_back(std::move(d));
    }
    return out;
}

const matriz::ficha::FichaDefinition& ProjetoAberto::definicaoPara(const std::string& tipoMidia) {
    auto it = definicoesCache_.find(tipoMidia);
    if (it != definicoesCache_.end()) return it->second;

    // Fichas embutidas no binário (FichasEmbutidas.cpp), não lidas da pasta do código-fonte.
    const auto& embutidas = fichasEmbutidas();
    auto yaml = embutidas.find(tipoMidia);
    if (yaml == embutidas.end())
        throw ProjetoAbertoError("nenhuma definição de ficha encontrada para o tipo \"" + tipoMidia + "\"");

    auto [inserido, ok] = definicoesCache_.emplace(tipoMidia, matriz::ficha::loadFromString(yaml->second, tipoMidia + ".yaml"));
    return inserido->second;
}

std::optional<std::string> ProjetoAberto::valorCampo(const std::string& itemId, const std::string& nivel,
                                                       int nivelIndice, const std::string& campoId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = leitura().prepare(
        "SELECT valor FROM item_campo WHERE item_id = ? AND nivel = ? AND nivel_indice = ? AND campo_id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    stmt.bind(2, matriz::db::Value::of(nivel));
    stmt.bind(3, matriz::db::Value::of(nivelIndice));
    stmt.bind(4, matriz::db::Value::of(campoId));
    if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
    return std::nullopt;
}

struct ParteUndoLinhas;
static void registrarUndoDeLinhas(ProjetoAberto& pa, const std::string& descricao, std::vector<ParteUndoLinhas> partes,
                                  std::vector<std::string> itensAvisar, const std::string& tipoEvento,
                                  std::vector<ProjetoAberto::ParteUndo> extras = {});
static void registrarUndoDeCampoRaiz(ProjetoAberto& pa, const std::string& itemId, const std::string& campoId);
static void registrarUndoDoContainer(ProjetoAberto& pa, const std::string& descricao, const std::string& nestId, bool capa);

void ProjetoAberto::definirCampoRaiz(const std::string& itemId, const std::string& campoId, const std::string& valor) {
    if (somenteLeitura_ || !projeto_ || itemId.empty()) return;
    registrarUndoDeCampoRaiz(*this, itemId, campoId);  // Versão escolhida / Etapa de produção (Preview)
    projeto_->registro().run(
        "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
        "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
        "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(campoId),
         matriz::db::Value::of(valor), matriz::db::Value::of(matriz::model::agoraIso8601())});
    EventBus::obterInstancia().dispararItemAlterado(itemId, "campo");
}

std::vector<std::string> ProjetoAberto::papeisArquivoPresentes(const std::string& itemId) const {
    std::vector<std::string> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare("SELECT DISTINCT papel FROM arquivo WHERE item_id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

static std::string idsEmJson(const std::vector<std::string>& ids) {
    juce::Array<juce::var> lista;
    for (const auto& id : ids) lista.add(juce::String::fromUTF8(id.c_str()));
    return juce::JSON::toString(juce::var(lista), true).toStdString();
}

// Undo de linhas com filtro: cada parte é (tabela, onde, params, noIndice). Guarda agora (chamar ANTES de gravar);
// a volta apaga o que bate com o filtro e regrava o guardado, na ordem dada. Persistente (sobrevive a reabrir).
struct ParteUndoLinhas {
    std::string tabela, onde;
    std::vector<matriz::db::Value> params;
    bool noIndice = false;
};
static void registrarUndoDeLinhas(ProjetoAberto& pa, const std::string& descricao, std::vector<ParteUndoLinhas> partes,
                                  std::vector<std::string> itensAvisar, const std::string& tipoEvento,
                                  std::vector<ProjetoAberto::ParteUndo> extras) {
    std::vector<ProjetoAberto::ParteUndo> ps;
    for (auto& pt : partes) {
        ProjetoAberto::ParteUndo p;
        p.tabela = pt.tabela;
        p.onde = pt.onde;
        p.params = pt.params;
        p.noIndice = pt.noIndice;
        ps.push_back(std::move(p));
    }
    for (auto& e : extras) ps.push_back(std::move(e));
    pa.registrarUndoPersistente(descricao, ps, itensAvisar, tipoEvento);
}

// Undo de container (NEST): a linha do nest e os membros voltam como estavam (o nest se apaga com cascata, por isso
// os dois juntos, nest antes) e, com `capa`, também a imagem de capa no disco (caminho fixo, sobrescrito/apagado).
static void registrarUndoDoContainer(ProjetoAberto& pa, const std::string& descricao, const std::string& nestId, bool capa) {
    if (nestId.empty()) return;
    std::vector<ProjetoAberto::ParteUndo> extras;
    if (capa) {
        ProjetoAberto::ParteUndo a;
        a.tipo = ProjetoAberto::ParteUndo::Tipo::Arquivo;
        a.arquivo = pa.projeto().pasta().getChildFile(".miniaturas/capa_container_" + juce::String(nestId) + ".jpg");
        extras.push_back(std::move(a));
    }
    registrarUndoDeLinhas(pa, descricao,
                          {{"nest", "id = ?", {matriz::db::Value::of(nestId)}, false},
                           {"nest_item", "nest_id = ?", {matriz::db::Value::of(nestId)}, false}},
                          {""}, "nest", std::move(extras));
}

static void registrarUndoDeCampoRaiz(ProjetoAberto& pa, const std::string& itemId, const std::string& campoId) {
    registrarUndoDeLinhas(pa, "Edit " + campoId,
                          {{"item_campo", "item_id = ? AND nivel = 'raiz' AND nivel_indice = 0 AND campo_id = ?",
                            {matriz::db::Value::of(itemId), matriz::db::Value::of(campoId)}, false}},
                          {itemId}, "campo");
}

// Undo da capa do item (Set/Remove Cover): só as linhas 'capa_frente' de `arquivo` e as miniaturas do item no índice
// (a da capa nova sai, as de antes voltam). Chamar ANTES de gravar.
static void registrarUndoDeCapa(ProjetoAberto& pa, const std::string& descricao, const std::vector<std::string>& itemIds) {
    if (itemIds.empty()) return;
    const std::string json = idsEmJson(itemIds);
    registrarUndoDeLinhas(pa, descricao,
                          {{"arquivo", "item_id IN (SELECT value FROM json_each(?)) AND papel = 'capa_frente'", {matriz::db::Value::of(json)}, false},
                           {"miniatura", "item_id IN (SELECT value FROM json_each(?))", {matriz::db::Value::of(json)}, true}},
                          itemIds, "miniatura");
}

int ProjetoAberto::definirCapa(const std::vector<std::string>& itemIds, const juce::File& imagem) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return 0; }
    if (!projeto_ || !imagem.existsAsFile()) return 0;
    registrarUndoDeCapa(*this, itemIds.size() == 1 ? "Set Cover" : "Set Cover (" + std::to_string(itemIds.size()) + " items)", itemIds);

    int aplicadas = 0;
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (auto& itemId : itemIds) {
            // Uma capa por item: trocar substitui a anterior em vez de
            // empilhar capas que ninguém mais consegue distinguir. Não chama
            // removerCapa() aqui — abriria uma segunda transação dentro
            // desta (SQLite não suporta BEGIN aninhado); mesma lógica dela,
            // sem BEGIN/COMMIT próprio (item 6, fix de performance).
            {
                auto stmt = db.prepare("SELECT id FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'");
                stmt.bind(1, matriz::db::Value::of(itemId));
                std::vector<std::string> capasAntigas;
                while (stmt.step()) capasAntigas.push_back(stmt.columnText(0));
                for (auto& arquivoIdAntigo : capasAntigas)
                    projeto_->indice().run("DELETE FROM miniatura WHERE arquivo_id = ?",
                                            {matriz::db::Value::of(arquivoIdAntigo)});
                db.run("DELETE FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'",
                       {matriz::db::Value::of(itemId)});
            }

            std::string arquivoId = matriz::model::novoUuid();
            juce::File destino = projeto_->pasta().getChildFile("arquivos").getChildFile(arquivoId).getChildFile(
                imagem.getFileName());
            destino.getParentDirectory().createDirectory();
            if (!imagem.copyFileTo(destino)) continue; // falha num item não derruba os outros

            std::string agora = matriz::model::agoraIso8601();
            db.run(
                "INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, "
                "criado_em, atualizado_em) VALUES (?, ?, ?, ?, 'capa_frente', 0, ?, ?, ?)",
                {matriz::db::Value::of(arquivoId), matriz::db::Value::of(itemId),
                 matriz::db::Value::of(destino.getRelativePathFrom(projeto_->pasta()).toStdString()),
                 matriz::db::Value::of(imagem.getFullPathName().toStdString()),
                 matriz::db::Value::of(static_cast<long long>(destino.getSize())),
                 matriz::db::Value::of(agora),
                 matriz::db::Value::of(agora)});

            // Grava a miniatura por cima: caminhoMiniaturaPrincipal() resolve por
            // gerado_em DESC, então a linha nova passa a valer sem precisar
            // apagar a gerada — e apagar a capa depois faz a antiga voltar sozinha.
            matriz::ingest::gerarEGravarMiniaturaPrincipal(projeto_->indice(), projeto_->pasta(), itemId, arquivoId,
                                                            destino, matriz::ingest::CategoriaMidia::Imagem,
                                                            std::nullopt);
            ++aplicadas;
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
    return aplicadas;
}

void ProjetoAberto::removerCapa(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    registrarUndoDeCapa(*this, itemIds.size() == 1 ? "Remove Cover" : "Remove Cover (" + std::to_string(itemIds.size()) + " items)", itemIds);
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (auto& itemId : itemIds) {
            // Primeiro as miniaturas geradas A PARTIR da capa (no índice, que é
            // outro banco — não há CASCADE entre os dois).
            auto stmt = db.prepare(
                "SELECT id FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'");
            stmt.bind(1, matriz::db::Value::of(itemId));
            std::vector<std::string> capas;
            while (stmt.step()) capas.push_back(stmt.columnText(0));

            for (auto& arquivoId : capas)
                projeto_->indice().run("DELETE FROM miniatura WHERE arquivo_id = ?",
                                        {matriz::db::Value::of(arquivoId)});

            db.run("DELETE FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'",
                   {matriz::db::Value::of(itemId)});
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

bool ProjetoAberto::temCapa(const std::string& itemId) const {
    if (!projeto_) return false;
    auto stmt = leitura().prepare(
        "SELECT 1 FROM arquivo WHERE item_id = ? AND papel = 'capa_frente' LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    return stmt.step();
}

bool ProjetoAberto::algumTemCapa(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return false;
    constexpr size_t kBloco = 400;  // abaixo do teto de variáveis do SQLite
    for (size_t ini = 0; ini < itemIds.size(); ini += kBloco) {
        const size_t fim = std::min(itemIds.size(), ini + kBloco);
        std::string sql = "SELECT 1 FROM arquivo WHERE papel = 'capa_frente' AND item_id IN (?";
        for (size_t k = ini + 1; k < fim; ++k) sql += ",?";
        sql += ") LIMIT 1";
        auto stmt = leitura().prepare(sql);
        for (size_t k = ini; k < fim; ++k) stmt.bind(static_cast<int>(k - ini) + 1, matriz::db::Value::of(itemIds[k]));
        if (stmt.step()) return true;
    }
    return false;
}

std::vector<std::string> ProjetoAberto::valoresUsadosNoCampo(const std::string& campoId) const {
    std::vector<std::string> out;
    if (!projeto_) return out;
    // Restrito aos itens DESTE projeto: o registro é por projeto, mas a
    // junção deixa isso explícito e resiste a um banco que um dia guarde
    // mais de um. Valor vazio não é vocabulário — é campo não preenchido.
    auto stmt = leitura().prepare(
        "SELECT DISTINCT ic.valor FROM item_campo ic JOIN item i ON i.id = ic.item_id "
        "WHERE i.projeto_id = ? AND ic.campo_id = ? AND ic.valor IS NOT NULL AND TRIM(ic.valor) <> '' "
        "ORDER BY ic.valor COLLATE NOCASE");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(campoId));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

namespace {
// Colunas que lerMetadado/valorComumEmLote leem (o nome entra no SQL: nada fora desta lista).
bool colunaDeMetadadoLegivel(const std::string& coluna) {
    static const std::set<std::string> kColunasPermitidas = {
        "ano", "caminho_catalogo", "content_type", "source_media", "collection_type", "isrc", "notas_livres", "titulo", "lista",
        "ai_generated",  // sem isto a ficha nunca via o campo (sempre nullopt) e o seletor AI generated aparecia desmarcado
        "dc_title", "dc_creator", "dc_subject", "dc_description", "dc_publisher", "dc_contributor",
        "dc_created", "dc_issued", "dc_type", "dc_format", "dc_identifier", "dc_source",
        "dc_language", "dc_relation", "dc_coverage", "dc_rights",
        // "As 3 datas do sistema" — DATE CREATED é um campo nativo do BKR
        // (nunca Dublin Core): já existia como "data_criacao" em item_campo
        // (preenchido no ingest, ver IngestArquivo.cpp), só faltava estar
        // liberado aqui pra virar campo editável de verdade na ficha.
        "data_criacao",
        // DATE ISSUED (item 11): quando o asset foi ingerido no BKR Matriz —
        // já existe como item.criado_em (setado uma vez, na criação da
        // linha), só somente-leitura aqui, nunca gravado via salvarMetadado.
        "criado_em"
    };
    return kColunasPermitidas.count(coluna) > 0;
}
}  // namespace

std::optional<std::string> ProjetoAberto::lerMetadado(const std::string& itemId, const std::string& coluna) const {
    if (!projeto_) return std::nullopt;
    if (!colunaDeMetadadoLegivel(coluna)) return std::nullopt;

    try {
        auto stmt = leitura().prepare("SELECT " + coluna + " FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step() && !stmt.columnIsNull(0)) {
            std::string val = stmt.columnText(0);
            if (!val.empty()) return val;
        }
    } catch (...) {}

    try {
        auto stmt = leitura().prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        stmt.bind(2, matriz::db::Value::of(coluna));
        if (stmt.step() && !stmt.columnIsNull(0)) {
            std::string val = stmt.columnText(0);
            if (!val.empty()) return val;
        }
    } catch (...) {}

    return std::nullopt;
}

std::optional<std::string> ProjetoAberto::valorComumEmLote(const std::vector<std::string>& itemIds, const std::string& coluna) const {
    if (itemIds.empty()) return std::string();
    if (!projeto_ || !colunaDeMetadadoLegivel(coluna)) return std::string();
    // Mesma regra de lerMetadado por item: a coluna do item, senão o item_campo; vazio conta como "".
    // Coluna que não existe na tabela item (ex.: data_criacao) só tem o item_campo.
    auto consulta = [&](const std::string& valorDoItem) {
        return leitura().prepare(
            "SELECT COUNT(DISTINCT v), MIN(v) FROM ("
            "  SELECT COALESCE(NULLIF(" + valorDoItem + ", ''), "
            "         NULLIF(CAST((SELECT c.valor FROM item_campo c WHERE c.item_id = j.value AND c.campo_id = ? LIMIT 1) AS TEXT), ''), '') AS v "
            "  FROM json_each(?) j LEFT JOIN item i ON i.id = j.value)");
    };
    try {
        std::optional<matriz::db::Statement> stmt;
        try { stmt.emplace(consulta("CAST(i." + coluna + " AS TEXT)")); }
        catch (...) { stmt.emplace(consulta("NULL")); }
        stmt->bind(1, matriz::db::Value::of(coluna));
        stmt->bind(2, matriz::db::Value::of(idsEmJson(itemIds)));
        if (!stmt->step()) return std::string();
        if (stmt->columnInt(0) > 1) return std::nullopt;
        return stmt->columnIsNull(1) ? std::string() : stmt->columnText(1);
    } catch (...) {
        return std::nullopt;  // na dúvida, "valores múltiplos": nunca grava um valor que não era de todos
    }
}

void ProjetoAberto::registrarUndo(const std::string& descricao, std::function<void()> acaoReversa) {
    if (desfazendo_ || !acaoReversa) return;
    // A pilha é da message thread: de outra thread, vira um passo próprio quando chegar lá.
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        std::weak_ptr<bool> vivo = vivo_;
        juce::MessageManager::callAsync([this, vivo, descricao, acaoReversa = std::move(acaoReversa)]() mutable {
            if (vivo.lock()) registrarUndoComoPassoProprio(descricao, std::move(acaoReversa));
        });
        return;
    }
    if (grupoAberto_) {
        grupoAberto_->acoesReversas.push_back(std::move(acaoReversa));
        grupoAberto_->tudoPersistente = false;  // esta ação só existe na memória: o grupo não volta ao reabrir
    } else {
        pilhaUndo_.push_back(UndoEntry{descricao, {std::move(acaoReversa)}});
        aparaPilhaUndo();
        if (aoMudarUndo) aoMudarUndo();
    }
}

void ProjetoAberto::registrarUndoComoPassoProprio(const std::string& descricao, std::function<void()> acaoReversa) {
    if (desfazendo_ || !acaoReversa) return;
    pilhaUndo_.push_back(UndoEntry{descricao, {std::move(acaoReversa)}});
    aparaPilhaUndo();
    if (aoMudarUndo) aoMudarUndo();
}

void ProjetoAberto::aparaPilhaUndo() {
    bool descartou = false;
    while (static_cast<int>(pilhaUndo_.size()) > kMaxUndo) {
        apagarPassoPersistente(pilhaUndo_.front().passoPersistente);
        pilhaUndo_.erase(pilhaUndo_.begin());
        descartou = true;
    }
    if (descartou && !avisouDescarteUndo_) {
        avisouDescarteUndo_ = true;
        if (aoDescartarUndoAntigo) aoDescartarUndoAntigo();
    }
}

void ProjetoAberto::registrarUndoDeColunas(const std::string& descricao, const std::string& tabela, const std::string& colunaChave,
                                           const std::string& valorChave, std::vector<std::string> colunas) {
    if (!projeto_ || desfazendo_ || colunas.empty()) return;
    // Persistente (fase 1 do histórico que sobrevive a fechar o projeto).
    ParteUndo p;
    p.tipo = ParteUndo::Tipo::Colunas;
    p.tabela = tabela;
    p.onde = "\"" + colunaChave + "\" = ?";
    p.params = {matriz::db::Value::of(valorChave)};
    p.colunas = std::move(colunas);
    registrarUndoPersistente(descricao, {p}, {}, "metadado");
}

// ---------------------------------------------------------------------------
// Undo persistente (ver ProjetoAberto.h, registrarUndoPersistente).
// ---------------------------------------------------------------------------
namespace {
// Uma linha como JSON (colunas binárias em hex, num objeto {"$b": ...}).
std::string expressaoJsonDaLinha(matriz::db::Database& d, const std::string& tabela, const std::vector<std::string>& soEstas) {
    std::vector<std::string> cols = soEstas;
    if (cols.empty()) {
        auto st = d.prepare("SELECT name FROM pragma_table_info(?)");
        st.bind(1, matriz::db::Value::of(tabela));
        while (st.step()) cols.push_back(st.columnText(0));
    }
    std::string e = "json_object(";
    for (size_t i = 0; i < cols.size(); ++i) {
        const std::string c = "\"" + cols[i] + "\"";
        e += (i ? ", '" : "'") + cols[i] + "', CASE WHEN typeof(" + c + ") = 'blob' THEN json_object('$b', hex(" + c + ")) ELSE " + c + " END";
    }
    return e + ")";
}

// O valor da coluna `c` a partir do JSON no parâmetro ?1.
std::string valorDoJson(const std::string& c) {
    const std::string caminho = "'$.\"" + c + "\"'";
    return "CASE WHEN json_type(?1, " + caminho + ") = 'object' THEN unhex(json_extract(?1, '$.\"" + c + "\".\"$b\"')) "
           "ELSE json_extract(?1, " + caminho + ") END";
}

std::string paramsParaJson(const std::vector<matriz::db::Value>& ps) {
    juce::Array<juce::var> a;
    for (const auto& v : ps) {
        switch (v.kind) {
            case matriz::db::Value::Kind::Text: a.add(juce::String::fromUTF8(v.text.c_str())); break;
            case matriz::db::Value::Kind::Int: a.add(static_cast<juce::int64>(v.integer)); break;
            case matriz::db::Value::Kind::Real: a.add(v.real); break;
            default: a.add(juce::var()); break;
        }
    }
    return juce::JSON::toString(juce::var(a), true).toStdString();
}

std::vector<matriz::db::Value> paramsDoJson(const std::string& json) {
    std::vector<matriz::db::Value> out;
    const auto v = juce::JSON::parse(juce::String::fromUTF8(json.c_str()));
    if (auto* a = v.getArray())
        for (const auto& x : *a) {
            if (x.isVoid() || x.isUndefined()) out.push_back(matriz::db::Value::null());
            else if (x.isInt() || x.isInt64()) out.push_back(matriz::db::Value::of(static_cast<long long>(static_cast<juce::int64>(x))));
            else if (x.isDouble()) out.push_back(matriz::db::Value::of(static_cast<double>(x)));
            else out.push_back(matriz::db::Value::of(x.toString().toStdString()));
        }
    return out;
}

std::vector<matriz::db::Value> comJsonNaFrente(const std::string& json, const std::vector<matriz::db::Value>& ps) {
    std::vector<matriz::db::Value> out{matriz::db::Value::of(json)};
    out.insert(out.end(), ps.begin(), ps.end());
    return out;
}
} // namespace

void ProjetoAberto::garantirTabelasUndo() {
    if (!projeto_ || somenteLeitura_) return;
    try {
        projeto_->registro().execScript(
            "CREATE TABLE IF NOT EXISTS undo_passo (id INTEGER PRIMARY KEY AUTOINCREMENT, descricao TEXT NOT NULL, "
            "  completo INTEGER NOT NULL DEFAULT 1, criado_em TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS undo_parte (id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "  passo_id INTEGER NOT NULL REFERENCES undo_passo(id) ON DELETE CASCADE, ordem INTEGER NOT NULL, tipo TEXT NOT NULL, "
            "  banco TEXT NOT NULL DEFAULT 'registro', tabela TEXT, onde TEXT, params_json TEXT, colunas_json TEXT, "
            "  arquivo TEXT, copia TEXT, linhas INTEGER NOT NULL DEFAULT 0, itens_json TEXT, tipo_evento TEXT);"
            "CREATE TABLE IF NOT EXISTS undo_linha (parte_id INTEGER NOT NULL REFERENCES undo_parte(id) ON DELETE CASCADE, "
            "  dados_json TEXT NOT NULL);"
            "CREATE INDEX IF NOT EXISTS idx_undo_parte_passo ON undo_parte(passo_id, ordem);"
            "CREATE INDEX IF NOT EXISTS idx_undo_linha_parte ON undo_linha(parte_id);");
    } catch (...) {}
}

// ordem = ação * kPartesPorAcao + posição: a volta roda as ações da última à primeira e, dentro de cada uma, as partes
// na ordem em que foram dadas (o container antes dos membros, por exemplo).
static constexpr int kPartesPorAcao = 10000;

bool ProjetoAberto::capturarPartesPersistentes(const std::string& descricao, const std::vector<ParteUndo>& partes,
                                               const std::vector<std::string>& itensAvisar, const std::string& tipoEvento,
                                               bool completo, int64_t& passo, int& de, int& ate, UndoEntry*& grupo) {
    if (desfazendo_ || somenteLeitura_ || !projeto_ || partes.empty()) return false;
    const bool naMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
    grupo = (naMessageThread && grupoAberto_) ? &*grupoAberto_ : nullptr;
    auto& r = projeto_->registro();
    auto& ind = projeto_->indice();
    const juce::File pasta = projeto_->pasta();
    std::unique_lock<std::recursive_mutex> lock(writeMutex());
    juce::Array<juce::var> itens;
    for (const auto& id : itensAvisar) itens.add(juce::String::fromUTF8(id.c_str()));
    const std::string itensJson = juce::JSON::toString(juce::var(itens), true).toStdString();
    passo = 0;
    int acao = 0;
    try {
        if (grupo && grupo->passoPersistente) {
            passo = grupo->passoPersistente;
            auto st = r.prepare("SELECT COALESCE(MAX(ordem), -1) FROM undo_parte WHERE passo_id = ?");
            st.bind(1, matriz::db::Value::of(static_cast<long long>(passo)));
            if (st.step()) acao = static_cast<int>(st.columnInt(0) < 0 ? 0 : st.columnInt(0) / kPartesPorAcao + 1);
        } else {
            // Num grupo, o passo só vale ao reabrir se o grupo inteiro for persistente (finalizarGrupoUndo decide).
            r.run("INSERT INTO undo_passo (descricao, completo, criado_em) VALUES (?, ?, ?)",
                  {matriz::db::Value::of(grupo ? grupo->descricao : descricao), matriz::db::Value::of(grupo || !completo ? 0 : 1),
                   matriz::db::Value::of(matriz::model::agoraIso8601())});
            auto st = r.prepare("SELECT last_insert_rowid()");
            if (st.step()) passo = st.columnInt(0);
            if (grupo) grupo->passoPersistente = passo;
        }
        int ordem = acao * kPartesPorAcao;
        de = ordem;
        for (const auto& pt : partes) {
            const char* tipo = pt.tipo == ParteUndo::Tipo::Linhas ? "linhas" : pt.tipo == ParteUndo::Tipo::Colunas ? "colunas"
                             : pt.tipo == ParteUndo::Tipo::Arquivo ? "arquivo" : "acao";
            juce::Array<juce::var> cols;
            for (const auto& c : pt.colunas) cols.add(juce::String::fromUTF8(c.c_str()));
            r.run("INSERT INTO undo_parte (passo_id, ordem, tipo, banco, tabela, onde, params_json, colunas_json, copia, itens_json, tipo_evento) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                  {matriz::db::Value::of(static_cast<long long>(passo)), matriz::db::Value::of(ordem++), matriz::db::Value::of(tipo),
                   matriz::db::Value::of(pt.noIndice ? "indice" : "registro"), matriz::db::Value::of(pt.tabela),
                   matriz::db::Value::of(pt.onde), matriz::db::Value::of(paramsParaJson(pt.params)),
                   matriz::db::Value::of(juce::JSON::toString(juce::var(cols), true).toStdString()),
                   pt.chave.empty() ? matriz::db::Value::null() : matriz::db::Value::of("chave:" + pt.chave),
                   matriz::db::Value::of(itensJson), matriz::db::Value::of(tipoEvento)});
            long long parte = 0;
            {
                auto st = r.prepare("SELECT last_insert_rowid()");
                if (st.step()) parte = st.columnInt(0);
            }
            if (pt.tipo == ParteUndo::Tipo::Acao) continue;
            if (pt.tipo == ParteUndo::Tipo::Arquivo) {
                const juce::String rel = matriz::caminhos::relativoParaBanco(pt.arquivo, pasta);
                juce::String copia;
                if (pt.arquivo.existsAsFile()) {
                    copia = ".undo/" + juce::String(parte) + ".bin";
                    pasta.getChildFile(".undo").createDirectory();
                    pt.arquivo.copyFileTo(pasta.getChildFile(copia));
                }
                r.run("UPDATE undo_parte SET arquivo = ?, copia = ? WHERE id = ?",
                      {matriz::db::Value::of(rel.toStdString()), copia.isEmpty() ? matriz::db::Value::null() : matriz::db::Value::of(copia.toStdString()),
                       matriz::db::Value::of(parte)});
                continue;
            }
            auto& d = pt.noIndice ? ind : r;
            std::vector<std::string> colsDaLinha;
            if (pt.tipo == ParteUndo::Tipo::Colunas) {
                colsDaLinha = pt.colunas;
                if (!pt.chave.empty() && std::find(colsDaLinha.begin(), colsDaLinha.end(), pt.chave) == colsDaLinha.end())
                    colsDaLinha.push_back(pt.chave);
            }
            const std::string sel = "SELECT " + expressaoJsonDaLinha(d, pt.tabela, colsDaLinha) + " FROM \"" + pt.tabela + "\" WHERE " + pt.onde;
            auto st = d.prepare(sel);
            for (size_t k = 0; k < pt.params.size(); ++k) st.bind(static_cast<int>(k + 1), pt.params[k]);
            long long n = 0;
            while (st.step()) {
                r.run("INSERT INTO undo_linha (parte_id, dados_json) VALUES (?, ?)", {matriz::db::Value::of(parte), matriz::db::Value::of(st.columnText(0))});
                ++n;
            }
            r.run("UPDATE undo_parte SET linhas = ? WHERE id = ?", {matriz::db::Value::of(n), matriz::db::Value::of(parte)});
        }
        ate = ordem - 1;
        return true;
    } catch (...) {
        // A captura falhou: sem Undo para esta ação (antes um passo pela metade). O passo incompleto sai na próxima abertura.
        try { if (passo && !grupo) r.run("UPDATE undo_passo SET completo = 0 WHERE id = ?", {matriz::db::Value::of(static_cast<long long>(passo))}); } catch (...) {}
        if (grupo) grupo->tudoPersistente = false;
        return false;
    }
}

void ProjetoAberto::empilharPassoPersistente(const std::string& descricao, int64_t passo, int de, int ate, UndoEntry* grupo) {
    auto acao = [this, passo, de, ate] { executarPartesPersistentes(passo, de, ate); };
    if (grupo) {
        grupo->acoesReversas.push_back(std::move(acao));
        return;
    }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        pilhaUndo_.push_back(UndoEntry{descricao, {std::move(acao)}, passo, true});
        aparaPilhaUndo();
        if (aoMudarUndo) aoMudarUndo();
        return;
    }
    std::weak_ptr<bool> vivo = vivo_;
    juce::MessageManager::callAsync([this, vivo, descricao, passo, acao = std::move(acao)]() mutable {
        if (!vivo.lock() || desfazendo_) return;
        pilhaUndo_.push_back(UndoEntry{descricao, {std::move(acao)}, passo, true});
        aparaPilhaUndo();
        if (aoMudarUndo) aoMudarUndo();
    });
}

void ProjetoAberto::registrarUndoPersistente(const std::string& descricao, const std::vector<ParteUndo>& partes,
                                             const std::vector<std::string>& itensAvisar, const std::string& tipoEvento) {
    int64_t passo = 0;
    int de = 0, ate = -1;
    UndoEntry* grupo = nullptr;
    if (!capturarPartesPersistentes(descricao, partes, itensAvisar, tipoEvento, true, passo, de, ate, grupo)) return;
    empilharPassoPersistente(descricao, passo, de, ate, grupo);
}

ProjetoAberto::Retrato ProjetoAberto::retratar(const std::vector<TabelaDoRetrato>& tabelas, const std::vector<std::string>& ids) {
    Retrato r;
    if (ids.empty()) return r;
    const std::string json = idsEmJson(ids);
    std::vector<ParteUndo> partes;
    for (const auto& t : tabelas) {
        ParteUndo p;
        p.tabela = t.tabela;
        p.onde = "\"" + t.chave + "\" IN (SELECT value FROM json_each(?))";
        p.params = {matriz::db::Value::of(json)};
        if (t.soAtualizar) {
            p.tipo = ParteUndo::Tipo::Colunas;
            p.chave = t.chave;
            auto st = projeto_->registro().prepare("SELECT name FROM pragma_table_info(?)");
            st.bind(1, matriz::db::Value::of(t.tabela));
            while (st.step()) p.colunas.push_back(st.columnText(0));
        }
        partes.push_back(std::move(p));
    }
    UndoEntry* grupo = nullptr;
    // incompleto até registrarUndoDeRetrato empilhar: se não empilhar, não volta ao reabrir
    if (!capturarPartesPersistentes("", partes, {}, "metadado", false, r.passo, r.de, r.ate, grupo)) return Retrato{};
    r.doGrupo = grupo != nullptr;
    return r;
}

void ProjetoAberto::registrarUndoDeRetrato(const std::string& descricao, Retrato retrato, std::vector<std::string> idsAvisar,
                                           std::vector<ParteUndo> acoesDepois, const std::string& tipoEvento) {
    if (retrato.empty() || !projeto_) return;
    auto& r = projeto_->registro();
    UndoEntry* grupo = retrato.doGrupo && grupoAberto_ ? &*grupoAberto_ : nullptr;
    try {
        std::unique_lock<std::recursive_mutex> lock(writeMutex());
        if (!grupo) r.run("UPDATE undo_passo SET descricao = ?, completo = 1 WHERE id = ?",
                          {matriz::db::Value::of(descricao), matriz::db::Value::of(static_cast<long long>(retrato.passo))});
        // avisos e ações no fim do mesmo intervalo (mesma ação)
        juce::Array<juce::var> itens;
        for (const auto& id : idsAvisar) itens.add(juce::String::fromUTF8(id.c_str()));
        r.run("UPDATE undo_parte SET itens_json = ?, tipo_evento = ? WHERE passo_id = ? AND ordem BETWEEN ? AND ?",
              {matriz::db::Value::of(juce::JSON::toString(juce::var(itens), true).toStdString()), matriz::db::Value::of(tipoEvento),
               matriz::db::Value::of(static_cast<long long>(retrato.passo)), matriz::db::Value::of(retrato.de), matriz::db::Value::of(retrato.ate)});
        int ordem = retrato.ate + 1;
        for (const auto& a : acoesDepois) {
            r.run("INSERT INTO undo_parte (passo_id, ordem, tipo, banco, tabela, onde, params_json, colunas_json) "
                  "VALUES (?, ?, 'acao', 'registro', ?, ?, '[]', '[]')",
                  {matriz::db::Value::of(static_cast<long long>(retrato.passo)), matriz::db::Value::of(ordem++),
                   matriz::db::Value::of(a.tabela), matriz::db::Value::of(a.onde)});
        }
        retrato.ate = ordem - 1;
    } catch (...) { return; }
    empilharPassoPersistente(descricao, retrato.passo, retrato.de, retrato.ate, grupo);
}

void ProjetoAberto::executarAcaoDeUndo(const std::string& nome, const std::string& dadosJson) {
    const auto v = juce::JSON::parse(juce::String::fromUTF8(dadosJson.c_str()));
    auto lista = [](const juce::var& x) {
        std::vector<std::string> out;
        if (auto* a = x.getArray()) for (const auto& e : *a) out.push_back(e.toString().toStdString());
        return out;
    };
    if (nome == "tags") {  // {"id": [tags]} — definirTags cuida de item_tag e da busca
        if (auto* o = v.getDynamicObject())
            for (const auto& prop : o->getProperties()) definirTags(prop.name.toString().toStdString(), lista(prop.value));
    } else if (nome == "marcas") {  // {"tipo": n, "ids": [...], "marcados": [...]}
        const auto tipo = static_cast<TipoMarcacao>(static_cast<int>(v["tipo"]));
        const auto ids = lista(v["ids"]);
        const auto marcados = lista(v["marcados"]);
        const std::set<std::string> m(marcados.begin(), marcados.end());
        std::vector<std::string> ligar, desligar;
        for (const auto& id : ids) (m.count(id) ? ligar : desligar).push_back(id);
        if (!ligar.empty()) definirMarcacao(tipo, ligar, true);
        if (!desligar.empty()) definirMarcacao(tipo, desligar, false);
    } else if (nome == "marca_r") {  // {"ids": [...], "marcados": [...]}
        const auto ids = lista(v["ids"]);
        const auto marcados = lista(v["marcados"]);
        const std::set<std::string> m(marcados.begin(), marcados.end());
        std::vector<std::string> ligar, desligar;
        {
            std::lock_guard<std::mutex> lock(marcacoesMutex_);
            for (const auto& id : ids) {
                const bool agoraTem = marcadosR_.count(id) > 0, tinha = m.count(id) > 0;
                if (tinha && !agoraTem) ligar.push_back(id);
                else if (!tinha && agoraTem) desligar.push_back(id);
            }
        }
        if (!ligar.empty()) alternarMarcaR(ligar);       // todos sem R: liga
        if (!desligar.empty()) alternarMarcaR(desligar); // todos com R: desliga
    } else if (nome == "pastas_novas") {  // {"mapa": id, "antes": [...]} — as pastas criadas depois somem
        const std::string mapa = v["mapa"].toString().toStdString();
        const auto antes = lista(v["antes"]);
        const std::set<std::string> a(antes.begin(), antes.end());
        if (mapa.empty() || !projeto_) return;
        auto& d = projeto_->registro();
        std::vector<std::string> novas;
        auto st = d.prepare("SELECT id FROM acervo_pasta WHERE mapa_id = ?");
        st.bind(1, matriz::db::Value::of(mapa));
        while (st.step()) if (!a.count(st.columnText(0))) novas.push_back(st.columnText(0));
        for (const auto& id : novas)
            try { d.run("DELETE FROM acervo_pasta WHERE id = ?", {matriz::db::Value::of(id)}); } catch (...) {}
    }
}

void ProjetoAberto::executarPartesPersistentes(int64_t passo, int ordemDe, int ordemAte) {
    if (!projeto_) return;
    auto& r = projeto_->registro();
    auto& ind = projeto_->indice();
    const juce::File pasta = projeto_->pasta();
    std::unique_lock<std::recursive_mutex> lock(writeMutex());
    struct Parte {
        long long id = 0;
        std::string tipo, banco, tabela, onde, paramsJson, colunasJson, arquivo, copia, itensJson, tipoEvento;
        long long linhas = 0;
    };
    std::vector<Parte> partes;
    {
        auto st = r.prepare("SELECT id, tipo, banco, COALESCE(tabela, ''), COALESCE(onde, ''), COALESCE(params_json, '[]'), "
                            "COALESCE(colunas_json, '[]'), COALESCE(arquivo, ''), COALESCE(copia, ''), linhas, COALESCE(itens_json, '[]'), "
                            "COALESCE(tipo_evento, '') FROM undo_parte WHERE passo_id = ? AND ordem BETWEEN ? AND ? "
                            "ORDER BY ordem / " + std::to_string(kPartesPorAcao) + " DESC, ordem ASC");
        st.bind(1, matriz::db::Value::of(static_cast<long long>(passo)));
        st.bind(2, matriz::db::Value::of(ordemDe));
        st.bind(3, matriz::db::Value::of(ordemAte));
        while (st.step())
            partes.push_back({st.columnInt(0), st.columnText(1), st.columnText(2), st.columnText(3), st.columnText(4), st.columnText(5),
                              st.columnText(6), st.columnText(7), st.columnText(8), st.columnText(10), st.columnText(11), st.columnInt(9)});
    }
    auto linhasDa = [&](long long parte) {
        std::vector<std::string> out;
        auto st = r.prepare("SELECT dados_json FROM undo_linha WHERE parte_id = ?");
        st.bind(1, matriz::db::Value::of(parte));
        while (st.step()) out.push_back(st.columnText(0));
        return out;
    };
    auto restaurar = [&](matriz::db::Database& d, const Parte& p) {
        const auto params = paramsDoJson(p.paramsJson);
        const std::string t = "\"" + p.tabela + "\"";
        if (p.tipo == "colunas") {
            const std::string chave = p.copia.rfind("chave:", 0) == 0 ? p.copia.substr(6) : std::string();
            if (p.linhas == 0) {
                if (chave.empty()) d.run("DELETE FROM " + t + " WHERE " + p.onde, params);  // a linha não existia
                return;
            }
            std::string sets;
            const auto cols = juce::JSON::parse(juce::String::fromUTF8(p.colunasJson.c_str()));
            if (auto* a = cols.getArray())
                for (const auto& c : *a) {
                    const std::string nome = c.toString().toStdString();
                    if (nome == chave) continue;
                    sets += (sets.empty() ? "\"" : ", \"") + nome + "\" = " + valorDoJson(nome);
                }
            if (sets.empty()) return;
            for (const auto& j : linhasDa(p.id)) {
                if (chave.empty()) d.run("UPDATE " + t + " SET " + sets + " WHERE " + p.onde, comJsonNaFrente(j, params));
                else d.run("UPDATE " + t + " SET " + sets + " WHERE \"" + chave + "\" = " + valorDoJson(chave), {matriz::db::Value::of(j)});
            }
            return;
        }
        // linhas: o que bate hoje sai, as guardadas voltam
        d.run("DELETE FROM " + t + " WHERE " + p.onde, params);
        std::vector<std::string> cols;
        {
            auto st = d.prepare("SELECT name FROM pragma_table_info(?)");
            st.bind(1, matriz::db::Value::of(p.tabela));
            while (st.step()) cols.push_back(st.columnText(0));
        }
        std::string nomes, valores;
        for (const auto& c : cols) {
            nomes += (nomes.empty() ? "\"" : ", \"") + c + "\"";
            valores += (valores.empty() ? "" : ", ") + valorDoJson(c);
        }
        for (const auto& j : linhasDa(p.id))
            d.run("INSERT INTO " + t + " (" + nomes + ") VALUES (" + valores + ")", {matriz::db::Value::of(j)});
    };
    try {
        r.run("BEGIN TRANSACTION", {});
        for (const auto& p : partes)
            if ((p.tipo == "linhas" || p.tipo == "colunas") && p.banco == "registro") restaurar(r, p);
        r.run("COMMIT", {});
    } catch (...) {
        try { r.run("ROLLBACK", {}); } catch (...) {}
        return;
    }
    for (const auto& p : partes)
        if ((p.tipo == "linhas" || p.tipo == "colunas") && p.banco == "indice") {
            try { restaurar(ind, p); } catch (...) {}
        }
    for (const auto& p : partes)
        if (p.tipo == "arquivo" && !p.arquivo.empty()) {
            const juce::File alvo = pasta.getChildFile(juce::String::fromUTF8(p.arquivo.c_str()));
            if (!p.copia.empty()) pasta.getChildFile(juce::String::fromUTF8(p.copia.c_str())).copyFileTo(alvo);
            else alvo.deleteFile();
        }
    for (const auto& p : partes)
        if (p.tipo == "acao") {
            try { executarAcaoDeUndo(p.tabela, p.onde); } catch (...) {}
        }
    lock.unlock();
    auto& bus = EventBus::obterInstancia();
    bus.iniciarLote();
    for (const auto& p : partes) {
        const auto itens = juce::JSON::parse(juce::String::fromUTF8(p.itensJson.c_str()));
        if (auto* a = itens.getArray())
            for (const auto& id : *a) bus.dispararItemAlterado(id.toString().toStdString(), p.tipoEvento);
    }
    bus.finalizarLote();
}

void ProjetoAberto::apagarPassoPersistente(int64_t passo) {
    if (!projeto_ || passo == 0 || somenteLeitura_) return;
    auto& r = projeto_->registro();
    const juce::File pasta = projeto_->pasta();
    std::unique_lock<std::recursive_mutex> lock(writeMutex());
    try {
        auto st = r.prepare("SELECT copia FROM undo_parte WHERE passo_id = ? AND tipo = 'arquivo' AND copia IS NOT NULL");
        st.bind(1, matriz::db::Value::of(static_cast<long long>(passo)));
        while (st.step()) pasta.getChildFile(juce::String::fromUTF8(st.columnText(0).c_str())).deleteFile();
        r.run("DELETE FROM undo_linha WHERE parte_id IN (SELECT id FROM undo_parte WHERE passo_id = ?)", {matriz::db::Value::of(static_cast<long long>(passo))});
        r.run("DELETE FROM undo_parte WHERE passo_id = ?", {matriz::db::Value::of(static_cast<long long>(passo))});
        r.run("DELETE FROM undo_passo WHERE id = ?", {matriz::db::Value::of(static_cast<long long>(passo))});
    } catch (...) {}
}

void ProjetoAberto::carregarUndoPersistente() {
    if (!projeto_ || somenteLeitura_) return;
    auto& r = projeto_->registro();
    try {
        std::vector<long long> incompletos;
        {
            auto st = r.prepare("SELECT id FROM undo_passo WHERE completo = 0");
            while (st.step()) incompletos.push_back(st.columnInt(0));
        }
        for (auto id : incompletos) apagarPassoPersistente(id);  // grupo com parte só na memória, ou captura pela metade
        std::vector<std::pair<long long, std::string>> passos;
        {
            auto st = r.prepare("SELECT id, descricao FROM undo_passo ORDER BY id");
            while (st.step()) passos.push_back({st.columnInt(0), st.columnText(1)});
        }
        while (static_cast<int>(passos.size()) > kMaxUndo) {
            apagarPassoPersistente(passos.front().first);
            passos.erase(passos.begin());
        }
        for (const auto& [id, desc] : passos) {
            const int64_t passo = id;
            pilhaUndo_.push_back(UndoEntry{desc, {[this, passo] { executarPartesPersistentes(passo, 0, std::numeric_limits<int>::max()); }}, passo, true});
        }
    } catch (...) {}
}

ProjetoAberto::RetratoGeo ProjetoAberto::retratoGeo(const std::vector<std::string>& itemIds) const {
    RetratoGeo out;
    if (!projeto_) return out;
    out.reserve(itemIds.size());
    for (const auto& id : itemIds)
        out.emplace_back(id, matriz::analytics::AssetGeolocationRepository::obterPorAssetId(projeto_->registro(), id));
    return out;
}

void ProjetoAberto::registrarUndoGeo(const std::string& descricao, const std::vector<std::string>& itemIds) {
    if (itemIds.empty()) return;
    // Persistente: as linhas de asset_geolocation desses itens (a que não existia some na volta). De outra thread
    // (Intake em lote), vira passo próprio na message thread.
    ParteUndo p;
    p.tabela = "asset_geolocation";
    p.onde = "asset_id IN (SELECT value FROM json_each(?))";
    p.params = {matriz::db::Value::of(idsEmJson(itemIds))};
    registrarUndoPersistente(descricao, {p}, itemIds, "metadado");
}

int ProjetoAberto::completarEnderecosGeo(const std::vector<matriz::analytics::AssetGeolocation>& achados) {
    if (!projeto_ || somenteLeitura_ || achados.empty()) return 0;
    std::vector<std::string> ids;
    for (const auto& a : achados) ids.push_back(a.assetId);
    auto& db = projeto_->registro();
    std::unique_lock<std::recursive_mutex> lock(writeMutex());
    auto antes = retratoGeo(ids);
    std::vector<matriz::analytics::AssetGeolocation> novos;  // só os que mudam
    for (size_t i = 0; i < achados.size(); ++i) {
        if (!antes[i].second) continue;  // sem linha = sem coordenadas: nada a completar
        auto geo = *antes[i].second;
        bool mudou = false;
        auto completar = [&mudou](std::optional<std::string>& campo, const std::optional<std::string>& achado) {
            if ((!campo || campo->empty()) && achado && !achado->empty()) { campo = achado; mudou = true; }
        };
        completar(geo.formattedAddress, achados[i].formattedAddress);
        completar(geo.city, achados[i].city);
        completar(geo.stateProvince, achados[i].stateProvince);
        completar(geo.country, achados[i].country);
        if (mudou) novos.push_back(std::move(geo));
    }
    const int n = static_cast<int>(novos.size());
    if (n == 0) return 0;
    std::vector<std::string> mudados;
    for (const auto& g : novos) mudados.push_back(g.assetId);
    registrarUndoGeo(n == 1 ? "GET address" : "GET addresses (" + std::to_string(n) + " items)", mudados);  // antes de gravar
    bool emTransacao = false;
    try { db.exec("BEGIN IMMEDIATE"); emTransacao = true; } catch (...) {}
    try {
        for (const auto& g : novos) matriz::analytics::AssetGeolocationRepository::salvar(db, g);
        if (emTransacao) db.exec("COMMIT");
    } catch (...) {
        if (emTransacao) { try { db.exec("ROLLBACK"); } catch (...) {} }
        return 0;
    }
    return n;
}

void ProjetoAberto::iniciarGrupoUndo(const std::string& descricao) {
    if (desfazendo_) return;
    if (grupoAberto_) finalizarGrupoUndo();
    grupoAberto_ = UndoEntry{descricao, {}};
}

void ProjetoAberto::finalizarGrupoUndo() {
    if (!grupoAberto_ || grupoAberto_->acoesReversas.empty()) {
        if (grupoAberto_) apagarPassoPersistente(grupoAberto_->passoPersistente);
        grupoAberto_.reset();
        return;
    }
    if (grupoAberto_->passoPersistente && grupoAberto_->tudoPersistente && projeto_)
        try {
            projeto_->registro().run("UPDATE undo_passo SET completo = 1 WHERE id = ?",
                                     {matriz::db::Value::of(static_cast<long long>(grupoAberto_->passoPersistente))});
        } catch (...) {}
    pilhaUndo_.push_back(std::move(*grupoAberto_));
    grupoAberto_.reset();
    aparaPilhaUndo();
    if (aoMudarUndo) aoMudarUndo();
}

void ProjetoAberto::registrarUndoVisualMapa(const std::string& descricao, const std::string& mapaId) {
    if (desfazendo_ || somenteLeitura_ || !projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return;
    const std::string tab = "undo_vi_" + std::to_string(++contadorSnapUndo_);
    const double fator = fatorCardsDoMapa(mapaId);
    try {
        projeto_->registro().run("CREATE TEMP TABLE " + tab + " AS SELECT id, posicao_x, posicao_y, escala_no, ativo, cor_customizada "
                                 "FROM acervo_pasta WHERE mapa_id = ?", {matriz::db::Value::of(mapaId)});
    } catch (...) { return; }
    registrarUndo(descricao, [this, tab, mapaId, fator]() {
        auto& d = projeto_->registro();
        try {
            d.run("BEGIN TRANSACTION", {});
            for (const char* col : {"posicao_x", "posicao_y", "escala_no", "ativo", "cor_customizada"})
                d.run(std::string("UPDATE acervo_pasta SET ") + col + " = (SELECT u." + col + " FROM " + tab +
                      " u WHERE u.id = acervo_pasta.id) WHERE id IN (SELECT id FROM " + tab + ")", {});
            d.run("COMMIT", {});
            if (fator > 0.0) definirFatorCardsDoMapa(mapaId, fator);
            d.run("DROP TABLE IF EXISTS " + tab, {});
        } catch (...) {
            try { d.run("ROLLBACK", {}); } catch (...) {}
        }
    });
}

void ProjetoAberto::registrarUndoSnapshotMapa(const std::string& descricao, const std::string& mapaId, bool incluirLinhaMapa) {
    if (desfazendo_ || somenteLeitura_ || !projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return;
    const std::string n = std::to_string(++contadorSnapUndo_);
    const std::string tp = "undo_pa_" + n, tl = "undo_li_" + n, tm = "undo_fm_" + n;
    auto& db = projeto_->registro();
    try {
        db.run("CREATE TEMP TABLE " + tp + " AS SELECT * FROM acervo_pasta WHERE mapa_id = ?", {matriz::db::Value::of(mapaId)});
        db.run("CREATE TEMP TABLE " + tl + " AS SELECT * FROM acervo_item_pasta WHERE mapa_id = ?", {matriz::db::Value::of(mapaId)});
        if (incluirLinhaMapa)
            db.run("CREATE TEMP TABLE " + tm + " AS SELECT * FROM folder_map WHERE id = ?", {matriz::db::Value::of(mapaId)});
    } catch (...) { return; }
    registrarUndo(descricao, [this, tp, tl, tm, mapaId, incluirLinhaMapa]() {
        auto& d = projeto_->registro();
        try {
            d.run("BEGIN TRANSACTION", {});
            if (incluirLinhaMapa)
                d.run("INSERT OR REPLACE INTO folder_map SELECT * FROM " + tm, {});
            d.run("DELETE FROM acervo_item_pasta WHERE mapa_id = ?", {matriz::db::Value::of(mapaId)});
            d.run("DELETE FROM acervo_pasta WHERE mapa_id = ?", {matriz::db::Value::of(mapaId)});
            d.run("INSERT INTO acervo_pasta SELECT * FROM " + tp, {});
            d.run("INSERT OR IGNORE INTO acervo_item_pasta SELECT * FROM " + tl + " WHERE item_id IN (SELECT id FROM item)", {});
            d.run("COMMIT", {});
            for (const auto& t : {tp, tl, tm}) d.run("DROP TABLE IF EXISTS " + t, {});
        } catch (...) {
            try { d.run("ROLLBACK", {}); } catch (...) {}
        }
    });
}

void ProjetoAberto::registrarUndoSubarvoreApagada(const std::string& pastaId) {
    if (desfazendo_ || !projeto_ || pastaId.empty()) return;
    const std::string n = std::to_string(++contadorSnapUndo_);
    const std::string tp = "undo_pa_" + n, tl = "undo_li_" + n;
    auto& db = projeto_->registro();
    try {
        db.run("CREATE TEMP TABLE " + tp + " AS WITH RECURSIVE t(id) AS (SELECT id FROM acervo_pasta WHERE id = ? "
               "UNION ALL SELECT c.id FROM acervo_pasta c JOIN t ON c.pasta_pai_id = t.id) "
               "SELECT * FROM acervo_pasta WHERE id IN (SELECT id FROM t)", {matriz::db::Value::of(pastaId)});
        db.run("CREATE TEMP TABLE " + tl + " AS SELECT * FROM acervo_item_pasta WHERE pasta_id IN (SELECT id FROM " + tp + ")", {});
    } catch (...) { return; }
    registrarUndo("Delete Folder", [this, tp, tl]() {
        auto& d = projeto_->registro();
        try {
            d.run("BEGIN TRANSACTION", {});
            d.run("INSERT OR IGNORE INTO acervo_pasta SELECT * FROM " + tp, {});
            d.run("INSERT OR IGNORE INTO acervo_item_pasta SELECT * FROM " + tl + " WHERE item_id IN (SELECT id FROM item)", {});
            d.run("COMMIT", {});
            d.run("DROP TABLE IF EXISTS " + tp, {});
            d.run("DROP TABLE IF EXISTS " + tl, {});
        } catch (...) {
            try { d.run("ROLLBACK", {}); } catch (...) {}
        }
    });
}

bool ProjetoAberto::desfazer() {
    if (pilhaUndo_.empty()) return false;
    auto entry = std::move(pilhaUndo_.back());
    pilhaUndo_.pop_back();
    desfazendo_ = true;
    for (auto it = entry.acoesReversas.rbegin(); it != entry.acoesReversas.rend(); ++it) {
        if (*it) (*it)();
    }
    desfazendo_ = false;
    apagarPassoPersistente(entry.passoPersistente);  // desfeito: não volta ao reabrir
    if (aoMudarUndo) aoMudarUndo();
    return true;
}

std::string ProjetoAberto::descricaoUndoAtual() const {
    if (pilhaUndo_.empty()) return {};
    return pilhaUndo_.back().descricao;
}

void ProjetoAberto::salvarMetadado(const std::string& itemId, const std::string& coluna, const std::string& valorDigitado) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    // Nomes sem distinção de caixa e acento: "show" digitado vira o "Show" que o projeto já usa; "Véritas" no LIST vira o
    // "Veritas" já gravado.
    const std::string valor = coluna == "dc_subject"
                                  ? matriz::model::nomes::subjectsCanonicos(projeto_->registro(), valorDigitado)
                                  : matriz::model::nomes::valorCanonicoDaColuna(projeto_->registro(), coluna, valorDigitado);

    if (!desfazendo_) {
        auto old = lerMetadado(itemId, coluna);
        std::string oldVal = old.value_or("");
        registrarUndo("Edit " + coluna, [this, itemId, coluna, oldVal]() {
            salvarMetadado(itemId, coluna, oldVal);
        });
    }

    // Try updating column on item table
    try {
        projeto_->registro().run(
            "UPDATE item SET " + coluna + " = ?, atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
            {matriz::db::Value::of(valor),
             matriz::db::Value::of(matriz::model::agoraIso8601()),
             matriz::db::Value::of(itemId)});
    } catch (...) {
        try {
            projeto_->registro().run(
                "UPDATE item SET atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                {matriz::db::Value::of(matriz::model::agoraIso8601()),
                 matriz::db::Value::of(itemId)});
        } catch (...) {}
    }

    // Sync to item_campo
    std::string agora = matriz::model::agoraIso8601();
    try {
        projeto_->registro().run(
            "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
            "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
            {matriz::db::Value::of(matriz::model::novoUuid()),
             matriz::db::Value::of(itemId),
             matriz::db::Value::of(coluna),
             matriz::db::Value::of(valor),
             matriz::db::Value::of(agora)});
    } catch (...) {}

    // A coluna "ano" (EVENT DATE na ficha) alimenta os filtros de data da aba
    // METADATA; ela ganha um tipo próprio de evento para que a lista possa
    // se reavaliar na hora, em vez de só na próxima vez que a aba recarrega.
    EventBus::obterInstancia().dispararItemAlterado(itemId, coluna == "ano" ? "metadado_data" : "metadado");
}

void ProjetoAberto::salvarMetadadoEmLote(const std::vector<std::string>& itemIds,
                                          const std::vector<std::pair<std::string, std::string>>& camposEValoresDigitados,
                                          const std::set<std::pair<std::string, std::string>>& pular) {
    MATRIZ_TRACE("ProjetoAberto::salvarMetadadoEmLote");
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty() || camposEValoresDigitados.empty()) return;
    // Nomes sem distinção de caixa e acento: SUBJECT, LIST, CREATOR... entram na grafia que o projeto já usa.
    auto camposEValores = camposEValoresDigitados;
    for (auto& [coluna, valor] : camposEValores) {
        if (coluna == "dc_subject") valor = matriz::model::nomes::subjectsCanonicos(projeto_->registro(), valor);
        else valor = matriz::model::nomes::valorCanonicoDaColuna(projeto_->registro(), coluna, valor);
    }

    // Uma entrada de Undo só pro lote inteiro (não uma por item x campo):
    // captura o valor antigo de cada combinação antes de escrever. Desfazer
    // restaura item por item via salvarMetadado() normal — ação rara e
    // pequena o bastante pra não precisar ser transacional.
    if (!desfazendo_) {
        struct ValorAntigo { std::string itemId, coluna, valor; };
        auto anteriores = std::make_shared<std::vector<ValorAntigo>>();
        anteriores->reserve(itemIds.size() * camposEValores.size());
        for (const auto& itemId : itemIds) {
            for (const auto& campoValor : camposEValores) {
                if (pular.count({itemId, campoValor.first})) continue;
                anteriores->push_back({itemId, campoValor.first,
                                        lerMetadado(itemId, campoValor.first).value_or("")});
            }
        }
        auto desfazerLote = [this, anteriores]() {
            for (const auto& v : *anteriores) {
                salvarMetadado(v.itemId, v.coluna, v.valor);
            }
        };
        std::string descricao = "Edit " + camposEValores.front().first + " (lote)";
        // Chamado de background pelo Intake: a pilha de Undo e aoMudarUndo
        // são da message thread — registra lá. vivo_ expira se o projeto
        // fechar antes do callAsync rodar.
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
            registrarUndo(descricao, std::move(desfazerLote));
        } else {
            std::weak_ptr<bool> vivo = vivo_;
            juce::MessageManager::callAsync([this, vivo, descricao, desfazerLote]() mutable {
                if (vivo.lock()) registrarUndoComoPassoProprio(descricao, std::move(desfazerLote));
            });
        }
    }

    auto& db = projeto_->registro();
    std::string agora = matriz::model::agoraIso8601();

    std::unique_lock<std::recursive_mutex> writeLock(projeto_->writeMutex());
    try {
        db.exec("BEGIN IMMEDIATE");
        for (const auto& itemId : itemIds) {
            for (const auto& campoValor : camposEValores) {
                const std::string& coluna = campoValor.first;
                if (pular.count({itemId, coluna})) continue;
                // O lote troca o meio; o DEVICE (aparelho que gerou o arquivo) continua o de cada item.
                const std::string valor = coluna == "source_media"
                    ? matriz::ui::manterDeviceDoItem(campoValor.second, matriz::ui::deviceDoItem(db, itemId))
                    : campoValor.second;
                try {
                    db.run(
                        "UPDATE item SET " + coluna + " = ?, atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                        {matriz::db::Value::of(valor), matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
                } catch (...) {
                    try {
                        db.run("UPDATE item SET atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                               {matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
                    } catch (...) {}
                }
                try {
                    db.run(
                        "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                        "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
                        "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
                        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
                         matriz::db::Value::of(coluna), matriz::db::Value::of(valor), matriz::db::Value::of(agora)});
                } catch (...) {}
            }
        }
        db.exec("COMMIT");
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        return;
    }

    // EventBus/juce::ListenerList não é thread-safe pra iterar/chamar de uma
    // thread que não seja a message thread -- este método pode ser chamado
    // de background (ver IntakeWorkspaceComponent::aplicarXAosSelecionados),
    // então o disparo do evento amplo sempre volta pra message thread via
    // callAsync, nunca direto.
    // Evento de LOTE com os ids (a grade atualiza só esses itens, num refiltro só); o amplo ("" = recarregar
    // o catálogo inteiro) fica só para lotes enormes.
    bool ehData = camposEValores.front().first == "ano";
    juce::MessageManager::callAsync([ehData, itemIds] {
        const std::string tipo = ehData ? "metadado_data" : "metadado";
        if (itemIds.size() <= 2000) EventBus::obterInstancia().dispararItensAlterados(itemIds, tipo);
        else EventBus::obterInstancia().dispararItemAlterado("", tipo);
    });
}

void ProjetoAberto::definirOrigemPhoneDump(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_ || !projeto_ || itemIds.empty()) return;
    auto& db = projeto_->registro();
    {   // Undo: o Original Source Medium de cada item (campo da ficha e coluna do item) como estava, num passo.
        const std::string json = idsEmJson(itemIds);
        ParteUndo campo;
        campo.tabela = "item_campo";
        campo.onde = "item_id IN (SELECT value FROM json_each(?)) AND nivel = 'raiz' AND nivel_indice = 0 AND campo_id = 'source_media'";
        campo.params = {matriz::db::Value::of(json)};
        ParteUndo coluna;
        coluna.tipo = ParteUndo::Tipo::Colunas;
        coluna.tabela = "item";
        coluna.onde = "id IN (SELECT value FROM json_each(?))";
        coluna.params = {matriz::db::Value::of(json)};
        coluna.colunas = {"source_media"};
        coluna.chave = "id";
        registrarUndoPersistente("Mark as Phone Dump", {campo, coluna}, itemIds, "metadado");
    }
    std::unique_lock<std::recursive_mutex> writeLock(projeto_->writeMutex());
    try {
        db.exec("BEGIN IMMEDIATE");
        matriz::ui::gravarOrigemPhoneDump(db, itemIds);
        db.exec("COMMIT");
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
    }
}

bool ProjetoAberto::preencherAnoPadraoSeVazio(const std::string& itemId, const std::string& ano) {
    if (!projeto_ || itemId.empty() || ano.size() != 4) return false;
    for (char c : ano) if (c < '0' || c > '9') return false;

    try {
        // Mesmo UPDATE condicional que IngestArquivo usa: quem já tem ano
        // (inclusive um digitado pelo usuário) nunca é sobrescrito. Nada de
        // metadados_editados, atualizado_em, Undo ou EventBus aqui — abrir
        // uma ficha não pode marcar o item como editado nem empilhar Undo.
        auto& db = projeto_->registro();
        auto antes = db.prepare("SELECT COUNT(*) FROM item WHERE id = ? AND (ano IS NULL OR ano = '')");
        antes.bind(1, matriz::db::Value::of(itemId));
        bool estavaVazio = antes.step() && antes.columnInt(0) > 0;
        if (!estavaVazio) return false;

        db.run("UPDATE item SET ano = ? WHERE id = ? AND (ano IS NULL OR ano = '')",
               {matriz::db::Value::of(ano), matriz::db::Value::of(itemId)});
        return true;
    } catch (...) {
        return false;
    }
}

void ProjetoAberto::redefinirMetadadosItens(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    auto& db = projeto_->registro();
    std::string agora = matriz::model::agoraIso8601();

    // Undo: a ficha inteira como estava (colunas do item e TODOS os campos — a volta regrava o conjunto) e as tags.
    auto retrato = retratar({{"item", "id", true}, {"item_campo", "item_id", false}}, itemIds);
    std::vector<std::pair<std::string, std::vector<std::string>>> tagsAntes;
    for (const auto& id : itemIds) tagsAntes.emplace_back(id, lerTags(id));

    for (const auto& itemId : itemIds) {
        if (itemId.empty()) continue;

        try {
            // 1. Clear non-native custom field entries (keep only reading/ingest technical fields)
            db.run("DELETE FROM item_campo WHERE item_id = ? AND fonte != 'leitura_tecnica'",
                   {matriz::db::Value::of(itemId)});

            // 2. Clear user tags
            db.run("DELETE FROM item_tag WHERE item_id = ?",
                   {matriz::db::Value::of(itemId)});

            // 3. Clear search entries for tags
            db.run("DELETE FROM busca_fts WHERE item_id = ? AND conteudo LIKE '#%'",
                   {matriz::db::Value::of(itemId)});

            // 4. Find if there are native readings for ano / date or source_media
            std::optional<std::string> nativeAno;
            std::optional<std::string> nativeSourceMedia;

            auto stmtAno = db.prepare(
                "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id IN ('dc_created', 'data_criacao', 'ano') AND fonte = 'leitura_tecnica' LIMIT 1");
            stmtAno.bind(1, matriz::db::Value::of(itemId));
            if (stmtAno.step() && !stmtAno.columnIsNull(0)) {
                juce::String dt(stmtAno.columnText(0));
                if (dt.length() >= 4) {
                    nativeAno = dt.substring(0, 4).toStdString();
                }
            }

            auto stmtSm = db.prepare(
                "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = 'source_media' AND fonte = 'leitura_tecnica' LIMIT 1");
            stmtSm.bind(1, matriz::db::Value::of(itemId));
            if (stmtSm.step() && !stmtSm.columnIsNull(0)) {
                nativeSourceMedia = stmtSm.columnText(0);
            }

            // 5. Reset item columns to original/null and mark metadados_editados = 0
            db.run("UPDATE item SET metadados_editados = 0, notas_livres = NULL, isrc = NULL, content_type = NULL, collection_type = NULL, ano = ?, source_media = ?, atualizado_em = ? WHERE id = ?",
                   {nativeAno ? matriz::db::Value::of(*nativeAno) : matriz::db::Value::null(),
                    nativeSourceMedia ? matriz::db::Value::of(*nativeSourceMedia) : matriz::db::Value::null(),
                    matriz::db::Value::of(agora),
                    matriz::db::Value::of(itemId)});

            EventBus::obterInstancia().dispararItemAlterado(itemId, "metadado");
        } catch (...) {}
    }
    // As tags voltam por definirTags (que cuida de item_tag e da busca): uma ação no fim do passo.
    juce::DynamicObject::Ptr tags = new juce::DynamicObject();
    for (const auto& [id, lista] : tagsAntes) {
        if (lista.empty()) continue;
        juce::Array<juce::var> a;
        for (const auto& t : lista) a.add(juce::String::fromUTF8(t.c_str()));
        tags->setProperty(juce::Identifier(juce::String::fromUTF8(id.c_str())), a);
    }
    ParteUndo acaoTags;
    acaoTags.tipo = ParteUndo::Tipo::Acao;
    acaoTags.tabela = "tags";
    acaoTags.onde = juce::JSON::toString(juce::var(tags.get()), true).toStdString();
    registrarUndoDeRetrato(itemIds.size() == 1 ? "Clear Metadata" : "Clear Metadata (" + std::to_string(itemIds.size()) + " items)",
                           std::move(retrato), itemIds, {acaoTags});
}

std::vector<std::string> ProjetoAberto::lerTags(const std::string& itemId) const {
    std::vector<std::string> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT tag FROM item_tag WHERE item_id = ? ORDER BY tag COLLATE NOCASE");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

void ProjetoAberto::definirTags(const std::string& itemId, const std::vector<std::string>& tagsDigitadas) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    if (!desfazendo_) {
        auto oldTags = lerTags(itemId);
        registrarUndo("Set Tags", [this, itemId, oldTags]() {
            definirTags(itemId, oldTags);
        });
    }
    auto& db = projeto_->registro();
    // Nomes case-insensitive: cada tag na grafia que o projeto já usa, sem
    // repetir ("Show" e "show" no mesmo item viram uma só).
    std::vector<std::string> tags;
    {
        std::set<std::string> chaves;
        for (const auto& t : tagsDigitadas) {
            std::string canon = matriz::model::nomes::tagCanonica(db, t);
            if (!canon.empty() && chaves.insert(matriz::model::nomes::chave(canon)).second) tags.push_back(canon);
        }
    }
    db.run("BEGIN TRANSACTION", {});
    try {
        db.run("DELETE FROM item_tag WHERE item_id = ?", {matriz::db::Value::of(itemId)});
        for (const auto& tag : tags) {
            if (tag.empty()) continue;
            db.run(
                "INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                {matriz::db::Value::of(matriz::model::novoUuid()),
                 matriz::db::Value::of(itemId),
                 matriz::db::Value::of(tag)});
        }
        // Index tags in FTS (both with and without '#') — erro aqui não
        // derruba a transação inteira, só a indexação de busca fica velha.
        try {
            db.run("DELETE FROM busca_fts WHERE item_id = ? AND (conteudo LIKE '#%' OR conteudo IN (SELECT tag FROM item_tag WHERE item_id = ?))",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(itemId)});
            for (const auto& tag : tags) {
                if (tag.empty()) continue;
                db.run(
                    "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
                    {matriz::db::Value::of(itemId), matriz::db::Value::of(tag)});
                db.run(
                    "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
                    {matriz::db::Value::of(itemId), matriz::db::Value::of("#" + tag)});
            }
        } catch (...) {}
        try {
            db.run(
                "UPDATE item SET metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                {matriz::db::Value::of(matriz::model::agoraIso8601()),
                 matriz::db::Value::of(itemId)});
        } catch (...) {}
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
    EventBus::obterInstancia().dispararItemAlterado(itemId, "tags");
}

void ProjetoAberto::adicionarTag(const std::string& itemId, const std::string& tag) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || tag.empty()) return;
    juce::String clean = juce::String(tag).trimCharactersAtStart("#").trim();
    if (clean.isEmpty()) return;
    // Nomes case-insensitive: grafia que o projeto já usa.
    std::string cleanStr = matriz::model::nomes::tagCanonica(projeto_->registro(), clean.toStdString());
    bool jaTinha = false;
    {
        auto st = projeto_->registro().prepare("SELECT 1 FROM item_tag WHERE item_id = ? AND tag = ?");
        st.bind(1, matriz::db::Value::of(itemId));
        st.bind(2, matriz::db::Value::of(cleanStr));
        jaTinha = st.step();
    }
    if (jaTinha) return;  // "show" num item que já tem "Show": nada muda (e o Undo não apagaria a dele)
    if (!desfazendo_) {
        registrarUndo("Add Tag", [this, itemId, cleanStr]() {
            removerTag(itemId, cleanStr);
        });
    }
    projeto_->registro().run(
        "INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()),
         matriz::db::Value::of(itemId),
         matriz::db::Value::of(cleanStr)});
    try {
        projeto_->registro().run(
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
            {matriz::db::Value::of(itemId), matriz::db::Value::of(cleanStr)});
        projeto_->registro().run(
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
            {matriz::db::Value::of(itemId), matriz::db::Value::of("#" + cleanStr)});
    } catch (...) {}
    try {
        projeto_->registro().run(
            "UPDATE item SET metadados_editados = 1, atualizado_em = ? WHERE id = ?",
            {matriz::db::Value::of(matriz::model::agoraIso8601()),
             matriz::db::Value::of(itemId)});
    } catch (...) {}
    EventBus::obterInstancia().dispararItemAlterado(itemId, "tags");
}

void ProjetoAberto::removerTag(const std::string& itemId, const std::string& tag) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || tag.empty()) return;
    juce::String clean = juce::String(tag).trimCharactersAtStart("#").trim();
    std::string cleanStr = clean.toStdString();
    if (!desfazendo_) {
        registrarUndo("Remove Tag", [this, itemId, cleanStr]() {
            adicionarTag(itemId, cleanStr);
        });
    }
    // Nomes case-insensitive: "SHOW" remove o "Show" do item.
    {
        const std::string k = matriz::model::nomes::chave(cleanStr);
        std::vector<std::string> doItem;
        auto st = projeto_->registro().prepare("SELECT tag FROM item_tag WHERE item_id = ?");
        st.bind(1, matriz::db::Value::of(itemId));
        while (st.step()) doItem.push_back(st.columnText(0));
        for (const auto& t : doItem)
            if (t != tag && t != cleanStr && matriz::model::nomes::chave(t) == k)
                projeto_->registro().run("DELETE FROM item_tag WHERE item_id = ? AND tag = ?",
                                          {matriz::db::Value::of(itemId), matriz::db::Value::of(t)});
    }
    projeto_->registro().run("DELETE FROM item_tag WHERE item_id = ? AND (tag = ? OR tag = ?)",
                              {matriz::db::Value::of(itemId), matriz::db::Value::of(tag), matriz::db::Value::of(cleanStr)});
    try {
        projeto_->registro().run("DELETE FROM busca_fts WHERE item_id = ? AND (conteudo = ? OR conteudo = ? OR conteudo = ?)",
                                  {matriz::db::Value::of(itemId), matriz::db::Value::of(tag), matriz::db::Value::of(cleanStr), matriz::db::Value::of("#" + cleanStr)});
    } catch (...) {}
    EventBus::obterInstancia().dispararItemAlterado(itemId, "tags");
}

std::vector<std::string> ProjetoAberto::listarPessoas() const {
    std::vector<std::string> pessoas;
    if (!projeto_) return pessoas;
    try {
        projeto_->registro().execScript(
            "CREATE TABLE IF NOT EXISTS collection_person ("
            "  id TEXT PRIMARY KEY,"
            "  nome TEXT NOT NULL UNIQUE,"
            "  criado_em TEXT NOT NULL"
            ");"
        );
        auto stmt = projeto_->registro().prepare("SELECT nome FROM collection_person ORDER BY nome COLLATE NOCASE ASC");
        while (stmt.step()) {
            pessoas.push_back(stmt.columnText(0));
        }
    } catch (...) {}
    return pessoas;
}

bool ProjetoAberto::adicionarPessoa(const std::string& nome) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || nome.empty()) return false;
    juce::String clean = juce::String(nome).trim();
    if (clean.isEmpty()) return false;
    try {
        projeto_->registro().execScript(
            "CREATE TABLE IF NOT EXISTS collection_person ("
            "  id TEXT PRIMARY KEY,"
            "  nome TEXT NOT NULL UNIQUE,"
            "  criado_em TEXT NOT NULL"
            ");"
        );
        auto stmt = projeto_->registro().prepare(
            "INSERT OR IGNORE INTO collection_person (id, nome, criado_em) VALUES (?, ?, ?)"
        );
        stmt.bind(1, matriz::db::Value::of(matriz::model::novoUuid()));
        // Nomes case-insensitive: "joão" não cria uma 2ª entrada ao lado de "João".
        stmt.bind(2, matriz::db::Value::of(matriz::model::nomes::tagCanonica(projeto_->registro(), clean.toStdString())));
        stmt.bind(3, matriz::db::Value::of(matriz::model::agoraIso8601()));
        stmt.step();
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjetoAberto::removerPessoa(const std::string& nome) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || nome.empty()) return false;
    try {
        // Só sai da lista de PESSOAS do projeto (os itens não mudam); o Undo devolve a linha como estava.
        registrarUndoDeRetrato("Remove Person", retratar({{"collection_person", "nome", false}}, {nome}), {});
        auto stmt = projeto_->registro().prepare("DELETE FROM collection_person WHERE nome = ?");
        stmt.bind(1, matriz::db::Value::of(nome));
        stmt.step();
        return true;
    } catch (...) {
        return false;
    }
}

const std::vector<std::string>& ProjetoAberto::ultimosItensIngeridos() const {
    if (ultimosItensIngeridosValido_ || !projeto_) return ultimosItensIngeridos_;
    ultimosItensIngeridos_.clear();
    try {
        auto stmt = leitura().prepare(
            "SELECT id FROM item WHERE lote_grid_id = "
            "(SELECT MAX(lote_grid_id) FROM item WHERE lote_grid_id IS NOT NULL) "
            "AND COALESCE(em_quarentena, 0) = 0 AND COALESCE(estado, '') != 'duplicata'");
        while (stmt.step()) ultimosItensIngeridos_.push_back(stmt.columnText(0));
        ultimosItensIngeridosValido_ = true;
    } catch (...) {}
    return ultimosItensIngeridos_;
}

// Undo de Reload/Replace File: a derivada (o "arquivo atual" de um Replace), o cache técnico e as miniaturas do item, e
// o tamanho do master, como estavam. O arquivo no disco não volta (um Reload relê o que está lá). Ordem: o cache dos
// arquivos de antes volta depois da derivada (apagar a derivada leva o cache dela junto).
static void registrarUndoDeArquivoAtual(ProjetoAberto& pa, const std::string& descricao, const std::string& itemId,
                                        const std::string& masterId) {
    auto& reg = pa.projeto().registro();
    std::vector<std::string> arquivos;
    {
        auto st = reg.prepare("SELECT id FROM arquivo WHERE item_id = ?");
        st.bind(1, matriz::db::Value::of(itemId));
        while (st.step()) arquivos.push_back(st.columnText(0));
    }
    ProjetoAberto::ParteUndo master;
    master.tipo = ProjetoAberto::ParteUndo::Tipo::Colunas;
    master.tabela = "arquivo";
    master.onde = "id = ?";
    master.params = {matriz::db::Value::of(masterId)};
    master.colunas = {"tamanho_bytes"};
    registrarUndoDeLinhas(pa, descricao,
                          {{"arquivo", "item_id = ? AND derivada_de_arquivo_id IS NOT NULL", {matriz::db::Value::of(itemId)}, false},
                           {"cache_arquivo", "arquivo_id IN (SELECT value FROM json_each(?))", {matriz::db::Value::of(idsEmJson(arquivos))}, false},
                           {"miniatura", "item_id = ?", {matriz::db::Value::of(itemId)}, true}},
                          {itemId}, "miniatura", {master});
}

bool ProjetoAberto::recarregarOuSubstituirArquivo(const std::string& itemId, const juce::File& novoCaminho,
                                                   juce::String& erro) {
    if (!projeto_) { erro = "Nenhum projeto aberto."; return false; }
    if (somenteLeitura_) { erro = matriz::i18n::t("clone_ro.bloqueado").toStdString(); return false; }
    if (!novoCaminho.existsAsFile()) {
        erro = "Arquivo nao encontrado: " + novoCaminho.getFullPathName();
        return false;
    }

    auto& db = projeto_->registro();

    std::string masterId, masterPapel, masterCaminhoRelativo;
    {
        auto stmt = db.prepare("SELECT id, papel, caminho_relativo FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (!stmt.step()) { erro = "Este item nao tem um arquivo master."; return false; }
        masterId = stmt.columnText(0);
        masterPapel = stmt.columnText(1);
        masterCaminhoRelativo = stmt.columnText(2);
    }
    // Reload do próprio master (o original, ou a cópia que o resolve): uma derivada com o mesmo caminho batia em
    // UNIQUE(item_id, caminho_relativo), o ROLLBACK desfazia tudo e nada recarregava. Relê no lugar: tamanho, cache e
    // miniatura do master (checksums e caminho continuam travados — arquivo_master_travado).
    // Mesmo critério do UNIQUE (o caminho relativo que gravarArquivoAnalisado gravaria), não comparação de caminho
    // absoluto (/var x /private/var, volume remontado...); ou o arquivo que o master resolve hoje (MAIN/CLONE).
    const bool noLugar = matriz::vault::caminhoRelativoAoVolume(novoCaminho) == masterCaminhoRelativo ||
                         matriz::vault::caminhoEsperadoArquivo(db, masterId, projeto_->pasta()) == novoCaminho;
    registrarUndoDeArquivoAtual(*this, noLugar ? "Reload File" : "Replace File", itemId, masterId);

    try {
        auto analise = matriz::ingest::analisarArquivo(novoCaminho);
        auto categoria = matriz::ingest::categoriaPorExtensao(novoCaminho);

        matriz::ingest::AnaliseCache cache;
        if (!analise.ehPlaceholderNuvem)
            cache = matriz::ingest::calcularCache(novoCaminho, categoria, projeto_->pasta(),
                                                   analise.leitura.duracaoSegundos);

        if (noLugar) {
            db.run("BEGIN TRANSACTION", {});
            try {
                // O original volta a ser o conteúdo do item: derivada de um Replace anterior sai.
                std::vector<std::string> derivadas;
                auto stmtDerivadas = db.prepare("SELECT id FROM arquivo WHERE item_id = ? AND derivada_de_arquivo_id IS NOT NULL");
                stmtDerivadas.bind(1, matriz::db::Value::of(itemId));
                while (stmtDerivadas.step()) derivadas.push_back(stmtDerivadas.columnText(0));
                for (auto& id : derivadas)
                    projeto_->indice().run("DELETE FROM miniatura WHERE arquivo_id = ?", {matriz::db::Value::of(id)});
                db.run("DELETE FROM arquivo WHERE item_id = ? AND derivada_de_arquivo_id IS NOT NULL", {matriz::db::Value::of(itemId)});
                db.run("UPDATE arquivo SET tamanho_bytes = ?, atualizado_em = ? WHERE id = ?",
                       {matriz::db::Value::of(static_cast<long long>(novoCaminho.getSize())),
                        matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(masterId)});
                matriz::ingest::gravarCache(db, masterId, cache);
                db.run("COMMIT", {});
            } catch (...) {
                db.run("ROLLBACK", {});
                throw;
            }
            // Miniatura refeita do zero (o arquivo da miniatura antiga sai antes, senão poderia ficar a velha).
            projeto_->indice().run("DELETE FROM miniatura WHERE arquivo_id = ?", {matriz::db::Value::of(masterId)});
            projeto_->pasta().getChildFile(".miniaturas").getChildFile(masterId + ".jpg").deleteFile();
            matriz::ingest::gerarEGravarMiniaturaPrincipal(projeto_->indice(), projeto_->pasta(), itemId, masterId, novoCaminho,
                                                             categoria, analise.leitura.duracaoSegundos);
            return true;
        }

        db.run("BEGIN TRANSACTION", {});
        std::string novoArquivoId;
        try {
            // Uma derivada por item (mesma regra que definirCapa já usa pra
            // capa): a anterior sai antes da nova entrar, senão cada
            // reload/replace empilha uma derivada nova sem limite. O master
            // (masterId) nunca é tocado aqui.
            std::vector<std::string> derivadasAntigas;
            auto stmtAntigas = db.prepare(
                "SELECT id FROM arquivo WHERE item_id = ? AND derivada_de_arquivo_id IS NOT NULL");
            stmtAntigas.bind(1, matriz::db::Value::of(itemId));
            while (stmtAntigas.step()) derivadasAntigas.push_back(stmtAntigas.columnText(0));
            for (auto& idAntigo : derivadasAntigas)
                projeto_->indice().run("DELETE FROM miniatura WHERE arquivo_id = ?",
                                        {matriz::db::Value::of(idAntigo)});
            db.run("DELETE FROM arquivo WHERE item_id = ? AND derivada_de_arquivo_id IS NOT NULL",
                   {matriz::db::Value::of(itemId)});

            auto resultado = matriz::ingest::gravarArquivoAnalisado(db, itemId, analise, masterPapel,
                                                                      /*ehMaster=*/false, masterId);
            novoArquivoId = resultado.arquivoId;
            matriz::ingest::gravarCache(db, novoArquivoId, cache);

            db.run("COMMIT", {});
        } catch (...) {
            db.run("ROLLBACK", {});
            throw;
        }

        if (!novoArquivoId.empty()) {
            // Fora da transação (miniatura mora no índice, banco separado) —
            // mesmo padrão de gerarEGravarMiniaturaPrincipal no ingest normal.
            matriz::ingest::gerarEGravarMiniaturaPrincipal(projeto_->indice(), projeto_->pasta(), itemId,
                                                             novoArquivoId, novoCaminho, categoria,
                                                             analise.leitura.duracaoSegundos);
        }

        return true;
    } catch (const std::exception& e) {
        erro = juce::String(e.what());
        return false;
    }
}

std::optional<juce::String> ProjetoAberto::caminhoMiniaturaPrincipal(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = leituraIndice().prepare(
        "SELECT caminho_relativo FROM miniatura WHERE item_id = ? AND tipo = 'miniatura' ORDER BY gerado_em DESC LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return std::nullopt;
    juce::String relativo = stmt.columnText(0);
    return projeto_->pasta().getChildFile(relativo).getFullPathName();
}

std::vector<ProjetoAberto::ImagemCatalogo> ProjetoAberto::buscarImagensDoCatalogo(const std::string& termo, int limite) const {
    std::vector<ImagemCatalogo> out;
    if (!projeto_) return out;
    try {
        const std::string padrao = "%" + termo + "%";
        auto stmt = leitura().prepare(
            "SELECT i.id, i.codigo_acervo, COALESCE(NULLIF(i.titulo, ''), a.caminho_relativo) "
            "FROM item i JOIN arquivo a ON a.item_id = i.id AND a.eh_master = 1 "
            "WHERE COALESCE(i.em_quarentena, 0) = 0 AND COALESCE(i.estado, '') != 'duplicata' AND COALESCE(a.papel, '') != 'capa_frente' "
            "AND (LOWER(a.caminho_relativo) LIKE '%.jpg' OR LOWER(a.caminho_relativo) LIKE '%.jpeg' OR "
            "     LOWER(a.caminho_relativo) LIKE '%.png' OR LOWER(a.caminho_relativo) LIKE '%.gif' OR "
            "     LOWER(a.caminho_relativo) LIKE '%.bmp' OR LOWER(a.caminho_relativo) LIKE '%.webp' OR "
            "     LOWER(a.caminho_relativo) LIKE '%.tif' OR LOWER(a.caminho_relativo) LIKE '%.tiff') "
            "AND (? = '' OR i.titulo LIKE ? OR i.codigo_acervo LIKE ? OR a.caminho_relativo LIKE ?) "
            "ORDER BY i.criado_em DESC LIMIT ?");
        stmt.bind(1, matriz::db::Value::of(termo));
        stmt.bind(2, matriz::db::Value::of(padrao));
        stmt.bind(3, matriz::db::Value::of(padrao));
        stmt.bind(4, matriz::db::Value::of(padrao));
        stmt.bind(5, matriz::db::Value::of(static_cast<long long>(limite)));
        while (stmt.step()) out.push_back({stmt.columnText(0), stmt.columnText(1), stmt.columnText(2)});
    } catch (const std::exception&) {}
    return out;
}

void ProjetoAberto::gerarMiniaturasFaltantes() {
    if (!projeto_) { DBG("gerarMiniaturasFaltantes: projeto_ é nulo"); return; }
    auto& reg = projeto_->registro();
    auto& idx = projeto_->indice();
    auto pasta = projeto_->pasta();

    std::set<std::string> comMiniatura;
    {
        auto stmt = idx.prepare("SELECT DISTINCT item_id FROM miniatura WHERE tipo = 'miniatura'");
        while (stmt.step()) comMiniatura.insert(stmt.columnText(0));
    }
    DBG("gerarMiniaturasFaltantes: " + juce::String((int)comMiniatura.size()) + " itens já têm miniatura");

    auto stmt = reg.prepare(
        std::string("SELECT a.item_id, a.id, a.caracteristicas_tecnicas_json, ") +
        matriz::vault::colunasDeResolucao() +
        " FROM arquivo a " + matriz::vault::joinDeResolucao() +
        " ORDER BY a.eh_master DESC, a.id");

    // Progresso visível (a geração rodava muda em background): total aproximado =
    // itens com arquivo que ainda não têm miniatura. Sem nada faltando, nenhuma tarefa.
    int totalFaltando = 0;
    {
        auto st = reg.prepare("SELECT COUNT(DISTINCT item_id) FROM arquivo");
        if (st.step()) totalFaltando = juce::jmax(0, static_cast<int>(st.columnInt(0)) - static_cast<int>(comMiniatura.size()));
    }
    const juce::String kIdTarefa = "catalog_thumbs";
    if (totalFaltando > 0)
        ProgressoGlobal::obterInstancia().iniciarTarefa(kIdTarefa, "Generating thumbnails", totalFaltando, nullptr,
                                                       "Checking files on disk...", false, true);
    juce::uint32 ultimoAviso = 0;

    std::set<std::string> jaProcessados;
    int gerados = 0, pulados = 0;
    while (stmt.step()) {
        std::string itemId = stmt.columnText(0);
        if (comMiniatura.count(itemId) || jaProcessados.count(itemId)) continue;
        jaProcessados.insert(itemId);

        std::string arquivoId = stmt.columnText(1);
        std::string jsonTecnico = stmt.columnText(2);
        juce::File arq = matriz::vault::caminhoEsperado(pasta, stmt.columnText(3),
                                                         stmt.columnText(4), stmt.columnText(5));
        if (totalFaltando > 0) {
            const juce::uint32 agora = juce::Time::getMillisecondCounter();
            if (agora - ultimoAviso >= 250) {
                ultimoAviso = agora;
                ProgressoGlobal::obterInstancia().atualizarProgresso(
                    kIdTarefa, juce::jmin(static_cast<int>(jaProcessados.size()), totalFaltando),
                    "Thumbnails " + juce::String(static_cast<int>(jaProcessados.size())) + " of " +
                        juce::String(totalFaltando) + "  |  " + arq.getFileName());
            }
        }
        if (!arq.existsAsFile()) { ++pulados; continue; }

        auto cat = matriz::ingest::categoriaPorExtensao(arq);
        if (cat != matriz::ingest::CategoriaMidia::Video &&
            cat != matriz::ingest::CategoriaMidia::Audio &&
            cat != matriz::ingest::CategoriaMidia::Imagem &&
            cat != matriz::ingest::CategoriaMidia::Documento &&
            cat != matriz::ingest::CategoriaMidia::Texto)
            continue;

        std::optional<double> duracao;
        juce::var raiz = juce::JSON::parse(jsonTecnico);
        if (raiz.isObject() && raiz.hasProperty("duracaoSegundos"))
            duracao = static_cast<double>(raiz["duracaoSegundos"]);

        if (!duracao.has_value() && (cat == matriz::ingest::CategoriaMidia::Video ||
                                     cat == matriz::ingest::CategoriaMidia::Audio)) {
            try {
                juce::StringArray args;
                args.add("-v"); args.add("quiet");
                args.add("-show_entries"); args.add("format=duration");
                args.add("-of"); args.add("default=noprint_wrappers=1:nokey=1");
                args.add(arq.getFullPathName());
                auto saida = matriz::ingest::capturarSaidaTexto("ffprobe", args, 10000);
                double d = juce::String(saida).trim().getDoubleValue();
                if (d > 0.0) duracao = d;
            } catch (...) {}
        }

        DBG("gerarMiniatura: " + arq.getFileName() + " cat=" + juce::String((int)cat)
            + " dur=" + juce::String(duracao.value_or(-1.0)));

        try {
            matriz::ingest::gerarEGravarMiniaturaPrincipal(idx, pasta, itemId, arquivoId, arq, cat, duracao);
            ++gerados;
        } catch (const std::exception& e) {
            DBG("gerarMiniatura ERRO: " + juce::String(e.what()));
        }
    }
    if (totalFaltando > 0)
        ProgressoGlobal::obterInstancia().concluirTarefa(
            kIdTarefa, "Catalog ready  |  " + juce::String(gerados) + " thumbnails created" +
                           (pulados > 0 ? "  |  " + juce::String(pulados) + " files offline" : juce::String()));
    DBG("gerarMiniaturasFaltantes: gerados=" + juce::String(gerados) + " pulados=" + juce::String(pulados)
        + " processados=" + juce::String((int)jaProcessados.size()));
}

std::optional<ProjetoAberto::ArquivoInfo> ProjetoAberto::arquivoPrincipal(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = leitura().prepare(
        std::string("SELECT a.id, a.papel, a.eh_master, a.caracteristicas_tecnicas_json, ") +
        matriz::vault::colunasDeResolucao() + " FROM arquivo a " + matriz::vault::joinDeResolucao() +
        // Item D.9/10 (Reload File/Replace File): uma derivada de "reload/
        // replace" (derivada_de_arquivo_id preenchido) vale mais que o
        // master enquanto existir — é o conteúdo atual do item; o master
        // propriamente dito nunca é tocado (fica preservado no histórico).
        " WHERE a.item_id = ? "
        "ORDER BY (CASE WHEN a.derivada_de_arquivo_id IS NOT NULL THEN 1 ELSE 0 END) DESC, "
        "a.eh_master DESC, a.id LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return std::nullopt;

    ArquivoInfo info;
    info.id = stmt.columnText(0);
    info.papel = stmt.columnText(1);
    info.ehMaster = stmt.columnInt(2) != 0;
    info.caracteristicasTecnicasJson = stmt.columnText(3);
    // Caminho ESPERADO, não resolvido: com o Vault offline (I3) a ficha
    // continua abrindo e o painel precisa poder dizer onde o arquivo mora.
    // Quem vai realmente ler os bytes checa existsAsFile().
    // Etapa 2: MAIN/CLONE primeiro — preview, player, miniatura e forma de
    // onda abrem a cópia do backup quando o SOURCE está guardado.
    info.caminhoAbsoluto = matriz::vault::caminhoEsperadoArquivo(leitura(), info.id, projeto_->pasta())
                               .getFullPathName();
    return info;
}

std::optional<ProjetoAberto::SugestaoCampo> ProjetoAberto::sugestaoPendente(const std::string& itemId,
                                                                             const std::string& nivel, int nivelIndice,
                                                                             const std::string& campoId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = leituraIndice().prepare(
        "SELECT id, valor, confianca, modelo, modelo_versao FROM sugestao_campo "
        "WHERE item_id = ? AND nivel = ? AND nivel_indice = ? AND campo_id = ? AND confirmado = 0 "
        "ORDER BY processado_em DESC LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    stmt.bind(2, matriz::db::Value::of(nivel));
    stmt.bind(3, matriz::db::Value::of(nivelIndice));
    stmt.bind(4, matriz::db::Value::of(campoId));
    if (!stmt.step()) return std::nullopt;

    SugestaoCampo s;
    s.id = stmt.columnText(0);
    s.valor = stmt.columnText(1);
    if (!stmt.columnIsNull(2)) s.confianca = stmt.columnReal(2);
    s.modelo = stmt.columnText(3);
    s.modeloVersao = stmt.columnText(4);
    return s;
}

void ProjetoAberto::confirmarSugestao(const SugestaoCampo& sugestao, const std::string& itemId,
                                       const std::string& nivel, int nivelIndice, const std::string& campoId,
                                       const std::string& autor) {
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();
    // Undo: o campo, o registro da confirmação no histórico e a sugestão (que volta a ficar pendente).
    registrarUndoDeLinhas(*this, "Confirm AI suggestion",
                          {{"item_campo", "item_id = ? AND nivel = ? AND nivel_indice = ? AND campo_id = ?",
                            {matriz::db::Value::of(itemId), matriz::db::Value::of(nivel), matriz::db::Value::of(nivelIndice),
                             matriz::db::Value::of(campoId)}, false},
                           {"item_historico", "item_id = ? AND tipo_evento = 'confirmacao_sugestao_ia' AND campo_id = ?",
                            {matriz::db::Value::of(itemId), matriz::db::Value::of(campoId)}, false},
                           {"sugestao_campo", "id = ?", {matriz::db::Value::of(sugestao.id)}, true}},
                          {itemId}, "campo");

    projeto_->registro().run(
        "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, 'humano', ?) "
        "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) "
        "DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(nivel),
         matriz::db::Value::of(nivelIndice), matriz::db::Value::of(campoId), matriz::db::Value::of(sugestao.valor),
         matriz::db::Value::of(agora)});

    projeto_->registro().run(
        "INSERT INTO item_historico (id, item_id, tipo_evento, campo_id, valor_novo, modelo_origem, "
        "modelo_origem_versao, confianca_origem, autor, criado_em) "
        "VALUES (?, ?, 'confirmacao_sugestao_ia', ?, ?, ?, ?, ?, ?, ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(campoId),
         matriz::db::Value::of(sugestao.valor), matriz::db::Value::of(sugestao.modelo),
         matriz::db::Value::of(sugestao.modeloVersao),
         sugestao.confianca ? matriz::db::Value::of(*sugestao.confianca) : matriz::db::Value::null(),
         matriz::db::Value::of(autor), matriz::db::Value::of(agora)});

    projeto_->indice().run("UPDATE sugestao_campo SET confirmado = 1, confirmado_por = ?, confirmado_em = ? WHERE id = ?",
                            {matriz::db::Value::of(autor), matriz::db::Value::of(agora), matriz::db::Value::of(sugestao.id)});
}

std::vector<ProjetoAberto::ItemObservacao> ProjetoAberto::observacoesDoItem(const std::string& itemId) const {
    std::vector<ItemObservacao> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT id, texto, autor, criado_em, minutagem_ms FROM item_observacao WHERE item_id = ? ORDER BY criado_em");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) {
        ItemObservacao o;
        o.id = stmt.columnText(0);
        o.texto = stmt.columnText(1);
        o.autor = stmt.columnText(2);
        o.criadoEm = stmt.columnText(3);
        if (!stmt.columnIsNull(4)) o.minutagemMs = static_cast<int64_t>(stmt.columnInt(4));
        out.push_back(std::move(o));
    }
    return out;
}

std::string ProjetoAberto::adicionarObservacao(const std::string& itemId, const std::string& texto,
                                                std::optional<int64_t> minutagemMs, const std::string& autor) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_) return {};
    std::string id = matriz::model::novoUuid();
    registrarUndoDeLinhas(*this, "Add Note", {{"item_observacao", "id = ?", {matriz::db::Value::of(id)}, false}}, {itemId}, "metadado");
    projeto_->registro().run(
        "INSERT INTO item_observacao (id, item_id, texto, autor, criado_em, minutagem_ms) VALUES (?, ?, ?, ?, ?, ?)",
        {matriz::db::Value::of(id), matriz::db::Value::of(itemId), matriz::db::Value::of(texto),
         matriz::db::Value::of(autor), matriz::db::Value::of(matriz::model::agoraIso8601()),
         minutagemMs ? matriz::db::Value::of(static_cast<long long>(*minutagemMs)) : matriz::db::Value::null()});
    return id;
}

void ProjetoAberto::atualizarObservacao(const std::string& observacaoId, const std::string& texto,
                                         std::optional<int64_t> minutagemMs) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    registrarUndoDeLinhas(*this, "Edit Note", {{"item_observacao", "id = ?", {matriz::db::Value::of(observacaoId)}, false}}, {}, "metadado");
    projeto_->registro().run(
        "UPDATE item_observacao SET texto = ?, minutagem_ms = ? WHERE id = ?",
        {matriz::db::Value::of(texto),
         minutagemMs ? matriz::db::Value::of(static_cast<long long>(*minutagemMs)) : matriz::db::Value::null(),
         matriz::db::Value::of(observacaoId)});
}

void ProjetoAberto::removerObservacao(const std::string& observacaoId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    registrarUndoDeLinhas(*this, "Delete Note", {{"item_observacao", "id = ?", {matriz::db::Value::of(observacaoId)}, false}}, {}, "metadado");
    projeto_->registro().run("DELETE FROM item_observacao WHERE id = ?", {matriz::db::Value::of(observacaoId)});
}

ProjetoAberto::NoArvore ProjetoAberto::arvoreOrigem(bool incluirTodos) const {
    if (!projeto_) return {};

    NoBuilder raiz;

    std::string sql =
        "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a WHERE a.caminho_absoluto_origem IS NOT NULL "
        "AND a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = a.item_id ORDER BY eh_master DESC, id LIMIT 1)";
    if (!incluirTodos)
        sql += " AND a.item_id NOT IN (SELECT item_id FROM acervo_item_pasta)";
    auto stmt = leitura().prepare(sql);

    struct Par { std::string itemId; juce::StringArray segmentos; };
    std::vector<Par> pares;

    while (stmt.step()) {
        Par p;
        p.itemId = stmt.columnText(0);
        juce::File pasta = juce::File(juce::String(stmt.columnText(1))).getParentDirectory();
        p.segmentos.addTokens(pasta.getFullPathName(), juce::File::getSeparatorString(), "");
        p.segmentos.removeEmptyStrings();
        pares.push_back(std::move(p));
    }

    // Collapse common prefix: only show from the loaded folder downward,
    // never volumes/HDs/ancestor directories.
    int prefixoComum = 0;
    if (!pares.empty()) {
        prefixoComum = pares[0].segmentos.size();
        for (size_t i = 1; i < pares.size(); ++i) {
            int n = juce::jmin(prefixoComum, pares[i].segmentos.size());
            int match = 0;
            while (match < n && pares[0].segmentos[match] == pares[i].segmentos[match])
                ++match;
            prefixoComum = match;
        }
        if (prefixoComum > 0)
            prefixoComum = prefixoComum - 1;
    }

    for (auto& p : pares) {
        NoBuilder* atual = &raiz;
        for (int s = prefixoComum; s < p.segmentos.size(); ++s)
            atual = obterOuCriarFilhoPorNome(*atual, p.segmentos[s]);
        atual->itemIdsDiretos.insert(p.itemId);
    }

    return materializar(raiz, true);
}

ProjetoAberto::NoArvore ProjetoAberto::podarArvore(const NoArvore& raiz, const std::set<std::string>& idsPermitidos) {
    NoArvore podado;
    podado.id = raiz.id;
    podado.nome = raiz.nome;
    podado.pastaPaiId = raiz.pastaPaiId;
    podado.posicaoX = raiz.posicaoX;
    podado.posicaoY = raiz.posicaoY;
    podado.ativo = raiz.ativo;

    for (const auto& id : raiz.itemIds)
        if (idsPermitidos.count(id)) podado.itemIds.insert(id);
    for (const auto& id : raiz.itemIdsDiretos)
        if (idsPermitidos.count(id)) podado.itemIdsDiretos.insert(id);

    for (const auto& filho : raiz.filhos) {
        auto filhoPodado = podarArvore(filho, idsPermitidos);
        if (!filhoPodado.itemIds.empty() || !filhoPodado.filhos.empty())
            podado.filhos.push_back(std::move(filhoPodado));
    }

    return podado;
}

const std::string ProjetoAberto::kMapaOriginal = "__ORIGINAL__";

std::string ProjetoAberto::mapaAtivoPadrao() const {
    if (!projeto_) return {};
    if (!mapaAtivoSelecionado_.empty() && mapaAtivoSelecionado_ != kMapaOriginal) {
        auto stmtSel = leitura().prepare("SELECT 1 FROM folder_map WHERE id = ? AND projeto_id = ?");
        stmtSel.bind(1, matriz::db::Value::of(mapaAtivoSelecionado_));
        stmtSel.bind(2, matriz::db::Value::of(projeto_->projetoId()));
        if (stmtSel.step()) return mapaAtivoSelecionado_;
    }
    auto stmt = leitura().prepare(
        "SELECT id FROM folder_map WHERE projeto_id = ? ORDER BY ordem, criado_em LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    return stmt.step() ? stmt.columnText(0) : std::string();
}

void ProjetoAberto::definirMapaAtivo(const std::string& mapaId) {
    mapaAtivoSelecionado_ = mapaId;
    if (!projeto_ || mapaId.empty()) return;
    projeto_->pasta().getChildFile("mapa_ativo.txt").replaceWithText(juce::String(mapaId));
    // Os cards de analytics (NO FOLDER) seguem o mapa selecionado no dropdown do Folder Map.
    EventBus::obterInstancia().dispararItemAlterado("", "mapa_ativo");
}

std::string ProjetoAberto::mapaInicialDoFolderMap() const {
    if (!projeto_) return kMapaOriginal;
    const auto arq = projeto_->pasta().getChildFile("mapa_ativo.txt");
    if (!arq.existsAsFile()) return kMapaOriginal;
    const std::string lembrado = arq.loadFileAsString().trim().toStdString();
    if (lembrado.empty() || lembrado == kMapaOriginal) return kMapaOriginal;
    auto stmt = leitura().prepare("SELECT 1 FROM folder_map WHERE id = ? AND projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(lembrado));
    stmt.bind(2, matriz::db::Value::of(projeto_->projetoId()));
    return stmt.step() ? lembrado : kMapaOriginal;
}

std::string ProjetoAberto::mapaSelecionadoNoFolderMap() const {
    return mapaAtivoSelecionado_.empty() ? mapaInicialDoFolderMap() : mapaAtivoSelecionado_;
}

std::vector<ProjetoAberto::FolderMapInfo> ProjetoAberto::listarFolderMaps() const {
    std::vector<FolderMapInfo> out;
    FolderMapInfo original;
    original.id = kMapaOriginal;
    original.nome = matriz::i18n::t("mapa.original");
    original.original = true;
    out.push_back(original);
    if (!projeto_) return out;

    auto stmt = leitura().prepare(
        "SELECT id, nome FROM folder_map WHERE projeto_id = ? ORDER BY ordem, criado_em");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        FolderMapInfo info;
        info.id = stmt.columnText(0);
        info.nome = juce::String(stmt.columnText(1));
        out.push_back(info);
    }
    return out;
}

std::string ProjetoAberto::criarFolderMap(const juce::String& nome, const std::optional<std::string>& origemMapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_ || nome.trim().isEmpty()) return {};

    std::string mapaId = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();

    int ordem = 0;
    {
        auto stmt = db.prepare("SELECT COALESCE(MAX(ordem), -1) + 1 FROM folder_map WHERE projeto_id = ?");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        if (stmt.step()) ordem = stmt.columnInt(0);
    }

    db.run("INSERT INTO folder_map (id, projeto_id, nome, ordem, criado_em, atualizado_em) VALUES (?, ?, ?, ?, ?, ?)",
           {matriz::db::Value::of(mapaId), matriz::db::Value::of(projeto_->projetoId()),
            matriz::db::Value::of(nome.trim().toStdString()), matriz::db::Value::of(ordem),
            matriz::db::Value::of(agora), matriz::db::Value::of(agora)});

    registrarUndo("Create Folder Map", [this, mapaId]() {
        auto& d = projeto_->registro();
        try {
            d.run("BEGIN TRANSACTION", {});
            d.run("DELETE FROM acervo_item_pasta WHERE mapa_id = ?", {matriz::db::Value::of(mapaId)});
            d.run("DELETE FROM acervo_pasta WHERE mapa_id = ?", {matriz::db::Value::of(mapaId)});
            d.run("DELETE FROM folder_map WHERE id = ?", {matriz::db::Value::of(mapaId)});
            d.run("COMMIT", {});
        } catch (...) { try { d.run("ROLLBACK", {}); } catch (...) {} }
    });
    if (!origemMapaId) return mapaId; // em branco

    // Cópia do ORIGINAL ou de outro mapa: um RETRATO do momento, não uma
    // referência viva — recria pastas + item->pasta com ids novos, nunca
    // reaproveita os do mapa de origem (evita colisão e mantém os mapas
    // totalmente independentes daqui em diante).
    NoArvore origem = arvoreAcervo(*origemMapaId);
    for (auto& raizFilho : origem.filhos) {
        // arvoreAcervo() sempre acrescenta um nó sintético "não organizados"
        // como filho da raiz, com id vazio (nenhuma pasta real tem id
        // vazio) — nunca copiado pra dentro do mapa novo.
        if (raizFilho.id.empty()) continue;
        replicarSubarvoreNoAcervo(raizFilho, "", true, mapaId);
    }
    return mapaId;
}

bool ProjetoAberto::renomearFolderMap(const std::string& mapaId, const juce::String& novoNome) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || mapaId == kMapaOriginal || novoNome.trim().isEmpty()) return false;
    if (!desfazendo_) {
        auto st = projeto_->registro().prepare("SELECT nome FROM folder_map WHERE id = ?");
        st.bind(1, matriz::db::Value::of(mapaId));
        if (st.step()) {
            const juce::String nomeAntes(st.columnText(0));
            registrarUndo("Rename Folder Map", [this, mapaId, nomeAntes]() { renomearFolderMap(mapaId, nomeAntes); });
        }
    }
    projeto_->registro().run("UPDATE folder_map SET nome = ?, atualizado_em = ? WHERE id = ? AND projeto_id = ?",
                              {matriz::db::Value::of(novoNome.trim().toStdString()),
                               matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(mapaId),
                               matriz::db::Value::of(projeto_->projetoId())});
    return true;
}

bool ProjetoAberto::apagarFolderMap(const std::string& mapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || mapaId == kMapaOriginal) return false;
    // Fase 2: o mapa do MAIN (backup_config_main.mapa_id) não pode ser apagado.
    if (mapaId == mapaDoMainId()) return false;
    registrarUndoSnapshotMapa("Delete Folder Map", mapaId, true);
    projeto_->registro().run("DELETE FROM folder_map WHERE id = ? AND projeto_id = ?",
                              {matriz::db::Value::of(mapaId), matriz::db::Value::of(projeto_->projetoId())});
    return true;
}

std::set<std::string> ProjetoAberto::itensSemPasta(const std::string& mapaId) const {
    std::set<std::string> out;
    if (!projeto_ || mapaId == kMapaOriginal) return out;
    auto stmt = leitura().prepare(
        "SELECT id FROM item WHERE projeto_id = ? AND COALESCE(estado, '') != 'duplicata' AND id NOT IN "
        "(SELECT aip.item_id FROM acervo_item_pasta aip WHERE aip.mapa_id = ?)");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(mapaId));
    while (stmt.step()) out.insert(stmt.columnText(0));
    return out;
}

std::vector<ProjetoAberto::DuplicataResolvida> ProjetoAberto::listarDuplicatasResolvidas() const {
    std::vector<DuplicataResolvida> out;
    if (!projeto_) return out;
    try {
        auto st = leitura().prepare(
            "SELECT i.id, COALESCE(i.codigo_acervo, ''), COALESCE(i.titulo, ''), "
            "COALESCE((SELECT COALESCE(NULLIF(a.caminho_absoluto_origem, ''), a.caminho_relativo) FROM arquivo a "
            "          WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), '') "
            "FROM item i WHERE i.estado = 'duplicata' ORDER BY i.codigo_acervo, i.id");
        while (st.step()) out.push_back({st.columnText(0), st.columnText(1), st.columnText(2), st.columnText(3)});
    } catch (...) {}
    return out;
}

int ProjetoAberto::contarDuplicatasResolvidas() const {
    if (!projeto_) return 0;
    try {
        auto st = leitura().prepare("SELECT COUNT(*) FROM item WHERE estado = 'duplicata'");
        return st.step() ? static_cast<int>(st.columnInt(0)) : 0;
    } catch (...) { return 0; }
}

int ProjetoAberto::restaurarDuplicatas(const std::vector<std::string>& itemIds) {
    using matriz::db::Value;
    if (somenteLeitura_) { avisarSomenteLeitura(); return 0; }
    if (!projeto_ || itemIds.empty()) return 0;
    auto& db = projeto_->registro();
    const std::string agora = matriz::model::agoraIso8601();
    const std::string bloco = "\n\n[RESTORED FROM RESOLVED DUPLICATES] " + agora +
                              " - the user dismissed this duplicate; item returned to the grid. [USER_VERIFIED_NOT_DUPLICATE]";
    int restaurados = 0;
    // Undo: estado e notas de cada item como estavam (o item volta a ser duplicata e sai da grade).
    registrarUndoDeColunasDosItens("Restore Duplicates", {"estado", "notas_livres"}, itemIds);
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    try {
        db.exec("BEGIN IMMEDIATE");  // N itens = uma transação
        for (const auto& id : itemIds) {
            db.run("UPDATE item SET estado = 'novo', notas_livres = COALESCE(notas_livres, '') || ?, atualizado_em = ? "
                   "WHERE id = ? AND estado = 'duplicata'",
                   {Value::of(bloco), Value::of(agora), Value::of(id)});
            auto ch = db.prepare("SELECT changes()");
            if (ch.step()) restaurados += static_cast<int>(ch.columnInt(0));
        }
        db.exec("COMMIT");
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        return 0;
    }
    escrita.unlock();
    if (restaurados > 0) EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");
    return restaurados;
}

int ProjetoAberto::contarItensSemPasta(const std::string& mapaId) const {
    if (!projeto_ || mapaId == kMapaOriginal) return 0;
    auto stmt = leitura().prepare(
        "SELECT COUNT(*) FROM item WHERE projeto_id = ? AND COALESCE(estado, '') != 'duplicata' AND id NOT IN "
        "(SELECT aip.item_id FROM acervo_item_pasta aip WHERE aip.mapa_id = ?)");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(mapaId));
    return stmt.step() ? stmt.columnInt(0) : 0;
}

std::string ProjetoAberto::mapaIdDaPasta(const std::string& pastaId) const {
    if (!projeto_ || pastaId.empty()) return {};
    auto stmt = leitura().prepare("SELECT mapa_id FROM acervo_pasta WHERE id = ?");
    stmt.bind(1, matriz::db::Value::of(pastaId));
    if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
    return {};
}

void ProjetoAberto::inserirItemPastaInterno(const std::string& itemId, const std::string& pastaId,
                                             const std::string& agora) {
    if (!projeto_) return;
    std::string mapaId = mapaIdDaPasta(pastaId);
    projeto_->registro().run(
        "INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, mapa_id, criado_em) VALUES (?, ?, ?, ?, ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
         matriz::db::Value::of(pastaId),
         mapaId.empty() ? matriz::db::Value::null() : matriz::db::Value::of(mapaId),
         matriz::db::Value::of(agora)});
}

ProjetoAberto::NoArvore ProjetoAberto::arvoreAcervo(const std::string& mapaId) const {
    if (!projeto_) return {};
    if (mapaId == kMapaOriginal) return construirArvoreOriginalVirtual(leitura());

    struct Registro {
        std::string id;
        std::optional<std::string> paiId;
    };
    std::vector<Registro> registros;
    std::unordered_map<std::string, std::unique_ptr<NoBuilder>> porId;
    std::unordered_map<std::string, NoBuilder*> ptrPorId;

    auto stmt = leitura().prepare(
        "SELECT id, pasta_pai_id, nome, posicao_x, posicao_y, ativo, cor_customizada, regra_organizacao, escala_no, proposta_id FROM acervo_pasta "
        "WHERE projeto_id = ? AND mapa_id = ? ORDER BY ordem, criado_em");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(mapaId));
    while (stmt.step()) {
        Registro r;
        r.id = stmt.columnText(0);
        if (!stmt.columnIsNull(1)) r.paiId = stmt.columnText(1);
        registros.push_back(r);

        auto no = std::make_unique<NoBuilder>();
        no->id = r.id;
        if (r.paiId) no->pastaPaiId = *r.paiId;
        no->nome = stmt.columnText(2);
        no->posicaoX = stmt.columnInt(3);
        no->posicaoY = stmt.columnInt(4);
        no->ativo = (stmt.columnInt(5) != 0);
        if (!stmt.columnIsNull(6)) no->corCustomizadaHex = juce::String(stmt.columnText(6));
        if (!stmt.columnIsNull(7)) no->regraOrganizacao = juce::String(stmt.columnText(7));
        if (!stmt.columnIsNull(8)) no->escalaNo = stmt.columnReal(8);
        no->proposta = !stmt.columnIsNull(9) && !stmt.columnText(9).empty();
        ptrPorId[r.id] = no.get();
        porId[r.id] = std::move(no);
    }

    NoBuilder raiz;
    for (auto& r : registros) {
        NoBuilder* alvo = (r.paiId && ptrPorId.count(*r.paiId)) ? ptrPorId[*r.paiId] : &raiz;
        alvo->filhos.push_back(std::move(porId[r.id]));
    }

    auto stmtItens = leitura().prepare(
        "SELECT pasta_id, item_id FROM acervo_item_pasta WHERE mapa_id = ?");
    stmtItens.bind(1, matriz::db::Value::of(mapaId));
    while (stmtItens.step()) {
        auto it = ptrPorId.find(stmtItens.columnText(0));
        if (it != ptrPorId.end()) it->second->itemIdsDiretos.insert(stmtItens.columnText(1));
    }

    NoArvore raizFinal = materializar(raiz, false);
    raizFinal.id.clear();
    raizFinal.nome = juce::String();

    NoArvore naoOrganizados;
    naoOrganizados.nome = matriz::i18n::t("arvore.nao_organizados");
    naoOrganizados.itemIds = itensSemPasta(mapaId);
    naoOrganizados.itemIdsDiretos = naoOrganizados.itemIds;
    raizFinal.filhos.push_back(std::move(naoOrganizados));

    return raizFinal;
}

std::string ProjetoAberto::criarPastaAcervo(const std::string& nome, const std::optional<std::string>& pastaPaiId,
                                             const std::string& mapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return {};
    std::string id = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();

    // pastaPaiId, se dado, precisa pertencer ao MESMO mapa — senão a nova
    // pasta viraria uma ponte entre dois mapas que deveriam ser
    // independentes. Defensivo: promove a raiz do mapa em vez de falhar.
    std::optional<std::string> pastaPaiIdSaneado = pastaPaiId;
    if (pastaPaiIdSaneado && mapaIdDaPasta(*pastaPaiIdSaneado) != mapaId) pastaPaiIdSaneado = std::nullopt;
    const auto& pastaPaiIdEfetivo = pastaPaiIdSaneado;

    int ordem = 0;
    {
        auto stmt = projeto_->registro().prepare(
            pastaPaiIdEfetivo ? "SELECT COALESCE(MAX(ordem), -1) + 1 FROM acervo_pasta WHERE pasta_pai_id = ?"
                       : "SELECT COALESCE(MAX(ordem), -1) + 1 FROM acervo_pasta WHERE pasta_pai_id IS NULL AND projeto_id = ? AND mapa_id = ?");
        stmt.bind(1, pastaPaiIdEfetivo ? matriz::db::Value::of(*pastaPaiIdEfetivo) : matriz::db::Value::of(projeto_->projetoId()));
        if (!pastaPaiIdEfetivo) stmt.bind(2, matriz::db::Value::of(mapaId));
        if (stmt.step()) ordem = stmt.columnInt(0);
    }

    projeto_->registro().run(
        "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mapa_id, posicao_x, posicao_y, ativo, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, 0, 0, 1, ?, ?)",
        {matriz::db::Value::of(id), matriz::db::Value::of(projeto_->projetoId()),
         pastaPaiIdEfetivo ? matriz::db::Value::of(*pastaPaiIdEfetivo) : matriz::db::Value::null(), matriz::db::Value::of(nome),
         matriz::db::Value::of(ordem), matriz::db::Value::of(mapaId), matriz::db::Value::of(agora),
         matriz::db::Value::of(agora)});

    // MAIN EDIT MODE: pasta nova no mapa do MAIN nasce junto no disco, com desfazer.
    if (editandoMain_ && !mapaDoMainId().empty() && mapaId == mapaDoMainId()) {
        auto r = matriz::mainedit::criarPastaFisica(contextoDoMain(), id);
        if (!r.ok) avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str()));
        tocarAtividadeMain();
        if (r.ok && !desfazendo_) {
            registrarUndo("Create Folder", [this, id]() {
                const auto rel = matriz::consolidacao::caminhoFisicoDaPasta(projeto_->registro(), id);
                if (!apagarPastaAcervo(id)) return;
                const juce::File dir = contextoDoMain().media().getChildFile(rel);
                if (rel.isNotEmpty() && dir.isDirectory() && dir.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0)
                    dir.deleteFile();
            });
        }
    }
    return id;
}

bool ProjetoAberto::mainExiste() const {
    if (!projeto_) return false;
    if (editandoMain_) return false;  // rebuild: o MAIN antigo é só fonte; o projeto está sem MAIN
    try {
        auto st = leitura().prepare("SELECT 1 FROM consolidacao_registro LIMIT 1");
        return st.step();
    } catch (...) {
        return false;
    }
}

std::string ProjetoAberto::mapaDoMainId() const {
    if (!projeto_) return {};
    try {
        auto st = leitura().prepare("SELECT COALESCE(backup_config_main, '') FROM projeto LIMIT 1");
        if (!st.step()) return {};
        juce::var cfg = juce::JSON::parse(juce::String::fromUTF8(st.columnText(0).c_str()));
        if (!cfg.isObject()) return {};
        return cfg.getProperty("mapa_id", "").toString().toStdString();
    } catch (...) {
        return {};
    }
}

juce::String ProjetoAberto::nomeDoMapaDoMain() const {
    const auto id = mapaDoMainId();
    if (id.empty() || !projeto_) return {};
    try {
        auto st = leitura().prepare("SELECT nome FROM folder_map WHERE id = ?");
        st.bind(1, matriz::db::Value::of(id));
        return st.step() ? juce::String::fromUTF8(st.columnText(0).c_str()) : juce::String();
    } catch (...) {
        return {};
    }
}

bool ProjetoAberto::pastaTemArquivosNoMain(const std::string& pastaId) const {
    if (!projeto_) return false;
    if (editandoMain_) return false;  // rebuild: estrutura livre, nada travado pelo MAIN antigo
    try {
        // Fase 2: as travas valem só pro mapa do MAIN; os outros mapas do
        // usuário ficam sempre livres. Sem mapa_id gravado, MAIN por regra
        // ou estrutura original não usa mapa nenhum. Só projeto sem config
        // (MAIN legado) segue a regra antiga, sem distinguir mapa.
        const auto mapaMain = mapaDoMainId();
        bool temConfig = false;
        {
            auto sc = leitura().prepare("SELECT COALESCE(backup_config_main, '') FROM projeto LIMIT 1");
            if (sc.step()) temConfig = juce::JSON::parse(juce::String::fromUTF8(sc.columnText(0).c_str())).isObject();
        }
        if (temConfig && mapaIdDaPasta(pastaId) != mapaMain) return false;
        // A pasta e todas as subpastas: algum item dali (hoje ou quando foi
        // copiado) já tem cópia registrada no MAIN.
        auto st = leitura().prepare(
            "WITH RECURSIVE sub(id) AS (SELECT ? UNION ALL "
            "  SELECT p.id FROM acervo_pasta p JOIN sub ON p.pasta_pai_id = sub.id) "
            "SELECT 1 FROM consolidacao_registro cr WHERE cr.pasta_id IN (SELECT id FROM sub) "
            "   OR cr.item_id IN (SELECT aip.item_id FROM acervo_item_pasta aip WHERE aip.pasta_id IN (SELECT id FROM sub)) "
            "LIMIT 1");
        st.bind(1, matriz::db::Value::of(pastaId));
        return st.step();
    } catch (...) {
        return false;
    }
}

// ── Auto-organização por pasta (Folder Map) ─────────────────────────────

namespace {

// Ano de um texto de data: primeira sequência de 4 dígitos entre 1800 e 2099.
juce::String anoDeTextoDeData(const juce::String& texto) {
    for (int i = 0; i + 3 < texto.length(); ++i) {
        if (juce::CharacterFunctions::isDigit(texto[i]) && juce::CharacterFunctions::isDigit(texto[i + 1]) &&
            juce::CharacterFunctions::isDigit(texto[i + 2]) && juce::CharacterFunctions::isDigit(texto[i + 3])) {
            const int ano = texto.substring(i, i + 4).getIntValue();
            if (ano >= 1800 && ano <= 2099) return juce::String(ano);
        }
    }
    return {};
}

// Mesma limpeza de nome de pasta do backup (Consolidacao.cpp: segmentoSeguro).
juce::String segmentoDePastaAuto(const juce::String& bruto, const juce::String& vazio) {
    juce::String s = bruto.trim();
    for (auto c : juce::String("/\\:*?\"<>|")) s = s.replaceCharacter(c, '-');
    s = s.trimCharactersAtEnd(". ");
    return s.isEmpty() ? vazio : s;
}

bool nivelPermitidoNaAutoOrganizacao(matriz::consolidacao::NivelHierarquia n) {
    using N = matriz::consolidacao::NivelHierarquia;
    return n == N::Ano || n == N::TipoMidia || n == N::TipoArquivo || n == N::Origem || n == N::Artista ||
           n == N::ContentType || n == N::Subject;
}

// Só as chaves de fato usadas por desfazer/refazer de uma organização.
struct LinhaPasta {
    std::string id, projetoId, paiId, nome, mapaId, cor, regra, criadoEm, atualizadoEm;
    bool paiNulo = true, corNula = true, regraNula = true;
    long long ordem = 0, x = 0, y = 0, ativo = 1;
};

} // namespace

std::optional<int> ProjetoAberto::extrairAnoDeData(const juce::String& texto) {
    const auto ano = anoDeTextoDeData(texto);
    if (ano.isEmpty()) return std::nullopt;
    return ano.getIntValue();
}

std::optional<int> ProjetoAberto::anoDoEventDate(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    try {
        auto st = leitura().prepare(
            "SELECT ano FROM item WHERE id = ?");
        st.bind(1, matriz::db::Value::of(itemId));
        if (st.step() && !st.columnIsNull(0)) return extrairAnoDeData(juce::String::fromUTF8(st.columnText(0).c_str()));
    } catch (...) {}
    return std::nullopt;
}

std::map<std::string, std::vector<juce::String>> ProjetoAberto::segmentosDeOrganizacao(
    const std::set<std::string>& itemIds, const matriz::consolidacao::HierarquiaBackup& niveis) const {
    using N = matriz::consolidacao::NivelHierarquia;
    std::map<std::string, std::vector<juce::String>> out;
    if (!projeto_ || itemIds.empty() || niveis.empty()) return out;

    const std::vector<std::string> ids(itemIds.begin(), itemIds.end());
    constexpr size_t kLote = 500;
    for (size_t inicio = 0; inicio < ids.size(); inicio += kLote) {
        const size_t fim = std::min(ids.size(), inicio + kLote);
        std::string marcas;
        for (size_t i = inicio; i < fim; ++i) marcas += (i > inicio ? ",?" : "?");
        auto stmt = leitura().prepare(
            "SELECT i.id, i.tipo_midia, i.dc_creator, i.collection_type, i.dc_subject, i.source_media, "
            "i.ano, "
            "a.caminho_relativo "
            "FROM item i "
            "LEFT JOIN arquivo a ON a.item_id = i.id AND a.id = ("
            "  SELECT a2.id FROM arquivo a2 WHERE a2.item_id = i.id ORDER BY a2.eh_master DESC, a2.id LIMIT 1) "
            "WHERE i.id IN (" + marcas + ")");
        for (size_t i = inicio; i < fim; ++i) stmt.bind(static_cast<int>(i - inicio) + 1, matriz::db::Value::of(ids[i]));

        auto texto = [&](int col) { return stmt.columnIsNull(col) ? juce::String() : juce::String::fromUTF8(stmt.columnText(col).c_str()); };
        while (stmt.step()) {
            std::vector<juce::String> segs;
            segs.reserve(niveis.size());
            for (auto nivel : niveis) {
                switch (nivel) {
                    case N::Ano: {
                        // Só o EVENT DATE (item.ano, o que a ficha mostra): vazio ou "0" = "No year"
                        // (mesma regra do Unknown do METADATA).
                        segs.push_back(segmentoDePastaAuto(anoDeTextoDeData(texto(6)), "No year"));
                        break;
                    }
                    case N::TipoMidia: segs.push_back(segmentoDePastaAuto(texto(1), "Unclassified")); break;
                    case N::TipoArquivo: {
                        const auto caminho = texto(7);
                        const int ponto = caminho.lastIndexOfChar('.');
                        const int barra = juce::jmax(caminho.lastIndexOfChar('/'), caminho.lastIndexOfChar('\\'));
                        const juce::String ext = ponto > barra ? caminho.substring(ponto + 1).toUpperCase() : juce::String();
                        segs.push_back(segmentoDePastaAuto(ext, "No extension"));
                        break;
                    }
                    case N::Origem: {
                        // source_media é um JSON com a chave "medium"; valor legado não-JSON vale direto.
                        const auto bruto = texto(5);
                        juce::String medium;
                        if (bruto.isNotEmpty()) {
                            juce::var parsed = juce::JSON::parse(bruto);
                            if (auto* obj = parsed.getDynamicObject()) {
                                if (obj->hasProperty("medium")) medium = obj->getProperty("medium").toString();
                            } else {
                                medium = bruto;
                            }
                        }
                        segs.push_back(segmentoDePastaAuto(medium, "No source medium"));
                        break;
                    }
                    case N::Artista: segs.push_back(segmentoDePastaAuto(texto(2), "No creator")); break;
                    case N::ContentType: segs.push_back(segmentoDePastaAuto(texto(3), "No content")); break;
                    case N::Subject: segs.push_back(segmentoDePastaAuto(texto(4), "No subject")); break;
                    default: break;
                }
            }
            out[stmt.columnText(0)] = std::move(segs);
        }
    }
    return out;
}

ProjetoAberto::ResultadoAutoOrg ProjetoAberto::aplicarAutoOrganizacao(const std::string& pastaId, const std::string& regraCsv,
                                                                      bool automatico) {
    ResultadoAutoOrg res;
    if (somenteLeitura_) { res.status = StatusAutoOrg::SomenteLeitura; return res; }
    if (!projeto_ || pastaId.empty()) { res.status = StatusAutoOrg::PastaInvalida; return res; }
    const std::string mapaId = mapaIdDaPasta(pastaId);
    if (mapaId.empty()) { res.status = StatusAutoOrg::PastaInvalida; return res; }
    if (mapaId == kMapaOriginal) { res.status = StatusAutoOrg::MapaOriginal; return res; }

    matriz::consolidacao::HierarquiaBackup niveis;
    for (auto n : matriz::consolidacao::hierarquiaDeCsv(regraCsv))
        if (nivelPermitidoNaAutoOrganizacao(n)) niveis.push_back(n);
    if (niveis.empty() || regraCsv.empty()) { res.status = StatusAutoOrg::SemNiveis; return res; }
    const std::string csv = matriz::consolidacao::hierarquiaParaCsv(niveis);

    auto& db = projeto_->registro();

    // 1. A pasta e as subpastas AUTO dela (à mão ficam de fora), com os itens de cada uma.
    std::vector<LinhaPasta> linhas;
    {
        auto st = db.prepare(
            "SELECT id, projeto_id, pasta_pai_id, nome, ordem, mapa_id, posicao_x, posicao_y, ativo, cor_customizada, "
            "regra_organizacao, criado_em, atualizado_em FROM acervo_pasta WHERE projeto_id = ? AND mapa_id = ? "
            "ORDER BY ordem, criado_em");
        st.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        st.bind(2, matriz::db::Value::of(mapaId));
        while (st.step()) {
            LinhaPasta l;
            l.id = st.columnText(0);
            l.projetoId = st.columnText(1);
            l.paiNulo = st.columnIsNull(2);
            if (!l.paiNulo) l.paiId = st.columnText(2);
            l.nome = st.columnText(3);
            l.ordem = st.columnInt(4);
            l.mapaId = st.columnText(5);
            l.x = st.columnInt(6);
            l.y = st.columnInt(7);
            l.ativo = st.columnInt(8);
            l.corNula = st.columnIsNull(9);
            if (!l.corNula) l.cor = st.columnText(9);
            l.regraNula = st.columnIsNull(10) || st.columnText(10).empty();
            if (!l.regraNula) l.regra = st.columnText(10);
            l.criadoEm = st.columnText(11);
            l.atualizadoEm = st.columnText(12);
            linhas.push_back(std::move(l));
        }
    }
    std::map<std::string, size_t> indice;
    for (size_t i = 0; i < linhas.size(); ++i) indice[linhas[i].id] = i;
    auto alvoIt = indice.find(pastaId);
    if (alvoIt == indice.end()) { res.status = StatusAutoOrg::PastaInvalida; return res; }
    if (regraEhAuto(juce::String(linhas[alvoIt->second].regra))) { res.status = StatusAutoOrg::SubpastaAuto; return res; }
    if (pastaTemArquivosNoMain(pastaId)) { res.status = StatusAutoOrg::TemArquivosNoMain; return res; }
    const std::string regraAnterior = linhas[alvoIt->second].regra;
    const bool regraAnteriorNula = linhas[alvoIt->second].regraNula;

    std::map<std::string, std::vector<std::string>> filhosPorPai;
    for (const auto& l : linhas)
        if (!l.paiNulo) filhosPorPai[l.paiId].push_back(l.id);
    // Subpastas AUTO alcançáveis a partir da pasta por pastas AUTO (uma pasta manual barra o caminho).
    std::vector<std::string> autoDescendentes;
    {
        std::vector<std::string> pilha{pastaId};
        while (!pilha.empty()) {
            const auto atual = pilha.back();
            pilha.pop_back();
            for (const auto& f : filhosPorPai[atual]) {
                if (regraEhAuto(juce::String(linhas[indice[f]].regra))) {
                    autoDescendentes.push_back(f);
                    pilha.push_back(f);
                }
            }
        }
    }
    std::set<std::string> pastasDeOrigem(autoDescendentes.begin(), autoDescendentes.end());
    pastasDeOrigem.insert(pastaId);

    std::map<std::string, std::vector<std::string>> pastasAtuaisDoItem;  // item -> pastas DESTE mapa
    std::set<std::string> itensAOrganizar;
    {
        auto st = db.prepare("SELECT item_id, pasta_id FROM acervo_item_pasta WHERE mapa_id = ?");
        st.bind(1, matriz::db::Value::of(mapaId));
        std::vector<std::pair<std::string, std::string>> todas;
        while (st.step()) todas.emplace_back(st.columnText(0), st.columnText(1));
        for (const auto& [item, pasta] : todas)
            if (pastasDeOrigem.count(pasta)) itensAOrganizar.insert(item);
        for (const auto& [item, pasta] : todas)
            if (itensAOrganizar.count(item)) pastasAtuaisDoItem[item].push_back(pasta);
    }

    // 2. Segmentos de cada item, numa consulta por lote.
    const auto segmentos = segmentosDeOrganizacao(itensAOrganizar, niveis);

    // 3. Caminhos distintos, em ordem alfabética, pra as pastas nascerem já ordenadas.
    auto chave = [](const std::vector<juce::String>& segs) {
        std::vector<std::string> k;
        for (const auto& s : segs) k.push_back(s.toLowerCase().toStdString());
        return k;
    };
    std::map<std::vector<std::string>, std::vector<juce::String>> caminhos;
    for (const auto& [item, segs] : segmentos) caminhos.emplace(chave(segs), segs);

    // Desfazer: uma organização = um grupo. A passada automática não registra nada (senão o
    // Cmd+Z seria desfeito de novo no próximo recarregamento do mapa).
    const bool registrarDesfazer = !automatico && !desfazendo_;
    const bool eraDesfazendo = desfazendo_;
    if (automatico) desfazendo_ = true;  // criarPastaAcervo/apagar não registram nada nesta passada
    struct RestauraFlag {
        bool& flag; bool valor;
        ~RestauraFlag() { flag = valor; }
    } restaura{desfazendo_, eraDesfazendo};
    if (registrarDesfazer) iniciarGrupoUndo("Auto-organize folder");

    std::unique_lock<std::recursive_mutex> trava(writeMutex());
    bool emTransacao = false;
    try { db.exec("BEGIN IMMEDIATE"); emTransacao = true; } catch (...) {}
    const std::string agora = matriz::model::agoraIso8601();
    std::vector<std::string> criadas;
    std::vector<LinhaPasta> apagadas;
    std::vector<std::pair<std::string, std::string>> associacoesAntes;  // (item, pasta) dos itens movidos
    std::map<std::string, std::string> destinoDoItem;
    bool falhou = false;
    try {
        // Regra da pasta.
        db.run("UPDATE acervo_pasta SET regra_organizacao = ?, atualizado_em = ? WHERE id = ?",
               {matriz::db::Value::of(csv), matriz::db::Value::of(agora), matriz::db::Value::of(pastaId)});

        // Subpastas: reaproveita as AUTO pelo nome, cria as que faltam.
        std::map<std::string, std::map<std::string, std::string>> autoFilhoPorNome;  // pai -> nome minúsculo -> id
        for (const auto& id : autoDescendentes) {
            const auto& l = linhas[indice[id]];
            autoFilhoPorNome[l.paiId][juce::String(l.nome).toLowerCase().toStdString()] = id;
        }
        std::map<std::vector<std::string>, std::string> pastaDoCaminho;
        for (const auto& [k, segs] : caminhos) {
            std::string atual = pastaId;
            for (const auto& seg : segs) {
                const auto nomeMin = seg.toLowerCase().toStdString();
                auto& mapaFilhos = autoFilhoPorNome[atual];
                auto it = mapaFilhos.find(nomeMin);
                if (it == mapaFilhos.end()) {
                    const std::string novo = criarPastaAcervo(seg.toStdString(), atual, mapaId);
                    if (novo.empty()) throw std::runtime_error("criarPastaAcervo falhou");
                    db.run("UPDATE acervo_pasta SET regra_organizacao = '@auto' WHERE id = ?", {matriz::db::Value::of(novo)});
                    criadas.push_back(novo);
                    it = mapaFilhos.emplace(nomeMin, novo).first;
                }
                atual = it->second;
            }
            pastaDoCaminho[k] = atual;
        }

        // Itens: cada um vai pra pasta do caminho dele (mesma transação, sem N transações).
        for (const auto& [item, segs] : segmentos) {
            const std::string& destino = pastaDoCaminho[chave(segs)];
            const auto& atuais = pastasAtuaisDoItem[item];
            if (atuais.size() == 1 && atuais.front() == destino) continue;
            for (const auto& p : atuais) associacoesAntes.emplace_back(item, p);
            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(item), matriz::db::Value::of(mapaId)});
            db.run("INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, mapa_id, criado_em) VALUES (?, ?, ?, ?, ?)",
                   {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item),
                    matriz::db::Value::of(destino), matriz::db::Value::of(mapaId), matriz::db::Value::of(agora)});
            destinoDoItem[item] = destino;
            ++res.itensMovidos;
        }

        // Subpastas AUTO que ficaram vazias (sem itens e sem filhas), da mais funda pra mais rasa.
        // Nunca apaga pasta manual, nem AUTO que abriga pasta manual.
        std::set<std::string> comItens;
        {
            auto st = db.prepare("SELECT DISTINCT pasta_id FROM acervo_item_pasta WHERE mapa_id = ?");
            st.bind(1, matriz::db::Value::of(mapaId));
            while (st.step()) comItens.insert(st.columnText(0));
        }
        std::map<std::string, int> filhasRestantes;
        for (const auto& l : linhas)
            if (!l.paiNulo) ++filhasRestantes[l.paiId];
        const std::set<std::string> criadasSet(criadas.begin(), criadas.end());
        for (const auto& [pai, filhos] : autoFilhoPorNome)
            for (const auto& [nome, id] : filhos)
                if (criadasSet.count(id)) ++filhasRestantes[pai];
        std::vector<std::string> candidatas = autoDescendentes;
        // Da mais funda pra mais rasa: profundidade pelo número de ancestrais AUTO.
        auto profundidade = [&](const std::string& id) {
            int p = 0;
            std::string cur = id;
            while (cur != pastaId && indice.count(cur) && !linhas[indice[cur]].paiNulo) { cur = linhas[indice[cur]].paiId; ++p; }
            return p;
        };
        std::sort(candidatas.begin(), candidatas.end(),
                  [&](const std::string& a, const std::string& b) { return profundidade(a) > profundidade(b); });
        for (const auto& id : candidatas) {
            if (comItens.count(id) || filhasRestantes[id] > 0) continue;
            LinhaPasta copia = linhas[indice[id]];
            db.run("DELETE FROM acervo_pasta WHERE id = ?", {matriz::db::Value::of(id)});
            apagadas.push_back(copia);
            --filhasRestantes[copia.paiId];
            ++res.pastasApagadas;
        }
        res.pastasCriadas = static_cast<int>(criadas.size());
    } catch (...) {
        falhou = true;
    }
    if (emTransacao) {
        try { db.exec(falhou ? "ROLLBACK" : "COMMIT"); } catch (...) { try { db.exec("ROLLBACK"); } catch (...) {} falhou = true; }
    }
    if (falhou) {
        if (registrarDesfazer) grupoAberto_.reset();
        res = ResultadoAutoOrg{};
        res.status = StatusAutoOrg::PastaInvalida;
        return res;
    }

    // Reversões, na ordem em que foram registradas (o desfazer as executa de trás pra frente):
    // regra -> pastas criadas -> itens movidos -> pastas apagadas.
    // Reorganizar sem nada a mudar não deixa entrada de desfazer.
    const bool mudouAlgo = !criadas.empty() || !destinoDoItem.empty() || !apagadas.empty() || regraAnteriorNula || regraAnterior != csv;
    if (registrarDesfazer && !mudouAlgo) grupoAberto_.reset();
    if (registrarDesfazer && mudouAlgo) {
        registrarUndo("Auto-organize folder", [this, pastaId, regraAnterior, regraAnteriorNula]() {
            projeto_->registro().run("UPDATE acervo_pasta SET regra_organizacao = ? WHERE id = ?",
                                     {regraAnteriorNula ? matriz::db::Value::null() : matriz::db::Value::of(regraAnterior),
                                      matriz::db::Value::of(pastaId)});
        });
        if (!criadas.empty())
            registrarUndo("Auto-organize folder", [this, criadas]() {
                for (auto it = criadas.rbegin(); it != criadas.rend(); ++it)
                    projeto_->registro().run("DELETE FROM acervo_pasta WHERE id = ?", {matriz::db::Value::of(*it)});
            });
        if (!associacoesAntes.empty() || !destinoDoItem.empty()) {
            std::set<std::string> movidos;
            for (const auto& [item, d] : destinoDoItem) movidos.insert(item);
            registrarUndo("Auto-organize folder", [this, mapaId, associacoesAntes, movidos]() {
                auto& reg = projeto_->registro();
                const std::string quando = matriz::model::agoraIso8601();
                reg.run("BEGIN TRANSACTION", {});
                try {
                    for (const auto& item : movidos)
                        reg.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                                {matriz::db::Value::of(item), matriz::db::Value::of(mapaId)});
                    for (const auto& [item, pasta] : associacoesAntes)
                        reg.run("INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, mapa_id, criado_em) VALUES (?, ?, ?, ?, ?)",
                                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item),
                                 matriz::db::Value::of(pasta), matriz::db::Value::of(mapaId), matriz::db::Value::of(quando)});
                    reg.run("COMMIT", {});
                } catch (...) {
                    reg.run("ROLLBACK", {});
                }
            });
        }
        if (!apagadas.empty())
            registrarUndo("Auto-organize folder", [this, apagadas]() {
                // Pais antes dos filhos (foram apagadas da mais funda pra mais rasa).
                for (auto it = apagadas.rbegin(); it != apagadas.rend(); ++it) {
                    projeto_->registro().run(
                        "INSERT OR IGNORE INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mapa_id, posicao_x, posicao_y, "
                        "ativo, cor_customizada, regra_organizacao, criado_em, atualizado_em) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        {matriz::db::Value::of(it->id), matriz::db::Value::of(it->projetoId),
                         it->paiNulo ? matriz::db::Value::null() : matriz::db::Value::of(it->paiId),
                         matriz::db::Value::of(it->nome), matriz::db::Value::of(it->ordem), matriz::db::Value::of(it->mapaId),
                         matriz::db::Value::of(it->x), matriz::db::Value::of(it->y), matriz::db::Value::of(it->ativo),
                         it->corNula ? matriz::db::Value::null() : matriz::db::Value::of(it->cor),
                         it->regraNula ? matriz::db::Value::null() : matriz::db::Value::of(it->regra),
                         matriz::db::Value::of(it->criadoEm), matriz::db::Value::of(it->atualizadoEm)});
                }
            });
        finalizarGrupoUndo();
    }
    return res;
}

void ProjetoAberto::desligarAutoOrganizacao(const std::string& pastaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || pastaId.empty()) return;
    const std::string mapaId = mapaIdDaPasta(pastaId);
    if (mapaId.empty() || mapaId == kMapaOriginal) return;
    auto& db = projeto_->registro();

    // A pasta e as subpastas AUTO dela ficam como estão: só perdem a regra e a marcação.
    std::map<std::string, std::string> regraAntes;  // id -> regra (só as que tinham)
    {
        auto st = db.prepare(
            "WITH RECURSIVE sub(id) AS (SELECT ? UNION ALL SELECT p.id FROM acervo_pasta p JOIN sub ON p.pasta_pai_id = sub.id "
            "WHERE p.regra_organizacao = '@auto') "
            "SELECT p.id, COALESCE(p.regra_organizacao, '') FROM acervo_pasta p WHERE p.id IN (SELECT id FROM sub)");
        st.bind(1, matriz::db::Value::of(pastaId));
        while (st.step())
            if (!st.columnText(1).empty()) regraAntes[st.columnText(0)] = st.columnText(1);
    }
    if (regraAntes.empty()) return;
    const std::string agora = matriz::model::agoraIso8601();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& [id, regra] : regraAntes)
            db.run("UPDATE acervo_pasta SET regra_organizacao = NULL, atualizado_em = ? WHERE id = ?",
                   {matriz::db::Value::of(agora), matriz::db::Value::of(id)});
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        return;
    }
    registrarUndo("Turn off auto-organize", [this, regraAntes]() {
        for (const auto& [id, regra] : regraAntes)
            projeto_->registro().run("UPDATE acervo_pasta SET regra_organizacao = ? WHERE id = ?",
                                     {matriz::db::Value::of(regra), matriz::db::Value::of(id)});
    });
}

namespace {
// Nº de code points UTF-8 (substr() do SQLite conta caracteres, não bytes).
int comprimentoUtf8(const std::string& s) {
    int n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// Arquivos (itens) que ainda estão em SEM PASTA no mapa e cuja origem fica em `dir` — só direto, sem subpastas
// (recursivo = true inclui as subpastas).
std::vector<std::string> itensSoltosDoDiretorio(matriz::db::Database& db, const std::string& mapaId,
                                                const std::string& dir, bool recursivo) {
    std::vector<std::string> out;
    const std::string prefixo = dir + "/";
    const int len = comprimentoUtf8(prefixo);
    auto st = db.prepare(
        std::string("SELECT a.item_id FROM arquivo a WHERE substr(a.caminho_absoluto_origem, 1, ?) = ? ") +
        (recursivo ? "" : "AND instr(substr(a.caminho_absoluto_origem, ?), '/') = 0 ") +
        "AND a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = a.item_id ORDER BY eh_master DESC, id LIMIT 1) "
        "AND a.item_id NOT IN (SELECT aip.item_id FROM acervo_item_pasta aip WHERE aip.mapa_id = ?)");
    int k = 1;
    st.bind(k++, matriz::db::Value::of(static_cast<int>(len)));
    st.bind(k++, matriz::db::Value::of(prefixo));
    if (!recursivo) st.bind(k++, matriz::db::Value::of(static_cast<int>(len + 1)));
    st.bind(k++, matriz::db::Value::of(mapaId));
    while (st.step()) out.push_back(st.columnText(0));
    return out;
}

struct PastaLinha {
    std::string id, pai, nome, proposta, origem;
};

std::vector<PastaLinha> carregarPastasDoMapa(matriz::db::Database& db, const std::string& projetoId, const std::string& mapaId) {
    std::vector<PastaLinha> v;
    auto st = db.prepare("SELECT id, COALESCE(pasta_pai_id, ''), nome, COALESCE(proposta_id, ''), COALESCE(origem_caminho, '') "
                         "FROM acervo_pasta WHERE projeto_id = ? AND mapa_id = ? ORDER BY ordem, criado_em");
    st.bind(1, matriz::db::Value::of(projetoId));
    st.bind(2, matriz::db::Value::of(mapaId));
    while (st.step()) v.push_back({st.columnText(0), st.columnText(1), st.columnText(2), st.columnText(3), st.columnText(4)});
    return v;
}

juce::String caminhoDaPastaNaLista(const std::vector<PastaLinha>& v, const std::string& id) {
    std::map<std::string, const PastaLinha*> por;
    for (const auto& p : v) por[p.id] = &p;
    juce::StringArray partes;
    std::string cur = id;
    for (int guard = 0; guard < 200 && !cur.empty(); ++guard) {
        auto it = por.find(cur);
        if (it == por.end()) break;
        partes.insert(0, juce::String::fromUTF8(it->second->nome.c_str()));
        cur = it->second->pai;
    }
    return partes.joinIntoString(" / ");
}
}  // namespace

void ProjetoAberto::registrarPastasIngeridas(const std::vector<std::string>& caminhos) {
    if (somenteLeitura_ || !projeto_ || caminhos.empty()) return;
    try {
        const std::string agora = matriz::model::agoraIso8601();
        for (const auto& c : caminhos) {
            const std::string norm = juce::File(juce::String::fromUTF8(c.c_str())).getFullPathName().toStdString();
            if (norm.empty()) continue;
            projeto_->registro().run("INSERT INTO ingest_pasta_raiz (id, projeto_id, caminho, criado_em) VALUES (?, ?, ?, ?)",
                                     {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(projeto_->projetoId()),
                                      matriz::db::Value::of(norm), matriz::db::Value::of(agora)});
        }
    } catch (...) {}
}

int ProjetoAberto::gerarPropostasDeNovasPastas(const std::string& mapaId) {
    if (somenteLeitura_ || !projeto_ || mapaId.empty() || mapaId == kMapaOriginal || mapaId == mapaDoMainId()) return 0;
    using matriz::db::Value;
    auto& db = projeto_->registro();
    struct Raiz { std::string id, caminho; bool velha; };
    std::vector<Raiz> raizes;
    try {
        auto st = db.prepare(
            "SELECT r.id, r.caminho, julianday(r.criado_em) < julianday('now', '-2 day') FROM ingest_pasta_raiz r "
            "WHERE r.projeto_id = ? AND r.criado_em >= (SELECT criado_em FROM folder_map WHERE id = ?) "
            "AND NOT EXISTS (SELECT 1 FROM ingest_pasta_raiz_mapa m WHERE m.raiz_id = r.id AND m.mapa_id = ?) "
            "ORDER BY r.criado_em");
        st.bind(1, Value::of(projeto_->projetoId()));
        st.bind(2, Value::of(mapaId));
        st.bind(3, Value::of(mapaId));
        while (st.step()) raizes.push_back({st.columnText(0), st.columnText(1), st.columnInt(2) != 0});
    } catch (...) { return 0; }
    if (raizes.empty()) return 0;

    static const char* kCores[] = {"ffe11d48", "ff0ea5e9", "fff59e0b", "ff10b981", "ff8b5cf6", "ffec4899"};
    int criadas = 0;
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    for (const auto& raiz : raizes) {
        try {
            // Mesma pasta ingerida de novo (cada ingest grava uma linha em ingest_pasta_raiz): se este mapa já tem uma
            // proposta ABERTA para esse caminho, não cria a gêmea — ela ficaria vazia depois de USE TREE/MERGE na outra.
            {
                auto aberta = db.prepare("SELECT 1 FROM acervo_pasta WHERE projeto_id = ? AND mapa_id = ? "
                                         "AND proposta_id = id AND origem_caminho = ? LIMIT 1");
                aberta.bind(1, Value::of(projeto_->projetoId()));
                aberta.bind(2, Value::of(mapaId));
                aberta.bind(3, Value::of(raiz.caminho));
                if (aberta.step()) {
                    db.run("INSERT OR IGNORE INTO ingest_pasta_raiz_mapa (raiz_id, mapa_id) VALUES (?, ?)", {Value::of(raiz.id), Value::of(mapaId)});
                    continue;
                }
            }
            const auto itens = itensSoltosDoDiretorio(db, mapaId, raiz.caminho, true);
            const std::string agora = matriz::model::agoraIso8601();
            if (itens.empty()) {
                // Ingestão ainda não terminou (ou só trouxe duplicatas): tenta de novo; raiz antiga é dada como tratada.
                if (raiz.velha)
                    db.run("INSERT OR IGNORE INTO ingest_pasta_raiz_mapa (raiz_id, mapa_id) VALUES (?, ?)", {Value::of(raiz.id), Value::of(mapaId)});
                continue;
            }
            // Diretórios que contêm arquivos + ancestrais até a raiz.
            std::set<std::string> dirs{raiz.caminho};
            {
                const std::string prefixo = raiz.caminho + "/";
                const int len = comprimentoUtf8(prefixo);
                auto st = db.prepare("SELECT DISTINCT a.caminho_absoluto_origem FROM arquivo a WHERE substr(a.caminho_absoluto_origem, 1, ?) = ?");
                st.bind(1, Value::of(static_cast<int>(len)));
                st.bind(2, Value::of(prefixo));
                while (st.step()) {
                    juce::File d = juce::File(juce::String::fromUTF8(st.columnText(0).c_str())).getParentDirectory();
                    const juce::File r(juce::String::fromUTF8(raiz.caminho.c_str()));
                    for (int guard = 0; guard < 100 && d != r && d.getFullPathName().length() > r.getFullPathName().length(); ++guard) {
                        dirs.insert(d.getFullPathName().toStdString());
                        d = d.getParentDirectory();
                    }
                }
            }
            std::vector<std::string> ordenados(dirs.begin(), dirs.end());
            std::sort(ordenados.begin(), ordenados.end(), [](const std::string& a, const std::string& b) {
                const auto pa = std::count(a.begin(), a.end(), '/'), pb = std::count(b.begin(), b.end(), '/');
                return pa != pb ? pa < pb : a < b;
            });

            db.exec("BEGIN IMMEDIATE");
            std::map<std::string, std::string> idPorDir;
            const std::string raizPastaId = matriz::model::novoUuid();
            const std::string cor = kCores[(criadas + std::count(raiz.caminho.begin(), raiz.caminho.end(), '/')) % 6];
            int ordemRaiz = 0;
            {
                auto st = db.prepare("SELECT COALESCE(MAX(ordem), -1) + 1 FROM acervo_pasta WHERE pasta_pai_id IS NULL AND projeto_id = ? AND mapa_id = ?");
                st.bind(1, Value::of(projeto_->projetoId()));
                st.bind(2, Value::of(mapaId));
                if (st.step()) ordemRaiz = st.columnInt(0);
            }
            int ordem = 0;
            for (const auto& dir : ordenados) {
                const bool ehRaiz = dir == raiz.caminho;
                const std::string id = ehRaiz ? raizPastaId : matriz::model::novoUuid();
                std::string pai;
                if (!ehRaiz) {
                    const std::string paiDir = juce::File(juce::String::fromUTF8(dir.c_str())).getParentDirectory().getFullPathName().toStdString();
                    if (auto it = idPorDir.find(paiDir); it != idPorDir.end()) pai = it->second;
                    else pai = raizPastaId;
                }
                const std::string nome = juce::File(juce::String::fromUTF8(dir.c_str())).getFileName().toStdString();
                db.run("INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, mapa_id, posicao_x, posicao_y, ativo, "
                       "cor_customizada, proposta_id, origem_caminho, criado_em, atualizado_em) "
                       "VALUES (?, ?, ?, ?, ?, ?, 0, 0, 1, ?, ?, ?, ?, ?)",
                       {Value::of(id), Value::of(projeto_->projetoId()), pai.empty() ? Value::null() : Value::of(pai),
                        Value::of(nome), Value::of(ehRaiz ? ordemRaiz : ordem++), Value::of(mapaId), Value::of(cor),
                        Value::of(raizPastaId), Value::of(dir), Value::of(agora), Value::of(agora)});
                idPorDir[dir] = id;
            }
            db.run("INSERT OR IGNORE INTO ingest_pasta_raiz_mapa (raiz_id, mapa_id) VALUES (?, ?)", {Value::of(raiz.id), Value::of(mapaId)});
            db.exec("COMMIT");
            ++criadas;
        } catch (...) {
            try { db.exec("ROLLBACK"); } catch (...) {}
        }
    }
    return criadas;
}

std::vector<ProjetoAberto::PropostaPasta> ProjetoAberto::listarPropostas(const std::string& mapaId) const {
    std::vector<PropostaPasta> out;
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return out;
    try {
        auto& db = projeto_->registro();
        std::vector<std::tuple<std::string, std::string, std::string>> raizes;
        {
            auto st = db.prepare("SELECT id, nome, COALESCE(origem_caminho, '') FROM acervo_pasta "
                                 "WHERE projeto_id = ? AND mapa_id = ? AND proposta_id = id ORDER BY criado_em, nome");
            st.bind(1, matriz::db::Value::of(projeto_->projetoId()));
            st.bind(2, matriz::db::Value::of(mapaId));
            while (st.step()) raizes.emplace_back(st.columnText(0), st.columnText(1), st.columnText(2));
        }
        for (const auto& [id, nome, origem] : raizes) {
            PropostaPasta p;
            p.raizId = id;
            p.nome = juce::String::fromUTF8(nome.c_str());
            p.arquivos = origem.empty() ? 0 : static_cast<int>(itensSoltosDoDiretorio(db, mapaId, origem, true).size());
            out.push_back(std::move(p));
        }
    } catch (...) {}
    return out;
}

namespace {
std::string mapaDaPasta(matriz::db::Database& db, const std::string& pastaId) {
    auto st = db.prepare("SELECT COALESCE(mapa_id, '') FROM acervo_pasta WHERE id = ?");
    st.bind(1, matriz::db::Value::of(pastaId));
    return st.step() ? st.columnText(0) : std::string();
}

int colocarItensDoDiretorio(matriz::db::Database& db, const std::string& mapaId, const std::string& pastaId,
                            const std::string& dir, const std::string& agora) {
    if (dir.empty()) return 0;
    int n = 0;
    for (const auto& item : itensSoltosDoDiretorio(db, mapaId, dir, false)) {
        db.run("INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, mapa_id, criado_em) VALUES (?, ?, ?, ?, ?)",
               {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(item), matriz::db::Value::of(pastaId),
                matriz::db::Value::of(mapaId), matriz::db::Value::of(agora)});
        ++n;
    }
    return n;
}

void descendentes(const std::vector<PastaLinha>& v, const std::string& id, std::vector<const PastaLinha*>& out) {
    for (const auto& p : v)
        if (p.pai == id) { out.push_back(&p); descendentes(v, p.id, out); }
}
}  // namespace

void ProjetoAberto::usarArvoreProposta(const std::string& raizId) {
    mesclarProposta(raizId, {}, {});
}

void ProjetoAberto::descartarProposta(const std::string& raizId) {
    if (somenteLeitura_ || !projeto_ || raizId.empty()) return;
    try {
        registrarUndoSnapshotMapa("Discard Proposed Tree", mapaIdDaPasta(raizId));
        projeto_->registro().run("DELETE FROM acervo_pasta WHERE proposta_id = ?", {matriz::db::Value::of(raizId)});
    } catch (...) {}
}

std::vector<ProjetoAberto::PastaDoMapa> ProjetoAberto::destinosDeMescla(const std::string& raizId) const {
    std::vector<PastaDoMapa> out;
    if (!projeto_) return out;
    try {
        auto& db = projeto_->registro();
        const std::string mapaId = mapaDaPasta(db, raizId);
        const auto v = carregarPastasDoMapa(db, projeto_->projetoId(), mapaId);
        juce::String nomeRaiz;
        for (const auto& p : v) if (p.id == raizId) nomeRaiz = juce::String::fromUTF8(p.nome.c_str());
        for (const auto& p : v) {
            if (!p.proposta.empty()) continue;
            PastaDoMapa d;
            d.id = p.id;
            d.caminho = caminhoDaPastaNaLista(v, p.id);
            d.mesmoNome = juce::String::fromUTF8(p.nome.c_str()).equalsIgnoreCase(nomeRaiz);
            out.push_back(std::move(d));
        }
        std::stable_sort(out.begin(), out.end(), [](const PastaDoMapa& a, const PastaDoMapa& b) {
            if (a.mesmoNome != b.mesmoNome) return a.mesmoNome;
            return a.caminho.compareIgnoreCase(b.caminho) < 0;
        });
    } catch (...) {}
    return out;
}

std::vector<ProjetoAberto::LinhaMescla> ProjetoAberto::planoDeMescla(const std::string& raizId, const std::string& alvoId,
                                                                    const std::map<std::string, bool>& decisoes) const {
    std::vector<LinhaMescla> out;
    if (!projeto_ || alvoId.empty()) return out;
    try {
        auto& db = projeto_->registro();
        const std::string mapaId = mapaDaPasta(db, raizId);
        const auto v = carregarPastasDoMapa(db, projeto_->projetoId(), mapaId);
        std::function<void(const std::string&, const std::string&)> rec = [&](const std::string& propId, const std::string& alvo) {
            for (const auto& c : v) {
                if (c.pai != propId) continue;
                const PastaLinha* par = nullptr;
                for (const auto& x : v)
                    if (x.pai == alvo && x.proposta.empty() &&
                        juce::String::fromUTF8(x.nome.c_str()).equalsIgnoreCase(juce::String::fromUTF8(c.nome.c_str()))) { par = &x; break; }
                if (!par) continue;
                LinhaMescla l;
                l.propostaId = c.id;
                l.caminhoProposta = caminhoDaPastaNaLista(v, c.id);
                l.alvoId = par->id;
                l.caminhoAlvo = caminhoDaPastaNaLista(v, par->id);
                l.arquivos = c.origem.empty() ? 0 : static_cast<int>(itensSoltosDoDiretorio(db, mapaId, c.origem, true).size());
                auto it = decisoes.find(c.id);
                l.mesclar = it == decisoes.end() ? true : it->second;
                out.push_back(l);
                if (l.mesclar) rec(c.id, par->id);
            }
        };
        rec(raizId, alvoId);
    } catch (...) {}
    return out;
}

int ProjetoAberto::mesclarProposta(const std::string& raizId, const std::string& alvoId, const std::map<std::string, bool>& decisoes) {
    if (somenteLeitura_ || !projeto_ || raizId.empty()) return -1;
    using matriz::db::Value;
    auto& db = projeto_->registro();
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    try {
        const std::string mapaId = mapaDaPasta(db, raizId);
        if (mapaId.empty()) return -1;
        registrarUndoSnapshotMapa(alvoId.empty() ? "Use Proposed Tree" : "Merge Proposed Tree", mapaId);
        const auto v = carregarPastasDoMapa(db, projeto_->projetoId(), mapaId);
        const std::string agora = matriz::model::agoraIso8601();
        int colocados = 0;
        db.exec("BEGIN IMMEDIATE");

        // Aceita uma subárvore como está: arquivos nas pastas dela, marca de proposta e cor saem.
        auto aceitar = [&](const std::string& id) {
            std::vector<const PastaLinha*> todas;
            for (const auto& p : v) if (p.id == id) todas.push_back(&p);
            descendentes(v, id, todas);
            for (const auto* p : todas) {
                colocados += colocarItensDoDiretorio(db, mapaId, p->id, p->origem, agora);
                db.run("UPDATE acervo_pasta SET proposta_id = NULL, cor_customizada = NULL WHERE id = ?", {Value::of(p->id)});
            }
        };

        if (alvoId.empty()) {
            aceitar(raizId);
        } else {
            std::function<void(const std::string&, const std::string&)> mesclar = [&](const std::string& propId, const std::string& alvo) {
                const PastaLinha* prop = nullptr;
                for (const auto& p : v) if (p.id == propId) prop = &p;
                if (prop) colocados += colocarItensDoDiretorio(db, mapaId, alvo, prop->origem, agora);
                for (const auto& c : v) {
                    if (c.pai != propId) continue;
                    const PastaLinha* par = nullptr;
                    auto it = decisoes.find(c.id);
                    const bool querMesclar = it == decisoes.end() ? true : it->second;
                    if (querMesclar)
                        for (const auto& x : v)
                            if (x.pai == alvo && x.proposta.empty() &&
                                juce::String::fromUTF8(x.nome.c_str()).equalsIgnoreCase(juce::String::fromUTF8(c.nome.c_str()))) { par = &x; break; }
                    if (par) {
                        mesclar(c.id, par->id);
                    } else {
                        db.run("UPDATE acervo_pasta SET pasta_pai_id = ? WHERE id = ?", {Value::of(alvo), Value::of(c.id)});
                        aceitar(c.id);
                    }
                }
                db.run("DELETE FROM acervo_pasta WHERE id = ?", {Value::of(propId)});
            };
            mesclar(raizId, alvoId);
        }
        db.exec("COMMIT");
        return colocados;
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        return -1;
    }
}

bool ProjetoAberto::podeMesclarPastas(const std::string& origemId, const std::string& alvoId) const {
    if (somenteLeitura_ || !projeto_ || origemId.empty() || alvoId.empty() || origemId == alvoId) return false;
    try {
        auto& db = projeto_->registro();
        const std::string mapaId = mapaDaPasta(db, origemId);
        if (mapaId.empty() || mapaId == kMapaOriginal || mapaId != mapaDaPasta(db, alvoId)) return false;
        const auto v = carregarPastasDoMapa(db, projeto_->projetoId(), mapaId);
        std::vector<const PastaLinha*> sub;
        descendentes(v, origemId, sub);
        for (const auto* p : sub) if (p->id == alvoId) return false;  // alvo dentro da origem
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<ProjetoAberto::LinhaMescla> ProjetoAberto::planoDeMesclaPastas(const std::string& origemId, const std::string& alvoId,
                                                                          const std::map<std::string, bool>& decisoes) const {
    std::vector<LinhaMescla> out;
    if (!projeto_ || !podeMesclarPastas(origemId, alvoId)) return out;
    try {
        auto& db = projeto_->registro();
        const std::string mapaId = mapaDaPasta(db, origemId);
        const auto v = carregarPastasDoMapa(db, projeto_->projetoId(), mapaId);
        auto itensDireto = [&](const std::string& pastaId) {
            auto st = db.prepare("SELECT COUNT(*) FROM acervo_item_pasta WHERE pasta_id = ? AND mapa_id = ?");
            st.bind(1, matriz::db::Value::of(pastaId));
            st.bind(2, matriz::db::Value::of(mapaId));
            return st.step() ? st.columnInt(0) : 0;
        };
        std::function<void(const std::string&, const std::string&)> rec = [&](const std::string& orig, const std::string& alvo) {
            for (const auto& c : v) {
                if (c.pai != orig) continue;
                const PastaLinha* par = nullptr;
                for (const auto& x : v)
                    if (x.pai == alvo && x.id != c.id &&
                        juce::String::fromUTF8(x.nome.c_str()).equalsIgnoreCase(juce::String::fromUTF8(c.nome.c_str()))) { par = &x; break; }
                if (!par) continue;
                LinhaMescla l;
                l.propostaId = c.id;
                l.caminhoProposta = caminhoDaPastaNaLista(v, c.id);
                l.alvoId = par->id;
                l.caminhoAlvo = caminhoDaPastaNaLista(v, par->id);
                l.arquivos = itensDireto(c.id);
                auto it = decisoes.find(c.id);
                l.mesclar = it == decisoes.end() ? true : it->second;
                out.push_back(l);
                if (l.mesclar) rec(c.id, par->id);
            }
        };
        rec(origemId, alvoId);
    } catch (...) {}
    return out;
}

int ProjetoAberto::mesclarPastas(const std::string& origemId, const std::string& alvoId, const std::map<std::string, bool>& decisoes) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return -1; }
    if (!projeto_ || !podeMesclarPastas(origemId, alvoId)) return -1;
    if (pastaTemArquivosNoMain(origemId) || pastaTemArquivosNoMain(alvoId)) {
        avisarMapaTravado(matriz::i18n::t("mapa_main.pasta_no_main"));
        return -1;
    }
    using matriz::db::Value;
    auto& db = projeto_->registro();
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    try {
        const std::string mapaId = mapaDaPasta(db, origemId);
        registrarUndoSnapshotMapa("Merge Folders", mapaId);
        const auto v = carregarPastasDoMapa(db, projeto_->projetoId(), mapaId);
        int movidos = 0;
        db.exec("BEGIN IMMEDIATE");
        std::function<void(const std::string&, const std::string&)> mesclar = [&](const std::string& orig, const std::string& alvo) {
            {
                auto st = db.prepare("SELECT COUNT(*) FROM acervo_item_pasta WHERE pasta_id = ? AND mapa_id = ?");
                st.bind(1, Value::of(orig));
                st.bind(2, Value::of(mapaId));
                if (st.step()) movidos += st.columnInt(0);
            }
            // O item que já está no alvo fica como está (UPDATE OR IGNORE); o resto da origem é descartado.
            db.run("UPDATE OR IGNORE acervo_item_pasta SET pasta_id = ? WHERE pasta_id = ? AND mapa_id = ?",
                   {Value::of(alvo), Value::of(orig), Value::of(mapaId)});
            db.run("DELETE FROM acervo_item_pasta WHERE pasta_id = ? AND mapa_id = ?", {Value::of(orig), Value::of(mapaId)});
            for (const auto& c : v) {
                if (c.pai != orig) continue;
                const PastaLinha* par = nullptr;
                auto it = decisoes.find(c.id);
                const bool querMesclar = it == decisoes.end() ? true : it->second;
                if (querMesclar)
                    for (const auto& x : v)
                        if (x.pai == alvo && x.id != c.id &&
                            juce::String::fromUTF8(x.nome.c_str()).equalsIgnoreCase(juce::String::fromUTF8(c.nome.c_str()))) { par = &x; break; }
                if (par) mesclar(c.id, par->id);
                else db.run("UPDATE acervo_pasta SET pasta_pai_id = ? WHERE id = ?", {Value::of(alvo), Value::of(c.id)});
            }
            db.run("DELETE FROM acervo_pasta WHERE id = ?", {Value::of(orig)});
        };
        mesclar(origemId, alvoId);
        db.exec("COMMIT");
        return movidos;
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        return -1;
    }
}

int ProjetoAberto::organizarItensSoltosDasPastasComRegra(const std::string& mapaId) {
    if (somenteLeitura_ || !projeto_ || mapaId.empty() || mapaId == kMapaOriginal || organizandoAuto_) return 0;
    // Checagem barata: uma consulta só, e só pastas com regra E com itens diretos.
    std::vector<std::pair<std::string, std::string>> alvos;
    try {
        auto st = projeto_->registro().prepare(
            "SELECT p.id, p.regra_organizacao FROM acervo_pasta p WHERE p.projeto_id = ? AND p.mapa_id = ? "
            "AND p.regra_organizacao IS NOT NULL AND p.regra_organizacao <> '' AND p.regra_organizacao <> '@auto' "
            "AND EXISTS (SELECT 1 FROM acervo_item_pasta aip WHERE aip.pasta_id = p.id)");
        st.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        st.bind(2, matriz::db::Value::of(mapaId));
        while (st.step()) alvos.emplace_back(st.columnText(0), st.columnText(1));
    } catch (...) { return 0; }
    if (alvos.empty()) return 0;

    organizandoAuto_ = true;  // guarda contra recursão
    int organizadas = 0;
    for (const auto& [id, regra] : alvos) {
        const auto r = aplicarAutoOrganizacao(id, regra, true);
        if (r.status == StatusAutoOrg::Ok && (r.itensMovidos > 0 || r.pastasApagadas > 0)) ++organizadas;
    }
    organizandoAuto_ = false;
    return organizadas;
}

void ProjetoAberto::avisarMapaTravado(const juce::String& mensagem) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(matriz::i18n::t("mapa_main.titulo"))
                                     .withMessage(mensagem)
                                     .withButton(matriz::i18n::t("dialogo.ok")),
                                 juce::ModalCallbackFunction::create([](int) {}));
}

bool ProjetoAberto::renomearPastaAcervo(const std::string& pastaId, const std::string& novoNome) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_) return false;
    // MAIN EDIT MODE: renomear pasta do mapa do MAIN renomeia a pasta no disco e
    // reaponta os caminhos registrados, juntos (motor MainEdit).
    if (editandoMain_ && !mapaDoMainId().empty() && mapaIdDaPasta(pastaId) == mapaDoMainId()) {
        std::string nomeAntes;
        {
            auto st = projeto_->registro().prepare("SELECT nome FROM acervo_pasta WHERE id = ?");
            st.bind(1, matriz::db::Value::of(pastaId));
            if (st.step()) nomeAntes = st.columnText(0);
        }
        const bool registrarUndoDesta = !desfazendo_;
        auto aoTerminar = [this, pastaId, nomeAntes, registrarUndoDesta](const matriz::mainedit::Resultado& r) {
            if (!r.ok) avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str()));
            else if (registrarUndoDesta)
                registrarUndo("Rename Folder", [this, pastaId, nomeAntes]() { renomearPastaAcervo(pastaId, nomeAntes); });
            if (aoTerminarEdicaoPastaMain) aoTerminarEdicaoPastaMain();
        };
        if (contarArquivosDaPastaNoMain(pastaId) > kLimiteArquivosPastaSincrona) {
            // Muitos arquivos: background com progresso global (a UI não trava). Mesma transação única no banco.
            auto ctx = contextoDoMain();
            const auto novo = juce::String::fromUTF8(novoNome.c_str());
            executarEdicaoMain("Renaming folder in MAIN", [ctx, pastaId, novo] {
                return matriz::mainedit::renomearPasta(ctx, pastaId, novo);
            }, aoTerminar);
            return true;
        }
        auto r = matriz::mainedit::renomearPasta(contextoDoMain(), pastaId, juce::String::fromUTF8(novoNome.c_str()));
        tocarAtividadeMain();
        if (!r.ok) { avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str())); return false; }
        if (registrarUndoDesta)
            registrarUndo("Rename Folder", [this, pastaId, nomeAntes]() { renomearPastaAcervo(pastaId, nomeAntes); });
        return true;
    }
    if (pastaTemArquivosNoMain(pastaId)) {
        avisarMapaTravado(matriz::i18n::t("mapa_main.pasta_no_main"));
        return false;
    }
    if (!desfazendo_) {
        auto stmt = projeto_->registro().prepare("SELECT nome FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(pastaId));
        std::string oldNome = stmt.step() ? stmt.columnText(0) : "";
        registrarUndo("Rename Folder", [this, pastaId, oldNome]() {
            renomearPastaAcervo(pastaId, oldNome);
        });
    }
    projeto_->registro().run("UPDATE acervo_pasta SET nome = ?, atualizado_em = ? WHERE id = ?",
                              {matriz::db::Value::of(novoNome), matriz::db::Value::of(matriz::model::agoraIso8601()),
                               matriz::db::Value::of(pastaId)});
    return true;
}

bool ProjetoAberto::apagarPastaAcervo(const std::string& pastaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_) return false;
    if (pastaTemArquivosNoMain(pastaId)) {
        avisarMapaTravado(matriz::i18n::t("mapa_main.pasta_no_main"));
        return false;
    }
    if (ehPastaTrash(pastaId)) return false;
    registrarUndoSubarvoreApagada(pastaId);
    projeto_->registro().run("DELETE FROM acervo_pasta WHERE id = ?", {matriz::db::Value::of(pastaId)});
    return true;
}

static void garantirTabelaLixeira(matriz::db::Database& db) {
    db.run(
        "CREATE TABLE IF NOT EXISTS lixeira_item ("
        "  item_id TEXT NOT NULL,"
        "  mapa_id TEXT NOT NULL,"
        "  pasta_origem_id TEXT,"
        "  nome_pasta_origem TEXT,"
        "  deletado_em TEXT NOT NULL,"
        "  PRIMARY KEY (item_id, mapa_id)"
        ")",
        {}
    );
}

std::string ProjetoAberto::obterOuCriarPastaTrash(const std::string& mapaId) {
    if (!projeto_) return {};
    auto& db = projeto_->registro();
    garantirTabelaLixeira(db);
    auto st = db.prepare("SELECT id FROM acervo_pasta WHERE mapa_id = ? AND nome = '_trash' LIMIT 1");
    st.bind(1, matriz::db::Value::of(mapaId));
    if (st.step()) return st.columnText(0);

    std::string id = juce::Uuid().toString().toStdString();
    db.run(
        "INSERT INTO acervo_pasta (id, projeto_id, nome, pasta_pai_id, mapa_id, ativo, criado_em, atualizado_em) "
        "VALUES (?, ?, '_trash', NULL, ?, 0, datetime('now'), datetime('now'))",
        {matriz::db::Value::of(id), matriz::db::Value::of(projeto_->projetoId()), matriz::db::Value::of(mapaId)}
    );
    return id;
}

bool ProjetoAberto::ehPastaTrash(const std::string& pastaId) const {
    if (!projeto_ || pastaId.empty()) return false;
    auto& db = projeto_->registro();
    auto st = db.prepare("SELECT nome FROM acervo_pasta WHERE id = ?");
    st.bind(1, matriz::db::Value::of(pastaId));
    if (st.step()) return st.columnText(0) == "_trash";
    return false;
}

void ProjetoAberto::registrarUndoVinculosDePasta(const std::string& descricao, const std::vector<std::string>& itemIds,
                                                 const std::string& mapaId) {
    if (!projeto_ || itemIds.empty() || desfazendo_) return;
    auto& db = projeto_->registro();
    garantirTabelaLixeira(db);
    auto retrato = retratar({{"acervo_item_pasta", "item_id", false}, {"lixeira_item", "item_id", false}}, itemIds);
    std::set<std::string> pastasAntes;
    if (!mapaId.empty()) {
        auto st = db.prepare("SELECT id FROM acervo_pasta WHERE mapa_id = ?");
        st.bind(1, matriz::db::Value::of(mapaId));
        while (st.step()) pastasAntes.insert(st.columnText(0));
    }
    std::vector<ParteUndo> acoes;
    if (!mapaId.empty()) {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("mapa", juce::String::fromUTF8(mapaId.c_str()));
        juce::Array<juce::var> antes;
        for (const auto& id : pastasAntes) antes.add(juce::String::fromUTF8(id.c_str()));
        o->setProperty("antes", antes);
        ParteUndo a;
        a.tipo = ParteUndo::Tipo::Acao;
        a.tabela = "pastas_novas";  // as pastas que a operação criou (o _trash, a recriada pelo nome) somem
        a.onde = juce::JSON::toString(juce::var(o.get()), true).toStdString();
        acoes.push_back(std::move(a));
    }
    registrarUndoDeRetrato(descricao, std::move(retrato), itemIds, std::move(acoes));
}

void ProjetoAberto::moverItensParaTrash(const std::vector<std::string>& idsPedidos, const std::string& mapaId, const std::string& nomePastaOrigem) {
    if (somenteLeitura_ || !projeto_ || idsPedidos.empty()) return;
    const auto itemIds = expandirMembrosDeNest(idsPedidos);
    registrarUndoVinculosDePasta(itemIds.size() == 1 ? "Move to Trash" : "Move " + std::to_string(itemIds.size()) + " Items to Trash",
                                 itemIds, mapaId);  // antes do _trash ser criado: ele também some no Undo
    const std::string trashId = obterOuCriarPastaTrash(mapaId);
    if (trashId.empty()) return;
    auto& db = projeto_->registro();
    garantirTabelaLixeira(db);

    db.run("BEGIN IMMEDIATE TRANSACTION", {});
    try {
        std::string agora = matriz::model::agoraIso8601();
        for (const auto& itemId : itemIds) {
            std::string pastaOrigId;
            std::string nomeOrig = nomePastaOrigem;
            if (nomeOrig.empty()) {
                auto stP = db.prepare("SELECT ap.id, ap.nome FROM acervo_item_pasta aip "
                                      "JOIN acervo_pasta ap ON ap.id = aip.pasta_id "
                                      "WHERE aip.item_id = ? AND aip.mapa_id = ? LIMIT 1");
                stP.bind(1, matriz::db::Value::of(itemId));
                stP.bind(2, matriz::db::Value::of(mapaId));
                if (stP.step()) {
                    pastaOrigId = stP.columnText(0);
                    nomeOrig = stP.columnText(1);
                }
            }
            if (nomeOrig == "_trash") nomeOrig.clear();

            db.run("INSERT OR REPLACE INTO lixeira_item (item_id, mapa_id, pasta_origem_id, nome_pasta_origem, deletado_em) "
                   "VALUES (?, ?, ?, ?, ?)",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId),
                    matriz::db::Value::of(pastaOrigId), matriz::db::Value::of(nomeOrig),
                    matriz::db::Value::of(agora)});

            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
            inserirItemPastaInterno(itemId, trashId, agora);
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

void ProjetoAberto::extrairItensParaCanvas(const std::vector<std::string>& idsPedidos, const std::string& mapaId) {
    if (somenteLeitura_ || !projeto_ || idsPedidos.empty()) return;
    const auto itemIds = expandirMembrosDeNest(idsPedidos);
    registrarUndoVinculosDePasta(itemIds.size() == 1 ? "Extract from Trash" : "Extract " + std::to_string(itemIds.size()) + " Items from Trash",
                                 itemIds, mapaId);
    auto& db = projeto_->registro();
    garantirTabelaLixeira(db);

    db.run("BEGIN IMMEDIATE TRANSACTION", {});
    try {
        for (const auto& itemId : itemIds) {
            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
            db.run("DELETE FROM lixeira_item WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

void ProjetoAberto::restaurarItensDaTrash(const std::vector<std::string>& idsPedidos, const std::string& mapaId) {
    if (somenteLeitura_ || !projeto_ || idsPedidos.empty()) return;
    const auto itemIds = expandirMembrosDeNest(idsPedidos);
    registrarUndoVinculosDePasta(itemIds.size() == 1 ? "Restore from Trash" : "Restore " + std::to_string(itemIds.size()) + " Items from Trash",
                                 itemIds, mapaId);  // a pasta recriada pelo nome também some no Undo
    auto& db = projeto_->registro();
    garantirTabelaLixeira(db);

    db.run("BEGIN IMMEDIATE TRANSACTION", {});
    try {
        std::string agora = matriz::model::agoraIso8601();
        for (const auto& itemId : itemIds) {
            std::string pastaOrigId, nomePastaOrig;
            auto st = db.prepare("SELECT pasta_origem_id, nome_pasta_origem FROM lixeira_item WHERE item_id = ? AND mapa_id = ?");
            st.bind(1, matriz::db::Value::of(itemId));
            st.bind(2, matriz::db::Value::of(mapaId));
            if (st.step()) {
                if (!st.columnIsNull(0)) pastaOrigId = st.columnText(0);
                if (!st.columnIsNull(1)) nomePastaOrig = st.columnText(1);
            }

            std::string pastaDestinoId;
            // Se a pasta original ainda existe no mapa e não é a lixeira
            if (!pastaOrigId.empty()) {
                auto stChk = db.prepare("SELECT id FROM acervo_pasta WHERE id = ? AND mapa_id = ? AND nome <> '_trash'");
                stChk.bind(1, matriz::db::Value::of(pastaOrigId));
                stChk.bind(2, matriz::db::Value::of(mapaId));
                if (stChk.step()) pastaDestinoId = pastaOrigId;
            }

            // Se a pasta original foi deletada do canvas, recria pasta com o mesmo nome
            if (pastaDestinoId.empty() && !nomePastaOrig.empty()) {
                auto stNome = db.prepare("SELECT id FROM acervo_pasta WHERE mapa_id = ? AND lower(nome) = lower(?) AND nome <> '_trash' LIMIT 1");
                stNome.bind(1, matriz::db::Value::of(mapaId));
                stNome.bind(2, matriz::db::Value::of(nomePastaOrig));
                if (stNome.step()) {
                    pastaDestinoId = stNome.columnText(0);
                } else {
                    pastaDestinoId = juce::Uuid().toString().toStdString();
                    db.run("INSERT INTO acervo_pasta (id, projeto_id, nome, pasta_pai_id, mapa_id, ativo, criado_em, atualizado_em) "
                           "VALUES (?, ?, ?, NULL, ?, 1, ?, ?)",
                           {matriz::db::Value::of(pastaDestinoId), matriz::db::Value::of(projeto_->projetoId()),
                            matriz::db::Value::of(nomePastaOrig), matriz::db::Value::of(mapaId),
                            matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
                }
            }

            if (!pastaDestinoId.empty()) {
                db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                       {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
                inserirItemPastaInterno(itemId, pastaDestinoId, agora);
            } else {
                db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                       {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
            }

            db.run("DELETE FROM lixeira_item WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

std::vector<std::string> ProjetoAberto::itensNaPastaTrash(const std::string& mapaId) const {
    std::vector<std::string> out;
    if (!projeto_) return out;
    auto& db = projeto_->registro();
    auto st = db.prepare(
        "SELECT aip.item_id FROM acervo_item_pasta aip "
        "JOIN acervo_pasta ap ON ap.id = aip.pasta_id "
        "WHERE aip.mapa_id = ? AND ap.nome = '_trash'");
    st.bind(1, matriz::db::Value::of(mapaId));
    while (st.step()) out.push_back(st.columnText(0));
    return out;
}

bool ProjetoAberto::moverPastaAcervo(const std::string& pastaId, const std::optional<std::string>& novaPastaPaiId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_) return false;
    if (editandoMain_ && !mapaDoMainId().empty() && mapaIdDaPasta(pastaId) == mapaDoMainId()) {
        std::optional<std::string> paiAntes;
        {
            auto st = projeto_->registro().prepare("SELECT pasta_pai_id FROM acervo_pasta WHERE id = ?");
            st.bind(1, matriz::db::Value::of(pastaId));
            if (st.step() && !st.columnIsNull(0)) paiAntes = st.columnText(0);
        }
        // Mesmo pai = nada a mover (arrastar sem soltar em outra pasta).
        if (paiAntes.value_or("") == novaPastaPaiId.value_or("")) return true;
        const bool registrarUndoDesta = !desfazendo_;
        const std::string novoPai = novaPastaPaiId.value_or("");
        auto aoTerminar = [this, pastaId, paiAntes, registrarUndoDesta](const matriz::mainedit::Resultado& r) {
            if (!r.ok) avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str()));
            else if (registrarUndoDesta)
                registrarUndo("Move Folder", [this, pastaId, paiAntes]() { moverPastaAcervo(pastaId, paiAntes); });
            if (aoTerminarEdicaoPastaMain) aoTerminarEdicaoPastaMain();
        };
        if (contarArquivosDaPastaNoMain(pastaId) > kLimiteArquivosPastaSincrona) {
            auto ctx = contextoDoMain();
            executarEdicaoMain("Moving folder in MAIN", [ctx, pastaId, novoPai] {
                return matriz::mainedit::moverPasta(ctx, pastaId, novoPai);
            }, aoTerminar);
            return true;
        }
        auto r = matriz::mainedit::moverPasta(contextoDoMain(), pastaId, novoPai);
        tocarAtividadeMain();
        if (!r.ok) { avisarMapaTravado(juce::String::fromUTF8(r.erro.c_str())); return false; }
        if (registrarUndoDesta)
            registrarUndo("Move Folder", [this, pastaId, paiAntes]() { moverPastaAcervo(pastaId, paiAntes); });
        return true;
    }
    if (pastaTemArquivosNoMain(pastaId)) {
        avisarMapaTravado(matriz::i18n::t("mapa_main.pasta_no_main"));
        return false;
    }
    // Vale pra QUALQUER move de hierarquia (desconectar, reconectar via
    // socket-drag, futura reorganização) — não só o desconectar do item 7.
    // Mesmo padrão de renomearPastaAcervo: guardado por desfazendo_, então
    // desfazer() nunca registra a si mesmo de novo ao reverter.
    if (!desfazendo_) {
        std::optional<std::string> oldPaiId;
        auto stmt = projeto_->registro().prepare("SELECT pasta_pai_id FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(pastaId));
        if (stmt.step() && !stmt.columnIsNull(0)) oldPaiId = stmt.columnText(0);
        registrarUndo("Move Folder", [this, pastaId, oldPaiId]() {
            moverPastaAcervo(pastaId, oldPaiId);
        });
    }
    projeto_->registro().run(
        "UPDATE acervo_pasta SET pasta_pai_id = ?, atualizado_em = ? WHERE id = ?",
        {novaPastaPaiId ? matriz::db::Value::of(*novaPastaPaiId) : matriz::db::Value::null(),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
    return true;
}

void ProjetoAberto::atualizarEscalasPastasAcervo(const std::vector<std::pair<std::string, double>>& escalasPorPasta) {
    if (somenteLeitura_ || !projeto_ || escalasPorPasta.empty()) return;
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& [id, escala] : escalasPorPasta)
            db.run("UPDATE acervo_pasta SET escala_no = ? WHERE id = ?",
                   {escala == 1.0 ? matriz::db::Value::null() : matriz::db::Value::of(escala), matriz::db::Value::of(id)});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
}

double ProjetoAberto::fatorCardsDoMapa(const std::string& mapaId) const {
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return 0.0;
    try {
        auto st = leitura().prepare("SELECT fator_cards FROM folder_map WHERE id = ?");
        st.bind(1, matriz::db::Value::of(mapaId));
        if (st.step() && !st.columnIsNull(0)) return st.columnReal(0);
    } catch (...) {}
    return 0.0;
}

void ProjetoAberto::definirFatorCardsDoMapa(const std::string& mapaId, double fator) {
    if (somenteLeitura_ || !projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return;
    projeto_->registro().run("UPDATE folder_map SET fator_cards = ? WHERE id = ?",
                             {matriz::db::Value::of(fator), matriz::db::Value::of(mapaId)});
}

void ProjetoAberto::atualizarPosicaoPastaAcervo(const std::string& pastaId, int x, int y) {
    if (somenteLeitura_) return;
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE acervo_pasta SET posicao_x = ?, posicao_y = ?, atualizado_em = ? WHERE id = ?",
        {matriz::db::Value::of(x), matriz::db::Value::of(y),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::alternarAtivoPastaAcervo(const std::string& pastaId, bool ativo) {
    if (somenteLeitura_) return;
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE acervo_pasta SET ativo = ?, atualizado_em = ? WHERE id = ?",
        {matriz::db::Value::of(ativo ? 1 : 0),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::definirCorPastaAcervo(const std::string& pastaId, const juce::String& corArgbHex) {
    if (somenteLeitura_) return;
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE acervo_pasta SET cor_customizada = ?, atualizado_em = ? WHERE id = ?",
        {corArgbHex.isEmpty() ? matriz::db::Value::null() : matriz::db::Value::of(corArgbHex.toStdString()),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
}

juce::String ProjetoAberto::lerCorPastaAcervo(const std::string& pastaId) const {
    if (!projeto_) return {};
    try {
        auto stmt = leitura().prepare("SELECT cor_customizada FROM acervo_pasta WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(pastaId));
        if (stmt.step() && !stmt.columnIsNull(0)) return juce::String(stmt.columnText(0));
    } catch (...) {}
    return {};
}

std::vector<juce::String> ProjetoAberto::historicoCoresPasta() const {
    std::vector<juce::String> resultado;
    if (!projeto_) return resultado;
    try {
        auto stmt = leitura().prepare("SELECT historico_cores_pasta FROM projeto LIMIT 1");
        if (stmt.step() && !stmt.columnIsNull(0)) {
            juce::var arr = juce::JSON::parse(juce::String(stmt.columnText(0)));
            if (auto* a = arr.getArray()) {
                for (auto& v : *a) {
                    juce::String hex = v.toString();
                    if (hex.isNotEmpty()) resultado.push_back(hex);
                }
            }
        }
    } catch (...) {}
    return resultado;
}

void ProjetoAberto::definirHistoricoCoresPasta(const std::vector<juce::String>& coresHex) {
    if (somenteLeitura_) return;
    if (!projeto_) return;
    juce::Array<juce::var> arr;
    for (const auto& hex : coresHex) arr.add(hex);
    juce::String json = juce::JSON::toString(juce::var(arr), true);
    try {
        projeto_->registro().run("UPDATE projeto SET historico_cores_pasta = ?",
                                  {matriz::db::Value::of(json.toStdString())});
    } catch (...) {}
}

void ProjetoAberto::adicionarItensAPasta(const std::vector<std::string>& idsPedidos, const std::string& pastaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    const auto itemIds = expandirMembrosDeNest(idsPedidos);  // mover um nest move todos os arquivos dele juntos
    std::string mapaId = mapaIdDaPasta(pastaId);
    if (mapaId.empty()) return;
    if (!desfazendo_) {
        std::map<std::string, std::vector<std::string>> antigasPastas;
        for (const auto& id : itemIds) {
            // Só as pastas DESTE mapa — mover num mapa nunca deve mexer na
            // posição do item em outro mapa (Fase 1).
            auto stmtOld = projeto_->registro().prepare(
                "SELECT pasta_id FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?");
            stmtOld.bind(1, matriz::db::Value::of(id));
            stmtOld.bind(2, matriz::db::Value::of(mapaId));
            std::vector<std::string> pastas;
            while (stmtOld.step()) pastas.push_back(stmtOld.columnText(0));
            antigasPastas[id] = std::move(pastas);
        }
        registrarUndo("Move Items to Folder", [this, antigasPastas, mapaId]() {
            std::string agora = matriz::model::agoraIso8601();
            for (const auto& [id, pastas] : antigasPastas) {
                projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                                         {matriz::db::Value::of(id), matriz::db::Value::of(mapaId)});
                for (const auto& oldPasta : pastas) inserirItemPastaInterno(id, oldPasta, agora);
            }
        });
    }
    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (auto& itemId : itemIds) {
            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
            inserirItemPastaInterno(itemId, pastaId, agora);
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

namespace {
// Pasta onde o item está, na ordem de preferência dos mapas; vazio se o item não está em pasta nenhuma.
std::string pastaDoItemNosMapas(matriz::db::Database& db, const std::string& itemId, const std::vector<std::string>& mapas) {
    for (const auto& mapa : mapas) {
        auto st = db.prepare(
            "SELECT aip.pasta_id FROM acervo_item_pasta aip JOIN acervo_pasta ap ON ap.id = aip.pasta_id "
            "WHERE aip.item_id = ? AND ap.mapa_id = ? LIMIT 1");
        st.bind(1, matriz::db::Value::of(itemId));
        st.bind(2, matriz::db::Value::of(mapa));
        if (st.step()) return st.columnText(0);
    }
    auto st = db.prepare("SELECT pasta_id FROM acervo_item_pasta WHERE item_id = ? LIMIT 1");
    st.bind(1, matriz::db::Value::of(itemId));
    return st.step() ? st.columnText(0) : std::string();
}

std::vector<std::string> cadeiaDaPasta(matriz::db::Database& db, std::string pastaId) {
    std::vector<std::string> cadeia;
    for (int guarda = 0; !pastaId.empty() && guarda < 64; ++guarda) {
        auto st = db.prepare("SELECT nome, pasta_pai_id FROM acervo_pasta WHERE id = ?");
        st.bind(1, matriz::db::Value::of(pastaId));
        if (!st.step()) break;
        cadeia.push_back(st.columnText(0));
        pastaId = st.columnIsNull(1) ? std::string() : st.columnText(1);
    }
    return cadeia;
}
}  // namespace

std::vector<std::string> ProjetoAberto::cadeiaDePastasDoItem(const std::string& itemId) const {
    if (!projeto_ || itemId.empty()) return {};
    try {
        std::vector<std::string> mapas;
        const std::string doMain = mapaDoMainId();
        if (!doMain.empty()) mapas.push_back(doMain);
        const std::string ativo = mapaAtivoPadrao();
        if (!ativo.empty() && ativo != doMain) mapas.push_back(ativo);
        return cadeiaDaPasta(leitura(), pastaDoItemNosMapas(leitura(), itemId, mapas));
    } catch (...) {
        return {};
    }
}

ProjetoAberto::ListaPorPastaPrevia ProjetoAberto::previaListaPelasPastas(const std::vector<std::string>& itemIds, int nivel,
                                                                         bool soVazios) const {
    ListaPorPastaPrevia r;
    if (!projeto_ || nivel < 0) return r;
    std::vector<std::string> mapas;
    try {
        const std::string doMain = mapaDoMainId();
        if (!doMain.empty()) mapas.push_back(doMain);
        const std::string ativo = mapaAtivoPadrao();
        if (!ativo.empty() && ativo != doMain) mapas.push_back(ativo);
    } catch (...) {}
    std::map<std::string, std::vector<std::string>> cachePorPasta;  // milhares de itens costumam dividir poucas pastas
    for (const auto& id : itemIds) {
        try {
            const std::string pastaId = pastaDoItemNosMapas(leitura(), id, mapas);
            if (pastaId.empty()) { ++r.semPasta; continue; }
            auto it = cachePorPasta.find(pastaId);
            if (it == cachePorPasta.end()) it = cachePorPasta.emplace(pastaId, cadeiaDaPasta(leitura(), pastaId)).first;
            const auto& cadeia = it->second;
            if (static_cast<size_t>(nivel) >= cadeia.size()) { ++r.semNivel; continue; }
            const std::string& valor = cadeia[static_cast<size_t>(nivel)];
            const std::string atual = lerMetadado(id, "lista").value_or("");
            if (soVazios && !juce::String(atual).trim().isEmpty()) { ++r.jaPreenchidos; continue; }
            if (atual == valor) { ++r.jaIguais; continue; }
            r.itemEValor.emplace_back(id, valor);
            r.valoresDistintos.insert(valor);
        } catch (...) {}
    }
    return r;
}

void ProjetoAberto::aplicarListaPelasPastas(const std::vector<std::pair<std::string, std::string>>& itemEValor) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (itemEValor.empty()) return;
    std::map<std::string, std::vector<std::string>> porValor;
    for (const auto& [id, valor] : itemEValor) porValor[valor].push_back(id);
    iniciarGrupoUndo("Edit LIST (from Folder Map)");
    for (const auto& [valor, ids] : porValor) salvarMetadadoEmLote(ids, {{"lista", valor}});
    finalizarGrupoUndo();
}

void ProjetoAberto::adicionarItemAPastaSemRemoverOutras(const std::string& itemId, const std::string& pastaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemId.empty() || pastaId.empty()) return;
    inserirItemPastaInterno(itemId, pastaId, matriz::model::agoraIso8601());
}

std::optional<std::string> ProjetoAberto::localizarItemPorCodigo(const std::string& codigoAcervo) const {
    if (!projeto_ || codigoAcervo.empty()) return std::nullopt;
    auto stmt = leitura().prepare(
        "SELECT id FROM item WHERE projeto_id = ? AND codigo_acervo = ? LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(codigoAcervo));
    if (stmt.step()) return stmt.columnText(0);
    return std::nullopt;
}

std::string ProjetoAberto::agruparItensEmNovaPasta(const std::vector<std::string>& idsPedidos,
                                                    const std::string& mapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return {};
    const auto itemIds = expandirMembrosDeNest(idsPedidos);

    // Um passo de Undo: os vínculos dos itens voltam e a pasta nova some (o Undo de criarPastaAcervo vem no grupo).
    iniciarGrupoUndo("Group into New Folder");
    struct FimDoGrupo {
        ProjetoAberto* p;
        ~FimDoGrupo() { p->finalizarGrupoUndo(); }
    } fimDoGrupo{this};
    registrarUndoVinculosDePasta("Group into New Folder", itemIds, mapaId);
    std::string newFolderId = criarPastaAcervo("New Folder", std::nullopt, mapaId);

    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& itemId : itemIds) {
            // Só tira o item das pastas DESTE mapa — outros mapas não são
            // afetados (Fase 1: mapas totalmente independentes).
            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
            inserirItemPastaInterno(itemId, newFolderId, agora);
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }

    return newFolderId;
}

void ProjetoAberto::removerItensDoBackup(const std::vector<std::string>& idsPedidos, const std::string& mapaId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return;
    const auto itemIds = expandirMembrosDeNest(idsPedidos);  // tirar um nest da lista tira todos os arquivos dele
    if (!desfazendo_) {
        std::vector<std::pair<std::string, std::string>> anteriores;
        for (const auto& itemId : itemIds) {
            auto stmt = projeto_->registro().prepare(
                "SELECT pasta_id FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?");
            stmt.bind(1, matriz::db::Value::of(itemId));
            stmt.bind(2, matriz::db::Value::of(mapaId));
            if (stmt.step()) {
                anteriores.push_back({itemId, stmt.columnText(0)});
            }
        }
        registrarUndo("Exclude from Backup", [this, anteriores]() {
            restaurarItensParaBackup(anteriores);
        });
    }
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (auto& itemId : itemIds)
            db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                   {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

void ProjetoAberto::restaurarItensParaBackup(const std::vector<std::pair<std::string, std::string>>& itensPastas) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& [itemId, pastaId] : itensPastas)
            if (!pastaId.empty()) inserirItemPastaInterno(itemId, pastaId, agora);
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
}

namespace {
// Undo do REJECT (Intake): antes de apagar, as linhas do item e de tudo que
// referencia item ou arquivo (FK — lido do próprio esquema) vão pra tabelas
// TEMP da conexão, marcadas com um token; o undo as reinsere. TEMP = some ao
// fechar o projeto (a pilha de undo também não sobrevive).
struct TabelaLigada {
    std::string tabela, coluna, alvo;  // alvo: "item" ou "arquivo"
};
std::vector<TabelaLigada> tabelasLigadasAoItem(matriz::db::Database& db) {
    std::vector<TabelaLigada> out;
    auto st = db.prepare("SELECT m.name, f.\"from\", f.\"table\" FROM sqlite_master m, pragma_foreign_key_list(m.name) f "
                         "WHERE m.type = 'table' AND f.\"table\" IN ('item', 'arquivo')");
    while (st.step()) out.push_back({st.columnText(0), st.columnText(1), st.columnText(2)});
    return out;
}
std::string colunasDe(matriz::db::Database& db, const std::string& tabela) {
    std::string cols;
    auto st = db.prepare("SELECT name FROM pragma_table_info(?)");
    st.bind(1, matriz::db::Value::of(tabela));
    while (st.step()) cols += (cols.empty() ? "\"" : ", \"") + st.columnText(0) + "\"";
    return cols;
}
// tokenUndo marca as linhas guardadas (o Undo as restaura por ele); tokenIds seleciona, em temp.undo_ids, os itens
// DESTE lote. Na remoção síncrona são o mesmo token; na em lotes, um tokenIds por lote e um tokenUndo só.
void guardarParaUndo(matriz::db::Database& db, const std::string& tokenUndo, const std::string& tokenIds,
                     const std::string& tabela, const std::string& onde) {
    db.exec("CREATE TEMP TABLE IF NOT EXISTS \"undo__" + tabela + "\" AS SELECT '' AS undo_token, * FROM \"" + tabela +
            "\" WHERE 0");
    db.run("INSERT INTO temp.\"undo__" + tabela + "\" SELECT ?, * FROM \"" + tabela + "\" WHERE " + onde,
           {matriz::db::Value::of(tokenUndo), matriz::db::Value::of(tokenIds)});
}
} // namespace

void ProjetoAberto::removerItensDoProjeto(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    {   // as linhas de intake_marca_r saem por ON DELETE CASCADE; espelha na memória
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        for (const auto& id : itemIds) marcadosR_.erase(id);
        ++versaoMarcasR_;
    }
    // Uma transação só pro lote inteiro (era um DELETE autocommit por item —
    // cada um pagando seu próprio overhead de WAL + cascade de FK sozinho,
    // igual o removerItensDoBackup já fazia certo logo acima).
    auto& db = projeto_->registro();
    const std::string token = matriz::model::novoUuid();
    std::vector<std::string> tabelasGuardadas;
    db.run("BEGIN TRANSACTION", {});
    try {
        if (!desfazendo_) {
            db.exec("CREATE TEMP TABLE IF NOT EXISTS undo_ids (token TEXT, id TEXT)");
            for (auto& itemId : itemIds)
                db.run("INSERT INTO temp.undo_ids (token, id) VALUES (?, ?)",
                       {matriz::db::Value::of(token), matriz::db::Value::of(itemId)});
            const std::string idsItem = "(SELECT id FROM temp.undo_ids WHERE token = ?)";
            const std::string idsArquivo = "(SELECT id FROM arquivo WHERE item_id IN " + idsItem + ")";
            guardarParaUndo(db, token, token, "item", "id IN " + idsItem);
            tabelasGuardadas.push_back("item");
            guardarParaUndo(db, token, token, "arquivo", "item_id IN " + idsItem);
            tabelasGuardadas.push_back("arquivo");
            for (const auto& t : tabelasLigadasAoItem(db)) {
                if (t.tabela == "arquivo") continue;
                guardarParaUndo(db, token, token, t.tabela,
                                "\"" + t.coluna + "\" IN " + (t.alvo == "item" ? idsItem : idsArquivo));
                tabelasGuardadas.push_back(t.tabela);
            }
        }
        for (auto& itemId : itemIds)
            db.run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(itemId)});
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
    registrarUndoDeRemocao(token, std::move(tabelasGuardadas), itemIds);
}

void ProjetoAberto::registrarUndoDeRemocao(const std::string& token, std::vector<std::string> tabelasGuardadas,
                                           const std::vector<std::string>& itemIds) {
    if (desfazendo_ || tabelasGuardadas.size() < 2) return;
    // Ordem: item, arquivo, depois as filhas (FK). OR IGNORE: tabela alcançada
    // por duas colunas (ex. item_relacao a/b) guardou a linha duas vezes.
    std::sort(tabelasGuardadas.begin() + 2, tabelasGuardadas.end());
    tabelasGuardadas.erase(std::unique(tabelasGuardadas.begin() + 2, tabelasGuardadas.end()), tabelasGuardadas.end());
    const std::vector<std::string> ids = itemIds;
    registrarUndo(itemIds.size() == 1 ? "Reject Item" : "Reject " + std::to_string(itemIds.size()) + " Items",
                  [this, token, tabelasGuardadas, ids]() {
        auto& d = projeto_->registro();
        d.run("BEGIN TRANSACTION", {});
        try {
            for (const auto& t : tabelasGuardadas) {
                const std::string cols = colunasDe(d, t);
                d.run("INSERT OR IGNORE INTO \"" + t + "\" (" + cols + ") SELECT " + cols + " FROM temp.\"undo__" + t +
                          "\" WHERE undo_token = ?",
                      {matriz::db::Value::of(token)});
            }
            d.run("COMMIT", {});
        } catch (...) {
            try { d.run("ROLLBACK", {}); } catch (...) {}
            return;
        }
        for (const auto& id : ids) EventBus::obterInstancia().dispararItemAlterado(id, "quarentena");
    });
}


void ProjetoAberto::removerItensDoProjetoEmSegundoPlano(
    std::vector<std::string> itemIds, std::shared_ptr<std::atomic<bool>> cancelar,
    std::function<void(int, int)> aoProgredir,
    std::function<void(int, bool, const std::string&)> aoConcluir) {
    if (somenteLeitura_) {
        avisarSomenteLeitura();
        if (aoConcluir) aoConcluir(0, false, "read-only");
        return;
    }
    if (!projeto_ || itemIds.empty()) {
        if (aoConcluir) aoConcluir(0, false, std::string());
        return;
    }
    const bool guardarUndo = !desfazendo_;
    std::weak_ptr<bool> vivo = vivo_;
    agendarNoPoolDoMain([this, vivo, guardarUndo, itemIds = std::move(itemIds), cancelar = std::move(cancelar),
                         aoProgredir = std::move(aoProgredir), aoConcluir = std::move(aoConcluir)] {
        constexpr size_t kLote = 100;
        const int total = static_cast<int>(itemIds.size());
        const std::string token = matriz::model::novoUuid();
        std::vector<std::string> tabelasGuardadas;
        size_t removidos = 0;
        bool cancelado = false;
        std::string erro;

        try {
            std::vector<TabelaLigada> ligadas;
            if (guardarUndo) {
                std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
                ligadas = tabelasLigadasAoItem(projeto_->registro());
            }
            while (removidos < itemIds.size() && erro.empty()) {
                if (cancelar && cancelar->load()) { cancelado = true; break; }
                if (vivo.expired()) return;  // projeto fechando: nada a reportar, a UI que chamava já se foi
                const size_t fim = std::min(removidos + kLote, itemIds.size());
                const std::string lote = matriz::model::novoUuid();
                {
                    // Uma transação por lote: o writeMutex só fica preso durante ele, não durante os 5.000 itens.
                    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
                    auto& db = projeto_->registro();
                    db.run("BEGIN IMMEDIATE", {});
                    try {
                        if (guardarUndo) {
                            db.exec("CREATE TEMP TABLE IF NOT EXISTS undo_ids (token TEXT, id TEXT)");
                            for (size_t j = removidos; j < fim; ++j)
                                db.run("INSERT INTO temp.undo_ids (token, id) VALUES (?, ?)",
                                       {matriz::db::Value::of(lote), matriz::db::Value::of(itemIds[j])});
                            const std::string idsItem = "(SELECT id FROM temp.undo_ids WHERE token = ?)";
                            const std::string idsArquivo = "(SELECT id FROM arquivo WHERE item_id IN " + idsItem + ")";
                            const bool primeiro = tabelasGuardadas.empty();
                            guardarParaUndo(db, token, lote, "item", "id IN " + idsItem);
                            guardarParaUndo(db, token, lote, "arquivo", "item_id IN " + idsItem);
                            if (primeiro) { tabelasGuardadas.push_back("item"); tabelasGuardadas.push_back("arquivo"); }
                            for (const auto& t : ligadas) {
                                if (t.tabela == "arquivo") continue;
                                guardarParaUndo(db, token, lote, t.tabela,
                                                "\"" + t.coluna + "\" IN " + (t.alvo == "item" ? idsItem : idsArquivo));
                                if (primeiro) tabelasGuardadas.push_back(t.tabela);
                            }
                        }
                        for (size_t j = removidos; j < fim; ++j)
                            db.run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(itemIds[j])});
                        if (guardarUndo) db.run("DELETE FROM temp.undo_ids WHERE token = ?", {matriz::db::Value::of(lote)});
                        db.run("COMMIT", {});
                    } catch (const std::exception& e) {
                        try { db.run("ROLLBACK", {}); } catch (...) {}
                        erro = e.what();
                        break;
                    }
                }
                removidos = fim;
                if (aoProgredir)
                    juce::MessageManager::callAsync([vivo, aoProgredir, feitos = static_cast<int>(removidos), total] {
                        if (vivo.lock()) aoProgredir(feitos, total);
                    });
            }
        } catch (const std::exception& e) {
            erro = e.what();
        } catch (...) {
            erro = "unexpected error";
        }

        const std::vector<std::string> removidosIds(itemIds.begin(), itemIds.begin() + static_cast<std::ptrdiff_t>(removidos));
        juce::MessageManager::callAsync([this, vivo, guardarUndo, token, tabelasGuardadas, removidosIds, cancelado, erro,
                                         aoConcluir] {
            if (!vivo.lock()) return;
            if (guardarUndo && !removidosIds.empty()) registrarUndoDeRemocao(token, tabelasGuardadas, removidosIds);
            if (aoConcluir) aoConcluir(static_cast<int>(removidosIds.size()), cancelado, erro);
        });
    });
}

// ── NEST ─────────────────────────────────────────────────────────────────

namespace {
struct EstadoNest {
    std::string id, projetoId, capa, criadoEm;
    std::vector<std::string> membros;
    // Identidade de CONTAINER (nest.origem e colunas da ficha): sem isto o Undo de "Ungroup" devolvia um nest comum.
    std::optional<std::string> origem, capaImagem, icone, questionario, nome, tipoContainer;
};

// Os nests a que os itens pertencem, com todos os membros — o retrato que o Undo restaura.
std::vector<EstadoNest> capturarNests(matriz::db::Database& db, const std::set<std::string>& itemIds) {
    std::set<std::string> idsDeNest;
    {
        auto st = db.prepare("SELECT nest_id FROM nest_item WHERE item_id = ?");
        for (const auto& id : itemIds) {
            st.reset();
            st.bind(1, matriz::db::Value::of(id));
            while (st.step()) idsDeNest.insert(st.columnText(0));
        }
    }
    std::vector<EstadoNest> out;
    for (const auto& nid : idsDeNest) {
        EstadoNest e;
        e.id = nid;
        auto st = db.prepare("SELECT projeto_id, COALESCE(capa_item_id, ''), criado_em, origem, capa_imagem, icone, questionario, nome, tipo_container "
                             "FROM nest WHERE id = ?");
        st.bind(1, matriz::db::Value::of(nid));
        if (!st.step()) continue;
        e.projetoId = st.columnText(0);
        e.capa = st.columnText(1);
        e.criadoEm = st.columnText(2);
        const auto opt = [&st](int c) { return st.columnIsNull(c) ? std::optional<std::string>() : std::optional<std::string>(st.columnText(c)); };
        e.origem = opt(3); e.capaImagem = opt(4); e.icone = opt(5); e.questionario = opt(6); e.nome = opt(7); e.tipoContainer = opt(8);
        auto sm = db.prepare("SELECT item_id FROM nest_item WHERE nest_id = ?");
        sm.bind(1, matriz::db::Value::of(nid));
        while (sm.step()) e.membros.push_back(sm.columnText(0));
        out.push_back(std::move(e));
    }
    return out;
}

void removerNestsDosItens(matriz::db::Database& db, const std::set<std::string>& itemIds) {
    for (const auto& id : itemIds)
        db.run("DELETE FROM nest WHERE id IN (SELECT nest_id FROM nest_item WHERE item_id = ?)", {matriz::db::Value::of(id)});
}

void restaurarNests(matriz::db::Database& db, const std::vector<EstadoNest>& estados) {
    for (const auto& e : estados) {
        const auto opt = [](const std::optional<std::string>& v) { return v ? matriz::db::Value::of(*v) : matriz::db::Value::null(); };
        db.run("INSERT OR REPLACE INTO nest (id, projeto_id, capa_item_id, criado_em, origem, capa_imagem, icone, questionario, nome, tipo_container) "
               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
               {matriz::db::Value::of(e.id), matriz::db::Value::of(e.projetoId),
                e.capa.empty() ? matriz::db::Value::null() : matriz::db::Value::of(e.capa), matriz::db::Value::of(e.criadoEm),
                opt(e.origem), opt(e.capaImagem), opt(e.icone), opt(e.questionario), opt(e.nome), opt(e.tipoContainer)});
        for (const auto& m : e.membros)
            db.run("INSERT OR REPLACE INTO nest_item (item_id, nest_id, adicionado_em) VALUES (?, ?, ?)",
                   {matriz::db::Value::of(m), matriz::db::Value::of(e.id), matriz::db::Value::of(e.criadoEm)});
    }
}
} // namespace

std::map<std::string, ProjetoAberto::NestInfo> ProjetoAberto::mapaDeNests(matriz::db::Database& registro) {
    std::map<std::string, NestInfo> out;
    try {
        std::map<std::string, std::vector<std::string>> membros;  // nest -> itens
        std::map<std::string, std::string> capaDe;
        auto st = registro.prepare(
            "SELECT ni.item_id, ni.nest_id, "
            "COALESCE(n.capa_item_id, (SELECT x.item_id FROM nest_item x JOIN item i2 ON i2.id = x.item_id "
            "                          WHERE x.nest_id = ni.nest_id ORDER BY i2.criado_em, i2.codigo_acervo LIMIT 1)), "
            "n.origem, n.capa_imagem, n.icone, n.nome, n.tipo_container "
            "FROM nest_item ni JOIN nest n ON n.id = ni.nest_id");
        std::map<std::string, std::string> origemDe, capaImagemDe, iconeDe, nomeDe, tipoDe;
        while (st.step()) {
            membros[st.columnText(1)].push_back(st.columnText(0));
            capaDe[st.columnText(1)] = st.columnIsNull(2) ? std::string() : st.columnText(2);
            if (!st.columnIsNull(3)) origemDe[st.columnText(1)] = st.columnText(3);
            if (!st.columnIsNull(4)) capaImagemDe[st.columnText(1)] = st.columnText(4);
            if (!st.columnIsNull(5)) iconeDe[st.columnText(1)] = st.columnText(5);
            if (!st.columnIsNull(6)) nomeDe[st.columnText(1)] = st.columnText(6);
            if (!st.columnIsNull(7)) tipoDe[st.columnText(1)] = st.columnText(7);
        }
        for (const auto& [nid, itens] : membros) {
            if (itens.size() < 2) continue;  // nest de um arquivo só não existe
            NestInfo info;
            info.nestId = nid;
            info.capaId = capaDe[nid];
            if (auto oit = origemDe.find(nid); oit != origemDe.end()) info.origem = oit->second;
            if (auto cit = capaImagemDe.find(nid); cit != capaImagemDe.end()) info.capaImagem = cit->second;
            if (auto iit = iconeDe.find(nid); iit != iconeDe.end()) info.icone = iit->second;
            if (auto nmit = nomeDe.find(nid); nmit != nomeDe.end()) info.nome = nmit->second;
            if (auto tit = tipoDe.find(nid); tit != tipoDe.end()) info.tipo = tit->second;
            info.total = static_cast<int>(itens.size());
            for (const auto& id : itens) out[id] = info;
        }
    } catch (...) {}
    return out;
}

bool ProjetoAberto::apagarMensagemDaConversa(const std::string& conversaItemId, int indiceOriginal, const juce::String& assinatura,
                                             const std::string& midiaItemId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || conversaItemId.empty() || indiceOriginal < 0) return false;
    auto& db = projeto_->registro();
    db.exec("CREATE TABLE IF NOT EXISTS conversa_mensagem_apagada ("
            "conversa_item_id TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE, indice INTEGER NOT NULL, "
            "assinatura TEXT NOT NULL, apagada_em TEXT NOT NULL, PRIMARY KEY (conversa_item_id, indice))");
    // Undo (antes não havia — apagava sem volta): um passo só com a marca de apagada + contador da conversa (retrato)
    // e a mídia, que sai pelo removerItensDoProjeto (guarda arquivo, conversa_midia, nest_item… para voltar).
    iniciarGrupoUndo("Delete WhatsApp message");
    auto retrato = retratar({{"conversa_mensagem_apagada", "conversa_item_id", false}, {"conversa_whatsapp", "item_id", true}},
                            {conversaItemId});
    db.run("BEGIN TRANSACTION", {});
    try {
        db.run("INSERT OR REPLACE INTO conversa_mensagem_apagada (conversa_item_id, indice, assinatura, apagada_em) VALUES (?, ?, ?, ?)",
               {matriz::db::Value::of(conversaItemId), matriz::db::Value::of(static_cast<int64_t>(indiceOriginal)),
                matriz::db::Value::of(assinatura.toStdString()), matriz::db::Value::of(matriz::model::agoraIso8601())});
        db.run("UPDATE conversa_whatsapp SET n_mensagens = MAX(0, n_mensagens - 1) WHERE item_id = ?", {matriz::db::Value::of(conversaItemId)});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        finalizarGrupoUndo();
        return false;
    }
    registrarUndoDeRetrato("Delete WhatsApp message", std::move(retrato), {conversaItemId});
    if (!midiaItemId.empty()) {
        try { removerItensDoProjeto({midiaItemId}); } catch (...) {}
    }
    finalizarGrupoUndo();
    ultimosItensIngeridosValido_ = false;
    EventBus::obterInstancia().dispararItemAlterado(conversaItemId, "metadado");
    return true;
}

std::map<int, juce::String> ProjetoAberto::mensagensApagadasDaConversa(const std::string& conversaItemId) const {
    std::map<int, juce::String> out;
    if (!projeto_) return out;
    try {
        auto st = leitura().prepare("SELECT indice, assinatura FROM conversa_mensagem_apagada WHERE conversa_item_id = ?");
        st.bind(1, matriz::db::Value::of(conversaItemId));
        while (st.step()) out[static_cast<int>(st.columnInt(0))] = juce::String::fromUTF8(st.columnText(1).c_str());
    } catch (...) {}  // tabela ainda não existe (nada apagado)
    return out;
}

std::string ProjetoAberto::criarContainerImportacao(const std::vector<std::string>& itemIds, const std::string& origem) {
    if (somenteLeitura_ || !projeto_ || itemIds.size() < 2 || origem.empty()) return {};
    auto& db = projeto_->registro();
    const std::set<std::string> jaEmNest = [&] {
        std::set<std::string> out;
        for (const auto& [id, info] : mapaDeNests(db)) { juce::ignoreUnused(info); out.insert(id); }
        return out;
    }();
    std::vector<std::string> livres;
    for (const auto& id : itemIds) if (jaEmNest.count(id) == 0) livres.push_back(id);
    if (livres.size() < 2) return {};
    const std::string nestId = matriz::model::novoUuid();
    const std::string agora = matriz::model::agoraIso8601();
    db.run("BEGIN TRANSACTION", {});
    try {
        db.run("INSERT INTO nest (id, projeto_id, capa_item_id, criado_em, origem) VALUES (?, ?, ?, ?, ?)",
               {matriz::db::Value::of(nestId), matriz::db::Value::of(projeto_->projetoId()), matriz::db::Value::of(livres.front()),
                matriz::db::Value::of(agora), matriz::db::Value::of(origem)});
        for (const auto& id : livres)
            db.run("INSERT INTO nest_item (item_id, nest_id, adicionado_em) VALUES (?, ?, ?)",
                   {matriz::db::Value::of(id), matriz::db::Value::of(nestId), matriz::db::Value::of(agora)});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    return nestId;
}

std::string ProjetoAberto::criarContainerDeTipo(const std::vector<std::string>& itemIds, const std::string& tipo, const std::string& origem,
                                                const std::string& nome) {
    if (somenteLeitura_ || !projeto_ || itemIds.size() < 2 || tipo.empty() || origem.empty()) return {};
    auto& db = projeto_->registro();
    const auto jaEmNest = mapaDeNests(db);
    for (const auto& id : itemIds) if (jaEmNest.count(id) != 0) return {};
    const std::string capa = ordenarPorDataDeCriacao(itemIds).front();
    const std::string nestId = matriz::model::novoUuid();
    const std::string agora = matriz::model::agoraIso8601();
    registrarUndoDoContainer(*this, "Create Container", nestId, false);  // retrato vazio: o Undo apaga o container novo
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    db.run("BEGIN TRANSACTION", {});
    try {
        db.run("INSERT INTO nest (id, projeto_id, capa_item_id, criado_em, origem, tipo_container, nome) VALUES (?, ?, ?, ?, ?, ?, ?)",
               {matriz::db::Value::of(nestId), matriz::db::Value::of(projeto_->projetoId()), matriz::db::Value::of(capa),
                matriz::db::Value::of(agora), matriz::db::Value::of(origem), matriz::db::Value::of(tipo),
                nome.empty() ? matriz::db::Value::null() : matriz::db::Value::of(nome)});
        for (const auto& id : itemIds)
            db.run("INSERT INTO nest_item (item_id, nest_id, adicionado_em) VALUES (?, ?, ?)",
                   {matriz::db::Value::of(id), matriz::db::Value::of(nestId), matriz::db::Value::of(agora)});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        return {};
    }
    return nestId;
}

bool ProjetoAberto::trocarTipoDoContainer(const std::string& nestId, const std::string& tipo, const std::string& origem) {
    if (somenteLeitura_ || !projeto_ || nestId.empty() || tipo.empty() || origem.empty()) return false;
    registrarUndoDoContainer(*this, "Change Container Type", nestId, false);
    auto& db = projeto_->registro();
    const std::string json = questionarioDoContainer(nestId);
    const std::string folhaAtual = json.empty() ? std::string() : juce::JSON::parse(juce::String::fromUTF8(json.c_str()))["folha"].toString().toStdString();
    const bool temQuestionario = !folhaAtual.empty();
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    try {
        db.run("UPDATE nest SET origem = ?, tipo_container = ?, icone = NULL WHERE id = ?",
               {matriz::db::Value::of(origem), matriz::db::Value::of(tipo), matriz::db::Value::of(nestId)});
        if (temQuestionario && folhaAtual != tipo)  // o questionario era de outro cartao: Edit Project Info nao se aplica mais
            db.run("UPDATE nest SET questionario = NULL WHERE id = ?", {matriz::db::Value::of(nestId)});
    } catch (...) { return false; }
    return true;
}

int ProjetoAberto::aplicarContextoDeImportacao(const std::vector<std::string>& itemIds, const matriz::ingest::SobreMaterial& sobre,
                                               const std::string& videoPrincipalId) {
    if (somenteLeitura_ || !projeto_ || itemIds.empty()) return 0;
    auto& db = projeto_->registro();
    matriz::ingest::SobreMaterial base = sobre;
    base.collectionType.clear();  // CONTENT segue a regra propria (so onde vazio)
    matriz::ingest::SobreMaterial semVideo = base;
    semVideo.camposDoVideo.clear();
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    int tocados = 0;
    try {
        db.exec("BEGIN IMMEDIATE");
        {
            matriz::ingest::AplicadorSobreMaterial comVideo(db, base);
            matriz::ingest::AplicadorSobreMaterial sem(db, semVideo);
            for (const auto& id : itemIds) {
                if (id == videoPrincipalId) comVideo.aplicar(id); else sem.aplicar(id);
                ++tocados;
            }
        }
        db.exec("COMMIT");
    } catch (const std::exception& e) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        juce::Logger::writeToLog("aplicarContextoDeImportacao: " + juce::String(e.what()));
        return 0;
    }
    escrita.unlock();
    juce::MessageManager::callAsync([itemIds] { EventBus::obterInstancia().dispararItensAlterados(itemIds, "metadado"); });
    return tocados;
}

int ProjetoAberto::definirContentSeVazio(const std::vector<std::string>& itemIds, const std::string& valor) {
    if (somenteLeitura_ || !projeto_ || itemIds.empty() || valor.empty()) return 0;
    registrarUndoDeRetrato("Set Content", retratar({{"item", "id", true}, {"item_campo", "item_id", false}}, itemIds), itemIds);
    auto& db = projeto_->registro();
    const std::string agora = matriz::model::agoraIso8601();
    std::vector<std::string> gravados;
    std::unique_lock<std::recursive_mutex> escrita(projeto_->writeMutex());
    try {
        db.exec("BEGIN IMMEDIATE");
        for (const auto& id : itemIds) {
            auto st = db.prepare("SELECT COALESCE(TRIM(collection_type), ''), "
                                 "COALESCE((SELECT TRIM(valor) FROM item_campo WHERE item_id = ?1 AND campo_id = 'collection_type' LIMIT 1), '') "
                                 "FROM item WHERE id = ?1");
            st.bind(1, matriz::db::Value::of(id));
            if (!st.step() || !st.columnText(0).empty() || !st.columnText(1).empty()) continue;  // o que o usuario preencheu nunca e sobrescrito
            db.run("UPDATE item SET collection_type = ?, metadados_editados = 1, atualizado_em = ? WHERE id = ?",
                   {matriz::db::Value::of(valor), matriz::db::Value::of(agora), matriz::db::Value::of(id)});
            db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                   "VALUES (?, ?, 'raiz', 0, 'collection_type', ?, 'humano', ?) "
                   "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
                   {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id), matriz::db::Value::of(valor), matriz::db::Value::of(agora)});
            gravados.push_back(id);
        }
        db.exec("COMMIT");
    } catch (const std::exception& e) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        juce::Logger::writeToLog("definirContentSeVazio: " + juce::String(e.what()));
        return 0;
    }
    escrita.unlock();
    if (!gravados.empty())
        juce::MessageManager::callAsync([gravados] { EventBus::obterInstancia().dispararItensAlterados(gravados, "metadado"); });
    return static_cast<int>(gravados.size());
}

bool ProjetoAberto::definirCapaDoContainer(const std::string& nestId, const juce::File& imagem) {
    if (somenteLeitura_ || !projeto_ || nestId.empty() || !imagem.existsAsFile()) return false;
    juce::Image img = juce::ImageFileFormat::loadFrom(imagem);
    if (!img.isValid()) return false;
    registrarUndoDoContainer(*this, "Set Container Cover", nestId, true);
    constexpr int kLado = 512;
    if (img.getWidth() > kLado || img.getHeight() > kLado) {
        const float escala = static_cast<float>(kLado) / static_cast<float>(juce::jmax(img.getWidth(), img.getHeight()));
        img = img.rescaled(juce::jmax(1, juce::roundToInt(img.getWidth() * escala)), juce::jmax(1, juce::roundToInt(img.getHeight() * escala)),
                           juce::Graphics::highResamplingQuality);
    }
    const juce::String relativo = ".miniaturas/capa_container_" + juce::String(nestId) + ".jpg";
    const juce::File destino = projeto_->pasta().getChildFile(relativo);
    destino.getParentDirectory().createDirectory();
    destino.deleteFile();
    {
        juce::FileOutputStream saida(destino);
        if (!saida.openedOk()) return false;
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality(0.9f);
        if (!jpeg.writeImageToStream(img, saida)) return false;
    }
    try {
        projeto_->registro().run("UPDATE nest SET capa_imagem = ? WHERE id = ?",
                                 {matriz::db::Value::of(relativo.toStdString()), matriz::db::Value::of(nestId)});
    } catch (...) { return false; }
    return true;
}

bool ProjetoAberto::definirIconeDoContainer(const std::string& nestId, const std::string& nomeDoRecurso) {
    if (somenteLeitura_ || !projeto_ || nestId.empty() || nomeDoRecurso.empty()) return false;
    registrarUndoDoContainer(*this, "Set Container Icon", nestId, false);
    try {
        projeto_->registro().run("UPDATE nest SET icone = ? WHERE id = ?", {matriz::db::Value::of(nomeDoRecurso), matriz::db::Value::of(nestId)});
    } catch (...) { return false; }
    return true;
}

std::string ProjetoAberto::questionarioDoContainer(const std::string& nestId) const {
    if (!projeto_ || nestId.empty()) return {};
    try {
        auto st = leitura().prepare("SELECT questionario FROM nest WHERE id = ?");
        st.bind(1, matriz::db::Value::of(nestId));
        if (st.step() && !st.columnIsNull(0)) return st.columnText(0);
    } catch (...) {}
    return {};
}

std::string ProjetoAberto::origemDoContainer(const std::string& nestId) const {
    if (!projeto_ || nestId.empty()) return {};
    try {
        auto st = leitura().prepare("SELECT origem FROM nest WHERE id = ?");
        st.bind(1, matriz::db::Value::of(nestId));
        if (st.step() && !st.columnIsNull(0)) return st.columnText(0);
    } catch (...) {}
    return {};
}

std::string ProjetoAberto::tipoDoContainer(const std::string& nestId) const {
    if (!projeto_ || nestId.empty()) return {};
    try {
        auto st = leitura().prepare("SELECT tipo_container FROM nest WHERE id = ?");
        st.bind(1, matriz::db::Value::of(nestId));
        if (st.step() && !st.columnIsNull(0)) return st.columnText(0);
    } catch (...) {}
    return {};
}

void ProjetoAberto::definirQuestionarioDoContainer(const std::string& nestId, const std::string& json) {
    if (somenteLeitura_ || !projeto_ || nestId.empty() || json.empty()) return;
    registrarUndoDoContainer(*this, "Edit Container Questionnaire", nestId, false);
    try {
        projeto_->registro().run("UPDATE nest SET questionario = ? WHERE id = ?", {matriz::db::Value::of(json), matriz::db::Value::of(nestId)});
    } catch (...) {}
}

bool ProjetoAberto::reaplicarQuestionarioDoContainer(const std::string& nestId, const matriz::ingest::SobreMaterial& antigo,
                                                      const matriz::ingest::SobreMaterial& novo, const std::string& json) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || nestId.empty()) return false;
    const auto membros = membrosDoNest(nestId);
    if (membros.empty()) return false;
    auto& db = projeto_->registro();
    std::unique_lock<std::recursive_mutex> writeLock(projeto_->writeMutex());
    try {
        db.exec("BEGIN IMMEDIATE");
        {
            matriz::ingest::AplicadorSobreMaterial tirar(db, antigo);
            matriz::ingest::AplicadorSobreMaterial por(db, novo);
            for (const auto& id : membros) { tirar.desfazer(id); por.aplicar(id); }
        }
        db.run("UPDATE nest SET questionario = ? WHERE id = ?", {matriz::db::Value::of(json), matriz::db::Value::of(nestId)});
        // O nome vem do questionário: apagado lá, o container volta a mostrar o rótulo do tipo.
        db.run("UPDATE nest SET nome = ? WHERE id = ?", {novo.nomeContainer.empty() ? matriz::db::Value::null() : matriz::db::Value::of(novo.nomeContainer), matriz::db::Value::of(nestId)});
        db.exec("COMMIT");
    } catch (const std::exception& e) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        juce::Logger::writeToLog("reaplicarQuestionarioDoContainer: " + juce::String(e.what()));
        return false;
    }
    writeLock.unlock();
    EventBus::obterInstancia().dispararItensAlterados(membros, "metadado");
    // Nome, capa e SUBJECT do container mudam em várias telas ao mesmo tempo: recarga ampla (Grid, METADATA, Folder Map, ficha).
    EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");
    return true;
}

bool ProjetoAberto::definirNomeDoContainer(const std::string& nestId, const std::string& nome) {
    if (somenteLeitura_ || !projeto_ || nestId.empty() || nome.empty()) return false;
    registrarUndoDoContainer(*this, "Rename Container", nestId, false);
    try {
        projeto_->registro().run("UPDATE nest SET nome = ? WHERE id = ?", {matriz::db::Value::of(nome), matriz::db::Value::of(nestId)});
    } catch (...) { return false; }
    return true;
}

bool ProjetoAberto::removerCapaDoContainer(const std::string& nestId) {
    if (somenteLeitura_ || !projeto_ || nestId.empty()) return false;
    registrarUndoDoContainer(*this, "Remove Container Cover", nestId, true);
    try {
        projeto_->registro().run("UPDATE nest SET capa_imagem = NULL WHERE id = ?", {matriz::db::Value::of(nestId)});
    } catch (...) { return false; }
    projeto_->pasta().getChildFile(".miniaturas/capa_container_" + juce::String(nestId) + ".jpg").deleteFile();
    definirRespostaDoQuestionario(nestId, "capa", {});
    return true;
}

void ProjetoAberto::definirRespostaDoQuestionario(const std::string& nestId, const std::string& chave, const std::string& valor) {
    if (somenteLeitura_ || !projeto_ || nestId.empty()) return;
    const auto atual = questionarioDoContainer(nestId);
    if (atual.empty()) return;  // container sem questionário guardado: nada a manter coerente
    auto v = juce::JSON::parse(juce::String::fromUTF8(atual.c_str()));
    auto* raiz = v.getDynamicObject();
    if (raiz == nullptr) return;
    auto* respostas = raiz->getProperty("respostas").getDynamicObject();
    if (respostas == nullptr) return;
    if (valor.empty()) respostas->removeProperty(juce::Identifier(chave.c_str()));
    else respostas->setProperty(juce::Identifier(chave.c_str()), juce::String::fromUTF8(valor.c_str()));
    definirQuestionarioDoContainer(nestId, juce::JSON::toString(v, true).toStdString());
}

bool ProjetoAberto::definirCapaDoContainerComFrameDeVideo(const std::string& nestId, bool gerarFrame) {
    if (somenteLeitura_ || !projeto_ || nestId.empty()) return false;
    registrarUndoDoContainer(*this, "Set Container Cover", nestId, true);
    juce::File video;
    double duracao = 0.0;
    juce::int64 maior = -1;
    std::string itemDoVideo;
    try {
        auto stmt = projeto_->registro().prepare(
            "SELECT a.caminho_absoluto_origem, a.tamanho_bytes, a.caracteristicas_tecnicas_json, ni.item_id "
            "FROM nest_item ni JOIN arquivo a ON a.item_id = ni.item_id AND a.eh_master = 1 WHERE ni.nest_id = ?");
        stmt.bind(1, matriz::db::Value::of(nestId));
        while (stmt.step()) {
            if (stmt.columnIsNull(0)) continue;
            const juce::File f(juce::String(stmt.columnText(0)));
            if (matriz::ingest::categoriaPorExtensao(f) != matriz::ingest::CategoriaMidia::Video || !f.existsAsFile()) continue;
            const juce::int64 tamanho = stmt.columnIsNull(1) ? 0 : static_cast<juce::int64>(stmt.columnInt(1));
            if (tamanho <= maior) continue;
            double d = 0.0;
            if (!stmt.columnIsNull(2)) {
                const auto json = juce::JSON::parse(juce::String(stmt.columnText(2)));
                if (json.isObject() && json.hasProperty("duracaoSegundos")) d = static_cast<double>(json["duracaoSegundos"]);
            }
            if (d <= 0.0) continue;
            maior = tamanho;
            video = f;
            duracao = d;
            itemDoVideo = stmt.columnText(3);
        }
    } catch (...) { return false; }
    if (!video.existsAsFile()) return false;
    if (!gerarFrame) {  // a capa já foi escolhida: só o vídeo vira o item que representa o container
        if (itemDoVideo.empty()) return false;
        try { projeto_->registro().run("UPDATE nest SET capa_item_id = ? WHERE id = ?", {matriz::db::Value::of(itemDoVideo), matriz::db::Value::of(nestId)}); }
        catch (...) { return false; }
        return true;
    }
    try {
        const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("matriz_capa_" + juce::String(nestId));
        auto frames = matriz::ingest::gerarKeyframesVideo(video, duracao, 1, tmp, "capa", 640, false, 0.35, 0.65);
        bool ok = false;
        if (!frames.empty()) ok = definirCapaDoContainer(nestId, frames.front().arquivo);
        tmp.deleteRecursively();
        // O vídeo é quem representa o container no Intake (categoria, título e miniatura), não o primeiro arquivo por data.
        if (ok && !itemDoVideo.empty())
            projeto_->registro().run("UPDATE nest SET capa_item_id = ? WHERE id = ?", {matriz::db::Value::of(itemDoVideo), matriz::db::Value::of(nestId)});
        return ok;
    } catch (...) { return false; }
}

std::string ProjetoAberto::criarNestDeImportacao(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_ || !projeto_ || itemIds.size() < 2) return {};
    auto& db = projeto_->registro();
    std::set<std::string> jaEmNest;
    for (const auto& [id, info] : mapaDeNests(db)) { juce::ignoreUnused(info); jaEmNest.insert(id); }
    std::vector<std::string> livres;
    for (const auto& id : itemIds) if (jaEmNest.count(id) == 0) livres.push_back(id);
    if (livres.size() < 2) return {};
    const std::string nestId = matriz::model::novoUuid();
    const std::string agora = matriz::model::agoraIso8601();
    db.run("BEGIN TRANSACTION", {});
    try {
        db.run("INSERT INTO nest (id, projeto_id, capa_item_id, criado_em, origem) VALUES (?, ?, ?, ?, NULL)",
               {matriz::db::Value::of(nestId), matriz::db::Value::of(projeto_->projetoId()), matriz::db::Value::of(livres.front()),
                matriz::db::Value::of(agora)});
        for (const auto& id : livres)
            db.run("INSERT INTO nest_item (item_id, nest_id, adicionado_em) VALUES (?, ?, ?)",
                   {matriz::db::Value::of(id), matriz::db::Value::of(nestId), matriz::db::Value::of(agora)});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    return nestId;
}

std::optional<juce::String> ProjetoAberto::capaImagemDoContainer(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    try {
        auto stmt = leitura().prepare("SELECT capa_imagem FROM nest WHERE capa_item_id = ? AND capa_imagem IS NOT NULL AND capa_imagem <> '' LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (!stmt.step()) return std::nullopt;
        const juce::File f = projeto_->pasta().getChildFile(juce::String(stmt.columnText(0)));
        if (f.existsAsFile()) return f.getFullPathName();
    } catch (...) {}
    return std::nullopt;
}

void ProjetoAberto::removerDoContainer(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_ || !projeto_ || itemIds.empty()) return;
    auto& db = projeto_->registro();
    std::set<std::string> nests;
    for (const auto& id : itemIds) {
        auto st = db.prepare("SELECT nest_id FROM nest_item WHERE item_id = ?");
        st.bind(1, matriz::db::Value::of(id));
        if (st.step()) nests.insert(st.columnText(0));
    }
    if (nests.empty()) return;
    // Undo: os containers afetados inteiros (linha do nest + todos os membros), porque um container que fica com
    // menos de 2 itens é desfeito e o que sobra muda de capa.
    registrarUndoDeRetrato(itemIds.size() == 1 ? "Remove from Container" : "Remove " + std::to_string(itemIds.size()) + " Items from Container",
                           retratar({{"nest", "id", false}, {"nest_item", "nest_id", false}},
                                    std::vector<std::string>(nests.begin(), nests.end())),
                           {""}, {}, "nest");
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& id : itemIds) db.run("DELETE FROM nest_item WHERE item_id = ?", {matriz::db::Value::of(id)});
        for (const auto& nid : nests) {
            auto st = db.prepare("SELECT item_id FROM nest_item WHERE nest_id = ? ORDER BY adicionado_em, item_id");
            st.bind(1, matriz::db::Value::of(nid));
            std::vector<std::string> restantes;
            while (st.step()) restantes.push_back(st.columnText(0));
            if (restantes.size() < 2) {
                db.run("DELETE FROM nest_item WHERE nest_id = ?", {matriz::db::Value::of(nid)});
                db.run("DELETE FROM nest WHERE id = ?", {matriz::db::Value::of(nid)});
            }
            else db.run("UPDATE nest SET capa_item_id = ? WHERE id = ?", {matriz::db::Value::of(restantes.front()), matriz::db::Value::of(nid)});
        }
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    EventBus::obterInstancia().dispararItemAlterado("", "nest");
}

std::string ProjetoAberto::criarNest(const std::vector<std::string>& itemIdsPedidos) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_ || itemIdsPedidos.size() < 2) return {};
    auto& db = projeto_->registro();

    std::set<std::string> conjunto(itemIdsPedidos.begin(), itemIdsPedidos.end());
    const auto antes = capturarNests(db, conjunto);
    for (const auto& e : antes) conjunto.insert(e.membros.begin(), e.membros.end());  // nest sobre nest: junta tudo
    if (conjunto.size() < 2) return {};

    std::string nestId = matriz::model::novoUuid();
    std::string capa, criadoEm = matriz::model::agoraIso8601();
    if (!antes.empty()) {  // reaproveita o nest mais antigo (e a capa dele, se continuar entre os arquivos)
        const auto maisAntigo = std::min_element(antes.begin(), antes.end(),
                                                 [](const EstadoNest& a, const EstadoNest& b) { return a.criadoEm < b.criadoEm; });
        nestId = maisAntigo->id;
        criadoEm = maisAntigo->criadoEm;
        capa = maisAntigo->capa;
    }
    if (capa.empty() || conjunto.count(capa) == 0)
        capa = ordenarPorDataDeCriacao(std::vector<std::string>(conjunto.begin(), conjunto.end())).front();  // 1º por data

    db.run("BEGIN TRANSACTION", {});
    try {
        removerNestsDosItens(db, conjunto);
        EstadoNest novo;
        novo.id = nestId;
        novo.projetoId = projeto_->projetoId();
        novo.capa = capa;
        novo.criadoEm = criadoEm;
        novo.membros.assign(conjunto.begin(), conjunto.end());
        restaurarNests(db, {novo});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    if (!desfazendo_) {
        registrarUndo("Nest " + std::to_string(conjunto.size()) + " Files", [this, antes, conjunto]() {
            auto& d = projeto_->registro();
            d.run("BEGIN TRANSACTION", {});
            try {
                removerNestsDosItens(d, conjunto);
                restaurarNests(d, antes);
                d.run("COMMIT", {});
            } catch (...) { try { d.run("ROLLBACK", {}); } catch (...) {} return; }
            EventBus::obterInstancia().dispararItemAlterado("", "nest");
        });
    }
    EventBus::obterInstancia().dispararItemAlterado("", "nest");
    return nestId;
}

void ProjetoAberto::desfazerNest(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    auto& db = projeto_->registro();
    const std::set<std::string> pedidos(itemIds.begin(), itemIds.end());
    const auto antes = capturarNests(db, pedidos);
    if (antes.empty()) return;
    std::set<std::string> todos = pedidos;
    for (const auto& e : antes) todos.insert(e.membros.begin(), e.membros.end());

    db.run("BEGIN TRANSACTION", {});
    try {
        removerNestsDosItens(db, todos);
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    if (!desfazendo_) {
        registrarUndo("Un-nest", [this, antes]() {
            auto& d = projeto_->registro();
            d.run("BEGIN TRANSACTION", {});
            try {
                restaurarNests(d, antes);
                d.run("COMMIT", {});
            } catch (...) { try { d.run("ROLLBACK", {}); } catch (...) {} return; }
            EventBus::obterInstancia().dispararItemAlterado("", "nest");
        });
    }
    EventBus::obterInstancia().dispararItemAlterado("", "nest");
}

void ProjetoAberto::registrarNoLog(const juce::String& titulo, const juce::StringArray& linhas) {
    if (!projeto_) return;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        ++gravacoesMarcasPendentes_;
    }
    poolMarcas_.addJob([this, titulo, linhas]() {
        try { matriz::model::ProjectLog(projeto_->pasta()).appendEntry(titulo, linhas); } catch (...) {}
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        --gravacoesMarcasPendentes_;
    });
}

namespace {
// Capa que o nest passa a ter quando `retirados` saem dele: a atual, se continua; senão o 1º por data.
std::string capaAposRetirar(const EstadoNest& e, const std::set<std::string>& retirados,
                            const std::vector<std::string>& restantesOrdenados) {
    if (!e.capa.empty() && retirados.count(e.capa) == 0 &&
        std::find(restantesOrdenados.begin(), restantesOrdenados.end(), e.capa) != restantesOrdenados.end())
        return e.capa;
    return restantesOrdenados.empty() ? std::string() : restantesOrdenados.front();
}
}  // namespace

void ProjetoAberto::extrairDoNest(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    auto& db = projeto_->registro();
    const std::set<std::string> pedidos(itemIds.begin(), itemIds.end());
    const auto antes = capturarNests(db, pedidos);
    if (antes.empty()) return;

    std::set<std::string> tocados;  // todos os membros dos nests afetados
    std::vector<EstadoNest> depois;
    for (const auto& e : antes) {
        tocados.insert(e.membros.begin(), e.membros.end());
        std::vector<std::string> restantes;
        for (const auto& m : e.membros) if (pedidos.count(m) == 0) restantes.push_back(m);
        if (restantes.size() < 2) continue;  // sobrou 1 (ou nenhum): o nest se desfaz
        restantes = ordenarPorDataDeCriacao(restantes);
        EstadoNest n = e;
        n.membros = restantes;
        n.capa = capaAposRetirar(e, pedidos, restantes);
        depois.push_back(std::move(n));
    }

    db.run("BEGIN TRANSACTION", {});
    try {
        removerNestsDosItens(db, tocados);
        restaurarNests(db, depois);
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    if (!desfazendo_) {
        registrarUndo("Extract from Nest", [this, antes, tocados]() {
            auto& d = projeto_->registro();
            d.run("BEGIN TRANSACTION", {});
            try {
                removerNestsDosItens(d, tocados);
                restaurarNests(d, antes);
                d.run("COMMIT", {});
            } catch (...) { try { d.run("ROLLBACK", {}); } catch (...) {} return; }
            EventBus::obterInstancia().dispararItemAlterado("", "nest");
        });
    }
    juce::StringArray linhas;
    linhas.add("Files: " + juce::String(static_cast<int>(pedidos.size())));
    registrarNoLog("Nest: files extracted", linhas);
    EventBus::obterInstancia().dispararItemAlterado("", "nest");
}

void ProjetoAberto::apagarDoNest(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    auto& db = projeto_->registro();
    const std::set<std::string> pedidos(itemIds.begin(), itemIds.end());

    // A marcação de EXPORT (H/K/P/W) segue a capa: se a capa vai embora, a nova capa herda as marcas.
    const auto antes = capturarNests(db, pedidos);
    std::vector<std::pair<std::string, std::string>> trocasDeCapa;
    for (const auto& e : antes) {
        if (e.capa.empty() || pedidos.count(e.capa) == 0) continue;
        std::vector<std::string> restantes;
        for (const auto& m : e.membros) if (pedidos.count(m) == 0) restantes.push_back(m);
        if (restantes.size() < 2) continue;
        const auto nova = capaAposRetirar(e, pedidos, ordenarPorDataDeCriacao(restantes));
        if (!nova.empty()) trocasDeCapa.emplace_back(e.capa, nova);
    }

    iniciarGrupoUndo(pedidos.size() == 1 ? "Delete File from Nest" : "Delete " + std::to_string(pedidos.size()) + " Files from Nest");
    try {
        for (const auto& [velha, nova] : trocasDeCapa) transferirMarcacoes(velha, nova);
        extrairDoNest(itemIds);                  // estrutura do nest (capa nova / dissolução)
        removerItensDoProjeto(itemIds);          // sai do projeto; o arquivo em disco nunca é tocado
    } catch (...) {
        finalizarGrupoUndo();
        throw;
    }
    finalizarGrupoUndo();
    juce::StringArray linhas;
    linhas.add("Files removed from the project (originals untouched): " + juce::String(static_cast<int>(pedidos.size())));
    registrarNoLog("Nest: files deleted", linhas);
}

void ProjetoAberto::definirCapaDoNest(const std::string& nestId, const std::string& itemId) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || nestId.empty() || itemId.empty()) return;
    auto& db = projeto_->registro();
    std::string capaAntiga;
    {
        auto st = db.prepare("SELECT COALESCE(n.capa_item_id, '') FROM nest n "
                             "WHERE n.id = ? AND EXISTS (SELECT 1 FROM nest_item WHERE nest_id = n.id AND item_id = ?)");
        st.bind(1, matriz::db::Value::of(nestId));
        st.bind(2, matriz::db::Value::of(itemId));
        if (!st.step()) return;  // o arquivo não é deste nest
        capaAntiga = st.columnText(0);
    }
    if (capaAntiga == itemId) return;
    db.run("UPDATE nest SET capa_item_id = ? WHERE id = ?", {matriz::db::Value::of(itemId), matriz::db::Value::of(nestId)});
    if (!capaAntiga.empty()) transferirMarcacoes(capaAntiga, itemId);  // o EXPORT segue o que está marcado
    if (!desfazendo_) {
        registrarUndo("Change Nest Cover", [this, nestId, capaAntiga, itemId]() {
            projeto_->registro().run("UPDATE nest SET capa_item_id = ? WHERE id = ?",
                                     {capaAntiga.empty() ? matriz::db::Value::null() : matriz::db::Value::of(capaAntiga),
                                      matriz::db::Value::of(nestId)});
            if (!capaAntiga.empty()) transferirMarcacoes(itemId, capaAntiga);
            EventBus::obterInstancia().dispararItemAlterado("", "nest");
        });
    }
    EventBus::obterInstancia().dispararItemAlterado("", "nest");
}

std::vector<std::string> ProjetoAberto::ordenarPorDataDeCriacao(const std::vector<std::string>& ids) const {
    if (!projeto_ || ids.size() < 2) return ids;
    std::vector<std::pair<std::pair<std::string, std::string>, std::string>> chaves;  // ((data, código), id)
    auto st = leitura().prepare(
        "SELECT COALESCE((SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'data_criacao'), "
        "                (SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'dc_created'), "
        "                i.criado_em), i.codigo_acervo FROM item i WHERE i.id = ?");
    for (const auto& id : ids) {
        st.reset();
        st.bind(1, matriz::db::Value::of(id));
        std::string data, codigo;
        if (st.step()) {
            data = st.columnIsNull(0) ? std::string() : st.columnText(0);
            codigo = st.columnIsNull(1) ? std::string() : st.columnText(1);
        }
        chaves.push_back({{data, codigo}, id});
    }
    std::sort(chaves.begin(), chaves.end());
    std::vector<std::string> out;
    out.reserve(chaves.size());
    for (auto& c : chaves) out.push_back(c.second);
    return out;
}

std::vector<std::string> ProjetoAberto::membrosDoNest(const std::string& nestId) const {
    std::vector<std::string> ids;
    if (!projeto_ || nestId.empty()) return ids;
    auto st = leitura().prepare("SELECT item_id FROM nest_item WHERE nest_id = ?");
    st.bind(1, matriz::db::Value::of(nestId));
    while (st.step()) ids.push_back(st.columnText(0));
    return ordenarPorDataDeCriacao(ids);
}

std::optional<ProjetoAberto::NestInfo> ProjetoAberto::nestDoItem(const std::string& itemId) const {
    if (!projeto_ || itemId.empty()) return std::nullopt;
    auto st = leitura().prepare("SELECT nest_id FROM nest_item WHERE item_id = ?");
    st.bind(1, matriz::db::Value::of(itemId));
    if (!st.step()) return std::nullopt;
    const std::string nid = st.columnText(0);
    const auto mapa = mapaDeNests(leitura());
    auto it = mapa.find(itemId);
    if (it == mapa.end() || it->second.nestId != nid) return std::nullopt;
    return it->second;
}

std::vector<std::string> ProjetoAberto::semMembrosNaoCapaDeNest(const std::vector<std::string>& itemIds) const {
    if (!projeto_ || itemIds.empty()) return itemIds;
    const auto nests = mapaDeNests(leitura());
    if (nests.empty()) return itemIds;
    std::vector<std::string> out;
    out.reserve(itemIds.size());
    std::set<std::string> ja(itemIds.begin(), itemIds.end());
    std::set<std::string> containersAcrescentados;
    for (const auto& id : itemIds) {
        auto it = nests.find(id);
        const bool container = it != nests.end() && !it->second.origem.empty();
        if (it == nests.end() || it->second.capaId == id || container) out.push_back(id);  // só NEST comum perde os não-capa
        // CONTAINER (nest com origem): sai o conjunto inteiro, não só a capa. Os membros que faltam entram aqui.
        if (container && containersAcrescentados.insert(it->second.nestId).second) {
            for (const auto& m : membrosDoNest(it->second.nestId))
                if (ja.insert(m).second) out.push_back(m);
        }
    }
    return out;
}

std::optional<std::vector<std::string>> ProjetoAberto::idsDoCatalogoSemNaoCapas() const {
    if (!projeto_) return std::nullopt;
    const auto nests = mapaDeNests(leitura());
    if (nests.empty()) return std::nullopt;
    std::vector<std::string> out;
    auto st = leitura().prepare("SELECT id FROM item WHERE COALESCE(em_quarentena, 0) = 0 AND COALESCE(estado, '') != 'duplicata'");
    while (st.step()) {
        const std::string id = st.columnText(0);
        auto it = nests.find(id);
        if (it == nests.end() || it->second.capaId == id || !it->second.origem.empty()) out.push_back(id);  // container: todos os membros
    }
    return out;
}

std::vector<std::string> ProjetoAberto::expandirMembrosDeNest(const std::vector<std::string>& itemIds) const {
    if (!projeto_ || itemIds.empty()) return itemIds;
    std::vector<std::string> out = itemIds;
    std::set<std::string> ja(itemIds.begin(), itemIds.end());
    std::set<std::string> nestsVistos;
    auto st = leitura().prepare("SELECT nest_id FROM nest_item WHERE item_id = ?");
    auto sm = leitura().prepare("SELECT item_id FROM nest_item WHERE nest_id = ?");
    for (const auto& id : itemIds) {
        st.reset();
        st.bind(1, matriz::db::Value::of(id));
        if (!st.step()) continue;
        const std::string nid = st.columnText(0);
        if (!nestsVistos.insert(nid).second) continue;
        sm.reset();
        sm.bind(1, matriz::db::Value::of(nid));
        while (sm.step()) {
            const std::string m = sm.columnText(0);
            if (ja.insert(m).second) out.push_back(m);
        }
    }
    return out;
}

std::set<std::string> ProjetoAberto::idsMarcadosR() const {
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    return marcadosR_;
}

void ProjetoAberto::carregarMarcasRDoBanco() {
    if (!projeto_) return;
    std::uint64_t versaoAntes = 0;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        versaoAntes = versaoMarcasR_;
    }
    std::set<std::string> doBanco;
    try {
        // i.em_quarentena = 1 (a coluna é NOT NULL DEFAULT 0): usa idx_item_quarentena_criado.
        auto st = leitura().prepare(
            "SELECT m.item_id FROM intake_marca_r m JOIN item i ON i.id = m.item_id WHERE i.em_quarentena = 1 AND COALESCE(i.estado, '') != 'duplicata'");
        while (st.step()) doBanco.insert(st.columnText(0));
    } catch (...) { return; }
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    // A memória manda enquanto há toque recente ou gravação pendente: o banco ainda está atrasado.
    if (versaoMarcasR_ != versaoAntes || gravacoesMarcasPendentes_ > 0) return;
    marcadosR_ = std::move(doBanco);
}

void ProjetoAberto::alternarMarcaR(const std::vector<std::string>& itemIds) {
    if (itemIds.empty() || !projeto_) return;
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    bool remover = true;
    std::set<std::string> marcadosAntes;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        for (const auto& id : itemIds) if (marcadosR_.count(id)) marcadosAntes.insert(id);
        for (const auto& id : itemIds)
            if (marcadosR_.count(id) == 0) { remover = false; break; }
        for (const auto& id : itemIds) {
            if (remover) marcadosR_.erase(id);
            else marcadosR_.insert(id);
        }
        ++versaoMarcasR_;
        ++gravacoesMarcasPendentes_;
    }
    // Undo (Intake, item a item): cada item volta a ter ou não ter R, pelo mesmo caminho (memória + fila serial).
    if (!desfazendo_) {  // persistente: ação "marca_r" com quem tinha R antes
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        juce::Array<juce::var> ids, marc;
        for (const auto& id : itemIds) ids.add(juce::String::fromUTF8(id.c_str()));
        for (const auto& id : marcadosAntes) marc.add(juce::String::fromUTF8(id.c_str()));
        o->setProperty("ids", ids);
        o->setProperty("marcados", marc);
        ParteUndo a;
        a.tipo = ParteUndo::Tipo::Acao;
        a.tabela = "marca_r";
        a.onde = juce::JSON::toString(juce::var(o.get()), true).toStdString();
        registrarUndoPersistente(itemIds.size() == 1 ? "Mark R" : "Mark R (" + std::to_string(itemIds.size()) + " items)", {a}, {}, "marca_r");
    }
    const std::string agora = matriz::model::agoraIso8601();
    std::weak_ptr<bool> vivo = vivo_;
    poolMarcas_.addJob([this, itemIds, remover, agora, vivo]() {
        bool ok = true;
        try {
            auto& db = projeto_->registro();
            db.exec("BEGIN IMMEDIATE");
            try {
                for (const auto& id : itemIds) {
                    if (remover)
                        db.run("DELETE FROM intake_marca_r WHERE item_id = ?", {matriz::db::Value::of(id)});
                    else
                        db.run("INSERT OR IGNORE INTO intake_marca_r (item_id, origem, marcado_em) VALUES (?, 'usuario', ?)",
                               {matriz::db::Value::of(id), matriz::db::Value::of(agora)});
                }
                db.exec("COMMIT");
            } catch (...) {
                try { db.exec("ROLLBACK"); } catch (...) {}
                ok = false;
            }
        } catch (...) { ok = false; }
        {
            std::lock_guard<std::mutex> lock(marcacoesMutex_);
            if (!ok) {  // volta ao estado anterior
                for (const auto& id : itemIds) {
                    if (remover) marcadosR_.insert(id);
                    else marcadosR_.erase(id);
                }
                ++versaoMarcasR_;
            }
            --gravacoesMarcasPendentes_;
        }
        if (!ok) {
            juce::MessageManager::callAsync([this, vivo, itemIds]() {
                if (vivo.expired()) return;
                for (const auto& id : itemIds)
                    EventBus::obterInstancia().dispararItemAlterado(id, "marca_r");
                if (aoMarcasRRevertidas) aoMarcasRRevertidas();
                const bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("R")
                        .withMessage(isPt ? juce::String::fromUTF8("N\xc3\xa3o foi poss\xc3\xadvel gravar a marca R; a altera\xc3\xa7\xc3\xa3o foi desfeita.")
                                          : juce::String("Could not save the R mark; the change was reverted."))
                        .withButton("OK"),
                    nullptr);
            });
        }
    });
}

int ProjetoAberto::rejeitarMarcadosR() {
    if (!projeto_) return 0;
    if (somenteLeitura_) { avisarSomenteLeitura(); return 0; }
    const auto marcados = idsMarcadosR();
    if (marcados.empty()) return 0;
    const std::vector<std::string> ids(marcados.begin(), marcados.end());
    auto& db = projeto_->registro();

    // SHA-256 (e nome) de cada um, lidos ANTES de o item sair — depois não há mais de onde ler.
    std::vector<std::pair<std::string, std::string>> hashes;
    {
        auto st = db.prepare("SELECT checksum_sha256, caminho_relativo FROM arquivo "
                             "WHERE item_id = ? AND checksum_sha256 IS NOT NULL AND checksum_sha256 != ''");
        for (const auto& id : ids) {
            st.reset();
            st.bind(1, matriz::db::Value::of(id));
            while (st.step())
                hashes.push_back({st.columnText(0), juce::File(juce::String(st.columnText(1))).getFileName().toStdString()});
        }
    }

    // Um Cmd+Z desfaz tudo junto: os itens voltam e os hashes novos saem da lista.
    iniciarGrupoUndo(ids.size() == 1 ? "Reject Marked File" : "Reject " + std::to_string(ids.size()) + " Marked Files");
    try {
        removerItensDoProjeto(ids);
    } catch (...) {
        finalizarGrupoUndo();
        throw;
    }

    std::vector<std::string> novos;
    const std::string agora = matriz::model::agoraIso8601();
    db.run("BEGIN TRANSACTION", {});
    try {
        auto existe = db.prepare("SELECT 1 FROM intake_rejeitados WHERE sha256 = ?");
        for (const auto& [sha, nome] : hashes) {
            existe.reset();
            existe.bind(1, matriz::db::Value::of(sha));
            if (existe.step()) continue;
            db.run("INSERT INTO intake_rejeitados (sha256, nome_original, rejeitado_em) VALUES (?, ?, ?)",
                   {matriz::db::Value::of(sha), matriz::db::Value::of(nome), matriz::db::Value::of(agora)});
            novos.push_back(sha);
        }
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        novos.clear();
    }
    if (!novos.empty()) {
        registrarUndo("Reject Marked Files", [this, novos]() {
            auto& d = projeto_->registro();
            d.run("BEGIN TRANSACTION", {});
            try {
                for (const auto& sha : novos)
                    d.run("DELETE FROM intake_rejeitados WHERE sha256 = ?", {matriz::db::Value::of(sha)});
                d.run("COMMIT", {});
            } catch (...) { try { d.run("ROLLBACK", {}); } catch (...) {} }
        });
    }
    finalizarGrupoUndo();
    return static_cast<int>(ids.size());
}

void ProjetoAberto::renomearItens(const std::vector<std::string>& itemIds, const std::string& novoTitulo) {
    std::vector<std::pair<std::string, std::string>> itemETitulo;
    itemETitulo.reserve(itemIds.size());
    for (const auto& id : itemIds) itemETitulo.emplace_back(id, novoTitulo);
    renomearItensComTitulos(itemETitulo);
}

void ProjetoAberto::renomearItensComTitulos(const std::vector<std::pair<std::string, std::string>>& itemETitulo) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemETitulo.empty()) return;
    if (!desfazendo_) {
        std::vector<std::pair<std::string, std::string>> antigosTitulos;
        for (const auto& par : itemETitulo) {
            const auto& id = par.first;
            auto stmt = projeto_->registro().prepare("SELECT titulo FROM item WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(id));
            if (stmt.step()) antigosTitulos.push_back({id, stmt.columnText(0)});
        }
        registrarUndo("Rename Items", [this, antigosTitulos]() {
            renomearItensComTitulos(antigosTitulos);
        });
    }
    std::string agora = matriz::model::agoraIso8601();
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& [itemId, novoTitulo] : itemETitulo) {
            juce::String tituloNovoTrim = juce::String(novoTitulo).trim();
            auto stmtAntigo = db.prepare("SELECT titulo, notas_livres FROM item WHERE id = ?");
            stmtAntigo.bind(1, matriz::db::Value::of(itemId));
            std::string tituloAntigo;
            std::string notasAtuais;
            if (stmtAntigo.step()) {
                tituloAntigo = stmtAntigo.columnText(0);
                if (!stmtAntigo.columnIsNull(1)) notasAtuais = stmtAntigo.columnText(1);
            }

            db.run("UPDATE item SET titulo = ?, atualizado_em = ? WHERE id = ?",
                   {matriz::db::Value::of(novoTitulo), matriz::db::Value::of(agora),
                    matriz::db::Value::of(itemId)});

            bool tituloRealmenteMudou = tituloNovoTrim != juce::String(tituloAntigo).trim();

            if (!desfazendo_ && tituloRealmenteMudou && !tituloAntigo.empty()) {
                std::string linhaNota = "Previous Name: " + tituloAntigo;
                std::string notasNovas = notasAtuais.empty() ? linhaNota : (notasAtuais + "\n" + linhaNota);
                db.run("UPDATE item SET notas_livres = ? WHERE id = ?",
                       {matriz::db::Value::of(notasNovas), matriz::db::Value::of(itemId)});
            }

            // Modelo SOURCE/MAIN (etapa 5): o nome físico de um arquivo já no
            // MAIN nunca muda — renomear o item muda só o catálogo (e o nome
            // "Previous Name" nas notas). Antes, sincronizarNomeDeBackupAposRenomear
            // renomeava a cópia no backup a cada edição de título.
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }

    // UM evento de lote, depois do COMMIT (antes: um por item, com a transação ainda aberta).
    std::vector<std::string> ids;
    ids.reserve(itemETitulo.size());
    for (const auto& par : itemETitulo) ids.push_back(par.first);
    EventBus::obterInstancia().dispararItensAlterados(ids, "titulo");
}

void ProjetoAberto::registrarUndoRevisado(const std::vector<std::string>& itemIds) {
    if (!projeto_ || itemIds.empty() || desfazendo_) return;
    registrarUndoDeColunasDosItens(itemIds.size() == 1 ? "Mark E" : "Mark E (" + std::to_string(itemIds.size()) + " items)",
                                   {"marcado_revisado"}, itemIds, "marcado_revisado");
}

void ProjetoAberto::alternarMarcadoRevisado(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    registrarUndoRevisado(itemIds);
    bool todosMarcados = true;
    for (const auto& id : itemIds) {
        auto stmt = projeto_->registro().prepare("SELECT marcado_revisado FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(id));
        if (!stmt.step() || stmt.columnIsNull(0) || stmt.columnInt(0) == 0) {
            todosMarcados = false;
            break;
        }
    }
    int novoValor = todosMarcados ? 0 : 1;
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& id : itemIds) {
            db.run("UPDATE item SET marcado_revisado = ? WHERE id = ?",
                   {matriz::db::Value::of(novoValor), matriz::db::Value::of(id)});
        }
        db.run("COMMIT", {});
    } catch (...) {
        db.run("ROLLBACK", {});
        throw;
    }
    for (const auto& id : itemIds) {
        EventBus::obterInstancia().dispararItemAlterado(id, "marcado_revisado");
    }
}

bool ProjetoAberto::itemMarcadoRevisado(const std::string& itemId) const {
    if (!projeto_ || itemId.empty()) return false;
    try {
        auto stmt = leitura().prepare("SELECT COALESCE(marcado_revisado, 0) != 0 FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) return stmt.columnInt(0) != 0;
    } catch (...) {}
    return false;
}

void ProjetoAberto::limparMarcadoRevisadoDe(const std::vector<std::string>& itemIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty()) return;
    registrarUndoRevisado(itemIds);
    auto& db = projeto_->registro();
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& id : itemIds)
            db.run("UPDATE item SET marcado_revisado = 0 WHERE id = ? AND marcado_revisado != 0", {matriz::db::Value::of(id)});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        throw;
    }
    for (const auto& id : itemIds) EventBus::obterInstancia().dispararItemAlterado(id, "marcado_revisado");
}

void ProjetoAberto::limparTodosMarcadosRevisado() {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    projeto_->registro().run("UPDATE item SET marcado_revisado = 0 WHERE marcado_revisado != 0", {});
    EventBus::obterInstancia().dispararItemAlterado("", "marcado_revisado");
}

std::vector<ProjetoAberto::AlvoArquivo> ProjetoAberto::alvosArquivoPrincipal(const std::vector<std::string>& itemIds) const {
    std::vector<AlvoArquivo> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        std::string("SELECT a.id, ") + matriz::vault::colunasDeResolucao() + " FROM arquivo a " +
        matriz::vault::joinDeResolucao() + " WHERE a.item_id = ? ORDER BY a.eh_master DESC, a.id LIMIT 1");
    for (const auto& itemId : itemIds) {
        stmt.reset();
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (!stmt.step()) continue;
        out.push_back({itemId, stmt.columnText(0), stmt.columnText(1), stmt.columnText(2), stmt.columnText(3)});
    }
    return out;
}

std::shared_ptr<matriz::vault::ResolvedorEmLote> ProjetoAberto::criarResolvedorEmLote() const {
    if (!projeto_) return nullptr;
    return std::make_shared<matriz::vault::ResolvedorEmLote>(projeto_->registro(), projeto_->pasta());
}

void ProjetoAberto::registrarUndoDeColunasDosItens(const std::string& descricao, std::vector<std::string> colunas,
                                                   const std::vector<std::string>& itemIds, const std::string& tipoEvento) {
    if (itemIds.empty() || colunas.empty() || desfazendo_) return;
    ParteUndo p;
    p.tipo = ParteUndo::Tipo::Colunas;
    p.tabela = "item";
    p.onde = "id IN (SELECT value FROM json_each(?))";
    p.params = {matriz::db::Value::of(idsEmJson(itemIds))};
    p.colunas = std::move(colunas);
    p.chave = "id";
    registrarUndoPersistente(descricao, {p}, itemIds, tipoEvento);
}

int ProjetoAberto::gravarOutraMetadataEmLote(const std::map<std::string, std::string>& textoPorItem) {
    if (!projeto_ || textoPorItem.empty()) return 0;
    auto& db = projeto_->registro();
    const std::string agora = matriz::model::agoraIso8601();
    std::unique_lock<std::recursive_mutex> writeLock(projeto_->writeMutex());
    int gravados = 0;
    {   // Undo (antes de gravar): a nota de cada item como está agora.
        std::vector<std::string> ids;
        for (const auto& [id, texto] : textoPorItem) ids.push_back(id);
        registrarUndoDeColunasDosItens(ids.size() == 1 ? "Other Metadata" : "Other Metadata (" + std::to_string(ids.size()) + " items)",
                                       {"notas_livres"}, ids);
    }
    try {
        db.exec("BEGIN IMMEDIATE");
        auto ler = db.prepare("SELECT notas_livres FROM item WHERE id = ?");
        for (const auto& [itemId, texto] : textoPorItem) {
            ler.reset();
            ler.bind(1, matriz::db::Value::of(itemId));
            std::string atual;
            if (ler.step() && !ler.columnIsNull(0)) atual = ler.columnText(0);
            // A automática vai sempre primeiro (como no ingest); as seções do
            // usuário ficam como estavam.
            std::vector<matriz::model::SecaoNota> secoes = {{matriz::model::kOutraMetadataTitulo, texto, true}};
            for (auto& sec : matriz::model::parseNotasEstruturadas(atual))
                if (!sec.automatica) secoes.push_back(std::move(sec));
            db.run("UPDATE item SET notas_livres = ?, atualizado_em = ? WHERE id = ?",
                   {matriz::db::Value::of(matriz::model::serializarNotasEstruturadas(secoes)),
                    matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
            ++gravados;
        }
        db.exec("COMMIT");
    } catch (...) {
        try { db.exec("ROLLBACK"); } catch (...) {}
        return 0;
    }
    return gravados;
}

std::optional<juce::String> ProjetoAberto::caminhoDeOrigem(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;

    // 1. Try resolving via arquivo table with vault resolution
    try {
        auto stmt = projeto_->registro().prepare(
            std::string("SELECT ") + matriz::vault::colunasDeResolucao() +
            " FROM arquivo a " + matriz::vault::joinDeResolucao() +
            " WHERE a.item_id = ? ORDER BY a.eh_master DESC, a.id LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) {
            std::string locVault = stmt.columnText(0);
            std::string camRel = stmt.columnText(1);
            std::string camAbs = stmt.columnText(2);

            auto f = matriz::vault::resolverCaminho(projeto_->pasta(), locVault, camRel, camAbs);
            if (f && f->existsAsFile()) {
                return f->getFullPathName();
            }
            auto fEsp = matriz::vault::caminhoEsperado(projeto_->pasta(), locVault, camRel, camAbs);
            if (fEsp != juce::File() && fEsp.getFullPathName().isNotEmpty()) {
                return fEsp.getFullPathName();
            }
            if (!camAbs.empty()) {
                return juce::String(camAbs);
            }
        }
    } catch (...) {}

    // 2. Try simple caminho_absoluto_origem query directly
    try {
        auto stmt2 = projeto_->registro().prepare(
            "SELECT caminho_absoluto_origem FROM arquivo WHERE item_id = ? AND caminho_absoluto_origem IS NOT NULL AND caminho_absoluto_origem != '' "
            "ORDER BY eh_master DESC, id LIMIT 1");
        stmt2.bind(1, matriz::db::Value::of(itemId));
        if (stmt2.step()) {
            juce::String c = stmt2.columnText(0);
            if (c.isNotEmpty()) return c;
        }
    } catch (...) {}

    // 3. Try localizacao_conhecida
    try {
        auto stmtLoc = projeto_->registro().prepare(
            "SELECT lc.caminho_absoluto FROM localizacao_conhecida lc "
            "JOIN arquivo a ON a.id = lc.arquivo_id WHERE a.item_id = ? "
            "ORDER BY lc.criado_em DESC LIMIT 1");
        stmtLoc.bind(1, matriz::db::Value::of(itemId));
        if (stmtLoc.step()) {
            juce::String c = stmtLoc.columnText(0);
            if (c.isNotEmpty()) return c;
        }
    } catch (...) {}

    // 4. Try caminho_catalogo in item table
    try {
        auto stmtItem = projeto_->registro().prepare("SELECT caminho_catalogo FROM item WHERE id = ?");
        stmtItem.bind(1, matriz::db::Value::of(itemId));
        if (stmtItem.step()) {
            juce::String camCat = stmtItem.columnText(0);
            if (camCat.isNotEmpty()) return camCat;
        }
    } catch (...) {}

    // 5. Try item_campo metadata
    try {
        auto stmtCampo = projeto_->registro().prepare(
            "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id IN ('caminho', 'caminho_absoluto', 'caminho_origem', 'path') LIMIT 1");
        stmtCampo.bind(1, matriz::db::Value::of(itemId));
        if (stmtCampo.step()) {
            juce::String val = stmtCampo.columnText(0);
            if (val.isNotEmpty()) return val;
        }
    } catch (...) {}

    // 6. Try relative path resolution directly against disk / volumes
    try {
        auto stmtRel = projeto_->registro().prepare(
            "SELECT caminho_relativo FROM arquivo WHERE item_id = ? AND caminho_relativo IS NOT NULL AND caminho_relativo != '' "
            "ORDER BY eh_master DESC, id LIMIT 1");
        stmtRel.bind(1, matriz::db::Value::of(itemId));
        if (stmtRel.step()) {
            juce::String rel = stmtRel.columnText(0);
            if (rel.isNotEmpty()) {
                if (juce::File::isAbsolutePath(rel)) {
                    juce::File f(rel);
                    if (f.existsAsFile() || f.isDirectory()) return f.getFullPathName();
                }
                juce::File inProj = projeto_->pasta().getChildFile(rel);
                if (inProj.existsAsFile() || inProj.isDirectory()) return inProj.getFullPathName();

                juce::File inVol = juce::File("/Volumes").getChildFile(rel);
                if (inVol.existsAsFile() || inVol.isDirectory()) return inVol.getFullPathName();
                
                return rel;
            }
        }
    } catch (...) {}

    return std::nullopt;
}

std::set<std::string> ProjetoAberto::itensComMesmoConteudo(const std::string& itemId) const {
    std::set<std::string> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT DISTINCT a2.item_id FROM arquivo a1 "
        "JOIN arquivo a2 ON a2.checksum_sha256 = a1.checksum_sha256 AND a2.tamanho_bytes = a1.tamanho_bytes "
        "WHERE a1.item_id = ? AND a1.checksum_sha256 IS NOT NULL AND a1.checksum_sha256 <> '' "
        "AND a1.tamanho_bytes > 0 AND a2.tamanho_bytes > 0");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.insert(stmt.columnText(0));

    // Só o próprio item = não há duplicata; devolver {ele} faria a grade
    // filtrar pra um item só e parecer que "achou" alguma coisa.
    if (out.size() <= 1) out.clear();
    return out;
}

std::vector<ProjetoAberto::ParDuplicatas> ProjetoAberto::listarGruposDuplicados() const {
    std::vector<ParDuplicatas> out;
    if (!projeto_) return out;

    struct DbInfo {
        std::string itemId;
        std::string codigoAcervo;
        std::string titulo;
        std::string tipoMidia;
        std::string estado;
        juce::String caminhoRelativo;
        juce::String caminhoOrigem;
        juce::int64 tamanhoBytes = 0;
        std::string checksumSha256;
    };

    std::vector<DbInfo> arquivos;

    auto stmt = leitura().prepare(
        "SELECT a.item_id, i.codigo_acervo, i.titulo, i.tipo_midia, i.estado, "
        "a.caminho_relativo, a.caminho_absoluto_origem, a.tamanho_bytes, a.checksum_sha256 "
        "FROM arquivo a "
        "JOIN item i ON i.id = a.item_id "
        "WHERE a.eh_master = 1 AND i.estado <> 'duplicata'");

    while (stmt.step()) {
        DbInfo inf;
        inf.itemId = stmt.columnText(0);
        inf.codigoAcervo = stmt.columnIsNull(1) ? "" : stmt.columnText(1);
        inf.titulo = stmt.columnText(2);
        inf.tipoMidia = stmt.columnIsNull(3) ? "" : stmt.columnText(3);
        inf.estado = stmt.columnText(4);
        inf.caminhoRelativo = stmt.columnText(5);
        inf.caminhoOrigem = stmt.columnIsNull(6) ? "" : stmt.columnText(6);
        inf.tamanhoBytes = stmt.columnIsNull(7) ? 0 : static_cast<juce::int64>(stmt.columnInt(7));
        inf.checksumSha256 = stmt.columnIsNull(8) ? "" : stmt.columnText(8);

        if (inf.tamanhoBytes > 0 && !inf.checksumSha256.empty()) {
            arquivos.push_back(inf);
        }
    }

    auto extrairNome = [](const juce::String& rel, const juce::String& orig) -> juce::String {
        juce::String caminho = orig.isNotEmpty() ? orig : rel;
        int slashPos = std::max(caminho.lastIndexOfChar('/'), caminho.lastIndexOfChar('\\'));
        return slashPos >= 0 ? caminho.substring(slashPos + 1) : caminho;
    };

    std::map<std::tuple<juce::String, juce::int64, std::string>, std::vector<DbInfo>> grupos;
    for (const auto& a : arquivos) {
        juce::String fname = extrairNome(a.caminhoRelativo, a.caminhoOrigem).toLowerCase();
        auto key = std::make_tuple(fname, a.tamanhoBytes, a.checksumSha256);
        grupos[key].push_back(a);
    }

    for (const auto& [key, itensGrupo] : grupos) {
        std::set<std::string> itemIds;
        for (const auto& item : itensGrupo) {
            itemIds.insert(item.itemId);
        }

        if (itemIds.size() > 1) {
            ParDuplicatas grupo;
            const auto& firstItem = itensGrupo.front();
            grupo.filename = extrairNome(firstItem.caminhoRelativo, firstItem.caminhoOrigem);
            grupo.tamanhoBytes = std::get<1>(key);
            grupo.checksumSha256 = std::get<2>(key);

            for (const auto& dbItem : itensGrupo) {
                ParDuplicatas::ItemInfo info;
                info.id = dbItem.itemId;
                info.codigoAcervo = dbItem.codigoAcervo;
                info.titulo = dbItem.titulo;
                info.tipoMidia = dbItem.tipoMidia;
                info.estado = dbItem.estado;
                info.caminhoRelativo = dbItem.caminhoRelativo;
                info.caminhoOrigem = dbItem.caminhoOrigem;
                info.tamanhoBytes = dbItem.tamanhoBytes;
                grupo.itens.push_back(info);
            }
            out.push_back(std::move(grupo));
        }
    }

    return out;
}

void ProjetoAberto::atualizarEstadoItem(const std::string& itemId, const std::string& novoEstado) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    registrarUndoDeColunasDosItens("Change State", {"estado"}, {itemId});
    std::string agora = matriz::model::agoraIso8601();
    projeto_->registro().run("UPDATE item SET estado = ?, atualizado_em = ? WHERE id = ?",
                             {matriz::db::Value::of(novoEstado), matriz::db::Value::of(agora),
                              matriz::db::Value::of(itemId)});
}

int ProjetoAberto::replicarSubarvoreNoAcervo(const NoArvore& origem, const std::string& pastaPaiId,
                                              bool manterEstrutura, const std::string& mapaId) {
    if (!projeto_ || mapaId.empty() || mapaId == kMapaOriginal) return 0;
    // Um passo de Undo para a cópia inteira: o retrato do mapa (pastas + vínculos) cobre as pastas novas, a cor
    // copiada e os itens ligados; os Undos de criarPastaAcervo/adicionarItensAPasta entram no mesmo grupo.
    iniciarGrupoUndo("Copy Folder Structure");
    struct FimDoGrupo {
        ProjetoAberto* p;
        ~FimDoGrupo() { p->finalizarGrupoUndo(); }
    } fimDoGrupo{this};
    registrarUndoSnapshotMapa("Copy Folder Structure", mapaId);
    if (!manterEstrutura) {
        std::vector<std::string> ids(origem.itemIds.begin(), origem.itemIds.end());
        if (ids.empty()) return 0;
        std::string destino = pastaPaiId;
        if (destino.empty())
            destino = criarPastaAcervo(origem.nome.toStdString(), std::nullopt, mapaId);
        adicionarItensAPasta(ids, destino);
        return static_cast<int>(ids.size());
    }

    // Manter estrutura (padrão). Uma transação só: replicar um catálogo com
    // milhares de pastas em auto-commit levaria um fsync por INSERT, e uma
    // falha no meio deixaria meia hierarquia montada.
    projeto_->registro().run("BEGIN", {});
    int vinculados = 0;
    try {
        // Pilha explícita em vez de recursão: uma hierarquia de disco pode
        // ser bem funda, e estourar a pilha nativa no meio de uma transação
        // seria o pior lugar possível pra descobrir isso.
        struct Pendente {
            const NoArvore* no;
            std::string pastaPaiId;
        };
        std::vector<Pendente> pilha{{&origem, pastaPaiId}};

        while (!pilha.empty()) {
            Pendente atual = pilha.back();
            pilha.pop_back();

            // A própria pasta arrastada também é recriada no destino — é o
            // que faz "arrastar a pasta X" produzir X lá dentro, e não só o
            // conteúdo dela derramado na pasta de destino.
            std::optional<std::string> pai =
                atual.pastaPaiId.empty() ? std::nullopt : std::optional(atual.pastaPaiId);
            std::string novaPastaId = criarPastaAcervo(atual.no->nome.toStdString(), pai, mapaId);

            // A cópia leva o esquema de cores (FOLDER COLOR) da pasta de origem.
            if (atual.no->corCustomizadaHex.isNotEmpty())
                projeto_->registro().run("UPDATE acervo_pasta SET cor_customizada = ? WHERE id = ?",
                                         {matriz::db::Value::of(atual.no->corCustomizadaHex.toStdString()),
                                          matriz::db::Value::of(novaPastaId)});

            if (!atual.no->itemIdsDiretos.empty()) {
                std::string agoraItem = matriz::model::agoraIso8601();
                for (const auto& itemId : atual.no->itemIdsDiretos) {
                    // Só tira o item das pastas DESTE mapa (Fase 1).
                    projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?",
                                             {matriz::db::Value::of(itemId), matriz::db::Value::of(mapaId)});
                    inserirItemPastaInterno(itemId, novaPastaId, agoraItem);
                }
                vinculados += static_cast<int>(atual.no->itemIdsDiretos.size());
            }

            for (auto& filho : atual.no->filhos) pilha.push_back({&filho, novaPastaId});
        }
        projeto_->registro().run("COMMIT", {});
    } catch (...) {
        projeto_->registro().run("ROLLBACK", {});
        throw;
    }
    return vinculados;
}

void ProjetoAberto::removerItemDaPasta(const std::string& itemId, const std::string& pastaId) {
    removerItensDasPastas({itemId}, {pastaId});
}

void ProjetoAberto::removerItensDasPastas(const std::vector<std::string>& itemIds, const std::set<std::string>& pastaIds) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_ || itemIds.empty() || pastaIds.empty()) return;
    registrarUndoVinculosDePasta(itemIds.size() == 1 ? "Remove from Folder" : "Remove " + std::to_string(itemIds.size()) + " Items from Folder",
                                 itemIds, {});
    auto& db = projeto_->registro();
    std::unique_lock<std::recursive_mutex> lock(writeMutex());
    db.run("BEGIN TRANSACTION", {});
    try {
        for (const auto& itemId : itemIds)
            for (const auto& pastaId : pastaIds)
                db.run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND pasta_id = ?",
                       {matriz::db::Value::of(itemId), matriz::db::Value::of(pastaId)});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}  // quem chama (menu da janela da pasta) não trata exceção
    }
}

std::set<std::string> ProjetoAberto::buscarItens(const juce::String& texto, EscopoBusca escopo) const {
    std::set<std::string> out;
    if (!projeto_) return out;
    juce::String termo = texto.trim();
    if (termo.isEmpty()) return out;
    std::string projetoId = projeto_->projetoId();
    if (escopo != EscopoBusca::Todos) return buscarItensNoEscopo(termo, escopo);

    juce::StringArray tokens;
    tokens.addTokens(termo, " \t\r\n,", "\"");

    if (tokens.isEmpty()) return out;

    bool primeiroToken = true;

    for (auto token : tokens) {
        token = token.trim().unquoted();
        if (token.isEmpty()) continue;

        std::set<std::string> matchesParaToken;

        // 1. FTS5 search for this token
        juce::String cleanToken = token.trimCharactersAtStart("#").replace("\"", "\"\"");
        if (cleanToken.isNotEmpty()) {
            juce::String ftsQuery = "\"" + cleanToken + "\"*";
            try {
                auto stmt = leitura().prepare(
                    "SELECT DISTINCT b.item_id FROM busca_fts b "
                    "JOIN item i ON i.id = b.item_id "
                    "WHERE i.projeto_id = ? AND busca_fts MATCH ?");
                stmt.bind(1, matriz::db::Value::of(projetoId));
                stmt.bind(2, matriz::db::Value::of(ftsQuery.toStdString()));
                while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
            } catch (...) {}
        }

        // 2. Direct SQL search across all metadata tables and columns (case-insensitive LIKE)
        std::string pattern = "%" + token.replace("%", "\\%").replace("_", "\\_").toStdString() + "%";
        std::string cleanPattern = "%" + token.trimCharactersAtStart("#").replace("%", "\\%").replace("_", "\\_").toStdString() + "%";

        // a) item table columns
        try {
            auto stmt = leitura().prepare(
                "SELECT id FROM item "
                "WHERE projeto_id = ? AND ("
                "   titulo LIKE ? ESCAPE '\\' OR "
                "   codigo_acervo LIKE ? ESCAPE '\\' OR "
                "   persistent_id LIKE ? ESCAPE '\\' OR "
                "   notas_livres LIKE ? ESCAPE '\\' OR "
                "   content_type LIKE ? ESCAPE '\\' OR "
                "   collection_type LIKE ? ESCAPE '\\' OR "
                "   isrc LIKE ? ESCAPE '\\' OR "
                "   tipo_midia LIKE ? ESCAPE '\\' OR "
                "   estado LIKE ? ESCAPE '\\'"
                ")");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            for (int k = 2; k <= 10; ++k) stmt.bind(k, matriz::db::Value::of(pattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // b) item_campo (all metadata fields: artist, creator, description, year, custom YAML fields, etc.)
        try {
            auto stmt = leitura().prepare(
                "SELECT c.item_id FROM item_campo c "
                "JOIN item i ON i.id = c.item_id "
                "WHERE i.projeto_id = ? AND c.valor LIKE ? ESCAPE '\\'");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(cleanPattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // c) item_tag (tags)
        try {
            auto stmt = leitura().prepare(
                "SELECT t.item_id FROM item_tag t "
                "JOIN item i ON i.id = t.item_id "
                "WHERE i.projeto_id = ? AND t.tag LIKE ? ESCAPE '\\'");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(cleanPattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // d) item_observacao (notes)
        try {
            auto stmt = leitura().prepare(
                "SELECT o.item_id FROM item_observacao o "
                "JOIN item i ON i.id = o.item_id "
                "WHERE i.projeto_id = ? AND o.texto LIKE ? ESCAPE '\\'");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(pattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // e) arquivo (filename, relative path, origin absolute path)
        try {
            auto stmt = leitura().prepare(
                "SELECT a.item_id FROM arquivo a "
                "JOIN item i ON i.id = a.item_id "
                "WHERE i.projeto_id = ? AND ("
                "   a.caminho_relativo LIKE ? ESCAPE '\\' OR "
                "   a.caminho_absoluto_origem LIKE ? ESCAPE '\\'"
                ")");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(pattern));
            stmt.bind(3, matriz::db::Value::of(pattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // f) item_assunto / assunto
        try {
            auto stmt = leitura().prepare(
                "SELECT ia.item_id FROM item_assunto ia "
                "JOIN assunto s ON s.id = ia.assunto_id "
                "JOIN item i ON i.id = ia.item_id "
                "WHERE i.projeto_id = ? AND s.termo LIKE ? ESCAPE '\\'");
            stmt.bind(1, matriz::db::Value::of(projetoId));
            stmt.bind(2, matriz::db::Value::of(pattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // g) CONTAINER: nome do container e tipo (rotulo do tipo no idioma ativo): todos os arquivos dele casam.
        try {
            std::string idsDeTipo, origensDeTipo;
            for (const auto& t : tiposDeContainer()) {
                if (!rotuloDoTipoContainer(t).containsIgnoreCase(cleanToken)) continue;
                idsDeTipo += (idsDeTipo.empty() ? "'" : ",'") + std::string(t.id) + "'";
                if (t.temPlayer()) origensDeTipo += (origensDeTipo.empty() ? "'" : ",'") + std::string(t.origemPlayer) + "'";
            }
            std::string sql = "SELECT ni.item_id FROM nest_item ni JOIN nest n ON n.id = ni.nest_id WHERE COALESCE(n.origem, '') <> '' AND (n.nome LIKE ? ESCAPE '\\'";
            if (!idsDeTipo.empty()) sql += " OR n.tipo_container IN (" + idsDeTipo + ")";
            if (!origensDeTipo.empty()) sql += " OR (COALESCE(n.tipo_container, '') = '' AND n.origem IN (" + origensDeTipo + "))";
            sql += ")";
            auto stmt = leitura().prepare(sql);
            stmt.bind(1, matriz::db::Value::of(cleanPattern));
            while (stmt.step()) matchesParaToken.insert(stmt.columnText(0));
        } catch (...) {}

        // Intersect with overall result (AND logic across multiple words)
        if (primeiroToken) {
            out = std::move(matchesParaToken);
            primeiroToken = false;
        } else {
            std::set<std::string> inter;
            for (const auto& id : out) {
                if (matchesParaToken.count(id)) inter.insert(id);
            }
            out = std::move(inter);
        }

        if (out.empty()) break;
    }

    return out;
}

std::set<std::string> ProjetoAberto::buscarItensNoEscopo(const juce::String& termo, EscopoBusca escopo) const {
    // Um conjunto por palavra, intersectados (E lógico, como a busca geral);
    // cada palavra procurada só nos campos do escopo.
    std::set<std::string> out;
    juce::StringArray tokens;
    tokens.addTokens(termo, " \t\r\n,", "\"");
    tokens.removeEmptyStrings();
    const std::string pid = projeto_->projetoId();
    bool primeiro = true;
    for (auto token : tokens) {
        token = token.trim().unquoted().trimCharactersAtStart("#");
        if (token.isEmpty()) continue;
        const std::string like = "%" + token.replace("%", "\\%").replace("_", "\\_").toStdString() + "%";
        std::vector<std::pair<std::string, std::vector<std::string>>> consultas;  // SQL (1º ? = projeto), padrões
        const std::string doItem = "SELECT i.id FROM item i WHERE i.projeto_id = ? AND (";
        switch (escopo) {
            case EscopoBusca::NomeArquivo:
                consultas.push_back({doItem + "i.titulo LIKE ? ESCAPE '\\' OR i.codigo_acervo LIKE ? ESCAPE '\\')", {like, like}});
                consultas.push_back({"SELECT a.item_id FROM arquivo a JOIN item i ON i.id = a.item_id WHERE i.projeto_id = ? AND "
                                     "(a.caminho_relativo LIKE ? ESCAPE '\\' OR a.caminho_absoluto_origem LIKE ? ESCAPE '\\')", {like, like}});
                break;
            case EscopoBusca::Criador:
                consultas.push_back({doItem + "i.dc_creator LIKE ? ESCAPE '\\')", {like}});
                consultas.push_back({"SELECT c.item_id FROM item_campo c JOIN item i ON i.id = c.item_id WHERE i.projeto_id = ? AND "
                                     "c.campo_id IN ('dc_creator', 'artista_principal', 'creator', 'autor') AND c.valor LIKE ? ESCAPE '\\'", {like}});
                break;
            case EscopoBusca::Assunto:
                consultas.push_back({doItem + "i.dc_subject LIKE ? ESCAPE '\\')", {like}});
                consultas.push_back({"SELECT ia.item_id FROM item_assunto ia JOIN assunto s ON s.id = ia.assunto_id JOIN item i ON i.id = ia.item_id "
                                     "WHERE i.projeto_id = ? AND s.termo LIKE ? ESCAPE '\\'", {like}});
                break;
            case EscopoBusca::Conteudo: {
                // Aceita o nome em português (o banco guarda em inglês).
                const std::string likeEn = "%" + traduzirContent(token, false).toStdString() + "%";
                consultas.push_back({doItem + "i.collection_type LIKE ? ESCAPE '\\' OR i.content_type LIKE ? ESCAPE '\\' "
                                              "OR i.collection_type LIKE ? OR i.content_type LIKE ?)", {like, like, likeEn, likeEn}});
                break;
            }
            case EscopoBusca::PessoasTags:
                consultas.push_back({"SELECT t.item_id FROM item_tag t JOIN item i ON i.id = t.item_id WHERE i.projeto_id = ? AND "
                                     "t.tag LIKE ? ESCAPE '\\'", {like}});
                consultas.push_back({doItem + "i.dc_contributor LIKE ? ESCAPE '\\')", {like}});
                consultas.push_back({"SELECT c.item_id FROM item_campo c JOIN item i ON i.id = c.item_id WHERE i.projeto_id = ? AND "
                                     "(c.campo_id LIKE '%pesso%' OR c.campo_id LIKE '%people%' OR c.campo_id LIKE '%contributor%') "
                                     "AND c.valor LIKE ? ESCAPE '\\'", {like}});
                break;
            case EscopoBusca::Extensao: {
                const std::string ext = "%." + token.trimCharactersAtStart(".").toStdString();
                consultas.push_back({"SELECT a.item_id FROM arquivo a JOIN item i ON i.id = a.item_id WHERE i.projeto_id = ? AND "
                                     "a.caminho_relativo LIKE ?", {ext}});
                break;
            }
            case EscopoBusca::Notas:
                consultas.push_back({doItem + "i.notas_livres LIKE ? ESCAPE '\\')", {like}});
                consultas.push_back({"SELECT o.item_id FROM item_observacao o JOIN item i ON i.id = o.item_id WHERE i.projeto_id = ? AND "
                                     "o.texto LIKE ? ESCAPE '\\'", {like}});
                break;
            case EscopoBusca::Geo: {
                // Termos do endereço ou coordenadas ("-16.44" acha latitude/longitude começando assim).
                const std::string coord = token.toStdString() + "%";
                consultas.push_back({"SELECT g.asset_id FROM asset_geolocation g JOIN item i ON i.id = g.asset_id WHERE i.projeto_id = ? AND ("
                                     "g.city LIKE ?1x OR g.state_province LIKE ?1x OR g.country LIKE ?1x OR g.formatted_address LIKE ?1x "
                                     "OR g.street LIKE ?1x OR g.neighborhood LIKE ?1x OR g.locality LIKE ?1x OR g.municipality LIKE ?1x "
                                     "OR CAST(g.latitude AS TEXT) LIKE ?2x OR CAST(g.longitude AS TEXT) LIKE ?2x)", {like, coord}});
                break;
            }
            case EscopoBusca::Todos:
                break;
        }
        std::set<std::string> achados;
        for (auto& [sqlBruto, padroes] : consultas) {
            try {
                // "?1x"/"?2x": o mesmo padrão repetido — expande pra ?s posicionais.
                std::string sql;
                std::vector<std::string> binds;
                for (size_t k = 0; k < sqlBruto.size(); ++k) {
                    if (sqlBruto[k] == '?' && k + 2 < sqlBruto.size() && sqlBruto[k + 2] == 'x') {
                        binds.push_back(padroes[static_cast<size_t>(sqlBruto[k + 1] - '1')]);
                        sql += '?';
                        k += 2;
                    } else {
                        sql += sqlBruto[k];
                    }
                }
                const bool expandiu = !binds.empty();
                if (!expandiu) binds = padroes;
                auto st = leitura().prepare(sql);
                st.bind(1, matriz::db::Value::of(pid));
                for (size_t k = 0; k < binds.size(); ++k) st.bind(static_cast<int>(k) + 2, matriz::db::Value::of(binds[k]));
                while (st.step()) achados.insert(st.columnText(0));
            } catch (...) {}
        }
        if (primeiro) {
            out = std::move(achados);
            primeiro = false;
        } else {
            std::set<std::string> inter;
            for (const auto& id : out) if (achados.count(id)) inter.insert(id);
            out = std::move(inter);
        }
        if (out.empty()) break;
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorTipoMidia() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare("SELECT tipo_midia, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY tipo_midia");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorEstado() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare("SELECT estado, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY estado");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorExtensao() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT (SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1) "
        "FROM item i WHERE i.projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        if (stmt.columnIsNull(0)) continue;
        juce::String caminho = stmt.columnText(0);
        int dotPos = caminho.lastIndexOfChar('.');
        juce::String ext = dotPos >= 0 ? caminho.substring(dotPos + 1).toLowerCase() : juce::String();
        if (ext.isEmpty()) continue;
        out[ext.toStdString()]++;
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorOrigem() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT (SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'origem') "
        "FROM item i WHERE i.projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnIsNull(0) ? std::string() : stmt.columnText(0)]++;
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorContentType() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT content_type FROM item WHERE projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        std::string v = stmt.columnIsNull(0) ? std::string() : stmt.columnText(0);
        if (!v.empty()) out[v]++;
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorCollectionType() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT collection_type FROM item WHERE projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        std::string v = stmt.columnIsNull(0) ? std::string() : stmt.columnText(0);
        if (!v.empty()) out[v]++;
    }
    return out;
}

std::vector<ProjetoAberto::ColecaoDisponivel> ProjetoAberto::listarColecoesDisponiveis() const {
    std::vector<ColecaoDisponivel> out;
    if (!projeto_) return out;

    std::map<std::string, int> contagens;
    int semColecao = 0;

    auto stmt = leitura().prepare(
        "SELECT collection_type, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY collection_type");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        std::string v = stmt.columnIsNull(0) ? std::string() : stmt.columnText(0);
        int count = stmt.columnInt(1);
        if (v.empty()) {
            semColecao += count;
        } else {
            contagens[v] += count;
        }
    }

    for (const auto& [nome, cnt] : contagens) {
        out.push_back({nome, juce::String(nome), cnt});
    }
    if (semColecao > 0) {
        out.push_back({"Unknown", "Unknown", semColecao});
    }
    return out;
}

std::set<std::string> ProjetoAberto::itensDaColecao(const std::string& chave) const {
    std::set<std::string> out;
    if (!projeto_) return out;

    if (chave == "Unknown" || chave.empty()) {
        auto stmt = leitura().prepare(
            "SELECT id FROM item WHERE projeto_id = ? AND (collection_type IS NULL OR collection_type = '')");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            out.insert(stmt.columnText(0));
        }
    } else {
        auto stmt = leitura().prepare(
            "SELECT id FROM item WHERE projeto_id = ? AND collection_type = ?");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        stmt.bind(2, matriz::db::Value::of(chave));
        while (stmt.step()) {
            out.insert(stmt.columnText(0));
        }
    }
    return out;
}

std::vector<ProjetoAberto::ColecaoLink> ProjetoAberto::listarColecoesLinkadas() const {
    std::vector<ColecaoLink> out;
    if (!projeto_) return out;

    try {
        auto stmt = leitura().prepare(
            "SELECT id, caminho_projeto, nome, IFNULL(grupo, ''), criado_em FROM catalog_colecao_link ORDER BY nome ASC");
        while (stmt.step()) {
            ColecaoLink link;
            link.id = stmt.columnText(0);
            link.caminhoProjeto = juce::String::fromUTF8(stmt.columnText(1).c_str());
            link.nome = juce::String::fromUTF8(stmt.columnText(2).c_str());
            link.grupo = juce::String::fromUTF8(stmt.columnText(3).c_str());
            link.criadoEm = juce::String::fromUTF8(stmt.columnText(4).c_str());

            juce::File pasta(link.caminhoProjeto);
            juce::File dbFile = matriz::model::Project::resolverPastaProjeto(pasta).getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                link.valido = true;
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    auto countStmt = colDb.prepare("SELECT COUNT(*) FROM item");
                    if (countStmt.step()) link.totalAssets = static_cast<uint64_t>(countStmt.columnInt(0));

                    auto sizeStmt = colDb.prepare("SELECT IFNULL(SUM(tamanho_bytes), 0) FROM arquivo");
                    if (sizeStmt.step()) link.totalBytes = static_cast<juce::int64>(sizeStmt.columnInt(0));
                } catch (...) {}
            } else {
                link.valido = false;
            }

            out.push_back(link);
        }
    } catch (...) {}
    return out;
}

bool ProjetoAberto::linkarColecao(const juce::File& pastaProjeto, const juce::String& grupo) {
    if (!projeto_ || !pastaProjeto.isDirectory()) return false;
    juce::File resolvedPasta = matriz::model::Project::resolverPastaProjeto(pastaProjeto);
    juce::File dbFile = resolvedPasta.getChildFile("registro.sqlite");
    if (!dbFile.existsAsFile()) return false;

    juce::String nome = pastaProjeto.getFileName();
    try {
        matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
        auto nameStmt = colDb.prepare("SELECT nome FROM projeto LIMIT 1");
        if (nameStmt.step() && !nameStmt.columnIsNull(0)) {
            juce::String dbNome = juce::String::fromUTF8(nameStmt.columnText(0).c_str());
            if (dbNome.isNotEmpty()) nome = dbNome;
        }
    } catch (...) {}

    std::string id = matriz::model::novoUuid();
    std::string caminho = pastaProjeto.getFullPathName().toStdString();
    std::string nomeStr = nome.toStdString();
    std::string grupoStr = grupo.toStdString();
    std::string agora = juce::Time::getCurrentTime().formatted("%Y-%m-%dT%H:%M:%SZ").toStdString();

    try {
        registrarUndoDeLinhas(*this, "Link Collection",
                              {{"catalog_colecao_link", "caminho_projeto = ?", {matriz::db::Value::of(caminho)}, false}}, {}, "colecao");
        projeto_->registro().run(
            "INSERT INTO catalog_colecao_link (id, caminho_projeto, nome, grupo, criado_em) "
            "VALUES (?, ?, ?, ?, ?) "
            "ON CONFLICT(caminho_projeto) DO UPDATE SET nome = excluded.nome, grupo = excluded.grupo",
            {matriz::db::Value::of(id),
             matriz::db::Value::of(caminho),
             matriz::db::Value::of(nomeStr),
             matriz::db::Value::of(grupoStr),
             matriz::db::Value::of(agora)});
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjetoAberto::desvincularColecao(const std::string& linkId) {
    if (!projeto_ || linkId.empty()) return false;
    try {
        registrarUndoDeLinhas(*this, "Unlink Collection",
                              {{"catalog_colecao_link", "id = ?", {matriz::db::Value::of(linkId)}, false}}, {}, "colecao");
        projeto_->registro().run("DELETE FROM catalog_colecao_link WHERE id = ?", {matriz::db::Value::of(linkId)});
        dirty_ = true;
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjetoAberto::relocarColecaoLink(const std::string& linkId, const juce::File& novaPastaProjeto) {
    if (!projeto_ || linkId.empty() || !novaPastaProjeto.exists()) return false;
    juce::File dbFile = matriz::model::Project::resolverPastaProjeto(novaPastaProjeto).getChildFile("registro.sqlite");
    if (!dbFile.existsAsFile()) return false;

    try {
        std::string caminho = novaPastaProjeto.getFullPathName().toStdString();
        registrarUndoDeLinhas(*this, "Relocate Collection",
                              {{"catalog_colecao_link", "id = ?", {matriz::db::Value::of(linkId)}, false}}, {}, "colecao");
        projeto_->registro().run(
            "UPDATE catalog_colecao_link SET caminho_projeto = ? WHERE id = ?",
            {matriz::db::Value::of(caminho), matriz::db::Value::of(linkId)});
        dirty_ = true;
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjetoAberto::atualizarGrupoColecao(const std::string& linkId, const juce::String& novoGrupo) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return false; }
    if (!projeto_ || linkId.empty()) return false;
    try {
        projeto_->registro().run(
            "UPDATE catalog_colecao_link SET grupo = ? WHERE id = ?",
            {matriz::db::Value::of(novoGrupo.toStdString()), matriz::db::Value::of(linkId)});
        return true;
    } catch (...) {
        return false;
    }
}

std::set<std::string> ProjetoAberto::itensPorFaixaAno(int anoDe, int anoAte) const {
    std::set<std::string> out;
    if (!projeto_) return out;

    // 1. Check user-filled creation fields in item_campo (ano, dc_created, data_criacao)
    try {
        auto stmt = leitura().prepare(
            "SELECT c.item_id, c.valor FROM item_campo c JOIN item i ON i.id = c.item_id "
            "WHERE i.projeto_id = ? AND c.campo_id IN ('ano', 'dc_created', 'data_criacao')");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string val = stmt.columnText(1);
            if (!val.empty()) {
                for (size_t i = 0; i + 3 < val.size(); ++i) {
                    if (std::isdigit(val[i]) && std::isdigit(val[i+1]) && std::isdigit(val[i+2]) && std::isdigit(val[i+3])) {
                        int yr = std::stoi(val.substr(i, 4));
                        if (yr >= anoDe && yr <= anoAte) {
                            out.insert(itemId);
                            break;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    // 2. Direct column ano in item table
    try {
        auto stmt = leitura().prepare(
            "SELECT id FROM item WHERE projeto_id = ? AND ano IS NOT NULL AND ano >= ? AND ano <= ?");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        stmt.bind(2, matriz::db::Value::of(anoDe));
        stmt.bind(3, matriz::db::Value::of(anoAte));
        while (stmt.step()) out.insert(stmt.columnText(0));
    } catch (...) {}

    // 3. EXIF creation date in arquivo.caracteristicas_tecnicas_json
    try {
        auto stmt = leitura().prepare(
            "SELECT a.item_id, a.caracteristicas_tecnicas_json FROM arquivo a JOIN item i ON i.id = a.item_id "
            "WHERE i.projeto_id = ? AND a.eh_master = 1 AND a.caracteristicas_tecnicas_json LIKE '%exifDataOriginal%'");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string jsonStr = stmt.columnText(1);
            auto varObj = juce::JSON::parse(jsonStr);
            if (varObj.isObject() && varObj.hasProperty("exifDataOriginal")) {
                juce::String exifDt = varObj["exifDataOriginal"].toString();
                for (int i = 0; i + 3 < exifDt.length(); ++i) {
                    if (std::isdigit(exifDt[i]) && std::isdigit(exifDt[i+1]) &&
                        std::isdigit(exifDt[i+2]) && std::isdigit(exifDt[i+3])) {
                        int yr = exifDt.substring(i, i + 4).getIntValue();
                        if (yr >= anoDe && yr <= anoAte) {
                            out.insert(itemId);
                            break;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    // 4. Physical file creation/modification date
    try {
        auto stmt = leitura().prepare(
            "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a JOIN item i ON i.id = a.item_id "
            "WHERE i.projeto_id = ? AND a.eh_master = 1 AND a.caminho_absoluto_origem IS NOT NULL AND a.caminho_absoluto_origem != ''");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string absPath = stmt.columnText(1);
            juce::File f(absPath);
            if (f.existsAsFile()) {
                int yr = f.getCreationTime().getYear();
                if (yr <= 1970 || yr > 2025) yr = f.getLastModificationTime().getYear();
                if (yr >= anoDe && yr <= anoAte) {
                    out.insert(itemId);
                }
            }
        }
    } catch (...) {}

    return out;
}

std::vector<ProjetoAberto::ColecaoInteligente> ProjetoAberto::listarColecoes() const {
    std::vector<ColecaoInteligente> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT id, nome, busca_texto, filtros_tipo_midia, filtros_estado, filtros_extensao, filtros_origem, "
        "ano_de, ano_ate, filtros_content_type, filtros_collection_type FROM colecao_inteligente "
        "WHERE projeto_id = ? ORDER BY ordem, criado_em");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        ColecaoInteligente c;
        c.id = stmt.columnText(0);
        c.nome = stmt.columnText(1);
        c.buscaTexto = stmt.columnText(2);
        c.filtrosTipoMidia = conjuntoDeCsv(stmt.columnText(3));
        c.filtrosEstado = conjuntoDeCsv(stmt.columnText(4));
        c.filtrosExtensao = conjuntoDeCsv(stmt.columnText(5));
        c.filtrosOrigem = conjuntoDeCsv(stmt.columnText(6));
        if (!stmt.columnIsNull(7)) c.anoDe = static_cast<int>(stmt.columnInt(7));
        if (!stmt.columnIsNull(8)) c.anoAte = static_cast<int>(stmt.columnInt(8));
        c.filtrosContentType = conjuntoDeCsv(stmt.columnText(9));
        c.filtrosCollectionType = conjuntoDeCsv(stmt.columnText(10));
        out.push_back(std::move(c));
    }
    return out;
}

std::string ProjetoAberto::salvarColecao(const ColecaoInteligente& colecao) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return {}; }
    if (!projeto_) return {};
    std::string id = colecao.id.empty() ? matriz::model::novoUuid() : colecao.id;
    std::string agora = matriz::model::agoraIso8601();
    registrarUndoDeLinhas(*this, colecao.id.empty() ? "Save Collection" : "Edit Collection",
                          {{"colecao_inteligente", "id = ?", {matriz::db::Value::of(id)}, false}}, {}, "colecao");
    projeto_->registro().run(
        "INSERT INTO colecao_inteligente (id, projeto_id, nome, busca_texto, filtros_tipo_midia, filtros_estado, "
        "filtros_extensao, filtros_origem, filtros_content_type, filtros_collection_type, ano_de, ano_ate, ordem, criado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?) "
        "ON CONFLICT(id) DO UPDATE SET nome = excluded.nome, busca_texto = excluded.busca_texto, "
        "filtros_tipo_midia = excluded.filtros_tipo_midia, filtros_estado = excluded.filtros_estado, "
        "filtros_extensao = excluded.filtros_extensao, filtros_origem = excluded.filtros_origem, "
        "filtros_content_type = excluded.filtros_content_type, filtros_collection_type = excluded.filtros_collection_type, "
        "ano_de = excluded.ano_de, ano_ate = excluded.ano_ate",
        {matriz::db::Value::of(id), matriz::db::Value::of(projeto_->projetoId()),
         matriz::db::Value::of(colecao.nome.toStdString()), matriz::db::Value::of(colecao.buscaTexto.toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosTipoMidia).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosEstado).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosExtensao).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosOrigem).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosContentType).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosCollectionType).toStdString()),
         colecao.anoDe ? matriz::db::Value::of(*colecao.anoDe) : matriz::db::Value::null(),
         colecao.anoAte ? matriz::db::Value::of(*colecao.anoAte) : matriz::db::Value::null(),
         matriz::db::Value::of(agora)});
    return id;
}

void ProjetoAberto::apagarColecao(const std::string& id) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    registrarUndoDeLinhas(*this, "Delete Collection", {{"colecao_inteligente", "id = ?", {matriz::db::Value::of(id)}, false}}, {}, "colecao");
    projeto_->registro().run("DELETE FROM colecao_inteligente WHERE id = ?", {matriz::db::Value::of(id)});
}

bool ProjetoAberto::obterItemInfo(const std::string& itemId, std::string& titulo, std::string& tipoMidia, std::string& codigoAcervo) const {
    if (!projeto_) return false;
    auto stmt = leitura().prepare("SELECT titulo, tipo_midia, codigo_acervo FROM item WHERE id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return false;
    titulo = stmt.columnText(0);
    tipoMidia = stmt.columnIsNull(1) ? "" : stmt.columnText(1);
    codigoAcervo = stmt.columnText(2);
    return true;
}

std::optional<ItemResumo> ProjetoAberto::obterItemResumo(const std::string& itemId) const {
    if (!projeto_ || itemId.empty()) return std::nullopt;
    std::string tit, tipo, cod;
    if (!obterItemInfo(itemId, tit, tipo, cod)) return std::nullopt;

    ItemResumo r;
    r.id = itemId;
    r.titulo = tit;
    r.tipoMidia = tipo;
    r.codigoAcervo = cod;

    auto arq = arquivoPrincipal(itemId);
    if (arq) {
        juce::File f(arq->caminhoAbsoluto);
        r.nomeOriginalArquivo = f.getFileName().toStdString();
        r.extensaoArquivo = f.getFileExtension().toStdString();
        r.caminhoAbsolutoOrigem = arq->caminhoAbsoluto.toStdString();
        r.tamanhoBytes = f.existsAsFile() ? f.getSize() : 0;
    }

    try {
        auto stmt = leitura().prepare(
            "SELECT COALESCE(metadados_editados, 0) != 0, content_type, collection_type, ano, "
            "dc_subject, lista, source_media, "
            "EXISTS(SELECT 1 FROM asset_geolocation g WHERE g.asset_id = item.id AND ("
            "g.latitude IS NOT NULL OR g.longitude IS NOT NULL OR "
            "TRIM(COALESCE(g.formatted_address, '')) != '' OR TRIM(COALESCE(g.city, '')) != '' OR "
            "TRIM(COALESCE(g.state_province, '')) != '' OR TRIM(COALESCE(g.country, '')) != '')) "
            "FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step()) {
            r.metadadosEditados = stmt.columnInt(0) != 0;
            if (!stmt.columnIsNull(1)) r.contentType = stmt.columnText(1);
            if (!stmt.columnIsNull(2)) r.collectionType = stmt.columnText(2);
            if (!stmt.columnIsNull(3)) r.ano = extrairAnoDeData(juce::String::fromUTF8(stmt.columnText(3).c_str()));
            if (!stmt.columnIsNull(4)) r.subject = stmt.columnText(4);
            if (!stmt.columnIsNull(5)) r.lista = stmt.columnText(5);
            if (!stmt.columnIsNull(6)) r.sourceMedia = stmt.columnText(6);
            r.temGeolocalizacao = stmt.columnInt(7) != 0;
        }
    } catch (...) {}

    r.tags = lerTags(itemId);
    r.aiGenerated = !lerMetadado(itemId, "ai_generated").value_or("").empty();

    // Sem isto marcadoRevisado vinha sempre false — e quem copiava o resumo
    // (MosaicoComponent::atualizarItemEmMemoria) apagava a borda do E.
    r.marcadoRevisado = itemMarcadoRevisado(itemId);
    r.marcadoPublicacao = contemMarcacao(TipoMarcacao::Html, itemId);
    r.marcadoZip = contemMarcacao(TipoMarcacao::Zip, itemId);
    r.marcadoPrint = contemMarcacao(TipoMarcacao::Print, itemId);
    r.marcadoWatermark = contemMarcacao(TipoMarcacao::Watermark, itemId);
    r.marcadoBounce = contemMarcacao(TipoMarcacao::Bounce, itemId);
    r.marcadoRejeitado = contemMarcacao(TipoMarcacao::Rejeitado, itemId);
    r.impresso = itemImpresso(itemId);

    return r;
}

std::set<int> ProjetoAberto::indicesExistentes(const std::string& itemId, const std::string& nivel) const {
    std::set<int> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT DISTINCT nivel_indice FROM item_campo WHERE item_id = ? AND nivel = ? ORDER BY nivel_indice");
    stmt.bind(1, matriz::db::Value::of(itemId));
    stmt.bind(2, matriz::db::Value::of(nivel));
    while (stmt.step()) out.insert(static_cast<int>(stmt.columnInt(0)));
    return out;
}

void ProjetoAberto::gravarTipoMidia(const std::string& itemId, const std::string& tipoMidia, const std::string& agora,
                                    bool promoverEstado) {
    auto& registro = projeto_->registro();
    if (promoverEstado) {
        // Classificar É o que move o item de 'novo' pra 'catalogado' (§4): o
        // ingest só o trouxe pra dentro; a decisão de que tipo de mídia é isto
        // é humana. Estados posteriores (revisado/aprovado/publicado) não são
        // sobrescritos — reclassificar um item já aprovado não o rebaixa.
        registro.run(
            "UPDATE item SET tipo_midia = ?, atualizado_em = ?, "
            "estado = CASE WHEN estado IN ('novo', 'capturado', 'nao_digitalizado') THEN 'catalogado' ELSE estado END "
            "WHERE id = ?",
            {matriz::db::Value::of(tipoMidia), matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
    } else {
        registro.run("UPDATE item SET tipo_midia = ?, atualizado_em = ? WHERE id = ?",
                     {matriz::db::Value::of(tipoMidia), matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});
    }

    auto origem = matriz::ficha::origemPadraoParaTipo(tipoMidia);
    if (origem) {
        registro.run(
            "INSERT OR IGNORE INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'raiz', 0, 'origem', ?, 'leitura_tecnica', ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(*origem), matriz::db::Value::of(agora)});
    }
}

void ProjetoAberto::atualizarTipoMidia(const std::string& itemId, const std::string& tipoMidia) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    if (!desfazendo_) {
        auto stmt = projeto_->registro().prepare("SELECT tipo_midia FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        std::string oldTipo = stmt.step() ? stmt.columnText(0) : "";
        registrarUndo("Change Media Type", [this, itemId, oldTipo]() {
            atualizarTipoMidia(itemId, oldTipo);
        });
    }
    gravarTipoMidia(itemId, tipoMidia, matriz::model::agoraIso8601(), /*promoverEstado*/ true);

    EventBus::obterInstancia().dispararItemAlterado(itemId, "classificacao");
}

void ProjetoAberto::restaurarTiposMidia(const std::map<std::string, std::string>& tiposPorItem) {
    if (!projeto_) return;
    // Mesma escrita de atualizarTipoMidia() (o desfazer sempre foi por ela), mas
    // numa transação só e com um evento de lote.
    const std::string agora = matriz::model::agoraIso8601();
    auto& registro = projeto_->registro();
    registro.run("BEGIN", {});
    try {
        for (const auto& [id, tipo] : tiposPorItem) gravarTipoMidia(id, tipo, agora, /*promoverEstado*/ true);
        registro.run("COMMIT", {});
    } catch (...) {
        registro.run("ROLLBACK", {});
        throw;
    }
    std::vector<std::string> ids;
    ids.reserve(tiposPorItem.size());
    for (const auto& par : tiposPorItem) ids.push_back(par.first);
    EventBus::obterInstancia().dispararItensAlterados(ids, "classificacao");
}

void ProjetoAberto::aplicarTipoMidiaEmLote(const std::vector<std::string>& itemIds, const std::string& tipoMidia,
                                           bool promoverEstado) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    if (!desfazendo_) {
        std::map<std::string, std::string> antigosTipos;
        for (const auto& id : itemIds) {
            auto stmt = projeto_->registro().prepare("SELECT tipo_midia FROM item WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(id));
            if (stmt.step()) antigosTipos[id] = stmt.columnText(0);
        }
        registrarUndo("Batch Change Media Type", [this, antigosTipos]() {
            restaurarTiposMidia(antigosTipos);
        });
    }
    std::string agora = matriz::model::agoraIso8601();
    auto& registro = projeto_->registro();
    registro.run("BEGIN", {});
    try {
        for (auto& id : itemIds) gravarTipoMidia(id, tipoMidia, agora, promoverEstado);
        registro.run("COMMIT", {});
    } catch (...) {
        registro.run("ROLLBACK", {});
        throw;
    }

    EventBus::obterInstancia().dispararItensAlterados(itemIds, "classificacao");
}

void ProjetoAberto::obterTiposMidiaDosItens(const std::vector<std::string>& itemIds, std::set<std::string>& tiposPresentes, bool& algumNulo) const {
    algumNulo = false;
    if (!projeto_) return;
    for (auto& id : itemIds) {
        auto stmt = leitura().prepare("SELECT tipo_midia FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(id));
        if (!stmt.step()) continue;
        if (stmt.columnIsNull(0)) algumNulo = true;
        else tiposPresentes.insert(stmt.columnText(0));
    }
}

} // namespace matriz::ui

namespace matriz::ui {

std::vector<ProjetoAberto::VaultResumo> ProjetoAberto::listarVaults() const {
    std::vector<VaultResumo> out;
    if (!projeto_) return out;
    auto stmt = leitura().prepare(
        "SELECT v.id, v.nome, v.localizacao, v.status, "
        "(SELECT COUNT(DISTINCT a.item_id) FROM arquivo a WHERE a.vault_id = v.id) "
        "FROM vault v "
        "WHERE v.projeto_id = ? "
        "  AND v.localizacao NOT LIKE '/System/Volumes/%' "
        "  AND v.localizacao NOT IN ('/dev', '/net', '/home') "
        "  AND v.nome NOT IN ('dev', 'Preboot', 'Update', 'VM', 'xarts', 'Hardware') "
        "ORDER BY v.nome");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        VaultResumo v;
        v.id = stmt.columnText(0);
        v.nome = juce::String(stmt.columnText(1));
        v.localizacao = juce::String(stmt.columnText(2));
        v.online = stmt.columnText(3) == "online";
        v.totalItens = static_cast<int>(stmt.columnInt(4));
        out.push_back(std::move(v));
    }
    return out;
}

ProjetoAberto::SituacaoMain ProjetoAberto::normalizarPapelMain() {
    SituacaoMain sit;
    if (!projeto_) return sit;
    if (editandoMain_) { sit.tipo = SituacaoMain::Tipo::Ok; return sit; }  // rebuild: sem MAIN de propósito
    auto& db = projeto_->registro();
    struct Linha { std::string id, destinationId, papel; juce::String rotulo, caminho; };
    std::vector<Linha> linhas;
    try {
        auto st = db.prepare("SELECT id, COALESCE(destination_id, id), COALESCE(papel, 'CLONE'), rotulo, destino_path "
                             "FROM backup_destino WHERE ativo = 1 ORDER BY criado_em ASC");
        while (st.step())
            linhas.push_back({st.columnText(0), st.columnText(1), st.columnText(2),
                              juce::String::fromUTF8(st.columnText(3).c_str()),
                              juce::String::fromUTF8(st.columnText(4).c_str())});
    } catch (...) { return sit; }
    if (linhas.empty()) return sit;  // projeto sem backup: continua como hoje

    std::vector<const Linha*> mains;
    for (auto& l : linhas) if (l.papel == "ORIGINAL") mains.push_back(&l);
    if (mains.size() == 1) { sit.tipo = SituacaoMain::Tipo::Ok; return sit; }

    if (mains.size() > 1) {
        const std::string raizId = projeto_->destinationId();
        for (auto* m : mains) {
            if (!raizId.empty() && m->destinationId == raizId) {
                definirMain(m->id);
                sit.tipo = SituacaoMain::Tipo::Ok;
                return sit;
            }
        }
    }
    sit.tipo = SituacaoMain::Tipo::Perguntar;
    for (auto& l : linhas) sit.opcoes.push_back({l.id, l.rotulo + juce::String::fromUTF8(" \xe2\x80\x94 ") + l.caminho});
    return sit;
}

void ProjetoAberto::definirMain(const std::string& backupDestinoId) {
    if (!projeto_ || backupDestinoId.empty()) return;
    auto& db = projeto_->registro();
    juce::String rotulo;
    try {
        db.run("BEGIN IMMEDIATE", {});
        db.run("UPDATE backup_destino SET papel = CASE WHEN id = ? THEN 'ORIGINAL' ELSE 'CLONE' END WHERE ativo = 1",
               {matriz::db::Value::of(backupDestinoId)});
        auto st = db.prepare("SELECT rotulo FROM backup_destino WHERE id = ?");
        st.bind(1, matriz::db::Value::of(backupDestinoId));
        if (st.step()) rotulo = juce::String::fromUTF8(st.columnText(0).c_str());
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        return;
    }
    try {
        matriz::model::ProjectLog(projeto_->pasta()).appendEntry(
            "MAIN assigned", {"Version: " + rotulo, "Other registered versions are CLONE (labels only, nothing copied)"});
    } catch (...) {}
}

int ProjetoAberto::arquivosQueDependemDoSource() {
    if (!projeto_) return 0;
    auto& db = projeto_->registro();
    try {
        std::string mainId = projeto_->destinationId();
        auto sm = db.prepare("SELECT COALESCE(destination_id, id) FROM backup_destino WHERE ativo = 1 AND papel = 'ORIGINAL' LIMIT 1");
        if (sm.step()) mainId = sm.columnText(0);
        auto st = db.prepare(
            "SELECT COUNT(*) FROM arquivo a JOIN item i ON i.id = a.item_id "
            "WHERE COALESCE(i.em_quarentena, 0) = 0 AND COALESCE(i.estado, '') != 'duplicata' AND a.eh_master = 1 AND NOT EXISTS ("
            "  SELECT 1 FROM consolidacao_registro c WHERE c.arquivo_id = a.id "
            "  AND (COALESCE(c.destino_id, '') = '' OR c.destino_id = ?))");
        st.bind(1, matriz::db::Value::of(mainId));
        if (st.step()) return static_cast<int>(st.columnInt(0));
    } catch (...) {}
    return 0;
}

std::vector<ProjetoAberto::VersaoResumo> ProjetoAberto::listarVersoes() {
    std::vector<VersaoResumo> out;
    if (!projeto_) return out;
    sincronizarBackupDestinoDeHistorico();
    auto& db = projeto_->registro();
    const int64_t revisaoProjeto = projeto_->revisao();
    std::string mainDestinationId;

    // MAIN e CLONEs.
    try {
        auto st = db.prepare(
            "SELECT id, COALESCE(destination_id, id), COALESCE(papel, 'CLONE'), rotulo, destino_path, "
            "COALESCE(ultima_revisao_conhecida, 0), COALESCE(ultimo_visto_em, '') FROM backup_destino WHERE ativo = 1 "
            "ORDER BY criado_em ASC");
        auto raizes = matriz::vault::destinosDeBackup(db, projeto_->pasta());
        while (st.step()) {
            VersaoResumo v;
            v.id = st.columnText(0);
            const std::string destId = st.columnText(1);
            v.papel = st.columnText(2) == "ORIGINAL" ? VersaoResumo::Papel::Main : VersaoResumo::Papel::Clone;
            v.rotulo = juce::String::fromUTF8(st.columnText(3).c_str());
            v.caminho = juce::String::fromUTF8(st.columnText(4).c_str());
            // Raiz atual pelo destination_id (a pasta aberta pode ser este destino
            // montado com outro nome).
            juce::File raiz(v.caminho);
            for (auto& d : raizes) if (d.destinationId == destId) raiz = d.raiz;
            v.caminho = raiz.getFullPathName();
            v.online = raiz.isDirectory();
            if (v.papel == VersaoResumo::Papel::Main) {
                mainDestinationId = destId;
            } else {
                v.desatualizado = st.columnInt(5) < revisaoProjeto;
                v.ultimaData = juce::String::fromUTF8(st.columnText(6).c_str());
                v.origem = "MAIN";
            }
            // Registros deste destino (por id ou caminho); os legados (sem destino)
            // contam pro MAIN.
            auto stats = db.prepare(
                "SELECT COUNT(DISTINCT item_id), MAX(consolidado_em) FROM consolidacao_registro "
                "WHERE (destino_id != '' AND destino_id = ?) OR destino_path = ? OR destino_path = ? "
                "   OR (? = 1 AND COALESCE(destino_id, '') = '' AND COALESCE(destino_path, '') = '')");
            stats.bind(1, matriz::db::Value::of(destId));
            stats.bind(2, matriz::db::Value::of(st.columnText(4)));
            stats.bind(3, matriz::db::Value::of(raiz.getChildFile("Media").getFullPathName().toStdString()));
            stats.bind(4, matriz::db::Value::of(v.papel == VersaoResumo::Papel::Main ? 1 : 0));
            if (stats.step()) {
                v.totalItens = static_cast<int>(stats.columnInt(0));
                if (v.papel == VersaoResumo::Papel::Main && !stats.columnIsNull(1))
                    v.ultimaData = juce::String::fromUTF8(stats.columnText(1).c_str());
            }
            out.push_back(std::move(v));
        }
    } catch (...) {}

    // SOURCEs: volumes (tabela vault) de onde vieram arquivos. Uma pasta de
    // origem que é um destino de backup não é SOURCE.
    try {
        auto st = db.prepare(
            "SELECT v.id, v.nome, v.localizacao, COUNT(a.id), MAX(a.criado_em), "
            "SUM(CASE WHEN NOT EXISTS (SELECT 1 FROM consolidacao_registro c WHERE c.arquivo_id = a.id "
            "      AND (COALESCE(c.destino_id, '') = '' OR c.destino_id = ?)) THEN 1 ELSE 0 END), "
            "COUNT(DISTINCT substr(a.criado_em, 1, 10)), "
            "EXISTS (SELECT 1 FROM consolidacao_registro c JOIN arquivo a2 ON a2.id = c.arquivo_id WHERE a2.vault_id = v.id) "
            "FROM vault v JOIN arquivo a ON a.vault_id = v.id GROUP BY v.id ORDER BY MIN(a.criado_em)");
        st.bind(1, matriz::db::Value::of(mainDestinationId));
        const auto codigos = matriz::consolidacao::codigosDeSource(db);
        while (st.step()) {
            VersaoResumo v;
            v.papel = VersaoResumo::Papel::Source;
            v.id = st.columnText(0);
            v.rotulo = juce::String::fromUTF8(st.columnText(1).c_str());
            v.caminho = juce::String::fromUTF8(st.columnText(2).c_str());
            bool ehDestino = false;
            for (auto& d : out)
                if (d.papel != VersaoResumo::Papel::Source &&
                    (juce::File(v.caminho) == juce::File(d.caminho) || juce::File(v.caminho).isAChildOf(juce::File(d.caminho))))
                    ehDestino = true;
            if (ehDestino) continue;
            v.online = juce::File(v.caminho).isDirectory();
            v.totalItens = static_cast<int>(st.columnInt(3));
            v.ultimaData = juce::String::fromUTF8(st.columnText(4).c_str());
            v.dependentes = static_cast<int>(st.columnInt(5));
            v.ingestoes = static_cast<int>(st.columnInt(6));
            v.codigoEditavel = st.columnInt(7) == 0;
            if (auto it = codigos.find(v.id); it != codigos.end()) v.codigo = juce::String(it->second);
            out.push_back(std::move(v));
        }
    } catch (...) {}

    // Clones brutos de SOURCE (etapa 7): também são CLONE, com a origem.
    try {
        db.exec("CREATE TABLE IF NOT EXISTS source_clone ("
                "  id TEXT PRIMARY KEY, vault_id TEXT NOT NULL, origem_path TEXT NOT NULL, destino_path TEXT NOT NULL,"
                "  rotulo TEXT NOT NULL DEFAULT '', criado_em TEXT NOT NULL, ultima_sync_em TEXT, arquivos INTEGER NOT NULL DEFAULT 0)");
        const auto codigos = matriz::consolidacao::codigosDeSource(db);
        auto st = db.prepare("SELECT c.id, c.rotulo, c.destino_path, COALESCE(c.ultima_sync_em, ''), c.arquivos, c.vault_id, "
                             "COALESCE(v.nome, '') FROM source_clone c LEFT JOIN vault v ON v.id = c.vault_id "
                             "ORDER BY c.criado_em");
        while (st.step()) {
            VersaoResumo v;
            v.papel = VersaoResumo::Papel::Clone;
            v.cloneDeSource = true;
            v.id = st.columnText(0);
            v.rotulo = juce::String::fromUTF8(st.columnText(1).c_str());
            v.caminho = juce::String::fromUTF8(st.columnText(2).c_str());
            v.online = juce::File(v.caminho).isDirectory();
            v.ultimaData = juce::String::fromUTF8(st.columnText(3).c_str());
            v.totalItens = static_cast<int>(st.columnInt(4));
            auto it = codigos.find(st.columnText(5));
            v.origem = (it != codigos.end() ? juce::String(it->second) + " " : juce::String()) +
                       juce::String::fromUTF8(st.columnText(6).c_str());
            out.push_back(std::move(v));
        }
    } catch (...) {}

    // MAIN sempre no topo; depois CLONEs; depois SOURCEs.
    std::stable_sort(out.begin(), out.end(), [](const VersaoResumo& a, const VersaoResumo& b) {
        return static_cast<int>(a.papel) < static_cast<int>(b.papel);
    });
    return out;
}

void ProjetoAberto::sincronizarBackupDestinoDeHistorico() {
    if (!projeto_) return;
    auto& db = projeto_->registro();
    // Roda a cada visita à aba BACKUP: só grava (e só marca o projeto como
    // sujo — Database::run() sempre marca) quando há algo a mudar de fato.
    auto destinoJaRegistrado = [&db](const std::string& caminho) {
        auto st = db.prepare("SELECT 1 FROM backup_destino WHERE destino_path = ? LIMIT 1");
        st.bind(1, matriz::db::Value::of(caminho));
        return st.step();
    };
    try {
        // Se houver registros legados com destino_path vazio e existir a pasta padrão <projeto>/Backup com arquivos
        juce::File pastaBackupPadrao = projeto_->pasta().getChildFile("Backup");
        if (pastaBackupPadrao.isDirectory()) {
            std::string pathPadrao = pastaBackupPadrao.getFullPathName().toStdString();
            bool temLegado = false;
            {
                auto stmtLegado = db.prepare(
                    "SELECT 1 FROM consolidacao_registro WHERE destino_path = '' OR destino_path IS NULL LIMIT 1");
                temLegado = stmtLegado.step();
            }
            if (temLegado)
                db.run("UPDATE consolidacao_registro SET destino_path = ? WHERE destino_path = '' OR destino_path IS NULL",
                       {matriz::db::Value::of(pathPadrao)});

            auto stmtCount = db.prepare("SELECT COUNT(*) FROM consolidacao_registro WHERE destino_path = ?");
            stmtCount.bind(1, matriz::db::Value::of(pathPadrao));
            if (stmtCount.step() && stmtCount.columnInt(0) > 0 && !destinoJaRegistrado(pathPadrao)) {
                std::string agora = matriz::model::agoraIso8601();
                db.run("INSERT OR IGNORE INTO backup_destino (id, destino_path, rotulo, ativo, criado_em) VALUES (?, ?, ?, 1, ?)",
                       {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(pathPadrao),
                        matriz::db::Value::of("Backup"), matriz::db::Value::of(agora)});
            }
        }

        // Verificar se há vaults com histórico em consolidacao_registro
        auto stmtVaults = db.prepare("SELECT nome, localizacao FROM vault WHERE localizacao IS NOT NULL AND localizacao != ''");
        while (stmtVaults.step()) {
            std::string vNome = stmtVaults.columnText(0);
            std::string vLoc = stmtVaults.columnText(1);
            juce::File fVault(vLoc);
            if (fVault.isDirectory()) {
                auto sCheck = db.prepare("SELECT COUNT(*) FROM consolidacao_registro WHERE destino_path = ?");
                sCheck.bind(1, matriz::db::Value::of(vLoc));
                if (sCheck.step() && sCheck.columnInt(0) > 0 && !destinoJaRegistrado(vLoc)) {
                    std::string agora = matriz::model::agoraIso8601();
                    db.run("INSERT OR IGNORE INTO backup_destino (id, destino_path, rotulo, ativo, criado_em) VALUES (?, ?, ?, 1, ?)",
                           {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(vLoc),
                            matriz::db::Value::of(vNome), matriz::db::Value::of(agora)});
                }
            }
        }

        // Buscar outros destinos existentes no histórico de consolidação
        auto stmtAuto = db.prepare(
            "SELECT DISTINCT destino_path FROM consolidacao_registro "
            "WHERE destino_path != '' AND destino_path NOT IN (SELECT destino_path FROM backup_destino)");
        std::vector<std::string> novosDestinos;
        while (stmtAuto.step()) {
            novosDestinos.push_back(stmtAuto.columnText(0));
        }

        for (const auto& dp : novosDestinos) {
            juce::File f(dp);
            if (f.getFileName().equalsIgnoreCase("Media") || f.getFileName().equalsIgnoreCase("Project")) {
                continue; // Não registrar subpastas Media/Project como destino
            }
            std::string rotulo = f.getFileName().toStdString();
            if (rotulo.empty()) rotulo = "Backup Destination";
            std::string agora = matriz::model::agoraIso8601();
            db.run("INSERT OR IGNORE INTO backup_destino (id, destino_path, rotulo, ativo, criado_em) VALUES (?, ?, ?, 1, ?)",
                   {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(dp),
                    matriz::db::Value::of(rotulo), matriz::db::Value::of(agora)});
        }
    } catch (...) {}
}

std::vector<ProjetoAberto::ColecaoEmbutida> ProjetoAberto::listarColecoesEmbutidas() const {
    if (!projeto_) return {};

    static const std::vector<std::pair<const char*, const char*>> kEmbutidas = {
        {"clipping", "colecoes.clipping"},       {"ausentes", "colecoes.ausentes"},
        {"nao_baixados", "colecoes.nao_baixados"}, {"incompletos", "colecoes.incompletos"},
        {"vulneraveis", "colecoes.vulneraveis"}, {"revisao", "colecoes.revisao"}};

    std::map<std::string, int> contagens;
    try {
        auto stmt = leitura().prepare(
            "SELECT colecao, COUNT(DISTINCT item_id) FROM colecao_embutida GROUP BY colecao");
        while (stmt.step()) contagens[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    } catch (const std::exception&) {
        // Projeto antigo reaberto antes das views existirem: a seção some,
        // o resto do painel continua.
        return {};
    }

    std::vector<ColecaoEmbutida> out;
    for (const auto& [chave, chaveI18n] : kEmbutidas) {
        ColecaoEmbutida c;
        c.chave = chave;
        c.rotulo = matriz::i18n::t(chaveI18n);
        c.contagem = contagens.count(chave) ? contagens[chave] : 0;
        out.push_back(std::move(c));
    }
    return out;
}

std::set<std::string> ProjetoAberto::itensDaColecaoEmbutida(const std::string& chave) const {
    std::set<std::string> out;
    if (!projeto_) return out;
    // Fase 4: não é view do schema (bancos antigos não a teriam) — vem direto
    // do histórico de merge.
    if (chave == "merge_conflitos") return matriz::model::merge::itensComConflitoPendente(leitura());
    try {
        auto stmt = leitura().prepare("SELECT DISTINCT item_id FROM colecao_embutida WHERE colecao = ?");
        stmt.bind(1, matriz::db::Value::of(chave));
        while (stmt.step()) out.insert(stmt.columnText(0));
    } catch (const std::exception&) {
    }
    return out;
}

std::vector<std::string> ProjetoAberto::reavaliarVaults() {
    if (!projeto_) return {};
    try {
        bool mudou = false;
        auto reconectados = matriz::vault::reavaliarVaults(projeto_->registro(), &mudou);
        if (mudou) vaultsMudaram_.store(true);
        return reconectados;
    } catch (const std::exception&) {
        return {};
    }
}

// ---------------------------------------------------------------------------
// Camada de Preservação Digital (OAIS / PREMIS / FAIR)
// ---------------------------------------------------------------------------

preservation::PreservationStatus ProjetoAberto::obterPreservationStatus(const std::string& itemId) const {
    if (!projeto_) return {};
    try { return preservation::obterStatus(projeto_->registro(), itemId); }
    catch (...) { return {}; }
}

std::vector<preservation::EventoPreservacao> ProjetoAberto::listarEventosPreservacao(const std::string& itemId) const {
    if (!projeto_) return {};
    try { return preservation::listarEventos(projeto_->registro(), itemId); }
    catch (...) { return {}; }
}

std::optional<preservation::DireitosPreservacao> ProjetoAberto::obterDireitos(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    try { return preservation::obterDireitos(projeto_->registro(), itemId); }
    catch (...) { return std::nullopt; }
}

void ProjetoAberto::salvarDireitos(const std::string& itemId, const preservation::DireitosPreservacao& d) {
    if (somenteLeitura_) { avisarSomenteLeitura(); return; }
    if (!projeto_) return;
    try {
        preservation::salvarDireitos(projeto_->registro(), itemId, d, "bkr-agent-sistema");
    } catch (...) {}
}

void ProjetoAberto::verificarFixityAsync(const std::string& arquivoId,
                                         const std::string& caminhoAbsoluto,
                                         std::function<void(preservation::ResultadoFixity)> callback) {
    if (!projeto_ || !callback) return;

    // Captura o caminho do arquivo do projeto para abrir uma segunda conexão
    // na thread de background (a conexão principal pertence à message thread).
    juce::File registroFile = projeto_->pasta().getChildFile("registro.sqlite");

    std::thread([registroFile, arquivoId, caminhoAbsoluto, callback = std::move(callback)]() mutable {
        preservation::ResultadoFixity resultado;
        try {
            db::Database db(registroFile.getFullPathName().toStdString());
            resultado = preservation::verificarFixity(db, arquivoId, caminhoAbsoluto, "bkr-agent-sistema");
        } catch (const std::exception& e) {
            resultado.success  = false;
            resultado.mensagem = std::string("Erro ao verificar: ") + e.what();
        }
        // Devolve resultado na message thread
        juce::MessageManager::callAsync([resultado, callback = std::move(callback)]() mutable {
            callback(resultado);
        });
    }).detach();
}

juce::String ProjetoAberto::exportarPreservacaoJson(const std::string& itemId) const {
    if (!projeto_) return "{}";
    try { return preservation::exportarJson(projeto_->registro(), itemId); }
    catch (...) { return "{}"; }
}

juce::String ProjetoAberto::exportarPreservacaoCsv(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto colecoes = listarColecoesLinkadas();
        juce::String csv;
        bool headerWritten = false;

        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = colDir.getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    std::vector<std::string> ids;
                    auto stmt = colDb.prepare("SELECT id FROM item ORDER BY codigo_acervo ASC, id ASC");
                    while (stmt.step()) ids.push_back(stmt.columnText(0));

                    juce::String part = preservation::exportarCsv(colDb, ids);
                    if (!headerWritten) {
                        csv += part;
                        headerWritten = true;
                    } else {
                        int newlinePos = part.indexOfChar('\n');
                        if (newlinePos >= 0) csv += part.substring(newlinePos + 1);
                    }
                } catch (...) {}
            }
        }
        if (csv.isEmpty()) {
            return preservation::exportarCsv(projeto_->registro(), itemIds);
        }
        return csv;
    }
    try { return preservation::exportarCsv(projeto_->registro(), itemIds); }
    catch (...) { return {}; }
}

juce::String ProjetoAberto::exportarFullCsv(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto colecoes = listarColecoesLinkadas();
        juce::String csv;
        bool headerWritten = false;

        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = colDir.getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    std::vector<std::string> ids;
                    auto stmt = colDb.prepare("SELECT id FROM item ORDER BY codigo_acervo ASC, id ASC");
                    while (stmt.step()) ids.push_back(stmt.columnText(0));

                    juce::String part = preservation::exportarFullCsv(colDb, ids);
                    if (!headerWritten) {
                        csv += part;
                        headerWritten = true;
                    } else {
                        int newlinePos = part.indexOfChar('\n');
                        if (newlinePos >= 0) csv += part.substring(newlinePos + 1);
                    }
                } catch (...) {}
            }
        }
        if (csv.isEmpty()) {
            return preservation::exportarFullCsv(projeto_->registro(), itemIds);
        }
        return csv;
    }
    try { return preservation::exportarFullCsv(projeto_->registro(), itemIds); }
    catch (...) { return {}; }
}

bool ProjetoAberto::exportarFullCsvPacote(const std::vector<std::string>& itemIds, const juce::File& destLocation, juce::String& errorOut) const {
    if (!projeto_) {
        errorOut = "No active project open";
        return false;
    }
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        try {
            juce::File pkgDir = (destLocation.isDirectory() && destLocation.getFileName() == "BKR_Full_Export")
                                    ? destLocation
                                    : destLocation.getChildFile("BKR_Full_Export");
            if (!pkgDir.exists()) pkgDir.createDirectory();
            juce::File csvFile = pkgDir.getChildFile("BKR_FULL.csv");
            juce::String csvContent = exportarFullCsv(itemIds);
            csvFile.replaceWithText(csvContent, false, false, "\n");

            juce::File schemaFile = pkgDir.getChildFile("BKR_FULL.schema.json");
            schemaFile.replaceWithText(preservation::gerarFullCsvSchemaJson(), false, false, "\n");

            juce::MemoryBlock block;
            csvFile.loadFileAsData(block);
            juce::SHA256 sha(block.getData(), block.getSize());
            juce::String csvSha256 = sha.toHexString();

            juce::StringArray lines;
            lines.addLines(csvContent);
            int assetCount = 0;
            for (int i = 1; i < lines.size(); ++i) {
                if (lines[i].trim().isNotEmpty()) assetCount++;
            }

            juce::File manifestFile = pkgDir.getChildFile("manifest.json");
            juce::String manifestContent = preservation::gerarFullCsvManifestJson(assetCount, csvSha256);
            manifestFile.replaceWithText(manifestContent, false, false, "\n");

            auto valRes = preservation::validarFullCsvFile(csvFile, assetCount);
            if (!valRes.valid) {
                errorOut = valRes.error;
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            errorOut = e.what();
            return false;
        } catch (...) {
            errorOut = "Unknown error during export";
            return false;
        }
    }
    try { return preservation::exportarFullCsvPacote(projeto_->registro(), itemIds, destLocation, errorOut); }
    catch (const std::exception& e) { errorOut = e.what(); return false; }
    catch (...) { errorOut = "Unknown error during export"; return false; }
}

juce::String ProjetoAberto::exportarXlsXml(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    std::string projName = projeto_->nome();
    std::string catCode = "BKR-MATRIZ-01";

    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto escHtml = [](const std::string& str) -> juce::String {
            juce::String s(str);
            return s.replace("&", "&amp;")
                    .replace("<", "&lt;")
                    .replace(">", "&gt;")
                    .replace("\"", "&quot;")
                    .replace("'", "&apos;");
        };

        auto colecoes = listarColecoesLinkadas();
        std::string dateNow = matriz::model::agoraIso8601();

        juce::int64 totalBytes = 0;
        int totalAssets = 0;

        struct ItemRow {
            std::string title, creator, subject, description, publisher, contributor;
            std::string created, issued, type, format, identifier, source, language;
            std::string relation, coverage, rights;
        };
        std::vector<ItemRow> allRows;

        auto processDb = [&](matriz::db::Database& db, const std::vector<std::string>& ids) {
            std::vector<std::string> itemIdsList = ids;
            if (itemIdsList.empty()) {
                try {
                    auto stmt = db.prepare("SELECT id FROM item ORDER BY codigo_acervo ASC, id ASC");
                    while (stmt.step()) itemIdsList.push_back(stmt.columnText(0));
                } catch (...) {}
            }

            for (const auto& id : itemIdsList) {
                try {
                    auto stmt = db.prepare("SELECT IFNULL(tamanho_bytes,0) FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
                    stmt.bind(1, matriz::db::Value::of(id));
                    if (stmt.step()) totalBytes += stmt.columnInt(0);
                } catch (...) {}

                auto lerCampo = [&](const std::string& itemId, const std::string& campo) -> std::string {
                    try {
                        auto stmt = db.prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
                        stmt.bind(1, matriz::db::Value::of(itemId));
                        stmt.bind(2, matriz::db::Value::of(campo));
                        if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
                    } catch (...) {}
                    return "";
                };

                ItemRow row;
                row.title = lerCampo(id, "dc_title");
                row.creator = lerCampo(id, "dc_creator");
                row.subject = lerCampo(id, "dc_subject");
                row.description = lerCampo(id, "dc_description");
                row.publisher = lerCampo(id, "dc_publisher");
                row.contributor = lerCampo(id, "dc_contributor");
                row.created = lerCampo(id, "dc_created");
                row.issued = lerCampo(id, "dc_issued");
                row.type = lerCampo(id, "dc_type");
                row.format = lerCampo(id, "dc_format");
                row.identifier = lerCampo(id, "dc_identifier");
                row.source = lerCampo(id, "dc_source");
                row.language = lerCampo(id, "dc_language");
                row.relation = lerCampo(id, "dc_relation");
                row.coverage = lerCampo(id, "dc_coverage");
                row.rights = lerCampo(id, "dc_rights");

                try {
                    auto stmt = db.prepare(
                        "SELECT IFNULL(persistent_id,''), IFNULL(titulo,''), IFNULL(tipo_midia,''), criado_em FROM item WHERE id = ? LIMIT 1");
                    stmt.bind(1, matriz::db::Value::of(id));
                    if (stmt.step()) {
                        if (row.identifier.empty()) row.identifier = stmt.columnText(0);
                        if (row.title.empty()) row.title = stmt.columnText(1);
                        if (row.format.empty()) row.format = stmt.columnText(2);
                        if (row.created.empty()) row.created = stmt.columnText(3);
                    }
                } catch (...) {}

                allRows.push_back(row);
                totalAssets++;
            }
        };

        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = matriz::model::Project::resolverPastaProjeto(colDir).getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    processDb(colDb, {});
                } catch (...) {}
            }
        }

        // If no linked collections or if root db has items as well
        if (allRows.empty()) {
            processDb(projeto_->registro(), itemIds);
        }

        juce::String html;
        html += "<html xmlns:o=\"urn:schemas-microsoft-com:office:office\"\n"
                "xmlns:x=\"urn:schemas-microsoft-com:office:excel\"\n"
                "xmlns=\"http://www.w3.org/TR/REC-html40\">\n"
                "<head>\n"
                "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
                "<meta name=\"ProgId\" content=\"Excel.Sheet\">\n"
                "<meta name=\"Generator\" content=\"BKR Matriz Archival Engine\">\n"
                "<!--[if gte mso 9]><xml>\n"
                " <x:ExcelWorkbook>\n"
                "  <x:ExcelWorksheets>\n"
                "   <x:ExcelWorksheet>\n"
                "    <x:Name>Dublin Core Catalog</x:Name>\n"
                "    <x:WorksheetOptions>\n"
                "     <x:DisplayGridlines/>\n"
                "    </x:WorksheetOptions>\n"
                "   </x:ExcelWorksheet>\n"
                "  </x:ExcelWorksheets>\n"
                " </x:ExcelWorkbook>\n"
                "</xml><![endif]-->\n"
                "<style>\n"
                "  body { font-family: 'Segoe UI', Arial, sans-serif; background-color: #F8FAFC; margin: 0; padding: 20px; }\n"
                "  .hdr-table { width: 100%; border-collapse: collapse; margin-bottom: 20px; font-family: 'Segoe UI', Arial, sans-serif; }\n"
                "  .hdr-main { background-color: #0F172A; color: #38BDF8; font-size: 16px; font-weight: bold; padding: 14px; border: 1px solid #1E293B; text-transform: uppercase; letter-spacing: 1px; }\n"
                "  .hdr-sub { background-color: #1E293B; color: #F8FAFC; font-size: 12px; padding: 10px 14px; border: 1px solid #334155; }\n"
                "  .meta-grid { width: 100%; border-collapse: collapse; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; margin-top: 10px; }\n"
                "  .meta-grid th { background-color: #1E293B; color: #38BDF8; font-weight: bold; text-align: left; padding: 10px 12px; border: 1px solid #334155; font-size: 11px; text-transform: uppercase; letter-spacing: 0.5px; }\n"
                "  .meta-grid td { padding: 9px 12px; border: 1px solid #CBD5E1; color: #0F172A; font-size: 12px; background-color: #FFFFFF; }\n"
                "  .meta-grid tr:nth-child(even) td { background-color: #F8FAFC; }\n"
                "</style>\n"
                "</head>\n"
                "<body>\n";

        html += "<table class=\"hdr-table\">\n";
        html += "  <tr>\n";
        html += "    <td colspan=\"16\" class=\"hdr-main\">BKR MATRIZ — PROJECT / COLLECTION: " + escHtml(projName) + " &nbsp;|&nbsp; CATALOG: " + escHtml(catCode) + "</td>\n";
        html += "  </tr>\n";
        html += "  <tr>\n";
        html += "    <td colspan=\"8\" class=\"hdr-sub\"><b>CATALOGING / BACKUP DATE:</b> " + escHtml(dateNow) + "</td>\n";
        html += "    <td colspan=\"8\" class=\"hdr-sub\"><b>TOTAL ASSETS:</b> " + juce::String(totalAssets) + " &nbsp;|&nbsp; <b>TOTAL SIZE:</b> " + juce::File::descriptionOfSizeInBytes(totalBytes) + "</td>\n";
        html += "  </tr>\n";
        html += "</table>\n";

        html += "<table class=\"meta-grid\">\n";
        html += "  <thead>\n";
        html += "    <tr>\n";
        html += "      <th>TITLE (dc.title)</th>\n";
        html += "      <th>CREATOR (dc.creator)</th>\n";
        html += "      <th>SUBJECT (dc.subject)</th>\n";
        html += "      <th>DESCRIPTION (dc.description)</th>\n";
        html += "      <th>PUBLISHER (dc.publisher)</th>\n";
        html += "      <th>CONTRIBUTOR (dc.contributor)</th>\n";
        html += "      <th>DATE CREATED (dc.created)</th>\n";
        html += "      <th>DATE ISSUED (dc.issued)</th>\n";
        html += "      <th>TYPE (dc.type)</th>\n";
        html += "      <th>FORMAT (dc.format)</th>\n";
        html += "      <th>IDENTIFIER (dc.identifier)</th>\n";
        html += "      <th>SOURCE (dc.source)</th>\n";
        html += "      <th>LANGUAGE (dc.language)</th>\n";
        html += "      <th>RELATION (dc.relation)</th>\n";
        html += "      <th>COVERAGE (dc.coverage)</th>\n";
        html += "      <th>RIGHTS (dc.rights)</th>\n";
        html += "    </tr>\n";
        html += "  </thead>\n";
        html += "  <tbody>\n";

        for (const auto& row : allRows) {
            html += "    <tr>\n";
            html += "      <td>" + escHtml(row.title) + "</td>\n";
            html += "      <td>" + escHtml(row.creator) + "</td>\n";
            html += "      <td>" + escHtml(row.subject) + "</td>\n";
            html += "      <td>" + escHtml(row.description) + "</td>\n";
            html += "      <td>" + escHtml(row.publisher) + "</td>\n";
            html += "      <td>" + escHtml(row.contributor) + "</td>\n";
            html += "      <td>" + escHtml(row.created) + "</td>\n";
            html += "      <td>" + escHtml(row.issued) + "</td>\n";
            html += "      <td>" + escHtml(row.type) + "</td>\n";
            html += "      <td>" + escHtml(row.format) + "</td>\n";
            html += "      <td>" + escHtml(row.identifier) + "</td>\n";
            html += "      <td>" + escHtml(row.source) + "</td>\n";
            html += "      <td>" + escHtml(row.language) + "</td>\n";
            html += "      <td>" + escHtml(row.relation) + "</td>\n";
            html += "      <td>" + escHtml(row.coverage) + "</td>\n";
            html += "      <td>" + escHtml(row.rights) + "</td>\n";
            html += "    </tr>\n";
        }

        html += "  </tbody>\n";
        html += "</table>\n";
        html += "</body>\n</html>";
        return html;
    }

    try { return preservation::exportarXlsXml(projeto_->registro(), itemIds, projName, catCode); }
    catch (...) { return {}; }
}

juce::String ProjetoAberto::exportarDublinCoreCsv(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto colecoes = listarColecoesLinkadas();
        juce::String csv;
        bool headerWritten = false;

        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = matriz::model::Project::resolverPastaProjeto(colDir).getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    std::vector<std::string> ids;
                    auto stmt = colDb.prepare("SELECT id FROM item ORDER BY codigo_acervo ASC, id ASC");
                    while (stmt.step()) ids.push_back(stmt.columnText(0));

                    juce::String part = preservation::exportarCsvDublinCore(colDb, ids);
                    if (!headerWritten) {
                        csv += part;
                        headerWritten = true;
                    } else {
                        int newlinePos = part.indexOfChar('\n');
                        if (newlinePos >= 0) csv += part.substring(newlinePos + 1);
                    }
                } catch (...) {}
            }
        }
        if (csv.isEmpty()) {
            return preservation::exportarCsvDublinCore(projeto_->registro(), itemIds);
        }
        return csv;
    }
    try { return preservation::exportarCsvDublinCore(projeto_->registro(), itemIds); }
    catch (...) { return {}; }
}

juce::String ProjetoAberto::exportarFixityManifest(const std::vector<std::string>& itemIds,
                                                     const std::string& algoritmo) const {
    if (!projeto_) return {};
    if (projeto_->modo() == matriz::model::Modo::Catalogo) {
        auto colecoes = listarColecoesLinkadas();
        juce::String manifest;
        for (const auto& c : colecoes) {
            if (!c.valido) continue;
            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = matriz::model::Project::resolverPastaProjeto(colDir).getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    std::vector<std::string> ids;
                    auto stmt = colDb.prepare("SELECT id FROM item");
                    while (stmt.step()) ids.push_back(stmt.columnText(0));
                    manifest += preservation::exportarFixityManifest(colDb, ids, algoritmo);
                } catch (...) {}
            }
        }
        if (manifest.isEmpty()) {
            return preservation::exportarFixityManifest(projeto_->registro(), itemIds, algoritmo);
        }
        return manifest;
    }
    try { return preservation::exportarFixityManifest(projeto_->registro(), itemIds, algoritmo); }
    catch (...) { return {}; }
}

void ProjetoAberto::aplicarRelinkEmMemoria(const std::string& arquivoId, const std::string& newPath) {
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        inMemoryRelinkedPaths_[arquivoId] = newPath;
    }
    dirty_ = true;
}

void ProjetoAberto::aplicarBatchRelinkEmMemoria(const std::map<std::string, std::string>& newPaths) {
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        for (const auto& [arqId, p] : newPaths) {
            inMemoryRelinkedPaths_[arqId] = p;
        }
    }
    dirty_ = true;
}

void ProjetoAberto::salvar() {
    if (!projeto_) return;
    try {
        std::map<std::string, std::string> paraGravar;
        {
            std::lock_guard<std::mutex> lock(marcacoesMutex_);
            paraGravar = inMemoryRelinkedPaths_;
        }

        auto& db = projeto_->registro();
        for (const auto& [arqId, newPath] : paraGravar) {
            auto stmt = db.prepare("UPDATE arquivo SET caminho_absoluto_origem = ?, atualizado_em = ? WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(newPath));
            stmt.bind(2, matriz::db::Value::of(matriz::model::agoraIso8601()));
            stmt.bind(3, matriz::db::Value::of(arqId));
            stmt.step();

            auto stmtLoc = db.prepare("INSERT OR IGNORE INTO localizacao_conhecida (id, arquivo_id, caminho_absoluto, criado_em) VALUES (?, ?, ?, ?)");
            stmtLoc.bind(1, matriz::db::Value::of(matriz::model::novoUuid()));
            stmtLoc.bind(2, matriz::db::Value::of(arqId));
            stmtLoc.bind(3, matriz::db::Value::of(newPath));
            stmtLoc.bind(4, matriz::db::Value::of(matriz::model::agoraIso8601()));
            stmtLoc.step();
        }

        db.run("PRAGMA wal_checkpoint(TRUNCATE)", {});
        projeto_->indice().run("PRAGMA wal_checkpoint(TRUNCATE)", {});

        {
            std::lock_guard<std::mutex> lock(marcacoesMutex_);
            inMemoryRelinkedPaths_.clear();
        }
        dirty_ = false;
    } catch (...) {}
}

void ProjetoAberto::descartarAlteracoesEmMemoria() {
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        inMemoryRelinkedPaths_.clear();
    }
    dirty_ = false;
}

std::optional<juce::File> ProjetoAberto::resolverArquivoComMemoria(const std::string& arquivoId) const {
    std::string pathEmMemoria;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        auto it = inMemoryRelinkedPaths_.find(arquivoId);
        if (it != inMemoryRelinkedPaths_.end()) pathEmMemoria = it->second;
    }
    if (!pathEmMemoria.empty()) {
        juce::File f(pathEmMemoria);
        if (f.existsAsFile()) return f;
    }
    if (!projeto_) return std::nullopt;
    return matriz::vault::resolverArquivo(projeto_->registro(), arquivoId, projeto_->pasta());
}

std::set<std::string>& ProjetoAberto::obterConjuntoMarcacao(TipoMarcacao tipo) {
    switch (tipo) {
        case TipoMarcacao::Html: return marcadosHtml_;
        case TipoMarcacao::Zip: return marcadosZip_;
        case TipoMarcacao::Print: return marcadosPrint_;
        case TipoMarcacao::Watermark: return marcadosWatermark_;
        case TipoMarcacao::Bounce: return marcadosBounce_;
        case TipoMarcacao::Rejeitado: return marcadosRejeitado_;
    }
    return marcadosHtml_;
}

const std::set<std::string>& ProjetoAberto::obterConjuntoMarcacao(TipoMarcacao tipo) const {
    switch (tipo) {
        case TipoMarcacao::Html: return marcadosHtml_;
        case TipoMarcacao::Zip: return marcadosZip_;
        case TipoMarcacao::Print: return marcadosPrint_;
        case TipoMarcacao::Watermark: return marcadosWatermark_;
        case TipoMarcacao::Bounce: return marcadosBounce_;
        case TipoMarcacao::Rejeitado: return marcadosRejeitado_;
    }
    return marcadosHtml_;
}

// obterConjuntoMarcacao() não tranca sozinha -- quem chama precisa segurar
// marcacoesMutex_ (ver comentário no membro, ProjetoAberto.h). É privada e
// só usada pelas funções abaixo, cada uma já tranca a sua parte.
void ProjetoAberto::registrarUndoMarcacao(TipoMarcacao tipo, std::vector<std::string> itemIds, std::set<std::string> marcados) {
    if (itemIds.empty() || desfazendo_) return;  // de outra thread, registrarUndo leva para a message thread
    const char* letra = tipo == TipoMarcacao::Html ? "H" : tipo == TipoMarcacao::Zip ? "K" : tipo == TipoMarcacao::Print ? "P"
                      : tipo == TipoMarcacao::Watermark ? "W" : tipo == TipoMarcacao::Bounce ? "B" : "R";
    // Persistente: uma ação "marcas" com quem tinha a marca antes; a volta passa por definirMarcacao (memória + fila).
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("tipo", static_cast<int>(tipo));
    juce::Array<juce::var> ids, marc;
    for (const auto& id : itemIds) ids.add(juce::String::fromUTF8(id.c_str()));
    for (const auto& id : marcados) marc.add(juce::String::fromUTF8(id.c_str()));
    o->setProperty("ids", ids);
    o->setProperty("marcados", marc);
    ParteUndo a;
    a.tipo = ParteUndo::Tipo::Acao;
    a.tabela = "marcas";
    a.onde = juce::JSON::toString(juce::var(o.get()), true).toStdString();
    registrarUndoPersistente(std::string("Mark ") + letra + (itemIds.size() == 1 ? "" : " (" + std::to_string(itemIds.size()) + " items)"), {a},
                             {}, "marcacao");
}

void ProjetoAberto::alternarMarcacao(TipoMarcacao tipo, const std::vector<std::string>& itemIds) {
    if (itemIds.empty()) return;
    bool remover = true;
    std::set<std::string> marcadosAntes;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        auto& s = obterConjuntoMarcacao(tipo);
        for (const auto& id : itemIds) if (s.count(id)) marcadosAntes.insert(id);
        for (const auto& id : itemIds) {
            if (s.find(id) == s.end()) {
                remover = false;
                break;
            }
        }
        for (const auto& id : itemIds) {
            if (remover) s.erase(id);
            else s.insert(id);
        }
        ++versaoMarcacoes_;
    }
    enfileirarGravacaoMarcacao(remover ? OpMarcacao::Remover : OpMarcacao::Inserir, tipo, itemIds);
    registrarUndoMarcacao(tipo, itemIds, std::move(marcadosAntes));
    EventBus::obterInstancia().dispararItensAlterados(itemIds, "marcacao");
}

void ProjetoAberto::definirMarcacao(TipoMarcacao tipo, const std::vector<std::string>& itemIds, bool marcado) {
    if (itemIds.empty()) return;
    std::set<std::string> marcadosAntes;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        auto& s = obterConjuntoMarcacao(tipo);
        for (const auto& id : itemIds) if (s.count(id)) marcadosAntes.insert(id);
        for (const auto& id : itemIds) {
            if (marcado) s.insert(id);
            else s.erase(id);
        }
        ++versaoMarcacoes_;
    }
    enfileirarGravacaoMarcacao(marcado ? OpMarcacao::Inserir : OpMarcacao::Remover, tipo, itemIds);
    registrarUndoMarcacao(tipo, itemIds, std::move(marcadosAntes));
    EventBus::obterInstancia().dispararItensAlterados(itemIds, "marcacao");
}

bool ProjetoAberto::contemMarcacao(TipoMarcacao tipo, const std::string& itemId) const {
    if (itemId.empty()) return false;
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    const auto& s = obterConjuntoMarcacao(tipo);
    return s.find(itemId) != s.end();
}

size_t ProjetoAberto::contarMarcacoes(TipoMarcacao tipo) const {
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    return obterConjuntoMarcacao(tipo).size();
}

void ProjetoAberto::limparMarcacoes(TipoMarcacao tipo) {
    std::vector<std::string> afetados;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        auto& s = obterConjuntoMarcacao(tipo);
        if (s.empty()) return;
        afetados.assign(s.begin(), s.end());
        s.clear();
        ++versaoMarcacoes_;
    }
    enfileirarGravacaoMarcacao(OpMarcacao::LimparTipo, tipo, {});
    registrarUndoMarcacao(tipo, afetados, std::set<std::string>(afetados.begin(), afetados.end()));
    EventBus::obterInstancia().dispararItensAlterados(afetados, "marcacao");
}

void ProjetoAberto::limparTodasMarcacoes() {
    // Os 4 conjuntos de uma vez e UM evento de lote com a união dos afetados.
    std::set<std::string> afetados;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        for (auto tipo : { TipoMarcacao::Html, TipoMarcacao::Zip, TipoMarcacao::Print, TipoMarcacao::Watermark, TipoMarcacao::Bounce, TipoMarcacao::Rejeitado }) {
            auto& s = obterConjuntoMarcacao(tipo);
            afetados.insert(s.begin(), s.end());
            s.clear();
        }
        ++versaoMarcacoes_;
    }
    enfileirarGravacaoMarcacao(OpMarcacao::LimparTodas, TipoMarcacao::Html, {});
    EventBus::obterInstancia().dispararItensAlterados(std::vector<std::string>(afetados.begin(), afetados.end()), "marcacao");
}

std::vector<std::string> ProjetoAberto::idsMarcados(TipoMarcacao tipo) const {
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    const auto& s = obterConjuntoMarcacao(tipo);
    return std::vector<std::string>(s.begin(), s.end());
}

void ProjetoAberto::transferirMarcacoes(const std::string& oldItemId, const std::string& newItemId) {
    if (oldItemId.empty() || newItemId.empty() || oldItemId == newItemId) return;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        for (auto tipo : { TipoMarcacao::Html, TipoMarcacao::Zip, TipoMarcacao::Print, TipoMarcacao::Watermark, TipoMarcacao::Bounce, TipoMarcacao::Rejeitado }) {
            auto& s = obterConjuntoMarcacao(tipo);
            if (s.erase(oldItemId) > 0) {
                s.insert(newItemId);
            }
        }
        ++versaoMarcacoes_;
    }
    enfileirarGravacaoMarcacao(OpMarcacao::Transferir, TipoMarcacao::Html, {oldItemId}, newItemId);
    EventBus::obterInstancia().dispararItemAlterado(newItemId, "marcacao");
}

namespace {
const char* nomeTipoMarcacao(int t) {
    switch (t) {
        case 0: return "html";
        case 1: return "zip";
        case 2: return "print";
        case 3: return "watermark";
        case 4: return "bounce";
        case 5: return "reject";
    }
    return "html";
}
}  // namespace

void ProjetoAberto::enfileirarGravacaoMarcacao(OpMarcacao op, TipoMarcacao tipo, std::vector<std::string> ids,
                                               std::string novoId) {
    if (!projeto_ || somenteLeitura_) return;  // clone somente-leitura: marcas valem só na sessão
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        ++gravacoesMarcasPendentes_;
    }
    const std::string nomeTipo = nomeTipoMarcacao(static_cast<int>(tipo));
    poolMarcas_.addJob([this, op, nomeTipo, ids = std::move(ids), novoId = std::move(novoId)]() {
        bool ok = true;
        try {
            auto& db = projeto_->registro();
            db.exec("BEGIN IMMEDIATE");  // uma transação por operação, nunca uma por item
            try {
                switch (op) {
                    case OpMarcacao::Inserir:
                        for (const auto& id : ids)
                            db.run("INSERT OR IGNORE INTO item_marcacao (item_id, tipo) "
                                   "SELECT ?, ? WHERE EXISTS (SELECT 1 FROM item WHERE id = ?)",
                                   {matriz::db::Value::of(id), matriz::db::Value::of(nomeTipo), matriz::db::Value::of(id)});
                        break;
                    case OpMarcacao::Remover:
                        for (const auto& id : ids)
                            db.run("DELETE FROM item_marcacao WHERE item_id = ? AND tipo = ?",
                                   {matriz::db::Value::of(id), matriz::db::Value::of(nomeTipo)});
                        break;
                    case OpMarcacao::LimparTipo:
                        db.run("DELETE FROM item_marcacao WHERE tipo = ?", {matriz::db::Value::of(nomeTipo)});
                        break;
                    case OpMarcacao::LimparTodas:
                        db.run("DELETE FROM item_marcacao", {});
                        break;
                    case OpMarcacao::Transferir:
                        for (const auto& id : ids)
                            db.run("UPDATE OR IGNORE item_marcacao SET item_id = ? WHERE item_id = ?",
                                   {matriz::db::Value::of(novoId), matriz::db::Value::of(id)});
                        break;
                }
                db.exec("COMMIT");
            } catch (...) {
                try { db.exec("ROLLBACK"); } catch (...) {}
                ok = false;
            }
        } catch (...) { ok = false; }
        if (!ok) matriz::diag::logarWatchdog("[Marcacao] falha ao gravar item_marcacao (" + juce::String(nomeTipo) + ")");
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        --gravacoesMarcasPendentes_;
    });
}

void ProjetoAberto::registrarSessaoPrint(const juce::String& nome, const std::vector<std::string>& itemIds) {
    if (!projeto_ || itemIds.empty() || nome.trim().isEmpty()) return;
    SessaoPrintRef ref;
    ref.id = matriz::model::novoUuid();
    ref.nome = nome.trim();
    ref.criadoEm = juce::String(matriz::model::agoraIso8601());
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        for (const auto& id : itemIds) sessoesPrintPorItem_[id].push_back(ref);
        ++versaoMarcacoes_;
        if (!somenteLeitura_) ++gravacoesMarcasPendentes_;
    }
    EventBus::obterInstancia().dispararItensAlterados(itemIds, "marcacao");  // grade repinta o selo
    if (somenteLeitura_) return;
    poolMarcas_.addJob([this, ref, itemIds]() {
        bool ok = true;
        try {
            auto& db = projeto_->registro();
            db.exec("BEGIN IMMEDIATE");
            try {
                db.run("INSERT INTO print_sessao (id, nome, criado_em, quantidade) VALUES (?, ?, ?, ?)",
                       {matriz::db::Value::of(ref.id), matriz::db::Value::of(ref.nome.toStdString()),
                        matriz::db::Value::of(ref.criadoEm.toStdString()),
                        matriz::db::Value::of(static_cast<long long>(itemIds.size()))});
                for (const auto& id : itemIds)
                    db.run("INSERT OR IGNORE INTO print_sessao_item (sessao_id, item_id) "
                           "SELECT ?, ? WHERE EXISTS (SELECT 1 FROM item WHERE id = ?)",
                           {matriz::db::Value::of(ref.id), matriz::db::Value::of(id), matriz::db::Value::of(id)});
                db.exec("COMMIT");
            } catch (...) {
                try { db.exec("ROLLBACK"); } catch (...) {}
                ok = false;
            }
        } catch (...) { ok = false; }
        if (ok) {
            try {
                juce::StringArray linhas;
                linhas.add("Session: " + ref.nome);
                linhas.add("Photos: " + juce::String(static_cast<int>(itemIds.size())));
                matriz::model::ProjectLog(projeto_->pasta()).appendEntry("Print Session", linhas);
            } catch (...) {}
        } else {
            matriz::diag::logarWatchdog("[Print] falha ao gravar a sessao de impressao");
        }
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        --gravacoesMarcasPendentes_;
    });
}

juce::String ProjetoAberto::descreverSessoesPrint(const std::vector<SessaoPrintRef>& sessoes) {
    juce::StringArray partes;
    for (const auto& s : sessoes) {
        const auto t = juce::Time::fromISO8601(s.criadoEm);
        partes.add(s.nome + " (" + t.formatted("%Y-%m-%d %H:%M") + ")");
    }
    return juce::String("Printed: ") + partes.joinIntoString("; ");
}

bool ProjetoAberto::itemImpresso(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    return sessoesPrintPorItem_.count(itemId) > 0;
}

std::vector<ProjetoAberto::SessaoPrintRef> ProjetoAberto::sessoesPrintDoItem(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    auto it = sessoesPrintPorItem_.find(itemId);
    return it == sessoesPrintPorItem_.end() ? std::vector<SessaoPrintRef>{} : it->second;
}

std::set<std::string> ProjetoAberto::idsImpressos() const {
    std::set<std::string> out;
    std::lock_guard<std::mutex> lock(marcacoesMutex_);
    for (const auto& par : sessoesPrintPorItem_) out.insert(par.first);
    return out;
}

void ProjetoAberto::carregarSessoesPrintDoBanco() {
    if (!projeto_) return;
    std::uint64_t versaoAntes = 0;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        versaoAntes = versaoMarcacoes_;
    }
    std::map<std::string, std::vector<SessaoPrintRef>> doBanco;
    try {
        auto st = leitura().prepare(
            "SELECT pi.item_id, s.id, s.nome, s.criado_em FROM print_sessao_item pi "
            "JOIN print_sessao s ON s.id = pi.sessao_id ORDER BY s.criado_em");
        while (st.step()) {
            SessaoPrintRef r;
            r.id = st.columnText(1);
            r.nome = juce::String::fromUTF8(st.columnText(2).c_str());
            r.criadoEm = juce::String(st.columnText(3));
            doBanco[st.columnText(0)].push_back(std::move(r));
        }
    } catch (...) { return; }
    std::vector<std::string> ids;
    for (const auto& par : doBanco) ids.push_back(par.first);
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        if (versaoMarcacoes_ != versaoAntes || gravacoesMarcasPendentes_ > 0) return;
        sessoesPrintPorItem_ = std::move(doBanco);
    }
    if (ids.empty()) return;
    std::weak_ptr<bool> vivo = vivo_;
    juce::MessageManager::callAsync([vivo, ids = std::move(ids)]() {
        if (vivo.expired()) return;
        EventBus::obterInstancia().dispararItensAlterados(ids, "marcacao");
    });
}

void ProjetoAberto::carregarMarcacoesDoBanco() {
    if (!projeto_) return;
    std::uint64_t versaoAntes = 0;
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        versaoAntes = versaoMarcacoes_;
    }
    std::set<std::string> h, z, p, w, b, rj;
    try {
        auto st = leitura().prepare("SELECT item_id, tipo FROM item_marcacao");
        while (st.step()) {
            const std::string tipo = st.columnText(1);
            if (tipo == "html") h.insert(st.columnText(0));
            else if (tipo == "zip") z.insert(st.columnText(0));
            else if (tipo == "print") p.insert(st.columnText(0));
            else if (tipo == "watermark") w.insert(st.columnText(0));
            else if (tipo == "bounce") b.insert(st.columnText(0));
            else if (tipo == "reject") rj.insert(st.columnText(0));
        }
    } catch (...) { return; }
    std::set<std::string> todos;
    todos.insert(h.begin(), h.end()); todos.insert(z.begin(), z.end());
    todos.insert(p.begin(), p.end()); todos.insert(w.begin(), w.end()); todos.insert(b.begin(), b.end());
    todos.insert(rj.begin(), rj.end());
    {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        if (versaoMarcacoes_ != versaoAntes || gravacoesMarcasPendentes_ > 0) return;  // a memória manda
        marcadosHtml_ = std::move(h);
        marcadosZip_ = std::move(z);
        marcadosPrint_ = std::move(p);
        marcadosWatermark_ = std::move(w);
        marcadosBounce_ = std::move(b);
        marcadosRejeitado_ = std::move(rj);
    }
    if (todos.empty()) return;
    std::weak_ptr<bool> vivo = vivo_;
    juce::MessageManager::callAsync([vivo, ids = std::vector<std::string>(todos.begin(), todos.end())]() {
        if (vivo.expired()) return;  // o grid já pintou antes da carga: avisa para repintar
        EventBus::obterInstancia().dispararItensAlterados(ids, "marcacao");
    });
}

void ProjetoAberto::salvarConfiguracaoWatermark(const ConfiguracaoWatermark& cfg) {
    if (!projeto_) return;
    juce::File f = projeto_->pasta().getChildFile("watermark.json");
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("caminhoLogo", cfg.caminhoLogo);
    obj->setProperty("opacidade", static_cast<double>(cfg.opacidade));
    obj->setProperty("escala", static_cast<double>(cfg.escala));
    obj->setProperty("margem", static_cast<double>(cfg.margem));
    obj->setProperty("posicaoIdH", cfg.posicaoIdH);
    obj->setProperty("customPosX_H", static_cast<double>(cfg.customPosX_H));
    obj->setProperty("customPosY_H", static_cast<double>(cfg.customPosY_H));
    obj->setProperty("posicaoIdV", cfg.posicaoIdV);
    obj->setProperty("customPosX_V", static_cast<double>(cfg.customPosX_V));
    obj->setProperty("customPosY_V", static_cast<double>(cfg.customPosY_V));

    juce::var v(obj.release());
    f.replaceWithText(juce::JSON::toString(v, true));
}

ConfiguracaoWatermark ProjetoAberto::obterConfiguracaoWatermark() const {
    if (!projeto_) return ConfiguracaoWatermark();
    return carregarConfiguracaoWatermarkDePasta(projeto_->pasta());
}

ConfiguracaoWatermark ProjetoAberto::carregarConfiguracaoWatermarkDePasta(const juce::File& pastaProjeto) {
    ConfiguracaoWatermark cfg;
    juce::File f = pastaProjeto.getChildFile("watermark.json");
    if (!f.existsAsFile()) return cfg;

    auto parsed = juce::JSON::parse(f);
    if (!parsed.isObject()) return cfg;

    auto* obj = parsed.getDynamicObject();
    if (!obj) return cfg;

    cfg.caminhoLogo = obj->getProperty("caminhoLogo").toString();
    if (obj->hasProperty("opacidade")) cfg.opacidade = static_cast<float>(static_cast<double>(obj->getProperty("opacidade")));
    if (obj->hasProperty("escala")) cfg.escala = static_cast<float>(static_cast<double>(obj->getProperty("escala")));
    if (obj->hasProperty("margem")) cfg.margem = static_cast<float>(static_cast<double>(obj->getProperty("margem")));
    if (obj->hasProperty("posicaoIdH")) cfg.posicaoIdH = static_cast<int>(obj->getProperty("posicaoIdH"));
    if (obj->hasProperty("customPosX_H")) cfg.customPosX_H = static_cast<float>(static_cast<double>(obj->getProperty("customPosX_H")));
    if (obj->hasProperty("customPosY_H")) cfg.customPosY_H = static_cast<float>(static_cast<double>(obj->getProperty("customPosY_H")));
    if (obj->hasProperty("posicaoIdV")) cfg.posicaoIdV = static_cast<int>(obj->getProperty("posicaoIdV"));
    if (obj->hasProperty("customPosX_V")) cfg.customPosX_V = static_cast<float>(static_cast<double>(obj->getProperty("customPosX_V")));
    if (obj->hasProperty("customPosY_V")) cfg.customPosY_V = static_cast<float>(static_cast<double>(obj->getProperty("customPosY_V")));

    return cfg;
}

// ============================================================================
// MAIN EDIT MODE (Fase 4)
// ============================================================================

bool ProjetoAberto::podeEditarMain() const {
    return projeto_ && projeto_->papel() == "ORIGINAL" && projeto_->modo() != matriz::model::Modo::Catalogo && mainExiste();
}

bool ProjetoAberto::nomeConfereComProjeto(const juce::String& digitado) const {
    if (!projeto_) return false;
    // Nome ATUAL (File > Rename Project pode ter mudado): compara com o que está no banco agora.
    const auto atual = juce::String::fromUTF8(projeto_->nome().c_str()).trim();
    return atual.isNotEmpty() && digitado.trim() == atual;
}

std::optional<ProjetoAberto::FonteRebuild> ProjetoAberto::fonteRebuild() const {
    if (!projeto_) return std::nullopt;
    try {
        auto st = projeto_->registro().prepare(
            "SELECT COALESCE(destination_id, id), destino_path FROM backup_destino WHERE papel = 'FONTE_REBUILD' LIMIT 1");
        if (!st.step()) return std::nullopt;
        FonteRebuild f;
        f.destinoId = st.columnText(0);
        f.raiz = juce::File(juce::String::fromUTF8(st.columnText(1).c_str()));
        return f;
    } catch (...) {
        return std::nullopt;
    }
}

juce::File ProjetoAberto::rebuildDestino() const {
    if (!projeto_) return {};
    try {
        auto st = projeto_->registro().prepare("SELECT COALESCE(rebuild_destino, '') FROM projeto LIMIT 1");
        if (st.step() && !st.columnText(0).empty()) return juce::File(juce::String::fromUTF8(st.columnText(0).c_str()));
    } catch (...) {}
    return {};
}

void ProjetoAberto::definirRebuildDestino(const juce::File& raiz) {
    if (!projeto_) return;
    projeto_->registro().run("UPDATE projeto SET rebuild_destino = ?",
                             {matriz::db::Value::of(raiz.getFullPathName().toStdString())});
}

// Entrar no modo = o MAIN atual vira fonte de leitura e o projeto fica sem MAIN. Só o banco muda;
// a pasta do MAIN antigo não é tocada. Itens, IDs, metadados e histórico seguem os mesmos.
bool ProjetoAberto::entrarModoEdicaoMain(const juce::String& nomeDigitado) {
    if (editandoMain_) return true;
    if (!podeEditarMain() || !nomeConfereComProjeto(nomeDigitado)) return false;
    auto& db = projeto_->registro();
    juce::String configAntiga;
    try {
        db.run("BEGIN IMMEDIATE", {});
        std::string idMain;
        {
            auto st = db.prepare("SELECT id FROM backup_destino WHERE papel = 'ORIGINAL' AND ativo = 1 LIMIT 1");
            if (st.step()) idMain = st.columnText(0);
        }
        if (!idMain.empty()) {
            db.run("UPDATE backup_destino SET papel = 'FONTE_REBUILD', ativo = 1 WHERE id = ?", {matriz::db::Value::of(idMain)});
        } else {
            // MAIN na raiz do projeto, nunca registrado como destino: registra já como fonte.
            const std::string agora = matriz::model::agoraIso8601();
            const std::string novoId = matriz::model::novoUuid();
            db.run("INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em) "
                   "VALUES (?, ?, ?, ?, 'FONTE_REBUILD', 1, ?)",
                   {matriz::db::Value::of(novoId), matriz::db::Value::of(projeto_->destinationId()),
                    matriz::db::Value::of(projeto_->raiz().getFullPathName().toStdString()),
                    matriz::db::Value::of(projeto_->raiz().getFileName().toStdString()), matriz::db::Value::of(agora)});
        }
        {
            auto sc = db.prepare("SELECT COALESCE(backup_config_main, '') FROM projeto LIMIT 1");
            if (sc.step()) configAntiga = juce::String::fromUTF8(sc.columnText(0).c_str());
        }
        // A estrutura do MAIN novo é decidida no Make Backup (como no primeiro backup).
        db.run("UPDATE projeto SET backup_config_main = NULL, rebuild_destino = ''", {});
        db.run("COMMIT", {});
    } catch (...) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        return false;
    }
    editandoMain_ = true;
    try {
        matriz::model::ProjectLog(projeto_->pasta()).appendEntry(
            "REBUILD DO MAIN: entered",
            {"The old MAIN is now read-only (FONTE_REBUILD): " + projeto_->raiz().getFullPathName(),
             "Old MAIN settings: " + configAntiga});
    } catch (...) {}
    if (aoMudarModoEdicaoMain) aoMudarModoEdicaoMain();
    return true;
}

// Não há saída sem concluir o rebuild (o modo persiste até o Make Backup terminar sem falhas).
void ProjetoAberto::sairModoEdicaoMain(const juce::String&) {}

juce::String ProjetoAberto::concluirRebuild(const juce::File& novaRaiz, const std::string& novoDestinationId) {
    if (!projeto_ || !editandoMain_) return "rebuild is not active";
    const auto fonte = fonteRebuild();
    if (!fonte) return "old MAIN (FONTE_REBUILD) not found";
    auto& db = projeto_->registro();
    const std::string chaveNova = matriz::consolidacao::chaveDestino(novaRaiz.getChildFile("Media"));
    const std::string agora = matriz::model::agoraIso8601();
    try {
        db.run("BEGIN IMMEDIATE", {});
        // Os registros do MAIN antigo (destino_id dele, ou legados sem destino) saem; ficam só os do novo.
        db.run("DELETE FROM consolidacao_registro WHERE COALESCE(destino_path, '') <> ? "
               "AND (COALESCE(destino_id, '') IN ('', ?))",
               {matriz::db::Value::of(chaveNova), matriz::db::Value::of(fonte->destinoId)});
        db.run("DELETE FROM backup_destino WHERE papel = 'FONTE_REBUILD'", {});
        auto st = db.prepare("SELECT id FROM backup_destino WHERE destino_path = ? LIMIT 1");
        st.bind(1, matriz::db::Value::of(novaRaiz.getFullPathName().toStdString()));
        if (st.step()) {
            db.run("UPDATE backup_destino SET papel = 'ORIGINAL', ativo = 1, destination_id = ?, desatualizado_rebuild = 0 WHERE id = ?",
                   {matriz::db::Value::of(novoDestinationId), matriz::db::Value::of(st.columnText(0))});
        } else {
            db.run("INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, "
                   "ultima_revisao_conhecida) VALUES (?, ?, ?, ?, 'ORIGINAL', 1, ?, ?)",
                   {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(novoDestinationId),
                    matriz::db::Value::of(novaRaiz.getFullPathName().toStdString()),
                    matriz::db::Value::of(novaRaiz.getFileName().toStdString()), matriz::db::Value::of(agora),
                    matriz::db::Value::of(static_cast<int64_t>(projeto_->revisao()))});
        }
        // Clones do MAIN antigo: desatualizados, sem sincronizar (criar clones novos do MAIN novo).
        db.run("UPDATE backup_destino SET desatualizado_rebuild = 1 WHERE papel = 'CLONE'", {});
        db.run("UPDATE projeto SET rebuild_destino = ''", {});
        db.run("COMMIT", {});
    } catch (const std::exception& e) {
        try { db.run("ROLLBACK", {}); } catch (...) {}
        return juce::String::fromUTF8(e.what());
    }
    editandoMain_ = false;
    try {
        matriz::model::ProjectLog(projeto_->pasta()).appendEntry(
            "REBUILD DO MAIN: completed",
            {"New MAIN: " + novaRaiz.getFullPathName(), "Old MAIN (now unlinked): " + fonte->raiz.getFullPathName(),
             "Clones of the old MAIN are stale: create new clones from the new MAIN."});
    } catch (...) {}
    if (aoMudarModoEdicaoMain) aoMudarModoEdicaoMain();
    return {};
}

void ProjetoAberto::tocarAtividadeMain() { ultimaAtividadeMainMs_ = juce::Time::currentTimeMillis(); }

bool ProjetoAberto::verificarTimeoutEdicaoMain() { return false; }  // rebuild não expira: só termina concluído

int ProjetoAberto::contarArquivosDaPastaNoMain(const std::string& pastaId) const {
    if (!projeto_) return 0;
    try {
        const auto pref = matriz::consolidacao::caminhoFisicoDaPasta(leitura(), pastaId);
        if (pref.isEmpty()) return 0;
        const std::string prefixo = (pref + "/").toStdString();
        auto st = leitura().prepare(
            "SELECT COUNT(*) FROM consolidacao_registro WHERE substr(caminho_relativo_destino, 1, ?) = ?");
        st.bind(1, matriz::db::Value::of(static_cast<long long>(pref.length() + 1)));
        st.bind(2, matriz::db::Value::of(prefixo));
        return st.step() ? static_cast<int>(st.columnInt(0)) : 0;
    } catch (...) {
        return 0;
    }
}

void ProjetoAberto::mapaMoverItemDoMain(const std::string& itemId, const std::string& pastaDe, const std::string& pastaPara) {
    if (!projeto_ || pastaPara.empty()) return;
    if (!pastaDe.empty())
        projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND pasta_id = ?",
                                  {matriz::db::Value::of(itemId), matriz::db::Value::of(pastaDe)});
    inserirItemPastaInterno(itemId, pastaPara, matriz::model::agoraIso8601());
}

matriz::mainedit::ContextoMain ProjetoAberto::contextoDoMain() const {
    matriz::mainedit::ContextoMain c;
    c.registro = &projeto_->registro();
    c.raiz = matriz::model::normalizarParaRaizDestino(projeto_->raiz());
    c.pastaProjeto = projeto_->pasta();
    c.destinoId = projeto_->destinationId();
    c.mainUsaMapa = mainUsaMapa();
    auto* self = const_cast<ProjetoAberto*>(this);
    c.mapaMoverItem = [self](const std::string& item, const std::string& de, const std::string& para) {
        self->mapaMoverItemDoMain(item, de, para);
    };
    return c;
}

void ProjetoAberto::executarConsolidacaoEmSegundoPlano(std::function<void()> trabalho) {
    consolidacoesEmCurso_.fetch_add(1);
    poolMainEdit_.addJob([this, trabalho = std::move(trabalho)]() mutable {
        try {
            trabalho();
        } catch (...) {}
        trabalho = nullptr;  // solta o que o job capturou antes de liberar o fechamento do projeto
        consolidacoesEmCurso_.fetch_sub(1);
    });
}

void ProjetoAberto::executarEdicaoMain(const juce::String& titulo, std::function<matriz::mainedit::Resultado()> trabalho,
                                       AoConcluirEdicaoMain aoConcluir) {
    auto falhar = [&](const std::string& msg) {
        matriz::mainedit::Resultado r;
        r.erro = msg;
        if (aoConcluir) aoConcluir(r);
    };
    if (!editandoMain_) { falhar("MAIN EDIT MODE is not active"); return; }
    bool esperado = false;
    if (!operacaoMainEmCurso_.compare_exchange_strong(esperado, true)) { falhar("another MAIN operation is still running"); return; }
    tocarAtividadeMain();
    ProgressoGlobal::obterInstancia().iniciarTarefa("main_edit", titulo, 0, nullptr, titulo + "...");
    std::weak_ptr<bool> vivo = vivo_;
    poolMainEdit_.addJob([this, vivo, trabalho = std::move(trabalho), aoConcluir = std::move(aoConcluir), titulo]() {
        matriz::mainedit::Resultado r;
        try {
            r = trabalho();
        } catch (const std::exception& e) {
            r.ok = false;
            r.erro = e.what();
        }
        juce::MessageManager::callAsync([this, vivo, r, aoConcluir, titulo]() {
            auto vivoAgora = vivo.lock();
            if (!vivoAgora) return;  // o projeto foi fechado no meio
            operacaoMainEmCurso_.store(false);
            tocarAtividadeMain();
            ProgressoGlobal::obterInstancia().concluirTarefa(
                "main_edit", r.ok ? titulo + ": done" : titulo + ": failed - " + juce::String::fromUTF8(r.erro.c_str()));
            if (aoConcluir) aoConcluir(r);
        });
    });
}

void ProjetoAberto::editarMainRenomearArquivo(const std::string& registroId, const juce::String& novoNome,
                                              AoConcluirEdicaoMain aoConcluir, bool registrarUndoDesta) {
    auto ctx = contextoDoMain();
    executarEdicaoMain("Renaming file in MAIN", [ctx, registroId, novoNome] {
        return matriz::mainedit::renomearArquivo(ctx, registroId, novoNome);
    }, [this, registroId, aoConcluir, registrarUndoDesta](const matriz::mainedit::Resultado& r) {
        if (r.ok && registrarUndoDesta) {
            const auto nomeAntes = juce::File(r.deRel).getFileName();
            registrarUndo("Rename file in MAIN", [this, registroId, nomeAntes]() {
                editarMainRenomearArquivo(registroId, nomeAntes, nullptr, false);
            });
        }
        if (aoConcluir) aoConcluir(r);
    });
}

void ProjetoAberto::editarMainMoverArquivo(const std::string& registroId, const std::string& novaPastaId,
                                           AoConcluirEdicaoMain aoConcluir, bool registrarUndoDesta) {
    auto ctx = contextoDoMain();
    std::string pastaAntes;
    {
        auto st = projeto_->registro().prepare("SELECT pasta_id FROM consolidacao_registro WHERE id = ?");
        st.bind(1, matriz::db::Value::of(registroId));
        if (st.step()) pastaAntes = st.columnText(0);
    }
    executarEdicaoMain("Moving file in MAIN", [ctx, registroId, novaPastaId] {
        return matriz::mainedit::moverArquivo(ctx, registroId, novaPastaId);
    }, [this, registroId, aoConcluir, registrarUndoDesta, pastaAntes](const matriz::mainedit::Resultado& r) {
        if (r.ok && registrarUndoDesta && !pastaAntes.empty()) {
            registrarUndo("Move file in MAIN", [this, registroId, pastaAntes]() {
                editarMainMoverArquivo(registroId, pastaAntes, nullptr, false);
            });
        }
        if (aoConcluir) aoConcluir(r);
    });
}

void ProjetoAberto::editarMainSubstituirArquivo(const std::string& registroId, const juce::File& novaVersao,
                                                AoConcluirEdicaoMain aoConcluir) {
    auto ctx = contextoDoMain();
    executarEdicaoMain("Replacing file in MAIN", [ctx, registroId, novaVersao] {
        return matriz::mainedit::substituirArquivo(ctx, registroId, novaVersao);
    }, std::move(aoConcluir));
}

void ProjetoAberto::editarMainDeletarArquivo(const std::string& registroId, AoConcluirEdicaoMain aoConcluir) {
    auto ctx = contextoDoMain();
    executarEdicaoMain("Moving file to quarantine", [ctx, registroId] {
        return matriz::mainedit::deletarArquivo(ctx, registroId);
    }, std::move(aoConcluir));
}

void ProjetoAberto::editarMainRestaurar(const std::string& quarentenaId, const juce::String& destinoAlternativoRel,
                                        AoConcluirEdicaoMain aoConcluir) {
    auto ctx = contextoDoMain();
    executarEdicaoMain("Restoring from quarantine", [ctx, quarentenaId, destinoAlternativoRel] {
        return matriz::mainedit::restaurar(ctx, quarentenaId, destinoAlternativoRel);
    }, std::move(aoConcluir));
}

void ProjetoAberto::editarMainEsvaziarQuarentena(std::function<void(const matriz::mainedit::ResultadoEsvaziar&)> aoConcluir) {
    auto ctx = contextoDoMain();
    auto resultado = std::make_shared<matriz::mainedit::ResultadoEsvaziar>();
    executarEdicaoMain("Emptying quarantine", [ctx, resultado] {
        *resultado = matriz::mainedit::esvaziarQuarentena(ctx, [](int feito, int total) {
            juce::MessageManager::callAsync([feito, total] {
                ProgressoGlobal::obterInstancia().atualizarFracao("main_edit", static_cast<double>(feito) / std::max(1, total),
                                                                   juce::String(feito) + " / " + juce::String(total));
            });
            return true;
        });
        matriz::mainedit::Resultado r;
        r.ok = resultado->falhas.empty() && !resultado->cancelado;
        if (!r.ok && !resultado->falhas.empty()) r.erro = resultado->falhas.front();
        return r;
    }, [resultado, aoConcluir](const matriz::mainedit::Resultado&) {
        if (aoConcluir) aoConcluir(*resultado);
    });
}

void ProjetoAberto::converterMainParaFolderMap(
    std::function<void(const juce::String& erro, const juce::String& nomeDoMapa)> aoConcluir) {
    if (!editandoMain_ || !projeto_) { if (aoConcluir) aoConcluir("MAIN EDIT MODE is not active", {}); return; }
    if (mainUsaMapa()) { if (aoConcluir) aoConcluir("The MAIN already uses a folder map", {}); return; }
    auto nomeDoMapa = std::make_shared<juce::String>();
    executarEdicaoMain("Converting MAIN to folder map", [this, nomeDoMapa]() {
        matriz::mainedit::Resultado r;
        auto& db = projeto_->registro();
        struct Linha { std::string id, item; juce::String caminho; };
        std::vector<Linha> linhas;
        {
            auto st = db.prepare("SELECT id, item_id, caminho_relativo_destino FROM consolidacao_registro");
            while (st.step()) linhas.push_back({st.columnText(0), st.columnText(1), juce::String::fromUTF8(st.columnText(2).c_str())});
        }
        if (linhas.empty()) { r.erro = "the MAIN has no registered files"; return r; }

        // Pastas reais: todo prefixo de diretório dos caminhos registrados.
        std::set<juce::String> dirs;
        for (auto& l : linhas) {
            juce::StringArray partes;
            partes.addTokens(l.caminho, "/", "");
            juce::String acumulado;
            for (int i = 0; i < partes.size() - 1; ++i) {
                acumulado += (acumulado.isEmpty() ? "" : "/") + partes[i];
                if (partes[i].isNotEmpty()) dirs.insert(acumulado);
            }
        }
        std::vector<juce::String> ordenadas(dirs.begin(), dirs.end());
        std::sort(ordenadas.begin(), ordenadas.end(), [](const juce::String& a, const juce::String& b) {
            const int da = a.retainCharacters("/").length(), dbb = b.retainCharacters("/").length();
            return da != dbb ? da < dbb : a < b;
        });

        const juce::String nome = "MAIN structure";
        std::string mapaId = criarFolderMap(nome, std::nullopt);
        if (mapaId.empty()) { r.erro = "could not create the folder map"; return r; }
        *nomeDoMapa = nome;

        matriz::db::Database::Trava trava(db);
        db.exec("BEGIN IMMEDIATE");
        try {
            std::map<juce::String, std::string> idPorDir;
            for (auto& dir : ordenadas) {
                const juce::String pai = dir.contains("/") ? dir.upToLastOccurrenceOf("/", false, false) : juce::String();
                const juce::String seg = dir.contains("/") ? dir.fromLastOccurrenceOf("/", false, false) : dir;
                std::optional<std::string> paiId;
                if (pai.isNotEmpty()) paiId = idPorDir[pai];
                idPorDir[dir] = criarPastaAcervo(seg.toStdString(), paiId, mapaId);
            }
            const auto agora = matriz::model::agoraIso8601();
            for (auto& l : linhas) {
                if (!l.caminho.contains("/")) continue;  // raiz de Media/: fica em NO FOLDER
                const auto pastaId = idPorDir[l.caminho.upToLastOccurrenceOf("/", false, false)];
                if (pastaId.empty()) continue;
                inserirItemPastaInterno(l.item, pastaId, agora);
                db.run("UPDATE consolidacao_registro SET pasta_id = ? WHERE id = ?",
                       {matriz::db::Value::of(pastaId), matriz::db::Value::of(l.id)});
            }
            // A partir daqui o MAIN segue as regras "com folder map".
            juce::var cfg;
            {
                auto st = db.prepare("SELECT COALESCE(backup_config_main, '') FROM projeto LIMIT 1");
                if (st.step()) cfg = juce::JSON::parse(juce::String::fromUTF8(st.columnText(0).c_str()));
            }
            if (!cfg.isObject()) cfg = juce::var(new juce::DynamicObject());
            auto* o = cfg.getDynamicObject();
            o->setProperty("preservar", false);
            o->setProperty("mapa", true);
            o->setProperty("org", 1);
            o->setProperty("por_source", false);
            o->setProperty("mapa_id", juce::String(mapaId));
            db.run("UPDATE projeto SET backup_config_main = ?", {matriz::db::Value::of(juce::JSON::toString(cfg, true).toStdString())});
            db.exec("COMMIT");
        } catch (...) {
            try { db.exec("ROLLBACK"); } catch (...) {}
            throw;
        }
        try {
            matriz::model::ProjectLog(projeto_->pasta()).appendEntry(
                "MAIN EDIT: MAIN converted to folder map",
                {"New folder map: " + nome, "Folders created: " + juce::String(static_cast<int>(ordenadas.size())),
                 "Automatic organization by year/type/source no longer applies; new items go to _SEM_PASTA until organized."});
        } catch (...) {}
        r.ok = true;
        return r;
    }, [nomeDoMapa, aoConcluir](const matriz::mainedit::Resultado& r) {
        if (aoConcluir) aoConcluir(r.ok ? juce::String() : juce::String::fromUTF8(r.erro.c_str()), *nomeDoMapa);
    });
}

void ProjetoAberto::recuperarOperacoesDoMain() {
    if (!projeto_ || projeto_->papel() != "ORIGINAL" || projeto_->modo() == matriz::model::Modo::Catalogo) return;
    auto ctx = contextoDoMain();
    std::weak_ptr<bool> vivo = vivo_;
    poolMainEdit_.addJob([ctx, vivo]() {
        auto res = matriz::mainedit::recuperarJournal(ctx);
        if (res.conflitos.empty()) return;
        juce::MessageManager::callAsync([vivo, res]() {
            if (!vivo.lock()) return;
            juce::String msg = matriz::i18n::t("main_edit.recuperacao_conflito");
            for (size_t i = 0; i < res.conflitos.size() && i < 5; ++i) msg << "\n\n" << juce::String::fromUTF8(res.conflitos[i].c_str());
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                                             .withTitle(matriz::i18n::t("main_edit.titulo"))
                                             .withMessage(msg)
                                             .withButton(matriz::i18n::t("dialogo.ok")),
                                         juce::ModalCallbackFunction::create([](int) {}));
        });
    });
}

} // namespace matriz::ui
